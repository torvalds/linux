/*
 * ACS - Automatic Channel Selection module
 * Copyright (c) 2011, Atheros Communications
 * Copyright (c) 2013, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"
#include <math.h>

#include "utils/common.h"
#include "utils/list.h"
#include "common/ieee802_11_defs.h"
#include "common/wpa_ctrl.h"
#include "drivers/driver.h"
#include "hostapd.h"
#include "ap_drv_ops.h"
#include "ap_config.h"
#include "hw_features.h"
#include "acs.h"

/*
 * Automatic Channel Selection
 * ===========================
 *
 * More info at
 * ------------
 * http://wireless.kernel.org/en/users/Documentation/acs
 *
 * How to use
 * ----------
 * - make sure you have CONFIG_ACS=y in hostapd's .config
 * - use channel=0 or channel=acs to enable ACS
 *
 * How does it work
 * ----------------
 * 1. passive scans are used to collect survey data
 *    (it is assumed that scan trigger collection of survey data in driver)
 * 2. interference factor is calculated for each channel
 * 3. ideal channel is picked depending on channel width by using adjacent
 *    channel interference factors
 *
 * Known limitations
 * -----------------
 * - Current implementation depends heavily on the amount of time willing to
 *   spend gathering survey data during hostapd startup. Short traffic bursts
 *   may be missed and a suboptimal channel may be picked.
 * - Ideal channel may end up overlapping a channel with 40 MHz intolerant BSS
 *
 * Todo / Ideas
 * ------------
 * - implement other interference computation methods
 *   - BSS/RSSI based
 *   - spectral scan based
 *   (should be possibly to hook this up with current ACS scans)
 * - add wpa_supplicant support (for P2P)
 * - collect a histogram of interference over time allowing more educated
 *   guess about an ideal channel (perhaps CSA could be used to migrate AP to a
 *   new "better" channel while running)
 * - include neighboring BSS scan to avoid conflicts with 40 MHz intolerant BSSs
 *   when choosing the ideal channel
 *
 * Survey interference factor implementation details
 * -------------------------------------------------
 * Generic interference_factor in struct hostapd_channel_data is used.
 *
 * The survey interference factor is defined as the ratio of the
 * observed busy time over the time we spent on the channel,
 * this value is then amplified by the observed noise floor on
 * the channel in comparison to the lowest noise floor observed
 * on the entire band.
 *
 * This corresponds to:
 * ---
 * (busy time - tx time) / (active time - tx time) * 2^(chan_nf + band_min_nf)
 * ---
 *
 * The coefficient of 2 reflects the way power in "far-field"
 * radiation decreases as the square of distance from the antenna [1].
 * What this does is it decreases the observed busy time ratio if the
 * noise observed was low but increases it if the noise was high,
 * proportionally to the way "far field" radiation changes over
 * distance.
 *
 * If channel busy time is not available the fallback is to use channel RX time.
 *
 * Since noise floor is in dBm it is necessary to convert it into Watts so that
 * combined channel interference (e.g., HT40, which uses two channels) can be
 * calculated easily.
 * ---
 * (busy time - tx time) / (active time - tx time) *
 *    2^(10^(chan_nf/10) + 10^(band_min_nf/10))
 * ---
 *
 * However to account for cases where busy/rx time is 0 (channel load is then
 * 0%) channel noise floor signal power is combined into the equation so a
 * channel with lower noise floor is preferred. The equation becomes:
 * ---
 * 10^(chan_nf/5) + (busy time - tx time) / (active time - tx time) *
 *    2^(10^(chan_nf/10) + 10^(band_min_nf/10))
 * ---
 *
 * All this "interference factor" is purely subjective and only time
 * will tell how usable this is. By using the minimum noise floor we
 * remove any possible issues due to card calibration. The computation
 * of the interference factor then is dependent on what the card itself
 * picks up as the minimum noise, not an actual real possible card
 * noise value.
 *
 * Total interference computation details
 * --------------------------------------
 * The above channel interference factor is calculated with no respect to
 * target operational bandwidth.
 *
 * To find an ideal channel the above data is combined by taking into account
 * the target operational bandwidth and selected band. E.g., on 2.4 GHz channels
 * overlap with 20 MHz bandwidth, but there is no overlap for 20 MHz bandwidth
 * on 5 GHz.
 *
 * Each valid and possible channel spec (i.e., channel + width) is taken and its
 * interference factor is computed by summing up interferences of each channel
 * it overlaps. The one with least total interference is picked up.
 *
 * Note: This implies base channel interference factor must be non-negative
 * allowing easy summing up.
 *
 * Example ACS analysis printout
 * -----------------------------
 *
 * ACS: Trying survey-based ACS
 * ACS: Survey analysis for channel 1 (2412 MHz)
 * ACS:  1: min_nf=-113 interference_factor=0.0802469 nf=-113 time=162 busy=0 rx=13
 * ACS:  2: min_nf=-113 interference_factor=0.0745342 nf=-113 time=161 busy=0 rx=12
 * ACS:  3: min_nf=-113 interference_factor=0.0679012 nf=-113 time=162 busy=0 rx=11
 * ACS:  4: min_nf=-113 interference_factor=0.0310559 nf=-113 time=161 busy=0 rx=5
 * ACS:  5: min_nf=-113 interference_factor=0.0248447 nf=-113 time=161 busy=0 rx=4
 * ACS:  * interference factor average: 0.0557166
 * ACS: Survey analysis for channel 2 (2417 MHz)
 * ACS:  1: min_nf=-113 interference_factor=0.0185185 nf=-113 time=162 busy=0 rx=3
 * ACS:  2: min_nf=-113 interference_factor=0.0246914 nf=-113 time=162 busy=0 rx=4
 * ACS:  3: min_nf=-113 interference_factor=0.037037 nf=-113 time=162 busy=0 rx=6
 * ACS:  4: min_nf=-113 interference_factor=0.149068 nf=-113 time=161 busy=0 rx=24
 * ACS:  5: min_nf=-113 interference_factor=0.0248447 nf=-113 time=161 busy=0 rx=4
 * ACS:  * interference factor average: 0.050832
 * ACS: Survey analysis for channel 3 (2422 MHz)
 * ACS:  1: min_nf=-113 interference_factor=2.51189e-23 nf=-113 time=162 busy=0 rx=0
 * ACS:  2: min_nf=-113 interference_factor=0.0185185 nf=-113 time=162 busy=0 rx=3
 * ACS:  3: min_nf=-113 interference_factor=0.0186335 nf=-113 time=161 busy=0 rx=3
 * ACS:  4: min_nf=-113 interference_factor=0.0186335 nf=-113 time=161 busy=0 rx=3
 * ACS:  5: min_nf=-113 interference_factor=0.0186335 nf=-113 time=161 busy=0 rx=3
 * ACS:  * interference factor average: 0.0148838
 * ACS: Survey analysis for channel 4 (2427 MHz)
 * ACS:  1: min_nf=-114 interference_factor=1.58489e-23 nf=-114 time=162 busy=0 rx=0
 * ACS:  2: min_nf=-114 interference_factor=0.0555556 nf=-114 time=162 busy=0 rx=9
 * ACS:  3: min_nf=-114 interference_factor=1.58489e-23 nf=-114 time=161 busy=0 rx=0
 * ACS:  4: min_nf=-114 interference_factor=0.0186335 nf=-114 time=161 busy=0 rx=3
 * ACS:  5: min_nf=-114 interference_factor=0.00621118 nf=-114 time=161 busy=0 rx=1
 * ACS:  * interference factor average: 0.0160801
 * ACS: Survey analysis for channel 5 (2432 MHz)
 * ACS:  1: min_nf=-114 interference_factor=0.409938 nf=-113 time=161 busy=0 rx=66
 * ACS:  2: min_nf=-114 interference_factor=0.0432099 nf=-113 time=162 busy=0 rx=7
 * ACS:  3: min_nf=-114 interference_factor=0.0124224 nf=-113 time=161 busy=0 rx=2
 * ACS:  4: min_nf=-114 interference_factor=0.677019 nf=-113 time=161 busy=0 rx=109
 * ACS:  5: min_nf=-114 interference_factor=0.0186335 nf=-114 time=161 busy=0 rx=3
 * ACS:  * interference factor average: 0.232244
 * ACS: Survey analysis for channel 6 (2437 MHz)
 * ACS:  1: min_nf=-113 interference_factor=0.552795 nf=-113 time=161 busy=0 rx=89
 * ACS:  2: min_nf=-113 interference_factor=0.0807453 nf=-112 time=161 busy=0 rx=13
 * ACS:  3: min_nf=-113 interference_factor=0.0310559 nf=-113 time=161 busy=0 rx=5
 * ACS:  4: min_nf=-113 interference_factor=0.434783 nf=-112 time=161 busy=0 rx=70
 * ACS:  5: min_nf=-113 interference_factor=0.0621118 nf=-113 time=161 busy=0 rx=10
 * ACS:  * interference factor average: 0.232298
 * ACS: Survey analysis for channel 7 (2442 MHz)
 * ACS:  1: min_nf=-113 interference_factor=0.440994 nf=-112 time=161 busy=0 rx=71
 * ACS:  2: min_nf=-113 interference_factor=0.385093 nf=-113 time=161 busy=0 rx=62
 * ACS:  3: min_nf=-113 interference_factor=0.0372671 nf=-113 time=161 busy=0 rx=6
 * ACS:  4: min_nf=-113 interference_factor=0.0372671 nf=-113 time=161 busy=0 rx=6
 * ACS:  5: min_nf=-113 interference_factor=0.0745342 nf=-113 time=161 busy=0 rx=12
 * ACS:  * interference factor average: 0.195031
 * ACS: Survey analysis for channel 8 (2447 MHz)
 * ACS:  1: min_nf=-114 interference_factor=0.0496894 nf=-112 time=161 busy=0 rx=8
 * ACS:  2: min_nf=-114 interference_factor=0.0496894 nf=-114 time=161 busy=0 rx=8
 * ACS:  3: min_nf=-114 interference_factor=0.0372671 nf=-113 time=161 busy=0 rx=6
 * ACS:  4: min_nf=-114 interference_factor=0.12963 nf=-113 time=162 busy=0 rx=21
 * ACS:  5: min_nf=-114 interference_factor=0.166667 nf=-114 time=162 busy=0 rx=27
 * ACS:  * interference factor average: 0.0865885
 * ACS: Survey analysis for channel 9 (2452 MHz)
 * ACS:  1: min_nf=-114 interference_factor=0.0124224 nf=-114 time=161 busy=0 rx=2
 * ACS:  2: min_nf=-114 interference_factor=0.0310559 nf=-114 time=161 busy=0 rx=5
 * ACS:  3: min_nf=-114 interference_factor=1.58489e-23 nf=-114 time=161 busy=0 rx=0
 * ACS:  4: min_nf=-114 interference_factor=0.00617284 nf=-114 time=162 busy=0 rx=1
 * ACS:  5: min_nf=-114 interference_factor=1.58489e-23 nf=-114 time=162 busy=0 rx=0
 * ACS:  * interference factor average: 0.00993022
 * ACS: Survey analysis for channel 10 (2457 MHz)
 * ACS:  1: min_nf=-114 interference_factor=0.00621118 nf=-114 time=161 busy=0 rx=1
 * ACS:  2: min_nf=-114 interference_factor=0.00621118 nf=-114 time=161 busy=0 rx=1
 * ACS:  3: min_nf=-114 interference_factor=0.00621118 nf=-114 time=161 busy=0 rx=1
 * ACS:  4: min_nf=-114 interference_factor=0.0493827 nf=-114 time=162 busy=0 rx=8
 * ACS:  5: min_nf=-114 interference_factor=1.58489e-23 nf=-114 time=162 busy=0 rx=0
 * ACS:  * interference factor average: 0.0136033
 * ACS: Survey analysis for channel 11 (2462 MHz)
 * ACS:  1: min_nf=-114 interference_factor=1.58489e-23 nf=-114 time=161 busy=0 rx=0
 * ACS:  2: min_nf=-114 interference_factor=2.51189e-23 nf=-113 time=161 busy=0 rx=0
 * ACS:  3: min_nf=-114 interference_factor=2.51189e-23 nf=-113 time=161 busy=0 rx=0
 * ACS:  4: min_nf=-114 interference_factor=0.0432099 nf=-114 time=162 busy=0 rx=7
 * ACS:  5: min_nf=-114 interference_factor=0.0925926 nf=-114 time=162 busy=0 rx=15
 * ACS:  * interference factor average: 0.0271605
 * ACS: Survey analysis for channel 12 (2467 MHz)
 * ACS:  1: min_nf=-114 interference_factor=0.0621118 nf=-113 time=161 busy=0 rx=10
 * ACS:  2: min_nf=-114 interference_factor=0.00621118 nf=-114 time=161 busy=0 rx=1
 * ACS:  3: min_nf=-114 interference_factor=2.51189e-23 nf=-113 time=162 busy=0 rx=0
 * ACS:  4: min_nf=-114 interference_factor=2.51189e-23 nf=-113 time=162 busy=0 rx=0
 * ACS:  5: min_nf=-114 interference_factor=0.00617284 nf=-113 time=162 busy=0 rx=1
 * ACS:  * interference factor average: 0.0148992
 * ACS: Survey analysis for channel 13 (2472 MHz)
 * ACS:  1: min_nf=-114 interference_factor=0.0745342 nf=-114 time=161 busy=0 rx=12
 * ACS:  2: min_nf=-114 interference_factor=0.0555556 nf=-114 time=162 busy=0 rx=9
 * ACS:  3: min_nf=-114 interference_factor=1.58489e-23 nf=-114 time=162 busy=0 rx=0
 * ACS:  4: min_nf=-114 interference_factor=1.58489e-23 nf=-114 time=162 busy=0 rx=0
 * ACS:  5: min_nf=-114 interference_factor=1.58489e-23 nf=-114 time=162 busy=0 rx=0
 * ACS:  * interference factor average: 0.0260179
 * ACS: Survey analysis for selected bandwidth 20MHz
 * ACS:  * channel 1: total interference = 0.121432
 * ACS:  * channel 2: total interference = 0.137512
 * ACS:  * channel 3: total interference = 0.369757
 * ACS:  * channel 4: total interference = 0.546338
 * ACS:  * channel 5: total interference = 0.690538
 * ACS:  * channel 6: total interference = 0.762242
 * ACS:  * channel 7: total interference = 0.756092
 * ACS:  * channel 8: total interference = 0.537451
 * ACS:  * channel 9: total interference = 0.332313
 * ACS:  * channel 10: total interference = 0.152182
 * ACS:  * channel 11: total interference = 0.0916111
 * ACS:  * channel 12: total interference = 0.0816809
 * ACS:  * channel 13: total interference = 0.0680776
 * ACS: Ideal channel is 13 (2472 MHz) with total interference factor of 0.0680776
 *
 * [1] http://en.wikipedia.org/wiki/Near_and_far_field
 */


