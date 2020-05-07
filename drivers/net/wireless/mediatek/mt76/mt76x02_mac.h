/* SPDX-License-Identifier: ISC */
/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 * Copyright (C) 2018 Stanislaw Gruszka <stf_xl@wp.pl>
 */

#ifndef __MT76X02_MAC_H
#define __MT76X02_MAC_H

struct mt76x02_dev;

struct mt76x02_tx_status {
	u8 valid:1;
	u8 success:1;
	u8 aggr:1;
	u8 ack_req:1;
	u8 wcid;
	u8 pktid;
	u8 retry;
	u16 rate;
} __packed __aligned(2);

#define MT_VIF_WCID(_n)		(254 - ((_n) & 7))
#define MT_MAX_VIFS		8

#define MT_PKTID_RATE		GENMASK(4, 0)
#define MT_PKTID_AC		GENMASK(6, 5)

struct mt76x02_vif {
	struct mt76_wcid group_wcid; /* must be first */
	u8 idx;
};

DECLARE_EWMA(pktlen, 8, 8);

struct mt76x02_sta {
	struct mt76_wcid wcid; /* must be first */

	struct mt76x02_vif *vif;
	struct mt76x02_tx_status status;
	int n_frames;

	struct ewma_pktlen pktlen;
};

#define MT_RXINFO_BA			BIT(0)
#define MT_RXINFO_DATA			BIT(1)
#define MT_RXINFO_NULL			BIT(2)
#define MT_RXINFO_FRAG			BIT(3)
#define MT_RXINFO_UNICAST		BIT(4)
#define MT_RXINFO_MULTICAST		BIT(5)
#define MT_RXINFO_BROADCAST		BIT(6)
#define MT_RXINFO_MYBSS			BIT(7)
#define MT_RXINFO_CRCERR		BIT(8)
#define MT_RXINFO_ICVERR		BIT(9)
#define MT_RXINFO_MICERR		BIT(10)
#define MT_RXINFO_AMSDU			BIT(11)
#define MT_RXINFO_HTC			BIT(12)
#define MT_RXINFO_RSSI			BIT(13)
#define MT_RXINFO_L2PAD			BIT(14)
#define MT_RXINFO_AMPDU			BIT(15)
#define MT_RXINFO_DECRYPT		BIT(16)
#define MT_RXINFO_BSSIDX3		BIT(17)
#define MT_RXINFO_WAPI_KEY		BIT(18)
#define MT_RXINFO_PN_LEN		GENMASK(21, 19)
#define MT_RXINFO_SW_FTYPE0		BIT(22)
#define MT_RXINFO_SW_FTYPE1		BIT(23)
#define MT_RXINFO_PROBE_RESP		BIT(24)
#define MT_RXINFO_BEACON		BIT(25)
#define MT_RXINFO_DISASSOC		BIT(26)
#define MT_RXINFO_DEAUTH		BIT(27)
#define MT_RXINFO_ACTION		BIT(28)
#define MT_RXINFO_TCP_SUM_ERR		BIT(30)
#define MT_RXINFO_IP_SUM_ERR		BIT(31)

#define MT_RXWI_CTL_WCID		GENMASK(7, 0)
#define MT_RXWI_CTL_KEY_IDX		GENMASK(9, 8)
#define MT_RXWI_CTL_BSS_IDX		GENMASK(12, 10)
#define MT_RXWI_CTL_UDF			GENMASK(15, 13)
#define MT_RXWI_CTL_MPDU_LEN		GENMASK(29, 16)
#define MT_RXWI_CTL_EOF			BIT(31)

#define MT_RXWI_TID			GENMASK(3, 0)
#define MT_RXWI_SN			GENMASK(15, 4)

