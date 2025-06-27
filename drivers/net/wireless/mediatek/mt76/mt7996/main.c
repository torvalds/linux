// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2022 MediaTek Inc.
 */

#include "mt7996.h"
#include "mcu.h"
#include "mac.h"

int mt7996_run(struct mt7996_phy *phy)
{
	struct mt7996_dev *dev = phy->dev;
	int ret;

	mt7996_mac_enable_nf(dev, phy->mt76->band_idx);

	ret = mt7996_mcu_set_rts_thresh(phy, 0x92b);
	if (ret)
		return ret;

	ret = mt7996_mcu_set_radio_en(phy, true);
	if (ret)
		return ret;

	ret = mt7996_mcu_set_chan_info(phy, UNI_CHANNEL_RX_PATH);
	if (ret)
		return ret;

	ret = mt7996_mcu_set_thermal_throttling(phy, MT7996_THERMAL_THROTTLE_MAX);
	if (ret)
		return ret;

	ret = mt7996_mcu_set_thermal_protect(phy, true);
	if (ret)
		return ret;

	set_bit(MT76_STATE_RUNNING, &phy->mt76->state);

	ieee80211_queue_delayed_work(dev->mphy.hw, &phy->mt76->mac_work,
				     MT7996_WATCHDOG_TIME);

	if (!phy->counter_reset) {
		mt7996_mac_reset_counters(phy);
		phy->counter_reset = true;
	}

	return 0;
}

static int mt7996_start(struct ieee80211_hw *hw)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	int ret;

	flush_work(&dev->init_work);

	mutex_lock(&dev->mt76.mutex);
	ret = mt7996_mcu_set_hdr_trans(dev, true);
	if (!ret && is_mt7992(&dev->mt76)) {
		u8 queue = mt76_connac_lmac_mapping(IEEE80211_AC_VI);

		ret = mt7996_mcu_cp_support(dev, queue);
	}
	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

static void mt7996_stop_phy(struct mt7996_phy *phy)
{
	struct mt7996_dev *dev;

	if (!phy || !test_bit(MT76_STATE_RUNNING, &phy->mt76->state))
		return;

	dev = phy->dev;

	cancel_delayed_work_sync(&phy->mt76->mac_work);

	mutex_lock(&dev->mt76.mutex);

	mt7996_mcu_set_radio_en(phy, false);

	clear_bit(MT76_STATE_RUNNING, &phy->mt76->state);

	mutex_unlock(&dev->mt76.mutex);
}

static void mt7996_stop(struct ieee80211_hw *hw, bool suspend)
{
}

static inline int get_free_idx(u32 mask, u8 start, u8 end)
{
	return ffs(~mask & GENMASK(end, start));
}

static int get_omac_idx(enum nl80211_iftype type, u64 mask)
{
	int i;

	switch (type) {
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_STATION:
		/* prefer hw bssid slot 1-3 */
		i = get_free_idx(mask, HW_BSSID_1, HW_BSSID_3);
		if (i)
			return i - 1;

		if (type != NL80211_IFTYPE_STATION)
			break;

		i = get_free_idx(mask, EXT_BSSID_1, EXT_BSSID_MAX);
		if (i)
			return i - 1;

		if (~mask & BIT(HW_BSSID_0))
			return HW_BSSID_0;

		break;
	case NL80211_IFTYPE_MONITOR:
	case NL80211_IFTYPE_AP:
		/* ap uses hw bssid 0 and ext bssid */
		if (~mask & BIT(HW_BSSID_0))
			return HW_BSSID_0;

		i = get_free_idx(mask, EXT_BSSID_1, EXT_BSSID_MAX);
		if (i)
			return i - 1;

		break;
	default:
		WARN_ON(1);
		break;
	}

	return -1;
}

static void
mt7996_init_bitrate_mask(struct ieee80211_vif *vif, struct mt7996_vif_link *mlink)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mlink->bitrate_mask.control); i++) {
		mlink->bitrate_mask.control[i].gi = NL80211_TXRATE_DEFAULT_GI;
		mlink->bitrate_mask.control[i].he_gi = 0xff;
		mlink->bitrate_mask.control[i].he_ltf = 0xff;
		mlink->bitrate_mask.control[i].legacy = GENMASK(31, 0);
		memset(mlink->bitrate_mask.control[i].ht_mcs, 0xff,
		       sizeof(mlink->bitrate_mask.control[i].ht_mcs));
		memset(mlink->bitrate_mask.control[i].vht_mcs, 0xff,
		       sizeof(mlink->bitrate_mask.control[i].vht_mcs));
		memset(mlink->bitrate_mask.control[i].he_mcs, 0xff,
		       sizeof(mlink->bitrate_mask.control[i].he_mcs));
	}
}

static int
mt7996_set_hw_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
		  struct ieee80211_vif *vif, struct ieee80211_sta *sta,
		  struct ieee80211_key_conf *key)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	int idx = key->keyidx;
	unsigned int link_id;
	unsigned long links;

	if (key->link_id >= 0)
		links = BIT(key->link_id);
	else if (sta && sta->valid_links)
		links = sta->valid_links;
	else if (vif->valid_links)
		links = vif->valid_links;
	else
		links = BIT(0);

	for_each_set_bit(link_id, &links, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct mt7996_sta_link *msta_link;
		struct mt7996_vif_link *link;
		u8 *wcid_keyidx;
		int err;

		link = mt7996_vif_link(dev, vif, link_id);
		if (!link)
			continue;

		if (sta) {
			struct mt7996_sta *msta;

			msta = (struct mt7996_sta *)sta->drv_priv;
			msta_link = mt76_dereference(msta->link[link_id],
						     &dev->mt76);
			if (!msta_link)
				continue;

			if (!msta_link->wcid.sta)
				return -EOPNOTSUPP;
		} else {
			msta_link = &link->msta_link;
		}
		wcid_keyidx = &msta_link->wcid.hw_key_idx;

		switch (key->cipher) {
		case WLAN_CIPHER_SUITE_AES_CMAC:
		case WLAN_CIPHER_SUITE_BIP_CMAC_256:
		case WLAN_CIPHER_SUITE_BIP_GMAC_128:
		case WLAN_CIPHER_SUITE_BIP_GMAC_256:
			if (key->keyidx == 6 || key->keyidx == 7) {
				wcid_keyidx = &msta_link->wcid.hw_key_idx2;
				key->flags |= IEEE80211_KEY_FLAG_GENERATE_MMIE;
			}
			break;
		default:
			break;
		}

		if (cmd == SET_KEY && !sta && !link->mt76.cipher) {
			struct ieee80211_bss_conf *link_conf;

			link_conf = link_conf_dereference_protected(vif,
								    link_id);
			if (!link_conf)
				link_conf = &vif->bss_conf;

			link->mt76.cipher =
				mt76_connac_mcu_get_cipher(key->cipher);
			mt7996_mcu_add_bss_info(link->phy, vif, link_conf,
						&link->mt76, msta_link, true);
		}

		if (cmd == SET_KEY) {
			*wcid_keyidx = idx;
		} else {
			if (idx == *wcid_keyidx)
				*wcid_keyidx = -1;
			continue;
		}

		mt76_wcid_key_setup(&dev->mt76, &msta_link->wcid, key);

		if (key->keyidx == 6 || key->keyidx == 7) {
			err = mt7996_mcu_bcn_prot_enable(dev, link,
							 msta_link, key);
			if (err)
				return err;
		}

		err = mt7996_mcu_add_key(&dev->mt76, vif, key,
					 MCU_WMWA_UNI_CMD(STA_REC_UPDATE),
					 &msta_link->wcid, cmd);
		if (err)
			return err;
	}

	return 0;
}

static void
mt7996_key_iter(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		struct ieee80211_sta *sta, struct ieee80211_key_conf *key,
		void *data)
{
	if (sta)
		return;

	WARN_ON(mt7996_set_hw_key(hw, SET_KEY, vif, NULL, key));
}

int mt7996_vif_link_add(struct mt76_phy *mphy, struct ieee80211_vif *vif,
			struct ieee80211_bss_conf *link_conf,
			struct mt76_vif_link *mlink)
{
	struct mt7996_vif_link *link = container_of(mlink, struct mt7996_vif_link, mt76);
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_sta_link *msta_link = &link->msta_link;
	struct mt7996_phy *phy = mphy->priv;
	struct mt7996_dev *dev = phy->dev;
	u8 band_idx = phy->mt76->band_idx;
	struct mt76_txq *mtxq;
	int idx, ret;

	mlink->idx = __ffs64(~dev->mt76.vif_mask);
	if (mlink->idx >= mt7996_max_interface_num(dev))
		return -ENOSPC;

	idx = get_omac_idx(vif->type, phy->omac_mask);
	if (idx < 0)
		return -ENOSPC;

	link->phy = phy;
	mlink->omac_idx = idx;
	mlink->band_idx = band_idx;
	mlink->wmm_idx = vif->type == NL80211_IFTYPE_AP ? 0 : 3;
	mlink->wcid = &msta_link->wcid;
	mlink->wcid->offchannel = mlink->offchannel;

	ret = mt7996_mcu_add_dev_info(phy, vif, link_conf, mlink, true);
	if (ret)
		return ret;

