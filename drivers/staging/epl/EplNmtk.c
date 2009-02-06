/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  source file for NMT-Kernelspace-Module

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

                $RCSfile: EplNmtk.c,v $

                $Author: D.Krueger $

                $Revision: 1.12 $  $Date: 2008/11/13 17:13:09 $

                $State: Exp $

                Build Environment:
                    GCC V3.4

  -------------------------------------------------------------------------

  Revision History:

  2006/06/09 k.t.:   start of the implementation

****************************************************************************/

#include "kernel/EplNmtk.h"
#include "kernel/EplTimerk.h"

#include "kernel/EplDllk.h"	// for EplDllkProcess()

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMTK)) != 0)
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
#define EPL_NMTK_DBG_POST_TRACE_VALUE(NmtEvent_p, OldNmtState_p, NewNmtState_p) \
    TGT_DBG_POST_TRACE_VALUE((kEplEventSinkNmtk << 28) | (NmtEvent_p << 16) \
                             | ((OldNmtState_p & 0xFF) << 8) \
                             | (NewNmtState_p & 0xFF))

//---------------------------------------------------------------------------
// local types
//---------------------------------------------------------------------------
// struct for instance table
INSTANCE_TYPE_BEGIN EPL_MCO_DECL_INSTANCE_MEMBER()

STATIC volatile tEplNmtState INST_FAR m_NmtState;
STATIC volatile BOOL INST_FAR m_fEnableReadyToOperate;
STATIC volatile BOOL INST_FAR m_fAppReadyToOperate;
STATIC volatile BOOL INST_FAR m_fTimerMsPreOp2;
STATIC volatile BOOL INST_FAR m_fAllMandatoryCNIdent;
STATIC volatile BOOL INST_FAR m_fFrozen;

INSTANCE_TYPE_END
//---------------------------------------------------------------------------
// modul globale vars
//---------------------------------------------------------------------------
// This macro replace the unspecific pointer to an instance through
// the modul specific type for the local instance table. This macro
// must defined in each modul.
//#define tEplPtrInstance             tEplInstanceInfo MEM*
EPL_MCO_DECL_INSTANCE_VAR()
//---------------------------------------------------------------------------
// local function prototypes
//---------------------------------------------------------------------------
EPL_MCO_DEFINE_INSTANCE_FCT()

