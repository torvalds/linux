/*
 * hostapd / Hardware feature query and different modes
 * Copyright 2002-2003, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright (c) 2008-2009, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "utils/eloop.h"
#include "common/ieee802_11_defs.h"
#include "common/ieee802_11_common.h"
#include "drivers/driver.h"
#include "hostapd.h"
#include "ap_config.h"
#include "ap_drv_ops.h"
#include "hw_features.h"


void hostapd_free_hw_features(struct hostapd_hw_modes *hw_features,
			      size_t num_hw_features)
{
	size_t i;

	if (hw_features == NULL)
		return;

	for (i = 0; i < num_hw_features; i++) {
		os_free(hw_features[i].channels);
		os_free(hw_features[i].rates);
	}

	os_free(hw_features);
}


int hostapd_get_hw_features(struct hostapd_iface *iface)
{
	struct hostapd_data *hapd = iface->bss[0];
	int ret = 0, i, j;
	u16 num_modes, flags;
	struct hostapd_hw_modes *modes;

	if (hostapd_drv_none(hapd))
		return -1;
	modes = hostapd_get_hw_feature_data(hapd, &num_modes, &flags);
	if (modes == NULL) {
		hostapd_logger(hapd, NULL, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_DEBUG,
			       "Fetching hardware channel/rate support not "
			       "supported.");
		return -1;
	}

	iface->hw_flags = flags;

	hostapd_free_hw_features(iface->hw_features, iface->num_hw_features);
	iface->hw_features = modes;
	iface->num_hw_features = num_modes;

	for (i = 0; i < num_modes; i++) {
		struct hostapd_hw_modes *feature = &modes[i];
		/* set flag for channels we can use in current regulatory
		 * domain */
		for (j = 0; j < feature->num_channels; j++) {
			/*
			 * Disable all channels that are marked not to allow
			 * IBSS operation or active scanning. In addition,
			 * disable all channels that require radar detection,
			 * since that (in addition to full DFS) is not yet
			 * supported.
			 */
			if (feature->channels[j].flag &
			    (HOSTAPD_CHAN_NO_IBSS |
			     HOSTAPD_CHAN_PASSIVE_SCAN |
			     HOSTAPD_CHAN_RADAR))
				feature->channels[j].flag |=
					HOSTAPD_CHAN_DISABLED;
			if (feature->channels[j].flag & HOSTAPD_CHAN_DISABLED)
				continue;
			wpa_printf(MSG_MSGDUMP, "Allowed channel: mode=%d "
				   "chan=%d freq=%d MHz max_tx_power=%d dBm",
				   feature->mode,
				   feature->channels[j].chan,
				   feature->channels[j].freq,
				   feature->channels[j].max_tx_power);
		}
	}

	return ret;
}


int hostapd_prepare_rates(struct hostapd_data *hapd,
			  struct hostapd_hw_modes *mode)
{
	int i, num_basic_rates = 0;
	int basic_rates_a[] = { 60, 120, 240, -1 };
	int basic_rates_b[] = { 10, 20, -1 };
	int basic_rates_g[] = { 10, 20, 55, 110, -1 };
	int *basic_rates;

	if (hapd->iconf->basic_rates)
		basic_rates = hapd->iconf->basic_rates;
	else switch (mode->mode) {
	case HOSTAPD_MODE_IEEE80211A:
		basic_rates = basic_rates_a;
		break;
	case HOSTAPD_MODE_IEEE80211B:
		basic_rates = basic_rates_b;
		break;
	case HOSTAPD_MODE_IEEE80211G:
		basic_rates = basic_rates_g;
		break;
	default:
		return -1;
	}

	if (hostapd_set_rate_sets(hapd, hapd->iconf->supported_rates,
				  basic_rates, mode->mode)) {
		wpa_printf(MSG_ERROR, "Failed to update rate sets in kernel "
			   "module");
	}

	os_free(hapd->iface->current_rates);
	hapd->iface->num_rates = 0;

	hapd->iface->current_rates =
		os_zalloc(mode->num_rates * sizeof(struct hostapd_rate_data));
	if (!hapd->iface->current_rates) {
		wpa_printf(MSG_ERROR, "Failed to allocate memory for rate "
			   "table.");
		return -1;
	}