static int acs_request_scan(struct hostapd_iface *iface);
static int acs_survey_is_sufficient(struct freq_survey *survey);


static void acs_clean_chan_surveys(struct hostapd_channel_data *chan)
{
	struct freq_survey *survey, *tmp;

	if (dl_list_empty(&chan->survey_list))
		return;

	dl_list_for_each_safe(survey, tmp, &chan->survey_list,
			      struct freq_survey, list) {
		dl_list_del(&survey->list);
		os_free(survey);
	}
}


void acs_cleanup(struct hostapd_iface *iface)
{
	int i;
	struct hostapd_channel_data *chan;

	for (i = 0; i < iface->current_mode->num_channels; i++) {
		chan = &iface->current_mode->channels[i];

		if (chan->flag & HOSTAPD_CHAN_SURVEY_LIST_INITIALIZED)
			acs_clean_chan_surveys(chan);

		dl_list_init(&chan->survey_list);
		chan->flag |= HOSTAPD_CHAN_SURVEY_LIST_INITIALIZED;
		chan->min_nf = 0;
	}

	iface->chans_surveyed = 0;
	iface->acs_num_completed_scans = 0;
}


static void acs_fail(struct hostapd_iface *iface)
{
	wpa_printf(MSG_ERROR, "ACS: Failed to start");
	acs_cleanup(iface);
	hostapd_disable_iface(iface);
}


