/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  source file for NMT-CN-Userspace-Module

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

                $RCSfile: EplNmtCnu.c,v $

                $Author: D.Krueger $

                $Revision: 1.6 $  $Date: 2008/10/17 15:32:32 $

                $State: Exp $

                Build Environment:
                    GCC V3.4

  -------------------------------------------------------------------------

  Revision History:

  2006/06/09 k.t.:   start of the implementation

****************************************************************************/

#include "EplInc.h"
#include "user/EplNmtCnu.h"
#include "user/EplDlluCal.h"

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_CN)) != 0)

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
	unsigned int m_uiNodeId;
	tEplNmtuCheckEventCallback m_pfnCheckEventCb;

} tEplNmtCnuInstance;

//---------------------------------------------------------------------------
// modul globale vars
//---------------------------------------------------------------------------

static tEplNmtCnuInstance EplNmtCnuInstance_g;

//---------------------------------------------------------------------------
// local function prototypes
//---------------------------------------------------------------------------

static tEplNmtCommand EplNmtCnuGetNmtCommand(tEplFrameInfo * pFrameInfo_p);

static BOOL EplNmtCnuNodeIdList(u8 * pbNmtCommandDate_p);

static tEplKernel EplNmtCnuCommandCb(tEplFrameInfo *pFrameInfo_p);

//=========================================================================//
//                                                                         //
//          P U B L I C   F U N C T I O N S                                //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//
// Function:    EplNmtCnuInit
//
// Description: init the first instance of the module
//
//
//
// Parameters:      uiNodeId_p = NodeId of the local node
//
//
// Returns:         tEplKernel = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel EplNmtCnuInit(unsigned int uiNodeId_p)
{
	tEplKernel Ret;

	Ret = EplNmtCnuAddInstance(uiNodeId_p);

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplNmtCnuAddInstance
//
// Description: init the add new instance of the module
//
//
//
// Parameters:      uiNodeId_p = NodeId of the local node
//
//
// Returns:         tEplKernel = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel EplNmtCnuAddInstance(unsigned int uiNodeId_p)
{
	tEplKernel Ret;

	Ret = kEplSuccessful;

	// reset instance structure
	EPL_MEMSET(&EplNmtCnuInstance_g, 0, sizeof(EplNmtCnuInstance_g));

	// save nodeid
	EplNmtCnuInstance_g.m_uiNodeId = uiNodeId_p;

	// register callback-function for NMT-commands
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_DLLU)) != 0)
	Ret = EplDlluCalRegAsndService(kEplDllAsndNmtCommand,
				       EplNmtCnuCommandCb,
				       kEplDllAsndFilterLocal);
#endif

	return Ret;

}

//---------------------------------------------------------------------------
//
// Function:    EplNmtCnuDelInstance
//
// Description: delte instance of the module
//
//
//
// Parameters:
//
//
// Returns:         tEplKernel = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel EplNmtCnuDelInstance(void)
{
	tEplKernel Ret;

	Ret = kEplSuccessful;

#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_DLLU)) != 0)
	// deregister callback function from DLL
	Ret = EplDlluCalRegAsndService(kEplDllAsndNmtCommand,
				       NULL, kEplDllAsndFilterNone);
#endif

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplNmtCnuSendNmtRequest
//
// Description: Send an NMT-Request to the MN
//
//
//
// Parameters:      uiNodeId_p = NodeId of the local node
//                  NmtCommand_p = requested NMT-Command
//
//
// Returns:         tEplKernel = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
tEplKernel EplNmtCnuSendNmtRequest(unsigned int uiNodeId_p,
				   tEplNmtCommand NmtCommand_p)
{
	tEplKernel Ret;
	tEplFrameInfo NmtRequestFrameInfo;
	tEplFrame NmtRequestFrame;

	Ret = kEplSuccessful;

	// build frame
	EPL_MEMSET(&NmtRequestFrame.m_be_abDstMac[0], 0x00, sizeof(NmtRequestFrame.m_be_abDstMac));	// set by DLL
	EPL_MEMSET(&NmtRequestFrame.m_be_abSrcMac[0], 0x00, sizeof(NmtRequestFrame.m_be_abSrcMac));	// set by DLL
	AmiSetWordToBe(&NmtRequestFrame.m_be_wEtherType,
		       EPL_C_DLL_ETHERTYPE_EPL);
	AmiSetByteToLe(&NmtRequestFrame.m_le_bDstNodeId, (u8) EPL_C_ADR_MN_DEF_NODE_ID);	// node id of the MN
	AmiSetByteToLe(&NmtRequestFrame.m_le_bMessageType,
		       (u8) kEplMsgTypeAsnd);
	AmiSetByteToLe(&NmtRequestFrame.m_Data.m_Asnd.m_le_bServiceId,
		       (u8) kEplDllAsndNmtRequest);
	AmiSetByteToLe(&NmtRequestFrame.m_Data.m_Asnd.m_Payload.
		       m_NmtRequestService.m_le_bNmtCommandId,
		       (u8) NmtCommand_p);
	AmiSetByteToLe(&NmtRequestFrame.m_Data.m_Asnd.m_Payload.m_NmtRequestService.m_le_bTargetNodeId, (u8) uiNodeId_p);	// target for the nmt command
	EPL_MEMSET(&NmtRequestFrame.m_Data.m_Asnd.m_Payload.m_NmtRequestService.
		   m_le_abNmtCommandData[0], 0x00,
		   sizeof(NmtRequestFrame.m_Data.m_Asnd.m_Payload.
			  m_NmtRequestService.m_le_abNmtCommandData));

	// build info-structure
	NmtRequestFrameInfo.m_NetTime.m_dwNanoSec = 0;
	NmtRequestFrameInfo.m_NetTime.m_dwSec = 0;
	NmtRequestFrameInfo.m_pFrame = &NmtRequestFrame;
	NmtRequestFrameInfo.m_uiFrameSize = EPL_C_DLL_MINSIZE_NMTREQ;	// sizeof(NmtRequestFrame);

	// send NMT-Request
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_DLLU)) != 0)
	Ret = EplDlluCalAsyncSend(&NmtRequestFrameInfo,	// pointer to frameinfo
				  kEplDllAsyncReqPrioNmt);	// priority
