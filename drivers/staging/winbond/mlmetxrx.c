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

void MLMEResetTxRx(PWB32_ADAPTER Adapter)
{
	s32     i;

	// Reset the interface between MDS and MLME
	for (i = 0; i < MAX_NUM_TX_MMPDU; i++)
		Adapter->sMlmeFrame.TxMMPDUInUse[i] = FALSE;
	for (i = 0; i < MAX_NUM_RX_MMPDU; i++)
		Adapter->sMlmeFrame.SaveRxBufSlotInUse[i] = FALSE;

	Adapter->sMlmeFrame.wNumRxMMPDUInMLME   = 0;
	Adapter->sMlmeFrame.wNumRxMMPDUDiscarded = 0;
	Adapter->sMlmeFrame.wNumRxMMPDU          = 0;
	Adapter->sMlmeFrame.wNumTxMMPDUDiscarded = 0;
	Adapter->sMlmeFrame.wNumTxMMPDU          = 0;
	Adapter->sLocalPara.boCCAbusy    = FALSE;
	Adapter->sLocalPara.iPowerSaveMode     = PWR_ACTIVE;     // Power active
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
//    Adapter   - pointer to the miniport adapter context.
//
//	Return value:
//    NULL     : No available data buffer available
//    Otherwise: Pointer to the data buffer
//=============================================================================

/* FIXME: Should this just be replaced with kmalloc() and kfree()? */
u8 *MLMEGetMMPDUBuffer(PWB32_ADAPTER Adapter)
{
	s32 i;
	u8 *returnVal;

	for (i = 0; i< MAX_NUM_TX_MMPDU; i++) {
		if (Adapter->sMlmeFrame.TxMMPDUInUse[i] == FALSE)
			break;
	}
	if (i >= MAX_NUM_TX_MMPDU) return NULL;

	returnVal = (u8 *)&(Adapter->sMlmeFrame.TxMMPDU[i]);
	Adapter->sMlmeFrame.TxMMPDUInUse[i] = TRUE;

	return returnVal;
}

//=============================================================================
u8 MLMESendFrame(PWB32_ADAPTER Adapter, u8 *pMMPDU, u16 len, u8 DataType)
/*	DataType : FRAME_TYPE_802_11_MANAGEMENT, FRAME_TYPE_802_11_MANAGEMENT_CHALLENGE,
				FRAME_TYPE_802_11_DATA */
{
	if (Adapter->sMlmeFrame.IsInUsed != PACKET_FREE_TO_USE) {
		Adapter->sMlmeFrame.wNumTxMMPDUDiscarded++;
		return FALSE;
	}
	Adapter->sMlmeFrame.IsInUsed = PACKET_COME_FROM_MLME;

	// Keep information for sending
	Adapter->sMlmeFrame.pMMPDU = pMMPDU;
	Adapter->sMlmeFrame.DataType = DataType;
	// len must be the last setting due to QUERY_SIZE_SECOND of Mds
	Adapter->sMlmeFrame.len = len;
	Adapter->sMlmeFrame.wNumTxMMPDU++;

	// H/W will enter power save by set the register. S/W don't send null frame
	//with PWRMgt bit enbled to enter power save now.

	// Transmit NDIS packet
	Mds_Tx(Adapter);
	return TRUE;
}

void
MLME_GetNextPacket(PADAPTER Adapter, PDESCRIPTOR pDes)
{
#define DESCRIPTOR_ADD_BUFFER( _D, _A, _S ) \
{\
	_D->InternalUsed = _D->buffer_start_index + _D->buffer_number; \
	_D->InternalUsed %= MAX_DESCRIPTOR_BUFFER_INDEX; \
	_D->buffer_address[ _D->InternalUsed ] = _A; \
	_D->buffer_size[ _D->InternalUsed ] = _S; \
	_D->buffer_total_size += _S; \
	_D->buffer_number++;\
}

	DESCRIPTOR_ADD_BUFFER( pDes, Adapter->sMlmeFrame.pMMPDU, Adapter->sMlmeFrame.len );
	pDes->Type = Adapter->sMlmeFrame.DataType;
}

void MLMEfreeMMPDUBuffer(PWB32_ADAPTER Adapter, PCHAR pData)
{
	int i;

	// Reclaim the data buffer
	for (i = 0; i < MAX_NUM_TX_MMPDU; i++) {
		if (pData == (PCHAR)&(Adapter->sMlmeFrame.TxMMPDU[i]))
			break;
	}
	if (Adapter->sMlmeFrame.TxMMPDUInUse[i])
		Adapter->sMlmeFrame.TxMMPDUInUse[i] = FALSE;
	else  {
		// Something wrong
		// PD43 Add debug code here???
	}
}

void
MLME_SendComplete(PADAPTER Adapter, u8 PacketID, unsigned char SendOK)
{
	MLME_TXCALLBACK	TxCallback;

    // Reclaim the data buffer
	Adapter->sMlmeFrame.len = 0;
	MLMEfreeMMPDUBuffer( Adapter, Adapter->sMlmeFrame.pMMPDU );


	TxCallback.bResult = MLME_SUCCESS;

	// Return resource
	Adapter->sMlmeFrame.IsInUsed = PACKET_FREE_TO_USE;
}



