// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc. */

#include <linux/etherdevice.h>
#include <linux/timekeeping.h>
#include "coredump.h"
#include "mt7915.h"
#include "../dma.h"
#include "mac.h"
#include "mcu.h"

#define to_rssi(field, rcpi)	((FIELD_GET(field, rcpi) - 220) / 2)

static const struct mt7915_dfs_radar_spec etsi_radar_specs = {
	.pulse_th = { 110, -10, -80, 40, 5200, 128, 5200 },
	.radar_pattern = {
		[5] =  { 1, 0,  6, 32, 28, 0,  990, 5010, 17, 1, 1 },
		[6] =  { 1, 0,  9, 32, 28, 0,  615, 5010, 27, 1, 1 },
		[7] =  { 1, 0, 15, 32, 28, 0,  240,  445, 27, 1, 1 },
		[8] =  { 1, 0, 12, 32, 28, 0,  240,  510, 42, 1, 1 },
		[9] =  { 1, 1,  0,  0,  0, 0, 2490, 3343, 14, 0, 0, 12, 32, 28, { }, 126 },
		[10] = { 1, 1,  0,  0,  0, 0, 2490, 3343, 14, 0, 0, 15, 32, 24, { }, 126 },
		[11] = { 1, 1,  0,  0,  0, 0,  823, 2510, 14, 0, 0, 18, 32, 28, { },  54 },
		[12] = { 1, 1,  0,  0,  0, 0,  823, 2510, 14, 0, 0, 27, 32, 24, { },  54 },
	},
};

static const struct mt7915_dfs_radar_spec fcc_radar_specs = {
	.pulse_th = { 110, -10, -80, 40, 5200, 128, 5200 },
	.radar_pattern = {
		[0] = { 1, 0,  8,  32, 28, 0, 508, 3076, 13, 1,  1 },
		[1] = { 1, 0, 12,  32, 28, 0, 140,  240, 17, 1,  1 },
		[2] = { 1, 0,  8,  32, 28, 0, 190,  510, 22, 1,  1 },
		[3] = { 1, 0,  6,  32, 28, 0, 190,  510, 32, 1,  1 },
		[4] = { 1, 0,  9, 255, 28, 0, 323,  343, 13, 1, 32 },
	},
};

static const struct mt7915_dfs_radar_spec jp_radar_specs = {
	.pulse_th = { 110, -10, -80, 40, 5200, 128, 5200 },
	.radar_pattern = {
		[0] =  { 1, 0,  8,  32, 28, 0,  508, 3076,  13, 1,  1 },
		[1] =  { 1, 0, 12,  32, 28, 0,  140,  240,  17, 1,  1 },
		[2] =  { 1, 0,  8,  32, 28, 0,  190,  510,  22, 1,  1 },
		[3] =  { 1, 0,  6,  32, 28, 0,  190,  510,  32, 1,  1 },
		[4] =  { 1, 0,  9, 255, 28, 0,  323,  343,  13, 1, 32 },
		[13] = { 1, 0,  7,  32, 28, 0, 3836, 3856,  14, 1,  1 },
		[14] = { 1, 0,  6,  32, 28, 0,  615, 5010, 110, 1,  1 },
		[15] = { 1, 1,  0,   0,  0, 0,   15, 5010, 110, 0,  0, 12, 32, 28 },
	},
};

static struct mt76_wcid *mt7915_rx_get_wcid(struct mt7915_dev *dev,
					    u16 idx, bool unicast)
{
	struct mt7915_sta *sta;
	struct mt76_wcid *wcid;

	if (idx >= ARRAY_SIZE(dev->mt76.wcid))
		return NULL;

	wcid = rcu_dereference(dev->mt76.wcid[idx]);
	if (unicast || !wcid)
		return wcid;

	if (!wcid->sta)
		return NULL;

	sta = container_of(wcid, struct mt7915_sta, wcid);
	if (!sta->vif)
		return NULL;

	return &sta->vif->sta.wcid;
}

void mt7915_sta_ps(struct mt76_dev *mdev, struct ieee80211_sta *sta, bool ps)
{
}

bool mt7915_mac_wtbl_update(struct mt7915_dev *dev, int idx, u32 mask)
{
	mt76_rmw(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_WLAN_IDX,
		 FIELD_PREP(MT_WTBL_UPDATE_WLAN_IDX, idx) | mask);

	return mt76_poll(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_BUSY,
			 0, 5000);
}

u32 mt7915_mac_wtbl_lmac_addr(struct mt7915_dev *dev, u16 wcid, u8 dw)
{
	mt76_wr(dev, MT_WTBLON_TOP_WDUCR,
		FIELD_PREP(MT_WTBLON_TOP_WDUCR_GROUP, (wcid >> 7)));

	return MT_WTBL_LMAC_OFFS(wcid, dw);
}

static void mt7915_mac_sta_poll(struct mt7915_dev *dev)
{
	static const u8 ac_to_tid[] = {
		[IEEE80211_AC_BE] = 0,
		[IEEE80211_AC_BK] = 1,
		[IEEE80211_AC_VI] = 4,
		[IEEE80211_AC_VO] = 6
	};
	struct ieee80211_sta *sta;
	struct mt7915_sta *msta;
	struct rate_info *rate;
	u32 tx_time[IEEE80211_NUM_ACS], rx_time[IEEE80211_NUM_ACS];
	LIST_HEAD(sta_poll_list);
	int i;

	spin_lock_bh(&dev->sta_poll_lock);
	list_splice_init(&dev->sta_poll_list, &sta_poll_list);
	spin_unlock_bh(&dev->sta_poll_lock);

	rcu_read_lock();

	while (true) {
		bool clear = false;
		u32 addr, val;
		u16 idx;
		s8 rssi[4];
		u8 bw;

		spin_lock_bh(&dev->sta_poll_lock);
		if (list_empty(&sta_poll_list)) {
			spin_unlock_bh(&dev->sta_poll_lock);
			break;
		}
		msta = list_first_entry(&sta_poll_list,
					struct mt7915_sta, poll_list);
		list_del_init(&msta->poll_list);
		spin_unlock_bh(&dev->sta_poll_lock);

		idx = msta->wcid.idx;

		/* refresh peer's airtime reporting */
		addr = mt7915_mac_wtbl_lmac_addr(dev, idx, 20);

		for (i = 0; i < IEEE80211_NUM_ACS; i++) {
			u32 tx_last = msta->airtime_ac[i];
			u32 rx_last = msta->airtime_ac[i + 4];

			msta->airtime_ac[i] = mt76_rr(dev, addr);
			msta->airtime_ac[i + 4] = mt76_rr(dev, addr + 4);

			tx_time[i] = msta->airtime_ac[i] - tx_last;
			rx_time[i] = msta->airtime_ac[i + 4] - rx_last;

			if ((tx_last | rx_last) & BIT(30))
				clear = true;

			addr += 8;
		}

		if (clear) {
			mt7915_mac_wtbl_update(dev, idx,
					       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);
			memset(msta->airtime_ac, 0, sizeof(msta->airtime_ac));
		}

		if (!msta->wcid.sta)
			continue;

		sta = container_of((void *)msta, struct ieee80211_sta,
				   drv_priv);
		for (i = 0; i < IEEE80211_NUM_ACS; i++) {
			u8 queue = mt76_connac_lmac_mapping(i);
			u32 tx_cur = tx_time[queue];
			u32 rx_cur = rx_time[queue];
			u8 tid = ac_to_tid[i];

			if (!tx_cur && !rx_cur)
				continue;

			ieee80211_sta_register_airtime(sta, tid, tx_cur,
						       rx_cur);
		}

		/*
		 * We don't support reading GI info from txs packets.
		 * For accurate tx status reporting and AQL improvement,
		 * we need to make sure that flags match so polling GI
		 * from per-sta counters directly.
		 */
		rate = &msta->wcid.rate;
		addr = mt7915_mac_wtbl_lmac_addr(dev, idx, 7);
		val = mt76_rr(dev, addr);

		switch (rate->bw) {
		case RATE_INFO_BW_160:
			bw = IEEE80211_STA_RX_BW_160;
			break;
		case RATE_INFO_BW_80:
			bw = IEEE80211_STA_RX_BW_80;
			break;
		case RATE_INFO_BW_40:
			bw = IEEE80211_STA_RX_BW_40;
			break;
		default:
			bw = IEEE80211_STA_RX_BW_20;
			break;
		}

		if (rate->flags & RATE_INFO_FLAGS_HE_MCS) {
			u8 offs = 24 + 2 * bw;

			rate->he_gi = (val & (0x3 << offs)) >> offs;
		} else if (rate->flags &
			   (RATE_INFO_FLAGS_VHT_MCS | RATE_INFO_FLAGS_MCS)) {
			if (val & BIT(12 + bw))
				rate->flags |= RATE_INFO_FLAGS_SHORT_GI;
			else
				rate->flags &= ~RATE_INFO_FLAGS_SHORT_GI;
		}

		/* get signal strength of resp frames (CTS/BA/ACK) */
		addr = mt7915_mac_wtbl_lmac_addr(dev, idx, 30);
		val = mt76_rr(dev, addr);

		rssi[0] = to_rssi(GENMASK(7, 0), val);
		rssi[1] = to_rssi(GENMASK(15, 8), val);
		rssi[2] = to_rssi(GENMASK(23, 16), val);
		rssi[3] = to_rssi(GENMASK(31, 14), val);

		msta->ack_signal =
			mt76_rx_signal(msta->vif->phy->mt76->antenna_mask, rssi);

		ewma_avg_signal_add(&msta->avg_ack_signal, -msta->ack_signal);
	}

	rcu_read_unlock();
}

void mt7915_mac_enable_rtscts(struct mt7915_dev *dev,
			      struct ieee80211_vif *vif, bool enable)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	u32 addr;

	addr = mt7915_mac_wtbl_lmac_addr(dev, mvif->sta.wcid.idx, 5);
	if (enable)
		mt76_set(dev, addr, BIT(5));
	else
		mt76_clear(dev, addr, BIT(5));
}

static void
mt7915_wed_check_ppe(struct mt7915_dev *dev, struct mt76_queue *q,
		     struct mt7915_sta *msta, struct sk_buff *skb,
		     u32 info)
{
	struct ieee80211_vif *vif;
	struct wireless_dev *wdev;

	if (!msta || !msta->vif)
		return;

	if (!mt76_queue_is_wed_rx(q))
		return;

	if (!(info & MT_DMA_INFO_PPE_VLD))
		return;

	vif = container_of((void *)msta->vif, struct ieee80211_vif,
			   drv_priv);
	wdev = ieee80211_vif_to_wdev(vif);
	skb->dev = wdev->netdev;

	mtk_wed_device_ppe_check(&dev->mt76.mmio.wed, skb,
				 FIELD_GET(MT_DMA_PPE_CPU_REASON, info),
				 FIELD_GET(MT_DMA_PPE_ENTRY, info));
}

static int
mt7915_mac_fill_rx(struct mt7915_dev *dev, struct sk_buff *skb,
		   enum mt76_rxq_id q, u32 *info)
{
	struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt7915_phy *phy = &dev->phy;
	struct ieee80211_supported_band *sband;
	__le32 *rxd = (__le32 *)skb->data;
	__le32 *rxv = NULL;
	u32 rxd0 = le32_to_cpu(rxd[0]);
	u32 rxd1 = le32_to_cpu(rxd[1]);
	u32 rxd2 = le32_to_cpu(rxd[2]);
	u32 rxd3 = le32_to_cpu(rxd[3]);
	u32 rxd4 = le32_to_cpu(rxd[4]);
	u32 csum_mask = MT_RXD0_NORMAL_IP_SUM | MT_RXD0_NORMAL_UDP_TCP_SUM;
	bool unicast, insert_ccmp_hdr = false;
	u8 remove_pad, amsdu_info;
	u8 mode = 0, qos_ctl = 0;
	struct mt7915_sta *msta = NULL;
	u32 csum_status = *(u32 *)skb->cb;
	bool hdr_trans;
	u16 hdr_gap;
	u16 seq_ctrl = 0;
	__le16 fc = 0;
	int idx;

	memset(status, 0, sizeof(*status));

	if ((rxd1 & MT_RXD1_NORMAL_BAND_IDX) && !phy->mt76->band_idx) {
		mphy = dev->mt76.phys[MT_BAND1];
		if (!mphy)
			return -EINVAL;

		phy = mphy->priv;
		status->phy_idx = 1;
	}

