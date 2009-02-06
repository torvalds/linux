/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  include file for event module

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

                $RCSfile: EplEvent.h,v $

                $Author: D.Krueger $

                $Revision: 1.8 $  $Date: 2008/11/17 16:40:39 $

                $State: Exp $

                Build Environment:
                    GCC V3.4

  -------------------------------------------------------------------------

  Revision History:

  2006/06/12 d.k.:   start of the implementation, version 1.00

****************************************************************************/

#ifndef _EPL_EVENT_H_
#define _EPL_EVENT_H_

#include "EplInc.h"
#include "EplNmt.h"

//---------------------------------------------------------------------------
// const defines
//---------------------------------------------------------------------------

// name and size of event queues
#define EPL_EVENT_NAME_SHB_KERNEL_TO_USER   "ShbKernelToUser"
#ifndef EPL_EVENT_SIZE_SHB_KERNEL_TO_USER
#define EPL_EVENT_SIZE_SHB_KERNEL_TO_USER   32768	// 32 kByte
#endif

#define EPL_EVENT_NAME_SHB_USER_TO_KERNEL   "ShbUserToKernel"
#ifndef EPL_EVENT_SIZE_SHB_USER_TO_KERNEL
#define EPL_EVENT_SIZE_SHB_USER_TO_KERNEL   32768	// 32 kByte
#endif

// max size of event argument
#ifndef EPL_MAX_EVENT_ARG_SIZE
#define EPL_MAX_EVENT_ARG_SIZE      256	// because of PDO
#endif

#define EPL_DLL_ERR_MN_CRC           0x00000001L	// object 0x1C00
#define EPL_DLL_ERR_MN_COLLISION     0x00000002L	// object 0x1C01
#define EPL_DLL_ERR_MN_CYCTIMEEXCEED 0x00000004L	// object 0x1C02
#define EPL_DLL_ERR_MN_LOSS_LINK     0x00000008L	// object 0x1C03
#define EPL_DLL_ERR_MN_CN_LATE_PRES  0x00000010L	// objects 0x1C04-0x1C06
#define EPL_DLL_ERR_MN_CN_LOSS_PRES  0x00000080L	// objects 0x1C07-0x1C09
#define EPL_DLL_ERR_CN_COLLISION     0x00000400L	// object 0x1C0A
#define EPL_DLL_ERR_CN_LOSS_SOC      0x00000800L	// object 0x1C0B
#define EPL_DLL_ERR_CN_LOSS_SOA      0x00001000L	// object 0x1C0C
#define EPL_DLL_ERR_CN_LOSS_PREQ     0x00002000L	// object 0x1C0D
#define EPL_DLL_ERR_CN_RECVD_PREQ    0x00004000L	// decrement object 0x1C0D/2
#define EPL_DLL_ERR_CN_SOC_JITTER    0x00008000L	// object 0x1C0E
#define EPL_DLL_ERR_CN_CRC           0x00010000L	// object 0x1C0F
#define EPL_DLL_ERR_CN_LOSS_LINK     0x00020000L	// object 0x1C10
#define EPL_DLL_ERR_MN_LOSS_STATRES  0x00040000L	// objects 0x1C15-0x1C17 (should be operated by NmtMnu module)
#define EPL_DLL_ERR_BAD_PHYS_MODE    0x00080000L	// no object
#define EPL_DLL_ERR_MAC_BUFFER       0x00100000L	// no object (NMT_GT6)
#define EPL_DLL_ERR_INVALID_FORMAT   0x00200000L	// no object (NMT_GT6)
#define EPL_DLL_ERR_ADDRESS_CONFLICT 0x00400000L	// no object (remove CN from configuration)

//---------------------------------------------------------------------------
// typedef
//---------------------------------------------------------------------------

