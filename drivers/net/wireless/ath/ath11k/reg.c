// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <linux/rtnetlink.h>

#include "core.h"
#include "debug.h"

/* World regdom to be used in case default regd from fw is unavailable */
#define ATH11K_2GHZ_CH01_11      REG_RULE(2412 - 10, 2462 + 10, 40, 0, 20, 0)
#define ATH11K_5GHZ_5150_5350    REG_RULE(5150 - 10, 5350 + 10, 80, 0, 30,\
					  NL80211_RRF_NO_IR)
#define ATH11K_5GHZ_5725_5850    REG_RULE(5725 - 10, 5850 + 10, 80, 0, 30,\
					  NL80211_RRF_NO_IR)

#define ETSI_WEATHER_RADAR_BAND_LOW		5590
#define ETSI_WEATHER_RADAR_BAND_HIGH		5650
#define ETSI_WEATHER_RADAR_BAND_CAC_TIMEOUT	600000

static const struct ieee80211_regdomain ath11k_world_regd = {
	.n_reg_rules = 3,
	.alpha2 =  "00",
	.reg_rules = {
		ATH11K_2GHZ_CH01_11,
		ATH11K_5GHZ_5150_5350,
		ATH11K_5GHZ_5725_5850,
	}
};

static bool ath11k_regdom_changes(struct ath11k *ar, char *alpha2)
{
	const struct ieee80211_regdomain *regd;

	regd = rcu_dereference_rtnl(ar->hw->wiphy->regd);
	/* This can happen during wiphy registration where the previous
	 * user request is received before we update the regd received
	 * from firmware.
	 */
	if (!regd)
		return true;

	return memcmp(regd->alpha2, alpha2, 2) != 0;
}

static void
ath11k_reg_notifier(struct wiphy *wiphy, struct regulatory_request *request)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct wmi_init_country_params init_country_param;
	struct ath11k *ar = hw->priv;
	int ret;

	ath11k_dbg(ar->ab, ATH11K_DBG_REG,
		   "Regulatory Notification received for %s\n", wiphy_name(wiphy));

	/* Currently supporting only General User Hints. Cell base user
	 * hints to be handled later.
	 * Hints from other sources like Core, Beacons are not expected for
	 * self managed wiphy's
	 */
	if (!(request->initiator == NL80211_REGDOM_SET_BY_USER &&
	      request->user_reg_hint_type == NL80211_USER_REG_HINT_USER)) {
		ath11k_warn(ar->ab, "Unexpected Regulatory event for this wiphy\n");
		return;
	}

	if (!IS_ENABLED(CONFIG_ATH_REG_DYNAMIC_USER_REG_HINTS)) {
		ath11k_dbg(ar->ab, ATH11K_DBG_REG,
			   "Country Setting is not allowed\n");
		return;
	}

	if (!ath11k_regdom_changes(ar, request->alpha2)) {
		ath11k_dbg(ar->ab, ATH11K_DBG_REG, "Country is already set\n");
		return;
	}

	/* Set the country code to the firmware and will receive
	 * the WMI_REG_CHAN_LIST_CC EVENT for updating the
	 * reg info
	 */
	if (ar->ab->hw_params.current_cc_support) {
		memcpy(&ar->alpha2, request->alpha2, 2);
		ret = ath11k_reg_set_cc(ar);
		if (ret)
			ath11k_warn(ar->ab,
				    "failed set current country code: %d\n", ret);
	} else {
		init_country_param.flags = ALPHA_IS_SET;
		memcpy(&init_country_param.cc_info.alpha2, request->alpha2, 2);
		init_country_param.cc_info.alpha2[2] = 0;

		ret = ath11k_wmi_send_init_country_cmd(ar, init_country_param);
		if (ret)
			ath11k_warn(ar->ab,
				    "INIT Country code set to fw failed : %d\n", ret);
	}

	ath11k_mac_11d_scan_stop(ar);
	ar->regdom_set_by_user = true;
}

