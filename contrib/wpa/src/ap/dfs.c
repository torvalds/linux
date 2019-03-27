/*
 * DFS - Dynamic Frequency Selection
 * Copyright (c) 2002-2013, Jouni Malinen <j@w1.fi>
 * Copyright (c) 2013-2017, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "common/ieee802_11_defs.h"
#include "common/hw_features_common.h"
#include "common/wpa_ctrl.h"
#include "hostapd.h"
#include "ap_drv_ops.h"
#include "drivers/driver.h"
#include "dfs.h"


static int dfs_get_used_n_chans(struct hostapd_iface *iface, int *seg1)
{
	int n_chans = 1;

	*seg1 = 0;

	if (iface->conf->ieee80211n && iface->conf->secondary_channel)
		n_chans = 2;

	if (iface->conf->ieee80211ac) {
		switch (iface->conf->vht_oper_chwidth) {
		case VHT_CHANWIDTH_USE_HT:
			break;
		case VHT_CHANWIDTH_80MHZ:
			n_chans = 4;
			break;
		case VHT_CHANWIDTH_160MHZ:
			n_chans = 8;
			break;
		case VHT_CHANWIDTH_80P80MHZ:
			n_chans = 4;
			*seg1 = 4;
			break;
		default:
			break;
		}
	}

	return n_chans;
}


static int dfs_channel_available(struct hostapd_channel_data *chan,
				 int skip_radar)
{
	/*
	 * When radar detection happens, CSA is performed. However, there's no
	 * time for CAC, so radar channels must be skipped when finding a new
	 * channel for CSA, unless they are available for immediate use.
	 */
	if (skip_radar && (chan->flag & HOSTAPD_CHAN_RADAR) &&
	    ((chan->flag & HOSTAPD_CHAN_DFS_MASK) !=
	     HOSTAPD_CHAN_DFS_AVAILABLE))
		return 0;

	if (chan->flag & HOSTAPD_CHAN_DISABLED)
		return 0;
	if ((chan->flag & HOSTAPD_CHAN_RADAR) &&
	    ((chan->flag & HOSTAPD_CHAN_DFS_MASK) ==
	     HOSTAPD_CHAN_DFS_UNAVAILABLE))
		return 0;
	return 1;
}


static int dfs_is_chan_allowed(struct hostapd_channel_data *chan, int n_chans)
{
	/*
	 * The tables contain first valid channel number based on channel width.
	 * We will also choose this first channel as the control one.
	 */
	int allowed_40[] = { 36, 44, 52, 60, 100, 108, 116, 124, 132, 149, 157,
			     184, 192 };
	/*
	 * VHT80, valid channels based on center frequency:
	 * 42, 58, 106, 122, 138, 155
	 */
	int allowed_80[] = { 36, 52, 100, 116, 132, 149 };
	/*
	 * VHT160 valid channels based on center frequency:
	 * 50, 114
	 */
	int allowed_160[] = { 36, 100 };
	int *allowed = allowed_40;
	unsigned int i, allowed_no = 0;

	switch (n_chans) {
	case 2:
		allowed = allowed_40;
		allowed_no = ARRAY_SIZE(allowed_40);
		break;
	case 4:
		allowed = allowed_80;
		allowed_no = ARRAY_SIZE(allowed_80);
		break;
	case 8:
		allowed = allowed_160;
		allowed_no = ARRAY_SIZE(allowed_160);
		break;
	default:
		wpa_printf(MSG_DEBUG, "Unknown width for %d channels", n_chans);
		break;
	}

	for (i = 0; i < allowed_no; i++) {
		if (chan->chan == allowed[i])
			return 1;
	}

	return 0;
}


static struct hostapd_channel_data *
dfs_get_chan_data(struct hostapd_hw_modes *mode, int freq, int first_chan_idx)
{
	int i;

	for (i = first_chan_idx; i < mode->num_channels; i++) {
		if (mode->channels[i].freq == freq)
			return &mode->channels[i];
	}

	return NULL;
}


static int dfs_chan_range_available(struct hostapd_hw_modes *mode,
				    int first_chan_idx, int num_chans,
				    int skip_radar)
{
	struct hostapd_channel_data *first_chan, *chan;
	int i;

	if (first_chan_idx + num_chans > mode->num_channels)
		return 0;

	first_chan = &mode->channels[first_chan_idx];

	for (i = 0; i < num_chans; i++) {
		chan = dfs_get_chan_data(mode, first_chan->freq + i * 20,
					 first_chan_idx);
		if (!chan)
			return 0;

		if (!dfs_channel_available(chan, skip_radar))
			return 0;
	}

	return 1;
}


static int is_in_chanlist(struct hostapd_iface *iface,
			  struct hostapd_channel_data *chan)
{
	if (!iface->conf->acs_ch_list.num)
		return 1;

