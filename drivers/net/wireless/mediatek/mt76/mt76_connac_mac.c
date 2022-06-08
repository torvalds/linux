// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc. */

#include "mt76_connac.h"
#include "mt76_connac2_mac.h"

#define HE_BITS(f)		cpu_to_le16(IEEE80211_RADIOTAP_HE_##f)
#define HE_PREP(f, m, v)	le16_encode_bits(le32_get_bits(v, MT_CRXV_HE_##m),\
						 IEEE80211_RADIOTAP_HE_##f)

int mt76_connac_pm_wake(struct mt76_phy *phy, struct mt76_connac_pm *pm)
{
	struct mt76_dev *dev = phy->dev;

	if (mt76_is_usb(dev))
		return 0;

	cancel_delayed_work_sync(&pm->ps_work);
	if (!test_bit(MT76_STATE_PM, &phy->state))
		return 0;

	if (pm->suspended)
		return 0;

	queue_work(dev->wq, &pm->wake_work);
	if (!wait_event_timeout(pm->wait,
				!test_bit(MT76_STATE_PM, &phy->state),
				3 * HZ)) {
		ieee80211_wake_queues(phy->hw);
		return -ETIMEDOUT;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mt76_connac_pm_wake);

void mt76_connac_power_save_sched(struct mt76_phy *phy,
				  struct mt76_connac_pm *pm)
{
	struct mt76_dev *dev = phy->dev;

	if (mt76_is_usb(dev))
		return;

	if (!pm->enable)
		return;

	if (pm->suspended)
		return;

	pm->last_activity = jiffies;

	if (!test_bit(MT76_STATE_PM, &phy->state)) {
		cancel_delayed_work(&phy->mac_work);
		queue_delayed_work(dev->wq, &pm->ps_work, pm->idle_timeout);
	}
}
EXPORT_SYMBOL_GPL(mt76_connac_power_save_sched);

void mt76_connac_free_pending_tx_skbs(struct mt76_connac_pm *pm,
				      struct mt76_wcid *wcid)
{
	int i;

	spin_lock_bh(&pm->txq_lock);
	for (i = 0; i < IEEE80211_NUM_ACS; i++) {
		if (wcid && pm->tx_q[i].wcid != wcid)
			continue;

		dev_kfree_skb(pm->tx_q[i].skb);
		pm->tx_q[i].skb = NULL;
	}
	spin_unlock_bh(&pm->txq_lock);
}
EXPORT_SYMBOL_GPL(mt76_connac_free_pending_tx_skbs);

void mt76_connac_pm_queue_skb(struct ieee80211_hw *hw,
			      struct mt76_connac_pm *pm,
			      struct mt76_wcid *wcid,
			      struct sk_buff *skb)
{
	int qid = skb_get_queue_mapping(skb);
	struct mt76_phy *phy = hw->priv;

	spin_lock_bh(&pm->txq_lock);
	if (!pm->tx_q[qid].skb) {
		ieee80211_stop_queues(hw);
		pm->tx_q[qid].wcid = wcid;
		pm->tx_q[qid].skb = skb;
		queue_work(phy->dev->wq, &pm->wake_work);
	} else {
		dev_kfree_skb(skb);
	}
	spin_unlock_bh(&pm->txq_lock);
}
EXPORT_SYMBOL_GPL(mt76_connac_pm_queue_skb);

void mt76_connac_pm_dequeue_skbs(struct mt76_phy *phy,
				 struct mt76_connac_pm *pm)
{
	int i;

	spin_lock_bh(&pm->txq_lock);
	for (i = 0; i < IEEE80211_NUM_ACS; i++) {
		struct mt76_wcid *wcid = pm->tx_q[i].wcid;
		struct ieee80211_sta *sta = NULL;

		if (!pm->tx_q[i].skb)
			continue;

		if (wcid && wcid->sta)
			sta = container_of((void *)wcid, struct ieee80211_sta,
					   drv_priv);

		mt76_tx(phy, sta, wcid, pm->tx_q[i].skb);
		pm->tx_q[i].skb = NULL;
	}
	spin_unlock_bh(&pm->txq_lock);

	mt76_worker_schedule(&phy->dev->tx_worker);
}
EXPORT_SYMBOL_GPL(mt76_connac_pm_dequeue_skbs);

static u16
mt76_connac2_mac_tx_rate_val(struct mt76_phy *mphy, struct ieee80211_vif *vif,
			     bool beacon, bool mcast)
{
	u8 mode = 0, band = mphy->chandef.chan->band;
	int rateidx = 0, mcast_rate;

	if (!vif)
		goto legacy;

	if (is_mt7921(mphy->dev)) {
		rateidx = ffs(vif->bss_conf.basic_rates) - 1;
		goto legacy;
	}

	if (beacon) {
		struct cfg80211_bitrate_mask *mask;

		mask = &vif->bss_conf.beacon_tx_rate;
		if (hweight16(mask->control[band].he_mcs[0]) == 1) {
			rateidx = ffs(mask->control[band].he_mcs[0]) - 1;
			mode = MT_PHY_TYPE_HE_SU;
			goto out;
		} else if (hweight16(mask->control[band].vht_mcs[0]) == 1) {
			rateidx = ffs(mask->control[band].vht_mcs[0]) - 1;
			mode = MT_PHY_TYPE_VHT;
			goto out;
		} else if (hweight8(mask->control[band].ht_mcs[0]) == 1) {
			rateidx = ffs(mask->control[band].ht_mcs[0]) - 1;
			mode = MT_PHY_TYPE_HT;
			goto out;
		} else if (hweight32(mask->control[band].legacy) == 1) {
			rateidx = ffs(mask->control[band].legacy) - 1;
			goto legacy;
		}
	}

	mcast_rate = vif->bss_conf.mcast_rate[band];
	if (mcast && mcast_rate > 0)
		rateidx = mcast_rate - 1;
	else
		rateidx = ffs(vif->bss_conf.basic_rates) - 1;

legacy:
	rateidx = mt76_calculate_default_rate(mphy, rateidx);
	mode = rateidx >> 8;
	rateidx &= GENMASK(7, 0);

out:
	return FIELD_PREP(MT_TX_RATE_IDX, rateidx) |
	       FIELD_PREP(MT_TX_RATE_MODE, mode);
}

static void
mt76_connac2_mac_write_txwi_8023(__le32 *txwi, struct sk_buff *skb,
				 struct mt76_wcid *wcid)
{
	u8 tid = skb->priority & IEEE80211_QOS_CTL_TID_MASK;
	u8 fc_type, fc_stype;
	u16 ethertype;
	bool wmm = false;
	u32 val;

	if (wcid->sta) {
		struct ieee80211_sta *sta;

		sta = container_of((void *)wcid, struct ieee80211_sta, drv_priv);
		wmm = sta->wme;
	}

	val = FIELD_PREP(MT_TXD1_HDR_FORMAT, MT_HDR_FORMAT_802_3) |
	      FIELD_PREP(MT_TXD1_TID, tid);

	ethertype = get_unaligned_be16(&skb->data[12]);
	if (ethertype >= ETH_P_802_3_MIN)
		val |= MT_TXD1_ETH_802_3;

	txwi[1] |= cpu_to_le32(val);

	fc_type = IEEE80211_FTYPE_DATA >> 2;
	fc_stype = wmm ? IEEE80211_STYPE_QOS_DATA >> 4 : 0;

	val = FIELD_PREP(MT_TXD2_FRAME_TYPE, fc_type) |
	      FIELD_PREP(MT_TXD2_SUB_TYPE, fc_stype);

	txwi[2] |= cpu_to_le32(val);

	val = FIELD_PREP(MT_TXD7_TYPE, fc_type) |
	      FIELD_PREP(MT_TXD7_SUB_TYPE, fc_stype);

	txwi[7] |= cpu_to_le32(val);
}

static void
mt76_connac2_mac_write_txwi_80211(struct mt76_dev *dev, __le32 *txwi,
				  struct sk_buff *skb,
				  struct ieee80211_key_conf *key)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	bool multicast = is_multicast_ether_addr(hdr->addr1);
	u8 tid = skb->priority & IEEE80211_QOS_CTL_TID_MASK;
	__le16 fc = hdr->frame_control;
	u8 fc_type, fc_stype;
	u32 val;

	if (ieee80211_is_action(fc) &&
	    mgmt->u.action.category == WLAN_CATEGORY_BACK &&
	    mgmt->u.action.u.addba_req.action_code == WLAN_ACTION_ADDBA_REQ) {
		u16 capab = le16_to_cpu(mgmt->u.action.u.addba_req.capab);

		txwi[5] |= cpu_to_le32(MT_TXD5_ADD_BA);
		tid = (capab >> 2) & IEEE80211_QOS_CTL_TID_MASK;
	} else if (ieee80211_is_back_req(hdr->frame_control)) {
		struct ieee80211_bar *bar = (struct ieee80211_bar *)hdr;
		u16 control = le16_to_cpu(bar->control);

		tid = FIELD_GET(IEEE80211_BAR_CTRL_TID_INFO_MASK, control);
	}

	val = FIELD_PREP(MT_TXD1_HDR_FORMAT, MT_HDR_FORMAT_802_11) |
	      FIELD_PREP(MT_TXD1_HDR_INFO,
			 ieee80211_get_hdrlen_from_skb(skb) / 2) |
	      FIELD_PREP(MT_TXD1_TID, tid);

	txwi[1] |= cpu_to_le32(val);

	fc_type = (le16_to_cpu(fc) & IEEE80211_FCTL_FTYPE) >> 2;
	fc_stype = (le16_to_cpu(fc) & IEEE80211_FCTL_STYPE) >> 4;

	val = FIELD_PREP(MT_TXD2_FRAME_TYPE, fc_type) |
	      FIELD_PREP(MT_TXD2_SUB_TYPE, fc_stype) |
	      FIELD_PREP(MT_TXD2_MULTICAST, multicast);

	if (key && multicast && ieee80211_is_robust_mgmt_frame(skb) &&
	    key->cipher == WLAN_CIPHER_SUITE_AES_CMAC) {
		val |= MT_TXD2_BIP;
		txwi[3] &= ~cpu_to_le32(MT_TXD3_PROTECT_FRAME);
	}

	if (!ieee80211_is_data(fc) || multicast ||
	    info->flags & IEEE80211_TX_CTL_USE_MINRATE)
		val |= MT_TXD2_FIX_RATE;

	txwi[2] |= cpu_to_le32(val);

	if (ieee80211_is_beacon(fc)) {
		txwi[3] &= ~cpu_to_le32(MT_TXD3_SW_POWER_MGMT);
		txwi[3] |= cpu_to_le32(MT_TXD3_REM_TX_COUNT);
		if (!is_mt7921(dev))
			txwi[7] |= cpu_to_le32(FIELD_PREP(MT_TXD7_SPE_IDX,
							  0x18));
	}

	if (info->flags & IEEE80211_TX_CTL_INJECTED) {
		u16 seqno = le16_to_cpu(hdr->seq_ctrl);

		if (ieee80211_is_back_req(hdr->frame_control)) {
			struct ieee80211_bar *bar;

			bar = (struct ieee80211_bar *)skb->data;
			seqno = le16_to_cpu(bar->start_seq_num);
		}

		val = MT_TXD3_SN_VALID |
		      FIELD_PREP(MT_TXD3_SEQ, IEEE80211_SEQ_TO_SN(seqno));
		txwi[3] |= cpu_to_le32(val);
		txwi[7] &= ~cpu_to_le32(MT_TXD7_HW_AMSDU);
	}

	if (mt76_is_mmio(dev)) {
		val = FIELD_PREP(MT_TXD7_TYPE, fc_type) |
		      FIELD_PREP(MT_TXD7_SUB_TYPE, fc_stype);
		txwi[7] |= cpu_to_le32(val);
	} else {
		val = FIELD_PREP(MT_TXD8_L_TYPE, fc_type) |
		      FIELD_PREP(MT_TXD8_L_SUB_TYPE, fc_stype);
		txwi[8] |= cpu_to_le32(val);
	}
}

