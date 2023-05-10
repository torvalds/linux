// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc. */

#include <linux/etherdevice.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <net/ipv6.h>
#include "mt7921.h"
#include "mcu.h"

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
		case NL80211_IFTYPE_AP:
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
		else
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
		case NL80211_IFTYPE_AP:
			he_cap_elem->mac_cap_info[2] |=
				IEEE80211_HE_MAC_CAP2_BSR;
			he_cap_elem->mac_cap_info[4] |=
				IEEE80211_HE_MAC_CAP4_BQR;
			he_cap_elem->mac_cap_info[5] |=
				IEEE80211_HE_MAC_CAP5_OM_CTRL_UL_MU_DATA_DIS_RX;
			he_cap_elem->phy_cap_info[3] |=
				IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_QPSK |
				IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_QPSK;
			he_cap_elem->phy_cap_info[6] |=
				IEEE80211_HE_PHY_CAP6_PARTIAL_BW_EXT_RANGE |
				IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT;
			he_cap_elem->phy_cap_info[9] |=
				IEEE80211_HE_PHY_CAP9_TX_1024_QAM_LESS_THAN_242_TONE_RU |
				IEEE80211_HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU;
			break;
		case NL80211_IFTYPE_STATION:
			he_cap_elem->mac_cap_info[1] |=
				IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_16US;

			if (band == NL80211_BAND_2GHZ)
				he_cap_elem->phy_cap_info[0] |=
					IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_RU_MAPPING_IN_2G;
			else
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

			if (is_mt7922(phy->mt76->dev)) {
				he_cap_elem->phy_cap_info[0] |=
					IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G;
				he_cap_elem->phy_cap_info[8] |=
					IEEE80211_HE_PHY_CAP8_20MHZ_IN_160MHZ_HE_PPDU |
					IEEE80211_HE_PHY_CAP8_80MHZ_IN_160MHZ_HE_PPDU;
			}
			break;
		}

		he_mcs->rx_mcs_80 = cpu_to_le16(mcs_map);
		he_mcs->tx_mcs_80 = cpu_to_le16(mcs_map);
		if (is_mt7922(phy->mt76->dev)) {
			he_mcs->rx_mcs_160 = cpu_to_le16(mcs_map);
			he_mcs->tx_mcs_160 = cpu_to_le16(mcs_map);
		}

		memset(he_cap->ppe_thres, 0, sizeof(he_cap->ppe_thres));
		if (he_cap_elem->phy_cap_info[6] &
		    IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT) {
			mt76_connac_gen_ppe_thresh(he_cap->ppe_thres, nss);
		} else {
			he_cap_elem->phy_cap_info[9] |=
				u8_encode_bits(IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_16US,
					       IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_MASK);
		}

		if (band == NL80211_BAND_6GHZ) {
			struct ieee80211_supported_band *sband =
				&phy->mt76->sband_5g.sband;
			struct ieee80211_sta_vht_cap *vht_cap = &sband->vht_cap;
			struct ieee80211_sta_ht_cap *ht_cap = &sband->ht_cap;
			u32 exp;
			u16 cap;

			cap = u16_encode_bits(ht_cap->ampdu_density,
					IEEE80211_HE_6GHZ_CAP_MIN_MPDU_START);
			exp = u32_get_bits(vht_cap->cap,
				IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK);
			cap |= u16_encode_bits(exp,
					IEEE80211_HE_6GHZ_CAP_MAX_AMPDU_LEN_EXP);
			exp = u32_get_bits(vht_cap->cap,
					   IEEE80211_VHT_CAP_MAX_MPDU_MASK);
			cap |= u16_encode_bits(exp,
					IEEE80211_HE_6GHZ_CAP_MAX_MPDU_LEN);
			if (vht_cap->cap & IEEE80211_VHT_CAP_TX_ANTENNA_PATTERN)
				cap |= IEEE80211_HE_6GHZ_CAP_TX_ANTPAT_CONS;
			if (vht_cap->cap & IEEE80211_VHT_CAP_RX_ANTENNA_PATTERN)
				cap |= IEEE80211_HE_6GHZ_CAP_RX_ANTPAT_CONS;

			data[idx].he_6ghz_capa.capa = cpu_to_le16(cap);
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

		if (phy->mt76->cap.has_6ghz) {
			data = phy->iftype[NL80211_BAND_6GHZ];
			n = mt7921_init_he_caps(phy, NL80211_BAND_6GHZ, data);

			band = &phy->mt76->sband_6g.sband;
			band->iftype_data = data;
			band->n_iftype_data = n;
		}
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

	err = mt7921_mcu_set_chan_info(phy, MCU_EXT_CMD(SET_RX_PATH));
	if (err)
		return err;

	err = mt7921_set_tx_sar_pwr(mphy->hw, NULL);
	if (err)
		return err;

	mt7921_mac_reset_counters(phy);
	set_bit(MT76_STATE_RUNNING, &mphy->state);

	ieee80211_queue_delayed_work(mphy->hw, &mphy->mac_work,
				     MT7921_WATCHDOG_TIME);

	return 0;
}
EXPORT_SYMBOL_GPL(__mt7921_start);

