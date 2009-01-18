/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  source file for Epl Userspace-Timermodule for Win32

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

                $RCSfile: EplTimeruWin32.c,v $

                $Author: D.Krueger $

                $Revision: 1.4 $  $Date: 2008/04/17 21:36:32 $

                $State: Exp $

                Build Environment:
                    GCC V3.4

  -------------------------------------------------------------------------

  Revision History:

  2006/07/06 k.t.:   start of the implementation

****************************************************************************/

#include "user/EplTimeru.h"

/***************************************************************************/
/*                                                                         */
/*                                                                         */
/*          G L O B A L   D E F I N I T I O N S                            */
/*                                                                         */
/*                                                                         */
/***************************************************************************/

//---------------------------------------------------------------------------
// const defines
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// local types
//---------------------------------------------------------------------------
typedef struct {
	tEplTimerArg TimerArgument;
	HANDLE DelteHandle;
	unsigned long ulTimeout;

} tEplTimeruThread;

typedef struct {
	LPCRITICAL_SECTION m_pCriticalSection;
	CRITICAL_SECTION m_CriticalSection;
} tEplTimeruInstance;
//---------------------------------------------------------------------------
// modul globale vars
//---------------------------------------------------------------------------
static tEplTimeruInstance EplTimeruInstance_g;
static tEplTimeruThread ThreadData_l;
//---------------------------------------------------------------------------
// local function prototypes
//---------------------------------------------------------------------------
DWORD PUBLIC EplSdoTimeruThreadms(LPVOID lpParameter);

/***************************************************************************/
/*                                                                         */
/*                                                                         */
/*          C L A S S  <Epl Userspace-Timermodule for Win32>               */
/*                                                                         */
/*                                                                         */
/***************************************************************************/
//
// Description: Epl Userspace-Timermodule for Win32
//
//
/***************************************************************************/