void mt76_connac2_mac_write_txwi(struct mt76_dev *dev, __le32 *txwi,
				 struct sk_buff *skb, struct mt76_wcid *wcid,
				 struct ieee80211_key_conf *key, int pid,
				 u32 changed)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	bool ext_phy = info->hw_queue & MT_TX_HW_QUEUE_EXT_PHY;
	struct ieee80211_vif *vif = info->control.vif;
	struct mt76_phy *mphy = &dev->phy;
	u8 p_fmt, q_idx, omac_idx = 0, wmm_idx = 0, band_idx = 0;
	u32 val, sz_txd = mt76_is_mmio(dev) ? MT_TXD_SIZE : MT_SDIO_TXD_SIZE;
	bool is_8023 = info->flags & IEEE80211_TX_CTL_HW_80211_ENCAP;
	bool beacon = !!(changed & (BSS_CHANGED_BEACON |
				    BSS_CHANGED_BEACON_ENABLED));
	bool inband_disc = !!(changed & (BSS_CHANGED_UNSOL_BCAST_PROBE_RESP |
					 BSS_CHANGED_FILS_DISCOVERY));

	if (vif) {
		struct mt76_vif *mvif = (struct mt76_vif *)vif->drv_priv;

		omac_idx = mvif->omac_idx;
		wmm_idx = mvif->wmm_idx;
		band_idx = mvif->band_idx;
	}

	if (ext_phy && dev->phy2)
		mphy = dev->phy2;

	if (inband_disc) {
		p_fmt = MT_TX_TYPE_FW;
		q_idx = MT_LMAC_ALTX0;
	} else if (beacon) {
		p_fmt = MT_TX_TYPE_FW;
		q_idx = MT_LMAC_BCN0;
	} else if (skb_get_queue_mapping(skb) >= MT_TXQ_PSD) {
		p_fmt = mt76_is_mmio(dev) ? MT_TX_TYPE_CT : MT_TX_TYPE_SF;
		q_idx = MT_LMAC_ALTX0;
	} else {
		p_fmt = mt76_is_mmio(dev) ? MT_TX_TYPE_CT : MT_TX_TYPE_SF;
		q_idx = wmm_idx * MT76_CONNAC_MAX_WMM_SETS +
			mt76_connac_lmac_mapping(skb_get_queue_mapping(skb));
	}

	val = FIELD_PREP(MT_TXD0_TX_BYTES, skb->len + sz_txd) |
	      FIELD_PREP(MT_TXD0_PKT_FMT, p_fmt) |
	      FIELD_PREP(MT_TXD0_Q_IDX, q_idx);
	txwi[0] = cpu_to_le32(val);

	val = MT_TXD1_LONG_FORMAT |
	      FIELD_PREP(MT_TXD1_WLAN_IDX, wcid->idx) |
	      FIELD_PREP(MT_TXD1_OWN_MAC, omac_idx);
	if (!is_mt7921(dev))
		val |= MT_TXD1_VTA;
	if (ext_phy || band_idx)
		val |= MT_TXD1_TGID;

	txwi[1] = cpu_to_le32(val);
	txwi[2] = 0;

	val = FIELD_PREP(MT_TXD3_REM_TX_COUNT, 15);
	if (!is_mt7921(dev))
		val |= MT_TXD3_SW_POWER_MGMT;
	if (key)
		val |= MT_TXD3_PROTECT_FRAME;
	if (info->flags & IEEE80211_TX_CTL_NO_ACK)
		val |= MT_TXD3_NO_ACK;

	txwi[3] = cpu_to_le32(val);
	txwi[4] = 0;

	val = FIELD_PREP(MT_TXD5_PID, pid);
	if (pid >= MT_PACKET_ID_FIRST)
		val |= MT_TXD5_TX_STATUS_HOST;

	txwi[5] = cpu_to_le32(val);
	txwi[6] = 0;
	txwi[7] = wcid->amsdu ? cpu_to_le32(MT_TXD7_HW_AMSDU) : 0;

	if (is_8023)
		mt76_connac2_mac_write_txwi_8023(txwi, skb, wcid);
	else
		mt76_connac2_mac_write_txwi_80211(dev, txwi, skb, key);

	if (txwi[2] & cpu_to_le32(MT_TXD2_FIX_RATE)) {
		/* Fixed rata is available just for 802.11 txd */
		struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
		bool multicast = is_multicast_ether_addr(hdr->addr1);
		u16 rate = mt76_connac2_mac_tx_rate_val(mphy, vif, beacon,
							multicast);
		u32 val = MT_TXD6_FIXED_BW;

		/* hardware won't add HTC for mgmt/ctrl frame */
		txwi[2] |= cpu_to_le32(MT_TXD2_HTC_VLD);

		val |= FIELD_PREP(MT_TXD6_TX_RATE, rate);
		txwi[6] |= cpu_to_le32(val);
		txwi[3] |= cpu_to_le32(MT_TXD3_BA_DISABLE);
	}
}
EXPORT_SYMBOL_GPL(mt76_connac2_mac_write_txwi);