	if (!test_bit(MT76_STATE_RUNNING, &mphy->state))
		return -EINVAL;

	if (rxd2 & MT_RXD2_NORMAL_AMSDU_ERR)
		return -EINVAL;

	hdr_trans = rxd2 & MT_RXD2_NORMAL_HDR_TRANS;
	if (hdr_trans && (rxd1 & MT_RXD1_NORMAL_CM))
		return -EINVAL;

	/* ICV error or CCMP/BIP/WPI MIC error */
	if (rxd1 & MT_RXD1_NORMAL_ICV_ERR)
		status->flag |= RX_FLAG_ONLY_MONITOR;

	unicast = FIELD_GET(MT_RXD3_NORMAL_ADDR_TYPE, rxd3) == MT_RXD3_NORMAL_U2M;
	idx = FIELD_GET(MT_RXD1_NORMAL_WLAN_IDX, rxd1);
	status->wcid = mt7915_rx_get_wcid(dev, idx, unicast);

	if (status->wcid) {
		msta = container_of(status->wcid, struct mt7915_sta, wcid);
		spin_lock_bh(&dev->sta_poll_lock);
		if (list_empty(&msta->poll_list))
			list_add_tail(&msta->poll_list, &dev->sta_poll_list);
		spin_unlock_bh(&dev->sta_poll_lock);
	}

	status->freq = mphy->chandef.chan->center_freq;
	status->band = mphy->chandef.chan->band;
	if (status->band == NL80211_BAND_5GHZ)
		sband = &mphy->sband_5g.sband;
	else if (status->band == NL80211_BAND_6GHZ)
		sband = &mphy->sband_6g.sband;
	else
		sband = &mphy->sband_2g.sband;

	if (!sband->channels)
		return -EINVAL;

	if ((rxd0 & csum_mask) == csum_mask &&
	    !(csum_status & (BIT(0) | BIT(2) | BIT(3))))
		skb->ip_summed = CHECKSUM_UNNECESSARY;

	if (rxd1 & MT_RXD1_NORMAL_FCS_ERR)
		status->flag |= RX_FLAG_FAILED_FCS_CRC;

	if (rxd1 & MT_RXD1_NORMAL_TKIP_MIC_ERR)
		status->flag |= RX_FLAG_MMIC_ERROR;

	if (FIELD_GET(MT_RXD1_NORMAL_SEC_MODE, rxd1) != 0 &&
	    !(rxd1 & (MT_RXD1_NORMAL_CLM | MT_RXD1_NORMAL_CM))) {
		status->flag |= RX_FLAG_DECRYPTED;
		status->flag |= RX_FLAG_IV_STRIPPED;
		status->flag |= RX_FLAG_MMIC_STRIPPED | RX_FLAG_MIC_STRIPPED;
	}

	remove_pad = FIELD_GET(MT_RXD2_NORMAL_HDR_OFFSET, rxd2);

	if (rxd2 & MT_RXD2_NORMAL_MAX_LEN_ERROR)
		return -EINVAL;

	rxd += 6;
	if (rxd1 & MT_RXD1_NORMAL_GROUP_4) {
		u32 v0 = le32_to_cpu(rxd[0]);
		u32 v2 = le32_to_cpu(rxd[2]);

		fc = cpu_to_le16(FIELD_GET(MT_RXD6_FRAME_CONTROL, v0));
		qos_ctl = FIELD_GET(MT_RXD8_QOS_CTL, v2);
		seq_ctrl = FIELD_GET(MT_RXD8_SEQ_CTRL, v2);

		rxd += 4;
		if ((u8 *)rxd - skb->data >= skb->len)
			return -EINVAL;
	}

	if (rxd1 & MT_RXD1_NORMAL_GROUP_1) {
		u8 *data = (u8 *)rxd;

		if (status->flag & RX_FLAG_DECRYPTED) {
			switch (FIELD_GET(MT_RXD1_NORMAL_SEC_MODE, rxd1)) {
			case MT_CIPHER_AES_CCMP:
			case MT_CIPHER_CCMP_CCX:
			case MT_CIPHER_CCMP_256:
				insert_ccmp_hdr =
					FIELD_GET(MT_RXD2_NORMAL_FRAG, rxd2);
				fallthrough;
			case MT_CIPHER_TKIP:
			case MT_CIPHER_TKIP_NO_MIC:
			case MT_CIPHER_GCMP:
			case MT_CIPHER_GCMP_256:
				status->iv[0] = data[5];
				status->iv[1] = data[4];
				status->iv[2] = data[3];
				status->iv[3] = data[2];
				status->iv[4] = data[1];
				status->iv[5] = data[0];
				break;
			default:
				break;
			}
		}
		rxd += 4;
		if ((u8 *)rxd - skb->data >= skb->len)
			return -EINVAL;
	}

	if (rxd1 & MT_RXD1_NORMAL_GROUP_2) {
		status->timestamp = le32_to_cpu(rxd[0]);
		status->flag |= RX_FLAG_MACTIME_START;

		if (!(rxd2 & MT_RXD2_NORMAL_NON_AMPDU)) {
			status->flag |= RX_FLAG_AMPDU_DETAILS;

			/* all subframes of an A-MPDU have the same timestamp */
			if (phy->rx_ampdu_ts != status->timestamp) {
				if (!++phy->ampdu_ref)
					phy->ampdu_ref++;
			}
			phy->rx_ampdu_ts = status->timestamp;

			status->ampdu_ref = phy->ampdu_ref;
		}

		rxd += 2;
		if ((u8 *)rxd - skb->data >= skb->len)
			return -EINVAL;
	}

	/* RXD Group 3 - P-RXV */
	if (rxd1 & MT_RXD1_NORMAL_GROUP_3) {
		u32 v0, v1;
		int ret;

		rxv = rxd;
		rxd += 2;
		if ((u8 *)rxd - skb->data >= skb->len)
			return -EINVAL;

		v0 = le32_to_cpu(rxv[0]);
		v1 = le32_to_cpu(rxv[1]);

		if (v0 & MT_PRXV_HT_AD_CODE)
			status->enc_flags |= RX_ENC_FLAG_LDPC;

		status->chains = mphy->antenna_mask;
		status->chain_signal[0] = to_rssi(MT_PRXV_RCPI0, v1);
		status->chain_signal[1] = to_rssi(MT_PRXV_RCPI1, v1);
		status->chain_signal[2] = to_rssi(MT_PRXV_RCPI2, v1);
		status->chain_signal[3] = to_rssi(MT_PRXV_RCPI3, v1);

		/* RXD Group 5 - C-RXV */
		if (rxd1 & MT_RXD1_NORMAL_GROUP_5) {
			rxd += 18;
			if ((u8 *)rxd - skb->data >= skb->len)
				return -EINVAL;
		}

		if (!is_mt7915(&dev->mt76) || (rxd1 & MT_RXD1_NORMAL_GROUP_5)) {
			ret = mt76_connac2_mac_fill_rx_rate(&dev->mt76, status,
							    sband, rxv, &mode);
			if (ret < 0)
				return ret;
		}
	}

	amsdu_info = FIELD_GET(MT_RXD4_NORMAL_PAYLOAD_FORMAT, rxd4);
	status->amsdu = !!amsdu_info;
	if (status->amsdu) {
		status->first_amsdu = amsdu_info == MT_RXD4_FIRST_AMSDU_FRAME;
		status->last_amsdu = amsdu_info == MT_RXD4_LAST_AMSDU_FRAME;
	}

	hdr_gap = (u8 *)rxd - skb->data + 2 * remove_pad;
	if (hdr_trans && ieee80211_has_morefrags(fc)) {
		struct ieee80211_vif *vif;
		int err;

		if (!msta || !msta->vif)
			return -EINVAL;

		vif = container_of((void *)msta->vif, struct ieee80211_vif,
				   drv_priv);
		err = mt76_connac2_reverse_frag0_hdr_trans(vif, skb, hdr_gap);
		if (err)
			return err;

		hdr_trans = false;
	} else {
		int pad_start = 0;

		skb_pull(skb, hdr_gap);
		if (!hdr_trans && status->amsdu) {
			pad_start = ieee80211_get_hdrlen_from_skb(skb);
		} else if (hdr_trans && (rxd2 & MT_RXD2_NORMAL_HDR_TRANS_ERROR)) {
			/*
			 * When header translation failure is indicated,
			 * the hardware will insert an extra 2-byte field
			 * containing the data length after the protocol
			 * type field. This happens either when the LLC-SNAP
			 * pattern did not match, or if a VLAN header was
			 * detected.
			 */
			pad_start = 12;
			if (get_unaligned_be16(skb->data + pad_start) == ETH_P_8021Q)
				pad_start += 4;
			else
				pad_start = 0;
		}

		if (pad_start) {
			memmove(skb->data + 2, skb->data, pad_start);
			skb_pull(skb, 2);
		}
	}

	if (!hdr_trans) {
		struct ieee80211_hdr *hdr;

		if (insert_ccmp_hdr) {
			u8 key_id = FIELD_GET(MT_RXD1_NORMAL_KEY_ID, rxd1);

			mt76_insert_ccmp_hdr(skb, key_id);
		}

		hdr = mt76_skb_get_hdr(skb);
		fc = hdr->frame_control;
		if (ieee80211_is_data_qos(fc)) {
			seq_ctrl = le16_to_cpu(hdr->seq_ctrl);
			qos_ctl = *ieee80211_get_qos_ctl(hdr);
		}
	} else {
		status->flag |= RX_FLAG_8023;
		mt7915_wed_check_ppe(dev, &dev->mt76.q_rx[q], msta, skb,
				     *info);
	}

	if (rxv && mode >= MT_PHY_TYPE_HE_SU && !(status->flag & RX_FLAG_8023))
		mt76_connac2_mac_decode_he_radiotap(&dev->mt76, skb, rxv, mode);

	if (!status->wcid || !ieee80211_is_data_qos(fc))
		return 0;

	status->aggr = unicast &&
		       !ieee80211_is_qos_nullfunc(fc);
	status->qos_ctl = qos_ctl;
	status->seqno = IEEE80211_SEQ_TO_SN(seq_ctrl);

	return 0;
}

static void
mt7915_mac_fill_rx_vector(struct mt7915_dev *dev, struct sk_buff *skb)
{
#ifdef CONFIG_NL80211_TESTMODE
	struct mt7915_phy *phy = &dev->phy;
	__le32 *rxd = (__le32 *)skb->data;
	__le32 *rxv_hdr = rxd + 2;
	__le32 *rxv = rxd + 4;
	u32 rcpi, ib_rssi, wb_rssi, v20, v21;
	u8 band_idx;
	s32 foe;
	u8 snr;
	int i;

	band_idx = le32_get_bits(rxv_hdr[1], MT_RXV_HDR_BAND_IDX);
	if (band_idx && !phy->mt76->band_idx) {
		phy = mt7915_ext_phy(dev);
		if (!phy)
			goto out;
	}

	rcpi = le32_to_cpu(rxv[6]);
	ib_rssi = le32_to_cpu(rxv[7]);
	wb_rssi = le32_to_cpu(rxv[8]) >> 5;

	for (i = 0; i < 4; i++, rcpi >>= 8, ib_rssi >>= 8, wb_rssi >>= 9) {
		if (i == 3)
			wb_rssi = le32_to_cpu(rxv[9]);

		phy->test.last_rcpi[i] = rcpi & 0xff;
		phy->test.last_ib_rssi[i] = ib_rssi & 0xff;
		phy->test.last_wb_rssi[i] = wb_rssi & 0xff;
	}

	v20 = le32_to_cpu(rxv[20]);
	v21 = le32_to_cpu(rxv[21]);

	foe = FIELD_GET(MT_CRXV_FOE_LO, v20) |
	      (FIELD_GET(MT_CRXV_FOE_HI, v21) << MT_CRXV_FOE_SHIFT);

	snr = FIELD_GET(MT_CRXV_SNR, v20) - 16;

	phy->test.last_freq_offset = foe;
	phy->test.last_snr = snr;
out:
#endif
	dev_kfree_skb(skb);
}

static void
mt7915_mac_write_txwi_tm(struct mt7915_phy *phy, __le32 *txwi,
			 struct sk_buff *skb)
{
#ifdef CONFIG_NL80211_TESTMODE
	struct mt76_testmode_data *td = &phy->mt76->test;
	const struct ieee80211_rate *r;
	u8 bw, mode, nss = td->tx_rate_nss;
	u8 rate_idx = td->tx_rate_idx;
	u16 rateval = 0;
	u32 val;
	bool cck = false;
	int band;