static int mt7921_start(struct ieee80211_hw *hw)
{
	struct mt7921_phy *phy = mt7921_hw_phy(hw);
	int err;

	mt7921_mutex_acquire(phy->dev);
	err = __mt7921_start(phy);
	mt7921_mutex_release(phy->dev);

	return err;
}

void mt7921_stop(struct ieee80211_hw *hw)
{
	struct mt7921_dev *dev = mt7921_hw_dev(hw);
	struct mt7921_phy *phy = mt7921_hw_phy(hw);

	cancel_delayed_work_sync(&phy->mt76->mac_work);

	cancel_delayed_work_sync(&dev->pm.ps_work);
	cancel_work_sync(&dev->pm.wake_work);
	cancel_work_sync(&dev->reset_work);
	mt76_connac_free_pending_tx_skbs(&dev->pm, NULL);

	mt7921_mutex_acquire(dev);
	clear_bit(MT76_STATE_RUNNING, &phy->mt76->state);
	mt76_connac_mcu_set_mac_enable(&dev->mt76, 0, false, false);
	mt7921_mutex_release(dev);
}
EXPORT_SYMBOL_GPL(mt7921_stop);

static int mt7921_add_interface(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif)
{
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;
	struct mt7921_dev *dev = mt7921_hw_dev(hw);
	struct mt7921_phy *phy = mt7921_hw_phy(hw);
	struct mt76_txq *mtxq;
	int idx, ret = 0;

	mt7921_mutex_acquire(dev);

	mvif->mt76.idx = __ffs64(~dev->mt76.vif_mask);
	if (mvif->mt76.idx >= MT7921_MAX_INTERFACES) {
		ret = -ENOSPC;
		goto out;
	}

	mvif->mt76.omac_idx = mvif->mt76.idx;
	mvif->phy = phy;
	mvif->mt76.band_idx = 0;
	mvif->mt76.wmm_idx = mvif->mt76.idx % MT76_CONNAC_MAX_WMM_SETS;

	ret = mt76_connac_mcu_uni_add_dev(&dev->mphy, vif, &mvif->sta.wcid,
					  true);
	if (ret)
		goto out;

	dev->mt76.vif_mask |= BIT_ULL(mvif->mt76.idx);
	phy->omac_mask |= BIT_ULL(mvif->mt76.omac_idx);

	idx = MT7921_WTBL_RESERVED - mvif->mt76.idx;

	INIT_LIST_HEAD(&mvif->sta.poll_list);
	mvif->sta.wcid.idx = idx;
	mvif->sta.wcid.phy_idx = mvif->mt76.band_idx;
	mvif->sta.wcid.hw_key_idx = -1;
	mvif->sta.wcid.tx_info |= MT_WCID_TX_INFO_SET;
	mt76_packet_id_init(&mvif->sta.wcid);

	mt7921_mac_wtbl_update(dev, idx,
			       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);

	ewma_rssi_init(&mvif->rssi);

	rcu_assign_pointer(dev->mt76.wcid[idx], &mvif->sta.wcid);
	if (vif->txq) {
		mtxq = (struct mt76_txq *)vif->txq->drv_priv;
		mtxq->wcid = idx;
	}

	vif->driver_flags |= IEEE80211_VIF_BEACON_FILTER;
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

	mt7921_mutex_acquire(dev);
	mt76_connac_free_pending_tx_skbs(&dev->pm, &msta->wcid);
	mt76_connac_mcu_uni_add_dev(&dev->mphy, vif, &mvif->sta.wcid, false);

	rcu_assign_pointer(dev->mt76.wcid[idx], NULL);

	dev->mt76.vif_mask &= ~BIT_ULL(mvif->mt76.idx);
	phy->omac_mask &= ~BIT_ULL(mvif->mt76.omac_idx);
	mt7921_mutex_release(dev);

	spin_lock_bh(&dev->sta_poll_lock);
	if (!list_empty(&msta->poll_list))
		list_del_init(&msta->poll_list);
	spin_unlock_bh(&dev->sta_poll_lock);

	mt76_packet_id_flush(&dev->mt76, &msta->wcid);
}

static void mt7921_roc_iter(void *priv, u8 *mac,
			    struct ieee80211_vif *vif)
{
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;
	struct mt7921_phy *phy = priv;

	mt7921_mcu_abort_roc(phy, mvif, phy->roc_token_id);
}

void mt7921_roc_work(struct work_struct *work)
{
	struct mt7921_phy *phy;

	phy = (struct mt7921_phy *)container_of(work, struct mt7921_phy,
						roc_work);

	if (!test_and_clear_bit(MT76_STATE_ROC, &phy->mt76->state))
		return;

	mt7921_mutex_acquire(phy->dev);
	ieee80211_iterate_active_interfaces(phy->mt76->hw,
					    IEEE80211_IFACE_ITER_RESUME_ALL,
					    mt7921_roc_iter, phy);
	mt7921_mutex_release(phy->dev);
	ieee80211_remain_on_channel_expired(phy->mt76->hw);
}

