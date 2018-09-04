/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __MT76x2_MAC_H
#define __MT76x2_MAC_H

#include "mt76.h"
#include "mt76x02_mac.h"

struct mt76x2_dev;
struct mt76x2_sta;
struct mt76x02_vif;
struct mt76x2_txwi;

struct mt76x2_tx_info {
	unsigned long jiffies;
	u8 tries;

	u8 wcid;
	u8 pktid;
	u8 retry;
};

struct mt76x2_rxwi {
	__le32 rxinfo;

	__le32 ctl;

	__le16 tid_sn;
	__le16 rate;

	u8 rssi[4];

	__le32 bbp_rxinfo[4];
};

#define MT_TX_PWR_ADJ			GENMASK(3, 0)

enum mt76x2_phy_bandwidth {
	MT_PHY_BW_20,
	MT_PHY_BW_40,
	MT_PHY_BW_80,
};

#define MT_TXWI_FLAGS_FRAG		BIT(0)
#define MT_TXWI_FLAGS_MMPS		BIT(1)
#define MT_TXWI_FLAGS_CFACK		BIT(2)
#define MT_TXWI_FLAGS_TS		BIT(3)
#define MT_TXWI_FLAGS_AMPDU		BIT(4)
#define MT_TXWI_FLAGS_MPDU_DENSITY	GENMASK(7, 5)
#define MT_TXWI_FLAGS_TXOP		GENMASK(9, 8)
#define MT_TXWI_FLAGS_NDPS		BIT(10)
#define MT_TXWI_FLAGS_RTSBWSIG		BIT(11)
#define MT_TXWI_FLAGS_NDP_BW		GENMASK(13, 12)
#define MT_TXWI_FLAGS_SOUND		BIT(14)
#define MT_TXWI_FLAGS_TX_RATE_LUT	BIT(15)

#define MT_TXWI_ACK_CTL_REQ		BIT(0)
#define MT_TXWI_ACK_CTL_NSEQ		BIT(1)
#define MT_TXWI_ACK_CTL_BA_WINDOW	GENMASK(7, 2)

#define MT_TXWI_PKTID_PROBE		BIT(7)

struct mt76x2_txwi {
	__le16 flags;
	__le16 rate;
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

static inline struct mt76x2_tx_info *
mt76x2_skb_tx_info(struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	return (void *) info->status.status_driver_data;
}

int mt76x2_mac_start(struct mt76x2_dev *dev);
void mt76x2_mac_stop(struct mt76x2_dev *dev, bool force);
void mt76x2_mac_resume(struct mt76x2_dev *dev);
void mt76x2_mac_set_bssid(struct mt76x2_dev *dev, u8 idx, const u8 *addr);

int mt76x2_mac_process_rx(struct mt76x2_dev *dev, struct sk_buff *skb,
			  void *rxi);
void mt76x2_mac_write_txwi(struct mt76x2_dev *dev, struct mt76x2_txwi *txwi,
			   struct sk_buff *skb, struct mt76_wcid *wcid,
			   struct ieee80211_sta *sta, int len);

int mt76x2_mac_set_beacon(struct mt76x2_dev *dev, u8 vif_idx,
			  struct sk_buff *skb);
void mt76x2_mac_set_beacon_enable(struct mt76x2_dev *dev, u8 vif_idx, bool val);

void mt76x2_mac_poll_tx_status(struct mt76x2_dev *dev, bool irq);
void mt76x2_mac_process_tx_status_fifo(struct mt76x2_dev *dev);

void mt76x2_mac_work(struct work_struct *work);

#endif
