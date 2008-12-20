/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      Project independend shared buffer (linear + circular)

  Description:  Implementation of platform specific part for the
                shared buffer
                (Implementation for Win32)

  License:

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:

    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

    3. Neither the name of SYSTEC electronic GmbH nor the names of its
       contributors may be used to endorse or promote products derived
       from this software without prior written permission. For written
       permission, please contact info@systec-electronic.com.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
    FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
    COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
    BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
    LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
    ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.

    Severability Clause:

        If a provision of this License is or becomes illegal, invalid or
        unenforceable in any jurisdiction, that shall not affect:
        1. the validity or enforceability in that jurisdiction of any other
           provision of this License; or
        2. the validity or enforceability in other jurisdictions of that or
           any other provision of this License.

  -------------------------------------------------------------------------

  2006/06/27 -rs:   V 1.00 (initial version)

****************************************************************************/

#define WINVER       0x0400	// #defines necessary for usage of
#define _WIN32_WINNT 0x0400	// function <SignalObjectAndWait>

#include <windows.h>
#include <stdio.h>
#include "global.h"
#include "sharedbuff.h"
#include "shbipc.h"

/***************************************************************************/
/*                                                                         */
/*                                                                         */
/*          G L O B A L   D E F I N I T I O N S                            */
/*                                                                         */
/*                                                                         */
/***************************************************************************/

#if (!defined(SHBIPC_INLINED)) || defined(SHBIPC_INLINE_ENABLED)

//---------------------------------------------------------------------------
//  Configuration
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
//  Constant definitions
//---------------------------------------------------------------------------

#define MAX_LEN_BUFFER_ID       MAX_PATH

#define IDX_EVENT_NEW_DATA      0
#define IDX_EVENT_TERM_REQU     1
#define IDX_EVENT_TERM_RESP     2

#define NAME_MUTEX_BUFF_ACCESS  "BuffAccess"
#define NAME_EVENT_NEW_DATA     "NewData"
#define NAME_EVENT_TERM_REQU    "TermRequ"
#define NAME_EVENT_TERM_RESP    "TermResp"
#define NAME_EVENT_JOB_READY    "JobReady"

#define TIMEOUT_ENTER_ATOMIC    1000	// for debgging: INFINITE
#define TIMEOUT_TERM_THREAD     2000

#define SBI_MAGIC_ID            0x5342492B	// magic ID ("SBI+")
#define SBH_MAGIC_ID            0x5342482A	// magic ID ("SBH*")

//---------------------------------------------------------------------------
//  Local types
//---------------------------------------------------------------------------

// This structure is the common header for the shared memory region used
// by all processes attached this shared memory. It includes common
// information to administrate/manage the shared buffer from a couple of
// separated processes (e.g. the refernce counter). This structure is
// located at the start of the shared memory region itself and exists
// consequently only one times per shared memory instance.
typedef struct {
	unsigned long m_SbhMagicID;	// magic ID ("SBH*")
	unsigned long m_ulShMemSize;
	unsigned long m_ulRefCount;
	char m_szBufferID[MAX_LEN_BUFFER_ID];

#ifndef NDEBUG
	unsigned long m_ulOwnerProcID;
#endif

} tShbMemHeader;

// This structure is the "external entry point" from a separate process
// to get access to a shared buffer. This structure includes all platform
// resp. target specific information to administrate/manage the shared
// buffer from a separate process. Every process attached to the shared
// buffer has its own runtime instance of this structure with its individual
// runtime data (e.g. the scope of an event handle is limitted to the
// owner process only). The structure member <m_pShbMemHeader> points
// to the (process specific) start address of the shared memory region
// itself.
typedef struct {
	unsigned long m_SbiMagicID;	// magic ID ("SBI+")
	HANDLE m_hSharedMem;
	HANDLE m_hMutexBuffAccess;
	HANDLE m_hThreadNewData;	// thraed to signal that new data are available
	HANDLE m_ahEventNewData[3];	// IDX_EVENT_NEW_DATA + IDX_EVENT_TERM_REQU + ID_EVENT_TERM_RESP
	tSigHndlrNewData m_pfnSigHndlrNewData;
	HANDLE m_hThreadJobReady;	// thread to signal that a job/operation is ready now (e.g. reset buffer)
	HANDLE m_hEventJobReady;
	unsigned long m_ulTimeOutJobReady;
	tSigHndlrJobReady m_pfnSigHndlrJobReady;
	tShbMemHeader *m_pShbMemHeader;

#ifndef NDEBUG
	unsigned long m_ulThreadIDNewData;
	unsigned long m_ulThreadIDJobReady;
#endif

} tShbMemInst;

//---------------------------------------------------------------------------
//  Global variables
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
//  Local variables
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
//  Prototypes of internal functions
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
//  Get pointer to process local information structure
//---------------------------------------------------------------------------

INLINE_FUNCTION tShbMemInst *ShbIpcGetShbMemInst(tShbInstance pShbInstance_p)
{

	tShbMemInst *pShbMemInst;

	pShbMemInst = (tShbMemInst *) pShbInstance_p;
	ASSERT(pShbMemInst->m_SbiMagicID == SBI_MAGIC_ID);

	return (pShbMemInst);

}

//---------------------------------------------------------------------------
//  Get pointer to shared memory header
//---------------------------------------------------------------------------