	dev->mt76.vif_mask |= BIT_ULL(mlink->idx);
	phy->omac_mask |= BIT_ULL(mlink->omac_idx);

	idx = MT7996_WTBL_RESERVED - mlink->idx;

	INIT_LIST_HEAD(&msta_link->rc_list);
	msta_link->wcid.idx = idx;
	msta_link->wcid.link_id = link_conf->link_id;
	msta_link->wcid.tx_info |= MT_WCID_TX_INFO_SET;
	mt76_wcid_init(&msta_link->wcid, band_idx);

	mt7996_mac_wtbl_update(dev, idx,
			       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);

	if (vif->txq) {
		mtxq = (struct mt76_txq *)vif->txq->drv_priv;
		mtxq->wcid = idx;
	}

	if (vif->type != NL80211_IFTYPE_AP &&
	    (!mlink->omac_idx || mlink->omac_idx > 3))
		vif->offload_flags = 0;

	if (phy->mt76->chandef.chan->band != NL80211_BAND_2GHZ)
		mlink->basic_rates_idx = MT7996_BASIC_RATES_TBL + 4;
	else
		mlink->basic_rates_idx = MT7996_BASIC_RATES_TBL;

	mt7996_init_bitrate_mask(vif, link);

	mt7996_mcu_add_bss_info(phy, vif, link_conf, mlink, msta_link, true);
	/* defer the first STA_REC of BMC entry to BSS_CHANGED_BSSID for STA
	 * interface, since firmware only records BSSID when the entry is new
	 */
	if (vif->type != NL80211_IFTYPE_STATION)
		mt7996_mcu_add_sta(dev, link_conf, NULL, link, NULL,
				   CONN_STATE_PORT_SECURE, true);
	rcu_assign_pointer(dev->mt76.wcid[idx], &msta_link->wcid);

	ieee80211_iter_keys(mphy->hw, vif, mt7996_key_iter, NULL);

	if (mvif->mt76.deflink_id == IEEE80211_LINK_UNSPECIFIED)
		mvif->mt76.deflink_id = link_conf->link_id;

	return 0;
}

void mt7996_vif_link_remove(struct mt76_phy *mphy, struct ieee80211_vif *vif,
			    struct ieee80211_bss_conf *link_conf,
			    struct mt76_vif_link *mlink)
{
	struct mt7996_vif_link *link = container_of(mlink, struct mt7996_vif_link, mt76);
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_sta_link *msta_link = &link->msta_link;
	struct mt7996_phy *phy = mphy->priv;
	struct mt7996_dev *dev = phy->dev;
	int idx = msta_link->wcid.idx;

	mt7996_mcu_add_sta(dev, link_conf, NULL, link, NULL,
			   CONN_STATE_DISCONNECT, false);
	mt7996_mcu_add_bss_info(phy, vif, link_conf, mlink, msta_link, false);

	mt7996_mcu_add_dev_info(phy, vif, link_conf, mlink, false);

	rcu_assign_pointer(dev->mt76.wcid[idx], NULL);

	if (mvif->mt76.deflink_id == link_conf->link_id) {
		struct ieee80211_bss_conf *iter;
		unsigned int link_id;

		mvif->mt76.deflink_id = IEEE80211_LINK_UNSPECIFIED;
		for_each_vif_active_link(vif, iter, link_id) {
			if (link_id != IEEE80211_LINK_UNSPECIFIED) {
				mvif->mt76.deflink_id = link_id;
				break;
			}
		}
	}

	dev->mt76.vif_mask &= ~BIT_ULL(mlink->idx);
	phy->omac_mask &= ~BIT_ULL(mlink->omac_idx);

	spin_lock_bh(&dev->mt76.sta_poll_lock);
	if (!list_empty(&msta_link->wcid.poll_list))
		list_del_init(&msta_link->wcid.poll_list);
	spin_unlock_bh(&dev->mt76.sta_poll_lock);

	mt76_wcid_cleanup(&dev->mt76, &msta_link->wcid);
}

static void mt7996_phy_set_rxfilter(struct mt7996_phy *phy)
{
	struct mt7996_dev *dev = phy->dev;
	u32 ctl_flags = MT_WF_RFCR1_DROP_ACK |
			MT_WF_RFCR1_DROP_BF_POLL |
			MT_WF_RFCR1_DROP_BA |
			MT_WF_RFCR1_DROP_CFEND |
			MT_WF_RFCR1_DROP_CFACK;
	u32 filter = phy->rxfilter;

	if (filter & MT_WF_RFCR_DROP_OTHER_UC) {
		filter |= MT_WF_RFCR_DROP_CTS |
			  MT_WF_RFCR_DROP_RTS |
			  MT_WF_RFCR_DROP_CTL_RSV |
			  MT_WF_RFCR_DROP_FCSFAIL;
	}

	mt76_wr(dev, MT_WF_RFCR(phy->mt76->band_idx), filter);
	if (filter & MT_WF_RFCR_DROP_CTL_RSV)
		mt76_set(dev, MT_WF_RFCR1(phy->mt76->band_idx), ctl_flags);
	else
		mt76_clear(dev, MT_WF_RFCR1(phy->mt76->band_idx), ctl_flags);
}

static void mt7996_set_monitor(struct mt7996_phy *phy, bool enabled)
{
	struct mt7996_dev *dev;

	if (!phy)
		return;

	dev = phy->dev;

	if (enabled == !(phy->rxfilter & MT_WF_RFCR_DROP_OTHER_UC))
		return;

	if (!enabled)
		phy->rxfilter |= MT_WF_RFCR_DROP_OTHER_UC;
	else
		phy->rxfilter &= ~MT_WF_RFCR_DROP_OTHER_UC;

	mt76_rmw_field(dev, MT_DMA_DCR0(phy->mt76->band_idx),
		       MT_DMA_DCR0_RXD_G5_EN, enabled);
	mt7996_phy_set_rxfilter(phy);
	mt7996_mcu_set_sniffer_mode(phy, enabled);
}

static int mt7996_add_interface(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif)
{
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct wireless_dev *wdev = ieee80211_vif_to_wdev(vif);
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	int i, err = 0;

	mutex_lock(&dev->mt76.mutex);

	for (i = 0; i < MT7996_MAX_RADIOS; i++) {
		struct mt7996_phy *phy = dev->radio_phy[i];

		if (!phy || !(wdev->radio_mask & BIT(i)) ||
		    test_bit(MT76_STATE_RUNNING, &phy->mt76->state))
			continue;

		err = mt7996_run(phy);
		if (err)
			goto out;

		if (vif->type == NL80211_IFTYPE_MONITOR)
			mt7996_set_monitor(phy, true);
	}

	mt76_vif_init(vif, &mvif->mt76);

	vif->offload_flags |= IEEE80211_OFFLOAD_ENCAP_4ADDR;
	mvif->mt76.deflink_id = IEEE80211_LINK_UNSPECIFIED;

out:
	mutex_unlock(&dev->mt76.mutex);

	return err;
}

struct mt7996_radio_data {
	u32 active_mask;
	u32 monitor_mask;
};

static void mt7996_remove_iter(void *data, u8 *mac, struct ieee80211_vif *vif)
{
	struct wireless_dev *wdev = ieee80211_vif_to_wdev(vif);
	struct mt7996_radio_data *rdata = data;

	rdata->active_mask |= wdev->radio_mask;
	if (vif->type == NL80211_IFTYPE_MONITOR)
		rdata->monitor_mask |= wdev->radio_mask;
}

static void mt7996_remove_interface(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_radio_data rdata = {};
	int i;

	ieee80211_iterate_active_interfaces_mtx(hw, 0, mt7996_remove_iter,
						&rdata);
	mt76_vif_cleanup(&dev->mt76, vif);

	for (i = 0; i < MT7996_MAX_RADIOS; i++) {
		struct mt7996_phy *phy = dev->radio_phy[i];

		if (!phy)
			continue;
		if (!(rdata.monitor_mask & BIT(i)))
			mt7996_set_monitor(phy, false);
		if (!(rdata.active_mask & BIT(i)))
			mt7996_stop_phy(phy);
	}
}

int mt7996_set_channel(struct mt76_phy *mphy)
{
	struct mt7996_phy *phy = mphy->priv;
	int ret;

	ret = mt7996_mcu_set_chan_info(phy, UNI_CHANNEL_SWITCH);
	if (ret)
		goto out;

	ret = mt7996_mcu_set_chan_info(phy, UNI_CHANNEL_RX_PATH);
	if (ret)
		goto out;

	ret = mt7996_mcu_set_txpower_sku(phy);
	if (ret)
		goto out;

	ret = mt7996_dfs_init_radar_detector(phy);
	mt7996_mac_cca_stats_reset(phy);

	mt7996_mac_reset_counters(phy);
	phy->noise = 0;

out:
	ieee80211_queue_delayed_work(mphy->hw, &mphy->mac_work,
				     MT7996_WATCHDOG_TIME);

	return ret;
}

