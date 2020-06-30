// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 */
#include <linux/of.h>
#include "mt76.h"

#define CHAN2G(_idx, _freq) {			\
	.band = NL80211_BAND_2GHZ,		\
	.center_freq = (_freq),			\
	.hw_value = (_idx),			\
	.max_power = 30,			\
}

#define CHAN5G(_idx, _freq) {			\
	.band = NL80211_BAND_5GHZ,		\
	.center_freq = (_freq),			\
	.hw_value = (_idx),			\
	.max_power = 30,			\
}

static const struct ieee80211_channel mt76_channels_2ghz[] = {
	CHAN2G(1, 2412),
	CHAN2G(2, 2417),
	CHAN2G(3, 2422),
	CHAN2G(4, 2427),
	CHAN2G(5, 2432),
	CHAN2G(6, 2437),
	CHAN2G(7, 2442),
	CHAN2G(8, 2447),
	CHAN2G(9, 2452),
	CHAN2G(10, 2457),
	CHAN2G(11, 2462),
	CHAN2G(12, 2467),
	CHAN2G(13, 2472),
	CHAN2G(14, 2484),
};

static const struct ieee80211_channel mt76_channels_5ghz[] = {
	CHAN5G(36, 5180),
	CHAN5G(40, 5200),
	CHAN5G(44, 5220),
	CHAN5G(48, 5240),

	CHAN5G(52, 5260),
	CHAN5G(56, 5280),
	CHAN5G(60, 5300),
	CHAN5G(64, 5320),

	CHAN5G(100, 5500),
	CHAN5G(104, 5520),
	CHAN5G(108, 5540),
	CHAN5G(112, 5560),
	CHAN5G(116, 5580),
	CHAN5G(120, 5600),
	CHAN5G(124, 5620),
	CHAN5G(128, 5640),
	CHAN5G(132, 5660),
	CHAN5G(136, 5680),
	CHAN5G(140, 5700),

	CHAN5G(149, 5745),
	CHAN5G(153, 5765),
	CHAN5G(157, 5785),
	CHAN5G(161, 5805),
	CHAN5G(165, 5825),
};

static const struct ieee80211_tpt_blink mt76_tpt_blink[] = {
	{ .throughput =   0 * 1024, .blink_time = 334 },
	{ .throughput =   1 * 1024, .blink_time = 260 },
	{ .throughput =   5 * 1024, .blink_time = 220 },
	{ .throughput =  10 * 1024, .blink_time = 190 },
	{ .throughput =  20 * 1024, .blink_time = 170 },
	{ .throughput =  50 * 1024, .blink_time = 150 },
	{ .throughput =  70 * 1024, .blink_time = 130 },
	{ .throughput = 100 * 1024, .blink_time = 110 },
	{ .throughput = 200 * 1024, .blink_time =  80 },
	{ .throughput = 300 * 1024, .blink_time =  50 },
};

static int mt76_led_init(struct mt76_dev *dev)
{
	struct device_node *np = dev->dev->of_node;
	struct ieee80211_hw *hw = dev->hw;
	int led_pin;

	if (!dev->led_cdev.brightness_set && !dev->led_cdev.blink_set)
		return 0;

	snprintf(dev->led_name, sizeof(dev->led_name),
		 "mt76-%s", wiphy_name(hw->wiphy));

	dev->led_cdev.name = dev->led_name;
	dev->led_cdev.default_trigger =
		ieee80211_create_tpt_led_trigger(hw,
					IEEE80211_TPT_LEDTRIG_FL_RADIO,
					mt76_tpt_blink,
					ARRAY_SIZE(mt76_tpt_blink));

	np = of_get_child_by_name(np, "led");
	if (np) {
		if (!of_property_read_u32(np, "led-sources", &led_pin))
			dev->led_pin = led_pin;
		dev->led_al = of_property_read_bool(np, "led-active-low");
	}

	return led_classdev_register(dev->dev, &dev->led_cdev);
}

static void mt76_led_cleanup(struct mt76_dev *dev)
{
	if (!dev->led_cdev.brightness_set && !dev->led_cdev.blink_set)
		return;

	led_classdev_unregister(&dev->led_cdev);
}

static void mt76_init_stream_cap(struct mt76_phy *phy,
				 struct ieee80211_supported_band *sband,
				 bool vht)
{
	struct ieee80211_sta_ht_cap *ht_cap = &sband->ht_cap;
	int i, nstream = hweight8(phy->antenna_mask);
	struct ieee80211_sta_vht_cap *vht_cap;
	u16 mcs_map = 0;

	if (nstream > 1)
		ht_cap->cap |= IEEE80211_HT_CAP_TX_STBC;
	else
		ht_cap->cap &= ~IEEE80211_HT_CAP_TX_STBC;

	for (i = 0; i < IEEE80211_HT_MCS_MASK_LEN; i++)
		ht_cap->mcs.rx_mask[i] = i < nstream ? 0xff : 0;

	if (!vht)
		return;

	vht_cap = &sband->vht_cap;
	if (nstream > 1)
		vht_cap->cap |= IEEE80211_VHT_CAP_TXSTBC;
	else
		vht_cap->cap &= ~IEEE80211_VHT_CAP_TXSTBC;

	for (i = 0; i < 8; i++) {
		if (i < nstream)
			mcs_map |= (IEEE80211_VHT_MCS_SUPPORT_0_9 << (i * 2));
		else
			mcs_map |=
				(IEEE80211_VHT_MCS_NOT_SUPPORTED << (i * 2));
	}
	vht_cap->vht_mcs.rx_mcs_map = cpu_to_le16(mcs_map);
	vht_cap->vht_mcs.tx_mcs_map = cpu_to_le16(mcs_map);
}

