/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  source file for Epl-Kernelspace-Event-Modul

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

                $RCSfile: EplEventk.c,v $

                $Author: D.Krueger $

                $Revision: 1.9 $  $Date: 2008/10/17 15:32:32 $

                $State: Exp $

                Build Environment:
                    GCC V3.4

  -------------------------------------------------------------------------

  Revision History:

  2006/06/20 k.t.:   start of the implementation

****************************************************************************/

#include "kernel/EplEventk.h"
#include "kernel/EplNmtk.h"
#include "kernel/EplDllk.h"
#include "kernel/EplDllkCal.h"
#include "kernel/EplErrorHandlerk.h"
#include "Benchmark.h"

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_PDOK)) != 0)
#include "kernel/EplPdok.h"
#include "kernel/EplPdokCal.h"
#endif

#ifdef EPL_NO_FIFO
#include "user/EplEventu.h"
#else
#include "SharedBuff.h"
#endif

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

// TracePoint support for realtime-debugging
#ifdef _DBG_TRACE_POINTS_
void PUBLIC TgtDbgSignalTracePoint(BYTE bTracePointNumber_p);
void PUBLIC TgtDbgPostTraceValue(DWORD dwTraceValue_p);
#define TGT_DBG_SIGNAL_TRACE_POINT(p)   TgtDbgSignalTracePoint(p)
#define TGT_DBG_POST_TRACE_VALUE(v)     TgtDbgPostTraceValue(v)
#else
#define TGT_DBG_SIGNAL_TRACE_POINT(p)
#define TGT_DBG_POST_TRACE_VALUE(v)
#endif

//---------------------------------------------------------------------------
// local types
//---------------------------------------------------------------------------

typedef struct {
#ifndef EPL_NO_FIFO
	tShbInstance m_pShbKernelToUserInstance;
	tShbInstance m_pShbUserToKernelInstance;
#else

#endif
	tEplSyncCb m_pfnCbSync;
	unsigned int m_uiUserToKernelFullCount;

} tEplEventkInstance;

//---------------------------------------------------------------------------
// modul globale vars
//---------------------------------------------------------------------------
static tEplEventkInstance EplEventkInstance_g;
//---------------------------------------------------------------------------
// local function prototypes
//---------------------------------------------------------------------------

// callback function for incoming events
#ifndef EPL_NO_FIFO
static void EplEventkRxSignalHandlerCb(tShbInstance pShbRxInstance_p,
				       unsigned long ulDataSize_p);
#endif

/***************************************************************************/
/*                                                                         */
/*                                                                         */
/*          C L A S S  <Epl-Kernelspace-Event>                             */
/*                                                                         */
/*                                                                         */
/***************************************************************************/
//
// Description:
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
// Function:    EplEventkInit
//
// Description: function initializes the first instance
//
// Parameters:  pfnCbSync_p = callback-function for sync event
//
// Returns:     tEpKernel   = errorcode
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel PUBLIC EplEventkInit(tEplSyncCb pfnCbSync_p)
{
	tEplKernel Ret;

	Ret = EplEventkAddInstance(pfnCbSync_p);

	return Ret;

}

