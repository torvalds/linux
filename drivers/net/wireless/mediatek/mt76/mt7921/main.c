// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc. */

#include <linux/etherdevice.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/module.h>
#include "mt7921.h"
#include "mcu.h"

static void
mt7921_gen_ppe_thresh(u8 *he_ppet, int nss)
{
	u8 i, ppet_bits, ppet_size, ru_bit_mask = 0x7; /* HE80 */
	u8 ppet16_ppet8_ru3_ru0[] = {0x1c, 0xc7, 0x71};

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

static int
mt7921_init_he_caps(struct mt7921_phy *phy, enum nl80211_band band,
		    struct ieee80211_sband_iftype_data *data)
{
	int i, idx = 0;
	int nss = hweight8(phy->mt76->chainmask);
	u16 mcs_map = 0;

	for (i = 0; i < 8; i++) {
		if (i < nss)
			mcs_map |= (IEEE80211_HE_MCS_SUPPORT_0_11 << (i * 2));
		else
			mcs_map |= (IEEE80211_HE_MCS_NOT_SUPPORTED << (i * 2));
	}

	for (i = 0; i < NUM_NL80211_IFTYPES; i++) {
		struct ieee80211_sta_he_cap *he_cap = &data[idx].he_cap;
		struct ieee80211_he_cap_elem *he_cap_elem =
				&he_cap->he_cap_elem;
		struct ieee80211_he_mcs_nss_supp *he_mcs =
				&he_cap->he_mcs_nss_supp;

		switch (i) {
		case NL80211_IFTYPE_STATION:
			break;
		default:
			continue;
		}

		data[idx].types_mask = BIT(i);
		he_cap->has_he = true;

		he_cap_elem->mac_cap_info[0] =
			IEEE80211_HE_MAC_CAP0_HTC_HE;
		he_cap_elem->mac_cap_info[3] =
			IEEE80211_HE_MAC_CAP3_OMI_CONTROL |
			IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_3;
		he_cap_elem->mac_cap_info[4] =
			IEEE80211_HE_MAC_CAP4_AMSDU_IN_AMPDU;

		if (band == NL80211_BAND_2GHZ)
			he_cap_elem->phy_cap_info[0] =
				IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G;
		else if (band == NL80211_BAND_5GHZ)
			he_cap_elem->phy_cap_info[0] =
				IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G;

		he_cap_elem->phy_cap_info[1] =
			IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD;
		he_cap_elem->phy_cap_info[2] =
			IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US |
			IEEE80211_HE_PHY_CAP2_STBC_TX_UNDER_80MHZ |
			IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ |
			IEEE80211_HE_PHY_CAP2_UL_MU_FULL_MU_MIMO |
			IEEE80211_HE_PHY_CAP2_UL_MU_PARTIAL_MU_MIMO;

		switch (i) {
		case NL80211_IFTYPE_STATION:
			he_cap_elem->mac_cap_info[1] |=
				IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_16US;

			if (band == NL80211_BAND_2GHZ)
				he_cap_elem->phy_cap_info[0] |=
					IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_RU_MAPPING_IN_2G;
			else if (band == NL80211_BAND_5GHZ)
				he_cap_elem->phy_cap_info[0] |=
					IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_RU_MAPPING_IN_5G;

			he_cap_elem->phy_cap_info[1] |=
				IEEE80211_HE_PHY_CAP1_DEVICE_CLASS_A |
				IEEE80211_HE_PHY_CAP1_HE_LTF_AND_GI_FOR_HE_PPDUS_0_8US;
			he_cap_elem->phy_cap_info[3] |=
				IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_QPSK |
				IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_QPSK;
			he_cap_elem->phy_cap_info[4] |=
				IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE |
				IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_4;
			he_cap_elem->phy_cap_info[5] |=
				IEEE80211_HE_PHY_CAP5_NG16_SU_FEEDBACK |
				IEEE80211_HE_PHY_CAP5_NG16_MU_FEEDBACK;
			he_cap_elem->phy_cap_info[6] |=
				IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_42_SU |
				IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_75_MU |
				IEEE80211_HE_PHY_CAP6_TRIG_CQI_FB |
				IEEE80211_HE_PHY_CAP6_PARTIAL_BW_EXT_RANGE |
				IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT;
			he_cap_elem->phy_cap_info[7] |=
				IEEE80211_HE_PHY_CAP7_POWER_BOOST_FACTOR_SUPP |
				IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI;
			he_cap_elem->phy_cap_info[8] |=
				IEEE80211_HE_PHY_CAP8_20MHZ_IN_40MHZ_HE_PPDU_IN_2G |
				IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_484;
			he_cap_elem->phy_cap_info[9] |=
				IEEE80211_HE_PHY_CAP9_LONGER_THAN_16_SIGB_OFDM_SYM |
				IEEE80211_HE_PHY_CAP9_NON_TRIGGERED_CQI_FEEDBACK |
				IEEE80211_HE_PHY_CAP9_TX_1024_QAM_LESS_THAN_242_TONE_RU |
				IEEE80211_HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU |
				IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_COMP_SIGB |
				IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_NON_COMP_SIGB;
			break;
		}

		he_mcs->rx_mcs_80 = cpu_to_le16(mcs_map);
		he_mcs->tx_mcs_80 = cpu_to_le16(mcs_map);

		memset(he_cap->ppe_thres, 0, sizeof(he_cap->ppe_thres));
		if (he_cap_elem->phy_cap_info[6] &
		    IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT) {
			mt7921_gen_ppe_thresh(he_cap->ppe_thres, nss);
		} else {
			he_cap_elem->phy_cap_info[9] |=
				IEEE80211_HE_PHY_CAP9_NOMIMAL_PKT_PADDING_16US;
		}
		idx++;
	}

	return idx;
}

void mt7921_set_stream_he_caps(struct mt7921_phy *phy)
{
	struct ieee80211_sband_iftype_data *data;
	struct ieee80211_supported_band *band;
	int n;

	if (phy->mt76->cap.has_2ghz) {
		data = phy->iftype[NL80211_BAND_2GHZ];
		n = mt7921_init_he_caps(phy, NL80211_BAND_2GHZ, data);

		band = &phy->mt76->sband_2g.sband;
		band->iftype_data = data;
		band->n_iftype_data = n;
	}

	if (phy->mt76->cap.has_5ghz) {
		data = phy->iftype[NL80211_BAND_5GHZ];
		n = mt7921_init_he_caps(phy, NL80211_BAND_5GHZ, data);

		band = &phy->mt76->sband_5g.sband;
		band->iftype_data = data;
		band->n_iftype_data = n;
	}
}

int __mt7921_start(struct mt7921_phy *phy)
{
	struct mt76_phy *mphy = phy->mt76;
	int err;

	err = mt76_connac_mcu_set_mac_enable(mphy->dev, 0, true, false);
	if (err)
		return err;

	err = mt76_connac_mcu_set_channel_domain(mphy);
	if (err)
		return err;

	err = mt7921_mcu_set_chan_info(phy, MCU_EXT_CMD_SET_RX_PATH);
	if (err)
		return err;

	err = mt76_connac_mcu_set_rate_txpower(phy->mt76);
	if (err)
		return err;

	mt7921_mac_reset_counters(phy);
	set_bit(MT76_STATE_RUNNING, &mphy->state);

	ieee80211_queue_delayed_work(mphy->hw, &mphy->mac_work,
				     MT7921_WATCHDOG_TIME);

	return 0;
}

static int mt7921_start(struct ieee80211_hw *hw)
{
	struct mt7921_phy *phy = mt7921_hw_phy(hw);
	int err;

	mt7921_mutex_acquire(phy->dev);
	err = __mt7921_start(phy);
	mt7921_mutex_release(phy->dev);

	return err;
}

static void mt7921_stop(struct ieee80211_hw *hw)
{
	struct mt7921_dev *dev = mt7921_hw_dev(hw);
	struct mt7921_phy *phy = mt7921_hw_phy(hw);

	cancel_delayed_work_sync(&phy->mt76->mac_work);

	cancel_delayed_work_sync(&dev->pm.ps_work);
	cancel_work_sync(&dev->pm.wake_work);
	mt76_connac_free_pending_tx_skbs(&dev->pm, NULL);

	mt7921_mutex_acquire(dev);
	clear_bit(MT76_STATE_RUNNING, &phy->mt76->state);
	mt76_connac_mcu_set_mac_enable(&dev->mt76, 0, false, false);
	mt7921_mutex_release(dev);
}

static int mt7921_add_interface(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif)
{
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;
	struct mt7921_dev *dev = mt7921_hw_dev(hw);
	struct mt7921_phy *phy = mt7921_hw_phy(hw);
	struct mt76_txq *mtxq;
	int idx, ret = 0;

	mt7921_mutex_acquire(dev);

	if (vif->type == NL80211_IFTYPE_MONITOR &&
	    is_zero_ether_addr(vif->addr))
		phy->monitor_vif = vif;

	mvif->mt76.idx = ffs(~dev->mt76.vif_mask) - 1;
	if (mvif->mt76.idx >= MT7921_MAX_INTERFACES) {
		ret = -ENOSPC;
		goto out;
	}

	mvif->mt76.omac_idx = mvif->mt76.idx;
	mvif->phy = phy;
	mvif->mt76.band_idx = 0;
	mvif->mt76.wmm_idx = mvif->mt76.idx % MT7921_MAX_WMM_SETS;

	ret = mt76_connac_mcu_uni_add_dev(&dev->mphy, vif, &mvif->sta.wcid,
					  true);
	if (ret)
		goto out;

	dev->mt76.vif_mask |= BIT(mvif->mt76.idx);
	phy->omac_mask |= BIT_ULL(mvif->mt76.omac_idx);

	idx = MT7921_WTBL_RESERVED - mvif->mt76.idx;

	INIT_LIST_HEAD(&mvif->sta.stats_list);
	INIT_LIST_HEAD(&mvif->sta.poll_list);
	mvif->sta.wcid.idx = idx;
	mvif->sta.wcid.ext_phy = mvif->mt76.band_idx;
	mvif->sta.wcid.hw_key_idx = -1;
	mvif->sta.wcid.tx_info |= MT_WCID_TX_INFO_SET;
	mt7921_mac_wtbl_update(dev, idx,
			       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);

	ewma_rssi_init(&mvif->rssi);

	rcu_assign_pointer(dev->mt76.wcid[idx], &mvif->sta.wcid);
	if (vif->txq) {
		mtxq = (struct mt76_txq *)vif->txq->drv_priv;
		mtxq->wcid = &mvif->sta.wcid;
	}

	if (vif->type != NL80211_IFTYPE_AP &&
	    (!mvif->mt76.omac_idx || mvif->mt76.omac_idx > 3))
		vif->offload_flags = 0;

	vif->offload_flags |= IEEE80211_OFFLOAD_ENCAP_4ADDR;

out:
	mt7921_mutex_release(dev);

	return ret;
}

static void mt7921_remove_interface(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif)
{
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;
	struct mt7921_sta *msta = &mvif->sta;
	struct mt7921_dev *dev = mt7921_hw_dev(hw);
	struct mt7921_phy *phy = mt7921_hw_phy(hw);
	int idx = msta->wcid.idx;

	if (vif == phy->monitor_vif)
		phy->monitor_vif = NULL;

	mt7921_mutex_acquire(dev);
	mt76_connac_free_pending_tx_skbs(&dev->pm, &msta->wcid);
	mt76_connac_mcu_uni_add_dev(&dev->mphy, vif, &mvif->sta.wcid, false);

	rcu_assign_pointer(dev->mt76.wcid[idx], NULL);

	dev->mt76.vif_mask &= ~BIT(mvif->mt76.idx);
	phy->omac_mask &= ~BIT_ULL(mvif->mt76.omac_idx);
	mt7921_mutex_release(dev);

	spin_lock_bh(&dev->sta_poll_lock);
	if (!list_empty(&msta->poll_list))
		list_del_init(&msta->poll_list);
	spin_unlock_bh(&dev->sta_poll_lock);
}

static int mt7921_set_channel(struct mt7921_phy *phy)
{
	struct mt7921_dev *dev = phy->dev;
	int ret;

	cancel_delayed_work_sync(&phy->mt76->mac_work);

	mt7921_mutex_acquire(dev);
	set_bit(MT76_RESET, &phy->mt76->state);

	mt76_set_channel(phy->mt76);

	ret = mt7921_mcu_set_chan_info(phy, MCU_EXT_CMD_CHANNEL_SWITCH);
	if (ret)
		goto out;

	mt7921_mac_set_timing(phy);

	mt7921_mac_reset_counters(phy);
	phy->noise = 0;

out:
	clear_bit(MT76_RESET, &phy->mt76->state);
	mt7921_mutex_release(dev);

	mt76_worker_schedule(&dev->mt76.tx_worker);
	ieee80211_queue_delayed_work(phy->mt76->hw, &phy->mt76->mac_work,
				     MT7921_WATCHDOG_TIME);

	return ret;
}

static int mt7921_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			  struct ieee80211_vif *vif, struct ieee80211_sta *sta,
			  struct ieee80211_key_conf *key)
{
	struct mt7921_dev *dev = mt7921_hw_dev(hw);
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;
	struct mt7921_sta *msta = sta ? (struct mt7921_sta *)sta->drv_priv :
				  &mvif->sta;
	struct mt76_wcid *wcid = &msta->wcid;
	u8 *wcid_keyidx = &wcid->hw_key_idx;
	int idx = key->keyidx, err = 0;

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
	case WLAN_CIPHER_SUITE_AES_CMAC:
		key->flags |= IEEE80211_KEY_FLAG_GENERATE_MMIE;
		wcid_keyidx = &wcid->hw_key_idx2;
		break;
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		if (!mvif->wep_sta)
			return -EOPNOTSUPP;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_CCMP_256:
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
	case WLAN_CIPHER_SUITE_SMS4:
		break;
	default:
		return -EOPNOTSUPP;
	}

	mt7921_mutex_acquire(dev);

	if (cmd == SET_KEY)
		*wcid_keyidx = idx;
	else if (idx == *wcid_keyidx)
		*wcid_keyidx = -1;
	else
		goto out;

	mt76_wcid_key_setup(&dev->mt76, wcid,
			    cmd == SET_KEY ? key : NULL);

	err = mt7921_mcu_add_key(dev, vif, msta, key, cmd);
	if (err)
		goto out;

	if (key->cipher == WLAN_CIPHER_SUITE_WEP104 ||
	    key->cipher == WLAN_CIPHER_SUITE_WEP40)
		err = mt7921_mcu_add_key(dev, vif, mvif->wep_sta, key, cmd);