void mt76_set_stream_caps(struct mt76_phy *phy, bool vht)
{
	if (phy->dev->cap.has_2ghz)
		mt76_init_stream_cap(phy, &phy->sband_2g.sband, false);
	if (phy->dev->cap.has_5ghz)
		mt76_init_stream_cap(phy, &phy->sband_5g.sband, vht);
}
EXPORT_SYMBOL_GPL(mt76_set_stream_caps);

static int
mt76_init_sband(struct mt76_dev *dev, struct mt76_sband *msband,
		const struct ieee80211_channel *chan, int n_chan,
		struct ieee80211_rate *rates, int n_rates, bool vht)
{
	struct ieee80211_supported_band *sband = &msband->sband;
	struct ieee80211_sta_ht_cap *ht_cap;
	struct ieee80211_sta_vht_cap *vht_cap;
	void *chanlist;
	int size;

	size = n_chan * sizeof(*chan);
	chanlist = devm_kmemdup(dev->dev, chan, size, GFP_KERNEL);
	if (!chanlist)
		return -ENOMEM;

	msband->chan = devm_kcalloc(dev->dev, n_chan, sizeof(*msband->chan),
				    GFP_KERNEL);
	if (!msband->chan)
		return -ENOMEM;

	sband->channels = chanlist;
	sband->n_channels = n_chan;
	sband->bitrates = rates;
	sband->n_bitrates = n_rates;

	ht_cap = &sband->ht_cap;
	ht_cap->ht_supported = true;
	ht_cap->cap |= IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
		       IEEE80211_HT_CAP_GRN_FLD |
		       IEEE80211_HT_CAP_SGI_20 |
		       IEEE80211_HT_CAP_SGI_40 |
		       (1 << IEEE80211_HT_CAP_RX_STBC_SHIFT);

	ht_cap->mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED;
	ht_cap->ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K;

	mt76_init_stream_cap(&dev->phy, sband, vht);

	if (!vht)
		return 0;

	vht_cap = &sband->vht_cap;
	vht_cap->vht_supported = true;
	vht_cap->cap |= IEEE80211_VHT_CAP_RXLDPC |
			IEEE80211_VHT_CAP_RXSTBC_1 |
			IEEE80211_VHT_CAP_SHORT_GI_80 |
			IEEE80211_VHT_CAP_RX_ANTENNA_PATTERN |
			IEEE80211_VHT_CAP_TX_ANTENNA_PATTERN |
			(3 << IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT);

	return 0;
}

static int
mt76_init_sband_2g(struct mt76_dev *dev, struct ieee80211_rate *rates,
		   int n_rates)
{
	dev->hw->wiphy->bands[NL80211_BAND_2GHZ] = &dev->phy.sband_2g.sband;

	return mt76_init_sband(dev, &dev->phy.sband_2g,
			       mt76_channels_2ghz,
			       ARRAY_SIZE(mt76_channels_2ghz),
			       rates, n_rates, false);
}

static int
mt76_init_sband_5g(struct mt76_dev *dev, struct ieee80211_rate *rates,
		   int n_rates, bool vht)
{
	dev->hw->wiphy->bands[NL80211_BAND_5GHZ] = &dev->phy.sband_5g.sband;

	return mt76_init_sband(dev, &dev->phy.sband_5g,
			       mt76_channels_5ghz,
			       ARRAY_SIZE(mt76_channels_5ghz),
			       rates, n_rates, vht);
}

static void
mt76_check_sband(struct mt76_phy *phy, struct mt76_sband *msband,
		 enum nl80211_band band)
{
	struct ieee80211_supported_band *sband = &msband->sband;
	bool found = false;
	int i;

	if (!sband)
		return;

	for (i = 0; i < sband->n_channels; i++) {
		if (sband->channels[i].flags & IEEE80211_CHAN_DISABLED)
			continue;

		found = true;
		break;
	}

	if (found) {
		phy->chandef.chan = &sband->channels[0];
		phy->chan_state = &msband->chan[0];
		return;
	}

	sband->n_channels = 0;
	phy->hw->wiphy->bands[band] = NULL;
}

