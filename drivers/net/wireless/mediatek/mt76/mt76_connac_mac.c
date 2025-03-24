// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc. */

#include "mt76_connac.h"
#include "mt76_connac2_mac.h"
#include "dma.h"

#define HE_BITS(f)		cpu_to_le16(IEEE80211_RADIOTAP_HE_##f)
#define HE_PREP(f, m, v)	le16_encode_bits(le32_get_bits(v, MT_CRXV_HE_##m),\
						 IEEE80211_RADIOTAP_HE_##f)

void mt76_connac_gen_ppe_thresh(u8 *he_ppet, int nss, enum nl80211_band band)
{
	static const u8 ppet16_ppet8_ru3_ru0[] = { 0x1c, 0xc7, 0x71 };
	u8 i, ppet_bits, ppet_size, ru_bit_mask = 0xf;

	if (band == NL80211_BAND_2GHZ)
		ru_bit_mask = 0x3;

	he_ppet[0] = FIELD_PREP(IEEE80211_PPE_THRES_NSS_MASK, nss - 1) |
		     FIELD_PREP(IEEE80211_PPE_THRES_RU_INDEX_BITMASK_MASK,
				ru_bit_mask);

	ppet_bits = IEEE80211_PPE_THRES_INFO_PPET_SIZE *
		    nss * hweight8(ru_bit_mask) * 2;
	ppet_size = DIV_ROUND_UP(ppet_bits, 8);

	for (i = 0; i < ppet_size - 1; i++)
		he_ppet[i + 1] = ppet16_ppet8_ru3_ru0[i % 3];

	he_ppet[i + 1] = ppet16_ppet8_ru3_ru0[i % 3] &
			 (0xff >> (8 - (ppet_bits - 1) % 8));
}
EXPORT_SYMBOL_GPL(mt76_connac_gen_ppe_thresh);

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

void mt76_connac_tx_complete_skb(struct mt76_dev *mdev,
				 struct mt76_queue_entry *e)
{
	if (!e->txwi) {
		dev_kfree_skb_any(e->skb);
		return;
	}

	if (e->skb)
		mt76_tx_complete_skb(mdev, e->wcid, e->skb);
}
EXPORT_SYMBOL_GPL(mt76_connac_tx_complete_skb);

void mt76_connac_write_hw_txp(struct mt76_dev *dev,
			      struct mt76_tx_info *tx_info,
			      void *txp_ptr, u32 id)
{
	struct mt76_connac_hw_txp *txp = txp_ptr;
	struct mt76_connac_txp_ptr *ptr = &txp->ptr[0];
	int i, nbuf = tx_info->nbuf - 1;
	u32 last_mask;

	tx_info->buf[0].len = MT_TXD_SIZE + sizeof(*txp);
	tx_info->nbuf = 1;

	txp->msdu_id[0] = cpu_to_le16(id | MT_MSDU_ID_VALID);

	if (is_mt7663(dev) || is_mt7921(dev) || is_mt7925(dev))
		last_mask = MT_TXD_LEN_LAST;
	else
		last_mask = MT_TXD_LEN_AMSDU_LAST |
			    MT_TXD_LEN_MSDU_LAST;

	for (i = 0; i < nbuf; i++) {
		u16 len = tx_info->buf[i + 1].len & MT_TXD_LEN_MASK;
		u32 addr = tx_info->buf[i + 1].addr;

		if (i == nbuf - 1)
			len |= last_mask;

		if (i & 1) {
			ptr->buf1 = cpu_to_le32(addr);
			ptr->len1 = cpu_to_le16(len);
			ptr++;
		} else {
			ptr->buf0 = cpu_to_le32(addr);
			ptr->len0 = cpu_to_le16(len);
		}
	}
}
EXPORT_SYMBOL_GPL(mt76_connac_write_hw_txp);

static void
mt76_connac_txp_skb_unmap_fw(struct mt76_dev *mdev,
			     struct mt76_connac_fw_txp *txp)
{
	struct device *dev = is_connac_v1(mdev) ? mdev->dev : mdev->dma_dev;
	int i;

	for (i = 0; i < txp->nbuf; i++)
		dma_unmap_single(dev, le32_to_cpu(txp->buf[i]),
				 le16_to_cpu(txp->len[i]), DMA_TO_DEVICE);
}