out:
	mt7921_mutex_release(dev);

	return err;
}

static int mt7921_config(struct ieee80211_hw *hw, u32 changed)
{
	struct mt7921_dev *dev = mt7921_hw_dev(hw);
	struct mt7921_phy *phy = mt7921_hw_phy(hw);
	int ret;

	if (changed & IEEE80211_CONF_CHANGE_CHANNEL) {
		ieee80211_stop_queues(hw);
		ret = mt7921_set_channel(phy);
		if (ret)
			return ret;
		ieee80211_wake_queues(hw);
	}

	mt7921_mutex_acquire(dev);

	if (changed & IEEE80211_CONF_CHANGE_POWER)
		mt76_connac_mcu_set_rate_txpower(phy->mt76);

	if (changed & IEEE80211_CONF_CHANGE_MONITOR) {
		bool enabled = !!(hw->conf.flags & IEEE80211_CONF_MONITOR);

		if (!enabled)
			phy->rxfilter |= MT_WF_RFCR_DROP_OTHER_UC;
		else
			phy->rxfilter &= ~MT_WF_RFCR_DROP_OTHER_UC;

		mt76_rmw_field(dev, MT_DMA_DCR0(0), MT_DMA_DCR0_RXD_G5_EN,
			       enabled);
		mt76_wr(dev, MT_WF_RFCR(0), phy->rxfilter);
	}

	mt7921_mutex_release(dev);

	return 0;
}