bool mt76_connac2_mac_add_txs_skb(struct mt76_dev *dev, struct mt76_wcid *wcid,
				  int pid, __le32 *txs_data,
				  struct mt76_sta_stats *stats)
{
	struct ieee80211_supported_band *sband;
	struct mt76_phy *mphy;
	struct ieee80211_tx_info *info;
	struct sk_buff_head list;
	struct rate_info rate = {};
	struct sk_buff *skb;
	bool cck = false;
	u32 txrate, txs, mode;

	mt76_tx_status_lock(dev, &list);
	skb = mt76_tx_status_skb_get(dev, wcid, pid, &list);
	if (!skb)
		goto out;

	txs = le32_to_cpu(txs_data[0]);

	info = IEEE80211_SKB_CB(skb);
	if (!(txs & MT_TXS0_ACK_ERROR_MASK))
		info->flags |= IEEE80211_TX_STAT_ACK;

	info->status.ampdu_len = 1;
	info->status.ampdu_ack_len = !!(info->flags &
					IEEE80211_TX_STAT_ACK);

	info->status.rates[0].idx = -1;

	txrate = FIELD_GET(MT_TXS0_TX_RATE, txs);

	rate.mcs = FIELD_GET(MT_TX_RATE_IDX, txrate);
	rate.nss = FIELD_GET(MT_TX_RATE_NSS, txrate) + 1;