	if (skb != phy->mt76->test.tx_skb)
		return;

	switch (td->tx_rate_mode) {
	case MT76_TM_TX_MODE_HT:
		nss = 1 + (rate_idx >> 3);
		mode = MT_PHY_TYPE_HT;
		break;
	case MT76_TM_TX_MODE_VHT:
		mode = MT_PHY_TYPE_VHT;
		break;
	case MT76_TM_TX_MODE_HE_SU:
		mode = MT_PHY_TYPE_HE_SU;
		break;
	case MT76_TM_TX_MODE_HE_EXT_SU:
		mode = MT_PHY_TYPE_HE_EXT_SU;
		break;
	case MT76_TM_TX_MODE_HE_TB:
		mode = MT_PHY_TYPE_HE_TB;
		break;
	case MT76_TM_TX_MODE_HE_MU:
		mode = MT_PHY_TYPE_HE_MU;
		break;
	case MT76_TM_TX_MODE_CCK:
		cck = true;
		fallthrough;
	case MT76_TM_TX_MODE_OFDM:
		band = phy->mt76->chandef.chan->band;
		if (band == NL80211_BAND_2GHZ && !cck)
			rate_idx += 4;

		r = &phy->mt76->hw->wiphy->bands[band]->bitrates[rate_idx];
		val = cck ? r->hw_value_short : r->hw_value;

		mode = val >> 8;
		rate_idx = val & 0xff;
		break;
	default:
		mode = MT_PHY_TYPE_OFDM;
		break;
	}

	switch (phy->mt76->chandef.width) {
	case NL80211_CHAN_WIDTH_40:
		bw = 1;
		break;
	case NL80211_CHAN_WIDTH_80:
		bw = 2;
		break;
	case NL80211_CHAN_WIDTH_80P80:
	case NL80211_CHAN_WIDTH_160:
		bw = 3;
		break;
	default:
		bw = 0;
		break;
	}

	if (td->tx_rate_stbc && nss == 1) {
		nss++;
		rateval |= MT_TX_RATE_STBC;
	}

	rateval |= FIELD_PREP(MT_TX_RATE_IDX, rate_idx) |
		   FIELD_PREP(MT_TX_RATE_MODE, mode) |
		   FIELD_PREP(MT_TX_RATE_NSS, nss - 1);

	txwi[2] |= cpu_to_le32(MT_TXD2_FIX_RATE);

	le32p_replace_bits(&txwi[3], 1, MT_TXD3_REM_TX_COUNT);
	if (td->tx_rate_mode < MT76_TM_TX_MODE_HT)
		txwi[3] |= cpu_to_le32(MT_TXD3_BA_DISABLE);

	val = MT_TXD6_FIXED_BW |
	      FIELD_PREP(MT_TXD6_BW, bw) |
	      FIELD_PREP(MT_TXD6_TX_RATE, rateval) |
	      FIELD_PREP(MT_TXD6_SGI, td->tx_rate_sgi);

	/* for HE_SU/HE_EXT_SU PPDU
	 * - 1x, 2x, 4x LTF + 0.8us GI
	 * - 2x LTF + 1.6us GI, 4x LTF + 3.2us GI
	 * for HE_MU PPDU
	 * - 2x, 4x LTF + 0.8us GI
	 * - 2x LTF + 1.6us GI, 4x LTF + 3.2us GI
	 * for HE_TB PPDU
	 * - 1x, 2x LTF + 1.6us GI
	 * - 4x LTF + 3.2us GI
	 */
	if (mode >= MT_PHY_TYPE_HE_SU)
		val |= FIELD_PREP(MT_TXD6_HELTF, td->tx_ltf);

	if (td->tx_rate_ldpc || (bw > 0 && mode >= MT_PHY_TYPE_HE_SU))
		val |= MT_TXD6_LDPC;

	txwi[3] &= ~cpu_to_le32(MT_TXD3_SN_VALID);
	txwi[6] |= cpu_to_le32(val);
	txwi[7] |= cpu_to_le32(FIELD_PREP(MT_TXD7_SPE_IDX,
					  phy->test.spe_idx));
#endif
}

void mt7915_mac_write_txwi(struct mt76_dev *dev, __le32 *txwi,
			   struct sk_buff *skb, struct mt76_wcid *wcid, int pid,
			   struct ieee80211_key_conf *key,
			   enum mt76_txq_id qid, u32 changed)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	u8 phy_idx = (info->hw_queue & MT_TX_HW_QUEUE_PHY) >> 2;
	struct mt76_phy *mphy = &dev->phy;

	if (phy_idx && dev->phys[MT_BAND1])
		mphy = dev->phys[MT_BAND1];

	mt76_connac2_mac_write_txwi(dev, txwi, skb, wcid, key, pid, qid, changed);

	if (mt76_testmode_enabled(mphy))
		mt7915_mac_write_txwi_tm(mphy->priv, txwi, skb);
}

int mt7915_tx_prepare_skb(struct mt76_dev *mdev, void *txwi_ptr,
			  enum mt76_txq_id qid, struct mt76_wcid *wcid,
			  struct ieee80211_sta *sta,
			  struct mt76_tx_info *tx_info)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)tx_info->skb->data;
	struct mt7915_dev *dev = container_of(mdev, struct mt7915_dev, mt76);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx_info->skb);
	struct ieee80211_key_conf *key = info->control.hw_key;
	struct ieee80211_vif *vif = info->control.vif;
	struct mt76_connac_fw_txp *txp;
	struct mt76_txwi_cache *t;
	int id, i, nbuf = tx_info->nbuf - 1;
	u8 *txwi = (u8 *)txwi_ptr;
	int pid;

	if (unlikely(tx_info->skb->len <= ETH_HLEN))
		return -EINVAL;

	if (!wcid)
		wcid = &dev->mt76.global_wcid;

	if (sta) {
		struct mt7915_sta *msta;

		msta = (struct mt7915_sta *)sta->drv_priv;

		if (time_after(jiffies, msta->jiffies + HZ / 4)) {
			info->flags |= IEEE80211_TX_CTL_REQ_TX_STATUS;
			msta->jiffies = jiffies;
		}
	}

	t = (struct mt76_txwi_cache *)(txwi + mdev->drv->txwi_size);
	t->skb = tx_info->skb;

	id = mt76_token_consume(mdev, &t);
	if (id < 0)
		return id;

	pid = mt76_tx_status_skb_add(mdev, wcid, tx_info->skb);
	mt7915_mac_write_txwi(mdev, txwi_ptr, tx_info->skb, wcid, pid, key,
			      qid, 0);

	txp = (struct mt76_connac_fw_txp *)(txwi + MT_TXD_SIZE);
	for (i = 0; i < nbuf; i++) {
		txp->buf[i] = cpu_to_le32(tx_info->buf[i + 1].addr);
		txp->len[i] = cpu_to_le16(tx_info->buf[i + 1].len);
	}
	txp->nbuf = nbuf;

	txp->flags = cpu_to_le16(MT_CT_INFO_APPLY_TXD | MT_CT_INFO_FROM_HOST);

	if (!key)
		txp->flags |= cpu_to_le16(MT_CT_INFO_NONE_CIPHER_FRAME);

	if (!(info->flags & IEEE80211_TX_CTL_HW_80211_ENCAP) &&
	    ieee80211_is_mgmt(hdr->frame_control))
		txp->flags |= cpu_to_le16(MT_CT_INFO_MGMT_FRAME);

	if (vif) {
		struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;

		txp->bss_idx = mvif->mt76.idx;
	}

	txp->token = cpu_to_le16(id);
	if (test_bit(MT_WCID_FLAG_4ADDR, &wcid->flags))
		txp->rept_wds_wcid = cpu_to_le16(wcid->idx);
	else
		txp->rept_wds_wcid = cpu_to_le16(0x3ff);
	tx_info->skb = DMA_DUMMY_DATA;

	/* pass partial skb header to fw */
	tx_info->buf[1].len = MT_CT_PARSE_LEN;
	tx_info->buf[1].skip_unmap = true;
	tx_info->nbuf = MT_CT_DMA_BUF_NUM;

	return 0;
}

u32 mt7915_wed_init_buf(void *ptr, dma_addr_t phys, int token_id)
{
	struct mt76_connac_fw_txp *txp = ptr + MT_TXD_SIZE;
	__le32 *txwi = ptr;
	u32 val;

	memset(ptr, 0, MT_TXD_SIZE + sizeof(*txp));

	val = FIELD_PREP(MT_TXD0_TX_BYTES, MT_TXD_SIZE) |
	      FIELD_PREP(MT_TXD0_PKT_FMT, MT_TX_TYPE_CT);
	txwi[0] = cpu_to_le32(val);

	val = MT_TXD1_LONG_FORMAT |
	      FIELD_PREP(MT_TXD1_HDR_FORMAT, MT_HDR_FORMAT_802_3);
	txwi[1] = cpu_to_le32(val);

	txp->token = cpu_to_le16(token_id);
	txp->nbuf = 1;
	txp->buf[0] = cpu_to_le32(phys + MT_TXD_SIZE + sizeof(*txp));

	return MT_TXD_SIZE + sizeof(*txp);
}

static void
mt7915_tx_check_aggr(struct ieee80211_sta *sta, __le32 *txwi)
{
	struct mt7915_sta *msta;
	u16 fc, tid;
	u32 val;

	if (!sta || !(sta->deflink.ht_cap.ht_supported || sta->deflink.he_cap.has_he))
		return;

	tid = le32_get_bits(txwi[1], MT_TXD1_TID);
	if (tid >= 6) /* skip VO queue */
		return;

	val = le32_to_cpu(txwi[2]);
	fc = FIELD_GET(MT_TXD2_FRAME_TYPE, val) << 2 |
	     FIELD_GET(MT_TXD2_SUB_TYPE, val) << 4;
	if (unlikely(fc != (IEEE80211_FTYPE_DATA | IEEE80211_STYPE_QOS_DATA)))
		return;

	msta = (struct mt7915_sta *)sta->drv_priv;
	if (!test_and_set_bit(tid, &msta->ampdu_state))
		ieee80211_start_tx_ba_session(sta, tid, 0);
}

static void
mt7915_txwi_free(struct mt7915_dev *dev, struct mt76_txwi_cache *t,
		 struct ieee80211_sta *sta, struct list_head *free_list)
{
	struct mt76_dev *mdev = &dev->mt76;
	struct mt7915_sta *msta;
	struct mt76_wcid *wcid;
	__le32 *txwi;
	u16 wcid_idx;

	mt76_connac_txp_skb_unmap(mdev, t);
	if (!t->skb)
		goto out;

	txwi = (__le32 *)mt76_get_txwi_ptr(mdev, t);
	if (sta) {
		wcid = (struct mt76_wcid *)sta->drv_priv;
		wcid_idx = wcid->idx;
	} else {
		wcid_idx = le32_get_bits(txwi[1], MT_TXD1_WLAN_IDX);
		wcid = rcu_dereference(dev->mt76.wcid[wcid_idx]);

		if (wcid && wcid->sta) {
			msta = container_of(wcid, struct mt7915_sta, wcid);
			sta = container_of((void *)msta, struct ieee80211_sta,
					  drv_priv);
			spin_lock_bh(&dev->sta_poll_lock);
			if (list_empty(&msta->poll_list))
				list_add_tail(&msta->poll_list, &dev->sta_poll_list);
			spin_unlock_bh(&dev->sta_poll_lock);
		}
	}

	if (sta && likely(t->skb->protocol != cpu_to_be16(ETH_P_PAE)))
		mt7915_tx_check_aggr(sta, txwi);

	__mt76_tx_complete_skb(mdev, wcid_idx, t->skb, free_list);

out:
	t->skb = NULL;
	mt76_put_txwi(mdev, t);
}

static void
mt7915_mac_tx_free_prepare(struct mt7915_dev *dev)
{
	struct mt76_dev *mdev = &dev->mt76;
	struct mt76_phy *mphy_ext = mdev->phys[MT_BAND1];

	/* clean DMA queues and unmap buffers first */
	mt76_queue_tx_cleanup(dev, dev->mphy.q_tx[MT_TXQ_PSD], false);
	mt76_queue_tx_cleanup(dev, dev->mphy.q_tx[MT_TXQ_BE], false);
	if (mphy_ext) {
		mt76_queue_tx_cleanup(dev, mphy_ext->q_tx[MT_TXQ_PSD], false);
		mt76_queue_tx_cleanup(dev, mphy_ext->q_tx[MT_TXQ_BE], false);
	}
}