static int
mt7921_conf_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif, u16 queue,
	       const struct ieee80211_tx_queue_params *params)
{
	struct mt7921_dev *dev = mt7921_hw_dev(hw);
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;

	/* no need to update right away, we'll get BSS_CHANGED_QOS */
	queue = mt7921_lmac_mapping(dev, queue);
	mvif->queue_params[queue] = *params;

	return 0;
}

static void mt7921_configure_filter(struct ieee80211_hw *hw,
				    unsigned int changed_flags,
				    unsigned int *total_flags,
				    u64 multicast)
{
	struct mt7921_dev *dev = mt7921_hw_dev(hw);
	struct mt7921_phy *phy = mt7921_hw_phy(hw);
	u32 ctl_flags = MT_WF_RFCR1_DROP_ACK |
			MT_WF_RFCR1_DROP_BF_POLL |
			MT_WF_RFCR1_DROP_BA |
			MT_WF_RFCR1_DROP_CFEND |
			MT_WF_RFCR1_DROP_CFACK;
	u32 flags = 0;

#define MT76_FILTER(_flag, _hw) do {					\
		flags |= *total_flags & FIF_##_flag;			\
		phy->rxfilter &= ~(_hw);				\
		phy->rxfilter |= !(flags & FIF_##_flag) * (_hw);	\
	} while (0)

	mt7921_mutex_acquire(dev);

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
			   MT_WF_RFCR_DROP_STBC_MULTI);

	MT76_FILTER(OTHER_BSS, MT_WF_RFCR_DROP_OTHER_TIM |
			       MT_WF_RFCR_DROP_A3_MAC |
			       MT_WF_RFCR_DROP_A3_BSSID);

	MT76_FILTER(FCSFAIL, MT_WF_RFCR_DROP_FCSFAIL);

	MT76_FILTER(CONTROL, MT_WF_RFCR_DROP_CTS |
			     MT_WF_RFCR_DROP_RTS |
			     MT_WF_RFCR_DROP_CTL_RSV |
			     MT_WF_RFCR_DROP_NDPA);

	*total_flags = flags;
	mt76_wr(dev, MT_WF_RFCR(0), phy->rxfilter);

	if (*total_flags & FIF_CONTROL)
		mt76_clear(dev, MT_WF_RFCR1(0), ctl_flags);
	else
		mt76_set(dev, MT_WF_RFCR1(0), ctl_flags);

	mt7921_mutex_release(dev);
}

