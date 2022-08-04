// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2018 Stanislaw Gruszka <stf_xl@wp.pl>
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 */

#include <linux/module.h>
#include "mt76x02.h"

#define MT76x02_CCK_RATE(_idx, _rate) {					\
	.bitrate = _rate,					\
	.flags = IEEE80211_RATE_SHORT_PREAMBLE,			\
	.hw_value = (MT_PHY_TYPE_CCK << 8) | (_idx),		\
	.hw_value_short = (MT_PHY_TYPE_CCK << 8) | (8 + (_idx)),	\
}

struct ieee80211_rate mt76x02_rates[] = {
	MT76x02_CCK_RATE(0, 10),
	MT76x02_CCK_RATE(1, 20),
	MT76x02_CCK_RATE(2, 55),
	MT76x02_CCK_RATE(3, 110),
	OFDM_RATE(0, 60),
	OFDM_RATE(1, 90),
	OFDM_RATE(2, 120),
	OFDM_RATE(3, 180),
	OFDM_RATE(4, 240),
	OFDM_RATE(5, 360),
	OFDM_RATE(6, 480),
	OFDM_RATE(7, 540),
};
EXPORT_SYMBOL_GPL(mt76x02_rates);

static const struct ieee80211_iface_limit mt76x02_if_limits[] = {
	{
		.max = 1,
		.types = BIT(NL80211_IFTYPE_ADHOC)
	}, {
		.max = 8,
		.types = BIT(NL80211_IFTYPE_STATION) |
#ifdef CONFIG_MAC80211_MESH
			 BIT(NL80211_IFTYPE_MESH_POINT) |
#endif
			 BIT(NL80211_IFTYPE_P2P_CLIENT) |
			 BIT(NL80211_IFTYPE_P2P_GO) |
			 BIT(NL80211_IFTYPE_AP)
	 },
};

static const struct ieee80211_iface_limit mt76x02u_if_limits[] = {
	{
		.max = 1,
		.types = BIT(NL80211_IFTYPE_ADHOC)
	}, {
		.max = 2,
		.types = BIT(NL80211_IFTYPE_STATION) |
#ifdef CONFIG_MAC80211_MESH
			 BIT(NL80211_IFTYPE_MESH_POINT) |
#endif
			 BIT(NL80211_IFTYPE_P2P_CLIENT) |
			 BIT(NL80211_IFTYPE_P2P_GO) |
			 BIT(NL80211_IFTYPE_AP)
	},
};

static const struct ieee80211_iface_combination mt76x02_if_comb[] = {
	{
		.limits = mt76x02_if_limits,
		.n_limits = ARRAY_SIZE(mt76x02_if_limits),
		.max_interfaces = 8,
		.num_different_channels = 1,
		.beacon_int_infra_match = true,
		.radar_detect_widths = BIT(NL80211_CHAN_WIDTH_20_NOHT) |
				       BIT(NL80211_CHAN_WIDTH_20) |
				       BIT(NL80211_CHAN_WIDTH_40) |
				       BIT(NL80211_CHAN_WIDTH_80),
	}
};

static const struct ieee80211_iface_combination mt76x02u_if_comb[] = {
	{
		.limits = mt76x02u_if_limits,
		.n_limits = ARRAY_SIZE(mt76x02u_if_limits),
		.max_interfaces = 2,
		.num_different_channels = 1,
		.beacon_int_infra_match = true,
	}
};

static void
mt76x02_led_set_config(struct mt76_dev *mdev, u8 delay_on,
		       u8 delay_off)
{
	struct mt76x02_dev *dev = container_of(mdev, struct mt76x02_dev,
					       mt76);
	u32 val;

	val = FIELD_PREP(MT_LED_STATUS_DURATION, 0xff) |
	      FIELD_PREP(MT_LED_STATUS_OFF, delay_off) |
	      FIELD_PREP(MT_LED_STATUS_ON, delay_on);

	mt76_wr(dev, MT_LED_S0(mdev->led_pin), val);
	mt76_wr(dev, MT_LED_S1(mdev->led_pin), val);

	val = MT_LED_CTRL_REPLAY(mdev->led_pin) |
	      MT_LED_CTRL_KICK(mdev->led_pin);
	if (mdev->led_al)
		val |= MT_LED_CTRL_POLARITY(mdev->led_pin);
	mt76_wr(dev, MT_LED_CTRL, val);
}