static void
mt76_phy_init(struct mt76_dev *dev, struct ieee80211_hw *hw)
{
	struct wiphy *wiphy = hw->wiphy;

	SET_IEEE80211_DEV(hw, dev->dev);
	SET_IEEE80211_PERM_ADDR(hw, dev->macaddr);

	wiphy->features |= NL80211_FEATURE_ACTIVE_MONITOR;
	wiphy->flags |= WIPHY_FLAG_HAS_CHANNEL_SWITCH |
			WIPHY_FLAG_SUPPORTS_TDLS;

	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_CQM_RSSI_LIST);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_AIRTIME_FAIRNESS);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_AQL);

	wiphy->available_antennas_tx = dev->phy.antenna_mask;
	wiphy->available_antennas_rx = dev->phy.antenna_mask;

	hw->txq_data_size = sizeof(struct mt76_txq);

	if (!hw->max_tx_fragments)
		hw->max_tx_fragments = 16;

	ieee80211_hw_set(hw, SIGNAL_DBM);
	ieee80211_hw_set(hw, AMPDU_AGGREGATION);
	ieee80211_hw_set(hw, SUPPORTS_RC_TABLE);
	ieee80211_hw_set(hw, SUPPORT_FAST_XMIT);
	ieee80211_hw_set(hw, SUPPORTS_CLONED_SKBS);
	ieee80211_hw_set(hw, SUPPORTS_AMSDU_IN_AMPDU);
	ieee80211_hw_set(hw, TX_AMSDU);
	ieee80211_hw_set(hw, TX_FRAG_LIST);
	ieee80211_hw_set(hw, MFP_CAPABLE);
	ieee80211_hw_set(hw, AP_LINK_PS);
	ieee80211_hw_set(hw, REPORTS_TX_ACK_STATUS);
	ieee80211_hw_set(hw, NEEDS_UNIQUE_STA_ADDR);

	wiphy->flags |= WIPHY_FLAG_IBSS_RSN;
	wiphy->interface_modes =
		BIT(NL80211_IFTYPE_STATION) |
		BIT(NL80211_IFTYPE_AP) |
#ifdef CONFIG_MAC80211_MESH
		BIT(NL80211_IFTYPE_MESH_POINT) |
#endif
		BIT(NL80211_IFTYPE_P2P_CLIENT) |
		BIT(NL80211_IFTYPE_P2P_GO) |
		BIT(NL80211_IFTYPE_ADHOC);
}

struct mt76_phy *
mt76_alloc_phy(struct mt76_dev *dev, unsigned int size,
	       const struct ieee80211_ops *ops)
{
	struct ieee80211_hw *hw;
	struct mt76_phy *phy;
	unsigned int phy_size, chan_size;
	unsigned int size_2g, size_5g;
	void *priv;

	phy_size = ALIGN(sizeof(*phy), 8);
	chan_size = sizeof(dev->phy.sband_2g.chan[0]);
	size_2g = ALIGN(ARRAY_SIZE(mt76_channels_2ghz) * chan_size, 8);
	size_5g = ALIGN(ARRAY_SIZE(mt76_channels_5ghz) * chan_size, 8);

	size += phy_size + size_2g + size_5g;
	hw = ieee80211_alloc_hw(size, ops);
	if (!hw)
		return NULL;

	phy = hw->priv;
	phy->dev = dev;
	phy->hw = hw;

	mt76_phy_init(dev, hw);

	priv = hw->priv + phy_size;

	phy->sband_2g = dev->phy.sband_2g;
	phy->sband_2g.chan = priv;
	priv += size_2g;

	phy->sband_5g = dev->phy.sband_5g;
	phy->sband_5g.chan = priv;
	priv += size_5g;

	phy->priv = priv;

	hw->wiphy->bands[NL80211_BAND_2GHZ] = &phy->sband_2g.sband;
	hw->wiphy->bands[NL80211_BAND_5GHZ] = &phy->sband_5g.sband;

	mt76_check_sband(phy, &phy->sband_2g, NL80211_BAND_2GHZ);
	mt76_check_sband(phy, &phy->sband_5g, NL80211_BAND_5GHZ);

	return phy;
}
EXPORT_SYMBOL_GPL(mt76_alloc_phy);

int
mt76_register_phy(struct mt76_phy *phy)
{
	int ret;

	ret = ieee80211_register_hw(phy->hw);
	if (ret)
		return ret;

	phy->dev->phy2 = phy;
	return 0;
}
EXPORT_SYMBOL_GPL(mt76_register_phy);

void
mt76_unregister_phy(struct mt76_phy *phy)
{
	struct mt76_dev *dev = phy->dev;

	dev->phy2 = NULL;
	mt76_tx_status_check(dev, NULL, true);
	ieee80211_unregister_hw(phy->hw);
}
EXPORT_SYMBOL_GPL(mt76_unregister_phy);

struct mt76_dev *
mt76_alloc_device(struct device *pdev, unsigned int size,
		  const struct ieee80211_ops *ops,
		  const struct mt76_driver_ops *drv_ops)
{
	struct ieee80211_hw *hw;
	struct mt76_phy *phy;
	struct mt76_dev *dev;
	int i;

	hw = ieee80211_alloc_hw(size, ops);
	if (!hw)
		return NULL;

	dev = hw->priv;
	dev->hw = hw;
	dev->dev = pdev;
	dev->drv = drv_ops;

	phy = &dev->phy;
	phy->dev = dev;
	phy->hw = hw;

	spin_lock_init(&dev->rx_lock);
	spin_lock_init(&dev->lock);
	spin_lock_init(&dev->cc_lock);
	mutex_init(&dev->mutex);
	init_waitqueue_head(&dev->tx_wait);
	skb_queue_head_init(&dev->status_list);

	skb_queue_head_init(&dev->mcu.res_q);
	init_waitqueue_head(&dev->mcu.wait);
	mutex_init(&dev->mcu.mutex);

	INIT_LIST_HEAD(&dev->txwi_cache);

	for (i = 0; i < ARRAY_SIZE(dev->q_rx); i++)
		skb_queue_head_init(&dev->rx_skb[i]);

	tasklet_init(&dev->tx_tasklet, mt76_tx_tasklet, (unsigned long)dev);

	return dev;
}
EXPORT_SYMBOL_GPL(mt76_alloc_device);