static long double
acs_survey_interference_factor(struct freq_survey *survey, s8 min_nf)
{
	long double factor, busy, total;

	if (survey->filled & SURVEY_HAS_CHAN_TIME_BUSY)
		busy = survey->channel_time_busy;
	else if (survey->filled & SURVEY_HAS_CHAN_TIME_RX)
		busy = survey->channel_time_rx;
	else {
		/* This shouldn't really happen as survey data is checked in
		 * acs_sanity_check() */
		wpa_printf(MSG_ERROR, "ACS: Survey data missing");
		return 0;
	}

	total = survey->channel_time;

	if (survey->filled & SURVEY_HAS_CHAN_TIME_TX) {
		busy -= survey->channel_time_tx;
		total -= survey->channel_time_tx;
	}

	/* TODO: figure out the best multiplier for noise floor base */
	factor = pow(10, survey->nf / 5.0L) +
		(total ? (busy / total) : 0) *
		pow(2, pow(10, (long double) survey->nf / 10.0L) -
		    pow(10, (long double) min_nf / 10.0L));

	return factor;
}


static void
acs_survey_chan_interference_factor(struct hostapd_iface *iface,
				    struct hostapd_channel_data *chan)
{
	struct freq_survey *survey;
	unsigned int i = 0;
	long double int_factor = 0;
	unsigned count = 0;

