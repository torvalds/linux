// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <linux/rtnetlink.h>
#include "core.h"
#include "debug.h"
#include "mac.h"

/* World regdom to be used in case default regd from fw is unavailable */
#define ATH12K_2GHZ_CH01_11      REG_RULE(2412 - 10, 2462 + 10, 40, 0, 20, 0)
#define ATH12K_5GHZ_5150_5350    REG_RULE(5150 - 10, 5350 + 10, 80, 0, 30,\
					  NL80211_RRF_NO_IR)
#define ATH12K_5GHZ_5725_5850    REG_RULE(5725 - 10, 5850 + 10, 80, 0, 30,\
					  NL80211_RRF_NO_IR)

#define ETSI_WEATHER_RADAR_BAND_LOW		5590
#define ETSI_WEATHER_RADAR_BAND_HIGH		5650
#define ETSI_WEATHER_RADAR_BAND_CAC_TIMEOUT	600000

static const struct ieee80211_regdomain ath12k_world_regd = {
	.n_reg_rules = 3,
	.alpha2 = "00",
	.reg_rules = {
		ATH12K_2GHZ_CH01_11,
		ATH12K_5GHZ_5150_5350,
		ATH12K_5GHZ_5725_5850,
	}
};

static bool ath12k_regdom_changes(struct ieee80211_hw *hw, char *alpha2)
{
	const struct ieee80211_regdomain *regd;

	regd = rcu_dereference_rtnl(hw->wiphy->regd);
	/* This can happen during wiphy registration where the previous
	 * user request is received before we update the regd received
	 * from firmware.
	 */
	if (!regd)
		return true;

	return memcmp(regd->alpha2, alpha2, 2) != 0;
}

static void
ath12k_reg_notifier(struct wiphy *wiphy, struct regulatory_request *request)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct ath12k_wmi_init_country_arg arg;
	struct wmi_set_current_country_arg current_arg = {};
	struct ath12k_hw *ah = ath12k_hw_to_ah(hw);
	struct ath12k *ar = ath12k_ah_to_ar(ah, 0);
	int ret, i;

	ath12k_dbg(ar->ab, ATH12K_DBG_REG,
		   "Regulatory Notification received for %s\n", wiphy_name(wiphy));

	if (request->initiator == NL80211_REGDOM_SET_BY_DRIVER) {
		ath12k_dbg(ar->ab, ATH12K_DBG_REG,
			   "driver initiated regd update\n");
		if (ah->state != ATH12K_HW_STATE_ON)
			return;

		for_each_ar(ah, ar, i) {
			ret = ath12k_reg_update_chan_list(ar, true);
			if (ret) {
				ath12k_warn(ar->ab,
					    "failed to update chan list for pdev %u, ret %d\n",
					    i, ret);
				break;
			}
		}
		return;
	}

	/* Currently supporting only General User Hints. Cell base user
	 * hints to be handled later.
	 * Hints from other sources like Core, Beacons are not expected for
	 * self managed wiphy's
	 */
	if (!(request->initiator == NL80211_REGDOM_SET_BY_USER &&
	      request->user_reg_hint_type == NL80211_USER_REG_HINT_USER)) {
		ath12k_warn(ar->ab, "Unexpected Regulatory event for this wiphy\n");
		return;
	}

	if (!IS_ENABLED(CONFIG_ATH_REG_DYNAMIC_USER_REG_HINTS)) {
		ath12k_dbg(ar->ab, ATH12K_DBG_REG,
			   "Country Setting is not allowed\n");
		return;
	}

	if (!ath12k_regdom_changes(hw, request->alpha2)) {
		ath12k_dbg(ar->ab, ATH12K_DBG_REG, "Country is already set\n");
		return;
	}

	/* Allow fresh updates to wiphy regd */
	ah->regd_updated = false;

	/* Send the reg change request to all the radios */
	for_each_ar(ah, ar, i) {
		if (ar->ab->hw_params->current_cc_support) {
			memcpy(&current_arg.alpha2, request->alpha2, 2);
			memcpy(&ar->alpha2, &current_arg.alpha2, 2);
			ret = ath12k_wmi_send_set_current_country_cmd(ar, &current_arg);
			if (ret)
				ath12k_warn(ar->ab,
					    "failed set current country code: %d\n", ret);
		} else {
			arg.flags = ALPHA_IS_SET;
			memcpy(&arg.cc_info.alpha2, request->alpha2, 2);
			arg.cc_info.alpha2[2] = 0;

			ret = ath12k_wmi_send_init_country_cmd(ar, &arg);
			if (ret)
				ath12k_warn(ar->ab,
					    "failed set INIT Country code: %d\n", ret);
		}

		wiphy_lock(wiphy);
		ath12k_mac_11d_scan_stop(ar);
		wiphy_unlock(wiphy);

		ar->regdom_set_by_user = true;
	}
}

