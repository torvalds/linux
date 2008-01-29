/* ZD1211 USB-WLAN driver for Linux
 *
 * Copyright (C) 2005-2007 Ulrich Kunitz <kune@deine-taler.de>
 * Copyright (C) 2006-2007 Daniel Drake <dsd@gentoo.org>
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
 * In the long term, we'll probably find a better way of handling regulatory
 * requirements outside of the driver.
 */

#include <linux/kernel.h>
#include <net/mac80211.h>

#include "zd_ieee80211.h"
#include "zd_mac.h"

struct channel_range {
	u8 regdomain;
	u8 start;
	u8 end; /* exclusive (channel must be less than end) */
};

static const struct channel_range channel_ranges[] = {
	{ ZD_REGDOMAIN_FCC,		1, 12 },
	{ ZD_REGDOMAIN_IC,		1, 12 },
	{ ZD_REGDOMAIN_ETSI,		1, 14 },
	{ ZD_REGDOMAIN_JAPAN,		1, 14 },
	{ ZD_REGDOMAIN_SPAIN,		1, 14 },
	{ ZD_REGDOMAIN_FRANCE,		1, 14 },

	/* Japan originally only had channel 14 available (see CHNL_ID 0x40 in
	 * 802.11). However, in 2001 the range was extended to include channels
	 * 1-13. The ZyDAS devices still use the old region code but are
	 * designed to allow the extra channel access in Japan. */
	{ ZD_REGDOMAIN_JAPAN_ADD,	1, 15 },
};

static const struct channel_range *zd_channel_range(u8 regdomain)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(channel_ranges); i++) {
		const struct channel_range *range = &channel_ranges[i];
		if (range->regdomain == regdomain)
			return range;
	}
	return NULL;
}

#define CHAN_TO_IDX(chan) ((chan) - 1)

static void unmask_bg_channels(struct ieee80211_hw *hw,
	const struct channel_range *range,
	struct ieee80211_hw_mode *mode)
{
	u8 channel;

	for (channel = range->start; channel < range->end; channel++) {
		struct ieee80211_channel *chan =
			&mode->channels[CHAN_TO_IDX(channel)];
		chan->flag |= IEEE80211_CHAN_W_SCAN |
			IEEE80211_CHAN_W_ACTIVE_SCAN |
			IEEE80211_CHAN_W_IBSS;
	}
}

void zd_geo_init(struct ieee80211_hw *hw, u8 regdomain)
{
	struct zd_mac *mac = zd_hw_mac(hw);
	const struct channel_range *range;

	dev_dbg(zd_mac_dev(mac), "regdomain %#02x\n", regdomain);

	range = zd_channel_range(regdomain);
	if (!range) {
		/* The vendor driver overrides the regulatory domain and
		 * allowed channel registers and unconditionally restricts
		 * available channels to 1-11 everywhere. Match their
		 * questionable behaviour only for regdomains which we don't
		 * recognise. */
		dev_warn(zd_mac_dev(mac), "Unrecognised regulatory domain: "
			"%#02x. Defaulting to FCC.\n", regdomain);
		range = zd_channel_range(ZD_REGDOMAIN_FCC);
	}

	unmask_bg_channels(hw, range, &mac->modes[0]);
	unmask_bg_channels(hw, range, &mac->modes[1]);
}