static int mt7996_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			  struct ieee80211_vif *vif, struct ieee80211_sta *sta,
			  struct ieee80211_key_conf *key)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	int err;

	/* The hardware does not support per-STA RX GTK, fallback
	 * to software mode for these.
	 */
	if ((vif->type == NL80211_IFTYPE_ADHOC ||
	     vif->type == NL80211_IFTYPE_MESH_POINT) &&
	    (key->cipher == WLAN_CIPHER_SUITE_TKIP ||
	     key->cipher == WLAN_CIPHER_SUITE_CCMP) &&
	    !(key->flags & IEEE80211_KEY_FLAG_PAIRWISE))
		return -EOPNOTSUPP;

	/* fall back to sw encryption for unsupported ciphers */
	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_TKIP:
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_CCMP_256:
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
	case WLAN_CIPHER_SUITE_SMS4:
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
	case WLAN_CIPHER_SUITE_BIP_CMAC_256:
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
		if (key->keyidx == 6 || key->keyidx == 7)
			break;
		fallthrough;
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
	default:
		return -EOPNOTSUPP;
	}

	if (!mt7996_vif_link_phy(&mvif->deflink))
		return 0; /* defer until after link add */

	mutex_lock(&dev->mt76.mutex);
	err = mt7996_set_hw_key(hw, cmd, vif, sta, key);
	mutex_unlock(&dev->mt76.mutex);

	return err;
}

static int mt7996_config(struct ieee80211_hw *hw, int radio_idx, u32 changed)
{
	return 0;
}

static int
mt7996_conf_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	       unsigned int link_id, u16 queue,
	       const struct ieee80211_tx_queue_params *params)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_vif_link *mlink = mt7996_vif_link(dev, vif, link_id);
	static const u8 mq_to_aci[] = {
		[IEEE80211_AC_VO] = 3,
		[IEEE80211_AC_VI] = 2,
		[IEEE80211_AC_BE] = 0,
		[IEEE80211_AC_BK] = 1,
	};

	/* firmware uses access class index */
	mlink->queue_params[mq_to_aci[queue]] = *params;
	/* no need to update right away, we'll get BSS_CHANGED_QOS */

	return 0;
}

static void mt7996_configure_filter(struct ieee80211_hw *hw,
				    unsigned int changed_flags,
				    unsigned int *total_flags,
				    u64 multicast)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_phy *phy;
	u32 filter_mask = 0, filter_set = 0;
	u32 flags = 0;

#define MT76_FILTER(_flag, _hw) do {				\
		flags |= *total_flags & FIF_##_flag;		\
		filter_mask |= (_hw);				\
		filter_set |= !(flags & FIF_##_flag) * (_hw);	\
	} while (0)

	mutex_lock(&dev->mt76.mutex);

	MT76_FILTER(OTHER_BSS, MT_WF_RFCR_DROP_OTHER_TIM |
			       MT_WF_RFCR_DROP_A3_MAC |
			       MT_WF_RFCR_DROP_A3_BSSID);

	MT76_FILTER(FCSFAIL, MT_WF_RFCR_DROP_FCSFAIL);

	MT76_FILTER(CONTROL, MT_WF_RFCR_DROP_CTS |
			     MT_WF_RFCR_DROP_RTS |
			     MT_WF_RFCR_DROP_CTL_RSV);

	*total_flags = flags;

	mt7996_for_each_phy(dev, phy) {
		phy->rxfilter &= ~(MT_WF_RFCR_DROP_OTHER_BSS |
				   MT_WF_RFCR_DROP_OTHER_BEACON |
				   MT_WF_RFCR_DROP_FRAME_REPORT |
				   MT_WF_RFCR_DROP_PROBEREQ |
				   MT_WF_RFCR_DROP_MCAST_FILTERED |
				   MT_WF_RFCR_DROP_MCAST |
				   MT_WF_RFCR_DROP_BCAST |
				   MT_WF_RFCR_DROP_DUPLICATE |
				   MT_WF_RFCR_DROP_A2_BSSID |
				   MT_WF_RFCR_DROP_UNWANTED_CTL |
				   MT_WF_RFCR_DROP_STBC_MULTI |
				   filter_mask);
		phy->rxfilter |= filter_set;
		mt7996_phy_set_rxfilter(phy);
	}

	mutex_unlock(&dev->mt76.mutex);
}

static int
mt7996_get_txpower(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		   unsigned int link_id, int *dbm)
{
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_phy *phy = mt7996_vif_link_phy(&mvif->deflink);
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct wireless_dev *wdev;
	int n_chains, delta, i;

	if (!phy) {
		wdev = ieee80211_vif_to_wdev(vif);
		for (i = 0; i < hw->wiphy->n_radio; i++)
			if (wdev->radio_mask & BIT(i))
				phy = dev->radio_phy[i];

		if (!phy)
			return -EINVAL;
	}

	n_chains = hweight16(phy->mt76->chainmask);
	delta = mt76_tx_power_path_delta(n_chains);
	*dbm = DIV_ROUND_UP(phy->mt76->txpower_cur + delta, 2);

	return 0;
}

static u8
mt7996_get_rates_table(struct mt7996_phy *phy, struct ieee80211_bss_conf *conf,
		       bool beacon, bool mcast)
{
	struct mt7996_dev *dev = phy->dev;
	struct mt76_vif_link *mvif = mt76_vif_conf_link(&dev->mt76, conf->vif, conf);
	u16 rate;
	u8 i, idx;

	rate = mt76_connac2_mac_tx_rate_val(phy->mt76, conf, beacon, mcast);

	if (beacon) {
		/* odd index for driver, even index for firmware */
		idx = MT7996_BEACON_RATES_TBL + 2 * phy->mt76->band_idx;
		if (phy->beacon_rate != rate)
			mt7996_mcu_set_fixed_rate_table(phy, idx, rate, beacon);

		return idx;
	}

	idx = FIELD_GET(MT_TX_RATE_IDX, rate);
	for (i = 0; i < ARRAY_SIZE(mt76_rates); i++)
		if ((mt76_rates[i].hw_value & GENMASK(7, 0)) == idx)
			return MT7996_BASIC_RATES_TBL + 2 * i;

	return mvif->basic_rates_idx;
}

static void
mt7996_update_mu_group(struct ieee80211_hw *hw, struct mt7996_vif_link *link,
		       struct ieee80211_bss_conf *info)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	u8 band = link->mt76.band_idx;
	u32 *mu;

	mu = (u32 *)info->mu_group.membership;
	mt76_wr(dev, MT_WF_PHYRX_BAND_GID_TAB_VLD0(band), mu[0]);
	mt76_wr(dev, MT_WF_PHYRX_BAND_GID_TAB_VLD1(band), mu[1]);

	mu = (u32 *)info->mu_group.position;
	mt76_wr(dev, MT_WF_PHYRX_BAND_GID_TAB_POS0(band), mu[0]);
	mt76_wr(dev, MT_WF_PHYRX_BAND_GID_TAB_POS1(band), mu[1]);
	mt76_wr(dev, MT_WF_PHYRX_BAND_GID_TAB_POS2(band), mu[2]);
	mt76_wr(dev, MT_WF_PHYRX_BAND_GID_TAB_POS3(band), mu[3]);
}

static void
mt7996_vif_cfg_changed(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		       u64 changed)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);

	mutex_lock(&dev->mt76.mutex);

	if ((changed & BSS_CHANGED_ASSOC) && vif->cfg.assoc) {
		struct ieee80211_bss_conf *link_conf;
		unsigned long link_id;

		for_each_vif_active_link(vif, link_conf, link_id) {
			struct mt7996_vif_link *link;

			link = mt7996_vif_link(dev, vif, link_id);
			if (!link)
				continue;

			if (!link->phy)
				continue;

			mt7996_mcu_add_bss_info(link->phy, vif, link_conf,
						&link->mt76, &link->msta_link,
						true);
			mt7996_mcu_add_sta(dev, link_conf, NULL, link, NULL,
					   CONN_STATE_PORT_SECURE,
					   !!(changed & BSS_CHANGED_BSSID));
		}
	}

	mutex_unlock(&dev->mt76.mutex);
}

