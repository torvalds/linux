/*
 * Common hostapd/wpa_supplicant HW features
 * Copyright (c) 2002-2013, Jouni Malinen <j@w1.fi>
 * Copyright (c) 2015, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "defs.h"
#include "ieee802_11_defs.h"
#include "ieee802_11_common.h"
#include "hw_features_common.h"


struct hostapd_channel_data * hw_get_channel_chan(struct hostapd_hw_modes *mode,
						  int chan, int *freq)
{
	int i;

	if (freq)
		*freq = 0;

	if (!mode)
		return NULL;

	for (i = 0; i < mode->num_channels; i++) {
		struct hostapd_channel_data *ch = &mode->channels[i];
		if (ch->chan == chan) {
			if (freq)
				*freq = ch->freq;
			return ch;
		}
	}

	return NULL;
}


struct hostapd_channel_data * hw_get_channel_freq(struct hostapd_hw_modes *mode,
						  int freq, int *chan)
{
	int i;

	if (chan)
		*chan = 0;

	if (!mode)
		return NULL;

	for (i = 0; i < mode->num_channels; i++) {
		struct hostapd_channel_data *ch = &mode->channels[i];
		if (ch->freq == freq) {
			if (chan)
				*chan = ch->chan;
			return ch;
		}
	}

	return NULL;
}


int hw_get_freq(struct hostapd_hw_modes *mode, int chan)
{
	int freq;

	hw_get_channel_chan(mode, chan, &freq);

	return freq;
}


int hw_get_chan(struct hostapd_hw_modes *mode, int freq)
{
	int chan;

	hw_get_channel_freq(mode, freq, &chan);

	return chan;
}


int allowed_ht40_channel_pair(struct hostapd_hw_modes *mode, int pri_chan,
			      int sec_chan)
{
	int ok, j, first;
	int allowed[] = { 36, 44, 52, 60, 100, 108, 116, 124, 132, 140,
			  149, 157, 165, 184, 192 };
	size_t k;

	if (pri_chan == sec_chan || !sec_chan)
		return 1; /* HT40 not used */

	wpa_printf(MSG_DEBUG,
		   "HT40: control channel: %d  secondary channel: %d",
		   pri_chan, sec_chan);

	/* Verify that HT40 secondary channel is an allowed 20 MHz
	 * channel */
	ok = 0;
	for (j = 0; j < mode->num_channels; j++) {
		struct hostapd_channel_data *chan = &mode->channels[j];
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
	if (mode->mode != HOSTAPD_MODE_IEEE80211A)
		return 1;

	first = pri_chan < sec_chan ? pri_chan : sec_chan;

	ok = 0;
	for (k = 0; k < ARRAY_SIZE(allowed); k++) {
		if (first == allowed[k]) {
			ok = 1;
			break;
		}
	}
	if (!ok) {
		wpa_printf(MSG_ERROR, "HT40 channel pair (%d, %d) not allowed",
			   pri_chan, sec_chan);
		return 0;
	}

	return 1;
}


void get_pri_sec_chan(struct wpa_scan_res *bss, int *pri_chan, int *sec_chan)
{
	struct ieee80211_ht_operation *oper;
	struct ieee802_11_elems elems;

	*pri_chan = *sec_chan = 0;

	ieee802_11_parse_elems((u8 *) (bss + 1), bss->ie_len, &elems, 0);
	if (elems.ht_operation) {
		oper = (struct ieee80211_ht_operation *) elems.ht_operation;
		*pri_chan = oper->primary_chan;
		if (oper->ht_param & HT_INFO_HT_PARAM_STA_CHNL_WIDTH) {
			int sec = oper->ht_param &
				HT_INFO_HT_PARAM_SECONDARY_CHNL_OFF_MASK;
			if (sec == HT_INFO_HT_PARAM_SECONDARY_CHNL_ABOVE)
				*sec_chan = *pri_chan + 4;
			else if (sec == HT_INFO_HT_PARAM_SECONDARY_CHNL_BELOW)
				*sec_chan = *pri_chan - 4;
		}
	}
}