//=========================================================================//
//                                                                         //
//          P U B L I C   F U N C T I O N S                                //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//
// Function:    EplTimeruInit
//
// Description: function init first instance
//
//
//
// Parameters:
//
//
// Returns:     tEplKernel  = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel PUBLIC EplTimeruInit()
{
	tEplKernel Ret;

	Ret = EplTimeruAddInstance();

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplTimeruAddInstance
//
// Description: function init additional instance
//
//
//
// Parameters:
//
//
// Returns:     tEplKernel  = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel PUBLIC EplTimeruAddInstance()
{
	tEplKernel Ret;

	Ret = kEplSuccessful;

	// create critical section
	EplTimeruInstance_g.m_pCriticalSection =
	    &EplTimeruInstance_g.m_CriticalSection;
	InitializeCriticalSection(EplTimeruInstance_g.m_pCriticalSection);

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplTimeruDelInstance
//
// Description: function delte instance
//              -> under Win32 nothing to do
//              -> no instnace table needed
//
//
//
// Parameters:
//
//
// Returns:     tEplKernel  = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel PUBLIC EplTimeruDelInstance()
{
	tEplKernel Ret;

	Ret = kEplSuccessful;

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplTimeruSetTimerMs
//
// Description: function create a timer and return a handle to the pointer
//
//
//
// Parameters:  pTimerHdl_p = pointer to a buffer to fill in the handle
//              ulTime_p    = time for timer in ms
//              Argument_p  = argument for timer
//
//
// Returns:     tEplKernel  = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel PUBLIC EplTimeruSetTimerMs(tEplTimerHdl * pTimerHdl_p,
				      unsigned long ulTime_p,
				      tEplTimerArg Argument_p)
{
	tEplKernel Ret;
	HANDLE DeleteHandle;
	HANDLE ThreadHandle;
	DWORD ThreadId;

	Ret = kEplSuccessful;

	// check handle
	if (pTimerHdl_p == NULL) {
		Ret = kEplTimerInvalidHandle;
		goto Exit;
	}
	// enter  critical section
	EnterCriticalSection(EplTimeruInstance_g.m_pCriticalSection);

	// first create event to delete timer
	DeleteHandle = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (DeleteHandle == NULL) {
		Ret = kEplTimerNoTimerCreated;
		goto Exit;
	}
	// set handle for caller
	*pTimerHdl_p = (tEplTimerHdl) DeleteHandle;

	// fill data for thread
	ThreadData_l.DelteHandle = DeleteHandle;
	EPL_MEMCPY(&ThreadData_l.TimerArgument, &Argument_p,
		   sizeof(tEplTimerArg));
	ThreadData_l.ulTimeout = ulTime_p;

	// create thread to create waitable timer and wait for timer
	ThreadHandle = CreateThread(NULL,
				    0,
				    EplSdoTimeruThreadms,
				    &ThreadData_l, 0, &ThreadId);
	if (ThreadHandle == NULL) {
		// leave critical section
		LeaveCriticalSection(EplTimeruInstance_g.m_pCriticalSection);

		// delte handle
		CloseHandle(DeleteHandle);

		Ret = kEplTimerNoTimerCreated;
		goto Exit;
	}

      Exit:
	return Ret;
}

 //---------------------------------------------------------------------------
//
// Function:    EplTimeruModifyTimerMs
//
// Description: function change a timer and return a handle to the pointer
//
//
//
// Parameters:  pTimerHdl_p = pointer to a buffer to fill in the handle
//              ulTime_p    = time for timer in ms
//              Argument_p  = argument for timer
//
//
// Returns:     tEplKernel  = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel PUBLIC EplTimeruModifyTimerMs(tEplTimerHdl * pTimerHdl_p,
					 unsigned long ulTime_p,
					 tEplTimerArg Argument_p)
{
	tEplKernel Ret;
	HANDLE DeleteHandle;
	HANDLE ThreadHandle;
	DWORD ThreadId;

	Ret = kEplSuccessful;

	// check parameter
	if (pTimerHdl_p == NULL) {
		Ret = kEplTimerInvalidHandle;
		goto Exit;
	}

	DeleteHandle = (HANDLE) (*pTimerHdl_p);

	// set event to end timer task for this timer
	SetEvent(DeleteHandle);

	// create new timer
	// first create event to delete timer
	DeleteHandle = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (DeleteHandle == NULL) {
		Ret = kEplTimerNoTimerCreated;
		goto Exit;
	}
	// set handle for caller
	*pTimerHdl_p = (tEplTimerHdl) DeleteHandle;

	// enter  critical section
	EnterCriticalSection(EplTimeruInstance_g.m_pCriticalSection);

	// fill data for thread
	ThreadData_l.DelteHandle = DeleteHandle;
	EPL_MEMCPY(&ThreadData_l.TimerArgument, &Argument_p,
		   sizeof(tEplTimerArg));
	ThreadData_l.ulTimeout = ulTime_p;

	// create thread to create waitable timer and wait for timer
	ThreadHandle = CreateThread(NULL,
				    0,
				    EplSdoTimeruThreadms,
				    &ThreadData_l, 0, &ThreadId);
	if (ThreadHandle == NULL) {
		// leave critical section
		LeaveCriticalSection(EplTimeruInstance_g.m_pCriticalSection);

		// delte handle

		Ret = kEplTimerNoTimerCreated;
		goto Exit;
	}

      Exit:
	return Ret;
}

 //---------------------------------------------------------------------------
//
// Function:    EplTimeruDeleteTimer
//
// Description: function delte a timer
//
//
//
// Parameters:  pTimerHdl_p = pointer to a buffer to fill in the handle
//
//
// Returns:     tEplKernel  = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel PUBLIC EplTimeruDeleteTimer(tEplTimerHdl * pTimerHdl_p)
{
	tEplKernel Ret;
	HANDLE DeleteHandle;

	Ret = kEplSuccessful;

	// check parameter
	if (pTimerHdl_p == NULL) {
		Ret = kEplTimerInvalidHandle;
		goto Exit;
	}

	DeleteHandle = (HANDLE) (*pTimerHdl_p);

	// set event to end timer task for this timer
	SetEvent(DeleteHandle);

	// set handle invalide
	*pTimerHdl_p = 0;

      Exit:
	return Ret;

}

//=========================================================================//
//                                                                         //
//          P R I V A T E   F U N C T I O N S                              //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//
// Function:    EplSdoTimeruThreadms
//
// Description: function to process timer as thread
//
//
//
// Parameters:  lpParameter = pointer to structur of type tEplTimeruThread
//
//
// Returns:     DWORD = Errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
DWORD PUBLIC EplSdoTimeruThreadms(LPVOID lpParameter)
{
	tEplKernel Ret;
	tEplTimeruThread *pThreadData;
	HANDLE aHandles[2];
	BOOL fReturn;
	LARGE_INTEGER TimeoutTime;
	unsigned long ulEvent;
	tEplEvent EplEvent;
	tEplTimeruThread ThreadData;
	tEplTimerEventArg TimerEventArg;

	Ret = kEplSuccessful;

	// get pointer to data
	pThreadData = (tEplTimeruThread *) lpParameter;
	// copy thread data
	EPL_MEMCPY(&ThreadData, pThreadData, sizeof(ThreadData));
	pThreadData = &ThreadData;

	// leave critical section
	LeaveCriticalSection(EplTimeruInstance_g.m_pCriticalSection);

	// create waitable timer
	aHandles[1] = CreateWaitableTimer(NULL, FALSE, NULL);
	if (aHandles[1] == NULL) {
		Ret = kEplTimerNoTimerCreated;
		goto Exit;
	}
	// set timer
	// set timeout interval -> needed to be negativ
	// -> because relative timeout
	// -> multiply by 10000 for 100 ns timebase of function
	TimeoutTime.QuadPart = (((long long)pThreadData->ulTimeout) * -10000);
	fReturn = SetWaitableTimer(aHandles[1],
				   &TimeoutTime, 0, NULL, NULL, FALSE);
	if (fReturn == 0) {
		Ret = kEplTimerNoTimerCreated;
		goto Exit;
	}
	// save delte event handle in handle array
	aHandles[0] = pThreadData->DelteHandle;

	// wait for one of the events
	ulEvent = WaitForMultipleObjects(2, &aHandles[0], FALSE, INFINITE);
	if (ulEvent == WAIT_OBJECT_0) {	// delte event

		// close handels
		CloseHandle(aHandles[1]);
		// terminate thread
		goto Exit;
	} else if (ulEvent == (WAIT_OBJECT_0 + 1)) {	// timer event
		// call event function
		TimerEventArg.m_TimerHdl =
		    (tEplTimerHdl) pThreadData->DelteHandle;
		TimerEventArg.m_ulArg = pThreadData->TimerArgument.m_ulArg;

		EplEvent.m_EventSink = pThreadData->TimerArgument.m_EventSink;
		EplEvent.m_EventType = kEplEventTypeTimer;
		EPL_MEMSET(&EplEvent.m_NetTime, 0x00, sizeof(tEplNetTime));
		EplEvent.m_pArg = &TimerEventArg;
		EplEvent.m_uiSize = sizeof(TimerEventArg);

		Ret = EplEventuPost(&EplEvent);

		// close handels
		CloseHandle(aHandles[1]);
		// terminate thread
		goto Exit;

	} else {		// error
		ulEvent = GetLastError();
		TRACE1("Error in WaitForMultipleObjects Errorcode: 0x%x\n",
		       ulEvent);
		// terminate thread
		goto Exit;
	}

      Exit:
	return Ret;
}

// EOF