int ath12k_reg_update_chan_list(struct ath12k *ar, bool wait)
{
	struct ieee80211_supported_band **bands;
	struct ath12k_wmi_scan_chan_list_arg *arg;
	struct ieee80211_channel *channel;
	struct ieee80211_hw *hw = ath12k_ar_to_hw(ar);
	struct ath12k_wmi_channel_arg *ch;
	enum nl80211_band band;
	int num_channels = 0;
	int i, ret, left;

	if (wait && ar->state_11d != ATH12K_11D_IDLE) {
		left = wait_for_completion_timeout(&ar->completed_11d_scan,
						   ATH12K_SCAN_TIMEOUT_HZ);
		if (!left) {
			ath12k_dbg(ar->ab, ATH12K_DBG_REG,
				   "failed to receive 11d scan complete: timed out\n");
			ar->state_11d = ATH12K_11D_IDLE;
		}
		ath12k_dbg(ar->ab, ATH12K_DBG_REG,
			   "reg 11d scan wait left time %d\n", left);
	}

	if (wait &&
	    (ar->scan.state == ATH12K_SCAN_STARTING ||
	    ar->scan.state == ATH12K_SCAN_RUNNING)) {
		left = wait_for_completion_timeout(&ar->scan.completed,
						   ATH12K_SCAN_TIMEOUT_HZ);
		if (!left)
			ath12k_dbg(ar->ab, ATH12K_DBG_REG,
				   "failed to receive hw scan complete: timed out\n");

		ath12k_dbg(ar->ab, ATH12K_DBG_REG,
			   "reg hw scan wait left time %d\n", left);
	}

	if (ar->ah->state == ATH12K_HW_STATE_RESTARTING)
		return 0;

	bands = hw->wiphy->bands;
	for (band = 0; band < NUM_NL80211_BANDS; band++) {
		if (!(ar->mac.sbands[band].channels && bands[band]))
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

	arg = kzalloc(struct_size(arg, channel, num_channels), GFP_KERNEL);

	if (!arg)
		return -ENOMEM;

	arg->pdev_id = ar->pdev->pdev_id;
	arg->nallchans = num_channels;

	ch = arg->channel;

	for (band = 0; band < NUM_NL80211_BANDS; band++) {
		if (!(ar->mac.sbands[band].channels && bands[band]))
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

			ath12k_dbg(ar->ab, ATH12K_DBG_WMI,
				   "mac channel [%d/%d] freq %d maxpower %d regpower %d antenna %d mode %d\n",
				   i, arg->nallchans,
				   ch->mhz, ch->maxpower, ch->maxregpower,
				   ch->antennamax, ch->phy_mode);

			ch++;
			/* TODO: use quarrter/half rate, cfreq12, dfs_cfreq2
			 * set_agile, reg_class_idx
			 */
		}
	}

	ret = ath12k_wmi_send_scan_chan_list_cmd(ar, arg);
	kfree(arg);

	return ret;
}

static void ath12k_copy_regd(struct ieee80211_regdomain *regd_orig,
			     struct ieee80211_regdomain *regd_copy)
{
	u8 i;

	/* The caller should have checked error conditions */
	memcpy(regd_copy, regd_orig, sizeof(*regd_orig));

