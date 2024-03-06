// SPDX-License-Identifier: ISC
/* Copyright (C) 2023 MediaTek Inc. */

#include <linux/etherdevice.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/ctype.h>
#include <net/ipv6.h>
#include "mt7925.h"
#include "mcu.h"
#include "mac.h"

static void
mt7925_init_he_caps(struct mt792x_phy *phy, enum nl80211_band band,
		    struct ieee80211_sband_iftype_data *data,
		    enum nl80211_iftype iftype)
{
	struct ieee80211_sta_he_cap *he_cap = &data->he_cap;
	struct ieee80211_he_cap_elem *he_cap_elem = &he_cap->he_cap_elem;
	struct ieee80211_he_mcs_nss_supp *he_mcs = &he_cap->he_mcs_nss_supp;
	int i, nss = hweight8(phy->mt76->antenna_mask);
	u16 mcs_map = 0;

	for (i = 0; i < 8; i++) {
		if (i < nss)
			mcs_map |= (IEEE80211_HE_MCS_SUPPORT_0_11 << (i * 2));
		else
			mcs_map |= (IEEE80211_HE_MCS_NOT_SUPPORTED << (i * 2));
	}

	he_cap->has_he = true;

	he_cap_elem->mac_cap_info[0] = IEEE80211_HE_MAC_CAP0_HTC_HE;
	he_cap_elem->mac_cap_info[3] = IEEE80211_HE_MAC_CAP3_OMI_CONTROL |
				       IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_3;
	he_cap_elem->mac_cap_info[4] = IEEE80211_HE_MAC_CAP4_AMSDU_IN_AMPDU;

	if (band == NL80211_BAND_2GHZ)
		he_cap_elem->phy_cap_info[0] =
			IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G;
	else
		he_cap_elem->phy_cap_info[0] =
			IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G |
			IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G;

	he_cap_elem->phy_cap_info[1] =
		IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD;
	he_cap_elem->phy_cap_info[2] =
		IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US |
		IEEE80211_HE_PHY_CAP2_STBC_TX_UNDER_80MHZ |
		IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ |
		IEEE80211_HE_PHY_CAP2_UL_MU_FULL_MU_MIMO |
		IEEE80211_HE_PHY_CAP2_UL_MU_PARTIAL_MU_MIMO;

	switch (iftype) {
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
			IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_4 |
			IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_4;
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
			IEEE80211_HE_PHY_CAP8_20MHZ_IN_160MHZ_HE_PPDU |
			IEEE80211_HE_PHY_CAP8_80MHZ_IN_160MHZ_HE_PPDU |
			IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_484;
		he_cap_elem->phy_cap_info[9] |=
			IEEE80211_HE_PHY_CAP9_LONGER_THAN_16_SIGB_OFDM_SYM |
			IEEE80211_HE_PHY_CAP9_NON_TRIGGERED_CQI_FEEDBACK |
			IEEE80211_HE_PHY_CAP9_TX_1024_QAM_LESS_THAN_242_TONE_RU |
			IEEE80211_HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU |
			IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_COMP_SIGB |
			IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_NON_COMP_SIGB;
		break;
	default:
		break;
	}

	he_mcs->rx_mcs_80 = cpu_to_le16(mcs_map);
	he_mcs->tx_mcs_80 = cpu_to_le16(mcs_map);
	he_mcs->rx_mcs_160 = cpu_to_le16(mcs_map);
	he_mcs->tx_mcs_160 = cpu_to_le16(mcs_map);

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
		u16 cap = IEEE80211_HE_6GHZ_CAP_TX_ANTPAT_CONS |
			  IEEE80211_HE_6GHZ_CAP_RX_ANTPAT_CONS;

		cap |= u16_encode_bits(IEEE80211_HT_MPDU_DENSITY_0_5,
				       IEEE80211_HE_6GHZ_CAP_MIN_MPDU_START) |
		       u16_encode_bits(IEEE80211_VHT_MAX_AMPDU_1024K,
				       IEEE80211_HE_6GHZ_CAP_MAX_AMPDU_LEN_EXP) |
		       u16_encode_bits(IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454,
				       IEEE80211_HE_6GHZ_CAP_MAX_MPDU_LEN);

		data->he_6ghz_capa.capa = cpu_to_le16(cap);
	}
}

static void
mt7925_init_eht_caps(struct mt792x_phy *phy, enum nl80211_band band,
		     struct ieee80211_sband_iftype_data *data)
{
	struct ieee80211_sta_eht_cap *eht_cap = &data->eht_cap;
	struct ieee80211_eht_cap_elem_fixed *eht_cap_elem = &eht_cap->eht_cap_elem;
	struct ieee80211_eht_mcs_nss_supp *eht_nss = &eht_cap->eht_mcs_nss_supp;
	enum nl80211_chan_width width = phy->mt76->chandef.width;
	int nss = hweight8(phy->mt76->antenna_mask);
	int sts = hweight16(phy->mt76->chainmask);
	u8 val;

	if (!phy->dev->has_eht)
		return;

	eht_cap->has_eht = true;

	eht_cap_elem->mac_cap_info[0] =
		IEEE80211_EHT_MAC_CAP0_EPCS_PRIO_ACCESS |
		IEEE80211_EHT_MAC_CAP0_OM_CONTROL;

	eht_cap_elem->phy_cap_info[0] =
		IEEE80211_EHT_PHY_CAP0_NDP_4_EHT_LFT_32_GI |
		IEEE80211_EHT_PHY_CAP0_SU_BEAMFORMER |
		IEEE80211_EHT_PHY_CAP0_SU_BEAMFORMEE;

	eht_cap_elem->phy_cap_info[0] |=
		u8_encode_bits(u8_get_bits(sts - 1, BIT(0)),
			       IEEE80211_EHT_PHY_CAP0_BEAMFORMEE_SS_80MHZ_MASK);

	eht_cap_elem->phy_cap_info[1] =
		u8_encode_bits(u8_get_bits(sts - 1, GENMASK(2, 1)),
			       IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_80MHZ_MASK) |
		u8_encode_bits(sts - 1,
			       IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_160MHZ_MASK);

	eht_cap_elem->phy_cap_info[2] =
		u8_encode_bits(sts - 1, IEEE80211_EHT_PHY_CAP2_SOUNDING_DIM_80MHZ_MASK) |
		u8_encode_bits(sts - 1, IEEE80211_EHT_PHY_CAP2_SOUNDING_DIM_160MHZ_MASK);

