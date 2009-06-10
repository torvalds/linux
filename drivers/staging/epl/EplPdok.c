/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  source file for kernel PDO module

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

                $RCSfile: EplPdok.c,v $

                $Author: D.Krueger $

                $Revision: 1.8 $  $Date: 2008/10/17 15:32:32 $

                $State: Exp $

                Build Environment:
                    GCC V3.4

  -------------------------------------------------------------------------

  Revision History:

  2006/05/22 d.k.:   start of the implementation, version 1.00

****************************************************************************/

#include "kernel/EplPdok.h"
#include "kernel/EplPdokCal.h"
#include "kernel/EplEventk.h"
#include "kernel/EplObdk.h"

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_PDOK)) != 0)

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_DLLK)) == 0)

#error 'ERROR: Missing DLLk-Modul!'

#endif

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDK)) == 0)

#error 'ERROR: Missing OBDk-Modul!'

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

#define EPL_PDOK_OBD_IDX_RX_COMM_PARAM  0x1400
#define EPL_PDOK_OBD_IDX_RX_MAPP_PARAM  0x1600
#define EPL_PDOK_OBD_IDX_TX_COMM_PARAM  0x1800
#define EPL_PDOK_OBD_IDX_TX_MAPP_PARAM  0x1A00

//---------------------------------------------------------------------------
// local types
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// modul globale vars
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// local function prototypes
//---------------------------------------------------------------------------

/***************************************************************************/
/*                                                                         */
/*                                                                         */
/*          C L A S S  EplPdok                                             */
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

//---------------------------------------------------------------------------
// local types
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// local vars
//---------------------------------------------------------------------------

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
// Function:    EplPdokAddInstance()
//
// Description: add and initialize new instance of EPL stack
//
// Parameters:  none
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplPdokAddInstance(void)
{

	return kEplSuccessful;
}

//---------------------------------------------------------------------------
//
// Function:    EplPdokDelInstance()
//
// Description: deletes an instance of EPL stack
//
// Parameters:  none
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplPdokDelInstance(void)
{

	return kEplSuccessful;
}