	for (i = 0; i < regd_orig->n_reg_rules; i++)
		memcpy(&regd_copy->reg_rules[i], &regd_orig->reg_rules[i],
		       sizeof(struct ieee80211_reg_rule));
}

int ath12k_regd_update(struct ath12k *ar, bool init)
{
	u32 phy_id, freq_low = 0, freq_high = 0, supported_bands, band;
	struct ath12k_wmi_hal_reg_capabilities_ext_arg *reg_cap;
	struct ath12k_hw *ah = ath12k_ar_to_ah(ar);
	struct ieee80211_hw *hw = ah->hw;
	struct ieee80211_regdomain *regd, *regd_copy = NULL;
	int ret, regd_len, pdev_id;
	struct ath12k_base *ab;

	ab = ar->ab;

	supported_bands = ar->pdev->cap.supported_bands;
	if (supported_bands & WMI_HOST_WLAN_2GHZ_CAP) {
		band = NL80211_BAND_2GHZ;
	} else if (supported_bands & WMI_HOST_WLAN_5GHZ_CAP && !ar->supports_6ghz) {
		band = NL80211_BAND_5GHZ;
	} else if (supported_bands & WMI_HOST_WLAN_5GHZ_CAP && ar->supports_6ghz) {
		band = NL80211_BAND_6GHZ;
	} else {
		/* This condition is not expected.
		 */
		WARN_ON(1);
		ret = -EINVAL;
		goto err;
	}

	reg_cap = &ab->hal_reg_cap[ar->pdev_idx];

	if (ab->hw_params->single_pdev_only && !ar->supports_6ghz) {
		phy_id = ar->pdev->cap.band[band].phy_id;
		reg_cap = &ab->hal_reg_cap[phy_id];
	}

	/* Possible that due to reg change, current limits for supported
	 * frequency changed. Update that
	 */
	if (supported_bands & WMI_HOST_WLAN_2GHZ_CAP) {
		freq_low = max(reg_cap->low_2ghz_chan, ab->reg_freq_2ghz.start_freq);
		freq_high = min(reg_cap->high_2ghz_chan, ab->reg_freq_2ghz.end_freq);
	} else if (supported_bands & WMI_HOST_WLAN_5GHZ_CAP && !ar->supports_6ghz) {
		freq_low = max(reg_cap->low_5ghz_chan, ab->reg_freq_5ghz.start_freq);
		freq_high = min(reg_cap->high_5ghz_chan, ab->reg_freq_5ghz.end_freq);
	} else if (supported_bands & WMI_HOST_WLAN_5GHZ_CAP && ar->supports_6ghz) {
		freq_low = max(reg_cap->low_5ghz_chan, ab->reg_freq_6ghz.start_freq);
		freq_high = min(reg_cap->high_5ghz_chan, ab->reg_freq_6ghz.end_freq);
	}

	ath12k_mac_update_freq_range(ar, freq_low, freq_high);

	ath12k_dbg(ab, ATH12K_DBG_REG, "pdev %u reg updated freq limits %u->%u MHz\n",
		   ar->pdev->pdev_id, freq_low, freq_high);

	/* If one of the radios within ah has already updated the regd for
	 * the wiphy, then avoid setting regd again
	 */
	if (ah->regd_updated)
		return 0;

	/* firmware provides reg rules which are similar for 2 GHz and 5 GHz
	 * pdev but 6 GHz pdev has superset of all rules including rules for
	 * all bands, we prefer 6 GHz pdev's rules to be used for setup of
	 * the wiphy regd.
	 * If 6 GHz pdev was part of the ath12k_hw, wait for the 6 GHz pdev,
	 * else pick the first pdev which calls this function and use its
	 * regd to update global hw regd.
	 * The regd_updated flag set at the end will not allow any further
	 * updates.
	 */
	if (ah->use_6ghz_regd && !ar->supports_6ghz)
		return 0;

	pdev_id = ar->pdev_idx;

	spin_lock_bh(&ab->base_lock);

	if (init) {
		/* Apply the regd received during init through
		 * WMI_REG_CHAN_LIST_CC event. In case of failure to
		 * receive the regd, initialize with a default world
		 * regulatory.
		 */
		if (ab->default_regd[pdev_id]) {
			regd = ab->default_regd[pdev_id];
		} else {
			ath12k_warn(ab,
				    "failed to receive default regd during init\n");
			regd = (struct ieee80211_regdomain *)&ath12k_world_regd;
		}
	} else {
		regd = ab->new_regd[pdev_id];
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
		ath12k_copy_regd(regd, regd_copy);

	spin_unlock_bh(&ab->base_lock);

	if (!regd_copy) {
		ret = -ENOMEM;
		goto err;
	}

	ret = regulatory_set_wiphy_regd(hw->wiphy, regd_copy);

	kfree(regd_copy);

	if (ret)
		goto err;

	if (ah->state != ATH12K_HW_STATE_ON)
		goto skip;

	ah->regd_updated = true;

skip:
	return 0;
err:
	ath12k_warn(ab, "failed to perform regd update : %d\n", ret);
	return ret;
}

static enum nl80211_dfs_regions
ath12k_map_fw_dfs_region(enum ath12k_dfs_region dfs_region)
{
	switch (dfs_region) {
	case ATH12K_DFS_REG_FCC:
	case ATH12K_DFS_REG_CN:
		return NL80211_DFS_FCC;
	case ATH12K_DFS_REG_ETSI:
	case ATH12K_DFS_REG_KR:
		return NL80211_DFS_ETSI;
	case ATH12K_DFS_REG_MKK:
	case ATH12K_DFS_REG_MKK_N:
		return NL80211_DFS_JP;
	default:
		return NL80211_DFS_UNSET;
	}
}

static u32 ath12k_map_fw_reg_flags(u16 reg_flags)
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

static u32 ath12k_map_fw_phy_flags(u32 phy_flags)
{
	u32 flags = 0;

	if (phy_flags & ATH12K_REG_PHY_BITMAP_NO11AX)
		flags |= NL80211_RRF_NO_HE;

	if (phy_flags & ATH12K_REG_PHY_BITMAP_NO11BE)
		flags |= NL80211_RRF_NO_EHT;

	return flags;
}

static bool
ath12k_reg_can_intersect(struct ieee80211_reg_rule *rule1,
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

static void ath12k_reg_intersect_rules(struct ieee80211_reg_rule *rule1,
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

	/* To be safe, lts use the max cac timeout of both rules */
	new_rule->dfs_cac_ms = max_t(u32, rule1->dfs_cac_ms,
				     rule2->dfs_cac_ms);
}

static struct ieee80211_regdomain *
ath12k_regd_intersect(struct ieee80211_regdomain *default_regd,
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

			if (ath12k_reg_can_intersect(old_rule, curr_rule))
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

			if (ath12k_reg_can_intersect(old_rule, curr_rule))
				ath12k_reg_intersect_rules(old_rule, curr_rule,
							   (new_rule + k++));
		}
	}
	return new_regd;
}