	for (i = 0; i < mode->num_rates; i++) {
		struct hostapd_rate_data *rate;

		if (hapd->iconf->supported_rates &&
		    !hostapd_rate_found(hapd->iconf->supported_rates,
					mode->rates[i]))
			continue;

		rate = &hapd->iface->current_rates[hapd->iface->num_rates];
		rate->rate = mode->rates[i];
		if (hostapd_rate_found(basic_rates, rate->rate)) {
			rate->flags |= HOSTAPD_RATE_BASIC;
			num_basic_rates++;
		}
		wpa_printf(MSG_DEBUG, "RATE[%d] rate=%d flags=0x%x",
			   hapd->iface->num_rates, rate->rate, rate->flags);
		hapd->iface->num_rates++;
	}

	if ((hapd->iface->num_rates == 0 || num_basic_rates == 0) &&
	    (!hapd->iconf->ieee80211n || !hapd->iconf->require_ht)) {
		wpa_printf(MSG_ERROR, "No rates remaining in supported/basic "
			   "rate sets (%d,%d).",
			   hapd->iface->num_rates, num_basic_rates);
		return -1;
	}

	return 0;
}


#ifdef CONFIG_IEEE80211N
static int ieee80211n_allowed_ht40_channel_pair(struct hostapd_iface *iface)
{
	int sec_chan, ok, j, first;
	int allowed[] = { 36, 44, 52, 60, 100, 108, 116, 124, 132, 149, 157,
			  184, 192 };
	size_t k;

	if (!iface->conf->secondary_channel)
		return 1; /* HT40 not used */

	sec_chan = iface->conf->channel + iface->conf->secondary_channel * 4;
	wpa_printf(MSG_DEBUG, "HT40: control channel: %d  "
		   "secondary channel: %d",
		   iface->conf->channel, sec_chan);

	/* Verify that HT40 secondary channel is an allowed 20 MHz
	 * channel */
	ok = 0;
	for (j = 0; j < iface->current_mode->num_channels; j++) {
		struct hostapd_channel_data *chan =
			&iface->current_mode->channels[j];
		if (!(chan->flag & HOSTAPD_CHAN_DISABLED) &&
		    chan->chan == sec_chan) {
			ok = 1;
			break;
		}
	}
	if (!ok) {
		wpa_printf(MSG_ERROR, "HT40 secondary channel %d not allowed",
			   sec_chan);
		return 0;
	}

	/*
	 * Verify that HT40 primary,secondary channel pair is allowed per
	 * IEEE 802.11n Annex J. This is only needed for 5 GHz band since
	 * 2.4 GHz rules allow all cases where the secondary channel fits into
	 * the list of allowed channels (already checked above).
	 */
	if (iface->current_mode->mode != HOSTAPD_MODE_IEEE80211A)
		return 1;

	if (iface->conf->secondary_channel > 0)
		first = iface->conf->channel;
	else
		first = sec_chan;

	ok = 0;
	for (k = 0; k < sizeof(allowed) / sizeof(allowed[0]); k++) {
		if (first == allowed[k]) {
			ok = 1;
			break;
		}
	}
	if (!ok) {
		wpa_printf(MSG_ERROR, "HT40 channel pair (%d, %d) not allowed",
			   iface->conf->channel,
			   iface->conf->secondary_channel);
		return 0;
	}

	return 1;
}


static void ieee80211n_switch_pri_sec(struct hostapd_iface *iface)
{
	if (iface->conf->secondary_channel > 0) {
		iface->conf->channel += 4;
		iface->conf->secondary_channel = -1;
	} else {
		iface->conf->channel -= 4;
		iface->conf->secondary_channel = 1;
	}
}


static void ieee80211n_get_pri_sec_chan(struct wpa_scan_res *bss,
					int *pri_chan, int *sec_chan)
{
	struct ieee80211_ht_operation *oper;
	struct ieee802_11_elems elems;

	*pri_chan = *sec_chan = 0;

	ieee802_11_parse_elems((u8 *) (bss + 1), bss->ie_len, &elems, 0);
	if (elems.ht_operation &&
	    elems.ht_operation_len >= sizeof(*oper)) {
		oper = (struct ieee80211_ht_operation *) elems.ht_operation;
		*pri_chan = oper->control_chan;
		if (oper->ht_param & HT_INFO_HT_PARAM_REC_TRANS_CHNL_WIDTH) {
			int sec = oper->ht_param &
				HT_INFO_HT_PARAM_SECONDARY_CHNL_OFF_MASK;
			if (sec == HT_INFO_HT_PARAM_SECONDARY_CHNL_ABOVE)
				*sec_chan = *pri_chan + 4;
			else if (sec == HT_INFO_HT_PARAM_SECONDARY_CHNL_BELOW)
				*sec_chan = *pri_chan - 4;
		}
	}
}