	if (dl_list_empty(&chan->survey_list) ||
	    (chan->flag & HOSTAPD_CHAN_DISABLED))
		return;

	chan->interference_factor = 0;

	dl_list_for_each(survey, &chan->survey_list, struct freq_survey, list)
	{
		i++;

		if (!acs_survey_is_sufficient(survey)) {
			wpa_printf(MSG_DEBUG, "ACS: %d: insufficient data", i);
			continue;
		}

		count++;
		int_factor = acs_survey_interference_factor(survey,
							    iface->lowest_nf);
		chan->interference_factor += int_factor;
		wpa_printf(MSG_DEBUG, "ACS: %d: min_nf=%d interference_factor=%Lg nf=%d time=%lu busy=%lu rx=%lu",
			   i, chan->min_nf, int_factor,
			   survey->nf, (unsigned long) survey->channel_time,
			   (unsigned long) survey->channel_time_busy,
			   (unsigned long) survey->channel_time_rx);
	}

	if (count)
		chan->interference_factor /= count;
}


static int acs_usable_ht40_chan(struct hostapd_channel_data *chan)
{
	const int allowed[] = { 36, 44, 52, 60, 100, 108, 116, 124, 132, 149,
				157, 184, 192 };
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(allowed); i++)
		if (chan->chan == allowed[i])
			return 1;

	return 0;
}