	if (rate.nss - 1 < ARRAY_SIZE(stats->tx_nss))
		stats->tx_nss[rate.nss - 1]++;
	if (rate.mcs < ARRAY_SIZE(stats->tx_mcs))
		stats->tx_mcs[rate.mcs]++;

	mode = FIELD_GET(MT_TX_RATE_MODE, txrate);
	switch (mode) {
	case MT_PHY_TYPE_CCK:
		cck = true;
		fallthrough;
	case MT_PHY_TYPE_OFDM:
		mphy = &dev->phy;
		if (wcid->ext_phy && dev->phy2)
			mphy = dev->phy2;

		if (mphy->chandef.chan->band == NL80211_BAND_5GHZ)
			sband = &mphy->sband_5g.sband;
		else if (mphy->chandef.chan->band == NL80211_BAND_6GHZ)
			sband = &mphy->sband_6g.sband;
		else
			sband = &mphy->sband_2g.sband;

		rate.mcs = mt76_get_rate(mphy->dev, sband, rate.mcs, cck);
		rate.legacy = sband->bitrates[rate.mcs].bitrate;
		break;
	case MT_PHY_TYPE_HT:
	case MT_PHY_TYPE_HT_GF:
		if (rate.mcs > 31)
			goto out;

		rate.flags = RATE_INFO_FLAGS_MCS;
		if (wcid->rate.flags & RATE_INFO_FLAGS_SHORT_GI)
			rate.flags |= RATE_INFO_FLAGS_SHORT_GI;
		break;
	case MT_PHY_TYPE_VHT:
		if (rate.mcs > 9)
			goto out;

		rate.flags = RATE_INFO_FLAGS_VHT_MCS;
		break;
	case MT_PHY_TYPE_HE_SU:
	case MT_PHY_TYPE_HE_EXT_SU:
	case MT_PHY_TYPE_HE_TB:
	case MT_PHY_TYPE_HE_MU:
		if (rate.mcs > 11)
			goto out;

		rate.he_gi = wcid->rate.he_gi;
		rate.he_dcm = FIELD_GET(MT_TX_RATE_DCM, txrate);
		rate.flags = RATE_INFO_FLAGS_HE_MCS;
		break;
	default:
		goto out;
	}