//---------------------------------------------------------------------------
//
// Function:    EplEventkAddInstance
//
// Description: function adds one more instance
//
// Parameters:  pfnCbSync_p = callback-function for sync event
//
// Returns:     tEpKernel   = errorcode
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel PUBLIC EplEventkAddInstance(tEplSyncCb pfnCbSync_p)
{
	tEplKernel Ret;
#ifndef EPL_NO_FIFO
	tShbError ShbError;
	unsigned int fShbNewCreated;
#endif

	Ret = kEplSuccessful;

	// init instance structure
	EplEventkInstance_g.m_uiUserToKernelFullCount = 0;

	// save cb-function
	EplEventkInstance_g.m_pfnCbSync = pfnCbSync_p;

#ifndef EPL_NO_FIFO
	// init shared loop buffer
	// kernel -> user
	ShbError = ShbCirAllocBuffer(EPL_EVENT_SIZE_SHB_KERNEL_TO_USER,
				     EPL_EVENT_NAME_SHB_KERNEL_TO_USER,
				     &EplEventkInstance_g.
				     m_pShbKernelToUserInstance,
				     &fShbNewCreated);
	if (ShbError != kShbOk) {
		EPL_DBGLVL_EVENTK_TRACE1
		    ("EplEventkAddInstance(): ShbCirAllocBuffer(K2U) -> 0x%X\n",
		     ShbError);
		Ret = kEplNoResource;
		goto Exit;
	}
	// user -> kernel
	ShbError = ShbCirAllocBuffer(EPL_EVENT_SIZE_SHB_USER_TO_KERNEL,
				     EPL_EVENT_NAME_SHB_USER_TO_KERNEL,
				     &EplEventkInstance_g.
				     m_pShbUserToKernelInstance,
				     &fShbNewCreated);
	if (ShbError != kShbOk) {
		EPL_DBGLVL_EVENTK_TRACE1
		    ("EplEventkAddInstance(): ShbCirAllocBuffer(U2K) -> 0x%X\n",
		     ShbError);
		Ret = kEplNoResource;
		goto Exit;
	}
	// register eventhandler
	ShbError =
	    ShbCirSetSignalHandlerNewData(EplEventkInstance_g.
					  m_pShbUserToKernelInstance,
					  EplEventkRxSignalHandlerCb,
					  kshbPriorityHigh);
	if (ShbError != kShbOk) {
		EPL_DBGLVL_EVENTK_TRACE1
		    ("EplEventkAddInstance(): ShbCirSetSignalHandlerNewData(U2K) -> 0x%X\n",
		     ShbError);
		Ret = kEplNoResource;
		goto Exit;
	}

      Exit:
#endif

	return Ret;

}

//---------------------------------------------------------------------------
//
// Function:    EplEventkDelInstance
//
// Description: function deletes instance and frees the buffers
//
// Parameters:  void
//
// Returns:     tEpKernel   = errorcode
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel PUBLIC EplEventkDelInstance()
{
	tEplKernel Ret;
#ifndef EPL_NO_FIFO
	tShbError ShbError;
#endif

	Ret = kEplSuccessful;

#ifndef EPL_NO_FIFO
	// set eventhandler to NULL
	ShbError =
	    ShbCirSetSignalHandlerNewData(EplEventkInstance_g.
					  m_pShbUserToKernelInstance, NULL,
					  kShbPriorityNormal);
	if (ShbError != kShbOk) {
		EPL_DBGLVL_EVENTK_TRACE1
		    ("EplEventkDelInstance(): ShbCirSetSignalHandlerNewData(U2K) -> 0x%X\n",
		     ShbError);
		Ret = kEplNoResource;
	}
	// free buffer User -> Kernel
	ShbError =
	    ShbCirReleaseBuffer(EplEventkInstance_g.m_pShbUserToKernelInstance);
	if (ShbError != kShbOk) {
		EPL_DBGLVL_EVENTK_TRACE1
		    ("EplEventkDelInstance(): ShbCirReleaseBuffer(U2K) -> 0x%X\n",
		     ShbError);
		Ret = kEplNoResource;
	} else {
		EplEventkInstance_g.m_pShbUserToKernelInstance = NULL;
	}

	// free buffer  Kernel -> User
	ShbError =
	    ShbCirReleaseBuffer(EplEventkInstance_g.m_pShbKernelToUserInstance);
	if (ShbError != kShbOk) {
		EPL_DBGLVL_EVENTK_TRACE1
		    ("EplEventkDelInstance(): ShbCirReleaseBuffer(K2U) -> 0x%X\n",
		     ShbError);
		Ret = kEplNoResource;
	} else {
		EplEventkInstance_g.m_pShbKernelToUserInstance = NULL;
	}
#endif

	return Ret;

}