static int acs_usable_vht80_chan(struct hostapd_channel_data *chan)
{
	const int allowed[] = { 36, 52, 100, 116, 132, 149 };
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(allowed); i++)
		if (chan->chan == allowed[i])
			return 1;

	return 0;
}


static int acs_survey_is_sufficient(struct freq_survey *survey)
{
	if (!(survey->filled & SURVEY_HAS_NF)) {
		wpa_printf(MSG_INFO, "ACS: Survey is missing noise floor");
		return 0;
	}

	if (!(survey->filled & SURVEY_HAS_CHAN_TIME)) {
		wpa_printf(MSG_INFO, "ACS: Survey is missing channel time");
		return 0;
	}

	if (!(survey->filled & SURVEY_HAS_CHAN_TIME_BUSY) &&
	    !(survey->filled & SURVEY_HAS_CHAN_TIME_RX)) {
		wpa_printf(MSG_INFO,
			   "ACS: Survey is missing RX and busy time (at least one is required)");
		return 0;
	}

	return 1;
}


static int acs_survey_list_is_sufficient(struct hostapd_channel_data *chan)
{
	struct freq_survey *survey;
	int ret = -1;

	dl_list_for_each(survey, &chan->survey_list, struct freq_survey, list)
	{
		if (acs_survey_is_sufficient(survey)) {
			ret = 1;
			break;
		}
		ret = 0;
	}

	if (ret == -1)
		ret = 1; /* no survey list entries */

	if (!ret) {
		wpa_printf(MSG_INFO,
			   "ACS: Channel %d has insufficient survey data",
			   chan->chan);
	}

	return ret;
}


static int acs_surveys_are_sufficient(struct hostapd_iface *iface)
{
	int i;
	struct hostapd_channel_data *chan;
	int valid = 0;

	for (i = 0; i < iface->current_mode->num_channels; i++) {
		chan = &iface->current_mode->channels[i];
		if (!(chan->flag & HOSTAPD_CHAN_DISABLED) &&
		    acs_survey_list_is_sufficient(chan))
			valid++;
	}

	/* We need at least survey data for one channel */
	return !!valid;
}


static int acs_usable_chan(struct hostapd_channel_data *chan)
{
	return !dl_list_empty(&chan->survey_list) &&
		!(chan->flag & HOSTAPD_CHAN_DISABLED) &&
		acs_survey_list_is_sufficient(chan);
}


static int is_in_chanlist(struct hostapd_iface *iface,
			  struct hostapd_channel_data *chan)
{
	if (!iface->conf->acs_ch_list.num)
		return 1;

	return freq_range_list_includes(&iface->conf->acs_ch_list, chan->chan);
}


static void acs_survey_all_chans_intereference_factor(
	struct hostapd_iface *iface)
{
	int i;
	struct hostapd_channel_data *chan;

	for (i = 0; i < iface->current_mode->num_channels; i++) {
		chan = &iface->current_mode->channels[i];

		if (!acs_usable_chan(chan))
			continue;

		if (!is_in_chanlist(iface, chan))
			continue;

		wpa_printf(MSG_DEBUG, "ACS: Survey analysis for channel %d (%d MHz)",
			   chan->chan, chan->freq);

		acs_survey_chan_interference_factor(iface, chan);

		wpa_printf(MSG_DEBUG, "ACS:  * interference factor average: %Lg",
			   chan->interference_factor);
	}
}