static int
mt76x02_led_set_blink(struct led_classdev *led_cdev,
		      unsigned long *delay_on,
		      unsigned long *delay_off)
{
	struct mt76_dev *mdev = container_of(led_cdev, struct mt76_dev,
					     led_cdev);
	u8 delta_on, delta_off;

	delta_off = max_t(u8, *delay_off / 10, 1);
	delta_on = max_t(u8, *delay_on / 10, 1);

	mt76x02_led_set_config(mdev, delta_on, delta_off);

	return 0;
}

static void
mt76x02_led_set_brightness(struct led_classdev *led_cdev,
			   enum led_brightness brightness)
{
	struct mt76_dev *mdev = container_of(led_cdev, struct mt76_dev,
					     led_cdev);

	if (!brightness)
		mt76x02_led_set_config(mdev, 0, 0xff);
	else
		mt76x02_led_set_config(mdev, 0xff, 0);
}

void mt76x02_init_device(struct mt76x02_dev *dev)
{
	struct ieee80211_hw *hw = mt76_hw(dev);
	struct wiphy *wiphy = hw->wiphy;

	INIT_DELAYED_WORK(&dev->mphy.mac_work, mt76x02_mac_work);

	hw->queues = 4;
	hw->max_rates = 1;
	hw->max_report_rates = 7;
	hw->max_rate_tries = 1;
	hw->extra_tx_headroom = 2;

	if (mt76_is_usb(&dev->mt76)) {
		hw->extra_tx_headroom += sizeof(struct mt76x02_txwi) +
					 MT_DMA_HDR_LEN;
		wiphy->iface_combinations = mt76x02u_if_comb;
		wiphy->n_iface_combinations = ARRAY_SIZE(mt76x02u_if_comb);
	} else {
		INIT_DELAYED_WORK(&dev->wdt_work, mt76x02_wdt_work);

		mt76x02_dfs_init_detector(dev);

		wiphy->reg_notifier = mt76x02_regd_notifier;
		wiphy->iface_combinations = mt76x02_if_comb;
		wiphy->n_iface_combinations = ARRAY_SIZE(mt76x02_if_comb);

		/* init led callbacks */
		if (IS_ENABLED(CONFIG_MT76_LEDS)) {
			dev->mt76.led_cdev.brightness_set =
					mt76x02_led_set_brightness;
			dev->mt76.led_cdev.blink_set = mt76x02_led_set_blink;
		}
	}

	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_VHT_IBSS);

	hw->sta_data_size = sizeof(struct mt76x02_sta);
	hw->vif_data_size = sizeof(struct mt76x02_vif);

	ieee80211_hw_set(hw, SUPPORTS_HT_CCK_RATES);
	ieee80211_hw_set(hw, HOST_BROADCAST_PS_BUFFERING);
	ieee80211_hw_set(hw, NEEDS_UNIQUE_STA_ADDR);

	dev->mt76.global_wcid.idx = 255;
	dev->mt76.global_wcid.hw_key_idx = -1;
	dev->slottime = 9;

	if (is_mt76x2(dev)) {
		dev->mphy.sband_2g.sband.ht_cap.cap |=
				IEEE80211_HT_CAP_LDPC_CODING;
		dev->mphy.sband_5g.sband.ht_cap.cap |=
				IEEE80211_HT_CAP_LDPC_CODING;
		dev->mphy.chainmask = 0x202;
		dev->mphy.antenna_mask = 3;
	} else {
		dev->mphy.chainmask = 0x101;
		dev->mphy.antenna_mask = 1;
	}
}
EXPORT_SYMBOL_GPL(mt76x02_init_device);