static void
mt7996_link_info_changed(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			 struct ieee80211_bss_conf *info, u64 changed)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_vif_link *link;
	struct mt7996_phy *phy;
	struct mt76_phy *mphy;

	mutex_lock(&dev->mt76.mutex);

	link = mt7996_vif_conf_link(dev, vif, info);
	if (!link)
		goto out;

	mphy = mt76_vif_link_phy(&link->mt76);
	if (!mphy)
		goto out;

	phy = mphy->priv;

	/* station mode uses BSSID to map the wlan entry to a peer,
	 * and then peer references bss_info_rfch to set bandwidth cap.
	 */
	if ((changed & BSS_CHANGED_BSSID && !is_zero_ether_addr(info->bssid)) ||
	    (changed & BSS_CHANGED_BEACON_ENABLED && info->enable_beacon)) {
		mt7996_mcu_add_bss_info(phy, vif, info, &link->mt76,
					&link->msta_link, true);
		mt7996_mcu_add_sta(dev, info, NULL, link, NULL,
				   CONN_STATE_PORT_SECURE,
				   !!(changed & BSS_CHANGED_BSSID));
	}

	if (changed & BSS_CHANGED_ERP_SLOT) {
		int slottime = info->use_short_slot ? 9 : 20;

		if (slottime != phy->slottime) {
			phy->slottime = slottime;
			mt7996_mcu_set_timing(phy, vif, info);
		}
	}

	if (changed & BSS_CHANGED_MCAST_RATE)
		link->mt76.mcast_rates_idx =
			mt7996_get_rates_table(phy, info, false, true);

	if (changed & BSS_CHANGED_BASIC_RATES)
		link->mt76.basic_rates_idx =
			mt7996_get_rates_table(phy, info, false, false);

	/* ensure that enable txcmd_mode after bss_info */
	if (changed & (BSS_CHANGED_QOS | BSS_CHANGED_BEACON_ENABLED))
		mt7996_mcu_set_tx(dev, vif, info);

	if (changed & BSS_CHANGED_HE_OBSS_PD)
		mt7996_mcu_add_obss_spr(phy, link, &info->he_obss_pd);

	if (changed & BSS_CHANGED_HE_BSS_COLOR) {
		if ((vif->type == NL80211_IFTYPE_AP &&
		    link->mt76.omac_idx <= HW_BSSID_MAX) ||
		   vif->type == NL80211_IFTYPE_STATION)
			mt7996_mcu_update_bss_color(dev, &link->mt76,
						    &info->he_bss_color);
	}

	if (changed & (BSS_CHANGED_BEACON |
		       BSS_CHANGED_BEACON_ENABLED)) {
		link->mt76.beacon_rates_idx =
			mt7996_get_rates_table(phy, info, true, false);

		mt7996_mcu_add_beacon(hw, vif, info);
	}

	if (changed & (BSS_CHANGED_UNSOL_BCAST_PROBE_RESP |
		       BSS_CHANGED_FILS_DISCOVERY))
		mt7996_mcu_beacon_inband_discov(dev, info, link, changed);

	if (changed & BSS_CHANGED_MU_GROUPS)
		mt7996_update_mu_group(hw, link, info);

	if (changed & BSS_CHANGED_TXPOWER &&
	    info->txpower != phy->txpower) {
		phy->txpower = info->txpower;
		mt7996_mcu_set_txpower_sku(phy);
	}

out:
	mutex_unlock(&dev->mt76.mutex);
}

static void
mt7996_channel_switch_beacon(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct cfg80211_chan_def *chandef)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);

	mutex_lock(&dev->mt76.mutex);
	mt7996_mcu_add_beacon(hw, vif, &vif->bss_conf);
	mutex_unlock(&dev->mt76.mutex);
}

static int
mt7996_mac_sta_init_link(struct mt7996_dev *dev,
			 struct ieee80211_bss_conf *link_conf,
			 struct ieee80211_link_sta *link_sta,
			 struct mt7996_vif_link *link, unsigned int link_id)
{
	struct ieee80211_sta *sta = link_sta->sta;
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt7996_phy *phy = link->phy;
	struct mt7996_sta_link *msta_link;
	int idx;

	idx = mt76_wcid_alloc(dev->mt76.wcid_mask, MT7996_WTBL_STA);
	if (idx < 0)
		return -ENOSPC;

	if (msta->deflink_id == IEEE80211_LINK_UNSPECIFIED) {
		int i;

		msta_link = &msta->deflink;
		msta->deflink_id = link_id;

		for (i = 0; i < ARRAY_SIZE(sta->txq); i++) {
			struct mt76_txq *mtxq;

			if (!sta->txq[i])
				continue;

			mtxq = (struct mt76_txq *)sta->txq[i]->drv_priv;
			mtxq->wcid = idx;
		}
	} else {
		msta_link = kzalloc(sizeof(*msta_link), GFP_KERNEL);
		if (!msta_link)
			return -ENOMEM;
	}

	INIT_LIST_HEAD(&msta_link->rc_list);
	INIT_LIST_HEAD(&msta_link->wcid.poll_list);
	msta_link->sta = msta;
	msta_link->wcid.sta = 1;
	msta_link->wcid.idx = idx;
	msta_link->wcid.link_id = link_id;

	ewma_avg_signal_init(&msta_link->avg_ack_signal);
	ewma_signal_init(&msta_link->wcid.rssi);

	rcu_assign_pointer(msta->link[link_id], msta_link);

	mt7996_mac_wtbl_update(dev, idx, MT_WTBL_UPDATE_ADM_COUNT_CLEAR);
	mt7996_mcu_add_sta(dev, link_conf, link_sta, link, msta_link,
			   CONN_STATE_DISCONNECT, true);

	rcu_assign_pointer(dev->mt76.wcid[idx], &msta_link->wcid);
	mt76_wcid_init(&msta_link->wcid, phy->mt76->band_idx);

	return 0;
}

static void
mt7996_mac_sta_deinit_link(struct mt7996_dev *dev,
			   struct mt7996_sta_link *msta_link)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(msta_link->wcid.aggr); i++)
		mt76_rx_aggr_stop(&dev->mt76, &msta_link->wcid, i);

	mt7996_mac_wtbl_update(dev, msta_link->wcid.idx,
			       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);

	spin_lock_bh(&dev->mt76.sta_poll_lock);
	if (!list_empty(&msta_link->wcid.poll_list))
		list_del_init(&msta_link->wcid.poll_list);
	if (!list_empty(&msta_link->rc_list))
		list_del_init(&msta_link->rc_list);
	spin_unlock_bh(&dev->mt76.sta_poll_lock);

	mt76_wcid_cleanup(&dev->mt76, &msta_link->wcid);
	mt76_wcid_mask_clear(dev->mt76.wcid_mask, msta_link->wcid.idx);
}

static void
mt7996_mac_sta_remove_links(struct mt7996_dev *dev, struct ieee80211_sta *sta,
			    unsigned long links)
{
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt76_dev *mdev = &dev->mt76;
	unsigned int link_id;

	for_each_set_bit(link_id, &links, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct mt7996_sta_link *msta_link = NULL;

		msta_link = rcu_replace_pointer(msta->link[link_id], msta_link,
						lockdep_is_held(&mdev->mutex));
		if (!msta_link)
			continue;

		mt7996_mac_sta_deinit_link(dev, msta_link);
		if (msta->deflink_id == link_id) {
			msta->deflink_id = IEEE80211_LINK_UNSPECIFIED;
			continue;
		}

		kfree_rcu(msta_link, rcu_head);
	}
}

static int
mt7996_mac_sta_add_links(struct mt7996_dev *dev, struct ieee80211_vif *vif,
			 struct ieee80211_sta *sta, unsigned long new_links)
{
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	unsigned int link_id;
	int err = 0;

	for_each_set_bit(link_id, &new_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct ieee80211_bss_conf *link_conf;
		struct ieee80211_link_sta *link_sta;
		struct mt7996_vif_link *link;

		if (rcu_access_pointer(msta->link[link_id]))
			continue;

		link_conf = link_conf_dereference_protected(vif, link_id);
		if (!link_conf) {
			err = -EINVAL;
			goto error_unlink;
		}

		link = mt7996_vif_link(dev, vif, link_id);
		if (!link) {
			err = -EINVAL;
			goto error_unlink;
		}

		link_sta = link_sta_dereference_protected(sta, link_id);
		if (!link_sta) {
			err = -EINVAL;
			goto error_unlink;
		}

		err = mt7996_mac_sta_init_link(dev, link_conf, link_sta, link,
					       link_id);
		if (err)
			goto error_unlink;
	}

	return 0;

error_unlink:
	mt7996_mac_sta_remove_links(dev, sta, new_links);

	return err;
}

static int
mt7996_mac_sta_change_links(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta, u16 old_links,
			    u16 new_links)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	unsigned long add = new_links & ~old_links;
	unsigned long rem = old_links & ~new_links;
	int ret;

	mutex_lock(&dev->mt76.mutex);

	mt7996_mac_sta_remove_links(dev, sta, rem);
	ret = mt7996_mac_sta_add_links(dev, vif, sta, add);

	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

static int
mt7996_mac_sta_add(struct mt76_phy *mphy, struct ieee80211_vif *vif,
		   struct ieee80211_sta *sta)
{
	struct mt76_dev *mdev = mphy->dev;
	struct mt7996_dev *dev = container_of(mdev, struct mt7996_dev, mt76);
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	unsigned long links = sta->mlo ? sta->valid_links : BIT(0);
	int err;

	mutex_lock(&mdev->mutex);

	msta->deflink_id = IEEE80211_LINK_UNSPECIFIED;
	msta->vif = mvif;
	err = mt7996_mac_sta_add_links(dev, vif, sta, links);
	if (!err)
		mphy->num_sta++;

	mutex_unlock(&mdev->mutex);

	return err;
}

static int
mt7996_mac_sta_event(struct mt7996_dev *dev, struct ieee80211_vif *vif,
		     struct ieee80211_sta *sta, enum mt76_sta_event ev)
{
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	unsigned long links = sta->valid_links;
	struct ieee80211_link_sta *link_sta;
	unsigned int link_id;