	stats->tx_mode[mode]++;

	switch (FIELD_GET(MT_TXS0_BW, txs)) {
	case IEEE80211_STA_RX_BW_160:
		rate.bw = RATE_INFO_BW_160;
		stats->tx_bw[3]++;
		break;
	case IEEE80211_STA_RX_BW_80:
		rate.bw = RATE_INFO_BW_80;
		stats->tx_bw[2]++;
		break;
	case IEEE80211_STA_RX_BW_40:
		rate.bw = RATE_INFO_BW_40;
		stats->tx_bw[1]++;
		break;
	default:
		rate.bw = RATE_INFO_BW_20;
		stats->tx_bw[0]++;
		break;
	}
	wcid->rate = rate;

out:
	if (skb)
		mt76_tx_status_skb_done(dev, skb, &list);

	mt76_tx_status_unlock(dev, &list);

	return !!skb;
}
EXPORT_SYMBOL_GPL(mt76_connac2_mac_add_txs_skb);

static void
mt76_connac2_mac_decode_he_radiotap_ru(struct mt76_rx_status *status,
				       struct ieee80211_radiotap_he *he,
				       __le32 *rxv)
{
	u32 ru_h, ru_l;
	u8 ru, offs = 0;

	ru_l = le32_get_bits(rxv[0], MT_PRXV_HE_RU_ALLOC_L);
	ru_h = le32_get_bits(rxv[1], MT_PRXV_HE_RU_ALLOC_H);
	ru = (u8)(ru_l | ru_h << 4);

	status->bw = RATE_INFO_BW_HE_RU;

	switch (ru) {
	case 0 ... 36:
		status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_26;
		offs = ru;
		break;
	case 37 ... 52:
		status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_52;
		offs = ru - 37;
		break;
	case 53 ... 60:
		status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_106;
		offs = ru - 53;
		break;
	case 61 ... 64:
		status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_242;
		offs = ru - 61;
		break;
	case 65 ... 66:
		status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_484;
		offs = ru - 65;
		break;
	case 67:
		status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_996;
		break;
	case 68:
		status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_2x996;
		break;
	}

	he->data1 |= HE_BITS(DATA1_BW_RU_ALLOC_KNOWN);
	he->data2 |= HE_BITS(DATA2_RU_OFFSET_KNOWN) |
		     le16_encode_bits(offs,
				      IEEE80211_RADIOTAP_HE_DATA2_RU_OFFSET);
}