//---------------------------------------------------------------------------
//
// Function:    EplEventkProcess
//
// Description: Kernelthread that dispatches events in kernel part
//
// Parameters:  pEvent_p    = pointer to event-structure from buffer
//
// Returns:     tEpKernel   = errorcode
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel PUBLIC EplEventkProcess(tEplEvent * pEvent_p)
{
	tEplKernel Ret;
	tEplEventSource EventSource;

	Ret = kEplSuccessful;

	// error handling if event queue is full
	if (EplEventkInstance_g.m_uiUserToKernelFullCount > 0) {	// UserToKernel event queue has run out of space -> kEplNmtEventInternComError
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMTK)) != 0)
		tEplEvent Event;
		tEplNmtEvent NmtEvent;
#endif
#ifndef EPL_NO_FIFO
		tShbError ShbError;
#endif

		// directly call NMTk process function, because event queue is full
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMTK)) != 0)
		NmtEvent = kEplNmtEventInternComError;
		Event.m_EventSink = kEplEventSinkNmtk;
		Event.m_NetTime.m_dwNanoSec = 0;
		Event.m_NetTime.m_dwSec = 0;
		Event.m_EventType = kEplEventTypeNmtEvent;
		Event.m_pArg = &NmtEvent;
		Event.m_uiSize = sizeof(NmtEvent);
		Ret = EplNmtkProcess(&Event);
#endif

		// NMT state machine changed to reset (i.e. NMT_GS_RESET_COMMUNICATION)
		// now, it is safe to reset the counter and empty the event queue
#ifndef EPL_NO_FIFO
		ShbError =
		    ShbCirResetBuffer(EplEventkInstance_g.
				      m_pShbUserToKernelInstance, 1000, NULL);
#endif

		EplEventkInstance_g.m_uiUserToKernelFullCount = 0;
		TGT_DBG_SIGNAL_TRACE_POINT(22);

		// also discard the current event (it doesn't matter if we lose another event)
		goto Exit;
	}
	// check m_EventSink
	switch (pEvent_p->m_EventSink) {
	case kEplEventSinkSync:
		{
			if (EplEventkInstance_g.m_pfnCbSync != NULL) {
				Ret = EplEventkInstance_g.m_pfnCbSync();
				if (Ret == kEplSuccessful) {
#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_PDOK)) != 0)
					// mark TPDOs as valid
					Ret = EplPdokCalSetTpdosValid(TRUE);
#endif
				} else if ((Ret != kEplReject)
					   && (Ret != kEplShutdown)) {
					EventSource = kEplEventSourceSyncCb;

					// Error event for API layer
					EplEventkPostError
					    (kEplEventSourceEventk, Ret,
					     sizeof(EventSource), &EventSource);
				}
			}
			break;
		}

		// NMT-Kernel-Modul
	case kEplEventSinkNmtk:
		{
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMTK)) != 0)
			Ret = EplNmtkProcess(pEvent_p);
			if ((Ret != kEplSuccessful) && (Ret != kEplShutdown)) {
				EventSource = kEplEventSourceNmtk;

				// Error event for API layer
				EplEventkPostError(kEplEventSourceEventk,
						   Ret,
						   sizeof(EventSource),
						   &EventSource);
			}
#endif
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_DLLK)) != 0)
			if ((pEvent_p->m_EventType == kEplEventTypeNmtEvent)
			    &&
			    ((*((tEplNmtEvent *) pEvent_p->m_pArg) ==
			      kEplNmtEventDllCeSoa)
#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
			     || (*((tEplNmtEvent *) pEvent_p->m_pArg) ==
				 kEplNmtEventDllMeSoaSent)
#endif
			    )) {	// forward SoA event to error handler
				Ret = EplErrorHandlerkProcess(pEvent_p);
				if ((Ret != kEplSuccessful)
				    && (Ret != kEplShutdown)) {
					EventSource = kEplEventSourceErrk;

					// Error event for API layer
					EplEventkPostError
					    (kEplEventSourceEventk, Ret,
					     sizeof(EventSource), &EventSource);
				}
#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_PDOK)) != 0)
				// forward SoA event to PDO module
				pEvent_p->m_EventType = kEplEventTypePdoSoa;
				Ret = EplPdokProcess(pEvent_p);
				if ((Ret != kEplSuccessful)
				    && (Ret != kEplShutdown)) {
					EventSource = kEplEventSourcePdok;

					// Error event for API layer
					EplEventkPostError
					    (kEplEventSourceEventk, Ret,
					     sizeof(EventSource), &EventSource);
				}
#endif

			}
			break;
