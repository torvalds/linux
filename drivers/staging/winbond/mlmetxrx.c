//============================================================================
//  Module Name:
//    MLMETxRx.C
//
//  Description:
//    The interface between MDS (MAC Data Service) and MLME.
//
//  Revision History:
//  --------------------------------------------------------------------------
//          200209      UN20 Jennifer Xu
//                      Initial Release
//          20021108    PD43 Austin Liu
//          20030117    PD43 Austin Liu
//                      Deleted MLMEReturnPacket and MLMEProcThread()
//
//  Copyright (c) 1996-2002 Winbond Electronics Corp. All Rights Reserved.
//============================================================================
#include "os_common.h"

void MLMEResetTxRx(struct wb35_adapter * adapter)
{
	s32     i;

	// Reset the interface between MDS and MLME
	for (i = 0; i < MAX_NUM_TX_MMPDU; i++)
		adapter->sMlmeFrame.TxMMPDUInUse[i] = false;
	for (i = 0; i < MAX_NUM_RX_MMPDU; i++)
		adapter->sMlmeFrame.SaveRxBufSlotInUse[i] = false;

	adapter->sMlmeFrame.wNumRxMMPDUInMLME   = 0;
	adapter->sMlmeFrame.wNumRxMMPDUDiscarded = 0;
	adapter->sMlmeFrame.wNumRxMMPDU          = 0;
	adapter->sMlmeFrame.wNumTxMMPDUDiscarded = 0;
	adapter->sMlmeFrame.wNumTxMMPDU          = 0;
	adapter->sLocalPara.boCCAbusy    = false;
	adapter->sLocalPara.iPowerSaveMode     = PWR_ACTIVE;     // Power active
}

//=============================================================================
//	Function:
//    MLMEGetMMPDUBuffer()
//
//	Description:
//    Return the pointer to an available data buffer with
//    the size MAX_MMPDU_SIZE for a MMPDU.
//
//  Arguments:
//    adapter   - pointer to the miniport adapter context.
//
//	Return value:
//    NULL     : No available data buffer available
//    Otherwise: Pointer to the data buffer
//=============================================================================

/* FIXME: Should this just be replaced with kmalloc() and kfree()? */
u8 *MLMEGetMMPDUBuffer(struct wb35_adapter * adapter)
{
	s32 i;
	u8 *returnVal;

	for (i = 0; i< MAX_NUM_TX_MMPDU; i++) {
		if (adapter->sMlmeFrame.TxMMPDUInUse[i] == false)
			break;
	}
	if (i >= MAX_NUM_TX_MMPDU) return NULL;

	returnVal = (u8 *)&(adapter->sMlmeFrame.TxMMPDU[i]);
	adapter->sMlmeFrame.TxMMPDUInUse[i] = true;

	return returnVal;
}

//=============================================================================
u8 MLMESendFrame(struct wb35_adapter * adapter, u8 *pMMPDU, u16 len, u8 DataType)
/*	DataType : FRAME_TYPE_802_11_MANAGEMENT, FRAME_TYPE_802_11_MANAGEMENT_CHALLENGE,
				FRAME_TYPE_802_11_DATA */
{
	if (adapter->sMlmeFrame.IsInUsed != PACKET_FREE_TO_USE) {
		adapter->sMlmeFrame.wNumTxMMPDUDiscarded++;
		return false;
	}
	adapter->sMlmeFrame.IsInUsed = PACKET_COME_FROM_MLME;

	// Keep information for sending
	adapter->sMlmeFrame.pMMPDU = pMMPDU;
	adapter->sMlmeFrame.DataType = DataType;
	// len must be the last setting due to QUERY_SIZE_SECOND of Mds
	adapter->sMlmeFrame.len = len;
	adapter->sMlmeFrame.wNumTxMMPDU++;

	// H/W will enter power save by set the register. S/W don't send null frame
	//with PWRMgt bit enbled to enter power save now.

	// Transmit NDIS packet
	Mds_Tx(adapter);
	return true;
}

void MLME_GetNextPacket(struct wb35_adapter *adapter, PDESCRIPTOR desc)
{
	desc->InternalUsed = desc->buffer_start_index + desc->buffer_number;
	desc->InternalUsed %= MAX_DESCRIPTOR_BUFFER_INDEX;
	desc->buffer_address[desc->InternalUsed] = adapter->sMlmeFrame.pMMPDU;
	desc->buffer_size[desc->InternalUsed] = adapter->sMlmeFrame.len;
	desc->buffer_total_size += adapter->sMlmeFrame.len;
	desc->buffer_number++;
	desc->Type = adapter->sMlmeFrame.DataType;
}

void MLMEfreeMMPDUBuffer(struct wb35_adapter * adapter, s8 *pData)
{
	int i;

	// Reclaim the data buffer
	for (i = 0; i < MAX_NUM_TX_MMPDU; i++) {
		if (pData == (s8 *)&(adapter->sMlmeFrame.TxMMPDU[i]))
			break;
	}
	if (adapter->sMlmeFrame.TxMMPDUInUse[i])
		adapter->sMlmeFrame.TxMMPDUInUse[i] = false;
	else  {
		// Something wrong
		// PD43 Add debug code here???
	}
}

void
MLME_SendComplete(struct wb35_adapter * adapter, u8 PacketID, unsigned char SendOK)
{
	MLME_TXCALLBACK	TxCallback;

    // Reclaim the data buffer
	adapter->sMlmeFrame.len = 0;
	MLMEfreeMMPDUBuffer( adapter, adapter->sMlmeFrame.pMMPDU );


	TxCallback.bResult = MLME_SUCCESS;

	// Return resource
	adapter->sMlmeFrame.IsInUsed = PACKET_FREE_TO_USE;
}



