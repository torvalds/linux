/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
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
 *
 * File: channel.c
 *
 * Purpose: Channel number mapping
 *
 * Author: Lucas Lin
 *
 * Date: Dec 24, 2004
 *
 *
 *
 * Revision History:
 *	01-18-2005	RobertYu:	remove the for loop searching in
 *					ChannelValid, change ChannelRuleTab
 *					to lookup-type, reorder table items.
 *
 *
 */

#include "device.h"
#include "channel.h"
#include "rf.h"

static struct ieee80211_rate vnt_rates_bg[] = {
	{ .bitrate = 10,  .hw_value = RATE_1M },
	{ .bitrate = 20,  .hw_value = RATE_2M },
	{ .bitrate = 55,  .hw_value = RATE_5M },
	{ .bitrate = 110, .hw_value = RATE_11M },
	{ .bitrate = 60,  .hw_value = RATE_6M },
	{ .bitrate = 90,  .hw_value = RATE_9M },
	{ .bitrate = 120, .hw_value = RATE_12M },
	{ .bitrate = 180, .hw_value = RATE_18M },
	{ .bitrate = 240, .hw_value = RATE_24M },
	{ .bitrate = 360, .hw_value = RATE_36M },
	{ .bitrate = 480, .hw_value = RATE_48M },
	{ .bitrate = 540, .hw_value = RATE_54M },
};

static struct ieee80211_rate vnt_rates_a[] = {
	{ .bitrate = 60,  .hw_value = RATE_6M },
	{ .bitrate = 90,  .hw_value = RATE_9M },
	{ .bitrate = 120, .hw_value = RATE_12M },
	{ .bitrate = 180, .hw_value = RATE_18M },
	{ .bitrate = 240, .hw_value = RATE_24M },
	{ .bitrate = 360, .hw_value = RATE_36M },
	{ .bitrate = 480, .hw_value = RATE_48M },
	{ .bitrate = 540, .hw_value = RATE_54M },
};

static struct ieee80211_channel vnt_channels_2ghz[] = {
	{ .center_freq = 2412, .hw_value = 1 },
	{ .center_freq = 2417, .hw_value = 2 },
	{ .center_freq = 2422, .hw_value = 3 },
	{ .center_freq = 2427, .hw_value = 4 },
	{ .center_freq = 2432, .hw_value = 5 },
	{ .center_freq = 2437, .hw_value = 6 },
	{ .center_freq = 2442, .hw_value = 7 },
	{ .center_freq = 2447, .hw_value = 8 },
	{ .center_freq = 2452, .hw_value = 9 },
	{ .center_freq = 2457, .hw_value = 10 },
	{ .center_freq = 2462, .hw_value = 11 },
	{ .center_freq = 2467, .hw_value = 12 },
	{ .center_freq = 2472, .hw_value = 13 },
	{ .center_freq = 2484, .hw_value = 14 }
};

static struct ieee80211_channel vnt_channels_5ghz[] = {
	{ .center_freq = 4915, .hw_value = 15 },
	{ .center_freq = 4920, .hw_value = 16 },
	{ .center_freq = 4925, .hw_value = 17 },
	{ .center_freq = 4935, .hw_value = 18 },
	{ .center_freq = 4940, .hw_value = 19 },
	{ .center_freq = 4945, .hw_value = 20 },
	{ .center_freq = 4960, .hw_value = 21 },
	{ .center_freq = 4980, .hw_value = 22 },
	{ .center_freq = 5035, .hw_value = 23 },
	{ .center_freq = 5040, .hw_value = 24 },
	{ .center_freq = 5045, .hw_value = 25 },
	{ .center_freq = 5055, .hw_value = 26 },
	{ .center_freq = 5060, .hw_value = 27 },
	{ .center_freq = 5080, .hw_value = 28 },
	{ .center_freq = 5170, .hw_value = 29 },
	{ .center_freq = 5180, .hw_value = 30 },
	{ .center_freq = 5190, .hw_value = 31 },
	{ .center_freq = 5200, .hw_value = 32 },
	{ .center_freq = 5210, .hw_value = 33 },
	{ .center_freq = 5220, .hw_value = 34 },
	{ .center_freq = 5230, .hw_value = 35 },
	{ .center_freq = 5240, .hw_value = 36 },
	{ .center_freq = 5260, .hw_value = 37 },
	{ .center_freq = 5280, .hw_value = 38 },
	{ .center_freq = 5300, .hw_value = 39 },
	{ .center_freq = 5320, .hw_value = 40 },
	{ .center_freq = 5500, .hw_value = 41 },
	{ .center_freq = 5520, .hw_value = 42 },
	{ .center_freq = 5540, .hw_value = 43 },
	{ .center_freq = 5560, .hw_value = 44 },
	{ .center_freq = 5580, .hw_value = 45 },
	{ .center_freq = 5600, .hw_value = 46 },
	{ .center_freq = 5620, .hw_value = 47 },
	{ .center_freq = 5640, .hw_value = 48 },
	{ .center_freq = 5660, .hw_value = 49 },
	{ .center_freq = 5680, .hw_value = 50 },
	{ .center_freq = 5700, .hw_value = 51 },
	{ .center_freq = 5745, .hw_value = 52 },
	{ .center_freq = 5765, .hw_value = 53 },
	{ .center_freq = 5785, .hw_value = 54 },
	{ .center_freq = 5805, .hw_value = 55 },
	{ .center_freq = 5825, .hw_value = 56 }
};

static struct ieee80211_supported_band vnt_supported_2ghz_band = {
	.channels = vnt_channels_2ghz,
	.n_channels = ARRAY_SIZE(vnt_channels_2ghz),
	.bitrates = vnt_rates_bg,
	.n_bitrates = ARRAY_SIZE(vnt_rates_bg),
};

static struct ieee80211_supported_band vnt_supported_5ghz_band = {
	.channels = vnt_channels_5ghz,
	.n_channels = ARRAY_SIZE(vnt_channels_5ghz),
	.bitrates = vnt_rates_a,
	.n_bitrates = ARRAY_SIZE(vnt_rates_a),
};

void vnt_init_bands(struct vnt_private *priv)
{
	struct ieee80211_channel *ch;
	int i;

	switch (priv->rf_type) {
	case RF_AIROHA7230:
	case RF_VT3342A0:
	default:
		ch = vnt_channels_5ghz;

		for (i = 0; i < ARRAY_SIZE(vnt_channels_5ghz); i++) {
			ch[i].max_power = VNT_RF_MAX_POWER;
			ch[i].flags = IEEE80211_CHAN_NO_HT40;
		}

		priv->hw->wiphy->bands[NL80211_BAND_5GHZ] =
						&vnt_supported_5ghz_band;
	/* fallthrough */
	case RF_AL2230:
	case RF_AL2230S:
	case RF_VT3226:
	case RF_VT3226D0:
		ch = vnt_channels_2ghz;

		for (i = 0; i < ARRAY_SIZE(vnt_channels_2ghz); i++) {
			ch[i].max_power = VNT_RF_MAX_POWER;
			ch[i].flags = IEEE80211_CHAN_NO_HT40;
		}

		priv->hw->wiphy->bands[NL80211_BAND_2GHZ] =
						&vnt_supported_2ghz_band;
		break;
	}
}