int ath11k_reg_update_chan_list(struct ath11k *ar, bool wait)
{
	struct ieee80211_supported_band **bands;
	struct scan_chan_list_params *params;
	struct ieee80211_channel *channel;
	struct ieee80211_hw *hw = ar->hw;
	struct channel_param *ch;
	enum nl80211_band band;
	int num_channels = 0;
	int i, ret, left;

	if (wait && ar->state_11d != ATH11K_11D_IDLE) {
		left = wait_for_completion_timeout(&ar->completed_11d_scan,
						   ATH11K_SCAN_TIMEOUT_HZ);
		if (!left) {
			ath11k_dbg(ar->ab, ATH11K_DBG_REG,
				   "failed to receive 11d scan complete: timed out\n");
			ar->state_11d = ATH11K_11D_IDLE;
		}
		ath11k_dbg(ar->ab, ATH11K_DBG_REG,
			   "11d scan wait left time %d\n", left);
	}

	if (wait &&
	    (ar->scan.state == ATH11K_SCAN_STARTING ||
	    ar->scan.state == ATH11K_SCAN_RUNNING)) {
		left = wait_for_completion_timeout(&ar->scan.completed,
						   ATH11K_SCAN_TIMEOUT_HZ);
		if (!left)
			ath11k_dbg(ar->ab, ATH11K_DBG_REG,
				   "failed to receive hw scan complete: timed out\n");

		ath11k_dbg(ar->ab, ATH11K_DBG_REG,
			   "hw scan wait left time %d\n", left);
	}

	if (ar->state == ATH11K_STATE_RESTARTING)
		return 0;

	bands = hw->wiphy->bands;
	for (band = 0; band < NUM_NL80211_BANDS; band++) {
		if (!bands[band])
			continue;

		for (i = 0; i < bands[band]->n_channels; i++) {
			if (bands[band]->channels[i].flags &
			    IEEE80211_CHAN_DISABLED)
				continue;

			num_channels++;
		}
	}

	if (WARN_ON(!num_channels))
		return -EINVAL;

	params = kzalloc(struct_size(params, ch_param, num_channels),
			 GFP_KERNEL);
	if (!params)
		return -ENOMEM;

	params->pdev_id = ar->pdev->pdev_id;
	params->nallchans = num_channels;

	ch = params->ch_param;

	for (band = 0; band < NUM_NL80211_BANDS; band++) {
		if (!bands[band])
			continue;

		for (i = 0; i < bands[band]->n_channels; i++) {
			channel = &bands[band]->channels[i];

			if (channel->flags & IEEE80211_CHAN_DISABLED)
				continue;

			/* TODO: Set to true/false based on some condition? */
			ch->allow_ht = true;
			ch->allow_vht = true;
			ch->allow_he = true;

			ch->dfs_set =
				!!(channel->flags & IEEE80211_CHAN_RADAR);
			ch->is_chan_passive = !!(channel->flags &
						IEEE80211_CHAN_NO_IR);
			ch->is_chan_passive |= ch->dfs_set;
			ch->mhz = channel->center_freq;
			ch->cfreq1 = channel->center_freq;
			ch->minpower = 0;
			ch->maxpower = channel->max_power * 2;
			ch->maxregpower = channel->max_reg_power * 2;
			ch->antennamax = channel->max_antenna_gain * 2;

			/* TODO: Use appropriate phymodes */
			if (channel->band == NL80211_BAND_2GHZ)
				ch->phy_mode = MODE_11G;
			else
				ch->phy_mode = MODE_11A;

			if (channel->band == NL80211_BAND_6GHZ &&
			    cfg80211_channel_is_psc(channel))
				ch->psc_channel = true;

			ath11k_dbg(ar->ab, ATH11K_DBG_WMI,
				   "mac channel [%d/%d] freq %d maxpower %d regpower %d antenna %d mode %d\n",
				   i, params->nallchans,
				   ch->mhz, ch->maxpower, ch->maxregpower,
				   ch->antennamax, ch->phy_mode);

			ch++;
			/* TODO: use quarrter/half rate, cfreq12, dfs_cfreq2
			 * set_agile, reg_class_idx
			 */
		}
	}

	ret = ath11k_wmi_send_scan_chan_list_cmd(ar, params);
	kfree(params);

	return ret;
}

static void ath11k_copy_regd(struct ieee80211_regdomain *regd_orig,
			     struct ieee80211_regdomain *regd_copy)
{
	u8 i;

	/* The caller should have checked error conditions */
	memcpy(regd_copy, regd_orig, sizeof(*regd_orig));

	for (i = 0; i < regd_orig->n_reg_rules; i++)
		memcpy(&regd_copy->reg_rules[i], &regd_orig->reg_rules[i],
		       sizeof(struct ieee80211_reg_rule));
}

int ath11k_regd_update(struct ath11k *ar)
{
	struct ieee80211_regdomain *regd, *regd_copy = NULL;
	int ret, regd_len, pdev_id;
	struct ath11k_base *ab;

	ab = ar->ab;
	pdev_id = ar->pdev_idx;

	spin_lock_bh(&ab->base_lock);

	/* Prefer the latest regd update over default if it's available */
	if (ab->new_regd[pdev_id]) {
		regd = ab->new_regd[pdev_id];
	} else {
		/* Apply the regd received during init through
		 * WMI_REG_CHAN_LIST_CC event. In case of failure to
		 * receive the regd, initialize with a default world
		 * regulatory.
		 */
		if (ab->default_regd[pdev_id]) {
			regd = ab->default_regd[pdev_id];
		} else {
			ath11k_warn(ab,
				    "failed to receive default regd during init\n");
			regd = (struct ieee80211_regdomain *)&ath11k_world_regd;
		}
	}

	if (!regd) {
		ret = -EINVAL;
		spin_unlock_bh(&ab->base_lock);
		goto err;
	}

	regd_len = sizeof(*regd) + (regd->n_reg_rules *
		sizeof(struct ieee80211_reg_rule));

	regd_copy = kzalloc(regd_len, GFP_ATOMIC);
	if (regd_copy)
		ath11k_copy_regd(regd, regd_copy);

	spin_unlock_bh(&ab->base_lock);

	if (!regd_copy) {
		ret = -ENOMEM;
		goto err;
	}

	ret = regulatory_set_wiphy_regd(ar->hw->wiphy, regd_copy);

	kfree(regd_copy);

	if (ret)
		goto err;

	if (ar->state == ATH11K_STATE_ON) {
		ret = ath11k_reg_update_chan_list(ar, true);
		if (ret)
			goto err;
	}

	return 0;
err:
	ath11k_warn(ab, "failed to perform regd update : %d\n", ret);
	return ret;
}