int mt76_register_device(struct mt76_dev *dev, bool vht,
			 struct ieee80211_rate *rates, int n_rates)
{
	struct ieee80211_hw *hw = dev->hw;
	struct mt76_phy *phy = &dev->phy;
	int ret;

	dev_set_drvdata(dev->dev, dev);
	mt76_phy_init(dev, hw);

	if (dev->cap.has_2ghz) {
		ret = mt76_init_sband_2g(dev, rates, n_rates);
		if (ret)
			return ret;
	}

	if (dev->cap.has_5ghz) {
		ret = mt76_init_sband_5g(dev, rates + 4, n_rates - 4, vht);
		if (ret)
			return ret;
	}

	wiphy_read_of_freq_limits(hw->wiphy);
	mt76_check_sband(&dev->phy, &phy->sband_2g, NL80211_BAND_2GHZ);
	mt76_check_sband(&dev->phy, &phy->sband_5g, NL80211_BAND_5GHZ);

	if (IS_ENABLED(CONFIG_MT76_LEDS)) {
		ret = mt76_led_init(dev);
		if (ret)
			return ret;
	}

	return ieee80211_register_hw(hw);
}
EXPORT_SYMBOL_GPL(mt76_register_device);

void mt76_unregister_device(struct mt76_dev *dev)
{
	struct ieee80211_hw *hw = dev->hw;

	if (IS_ENABLED(CONFIG_MT76_LEDS))
		mt76_led_cleanup(dev);
	mt76_tx_status_check(dev, NULL, true);
	ieee80211_unregister_hw(hw);
}
EXPORT_SYMBOL_GPL(mt76_unregister_device);

void mt76_free_device(struct mt76_dev *dev)
{
	mt76_tx_free(dev);
	ieee80211_free_hw(dev->hw);
}
EXPORT_SYMBOL_GPL(mt76_free_device);

void mt76_rx(struct mt76_dev *dev, enum mt76_rxq_id q, struct sk_buff *skb)
{
	struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
	struct mt76_phy *phy = mt76_dev_phy(dev, status->ext_phy);

	if (!test_bit(MT76_STATE_RUNNING, &phy->state)) {
		dev_kfree_skb(skb);
		return;
	}

	__skb_queue_tail(&dev->rx_skb[q], skb);
}
EXPORT_SYMBOL_GPL(mt76_rx);

bool mt76_has_tx_pending(struct mt76_phy *phy)
{
	struct mt76_dev *dev = phy->dev;
	struct mt76_queue *q;
	int i, offset;

	offset = __MT_TXQ_MAX * (phy != &dev->phy);

	for (i = 0; i < __MT_TXQ_MAX; i++) {
		q = dev->q_tx[offset + i].q;
		if (q && q->queued)
			return true;
	}

	return false;
}
EXPORT_SYMBOL_GPL(mt76_has_tx_pending);

static struct mt76_channel_state *
mt76_channel_state(struct mt76_phy *phy, struct ieee80211_channel *c)
{
	struct mt76_sband *msband;
	int idx;

	if (c->band == NL80211_BAND_2GHZ)
		msband = &phy->sband_2g;
	else
		msband = &phy->sband_5g;

	idx = c - &msband->sband.channels[0];
	return &msband->chan[idx];
}

static void
mt76_update_survey_active_time(struct mt76_phy *phy, ktime_t time)
{
	struct mt76_channel_state *state = phy->chan_state;

	state->cc_active += ktime_to_us(ktime_sub(time,
						  phy->survey_time));
	phy->survey_time = time;
}

void mt76_update_survey(struct mt76_dev *dev)
{
	ktime_t cur_time;

	if (dev->drv->update_survey)
		dev->drv->update_survey(dev);

	cur_time = ktime_get_boottime();
	mt76_update_survey_active_time(&dev->phy, cur_time);
	if (dev->phy2)
		mt76_update_survey_active_time(dev->phy2, cur_time);

	if (dev->drv->drv_flags & MT_DRV_SW_RX_AIRTIME) {
		struct mt76_channel_state *state = dev->phy.chan_state;

		spin_lock_bh(&dev->cc_lock);
		state->cc_bss_rx += dev->cur_cc_bss_rx;
		dev->cur_cc_bss_rx = 0;
		spin_unlock_bh(&dev->cc_lock);
	}
}
EXPORT_SYMBOL_GPL(mt76_update_survey);