INLINE_FUNCTION tShbMemHeader *ShbIpcGetShbMemHeader(tShbInstance
						     pShbInstance_p)
{

	tShbMemInst *pShbMemInst;
	tShbMemHeader *pShbMemHeader;

	pShbMemInst = ShbIpcGetShbMemInst(pShbInstance_p);
	pShbMemHeader = pShbMemInst->m_pShbMemHeader;
	ASSERT(pShbMemHeader->m_SbhMagicID == SBH_MAGIC_ID);

	return (pShbMemHeader);

}

// not inlined internal functions
DWORD WINAPI ShbIpcThreadSignalNewData(LPVOID pvThreadParam_p);
DWORD WINAPI ShbIpcThreadSignalJobReady(LPVOID pvThreadParam_p);
const char *ShbIpcGetUniformObjectName(const char *pszEventJobName_p,
				       const char *pszBufferID_p,
				       BOOL fGlobalObject_p);

#endif

#if !defined(SHBIPC_INLINE_ENABLED)
// true internal functions (not inlined)
static void *ShbIpcAllocPrivateMem(unsigned long ulMemSize_p);
static void ShbIpcReleasePrivateMem(void *pMem_p);
#endif

//=========================================================================//
//                                                                         //
//          P U B L I C   F U N C T I O N S                                //
//                                                                         //
//=========================================================================//

#if !defined(SHBIPC_INLINE_ENABLED)
// not inlined external functions

//---------------------------------------------------------------------------
//  Initialize IPC for Shared Buffer Module
//---------------------------------------------------------------------------

tShbError ShbIpcInit(void)
{

	return (kShbOk);

}

//---------------------------------------------------------------------------
//  Deinitialize IPC for Shared Buffer Module
//---------------------------------------------------------------------------

tShbError ShbIpcExit(void)
{

	return (kShbOk);

}

//---------------------------------------------------------------------------
//  Allocate Shared Buffer
//---------------------------------------------------------------------------