/***************************************************************************/
/*                                                                         */
/*                                                                         */
/*          C L A S S  <NMT_Kernel-Module>                                 */
/*                                                                         */
/*                                                                         */
/***************************************************************************/
//
// Description: This module realize the NMT-State-Machine of the EPL-Stack
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
// Function:        EplNmtkInit
//
// Description: initializes the first instance
//
//
//
// Parameters:  EPL_MCO_DECL_PTR_INSTANCE_PTR = Instance pointer
//              uiNodeId_p = Node Id of the lokal node
//
//
// Returns:     tEplKernel  =   Errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
EPLDLLEXPORT tEplKernel PUBLIC EplNmtkInit(EPL_MCO_DECL_PTR_INSTANCE_PTR)
{
	tEplKernel Ret;

	Ret = EplNmtkAddInstance(EPL_MCO_PTR_INSTANCE_PTR);

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:        EplNmtkAddInstance
//
// Description: adds a new instance
//
//
//
// Parameters:  EPL_MCO_DECL_PTR_INSTANCE_PTR = Instance pointer
//
//
// Returns:     tEplKernel  =   Errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
EPLDLLEXPORT tEplKernel PUBLIC EplNmtkAddInstance(EPL_MCO_DECL_PTR_INSTANCE_PTR)
{
	EPL_MCO_DECL_INSTANCE_PTR_LOCAL tEplKernel Ret;
//tEplEvent               Event;
//tEplEventNmtStateChange NmtStateChange;

	// check if pointer to instance pointer valid
	// get free instance and set the globale instance pointer
	// set also the instance addr to parameterlist
	EPL_MCO_CHECK_PTR_INSTANCE_PTR();
	EPL_MCO_GET_FREE_INSTANCE_PTR();
	EPL_MCO_SET_PTR_INSTANCE_PTR();

	// sign instance as used
	EPL_MCO_WRITE_INSTANCE_STATE(kStateUsed);

	Ret = kEplSuccessful;

	// initialize intern vaiables
	// 2006/07/31 d.k.: set NMT-State to kEplNmtGsOff
	EPL_MCO_GLB_VAR(m_NmtState) = kEplNmtGsOff;
	// set NMT-State to kEplNmtGsInitialising
	//EPL_MCO_GLB_VAR(m_NmtState) = kEplNmtGsInitialising;

	// set flags to FALSE
	EPL_MCO_GLB_VAR(m_fEnableReadyToOperate) = FALSE;
	EPL_MCO_GLB_VAR(m_fAppReadyToOperate) = FALSE;
	EPL_MCO_GLB_VAR(m_fTimerMsPreOp2) = FALSE;
	EPL_MCO_GLB_VAR(m_fAllMandatoryCNIdent) = FALSE;
	EPL_MCO_GLB_VAR(m_fFrozen) = FALSE;

//    EPL_MCO_GLB_VAR(m_TimerHdl) = 0;

	// inform higher layer about state change
	// 2006/07/31 d.k.: The EPL API layer/application has to start NMT state
	//                  machine via NmtEventSwReset after initialisation of
	//                  all modules has been completed. DLL has to be initialised
	//                  after NMTk because NMT state shall not be uninitialised
	//                  at that time.
/*    NmtStateChange.m_NewNmtState = EPL_MCO_GLB_VAR(m_NmtState);
    NmtStateChange.m_NmtEvent = kEplNmtEventNoEvent;
    Event.m_EventSink = kEplEventSinkNmtu;
    Event.m_EventType = kEplEventTypeNmtStateChange;
    EPL_MEMSET(&Event.m_NetTime, 0x00, sizeof(Event.m_NetTime));
    Event.m_pArg = &NmtStateChange;
    Event.m_uiSize = sizeof(NmtStateChange);
    Ret = EplEventkPost(&Event);
*/
	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:        EplNmtkDelInstance
//
// Description: delete instance
//
//
//
// Parameters:  EPL_MCO_DECL_PTR_INSTANCE_PTR = Instance pointer
//
//
// Returns:     tEplKernel  =   Errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
#if (EPL_USE_DELETEINST_FUNC != FALSE)
EPLDLLEXPORT tEplKernel PUBLIC EplNmtkDelInstance(EPL_MCO_DECL_PTR_INSTANCE_PTR)
{
	tEplKernel Ret = kEplSuccessful;
	// check for all API function if instance is valid
	EPL_MCO_CHECK_INSTANCE_STATE();

	// set NMT-State to kEplNmtGsOff
	EPL_MCO_GLB_VAR(m_NmtState) = kEplNmtGsOff;

	// sign instance as unused
	EPL_MCO_WRITE_INSTANCE_STATE(kStateUnused);

	// delete timer
//    Ret = EplTimerkDeleteTimer(&EPL_MCO_GLB_VAR(m_TimerHdl));

	return Ret;
}
#endif // (EPL_USE_DELETEINST_FUNC != FALSE)

//---------------------------------------------------------------------------
//
// Function:        EplNmtkProcess
//
// Description: main process function
//              -> process NMT-State-Maschine und read NMT-Events from Queue
//
//
//
// Parameters:  EPL_MCO_DECL_PTR_INSTANCE_PTR_ = Instance pointer
//              pEvent_p    =   Epl-Event with NMT-event to process
//
//
// Returns:     tEplKernel  =   Errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
EPLDLLEXPORT tEplKernel PUBLIC EplNmtkProcess(EPL_MCO_DECL_PTR_INSTANCE_PTR_
					      tEplEvent * pEvent_p)
{
	tEplKernel Ret;
	tEplNmtState OldNmtState;
	tEplNmtEvent NmtEvent;
	tEplEvent Event;
	tEplEventNmtStateChange NmtStateChange;

	// check for all API function if instance is valid
	EPL_MCO_CHECK_INSTANCE_STATE();

	Ret = kEplSuccessful;

	switch (pEvent_p->m_EventType) {
	case kEplEventTypeNmtEvent:
		{
			NmtEvent = *((tEplNmtEvent *) pEvent_p->m_pArg);
			break;
		}

	case kEplEventTypeTimer:
		{
			NmtEvent =
			    (tEplNmtEvent) ((tEplTimerEventArg *) pEvent_p->
					    m_pArg)->m_ulArg;
			break;
		}
	default:
		{
			Ret = kEplNmtInvalidEvent;
			goto Exit;
		}
	}

	// save NMT-State
	// needed for later comparison to
	// inform hgher layer about state change
	OldNmtState = EPL_MCO_GLB_VAR(m_NmtState);

	// NMT-State-Maschine
	switch (EPL_MCO_GLB_VAR(m_NmtState)) {
		//-----------------------------------------------------------
		// general part of the statemaschine

		// first init of the hardware
	case kEplNmtGsOff:
		{
			// leave this state only if higher layer says so
			if (NmtEvent == kEplNmtEventSwReset) {	// new state kEplNmtGsInitialising
				EPL_MCO_GLB_VAR(m_NmtState) =
				    kEplNmtGsInitialising;
			}
			break;
		}

		// first init of the hardware
	case kEplNmtGsInitialising:
		{
			// leave this state only if higher layer says so

			// check events
			switch (NmtEvent) {
				// 2006/07/31 d.k.: react also on NMT reset commands in ResetApp state
				// NMT Command SwitchOff
			case kEplNmtEventCriticalError:
			case kEplNmtEventSwitchOff:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsOff;
					break;
				}

				// new state kEplNmtGsResetApplication
			case kEplNmtEventEnterResetApp:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetApplication;
					break;
				}

			default:
				{
					break;
				}
			}
			break;
		}

		// init of the manufacturer-specific profile area and the
		// standardised device profile area
	case kEplNmtGsResetApplication:
		{
			// check events
			switch (NmtEvent) {
				// 2006/07/31 d.k.: react also on NMT reset commands in ResetApp state
				// NMT Command SwitchOff
			case kEplNmtEventCriticalError:
			case kEplNmtEventSwitchOff:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsOff;
					break;
				}

				// NMT Command SwReset
			case kEplNmtEventSwReset:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsInitialising;
					break;
				}

				// leave this state only if higher layer
				// say so
			case kEplNmtEventEnterResetCom:
				{
					// new state kEplNmtGsResetCommunication
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetCommunication;
					break;
				}

			default:
				{
					break;
				}
			}
			break;
		}

		// init of the communication profile area
	case kEplNmtGsResetCommunication:
		{
			// check events
			switch (NmtEvent) {
				// 2006/07/31 d.k.: react also on NMT reset commands in ResetComm state
				// NMT Command SwitchOff
			case kEplNmtEventCriticalError:
			case kEplNmtEventSwitchOff:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsOff;
					break;
				}

				// NMT Command SwReset
			case kEplNmtEventSwReset:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsInitialising;
					break;
				}

				// NMT Command ResetNode
			case kEplNmtEventResetNode:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetApplication;
					break;
				}

				// leave this state only if higher layer
				// say so
			case kEplNmtEventEnterResetConfig:
				{
					// new state kEplNmtGsResetCommunication
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetConfiguration;
					break;
				}

			default:
				{
					break;
				}
			}
			break;
		}

		// build the configuration with infos from OD
	case kEplNmtGsResetConfiguration:
		{
			// reset flags
			EPL_MCO_GLB_VAR(m_fEnableReadyToOperate) = FALSE;
			EPL_MCO_GLB_VAR(m_fAppReadyToOperate) = FALSE;
			EPL_MCO_GLB_VAR(m_fFrozen) = FALSE;

			// check events
			switch (NmtEvent) {
				// 2006/07/31 d.k.: react also on NMT reset commands in ResetConf state
				// NMT Command SwitchOff
			case kEplNmtEventCriticalError:
			case kEplNmtEventSwitchOff:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsOff;
					break;
				}

				// NMT Command SwReset
			case kEplNmtEventSwReset:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsInitialising;
					break;
				}

				// NMT Command ResetNode
			case kEplNmtEventResetNode:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetApplication;
					break;
				}

				// NMT Command ResetCommunication
			case kEplNmtEventResetCom:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetCommunication;
					break;
				}

				// leave this state only if higher layer says so
			case kEplNmtEventEnterCsNotActive:
				{	// Node should be CN
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtCsNotActive;
					break;

				}

			case kEplNmtEventEnterMsNotActive:
				{	// Node should be CN
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) == 0)
					// no MN functionality
					// TODO: -create error E_NMT_BA1_NO_MN_SUPPORT
					EPL_MCO_GLB_VAR(m_fFrozen) = TRUE;