	eht_cap_elem->phy_cap_info[3] =
		IEEE80211_EHT_PHY_CAP3_NG_16_SU_FEEDBACK |
		IEEE80211_EHT_PHY_CAP3_NG_16_MU_FEEDBACK |
		IEEE80211_EHT_PHY_CAP3_CODEBOOK_4_2_SU_FDBK |
		IEEE80211_EHT_PHY_CAP3_CODEBOOK_7_5_MU_FDBK |
		IEEE80211_EHT_PHY_CAP3_TRIG_SU_BF_FDBK |
		IEEE80211_EHT_PHY_CAP3_TRIG_MU_BF_PART_BW_FDBK |
		IEEE80211_EHT_PHY_CAP3_TRIG_CQI_FDBK;

	eht_cap_elem->phy_cap_info[4] =
		u8_encode_bits(min_t(int, sts - 1, 2),
			       IEEE80211_EHT_PHY_CAP4_MAX_NC_MASK);

	eht_cap_elem->phy_cap_info[5] =
		IEEE80211_EHT_PHY_CAP5_NON_TRIG_CQI_FEEDBACK |
		u8_encode_bits(IEEE80211_EHT_PHY_CAP5_COMMON_NOMINAL_PKT_PAD_16US,
			       IEEE80211_EHT_PHY_CAP5_COMMON_NOMINAL_PKT_PAD_MASK) |
		u8_encode_bits(u8_get_bits(0x11, GENMASK(1, 0)),
			       IEEE80211_EHT_PHY_CAP5_MAX_NUM_SUPP_EHT_LTF_MASK);

	val = width == NL80211_CHAN_WIDTH_160 ? 0x7 :
	      width == NL80211_CHAN_WIDTH_80 ? 0x3 : 0x1;
	eht_cap_elem->phy_cap_info[6] =
		u8_encode_bits(u8_get_bits(0x11, GENMASK(4, 2)),
			       IEEE80211_EHT_PHY_CAP6_MAX_NUM_SUPP_EHT_LTF_MASK) |
		u8_encode_bits(val, IEEE80211_EHT_PHY_CAP6_MCS15_SUPP_MASK);

	eht_cap_elem->phy_cap_info[7] =
		IEEE80211_EHT_PHY_CAP7_NON_OFDMA_UL_MU_MIMO_80MHZ |
		IEEE80211_EHT_PHY_CAP7_NON_OFDMA_UL_MU_MIMO_160MHZ |
		IEEE80211_EHT_PHY_CAP7_MU_BEAMFORMER_80MHZ |
		IEEE80211_EHT_PHY_CAP7_MU_BEAMFORMER_160MHZ;

	val = u8_encode_bits(nss, IEEE80211_EHT_MCS_NSS_RX) |
	      u8_encode_bits(nss, IEEE80211_EHT_MCS_NSS_TX);

	eht_nss->bw._80.rx_tx_mcs9_max_nss = val;
	eht_nss->bw._80.rx_tx_mcs11_max_nss = val;
	eht_nss->bw._80.rx_tx_mcs13_max_nss = val;
	eht_nss->bw._160.rx_tx_mcs9_max_nss = val;
	eht_nss->bw._160.rx_tx_mcs11_max_nss = val;
	eht_nss->bw._160.rx_tx_mcs13_max_nss = val;
}

static void
__mt7925_set_stream_he_eht_caps(struct mt792x_phy *phy,
				struct ieee80211_supported_band *sband,
				enum nl80211_band band)
{
	struct ieee80211_sband_iftype_data *data = phy->iftype[band];
	int i, n = 0;

	for (i = 0; i < NUM_NL80211_IFTYPES; i++) {
		switch (i) {
		case NL80211_IFTYPE_STATION:
		case NL80211_IFTYPE_AP:
			break;
		default:
			continue;
		}

		data[n].types_mask = BIT(i);
		mt7925_init_he_caps(phy, band, &data[n], i);
		mt7925_init_eht_caps(phy, band, &data[n]);

		n++;
	}

	_ieee80211_set_sband_iftype_data(sband, data, n);
}

void mt7925_set_stream_he_eht_caps(struct mt792x_phy *phy)
{
	if (phy->mt76->cap.has_2ghz)
		__mt7925_set_stream_he_eht_caps(phy, &phy->mt76->sband_2g.sband,
						NL80211_BAND_2GHZ);

	if (phy->mt76->cap.has_5ghz)
		__mt7925_set_stream_he_eht_caps(phy, &phy->mt76->sband_5g.sband,
						NL80211_BAND_5GHZ);

	if (phy->mt76->cap.has_6ghz)
		__mt7925_set_stream_he_eht_caps(phy, &phy->mt76->sband_6g.sband,
						NL80211_BAND_6GHZ);
}

int __mt7925_start(struct mt792x_phy *phy)
{
	struct mt76_phy *mphy = phy->mt76;
	int err;

	err = mt7925_mcu_set_channel_domain(mphy);
	if (err)
		return err;

	err = mt7925_mcu_set_rts_thresh(phy, 0x92b);
	if (err)
		return err;

	err = mt7925_set_tx_sar_pwr(mphy->hw, NULL);
	if (err)
		return err;

	mt792x_mac_reset_counters(phy);
	set_bit(MT76_STATE_RUNNING, &mphy->state);

	ieee80211_queue_delayed_work(mphy->hw, &mphy->mac_work,
				     MT792x_WATCHDOG_TIME);

	return 0;
}
EXPORT_SYMBOL_GPL(__mt7925_start);

static int mt7925_start(struct ieee80211_hw *hw)
{
	struct mt792x_phy *phy = mt792x_hw_phy(hw);
	int err;

	mt792x_mutex_acquire(phy->dev);
	err = __mt7925_start(phy);
	mt792x_mutex_release(phy->dev);

	return err;
}

