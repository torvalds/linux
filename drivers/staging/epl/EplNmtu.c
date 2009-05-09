/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  source file for NMT-Userspace-Module

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

                $RCSfile: EplNmtu.c,v $

                $Author: D.Krueger $

                $Revision: 1.8 $  $Date: 2008/11/10 17:17:42 $

                $State: Exp $

                Build Environment:
                    GCC V3.4

  -------------------------------------------------------------------------

  Revision History:

  2006/06/09 k.t.:   start of the implementation

****************************************************************************/

#include "EplInc.h"
#include "user/EplNmtu.h"
#include "user/EplObdu.h"
#include "user/EplTimeru.h"
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMTK)) != 0)
#include "kernel/EplNmtk.h"
#endif

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMTU)) != 0)
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
	tEplNmtuStateChangeCallback m_pfnNmtChangeCb;
	tEplTimerHdl m_TimerHdl;

} tEplNmtuInstance;

//---------------------------------------------------------------------------
// modul globale vars
//---------------------------------------------------------------------------

static tEplNmtuInstance EplNmtuInstance_g;

//---------------------------------------------------------------------------
// local function prototypes
//---------------------------------------------------------------------------

//=========================================================================//
//                                                                         //
//          P U B L I C   F U N C T I O N S                                //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//
// Function:    EplNmtuInit
//
// Description: init first instance of the module
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
tEplKernel EplNmtuInit(void)
{
	tEplKernel Ret;

	Ret = EplNmtuAddInstance();

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplNmtuAddInstance
//
// Description: init other instances of the module
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
tEplKernel EplNmtuAddInstance(void)
{
	tEplKernel Ret;

	Ret = kEplSuccessful;

	EplNmtuInstance_g.m_pfnNmtChangeCb = NULL;

	return Ret;

}

//---------------------------------------------------------------------------
//
// Function:    EplNmtuDelInstance
//
// Description: delete instance
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
tEplKernel EplNmtuDelInstance(void)
{
	tEplKernel Ret;

	Ret = kEplSuccessful;

	EplNmtuInstance_g.m_pfnNmtChangeCb = NULL;

	// delete timer
	Ret = EplTimeruDeleteTimer(&EplNmtuInstance_g.m_TimerHdl);

	return Ret;

}

//---------------------------------------------------------------------------
//
// Function:    EplNmtuNmtEvent
//
// Description: sends the NMT-Event to the NMT-State-Maschine
//
//
//
// Parameters:  NmtEvent_p  = NMT-Event to send
//
//
// Returns:     tEplKernel  = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel EplNmtuNmtEvent(tEplNmtEvent NmtEvent_p)
{
	tEplKernel Ret;
	tEplEvent Event;

	Event.m_EventSink = kEplEventSinkNmtk;
	Event.m_NetTime.m_dwNanoSec = 0;
	Event.m_NetTime.m_dwSec = 0;
	Event.m_EventType = kEplEventTypeNmtEvent;
	Event.m_pArg = &NmtEvent_p;
	Event.m_uiSize = sizeof(NmtEvent_p);

	Ret = EplEventuPost(&Event);

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplNmtuGetNmtState
//
// Description: returns the actuell NMT-State
//
//
//
// Parameters:
//
//
// Returns:     tEplNmtState  = NMT-State
//
//
// State:
//
//---------------------------------------------------------------------------
tEplNmtState EplNmtuGetNmtState(void)
{
	tEplNmtState NmtState;

	// $$$ call function of communication abstraction layer
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMTK)) != 0)
	NmtState = EplNmtkGetNmtState();
#else
	NmtState = 0;
#endif

	return NmtState;
}