#else

					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtMsNotActive;
#endif
					break;

				}

			default:
				{
					break;
				}
			}
			break;
		}

		//-----------------------------------------------------------
		// CN part of the statemaschine

		// node liste for EPL-Frames and check timeout
	case kEplNmtCsNotActive:
		{

			// check events
			switch (NmtEvent) {
				// 2006/07/31 d.k.: react also on NMT reset commands in NotActive state
				// NMT Command SwitchOff
			case kEplNmtEventCriticalError:
			case kEplNmtEventSwitchOff:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsOff;
					break;
				}

				// NMT Command SwReset
			case kEplNmtEventSwReset:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsInitialising;
//                    Ret = EplTimerkDeleteTimer(&EPL_MCO_GLB_VAR(m_TimerHdl));
					break;
				}

				// NMT Command ResetNode
			case kEplNmtEventResetNode:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetApplication;
//                    Ret = EplTimerkDeleteTimer(&EPL_MCO_GLB_VAR(m_TimerHdl));
					break;
				}

				// NMT Command ResetCommunication
				// or internal Communication error
			case kEplNmtEventResetCom:
			case kEplNmtEventInternComError:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetCommunication;
//                    Ret = EplTimerkDeleteTimer(&EPL_MCO_GLB_VAR(m_TimerHdl));
					break;
				}

				// NMT Command Reset Configuration
			case kEplNmtEventResetConfig:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetConfiguration;