static enum nl80211_dfs_regions
ath11k_map_fw_dfs_region(enum ath11k_dfs_region dfs_region)
{
	switch (dfs_region) {
	case ATH11K_DFS_REG_FCC:
	case ATH11K_DFS_REG_CN:
		return NL80211_DFS_FCC;
	case ATH11K_DFS_REG_ETSI:
	case ATH11K_DFS_REG_KR:
		return NL80211_DFS_ETSI;
	case ATH11K_DFS_REG_MKK:
	case ATH11K_DFS_REG_MKK_N:
		return NL80211_DFS_JP;
	default:
		return NL80211_DFS_UNSET;
	}
}

static u32 ath11k_map_fw_reg_flags(u16 reg_flags)
{
	u32 flags = 0;

	if (reg_flags & REGULATORY_CHAN_NO_IR)
		flags = NL80211_RRF_NO_IR;

	if (reg_flags & REGULATORY_CHAN_RADAR)
		flags |= NL80211_RRF_DFS;

	if (reg_flags & REGULATORY_CHAN_NO_OFDM)
		flags |= NL80211_RRF_NO_OFDM;

	if (reg_flags & REGULATORY_CHAN_INDOOR_ONLY)
		flags |= NL80211_RRF_NO_OUTDOOR;

	if (reg_flags & REGULATORY_CHAN_NO_HT40)
		flags |= NL80211_RRF_NO_HT40;

	if (reg_flags & REGULATORY_CHAN_NO_80MHZ)
		flags |= NL80211_RRF_NO_80MHZ;

	if (reg_flags & REGULATORY_CHAN_NO_160MHZ)
		flags |= NL80211_RRF_NO_160MHZ;

	return flags;
}

static u32 ath11k_map_fw_phy_flags(u32 phy_flags)
{
	u32 flags = 0;

	if (phy_flags & ATH11K_REG_PHY_BITMAP_NO11AX)
		flags |= NL80211_RRF_NO_HE;

	return flags;
}

static bool
ath11k_reg_can_intersect(struct ieee80211_reg_rule *rule1,
			 struct ieee80211_reg_rule *rule2)
{
	u32 start_freq1, end_freq1;
	u32 start_freq2, end_freq2;

	start_freq1 = rule1->freq_range.start_freq_khz;
	start_freq2 = rule2->freq_range.start_freq_khz;

	end_freq1 = rule1->freq_range.end_freq_khz;
	end_freq2 = rule2->freq_range.end_freq_khz;

	if ((start_freq1 >= start_freq2 &&
	     start_freq1 < end_freq2) ||
	    (start_freq2 > start_freq1 &&
	     start_freq2 < end_freq1))
		return true;

	/* TODO: Should we restrict intersection feasibility
	 *  based on min bandwidth of the intersected region also,
	 *  say the intersected rule should have a  min bandwidth
	 * of 20MHz?
	 */

	return false;
}

static void ath11k_reg_intersect_rules(struct ieee80211_reg_rule *rule1,
				       struct ieee80211_reg_rule *rule2,
				       struct ieee80211_reg_rule *new_rule)
{
	u32 start_freq1, end_freq1;
	u32 start_freq2, end_freq2;
	u32 freq_diff, max_bw;

	start_freq1 = rule1->freq_range.start_freq_khz;
	start_freq2 = rule2->freq_range.start_freq_khz;

	end_freq1 = rule1->freq_range.end_freq_khz;
	end_freq2 = rule2->freq_range.end_freq_khz;

	new_rule->freq_range.start_freq_khz = max_t(u32, start_freq1,
						    start_freq2);
	new_rule->freq_range.end_freq_khz = min_t(u32, end_freq1, end_freq2);

	freq_diff = new_rule->freq_range.end_freq_khz -
			new_rule->freq_range.start_freq_khz;
	max_bw = min_t(u32, rule1->freq_range.max_bandwidth_khz,
		       rule2->freq_range.max_bandwidth_khz);
	new_rule->freq_range.max_bandwidth_khz = min_t(u32, max_bw, freq_diff);