	for_each_sta_active_link(vif, sta, link_sta, link_id) {
		struct ieee80211_bss_conf *link_conf;
		struct mt7996_sta_link *msta_link;
		struct mt7996_vif_link *link;
		int i, err;

		link_conf = link_conf_dereference_protected(vif, link_id);
		if (!link_conf)
			continue;

		link = mt7996_vif_link(dev, vif, link_id);
		if (!link)
			continue;

		msta_link = mt76_dereference(msta->link[link_id], &dev->mt76);
		if (!msta_link)
			continue;

		switch (ev) {
		case MT76_STA_EVENT_ASSOC:
			err = mt7996_mcu_add_sta(dev, link_conf, link_sta,
						 link, msta_link,
						 CONN_STATE_CONNECT, true);
			if (err)
				return err;

			err = mt7996_mcu_add_rate_ctrl(dev, vif, link_conf,
						       link_sta, link,
						       msta_link, false);
			if (err)
				return err;

			msta_link->wcid.tx_info |= MT_WCID_TX_INFO_SET;
			msta_link->wcid.sta = 1;
			break;
		case MT76_STA_EVENT_AUTHORIZE:
			err = mt7996_mcu_add_sta(dev, link_conf, link_sta,
						 link, msta_link,
						 CONN_STATE_PORT_SECURE, false);
			if (err)
				return err;
			break;
		case MT76_STA_EVENT_DISASSOC:
			for (i = 0; i < ARRAY_SIZE(msta_link->twt.flow); i++)
				mt7996_mac_twt_teardown_flow(dev, link,
							     msta_link, i);

			if (sta->mlo && links == BIT(link_id)) /* last link */
				mt7996_mcu_teardown_mld_sta(dev, link,
							    msta_link);
			else
				mt7996_mcu_add_sta(dev, link_conf, link_sta,
						   link, msta_link,
						   CONN_STATE_DISCONNECT, false);
			msta_link->wcid.sta_disabled = 1;
			msta_link->wcid.sta = 0;
			links = links & ~BIT(link_id);
			break;
		}
	}

	return 0;
}

static void
mt7996_mac_sta_remove(struct mt76_phy *mphy, struct ieee80211_vif *vif,
		      struct ieee80211_sta *sta)
{
	struct mt76_dev *mdev = mphy->dev;
	struct mt7996_dev *dev = container_of(mdev, struct mt7996_dev, mt76);
	unsigned long links = sta->mlo ? sta->valid_links : BIT(0);

	mutex_lock(&mdev->mutex);

	mt7996_mac_sta_remove_links(dev, sta, links);
	mphy->num_sta--;

	mutex_unlock(&mdev->mutex);
}

static int
mt7996_sta_state(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		 struct ieee80211_sta *sta, enum ieee80211_sta_state old_state,
		 enum ieee80211_sta_state new_state)
{
	struct mt76_phy *mphy = mt76_vif_phy(hw, vif);
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	enum mt76_sta_event ev;

	if (!mphy)
		return -EINVAL;

	if (old_state == IEEE80211_STA_NOTEXIST &&
	    new_state == IEEE80211_STA_NONE)
		return mt7996_mac_sta_add(mphy, vif, sta);

	if (old_state == IEEE80211_STA_NONE &&
	    new_state == IEEE80211_STA_NOTEXIST)
		mt7996_mac_sta_remove(mphy, vif, sta);

	if (old_state == IEEE80211_STA_AUTH &&
	    new_state == IEEE80211_STA_ASSOC)
		ev = MT76_STA_EVENT_ASSOC;
	else if (old_state == IEEE80211_STA_ASSOC &&
		 new_state == IEEE80211_STA_AUTHORIZED)
		ev = MT76_STA_EVENT_AUTHORIZE;
	else if (old_state == IEEE80211_STA_ASSOC &&
		 new_state == IEEE80211_STA_AUTH)
		ev = MT76_STA_EVENT_DISASSOC;
	else
		return 0;

	return mt7996_mac_sta_event(dev, vif, sta, ev);
}

static void mt7996_tx(struct ieee80211_hw *hw,
		      struct ieee80211_tx_control *control,
		      struct sk_buff *skb)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt76_phy *mphy = hw->priv;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_vif *vif = info->control.vif;
	struct mt76_wcid *wcid = &dev->mt76.global_wcid;
	u8 link_id = u32_get_bits(info->control.flags,
				  IEEE80211_TX_CTRL_MLO_LINK);

	rcu_read_lock();

	if (vif) {
		struct mt7996_vif *mvif = (void *)vif->drv_priv;
		struct mt76_vif_link *mlink;

		mlink = rcu_dereference(mvif->mt76.link[link_id]);
		if (mlink && mlink->wcid)
			wcid = mlink->wcid;

		if (mvif->mt76.roc_phy &&
		    (info->flags & IEEE80211_TX_CTL_TX_OFFCHAN)) {
			mphy = mvif->mt76.roc_phy;
			if (mphy->roc_link)
				wcid = mphy->roc_link->wcid;
		} else {
			mphy = mt76_vif_link_phy(&mvif->deflink.mt76);
		}
	}

	if (!mphy) {
		ieee80211_free_txskb(hw, skb);
		goto unlock;
	}

	if (control->sta) {
		struct mt7996_sta *msta = (void *)control->sta->drv_priv;
		struct mt7996_sta_link *msta_link;

		msta_link = rcu_dereference(msta->link[link_id]);
		if (msta_link)
			wcid = &msta_link->wcid;
	}
	mt76_tx(mphy, control->sta, wcid, skb);
unlock:
	rcu_read_unlock();
}

static int mt7996_set_rts_threshold(struct ieee80211_hw *hw, int radio_idx,
				    u32 val)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	int i, ret = 0;

	mutex_lock(&dev->mt76.mutex);

	for (i = 0; i < hw->wiphy->n_radio; i++) {
		struct mt7996_phy *phy = dev->radio_phy[i];

		ret = mt7996_mcu_set_rts_thresh(phy, val);
		if (ret)
			break;
	}

	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

static int
mt7996_ampdu_action(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		    struct ieee80211_ampdu_params *params)
{
	enum ieee80211_ampdu_mlme_action action = params->action;
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct ieee80211_sta *sta = params->sta;
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct ieee80211_txq *txq = sta->txq[params->tid];
	struct ieee80211_link_sta *link_sta;
	u16 tid = params->tid;
	u16 ssn = params->ssn;
	struct mt76_txq *mtxq;
	unsigned int link_id;
	int ret = 0;

	if (!txq)
		return -EINVAL;

	mtxq = (struct mt76_txq *)txq->drv_priv;

	mutex_lock(&dev->mt76.mutex);

	for_each_sta_active_link(vif, sta, link_sta, link_id) {
		struct mt7996_sta_link *msta_link;
		struct mt7996_vif_link *link;

		msta_link = mt76_dereference(msta->link[link_id], &dev->mt76);
		if (!msta_link)
			continue;

		link = mt7996_vif_link(dev, vif, link_id);
		if (!link)
			continue;

		switch (action) {
		case IEEE80211_AMPDU_RX_START:
			mt76_rx_aggr_start(&dev->mt76, &msta_link->wcid, tid,
					   ssn, params->buf_size);
			ret = mt7996_mcu_add_rx_ba(dev, params, link, true);
			break;
		case IEEE80211_AMPDU_RX_STOP:
			mt76_rx_aggr_stop(&dev->mt76, &msta_link->wcid, tid);
			ret = mt7996_mcu_add_rx_ba(dev, params, link, false);
			break;
		case IEEE80211_AMPDU_TX_OPERATIONAL:
			mtxq->aggr = true;
			mtxq->send_bar = false;
			ret = mt7996_mcu_add_tx_ba(dev, params, link,
						   msta_link, true);
			break;
		case IEEE80211_AMPDU_TX_STOP_FLUSH:
		case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
			mtxq->aggr = false;
			clear_bit(tid, &msta_link->wcid.ampdu_state);
			ret = mt7996_mcu_add_tx_ba(dev, params, link,
						   msta_link, false);
			break;
		case IEEE80211_AMPDU_TX_START:
			set_bit(tid, &msta_link->wcid.ampdu_state);
			ret = IEEE80211_AMPDU_TX_START_IMMEDIATE;
			break;
		case IEEE80211_AMPDU_TX_STOP_CONT:
			mtxq->aggr = false;
			clear_bit(tid, &msta_link->wcid.ampdu_state);
			ret = mt7996_mcu_add_tx_ba(dev, params, link,
						   msta_link, false);
			break;
		}

		if (ret)
			break;
	}

	if (action == IEEE80211_AMPDU_TX_STOP_CONT)
		ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);

	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

static int
mt7996_get_stats(struct ieee80211_hw *hw,
		 struct ieee80211_low_level_stats *stats)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	int i;

	mutex_lock(&dev->mt76.mutex);

	memset(stats, 0, sizeof(*stats));
	for (i = 0; i < hw->wiphy->n_radio; i++) {
		struct mt7996_phy *phy = dev->radio_phy[i];
		struct mt76_mib_stats *mib = &phy->mib;

		stats->dot11RTSSuccessCount += mib->rts_cnt;
		stats->dot11RTSFailureCount += mib->rts_retries_cnt;
		stats->dot11FCSErrorCount += mib->fcs_err_cnt;
		stats->dot11ACKFailureCount += mib->ack_fail_cnt;
	}

	mutex_unlock(&dev->mt76.mutex);

	return 0;
}

