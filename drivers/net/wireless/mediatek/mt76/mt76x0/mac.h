/*
 * Copyright (C) 2014 Felix Fietkau <nbd@openwrt.org>
 * Copyright (C) 2015 Jakub Kicinski <kubakici@wp.pl>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __MT76_MAC_H
#define __MT76_MAC_H

/* Note: values in original "RSSI" and "SNR" fields are not actually what they
 *	 are called for MT76X0U, names used by this driver are educated guesses
 *	 (see vendor mac/ral_omac.c).
 */
struct mt76x0_rxwi {
	__le32 rxinfo;

	__le32 ctl;

	__le16 tid_sn;
	__le16 rate;

	s8 rssi[4];

	__le32 bbp_rxinfo[4];
} __packed __aligned(4);

enum mt76_phy_bandwidth {
	MT_PHY_BW_20,
	MT_PHY_BW_40,
	MT_PHY_BW_80,
};

struct mt76_txwi {
	__le16 flags;
	__le16 rate_ctl;
	u8 ack_ctl;
	u8 wcid;
	__le16 len_ctl;
	__le32 iv;
	__le32 eiv;
	u8 aid;
	u8 txstream;
	u8 ctl2;
	u8 pktid;
} __packed __aligned(4);

#define MT_TXWI_FLAGS_FRAG		BIT(0)
#define MT_TXWI_FLAGS_MMPS		BIT(1)
#define MT_TXWI_FLAGS_CFACK		BIT(2)
#define MT_TXWI_FLAGS_TS		BIT(3)
#define MT_TXWI_FLAGS_AMPDU		BIT(4)
#define MT_TXWI_FLAGS_MPDU_DENSITY	GENMASK(7, 5)
#define MT_TXWI_FLAGS_TXOP		GENMASK(9, 8)
#define MT_TXWI_FLAGS_CWMIN		GENMASK(12, 10)
#define MT_TXWI_FLAGS_NO_RATE_FALLBACK	BIT(13)
#define MT_TXWI_FLAGS_TX_RPT		BIT(14)
#define MT_TXWI_FLAGS_TX_RATE_LUT	BIT(15)

#define MT_TXWI_RATE_MCS		GENMASK(6, 0)
#define MT_TXWI_RATE_BW			BIT(7)
#define MT_TXWI_RATE_SGI		BIT(8)
#define MT_TXWI_RATE_STBC		GENMASK(10, 9)
#define MT_TXWI_RATE_PHY_MODE		GENMASK(15, 14)

#define MT_TXWI_ACK_CTL_REQ		BIT(0)
#define MT_TXWI_ACK_CTL_NSEQ		BIT(1)
#define MT_TXWI_ACK_CTL_BA_WINDOW	GENMASK(7, 2)

#define MT_TXWI_LEN_BYTE_CNT		GENMASK(11, 0)

#define MT_TXWI_CTL_TX_POWER_ADJ	GENMASK(3, 0)
#define MT_TXWI_CTL_CHAN_CHECK_PKT	BIT(4)
#define MT_TXWI_CTL_PIFS_REV		BIT(6)

#define MT_TXWI_PKTID_PROBE             BIT(7)

u32 mt76x0_mac_process_rx(struct mt76x0_dev *dev, struct sk_buff *skb,
			u8 *data, void *rxi);
struct mt76x02_tx_status
mt76x0_mac_fetch_tx_status(struct mt76x0_dev *dev);
void mt76x0_send_tx_status(struct mt76x0_dev *dev, struct mt76x02_tx_status *stat, u8 *update);

#endif