static int
mt7921_bss_bcnft_apply(struct mt7921_dev *dev, struct ieee80211_vif *vif,
		       bool assoc)
{
	int ret;

	if (!dev->pm.enable)
		return 0;

	if (assoc) {
		ret = mt7921_mcu_uni_bss_bcnft(dev, vif, true);
		if (ret)
			return ret;

		vif->driver_flags |= IEEE80211_VIF_BEACON_FILTER;
		mt76_set(dev, MT_WF_RFCR(0), MT_WF_RFCR_DROP_OTHER_BEACON);

		return 0;
	}

	ret = mt7921_mcu_set_bss_pm(dev, vif, false);
	if (ret)
		return ret;

	vif->driver_flags &= ~IEEE80211_VIF_BEACON_FILTER;
	mt76_clear(dev, MT_WF_RFCR(0), MT_WF_RFCR_DROP_OTHER_BEACON);

	return 0;
}

static void mt7921_bss_info_changed(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    struct ieee80211_bss_conf *info,
				    u32 changed)
{
	struct mt7921_phy *phy = mt7921_hw_phy(hw);
	struct mt7921_dev *dev = mt7921_hw_dev(hw);

	mt7921_mutex_acquire(dev);

	if (changed & BSS_CHANGED_ERP_SLOT) {
		int slottime = info->use_short_slot ? 9 : 20;

		if (slottime != phy->slottime) {
			phy->slottime = slottime;
			mt7921_mac_set_timing(phy);
		}
	}

	/* ensure that enable txcmd_mode after bss_info */
	if (changed & (BSS_CHANGED_QOS | BSS_CHANGED_BEACON_ENABLED))
		mt7921_mcu_set_tx(dev, vif);

	if (changed & BSS_CHANGED_PS)
		mt7921_mcu_uni_bss_ps(dev, vif);

	if (changed & BSS_CHANGED_ASSOC) {
		mt7921_mcu_sta_update(dev, NULL, vif, true,
				      MT76_STA_INFO_STATE_ASSOC);
		mt7921_bss_bcnft_apply(dev, vif, info->assoc);
	}

	if (changed & BSS_CHANGED_ARP_FILTER) {
		struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;

		mt76_connac_mcu_update_arp_filter(&dev->mt76, &mvif->mt76,
						  info);
	}

	mt7921_mutex_release(dev);
}