void mt7921_roc_timer(struct timer_list *timer)
{
	struct mt7921_phy *phy = from_timer(phy, timer, roc_timer);

	ieee80211_queue_work(phy->mt76->hw, &phy->roc_work);
}

static int mt7921_abort_roc(struct mt7921_phy *phy, struct mt7921_vif *vif)
{
	int err = 0;

	del_timer_sync(&phy->roc_timer);
	cancel_work_sync(&phy->roc_work);

	mt7921_mutex_acquire(phy->dev);
	if (test_and_clear_bit(MT76_STATE_ROC, &phy->mt76->state))
		err = mt7921_mcu_abort_roc(phy, vif, phy->roc_token_id);
	mt7921_mutex_release(phy->dev);

	return err;
}

static int mt7921_set_roc(struct mt7921_phy *phy,
			  struct mt7921_vif *vif,
			  struct ieee80211_channel *chan,
			  int duration,
			  enum mt7921_roc_req type)
{
	int err;

	if (test_and_set_bit(MT76_STATE_ROC, &phy->mt76->state))
		return -EBUSY;

	phy->roc_grant = false;

	err = mt7921_mcu_set_roc(phy, vif, chan, duration, type,
				 ++phy->roc_token_id);
	if (err < 0) {
		clear_bit(MT76_STATE_ROC, &phy->mt76->state);
		goto out;
	}

	if (!wait_event_timeout(phy->roc_wait, phy->roc_grant, HZ)) {
		mt7921_mcu_abort_roc(phy, vif, phy->roc_token_id);
		clear_bit(MT76_STATE_ROC, &phy->mt76->state);
		err = -ETIMEDOUT;
	}

out:
	return err;
}

static int mt7921_remain_on_channel(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    struct ieee80211_channel *chan,
				    int duration,
				    enum ieee80211_roc_type type)
{
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;
	struct mt7921_phy *phy = mt7921_hw_phy(hw);
	int err;

	mt7921_mutex_acquire(phy->dev);
	err = mt7921_set_roc(phy, mvif, chan, duration, MT7921_ROC_REQ_ROC);
	mt7921_mutex_release(phy->dev);

	return err;
}

static int mt7921_cancel_remain_on_channel(struct ieee80211_hw *hw,
					   struct ieee80211_vif *vif)
{
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;
	struct mt7921_phy *phy = mt7921_hw_phy(hw);

	return mt7921_abort_roc(phy, mvif);
}

static int mt7921_set_channel(struct mt7921_phy *phy)
{
	struct mt7921_dev *dev = phy->dev;
	int ret;

	cancel_delayed_work_sync(&phy->mt76->mac_work);

	mt7921_mutex_acquire(dev);
	set_bit(MT76_RESET, &phy->mt76->state);

	mt76_set_channel(phy->mt76);

	ret = mt7921_mcu_set_chan_info(phy, MCU_EXT_CMD(CHANNEL_SWITCH));
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

	if (cmd == SET_KEY) {
		*wcid_keyidx = idx;
	} else {
		if (idx == *wcid_keyidx)
			*wcid_keyidx = -1;
		goto out;
	}

	mt76_wcid_key_setup(&dev->mt76, wcid, key);
	err = mt76_connac_mcu_add_key(&dev->mt76, vif, &msta->bip,
				      key, MCU_UNI_CMD(STA_REC_UPDATE),
				      &msta->wcid, cmd);
	if (err)
		goto out;

	if (key->cipher == WLAN_CIPHER_SUITE_WEP104 ||
	    key->cipher == WLAN_CIPHER_SUITE_WEP40)
		err = mt76_connac_mcu_add_key(&dev->mt76, vif,
					      &mvif->wep_sta->bip,
					      key, MCU_UNI_CMD(STA_REC_UPDATE),
					      &mvif->wep_sta->wcid, cmd);
out:
	mt7921_mutex_release(dev);

	return err;
}

static void
mt7921_pm_interface_iter(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	struct mt7921_dev *dev = priv;
	struct ieee80211_hw *hw = mt76_hw(dev);
	bool pm_enable = dev->pm.enable;
	int err;

	err = mt7921_mcu_set_beacon_filter(dev, vif, pm_enable);
	if (err < 0)
		return;

	if (pm_enable) {
		vif->driver_flags |= IEEE80211_VIF_BEACON_FILTER;
		ieee80211_hw_set(hw, CONNECTION_MONITOR);
	} else {
		vif->driver_flags &= ~IEEE80211_VIF_BEACON_FILTER;
		__clear_bit(IEEE80211_HW_CONNECTION_MONITOR, hw->flags);
	}
}