u64 __mt7996_get_tsf(struct ieee80211_hw *hw, struct mt7996_vif_link *link)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_phy *phy = link->phy;
	union {
		u64 t64;
		u32 t32[2];
	} tsf;
	u16 n;

	if (!phy)
		return 0;

	lockdep_assert_held(&dev->mt76.mutex);

	n = link->mt76.omac_idx > HW_BSSID_MAX ? HW_BSSID_0
					       : link->mt76.omac_idx;
	/* TSF software read */
	mt76_rmw(dev, MT_LPON_TCR(phy->mt76->band_idx, n), MT_LPON_TCR_SW_MODE,
		 MT_LPON_TCR_SW_READ);
	tsf.t32[0] = mt76_rr(dev, MT_LPON_UTTR0(phy->mt76->band_idx));
	tsf.t32[1] = mt76_rr(dev, MT_LPON_UTTR1(phy->mt76->band_idx));

	return tsf.t64;
}

static u64
mt7996_get_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	u64 ret;

	mutex_lock(&dev->mt76.mutex);
	ret = __mt7996_get_tsf(hw, &mvif->deflink);
	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

static void
mt7996_set_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	       u64 timestamp)
{
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_vif_link *link;
	struct mt7996_phy *phy;
	union {
		u64 t64;
		u32 t32[2];
	} tsf = { .t64 = timestamp, };
	u16 n;

	mutex_lock(&dev->mt76.mutex);

	link = mt7996_vif_link(dev, vif, mvif->mt76.deflink_id);
	if (!link)
		goto unlock;

	n = link->mt76.omac_idx > HW_BSSID_MAX ? HW_BSSID_0
					       : link->mt76.omac_idx;
	phy = link->phy;
	if (!phy)
		goto unlock;

	mt76_wr(dev, MT_LPON_UTTR0(phy->mt76->band_idx), tsf.t32[0]);
	mt76_wr(dev, MT_LPON_UTTR1(phy->mt76->band_idx), tsf.t32[1]);
	/* TSF software overwrite */
	mt76_rmw(dev, MT_LPON_TCR(phy->mt76->band_idx, n), MT_LPON_TCR_SW_MODE,
		 MT_LPON_TCR_SW_WRITE);

unlock:
	mutex_unlock(&dev->mt76.mutex);
}

static void
mt7996_offset_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		  s64 timestamp)
{
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_vif_link *link;
	struct mt7996_phy *phy;
	union {
		u64 t64;
		u32 t32[2];
	} tsf = { .t64 = timestamp, };
	u16 n;

	mutex_lock(&dev->mt76.mutex);

	link = mt7996_vif_link(dev, vif, mvif->mt76.deflink_id);
	if (!link)
		goto unlock;

	phy = link->phy;
	if (!phy)
		goto unlock;

	n = link->mt76.omac_idx > HW_BSSID_MAX ? HW_BSSID_0
					       : link->mt76.omac_idx;
	mt76_wr(dev, MT_LPON_UTTR0(phy->mt76->band_idx), tsf.t32[0]);
	mt76_wr(dev, MT_LPON_UTTR1(phy->mt76->band_idx), tsf.t32[1]);
	/* TSF software adjust*/
	mt76_rmw(dev, MT_LPON_TCR(phy->mt76->band_idx, n), MT_LPON_TCR_SW_MODE,
		 MT_LPON_TCR_SW_ADJUST);

unlock:
	mutex_unlock(&dev->mt76.mutex);
}

static void
mt7996_set_coverage_class(struct ieee80211_hw *hw, int radio_idx,
			  s16 coverage_class)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_phy *phy;

	mutex_lock(&dev->mt76.mutex);
	mt7996_for_each_phy(dev, phy) {
		phy->coverage_class = max_t(s16, coverage_class, 0);
		mt7996_mac_set_coverage_class(phy);
	}
	mutex_unlock(&dev->mt76.mutex);
}

static int
mt7996_set_antenna(struct ieee80211_hw *hw, int radio_idx,
		   u32 tx_ant, u32 rx_ant)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	int i;

	if (tx_ant != rx_ant)
		return -EINVAL;

	for (i = 0; i < hw->wiphy->n_radio; i++) {
		struct mt7996_phy *phy = dev->radio_phy[i];

		if (!(tx_ant & phy->orig_chainmask))
			return -EINVAL;
	}

	mutex_lock(&dev->mt76.mutex);

	for (i = 0; i < hw->wiphy->n_radio; i++) {
		struct mt7996_phy *phy = dev->radio_phy[i];
		u8 band_idx = phy->mt76->band_idx;
		u8 shift = dev->chainshift[band_idx];

		phy->mt76->chainmask = tx_ant & phy->orig_chainmask;
		phy->mt76->antenna_mask = (phy->mt76->chainmask >> shift) &
					  phy->orig_antenna_mask;

		mt76_set_stream_caps(phy->mt76, true);
		mt7996_set_stream_vht_txbf_caps(phy);
		mt7996_set_stream_he_eht_caps(phy);
		mt7996_mcu_set_txpower_sku(phy);
	}

	mutex_unlock(&dev->mt76.mutex);

	return 0;
}

static void mt7996_sta_statistics(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct ieee80211_sta *sta,
				  struct station_info *sinfo)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt7996_sta_link *msta_link = &msta->deflink;
	struct rate_info *txrate = &msta_link->wcid.rate;

	if (txrate->legacy || txrate->flags) {
		if (txrate->legacy) {
			sinfo->txrate.legacy = txrate->legacy;
		} else {
			sinfo->txrate.mcs = txrate->mcs;
			sinfo->txrate.nss = txrate->nss;
			sinfo->txrate.bw = txrate->bw;
			sinfo->txrate.he_gi = txrate->he_gi;
			sinfo->txrate.he_dcm = txrate->he_dcm;
			sinfo->txrate.he_ru_alloc = txrate->he_ru_alloc;
			sinfo->txrate.eht_gi = txrate->eht_gi;
		}
		sinfo->txrate.flags = txrate->flags;
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_BITRATE);
	}
	sinfo->txrate.flags = txrate->flags;
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_BITRATE);

	sinfo->tx_failed = msta_link->wcid.stats.tx_failed;
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_FAILED);

	sinfo->tx_retries = msta_link->wcid.stats.tx_retries;
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_RETRIES);

	sinfo->ack_signal = (s8)msta_link->ack_signal;
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_ACK_SIGNAL);

	sinfo->avg_ack_signal =
		-(s8)ewma_avg_signal_read(&msta_link->avg_ack_signal);
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_ACK_SIGNAL_AVG);

	if (mtk_wed_device_active(&dev->mt76.mmio.wed)) {
		sinfo->tx_bytes = msta_link->wcid.stats.tx_bytes;
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_BYTES64);

		sinfo->rx_bytes = msta_link->wcid.stats.rx_bytes;
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_RX_BYTES64);

		sinfo->tx_packets = msta_link->wcid.stats.tx_packets;
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_PACKETS);

		sinfo->rx_packets = msta_link->wcid.stats.rx_packets;
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_RX_PACKETS);
	}
}

static void mt7996_link_rate_ctrl_update(void *data, struct ieee80211_sta *sta)
{
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt7996_dev *dev = msta->vif->deflink.phy->dev;
	struct mt7996_sta_link *msta_link;
	u32 *changed = data;

	rcu_read_lock();

	msta_link = rcu_dereference(msta->link[msta->deflink_id]);
	if (!msta_link)
		goto out;

	spin_lock_bh(&dev->mt76.sta_poll_lock);

	msta_link->changed |= *changed;
	if (list_empty(&msta_link->rc_list))
		list_add_tail(&msta_link->rc_list, &dev->sta_rc_list);

	spin_unlock_bh(&dev->mt76.sta_poll_lock);
out:
	rcu_read_unlock();
}

static void mt7996_link_sta_rc_update(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      struct ieee80211_link_sta *link_sta,
				      u32 changed)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct ieee80211_sta *sta = link_sta->sta;

	mt7996_link_rate_ctrl_update(&changed, sta);
	ieee80211_queue_work(hw, &dev->rc_work);
}

static int
mt7996_set_bitrate_mask(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			const struct cfg80211_bitrate_mask *mask)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	u32 changed = IEEE80211_RC_SUPP_RATES_CHANGED;

	mvif->deflink.bitrate_mask = *mask;

	/* if multiple rates across different preambles are given we can
	 * reconfigure this info with all peers using sta_rec command with
	 * the below exception cases.
	 * - single rate : if a rate is passed along with different preambles,
	 * we select the highest one as fixed rate. i.e VHT MCS for VHT peers.
	 * - multiple rates: if it's not in range format i.e 0-{7,8,9} for VHT
	 * then multiple MCS setting (MCS 4,5,6) is not supported.
	 */
	ieee80211_iterate_stations_atomic(hw, mt7996_link_rate_ctrl_update,
					  &changed);
	ieee80211_queue_work(hw, &dev->rc_work);

	return 0;
}