static void
mt76_connac_txp_skb_unmap_hw(struct mt76_dev *dev,
			     struct mt76_connac_hw_txp *txp)
{
	u32 last_mask;
	int i;

	if (is_mt7663(dev) || is_mt7921(dev) || is_mt7925(dev))
		last_mask = MT_TXD_LEN_LAST;
	else
		last_mask = MT_TXD_LEN_MSDU_LAST;

	for (i = 0; i < ARRAY_SIZE(txp->ptr); i++) {
		struct mt76_connac_txp_ptr *ptr = &txp->ptr[i];
		bool last;
		u16 len;

		len = le16_to_cpu(ptr->len0);
		last = len & last_mask;
		len &= MT_TXD_LEN_MASK;
		dma_unmap_single(dev->dev, le32_to_cpu(ptr->buf0), len,
				 DMA_TO_DEVICE);
		if (last)
			break;

		len = le16_to_cpu(ptr->len1);
		last = len & last_mask;
		len &= MT_TXD_LEN_MASK;
		dma_unmap_single(dev->dev, le32_to_cpu(ptr->buf1), len,
				 DMA_TO_DEVICE);
		if (last)
			break;
	}
}

void mt76_connac_txp_skb_unmap(struct mt76_dev *dev,
			       struct mt76_txwi_cache *t)
{
	struct mt76_connac_txp_common *txp;

	txp = mt76_connac_txwi_to_txp(dev, t);
	if (is_mt76_fw_txp(dev))
		mt76_connac_txp_skb_unmap_fw(dev, &txp->fw);
	else
		mt76_connac_txp_skb_unmap_hw(dev, &txp->hw);
}
EXPORT_SYMBOL_GPL(mt76_connac_txp_skb_unmap);

int mt76_connac_init_tx_queues(struct mt76_phy *phy, int idx, int n_desc,
			       int ring_base, void *wed, u32 flags)
{
	int i, err;

	err = mt76_init_tx_queue(phy, 0, idx, n_desc, ring_base,
				 wed, flags);
	if (err < 0)
		return err;

	for (i = 1; i <= MT_TXQ_PSD; i++)
		phy->q_tx[i] = phy->q_tx[0];

	return 0;
}
EXPORT_SYMBOL_GPL(mt76_connac_init_tx_queues);

#define __bitrate_mask_check(_mcs, _mode)				\
({									\
	u8 i = 0;							\
	for (nss = 0; i < ARRAY_SIZE(mask->control[band]._mcs); i++) {	\
		if (!mask->control[band]._mcs[i])			\
			continue;					\
		if (hweight16(mask->control[band]._mcs[i]) == 1) {	\
			mode = MT_PHY_TYPE_##_mode;			\
			rateidx = ffs(mask->control[band]._mcs[i]) - 1;	\
			if (mode == MT_PHY_TYPE_HT)			\
				rateidx += 8 * i;			\
			else						\
				nss = i + 1;				\
			goto out;					\
		}							\
	}								\
})

