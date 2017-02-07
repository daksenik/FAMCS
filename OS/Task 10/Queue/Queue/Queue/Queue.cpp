/******************************************************************************
Module:  Queue.cpp
Notices: Copyright (c) 2000 Jeffrey Richter
******************************************************************************/
//Modified by a BSU fellow

#include "CmnHdr.h" /* See Appendix A. *///For chBEGINTHREADEX which is very like _beginthreadex
#include <windowsx.h>
#include <tchar.h>
#include <process.h>       // For _beginthreadex
#include "Resource.h"
#include <iostream>
#include <time.h>
#include <queue>

///////////////////////////////////////////////////////////////////////////////


class CQueue {
public:
   struct ELEMENT {
      int m_nThreadNum, m_nRequestNum;
	  long addTime;
      // Other element data should go here
   };
   typedef ELEMENT* PELEMENT;

private:
	CRITICAL_SECTION criticalSection;
	PELEMENT m_pElements;        // Array of elements to be processed
	int      m_nMaxElements;     // Maximum # of elements in the array
	HANDLE   m_hsemNumElements;

public:
   CQueue(int nMaxElements);
   ~CQueue();

   BOOL Append(PELEMENT pElement, DWORD dwMilliseconds);
   BOOL Remove(PELEMENT pElement, DWORD dwMilliseconds);
};


///////////////////////////////////////////////////////////////////////////////


CQueue::CQueue(int nMaxElements) 
{
	if (!InitializeCriticalSectionAndSpinCount(&criticalSection, 0x0400)) {
		MessageBox(NULL, L"Critical section creating failed", L"Queue critical section", MB_OK | MB_ICONEXCLAMATION);
		exit(0);
	}
   m_pElements = (PELEMENT) 
   HeapAlloc(GetProcessHeap(), 0, sizeof(ELEMENT) * nMaxElements);
   m_nMaxElements = nMaxElements;
   m_hsemNumElements = CreateSemaphore(NULL, 0, nMaxElements, NULL);
}


///////////////////////////////////////////////////////////////////////////////


CQueue::~CQueue() {
	DeleteCriticalSection(&criticalSection);
	CloseHandle(m_hsemNumElements);
	HeapFree(GetProcessHeap(), 0, m_pElements);
}


///////////////////////////////////////////////////////////////////////////////


BOOL CQueue::Append(PELEMENT pElement, DWORD dwTimeout) {

   BOOL fOk = FALSE;
   //DWORD dw = WaitForSingleObject(m_hmtxQ, dwTimeout);
   EnterCriticalSection(&criticalSection);

	// This thread has exclusive access to the queue

    // Increment the number of elements in the queue
    LONG lPrevCount;
	pElement->addTime = clock();
    fOk = ReleaseSemaphore(m_hsemNumElements, 1, &lPrevCount);
    if (fOk) {
		// The queue is not full, append the new element
		m_pElements[lPrevCount] = *pElement;
    } else {

		// The queue is full, set the error code and return failure
         SetLastError(ERROR_DATABASE_FULL);
    }

      // Allow other threads to access the queue

	LeaveCriticalSection(&criticalSection);

	return(fOk);   // Call GetLastError for more info
}


///////////////////////////////////////////////////////////////////////////////


BOOL CQueue::Remove(PELEMENT pElement, DWORD dwTimeout) {

   // Wait both for exclusive access to queue 
   //      and  for queue to have an element.

	EnterCriticalSection(&criticalSection);

	BOOL fOk = WaitForSingleObject(m_hsemNumElements, dwTimeout);

	if (!fOk) {
		SetLastError(ERROR_TIMEOUT);
		LeaveCriticalSection(&criticalSection);
		return FALSE;
	}

    // The queue has an element, pull it from the queue
    *pElement = m_pElements[0];
	pElement->addTime = clock() - pElement->addTime;

    // Shift the remaining elements down
    MoveMemory(&m_pElements[0], &m_pElements[1], sizeof(ELEMENT) * (m_nMaxElements - 1));

    // Allow other threads to access the queue
	LeaveCriticalSection(&criticalSection);

   return true;   // Call GetLastError for more info
}


///////////////////////////////////////////////////////////////////////////////


CQueue g_q(10);                     // The shared queue
volatile BOOL g_fShutdown = FALSE;  // Signals client/server threads to die
HWND g_hwnd;                        // How client/server threads give status


// Handles to all client/server threads & number of client/server threads
HANDLE g_hThreads[MAXIMUM_WAIT_OBJECTS];  
int    g_nNumThreads = 0;


///////////////////////////////////////////////////////////////////////////////