void mt76_set_channel(struct mt76_phy *phy)
{
	struct mt76_dev *dev = phy->dev;
	struct ieee80211_hw *hw = phy->hw;
	struct cfg80211_chan_def *chandef = &hw->conf.chandef;
	bool offchannel = hw->conf.flags & IEEE80211_CONF_OFFCHANNEL;
	int timeout = HZ / 5;

	wait_event_timeout(dev->tx_wait, !mt76_has_tx_pending(phy), timeout);
	mt76_update_survey(dev);

	phy->chandef = *chandef;
	phy->chan_state = mt76_channel_state(phy, chandef->chan);

	if (!offchannel)
		phy->main_chan = chandef->chan;

	if (chandef->chan != phy->main_chan)
		memset(phy->chan_state, 0, sizeof(*phy->chan_state));
}
EXPORT_SYMBOL_GPL(mt76_set_channel);

int mt76_get_survey(struct ieee80211_hw *hw, int idx,
		    struct survey_info *survey)
{
	struct mt76_phy *phy = hw->priv;
	struct mt76_dev *dev = phy->dev;
	struct mt76_sband *sband;
	struct ieee80211_channel *chan;
	struct mt76_channel_state *state;
	int ret = 0;

	mutex_lock(&dev->mutex);
	if (idx == 0 && dev->drv->update_survey)
		mt76_update_survey(dev);

	sband = &phy->sband_2g;
	if (idx >= sband->sband.n_channels) {
		idx -= sband->sband.n_channels;
		sband = &phy->sband_5g;
	}

	if (idx >= sband->sband.n_channels) {
		ret = -ENOENT;
		goto out;
	}

	chan = &sband->sband.channels[idx];
	state = mt76_channel_state(phy, chan);

	memset(survey, 0, sizeof(*survey));
	survey->channel = chan;
	survey->filled = SURVEY_INFO_TIME | SURVEY_INFO_TIME_BUSY;
	survey->filled |= dev->drv->survey_flags;
	if (state->noise)
		survey->filled |= SURVEY_INFO_NOISE_DBM;

	if (chan == phy->main_chan) {
		survey->filled |= SURVEY_INFO_IN_USE;

		if (dev->drv->drv_flags & MT_DRV_SW_RX_AIRTIME)
			survey->filled |= SURVEY_INFO_TIME_BSS_RX;
	}

	survey->time_busy = div_u64(state->cc_busy, 1000);
	survey->time_rx = div_u64(state->cc_rx, 1000);
	survey->time = div_u64(state->cc_active, 1000);
	survey->noise = state->noise;