static void
mt7915_mac_tx_free_done(struct mt7915_dev *dev,
			struct list_head *free_list, bool wake)
{
	struct sk_buff *skb, *tmp;

	mt7915_mac_sta_poll(dev);

	if (wake)
		mt76_set_tx_blocked(&dev->mt76, false);

	mt76_worker_schedule(&dev->mt76.tx_worker);

	list_for_each_entry_safe(skb, tmp, free_list, list) {
		skb_list_del_init(skb);
		napi_consume_skb(skb, 1);
	}
}

static void
mt7915_mac_tx_free(struct mt7915_dev *dev, void *data, int len)
{
	struct mt76_connac_tx_free *free = data;
	__le32 *tx_info = (__le32 *)(data + sizeof(*free));
	struct mt76_dev *mdev = &dev->mt76;
	struct mt76_txwi_cache *txwi;
	struct ieee80211_sta *sta = NULL;
	LIST_HEAD(free_list);
	void *end = data + len;
	bool v3, wake = false;
	u16 total, count = 0;
	u32 txd = le32_to_cpu(free->txd);
	__le32 *cur_info;

	mt7915_mac_tx_free_prepare(dev);

	total = le16_get_bits(free->ctrl, MT_TX_FREE_MSDU_CNT);
	v3 = (FIELD_GET(MT_TX_FREE_VER, txd) == 0x4);

	for (cur_info = tx_info; count < total; cur_info++) {
		u32 msdu, info;
		u8 i;

		if (WARN_ON_ONCE((void *)cur_info >= end))
			return;

		/*
		 * 1'b1: new wcid pair.
		 * 1'b0: msdu_id with the same 'wcid pair' as above.
		 */
		info = le32_to_cpu(*cur_info);
		if (info & MT_TX_FREE_PAIR) {
			struct mt7915_sta *msta;
			struct mt76_wcid *wcid;
			u16 idx;

			idx = FIELD_GET(MT_TX_FREE_WLAN_ID, info);
			wcid = rcu_dereference(dev->mt76.wcid[idx]);
			sta = wcid_to_sta(wcid);
			if (!sta)
				continue;

			msta = container_of(wcid, struct mt7915_sta, wcid);
			spin_lock_bh(&dev->sta_poll_lock);
			if (list_empty(&msta->poll_list))
				list_add_tail(&msta->poll_list, &dev->sta_poll_list);
			spin_unlock_bh(&dev->sta_poll_lock);
			continue;
		}

		if (v3 && (info & MT_TX_FREE_MPDU_HEADER))
			continue;

		for (i = 0; i < 1 + v3; i++) {
			if (v3) {
				msdu = (info >> (15 * i)) & MT_TX_FREE_MSDU_ID_V3;
				if (msdu == MT_TX_FREE_MSDU_ID_V3)
					continue;
			} else {
				msdu = FIELD_GET(MT_TX_FREE_MSDU_ID, info);
			}
			count++;
			txwi = mt76_token_release(mdev, msdu, &wake);
			if (!txwi)
				continue;

			mt7915_txwi_free(dev, txwi, sta, &free_list);
		}
	}

	mt7915_mac_tx_free_done(dev, &free_list, wake);
}

static void
mt7915_mac_tx_free_v0(struct mt7915_dev *dev, void *data, int len)
{
	struct mt76_connac_tx_free *free = data;
	__le16 *info = (__le16 *)(data + sizeof(*free));
	struct mt76_dev *mdev = &dev->mt76;
	void *end = data + len;
	LIST_HEAD(free_list);
	bool wake = false;
	u8 i, count;

	mt7915_mac_tx_free_prepare(dev);

	count = FIELD_GET(MT_TX_FREE_MSDU_CNT_V0, le16_to_cpu(free->ctrl));
	if (WARN_ON_ONCE((void *)&info[count] > end))
		return;

	for (i = 0; i < count; i++) {
		struct mt76_txwi_cache *txwi;
		u16 msdu = le16_to_cpu(info[i]);

		txwi = mt76_token_release(mdev, msdu, &wake);
		if (!txwi)
			continue;

		mt7915_txwi_free(dev, txwi, NULL, &free_list);
	}

	mt7915_mac_tx_free_done(dev, &free_list, wake);
}

static void mt7915_mac_add_txs(struct mt7915_dev *dev, void *data)
{
	struct mt7915_sta *msta = NULL;
	struct mt76_wcid *wcid;
	__le32 *txs_data = data;
	u16 wcidx;
	u8 pid;

	wcidx = le32_get_bits(txs_data[2], MT_TXS2_WCID);
	pid = le32_get_bits(txs_data[3], MT_TXS3_PID);

	if (pid < MT_PACKET_ID_WED)
		return;

	if (wcidx >= mt7915_wtbl_size(dev))
		return;

	rcu_read_lock();

	wcid = rcu_dereference(dev->mt76.wcid[wcidx]);
	if (!wcid)
		goto out;

	msta = container_of(wcid, struct mt7915_sta, wcid);

	if (pid == MT_PACKET_ID_WED)
		mt76_connac2_mac_fill_txs(&dev->mt76, wcid, txs_data);
	else
		mt76_connac2_mac_add_txs_skb(&dev->mt76, wcid, pid, txs_data);

	if (!wcid->sta)
		goto out;

	spin_lock_bh(&dev->sta_poll_lock);
	if (list_empty(&msta->poll_list))
		list_add_tail(&msta->poll_list, &dev->sta_poll_list);
	spin_unlock_bh(&dev->sta_poll_lock);

out:
	rcu_read_unlock();
}

bool mt7915_rx_check(struct mt76_dev *mdev, void *data, int len)
{
	struct mt7915_dev *dev = container_of(mdev, struct mt7915_dev, mt76);
	__le32 *rxd = (__le32 *)data;
	__le32 *end = (__le32 *)&rxd[len / 4];
	enum rx_pkt_type type;

	type = le32_get_bits(rxd[0], MT_RXD0_PKT_TYPE);

	switch (type) {
	case PKT_TYPE_TXRX_NOTIFY:
		mt7915_mac_tx_free(dev, data, len);
		return false;
	case PKT_TYPE_TXRX_NOTIFY_V0:
		mt7915_mac_tx_free_v0(dev, data, len);
		return false;
	case PKT_TYPE_TXS:
		for (rxd += 2; rxd + 8 <= end; rxd += 8)
			mt7915_mac_add_txs(dev, rxd);
		return false;
	case PKT_TYPE_RX_FW_MONITOR:
		mt7915_debugfs_rx_fw_monitor(dev, data, len);
		return false;
	default:
		return true;
	}
}

void mt7915_queue_rx_skb(struct mt76_dev *mdev, enum mt76_rxq_id q,
			 struct sk_buff *skb, u32 *info)
{
	struct mt7915_dev *dev = container_of(mdev, struct mt7915_dev, mt76);
	__le32 *rxd = (__le32 *)skb->data;
	__le32 *end = (__le32 *)&skb->data[skb->len];
	enum rx_pkt_type type;

	type = le32_get_bits(rxd[0], MT_RXD0_PKT_TYPE);

	switch (type) {
	case PKT_TYPE_TXRX_NOTIFY:
		mt7915_mac_tx_free(dev, skb->data, skb->len);
		napi_consume_skb(skb, 1);
		break;
	case PKT_TYPE_TXRX_NOTIFY_V0:
		mt7915_mac_tx_free_v0(dev, skb->data, skb->len);
		napi_consume_skb(skb, 1);
		break;
	case PKT_TYPE_RX_EVENT:
		mt7915_mcu_rx_event(dev, skb);
		break;
	case PKT_TYPE_TXRXV:
		mt7915_mac_fill_rx_vector(dev, skb);
		break;
	case PKT_TYPE_TXS:
		for (rxd += 2; rxd + 8 <= end; rxd += 8)
			mt7915_mac_add_txs(dev, rxd);
		dev_kfree_skb(skb);
		break;
	case PKT_TYPE_RX_FW_MONITOR:
		mt7915_debugfs_rx_fw_monitor(dev, skb->data, skb->len);
		dev_kfree_skb(skb);
		break;
	case PKT_TYPE_NORMAL:
		if (!mt7915_mac_fill_rx(dev, skb, q, info)) {
			mt76_rx(&dev->mt76, q, skb);
			return;
		}
		fallthrough;
	default:
		dev_kfree_skb(skb);
		break;
	}
}

void mt7915_mac_cca_stats_reset(struct mt7915_phy *phy)
{
	struct mt7915_dev *dev = phy->dev;
	u32 reg = MT_WF_PHY_RX_CTRL1(phy->mt76->band_idx);

	mt76_clear(dev, reg, MT_WF_PHY_RX_CTRL1_STSCNT_EN);
	mt76_set(dev, reg, BIT(11) | BIT(9));
}

void mt7915_mac_reset_counters(struct mt7915_phy *phy)
{
	struct mt7915_dev *dev = phy->dev;
	int i;

	for (i = 0; i < 4; i++) {
		mt76_rr(dev, MT_TX_AGG_CNT(phy->mt76->band_idx, i));
		mt76_rr(dev, MT_TX_AGG_CNT2(phy->mt76->band_idx, i));
	}

	phy->mt76->survey_time = ktime_get_boottime();
	memset(phy->mt76->aggr_stats, 0, sizeof(phy->mt76->aggr_stats));

	/* reset airtime counters */
	mt76_set(dev, MT_WF_RMAC_MIB_AIRTIME0(phy->mt76->band_idx),
		 MT_WF_RMAC_MIB_RXTIME_CLR);

	mt7915_mcu_get_chan_mib_info(phy, true);
}

void mt7915_mac_set_timing(struct mt7915_phy *phy)
{
	s16 coverage_class = phy->coverage_class;
	struct mt7915_dev *dev = phy->dev;
	struct mt7915_phy *ext_phy = mt7915_ext_phy(dev);
	u32 val, reg_offset;
	u32 cck = FIELD_PREP(MT_TIMEOUT_VAL_PLCP, 231) |
		  FIELD_PREP(MT_TIMEOUT_VAL_CCA, 48);
	u32 ofdm = FIELD_PREP(MT_TIMEOUT_VAL_PLCP, 60) |
		   FIELD_PREP(MT_TIMEOUT_VAL_CCA, 28);
	u8 band = phy->mt76->band_idx;
	int eifs_ofdm = 360, sifs = 10, offset;
	bool a_band = !(phy->mt76->chandef.chan->band == NL80211_BAND_2GHZ);

	if (!test_bit(MT76_STATE_RUNNING, &phy->mt76->state))
		return;

	if (ext_phy)
		coverage_class = max_t(s16, dev->phy.coverage_class,
				       ext_phy->coverage_class);

	mt76_set(dev, MT_ARB_SCR(band),
		 MT_ARB_SCR_TX_DISABLE | MT_ARB_SCR_RX_DISABLE);
	udelay(1);

	offset = 3 * coverage_class;
	reg_offset = FIELD_PREP(MT_TIMEOUT_VAL_PLCP, offset) |
		     FIELD_PREP(MT_TIMEOUT_VAL_CCA, offset);

	if (!is_mt7915(&dev->mt76)) {
		if (!a_band) {
			mt76_wr(dev, MT_TMAC_ICR1(band),
				FIELD_PREP(MT_IFS_EIFS_CCK, 314));
			eifs_ofdm = 78;
		} else {
			eifs_ofdm = 84;
		}
	} else if (a_band) {
		sifs = 16;
	}

	mt76_wr(dev, MT_TMAC_CDTR(band), cck + reg_offset);
	mt76_wr(dev, MT_TMAC_ODTR(band), ofdm + reg_offset);
	mt76_wr(dev, MT_TMAC_ICR0(band),
		FIELD_PREP(MT_IFS_EIFS_OFDM, eifs_ofdm) |
		FIELD_PREP(MT_IFS_RIFS, 2) |
		FIELD_PREP(MT_IFS_SIFS, sifs) |
		FIELD_PREP(MT_IFS_SLOT, phy->slottime));

	if (phy->slottime < 20 || a_band)
		val = MT7915_CFEND_RATE_DEFAULT;
	else
		val = MT7915_CFEND_RATE_11B;

	mt76_rmw_field(dev, MT_AGG_ACR0(band), MT_AGG_ACR_CFEND_RATE, val);
	mt76_clear(dev, MT_ARB_SCR(band),
		   MT_ARB_SCR_TX_DISABLE | MT_ARB_SCR_RX_DISABLE);
}