	new_rule->power_rule.max_antenna_gain =
		min_t(u32, rule1->power_rule.max_antenna_gain,
		      rule2->power_rule.max_antenna_gain);

	new_rule->power_rule.max_eirp = min_t(u32, rule1->power_rule.max_eirp,
					      rule2->power_rule.max_eirp);

	/* Use the flags of both the rules */
	new_rule->flags = rule1->flags | rule2->flags;

	if ((rule1->flags & NL80211_RRF_PSD) && (rule2->flags & NL80211_RRF_PSD))
		new_rule->psd = min_t(s8, rule1->psd, rule2->psd);
	else
		new_rule->flags &= ~NL80211_RRF_PSD;

	/* To be safe, lts use the max cac timeout of both rules */
	new_rule->dfs_cac_ms = max_t(u32, rule1->dfs_cac_ms,
				     rule2->dfs_cac_ms);
}

static struct ieee80211_regdomain *
ath11k_regd_intersect(struct ieee80211_regdomain *default_regd,
		      struct ieee80211_regdomain *curr_regd)
{
	u8 num_old_regd_rules, num_curr_regd_rules, num_new_regd_rules;
	struct ieee80211_reg_rule *old_rule, *curr_rule, *new_rule;
	struct ieee80211_regdomain *new_regd = NULL;
	u8 i, j, k;

	num_old_regd_rules = default_regd->n_reg_rules;
	num_curr_regd_rules = curr_regd->n_reg_rules;
	num_new_regd_rules = 0;

	/* Find the number of intersecting rules to allocate new regd memory */
	for (i = 0; i < num_old_regd_rules; i++) {
		old_rule = default_regd->reg_rules + i;
		for (j = 0; j < num_curr_regd_rules; j++) {
			curr_rule = curr_regd->reg_rules + j;

			if (ath11k_reg_can_intersect(old_rule, curr_rule))
				num_new_regd_rules++;
		}
	}

	if (!num_new_regd_rules)
		return NULL;

	new_regd = kzalloc(sizeof(*new_regd) + (num_new_regd_rules *
			sizeof(struct ieee80211_reg_rule)),
			GFP_ATOMIC);

	if (!new_regd)
		return NULL;

	/* We set the new country and dfs region directly and only trim
	 * the freq, power, antenna gain by intersecting with the
	 * default regdomain. Also MAX of the dfs cac timeout is selected.
	 */
	new_regd->n_reg_rules = num_new_regd_rules;
	memcpy(new_regd->alpha2, curr_regd->alpha2, sizeof(new_regd->alpha2));
	new_regd->dfs_region = curr_regd->dfs_region;
	new_rule = new_regd->reg_rules;

	for (i = 0, k = 0; i < num_old_regd_rules; i++) {
		old_rule = default_regd->reg_rules + i;
		for (j = 0; j < num_curr_regd_rules; j++) {
			curr_rule = curr_regd->reg_rules + j;

			if (ath11k_reg_can_intersect(old_rule, curr_rule))
				ath11k_reg_intersect_rules(old_rule, curr_rule,
							   (new_rule + k++));
		}
	}
	return new_regd;
}

static const char *
ath11k_reg_get_regdom_str(enum nl80211_dfs_regions dfs_region)
{
	switch (dfs_region) {
	case NL80211_DFS_FCC:
		return "FCC";
	case NL80211_DFS_ETSI:
		return "ETSI";
	case NL80211_DFS_JP:
		return "JP";
	default:
		return "UNSET";
	}
}

static u16
ath11k_reg_adjust_bw(u16 start_freq, u16 end_freq, u16 max_bw)
{
	u16 bw;

	if (end_freq <= start_freq)
		return 0;

	bw = end_freq - start_freq;
	bw = min_t(u16, bw, max_bw);

	if (bw >= 80 && bw < 160)
		bw = 80;
	else if (bw >= 40 && bw < 80)
		bw = 40;
	else if (bw >= 20 && bw < 40)
		bw = 20;
	else
		bw = 0;

	return bw;
}

static void
ath11k_reg_update_rule(struct ieee80211_reg_rule *reg_rule, u32 start_freq,
		       u32 end_freq, u32 bw, u32 ant_gain, u32 reg_pwr,
		       s8 psd, u32 reg_flags)
{
	reg_rule->freq_range.start_freq_khz = MHZ_TO_KHZ(start_freq);
	reg_rule->freq_range.end_freq_khz = MHZ_TO_KHZ(end_freq);
	reg_rule->freq_range.max_bandwidth_khz = MHZ_TO_KHZ(bw);
	reg_rule->power_rule.max_antenna_gain = DBI_TO_MBI(ant_gain);
	reg_rule->power_rule.max_eirp = DBM_TO_MBM(reg_pwr);
	reg_rule->psd = psd;
	reg_rule->flags = reg_flags;
}