	return freq_range_list_includes(&iface->conf->acs_ch_list, chan->chan);
}


/*
 * The function assumes HT40+ operation.
 * Make sure to adjust the following variables after calling this:
 *  - hapd->secondary_channel
 *  - hapd->vht_oper_centr_freq_seg0_idx
 *  - hapd->vht_oper_centr_freq_seg1_idx
 */
static int dfs_find_channel(struct hostapd_iface *iface,
			    struct hostapd_channel_data **ret_chan,
			    int idx, int skip_radar)
{
	struct hostapd_hw_modes *mode;
	struct hostapd_channel_data *chan;
	int i, channel_idx = 0, n_chans, n_chans1;

	mode = iface->current_mode;
	n_chans = dfs_get_used_n_chans(iface, &n_chans1);

	wpa_printf(MSG_DEBUG, "DFS new chan checking %d channels", n_chans);
	for (i = 0; i < mode->num_channels; i++) {
		chan = &mode->channels[i];

		/* Skip HT40/VHT incompatible channels */
		if (iface->conf->ieee80211n &&
		    iface->conf->secondary_channel &&
		    !dfs_is_chan_allowed(chan, n_chans))
			continue;

		/* Skip incompatible chandefs */
		if (!dfs_chan_range_available(mode, i, n_chans, skip_radar))
			continue;

		if (!is_in_chanlist(iface, chan))
			continue;

		if (ret_chan && idx == channel_idx) {
			wpa_printf(MSG_DEBUG, "Selected ch. #%d", chan->chan);
			*ret_chan = chan;
			return idx;
		}
		wpa_printf(MSG_DEBUG, "Adding channel: %d", chan->chan);
		channel_idx++;
	}
	return channel_idx;
}


static void dfs_adjust_vht_center_freq(struct hostapd_iface *iface,
				       struct hostapd_channel_data *chan,
				       int secondary_channel,
				       u8 *vht_oper_centr_freq_seg0_idx,
				       u8 *vht_oper_centr_freq_seg1_idx)
{
	if (!iface->conf->ieee80211ac)
		return;

	if (!chan)
		return;

	*vht_oper_centr_freq_seg1_idx = 0;

	switch (iface->conf->vht_oper_chwidth) {
	case VHT_CHANWIDTH_USE_HT:
		if (secondary_channel == 1)
			*vht_oper_centr_freq_seg0_idx = chan->chan + 2;
		else if (secondary_channel == -1)
			*vht_oper_centr_freq_seg0_idx = chan->chan - 2;
		else
			*vht_oper_centr_freq_seg0_idx = chan->chan;
		break;
	case VHT_CHANWIDTH_80MHZ:
		*vht_oper_centr_freq_seg0_idx = chan->chan + 6;
		break;
	case VHT_CHANWIDTH_160MHZ:
		*vht_oper_centr_freq_seg0_idx = chan->chan + 14;
		break;
	default:
		wpa_printf(MSG_INFO, "DFS only VHT20/40/80/160 is supported now");
		*vht_oper_centr_freq_seg0_idx = 0;
		break;
	}

	wpa_printf(MSG_DEBUG, "DFS adjusting VHT center frequency: %d, %d",
		   *vht_oper_centr_freq_seg0_idx,
		   *vht_oper_centr_freq_seg1_idx);
}


/* Return start channel idx we will use for mode->channels[idx] */
static int dfs_get_start_chan_idx(struct hostapd_iface *iface, int *seg1_start)
{
	struct hostapd_hw_modes *mode;
	struct hostapd_channel_data *chan;
	int channel_no = iface->conf->channel;
	int res = -1, i;
	int chan_seg1 = -1;

	*seg1_start = -1;

	/* HT40- */
	if (iface->conf->ieee80211n && iface->conf->secondary_channel == -1)
		channel_no -= 4;

	/* VHT */
	if (iface->conf->ieee80211ac) {
		switch (iface->conf->vht_oper_chwidth) {
		case VHT_CHANWIDTH_USE_HT:
			break;
		case VHT_CHANWIDTH_80MHZ:
			channel_no =
				iface->conf->vht_oper_centr_freq_seg0_idx - 6;
			break;
		case VHT_CHANWIDTH_160MHZ:
			channel_no =
				iface->conf->vht_oper_centr_freq_seg0_idx - 14;
			break;
		case VHT_CHANWIDTH_80P80MHZ:
			channel_no =
				iface->conf->vht_oper_centr_freq_seg0_idx - 6;
			chan_seg1 =
				iface->conf->vht_oper_centr_freq_seg1_idx - 6;
			break;
		default:
			wpa_printf(MSG_INFO,
				   "DFS only VHT20/40/80/160/80+80 is supported now");
			channel_no = -1;
			break;
		}
	}

	/* Get idx */
	mode = iface->current_mode;
	for (i = 0; i < mode->num_channels; i++) {
		chan = &mode->channels[i];
		if (chan->chan == channel_no) {
			res = i;
			break;
		}
	}

	if (res != -1 && chan_seg1 > -1) {
		int found = 0;

		/* Get idx for seg1 */
		mode = iface->current_mode;
		for (i = 0; i < mode->num_channels; i++) {
			chan = &mode->channels[i];
			if (chan->chan == chan_seg1) {
				*seg1_start = i;
				found = 1;
				break;
			}
		}
		if (!found)
			res = -1;
	}

	if (res == -1) {
		wpa_printf(MSG_DEBUG,
			   "DFS chan_idx seems wrong; num-ch: %d ch-no: %d conf-ch-no: %d 11n: %d sec-ch: %d vht-oper-width: %d",
			   mode->num_channels, channel_no, iface->conf->channel,
			   iface->conf->ieee80211n,
			   iface->conf->secondary_channel,
			   iface->conf->vht_oper_chwidth);

		for (i = 0; i < mode->num_channels; i++) {
			wpa_printf(MSG_DEBUG, "Available channel: %d",
				   mode->channels[i].chan);
		}
	}

	return res;
}


