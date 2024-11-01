/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Intel Pro/Wireless 2100, 2200BG, 2915ABG network connection driver
 *
 * Copyright 2012 Stanislav Yakovlev <stas.yakovlev@gmail.com>
 */

#ifndef __IPW_H__
#define __IPW_H__

#include <linux/ieee80211.h>

static const u32 ipw_cipher_suites[] = {
	WLAN_CIPHER_SUITE_WEP40,
	WLAN_CIPHER_SUITE_WEP104,
	WLAN_CIPHER_SUITE_TKIP,
	WLAN_CIPHER_SUITE_CCMP,
};

#endif