//                    Ret = EplTimerkDeleteTimer(&EPL_MCO_GLB_VAR(m_TimerHdl));
					break;
				}

				// see if SoA or SoC received
				// k.t. 20.07.2006: only SoA forces change of state
				// see EPL V2 DS 1.0.0 p.267
				// case kEplNmtEventDllCeSoc:
			case kEplNmtEventDllCeSoa:
				{	// new state PRE_OPERATIONAL1
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtCsPreOperational1;
//                    Ret = EplTimerkDeleteTimer(&EPL_MCO_GLB_VAR(m_TimerHdl));
					break;
				}
				// timeout for SoA and Soc
			case kEplNmtEventTimerBasicEthernet:
				{
					// new state BASIC_ETHERNET
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtCsBasicEthernet;
					break;
				}

			default:
				{
					break;
				}
			}	// end of switch(NmtEvent)

			break;
		}

		// node processes only async frames
	case kEplNmtCsPreOperational1:
		{

			// check events
			switch (NmtEvent) {
				// NMT Command SwitchOff
			case kEplNmtEventCriticalError:
			case kEplNmtEventSwitchOff:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsOff;
					break;
				}

				// NMT Command SwReset
			case kEplNmtEventSwReset:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsInitialising;
					break;
				}

				// NMT Command ResetNode
			case kEplNmtEventResetNode:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetApplication;
					break;
				}

				// NMT Command ResetCommunication
				// or internal Communication error
			case kEplNmtEventResetCom:
			case kEplNmtEventInternComError:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetCommunication;
					break;
				}

				// NMT Command Reset Configuration
			case kEplNmtEventResetConfig:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetConfiguration;
					break;
				}

				// NMT Command StopNode
			case kEplNmtEventStopNode:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtCsStopped;
					break;
				}

				// check if SoC received
			case kEplNmtEventDllCeSoc:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtCsPreOperational2;
					break;
				}

			default:
				{
					break;
				}

			}	// end of switch(NmtEvent)

			break;
		}

		// node processes isochronous and asynchronous frames
	case kEplNmtCsPreOperational2:
		{
			// check events
			switch (NmtEvent) {
				// NMT Command SwitchOff
			case kEplNmtEventCriticalError:
			case kEplNmtEventSwitchOff:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsOff;
					break;
				}

				// NMT Command SwReset
			case kEplNmtEventSwReset:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsInitialising;
					break;
				}

				// NMT Command ResetNode
			case kEplNmtEventResetNode:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetApplication;
					break;
				}

				// NMT Command ResetCommunication
				// or internal Communication error
			case kEplNmtEventResetCom:
			case kEplNmtEventInternComError:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetCommunication;
					break;
				}

				// NMT Command Reset Configuration
			case kEplNmtEventResetConfig:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetConfiguration;
					break;
				}

				// NMT Command StopNode
			case kEplNmtEventStopNode:
				{
					// reset flags
					EPL_MCO_GLB_VAR(m_fEnableReadyToOperate)
					    = FALSE;
					EPL_MCO_GLB_VAR(m_fAppReadyToOperate) =
					    FALSE;
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtCsStopped;
					break;
				}

				// error occured
			case kEplNmtEventNmtCycleError:
				{
					// reset flags
					EPL_MCO_GLB_VAR(m_fEnableReadyToOperate)
					    = FALSE;
					EPL_MCO_GLB_VAR(m_fAppReadyToOperate) =
					    FALSE;
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtCsPreOperational1;
					break;
				}

				// check if application is ready to operate
			case kEplNmtEventEnterReadyToOperate:
				{
					// check if command NMTEnableReadyToOperate from MN was received
					if (EPL_MCO_GLB_VAR(m_fEnableReadyToOperate) == TRUE) {	// reset flags
						EPL_MCO_GLB_VAR
						    (m_fEnableReadyToOperate) =
						    FALSE;
						EPL_MCO_GLB_VAR
						    (m_fAppReadyToOperate) =
						    FALSE;
						// change state
						EPL_MCO_GLB_VAR(m_NmtState) =
						    kEplNmtCsReadyToOperate;
					} else {	// set Flag
						EPL_MCO_GLB_VAR
						    (m_fAppReadyToOperate) =
						    TRUE;
					}
					break;
				}

				// NMT Commando EnableReadyToOperate
			case kEplNmtEventEnableReadyToOperate:
				{
					// check if application is ready
					if (EPL_MCO_GLB_VAR(m_fAppReadyToOperate) == TRUE) {	// reset flags
						EPL_MCO_GLB_VAR
						    (m_fEnableReadyToOperate) =
						    FALSE;
						EPL_MCO_GLB_VAR
						    (m_fAppReadyToOperate) =
						    FALSE;
						// change state
						EPL_MCO_GLB_VAR(m_NmtState) =
						    kEplNmtCsReadyToOperate;
					} else {	// set Flag
						EPL_MCO_GLB_VAR
						    (m_fEnableReadyToOperate) =
						    TRUE;
					}
					break;
				}

			default:
				{
					break;
				}

			}	// end of switch(NmtEvent)
			break;
		}

		// node should be configured und application is ready
	case kEplNmtCsReadyToOperate:
		{
			// check events
			switch (NmtEvent) {
				// NMT Command SwitchOff
			case kEplNmtEventCriticalError:
			case kEplNmtEventSwitchOff:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsOff;
					break;
				}

				// NMT Command SwReset
			case kEplNmtEventSwReset:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsInitialising;
					break;
				}

				// NMT Command ResetNode
			case kEplNmtEventResetNode:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetApplication;
					break;
				}

				// NMT Command ResetCommunication
				// or internal Communication error
			case kEplNmtEventResetCom:
			case kEplNmtEventInternComError:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetCommunication;
					break;
				}

				// NMT Command ResetConfiguration
			case kEplNmtEventResetConfig:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetConfiguration;
					break;
				}

				// NMT Command StopNode
			case kEplNmtEventStopNode:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtCsStopped;
					break;
				}

				// error occured
			case kEplNmtEventNmtCycleError:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtCsPreOperational1;
					break;
				}

				// NMT Command StartNode
			case kEplNmtEventStartNode:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtCsOperational;
					break;
				}

			default:
				{
					break;
				}

			}	// end of switch(NmtEvent)
			break;
		}

		// normal work state
	case kEplNmtCsOperational:
		{

			// check events
			switch (NmtEvent) {
				// NMT Command SwitchOff
			case kEplNmtEventCriticalError:
			case kEplNmtEventSwitchOff:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsOff;
					break;
				}

				// NMT Command SwReset
			case kEplNmtEventSwReset:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsInitialising;
					break;
				}

				// NMT Command ResetNode
			case kEplNmtEventResetNode:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetApplication;
					break;
				}

				// NMT Command ResetCommunication
				// or internal Communication error
			case kEplNmtEventResetCom:
			case kEplNmtEventInternComError:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetCommunication;
					break;
				}

				// NMT Command ResetConfiguration
			case kEplNmtEventResetConfig:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetConfiguration;
					break;
				}

				// NMT Command StopNode
			case kEplNmtEventStopNode:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtCsStopped;
					break;
				}

				// NMT Command EnterPreOperational2
			case kEplNmtEventEnterPreOperational2:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtCsPreOperational2;
					break;
				}

				// error occured
			case kEplNmtEventNmtCycleError:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtCsPreOperational1;
					break;
				}

			default:
				{
					break;
				}

			}	// end of switch(NmtEvent)
			break;
		}

		// node stopped by MN
		// -> only process asynchronous frames
	case kEplNmtCsStopped:
		{
			// check events
			switch (NmtEvent) {
				// NMT Command SwitchOff
			case kEplNmtEventCriticalError:
			case kEplNmtEventSwitchOff:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsOff;
					break;
				}

				// NMT Command SwReset
			case kEplNmtEventSwReset:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsInitialising;
					break;
				}

				// NMT Command ResetNode
			case kEplNmtEventResetNode:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetApplication;
					break;
				}

				// NMT Command ResetCommunication
				// or internal Communication error
			case kEplNmtEventResetCom:
			case kEplNmtEventInternComError:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetCommunication;
					break;
				}

				// NMT Command ResetConfiguration
			case kEplNmtEventResetConfig:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetConfiguration;
					break;
				}

				// NMT Command EnterPreOperational2
			case kEplNmtEventEnterPreOperational2:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtCsPreOperational2;
					break;
				}

				// error occured
			case kEplNmtEventNmtCycleError:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtCsPreOperational1;
					break;
				}

			default:
				{
					break;
				}

			}	// end of switch(NmtEvent)
			break;
		}

		// no epl cycle
		// -> normal ethernet communication
	case kEplNmtCsBasicEthernet:
		{
			// check events
			switch (NmtEvent) {
				// NMT Command SwitchOff
			case kEplNmtEventCriticalError:
			case kEplNmtEventSwitchOff:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsOff;
					break;
				}

				// NMT Command SwReset
			case kEplNmtEventSwReset:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsInitialising;
					break;
				}

				// NMT Command ResetNode
			case kEplNmtEventResetNode:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetApplication;
					break;
				}

				// NMT Command ResetCommunication
				// or internal Communication error
			case kEplNmtEventResetCom:
			case kEplNmtEventInternComError:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetCommunication;
					break;
				}

				// NMT Command ResetConfiguration
			case kEplNmtEventResetConfig:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetConfiguration;
					break;
				}

				// error occured
				// d.k.: how does this error occur? on CRC errors
