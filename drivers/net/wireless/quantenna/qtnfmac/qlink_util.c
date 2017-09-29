/*
 * Copyright (c) 2015-2016 Quantenna Communications, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/nl80211.h>

#include "qlink_util.h"

u16 qlink_iface_type_to_nl_mask(u16 qlink_type)
{
	u16 result = 0;

	switch (qlink_type) {
	case QLINK_IFTYPE_AP:
		result |= BIT(NL80211_IFTYPE_AP);
		break;
	case QLINK_IFTYPE_STATION:
		result |= BIT(NL80211_IFTYPE_STATION);
		break;
	case QLINK_IFTYPE_ADHOC:
		result |= BIT(NL80211_IFTYPE_ADHOC);
		break;
	case QLINK_IFTYPE_MONITOR:
		result |= BIT(NL80211_IFTYPE_MONITOR);
		break;
	case QLINK_IFTYPE_WDS:
		result |= BIT(NL80211_IFTYPE_WDS);
		break;
	case QLINK_IFTYPE_AP_VLAN:
		result |= BIT(NL80211_IFTYPE_AP_VLAN);
		break;
	}

	return result;
}

u8 qlink_chan_width_mask_to_nl(u16 qlink_mask)
{
	u8 result = 0;

	if (qlink_mask & QLINK_CHAN_WIDTH_5)
		result |= BIT(NL80211_CHAN_WIDTH_5);

	if (qlink_mask & QLINK_CHAN_WIDTH_10)
		result |= BIT(NL80211_CHAN_WIDTH_10);

	if (qlink_mask & QLINK_CHAN_WIDTH_20_NOHT)
		result |= BIT(NL80211_CHAN_WIDTH_20_NOHT);

	if (qlink_mask & QLINK_CHAN_WIDTH_20)
		result |= BIT(NL80211_CHAN_WIDTH_20);

	if (qlink_mask & QLINK_CHAN_WIDTH_40)
		result |= BIT(NL80211_CHAN_WIDTH_40);

	if (qlink_mask & QLINK_CHAN_WIDTH_80)
		result |= BIT(NL80211_CHAN_WIDTH_80);

	if (qlink_mask & QLINK_CHAN_WIDTH_80P80)
		result |= BIT(NL80211_CHAN_WIDTH_80P80);

	if (qlink_mask & QLINK_CHAN_WIDTH_160)
		result |= BIT(NL80211_CHAN_WIDTH_160);

	return result;
}