static const char *
ath12k_reg_get_regdom_str(enum nl80211_dfs_regions dfs_region)
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
ath12k_reg_adjust_bw(u16 start_freq, u16 end_freq, u16 max_bw)
{
	u16 bw;

	bw = end_freq - start_freq;
	bw = min_t(u16, bw, max_bw);

	if (bw >= 80 && bw < 160)
		bw = 80;
	else if (bw >= 40 && bw < 80)
		bw = 40;
	else if (bw < 40)
		bw = 20;

	return bw;
}

static void
ath12k_reg_update_rule(struct ieee80211_reg_rule *reg_rule, u32 start_freq,
		       u32 end_freq, u32 bw, u32 ant_gain, u32 reg_pwr,
		       u32 reg_flags)
{
	reg_rule->freq_range.start_freq_khz = MHZ_TO_KHZ(start_freq);
	reg_rule->freq_range.end_freq_khz = MHZ_TO_KHZ(end_freq);
	reg_rule->freq_range.max_bandwidth_khz = MHZ_TO_KHZ(bw);
	reg_rule->power_rule.max_antenna_gain = DBI_TO_MBI(ant_gain);
	reg_rule->power_rule.max_eirp = DBM_TO_MBM(reg_pwr);
	reg_rule->flags = reg_flags;
}