/*                case kEplNmtEventNmtCycleError:
                {
                    EPL_MCO_GLB_VAR(m_NmtState) = kEplNmtCsPreOperational1;
                    break;
                }
*/
			case kEplNmtEventDllCeSoc:
			case kEplNmtEventDllCePreq:
			case kEplNmtEventDllCePres:
			case kEplNmtEventDllCeSoa:
				{	// Epl-Frame on net -> stop any communication
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtCsPreOperational1;
					break;
				}

			default:
				{
					break;
				}

			}	// end of switch(NmtEvent)

			break;
		}

		//-----------------------------------------------------------
		// MN part of the statemaschine

		// MN listen to network
		// -> if no EPL traffic go to next state
	case kEplNmtMsNotActive:
		{
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) == 0)
			// no MN functionality
			// TODO: -create error E_NMT_BA1_NO_MN_SUPPORT
			EPL_MCO_GLB_VAR(m_fFrozen) = TRUE;
#else

			// check events
			switch (NmtEvent) {
				// NMT Command SwitchOff
			case kEplNmtEventCriticalError:
			case kEplNmtEventSwitchOff:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsOff;
					break;
				}

				// NMT Command SwReset
			case kEplNmtEventSwReset:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsInitialising;
					break;
				}

				// NMT Command ResetNode
			case kEplNmtEventResetNode:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetApplication;
					break;
				}

				// NMT Command ResetCommunication
				// or internal Communication error
			case kEplNmtEventResetCom:
			case kEplNmtEventInternComError:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetCommunication;
					break;
				}

				// NMT Command ResetConfiguration
			case kEplNmtEventResetConfig:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetConfiguration;
					break;
				}

				// EPL frames received
			case kEplNmtEventDllCeSoc:
			case kEplNmtEventDllCeSoa:
				{	// other MN in network
					// $$$ d.k.: generate error history entry
					EPL_MCO_GLB_VAR(m_fFrozen) = TRUE;
					break;
				}

				// timeout event
			case kEplNmtEventTimerBasicEthernet:
				{
					if (EPL_MCO_GLB_VAR(m_fFrozen) == FALSE) {	// new state BasicEthernet
						EPL_MCO_GLB_VAR(m_NmtState) =
						    kEplNmtMsBasicEthernet;
					}
					break;
				}

				// timeout event
			case kEplNmtEventTimerMsPreOp1:
				{
					if (EPL_MCO_GLB_VAR(m_fFrozen) == FALSE) {	// new state PreOp1
						EPL_MCO_GLB_VAR(m_NmtState) =
						    kEplNmtMsPreOperational1;
						EPL_MCO_GLB_VAR
						    (m_fTimerMsPreOp2) = FALSE;
						EPL_MCO_GLB_VAR
						    (m_fAllMandatoryCNIdent) =
						    FALSE;

					}
					break;
				}

			default:
				{
					break;
				}

			}	// end of switch(NmtEvent)

