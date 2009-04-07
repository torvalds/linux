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
#include "sysdef.h"

#include "mds_f.h"

//=============================================================================
u8 MLMESendFrame(struct wbsoft_priv * adapter, u8 *pMMPDU, u16 len, u8 DataType)
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

void MLME_GetNextPacket(struct wbsoft_priv *adapter, PDESCRIPTOR desc)
{
	desc->InternalUsed = desc->buffer_start_index + desc->buffer_number;
	desc->InternalUsed %= MAX_DESCRIPTOR_BUFFER_INDEX;
	desc->buffer_address[desc->InternalUsed] = adapter->sMlmeFrame.pMMPDU;
	desc->buffer_size[desc->InternalUsed] = adapter->sMlmeFrame.len;
	desc->buffer_total_size += adapter->sMlmeFrame.len;
	desc->buffer_number++;
	desc->Type = adapter->sMlmeFrame.DataType;
}

static void MLMEfreeMMPDUBuffer(struct wbsoft_priv *adapter, s8 *pData)
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
MLME_SendComplete(struct wbsoft_priv * adapter, u8 PacketID, unsigned char SendOK)
{
	MLME_TXCALLBACK	TxCallback;

    // Reclaim the data buffer
	adapter->sMlmeFrame.len = 0;
	MLMEfreeMMPDUBuffer( adapter, adapter->sMlmeFrame.pMMPDU );


	TxCallback.bResult = MLME_SUCCESS;

	// Return resource
	adapter->sMlmeFrame.IsInUsed = PACKET_FREE_TO_USE;
}