void mt7915_mac_enable_nf(struct mt7915_dev *dev, bool band)
{
	u32 reg;

	reg = is_mt7915(&dev->mt76) ? MT_WF_PHY_RXTD12(band) :
				      MT_WF_PHY_RXTD12_MT7916(band);
	mt76_set(dev, reg,
		 MT_WF_PHY_RXTD12_IRPI_SW_CLR_ONLY |
		 MT_WF_PHY_RXTD12_IRPI_SW_CLR);

	reg = is_mt7915(&dev->mt76) ? MT_WF_PHY_RX_CTRL1(band) :
				      MT_WF_PHY_RX_CTRL1_MT7916(band);
	mt76_set(dev, reg, FIELD_PREP(MT_WF_PHY_RX_CTRL1_IPI_EN, 0x5));
}

static u8
mt7915_phy_get_nf(struct mt7915_phy *phy, int idx)
{
	static const u8 nf_power[] = { 92, 89, 86, 83, 80, 75, 70, 65, 60, 55, 52 };
	struct mt7915_dev *dev = phy->dev;
	u32 val, sum = 0, n = 0;
	int nss, i;

	for (nss = 0; nss < hweight8(phy->mt76->chainmask); nss++) {
		u32 reg = is_mt7915(&dev->mt76) ?
			MT_WF_IRPI_NSS(0, nss + (idx << dev->dbdc_support)) :
			MT_WF_IRPI_NSS_MT7916(idx, nss);

		for (i = 0; i < ARRAY_SIZE(nf_power); i++, reg += 4) {
			val = mt76_rr(dev, reg);
			sum += val * nf_power[i];
			n += val;
		}
	}

	if (!n)
		return 0;

	return sum / n;
}

void mt7915_update_channel(struct mt76_phy *mphy)
{
	struct mt7915_phy *phy = (struct mt7915_phy *)mphy->priv;
	struct mt76_channel_state *state = mphy->chan_state;
	int nf;

	mt7915_mcu_get_chan_mib_info(phy, false);

	nf = mt7915_phy_get_nf(phy, phy->mt76->band_idx);
	if (!phy->noise)
		phy->noise = nf << 4;
	else if (nf)
		phy->noise += nf - (phy->noise >> 4);

	state->noise = -(phy->noise >> 4);
}

static bool
mt7915_wait_reset_state(struct mt7915_dev *dev, u32 state)
{
	bool ret;

	ret = wait_event_timeout(dev->reset_wait,
				 (READ_ONCE(dev->recovery.state) & state),
				 MT7915_RESET_TIMEOUT);

	WARN(!ret, "Timeout waiting for MCU reset state %x\n", state);
	return ret;
}

static void
mt7915_update_vif_beacon(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	struct ieee80211_hw *hw = priv;

	switch (vif->type) {
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_AP:
		mt7915_mcu_add_beacon(hw, vif, vif->bss_conf.enable_beacon,
				      BSS_CHANGED_BEACON_ENABLED);
		break;
	default:
		break;
	}
}

static void
mt7915_update_beacons(struct mt7915_dev *dev)
{
	struct mt76_phy *mphy_ext = dev->mt76.phys[MT_BAND1];

	ieee80211_iterate_active_interfaces(dev->mt76.hw,
		IEEE80211_IFACE_ITER_RESUME_ALL,
		mt7915_update_vif_beacon, dev->mt76.hw);

	if (!mphy_ext)
		return;

	ieee80211_iterate_active_interfaces(mphy_ext->hw,
		IEEE80211_IFACE_ITER_RESUME_ALL,
		mt7915_update_vif_beacon, mphy_ext->hw);
}

void mt7915_tx_token_put(struct mt7915_dev *dev)
{
	struct mt76_txwi_cache *txwi;
	int id;

	spin_lock_bh(&dev->mt76.token_lock);
	idr_for_each_entry(&dev->mt76.token, txwi, id) {
		mt7915_txwi_free(dev, txwi, NULL, NULL);
		dev->mt76.token_count--;
	}
	spin_unlock_bh(&dev->mt76.token_lock);
	idr_destroy(&dev->mt76.token);
}

static int
mt7915_mac_restart(struct mt7915_dev *dev)
{
	struct mt7915_phy *phy2;
	struct mt76_phy *ext_phy;
	struct mt76_dev *mdev = &dev->mt76;
	int i, ret;

	ext_phy = dev->mt76.phys[MT_BAND1];
	phy2 = ext_phy ? ext_phy->priv : NULL;

	if (dev->hif2) {
		mt76_wr(dev, MT_INT1_MASK_CSR, 0x0);
		mt76_wr(dev, MT_INT1_SOURCE_CSR, ~0);
	}

	if (dev_is_pci(mdev->dev)) {
		mt76_wr(dev, MT_PCIE_MAC_INT_ENABLE, 0x0);
		if (dev->hif2)
			mt76_wr(dev, MT_PCIE1_MAC_INT_ENABLE, 0x0);
	}

	set_bit(MT76_RESET, &dev->mphy.state);
	set_bit(MT76_MCU_RESET, &dev->mphy.state);
	wake_up(&dev->mt76.mcu.wait);
	if (ext_phy) {
		set_bit(MT76_RESET, &ext_phy->state);
		set_bit(MT76_MCU_RESET, &ext_phy->state);
	}

	/* lock/unlock all queues to ensure that no tx is pending */
	mt76_txq_schedule_all(&dev->mphy);
	if (ext_phy)
		mt76_txq_schedule_all(ext_phy);

	/* disable all tx/rx napi */
	mt76_worker_disable(&dev->mt76.tx_worker);
	mt76_for_each_q_rx(mdev, i) {
		if (mdev->q_rx[i].ndesc)
			napi_disable(&dev->mt76.napi[i]);
	}
	napi_disable(&dev->mt76.tx_napi);

	/* token reinit */
	mt7915_tx_token_put(dev);
	idr_init(&dev->mt76.token);

	mt7915_dma_reset(dev, true);

	local_bh_disable();
	mt76_for_each_q_rx(mdev, i) {
		if (mdev->q_rx[i].ndesc) {
			napi_enable(&dev->mt76.napi[i]);
			napi_schedule(&dev->mt76.napi[i]);
		}
	}
	local_bh_enable();
	clear_bit(MT76_MCU_RESET, &dev->mphy.state);
	clear_bit(MT76_STATE_MCU_RUNNING, &dev->mphy.state);

	mt76_wr(dev, MT_INT_MASK_CSR, dev->mt76.mmio.irqmask);
	mt76_wr(dev, MT_INT_SOURCE_CSR, ~0);

	if (dev->hif2) {
		mt76_wr(dev, MT_INT1_MASK_CSR, dev->mt76.mmio.irqmask);
		mt76_wr(dev, MT_INT1_SOURCE_CSR, ~0);
	}
	if (dev_is_pci(mdev->dev)) {
		mt76_wr(dev, MT_PCIE_MAC_INT_ENABLE, 0xff);
		if (dev->hif2)
			mt76_wr(dev, MT_PCIE1_MAC_INT_ENABLE, 0xff);
	}

	/* load firmware */
	ret = mt7915_mcu_init_firmware(dev);
	if (ret)
		goto out;

	/* set the necessary init items */
	ret = mt7915_mcu_set_eeprom(dev);
	if (ret)
		goto out;

	mt7915_mac_init(dev);
	mt7915_init_txpower(dev, &dev->mphy.sband_2g.sband);
	mt7915_init_txpower(dev, &dev->mphy.sband_5g.sband);
	ret = mt7915_txbf_init(dev);

	if (test_bit(MT76_STATE_RUNNING, &dev->mphy.state)) {
		ret = mt7915_run(dev->mphy.hw);
		if (ret)
			goto out;
	}

	if (ext_phy && test_bit(MT76_STATE_RUNNING, &ext_phy->state)) {
		ret = mt7915_run(ext_phy->hw);
		if (ret)
			goto out;
	}

out:
	/* reset done */
	clear_bit(MT76_RESET, &dev->mphy.state);
	if (phy2)
		clear_bit(MT76_RESET, &phy2->mt76->state);

	local_bh_disable();
	napi_enable(&dev->mt76.tx_napi);
	napi_schedule(&dev->mt76.tx_napi);
	local_bh_enable();

	mt76_worker_enable(&dev->mt76.tx_worker);

	return ret;
}

static void
mt7915_mac_full_reset(struct mt7915_dev *dev)
{
	struct mt76_phy *ext_phy;
	int i;

	ext_phy = dev->mt76.phys[MT_BAND1];

	dev->recovery.hw_full_reset = true;

	wake_up(&dev->mt76.mcu.wait);
	ieee80211_stop_queues(mt76_hw(dev));
	if (ext_phy)
		ieee80211_stop_queues(ext_phy->hw);

	cancel_delayed_work_sync(&dev->mphy.mac_work);
	if (ext_phy)
		cancel_delayed_work_sync(&ext_phy->mac_work);

	mutex_lock(&dev->mt76.mutex);
	for (i = 0; i < 10; i++) {
		if (!mt7915_mac_restart(dev))
			break;
	}
	mutex_unlock(&dev->mt76.mutex);

	if (i == 10)
		dev_err(dev->mt76.dev, "chip full reset failed\n");

	ieee80211_restart_hw(mt76_hw(dev));
	if (ext_phy)
		ieee80211_restart_hw(ext_phy->hw);

	ieee80211_wake_queues(mt76_hw(dev));
	if (ext_phy)
		ieee80211_wake_queues(ext_phy->hw);

	dev->recovery.hw_full_reset = false;
	ieee80211_queue_delayed_work(mt76_hw(dev), &dev->mphy.mac_work,
				     MT7915_WATCHDOG_TIME);
	if (ext_phy)
		ieee80211_queue_delayed_work(ext_phy->hw,
					     &ext_phy->mac_work,
					     MT7915_WATCHDOG_TIME);
}

/* system error recovery */
void mt7915_mac_reset_work(struct work_struct *work)
{
	struct mt7915_phy *phy2;
	struct mt76_phy *ext_phy;
	struct mt7915_dev *dev;
	int i;

	dev = container_of(work, struct mt7915_dev, reset_work);
	ext_phy = dev->mt76.phys[MT_BAND1];
	phy2 = ext_phy ? ext_phy->priv : NULL;

	/* chip full reset */
	if (dev->recovery.restart) {
		/* disable WA/WM WDT */
		mt76_clear(dev, MT_WFDMA0_MCU_HOST_INT_ENA,
			   MT_MCU_CMD_WDT_MASK);

		if (READ_ONCE(dev->recovery.state) & MT_MCU_CMD_WA_WDT)
			dev->recovery.wa_reset_count++;
		else
			dev->recovery.wm_reset_count++;

		mt7915_mac_full_reset(dev);

		/* enable mcu irq */
		mt7915_irq_enable(dev, MT_INT_MCU_CMD);
		mt7915_irq_disable(dev, 0);

		/* enable WA/WM WDT */
		mt76_set(dev, MT_WFDMA0_MCU_HOST_INT_ENA, MT_MCU_CMD_WDT_MASK);

		dev->recovery.state = MT_MCU_CMD_NORMAL_STATE;
		dev->recovery.restart = false;
		return;
	}

	/* chip partial reset */
	if (!(READ_ONCE(dev->recovery.state) & MT_MCU_CMD_STOP_DMA))
		return;

	if (mtk_wed_device_active(&dev->mt76.mmio.wed)) {
		mtk_wed_device_stop(&dev->mt76.mmio.wed);
		if (!is_mt7986(&dev->mt76))
			mt76_wr(dev, MT_INT_WED_MASK_CSR, 0);
	}

	ieee80211_stop_queues(mt76_hw(dev));
	if (ext_phy)
		ieee80211_stop_queues(ext_phy->hw);

	set_bit(MT76_RESET, &dev->mphy.state);
	set_bit(MT76_MCU_RESET, &dev->mphy.state);
	wake_up(&dev->mt76.mcu.wait);
	cancel_delayed_work_sync(&dev->mphy.mac_work);
	if (phy2) {
		set_bit(MT76_RESET, &phy2->mt76->state);
		cancel_delayed_work_sync(&phy2->mt76->mac_work);
	}
	mt76_worker_disable(&dev->mt76.tx_worker);
	mt76_for_each_q_rx(&dev->mt76, i)
		napi_disable(&dev->mt76.napi[i]);
	napi_disable(&dev->mt76.tx_napi);

	mutex_lock(&dev->mt76.mutex);

	mt76_wr(dev, MT_MCU_INT_EVENT, MT_MCU_INT_EVENT_DMA_STOPPED);

	if (mt7915_wait_reset_state(dev, MT_MCU_CMD_RESET_DONE)) {
		mt7915_dma_reset(dev, false);

		mt7915_tx_token_put(dev);
		idr_init(&dev->mt76.token);

		mt76_wr(dev, MT_MCU_INT_EVENT, MT_MCU_INT_EVENT_DMA_INIT);
		mt7915_wait_reset_state(dev, MT_MCU_CMD_RECOVERY_DONE);
	}

	clear_bit(MT76_MCU_RESET, &dev->mphy.state);
	clear_bit(MT76_RESET, &dev->mphy.state);
	if (phy2)
		clear_bit(MT76_RESET, &phy2->mt76->state);

	local_bh_disable();
	mt76_for_each_q_rx(&dev->mt76, i) {
		napi_enable(&dev->mt76.napi[i]);
		napi_schedule(&dev->mt76.napi[i]);
	}
	local_bh_enable();

	tasklet_schedule(&dev->irq_tasklet);

	mt76_wr(dev, MT_MCU_INT_EVENT, MT_MCU_INT_EVENT_RESET_DONE);
	mt7915_wait_reset_state(dev, MT_MCU_CMD_NORMAL_STATE);

	mt76_worker_enable(&dev->mt76.tx_worker);

	local_bh_disable();
	napi_enable(&dev->mt76.tx_napi);
	napi_schedule(&dev->mt76.tx_napi);
	local_bh_enable();

	ieee80211_wake_queues(mt76_hw(dev));
	if (ext_phy)
		ieee80211_wake_queues(ext_phy->hw);

	mutex_unlock(&dev->mt76.mutex);

	mt7915_update_beacons(dev);

	ieee80211_queue_delayed_work(mt76_hw(dev), &dev->mphy.mac_work,
				     MT7915_WATCHDOG_TIME);
	if (phy2)
		ieee80211_queue_delayed_work(ext_phy->hw,
					     &phy2->mt76->mac_work,
					     MT7915_WATCHDOG_TIME);
}