int check_40mhz_5g(struct hostapd_hw_modes *mode,
		   struct wpa_scan_results *scan_res, int pri_chan,
		   int sec_chan)
{
	int pri_freq, sec_freq, pri_bss, sec_bss;
	int bss_pri_chan, bss_sec_chan;
	size_t i;
	int match;

	if (!mode || !scan_res || !pri_chan || !sec_chan ||
	    pri_chan == sec_chan)
		return 0;

	pri_freq = hw_get_freq(mode, pri_chan);
	sec_freq = hw_get_freq(mode, sec_chan);

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
		wpa_printf(MSG_INFO,
			   "Switch own primary and secondary channel to get secondary channel with no Beacons from other BSSes");
		return 2;
	}

	/*
	 * Match PRI/SEC channel with any existing HT40 BSS on the same
	 * channels that we are about to use (if already mixed order in
	 * existing BSSes, use own preference).
	 */
	match = 0;
	for (i = 0; i < scan_res->num; i++) {
		struct wpa_scan_res *bss = scan_res->res[i];
		get_pri_sec_chan(bss, &bss_pri_chan, &bss_sec_chan);
		if (pri_chan == bss_pri_chan &&
		    sec_chan == bss_sec_chan) {
			match = 1;
			break;
		}
	}
	if (!match) {
		for (i = 0; i < scan_res->num; i++) {
			struct wpa_scan_res *bss = scan_res->res[i];
			get_pri_sec_chan(bss, &bss_pri_chan, &bss_sec_chan);
			if (pri_chan == bss_sec_chan &&
			    sec_chan == bss_pri_chan) {
				wpa_printf(MSG_INFO, "Switch own primary and "
					   "secondary channel due to BSS "
					   "overlap with " MACSTR,
					   MAC2STR(bss->bssid));
				return 2;
			}
		}
	}

	return 1;
}


static int check_20mhz_bss(struct wpa_scan_res *bss, int pri_freq, int start,
			   int end)
{
	struct ieee802_11_elems elems;
	struct ieee80211_ht_operation *oper;

	if (bss->freq < start || bss->freq > end || bss->freq == pri_freq)
		return 0;

	ieee802_11_parse_elems((u8 *) (bss + 1), bss->ie_len, &elems, 0);
	if (!elems.ht_capabilities) {
		wpa_printf(MSG_DEBUG, "Found overlapping legacy BSS: "
			   MACSTR " freq=%d", MAC2STR(bss->bssid), bss->freq);
		return 1;
	}

	if (elems.ht_operation) {
		oper = (struct ieee80211_ht_operation *) elems.ht_operation;
		if (oper->ht_param & HT_INFO_HT_PARAM_SECONDARY_CHNL_OFF_MASK)
			return 0;

		wpa_printf(MSG_DEBUG, "Found overlapping 20 MHz HT BSS: "
			   MACSTR " freq=%d", MAC2STR(bss->bssid), bss->freq);
		return 1;
	}
	return 0;
}


int check_40mhz_2g4(struct hostapd_hw_modes *mode,
		    struct wpa_scan_results *scan_res, int pri_chan,
		    int sec_chan)
{
	int pri_freq, sec_freq;
	int affected_start, affected_end;
	size_t i;

	if (!mode || !scan_res || !pri_chan || !sec_chan ||
	    pri_chan == sec_chan)
		return 0;

	pri_freq = hw_get_freq(mode, pri_chan);
	sec_freq = hw_get_freq(mode, sec_chan);

	affected_start = (pri_freq + sec_freq) / 2 - 25;
	affected_end = (pri_freq + sec_freq) / 2 + 25;
	wpa_printf(MSG_DEBUG, "40 MHz affected channel range: [%d,%d] MHz",
		   affected_start, affected_end);
	for (i = 0; i < scan_res->num; i++) {
		struct wpa_scan_res *bss = scan_res->res[i];
		int pri = bss->freq;
		int sec = pri;
		struct ieee802_11_elems elems;

		/* Check for overlapping 20 MHz BSS */
		if (check_20mhz_bss(bss, pri_freq, affected_start,
				    affected_end)) {
			wpa_printf(MSG_DEBUG,
				   "Overlapping 20 MHz BSS is found");
			return 0;
		}

		get_pri_sec_chan(bss, &pri_chan, &sec_chan);

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
				wpa_printf(MSG_DEBUG,
					   "40 MHz pri/sec mismatch with BSS "
					   MACSTR
					   " <%d,%d> (chan=%d%c) vs. <%d,%d>",
					   MAC2STR(bss->bssid),
					   pri, sec, pri_chan,
					   sec > pri ? '+' : '-',
					   pri_freq, sec_freq);
				return 0;
			}
		}

		ieee802_11_parse_elems((u8 *) (bss + 1), bss->ie_len, &elems,
				       0);
		if (elems.ht_capabilities) {
			struct ieee80211_ht_capabilities *ht_cap =
				(struct ieee80211_ht_capabilities *)
				elems.ht_capabilities;

			if (le_to_host16(ht_cap->ht_capabilities_info) &
			    HT_CAP_INFO_40MHZ_INTOLERANT) {
				wpa_printf(MSG_DEBUG,
					   "40 MHz Intolerant is set on channel %d in BSS "
					   MACSTR, pri, MAC2STR(bss->bssid));
				return 0;
			}
		}
	}

	return 1;
}