int mt7921_mac_sta_add(struct mt76_dev *mdev, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta)
{
	struct mt7921_dev *dev = container_of(mdev, struct mt7921_dev, mt76);
	struct mt7921_sta *msta = (struct mt7921_sta *)sta->drv_priv;
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;
	int ret, idx;

	idx = mt76_wcid_alloc(dev->mt76.wcid_mask, MT7921_WTBL_STA - 1);
	if (idx < 0)
		return -ENOSPC;

	INIT_LIST_HEAD(&msta->stats_list);
	INIT_LIST_HEAD(&msta->poll_list);
	msta->vif = mvif;
	msta->wcid.sta = 1;
	msta->wcid.idx = idx;
	msta->wcid.ext_phy = mvif->mt76.band_idx;
	msta->wcid.tx_info |= MT_WCID_TX_INFO_SET;
	msta->stats.jiffies = jiffies;

	ret = mt76_connac_pm_wake(&dev->mphy, &dev->pm);
	if (ret)
		return ret;

	if (vif->type == NL80211_IFTYPE_STATION)
		mvif->wep_sta = msta;

	mt7921_mac_wtbl_update(dev, idx,
			       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);

	ret = mt7921_mcu_sta_update(dev, sta, vif, true,
				    MT76_STA_INFO_STATE_NONE);
	if (ret)
		return ret;

	mt76_connac_power_save_sched(&dev->mphy, &dev->pm);

	return 0;
}

void mt7921_mac_sta_assoc(struct mt76_dev *mdev, struct ieee80211_vif *vif,
			  struct ieee80211_sta *sta)
{
	struct mt7921_dev *dev = container_of(mdev, struct mt7921_dev, mt76);
	struct mt7921_sta *msta = (struct mt7921_sta *)sta->drv_priv;
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;

	mt7921_mutex_acquire(dev);

	if (vif->type == NL80211_IFTYPE_STATION && !sta->tdls)
		mt76_connac_mcu_uni_add_bss(&dev->mphy, vif, &mvif->sta.wcid,
					    true);

	mt7921_mac_wtbl_update(dev, msta->wcid.idx,
			       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);

	mt7921_mcu_sta_update(dev, sta, vif, true, MT76_STA_INFO_STATE_ASSOC);

	mt7921_mutex_release(dev);
}

void mt7921_mac_sta_remove(struct mt76_dev *mdev, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta)
{
	struct mt7921_dev *dev = container_of(mdev, struct mt7921_dev, mt76);
	struct mt7921_sta *msta = (struct mt7921_sta *)sta->drv_priv;

	mt76_connac_free_pending_tx_skbs(&dev->pm, &msta->wcid);
	mt76_connac_pm_wake(&dev->mphy, &dev->pm);

	mt7921_mcu_sta_update(dev, sta, vif, false, MT76_STA_INFO_STATE_NONE);
	mt7921_mac_wtbl_update(dev, msta->wcid.idx,
			       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);

	if (vif->type == NL80211_IFTYPE_STATION) {
		struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;

		mvif->wep_sta = NULL;
		ewma_rssi_init(&mvif->rssi);
		if (!sta->tdls)
			mt76_connac_mcu_uni_add_bss(&dev->mphy, vif,
						    &mvif->sta.wcid, false);
	}

	spin_lock_bh(&dev->sta_poll_lock);
	if (!list_empty(&msta->poll_list))
		list_del_init(&msta->poll_list);
	if (!list_empty(&msta->stats_list))
		list_del_init(&msta->stats_list);
	spin_unlock_bh(&dev->sta_poll_lock);

	mt76_connac_power_save_sched(&dev->mphy, &dev->pm);
}

void mt7921_tx_worker(struct mt76_worker *w)
{
	struct mt7921_dev *dev = container_of(w, struct mt7921_dev,
					      mt76.tx_worker);

	if (!mt76_connac_pm_ref(&dev->mphy, &dev->pm)) {
		queue_work(dev->mt76.wq, &dev->pm.wake_work);
		return;
	}

	mt76_txq_schedule_all(&dev->mphy);
	mt76_connac_pm_unref(&dev->mphy, &dev->pm);
}

static void mt7921_tx(struct ieee80211_hw *hw,
		      struct ieee80211_tx_control *control,
		      struct sk_buff *skb)
{
	struct mt7921_dev *dev = mt7921_hw_dev(hw);
	struct mt76_phy *mphy = hw->priv;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_vif *vif = info->control.vif;
	struct mt76_wcid *wcid = &dev->mt76.global_wcid;
	int qid;

	if (control->sta) {
		struct mt7921_sta *sta;

		sta = (struct mt7921_sta *)control->sta->drv_priv;
		wcid = &sta->wcid;
	}

	if (vif && !control->sta) {
		struct mt7921_vif *mvif;

		mvif = (struct mt7921_vif *)vif->drv_priv;
		wcid = &mvif->sta.wcid;
	}

	if (mt76_connac_pm_ref(mphy, &dev->pm)) {
		mt76_tx(mphy, control->sta, wcid, skb);
		mt76_connac_pm_unref(mphy, &dev->pm);
		return;
	}

	qid = skb_get_queue_mapping(skb);
	if (qid >= MT_TXQ_PSD) {
		qid = IEEE80211_AC_BE;
		skb_set_queue_mapping(skb, qid);
	}

	mt76_connac_pm_queue_skb(hw, &dev->pm, wcid, skb);
}

static int mt7921_set_rts_threshold(struct ieee80211_hw *hw, u32 val)
{
	struct mt7921_dev *dev = mt7921_hw_dev(hw);

	mt7921_mutex_acquire(dev);
	mt76_connac_mcu_set_rts_thresh(&dev->mt76, val, 0);
	mt7921_mutex_release(dev);

	return 0;
}