static int
mt7925_add_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;
	struct mt792x_dev *dev = mt792x_hw_dev(hw);
	struct mt792x_phy *phy = mt792x_hw_phy(hw);
	struct mt76_txq *mtxq;
	int idx, ret = 0;

	mt792x_mutex_acquire(dev);

	mvif->mt76.idx = __ffs64(~dev->mt76.vif_mask);
	if (mvif->mt76.idx >= MT792x_MAX_INTERFACES) {
		ret = -ENOSPC;
		goto out;
	}

	mvif->mt76.omac_idx = mvif->mt76.idx;
	mvif->phy = phy;
	mvif->mt76.band_idx = 0;
	mvif->mt76.wmm_idx = mvif->mt76.idx % MT76_CONNAC_MAX_WMM_SETS;

	if (phy->mt76->chandef.chan->band != NL80211_BAND_2GHZ)
		mvif->mt76.basic_rates_idx = MT792x_BASIC_RATES_TBL + 4;
	else
		mvif->mt76.basic_rates_idx = MT792x_BASIC_RATES_TBL;

	ret = mt76_connac_mcu_uni_add_dev(&dev->mphy, vif, &mvif->sta.wcid,
					  true);
	if (ret)
		goto out;

	dev->mt76.vif_mask |= BIT_ULL(mvif->mt76.idx);
	phy->omac_mask |= BIT_ULL(mvif->mt76.omac_idx);

	idx = MT792x_WTBL_RESERVED - mvif->mt76.idx;

	INIT_LIST_HEAD(&mvif->sta.wcid.poll_list);
	mvif->sta.wcid.idx = idx;
	mvif->sta.wcid.phy_idx = mvif->mt76.band_idx;
	mvif->sta.wcid.hw_key_idx = -1;
	mvif->sta.wcid.tx_info |= MT_WCID_TX_INFO_SET;
	mt76_wcid_init(&mvif->sta.wcid);

	mt7925_mac_wtbl_update(dev, idx,
			       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);

	ewma_rssi_init(&mvif->rssi);

	rcu_assign_pointer(dev->mt76.wcid[idx], &mvif->sta.wcid);
	if (vif->txq) {
		mtxq = (struct mt76_txq *)vif->txq->drv_priv;
		mtxq->wcid = idx;
	}

	vif->driver_flags |= IEEE80211_VIF_BEACON_FILTER;
out:
	mt792x_mutex_release(dev);

	return ret;
}

static void mt7925_roc_iter(void *priv, u8 *mac,
			    struct ieee80211_vif *vif)
{
	struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;
	struct mt792x_phy *phy = priv;

	mt7925_mcu_abort_roc(phy, mvif, phy->roc_token_id);
}

void mt7925_roc_work(struct work_struct *work)
{
	struct mt792x_phy *phy;

	phy = (struct mt792x_phy *)container_of(work, struct mt792x_phy,
						roc_work);

	if (!test_and_clear_bit(MT76_STATE_ROC, &phy->mt76->state))
		return;

	mt792x_mutex_acquire(phy->dev);
	ieee80211_iterate_active_interfaces(phy->mt76->hw,
					    IEEE80211_IFACE_ITER_RESUME_ALL,
					    mt7925_roc_iter, phy);
	mt792x_mutex_release(phy->dev);
	ieee80211_remain_on_channel_expired(phy->mt76->hw);
}

static int mt7925_abort_roc(struct mt792x_phy *phy, struct mt792x_vif *vif)
{
	int err = 0;

	del_timer_sync(&phy->roc_timer);
	cancel_work_sync(&phy->roc_work);

	mt792x_mutex_acquire(phy->dev);
	if (test_and_clear_bit(MT76_STATE_ROC, &phy->mt76->state))
		err = mt7925_mcu_abort_roc(phy, vif, phy->roc_token_id);
	mt792x_mutex_release(phy->dev);

	return err;
}

static int mt7925_set_roc(struct mt792x_phy *phy,
			  struct mt792x_vif *vif,
			  struct ieee80211_channel *chan,
			  int duration,
			  enum mt7925_roc_req type)
{
	int err;

	if (test_and_set_bit(MT76_STATE_ROC, &phy->mt76->state))
		return -EBUSY;

	phy->roc_grant = false;

	err = mt7925_mcu_set_roc(phy, vif, chan, duration, type,
				 ++phy->roc_token_id);
	if (err < 0) {
		clear_bit(MT76_STATE_ROC, &phy->mt76->state);
		goto out;
	}

	if (!wait_event_timeout(phy->roc_wait, phy->roc_grant, 4 * HZ)) {
		mt7925_mcu_abort_roc(phy, vif, phy->roc_token_id);
		clear_bit(MT76_STATE_ROC, &phy->mt76->state);
		err = -ETIMEDOUT;
	}

out:
	return err;
}

static int mt7925_remain_on_channel(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    struct ieee80211_channel *chan,
				    int duration,
				    enum ieee80211_roc_type type)
{
	struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;
	struct mt792x_phy *phy = mt792x_hw_phy(hw);
	int err;

	mt792x_mutex_acquire(phy->dev);
	err = mt7925_set_roc(phy, mvif, chan, duration, MT7925_ROC_REQ_ROC);
	mt792x_mutex_release(phy->dev);

	return err;
}

static int mt7925_cancel_remain_on_channel(struct ieee80211_hw *hw,
					   struct ieee80211_vif *vif)
{
	struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;
	struct mt792x_phy *phy = mt792x_hw_phy(hw);

	return mt7925_abort_roc(phy, mvif);
}

static int mt7925_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			  struct ieee80211_vif *vif, struct ieee80211_sta *sta,
			  struct ieee80211_key_conf *key)
{
	struct mt792x_dev *dev = mt792x_hw_dev(hw);
	struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;
	struct mt792x_sta *msta = sta ? (struct mt792x_sta *)sta->drv_priv :
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

	mt792x_mutex_acquire(dev);

	if (cmd == SET_KEY && !mvif->mt76.cipher) {
		struct mt792x_phy *phy = mt792x_hw_phy(hw);

		mvif->mt76.cipher = mt76_connac_mcu_get_cipher(key->cipher);
		mt7925_mcu_add_bss_info(phy, mvif->mt76.ctx, vif, sta, true);
	}

	if (cmd == SET_KEY)
		*wcid_keyidx = idx;
	else if (idx == *wcid_keyidx)
		*wcid_keyidx = -1;
	else
		goto out;

	mt76_wcid_key_setup(&dev->mt76, wcid,
			    cmd == SET_KEY ? key : NULL);