static void
mt7921_sniffer_interface_iter(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	struct mt7921_dev *dev = priv;
	struct ieee80211_hw *hw = mt76_hw(dev);
	struct mt76_connac_pm *pm = &dev->pm;
	bool monitor = !!(hw->conf.flags & IEEE80211_CONF_MONITOR);

	mt7921_mcu_set_sniffer(dev, vif, monitor);
	pm->enable = pm->enable_user && !monitor;
	pm->ds_enable = pm->ds_enable_user && !monitor;

	mt76_connac_mcu_set_deep_sleep(&dev->mt76, pm->ds_enable);

	if (monitor)
		mt7921_mcu_set_beacon_filter(dev, vif, false);
}

void mt7921_set_runtime_pm(struct mt7921_dev *dev)
{
	struct ieee80211_hw *hw = mt76_hw(dev);
	struct mt76_connac_pm *pm = &dev->pm;
	bool monitor = !!(hw->conf.flags & IEEE80211_CONF_MONITOR);

	pm->enable = pm->enable_user && !monitor;
	ieee80211_iterate_active_interfaces(hw,
					    IEEE80211_IFACE_ITER_RESUME_ALL,
					    mt7921_pm_interface_iter, dev);
	pm->ds_enable = pm->ds_enable_user && !monitor;
	mt76_connac_mcu_set_deep_sleep(&dev->mt76, pm->ds_enable);
}

static int mt7921_config(struct ieee80211_hw *hw, u32 changed)
{
	struct mt7921_dev *dev = mt7921_hw_dev(hw);
	struct mt7921_phy *phy = mt7921_hw_phy(hw);
	int ret = 0;

	if (changed & IEEE80211_CONF_CHANGE_CHANNEL) {
		ieee80211_stop_queues(hw);
		ret = mt7921_set_channel(phy);
		if (ret)
			return ret;
		ieee80211_wake_queues(hw);
	}

	mt7921_mutex_acquire(dev);

	if (changed & IEEE80211_CONF_CHANGE_POWER) {
		ret = mt7921_set_tx_sar_pwr(hw, NULL);
		if (ret)
			goto out;
	}

	if (changed & IEEE80211_CONF_CHANGE_MONITOR) {
		ieee80211_iterate_active_interfaces(hw,
						    IEEE80211_IFACE_ITER_RESUME_ALL,
						    mt7921_sniffer_interface_iter, dev);
	}

out:
	mt7921_mutex_release(dev);

	return ret;
}

static int
mt7921_conf_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	       unsigned int link_id, u16 queue,
	       const struct ieee80211_tx_queue_params *params)
{
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;

	/* no need to update right away, we'll get BSS_CHANGED_QOS */
	queue = mt76_connac_lmac_mapping(queue);
	mvif->queue_params[queue] = *params;

	return 0;
}

static void mt7921_configure_filter(struct ieee80211_hw *hw,
				    unsigned int changed_flags,
				    unsigned int *total_flags,
				    u64 multicast)
{
#define MT7921_FILTER_FCSFAIL    BIT(2)
#define MT7921_FILTER_CONTROL    BIT(5)
#define MT7921_FILTER_OTHER_BSS  BIT(6)
#define MT7921_FILTER_ENABLE     BIT(31)

	struct mt7921_dev *dev = mt7921_hw_dev(hw);
	u32 flags = MT7921_FILTER_ENABLE;

#define MT7921_FILTER(_fif, _type) do {			\
		if (*total_flags & (_fif))		\
			flags |= MT7921_FILTER_##_type;	\
	} while (0)

	MT7921_FILTER(FIF_FCSFAIL, FCSFAIL);
	MT7921_FILTER(FIF_CONTROL, CONTROL);
	MT7921_FILTER(FIF_OTHER_BSS, OTHER_BSS);

	mt7921_mutex_acquire(dev);
	mt7921_mcu_set_rxfilter(dev, flags, 0, 0);
	mt7921_mutex_release(dev);

	*total_flags &= (FIF_OTHER_BSS | FIF_FCSFAIL | FIF_CONTROL);
}