// EventType determines the argument of the event
typedef enum {
	kEplEventTypeNmtEvent = 0x01,	// NMT event
	// arg is pointer to tEplNmtEvent
	kEplEventTypePdoRx = 0x02,	// PDO frame received event (PRes/PReq)
	// arg is pointer to tEplFrame
	kEplEventTypePdoTx = 0x03,	// PDO frame transmitted event (PRes/PReq)
	// arg is pointer to tEplFrameInfo
	kEplEventTypePdoSoa = 0x04,	// SoA frame received event (isochronous phase completed)
	// arg is pointer to nothing
	kEplEventTypeSync = 0x05,	// Sync event (e.g. SoC or anticipated SoC)
	// arg is pointer to nothing
	kEplEventTypeTimer = 0x06,	// Timer event
	// arg is pointer to tEplTimerEventArg
	kEplEventTypeHeartbeat = 0x07,	// Heartbeat event
	// arg is pointer to tEplHeartbeatEvent
	kEplEventTypeDllkCreate = 0x08,	// DLL kernel create event
	// arg is pointer to the new tEplNmtState
	kEplEventTypeDllkDestroy = 0x09,	// DLL kernel destroy event
	// arg is pointer to the old tEplNmtState
	kEplEventTypeDllkFillTx = 0x0A,	// DLL kernel fill TxBuffer event
	// arg is pointer to tEplDllAsyncReqPriority
	kEplEventTypeDllkPresReady = 0x0B,	// DLL kernel PRes ready event
	// arg is pointer to nothing
	kEplEventTypeError = 0x0C,	// Error event for API layer
	// arg is pointer to tEplEventError
	kEplEventTypeNmtStateChange = 0x0D,	// indicate change of NMT-State
	// arg is pointer to tEplEventNmtStateChange
	kEplEventTypeDllError = 0x0E,	// DLL error event for Error handler
	// arg is pointer to tEplErrorHandlerkEvent
	kEplEventTypeAsndRx = 0x0F,	// received ASnd frame for DLL user module
	// arg is pointer to tEplFrame
	kEplEventTypeDllkServFilter = 0x10,	// configure ServiceIdFilter
	// arg is pointer to tEplDllCalServiceIdFilter
	kEplEventTypeDllkIdentity = 0x11,	// configure Identity
	// arg is pointer to tEplDllIdentParam
	kEplEventTypeDllkConfig = 0x12,	// configure ConfigParam
	// arg is pointer to tEplDllConfigParam
	kEplEventTypeDllkIssueReq = 0x13,	// issue Ident/Status request
	// arg is pointer to tEplDllCalIssueRequest
	kEplEventTypeDllkAddNode = 0x14,	// add node to isochronous phase
	// arg is pointer to tEplDllNodeInfo
	kEplEventTypeDllkDelNode = 0x15,	// remove node from isochronous phase
	// arg is pointer to unsigned int
	kEplEventTypeDllkSoftDelNode = 0x16,	// remove node softly from isochronous phase
	// arg is pointer to unsigned int
	kEplEventTypeDllkStartReducedCycle = 0x17,	// start reduced EPL cycle on MN
	// arg is pointer to nothing
	kEplEventTypeNmtMnuNmtCmdSent = 0x18,	// NMT command was actually sent
	// arg is pointer to tEplFrame

} tEplEventType;

// EventSink determines the consumer of the event
typedef enum {
	kEplEventSinkSync = 0x00,	// Sync event for application or kernel EPL module
	kEplEventSinkNmtk = 0x01,	// events for Nmtk module
	kEplEventSinkDllk = 0x02,	// events for Dllk module
	kEplEventSinkDlluCal = 0x03,	// events for DlluCal module
	kEplEventSinkDllkCal = 0x04,	// events for DllkCal module
	kEplEventSinkPdok = 0x05,	// events for Pdok module
	kEplEventSinkNmtu = 0x06,	// events for Nmtu module
	kEplEventSinkErrk = 0x07,	// events for Error handler module
	kEplEventSinkErru = 0x08,	// events for Error signaling module
	kEplEventSinkSdoAsySeq = 0x09,	// events for asyncronous SDO Sequence Layer module
	kEplEventSinkNmtMnu = 0x0A,	// events for NmtMnu module
	kEplEventSinkLedu = 0x0B,	// events for Ledu module
	kEplEventSinkApi = 0x0F,	// events for API module

} tEplEventSink;