	err = mt7925_mcu_add_key(&dev->mt76, vif, &msta->bip,
				 key, MCU_UNI_CMD(STA_REC_UPDATE),
				 &msta->wcid, cmd);

	if (err)
		goto out;

	if (key->cipher == WLAN_CIPHER_SUITE_WEP104 ||
	    key->cipher == WLAN_CIPHER_SUITE_WEP40)
		err = mt7925_mcu_add_key(&dev->mt76, vif, &mvif->wep_sta->bip,
					 key, MCU_WMWA_UNI_CMD(STA_REC_UPDATE),
					 &mvif->wep_sta->wcid, cmd);

out:
	mt792x_mutex_release(dev);

	return err;
}

static void
mt7925_pm_interface_iter(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	struct mt792x_dev *dev = priv;
	struct ieee80211_hw *hw = mt76_hw(dev);
	bool pm_enable = dev->pm.enable;
	int err;

	err = mt7925_mcu_set_beacon_filter(dev, vif, pm_enable);
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
mt7925_sniffer_interface_iter(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	struct mt792x_dev *dev = priv;
	struct ieee80211_hw *hw = mt76_hw(dev);
	struct mt76_connac_pm *pm = &dev->pm;
	bool monitor = !!(hw->conf.flags & IEEE80211_CONF_MONITOR);

	mt7925_mcu_set_sniffer(dev, vif, monitor);
	pm->enable = pm->enable_user && !monitor;
	pm->ds_enable = pm->ds_enable_user && !monitor;

	mt7925_mcu_set_deep_sleep(dev, pm->ds_enable);

	if (monitor)
		mt7925_mcu_set_beacon_filter(dev, vif, false);
}

void mt7925_set_runtime_pm(struct mt792x_dev *dev)
{
	struct ieee80211_hw *hw = mt76_hw(dev);
	struct mt76_connac_pm *pm = &dev->pm;
	bool monitor = !!(hw->conf.flags & IEEE80211_CONF_MONITOR);

	pm->enable = pm->enable_user && !monitor;
	ieee80211_iterate_active_interfaces(hw,
					    IEEE80211_IFACE_ITER_RESUME_ALL,
					    mt7925_pm_interface_iter, dev);
	pm->ds_enable = pm->ds_enable_user && !monitor;
	mt7925_mcu_set_deep_sleep(dev, pm->ds_enable);
}

static int mt7925_config(struct ieee80211_hw *hw, u32 changed)
{
	struct mt792x_dev *dev = mt792x_hw_dev(hw);
	int ret = 0;

	mt792x_mutex_acquire(dev);

	if (changed & IEEE80211_CONF_CHANGE_POWER) {
		ret = mt7925_set_tx_sar_pwr(hw, NULL);
		if (ret)
			goto out;
	}

	if (changed & IEEE80211_CONF_CHANGE_MONITOR) {
		ieee80211_iterate_active_interfaces(hw,
						    IEEE80211_IFACE_ITER_RESUME_ALL,
						    mt7925_sniffer_interface_iter, dev);
	}

out:
	mt792x_mutex_release(dev);

	return ret;
}

static void mt7925_configure_filter(struct ieee80211_hw *hw,
				    unsigned int changed_flags,
				    unsigned int *total_flags,
				    u64 multicast)
{
#define MT7925_FILTER_FCSFAIL    BIT(2)
#define MT7925_FILTER_CONTROL    BIT(5)
#define MT7925_FILTER_OTHER_BSS  BIT(6)
#define MT7925_FILTER_ENABLE     BIT(31)
	struct mt792x_dev *dev = mt792x_hw_dev(hw);
	u32 flags = MT7925_FILTER_ENABLE;

#define MT7925_FILTER(_fif, _type) do {			\
		if (*total_flags & (_fif))		\
			flags |= MT7925_FILTER_##_type;	\
	} while (0)

	MT7925_FILTER(FIF_FCSFAIL, FCSFAIL);
	MT7925_FILTER(FIF_CONTROL, CONTROL);
	MT7925_FILTER(FIF_OTHER_BSS, OTHER_BSS);

	mt792x_mutex_acquire(dev);
	mt7925_mcu_set_rxfilter(dev, flags, 0, 0);
	mt792x_mutex_release(dev);

	*total_flags &= (FIF_OTHER_BSS | FIF_FCSFAIL | FIF_CONTROL);
}

static u8
mt7925_get_rates_table(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		       bool beacon, bool mcast)
{
	struct mt76_vif *mvif = (struct mt76_vif *)vif->drv_priv;
	struct mt76_phy *mphy = hw->priv;
	u16 rate;
	u8 i, idx, ht;

	rate = mt76_connac2_mac_tx_rate_val(mphy, vif, beacon, mcast);
	ht = FIELD_GET(MT_TX_RATE_MODE, rate) > MT_PHY_TYPE_OFDM;

	if (beacon && ht) {
		struct mt792x_dev *dev = mt792x_hw_dev(hw);

		/* must odd index */
		idx = MT7925_BEACON_RATES_TBL + 2 * (mvif->idx % 20);
		mt7925_mac_set_fixed_rate_table(dev, idx, rate);
		return idx;
	}

	idx = FIELD_GET(MT_TX_RATE_IDX, rate);
	for (i = 0; i < ARRAY_SIZE(mt76_rates); i++)
		if ((mt76_rates[i].hw_value & GENMASK(7, 0)) == idx)
			return MT792x_BASIC_RATES_TBL + i;

	return mvif->basic_rates_idx;
}