static struct hostapd_channel_data *acs_find_chan(struct hostapd_iface *iface,
						  int freq)
{
	struct hostapd_channel_data *chan;
	int i;

	for (i = 0; i < iface->current_mode->num_channels; i++) {
		chan = &iface->current_mode->channels[i];

		if (chan->flag & HOSTAPD_CHAN_DISABLED)
			continue;

		if (chan->freq == freq)
			return chan;
	}

	return NULL;
}


static int is_24ghz_mode(enum hostapd_hw_mode mode)
{
	return mode == HOSTAPD_MODE_IEEE80211B ||
		mode == HOSTAPD_MODE_IEEE80211G;
}


static int is_common_24ghz_chan(int chan)
{
	return chan == 1 || chan == 6 || chan == 11;
}


#ifndef ACS_ADJ_WEIGHT
#define ACS_ADJ_WEIGHT 0.85
#endif /* ACS_ADJ_WEIGHT */

#ifndef ACS_NEXT_ADJ_WEIGHT
#define ACS_NEXT_ADJ_WEIGHT 0.55
#endif /* ACS_NEXT_ADJ_WEIGHT */

#ifndef ACS_24GHZ_PREFER_1_6_11
/*
 * Select commonly used channels 1, 6, 11 by default even if a neighboring
 * channel has a smaller interference factor as long as it is not better by more
 * than this multiplier.
 */
#define ACS_24GHZ_PREFER_1_6_11 0.8
#endif /* ACS_24GHZ_PREFER_1_6_11 */

/*
 * At this point it's assumed chan->interface_factor has been computed.
 * This function should be reusable regardless of interference computation
 * option (survey, BSS, spectral, ...). chan->interference factor must be
 * summable (i.e., must be always greater than zero).
 */
