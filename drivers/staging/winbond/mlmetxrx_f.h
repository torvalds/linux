//================================================================
// MLMETxRx.H --
//
//   Functions defined in MLMETxRx.c.
//
// Copyright (c) 2002 Winbond Electrics Corp. All Rights Reserved.
//================================================================
#ifndef _MLMETXRX_H
#define _MLMETXRX_H

void
MLMEProcThread(
     PWB32_ADAPTER    Adapter
	);

void MLMEResetTxRx( PWB32_ADAPTER Adapter);

u8 *
MLMEGetMMPDUBuffer(
     PWB32_ADAPTER    Adapter
   );

void MLMEfreeMMPDUBuffer( PWB32_ADAPTER Adapter,  PCHAR pData);

void MLME_GetNextPacket(  PADAPTER Adapter,  PDESCRIPTOR pDes );
u8 MLMESendFrame( PWB32_ADAPTER Adapter,
					u8	*pMMPDU,
					u16	len,
					 u8	DataType);

void
MLME_SendComplete(  PWB32_ADAPTER Adapter,  u8 PacketID,  unsigned char SendOK );

void
MLMERcvFrame(
     PWB32_ADAPTER    Adapter,
     PRXBUFFER        pRxBufferArray,
     u8            NumOfBuffer,
     u8            ReturnSlotIndex
	);

void
MLMEReturnPacket(
     PWB32_ADAPTER    Adapter,
     PUCHAR           pRxBufer
   );
#ifdef _IBSS_BEACON_SEQ_STICK_
s8 SendBCNullData(PWB32_ADAPTER Adapter, u16 wIdx);
#endif

#endif