tShbError ShbIpcAllocBuffer(unsigned long ulBufferSize_p,
			    const char *pszBufferID_p,
			    tShbInstance * ppShbInstance_p,
			    unsigned int *pfShbNewCreated_p)
{

	HANDLE hSharedMem;
	LPVOID pSharedMem;
	unsigned long ulShMemSize;
	tShbMemInst *pShbMemInst;
	tShbMemHeader *pShbMemHeader;
	tShbInstance pShbInstance;
	unsigned int fShMemNewCreated;
	const char *pszObjectName;
	HANDLE hMutexBuffAccess;
	HANDLE hEventNewData;
	HANDLE hEventJobReady;
	tShbError ShbError;

	ulShMemSize = ulBufferSize_p + sizeof(tShbMemHeader);
	pSharedMem = NULL;
	pShbInstance = NULL;
	fShMemNewCreated = FALSE;
	ShbError = kShbOk;

	//---------------------------------------------------------------
	// (1) open an existing or create a new shared memory
	//---------------------------------------------------------------
	// try to open an already existing shared memory
	// (created by an another process)
	hSharedMem = OpenFileMapping(FILE_MAP_ALL_ACCESS,	// DWORD dwDesiredAccess
				     FALSE,	// BOOL bInheritHandle
				     pszBufferID_p);	// LPCTSTR lpName
	if (hSharedMem != NULL) {
		// a shared memory already exists
		fShMemNewCreated = FALSE;
	} else {
		// it seams that this process is the first who wants to use the
		// shared memory, so it has to create a new shared memory
		hSharedMem = CreateFileMapping(INVALID_HANDLE_VALUE,	// HANDLE hFile
					       NULL,	// LPSECURITY_ATTRIBUTES lpAttributes
					       PAGE_READWRITE,	// DWORD flProtect
					       0,	// DWORD dwMaximumSizeHigh
					       ulShMemSize,	// DWORD dwMaximumSizeLow
					       pszBufferID_p);	// LPCTSTR lpName

		fShMemNewCreated = TRUE;
	}

	if (hSharedMem == NULL) {
		ShbError = kShbOutOfMem;
		goto Exit;
	}

	//---------------------------------------------------------------
	// (2) get the pointer to the shared memory
	//---------------------------------------------------------------
	pSharedMem = MapViewOfFile(hSharedMem,	// HANDLE hFileMappingObject
				   FILE_MAP_ALL_ACCESS,	// DWORD dwDesiredAccess,
				   0,	// DWORD dwFileOffsetHigh,
				   0,	// DWORD dwFileOffsetLow,
				   ulShMemSize);	// SIZE_T dwNumberOfBytesToMap

	if (pSharedMem == NULL) {
		ShbError = kShbOutOfMem;
		goto Exit;
	}

	//---------------------------------------------------------------
	// (3) setup or update header and management information
	//---------------------------------------------------------------
	pShbMemHeader = (tShbMemHeader *) pSharedMem;

	// allocate a memory block from process specific mempool to save
	// process local information to administrate/manage the shared buffer
	pShbMemInst =
	    (tShbMemInst *) ShbIpcAllocPrivateMem(sizeof(tShbMemInst));
	if (pShbMemInst == NULL) {
		ShbError = kShbOutOfMem;
		goto Exit;
	}
	// reset complete header to default values
	pShbMemInst->m_SbiMagicID = SBI_MAGIC_ID;
	pShbMemInst->m_hSharedMem = hSharedMem;
	pShbMemInst->m_hMutexBuffAccess = INVALID_HANDLE_VALUE;
	pShbMemInst->m_hThreadNewData = INVALID_HANDLE_VALUE;
	pShbMemInst->m_ahEventNewData[IDX_EVENT_NEW_DATA] =
	    INVALID_HANDLE_VALUE;
	pShbMemInst->m_ahEventNewData[IDX_EVENT_TERM_REQU] =
	    INVALID_HANDLE_VALUE;
	pShbMemInst->m_ahEventNewData[IDX_EVENT_TERM_RESP] =
	    INVALID_HANDLE_VALUE;
	pShbMemInst->m_pfnSigHndlrNewData = NULL;
	pShbMemInst->m_hThreadJobReady = INVALID_HANDLE_VALUE;
	pShbMemInst->m_hEventJobReady = INVALID_HANDLE_VALUE;
	pShbMemInst->m_ulTimeOutJobReady = 0;
	pShbMemInst->m_pfnSigHndlrJobReady = NULL;
	pShbMemInst->m_pShbMemHeader = pShbMemHeader;

#ifndef NDEBUG
	{
		pShbMemInst->m_ulThreadIDNewData = 0;
		pShbMemInst->m_ulThreadIDJobReady = 0;
	}
#endif

	// create mutex for buffer access
	pszObjectName =
	    ShbIpcGetUniformObjectName(NAME_MUTEX_BUFF_ACCESS, pszBufferID_p,
				       TRUE);
	hMutexBuffAccess = CreateMutex(NULL,	// LPSECURITY_ATTRIBUTES lpMutexAttributes
				       FALSE,	// BOOL bInitialOwner
				       pszObjectName);	// LPCTSTR lpName
	pShbMemInst->m_hMutexBuffAccess = hMutexBuffAccess;
	ASSERT(pShbMemInst->m_hMutexBuffAccess != NULL);

	// The EventNewData is used for signaling of new data after a write
	// operation (SetEvent) as well as for waiting for new data on the
	// reader side (WaitForMultipleObjects). Because it's not known if
	// this process will be read or write data, the event will be
	// always created here.
	pszObjectName =
	    ShbIpcGetUniformObjectName(NAME_EVENT_NEW_DATA, pszBufferID_p,
				       TRUE);
	hEventNewData = CreateEvent(NULL,	// LPSECURITY_ATTRIBUTES lpEventAttributes
				    FALSE,	// BOOL bManualReset
				    FALSE,	// BOOL bInitialState
				    pszObjectName);	// LPCTSTR lpName
	pShbMemInst->m_ahEventNewData[IDX_EVENT_NEW_DATA] = hEventNewData;
	ASSERT(pShbMemInst->m_ahEventNewData[IDX_EVENT_NEW_DATA] != NULL);

	// The EventJobReady is used for signaling that a job is done (SetEvent)
	// as well as for waiting for finishing of a job (WaitForMultipleObjects).
	// Because it's not known if this process will signal or wait, the event
	// will be always created here.
	pszObjectName =
	    ShbIpcGetUniformObjectName(NAME_EVENT_JOB_READY, pszBufferID_p,
				       TRUE);
	hEventJobReady = CreateEvent(NULL,	// LPSECURITY_ATTRIBUTES lpEventAttributes
				     FALSE,	// BOOL bManualReset
				     FALSE,	// BOOL bInitialState
				     pszObjectName);	// LPCTSTR lpName
	pShbMemInst->m_hEventJobReady = hEventJobReady;
	ASSERT(pShbMemInst->m_hEventJobReady != NULL);

	if (fShMemNewCreated) {
		// this process was the first who wanted to use the shared memory,
		// so a new shared memory was created
		// -> setup new header information inside the shared memory region
		//    itself
		pShbMemHeader->m_SbhMagicID = SBH_MAGIC_ID;
		pShbMemHeader->m_ulShMemSize = ulShMemSize;
		pShbMemHeader->m_ulRefCount = 1;
		strncpy(pShbMemHeader->m_szBufferID, pszBufferID_p,
			sizeof(pShbMemHeader->m_szBufferID) - 1);

#ifndef NDEBUG
		{
			pShbMemHeader->m_ulOwnerProcID = GetCurrentProcessId();
		}
#endif
	} else {
		// any other process has created the shared memory and this
		// process has only attached to it
		// -> check and update existing header information inside the
		//    shared memory region itself
		if (pShbMemHeader->m_ulShMemSize != ulShMemSize) {
			ShbError = kShbOpenMismatch;
			goto Exit;
		}
#ifndef NDEBUG
		{
			if (strncmp
			    (pShbMemHeader->m_szBufferID, pszBufferID_p,
			     sizeof(pShbMemHeader->m_szBufferID) - 1)) {
				ShbError = kShbOpenMismatch;
				goto Exit;
			}
		}
#endif

		pShbMemHeader->m_ulRefCount++;
	}

	// set abstarct "handle" for returning to application
	pShbInstance = (tShbInstance *) pShbMemInst;

      Exit:

	if (ShbError != kShbOk) {
		if (pShbMemInst != NULL) {
			ShbIpcReleasePrivateMem(pShbMemInst);
		}
		if (pSharedMem != NULL) {
			UnmapViewOfFile(pSharedMem);
		}
		if (hSharedMem != NULL) {
			CloseHandle(hSharedMem);
		}
	}

	*pfShbNewCreated_p = fShMemNewCreated;
	*ppShbInstance_p = pShbInstance;

	return (ShbError);

}