static void mt7925_bss_info_changed(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    struct ieee80211_bss_conf *info,
				    u64 changed)
{
	struct mt76_vif *mvif = (struct mt76_vif *)vif->drv_priv;
	struct mt792x_phy *phy = mt792x_hw_phy(hw);
	struct mt792x_dev *dev = mt792x_hw_dev(hw);

	mt792x_mutex_acquire(dev);

	if (changed & BSS_CHANGED_ERP_SLOT) {
		int slottime = info->use_short_slot ? 9 : 20;

		if (slottime != phy->slottime) {
			phy->slottime = slottime;
			mt792x_mac_set_timeing(phy);
		}
	}

	if (changed & BSS_CHANGED_MCAST_RATE)
		mvif->mcast_rates_idx =
				mt7925_get_rates_table(hw, vif, false, true);

	if (changed & BSS_CHANGED_BASIC_RATES)
		mvif->basic_rates_idx =
				mt7925_get_rates_table(hw, vif, false, false);

	if (changed & (BSS_CHANGED_BEACON |
		       BSS_CHANGED_BEACON_ENABLED)) {
		mvif->beacon_rates_idx =
				mt7925_get_rates_table(hw, vif, true, false);

		mt7925_mcu_uni_add_beacon_offload(dev, hw, vif,
						  info->enable_beacon);
	}

	/* ensure that enable txcmd_mode after bss_info */
	if (changed & (BSS_CHANGED_QOS | BSS_CHANGED_BEACON_ENABLED))
		mt7925_mcu_set_tx(dev, vif);

	if (changed & BSS_CHANGED_PS)
		mt7925_mcu_uni_bss_ps(dev, vif);

	if (changed & BSS_CHANGED_ASSOC) {
		mt7925_mcu_sta_update(dev, NULL, vif, true,
				      MT76_STA_INFO_STATE_ASSOC);
		mt7925_mcu_set_beacon_filter(dev, vif, vif->cfg.assoc);
	}

	if (changed & BSS_CHANGED_ARP_FILTER) {
		struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;

		mt7925_mcu_update_arp_filter(&dev->mt76, &mvif->mt76, info);
	}

	mt792x_mutex_release(dev);
}

int mt7925_mac_sta_add(struct mt76_dev *mdev, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta)
{
	struct mt792x_dev *dev = container_of(mdev, struct mt792x_dev, mt76);
	struct mt792x_sta *msta = (struct mt792x_sta *)sta->drv_priv;
	struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;
	int ret, idx;

	idx = mt76_wcid_alloc(dev->mt76.wcid_mask, MT792x_WTBL_STA - 1);
	if (idx < 0)
		return -ENOSPC;

	INIT_LIST_HEAD(&msta->wcid.poll_list);
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

	mt7925_mac_wtbl_update(dev, idx,
			       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);

	/* should update bss info before STA add */
	if (vif->type == NL80211_IFTYPE_STATION && !sta->tdls)
		mt7925_mcu_add_bss_info(&dev->phy, mvif->mt76.ctx, vif, sta,
					false);

	ret = mt7925_mcu_sta_update(dev, sta, vif, true,
				    MT76_STA_INFO_STATE_NONE);
	if (ret)
		return ret;

	mt76_connac_power_save_sched(&dev->mphy, &dev->pm);

	return 0;
}
EXPORT_SYMBOL_GPL(mt7925_mac_sta_add);

void mt7925_mac_sta_assoc(struct mt76_dev *mdev, struct ieee80211_vif *vif,
			  struct ieee80211_sta *sta)
{
	struct mt792x_dev *dev = container_of(mdev, struct mt792x_dev, mt76);
	struct mt792x_sta *msta = (struct mt792x_sta *)sta->drv_priv;
	struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;

	mt792x_mutex_acquire(dev);

	if (vif->type == NL80211_IFTYPE_STATION && !sta->tdls)
		mt7925_mcu_add_bss_info(&dev->phy, mvif->mt76.ctx, vif, sta,
					true);

	ewma_avg_signal_init(&msta->avg_ack_signal);

	mt7925_mac_wtbl_update(dev, msta->wcid.idx,
			       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);
	memset(msta->airtime_ac, 0, sizeof(msta->airtime_ac));

	mt7925_mcu_sta_update(dev, sta, vif, true, MT76_STA_INFO_STATE_ASSOC);

	mt792x_mutex_release(dev);
}
EXPORT_SYMBOL_GPL(mt7925_mac_sta_assoc);

void mt7925_mac_sta_remove(struct mt76_dev *mdev, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta)
{
	struct mt792x_dev *dev = container_of(mdev, struct mt792x_dev, mt76);
	struct mt792x_sta *msta = (struct mt792x_sta *)sta->drv_priv;

	mt76_connac_free_pending_tx_skbs(&dev->pm, &msta->wcid);
	mt76_connac_pm_wake(&dev->mphy, &dev->pm);

	mt7925_mcu_sta_update(dev, sta, vif, false, MT76_STA_INFO_STATE_NONE);
	mt7925_mac_wtbl_update(dev, msta->wcid.idx,
			       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);

	if (vif->type == NL80211_IFTYPE_STATION) {
		struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;

		mvif->wep_sta = NULL;
		ewma_rssi_init(&mvif->rssi);
		if (!sta->tdls)
			mt7925_mcu_add_bss_info(&dev->phy, mvif->mt76.ctx, vif, sta,
						false);
	}

	spin_lock_bh(&mdev->sta_poll_lock);
	if (!list_empty(&msta->wcid.poll_list))
		list_del_init(&msta->wcid.poll_list);
	spin_unlock_bh(&mdev->sta_poll_lock);

	mt76_connac_power_save_sched(&dev->mphy, &dev->pm);
}
EXPORT_SYMBOL_GPL(mt7925_mac_sta_remove);

static int mt7925_set_rts_threshold(struct ieee80211_hw *hw, u32 val)
{
	struct mt792x_dev *dev = mt792x_hw_dev(hw);

	mt792x_mutex_acquire(dev);
	mt7925_mcu_set_rts_thresh(&dev->phy, val);
	mt792x_mutex_release(dev);

	return 0;
}

