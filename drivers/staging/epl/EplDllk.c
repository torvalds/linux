/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  source file for kernel DLL module

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

                $RCSfile: EplDllk.c,v $

                $Author: D.Krueger $

                $Revision: 1.21 $  $Date: 2008/11/13 17:13:09 $

                $State: Exp $

                Build Environment:
                    GCC V3.4

  -------------------------------------------------------------------------

  Revision History:

  2006/06/12 d.k.:   start of the implementation, version 1.00

****************************************************************************/

#include "kernel/EplDllk.h"
#include "kernel/EplDllkCal.h"
#include "kernel/EplEventk.h"
#include "kernel/EplNmtk.h"
#include "edrv.h"
#include "Benchmark.h"

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_PDOK)) != 0)
#include "kernel/EplPdok.h"
#endif

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_VETH)) != 0)
#include "kernel/VirtualEthernet.h"
#endif

//#if EPL_TIMER_USE_HIGHRES != FALSE
#include "kernel/EplTimerHighResk.h"
//#endif

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_DLLK)) != 0)

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMTK)) == 0)
#error "EPL module DLLK needs EPL module NMTK!"
#endif

#if (EPL_DLL_PRES_READY_AFTER_SOA != FALSE) && (EPL_DLL_PRES_READY_AFTER_SOC != FALSE)
#error "EPL module DLLK: select only one of EPL_DLL_PRES_READY_AFTER_SOA and EPL_DLL_PRES_READY_AFTER_SOC."
#endif

#if ((EPL_DLL_PRES_READY_AFTER_SOA != FALSE) || (EPL_DLL_PRES_READY_AFTER_SOC != FALSE)) \
    && (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) == 0)
#error "EPL module DLLK: currently, EPL_DLL_PRES_READY_AFTER_* is not supported if EPL_MODULE_NMT_MN is enabled."
#endif

#if (EDRV_FAST_TXFRAMES == FALSE) && \
    ((EPL_DLL_PRES_READY_AFTER_SOA != FALSE) || (EPL_DLL_PRES_READY_AFTER_SOC != FALSE))
#error "EPL module DLLK: EPL_DLL_PRES_READY_AFTER_* is enabled, but not EDRV_FAST_TXFRAMES."
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
void TgtDbgSignalTracePoint(u8 bTracePointNumber_p);
void TgtDbgPostTraceValue(u32 dwTraceValue_p);
#define TGT_DBG_SIGNAL_TRACE_POINT(p)   TgtDbgSignalTracePoint(p)
#define TGT_DBG_POST_TRACE_VALUE(v)     TgtDbgPostTraceValue(v)
#else
#define TGT_DBG_SIGNAL_TRACE_POINT(p)
#define TGT_DBG_POST_TRACE_VALUE(v)
#endif
#define EPL_DLLK_DBG_POST_TRACE_VALUE(Event_p, uiNodeId_p, wErrorCode_p) \
    TGT_DBG_POST_TRACE_VALUE((kEplEventSinkDllk << 28) | (Event_p << 24) \
                             | (uiNodeId_p << 16) | wErrorCode_p)

/***************************************************************************/
/*                                                                         */
/*                                                                         */
/*          C L A S S  EplDllk                                             */
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
//          P R I V A T E   D E F I N I T I O N S                          //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
// const defines
//---------------------------------------------------------------------------

// defines for indexes of tEplDllInstance.m_pTxFrameInfo
#define EPL_DLLK_TXFRAME_IDENTRES   0	// IdentResponse on CN / MN
#define EPL_DLLK_TXFRAME_STATUSRES  1	// StatusResponse on CN / MN
#define EPL_DLLK_TXFRAME_NMTREQ     2	// NMT Request from FIFO on CN / MN
#define EPL_DLLK_TXFRAME_NONEPL     3	// non-EPL frame from FIFO on CN / MN
#define EPL_DLLK_TXFRAME_PRES       4	// PRes on CN / MN
#define EPL_DLLK_TXFRAME_SOC        5	// SoC on MN
#define EPL_DLLK_TXFRAME_SOA        6	// SoA on MN
#define EPL_DLLK_TXFRAME_PREQ       7	// PReq on MN
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
#define EPL_DLLK_TXFRAME_COUNT      (7 + EPL_D_NMT_MaxCNNumber_U8 + 2)	// on MN: 7 + MaxPReq of regular CNs + 1 Diag + 1 Router
#else
#define EPL_DLLK_TXFRAME_COUNT      5	// on CN: 5
#endif

#define EPL_DLLK_BUFLEN_EMPTY       0	// buffer is empty
#define EPL_DLLK_BUFLEN_FILLING     1	// just the buffer is being filled
#define EPL_DLLK_BUFLEN_MIN         60	// minimum ethernet frame length

//---------------------------------------------------------------------------
// local types
//---------------------------------------------------------------------------

typedef enum {
	kEplDllGsInit = 0x00,	// MN/CN: initialisation (< PreOp2)
	kEplDllCsWaitPreq = 0x01,	// CN: wait for PReq frame
	kEplDllCsWaitSoc = 0x02,	// CN: wait for SoC frame
	kEplDllCsWaitSoa = 0x03,	// CN: wait for SoA frame
	kEplDllMsNonCyclic = 0x04,	// MN: reduced EPL cycle (PreOp1)
	kEplDllMsWaitSocTrig = 0x05,	// MN: wait for SoC trigger (cycle timer)
	kEplDllMsWaitPreqTrig = 0x06,	// MN: wait for (first) PReq trigger (WaitSoCPReq_U32)
	kEplDllMsWaitPres = 0x07,	// MN: wait for PRes frame from CN
	kEplDllMsWaitSoaTrig = 0x08,	// MN: wait for SoA trigger (PRes transmitted)
	kEplDllMsWaitAsndTrig = 0x09,	// MN: wait for ASnd trigger (SoA transmitted)
	kEplDllMsWaitAsnd = 0x0A,	// MN: wait for ASnd frame if SoA contained invitation

} tEplDllState;

typedef struct {
	u8 m_be_abSrcMac[6];
	tEdrvTxBuffer *m_pTxBuffer;	// Buffers for Tx-Frames
	unsigned int m_uiMaxTxFrames;
	u8 m_bFlag1;		// Flag 1 with EN, EC for PRes, StatusRes
	u8 m_bMnFlag1;	// Flag 1 with EA, ER from PReq, SoA of MN
	u8 m_bFlag2;		// Flag 2 with PR and RS for PRes, StatusRes, IdentRes
	tEplDllConfigParam m_DllConfigParam;
	tEplDllIdentParam m_DllIdentParam;
	tEplDllState m_DllState;
	tEplDllkCbAsync m_pfnCbAsync;
	tEplDllAsndFilter m_aAsndFilter[EPL_DLL_MAX_ASND_SERVICE_ID];

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
	tEplDllkNodeInfo *m_pFirstNodeInfo;
	tEplDllkNodeInfo *m_pCurNodeInfo;
	tEplDllkNodeInfo m_aNodeInfo[EPL_NMT_MAX_NODE_ID];
	tEplDllReqServiceId m_LastReqServiceId;
	unsigned int m_uiLastTargetNodeId;
#endif

#if EPL_TIMER_USE_HIGHRES != FALSE
	tEplTimerHdl m_TimerHdlCycle;	// used for EPL cycle monitoring on CN and generation on MN
#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
	tEplTimerHdl m_TimerHdlResponse;	// used for CN response monitoring
#endif				//(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
#endif

	unsigned int m_uiCycleCount;	// cycle counter (needed for multiplexed cycle support)
	unsigned long long m_ullFrameTimeout;	// frame timeout (cycle length + loss of frame tolerance)

} tEplDllkInstance;

//---------------------------------------------------------------------------
// local vars
//---------------------------------------------------------------------------

// if no dynamic memory allocation shall be used
// define structures statically
static tEplDllkInstance EplDllkInstance_g;

static tEdrvTxBuffer aEplDllkTxBuffer_l[EPL_DLLK_TXFRAME_COUNT];

//---------------------------------------------------------------------------
// local function prototypes
//---------------------------------------------------------------------------

// change DLL state on event
static tEplKernel EplDllkChangeState(tEplNmtEvent NmtEvent_p,
				     tEplNmtState NmtState_p);

// called from EdrvInterruptHandler()
static void EplDllkCbFrameReceived(tEdrvRxBuffer * pRxBuffer_p);

// called from EdrvInterruptHandler()
static void EplDllkCbFrameTransmitted(tEdrvTxBuffer * pTxBuffer_p);

// check frame and set missing information
static tEplKernel EplDllkCheckFrame(tEplFrame * pFrame_p,
				    unsigned int uiFrameSize_p);

// called by high resolution timer module to monitor EPL cycle as CN
#if EPL_TIMER_USE_HIGHRES != FALSE
static tEplKernel EplDllkCbCnTimer(tEplTimerEventArg *pEventArg_p);
#endif

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
// MN: returns internal node info structure
static tEplDllkNodeInfo *EplDllkGetNodeInfo(unsigned int uiNodeId_p);

// transmit SoA
static tEplKernel EplDllkMnSendSoa(tEplNmtState NmtState_p,
				   tEplDllState * pDllStateProposed_p,
				   BOOL fEnableInvitation_p);

static tEplKernel EplDllkMnSendSoc(void);

static tEplKernel EplDllkMnSendPreq(tEplNmtState NmtState_p,
				    tEplDllState * pDllStateProposed_p);

static tEplKernel EplDllkAsyncFrameNotReceived(tEplDllReqServiceId
					       ReqServiceId_p,
					       unsigned int uiNodeId_p);

static tEplKernel EplDllkCbMnTimerCycle(tEplTimerEventArg *pEventArg_p);

static tEplKernel EplDllkCbMnTimerResponse(tEplTimerEventArg *pEventArg_p);

#endif

//=========================================================================//
//                                                                         //
//          P U B L I C   F U N C T I O N S                                //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//
// Function:    EplDllkAddInstance()
//
// Description: add and initialize new instance of EPL stack
//
// Parameters:  pInitParam_p            = initialisation parameters like MAC address
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplDllkAddInstance(tEplDllkInitParam * pInitParam_p)
{
	tEplKernel Ret = kEplSuccessful;
	unsigned int uiIndex;
	tEdrvInitParam EdrvInitParam;

	// reset instance structure
	EPL_MEMSET(&EplDllkInstance_g, 0, sizeof(EplDllkInstance_g));

#if EPL_TIMER_USE_HIGHRES != FALSE
	Ret = EplTimerHighReskInit();
	if (Ret != kEplSuccessful) {	// error occured while initializing high resolution timer module
		goto Exit;
	}
#endif

	// if dynamic memory allocation available
	// allocate instance structure
	// allocate TPDO and RPDO table with default size

	// initialize and link pointers in instance structure to frame tables
	EplDllkInstance_g.m_pTxBuffer = aEplDllkTxBuffer_l;
	EplDllkInstance_g.m_uiMaxTxFrames =
	    sizeof(aEplDllkTxBuffer_l) / sizeof(tEdrvTxBuffer);

	// initialize state
	EplDllkInstance_g.m_DllState = kEplDllGsInit;

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
	// set up node info structure
	for (uiIndex = 0; uiIndex < tabentries(EplDllkInstance_g.m_aNodeInfo);
	     uiIndex++) {
		EplDllkInstance_g.m_aNodeInfo[uiIndex].m_uiNodeId = uiIndex + 1;
		EplDllkInstance_g.m_aNodeInfo[uiIndex].m_wPresPayloadLimit =
		    0xFFFF;
	}
#endif

	// initialize Edrv
	EPL_MEMCPY(EdrvInitParam.m_abMyMacAddr, pInitParam_p->m_be_abSrcMac, 6);
	EdrvInitParam.m_pfnRxHandler = EplDllkCbFrameReceived;
	EdrvInitParam.m_pfnTxHandler = EplDllkCbFrameTransmitted;
	Ret = EdrvInit(&EdrvInitParam);
	if (Ret != kEplSuccessful) {	// error occured while initializing ethernet driver
		goto Exit;
	}
	// copy local MAC address from Ethernet driver back to local instance structure
	// because Ethernet driver may have read it from controller EEPROM
	EPL_MEMCPY(EplDllkInstance_g.m_be_abSrcMac, EdrvInitParam.m_abMyMacAddr,
		   6);
	EPL_MEMCPY(pInitParam_p->m_be_abSrcMac, EdrvInitParam.m_abMyMacAddr, 6);

	// initialize TxBuffer array
	for (uiIndex = 0; uiIndex < EplDllkInstance_g.m_uiMaxTxFrames;
	     uiIndex++) {
		EplDllkInstance_g.m_pTxBuffer[uiIndex].m_pbBuffer = NULL;
	}

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_VETH)) != 0)
	Ret = VEthAddInstance(pInitParam_p);
#endif

      Exit:
	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplDllkDelInstance()
//
// Description: deletes an instance of EPL stack
//
// Parameters:  (none)
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplDllkDelInstance(void)
{
	tEplKernel Ret = kEplSuccessful;

	// reset state
	EplDllkInstance_g.m_DllState = kEplDllGsInit;

#if EPL_TIMER_USE_HIGHRES != FALSE
	Ret = EplTimerHighReskDelInstance();
#endif

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_VETH)) != 0)
	Ret = VEthDelInstance();
#endif

	Ret = EdrvShutdown();
	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplDllkCreateTxFrame
//
// Description: creates the buffer for a Tx frame and registers it to the
//              ethernet driver
//
// Parameters:  puiHandle_p             = OUT: handle to frame buffer
//              ppFrame_p               = OUT: pointer to pointer of EPL frame
//              puiFrameSize_p          = IN/OUT: pointer to size of frame
//                                        returned size is always equal or larger than
//                                        requested size, if that is not possible
//                                        an error will be returned
//              MsgType_p               = EPL message type
//              ServiceId_p             = Service ID in case of ASnd frame, otherwise
//                                        kEplDllAsndNotDefined
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplDllkCreateTxFrame(unsigned int *puiHandle_p,
				tEplFrame ** ppFrame_p,
				unsigned int *puiFrameSize_p,
				tEplMsgType MsgType_p,
				tEplDllAsndServiceId ServiceId_p)
{
	tEplKernel Ret = kEplSuccessful;
	tEplFrame *pTxFrame;
	unsigned int uiHandle = EplDllkInstance_g.m_uiMaxTxFrames;
	tEdrvTxBuffer *pTxBuffer = NULL;

	if (MsgType_p == kEplMsgTypeAsnd) {
		// search for fixed Tx buffers
		if (ServiceId_p == kEplDllAsndIdentResponse) {
			uiHandle = EPL_DLLK_TXFRAME_IDENTRES;
		} else if (ServiceId_p == kEplDllAsndStatusResponse) {
			uiHandle = EPL_DLLK_TXFRAME_STATUSRES;
		} else if ((ServiceId_p == kEplDllAsndNmtRequest)
			   || (ServiceId_p == kEplDllAsndNmtCommand)) {
			uiHandle = EPL_DLLK_TXFRAME_NMTREQ;
		}

		if (uiHandle >= EplDllkInstance_g.m_uiMaxTxFrames) {	// look for free entry
			uiHandle = EPL_DLLK_TXFRAME_PREQ;
			pTxBuffer = &EplDllkInstance_g.m_pTxBuffer[uiHandle];
			for (; uiHandle < EplDllkInstance_g.m_uiMaxTxFrames;
			     uiHandle++, pTxBuffer++) {
				if (pTxBuffer->m_pbBuffer == NULL) {	// free entry found
					break;
				}
			}
		}
	} else if (MsgType_p == kEplMsgTypeNonEpl) {
		uiHandle = EPL_DLLK_TXFRAME_NONEPL;
	} else if (MsgType_p == kEplMsgTypePres) {
		uiHandle = EPL_DLLK_TXFRAME_PRES;
	} else if (MsgType_p == kEplMsgTypeSoc) {
		uiHandle = EPL_DLLK_TXFRAME_SOC;
	} else if (MsgType_p == kEplMsgTypeSoa) {
		uiHandle = EPL_DLLK_TXFRAME_SOA;
	} else {		// look for free entry
		uiHandle = EPL_DLLK_TXFRAME_PREQ;
		pTxBuffer = &EplDllkInstance_g.m_pTxBuffer[uiHandle];
		for (; uiHandle < EplDllkInstance_g.m_uiMaxTxFrames;
		     uiHandle++, pTxBuffer++) {
			if (pTxBuffer->m_pbBuffer == NULL) {	// free entry found
				break;
			}
		}
		if (pTxBuffer->m_pbBuffer != NULL) {
			Ret = kEplEdrvNoFreeBufEntry;
			goto Exit;
		}
	}

	// test if requested entry is free
	pTxBuffer = &EplDllkInstance_g.m_pTxBuffer[uiHandle];
	if (pTxBuffer->m_pbBuffer != NULL) {	// entry is not free
		Ret = kEplEdrvNoFreeBufEntry;
		goto Exit;
	}
	// setup Tx buffer
	pTxBuffer->m_EplMsgType = MsgType_p;
	pTxBuffer->m_uiMaxBufferLen = *puiFrameSize_p;

	Ret = EdrvAllocTxMsgBuffer(pTxBuffer);
	if (Ret != kEplSuccessful) {	// error occured while registering Tx frame
		goto Exit;
	}
	// because buffer size may be larger than requested
	// memorize real length of frame
	pTxBuffer->m_uiTxMsgLen = *puiFrameSize_p;

	// fill whole frame with 0
	EPL_MEMSET(pTxBuffer->m_pbBuffer, 0, pTxBuffer->m_uiMaxBufferLen);

	pTxFrame = (tEplFrame *) pTxBuffer->m_pbBuffer;

	if (MsgType_p != kEplMsgTypeNonEpl) {	// fill out Frame only if it is an EPL frame
		// ethertype
		AmiSetWordToBe(&pTxFrame->m_be_wEtherType,
			       EPL_C_DLL_ETHERTYPE_EPL);
		// source node ID
		AmiSetByteToLe(&pTxFrame->m_le_bSrcNodeId,
			       (u8) EplDllkInstance_g.m_DllConfigParam.
			       m_uiNodeId);
		// source MAC address
		EPL_MEMCPY(&pTxFrame->m_be_abSrcMac[0],
			   &EplDllkInstance_g.m_be_abSrcMac[0], 6);
		switch (MsgType_p) {
		case kEplMsgTypeAsnd:
			// destination MAC address
			AmiSetQword48ToBe(&pTxFrame->m_be_abDstMac[0],
					  EPL_C_DLL_MULTICAST_ASND);
			// destination node ID
			switch (ServiceId_p) {
			case kEplDllAsndIdentResponse:
			case kEplDllAsndStatusResponse:
				{	// IdentResponses and StatusResponses are Broadcast
					AmiSetByteToLe(&pTxFrame->
						       m_le_bDstNodeId,
						       (u8)
						       EPL_C_ADR_BROADCAST);
					break;
				}

			default:
				break;
			}
			// ASnd Service ID
			AmiSetByteToLe(&pTxFrame->m_Data.m_Asnd.m_le_bServiceId,
				       ServiceId_p);
			break;

		case kEplMsgTypeSoc:
			// destination MAC address
			AmiSetQword48ToBe(&pTxFrame->m_be_abDstMac[0],
					  EPL_C_DLL_MULTICAST_SOC);
			// destination node ID
			AmiSetByteToLe(&pTxFrame->m_le_bDstNodeId,
				       (u8) EPL_C_ADR_BROADCAST);
			// reset Flags
			//AmiSetByteToLe(&pTxFrame->m_Data.m_Soc.m_le_bFlag1, (u8) 0);
			//AmiSetByteToLe(&pTxFrame->m_Data.m_Soc.m_le_bFlag2, (u8) 0);
			break;

		case kEplMsgTypeSoa:
			// destination MAC address
			AmiSetQword48ToBe(&pTxFrame->m_be_abDstMac[0],
					  EPL_C_DLL_MULTICAST_SOA);
			// destination node ID
			AmiSetByteToLe(&pTxFrame->m_le_bDstNodeId,
				       (u8) EPL_C_ADR_BROADCAST);
			// reset Flags
			//AmiSetByteToLe(&pTxFrame->m_Data.m_Soa.m_le_bFlag1, (u8) 0);
			//AmiSetByteToLe(&pTxFrame->m_Data.m_Soa.m_le_bFlag2, (u8) 0);
			// EPL profile version
			AmiSetByteToLe(&pTxFrame->m_Data.m_Soa.m_le_bEplVersion,
				       (u8) EPL_SPEC_VERSION);
			break;

		case kEplMsgTypePres:
			// destination MAC address
			AmiSetQword48ToBe(&pTxFrame->m_be_abDstMac[0],
					  EPL_C_DLL_MULTICAST_PRES);
			// destination node ID
			AmiSetByteToLe(&pTxFrame->m_le_bDstNodeId,
				       (u8) EPL_C_ADR_BROADCAST);
			// reset Flags
			//AmiSetByteToLe(&pTxFrame->m_Data.m_Pres.m_le_bFlag1, (u8) 0);
			//AmiSetByteToLe(&pTxFrame->m_Data.m_Pres.m_le_bFlag2, (u8) 0);
			// PDO size
			//AmiSetWordToLe(&pTxFrame->m_Data.m_Pres.m_le_wSize, 0);
			break;

		case kEplMsgTypePreq:
			// reset Flags
			//AmiSetByteToLe(&pTxFrame->m_Data.m_Preq.m_le_bFlag1, (u8) 0);
			//AmiSetByteToLe(&pTxFrame->m_Data.m_Preq.m_le_bFlag2, (u8) 0);
			// PDO size
			//AmiSetWordToLe(&pTxFrame->m_Data.m_Preq.m_le_wSize, 0);
			break;

		default:
			break;
		}
		// EPL message type
		AmiSetByteToLe(&pTxFrame->m_le_bMessageType, (u8) MsgType_p);
	}

	*ppFrame_p = pTxFrame;
	*puiFrameSize_p = pTxBuffer->m_uiMaxBufferLen;
	*puiHandle_p = uiHandle;

      Exit:
	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplDllkDeleteTxFrame
//
// Description: deletes the buffer for a Tx frame and frees it in the
//              ethernet driver
//
// Parameters:  uiHandle_p              = IN: handle to frame buffer
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplDllkDeleteTxFrame(unsigned int uiHandle_p)
{
	tEplKernel Ret = kEplSuccessful;
	tEdrvTxBuffer *pTxBuffer = NULL;

	if (uiHandle_p >= EplDllkInstance_g.m_uiMaxTxFrames) {	// handle is not valid
		Ret = kEplDllIllegalHdl;
		goto Exit;
	}

	pTxBuffer = &EplDllkInstance_g.m_pTxBuffer[uiHandle_p];

	// mark buffer as free so that frame will not be send in future anymore
	// $$$ d.k. What's up with running transmissions?
	pTxBuffer->m_uiTxMsgLen = EPL_DLLK_BUFLEN_EMPTY;
	pTxBuffer->m_pbBuffer = NULL;

	// delete Tx buffer
	Ret = EdrvReleaseTxMsgBuffer(pTxBuffer);
	if (Ret != kEplSuccessful) {	// error occured while releasing Tx frame
		goto Exit;
	}

      Exit:
	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplDllkProcess
//
// Description: process the passed event
//
// Parameters:  pEvent_p                = event to be processed
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplDllkProcess(tEplEvent * pEvent_p)
{
	tEplKernel Ret = kEplSuccessful;
	tEplFrame *pTxFrame;
	tEdrvTxBuffer *pTxBuffer;
	unsigned int uiHandle;
	unsigned int uiFrameSize;
	u8 abMulticastMac[6];
	tEplDllAsyncReqPriority AsyncReqPriority;
	unsigned int uiFrameCount;
	tEplNmtState NmtState;
#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_PDOK)) != 0)
	tEplFrameInfo FrameInfo;
#endif

	switch (pEvent_p->m_EventType) {
	case kEplEventTypeDllkCreate:
		{
			// $$$ reset ethernet driver

			NmtState = *((tEplNmtState *) pEvent_p->m_pArg);

			// initialize flags for PRes and StatusRes
			EplDllkInstance_g.m_bFlag1 = EPL_FRAME_FLAG1_EC;
			EplDllkInstance_g.m_bMnFlag1 = 0;
			EplDllkInstance_g.m_bFlag2 = 0;

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
			// initialize linked node list
			EplDllkInstance_g.m_pFirstNodeInfo = NULL;
#endif

			// register TxFrames in Edrv

			// IdentResponse
			uiFrameSize = EPL_C_DLL_MINSIZE_IDENTRES;
			Ret =
			    EplDllkCreateTxFrame(&uiHandle, &pTxFrame,
						 &uiFrameSize, kEplMsgTypeAsnd,
						 kEplDllAsndIdentResponse);
			if (Ret != kEplSuccessful) {	// error occured while registering Tx frame
				goto Exit;
			}
			// EPL profile version
			AmiSetByteToLe(&pTxFrame->m_Data.m_Asnd.m_Payload.
				       m_IdentResponse.m_le_bEplProfileVersion,
				       (u8) EPL_SPEC_VERSION);
			// FeatureFlags
			AmiSetDwordToLe(&pTxFrame->m_Data.m_Asnd.m_Payload.
					m_IdentResponse.m_le_dwFeatureFlags,
					EplDllkInstance_g.m_DllConfigParam.
					m_dwFeatureFlags);
			// MTU
			AmiSetWordToLe(&pTxFrame->m_Data.m_Asnd.m_Payload.
				       m_IdentResponse.m_le_wMtu,
				       (u16) EplDllkInstance_g.
				       m_DllConfigParam.m_uiAsyncMtu);
			// PollInSize
			AmiSetWordToLe(&pTxFrame->m_Data.m_Asnd.m_Payload.
				       m_IdentResponse.m_le_wPollInSize,
				       (u16) EplDllkInstance_g.
				       m_DllConfigParam.
				       m_uiPreqActPayloadLimit);
			// PollOutSize
			AmiSetWordToLe(&pTxFrame->m_Data.m_Asnd.m_Payload.
				       m_IdentResponse.m_le_wPollOutSize,
				       (u16) EplDllkInstance_g.
				       m_DllConfigParam.
				       m_uiPresActPayloadLimit);
			// ResponseTime / PresMaxLatency
			AmiSetDwordToLe(&pTxFrame->m_Data.m_Asnd.m_Payload.
					m_IdentResponse.m_le_dwResponseTime,
					EplDllkInstance_g.m_DllConfigParam.
					m_dwPresMaxLatency);
			// DeviceType
			AmiSetDwordToLe(&pTxFrame->m_Data.m_Asnd.m_Payload.
					m_IdentResponse.m_le_dwDeviceType,
					EplDllkInstance_g.m_DllIdentParam.
					m_dwDeviceType);
			// VendorId
			AmiSetDwordToLe(&pTxFrame->m_Data.m_Asnd.m_Payload.
					m_IdentResponse.m_le_dwVendorId,
					EplDllkInstance_g.m_DllIdentParam.
					m_dwVendorId);
			// ProductCode
			AmiSetDwordToLe(&pTxFrame->m_Data.m_Asnd.m_Payload.
					m_IdentResponse.m_le_dwProductCode,
					EplDllkInstance_g.m_DllIdentParam.
					m_dwProductCode);
			// RevisionNumber
			AmiSetDwordToLe(&pTxFrame->m_Data.m_Asnd.m_Payload.
					m_IdentResponse.m_le_dwRevisionNumber,
					EplDllkInstance_g.m_DllIdentParam.
					m_dwRevisionNumber);
			// SerialNumber
			AmiSetDwordToLe(&pTxFrame->m_Data.m_Asnd.m_Payload.
					m_IdentResponse.m_le_dwSerialNumber,
					EplDllkInstance_g.m_DllIdentParam.
					m_dwSerialNumber);
			// VendorSpecificExt1
			AmiSetQword64ToLe(&pTxFrame->m_Data.m_Asnd.m_Payload.
					  m_IdentResponse.
					  m_le_qwVendorSpecificExt1,
					  EplDllkInstance_g.m_DllIdentParam.
					  m_qwVendorSpecificExt1);
			// VerifyConfigurationDate
			AmiSetDwordToLe(&pTxFrame->m_Data.m_Asnd.m_Payload.
					m_IdentResponse.
					m_le_dwVerifyConfigurationDate,
					EplDllkInstance_g.m_DllIdentParam.
					m_dwVerifyConfigurationDate);
			// VerifyConfigurationTime
			AmiSetDwordToLe(&pTxFrame->m_Data.m_Asnd.m_Payload.
					m_IdentResponse.
					m_le_dwVerifyConfigurationTime,
					EplDllkInstance_g.m_DllIdentParam.
					m_dwVerifyConfigurationTime);
			// ApplicationSwDate
			AmiSetDwordToLe(&pTxFrame->m_Data.m_Asnd.m_Payload.
					m_IdentResponse.
					m_le_dwApplicationSwDate,
					EplDllkInstance_g.m_DllIdentParam.
					m_dwApplicationSwDate);
			// ApplicationSwTime
			AmiSetDwordToLe(&pTxFrame->m_Data.m_Asnd.m_Payload.
					m_IdentResponse.
					m_le_dwApplicationSwTime,
					EplDllkInstance_g.m_DllIdentParam.
					m_dwApplicationSwTime);
			// IPAddress
			AmiSetDwordToLe(&pTxFrame->m_Data.m_Asnd.m_Payload.
					m_IdentResponse.m_le_dwIpAddress,
					EplDllkInstance_g.m_DllIdentParam.
					m_dwIpAddress);
			// SubnetMask
			AmiSetDwordToLe(&pTxFrame->m_Data.m_Asnd.m_Payload.
					m_IdentResponse.m_le_dwSubnetMask,
					EplDllkInstance_g.m_DllIdentParam.
					m_dwSubnetMask);
			// DefaultGateway
			AmiSetDwordToLe(&pTxFrame->m_Data.m_Asnd.m_Payload.
					m_IdentResponse.m_le_dwDefaultGateway,
					EplDllkInstance_g.m_DllIdentParam.
					m_dwDefaultGateway);
			// HostName
			EPL_MEMCPY(&pTxFrame->m_Data.m_Asnd.m_Payload.
				   m_IdentResponse.m_le_sHostname[0],
				   &EplDllkInstance_g.m_DllIdentParam.
				   m_sHostname[0],
				   sizeof(EplDllkInstance_g.m_DllIdentParam.
					  m_sHostname));
			// VendorSpecificExt2
			EPL_MEMCPY(&pTxFrame->m_Data.m_Asnd.m_Payload.
				   m_IdentResponse.m_le_abVendorSpecificExt2[0],
				   &EplDllkInstance_g.m_DllIdentParam.
				   m_abVendorSpecificExt2[0],
				   sizeof(EplDllkInstance_g.m_DllIdentParam.
					  m_abVendorSpecificExt2));

			// StatusResponse
			uiFrameSize = EPL_C_DLL_MINSIZE_STATUSRES;
			Ret =
			    EplDllkCreateTxFrame(&uiHandle, &pTxFrame,
						 &uiFrameSize, kEplMsgTypeAsnd,
						 kEplDllAsndStatusResponse);
			if (Ret != kEplSuccessful) {	// error occured while registering Tx frame
				goto Exit;
			}
			// PRes $$$ maybe move this to PDO module
			if ((EplDllkInstance_g.m_DllConfigParam.m_fAsyncOnly ==
			     FALSE)
			    && (EplDllkInstance_g.m_DllConfigParam.m_uiPresActPayloadLimit >= 36)) {	// it is not configured as async-only CN,
				// so take part in isochronous phase and register PRes frame
				uiFrameSize =
				    EplDllkInstance_g.m_DllConfigParam.
				    m_uiPresActPayloadLimit + 24;
				Ret =
				    EplDllkCreateTxFrame(&uiHandle, &pTxFrame,
							 &uiFrameSize,
							 kEplMsgTypePres,
							 kEplDllAsndNotDefined);
				if (Ret != kEplSuccessful) {	// error occured while registering Tx frame
					goto Exit;
				}
#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_PDOK)) != 0)
				// initially encode TPDO -> inform PDO module
				FrameInfo.m_pFrame = pTxFrame;
				FrameInfo.m_uiFrameSize = uiFrameSize;
				Ret = EplPdokCbPdoTransmitted(&FrameInfo);
#endif
				// reset cycle counter
				EplDllkInstance_g.m_uiCycleCount = 0;
			} else {	// it is an async-only CN
				// fool EplDllkChangeState() to think that PRes was not expected
				EplDllkInstance_g.m_uiCycleCount = 1;
			}

			// NMT request
			uiFrameSize = EPL_C_IP_MAX_MTU;
			Ret =
			    EplDllkCreateTxFrame(&uiHandle, &pTxFrame,
						 &uiFrameSize, kEplMsgTypeAsnd,
						 kEplDllAsndNmtRequest);
			if (Ret != kEplSuccessful) {	// error occured while registering Tx frame
				goto Exit;
			}
			// mark Tx buffer as empty
			EplDllkInstance_g.m_pTxBuffer[uiHandle].m_uiTxMsgLen =
			    EPL_DLLK_BUFLEN_EMPTY;

			// non-EPL frame
			uiFrameSize = EPL_C_IP_MAX_MTU;
			Ret =
			    EplDllkCreateTxFrame(&uiHandle, &pTxFrame,
						 &uiFrameSize,
						 kEplMsgTypeNonEpl,
						 kEplDllAsndNotDefined);
			if (Ret != kEplSuccessful) {	// error occured while registering Tx frame
				goto Exit;
			}
			// mark Tx buffer as empty
			EplDllkInstance_g.m_pTxBuffer[uiHandle].m_uiTxMsgLen =
			    EPL_DLLK_BUFLEN_EMPTY;

			// register multicast MACs in ethernet driver
			AmiSetQword48ToBe(&abMulticastMac[0],
					  EPL_C_DLL_MULTICAST_SOC);
			Ret = EdrvDefineRxMacAddrEntry(abMulticastMac);
			AmiSetQword48ToBe(&abMulticastMac[0],
					  EPL_C_DLL_MULTICAST_SOA);
			Ret = EdrvDefineRxMacAddrEntry(abMulticastMac);
			AmiSetQword48ToBe(&abMulticastMac[0],
					  EPL_C_DLL_MULTICAST_PRES);
			Ret = EdrvDefineRxMacAddrEntry(abMulticastMac);
			AmiSetQword48ToBe(&abMulticastMac[0],
					  EPL_C_DLL_MULTICAST_ASND);
			Ret = EdrvDefineRxMacAddrEntry(abMulticastMac);

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
			if (NmtState >= kEplNmtMsNotActive) {	// local node is MN
				unsigned int uiIndex;

				// SoC
				uiFrameSize = EPL_C_DLL_MINSIZE_SOC;
				Ret =
				    EplDllkCreateTxFrame(&uiHandle, &pTxFrame,
							 &uiFrameSize,
							 kEplMsgTypeSoc,
							 kEplDllAsndNotDefined);
				if (Ret != kEplSuccessful) {	// error occured while registering Tx frame
					goto Exit;
				}
				// SoA
				uiFrameSize = EPL_C_DLL_MINSIZE_SOA;
				Ret =
				    EplDllkCreateTxFrame(&uiHandle, &pTxFrame,
							 &uiFrameSize,
							 kEplMsgTypeSoa,
							 kEplDllAsndNotDefined);
				if (Ret != kEplSuccessful) {	// error occured while registering Tx frame
					goto Exit;
				}

				for (uiIndex = 0;
				     uiIndex <
				     tabentries(EplDllkInstance_g.m_aNodeInfo);
				     uiIndex++) {
//                    EplDllkInstance_g.m_aNodeInfo[uiIndex].m_uiNodeId = uiIndex + 1;
					EplDllkInstance_g.m_aNodeInfo[uiIndex].
					    m_wPresPayloadLimit =
					    (u16) EplDllkInstance_g.
					    m_DllConfigParam.
					    m_uiIsochrRxMaxPayload;
				}

				// calculate cycle length
				EplDllkInstance_g.m_ullFrameTimeout = 1000LL
				    *
				    ((unsigned long long)EplDllkInstance_g.
				     m_DllConfigParam.m_dwCycleLen);
			}
#endif //(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)

			Ret = EplDllkCalAsyncClearBuffer();

			break;
		}

	case kEplEventTypeDllkDestroy:
		{
			// destroy all data structures

			NmtState = *((tEplNmtState *) pEvent_p->m_pArg);

			// delete Tx frames
			Ret = EplDllkDeleteTxFrame(EPL_DLLK_TXFRAME_IDENTRES);
			if (Ret != kEplSuccessful) {	// error occured while deregistering Tx frame
				goto Exit;
			}

			Ret = EplDllkDeleteTxFrame(EPL_DLLK_TXFRAME_STATUSRES);
			if (Ret != kEplSuccessful) {	// error occured while deregistering Tx frame
				goto Exit;
			}

			Ret = EplDllkDeleteTxFrame(EPL_DLLK_TXFRAME_PRES);
			if (Ret != kEplSuccessful) {	// error occured while deregistering Tx frame
				goto Exit;
			}

			Ret = EplDllkDeleteTxFrame(EPL_DLLK_TXFRAME_NMTREQ);
			if (Ret != kEplSuccessful) {	// error occured while deregistering Tx frame
				goto Exit;
			}

			Ret = EplDllkDeleteTxFrame(EPL_DLLK_TXFRAME_NONEPL);
			if (Ret != kEplSuccessful) {	// error occured while deregistering Tx frame
				goto Exit;
			}
#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
			if (NmtState >= kEplNmtMsNotActive) {	// local node was MN
				unsigned int uiIndex;

				Ret =
				    EplDllkDeleteTxFrame(EPL_DLLK_TXFRAME_SOC);
				if (Ret != kEplSuccessful) {	// error occured while deregistering Tx frame
					goto Exit;
				}

				Ret =
				    EplDllkDeleteTxFrame(EPL_DLLK_TXFRAME_SOA);
				if (Ret != kEplSuccessful) {	// error occured while deregistering Tx frame
					goto Exit;
				}

				for (uiIndex = 0;
				     uiIndex <
				     tabentries(EplDllkInstance_g.m_aNodeInfo);
				     uiIndex++) {
					if (EplDllkInstance_g.
					    m_aNodeInfo[uiIndex].
					    m_pPreqTxBuffer != NULL) {
						uiHandle =
						    EplDllkInstance_g.
						    m_aNodeInfo[uiIndex].
						    m_pPreqTxBuffer -
						    EplDllkInstance_g.
						    m_pTxBuffer;
						EplDllkInstance_g.
						    m_aNodeInfo[uiIndex].
						    m_pPreqTxBuffer = NULL;
						Ret =
						    EplDllkDeleteTxFrame
						    (uiHandle);
						if (Ret != kEplSuccessful) {	// error occured while deregistering Tx frame
							goto Exit;
						}

					}
					EplDllkInstance_g.m_aNodeInfo[uiIndex].
					    m_wPresPayloadLimit = 0xFFFF;
				}
			}
#endif //(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)

			// deregister multicast MACs in ethernet driver
			AmiSetQword48ToBe(&abMulticastMac[0],
					  EPL_C_DLL_MULTICAST_SOC);
			Ret = EdrvUndefineRxMacAddrEntry(abMulticastMac);
			AmiSetQword48ToBe(&abMulticastMac[0],
					  EPL_C_DLL_MULTICAST_SOA);
			Ret = EdrvUndefineRxMacAddrEntry(abMulticastMac);
			AmiSetQword48ToBe(&abMulticastMac[0],
					  EPL_C_DLL_MULTICAST_PRES);
			Ret = EdrvUndefineRxMacAddrEntry(abMulticastMac);
			AmiSetQword48ToBe(&abMulticastMac[0],
					  EPL_C_DLL_MULTICAST_ASND);
			Ret = EdrvUndefineRxMacAddrEntry(abMulticastMac);

			// delete timer
#if EPL_TIMER_USE_HIGHRES != FALSE
			Ret =
			    EplTimerHighReskDeleteTimer(&EplDllkInstance_g.
							m_TimerHdlCycle);
#endif

			break;
		}

	case kEplEventTypeDllkFillTx:
		{
			// fill TxBuffer of specified priority with new frame if empty

			pTxFrame = NULL;
			AsyncReqPriority =
			    *((tEplDllAsyncReqPriority *) pEvent_p->m_pArg);
			switch (AsyncReqPriority) {
			case kEplDllAsyncReqPrioNmt:	// NMT request priority
				{
					pTxBuffer =
					    &EplDllkInstance_g.
					    m_pTxBuffer
					    [EPL_DLLK_TXFRAME_NMTREQ];
					if (pTxBuffer->m_pbBuffer != NULL) {	// NmtRequest does exist
						// check if frame is empty and not being filled
						if (pTxBuffer->m_uiTxMsgLen ==
						    EPL_DLLK_BUFLEN_EMPTY) {
							// mark Tx buffer as filling is in process
							pTxBuffer->
							    m_uiTxMsgLen =
							    EPL_DLLK_BUFLEN_FILLING;
							// set max buffer size as input parameter
							uiFrameSize =
							    pTxBuffer->
							    m_uiMaxBufferLen;
							// copy frame from shared loop buffer to Tx buffer
							Ret =
							    EplDllkCalAsyncGetTxFrame
							    (pTxBuffer->
							     m_pbBuffer,
							     &uiFrameSize,
							     AsyncReqPriority);
							if (Ret ==
							    kEplSuccessful) {
								pTxFrame =
								    (tEplFrame
								     *)
								    pTxBuffer->
								    m_pbBuffer;
								Ret =
								    EplDllkCheckFrame
								    (pTxFrame,
								     uiFrameSize);

								// set buffer valid
								pTxBuffer->
								    m_uiTxMsgLen
								    =
								    uiFrameSize;
							} else if (Ret == kEplDllAsyncTxBufferEmpty) {	// empty Tx buffer is not a real problem
								// so just ignore it
								Ret =
								    kEplSuccessful;
								// mark Tx buffer as empty
								pTxBuffer->
								    m_uiTxMsgLen
								    =
								    EPL_DLLK_BUFLEN_EMPTY;
							}
						}
					}
					break;
				}

			default:	// generic priority
				{
					pTxBuffer =
					    &EplDllkInstance_g.
					    m_pTxBuffer
					    [EPL_DLLK_TXFRAME_NONEPL];
					if (pTxBuffer->m_pbBuffer != NULL) {	// non-EPL frame does exist
						// check if frame is empty and not being filled
						if (pTxBuffer->m_uiTxMsgLen ==
						    EPL_DLLK_BUFLEN_EMPTY) {
							// mark Tx buffer as filling is in process
							pTxBuffer->
							    m_uiTxMsgLen =
							    EPL_DLLK_BUFLEN_FILLING;
							// set max buffer size as input parameter
							uiFrameSize =
							    pTxBuffer->
							    m_uiMaxBufferLen;
							// copy frame from shared loop buffer to Tx buffer
							Ret =
							    EplDllkCalAsyncGetTxFrame
							    (pTxBuffer->
							     m_pbBuffer,
							     &uiFrameSize,
							     AsyncReqPriority);
							if (Ret ==
							    kEplSuccessful) {
								pTxFrame =
								    (tEplFrame
								     *)
								    pTxBuffer->
								    m_pbBuffer;
								Ret =
								    EplDllkCheckFrame
								    (pTxFrame,
								     uiFrameSize);

								// set buffer valid
								pTxBuffer->
								    m_uiTxMsgLen
								    =
								    uiFrameSize;
							} else if (Ret == kEplDllAsyncTxBufferEmpty) {	// empty Tx buffer is not a real problem
								// so just ignore it
								Ret =
								    kEplSuccessful;
								// mark Tx buffer as empty
								pTxBuffer->
								    m_uiTxMsgLen
								    =
								    EPL_DLLK_BUFLEN_EMPTY;
							}
						}
					}
					break;
				}
			}

			NmtState = EplNmtkGetNmtState();

			if ((NmtState == kEplNmtCsBasicEthernet) || (NmtState == kEplNmtMsBasicEthernet)) {	// send frame immediately
				if (pTxFrame != NULL) {	// frame is present
					// padding is done by Edrv or ethernet controller
					Ret = EdrvSendTxMsg(pTxBuffer);
				} else {	// no frame moved to TxBuffer
					// check if TxBuffers contain unsent frames
					if (EplDllkInstance_g.m_pTxBuffer[EPL_DLLK_TXFRAME_NMTREQ].m_uiTxMsgLen > EPL_DLLK_BUFLEN_EMPTY) {	// NMT request Tx buffer contains a frame
						Ret =
						    EdrvSendTxMsg
						    (&EplDllkInstance_g.
						     m_pTxBuffer
						     [EPL_DLLK_TXFRAME_NMTREQ]);
					} else if (EplDllkInstance_g.m_pTxBuffer[EPL_DLLK_TXFRAME_NONEPL].m_uiTxMsgLen > EPL_DLLK_BUFLEN_EMPTY) {	// non-EPL Tx buffer contains a frame
						Ret =
						    EdrvSendTxMsg
						    (&EplDllkInstance_g.
						     m_pTxBuffer
						     [EPL_DLLK_TXFRAME_NONEPL]);
					}
					if (Ret == kEplInvalidOperation) {	// ignore error if caused by already active transmission
						Ret = kEplSuccessful;
					}
				}
				// reset PRes flag 2
				EplDllkInstance_g.m_bFlag2 = 0;
			} else {
				// update Flag 2 (PR, RS)
				Ret =
				    EplDllkCalAsyncGetTxCount(&AsyncReqPriority,
							      &uiFrameCount);
				if (AsyncReqPriority == kEplDllAsyncReqPrioNmt) {	// non-empty FIFO with hightest priority is for NMT requests
					if (EplDllkInstance_g.m_pTxBuffer[EPL_DLLK_TXFRAME_NMTREQ].m_uiTxMsgLen > EPL_DLLK_BUFLEN_EMPTY) {	// NMT request Tx buffer contains a frame
						// add one more frame
						uiFrameCount++;
					}
				} else {	// non-empty FIFO with highest priority is for generic frames
					if (EplDllkInstance_g.m_pTxBuffer[EPL_DLLK_TXFRAME_NMTREQ].m_uiTxMsgLen > EPL_DLLK_BUFLEN_EMPTY) {	// NMT request Tx buffer contains a frame
						// use NMT request FIFO, because of higher priority
						uiFrameCount = 1;
						AsyncReqPriority =
						    kEplDllAsyncReqPrioNmt;
					} else if (EplDllkInstance_g.m_pTxBuffer[EPL_DLLK_TXFRAME_NONEPL].m_uiTxMsgLen > EPL_DLLK_BUFLEN_EMPTY) {	// non-EPL Tx buffer contains a frame
						// use NMT request FIFO, because of higher priority
						// add one more frame
						uiFrameCount++;
					}
				}

				if (uiFrameCount > 7) {	// limit frame request to send counter to 7
					uiFrameCount = 7;
				}
				if (uiFrameCount > 0) {
					EplDllkInstance_g.m_bFlag2 =
					    (u8) (((AsyncReqPriority <<
						      EPL_FRAME_FLAG2_PR_SHIFT)
						     & EPL_FRAME_FLAG2_PR)
						    | (uiFrameCount &
						       EPL_FRAME_FLAG2_RS));
				} else {
					EplDllkInstance_g.m_bFlag2 = 0;
				}
			}

			break;
		}

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
	case kEplEventTypeDllkStartReducedCycle:
		{
			// start the reduced cycle by programming the cycle timer
			// it is issued by NMT MN module, when PreOp1 is entered

			// clear the asynchronous queues
			Ret = EplDllkCalAsyncClearQueues();

			// reset cycle counter (everytime a SoA is triggerd in PreOp1 the counter is incremented
			// and when it reaches EPL_C_DLL_PREOP1_START_CYCLES the SoA may contain invitations)
			EplDllkInstance_g.m_uiCycleCount = 0;

			// remove any CN from isochronous phase
			while (EplDllkInstance_g.m_pFirstNodeInfo != NULL) {
				EplDllkDeleteNode(EplDllkInstance_g.
						  m_pFirstNodeInfo->m_uiNodeId);
			}

			// change state to NonCyclic,
			// hence EplDllkChangeState() will not ignore the next call
			EplDllkInstance_g.m_DllState = kEplDllMsNonCyclic;

#if EPL_TIMER_USE_HIGHRES != FALSE
			if (EplDllkInstance_g.m_DllConfigParam.
			    m_dwAsyncSlotTimeout != 0) {
				Ret =
				    EplTimerHighReskModifyTimerNs
				    (&EplDllkInstance_g.m_TimerHdlCycle,
				     EplDllkInstance_g.m_DllConfigParam.
				     m_dwAsyncSlotTimeout,
				     EplDllkCbMnTimerCycle, 0L, FALSE);
			}
#endif

			break;
		}
#endif

#if EPL_DLL_PRES_READY_AFTER_SOA != FALSE
	case kEplEventTypeDllkPresReady:
		{
			// post PRes to transmit FIFO

			NmtState = EplNmtkGetNmtState();

			if (NmtState != kEplNmtCsBasicEthernet) {
				// Does PRes exist?
				if (EplDllkInstance_g.m_pTxBuffer[EPL_DLLK_TXFRAME_PRES].m_pbBuffer != NULL) {	// PRes does exist
					pTxFrame =
					    (tEplFrame *) EplDllkInstance_g.
					    m_pTxBuffer[EPL_DLLK_TXFRAME_PRES].
					    m_pbBuffer;
					// update frame (NMT state, RD, RS, PR, MS, EN flags)
					if (NmtState < kEplNmtCsPreOperational2) {	// NMT state is not PreOp2, ReadyToOp or Op
						// fake NMT state PreOp2, because PRes will be sent only in PreOp2 or greater
						NmtState =
						    kEplNmtCsPreOperational2;
					}
					AmiSetByteToLe(&pTxFrame->m_Data.m_Pres.
						       m_le_bNmtStatus,
						       (u8) NmtState);
					AmiSetByteToLe(&pTxFrame->m_Data.m_Pres.
						       m_le_bFlag2,
						       EplDllkInstance_g.
						       m_bFlag2);
					if (NmtState != kEplNmtCsOperational) {	// mark PDO as invalid in NMT state Op
						// $$$ reset only RD flag; set other flags appropriately
						AmiSetByteToLe(&pTxFrame->
							       m_Data.m_Pres.
							       m_le_bFlag1, 0);
					}
					// $$$ make function that updates Pres, StatusRes
					// mark PRes frame as ready for transmission
					Ret =
					    EdrvTxMsgReady(&EplDllkInstance_g.
							   m_pTxBuffer
							   [EPL_DLLK_TXFRAME_PRES]);
				}
			}

			break;
		}
#endif
	default:
		{
			ASSERTMSG(FALSE,
				  "EplDllkProcess(): unhandled event type!\n");
		}
	}

      Exit:
	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplDllkConfig
//
// Description: configure parameters of DLL
//
// Parameters:  pDllConfigParam_p       = configuration parameters
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplDllkConfig(tEplDllConfigParam * pDllConfigParam_p)
{
	tEplKernel Ret = kEplSuccessful;

// d.k. check of NMT state disabled, because CycleLen is programmed at run time by MN without reset of CN
/*tEplNmtState    NmtState;

    NmtState = EplNmtkGetNmtState();

    if (NmtState > kEplNmtGsResetConfiguration)
    {   // only allowed in state DLL_GS_INIT
        Ret = kEplInvalidOperation;
        goto Exit;
    }
*/
	EPL_MEMCPY(&EplDllkInstance_g.m_DllConfigParam, pDllConfigParam_p,
		   (pDllConfigParam_p->m_uiSizeOfStruct <
		    sizeof(tEplDllConfigParam) ? pDllConfigParam_p->
		    m_uiSizeOfStruct : sizeof(tEplDllConfigParam)));

	if ((EplDllkInstance_g.m_DllConfigParam.m_dwCycleLen != 0)
	    && (EplDllkInstance_g.m_DllConfigParam.m_dwLossOfFrameTolerance != 0)) {	// monitor EPL cycle, calculate frame timeout
		EplDllkInstance_g.m_ullFrameTimeout = (1000LL
						       *
						       ((unsigned long long)
							EplDllkInstance_g.
							m_DllConfigParam.
							m_dwCycleLen))
		    +
		    ((unsigned long long)EplDllkInstance_g.m_DllConfigParam.
		     m_dwLossOfFrameTolerance);
	} else {
		EplDllkInstance_g.m_ullFrameTimeout = 0LL;
	}

	if (EplDllkInstance_g.m_DllConfigParam.m_fAsyncOnly != FALSE) {	// it is configured as async-only CN
		// disable multiplexed cycle, that m_uiCycleCount will not be incremented spuriously on SoC
		EplDllkInstance_g.m_DllConfigParam.m_uiMultiplCycleCnt = 0;
	}
//Exit:
	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplDllkSetIdentity
//
// Description: configure identity of local node for IdentResponse
//
// Parameters:  pDllIdentParam_p        = identity
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplDllkSetIdentity(tEplDllIdentParam * pDllIdentParam_p)
{
	tEplKernel Ret = kEplSuccessful;

	EPL_MEMCPY(&EplDllkInstance_g.m_DllIdentParam, pDllIdentParam_p,
		   (pDllIdentParam_p->m_uiSizeOfStruct <
		    sizeof(tEplDllIdentParam) ? pDllIdentParam_p->
		    m_uiSizeOfStruct : sizeof(tEplDllIdentParam)));

	// $$$ if IdentResponse frame exists update it

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplDllkRegAsyncHandler
//
// Description: registers handler for non-EPL frames
//
// Parameters:  pfnDllkCbAsync_p        = pointer to callback function
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplDllkRegAsyncHandler(tEplDllkCbAsync pfnDllkCbAsync_p)
{
	tEplKernel Ret = kEplSuccessful;

	if (EplDllkInstance_g.m_pfnCbAsync == NULL) {	// no handler registered yet
		EplDllkInstance_g.m_pfnCbAsync = pfnDllkCbAsync_p;
	} else {		// handler already registered
		Ret = kEplDllCbAsyncRegistered;
	}

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplDllkDeregAsyncHandler
//
// Description: deregisters handler for non-EPL frames
//
// Parameters:  pfnDllkCbAsync_p        = pointer to callback function
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplDllkDeregAsyncHandler(tEplDllkCbAsync pfnDllkCbAsync_p)
{
	tEplKernel Ret = kEplSuccessful;

	if (EplDllkInstance_g.m_pfnCbAsync == pfnDllkCbAsync_p) {	// same handler is registered
		// deregister it
		EplDllkInstance_g.m_pfnCbAsync = NULL;
	} else {		// wrong handler or no handler registered
		Ret = kEplDllCbAsyncRegistered;
	}

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplDllkSetAsndServiceIdFilter()
//
// Description: sets the specified node ID filter for the specified
//              AsndServiceId. It registers C_DLL_MULTICAST_ASND in ethernet
//              driver if any AsndServiceId is open.
//
// Parameters:  ServiceId_p             = ASnd Service ID
//              Filter_p                = node ID filter
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplDllkSetAsndServiceIdFilter(tEplDllAsndServiceId ServiceId_p,
					 tEplDllAsndFilter Filter_p)
{
	tEplKernel Ret = kEplSuccessful;

	if (ServiceId_p < tabentries(EplDllkInstance_g.m_aAsndFilter)) {
		EplDllkInstance_g.m_aAsndFilter[ServiceId_p] = Filter_p;
	}

	return Ret;
}

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)

//---------------------------------------------------------------------------
//
// Function:    EplDllkSetFlag1OfNode()
//
// Description: sets Flag1 (for PReq and SoA) of the specified node ID.
//
// Parameters:  uiNodeId_p              = node ID
//              bSoaFlag1_p             = flag1
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplDllkSetFlag1OfNode(unsigned int uiNodeId_p, u8 bSoaFlag1_p)
{
	tEplKernel Ret = kEplSuccessful;
	tEplDllkNodeInfo *pNodeInfo;

	pNodeInfo = EplDllkGetNodeInfo(uiNodeId_p);
	if (pNodeInfo == NULL) {	// no node info structure available
		Ret = kEplDllNoNodeInfo;
		goto Exit;
	}
	// store flag1 in internal node info structure
	pNodeInfo->m_bSoaFlag1 = bSoaFlag1_p;

      Exit:
	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplDllkGetFirstNodeInfo()
//
// Description: returns first info structure of first node in isochronous phase.
//              It is only useful for ErrorHandlerk module.
//
// Parameters:  ppNodeInfo_p            = pointer to pointer of internal node info structure
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplDllkGetFirstNodeInfo(tEplDllkNodeInfo ** ppNodeInfo_p)
{
	tEplKernel Ret = kEplSuccessful;

	*ppNodeInfo_p = EplDllkInstance_g.m_pFirstNodeInfo;

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplDllkAddNode()
//
// Description: adds the specified node to the isochronous phase.
//
// Parameters:  pNodeInfo_p             = pointer of node info structure
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplDllkAddNode(tEplDllNodeInfo * pNodeInfo_p)
{
	tEplKernel Ret = kEplSuccessful;
	tEplDllkNodeInfo *pIntNodeInfo;
	tEplDllkNodeInfo **ppIntNodeInfo;
	unsigned int uiHandle;
	tEplFrame *pFrame;
	unsigned int uiFrameSize;

	pIntNodeInfo = EplDllkGetNodeInfo(pNodeInfo_p->m_uiNodeId);
	if (pIntNodeInfo == NULL) {	// no node info structure available
		Ret = kEplDllNoNodeInfo;
		goto Exit;
	}

	EPL_DLLK_DBG_POST_TRACE_VALUE(kEplEventTypeDllkAddNode,
				      pNodeInfo_p->m_uiNodeId, 0);

	// copy node configuration
	pIntNodeInfo->m_dwPresTimeout = pNodeInfo_p->m_dwPresTimeout;
	pIntNodeInfo->m_wPresPayloadLimit = pNodeInfo_p->m_wPresPayloadLimit;

	// $$$ d.k.: actually add node only if MN. On CN it is sufficient to update the node configuration
	if (pNodeInfo_p->m_uiNodeId == EplDllkInstance_g.m_DllConfigParam.m_uiNodeId) {	// we shall send PRes ourself
		// insert our node at the end of the list
		ppIntNodeInfo = &EplDllkInstance_g.m_pFirstNodeInfo;
		while ((*ppIntNodeInfo != NULL)
		       && ((*ppIntNodeInfo)->m_pNextNodeInfo != NULL)) {
			ppIntNodeInfo = &(*ppIntNodeInfo)->m_pNextNodeInfo;
		}
		if (*ppIntNodeInfo != NULL) {
			if ((*ppIntNodeInfo)->m_uiNodeId == pNodeInfo_p->m_uiNodeId) {	// node was already added to list
				// $$$ d.k. maybe this should be an error
				goto Exit;
			} else {	// add our node at the end of the list
				ppIntNodeInfo =
				    &(*ppIntNodeInfo)->m_pNextNodeInfo;
			}
		}
		// set "PReq"-TxBuffer to PRes-TxBuffer
		pIntNodeInfo->m_pPreqTxBuffer =
		    &EplDllkInstance_g.m_pTxBuffer[EPL_DLLK_TXFRAME_PRES];
	} else {		// normal CN shall be added to isochronous phase
		// insert node into list in ascending order
		ppIntNodeInfo = &EplDllkInstance_g.m_pFirstNodeInfo;
		while ((*ppIntNodeInfo != NULL)
		       && ((*ppIntNodeInfo)->m_uiNodeId <
			   pNodeInfo_p->m_uiNodeId)
		       && ((*ppIntNodeInfo)->m_uiNodeId !=
			   EplDllkInstance_g.m_DllConfigParam.m_uiNodeId)) {
			ppIntNodeInfo = &(*ppIntNodeInfo)->m_pNextNodeInfo;
		}
		if ((*ppIntNodeInfo != NULL) && ((*ppIntNodeInfo)->m_uiNodeId == pNodeInfo_p->m_uiNodeId)) {	// node was already added to list
			// $$$ d.k. maybe this should be an error
			goto Exit;
		}
	}

	// initialize elements of internal node info structure
	pIntNodeInfo->m_bSoaFlag1 = 0;
	pIntNodeInfo->m_fSoftDelete = FALSE;
	pIntNodeInfo->m_NmtState = kEplNmtCsNotActive;
	if (pIntNodeInfo->m_pPreqTxBuffer == NULL) {	// create TxBuffer entry
		uiFrameSize = pNodeInfo_p->m_wPreqPayloadLimit + 24;
		Ret =
		    EplDllkCreateTxFrame(&uiHandle, &pFrame, &uiFrameSize,
					 kEplMsgTypePreq,
					 kEplDllAsndNotDefined);
		if (Ret != kEplSuccessful) {
			goto Exit;
		}
		pIntNodeInfo->m_pPreqTxBuffer =
		    &EplDllkInstance_g.m_pTxBuffer[uiHandle];
		AmiSetByteToLe(&pFrame->m_le_bDstNodeId,
			       (u8) pNodeInfo_p->m_uiNodeId);

		// set up destination MAC address
		EPL_MEMCPY(pFrame->m_be_abDstMac, pIntNodeInfo->m_be_abMacAddr,
			   6);

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_PDOK)) != 0)
		{
			tEplFrameInfo FrameInfo;

			// initially encode TPDO -> inform PDO module
			FrameInfo.m_pFrame = pFrame;
			FrameInfo.m_uiFrameSize = uiFrameSize;
			Ret = EplPdokCbPdoTransmitted(&FrameInfo);
		}
#endif
	}
	pIntNodeInfo->m_ulDllErrorEvents = 0L;
	// add node to list
	pIntNodeInfo->m_pNextNodeInfo = *ppIntNodeInfo;
	*ppIntNodeInfo = pIntNodeInfo;

      Exit:
	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplDllkDeleteNode()
//
// Description: removes the specified node from the isochronous phase.
//
// Parameters:  uiNodeId_p              = node ID
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplDllkDeleteNode(unsigned int uiNodeId_p)
{
	tEplKernel Ret = kEplSuccessful;
	tEplDllkNodeInfo *pIntNodeInfo;
	tEplDllkNodeInfo **ppIntNodeInfo;
	unsigned int uiHandle;

	pIntNodeInfo = EplDllkGetNodeInfo(uiNodeId_p);
	if (pIntNodeInfo == NULL) {	// no node info structure available
		Ret = kEplDllNoNodeInfo;
		goto Exit;
	}

	EPL_DLLK_DBG_POST_TRACE_VALUE(kEplEventTypeDllkDelNode, uiNodeId_p, 0);

	// search node in whole list
	ppIntNodeInfo = &EplDllkInstance_g.m_pFirstNodeInfo;
	while ((*ppIntNodeInfo != NULL)
	       && ((*ppIntNodeInfo)->m_uiNodeId != uiNodeId_p)) {
		ppIntNodeInfo = &(*ppIntNodeInfo)->m_pNextNodeInfo;
	}
	if ((*ppIntNodeInfo == NULL) || ((*ppIntNodeInfo)->m_uiNodeId != uiNodeId_p)) {	// node was not found in list
		// $$$ d.k. maybe this should be an error
		goto Exit;
	}
	// remove node from list
	*ppIntNodeInfo = pIntNodeInfo->m_pNextNodeInfo;

	if ((pIntNodeInfo->m_pPreqTxBuffer != NULL)
	    && (pIntNodeInfo->m_pPreqTxBuffer != &EplDllkInstance_g.m_pTxBuffer[EPL_DLLK_TXFRAME_PRES])) {	// delete TxBuffer entry
		uiHandle =
		    pIntNodeInfo->m_pPreqTxBuffer -
		    EplDllkInstance_g.m_pTxBuffer;
		pIntNodeInfo->m_pPreqTxBuffer = NULL;
		Ret = EplDllkDeleteTxFrame(uiHandle);
/*        if (Ret != kEplSuccessful)
        {
            goto Exit;
        }*/
	}

      Exit:
	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplDllkSoftDeleteNode()
//
// Description: removes the specified node not immediately from the isochronous phase.
//              Instead the will be removed after error (late/loss PRes) without
//              charging the error.
//
// Parameters:  uiNodeId_p              = node ID
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplDllkSoftDeleteNode(unsigned int uiNodeId_p)
{
	tEplKernel Ret = kEplSuccessful;
	tEplDllkNodeInfo *pIntNodeInfo;

	pIntNodeInfo = EplDllkGetNodeInfo(uiNodeId_p);
	if (pIntNodeInfo == NULL) {	// no node info structure available
		Ret = kEplDllNoNodeInfo;
		goto Exit;
	}

	EPL_DLLK_DBG_POST_TRACE_VALUE(kEplEventTypeDllkSoftDelNode,
				      uiNodeId_p, 0);

	pIntNodeInfo->m_fSoftDelete = TRUE;

      Exit:
	return Ret;
}

#endif //(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)

//=========================================================================//
//                                                                         //
//          P R I V A T E   F U N C T I O N S                              //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//
// Function:    EplDllkChangeState
//
// Description: change DLL state on event and diagnose some communication errors
//
// Parameters:  NmtEvent_p              = DLL event (wrapped in NMT event)
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

static tEplKernel EplDllkChangeState(tEplNmtEvent NmtEvent_p,
				     tEplNmtState NmtState_p)
{
	tEplKernel Ret = kEplSuccessful;
	tEplEvent Event;
	tEplErrorHandlerkEvent DllEvent;

	DllEvent.m_ulDllErrorEvents = 0;
	DllEvent.m_uiNodeId = 0;
	DllEvent.m_NmtState = NmtState_p;

	switch (NmtState_p) {
	case kEplNmtGsOff:
	case kEplNmtGsInitialising:
	case kEplNmtGsResetApplication:
	case kEplNmtGsResetCommunication:
	case kEplNmtGsResetConfiguration:
	case kEplNmtCsBasicEthernet:
		// enter DLL_GS_INIT
		EplDllkInstance_g.m_DllState = kEplDllGsInit;
		break;

	case kEplNmtCsNotActive:
	case kEplNmtCsPreOperational1:
		// reduced EPL cycle is active
		if (NmtEvent_p == kEplNmtEventDllCeSoc) {	// SoC received
			// enter DLL_CS_WAIT_PREQ
			EplDllkInstance_g.m_DllState = kEplDllCsWaitPreq;
		} else {
			// enter DLL_GS_INIT
			EplDllkInstance_g.m_DllState = kEplDllGsInit;
		}
		break;

	case kEplNmtCsPreOperational2:
	case kEplNmtCsReadyToOperate:
	case kEplNmtCsOperational:
		// full EPL cycle is active

		switch (EplDllkInstance_g.m_DllState) {
		case kEplDllCsWaitPreq:
			switch (NmtEvent_p) {
				// DLL_CT2
			case kEplNmtEventDllCePreq:
				// enter DLL_CS_WAIT_SOA
				DllEvent.m_ulDllErrorEvents |=
				    EPL_DLL_ERR_CN_RECVD_PREQ;
				EplDllkInstance_g.m_DllState = kEplDllCsWaitSoa;
				break;

				// DLL_CT8
			case kEplNmtEventDllCeFrameTimeout:
				if (NmtState_p == kEplNmtCsPreOperational2) {	// ignore frame timeout in PreOp2,
					// because the previously configured cycle len
					// may be wrong.
					// 2008/10/15 d.k. If it would not be ignored,
					// we would go cyclically to PreOp1 and on next
					// SoC back to PreOp2.
					break;
				}
				// report DLL_CEV_LOSS_SOC and DLL_CEV_LOSS_SOA
				DllEvent.m_ulDllErrorEvents |=
				    EPL_DLL_ERR_CN_LOSS_SOA |
				    EPL_DLL_ERR_CN_LOSS_SOC;

				// enter DLL_CS_WAIT_SOC
				EplDllkInstance_g.m_DllState = kEplDllCsWaitSoc;
				break;

			case kEplNmtEventDllCeSoa:
				// check if multiplexed and PReq should have been received in this cycle
				// and if >= NMT_CS_READY_TO_OPERATE
				if ((EplDllkInstance_g.m_uiCycleCount == 0)
				    && (NmtState_p >= kEplNmtCsReadyToOperate)) {	// report DLL_CEV_LOSS_OF_PREQ
					DllEvent.m_ulDllErrorEvents |=
					    EPL_DLL_ERR_CN_LOSS_PREQ;
				}
				// enter DLL_CS_WAIT_SOC
				EplDllkInstance_g.m_DllState = kEplDllCsWaitSoc;
				break;

				// DLL_CT7
			case kEplNmtEventDllCeSoc:
			case kEplNmtEventDllCeAsnd:
				// report DLL_CEV_LOSS_SOA
				DllEvent.m_ulDllErrorEvents |=
				    EPL_DLL_ERR_CN_LOSS_SOA;

			case kEplNmtEventDllCePres:
			default:
				// remain in this state
				break;
			}
			break;

		case kEplDllCsWaitSoc:
			switch (NmtEvent_p) {
				// DLL_CT1
			case kEplNmtEventDllCeSoc:
				// start of cycle and isochronous phase
				// enter DLL_CS_WAIT_PREQ
				EplDllkInstance_g.m_DllState =
				    kEplDllCsWaitPreq;
				break;

				// DLL_CT4
//                        case kEplNmtEventDllCePres:
			case kEplNmtEventDllCeFrameTimeout:
				if (NmtState_p == kEplNmtCsPreOperational2) {	// ignore frame timeout in PreOp2,
					// because the previously configured cycle len
					// may be wrong.
					// 2008/10/15 d.k. If it would not be ignored,
					// we would go cyclically to PreOp1 and on next
					// SoC back to PreOp2.
					break;
				}
				// fall through

			case kEplNmtEventDllCePreq:
			case kEplNmtEventDllCeSoa:
				// report DLL_CEV_LOSS_SOC
				DllEvent.m_ulDllErrorEvents |=
				    EPL_DLL_ERR_CN_LOSS_SOC;

			case kEplNmtEventDllCeAsnd:
			default:
				// remain in this state
				break;
			}
			break;

		case kEplDllCsWaitSoa:
			switch (NmtEvent_p) {
			case kEplNmtEventDllCeFrameTimeout:
				// DLL_CT3
				if (NmtState_p == kEplNmtCsPreOperational2) {	// ignore frame timeout in PreOp2,
					// because the previously configured cycle len
					// may be wrong.
					// 2008/10/15 d.k. If it would not be ignored,
					// we would go cyclically to PreOp1 and on next
					// SoC back to PreOp2.
					break;
				}
				// fall through

			case kEplNmtEventDllCePreq:
				// report DLL_CEV_LOSS_SOC and DLL_CEV_LOSS_SOA
				DllEvent.m_ulDllErrorEvents |=
				    EPL_DLL_ERR_CN_LOSS_SOA |
				    EPL_DLL_ERR_CN_LOSS_SOC;

			case kEplNmtEventDllCeSoa:
				// enter DLL_CS_WAIT_SOC
				EplDllkInstance_g.m_DllState = kEplDllCsWaitSoc;
				break;

				// DLL_CT9
			case kEplNmtEventDllCeSoc:
				// report DLL_CEV_LOSS_SOA
				DllEvent.m_ulDllErrorEvents |=
				    EPL_DLL_ERR_CN_LOSS_SOA;

				// enter DLL_CS_WAIT_PREQ
				EplDllkInstance_g.m_DllState =
				    kEplDllCsWaitPreq;
				break;

				// DLL_CT10
			case kEplNmtEventDllCeAsnd:
				// report DLL_CEV_LOSS_SOA
				DllEvent.m_ulDllErrorEvents |=
				    EPL_DLL_ERR_CN_LOSS_SOA;

			case kEplNmtEventDllCePres:
			default:
				// remain in this state
				break;
			}
			break;

		case kEplDllGsInit:
			// enter DLL_CS_WAIT_PREQ
			EplDllkInstance_g.m_DllState = kEplDllCsWaitPreq;
			break;

		default:
			break;
		}
		break;

	case kEplNmtCsStopped:
		// full EPL cycle is active, but without PReq/PRes

		switch (EplDllkInstance_g.m_DllState) {
		case kEplDllCsWaitPreq:
			switch (NmtEvent_p) {
				// DLL_CT2
			case kEplNmtEventDllCePreq:
				// enter DLL_CS_WAIT_SOA
				EplDllkInstance_g.m_DllState = kEplDllCsWaitSoa;
				break;

				// DLL_CT8
			case kEplNmtEventDllCeFrameTimeout:
				// report DLL_CEV_LOSS_SOC and DLL_CEV_LOSS_SOA
				DllEvent.m_ulDllErrorEvents |=
				    EPL_DLL_ERR_CN_LOSS_SOA |
				    EPL_DLL_ERR_CN_LOSS_SOC;

			case kEplNmtEventDllCeSoa:
				// NMT_CS_STOPPED active
				// it is Ok if no PReq was received

				// enter DLL_CS_WAIT_SOC
				EplDllkInstance_g.m_DllState = kEplDllCsWaitSoc;
				break;

				// DLL_CT7
			case kEplNmtEventDllCeSoc:
			case kEplNmtEventDllCeAsnd:
				// report DLL_CEV_LOSS_SOA
				DllEvent.m_ulDllErrorEvents |=
				    EPL_DLL_ERR_CN_LOSS_SOA;

			case kEplNmtEventDllCePres:
			default:
				// remain in this state
				break;
			}
			break;

		case kEplDllCsWaitSoc:
			switch (NmtEvent_p) {
				// DLL_CT1
			case kEplNmtEventDllCeSoc:
				// start of cycle and isochronous phase
				// enter DLL_CS_WAIT_SOA
				EplDllkInstance_g.m_DllState = kEplDllCsWaitSoa;
				break;

				// DLL_CT4
//                        case kEplNmtEventDllCePres:
			case kEplNmtEventDllCePreq:
			case kEplNmtEventDllCeSoa:
			case kEplNmtEventDllCeFrameTimeout:
				// report DLL_CEV_LOSS_SOC
				DllEvent.m_ulDllErrorEvents |=
				    EPL_DLL_ERR_CN_LOSS_SOC;

			case kEplNmtEventDllCeAsnd:
			default:
				// remain in this state
				break;
			}
			break;

		case kEplDllCsWaitSoa:
			switch (NmtEvent_p) {
				// DLL_CT3
			case kEplNmtEventDllCeFrameTimeout:
				// report DLL_CEV_LOSS_SOC and DLL_CEV_LOSS_SOA
				DllEvent.m_ulDllErrorEvents |=
				    EPL_DLL_ERR_CN_LOSS_SOA |
				    EPL_DLL_ERR_CN_LOSS_SOC;

			case kEplNmtEventDllCeSoa:
				// enter DLL_CS_WAIT_SOC
				EplDllkInstance_g.m_DllState = kEplDllCsWaitSoc;
				break;

				// DLL_CT9
			case kEplNmtEventDllCeSoc:
				// report DLL_CEV_LOSS_SOA
				DllEvent.m_ulDllErrorEvents |=
				    EPL_DLL_ERR_CN_LOSS_SOA;
				// remain in DLL_CS_WAIT_SOA
				break;

				// DLL_CT10
			case kEplNmtEventDllCeAsnd:
				// report DLL_CEV_LOSS_SOA
				DllEvent.m_ulDllErrorEvents |=
				    EPL_DLL_ERR_CN_LOSS_SOA;

			case kEplNmtEventDllCePreq:
				// NMT_CS_STOPPED active and we do not expect any PReq
				// so just ignore it
			case kEplNmtEventDllCePres:
			default:
				// remain in this state
				break;
			}
			break;

		case kEplDllGsInit:
		default:
			// enter DLL_CS_WAIT_PREQ
			EplDllkInstance_g.m_DllState = kEplDllCsWaitSoa;
			break;
		}
		break;

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
	case kEplNmtMsNotActive:
	case kEplNmtMsBasicEthernet:
		break;

	case kEplNmtMsPreOperational1:
		// reduced EPL cycle is active
		if (EplDllkInstance_g.m_DllState != kEplDllMsNonCyclic) {	// stop cycle timer
#if EPL_TIMER_USE_HIGHRES != FALSE
			Ret =
			    EplTimerHighReskDeleteTimer(&EplDllkInstance_g.
							m_TimerHdlCycle);
#endif
			EplDllkInstance_g.m_DllState = kEplDllMsNonCyclic;

			// stop further processing,
			// because it will be restarted by NMT MN module
			break;
		}

		switch (NmtEvent_p) {
		case kEplNmtEventDllMeSocTrig:
		case kEplNmtEventDllCeAsnd:
			{	// because of reduced EPL cycle SoA shall be triggered, not SoC
				tEplDllState DummyDllState;

				Ret =
				    EplDllkAsyncFrameNotReceived
				    (EplDllkInstance_g.m_LastReqServiceId,
				     EplDllkInstance_g.m_uiLastTargetNodeId);

				// go ahead and send SoA
				Ret = EplDllkMnSendSoa(NmtState_p,
						       &DummyDllState,
						       (EplDllkInstance_g.
							m_uiCycleCount >=
							EPL_C_DLL_PREOP1_START_CYCLES));
				// increment cycle counter to detect if EPL_C_DLL_PREOP1_START_CYCLES empty cycles are elapsed
				EplDllkInstance_g.m_uiCycleCount++;

				// reprogram timer
#if EPL_TIMER_USE_HIGHRES != FALSE
				if (EplDllkInstance_g.m_DllConfigParam.
				    m_dwAsyncSlotTimeout != 0) {
					Ret =
					    EplTimerHighReskModifyTimerNs
					    (&EplDllkInstance_g.m_TimerHdlCycle,
					     EplDllkInstance_g.m_DllConfigParam.
					     m_dwAsyncSlotTimeout,
					     EplDllkCbMnTimerCycle, 0L, FALSE);
				}
#endif
				break;
			}

		default:
			break;
		}
		break;

	case kEplNmtMsPreOperational2:
	case kEplNmtMsReadyToOperate:
	case kEplNmtMsOperational:
		// full EPL cycle is active
		switch (NmtEvent_p) {
		case kEplNmtEventDllMeSocTrig:
			{
				// update cycle counter
				if (EplDllkInstance_g.m_DllConfigParam.m_uiMultiplCycleCnt > 0) {	// multiplexed cycle active
					EplDllkInstance_g.m_uiCycleCount =
					    (EplDllkInstance_g.m_uiCycleCount +
					     1) %
					    EplDllkInstance_g.m_DllConfigParam.
					    m_uiMultiplCycleCnt;
					// $$$ check multiplexed cycle restart
					//     -> toggle MC flag
					//     -> change node linked list
				} else {	// non-multiplexed cycle active
					// start with first node in isochronous phase
					EplDllkInstance_g.m_pCurNodeInfo = NULL;
				}

				switch (EplDllkInstance_g.m_DllState) {
				case kEplDllMsNonCyclic:
					{	// start continuous cycle timer
#if EPL_TIMER_USE_HIGHRES != FALSE
						Ret =
						    EplTimerHighReskModifyTimerNs
						    (&EplDllkInstance_g.
						     m_TimerHdlCycle,
						     EplDllkInstance_g.
						     m_ullFrameTimeout,
						     EplDllkCbMnTimerCycle, 0L,
						     TRUE);
#endif
						// continue with sending SoC
					}

				case kEplDllMsWaitAsnd:
				case kEplDllMsWaitSocTrig:
					{	// if m_LastReqServiceId is still valid,
						// SoA was not correctly answered
						// and user part has to be informed
						Ret =
						    EplDllkAsyncFrameNotReceived
						    (EplDllkInstance_g.
						     m_LastReqServiceId,
						     EplDllkInstance_g.
						     m_uiLastTargetNodeId);

						// send SoC
						Ret = EplDllkMnSendSoc();

						// new DLL state
						EplDllkInstance_g.m_DllState =
						    kEplDllMsWaitPreqTrig;

						// start WaitSoCPReq Timer
#if EPL_TIMER_USE_HIGHRES != FALSE
						Ret =
						    EplTimerHighReskModifyTimerNs
						    (&EplDllkInstance_g.
						     m_TimerHdlResponse,
						     EplDllkInstance_g.
						     m_DllConfigParam.
						     m_dwWaitSocPreq,
						     EplDllkCbMnTimerResponse,
						     0L, FALSE);
#endif
						break;
					}

				default:
					{	// wrong DLL state / cycle time exceeded
						DllEvent.m_ulDllErrorEvents |=
						    EPL_DLL_ERR_MN_CYCTIMEEXCEED;
						EplDllkInstance_g.m_DllState =
						    kEplDllMsWaitSocTrig;
						break;
					}
				}

				break;
			}

		case kEplNmtEventDllMePresTimeout:
			{

				switch (EplDllkInstance_g.m_DllState) {
				case kEplDllMsWaitPres:
					{	// PRes not received

						if (EplDllkInstance_g.m_pCurNodeInfo->m_fSoftDelete == FALSE) {	// normal isochronous CN
							DllEvent.
							    m_ulDllErrorEvents
							    |=
							    EPL_DLL_ERR_MN_CN_LOSS_PRES;
							DllEvent.m_uiNodeId =
							    EplDllkInstance_g.
							    m_pCurNodeInfo->
							    m_uiNodeId;
						} else {	// CN shall be deleted softly
							Event.m_EventSink =
							    kEplEventSinkDllkCal;
							Event.m_EventType =
							    kEplEventTypeDllkSoftDelNode;
							// $$$ d.k. set Event.m_NetTime to current time
							Event.m_uiSize =
							    sizeof(unsigned
								   int);
							Event.m_pArg =
							    &EplDllkInstance_g.
							    m_pCurNodeInfo->
							    m_uiNodeId;
							Ret =
							    EplEventkPost
							    (&Event);
						}

						// continue with sending next PReq
					}

				case kEplDllMsWaitPreqTrig:
					{
						// send next PReq
						Ret =
						    EplDllkMnSendPreq
						    (NmtState_p,
						     &EplDllkInstance_g.
						     m_DllState);

						break;
					}

				default:
					{	// wrong DLL state
						break;
					}
				}

				break;
			}

		case kEplNmtEventDllCePres:
			{

				switch (EplDllkInstance_g.m_DllState) {
				case kEplDllMsWaitPres:
					{	// PRes received
						// send next PReq
						Ret =
						    EplDllkMnSendPreq
						    (NmtState_p,
						     &EplDllkInstance_g.
						     m_DllState);

						break;
					}

				default:
					{	// wrong DLL state
						break;
					}
				}

				break;
			}

		case kEplNmtEventDllMeSoaTrig:
			{

				switch (EplDllkInstance_g.m_DllState) {
				case kEplDllMsWaitSoaTrig:
					{	// MN PRes sent
						// send SoA
						Ret =
						    EplDllkMnSendSoa(NmtState_p,
								     &EplDllkInstance_g.
								     m_DllState,
								     TRUE);

						break;
					}

				default:
					{	// wrong DLL state
						break;
					}
				}

				break;
			}

		case kEplNmtEventDllCeAsnd:
			{	// ASnd has been received, but it may be not the requested one
/*
                    // report if SoA was correctly answered
                    Ret = EplDllkAsyncFrameNotReceived(EplDllkInstance_g.m_LastReqServiceId,
                                                       EplDllkInstance_g.m_uiLastTargetNodeId);
*/
				if (EplDllkInstance_g.m_DllState ==
				    kEplDllMsWaitAsnd) {
					EplDllkInstance_g.m_DllState =
					    kEplDllMsWaitSocTrig;
				}
				break;
			}

		default:
			break;
		}
		break;
#endif //(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)

	default:
		break;
	}

	if (DllEvent.m_ulDllErrorEvents != 0) {	// error event set -> post it to error handler
		Event.m_EventSink = kEplEventSinkErrk;
		Event.m_EventType = kEplEventTypeDllError;
		// $$$ d.k. set Event.m_NetTime to current time
		Event.m_uiSize = sizeof(DllEvent);
		Event.m_pArg = &DllEvent;
		Ret = EplEventkPost(&Event);
	}

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplDllkCbFrameReceived()
//
// Description: called from EdrvInterruptHandler()
//
// Parameters:  pRxBuffer_p             = receive buffer structure
//
// Returns:     (none)
//
//
// State:
//
//---------------------------------------------------------------------------

static void EplDllkCbFrameReceived(tEdrvRxBuffer * pRxBuffer_p)
{
	tEplKernel Ret = kEplSuccessful;
	tEplNmtState NmtState;
	tEplNmtEvent NmtEvent = kEplNmtEventNoEvent;
	tEplEvent Event;
	tEplFrame *pFrame;
	tEplFrame *pTxFrame;
	tEdrvTxBuffer *pTxBuffer = NULL;
	tEplFrameInfo FrameInfo;
	tEplMsgType MsgType;
	tEplDllReqServiceId ReqServiceId;
	unsigned int uiAsndServiceId;
	unsigned int uiNodeId;
	u8 bFlag1;

	BENCHMARK_MOD_02_SET(3);
	NmtState = EplNmtkGetNmtState();

	if (NmtState <= kEplNmtGsResetConfiguration) {
		goto Exit;
	}

	pFrame = (tEplFrame *) pRxBuffer_p->m_pbBuffer;

#if EDRV_EARLY_RX_INT != FALSE
	switch (pRxBuffer_p->m_BufferInFrame) {
	case kEdrvBufferFirstInFrame:
		{
			MsgType =
			    (tEplMsgType) AmiGetByteFromLe(&pFrame->
							   m_le_bMessageType);
			if (MsgType == kEplMsgTypePreq) {
				if (EplDllkInstance_g.m_DllState == kEplDllCsWaitPreq) {	// PReq expected and actually received
					// d.k.: The condition above is sufficent, because EPL cycle is active
					//       and no non-EPL frame shall be received in isochronous phase.
					// start transmission PRes
					// $$$ What if Tx buffer is invalid?
					pTxBuffer =
					    &EplDllkInstance_g.
					    m_pTxBuffer[EPL_DLLK_TXFRAME_PRES];
#if (EPL_DLL_PRES_READY_AFTER_SOA != FALSE) || (EPL_DLL_PRES_READY_AFTER_SOC != FALSE)
					Ret = EdrvTxMsgStart(pTxBuffer);
#else
					pTxFrame =
					    (tEplFrame *) pTxBuffer->m_pbBuffer;
					// update frame (NMT state, RD, RS, PR, MS, EN flags)
					AmiSetByteToLe(&pTxFrame->m_Data.m_Pres.
						       m_le_bNmtStatus,
						       (u8) NmtState);
					AmiSetByteToLe(&pTxFrame->m_Data.m_Pres.
						       m_le_bFlag2,
						       EplDllkInstance_g.
						       m_bFlag2);
					if (NmtState != kEplNmtCsOperational) {	// mark PDO as invalid in NMT state Op
						// $$$ reset only RD flag; set other flags appropriately
						AmiSetByteToLe(&pTxFrame->
							       m_Data.m_Pres.
							       m_le_bFlag1, 0);
					}
					// $$$ make function that updates Pres, StatusRes
					// send PRes frame
					Ret = EdrvSendTxMsg(pTxBuffer);
#endif
				}
			}
			goto Exit;
		}

	case kEdrvBufferMiddleInFrame:
		{
			goto Exit;
		}

	case kEdrvBufferLastInFrame:
		{
			break;
		}
	}
#endif

	FrameInfo.m_pFrame = pFrame;
	FrameInfo.m_uiFrameSize = pRxBuffer_p->m_uiRxMsgLen;
	FrameInfo.m_NetTime.m_dwNanoSec = pRxBuffer_p->m_NetTime.m_dwNanoSec;
	FrameInfo.m_NetTime.m_dwSec = pRxBuffer_p->m_NetTime.m_dwSec;

	if (AmiGetWordFromBe(&pFrame->m_be_wEtherType) != EPL_C_DLL_ETHERTYPE_EPL) {	// non-EPL frame
		//TRACE2("EplDllkCbFrameReceived: pfnCbAsync=0x%p SrcMAC=0x%llx\n", EplDllkInstance_g.m_pfnCbAsync, AmiGetQword48FromBe(pFrame->m_be_abSrcMac));
		if (EplDllkInstance_g.m_pfnCbAsync != NULL) {	// handler for async frames is registered
			EplDllkInstance_g.m_pfnCbAsync(&FrameInfo);
		}

		goto Exit;
	}

	MsgType = (tEplMsgType) AmiGetByteFromLe(&pFrame->m_le_bMessageType);
	switch (MsgType) {
	case kEplMsgTypePreq:
		{
			// PReq frame
			// d.k.: (we assume that this PReq frame is intended for us and don't check DstNodeId)
			if (AmiGetByteFromLe(&pFrame->m_le_bDstNodeId) != EplDllkInstance_g.m_DllConfigParam.m_uiNodeId) {	// this PReq is not intended for us
				goto Exit;
			}
			NmtEvent = kEplNmtEventDllCePreq;

			if (NmtState >= kEplNmtMsNotActive) {	// MN is active -> wrong msg type
				break;
			}
#if EDRV_EARLY_RX_INT == FALSE
			if (NmtState >= kEplNmtCsPreOperational2) {	// respond to and process PReq frames only in PreOp2, ReadyToOp and Op
				// Does PRes exist?
				pTxBuffer =
				    &EplDllkInstance_g.
				    m_pTxBuffer[EPL_DLLK_TXFRAME_PRES];
				if (pTxBuffer->m_pbBuffer != NULL) {	// PRes does exist
#if (EPL_DLL_PRES_READY_AFTER_SOA != FALSE) || (EPL_DLL_PRES_READY_AFTER_SOC != FALSE)
					EdrvTxMsgStart(pTxBuffer);
#else
					pTxFrame =
					    (tEplFrame *) pTxBuffer->m_pbBuffer;
					// update frame (NMT state, RD, RS, PR, MS, EN flags)
					AmiSetByteToLe(&pTxFrame->m_Data.m_Pres.
						       m_le_bNmtStatus,
						       (u8) NmtState);
					AmiSetByteToLe(&pTxFrame->m_Data.m_Pres.
						       m_le_bFlag2,
						       EplDllkInstance_g.
						       m_bFlag2);
					bFlag1 =
					    AmiGetByteFromLe(&pFrame->m_Data.
							     m_Preq.
							     m_le_bFlag1);
					// save EA flag
					EplDllkInstance_g.m_bMnFlag1 =
					    (EplDllkInstance_g.
					     m_bMnFlag1 & ~EPL_FRAME_FLAG1_EA)
					    | (bFlag1 & EPL_FRAME_FLAG1_EA);
					// preserve MS flag
					bFlag1 &= EPL_FRAME_FLAG1_MS;
					// add EN flag from Error signaling module
					bFlag1 |=
					    EplDllkInstance_g.
					    m_bFlag1 & EPL_FRAME_FLAG1_EN;
					if (NmtState != kEplNmtCsOperational) {	// mark PDO as invalid in NMT state Op
						// reset only RD flag
						AmiSetByteToLe(&pTxFrame->
							       m_Data.m_Pres.
							       m_le_bFlag1,
							       bFlag1);
					} else {	// leave RD flag untouched
						AmiSetByteToLe(&pTxFrame->
							       m_Data.m_Pres.
							       m_le_bFlag1,
							       (AmiGetByteFromLe
								(&pTxFrame->
								 m_Data.m_Pres.
								 m_le_bFlag1) &
								EPL_FRAME_FLAG1_RD)
							       | bFlag1);
					}
					// $$$ update EPL_DLL_PRES_READY_AFTER_* code
					// send PRes frame
					Ret = EdrvSendTxMsg(pTxBuffer);
					if (Ret != kEplSuccessful) {
						goto Exit;
					}
#endif
				}
#endif
				// inform PDO module
#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_PDOK)) != 0)
				if (NmtState >= kEplNmtCsReadyToOperate) {	// inform PDO module only in ReadyToOp and Op
					if (NmtState != kEplNmtCsOperational) {
						// reset RD flag and all other flags, but that does not matter, because they were processed above
						AmiSetByteToLe(&pFrame->m_Data.
							       m_Preq.
							       m_le_bFlag1, 0);
					}
					// compares real frame size and PDO size
					if ((unsigned
					     int)(AmiGetWordFromLe(&pFrame->
								   m_Data.
								   m_Preq.
								   m_le_wSize) +
						  24)
					    > FrameInfo.m_uiFrameSize) {	// format error
						tEplErrorHandlerkEvent DllEvent;

						DllEvent.m_ulDllErrorEvents =
						    EPL_DLL_ERR_INVALID_FORMAT;
						DllEvent.m_uiNodeId =
						    AmiGetByteFromLe(&pFrame->
								     m_le_bSrcNodeId);
						DllEvent.m_NmtState = NmtState;
						Event.m_EventSink =
						    kEplEventSinkErrk;
						Event.m_EventType =
						    kEplEventTypeDllError;
						Event.m_NetTime =
						    FrameInfo.m_NetTime;
						Event.m_uiSize =
						    sizeof(DllEvent);
						Event.m_pArg = &DllEvent;
						Ret = EplEventkPost(&Event);
						break;
					}
					// forward PReq frame as RPDO to PDO module
					Ret = EplPdokCbPdoReceived(&FrameInfo);

				}
#if (EPL_DLL_PRES_READY_AFTER_SOC != FALSE)
				if (pTxBuffer->m_pbBuffer != NULL) {	// PRes does exist
					// inform PDO module about PRes after PReq
					FrameInfo.m_pFrame =
					    (tEplFrame *) pTxBuffer->m_pbBuffer;
					FrameInfo.m_uiFrameSize =
					    pTxBuffer->m_uiMaxBufferLen;
					Ret =
					    EplPdokCbPdoTransmitted(&FrameInfo);
				}
#endif
#endif

#if EDRV_EARLY_RX_INT == FALSE
				// $$$ inform emergency protocol handling (error signaling module) about flags
			}
#endif

			// reset cycle counter
			EplDllkInstance_g.m_uiCycleCount = 0;

			break;
		}

	case kEplMsgTypePres:
		{
#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
			tEplDllkNodeInfo *pIntNodeInfo;
			tEplHeartbeatEvent HeartbeatEvent;
#endif

			// PRes frame
			NmtEvent = kEplNmtEventDllCePres;

			uiNodeId = AmiGetByteFromLe(&pFrame->m_le_bSrcNodeId);

			if ((NmtState >= kEplNmtCsPreOperational2)
			    && (NmtState <= kEplNmtCsOperational)) {	// process PRes frames only in PreOp2, ReadyToOp and Op of CN

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
				pIntNodeInfo = EplDllkGetNodeInfo(uiNodeId);
				if (pIntNodeInfo == NULL) {	// no node info structure available
					Ret = kEplDllNoNodeInfo;
					goto Exit;
				}
			} else if (EplDllkInstance_g.m_DllState == kEplDllMsWaitPres) {	// or process PRes frames in MsWaitPres

				pIntNodeInfo = EplDllkInstance_g.m_pCurNodeInfo;
				if ((pIntNodeInfo == NULL) || (pIntNodeInfo->m_uiNodeId != uiNodeId)) {	// ignore PRes, because it is from wrong CN
					// $$$ maybe post event to NmtMn module
					goto Exit;
				}
				// forward Flag2 to asynchronous scheduler
				bFlag1 =
				    AmiGetByteFromLe(&pFrame->m_Data.m_Asnd.
						     m_Payload.m_StatusResponse.
						     m_le_bFlag2);
				Ret =
				    EplDllkCalAsyncSetPendingRequests(uiNodeId,
								      ((tEplDllAsyncReqPriority) ((bFlag1 & EPL_FRAME_FLAG2_PR) >> EPL_FRAME_FLAG2_PR_SHIFT)), (bFlag1 & EPL_FRAME_FLAG2_RS));

#endif
			} else {	// ignore PRes, because it was received in wrong NMT state
				// but execute EplDllkChangeState() and post event to NMT module
				break;
			}

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
			{	// check NMT state of CN
				HeartbeatEvent.m_wErrorCode = EPL_E_NO_ERROR;
				HeartbeatEvent.m_NmtState =
				    (tEplNmtState) (AmiGetByteFromLe
						    (&pFrame->m_Data.m_Pres.
						     m_le_bNmtStatus) |
						    EPL_NMT_TYPE_CS);
				if (pIntNodeInfo->m_NmtState != HeartbeatEvent.m_NmtState) {	// NMT state of CN has changed -> post event to NmtMnu module
					if (pIntNodeInfo->m_fSoftDelete == FALSE) {	// normal isochronous CN
						HeartbeatEvent.m_uiNodeId =
						    uiNodeId;
						Event.m_EventSink =
						    kEplEventSinkNmtMnu;
						Event.m_EventType =
						    kEplEventTypeHeartbeat;
						Event.m_uiSize =
						    sizeof(HeartbeatEvent);
						Event.m_pArg = &HeartbeatEvent;
					} else {	// CN shall be deleted softly
						Event.m_EventSink =
						    kEplEventSinkDllkCal;
						Event.m_EventType =
						    kEplEventTypeDllkSoftDelNode;
						Event.m_uiSize =
						    sizeof(unsigned int);
						Event.m_pArg =
						    &pIntNodeInfo->m_uiNodeId;
					}
					Event.m_NetTime = FrameInfo.m_NetTime;
					Ret = EplEventkPost(&Event);

					// save current NMT state of CN in internal node structure
					pIntNodeInfo->m_NmtState =
					    HeartbeatEvent.m_NmtState;
				}
			}
#endif

			// inform PDO module
#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_PDOK)) != 0)
			if ((NmtState != kEplNmtCsPreOperational2)
			    && (NmtState != kEplNmtMsPreOperational2)) {	// inform PDO module only in ReadyToOp and Op
				// compare real frame size and PDO size?
				if (((unsigned
				      int)(AmiGetWordFromLe(&pFrame->m_Data.
							    m_Pres.m_le_wSize) +
					   24)
				     > FrameInfo.m_uiFrameSize)
#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
				    ||
				    (AmiGetWordFromLe
				     (&pFrame->m_Data.m_Pres.m_le_wSize) >
				     pIntNodeInfo->m_wPresPayloadLimit)
#endif
				    ) {	// format error
					tEplErrorHandlerkEvent DllEvent;

					DllEvent.m_ulDllErrorEvents =
					    EPL_DLL_ERR_INVALID_FORMAT;
					DllEvent.m_uiNodeId = uiNodeId;
					DllEvent.m_NmtState = NmtState;
					Event.m_EventSink = kEplEventSinkErrk;
					Event.m_EventType =
					    kEplEventTypeDllError;
					Event.m_NetTime = FrameInfo.m_NetTime;
					Event.m_uiSize = sizeof(DllEvent);
					Event.m_pArg = &DllEvent;
					Ret = EplEventkPost(&Event);
					break;
				}
				if ((NmtState != kEplNmtCsOperational)
				    && (NmtState != kEplNmtMsOperational)) {
					// reset RD flag and all other flags, but that does not matter, because they were processed above
					AmiSetByteToLe(&pFrame->m_Data.m_Pres.
						       m_le_bFlag1, 0);
				}
				Ret = EplPdokCbPdoReceived(&FrameInfo);
			}
#endif

			break;
		}

	case kEplMsgTypeSoc:
		{
			// SoC frame
			NmtEvent = kEplNmtEventDllCeSoc;

			if (NmtState >= kEplNmtMsNotActive) {	// MN is active -> wrong msg type
				break;
			}
#if EPL_DLL_PRES_READY_AFTER_SOC != FALSE
			// post PRes to transmit FIFO of the ethernet controller, but don't start
			// transmission over bus
			pTxBuffer =
			    &EplDllkInstance_g.
			    m_pTxBuffer[EPL_DLLK_TXFRAME_PRES];
			// Does PRes exist?
			if (pTxBuffer->m_pbBuffer != NULL) {	// PRes does exist
				pTxFrame = (tEplFrame *) pTxBuffer->m_pbBuffer;
				// update frame (NMT state, RD, RS, PR, MS, EN flags)
				if (NmtState < kEplNmtCsPreOperational2) {	// NMT state is not PreOp2, ReadyToOp or Op
					// fake NMT state PreOp2, because PRes will be sent only in PreOp2 or greater
					NmtState = kEplNmtCsPreOperational2;
				}
				AmiSetByteToLe(&pTxFrame->m_Data.m_Pres.
					       m_le_bNmtStatus,
					       (u8) NmtState);
				AmiSetByteToLe(&pTxFrame->m_Data.m_Pres.
					       m_le_bFlag2,
					       EplDllkInstance_g.m_bFlag2);
				if (NmtState != kEplNmtCsOperational) {	// mark PDO as invalid in NMT state Op
					// $$$ reset only RD flag; set other flags appropriately
					AmiSetByteToLe(&pTxFrame->m_Data.m_Pres.
						       m_le_bFlag1, 0);
				}
				// $$$ make function that updates Pres, StatusRes
				// mark PRes frame as ready for transmission
				Ret = EdrvTxMsgReady(pTxBuffer);
			}
#endif

			if (NmtState >= kEplNmtCsPreOperational2) {	// SoC frames only in PreOp2, ReadyToOp and Op
				// trigger synchronous task
				Event.m_EventSink = kEplEventSinkSync;
				Event.m_EventType = kEplEventTypeSync;
				Event.m_uiSize = 0;
				Ret = EplEventkPost(&Event);

				// update cycle counter
				if (EplDllkInstance_g.m_DllConfigParam.m_uiMultiplCycleCnt > 0) {	// multiplexed cycle active
					EplDllkInstance_g.m_uiCycleCount =
					    (EplDllkInstance_g.m_uiCycleCount +
					     1) %
					    EplDllkInstance_g.m_DllConfigParam.
					    m_uiMultiplCycleCnt;
				}
			}
			// reprogram timer
#if EPL_TIMER_USE_HIGHRES != FALSE
			if (EplDllkInstance_g.m_ullFrameTimeout != 0) {
				Ret =
				    EplTimerHighReskModifyTimerNs
				    (&EplDllkInstance_g.m_TimerHdlCycle,
				     EplDllkInstance_g.m_ullFrameTimeout,
				     EplDllkCbCnTimer, 0L, FALSE);
			}
#endif

			break;
		}

	case kEplMsgTypeSoa:
		{
			// SoA frame
			NmtEvent = kEplNmtEventDllCeSoa;

			if (NmtState >= kEplNmtMsNotActive) {	// MN is active -> wrong msg type
				break;
			}

			pTxFrame = NULL;

			if ((NmtState & EPL_NMT_SUPERSTATE_MASK) != EPL_NMT_CS_EPLMODE) {	// do not respond, if NMT state is < PreOp1 (i.e. not EPL_MODE)
				break;
			}
			// check TargetNodeId
			uiNodeId =
			    AmiGetByteFromLe(&pFrame->m_Data.m_Soa.
					     m_le_bReqServiceTarget);
			if (uiNodeId == EplDllkInstance_g.m_DllConfigParam.m_uiNodeId) {	// local node is the target of the current request

				// check ServiceId
				ReqServiceId =
				    (tEplDllReqServiceId)
				    AmiGetByteFromLe(&pFrame->m_Data.m_Soa.
						     m_le_bReqServiceId);
				if (ReqServiceId == kEplDllReqServiceStatus) {	// StatusRequest
					if (EplDllkInstance_g.m_pTxBuffer[EPL_DLLK_TXFRAME_STATUSRES].m_pbBuffer != NULL) {	// StatusRes does exist

						pTxFrame =
						    (tEplFrame *)
						    EplDllkInstance_g.
						    m_pTxBuffer
						    [EPL_DLLK_TXFRAME_STATUSRES].
						    m_pbBuffer;
						// update StatusRes frame (NMT state, EN, EC, RS, PR flags)
						AmiSetByteToLe(&pTxFrame->
							       m_Data.m_Asnd.
							       m_Payload.
							       m_StatusResponse.
							       m_le_bNmtStatus,
							       (u8) NmtState);
						AmiSetByteToLe(&pTxFrame->
							       m_Data.m_Asnd.
							       m_Payload.
							       m_StatusResponse.
							       m_le_bFlag1,
							       EplDllkInstance_g.
							       m_bFlag1);
						AmiSetByteToLe(&pTxFrame->
							       m_Data.m_Asnd.
							       m_Payload.
							       m_StatusResponse.
							       m_le_bFlag2,
							       EplDllkInstance_g.
							       m_bFlag2);
						// send StatusRes
						Ret =
						    EdrvSendTxMsg
						    (&EplDllkInstance_g.
						     m_pTxBuffer
						     [EPL_DLLK_TXFRAME_STATUSRES]);
						if (Ret != kEplSuccessful) {
							goto Exit;
						}
						TGT_DBG_SIGNAL_TRACE_POINT(8);

						// update error signaling
						bFlag1 =
						    AmiGetByteFromLe(&pFrame->
								     m_Data.
								     m_Soa.
								     m_le_bFlag1);
						if (((bFlag1 ^ EplDllkInstance_g.m_bMnFlag1) & EPL_FRAME_FLAG1_ER) != 0) {	// exception reset flag was changed by MN
							// assume same state for EC in next cycle (clear all other bits)
							if ((bFlag1 &
							     EPL_FRAME_FLAG1_ER)
							    != 0) {
								// set EC and reset rest
								EplDllkInstance_g.
								    m_bFlag1 =
								    EPL_FRAME_FLAG1_EC;
							} else {
								// reset complete flag 1 (including EC and EN)
								EplDllkInstance_g.
								    m_bFlag1 =
								    0;
							}
						}
						// save flag 1 from MN for Status request response cycle
						EplDllkInstance_g.m_bMnFlag1 =
						    bFlag1;
					}
				} else if (ReqServiceId == kEplDllReqServiceIdent) {	// IdentRequest
					if (EplDllkInstance_g.m_pTxBuffer[EPL_DLLK_TXFRAME_IDENTRES].m_pbBuffer != NULL) {	// IdentRes does exist
						pTxFrame =
						    (tEplFrame *)
						    EplDllkInstance_g.
						    m_pTxBuffer
						    [EPL_DLLK_TXFRAME_IDENTRES].
						    m_pbBuffer;
						// update IdentRes frame (NMT state, RS, PR flags)
						AmiSetByteToLe(&pTxFrame->
							       m_Data.m_Asnd.
							       m_Payload.
							       m_IdentResponse.
							       m_le_bNmtStatus,
							       (u8) NmtState);
						AmiSetByteToLe(&pTxFrame->
							       m_Data.m_Asnd.
							       m_Payload.
							       m_IdentResponse.
							       m_le_bFlag2,
							       EplDllkInstance_g.
							       m_bFlag2);
						// send IdentRes
						Ret =
						    EdrvSendTxMsg
						    (&EplDllkInstance_g.
						     m_pTxBuffer
						     [EPL_DLLK_TXFRAME_IDENTRES]);
						if (Ret != kEplSuccessful) {
							goto Exit;
						}
						TGT_DBG_SIGNAL_TRACE_POINT(7);
					}
				} else if (ReqServiceId == kEplDllReqServiceNmtRequest) {	// NmtRequest
					if (EplDllkInstance_g.m_pTxBuffer[EPL_DLLK_TXFRAME_NMTREQ].m_pbBuffer != NULL) {	// NmtRequest does exist
						// check if frame is not empty and not being filled
						if (EplDllkInstance_g.
						    m_pTxBuffer
						    [EPL_DLLK_TXFRAME_NMTREQ].
						    m_uiTxMsgLen >
						    EPL_DLLK_BUFLEN_FILLING) {
							/*if (EplDllkInstance_g.m_pTxBuffer[EPL_DLLK_TXFRAME_NMTREQ].m_uiTxMsgLen < EPL_DLLK_BUFLEN_MIN)
							   {   // pad frame
							   EplDllkInstance_g.m_pTxBuffer[EPL_DLLK_TXFRAME_NMTREQ].m_uiTxMsgLen = EPL_DLLK_BUFLEN_MIN;
							   } */
							// memorize transmission
							pTxFrame =
							    (tEplFrame *) 1;
							// send NmtRequest
							Ret =
							    EdrvSendTxMsg
							    (&EplDllkInstance_g.
							     m_pTxBuffer
							     [EPL_DLLK_TXFRAME_NMTREQ]);
							if (Ret !=
							    kEplSuccessful) {
								goto Exit;
							}

						}
					}

				} else if (ReqServiceId == kEplDllReqServiceUnspecified) {	// unspecified invite
					if (EplDllkInstance_g.m_pTxBuffer[EPL_DLLK_TXFRAME_NONEPL].m_pbBuffer != NULL) {	// non-EPL frame does exist
						// check if frame is not empty and not being filled
						if (EplDllkInstance_g.
						    m_pTxBuffer
						    [EPL_DLLK_TXFRAME_NONEPL].
						    m_uiTxMsgLen >
						    EPL_DLLK_BUFLEN_FILLING) {
							/*if (EplDllkInstance_g.m_pTxBuffer[EPL_DLLK_TXFRAME_NMTREQ].m_uiTxMsgLen < EPL_DLLK_BUFLEN_MIN)
							   {   // pad frame
							   EplDllkInstance_g.m_pTxBuffer[EPL_DLLK_TXFRAME_NMTREQ].m_uiTxMsgLen = EPL_DLLK_BUFLEN_MIN;
							   } */
							// memorize transmission
							pTxFrame =
							    (tEplFrame *) 1;
							// send non-EPL frame
							Ret =
							    EdrvSendTxMsg
							    (&EplDllkInstance_g.
							     m_pTxBuffer
							     [EPL_DLLK_TXFRAME_NONEPL]);
							if (Ret !=
							    kEplSuccessful) {
								goto Exit;
							}

						}
					}

				} else if (ReqServiceId == kEplDllReqServiceNo) {	// no async service requested -> do nothing
				}
			}
#if EPL_DLL_PRES_READY_AFTER_SOA != FALSE
			if (pTxFrame == NULL) {	// signal process function readiness of PRes frame
				Event.m_EventSink = kEplEventSinkDllk;
				Event.m_EventType = kEplEventTypeDllkPresReady;
				Event.m_uiSize = 0;
				Event.m_pArg = NULL;
				Ret = EplEventkPost(&Event);
			}
#endif

			// inform PDO module
#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_PDOK)) != 0)
//            Ret = EplPdokCbSoa(&FrameInfo);
#endif

			// $$$ put SrcNodeId, NMT state and NetTime as HeartbeatEvent into eventqueue

			// $$$ inform emergency protocol handling about flags
			break;
		}

	case kEplMsgTypeAsnd:
		{
			// ASnd frame
			NmtEvent = kEplNmtEventDllCeAsnd;

			// ASnd service registered?
			uiAsndServiceId =
			    (unsigned int)AmiGetByteFromLe(&pFrame->m_Data.
							   m_Asnd.
							   m_le_bServiceId);

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
			if ((EplDllkInstance_g.m_DllState >= kEplDllMsNonCyclic)
			    &&
			    ((((tEplDllAsndServiceId) uiAsndServiceId) ==
			      kEplDllAsndStatusResponse)
			     || (((tEplDllAsndServiceId) uiAsndServiceId) == kEplDllAsndIdentResponse))) {	// StatusRes or IdentRes received
				uiNodeId =
				    AmiGetByteFromLe(&pFrame->m_le_bSrcNodeId);
				if ((EplDllkInstance_g.m_LastReqServiceId ==
				     ((tEplDllReqServiceId) uiAsndServiceId))
				    && (uiNodeId == EplDllkInstance_g.m_uiLastTargetNodeId)) {	// mark request as responded
					EplDllkInstance_g.m_LastReqServiceId =
					    kEplDllReqServiceNo;
				}
				if (((tEplDllAsndServiceId) uiAsndServiceId) == kEplDllAsndIdentResponse) {	// memorize MAC address of CN for PReq
					tEplDllkNodeInfo *pIntNodeInfo;

					pIntNodeInfo =
					    EplDllkGetNodeInfo(uiNodeId);
					if (pIntNodeInfo == NULL) {	// no node info structure available
						Ret = kEplDllNoNodeInfo;
					} else {
						EPL_MEMCPY(pIntNodeInfo->
							   m_be_abMacAddr,
							   pFrame->
							   m_be_abSrcMac, 6);
					}
				}
				// forward Flag2 to asynchronous scheduler
				bFlag1 =
				    AmiGetByteFromLe(&pFrame->m_Data.m_Asnd.
						     m_Payload.m_StatusResponse.
						     m_le_bFlag2);
				Ret =
				    EplDllkCalAsyncSetPendingRequests(uiNodeId,
								      ((tEplDllAsyncReqPriority) ((bFlag1 & EPL_FRAME_FLAG2_PR) >> EPL_FRAME_FLAG2_PR_SHIFT)), (bFlag1 & EPL_FRAME_FLAG2_RS));
			}
#endif

			if (uiAsndServiceId < EPL_DLL_MAX_ASND_SERVICE_ID) {	// ASnd service ID is valid
				if (EplDllkInstance_g.m_aAsndFilter[uiAsndServiceId] == kEplDllAsndFilterAny) {	// ASnd service ID is registered
					// forward frame via async receive FIFO to userspace
					Ret =
					    EplDllkCalAsyncFrameReceived
					    (&FrameInfo);
				} else if (EplDllkInstance_g.m_aAsndFilter[uiAsndServiceId] == kEplDllAsndFilterLocal) {	// ASnd service ID is registered, but only local node ID or broadcasts
					// shall be forwarded
					uiNodeId =
					    AmiGetByteFromLe(&pFrame->
							     m_le_bDstNodeId);
					if ((uiNodeId ==
					     EplDllkInstance_g.m_DllConfigParam.
					     m_uiNodeId)
					    || (uiNodeId == EPL_C_ADR_BROADCAST)) {	// ASnd frame is intended for us
						// forward frame via async receive FIFO to userspace
						Ret =
						    EplDllkCalAsyncFrameReceived
						    (&FrameInfo);
					}
				}
			}
			break;
		}

	default:
		{
			break;
		}
	}

	if (NmtEvent != kEplNmtEventNoEvent) {	// event for DLL and NMT state machine generated
		Ret = EplDllkChangeState(NmtEvent, NmtState);
		if (Ret != kEplSuccessful) {
			goto Exit;
		}

		if ((NmtEvent != kEplNmtEventDllCeAsnd)
		    && ((NmtState <= kEplNmtCsPreOperational1) || (NmtEvent != kEplNmtEventDllCePres))) {	// NMT state machine is not interested in ASnd frames and PRes frames when not CsNotActive or CsPreOp1
			// inform NMT module
			Event.m_EventSink = kEplEventSinkNmtk;
			Event.m_EventType = kEplEventTypeNmtEvent;
			Event.m_uiSize = sizeof(NmtEvent);
			Event.m_pArg = &NmtEvent;
			Ret = EplEventkPost(&Event);
		}
	}

      Exit:
	if (Ret != kEplSuccessful) {
		u32 dwArg;

		BENCHMARK_MOD_02_TOGGLE(9);

		dwArg = EplDllkInstance_g.m_DllState | (NmtEvent << 8);

		// Error event for API layer
		Ret = EplEventkPostError(kEplEventSourceDllk,
					 Ret, sizeof(dwArg), &dwArg);
	}
	BENCHMARK_MOD_02_RESET(3);
	return;
}

//---------------------------------------------------------------------------
//
// Function:    EplDllkCbFrameTransmitted()
//
// Description: called from EdrvInterruptHandler().
//              It signals
//
// Parameters:  pRxBuffer_p             = receive buffer structure
//
// Returns:     (none)
//
//
// State:
//
//---------------------------------------------------------------------------

static void EplDllkCbFrameTransmitted(tEdrvTxBuffer * pTxBuffer_p)
{
	tEplKernel Ret = kEplSuccessful;
	tEplEvent Event;
	tEplDllAsyncReqPriority Priority;
	tEplNmtState NmtState;

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_PDOK)) != 0) \
    && (EPL_DLL_PRES_READY_AFTER_SOC == FALSE)
	tEplFrameInfo FrameInfo;
#endif

	NmtState = EplNmtkGetNmtState();

	if (NmtState <= kEplNmtGsResetConfiguration) {
		goto Exit;
	}

	if ((pTxBuffer_p - EplDllkInstance_g.m_pTxBuffer) == EPL_DLLK_TXFRAME_NMTREQ) {	// frame from NMT request FIFO sent
		// mark Tx-buffer as empty
		pTxBuffer_p->m_uiTxMsgLen = EPL_DLLK_BUFLEN_EMPTY;

		// post event to DLL
		Priority = kEplDllAsyncReqPrioNmt;
		Event.m_EventSink = kEplEventSinkDllk;
		Event.m_EventType = kEplEventTypeDllkFillTx;
		EPL_MEMSET(&Event.m_NetTime, 0x00, sizeof(Event.m_NetTime));
		Event.m_pArg = &Priority;
		Event.m_uiSize = sizeof(Priority);
		Ret = EplEventkPost(&Event);
	} else if ((pTxBuffer_p - EplDllkInstance_g.m_pTxBuffer) == EPL_DLLK_TXFRAME_NONEPL) {	// frame from generic priority FIFO sent
		// mark Tx-buffer as empty
		pTxBuffer_p->m_uiTxMsgLen = EPL_DLLK_BUFLEN_EMPTY;

		// post event to DLL
		Priority = kEplDllAsyncReqPrioGeneric;
		Event.m_EventSink = kEplEventSinkDllk;
		Event.m_EventType = kEplEventTypeDllkFillTx;
		EPL_MEMSET(&Event.m_NetTime, 0x00, sizeof(Event.m_NetTime));
		Event.m_pArg = &Priority;
		Event.m_uiSize = sizeof(Priority);
		Ret = EplEventkPost(&Event);
	}
#if ((((EPL_MODULE_INTEGRATION) & (EPL_MODULE_PDOK)) != 0) \
    && (EPL_DLL_PRES_READY_AFTER_SOC == FALSE)) \
    || (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
	else if ((pTxBuffer_p->m_EplMsgType == kEplMsgTypePreq)
		 || (pTxBuffer_p->m_EplMsgType == kEplMsgTypePres)) {	// PRes resp. PReq frame sent

#if ((((EPL_MODULE_INTEGRATION) & (EPL_MODULE_PDOK)) != 0) \
            && (EPL_DLL_PRES_READY_AFTER_SOC == FALSE))
		{
			// inform PDO module
			FrameInfo.m_pFrame =
			    (tEplFrame *) pTxBuffer_p->m_pbBuffer;
			FrameInfo.m_uiFrameSize = pTxBuffer_p->m_uiMaxBufferLen;
			Ret = EplPdokCbPdoTransmitted(&FrameInfo);
		}
#endif

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
		{
			// if own Pres on MN, trigger SoA
			if ((NmtState >= kEplNmtMsPreOperational2)
			    && (pTxBuffer_p ==
				&EplDllkInstance_g.
				m_pTxBuffer[EPL_DLLK_TXFRAME_PRES])) {
				Ret =
				    EplDllkChangeState(kEplNmtEventDllMeSoaTrig,
						       NmtState);
			}
		}
#endif

#if EPL_DLL_PRES_READY_AFTER_SOA != FALSE
		goto Exit;
#endif
	}
#endif
#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)
	else if (pTxBuffer_p->m_EplMsgType == kEplMsgTypeSoa) {	// SoA frame sent
		tEplNmtEvent NmtEvent = kEplNmtEventDllMeSoaSent;

		// check if we are invited
		if (EplDllkInstance_g.m_uiLastTargetNodeId ==
		    EplDllkInstance_g.m_DllConfigParam.m_uiNodeId) {
			tEplFrame *pTxFrame;

			if (EplDllkInstance_g.m_LastReqServiceId == kEplDllReqServiceStatus) {	// StatusRequest
				if (EplDllkInstance_g.m_pTxBuffer[EPL_DLLK_TXFRAME_STATUSRES].m_pbBuffer != NULL) {	// StatusRes does exist

					pTxFrame =
					    (tEplFrame *) EplDllkInstance_g.
					    m_pTxBuffer
					    [EPL_DLLK_TXFRAME_STATUSRES].
					    m_pbBuffer;
					// update StatusRes frame (NMT state, EN, EC, RS, PR flags)
					AmiSetByteToLe(&pTxFrame->m_Data.m_Asnd.
						       m_Payload.
						       m_StatusResponse.
						       m_le_bNmtStatus,
						       (u8) NmtState);
					AmiSetByteToLe(&pTxFrame->m_Data.m_Asnd.
						       m_Payload.
						       m_StatusResponse.
						       m_le_bFlag1,
						       EplDllkInstance_g.
						       m_bFlag1);
					AmiSetByteToLe(&pTxFrame->m_Data.m_Asnd.
						       m_Payload.
						       m_StatusResponse.
						       m_le_bFlag2,
						       EplDllkInstance_g.
						       m_bFlag2);
					// send StatusRes
					Ret =
					    EdrvSendTxMsg(&EplDllkInstance_g.
							  m_pTxBuffer
							  [EPL_DLLK_TXFRAME_STATUSRES]);
					if (Ret != kEplSuccessful) {
						goto Exit;
					}
					TGT_DBG_SIGNAL_TRACE_POINT(8);

				}
			} else if (EplDllkInstance_g.m_LastReqServiceId == kEplDllReqServiceIdent) {	// IdentRequest
				if (EplDllkInstance_g.m_pTxBuffer[EPL_DLLK_TXFRAME_IDENTRES].m_pbBuffer != NULL) {	// IdentRes does exist
					pTxFrame =
					    (tEplFrame *) EplDllkInstance_g.
					    m_pTxBuffer
					    [EPL_DLLK_TXFRAME_IDENTRES].
					    m_pbBuffer;
					// update IdentRes frame (NMT state, RS, PR flags)
					AmiSetByteToLe(&pTxFrame->m_Data.m_Asnd.
						       m_Payload.
						       m_IdentResponse.
						       m_le_bNmtStatus,
						       (u8) NmtState);
					AmiSetByteToLe(&pTxFrame->m_Data.m_Asnd.
						       m_Payload.
						       m_IdentResponse.
						       m_le_bFlag2,
						       EplDllkInstance_g.
						       m_bFlag2);
					// send IdentRes
					Ret =
					    EdrvSendTxMsg(&EplDllkInstance_g.
							  m_pTxBuffer
							  [EPL_DLLK_TXFRAME_IDENTRES]);
					if (Ret != kEplSuccessful) {
						goto Exit;
					}
					TGT_DBG_SIGNAL_TRACE_POINT(7);
				}
			} else if (EplDllkInstance_g.m_LastReqServiceId == kEplDllReqServiceNmtRequest) {	// NmtRequest
				if (EplDllkInstance_g.m_pTxBuffer[EPL_DLLK_TXFRAME_NMTREQ].m_pbBuffer != NULL) {	// NmtRequest does exist
					// check if frame is not empty and not being filled
					if (EplDllkInstance_g.
					    m_pTxBuffer
					    [EPL_DLLK_TXFRAME_NMTREQ].
					    m_uiTxMsgLen >
					    EPL_DLLK_BUFLEN_FILLING) {
						// check if this frame is a NMT command,
						// then forward this frame back to NmtMnu module,
						// because it needs the time, when this frame is
						// actually sent, to start the timer for monitoring
						// the NMT state change.

						pTxFrame =
						    (tEplFrame *)
						    EplDllkInstance_g.
						    m_pTxBuffer
						    [EPL_DLLK_TXFRAME_NMTREQ].
						    m_pbBuffer;
						if ((AmiGetByteFromLe
						     (&pTxFrame->
						      m_le_bMessageType)
						     == (u8) kEplMsgTypeAsnd)
						    &&
						    (AmiGetByteFromLe
						     (&pTxFrame->m_Data.m_Asnd.
						      m_le_bServiceId)
						     == (u8) kEplDllAsndNmtCommand)) {	// post event directly to NmtMnu module
							Event.m_EventSink =
							    kEplEventSinkNmtMnu;
							Event.m_EventType =
							    kEplEventTypeNmtMnuNmtCmdSent;
							Event.m_uiSize =
							    EplDllkInstance_g.
							    m_pTxBuffer
							    [EPL_DLLK_TXFRAME_NMTREQ].
							    m_uiTxMsgLen;
							Event.m_pArg = pTxFrame;
							Ret =
							    EplEventkPost
							    (&Event);

						}
						// send NmtRequest
						Ret =
						    EdrvSendTxMsg
						    (&EplDllkInstance_g.
						     m_pTxBuffer
						     [EPL_DLLK_TXFRAME_NMTREQ]);
						if (Ret != kEplSuccessful) {
							goto Exit;
						}

					}
				}

			} else if (EplDllkInstance_g.m_LastReqServiceId == kEplDllReqServiceUnspecified) {	// unspecified invite
				if (EplDllkInstance_g.m_pTxBuffer[EPL_DLLK_TXFRAME_NONEPL].m_pbBuffer != NULL) {	// non-EPL frame does exist
					// check if frame is not empty and not being filled
					if (EplDllkInstance_g.
					    m_pTxBuffer
					    [EPL_DLLK_TXFRAME_NONEPL].
					    m_uiTxMsgLen >
					    EPL_DLLK_BUFLEN_FILLING) {
						// send non-EPL frame
						Ret =
						    EdrvSendTxMsg
						    (&EplDllkInstance_g.
						     m_pTxBuffer
						     [EPL_DLLK_TXFRAME_NONEPL]);
						if (Ret != kEplSuccessful) {
							goto Exit;
						}

					}
				}
			}
			// ASnd frame was sent, remove the request
			EplDllkInstance_g.m_LastReqServiceId =
			    kEplDllReqServiceNo;
		}
		// forward event to ErrorHandler and PDO module
		Event.m_EventSink = kEplEventSinkNmtk;
		Event.m_EventType = kEplEventTypeNmtEvent;
		Event.m_uiSize = sizeof(NmtEvent);
		Event.m_pArg = &NmtEvent;
		Ret = EplEventkPost(&Event);
		if (Ret != kEplSuccessful) {
			goto Exit;
		}
	}
#endif

#if EPL_DLL_PRES_READY_AFTER_SOA != FALSE
	else {			// d.k.: Why that else? on CN it is entered on IdentRes and StatusRes
		goto Exit;
	}

	// signal process function readiness of PRes frame
	Event.m_EventSink = kEplEventSinkDllk;
	Event.m_EventType = kEplEventTypeDllkPresReady;
	Event.m_uiSize = 0;
	Event.m_pArg = NULL;
	Ret = EplEventkPost(&Event);

#endif

      Exit:
	if (Ret != kEplSuccessful) {
		u32 dwArg;

		BENCHMARK_MOD_02_TOGGLE(9);

		dwArg =
		    EplDllkInstance_g.m_DllState | (pTxBuffer_p->
						    m_EplMsgType << 16);

		// Error event for API layer
		Ret = EplEventkPostError(kEplEventSourceDllk,
					 Ret, sizeof(dwArg), &dwArg);
	}

	return;
}

//---------------------------------------------------------------------------
//
// Function:    EplDllkCheckFrame()
//
// Description: check frame and set missing information
//
// Parameters:  pFrame_p                = ethernet frame
//              uiFrameSize_p           = size of frame
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

static tEplKernel EplDllkCheckFrame(tEplFrame * pFrame_p,
				    unsigned int uiFrameSize_p)
{
	tEplMsgType MsgType;
	u16 wEtherType;

	// check frame
	if (pFrame_p != NULL) {
		// check SrcMAC
		if (AmiGetQword48FromBe(pFrame_p->m_be_abSrcMac) == 0) {
			// source MAC address
			EPL_MEMCPY(&pFrame_p->m_be_abSrcMac[0],
				   &EplDllkInstance_g.m_be_abSrcMac[0], 6);
		}
		// check ethertype
		wEtherType = AmiGetWordFromBe(&pFrame_p->m_be_wEtherType);
		if (wEtherType == 0) {
			// assume EPL frame
			wEtherType = EPL_C_DLL_ETHERTYPE_EPL;
			AmiSetWordToBe(&pFrame_p->m_be_wEtherType, wEtherType);
		}

		if (wEtherType == EPL_C_DLL_ETHERTYPE_EPL) {
			// source node ID
			AmiSetByteToLe(&pFrame_p->m_le_bSrcNodeId,
				       (u8) EplDllkInstance_g.
				       m_DllConfigParam.m_uiNodeId);

			// check message type
			MsgType =
			    AmiGetByteFromLe(&pFrame_p->m_le_bMessageType);
			if (MsgType == 0) {
				MsgType = kEplMsgTypeAsnd;
				AmiSetByteToLe(&pFrame_p->m_le_bMessageType,
					       (u8) MsgType);
			}

			if (MsgType == kEplMsgTypeAsnd) {
				// destination MAC address
				AmiSetQword48ToBe(&pFrame_p->m_be_abDstMac[0],
						  EPL_C_DLL_MULTICAST_ASND);
			}

		}
	}

	return kEplSuccessful;
}

//---------------------------------------------------------------------------
//
// Function:    EplDllkCbCnTimer()
//
// Description: called by timer module. It monitors the EPL cycle when it is a CN.
//
// Parameters:  pEventArg_p             = timer event argument
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

#if EPL_TIMER_USE_HIGHRES != FALSE
static tEplKernel EplDllkCbCnTimer(tEplTimerEventArg *pEventArg_p)
{
	tEplKernel Ret = kEplSuccessful;
	tEplNmtState NmtState;

#if EPL_TIMER_USE_HIGHRES != FALSE
	if (pEventArg_p->m_TimerHdl != EplDllkInstance_g.m_TimerHdlCycle) {	// zombie callback
		// just exit
		goto Exit;
	}
#endif

	NmtState = EplNmtkGetNmtState();

	if (NmtState <= kEplNmtGsResetConfiguration) {
		goto Exit;
	}

	Ret = EplDllkChangeState(kEplNmtEventDllCeFrameTimeout, NmtState);
	if (Ret != kEplSuccessful) {
		goto Exit;
	}
	// 2008/10/15 d.k. reprogramming of timer not necessary,
	// because it will be programmed, when SoC is received.
/*
    // reprogram timer
#if EPL_TIMER_USE_HIGHRES != FALSE
    if ((NmtState > kEplNmtCsPreOperational1)
        && (EplDllkInstance_g.m_ullFrameTimeout != 0))
    {
        Ret = EplTimerHighReskModifyTimerNs(&EplDllkInstance_g.m_TimerHdlCycle, EplDllkInstance_g.m_ullFrameTimeout, EplDllkCbCnTimer, 0L, FALSE);
    }
#endif
*/

      Exit:
	if (Ret != kEplSuccessful) {
		u32 dwArg;

		BENCHMARK_MOD_02_TOGGLE(9);

		dwArg =
		    EplDllkInstance_g.
		    m_DllState | (kEplNmtEventDllCeFrameTimeout << 8);

		// Error event for API layer
		Ret = EplEventkPostError(kEplEventSourceDllk,
					 Ret, sizeof(dwArg), &dwArg);
	}

	return Ret;
}
#endif

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)

//---------------------------------------------------------------------------
//
// Function:    EplDllkCbMnTimerCycle()
//
// Description: called by timer module. It triggers the SoC when it is a MN.
//
// Parameters:  pEventArg_p             = timer event argument
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

static tEplKernel EplDllkCbMnTimerCycle(tEplTimerEventArg *pEventArg_p)
{
	tEplKernel Ret = kEplSuccessful;
	tEplNmtState NmtState;

#if EPL_TIMER_USE_HIGHRES != FALSE
	if (pEventArg_p->m_TimerHdl != EplDllkInstance_g.m_TimerHdlCycle) {	// zombie callback
		// just exit
		goto Exit;
	}
#endif

	NmtState = EplNmtkGetNmtState();

	if (NmtState <= kEplNmtGsResetConfiguration) {
		goto Exit;
	}

	Ret = EplDllkChangeState(kEplNmtEventDllMeSocTrig, NmtState);

      Exit:
	if (Ret != kEplSuccessful) {
		u32 dwArg;

		BENCHMARK_MOD_02_TOGGLE(9);

		dwArg =
		    EplDllkInstance_g.
		    m_DllState | (kEplNmtEventDllMeSocTrig << 8);

		// Error event for API layer
		Ret = EplEventkPostError(kEplEventSourceDllk,
					 Ret, sizeof(dwArg), &dwArg);
	}

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplDllkCbMnTimerResponse()
//
// Description: called by timer module. It monitors the PRes timeout.
//
// Parameters:  pEventArg_p             = timer event argument
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

static tEplKernel EplDllkCbMnTimerResponse(tEplTimerEventArg *pEventArg_p)
{
	tEplKernel Ret = kEplSuccessful;
	tEplNmtState NmtState;

#if EPL_TIMER_USE_HIGHRES != FALSE
	if (pEventArg_p->m_TimerHdl != EplDllkInstance_g.m_TimerHdlResponse) {	// zombie callback
		// just exit
		goto Exit;
	}
#endif

	NmtState = EplNmtkGetNmtState();

	if (NmtState <= kEplNmtGsResetConfiguration) {
		goto Exit;
	}

	Ret = EplDllkChangeState(kEplNmtEventDllMePresTimeout, NmtState);

      Exit:
	if (Ret != kEplSuccessful) {
		u32 dwArg;

		BENCHMARK_MOD_02_TOGGLE(9);

		dwArg =
		    EplDllkInstance_g.
		    m_DllState | (kEplNmtEventDllMePresTimeout << 8);

		// Error event for API layer
		Ret = EplEventkPostError(kEplEventSourceDllk,
					 Ret, sizeof(dwArg), &dwArg);
	}

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplDllkGetNodeInfo()
//
// Description: returns node info structure of the specified node.
//
// Parameters:  uiNodeId_p              = node ID
//
// Returns:     tEplDllkNodeInfo*       = pointer to internal node info structure
//
//
// State:
//
//---------------------------------------------------------------------------

static tEplDllkNodeInfo *EplDllkGetNodeInfo(unsigned int uiNodeId_p)
{
	// $$$ d.k.: use hash algorithm to retrieve the appropriate node info structure
	//           if size of array is less than 254.
	uiNodeId_p--;		// node ID starts at 1 but array at 0
	if (uiNodeId_p >= tabentries(EplDllkInstance_g.m_aNodeInfo)) {
		return NULL;
	} else {
		return &EplDllkInstance_g.m_aNodeInfo[uiNodeId_p];
	}
}

//---------------------------------------------------------------------------
//
// Function:    EplDllkMnSendSoa()
//
// Description: it updates and transmits the SoA.
//
// Parameters:  NmtState_p              = current NMT state
//              pDllStateProposed_p     = proposed DLL state
//              fEnableInvitation_p     = enable invitation for asynchronous phase
//                                        it will be disabled for EPL_C_DLL_PREOP1_START_CYCLES SoAs
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

static tEplKernel EplDllkMnSendSoa(tEplNmtState NmtState_p,
				   tEplDllState * pDllStateProposed_p,
				   BOOL fEnableInvitation_p)
{
	tEplKernel Ret = kEplSuccessful;
	tEdrvTxBuffer *pTxBuffer = NULL;
	tEplFrame *pTxFrame;
	tEplDllkNodeInfo *pNodeInfo;

	*pDllStateProposed_p = kEplDllMsNonCyclic;

	pTxBuffer = &EplDllkInstance_g.m_pTxBuffer[EPL_DLLK_TXFRAME_SOA];
	if (pTxBuffer->m_pbBuffer != NULL) {	// SoA does exist
		pTxFrame = (tEplFrame *) pTxBuffer->m_pbBuffer;

		if (fEnableInvitation_p != FALSE) {	// fetch target of asynchronous phase
			if (EplDllkInstance_g.m_bFlag2 == 0) {	// own queues are empty
				EplDllkInstance_g.m_LastReqServiceId =
				    kEplDllReqServiceNo;
			} else if (((tEplDllAsyncReqPriority) (EplDllkInstance_g.m_bFlag2 >> EPL_FRAME_FLAG2_PR_SHIFT)) == kEplDllAsyncReqPrioNmt) {	// frames in own NMT request queue available
				EplDllkInstance_g.m_LastReqServiceId =
				    kEplDllReqServiceNmtRequest;
			} else {
				EplDllkInstance_g.m_LastReqServiceId =
				    kEplDllReqServiceUnspecified;
			}
			Ret =
			    EplDllkCalAsyncGetSoaRequest(&EplDllkInstance_g.
							 m_LastReqServiceId,
							 &EplDllkInstance_g.
							 m_uiLastTargetNodeId);
			if (Ret != kEplSuccessful) {
				goto Exit;
			}
			if (EplDllkInstance_g.m_LastReqServiceId != kEplDllReqServiceNo) {	// asynchronous phase will be assigned to one node
				if (EplDllkInstance_g.m_uiLastTargetNodeId == EPL_C_ADR_INVALID) {	// exchange invalid node ID with local node ID
					EplDllkInstance_g.m_uiLastTargetNodeId =
					    EplDllkInstance_g.m_DllConfigParam.
					    m_uiNodeId;
					// d.k. DLL state WaitAsndTrig is not helpful;
					//      so just step over to WaitSocTrig,
					//      because own ASnd is sent automatically in CbFrameTransmitted() after SoA.
					//*pDllStateProposed_p = kEplDllMsWaitAsndTrig;
					*pDllStateProposed_p =
					    kEplDllMsWaitSocTrig;
				} else {	// assignment to CN
					*pDllStateProposed_p =
					    kEplDllMsWaitAsnd;
				}

				pNodeInfo =
				    EplDllkGetNodeInfo(EplDllkInstance_g.
						       m_uiLastTargetNodeId);
				if (pNodeInfo == NULL) {	// no node info structure available
					Ret = kEplDllNoNodeInfo;
					goto Exit;
				}
				// update frame (EA, ER flags)
				AmiSetByteToLe(&pTxFrame->m_Data.m_Soa.
					       m_le_bFlag1,
					       pNodeInfo->
					       m_bSoaFlag1 & (EPL_FRAME_FLAG1_EA
							      |
							      EPL_FRAME_FLAG1_ER));
			} else {	// no assignment of asynchronous phase
				*pDllStateProposed_p = kEplDllMsWaitSocTrig;
				EplDllkInstance_g.m_uiLastTargetNodeId =
				    EPL_C_ADR_INVALID;
			}

			// update frame (target)
			AmiSetByteToLe(&pTxFrame->m_Data.m_Soa.
				       m_le_bReqServiceId,
				       (u8) EplDllkInstance_g.
				       m_LastReqServiceId);
			AmiSetByteToLe(&pTxFrame->m_Data.m_Soa.
				       m_le_bReqServiceTarget,
				       (u8) EplDllkInstance_g.
				       m_uiLastTargetNodeId);

		} else {	// invite nobody
			// update frame (target)
			AmiSetByteToLe(&pTxFrame->m_Data.m_Soa.
				       m_le_bReqServiceId, (u8) 0);
			AmiSetByteToLe(&pTxFrame->m_Data.m_Soa.
				       m_le_bReqServiceTarget, (u8) 0);
		}

		// update frame (NMT state)
		AmiSetByteToLe(&pTxFrame->m_Data.m_Soa.m_le_bNmtStatus,
			       (u8) NmtState_p);

		// send SoA frame
		Ret = EdrvSendTxMsg(pTxBuffer);
	}

      Exit:
	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplDllkMnSendSoc()
//
// Description: it updates and transmits the SoA.
//
// Parameters:  (none)
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

static tEplKernel EplDllkMnSendSoc(void)
{
	tEplKernel Ret = kEplSuccessful;
	tEdrvTxBuffer *pTxBuffer = NULL;
	tEplFrame *pTxFrame;
	tEplEvent Event;

	pTxBuffer = &EplDllkInstance_g.m_pTxBuffer[EPL_DLLK_TXFRAME_SOC];
	if (pTxBuffer->m_pbBuffer != NULL) {	// SoC does exist
		pTxFrame = (tEplFrame *) pTxBuffer->m_pbBuffer;

		// $$$ update NetTime

		// send SoC frame
		Ret = EdrvSendTxMsg(pTxBuffer);
		if (Ret != kEplSuccessful) {
			goto Exit;
		}
		// trigger synchronous task
		Event.m_EventSink = kEplEventSinkSync;
		Event.m_EventType = kEplEventTypeSync;
		Event.m_uiSize = 0;
		Ret = EplEventkPost(&Event);
	}

      Exit:
	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplDllkMnSendPreq()
//
// Description: it updates and transmits the PReq for the next isochronous CN
//              or own PRes if enabled.
//
// Parameters:  NmtState_p              = current NMT state
//              pDllStateProposed_p     = proposed DLL state
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

static tEplKernel EplDllkMnSendPreq(tEplNmtState NmtState_p,
				    tEplDllState * pDllStateProposed_p)
{
	tEplKernel Ret = kEplSuccessful;
	tEdrvTxBuffer *pTxBuffer = NULL;
	tEplFrame *pTxFrame;
	u8 bFlag1 = 0;

	if (EplDllkInstance_g.m_pCurNodeInfo == NULL) {	// start with first isochronous CN
		EplDllkInstance_g.m_pCurNodeInfo =
		    EplDllkInstance_g.m_pFirstNodeInfo;
	} else {		// iterate to next isochronous CN
		EplDllkInstance_g.m_pCurNodeInfo =
		    EplDllkInstance_g.m_pCurNodeInfo->m_pNextNodeInfo;
	}

	if (EplDllkInstance_g.m_pCurNodeInfo == NULL) {	// last isochronous CN reached
		Ret = EplDllkMnSendSoa(NmtState_p, pDllStateProposed_p, TRUE);
		goto Exit;
	} else {
		pTxBuffer = EplDllkInstance_g.m_pCurNodeInfo->m_pPreqTxBuffer;
		bFlag1 =
		    EplDllkInstance_g.m_pCurNodeInfo->
		    m_bSoaFlag1 & EPL_FRAME_FLAG1_EA;
		*pDllStateProposed_p = kEplDllMsWaitPres;

		// start PRes Timer
		// $$$ d.k.: maybe move this call to CbFrameTransmitted(), because the time should run from there
#if EPL_TIMER_USE_HIGHRES != FALSE
		Ret =
		    EplTimerHighReskModifyTimerNs(&EplDllkInstance_g.
						  m_TimerHdlResponse,
						  EplDllkInstance_g.
						  m_pCurNodeInfo->
						  m_dwPresTimeout,
						  EplDllkCbMnTimerResponse, 0L,
						  FALSE);
#endif
	}

	if (pTxBuffer == NULL) {	// PReq does not exist
		Ret = kEplDllTxBufNotReady;
		goto Exit;
	}

	pTxFrame = (tEplFrame *) pTxBuffer->m_pbBuffer;

	if (pTxFrame != NULL) {	// PReq does exist
		if (NmtState_p == kEplNmtMsOperational) {	// leave RD flag untouched
			bFlag1 |=
			    AmiGetByteFromLe(&pTxFrame->m_Data.m_Preq.
					     m_le_bFlag1) & EPL_FRAME_FLAG1_RD;
		}

		if (pTxBuffer == &EplDllkInstance_g.m_pTxBuffer[EPL_DLLK_TXFRAME_PRES]) {	// PRes of MN will be sent
			// update NMT state
			AmiSetByteToLe(&pTxFrame->m_Data.m_Pres.m_le_bNmtStatus,
				       (u8) NmtState_p);
			*pDllStateProposed_p = kEplDllMsWaitSoaTrig;
		}
		// $$$ d.k. set EPL_FRAME_FLAG1_MS if necessary
		// update frame (Flag1)
		AmiSetByteToLe(&pTxFrame->m_Data.m_Preq.m_le_bFlag1, bFlag1);

		// calculate frame size from payload size
		pTxBuffer->m_uiTxMsgLen =
		    AmiGetWordFromLe(&pTxFrame->m_Data.m_Preq.m_le_wSize) + 24;

		// send PReq frame
		Ret = EdrvSendTxMsg(pTxBuffer);
	} else {
		Ret = kEplDllTxFrameInvalid;
	}

      Exit:
	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplDllkAsyncFrameNotReceived()
//
// Description: passes empty ASnd frame to receive FIFO.
//              It will be called only for frames with registered AsndServiceIds
//              (only kEplDllAsndFilterAny).
//
// Parameters:  none
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

static tEplKernel EplDllkAsyncFrameNotReceived(tEplDllReqServiceId
					       ReqServiceId_p,
					       unsigned int uiNodeId_p)
{
	tEplKernel Ret = kEplSuccessful;
	u8 abBuffer[18];
	tEplFrame *pFrame = (tEplFrame *) abBuffer;
	tEplFrameInfo FrameInfo;

	// check if previous SoA invitation was not answered
	switch (ReqServiceId_p) {
	case kEplDllReqServiceIdent:
	case kEplDllReqServiceStatus:
		// ASnd service registered?
		if (EplDllkInstance_g.m_aAsndFilter[ReqServiceId_p] == kEplDllAsndFilterAny) {	// ASnd service ID is registered
			AmiSetByteToLe(&pFrame->m_le_bSrcNodeId,
				       (u8) uiNodeId_p);
			// EPL MsgType ASnd
			AmiSetByteToLe(&pFrame->m_le_bMessageType,
				       (u8) kEplMsgTypeAsnd);
			// ASnd Service ID
			AmiSetByteToLe(&pFrame->m_Data.m_Asnd.m_le_bServiceId,
				       (u8) ReqServiceId_p);
			// create frame info structure
			FrameInfo.m_pFrame = pFrame;
			FrameInfo.m_uiFrameSize = 18;	// empty non existing ASnd frame
			// forward frame via async receive FIFO to userspace
			Ret = EplDllkCalAsyncFrameReceived(&FrameInfo);
		}
		break;
	default:
		// no invitation issued or it was successfully answered or it is uninteresting
		break;
	}

	return Ret;
}

#endif //(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)

#endif // #if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_DLLK)) != 0)
// EOF