//---------------------------------------------------------------------------
//  Release Shared Buffer
//---------------------------------------------------------------------------

tShbError ShbIpcReleaseBuffer(tShbInstance pShbInstance_p)
{

	tShbMemInst *pShbMemInst;
	tShbMemHeader *pShbMemHeader;
	HANDLE hEventNewData;
	HANDLE hMutexBuffAccess;
	tShbError ShbError;
	tShbError ShbError2;

	if (pShbInstance_p == NULL) {
		return (kShbOk);
	}

	pShbMemInst = ShbIpcGetShbMemInst(pShbInstance_p);
	pShbMemHeader = ShbIpcGetShbMemHeader(pShbInstance_p);

	if (!--pShbMemHeader->m_ulRefCount) {
		ShbError = kShbOk;
	} else {
		ShbError = kShbMemUsedByOtherProcs;
	}

	ShbError2 = ShbIpcStopSignalingNewData(pShbInstance_p);
	hEventNewData = pShbMemInst->m_ahEventNewData[IDX_EVENT_NEW_DATA];
	if (hEventNewData != INVALID_HANDLE_VALUE) {
		CloseHandle(hEventNewData);
		pShbMemInst->m_ahEventNewData[IDX_EVENT_NEW_DATA] =
		    INVALID_HANDLE_VALUE;
	}

	hMutexBuffAccess = pShbMemInst->m_hMutexBuffAccess;
	if (hMutexBuffAccess != INVALID_HANDLE_VALUE) {
		CloseHandle(hMutexBuffAccess);
		pShbMemInst->m_hMutexBuffAccess = INVALID_HANDLE_VALUE;
	}

	UnmapViewOfFile(pShbMemHeader);
	if (pShbMemInst->m_hSharedMem != INVALID_HANDLE_VALUE) {
		CloseHandle(pShbMemInst->m_hSharedMem);
		pShbMemInst->m_hSharedMem = INVALID_HANDLE_VALUE;
	}

	ShbIpcReleasePrivateMem(pShbMemInst);

	if (ShbError == kShbOk) {
		ShbError = ShbError2;
	}

	return (ShbError);

}

#endif // !defined(SHBIPC_INLINE_ENABLED)

#if (!defined(SHBIPC_INLINED)) || defined(SHBIPC_INLINE_ENABLED)

//---------------------------------------------------------------------------
//  Enter atomic section for Shared Buffer access
//---------------------------------------------------------------------------

INLINE_FUNCTION tShbError ShbIpcEnterAtomicSection(tShbInstance pShbInstance_p)
{

	tShbMemInst *pShbMemInst;
	HANDLE hMutexBuffAccess;
	DWORD dwWaitResult;
	tShbError ShbError;

	if (pShbInstance_p == NULL) {
		return (kShbInvalidArg);
	}

	pShbMemInst = ShbIpcGetShbMemInst(pShbInstance_p);
	ShbError = kShbOk;

	hMutexBuffAccess = pShbMemInst->m_hMutexBuffAccess;
	if (hMutexBuffAccess != INVALID_HANDLE_VALUE) {
		dwWaitResult =
		    WaitForSingleObject(hMutexBuffAccess, TIMEOUT_ENTER_ATOMIC);
		switch (dwWaitResult) {
		case WAIT_OBJECT_0 + 0:
			{
				break;
			}

		case WAIT_TIMEOUT:
			{
				TRACE0
				    ("\nShbIpcEnterAtomicSection(): WAIT_TIMEOUT");
				ASSERT(0);
				ShbError = kShbBufferInvalid;
				break;
			}

		case WAIT_ABANDONED:
			{
				TRACE0
				    ("\nShbIpcEnterAtomicSection(): WAIT_ABANDONED");
				ASSERT(0);
				ShbError = kShbBufferInvalid;
				break;
			}

		case WAIT_FAILED:
			{
				TRACE1
				    ("\nShbIpcEnterAtomicSection(): WAIT_FAILED -> LastError=%ld",
				     GetLastError());
				ASSERT(0);
				ShbError = kShbBufferInvalid;
				break;
			}

		default:
			{
				TRACE1
				    ("\nShbIpcEnterAtomicSection(): unknown error -> LastError=%ld",
				     GetLastError());
				ASSERT(0);
				ShbError = kShbBufferInvalid;
				break;
			}
		}
	} else {
		ShbError = kShbBufferInvalid;
	}

	return (ShbError);

}

//---------------------------------------------------------------------------
//  Leave atomic section for Shared Buffer access
//---------------------------------------------------------------------------

INLINE_FUNCTION tShbError ShbIpcLeaveAtomicSection(tShbInstance pShbInstance_p)
{

	tShbMemInst *pShbMemInst;
	HANDLE hMutexBuffAccess;
	BOOL fRes;
	tShbError ShbError;

	if (pShbInstance_p == NULL) {
		return (kShbInvalidArg);
	}

	pShbMemInst = ShbIpcGetShbMemInst(pShbInstance_p);
	ShbError = kShbOk;

	hMutexBuffAccess = pShbMemInst->m_hMutexBuffAccess;
	if (hMutexBuffAccess != INVALID_HANDLE_VALUE) {
		fRes = ReleaseMutex(hMutexBuffAccess);
		ASSERT(fRes);
	} else {
		ShbError = kShbBufferInvalid;
	}

	return (ShbError);

}