static void
ath12k_reg_update_weather_radar_band(struct ath12k_base *ab,
				     struct ieee80211_regdomain *regd,
				     struct ath12k_reg_rule *reg_rule,
				     u8 *rule_idx, u32 flags, u16 max_bw)
{
	u32 end_freq;
	u16 bw;
	u8 i;

	i = *rule_idx;

	bw = ath12k_reg_adjust_bw(reg_rule->start_freq,
				  ETSI_WEATHER_RADAR_BAND_LOW, max_bw);

	ath12k_reg_update_rule(regd->reg_rules + i, reg_rule->start_freq,
			       ETSI_WEATHER_RADAR_BAND_LOW, bw,
			       reg_rule->ant_gain, reg_rule->reg_power,
			       flags);

	ath12k_dbg(ab, ATH12K_DBG_REG,
		   "\t%d. (%d - %d @ %d) (%d, %d) (%d ms) (FLAGS %d)\n",
		   i + 1, reg_rule->start_freq, ETSI_WEATHER_RADAR_BAND_LOW,
		   bw, reg_rule->ant_gain, reg_rule->reg_power,
		   regd->reg_rules[i].dfs_cac_ms,
		   flags);

	if (reg_rule->end_freq > ETSI_WEATHER_RADAR_BAND_HIGH)
		end_freq = ETSI_WEATHER_RADAR_BAND_HIGH;
	else
		end_freq = reg_rule->end_freq;

	bw = ath12k_reg_adjust_bw(ETSI_WEATHER_RADAR_BAND_LOW, end_freq,
				  max_bw);

	i++;

	ath12k_reg_update_rule(regd->reg_rules + i,
			       ETSI_WEATHER_RADAR_BAND_LOW, end_freq, bw,
			       reg_rule->ant_gain, reg_rule->reg_power,
			       flags);

	regd->reg_rules[i].dfs_cac_ms = ETSI_WEATHER_RADAR_BAND_CAC_TIMEOUT;

	ath12k_dbg(ab, ATH12K_DBG_REG,
		   "\t%d. (%d - %d @ %d) (%d, %d) (%d ms) (FLAGS %d)\n",
		   i + 1, ETSI_WEATHER_RADAR_BAND_LOW, end_freq,
		   bw, reg_rule->ant_gain, reg_rule->reg_power,
		   regd->reg_rules[i].dfs_cac_ms,
		   flags);

	if (end_freq == reg_rule->end_freq) {
		regd->n_reg_rules--;
		*rule_idx = i;
		return;
	}

	bw = ath12k_reg_adjust_bw(ETSI_WEATHER_RADAR_BAND_HIGH,
				  reg_rule->end_freq, max_bw);

	i++;

	ath12k_reg_update_rule(regd->reg_rules + i, ETSI_WEATHER_RADAR_BAND_HIGH,
			       reg_rule->end_freq, bw,
			       reg_rule->ant_gain, reg_rule->reg_power,
			       flags);

	ath12k_dbg(ab, ATH12K_DBG_REG,
		   "\t%d. (%d - %d @ %d) (%d, %d) (%d ms) (FLAGS %d)\n",
		   i + 1, ETSI_WEATHER_RADAR_BAND_HIGH, reg_rule->end_freq,
		   bw, reg_rule->ant_gain, reg_rule->reg_power,
		   regd->reg_rules[i].dfs_cac_ms,
		   flags);

	*rule_idx = i;
}

static void ath12k_reg_update_freq_range(struct ath12k_reg_freq *reg_freq,
					 struct ath12k_reg_rule *reg_rule)
{
	if (reg_freq->start_freq > reg_rule->start_freq)
		reg_freq->start_freq = reg_rule->start_freq;

