// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc. */

#include <linux/devcoredump.h>
#include <linux/etherdevice.h>
#include <linux/timekeeping.h>
#include "mt7921.h"
#include "../dma.h"
#include "mac.h"
#include "mcu.h"

static struct mt76_wcid *mt7921_rx_get_wcid(struct mt7921_dev *dev,
					    u16 idx, bool unicast)
{
	struct mt7921_sta *sta;
	struct mt76_wcid *wcid;

	if (idx >= ARRAY_SIZE(dev->mt76.wcid))
		return NULL;

	wcid = rcu_dereference(dev->mt76.wcid[idx]);
	if (unicast || !wcid)
		return wcid;

	if (!wcid->sta)
		return NULL;

	sta = container_of(wcid, struct mt7921_sta, wcid);
	if (!sta->vif)
		return NULL;

	return &sta->vif->sta.wcid;
}

void mt7921_sta_ps(struct mt76_dev *mdev, struct ieee80211_sta *sta, bool ps)
{
}
EXPORT_SYMBOL_GPL(mt7921_sta_ps);

bool mt7921_mac_wtbl_update(struct mt7921_dev *dev, int idx, u32 mask)
{
	mt76_rmw(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_WLAN_IDX,
		 FIELD_PREP(MT_WTBL_UPDATE_WLAN_IDX, idx) | mask);

	return mt76_poll(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_BUSY,
			 0, 5000);
}

void mt7921_mac_sta_poll(struct mt7921_dev *dev)
{
	static const u8 ac_to_tid[] = {
		[IEEE80211_AC_BE] = 0,
		[IEEE80211_AC_BK] = 1,
		[IEEE80211_AC_VI] = 4,
		[IEEE80211_AC_VO] = 6
	};
	struct ieee80211_sta *sta;
	struct mt7921_sta *msta;
	u32 tx_time[IEEE80211_NUM_ACS], rx_time[IEEE80211_NUM_ACS];
	LIST_HEAD(sta_poll_list);
	struct rate_info *rate;
	int i;

	spin_lock_bh(&dev->sta_poll_lock);
	list_splice_init(&dev->sta_poll_list, &sta_poll_list);
	spin_unlock_bh(&dev->sta_poll_lock);

	while (true) {
		bool clear = false;
		u32 addr, val;
		u16 idx;
		u8 bw;

		spin_lock_bh(&dev->sta_poll_lock);
		if (list_empty(&sta_poll_list)) {
			spin_unlock_bh(&dev->sta_poll_lock);
			break;
		}
		msta = list_first_entry(&sta_poll_list,
					struct mt7921_sta, poll_list);
		list_del_init(&msta->poll_list);
		spin_unlock_bh(&dev->sta_poll_lock);

		idx = msta->wcid.idx;
		addr = mt7921_mac_wtbl_lmac_addr(idx, MT_WTBL_AC0_CTT_OFFSET);

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
			mt7921_mac_wtbl_update(dev, idx,
					       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);
			memset(msta->airtime_ac, 0, sizeof(msta->airtime_ac));
		}

		if (!msta->wcid.sta)
			continue;

		sta = container_of((void *)msta, struct ieee80211_sta,
				   drv_priv);
		for (i = 0; i < IEEE80211_NUM_ACS; i++) {
			u8 q = mt76_connac_lmac_mapping(i);
			u32 tx_cur = tx_time[q];
			u32 rx_cur = rx_time[q];
			u8 tid = ac_to_tid[i];

			if (!tx_cur && !rx_cur)
				continue;

			ieee80211_sta_register_airtime(sta, tid, tx_cur,
						       rx_cur);
		}

		/* We don't support reading GI info from txs packets.
		 * For accurate tx status reporting and AQL improvement,
		 * we need to make sure that flags match so polling GI
		 * from per-sta counters directly.
		 */
		rate = &msta->wcid.rate;
		addr = mt7921_mac_wtbl_lmac_addr(idx,
						 MT_WTBL_TXRX_CAP_RATE_OFFSET);
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
			u8 offs = MT_WTBL_TXRX_RATE_G2_HE + 2 * bw;

			rate->he_gi = (val & (0x3 << offs)) >> offs;
		} else if (rate->flags &
			   (RATE_INFO_FLAGS_VHT_MCS | RATE_INFO_FLAGS_MCS)) {
			if (val & BIT(MT_WTBL_TXRX_RATE_G2 + bw))
				rate->flags |= RATE_INFO_FLAGS_SHORT_GI;
			else
				rate->flags &= ~RATE_INFO_FLAGS_SHORT_GI;
		}
	}
}
EXPORT_SYMBOL_GPL(mt7921_mac_sta_poll);

static void
mt7921_get_status_freq_info(struct mt7921_dev *dev, struct mt76_phy *mphy,
			    struct mt76_rx_status *status, u8 chfreq)
{
	if (!test_bit(MT76_HW_SCANNING, &mphy->state) &&
	    !test_bit(MT76_HW_SCHED_SCANNING, &mphy->state) &&
	    !test_bit(MT76_STATE_ROC, &mphy->state)) {
		status->freq = mphy->chandef.chan->center_freq;
		status->band = mphy->chandef.chan->band;
		return;
	}