void mt76x02_configure_filter(struct ieee80211_hw *hw,
			      unsigned int changed_flags,
			      unsigned int *total_flags, u64 multicast)
{
	struct mt76x02_dev *dev = hw->priv;
	u32 flags = 0;

#define MT76_FILTER(_flag, _hw) do { \
		flags |= *total_flags & FIF_##_flag;			\
		dev->mt76.rxfilter &= ~(_hw);				\
		dev->mt76.rxfilter |= !(flags & FIF_##_flag) * (_hw);	\
	} while (0)

	mutex_lock(&dev->mt76.mutex);

	dev->mt76.rxfilter &= ~MT_RX_FILTR_CFG_OTHER_BSS;

	MT76_FILTER(FCSFAIL, MT_RX_FILTR_CFG_CRC_ERR);
	MT76_FILTER(PLCPFAIL, MT_RX_FILTR_CFG_PHY_ERR);
	MT76_FILTER(CONTROL, MT_RX_FILTR_CFG_ACK |
			     MT_RX_FILTR_CFG_CTS |
			     MT_RX_FILTR_CFG_CFEND |
			     MT_RX_FILTR_CFG_CFACK |
			     MT_RX_FILTR_CFG_BA |
			     MT_RX_FILTR_CFG_CTRL_RSV);
	MT76_FILTER(PSPOLL, MT_RX_FILTR_CFG_PSPOLL);

	*total_flags = flags;
	mt76_wr(dev, MT_RX_FILTR_CFG, dev->mt76.rxfilter);

	mutex_unlock(&dev->mt76.mutex);
}
EXPORT_SYMBOL_GPL(mt76x02_configure_filter);

int mt76x02_sta_add(struct mt76_dev *mdev, struct ieee80211_vif *vif,
		    struct ieee80211_sta *sta)
{
	struct mt76x02_dev *dev = container_of(mdev, struct mt76x02_dev, mt76);
	struct mt76x02_sta *msta = (struct mt76x02_sta *)sta->drv_priv;
	struct mt76x02_vif *mvif = (struct mt76x02_vif *)vif->drv_priv;
	int idx = 0;

	memset(msta, 0, sizeof(*msta));

	idx = mt76_wcid_alloc(dev->mt76.wcid_mask, MT76x02_N_WCIDS);
	if (idx < 0)
		return -ENOSPC;

	msta->vif = mvif;
	msta->wcid.sta = 1;
	msta->wcid.idx = idx;
	msta->wcid.hw_key_idx = -1;
	mt76x02_mac_wcid_setup(dev, idx, mvif->idx, sta->addr);
	mt76x02_mac_wcid_set_drop(dev, idx, false);
	ewma_pktlen_init(&msta->pktlen);

	if (vif->type == NL80211_IFTYPE_AP)
		set_bit(MT_WCID_FLAG_CHECK_PS, &msta->wcid.flags);

	return 0;
}
EXPORT_SYMBOL_GPL(mt76x02_sta_add);

void mt76x02_sta_remove(struct mt76_dev *mdev, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta)
{
	struct mt76x02_dev *dev = container_of(mdev, struct mt76x02_dev, mt76);
	struct mt76_wcid *wcid = (struct mt76_wcid *)sta->drv_priv;
	int idx = wcid->idx;

	mt76x02_mac_wcid_set_drop(dev, idx, true);
	mt76x02_mac_wcid_setup(dev, idx, 0, NULL);
}
EXPORT_SYMBOL_GPL(mt76x02_sta_remove);

static void
mt76x02_vif_init(struct mt76x02_dev *dev, struct ieee80211_vif *vif,
		 unsigned int idx)
{
	struct mt76x02_vif *mvif = (struct mt76x02_vif *)vif->drv_priv;
	struct mt76_txq *mtxq;

	memset(mvif, 0, sizeof(*mvif));

	mvif->idx = idx;
	mvif->group_wcid.idx = MT_VIF_WCID(idx);
	mvif->group_wcid.hw_key_idx = -1;
	mtxq = (struct mt76_txq *)vif->txq->drv_priv;
	rcu_assign_pointer(dev->mt76.wcid[MT_VIF_WCID(idx)], &mvif->group_wcid);
	mtxq->wcid = MT_VIF_WCID(idx);
}