#define MT_RXWI_RATE_INDEX		GENMASK(5, 0)
#define MT_RXWI_RATE_LDPC		BIT(6)
#define MT_RXWI_RATE_BW			GENMASK(8, 7)
#define MT_RXWI_RATE_SGI		BIT(9)
#define MT_RXWI_RATE_STBC		BIT(10)
#define MT_RXWI_RATE_LDPC_EXSYM		BIT(11)
#define MT_RXWI_RATE_PHY		GENMASK(15, 13)

#define MT_RATE_INDEX_VHT_IDX		GENMASK(3, 0)
#define MT_RATE_INDEX_VHT_NSS		GENMASK(5, 4)

struct mt76x02_rxwi {
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

struct mt76x02_txwi {
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

static inline bool mt76x02_wait_for_mac(struct mt76_dev *dev)
{
	const u32 MAC_CSR0 = 0x1000;
	int i;

	for (i = 0; i < 500; i++) {
		if (test_bit(MT76_REMOVED, &dev->phy.state))
			return false;

		switch (dev->bus->rr(dev, MAC_CSR0)) {
		case 0:
		case ~0:
			break;
		default:
			return true;
		}
		usleep_range(5000, 10000);
	}
	return false;
}

void mt76x02_mac_reset_counters(struct mt76x02_dev *dev);
void mt76x02_mac_set_short_preamble(struct mt76x02_dev *dev, bool enable);
int mt76x02_mac_shared_key_setup(struct mt76x02_dev *dev, u8 vif_idx,
				 u8 key_idx, struct ieee80211_key_conf *key);
int mt76x02_mac_wcid_set_key(struct mt76x02_dev *dev, u8 idx,
			     struct ieee80211_key_conf *key);
void mt76x02_mac_wcid_sync_pn(struct mt76x02_dev *dev, u8 idx,
			      struct ieee80211_key_conf *key);
void mt76x02_mac_wcid_setup(struct mt76x02_dev *dev, u8 idx, u8 vif_idx,
			    u8 *mac);
void mt76x02_mac_wcid_set_drop(struct mt76x02_dev *dev, u8 idx, bool drop);
void mt76x02_mac_wcid_set_rate(struct mt76x02_dev *dev, struct mt76_wcid *wcid,
			       const struct ieee80211_tx_rate *rate);
bool mt76x02_mac_load_tx_status(struct mt76x02_dev *dev,
				struct mt76x02_tx_status *stat);
void mt76x02_send_tx_status(struct mt76x02_dev *dev,
			    struct mt76x02_tx_status *stat, u8 *update);
int mt76x02_mac_process_rx(struct mt76x02_dev *dev, struct sk_buff *skb,
			   void *rxi);
void mt76x02_mac_set_tx_protection(struct mt76x02_dev *dev, bool legacy_prot,
				   int ht_mode);
void mt76x02_mac_set_rts_thresh(struct mt76x02_dev *dev, u32 val);
void mt76x02_mac_setaddr(struct mt76x02_dev *dev, const u8 *addr);
void mt76x02_mac_write_txwi(struct mt76x02_dev *dev, struct mt76x02_txwi *txwi,
			    struct sk_buff *skb, struct mt76_wcid *wcid,
			    struct ieee80211_sta *sta, int len);
void mt76x02_mac_poll_tx_status(struct mt76x02_dev *dev, bool irq);
void mt76x02_tx_complete_skb(struct mt76_dev *mdev, enum mt76_txq_id qid,
			     struct mt76_queue_entry *e);
void mt76x02_update_channel(struct mt76_dev *mdev);
void mt76x02_mac_work(struct work_struct *work);

void mt76x02_mac_cc_reset(struct mt76x02_dev *dev);
void mt76x02_mac_set_bssid(struct mt76x02_dev *dev, u8 idx, const u8 *addr);
void mt76x02_mac_set_beacon(struct mt76x02_dev *dev, struct sk_buff *skb);
void mt76x02_mac_set_beacon_enable(struct mt76x02_dev *dev,
				   struct ieee80211_vif *vif, bool enable);

void mt76x02_edcca_init(struct mt76x02_dev *dev);
#endif