/* At least one channel have radar flag */
static int dfs_check_chans_radar(struct hostapd_iface *iface,
				 int start_chan_idx, int n_chans)
{
	struct hostapd_channel_data *channel;
	struct hostapd_hw_modes *mode;
	int i, res = 0;

	mode = iface->current_mode;

	for (i = 0; i < n_chans; i++) {
		channel = &mode->channels[start_chan_idx + i];
		if (channel->flag & HOSTAPD_CHAN_RADAR)
			res++;
	}

	return res;
}


/* All channels available */
static int dfs_check_chans_available(struct hostapd_iface *iface,
				     int start_chan_idx, int n_chans)
{
	struct hostapd_channel_data *channel;
	struct hostapd_hw_modes *mode;
	int i;

	mode = iface->current_mode;

	for (i = 0; i < n_chans; i++) {
		channel = &mode->channels[start_chan_idx + i];

		if (channel->flag & HOSTAPD_CHAN_DISABLED)
			break;

		if (!(channel->flag & HOSTAPD_CHAN_RADAR))
			continue;

		if ((channel->flag & HOSTAPD_CHAN_DFS_MASK) !=
		    HOSTAPD_CHAN_DFS_AVAILABLE)
			break;
	}

	return i == n_chans;
}


/* At least one channel unavailable */
static int dfs_check_chans_unavailable(struct hostapd_iface *iface,
				       int start_chan_idx,
				       int n_chans)
{
	struct hostapd_channel_data *channel;
	struct hostapd_hw_modes *mode;
	int i, res = 0;

	mode = iface->current_mode;

	for (i = 0; i < n_chans; i++) {
		channel = &mode->channels[start_chan_idx + i];
		if (channel->flag & HOSTAPD_CHAN_DISABLED)
			res++;
		if ((channel->flag & HOSTAPD_CHAN_DFS_MASK) ==
		    HOSTAPD_CHAN_DFS_UNAVAILABLE)
			res++;
	}

	return res;
}


static struct hostapd_channel_data *
dfs_get_valid_channel(struct hostapd_iface *iface,
		      int *secondary_channel,
		      u8 *vht_oper_centr_freq_seg0_idx,
		      u8 *vht_oper_centr_freq_seg1_idx,
		      int skip_radar)
{
	struct hostapd_hw_modes *mode;
	struct hostapd_channel_data *chan = NULL;
	int num_available_chandefs;
	int chan_idx;
	u32 _rand;

	wpa_printf(MSG_DEBUG, "DFS: Selecting random channel");
	*secondary_channel = 0;
	*vht_oper_centr_freq_seg0_idx = 0;
	*vht_oper_centr_freq_seg1_idx = 0;

	if (iface->current_mode == NULL)
		return NULL;

	mode = iface->current_mode;
	if (mode->mode != HOSTAPD_MODE_IEEE80211A)
		return NULL;

	/* Get the count first */
	num_available_chandefs = dfs_find_channel(iface, NULL, 0, skip_radar);
	if (num_available_chandefs == 0)
		return NULL;

	if (os_get_random((u8 *) &_rand, sizeof(_rand)) < 0)
		return NULL;
	chan_idx = _rand % num_available_chandefs;
	dfs_find_channel(iface, &chan, chan_idx, skip_radar);

	/* dfs_find_channel() calculations assume HT40+ */
	if (iface->conf->secondary_channel)
		*secondary_channel = 1;
	else
		*secondary_channel = 0;

	dfs_adjust_vht_center_freq(iface, chan,
				   *secondary_channel,
				   vht_oper_centr_freq_seg0_idx,
				   vht_oper_centr_freq_seg1_idx);

	return chan;
}