//---------------------------------------------------------------------------
//  Start signaling of new data (called from reading process)
//---------------------------------------------------------------------------

INLINE_FUNCTION tShbError ShbIpcStartSignalingNewData(tShbInstance
						      pShbInstance_p,
						      tSigHndlrNewData
						      pfnSignalHandlerNewData_p,
						      tShbPriority
						      ShbPriority_p)
{

	tShbMemInst *pShbMemInst;
	tShbMemHeader *pShbMemHeader;
	const char *pszObjectName;
	HANDLE hEventTermRequ;
	HANDLE hEventTermResp;
	HANDLE hThreadNewData;
	unsigned long ulThreadIDNewData;
	tShbError ShbError;
	int iPriority;

	if ((pShbInstance_p == NULL) || (pfnSignalHandlerNewData_p == NULL)) {
		return (kShbInvalidArg);
	}

	pShbMemInst = ShbIpcGetShbMemInst(pShbInstance_p);
	pShbMemHeader = ShbIpcGetShbMemHeader(pShbInstance_p);
	ShbError = kShbOk;

	if ((pShbMemInst->m_hThreadNewData != INVALID_HANDLE_VALUE) ||
	    (pShbMemInst->m_ahEventNewData[IDX_EVENT_TERM_REQU] !=
	     INVALID_HANDLE_VALUE)
	    || (pShbMemInst->m_ahEventNewData[IDX_EVENT_TERM_RESP] !=
		INVALID_HANDLE_VALUE)
	    || (pShbMemInst->m_pfnSigHndlrNewData != NULL)) {
		ShbError = kShbAlreadySignaling;
		goto Exit;
	}

	pShbMemInst->m_pfnSigHndlrNewData = pfnSignalHandlerNewData_p;

	// Because the event <pShbMemInst->m_ahEventNewData[IDX_EVENT_NEW_DATA]>
	// is used for signaling of new data after a write operation too (using
	// SetEvent), it is always created here (see <ShbIpcAllocBuffer>).

	pszObjectName =
	    ShbIpcGetUniformObjectName(NAME_EVENT_TERM_REQU,
				       pShbMemHeader->m_szBufferID, FALSE);
	hEventTermRequ = CreateEvent(NULL,	// LPSECURITY_ATTRIBUTES lpEventAttributes
				     FALSE,	// BOOL bManualReset
				     FALSE,	// BOOL bInitialState
				     pszObjectName);	// LPCTSTR lpName
	pShbMemInst->m_ahEventNewData[IDX_EVENT_TERM_REQU] = hEventTermRequ;
	ASSERT(pShbMemInst->m_ahEventNewData[IDX_EVENT_TERM_REQU] != NULL);

	pszObjectName =
	    ShbIpcGetUniformObjectName(NAME_EVENT_TERM_RESP,
				       pShbMemHeader->m_szBufferID, FALSE);
	hEventTermResp = CreateEvent(NULL,	// LPSECURITY_ATTRIBUTES lpEventAttributes
				     FALSE,	// BOOL bManualReset
				     FALSE,	// BOOL bInitialState
				     pszObjectName);	// LPCTSTR lpName
	pShbMemInst->m_ahEventNewData[IDX_EVENT_TERM_RESP] = hEventTermResp;
	ASSERT(pShbMemInst->m_ahEventNewData[IDX_EVENT_TERM_RESP] != NULL);

	hThreadNewData = CreateThread(NULL,	// LPSECURITY_ATTRIBUTES lpThreadAttributes
				      0,	// SIZE_T dwStackSize
				      ShbIpcThreadSignalNewData,	// LPTHREAD_START_ROUTINE lpStartAddress
				      pShbInstance_p,	// LPVOID lpParameter
				      0,	// DWORD dwCreationFlags
				      &ulThreadIDNewData);	// LPDWORD lpThreadId

	switch (ShbPriority_p) {
	case kShbPriorityLow:
		iPriority = THREAD_PRIORITY_BELOW_NORMAL;
		break;

	case kShbPriorityNormal:
		iPriority = THREAD_PRIORITY_NORMAL;
		break;

	case kshbPriorityHigh:
		iPriority = THREAD_PRIORITY_ABOVE_NORMAL;
		break;

	}

	ASSERT(pShbMemInst->m_hThreadNewData != NULL);

	SetThreadPriority(hThreadNewData, iPriority);

	pShbMemInst->m_hThreadNewData = hThreadNewData;

#ifndef NDEBUG
	{
		pShbMemInst->m_ulThreadIDNewData = ulThreadIDNewData;
	}
#endif

      Exit:

	return (ShbError);

}

//---------------------------------------------------------------------------
//  Stop signaling of new data (called from reading process)
//---------------------------------------------------------------------------