//---------------------------------------------------------------------------
//
// Function:    EplPdokCbPdoReceived
//
// Description: This function is called by DLL if PRes or PReq frame was
//              received. It posts the frame to the event queue.
//              It is called in states NMT_CS_READY_TO_OPERATE and NMT_CS_OPERATIONAL.
//              The passed PDO needs not to be valid.
//
// Parameters:  pFrameInfo_p            = pointer to frame info structure
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplPdokCbPdoReceived(tEplFrameInfo * pFrameInfo_p)
{
	tEplKernel Ret = kEplSuccessful;
	tEplEvent Event;

	Event.m_EventSink = kEplEventSinkPdok;
	Event.m_EventType = kEplEventTypePdoRx;
	// limit copied data to size of PDO (because from some CNs the frame is larger than necessary)
	Event.m_uiSize = AmiGetWordFromLe(&pFrameInfo_p->m_pFrame->m_Data.m_Pres.m_le_wSize) + 24;	// pFrameInfo_p->m_uiFrameSize;
	Event.m_pArg = pFrameInfo_p->m_pFrame;
	Ret = EplEventkPost(&Event);

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplPdokCbPdoTransmitted
//
// Description: This function is called by DLL if PRes or PReq frame was
//              sent. It posts the pointer to the frame to the event queue.
//              It is called in NMT_CS_PRE_OPERATIONAL_2,
//              NMT_CS_READY_TO_OPERATE and NMT_CS_OPERATIONAL.
//
// Parameters:  pFrameInfo_p            = pointer to frame info structure
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplPdokCbPdoTransmitted(tEplFrameInfo * pFrameInfo_p)
{
	tEplKernel Ret = kEplSuccessful;
	tEplEvent Event;

	Event.m_EventSink = kEplEventSinkPdok;
	Event.m_EventType = kEplEventTypePdoTx;
	Event.m_uiSize = sizeof(tEplFrameInfo);
	Event.m_pArg = pFrameInfo_p;
	Ret = EplEventkPost(&Event);

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplPdokCbSoa
//
// Description: This function is called by DLL if SoA frame was
//              received resp. sent. It posts this event to the event queue.
//
// Parameters:  pFrameInfo_p            = pointer to frame info structure
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplPdokCbSoa(tEplFrameInfo * pFrameInfo_p)
{
	tEplKernel Ret = kEplSuccessful;
	tEplEvent Event;

	Event.m_EventSink = kEplEventSinkPdok;
	Event.m_EventType = kEplEventTypePdoSoa;
	Event.m_uiSize = 0;
	Event.m_pArg = NULL;
	Ret = EplEventkPost(&Event);

	return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplPdokProcess
//
// Description: This function processes all received and transmitted PDOs.
//              This function must not be interrupted by any other task
//              except ISRs (like the ethernet driver ISR, which may call
//              EplPdokCbFrameReceived() or EplPdokCbFrameTransmitted()).
//
// Parameters:  pEvent_p                = pointer to event structure
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplPdokProcess(tEplEvent * pEvent_p)
{
	tEplKernel Ret = kEplSuccessful;
	u16 wPdoSize;
	u16 wBitOffset;
	u16 wBitSize;
	u16 wVarSize;
	u64 qwObjectMapping;
	u8 bMappSubindex;
	u8 bObdSubindex;
	u16 wObdMappIndex;
	u16 wObdCommIndex;
	u16 wPdoId;
	u8 bObdData;
	u8 bObjectCount;
	u8 bFrameData;
	BOOL fValid;
	tEplObdSize ObdSize;
	tEplFrame *pFrame;
	tEplFrameInfo *pFrameInfo;
	unsigned int uiNodeId;
	tEplMsgType MsgType;

	// 0xFF=invalid, RPDO: 0x00=PReq, localNodeId=PRes, remoteNodeId=PRes
	//               TPDO: 0x00=PRes, MN: CnNodeId=PReq

	switch (pEvent_p->m_EventType) {
	case kEplEventTypePdoRx:	// RPDO received
		pFrame = (tEplFrame *) pEvent_p->m_pArg;

		// check if received RPDO is valid
		bFrameData =
		    AmiGetByteFromLe(&pFrame->m_Data.m_Pres.m_le_bFlag1);
		if ((bFrameData & EPL_FRAME_FLAG1_RD) == 0) {	// RPDO invalid
			goto Exit;
		}
		// retrieve EPL message type
		MsgType = AmiGetByteFromLe(&pFrame->m_le_bMessageType);
		if (MsgType == kEplMsgTypePreq) {	// RPDO is PReq frame
			uiNodeId = EPL_PDO_PREQ_NODE_ID;	// 0x00
		} else {	// RPDO is PRes frame
			// retrieve node ID
			uiNodeId = AmiGetByteFromLe(&pFrame->m_le_bSrcNodeId);
		}

		// search for appropriate valid RPDO in OD
		wObdMappIndex = EPL_PDOK_OBD_IDX_RX_MAPP_PARAM;
		for (wObdCommIndex = EPL_PDOK_OBD_IDX_RX_COMM_PARAM;
		     wObdCommIndex < (EPL_PDOK_OBD_IDX_RX_COMM_PARAM + 0x00FF);
		     wObdCommIndex++, wObdMappIndex++) {
			ObdSize = 1;
			// read node ID from OD
			Ret =
			    EplObdReadEntry(wObdCommIndex, 0x01, &bObdData,
					    &ObdSize);
			if ((Ret == kEplObdIndexNotExist)
			    || (Ret == kEplObdSubindexNotExist)
			    || (Ret == kEplObdIllegalPart)) {	// PDO does not exist; last PDO reached
				Ret = kEplSuccessful;
				goto Exit;
			} else if (Ret != kEplSuccessful) {	// other fatal error occured
				goto Exit;
			}
			// entry read successfully
			if (bObdData != uiNodeId) {	// node ID does not equal - wrong PDO, try next PDO in OD
				continue;
			}
			ObdSize = 1;
			// read number of mapped objects from OD; this indicates if the PDO is valid
			Ret =
			    EplObdReadEntry(wObdMappIndex, 0x00, &bObjectCount,
					    &ObdSize);
			if ((Ret == kEplObdIndexNotExist)
			    || (Ret == kEplObdSubindexNotExist)
			    || (Ret == kEplObdIllegalPart)) {	// PDO does not exist; last PDO reached
				Ret = kEplSuccessful;
				goto Exit;
			} else if (Ret != kEplSuccessful) {	// other fatal error occured
				goto Exit;
			}
			// entry read successfully
			if (bObjectCount == 0) {	// PDO in OD not valid, try next PDO in OD
				continue;
			}

			ObdSize = 1;
			// check PDO mapping version
			Ret =
			    EplObdReadEntry(wObdCommIndex, 0x02, &bObdData,
					    &ObdSize);
			if (Ret != kEplSuccessful) {	// other fatal error occured
				goto Exit;
			}
			// entry read successfully
			// retrieve PDO version from frame
			bFrameData =
			    AmiGetByteFromLe(&pFrame->m_Data.m_Pres.
					     m_le_bPdoVersion);
			if ((bObdData & EPL_VERSION_MAIN) != (bFrameData & EPL_VERSION_MAIN)) {	// PDO versions do not match
				// $$$ raise PDO error
				// termiate processing of this RPDO
				goto Exit;
			}
			// valid RPDO found

			// retrieve PDO size
			wPdoSize =
			    AmiGetWordFromLe(&pFrame->m_Data.m_Pres.m_le_wSize);

			// process mapping
			for (bMappSubindex = 1; bMappSubindex <= bObjectCount;
			     bMappSubindex++) {
				ObdSize = 8;	// u64
				// read object mapping from OD
				Ret =
				    EplObdReadEntry(wObdMappIndex,
						    bMappSubindex,
						    &qwObjectMapping, &ObdSize);
				if (Ret != kEplSuccessful) {	// other fatal error occured
					goto Exit;
				}
				// check if object mapping entry is valid, i.e. unequal zero, because "empty" entries are allowed
				if (qwObjectMapping == 0) {	// invalid entry, continue with next entry
					continue;
				}
				// decode object mapping
				wObdCommIndex =
				    (u16) (qwObjectMapping &
					    0x000000000000FFFFLL);
				bObdSubindex =
				    (u8) ((qwObjectMapping &
					     0x0000000000FF0000LL) >> 16);
				wBitOffset =
				    (u16) ((qwObjectMapping &
					     0x0000FFFF00000000LL) >> 32);
				wBitSize =
				    (u16) ((qwObjectMapping &
					     0xFFFF000000000000LL) >> 48);

				// check if object exceeds PDO size
				if (((wBitOffset + wBitSize) >> 3) > wPdoSize) {	// wrong object mapping; PDO size is too low
					// $$$ raise PDO error
					// terminate processing of this RPDO
					goto Exit;
				}
				// copy object from RPDO to process/OD variable
				ObdSize = wBitSize >> 3;
				Ret =
				    EplObdWriteEntryFromLe(wObdCommIndex,
							   bObdSubindex,
							   &pFrame->m_Data.
							   m_Pres.
							   m_le_abPayload[(wBitOffset >> 3)], ObdSize);
				if (Ret != kEplSuccessful) {	// other fatal error occured
					goto Exit;
				}

			}

			// processing finished successfully
			goto Exit;
		}
		break;

	case kEplEventTypePdoTx:	// TPDO transmitted
		pFrameInfo = (tEplFrameInfo *) pEvent_p->m_pArg;
		pFrame = pFrameInfo->m_pFrame;

		// set TPDO invalid, so that only fully processed TPDOs are sent as valid
		bFrameData =
		    AmiGetByteFromLe(&pFrame->m_Data.m_Pres.m_le_bFlag1);
		AmiSetByteToLe(&pFrame->m_Data.m_Pres.m_le_bFlag1,
			       (bFrameData & ~EPL_FRAME_FLAG1_RD));

		// retrieve EPL message type
		MsgType = AmiGetByteFromLe(&pFrame->m_le_bMessageType);
		if (MsgType == kEplMsgTypePres) {	// TPDO is PRes frame
			uiNodeId = EPL_PDO_PRES_NODE_ID;	// 0x00
		} else {	// TPDO is PReq frame
			// retrieve node ID
			uiNodeId = AmiGetByteFromLe(&pFrame->m_le_bDstNodeId);
		}

		// search for appropriate valid TPDO in OD
		wObdMappIndex = EPL_PDOK_OBD_IDX_TX_MAPP_PARAM;
		wObdCommIndex = EPL_PDOK_OBD_IDX_TX_COMM_PARAM;
		for (wPdoId = 0;; wPdoId++, wObdCommIndex++, wObdMappIndex++) {
			ObdSize = 1;
			// read node ID from OD
			Ret =
			    EplObdReadEntry(wObdCommIndex, 0x01, &bObdData,
					    &ObdSize);
			if ((Ret == kEplObdIndexNotExist)
			    || (Ret == kEplObdSubindexNotExist)
			    || (Ret == kEplObdIllegalPart)) {	// PDO does not exist; last PDO reached
				Ret = kEplSuccessful;
				goto Exit;
			} else if (Ret != kEplSuccessful) {	// other fatal error occured
				goto Exit;
			}
			// entry read successfully
			if (bObdData != uiNodeId) {	// node ID does not equal - wrong PDO, try next PDO in OD
				continue;
			}
			ObdSize = 1;
			// read number of mapped objects from OD; this indicates if the PDO is valid
			Ret =
			    EplObdReadEntry(wObdMappIndex, 0x00, &bObjectCount,
					    &ObdSize);
			if ((Ret == kEplObdIndexNotExist)
			    || (Ret == kEplObdSubindexNotExist)
			    || (Ret == kEplObdIllegalPart)) {	// PDO does not exist; last PDO reached
				Ret = kEplSuccessful;
				goto Exit;
			} else if (Ret != kEplSuccessful) {	// other fatal error occured
				goto Exit;
			}
			// entry read successfully
			if (bObjectCount == 0) {	// PDO in OD not valid, try next PDO in OD
				continue;
			}
			// valid TPDO found

			ObdSize = 1;
			// get PDO mapping version from OD
			Ret =
			    EplObdReadEntry(wObdCommIndex, 0x02, &bObdData,
					    &ObdSize);
			if (Ret != kEplSuccessful) {	// other fatal error occured
				goto Exit;
			}
			// entry read successfully
			// set PDO version in frame
			AmiSetByteToLe(&pFrame->m_Data.m_Pres.m_le_bPdoVersion,
				       bObdData);

			// calculate PDO size
			wPdoSize = 0;

			// process mapping
			for (bMappSubindex = 1; bMappSubindex <= bObjectCount;
			     bMappSubindex++) {
				ObdSize = 8;	// u64
				// read object mapping from OD
				Ret =
				    EplObdReadEntry(wObdMappIndex,
						    bMappSubindex,
						    &qwObjectMapping, &ObdSize);
				if (Ret != kEplSuccessful) {	// other fatal error occured
					goto Exit;
				}
				// check if object mapping entry is valid, i.e. unequal zero, because "empty" entries are allowed
				if (qwObjectMapping == 0) {	// invalid entry, continue with next entry
					continue;
				}
				// decode object mapping
				wObdCommIndex =
				    (u16) (qwObjectMapping &
					    0x000000000000FFFFLL);
				bObdSubindex =
				    (u8) ((qwObjectMapping &
					     0x0000000000FF0000LL) >> 16);
				wBitOffset =
				    (u16) ((qwObjectMapping &
					     0x0000FFFF00000000LL) >> 32);
				wBitSize =
				    (u16) ((qwObjectMapping &
					     0xFFFF000000000000LL) >> 48);

				// calculate max PDO size
				ObdSize = wBitSize >> 3;
				wVarSize = (wBitOffset >> 3) + (u16) ObdSize;
				if ((unsigned int)(wVarSize + 24) > pFrameInfo->m_uiFrameSize) {	// TPDO is too short
					// $$$ raise PDO error, set Ret
					goto Exit;
				}
				if (wVarSize > wPdoSize) {	// memorize new PDO size
					wPdoSize = wVarSize;
				}
				// copy object from process/OD variable to TPDO
				Ret =
				    EplObdReadEntryToLe(wObdCommIndex,
							bObdSubindex,
							&pFrame->m_Data.m_Pres.
							m_le_abPayload[(wBitOffset >> 3)], &ObdSize);
				if (Ret != kEplSuccessful) {	// other fatal error occured
					goto Exit;
				}

			}

			// set PDO size in frame
			AmiSetWordToLe(&pFrame->m_Data.m_Pres.m_le_wSize,
				       wPdoSize);

			Ret = EplPdokCalAreTpdosValid(&fValid);
			if (fValid != FALSE) {
				// set TPDO valid
				bFrameData =
				    AmiGetByteFromLe(&pFrame->m_Data.m_Pres.
						     m_le_bFlag1);
				AmiSetByteToLe(&pFrame->m_Data.m_Pres.
					       m_le_bFlag1,
					       (bFrameData |
						EPL_FRAME_FLAG1_RD));
			}
			// processing finished successfully

			goto Exit;
		}
		break;

	case kEplEventTypePdoSoa:	// SoA received

		// invalidate TPDOs
		Ret = EplPdokCalSetTpdosValid(FALSE);
		break;

	default:
		{
			ASSERTMSG(FALSE,
				  "EplPdokProcess(): unhandled event type!\n");
		}
	}

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

#endif // #if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_PDOK)) != 0)

// EOF