/* firmware coredump */
void mt7915_mac_dump_work(struct work_struct *work)
{
	const struct mt7915_mem_region *mem_region;
	struct mt7915_crash_data *crash_data;
	struct mt7915_dev *dev;
	struct mt7915_mem_hdr *hdr;
	size_t buf_len;
	int i;
	u32 num;
	u8 *buf;

	dev = container_of(work, struct mt7915_dev, dump_work);

	mutex_lock(&dev->dump_mutex);

	crash_data = mt7915_coredump_new(dev);
	if (!crash_data) {
		mutex_unlock(&dev->dump_mutex);
		goto skip_coredump;
	}

	mem_region = mt7915_coredump_get_mem_layout(dev, &num);
	if (!mem_region || !crash_data->memdump_buf_len) {
		mutex_unlock(&dev->dump_mutex);
		goto skip_memdump;
	}

	buf = crash_data->memdump_buf;
	buf_len = crash_data->memdump_buf_len;

	/* dumping memory content... */
	memset(buf, 0, buf_len);
	for (i = 0; i < num; i++) {
		if (mem_region->len > buf_len) {
			dev_warn(dev->mt76.dev, "%s len %lu is too large\n",
				 mem_region->name,
				 (unsigned long)mem_region->len);
			break;
		}

		/* reserve space for the header */
		hdr = (void *)buf;
		buf += sizeof(*hdr);
		buf_len -= sizeof(*hdr);

		mt7915_memcpy_fromio(dev, buf, mem_region->start,
				     mem_region->len);

		hdr->start = mem_region->start;
		hdr->len = mem_region->len;

		if (!mem_region->len)
			/* note: the header remains, just with zero length */
			break;

		buf += mem_region->len;
		buf_len -= mem_region->len;

		mem_region++;
	}

	mutex_unlock(&dev->dump_mutex);

skip_memdump:
	mt7915_coredump_submit(dev);
skip_coredump:
	queue_work(dev->mt76.wq, &dev->reset_work);
}

void mt7915_reset(struct mt7915_dev *dev)
{
	if (!dev->recovery.hw_init_done)
		return;

	if (dev->recovery.hw_full_reset)
		return;

	/* wm/wa exception: do full recovery */
	if (READ_ONCE(dev->recovery.state) & MT_MCU_CMD_WDT_MASK) {
		dev->recovery.restart = true;
		dev_info(dev->mt76.dev,
			 "%s indicated firmware crash, attempting recovery\n",
			 wiphy_name(dev->mt76.hw->wiphy));

		mt7915_irq_disable(dev, MT_INT_MCU_CMD);
		queue_work(dev->mt76.wq, &dev->dump_work);
		return;
	}

	queue_work(dev->mt76.wq, &dev->reset_work);
	wake_up(&dev->reset_wait);
}

void mt7915_mac_update_stats(struct mt7915_phy *phy)
{
	struct mt7915_dev *dev = phy->dev;
	struct mib_stats *mib = &phy->mib;
	int i, aggr0 = 0, aggr1, cnt;
	u8 band = phy->mt76->band_idx;
	u32 val;

	cnt = mt76_rr(dev, MT_MIB_SDR3(band));
	mib->fcs_err_cnt += is_mt7915(&dev->mt76) ?
		FIELD_GET(MT_MIB_SDR3_FCS_ERR_MASK, cnt) :
		FIELD_GET(MT_MIB_SDR3_FCS_ERR_MASK_MT7916, cnt);

	cnt = mt76_rr(dev, MT_MIB_SDR4(band));
	mib->rx_fifo_full_cnt += FIELD_GET(MT_MIB_SDR4_RX_FIFO_FULL_MASK, cnt);

	cnt = mt76_rr(dev, MT_MIB_SDR5(band));
	mib->rx_mpdu_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_SDR6(band));
	mib->channel_idle_cnt += FIELD_GET(MT_MIB_SDR6_CHANNEL_IDL_CNT_MASK, cnt);

	cnt = mt76_rr(dev, MT_MIB_SDR7(band));
	mib->rx_vector_mismatch_cnt +=
		FIELD_GET(MT_MIB_SDR7_RX_VECTOR_MISMATCH_CNT_MASK, cnt);

	cnt = mt76_rr(dev, MT_MIB_SDR8(band));
	mib->rx_delimiter_fail_cnt +=
		FIELD_GET(MT_MIB_SDR8_RX_DELIMITER_FAIL_CNT_MASK, cnt);

	cnt = mt76_rr(dev, MT_MIB_SDR10(band));
	mib->rx_mrdy_cnt += is_mt7915(&dev->mt76) ?
		FIELD_GET(MT_MIB_SDR10_MRDY_COUNT_MASK, cnt) :
		FIELD_GET(MT_MIB_SDR10_MRDY_COUNT_MASK_MT7916, cnt);

	cnt = mt76_rr(dev, MT_MIB_SDR11(band));
	mib->rx_len_mismatch_cnt +=
		FIELD_GET(MT_MIB_SDR11_RX_LEN_MISMATCH_CNT_MASK, cnt);

	cnt = mt76_rr(dev, MT_MIB_SDR12(band));
	mib->tx_ampdu_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_SDR13(band));
	mib->tx_stop_q_empty_cnt +=
		FIELD_GET(MT_MIB_SDR13_TX_STOP_Q_EMPTY_CNT_MASK, cnt);

	cnt = mt76_rr(dev, MT_MIB_SDR14(band));
	mib->tx_mpdu_attempts_cnt += is_mt7915(&dev->mt76) ?
		FIELD_GET(MT_MIB_SDR14_TX_MPDU_ATTEMPTS_CNT_MASK, cnt) :
		FIELD_GET(MT_MIB_SDR14_TX_MPDU_ATTEMPTS_CNT_MASK_MT7916, cnt);

	cnt = mt76_rr(dev, MT_MIB_SDR15(band));
	mib->tx_mpdu_success_cnt += is_mt7915(&dev->mt76) ?
		FIELD_GET(MT_MIB_SDR15_TX_MPDU_SUCCESS_CNT_MASK, cnt) :
		FIELD_GET(MT_MIB_SDR15_TX_MPDU_SUCCESS_CNT_MASK_MT7916, cnt);

	cnt = mt76_rr(dev, MT_MIB_SDR16(band));
	mib->primary_cca_busy_time +=
		FIELD_GET(MT_MIB_SDR16_PRIMARY_CCA_BUSY_TIME_MASK, cnt);

	cnt = mt76_rr(dev, MT_MIB_SDR17(band));
	mib->secondary_cca_busy_time +=
		FIELD_GET(MT_MIB_SDR17_SECONDARY_CCA_BUSY_TIME_MASK, cnt);

	cnt = mt76_rr(dev, MT_MIB_SDR18(band));
	mib->primary_energy_detect_time +=
		FIELD_GET(MT_MIB_SDR18_PRIMARY_ENERGY_DETECT_TIME_MASK, cnt);

	cnt = mt76_rr(dev, MT_MIB_SDR19(band));
	mib->cck_mdrdy_time += FIELD_GET(MT_MIB_SDR19_CCK_MDRDY_TIME_MASK, cnt);

	cnt = mt76_rr(dev, MT_MIB_SDR20(band));
	mib->ofdm_mdrdy_time +=
		FIELD_GET(MT_MIB_SDR20_OFDM_VHT_MDRDY_TIME_MASK, cnt);

	cnt = mt76_rr(dev, MT_MIB_SDR21(band));
	mib->green_mdrdy_time +=
		FIELD_GET(MT_MIB_SDR21_GREEN_MDRDY_TIME_MASK, cnt);

	cnt = mt76_rr(dev, MT_MIB_SDR22(band));
	mib->rx_ampdu_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_SDR23(band));
	mib->rx_ampdu_bytes_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_SDR24(band));
	mib->rx_ampdu_valid_subframe_cnt += is_mt7915(&dev->mt76) ?
		FIELD_GET(MT_MIB_SDR24_RX_AMPDU_SF_CNT_MASK, cnt) :
		FIELD_GET(MT_MIB_SDR24_RX_AMPDU_SF_CNT_MASK_MT7916, cnt);

	cnt = mt76_rr(dev, MT_MIB_SDR25(band));
	mib->rx_ampdu_valid_subframe_bytes_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_SDR27(band));
	mib->tx_rwp_fail_cnt +=
		FIELD_GET(MT_MIB_SDR27_TX_RWP_FAIL_CNT_MASK, cnt);

	cnt = mt76_rr(dev, MT_MIB_SDR28(band));
	mib->tx_rwp_need_cnt +=
		FIELD_GET(MT_MIB_SDR28_TX_RWP_NEED_CNT_MASK, cnt);

	cnt = mt76_rr(dev, MT_MIB_SDR29(band));
	mib->rx_pfdrop_cnt += is_mt7915(&dev->mt76) ?
		FIELD_GET(MT_MIB_SDR29_RX_PFDROP_CNT_MASK, cnt) :
		FIELD_GET(MT_MIB_SDR29_RX_PFDROP_CNT_MASK_MT7916, cnt);

	cnt = mt76_rr(dev, MT_MIB_SDRVEC(band));
	mib->rx_vec_queue_overflow_drop_cnt += is_mt7915(&dev->mt76) ?
		FIELD_GET(MT_MIB_SDR30_RX_VEC_QUEUE_OVERFLOW_DROP_CNT_MASK, cnt) :
		FIELD_GET(MT_MIB_SDR30_RX_VEC_QUEUE_OVERFLOW_DROP_CNT_MASK_MT7916, cnt);

	cnt = mt76_rr(dev, MT_MIB_SDR31(band));
	mib->rx_ba_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_SDRMUBF(band));
	mib->tx_bf_cnt += FIELD_GET(MT_MIB_MU_BF_TX_CNT, cnt);

	cnt = mt76_rr(dev, MT_MIB_DR8(band));
	mib->tx_mu_mpdu_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_DR9(band));
	mib->tx_mu_acked_mpdu_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_DR11(band));
	mib->tx_su_acked_mpdu_cnt += cnt;

	cnt = mt76_rr(dev, MT_ETBF_PAR_RPT0(band));
	mib->tx_bf_rx_fb_bw = FIELD_GET(MT_ETBF_PAR_RPT0_FB_BW, cnt);
	mib->tx_bf_rx_fb_nc_cnt += FIELD_GET(MT_ETBF_PAR_RPT0_FB_NC, cnt);
	mib->tx_bf_rx_fb_nr_cnt += FIELD_GET(MT_ETBF_PAR_RPT0_FB_NR, cnt);

	for (i = 0; i < ARRAY_SIZE(mib->tx_amsdu); i++) {
		cnt = mt76_rr(dev, MT_PLE_AMSDU_PACK_MSDU_CNT(i));
		mib->tx_amsdu[i] += cnt;
		mib->tx_amsdu_cnt += cnt;
	}

	if (is_mt7915(&dev->mt76)) {
		for (i = 0, aggr1 = aggr0 + 8; i < 4; i++) {
			val = mt76_rr(dev, MT_MIB_MB_SDR1(band, (i << 4)));
			mib->ba_miss_cnt +=
				FIELD_GET(MT_MIB_BA_MISS_COUNT_MASK, val);
			mib->ack_fail_cnt +=
				FIELD_GET(MT_MIB_ACK_FAIL_COUNT_MASK, val);

			val = mt76_rr(dev, MT_MIB_MB_SDR0(band, (i << 4)));
			mib->rts_cnt += FIELD_GET(MT_MIB_RTS_COUNT_MASK, val);
			mib->rts_retries_cnt +=
				FIELD_GET(MT_MIB_RTS_RETRIES_COUNT_MASK, val);

			val = mt76_rr(dev, MT_TX_AGG_CNT(band, i));
			phy->mt76->aggr_stats[aggr0++] += val & 0xffff;
			phy->mt76->aggr_stats[aggr0++] += val >> 16;

			val = mt76_rr(dev, MT_TX_AGG_CNT2(band, i));
			phy->mt76->aggr_stats[aggr1++] += val & 0xffff;
			phy->mt76->aggr_stats[aggr1++] += val >> 16;
		}

		cnt = mt76_rr(dev, MT_MIB_SDR32(band));
		mib->tx_pkt_ebf_cnt += FIELD_GET(MT_MIB_SDR32_TX_PKT_EBF_CNT, cnt);

		cnt = mt76_rr(dev, MT_MIB_SDR33(band));
		mib->tx_pkt_ibf_cnt += FIELD_GET(MT_MIB_SDR33_TX_PKT_IBF_CNT, cnt);

		cnt = mt76_rr(dev, MT_ETBF_TX_APP_CNT(band));
		mib->tx_bf_ibf_ppdu_cnt += FIELD_GET(MT_ETBF_TX_IBF_CNT, cnt);
		mib->tx_bf_ebf_ppdu_cnt += FIELD_GET(MT_ETBF_TX_EBF_CNT, cnt);

		cnt = mt76_rr(dev, MT_ETBF_TX_NDP_BFRP(band));
		mib->tx_bf_fb_cpl_cnt += FIELD_GET(MT_ETBF_TX_FB_CPL, cnt);
		mib->tx_bf_fb_trig_cnt += FIELD_GET(MT_ETBF_TX_FB_TRI, cnt);

		cnt = mt76_rr(dev, MT_ETBF_RX_FB_CNT(band));
		mib->tx_bf_rx_fb_all_cnt += FIELD_GET(MT_ETBF_RX_FB_ALL, cnt);
		mib->tx_bf_rx_fb_he_cnt += FIELD_GET(MT_ETBF_RX_FB_HE, cnt);
		mib->tx_bf_rx_fb_vht_cnt += FIELD_GET(MT_ETBF_RX_FB_VHT, cnt);
		mib->tx_bf_rx_fb_ht_cnt += FIELD_GET(MT_ETBF_RX_FB_HT, cnt);
	} else {
		for (i = 0; i < 2; i++) {
			/* rts count */
			val = mt76_rr(dev, MT_MIB_MB_SDR0(band, (i << 2)));
			mib->rts_cnt += FIELD_GET(GENMASK(15, 0), val);
			mib->rts_cnt += FIELD_GET(GENMASK(31, 16), val);

			/* rts retry count */
			val = mt76_rr(dev, MT_MIB_MB_SDR1(band, (i << 2)));
			mib->rts_retries_cnt += FIELD_GET(GENMASK(15, 0), val);
			mib->rts_retries_cnt += FIELD_GET(GENMASK(31, 16), val);

			/* ba miss count */
			val = mt76_rr(dev, MT_MIB_MB_SDR2(band, (i << 2)));
			mib->ba_miss_cnt += FIELD_GET(GENMASK(15, 0), val);
			mib->ba_miss_cnt += FIELD_GET(GENMASK(31, 16), val);

			/* ack fail count */
			val = mt76_rr(dev, MT_MIB_MB_BFTF(band, (i << 2)));
			mib->ack_fail_cnt += FIELD_GET(GENMASK(15, 0), val);
			mib->ack_fail_cnt += FIELD_GET(GENMASK(31, 16), val);
		}

		for (i = 0; i < 8; i++) {
			val = mt76_rr(dev, MT_TX_AGG_CNT(band, i));
			phy->mt76->aggr_stats[aggr0++] += FIELD_GET(GENMASK(15, 0), val);
			phy->mt76->aggr_stats[aggr0++] += FIELD_GET(GENMASK(31, 16), val);
		}

		cnt = mt76_rr(dev, MT_MIB_SDR32(band));
		mib->tx_pkt_ibf_cnt += FIELD_GET(MT_MIB_SDR32_TX_PKT_IBF_CNT, cnt);
		mib->tx_bf_ibf_ppdu_cnt += FIELD_GET(MT_MIB_SDR32_TX_PKT_IBF_CNT, cnt);
		mib->tx_pkt_ebf_cnt += FIELD_GET(MT_MIB_SDR32_TX_PKT_EBF_CNT, cnt);
		mib->tx_bf_ebf_ppdu_cnt += FIELD_GET(MT_MIB_SDR32_TX_PKT_EBF_CNT, cnt);

		cnt = mt76_rr(dev, MT_MIB_BFCR7(band));
		mib->tx_bf_fb_cpl_cnt += FIELD_GET(MT_MIB_BFCR7_BFEE_TX_FB_CPL, cnt);

		cnt = mt76_rr(dev, MT_MIB_BFCR2(band));
		mib->tx_bf_fb_trig_cnt += FIELD_GET(MT_MIB_BFCR2_BFEE_TX_FB_TRIG, cnt);

		cnt = mt76_rr(dev, MT_MIB_BFCR0(band));
		mib->tx_bf_rx_fb_vht_cnt += FIELD_GET(MT_MIB_BFCR0_RX_FB_VHT, cnt);
		mib->tx_bf_rx_fb_all_cnt += FIELD_GET(MT_MIB_BFCR0_RX_FB_VHT, cnt);
		mib->tx_bf_rx_fb_ht_cnt += FIELD_GET(MT_MIB_BFCR0_RX_FB_HT, cnt);
		mib->tx_bf_rx_fb_all_cnt += FIELD_GET(MT_MIB_BFCR0_RX_FB_HT, cnt);

		cnt = mt76_rr(dev, MT_MIB_BFCR1(band));
		mib->tx_bf_rx_fb_he_cnt += FIELD_GET(MT_MIB_BFCR1_RX_FB_HE, cnt);
		mib->tx_bf_rx_fb_all_cnt += FIELD_GET(MT_MIB_BFCR1_RX_FB_HE, cnt);
	}
}