static int
mt7921_ampdu_action(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		    struct ieee80211_ampdu_params *params)
{
	enum ieee80211_ampdu_mlme_action action = params->action;
	struct mt7921_dev *dev = mt7921_hw_dev(hw);
	struct ieee80211_sta *sta = params->sta;
	struct ieee80211_txq *txq = sta->txq[params->tid];
	struct mt7921_sta *msta = (struct mt7921_sta *)sta->drv_priv;
	u16 tid = params->tid;
	u16 ssn = params->ssn;
	struct mt76_txq *mtxq;
	int ret = 0;

	if (!txq)
		return -EINVAL;

	mtxq = (struct mt76_txq *)txq->drv_priv;

	mt7921_mutex_acquire(dev);
	switch (action) {
	case IEEE80211_AMPDU_RX_START:
		mt76_rx_aggr_start(&dev->mt76, &msta->wcid, tid, ssn,
				   params->buf_size);
		mt7921_mcu_uni_rx_ba(dev, params, true);
		break;
	case IEEE80211_AMPDU_RX_STOP:
		mt76_rx_aggr_stop(&dev->mt76, &msta->wcid, tid);
		mt7921_mcu_uni_rx_ba(dev, params, false);
		break;
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		mtxq->aggr = true;
		mtxq->send_bar = false;
		mt7921_mcu_uni_tx_ba(dev, params, true);
		break;
	case IEEE80211_AMPDU_TX_STOP_FLUSH:
	case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
		mtxq->aggr = false;
		clear_bit(tid, &msta->ampdu_state);
		mt7921_mcu_uni_tx_ba(dev, params, false);
		break;
	case IEEE80211_AMPDU_TX_START:
		set_bit(tid, &msta->ampdu_state);
		ret = IEEE80211_AMPDU_TX_START_IMMEDIATE;
		break;
	case IEEE80211_AMPDU_TX_STOP_CONT:
		mtxq->aggr = false;
		clear_bit(tid, &msta->ampdu_state);
		mt7921_mcu_uni_tx_ba(dev, params, false);
		ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		break;
	}
	mt7921_mutex_release(dev);

	return ret;
}

static int mt7921_sta_state(struct ieee80211_hw *hw,
			    struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta,
			    enum ieee80211_sta_state old_state,
			    enum ieee80211_sta_state new_state)
{
	struct mt7921_dev *dev = mt7921_hw_dev(hw);

	if (dev->pm.ds_enable) {
		mt7921_mutex_acquire(dev);
		mt76_connac_sta_state_dp(&dev->mt76, old_state, new_state);
		mt7921_mutex_release(dev);
	}

	return mt76_sta_state(hw, vif, sta, old_state, new_state);
}

static int
mt7921_get_stats(struct ieee80211_hw *hw,
		 struct ieee80211_low_level_stats *stats)
{
	struct mt7921_phy *phy = mt7921_hw_phy(hw);
	struct mib_stats *mib = &phy->mib;

	mt7921_mutex_acquire(phy->dev);

	stats->dot11RTSSuccessCount = mib->rts_cnt;
	stats->dot11RTSFailureCount = mib->rts_retries_cnt;
	stats->dot11FCSErrorCount = mib->fcs_err_cnt;
	stats->dot11ACKFailureCount = mib->ack_fail_cnt;

	memset(mib, 0, sizeof(*mib));

	mt7921_mutex_release(phy->dev);

	return 0;
}

static u64
mt7921_get_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;
	struct mt7921_dev *dev = mt7921_hw_dev(hw);
	u8 omac_idx = mvif->mt76.omac_idx;
	union {
		u64 t64;
		u32 t32[2];
	} tsf;
	u16 n;

	mt7921_mutex_acquire(dev);

	n = omac_idx > HW_BSSID_MAX ? HW_BSSID_0 : omac_idx;
	/* TSF software read */
	mt76_set(dev, MT_LPON_TCR(0, n), MT_LPON_TCR_SW_MODE);
	tsf.t32[0] = mt76_rr(dev, MT_LPON_UTTR0(0));
	tsf.t32[1] = mt76_rr(dev, MT_LPON_UTTR1(0));

	mt7921_mutex_release(dev);

	return tsf.t64;
}

static void
mt7921_set_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	       u64 timestamp)
{
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;
	struct mt7921_dev *dev = mt7921_hw_dev(hw);
	u8 omac_idx = mvif->mt76.omac_idx;
	union {
		u64 t64;
		u32 t32[2];
	} tsf = { .t64 = timestamp, };
	u16 n;

	mt7921_mutex_acquire(dev);

	n = omac_idx > HW_BSSID_MAX ? HW_BSSID_0 : omac_idx;
	mt76_wr(dev, MT_LPON_UTTR0(0), tsf.t32[0]);
	mt76_wr(dev, MT_LPON_UTTR1(0), tsf.t32[1]);
	/* TSF software overwrite */
	mt76_set(dev, MT_LPON_TCR(0, n), MT_LPON_TCR_SW_WRITE);

	mt7921_mutex_release(dev);
}

