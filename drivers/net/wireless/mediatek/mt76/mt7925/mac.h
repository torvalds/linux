/* SPDX-License-Identifier: ISC */
/* Copyright (C) 2023 MediaTek Inc. */

#ifndef __MT7925_MAC_H
#define __MT7925_MAC_H

#include "../mt76_connac3_mac.h"

#define MT_WTBL_TXRX_CAP_RATE_OFFSET	7
#define MT_WTBL_TXRX_RATE_G2_HE		24
#define MT_WTBL_TXRX_RATE_G2		12

#define MT_WTBL_AC0_CTT_OFFSET		20

static inline u32 mt7925_mac_wtbl_lmac_addr(struct mt792x_dev *dev, u16 wcid, u8 dw)
{
	mt76_wr(dev, MT_WTBLON_TOP_WDUCR,
		FIELD_PREP(MT_WTBLON_TOP_WDUCR_GROUP, (wcid >> 7)));

	return MT_WTBL_LMAC_OFFS(wcid, dw);
}

#endif