	if (reg_freq->end_freq < reg_rule->end_freq)
		reg_freq->end_freq = reg_rule->end_freq;
}

struct ieee80211_regdomain *
ath12k_reg_build_regd(struct ath12k_base *ab,
		      struct ath12k_reg_info *reg_info, bool intersect)
{
	struct ieee80211_regdomain *tmp_regd, *default_regd, *new_regd = NULL;
	struct ath12k_reg_rule *reg_rule;
	u8 i = 0, j = 0, k = 0;
	u8 num_rules;
	u16 max_bw;
	u32 flags;
	char alpha2[3];

	num_rules = reg_info->num_5g_reg_rules + reg_info->num_2g_reg_rules;

	/* FIXME: Currently taking reg rules for 6G only from Indoor AP mode list.
	 * This can be updated to choose the combination dynamically based on AP
	 * type and client type, after complete 6G regulatory support is added.
	 */
	if (reg_info->is_ext_reg_event)
		num_rules += reg_info->num_6g_reg_rules_ap[WMI_REG_INDOOR_AP];

	if (!num_rules)
		goto ret;

	/* Add max additional rules to accommodate weather radar band */
	if (reg_info->dfs_region == ATH12K_DFS_REG_ETSI)
		num_rules += 2;

	tmp_regd = kzalloc(sizeof(*tmp_regd) +
			   (num_rules * sizeof(struct ieee80211_reg_rule)),
			   GFP_ATOMIC);
	if (!tmp_regd)
		goto ret;

	memcpy(tmp_regd->alpha2, reg_info->alpha2, REG_ALPHA2_LEN + 1);
	memcpy(alpha2, reg_info->alpha2, REG_ALPHA2_LEN + 1);
	alpha2[2] = '\0';
	tmp_regd->dfs_region = ath12k_map_fw_dfs_region(reg_info->dfs_region);

	ath12k_dbg(ab, ATH12K_DBG_REG,
		   "\r\nCountry %s, CFG Regdomain %s FW Regdomain %d, num_reg_rules %d\n",
		   alpha2, ath12k_reg_get_regdom_str(tmp_regd->dfs_region),
		   reg_info->dfs_region, num_rules);

	/* Reset start and end frequency for each band
	 */
	ab->reg_freq_5ghz.start_freq = INT_MAX;
	ab->reg_freq_5ghz.end_freq = 0;
	ab->reg_freq_2ghz.start_freq = INT_MAX;
	ab->reg_freq_2ghz.end_freq = 0;
	ab->reg_freq_6ghz.start_freq = INT_MAX;
	ab->reg_freq_6ghz.end_freq = 0;

