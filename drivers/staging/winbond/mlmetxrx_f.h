/* ================================================================
// MLMETxRx.H --
//
//   Functions defined in MLMETxRx.c.
//
// Copyright (c) 2002 Winbond Electrics Corp. All Rights Reserved.
//================================================================ */
#ifndef _MLMETXRX_H
#define _MLMETXRX_H

#include "core.h"

void MLME_GetNextPacket(struct wbsoft_priv *adapter, struct wb35_descriptor *pDes);

void
MLME_SendComplete(struct wbsoft_priv *adapter, u8 PacketID,
		  unsigned char SendOK);

#ifdef _IBSS_BEACON_SEQ_STICK_
s8 SendBCNullData(struct wbsoft_priv *adapter, u16 wIdx);
#endif

#endif