static struct hostapd_channel_data *
acs_find_ideal_chan(struct hostapd_iface *iface)
{
	struct hostapd_channel_data *chan, *adj_chan, *ideal_chan = NULL,
		*rand_chan = NULL;
	long double factor, ideal_factor = 0;
	int i, j;
	int n_chans = 1;
	unsigned int k;

	/* TODO: HT40- support */

	if (iface->conf->ieee80211n &&
	    iface->conf->secondary_channel == -1) {
		wpa_printf(MSG_ERROR, "ACS: HT40- is not supported yet. Please try HT40+");
		return NULL;
	}

	if (iface->conf->ieee80211n &&
	    iface->conf->secondary_channel)
		n_chans = 2;

	if (iface->conf->ieee80211ac &&
	    iface->conf->vht_oper_chwidth == 1)
		n_chans = 4;

	/* TODO: VHT80+80, VHT160. Update acs_adjust_vht_center_freq() too. */

	wpa_printf(MSG_DEBUG, "ACS: Survey analysis for selected bandwidth %d MHz",
		   n_chans == 1 ? 20 :
		   n_chans == 2 ? 40 :
		   80);

	for (i = 0; i < iface->current_mode->num_channels; i++) {
		double total_weight;
		struct acs_bias *bias, tmp_bias;

		chan = &iface->current_mode->channels[i];

		if (chan->flag & HOSTAPD_CHAN_DISABLED)
			continue;

		if (!is_in_chanlist(iface, chan))
			continue;

		/* HT40 on 5 GHz has a limited set of primary channels as per
		 * 11n Annex J */
		if (iface->current_mode->mode == HOSTAPD_MODE_IEEE80211A &&
		    iface->conf->ieee80211n &&
		    iface->conf->secondary_channel &&
		    !acs_usable_ht40_chan(chan)) {
			wpa_printf(MSG_DEBUG, "ACS: Channel %d: not allowed as primary channel for HT40",
				   chan->chan);
			continue;
		}

		if (iface->current_mode->mode == HOSTAPD_MODE_IEEE80211A &&
		    iface->conf->ieee80211ac &&
		    iface->conf->vht_oper_chwidth == 1 &&
		    !acs_usable_vht80_chan(chan)) {
			wpa_printf(MSG_DEBUG, "ACS: Channel %d: not allowed as primary channel for VHT80",
				   chan->chan);
			continue;
		}

		factor = 0;
		if (acs_usable_chan(chan))
			factor = chan->interference_factor;
		total_weight = 1;

		for (j = 1; j < n_chans; j++) {
			adj_chan = acs_find_chan(iface, chan->freq + (j * 20));
			if (!adj_chan)
				break;

			if (acs_usable_chan(adj_chan)) {
				factor += adj_chan->interference_factor;
				total_weight += 1;
			}
		}

		if (j != n_chans) {
			wpa_printf(MSG_DEBUG, "ACS: Channel %d: not enough bandwidth",
				   chan->chan);
			continue;
		}

		/* 2.4 GHz has overlapping 20 MHz channels. Include adjacent
		 * channel interference factor. */
		if (is_24ghz_mode(iface->current_mode->mode)) {
			for (j = 0; j < n_chans; j++) {
				adj_chan = acs_find_chan(iface, chan->freq +
							 (j * 20) - 5);
				if (adj_chan && acs_usable_chan(adj_chan)) {
					factor += ACS_ADJ_WEIGHT *
						adj_chan->interference_factor;
					total_weight += ACS_ADJ_WEIGHT;
				}

				adj_chan = acs_find_chan(iface, chan->freq +
							 (j * 20) - 10);
				if (adj_chan && acs_usable_chan(adj_chan)) {
					factor += ACS_NEXT_ADJ_WEIGHT *
						adj_chan->interference_factor;
					total_weight += ACS_NEXT_ADJ_WEIGHT;
				}

				adj_chan = acs_find_chan(iface, chan->freq +
							 (j * 20) + 5);
				if (adj_chan && acs_usable_chan(adj_chan)) {
					factor += ACS_ADJ_WEIGHT *
						adj_chan->interference_factor;
					total_weight += ACS_ADJ_WEIGHT;
				}

				adj_chan = acs_find_chan(iface, chan->freq +
							 (j * 20) + 10);
				if (adj_chan && acs_usable_chan(adj_chan)) {
					factor += ACS_NEXT_ADJ_WEIGHT *
						adj_chan->interference_factor;
					total_weight += ACS_NEXT_ADJ_WEIGHT;
				}
			}
		}

		factor /= total_weight;

		bias = NULL;
		if (iface->conf->acs_chan_bias) {
			for (k = 0; k < iface->conf->num_acs_chan_bias; k++) {
				bias = &iface->conf->acs_chan_bias[k];
				if (bias->channel == chan->chan)
					break;
				bias = NULL;
			}
		} else if (is_24ghz_mode(iface->current_mode->mode) &&
			   is_common_24ghz_chan(chan->chan)) {
			tmp_bias.channel = chan->chan;
			tmp_bias.bias = ACS_24GHZ_PREFER_1_6_11;
			bias = &tmp_bias;
		}

		if (bias) {
			factor *= bias->bias;
			wpa_printf(MSG_DEBUG,
				   "ACS:  * channel %d: total interference = %Lg (%f bias)",
				   chan->chan, factor, bias->bias);
		} else {
			wpa_printf(MSG_DEBUG,
				   "ACS:  * channel %d: total interference = %Lg",
				   chan->chan, factor);
		}

		if (acs_usable_chan(chan) &&
		    (!ideal_chan || factor < ideal_factor)) {
			ideal_factor = factor;
			ideal_chan = chan;
		}

		/* This channel would at least be usable */
		if (!rand_chan)
			rand_chan = chan;
	}

	if (ideal_chan) {
		wpa_printf(MSG_DEBUG, "ACS: Ideal channel is %d (%d MHz) with total interference factor of %Lg",
			   ideal_chan->chan, ideal_chan->freq, ideal_factor);
		return ideal_chan;
	}

	return rand_chan;
}


static void acs_adjust_vht_center_freq(struct hostapd_iface *iface)
{
	int offset;

	wpa_printf(MSG_DEBUG, "ACS: Adjusting VHT center frequency");

	switch (iface->conf->vht_oper_chwidth) {
	case VHT_CHANWIDTH_USE_HT:
		offset = 2 * iface->conf->secondary_channel;
		break;
	case VHT_CHANWIDTH_80MHZ:
		offset = 6;
		break;
	default:
		/* TODO: How can this be calculated? Adjust
		 * acs_find_ideal_chan() */
		wpa_printf(MSG_INFO, "ACS: Only VHT20/40/80 is supported now");
		return;
	}

	iface->conf->vht_oper_centr_freq_seg0_idx =
		iface->conf->channel + offset;
}


static int acs_study_survey_based(struct hostapd_iface *iface)
{
	wpa_printf(MSG_DEBUG, "ACS: Trying survey-based ACS");

	if (!iface->chans_surveyed) {
		wpa_printf(MSG_ERROR, "ACS: Unable to collect survey data");
		return -1;
	}

	if (!acs_surveys_are_sufficient(iface)) {
		wpa_printf(MSG_ERROR, "ACS: Surveys have insufficient data");
		return -1;
	}

	acs_survey_all_chans_intereference_factor(iface);
	return 0;
}