	/* Update reg_rules[] below. Firmware is expected to
	 * send these rules in order(2G rules first and then 5G)
	 */
	for (; i < num_rules; i++) {
		if (reg_info->num_2g_reg_rules &&
		    (i < reg_info->num_2g_reg_rules)) {
			reg_rule = reg_info->reg_rules_2g_ptr + i;
			max_bw = min_t(u16, reg_rule->max_bw,
				       reg_info->max_bw_2g);
			flags = 0;
			ath12k_reg_update_freq_range(&ab->reg_freq_2ghz, reg_rule);
		} else if (reg_info->num_5g_reg_rules &&
			   (j < reg_info->num_5g_reg_rules)) {
			reg_rule = reg_info->reg_rules_5g_ptr + j++;
			max_bw = min_t(u16, reg_rule->max_bw,
				       reg_info->max_bw_5g);

			/* FW doesn't pass NL80211_RRF_AUTO_BW flag for
			 * BW Auto correction, we can enable this by default
			 * for all 5G rules here. The regulatory core performs
			 * BW correction if required and applies flags as
			 * per other BW rule flags we pass from here
			 */
			flags = NL80211_RRF_AUTO_BW;
			ath12k_reg_update_freq_range(&ab->reg_freq_5ghz, reg_rule);
		} else if (reg_info->is_ext_reg_event &&
			   reg_info->num_6g_reg_rules_ap[WMI_REG_INDOOR_AP] &&
			(k < reg_info->num_6g_reg_rules_ap[WMI_REG_INDOOR_AP])) {
			reg_rule = reg_info->reg_rules_6g_ap_ptr[WMI_REG_INDOOR_AP] + k++;
			max_bw = min_t(u16, reg_rule->max_bw,
				       reg_info->max_bw_6g_ap[WMI_REG_INDOOR_AP]);
			flags = NL80211_RRF_AUTO_BW;
			ath12k_reg_update_freq_range(&ab->reg_freq_6ghz, reg_rule);
		} else {
			break;
		}

		flags |= ath12k_map_fw_reg_flags(reg_rule->flags);
		flags |= ath12k_map_fw_phy_flags(reg_info->phybitmap);

		ath12k_reg_update_rule(tmp_regd->reg_rules + i,
				       reg_rule->start_freq,
				       reg_rule->end_freq, max_bw,
				       reg_rule->ant_gain, reg_rule->reg_power,
				       flags);

		/* Update dfs cac timeout if the dfs domain is ETSI and the
		 * new rule covers weather radar band.
		 * Default value of '0' corresponds to 60s timeout, so no
		 * need to update that for other rules.
		 */
		if (flags & NL80211_RRF_DFS &&
		    reg_info->dfs_region == ATH12K_DFS_REG_ETSI &&
		    (reg_rule->end_freq > ETSI_WEATHER_RADAR_BAND_LOW &&
		    reg_rule->start_freq < ETSI_WEATHER_RADAR_BAND_HIGH)){
			ath12k_reg_update_weather_radar_band(ab, tmp_regd,
							     reg_rule, &i,
							     flags, max_bw);
			continue;
		}

		if (reg_info->is_ext_reg_event) {
			ath12k_dbg(ab, ATH12K_DBG_REG, "\t%d. (%d - %d @ %d) (%d, %d) (%d ms) (FLAGS %d) (%d, %d)\n",
				   i + 1, reg_rule->start_freq, reg_rule->end_freq,
				   max_bw, reg_rule->ant_gain, reg_rule->reg_power,
				   tmp_regd->reg_rules[i].dfs_cac_ms,
				   flags, reg_rule->psd_flag, reg_rule->psd_eirp);
		} else {
			ath12k_dbg(ab, ATH12K_DBG_REG,
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
		new_regd = ath12k_regd_intersect(default_regd, tmp_regd);
		kfree(tmp_regd);
		if (!new_regd) {
			ath12k_warn(ab, "Unable to create intersected regdomain\n");
			goto ret;
		}
	} else {
		new_regd = tmp_regd;
	}

ret:
	return new_regd;
}

void ath12k_regd_update_work(struct work_struct *work)
{
	struct ath12k *ar = container_of(work, struct ath12k,
					 regd_update_work);
	int ret;

	ret = ath12k_regd_update(ar, false);
	if (ret) {
		/* Firmware has already moved to the new regd. We need
		 * to maintain channel consistency across FW, Host driver
		 * and userspace. Hence as a fallback mechanism we can set
		 * the prev or default country code to the firmware.
		 */
		/* TODO: Implement Fallback Mechanism */
	}
}

void ath12k_reg_reset_reg_info(struct ath12k_reg_info *reg_info)
{
	u8 i, j;

	if (!reg_info)
		return;

	kfree(reg_info->reg_rules_2g_ptr);
	kfree(reg_info->reg_rules_5g_ptr);

	if (reg_info->is_ext_reg_event) {
		for (i = 0; i < WMI_REG_CURRENT_MAX_AP_TYPE; i++) {
			kfree(reg_info->reg_rules_6g_ap_ptr[i]);

			for (j = 0; j < WMI_REG_MAX_CLIENT_TYPE; j++)
				kfree(reg_info->reg_rules_6g_client_ptr[i][j]);
		}
	}
}

static bool ath12k_reg_is_world_alpha(char *alpha)
{
	if (alpha[0] == '0' && alpha[1] == '0')
		return true;

	if (alpha[0] == 'n' && alpha[1] == 'a')
		return true;

	return false;
}

int ath12k_reg_handle_chan_list(struct ath12k_base *ab,
				struct ath12k_reg_info *reg_info)
{
	struct ieee80211_regdomain *regd = NULL;
	bool intersect = false;
	struct ath12k *ar;
	int pdev_idx;

	if (reg_info->status_code != REG_SET_CC_STATUS_PASS) {
		/* In case of failure to set the requested country,
		 * firmware retains the current regd. We print a failure info
		 * and return from here.
		 */
		ath12k_warn(ab, "Failed to set the requested Country regulatory setting\n");
		return 0;
	}

	pdev_idx = reg_info->phy_id;
	if (pdev_idx >= ab->num_radios) {
		/* Process the event for phy0 only if single_pdev_only
		 * is true. If pdev_idx is valid but not 0, discard the
		 * event. Otherwise, it goes to fallback.
		 */
		if (ab->hw_params->single_pdev_only &&
		    pdev_idx < ab->hw_params->num_rxdma_per_pdev)
			return 0;
		else
			return -EINVAL;
	}

	/* Avoid multiple overwrites to default regd, during core
	 * stop-start after mac registration.
	 */
	if (ab->default_regd[pdev_idx] && !ab->new_regd[pdev_idx] &&
	    !memcmp(ab->default_regd[pdev_idx]->alpha2,
		    reg_info->alpha2, 2))
		return 0;

	/* Intersect new rules with default regd if a new country setting was
	 * requested, i.e a default regd was already set during initialization
	 * and the regd coming from this event has a valid country info.
	 */
	if (ab->default_regd[pdev_idx] &&
	    !ath12k_reg_is_world_alpha((char *)
		ab->default_regd[pdev_idx]->alpha2) &&
	    !ath12k_reg_is_world_alpha((char *)reg_info->alpha2))
		intersect = true;

	regd = ath12k_reg_build_regd(ab, reg_info, intersect);
	if (!regd)
		return -EINVAL;

	spin_lock_bh(&ab->base_lock);
	if (test_bit(ATH12K_FLAG_REGISTERED, &ab->dev_flags)) {
		/* Once mac is registered, ar is valid and all CC events from
		 * firmware is considered to be received due to user requests
		 * currently.
		 * Free previously built regd before assigning the newly
		 * generated regd to ar. NULL pointer handling will be
		 * taken care by kfree itself.
		 */
		ar = ab->pdevs[pdev_idx].ar;
		kfree(ab->new_regd[pdev_idx]);
		ab->new_regd[pdev_idx] = regd;
		queue_work(ab->workqueue, &ar->regd_update_work);
	} else {
		/* Multiple events for the same *ar is not expected. But we
		 * can still clear any previously stored default_regd if we
		 * are receiving this event for the same radio by mistake.
		 * NULL pointer handling will be taken care by kfree itself.
		 */
		kfree(ab->default_regd[pdev_idx]);
		/* This regd would be applied during mac registration */
		ab->default_regd[pdev_idx] = regd;
	}
	ab->dfs_region = reg_info->dfs_region;
	spin_unlock_bh(&ab->base_lock);

	return 0;
}

void ath12k_reg_init(struct ieee80211_hw *hw)
{
	hw->wiphy->regulatory_flags = REGULATORY_WIPHY_SELF_MANAGED;
	hw->wiphy->flags |= WIPHY_FLAG_NOTIFY_REGDOM_BY_DRIVER;
	hw->wiphy->reg_notifier = ath12k_reg_notifier;
}

void ath12k_reg_free(struct ath12k_base *ab)
{
	int i;

	mutex_lock(&ab->core_lock);
	for (i = 0; i < ab->hw_params->max_radios; i++) {
		kfree(ab->default_regd[i]);
		kfree(ab->new_regd[i]);
		ab->default_regd[i] = NULL;
		ab->new_regd[i] = NULL;
	}
	mutex_unlock(&ab->core_lock);
}