static void
ath11k_reg_update_weather_radar_band(struct ath11k_base *ab,
				     struct ieee80211_regdomain *regd,
				     struct cur_reg_rule *reg_rule,
				     u8 *rule_idx, u32 flags, u16 max_bw)
{
	u32 start_freq;
	u32 end_freq;
	u16 bw;
	u8 i;

	i = *rule_idx;

	/* there might be situations when even the input rule must be dropped */
	i--;

	/* frequencies below weather radar */
	bw = ath11k_reg_adjust_bw(reg_rule->start_freq,
				  ETSI_WEATHER_RADAR_BAND_LOW, max_bw);
	if (bw > 0) {
		i++;

		ath11k_reg_update_rule(regd->reg_rules + i,
				       reg_rule->start_freq,
				       ETSI_WEATHER_RADAR_BAND_LOW, bw,
				       reg_rule->ant_gain, reg_rule->reg_power,
				       reg_rule->psd_eirp, flags);

		ath11k_dbg(ab, ATH11K_DBG_REG,
			   "\t%d. (%d - %d @ %d) (%d, %d) (%d ms) (FLAGS %d)\n",
			   i + 1, reg_rule->start_freq,
			   ETSI_WEATHER_RADAR_BAND_LOW, bw, reg_rule->ant_gain,
			   reg_rule->reg_power, regd->reg_rules[i].dfs_cac_ms,
			   flags);
	}

	/* weather radar frequencies */
	start_freq = max_t(u32, reg_rule->start_freq,
			   ETSI_WEATHER_RADAR_BAND_LOW);
	end_freq = min_t(u32, reg_rule->end_freq, ETSI_WEATHER_RADAR_BAND_HIGH);

	bw = ath11k_reg_adjust_bw(start_freq, end_freq, max_bw);
	if (bw > 0) {
		i++;

		ath11k_reg_update_rule(regd->reg_rules + i, start_freq,
				       end_freq, bw, reg_rule->ant_gain,
				       reg_rule->reg_power, reg_rule->psd_eirp, flags);

		regd->reg_rules[i].dfs_cac_ms = ETSI_WEATHER_RADAR_BAND_CAC_TIMEOUT;

		ath11k_dbg(ab, ATH11K_DBG_REG,
			   "\t%d. (%d - %d @ %d) (%d, %d) (%d ms) (FLAGS %d)\n",
			   i + 1, start_freq, end_freq, bw,
			   reg_rule->ant_gain, reg_rule->reg_power,
			   regd->reg_rules[i].dfs_cac_ms, flags);
	}

	/* frequencies above weather radar */
	bw = ath11k_reg_adjust_bw(ETSI_WEATHER_RADAR_BAND_HIGH,
				  reg_rule->end_freq, max_bw);
	if (bw > 0) {
		i++;

		ath11k_reg_update_rule(regd->reg_rules + i,
				       ETSI_WEATHER_RADAR_BAND_HIGH,
				       reg_rule->end_freq, bw,
				       reg_rule->ant_gain, reg_rule->reg_power,
				       reg_rule->psd_eirp, flags);

		ath11k_dbg(ab, ATH11K_DBG_REG,
			   "\t%d. (%d - %d @ %d) (%d, %d) (%d ms) (FLAGS %d)\n",
			   i + 1, ETSI_WEATHER_RADAR_BAND_HIGH,
			   reg_rule->end_freq, bw, reg_rule->ant_gain,
			   reg_rule->reg_power, regd->reg_rules[i].dfs_cac_ms,
			   flags);
	}

	*rule_idx = i;
}

enum wmi_reg_6ghz_ap_type
ath11k_reg_ap_pwr_convert(enum ieee80211_ap_reg_power power_type)
{
	switch (power_type) {
	case IEEE80211_REG_LPI_AP:
		return WMI_REG_INDOOR_AP;
	case IEEE80211_REG_SP_AP:
		return WMI_REG_STANDARD_POWER_AP;
	case IEEE80211_REG_VLP_AP:
		return WMI_REG_VERY_LOW_POWER_AP;
	default:
		return WMI_REG_MAX_AP_TYPE;
	}
}

struct ieee80211_regdomain *
ath11k_reg_build_regd(struct ath11k_base *ab,
		      struct cur_regulatory_info *reg_info, bool intersect,
		      enum wmi_vdev_type vdev_type,
		      enum ieee80211_ap_reg_power power_type)
{
	struct ieee80211_regdomain *tmp_regd, *default_regd, *new_regd = NULL;
	struct cur_reg_rule *reg_rule, *reg_rule_6ghz;
	u8 i = 0, j = 0, k = 0;
	u8 num_rules;
	u16 max_bw;
	u32 flags, reg_6ghz_number, max_bw_6ghz;
	char alpha2[3];

	num_rules = reg_info->num_5ghz_reg_rules + reg_info->num_2ghz_reg_rules;