INLINE_FUNCTION tShbError ShbIpcStopSignalingNewData(tShbInstance
						     pShbInstance_p)
{

	tShbMemInst *pShbMemInst;
	HANDLE hEventTermRequ;
	HANDLE hEventTermResp;
	DWORD dwWaitResult;

	if (pShbInstance_p == NULL) {
		return (kShbInvalidArg);
	}

	pShbMemInst = ShbIpcGetShbMemInst(pShbInstance_p);

	// terminate new data signaling thread
	// (set event <hEventTermRequ> to wakeup the thread and dispose it
	// to exit, then wait for confirmation using event <hEventTermResp>)
	hEventTermRequ = pShbMemInst->m_ahEventNewData[IDX_EVENT_TERM_REQU];
	hEventTermResp = pShbMemInst->m_ahEventNewData[IDX_EVENT_TERM_RESP];
	if ((hEventTermRequ != INVALID_HANDLE_VALUE) &&
	    (hEventTermResp != INVALID_HANDLE_VALUE)) {
		TRACE0("\nShbIpcStopSignalingNewData(): enter wait state");
		dwWaitResult = SignalObjectAndWait(hEventTermRequ,	// HANDLE hObjectToSignal
						   hEventTermResp,	// HANDLE hObjectToWaitOn
						   TIMEOUT_TERM_THREAD,	// DWORD dwMilliseconds
						   FALSE);	// BOOL bAlertable
		TRACE0
		    ("\nShbIpcStopSignalingNewData(): wait state leaved: ---> ");
		switch (dwWaitResult) {
		case WAIT_OBJECT_0 + 0:	// event "new data signaling thread terminated"
			{
				TRACE0("Event = WAIT_OBJECT_0+0");
				break;
			}

		default:
			{
				TRACE0("Unhandled Event");
				ASSERT(0);
				break;
			}
		}
	}

	if (pShbMemInst->m_hThreadNewData != INVALID_HANDLE_VALUE) {
		CloseHandle(pShbMemInst->m_hThreadNewData);
		pShbMemInst->m_hThreadNewData = INVALID_HANDLE_VALUE;
	}

	if (pShbMemInst->m_ahEventNewData[IDX_EVENT_TERM_REQU] !=
	    INVALID_HANDLE_VALUE) {
		CloseHandle(pShbMemInst->m_ahEventNewData[IDX_EVENT_TERM_REQU]);
		pShbMemInst->m_ahEventNewData[IDX_EVENT_TERM_REQU] =
		    INVALID_HANDLE_VALUE;
	}

	if (pShbMemInst->m_ahEventNewData[IDX_EVENT_TERM_RESP] !=
	    INVALID_HANDLE_VALUE) {
		CloseHandle(pShbMemInst->m_ahEventNewData[IDX_EVENT_TERM_RESP]);
		pShbMemInst->m_ahEventNewData[IDX_EVENT_TERM_RESP] =
		    INVALID_HANDLE_VALUE;
	}

	pShbMemInst->m_pfnSigHndlrNewData = NULL;

	return (kShbOk);

}

//---------------------------------------------------------------------------
//  Signal new data (called from writing process)
//---------------------------------------------------------------------------

INLINE_FUNCTION tShbError ShbIpcSignalNewData(tShbInstance pShbInstance_p)
{

	tShbMemInst *pShbMemInst;
	HANDLE hEventNewData;
	BOOL fRes;

	// TRACE0("\nShbIpcSignalNewData(): enter\n");

	if (pShbInstance_p == NULL) {
		return (kShbInvalidArg);
	}

	pShbMemInst = ShbIpcGetShbMemInst(pShbInstance_p);

	ASSERT(pShbMemInst->m_ahEventNewData[IDX_EVENT_NEW_DATA] !=
	       INVALID_HANDLE_VALUE);
	hEventNewData = pShbMemInst->m_ahEventNewData[IDX_EVENT_NEW_DATA];
	if (hEventNewData != INVALID_HANDLE_VALUE) {
		fRes = SetEvent(hEventNewData);
		// TRACE1("\nShbIpcSignalNewData(): EventNewData set (Result=%d)\n", (int)fRes);
		ASSERT(fRes);
	}
	// TRACE0("\nShbIpcSignalNewData(): leave\n");
	return (kShbOk);

}

//---------------------------------------------------------------------------
//  Start signaling for job ready (called from waiting process)
//---------------------------------------------------------------------------

INLINE_FUNCTION tShbError ShbIpcStartSignalingJobReady(tShbInstance
						       pShbInstance_p,
						       unsigned long
						       ulTimeOut_p,
						       tSigHndlrJobReady
						       pfnSignalHandlerJobReady_p)
{

	tShbMemInst *pShbMemInst;
	tShbMemHeader *pShbMemHeader;
	HANDLE hThreadJobReady;
	unsigned long ulThreadIDJobReady;
	tShbError ShbError;

	if ((pShbInstance_p == NULL) || (pfnSignalHandlerJobReady_p == NULL)) {
		return (kShbInvalidArg);
	}

	pShbMemInst = ShbIpcGetShbMemInst(pShbInstance_p);
	pShbMemHeader = ShbIpcGetShbMemHeader(pShbInstance_p);
	ShbError = kShbOk;

	if ((pShbMemInst->m_hThreadJobReady != INVALID_HANDLE_VALUE) ||
	    (pShbMemInst->m_pfnSigHndlrJobReady != NULL)) {
		ShbError = kShbAlreadySignaling;
		goto Exit;
	}

	pShbMemInst->m_ulTimeOutJobReady = ulTimeOut_p;
	pShbMemInst->m_pfnSigHndlrJobReady = pfnSignalHandlerJobReady_p;

	// Because the event <pShbMemInst->m_ahEventJobReady> is used for
	// signaling of a finished job too (using SetEvent), it is always
	// created here (see <ShbIpcAllocBuffer>).

	hThreadJobReady = CreateThread(NULL,	// LPSECURITY_ATTRIBUTES lpThreadAttributes
				       0,	// SIZE_T dwStackSize
				       ShbIpcThreadSignalJobReady,	// LPTHREAD_START_ROUTINE lpStartAddress
				       pShbInstance_p,	// LPVOID lpParameter
				       0,	// DWORD dwCreationFlags
				       &ulThreadIDJobReady);	// LPDWORD lpThreadId

	pShbMemInst->m_hThreadJobReady = hThreadJobReady;
	ASSERT(pShbMemInst->m_hThreadJobReady != NULL);

#ifndef NDEBUG
	{
		pShbMemInst->m_ulThreadIDJobReady = ulThreadIDJobReady;
	}
#endif

      Exit:

	return (ShbError);

}