int hostapd_set_freq_params(struct hostapd_freq_params *data,
			    enum hostapd_hw_mode mode,
			    int freq, int channel, int ht_enabled,
			    int vht_enabled, int sec_channel_offset,
			    int vht_oper_chwidth, int center_segment0,
			    int center_segment1, u32 vht_caps)
{
	os_memset(data, 0, sizeof(*data));
	data->mode = mode;
	data->freq = freq;
	data->channel = channel;
	data->ht_enabled = ht_enabled;
	data->vht_enabled = vht_enabled;
	data->sec_channel_offset = sec_channel_offset;
	data->center_freq1 = freq + sec_channel_offset * 10;
	data->center_freq2 = 0;
	data->bandwidth = sec_channel_offset ? 40 : 20;

	if (data->vht_enabled) switch (vht_oper_chwidth) {
	case VHT_CHANWIDTH_USE_HT:
		if (center_segment1 ||
		    (center_segment0 != 0 &&
		     5000 + center_segment0 * 5 != data->center_freq1 &&
		     2407 + center_segment0 * 5 != data->center_freq1))
			return -1;
		break;
	case VHT_CHANWIDTH_80P80MHZ:
		if (!(vht_caps & VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ)) {
			wpa_printf(MSG_ERROR,
				   "80+80 channel width is not supported!");
			return -1;
		}
		if (center_segment1 == center_segment0 + 4 ||
		    center_segment1 == center_segment0 - 4)
			return -1;
		data->center_freq2 = 5000 + center_segment1 * 5;
		/* fall through */
	case VHT_CHANWIDTH_80MHZ:
		data->bandwidth = 80;
		if ((vht_oper_chwidth == VHT_CHANWIDTH_80MHZ &&
		     center_segment1) ||
		    (vht_oper_chwidth == VHT_CHANWIDTH_80P80MHZ &&
		     !center_segment1) ||
		    !sec_channel_offset)
			return -1;
		if (!center_segment0) {
			if (channel <= 48)
				center_segment0 = 42;
			else if (channel <= 64)
				center_segment0 = 58;
			else if (channel <= 112)
				center_segment0 = 106;
			else if (channel <= 128)
				center_segment0 = 122;
			else if (channel <= 144)
				center_segment0 = 138;
			else if (channel <= 161)
				center_segment0 = 155;
			data->center_freq1 = 5000 + center_segment0 * 5;
		} else {
			/*
			 * Note: HT/VHT config and params are coupled. Check if
			 * HT40 channel band is in VHT80 Pri channel band
			 * configuration.
			 */
			if (center_segment0 == channel + 6 ||
			    center_segment0 == channel + 2 ||
			    center_segment0 == channel - 2 ||
			    center_segment0 == channel - 6)
				data->center_freq1 = 5000 + center_segment0 * 5;
			else
				return -1;
		}
		break;
	case VHT_CHANWIDTH_160MHZ:
		data->bandwidth = 160;
		if (!(vht_caps & (VHT_CAP_SUPP_CHAN_WIDTH_160MHZ |
				  VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ))) {
			wpa_printf(MSG_ERROR,
				   "160MHZ channel width is not supported!");
			return -1;
		}
		if (center_segment1)
			return -1;
		if (!sec_channel_offset)
			return -1;
		/*
		 * Note: HT/VHT config and params are coupled. Check if
		 * HT40 channel band is in VHT160 channel band configuration.
		 */
		if (center_segment0 == channel + 14 ||
		    center_segment0 == channel + 10 ||
		    center_segment0 == channel + 6 ||
		    center_segment0 == channel + 2 ||
		    center_segment0 == channel - 2 ||
		    center_segment0 == channel - 6 ||
		    center_segment0 == channel - 10 ||
		    center_segment0 == channel - 14)
			data->center_freq1 = 5000 + center_segment0 * 5;
		else
			return -1;
		break;
	}

	return 0;
}