static void mt7921_bss_info_changed(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    struct ieee80211_bss_conf *info,
				    u64 changed)
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

	if (changed & (BSS_CHANGED_BEACON |
		       BSS_CHANGED_BEACON_ENABLED))
		mt7921_mcu_uni_add_beacon_offload(dev, hw, vif,
						  info->enable_beacon);

	/* ensure that enable txcmd_mode after bss_info */
	if (changed & (BSS_CHANGED_QOS | BSS_CHANGED_BEACON_ENABLED))
		mt7921_mcu_set_tx(dev, vif);

	if (changed & BSS_CHANGED_PS)
		mt7921_mcu_uni_bss_ps(dev, vif);

	if (changed & BSS_CHANGED_ASSOC) {
		mt7921_mcu_sta_update(dev, NULL, vif, true,
				      MT76_STA_INFO_STATE_ASSOC);
		mt7921_mcu_set_beacon_filter(dev, vif, vif->cfg.assoc);
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

	INIT_LIST_HEAD(&msta->poll_list);
	msta->vif = mvif;
	msta->wcid.sta = 1;
	msta->wcid.idx = idx;
	msta->wcid.phy_idx = mvif->mt76.band_idx;
	msta->wcid.tx_info |= MT_WCID_TX_INFO_SET;
	msta->last_txs = jiffies;

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
EXPORT_SYMBOL_GPL(mt7921_mac_sta_add);

void mt7921_mac_sta_assoc(struct mt76_dev *mdev, struct ieee80211_vif *vif,
			  struct ieee80211_sta *sta)
{
	struct mt7921_dev *dev = container_of(mdev, struct mt7921_dev, mt76);
	struct mt7921_sta *msta = (struct mt7921_sta *)sta->drv_priv;
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;

	mt7921_mutex_acquire(dev);

	if (vif->type == NL80211_IFTYPE_STATION && !sta->tdls)
		mt76_connac_mcu_uni_add_bss(&dev->mphy, vif, &mvif->sta.wcid,
					    true, mvif->ctx);

	ewma_avg_signal_init(&msta->avg_ack_signal);

	mt7921_mac_wtbl_update(dev, msta->wcid.idx,
			       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);
	memset(msta->airtime_ac, 0, sizeof(msta->airtime_ac));

	mt7921_mcu_sta_update(dev, sta, vif, true, MT76_STA_INFO_STATE_ASSOC);

	mt7921_mutex_release(dev);
}
EXPORT_SYMBOL_GPL(mt7921_mac_sta_assoc);

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
						    &mvif->sta.wcid, false,
						    mvif->ctx);
	}

	spin_lock_bh(&dev->sta_poll_lock);
	if (!list_empty(&msta->poll_list))
		list_del_init(&msta->poll_list);
	spin_unlock_bh(&dev->sta_poll_lock);

	mt76_connac_power_save_sched(&dev->mphy, &dev->pm);
}
EXPORT_SYMBOL_GPL(mt7921_mac_sta_remove);

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

	mt7921_mutex_release(phy->dev);

	return 0;
}

static const char mt7921_gstrings_stats[][ETH_GSTRING_LEN] = {
	/* tx counters */
	"tx_ampdu_cnt",
	"tx_mpdu_attempts",
	"tx_mpdu_success",
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
	"tx_msdu_pack_1",
	"tx_msdu_pack_2",
	"tx_msdu_pack_3",
	"tx_msdu_pack_4",
	"tx_msdu_pack_5",
	"tx_msdu_pack_6",
	"tx_msdu_pack_7",
	"tx_msdu_pack_8",
	/* rx counters */
	"rx_mpdu_cnt",
	"rx_ampdu_cnt",
	"rx_ampdu_bytes_cnt",
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
	"v_tx_bw_20",
	"v_tx_bw_40",
	"v_tx_bw_80",
	"v_tx_bw_160",
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
};

static void
mt7921_get_et_strings(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		      u32 sset, u8 *data)
{
	struct mt7921_dev *dev = mt7921_hw_dev(hw);

	if (sset != ETH_SS_STATS)
		return;

	memcpy(data, *mt7921_gstrings_stats, sizeof(mt7921_gstrings_stats));

	if (mt76_is_sdio(&dev->mt76))
		return;

	data += sizeof(mt7921_gstrings_stats);
	page_pool_ethtool_stats_get_strings(data);
}

static int
mt7921_get_et_sset_count(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			 int sset)
{
	struct mt7921_dev *dev = mt7921_hw_dev(hw);

	if (sset != ETH_SS_STATS)
		return 0;

	if (mt76_is_sdio(&dev->mt76))
		return ARRAY_SIZE(mt7921_gstrings_stats);

	return ARRAY_SIZE(mt7921_gstrings_stats) +
	       page_pool_ethtool_stats_get_count();
}

static void
mt7921_ethtool_worker(void *wi_data, struct ieee80211_sta *sta)
{
	struct mt7921_sta *msta = (struct mt7921_sta *)sta->drv_priv;
	struct mt76_ethtool_worker_info *wi = wi_data;

	if (msta->vif->mt76.idx != wi->idx)
		return;

	mt76_ethtool_worker(wi, &msta->wcid.stats, false);
}

