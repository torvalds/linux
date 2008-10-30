//================================================================
// MLMETxRx.H --
//
//   Functions defined in MLMETxRx.c.
//
// Copyright (c) 2002 Winbond Electrics Corp. All Rights Reserved.
//================================================================
#ifndef _MLMETXRX_H
#define _MLMETXRX_H

#include "adapter.h"

void
MLMEProcThread(
     struct wb35_adapter *    adapter
	);

void MLMEResetTxRx( struct wb35_adapter * adapter);

u8 *
MLMEGetMMPDUBuffer(
     struct wb35_adapter *    adapter
   );

void MLMEfreeMMPDUBuffer( struct wb35_adapter * adapter,  s8 * pData);

void MLME_GetNextPacket(  struct wb35_adapter * adapter,  PDESCRIPTOR pDes );
u8 MLMESendFrame( struct wb35_adapter * adapter,
					u8	*pMMPDU,
					u16	len,
					 u8	DataType);

void
MLME_SendComplete(  struct wb35_adapter * adapter,  u8 PacketID,  unsigned char SendOK );

void
MLMERcvFrame(
     struct wb35_adapter *    adapter,
     PRXBUFFER        pRxBufferArray,
     u8            NumOfBuffer,
     u8            ReturnSlotIndex
	);

void
MLMEReturnPacket(
     struct wb35_adapter *    adapter,
     u8 *          pRxBufer
   );
#ifdef _IBSS_BEACON_SEQ_STICK_
s8 SendBCNullData(struct wb35_adapter * adapter, u16 wIdx);
#endif

#endif