#endif // ((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) == 0)

			break;
		}
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
		// MN process reduces epl cycle
	case kEplNmtMsPreOperational1:
		{
			// check events
			switch (NmtEvent) {
				// NMT Command SwitchOff
			case kEplNmtEventCriticalError:
			case kEplNmtEventSwitchOff:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsOff;
					break;
				}

				// NMT Command SwReset
			case kEplNmtEventSwReset:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsInitialising;
					break;
				}

				// NMT Command ResetNode
			case kEplNmtEventResetNode:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetApplication;
					break;
				}

				// NMT Command ResetCommunication
				// or internal Communication error
			case kEplNmtEventResetCom:
			case kEplNmtEventInternComError:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetCommunication;
					break;
				}

				// NMT Command ResetConfiguration
			case kEplNmtEventResetConfig:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetConfiguration;
					break;
				}

				// EPL frames received
			case kEplNmtEventDllCeSoc:
			case kEplNmtEventDllCeSoa:
				{	// other MN in network
					// $$$ d.k.: generate error history entry
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetCommunication;
					break;
				}

				// error occured
				// d.k. MSPreOp1->CSPreOp1: nonsense -> keep state
				/*
				   case kEplNmtEventNmtCycleError:
				   {
				   EPL_MCO_GLB_VAR(m_NmtState) = kEplNmtCsPreOperational1;
				   break;
				   }
				 */

			case kEplNmtEventAllMandatoryCNIdent:
				{	// all mandatory CN identified
					if (EPL_MCO_GLB_VAR(m_fTimerMsPreOp2) !=
					    FALSE) {
						EPL_MCO_GLB_VAR(m_NmtState) =
						    kEplNmtMsPreOperational2;
					} else {
						EPL_MCO_GLB_VAR
						    (m_fAllMandatoryCNIdent) =
						    TRUE;
					}
					break;
				}

			case kEplNmtEventTimerMsPreOp2:
				{	// residence time for PreOp1 is elapsed
					if (EPL_MCO_GLB_VAR
					    (m_fAllMandatoryCNIdent) != FALSE) {
						EPL_MCO_GLB_VAR(m_NmtState) =
						    kEplNmtMsPreOperational2;
					} else {
						EPL_MCO_GLB_VAR
						    (m_fTimerMsPreOp2) = TRUE;
					}
					break;
				}

			default:
				{
					break;
				}

			}	// end of switch(NmtEvent)
			break;
		}

		// MN process full epl cycle
	case kEplNmtMsPreOperational2:
		{
			// check events
			switch (NmtEvent) {
				// NMT Command SwitchOff
			case kEplNmtEventCriticalError:
			case kEplNmtEventSwitchOff:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsOff;
					break;
				}

				// NMT Command SwReset
			case kEplNmtEventSwReset:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsInitialising;
					break;
				}

				// NMT Command ResetNode
			case kEplNmtEventResetNode:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetApplication;
					break;
				}

				// NMT Command ResetCommunication
				// or internal Communication error
			case kEplNmtEventResetCom:
			case kEplNmtEventInternComError:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetCommunication;
					break;
				}

				// NMT Command ResetConfiguration
			case kEplNmtEventResetConfig:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetConfiguration;
					break;
				}

				// EPL frames received
			case kEplNmtEventDllCeSoc:
			case kEplNmtEventDllCeSoa:
				{	// other MN in network
					// $$$ d.k.: generate error history entry
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetCommunication;
					break;
				}

				// error occured
			case kEplNmtEventNmtCycleError:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtMsPreOperational1;
					break;
				}

			case kEplNmtEventEnterReadyToOperate:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtMsReadyToOperate;
					break;
				}

			default:
				{
					break;
				}

			}	// end of switch(NmtEvent)

			break;
		}

		// all madatory nodes ready to operate
		// -> MN process full epl cycle
	case kEplNmtMsReadyToOperate:
		{

			// check events
			switch (NmtEvent) {
				// NMT Command SwitchOff
			case kEplNmtEventCriticalError:
			case kEplNmtEventSwitchOff:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsOff;
					break;
				}

				// NMT Command SwReset
			case kEplNmtEventSwReset:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsInitialising;
					break;
				}

				// NMT Command ResetNode
			case kEplNmtEventResetNode:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetApplication;
					break;
				}

				// NMT Command ResetCommunication
				// or internal Communication error
			case kEplNmtEventResetCom:
			case kEplNmtEventInternComError:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetCommunication;
					break;
				}

				// NMT Command ResetConfiguration
			case kEplNmtEventResetConfig:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetConfiguration;
					break;
				}

				// EPL frames received
			case kEplNmtEventDllCeSoc:
			case kEplNmtEventDllCeSoa:
				{	// other MN in network
					// $$$ d.k.: generate error history entry
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetCommunication;
					break;
				}

				// error occured
			case kEplNmtEventNmtCycleError:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtMsPreOperational1;
					break;
				}

			case kEplNmtEventEnterMsOperational:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtMsOperational;
					break;
				}

			default:
				{
					break;
				}

			}	// end of switch(NmtEvent)

			break;
		}

		// normal eplcycle processing
	case kEplNmtMsOperational:
		{
			// check events
			switch (NmtEvent) {
				// NMT Command SwitchOff
			case kEplNmtEventCriticalError:
			case kEplNmtEventSwitchOff:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsOff;
					break;
				}

				// NMT Command SwReset
			case kEplNmtEventSwReset:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsInitialising;
					break;
				}

				// NMT Command ResetNode
			case kEplNmtEventResetNode:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetApplication;
					break;
				}

				// NMT Command ResetCommunication
				// or internal Communication error
			case kEplNmtEventResetCom:
			case kEplNmtEventInternComError:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetCommunication;
					break;
				}

				// NMT Command ResetConfiguration
			case kEplNmtEventResetConfig:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetConfiguration;
					break;
				}

				// EPL frames received
			case kEplNmtEventDllCeSoc:
			case kEplNmtEventDllCeSoa:
				{	// other MN in network
					// $$$ d.k.: generate error history entry
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetCommunication;
					break;
				}

				// error occured
			case kEplNmtEventNmtCycleError:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtMsPreOperational1;
					break;
				}

			default:
				{
					break;
				}

			}	// end of switch(NmtEvent)
			break;
		}

		//  normal ethernet traffic
	case kEplNmtMsBasicEthernet:
		{

			// check events
			switch (NmtEvent) {
				// NMT Command SwitchOff
			case kEplNmtEventCriticalError:
			case kEplNmtEventSwitchOff:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsOff;
					break;
				}

				// NMT Command SwReset
			case kEplNmtEventSwReset:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsInitialising;
					break;
				}

				// NMT Command ResetNode
			case kEplNmtEventResetNode:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetApplication;
					break;
				}

				// NMT Command ResetCommunication
				// or internal Communication error
			case kEplNmtEventResetCom:
			case kEplNmtEventInternComError:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetCommunication;
					break;
				}

				// NMT Command ResetConfiguration
			case kEplNmtEventResetConfig:
				{
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetConfiguration;
					break;
				}

				// EPL frames received
			case kEplNmtEventDllCeSoc:
			case kEplNmtEventDllCeSoa:
				{	// other MN in network
					// $$$ d.k.: generate error history entry
					EPL_MCO_GLB_VAR(m_NmtState) =
					    kEplNmtGsResetCommunication;
					break;
				}

				// error occured
				// d.k. BE->PreOp1 on cycle error? No