static void mt7915_mac_severe_check(struct mt7915_phy *phy)
{
	struct mt7915_dev *dev = phy->dev;
	u32 trb;

	if (!phy->omac_mask)
		return;

	/* In rare cases, TRB pointers might be out of sync leads to RMAC
	 * stopping Rx, so check status periodically to see if TRB hardware
	 * requires minimal recovery.
	 */
	trb = mt76_rr(dev, MT_TRB_RXPSR0(phy->mt76->band_idx));

	if ((FIELD_GET(MT_TRB_RXPSR0_RX_RMAC_PTR, trb) !=
	     FIELD_GET(MT_TRB_RXPSR0_RX_WTBL_PTR, trb)) &&
	    (FIELD_GET(MT_TRB_RXPSR0_RX_RMAC_PTR, phy->trb_ts) !=
	     FIELD_GET(MT_TRB_RXPSR0_RX_WTBL_PTR, phy->trb_ts)) &&
	    trb == phy->trb_ts)
		mt7915_mcu_set_ser(dev, SER_RECOVER, SER_SET_RECOVER_L3_RX_ABORT,
				   phy->mt76->band_idx);

	phy->trb_ts = trb;
}

void mt7915_mac_sta_rc_work(struct work_struct *work)
{
	struct mt7915_dev *dev = container_of(work, struct mt7915_dev, rc_work);
	struct ieee80211_sta *sta;
	struct ieee80211_vif *vif;
	struct mt7915_sta *msta;
	u32 changed;
	LIST_HEAD(list);

	spin_lock_bh(&dev->sta_poll_lock);
	list_splice_init(&dev->sta_rc_list, &list);

	while (!list_empty(&list)) {
		msta = list_first_entry(&list, struct mt7915_sta, rc_list);
		list_del_init(&msta->rc_list);
		changed = msta->changed;
		msta->changed = 0;
		spin_unlock_bh(&dev->sta_poll_lock);

		sta = container_of((void *)msta, struct ieee80211_sta, drv_priv);
		vif = container_of((void *)msta->vif, struct ieee80211_vif, drv_priv);

		if (changed & (IEEE80211_RC_SUPP_RATES_CHANGED |
			       IEEE80211_RC_NSS_CHANGED |
			       IEEE80211_RC_BW_CHANGED))
			mt7915_mcu_add_rate_ctrl(dev, vif, sta, true);

		if (changed & IEEE80211_RC_SMPS_CHANGED)
			mt7915_mcu_add_smps(dev, vif, sta);

		spin_lock_bh(&dev->sta_poll_lock);
	}

	spin_unlock_bh(&dev->sta_poll_lock);
}

void mt7915_mac_work(struct work_struct *work)
{
	struct mt7915_phy *phy;
	struct mt76_phy *mphy;

	mphy = (struct mt76_phy *)container_of(work, struct mt76_phy,
					       mac_work.work);
	phy = mphy->priv;

	mutex_lock(&mphy->dev->mutex);

	mt76_update_survey(mphy);
	if (++mphy->mac_work_count == 5) {
		mphy->mac_work_count = 0;

		mt7915_mac_update_stats(phy);
		mt7915_mac_severe_check(phy);
	}

	mutex_unlock(&mphy->dev->mutex);

	mt76_tx_status_check(mphy->dev, false);

	ieee80211_queue_delayed_work(mphy->hw, &mphy->mac_work,
				     MT7915_WATCHDOG_TIME);
}

static void mt7915_dfs_stop_radar_detector(struct mt7915_phy *phy)
{
	struct mt7915_dev *dev = phy->dev;

	if (phy->rdd_state & BIT(0))
		mt76_connac_mcu_rdd_cmd(&dev->mt76, RDD_STOP, 0,
					MT_RX_SEL0, 0);
	if (phy->rdd_state & BIT(1))
		mt76_connac_mcu_rdd_cmd(&dev->mt76, RDD_STOP, 1,
					MT_RX_SEL0, 0);
}

static int mt7915_dfs_start_rdd(struct mt7915_dev *dev, int chain)
{
	int err, region;

	switch (dev->mt76.region) {
	case NL80211_DFS_ETSI:
		region = 0;
		break;
	case NL80211_DFS_JP:
		region = 2;
		break;
	case NL80211_DFS_FCC:
	default:
		region = 1;
		break;
	}

	err = mt76_connac_mcu_rdd_cmd(&dev->mt76, RDD_START, chain,
				      MT_RX_SEL0, region);
	if (err < 0)
		return err;

	if (is_mt7915(&dev->mt76)) {
		err = mt76_connac_mcu_rdd_cmd(&dev->mt76, RDD_SET_WF_ANT, chain,
					      0, dev->dbdc_support ? 2 : 0);
		if (err < 0)
			return err;
	}

	return mt76_connac_mcu_rdd_cmd(&dev->mt76, RDD_DET_MODE, chain,
				       MT_RX_SEL0, 1);
}