static int acs_study_options(struct hostapd_iface *iface)
{
	if (acs_study_survey_based(iface) == 0)
		return 0;

	/* TODO: If no surveys are available/sufficient this is a good
	 * place to fallback to BSS-based ACS */

	return -1;
}


static void acs_study(struct hostapd_iface *iface)
{
	struct hostapd_channel_data *ideal_chan;
	int err;

	err = acs_study_options(iface);
	if (err < 0) {
		wpa_printf(MSG_ERROR, "ACS: All study options have failed");
		goto fail;
	}

	ideal_chan = acs_find_ideal_chan(iface);
	if (!ideal_chan) {
		wpa_printf(MSG_ERROR, "ACS: Failed to compute ideal channel");
		err = -1;
		goto fail;
	}

	iface->conf->channel = ideal_chan->chan;

	if (iface->conf->ieee80211ac)
		acs_adjust_vht_center_freq(iface);

	err = 0;
fail:
	/*
	 * hostapd_setup_interface_complete() will return -1 on failure,
	 * 0 on success and 0 is HOSTAPD_CHAN_VALID :)
	 */
	if (hostapd_acs_completed(iface, err) == HOSTAPD_CHAN_VALID) {
		acs_cleanup(iface);
		return;
	}

	/* This can possibly happen if channel parameters (secondary
	 * channel, center frequencies) are misconfigured */
	wpa_printf(MSG_ERROR, "ACS: Possibly channel configuration is invalid, please report this along with your config file.");
	acs_fail(iface);
}


static void acs_scan_complete(struct hostapd_iface *iface)
{
	int err;

	iface->scan_cb = NULL;

	wpa_printf(MSG_DEBUG, "ACS: Using survey based algorithm (acs_num_scans=%d)",
		   iface->conf->acs_num_scans);

	err = hostapd_drv_get_survey(iface->bss[0], 0);
	if (err) {
		wpa_printf(MSG_ERROR, "ACS: Failed to get survey data");
		goto fail;
	}

	if (++iface->acs_num_completed_scans < iface->conf->acs_num_scans) {
		err = acs_request_scan(iface);
		if (err) {
			wpa_printf(MSG_ERROR, "ACS: Failed to request scan");
			goto fail;
		}

		return;
	}

	acs_study(iface);
	return;
fail:
	hostapd_acs_completed(iface, 1);
	acs_fail(iface);
}


static int acs_request_scan(struct hostapd_iface *iface)
{
	struct wpa_driver_scan_params params;
	struct hostapd_channel_data *chan;
	int i, *freq;

	os_memset(&params, 0, sizeof(params));
	params.freqs = os_calloc(iface->current_mode->num_channels + 1,
				 sizeof(params.freqs[0]));
	if (params.freqs == NULL)
		return -1;

	freq = params.freqs;
	for (i = 0; i < iface->current_mode->num_channels; i++) {
		chan = &iface->current_mode->channels[i];
		if (chan->flag & HOSTAPD_CHAN_DISABLED)
			continue;

		if (!is_in_chanlist(iface, chan))
			continue;

		*freq++ = chan->freq;
	}
	*freq = 0;

	iface->scan_cb = acs_scan_complete;

	wpa_printf(MSG_DEBUG, "ACS: Scanning %d / %d",
		   iface->acs_num_completed_scans + 1,
		   iface->conf->acs_num_scans);

	if (hostapd_driver_scan(iface->bss[0], &params) < 0) {
		wpa_printf(MSG_ERROR, "ACS: Failed to request initial scan");
		acs_cleanup(iface);
		os_free(params.freqs);
		return -1;
	}

	os_free(params.freqs);
	return 0;
}


enum hostapd_chan_status acs_init(struct hostapd_iface *iface)
{
	wpa_printf(MSG_INFO, "ACS: Automatic channel selection started, this may take a bit");

	if (iface->drv_flags & WPA_DRIVER_FLAGS_ACS_OFFLOAD) {
		wpa_printf(MSG_INFO, "ACS: Offloading to driver");
		if (hostapd_drv_do_acs(iface->bss[0]))
			return HOSTAPD_CHAN_INVALID;
		return HOSTAPD_CHAN_ACS;
	}

	if (!iface->current_mode)
		return HOSTAPD_CHAN_INVALID;

	acs_cleanup(iface);

	if (acs_request_scan(iface) < 0)
		return HOSTAPD_CHAN_INVALID;

	hostapd_set_state(iface, HAPD_IFACE_ACS);
	wpa_msg(iface->bss[0]->msg_ctx, MSG_INFO, ACS_EVENT_STARTED);

	return HOSTAPD_CHAN_ACS;
}