#endif

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplNmtCnuRegisterStateChangeCb
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

tEplKernel EplNmtCnuRegisterCheckEventCb(tEplNmtuCheckEventCallback pfnEplNmtCheckEventCb_p)
{
	tEplKernel Ret;

	Ret = kEplSuccessful;

	// save callback-function in modul global var
	EplNmtCnuInstance_g.m_pfnCheckEventCb = pfnEplNmtCheckEventCb_p;

	return Ret;

}

//=========================================================================//
//                                                                         //
//          P R I V A T E   F U N C T I O N S                              //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//
// Function:    EplNmtCnuCommandCb
//
// Description: callback funktion for NMT-Commands
//
//
//
// Parameters:      pFrameInfo_p = Frame with the NMT-Commando
//
//
// Returns:         tEplKernel = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------
static tEplKernel EplNmtCnuCommandCb(tEplFrameInfo *pFrameInfo_p)
{
	tEplKernel Ret = kEplSuccessful;
	tEplNmtCommand NmtCommand;
	BOOL fNodeIdInList;
	tEplNmtEvent NmtEvent = kEplNmtEventNoEvent;

	if (pFrameInfo_p == NULL) {
		Ret = kEplNmtInvalidFramePointer;
		goto Exit;
	}

	NmtCommand = EplNmtCnuGetNmtCommand(pFrameInfo_p);

	// check NMT-Command
	switch (NmtCommand) {

		//------------------------------------------------------------------------
		// plain NMT state commands
	case kEplNmtCmdStartNode:
		{		// send NMT-Event to state maschine kEplNmtEventStartNode
			NmtEvent = kEplNmtEventStartNode;
			break;
		}

	case kEplNmtCmdStopNode:
		{		// send NMT-Event to state maschine kEplNmtEventStopNode
			NmtEvent = kEplNmtEventStopNode;
			break;
		}

	case kEplNmtCmdEnterPreOperational2:
		{		// send NMT-Event to state maschine kEplNmtEventEnterPreOperational2
			NmtEvent = kEplNmtEventEnterPreOperational2;
			break;
		}

	case kEplNmtCmdEnableReadyToOperate:
		{		// send NMT-Event to state maschine kEplNmtEventEnableReadyToOperate
			NmtEvent = kEplNmtEventEnableReadyToOperate;
			break;
		}

	case kEplNmtCmdResetNode:
		{		// send NMT-Event to state maschine kEplNmtEventResetNode
			NmtEvent = kEplNmtEventResetNode;
			break;
		}

	case kEplNmtCmdResetCommunication:
		{		// send NMT-Event to state maschine kEplNmtEventResetCom
			NmtEvent = kEplNmtEventResetCom;
			break;
		}

	case kEplNmtCmdResetConfiguration:
		{		// send NMT-Event to state maschine kEplNmtEventResetConfig
			NmtEvent = kEplNmtEventResetConfig;
			break;
		}

	case kEplNmtCmdSwReset:
		{		// send NMT-Event to state maschine kEplNmtEventSwReset
			NmtEvent = kEplNmtEventSwReset;
			break;
		}

		//------------------------------------------------------------------------
		// extended NMT state commands

	case kEplNmtCmdStartNodeEx:
		{
			// check if own nodeid is in EPL node list
			fNodeIdInList =
			    EplNmtCnuNodeIdList(&
						(pFrameInfo_p->m_pFrame->m_Data.
						 m_Asnd.m_Payload.
						 m_NmtCommandService.
						 m_le_abNmtCommandData[0]));
			if (fNodeIdInList != FALSE) {	// own nodeid in list
				// send event to process command
				NmtEvent = kEplNmtEventStartNode;
			}
			break;
		}

	case kEplNmtCmdStopNodeEx:
		{		// check if own nodeid is in EPL node list
			fNodeIdInList =
			    EplNmtCnuNodeIdList(&pFrameInfo_p->m_pFrame->m_Data.
						m_Asnd.m_Payload.
						m_NmtCommandService.
						m_le_abNmtCommandData[0]);
			if (fNodeIdInList != FALSE) {	// own nodeid in list
				// send event to process command
				NmtEvent = kEplNmtEventStopNode;
			}
			break;
		}

	case kEplNmtCmdEnterPreOperational2Ex:
		{		// check if own nodeid is in EPL node list
			fNodeIdInList =
			    EplNmtCnuNodeIdList(&pFrameInfo_p->m_pFrame->m_Data.
						m_Asnd.m_Payload.
						m_NmtCommandService.
						m_le_abNmtCommandData[0]);
			if (fNodeIdInList != FALSE) {	// own nodeid in list
				// send event to process command
				NmtEvent = kEplNmtEventEnterPreOperational2;
			}
			break;
		}

	case kEplNmtCmdEnableReadyToOperateEx:
		{		// check if own nodeid is in EPL node list
			fNodeIdInList =
			    EplNmtCnuNodeIdList(&pFrameInfo_p->m_pFrame->m_Data.
						m_Asnd.m_Payload.
						m_NmtCommandService.
						m_le_abNmtCommandData[0]);
			if (fNodeIdInList != FALSE) {	// own nodeid in list
				// send event to process command
				NmtEvent = kEplNmtEventEnableReadyToOperate;
			}
			break;
		}

	case kEplNmtCmdResetNodeEx:
		{		// check if own nodeid is in EPL node list
			fNodeIdInList =
			    EplNmtCnuNodeIdList(&pFrameInfo_p->m_pFrame->m_Data.
						m_Asnd.m_Payload.
						m_NmtCommandService.
						m_le_abNmtCommandData[0]);
			if (fNodeIdInList != FALSE) {	// own nodeid in list
				// send event to process command
				NmtEvent = kEplNmtEventResetNode;
			}
			break;
		}

	case kEplNmtCmdResetCommunicationEx:
		{		// check if own nodeid is in EPL node list
			fNodeIdInList =
			    EplNmtCnuNodeIdList(&pFrameInfo_p->m_pFrame->m_Data.
						m_Asnd.m_Payload.
						m_NmtCommandService.
						m_le_abNmtCommandData[0]);
			if (fNodeIdInList != FALSE) {	// own nodeid in list
				// send event to process command
				NmtEvent = kEplNmtEventResetCom;
			}
			break;
		}

	case kEplNmtCmdResetConfigurationEx:
		{		// check if own nodeid is in EPL node list
			fNodeIdInList =
			    EplNmtCnuNodeIdList(&pFrameInfo_p->m_pFrame->m_Data.
						m_Asnd.m_Payload.
						m_NmtCommandService.
						m_le_abNmtCommandData[0]);
			if (fNodeIdInList != FALSE) {	// own nodeid in list
				// send event to process command
				NmtEvent = kEplNmtEventResetConfig;
			}
			break;
		}

	case kEplNmtCmdSwResetEx:
		{		// check if own nodeid is in EPL node list
			fNodeIdInList =
			    EplNmtCnuNodeIdList(&pFrameInfo_p->m_pFrame->m_Data.
						m_Asnd.m_Payload.
						m_NmtCommandService.
						m_le_abNmtCommandData[0]);
			if (fNodeIdInList != FALSE) {	// own nodeid in list
				// send event to process command
				NmtEvent = kEplNmtEventSwReset;
			}
			break;
		}

		//------------------------------------------------------------------------
		// NMT managing commands

		// TODO: add functions to process managing command (optional)

	case kEplNmtCmdNetHostNameSet:
		{
			break;
		}

	case kEplNmtCmdFlushArpEntry:
		{
			break;
		}

		//------------------------------------------------------------------------
		// NMT info services

		// TODO: forward event with infos to the application (optional)

	case kEplNmtCmdPublishConfiguredCN:
		{
			break;
		}

	case kEplNmtCmdPublishActiveCN:
		{
			break;
		}

	case kEplNmtCmdPublishPreOperational1:
		{
			break;
		}

	case kEplNmtCmdPublishPreOperational2:
		{
			break;
		}

	case kEplNmtCmdPublishReadyToOperate:
		{
			break;
		}

	case kEplNmtCmdPublishOperational:
		{
			break;
		}

	case kEplNmtCmdPublishStopped:
		{
			break;
		}

	case kEplNmtCmdPublishEmergencyNew:
		{
			break;
		}

	case kEplNmtCmdPublishTime:
		{
			break;
		}

		//-----------------------------------------------------------------------
		// error from MN
		// -> requested command not supported by MN
	case kEplNmtCmdInvalidService:
		{

			// TODO: errorevent to application
			break;
		}

		//------------------------------------------------------------------------
		// default
	default:
		{
			Ret = kEplNmtUnknownCommand;
			goto Exit;
		}

	}			// end of switch(NmtCommand)

	if (NmtEvent != kEplNmtEventNoEvent) {
		if (EplNmtCnuInstance_g.m_pfnCheckEventCb != NULL) {
			Ret = EplNmtCnuInstance_g.m_pfnCheckEventCb(NmtEvent);
			if (Ret == kEplReject) {
				Ret = kEplSuccessful;
				goto Exit;
			} else if (Ret != kEplSuccessful) {
				goto Exit;
			}
		}
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMTU)) != 0)
		Ret = EplNmtuNmtEvent(NmtEvent);
