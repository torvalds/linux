/* zd_ieee80211.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/*
 * A lot of this code is generic and should be moved into the upper layers
 * at some point.
 */

#include <linux/errno.h>
#include <linux/wireless.h>
#include <linux/kernel.h>
#include <net/ieee80211.h>

#include "zd_def.h"
#include "zd_ieee80211.h"
#include "zd_mac.h"

static const struct channel_range channel_ranges[] = {
	[0]			 = { 0,  0},
	[ZD_REGDOMAIN_FCC]	 = { 1, 12},
	[ZD_REGDOMAIN_IC]	 = { 1, 12},
	[ZD_REGDOMAIN_ETSI]	 = { 1, 14},
	[ZD_REGDOMAIN_JAPAN]	 = { 1, 14},
	[ZD_REGDOMAIN_SPAIN]	 = { 1, 14},
	[ZD_REGDOMAIN_FRANCE]	 = { 1, 14},

	/* Japan originally only had channel 14 available (see CHNL_ID 0x40 in
	 * 802.11). However, in 2001 the range was extended to include channels
	 * 1-13. The ZyDAS devices still use the old region code but are
	 * designed to allow the extra channel access in Japan. */
	[ZD_REGDOMAIN_JAPAN_ADD] = { 1, 15},
};

const struct channel_range *zd_channel_range(u8 regdomain)
{
	if (regdomain >= ARRAY_SIZE(channel_ranges))
		regdomain = 0;
	return &channel_ranges[regdomain];
}

int zd_regdomain_supports_channel(u8 regdomain, u8 channel)
{
	const struct channel_range *range = zd_channel_range(regdomain);
	return range->start <= channel && channel < range->end;
}

int zd_regdomain_supported(u8 regdomain)
{
	const struct channel_range *range = zd_channel_range(regdomain);
	return range->start != 0;
}

/* Stores channel frequencies in MHz. */
static const u16 channel_frequencies[] = {
	2412, 2417, 2422, 2427, 2432, 2437, 2442, 2447,
	2452, 2457, 2462, 2467, 2472, 2484,
};

#define NUM_CHANNELS ARRAY_SIZE(channel_frequencies)

static int compute_freq(struct iw_freq *freq, u32 mhz, u32 hz)
{
	u32 factor;

	freq->e = 0;
	if (mhz >= 1000000000U) {
		pr_debug("zd1211 mhz %u to large\n", mhz);
		freq->m = 0;
		return -EINVAL;
	}

	factor = 1000;
	while (mhz >= factor) {

		freq->e += 1;
		factor *= 10;
	}

	factor /= 1000U;
	freq->m = mhz * (1000000U/factor) + hz/factor;

	return 0;
}

int zd_channel_to_freq(struct iw_freq *freq, u8 channel)
{
	if (channel > NUM_CHANNELS) {
		freq->m = 0;
		freq->e = 0;
		return -EINVAL;
	}
	if (!channel) {
		freq->m = 0;
		freq->e = 0;
		return -EINVAL;
	}
	return compute_freq(freq, channel_frequencies[channel-1], 0);
}

static int freq_to_mhz(const struct iw_freq *freq)
{
	u32 factor;
	int e;

	/* Such high frequencies are not supported. */
	if (freq->e > 6)
		return -EINVAL;

	factor = 1;
	for (e = freq->e; e > 0; --e) {
		factor *= 10;
	}
	factor = 1000000U / factor;

	if (freq->m % factor) {
		return -EINVAL;
	}

	return freq->m / factor;
}

int zd_find_channel(u8 *channel, const struct iw_freq *freq)
{
	int i, r;
	u32 mhz;

	if (freq->m < 1000) {
		if (freq->m  > NUM_CHANNELS || freq->m == 0)
			return -EINVAL;
		*channel = freq->m;
		return 1;
	}

	r = freq_to_mhz(freq);
	if (r < 0)
		return r;
	mhz = r;

	for (i = 0; i < NUM_CHANNELS; i++) {
		if (mhz == channel_frequencies[i]) {
			*channel = i+1;
			return 1;
		}
	}

	return -EINVAL;
}

int zd_geo_init(struct ieee80211_device *ieee, u8 regdomain)
{
	struct ieee80211_geo geo;
	const struct channel_range *range;
	int i;
	u8 channel;

	dev_dbg(zd_mac_dev(zd_netdev_mac(ieee->dev)),
		"regdomain %#04x\n", regdomain);

	range = zd_channel_range(regdomain);
	if (range->start == 0) {
		dev_err(zd_mac_dev(zd_netdev_mac(ieee->dev)),
			"zd1211 regdomain %#04x not supported\n",
			regdomain);
		return -EINVAL;
	}

	memset(&geo, 0, sizeof(geo));

	for (i = 0, channel = range->start; channel < range->end; channel++) {
		struct ieee80211_channel *chan = &geo.bg[i++];
		chan->freq = channel_frequencies[channel - 1];
		chan->channel = channel;
	}

	geo.bg_channels = i;
	memcpy(geo.name, "XX ", 4);
	ieee80211_set_geo(ieee, &geo);
	return 0;
}