static void
mt76_connac2_mac_decode_he_mu_radiotap(struct mt76_dev *dev, struct sk_buff *skb,
				       __le32 *rxv)
{
	struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
	static struct ieee80211_radiotap_he_mu mu_known = {
		.flags1 = HE_BITS(MU_FLAGS1_SIG_B_MCS_KNOWN) |
			  HE_BITS(MU_FLAGS1_SIG_B_DCM_KNOWN) |
			  HE_BITS(MU_FLAGS1_CH1_RU_KNOWN) |
			  HE_BITS(MU_FLAGS1_SIG_B_SYMS_USERS_KNOWN),
		.flags2 = HE_BITS(MU_FLAGS2_BW_FROM_SIG_A_BW_KNOWN),
	};
	struct ieee80211_radiotap_he_mu *he_mu;

	if (is_mt7921(dev)) {
		mu_known.flags1 |= HE_BITS(MU_FLAGS1_SIG_B_COMP_KNOWN);
		mu_known.flags2 |= HE_BITS(MU_FLAGS2_PUNC_FROM_SIG_A_BW_KNOWN);
	}

	status->flag |= RX_FLAG_RADIOTAP_HE_MU;

	he_mu = skb_push(skb, sizeof(mu_known));
	memcpy(he_mu, &mu_known, sizeof(mu_known));

#define MU_PREP(f, v)	le16_encode_bits(v, IEEE80211_RADIOTAP_HE_MU_##f)

	he_mu->flags1 |= MU_PREP(FLAGS1_SIG_B_MCS, status->rate_idx);
	if (status->he_dcm)
		he_mu->flags1 |= MU_PREP(FLAGS1_SIG_B_DCM, status->he_dcm);

	he_mu->flags2 |= MU_PREP(FLAGS2_BW_FROM_SIG_A_BW, status->bw) |
			 MU_PREP(FLAGS2_SIG_B_SYMS_USERS,
				 le32_get_bits(rxv[2], MT_CRXV_HE_NUM_USER));

	he_mu->ru_ch1[0] = le32_get_bits(rxv[3], MT_CRXV_HE_RU0);

	if (status->bw >= RATE_INFO_BW_40) {
		he_mu->flags1 |= HE_BITS(MU_FLAGS1_CH2_RU_KNOWN);
		he_mu->ru_ch2[0] =
			le32_get_bits(rxv[3], MT_CRXV_HE_RU1);
	}

	if (status->bw >= RATE_INFO_BW_80) {
		he_mu->ru_ch1[1] =
			le32_get_bits(rxv[3], MT_CRXV_HE_RU2);
		he_mu->ru_ch2[1] =
			le32_get_bits(rxv[3], MT_CRXV_HE_RU3);
	}
}