//---------------------------------------------------------------------------
//  Signal job ready (called from executing process)
//---------------------------------------------------------------------------

INLINE_FUNCTION tShbError ShbIpcSignalJobReady(tShbInstance pShbInstance_p)
{

	tShbMemInst *pShbMemInst;
	HANDLE hEventJobReady;
	BOOL fRes;

	// TRACE0("\nShbIpcSignalJobReady(): enter\n");

	if (pShbInstance_p == NULL) {
		return (kShbInvalidArg);
	}

	pShbMemInst = ShbIpcGetShbMemInst(pShbInstance_p);

	ASSERT(pShbMemInst->m_hEventJobReady != INVALID_HANDLE_VALUE);
	hEventJobReady = pShbMemInst->m_hEventJobReady;
	if (hEventJobReady != INVALID_HANDLE_VALUE) {
		fRes = SetEvent(hEventJobReady);
		// TRACE1("\nShbIpcSignalJobReady(): EventJobReady set (Result=%d)\n", (int)fRes);
		ASSERT(fRes);
	}
	// TRACE0("\nShbIpcSignalJobReady(): leave\n");
	return (kShbOk);

}

//---------------------------------------------------------------------------
//  Get pointer to common used share memory area
//---------------------------------------------------------------------------

INLINE_FUNCTION void *ShbIpcGetShMemPtr(tShbInstance pShbInstance_p)
{

	tShbMemHeader *pShbMemHeader;
	void *pShbShMemPtr;

	pShbMemHeader = ShbIpcGetShbMemHeader(pShbInstance_p);
	if (pShbMemHeader != NULL) {
		pShbShMemPtr = (BYTE *) pShbMemHeader + sizeof(tShbMemHeader);
	} else {
		pShbShMemPtr = NULL;
	}

	return (pShbShMemPtr);

}

#endif

//=========================================================================//
//                                                                         //
//          P R I V A T E   F U N C T I O N S                              //
//                                                                         //
//=========================================================================//

#if !defined(SHBIPC_INLINE_ENABLED)

//---------------------------------------------------------------------------
//  Allocate a memory block from process specific mempool
//---------------------------------------------------------------------------

static void *ShbIpcAllocPrivateMem(unsigned long ulMemSize_p)
{

	HGLOBAL hMem;
	void *pMem;

	hMem = GlobalAlloc(GMEM_FIXED, ulMemSize_p + sizeof(HGLOBAL));
	pMem = GlobalLock(hMem);
	if (pMem != NULL) {
		*(HGLOBAL *) pMem = hMem;
		(BYTE *) pMem += sizeof(HGLOBAL);
	}

#ifndef NDEBUG
	{
		memset(pMem, 0xaa, ulMemSize_p);
	}
#endif

	return (pMem);

}

//---------------------------------------------------------------------------
//  Release a memory block from process specific mempool
//---------------------------------------------------------------------------

static void ShbIpcReleasePrivateMem(void *pMem_p)
{

	HGLOBAL hMem;

	if (pMem_p == NULL) {
		return;
	}

	(BYTE *) pMem_p -= sizeof(HGLOBAL);
	hMem = *(HGLOBAL *) pMem_p;

	GlobalUnlock(hMem);
	GlobalFree(hMem);

	return;

}

//---------------------------------------------------------------------------
//  Create uniform object name (needed for inter-process communication)
//---------------------------------------------------------------------------

const char *ShbIpcGetUniformObjectName(const char *pszObjectJobName_p,
				       const char *pszBufferID_p,
				       BOOL fGlobalObject_p)
{

	static char szObjectName[MAX_PATH];
	char szObjectPrefix[MAX_PATH];

	if (fGlobalObject_p) {
		strncpy(szObjectPrefix, "Global\\", sizeof(szObjectPrefix));
	} else {
		_snprintf(szObjectPrefix, sizeof(szObjectPrefix), "PID%08lX_",
			  (unsigned long)GetCurrentProcessId());
	}

	_snprintf(szObjectName, sizeof(szObjectName), "%s%s#%s",
		  szObjectPrefix, pszBufferID_p, pszObjectJobName_p);

	return (szObjectName);

}

//---------------------------------------------------------------------------
//  Thread for new data signaling
//---------------------------------------------------------------------------