u16 mt76_connac2_mac_tx_rate_val(struct mt76_phy *mphy,
				 struct ieee80211_bss_conf *conf,
				 bool beacon, bool mcast)
{
	struct mt76_vif_link *mvif = mt76_vif_conf_link(mphy->dev, conf->vif, conf);
	struct cfg80211_chan_def *chandef = mvif->ctx ?
					    &mvif->ctx->def : &mphy->chandef;
	u8 nss = 0, mode = 0, band = chandef->chan->band;
	int rateidx = 0, mcast_rate;
	int offset = 0;

	if (!conf)
		goto legacy;

	if (is_mt7921(mphy->dev)) {
		rateidx = ffs(conf->basic_rates) - 1;
		goto legacy;
	}

	if (beacon) {
		struct cfg80211_bitrate_mask *mask;

		mask = &conf->beacon_tx_rate;

		__bitrate_mask_check(he_mcs, HE_SU);
		__bitrate_mask_check(vht_mcs, VHT);
		__bitrate_mask_check(ht_mcs, HT);

		if (hweight32(mask->control[band].legacy) == 1) {
			rateidx = ffs(mask->control[band].legacy) - 1;
			goto legacy;
		}
	}

	mcast_rate = conf->mcast_rate[band];
	if (mcast && mcast_rate > 0)
		rateidx = mcast_rate - 1;
	else
		rateidx = ffs(conf->basic_rates) - 1;

legacy:
	if (band != NL80211_BAND_2GHZ)
		offset = 4;

	/* pick the lowest rate for hidden nodes */
	if (rateidx < 0)
		rateidx = 0;

	rateidx += offset;
	if (rateidx >= ARRAY_SIZE(mt76_rates))
		rateidx = offset;

	rateidx = mt76_rates[rateidx].hw_value;
	mode = rateidx >> 8;
	rateidx &= GENMASK(7, 0);
out:
	return FIELD_PREP(MT_TX_RATE_NSS, nss) |
	       FIELD_PREP(MT_TX_RATE_IDX, rateidx) |
	       FIELD_PREP(MT_TX_RATE_MODE, mode);
}
EXPORT_SYMBOL_GPL(mt76_connac2_mac_tx_rate_val);

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
	__le16 sc = hdr->seq_ctrl;
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

	if (ieee80211_has_morefrags(fc) && ieee80211_is_first_frag(sc))
		val |= FIELD_PREP(MT_TXD2_FRAG, MT_TX_FRAG_FIRST);
	else if (ieee80211_has_morefrags(fc) && !ieee80211_is_first_frag(sc))
		val |= FIELD_PREP(MT_TXD2_FRAG, MT_TX_FRAG_MID);
	else if (!ieee80211_has_morefrags(fc) && !ieee80211_is_first_frag(sc))
		val |= FIELD_PREP(MT_TXD2_FRAG, MT_TX_FRAG_LAST);

	txwi[2] |= cpu_to_le32(val);

	if (ieee80211_is_beacon(fc)) {
		txwi[3] &= ~cpu_to_le32(MT_TXD3_SW_POWER_MGMT);
		txwi[3] |= cpu_to_le32(MT_TXD3_REM_TX_COUNT);
	}

	if (info->flags & IEEE80211_TX_CTL_INJECTED) {
		u16 seqno = le16_to_cpu(sc);

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
				 enum mt76_txq_id qid, u32 changed)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	u8 phy_idx = (info->hw_queue & MT_TX_HW_QUEUE_PHY) >> 2;
	struct ieee80211_vif *vif = info->control.vif;
	struct mt76_phy *mphy = &dev->phy;
	u8 p_fmt, q_idx, omac_idx = 0, wmm_idx = 0, band_idx = 0;
	u32 val, sz_txd = mt76_is_mmio(dev) ? MT_TXD_SIZE : MT_SDIO_TXD_SIZE;
	bool is_8023 = info->flags & IEEE80211_TX_CTL_HW_80211_ENCAP;
	bool beacon = !!(changed & (BSS_CHANGED_BEACON |
				    BSS_CHANGED_BEACON_ENABLED));
	bool inband_disc = !!(changed & (BSS_CHANGED_UNSOL_BCAST_PROBE_RESP |
					 BSS_CHANGED_FILS_DISCOVERY));
	bool amsdu_en = wcid->amsdu;

	if (vif) {
		struct mt76_vif_link *mvif = (struct mt76_vif_link *)vif->drv_priv;

		omac_idx = mvif->omac_idx;
		wmm_idx = mvif->wmm_idx;
		band_idx = mvif->band_idx;
	}

	if (phy_idx && dev->phys[MT_BAND1])
		mphy = dev->phys[MT_BAND1];

	if (inband_disc) {
		p_fmt = MT_TX_TYPE_FW;
		q_idx = MT_LMAC_ALTX0;
	} else if (beacon) {
		p_fmt = MT_TX_TYPE_FW;
		q_idx = MT_LMAC_BCN0;
	} else if (qid >= MT_TXQ_PSD) {
		p_fmt = mt76_is_mmio(dev) ? MT_TX_TYPE_CT : MT_TX_TYPE_SF;
		q_idx = MT_LMAC_ALTX0;
	} else {
		p_fmt = mt76_is_mmio(dev) ? MT_TX_TYPE_CT : MT_TX_TYPE_SF;
		q_idx = wmm_idx * MT76_CONNAC_MAX_WMM_SETS +
			mt76_connac_lmac_mapping(skb_get_queue_mapping(skb));

		/* mt7915 WA only counts WED path */
		if (is_mt7915(dev) && mtk_wed_device_active(&dev->mmio.wed))
			wcid->stats.tx_packets++;
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
	if (phy_idx || band_idx)
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
	if (pid >= MT_PACKET_ID_FIRST) {
		val |= MT_TXD5_TX_STATUS_HOST;
		amsdu_en = 0;
	}

	txwi[5] = cpu_to_le32(val);
	txwi[6] = 0;
	txwi[7] = amsdu_en ? cpu_to_le32(MT_TXD7_HW_AMSDU) : 0;

	if (is_8023)
		mt76_connac2_mac_write_txwi_8023(txwi, skb, wcid);
	else
		mt76_connac2_mac_write_txwi_80211(dev, txwi, skb, key);

	if (txwi[2] & cpu_to_le32(MT_TXD2_FIX_RATE)) {
		/* Fixed rata is available just for 802.11 txd */
		struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
		bool multicast = ieee80211_is_data(hdr->frame_control) &&
				 is_multicast_ether_addr(hdr->addr1);
		u16 rate = mt76_connac2_mac_tx_rate_val(mphy, &vif->bss_conf, beacon,
							multicast);
		u32 val = MT_TXD6_FIXED_BW;

		/* hardware won't add HTC for mgmt/ctrl frame */
		txwi[2] |= cpu_to_le32(MT_TXD2_HTC_VLD);

		val |= FIELD_PREP(MT_TXD6_TX_RATE, rate);
		txwi[6] |= cpu_to_le32(val);
		txwi[3] |= cpu_to_le32(MT_TXD3_BA_DISABLE);

		if (!is_mt7921(dev)) {
			u8 spe_idx = mt76_connac_spe_idx(mphy->antenna_mask);

			if (!spe_idx)
				spe_idx = 24 + phy_idx;
			txwi[7] |= cpu_to_le32(FIELD_PREP(MT_TXD7_SPE_IDX, spe_idx));
		}

		txwi[7] &= ~cpu_to_le32(MT_TXD7_HW_AMSDU);
	}
}
EXPORT_SYMBOL_GPL(mt76_connac2_mac_write_txwi);

