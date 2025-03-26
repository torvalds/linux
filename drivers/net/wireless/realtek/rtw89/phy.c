// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#include "acpi.h"
#include "chan.h"
#include "coex.h"
#include "debug.h"
#include "fw.h"
#include "mac.h"
#include "phy.h"
#include "ps.h"
#include "reg.h"
#include "sar.h"
#include "txrx.h"
#include "util.h"

static u32 rtw89_phy0_phy1_offset(struct rtw89_dev *rtwdev, u32 addr)
{
	const struct rtw89_phy_gen_def *phy = rtwdev->chip->phy_def;

	return phy->phy0_phy1_offset(rtwdev, addr);
}

static u16 get_max_amsdu_len(struct rtw89_dev *rtwdev,
			     const struct rtw89_ra_report *report)
{
	u32 bit_rate = report->bit_rate;

	/* lower than ofdm, do not aggregate */
	if (bit_rate < 550)
		return 1;

	/* avoid AMSDU for legacy rate */
	if (report->might_fallback_legacy)
		return 1;

	/* lower than 20M vht 2ss mcs8, make it small */
	if (bit_rate < 1800)
		return 1200;

	/* lower than 40M vht 2ss mcs9, make it medium */
	if (bit_rate < 4000)
		return 2600;

	/* not yet 80M vht 2ss mcs8/9, make it twice regular packet size */
	if (bit_rate < 7000)
		return 3500;

	return rtwdev->chip->max_amsdu_limit;
}

static u64 get_mcs_ra_mask(u16 mcs_map, u8 highest_mcs, u8 gap)
{
	u64 ra_mask = 0;
	u8 mcs_cap;
	int i, nss;

	for (i = 0, nss = 12; i < 4; i++, mcs_map >>= 2, nss += 12) {
		mcs_cap = mcs_map & 0x3;
		switch (mcs_cap) {
		case 2:
			ra_mask |= GENMASK_ULL(highest_mcs, 0) << nss;
			break;
		case 1:
			ra_mask |= GENMASK_ULL(highest_mcs - gap, 0) << nss;
			break;
		case 0:
			ra_mask |= GENMASK_ULL(highest_mcs - gap * 2, 0) << nss;
			break;
		default:
			break;
		}
	}

	return ra_mask;
}

static u64 get_he_ra_mask(struct ieee80211_link_sta *link_sta)
{
	struct ieee80211_sta_he_cap cap = link_sta->he_cap;
	u16 mcs_map;

	switch (link_sta->bandwidth) {
	case IEEE80211_STA_RX_BW_160:
		if (cap.he_cap_elem.phy_cap_info[0] &
		    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G)
			mcs_map = le16_to_cpu(cap.he_mcs_nss_supp.rx_mcs_80p80);
		else
			mcs_map = le16_to_cpu(cap.he_mcs_nss_supp.rx_mcs_160);
		break;
	default:
		mcs_map = le16_to_cpu(cap.he_mcs_nss_supp.rx_mcs_80);
	}

	/* MCS11, MCS9, MCS7 */
	return get_mcs_ra_mask(mcs_map, 11, 2);
}

static u64 get_eht_mcs_ra_mask(u8 *max_nss, u8 start_mcs, u8 n_nss)
{
	u64 nss_mcs_shift;
	u64 nss_mcs_val;
	u64 mask = 0;
	int i, j;
	u8 nss;

	for (i = 0; i < n_nss; i++) {
		nss = u8_get_bits(max_nss[i], IEEE80211_EHT_MCS_NSS_RX);
		if (!nss)
			continue;

		nss_mcs_val = GENMASK_ULL(start_mcs + i * 2, 0);

		for (j = 0, nss_mcs_shift = 12; j < nss; j++, nss_mcs_shift += 16)
			mask |= nss_mcs_val << nss_mcs_shift;
	}

	return mask;
}

static u64 get_eht_ra_mask(struct ieee80211_link_sta *link_sta)
{
	struct ieee80211_sta_eht_cap *eht_cap = &link_sta->eht_cap;
	struct ieee80211_eht_mcs_nss_supp_20mhz_only *mcs_nss_20mhz;
	struct ieee80211_eht_mcs_nss_supp_bw *mcs_nss;
	u8 *he_phy_cap = link_sta->he_cap.he_cap_elem.phy_cap_info;

	switch (link_sta->bandwidth) {
	case IEEE80211_STA_RX_BW_320:
		mcs_nss = &eht_cap->eht_mcs_nss_supp.bw._320;
		/* MCS 9, 11, 13 */
		return get_eht_mcs_ra_mask(mcs_nss->rx_tx_max_nss, 9, 3);
	case IEEE80211_STA_RX_BW_160:
		mcs_nss = &eht_cap->eht_mcs_nss_supp.bw._160;
		/* MCS 9, 11, 13 */
		return get_eht_mcs_ra_mask(mcs_nss->rx_tx_max_nss, 9, 3);
	case IEEE80211_STA_RX_BW_20:
		if (!(he_phy_cap[0] &
		      IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_MASK_ALL)) {
			mcs_nss_20mhz = &eht_cap->eht_mcs_nss_supp.only_20mhz;
			/* MCS 7, 9, 11, 13 */
			return get_eht_mcs_ra_mask(mcs_nss_20mhz->rx_tx_max_nss, 7, 4);
		}
		fallthrough;
	case IEEE80211_STA_RX_BW_80:
	default:
		mcs_nss = &eht_cap->eht_mcs_nss_supp.bw._80;
		/* MCS 9, 11, 13 */
		return get_eht_mcs_ra_mask(mcs_nss->rx_tx_max_nss, 9, 3);
	}
}

#define RA_FLOOR_TABLE_SIZE	7
#define RA_FLOOR_UP_GAP		3
static u64 rtw89_phy_ra_mask_rssi(struct rtw89_dev *rtwdev, u8 rssi,
				  u8 ratr_state)
{
	u8 rssi_lv_t[RA_FLOOR_TABLE_SIZE] = {30, 44, 48, 52, 56, 60, 100};
	u8 rssi_lv = 0;
	u8 i;

	rssi >>= 1;
	for (i = 0; i < RA_FLOOR_TABLE_SIZE; i++) {
		if (i >= ratr_state)
			rssi_lv_t[i] += RA_FLOOR_UP_GAP;
		if (rssi < rssi_lv_t[i]) {
			rssi_lv = i;
			break;
		}
	}
	if (rssi_lv == 0)
		return 0xffffffffffffffffULL;
	else if (rssi_lv == 1)
		return 0xfffffffffffffff0ULL;
	else if (rssi_lv == 2)
		return 0xffffffffffffefe0ULL;
	else if (rssi_lv == 3)
		return 0xffffffffffffcfc0ULL;
	else if (rssi_lv == 4)
		return 0xffffffffffff8f80ULL;
	else if (rssi_lv >= 5)
		return 0xffffffffffff0f00ULL;

	return 0xffffffffffffffffULL;
}

static u64 rtw89_phy_ra_mask_recover(u64 ra_mask, u64 ra_mask_bak)
{
	if ((ra_mask & ~(RA_MASK_CCK_RATES | RA_MASK_OFDM_RATES)) == 0)
		ra_mask |= (ra_mask_bak & ~(RA_MASK_CCK_RATES | RA_MASK_OFDM_RATES));

	if (ra_mask == 0)
		ra_mask |= (ra_mask_bak & (RA_MASK_CCK_RATES | RA_MASK_OFDM_RATES));

	return ra_mask;
}

static u64 rtw89_phy_ra_mask_cfg(struct rtw89_dev *rtwdev,
				 struct rtw89_sta_link *rtwsta_link,
				 struct ieee80211_link_sta *link_sta,
				 const struct rtw89_chan *chan)
{
	struct cfg80211_bitrate_mask *mask = &rtwsta_link->mask;
	enum nl80211_band band;
	u64 cfg_mask;

	if (!rtwsta_link->use_cfg_mask)
		return -1;

	switch (chan->band_type) {
	case RTW89_BAND_2G:
		band = NL80211_BAND_2GHZ;
		cfg_mask = u64_encode_bits(mask->control[NL80211_BAND_2GHZ].legacy,
					   RA_MASK_CCK_RATES | RA_MASK_OFDM_RATES);
		break;
	case RTW89_BAND_5G:
		band = NL80211_BAND_5GHZ;
		cfg_mask = u64_encode_bits(mask->control[NL80211_BAND_5GHZ].legacy,
					   RA_MASK_OFDM_RATES);
		break;
	case RTW89_BAND_6G:
		band = NL80211_BAND_6GHZ;
		cfg_mask = u64_encode_bits(mask->control[NL80211_BAND_6GHZ].legacy,
					   RA_MASK_OFDM_RATES);
		break;
	default:
		rtw89_warn(rtwdev, "unhandled band type %d\n", chan->band_type);
		return -1;
	}

	if (link_sta->he_cap.has_he) {
		cfg_mask |= u64_encode_bits(mask->control[band].he_mcs[0],
					    RA_MASK_HE_1SS_RATES);
		cfg_mask |= u64_encode_bits(mask->control[band].he_mcs[1],
					    RA_MASK_HE_2SS_RATES);
	} else if (link_sta->vht_cap.vht_supported) {
		cfg_mask |= u64_encode_bits(mask->control[band].vht_mcs[0],
					    RA_MASK_VHT_1SS_RATES);
		cfg_mask |= u64_encode_bits(mask->control[band].vht_mcs[1],
					    RA_MASK_VHT_2SS_RATES);
	} else if (link_sta->ht_cap.ht_supported) {
		cfg_mask |= u64_encode_bits(mask->control[band].ht_mcs[0],
					    RA_MASK_HT_1SS_RATES);
		cfg_mask |= u64_encode_bits(mask->control[band].ht_mcs[1],
					    RA_MASK_HT_2SS_RATES);
	}

	return cfg_mask;
}

static const u64
rtw89_ra_mask_ht_rates[4] = {RA_MASK_HT_1SS_RATES, RA_MASK_HT_2SS_RATES,
			     RA_MASK_HT_3SS_RATES, RA_MASK_HT_4SS_RATES};
static const u64
rtw89_ra_mask_vht_rates[4] = {RA_MASK_VHT_1SS_RATES, RA_MASK_VHT_2SS_RATES,
			      RA_MASK_VHT_3SS_RATES, RA_MASK_VHT_4SS_RATES};
static const u64
rtw89_ra_mask_he_rates[4] = {RA_MASK_HE_1SS_RATES, RA_MASK_HE_2SS_RATES,
			     RA_MASK_HE_3SS_RATES, RA_MASK_HE_4SS_RATES};
static const u64
rtw89_ra_mask_eht_rates[4] = {RA_MASK_EHT_1SS_RATES, RA_MASK_EHT_2SS_RATES,
			      RA_MASK_EHT_3SS_RATES, RA_MASK_EHT_4SS_RATES};
static const u64
rtw89_ra_mask_eht_mcs0_11[4] = {RA_MASK_EHT_1SS_MCS0_11, RA_MASK_EHT_2SS_MCS0_11,
				RA_MASK_EHT_3SS_MCS0_11, RA_MASK_EHT_4SS_MCS0_11};

static void rtw89_phy_ra_gi_ltf(struct rtw89_dev *rtwdev,
				struct rtw89_sta_link *rtwsta_link,
				struct ieee80211_link_sta *link_sta,
				const struct rtw89_chan *chan,
				bool *fix_giltf_en, u8 *fix_giltf)
{
	struct cfg80211_bitrate_mask *mask = &rtwsta_link->mask;
	u8 band = chan->band_type;
	enum nl80211_band nl_band = rtw89_hw_to_nl80211_band(band);
	u8 he_ltf = mask->control[nl_band].he_ltf;
	u8 he_gi = mask->control[nl_band].he_gi;

	*fix_giltf_en = true;

	if (rtwdev->chip->chip_id == RTL8852C &&
	    chan->band_width == RTW89_CHANNEL_WIDTH_160 &&
	    rtw89_sta_link_has_su_mu_4xhe08(link_sta))
		*fix_giltf = RTW89_GILTF_SGI_4XHE08;
	else
		*fix_giltf = RTW89_GILTF_2XHE08;

	if (!(rtwsta_link->use_cfg_mask && link_sta->he_cap.has_he))
		return;

	if (he_ltf == 2 && he_gi == 2) {
		*fix_giltf = RTW89_GILTF_LGI_4XHE32;
	} else if (he_ltf == 2 && he_gi == 0) {
		*fix_giltf = RTW89_GILTF_SGI_4XHE08;
	} else if (he_ltf == 1 && he_gi == 1) {
		*fix_giltf = RTW89_GILTF_2XHE16;
	} else if (he_ltf == 1 && he_gi == 0) {
		*fix_giltf = RTW89_GILTF_2XHE08;
	} else if (he_ltf == 0 && he_gi == 1) {
		*fix_giltf = RTW89_GILTF_1XHE16;
	} else if (he_ltf == 0 && he_gi == 0) {
		*fix_giltf = RTW89_GILTF_1XHE08;
	}
}

static void rtw89_phy_ra_sta_update(struct rtw89_dev *rtwdev,
				    struct rtw89_vif_link *rtwvif_link,
				    struct rtw89_sta_link *rtwsta_link,
				    struct ieee80211_link_sta *link_sta,
				    bool p2p, bool csi)
{
	struct rtw89_phy_rate_pattern *rate_pattern = &rtwvif_link->rate_pattern;
	struct rtw89_ra_info *ra = &rtwsta_link->ra;
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev,
						       rtwvif_link->chanctx_idx);
	const u64 *high_rate_masks = rtw89_ra_mask_ht_rates;
	u8 rssi = ewma_rssi_read(&rtwsta_link->avg_rssi);
	u64 ra_mask = 0;
	u64 ra_mask_bak;
	u8 mode = 0;
	u8 csi_mode = RTW89_RA_RPT_MODE_LEGACY;
	u8 bw_mode = 0;
	u8 stbc_en = 0;
	u8 ldpc_en = 0;
	u8 fix_giltf = 0;
	u8 i;
	bool sgi = false;
	bool fix_giltf_en = false;

	memset(ra, 0, sizeof(*ra));
	/* Set the ra mask from sta's capability */
	if (link_sta->eht_cap.has_eht) {
		mode |= RTW89_RA_MODE_EHT;
		ra_mask |= get_eht_ra_mask(link_sta);

		if (rtwdev->hal.no_mcs_12_13)
			high_rate_masks = rtw89_ra_mask_eht_mcs0_11;
		else
			high_rate_masks = rtw89_ra_mask_eht_rates;

		rtw89_phy_ra_gi_ltf(rtwdev, rtwsta_link, link_sta,
				    chan, &fix_giltf_en, &fix_giltf);
	} else if (link_sta->he_cap.has_he) {
		mode |= RTW89_RA_MODE_HE;
		csi_mode = RTW89_RA_RPT_MODE_HE;
		ra_mask |= get_he_ra_mask(link_sta);
		high_rate_masks = rtw89_ra_mask_he_rates;
		if (link_sta->he_cap.he_cap_elem.phy_cap_info[2] &
		    IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ)
			stbc_en = 1;
		if (link_sta->he_cap.he_cap_elem.phy_cap_info[1] &
		    IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD)
			ldpc_en = 1;
		rtw89_phy_ra_gi_ltf(rtwdev, rtwsta_link, link_sta,
				    chan, &fix_giltf_en, &fix_giltf);
	} else if (link_sta->vht_cap.vht_supported) {
		u16 mcs_map = le16_to_cpu(link_sta->vht_cap.vht_mcs.rx_mcs_map);

		mode |= RTW89_RA_MODE_VHT;
		csi_mode = RTW89_RA_RPT_MODE_VHT;
		/* MCS9 (non-20MHz), MCS8, MCS7 */
		if (link_sta->bandwidth == IEEE80211_STA_RX_BW_20)
			ra_mask |= get_mcs_ra_mask(mcs_map, 8, 1);
		else
			ra_mask |= get_mcs_ra_mask(mcs_map, 9, 1);
		high_rate_masks = rtw89_ra_mask_vht_rates;
		if (link_sta->vht_cap.cap & IEEE80211_VHT_CAP_RXSTBC_MASK)
			stbc_en = 1;
		if (link_sta->vht_cap.cap & IEEE80211_VHT_CAP_RXLDPC)
			ldpc_en = 1;
	} else if (link_sta->ht_cap.ht_supported) {
		mode |= RTW89_RA_MODE_HT;
		csi_mode = RTW89_RA_RPT_MODE_HT;
		ra_mask |= ((u64)link_sta->ht_cap.mcs.rx_mask[3] << 48) |
			   ((u64)link_sta->ht_cap.mcs.rx_mask[2] << 36) |
			   ((u64)link_sta->ht_cap.mcs.rx_mask[1] << 24) |
			   ((u64)link_sta->ht_cap.mcs.rx_mask[0] << 12);
		high_rate_masks = rtw89_ra_mask_ht_rates;
		if (link_sta->ht_cap.cap & IEEE80211_HT_CAP_RX_STBC)
			stbc_en = 1;
		if (link_sta->ht_cap.cap & IEEE80211_HT_CAP_LDPC_CODING)
			ldpc_en = 1;
	}

	switch (chan->band_type) {
	case RTW89_BAND_2G:
		ra_mask |= link_sta->supp_rates[NL80211_BAND_2GHZ];
		if (link_sta->supp_rates[NL80211_BAND_2GHZ] & 0xf)
			mode |= RTW89_RA_MODE_CCK;
		if (link_sta->supp_rates[NL80211_BAND_2GHZ] & 0xff0)
			mode |= RTW89_RA_MODE_OFDM;
		break;
	case RTW89_BAND_5G:
		ra_mask |= (u64)link_sta->supp_rates[NL80211_BAND_5GHZ] << 4;
		mode |= RTW89_RA_MODE_OFDM;
		break;
	case RTW89_BAND_6G:
		ra_mask |= (u64)link_sta->supp_rates[NL80211_BAND_6GHZ] << 4;
		mode |= RTW89_RA_MODE_OFDM;
		break;
	default:
		rtw89_err(rtwdev, "Unknown band type\n");
		break;
	}

	ra_mask_bak = ra_mask;

	if (mode >= RTW89_RA_MODE_HT) {
		u64 mask = 0;
		for (i = 0; i < rtwdev->hal.tx_nss; i++)
			mask |= high_rate_masks[i];
		if (mode & RTW89_RA_MODE_OFDM)
			mask |= RA_MASK_SUBOFDM_RATES;
		if (mode & RTW89_RA_MODE_CCK)
			mask |= RA_MASK_SUBCCK_RATES;
		ra_mask &= mask;
	} else if (mode & RTW89_RA_MODE_OFDM) {
		ra_mask &= (RA_MASK_OFDM_RATES | RA_MASK_SUBCCK_RATES);
	}

	if (mode != RTW89_RA_MODE_CCK)
		ra_mask &= rtw89_phy_ra_mask_rssi(rtwdev, rssi, 0);

	ra_mask = rtw89_phy_ra_mask_recover(ra_mask, ra_mask_bak);
	ra_mask &= rtw89_phy_ra_mask_cfg(rtwdev, rtwsta_link, link_sta, chan);

	switch (link_sta->bandwidth) {
	case IEEE80211_STA_RX_BW_160:
		bw_mode = RTW89_CHANNEL_WIDTH_160;
		sgi = link_sta->vht_cap.vht_supported &&
		      (link_sta->vht_cap.cap & IEEE80211_VHT_CAP_SHORT_GI_160);
		break;
	case IEEE80211_STA_RX_BW_80:
		bw_mode = RTW89_CHANNEL_WIDTH_80;
		sgi = link_sta->vht_cap.vht_supported &&
		      (link_sta->vht_cap.cap & IEEE80211_VHT_CAP_SHORT_GI_80);
		break;
	case IEEE80211_STA_RX_BW_40:
		bw_mode = RTW89_CHANNEL_WIDTH_40;
		sgi = link_sta->ht_cap.ht_supported &&
		      (link_sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_40);
		break;
	default:
		bw_mode = RTW89_CHANNEL_WIDTH_20;
		sgi = link_sta->ht_cap.ht_supported &&
		      (link_sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_20);
		break;
	}

	if (link_sta->he_cap.he_cap_elem.phy_cap_info[3] &
	    IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_16_QAM)
		ra->dcm_cap = 1;

	if (rate_pattern->enable && !p2p) {
		ra_mask = rtw89_phy_ra_mask_cfg(rtwdev, rtwsta_link, link_sta, chan);
		ra_mask &= rate_pattern->ra_mask;
		mode = rate_pattern->ra_mode;
	}

	ra->bw_cap = bw_mode;
	ra->er_cap = rtwsta_link->er_cap;
	ra->mode_ctrl = mode;
	ra->macid = rtwsta_link->mac_id;
	ra->stbc_cap = stbc_en;
	ra->ldpc_cap = ldpc_en;
	ra->ss_num = min(link_sta->rx_nss, rtwdev->hal.tx_nss) - 1;
	ra->en_sgi = sgi;
	ra->ra_mask = ra_mask;
	ra->fix_giltf_en = fix_giltf_en;
	ra->fix_giltf = fix_giltf;

	if (!csi)
		return;

	ra->fixed_csi_rate_en = false;
	ra->ra_csi_rate_en = true;
	ra->cr_tbl_sel = false;
	ra->band_num = rtwvif_link->phy_idx;
	ra->csi_bw = bw_mode;
	ra->csi_gi_ltf = RTW89_GILTF_LGI_4XHE32;
	ra->csi_mcs_ss_idx = 5;
	ra->csi_mode = csi_mode;
}

void rtw89_phy_ra_update_sta_link(struct rtw89_dev *rtwdev,
				  struct rtw89_sta_link *rtwsta_link,
				  u32 changed)
{
	struct rtw89_vif_link *rtwvif_link = rtwsta_link->rtwvif_link;
	struct ieee80211_vif *vif = rtwvif_link_to_vif(rtwvif_link);
	struct rtw89_ra_info *ra = &rtwsta_link->ra;
	struct ieee80211_link_sta *link_sta;

	rcu_read_lock();

	link_sta = rtw89_sta_rcu_dereference_link(rtwsta_link, false);
	rtw89_phy_ra_sta_update(rtwdev, rtwvif_link, rtwsta_link,
				link_sta, vif->p2p, false);

	rcu_read_unlock();

	if (changed & IEEE80211_RC_SUPP_RATES_CHANGED)
		ra->upd_mask = 1;
	if (changed & (IEEE80211_RC_BW_CHANGED | IEEE80211_RC_NSS_CHANGED))
		ra->upd_bw_nss_mask = 1;

	rtw89_debug(rtwdev, RTW89_DBG_RA,
		    "ra updat: macid = %d, bw = %d, nss = %d, gi = %d %d",
		    ra->macid,
		    ra->bw_cap,
		    ra->ss_num,
		    ra->en_sgi,
		    ra->giltf);

	rtw89_fw_h2c_ra(rtwdev, ra, false);
}

void rtw89_phy_ra_update_sta(struct rtw89_dev *rtwdev, struct ieee80211_sta *sta,
			     u32 changed)
{
	struct rtw89_sta *rtwsta = sta_to_rtwsta(sta);
	struct rtw89_sta_link *rtwsta_link;
	unsigned int link_id;

	rtw89_sta_for_each_link(rtwsta, rtwsta_link, link_id)
		rtw89_phy_ra_update_sta_link(rtwdev, rtwsta_link, changed);
}

static bool __check_rate_pattern(struct rtw89_phy_rate_pattern *next,
				 u16 rate_base, u64 ra_mask, u8 ra_mode,
				 u32 rate_ctrl, u32 ctrl_skip, bool force)
{
	u8 n, c;

	if (rate_ctrl == ctrl_skip)
		return true;

	n = hweight32(rate_ctrl);
	if (n == 0)
		return true;

	if (force && n != 1)
		return false;

	if (next->enable)
		return false;

	c = __fls(rate_ctrl);
	next->rate = rate_base + c;
	next->ra_mode = ra_mode;
	next->ra_mask = ra_mask;
	next->enable = true;

	return true;
}

#define RTW89_HW_RATE_BY_CHIP_GEN(rate) \
	{ \
		[RTW89_CHIP_AX] = RTW89_HW_RATE_ ## rate, \
		[RTW89_CHIP_BE] = RTW89_HW_RATE_V1_ ## rate, \
	}

static
void __rtw89_phy_rate_pattern_vif(struct rtw89_dev *rtwdev,
				  struct rtw89_vif_link *rtwvif_link,
				  const struct cfg80211_bitrate_mask *mask)
{
	struct ieee80211_supported_band *sband;
	struct rtw89_phy_rate_pattern next_pattern = {0};
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev,
						       rtwvif_link->chanctx_idx);
	static const u16 hw_rate_he[][RTW89_CHIP_GEN_NUM] = {
		RTW89_HW_RATE_BY_CHIP_GEN(HE_NSS1_MCS0),
		RTW89_HW_RATE_BY_CHIP_GEN(HE_NSS2_MCS0),
		RTW89_HW_RATE_BY_CHIP_GEN(HE_NSS3_MCS0),
		RTW89_HW_RATE_BY_CHIP_GEN(HE_NSS4_MCS0),
	};
	static const u16 hw_rate_vht[][RTW89_CHIP_GEN_NUM] = {
		RTW89_HW_RATE_BY_CHIP_GEN(VHT_NSS1_MCS0),
		RTW89_HW_RATE_BY_CHIP_GEN(VHT_NSS2_MCS0),
		RTW89_HW_RATE_BY_CHIP_GEN(VHT_NSS3_MCS0),
		RTW89_HW_RATE_BY_CHIP_GEN(VHT_NSS4_MCS0),
	};
	static const u16 hw_rate_ht[][RTW89_CHIP_GEN_NUM] = {
		RTW89_HW_RATE_BY_CHIP_GEN(MCS0),
		RTW89_HW_RATE_BY_CHIP_GEN(MCS8),
		RTW89_HW_RATE_BY_CHIP_GEN(MCS16),
		RTW89_HW_RATE_BY_CHIP_GEN(MCS24),
	};
	u8 band = chan->band_type;
	enum nl80211_band nl_band = rtw89_hw_to_nl80211_band(band);
	enum rtw89_chip_gen chip_gen = rtwdev->chip->chip_gen;
	u8 tx_nss = rtwdev->hal.tx_nss;
	u8 i;

	for (i = 0; i < tx_nss; i++)
		if (!__check_rate_pattern(&next_pattern, hw_rate_he[i][chip_gen],
					  RA_MASK_HE_RATES, RTW89_RA_MODE_HE,
					  mask->control[nl_band].he_mcs[i],
					  0, true))
			goto out;

	for (i = 0; i < tx_nss; i++)
		if (!__check_rate_pattern(&next_pattern, hw_rate_vht[i][chip_gen],
					  RA_MASK_VHT_RATES, RTW89_RA_MODE_VHT,
					  mask->control[nl_band].vht_mcs[i],
					  0, true))
			goto out;

	for (i = 0; i < tx_nss; i++)
		if (!__check_rate_pattern(&next_pattern, hw_rate_ht[i][chip_gen],
					  RA_MASK_HT_RATES, RTW89_RA_MODE_HT,
					  mask->control[nl_band].ht_mcs[i],
					  0, true))
			goto out;

	/* lagacy cannot be empty for nl80211_parse_tx_bitrate_mask, and
	 * require at least one basic rate for ieee80211_set_bitrate_mask,
	 * so the decision just depends on if all bitrates are set or not.
	 */
	sband = rtwdev->hw->wiphy->bands[nl_band];
	if (band == RTW89_BAND_2G) {
		if (!__check_rate_pattern(&next_pattern, RTW89_HW_RATE_CCK1,
					  RA_MASK_CCK_RATES | RA_MASK_OFDM_RATES,
					  RTW89_RA_MODE_CCK | RTW89_RA_MODE_OFDM,
					  mask->control[nl_band].legacy,
					  BIT(sband->n_bitrates) - 1, false))
			goto out;
	} else {
		if (!__check_rate_pattern(&next_pattern, RTW89_HW_RATE_OFDM6,
					  RA_MASK_OFDM_RATES, RTW89_RA_MODE_OFDM,
					  mask->control[nl_band].legacy,
					  BIT(sband->n_bitrates) - 1, false))
			goto out;
	}

	if (!next_pattern.enable)
		goto out;

	rtwvif_link->rate_pattern = next_pattern;
	rtw89_debug(rtwdev, RTW89_DBG_RA,
		    "configure pattern: rate 0x%x, mask 0x%llx, mode 0x%x\n",
		    next_pattern.rate,
		    next_pattern.ra_mask,
		    next_pattern.ra_mode);
	return;

out:
	rtwvif_link->rate_pattern.enable = false;
	rtw89_debug(rtwdev, RTW89_DBG_RA, "unset rate pattern\n");
}

void rtw89_phy_rate_pattern_vif(struct rtw89_dev *rtwdev,
				struct ieee80211_vif *vif,
				const struct cfg80211_bitrate_mask *mask)
{
	struct rtw89_vif *rtwvif = vif_to_rtwvif(vif);
	struct rtw89_vif_link *rtwvif_link;
	unsigned int link_id;

	rtw89_vif_for_each_link(rtwvif, rtwvif_link, link_id)
		__rtw89_phy_rate_pattern_vif(rtwdev, rtwvif_link, mask);
}

static void rtw89_phy_ra_update_sta_iter(void *data, struct ieee80211_sta *sta)
{
	struct rtw89_dev *rtwdev = (struct rtw89_dev *)data;

	rtw89_phy_ra_update_sta(rtwdev, sta, IEEE80211_RC_SUPP_RATES_CHANGED);
}

void rtw89_phy_ra_update(struct rtw89_dev *rtwdev)
{
	ieee80211_iterate_stations_atomic(rtwdev->hw,
					  rtw89_phy_ra_update_sta_iter,
					  rtwdev);
}

void rtw89_phy_ra_assoc(struct rtw89_dev *rtwdev, struct rtw89_sta_link *rtwsta_link)
{
	struct rtw89_vif_link *rtwvif_link = rtwsta_link->rtwvif_link;
	struct ieee80211_vif *vif = rtwvif_link_to_vif(rtwvif_link);
	struct rtw89_ra_info *ra = &rtwsta_link->ra;
	u8 rssi = ewma_rssi_read(&rtwsta_link->avg_rssi) >> RSSI_FACTOR;
	struct ieee80211_link_sta *link_sta;
	bool csi;

	rcu_read_lock();

	link_sta = rtw89_sta_rcu_dereference_link(rtwsta_link, true);
	csi = rtw89_sta_has_beamformer_cap(link_sta);

	rtw89_phy_ra_sta_update(rtwdev, rtwvif_link, rtwsta_link,
				link_sta, vif->p2p, csi);

	rcu_read_unlock();

	if (rssi > 40)
		ra->init_rate_lv = 1;
	else if (rssi > 20)
		ra->init_rate_lv = 2;
	else if (rssi > 1)
		ra->init_rate_lv = 3;
	else
		ra->init_rate_lv = 0;
	ra->upd_all = 1;
	rtw89_debug(rtwdev, RTW89_DBG_RA,
		    "ra assoc: macid = %d, mode = %d, bw = %d, nss = %d, lv = %d",
		    ra->macid,
		    ra->mode_ctrl,
		    ra->bw_cap,
		    ra->ss_num,
		    ra->init_rate_lv);
	rtw89_debug(rtwdev, RTW89_DBG_RA,
		    "ra assoc: dcm = %d, er = %d, ldpc = %d, stbc = %d, gi = %d %d",
		    ra->dcm_cap,
		    ra->er_cap,
		    ra->ldpc_cap,
		    ra->stbc_cap,
		    ra->en_sgi,
		    ra->giltf);

	rtw89_fw_h2c_ra(rtwdev, ra, csi);
}

u8 rtw89_phy_get_txsc(struct rtw89_dev *rtwdev,
		      const struct rtw89_chan *chan,
		      enum rtw89_bandwidth dbw)
{
	enum rtw89_bandwidth cbw = chan->band_width;
	u8 pri_ch = chan->primary_channel;
	u8 central_ch = chan->channel;
	u8 txsc_idx = 0;
	u8 tmp = 0;

	if (cbw == dbw || cbw == RTW89_CHANNEL_WIDTH_20)
		return txsc_idx;

	switch (cbw) {
	case RTW89_CHANNEL_WIDTH_40:
		txsc_idx = pri_ch > central_ch ? 1 : 2;
		break;
	case RTW89_CHANNEL_WIDTH_80:
		if (dbw == RTW89_CHANNEL_WIDTH_20) {
			if (pri_ch > central_ch)
				txsc_idx = (pri_ch - central_ch) >> 1;
			else
				txsc_idx = ((central_ch - pri_ch) >> 1) + 1;
		} else {
			txsc_idx = pri_ch > central_ch ? 9 : 10;
		}
		break;
	case RTW89_CHANNEL_WIDTH_160:
		if (pri_ch > central_ch)
			tmp = (pri_ch - central_ch) >> 1;
		else
			tmp = ((central_ch - pri_ch) >> 1) + 1;

		if (dbw == RTW89_CHANNEL_WIDTH_20) {
			txsc_idx = tmp;
		} else if (dbw == RTW89_CHANNEL_WIDTH_40) {
			if (tmp == 1 || tmp == 3)
				txsc_idx = 9;
			else if (tmp == 5 || tmp == 7)
				txsc_idx = 11;
			else if (tmp == 2 || tmp == 4)
				txsc_idx = 10;
			else if (tmp == 6 || tmp == 8)
				txsc_idx = 12;
			else
				return 0xff;
		} else {
			txsc_idx = pri_ch > central_ch ? 13 : 14;
		}
		break;
	case RTW89_CHANNEL_WIDTH_80_80:
		if (dbw == RTW89_CHANNEL_WIDTH_20) {
			if (pri_ch > central_ch)
				txsc_idx = (10 - (pri_ch - central_ch)) >> 1;
			else
				txsc_idx = ((central_ch - pri_ch) >> 1) + 5;
		} else if (dbw == RTW89_CHANNEL_WIDTH_40) {
			txsc_idx = pri_ch > central_ch ? 10 : 12;
		} else {
			txsc_idx = 14;
		}
		break;
	default:
		break;
	}

	return txsc_idx;
}
EXPORT_SYMBOL(rtw89_phy_get_txsc);

u8 rtw89_phy_get_txsb(struct rtw89_dev *rtwdev, const struct rtw89_chan *chan,
		      enum rtw89_bandwidth dbw)
{
	enum rtw89_bandwidth cbw = chan->band_width;
	u8 pri_ch = chan->primary_channel;
	u8 central_ch = chan->channel;
	u8 txsb_idx = 0;

	if (cbw == dbw || cbw == RTW89_CHANNEL_WIDTH_20)
		return txsb_idx;

	switch (cbw) {
	case RTW89_CHANNEL_WIDTH_40:
		txsb_idx = pri_ch > central_ch ? 1 : 0;
		break;
	case RTW89_CHANNEL_WIDTH_80:
		if (dbw == RTW89_CHANNEL_WIDTH_20)
			txsb_idx = (pri_ch - central_ch + 6) / 4;
		else
			txsb_idx = pri_ch > central_ch ? 1 : 0;
		break;
	case RTW89_CHANNEL_WIDTH_160:
		if (dbw == RTW89_CHANNEL_WIDTH_20)
			txsb_idx = (pri_ch - central_ch + 14) / 4;
		else if (dbw == RTW89_CHANNEL_WIDTH_40)
			txsb_idx = (pri_ch - central_ch + 12) / 8;
		else
			txsb_idx = pri_ch > central_ch ? 1 : 0;
		break;
	case RTW89_CHANNEL_WIDTH_320:
		if (dbw == RTW89_CHANNEL_WIDTH_20)
			txsb_idx = (pri_ch - central_ch + 30) / 4;
		else if (dbw == RTW89_CHANNEL_WIDTH_40)
			txsb_idx = (pri_ch - central_ch + 28) / 8;
		else if (dbw == RTW89_CHANNEL_WIDTH_80)
			txsb_idx = (pri_ch - central_ch + 24) / 16;
		else
			txsb_idx = pri_ch > central_ch ? 1 : 0;
		break;
	default:
		break;
	}

	return txsb_idx;
}
EXPORT_SYMBOL(rtw89_phy_get_txsb);

static bool rtw89_phy_check_swsi_busy(struct rtw89_dev *rtwdev)
{
	return !!rtw89_phy_read32_mask(rtwdev, R_SWSI_V1, B_SWSI_W_BUSY_V1) ||
	       !!rtw89_phy_read32_mask(rtwdev, R_SWSI_V1, B_SWSI_R_BUSY_V1);
}

u32 rtw89_phy_read_rf(struct rtw89_dev *rtwdev, enum rtw89_rf_path rf_path,
		      u32 addr, u32 mask)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const u32 *base_addr = chip->rf_base_addr;
	u32 val, direct_addr;

	if (rf_path >= rtwdev->chip->rf_path_num) {
		rtw89_err(rtwdev, "unsupported rf path (%d)\n", rf_path);
		return INV_RF_DATA;
	}

	addr &= 0xff;
	direct_addr = base_addr[rf_path] + (addr << 2);
	mask &= RFREG_MASK;

	val = rtw89_phy_read32_mask(rtwdev, direct_addr, mask);

	return val;
}
EXPORT_SYMBOL(rtw89_phy_read_rf);

static u32 rtw89_phy_read_rf_a(struct rtw89_dev *rtwdev,
			       enum rtw89_rf_path rf_path, u32 addr, u32 mask)
{
	bool busy;
	bool done;
	u32 val;
	int ret;

	ret = read_poll_timeout_atomic(rtw89_phy_check_swsi_busy, busy, !busy,
				       1, 30, false, rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "read rf busy swsi\n");
		return INV_RF_DATA;
	}

	mask &= RFREG_MASK;

	val = FIELD_PREP(B_SWSI_READ_ADDR_PATH_V1, rf_path) |
	      FIELD_PREP(B_SWSI_READ_ADDR_ADDR_V1, addr);
	rtw89_phy_write32_mask(rtwdev, R_SWSI_READ_ADDR_V1, B_SWSI_READ_ADDR_V1, val);
	udelay(2);

	ret = read_poll_timeout_atomic(rtw89_phy_read32_mask, done, done, 1,
				       30, false, rtwdev, R_SWSI_V1,
				       B_SWSI_R_DATA_DONE_V1);
	if (ret) {
		rtw89_err(rtwdev, "read swsi busy\n");
		return INV_RF_DATA;
	}

	return rtw89_phy_read32_mask(rtwdev, R_SWSI_V1, mask);
}

u32 rtw89_phy_read_rf_v1(struct rtw89_dev *rtwdev, enum rtw89_rf_path rf_path,
			 u32 addr, u32 mask)
{
	bool ad_sel = FIELD_GET(RTW89_RF_ADDR_ADSEL_MASK, addr);

	if (rf_path >= rtwdev->chip->rf_path_num) {
		rtw89_err(rtwdev, "unsupported rf path (%d)\n", rf_path);
		return INV_RF_DATA;
	}

	if (ad_sel)
		return rtw89_phy_read_rf(rtwdev, rf_path, addr, mask);
	else
		return rtw89_phy_read_rf_a(rtwdev, rf_path, addr, mask);
}
EXPORT_SYMBOL(rtw89_phy_read_rf_v1);

static u32 rtw89_phy_read_full_rf_v2_a(struct rtw89_dev *rtwdev,
				       enum rtw89_rf_path rf_path, u32 addr)
{
	static const u16 r_addr_ofst[2] = {0x2C24, 0x2D24};
	static const u16 addr_ofst[2] = {0x2ADC, 0x2BDC};
	bool busy, done;
	int ret;
	u32 val;

	rtw89_phy_write32_mask(rtwdev, addr_ofst[rf_path], B_HWSI_ADD_CTL_MASK, 0x1);
	ret = read_poll_timeout_atomic(rtw89_phy_read32_mask, busy, !busy,
				       1, 3800, false,
				       rtwdev, r_addr_ofst[rf_path], B_HWSI_VAL_BUSY);
	if (ret) {
		rtw89_warn(rtwdev, "poll HWSI is busy\n");
		return INV_RF_DATA;
	}

	rtw89_phy_write32_mask(rtwdev, addr_ofst[rf_path], B_HWSI_ADD_MASK, addr);
	rtw89_phy_write32_mask(rtwdev, addr_ofst[rf_path], B_HWSI_ADD_RD, 0x1);
	udelay(2);

	ret = read_poll_timeout_atomic(rtw89_phy_read32_mask, done, done,
				       1, 3800, false,
				       rtwdev, r_addr_ofst[rf_path], B_HWSI_VAL_RDONE);
	if (ret) {
		rtw89_warn(rtwdev, "read HWSI is busy\n");
		val = INV_RF_DATA;
		goto out;
	}

	val = rtw89_phy_read32_mask(rtwdev, r_addr_ofst[rf_path], RFREG_MASK);
out:
	rtw89_phy_write32_mask(rtwdev, addr_ofst[rf_path], B_HWSI_ADD_POLL_MASK, 0);

	return val;
}

static u32 rtw89_phy_read_rf_v2_a(struct rtw89_dev *rtwdev,
				  enum rtw89_rf_path rf_path, u32 addr, u32 mask)
{
	u32 val;

	val = rtw89_phy_read_full_rf_v2_a(rtwdev, rf_path, addr);

	return (val & mask) >> __ffs(mask);
}

u32 rtw89_phy_read_rf_v2(struct rtw89_dev *rtwdev, enum rtw89_rf_path rf_path,
			 u32 addr, u32 mask)
{
	bool ad_sel = u32_get_bits(addr, RTW89_RF_ADDR_ADSEL_MASK);

	if (rf_path >= rtwdev->chip->rf_path_num) {
		rtw89_err(rtwdev, "unsupported rf path (%d)\n", rf_path);
		return INV_RF_DATA;
	}

	if (ad_sel)
		return rtw89_phy_read_rf(rtwdev, rf_path, addr, mask);
	else
		return rtw89_phy_read_rf_v2_a(rtwdev, rf_path, addr, mask);
}
EXPORT_SYMBOL(rtw89_phy_read_rf_v2);

bool rtw89_phy_write_rf(struct rtw89_dev *rtwdev, enum rtw89_rf_path rf_path,
			u32 addr, u32 mask, u32 data)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const u32 *base_addr = chip->rf_base_addr;
	u32 direct_addr;

	if (rf_path >= rtwdev->chip->rf_path_num) {
		rtw89_err(rtwdev, "unsupported rf path (%d)\n", rf_path);
		return false;
	}

	addr &= 0xff;
	direct_addr = base_addr[rf_path] + (addr << 2);
	mask &= RFREG_MASK;

	rtw89_phy_write32_mask(rtwdev, direct_addr, mask, data);

	/* delay to ensure writing properly */
	udelay(1);

	return true;
}
EXPORT_SYMBOL(rtw89_phy_write_rf);

static bool rtw89_phy_write_rf_a(struct rtw89_dev *rtwdev,
				 enum rtw89_rf_path rf_path, u32 addr, u32 mask,
				 u32 data)
{
	u8 bit_shift;
	u32 val;
	bool busy, b_msk_en = false;
	int ret;

	ret = read_poll_timeout_atomic(rtw89_phy_check_swsi_busy, busy, !busy,
				       1, 30, false, rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "write rf busy swsi\n");
		return false;
	}

	data &= RFREG_MASK;
	mask &= RFREG_MASK;

	if (mask != RFREG_MASK) {
		b_msk_en = true;
		rtw89_phy_write32_mask(rtwdev, R_SWSI_BIT_MASK_V1, RFREG_MASK,
				       mask);
		bit_shift = __ffs(mask);
		data = (data << bit_shift) & RFREG_MASK;
	}

	val = FIELD_PREP(B_SWSI_DATA_BIT_MASK_EN_V1, b_msk_en) |
	      FIELD_PREP(B_SWSI_DATA_PATH_V1, rf_path) |
	      FIELD_PREP(B_SWSI_DATA_ADDR_V1, addr) |
	      FIELD_PREP(B_SWSI_DATA_VAL_V1, data);

	rtw89_phy_write32_mask(rtwdev, R_SWSI_DATA_V1, MASKDWORD, val);

	return true;
}

bool rtw89_phy_write_rf_v1(struct rtw89_dev *rtwdev, enum rtw89_rf_path rf_path,
			   u32 addr, u32 mask, u32 data)
{
	bool ad_sel = FIELD_GET(RTW89_RF_ADDR_ADSEL_MASK, addr);

	if (rf_path >= rtwdev->chip->rf_path_num) {
		rtw89_err(rtwdev, "unsupported rf path (%d)\n", rf_path);
		return false;
	}

	if (ad_sel)
		return rtw89_phy_write_rf(rtwdev, rf_path, addr, mask, data);
	else
		return rtw89_phy_write_rf_a(rtwdev, rf_path, addr, mask, data);
}
EXPORT_SYMBOL(rtw89_phy_write_rf_v1);

static
bool rtw89_phy_write_full_rf_v2_a(struct rtw89_dev *rtwdev, enum rtw89_rf_path rf_path,
				  u32 addr, u32 data)
{
	static const u32 addr_is_idle[2] = {0x2C24, 0x2D24};
	static const u32 addr_ofst[2] = {0x2AE0, 0x2BE0};
	bool busy;
	u32 val;
	int ret;

	ret = read_poll_timeout_atomic(rtw89_phy_read32_mask, busy, !busy,
				       1, 3800, false,
				       rtwdev, addr_is_idle[rf_path], BIT(29));
	if (ret) {
		rtw89_warn(rtwdev, "[%s] HWSI is busy\n", __func__);
		return false;
	}

	val = u32_encode_bits(addr, B_HWSI_DATA_ADDR) |
	      u32_encode_bits(data, B_HWSI_DATA_VAL);

	rtw89_phy_write32(rtwdev, addr_ofst[rf_path], val);

	return true;
}

static
bool rtw89_phy_write_rf_a_v2(struct rtw89_dev *rtwdev, enum rtw89_rf_path rf_path,
			     u32 addr, u32 mask, u32 data)
{
	u32 val;

	if (mask == RFREG_MASK) {
		val = data;
	} else {
		val = rtw89_phy_read_full_rf_v2_a(rtwdev, rf_path, addr);
		val &= ~mask;
		val |= (data << __ffs(mask)) & mask;
	}

	return rtw89_phy_write_full_rf_v2_a(rtwdev, rf_path, addr, val);
}

bool rtw89_phy_write_rf_v2(struct rtw89_dev *rtwdev, enum rtw89_rf_path rf_path,
			   u32 addr, u32 mask, u32 data)
{
	bool ad_sel = u32_get_bits(addr, RTW89_RF_ADDR_ADSEL_MASK);

	if (rf_path >= rtwdev->chip->rf_path_num) {
		rtw89_err(rtwdev, "unsupported rf path (%d)\n", rf_path);
		return INV_RF_DATA;
	}

	if (ad_sel)
		return rtw89_phy_write_rf(rtwdev, rf_path, addr, mask, data);
	else
		return rtw89_phy_write_rf_a_v2(rtwdev, rf_path, addr, mask, data);
}
EXPORT_SYMBOL(rtw89_phy_write_rf_v2);

static bool rtw89_chip_rf_v1(struct rtw89_dev *rtwdev)
{
	return rtwdev->chip->ops->write_rf == rtw89_phy_write_rf_v1;
}

static void __rtw89_phy_bb_reset(struct rtw89_dev *rtwdev,
				 enum rtw89_phy_idx phy_idx)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	chip->ops->bb_reset(rtwdev, phy_idx);
}

static void rtw89_phy_bb_reset(struct rtw89_dev *rtwdev)
{
	__rtw89_phy_bb_reset(rtwdev, RTW89_PHY_0);
	if (rtwdev->dbcc_en)
		__rtw89_phy_bb_reset(rtwdev, RTW89_PHY_1);
}

static void rtw89_phy_config_bb_reg(struct rtw89_dev *rtwdev,
				    const struct rtw89_reg2_def *reg,
				    enum rtw89_rf_path rf_path,
				    void *extra_data)
{
	u32 addr;

	if (reg->addr == 0xfe) {
		mdelay(50);
	} else if (reg->addr == 0xfd) {
		mdelay(5);
	} else if (reg->addr == 0xfc) {
		mdelay(1);
	} else if (reg->addr == 0xfb) {
		udelay(50);
	} else if (reg->addr == 0xfa) {
		udelay(5);
	} else if (reg->addr == 0xf9) {
		udelay(1);
	} else if (reg->data == BYPASS_CR_DATA) {
		rtw89_debug(rtwdev, RTW89_DBG_PHY_TRACK, "Bypass CR 0x%x\n", reg->addr);
	} else {
		addr = reg->addr;

		if ((uintptr_t)extra_data == RTW89_PHY_1)
			addr += rtw89_phy0_phy1_offset(rtwdev, reg->addr);

		rtw89_phy_write32(rtwdev, addr, reg->data);
	}
}

union rtw89_phy_bb_gain_arg {
	u32 addr;
	struct {
		union {
			u8 type;
			struct {
				u8 rxsc_start:4;
				u8 bw:4;
			};
		};
		u8 path;
		u8 gain_band;
		u8 cfg_type;
	};
} __packed;

static void
rtw89_phy_cfg_bb_gain_error(struct rtw89_dev *rtwdev,
			    union rtw89_phy_bb_gain_arg arg, u32 data)
{
	struct rtw89_phy_bb_gain_info *gain = &rtwdev->bb_gain.ax;
	u8 type = arg.type;
	u8 path = arg.path;
	u8 gband = arg.gain_band;
	int i;

	switch (type) {
	case 0:
		for (i = 0; i < 4; i++, data >>= 8)
			gain->lna_gain[gband][path][i] = data & 0xff;
		break;
	case 1:
		for (i = 4; i < 7; i++, data >>= 8)
			gain->lna_gain[gband][path][i] = data & 0xff;
		break;
	case 2:
		for (i = 0; i < 2; i++, data >>= 8)
			gain->tia_gain[gband][path][i] = data & 0xff;
		break;
	default:
		rtw89_warn(rtwdev,
			   "bb gain error {0x%x:0x%x} with unknown type: %d\n",
			   arg.addr, data, type);
		break;
	}
}

enum rtw89_phy_bb_rxsc_start_idx {
	RTW89_BB_RXSC_START_IDX_FULL = 0,
	RTW89_BB_RXSC_START_IDX_20 = 1,
	RTW89_BB_RXSC_START_IDX_20_1 = 5,
	RTW89_BB_RXSC_START_IDX_40 = 9,
	RTW89_BB_RXSC_START_IDX_80 = 13,
};

static void
rtw89_phy_cfg_bb_rpl_ofst(struct rtw89_dev *rtwdev,
			  union rtw89_phy_bb_gain_arg arg, u32 data)
{
	struct rtw89_phy_bb_gain_info *gain = &rtwdev->bb_gain.ax;
	u8 rxsc_start = arg.rxsc_start;
	u8 bw = arg.bw;
	u8 path = arg.path;
	u8 gband = arg.gain_band;
	u8 rxsc;
	s8 ofst;
	int i;

	switch (bw) {
	case RTW89_CHANNEL_WIDTH_20:
		gain->rpl_ofst_20[gband][path] = (s8)data;
		break;
	case RTW89_CHANNEL_WIDTH_40:
		if (rxsc_start == RTW89_BB_RXSC_START_IDX_FULL) {
			gain->rpl_ofst_40[gband][path][0] = (s8)data;
		} else if (rxsc_start == RTW89_BB_RXSC_START_IDX_20) {
			for (i = 0; i < 2; i++, data >>= 8) {
				rxsc = RTW89_BB_RXSC_START_IDX_20 + i;
				ofst = (s8)(data & 0xff);
				gain->rpl_ofst_40[gband][path][rxsc] = ofst;
			}
		}
		break;
	case RTW89_CHANNEL_WIDTH_80:
		if (rxsc_start == RTW89_BB_RXSC_START_IDX_FULL) {
			gain->rpl_ofst_80[gband][path][0] = (s8)data;
		} else if (rxsc_start == RTW89_BB_RXSC_START_IDX_20) {
			for (i = 0; i < 4; i++, data >>= 8) {
				rxsc = RTW89_BB_RXSC_START_IDX_20 + i;
				ofst = (s8)(data & 0xff);
				gain->rpl_ofst_80[gband][path][rxsc] = ofst;
			}
		} else if (rxsc_start == RTW89_BB_RXSC_START_IDX_40) {
			for (i = 0; i < 2; i++, data >>= 8) {
				rxsc = RTW89_BB_RXSC_START_IDX_40 + i;
				ofst = (s8)(data & 0xff);
				gain->rpl_ofst_80[gband][path][rxsc] = ofst;
			}
		}
		break;
	case RTW89_CHANNEL_WIDTH_160:
		if (rxsc_start == RTW89_BB_RXSC_START_IDX_FULL) {
			gain->rpl_ofst_160[gband][path][0] = (s8)data;
		} else if (rxsc_start == RTW89_BB_RXSC_START_IDX_20) {
			for (i = 0; i < 4; i++, data >>= 8) {
				rxsc = RTW89_BB_RXSC_START_IDX_20 + i;
				ofst = (s8)(data & 0xff);
				gain->rpl_ofst_160[gband][path][rxsc] = ofst;
			}
		} else if (rxsc_start == RTW89_BB_RXSC_START_IDX_20_1) {
			for (i = 0; i < 4; i++, data >>= 8) {
				rxsc = RTW89_BB_RXSC_START_IDX_20_1 + i;
				ofst = (s8)(data & 0xff);
				gain->rpl_ofst_160[gband][path][rxsc] = ofst;
			}
		} else if (rxsc_start == RTW89_BB_RXSC_START_IDX_40) {
			for (i = 0; i < 4; i++, data >>= 8) {
				rxsc = RTW89_BB_RXSC_START_IDX_40 + i;
				ofst = (s8)(data & 0xff);
				gain->rpl_ofst_160[gband][path][rxsc] = ofst;
			}
		} else if (rxsc_start == RTW89_BB_RXSC_START_IDX_80) {
			for (i = 0; i < 2; i++, data >>= 8) {
				rxsc = RTW89_BB_RXSC_START_IDX_80 + i;
				ofst = (s8)(data & 0xff);
				gain->rpl_ofst_160[gband][path][rxsc] = ofst;
			}
		}
		break;
	default:
		rtw89_warn(rtwdev,
			   "bb rpl ofst {0x%x:0x%x} with unknown bw: %d\n",
			   arg.addr, data, bw);
		break;
	}
}

static void
rtw89_phy_cfg_bb_gain_bypass(struct rtw89_dev *rtwdev,
			     union rtw89_phy_bb_gain_arg arg, u32 data)
{
	struct rtw89_phy_bb_gain_info *gain = &rtwdev->bb_gain.ax;
	u8 type = arg.type;
	u8 path = arg.path;
	u8 gband = arg.gain_band;
	int i;

	switch (type) {
	case 0:
		for (i = 0; i < 4; i++, data >>= 8)
			gain->lna_gain_bypass[gband][path][i] = data & 0xff;
		break;
	case 1:
		for (i = 4; i < 7; i++, data >>= 8)
			gain->lna_gain_bypass[gband][path][i] = data & 0xff;
		break;
	default:
		rtw89_warn(rtwdev,
			   "bb gain bypass {0x%x:0x%x} with unknown type: %d\n",
			   arg.addr, data, type);
		break;
	}
}

static void
rtw89_phy_cfg_bb_gain_op1db(struct rtw89_dev *rtwdev,
			    union rtw89_phy_bb_gain_arg arg, u32 data)
{
	struct rtw89_phy_bb_gain_info *gain = &rtwdev->bb_gain.ax;
	u8 type = arg.type;
	u8 path = arg.path;
	u8 gband = arg.gain_band;
	int i;

	switch (type) {
	case 0:
		for (i = 0; i < 4; i++, data >>= 8)
			gain->lna_op1db[gband][path][i] = data & 0xff;
		break;
	case 1:
		for (i = 4; i < 7; i++, data >>= 8)
			gain->lna_op1db[gband][path][i] = data & 0xff;
		break;
	case 2:
		for (i = 0; i < 4; i++, data >>= 8)
			gain->tia_lna_op1db[gband][path][i] = data & 0xff;
		break;
	case 3:
		for (i = 4; i < 8; i++, data >>= 8)
			gain->tia_lna_op1db[gband][path][i] = data & 0xff;
		break;
	default:
		rtw89_warn(rtwdev,
			   "bb gain op1db {0x%x:0x%x} with unknown type: %d\n",
			   arg.addr, data, type);
		break;
	}
}

static void rtw89_phy_config_bb_gain_ax(struct rtw89_dev *rtwdev,
					const struct rtw89_reg2_def *reg,
					enum rtw89_rf_path rf_path,
					void *extra_data)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	union rtw89_phy_bb_gain_arg arg = { .addr = reg->addr };
	struct rtw89_efuse *efuse = &rtwdev->efuse;

	if (arg.gain_band >= RTW89_BB_GAIN_BAND_NR)
		return;

	if (arg.path >= chip->rf_path_num)
		return;

	if (arg.addr >= 0xf9 && arg.addr <= 0xfe) {
		rtw89_warn(rtwdev, "bb gain table with flow ctrl\n");
		return;
	}

	switch (arg.cfg_type) {
	case 0:
		rtw89_phy_cfg_bb_gain_error(rtwdev, arg, reg->data);
		break;
	case 1:
		rtw89_phy_cfg_bb_rpl_ofst(rtwdev, arg, reg->data);
		break;
	case 2:
		rtw89_phy_cfg_bb_gain_bypass(rtwdev, arg, reg->data);
		break;
	case 3:
		rtw89_phy_cfg_bb_gain_op1db(rtwdev, arg, reg->data);
		break;
	case 4:
		/* This cfg_type is only used by rfe_type >= 50 with eFEM */
		if (efuse->rfe_type < 50)
			break;
		fallthrough;
	default:
		rtw89_warn(rtwdev,
			   "bb gain {0x%x:0x%x} with unknown cfg type: %d\n",
			   arg.addr, reg->data, arg.cfg_type);
		break;
	}
}

static void
rtw89_phy_cofig_rf_reg_store(struct rtw89_dev *rtwdev,
			     const struct rtw89_reg2_def *reg,
			     enum rtw89_rf_path rf_path,
			     struct rtw89_fw_h2c_rf_reg_info *info)
{
	u16 idx = info->curr_idx % RTW89_H2C_RF_PAGE_SIZE;
	u8 page = info->curr_idx / RTW89_H2C_RF_PAGE_SIZE;

	if (page >= RTW89_H2C_RF_PAGE_NUM) {
		rtw89_warn(rtwdev, "RF parameters exceed size. path=%d, idx=%d",
			   rf_path, info->curr_idx);
		return;
	}

	info->rtw89_phy_config_rf_h2c[page][idx] =
		cpu_to_le32((reg->addr << 20) | reg->data);
	info->curr_idx++;
}

static int rtw89_phy_config_rf_reg_fw(struct rtw89_dev *rtwdev,
				      struct rtw89_fw_h2c_rf_reg_info *info)
{
	u16 remain = info->curr_idx;
	u16 len = 0;
	u8 i;
	int ret = 0;

	if (remain > RTW89_H2C_RF_PAGE_NUM * RTW89_H2C_RF_PAGE_SIZE) {
		rtw89_warn(rtwdev,
			   "rf reg h2c total len %d larger than %d\n",
			   remain, RTW89_H2C_RF_PAGE_NUM * RTW89_H2C_RF_PAGE_SIZE);
		ret = -EINVAL;
		goto out;
	}

	for (i = 0; i < RTW89_H2C_RF_PAGE_NUM && remain; i++, remain -= len) {
		len = remain > RTW89_H2C_RF_PAGE_SIZE ? RTW89_H2C_RF_PAGE_SIZE : remain;
		ret = rtw89_fw_h2c_rf_reg(rtwdev, info, len * 4, i);
		if (ret)
			goto out;
	}
out:
	info->curr_idx = 0;

	return ret;
}

static void rtw89_phy_config_rf_reg_noio(struct rtw89_dev *rtwdev,
					 const struct rtw89_reg2_def *reg,
					 enum rtw89_rf_path rf_path,
					 void *extra_data)
{
	u32 addr = reg->addr;

	if (addr == 0xfe || addr == 0xfd || addr == 0xfc || addr == 0xfb ||
	    addr == 0xfa || addr == 0xf9)
		return;

	if (rtw89_chip_rf_v1(rtwdev) && addr < 0x100)
		return;

	rtw89_phy_cofig_rf_reg_store(rtwdev, reg, rf_path,
				     (struct rtw89_fw_h2c_rf_reg_info *)extra_data);
}

static void rtw89_phy_config_rf_reg(struct rtw89_dev *rtwdev,
				    const struct rtw89_reg2_def *reg,
				    enum rtw89_rf_path rf_path,
				    void *extra_data)
{
	if (reg->addr == 0xfe) {
		mdelay(50);
	} else if (reg->addr == 0xfd) {
		mdelay(5);
	} else if (reg->addr == 0xfc) {
		mdelay(1);
	} else if (reg->addr == 0xfb) {
		udelay(50);
	} else if (reg->addr == 0xfa) {
		udelay(5);
	} else if (reg->addr == 0xf9) {
		udelay(1);
	} else {
		rtw89_write_rf(rtwdev, rf_path, reg->addr, 0xfffff, reg->data);
		rtw89_phy_cofig_rf_reg_store(rtwdev, reg, rf_path,
					     (struct rtw89_fw_h2c_rf_reg_info *)extra_data);
	}
}

void rtw89_phy_config_rf_reg_v1(struct rtw89_dev *rtwdev,
				const struct rtw89_reg2_def *reg,
				enum rtw89_rf_path rf_path,
				void *extra_data)
{
	rtw89_write_rf(rtwdev, rf_path, reg->addr, RFREG_MASK, reg->data);

	if (reg->addr < 0x100)
		return;

	rtw89_phy_cofig_rf_reg_store(rtwdev, reg, rf_path,
				     (struct rtw89_fw_h2c_rf_reg_info *)extra_data);
}
EXPORT_SYMBOL(rtw89_phy_config_rf_reg_v1);

static int rtw89_phy_sel_headline(struct rtw89_dev *rtwdev,
				  const struct rtw89_phy_table *table,
				  u32 *headline_size, u32 *headline_idx,
				  u8 rfe, u8 cv)
{
	const struct rtw89_reg2_def *reg;
	u32 headline;
	u32 compare, target;
	u8 rfe_para, cv_para;
	u8 cv_max = 0;
	bool case_matched = false;
	u32 i;

	for (i = 0; i < table->n_regs; i++) {
		reg = &table->regs[i];
		headline = get_phy_headline(reg->addr);
		if (headline != PHY_HEADLINE_VALID)
			break;
	}
	*headline_size = i;
	if (*headline_size == 0)
		return 0;

	/* case 1: RFE match, CV match */
	compare = get_phy_compare(rfe, cv);
	for (i = 0; i < *headline_size; i++) {
		reg = &table->regs[i];
		target = get_phy_target(reg->addr);
		if (target == compare) {
			*headline_idx = i;
			return 0;
		}
	}

	/* case 2: RFE match, CV don't care */
	compare = get_phy_compare(rfe, PHY_COND_DONT_CARE);
	for (i = 0; i < *headline_size; i++) {
		reg = &table->regs[i];
		target = get_phy_target(reg->addr);
		if (target == compare) {
			*headline_idx = i;
			return 0;
		}
	}

	/* case 3: RFE match, CV max in table */
	for (i = 0; i < *headline_size; i++) {
		reg = &table->regs[i];
		rfe_para = get_phy_cond_rfe(reg->addr);
		cv_para = get_phy_cond_cv(reg->addr);
		if (rfe_para == rfe) {
			if (cv_para >= cv_max) {
				cv_max = cv_para;
				*headline_idx = i;
				case_matched = true;
			}
		}
	}

	if (case_matched)
		return 0;

	/* case 4: RFE don't care, CV max in table */
	for (i = 0; i < *headline_size; i++) {
		reg = &table->regs[i];
		rfe_para = get_phy_cond_rfe(reg->addr);
		cv_para = get_phy_cond_cv(reg->addr);
		if (rfe_para == PHY_COND_DONT_CARE) {
			if (cv_para >= cv_max) {
				cv_max = cv_para;
				*headline_idx = i;
				case_matched = true;
			}
		}
	}

	if (case_matched)
		return 0;

	return -EINVAL;
}

static void rtw89_phy_init_reg(struct rtw89_dev *rtwdev,
			       const struct rtw89_phy_table *table,
			       void (*config)(struct rtw89_dev *rtwdev,
					      const struct rtw89_reg2_def *reg,
					      enum rtw89_rf_path rf_path,
					      void *data),
			       void *extra_data)
{
	const struct rtw89_reg2_def *reg;
	enum rtw89_rf_path rf_path = table->rf_path;
	u8 rfe = rtwdev->efuse.rfe_type;
	u8 cv = rtwdev->hal.cv;
	u32 i;
	u32 headline_size = 0, headline_idx = 0;
	u32 target = 0, cfg_target;
	u8 cond;
	bool is_matched = true;
	bool target_found = false;
	int ret;

	ret = rtw89_phy_sel_headline(rtwdev, table, &headline_size,
				     &headline_idx, rfe, cv);
	if (ret) {
		rtw89_err(rtwdev, "invalid PHY package: %d/%d\n", rfe, cv);
		return;
	}

	cfg_target = get_phy_target(table->regs[headline_idx].addr);
	for (i = headline_size; i < table->n_regs; i++) {
		reg = &table->regs[i];
		cond = get_phy_cond(reg->addr);
		switch (cond) {
		case PHY_COND_BRANCH_IF:
		case PHY_COND_BRANCH_ELIF:
			target = get_phy_target(reg->addr);
			break;
		case PHY_COND_BRANCH_ELSE:
			is_matched = false;
			if (!target_found) {
				rtw89_warn(rtwdev, "failed to load CR %x/%x\n",
					   reg->addr, reg->data);
				return;
			}
			break;
		case PHY_COND_BRANCH_END:
			is_matched = true;
			target_found = false;
			break;
		case PHY_COND_CHECK:
			if (target_found) {
				is_matched = false;
				break;
			}

			if (target == cfg_target) {
				is_matched = true;
				target_found = true;
			} else {
				is_matched = false;
				target_found = false;
			}
			break;
		default:
			if (is_matched)
				config(rtwdev, reg, rf_path, extra_data);
			break;
		}
	}
}

void rtw89_phy_init_bb_reg(struct rtw89_dev *rtwdev)
{
	struct rtw89_fw_elm_info *elm_info = &rtwdev->fw.elm_info;
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const struct rtw89_phy_table *bb_table;
	const struct rtw89_phy_table *bb_gain_table;

	bb_table = elm_info->bb_tbl ? elm_info->bb_tbl : chip->bb_table;
	rtw89_phy_init_reg(rtwdev, bb_table, rtw89_phy_config_bb_reg, NULL);
	if (rtwdev->dbcc_en)
		rtw89_phy_init_reg(rtwdev, bb_table, rtw89_phy_config_bb_reg,
				   (void *)RTW89_PHY_1);

	rtw89_chip_init_txpwr_unit(rtwdev);

	bb_gain_table = elm_info->bb_gain ? elm_info->bb_gain : chip->bb_gain_table;
	if (bb_gain_table)
		rtw89_phy_init_reg(rtwdev, bb_gain_table,
				   chip->phy_def->config_bb_gain, NULL);

	rtw89_phy_bb_reset(rtwdev);
}

static u32 rtw89_phy_nctl_poll(struct rtw89_dev *rtwdev)
{
	rtw89_phy_write32(rtwdev, 0x8080, 0x4);
	udelay(1);
	return rtw89_phy_read32(rtwdev, 0x8080);
}

void rtw89_phy_init_rf_reg(struct rtw89_dev *rtwdev, bool noio)
{
	void (*config)(struct rtw89_dev *rtwdev, const struct rtw89_reg2_def *reg,
		       enum rtw89_rf_path rf_path, void *data);
	struct rtw89_fw_elm_info *elm_info = &rtwdev->fw.elm_info;
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const struct rtw89_phy_table *rf_table;
	struct rtw89_fw_h2c_rf_reg_info *rf_reg_info;
	u8 path;

	rf_reg_info = kzalloc(sizeof(*rf_reg_info), GFP_KERNEL);
	if (!rf_reg_info)
		return;

	for (path = RF_PATH_A; path < chip->rf_path_num; path++) {
		rf_table = elm_info->rf_radio[path] ?
			   elm_info->rf_radio[path] : chip->rf_table[path];
		rf_reg_info->rf_path = rf_table->rf_path;
		if (noio)
			config = rtw89_phy_config_rf_reg_noio;
		else
			config = rf_table->config ? rf_table->config :
				 rtw89_phy_config_rf_reg;
		rtw89_phy_init_reg(rtwdev, rf_table, config, (void *)rf_reg_info);
		if (rtw89_phy_config_rf_reg_fw(rtwdev, rf_reg_info))
			rtw89_warn(rtwdev, "rf path %d reg h2c config failed\n",
				   rf_reg_info->rf_path);
	}
	kfree(rf_reg_info);
}

static void rtw89_phy_preinit_rf_nctl_ax(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	u32 val;
	int ret;

	/* IQK/DPK clock & reset */
	rtw89_phy_write32_set(rtwdev, R_IOQ_IQK_DPK, 0x3);
	rtw89_phy_write32_set(rtwdev, R_GNT_BT_WGT_EN, 0x1);
	rtw89_phy_write32_set(rtwdev, R_P0_PATH_RST, 0x8000000);
	if (chip->chip_id != RTL8851B)
		rtw89_phy_write32_set(rtwdev, R_P1_PATH_RST, 0x8000000);
	if (chip->chip_id == RTL8852B || chip->chip_id == RTL8852BT)
		rtw89_phy_write32_set(rtwdev, R_IOQ_IQK_DPK, 0x2);

	/* check 0x8080 */
	rtw89_phy_write32(rtwdev, R_NCTL_CFG, 0x8);

	ret = read_poll_timeout(rtw89_phy_nctl_poll, val, val == 0x4, 10,
				1000, false, rtwdev);
	if (ret)
		rtw89_err(rtwdev, "failed to poll nctl block\n");
}

static void rtw89_phy_init_rf_nctl(struct rtw89_dev *rtwdev)
{
	struct rtw89_fw_elm_info *elm_info = &rtwdev->fw.elm_info;
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const struct rtw89_phy_table *nctl_table;

	rtw89_phy_preinit_rf_nctl(rtwdev);

	nctl_table = elm_info->rf_nctl ? elm_info->rf_nctl : chip->nctl_table;
	rtw89_phy_init_reg(rtwdev, nctl_table, rtw89_phy_config_bb_reg, NULL);

	if (chip->nctl_post_table)
		rtw89_rfk_parser(rtwdev, chip->nctl_post_table);
}

static u32 rtw89_phy0_phy1_offset_ax(struct rtw89_dev *rtwdev, u32 addr)
{
	u32 phy_page = addr >> 8;
	u32 ofst = 0;

	switch (phy_page) {
	case 0x6:
	case 0x7:
	case 0x8:
	case 0x9:
	case 0xa:
	case 0xb:
	case 0xc:
	case 0xd:
	case 0x19:
	case 0x1a:
	case 0x1b:
		ofst = 0x2000;
		break;
	default:
		/* warning case */
		ofst = 0;
		break;
	}

	if (phy_page >= 0x40 && phy_page <= 0x4f)
		ofst = 0x2000;

	return ofst;
}

void rtw89_phy_write32_idx(struct rtw89_dev *rtwdev, u32 addr, u32 mask,
			   u32 data, enum rtw89_phy_idx phy_idx)
{
	if (rtwdev->dbcc_en && phy_idx == RTW89_PHY_1)
		addr += rtw89_phy0_phy1_offset(rtwdev, addr);
	rtw89_phy_write32_mask(rtwdev, addr, mask, data);
}
EXPORT_SYMBOL(rtw89_phy_write32_idx);

void rtw89_phy_write32_idx_set(struct rtw89_dev *rtwdev, u32 addr, u32 bits,
			       enum rtw89_phy_idx phy_idx)
{
	if (rtwdev->dbcc_en && phy_idx == RTW89_PHY_1)
		addr += rtw89_phy0_phy1_offset(rtwdev, addr);
	rtw89_phy_write32_set(rtwdev, addr, bits);
}
EXPORT_SYMBOL(rtw89_phy_write32_idx_set);

void rtw89_phy_write32_idx_clr(struct rtw89_dev *rtwdev, u32 addr, u32 bits,
			       enum rtw89_phy_idx phy_idx)
{
	if (rtwdev->dbcc_en && phy_idx == RTW89_PHY_1)
		addr += rtw89_phy0_phy1_offset(rtwdev, addr);
	rtw89_phy_write32_clr(rtwdev, addr, bits);
}
EXPORT_SYMBOL(rtw89_phy_write32_idx_clr);

u32 rtw89_phy_read32_idx(struct rtw89_dev *rtwdev, u32 addr, u32 mask,
			 enum rtw89_phy_idx phy_idx)
{
	if (rtwdev->dbcc_en && phy_idx == RTW89_PHY_1)
		addr += rtw89_phy0_phy1_offset(rtwdev, addr);
	return rtw89_phy_read32_mask(rtwdev, addr, mask);
}
EXPORT_SYMBOL(rtw89_phy_read32_idx);

void rtw89_phy_set_phy_regs(struct rtw89_dev *rtwdev, u32 addr, u32 mask,
			    u32 val)
{
	rtw89_phy_write32_idx(rtwdev, addr, mask, val, RTW89_PHY_0);

	if (!rtwdev->dbcc_en)
		return;

	rtw89_phy_write32_idx(rtwdev, addr, mask, val, RTW89_PHY_1);
}
EXPORT_SYMBOL(rtw89_phy_set_phy_regs);

void rtw89_phy_write_reg3_tbl(struct rtw89_dev *rtwdev,
			      const struct rtw89_phy_reg3_tbl *tbl)
{
	const struct rtw89_reg3_def *reg3;
	int i;

	for (i = 0; i < tbl->size; i++) {
		reg3 = &tbl->reg3[i];
		rtw89_phy_write32_mask(rtwdev, reg3->addr, reg3->mask, reg3->data);
	}
}
EXPORT_SYMBOL(rtw89_phy_write_reg3_tbl);

static u8 rtw89_phy_ant_gain_domain_to_regd(struct rtw89_dev *rtwdev, u8 ant_gain_regd)
{
	switch (ant_gain_regd) {
	case RTW89_ANT_GAIN_ETSI:
		return RTW89_ETSI;
	default:
		rtw89_debug(rtwdev, RTW89_DBG_TXPWR,
			    "unknown antenna gain domain: %d\n",
			    ant_gain_regd);
		return RTW89_REGD_NUM;
	}
}

/* antenna gain in unit of 0.25 dbm */
#define RTW89_ANT_GAIN_2GHZ_MIN -8
#define RTW89_ANT_GAIN_2GHZ_MAX 14
#define RTW89_ANT_GAIN_5GHZ_MIN -8
#define RTW89_ANT_GAIN_5GHZ_MAX 20
#define RTW89_ANT_GAIN_6GHZ_MIN -8
#define RTW89_ANT_GAIN_6GHZ_MAX 20

#define RTW89_ANT_GAIN_REF_2GHZ 14
#define RTW89_ANT_GAIN_REF_5GHZ 20
#define RTW89_ANT_GAIN_REF_6GHZ 20

void rtw89_phy_ant_gain_init(struct rtw89_dev *rtwdev)
{
	struct rtw89_ant_gain_info *ant_gain = &rtwdev->ant_gain;
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_acpi_rtag_result res = {};
	u32 domain;
	int ret;
	u8 i, j;
	u8 regd;
	u8 val;

	if (!chip->support_ant_gain)
		return;

	ret = rtw89_acpi_evaluate_rtag(rtwdev, &res);
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_TXPWR,
			    "acpi: cannot eval rtag: %d\n", ret);
		return;
	}

	if (res.revision != 0) {
		rtw89_debug(rtwdev, RTW89_DBG_TXPWR,
			    "unknown rtag revision: %d\n", res.revision);
		return;
	}

	domain = get_unaligned_le32(&res.domain);

	for (i = 0; i < RTW89_ANT_GAIN_DOMAIN_NUM; i++) {
		if (!(domain & BIT(i)))
			continue;

		regd = rtw89_phy_ant_gain_domain_to_regd(rtwdev, i);
		if (regd >= RTW89_REGD_NUM)
			continue;
		ant_gain->regd_enabled |= BIT(regd);
	}

	for (i = 0; i < RTW89_ANT_GAIN_CHAIN_NUM; i++) {
		for (j = 0; j < RTW89_ANT_GAIN_SUBBAND_NR; j++) {
			val = res.ant_gain_table[i][j];
			switch (j) {
			default:
			case RTW89_ANT_GAIN_2GHZ_SUBBAND:
				val = RTW89_ANT_GAIN_REF_2GHZ -
				      clamp_t(s8, val,
					      RTW89_ANT_GAIN_2GHZ_MIN,
					      RTW89_ANT_GAIN_2GHZ_MAX);
				break;
			case RTW89_ANT_GAIN_5GHZ_SUBBAND_1:
			case RTW89_ANT_GAIN_5GHZ_SUBBAND_2:
			case RTW89_ANT_GAIN_5GHZ_SUBBAND_2E:
			case RTW89_ANT_GAIN_5GHZ_SUBBAND_3_4:
				val = RTW89_ANT_GAIN_REF_5GHZ -
				      clamp_t(s8, val,
					      RTW89_ANT_GAIN_5GHZ_MIN,
					      RTW89_ANT_GAIN_5GHZ_MAX);
				break;
			case RTW89_ANT_GAIN_6GHZ_SUBBAND_5_L:
			case RTW89_ANT_GAIN_6GHZ_SUBBAND_5_H:
			case RTW89_ANT_GAIN_6GHZ_SUBBAND_6:
			case RTW89_ANT_GAIN_6GHZ_SUBBAND_7_L:
			case RTW89_ANT_GAIN_6GHZ_SUBBAND_7_H:
			case RTW89_ANT_GAIN_6GHZ_SUBBAND_8:
				val = RTW89_ANT_GAIN_REF_6GHZ -
				      clamp_t(s8, val,
					      RTW89_ANT_GAIN_6GHZ_MIN,
					      RTW89_ANT_GAIN_6GHZ_MAX);
			}
			ant_gain->offset[i][j] = val;
		}
	}
}

static
enum rtw89_ant_gain_subband rtw89_phy_ant_gain_get_subband(struct rtw89_dev *rtwdev,
							   u32 center_freq)
{
	switch (center_freq) {
	default:
		rtw89_debug(rtwdev, RTW89_DBG_TXPWR,
			    "center freq: %u to antenna gain subband is unhandled\n",
			    center_freq);
		fallthrough;
	case 2412 ... 2484:
		return RTW89_ANT_GAIN_2GHZ_SUBBAND;
	case 5180 ... 5240:
		return RTW89_ANT_GAIN_5GHZ_SUBBAND_1;
	case 5250 ... 5320:
		return RTW89_ANT_GAIN_5GHZ_SUBBAND_2;
	case 5500 ... 5720:
		return RTW89_ANT_GAIN_5GHZ_SUBBAND_2E;
	case 5745 ... 5885:
		return RTW89_ANT_GAIN_5GHZ_SUBBAND_3_4;
	case 5955 ... 6155:
		return RTW89_ANT_GAIN_6GHZ_SUBBAND_5_L;
	case 6175 ... 6415:
		return RTW89_ANT_GAIN_6GHZ_SUBBAND_5_H;
	case 6435 ... 6515:
		return RTW89_ANT_GAIN_6GHZ_SUBBAND_6;
	case 6535 ... 6695:
		return RTW89_ANT_GAIN_6GHZ_SUBBAND_7_L;
	case 6715 ... 6855:
		return RTW89_ANT_GAIN_6GHZ_SUBBAND_7_H;

	/* freq 6875 (ch 185, 20MHz) spans RTW89_ANT_GAIN_6GHZ_SUBBAND_7_H
	 * and RTW89_ANT_GAIN_6GHZ_SUBBAND_8, so directly describe it with
	 * struct rtw89_6ghz_span.
	 */

	case 6895 ... 7115:
		return RTW89_ANT_GAIN_6GHZ_SUBBAND_8;
	}
}

static s8 rtw89_phy_ant_gain_query(struct rtw89_dev *rtwdev,
				   enum rtw89_rf_path path, u32 center_freq)
{
	struct rtw89_ant_gain_info *ant_gain = &rtwdev->ant_gain;
	enum rtw89_ant_gain_subband subband_l, subband_h;
	const struct rtw89_6ghz_span *span;

	span = rtw89_get_6ghz_span(rtwdev, center_freq);

	if (span && RTW89_ANT_GAIN_SPAN_VALID(span)) {
		subband_l = span->ant_gain_subband_low;
		subband_h = span->ant_gain_subband_high;
	} else {
		subband_l = rtw89_phy_ant_gain_get_subband(rtwdev, center_freq);
		subband_h = subband_l;
	}

	rtw89_debug(rtwdev, RTW89_DBG_TXPWR,
		    "center_freq %u: antenna gain subband {%u, %u}\n",
		    center_freq, subband_l, subband_h);

	return min(ant_gain->offset[path][subband_l],
		   ant_gain->offset[path][subband_h]);
}

static s8 rtw89_phy_ant_gain_offset(struct rtw89_dev *rtwdev, u8 band, u32 center_freq)
{
	struct rtw89_ant_gain_info *ant_gain = &rtwdev->ant_gain;
	const struct rtw89_chip_info *chip = rtwdev->chip;
	u8 regd = rtw89_regd_get(rtwdev, band);
	s8 offset_patha, offset_pathb;

	if (!chip->support_ant_gain)
		return 0;

	if (ant_gain->block_country || !(ant_gain->regd_enabled & BIT(regd)))
		return 0;

	offset_patha = rtw89_phy_ant_gain_query(rtwdev, RF_PATH_A, center_freq);
	offset_pathb = rtw89_phy_ant_gain_query(rtwdev, RF_PATH_B, center_freq);

	if (RTW89_CHK_FW_FEATURE(NO_POWER_DIFFERENCE, &rtwdev->fw))
		return min(offset_patha, offset_pathb);

	return max(offset_patha, offset_pathb);
}

s16 rtw89_phy_ant_gain_pwr_offset(struct rtw89_dev *rtwdev,
				  const struct rtw89_chan *chan)
{
	struct rtw89_ant_gain_info *ant_gain = &rtwdev->ant_gain;
	const struct rtw89_chip_info *chip = rtwdev->chip;
	u8 regd = rtw89_regd_get(rtwdev, chan->band_type);
	s8 offset_patha, offset_pathb;

	if (!chip->support_ant_gain)
		return 0;

	if (ant_gain->block_country || !(ant_gain->regd_enabled & BIT(regd)))
		return 0;

	if (RTW89_CHK_FW_FEATURE(NO_POWER_DIFFERENCE, &rtwdev->fw))
		return 0;

	offset_patha = rtw89_phy_ant_gain_query(rtwdev, RF_PATH_A, chan->freq);
	offset_pathb = rtw89_phy_ant_gain_query(rtwdev, RF_PATH_B, chan->freq);

	return rtw89_phy_txpwr_rf_to_bb(rtwdev, offset_patha - offset_pathb);
}
EXPORT_SYMBOL(rtw89_phy_ant_gain_pwr_offset);

int rtw89_print_ant_gain(struct rtw89_dev *rtwdev, char *buf, size_t bufsz,
			 const struct rtw89_chan *chan)
{
	struct rtw89_ant_gain_info *ant_gain = &rtwdev->ant_gain;
	const struct rtw89_chip_info *chip = rtwdev->chip;
	u8 regd = rtw89_regd_get(rtwdev, chan->band_type);
	char *p = buf, *end = buf + bufsz;
	s8 offset_patha, offset_pathb;

	if (!(chip->support_ant_gain && (ant_gain->regd_enabled & BIT(regd))) ||
	    ant_gain->block_country) {
		p += scnprintf(p, end - p, "no DAG is applied\n");
		goto out;
	}

	offset_patha = rtw89_phy_ant_gain_query(rtwdev, RF_PATH_A, chan->freq);
	offset_pathb = rtw89_phy_ant_gain_query(rtwdev, RF_PATH_B, chan->freq);

	p += scnprintf(p, end - p, "ChainA offset: %d dBm\n", offset_patha);
	p += scnprintf(p, end - p, "ChainB offset: %d dBm\n", offset_pathb);

out:
	return p - buf;
}

static const u8 rtw89_rs_idx_num_ax[] = {
	[RTW89_RS_CCK] = RTW89_RATE_CCK_NUM,
	[RTW89_RS_OFDM] = RTW89_RATE_OFDM_NUM,
	[RTW89_RS_MCS] = RTW89_RATE_MCS_NUM_AX,
	[RTW89_RS_HEDCM] = RTW89_RATE_HEDCM_NUM,
	[RTW89_RS_OFFSET] = RTW89_RATE_OFFSET_NUM_AX,
};

static const u8 rtw89_rs_nss_num_ax[] = {
	[RTW89_RS_CCK] = 1,
	[RTW89_RS_OFDM] = 1,
	[RTW89_RS_MCS] = RTW89_NSS_NUM,
	[RTW89_RS_HEDCM] = RTW89_NSS_HEDCM_NUM,
	[RTW89_RS_OFFSET] = 1,
};

s8 *rtw89_phy_raw_byr_seek(struct rtw89_dev *rtwdev,
			   struct rtw89_txpwr_byrate *head,
			   const struct rtw89_rate_desc *desc)
{
	switch (desc->rs) {
	case RTW89_RS_CCK:
		return &head->cck[desc->idx];
	case RTW89_RS_OFDM:
		return &head->ofdm[desc->idx];
	case RTW89_RS_MCS:
		return &head->mcs[desc->ofdma][desc->nss][desc->idx];
	case RTW89_RS_HEDCM:
		return &head->hedcm[desc->ofdma][desc->nss][desc->idx];
	case RTW89_RS_OFFSET:
		return &head->offset[desc->idx];
	default:
		rtw89_warn(rtwdev, "unrecognized byr rs: %d\n", desc->rs);
		return &head->trap;
	}
}

void rtw89_phy_load_txpwr_byrate(struct rtw89_dev *rtwdev,
				 const struct rtw89_txpwr_table *tbl)
{
	const struct rtw89_txpwr_byrate_cfg *cfg = tbl->data;
	const struct rtw89_txpwr_byrate_cfg *end = cfg + tbl->size;
	struct rtw89_txpwr_byrate *byr_head;
	struct rtw89_rate_desc desc = {};
	s8 *byr;
	u32 data;
	u8 i;

	for (; cfg < end; cfg++) {
		byr_head = &rtwdev->byr[cfg->band][0];
		desc.rs = cfg->rs;
		desc.nss = cfg->nss;
		data = cfg->data;

		for (i = 0; i < cfg->len; i++, data >>= 8) {
			desc.idx = cfg->shf + i;
			byr = rtw89_phy_raw_byr_seek(rtwdev, byr_head, &desc);
			*byr = data & 0xff;
		}
	}
}
EXPORT_SYMBOL(rtw89_phy_load_txpwr_byrate);

static s8 rtw89_phy_txpwr_dbm_without_tolerance(s8 dbm)
{
	const u8 tssi_deviation_point = 0;
	const u8 tssi_max_deviation = 2;

	if (dbm <= tssi_deviation_point)
		dbm -= tssi_max_deviation;

	return dbm;
}

static s8 rtw89_phy_get_tpe_constraint(struct rtw89_dev *rtwdev, u8 band)
{
	struct rtw89_regulatory_info *regulatory = &rtwdev->regulatory;
	const struct rtw89_reg_6ghz_tpe *tpe = &regulatory->reg_6ghz_tpe;
	s8 cstr = S8_MAX;

	if (band == RTW89_BAND_6G && tpe->valid)
		cstr = rtw89_phy_txpwr_dbm_without_tolerance(tpe->constraint);

	return rtw89_phy_txpwr_dbm_to_mac(rtwdev, cstr);
}

s8 rtw89_phy_read_txpwr_byrate(struct rtw89_dev *rtwdev, u8 band, u8 bw,
			       const struct rtw89_rate_desc *rate_desc)
{
	struct rtw89_txpwr_byrate *byr_head;
	s8 *byr;

	if (rate_desc->rs == RTW89_RS_CCK)
		band = RTW89_BAND_2G;

	byr_head = &rtwdev->byr[band][bw];
	byr = rtw89_phy_raw_byr_seek(rtwdev, byr_head, rate_desc);

	return rtw89_phy_txpwr_rf_to_mac(rtwdev, *byr);
}

static u8 rtw89_channel_6g_to_idx(struct rtw89_dev *rtwdev, u8 channel_6g)
{
	switch (channel_6g) {
	case 1 ... 29:
		return (channel_6g - 1) / 2;
	case 33 ... 61:
		return (channel_6g - 3) / 2;
	case 65 ... 93:
		return (channel_6g - 5) / 2;
	case 97 ... 125:
		return (channel_6g - 7) / 2;
	case 129 ... 157:
		return (channel_6g - 9) / 2;
	case 161 ... 189:
		return (channel_6g - 11) / 2;
	case 193 ... 221:
		return (channel_6g - 13) / 2;
	case 225 ... 253:
		return (channel_6g - 15) / 2;
	default:
		rtw89_warn(rtwdev, "unknown 6g channel: %d\n", channel_6g);
		return 0;
	}
}

static u8 rtw89_channel_to_idx(struct rtw89_dev *rtwdev, u8 band, u8 channel)
{
	if (band == RTW89_BAND_6G)
		return rtw89_channel_6g_to_idx(rtwdev, channel);

	switch (channel) {
	case 1 ... 14:
		return channel - 1;
	case 36 ... 64:
		return (channel - 36) / 2;
	case 100 ... 144:
		return ((channel - 100) / 2) + 15;
	case 149 ... 177:
		return ((channel - 149) / 2) + 38;
	default:
		rtw89_warn(rtwdev, "unknown channel: %d\n", channel);
		return 0;
	}
}

s8 rtw89_phy_read_txpwr_limit(struct rtw89_dev *rtwdev, u8 band,
			      u8 bw, u8 ntx, u8 rs, u8 bf, u8 ch)
{
	const struct rtw89_rfe_parms *rfe_parms = rtwdev->rfe_parms;
	const struct rtw89_txpwr_rule_2ghz *rule_2ghz = &rfe_parms->rule_2ghz;
	const struct rtw89_txpwr_rule_5ghz *rule_5ghz = &rfe_parms->rule_5ghz;
	const struct rtw89_txpwr_rule_6ghz *rule_6ghz = &rfe_parms->rule_6ghz;
	struct rtw89_regulatory_info *regulatory = &rtwdev->regulatory;
	enum nl80211_band nl_band = rtw89_hw_to_nl80211_band(band);
	u32 freq = ieee80211_channel_to_frequency(ch, nl_band);
	u8 ch_idx = rtw89_channel_to_idx(rtwdev, band, ch);
	u8 regd = rtw89_regd_get(rtwdev, band);
	u8 reg6 = regulatory->reg_6ghz_power;
	struct rtw89_sar_parm sar_parm = {
		.center_freq = freq,
		.ntx = ntx,
	};
	s8 lmt = 0, sar, offset;
	s8 cstr;

	switch (band) {
	case RTW89_BAND_2G:
		lmt = (*rule_2ghz->lmt)[bw][ntx][rs][bf][regd][ch_idx];
		if (lmt)
			break;

		lmt = (*rule_2ghz->lmt)[bw][ntx][rs][bf][RTW89_WW][ch_idx];
		break;
	case RTW89_BAND_5G:
		lmt = (*rule_5ghz->lmt)[bw][ntx][rs][bf][regd][ch_idx];
		if (lmt)
			break;

		lmt = (*rule_5ghz->lmt)[bw][ntx][rs][bf][RTW89_WW][ch_idx];
		break;
	case RTW89_BAND_6G:
		lmt = (*rule_6ghz->lmt)[bw][ntx][rs][bf][regd][reg6][ch_idx];
		if (lmt)
			break;

		lmt = (*rule_6ghz->lmt)[bw][ntx][rs][bf][RTW89_WW]
				       [RTW89_REG_6GHZ_POWER_DFLT]
				       [ch_idx];
		break;
	default:
		rtw89_warn(rtwdev, "unknown band type: %d\n", band);
		return 0;
	}

	offset = rtw89_phy_ant_gain_offset(rtwdev, band, freq);
	lmt = rtw89_phy_txpwr_rf_to_mac(rtwdev, lmt + offset);
	sar = rtw89_query_sar(rtwdev, &sar_parm);
	cstr = rtw89_phy_get_tpe_constraint(rtwdev, band);

	return min3(lmt, sar, cstr);
}
EXPORT_SYMBOL(rtw89_phy_read_txpwr_limit);

#define __fill_txpwr_limit_nonbf_bf(ptr, band, bw, ntx, rs, ch)		\
	do {								\
		u8 __i;							\
		for (__i = 0; __i < RTW89_BF_NUM; __i++)		\
			ptr[__i] = rtw89_phy_read_txpwr_limit(rtwdev,	\
							      band,	\
							      bw, ntx,	\
							      rs, __i,	\
							      (ch));	\
	} while (0)

static void rtw89_phy_fill_txpwr_limit_20m_ax(struct rtw89_dev *rtwdev,
					      struct rtw89_txpwr_limit_ax *lmt,
					      u8 band, u8 ntx, u8 ch)
{
	__fill_txpwr_limit_nonbf_bf(lmt->cck_20m, band, RTW89_CHANNEL_WIDTH_20,
				    ntx, RTW89_RS_CCK, ch);
	__fill_txpwr_limit_nonbf_bf(lmt->cck_40m, band, RTW89_CHANNEL_WIDTH_40,
				    ntx, RTW89_RS_CCK, ch);
	__fill_txpwr_limit_nonbf_bf(lmt->ofdm, band, RTW89_CHANNEL_WIDTH_20,
				    ntx, RTW89_RS_OFDM, ch);
	__fill_txpwr_limit_nonbf_bf(lmt->mcs_20m[0], band,
				    RTW89_CHANNEL_WIDTH_20,
				    ntx, RTW89_RS_MCS, ch);
}

static void rtw89_phy_fill_txpwr_limit_40m_ax(struct rtw89_dev *rtwdev,
					      struct rtw89_txpwr_limit_ax *lmt,
					      u8 band, u8 ntx, u8 ch, u8 pri_ch)
{
	__fill_txpwr_limit_nonbf_bf(lmt->cck_20m, band, RTW89_CHANNEL_WIDTH_20,
				    ntx, RTW89_RS_CCK, ch - 2);
	__fill_txpwr_limit_nonbf_bf(lmt->cck_40m, band, RTW89_CHANNEL_WIDTH_40,
				    ntx, RTW89_RS_CCK, ch);
	__fill_txpwr_limit_nonbf_bf(lmt->ofdm, band, RTW89_CHANNEL_WIDTH_20,
				    ntx, RTW89_RS_OFDM, pri_ch);
	__fill_txpwr_limit_nonbf_bf(lmt->mcs_20m[0], band,
				    RTW89_CHANNEL_WIDTH_20,
				    ntx, RTW89_RS_MCS, ch - 2);
	__fill_txpwr_limit_nonbf_bf(lmt->mcs_20m[1], band,
				    RTW89_CHANNEL_WIDTH_20,
				    ntx, RTW89_RS_MCS, ch + 2);
	__fill_txpwr_limit_nonbf_bf(lmt->mcs_40m[0], band,
				    RTW89_CHANNEL_WIDTH_40,
				    ntx, RTW89_RS_MCS, ch);
}

static void rtw89_phy_fill_txpwr_limit_80m_ax(struct rtw89_dev *rtwdev,
					      struct rtw89_txpwr_limit_ax *lmt,
					      u8 band, u8 ntx, u8 ch, u8 pri_ch)
{
	s8 val_0p5_n[RTW89_BF_NUM];
	s8 val_0p5_p[RTW89_BF_NUM];
	u8 i;

	__fill_txpwr_limit_nonbf_bf(lmt->ofdm, band, RTW89_CHANNEL_WIDTH_20,
				    ntx, RTW89_RS_OFDM, pri_ch);
	__fill_txpwr_limit_nonbf_bf(lmt->mcs_20m[0], band,
				    RTW89_CHANNEL_WIDTH_20,
				    ntx, RTW89_RS_MCS, ch - 6);
	__fill_txpwr_limit_nonbf_bf(lmt->mcs_20m[1], band,
				    RTW89_CHANNEL_WIDTH_20,
				    ntx, RTW89_RS_MCS, ch - 2);
	__fill_txpwr_limit_nonbf_bf(lmt->mcs_20m[2], band,
				    RTW89_CHANNEL_WIDTH_20,
				    ntx, RTW89_RS_MCS, ch + 2);
	__fill_txpwr_limit_nonbf_bf(lmt->mcs_20m[3], band,
				    RTW89_CHANNEL_WIDTH_20,
				    ntx, RTW89_RS_MCS, ch + 6);
	__fill_txpwr_limit_nonbf_bf(lmt->mcs_40m[0], band,
				    RTW89_CHANNEL_WIDTH_40,
				    ntx, RTW89_RS_MCS, ch - 4);
	__fill_txpwr_limit_nonbf_bf(lmt->mcs_40m[1], band,
				    RTW89_CHANNEL_WIDTH_40,
				    ntx, RTW89_RS_MCS, ch + 4);
	__fill_txpwr_limit_nonbf_bf(lmt->mcs_80m[0], band,
				    RTW89_CHANNEL_WIDTH_80,
				    ntx, RTW89_RS_MCS, ch);

	__fill_txpwr_limit_nonbf_bf(val_0p5_n, band, RTW89_CHANNEL_WIDTH_40,
				    ntx, RTW89_RS_MCS, ch - 4);
	__fill_txpwr_limit_nonbf_bf(val_0p5_p, band, RTW89_CHANNEL_WIDTH_40,
				    ntx, RTW89_RS_MCS, ch + 4);

	for (i = 0; i < RTW89_BF_NUM; i++)
		lmt->mcs_40m_0p5[i] = min_t(s8, val_0p5_n[i], val_0p5_p[i]);
}

static void rtw89_phy_fill_txpwr_limit_160m_ax(struct rtw89_dev *rtwdev,
					       struct rtw89_txpwr_limit_ax *lmt,
					       u8 band, u8 ntx, u8 ch, u8 pri_ch)
{
	s8 val_0p5_n[RTW89_BF_NUM];
	s8 val_0p5_p[RTW89_BF_NUM];
	s8 val_2p5_n[RTW89_BF_NUM];
	s8 val_2p5_p[RTW89_BF_NUM];
	u8 i;

	/* fill ofdm section */
	__fill_txpwr_limit_nonbf_bf(lmt->ofdm, band, RTW89_CHANNEL_WIDTH_20,
				    ntx, RTW89_RS_OFDM, pri_ch);

	/* fill mcs 20m section */
	__fill_txpwr_limit_nonbf_bf(lmt->mcs_20m[0], band,
				    RTW89_CHANNEL_WIDTH_20,
				    ntx, RTW89_RS_MCS, ch - 14);
	__fill_txpwr_limit_nonbf_bf(lmt->mcs_20m[1], band,
				    RTW89_CHANNEL_WIDTH_20,
				    ntx, RTW89_RS_MCS, ch - 10);
	__fill_txpwr_limit_nonbf_bf(lmt->mcs_20m[2], band,
				    RTW89_CHANNEL_WIDTH_20,
				    ntx, RTW89_RS_MCS, ch - 6);
	__fill_txpwr_limit_nonbf_bf(lmt->mcs_20m[3], band,
				    RTW89_CHANNEL_WIDTH_20,
				    ntx, RTW89_RS_MCS, ch - 2);
	__fill_txpwr_limit_nonbf_bf(lmt->mcs_20m[4], band,
				    RTW89_CHANNEL_WIDTH_20,
				    ntx, RTW89_RS_MCS, ch + 2);
	__fill_txpwr_limit_nonbf_bf(lmt->mcs_20m[5], band,
				    RTW89_CHANNEL_WIDTH_20,
				    ntx, RTW89_RS_MCS, ch + 6);
	__fill_txpwr_limit_nonbf_bf(lmt->mcs_20m[6], band,
				    RTW89_CHANNEL_WIDTH_20,
				    ntx, RTW89_RS_MCS, ch + 10);
	__fill_txpwr_limit_nonbf_bf(lmt->mcs_20m[7], band,
				    RTW89_CHANNEL_WIDTH_20,
				    ntx, RTW89_RS_MCS, ch + 14);

	/* fill mcs 40m section */
	__fill_txpwr_limit_nonbf_bf(lmt->mcs_40m[0], band,
				    RTW89_CHANNEL_WIDTH_40,
				    ntx, RTW89_RS_MCS, ch - 12);
	__fill_txpwr_limit_nonbf_bf(lmt->mcs_40m[1], band,
				    RTW89_CHANNEL_WIDTH_40,
				    ntx, RTW89_RS_MCS, ch - 4);
	__fill_txpwr_limit_nonbf_bf(lmt->mcs_40m[2], band,
				    RTW89_CHANNEL_WIDTH_40,
				    ntx, RTW89_RS_MCS, ch + 4);
	__fill_txpwr_limit_nonbf_bf(lmt->mcs_40m[3], band,
				    RTW89_CHANNEL_WIDTH_40,
				    ntx, RTW89_RS_MCS, ch + 12);

	/* fill mcs 80m section */
	__fill_txpwr_limit_nonbf_bf(lmt->mcs_80m[0], band,
				    RTW89_CHANNEL_WIDTH_80,
				    ntx, RTW89_RS_MCS, ch - 8);
	__fill_txpwr_limit_nonbf_bf(lmt->mcs_80m[1], band,
				    RTW89_CHANNEL_WIDTH_80,
				    ntx, RTW89_RS_MCS, ch + 8);

	/* fill mcs 160m section */
	__fill_txpwr_limit_nonbf_bf(lmt->mcs_160m, band,
				    RTW89_CHANNEL_WIDTH_160,
				    ntx, RTW89_RS_MCS, ch);

	/* fill mcs 40m 0p5 section */
	__fill_txpwr_limit_nonbf_bf(val_0p5_n, band, RTW89_CHANNEL_WIDTH_40,
				    ntx, RTW89_RS_MCS, ch - 4);
	__fill_txpwr_limit_nonbf_bf(val_0p5_p, band, RTW89_CHANNEL_WIDTH_40,
				    ntx, RTW89_RS_MCS, ch + 4);

	for (i = 0; i < RTW89_BF_NUM; i++)
		lmt->mcs_40m_0p5[i] = min_t(s8, val_0p5_n[i], val_0p5_p[i]);

	/* fill mcs 40m 2p5 section */
	__fill_txpwr_limit_nonbf_bf(val_2p5_n, band, RTW89_CHANNEL_WIDTH_40,
				    ntx, RTW89_RS_MCS, ch - 8);
	__fill_txpwr_limit_nonbf_bf(val_2p5_p, band, RTW89_CHANNEL_WIDTH_40,
				    ntx, RTW89_RS_MCS, ch + 8);

	for (i = 0; i < RTW89_BF_NUM; i++)
		lmt->mcs_40m_2p5[i] = min_t(s8, val_2p5_n[i], val_2p5_p[i]);
}

static
void rtw89_phy_fill_txpwr_limit_ax(struct rtw89_dev *rtwdev,
				   const struct rtw89_chan *chan,
				   struct rtw89_txpwr_limit_ax *lmt,
				   u8 ntx)
{
	u8 band = chan->band_type;
	u8 pri_ch = chan->primary_channel;
	u8 ch = chan->channel;
	u8 bw = chan->band_width;

	memset(lmt, 0, sizeof(*lmt));

	switch (bw) {
	case RTW89_CHANNEL_WIDTH_20:
		rtw89_phy_fill_txpwr_limit_20m_ax(rtwdev, lmt, band, ntx, ch);
		break;
	case RTW89_CHANNEL_WIDTH_40:
		rtw89_phy_fill_txpwr_limit_40m_ax(rtwdev, lmt, band, ntx, ch,
						  pri_ch);
		break;
	case RTW89_CHANNEL_WIDTH_80:
		rtw89_phy_fill_txpwr_limit_80m_ax(rtwdev, lmt, band, ntx, ch,
						  pri_ch);
		break;
	case RTW89_CHANNEL_WIDTH_160:
		rtw89_phy_fill_txpwr_limit_160m_ax(rtwdev, lmt, band, ntx, ch,
						   pri_ch);
		break;
	}
}

s8 rtw89_phy_read_txpwr_limit_ru(struct rtw89_dev *rtwdev, u8 band,
				 u8 ru, u8 ntx, u8 ch)
{
	const struct rtw89_rfe_parms *rfe_parms = rtwdev->rfe_parms;
	const struct rtw89_txpwr_rule_2ghz *rule_2ghz = &rfe_parms->rule_2ghz;
	const struct rtw89_txpwr_rule_5ghz *rule_5ghz = &rfe_parms->rule_5ghz;
	const struct rtw89_txpwr_rule_6ghz *rule_6ghz = &rfe_parms->rule_6ghz;
	struct rtw89_regulatory_info *regulatory = &rtwdev->regulatory;
	enum nl80211_band nl_band = rtw89_hw_to_nl80211_band(band);
	u32 freq = ieee80211_channel_to_frequency(ch, nl_band);
	u8 ch_idx = rtw89_channel_to_idx(rtwdev, band, ch);
	u8 regd = rtw89_regd_get(rtwdev, band);
	u8 reg6 = regulatory->reg_6ghz_power;
	struct rtw89_sar_parm sar_parm = {
		.center_freq = freq,
		.ntx = ntx,
	};
	s8 lmt_ru = 0, sar, offset;
	s8 cstr;

	switch (band) {
	case RTW89_BAND_2G:
		lmt_ru = (*rule_2ghz->lmt_ru)[ru][ntx][regd][ch_idx];
		if (lmt_ru)
			break;

		lmt_ru = (*rule_2ghz->lmt_ru)[ru][ntx][RTW89_WW][ch_idx];
		break;
	case RTW89_BAND_5G:
		lmt_ru = (*rule_5ghz->lmt_ru)[ru][ntx][regd][ch_idx];
		if (lmt_ru)
			break;

		lmt_ru = (*rule_5ghz->lmt_ru)[ru][ntx][RTW89_WW][ch_idx];
		break;
	case RTW89_BAND_6G:
		lmt_ru = (*rule_6ghz->lmt_ru)[ru][ntx][regd][reg6][ch_idx];
		if (lmt_ru)
			break;

		lmt_ru = (*rule_6ghz->lmt_ru)[ru][ntx][RTW89_WW]
					     [RTW89_REG_6GHZ_POWER_DFLT]
					     [ch_idx];
		break;
	default:
		rtw89_warn(rtwdev, "unknown band type: %d\n", band);
		return 0;
	}

	offset = rtw89_phy_ant_gain_offset(rtwdev, band, freq);
	lmt_ru = rtw89_phy_txpwr_rf_to_mac(rtwdev, lmt_ru + offset);
	sar = rtw89_query_sar(rtwdev, &sar_parm);
	cstr = rtw89_phy_get_tpe_constraint(rtwdev, band);

	return min3(lmt_ru, sar, cstr);
}

static void
rtw89_phy_fill_txpwr_limit_ru_20m_ax(struct rtw89_dev *rtwdev,
				     struct rtw89_txpwr_limit_ru_ax *lmt_ru,
				     u8 band, u8 ntx, u8 ch)
{
	lmt_ru->ru26[0] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							RTW89_RU26,
							ntx, ch);
	lmt_ru->ru52[0] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							RTW89_RU52,
							ntx, ch);
	lmt_ru->ru106[0] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							 RTW89_RU106,
							 ntx, ch);
}

static void
rtw89_phy_fill_txpwr_limit_ru_40m_ax(struct rtw89_dev *rtwdev,
				     struct rtw89_txpwr_limit_ru_ax *lmt_ru,
				     u8 band, u8 ntx, u8 ch)
{
	lmt_ru->ru26[0] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							RTW89_RU26,
							ntx, ch - 2);
	lmt_ru->ru26[1] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							RTW89_RU26,
							ntx, ch + 2);
	lmt_ru->ru52[0] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							RTW89_RU52,
							ntx, ch - 2);
	lmt_ru->ru52[1] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							RTW89_RU52,
							ntx, ch + 2);
	lmt_ru->ru106[0] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							 RTW89_RU106,
							 ntx, ch - 2);
	lmt_ru->ru106[1] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							 RTW89_RU106,
							 ntx, ch + 2);
}

static void
rtw89_phy_fill_txpwr_limit_ru_80m_ax(struct rtw89_dev *rtwdev,
				     struct rtw89_txpwr_limit_ru_ax *lmt_ru,
				     u8 band, u8 ntx, u8 ch)
{
	lmt_ru->ru26[0] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							RTW89_RU26,
							ntx, ch - 6);
	lmt_ru->ru26[1] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							RTW89_RU26,
							ntx, ch - 2);
	lmt_ru->ru26[2] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							RTW89_RU26,
							ntx, ch + 2);
	lmt_ru->ru26[3] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							RTW89_RU26,
							ntx, ch + 6);
	lmt_ru->ru52[0] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							RTW89_RU52,
							ntx, ch - 6);
	lmt_ru->ru52[1] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							RTW89_RU52,
							ntx, ch - 2);
	lmt_ru->ru52[2] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							RTW89_RU52,
							ntx, ch + 2);
	lmt_ru->ru52[3] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							RTW89_RU52,
							ntx, ch + 6);
	lmt_ru->ru106[0] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							 RTW89_RU106,
							 ntx, ch - 6);
	lmt_ru->ru106[1] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							 RTW89_RU106,
							 ntx, ch - 2);
	lmt_ru->ru106[2] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							 RTW89_RU106,
							 ntx, ch + 2);
	lmt_ru->ru106[3] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							 RTW89_RU106,
							 ntx, ch + 6);
}

static void
rtw89_phy_fill_txpwr_limit_ru_160m_ax(struct rtw89_dev *rtwdev,
				      struct rtw89_txpwr_limit_ru_ax *lmt_ru,
				      u8 band, u8 ntx, u8 ch)
{
	static const int ofst[] = { -14, -10, -6, -2, 2, 6, 10, 14 };
	int i;

	static_assert(ARRAY_SIZE(ofst) == RTW89_RU_SEC_NUM_AX);
	for (i = 0; i < RTW89_RU_SEC_NUM_AX; i++) {
		lmt_ru->ru26[i] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
								RTW89_RU26,
								ntx,
								ch + ofst[i]);
		lmt_ru->ru52[i] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
								RTW89_RU52,
								ntx,
								ch + ofst[i]);
		lmt_ru->ru106[i] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
								 RTW89_RU106,
								 ntx,
								 ch + ofst[i]);
	}
}

static
void rtw89_phy_fill_txpwr_limit_ru_ax(struct rtw89_dev *rtwdev,
				      const struct rtw89_chan *chan,
				      struct rtw89_txpwr_limit_ru_ax *lmt_ru,
				      u8 ntx)
{
	u8 band = chan->band_type;
	u8 ch = chan->channel;
	u8 bw = chan->band_width;

	memset(lmt_ru, 0, sizeof(*lmt_ru));

	switch (bw) {
	case RTW89_CHANNEL_WIDTH_20:
		rtw89_phy_fill_txpwr_limit_ru_20m_ax(rtwdev, lmt_ru, band, ntx,
						     ch);
		break;
	case RTW89_CHANNEL_WIDTH_40:
		rtw89_phy_fill_txpwr_limit_ru_40m_ax(rtwdev, lmt_ru, band, ntx,
						     ch);
		break;
	case RTW89_CHANNEL_WIDTH_80:
		rtw89_phy_fill_txpwr_limit_ru_80m_ax(rtwdev, lmt_ru, band, ntx,
						     ch);
		break;
	case RTW89_CHANNEL_WIDTH_160:
		rtw89_phy_fill_txpwr_limit_ru_160m_ax(rtwdev, lmt_ru, band, ntx,
						      ch);
		break;
	}
}

static void rtw89_phy_set_txpwr_byrate_ax(struct rtw89_dev *rtwdev,
					  const struct rtw89_chan *chan,
					  enum rtw89_phy_idx phy_idx)
{
	u8 max_nss_num = rtwdev->chip->rf_path_num;
	static const u8 rs[] = {
		RTW89_RS_CCK,
		RTW89_RS_OFDM,
		RTW89_RS_MCS,
		RTW89_RS_HEDCM,
	};
	struct rtw89_rate_desc cur = {};
	u8 band = chan->band_type;
	u8 ch = chan->channel;
	u32 addr, val;
	s8 v[4] = {};
	u8 i;

	rtw89_debug(rtwdev, RTW89_DBG_TXPWR,
		    "[TXPWR] set txpwr byrate with ch=%d\n", ch);

	BUILD_BUG_ON(rtw89_rs_idx_num_ax[RTW89_RS_CCK] % 4);
	BUILD_BUG_ON(rtw89_rs_idx_num_ax[RTW89_RS_OFDM] % 4);
	BUILD_BUG_ON(rtw89_rs_idx_num_ax[RTW89_RS_MCS] % 4);
	BUILD_BUG_ON(rtw89_rs_idx_num_ax[RTW89_RS_HEDCM] % 4);

	addr = R_AX_PWR_BY_RATE;
	for (cur.nss = 0; cur.nss < max_nss_num; cur.nss++) {
		for (i = 0; i < ARRAY_SIZE(rs); i++) {
			if (cur.nss >= rtw89_rs_nss_num_ax[rs[i]])
				continue;

			cur.rs = rs[i];
			for (cur.idx = 0; cur.idx < rtw89_rs_idx_num_ax[rs[i]];
			     cur.idx++) {
				v[cur.idx % 4] =
					rtw89_phy_read_txpwr_byrate(rtwdev,
								    band, 0,
								    &cur);

				if ((cur.idx + 1) % 4)
					continue;

				val = FIELD_PREP(GENMASK(7, 0), v[0]) |
				      FIELD_PREP(GENMASK(15, 8), v[1]) |
				      FIELD_PREP(GENMASK(23, 16), v[2]) |
				      FIELD_PREP(GENMASK(31, 24), v[3]);

				rtw89_mac_txpwr_write32(rtwdev, phy_idx, addr,
							val);
				addr += 4;
			}
		}
	}
}

static
void rtw89_phy_set_txpwr_offset_ax(struct rtw89_dev *rtwdev,
				   const struct rtw89_chan *chan,
				   enum rtw89_phy_idx phy_idx)
{
	struct rtw89_rate_desc desc = {
		.nss = RTW89_NSS_1,
		.rs = RTW89_RS_OFFSET,
	};
	u8 band = chan->band_type;
	s8 v[RTW89_RATE_OFFSET_NUM_AX] = {};
	u32 val;

	rtw89_debug(rtwdev, RTW89_DBG_TXPWR, "[TXPWR] set txpwr offset\n");

	for (desc.idx = 0; desc.idx < RTW89_RATE_OFFSET_NUM_AX; desc.idx++)
		v[desc.idx] = rtw89_phy_read_txpwr_byrate(rtwdev, band, 0, &desc);

	BUILD_BUG_ON(RTW89_RATE_OFFSET_NUM_AX != 5);
	val = FIELD_PREP(GENMASK(3, 0), v[0]) |
	      FIELD_PREP(GENMASK(7, 4), v[1]) |
	      FIELD_PREP(GENMASK(11, 8), v[2]) |
	      FIELD_PREP(GENMASK(15, 12), v[3]) |
	      FIELD_PREP(GENMASK(19, 16), v[4]);

	rtw89_mac_txpwr_write32_mask(rtwdev, phy_idx, R_AX_PWR_RATE_OFST_CTRL,
				     GENMASK(19, 0), val);
}

static void rtw89_phy_set_txpwr_limit_ax(struct rtw89_dev *rtwdev,
					 const struct rtw89_chan *chan,
					 enum rtw89_phy_idx phy_idx)
{
	u8 max_ntx_num = rtwdev->chip->rf_path_num;
	struct rtw89_txpwr_limit_ax lmt;
	u8 ch = chan->channel;
	u8 bw = chan->band_width;
	const s8 *ptr;
	u32 addr, val;
	u8 i, j;

	rtw89_debug(rtwdev, RTW89_DBG_TXPWR,
		    "[TXPWR] set txpwr limit with ch=%d bw=%d\n", ch, bw);

	BUILD_BUG_ON(sizeof(struct rtw89_txpwr_limit_ax) !=
		     RTW89_TXPWR_LMT_PAGE_SIZE_AX);

	addr = R_AX_PWR_LMT;
	for (i = 0; i < max_ntx_num; i++) {
		rtw89_phy_fill_txpwr_limit_ax(rtwdev, chan, &lmt, i);

		ptr = (s8 *)&lmt;
		for (j = 0; j < RTW89_TXPWR_LMT_PAGE_SIZE_AX;
		     j += 4, addr += 4, ptr += 4) {
			val = FIELD_PREP(GENMASK(7, 0), ptr[0]) |
			      FIELD_PREP(GENMASK(15, 8), ptr[1]) |
			      FIELD_PREP(GENMASK(23, 16), ptr[2]) |
			      FIELD_PREP(GENMASK(31, 24), ptr[3]);

			rtw89_mac_txpwr_write32(rtwdev, phy_idx, addr, val);
		}
	}
}

static void rtw89_phy_set_txpwr_limit_ru_ax(struct rtw89_dev *rtwdev,
					    const struct rtw89_chan *chan,
					    enum rtw89_phy_idx phy_idx)
{
	u8 max_ntx_num = rtwdev->chip->rf_path_num;
	struct rtw89_txpwr_limit_ru_ax lmt_ru;
	u8 ch = chan->channel;
	u8 bw = chan->band_width;
	const s8 *ptr;
	u32 addr, val;
	u8 i, j;

	rtw89_debug(rtwdev, RTW89_DBG_TXPWR,
		    "[TXPWR] set txpwr limit ru with ch=%d bw=%d\n", ch, bw);

	BUILD_BUG_ON(sizeof(struct rtw89_txpwr_limit_ru_ax) !=
		     RTW89_TXPWR_LMT_RU_PAGE_SIZE_AX);

	addr = R_AX_PWR_RU_LMT;
	for (i = 0; i < max_ntx_num; i++) {
		rtw89_phy_fill_txpwr_limit_ru_ax(rtwdev, chan, &lmt_ru, i);

		ptr = (s8 *)&lmt_ru;
		for (j = 0; j < RTW89_TXPWR_LMT_RU_PAGE_SIZE_AX;
		     j += 4, addr += 4, ptr += 4) {
			val = FIELD_PREP(GENMASK(7, 0), ptr[0]) |
			      FIELD_PREP(GENMASK(15, 8), ptr[1]) |
			      FIELD_PREP(GENMASK(23, 16), ptr[2]) |
			      FIELD_PREP(GENMASK(31, 24), ptr[3]);

			rtw89_mac_txpwr_write32(rtwdev, phy_idx, addr, val);
		}
	}
}

struct rtw89_phy_iter_ra_data {
	struct rtw89_dev *rtwdev;
	struct sk_buff *c2h;
};

static void __rtw89_phy_c2h_ra_rpt_iter(struct rtw89_sta_link *rtwsta_link,
					struct ieee80211_link_sta *link_sta,
					struct rtw89_phy_iter_ra_data *ra_data)
{
	struct rtw89_dev *rtwdev = ra_data->rtwdev;
	const struct rtw89_c2h_ra_rpt *c2h =
		(const struct rtw89_c2h_ra_rpt *)ra_data->c2h->data;
	struct rtw89_ra_report *ra_report = &rtwsta_link->ra_report;
	const struct rtw89_chip_info *chip = rtwdev->chip;
	bool format_v1 = chip->chip_gen == RTW89_CHIP_BE;
	u8 mode, rate, bw, giltf, mac_id;
	u16 legacy_bitrate;
	bool valid;
	u8 mcs = 0;
	u8 t;

	mac_id = le32_get_bits(c2h->w2, RTW89_C2H_RA_RPT_W2_MACID);
	if (mac_id != rtwsta_link->mac_id)
		return;

	rate = le32_get_bits(c2h->w3, RTW89_C2H_RA_RPT_W3_MCSNSS);
	bw = le32_get_bits(c2h->w3, RTW89_C2H_RA_RPT_W3_BW);
	giltf = le32_get_bits(c2h->w3, RTW89_C2H_RA_RPT_W3_GILTF);
	mode = le32_get_bits(c2h->w3, RTW89_C2H_RA_RPT_W3_MD_SEL);

	if (format_v1) {
		t = le32_get_bits(c2h->w2, RTW89_C2H_RA_RPT_W2_MCSNSS_B7);
		rate |= u8_encode_bits(t, BIT(7));
		t = le32_get_bits(c2h->w3, RTW89_C2H_RA_RPT_W3_BW_B2);
		bw |= u8_encode_bits(t, BIT(2));
		t = le32_get_bits(c2h->w3, RTW89_C2H_RA_RPT_W3_MD_SEL_B2);
		mode |= u8_encode_bits(t, BIT(2));
	}

	if (mode == RTW89_RA_RPT_MODE_LEGACY) {
		valid = rtw89_ra_report_to_bitrate(rtwdev, rate, &legacy_bitrate);
		if (!valid)
			return;
	}

	memset(&ra_report->txrate, 0, sizeof(ra_report->txrate));

	switch (mode) {
	case RTW89_RA_RPT_MODE_LEGACY:
		ra_report->txrate.legacy = legacy_bitrate;
		break;
	case RTW89_RA_RPT_MODE_HT:
		ra_report->txrate.flags |= RATE_INFO_FLAGS_MCS;
		if (RTW89_CHK_FW_FEATURE(OLD_HT_RA_FORMAT, &rtwdev->fw))
			rate = RTW89_MK_HT_RATE(FIELD_GET(RTW89_RA_RATE_MASK_NSS, rate),
						FIELD_GET(RTW89_RA_RATE_MASK_MCS, rate));
		else
			rate = FIELD_GET(RTW89_RA_RATE_MASK_HT_MCS, rate);
		ra_report->txrate.mcs = rate;
		if (giltf)
			ra_report->txrate.flags |= RATE_INFO_FLAGS_SHORT_GI;
		mcs = ra_report->txrate.mcs & 0x07;
		break;
	case RTW89_RA_RPT_MODE_VHT:
		ra_report->txrate.flags |= RATE_INFO_FLAGS_VHT_MCS;
		ra_report->txrate.mcs = format_v1 ?
			u8_get_bits(rate, RTW89_RA_RATE_MASK_MCS_V1) :
			u8_get_bits(rate, RTW89_RA_RATE_MASK_MCS);
		ra_report->txrate.nss = format_v1 ?
			u8_get_bits(rate, RTW89_RA_RATE_MASK_NSS_V1) + 1 :
			u8_get_bits(rate, RTW89_RA_RATE_MASK_NSS) + 1;
		if (giltf)
			ra_report->txrate.flags |= RATE_INFO_FLAGS_SHORT_GI;
		mcs = ra_report->txrate.mcs;
		break;
	case RTW89_RA_RPT_MODE_HE:
		ra_report->txrate.flags |= RATE_INFO_FLAGS_HE_MCS;
		ra_report->txrate.mcs = format_v1 ?
			u8_get_bits(rate, RTW89_RA_RATE_MASK_MCS_V1) :
			u8_get_bits(rate, RTW89_RA_RATE_MASK_MCS);
		ra_report->txrate.nss  = format_v1 ?
			u8_get_bits(rate, RTW89_RA_RATE_MASK_NSS_V1) + 1 :
			u8_get_bits(rate, RTW89_RA_RATE_MASK_NSS) + 1;
		if (giltf == RTW89_GILTF_2XHE08 || giltf == RTW89_GILTF_1XHE08)
			ra_report->txrate.he_gi = NL80211_RATE_INFO_HE_GI_0_8;
		else if (giltf == RTW89_GILTF_2XHE16 || giltf == RTW89_GILTF_1XHE16)
			ra_report->txrate.he_gi = NL80211_RATE_INFO_HE_GI_1_6;
		else
			ra_report->txrate.he_gi = NL80211_RATE_INFO_HE_GI_3_2;
		mcs = ra_report->txrate.mcs;
		break;
	case RTW89_RA_RPT_MODE_EHT:
		ra_report->txrate.flags |= RATE_INFO_FLAGS_EHT_MCS;
		ra_report->txrate.mcs = u8_get_bits(rate, RTW89_RA_RATE_MASK_MCS_V1);
		ra_report->txrate.nss = u8_get_bits(rate, RTW89_RA_RATE_MASK_NSS_V1) + 1;
		if (giltf == RTW89_GILTF_2XHE08 || giltf == RTW89_GILTF_1XHE08)
			ra_report->txrate.eht_gi = NL80211_RATE_INFO_EHT_GI_0_8;
		else if (giltf == RTW89_GILTF_2XHE16 || giltf == RTW89_GILTF_1XHE16)
			ra_report->txrate.eht_gi = NL80211_RATE_INFO_EHT_GI_1_6;
		else
			ra_report->txrate.eht_gi = NL80211_RATE_INFO_EHT_GI_3_2;
		mcs = ra_report->txrate.mcs;
		break;
	}

	ra_report->txrate.bw = rtw89_hw_to_rate_info_bw(bw);
	ra_report->bit_rate = cfg80211_calculate_bitrate(&ra_report->txrate);
	ra_report->hw_rate = format_v1 ?
			     u16_encode_bits(mode, RTW89_HW_RATE_V1_MASK_MOD) |
			     u16_encode_bits(rate, RTW89_HW_RATE_V1_MASK_VAL) :
			     u16_encode_bits(mode, RTW89_HW_RATE_MASK_MOD) |
			     u16_encode_bits(rate, RTW89_HW_RATE_MASK_VAL);
	ra_report->might_fallback_legacy = mcs <= 2;
	link_sta->agg.max_rc_amsdu_len = get_max_amsdu_len(rtwdev, ra_report);
	rtwsta_link->max_agg_wait = link_sta->agg.max_rc_amsdu_len / 1500 - 1;
}

static void rtw89_phy_c2h_ra_rpt_iter(void *data, struct ieee80211_sta *sta)
{
	struct rtw89_phy_iter_ra_data *ra_data = (struct rtw89_phy_iter_ra_data *)data;
	struct rtw89_sta *rtwsta = sta_to_rtwsta(sta);
	struct rtw89_sta_link *rtwsta_link;
	struct ieee80211_link_sta *link_sta;
	unsigned int link_id;

	rcu_read_lock();

	rtw89_sta_for_each_link(rtwsta, rtwsta_link, link_id) {
		link_sta = rtw89_sta_rcu_dereference_link(rtwsta_link, false);
		__rtw89_phy_c2h_ra_rpt_iter(rtwsta_link, link_sta, ra_data);
	}

	rcu_read_unlock();
}

static void
rtw89_phy_c2h_ra_rpt(struct rtw89_dev *rtwdev, struct sk_buff *c2h, u32 len)
{
	struct rtw89_phy_iter_ra_data ra_data;

	ra_data.rtwdev = rtwdev;
	ra_data.c2h = c2h;
	ieee80211_iterate_stations_atomic(rtwdev->hw,
					  rtw89_phy_c2h_ra_rpt_iter,
					  &ra_data);
}

static
void (* const rtw89_phy_c2h_ra_handler[])(struct rtw89_dev *rtwdev,
					  struct sk_buff *c2h, u32 len) = {
	[RTW89_PHY_C2H_FUNC_STS_RPT] = rtw89_phy_c2h_ra_rpt,
	[RTW89_PHY_C2H_FUNC_MU_GPTBL_RPT] = NULL,
	[RTW89_PHY_C2H_FUNC_TXSTS] = NULL,
};

static void rtw89_phy_c2h_rfk_rpt_log(struct rtw89_dev *rtwdev,
				      enum rtw89_phy_c2h_rfk_log_func func,
				      void *content, u16 len)
{
	struct rtw89_c2h_rf_txgapk_rpt_log *txgapk;
	struct rtw89_c2h_rf_rxdck_rpt_log *rxdck;
	struct rtw89_c2h_rf_dack_rpt_log *dack;
	struct rtw89_c2h_rf_tssi_rpt_log *tssi;
	struct rtw89_c2h_rf_dpk_rpt_log *dpk;
	struct rtw89_c2h_rf_iqk_rpt_log *iqk;
	int i, j, k;

	switch (func) {
	case RTW89_PHY_C2H_RFK_LOG_FUNC_IQK:
		if (len != sizeof(*iqk))
			goto out;

		iqk = content;
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK] iqk->is_iqk_init = %x\n", iqk->is_iqk_init);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK] iqk->is_reload = %x\n", iqk->is_reload);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK] iqk->is_nbiqk = %x\n", iqk->is_nbiqk);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK] iqk->txiqk_en = %x\n", iqk->txiqk_en);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK] iqk->rxiqk_en = %x\n", iqk->rxiqk_en);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK] iqk->lok_en = %x\n", iqk->lok_en);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK] iqk->iqk_xym_en = %x\n", iqk->iqk_xym_en);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK] iqk->iqk_sram_en = %x\n", iqk->iqk_sram_en);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK] iqk->iqk_fft_en = %x\n", iqk->iqk_fft_en);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK] iqk->is_fw_iqk = %x\n", iqk->is_fw_iqk);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK] iqk->is_iqk_enable = %x\n", iqk->is_iqk_enable);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK] iqk->iqk_cfir_en = %x\n", iqk->iqk_cfir_en);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK] iqk->thermal_rek_en = %x\n", iqk->thermal_rek_en);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK] iqk->version = %x\n", iqk->version);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK] iqk->phy = %x\n", iqk->phy);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK] iqk->fwk_status = %x\n", iqk->fwk_status);

		for (i = 0; i < 2; i++) {
			rtw89_debug(rtwdev, RTW89_DBG_RFK,
				    "[IQK] ======== Path %x  ========\n", i);
			rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK] iqk->iqk_band[%d] = %x\n",
				    i, iqk->iqk_band[i]);
			rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK] iqk->iqk_ch[%d] = %x\n",
				    i, iqk->iqk_ch[i]);
			rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK] iqk->iqk_bw[%d] = %x\n",
				    i, iqk->iqk_bw[i]);
			rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK] iqk->lok_idac[%d] = %x\n",
				    i, le32_to_cpu(iqk->lok_idac[i]));
			rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK] iqk->lok_vbuf[%d] = %x\n",
				    i, le32_to_cpu(iqk->lok_vbuf[i]));
			rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK] iqk->iqk_tx_fail[%d] = %x\n",
				    i, iqk->iqk_tx_fail[i]);
			rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK] iqk->iqk_rx_fail[%d] = %x\n",
				    i, iqk->iqk_rx_fail[i]);
			for (j = 0; j < 4; j++)
				rtw89_debug(rtwdev, RTW89_DBG_RFK,
					    "[IQK] iqk->rftxgain[%d][%d] = %x\n",
					    i, j, le32_to_cpu(iqk->rftxgain[i][j]));
			for (j = 0; j < 4; j++)
				rtw89_debug(rtwdev, RTW89_DBG_RFK,
					    "[IQK] iqk->tx_xym[%d][%d] = %x\n",
					    i, j, le32_to_cpu(iqk->tx_xym[i][j]));
			for (j = 0; j < 4; j++)
				rtw89_debug(rtwdev, RTW89_DBG_RFK,
					    "[IQK] iqk->rfrxgain[%d][%d] = %x\n",
					    i, j, le32_to_cpu(iqk->rfrxgain[i][j]));
			for (j = 0; j < 4; j++)
				rtw89_debug(rtwdev, RTW89_DBG_RFK,
					    "[IQK] iqk->rx_xym[%d][%d] = %x\n",
					    i, j, le32_to_cpu(iqk->rx_xym[i][j]));
		}
		return;
	case RTW89_PHY_C2H_RFK_LOG_FUNC_DPK:
		if (len != sizeof(*dpk))
			goto out;

		dpk = content;
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "DPK ver:%d idx:%2ph band:%2ph bw:%2ph ch:%2ph path:%2ph\n",
			    dpk->ver, dpk->idx, dpk->band, dpk->bw, dpk->ch, dpk->path_ok);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "DPK txagc:%2ph ther:%2ph gs:%2ph dc_i:%4ph dc_q:%4ph\n",
			    dpk->txagc, dpk->ther, dpk->gs, dpk->dc_i, dpk->dc_q);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "DPK corr_v:%2ph corr_i:%2ph to:%2ph ov:%2ph\n",
			    dpk->corr_val, dpk->corr_idx, dpk->is_timeout, dpk->rxbb_ov);
		return;
	case RTW89_PHY_C2H_RFK_LOG_FUNC_DACK:
		if (len != sizeof(*dack))
			goto out;

		dack = content;

		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]FWDACK SUMMARY!!!!!\n");
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[DACK]FWDACK ver = 0x%x, FWDACK rpt_ver = 0x%x, driver rpt_ver = 0x%x\n",
			    dack->fwdack_ver, dack->fwdack_info_ver, 0x2);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[DACK]timeout code = [0x%x 0x%x 0x%x 0x%x 0x%x]\n",
			    dack->addck_timeout, dack->cdack_timeout, dack->dadck_timeout,
			    dack->adgaink_timeout, dack->msbk_timeout);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[DACK]DACK fail = 0x%x\n", dack->dack_fail);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[DACK]S0 WBADCK = [0x%x]\n", dack->wbdck_d[0]);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[DACK]S1 WBADCK = [0x%x]\n", dack->wbdck_d[1]);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[DACK]DRCK = [0x%x]\n", dack->rck_d);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S0 CDACK ic = [0x%x, 0x%x]\n",
			    dack->cdack_d[0][0][0], dack->cdack_d[0][0][1]);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S0 CDACK qc = [0x%x, 0x%x]\n",
			    dack->cdack_d[0][1][0], dack->cdack_d[0][1][1]);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S1 CDACK ic = [0x%x, 0x%x]\n",
			    dack->cdack_d[1][0][0], dack->cdack_d[1][0][1]);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S1 CDACK qc = [0x%x, 0x%x]\n",
			    dack->cdack_d[1][1][0], dack->cdack_d[1][1][1]);

		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S0 ADC_DCK ic = [0x%x, 0x%x]\n",
			    ((u32)dack->addck2_hd[0][0][0] << 8) | dack->addck2_ld[0][0][0],
			    ((u32)dack->addck2_hd[0][0][1] << 8) | dack->addck2_ld[0][0][1]);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S0 ADC_DCK qc = [0x%x, 0x%x]\n",
			    ((u32)dack->addck2_hd[0][1][0] << 8) | dack->addck2_ld[0][1][0],
			    ((u32)dack->addck2_hd[0][1][1] << 8) | dack->addck2_ld[0][1][1]);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S1 ADC_DCK ic = [0x%x, 0x%x]\n",
			    ((u32)dack->addck2_hd[1][0][0] << 8) | dack->addck2_ld[1][0][0],
			    ((u32)dack->addck2_hd[1][0][1] << 8) | dack->addck2_ld[1][0][1]);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S1 ADC_DCK qc = [0x%x, 0x%x]\n",
			    ((u32)dack->addck2_hd[1][1][0] << 8) | dack->addck2_ld[1][1][0],
			    ((u32)dack->addck2_hd[1][1][1] << 8) | dack->addck2_ld[1][1][1]);

		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S0 ADC_GAINK ic = 0x%x, qc = 0x%x\n",
			    dack->adgaink_d[0][0], dack->adgaink_d[0][1]);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S1 ADC_GAINK ic = 0x%x, qc = 0x%x\n",
			    dack->adgaink_d[1][0], dack->adgaink_d[1][1]);

		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S0 DAC_DCK ic = 0x%x, qc = 0x%x\n",
			    dack->dadck_d[0][0], dack->dadck_d[0][1]);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S1 DAC_DCK ic = 0x%x, qc = 0x%x\n",
			    dack->dadck_d[1][0], dack->dadck_d[1][1]);

		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S0 biask iqc = 0x%x\n",
			    ((u32)dack->biask_hd[0][0] << 8) | dack->biask_ld[0][0]);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S1 biask iqc = 0x%x\n",
			    ((u32)dack->biask_hd[1][0] << 8) | dack->biask_ld[1][0]);

		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S0 MSBK ic:\n");
		for (i = 0; i < 0x10; i++)
			rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]0x%x\n",
				    dack->msbk_d[0][0][i]);

		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S0 MSBK qc:\n");
		for (i = 0; i < 0x10; i++)
			rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]0x%x\n",
				    dack->msbk_d[0][1][i]);

		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S1 MSBK ic:\n");
		for (i = 0; i < 0x10; i++)
			rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]0x%x\n",
				    dack->msbk_d[1][0][i]);

		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S1 MSBK qc:\n");
		for (i = 0; i < 0x10; i++)
			rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]0x%x\n",
				    dack->msbk_d[1][1][i]);
		return;
	case RTW89_PHY_C2H_RFK_LOG_FUNC_RXDCK:
		if (len != sizeof(*rxdck))
			goto out;

		rxdck = content;
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "RXDCK ver:%d band:%2ph bw:%2ph ch:%2ph to:%2ph\n",
			    rxdck->ver, rxdck->band, rxdck->bw, rxdck->ch,
			    rxdck->timeout);
		return;
	case RTW89_PHY_C2H_RFK_LOG_FUNC_TSSI:
		if (len != sizeof(*tssi))
			goto out;

		tssi = content;
		for (i = 0; i < 2; i++) {
			for (j = 0; j < 2; j++) {
				for (k = 0; k < 4; k++) {
					rtw89_debug(rtwdev, RTW89_DBG_RFK,
						    "[TSSI] alignment_power_cw_h[%d][%d][%d]=%d\n",
						    i, j, k, tssi->alignment_power_cw_h[i][j][k]);
					rtw89_debug(rtwdev, RTW89_DBG_RFK,
						    "[TSSI] alignment_power_cw_l[%d][%d][%d]=%d\n",
						    i, j, k, tssi->alignment_power_cw_l[i][j][k]);
					rtw89_debug(rtwdev, RTW89_DBG_RFK,
						    "[TSSI] alignment_power[%d][%d][%d]=%d\n",
						    i, j, k, tssi->alignment_power[i][j][k]);
					rtw89_debug(rtwdev, RTW89_DBG_RFK,
						    "[TSSI] alignment_power_cw[%d][%d][%d]=%d\n",
						    i, j, k,
						    (tssi->alignment_power_cw_h[i][j][k] << 8) +
						     tssi->alignment_power_cw_l[i][j][k]);
				}

				rtw89_debug(rtwdev, RTW89_DBG_RFK,
					    "[TSSI] tssi_alimk_state[%d][%d]=%d\n",
					    i, j, tssi->tssi_alimk_state[i][j]);
				rtw89_debug(rtwdev, RTW89_DBG_RFK,
					    "[TSSI] default_txagc_offset[%d]=%d\n",
					    j, tssi->default_txagc_offset[0][j]);
			}
		}
		return;
	case RTW89_PHY_C2H_RFK_LOG_FUNC_TXGAPK:
		if (len != sizeof(*txgapk))
			goto out;

		txgapk = content;
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[TXGAPK]rpt r0x8010[0]=0x%x, r0x8010[1]=0x%x\n",
			    le32_to_cpu(txgapk->r0x8010[0]),
			    le32_to_cpu(txgapk->r0x8010[1]));
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[TXGAPK]rpt chk_id = %d\n",
			    txgapk->chk_id);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[TXGAPK]rpt chk_cnt = %d\n",
			    le32_to_cpu(txgapk->chk_cnt));
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[TXGAPK]rpt ver = 0x%x\n",
			    txgapk->ver);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[TXGAPK]rpt rsv1 = %d\n",
			    txgapk->rsv1);

		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[TXGAPK]rpt track_d[0] = %*ph\n",
			    (int)sizeof(txgapk->track_d[0]), txgapk->track_d[0]);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[TXGAPK]rpt power_d[0] = %*ph\n",
			    (int)sizeof(txgapk->power_d[0]), txgapk->power_d[0]);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[TXGAPK]rpt track_d[1] = %*ph\n",
			    (int)sizeof(txgapk->track_d[1]), txgapk->track_d[1]);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[TXGAPK]rpt power_d[1] = %*ph\n",
			    (int)sizeof(txgapk->power_d[1]), txgapk->power_d[1]);
		return;
	default:
		break;
	}

out:
	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "unexpected RFK func %d report log with length %d\n", func, len);
}

static bool rtw89_phy_c2h_rfk_run_log(struct rtw89_dev *rtwdev,
				      enum rtw89_phy_c2h_rfk_log_func func,
				      void *content, u16 len)
{
	struct rtw89_fw_elm_info *elm_info = &rtwdev->fw.elm_info;
	const struct rtw89_c2h_rf_run_log *log = content;
	const struct rtw89_fw_element_hdr *elm;
	u32 fmt_idx;
	u16 offset;

	if (sizeof(*log) != len)
		return false;

	if (!elm_info->rfk_log_fmt)
		return false;

	elm = elm_info->rfk_log_fmt->elm[func];
	fmt_idx = le32_to_cpu(log->fmt_idx);
	if (!elm || fmt_idx >= elm->u.rfk_log_fmt.nr)
		return false;

	offset = le16_to_cpu(elm->u.rfk_log_fmt.offset[fmt_idx]);
	if (offset == 0)
		return false;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, &elm->u.common.contents[offset],
		    le32_to_cpu(log->arg[0]), le32_to_cpu(log->arg[1]),
		    le32_to_cpu(log->arg[2]), le32_to_cpu(log->arg[3]));

	return true;
}

static void rtw89_phy_c2h_rfk_log(struct rtw89_dev *rtwdev, struct sk_buff *c2h,
				  u32 len, enum rtw89_phy_c2h_rfk_log_func func,
				  const char *rfk_name)
{
	struct rtw89_c2h_hdr *c2h_hdr = (struct rtw89_c2h_hdr *)c2h->data;
	struct rtw89_c2h_rf_log_hdr *log_hdr;
	void *log_ptr = c2h_hdr;
	u16 content_len;
	u16 chunk_len;
	bool handled;

	if (!rtw89_debug_is_enabled(rtwdev, RTW89_DBG_RFK))
		return;

	log_ptr += sizeof(*c2h_hdr);
	len -= sizeof(*c2h_hdr);

	while (len > sizeof(*log_hdr)) {
		log_hdr = log_ptr;
		content_len = le16_to_cpu(log_hdr->len);
		chunk_len = content_len + sizeof(*log_hdr);

		if (chunk_len > len)
			break;

		switch (log_hdr->type) {
		case RTW89_RF_RUN_LOG:
			handled = rtw89_phy_c2h_rfk_run_log(rtwdev, func,
							    log_hdr->content, content_len);
			if (handled)
				break;

			rtw89_debug(rtwdev, RTW89_DBG_RFK, "%s run: %*ph\n",
				    rfk_name, content_len, log_hdr->content);
			break;
		case RTW89_RF_RPT_LOG:
			rtw89_phy_c2h_rfk_rpt_log(rtwdev, func,
						  log_hdr->content, content_len);
			break;
		default:
			return;
		}

		log_ptr += chunk_len;
		len -= chunk_len;
	}
}

static void
rtw89_phy_c2h_rfk_log_iqk(struct rtw89_dev *rtwdev, struct sk_buff *c2h, u32 len)
{
	rtw89_phy_c2h_rfk_log(rtwdev, c2h, len,
			      RTW89_PHY_C2H_RFK_LOG_FUNC_IQK, "IQK");
}

static void
rtw89_phy_c2h_rfk_log_dpk(struct rtw89_dev *rtwdev, struct sk_buff *c2h, u32 len)
{
	rtw89_phy_c2h_rfk_log(rtwdev, c2h, len,
			      RTW89_PHY_C2H_RFK_LOG_FUNC_DPK, "DPK");
}

static void
rtw89_phy_c2h_rfk_log_dack(struct rtw89_dev *rtwdev, struct sk_buff *c2h, u32 len)
{
	rtw89_phy_c2h_rfk_log(rtwdev, c2h, len,
			      RTW89_PHY_C2H_RFK_LOG_FUNC_DACK, "DACK");
}

static void
rtw89_phy_c2h_rfk_log_rxdck(struct rtw89_dev *rtwdev, struct sk_buff *c2h, u32 len)
{
	rtw89_phy_c2h_rfk_log(rtwdev, c2h, len,
			      RTW89_PHY_C2H_RFK_LOG_FUNC_RXDCK, "RX_DCK");
}

static void
rtw89_phy_c2h_rfk_log_tssi(struct rtw89_dev *rtwdev, struct sk_buff *c2h, u32 len)
{
	rtw89_phy_c2h_rfk_log(rtwdev, c2h, len,
			      RTW89_PHY_C2H_RFK_LOG_FUNC_TSSI, "TSSI");
}

static void
rtw89_phy_c2h_rfk_log_txgapk(struct rtw89_dev *rtwdev, struct sk_buff *c2h, u32 len)
{
	rtw89_phy_c2h_rfk_log(rtwdev, c2h, len,
			      RTW89_PHY_C2H_RFK_LOG_FUNC_TXGAPK, "TXGAPK");
}

static
void (* const rtw89_phy_c2h_rfk_log_handler[])(struct rtw89_dev *rtwdev,
					       struct sk_buff *c2h, u32 len) = {
	[RTW89_PHY_C2H_RFK_LOG_FUNC_IQK] = rtw89_phy_c2h_rfk_log_iqk,
	[RTW89_PHY_C2H_RFK_LOG_FUNC_DPK] = rtw89_phy_c2h_rfk_log_dpk,
	[RTW89_PHY_C2H_RFK_LOG_FUNC_DACK] = rtw89_phy_c2h_rfk_log_dack,
	[RTW89_PHY_C2H_RFK_LOG_FUNC_RXDCK] = rtw89_phy_c2h_rfk_log_rxdck,
	[RTW89_PHY_C2H_RFK_LOG_FUNC_TSSI] = rtw89_phy_c2h_rfk_log_tssi,
	[RTW89_PHY_C2H_RFK_LOG_FUNC_TXGAPK] = rtw89_phy_c2h_rfk_log_txgapk,
};

static
void rtw89_phy_rfk_report_prep(struct rtw89_dev *rtwdev)
{
	struct rtw89_rfk_wait_info *wait = &rtwdev->rfk_wait;

	wait->state = RTW89_RFK_STATE_START;
	wait->start_time = ktime_get();
	reinit_completion(&wait->completion);
}

static
int rtw89_phy_rfk_report_wait(struct rtw89_dev *rtwdev, const char *rfk_name,
			      unsigned int ms)
{
	struct rtw89_rfk_wait_info *wait = &rtwdev->rfk_wait;
	unsigned long time_left;

	/* Since we can't receive C2H event during SER, use a fixed delay. */
	if (test_bit(RTW89_FLAG_SER_HANDLING, rtwdev->flags)) {
		fsleep(1000 * ms / 2);
		goto out;
	}

	time_left = wait_for_completion_timeout(&wait->completion,
						msecs_to_jiffies(ms));
	if (time_left == 0) {
		rtw89_warn(rtwdev, "failed to wait RF %s\n", rfk_name);
		return -ETIMEDOUT;
	} else if (wait->state != RTW89_RFK_STATE_OK) {
		rtw89_warn(rtwdev, "failed to do RF %s result from state %d\n",
			   rfk_name, wait->state);
		return -EFAULT;
	}

out:
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "RF %s takes %lld ms to complete\n",
		    rfk_name, ktime_ms_delta(ktime_get(), wait->start_time));

	return 0;
}

static void
rtw89_phy_c2h_rfk_report_state(struct rtw89_dev *rtwdev, struct sk_buff *c2h, u32 len)
{
	const struct rtw89_c2h_rfk_report *report =
		(const struct rtw89_c2h_rfk_report *)c2h->data;
	struct rtw89_rfk_wait_info *wait = &rtwdev->rfk_wait;

	wait->state = report->state;
	wait->version = report->version;

	complete(&wait->completion);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "RFK report state %d with version %d (%*ph)\n",
		    wait->state, wait->version,
		    (int)(len - sizeof(report->hdr)), &report->state);
}

static void
rtw89_phy_c2h_rfk_log_tas_pwr(struct rtw89_dev *rtwdev, struct sk_buff *c2h, u32 len)
{
	const struct rtw89_c2h_rf_tas_info *rf_tas =
		(const struct rtw89_c2h_rf_tas_info *)c2h->data;
	const enum rtw89_sar_sources src = rtwdev->sar.src;
	struct rtw89_tas_info *tas = &rtwdev->tas;
	u64 linear = 0;
	u32 i, cur_idx;
	s16 txpwr;

	if (!tas->enable || src == RTW89_SAR_SOURCE_NONE)
		return;

	cur_idx = le32_to_cpu(rf_tas->cur_idx);
	for (i = 0; i < cur_idx; i++) {
		txpwr = (s16)le16_to_cpu(rf_tas->txpwr_history[i]);
		linear += rtw89_db_quarter_to_linear(txpwr);

		rtw89_debug(rtwdev, RTW89_DBG_SAR,
			    "tas: index: %u, txpwr: %d\n", i, txpwr);
	}

	if (cur_idx == 0)
		tas->instant_txpwr = rtw89_db_to_linear(0);
	else
		tas->instant_txpwr = DIV_ROUND_DOWN_ULL(linear, cur_idx);
}

static
void (* const rtw89_phy_c2h_rfk_report_handler[])(struct rtw89_dev *rtwdev,
						  struct sk_buff *c2h, u32 len) = {
	[RTW89_PHY_C2H_RFK_REPORT_FUNC_STATE] = rtw89_phy_c2h_rfk_report_state,
	[RTW89_PHY_C2H_RFK_LOG_TAS_PWR] = rtw89_phy_c2h_rfk_log_tas_pwr,
};

bool rtw89_phy_c2h_chk_atomic(struct rtw89_dev *rtwdev, u8 class, u8 func)
{
	switch (class) {
	case RTW89_PHY_C2H_RFK_LOG:
		switch (func) {
		case RTW89_PHY_C2H_RFK_LOG_FUNC_IQK:
		case RTW89_PHY_C2H_RFK_LOG_FUNC_DPK:
		case RTW89_PHY_C2H_RFK_LOG_FUNC_DACK:
		case RTW89_PHY_C2H_RFK_LOG_FUNC_RXDCK:
		case RTW89_PHY_C2H_RFK_LOG_FUNC_TSSI:
		case RTW89_PHY_C2H_RFK_LOG_FUNC_TXGAPK:
			return true;
		default:
			return false;
		}
	case RTW89_PHY_C2H_RFK_REPORT:
		switch (func) {
		case RTW89_PHY_C2H_RFK_REPORT_FUNC_STATE:
			return true;
		default:
			return false;
		}
	default:
		return false;
	}
}

void rtw89_phy_c2h_handle(struct rtw89_dev *rtwdev, struct sk_buff *skb,
			  u32 len, u8 class, u8 func)
{
	void (*handler)(struct rtw89_dev *rtwdev,
			struct sk_buff *c2h, u32 len) = NULL;

	switch (class) {
	case RTW89_PHY_C2H_CLASS_RA:
		if (func < RTW89_PHY_C2H_FUNC_RA_MAX)
			handler = rtw89_phy_c2h_ra_handler[func];
		break;
	case RTW89_PHY_C2H_RFK_LOG:
		if (func < ARRAY_SIZE(rtw89_phy_c2h_rfk_log_handler))
			handler = rtw89_phy_c2h_rfk_log_handler[func];
		break;
	case RTW89_PHY_C2H_RFK_REPORT:
		if (func < ARRAY_SIZE(rtw89_phy_c2h_rfk_report_handler))
			handler = rtw89_phy_c2h_rfk_report_handler[func];
		break;
	case RTW89_PHY_C2H_CLASS_DM:
		if (func == RTW89_PHY_C2H_DM_FUNC_LOWRT_RTY)
			return;
		fallthrough;
	default:
		rtw89_info(rtwdev, "PHY c2h class %d not support\n", class);
		return;
	}
	if (!handler) {
		rtw89_info(rtwdev, "PHY c2h class %d func %d not support\n", class,
			   func);
		return;
	}
	handler(rtwdev, skb, len);
}

int rtw89_phy_rfk_pre_ntfy_and_wait(struct rtw89_dev *rtwdev,
				    enum rtw89_phy_idx phy_idx,
				    unsigned int ms)
{
	int ret;

	rtw89_phy_rfk_report_prep(rtwdev);

	ret = rtw89_fw_h2c_rf_pre_ntfy(rtwdev, phy_idx);
	if (ret)
		return ret;

	return rtw89_phy_rfk_report_wait(rtwdev, "PRE_NTFY", ms);
}
EXPORT_SYMBOL(rtw89_phy_rfk_pre_ntfy_and_wait);

int rtw89_phy_rfk_tssi_and_wait(struct rtw89_dev *rtwdev,
				enum rtw89_phy_idx phy_idx,
				const struct rtw89_chan *chan,
				enum rtw89_tssi_mode tssi_mode,
				unsigned int ms)
{
	int ret;

	rtw89_phy_rfk_report_prep(rtwdev);

	ret = rtw89_fw_h2c_rf_tssi(rtwdev, phy_idx, chan, tssi_mode);
	if (ret)
		return ret;

	return rtw89_phy_rfk_report_wait(rtwdev, "TSSI", ms);
}
EXPORT_SYMBOL(rtw89_phy_rfk_tssi_and_wait);

int rtw89_phy_rfk_iqk_and_wait(struct rtw89_dev *rtwdev,
			       enum rtw89_phy_idx phy_idx,
			       const struct rtw89_chan *chan,
			       unsigned int ms)
{
	int ret;

	rtw89_phy_rfk_report_prep(rtwdev);

	ret = rtw89_fw_h2c_rf_iqk(rtwdev, phy_idx, chan);
	if (ret)
		return ret;

	return rtw89_phy_rfk_report_wait(rtwdev, "IQK", ms);
}
EXPORT_SYMBOL(rtw89_phy_rfk_iqk_and_wait);

int rtw89_phy_rfk_dpk_and_wait(struct rtw89_dev *rtwdev,
			       enum rtw89_phy_idx phy_idx,
			       const struct rtw89_chan *chan,
			       unsigned int ms)
{
	int ret;

	rtw89_phy_rfk_report_prep(rtwdev);

	ret = rtw89_fw_h2c_rf_dpk(rtwdev, phy_idx, chan);
	if (ret)
		return ret;

	return rtw89_phy_rfk_report_wait(rtwdev, "DPK", ms);
}
EXPORT_SYMBOL(rtw89_phy_rfk_dpk_and_wait);

int rtw89_phy_rfk_txgapk_and_wait(struct rtw89_dev *rtwdev,
				  enum rtw89_phy_idx phy_idx,
				  const struct rtw89_chan *chan,
				  unsigned int ms)
{
	int ret;

	rtw89_phy_rfk_report_prep(rtwdev);

	ret = rtw89_fw_h2c_rf_txgapk(rtwdev, phy_idx, chan);
	if (ret)
		return ret;

	return rtw89_phy_rfk_report_wait(rtwdev, "TXGAPK", ms);
}
EXPORT_SYMBOL(rtw89_phy_rfk_txgapk_and_wait);

int rtw89_phy_rfk_dack_and_wait(struct rtw89_dev *rtwdev,
				enum rtw89_phy_idx phy_idx,
				const struct rtw89_chan *chan,
				unsigned int ms)
{
	int ret;

	rtw89_phy_rfk_report_prep(rtwdev);

	ret = rtw89_fw_h2c_rf_dack(rtwdev, phy_idx, chan);
	if (ret)
		return ret;

	return rtw89_phy_rfk_report_wait(rtwdev, "DACK", ms);
}
EXPORT_SYMBOL(rtw89_phy_rfk_dack_and_wait);

int rtw89_phy_rfk_rxdck_and_wait(struct rtw89_dev *rtwdev,
				 enum rtw89_phy_idx phy_idx,
				 const struct rtw89_chan *chan,
				 bool is_chl_k, unsigned int ms)
{
	int ret;

	rtw89_phy_rfk_report_prep(rtwdev);

	ret = rtw89_fw_h2c_rf_rxdck(rtwdev, phy_idx, chan, is_chl_k);
	if (ret)
		return ret;

	return rtw89_phy_rfk_report_wait(rtwdev, "RX_DCK", ms);
}
EXPORT_SYMBOL(rtw89_phy_rfk_rxdck_and_wait);

static u32 phy_tssi_get_cck_group(u8 ch)
{
	switch (ch) {
	case 1 ... 2:
		return 0;
	case 3 ... 5:
		return 1;
	case 6 ... 8:
		return 2;
	case 9 ... 11:
		return 3;
	case 12 ... 13:
		return 4;
	case 14:
		return 5;
	}

	return 0;
}

#define PHY_TSSI_EXTRA_GROUP_BIT BIT(31)
#define PHY_TSSI_EXTRA_GROUP(idx) (PHY_TSSI_EXTRA_GROUP_BIT | (idx))
#define PHY_IS_TSSI_EXTRA_GROUP(group) ((group) & PHY_TSSI_EXTRA_GROUP_BIT)
#define PHY_TSSI_EXTRA_GET_GROUP_IDX1(group) \
	((group) & ~PHY_TSSI_EXTRA_GROUP_BIT)
#define PHY_TSSI_EXTRA_GET_GROUP_IDX2(group) \
	(PHY_TSSI_EXTRA_GET_GROUP_IDX1(group) + 1)

static u32 phy_tssi_get_ofdm_group(u8 ch)
{
	switch (ch) {
	case 1 ... 2:
		return 0;
	case 3 ... 5:
		return 1;
	case 6 ... 8:
		return 2;
	case 9 ... 11:
		return 3;
	case 12 ... 14:
		return 4;
	case 36 ... 40:
		return 5;
	case 41 ... 43:
		return PHY_TSSI_EXTRA_GROUP(5);
	case 44 ... 48:
		return 6;
	case 49 ... 51:
		return PHY_TSSI_EXTRA_GROUP(6);
	case 52 ... 56:
		return 7;
	case 57 ... 59:
		return PHY_TSSI_EXTRA_GROUP(7);
	case 60 ... 64:
		return 8;
	case 100 ... 104:
		return 9;
	case 105 ... 107:
		return PHY_TSSI_EXTRA_GROUP(9);
	case 108 ... 112:
		return 10;
	case 113 ... 115:
		return PHY_TSSI_EXTRA_GROUP(10);
	case 116 ... 120:
		return 11;
	case 121 ... 123:
		return PHY_TSSI_EXTRA_GROUP(11);
	case 124 ... 128:
		return 12;
	case 129 ... 131:
		return PHY_TSSI_EXTRA_GROUP(12);
	case 132 ... 136:
		return 13;
	case 137 ... 139:
		return PHY_TSSI_EXTRA_GROUP(13);
	case 140 ... 144:
		return 14;
	case 149 ... 153:
		return 15;
	case 154 ... 156:
		return PHY_TSSI_EXTRA_GROUP(15);
	case 157 ... 161:
		return 16;
	case 162 ... 164:
		return PHY_TSSI_EXTRA_GROUP(16);
	case 165 ... 169:
		return 17;
	case 170 ... 172:
		return PHY_TSSI_EXTRA_GROUP(17);
	case 173 ... 177:
		return 18;
	}

	return 0;
}

static u32 phy_tssi_get_6g_ofdm_group(u8 ch)
{
	switch (ch) {
	case 1 ... 5:
		return 0;
	case 6 ... 8:
		return PHY_TSSI_EXTRA_GROUP(0);
	case 9 ... 13:
		return 1;
	case 14 ... 16:
		return PHY_TSSI_EXTRA_GROUP(1);
	case 17 ... 21:
		return 2;
	case 22 ... 24:
		return PHY_TSSI_EXTRA_GROUP(2);
	case 25 ... 29:
		return 3;
	case 33 ... 37:
		return 4;
	case 38 ... 40:
		return PHY_TSSI_EXTRA_GROUP(4);
	case 41 ... 45:
		return 5;
	case 46 ... 48:
		return PHY_TSSI_EXTRA_GROUP(5);
	case 49 ... 53:
		return 6;
	case 54 ... 56:
		return PHY_TSSI_EXTRA_GROUP(6);
	case 57 ... 61:
		return 7;
	case 65 ... 69:
		return 8;
	case 70 ... 72:
		return PHY_TSSI_EXTRA_GROUP(8);
	case 73 ... 77:
		return 9;
	case 78 ... 80:
		return PHY_TSSI_EXTRA_GROUP(9);
	case 81 ... 85:
		return 10;
	case 86 ... 88:
		return PHY_TSSI_EXTRA_GROUP(10);
	case 89 ... 93:
		return 11;
	case 97 ... 101:
		return 12;
	case 102 ... 104:
		return PHY_TSSI_EXTRA_GROUP(12);
	case 105 ... 109:
		return 13;
	case 110 ... 112:
		return PHY_TSSI_EXTRA_GROUP(13);
	case 113 ... 117:
		return 14;
	case 118 ... 120:
		return PHY_TSSI_EXTRA_GROUP(14);
	case 121 ... 125:
		return 15;
	case 129 ... 133:
		return 16;
	case 134 ... 136:
		return PHY_TSSI_EXTRA_GROUP(16);
	case 137 ... 141:
		return 17;
	case 142 ... 144:
		return PHY_TSSI_EXTRA_GROUP(17);
	case 145 ... 149:
		return 18;
	case 150 ... 152:
		return PHY_TSSI_EXTRA_GROUP(18);
	case 153 ... 157:
		return 19;
	case 161 ... 165:
		return 20;
	case 166 ... 168:
		return PHY_TSSI_EXTRA_GROUP(20);
	case 169 ... 173:
		return 21;
	case 174 ... 176:
		return PHY_TSSI_EXTRA_GROUP(21);
	case 177 ... 181:
		return 22;
	case 182 ... 184:
		return PHY_TSSI_EXTRA_GROUP(22);
	case 185 ... 189:
		return 23;
	case 193 ... 197:
		return 24;
	case 198 ... 200:
		return PHY_TSSI_EXTRA_GROUP(24);
	case 201 ... 205:
		return 25;
	case 206 ... 208:
		return PHY_TSSI_EXTRA_GROUP(25);
	case 209 ... 213:
		return 26;
	case 214 ... 216:
		return PHY_TSSI_EXTRA_GROUP(26);
	case 217 ... 221:
		return 27;
	case 225 ... 229:
		return 28;
	case 230 ... 232:
		return PHY_TSSI_EXTRA_GROUP(28);
	case 233 ... 237:
		return 29;
	case 238 ... 240:
		return PHY_TSSI_EXTRA_GROUP(29);
	case 241 ... 245:
		return 30;
	case 246 ... 248:
		return PHY_TSSI_EXTRA_GROUP(30);
	case 249 ... 253:
		return 31;
	}

	return 0;
}

static u32 phy_tssi_get_trim_group(u8 ch)
{
	switch (ch) {
	case 1 ... 8:
		return 0;
	case 9 ... 14:
		return 1;
	case 36 ... 48:
		return 2;
	case 49 ... 51:
		return PHY_TSSI_EXTRA_GROUP(2);
	case 52 ... 64:
		return 3;
	case 100 ... 112:
		return 4;
	case 113 ... 115:
		return PHY_TSSI_EXTRA_GROUP(4);
	case 116 ... 128:
		return 5;
	case 132 ... 144:
		return 6;
	case 149 ... 177:
		return 7;
	}

	return 0;
}

static u32 phy_tssi_get_6g_trim_group(u8 ch)
{
	switch (ch) {
	case 1 ... 13:
		return 0;
	case 14 ... 16:
		return PHY_TSSI_EXTRA_GROUP(0);
	case 17 ... 29:
		return 1;
	case 33 ... 45:
		return 2;
	case 46 ... 48:
		return PHY_TSSI_EXTRA_GROUP(2);
	case 49 ... 61:
		return 3;
	case 65 ... 77:
		return 4;
	case 78 ... 80:
		return PHY_TSSI_EXTRA_GROUP(4);
	case 81 ... 93:
		return 5;
	case 97 ... 109:
		return 6;
	case 110 ... 112:
		return PHY_TSSI_EXTRA_GROUP(6);
	case 113 ... 125:
		return 7;
	case 129 ... 141:
		return 8;
	case 142 ... 144:
		return PHY_TSSI_EXTRA_GROUP(8);
	case 145 ... 157:
		return 9;
	case 161 ... 173:
		return 10;
	case 174 ... 176:
		return PHY_TSSI_EXTRA_GROUP(10);
	case 177 ... 189:
		return 11;
	case 193 ... 205:
		return 12;
	case 206 ... 208:
		return PHY_TSSI_EXTRA_GROUP(12);
	case 209 ... 221:
		return 13;
	case 225 ... 237:
		return 14;
	case 238 ... 240:
		return PHY_TSSI_EXTRA_GROUP(14);
	case 241 ... 253:
		return 15;
	}

	return 0;
}

static s8 phy_tssi_get_ofdm_de(struct rtw89_dev *rtwdev,
			       enum rtw89_phy_idx phy,
			       const struct rtw89_chan *chan,
			       enum rtw89_rf_path path)
{
	struct rtw89_tssi_info *tssi_info = &rtwdev->tssi;
	enum rtw89_band band = chan->band_type;
	u8 ch = chan->channel;
	u32 gidx_1st;
	u32 gidx_2nd;
	s8 de_1st;
	s8 de_2nd;
	u32 gidx;
	s8 val;

	if (band == RTW89_BAND_6G)
		goto calc_6g;

	gidx = phy_tssi_get_ofdm_group(ch);

	rtw89_debug(rtwdev, RTW89_DBG_TSSI,
		    "[TSSI][TRIM]: path=%d mcs group_idx=0x%x\n",
		    path, gidx);

	if (PHY_IS_TSSI_EXTRA_GROUP(gidx)) {
		gidx_1st = PHY_TSSI_EXTRA_GET_GROUP_IDX1(gidx);
		gidx_2nd = PHY_TSSI_EXTRA_GET_GROUP_IDX2(gidx);
		de_1st = tssi_info->tssi_mcs[path][gidx_1st];
		de_2nd = tssi_info->tssi_mcs[path][gidx_2nd];
		val = (de_1st + de_2nd) / 2;

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI][TRIM]: path=%d mcs de=%d 1st=%d 2nd=%d\n",
			    path, val, de_1st, de_2nd);
	} else {
		val = tssi_info->tssi_mcs[path][gidx];

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI][TRIM]: path=%d mcs de=%d\n", path, val);
	}

	return val;

calc_6g:
	gidx = phy_tssi_get_6g_ofdm_group(ch);

	rtw89_debug(rtwdev, RTW89_DBG_TSSI,
		    "[TSSI][TRIM]: path=%d mcs group_idx=0x%x\n",
		    path, gidx);

	if (PHY_IS_TSSI_EXTRA_GROUP(gidx)) {
		gidx_1st = PHY_TSSI_EXTRA_GET_GROUP_IDX1(gidx);
		gidx_2nd = PHY_TSSI_EXTRA_GET_GROUP_IDX2(gidx);
		de_1st = tssi_info->tssi_6g_mcs[path][gidx_1st];
		de_2nd = tssi_info->tssi_6g_mcs[path][gidx_2nd];
		val = (de_1st + de_2nd) / 2;

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI][TRIM]: path=%d mcs de=%d 1st=%d 2nd=%d\n",
			    path, val, de_1st, de_2nd);
	} else {
		val = tssi_info->tssi_6g_mcs[path][gidx];

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI][TRIM]: path=%d mcs de=%d\n", path, val);
	}

	return val;
}

static s8 phy_tssi_get_ofdm_trim_de(struct rtw89_dev *rtwdev,
				    enum rtw89_phy_idx phy,
				    const struct rtw89_chan *chan,
				    enum rtw89_rf_path path)
{
	struct rtw89_tssi_info *tssi_info = &rtwdev->tssi;
	enum rtw89_band band = chan->band_type;
	u8 ch = chan->channel;
	u32 tgidx_1st;
	u32 tgidx_2nd;
	s8 tde_1st;
	s8 tde_2nd;
	u32 tgidx;
	s8 val;

	if (band == RTW89_BAND_6G)
		goto calc_6g;

	tgidx = phy_tssi_get_trim_group(ch);

	rtw89_debug(rtwdev, RTW89_DBG_TSSI,
		    "[TSSI][TRIM]: path=%d mcs trim_group_idx=0x%x\n",
		    path, tgidx);

	if (PHY_IS_TSSI_EXTRA_GROUP(tgidx)) {
		tgidx_1st = PHY_TSSI_EXTRA_GET_GROUP_IDX1(tgidx);
		tgidx_2nd = PHY_TSSI_EXTRA_GET_GROUP_IDX2(tgidx);
		tde_1st = tssi_info->tssi_trim[path][tgidx_1st];
		tde_2nd = tssi_info->tssi_trim[path][tgidx_2nd];
		val = (tde_1st + tde_2nd) / 2;

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI][TRIM]: path=%d mcs trim_de=%d 1st=%d 2nd=%d\n",
			    path, val, tde_1st, tde_2nd);
	} else {
		val = tssi_info->tssi_trim[path][tgidx];

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI][TRIM]: path=%d mcs trim_de=%d\n",
			    path, val);
	}

	return val;

calc_6g:
	tgidx = phy_tssi_get_6g_trim_group(ch);

	rtw89_debug(rtwdev, RTW89_DBG_TSSI,
		    "[TSSI][TRIM]: path=%d mcs trim_group_idx=0x%x\n",
		    path, tgidx);

	if (PHY_IS_TSSI_EXTRA_GROUP(tgidx)) {
		tgidx_1st = PHY_TSSI_EXTRA_GET_GROUP_IDX1(tgidx);
		tgidx_2nd = PHY_TSSI_EXTRA_GET_GROUP_IDX2(tgidx);
		tde_1st = tssi_info->tssi_trim_6g[path][tgidx_1st];
		tde_2nd = tssi_info->tssi_trim_6g[path][tgidx_2nd];
		val = (tde_1st + tde_2nd) / 2;

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI][TRIM]: path=%d mcs trim_de=%d 1st=%d 2nd=%d\n",
			    path, val, tde_1st, tde_2nd);
	} else {
		val = tssi_info->tssi_trim_6g[path][tgidx];

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI][TRIM]: path=%d mcs trim_de=%d\n",
			    path, val);
	}

	return val;
}

void rtw89_phy_rfk_tssi_fill_fwcmd_efuse_to_de(struct rtw89_dev *rtwdev,
					       enum rtw89_phy_idx phy,
					       const struct rtw89_chan *chan,
					       struct rtw89_h2c_rf_tssi *h2c)
{
	struct rtw89_tssi_info *tssi_info = &rtwdev->tssi;
	u8 ch = chan->channel;
	s8 trim_de;
	s8 ofdm_de;
	s8 cck_de;
	u8 gidx;
	s8 val;
	int i;

	rtw89_debug(rtwdev, RTW89_DBG_TSSI, "[TSSI][TRIM]: phy=%d ch=%d\n",
		    phy, ch);

	for (i = RF_PATH_A; i <= RF_PATH_B; i++) {
		trim_de = phy_tssi_get_ofdm_trim_de(rtwdev, phy, chan, i);
		h2c->curr_tssi_trim_de[i] = trim_de;

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI][TRIM]: path=%d trim_de=0x%x\n", i, trim_de);

		gidx = phy_tssi_get_cck_group(ch);
		cck_de = tssi_info->tssi_cck[i][gidx];
		val = u32_get_bits(cck_de + trim_de, 0xff);

		h2c->curr_tssi_cck_de[i] = 0x0;
		h2c->curr_tssi_cck_de_20m[i] = val;
		h2c->curr_tssi_cck_de_40m[i] = val;
		h2c->curr_tssi_efuse_cck_de[i] = cck_de;

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI][TRIM]: path=%d cck_de=0x%x\n", i, cck_de);

		ofdm_de = phy_tssi_get_ofdm_de(rtwdev, phy, chan, i);
		val = u32_get_bits(ofdm_de + trim_de, 0xff);

		h2c->curr_tssi_ofdm_de[i] = 0x0;
		h2c->curr_tssi_ofdm_de_20m[i] = val;
		h2c->curr_tssi_ofdm_de_40m[i] = val;
		h2c->curr_tssi_ofdm_de_80m[i] = val;
		h2c->curr_tssi_ofdm_de_160m[i] = val;
		h2c->curr_tssi_ofdm_de_320m[i] = val;
		h2c->curr_tssi_efuse_ofdm_de[i] = ofdm_de;

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI][TRIM]: path=%d ofdm_de=0x%x\n", i, ofdm_de);
	}
}

void rtw89_phy_rfk_tssi_fill_fwcmd_tmeter_tbl(struct rtw89_dev *rtwdev,
					      enum rtw89_phy_idx phy,
					      const struct rtw89_chan *chan,
					      struct rtw89_h2c_rf_tssi *h2c)
{
	struct rtw89_fw_txpwr_track_cfg *trk = rtwdev->fw.elm_info.txpwr_trk;
	struct rtw89_tssi_info *tssi_info = &rtwdev->tssi;
	const s8 *thm_up[RF_PATH_B + 1] = {};
	const s8 *thm_down[RF_PATH_B + 1] = {};
	u8 subband = chan->subband_type;
	s8 thm_ofst[128] = {0};
	u8 thermal;
	u8 path;
	u8 i, j;

	switch (subband) {
	default:
	case RTW89_CH_2G:
		thm_up[RF_PATH_A] = trk->delta[RTW89_FW_TXPWR_TRK_TYPE_2GA_P][0];
		thm_down[RF_PATH_A] = trk->delta[RTW89_FW_TXPWR_TRK_TYPE_2GA_N][0];
		thm_up[RF_PATH_B] = trk->delta[RTW89_FW_TXPWR_TRK_TYPE_2GB_P][0];
		thm_down[RF_PATH_B] = trk->delta[RTW89_FW_TXPWR_TRK_TYPE_2GB_N][0];
		break;
	case RTW89_CH_5G_BAND_1:
		thm_up[RF_PATH_A] = trk->delta[RTW89_FW_TXPWR_TRK_TYPE_5GA_P][0];
		thm_down[RF_PATH_A] = trk->delta[RTW89_FW_TXPWR_TRK_TYPE_5GA_N][0];
		thm_up[RF_PATH_B] = trk->delta[RTW89_FW_TXPWR_TRK_TYPE_5GB_P][0];
		thm_down[RF_PATH_B] = trk->delta[RTW89_FW_TXPWR_TRK_TYPE_5GB_N][0];
		break;
	case RTW89_CH_5G_BAND_3:
		thm_up[RF_PATH_A] = trk->delta[RTW89_FW_TXPWR_TRK_TYPE_5GA_P][1];
		thm_down[RF_PATH_A] = trk->delta[RTW89_FW_TXPWR_TRK_TYPE_5GA_N][1];
		thm_up[RF_PATH_B] = trk->delta[RTW89_FW_TXPWR_TRK_TYPE_5GB_P][1];
		thm_down[RF_PATH_B] = trk->delta[RTW89_FW_TXPWR_TRK_TYPE_5GB_N][1];
		break;
	case RTW89_CH_5G_BAND_4:
		thm_up[RF_PATH_A] = trk->delta[RTW89_FW_TXPWR_TRK_TYPE_5GA_P][2];
		thm_down[RF_PATH_A] = trk->delta[RTW89_FW_TXPWR_TRK_TYPE_5GA_N][2];
		thm_up[RF_PATH_B] = trk->delta[RTW89_FW_TXPWR_TRK_TYPE_5GB_P][2];
		thm_down[RF_PATH_B] = trk->delta[RTW89_FW_TXPWR_TRK_TYPE_5GB_N][2];
		break;
	case RTW89_CH_6G_BAND_IDX0:
	case RTW89_CH_6G_BAND_IDX1:
		thm_up[RF_PATH_A] = trk->delta[RTW89_FW_TXPWR_TRK_TYPE_6GA_P][0];
		thm_down[RF_PATH_A] = trk->delta[RTW89_FW_TXPWR_TRK_TYPE_6GA_N][0];
		thm_up[RF_PATH_B] = trk->delta[RTW89_FW_TXPWR_TRK_TYPE_6GB_P][0];
		thm_down[RF_PATH_B] = trk->delta[RTW89_FW_TXPWR_TRK_TYPE_6GB_N][0];
		break;
	case RTW89_CH_6G_BAND_IDX2:
	case RTW89_CH_6G_BAND_IDX3:
		thm_up[RF_PATH_A] = trk->delta[RTW89_FW_TXPWR_TRK_TYPE_6GA_P][1];
		thm_down[RF_PATH_A] = trk->delta[RTW89_FW_TXPWR_TRK_TYPE_6GA_N][1];
		thm_up[RF_PATH_B] = trk->delta[RTW89_FW_TXPWR_TRK_TYPE_6GB_P][1];
		thm_down[RF_PATH_B] = trk->delta[RTW89_FW_TXPWR_TRK_TYPE_6GB_N][1];
		break;
	case RTW89_CH_6G_BAND_IDX4:
	case RTW89_CH_6G_BAND_IDX5:
		thm_up[RF_PATH_A] = trk->delta[RTW89_FW_TXPWR_TRK_TYPE_6GA_P][2];
		thm_down[RF_PATH_A] = trk->delta[RTW89_FW_TXPWR_TRK_TYPE_6GA_N][2];
		thm_up[RF_PATH_B] = trk->delta[RTW89_FW_TXPWR_TRK_TYPE_6GB_P][2];
		thm_down[RF_PATH_B] = trk->delta[RTW89_FW_TXPWR_TRK_TYPE_6GB_N][2];
		break;
	case RTW89_CH_6G_BAND_IDX6:
	case RTW89_CH_6G_BAND_IDX7:
		thm_up[RF_PATH_A] = trk->delta[RTW89_FW_TXPWR_TRK_TYPE_6GA_P][3];
		thm_down[RF_PATH_A] = trk->delta[RTW89_FW_TXPWR_TRK_TYPE_6GA_N][3];
		thm_up[RF_PATH_B] = trk->delta[RTW89_FW_TXPWR_TRK_TYPE_6GB_P][3];
		thm_down[RF_PATH_B] = trk->delta[RTW89_FW_TXPWR_TRK_TYPE_6GB_N][3];
		break;
	}

	rtw89_debug(rtwdev, RTW89_DBG_TSSI,
		    "[TSSI] tmeter tbl on subband: %u\n", subband);

	for (path = RF_PATH_A; path <= RF_PATH_B; path++) {
		thermal = tssi_info->thermal[path];
		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "path: %u, pg thermal: 0x%x\n", path, thermal);

		if (thermal == 0xff) {
			h2c->pg_thermal[path] = 0x38;
			memset(h2c->ftable[path], 0, sizeof(h2c->ftable[path]));
			continue;
		}

		h2c->pg_thermal[path] = thermal;

		i = 0;
		for (j = 0; j < 64; j++)
			thm_ofst[j] = i < DELTA_SWINGIDX_SIZE ?
				      thm_up[path][i++] :
				      thm_up[path][DELTA_SWINGIDX_SIZE - 1];

		i = 1;
		for (j = 127; j >= 64; j--)
			thm_ofst[j] = i < DELTA_SWINGIDX_SIZE ?
				      -thm_down[path][i++] :
				      -thm_down[path][DELTA_SWINGIDX_SIZE - 1];

		for (i = 0; i < 128; i += 4) {
			h2c->ftable[path][i + 0] = thm_ofst[i + 3];
			h2c->ftable[path][i + 1] = thm_ofst[i + 2];
			h2c->ftable[path][i + 2] = thm_ofst[i + 1];
			h2c->ftable[path][i + 3] = thm_ofst[i + 0];

			rtw89_debug(rtwdev, RTW89_DBG_TSSI,
				    "thm ofst [%x]: %02x %02x %02x %02x\n",
				    i, thm_ofst[i], thm_ofst[i + 1],
				    thm_ofst[i + 2], thm_ofst[i + 3]);
		}
	}
}

static u8 rtw89_phy_cfo_get_xcap_reg(struct rtw89_dev *rtwdev, bool sc_xo)
{
	const struct rtw89_xtal_info *xtal = rtwdev->chip->xtal_info;
	u32 reg_mask;

	if (sc_xo)
		reg_mask = xtal->sc_xo_mask;
	else
		reg_mask = xtal->sc_xi_mask;

	return (u8)rtw89_read32_mask(rtwdev, xtal->xcap_reg, reg_mask);
}

static void rtw89_phy_cfo_set_xcap_reg(struct rtw89_dev *rtwdev, bool sc_xo,
				       u8 val)
{
	const struct rtw89_xtal_info *xtal = rtwdev->chip->xtal_info;
	u32 reg_mask;

	if (sc_xo)
		reg_mask = xtal->sc_xo_mask;
	else
		reg_mask = xtal->sc_xi_mask;

	rtw89_write32_mask(rtwdev, xtal->xcap_reg, reg_mask, val);
}

static void rtw89_phy_cfo_set_crystal_cap(struct rtw89_dev *rtwdev,
					  u8 crystal_cap, bool force)
{
	struct rtw89_cfo_tracking_info *cfo = &rtwdev->cfo_tracking;
	const struct rtw89_chip_info *chip = rtwdev->chip;
	u8 sc_xi_val, sc_xo_val;

	if (!force && cfo->crystal_cap == crystal_cap)
		return;
	if (chip->chip_id == RTL8852A || chip->chip_id == RTL8851B) {
		rtw89_phy_cfo_set_xcap_reg(rtwdev, true, crystal_cap);
		rtw89_phy_cfo_set_xcap_reg(rtwdev, false, crystal_cap);
		sc_xo_val = rtw89_phy_cfo_get_xcap_reg(rtwdev, true);
		sc_xi_val = rtw89_phy_cfo_get_xcap_reg(rtwdev, false);
	} else {
		rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_XTAL_SC_XO,
					crystal_cap, XTAL_SC_XO_MASK);
		rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_XTAL_SC_XI,
					crystal_cap, XTAL_SC_XI_MASK);
		rtw89_mac_read_xtal_si(rtwdev, XTAL_SI_XTAL_SC_XO, &sc_xo_val);
		rtw89_mac_read_xtal_si(rtwdev, XTAL_SI_XTAL_SC_XI, &sc_xi_val);
	}
	cfo->crystal_cap = sc_xi_val;
	cfo->x_cap_ofst = (s8)((int)cfo->crystal_cap - cfo->def_x_cap);

	rtw89_debug(rtwdev, RTW89_DBG_CFO, "Set sc_xi=0x%x\n", sc_xi_val);
	rtw89_debug(rtwdev, RTW89_DBG_CFO, "Set sc_xo=0x%x\n", sc_xo_val);
	rtw89_debug(rtwdev, RTW89_DBG_CFO, "Get xcap_ofst=%d\n",
		    cfo->x_cap_ofst);
	rtw89_debug(rtwdev, RTW89_DBG_CFO, "Set xcap OK\n");
}

static void rtw89_phy_cfo_reset(struct rtw89_dev *rtwdev)
{
	struct rtw89_cfo_tracking_info *cfo = &rtwdev->cfo_tracking;
	u8 cap;

	cfo->def_x_cap = cfo->crystal_cap_default & B_AX_XTAL_SC_MASK;
	cfo->is_adjust = false;
	if (cfo->crystal_cap == cfo->def_x_cap)
		return;
	cap = cfo->crystal_cap;
	cap += (cap > cfo->def_x_cap ? -1 : 1);
	rtw89_phy_cfo_set_crystal_cap(rtwdev, cap, false);
	rtw89_debug(rtwdev, RTW89_DBG_CFO,
		    "(0x%x) approach to dflt_val=(0x%x)\n", cfo->crystal_cap,
		    cfo->def_x_cap);
}

static void rtw89_dcfo_comp(struct rtw89_dev *rtwdev, s32 curr_cfo)
{
	const struct rtw89_reg_def *dcfo_comp = rtwdev->chip->dcfo_comp;
	bool is_linked = rtwdev->total_sta_assoc > 0;
	s32 cfo_avg_312;
	s32 dcfo_comp_val;
	int sign;

	if (rtwdev->chip->chip_id == RTL8922A)
		return;

	if (!is_linked) {
		rtw89_debug(rtwdev, RTW89_DBG_CFO, "DCFO: is_linked=%d\n",
			    is_linked);
		return;
	}
	rtw89_debug(rtwdev, RTW89_DBG_CFO, "DCFO: curr_cfo=%d\n", curr_cfo);
	if (curr_cfo == 0)
		return;
	dcfo_comp_val = rtw89_phy_read32_mask(rtwdev, R_DCFO, B_DCFO);
	sign = curr_cfo > 0 ? 1 : -1;
	cfo_avg_312 = curr_cfo / 625 + sign * dcfo_comp_val;
	rtw89_debug(rtwdev, RTW89_DBG_CFO, "avg_cfo_312=%d step\n", cfo_avg_312);
	if (rtwdev->chip->chip_id == RTL8852A && rtwdev->hal.cv == CHIP_CBV)
		cfo_avg_312 = -cfo_avg_312;
	rtw89_phy_set_phy_regs(rtwdev, dcfo_comp->addr, dcfo_comp->mask,
			       cfo_avg_312);
}

static void rtw89_dcfo_comp_init(struct rtw89_dev *rtwdev)
{
	const struct rtw89_phy_gen_def *phy = rtwdev->chip->phy_def;
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const struct rtw89_cfo_regs *cfo = phy->cfo;

	rtw89_phy_set_phy_regs(rtwdev, cfo->comp_seg0, cfo->valid_0_mask, 1);
	rtw89_phy_set_phy_regs(rtwdev, cfo->comp, cfo->weighting_mask, 8);

	if (chip->chip_gen == RTW89_CHIP_AX) {
		if (chip->cfo_hw_comp) {
			rtw89_write32_mask(rtwdev, R_AX_PWR_UL_CTRL2,
					   B_AX_PWR_UL_CFO_MASK, 0x6);
		} else {
			rtw89_phy_set_phy_regs(rtwdev, R_DCFO, B_DCFO, 1);
			rtw89_write32_clr(rtwdev, R_AX_PWR_UL_CTRL2,
					  B_AX_PWR_UL_CFO_MASK);
		}
	}
}

static void rtw89_phy_cfo_init(struct rtw89_dev *rtwdev)
{
	struct rtw89_cfo_tracking_info *cfo = &rtwdev->cfo_tracking;
	struct rtw89_efuse *efuse = &rtwdev->efuse;

	cfo->crystal_cap_default = efuse->xtal_cap & B_AX_XTAL_SC_MASK;
	cfo->crystal_cap = cfo->crystal_cap_default;
	cfo->def_x_cap = cfo->crystal_cap;
	cfo->x_cap_ub = min_t(int, cfo->def_x_cap + CFO_BOUND, 0x7f);
	cfo->x_cap_lb = max_t(int, cfo->def_x_cap - CFO_BOUND, 0x1);
	cfo->is_adjust = false;
	cfo->divergence_lock_en = false;
	cfo->x_cap_ofst = 0;
	cfo->lock_cnt = 0;
	cfo->rtw89_multi_cfo_mode = RTW89_TP_BASED_AVG_MODE;
	cfo->apply_compensation = false;
	cfo->residual_cfo_acc = 0;
	rtw89_debug(rtwdev, RTW89_DBG_CFO, "Default xcap=%0x\n",
		    cfo->crystal_cap_default);
	rtw89_phy_cfo_set_crystal_cap(rtwdev, cfo->crystal_cap_default, true);
	rtw89_dcfo_comp_init(rtwdev);
	cfo->cfo_timer_ms = 2000;
	cfo->cfo_trig_by_timer_en = false;
	cfo->phy_cfo_trk_cnt = 0;
	cfo->phy_cfo_status = RTW89_PHY_DCFO_STATE_NORMAL;
	cfo->cfo_ul_ofdma_acc_mode = RTW89_CFO_UL_OFDMA_ACC_ENABLE;
}

static void rtw89_phy_cfo_crystal_cap_adjust(struct rtw89_dev *rtwdev,
					     s32 curr_cfo)
{
	struct rtw89_cfo_tracking_info *cfo = &rtwdev->cfo_tracking;
	int crystal_cap = cfo->crystal_cap;
	s32 cfo_abs = abs(curr_cfo);
	int sign;

	if (curr_cfo == 0) {
		rtw89_debug(rtwdev, RTW89_DBG_CFO, "curr_cfo=0\n");
		return;
	}
	if (!cfo->is_adjust) {
		if (cfo_abs > CFO_TRK_ENABLE_TH)
			cfo->is_adjust = true;
	} else {
		if (cfo_abs <= CFO_TRK_STOP_TH)
			cfo->is_adjust = false;
	}
	if (!cfo->is_adjust) {
		rtw89_debug(rtwdev, RTW89_DBG_CFO, "Stop CFO tracking\n");
		return;
	}
	sign = curr_cfo > 0 ? 1 : -1;
	if (cfo_abs > CFO_TRK_STOP_TH_4)
		crystal_cap += 3 * sign;
	else if (cfo_abs > CFO_TRK_STOP_TH_3)
		crystal_cap += 3 * sign;
	else if (cfo_abs > CFO_TRK_STOP_TH_2)
		crystal_cap += 1 * sign;
	else if (cfo_abs > CFO_TRK_STOP_TH_1)
		crystal_cap += 1 * sign;
	else
		return;

	crystal_cap = clamp(crystal_cap, 0, 127);
	rtw89_phy_cfo_set_crystal_cap(rtwdev, (u8)crystal_cap, false);
	rtw89_debug(rtwdev, RTW89_DBG_CFO,
		    "X_cap{Curr,Default}={0x%x,0x%x}\n",
		    cfo->crystal_cap, cfo->def_x_cap);
}

static s32 rtw89_phy_average_cfo_calc(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_cfo_tracking_info *cfo = &rtwdev->cfo_tracking;
	s32 cfo_khz_all = 0;
	s32 cfo_cnt_all = 0;
	s32 cfo_all_avg = 0;
	u8 i;

	if (rtwdev->total_sta_assoc != 1)
		return 0;
	rtw89_debug(rtwdev, RTW89_DBG_CFO, "one_entry_only\n");
	for (i = 0; i < CFO_TRACK_MAX_USER; i++) {
		if (cfo->cfo_cnt[i] == 0)
			continue;
		cfo_khz_all += cfo->cfo_tail[i];
		cfo_cnt_all += cfo->cfo_cnt[i];
		cfo_all_avg = phy_div(cfo_khz_all, cfo_cnt_all);
		cfo->pre_cfo_avg[i] = cfo->cfo_avg[i];
		cfo->dcfo_avg = phy_div(cfo_khz_all << chip->dcfo_comp_sft,
					cfo_cnt_all);
	}
	rtw89_debug(rtwdev, RTW89_DBG_CFO,
		    "CFO track for macid = %d\n", i);
	rtw89_debug(rtwdev, RTW89_DBG_CFO,
		    "Total cfo=%dK, pkt_cnt=%d, avg_cfo=%dK\n",
		    cfo_khz_all, cfo_cnt_all, cfo_all_avg);
	return cfo_all_avg;
}

static s32 rtw89_phy_multi_sta_cfo_calc(struct rtw89_dev *rtwdev)
{
	struct rtw89_cfo_tracking_info *cfo = &rtwdev->cfo_tracking;
	struct rtw89_traffic_stats *stats = &rtwdev->stats;
	s32 target_cfo = 0;
	s32 cfo_khz_all = 0;
	s32 cfo_khz_all_tp_wgt = 0;
	s32 cfo_avg = 0;
	s32 max_cfo_lb = BIT(31);
	s32 min_cfo_ub = GENMASK(30, 0);
	u16 cfo_cnt_all = 0;
	u8 active_entry_cnt = 0;
	u8 sta_cnt = 0;
	u32 tp_all = 0;
	u8 i;
	u8 cfo_tol = 0;

	rtw89_debug(rtwdev, RTW89_DBG_CFO, "Multi entry cfo_trk\n");
	if (cfo->rtw89_multi_cfo_mode == RTW89_PKT_BASED_AVG_MODE) {
		rtw89_debug(rtwdev, RTW89_DBG_CFO, "Pkt based avg mode\n");
		for (i = 0; i < CFO_TRACK_MAX_USER; i++) {
			if (cfo->cfo_cnt[i] == 0)
				continue;
			cfo_khz_all += cfo->cfo_tail[i];
			cfo_cnt_all += cfo->cfo_cnt[i];
			cfo_avg = phy_div(cfo_khz_all, (s32)cfo_cnt_all);
			rtw89_debug(rtwdev, RTW89_DBG_CFO,
				    "Msta cfo=%d, pkt_cnt=%d, avg_cfo=%d\n",
				    cfo_khz_all, cfo_cnt_all, cfo_avg);
			target_cfo = cfo_avg;
		}
	} else if (cfo->rtw89_multi_cfo_mode == RTW89_ENTRY_BASED_AVG_MODE) {
		rtw89_debug(rtwdev, RTW89_DBG_CFO, "Entry based avg mode\n");
		for (i = 0; i < CFO_TRACK_MAX_USER; i++) {
			if (cfo->cfo_cnt[i] == 0)
				continue;
			cfo->cfo_avg[i] = phy_div(cfo->cfo_tail[i],
						  (s32)cfo->cfo_cnt[i]);
			cfo_khz_all += cfo->cfo_avg[i];
			rtw89_debug(rtwdev, RTW89_DBG_CFO,
				    "Macid=%d, cfo_avg=%d\n", i,
				    cfo->cfo_avg[i]);
		}
		sta_cnt = rtwdev->total_sta_assoc;
		cfo_avg = phy_div(cfo_khz_all, (s32)sta_cnt);
		rtw89_debug(rtwdev, RTW89_DBG_CFO,
			    "Msta cfo_acc=%d, ent_cnt=%d, avg_cfo=%d\n",
			    cfo_khz_all, sta_cnt, cfo_avg);
		target_cfo = cfo_avg;
	} else if (cfo->rtw89_multi_cfo_mode == RTW89_TP_BASED_AVG_MODE) {
		rtw89_debug(rtwdev, RTW89_DBG_CFO, "TP based avg mode\n");
		cfo_tol = cfo->sta_cfo_tolerance;
		for (i = 0; i < CFO_TRACK_MAX_USER; i++) {
			sta_cnt++;
			if (cfo->cfo_cnt[i] != 0) {
				cfo->cfo_avg[i] = phy_div(cfo->cfo_tail[i],
							  (s32)cfo->cfo_cnt[i]);
				active_entry_cnt++;
			} else {
				cfo->cfo_avg[i] = cfo->pre_cfo_avg[i];
			}
			max_cfo_lb = max(cfo->cfo_avg[i] - cfo_tol, max_cfo_lb);
			min_cfo_ub = min(cfo->cfo_avg[i] + cfo_tol, min_cfo_ub);
			cfo_khz_all += cfo->cfo_avg[i];
			/* need tp for each entry */
			rtw89_debug(rtwdev, RTW89_DBG_CFO,
				    "[%d] cfo_avg=%d, tp=tbd\n",
				    i, cfo->cfo_avg[i]);
			if (sta_cnt >= rtwdev->total_sta_assoc)
				break;
		}
		tp_all = stats->rx_throughput; /* need tp for each entry */
		cfo_avg =  phy_div(cfo_khz_all_tp_wgt, (s32)tp_all);

		rtw89_debug(rtwdev, RTW89_DBG_CFO, "Assoc sta cnt=%d\n",
			    sta_cnt);
		rtw89_debug(rtwdev, RTW89_DBG_CFO, "Active sta cnt=%d\n",
			    active_entry_cnt);
		rtw89_debug(rtwdev, RTW89_DBG_CFO,
			    "Msta cfo with tp_wgt=%d, avg_cfo=%d\n",
			    cfo_khz_all_tp_wgt, cfo_avg);
		rtw89_debug(rtwdev, RTW89_DBG_CFO, "cfo_lb=%d,cfo_ub=%d\n",
			    max_cfo_lb, min_cfo_ub);
		if (max_cfo_lb <= min_cfo_ub) {
			rtw89_debug(rtwdev, RTW89_DBG_CFO,
				    "cfo win_size=%d\n",
				    min_cfo_ub - max_cfo_lb);
			target_cfo = clamp(cfo_avg, max_cfo_lb, min_cfo_ub);
		} else {
			rtw89_debug(rtwdev, RTW89_DBG_CFO,
				    "No intersection of cfo tolerance windows\n");
			target_cfo = phy_div(cfo_khz_all, (s32)sta_cnt);
		}
		for (i = 0; i < CFO_TRACK_MAX_USER; i++)
			cfo->pre_cfo_avg[i] = cfo->cfo_avg[i];
	}
	rtw89_debug(rtwdev, RTW89_DBG_CFO, "Target cfo=%d\n", target_cfo);
	return target_cfo;
}

static void rtw89_phy_cfo_statistics_reset(struct rtw89_dev *rtwdev)
{
	struct rtw89_cfo_tracking_info *cfo = &rtwdev->cfo_tracking;

	memset(&cfo->cfo_tail, 0, sizeof(cfo->cfo_tail));
	memset(&cfo->cfo_cnt, 0, sizeof(cfo->cfo_cnt));
	cfo->packet_count = 0;
	cfo->packet_count_pre = 0;
	cfo->cfo_avg_pre = 0;
}

static void rtw89_phy_cfo_dm(struct rtw89_dev *rtwdev)
{
	struct rtw89_cfo_tracking_info *cfo = &rtwdev->cfo_tracking;
	s32 new_cfo = 0;
	bool x_cap_update = false;
	u8 pre_x_cap = cfo->crystal_cap;
	u8 dcfo_comp_sft = rtwdev->chip->dcfo_comp_sft;

	cfo->dcfo_avg = 0;
	rtw89_debug(rtwdev, RTW89_DBG_CFO, "CFO:total_sta_assoc=%d\n",
		    rtwdev->total_sta_assoc);
	if (rtwdev->total_sta_assoc == 0 || rtw89_is_mlo_1_1(rtwdev)) {
		rtw89_phy_cfo_reset(rtwdev);
		return;
	}
	if (cfo->packet_count == 0) {
		rtw89_debug(rtwdev, RTW89_DBG_CFO, "Pkt cnt = 0\n");
		return;
	}
	if (cfo->packet_count == cfo->packet_count_pre) {
		rtw89_debug(rtwdev, RTW89_DBG_CFO, "Pkt cnt doesn't change\n");
		return;
	}
	if (rtwdev->total_sta_assoc == 1)
		new_cfo = rtw89_phy_average_cfo_calc(rtwdev);
	else
		new_cfo = rtw89_phy_multi_sta_cfo_calc(rtwdev);
	if (cfo->divergence_lock_en) {
		cfo->lock_cnt++;
		if (cfo->lock_cnt > CFO_PERIOD_CNT) {
			cfo->divergence_lock_en = false;
			cfo->lock_cnt = 0;
		} else {
			rtw89_phy_cfo_reset(rtwdev);
		}
		return;
	}
	if (cfo->crystal_cap >= cfo->x_cap_ub ||
	    cfo->crystal_cap <= cfo->x_cap_lb) {
		cfo->divergence_lock_en = true;
		rtw89_phy_cfo_reset(rtwdev);
		return;
	}

	rtw89_phy_cfo_crystal_cap_adjust(rtwdev, new_cfo);
	cfo->cfo_avg_pre = new_cfo;
	cfo->dcfo_avg_pre = cfo->dcfo_avg;
	x_cap_update =  cfo->crystal_cap != pre_x_cap;
	rtw89_debug(rtwdev, RTW89_DBG_CFO, "Xcap_up=%d\n", x_cap_update);
	rtw89_debug(rtwdev, RTW89_DBG_CFO, "Xcap: D:%x C:%x->%x, ofst=%d\n",
		    cfo->def_x_cap, pre_x_cap, cfo->crystal_cap,
		    cfo->x_cap_ofst);
	if (x_cap_update) {
		if (cfo->dcfo_avg > 0)
			cfo->dcfo_avg -= CFO_SW_COMP_FINE_TUNE << dcfo_comp_sft;
		else
			cfo->dcfo_avg += CFO_SW_COMP_FINE_TUNE << dcfo_comp_sft;
	}
	rtw89_dcfo_comp(rtwdev, cfo->dcfo_avg);
	rtw89_phy_cfo_statistics_reset(rtwdev);
}

void rtw89_phy_cfo_track_work(struct wiphy *wiphy, struct wiphy_work *work)
{
	struct rtw89_dev *rtwdev = container_of(work, struct rtw89_dev,
						cfo_track_work.work);
	struct rtw89_cfo_tracking_info *cfo = &rtwdev->cfo_tracking;

	lockdep_assert_wiphy(wiphy);

	if (!cfo->cfo_trig_by_timer_en)
		return;
	rtw89_leave_ps_mode(rtwdev);
	rtw89_phy_cfo_dm(rtwdev);
	wiphy_delayed_work_queue(wiphy, &rtwdev->cfo_track_work,
				 msecs_to_jiffies(cfo->cfo_timer_ms));
}

static void rtw89_phy_cfo_start_work(struct rtw89_dev *rtwdev)
{
	struct rtw89_cfo_tracking_info *cfo = &rtwdev->cfo_tracking;

	wiphy_delayed_work_queue(rtwdev->hw->wiphy, &rtwdev->cfo_track_work,
				 msecs_to_jiffies(cfo->cfo_timer_ms));
}

void rtw89_phy_cfo_track(struct rtw89_dev *rtwdev)
{
	struct rtw89_cfo_tracking_info *cfo = &rtwdev->cfo_tracking;
	struct rtw89_traffic_stats *stats = &rtwdev->stats;
	bool is_ul_ofdma = false, ofdma_acc_en = false;

	if (stats->rx_tf_periodic > CFO_TF_CNT_TH)
		is_ul_ofdma = true;
	if (cfo->cfo_ul_ofdma_acc_mode == RTW89_CFO_UL_OFDMA_ACC_ENABLE &&
	    is_ul_ofdma)
		ofdma_acc_en = true;

	switch (cfo->phy_cfo_status) {
	case RTW89_PHY_DCFO_STATE_NORMAL:
		if (stats->tx_throughput >= CFO_TP_UPPER) {
			cfo->phy_cfo_status = RTW89_PHY_DCFO_STATE_ENHANCE;
			cfo->cfo_trig_by_timer_en = true;
			cfo->cfo_timer_ms = CFO_COMP_PERIOD;
			rtw89_phy_cfo_start_work(rtwdev);
		}
		break;
	case RTW89_PHY_DCFO_STATE_ENHANCE:
		if (stats->tx_throughput <= CFO_TP_LOWER)
			cfo->phy_cfo_status = RTW89_PHY_DCFO_STATE_NORMAL;
		else if (ofdma_acc_en &&
			 cfo->phy_cfo_trk_cnt >= CFO_PERIOD_CNT)
			cfo->phy_cfo_status = RTW89_PHY_DCFO_STATE_HOLD;
		else
			cfo->phy_cfo_trk_cnt++;

		if (cfo->phy_cfo_status == RTW89_PHY_DCFO_STATE_NORMAL) {
			cfo->phy_cfo_trk_cnt = 0;
			cfo->cfo_trig_by_timer_en = false;
		}
		break;
	case RTW89_PHY_DCFO_STATE_HOLD:
		if (stats->tx_throughput <= CFO_TP_LOWER) {
			cfo->phy_cfo_status = RTW89_PHY_DCFO_STATE_NORMAL;
			cfo->phy_cfo_trk_cnt = 0;
			cfo->cfo_trig_by_timer_en = false;
		} else {
			cfo->phy_cfo_trk_cnt++;
		}
		break;
	default:
		cfo->phy_cfo_status = RTW89_PHY_DCFO_STATE_NORMAL;
		cfo->phy_cfo_trk_cnt = 0;
		break;
	}
	rtw89_debug(rtwdev, RTW89_DBG_CFO,
		    "[CFO]WatchDog tp=%d,state=%d,timer_en=%d,trk_cnt=%d,thermal=%ld\n",
		    stats->tx_throughput, cfo->phy_cfo_status,
		    cfo->cfo_trig_by_timer_en, cfo->phy_cfo_trk_cnt,
		    ewma_thermal_read(&rtwdev->phystat.avg_thermal[0]));
	if (cfo->cfo_trig_by_timer_en)
		return;
	rtw89_phy_cfo_dm(rtwdev);
}

void rtw89_phy_cfo_parse(struct rtw89_dev *rtwdev, s16 cfo_val,
			 struct rtw89_rx_phy_ppdu *phy_ppdu)
{
	struct rtw89_cfo_tracking_info *cfo = &rtwdev->cfo_tracking;
	u8 macid = phy_ppdu->mac_id;

	if (macid >= CFO_TRACK_MAX_USER) {
		rtw89_warn(rtwdev, "mac_id %d is out of range\n", macid);
		return;
	}

	cfo->cfo_tail[macid] += cfo_val;
	cfo->cfo_cnt[macid]++;
	cfo->packet_count++;
}

void rtw89_phy_ul_tb_assoc(struct rtw89_dev *rtwdev, struct rtw89_vif_link *rtwvif_link)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev,
						       rtwvif_link->chanctx_idx);
	struct rtw89_phy_ul_tb_info *ul_tb_info = &rtwdev->ul_tb_info;

	if (!chip->ul_tb_waveform_ctrl)
		return;

	rtwvif_link->def_tri_idx =
		rtw89_phy_read32_mask(rtwdev, R_DCFO_OPT, B_TXSHAPE_TRIANGULAR_CFG);

	if (chip->chip_id == RTL8852B && rtwdev->hal.cv > CHIP_CBV)
		rtwvif_link->dyn_tb_bedge_en = false;
	else if (chan->band_type >= RTW89_BAND_5G &&
		 chan->band_width >= RTW89_CHANNEL_WIDTH_40)
		rtwvif_link->dyn_tb_bedge_en = true;
	else
		rtwvif_link->dyn_tb_bedge_en = false;

	rtw89_debug(rtwdev, RTW89_DBG_UL_TB,
		    "[ULTB] def_if_bandedge=%d, def_tri_idx=%d\n",
		    ul_tb_info->def_if_bandedge, rtwvif_link->def_tri_idx);
	rtw89_debug(rtwdev, RTW89_DBG_UL_TB,
		    "[ULTB] dyn_tb_begde_en=%d, dyn_tb_tri_en=%d\n",
		    rtwvif_link->dyn_tb_bedge_en, ul_tb_info->dyn_tb_tri_en);
}

struct rtw89_phy_ul_tb_check_data {
	bool valid;
	bool high_tf_client;
	bool low_tf_client;
	bool dyn_tb_bedge_en;
	u8 def_tri_idx;
};

struct rtw89_phy_power_diff {
	u32 q_00;
	u32 q_11;
	u32 q_matrix_en;
	u32 ultb_1t_norm_160;
	u32 ultb_2t_norm_160;
	u32 com1_norm_1sts;
	u32 com2_resp_1sts_path;
};

static void rtw89_phy_ofdma_power_diff(struct rtw89_dev *rtwdev,
				       struct rtw89_vif_link *rtwvif_link)
{
	static const struct rtw89_phy_power_diff table[2] = {
		{0x0, 0x0, 0x0, 0x0, 0xf4, 0x3, 0x3},
		{0xb50, 0xb50, 0x1, 0xc, 0x0, 0x1, 0x1},
	};
	const struct rtw89_phy_power_diff *param;
	u32 reg;

	if (!rtwdev->chip->ul_tb_pwr_diff)
		return;

	if (rtwvif_link->pwr_diff_en == rtwvif_link->pre_pwr_diff_en) {
		rtwvif_link->pwr_diff_en = false;
		return;
	}

	rtwvif_link->pre_pwr_diff_en = rtwvif_link->pwr_diff_en;
	param = &table[rtwvif_link->pwr_diff_en];

	rtw89_phy_write32_mask(rtwdev, R_Q_MATRIX_00, B_Q_MATRIX_00_REAL,
			       param->q_00);
	rtw89_phy_write32_mask(rtwdev, R_Q_MATRIX_11, B_Q_MATRIX_11_REAL,
			       param->q_11);
	rtw89_phy_write32_mask(rtwdev, R_CUSTOMIZE_Q_MATRIX,
			       B_CUSTOMIZE_Q_MATRIX_EN, param->q_matrix_en);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_PWR_UL_TB_1T, rtwvif_link->mac_idx);
	rtw89_write32_mask(rtwdev, reg, B_AX_PWR_UL_TB_1T_NORM_BW160,
			   param->ultb_1t_norm_160);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_PWR_UL_TB_2T, rtwvif_link->mac_idx);
	rtw89_write32_mask(rtwdev, reg, B_AX_PWR_UL_TB_2T_NORM_BW160,
			   param->ultb_2t_norm_160);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_PATH_COM1, rtwvif_link->mac_idx);
	rtw89_write32_mask(rtwdev, reg, B_AX_PATH_COM1_NORM_1STS,
			   param->com1_norm_1sts);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_PATH_COM2, rtwvif_link->mac_idx);
	rtw89_write32_mask(rtwdev, reg, B_AX_PATH_COM2_RESP_1STS_PATH,
			   param->com2_resp_1sts_path);
}

static
void rtw89_phy_ul_tb_ctrl_check(struct rtw89_dev *rtwdev,
				struct rtw89_vif_link *rtwvif_link,
				struct rtw89_phy_ul_tb_check_data *ul_tb_data)
{
	struct rtw89_traffic_stats *stats = &rtwdev->stats;
	struct ieee80211_vif *vif = rtwvif_link_to_vif(rtwvif_link);

	if (rtwvif_link->wifi_role != RTW89_WIFI_ROLE_STATION)
		return;

	if (!vif->cfg.assoc)
		return;

	if (rtwdev->chip->ul_tb_waveform_ctrl) {
		if (stats->rx_tf_periodic > UL_TB_TF_CNT_L2H_TH)
			ul_tb_data->high_tf_client = true;
		else if (stats->rx_tf_periodic < UL_TB_TF_CNT_H2L_TH)
			ul_tb_data->low_tf_client = true;

		ul_tb_data->valid = true;
		ul_tb_data->def_tri_idx = rtwvif_link->def_tri_idx;
		ul_tb_data->dyn_tb_bedge_en = rtwvif_link->dyn_tb_bedge_en;
	}

	rtw89_phy_ofdma_power_diff(rtwdev, rtwvif_link);
}

static void rtw89_phy_ul_tb_waveform_ctrl(struct rtw89_dev *rtwdev,
					  struct rtw89_phy_ul_tb_check_data *ul_tb_data)
{
	struct rtw89_phy_ul_tb_info *ul_tb_info = &rtwdev->ul_tb_info;

	if (!rtwdev->chip->ul_tb_waveform_ctrl)
		return;

	if (ul_tb_data->dyn_tb_bedge_en) {
		if (ul_tb_data->high_tf_client) {
			rtw89_phy_write32_mask(rtwdev, R_BANDEDGE, B_BANDEDGE_EN, 0);
			rtw89_debug(rtwdev, RTW89_DBG_UL_TB,
				    "[ULTB] Turn off if_bandedge\n");
		} else if (ul_tb_data->low_tf_client) {
			rtw89_phy_write32_mask(rtwdev, R_BANDEDGE, B_BANDEDGE_EN,
					       ul_tb_info->def_if_bandedge);
			rtw89_debug(rtwdev, RTW89_DBG_UL_TB,
				    "[ULTB] Set to default if_bandedge = %d\n",
				    ul_tb_info->def_if_bandedge);
		}
	}

	if (ul_tb_info->dyn_tb_tri_en) {
		if (ul_tb_data->high_tf_client) {
			rtw89_phy_write32_mask(rtwdev, R_DCFO_OPT,
					       B_TXSHAPE_TRIANGULAR_CFG, 0);
			rtw89_debug(rtwdev, RTW89_DBG_UL_TB,
				    "[ULTB] Turn off Tx triangle\n");
		} else if (ul_tb_data->low_tf_client) {
			rtw89_phy_write32_mask(rtwdev, R_DCFO_OPT,
					       B_TXSHAPE_TRIANGULAR_CFG,
					       ul_tb_data->def_tri_idx);
			rtw89_debug(rtwdev, RTW89_DBG_UL_TB,
				    "[ULTB] Set to default tx_shap_idx = %d\n",
				    ul_tb_data->def_tri_idx);
		}
	}
}

void rtw89_phy_ul_tb_ctrl_track(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_phy_ul_tb_check_data ul_tb_data = {};
	struct rtw89_vif_link *rtwvif_link;
	struct rtw89_vif *rtwvif;
	unsigned int link_id;

	if (!chip->ul_tb_waveform_ctrl && !chip->ul_tb_pwr_diff)
		return;

	if (rtwdev->total_sta_assoc != 1)
		return;

	rtw89_for_each_rtwvif(rtwdev, rtwvif)
		rtw89_vif_for_each_link(rtwvif, rtwvif_link, link_id)
			rtw89_phy_ul_tb_ctrl_check(rtwdev, rtwvif_link, &ul_tb_data);

	if (!ul_tb_data.valid)
		return;

	rtw89_phy_ul_tb_waveform_ctrl(rtwdev, &ul_tb_data);
}

static void rtw89_phy_ul_tb_info_init(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_phy_ul_tb_info *ul_tb_info = &rtwdev->ul_tb_info;

	if (!chip->ul_tb_waveform_ctrl)
		return;

	ul_tb_info->dyn_tb_tri_en = true;
	ul_tb_info->def_if_bandedge =
		rtw89_phy_read32_mask(rtwdev, R_BANDEDGE, B_BANDEDGE_EN);
}

static
void rtw89_phy_antdiv_sts_instance_reset(struct rtw89_antdiv_stats *antdiv_sts)
{
	ewma_rssi_init(&antdiv_sts->cck_rssi_avg);
	ewma_rssi_init(&antdiv_sts->ofdm_rssi_avg);
	ewma_rssi_init(&antdiv_sts->non_legacy_rssi_avg);
	antdiv_sts->pkt_cnt_cck = 0;
	antdiv_sts->pkt_cnt_ofdm = 0;
	antdiv_sts->pkt_cnt_non_legacy = 0;
	antdiv_sts->evm = 0;
}

static void rtw89_phy_antdiv_sts_instance_add(struct rtw89_dev *rtwdev,
					      struct rtw89_rx_phy_ppdu *phy_ppdu,
					      struct rtw89_antdiv_stats *stats)
{
	if (rtw89_get_data_rate_mode(rtwdev, phy_ppdu->rate) == DATA_RATE_MODE_NON_HT) {
		if (phy_ppdu->rate < RTW89_HW_RATE_OFDM6) {
			ewma_rssi_add(&stats->cck_rssi_avg, phy_ppdu->rssi_avg);
			stats->pkt_cnt_cck++;
		} else {
			ewma_rssi_add(&stats->ofdm_rssi_avg, phy_ppdu->rssi_avg);
			stats->pkt_cnt_ofdm++;
			stats->evm += phy_ppdu->ofdm.evm_min;
		}
	} else {
		ewma_rssi_add(&stats->non_legacy_rssi_avg, phy_ppdu->rssi_avg);
		stats->pkt_cnt_non_legacy++;
		stats->evm += phy_ppdu->ofdm.evm_min;
	}
}

static u8 rtw89_phy_antdiv_sts_instance_get_rssi(struct rtw89_antdiv_stats *stats)
{
	if (stats->pkt_cnt_non_legacy >= stats->pkt_cnt_cck &&
	    stats->pkt_cnt_non_legacy >= stats->pkt_cnt_ofdm)
		return ewma_rssi_read(&stats->non_legacy_rssi_avg);
	else if (stats->pkt_cnt_ofdm >= stats->pkt_cnt_cck &&
		 stats->pkt_cnt_ofdm >= stats->pkt_cnt_non_legacy)
		return ewma_rssi_read(&stats->ofdm_rssi_avg);
	else
		return ewma_rssi_read(&stats->cck_rssi_avg);
}

static u8 rtw89_phy_antdiv_sts_instance_get_evm(struct rtw89_antdiv_stats *stats)
{
	return phy_div(stats->evm, stats->pkt_cnt_non_legacy + stats->pkt_cnt_ofdm);
}

void rtw89_phy_antdiv_parse(struct rtw89_dev *rtwdev,
			    struct rtw89_rx_phy_ppdu *phy_ppdu)
{
	struct rtw89_antdiv_info *antdiv = &rtwdev->antdiv;
	struct rtw89_hal *hal = &rtwdev->hal;

	if (!hal->ant_diversity || hal->ant_diversity_fixed)
		return;

	rtw89_phy_antdiv_sts_instance_add(rtwdev, phy_ppdu, &antdiv->target_stats);

	if (!antdiv->get_stats)
		return;

	if (hal->antenna_rx == RF_A)
		rtw89_phy_antdiv_sts_instance_add(rtwdev, phy_ppdu, &antdiv->main_stats);
	else if (hal->antenna_rx == RF_B)
		rtw89_phy_antdiv_sts_instance_add(rtwdev, phy_ppdu, &antdiv->aux_stats);
}

static void rtw89_phy_antdiv_reg_init(struct rtw89_dev *rtwdev)
{
	rtw89_phy_write32_idx(rtwdev, R_P0_TRSW, B_P0_ANT_TRAIN_EN,
			      0x0, RTW89_PHY_0);
	rtw89_phy_write32_idx(rtwdev, R_P0_TRSW, B_P0_TX_ANT_SEL,
			      0x0, RTW89_PHY_0);

	rtw89_phy_write32_idx(rtwdev, R_P0_ANT_SW, B_P0_TRSW_TX_EXTEND,
			      0x0, RTW89_PHY_0);
	rtw89_phy_write32_idx(rtwdev, R_P0_ANT_SW, B_P0_HW_ANTSW_DIS_BY_GNT_BT,
			      0x0, RTW89_PHY_0);

	rtw89_phy_write32_idx(rtwdev, R_P0_TRSW, B_P0_BT_FORCE_ANTIDX_EN,
			      0x0, RTW89_PHY_0);

	rtw89_phy_write32_idx(rtwdev, R_RFSW_CTRL_ANT0_BASE, B_RFSW_CTRL_ANT_MAPPING,
			      0x0100, RTW89_PHY_0);

	rtw89_phy_write32_idx(rtwdev, R_P0_ANTSEL, B_P0_ANTSEL_BTG_TRX,
			      0x1, RTW89_PHY_0);
	rtw89_phy_write32_idx(rtwdev, R_P0_ANTSEL, B_P0_ANTSEL_HW_CTRL,
			      0x0, RTW89_PHY_0);
	rtw89_phy_write32_idx(rtwdev, R_P0_ANTSEL, B_P0_ANTSEL_SW_2G,
			      0x0, RTW89_PHY_0);
	rtw89_phy_write32_idx(rtwdev, R_P0_ANTSEL, B_P0_ANTSEL_SW_5G,
			      0x0, RTW89_PHY_0);
}

static void rtw89_phy_antdiv_sts_reset(struct rtw89_dev *rtwdev)
{
	struct rtw89_antdiv_info *antdiv = &rtwdev->antdiv;

	rtw89_phy_antdiv_sts_instance_reset(&antdiv->target_stats);
	rtw89_phy_antdiv_sts_instance_reset(&antdiv->main_stats);
	rtw89_phy_antdiv_sts_instance_reset(&antdiv->aux_stats);
}

static void rtw89_phy_antdiv_init(struct rtw89_dev *rtwdev)
{
	struct rtw89_antdiv_info *antdiv = &rtwdev->antdiv;
	struct rtw89_hal *hal = &rtwdev->hal;

	if (!hal->ant_diversity)
		return;

	antdiv->get_stats = false;
	antdiv->rssi_pre = 0;
	rtw89_phy_antdiv_sts_reset(rtwdev);
	rtw89_phy_antdiv_reg_init(rtwdev);
}

static void rtw89_phy_thermal_protect(struct rtw89_dev *rtwdev)
{
	struct rtw89_phy_stat *phystat = &rtwdev->phystat;
	struct rtw89_hal *hal = &rtwdev->hal;
	u8 th_max = phystat->last_thermal_max;
	u8 lv = hal->thermal_prot_lv;

	if (!hal->thermal_prot_th ||
	    (hal->disabled_dm_bitmap & BIT(RTW89_DM_THERMAL_PROTECT)))
		return;

	if (th_max > hal->thermal_prot_th && lv < RTW89_THERMAL_PROT_LV_MAX)
		lv++;
	else if (th_max < hal->thermal_prot_th - 2 && lv > 0)
		lv--;
	else
		return;

	hal->thermal_prot_lv = lv;

	rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK, "thermal protection lv=%d\n", lv);

	rtw89_fw_h2c_tx_duty(rtwdev, hal->thermal_prot_lv);
}

static void rtw89_phy_stat_thermal_update(struct rtw89_dev *rtwdev)
{
	struct rtw89_phy_stat *phystat = &rtwdev->phystat;
	u8 th, th_max = 0;
	int i;

	for (i = 0; i < rtwdev->chip->rf_path_num; i++) {
		th = rtw89_chip_get_thermal(rtwdev, i);
		if (th)
			ewma_thermal_add(&phystat->avg_thermal[i], th);

		rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK,
			    "path(%d) thermal cur=%u avg=%ld", i, th,
			    ewma_thermal_read(&phystat->avg_thermal[i]));

		th_max = max(th_max, th);
	}

	phystat->last_thermal_max = th_max;
}

struct rtw89_phy_iter_rssi_data {
	struct rtw89_dev *rtwdev;
	bool rssi_changed;
};

static
void __rtw89_phy_stat_rssi_update_iter(struct rtw89_sta_link *rtwsta_link,
				       struct rtw89_phy_iter_rssi_data *rssi_data)
{
	struct rtw89_vif_link *rtwvif_link = rtwsta_link->rtwvif_link;
	struct rtw89_dev *rtwdev = rssi_data->rtwdev;
	struct rtw89_phy_ch_info *ch_info;
	struct rtw89_bb_ctx *bb;
	unsigned long rssi_curr;

	rssi_curr = ewma_rssi_read(&rtwsta_link->avg_rssi);
	bb = rtw89_get_bb_ctx(rtwdev, rtwvif_link->phy_idx);
	ch_info = &bb->ch_info;

	if (rssi_curr < ch_info->rssi_min) {
		ch_info->rssi_min = rssi_curr;
		ch_info->rssi_min_macid = rtwsta_link->mac_id;
	}

	if (rtwsta_link->prev_rssi == 0) {
		rtwsta_link->prev_rssi = rssi_curr;
	} else if (abs((int)rtwsta_link->prev_rssi - (int)rssi_curr) >
		   (3 << RSSI_FACTOR)) {
		rtwsta_link->prev_rssi = rssi_curr;
		rssi_data->rssi_changed = true;
	}
}

static void rtw89_phy_stat_rssi_update_iter(void *data,
					    struct ieee80211_sta *sta)
{
	struct rtw89_phy_iter_rssi_data *rssi_data =
					(struct rtw89_phy_iter_rssi_data *)data;
	struct rtw89_sta *rtwsta = sta_to_rtwsta(sta);
	struct rtw89_sta_link *rtwsta_link;
	unsigned int link_id;

	rtw89_sta_for_each_link(rtwsta, rtwsta_link, link_id)
		__rtw89_phy_stat_rssi_update_iter(rtwsta_link, rssi_data);
}

static void rtw89_phy_stat_rssi_update(struct rtw89_dev *rtwdev)
{
	struct rtw89_phy_iter_rssi_data rssi_data = {};
	struct rtw89_bb_ctx *bb;

	rssi_data.rtwdev = rtwdev;
	rtw89_for_each_active_bb(rtwdev, bb)
		bb->ch_info.rssi_min = U8_MAX;

	ieee80211_iterate_stations_atomic(rtwdev->hw,
					  rtw89_phy_stat_rssi_update_iter,
					  &rssi_data);
	if (rssi_data.rssi_changed)
		rtw89_btc_ntfy_wl_sta(rtwdev);
}

static void rtw89_phy_stat_init(struct rtw89_dev *rtwdev)
{
	struct rtw89_phy_stat *phystat = &rtwdev->phystat;
	int i;

	for (i = 0; i < rtwdev->chip->rf_path_num; i++)
		ewma_thermal_init(&phystat->avg_thermal[i]);

	rtw89_phy_stat_thermal_update(rtwdev);

	memset(&phystat->cur_pkt_stat, 0, sizeof(phystat->cur_pkt_stat));
	memset(&phystat->last_pkt_stat, 0, sizeof(phystat->last_pkt_stat));

	ewma_rssi_init(&phystat->bcn_rssi);

	rtwdev->hal.thermal_prot_lv = 0;
}

void rtw89_phy_stat_track(struct rtw89_dev *rtwdev)
{
	struct rtw89_phy_stat *phystat = &rtwdev->phystat;

	rtw89_phy_stat_thermal_update(rtwdev);
	rtw89_phy_thermal_protect(rtwdev);
	rtw89_phy_stat_rssi_update(rtwdev);

	phystat->last_pkt_stat = phystat->cur_pkt_stat;
	memset(&phystat->cur_pkt_stat, 0, sizeof(phystat->cur_pkt_stat));
}

static u16 rtw89_phy_ccx_us_to_idx(struct rtw89_dev *rtwdev,
				   struct rtw89_bb_ctx *bb, u32 time_us)
{
	struct rtw89_env_monitor_info *env = &bb->env_monitor;

	return time_us >> (ilog2(CCX_US_BASE_RATIO) + env->ccx_unit_idx);
}

static u32 rtw89_phy_ccx_idx_to_us(struct rtw89_dev *rtwdev,
				   struct rtw89_bb_ctx *bb, u16 idx)
{
	struct rtw89_env_monitor_info *env = &bb->env_monitor;

	return idx << (ilog2(CCX_US_BASE_RATIO) + env->ccx_unit_idx);
}

static void rtw89_phy_ccx_top_setting_init(struct rtw89_dev *rtwdev,
					   struct rtw89_bb_ctx *bb)
{
	const struct rtw89_phy_gen_def *phy = rtwdev->chip->phy_def;
	struct rtw89_env_monitor_info *env = &bb->env_monitor;
	const struct rtw89_ccx_regs *ccx = phy->ccx;

	env->ccx_manual_ctrl = false;
	env->ccx_ongoing = false;
	env->ccx_rac_lv = RTW89_RAC_RELEASE;
	env->ccx_period = 0;
	env->ccx_unit_idx = RTW89_CCX_32_US;

	rtw89_phy_write32_idx(rtwdev, ccx->setting_addr, ccx->en_mask, 1, bb->phy_idx);
	rtw89_phy_write32_idx(rtwdev, ccx->setting_addr, ccx->trig_opt_mask, 1,
			      bb->phy_idx);
	rtw89_phy_write32_idx(rtwdev, ccx->setting_addr, ccx->measurement_trig_mask, 1,
			      bb->phy_idx);
	rtw89_phy_write32_idx(rtwdev, ccx->setting_addr, ccx->edcca_opt_mask,
			      RTW89_CCX_EDCCA_BW20_0, bb->phy_idx);
}

static u16 rtw89_phy_ccx_get_report(struct rtw89_dev *rtwdev,
				    struct rtw89_bb_ctx *bb,
				    u16 report, u16 score)
{
	struct rtw89_env_monitor_info *env = &bb->env_monitor;
	u32 numer = 0;
	u16 ret = 0;

	numer = report * score + (env->ccx_period >> 1);
	if (env->ccx_period)
		ret = numer / env->ccx_period;

	return ret >= score ? score - 1 : ret;
}

static void rtw89_phy_ccx_ms_to_period_unit(struct rtw89_dev *rtwdev,
					    u16 time_ms, u32 *period,
					    u32 *unit_idx)
{
	u32 idx;
	u8 quotient;

	if (time_ms >= CCX_MAX_PERIOD)
		time_ms = CCX_MAX_PERIOD;

	quotient = CCX_MAX_PERIOD_UNIT * time_ms / CCX_MAX_PERIOD;

	if (quotient < 4)
		idx = RTW89_CCX_4_US;
	else if (quotient < 8)
		idx = RTW89_CCX_8_US;
	else if (quotient < 16)
		idx = RTW89_CCX_16_US;
	else
		idx = RTW89_CCX_32_US;

	*unit_idx = idx;
	*period = (time_ms * MS_TO_4US_RATIO) >> idx;

	rtw89_debug(rtwdev, RTW89_DBG_PHY_TRACK,
		    "[Trigger Time] period:%d, unit_idx:%d\n",
		    *period, *unit_idx);
}

static void rtw89_phy_ccx_racing_release(struct rtw89_dev *rtwdev,
					 struct rtw89_bb_ctx *bb)
{
	struct rtw89_env_monitor_info *env = &bb->env_monitor;

	rtw89_debug(rtwdev, RTW89_DBG_PHY_TRACK,
		    "lv:(%d)->(0)\n", env->ccx_rac_lv);

	env->ccx_ongoing = false;
	env->ccx_rac_lv = RTW89_RAC_RELEASE;
	env->ifs_clm_app = RTW89_IFS_CLM_BACKGROUND;
}

static bool rtw89_phy_ifs_clm_th_update_check(struct rtw89_dev *rtwdev,
					      struct rtw89_bb_ctx *bb,
					      struct rtw89_ccx_para_info *para)
{
	struct rtw89_env_monitor_info *env = &bb->env_monitor;
	bool is_update = env->ifs_clm_app != para->ifs_clm_app;
	u8 i = 0;
	u16 *ifs_th_l = env->ifs_clm_th_l;
	u16 *ifs_th_h = env->ifs_clm_th_h;
	u32 ifs_th0_us = 0, ifs_th_times = 0;
	u32 ifs_th_h_us[RTW89_IFS_CLM_NUM] = {0};

	if (!is_update)
		goto ifs_update_finished;

	switch (para->ifs_clm_app) {
	case RTW89_IFS_CLM_INIT:
	case RTW89_IFS_CLM_BACKGROUND:
	case RTW89_IFS_CLM_ACS:
	case RTW89_IFS_CLM_DBG:
	case RTW89_IFS_CLM_DIG:
	case RTW89_IFS_CLM_TDMA_DIG:
		ifs_th0_us = IFS_CLM_TH0_UPPER;
		ifs_th_times = IFS_CLM_TH_MUL;
		break;
	case RTW89_IFS_CLM_DBG_MANUAL:
		ifs_th0_us = para->ifs_clm_manual_th0;
		ifs_th_times = para->ifs_clm_manual_th_times;
		break;
	default:
		break;
	}

	/* Set sampling threshold for 4 different regions, unit in idx_cnt.
	 * low[i] = high[i-1] + 1
	 * high[i] = high[i-1] * ifs_th_times
	 */
	ifs_th_l[IFS_CLM_TH_START_IDX] = 0;
	ifs_th_h_us[IFS_CLM_TH_START_IDX] = ifs_th0_us;
	ifs_th_h[IFS_CLM_TH_START_IDX] = rtw89_phy_ccx_us_to_idx(rtwdev, bb,
								 ifs_th0_us);
	for (i = 1; i < RTW89_IFS_CLM_NUM; i++) {
		ifs_th_l[i] = ifs_th_h[i - 1] + 1;
		ifs_th_h_us[i] = ifs_th_h_us[i - 1] * ifs_th_times;
		ifs_th_h[i] = rtw89_phy_ccx_us_to_idx(rtwdev, bb, ifs_th_h_us[i]);
	}

ifs_update_finished:
	if (!is_update)
		rtw89_debug(rtwdev, RTW89_DBG_PHY_TRACK,
			    "No need to update IFS_TH\n");

	return is_update;
}

static void rtw89_phy_ifs_clm_set_th_reg(struct rtw89_dev *rtwdev,
					 struct rtw89_bb_ctx *bb)
{
	const struct rtw89_phy_gen_def *phy = rtwdev->chip->phy_def;
	struct rtw89_env_monitor_info *env = &bb->env_monitor;
	const struct rtw89_ccx_regs *ccx = phy->ccx;
	u8 i = 0;

	rtw89_phy_write32_idx(rtwdev, ccx->ifs_t1_addr, ccx->ifs_t1_th_l_mask,
			      env->ifs_clm_th_l[0], bb->phy_idx);
	rtw89_phy_write32_idx(rtwdev, ccx->ifs_t2_addr, ccx->ifs_t2_th_l_mask,
			      env->ifs_clm_th_l[1], bb->phy_idx);
	rtw89_phy_write32_idx(rtwdev, ccx->ifs_t3_addr, ccx->ifs_t3_th_l_mask,
			      env->ifs_clm_th_l[2], bb->phy_idx);
	rtw89_phy_write32_idx(rtwdev, ccx->ifs_t4_addr, ccx->ifs_t4_th_l_mask,
			      env->ifs_clm_th_l[3], bb->phy_idx);

	rtw89_phy_write32_idx(rtwdev, ccx->ifs_t1_addr, ccx->ifs_t1_th_h_mask,
			      env->ifs_clm_th_h[0], bb->phy_idx);
	rtw89_phy_write32_idx(rtwdev, ccx->ifs_t2_addr, ccx->ifs_t2_th_h_mask,
			      env->ifs_clm_th_h[1], bb->phy_idx);
	rtw89_phy_write32_idx(rtwdev, ccx->ifs_t3_addr, ccx->ifs_t3_th_h_mask,
			      env->ifs_clm_th_h[2], bb->phy_idx);
	rtw89_phy_write32_idx(rtwdev, ccx->ifs_t4_addr, ccx->ifs_t4_th_h_mask,
			      env->ifs_clm_th_h[3], bb->phy_idx);

	for (i = 0; i < RTW89_IFS_CLM_NUM; i++)
		rtw89_debug(rtwdev, RTW89_DBG_PHY_TRACK,
			    "Update IFS_T%d_th{low, high} : {%d, %d}\n",
			    i + 1, env->ifs_clm_th_l[i], env->ifs_clm_th_h[i]);
}

static void rtw89_phy_ifs_clm_setting_init(struct rtw89_dev *rtwdev,
					   struct rtw89_bb_ctx *bb)
{
	const struct rtw89_phy_gen_def *phy = rtwdev->chip->phy_def;
	struct rtw89_env_monitor_info *env = &bb->env_monitor;
	const struct rtw89_ccx_regs *ccx = phy->ccx;
	struct rtw89_ccx_para_info para = {};

	env->ifs_clm_app = RTW89_IFS_CLM_BACKGROUND;
	env->ifs_clm_mntr_time = 0;

	para.ifs_clm_app = RTW89_IFS_CLM_INIT;
	if (rtw89_phy_ifs_clm_th_update_check(rtwdev, bb, &para))
		rtw89_phy_ifs_clm_set_th_reg(rtwdev, bb);

	rtw89_phy_write32_idx(rtwdev, ccx->ifs_cnt_addr, ccx->ifs_collect_en_mask, true,
			      bb->phy_idx);
	rtw89_phy_write32_idx(rtwdev, ccx->ifs_t1_addr, ccx->ifs_t1_en_mask, true,
			      bb->phy_idx);
	rtw89_phy_write32_idx(rtwdev, ccx->ifs_t2_addr, ccx->ifs_t2_en_mask, true,
			      bb->phy_idx);
	rtw89_phy_write32_idx(rtwdev, ccx->ifs_t3_addr, ccx->ifs_t3_en_mask, true,
			      bb->phy_idx);
	rtw89_phy_write32_idx(rtwdev, ccx->ifs_t4_addr, ccx->ifs_t4_en_mask, true,
			      bb->phy_idx);
}

static int rtw89_phy_ccx_racing_ctrl(struct rtw89_dev *rtwdev,
				     struct rtw89_bb_ctx *bb,
				     enum rtw89_env_racing_lv level)
{
	struct rtw89_env_monitor_info *env = &bb->env_monitor;
	int ret = 0;

	if (level >= RTW89_RAC_MAX_NUM) {
		rtw89_debug(rtwdev, RTW89_DBG_PHY_TRACK,
			    "[WARNING] Wrong LV=%d\n", level);
		return -EINVAL;
	}

	rtw89_debug(rtwdev, RTW89_DBG_PHY_TRACK,
		    "ccx_ongoing=%d, level:(%d)->(%d)\n", env->ccx_ongoing,
		    env->ccx_rac_lv, level);

	if (env->ccx_ongoing) {
		if (level <= env->ccx_rac_lv)
			ret = -EINVAL;
		else
			env->ccx_ongoing = false;
	}

	if (ret == 0)
		env->ccx_rac_lv = level;

	rtw89_debug(rtwdev, RTW89_DBG_PHY_TRACK, "ccx racing success=%d\n",
		    !ret);

	return ret;
}

static void rtw89_phy_ccx_trigger(struct rtw89_dev *rtwdev,
				  struct rtw89_bb_ctx *bb)
{
	const struct rtw89_phy_gen_def *phy = rtwdev->chip->phy_def;
	struct rtw89_env_monitor_info *env = &bb->env_monitor;
	const struct rtw89_ccx_regs *ccx = phy->ccx;

	rtw89_phy_write32_idx(rtwdev, ccx->ifs_cnt_addr, ccx->ifs_clm_cnt_clear_mask, 0,
			      bb->phy_idx);
	rtw89_phy_write32_idx(rtwdev, ccx->setting_addr, ccx->measurement_trig_mask, 0,
			      bb->phy_idx);
	rtw89_phy_write32_idx(rtwdev, ccx->ifs_cnt_addr, ccx->ifs_clm_cnt_clear_mask, 1,
			      bb->phy_idx);
	rtw89_phy_write32_idx(rtwdev, ccx->setting_addr, ccx->measurement_trig_mask, 1,
			      bb->phy_idx);

	env->ccx_ongoing = true;
}

static void rtw89_phy_ifs_clm_get_utility(struct rtw89_dev *rtwdev,
					  struct rtw89_bb_ctx *bb)
{
	struct rtw89_env_monitor_info *env = &bb->env_monitor;
	u8 i = 0;
	u32 res = 0;

	env->ifs_clm_tx_ratio =
		rtw89_phy_ccx_get_report(rtwdev, bb, env->ifs_clm_tx, PERCENT);
	env->ifs_clm_edcca_excl_cca_ratio =
		rtw89_phy_ccx_get_report(rtwdev, bb, env->ifs_clm_edcca_excl_cca,
					 PERCENT);
	env->ifs_clm_cck_fa_ratio =
		rtw89_phy_ccx_get_report(rtwdev, bb, env->ifs_clm_cckfa, PERCENT);
	env->ifs_clm_ofdm_fa_ratio =
		rtw89_phy_ccx_get_report(rtwdev, bb, env->ifs_clm_ofdmfa, PERCENT);
	env->ifs_clm_cck_cca_excl_fa_ratio =
		rtw89_phy_ccx_get_report(rtwdev, bb, env->ifs_clm_cckcca_excl_fa,
					 PERCENT);
	env->ifs_clm_ofdm_cca_excl_fa_ratio =
		rtw89_phy_ccx_get_report(rtwdev, bb, env->ifs_clm_ofdmcca_excl_fa,
					 PERCENT);
	env->ifs_clm_cck_fa_permil =
		rtw89_phy_ccx_get_report(rtwdev, bb, env->ifs_clm_cckfa, PERMIL);
	env->ifs_clm_ofdm_fa_permil =
		rtw89_phy_ccx_get_report(rtwdev, bb, env->ifs_clm_ofdmfa, PERMIL);

	for (i = 0; i < RTW89_IFS_CLM_NUM; i++) {
		if (env->ifs_clm_his[i] > ENV_MNTR_IFSCLM_HIS_MAX) {
			env->ifs_clm_ifs_avg[i] = ENV_MNTR_FAIL_DWORD;
		} else {
			env->ifs_clm_ifs_avg[i] =
				rtw89_phy_ccx_idx_to_us(rtwdev, bb,
							env->ifs_clm_avg[i]);
		}

		res = rtw89_phy_ccx_idx_to_us(rtwdev, bb, env->ifs_clm_cca[i]);
		res += env->ifs_clm_his[i] >> 1;
		if (env->ifs_clm_his[i])
			res /= env->ifs_clm_his[i];
		else
			res = 0;
		env->ifs_clm_cca_avg[i] = res;
	}

	rtw89_debug(rtwdev, RTW89_DBG_PHY_TRACK,
		    "IFS-CLM ratio {Tx, EDCCA_exclu_cca} = {%d, %d}\n",
		    env->ifs_clm_tx_ratio, env->ifs_clm_edcca_excl_cca_ratio);
	rtw89_debug(rtwdev, RTW89_DBG_PHY_TRACK,
		    "IFS-CLM FA ratio {CCK, OFDM} = {%d, %d}\n",
		    env->ifs_clm_cck_fa_ratio, env->ifs_clm_ofdm_fa_ratio);
	rtw89_debug(rtwdev, RTW89_DBG_PHY_TRACK,
		    "IFS-CLM FA permil {CCK, OFDM} = {%d, %d}\n",
		    env->ifs_clm_cck_fa_permil, env->ifs_clm_ofdm_fa_permil);
	rtw89_debug(rtwdev, RTW89_DBG_PHY_TRACK,
		    "IFS-CLM CCA_exclu_FA ratio {CCK, OFDM} = {%d, %d}\n",
		    env->ifs_clm_cck_cca_excl_fa_ratio,
		    env->ifs_clm_ofdm_cca_excl_fa_ratio);
	rtw89_debug(rtwdev, RTW89_DBG_PHY_TRACK,
		    "Time:[his, ifs_avg(us), cca_avg(us)]\n");
	for (i = 0; i < RTW89_IFS_CLM_NUM; i++)
		rtw89_debug(rtwdev, RTW89_DBG_PHY_TRACK, "T%d:[%d, %d, %d]\n",
			    i + 1, env->ifs_clm_his[i], env->ifs_clm_ifs_avg[i],
			    env->ifs_clm_cca_avg[i]);
}

static bool rtw89_phy_ifs_clm_get_result(struct rtw89_dev *rtwdev,
					 struct rtw89_bb_ctx *bb)
{
	const struct rtw89_phy_gen_def *phy = rtwdev->chip->phy_def;
	struct rtw89_env_monitor_info *env = &bb->env_monitor;
	const struct rtw89_ccx_regs *ccx = phy->ccx;
	u8 i = 0;

	if (rtw89_phy_read32_idx(rtwdev, ccx->ifs_total_addr,
				 ccx->ifs_cnt_done_mask, bb->phy_idx) == 0) {
		rtw89_debug(rtwdev, RTW89_DBG_PHY_TRACK,
			    "Get IFS_CLM report Fail\n");
		return false;
	}

	env->ifs_clm_tx =
		rtw89_phy_read32_idx(rtwdev, ccx->ifs_clm_tx_cnt_addr,
				     ccx->ifs_clm_tx_cnt_msk, bb->phy_idx);
	env->ifs_clm_edcca_excl_cca =
		rtw89_phy_read32_idx(rtwdev, ccx->ifs_clm_tx_cnt_addr,
				     ccx->ifs_clm_edcca_excl_cca_fa_mask, bb->phy_idx);
	env->ifs_clm_cckcca_excl_fa =
		rtw89_phy_read32_idx(rtwdev, ccx->ifs_clm_cca_addr,
				     ccx->ifs_clm_cckcca_excl_fa_mask, bb->phy_idx);
	env->ifs_clm_ofdmcca_excl_fa =
		rtw89_phy_read32_idx(rtwdev, ccx->ifs_clm_cca_addr,
				     ccx->ifs_clm_ofdmcca_excl_fa_mask, bb->phy_idx);
	env->ifs_clm_cckfa =
		rtw89_phy_read32_idx(rtwdev, ccx->ifs_clm_fa_addr,
				     ccx->ifs_clm_cck_fa_mask, bb->phy_idx);
	env->ifs_clm_ofdmfa =
		rtw89_phy_read32_idx(rtwdev, ccx->ifs_clm_fa_addr,
				     ccx->ifs_clm_ofdm_fa_mask, bb->phy_idx);

	env->ifs_clm_his[0] =
		rtw89_phy_read32_idx(rtwdev, ccx->ifs_his_addr,
				     ccx->ifs_t1_his_mask, bb->phy_idx);
	env->ifs_clm_his[1] =
		rtw89_phy_read32_idx(rtwdev, ccx->ifs_his_addr,
				     ccx->ifs_t2_his_mask, bb->phy_idx);
	env->ifs_clm_his[2] =
		rtw89_phy_read32_idx(rtwdev, ccx->ifs_his_addr,
				     ccx->ifs_t3_his_mask, bb->phy_idx);
	env->ifs_clm_his[3] =
		rtw89_phy_read32_idx(rtwdev, ccx->ifs_his_addr,
				     ccx->ifs_t4_his_mask, bb->phy_idx);

	env->ifs_clm_avg[0] =
		rtw89_phy_read32_idx(rtwdev, ccx->ifs_avg_l_addr,
				     ccx->ifs_t1_avg_mask, bb->phy_idx);
	env->ifs_clm_avg[1] =
		rtw89_phy_read32_idx(rtwdev, ccx->ifs_avg_l_addr,
				     ccx->ifs_t2_avg_mask, bb->phy_idx);
	env->ifs_clm_avg[2] =
		rtw89_phy_read32_idx(rtwdev, ccx->ifs_avg_h_addr,
				     ccx->ifs_t3_avg_mask, bb->phy_idx);
	env->ifs_clm_avg[3] =
		rtw89_phy_read32_idx(rtwdev, ccx->ifs_avg_h_addr,
				     ccx->ifs_t4_avg_mask, bb->phy_idx);

	env->ifs_clm_cca[0] =
		rtw89_phy_read32_idx(rtwdev, ccx->ifs_cca_l_addr,
				     ccx->ifs_t1_cca_mask, bb->phy_idx);
	env->ifs_clm_cca[1] =
		rtw89_phy_read32_idx(rtwdev, ccx->ifs_cca_l_addr,
				     ccx->ifs_t2_cca_mask, bb->phy_idx);
	env->ifs_clm_cca[2] =
		rtw89_phy_read32_idx(rtwdev, ccx->ifs_cca_h_addr,
				     ccx->ifs_t3_cca_mask, bb->phy_idx);
	env->ifs_clm_cca[3] =
		rtw89_phy_read32_idx(rtwdev, ccx->ifs_cca_h_addr,
				     ccx->ifs_t4_cca_mask, bb->phy_idx);

	env->ifs_clm_total_ifs =
		rtw89_phy_read32_idx(rtwdev, ccx->ifs_total_addr,
				     ccx->ifs_total_mask, bb->phy_idx);

	rtw89_debug(rtwdev, RTW89_DBG_PHY_TRACK, "IFS-CLM total_ifs = %d\n",
		    env->ifs_clm_total_ifs);
	rtw89_debug(rtwdev, RTW89_DBG_PHY_TRACK,
		    "{Tx, EDCCA_exclu_cca} = {%d, %d}\n",
		    env->ifs_clm_tx, env->ifs_clm_edcca_excl_cca);
	rtw89_debug(rtwdev, RTW89_DBG_PHY_TRACK,
		    "IFS-CLM FA{CCK, OFDM} = {%d, %d}\n",
		    env->ifs_clm_cckfa, env->ifs_clm_ofdmfa);
	rtw89_debug(rtwdev, RTW89_DBG_PHY_TRACK,
		    "IFS-CLM CCA_exclu_FA{CCK, OFDM} = {%d, %d}\n",
		    env->ifs_clm_cckcca_excl_fa, env->ifs_clm_ofdmcca_excl_fa);

	rtw89_debug(rtwdev, RTW89_DBG_PHY_TRACK, "Time:[his, avg, cca]\n");
	for (i = 0; i < RTW89_IFS_CLM_NUM; i++)
		rtw89_debug(rtwdev, RTW89_DBG_PHY_TRACK,
			    "T%d:[%d, %d, %d]\n", i + 1, env->ifs_clm_his[i],
			    env->ifs_clm_avg[i], env->ifs_clm_cca[i]);

	rtw89_phy_ifs_clm_get_utility(rtwdev, bb);

	return true;
}

static int rtw89_phy_ifs_clm_set(struct rtw89_dev *rtwdev,
				 struct rtw89_bb_ctx *bb,
				 struct rtw89_ccx_para_info *para)
{
	const struct rtw89_phy_gen_def *phy = rtwdev->chip->phy_def;
	struct rtw89_env_monitor_info *env = &bb->env_monitor;
	const struct rtw89_ccx_regs *ccx = phy->ccx;
	u32 period = 0;
	u32 unit_idx = 0;

	if (para->mntr_time == 0) {
		rtw89_debug(rtwdev, RTW89_DBG_PHY_TRACK,
			    "[WARN] MNTR_TIME is 0\n");
		return -EINVAL;
	}

	if (rtw89_phy_ccx_racing_ctrl(rtwdev, bb, para->rac_lv))
		return -EINVAL;

	if (para->mntr_time != env->ifs_clm_mntr_time) {
		rtw89_phy_ccx_ms_to_period_unit(rtwdev, para->mntr_time,
						&period, &unit_idx);
		rtw89_phy_write32_idx(rtwdev, ccx->ifs_cnt_addr,
				      ccx->ifs_clm_period_mask, period, bb->phy_idx);
		rtw89_phy_write32_idx(rtwdev, ccx->ifs_cnt_addr,
				      ccx->ifs_clm_cnt_unit_mask,
				      unit_idx, bb->phy_idx);

		rtw89_debug(rtwdev, RTW89_DBG_PHY_TRACK,
			    "Update IFS-CLM time ((%d)) -> ((%d))\n",
			    env->ifs_clm_mntr_time, para->mntr_time);

		env->ifs_clm_mntr_time = para->mntr_time;
		env->ccx_period = (u16)period;
		env->ccx_unit_idx = (u8)unit_idx;
	}

	if (rtw89_phy_ifs_clm_th_update_check(rtwdev, bb, para)) {
		env->ifs_clm_app = para->ifs_clm_app;
		rtw89_phy_ifs_clm_set_th_reg(rtwdev, bb);
	}

	return 0;
}

static void __rtw89_phy_env_monitor_track(struct rtw89_dev *rtwdev,
					  struct rtw89_bb_ctx *bb)
{
	struct rtw89_env_monitor_info *env = &bb->env_monitor;
	struct rtw89_ccx_para_info para = {};
	u8 chk_result = RTW89_PHY_ENV_MON_CCX_FAIL;

	env->ccx_watchdog_result = RTW89_PHY_ENV_MON_CCX_FAIL;
	if (env->ccx_manual_ctrl) {
		rtw89_debug(rtwdev, RTW89_DBG_PHY_TRACK,
			    "CCX in manual ctrl\n");
		return;
	}

	rtw89_debug(rtwdev, RTW89_DBG_PHY_TRACK,
		    "BB-%d env_monitor track\n", bb->phy_idx);

	/* only ifs_clm for now */
	if (rtw89_phy_ifs_clm_get_result(rtwdev, bb))
		env->ccx_watchdog_result |= RTW89_PHY_ENV_MON_IFS_CLM;

	rtw89_phy_ccx_racing_release(rtwdev, bb);
	para.mntr_time = 1900;
	para.rac_lv = RTW89_RAC_LV_1;
	para.ifs_clm_app = RTW89_IFS_CLM_BACKGROUND;

	if (rtw89_phy_ifs_clm_set(rtwdev, bb, &para) == 0)
		chk_result |= RTW89_PHY_ENV_MON_IFS_CLM;
	if (chk_result)
		rtw89_phy_ccx_trigger(rtwdev, bb);

	rtw89_debug(rtwdev, RTW89_DBG_PHY_TRACK,
		    "get_result=0x%x, chk_result:0x%x\n",
		    env->ccx_watchdog_result, chk_result);
}

void rtw89_phy_env_monitor_track(struct rtw89_dev *rtwdev)
{
	struct rtw89_bb_ctx *bb;

	rtw89_for_each_active_bb(rtwdev, bb)
		__rtw89_phy_env_monitor_track(rtwdev, bb);
}

static bool rtw89_physts_ie_page_valid(enum rtw89_phy_status_bitmap *ie_page)
{
	if (*ie_page >= RTW89_PHYSTS_BITMAP_NUM ||
	    *ie_page == RTW89_RSVD_9)
		return false;
	else if (*ie_page > RTW89_RSVD_9)
		*ie_page -= 1;

	return true;
}

static u32 rtw89_phy_get_ie_bitmap_addr(enum rtw89_phy_status_bitmap ie_page)
{
	static const u8 ie_page_shift = 2;

	return R_PHY_STS_BITMAP_ADDR_START + (ie_page << ie_page_shift);
}

static u32 rtw89_physts_get_ie_bitmap(struct rtw89_dev *rtwdev,
				      enum rtw89_phy_status_bitmap ie_page,
				      enum rtw89_phy_idx phy_idx)
{
	u32 addr;

	if (!rtw89_physts_ie_page_valid(&ie_page))
		return 0;

	addr = rtw89_phy_get_ie_bitmap_addr(ie_page);

	return rtw89_phy_read32_idx(rtwdev, addr, MASKDWORD, phy_idx);
}

static void rtw89_physts_set_ie_bitmap(struct rtw89_dev *rtwdev,
				       enum rtw89_phy_status_bitmap ie_page,
				       u32 val, enum rtw89_phy_idx phy_idx)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	u32 addr;

	if (!rtw89_physts_ie_page_valid(&ie_page))
		return;

	if (chip->chip_id == RTL8852A)
		val &= B_PHY_STS_BITMAP_MSK_52A;

	addr = rtw89_phy_get_ie_bitmap_addr(ie_page);
	rtw89_phy_write32_idx(rtwdev, addr, MASKDWORD, val, phy_idx);
}

static void rtw89_physts_enable_ie_bitmap(struct rtw89_dev *rtwdev,
					  enum rtw89_phy_status_bitmap bitmap,
					  enum rtw89_phy_status_ie_type ie,
					  bool enable, enum rtw89_phy_idx phy_idx)
{
	u32 val = rtw89_physts_get_ie_bitmap(rtwdev, bitmap, phy_idx);

	if (enable)
		val |= BIT(ie);
	else
		val &= ~BIT(ie);

	rtw89_physts_set_ie_bitmap(rtwdev, bitmap, val, phy_idx);
}

static void rtw89_physts_enable_fail_report(struct rtw89_dev *rtwdev,
					    bool enable,
					    enum rtw89_phy_idx phy_idx)
{
	const struct rtw89_phy_gen_def *phy = rtwdev->chip->phy_def;
	const struct rtw89_physts_regs *physts = phy->physts;

	if (enable) {
		rtw89_phy_write32_idx_clr(rtwdev, physts->setting_addr,
					  physts->dis_trigger_fail_mask, phy_idx);
		rtw89_phy_write32_idx_clr(rtwdev, physts->setting_addr,
					  physts->dis_trigger_brk_mask, phy_idx);
	} else {
		rtw89_phy_write32_idx_set(rtwdev, physts->setting_addr,
					  physts->dis_trigger_fail_mask, phy_idx);
		rtw89_phy_write32_idx_set(rtwdev, physts->setting_addr,
					  physts->dis_trigger_brk_mask, phy_idx);
	}
}

static void __rtw89_physts_parsing_init(struct rtw89_dev *rtwdev,
					enum rtw89_phy_idx phy_idx)
{
	u8 i;

	rtw89_physts_enable_fail_report(rtwdev, false, phy_idx);

	for (i = 0; i < RTW89_PHYSTS_BITMAP_NUM; i++) {
		if (i >= RTW89_CCK_PKT)
			rtw89_physts_enable_ie_bitmap(rtwdev, i,
						      RTW89_PHYSTS_IE09_FTR_0,
						      true, phy_idx);
		if ((i >= RTW89_CCK_BRK && i <= RTW89_VHT_MU) ||
		    (i >= RTW89_RSVD_9 && i <= RTW89_CCK_PKT))
			continue;
		rtw89_physts_enable_ie_bitmap(rtwdev, i,
					      RTW89_PHYSTS_IE24_OFDM_TD_PATH_A,
					      true, phy_idx);
	}
	rtw89_physts_enable_ie_bitmap(rtwdev, RTW89_VHT_PKT,
				      RTW89_PHYSTS_IE13_DL_MU_DEF, true, phy_idx);
	rtw89_physts_enable_ie_bitmap(rtwdev, RTW89_HE_PKT,
				      RTW89_PHYSTS_IE13_DL_MU_DEF, true, phy_idx);

	/* force IE01 for channel index, only channel field is valid */
	rtw89_physts_enable_ie_bitmap(rtwdev, RTW89_CCK_PKT,
				      RTW89_PHYSTS_IE01_CMN_OFDM, true, phy_idx);
}

static void rtw89_physts_parsing_init(struct rtw89_dev *rtwdev)
{
	__rtw89_physts_parsing_init(rtwdev, RTW89_PHY_0);
	if (rtwdev->dbcc_en)
		__rtw89_physts_parsing_init(rtwdev, RTW89_PHY_1);
}

static void rtw89_phy_dig_read_gain_table(struct rtw89_dev *rtwdev,
					  struct rtw89_bb_ctx *bb, int type)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const struct rtw89_phy_dig_gain_cfg *cfg;
	struct rtw89_dig_info *dig = &bb->dig;
	const char *msg;
	u8 i;
	s8 gain_base;
	s8 *gain_arr;
	u32 tmp;

	switch (type) {
	case RTW89_DIG_GAIN_LNA_G:
		gain_arr = dig->lna_gain_g;
		gain_base = LNA0_GAIN;
		cfg = chip->dig_table->cfg_lna_g;
		msg = "lna_gain_g";
		break;
	case RTW89_DIG_GAIN_TIA_G:
		gain_arr = dig->tia_gain_g;
		gain_base = TIA0_GAIN_G;
		cfg = chip->dig_table->cfg_tia_g;
		msg = "tia_gain_g";
		break;
	case RTW89_DIG_GAIN_LNA_A:
		gain_arr = dig->lna_gain_a;
		gain_base = LNA0_GAIN;
		cfg = chip->dig_table->cfg_lna_a;
		msg = "lna_gain_a";
		break;
	case RTW89_DIG_GAIN_TIA_A:
		gain_arr = dig->tia_gain_a;
		gain_base = TIA0_GAIN_A;
		cfg = chip->dig_table->cfg_tia_a;
		msg = "tia_gain_a";
		break;
	default:
		return;
	}

	for (i = 0; i < cfg->size; i++) {
		tmp = rtw89_phy_read32_idx(rtwdev, cfg->table[i].addr,
					   cfg->table[i].mask, bb->phy_idx);
		tmp >>= DIG_GAIN_SHIFT;
		gain_arr[i] = sign_extend32(tmp, U4_MAX_BIT) + gain_base;
		gain_base += DIG_GAIN;

		rtw89_debug(rtwdev, RTW89_DBG_DIG, "%s[%d]=%d\n",
			    msg, i, gain_arr[i]);
	}
}

static void rtw89_phy_dig_update_gain_para(struct rtw89_dev *rtwdev,
					   struct rtw89_bb_ctx *bb)
{
	struct rtw89_dig_info *dig = &bb->dig;
	u32 tmp;
	u8 i;

	if (!rtwdev->hal.support_igi)
		return;

	tmp = rtw89_phy_read32_idx(rtwdev, R_PATH0_IB_PKPW,
				   B_PATH0_IB_PKPW_MSK, bb->phy_idx);
	dig->ib_pkpwr = sign_extend32(tmp >> DIG_GAIN_SHIFT, U8_MAX_BIT);
	dig->ib_pbk = rtw89_phy_read32_idx(rtwdev, R_PATH0_IB_PBK,
					   B_PATH0_IB_PBK_MSK, bb->phy_idx);
	rtw89_debug(rtwdev, RTW89_DBG_DIG, "ib_pkpwr=%d, ib_pbk=%d\n",
		    dig->ib_pkpwr, dig->ib_pbk);

	for (i = RTW89_DIG_GAIN_LNA_G; i < RTW89_DIG_GAIN_MAX; i++)
		rtw89_phy_dig_read_gain_table(rtwdev, bb, i);
}

static const u8 rssi_nolink = 22;
static const u8 igi_rssi_th[IGI_RSSI_TH_NUM] = {68, 84, 90, 98, 104};
static const u16 fa_th_2g[FA_TH_NUM] = {22, 44, 66, 88};
static const u16 fa_th_5g[FA_TH_NUM] = {4, 8, 12, 16};
static const u16 fa_th_nolink[FA_TH_NUM] = {196, 352, 440, 528};

static void rtw89_phy_dig_update_rssi_info(struct rtw89_dev *rtwdev,
					   struct rtw89_bb_ctx *bb)
{
	struct rtw89_phy_ch_info *ch_info = &bb->ch_info;
	struct rtw89_dig_info *dig = &bb->dig;
	bool is_linked = rtwdev->total_sta_assoc > 0;

	if (is_linked) {
		dig->igi_rssi = ch_info->rssi_min >> 1;
	} else {
		rtw89_debug(rtwdev, RTW89_DBG_DIG, "RSSI update : NO Link\n");
		dig->igi_rssi = rssi_nolink;
	}
}

static void rtw89_phy_dig_update_para(struct rtw89_dev *rtwdev,
				      struct rtw89_bb_ctx *bb)
{
	const struct rtw89_chan *chan = rtw89_mgnt_chan_get(rtwdev, bb->phy_idx);
	struct rtw89_dig_info *dig = &bb->dig;
	bool is_linked = rtwdev->total_sta_assoc > 0;
	const u16 *fa_th_src = NULL;

	switch (chan->band_type) {
	case RTW89_BAND_2G:
		dig->lna_gain = dig->lna_gain_g;
		dig->tia_gain = dig->tia_gain_g;
		fa_th_src = is_linked ? fa_th_2g : fa_th_nolink;
		dig->force_gaincode_idx_en = false;
		dig->dyn_pd_th_en = true;
		break;
	case RTW89_BAND_5G:
	default:
		dig->lna_gain = dig->lna_gain_a;
		dig->tia_gain = dig->tia_gain_a;
		fa_th_src = is_linked ? fa_th_5g : fa_th_nolink;
		dig->force_gaincode_idx_en = true;
		dig->dyn_pd_th_en = true;
		break;
	}
	memcpy(dig->fa_th, fa_th_src, sizeof(dig->fa_th));
	memcpy(dig->igi_rssi_th, igi_rssi_th, sizeof(dig->igi_rssi_th));
}

static const u8 pd_low_th_offset = 16, dynamic_igi_min = 0x20;
static const u8 igi_max_performance_mode = 0x5a;
static const u8 dynamic_pd_threshold_max;

static void rtw89_phy_dig_para_reset(struct rtw89_dev *rtwdev,
				     struct rtw89_bb_ctx *bb)
{
	struct rtw89_dig_info *dig = &bb->dig;

	dig->cur_gaincode.lna_idx = LNA_IDX_MAX;
	dig->cur_gaincode.tia_idx = TIA_IDX_MAX;
	dig->cur_gaincode.rxb_idx = RXB_IDX_MAX;
	dig->force_gaincode.lna_idx = LNA_IDX_MAX;
	dig->force_gaincode.tia_idx = TIA_IDX_MAX;
	dig->force_gaincode.rxb_idx = RXB_IDX_MAX;

	dig->dyn_igi_max = igi_max_performance_mode;
	dig->dyn_igi_min = dynamic_igi_min;
	dig->dyn_pd_th_max = dynamic_pd_threshold_max;
	dig->pd_low_th_ofst = pd_low_th_offset;
	dig->is_linked_pre = false;
}

static void __rtw89_phy_dig_init(struct rtw89_dev *rtwdev,
				 struct rtw89_bb_ctx *bb)
{
	rtw89_debug(rtwdev, RTW89_DBG_DIG, "BB-%d dig_init\n", bb->phy_idx);

	rtw89_phy_dig_update_gain_para(rtwdev, bb);
	rtw89_phy_dig_reset(rtwdev, bb);
}

static void rtw89_phy_dig_init(struct rtw89_dev *rtwdev)
{
	struct rtw89_bb_ctx *bb;

	rtw89_for_each_capab_bb(rtwdev, bb)
		__rtw89_phy_dig_init(rtwdev, bb);
}

static u8 rtw89_phy_dig_lna_idx_by_rssi(struct rtw89_dev *rtwdev,
					struct rtw89_bb_ctx *bb, u8 rssi)
{
	struct rtw89_dig_info *dig = &bb->dig;
	u8 lna_idx;

	if (rssi < dig->igi_rssi_th[0])
		lna_idx = RTW89_DIG_GAIN_LNA_IDX6;
	else if (rssi < dig->igi_rssi_th[1])
		lna_idx = RTW89_DIG_GAIN_LNA_IDX5;
	else if (rssi < dig->igi_rssi_th[2])
		lna_idx = RTW89_DIG_GAIN_LNA_IDX4;
	else if (rssi < dig->igi_rssi_th[3])
		lna_idx = RTW89_DIG_GAIN_LNA_IDX3;
	else if (rssi < dig->igi_rssi_th[4])
		lna_idx = RTW89_DIG_GAIN_LNA_IDX2;
	else
		lna_idx = RTW89_DIG_GAIN_LNA_IDX1;

	return lna_idx;
}

static u8 rtw89_phy_dig_tia_idx_by_rssi(struct rtw89_dev *rtwdev,
					struct rtw89_bb_ctx *bb, u8 rssi)
{
	struct rtw89_dig_info *dig = &bb->dig;
	u8 tia_idx;

	if (rssi < dig->igi_rssi_th[0])
		tia_idx = RTW89_DIG_GAIN_TIA_IDX1;
	else
		tia_idx = RTW89_DIG_GAIN_TIA_IDX0;

	return tia_idx;
}

#define IB_PBK_BASE 110
#define WB_RSSI_BASE 10
static u8 rtw89_phy_dig_rxb_idx_by_rssi(struct rtw89_dev *rtwdev,
					struct rtw89_bb_ctx *bb, u8 rssi,
					struct rtw89_agc_gaincode_set *set)
{
	struct rtw89_dig_info *dig = &bb->dig;
	s8 lna_gain = dig->lna_gain[set->lna_idx];
	s8 tia_gain = dig->tia_gain[set->tia_idx];
	s32 wb_rssi = rssi + lna_gain + tia_gain;
	s32 rxb_idx_tmp = IB_PBK_BASE + WB_RSSI_BASE;
	u8 rxb_idx;

	rxb_idx_tmp += dig->ib_pkpwr - dig->ib_pbk - wb_rssi;
	rxb_idx = clamp_t(s32, rxb_idx_tmp, RXB_IDX_MIN, RXB_IDX_MAX);

	rtw89_debug(rtwdev, RTW89_DBG_DIG, "wb_rssi=%03d, rxb_idx_tmp=%03d\n",
		    wb_rssi, rxb_idx_tmp);

	return rxb_idx;
}

static void rtw89_phy_dig_gaincode_by_rssi(struct rtw89_dev *rtwdev,
					   struct rtw89_bb_ctx *bb, u8 rssi,
					   struct rtw89_agc_gaincode_set *set)
{
	set->lna_idx = rtw89_phy_dig_lna_idx_by_rssi(rtwdev, bb, rssi);
	set->tia_idx = rtw89_phy_dig_tia_idx_by_rssi(rtwdev, bb, rssi);
	set->rxb_idx = rtw89_phy_dig_rxb_idx_by_rssi(rtwdev, bb, rssi, set);

	rtw89_debug(rtwdev, RTW89_DBG_DIG,
		    "final_rssi=%03d, (lna,tia,rab)=(%d,%d,%02d)\n",
		    rssi, set->lna_idx, set->tia_idx, set->rxb_idx);
}

#define IGI_OFFSET_MAX 25
#define IGI_OFFSET_MUL 2
static void rtw89_phy_dig_igi_offset_by_env(struct rtw89_dev *rtwdev,
					    struct rtw89_bb_ctx *bb)
{
	struct rtw89_dig_info *dig = &bb->dig;
	struct rtw89_env_monitor_info *env = &bb->env_monitor;
	enum rtw89_dig_noisy_level noisy_lv;
	u8 igi_offset = dig->fa_rssi_ofst;
	u16 fa_ratio = 0;

	fa_ratio = env->ifs_clm_cck_fa_permil + env->ifs_clm_ofdm_fa_permil;

	if (fa_ratio < dig->fa_th[0])
		noisy_lv = RTW89_DIG_NOISY_LEVEL0;
	else if (fa_ratio < dig->fa_th[1])
		noisy_lv = RTW89_DIG_NOISY_LEVEL1;
	else if (fa_ratio < dig->fa_th[2])
		noisy_lv = RTW89_DIG_NOISY_LEVEL2;
	else if (fa_ratio < dig->fa_th[3])
		noisy_lv = RTW89_DIG_NOISY_LEVEL3;
	else
		noisy_lv = RTW89_DIG_NOISY_LEVEL_MAX;

	if (noisy_lv == RTW89_DIG_NOISY_LEVEL0 && igi_offset < 2)
		igi_offset = 0;
	else
		igi_offset += noisy_lv * IGI_OFFSET_MUL;

	igi_offset = min_t(u8, igi_offset, IGI_OFFSET_MAX);
	dig->fa_rssi_ofst = igi_offset;

	rtw89_debug(rtwdev, RTW89_DBG_DIG,
		    "fa_th: [+6 (%d) +4 (%d) +2 (%d) 0 (%d) -2 ]\n",
		    dig->fa_th[3], dig->fa_th[2], dig->fa_th[1], dig->fa_th[0]);

	rtw89_debug(rtwdev, RTW89_DBG_DIG,
		    "fa(CCK,OFDM,ALL)=(%d,%d,%d)%%, noisy_lv=%d, ofst=%d\n",
		    env->ifs_clm_cck_fa_permil, env->ifs_clm_ofdm_fa_permil,
		    env->ifs_clm_cck_fa_permil + env->ifs_clm_ofdm_fa_permil,
		    noisy_lv, igi_offset);
}

static void rtw89_phy_dig_set_lna_idx(struct rtw89_dev *rtwdev,
				      struct rtw89_bb_ctx *bb, u8 lna_idx)
{
	const struct rtw89_dig_regs *dig_regs = rtwdev->chip->dig_regs;

	rtw89_phy_write32_idx(rtwdev, dig_regs->p0_lna_init.addr,
			      dig_regs->p0_lna_init.mask, lna_idx, bb->phy_idx);
	rtw89_phy_write32_idx(rtwdev, dig_regs->p1_lna_init.addr,
			      dig_regs->p1_lna_init.mask, lna_idx, bb->phy_idx);
}

static void rtw89_phy_dig_set_tia_idx(struct rtw89_dev *rtwdev,
				      struct rtw89_bb_ctx *bb, u8 tia_idx)
{
	const struct rtw89_dig_regs *dig_regs = rtwdev->chip->dig_regs;

	rtw89_phy_write32_idx(rtwdev, dig_regs->p0_tia_init.addr,
			      dig_regs->p0_tia_init.mask, tia_idx, bb->phy_idx);
	rtw89_phy_write32_idx(rtwdev, dig_regs->p1_tia_init.addr,
			      dig_regs->p1_tia_init.mask, tia_idx, bb->phy_idx);
}

static void rtw89_phy_dig_set_rxb_idx(struct rtw89_dev *rtwdev,
				      struct rtw89_bb_ctx *bb, u8 rxb_idx)
{
	const struct rtw89_dig_regs *dig_regs = rtwdev->chip->dig_regs;

	rtw89_phy_write32_idx(rtwdev, dig_regs->p0_rxb_init.addr,
			      dig_regs->p0_rxb_init.mask, rxb_idx, bb->phy_idx);
	rtw89_phy_write32_idx(rtwdev, dig_regs->p1_rxb_init.addr,
			      dig_regs->p1_rxb_init.mask, rxb_idx, bb->phy_idx);
}

static void rtw89_phy_dig_set_igi_cr(struct rtw89_dev *rtwdev,
				     struct rtw89_bb_ctx *bb,
				     const struct rtw89_agc_gaincode_set set)
{
	if (!rtwdev->hal.support_igi)
		return;

	rtw89_phy_dig_set_lna_idx(rtwdev, bb, set.lna_idx);
	rtw89_phy_dig_set_tia_idx(rtwdev, bb, set.tia_idx);
	rtw89_phy_dig_set_rxb_idx(rtwdev, bb, set.rxb_idx);

	rtw89_debug(rtwdev, RTW89_DBG_DIG, "Set (lna,tia,rxb)=((%d,%d,%02d))\n",
		    set.lna_idx, set.tia_idx, set.rxb_idx);
}

static void rtw89_phy_dig_sdagc_follow_pagc_config(struct rtw89_dev *rtwdev,
						   struct rtw89_bb_ctx *bb,
						   bool enable)
{
	const struct rtw89_dig_regs *dig_regs = rtwdev->chip->dig_regs;

	rtw89_phy_write32_idx(rtwdev, dig_regs->p0_p20_pagcugc_en.addr,
			      dig_regs->p0_p20_pagcugc_en.mask, enable, bb->phy_idx);
	rtw89_phy_write32_idx(rtwdev, dig_regs->p0_s20_pagcugc_en.addr,
			      dig_regs->p0_s20_pagcugc_en.mask, enable, bb->phy_idx);
	rtw89_phy_write32_idx(rtwdev, dig_regs->p1_p20_pagcugc_en.addr,
			      dig_regs->p1_p20_pagcugc_en.mask, enable, bb->phy_idx);
	rtw89_phy_write32_idx(rtwdev, dig_regs->p1_s20_pagcugc_en.addr,
			      dig_regs->p1_s20_pagcugc_en.mask, enable, bb->phy_idx);

	rtw89_debug(rtwdev, RTW89_DBG_DIG, "sdagc_follow_pagc=%d\n", enable);
}

static void rtw89_phy_dig_config_igi(struct rtw89_dev *rtwdev,
				     struct rtw89_bb_ctx *bb)
{
	struct rtw89_dig_info *dig = &bb->dig;

	if (!rtwdev->hal.support_igi)
		return;

	if (dig->force_gaincode_idx_en) {
		rtw89_phy_dig_set_igi_cr(rtwdev, bb, dig->force_gaincode);
		rtw89_debug(rtwdev, RTW89_DBG_DIG,
			    "Force gaincode index enabled.\n");
	} else {
		rtw89_phy_dig_gaincode_by_rssi(rtwdev, bb, dig->igi_fa_rssi,
					       &dig->cur_gaincode);
		rtw89_phy_dig_set_igi_cr(rtwdev, bb, dig->cur_gaincode);
	}
}

static void rtw89_phy_dig_dyn_pd_th(struct rtw89_dev *rtwdev,
				    struct rtw89_bb_ctx *bb,
				    u8 rssi, bool enable)
{
	const struct rtw89_chan *chan = rtw89_mgnt_chan_get(rtwdev, bb->phy_idx);
	const struct rtw89_dig_regs *dig_regs = rtwdev->chip->dig_regs;
	enum rtw89_bandwidth cbw = chan->band_width;
	struct rtw89_dig_info *dig = &bb->dig;
	u8 final_rssi = 0, under_region = dig->pd_low_th_ofst;
	u8 ofdm_cca_th;
	s8 cck_cca_th;
	u32 pd_val = 0;

	if (rtwdev->chip->chip_gen == RTW89_CHIP_AX)
		under_region += PD_TH_SB_FLTR_CMP_VAL;

	switch (cbw) {
	case RTW89_CHANNEL_WIDTH_40:
		under_region += PD_TH_BW40_CMP_VAL;
		break;
	case RTW89_CHANNEL_WIDTH_80:
		under_region += PD_TH_BW80_CMP_VAL;
		break;
	case RTW89_CHANNEL_WIDTH_160:
		under_region += PD_TH_BW160_CMP_VAL;
		break;
	case RTW89_CHANNEL_WIDTH_20:
		fallthrough;
	default:
		under_region += PD_TH_BW20_CMP_VAL;
		break;
	}

	dig->dyn_pd_th_max = dig->igi_rssi;

	final_rssi = min_t(u8, rssi, dig->igi_rssi);
	ofdm_cca_th = clamp_t(u8, final_rssi, PD_TH_MIN_RSSI + under_region,
			      PD_TH_MAX_RSSI + under_region);

	if (enable) {
		pd_val = (ofdm_cca_th - under_region - PD_TH_MIN_RSSI) >> 1;
		rtw89_debug(rtwdev, RTW89_DBG_DIG,
			    "igi=%d, ofdm_ccaTH=%d, backoff=%d, PD_low=%d\n",
			    final_rssi, ofdm_cca_th, under_region, pd_val);
	} else {
		rtw89_debug(rtwdev, RTW89_DBG_DIG,
			    "Dynamic PD th disabled, Set PD_low_bd=0\n");
	}

	rtw89_phy_write32_idx(rtwdev, dig_regs->seg0_pd_reg,
			      dig_regs->pd_lower_bound_mask, pd_val, bb->phy_idx);
	rtw89_phy_write32_idx(rtwdev, dig_regs->seg0_pd_reg,
			      dig_regs->pd_spatial_reuse_en, enable, bb->phy_idx);

	if (!rtwdev->hal.support_cckpd)
		return;

	cck_cca_th = max_t(s8, final_rssi - under_region, CCKPD_TH_MIN_RSSI);
	pd_val = (u32)(cck_cca_th - IGI_RSSI_MAX);

	rtw89_debug(rtwdev, RTW89_DBG_DIG,
		    "igi=%d, cck_ccaTH=%d, backoff=%d, cck_PD_low=((%d))dB\n",
		    final_rssi, cck_cca_th, under_region, pd_val);

	rtw89_phy_write32_idx(rtwdev, dig_regs->bmode_pd_reg,
			      dig_regs->bmode_cca_rssi_limit_en, enable, bb->phy_idx);
	rtw89_phy_write32_idx(rtwdev, dig_regs->bmode_pd_lower_bound_reg,
			      dig_regs->bmode_rssi_nocca_low_th_mask, pd_val, bb->phy_idx);
}

void rtw89_phy_dig_reset(struct rtw89_dev *rtwdev, struct rtw89_bb_ctx *bb)
{
	struct rtw89_dig_info *dig = &bb->dig;

	dig->bypass_dig = false;
	rtw89_phy_dig_para_reset(rtwdev, bb);
	rtw89_phy_dig_set_igi_cr(rtwdev, bb, dig->force_gaincode);
	rtw89_phy_dig_dyn_pd_th(rtwdev, bb, rssi_nolink, false);
	rtw89_phy_dig_sdagc_follow_pagc_config(rtwdev, bb, false);
	rtw89_phy_dig_update_para(rtwdev, bb);
}

#define IGI_RSSI_MIN 10
#define ABS_IGI_MIN 0xc
static void __rtw89_phy_dig(struct rtw89_dev *rtwdev, struct rtw89_bb_ctx *bb)
{
	struct rtw89_dig_info *dig = &bb->dig;
	bool is_linked = rtwdev->total_sta_assoc > 0;
	u8 igi_min;

	if (unlikely(dig->bypass_dig)) {
		dig->bypass_dig = false;
		return;
	}

	rtw89_debug(rtwdev, RTW89_DBG_DIG, "BB-%d dig track\n", bb->phy_idx);

	rtw89_phy_dig_update_rssi_info(rtwdev, bb);

	if (!dig->is_linked_pre && is_linked) {
		rtw89_debug(rtwdev, RTW89_DBG_DIG, "First connected\n");
		rtw89_phy_dig_update_para(rtwdev, bb);
		dig->igi_fa_rssi = dig->igi_rssi;
	} else if (dig->is_linked_pre && !is_linked) {
		rtw89_debug(rtwdev, RTW89_DBG_DIG, "First disconnected\n");
		rtw89_phy_dig_update_para(rtwdev, bb);
		dig->igi_fa_rssi = dig->igi_rssi;
	}
	dig->is_linked_pre = is_linked;

	rtw89_phy_dig_igi_offset_by_env(rtwdev, bb);

	igi_min = max_t(int, dig->igi_rssi - IGI_RSSI_MIN, 0);
	dig->dyn_igi_max = min(igi_min + IGI_OFFSET_MAX, igi_max_performance_mode);
	dig->dyn_igi_min = max(igi_min, ABS_IGI_MIN);

	if (dig->dyn_igi_max >= dig->dyn_igi_min) {
		dig->igi_fa_rssi += dig->fa_rssi_ofst;
		dig->igi_fa_rssi = clamp(dig->igi_fa_rssi, dig->dyn_igi_min,
					 dig->dyn_igi_max);
	} else {
		dig->igi_fa_rssi = dig->dyn_igi_max;
	}

	rtw89_debug(rtwdev, RTW89_DBG_DIG,
		    "rssi=%03d, dyn_joint(max,min)=(%d,%d), final_rssi=%d\n",
		    dig->igi_rssi, dig->dyn_igi_max, dig->dyn_igi_min,
		    dig->igi_fa_rssi);

	rtw89_phy_dig_config_igi(rtwdev, bb);

	rtw89_phy_dig_dyn_pd_th(rtwdev, bb, dig->igi_fa_rssi, dig->dyn_pd_th_en);

	if (dig->dyn_pd_th_en && dig->igi_fa_rssi > dig->dyn_pd_th_max)
		rtw89_phy_dig_sdagc_follow_pagc_config(rtwdev, bb, true);
	else
		rtw89_phy_dig_sdagc_follow_pagc_config(rtwdev, bb, false);
}

void rtw89_phy_dig(struct rtw89_dev *rtwdev)
{
	struct rtw89_bb_ctx *bb;

	rtw89_for_each_active_bb(rtwdev, bb)
		__rtw89_phy_dig(rtwdev, bb);
}

static void __rtw89_phy_tx_path_div_sta_iter(struct rtw89_dev *rtwdev,
					     struct rtw89_sta_link *rtwsta_link)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	u8 rssi_a, rssi_b;
	u32 candidate;

	rssi_a = ewma_rssi_read(&rtwsta_link->rssi[RF_PATH_A]);
	rssi_b = ewma_rssi_read(&rtwsta_link->rssi[RF_PATH_B]);

	if (rssi_a > rssi_b + RTW89_TX_DIV_RSSI_RAW_TH)
		candidate = RF_A;
	else if (rssi_b > rssi_a + RTW89_TX_DIV_RSSI_RAW_TH)
		candidate = RF_B;
	else
		return;

	if (hal->antenna_tx == candidate)
		return;

	hal->antenna_tx = candidate;
	rtw89_fw_h2c_txpath_cmac_tbl(rtwdev, rtwsta_link);

	if (hal->antenna_tx == RF_A) {
		rtw89_phy_write32_mask(rtwdev, R_P0_RFMODE, B_P0_RFMODE_MUX, 0x12);
		rtw89_phy_write32_mask(rtwdev, R_P1_RFMODE, B_P1_RFMODE_MUX, 0x11);
	} else if (hal->antenna_tx == RF_B) {
		rtw89_phy_write32_mask(rtwdev, R_P0_RFMODE, B_P0_RFMODE_MUX, 0x11);
		rtw89_phy_write32_mask(rtwdev, R_P1_RFMODE, B_P1_RFMODE_MUX, 0x12);
	}
}

static void rtw89_phy_tx_path_div_sta_iter(void *data, struct ieee80211_sta *sta)
{
	struct rtw89_sta *rtwsta = sta_to_rtwsta(sta);
	struct rtw89_dev *rtwdev = rtwsta->rtwdev;
	struct rtw89_vif *rtwvif = rtwsta->rtwvif;
	struct ieee80211_vif *vif = rtwvif_to_vif(rtwvif);
	struct rtw89_vif_link *rtwvif_link;
	struct rtw89_sta_link *rtwsta_link;
	unsigned int link_id;
	bool *done = data;

	if (WARN(ieee80211_vif_is_mld(vif), "MLD mix path_div\n"))
		return;

	if (sta->tdls)
		return;

	if (*done)
		return;

	rtw89_sta_for_each_link(rtwsta, rtwsta_link, link_id) {
		rtwvif_link = rtwsta_link->rtwvif_link;
		if (rtwvif_link->wifi_role != RTW89_WIFI_ROLE_STATION)
			continue;

		*done = true;
		__rtw89_phy_tx_path_div_sta_iter(rtwdev, rtwsta_link);
		return;
	}
}

void rtw89_phy_tx_path_div_track(struct rtw89_dev *rtwdev)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	bool done = false;

	if (!hal->tx_path_diversity)
		return;

	ieee80211_iterate_stations_atomic(rtwdev->hw,
					  rtw89_phy_tx_path_div_sta_iter,
					  &done);
}

#define ANTDIV_MAIN 0
#define ANTDIV_AUX 1

static void rtw89_phy_antdiv_set_ant(struct rtw89_dev *rtwdev)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	u8 default_ant, optional_ant;

	if (!hal->ant_diversity || hal->antenna_tx == 0)
		return;

	if (hal->antenna_tx == RF_B) {
		default_ant = ANTDIV_AUX;
		optional_ant = ANTDIV_MAIN;
	} else {
		default_ant = ANTDIV_MAIN;
		optional_ant = ANTDIV_AUX;
	}

	rtw89_phy_write32_idx(rtwdev, R_P0_ANTSEL, B_P0_ANTSEL_CGCS_CTRL,
			      default_ant, RTW89_PHY_0);
	rtw89_phy_write32_idx(rtwdev, R_P0_ANTSEL, B_P0_ANTSEL_RX_ORI,
			      default_ant, RTW89_PHY_0);
	rtw89_phy_write32_idx(rtwdev, R_P0_ANTSEL, B_P0_ANTSEL_RX_ALT,
			      optional_ant, RTW89_PHY_0);
	rtw89_phy_write32_idx(rtwdev, R_P0_ANTSEL, B_P0_ANTSEL_TX_ORI,
			      default_ant, RTW89_PHY_0);
}

static void rtw89_phy_swap_hal_antenna(struct rtw89_dev *rtwdev)
{
	struct rtw89_hal *hal = &rtwdev->hal;

	hal->antenna_rx = hal->antenna_rx == RF_A ? RF_B : RF_A;
	hal->antenna_tx = hal->antenna_rx;
}

static void rtw89_phy_antdiv_decision_state(struct rtw89_dev *rtwdev)
{
	struct rtw89_antdiv_info *antdiv = &rtwdev->antdiv;
	struct rtw89_hal *hal = &rtwdev->hal;
	bool no_change = false;
	u8 main_rssi, aux_rssi;
	u8 main_evm, aux_evm;
	u32 candidate;

	antdiv->get_stats = false;
	antdiv->training_count = 0;

	main_rssi = rtw89_phy_antdiv_sts_instance_get_rssi(&antdiv->main_stats);
	main_evm = rtw89_phy_antdiv_sts_instance_get_evm(&antdiv->main_stats);
	aux_rssi = rtw89_phy_antdiv_sts_instance_get_rssi(&antdiv->aux_stats);
	aux_evm = rtw89_phy_antdiv_sts_instance_get_evm(&antdiv->aux_stats);

	if (main_evm > aux_evm + ANTDIV_EVM_DIFF_TH)
		candidate = RF_A;
	else if (aux_evm > main_evm + ANTDIV_EVM_DIFF_TH)
		candidate = RF_B;
	else if (main_rssi > aux_rssi + RTW89_TX_DIV_RSSI_RAW_TH)
		candidate = RF_A;
	else if (aux_rssi > main_rssi + RTW89_TX_DIV_RSSI_RAW_TH)
		candidate = RF_B;
	else
		no_change = true;

	if (no_change) {
		/* swap back from training antenna to original */
		rtw89_phy_swap_hal_antenna(rtwdev);
		return;
	}

	hal->antenna_tx = candidate;
	hal->antenna_rx = candidate;
}

static void rtw89_phy_antdiv_training_state(struct rtw89_dev *rtwdev)
{
	struct rtw89_antdiv_info *antdiv = &rtwdev->antdiv;
	u64 state_period;

	if (antdiv->training_count % 2 == 0) {
		if (antdiv->training_count == 0)
			rtw89_phy_antdiv_sts_reset(rtwdev);

		antdiv->get_stats = true;
		state_period = msecs_to_jiffies(ANTDIV_TRAINNING_INTVL);
	} else {
		antdiv->get_stats = false;
		state_period = msecs_to_jiffies(ANTDIV_DELAY);

		rtw89_phy_swap_hal_antenna(rtwdev);
		rtw89_phy_antdiv_set_ant(rtwdev);
	}

	antdiv->training_count++;
	wiphy_delayed_work_queue(rtwdev->hw->wiphy, &rtwdev->antdiv_work,
				 state_period);
}

void rtw89_phy_antdiv_work(struct wiphy *wiphy, struct wiphy_work *work)
{
	struct rtw89_dev *rtwdev = container_of(work, struct rtw89_dev,
						antdiv_work.work);
	struct rtw89_antdiv_info *antdiv = &rtwdev->antdiv;

	lockdep_assert_wiphy(wiphy);

	if (antdiv->training_count <= ANTDIV_TRAINNING_CNT) {
		rtw89_phy_antdiv_training_state(rtwdev);
	} else {
		rtw89_phy_antdiv_decision_state(rtwdev);
		rtw89_phy_antdiv_set_ant(rtwdev);
	}
}

void rtw89_phy_antdiv_track(struct rtw89_dev *rtwdev)
{
	struct rtw89_antdiv_info *antdiv = &rtwdev->antdiv;
	struct rtw89_hal *hal = &rtwdev->hal;
	u8 rssi, rssi_pre;

	if (!hal->ant_diversity || hal->ant_diversity_fixed)
		return;

	rssi = rtw89_phy_antdiv_sts_instance_get_rssi(&antdiv->target_stats);
	rssi_pre = antdiv->rssi_pre;
	antdiv->rssi_pre = rssi;
	rtw89_phy_antdiv_sts_instance_reset(&antdiv->target_stats);

	if (abs((int)rssi - (int)rssi_pre) < ANTDIV_RSSI_DIFF_TH)
		return;

	antdiv->training_count = 0;
	wiphy_delayed_work_queue(rtwdev->hw->wiphy, &rtwdev->antdiv_work, 0);
}

static void __rtw89_phy_env_monitor_init(struct rtw89_dev *rtwdev,
					 struct rtw89_bb_ctx *bb)
{
	rtw89_debug(rtwdev, RTW89_DBG_PHY_TRACK,
		    "BB-%d env_monitor init\n", bb->phy_idx);

	rtw89_phy_ccx_top_setting_init(rtwdev, bb);
	rtw89_phy_ifs_clm_setting_init(rtwdev, bb);
}

static void rtw89_phy_env_monitor_init(struct rtw89_dev *rtwdev)
{
	struct rtw89_bb_ctx *bb;

	rtw89_for_each_capab_bb(rtwdev, bb)
		__rtw89_phy_env_monitor_init(rtwdev, bb);
}

static void __rtw89_phy_edcca_init(struct rtw89_dev *rtwdev,
				   struct rtw89_bb_ctx *bb)
{
	const struct rtw89_edcca_regs *edcca_regs = rtwdev->chip->edcca_regs;
	struct rtw89_edcca_bak *edcca_bak = &bb->edcca_bak;

	rtw89_debug(rtwdev, RTW89_DBG_EDCCA, "BB-%d edcca init\n", bb->phy_idx);

	memset(edcca_bak, 0, sizeof(*edcca_bak));

	if (rtwdev->chip->chip_id == RTL8922A && rtwdev->hal.cv == CHIP_CAV) {
		rtw89_phy_set_phy_regs(rtwdev, R_TXGATING, B_TXGATING_EN, 0);
		rtw89_phy_set_phy_regs(rtwdev, R_CTLTOP, B_CTLTOP_VAL, 2);
		rtw89_phy_set_phy_regs(rtwdev, R_CTLTOP, B_CTLTOP_ON, 1);
		rtw89_phy_set_phy_regs(rtwdev, R_SPOOF_CG, B_SPOOF_CG_EN, 0);
		rtw89_phy_set_phy_regs(rtwdev, R_DFS_FFT_CG, B_DFS_CG_EN, 0);
		rtw89_phy_set_phy_regs(rtwdev, R_DFS_FFT_CG, B_DFS_FFT_EN, 0);
		rtw89_phy_set_phy_regs(rtwdev, R_SEGSND, B_SEGSND_EN, 0);
		rtw89_phy_set_phy_regs(rtwdev, R_SEGSND, B_SEGSND_EN, 1);
		rtw89_phy_set_phy_regs(rtwdev, R_DFS_FFT_CG, B_DFS_FFT_EN, 1);
	}

	rtw89_phy_write32_idx(rtwdev, edcca_regs->tx_collision_t2r_st,
			      edcca_regs->tx_collision_t2r_st_mask, 0x29, bb->phy_idx);
}

static void rtw89_phy_edcca_init(struct rtw89_dev *rtwdev)
{
	struct rtw89_bb_ctx *bb;

	rtw89_for_each_capab_bb(rtwdev, bb)
		__rtw89_phy_edcca_init(rtwdev, bb);
}

void rtw89_phy_dm_init(struct rtw89_dev *rtwdev)
{
	rtw89_phy_stat_init(rtwdev);

	rtw89_chip_bb_sethw(rtwdev);

	rtw89_phy_env_monitor_init(rtwdev);
	rtw89_physts_parsing_init(rtwdev);
	rtw89_phy_dig_init(rtwdev);
	rtw89_phy_cfo_init(rtwdev);
	rtw89_phy_bb_wrap_init(rtwdev);
	rtw89_phy_edcca_init(rtwdev);
	rtw89_phy_ch_info_init(rtwdev);
	rtw89_phy_ul_tb_info_init(rtwdev);
	rtw89_phy_antdiv_init(rtwdev);
	rtw89_chip_rfe_gpio(rtwdev);
	rtw89_phy_antdiv_set_ant(rtwdev);

	rtw89_chip_rfk_hw_init(rtwdev);
	rtw89_phy_init_rf_nctl(rtwdev);
	rtw89_chip_rfk_init(rtwdev);
	rtw89_chip_set_txpwr_ctrl(rtwdev);
	rtw89_chip_power_trim(rtwdev);
	rtw89_chip_cfg_txrx_path(rtwdev);
}

void rtw89_phy_dm_reinit(struct rtw89_dev *rtwdev)
{
	rtw89_phy_env_monitor_init(rtwdev);
	rtw89_physts_parsing_init(rtwdev);
}

void rtw89_phy_set_bss_color(struct rtw89_dev *rtwdev,
			     struct rtw89_vif_link *rtwvif_link)
{
	struct ieee80211_vif *vif = rtwvif_link_to_vif(rtwvif_link);
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const struct rtw89_reg_def *bss_clr_vld = &chip->bss_clr_vld;
	enum rtw89_phy_idx phy_idx = rtwvif_link->phy_idx;
	struct ieee80211_bss_conf *bss_conf;
	u8 bss_color;

	rcu_read_lock();

	bss_conf = rtw89_vif_rcu_dereference_link(rtwvif_link, true);
	if (!bss_conf->he_support || !vif->cfg.assoc) {
		rcu_read_unlock();
		return;
	}

	bss_color = bss_conf->he_bss_color.color;

	rcu_read_unlock();

	rtw89_phy_write32_idx(rtwdev, bss_clr_vld->addr, bss_clr_vld->mask, 0x1,
			      phy_idx);
	rtw89_phy_write32_idx(rtwdev, chip->bss_clr_map_reg, B_BSS_CLR_MAP_TGT,
			      bss_color, phy_idx);
	rtw89_phy_write32_idx(rtwdev, chip->bss_clr_map_reg, B_BSS_CLR_MAP_STAID,
			      vif->cfg.aid, phy_idx);
}

static bool rfk_chan_validate_desc(const struct rtw89_rfk_chan_desc *desc)
{
	return desc->ch != 0;
}

static bool rfk_chan_is_equivalent(const struct rtw89_rfk_chan_desc *desc,
				   const struct rtw89_chan *chan)
{
	if (!rfk_chan_validate_desc(desc))
		return false;

	if (desc->ch != chan->channel)
		return false;

	if (desc->has_band && desc->band != chan->band_type)
		return false;

	if (desc->has_bw && desc->bw != chan->band_width)
		return false;

	return true;
}

struct rfk_chan_iter_data {
	const struct rtw89_rfk_chan_desc desc;
	unsigned int found;
};

static int rfk_chan_iter_search(const struct rtw89_chan *chan, void *data)
{
	struct rfk_chan_iter_data *iter_data = data;

	if (rfk_chan_is_equivalent(&iter_data->desc, chan))
		iter_data->found++;

	return 0;
}

u8 rtw89_rfk_chan_lookup(struct rtw89_dev *rtwdev,
			 const struct rtw89_rfk_chan_desc *desc, u8 desc_nr,
			 const struct rtw89_chan *target_chan)
{
	int sel = -1;
	u8 i;

	for (i = 0; i < desc_nr; i++) {
		struct rfk_chan_iter_data iter_data = {
			.desc = desc[i],
		};

		if (rfk_chan_is_equivalent(&desc[i], target_chan))
			return i;

		rtw89_iterate_entity_chan(rtwdev, rfk_chan_iter_search, &iter_data);
		if (!iter_data.found && sel == -1)
			sel = i;
	}

	if (sel == -1) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "no idle rfk entry; force replace the first\n");
		sel = 0;
	}

	return sel;
}
EXPORT_SYMBOL(rtw89_rfk_chan_lookup);

static void
_rfk_write_rf(struct rtw89_dev *rtwdev, const struct rtw89_reg5_def *def)
{
	rtw89_write_rf(rtwdev, def->path, def->addr, def->mask, def->data);
}

static void
_rfk_write32_mask(struct rtw89_dev *rtwdev, const struct rtw89_reg5_def *def)
{
	rtw89_phy_write32_mask(rtwdev, def->addr, def->mask, def->data);
}

static void
_rfk_write32_set(struct rtw89_dev *rtwdev, const struct rtw89_reg5_def *def)
{
	rtw89_phy_write32_set(rtwdev, def->addr, def->mask);
}

static void
_rfk_write32_clr(struct rtw89_dev *rtwdev, const struct rtw89_reg5_def *def)
{
	rtw89_phy_write32_clr(rtwdev, def->addr, def->mask);
}

static void
_rfk_delay(struct rtw89_dev *rtwdev, const struct rtw89_reg5_def *def)
{
	udelay(def->data);
}

static void
(*_rfk_handler[])(struct rtw89_dev *rtwdev, const struct rtw89_reg5_def *def) = {
	[RTW89_RFK_F_WRF] = _rfk_write_rf,
	[RTW89_RFK_F_WM] = _rfk_write32_mask,
	[RTW89_RFK_F_WS] = _rfk_write32_set,
	[RTW89_RFK_F_WC] = _rfk_write32_clr,
	[RTW89_RFK_F_DELAY] = _rfk_delay,
};

static_assert(ARRAY_SIZE(_rfk_handler) == RTW89_RFK_F_NUM);

void
rtw89_rfk_parser(struct rtw89_dev *rtwdev, const struct rtw89_rfk_tbl *tbl)
{
	const struct rtw89_reg5_def *p = tbl->defs;
	const struct rtw89_reg5_def *end = tbl->defs + tbl->size;

	for (; p < end; p++)
		_rfk_handler[p->flag](rtwdev, p);
}
EXPORT_SYMBOL(rtw89_rfk_parser);

#define RTW89_TSSI_FAST_MODE_NUM 4

static const struct rtw89_reg_def rtw89_tssi_fastmode_regs_flat[RTW89_TSSI_FAST_MODE_NUM] = {
	{0xD934, 0xff0000},
	{0xD934, 0xff000000},
	{0xD938, 0xff},
	{0xD934, 0xff00},
};

static const struct rtw89_reg_def rtw89_tssi_fastmode_regs_level[RTW89_TSSI_FAST_MODE_NUM] = {
	{0xD930, 0xff0000},
	{0xD930, 0xff000000},
	{0xD934, 0xff},
	{0xD930, 0xff00},
};

static
void rtw89_phy_tssi_ctrl_set_fast_mode_cfg(struct rtw89_dev *rtwdev,
					   enum rtw89_mac_idx mac_idx,
					   enum rtw89_tssi_bandedge_cfg bandedge_cfg,
					   u32 val)
{
	const struct rtw89_reg_def *regs;
	u32 reg;
	int i;

	if (bandedge_cfg == RTW89_TSSI_BANDEDGE_FLAT)
		regs = rtw89_tssi_fastmode_regs_flat;
	else
		regs = rtw89_tssi_fastmode_regs_level;

	for (i = 0; i < RTW89_TSSI_FAST_MODE_NUM; i++) {
		reg = rtw89_mac_reg_by_idx(rtwdev, regs[i].addr, mac_idx);
		rtw89_write32_mask(rtwdev, reg, regs[i].mask, val);
	}
}

static const struct rtw89_reg_def rtw89_tssi_bandedge_regs_flat[RTW89_TSSI_SBW_NUM] = {
	{0xD91C, 0xff000000},
	{0xD920, 0xff},
	{0xD920, 0xff00},
	{0xD920, 0xff0000},
	{0xD920, 0xff000000},
	{0xD924, 0xff},
	{0xD924, 0xff00},
	{0xD914, 0xff000000},
	{0xD918, 0xff},
	{0xD918, 0xff00},
	{0xD918, 0xff0000},
	{0xD918, 0xff000000},
	{0xD91C, 0xff},
	{0xD91C, 0xff00},
	{0xD91C, 0xff0000},
};

static const struct rtw89_reg_def rtw89_tssi_bandedge_regs_level[RTW89_TSSI_SBW_NUM] = {
	{0xD910, 0xff},
	{0xD910, 0xff00},
	{0xD910, 0xff0000},
	{0xD910, 0xff000000},
	{0xD914, 0xff},
	{0xD914, 0xff00},
	{0xD914, 0xff0000},
	{0xD908, 0xff},
	{0xD908, 0xff00},
	{0xD908, 0xff0000},
	{0xD908, 0xff000000},
	{0xD90C, 0xff},
	{0xD90C, 0xff00},
	{0xD90C, 0xff0000},
	{0xD90C, 0xff000000},
};

void rtw89_phy_tssi_ctrl_set_bandedge_cfg(struct rtw89_dev *rtwdev,
					  enum rtw89_mac_idx mac_idx,
					  enum rtw89_tssi_bandedge_cfg bandedge_cfg)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const struct rtw89_reg_def *regs;
	const u32 *data;
	u32 reg;
	int i;

	if (bandedge_cfg >= RTW89_TSSI_CFG_NUM)
		return;

	if (bandedge_cfg == RTW89_TSSI_BANDEDGE_FLAT)
		regs = rtw89_tssi_bandedge_regs_flat;
	else
		regs = rtw89_tssi_bandedge_regs_level;

	data = chip->tssi_dbw_table->data[bandedge_cfg];

	for (i = 0; i < RTW89_TSSI_SBW_NUM; i++) {
		reg = rtw89_mac_reg_by_idx(rtwdev, regs[i].addr, mac_idx);
		rtw89_write32_mask(rtwdev, reg, regs[i].mask, data[i]);
	}

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_BANDEDGE_CFG, mac_idx);
	rtw89_write32_mask(rtwdev, reg, B_AX_BANDEDGE_CFG_IDX_MASK, bandedge_cfg);

	rtw89_phy_tssi_ctrl_set_fast_mode_cfg(rtwdev, mac_idx, bandedge_cfg,
					      data[RTW89_TSSI_SBW20]);
}
EXPORT_SYMBOL(rtw89_phy_tssi_ctrl_set_bandedge_cfg);

static
const u8 rtw89_ch_base_table[16] = {1, 0xff,
				    36, 100, 132, 149, 0xff,
				    1, 33, 65, 97, 129, 161, 193, 225, 0xff};
#define RTW89_CH_BASE_IDX_2G		0
#define RTW89_CH_BASE_IDX_5G_FIRST	2
#define RTW89_CH_BASE_IDX_5G_LAST	5
#define RTW89_CH_BASE_IDX_6G_FIRST	7
#define RTW89_CH_BASE_IDX_6G_LAST	14

#define RTW89_CH_BASE_IDX_MASK		GENMASK(7, 4)
#define RTW89_CH_OFFSET_MASK		GENMASK(3, 0)

u8 rtw89_encode_chan_idx(struct rtw89_dev *rtwdev, u8 central_ch, u8 band)
{
	u8 chan_idx;
	u8 last, first;
	u8 idx;

	switch (band) {
	case RTW89_BAND_2G:
		chan_idx = FIELD_PREP(RTW89_CH_BASE_IDX_MASK, RTW89_CH_BASE_IDX_2G) |
			   FIELD_PREP(RTW89_CH_OFFSET_MASK, central_ch);
		return chan_idx;
	case RTW89_BAND_5G:
		first = RTW89_CH_BASE_IDX_5G_FIRST;
		last = RTW89_CH_BASE_IDX_5G_LAST;
		break;
	case RTW89_BAND_6G:
		first = RTW89_CH_BASE_IDX_6G_FIRST;
		last = RTW89_CH_BASE_IDX_6G_LAST;
		break;
	default:
		rtw89_warn(rtwdev, "Unsupported band %d\n", band);
		return 0;
	}

	for (idx = last; idx >= first; idx--)
		if (central_ch >= rtw89_ch_base_table[idx])
			break;

	if (idx < first) {
		rtw89_warn(rtwdev, "Unknown band %d channel %d\n", band, central_ch);
		return 0;
	}

	chan_idx = FIELD_PREP(RTW89_CH_BASE_IDX_MASK, idx) |
		   FIELD_PREP(RTW89_CH_OFFSET_MASK,
			      (central_ch - rtw89_ch_base_table[idx]) >> 1);
	return chan_idx;
}
EXPORT_SYMBOL(rtw89_encode_chan_idx);

void rtw89_decode_chan_idx(struct rtw89_dev *rtwdev, u8 chan_idx,
			   u8 *ch, enum nl80211_band *band)
{
	u8 idx, offset;

	idx = FIELD_GET(RTW89_CH_BASE_IDX_MASK, chan_idx);
	offset = FIELD_GET(RTW89_CH_OFFSET_MASK, chan_idx);

	if (idx == RTW89_CH_BASE_IDX_2G) {
		*band = NL80211_BAND_2GHZ;
		*ch = offset;
		return;
	}

	*band = idx <= RTW89_CH_BASE_IDX_5G_LAST ? NL80211_BAND_5GHZ : NL80211_BAND_6GHZ;
	*ch = rtw89_ch_base_table[idx] + (offset << 1);
}
EXPORT_SYMBOL(rtw89_decode_chan_idx);

void rtw89_phy_config_edcca(struct rtw89_dev *rtwdev,
			    struct rtw89_bb_ctx *bb, bool scan)
{
	const struct rtw89_edcca_regs *edcca_regs = rtwdev->chip->edcca_regs;
	struct rtw89_edcca_bak *edcca_bak = &bb->edcca_bak;

	if (scan) {
		edcca_bak->a =
			rtw89_phy_read32_idx(rtwdev, edcca_regs->edcca_level,
					     edcca_regs->edcca_mask, bb->phy_idx);
		edcca_bak->p =
			rtw89_phy_read32_idx(rtwdev, edcca_regs->edcca_level,
					     edcca_regs->edcca_p_mask, bb->phy_idx);
		edcca_bak->ppdu =
			rtw89_phy_read32_idx(rtwdev, edcca_regs->ppdu_level,
					     edcca_regs->ppdu_mask, bb->phy_idx);

		rtw89_phy_write32_idx(rtwdev, edcca_regs->edcca_level,
				      edcca_regs->edcca_mask, EDCCA_MAX, bb->phy_idx);
		rtw89_phy_write32_idx(rtwdev, edcca_regs->edcca_level,
				      edcca_regs->edcca_p_mask, EDCCA_MAX, bb->phy_idx);
		rtw89_phy_write32_idx(rtwdev, edcca_regs->ppdu_level,
				      edcca_regs->ppdu_mask, EDCCA_MAX, bb->phy_idx);
	} else {
		rtw89_phy_write32_idx(rtwdev, edcca_regs->edcca_level,
				      edcca_regs->edcca_mask,
				      edcca_bak->a, bb->phy_idx);
		rtw89_phy_write32_idx(rtwdev, edcca_regs->edcca_level,
				      edcca_regs->edcca_p_mask,
				      edcca_bak->p, bb->phy_idx);
		rtw89_phy_write32_idx(rtwdev, edcca_regs->ppdu_level,
				      edcca_regs->ppdu_mask,
				      edcca_bak->ppdu, bb->phy_idx);
	}
}

static void rtw89_phy_edcca_log(struct rtw89_dev *rtwdev, struct rtw89_bb_ctx *bb)
{
	const struct rtw89_edcca_regs *edcca_regs = rtwdev->chip->edcca_regs;
	const struct rtw89_edcca_p_regs *edcca_p_regs;
	bool flag_fb, flag_p20, flag_s20, flag_s40, flag_s80;
	s8 pwdb_fb, pwdb_p20, pwdb_s20, pwdb_s40, pwdb_s80;
	u8 path, per20_bitmap;
	u8 pwdb[8];
	u32 tmp;

	if (!rtw89_debug_is_enabled(rtwdev, RTW89_DBG_EDCCA))
		return;

	if (bb->phy_idx == RTW89_PHY_1)
		edcca_p_regs = &edcca_regs->p[RTW89_PHY_1];
	else
		edcca_p_regs = &edcca_regs->p[RTW89_PHY_0];

	if (rtwdev->chip->chip_id == RTL8922A)
		rtw89_phy_write32_mask(rtwdev, edcca_regs->rpt_sel_be,
				       edcca_regs->rpt_sel_be_mask, 0);

	rtw89_phy_write32_mask(rtwdev, edcca_p_regs->rpt_sel,
			       edcca_p_regs->rpt_sel_mask, 0);
	tmp = rtw89_phy_read32(rtwdev, edcca_p_regs->rpt_b);
	path = u32_get_bits(tmp, B_EDCCA_RPT_B_PATH_MASK);
	flag_s80 = u32_get_bits(tmp, B_EDCCA_RPT_B_S80);
	flag_s40 = u32_get_bits(tmp, B_EDCCA_RPT_B_S40);
	flag_s20 = u32_get_bits(tmp, B_EDCCA_RPT_B_S20);
	flag_p20 = u32_get_bits(tmp, B_EDCCA_RPT_B_P20);
	flag_fb = u32_get_bits(tmp, B_EDCCA_RPT_B_FB);
	pwdb_s20 = u32_get_bits(tmp, MASKBYTE1);
	pwdb_p20 = u32_get_bits(tmp, MASKBYTE2);
	pwdb_fb = u32_get_bits(tmp, MASKBYTE3);

	rtw89_phy_write32_mask(rtwdev, edcca_p_regs->rpt_sel,
			       edcca_p_regs->rpt_sel_mask, 4);
	tmp = rtw89_phy_read32(rtwdev, edcca_p_regs->rpt_b);
	pwdb_s80 = u32_get_bits(tmp, MASKBYTE1);
	pwdb_s40 = u32_get_bits(tmp, MASKBYTE2);

	per20_bitmap = rtw89_phy_read32_mask(rtwdev, edcca_p_regs->rpt_a,
					     MASKBYTE0);

	if (rtwdev->chip->chip_id == RTL8922A) {
		rtw89_phy_write32_mask(rtwdev, edcca_regs->rpt_sel_be,
				       edcca_regs->rpt_sel_be_mask, 4);
		tmp = rtw89_phy_read32(rtwdev, edcca_p_regs->rpt_b);
		pwdb[0] = u32_get_bits(tmp, MASKBYTE3);
		pwdb[1] = u32_get_bits(tmp, MASKBYTE2);
		pwdb[2] = u32_get_bits(tmp, MASKBYTE1);
		pwdb[3] = u32_get_bits(tmp, MASKBYTE0);

		rtw89_phy_write32_mask(rtwdev, edcca_regs->rpt_sel_be,
				       edcca_regs->rpt_sel_be_mask, 5);
		tmp = rtw89_phy_read32(rtwdev, edcca_p_regs->rpt_b);
		pwdb[4] = u32_get_bits(tmp, MASKBYTE3);
		pwdb[5] = u32_get_bits(tmp, MASKBYTE2);
		pwdb[6] = u32_get_bits(tmp, MASKBYTE1);
		pwdb[7] = u32_get_bits(tmp, MASKBYTE0);
	} else {
		rtw89_phy_write32_mask(rtwdev, edcca_p_regs->rpt_sel,
				       edcca_p_regs->rpt_sel_mask, 0);
		tmp = rtw89_phy_read32(rtwdev, edcca_p_regs->rpt_a);
		pwdb[0] = u32_get_bits(tmp, MASKBYTE3);
		pwdb[1] = u32_get_bits(tmp, MASKBYTE2);

		rtw89_phy_write32_mask(rtwdev, edcca_p_regs->rpt_sel,
				       edcca_p_regs->rpt_sel_mask, 1);
		tmp = rtw89_phy_read32(rtwdev, edcca_p_regs->rpt_a);
		pwdb[2] = u32_get_bits(tmp, MASKBYTE3);
		pwdb[3] = u32_get_bits(tmp, MASKBYTE2);

		rtw89_phy_write32_mask(rtwdev, edcca_p_regs->rpt_sel,
				       edcca_p_regs->rpt_sel_mask, 2);
		tmp = rtw89_phy_read32(rtwdev, edcca_p_regs->rpt_a);
		pwdb[4] = u32_get_bits(tmp, MASKBYTE3);
		pwdb[5] = u32_get_bits(tmp, MASKBYTE2);

		rtw89_phy_write32_mask(rtwdev, edcca_p_regs->rpt_sel,
				       edcca_p_regs->rpt_sel_mask, 3);
		tmp = rtw89_phy_read32(rtwdev, edcca_p_regs->rpt_a);
		pwdb[6] = u32_get_bits(tmp, MASKBYTE3);
		pwdb[7] = u32_get_bits(tmp, MASKBYTE2);
	}

	rtw89_debug(rtwdev, RTW89_DBG_EDCCA,
		    "[EDCCA]: edcca_bitmap = %04x\n", per20_bitmap);

	rtw89_debug(rtwdev, RTW89_DBG_EDCCA,
		    "[EDCCA]: pwdb per20{0,1,2,3,4,5,6,7} = {%d,%d,%d,%d,%d,%d,%d,%d}(dBm)\n",
		    pwdb[0], pwdb[1], pwdb[2], pwdb[3], pwdb[4], pwdb[5],
		    pwdb[6], pwdb[7]);

	rtw89_debug(rtwdev, RTW89_DBG_EDCCA,
		    "[EDCCA]: path=%d, flag {FB,p20,s20,s40,s80} = {%d,%d,%d,%d,%d}\n",
		    path, flag_fb, flag_p20, flag_s20, flag_s40, flag_s80);

	rtw89_debug(rtwdev, RTW89_DBG_EDCCA,
		    "[EDCCA]: pwdb {FB,p20,s20,s40,s80} = {%d,%d,%d,%d,%d}(dBm)\n",
		    pwdb_fb, pwdb_p20, pwdb_s20, pwdb_s40, pwdb_s80);
}

static u8 rtw89_phy_edcca_get_thre_by_rssi(struct rtw89_dev *rtwdev,
					   struct rtw89_bb_ctx *bb)
{
	struct rtw89_phy_ch_info *ch_info = &bb->ch_info;
	bool is_linked = rtwdev->total_sta_assoc > 0;
	u8 rssi_min = ch_info->rssi_min >> 1;
	u8 edcca_thre;

	if (!is_linked) {
		edcca_thre = EDCCA_MAX;
	} else {
		edcca_thre = rssi_min - RSSI_UNIT_CONVER + EDCCA_UNIT_CONVER -
			     EDCCA_TH_REF;
		edcca_thre = max_t(u8, edcca_thre, EDCCA_TH_L2H_LB);
	}

	return edcca_thre;
}

void rtw89_phy_edcca_thre_calc(struct rtw89_dev *rtwdev, struct rtw89_bb_ctx *bb)
{
	const struct rtw89_edcca_regs *edcca_regs = rtwdev->chip->edcca_regs;
	struct rtw89_edcca_bak *edcca_bak = &bb->edcca_bak;
	u8 th;

	th = rtw89_phy_edcca_get_thre_by_rssi(rtwdev, bb);
	if (th == edcca_bak->th_old)
		return;

	edcca_bak->th_old = th;

	rtw89_debug(rtwdev, RTW89_DBG_EDCCA,
		    "[EDCCA]: Normal Mode, EDCCA_th = %d\n", th);

	rtw89_phy_write32_idx(rtwdev, edcca_regs->edcca_level,
			      edcca_regs->edcca_mask, th, bb->phy_idx);
	rtw89_phy_write32_idx(rtwdev, edcca_regs->edcca_level,
			      edcca_regs->edcca_p_mask, th, bb->phy_idx);
	rtw89_phy_write32_idx(rtwdev, edcca_regs->ppdu_level,
			      edcca_regs->ppdu_mask, th, bb->phy_idx);
}

static
void __rtw89_phy_edcca_track(struct rtw89_dev *rtwdev, struct rtw89_bb_ctx *bb)
{
	rtw89_debug(rtwdev, RTW89_DBG_EDCCA, "BB-%d edcca track\n", bb->phy_idx);

	rtw89_phy_edcca_thre_calc(rtwdev, bb);
	rtw89_phy_edcca_log(rtwdev, bb);
}

void rtw89_phy_edcca_track(struct rtw89_dev *rtwdev)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	struct rtw89_bb_ctx *bb;

	if (hal->disabled_dm_bitmap & BIT(RTW89_DM_DYNAMIC_EDCCA))
		return;

	rtw89_for_each_active_bb(rtwdev, bb)
		__rtw89_phy_edcca_track(rtwdev, bb);
}

enum rtw89_rf_path_bit rtw89_phy_get_kpath(struct rtw89_dev *rtwdev,
					   enum rtw89_phy_idx phy_idx)
{
	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[RFK] kpath dbcc_en: 0x%x, mode=0x%x, PHY%d\n",
		    rtwdev->dbcc_en, rtwdev->mlo_dbcc_mode, phy_idx);

	switch (rtwdev->mlo_dbcc_mode) {
	case MLO_1_PLUS_1_1RF:
		if (phy_idx == RTW89_PHY_0)
			return RF_A;
		else
			return RF_B;
	case MLO_1_PLUS_1_2RF:
		if (phy_idx == RTW89_PHY_0)
			return RF_A;
		else
			return RF_D;
	case MLO_0_PLUS_2_1RF:
	case MLO_2_PLUS_0_1RF:
		/* for both PHY 0/1 */
		return RF_AB;
	case MLO_0_PLUS_2_2RF:
	case MLO_2_PLUS_0_2RF:
	case MLO_2_PLUS_2_2RF:
	default:
		if (phy_idx == RTW89_PHY_0)
			return RF_AB;
		else
			return RF_CD;
	}
}
EXPORT_SYMBOL(rtw89_phy_get_kpath);

enum rtw89_rf_path rtw89_phy_get_syn_sel(struct rtw89_dev *rtwdev,
					 enum rtw89_phy_idx phy_idx)
{
	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[RFK] kpath dbcc_en: 0x%x, mode=0x%x, PHY%d\n",
		    rtwdev->dbcc_en, rtwdev->mlo_dbcc_mode, phy_idx);

	switch (rtwdev->mlo_dbcc_mode) {
	case MLO_1_PLUS_1_1RF:
		if (phy_idx == RTW89_PHY_0)
			return RF_PATH_A;
		else
			return RF_PATH_B;
	case MLO_1_PLUS_1_2RF:
		if (phy_idx == RTW89_PHY_0)
			return RF_PATH_A;
		else
			return RF_PATH_D;
	case MLO_0_PLUS_2_1RF:
	case MLO_2_PLUS_0_1RF:
		if (phy_idx == RTW89_PHY_0)
			return RF_PATH_A;
		else
			return RF_PATH_B;
	case MLO_0_PLUS_2_2RF:
	case MLO_2_PLUS_0_2RF:
	case MLO_2_PLUS_2_2RF:
	default:
		if (phy_idx == RTW89_PHY_0)
			return RF_PATH_A;
		else
			return RF_PATH_C;
	}
}
EXPORT_SYMBOL(rtw89_phy_get_syn_sel);

static const struct rtw89_ccx_regs rtw89_ccx_regs_ax = {
	.setting_addr = R_CCX,
	.edcca_opt_mask = B_CCX_EDCCA_OPT_MSK,
	.measurement_trig_mask = B_MEASUREMENT_TRIG_MSK,
	.trig_opt_mask = B_CCX_TRIG_OPT_MSK,
	.en_mask = B_CCX_EN_MSK,
	.ifs_cnt_addr = R_IFS_COUNTER,
	.ifs_clm_period_mask = B_IFS_CLM_PERIOD_MSK,
	.ifs_clm_cnt_unit_mask = B_IFS_CLM_COUNTER_UNIT_MSK,
	.ifs_clm_cnt_clear_mask = B_IFS_COUNTER_CLR_MSK,
	.ifs_collect_en_mask = B_IFS_COLLECT_EN,
	.ifs_t1_addr = R_IFS_T1,
	.ifs_t1_th_h_mask = B_IFS_T1_TH_HIGH_MSK,
	.ifs_t1_en_mask = B_IFS_T1_EN_MSK,
	.ifs_t1_th_l_mask = B_IFS_T1_TH_LOW_MSK,
	.ifs_t2_addr = R_IFS_T2,
	.ifs_t2_th_h_mask = B_IFS_T2_TH_HIGH_MSK,
	.ifs_t2_en_mask = B_IFS_T2_EN_MSK,
	.ifs_t2_th_l_mask = B_IFS_T2_TH_LOW_MSK,
	.ifs_t3_addr = R_IFS_T3,
	.ifs_t3_th_h_mask = B_IFS_T3_TH_HIGH_MSK,
	.ifs_t3_en_mask = B_IFS_T3_EN_MSK,
	.ifs_t3_th_l_mask = B_IFS_T3_TH_LOW_MSK,
	.ifs_t4_addr = R_IFS_T4,
	.ifs_t4_th_h_mask = B_IFS_T4_TH_HIGH_MSK,
	.ifs_t4_en_mask = B_IFS_T4_EN_MSK,
	.ifs_t4_th_l_mask = B_IFS_T4_TH_LOW_MSK,
	.ifs_clm_tx_cnt_addr = R_IFS_CLM_TX_CNT,
	.ifs_clm_edcca_excl_cca_fa_mask = B_IFS_CLM_EDCCA_EXCLUDE_CCA_FA_MSK,
	.ifs_clm_tx_cnt_msk = B_IFS_CLM_TX_CNT_MSK,
	.ifs_clm_cca_addr = R_IFS_CLM_CCA,
	.ifs_clm_ofdmcca_excl_fa_mask = B_IFS_CLM_OFDMCCA_EXCLUDE_FA_MSK,
	.ifs_clm_cckcca_excl_fa_mask = B_IFS_CLM_CCKCCA_EXCLUDE_FA_MSK,
	.ifs_clm_fa_addr = R_IFS_CLM_FA,
	.ifs_clm_ofdm_fa_mask = B_IFS_CLM_OFDM_FA_MSK,
	.ifs_clm_cck_fa_mask = B_IFS_CLM_CCK_FA_MSK,
	.ifs_his_addr = R_IFS_HIS,
	.ifs_t4_his_mask = B_IFS_T4_HIS_MSK,
	.ifs_t3_his_mask = B_IFS_T3_HIS_MSK,
	.ifs_t2_his_mask = B_IFS_T2_HIS_MSK,
	.ifs_t1_his_mask = B_IFS_T1_HIS_MSK,
	.ifs_avg_l_addr = R_IFS_AVG_L,
	.ifs_t2_avg_mask = B_IFS_T2_AVG_MSK,
	.ifs_t1_avg_mask = B_IFS_T1_AVG_MSK,
	.ifs_avg_h_addr = R_IFS_AVG_H,
	.ifs_t4_avg_mask = B_IFS_T4_AVG_MSK,
	.ifs_t3_avg_mask = B_IFS_T3_AVG_MSK,
	.ifs_cca_l_addr = R_IFS_CCA_L,
	.ifs_t2_cca_mask = B_IFS_T2_CCA_MSK,
	.ifs_t1_cca_mask = B_IFS_T1_CCA_MSK,
	.ifs_cca_h_addr = R_IFS_CCA_H,
	.ifs_t4_cca_mask = B_IFS_T4_CCA_MSK,
	.ifs_t3_cca_mask = B_IFS_T3_CCA_MSK,
	.ifs_total_addr = R_IFSCNT,
	.ifs_cnt_done_mask = B_IFSCNT_DONE_MSK,
	.ifs_total_mask = B_IFSCNT_TOTAL_CNT_MSK,
};

static const struct rtw89_physts_regs rtw89_physts_regs_ax = {
	.setting_addr = R_PLCP_HISTOGRAM,
	.dis_trigger_fail_mask = B_STS_DIS_TRIG_BY_FAIL,
	.dis_trigger_brk_mask = B_STS_DIS_TRIG_BY_BRK,
};

static const struct rtw89_cfo_regs rtw89_cfo_regs_ax = {
	.comp = R_DCFO_WEIGHT,
	.weighting_mask = B_DCFO_WEIGHT_MSK,
	.comp_seg0 = R_DCFO_OPT,
	.valid_0_mask = B_DCFO_OPT_EN,
};

const struct rtw89_phy_gen_def rtw89_phy_gen_ax = {
	.cr_base = 0x10000,
	.ccx = &rtw89_ccx_regs_ax,
	.physts = &rtw89_physts_regs_ax,
	.cfo = &rtw89_cfo_regs_ax,
	.phy0_phy1_offset = rtw89_phy0_phy1_offset_ax,
	.config_bb_gain = rtw89_phy_config_bb_gain_ax,
	.preinit_rf_nctl = rtw89_phy_preinit_rf_nctl_ax,
	.bb_wrap_init = NULL,
	.ch_info_init = NULL,

	.set_txpwr_byrate = rtw89_phy_set_txpwr_byrate_ax,
	.set_txpwr_offset = rtw89_phy_set_txpwr_offset_ax,
	.set_txpwr_limit = rtw89_phy_set_txpwr_limit_ax,
	.set_txpwr_limit_ru = rtw89_phy_set_txpwr_limit_ru_ax,
};
EXPORT_SYMBOL(rtw89_phy_gen_ax);