static int set_dfs_state_freq(struct hostapd_iface *iface, int freq, u32 state)
{
	struct hostapd_hw_modes *mode;
	struct hostapd_channel_data *chan = NULL;
	int i;

	mode = iface->current_mode;
	if (mode == NULL)
		return 0;

	wpa_printf(MSG_DEBUG, "set_dfs_state 0x%X for %d MHz", state, freq);
	for (i = 0; i < iface->current_mode->num_channels; i++) {
		chan = &iface->current_mode->channels[i];
		if (chan->freq == freq) {
			if (chan->flag & HOSTAPD_CHAN_RADAR) {
				chan->flag &= ~HOSTAPD_CHAN_DFS_MASK;
				chan->flag |= state;
				return 1; /* Channel found */
			}
		}
	}
	wpa_printf(MSG_WARNING, "Can't set DFS state for freq %d MHz", freq);
	return 0;
}


static int set_dfs_state(struct hostapd_iface *iface, int freq, int ht_enabled,
			 int chan_offset, int chan_width, int cf1,
			 int cf2, u32 state)
{
	int n_chans = 1, i;
	struct hostapd_hw_modes *mode;
	int frequency = freq;
	int ret = 0;

	mode = iface->current_mode;
	if (mode == NULL)
		return 0;

	if (mode->mode != HOSTAPD_MODE_IEEE80211A) {
		wpa_printf(MSG_WARNING, "current_mode != IEEE80211A");
		return 0;
	}

	/* Seems cf1 and chan_width is enough here */
	switch (chan_width) {
	case CHAN_WIDTH_20_NOHT:
	case CHAN_WIDTH_20:
		n_chans = 1;
		if (frequency == 0)
			frequency = cf1;
		break;
	case CHAN_WIDTH_40:
		n_chans = 2;
		frequency = cf1 - 10;
		break;
	case CHAN_WIDTH_80:
		n_chans = 4;
		frequency = cf1 - 30;
		break;
	case CHAN_WIDTH_160:
		n_chans = 8;
		frequency = cf1 - 70;
		break;
	default:
		wpa_printf(MSG_INFO, "DFS chan_width %d not supported",
			   chan_width);
		break;
	}

	wpa_printf(MSG_DEBUG, "DFS freq: %dMHz, n_chans: %d", frequency,
		   n_chans);
	for (i = 0; i < n_chans; i++) {
		ret += set_dfs_state_freq(iface, frequency, state);
		frequency = frequency + 20;
	}

	return ret;
}


static int dfs_are_channels_overlapped(struct hostapd_iface *iface, int freq,
				       int chan_width, int cf1, int cf2)
{
	int start_chan_idx, start_chan_idx1;
	struct hostapd_hw_modes *mode;
	struct hostapd_channel_data *chan;
	int n_chans, n_chans1, i, j, frequency = freq, radar_n_chans = 1;
	u8 radar_chan;
	int res = 0;

	/* Our configuration */
	mode = iface->current_mode;
	start_chan_idx = dfs_get_start_chan_idx(iface, &start_chan_idx1);
	n_chans = dfs_get_used_n_chans(iface, &n_chans1);

	/* Check we are on DFS channel(s) */
	if (!dfs_check_chans_radar(iface, start_chan_idx, n_chans))
		return 0;

	/* Reported via radar event */
	switch (chan_width) {
	case CHAN_WIDTH_20_NOHT:
	case CHAN_WIDTH_20:
		radar_n_chans = 1;
		if (frequency == 0)
			frequency = cf1;
		break;
	case CHAN_WIDTH_40:
		radar_n_chans = 2;
		frequency = cf1 - 10;
		break;
	case CHAN_WIDTH_80:
		radar_n_chans = 4;
		frequency = cf1 - 30;
		break;
	case CHAN_WIDTH_160:
		radar_n_chans = 8;
		frequency = cf1 - 70;
		break;
	default:
		wpa_printf(MSG_INFO, "DFS chan_width %d not supported",
			   chan_width);
		break;
	}

	ieee80211_freq_to_chan(frequency, &radar_chan);

	for (i = 0; i < n_chans; i++) {
		chan = &mode->channels[start_chan_idx + i];
		if (!(chan->flag & HOSTAPD_CHAN_RADAR))
			continue;
		for (j = 0; j < radar_n_chans; j++) {
			wpa_printf(MSG_DEBUG, "checking our: %d, radar: %d",
				   chan->chan, radar_chan + j * 4);
			if (chan->chan == radar_chan + j * 4)
				res++;
		}
	}

	wpa_printf(MSG_DEBUG, "overlapped: %d", res);

	return res;
}