bool mt76_connac2_mac_fill_txs(struct mt76_dev *dev, struct mt76_wcid *wcid,
			       __le32 *txs_data)
{
	struct mt76_sta_stats *stats = &wcid->stats;
	struct ieee80211_supported_band *sband;
	struct mt76_phy *mphy;
	struct rate_info rate = {};
	bool cck = false;
	u32 txrate, txs, mode, stbc;

	txs = le32_to_cpu(txs_data[0]);

	/* PPDU based reporting */
	if (mtk_wed_device_active(&dev->mmio.wed) &&
	    FIELD_GET(MT_TXS0_TXS_FORMAT, txs) > 1) {
		stats->tx_bytes +=
			le32_get_bits(txs_data[5], MT_TXS5_MPDU_TX_BYTE) -
			le32_get_bits(txs_data[7], MT_TXS7_MPDU_RETRY_BYTE);
		stats->tx_failed +=
			le32_get_bits(txs_data[6], MT_TXS6_MPDU_FAIL_CNT);
		stats->tx_retries +=
			le32_get_bits(txs_data[7], MT_TXS7_MPDU_RETRY_CNT);

		if (wcid->sta) {
			struct ieee80211_sta *sta;
			u8 tid;

			sta = container_of((void *)wcid, struct ieee80211_sta,
					   drv_priv);
			tid = FIELD_GET(MT_TXS0_TID, txs);

			ieee80211_refresh_tx_agg_session_timer(sta, tid);
		}
	}

	txrate = FIELD_GET(MT_TXS0_TX_RATE, txs);

	rate.mcs = FIELD_GET(MT_TX_RATE_IDX, txrate);
	rate.nss = FIELD_GET(MT_TX_RATE_NSS, txrate) + 1;
	stbc = FIELD_GET(MT_TX_RATE_STBC, txrate);

	if (stbc && rate.nss > 1)
		rate.nss >>= 1;

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
		if (wcid->phy_idx == MT_BAND1 && dev->phys[MT_BAND1])
			mphy = dev->phys[MT_BAND1];

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
			return false;

		rate.flags = RATE_INFO_FLAGS_MCS;
		if (wcid->rate.flags & RATE_INFO_FLAGS_SHORT_GI)
			rate.flags |= RATE_INFO_FLAGS_SHORT_GI;
		break;
	case MT_PHY_TYPE_VHT:
		if (rate.mcs > 9)
			return false;

		rate.flags = RATE_INFO_FLAGS_VHT_MCS;
		break;
	case MT_PHY_TYPE_HE_SU:
	case MT_PHY_TYPE_HE_EXT_SU:
	case MT_PHY_TYPE_HE_TB:
	case MT_PHY_TYPE_HE_MU:
		if (rate.mcs > 11)
			return false;

		rate.he_gi = wcid->rate.he_gi;
		rate.he_dcm = FIELD_GET(MT_TX_RATE_DCM, txrate);
		rate.flags = RATE_INFO_FLAGS_HE_MCS;
		break;
	default:
		return false;
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

	return true;
}
EXPORT_SYMBOL_GPL(mt76_connac2_mac_fill_txs);