/*                case kEplNmtEventNmtCycleError:
                {
                    EPL_MCO_GLB_VAR(m_NmtState) = kEplNmtCsPreOperational1;
                    break;
                }
*/
			default:
				{
					break;
				}

			}	// end of switch(NmtEvent)
			break;
		}
#endif //#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)

	default:
		{
			//DEBUG_EPL_DBGLVL_NMTK_TRACE0(EPL_DBGLVL_NMT ,"Error in EplNmtProcess: Unknown NMT-State");
			//EPL_MCO_GLB_VAR(m_NmtState) = kEplNmtGsResetApplication;
			Ret = kEplNmtInvalidState;
			goto Exit;
		}

	}			// end of switch(NmtEvent)

	// inform higher layer about State-Change if needed
	if (OldNmtState != EPL_MCO_GLB_VAR(m_NmtState)) {
		EPL_NMTK_DBG_POST_TRACE_VALUE(NmtEvent, OldNmtState,
					      EPL_MCO_GLB_VAR(m_NmtState));

		// d.k.: memorize NMT state before posting any events
		NmtStateChange.m_NewNmtState = EPL_MCO_GLB_VAR(m_NmtState);

		// inform DLL
		if ((OldNmtState > kEplNmtGsResetConfiguration)
		    && (EPL_MCO_GLB_VAR(m_NmtState) <=
			kEplNmtGsResetConfiguration)) {
			// send DLL DEINIT
			Event.m_EventSink = kEplEventSinkDllk;
			Event.m_EventType = kEplEventTypeDllkDestroy;
			EPL_MEMSET(&Event.m_NetTime, 0x00,
				   sizeof(Event.m_NetTime));
			Event.m_pArg = &OldNmtState;
			Event.m_uiSize = sizeof(OldNmtState);
			// d.k.: directly call DLLk process function, because
			//       1. execution of process function is still synchonized and serialized,
			//       2. it is the same as without event queues (i.e. well tested),
			//       3. DLLk will get those necessary events even if event queue is full,
			//       4. event queue is very inefficient
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_DLLK)) != 0)
			Ret = EplDllkProcess(&Event);