static int
mt7925_ampdu_action(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		    struct ieee80211_ampdu_params *params)
{
	enum ieee80211_ampdu_mlme_action action = params->action;
	struct mt792x_dev *dev = mt792x_hw_dev(hw);
	struct ieee80211_sta *sta = params->sta;
	struct ieee80211_txq *txq = sta->txq[params->tid];
	struct mt792x_sta *msta = (struct mt792x_sta *)sta->drv_priv;
	u16 tid = params->tid;
	u16 ssn = params->ssn;
	struct mt76_txq *mtxq;
	int ret = 0;

	if (!txq)
		return -EINVAL;

	mtxq = (struct mt76_txq *)txq->drv_priv;

	mt792x_mutex_acquire(dev);
	switch (action) {
	case IEEE80211_AMPDU_RX_START:
		mt76_rx_aggr_start(&dev->mt76, &msta->wcid, tid, ssn,
				   params->buf_size);
		mt7925_mcu_uni_rx_ba(dev, params, true);
		break;
	case IEEE80211_AMPDU_RX_STOP:
		mt76_rx_aggr_stop(&dev->mt76, &msta->wcid, tid);
		mt7925_mcu_uni_rx_ba(dev, params, false);
		break;
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		mtxq->aggr = true;
		mtxq->send_bar = false;
		mt7925_mcu_uni_tx_ba(dev, params, true);
		break;
	case IEEE80211_AMPDU_TX_STOP_FLUSH:
	case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
		mtxq->aggr = false;
		clear_bit(tid, &msta->wcid.ampdu_state);
		mt7925_mcu_uni_tx_ba(dev, params, false);
		break;
	case IEEE80211_AMPDU_TX_START:
		set_bit(tid, &msta->wcid.ampdu_state);
		ret = IEEE80211_AMPDU_TX_START_IMMEDIATE;
		break;
	case IEEE80211_AMPDU_TX_STOP_CONT:
		mtxq->aggr = false;
		clear_bit(tid, &msta->wcid.ampdu_state);
		mt7925_mcu_uni_tx_ba(dev, params, false);
		ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		break;
	}
	mt792x_mutex_release(dev);

	return ret;
}

static bool is_valid_alpha2(const char *alpha2)
{
	if (!alpha2)
		return false;

	if (alpha2[0] == '0' && alpha2[1] == '0')
		return true;

	if (isalpha(alpha2[0]) && isalpha(alpha2[1]))
		return true;

	return false;
}

void mt7925_scan_work(struct work_struct *work)
{
	struct mt792x_phy *phy;

	phy = (struct mt792x_phy *)container_of(work, struct mt792x_phy,
						scan_work.work);

	while (true) {
		struct mt76_dev *mdev = &phy->dev->mt76;
		struct sk_buff *skb;
		struct tlv *tlv;
		int tlv_len;

		spin_lock_bh(&phy->dev->mt76.lock);
		skb = __skb_dequeue(&phy->scan_event_list);
		spin_unlock_bh(&phy->dev->mt76.lock);

		if (!skb)
			break;

		skb_pull(skb, sizeof(struct mt7925_mcu_rxd) + 4);
		tlv = (struct tlv *)skb->data;
		tlv_len = skb->len;

		while (tlv_len > 0 && le16_to_cpu(tlv->len) <= tlv_len) {
			struct mt7925_mcu_scan_chinfo_event *evt;

			switch (le16_to_cpu(tlv->tag)) {
			case UNI_EVENT_SCAN_DONE_BASIC:
				if (test_and_clear_bit(MT76_HW_SCANNING, &phy->mt76->state)) {
					struct cfg80211_scan_info info = {
						.aborted = false,
					};
					ieee80211_scan_completed(phy->mt76->hw, &info);
				}
				break;
			case UNI_EVENT_SCAN_DONE_CHNLINFO:
				evt = (struct mt7925_mcu_scan_chinfo_event *)tlv->data;

				if (!is_valid_alpha2(evt->alpha2))
					break;

				if (mdev->alpha2[0] != '0' && mdev->alpha2[1] != '0')
					break;

				mt7925_mcu_set_clc(phy->dev, evt->alpha2, ENVIRON_INDOOR);

				break;
			case UNI_EVENT_SCAN_DONE_NLO:
				ieee80211_sched_scan_results(phy->mt76->hw);
				break;
			default:
				break;
			}

			tlv_len -= le16_to_cpu(tlv->len);
			tlv = (struct tlv *)((char *)(tlv) + le16_to_cpu(tlv->len));
		}

		dev_kfree_skb(skb);
	}
}

static int
mt7925_hw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	       struct ieee80211_scan_request *req)
{
	struct mt792x_dev *dev = mt792x_hw_dev(hw);
	struct mt76_phy *mphy = hw->priv;
	int err;

	mt792x_mutex_acquire(dev);
	err = mt7925_mcu_hw_scan(mphy, vif, req);
	mt792x_mutex_release(dev);

	return err;
}

static void
mt7925_cancel_hw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct mt792x_dev *dev = mt792x_hw_dev(hw);
	struct mt76_phy *mphy = hw->priv;

	mt792x_mutex_acquire(dev);
	mt7925_mcu_cancel_hw_scan(mphy, vif);
	mt792x_mutex_release(dev);
}

static int
mt7925_start_sched_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			struct cfg80211_sched_scan_request *req,
			struct ieee80211_scan_ies *ies)
{
	struct mt792x_dev *dev = mt792x_hw_dev(hw);
	struct mt76_phy *mphy = hw->priv;
	int err;

	mt792x_mutex_acquire(dev);

	err = mt7925_mcu_sched_scan_req(mphy, vif, req);
	if (err < 0)
		goto out;

	err = mt7925_mcu_sched_scan_enable(mphy, vif, true);
out:
	mt792x_mutex_release(dev);

	return err;
}

static int
mt7925_stop_sched_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct mt792x_dev *dev = mt792x_hw_dev(hw);
	struct mt76_phy *mphy = hw->priv;
	int err;

	mt792x_mutex_acquire(dev);
	err = mt7925_mcu_sched_scan_enable(mphy, vif, false);
	mt792x_mutex_release(dev);

	return err;
}

static int
mt7925_set_antenna(struct ieee80211_hw *hw, u32 tx_ant, u32 rx_ant)
{
	struct mt792x_dev *dev = mt792x_hw_dev(hw);
	struct mt792x_phy *phy = mt792x_hw_phy(hw);
	int max_nss = hweight8(hw->wiphy->available_antennas_tx);

	if (!tx_ant || tx_ant != rx_ant || ffs(tx_ant) > max_nss)
		return -EINVAL;

	if ((BIT(hweight8(tx_ant)) - 1) != tx_ant)
		tx_ant = BIT(ffs(tx_ant) - 1) - 1;

	mt792x_mutex_acquire(dev);

	phy->mt76->antenna_mask = tx_ant;
	phy->mt76->chainmask = tx_ant;

	mt76_set_stream_caps(phy->mt76, true);
	mt7925_set_stream_he_eht_caps(phy);

	/* TODO: update bmc_wtbl spe_idx when antenna changes */
	mt792x_mutex_release(dev);

	return 0;
}