static void
mt7921_set_coverage_class(struct ieee80211_hw *hw, s16 coverage_class)
{
	struct mt7921_phy *phy = mt7921_hw_phy(hw);
	struct mt7921_dev *dev = phy->dev;

	mt7921_mutex_acquire(dev);
	phy->coverage_class = max_t(s16, coverage_class, 0);
	mt7921_mac_set_timing(phy);
	mt7921_mutex_release(dev);
}

void mt7921_scan_work(struct work_struct *work)
{
	struct mt7921_phy *phy;

	phy = (struct mt7921_phy *)container_of(work, struct mt7921_phy,
						scan_work.work);

	while (true) {
		struct mt7921_mcu_rxd *rxd;
		struct sk_buff *skb;

		spin_lock_bh(&phy->dev->mt76.lock);
		skb = __skb_dequeue(&phy->scan_event_list);
		spin_unlock_bh(&phy->dev->mt76.lock);

		if (!skb)
			break;

		rxd = (struct mt7921_mcu_rxd *)skb->data;
		if (rxd->eid == MCU_EVENT_SCHED_SCAN_DONE) {
			ieee80211_sched_scan_results(phy->mt76->hw);
		} else if (test_and_clear_bit(MT76_HW_SCANNING,
					      &phy->mt76->state)) {
			struct cfg80211_scan_info info = {
				.aborted = false,
			};

			ieee80211_scan_completed(phy->mt76->hw, &info);
		}
		dev_kfree_skb(skb);
	}
}

static int
mt7921_hw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	       struct ieee80211_scan_request *req)
{
	struct mt7921_dev *dev = mt7921_hw_dev(hw);
	struct mt76_phy *mphy = hw->priv;
	int err;

	mt7921_mutex_acquire(dev);
	err = mt76_connac_mcu_hw_scan(mphy, vif, req);
	mt7921_mutex_release(dev);

	return err;
}

static void
mt7921_cancel_hw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct mt7921_dev *dev = mt7921_hw_dev(hw);
	struct mt76_phy *mphy = hw->priv;

	mt7921_mutex_acquire(dev);
	mt76_connac_mcu_cancel_hw_scan(mphy, vif);
	mt7921_mutex_release(dev);
}

static int
mt7921_start_sched_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			struct cfg80211_sched_scan_request *req,
			struct ieee80211_scan_ies *ies)
{
	struct mt7921_dev *dev = mt7921_hw_dev(hw);
	struct mt76_phy *mphy = hw->priv;
	int err;

	mt7921_mutex_acquire(dev);

	err = mt76_connac_mcu_sched_scan_req(mphy, vif, req);
	if (err < 0)
		goto out;

	err = mt76_connac_mcu_sched_scan_enable(mphy, vif, true);
out:
	mt7921_mutex_release(dev);

	return err;
}

static int
mt7921_stop_sched_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct mt7921_dev *dev = mt7921_hw_dev(hw);
	struct mt76_phy *mphy = hw->priv;
	int err;

	mt7921_mutex_acquire(dev);
	err = mt76_connac_mcu_sched_scan_enable(mphy, vif, false);
	mt7921_mutex_release(dev);

	return err;
}

static int
mt7921_set_antenna(struct ieee80211_hw *hw, u32 tx_ant, u32 rx_ant)
{
	struct mt7921_dev *dev = mt7921_hw_dev(hw);
	struct mt7921_phy *phy = mt7921_hw_phy(hw);
	int max_nss = hweight8(hw->wiphy->available_antennas_tx);

	if (!tx_ant || tx_ant != rx_ant || ffs(tx_ant) > max_nss)
		return -EINVAL;

	if ((BIT(hweight8(tx_ant)) - 1) != tx_ant)
		tx_ant = BIT(ffs(tx_ant) - 1) - 1;

	mt7921_mutex_acquire(dev);

	phy->mt76->antenna_mask = tx_ant;
	phy->mt76->chainmask = tx_ant;

	mt76_set_stream_caps(phy->mt76, true);
	mt7921_set_stream_he_caps(phy);

	mt7921_mutex_release(dev);

	return 0;
}

static void mt7921_sta_statistics(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct ieee80211_sta *sta,
				  struct station_info *sinfo)
{
	struct mt7921_sta *msta = (struct mt7921_sta *)sta->drv_priv;
	struct mt7921_sta_stats *stats = &msta->stats;

	if (!stats->tx_rate.legacy && !stats->tx_rate.flags)
		return;

	if (stats->tx_rate.legacy) {
		sinfo->txrate.legacy = stats->tx_rate.legacy;
	} else {
		sinfo->txrate.mcs = stats->tx_rate.mcs;
		sinfo->txrate.nss = stats->tx_rate.nss;
		sinfo->txrate.bw = stats->tx_rate.bw;
		sinfo->txrate.he_gi = stats->tx_rate.he_gi;
		sinfo->txrate.he_dcm = stats->tx_rate.he_dcm;
		sinfo->txrate.he_ru_alloc = stats->tx_rate.he_ru_alloc;
	}
	sinfo->txrate.flags = stats->tx_rate.flags;
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_BITRATE);
}

#ifdef CONFIG_PM
static int mt7921_suspend(struct ieee80211_hw *hw,
			  struct cfg80211_wowlan *wowlan)
{
	struct mt7921_dev *dev = mt7921_hw_dev(hw);
	struct mt7921_phy *phy = mt7921_hw_phy(hw);
	int err;

	cancel_delayed_work_sync(&phy->scan_work);
	cancel_delayed_work_sync(&phy->mt76->mac_work);