static
void mt7921_get_et_stats(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			 struct ethtool_stats *stats, u64 *data)
{
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;
	int stats_size = ARRAY_SIZE(mt7921_gstrings_stats);
	struct mt7921_phy *phy = mt7921_hw_phy(hw);
	struct mt7921_dev *dev = phy->dev;
	struct mib_stats *mib = &phy->mib;
	struct mt76_ethtool_worker_info wi = {
		.data = data,
		.idx = mvif->mt76.idx,
	};
	int i, ei = 0;

	mt7921_mutex_acquire(dev);

	mt7921_mac_update_mib_stats(phy);

	data[ei++] = mib->tx_ampdu_cnt;
	data[ei++] = mib->tx_mpdu_attempts_cnt;
	data[ei++] = mib->tx_mpdu_success_cnt;
	data[ei++] = mib->tx_pkt_ebf_cnt;
	data[ei++] = mib->tx_pkt_ibf_cnt;

	/* Tx ampdu stat */
	for (i = 0; i < 15; i++)
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

	/* Tx amsdu info (pack-count histogram) */
	for (i = 0; i < ARRAY_SIZE(mib->tx_amsdu); i++)
		data[ei++] = mib->tx_amsdu[i];

	/* rx counters */
	data[ei++] = mib->rx_mpdu_cnt;
	data[ei++] = mib->rx_ampdu_cnt;
	data[ei++] = mib->rx_ampdu_bytes_cnt;
	data[ei++] = mib->rx_ba_cnt;

	/* Add values for all stations owned by this vif */
	wi.initial_stat_idx = ei;
	ieee80211_iterate_stations_atomic(hw, mt7921_ethtool_worker, &wi);

	mt7921_mutex_release(dev);

	if (!wi.sta_count)
		return;

	ei += wi.worker_stat_count;

	if (!mt76_is_sdio(&dev->mt76)) {
		mt76_ethtool_page_pool_stats(&dev->mt76, &data[ei], &ei);
		stats_size += page_pool_ethtool_stats_get_count();
	}

	if (ei != stats_size)
		dev_err(dev->mt76.dev, "ei: %d  SSTATS_LEN: %d", ei, stats_size);
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
		struct mt76_connac2_mcu_rxd *rxd;
		struct sk_buff *skb;

		spin_lock_bh(&phy->dev->mt76.lock);
		skb = __skb_dequeue(&phy->scan_event_list);
		spin_unlock_bh(&phy->dev->mt76.lock);

		if (!skb)
			break;

		rxd = (struct mt76_connac2_mcu_rxd *)skb->data;
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
		return -EINVAL;

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
	struct rate_info *txrate = &msta->wcid.rate;

	if (!txrate->legacy && !txrate->flags)
		return;

	if (txrate->legacy) {
		sinfo->txrate.legacy = txrate->legacy;
	} else {
		sinfo->txrate.mcs = txrate->mcs;
		sinfo->txrate.nss = txrate->nss;
		sinfo->txrate.bw = txrate->bw;
		sinfo->txrate.he_gi = txrate->he_gi;
		sinfo->txrate.he_dcm = txrate->he_dcm;
		sinfo->txrate.he_ru_alloc = txrate->he_ru_alloc;
	}
	sinfo->txrate.flags = txrate->flags;
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_BITRATE);

	sinfo->ack_signal = (s8)msta->ack_signal;
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_ACK_SIGNAL);

	sinfo->avg_ack_signal = -(s8)ewma_avg_signal_read(&msta->avg_ack_signal);
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_ACK_SIGNAL_AVG);
}

#ifdef CONFIG_PM
static int mt7921_suspend(struct ieee80211_hw *hw,
			  struct cfg80211_wowlan *wowlan)
{
	struct mt7921_dev *dev = mt7921_hw_dev(hw);
	struct mt7921_phy *phy = mt7921_hw_phy(hw);

	cancel_delayed_work_sync(&phy->scan_work);
	cancel_delayed_work_sync(&phy->mt76->mac_work);

	cancel_delayed_work_sync(&dev->pm.ps_work);
	mt76_connac_free_pending_tx_skbs(&dev->pm, NULL);

	mt7921_mutex_acquire(dev);

	clear_bit(MT76_STATE_RUNNING, &phy->mt76->state);
	ieee80211_iterate_active_interfaces(hw,
					    IEEE80211_IFACE_ITER_RESUME_ALL,
					    mt7921_mcu_set_suspend_iter,
					    &dev->mphy);

	mt7921_mutex_release(dev);

	return 0;
}

static int mt7921_resume(struct ieee80211_hw *hw)
{
	struct mt7921_dev *dev = mt7921_hw_dev(hw);
	struct mt7921_phy *phy = mt7921_hw_phy(hw);

	mt7921_mutex_acquire(dev);

	set_bit(MT76_STATE_RUNNING, &phy->mt76->state);
	ieee80211_iterate_active_interfaces(hw,
					    IEEE80211_IFACE_ITER_RESUME_ALL,
					    mt76_connac_mcu_set_suspend_iter,
					    &dev->mphy);

	ieee80211_queue_delayed_work(hw, &phy->mt76->mac_work,
				     MT7921_WATCHDOG_TIME);

	mt7921_mutex_release(dev);

	return 0;
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

	mt7921_mutex_acquire(dev);

	if (enabled)
		set_bit(MT_WCID_FLAG_HDR_TRANS, &msta->wcid.flags);
	else
		clear_bit(MT_WCID_FLAG_HDR_TRANS, &msta->wcid.flags);

	mt76_connac_mcu_sta_update_hdr_trans(&dev->mt76, vif, &msta->wcid,
					     MCU_UNI_CMD(STA_REC_UPDATE));

	mt7921_mutex_release(dev);
}