// EventSource determines the source of an errorevent
typedef enum {
	// kernelspace modules
	kEplEventSourceDllk = 0x01,	// Dllk module
	kEplEventSourceNmtk = 0x02,	// Nmtk module
	kEplEventSourceObdk = 0x03,	// Obdk module
	kEplEventSourcePdok = 0x04,	// Pdok module
	kEplEventSourceTimerk = 0x05,	// Timerk module
	kEplEventSourceEventk = 0x06,	// Eventk module
	kEplEventSourceSyncCb = 0x07,	// sync-Cb
	kEplEventSourceErrk = 0x08,	// Error handler module

	// userspace modules
	kEplEventSourceDllu = 0x10,	// Dllu module
	kEplEventSourceNmtu = 0x11,	// Nmtu module
	kEplEventSourceNmtCnu = 0x12,	// NmtCnu module
	kEplEventSourceNmtMnu = 0x13,	// NmtMnu module
	kEplEventSourceObdu = 0x14,	// Obdu module
	kEplEventSourceSdoUdp = 0x15,	// Sdo/Udp module
	kEplEventSourceSdoAsnd = 0x16,	// Sdo/Asnd module
	kEplEventSourceSdoAsySeq = 0x17,	// Sdo asynchronus Sequence Layer module
	kEplEventSourceSdoCom = 0x18,	// Sdo command layer module
	kEplEventSourceTimeru = 0x19,	// Timeru module
	kEplEventSourceCfgMau = 0x1A,	// CfgMau module
	kEplEventSourceEventu = 0x1B,	// Eventu module
	kEplEventSourceEplApi = 0x1C,	// Api module
	kEplEventSourceLedu = 0x1D,	// Ledu module

} tEplEventSource;

// structure of EPL event (element order must not be changed!)
typedef struct {
	tEplEventType m_EventType /*:28 */ ;	// event type
	tEplEventSink m_EventSink /*:4 */ ;	// event sink
	tEplNetTime m_NetTime;	// timestamp
	unsigned int m_uiSize;	// size of argument
	void *m_pArg;		// argument of event

} tEplEvent;

// short structure of EPL event without argument and its size (element order must not be changed!)
typedef struct {
	tEplEventType m_EventType /*:28 */ ;	// event type
	tEplEventSink m_EventSink /*:4 */ ;	// event sink
	tEplNetTime m_NetTime;	// timestamp

} tEplEventShort;

typedef struct {
	unsigned int m_uiIndex;
	unsigned int m_uiSubIndex;

} tEplEventObdError;

// structure for kEplEventTypeError
typedef struct {
	tEplEventSource m_EventSource;	// module which posted this error event
	tEplKernel m_EplError;	// EPL error which occured
	union {
		BYTE m_bArg;
		DWORD m_dwArg;
		tEplEventSource m_EventSource;	// from Eventk/u module (originating error source)
		tEplEventObdError m_ObdError;	// from Obd module
//        tEplErrHistoryEntry     m_HistoryEntry; // from Nmtk/u module

	} m_Arg;

} tEplEventError;

// structure for kEplEventTypeDllError
typedef struct {
	unsigned long m_ulDllErrorEvents;	// EPL_DLL_ERR_*
	unsigned int m_uiNodeId;
	tEplNmtState m_NmtState;

} tEplErrorHandlerkEvent;

// callback function to get informed about sync event
typedef tEplKernel(PUBLIC * tEplSyncCb) (void);

// callback function for generic events
typedef tEplKernel(PUBLIC * tEplProcessEventCb) (tEplEvent * pEplEvent_p);

//---------------------------------------------------------------------------
// function prototypes
//---------------------------------------------------------------------------

#endif // #ifndef _EPL_EVENT_H_