static int ieee80211n_check_40mhz_5g(struct hostapd_iface *iface,
				     struct wpa_scan_results *scan_res)
{
	int pri_chan, sec_chan, pri_freq, sec_freq, pri_bss, sec_bss;
	int bss_pri_chan, bss_sec_chan;
	size_t i;
	int match;

	pri_chan = iface->conf->channel;
	sec_chan = iface->conf->secondary_channel * 4;
	pri_freq = hostapd_hw_get_freq(iface->bss[0], pri_chan);
	if (iface->conf->secondary_channel > 0)
		sec_freq = pri_freq + 20;
	else
		sec_freq = pri_freq - 20;

	/*
	 * Switch PRI/SEC channels if Beacons were detected on selected SEC
	 * channel, but not on selected PRI channel.
	 */
	pri_bss = sec_bss = 0;
	for (i = 0; i < scan_res->num; i++) {
		struct wpa_scan_res *bss = scan_res->res[i];
		if (bss->freq == pri_freq)
			pri_bss++;
		else if (bss->freq == sec_freq)
			sec_bss++;
	}
	if (sec_bss && !pri_bss) {
		wpa_printf(MSG_INFO, "Switch own primary and secondary "
			   "channel to get secondary channel with no Beacons "
			   "from other BSSes");
		ieee80211n_switch_pri_sec(iface);
	}

	/*
	 * Match PRI/SEC channel with any existing HT40 BSS on the same
	 * channels that we are about to use (if already mixed order in
	 * existing BSSes, use own preference).
	 */
	match = 0;
	for (i = 0; i < scan_res->num; i++) {
		struct wpa_scan_res *bss = scan_res->res[i];
		ieee80211n_get_pri_sec_chan(bss, &bss_pri_chan, &bss_sec_chan);
		if (pri_chan == bss_pri_chan &&
		    sec_chan == bss_sec_chan) {
			match = 1;
			break;
		}
	}
	if (!match) {
		for (i = 0; i < scan_res->num; i++) {
			struct wpa_scan_res *bss = scan_res->res[i];
			ieee80211n_get_pri_sec_chan(bss, &bss_pri_chan,
						    &bss_sec_chan);
			if (pri_chan == bss_sec_chan &&
			    sec_chan == bss_pri_chan) {
				wpa_printf(MSG_INFO, "Switch own primary and "
					   "secondary channel due to BSS "
					   "overlap with " MACSTR,
					   MAC2STR(bss->bssid));
				ieee80211n_switch_pri_sec(iface);
				break;
			}
		}
	}

	return 1;
}


static int ieee80211n_check_40mhz_2g4(struct hostapd_iface *iface,
				      struct wpa_scan_results *scan_res)
{
	int pri_freq, sec_freq;
	int affected_start, affected_end;
	size_t i;

	pri_freq = hostapd_hw_get_freq(iface->bss[0], iface->conf->channel);
	if (iface->conf->secondary_channel > 0)
		sec_freq = pri_freq + 20;
	else
		sec_freq = pri_freq - 20;
	affected_start = (pri_freq + sec_freq) / 2 - 25;
	affected_end = (pri_freq + sec_freq) / 2 + 25;
	wpa_printf(MSG_DEBUG, "40 MHz affected channel range: [%d,%d] MHz",
		   affected_start, affected_end);
	for (i = 0; i < scan_res->num; i++) {
		struct wpa_scan_res *bss = scan_res->res[i];
		int pri = bss->freq;
		int sec = pri;
		int sec_chan, pri_chan;

		ieee80211n_get_pri_sec_chan(bss, &pri_chan, &sec_chan);

		if (sec_chan) {
			if (sec_chan < pri_chan)
				sec = pri - 20;
			else
				sec = pri + 20;
		}

		if ((pri < affected_start || pri > affected_end) &&
		    (sec < affected_start || sec > affected_end))
			continue; /* not within affected channel range */

		wpa_printf(MSG_DEBUG, "Neighboring BSS: " MACSTR
			   " freq=%d pri=%d sec=%d",
			   MAC2STR(bss->bssid), bss->freq, pri_chan, sec_chan);

		if (sec_chan) {
			if (pri_freq != pri || sec_freq != sec) {
				wpa_printf(MSG_DEBUG, "40 MHz pri/sec "
					   "mismatch with BSS " MACSTR
					   " <%d,%d> (chan=%d%c) vs. <%d,%d>",
					   MAC2STR(bss->bssid),
					   pri, sec, pri_chan,
					   sec > pri ? '+' : '-',
					   pri_freq, sec_freq);
				return 0;
			}
		}

		/* TODO: 40 MHz intolerant */
	}