#if IS_ENABLED(CONFIG_IPV6)
static void mt7921_ipv6_addr_change(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    struct inet6_dev *idev)
{
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;
	struct mt7921_dev *dev = mvif->phy->dev;
	struct inet6_ifaddr *ifa;
	struct in6_addr ns_addrs[IEEE80211_BSS_ARP_ADDR_LIST_LEN];
	struct sk_buff *skb;
	u8 i, idx = 0;

	struct {
		struct {
			u8 bss_idx;
			u8 pad[3];
		} __packed hdr;
		struct mt76_connac_arpns_tlv arpns;
	} req_hdr = {
		.hdr = {
			.bss_idx = mvif->mt76.idx,
		},
		.arpns = {
			.tag = cpu_to_le16(UNI_OFFLOAD_OFFLOAD_ND),
			.mode = 2,  /* update */
			.option = 1, /* update only */
		},
	};

	read_lock_bh(&idev->lock);
	list_for_each_entry(ifa, &idev->addr_list, if_list) {
		if (ifa->flags & IFA_F_TENTATIVE)
			continue;
		ns_addrs[idx] = ifa->addr;
		if (++idx >= IEEE80211_BSS_ARP_ADDR_LIST_LEN)
			break;
	}
	read_unlock_bh(&idev->lock);

	if (!idx)
		return;

	req_hdr.arpns.ips_num = idx;
	req_hdr.arpns.len = cpu_to_le16(sizeof(struct mt76_connac_arpns_tlv)
					+ idx * sizeof(struct in6_addr));
	skb = __mt76_mcu_msg_alloc(&dev->mt76, &req_hdr,
			sizeof(req_hdr) + idx * sizeof(struct in6_addr),
			sizeof(req_hdr), GFP_ATOMIC);
	if (!skb)
		return;

	for (i = 0; i < idx; i++)
		skb_put_data(skb, &ns_addrs[i].in6_u, sizeof(struct in6_addr));

	skb_queue_tail(&dev->ipv6_ns_list, skb);

	ieee80211_queue_work(dev->mt76.hw, &dev->ipv6_ns_work);
}
#endif

int mt7921_set_tx_sar_pwr(struct ieee80211_hw *hw,
			  const struct cfg80211_sar_specs *sar)
{
	struct mt76_phy *mphy = hw->priv;
	int err;

	if (sar) {
		err = mt76_init_sar_power(hw, sar);
		if (err)
			return err;
	}

	mt7921_init_acpi_sar_power(mt7921_hw_phy(hw), !sar);

	err = mt76_connac_mcu_set_rate_txpower(mphy);

	return err;
}

static int mt7921_set_sar_specs(struct ieee80211_hw *hw,
				const struct cfg80211_sar_specs *sar)
{
	struct mt7921_dev *dev = mt7921_hw_dev(hw);
	int err;

	mt7921_mutex_acquire(dev);
	err = mt7921_mcu_set_clc(dev, dev->mt76.alpha2,
				 dev->country_ie_env);
	if (err < 0)
		goto out;

	err = mt7921_set_tx_sar_pwr(hw, sar);
out:
	mt7921_mutex_release(dev);

	return err;
}

static void
mt7921_channel_switch_beacon(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct cfg80211_chan_def *chandef)
{
	struct mt7921_dev *dev = mt7921_hw_dev(hw);

	mt7921_mutex_acquire(dev);
	mt7921_mcu_uni_add_beacon_offload(dev, hw, vif, true);
	mt7921_mutex_release(dev);
}

static int
mt7921_start_ap(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		struct ieee80211_bss_conf *link_conf)
{
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;
	struct mt7921_phy *phy = mt7921_hw_phy(hw);
	struct mt7921_dev *dev = mt7921_hw_dev(hw);
	int err;

	mt7921_mutex_acquire(dev);

	err = mt76_connac_mcu_uni_add_bss(phy->mt76, vif, &mvif->sta.wcid,
					  true, mvif->ctx);
	if (err)
		goto out;

	err = mt7921_mcu_set_bss_pm(dev, vif, true);
	if (err)
		goto out;

	err = mt7921_mcu_sta_update(dev, NULL, vif, true,
				    MT76_STA_INFO_STATE_NONE);
out:
	mt7921_mutex_release(dev);

	return err;
}

static void
mt7921_stop_ap(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	       struct ieee80211_bss_conf *link_conf)
{
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;
	struct mt7921_phy *phy = mt7921_hw_phy(hw);
	struct mt7921_dev *dev = mt7921_hw_dev(hw);
	int err;

	mt7921_mutex_acquire(dev);

	err = mt7921_mcu_set_bss_pm(dev, vif, false);
	if (err)
		goto out;

	mt76_connac_mcu_uni_add_bss(phy->mt76, vif, &mvif->sta.wcid, false,
				    mvif->ctx);

out:
	mt7921_mutex_release(dev);
}

static int
mt7921_add_chanctx(struct ieee80211_hw *hw,
		   struct ieee80211_chanctx_conf *ctx)
{
	return 0;
}

static void
mt7921_remove_chanctx(struct ieee80211_hw *hw,
		      struct ieee80211_chanctx_conf *ctx)
{
}