bool mt76_connac2_mac_add_txs_skb(struct mt76_dev *dev, struct mt76_wcid *wcid,
				  int pid, __le32 *txs_data)
{
	struct sk_buff_head list;
	struct sk_buff *skb;

	if (le32_get_bits(txs_data[0], MT_TXS0_TXS_FORMAT) == MT_TXS_PPDU_FMT)
		return false;

	mt76_tx_status_lock(dev, &list);
	skb = mt76_tx_status_skb_get(dev, wcid, pid, &list);
	if (skb) {
		struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

		if (!(le32_to_cpu(txs_data[0]) & MT_TXS0_ACK_ERROR_MASK))
			info->flags |= IEEE80211_TX_STAT_ACK;

		info->status.ampdu_len = 1;
		info->status.ampdu_ack_len =
			!!(info->flags & IEEE80211_TX_STAT_ACK);
		info->status.rates[0].idx = -1;

		mt76_connac2_mac_fill_txs(dev, wcid, txs_data);
		mt76_tx_status_skb_done(dev, skb, &list);
	}
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

/* The HW does not translate the mac header to 802.3 for mesh point */
int mt76_connac2_reverse_frag0_hdr_trans(struct ieee80211_vif *vif,
					 struct sk_buff *skb, u16 hdr_offset)
{
	struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
	struct ethhdr *eth_hdr = (struct ethhdr *)(skb->data + hdr_offset);
	__le32 *rxd = (__le32 *)skb->data;
	struct ieee80211_sta *sta;
	struct ieee80211_hdr hdr;
	u16 frame_control;

	if (le32_get_bits(rxd[3], MT_RXD3_NORMAL_ADDR_TYPE) !=
	    MT_RXD3_NORMAL_U2M)
		return -EINVAL;

	if (!(le32_to_cpu(rxd[1]) & MT_RXD1_NORMAL_GROUP_4))
		return -EINVAL;

	sta = container_of((void *)status->wcid, struct ieee80211_sta, drv_priv);

	/* store the info from RXD and ethhdr to avoid being overridden */
	frame_control = le32_get_bits(rxd[6], MT_RXD6_FRAME_CONTROL);
	hdr.frame_control = cpu_to_le16(frame_control);
	hdr.seq_ctrl = cpu_to_le16(le32_get_bits(rxd[8], MT_RXD8_SEQ_CTRL));
	hdr.duration_id = 0;

	ether_addr_copy(hdr.addr1, vif->addr);
	ether_addr_copy(hdr.addr2, sta->addr);
	switch (frame_control & (IEEE80211_FCTL_TODS |
				 IEEE80211_FCTL_FROMDS)) {
	case 0:
		ether_addr_copy(hdr.addr3, vif->bss_conf.bssid);
		break;
	case IEEE80211_FCTL_FROMDS:
		ether_addr_copy(hdr.addr3, eth_hdr->h_source);
		break;
	case IEEE80211_FCTL_TODS:
		ether_addr_copy(hdr.addr3, eth_hdr->h_dest);
		break;
	case IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS:
		ether_addr_copy(hdr.addr3, eth_hdr->h_dest);
		ether_addr_copy(hdr.addr4, eth_hdr->h_source);
		break;
	default:
		return -EINVAL;
	}

	skb_pull(skb, hdr_offset + sizeof(struct ethhdr) - 2);
	if (eth_hdr->h_proto == cpu_to_be16(ETH_P_AARP) ||
	    eth_hdr->h_proto == cpu_to_be16(ETH_P_IPX))
		ether_addr_copy(skb_push(skb, ETH_ALEN), bridge_tunnel_header);
	else if (be16_to_cpu(eth_hdr->h_proto) >= ETH_P_802_3_MIN)
		ether_addr_copy(skb_push(skb, ETH_ALEN), rfc1042_header);
	else
		skb_pull(skb, 2);

	if (ieee80211_has_order(hdr.frame_control))
		memcpy(skb_push(skb, IEEE80211_HT_CTL_LEN), &rxd[9],
		       IEEE80211_HT_CTL_LEN);
	if (ieee80211_is_data_qos(hdr.frame_control)) {
		__le16 qos_ctrl;

		qos_ctrl = cpu_to_le16(le32_get_bits(rxd[8], MT_RXD8_QOS_CTL));
		memcpy(skb_push(skb, IEEE80211_QOS_CTL_LEN), &qos_ctrl,
		       IEEE80211_QOS_CTL_LEN);
	}

	if (ieee80211_has_a4(hdr.frame_control))
		memcpy(skb_push(skb, sizeof(hdr)), &hdr, sizeof(hdr));
	else
		memcpy(skb_push(skb, sizeof(hdr) - 6), &hdr, sizeof(hdr) - 6);

	return 0;
}
EXPORT_SYMBOL_GPL(mt76_connac2_reverse_frag0_hdr_trans);

int mt76_connac2_mac_fill_rx_rate(struct mt76_dev *dev,
				  struct mt76_rx_status *status,
				  struct ieee80211_supported_band *sband,
				  __le32 *rxv, u8 *mode)
{
	u32 v0, v2;
	u8 stbc, gi, bw, dcm, nss;
	int i, idx;
	bool cck = false;

	v0 = le32_to_cpu(rxv[0]);
	v2 = le32_to_cpu(rxv[2]);

	idx = i = FIELD_GET(MT_PRXV_TX_RATE, v0);
	nss = FIELD_GET(MT_PRXV_NSTS, v0) + 1;

	if (!is_mt7915(dev)) {
		stbc = FIELD_GET(MT_PRXV_HT_STBC, v0);
		gi = FIELD_GET(MT_PRXV_HT_SGI, v0);
		*mode = FIELD_GET(MT_PRXV_TX_MODE, v0);
		if (is_mt7921(dev))
			dcm = !!(idx & MT_PRXV_TX_DCM);
		else
			dcm = FIELD_GET(MT_PRXV_DCM, v0);
		bw = FIELD_GET(MT_PRXV_FRAME_MODE, v0);
	} else {
		stbc = FIELD_GET(MT_CRXV_HT_STBC, v2);
		gi = FIELD_GET(MT_CRXV_HT_SHORT_GI, v2);
		*mode = FIELD_GET(MT_CRXV_TX_MODE, v2);
		dcm = !!(idx & GENMASK(3, 0) & MT_PRXV_TX_DCM);
		bw = FIELD_GET(MT_CRXV_FRAME_MODE, v2);
	}

	switch (*mode) {
	case MT_PHY_TYPE_CCK:
		cck = true;
		fallthrough;
	case MT_PHY_TYPE_OFDM:
		i = mt76_get_rate(dev, sband, i, cck);
		break;
	case MT_PHY_TYPE_HT_GF:
	case MT_PHY_TYPE_HT:
		status->encoding = RX_ENC_HT;
		if (gi)
			status->enc_flags |= RX_ENC_FLAG_SHORT_GI;
		if (i > 31)
			return -EINVAL;
		break;
	case MT_PHY_TYPE_VHT:
		status->nss = nss;
		status->encoding = RX_ENC_VHT;
		if (gi)
			status->enc_flags |= RX_ENC_FLAG_SHORT_GI;
		if (i > 11)
			return -EINVAL;
		break;
	case MT_PHY_TYPE_HE_MU:
	case MT_PHY_TYPE_HE_SU:
	case MT_PHY_TYPE_HE_EXT_SU:
	case MT_PHY_TYPE_HE_TB:
		status->nss = nss;
		status->encoding = RX_ENC_HE;
		i &= GENMASK(3, 0);

		if (gi <= NL80211_RATE_INFO_HE_GI_3_2)
			status->he_gi = gi;

		status->he_dcm = dcm;
		break;
	default:
		return -EINVAL;
	}
	status->rate_idx = i;

	switch (bw) {
	case IEEE80211_STA_RX_BW_20:
		break;
	case IEEE80211_STA_RX_BW_40:
		if (*mode & MT_PHY_TYPE_HE_EXT_SU &&
		    (idx & MT_PRXV_TX_ER_SU_106T)) {
			status->bw = RATE_INFO_BW_HE_RU;
			status->he_ru =
				NL80211_RATE_INFO_HE_RU_ALLOC_106;
		} else {
			status->bw = RATE_INFO_BW_40;
		}
		break;
	case IEEE80211_STA_RX_BW_80:
		status->bw = RATE_INFO_BW_80;
		break;
	case IEEE80211_STA_RX_BW_160:
		status->bw = RATE_INFO_BW_160;
		break;
	default:
		return -EINVAL;
	}

	status->enc_flags |= RX_ENC_FLAG_STBC_MASK * stbc;
	if (*mode < MT_PHY_TYPE_HE_SU && gi)
		status->enc_flags |= RX_ENC_FLAG_SHORT_GI;

	return 0;
}
EXPORT_SYMBOL_GPL(mt76_connac2_mac_fill_rx_rate);

void mt76_connac2_tx_check_aggr(struct ieee80211_sta *sta, __le32 *txwi)
{
	struct mt76_wcid *wcid;
	u16 fc, tid;
	u32 val;

	if (!sta ||
	    !(sta->deflink.ht_cap.ht_supported || sta->deflink.he_cap.has_he))
		return;

	tid = le32_get_bits(txwi[1], MT_TXD1_TID);
	if (tid >= 6) /* skip VO queue */
		return;

	val = le32_to_cpu(txwi[2]);
	fc = FIELD_GET(MT_TXD2_FRAME_TYPE, val) << 2 |
	     FIELD_GET(MT_TXD2_SUB_TYPE, val) << 4;
	if (unlikely(fc != (IEEE80211_FTYPE_DATA | IEEE80211_STYPE_QOS_DATA)))
		return;

	wcid = (struct mt76_wcid *)sta->drv_priv;
	if (!test_and_set_bit(tid, &wcid->ampdu_state))
		ieee80211_start_tx_ba_session(sta, tid, 0);
}
EXPORT_SYMBOL_GPL(mt76_connac2_tx_check_aggr);

void mt76_connac2_txwi_free(struct mt76_dev *dev, struct mt76_txwi_cache *t,
			    struct ieee80211_sta *sta,
			    struct list_head *free_list)
{
	struct mt76_wcid *wcid;
	__le32 *txwi;
	u16 wcid_idx;

	mt76_connac_txp_skb_unmap(dev, t);
	if (!t->skb)
		goto out;

	txwi = (__le32 *)mt76_get_txwi_ptr(dev, t);
	if (sta) {
		wcid = (struct mt76_wcid *)sta->drv_priv;
		wcid_idx = wcid->idx;
	} else {
		wcid_idx = le32_get_bits(txwi[1], MT_TXD1_WLAN_IDX);
		wcid = rcu_dereference(dev->wcid[wcid_idx]);

		if (wcid && wcid->sta) {
			sta = container_of((void *)wcid, struct ieee80211_sta,
					   drv_priv);
			mt76_wcid_add_poll(dev, wcid);
		}
	}

	if (sta && likely(t->skb->protocol != cpu_to_be16(ETH_P_PAE)))
		mt76_connac2_tx_check_aggr(sta, txwi);

	__mt76_tx_complete_skb(dev, wcid_idx, t->skb, free_list);
out:
	t->skb = NULL;
	mt76_put_txwi(dev, t);
}
EXPORT_SYMBOL_GPL(mt76_connac2_txwi_free);

void mt76_connac2_tx_token_put(struct mt76_dev *dev)
{
	struct mt76_txwi_cache *txwi;
	int id;

	spin_lock_bh(&dev->token_lock);
	idr_for_each_entry(&dev->token, txwi, id) {
		mt76_connac2_txwi_free(dev, txwi, NULL, NULL);
		dev->token_count--;
	}
	spin_unlock_bh(&dev->token_lock);
	idr_destroy(&dev->token);
}
EXPORT_SYMBOL_GPL(mt76_connac2_tx_token_put);