	return 1;
}


static void wpa_scan_results_free(struct wpa_scan_results *res)
{
	size_t i;

	if (res == NULL)
		return;

	for (i = 0; i < res->num; i++)
		os_free(res->res[i]);
	os_free(res->res);
	os_free(res);
}


static void ieee80211n_check_scan(struct hostapd_iface *iface)
{
	struct wpa_scan_results *scan_res;
	int oper40;
	int res;

	/* Check list of neighboring BSSes (from scan) to see whether 40 MHz is
	 * allowed per IEEE 802.11n/D7.0, 11.14.3.2 */

	iface->scan_cb = NULL;

	scan_res = hostapd_driver_get_scan_results(iface->bss[0]);
	if (scan_res == NULL) {
		hostapd_setup_interface_complete(iface, 1);
		return;
	}

	if (iface->current_mode->mode == HOSTAPD_MODE_IEEE80211A)
		oper40 = ieee80211n_check_40mhz_5g(iface, scan_res);
	else
		oper40 = ieee80211n_check_40mhz_2g4(iface, scan_res);
	wpa_scan_results_free(scan_res);

	if (!oper40) {
		wpa_printf(MSG_INFO, "20/40 MHz operation not permitted on "
			   "channel pri=%d sec=%d based on overlapping BSSes",
			   iface->conf->channel,
			   iface->conf->channel +
			   iface->conf->secondary_channel * 4);
		iface->conf->secondary_channel = 0;
		iface->conf->ht_capab &= ~HT_CAP_INFO_SUPP_CHANNEL_WIDTH_SET;
	}

	res = ieee80211n_allowed_ht40_channel_pair(iface);
	hostapd_setup_interface_complete(iface, !res);
}


static int ieee80211n_check_40mhz(struct hostapd_iface *iface)
{
	struct wpa_driver_scan_params params;

	if (!iface->conf->secondary_channel)
		return 0; /* HT40 not used */

	wpa_printf(MSG_DEBUG, "Scan for neighboring BSSes prior to enabling "
		   "40 MHz channel");
	os_memset(&params, 0, sizeof(params));
	/* TODO: scan only the needed frequency */
	if (hostapd_driver_scan(iface->bss[0], &params) < 0) {
		wpa_printf(MSG_ERROR, "Failed to request a scan of "
			   "neighboring BSSes");

		//DRIVER_RTW Modify
		//return -1;		
		return 0;//ignore this error 
	}

	iface->scan_cb = ieee80211n_check_scan;
	return 1;
}