static unsigned int dfs_get_cac_time(struct hostapd_iface *iface,
				     int start_chan_idx, int n_chans)
{
	struct hostapd_channel_data *channel;
	struct hostapd_hw_modes *mode;
	int i;
	unsigned int cac_time_ms = 0;

	mode = iface->current_mode;

	for (i = 0; i < n_chans; i++) {
		channel = &mode->channels[start_chan_idx + i];
		if (!(channel->flag & HOSTAPD_CHAN_RADAR))
			continue;
		if (channel->dfs_cac_ms > cac_time_ms)
			cac_time_ms = channel->dfs_cac_ms;
	}

	return cac_time_ms;
}


/*
 * Main DFS handler
 * 1 - continue channel/ap setup
 * 0 - channel/ap setup will be continued after CAC
 * -1 - hit critical error
 */
int hostapd_handle_dfs(struct hostapd_iface *iface)
{
	struct hostapd_channel_data *channel;
	int res, n_chans, n_chans1, start_chan_idx, start_chan_idx1;
	int skip_radar = 0;

	if (!iface->current_mode) {
		/*
		 * This can happen with drivers that do not provide mode
		 * information and as such, cannot really use hostapd for DFS.
		 */
		wpa_printf(MSG_DEBUG,
			   "DFS: No current_mode information - assume no need to perform DFS operations by hostapd");
		return 1;
	}

	iface->cac_started = 0;

	do {
		/* Get start (first) channel for current configuration */
		start_chan_idx = dfs_get_start_chan_idx(iface,
							&start_chan_idx1);
		if (start_chan_idx == -1)
			return -1;

		/* Get number of used channels, depend on width */
		n_chans = dfs_get_used_n_chans(iface, &n_chans1);

		/* Setup CAC time */
		iface->dfs_cac_ms = dfs_get_cac_time(iface, start_chan_idx,
						     n_chans);

		/* Check if any of configured channels require DFS */
		res = dfs_check_chans_radar(iface, start_chan_idx, n_chans);
		wpa_printf(MSG_DEBUG,
			   "DFS %d channels required radar detection",
			   res);
		if (!res)
			return 1;

		/* Check if all channels are DFS available */
		res = dfs_check_chans_available(iface, start_chan_idx, n_chans);
		wpa_printf(MSG_DEBUG,
			   "DFS all channels available, (SKIP CAC): %s",
			   res ? "yes" : "no");
		if (res)
			return 1;

		/* Check if any of configured channels is unavailable */
		res = dfs_check_chans_unavailable(iface, start_chan_idx,
						  n_chans);
		wpa_printf(MSG_DEBUG, "DFS %d chans unavailable - choose other channel: %s",
			   res, res ? "yes": "no");
		if (res) {
			int sec = 0;
			u8 cf1 = 0, cf2 = 0;

			channel = dfs_get_valid_channel(iface, &sec, &cf1, &cf2,
							skip_radar);
			if (!channel) {
				wpa_printf(MSG_ERROR, "could not get valid channel");
				hostapd_set_state(iface, HAPD_IFACE_DFS);
				return 0;
			}

			iface->freq = channel->freq;
			iface->conf->channel = channel->chan;
			iface->conf->secondary_channel = sec;
			iface->conf->vht_oper_centr_freq_seg0_idx = cf1;
			iface->conf->vht_oper_centr_freq_seg1_idx = cf2;
		}
	} while (res);

	/* Finally start CAC */
	hostapd_set_state(iface, HAPD_IFACE_DFS);
	wpa_printf(MSG_DEBUG, "DFS start CAC on %d MHz", iface->freq);
	wpa_msg(iface->bss[0]->msg_ctx, MSG_INFO, DFS_EVENT_CAC_START
		"freq=%d chan=%d sec_chan=%d, width=%d, seg0=%d, seg1=%d, cac_time=%ds",
		iface->freq,
		iface->conf->channel, iface->conf->secondary_channel,
		iface->conf->vht_oper_chwidth,
		iface->conf->vht_oper_centr_freq_seg0_idx,
		iface->conf->vht_oper_centr_freq_seg1_idx,
		iface->dfs_cac_ms / 1000);

	res = hostapd_start_dfs_cac(iface, iface->conf->hw_mode,
				    iface->freq,
				    iface->conf->channel,
				    iface->conf->ieee80211n,
				    iface->conf->ieee80211ac,
				    iface->conf->secondary_channel,
				    iface->conf->vht_oper_chwidth,
				    iface->conf->vht_oper_centr_freq_seg0_idx,
				    iface->conf->vht_oper_centr_freq_seg1_idx);

	if (res) {
		wpa_printf(MSG_ERROR, "DFS start_dfs_cac() failed, %d", res);
		return -1;
	}

	return 0;
}


