/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  global include file for EPL-NMT-Modules

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

                $RCSfile: EplNmt.h,v $

                $Author: D.Krueger $

                $Revision: 1.6 $  $Date: 2008/11/17 16:40:39 $

                $State: Exp $

                Build Environment:
                    GCC V3.4

  -------------------------------------------------------------------------

  Revision History:

  2006/06/09 k.t.:   start of the implementation

****************************************************************************/

#ifndef _EPLNMT_H_
#define _EPLNMT_H_

#include "EplInc.h"

//---------------------------------------------------------------------------
// const defines
//---------------------------------------------------------------------------

// define super-states and masks to identify a super-state
#define EPL_NMT_GS_POWERED          0x0008	// super state
#define EPL_NMT_GS_INITIALISATION   0x0009	// super state
#define EPL_NMT_GS_COMMUNICATING    0x000C	// super state
#define EPL_NMT_CS_EPLMODE          0x000D	// super state
#define EPL_NMT_MS_EPLMODE          0x000D	// super state

#define EPL_NMT_SUPERSTATE_MASK     0x000F	// mask to select state

#define EPL_NMT_TYPE_UNDEFINED      0x0000	// type of NMT state is still undefined
#define EPL_NMT_TYPE_CS             0x0100	// CS type of NMT state
#define EPL_NMT_TYPE_MS             0x0200	// MS type of NMT state
#define EPL_NMT_TYPE_MASK           0x0300	// mask to select type of NMT state (i.e. CS or MS)

//---------------------------------------------------------------------------
// typedef
//---------------------------------------------------------------------------

// the lower Byte of the NMT-State is encoded
// like the values in the EPL-Standard
// the higher byte is used to encode MN
// (Bit 1 of the higher byte = 1) or CN (Bit 0 of the
// higher byte  = 1)
// the super-states are not mentioned in this
// enum because they are no real states
// --> there are masks defined to indentify the
// super-states

typedef enum {
	kEplNmtGsOff = 0x0000,
	kEplNmtGsInitialising = 0x0019,
	kEplNmtGsResetApplication = 0x0029,
	kEplNmtGsResetCommunication = 0x0039,
	kEplNmtGsResetConfiguration = 0x0079,
	kEplNmtCsNotActive = 0x011C,
	kEplNmtCsPreOperational1 = 0x011D,
	kEplNmtCsStopped = 0x014D,
	kEplNmtCsPreOperational2 = 0x015D,
	kEplNmtCsReadyToOperate = 0x016D,
	kEplNmtCsOperational = 0x01FD,
	kEplNmtCsBasicEthernet = 0x011E,
	kEplNmtMsNotActive = 0x021C,
	kEplNmtMsPreOperational1 = 0x021D,
	kEplNmtMsPreOperational2 = 0x025D,
	kEplNmtMsReadyToOperate = 0x026D,
	kEplNmtMsOperational = 0x02FD,
	kEplNmtMsBasicEthernet = 0x021E
} tEplNmtState;

// NMT-events
typedef enum {
	// Events from DLL
	// Events defined by EPL V2 specification
	kEplNmtEventNoEvent = 0x00,
//    kEplNmtEventDllMePres           =   0x01,
	kEplNmtEventDllMePresTimeout = 0x02,
//    kEplNmtEventDllMeAsnd           =   0x03,
//    kEplNmtEventDllMeAsndTimeout    =   0x04,
	kEplNmtEventDllMeSoaSent = 0x04,
	kEplNmtEventDllMeSocTrig = 0x05,
	kEplNmtEventDllMeSoaTrig = 0x06,
	kEplNmtEventDllCeSoc = 0x07,
	kEplNmtEventDllCePreq = 0x08,
	kEplNmtEventDllCePres = 0x09,
	kEplNmtEventDllCeSoa = 0x0A,
	kEplNmtEventDllCeAsnd = 0x0B,
	kEplNmtEventDllCeFrameTimeout = 0x0C,

	// Events triggered by NMT-Commands
	kEplNmtEventSwReset = 0x10,	// NMT_GT1, NMT_GT2, NMT_GT8
	kEplNmtEventResetNode = 0x11,
	kEplNmtEventResetCom = 0x12,
	kEplNmtEventResetConfig = 0x13,
	kEplNmtEventEnterPreOperational2 = 0x14,
	kEplNmtEventEnableReadyToOperate = 0x15,
	kEplNmtEventStartNode = 0x16,	// NMT_CT7
	kEplNmtEventStopNode = 0x17,

	// Events triggered by higher layer
	kEplNmtEventEnterResetApp = 0x20,
	kEplNmtEventEnterResetCom = 0x21,
	kEplNmtEventInternComError = 0x22,	// NMT_GT6, internal communication error -> enter ResetCommunication
	kEplNmtEventEnterResetConfig = 0x23,
	kEplNmtEventEnterCsNotActive = 0x24,
	kEplNmtEventEnterMsNotActive = 0x25,
	kEplNmtEventTimerBasicEthernet = 0x26,	// NMT_CT3; timer triggered state change (NotActive -> BasicEth)
	kEplNmtEventTimerMsPreOp1 = 0x27,	// enter PreOp1 on MN (NotActive -> MsPreOp1)
	kEplNmtEventNmtCycleError = 0x28,	// NMT_CT11, NMT_MT6; error during cycle -> enter PreOp1
	kEplNmtEventTimerMsPreOp2 = 0x29,	// enter PreOp2 on MN (MsPreOp1 -> MsPreOp2 if kEplNmtEventAllMandatoryCNIdent)
	kEplNmtEventAllMandatoryCNIdent = 0x2A,	// enter PreOp2 on MN if kEplNmtEventTimerMsPreOp2
	kEplNmtEventEnterReadyToOperate = 0x2B,	// application ready for the state ReadyToOp
	kEplNmtEventEnterMsOperational = 0x2C,	// enter Operational on MN
	kEplNmtEventSwitchOff = 0x2D,	// enter state Off
	kEplNmtEventCriticalError = 0x2E,	// enter state Off because of critical error

} tEplNmtEvent;