#endif
	}

      Exit:
	return Ret;

}

//---------------------------------------------------------------------------
//
// Function:    EplNmtCnuGetNmtCommand()
//
// Description: returns the NMT-Command from the frame
//
//
//
// Parameters:      pFrameInfo_p = pointer to the Frame
//                                 with the NMT-Command
//
//
// Returns:         tEplNmtCommand = NMT-Command
//
//
// State:
//
//---------------------------------------------------------------------------
static tEplNmtCommand EplNmtCnuGetNmtCommand(tEplFrameInfo * pFrameInfo_p)
{
	tEplNmtCommand NmtCommand;
	tEplNmtCommandService *pNmtCommandService;

	pNmtCommandService =
	    &pFrameInfo_p->m_pFrame->m_Data.m_Asnd.m_Payload.
	    m_NmtCommandService;

	NmtCommand =
	    (tEplNmtCommand) AmiGetByteFromLe(&pNmtCommandService->
					      m_le_bNmtCommandId);

	return NmtCommand;
}

//---------------------------------------------------------------------------
//
// Function:    EplNmtCnuNodeIdList()
//
// Description: check if the own nodeid is set in EPL Node List
//
//
//
// Parameters:      pbNmtCommandDate_p = pointer to the data of the NMT Command
//
//
// Returns:         BOOL = TRUE if nodeid is set in EPL Node List
//                         FALSE if nodeid not set in EPL Node List
//
//
// State:
//
//---------------------------------------------------------------------------
static BOOL EplNmtCnuNodeIdList(u8 * pbNmtCommandDate_p)
{
	BOOL fNodeIdInList;
	unsigned int uiByteOffset;
	u8 bBitOffset;
	u8 bNodeListByte;

	// get byte-offset of the own nodeid in NodeIdList
	// devide though 8
	uiByteOffset = (unsigned int)(EplNmtCnuInstance_g.m_uiNodeId >> 3);
	// get bitoffset
	bBitOffset = (u8) EplNmtCnuInstance_g.m_uiNodeId % 8;

	bNodeListByte = AmiGetByteFromLe(&pbNmtCommandDate_p[uiByteOffset]);
	if ((bNodeListByte & bBitOffset) == 0) {
		fNodeIdInList = FALSE;
	} else {
		fNodeIdInList = TRUE;
	}

	return fNodeIdInList;
}

#endif // #if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_CN)) != 0)

// EOF