static void mt7996_sta_set_4addr(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct ieee80211_sta *sta,
				 bool enabled)
{
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct ieee80211_link_sta *link_sta;
	unsigned int link_id;

	mutex_lock(&dev->mt76.mutex);

	for_each_sta_active_link(vif, sta, link_sta, link_id) {
		struct mt7996_sta_link *msta_link;
		struct mt7996_vif_link *link;

		link = mt7996_vif_link(dev, vif, link_id);
		if (!link)
			continue;

		msta_link = mt76_dereference(msta->link[link_id], &dev->mt76);
		if (!msta_link)
			continue;

		if (enabled)
			set_bit(MT_WCID_FLAG_4ADDR, &msta_link->wcid.flags);
		else
			clear_bit(MT_WCID_FLAG_4ADDR, &msta_link->wcid.flags);

		if (!msta_link->wcid.sta)
			continue;

		mt7996_mcu_wtbl_update_hdr_trans(dev, vif, link, msta_link);
	}

	mutex_unlock(&dev->mt76.mutex);
}

static void mt7996_sta_set_decap_offload(struct ieee80211_hw *hw,
					 struct ieee80211_vif *vif,
					 struct ieee80211_sta *sta,
					 bool enabled)
{
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct ieee80211_link_sta *link_sta;
	unsigned int link_id;

	mutex_lock(&dev->mt76.mutex);

	for_each_sta_active_link(vif, sta, link_sta, link_id) {
		struct mt7996_sta_link *msta_link;
		struct mt7996_vif_link *link;

		link = mt7996_vif_link(dev, vif, link_id);
		if (!link)
			continue;

		msta_link = mt76_dereference(msta->link[link_id], &dev->mt76);
		if (!msta_link)
			continue;

		if (enabled)
			set_bit(MT_WCID_FLAG_HDR_TRANS,
				&msta_link->wcid.flags);
		else
			clear_bit(MT_WCID_FLAG_HDR_TRANS,
				  &msta_link->wcid.flags);

		if (!msta_link->wcid.sta)
			continue;

		mt7996_mcu_wtbl_update_hdr_trans(dev, vif, link, msta_link);
	}

	mutex_unlock(&dev->mt76.mutex);
}

static const char mt7996_gstrings_stats[][ETH_GSTRING_LEN] = {
	"tx_ampdu_cnt",
	"tx_stop_q_empty_cnt",
	"tx_mpdu_attempts",
	"tx_mpdu_success",
	"tx_rwp_fail_cnt",
	"tx_rwp_need_cnt",
	"tx_pkt_ebf_cnt",
	"tx_pkt_ibf_cnt",
	"tx_ampdu_len:0-1",
	"tx_ampdu_len:2-10",
	"tx_ampdu_len:11-19",
	"tx_ampdu_len:20-28",
	"tx_ampdu_len:29-37",
	"tx_ampdu_len:38-46",
	"tx_ampdu_len:47-55",
	"tx_ampdu_len:56-79",
	"tx_ampdu_len:80-103",
	"tx_ampdu_len:104-127",
	"tx_ampdu_len:128-151",
	"tx_ampdu_len:152-175",
	"tx_ampdu_len:176-199",
	"tx_ampdu_len:200-223",
	"tx_ampdu_len:224-247",
	"ba_miss_count",
	"tx_beamformer_ppdu_iBF",
	"tx_beamformer_ppdu_eBF",
	"tx_beamformer_rx_feedback_all",
	"tx_beamformer_rx_feedback_he",
	"tx_beamformer_rx_feedback_vht",
	"tx_beamformer_rx_feedback_ht",
	"tx_beamformer_rx_feedback_bw", /* zero based idx: 20, 40, 80, 160 */
	"tx_beamformer_rx_feedback_nc",
	"tx_beamformer_rx_feedback_nr",
	"tx_beamformee_ok_feedback_pkts",
	"tx_beamformee_feedback_trig",
	"tx_mu_beamforming",
	"tx_mu_mpdu",
	"tx_mu_successful_mpdu",
	"tx_su_successful_mpdu",
	"tx_msdu_pack_1",
	"tx_msdu_pack_2",
	"tx_msdu_pack_3",
	"tx_msdu_pack_4",
	"tx_msdu_pack_5",
	"tx_msdu_pack_6",
	"tx_msdu_pack_7",
	"tx_msdu_pack_8",

	/* rx counters */
	"rx_fifo_full_cnt",
	"rx_mpdu_cnt",
	"channel_idle_cnt",
	"rx_vector_mismatch_cnt",
	"rx_delimiter_fail_cnt",
	"rx_len_mismatch_cnt",
	"rx_ampdu_cnt",
	"rx_ampdu_bytes_cnt",
	"rx_ampdu_valid_subframe_cnt",
	"rx_ampdu_valid_subframe_b_cnt",
	"rx_pfdrop_cnt",
	"rx_vec_queue_overflow_drop_cnt",
	"rx_ba_cnt",

	/* per vif counters */
	"v_tx_mode_cck",
	"v_tx_mode_ofdm",
	"v_tx_mode_ht",
	"v_tx_mode_ht_gf",
	"v_tx_mode_vht",
	"v_tx_mode_he_su",
	"v_tx_mode_he_ext_su",
	"v_tx_mode_he_tb",
	"v_tx_mode_he_mu",
	"v_tx_mode_eht_su",
	"v_tx_mode_eht_trig",
	"v_tx_mode_eht_mu",
	"v_tx_bw_20",
	"v_tx_bw_40",
	"v_tx_bw_80",
	"v_tx_bw_160",
	"v_tx_bw_320",
	"v_tx_mcs_0",
	"v_tx_mcs_1",
	"v_tx_mcs_2",
	"v_tx_mcs_3",
	"v_tx_mcs_4",
	"v_tx_mcs_5",
	"v_tx_mcs_6",
	"v_tx_mcs_7",
	"v_tx_mcs_8",
	"v_tx_mcs_9",
	"v_tx_mcs_10",
	"v_tx_mcs_11",
	"v_tx_mcs_12",
	"v_tx_mcs_13",
	"v_tx_nss_1",
	"v_tx_nss_2",
	"v_tx_nss_3",
	"v_tx_nss_4",
};

#define MT7996_SSTATS_LEN ARRAY_SIZE(mt7996_gstrings_stats)

/* Ethtool related API */
static
void mt7996_get_et_strings(struct ieee80211_hw *hw,
			   struct ieee80211_vif *vif,
			   u32 sset, u8 *data)
{
	if (sset == ETH_SS_STATS)
		memcpy(data, mt7996_gstrings_stats,
		       sizeof(mt7996_gstrings_stats));
}

static
int mt7996_get_et_sset_count(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif, int sset)
{
	if (sset == ETH_SS_STATS)
		return MT7996_SSTATS_LEN;

	return 0;
}

static void mt7996_ethtool_worker(void *wi_data, struct ieee80211_sta *sta)
{
	struct mt76_ethtool_worker_info *wi = wi_data;
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt7996_sta_link *msta_link = &msta->deflink;

	if (msta->vif->deflink.mt76.idx != wi->idx)
		return;

	mt76_ethtool_worker(wi, &msta_link->wcid.stats, true);
}

static
void mt7996_get_et_stats(struct ieee80211_hw *hw,
			 struct ieee80211_vif *vif,
			 struct ethtool_stats *stats, u64 *data)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_phy *phy = mt7996_vif_link_phy(&mvif->deflink);
	struct mt76_mib_stats *mib = &phy->mib;
	struct mt76_ethtool_worker_info wi = {
		.data = data,
		.idx = mvif->deflink.mt76.idx,
	};
	/* See mt7996_ampdu_stat_read_phy, etc */
	int i, ei = 0;

	if (!phy)
		return;

	mutex_lock(&dev->mt76.mutex);

	mt7996_mac_update_stats(phy);

	data[ei++] = mib->tx_ampdu_cnt;
	data[ei++] = mib->tx_stop_q_empty_cnt;
	data[ei++] = mib->tx_mpdu_attempts_cnt;
	data[ei++] = mib->tx_mpdu_success_cnt;
	data[ei++] = mib->tx_rwp_fail_cnt;
	data[ei++] = mib->tx_rwp_need_cnt;
	data[ei++] = mib->tx_bf_ebf_ppdu_cnt;
	data[ei++] = mib->tx_bf_ibf_ppdu_cnt;

	/* Tx ampdu stat */
	for (i = 0; i < 15 /*ARRAY_SIZE(bound)*/; i++)
		data[ei++] = phy->mt76->aggr_stats[i];
	data[ei++] = phy->mib.ba_miss_cnt;

	/* Tx Beamformer monitor */
	data[ei++] = mib->tx_bf_ibf_ppdu_cnt;
	data[ei++] = mib->tx_bf_ebf_ppdu_cnt;

	/* Tx Beamformer Rx feedback monitor */
	data[ei++] = mib->tx_bf_rx_fb_all_cnt;
	data[ei++] = mib->tx_bf_rx_fb_he_cnt;
	data[ei++] = mib->tx_bf_rx_fb_vht_cnt;
	data[ei++] = mib->tx_bf_rx_fb_ht_cnt;

	data[ei++] = mib->tx_bf_rx_fb_bw;
	data[ei++] = mib->tx_bf_rx_fb_nc_cnt;
	data[ei++] = mib->tx_bf_rx_fb_nr_cnt;

	/* Tx Beamformee Rx NDPA & Tx feedback report */
	data[ei++] = mib->tx_bf_fb_cpl_cnt;
	data[ei++] = mib->tx_bf_fb_trig_cnt;

	/* Tx SU & MU counters */
	data[ei++] = mib->tx_mu_bf_cnt;
	data[ei++] = mib->tx_mu_mpdu_cnt;
	data[ei++] = mib->tx_mu_acked_mpdu_cnt;
	data[ei++] = mib->tx_su_acked_mpdu_cnt;

	/* Tx amsdu info (pack-count histogram) */
	for (i = 0; i < ARRAY_SIZE(mib->tx_amsdu); i++)
		data[ei++] = mib->tx_amsdu[i];

	/* rx counters */
	data[ei++] = mib->rx_fifo_full_cnt;
	data[ei++] = mib->rx_mpdu_cnt;
	data[ei++] = mib->channel_idle_cnt;
	data[ei++] = mib->rx_vector_mismatch_cnt;
	data[ei++] = mib->rx_delimiter_fail_cnt;
	data[ei++] = mib->rx_len_mismatch_cnt;
	data[ei++] = mib->rx_ampdu_cnt;
	data[ei++] = mib->rx_ampdu_bytes_cnt;
	data[ei++] = mib->rx_ampdu_valid_subframe_cnt;
	data[ei++] = mib->rx_ampdu_valid_subframe_bytes_cnt;
	data[ei++] = mib->rx_pfdrop_cnt;
	data[ei++] = mib->rx_vec_queue_overflow_drop_cnt;
	data[ei++] = mib->rx_ba_cnt;

	/* Add values for all stations owned by this vif */
	wi.initial_stat_idx = ei;
	ieee80211_iterate_stations_atomic(hw, mt7996_ethtool_worker, &wi);

	mutex_unlock(&dev->mt76.mutex);

	if (wi.sta_count == 0)
		return;

	ei += wi.worker_stat_count;
	if (ei != MT7996_SSTATS_LEN)
		dev_err(dev->mt76.dev, "ei: %d  MT7996_SSTATS_LEN: %d",
			ei, (int)MT7996_SSTATS_LEN);
}