#else
			Ret = EplEventkPost(&Event);
#endif
		} else if ((OldNmtState <= kEplNmtGsResetConfiguration)
			   && (EPL_MCO_GLB_VAR(m_NmtState) >
			       kEplNmtGsResetConfiguration)) {
			// send DLL INIT
			Event.m_EventSink = kEplEventSinkDllk;
			Event.m_EventType = kEplEventTypeDllkCreate;
			EPL_MEMSET(&Event.m_NetTime, 0x00,
				   sizeof(Event.m_NetTime));
			Event.m_pArg = &NmtStateChange.m_NewNmtState;
			Event.m_uiSize = sizeof(NmtStateChange.m_NewNmtState);
			// d.k.: directly call DLLk process function, because
			//       1. execution of process function is still synchonized and serialized,
			//       2. it is the same as without event queues (i.e. well tested),
			//       3. DLLk will get those necessary events even if event queue is full
			//       4. event queue is very inefficient
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_DLLK)) != 0)
			Ret = EplDllkProcess(&Event);
#else
			Ret = EplEventkPost(&Event);
#endif
		} else
		    if ((EPL_MCO_GLB_VAR(m_NmtState) == kEplNmtCsBasicEthernet)
			|| (EPL_MCO_GLB_VAR(m_NmtState) ==
			    kEplNmtMsBasicEthernet)) {
			tEplDllAsyncReqPriority AsyncReqPriority;

			// send DLL Fill Async Tx Buffer, because state BasicEthernet was entered
			Event.m_EventSink = kEplEventSinkDllk;
			Event.m_EventType = kEplEventTypeDllkFillTx;
			EPL_MEMSET(&Event.m_NetTime, 0x00,
				   sizeof(Event.m_NetTime));
			AsyncReqPriority = kEplDllAsyncReqPrioGeneric;
			Event.m_pArg = &AsyncReqPriority;
			Event.m_uiSize = sizeof(AsyncReqPriority);
			// d.k.: directly call DLLk process function, because
			//       1. execution of process function is still synchonized and serialized,
			//       2. it is the same as without event queues (i.e. well tested),
			//       3. DLLk will get those necessary events even if event queue is full
			//       4. event queue is very inefficient
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_DLLK)) != 0)
			Ret = EplDllkProcess(&Event);
#else
			Ret = EplEventkPost(&Event);
#endif
		}
		// inform higher layer about state change
		NmtStateChange.m_NmtEvent = NmtEvent;
		Event.m_EventSink = kEplEventSinkNmtu;
		Event.m_EventType = kEplEventTypeNmtStateChange;
		EPL_MEMSET(&Event.m_NetTime, 0x00, sizeof(Event.m_NetTime));
		Event.m_pArg = &NmtStateChange;
		Event.m_uiSize = sizeof(NmtStateChange);
		Ret = EplEventkPost(&Event);
		EPL_DBGLVL_NMTK_TRACE2
		    ("EplNmtkProcess(NMT-Event = 0x%04X): New NMT-State = 0x%03X\n",
		     NmtEvent, NmtStateChange.m_NewNmtState);

	}

      Exit:

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplNmtkGetNmtState
//
// Description: return the actuell NMT-State and the bits
//              to for MN- or CN-mode
//
//
//
// Parameters:  EPL_MCO_DECL_PTR_INSTANCE_PTR_ = Instancepointer
//
//
// Returns:     tEplNmtState = NMT-State
//
//
// State:
//
//---------------------------------------------------------------------------
EPLDLLEXPORT tEplNmtState PUBLIC
EplNmtkGetNmtState(EPL_MCO_DECL_PTR_INSTANCE_PTR)
{
	tEplNmtState NmtState;

	NmtState = EPL_MCO_GLB_VAR(m_NmtState);

	return NmtState;

}

//=========================================================================//
//                                                                         //
//          P R I V A T E   D E F I N I T I O N S                          //
//                                                                         //
//=========================================================================//
EPL_MCO_DECL_INSTANCE_FCT()
//---------------------------------------------------------------------------
//
// Function:
//
// Description:
//
//
//
// Parameters:
//
//
// Returns:
//
//
// State:
//
//---------------------------------------------------------------------------
#endif // #if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMTK)) != 0)
// EOF