void mt76_connac2_mac_decode_he_radiotap(struct mt76_dev *dev,
					 struct sk_buff *skb,
					 __le32 *rxv, u32 mode)
{
	struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
	static const struct ieee80211_radiotap_he known = {
		.data1 = HE_BITS(DATA1_DATA_MCS_KNOWN) |
			 HE_BITS(DATA1_DATA_DCM_KNOWN) |
			 HE_BITS(DATA1_STBC_KNOWN) |
			 HE_BITS(DATA1_CODING_KNOWN) |
			 HE_BITS(DATA1_LDPC_XSYMSEG_KNOWN) |
			 HE_BITS(DATA1_DOPPLER_KNOWN) |
			 HE_BITS(DATA1_SPTL_REUSE_KNOWN) |
			 HE_BITS(DATA1_BSS_COLOR_KNOWN),
		.data2 = HE_BITS(DATA2_GI_KNOWN) |
			 HE_BITS(DATA2_TXBF_KNOWN) |
			 HE_BITS(DATA2_PE_DISAMBIG_KNOWN) |
			 HE_BITS(DATA2_TXOP_KNOWN),
	};
	u32 ltf_size = le32_get_bits(rxv[2], MT_CRXV_HE_LTF_SIZE) + 1;
	struct ieee80211_radiotap_he *he;

	status->flag |= RX_FLAG_RADIOTAP_HE;

	he = skb_push(skb, sizeof(known));
	memcpy(he, &known, sizeof(known));

	he->data3 = HE_PREP(DATA3_BSS_COLOR, BSS_COLOR, rxv[14]) |
		    HE_PREP(DATA3_LDPC_XSYMSEG, LDPC_EXT_SYM, rxv[2]);
	he->data4 = HE_PREP(DATA4_SU_MU_SPTL_REUSE, SR_MASK, rxv[11]);
	he->data5 = HE_PREP(DATA5_PE_DISAMBIG, PE_DISAMBIG, rxv[2]) |
		    le16_encode_bits(ltf_size,
				     IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE);
	if (le32_to_cpu(rxv[0]) & MT_PRXV_TXBF)
		he->data5 |= HE_BITS(DATA5_TXBF);
	he->data6 = HE_PREP(DATA6_TXOP, TXOP_DUR, rxv[14]) |
		    HE_PREP(DATA6_DOPPLER, DOPPLER, rxv[14]);

	switch (mode) {
	case MT_PHY_TYPE_HE_SU:
		he->data1 |= HE_BITS(DATA1_FORMAT_SU) |
			     HE_BITS(DATA1_UL_DL_KNOWN) |
			     HE_BITS(DATA1_BEAM_CHANGE_KNOWN) |
			     HE_BITS(DATA1_BW_RU_ALLOC_KNOWN);

		he->data3 |= HE_PREP(DATA3_BEAM_CHANGE, BEAM_CHNG, rxv[14]) |
			     HE_PREP(DATA3_UL_DL, UPLINK, rxv[2]);
		break;
	case MT_PHY_TYPE_HE_EXT_SU:
		he->data1 |= HE_BITS(DATA1_FORMAT_EXT_SU) |
			     HE_BITS(DATA1_UL_DL_KNOWN) |
			     HE_BITS(DATA1_BW_RU_ALLOC_KNOWN);

		he->data3 |= HE_PREP(DATA3_UL_DL, UPLINK, rxv[2]);
		break;
	case MT_PHY_TYPE_HE_MU:
		he->data1 |= HE_BITS(DATA1_FORMAT_MU) |
			     HE_BITS(DATA1_UL_DL_KNOWN);

		he->data3 |= HE_PREP(DATA3_UL_DL, UPLINK, rxv[2]);
		he->data4 |= HE_PREP(DATA4_MU_STA_ID, MU_AID, rxv[7]);

		mt76_connac2_mac_decode_he_radiotap_ru(status, he, rxv);
		mt76_connac2_mac_decode_he_mu_radiotap(dev, skb, rxv);
		break;
	case MT_PHY_TYPE_HE_TB:
		he->data1 |= HE_BITS(DATA1_FORMAT_TRIG) |
			     HE_BITS(DATA1_SPTL_REUSE2_KNOWN) |
			     HE_BITS(DATA1_SPTL_REUSE3_KNOWN) |
			     HE_BITS(DATA1_SPTL_REUSE4_KNOWN);

		he->data4 |= HE_PREP(DATA4_TB_SPTL_REUSE1, SR_MASK, rxv[11]) |
			     HE_PREP(DATA4_TB_SPTL_REUSE2, SR1_MASK, rxv[11]) |
			     HE_PREP(DATA4_TB_SPTL_REUSE3, SR2_MASK, rxv[11]) |
			     HE_PREP(DATA4_TB_SPTL_REUSE4, SR3_MASK, rxv[11]);

		mt76_connac2_mac_decode_he_radiotap_ru(status, he, rxv);
		break;
	default:
		break;
	}
}
EXPORT_SYMBOL_GPL(mt76_connac2_mac_decode_he_radiotap);