// type for argument of event kEplEventTypeNmtStateChange
typedef struct {
	tEplNmtState m_NewNmtState;
	tEplNmtEvent m_NmtEvent;

} tEplEventNmtStateChange;

// structure for kEplEventTypeHeartbeat
typedef struct {
	unsigned int m_uiNodeId;	// NodeId
	tEplNmtState m_NmtState;	// NMT state (remember distinguish between MN / CN)
	u16 m_wErrorCode;	// EPL error code in case of NMT state NotActive

} tEplHeartbeatEvent;

typedef enum {
	kEplNmtNodeEventFound = 0x00,
	kEplNmtNodeEventUpdateSw = 0x01,	// application shall update software on CN
	kEplNmtNodeEventCheckConf = 0x02,	// application / Configuration Manager shall check and update configuration on CN
	kEplNmtNodeEventUpdateConf = 0x03,	// application / Configuration Manager shall update configuration on CN (check was done by NmtMn module)
	kEplNmtNodeEventVerifyConf = 0x04,	// application / Configuration Manager shall verify configuration of CN
	kEplNmtNodeEventReadyToStart = 0x05,	// issued if EPL_NMTST_NO_STARTNODE set
	// application must call EplNmtMnuSendNmtCommand(kEplNmtCmdStartNode) manually
	kEplNmtNodeEventNmtState = 0x06,
	kEplNmtNodeEventError = 0x07,	// NMT error of CN

} tEplNmtNodeEvent;

typedef enum {
	kEplNmtNodeCommandBoot = 0x01,	// if EPL_NODEASSIGN_START_CN not set it must be issued after kEplNmtNodeEventFound
	kEplNmtNodeCommandSwOk = 0x02,	// application updated software on CN successfully
	kEplNmtNodeCommandSwUpdated = 0x03,	// application updated software on CN successfully
	kEplNmtNodeCommandConfOk = 0x04,	// application / Configuration Manager has updated configuration on CN successfully
	kEplNmtNodeCommandConfReset = 0x05,	// application / Configuration Manager has updated configuration on CN successfully
	// and CN needs ResetConf so that the configuration gets actived
	kEplNmtNodeCommandConfErr = 0x06,	// application / Configuration Manager failed on updating configuration on CN
	kEplNmtNodeCommandStart = 0x07,	// if EPL_NMTST_NO_STARTNODE set it must be issued after kEplNmtNodeEventReadyToStart

} tEplNmtNodeCommand;

typedef enum {
	kEplNmtBootEventBootStep1Finish = 0x00,	// PreOp2 is possible
	kEplNmtBootEventBootStep2Finish = 0x01,	// ReadyToOp is possible
	kEplNmtBootEventCheckComFinish = 0x02,	// Operational is possible
	kEplNmtBootEventOperational = 0x03,	// all mandatory CNs are Operational
	kEplNmtBootEventError = 0x04,	// boot process halted because of an error

} tEplNmtBootEvent;

//---------------------------------------------------------------------------
// function prototypes
//---------------------------------------------------------------------------

#endif // #ifndef _EPLNMT_H_