static int hostapd_config_dfs_chan_available(struct hostapd_iface *iface)
{
	int n_chans, n_chans1, start_chan_idx, start_chan_idx1;

	/* Get the start (first) channel for current configuration */
	start_chan_idx = dfs_get_start_chan_idx(iface, &start_chan_idx1);
	if (start_chan_idx < 0)
		return 0;

	/* Get the number of used channels, depending on width */
	n_chans = dfs_get_used_n_chans(iface, &n_chans1);

	/* Check if all channels are DFS available */
	return dfs_check_chans_available(iface, start_chan_idx, n_chans);
}


int hostapd_dfs_complete_cac(struct hostapd_iface *iface, int success, int freq,
			     int ht_enabled, int chan_offset, int chan_width,
			     int cf1, int cf2)
{
	wpa_msg(iface->bss[0]->msg_ctx, MSG_INFO, DFS_EVENT_CAC_COMPLETED
		"success=%d freq=%d ht_enabled=%d chan_offset=%d chan_width=%d cf1=%d cf2=%d",
		success, freq, ht_enabled, chan_offset, chan_width, cf1, cf2);

	if (success) {
		/* Complete iface/ap configuration */
		if (iface->drv_flags & WPA_DRIVER_FLAGS_DFS_OFFLOAD) {
			/* Complete AP configuration for the first bring up. */
			if (iface->state != HAPD_IFACE_ENABLED)
				hostapd_setup_interface_complete(iface, 0);
			else
				iface->cac_started = 0;
		} else {
			set_dfs_state(iface, freq, ht_enabled, chan_offset,
				      chan_width, cf1, cf2,
				      HOSTAPD_CHAN_DFS_AVAILABLE);
			/*
			 * Just mark the channel available when CAC completion
			 * event is received in enabled state. CAC result could
			 * have been propagated from another radio having the
			 * same regulatory configuration. When CAC completion is
			 * received during non-HAPD_IFACE_ENABLED state, make
			 * sure the configured channel is available because this
			 * CAC completion event could have been propagated from
			 * another radio.
			 */
			if (iface->state != HAPD_IFACE_ENABLED &&
			    hostapd_config_dfs_chan_available(iface)) {
				hostapd_setup_interface_complete(iface, 0);
				iface->cac_started = 0;
			}
		}
	}

	return 0;
}


int hostapd_dfs_pre_cac_expired(struct hostapd_iface *iface, int freq,
				int ht_enabled, int chan_offset, int chan_width,
				int cf1, int cf2)
{
	wpa_msg(iface->bss[0]->msg_ctx, MSG_INFO, DFS_EVENT_PRE_CAC_EXPIRED
		"freq=%d ht_enabled=%d chan_offset=%d chan_width=%d cf1=%d cf2=%d",
		freq, ht_enabled, chan_offset, chan_width, cf1, cf2);

	/* Proceed only if DFS is not offloaded to the driver */
	if (iface->drv_flags & WPA_DRIVER_FLAGS_DFS_OFFLOAD)
		return 0;

	set_dfs_state(iface, freq, ht_enabled, chan_offset, chan_width,
		      cf1, cf2, HOSTAPD_CHAN_DFS_USABLE);

	return 0;
}


static int hostapd_dfs_start_channel_switch_cac(struct hostapd_iface *iface)
{
	struct hostapd_channel_data *channel;
	int secondary_channel;
	u8 vht_oper_centr_freq_seg0_idx = 0;
	u8 vht_oper_centr_freq_seg1_idx = 0;
	int skip_radar = 0;
	int err = 1;

	/* Radar detected during active CAC */
	iface->cac_started = 0;
	channel = dfs_get_valid_channel(iface, &secondary_channel,
					&vht_oper_centr_freq_seg0_idx,
					&vht_oper_centr_freq_seg1_idx,
					skip_radar);

	if (!channel) {
		wpa_printf(MSG_ERROR, "No valid channel available");
		return err;
	}

	wpa_printf(MSG_DEBUG, "DFS will switch to a new channel %d",
		   channel->chan);
	wpa_msg(iface->bss[0]->msg_ctx, MSG_INFO, DFS_EVENT_NEW_CHANNEL
		"freq=%d chan=%d sec_chan=%d", channel->freq,
		channel->chan, secondary_channel);

	iface->freq = channel->freq;
	iface->conf->channel = channel->chan;
	iface->conf->secondary_channel = secondary_channel;
	iface->conf->vht_oper_centr_freq_seg0_idx =
		vht_oper_centr_freq_seg0_idx;
	iface->conf->vht_oper_centr_freq_seg1_idx =
		vht_oper_centr_freq_seg1_idx;
	err = 0;

	hostapd_setup_interface_complete(iface, err);
	return err;
}


