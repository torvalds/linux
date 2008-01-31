/*
 * Copyright (c) 2004, 2005 Reyk Floeter <reyk@vantronix.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
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

/*
 * Basic regulation domain extensions for the IEEE 802.11 stack
 */

#include <linux/kernel.h>
#include <linux/string.h>

#include "regdom.h"

static const struct ath5k_regdommap {
	enum ath5k_regdom dmn;
	enum ath5k_regdom dmn5;
	enum ath5k_regdom dmn2;
} r_map[] = {
	{ DMN_DEFAULT,		DMN_DEBUG,	DMN_DEBUG },
	{ DMN_NULL_WORLD,	DMN_NULL,	DMN_WORLD },
	{ DMN_NULL_ETSIB,	DMN_NULL,	DMN_ETSIB },
	{ DMN_NULL_ETSIC,	DMN_NULL,	DMN_ETSIC },
	{ DMN_FCC1_FCCA,	DMN_FCC1,	DMN_FCCA },
	{ DMN_FCC1_WORLD,	DMN_FCC1,	DMN_WORLD },
	{ DMN_FCC2_FCCA,	DMN_FCC2,	DMN_FCCA },
	{ DMN_FCC2_WORLD,	DMN_FCC2,	DMN_WORLD },
	{ DMN_FCC2_ETSIC,	DMN_FCC2,	DMN_ETSIC },
	{ DMN_FRANCE_NULL,	DMN_ETSI3,	DMN_ETSI3 },
	{ DMN_FCC3_FCCA,	DMN_FCC3,	DMN_WORLD },
	{ DMN_ETSI1_WORLD,	DMN_ETSI1,	DMN_WORLD },
	{ DMN_ETSI3_ETSIA,	DMN_ETSI3,	DMN_WORLD },
	{ DMN_ETSI2_WORLD,	DMN_ETSI2,	DMN_WORLD },
	{ DMN_ETSI3_WORLD,	DMN_ETSI3,	DMN_WORLD },
	{ DMN_ETSI4_WORLD,	DMN_ETSI4,	DMN_WORLD },
	{ DMN_ETSI4_ETSIC,	DMN_ETSI4,	DMN_ETSIC },
	{ DMN_ETSI5_WORLD,	DMN_ETSI5,	DMN_WORLD },
	{ DMN_ETSI6_WORLD,	DMN_ETSI6,	DMN_WORLD },
	{ DMN_ETSI_NULL,	DMN_ETSI1,	DMN_ETSI1 },
	{ DMN_MKK1_MKKA,	DMN_MKK1,	DMN_MKKA },
	{ DMN_MKK1_MKKB,	DMN_MKK1,	DMN_MKKA },
	{ DMN_APL4_WORLD,	DMN_APL4,	DMN_WORLD },
	{ DMN_MKK2_MKKA,	DMN_MKK2,	DMN_MKKA },
	{ DMN_APL_NULL,		DMN_APL1,	DMN_NULL },
	{ DMN_APL2_WORLD,	DMN_APL2,	DMN_WORLD },
	{ DMN_APL2_APLC,	DMN_APL2,	DMN_WORLD },
	{ DMN_APL3_WORLD,	DMN_APL3,	DMN_WORLD },
	{ DMN_MKK1_FCCA,	DMN_MKK1,	DMN_FCCA },
	{ DMN_APL2_APLD,	DMN_APL2,	DMN_APLD },
	{ DMN_MKK1_MKKA1,	DMN_MKK1,	DMN_MKKA },
	{ DMN_MKK1_MKKA2,	DMN_MKK1,	DMN_MKKA },
	{ DMN_APL1_WORLD,	DMN_APL1,	DMN_WORLD },
	{ DMN_APL1_FCCA,	DMN_APL1,	DMN_FCCA },
	{ DMN_APL1_APLA,	DMN_APL1,	DMN_WORLD },
	{ DMN_APL1_ETSIC,	DMN_APL1,	DMN_ETSIC },
	{ DMN_APL2_ETSIC,	DMN_APL2,	DMN_ETSIC },
	{ DMN_APL5_WORLD,	DMN_APL5,	DMN_WORLD },
	{ DMN_WOR0_WORLD,	DMN_WORLD,	DMN_WORLD },
	{ DMN_WOR1_WORLD,	DMN_WORLD,	DMN_WORLD },
	{ DMN_WOR2_WORLD,	DMN_WORLD,	DMN_WORLD },
	{ DMN_WOR3_WORLD,	DMN_WORLD,	DMN_WORLD },
	{ DMN_WOR4_WORLD,	DMN_WORLD,	DMN_WORLD },
	{ DMN_WOR5_ETSIC,	DMN_WORLD,	DMN_WORLD },
	{ DMN_WOR01_WORLD,	DMN_WORLD,	DMN_WORLD },
	{ DMN_WOR02_WORLD,	DMN_WORLD,	DMN_WORLD },
	{ DMN_EU1_WORLD,	DMN_ETSI1,	DMN_WORLD },
	{ DMN_WOR9_WORLD,	DMN_WORLD,	DMN_WORLD },
	{ DMN_WORA_WORLD,	DMN_WORLD,	DMN_WORLD },
};

enum ath5k_regdom ath5k_regdom2flag(enum ath5k_regdom dmn, u16 mhz)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(r_map); i++) {
		if (r_map[i].dmn == dmn) {
			if (mhz >= 2000 && mhz <= 3000)
				return r_map[i].dmn2;
			if (mhz >= IEEE80211_CHANNELS_5GHZ_MIN &&
					mhz <= IEEE80211_CHANNELS_5GHZ_MAX)
				return r_map[i].dmn5;
		}
	}

	return DMN_DEBUG;
}

u16 ath5k_regdom_from_ieee(enum ath5k_regdom ieee)
{
	u32 regdomain = (u32)ieee;

	/*
	 * Use the default regulation domain if the value is empty
	 * or not supported by the net80211 regulation code.
	 */
	if (ath5k_regdom2flag(regdomain, IEEE80211_CHANNELS_5GHZ_MIN) ==
			DMN_DEBUG)
		return (u16)AR5K_TUNE_REGDOMAIN;

	/* It is supported, just return the value */
	return regdomain;
}

enum ath5k_regdom ath5k_regdom_to_ieee(u16 regdomain)
{
	enum ath5k_regdom ieee = (enum ath5k_regdom)regdomain;

	return ieee;
}