	if (reg_info->is_ext_reg_event) {
		if (vdev_type == WMI_VDEV_TYPE_STA) {
			enum wmi_reg_6ghz_ap_type ap_type;

			ap_type = ath11k_reg_ap_pwr_convert(power_type);

			if (ap_type == WMI_REG_MAX_AP_TYPE)
				ap_type = WMI_REG_INDOOR_AP;

			reg_6ghz_number = reg_info->num_6ghz_rules_client
					[ap_type][WMI_REG_DEFAULT_CLIENT];

			if (reg_6ghz_number == 0) {
				ap_type = WMI_REG_INDOOR_AP;
				reg_6ghz_number = reg_info->num_6ghz_rules_client
						[ap_type][WMI_REG_DEFAULT_CLIENT];
			}

			reg_rule_6ghz = reg_info->reg_rules_6ghz_client_ptr
					[ap_type][WMI_REG_DEFAULT_CLIENT];
			max_bw_6ghz = reg_info->max_bw_6ghz_client
					[ap_type][WMI_REG_DEFAULT_CLIENT];
		} else {
			reg_6ghz_number = reg_info->num_6ghz_rules_ap[WMI_REG_INDOOR_AP];
			reg_rule_6ghz =
				reg_info->reg_rules_6ghz_ap_ptr[WMI_REG_INDOOR_AP];
			max_bw_6ghz = reg_info->max_bw_6ghz_ap[WMI_REG_INDOOR_AP];
		}

		num_rules += reg_6ghz_number;
	}

	if (!num_rules)
		goto ret;

	/* Add max additional rules to accommodate weather radar band */
	if (reg_info->dfs_region == ATH11K_DFS_REG_ETSI)
		num_rules += 2;

	tmp_regd =  kzalloc(sizeof(*tmp_regd) +
			(num_rules * sizeof(struct ieee80211_reg_rule)),
			GFP_ATOMIC);
	if (!tmp_regd)
		goto ret;

	memcpy(tmp_regd->alpha2, reg_info->alpha2, REG_ALPHA2_LEN + 1);
	memcpy(alpha2, reg_info->alpha2, REG_ALPHA2_LEN + 1);
	alpha2[2] = '\0';
	tmp_regd->dfs_region = ath11k_map_fw_dfs_region(reg_info->dfs_region);

	ath11k_dbg(ab, ATH11K_DBG_REG,
		   "Country %s, CFG Regdomain %s FW Regdomain %d, num_reg_rules %d\n",
		   alpha2, ath11k_reg_get_regdom_str(tmp_regd->dfs_region),
		   reg_info->dfs_region, num_rules);
	/* Update reg_rules[] below. Firmware is expected to
	 * send these rules in order(2 GHz rules first and then 5 GHz)
	 */
	for (; i < num_rules; i++) {
		if (reg_info->num_2ghz_reg_rules &&
		    (i < reg_info->num_2ghz_reg_rules)) {
			reg_rule = reg_info->reg_rules_2ghz_ptr + i;
			max_bw = min_t(u16, reg_rule->max_bw,
				       reg_info->max_bw_2ghz);
			flags = 0;
		} else if (reg_info->num_5ghz_reg_rules &&
			   (j < reg_info->num_5ghz_reg_rules)) {
			reg_rule = reg_info->reg_rules_5ghz_ptr + j++;
			max_bw = min_t(u16, reg_rule->max_bw,
				       reg_info->max_bw_5ghz);

			/* FW doesn't pass NL80211_RRF_AUTO_BW flag for
			 * BW Auto correction, we can enable this by default
			 * for all 5G rules here. The regulatory core performs
			 * BW correction if required and applies flags as
			 * per other BW rule flags we pass from here
			 */
			flags = NL80211_RRF_AUTO_BW;
		} else if (reg_info->is_ext_reg_event && reg_6ghz_number &&
			   k < reg_6ghz_number) {
			reg_rule = reg_rule_6ghz + k++;
			max_bw = min_t(u16, reg_rule->max_bw, max_bw_6ghz);
			flags = NL80211_RRF_AUTO_BW;
			if (reg_rule->psd_flag)
				flags |= NL80211_RRF_PSD;
		} else {
			break;
		}

		flags |= ath11k_map_fw_reg_flags(reg_rule->flags);
		flags |= ath11k_map_fw_phy_flags(reg_info->phybitmap);

		ath11k_reg_update_rule(tmp_regd->reg_rules + i,
				       reg_rule->start_freq,
				       reg_rule->end_freq, max_bw,
				       reg_rule->ant_gain, reg_rule->reg_power,
				       reg_rule->psd_eirp, flags);

		/* Update dfs cac timeout if the dfs domain is ETSI and the
		 * new rule covers weather radar band.
		 * Default value of '0' corresponds to 60s timeout, so no
		 * need to update that for other rules.
		 */
		if (flags & NL80211_RRF_DFS &&
		    reg_info->dfs_region == ATH11K_DFS_REG_ETSI &&
		    (reg_rule->end_freq > ETSI_WEATHER_RADAR_BAND_LOW &&
		    reg_rule->start_freq < ETSI_WEATHER_RADAR_BAND_HIGH)){
			ath11k_reg_update_weather_radar_band(ab, tmp_regd,
							     reg_rule, &i,
							     flags, max_bw);
			continue;
		}

		if (reg_info->is_ext_reg_event) {
			ath11k_dbg(ab, ATH11K_DBG_REG,
				   "\t%d. (%d - %d @ %d) (%d, %d) (%d ms) (FLAGS %d) (%d, %d)\n",
				   i + 1, reg_rule->start_freq, reg_rule->end_freq,
				   max_bw, reg_rule->ant_gain, reg_rule->reg_power,
				   tmp_regd->reg_rules[i].dfs_cac_ms, flags,
				   reg_rule->psd_flag, reg_rule->psd_eirp);
		} else {
			ath11k_dbg(ab, ATH11K_DBG_REG,
				   "\t%d. (%d - %d @ %d) (%d, %d) (%d ms) (FLAGS %d)\n",
				   i + 1, reg_rule->start_freq, reg_rule->end_freq,
				   max_bw, reg_rule->ant_gain, reg_rule->reg_power,
				   tmp_regd->reg_rules[i].dfs_cac_ms,
				   flags);
		}
	}