int
mt76x02_add_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct mt76x02_dev *dev = hw->priv;
	unsigned int idx = 0;

	/* Allow to change address in HW if we create first interface. */
	if (!dev->mt76.vif_mask &&
	    (((vif->addr[0] ^ dev->mphy.macaddr[0]) & ~GENMASK(4, 1)) ||
	     memcmp(vif->addr + 1, dev->mphy.macaddr + 1, ETH_ALEN - 1)))
		mt76x02_mac_setaddr(dev, vif->addr);

	if (vif->addr[0] & BIT(1))
		idx = 1 + (((dev->mphy.macaddr[0] ^ vif->addr[0]) >> 2) & 7);

	/*
	 * Client mode typically only has one configurable BSSID register,
	 * which is used for bssidx=0. This is linked to the MAC address.
	 * Since mac80211 allows changing interface types, and we cannot
	 * force the use of the primary MAC address for a station mode
	 * interface, we need some other way of configuring a per-interface
	 * remote BSSID.
	 * The hardware provides an AP-Client feature, where bssidx 0-7 are
	 * used for AP mode and bssidx 8-15 for client mode.
	 * We shift the station interface bss index by 8 to force the
	 * hardware to recognize the BSSID.
	 * The resulting bssidx mismatch for unicast frames is ignored by hw.
	 */
	if (vif->type == NL80211_IFTYPE_STATION)
		idx += 8;

	/* vif is already set or idx is 8 for AP/Mesh/... */
	if (dev->mt76.vif_mask & BIT(idx) ||
	    (vif->type != NL80211_IFTYPE_STATION && idx > 7))
		return -EBUSY;

	dev->mt76.vif_mask |= BIT(idx);

	mt76x02_vif_init(dev, vif, idx);
	return 0;
}
EXPORT_SYMBOL_GPL(mt76x02_add_interface);

void mt76x02_remove_interface(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif)
{
	struct mt76x02_dev *dev = hw->priv;
	struct mt76x02_vif *mvif = (struct mt76x02_vif *)vif->drv_priv;

	dev->mt76.vif_mask &= ~BIT(mvif->idx);
	rcu_assign_pointer(dev->mt76.wcid[mvif->group_wcid.idx], NULL);
}
EXPORT_SYMBOL_GPL(mt76x02_remove_interface);