static void
mt7996_twt_teardown_request(struct ieee80211_hw *hw,
			    struct ieee80211_sta *sta,
			    u8 flowid)
{
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt7996_sta_link *msta_link = &msta->deflink;
	struct mt7996_vif_link *link = &msta->vif->deflink;
	struct mt7996_dev *dev = mt7996_hw_dev(hw);

	mutex_lock(&dev->mt76.mutex);
	mt7996_mac_twt_teardown_flow(dev, link, msta_link, flowid);
	mutex_unlock(&dev->mt76.mutex);
}

static int
mt7996_set_radar_background(struct ieee80211_hw *hw,
			    struct cfg80211_chan_def *chandef)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_phy *phy;
	int ret = -EINVAL;
	bool running;

	if (chandef)
		phy = mt7996_band_phy(dev, chandef->chan->band);
	else
		phy = dev->rdd2_phy;
	if (!phy)
	    return -EINVAL;

	mutex_lock(&dev->mt76.mutex);

	if (dev->mt76.region == NL80211_DFS_UNSET)
		goto out;

	if (dev->rdd2_phy && dev->rdd2_phy != phy) {
		/* rdd2 is already locked */
		ret = -EBUSY;
		goto out;
	}

	/* rdd2 already configured on a radar channel */
	running = dev->rdd2_phy &&
		  cfg80211_chandef_valid(&dev->rdd2_chandef) &&
		  !!(dev->rdd2_chandef.chan->flags & IEEE80211_CHAN_RADAR);

	if (!chandef || running ||
	    !(chandef->chan->flags & IEEE80211_CHAN_RADAR)) {
		ret = mt7996_mcu_rdd_background_enable(phy, NULL);
		if (ret)
			goto out;

		if (!running)
			goto update_phy;
	}

	ret = mt7996_mcu_rdd_background_enable(phy, chandef);
	if (ret)
		goto out;

update_phy:
	dev->rdd2_phy = chandef ? phy : NULL;
	if (chandef)
		dev->rdd2_chandef = *chandef;
out:
	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

#ifdef CONFIG_NET_MEDIATEK_SOC_WED
static int
mt7996_net_fill_forward_path(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct ieee80211_sta *sta,
			     struct net_device_path_ctx *ctx,
			     struct net_device_path *path)
{
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mtk_wed_device *wed = &dev->mt76.mmio.wed;
	struct mt7996_sta_link *msta_link;
	struct mt7996_vif_link *link;
	struct mt76_vif_link *mlink;
	struct mt7996_phy *phy;

	mlink = rcu_dereference(mvif->mt76.link[msta->deflink_id]);
	if (!mlink)
		return -EIO;

	msta_link = rcu_dereference(msta->link[msta->deflink_id]);
	if (!msta_link)
		return -EIO;

	if (!msta_link->wcid.sta || msta_link->wcid.idx > MT7996_WTBL_STA)
		return -EIO;

	link = (struct mt7996_vif_link *)mlink;
	phy = mt7996_vif_link_phy(link);
	if (!phy)
		return -ENODEV;

	if (phy != &dev->phy && phy->mt76->band_idx == MT_BAND2)
		wed = &dev->mt76.mmio.wed_hif2;

	if (!mtk_wed_device_active(wed))
		return -ENODEV;

	path->type = DEV_PATH_MTK_WDMA;
	path->dev = ctx->dev;
	path->mtk_wdma.wdma_idx = wed->wdma_idx;
	path->mtk_wdma.bss = mlink->idx;
	path->mtk_wdma.queue = 0;
	path->mtk_wdma.wcid = msta_link->wcid.idx;

	path->mtk_wdma.amsdu = mtk_wed_is_amsdu_supported(wed);
	ctx->dev = NULL;

	return 0;
}

#endif

static int
mt7996_change_vif_links(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			u16 old_links, u16 new_links,
			struct ieee80211_bss_conf *old[IEEE80211_MLD_MAX_NUM_LINKS])
{
	return 0;
}

const struct ieee80211_ops mt7996_ops = {
	.add_chanctx = mt76_add_chanctx,
	.remove_chanctx = mt76_remove_chanctx,
	.change_chanctx = mt76_change_chanctx,
	.assign_vif_chanctx = mt76_assign_vif_chanctx,
	.unassign_vif_chanctx = mt76_unassign_vif_chanctx,
	.switch_vif_chanctx = mt76_switch_vif_chanctx,
	.tx = mt7996_tx,
	.start = mt7996_start,
	.stop = mt7996_stop,
	.add_interface = mt7996_add_interface,
	.remove_interface = mt7996_remove_interface,
	.config = mt7996_config,
	.conf_tx = mt7996_conf_tx,
	.configure_filter = mt7996_configure_filter,
	.vif_cfg_changed = mt7996_vif_cfg_changed,
	.link_info_changed = mt7996_link_info_changed,
	.sta_state = mt7996_sta_state,
	.sta_pre_rcu_remove = mt76_sta_pre_rcu_remove,
	.link_sta_rc_update = mt7996_link_sta_rc_update,
	.set_key = mt7996_set_key,
	.ampdu_action = mt7996_ampdu_action,
	.set_rts_threshold = mt7996_set_rts_threshold,
	.wake_tx_queue = mt76_wake_tx_queue,
	.hw_scan = mt76_hw_scan,
	.cancel_hw_scan = mt76_cancel_hw_scan,
	.remain_on_channel = mt76_remain_on_channel,
	.cancel_remain_on_channel = mt76_cancel_remain_on_channel,
	.release_buffered_frames = mt76_release_buffered_frames,
	.get_txpower = mt7996_get_txpower,
	.channel_switch_beacon = mt7996_channel_switch_beacon,
	.get_stats = mt7996_get_stats,
	.get_et_sset_count = mt7996_get_et_sset_count,
	.get_et_stats = mt7996_get_et_stats,
	.get_et_strings = mt7996_get_et_strings,
	.get_tsf = mt7996_get_tsf,
	.set_tsf = mt7996_set_tsf,
	.offset_tsf = mt7996_offset_tsf,
	.get_survey = mt76_get_survey,
	.get_antenna = mt76_get_antenna,
	.set_antenna = mt7996_set_antenna,
	.set_bitrate_mask = mt7996_set_bitrate_mask,
	.set_coverage_class = mt7996_set_coverage_class,
	.sta_statistics = mt7996_sta_statistics,
	.sta_set_4addr = mt7996_sta_set_4addr,
	.sta_set_decap_offload = mt7996_sta_set_decap_offload,
	.add_twt_setup = mt7996_mac_add_twt_setup,
	.twt_teardown_request = mt7996_twt_teardown_request,
#ifdef CONFIG_MAC80211_DEBUGFS
	.sta_add_debugfs = mt7996_sta_add_debugfs,
#endif
	.set_radar_background = mt7996_set_radar_background,
#ifdef CONFIG_NET_MEDIATEK_SOC_WED
	.net_fill_forward_path = mt7996_net_fill_forward_path,
	.net_setup_tc = mt76_wed_net_setup_tc,
#endif
	.change_vif_links = mt7996_change_vif_links,
	.change_sta_links = mt7996_mac_sta_change_links,
};