#ifdef CONFIG_PM
static int mt7925_suspend(struct ieee80211_hw *hw,
			  struct cfg80211_wowlan *wowlan)
{
	struct mt792x_dev *dev = mt792x_hw_dev(hw);
	struct mt792x_phy *phy = mt792x_hw_phy(hw);

	cancel_delayed_work_sync(&phy->scan_work);
	cancel_delayed_work_sync(&phy->mt76->mac_work);

	cancel_delayed_work_sync(&dev->pm.ps_work);
	mt76_connac_free_pending_tx_skbs(&dev->pm, NULL);

	mt792x_mutex_acquire(dev);

	clear_bit(MT76_STATE_RUNNING, &phy->mt76->state);
	ieee80211_iterate_active_interfaces(hw,
					    IEEE80211_IFACE_ITER_RESUME_ALL,
					    mt7925_mcu_set_suspend_iter,
					    &dev->mphy);

	mt792x_mutex_release(dev);

	return 0;
}

static int mt7925_resume(struct ieee80211_hw *hw)
{
	struct mt792x_dev *dev = mt792x_hw_dev(hw);
	struct mt792x_phy *phy = mt792x_hw_phy(hw);

	mt792x_mutex_acquire(dev);

	set_bit(MT76_STATE_RUNNING, &phy->mt76->state);
	ieee80211_iterate_active_interfaces(hw,
					    IEEE80211_IFACE_ITER_RESUME_ALL,
					    mt7925_mcu_set_suspend_iter,
					    &dev->mphy);

	ieee80211_queue_delayed_work(hw, &phy->mt76->mac_work,
				     MT792x_WATCHDOG_TIME);

	mt792x_mutex_release(dev);

	return 0;
}

static void mt7925_set_rekey_data(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct cfg80211_gtk_rekey_data *data)
{
	struct mt792x_dev *dev = mt792x_hw_dev(hw);

	mt792x_mutex_acquire(dev);
	mt76_connac_mcu_update_gtk_rekey(hw, vif, data);
	mt792x_mutex_release(dev);
}
#endif /* CONFIG_PM */

static void mt7925_sta_set_decap_offload(struct ieee80211_hw *hw,
					 struct ieee80211_vif *vif,
					 struct ieee80211_sta *sta,
					 bool enabled)
{
	struct mt792x_sta *msta = (struct mt792x_sta *)sta->drv_priv;
	struct mt792x_dev *dev = mt792x_hw_dev(hw);

	mt792x_mutex_acquire(dev);

	if (enabled)
		set_bit(MT_WCID_FLAG_HDR_TRANS, &msta->wcid.flags);
	else
		clear_bit(MT_WCID_FLAG_HDR_TRANS, &msta->wcid.flags);

	mt7925_mcu_wtbl_update_hdr_trans(dev, vif, sta);

	mt792x_mutex_release(dev);
}

#if IS_ENABLED(CONFIG_IPV6)
static void mt7925_ipv6_addr_change(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    struct inet6_dev *idev)
{
	struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;
	struct mt792x_dev *dev = mvif->phy->dev;
	struct inet6_ifaddr *ifa;
	struct sk_buff *skb;
	u8 idx = 0;

	struct {
		struct {
			u8 bss_idx;
			u8 pad[3];
		} __packed hdr;
		struct mt7925_arpns_tlv arpns;
		struct in6_addr ns_addrs[IEEE80211_BSS_ARP_ADDR_LIST_LEN];
	} req_hdr = {
		.hdr = {
			.bss_idx = mvif->mt76.idx,
		},
		.arpns = {
			.tag = cpu_to_le16(UNI_OFFLOAD_OFFLOAD_ND),
			.len = cpu_to_le16(sizeof(req_hdr) - 4),
			.enable = true,
		},
	};

	read_lock_bh(&idev->lock);
	list_for_each_entry(ifa, &idev->addr_list, if_list) {
		if (ifa->flags & IFA_F_TENTATIVE)
			continue;
		req_hdr.ns_addrs[idx] = ifa->addr;
		if (++idx >= IEEE80211_BSS_ARP_ADDR_LIST_LEN)
			break;
	}
	read_unlock_bh(&idev->lock);

	if (!idx)
		return;

	req_hdr.arpns.ips_num = idx;

	skb = __mt76_mcu_msg_alloc(&dev->mt76, NULL, sizeof(req_hdr),
				   0, GFP_ATOMIC);
	if (!skb)
		return;

	skb_put_data(skb, &req_hdr, sizeof(req_hdr));

	skb_queue_tail(&dev->ipv6_ns_list, skb);

	ieee80211_queue_work(dev->mt76.hw, &dev->ipv6_ns_work);
}
#endif

int mt7925_set_tx_sar_pwr(struct ieee80211_hw *hw,
			  const struct cfg80211_sar_specs *sar)
{
	struct mt76_phy *mphy = hw->priv;

	if (sar) {
		int err = mt76_init_sar_power(hw, sar);

		if (err)
			return err;
	}
	mt792x_init_acpi_sar_power(mt792x_hw_phy(hw), !sar);

	return mt7925_mcu_set_rate_txpower(mphy);
}

static int mt7925_set_sar_specs(struct ieee80211_hw *hw,
				const struct cfg80211_sar_specs *sar)
{
	struct mt792x_dev *dev = mt792x_hw_dev(hw);
	int err;

	mt792x_mutex_acquire(dev);
	err = mt7925_mcu_set_clc(dev, dev->mt76.alpha2,
				 dev->country_ie_env);
	if (err < 0)
		goto out;

	err = mt7925_set_tx_sar_pwr(hw, sar);
out:
	mt792x_mutex_release(dev);

	return err;
}

static void
mt7925_channel_switch_beacon(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct cfg80211_chan_def *chandef)
{
	struct mt792x_dev *dev = mt792x_hw_dev(hw);

	mt792x_mutex_acquire(dev);
	mt7925_mcu_uni_add_beacon_offload(dev, hw, vif, true);
	mt792x_mutex_release(dev);
}

static int
mt7925_start_ap(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		struct ieee80211_bss_conf *link_conf)
{
	struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;
	struct mt792x_dev *dev = mt792x_hw_dev(hw);
	int err;

	mt792x_mutex_acquire(dev);

	err = mt7925_mcu_add_bss_info(&dev->phy, mvif->mt76.ctx, vif, NULL,
				      true);
	if (err)
		goto out;

	err = mt7925_mcu_set_bss_pm(dev, vif, true);
	if (err)
		goto out;

	err = mt7925_mcu_sta_update(dev, NULL, vif, true,
				    MT76_STA_INFO_STATE_NONE);
out:
	mt792x_mutex_release(dev);

	return err;
}