	if (chfreq > 180) {
		status->band = NL80211_BAND_6GHZ;
		chfreq = (chfreq - 181) * 4 + 1;
	} else if (chfreq > 14) {
		status->band = NL80211_BAND_5GHZ;
	} else {
		status->band = NL80211_BAND_2GHZ;
	}
	status->freq = ieee80211_channel_to_frequency(chfreq, status->band);
}

static void
mt7921_mac_rssi_iter(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	struct sk_buff *skb = priv;
	struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;
	struct ieee80211_hdr *hdr = mt76_skb_get_hdr(skb);

	if (status->signal > 0)
		return;

	if (!ether_addr_equal(vif->addr, hdr->addr1))
		return;

	ewma_rssi_add(&mvif->rssi, -status->signal);
}

static void
mt7921_mac_assoc_rssi(struct mt7921_dev *dev, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = mt76_skb_get_hdr(skb);

	if (!ieee80211_is_assoc_resp(hdr->frame_control) &&
	    !ieee80211_is_auth(hdr->frame_control))
		return;

	ieee80211_iterate_active_interfaces_atomic(mt76_hw(dev),
		IEEE80211_IFACE_ITER_RESUME_ALL,
		mt7921_mac_rssi_iter, skb);
}

static int
mt7921_mac_fill_rx(struct mt7921_dev *dev, struct sk_buff *skb)
{
	u32 csum_mask = MT_RXD0_NORMAL_IP_SUM | MT_RXD0_NORMAL_UDP_TCP_SUM;
	struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
	bool hdr_trans, unicast, insert_ccmp_hdr = false;
	u8 chfreq, qos_ctl = 0, remove_pad, amsdu_info;
	u16 hdr_gap;
	__le32 *rxv = NULL, *rxd = (__le32 *)skb->data;
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt7921_phy *phy = &dev->phy;
	struct ieee80211_supported_band *sband;
	u32 csum_status = *(u32 *)skb->cb;
	u32 rxd0 = le32_to_cpu(rxd[0]);
	u32 rxd1 = le32_to_cpu(rxd[1]);
	u32 rxd2 = le32_to_cpu(rxd[2]);
	u32 rxd3 = le32_to_cpu(rxd[3]);
	u32 rxd4 = le32_to_cpu(rxd[4]);
	struct mt7921_sta *msta = NULL;
	u16 seq_ctrl = 0;
	__le16 fc = 0;
	u8 mode = 0;
	int i, idx;

	memset(status, 0, sizeof(*status));

	if (rxd1 & MT_RXD1_NORMAL_BAND_IDX)
		return -EINVAL;

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

	chfreq = FIELD_GET(MT_RXD3_NORMAL_CH_FREQ, rxd3);
	unicast = FIELD_GET(MT_RXD3_NORMAL_ADDR_TYPE, rxd3) == MT_RXD3_NORMAL_U2M;
	idx = FIELD_GET(MT_RXD1_NORMAL_WLAN_IDX, rxd1);
	status->wcid = mt7921_rx_get_wcid(dev, idx, unicast);

	if (status->wcid) {
		msta = container_of(status->wcid, struct mt7921_sta, wcid);
		spin_lock_bh(&dev->sta_poll_lock);
		if (list_empty(&msta->poll_list))
			list_add_tail(&msta->poll_list, &dev->sta_poll_list);
		spin_unlock_bh(&dev->sta_poll_lock);
	}

	mt7921_get_status_freq_info(dev, mphy, status, chfreq);

	switch (status->band) {
	case NL80211_BAND_5GHZ:
		sband = &mphy->sband_5g.sband;
		break;
	case NL80211_BAND_6GHZ:
		sband = &mphy->sband_6g.sband;
		break;
	default:
		sband = &mphy->sband_2g.sband;
		break;
	}

	if (!sband->channels)
		return -EINVAL;

	if (mt76_is_mmio(&dev->mt76) && (rxd0 & csum_mask) == csum_mask &&
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
		seq_ctrl = FIELD_GET(MT_RXD8_SEQ_CTRL, v2);
		qos_ctl = FIELD_GET(MT_RXD8_QOS_CTL, v2);

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

		ret = mt76_connac2_mac_fill_rx_rate(&dev->mt76, status, sband,
						    rxv, &mode);
		if (ret < 0)
			return ret;

		if (rxd1 & MT_RXD1_NORMAL_GROUP_5) {
			rxd += 6;
			if ((u8 *)rxd - skb->data >= skb->len)
				return -EINVAL;

			rxv = rxd;
			/* Monitor mode would use RCPI described in GROUP 5
			 * instead.
			 */
			v1 = le32_to_cpu(rxv[0]);

			rxd += 12;
			if ((u8 *)rxd - skb->data >= skb->len)
				return -EINVAL;
		}

		status->chains = mphy->antenna_mask;
		status->chain_signal[0] = to_rssi(MT_PRXV_RCPI0, v1);
		status->chain_signal[1] = to_rssi(MT_PRXV_RCPI1, v1);
		status->chain_signal[2] = to_rssi(MT_PRXV_RCPI2, v1);
		status->chain_signal[3] = to_rssi(MT_PRXV_RCPI3, v1);
		status->signal = -128;
		for (i = 0; i < hweight8(mphy->antenna_mask); i++) {
			if (!(status->chains & BIT(i)) ||
			    status->chain_signal[i] >= 0)
				continue;

			status->signal = max(status->signal,
					     status->chain_signal[i]);
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
		skb_pull(skb, hdr_gap);
		if (!hdr_trans && status->amsdu) {
			memmove(skb->data + 2, skb->data,
				ieee80211_get_hdrlen_from_skb(skb));
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
	}

	mt7921_mac_assoc_rssi(dev, skb);

	if (rxv && mode >= MT_PHY_TYPE_HE_SU && !(status->flag & RX_FLAG_8023))
		mt76_connac2_mac_decode_he_radiotap(&dev->mt76, skb, rxv, mode);

	if (!status->wcid || !ieee80211_is_data_qos(fc))
		return 0;

	status->aggr = unicast && !ieee80211_is_qos_nullfunc(fc);
	status->seqno = IEEE80211_SEQ_TO_SN(seq_ctrl);
	status->qos_ctl = qos_ctl;

	return 0;
}

static void mt7921_tx_check_aggr(struct ieee80211_sta *sta, __le32 *txwi)
{
	struct mt7921_sta *msta;
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

	msta = (struct mt7921_sta *)sta->drv_priv;
	if (!test_and_set_bit(tid, &msta->ampdu_state))
		ieee80211_start_tx_ba_session(sta, tid, 0);
}

void mt7921_mac_add_txs(struct mt7921_dev *dev, void *data)
{
	struct mt7921_sta *msta = NULL;
	struct mt76_wcid *wcid;
	__le32 *txs_data = data;
	u16 wcidx;
	u8 pid;

	if (le32_get_bits(txs_data[0], MT_TXS0_TXS_FORMAT) > 1)
		return;

	wcidx = le32_get_bits(txs_data[2], MT_TXS2_WCID);
	pid = le32_get_bits(txs_data[3], MT_TXS3_PID);

	if (pid < MT_PACKET_ID_FIRST)
		return;

	if (wcidx >= MT7921_WTBL_SIZE)
		return;

	rcu_read_lock();

	wcid = rcu_dereference(dev->mt76.wcid[wcidx]);
	if (!wcid)
		goto out;

	msta = container_of(wcid, struct mt7921_sta, wcid);

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

void mt7921_txwi_free(struct mt7921_dev *dev, struct mt76_txwi_cache *t,
		      struct ieee80211_sta *sta, bool clear_status,
		      struct list_head *free_list)
{
	struct mt76_dev *mdev = &dev->mt76;
	__le32 *txwi;
	u16 wcid_idx;

	mt76_connac_txp_skb_unmap(mdev, t);
	if (!t->skb)
		goto out;

	txwi = (__le32 *)mt76_get_txwi_ptr(mdev, t);
	if (sta) {
		struct mt76_wcid *wcid = (struct mt76_wcid *)sta->drv_priv;

		if (likely(t->skb->protocol != cpu_to_be16(ETH_P_PAE)))
			mt7921_tx_check_aggr(sta, txwi);

		wcid_idx = wcid->idx;
	} else {
		wcid_idx = le32_get_bits(txwi[1], MT_TXD1_WLAN_IDX);
	}

	__mt76_tx_complete_skb(mdev, wcid_idx, t->skb, free_list);
out:
	t->skb = NULL;
	mt76_put_txwi(mdev, t);
}
EXPORT_SYMBOL_GPL(mt7921_txwi_free);

static void mt7921_mac_tx_free(struct mt7921_dev *dev, void *data, int len)
{
	struct mt76_connac_tx_free *free = data;
	__le32 *tx_info = (__le32 *)(data + sizeof(*free));
	struct mt76_dev *mdev = &dev->mt76;
	struct mt76_txwi_cache *txwi;
	struct ieee80211_sta *sta = NULL;
	struct sk_buff *skb, *tmp;
	void *end = data + len;
	LIST_HEAD(free_list);
	bool wake = false;
	u8 i, count;

	/* clean DMA queues and unmap buffers first */
	mt76_queue_tx_cleanup(dev, dev->mphy.q_tx[MT_TXQ_PSD], false);
	mt76_queue_tx_cleanup(dev, dev->mphy.q_tx[MT_TXQ_BE], false);

	count = le16_get_bits(free->ctrl, MT_TX_FREE_MSDU_CNT);
	if (WARN_ON_ONCE((void *)&tx_info[count] > end))
		return;

	for (i = 0; i < count; i++) {
		u32 msdu, info = le32_to_cpu(tx_info[i]);
		u8 stat;

		/* 1'b1: new wcid pair.
		 * 1'b0: msdu_id with the same 'wcid pair' as above.
		 */
		if (info & MT_TX_FREE_PAIR) {
			struct mt7921_sta *msta;
			struct mt76_wcid *wcid;
			u16 idx;

			count++;
			idx = FIELD_GET(MT_TX_FREE_WLAN_ID, info);
			wcid = rcu_dereference(dev->mt76.wcid[idx]);
			sta = wcid_to_sta(wcid);
			if (!sta)
				continue;

			msta = container_of(wcid, struct mt7921_sta, wcid);
			spin_lock_bh(&dev->sta_poll_lock);
			if (list_empty(&msta->poll_list))
				list_add_tail(&msta->poll_list, &dev->sta_poll_list);
			spin_unlock_bh(&dev->sta_poll_lock);
			continue;
		}

		msdu = FIELD_GET(MT_TX_FREE_MSDU_ID, info);
		stat = FIELD_GET(MT_TX_FREE_STATUS, info);

		txwi = mt76_token_release(mdev, msdu, &wake);
		if (!txwi)
			continue;

		mt7921_txwi_free(dev, txwi, sta, stat, &free_list);
	}

	if (wake)
		mt76_set_tx_blocked(&dev->mt76, false);

	list_for_each_entry_safe(skb, tmp, &free_list, list) {
		skb_list_del_init(skb);
		napi_consume_skb(skb, 1);
	}

	rcu_read_lock();
	mt7921_mac_sta_poll(dev);
	rcu_read_unlock();

	mt76_worker_schedule(&dev->mt76.tx_worker);
}

bool mt7921_rx_check(struct mt76_dev *mdev, void *data, int len)
{
	struct mt7921_dev *dev = container_of(mdev, struct mt7921_dev, mt76);
	__le32 *rxd = (__le32 *)data;
	__le32 *end = (__le32 *)&rxd[len / 4];
	enum rx_pkt_type type;

	type = le32_get_bits(rxd[0], MT_RXD0_PKT_TYPE);

	switch (type) {
	case PKT_TYPE_TXRX_NOTIFY:
		/* PKT_TYPE_TXRX_NOTIFY can be received only by mmio devices */
		mt7921_mac_tx_free(dev, data, len); /* mmio */
		return false;
	case PKT_TYPE_TXS:
		for (rxd += 2; rxd + 8 <= end; rxd += 8)
			mt7921_mac_add_txs(dev, rxd);
		return false;
	default:
		return true;
	}
}
EXPORT_SYMBOL_GPL(mt7921_rx_check);

void mt7921_queue_rx_skb(struct mt76_dev *mdev, enum mt76_rxq_id q,
			 struct sk_buff *skb)
{
	struct mt7921_dev *dev = container_of(mdev, struct mt7921_dev, mt76);
	__le32 *rxd = (__le32 *)skb->data;
	__le32 *end = (__le32 *)&skb->data[skb->len];
	enum rx_pkt_type type;
	u16 flag;

	type = le32_get_bits(rxd[0], MT_RXD0_PKT_TYPE);
	flag = le32_get_bits(rxd[0], MT_RXD0_PKT_FLAG);

	if (type == PKT_TYPE_RX_EVENT && flag == 0x1)
		type = PKT_TYPE_NORMAL_MCU;

	switch (type) {
	case PKT_TYPE_TXRX_NOTIFY:
		/* PKT_TYPE_TXRX_NOTIFY can be received only by mmio devices */
		mt7921_mac_tx_free(dev, skb->data, skb->len);
		napi_consume_skb(skb, 1);
		break;
	case PKT_TYPE_RX_EVENT:
		mt7921_mcu_rx_event(dev, skb);
		break;
	case PKT_TYPE_TXS:
		for (rxd += 2; rxd + 8 <= end; rxd += 8)
			mt7921_mac_add_txs(dev, rxd);
		dev_kfree_skb(skb);
		break;
	case PKT_TYPE_NORMAL_MCU:
	case PKT_TYPE_NORMAL:
		if (!mt7921_mac_fill_rx(dev, skb)) {
			mt76_rx(&dev->mt76, q, skb);
			return;
		}
		fallthrough;
	default:
		dev_kfree_skb(skb);
		break;
	}
}
EXPORT_SYMBOL_GPL(mt7921_queue_rx_skb);

void mt7921_mac_reset_counters(struct mt7921_phy *phy)
{
	struct mt7921_dev *dev = phy->dev;
	int i;

	for (i = 0; i < 4; i++) {
		mt76_rr(dev, MT_TX_AGG_CNT(0, i));
		mt76_rr(dev, MT_TX_AGG_CNT2(0, i));
	}

	dev->mt76.phy.survey_time = ktime_get_boottime();
	memset(&dev->mt76.aggr_stats[0], 0, sizeof(dev->mt76.aggr_stats) / 2);

	/* reset airtime counters */
	mt76_rr(dev, MT_MIB_SDR9(0));
	mt76_rr(dev, MT_MIB_SDR36(0));
	mt76_rr(dev, MT_MIB_SDR37(0));

	mt76_set(dev, MT_WF_RMAC_MIB_TIME0(0), MT_WF_RMAC_MIB_RXTIME_CLR);
	mt76_set(dev, MT_WF_RMAC_MIB_AIRTIME0(0), MT_WF_RMAC_MIB_RXTIME_CLR);
}

void mt7921_mac_set_timing(struct mt7921_phy *phy)
{
	s16 coverage_class = phy->coverage_class;
	struct mt7921_dev *dev = phy->dev;
	u32 val, reg_offset;
	u32 cck = FIELD_PREP(MT_TIMEOUT_VAL_PLCP, 231) |
		  FIELD_PREP(MT_TIMEOUT_VAL_CCA, 48);
	u32 ofdm = FIELD_PREP(MT_TIMEOUT_VAL_PLCP, 60) |
		   FIELD_PREP(MT_TIMEOUT_VAL_CCA, 28);
	bool is_2ghz = phy->mt76->chandef.chan->band == NL80211_BAND_2GHZ;
	int sifs = is_2ghz ? 10 : 16, offset;

	if (!test_bit(MT76_STATE_RUNNING, &phy->mt76->state))
		return;

	mt76_set(dev, MT_ARB_SCR(0),
		 MT_ARB_SCR_TX_DISABLE | MT_ARB_SCR_RX_DISABLE);
	udelay(1);

	offset = 3 * coverage_class;
	reg_offset = FIELD_PREP(MT_TIMEOUT_VAL_PLCP, offset) |
		     FIELD_PREP(MT_TIMEOUT_VAL_CCA, offset);

	mt76_wr(dev, MT_TMAC_CDTR(0), cck + reg_offset);
	mt76_wr(dev, MT_TMAC_ODTR(0), ofdm + reg_offset);
	mt76_wr(dev, MT_TMAC_ICR0(0),
		FIELD_PREP(MT_IFS_EIFS, 360) |
		FIELD_PREP(MT_IFS_RIFS, 2) |
		FIELD_PREP(MT_IFS_SIFS, sifs) |
		FIELD_PREP(MT_IFS_SLOT, phy->slottime));

	if (phy->slottime < 20 || !is_2ghz)
		val = MT7921_CFEND_RATE_DEFAULT;
	else
		val = MT7921_CFEND_RATE_11B;

	mt76_rmw_field(dev, MT_AGG_ACR0(0), MT_AGG_ACR_CFEND_RATE, val);
	mt76_clear(dev, MT_ARB_SCR(0),
		   MT_ARB_SCR_TX_DISABLE | MT_ARB_SCR_RX_DISABLE);
}

static u8
mt7921_phy_get_nf(struct mt7921_phy *phy, int idx)
{
	return 0;
}

static void
mt7921_phy_update_channel(struct mt76_phy *mphy, int idx)
{
	struct mt7921_dev *dev = container_of(mphy->dev, struct mt7921_dev, mt76);
	struct mt7921_phy *phy = (struct mt7921_phy *)mphy->priv;
	struct mt76_channel_state *state;
	u64 busy_time, tx_time, rx_time, obss_time;
	int nf;

	busy_time = mt76_get_field(dev, MT_MIB_SDR9(idx),
				   MT_MIB_SDR9_BUSY_MASK);
	tx_time = mt76_get_field(dev, MT_MIB_SDR36(idx),
				 MT_MIB_SDR36_TXTIME_MASK);
	rx_time = mt76_get_field(dev, MT_MIB_SDR37(idx),
				 MT_MIB_SDR37_RXTIME_MASK);
	obss_time = mt76_get_field(dev, MT_WF_RMAC_MIB_AIRTIME14(idx),
				   MT_MIB_OBSSTIME_MASK);

	nf = mt7921_phy_get_nf(phy, idx);
	if (!phy->noise)
		phy->noise = nf << 4;
	else if (nf)
		phy->noise += nf - (phy->noise >> 4);

	state = mphy->chan_state;
	state->cc_busy += busy_time;
	state->cc_tx += tx_time;
	state->cc_rx += rx_time + obss_time;
	state->cc_bss_rx += rx_time;
	state->noise = -(phy->noise >> 4);
}

void mt7921_update_channel(struct mt76_phy *mphy)
{
	struct mt7921_dev *dev = container_of(mphy->dev, struct mt7921_dev, mt76);

	if (mt76_connac_pm_wake(mphy, &dev->pm))
		return;

	mt7921_phy_update_channel(mphy, 0);
	/* reset obss airtime */
	mt76_set(dev, MT_WF_RMAC_MIB_TIME0(0), MT_WF_RMAC_MIB_RXTIME_CLR);

	mt76_connac_power_save_sched(mphy, &dev->pm);
}
EXPORT_SYMBOL_GPL(mt7921_update_channel);

static void
mt7921_vif_connect_iter(void *priv, u8 *mac,
			struct ieee80211_vif *vif)
{
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;
	struct mt7921_dev *dev = mvif->phy->dev;
	struct ieee80211_hw *hw = mt76_hw(dev);

	if (vif->type == NL80211_IFTYPE_STATION)
		ieee80211_disconnect(vif, true);

	mt76_connac_mcu_uni_add_dev(&dev->mphy, vif, &mvif->sta.wcid, true);
	mt7921_mcu_set_tx(dev, vif);

	if (vif->type == NL80211_IFTYPE_AP) {
		mt76_connac_mcu_uni_add_bss(dev->phy.mt76, vif, &mvif->sta.wcid,
					    true);
		mt7921_mcu_sta_update(dev, NULL, vif, true,
				      MT76_STA_INFO_STATE_NONE);
		mt7921_mcu_uni_add_beacon_offload(dev, hw, vif, true);
	}
}

/* system error recovery */
void mt7921_mac_reset_work(struct work_struct *work)
{
	struct mt7921_dev *dev = container_of(work, struct mt7921_dev,
					      reset_work);
	struct ieee80211_hw *hw = mt76_hw(dev);
	struct mt76_connac_pm *pm = &dev->pm;
	int i, ret;

	dev_dbg(dev->mt76.dev, "chip reset\n");
	dev->hw_full_reset = true;
	ieee80211_stop_queues(hw);

	cancel_delayed_work_sync(&dev->mphy.mac_work);
	cancel_delayed_work_sync(&pm->ps_work);
	cancel_work_sync(&pm->wake_work);

	for (i = 0; i < 10; i++) {
		mutex_lock(&dev->mt76.mutex);
		ret = mt7921_dev_reset(dev);
		mutex_unlock(&dev->mt76.mutex);

		if (!ret)
			break;
	}

	if (i == 10)
		dev_err(dev->mt76.dev, "chip reset failed\n");

	if (test_and_clear_bit(MT76_HW_SCANNING, &dev->mphy.state)) {
		struct cfg80211_scan_info info = {
			.aborted = true,
		};

		ieee80211_scan_completed(dev->mphy.hw, &info);
	}

	dev->hw_full_reset = false;
	pm->suspended = false;
	ieee80211_wake_queues(hw);
	ieee80211_iterate_active_interfaces(hw,
					    IEEE80211_IFACE_ITER_RESUME_ALL,
					    mt7921_vif_connect_iter, NULL);
	mt76_connac_power_save_sched(&dev->mt76.phy, pm);
}

void mt7921_reset(struct mt76_dev *mdev)
{
	struct mt7921_dev *dev = container_of(mdev, struct mt7921_dev, mt76);
	struct mt76_connac_pm *pm = &dev->pm;

	if (!dev->hw_init_done)
		return;

	if (dev->hw_full_reset)
		return;

	if (pm->suspended)
		return;

	queue_work(dev->mt76.wq, &dev->reset_work);
}
EXPORT_SYMBOL_GPL(mt7921_reset);

void mt7921_mac_update_mib_stats(struct mt7921_phy *phy)
{
	struct mt7921_dev *dev = phy->dev;
	struct mib_stats *mib = &phy->mib;
	int i, aggr0 = 0, aggr1;
	u32 val;

	mib->fcs_err_cnt += mt76_get_field(dev, MT_MIB_SDR3(0),
					   MT_MIB_SDR3_FCS_ERR_MASK);
	mib->ack_fail_cnt += mt76_get_field(dev, MT_MIB_MB_BSDR3(0),
					    MT_MIB_ACK_FAIL_COUNT_MASK);
	mib->ba_miss_cnt += mt76_get_field(dev, MT_MIB_MB_BSDR2(0),
					   MT_MIB_BA_FAIL_COUNT_MASK);
	mib->rts_cnt += mt76_get_field(dev, MT_MIB_MB_BSDR0(0),
				       MT_MIB_RTS_COUNT_MASK);
	mib->rts_retries_cnt += mt76_get_field(dev, MT_MIB_MB_BSDR1(0),
					       MT_MIB_RTS_FAIL_COUNT_MASK);

	mib->tx_ampdu_cnt += mt76_rr(dev, MT_MIB_SDR12(0));
	mib->tx_mpdu_attempts_cnt += mt76_rr(dev, MT_MIB_SDR14(0));
	mib->tx_mpdu_success_cnt += mt76_rr(dev, MT_MIB_SDR15(0));

	val = mt76_rr(dev, MT_MIB_SDR32(0));
	mib->tx_pkt_ebf_cnt += FIELD_GET(MT_MIB_SDR9_EBF_CNT_MASK, val);
	mib->tx_pkt_ibf_cnt += FIELD_GET(MT_MIB_SDR9_IBF_CNT_MASK, val);

	val = mt76_rr(dev, MT_ETBF_TX_APP_CNT(0));
	mib->tx_bf_ibf_ppdu_cnt += FIELD_GET(MT_ETBF_TX_IBF_CNT, val);
	mib->tx_bf_ebf_ppdu_cnt += FIELD_GET(MT_ETBF_TX_EBF_CNT, val);

	val = mt76_rr(dev, MT_ETBF_RX_FB_CNT(0));
	mib->tx_bf_rx_fb_all_cnt += FIELD_GET(MT_ETBF_RX_FB_ALL, val);
	mib->tx_bf_rx_fb_he_cnt += FIELD_GET(MT_ETBF_RX_FB_HE, val);
	mib->tx_bf_rx_fb_vht_cnt += FIELD_GET(MT_ETBF_RX_FB_VHT, val);
	mib->tx_bf_rx_fb_ht_cnt += FIELD_GET(MT_ETBF_RX_FB_HT, val);

	mib->rx_mpdu_cnt += mt76_rr(dev, MT_MIB_SDR5(0));
	mib->rx_ampdu_cnt += mt76_rr(dev, MT_MIB_SDR22(0));
	mib->rx_ampdu_bytes_cnt += mt76_rr(dev, MT_MIB_SDR23(0));
	mib->rx_ba_cnt += mt76_rr(dev, MT_MIB_SDR31(0));

	for (i = 0; i < ARRAY_SIZE(mib->tx_amsdu); i++) {
		val = mt76_rr(dev, MT_PLE_AMSDU_PACK_MSDU_CNT(i));
		mib->tx_amsdu[i] += val;
		mib->tx_amsdu_cnt += val;
	}

	for (i = 0, aggr1 = aggr0 + 8; i < 4; i++) {
		u32 val2;

		val = mt76_rr(dev, MT_TX_AGG_CNT(0, i));
		val2 = mt76_rr(dev, MT_TX_AGG_CNT2(0, i));

		dev->mt76.aggr_stats[aggr0++] += val & 0xffff;
		dev->mt76.aggr_stats[aggr0++] += val >> 16;
		dev->mt76.aggr_stats[aggr1++] += val2 & 0xffff;
		dev->mt76.aggr_stats[aggr1++] += val2 >> 16;
	}
}

void mt7921_mac_work(struct work_struct *work)
{
	struct mt7921_phy *phy;
	struct mt76_phy *mphy;

	mphy = (struct mt76_phy *)container_of(work, struct mt76_phy,
					       mac_work.work);
	phy = mphy->priv;

	mt7921_mutex_acquire(phy->dev);

	mt76_update_survey(mphy);
	if (++mphy->mac_work_count == 2) {
		mphy->mac_work_count = 0;

		mt7921_mac_update_mib_stats(phy);
	}

	mt7921_mutex_release(phy->dev);

	mt76_tx_status_check(mphy->dev, false);
	ieee80211_queue_delayed_work(phy->mt76->hw, &mphy->mac_work,
				     MT7921_WATCHDOG_TIME);
}

void mt7921_pm_wake_work(struct work_struct *work)
{
	struct mt7921_dev *dev;
	struct mt76_phy *mphy;

	dev = (struct mt7921_dev *)container_of(work, struct mt7921_dev,
						pm.wake_work);
	mphy = dev->phy.mt76;

	if (!mt7921_mcu_drv_pmctrl(dev)) {
		struct mt76_dev *mdev = &dev->mt76;
		int i;

		if (mt76_is_sdio(mdev)) {
			mt76_connac_pm_dequeue_skbs(mphy, &dev->pm);
			mt76_worker_schedule(&mdev->sdio.txrx_worker);
		} else {
			local_bh_disable();
			mt76_for_each_q_rx(mdev, i)
				napi_schedule(&mdev->napi[i]);
			local_bh_enable();
			mt76_connac_pm_dequeue_skbs(mphy, &dev->pm);
			mt76_connac_tx_cleanup(mdev);
		}
		if (test_bit(MT76_STATE_RUNNING, &mphy->state))
			ieee80211_queue_delayed_work(mphy->hw, &mphy->mac_work,
						     MT7921_WATCHDOG_TIME);
	}

	ieee80211_wake_queues(mphy->hw);
	wake_up(&dev->pm.wait);
}

void mt7921_pm_power_save_work(struct work_struct *work)
{
	struct mt7921_dev *dev;
	unsigned long delta;
	struct mt76_phy *mphy;

	dev = (struct mt7921_dev *)container_of(work, struct mt7921_dev,
						pm.ps_work.work);
	mphy = dev->phy.mt76;

	delta = dev->pm.idle_timeout;
	if (test_bit(MT76_HW_SCANNING, &mphy->state) ||
	    test_bit(MT76_HW_SCHED_SCANNING, &mphy->state) ||
	    dev->fw_assert)
		goto out;

	if (mutex_is_locked(&dev->mt76.mutex))
		/* if mt76 mutex is held we should not put the device
		 * to sleep since we are currently accessing device
		 * register map. We need to wait for the next power_save
		 * trigger.
		 */
		goto out;

	if (time_is_after_jiffies(dev->pm.last_activity + delta)) {
		delta = dev->pm.last_activity + delta - jiffies;
		goto out;
	}

	if (!mt7921_mcu_fw_pmctrl(dev)) {
		cancel_delayed_work_sync(&mphy->mac_work);
		return;
	}
out:
	queue_delayed_work(dev->mt76.wq, &dev->pm.ps_work, delta);
}

void mt7921_coredump_work(struct work_struct *work)
{
	struct mt7921_dev *dev;
	char *dump, *data;

	dev = (struct mt7921_dev *)container_of(work, struct mt7921_dev,
						coredump.work.work);

	if (time_is_after_jiffies(dev->coredump.last_activity +
				  4 * MT76_CONNAC_COREDUMP_TIMEOUT)) {
		queue_delayed_work(dev->mt76.wq, &dev->coredump.work,
				   MT76_CONNAC_COREDUMP_TIMEOUT);
		return;
	}

	dump = vzalloc(MT76_CONNAC_COREDUMP_SZ);
	data = dump;

	while (true) {
		struct sk_buff *skb;

		spin_lock_bh(&dev->mt76.lock);
		skb = __skb_dequeue(&dev->coredump.msg_list);
		spin_unlock_bh(&dev->mt76.lock);

		if (!skb)
			break;

		skb_pull(skb, sizeof(struct mt76_connac2_mcu_rxd));
		if (!dump || data + skb->len - dump > MT76_CONNAC_COREDUMP_SZ) {
			dev_kfree_skb(skb);
			continue;
		}

		memcpy(data, skb->data, skb->len);
		data += skb->len;

		dev_kfree_skb(skb);
	}

	if (dump)
		dev_coredumpv(dev->mt76.dev, dump, MT76_CONNAC_COREDUMP_SZ,
			      GFP_KERNEL);

	mt7921_reset(&dev->mt76);
}

/* usb_sdio */
static void
mt7921_usb_sdio_write_txwi(struct mt7921_dev *dev, struct mt76_wcid *wcid,
			   enum mt76_txq_id qid, struct ieee80211_sta *sta,
			   struct ieee80211_key_conf *key, int pid,
			   struct sk_buff *skb)
{
	__le32 *txwi = (__le32 *)(skb->data - MT_SDIO_TXD_SIZE);

	memset(txwi, 0, MT_SDIO_TXD_SIZE);
	mt76_connac2_mac_write_txwi(&dev->mt76, txwi, skb, wcid, key, pid, qid, 0);
	skb_push(skb, MT_SDIO_TXD_SIZE);
}

int mt7921_usb_sdio_tx_prepare_skb(struct mt76_dev *mdev, void *txwi_ptr,
				   enum mt76_txq_id qid, struct mt76_wcid *wcid,
				   struct ieee80211_sta *sta,
				   struct mt76_tx_info *tx_info)
{
	struct mt7921_dev *dev = container_of(mdev, struct mt7921_dev, mt76);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx_info->skb);
	struct ieee80211_key_conf *key = info->control.hw_key;
	struct sk_buff *skb = tx_info->skb;
	int err, pad, pktid, type;

	if (unlikely(tx_info->skb->len <= ETH_HLEN))
		return -EINVAL;

	err = skb_cow_head(skb, MT_SDIO_TXD_SIZE + MT_SDIO_HDR_SIZE);
	if (err)
		return err;

	if (!wcid)
		wcid = &dev->mt76.global_wcid;

	if (sta) {
		struct mt7921_sta *msta = (struct mt7921_sta *)sta->drv_priv;

		if (time_after(jiffies, msta->last_txs + HZ / 4)) {
			info->flags |= IEEE80211_TX_CTL_REQ_TX_STATUS;
			msta->last_txs = jiffies;
		}
	}

	pktid = mt76_tx_status_skb_add(&dev->mt76, wcid, skb);
	mt7921_usb_sdio_write_txwi(dev, wcid, qid, sta, key, pktid, skb);

	type = mt76_is_sdio(mdev) ? MT7921_SDIO_DATA : 0;
	mt7921_skb_add_usb_sdio_hdr(dev, skb, type);
	pad = round_up(skb->len, 4) - skb->len;
	if (mt76_is_usb(mdev))
		pad += 4;

	err = mt76_skb_adjust_pad(skb, pad);
	if (err)
		/* Release pktid in case of error. */
		idr_remove(&wcid->pktid, pktid);

	return err;
}
EXPORT_SYMBOL_GPL(mt7921_usb_sdio_tx_prepare_skb);

void mt7921_usb_sdio_tx_complete_skb(struct mt76_dev *mdev,
				     struct mt76_queue_entry *e)
{
	__le32 *txwi = (__le32 *)(e->skb->data + MT_SDIO_HDR_SIZE);
	unsigned int headroom = MT_SDIO_TXD_SIZE + MT_SDIO_HDR_SIZE;
	struct ieee80211_sta *sta;
	struct mt76_wcid *wcid;
	u16 idx;

	idx = le32_get_bits(txwi[1], MT_TXD1_WLAN_IDX);
	wcid = rcu_dereference(mdev->wcid[idx]);
	sta = wcid_to_sta(wcid);

	if (sta && likely(e->skb->protocol != cpu_to_be16(ETH_P_PAE)))
		mt7921_tx_check_aggr(sta, txwi);

	skb_pull(e->skb, headroom);
	mt76_tx_complete_skb(mdev, e->wcid, e->skb);
}
EXPORT_SYMBOL_GPL(mt7921_usb_sdio_tx_complete_skb);

bool mt7921_usb_sdio_tx_status_data(struct mt76_dev *mdev, u8 *update)
{
	struct mt7921_dev *dev = container_of(mdev, struct mt7921_dev, mt76);

	mt7921_mutex_acquire(dev);
	mt7921_mac_sta_poll(dev);
	mt7921_mutex_release(dev);

	return false;
}
EXPORT_SYMBOL_GPL(mt7921_usb_sdio_tx_status_data);

#if IS_ENABLED(CONFIG_IPV6)
void mt7921_set_ipv6_ns_work(struct work_struct *work)
{
	struct mt7921_dev *dev = container_of(work, struct mt7921_dev,
						ipv6_ns_work);
	struct sk_buff *skb;
	int ret = 0;

	do {
		skb = skb_dequeue(&dev->ipv6_ns_list);

		if (!skb)
			break;

		mt7921_mutex_acquire(dev);
		ret = mt76_mcu_skb_send_msg(&dev->mt76, skb,
					    MCU_UNI_CMD(OFFLOAD), true);
		mt7921_mutex_release(dev);

	} while (!ret);

	if (ret)
		skb_queue_purge(&dev->ipv6_ns_list);
}
#endif