static int ieee80211n_supported_ht_capab(struct hostapd_iface *iface)
{
	u16 hw = iface->current_mode->ht_capab;
	u16 conf = iface->conf->ht_capab;

	if ((conf & HT_CAP_INFO_LDPC_CODING_CAP) &&
	    !(hw & HT_CAP_INFO_LDPC_CODING_CAP)) {
		wpa_printf(MSG_ERROR, "Driver does not support configured "
			   "HT capability [LDPC]");
		return 0;
	}

	if ((conf & HT_CAP_INFO_SUPP_CHANNEL_WIDTH_SET) &&
	    !(hw & HT_CAP_INFO_SUPP_CHANNEL_WIDTH_SET)) {
		wpa_printf(MSG_ERROR, "Driver does not support configured "
			   "HT capability [HT40*]");
		return 0;
	}

	if ((conf & HT_CAP_INFO_SMPS_MASK) != (hw & HT_CAP_INFO_SMPS_MASK) &&
	    (conf & HT_CAP_INFO_SMPS_MASK) != HT_CAP_INFO_SMPS_DISABLED) {
		wpa_printf(MSG_ERROR, "Driver does not support configured "
			   "HT capability [SMPS-*]");
		return 0;
	}

	if ((conf & HT_CAP_INFO_GREEN_FIELD) &&
	    !(hw & HT_CAP_INFO_GREEN_FIELD)) {
		wpa_printf(MSG_ERROR, "Driver does not support configured "
			   "HT capability [GF]");
		return 0;
	}

	if ((conf & HT_CAP_INFO_SHORT_GI20MHZ) &&
	    !(hw & HT_CAP_INFO_SHORT_GI20MHZ)) {
		wpa_printf(MSG_ERROR, "Driver does not support configured "
			   "HT capability [SHORT-GI-20]");
		return 0;
	}

	if ((conf & HT_CAP_INFO_SHORT_GI40MHZ) &&
	    !(hw & HT_CAP_INFO_SHORT_GI40MHZ)) {
		wpa_printf(MSG_ERROR, "Driver does not support configured "
			   "HT capability [SHORT-GI-40]");
		return 0;
	}

	if ((conf & HT_CAP_INFO_TX_STBC) && !(hw & HT_CAP_INFO_TX_STBC)) {
		wpa_printf(MSG_ERROR, "Driver does not support configured "
			   "HT capability [TX-STBC]");
		return 0;
	}

	if ((conf & HT_CAP_INFO_RX_STBC_MASK) >
	    (hw & HT_CAP_INFO_RX_STBC_MASK)) {
		wpa_printf(MSG_ERROR, "Driver does not support configured "
			   "HT capability [RX-STBC*]");
		return 0;
	}

	if ((conf & HT_CAP_INFO_DELAYED_BA) &&
	    !(hw & HT_CAP_INFO_DELAYED_BA)) {
		wpa_printf(MSG_ERROR, "Driver does not support configured "
			   "HT capability [DELAYED-BA]");
		return 0;
	}

	if ((conf & HT_CAP_INFO_MAX_AMSDU_SIZE) &&
	    !(hw & HT_CAP_INFO_MAX_AMSDU_SIZE)) {
		wpa_printf(MSG_ERROR, "Driver does not support configured "
			   "HT capability [MAX-AMSDU-7935]");
		return 0;
	}

	if ((conf & HT_CAP_INFO_DSSS_CCK40MHZ) &&
	    !(hw & HT_CAP_INFO_DSSS_CCK40MHZ)) {
		wpa_printf(MSG_ERROR, "Driver does not support configured "
			   "HT capability [DSSS_CCK-40]");
		return 0;
	}

	if ((conf & HT_CAP_INFO_PSMP_SUPP) && !(hw & HT_CAP_INFO_PSMP_SUPP)) {
		wpa_printf(MSG_ERROR, "Driver does not support configured "
			   "HT capability [PSMP]");
		return 0;
	}

	if ((conf & HT_CAP_INFO_LSIG_TXOP_PROTECT_SUPPORT) &&
	    !(hw & HT_CAP_INFO_LSIG_TXOP_PROTECT_SUPPORT)) {
		wpa_printf(MSG_ERROR, "Driver does not support configured "
			   "HT capability [LSIG-TXOP-PROT]");
		return 0;
	}

	return 1;
}

#endif /* CONFIG_IEEE80211N */


int hostapd_check_ht_capab(struct hostapd_iface *iface)
{
#ifdef CONFIG_IEEE80211N
	int ret;
	if (!iface->conf->ieee80211n)
		return 0;
	if (!ieee80211n_supported_ht_capab(iface))
		return -1;
	ret = ieee80211n_check_40mhz(iface);
	if (ret)
		return ret;
	if (!ieee80211n_allowed_ht40_channel_pair(iface))
		return -1;
#endif /* CONFIG_IEEE80211N */

	return 0;
}


/**
 * hostapd_select_hw_mode - Select the hardware mode
 * @iface: Pointer to interface data.
 * Returns: 0 on success, -1 on failure
 *
 * Sets up the hardware mode, channel, rates, and passive scanning
 * based on the configuration.
 */