	tmp_regd->n_reg_rules = i;

	if (intersect) {
		default_regd = ab->default_regd[reg_info->phy_id];

		/* Get a new regd by intersecting the received regd with
		 * our default regd.
		 */
		new_regd = ath11k_regd_intersect(default_regd, tmp_regd);
		kfree(tmp_regd);
		if (!new_regd) {
			ath11k_warn(ab, "Unable to create intersected regdomain\n");
			goto ret;
		}
	} else {
		new_regd = tmp_regd;
	}

ret:
	return new_regd;
}

static bool ath11k_reg_is_world_alpha(char *alpha)
{
	if (alpha[0] == '0' && alpha[1] == '0')
		return true;

	if (alpha[0] == 'n' && alpha[1] == 'a')
		return true;

	return false;
}

static enum wmi_vdev_type ath11k_reg_get_ar_vdev_type(struct ath11k *ar)
{
	struct ath11k_vif *arvif;

	/* Currently each struct ath11k maps to one struct ieee80211_hw/wiphy
	 * and one struct ieee80211_regdomain, so it could only store one group
	 * reg rules. It means multi-interface concurrency in the same ath11k is
	 * not support for the regdomain. So get the vdev type of the first entry
	 * now. After concurrency support for the regdomain, this should change.
	 */
	arvif = list_first_entry_or_null(&ar->arvifs, struct ath11k_vif, list);
	if (arvif)
		return arvif->vdev_type;

	return WMI_VDEV_TYPE_UNSPEC;
}

int ath11k_reg_handle_chan_list(struct ath11k_base *ab,
				struct cur_regulatory_info *reg_info,
				enum ieee80211_ap_reg_power power_type)
{
	struct ieee80211_regdomain *regd;
	bool intersect = false;
	int pdev_idx;
	struct ath11k *ar;
	enum wmi_vdev_type vdev_type;

	ath11k_dbg(ab, ATH11K_DBG_WMI, "event reg handle chan list");

	if (reg_info->status_code != REG_SET_CC_STATUS_PASS) {
		/* In case of failure to set the requested ctry,
		 * fw retains the current regd. We print a failure info
		 * and return from here.
		 */
		ath11k_warn(ab, "Failed to set the requested Country regulatory setting\n");
		return -EINVAL;
	}

	pdev_idx = reg_info->phy_id;

	/* Avoid default reg rule updates sent during FW recovery if
	 * it is already available
	 */
	spin_lock_bh(&ab->base_lock);
	if (test_bit(ATH11K_FLAG_RECOVERY, &ab->dev_flags) &&
	    ab->default_regd[pdev_idx]) {
		spin_unlock_bh(&ab->base_lock);
		goto retfail;
	}
	spin_unlock_bh(&ab->base_lock);

	if (pdev_idx >= ab->num_radios) {
		/* Process the event for phy0 only if single_pdev_only
		 * is true. If pdev_idx is valid but not 0, discard the
		 * event. Otherwise, it goes to fallback. In either case
		 * ath11k_reg_reset_info() needs to be called to avoid
		 * memory leak issue.
		 */
		ath11k_reg_reset_info(reg_info);

		if (ab->hw_params.single_pdev_only &&
		    pdev_idx < ab->hw_params.num_rxdma_per_pdev)
			return 0;
		goto fallback;
	}

	/* Avoid multiple overwrites to default regd, during core
	 * stop-start after mac registration.
	 */
	if (ab->default_regd[pdev_idx] && !ab->new_regd[pdev_idx] &&
	    !memcmp((char *)ab->default_regd[pdev_idx]->alpha2,
		    (char *)reg_info->alpha2, 2))
		goto retfail;