int mt76x02_ampdu_action(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			 struct ieee80211_ampdu_params *params)
{
	enum ieee80211_ampdu_mlme_action action = params->action;
	struct ieee80211_sta *sta = params->sta;
	struct mt76x02_dev *dev = hw->priv;
	struct mt76x02_sta *msta = (struct mt76x02_sta *)sta->drv_priv;
	struct ieee80211_txq *txq = sta->txq[params->tid];
	u16 tid = params->tid;
	u16 ssn = params->ssn;
	struct mt76_txq *mtxq;
	int ret = 0;

	if (!txq)
		return -EINVAL;

	mtxq = (struct mt76_txq *)txq->drv_priv;

	mutex_lock(&dev->mt76.mutex);
	switch (action) {
	case IEEE80211_AMPDU_RX_START:
		mt76_rx_aggr_start(&dev->mt76, &msta->wcid, tid,
				   ssn, params->buf_size);
		mt76_set(dev, MT_WCID_ADDR(msta->wcid.idx) + 4, BIT(16 + tid));
		break;
	case IEEE80211_AMPDU_RX_STOP:
		mt76_rx_aggr_stop(&dev->mt76, &msta->wcid, tid);
		mt76_clear(dev, MT_WCID_ADDR(msta->wcid.idx) + 4,
			   BIT(16 + tid));
		break;
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		mtxq->aggr = true;
		mtxq->send_bar = false;
		ieee80211_send_bar(vif, sta->addr, tid, mtxq->agg_ssn);
		break;
	case IEEE80211_AMPDU_TX_STOP_FLUSH:
	case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
		mtxq->aggr = false;
		break;
	case IEEE80211_AMPDU_TX_START:
		mtxq->agg_ssn = IEEE80211_SN_TO_SEQ(ssn);
		ret = IEEE80211_AMPDU_TX_START_IMMEDIATE;
		break;
	case IEEE80211_AMPDU_TX_STOP_CONT:
		mtxq->aggr = false;
		ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		break;
	}
	mutex_unlock(&dev->mt76.mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(mt76x02_ampdu_action);

int mt76x02_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
		    struct ieee80211_vif *vif, struct ieee80211_sta *sta,
		    struct ieee80211_key_conf *key)
{
	struct mt76x02_dev *dev = hw->priv;
	struct mt76x02_vif *mvif = (struct mt76x02_vif *)vif->drv_priv;
	struct mt76x02_sta *msta;
	struct mt76_wcid *wcid;
	int idx = key->keyidx;
	int ret;

	/* fall back to sw encryption for unsupported ciphers */
	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
	case WLAN_CIPHER_SUITE_TKIP:
	case WLAN_CIPHER_SUITE_CCMP:
		break;
	default:
		return -EOPNOTSUPP;
	}

	/*
	 * The hardware does not support per-STA RX GTK, fall back
	 * to software mode for these.
	 */
	if ((vif->type == NL80211_IFTYPE_ADHOC ||
	     vif->type == NL80211_IFTYPE_MESH_POINT) &&
	    (key->cipher == WLAN_CIPHER_SUITE_TKIP ||
	     key->cipher == WLAN_CIPHER_SUITE_CCMP) &&
	    !(key->flags & IEEE80211_KEY_FLAG_PAIRWISE))
		return -EOPNOTSUPP;

	/*
	 * In USB AP mode, broadcast/multicast frames are setup in beacon
	 * data registers and sent via HW beacons engine, they require to
	 * be already encrypted.
	 */
	if (mt76_is_usb(&dev->mt76) &&
	    vif->type == NL80211_IFTYPE_AP &&
	    !(key->flags & IEEE80211_KEY_FLAG_PAIRWISE))
		return -EOPNOTSUPP;

	/* MT76x0 GTK offloading does not work with more than one VIF */
	if (is_mt76x0(dev) && !(key->flags & IEEE80211_KEY_FLAG_PAIRWISE))
		return -EOPNOTSUPP;

	msta = sta ? (struct mt76x02_sta *)sta->drv_priv : NULL;
	wcid = msta ? &msta->wcid : &mvif->group_wcid;

	if (cmd == SET_KEY) {
		key->hw_key_idx = wcid->idx;
		wcid->hw_key_idx = idx;
		if (key->flags & IEEE80211_KEY_FLAG_RX_MGMT) {
			key->flags |= IEEE80211_KEY_FLAG_SW_MGMT_TX;
			wcid->sw_iv = true;
		}
	} else {
		if (idx == wcid->hw_key_idx) {
			wcid->hw_key_idx = -1;
			wcid->sw_iv = false;
		}

		key = NULL;
	}
	mt76_wcid_key_setup(&dev->mt76, wcid, key);

	if (!msta) {
		if (key || wcid->hw_key_idx == idx) {
			ret = mt76x02_mac_wcid_set_key(dev, wcid->idx, key);
			if (ret)
				return ret;
		}

		return mt76x02_mac_shared_key_setup(dev, mvif->idx, idx, key);
	}

	return mt76x02_mac_wcid_set_key(dev, msta->wcid.idx, key);
}
EXPORT_SYMBOL_GPL(mt76x02_set_key);