int hostapd_select_hw_mode(struct hostapd_iface *iface)
{
	int i, j, ok;

	if (iface->num_hw_features < 1)
		return -1;

	iface->current_mode = NULL;
	for (i = 0; i < iface->num_hw_features; i++) {
		struct hostapd_hw_modes *mode = &iface->hw_features[i];
		if (mode->mode == iface->conf->hw_mode) {
			iface->current_mode = mode;
			break;
		}
	}

	if (iface->current_mode == NULL) {
		wpa_printf(MSG_ERROR, "Hardware does not support configured "
			   "mode");
		hostapd_logger(iface->bss[0], NULL, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_WARNING,
			       "Hardware does not support configured mode "
			       "(%d)", (int) iface->conf->hw_mode);
		return -1;
	}

	ok = 0;
	for (j = 0; j < iface->current_mode->num_channels; j++) {
		struct hostapd_channel_data *chan =
			&iface->current_mode->channels[j];
		if (!(chan->flag & HOSTAPD_CHAN_DISABLED) &&
		    (chan->chan == iface->conf->channel)) {
			ok = 1;
			break;
		}
	}
	if (ok && iface->conf->secondary_channel) {
		int sec_ok = 0;
		int sec_chan = iface->conf->channel +
			iface->conf->secondary_channel * 4;
		for (j = 0; j < iface->current_mode->num_channels; j++) {
			struct hostapd_channel_data *chan =
				&iface->current_mode->channels[j];
			if (!(chan->flag & HOSTAPD_CHAN_DISABLED) &&
			    (chan->chan == sec_chan)) {
				sec_ok = 1;
				break;
			}
		}
		if (!sec_ok) {
			hostapd_logger(iface->bss[0], NULL,
				       HOSTAPD_MODULE_IEEE80211,
				       HOSTAPD_LEVEL_WARNING,
				       "Configured HT40 secondary channel "
				       "(%d) not found from the channel list "
				       "of current mode (%d) %s",
				       sec_chan, iface->current_mode->mode,
				       hostapd_hw_mode_txt(
					       iface->current_mode->mode));
			ok = 0;
		}
	}
	if (iface->conf->channel == 0) {
		/* TODO: could request a scan of neighboring BSSes and select
		 * the channel automatically */
		wpa_printf(MSG_ERROR, "Channel not configured "
			   "(hw_mode/channel in hostapd.conf)");
		return -1;
	}
	if (ok == 0 && iface->conf->channel != 0) {
		hostapd_logger(iface->bss[0], NULL,
			       HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_WARNING,
			       "Configured channel (%d) not found from the "
			       "channel list of current mode (%d) %s",
			       iface->conf->channel,
			       iface->current_mode->mode,
			       hostapd_hw_mode_txt(iface->current_mode->mode));
		iface->current_mode = NULL;
	}

	if (iface->current_mode == NULL) {
		hostapd_logger(iface->bss[0], NULL, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_WARNING,
			       "Hardware does not support configured channel");
		return -1;
	}

	return 0;
}


const char * hostapd_hw_mode_txt(int mode)
{
	switch (mode) {
	case HOSTAPD_MODE_IEEE80211A:
		return "IEEE 802.11a";
	case HOSTAPD_MODE_IEEE80211B:
		return "IEEE 802.11b";
	case HOSTAPD_MODE_IEEE80211G:
		return "IEEE 802.11g";
	default:
		return "UNKNOWN";
	}
}


int hostapd_hw_get_freq(struct hostapd_data *hapd, int chan)
{
	int i;

	if (!hapd->iface->current_mode)
		return 0;

	for (i = 0; i < hapd->iface->current_mode->num_channels; i++) {
		struct hostapd_channel_data *ch =
			&hapd->iface->current_mode->channels[i];
		if (ch->chan == chan)
			return ch->freq;
	}

	return 0;
}


int hostapd_hw_get_channel(struct hostapd_data *hapd, int freq)
{
	int i;

	if (!hapd->iface->current_mode)
		return 0;

	for (i = 0; i < hapd->iface->current_mode->num_channels; i++) {
		struct hostapd_channel_data *ch =
			&hapd->iface->current_mode->channels[i];
		if (ch->freq == freq)
			return ch->chan;
	}

	return 0;
}