	/* Intersect new rules with default regd if a new country setting was
	 * requested, i.e a default regd was already set during initialization
	 * and the regd coming from this event has a valid country info.
	 */
	if (ab->default_regd[pdev_idx] &&
	    !ath11k_reg_is_world_alpha((char *)
		ab->default_regd[pdev_idx]->alpha2) &&
	    !ath11k_reg_is_world_alpha((char *)reg_info->alpha2))
		intersect = true;

	ar = ab->pdevs[pdev_idx].ar;
	vdev_type = ath11k_reg_get_ar_vdev_type(ar);

	ath11k_dbg(ab, ATH11K_DBG_WMI,
		   "wmi handle chan list power type %d vdev type %d intersect %d\n",
		   power_type, vdev_type, intersect);

	regd = ath11k_reg_build_regd(ab, reg_info, intersect, vdev_type, power_type);
	if (!regd) {
		ath11k_warn(ab, "failed to build regd from reg_info\n");
		goto fallback;
	}

	if (power_type == IEEE80211_REG_UNSET_AP) {
		ath11k_reg_reset_info(&ab->reg_info_store[pdev_idx]);
		ab->reg_info_store[pdev_idx] = *reg_info;
	}

	spin_lock_bh(&ab->base_lock);
	if (ab->default_regd[pdev_idx]) {
		/* The initial rules from FW after WMI Init is to build
		 * the default regd. From then on, any rules updated for
		 * the pdev could be due to user reg changes.
		 * Free previously built regd before assigning the newly
		 * generated regd to ar. NULL pointer handling will be
		 * taken care by kfree itself.
		 */
		ar = ab->pdevs[pdev_idx].ar;
		kfree(ab->new_regd[pdev_idx]);
		ab->new_regd[pdev_idx] = regd;
		queue_work(ab->workqueue, &ar->regd_update_work);
	} else {
		/* This regd would be applied during mac registration and is
		 * held constant throughout for regd intersection purpose
		 */
		ab->default_regd[pdev_idx] = regd;
	}
	ab->dfs_region = reg_info->dfs_region;
	spin_unlock_bh(&ab->base_lock);

	return 0;

fallback:
	/* Fallback to older reg (by sending previous country setting
	 * again if fw has succeeded and we failed to process here.
	 * The Regdomain should be uniform across driver and fw. Since the
	 * FW has processed the command and sent a success status, we expect
	 * this function to succeed as well. If it doesn't, CTRY needs to be
	 * reverted at the fw and the old SCAN_CHAN_LIST cmd needs to be sent.
	 */
	/* TODO: This is rare, but still should also be handled */
	WARN_ON(1);

retfail:

	return -EINVAL;
}

void ath11k_regd_update_work(struct work_struct *work)
{
	struct ath11k *ar = container_of(work, struct ath11k,
					 regd_update_work);
	int ret;

	ret = ath11k_regd_update(ar);
	if (ret) {
		/* Firmware has already moved to the new regd. We need
		 * to maintain channel consistency across FW, Host driver
		 * and userspace. Hence as a fallback mechanism we can set
		 * the prev or default country code to the firmware.
		 */
		/* TODO: Implement Fallback Mechanism */
	}
}

void ath11k_reg_init(struct ath11k *ar)
{
	ar->hw->wiphy->regulatory_flags = REGULATORY_WIPHY_SELF_MANAGED;
	ar->hw->wiphy->reg_notifier = ath11k_reg_notifier;
}

void ath11k_reg_reset_info(struct cur_regulatory_info *reg_info)
{
	int i, j;

	if (!reg_info)
		return;

	kfree(reg_info->reg_rules_2ghz_ptr);
	kfree(reg_info->reg_rules_5ghz_ptr);

	for (i = 0; i < WMI_REG_CURRENT_MAX_AP_TYPE; i++) {
		kfree(reg_info->reg_rules_6ghz_ap_ptr[i]);

		for (j = 0; j < WMI_REG_MAX_CLIENT_TYPE; j++)
			kfree(reg_info->reg_rules_6ghz_client_ptr[i][j]);
	}

	memset(reg_info, 0, sizeof(*reg_info));
}

void ath11k_reg_free(struct ath11k_base *ab)
{
	int i;

	for (i = 0; i < ab->num_radios; i++)
		ath11k_reg_reset_info(&ab->reg_info_store[i]);

	kfree(ab->reg_info_store);
	ab->reg_info_store = NULL;

	for (i = 0; i < ab->hw_params.max_radios; i++) {
		kfree(ab->default_regd[i]);
		kfree(ab->new_regd[i]);
	}
}

int ath11k_reg_set_cc(struct ath11k *ar)
{
	struct wmi_set_current_country_params set_current_param = {};

	memcpy(&set_current_param.alpha2, ar->alpha2, 2);
	return ath11k_wmi_send_set_current_country_cmd(ar, &set_current_param);
}