static void
mt7925_stop_ap(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	       struct ieee80211_bss_conf *link_conf)
{
	struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;
	struct mt792x_dev *dev = mt792x_hw_dev(hw);
	int err;

	mt792x_mutex_acquire(dev);

	err = mt7925_mcu_set_bss_pm(dev, vif, false);
	if (err)
		goto out;

	mt7925_mcu_add_bss_info(&dev->phy, mvif->mt76.ctx, vif, NULL,
				false);

out:
	mt792x_mutex_release(dev);
}

static int
mt7925_add_chanctx(struct ieee80211_hw *hw,
		   struct ieee80211_chanctx_conf *ctx)
{
	return 0;
}

static void
mt7925_remove_chanctx(struct ieee80211_hw *hw,
		      struct ieee80211_chanctx_conf *ctx)
{
}

static void mt7925_ctx_iter(void *priv, u8 *mac,
			    struct ieee80211_vif *vif)
{
	struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;
	struct ieee80211_chanctx_conf *ctx = priv;

	if (ctx != mvif->mt76.ctx)
		return;

	if (vif->type == NL80211_IFTYPE_MONITOR) {
		mt7925_mcu_set_sniffer(mvif->phy->dev, vif, true);
		mt7925_mcu_config_sniffer(mvif, ctx);
	} else {
		mt7925_mcu_set_chctx(mvif->phy->mt76, &mvif->mt76, ctx);
	}
}

static void
mt7925_change_chanctx(struct ieee80211_hw *hw,
		      struct ieee80211_chanctx_conf *ctx,
		      u32 changed)
{
	struct mt792x_phy *phy = mt792x_hw_phy(hw);

	mt792x_mutex_acquire(phy->dev);
	ieee80211_iterate_active_interfaces(phy->mt76->hw,
					    IEEE80211_IFACE_ITER_ACTIVE,
					    mt7925_ctx_iter, ctx);
	mt792x_mutex_release(phy->dev);
}

static void mt7925_mgd_prepare_tx(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct ieee80211_prep_tx_info *info)
{
	struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;
	struct mt792x_dev *dev = mt792x_hw_dev(hw);
	u16 duration = info->duration ? info->duration :
		       jiffies_to_msecs(HZ);

	mt792x_mutex_acquire(dev);
	mt7925_set_roc(mvif->phy, mvif, mvif->mt76.ctx->def.chan, duration,
		       MT7925_ROC_REQ_JOIN);
	mt792x_mutex_release(dev);
}

static void mt7925_mgd_complete_tx(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   struct ieee80211_prep_tx_info *info)
{
	struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;

	mt7925_abort_roc(mvif->phy, mvif);
}

const struct ieee80211_ops mt7925_ops = {
	.tx = mt792x_tx,
	.start = mt7925_start,
	.stop = mt792x_stop,
	.add_interface = mt7925_add_interface,
	.remove_interface = mt792x_remove_interface,
	.config = mt7925_config,
	.conf_tx = mt792x_conf_tx,
	.configure_filter = mt7925_configure_filter,
	.bss_info_changed = mt7925_bss_info_changed,
	.start_ap = mt7925_start_ap,
	.stop_ap = mt7925_stop_ap,
	.sta_state = mt76_sta_state,
	.sta_pre_rcu_remove = mt76_sta_pre_rcu_remove,
	.set_key = mt7925_set_key,
	.sta_set_decap_offload = mt7925_sta_set_decap_offload,
#if IS_ENABLED(CONFIG_IPV6)
	.ipv6_addr_change = mt7925_ipv6_addr_change,
#endif /* CONFIG_IPV6 */
	.ampdu_action = mt7925_ampdu_action,
	.set_rts_threshold = mt7925_set_rts_threshold,
	.wake_tx_queue = mt76_wake_tx_queue,
	.release_buffered_frames = mt76_release_buffered_frames,
	.channel_switch_beacon = mt7925_channel_switch_beacon,
	.get_txpower = mt76_get_txpower,
	.get_stats = mt792x_get_stats,
	.get_et_sset_count = mt792x_get_et_sset_count,
	.get_et_strings = mt792x_get_et_strings,
	.get_et_stats = mt792x_get_et_stats,
	.get_tsf = mt792x_get_tsf,
	.set_tsf = mt792x_set_tsf,
	.get_survey = mt76_get_survey,
	.get_antenna = mt76_get_antenna,
	.set_antenna = mt7925_set_antenna,
	.set_coverage_class = mt792x_set_coverage_class,
	.hw_scan = mt7925_hw_scan,
	.cancel_hw_scan = mt7925_cancel_hw_scan,
	.sta_statistics = mt792x_sta_statistics,
	.sched_scan_start = mt7925_start_sched_scan,
	.sched_scan_stop = mt7925_stop_sched_scan,
#ifdef CONFIG_PM
	.suspend = mt7925_suspend,
	.resume = mt7925_resume,
	.set_wakeup = mt792x_set_wakeup,
	.set_rekey_data = mt7925_set_rekey_data,
#endif /* CONFIG_PM */
	.flush = mt792x_flush,
	.set_sar_specs = mt7925_set_sar_specs,
	.remain_on_channel = mt7925_remain_on_channel,
	.cancel_remain_on_channel = mt7925_cancel_remain_on_channel,
	.add_chanctx = mt7925_add_chanctx,
	.remove_chanctx = mt7925_remove_chanctx,
	.change_chanctx = mt7925_change_chanctx,
	.assign_vif_chanctx = mt792x_assign_vif_chanctx,
	.unassign_vif_chanctx = mt792x_unassign_vif_chanctx,
	.mgd_prepare_tx = mt7925_mgd_prepare_tx,
	.mgd_complete_tx = mt7925_mgd_complete_tx,
};
EXPORT_SYMBOL_GPL(mt7925_ops);

MODULE_AUTHOR("Deren Wu <deren.wu@mediatek.com>");
MODULE_DESCRIPTION("MediaTek MT7925 core driver");
MODULE_LICENSE("Dual BSD/GPL");