static void mt7921_ctx_iter(void *priv, u8 *mac,
			    struct ieee80211_vif *vif)
{
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;
	struct ieee80211_chanctx_conf *ctx = priv;

	if (ctx != mvif->ctx)
		return;

	if (vif->type == NL80211_IFTYPE_MONITOR)
		mt7921_mcu_config_sniffer(mvif, ctx);
	else
		mt76_connac_mcu_uni_set_chctx(mvif->phy->mt76, &mvif->mt76, ctx);
}

static void
mt7921_change_chanctx(struct ieee80211_hw *hw,
		      struct ieee80211_chanctx_conf *ctx,
		      u32 changed)
{
	struct mt7921_phy *phy = mt7921_hw_phy(hw);

	mt7921_mutex_acquire(phy->dev);
	ieee80211_iterate_active_interfaces(phy->mt76->hw,
					    IEEE80211_IFACE_ITER_ACTIVE,
					    mt7921_ctx_iter, ctx);
	mt7921_mutex_release(phy->dev);
}

static int
mt7921_assign_vif_chanctx(struct ieee80211_hw *hw,
			  struct ieee80211_vif *vif,
			  struct ieee80211_bss_conf *link_conf,
			  struct ieee80211_chanctx_conf *ctx)
{
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;
	struct mt7921_dev *dev = mt7921_hw_dev(hw);

	mutex_lock(&dev->mt76.mutex);
	mvif->ctx = ctx;
	mutex_unlock(&dev->mt76.mutex);

	return 0;
}

static void
mt7921_unassign_vif_chanctx(struct ieee80211_hw *hw,
			    struct ieee80211_vif *vif,
			    struct ieee80211_bss_conf *link_conf,
			    struct ieee80211_chanctx_conf *ctx)
{
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;
	struct mt7921_dev *dev = mt7921_hw_dev(hw);

	mutex_lock(&dev->mt76.mutex);
	mvif->ctx = NULL;
	mutex_unlock(&dev->mt76.mutex);
}

static void mt7921_mgd_prepare_tx(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct ieee80211_prep_tx_info *info)
{
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;
	struct mt7921_dev *dev = mt7921_hw_dev(hw);
	u16 duration = info->duration ? info->duration :
		       jiffies_to_msecs(HZ);

	mt7921_mutex_acquire(dev);
	mt7921_set_roc(mvif->phy, mvif, mvif->ctx->def.chan, duration,
		       MT7921_ROC_REQ_JOIN);
	mt7921_mutex_release(dev);
}

static void mt7921_mgd_complete_tx(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   struct ieee80211_prep_tx_info *info)
{
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;

	mt7921_abort_roc(mvif->phy, mvif);
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
	.start_ap = mt7921_start_ap,
	.stop_ap = mt7921_stop_ap,
	.sta_state = mt7921_sta_state,
	.sta_pre_rcu_remove = mt76_sta_pre_rcu_remove,
	.set_key = mt7921_set_key,
	.sta_set_decap_offload = mt7921_sta_set_decap_offload,
#if IS_ENABLED(CONFIG_IPV6)
	.ipv6_addr_change = mt7921_ipv6_addr_change,
#endif /* CONFIG_IPV6 */
	.ampdu_action = mt7921_ampdu_action,
	.set_rts_threshold = mt7921_set_rts_threshold,
	.wake_tx_queue = mt76_wake_tx_queue,
	.release_buffered_frames = mt76_release_buffered_frames,
	.channel_switch_beacon = mt7921_channel_switch_beacon,
	.get_txpower = mt76_get_txpower,
	.get_stats = mt7921_get_stats,
	.get_et_sset_count = mt7921_get_et_sset_count,
	.get_et_strings = mt7921_get_et_strings,
	.get_et_stats = mt7921_get_et_stats,
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
	CFG80211_TESTMODE_CMD(mt7921_testmode_cmd)
	CFG80211_TESTMODE_DUMP(mt7921_testmode_dump)
#ifdef CONFIG_PM
	.suspend = mt7921_suspend,
	.resume = mt7921_resume,
	.set_wakeup = mt7921_set_wakeup,
	.set_rekey_data = mt7921_set_rekey_data,
#endif /* CONFIG_PM */
	.flush = mt7921_flush,
	.set_sar_specs = mt7921_set_sar_specs,
	.remain_on_channel = mt7921_remain_on_channel,
	.cancel_remain_on_channel = mt7921_cancel_remain_on_channel,
	.add_chanctx = mt7921_add_chanctx,
	.remove_chanctx = mt7921_remove_chanctx,
	.change_chanctx = mt7921_change_chanctx,
	.assign_vif_chanctx = mt7921_assign_vif_chanctx,
	.unassign_vif_chanctx = mt7921_unassign_vif_chanctx,
	.mgd_prepare_tx = mt7921_mgd_prepare_tx,
	.mgd_complete_tx = mt7921_mgd_complete_tx,
};
EXPORT_SYMBOL_GPL(mt7921_ops);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Sean Wang <sean.wang@mediatek.com>");