static int mt7915_dfs_start_radar_detector(struct mt7915_phy *phy)
{
	struct cfg80211_chan_def *chandef = &phy->mt76->chandef;
	struct mt7915_dev *dev = phy->dev;
	int err;

	/* start CAC */
	err = mt76_connac_mcu_rdd_cmd(&dev->mt76, RDD_CAC_START,
				      phy->mt76->band_idx, MT_RX_SEL0, 0);
	if (err < 0)
		return err;

	err = mt7915_dfs_start_rdd(dev, phy->mt76->band_idx);
	if (err < 0)
		return err;

	phy->rdd_state |= BIT(phy->mt76->band_idx);

	if (!is_mt7915(&dev->mt76))
		return 0;

	if (chandef->width == NL80211_CHAN_WIDTH_160 ||
	    chandef->width == NL80211_CHAN_WIDTH_80P80) {
		err = mt7915_dfs_start_rdd(dev, 1);
		if (err < 0)
			return err;

		phy->rdd_state |= BIT(1);
	}

	return 0;
}

static int
mt7915_dfs_init_radar_specs(struct mt7915_phy *phy)
{
	const struct mt7915_dfs_radar_spec *radar_specs;
	struct mt7915_dev *dev = phy->dev;
	int err, i;

	switch (dev->mt76.region) {
	case NL80211_DFS_FCC:
		radar_specs = &fcc_radar_specs;
		err = mt7915_mcu_set_fcc5_lpn(dev, 8);
		if (err < 0)
			return err;
		break;
	case NL80211_DFS_ETSI:
		radar_specs = &etsi_radar_specs;
		break;
	case NL80211_DFS_JP:
		radar_specs = &jp_radar_specs;
		break;
	default:
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(radar_specs->radar_pattern); i++) {
		err = mt7915_mcu_set_radar_th(dev, i,
					      &radar_specs->radar_pattern[i]);
		if (err < 0)
			return err;
	}

	return mt7915_mcu_set_pulse_th(dev, &radar_specs->pulse_th);
}

int mt7915_dfs_init_radar_detector(struct mt7915_phy *phy)
{
	struct mt7915_dev *dev = phy->dev;
	enum mt76_dfs_state dfs_state, prev_state;
	int err;

	prev_state = phy->mt76->dfs_state;
	dfs_state = mt76_phy_dfs_state(phy->mt76);

	if (prev_state == dfs_state)
		return 0;

	if (prev_state == MT_DFS_STATE_UNKNOWN)
		mt7915_dfs_stop_radar_detector(phy);

	if (dfs_state == MT_DFS_STATE_DISABLED)
		goto stop;

	if (prev_state <= MT_DFS_STATE_DISABLED) {
		err = mt7915_dfs_init_radar_specs(phy);
		if (err < 0)
			return err;

		err = mt7915_dfs_start_radar_detector(phy);
		if (err < 0)
			return err;

		phy->mt76->dfs_state = MT_DFS_STATE_CAC;
	}

	if (dfs_state == MT_DFS_STATE_CAC)
		return 0;

	err = mt76_connac_mcu_rdd_cmd(&dev->mt76, RDD_CAC_END,
				      phy->mt76->band_idx, MT_RX_SEL0, 0);
	if (err < 0) {
		phy->mt76->dfs_state = MT_DFS_STATE_UNKNOWN;
		return err;
	}

	phy->mt76->dfs_state = MT_DFS_STATE_ACTIVE;
	return 0;

stop:
	err = mt76_connac_mcu_rdd_cmd(&dev->mt76, RDD_NORMAL_START,
				      phy->mt76->band_idx, MT_RX_SEL0, 0);
	if (err < 0)
		return err;

	if (is_mt7915(&dev->mt76)) {
		err = mt76_connac_mcu_rdd_cmd(&dev->mt76, RDD_SET_WF_ANT,
					      phy->mt76->band_idx, 0,
					      dev->dbdc_support ? 2 : 0);
		if (err < 0)
			return err;
	}

	mt7915_dfs_stop_radar_detector(phy);
	phy->mt76->dfs_state = MT_DFS_STATE_DISABLED;

	return 0;
}

static int
mt7915_mac_twt_duration_align(int duration)
{
	return duration << 8;
}

static u64
mt7915_mac_twt_sched_list_add(struct mt7915_dev *dev,
			      struct mt7915_twt_flow *flow)
{
	struct mt7915_twt_flow *iter, *iter_next;
	u32 duration = flow->duration << 8;
	u64 start_tsf;

	iter = list_first_entry_or_null(&dev->twt_list,
					struct mt7915_twt_flow, list);
	if (!iter || !iter->sched || iter->start_tsf > duration) {
		/* add flow as first entry in the list */
		list_add(&flow->list, &dev->twt_list);
		return 0;
	}

	list_for_each_entry_safe(iter, iter_next, &dev->twt_list, list) {
		start_tsf = iter->start_tsf +
			    mt7915_mac_twt_duration_align(iter->duration);
		if (list_is_last(&iter->list, &dev->twt_list))
			break;

		if (!iter_next->sched ||
		    iter_next->start_tsf > start_tsf + duration) {
			list_add(&flow->list, &iter->list);
			goto out;
		}
	}

	/* add flow as last entry in the list */
	list_add_tail(&flow->list, &dev->twt_list);
out:
	return start_tsf;
}

static int mt7915_mac_check_twt_req(struct ieee80211_twt_setup *twt)
{
	struct ieee80211_twt_params *twt_agrt;
	u64 interval, duration;
	u16 mantissa;
	u8 exp;

	/* only individual agreement supported */
	if (twt->control & IEEE80211_TWT_CONTROL_NEG_TYPE_BROADCAST)
		return -EOPNOTSUPP;

	/* only 256us unit supported */
	if (twt->control & IEEE80211_TWT_CONTROL_WAKE_DUR_UNIT)
		return -EOPNOTSUPP;

	twt_agrt = (struct ieee80211_twt_params *)twt->params;

	/* explicit agreement not supported */
	if (!(twt_agrt->req_type & cpu_to_le16(IEEE80211_TWT_REQTYPE_IMPLICIT)))
		return -EOPNOTSUPP;

	exp = FIELD_GET(IEEE80211_TWT_REQTYPE_WAKE_INT_EXP,
			le16_to_cpu(twt_agrt->req_type));
	mantissa = le16_to_cpu(twt_agrt->mantissa);
	duration = twt_agrt->min_twt_dur << 8;

	interval = (u64)mantissa << exp;
	if (interval < duration)
		return -EOPNOTSUPP;

	return 0;
}

static bool
mt7915_mac_twt_param_equal(struct mt7915_sta *msta,
			   struct ieee80211_twt_params *twt_agrt)
{
	u16 type = le16_to_cpu(twt_agrt->req_type);
	u8 exp;
	int i;

	exp = FIELD_GET(IEEE80211_TWT_REQTYPE_WAKE_INT_EXP, type);
	for (i = 0; i < MT7915_MAX_STA_TWT_AGRT; i++) {
		struct mt7915_twt_flow *f;

		if (!(msta->twt.flowid_mask & BIT(i)))
			continue;

		f = &msta->twt.flow[i];
		if (f->duration == twt_agrt->min_twt_dur &&
		    f->mantissa == twt_agrt->mantissa &&
		    f->exp == exp &&
		    f->protection == !!(type & IEEE80211_TWT_REQTYPE_PROTECTION) &&
		    f->flowtype == !!(type & IEEE80211_TWT_REQTYPE_FLOWTYPE) &&
		    f->trigger == !!(type & IEEE80211_TWT_REQTYPE_TRIGGER))
			return true;
	}

	return false;
}

void mt7915_mac_add_twt_setup(struct ieee80211_hw *hw,
			      struct ieee80211_sta *sta,
			      struct ieee80211_twt_setup *twt)
{
	enum ieee80211_twt_setup_cmd setup_cmd = TWT_SETUP_CMD_REJECT;
	struct mt7915_sta *msta = (struct mt7915_sta *)sta->drv_priv;
	struct ieee80211_twt_params *twt_agrt = (void *)twt->params;
	u16 req_type = le16_to_cpu(twt_agrt->req_type);
	enum ieee80211_twt_setup_cmd sta_setup_cmd;
	struct mt7915_dev *dev = mt7915_hw_dev(hw);
	struct mt7915_twt_flow *flow;
	int flowid, table_id;
	u8 exp;

	if (mt7915_mac_check_twt_req(twt))
		goto out;

	mutex_lock(&dev->mt76.mutex);

	if (dev->twt.n_agrt == MT7915_MAX_TWT_AGRT)
		goto unlock;

	if (hweight8(msta->twt.flowid_mask) == ARRAY_SIZE(msta->twt.flow))
		goto unlock;

	if (twt_agrt->min_twt_dur < MT7915_MIN_TWT_DUR) {
		setup_cmd = TWT_SETUP_CMD_DICTATE;
		twt_agrt->min_twt_dur = MT7915_MIN_TWT_DUR;
		goto unlock;
	}

	flowid = ffs(~msta->twt.flowid_mask) - 1;
	twt_agrt->req_type &= ~cpu_to_le16(IEEE80211_TWT_REQTYPE_FLOWID);
	twt_agrt->req_type |= le16_encode_bits(flowid,
					       IEEE80211_TWT_REQTYPE_FLOWID);

	table_id = ffs(~dev->twt.table_mask) - 1;
	exp = FIELD_GET(IEEE80211_TWT_REQTYPE_WAKE_INT_EXP, req_type);
	sta_setup_cmd = FIELD_GET(IEEE80211_TWT_REQTYPE_SETUP_CMD, req_type);

	if (mt7915_mac_twt_param_equal(msta, twt_agrt))
		goto unlock;

	flow = &msta->twt.flow[flowid];
	memset(flow, 0, sizeof(*flow));
	INIT_LIST_HEAD(&flow->list);
	flow->wcid = msta->wcid.idx;
	flow->table_id = table_id;
	flow->id = flowid;
	flow->duration = twt_agrt->min_twt_dur;
	flow->mantissa = twt_agrt->mantissa;
	flow->exp = exp;
	flow->protection = !!(req_type & IEEE80211_TWT_REQTYPE_PROTECTION);
	flow->flowtype = !!(req_type & IEEE80211_TWT_REQTYPE_FLOWTYPE);
	flow->trigger = !!(req_type & IEEE80211_TWT_REQTYPE_TRIGGER);

	if (sta_setup_cmd == TWT_SETUP_CMD_REQUEST ||
	    sta_setup_cmd == TWT_SETUP_CMD_SUGGEST) {
		u64 interval = (u64)le16_to_cpu(twt_agrt->mantissa) << exp;
		u64 flow_tsf, curr_tsf;
		u32 rem;

		flow->sched = true;
		flow->start_tsf = mt7915_mac_twt_sched_list_add(dev, flow);
		curr_tsf = __mt7915_get_tsf(hw, msta->vif);
		div_u64_rem(curr_tsf - flow->start_tsf, interval, &rem);
		flow_tsf = curr_tsf + interval - rem;
		twt_agrt->twt = cpu_to_le64(flow_tsf);
	} else {
		list_add_tail(&flow->list, &dev->twt_list);
	}
	flow->tsf = le64_to_cpu(twt_agrt->twt);

	if (mt7915_mcu_twt_agrt_update(dev, msta->vif, flow, MCU_TWT_AGRT_ADD))
		goto unlock;

	setup_cmd = TWT_SETUP_CMD_ACCEPT;
	dev->twt.table_mask |= BIT(table_id);
	msta->twt.flowid_mask |= BIT(flowid);
	dev->twt.n_agrt++;

unlock:
	mutex_unlock(&dev->mt76.mutex);
out:
	twt_agrt->req_type &= ~cpu_to_le16(IEEE80211_TWT_REQTYPE_SETUP_CMD);
	twt_agrt->req_type |=
		le16_encode_bits(setup_cmd, IEEE80211_TWT_REQTYPE_SETUP_CMD);
	twt->control = (twt->control & IEEE80211_TWT_CONTROL_WAKE_DUR_UNIT) |
		       (twt->control & IEEE80211_TWT_CONTROL_RX_DISABLED);
}

void mt7915_mac_twt_teardown_flow(struct mt7915_dev *dev,
				  struct mt7915_sta *msta,
				  u8 flowid)
{
	struct mt7915_twt_flow *flow;

	lockdep_assert_held(&dev->mt76.mutex);

	if (flowid >= ARRAY_SIZE(msta->twt.flow))
		return;

	if (!(msta->twt.flowid_mask & BIT(flowid)))
		return;

	flow = &msta->twt.flow[flowid];
	if (mt7915_mcu_twt_agrt_update(dev, msta->vif, flow,
				       MCU_TWT_AGRT_DELETE))
		return;

	list_del_init(&flow->list);
	msta->twt.flowid_mask &= ~BIT(flowid);
	dev->twt.table_mask &= ~BIT(flow->table_id);
	dev->twt.n_agrt--;
}