DWORD WINAPI ShbIpcThreadSignalNewData(LPVOID pvThreadParam_p)
{

	tShbInstance pShbInstance;
	tShbMemInst *pShbMemInst;
	DWORD dwWaitResult;
	BOOL fTermRequ;
	int fCallAgain;

	TRACE1
	    ("\nShbIpcThreadSignalNewData(): SignalThread started (pShbInstance=0x%08lX)\n",
	     (DWORD) pvThreadParam_p);

	pShbInstance = (tShbMemInst *) pvThreadParam_p;
	pShbMemInst = ShbIpcGetShbMemInst(pShbInstance);
	fTermRequ = FALSE;

	do {
		ASSERT((pShbMemInst->m_ahEventNewData[0] !=
			INVALID_HANDLE_VALUE)
		       && (pShbMemInst->m_ahEventNewData[0] != NULL));
		ASSERT((pShbMemInst->m_ahEventNewData[1] !=
			INVALID_HANDLE_VALUE)
		       && (pShbMemInst->m_ahEventNewData[1] != NULL));

		TRACE0("\nShbIpcThreadSignalNewData(): enter wait state");
		dwWaitResult = WaitForMultipleObjects(2,	// DWORD nCount
						      pShbMemInst->m_ahEventNewData,	// const HANDLE* lpHandles
						      FALSE,	// BOOL bWaitAll
						      INFINITE);	// DWORD dwMilliseconds
		TRACE0
		    ("\nShbIpcThreadSignalNewData(): wait state leaved: ---> ");
		switch (dwWaitResult) {
		case WAIT_OBJECT_0 + 0:	// event "new data"
			{
				TRACE0("Event = WAIT_OBJECT_0+0");
				if (pShbMemInst->m_pfnSigHndlrNewData != NULL) {
					TRACE0
					    ("\nShbIpcThreadSignalNewData(): calling SignalHandlerNewData");
					do {
						fCallAgain =
						    pShbMemInst->
						    m_pfnSigHndlrNewData
						    (pShbInstance);
						// d.k.: try to run any shared buffer which has higher priority.
						//           under Windows this is not really necessary because the Windows scheduler
						//           already preempts tasks with lower priority.
					} while (fCallAgain != FALSE);
				}
				break;
			}

		case WAIT_OBJECT_0 + 1:	// event "terminate"
			{
				TRACE0("Event = WAIT_OBJECT_0+1");
				fTermRequ = TRUE;
				break;
			}

		default:
			{
				TRACE0("Unhandled Event");
				ASSERT(0);
				fTermRequ = TRUE;
				break;
			}
		}
	}
	while (!fTermRequ);

	if (pShbMemInst->m_ahEventNewData[IDX_EVENT_TERM_RESP] !=
	    INVALID_HANDLE_VALUE) {
		SetEvent(pShbMemInst->m_ahEventNewData[IDX_EVENT_TERM_RESP]);
	}

	TRACE1
	    ("\nShbIpcThreadSignalNewData(): SignalThread terminated (pShbInstance=0x%08lX)\n",
	     (DWORD) pShbInstance);

	ExitThread(0);

}

//---------------------------------------------------------------------------
//  Thread for new data signaling
//---------------------------------------------------------------------------

DWORD WINAPI ShbIpcThreadSignalJobReady(LPVOID pvThreadParam_p)
{

	tShbInstance *pShbInstance;
	tShbMemInst *pShbMemInst;
	DWORD ulTimeOut;
	DWORD dwWaitResult;
	unsigned int fTimeOut;

	TRACE1
	    ("\nShbIpcThreadSignalJobReady(): SignalThread started (pShbInstance=0x%08lX)\n",
	     (DWORD) pvThreadParam_p);

	pShbInstance = (tShbInstance *) pvThreadParam_p;
	pShbMemInst = ShbIpcGetShbMemInst(pShbInstance);
	fTimeOut = FALSE;

	if (pShbMemInst->m_ulTimeOutJobReady != 0) {
		ulTimeOut = pShbMemInst->m_ulTimeOutJobReady;
	} else {
		ulTimeOut = INFINITE;
	}

	ASSERT((pShbMemInst->m_hEventJobReady != INVALID_HANDLE_VALUE)
	       && (pShbMemInst->m_hEventJobReady != NULL));

	TRACE0("\nShbIpcThreadSignalJobReady(): enter wait state");
	dwWaitResult = WaitForSingleObject(pShbMemInst->m_hEventJobReady,	// HANDLE hHandle
					   ulTimeOut);	// DWORD dwMilliseconds
	TRACE0("\nShbIpcThreadSignalJobReady(): wait state leaved: ---> ");
	switch (dwWaitResult) {
	case WAIT_OBJECT_0 + 0:	// event "new data"
		{
			TRACE0("Event = WAIT_OBJECT_0+0");
			fTimeOut = FALSE;
			break;
		}

	case WAIT_TIMEOUT:
		{
			TRACE0("\nEvent = WAIT_TIMEOUT");
			fTimeOut = TRUE;
			// ASSERT(0);
			break;
		}

	default:
		{
			TRACE0("Unhandled Event");
			fTimeOut = TRUE;
			ASSERT(0);
			break;
		}
	}

	if (pShbMemInst->m_pfnSigHndlrJobReady != NULL) {
		TRACE0
		    ("\nShbIpcThreadSignalJobReady(): calling SignalHandlerJobReady");
		pShbMemInst->m_pfnSigHndlrJobReady(pShbInstance, fTimeOut);
	}

	pShbMemInst->m_hThreadJobReady = INVALID_HANDLE_VALUE;
	pShbMemInst->m_pfnSigHndlrJobReady = NULL;

	TRACE1
	    ("\nShbIpcThreadSignalJobReady(): SignalThread terminated (pShbInstance=0x%08lX)\n",
	     (DWORD) pShbInstance);

	ExitThread(0);

}

#endif

// EOF