#endif
		}

		// events for Dllk module
	case kEplEventSinkDllk:
		{
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_DLLK)) != 0)
			Ret = EplDllkProcess(pEvent_p);
			if ((Ret != kEplSuccessful) && (Ret != kEplShutdown)) {
				EventSource = kEplEventSourceDllk;

				// Error event for API layer
				EplEventkPostError(kEplEventSourceEventk,
						   Ret,
						   sizeof(EventSource),
						   &EventSource);
			}
#endif
			break;
		}

		// events for DllkCal module
	case kEplEventSinkDllkCal:
		{
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_DLLK)) != 0)
			Ret = EplDllkCalProcess(pEvent_p);
			if ((Ret != kEplSuccessful) && (Ret != kEplShutdown)) {
				EventSource = kEplEventSourceDllk;

				// Error event for API layer
				EplEventkPostError(kEplEventSourceEventk,
						   Ret,
						   sizeof(EventSource),
						   &EventSource);
			}
#endif
			break;
		}

		//
	case kEplEventSinkPdok:
		{
			// PDO-Module
#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_PDOK)) != 0)
			Ret = EplPdokProcess(pEvent_p);
			if ((Ret != kEplSuccessful) && (Ret != kEplShutdown)) {
				EventSource = kEplEventSourcePdok;

				// Error event for API layer
				EplEventkPostError(kEplEventSourceEventk,
						   Ret,
						   sizeof(EventSource),
						   &EventSource);
			}
#endif
			break;
		}

		// events for Error handler module
	case kEplEventSinkErrk:
		{
			// only call error handler if DLL is present
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_DLLK)) != 0)
			Ret = EplErrorHandlerkProcess(pEvent_p);
			if ((Ret != kEplSuccessful) && (Ret != kEplShutdown)) {
				EventSource = kEplEventSourceErrk;

				// Error event for API layer
				EplEventkPostError(kEplEventSourceEventk,
						   Ret,
						   sizeof(EventSource),
						   &EventSource);
			}
			break;
#endif
		}

		// unknown sink
	default:
		{
			Ret = kEplEventUnknownSink;
		}

	}			// end of switch(pEvent_p->m_EventSink)

      Exit:
	return Ret;

}