DWORD WINAPI ClientThread(PVOID pvParam) {

   int nThreadNum = PtrToUlong(pvParam);
   HWND hwndLB = GetDlgItem(g_hwnd, IDC_CLIENTS);

   for (int nRequestNum = 1; !g_fShutdown; nRequestNum++) {

      TCHAR sz[1024];
      CQueue::ELEMENT e = { nThreadNum, nRequestNum };

      // Try to put an element on the queue
      if (g_q.Append(&e, 200)) {

         // Indicate which thread sent it and which request
         wsprintf(sz, TEXT("Sending %d:%d"), nThreadNum, nRequestNum);
      } else {

         // Couldn't put an element on the queue
         wsprintf(sz, TEXT("Sending %d:%d (%s)"), nThreadNum, nRequestNum,
			 (GetLastError() == ERROR_TIMEOUT) ? TEXT("timeout") : TEXT("full"));
      }
	  
      // Show result of appending element
      ListBox_SetCurSel(hwndLB, ListBox_AddString(hwndLB, sz));
      Sleep(2500);   // Wait before appending another element
   }
   
   return(0);
}


///////////////////////////////////////////////////////////////////////////////


DWORD WINAPI ServerThread(PVOID pvParam) {

   int nThreadNum = PtrToUlong(pvParam);
   HWND hwndLB = GetDlgItem(g_hwnd, IDC_SERVERS);

   while (!g_fShutdown) {

      TCHAR sz[1024];
      CQueue::ELEMENT e;

      // Try to get an element from the queue
      if (g_q.Remove(&e, 5000)) {

         // Indicate which thread is processing it, which thread
         // sent it and which request we're processing
         wsprintf(sz, TEXT("%d: Processing %d:%d [ %d ]"), 
            nThreadNum, e.m_nThreadNum, e.m_nRequestNum,e.addTime);

         // The server takes some time to process the request
         Sleep(2000 * e.m_nThreadNum);

      } else {
         // Couldn't get an element from the queue
		  wsprintf(sz, TEXT("%d: (timeout)"), nThreadNum);
      }

      // Show result of processing element
      ListBox_SetCurSel(hwndLB, ListBox_AddString(hwndLB, sz));
   }

   return(0);
}


///////////////////////////////////////////////////////////////////////////////


BOOL Dlg_OnInitDialog(HWND hwnd, HWND hwndFocus, LPARAM lParam) {

   chSETDLGICONS(hwnd, IDI_QUEUE);

   g_hwnd = hwnd; // Used by client/server threads to show status

   DWORD dwThreadID;

   // Create the client threads
   for (int x = 0; x < 4; x++)
      g_hThreads[g_nNumThreads++] = 
         chBEGINTHREADEX(NULL, 0, ClientThread, (PVOID) (INT_PTR) x, 
            0, &dwThreadID);

   // Create the server threads
   for (int x = 0; x < 2; x++)
      g_hThreads[g_nNumThreads++] = 
         chBEGINTHREADEX(NULL, 0, ServerThread, (PVOID) (INT_PTR) x, 
            0, &dwThreadID);

   return(TRUE);
}


///////////////////////////////////////////////////////////////////////////////


void Dlg_OnCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify) {

   switch (id) {
      case IDCANCEL:
         EndDialog(hwnd, id);
         break;
   }
}

void Dlg_OnClose(HWND hwnd)
{
	for (int i = 0; i < g_nNumThreads; i++)TerminateThread(g_hThreads[i], 2);
	EndDialog(hwnd, 0);
}


///////////////////////////////////////////////////////////////////////////////


INT_PTR WINAPI Dlg_Proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
   
   switch (uMsg) {
	   chHANDLE_DLGMSG(hwnd, WM_CLOSE, Dlg_OnClose);
		chHANDLE_DLGMSG(hwnd, WM_INITDIALOG, Dlg_OnInitDialog);
		chHANDLE_DLGMSG(hwnd, WM_COMMAND,    Dlg_OnCommand);
   }
   return(FALSE);
}


///////////////////////////////////////////////////////////////////////////////


int WINAPI _tWinMain(HINSTANCE hinstExe, HINSTANCE, PTSTR pszCmdLine, int) {

   DialogBox(hinstExe, MAKEINTRESOURCE(IDD_QUEUE), NULL,(DLGPROC) Dlg_Proc);

   InterlockedExchange((LPLONG) &g_fShutdown, (LONG) TRUE);
/*   LONG InterlockedExchange(
  LPLONG Target,
               // pointer to the 32-bit value to exchange
  LONG Value   // new value for the LONG value pointed to by Target
);
*/ 

   // Wait for all the threads to terminate & then cleanup
   WaitForMultipleObjects(g_nNumThreads, g_hThreads, TRUE, INFINITE);
   while (g_nNumThreads--)
      CloseHandle(g_hThreads[g_nNumThreads]);

   return(0);
}


//////////////////////////////// End of File //////////////////////////////////