static int hostapd_dfs_start_channel_switch(struct hostapd_iface *iface)
{
	struct hostapd_channel_data *channel;
	int secondary_channel;
	u8 vht_oper_centr_freq_seg0_idx;
	u8 vht_oper_centr_freq_seg1_idx;
	int skip_radar = 1;
	struct csa_settings csa_settings;
	unsigned int i;
	int err = 1;

	wpa_printf(MSG_DEBUG, "%s called (CAC active: %s, CSA active: %s)",
		   __func__, iface->cac_started ? "yes" : "no",
		   hostapd_csa_in_progress(iface) ? "yes" : "no");

	/* Check if CSA in progress */
	if (hostapd_csa_in_progress(iface))
		return 0;

	/* Check if active CAC */
	if (iface->cac_started)
		return hostapd_dfs_start_channel_switch_cac(iface);

	/*
	 * Allow selection of DFS channel in ETSI to comply with
	 * uniform spreading.
	 */
	if (iface->dfs_domain == HOSTAPD_DFS_REGION_ETSI)
		skip_radar = 0;

	/* Perform channel switch/CSA */
	channel = dfs_get_valid_channel(iface, &secondary_channel,
					&vht_oper_centr_freq_seg0_idx,
					&vht_oper_centr_freq_seg1_idx,
					skip_radar);

	if (!channel) {
		/*
		 * If there is no channel to switch immediately to, check if
		 * there is another channel where we can switch even if it
		 * requires to perform a CAC first.
		 */
		skip_radar = 0;
		channel = dfs_get_valid_channel(iface, &secondary_channel,
						&vht_oper_centr_freq_seg0_idx,
						&vht_oper_centr_freq_seg1_idx,
						skip_radar);
		if (!channel) {
			wpa_printf(MSG_INFO,
				   "%s: no DFS channels left, waiting for NOP to finish",
				   __func__);
			return err;
		}

		iface->freq = channel->freq;
		iface->conf->channel = channel->chan;
		iface->conf->secondary_channel = secondary_channel;
		iface->conf->vht_oper_centr_freq_seg0_idx =
			vht_oper_centr_freq_seg0_idx;
		iface->conf->vht_oper_centr_freq_seg1_idx =
			vht_oper_centr_freq_seg1_idx;

		hostapd_disable_iface(iface);
		hostapd_enable_iface(iface);
		return 0;
	}

	wpa_printf(MSG_DEBUG, "DFS will switch to a new channel %d",
		   channel->chan);
	wpa_msg(iface->bss[0]->msg_ctx, MSG_INFO, DFS_EVENT_NEW_CHANNEL
		"freq=%d chan=%d sec_chan=%d", channel->freq,
		channel->chan, secondary_channel);

	/* Setup CSA request */
	os_memset(&csa_settings, 0, sizeof(csa_settings));
	csa_settings.cs_count = 5;
	csa_settings.block_tx = 1;
	err = hostapd_set_freq_params(&csa_settings.freq_params,
				      iface->conf->hw_mode,
				      channel->freq,
				      channel->chan,
				      iface->conf->ieee80211n,
				      iface->conf->ieee80211ac,
				      secondary_channel,
				      iface->conf->vht_oper_chwidth,
				      vht_oper_centr_freq_seg0_idx,
				      vht_oper_centr_freq_seg1_idx,
				      iface->current_mode->vht_capab);

	if (err) {
		wpa_printf(MSG_ERROR, "DFS failed to calculate CSA freq params");
		hostapd_disable_iface(iface);
		return err;
	}

	for (i = 0; i < iface->num_bss; i++) {
		err = hostapd_switch_channel(iface->bss[i], &csa_settings);
		if (err)
			break;
	}

	if (err) {
		wpa_printf(MSG_WARNING, "DFS failed to schedule CSA (%d) - trying fallback",
			   err);
		iface->freq = channel->freq;
		iface->conf->channel = channel->chan;
		iface->conf->secondary_channel = secondary_channel;
		iface->conf->vht_oper_centr_freq_seg0_idx =
			vht_oper_centr_freq_seg0_idx;
		iface->conf->vht_oper_centr_freq_seg1_idx =
			vht_oper_centr_freq_seg1_idx;

		hostapd_disable_iface(iface);
		hostapd_enable_iface(iface);
		return 0;
	}

	/* Channel configuration will be updated once CSA completes and
	 * ch_switch_notify event is received */

	wpa_printf(MSG_DEBUG, "DFS waiting channel switch event");
	return 0;
}