//---------------------------------------------------------------------------
//
// Function:    EplEventkPost
//
// Description: post events from kernel part
//
// Parameters:  pEvent_p    = pointer to event-structure from buffer
//
// Returns:     tEpKernel   = errorcode
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel PUBLIC EplEventkPost(tEplEvent * pEvent_p)
{
	tEplKernel Ret;
#ifndef EPL_NO_FIFO
	tShbError ShbError;
	tShbCirChunk ShbCirChunk;
	unsigned long ulDataSize;
	unsigned int fBufferCompleted;
#endif

	Ret = kEplSuccessful;

	// the event must be posted by using the abBuffer
	// it is neede because the Argument must by copied
	// to the buffer too and not only the pointer

#ifndef EPL_NO_FIFO
	// 2006/08/03 d.k.: Event and argument are posted as separate chunks to the event queue.
	ulDataSize =
	    sizeof(tEplEvent) +
	    ((pEvent_p->m_pArg != NULL) ? pEvent_p->m_uiSize : 0);
#endif

	// decide in which buffer the event have to write
	switch (pEvent_p->m_EventSink) {
		// kernelspace modules
	case kEplEventSinkSync:
	case kEplEventSinkNmtk:
	case kEplEventSinkDllk:
	case kEplEventSinkDllkCal:
	case kEplEventSinkPdok:
	case kEplEventSinkErrk:
		{
#ifndef EPL_NO_FIFO
			// post message
			BENCHMARK_MOD_27_SET(2);
			ShbError =
			    ShbCirAllocDataBlock(EplEventkInstance_g.
						 m_pShbUserToKernelInstance,
						 &ShbCirChunk, ulDataSize);
			switch (ShbError) {
			case kShbOk:
				break;

			case kShbBufferFull:
				{
					EplEventkInstance_g.
					    m_uiUserToKernelFullCount++;
					Ret = kEplEventPostError;
					goto Exit;
				}

			default:
				{
					EPL_DBGLVL_EVENTK_TRACE1
					    ("EplEventkPost(): ShbCirAllocDataBlock(U2K) -> 0x%X\n",
					     ShbError);
					Ret = kEplEventPostError;
					goto Exit;
				}
			}
			ShbError =
			    ShbCirWriteDataChunk(EplEventkInstance_g.
						 m_pShbUserToKernelInstance,
						 &ShbCirChunk, pEvent_p,
						 sizeof(tEplEvent),
						 &fBufferCompleted);
			if (ShbError != kShbOk) {
				EPL_DBGLVL_EVENTK_TRACE1
				    ("EplEventkPost(): ShbCirWriteDataChunk(U2K) -> 0x%X\n",
				     ShbError);
				Ret = kEplEventPostError;
				goto Exit;
			}
			if (fBufferCompleted == FALSE) {
				ShbError =
				    ShbCirWriteDataChunk(EplEventkInstance_g.
							 m_pShbUserToKernelInstance,
							 &ShbCirChunk,
							 pEvent_p->m_pArg,
							 (unsigned long)
							 pEvent_p->m_uiSize,
							 &fBufferCompleted);
				if ((ShbError != kShbOk)
				    || (fBufferCompleted == FALSE)) {
					EPL_DBGLVL_EVENTK_TRACE1
					    ("EplEventkPost(): ShbCirWriteDataChunk2(U2K) -> 0x%X\n",
					     ShbError);
					Ret = kEplEventPostError;
					goto Exit;
				}
			}
			BENCHMARK_MOD_27_RESET(2);

#else
			Ret = EplEventkProcess(pEvent_p);
#endif

			break;
		}

		// userspace modules
	case kEplEventSinkNmtu:
	case kEplEventSinkNmtMnu:
	case kEplEventSinkSdoAsySeq:
	case kEplEventSinkApi:
	case kEplEventSinkDlluCal:
	case kEplEventSinkErru:
		{
#ifndef EPL_NO_FIFO
			// post message
//            BENCHMARK_MOD_27_SET(3);    // 74 µs until reset
			ShbError =
			    ShbCirAllocDataBlock(EplEventkInstance_g.
						 m_pShbKernelToUserInstance,
						 &ShbCirChunk, ulDataSize);
			if (ShbError != kShbOk) {
				EPL_DBGLVL_EVENTK_TRACE1
				    ("EplEventkPost(): ShbCirAllocDataBlock(K2U) -> 0x%X\n",
				     ShbError);
				Ret = kEplEventPostError;
				goto Exit;
			}
			ShbError =
			    ShbCirWriteDataChunk(EplEventkInstance_g.
						 m_pShbKernelToUserInstance,
						 &ShbCirChunk, pEvent_p,
						 sizeof(tEplEvent),
						 &fBufferCompleted);
			if (ShbError != kShbOk) {
				EPL_DBGLVL_EVENTK_TRACE1
				    ("EplEventkPost(): ShbCirWriteDataChunk(K2U) -> 0x%X\n",
				     ShbError);
				Ret = kEplEventPostError;
				goto Exit;
			}
			if (fBufferCompleted == FALSE) {
				ShbError =
				    ShbCirWriteDataChunk(EplEventkInstance_g.
							 m_pShbKernelToUserInstance,
							 &ShbCirChunk,
							 pEvent_p->m_pArg,
							 (unsigned long)
							 pEvent_p->m_uiSize,
							 &fBufferCompleted);
				if ((ShbError != kShbOk)
				    || (fBufferCompleted == FALSE)) {
					EPL_DBGLVL_EVENTK_TRACE1
					    ("EplEventkPost(): ShbCirWriteDataChunk2(K2U) -> 0x%X\n",
					     ShbError);
					Ret = kEplEventPostError;
					goto Exit;
				}
			}
//            BENCHMARK_MOD_27_RESET(3);  // 82 µs until ShbCirGetReadDataSize() in EplEventu

#else
			Ret = EplEventuProcess(pEvent_p);
#endif

			break;
		}

	default:
		{
			Ret = kEplEventUnknownSink;
		}

	}			// end of switch(pEvent_p->m_EventSink)

#ifndef EPL_NO_FIFO
      Exit:
#endif
	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplEventkPostError
//
// Description: post error event from kernel part to API layer
//
// Parameters:  EventSource_p   = source-module of the error event
//              EplError_p      = code of occured error
//              ArgSize_p       = size of the argument
//              pArg_p          = pointer to the argument
//
// Returns:     tEpKernel       = errorcode
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel PUBLIC EplEventkPostError(tEplEventSource EventSource_p,
				     tEplKernel EplError_p,
				     unsigned int uiArgSize_p, void *pArg_p)
{
	tEplKernel Ret;
	BYTE abBuffer[EPL_MAX_EVENT_ARG_SIZE];
	tEplEventError *pEventError = (tEplEventError *) abBuffer;
	tEplEvent EplEvent;

	Ret = kEplSuccessful;

	// create argument
	pEventError->m_EventSource = EventSource_p;
	pEventError->m_EplError = EplError_p;
	EPL_MEMCPY(&pEventError->m_Arg, pArg_p, uiArgSize_p);

	// create event
	EplEvent.m_EventType = kEplEventTypeError;
	EplEvent.m_EventSink = kEplEventSinkApi;
	EPL_MEMSET(&EplEvent.m_NetTime, 0x00, sizeof(EplEvent.m_NetTime));
	EplEvent.m_uiSize =
	    (sizeof(EventSource_p) + sizeof(EplError_p) + uiArgSize_p);
	EplEvent.m_pArg = &abBuffer[0];

	// post errorevent
	Ret = EplEventkPost(&EplEvent);

	return Ret;
}

//=========================================================================//
//                                                                         //
//          P R I V A T E   F U N C T I O N S                              //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//
// Function:    EplEventkRxSignalHandlerCb()
//
// Description: Callback-function for events from user and kernel part
//
// Parameters:  pShbRxInstance_p    = Instance-pointer of buffer
//              ulDataSize_p        = size of data
//
// Returns: void
//
// State:
//
//---------------------------------------------------------------------------

#ifndef EPL_NO_FIFO
static void EplEventkRxSignalHandlerCb(tShbInstance pShbRxInstance_p,
				       unsigned long ulDataSize_p)
{
	tEplEvent *pEplEvent;
	tShbError ShbError;
//unsigned long   ulBlockCount;
//unsigned long   ulDataSize;
	BYTE abDataBuffer[sizeof(tEplEvent) + EPL_MAX_EVENT_ARG_SIZE];
	// d.k.: abDataBuffer contains the complete tEplEvent structure
	//       and behind this the argument

	TGT_DBG_SIGNAL_TRACE_POINT(20);

	BENCHMARK_MOD_27_RESET(0);
	// copy data from event queue
	ShbError = ShbCirReadDataBlock(pShbRxInstance_p,
				       &abDataBuffer[0],
				       sizeof(abDataBuffer), &ulDataSize_p);
	if (ShbError != kShbOk) {
		// error goto exit
		goto Exit;
	}
	// resolve the pointer to the event structure
	pEplEvent = (tEplEvent *) abDataBuffer;
	// set Datasize
	pEplEvent->m_uiSize = (ulDataSize_p - sizeof(tEplEvent));
	if (pEplEvent->m_uiSize > 0) {
		// set pointer to argument
		pEplEvent->m_pArg = &abDataBuffer[sizeof(tEplEvent)];
	} else {
		//set pointer to NULL
		pEplEvent->m_pArg = NULL;
	}

	BENCHMARK_MOD_27_SET(0);
	// call processfunction
	EplEventkProcess(pEplEvent);

      Exit:
	return;
}
#endif

// EOF