int mt76x02_conf_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		    u16 queue, const struct ieee80211_tx_queue_params *params)
{
	struct mt76x02_dev *dev = hw->priv;
	u8 cw_min = 5, cw_max = 10, qid;
	u32 val;

	qid = dev->mphy.q_tx[queue]->hw_idx;

	if (params->cw_min)
		cw_min = fls(params->cw_min);
	if (params->cw_max)
		cw_max = fls(params->cw_max);

	val = FIELD_PREP(MT_EDCA_CFG_TXOP, params->txop) |
	      FIELD_PREP(MT_EDCA_CFG_AIFSN, params->aifs) |
	      FIELD_PREP(MT_EDCA_CFG_CWMIN, cw_min) |
	      FIELD_PREP(MT_EDCA_CFG_CWMAX, cw_max);
	mt76_wr(dev, MT_EDCA_CFG_AC(qid), val);

	val = mt76_rr(dev, MT_WMM_TXOP(qid));
	val &= ~(MT_WMM_TXOP_MASK << MT_WMM_TXOP_SHIFT(qid));
	val |= params->txop << MT_WMM_TXOP_SHIFT(qid);
	mt76_wr(dev, MT_WMM_TXOP(qid), val);

	val = mt76_rr(dev, MT_WMM_AIFSN);
	val &= ~(MT_WMM_AIFSN_MASK << MT_WMM_AIFSN_SHIFT(qid));
	val |= params->aifs << MT_WMM_AIFSN_SHIFT(qid);
	mt76_wr(dev, MT_WMM_AIFSN, val);

	val = mt76_rr(dev, MT_WMM_CWMIN);
	val &= ~(MT_WMM_CWMIN_MASK << MT_WMM_CWMIN_SHIFT(qid));
	val |= cw_min << MT_WMM_CWMIN_SHIFT(qid);
	mt76_wr(dev, MT_WMM_CWMIN, val);

	val = mt76_rr(dev, MT_WMM_CWMAX);
	val &= ~(MT_WMM_CWMAX_MASK << MT_WMM_CWMAX_SHIFT(qid));
	val |= cw_max << MT_WMM_CWMAX_SHIFT(qid);
	mt76_wr(dev, MT_WMM_CWMAX, val);

	return 0;
}
EXPORT_SYMBOL_GPL(mt76x02_conf_tx);

void mt76x02_set_tx_ackto(struct mt76x02_dev *dev)
{
	u8 ackto, sifs, slottime = dev->slottime;

	/* As defined by IEEE 802.11-2007 17.3.8.6 */
	slottime += 3 * dev->coverage_class;
	mt76_rmw_field(dev, MT_BKOFF_SLOT_CFG,
		       MT_BKOFF_SLOT_CFG_SLOTTIME, slottime);

	sifs = mt76_get_field(dev, MT_XIFS_TIME_CFG,
			      MT_XIFS_TIME_CFG_OFDM_SIFS);

	ackto = slottime + sifs;
	mt76_rmw_field(dev, MT_TX_TIMEOUT_CFG,
		       MT_TX_TIMEOUT_CFG_ACKTO, ackto);
}
EXPORT_SYMBOL_GPL(mt76x02_set_tx_ackto);

void mt76x02_set_coverage_class(struct ieee80211_hw *hw,
				s16 coverage_class)
{
	struct mt76x02_dev *dev = hw->priv;

	mutex_lock(&dev->mt76.mutex);
	dev->coverage_class = max_t(s16, coverage_class, 0);
	mt76x02_set_tx_ackto(dev);
	mutex_unlock(&dev->mt76.mutex);
}
EXPORT_SYMBOL_GPL(mt76x02_set_coverage_class);