int hostapd_dfs_radar_detected(struct hostapd_iface *iface, int freq,
			       int ht_enabled, int chan_offset, int chan_width,
			       int cf1, int cf2)
{
	int res;

	wpa_msg(iface->bss[0]->msg_ctx, MSG_INFO, DFS_EVENT_RADAR_DETECTED
		"freq=%d ht_enabled=%d chan_offset=%d chan_width=%d cf1=%d cf2=%d",
		freq, ht_enabled, chan_offset, chan_width, cf1, cf2);

	/* Proceed only if DFS is not offloaded to the driver */
	if (iface->drv_flags & WPA_DRIVER_FLAGS_DFS_OFFLOAD)
		return 0;

	if (!iface->conf->ieee80211h)
		return 0;

	/* mark radar frequency as invalid */
	set_dfs_state(iface, freq, ht_enabled, chan_offset, chan_width,
		      cf1, cf2, HOSTAPD_CHAN_DFS_UNAVAILABLE);

	/* Skip if reported radar event not overlapped our channels */
	res = dfs_are_channels_overlapped(iface, freq, chan_width, cf1, cf2);
	if (!res)
		return 0;

	/* radar detected while operating, switch the channel. */
	res = hostapd_dfs_start_channel_switch(iface);

	return res;
}


int hostapd_dfs_nop_finished(struct hostapd_iface *iface, int freq,
			     int ht_enabled, int chan_offset, int chan_width,
			     int cf1, int cf2)
{
	wpa_msg(iface->bss[0]->msg_ctx, MSG_INFO, DFS_EVENT_NOP_FINISHED
		"freq=%d ht_enabled=%d chan_offset=%d chan_width=%d cf1=%d cf2=%d",
		freq, ht_enabled, chan_offset, chan_width, cf1, cf2);

	/* Proceed only if DFS is not offloaded to the driver */
	if (iface->drv_flags & WPA_DRIVER_FLAGS_DFS_OFFLOAD)
		return 0;

	/* TODO add correct implementation here */
	set_dfs_state(iface, freq, ht_enabled, chan_offset, chan_width,
		      cf1, cf2, HOSTAPD_CHAN_DFS_USABLE);

	/* Handle cases where all channels were initially unavailable */
	if (iface->state == HAPD_IFACE_DFS && !iface->cac_started)
		hostapd_handle_dfs(iface);

	return 0;
}


int hostapd_is_dfs_required(struct hostapd_iface *iface)
{
	int n_chans, n_chans1, start_chan_idx, start_chan_idx1, res;

	if (!iface->conf->ieee80211h || !iface->current_mode ||
	    iface->current_mode->mode != HOSTAPD_MODE_IEEE80211A)
		return 0;

	/* Get start (first) channel for current configuration */
	start_chan_idx = dfs_get_start_chan_idx(iface, &start_chan_idx1);
	if (start_chan_idx == -1)
		return -1;

	/* Get number of used channels, depend on width */
	n_chans = dfs_get_used_n_chans(iface, &n_chans1);

	/* Check if any of configured channels require DFS */
	res = dfs_check_chans_radar(iface, start_chan_idx, n_chans);
	if (res)
		return res;
	if (start_chan_idx1 >= 0 && n_chans1 > 0)
		res = dfs_check_chans_radar(iface, start_chan_idx1, n_chans1);
	return res;
}


int hostapd_dfs_start_cac(struct hostapd_iface *iface, int freq,
			  int ht_enabled, int chan_offset, int chan_width,
			  int cf1, int cf2)
{
	wpa_msg(iface->bss[0]->msg_ctx, MSG_INFO, DFS_EVENT_CAC_START
		"freq=%d chan=%d chan_offset=%d width=%d seg0=%d "
		"seg1=%d cac_time=%ds",
		freq, (freq - 5000) / 5, chan_offset, chan_width, cf1, cf2, 60);
	iface->cac_started = 1;
	return 0;
}


/*
 * Main DFS handler for offloaded case.
 * 2 - continue channel/AP setup for non-DFS channel
 * 1 - continue channel/AP setup for DFS channel
 * 0 - channel/AP setup will be continued after CAC
 * -1 - hit critical error
 */
int hostapd_handle_dfs_offload(struct hostapd_iface *iface)
{
	wpa_printf(MSG_DEBUG, "%s: iface->cac_started: %d",
		   __func__, iface->cac_started);

	/*
	 * If DFS has already been started, then we are being called from a
	 * callback to continue AP/channel setup. Reset the CAC start flag and
	 * return.
	 */
	if (iface->cac_started) {
		wpa_printf(MSG_DEBUG, "%s: iface->cac_started: %d",
			   __func__, iface->cac_started);
		iface->cac_started = 0;
		return 1;
	}

	if (ieee80211_is_dfs(iface->freq, iface->hw_features,
			     iface->num_hw_features)) {
		wpa_printf(MSG_DEBUG, "%s: freq %d MHz requires DFS",
			   __func__, iface->freq);
		return 0;
	}

	wpa_printf(MSG_DEBUG,
		   "%s: freq %d MHz does not require DFS. Continue channel/AP setup",
		   __func__, iface->freq);
	return 2;
}
