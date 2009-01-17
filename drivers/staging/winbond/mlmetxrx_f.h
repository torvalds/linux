//================================================================
// MLMETxRx.H --
//
//   Functions defined in MLMETxRx.c.
//
// Copyright (c) 2002 Winbond Electrics Corp. All Rights Reserved.
//================================================================
#ifndef _MLMETXRX_H
#define _MLMETXRX_H

#include "core.h"

void
MLMEProcThread(
     struct wbsoft_priv *    adapter
	);

void MLME_GetNextPacket(  struct wbsoft_priv * adapter,  PDESCRIPTOR pDes );
u8 MLMESendFrame( struct wbsoft_priv * adapter,
					u8	*pMMPDU,
					u16	len,
					 u8	DataType);

void
MLME_SendComplete(  struct wbsoft_priv * adapter,  u8 PacketID,  unsigned char SendOK );

void
MLMERcvFrame(
     struct wbsoft_priv *    adapter,
     PRXBUFFER        pRxBufferArray,
     u8            NumOfBuffer,
     u8            ReturnSlotIndex
	);

void
MLMEReturnPacket(
     struct wbsoft_priv *    adapter,
     u8 *          pRxBufer
   );
#ifdef _IBSS_BEACON_SEQ_STICK_
s8 SendBCNullData(struct wbsoft_priv * adapter, u16 wIdx);
#endif

#endif