int mt76x02_set_rts_threshold(struct ieee80211_hw *hw, u32 val)
{
	struct mt76x02_dev *dev = hw->priv;

	if (val != ~0 && val > 0xffff)
		return -EINVAL;

	mutex_lock(&dev->mt76.mutex);
	mt76x02_mac_set_rts_thresh(dev, val);
	mutex_unlock(&dev->mt76.mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(mt76x02_set_rts_threshold);

void mt76x02_sta_rate_tbl_update(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct ieee80211_sta *sta)
{
	struct mt76x02_dev *dev = hw->priv;
	struct mt76x02_sta *msta = (struct mt76x02_sta *)sta->drv_priv;
	struct ieee80211_sta_rates *rates = rcu_dereference(sta->rates);
	struct ieee80211_tx_rate rate = {};

	if (!rates)
		return;

	rate.idx = rates->rate[0].idx;
	rate.flags = rates->rate[0].flags;
	mt76x02_mac_wcid_set_rate(dev, &msta->wcid, &rate);
}
EXPORT_SYMBOL_GPL(mt76x02_sta_rate_tbl_update);

void mt76x02_remove_hdr_pad(struct sk_buff *skb, int len)
{
	int hdrlen;

	if (!len)
		return;

	hdrlen = ieee80211_get_hdrlen_from_skb(skb);
	memmove(skb->data + len, skb->data, hdrlen);
	skb_pull(skb, len);
}
EXPORT_SYMBOL_GPL(mt76x02_remove_hdr_pad);

void mt76x02_sw_scan_complete(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif)
{
	struct mt76x02_dev *dev = hw->priv;

	clear_bit(MT76_SCANNING, &dev->mphy.state);
	if (dev->cal.gain_init_done) {
		/* Restore AGC gain and resume calibration after scanning. */
		dev->cal.low_gain = -1;
		ieee80211_queue_delayed_work(hw, &dev->cal_work, 0);
	}
}
EXPORT_SYMBOL_GPL(mt76x02_sw_scan_complete);

void mt76x02_sta_ps(struct mt76_dev *mdev, struct ieee80211_sta *sta,
		    bool ps)
{
	struct mt76x02_dev *dev = container_of(mdev, struct mt76x02_dev, mt76);
	struct mt76x02_sta *msta = (struct mt76x02_sta *)sta->drv_priv;
	int idx = msta->wcid.idx;

	mt76_stop_tx_queues(&dev->mphy, sta, true);
	if (mt76_is_mmio(mdev))
		mt76x02_mac_wcid_set_drop(dev, idx, ps);
}
EXPORT_SYMBOL_GPL(mt76x02_sta_ps);

void mt76x02_bss_info_changed(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      struct ieee80211_bss_conf *info,
			      u32 changed)
{
	struct mt76x02_vif *mvif = (struct mt76x02_vif *)vif->drv_priv;
	struct mt76x02_dev *dev = hw->priv;

	mutex_lock(&dev->mt76.mutex);

	if (changed & BSS_CHANGED_BSSID)
		mt76x02_mac_set_bssid(dev, mvif->idx, info->bssid);

	if (changed & BSS_CHANGED_HT || changed & BSS_CHANGED_ERP_CTS_PROT)
		mt76x02_mac_set_tx_protection(dev, info->use_cts_prot,
					      info->ht_operation_mode);

	if (changed & BSS_CHANGED_BEACON_INT) {
		mt76_rmw_field(dev, MT_BEACON_TIME_CFG,
			       MT_BEACON_TIME_CFG_INTVAL,
			       info->beacon_int << 4);
		dev->mt76.beacon_int = info->beacon_int;
	}

	if (changed & BSS_CHANGED_BEACON_ENABLED)
		mt76x02_mac_set_beacon_enable(dev, vif, info->enable_beacon);

	if (changed & BSS_CHANGED_ERP_PREAMBLE)
		mt76x02_mac_set_short_preamble(dev, info->use_short_preamble);

	if (changed & BSS_CHANGED_ERP_SLOT) {
		int slottime = info->use_short_slot ? 9 : 20;

		dev->slottime = slottime;
		mt76x02_set_tx_ackto(dev);
	}

	mutex_unlock(&dev->mt76.mutex);
}
EXPORT_SYMBOL_GPL(mt76x02_bss_info_changed);

void mt76x02_config_mac_addr_list(struct mt76x02_dev *dev)
{
	struct ieee80211_hw *hw = mt76_hw(dev);
	struct wiphy *wiphy = hw->wiphy;
	int i;

	for (i = 0; i < ARRAY_SIZE(dev->macaddr_list); i++) {
		u8 *addr = dev->macaddr_list[i].addr;

		memcpy(addr, dev->mphy.macaddr, ETH_ALEN);

		if (!i)
			continue;

		addr[0] |= BIT(1);
		addr[0] ^= ((i - 1) << 2);
	}
	wiphy->addresses = dev->macaddr_list;
	wiphy->n_addresses = ARRAY_SIZE(dev->macaddr_list);
}
EXPORT_SYMBOL_GPL(mt76x02_config_mac_addr_list);

MODULE_LICENSE("Dual BSD/GPL");
