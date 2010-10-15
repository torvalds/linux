/*
 * Copyright (c) 2009 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "ath.h"
#include "debug.h"

void ath_print(struct ath_common *common, int dbg_mask, const char *fmt, ...)
{
	va_list args;

	if (likely(!(common->debug_mask & dbg_mask)))
		return;

	va_start(args, fmt);
	printk(KERN_DEBUG "ath: ");
	vprintk(fmt, args);
	va_end(args);
}
EXPORT_SYMBOL(ath_print);

const char *ath_opmode_to_string(enum nl80211_iftype opmode)
{
	switch (opmode) {
	case NL80211_IFTYPE_UNSPECIFIED:
		return "UNSPEC";
	case NL80211_IFTYPE_ADHOC:
		return "ADHOC";
	case NL80211_IFTYPE_STATION:
		return "STATION";
	case NL80211_IFTYPE_AP:
		return "AP";
	case NL80211_IFTYPE_AP_VLAN:
		return "AP-VLAN";
	case NL80211_IFTYPE_WDS:
		return "WDS";
	case NL80211_IFTYPE_MONITOR:
		return "MONITOR";
	case NL80211_IFTYPE_MESH_POINT:
		return "MESH";
	case NL80211_IFTYPE_P2P_CLIENT:
		return "P2P-CLIENT";
	case NL80211_IFTYPE_P2P_GO:
		return "P2P-GO";
	default:
		return "UNKNOWN";
	}
}
EXPORT_SYMBOL(ath_opmode_to_string);