	spin_lock_bh(&dev->cc_lock);
	survey->time_bss_rx = div_u64(state->cc_bss_rx, 1000);
	survey->time_tx = div_u64(state->cc_tx, 1000);
	spin_unlock_bh(&dev->cc_lock);

out:
	mutex_unlock(&dev->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(mt76_get_survey);

void mt76_wcid_key_setup(struct mt76_dev *dev, struct mt76_wcid *wcid,
			 struct ieee80211_key_conf *key)
{
	struct ieee80211_key_seq seq;
	int i;

	wcid->rx_check_pn = false;

	if (!key)
		return;

	if (key->cipher != WLAN_CIPHER_SUITE_CCMP)
		return;

	wcid->rx_check_pn = true;
	for (i = 0; i < IEEE80211_NUM_TIDS; i++) {
		ieee80211_get_key_rx_seq(key, i, &seq);
		memcpy(wcid->rx_key_pn[i], seq.ccmp.pn, sizeof(seq.ccmp.pn));
	}
}
EXPORT_SYMBOL(mt76_wcid_key_setup);

static void
mt76_rx_convert(struct mt76_dev *dev, struct sk_buff *skb,
		struct ieee80211_hw **hw,
		struct ieee80211_sta **sta)
{
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(skb);
	struct mt76_rx_status mstat;

	mstat = *((struct mt76_rx_status *)skb->cb);
	memset(status, 0, sizeof(*status));

	status->flag = mstat.flag;
	status->freq = mstat.freq;
	status->enc_flags = mstat.enc_flags;
	status->encoding = mstat.encoding;
	status->bw = mstat.bw;
	status->he_ru = mstat.he_ru;
	status->he_gi = mstat.he_gi;
	status->he_dcm = mstat.he_dcm;
	status->rate_idx = mstat.rate_idx;
	status->nss = mstat.nss;
	status->band = mstat.band;
	status->signal = mstat.signal;
	status->chains = mstat.chains;
	status->ampdu_reference = mstat.ampdu_ref;

	BUILD_BUG_ON(sizeof(mstat) > sizeof(skb->cb));
	BUILD_BUG_ON(sizeof(status->chain_signal) !=
		     sizeof(mstat.chain_signal));
	memcpy(status->chain_signal, mstat.chain_signal,
	       sizeof(mstat.chain_signal));

	*sta = wcid_to_sta(mstat.wcid);
	*hw = mt76_phy_hw(dev, mstat.ext_phy);
}

static int
mt76_check_ccmp_pn(struct sk_buff *skb)
{
	struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
	struct mt76_wcid *wcid = status->wcid;
	struct ieee80211_hdr *hdr;
	int ret;

	if (!(status->flag & RX_FLAG_DECRYPTED))
		return 0;

	if (!wcid || !wcid->rx_check_pn)
		return 0;

	if (!(status->flag & RX_FLAG_IV_STRIPPED)) {
		/*
		 * Validate the first fragment both here and in mac80211
		 * All further fragments will be validated by mac80211 only.
		 */
		hdr = mt76_skb_get_hdr(skb);
		if (ieee80211_is_frag(hdr) &&
		    !ieee80211_is_first_frag(hdr->frame_control))
			return 0;
	}

	BUILD_BUG_ON(sizeof(status->iv) != sizeof(wcid->rx_key_pn[0]));
	ret = memcmp(status->iv, wcid->rx_key_pn[status->tid],
		     sizeof(status->iv));
	if (ret <= 0)
		return -EINVAL; /* replay */

	memcpy(wcid->rx_key_pn[status->tid], status->iv, sizeof(status->iv));

	if (status->flag & RX_FLAG_IV_STRIPPED)
		status->flag |= RX_FLAG_PN_VALIDATED;

	return 0;
}

static void
mt76_airtime_report(struct mt76_dev *dev, struct mt76_rx_status *status,
		    int len)
{
	struct mt76_wcid *wcid = status->wcid;
	struct ieee80211_rx_status info = {
		.enc_flags = status->enc_flags,
		.rate_idx = status->rate_idx,
		.encoding = status->encoding,
		.band = status->band,
		.nss = status->nss,
		.bw = status->bw,
	};
	struct ieee80211_sta *sta;
	u32 airtime;

	airtime = ieee80211_calc_rx_airtime(dev->hw, &info, len);
	spin_lock(&dev->cc_lock);
	dev->cur_cc_bss_rx += airtime;
	spin_unlock(&dev->cc_lock);

	if (!wcid || !wcid->sta)
		return;

	sta = container_of((void *)wcid, struct ieee80211_sta, drv_priv);
	ieee80211_sta_register_airtime(sta, status->tid, 0, airtime);
}

static void
mt76_airtime_flush_ampdu(struct mt76_dev *dev)
{
	struct mt76_wcid *wcid;
	int wcid_idx;

	if (!dev->rx_ampdu_len)
		return;

	wcid_idx = dev->rx_ampdu_status.wcid_idx;
	if (wcid_idx < ARRAY_SIZE(dev->wcid))
		wcid = rcu_dereference(dev->wcid[wcid_idx]);
	else
		wcid = NULL;
	dev->rx_ampdu_status.wcid = wcid;

	mt76_airtime_report(dev, &dev->rx_ampdu_status, dev->rx_ampdu_len);

	dev->rx_ampdu_len = 0;
	dev->rx_ampdu_ref = 0;
}

static void
mt76_airtime_check(struct mt76_dev *dev, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = mt76_skb_get_hdr(skb);
	struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
	struct mt76_wcid *wcid = status->wcid;

	if (!(dev->drv->drv_flags & MT_DRV_SW_RX_AIRTIME))
		return;

	if (!wcid || !wcid->sta) {
		if (!ether_addr_equal(hdr->addr1, dev->macaddr))
			return;

		wcid = NULL;
	}

	if (!(status->flag & RX_FLAG_AMPDU_DETAILS) ||
	    status->ampdu_ref != dev->rx_ampdu_ref)
		mt76_airtime_flush_ampdu(dev);

	if (status->flag & RX_FLAG_AMPDU_DETAILS) {
		if (!dev->rx_ampdu_len ||
		    status->ampdu_ref != dev->rx_ampdu_ref) {
			dev->rx_ampdu_status = *status;
			dev->rx_ampdu_status.wcid_idx = wcid ? wcid->idx : 0xff;
			dev->rx_ampdu_ref = status->ampdu_ref;
		}

		dev->rx_ampdu_len += skb->len;
		return;
	}

	mt76_airtime_report(dev, status, skb->len);
}

static void
mt76_check_sta(struct mt76_dev *dev, struct sk_buff *skb)
{
	struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
	struct ieee80211_hdr *hdr = mt76_skb_get_hdr(skb);
	struct ieee80211_sta *sta;
	struct ieee80211_hw *hw;
	struct mt76_wcid *wcid = status->wcid;
	bool ps;
	int i;

	hw = mt76_phy_hw(dev, status->ext_phy);
	if (ieee80211_is_pspoll(hdr->frame_control) && !wcid) {
		sta = ieee80211_find_sta_by_ifaddr(hw, hdr->addr2, NULL);
		if (sta)
			wcid = status->wcid = (struct mt76_wcid *)sta->drv_priv;
	}

	mt76_airtime_check(dev, skb);

	if (!wcid || !wcid->sta)
		return;

	sta = container_of((void *)wcid, struct ieee80211_sta, drv_priv);

	if (status->signal <= 0)
		ewma_signal_add(&wcid->rssi, -status->signal);

	wcid->inactive_count = 0;

	if (!test_bit(MT_WCID_FLAG_CHECK_PS, &wcid->flags))
		return;

	if (ieee80211_is_pspoll(hdr->frame_control)) {
		ieee80211_sta_pspoll(sta);
		return;
	}

	if (ieee80211_has_morefrags(hdr->frame_control) ||
	    !(ieee80211_is_mgmt(hdr->frame_control) ||
	      ieee80211_is_data(hdr->frame_control)))
		return;

	ps = ieee80211_has_pm(hdr->frame_control);

	if (ps && (ieee80211_is_data_qos(hdr->frame_control) ||
		   ieee80211_is_qos_nullfunc(hdr->frame_control)))
		ieee80211_sta_uapsd_trigger(sta, status->tid);

	if (!!test_bit(MT_WCID_FLAG_PS, &wcid->flags) == ps)
		return;

	if (ps)
		set_bit(MT_WCID_FLAG_PS, &wcid->flags);
	else
		clear_bit(MT_WCID_FLAG_PS, &wcid->flags);

	dev->drv->sta_ps(dev, sta, ps);
	ieee80211_sta_ps_transition(sta, ps);

	if (ps)
		return;

	for (i = 0; i < ARRAY_SIZE(sta->txq); i++) {
		struct mt76_txq *mtxq;

		if (!sta->txq[i])
			continue;

		mtxq = (struct mt76_txq *)sta->txq[i]->drv_priv;
		if (!skb_queue_empty(&mtxq->retry_q))
			ieee80211_schedule_txq(hw, sta->txq[i]);
	}
}

void mt76_rx_complete(struct mt76_dev *dev, struct sk_buff_head *frames,
		      struct napi_struct *napi)
{
	struct ieee80211_sta *sta;
	struct ieee80211_hw *hw;
	struct sk_buff *skb;

	spin_lock(&dev->rx_lock);
	while ((skb = __skb_dequeue(frames)) != NULL) {
		if (mt76_check_ccmp_pn(skb)) {
			dev_kfree_skb(skb);
			continue;
		}

		mt76_rx_convert(dev, skb, &hw, &sta);
		ieee80211_rx_napi(hw, sta, skb, napi);
	}
	spin_unlock(&dev->rx_lock);
}

void mt76_rx_poll_complete(struct mt76_dev *dev, enum mt76_rxq_id q,
			   struct napi_struct *napi)
{
	struct sk_buff_head frames;
	struct sk_buff *skb;

	__skb_queue_head_init(&frames);

	while ((skb = __skb_dequeue(&dev->rx_skb[q])) != NULL) {
		mt76_check_sta(dev, skb);
		mt76_rx_aggr_reorder(skb, &frames);
	}

	mt76_rx_complete(dev, &frames, napi);
}
EXPORT_SYMBOL_GPL(mt76_rx_poll_complete);

static int
mt76_sta_add(struct mt76_dev *dev, struct ieee80211_vif *vif,
	     struct ieee80211_sta *sta, bool ext_phy)
{
	struct mt76_wcid *wcid = (struct mt76_wcid *)sta->drv_priv;
	int ret;
	int i;

	mutex_lock(&dev->mutex);

	ret = dev->drv->sta_add(dev, vif, sta);
	if (ret)
		goto out;

	for (i = 0; i < ARRAY_SIZE(sta->txq); i++) {
		struct mt76_txq *mtxq;

		if (!sta->txq[i])
			continue;

		mtxq = (struct mt76_txq *)sta->txq[i]->drv_priv;
		mtxq->wcid = wcid;

		mt76_txq_init(dev, sta->txq[i]);
	}

	ewma_signal_init(&wcid->rssi);
	if (ext_phy)
		mt76_wcid_mask_set(dev->wcid_phy_mask, wcid->idx);
	wcid->ext_phy = ext_phy;
	rcu_assign_pointer(dev->wcid[wcid->idx], wcid);

out:
	mutex_unlock(&dev->mutex);

	return ret;
}

void __mt76_sta_remove(struct mt76_dev *dev, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta)
{
	struct mt76_wcid *wcid = (struct mt76_wcid *)sta->drv_priv;
	int i, idx = wcid->idx;

	for (i = 0; i < ARRAY_SIZE(wcid->aggr); i++)
		mt76_rx_aggr_stop(dev, wcid, i);

	if (dev->drv->sta_remove)
		dev->drv->sta_remove(dev, vif, sta);

	mt76_tx_status_check(dev, wcid, true);
	for (i = 0; i < ARRAY_SIZE(sta->txq); i++)
		mt76_txq_remove(dev, sta->txq[i]);
	mt76_wcid_mask_clear(dev->wcid_mask, idx);
	mt76_wcid_mask_clear(dev->wcid_phy_mask, idx);
}
EXPORT_SYMBOL_GPL(__mt76_sta_remove);

static void
mt76_sta_remove(struct mt76_dev *dev, struct ieee80211_vif *vif,
		struct ieee80211_sta *sta)
{
	mutex_lock(&dev->mutex);
	__mt76_sta_remove(dev, vif, sta);
	mutex_unlock(&dev->mutex);
}

int mt76_sta_state(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		   struct ieee80211_sta *sta,
		   enum ieee80211_sta_state old_state,
		   enum ieee80211_sta_state new_state)
{
	struct mt76_phy *phy = hw->priv;
	struct mt76_dev *dev = phy->dev;
	bool ext_phy = phy != &dev->phy;

	if (old_state == IEEE80211_STA_NOTEXIST &&
	    new_state == IEEE80211_STA_NONE)
		return mt76_sta_add(dev, vif, sta, ext_phy);

	if (old_state == IEEE80211_STA_AUTH &&
	    new_state == IEEE80211_STA_ASSOC &&
	    dev->drv->sta_assoc)
		dev->drv->sta_assoc(dev, vif, sta);

	if (old_state == IEEE80211_STA_NONE &&
	    new_state == IEEE80211_STA_NOTEXIST)
		mt76_sta_remove(dev, vif, sta);

	return 0;
}
EXPORT_SYMBOL_GPL(mt76_sta_state);

void mt76_sta_pre_rcu_remove(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			     struct ieee80211_sta *sta)
{
	struct mt76_phy *phy = hw->priv;
	struct mt76_dev *dev = phy->dev;
	struct mt76_wcid *wcid = (struct mt76_wcid *)sta->drv_priv;

	mutex_lock(&dev->mutex);
	rcu_assign_pointer(dev->wcid[wcid->idx], NULL);
	mutex_unlock(&dev->mutex);
}
EXPORT_SYMBOL_GPL(mt76_sta_pre_rcu_remove);

int mt76_get_txpower(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		     int *dbm)
{
	struct mt76_phy *phy = hw->priv;
	int n_chains = hweight8(phy->antenna_mask);
	int delta = mt76_tx_power_nss_delta(n_chains);

	*dbm = DIV_ROUND_UP(phy->txpower_cur + delta, 2);

	return 0;
}
EXPORT_SYMBOL_GPL(mt76_get_txpower);

static void
__mt76_csa_finish(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	if (vif->csa_active && ieee80211_csa_is_complete(vif))
		ieee80211_csa_finish(vif);
}

void mt76_csa_finish(struct mt76_dev *dev)
{
	if (!dev->csa_complete)
		return;

	ieee80211_iterate_active_interfaces_atomic(dev->hw,
		IEEE80211_IFACE_ITER_RESUME_ALL,
		__mt76_csa_finish, dev);

	dev->csa_complete = 0;
}
EXPORT_SYMBOL_GPL(mt76_csa_finish);

static void
__mt76_csa_check(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	struct mt76_dev *dev = priv;

	if (!vif->csa_active)
		return;

	dev->csa_complete |= ieee80211_csa_is_complete(vif);
}

void mt76_csa_check(struct mt76_dev *dev)
{
	ieee80211_iterate_active_interfaces_atomic(dev->hw,
		IEEE80211_IFACE_ITER_RESUME_ALL,
		__mt76_csa_check, dev);
}
EXPORT_SYMBOL_GPL(mt76_csa_check);

int
mt76_set_tim(struct ieee80211_hw *hw, struct ieee80211_sta *sta, bool set)
{
	return 0;
}
EXPORT_SYMBOL_GPL(mt76_set_tim);

void mt76_insert_ccmp_hdr(struct sk_buff *skb, u8 key_id)
{
	struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
	int hdr_len = ieee80211_get_hdrlen_from_skb(skb);
	u8 *hdr, *pn = status->iv;

	__skb_push(skb, 8);
	memmove(skb->data, skb->data + 8, hdr_len);
	hdr = skb->data + hdr_len;

	hdr[0] = pn[5];
	hdr[1] = pn[4];
	hdr[2] = 0;
	hdr[3] = 0x20 | (key_id << 6);
	hdr[4] = pn[3];
	hdr[5] = pn[2];
	hdr[6] = pn[1];
	hdr[7] = pn[0];

	status->flag &= ~RX_FLAG_IV_STRIPPED;
}
EXPORT_SYMBOL_GPL(mt76_insert_ccmp_hdr);

int mt76_get_rate(struct mt76_dev *dev,
		  struct ieee80211_supported_band *sband,
		  int idx, bool cck)
{
	int i, offset = 0, len = sband->n_bitrates;

	if (cck) {
		if (sband == &dev->phy.sband_5g.sband)
			return 0;

		idx &= ~BIT(2); /* short preamble */
	} else if (sband == &dev->phy.sband_2g.sband) {
		offset = 4;
	}

	for (i = offset; i < len; i++) {
		if ((sband->bitrates[i].hw_value & GENMASK(7, 0)) == idx)
			return i;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mt76_get_rate);

void mt76_sw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		  const u8 *mac)
{
	struct mt76_phy *phy = hw->priv;

	set_bit(MT76_SCANNING, &phy->state);
}
EXPORT_SYMBOL_GPL(mt76_sw_scan);

void mt76_sw_scan_complete(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct mt76_phy *phy = hw->priv;

	clear_bit(MT76_SCANNING, &phy->state);
}
EXPORT_SYMBOL_GPL(mt76_sw_scan_complete);

int mt76_get_antenna(struct ieee80211_hw *hw, u32 *tx_ant, u32 *rx_ant)
{
	struct mt76_phy *phy = hw->priv;
	struct mt76_dev *dev = phy->dev;

	mutex_lock(&dev->mutex);
	*tx_ant = phy->antenna_mask;
	*rx_ant = phy->antenna_mask;
	mutex_unlock(&dev->mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(mt76_get_antenna);