	cancel_delayed_work_sync(&dev->pm.ps_work);
	mt76_connac_free_pending_tx_skbs(&dev->pm, NULL);

	mt7921_mutex_acquire(dev);

	clear_bit(MT76_STATE_RUNNING, &phy->mt76->state);

	set_bit(MT76_STATE_SUSPEND, &phy->mt76->state);
	ieee80211_iterate_active_interfaces(hw,
					    IEEE80211_IFACE_ITER_RESUME_ALL,
					    mt76_connac_mcu_set_suspend_iter,
					    &dev->mphy);

	err = mt76_connac_mcu_set_hif_suspend(&dev->mt76, true);

	mt7921_mutex_release(dev);

	return err;
}

static int mt7921_resume(struct ieee80211_hw *hw)
{
	struct mt7921_dev *dev = mt7921_hw_dev(hw);
	struct mt7921_phy *phy = mt7921_hw_phy(hw);
	int err;

	mt7921_mutex_acquire(dev);

	err = mt76_connac_mcu_set_hif_suspend(&dev->mt76, false);
	if (err < 0)
		goto out;

	set_bit(MT76_STATE_RUNNING, &phy->mt76->state);
	clear_bit(MT76_STATE_SUSPEND, &phy->mt76->state);
	ieee80211_iterate_active_interfaces(hw,
					    IEEE80211_IFACE_ITER_RESUME_ALL,
					    mt76_connac_mcu_set_suspend_iter,
					    &dev->mphy);

	ieee80211_queue_delayed_work(hw, &phy->mt76->mac_work,
				     MT7921_WATCHDOG_TIME);
out:

	mt7921_mutex_release(dev);

	return err;
}

static void mt7921_set_wakeup(struct ieee80211_hw *hw, bool enabled)
{
	struct mt7921_dev *dev = mt7921_hw_dev(hw);
	struct mt76_dev *mdev = &dev->mt76;

	device_set_wakeup_enable(mdev->dev, enabled);
}

static void mt7921_set_rekey_data(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct cfg80211_gtk_rekey_data *data)
{
	struct mt7921_dev *dev = mt7921_hw_dev(hw);

	mt7921_mutex_acquire(dev);
	mt76_connac_mcu_update_gtk_rekey(hw, vif, data);
	mt7921_mutex_release(dev);
}
#endif /* CONFIG_PM */

static void mt7921_flush(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			 u32 queues, bool drop)
{
	struct mt7921_dev *dev = mt7921_hw_dev(hw);

	wait_event_timeout(dev->mt76.tx_wait, !mt76_has_tx_pending(&dev->mphy),
			   HZ / 2);
}

static void mt7921_sta_set_decap_offload(struct ieee80211_hw *hw,
					 struct ieee80211_vif *vif,
					 struct ieee80211_sta *sta,
					 bool enabled)
{
	struct mt7921_sta *msta = (struct mt7921_sta *)sta->drv_priv;
	struct mt7921_dev *dev = mt7921_hw_dev(hw);

	if (enabled)
		set_bit(MT_WCID_FLAG_HDR_TRANS, &msta->wcid.flags);
	else
		clear_bit(MT_WCID_FLAG_HDR_TRANS, &msta->wcid.flags);

	mt76_connac_mcu_sta_update_hdr_trans(&dev->mt76, vif, &msta->wcid,
					     MCU_UNI_CMD_STA_REC_UPDATE);
}

const struct ieee80211_ops mt7921_ops = {
	.tx = mt7921_tx,
	.start = mt7921_start,
	.stop = mt7921_stop,
	.add_interface = mt7921_add_interface,
	.remove_interface = mt7921_remove_interface,
	.config = mt7921_config,
	.conf_tx = mt7921_conf_tx,
	.configure_filter = mt7921_configure_filter,
	.bss_info_changed = mt7921_bss_info_changed,
	.sta_state = mt7921_sta_state,
	.sta_pre_rcu_remove = mt76_sta_pre_rcu_remove,
	.set_key = mt7921_set_key,
	.sta_set_decap_offload = mt7921_sta_set_decap_offload,
	.ampdu_action = mt7921_ampdu_action,
	.set_rts_threshold = mt7921_set_rts_threshold,
	.wake_tx_queue = mt76_wake_tx_queue,
	.release_buffered_frames = mt76_release_buffered_frames,
	.get_txpower = mt76_get_txpower,
	.get_stats = mt7921_get_stats,
	.get_tsf = mt7921_get_tsf,
	.set_tsf = mt7921_set_tsf,
	.get_survey = mt76_get_survey,
	.get_antenna = mt76_get_antenna,
	.set_antenna = mt7921_set_antenna,
	.set_coverage_class = mt7921_set_coverage_class,
	.hw_scan = mt7921_hw_scan,
	.cancel_hw_scan = mt7921_cancel_hw_scan,
	.sta_statistics = mt7921_sta_statistics,
	.sched_scan_start = mt7921_start_sched_scan,
	.sched_scan_stop = mt7921_stop_sched_scan,
#ifdef CONFIG_PM
	.suspend = mt7921_suspend,
	.resume = mt7921_resume,
	.set_wakeup = mt7921_set_wakeup,
	.set_rekey_data = mt7921_set_rekey_data,
#endif /* CONFIG_PM */
	.flush = mt7921_flush,
};