//---------------------------------------------------------------------------
//
// Function:    EplNmtuProcessEvent
//
// Description: processes events from event queue
//
//
//
// Parameters:  pEplEvent_p =   pointer to event
//
//
// Returns:     tEplKernel  = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel EplNmtuProcessEvent(tEplEvent *pEplEvent_p)
{
	tEplKernel Ret;

	Ret = kEplSuccessful;

	// process event
	switch (pEplEvent_p->m_EventType) {
		// state change of NMT-Module
	case kEplEventTypeNmtStateChange:
		{
			tEplEventNmtStateChange *pNmtStateChange;

			// delete timer
			Ret =
			    EplTimeruDeleteTimer(&EplNmtuInstance_g.m_TimerHdl);

			pNmtStateChange =
			    (tEplEventNmtStateChange *) pEplEvent_p->m_pArg;

			// call cb-functions to inform higher layer
			if (EplNmtuInstance_g.m_pfnNmtChangeCb != NULL) {
				Ret =
				    EplNmtuInstance_g.
				    m_pfnNmtChangeCb(*pNmtStateChange);
			}

			if (Ret == kEplSuccessful) {	// everything is OK, so switch to next state if necessary
				switch (pNmtStateChange->m_NewNmtState) {
					// EPL stack is not running
				case kEplNmtGsOff:
					break;

					// first init of the hardware
				case kEplNmtGsInitialising:
					{
						Ret =
						    EplNmtuNmtEvent
						    (kEplNmtEventEnterResetApp);
						break;
					}

					// init of the manufacturer-specific profile area and the
					// standardised device profile area
				case kEplNmtGsResetApplication:
					{
						Ret =
						    EplNmtuNmtEvent
						    (kEplNmtEventEnterResetCom);
						break;
					}

					// init of the communication profile area
				case kEplNmtGsResetCommunication:
					{
						Ret =
						    EplNmtuNmtEvent
						    (kEplNmtEventEnterResetConfig);
						break;
					}

					// build the configuration with infos from OD
				case kEplNmtGsResetConfiguration:
					{
						unsigned int uiNodeId;

						// get node ID from OD
#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDU)) != 0) || (EPL_OBD_USE_KERNEL != FALSE)
						uiNodeId =
						    EplObduGetNodeId
						    (EPL_MCO_PTR_INSTANCE_PTR);
#else
						uiNodeId = 0;
#endif
						//check node ID if not should be master or slave
						if (uiNodeId == EPL_C_ADR_MN_DEF_NODE_ID) {	// node shall be MN
#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
							Ret =
							    EplNmtuNmtEvent
							    (kEplNmtEventEnterMsNotActive);
#else
							TRACE0
							    ("EplNmtuProcess(): no MN functionality implemented\n");
#endif
						} else {	// node shall be CN
							Ret =
							    EplNmtuNmtEvent
							    (kEplNmtEventEnterCsNotActive);
						}
						break;
					}

					//-----------------------------------------------------------
					// CN part of the state machine

					// node listens for EPL-Frames and check timeout
				case kEplNmtCsNotActive:
					{
						u32 dwBuffer;
						tEplObdSize ObdSize;
						tEplTimerArg TimerArg;

						// create timer to switch automatically to BasicEthernet if no MN available in network

						// read NMT_CNBasicEthernetTimerout_U32 from OD
						ObdSize = sizeof(dwBuffer);
#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDU)) != 0) || (EPL_OBD_USE_KERNEL != FALSE)
						Ret =
						    EplObduReadEntry
						    (EPL_MCO_PTR_INSTANCE_PTR_
						     0x1F99, 0x00, &dwBuffer,
						     &ObdSize);
#else
						Ret = kEplObdIndexNotExist;
#endif
						if (Ret != kEplSuccessful) {
							break;
						}
						if (dwBuffer != 0) {	// BasicEthernet is enabled
							// convert us into ms
							dwBuffer =
							    dwBuffer / 1000;
							if (dwBuffer == 0) {	// timer was below one ms
								// set one ms
								dwBuffer = 1;
							}
							TimerArg.m_EventSink =
							    kEplEventSinkNmtk;
							TimerArg.m_ulArg =
							    (unsigned long)
							    kEplNmtEventTimerBasicEthernet;
							Ret =
							    EplTimeruModifyTimerMs
							    (&EplNmtuInstance_g.
							     m_TimerHdl,
							     (unsigned long)
							     dwBuffer,
							     TimerArg);
							// potential error is forwarded to event queue which generates error event
						}
						break;
					}

					// node processes only async frames
				case kEplNmtCsPreOperational1:
					{
						break;
					}

					// node processes isochronous and asynchronous frames
				case kEplNmtCsPreOperational2:
					{
						Ret =
						    EplNmtuNmtEvent
						    (kEplNmtEventEnterReadyToOperate);
						break;
					}

					// node should be configured und application is ready
				case kEplNmtCsReadyToOperate:
					{
						break;
					}

					// normal work state
				case kEplNmtCsOperational:
					{
						break;
					}

					// node stopped by MN
					// -> only process asynchronous frames
				case kEplNmtCsStopped:
					{
						break;
					}

					// no EPL cycle
					// -> normal ethernet communication
				case kEplNmtCsBasicEthernet:
					{
						break;
					}

					//-----------------------------------------------------------
					// MN part of the state machine

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
					// node listens for EPL-Frames and check timeout
				case kEplNmtMsNotActive:
					{
						u32 dwBuffer;
						tEplObdSize ObdSize;
						tEplTimerArg TimerArg;

						// create timer to switch automatically to BasicEthernet/PreOp1 if no other MN active in network

						// check NMT_StartUp_U32.Bit13
						// read NMT_StartUp_U32 from OD
						ObdSize = sizeof(dwBuffer);
#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDU)) != 0) || (EPL_OBD_USE_KERNEL != FALSE)
						Ret =
						    EplObduReadEntry
						    (EPL_MCO_PTR_INSTANCE_PTR_
						     0x1F80, 0x00, &dwBuffer,
						     &ObdSize);
#else
						Ret = kEplObdIndexNotExist;
#endif
						if (Ret != kEplSuccessful) {
							break;
						}

						if ((dwBuffer & EPL_NMTST_BASICETHERNET) == 0) {	// NMT_StartUp_U32.Bit13 == 0
							// new state PreOperational1
							TimerArg.m_ulArg =
							    (unsigned long)
							    kEplNmtEventTimerMsPreOp1;
						} else {	// NMT_StartUp_U32.Bit13 == 1
							// new state BasicEthernet
							TimerArg.m_ulArg =
							    (unsigned long)
							    kEplNmtEventTimerBasicEthernet;
						}

						// read NMT_BootTime_REC.MNWaitNotAct_U32 from OD
						ObdSize = sizeof(dwBuffer);
#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDU)) != 0) || (EPL_OBD_USE_KERNEL != FALSE)
						Ret =
						    EplObduReadEntry
						    (EPL_MCO_PTR_INSTANCE_PTR_
						     0x1F89, 0x01, &dwBuffer,
						     &ObdSize);
#else
						Ret = kEplObdIndexNotExist;
#endif
						if (Ret != kEplSuccessful) {
							break;
						}
						// convert us into ms
						dwBuffer = dwBuffer / 1000;
						if (dwBuffer == 0) {	// timer was below one ms
							// set one ms
							dwBuffer = 1;
						}
						TimerArg.m_EventSink =
						    kEplEventSinkNmtk;
						Ret =
						    EplTimeruModifyTimerMs
						    (&EplNmtuInstance_g.
						     m_TimerHdl,
						     (unsigned long)dwBuffer,
						     TimerArg);
						// potential error is forwarded to event queue which generates error event
						break;
					}

					// node processes only async frames
				case kEplNmtMsPreOperational1:
					{
						u32 dwBuffer = 0;
						tEplObdSize ObdSize;
						tEplTimerArg TimerArg;

						// create timer to switch automatically to PreOp2 if MN identified all mandatory CNs

						// read NMT_BootTime_REC.MNWaitPreOp1_U32 from OD
						ObdSize = sizeof(dwBuffer);
#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDU)) != 0) || (EPL_OBD_USE_KERNEL != FALSE)
						Ret =
						    EplObduReadEntry
						    (EPL_MCO_PTR_INSTANCE_PTR_
						     0x1F89, 0x03, &dwBuffer,
						     &ObdSize);
						if (Ret != kEplSuccessful) {
							// ignore error, because this timeout is optional
							dwBuffer = 0;
						}
#endif
						if (dwBuffer == 0) {	// delay is deactivated
							// immediately post timer event
							Ret =
							    EplNmtuNmtEvent
							    (kEplNmtEventTimerMsPreOp2);
							break;
						}
						// convert us into ms
						dwBuffer = dwBuffer / 1000;
						if (dwBuffer == 0) {	// timer was below one ms
							// set one ms
							dwBuffer = 1;
						}
						TimerArg.m_EventSink =
						    kEplEventSinkNmtk;
						TimerArg.m_ulArg =
						    (unsigned long)
						    kEplNmtEventTimerMsPreOp2;
						Ret =
						    EplTimeruModifyTimerMs
						    (&EplNmtuInstance_g.
						     m_TimerHdl,
						     (unsigned long)dwBuffer,
						     TimerArg);
						// potential error is forwarded to event queue which generates error event
						break;
					}

					// node processes isochronous and asynchronous frames
				case kEplNmtMsPreOperational2:
					{
						break;
					}

					// node should be configured und application is ready
				case kEplNmtMsReadyToOperate:
					{
						break;
					}

					// normal work state
				case kEplNmtMsOperational:
					{
						break;
					}

					// no EPL cycle
					// -> normal ethernet communication
				case kEplNmtMsBasicEthernet:
					{
						break;
					}
#endif // (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)

				default:
					{
						TRACE1
						    ("EplNmtuProcess(): unhandled NMT state 0x%X\n",
						     pNmtStateChange->
						     m_NewNmtState);
					}
				}
			} else if (Ret == kEplReject) {	// application wants to change NMT state itself
				// it's OK
				Ret = kEplSuccessful;
			}

			EPL_DBGLVL_NMTU_TRACE0
			    ("EplNmtuProcessEvent(): NMT-State-Maschine announce change of NMT State\n");
			break;
		}

	default:
		{
			Ret = kEplNmtInvalidEvent;
		}

	}

//Exit:
	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplNmtuRegisterStateChangeCb
//
// Description: register Callback-function go get informed about a
//              NMT-Change-State-Event
//
//
//
// Parameters:  pfnEplNmtStateChangeCb_p = functionpointer
//
//
// Returns:     tEplKernel  = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel EplNmtuRegisterStateChangeCb(tEplNmtuStateChangeCallback pfnEplNmtStateChangeCb_p)
{
	tEplKernel Ret;

	Ret = kEplSuccessful;

	// save callback-function in modul global var
	EplNmtuInstance_g.m_pfnNmtChangeCb = pfnEplNmtStateChangeCb_p;

	return Ret;

}

//=========================================================================//
//                                                                         //
//          P R I V A T E   F U N C T I O N S                              //
//                                                                         //
//=========================================================================//

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

#endif // #if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMTU)) != 0)

// EOF
