/* SPDX-License-Identifier: ISC */
/* Copyright (C) 2020 MediaTek Inc. */

#ifndef __MT7921_MAC_H
#define __MT7921_MAC_H

#include "../mt76_connac2_mac.h"

#define MT_CT_PARSE_LEN			72
#define MT_CT_DMA_BUF_NUM		2

#define MT_RXD0_LENGTH			GENMASK(15, 0)
#define MT_RXD0_PKT_FLAG                GENMASK(19, 16)
#define MT_RXD0_PKT_TYPE		GENMASK(31, 27)

#define MT_RXD0_NORMAL_ETH_TYPE_OFS	GENMASK(22, 16)
#define MT_RXD0_NORMAL_IP_SUM		BIT(23)
#define MT_RXD0_NORMAL_UDP_TCP_SUM	BIT(24)

enum rx_pkt_type {
	PKT_TYPE_TXS,
	PKT_TYPE_TXRXV,
	PKT_TYPE_NORMAL,
	PKT_TYPE_RX_DUP_RFB,
	PKT_TYPE_RX_TMR,
	PKT_TYPE_RETRIEVE,
	PKT_TYPE_TXRX_NOTIFY,
	PKT_TYPE_RX_EVENT,
	PKT_TYPE_NORMAL_MCU,
};

#define MT_TX_FREE_MSDU_CNT		GENMASK(9, 0)
#define MT_TX_FREE_WLAN_ID		GENMASK(23, 14)
#define MT_TX_FREE_LATENCY		GENMASK(12, 0)
/* 0: success, others: dropped */
#define MT_TX_FREE_STATUS		GENMASK(14, 13)
#define MT_TX_FREE_MSDU_ID		GENMASK(30, 16)
#define MT_TX_FREE_PAIR			BIT(31)
/* will support this field in further revision */
#define MT_TX_FREE_RATE			GENMASK(13, 0)

#define MT_WTBL_TXRX_CAP_RATE_OFFSET	7
#define MT_WTBL_TXRX_RATE_G2_HE		24
#define MT_WTBL_TXRX_RATE_G2		12

#define MT_WTBL_AC0_CTT_OFFSET		20

static inline u32 mt7921_mac_wtbl_lmac_addr(int idx, u8 offset)
{
	return MT_WTBL_LMAC_OFFS(idx, 0) + offset * 4;
}

#endif