void set_disable_ht40(struct ieee80211_ht_capabilities *htcaps,
		      int disabled)
{
	/* Masking these out disables HT40 */
	le16 msk = host_to_le16(HT_CAP_INFO_SUPP_CHANNEL_WIDTH_SET |
				HT_CAP_INFO_SHORT_GI40MHZ);

	if (disabled)
		htcaps->ht_capabilities_info &= ~msk;
	else
		htcaps->ht_capabilities_info |= msk;
}


#ifdef CONFIG_IEEE80211AC

static int _ieee80211ac_cap_check(u32 hw, u32 conf, u32 cap,
				  const char *name)
{
	u32 req_cap = conf & cap;

	/*
	 * Make sure we support all requested capabilities.
	 * NOTE: We assume that 'cap' represents a capability mask,
	 * not a discrete value.
	 */
	if ((hw & req_cap) != req_cap) {
		wpa_printf(MSG_ERROR,
			   "Driver does not support configured VHT capability [%s]",
			   name);
		return 0;
	}
	return 1;
}


static int ieee80211ac_cap_check_max(u32 hw, u32 conf, u32 mask,
				     unsigned int shift,
				     const char *name)
{
	u32 hw_max = hw & mask;
	u32 conf_val = conf & mask;

	if (conf_val > hw_max) {
		wpa_printf(MSG_ERROR,
			   "Configured VHT capability [%s] exceeds max value supported by the driver (%d > %d)",
			   name, conf_val >> shift, hw_max >> shift);
		return 0;
	}
	return 1;
}


int ieee80211ac_cap_check(u32 hw, u32 conf)
{
#define VHT_CAP_CHECK(cap) \
	do { \
		if (!_ieee80211ac_cap_check(hw, conf, cap, #cap)) \
			return 0; \
	} while (0)

#define VHT_CAP_CHECK_MAX(cap) \
	do { \
		if (!ieee80211ac_cap_check_max(hw, conf, cap, cap ## _SHIFT, \
					       #cap)) \
			return 0; \
	} while (0)

	VHT_CAP_CHECK_MAX(VHT_CAP_MAX_MPDU_LENGTH_MASK);
	VHT_CAP_CHECK_MAX(VHT_CAP_SUPP_CHAN_WIDTH_MASK);
	VHT_CAP_CHECK(VHT_CAP_RXLDPC);
	VHT_CAP_CHECK(VHT_CAP_SHORT_GI_80);
	VHT_CAP_CHECK(VHT_CAP_SHORT_GI_160);
	VHT_CAP_CHECK(VHT_CAP_TXSTBC);
	VHT_CAP_CHECK_MAX(VHT_CAP_RXSTBC_MASK);
	VHT_CAP_CHECK(VHT_CAP_SU_BEAMFORMER_CAPABLE);
	VHT_CAP_CHECK(VHT_CAP_SU_BEAMFORMEE_CAPABLE);
	VHT_CAP_CHECK_MAX(VHT_CAP_BEAMFORMEE_STS_MAX);
	VHT_CAP_CHECK_MAX(VHT_CAP_SOUNDING_DIMENSION_MAX);
	VHT_CAP_CHECK(VHT_CAP_MU_BEAMFORMER_CAPABLE);
	VHT_CAP_CHECK(VHT_CAP_MU_BEAMFORMEE_CAPABLE);
	VHT_CAP_CHECK(VHT_CAP_VHT_TXOP_PS);
	VHT_CAP_CHECK(VHT_CAP_HTC_VHT);
	VHT_CAP_CHECK_MAX(VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MAX);
	VHT_CAP_CHECK(VHT_CAP_VHT_LINK_ADAPTATION_VHT_UNSOL_MFB);
	VHT_CAP_CHECK(VHT_CAP_VHT_LINK_ADAPTATION_VHT_MRQ_MFB);
	VHT_CAP_CHECK(VHT_CAP_RX_ANTENNA_PATTERN);
	VHT_CAP_CHECK(VHT_CAP_TX_ANTENNA_PATTERN);

#undef VHT_CAP_CHECK
#undef VHT_CAP_CHECK_MAX

	return 1;
}

#endif /* CONFIG_IEEE80211AC */
