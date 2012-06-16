/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/types.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>
#include <net/regulatory.h>

#include <defs.h>
#include "pub.h"
#include "phy/phy_hal.h"
#include "main.h"
#include "stf.h"
#include "channel.h"
#include "mac80211_if.h"

/* QDB() macro takes a dB value and converts to a quarter dB value */
#define QDB(n) ((n) * BRCMS_TXPWR_DB_FACTOR)

#define  LOCALE_CHAN_01_11	 (1<<0)
#define  LOCALE_CHAN_12_13	 (1<<1)
#define  LOCALE_CHAN_14		 (1<<2)
#define  LOCALE_SET_5G_LOW_JP1   (1<<3)	/* 34-48, step 2 */
#define  LOCALE_SET_5G_LOW_JP2   (1<<4)	/* 34-46, step 4 */
#define  LOCALE_SET_5G_LOW1      (1<<5)	/* 36-48, step 4 */
#define  LOCALE_SET_5G_LOW2      (1<<6)	/* 52 */
#define  LOCALE_SET_5G_LOW3      (1<<7)	/* 56-64, step 4 */
#define  LOCALE_SET_5G_MID1      (1<<8)	/* 100-116, step 4 */
#define  LOCALE_SET_5G_MID2	 (1<<9)	/* 120-124, step 4 */
#define  LOCALE_SET_5G_MID3      (1<<10)	/* 128 */
#define  LOCALE_SET_5G_HIGH1     (1<<11)	/* 132-140, step 4 */
#define  LOCALE_SET_5G_HIGH2     (1<<12)	/* 149-161, step 4 */
#define  LOCALE_SET_5G_HIGH3     (1<<13)	/* 165 */
#define  LOCALE_CHAN_52_140_ALL  (1<<14)
#define  LOCALE_SET_5G_HIGH4     (1<<15)	/* 184-216 */

#define  LOCALE_CHAN_36_64	(LOCALE_SET_5G_LOW1 | \
				 LOCALE_SET_5G_LOW2 | \
				 LOCALE_SET_5G_LOW3)
#define  LOCALE_CHAN_52_64	(LOCALE_SET_5G_LOW2 | LOCALE_SET_5G_LOW3)
#define  LOCALE_CHAN_100_124	(LOCALE_SET_5G_MID1 | LOCALE_SET_5G_MID2)
#define  LOCALE_CHAN_100_140	(LOCALE_SET_5G_MID1 | LOCALE_SET_5G_MID2 | \
				  LOCALE_SET_5G_MID3 | LOCALE_SET_5G_HIGH1)
#define  LOCALE_CHAN_149_165	(LOCALE_SET_5G_HIGH2 | LOCALE_SET_5G_HIGH3)
#define  LOCALE_CHAN_184_216	LOCALE_SET_5G_HIGH4

#define  LOCALE_CHAN_01_14	(LOCALE_CHAN_01_11 | \
				 LOCALE_CHAN_12_13 | \
				 LOCALE_CHAN_14)

#define  LOCALE_RESTRICTED_NONE		  0
#define  LOCALE_RESTRICTED_SET_2G_SHORT   1

#define LOCALE_2G_IDX_i			0
#define LOCALE_5G_IDX_11		0
#define LOCALE_MIMO_IDX_bn		0
#define LOCALE_MIMO_IDX_11n		0

/* max of BAND_5G_PWR_LVLS and 6 for 2.4 GHz */
#define BRCMS_MAXPWR_TBL_SIZE		6
/* max of BAND_5G_PWR_LVLS and 14 for 2.4 GHz */
#define BRCMS_MAXPWR_MIMO_TBL_SIZE	14

/* power level in group of 2.4GHz band channels:
 * maxpwr[0] - CCK  channels [1]
 * maxpwr[1] - CCK  channels [2-10]
 * maxpwr[2] - CCK  channels [11-14]
 * maxpwr[3] - OFDM channels [1]
 * maxpwr[4] - OFDM channels [2-10]
 * maxpwr[5] - OFDM channels [11-14]
 */

/* maxpwr mapping to 5GHz band channels:
 * maxpwr[0] - channels [34-48]
 * maxpwr[1] - channels [52-60]
 * maxpwr[2] - channels [62-64]
 * maxpwr[3] - channels [100-140]
 * maxpwr[4] - channels [149-165]
 */
#define BAND_5G_PWR_LVLS	5	/* 5 power levels for 5G */

#define LC(id)	LOCALE_MIMO_IDX_ ## id

#define LC_2G(id)	LOCALE_2G_IDX_ ## id

#define LC_5G(id)	LOCALE_5G_IDX_ ## id

#define LOCALES(band2, band5, mimo2, mimo5) \
		{LC_2G(band2), LC_5G(band5), LC(mimo2), LC(mimo5)}

/* macro to get 2.4 GHz channel group index for tx power */
#define CHANNEL_POWER_IDX_2G_CCK(c) (((c) < 2) ? 0 : (((c) < 11) ? 1 : 2))
#define CHANNEL_POWER_IDX_2G_OFDM(c) (((c) < 2) ? 3 : (((c) < 11) ? 4 : 5))

/* macro to get 5 GHz channel group index for tx power */
#define CHANNEL_POWER_IDX_5G(c) (((c) < 52) ? 0 : \
				 (((c) < 62) ? 1 : \
				 (((c) < 100) ? 2 : \
				 (((c) < 149) ? 3 : 4))))

#define BRCM_2GHZ_2412_2462	REG_RULE(2412-10, 2462+10, 40, 0, 19, 0)
#define BRCM_2GHZ_2467_2472	REG_RULE(2467-10, 2472+10, 20, 0, 19, \
					 NL80211_RRF_PASSIVE_SCAN | \
					 NL80211_RRF_NO_IBSS)

#define BRCM_5GHZ_5180_5240	REG_RULE(5180-10, 5240+10, 40, 0, 21, \
					 NL80211_RRF_PASSIVE_SCAN | \
					 NL80211_RRF_NO_IBSS)
#define BRCM_5GHZ_5260_5320	REG_RULE(5260-10, 5320+10, 40, 0, 21, \
					 NL80211_RRF_PASSIVE_SCAN | \
					 NL80211_RRF_DFS | \
					 NL80211_RRF_NO_IBSS)
#define BRCM_5GHZ_5500_5700	REG_RULE(5500-10, 5700+10, 40, 0, 21, \
					 NL80211_RRF_PASSIVE_SCAN | \
					 NL80211_RRF_DFS | \
					 NL80211_RRF_NO_IBSS)
#define BRCM_5GHZ_5745_5825	REG_RULE(5745-10, 5825+10, 40, 0, 21, \
					 NL80211_RRF_PASSIVE_SCAN | \
					 NL80211_RRF_NO_IBSS)

static const struct ieee80211_regdomain brcms_regdom_x2 = {
	.n_reg_rules = 7,
	.alpha2 = "X2",
	.reg_rules = {
		BRCM_2GHZ_2412_2462,
		BRCM_2GHZ_2467_2472,
		BRCM_5GHZ_5180_5240,
		BRCM_5GHZ_5260_5320,
		BRCM_5GHZ_5500_5700,
		BRCM_5GHZ_5745_5825,
	}
};

struct brcms_cm_band {
	/* struct locale_info flags */
	u8 locale_flags;
	/* List of valid channels in the country */
	struct brcms_chanvec valid_channels;
	/* List of restricted use channels */
	const struct brcms_chanvec *restricted_channels;
};

 /* locale per-channel tx power limits for MIMO frames
  * maxpwr arrays are index by channel for 2.4 GHz limits, and
  * by sub-band for 5 GHz limits using CHANNEL_POWER_IDX_5G(channel)
  */
struct locale_mimo_info {
	/* tx 20 MHz power limits, qdBm units */
	s8 maxpwr20[BRCMS_MAXPWR_MIMO_TBL_SIZE];
	/* tx 40 MHz power limits, qdBm units */
	s8 maxpwr40[BRCMS_MAXPWR_MIMO_TBL_SIZE];
	u8 flags;
};

/* Country names and abbreviations with locale defined from ISO 3166 */
struct country_info {
	const u8 locale_2G;	/* 2.4G band locale */
	const u8 locale_5G;	/* 5G band locale */
	const u8 locale_mimo_2G;	/* 2.4G mimo info */
	const u8 locale_mimo_5G;	/* 5G mimo info */
};

struct brcms_regd {
	struct country_info country;
	const struct ieee80211_regdomain *regdomain;
};

struct brcms_cm_info {
	struct brcms_pub *pub;
	struct brcms_c_info *wlc;
	const struct brcms_regd *world_regd;
	/* per-band state (one per phy/radio) */
	struct brcms_cm_band bandstate[MAXBANDS];
	/* quiet channels currently for radar sensitivity or 11h support */
	/* channels on which we cannot transmit */
	struct brcms_chanvec quiet_channels;
};

/* locale channel and power info. */
struct locale_info {
	u32 valid_channels;
	/* List of channels used only if APs are detected */
	u8 restricted_channels;
	/* Max tx pwr in qdBm for each sub-band */
	s8 maxpwr[BRCMS_MAXPWR_TBL_SIZE];
	u8 flags;
};

/* Regulatory Matrix Spreadsheet (CLM) MIMO v3.7.9 */

/*
 * Some common channel sets
 */

/* No channels */
static const struct brcms_chanvec chanvec_none = {
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

/*
 * Restricted channel sets
 */

/* Channels 12, 13 */
static const struct brcms_chanvec restricted_set_2g_short = {
	{0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

/* global memory to provide working buffer for expanded locale */

static const struct brcms_chanvec *g_table_restricted_chan[] = {
	&chanvec_none,		/* restricted_set_none */
	&restricted_set_2g_short,
};

static const struct brcms_chanvec locale_2g_01_11 = {
	{0xfe, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

static const struct brcms_chanvec locale_2g_12_13 = {
	{0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

static const struct brcms_chanvec locale_2g_14 = {
	{0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

static const struct brcms_chanvec locale_5g_LOW_JP1 = {
	{0x00, 0x00, 0x00, 0x00, 0x54, 0x55, 0x01, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

static const struct brcms_chanvec locale_5g_LOW_JP2 = {
	{0x00, 0x00, 0x00, 0x00, 0x44, 0x44, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

static const struct brcms_chanvec locale_5g_LOW1 = {
	{0x00, 0x00, 0x00, 0x00, 0x10, 0x11, 0x01, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

static const struct brcms_chanvec locale_5g_LOW2 = {
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

static const struct brcms_chanvec locale_5g_LOW3 = {
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11,
	 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

static const struct brcms_chanvec locale_5g_MID1 = {
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x10, 0x11, 0x11, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

static const struct brcms_chanvec locale_5g_MID2 = {
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

static const struct brcms_chanvec locale_5g_MID3 = {
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

static const struct brcms_chanvec locale_5g_HIGH1 = {
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x10, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

static const struct brcms_chanvec locale_5g_HIGH2 = {
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x20, 0x22, 0x02, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

static const struct brcms_chanvec locale_5g_HIGH3 = {
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

static const struct brcms_chanvec locale_5g_52_140_ALL = {
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x11,
	 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
	 0x11, 0x11, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

static const struct brcms_chanvec locale_5g_HIGH4 = {
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11,
	 0x11, 0x11, 0x11, 0x11}
};

static const struct brcms_chanvec *g_table_locale_base[] = {
	&locale_2g_01_11,
	&locale_2g_12_13,
	&locale_2g_14,
	&locale_5g_LOW_JP1,
	&locale_5g_LOW_JP2,
	&locale_5g_LOW1,
	&locale_5g_LOW2,
	&locale_5g_LOW3,
	&locale_5g_MID1,
	&locale_5g_MID2,
	&locale_5g_MID3,
	&locale_5g_HIGH1,
	&locale_5g_HIGH2,
	&locale_5g_HIGH3,
	&locale_5g_52_140_ALL,
	&locale_5g_HIGH4
};

static void brcms_c_locale_add_channels(struct brcms_chanvec *target,
				    const struct brcms_chanvec *channels)
{
	u8 i;
	for (i = 0; i < sizeof(struct brcms_chanvec); i++)
		target->vec[i] |= channels->vec[i];
}

static void brcms_c_locale_get_channels(const struct locale_info *locale,
				    struct brcms_chanvec *channels)
{
	u8 i;

	memset(channels, 0, sizeof(struct brcms_chanvec));

	for (i = 0; i < ARRAY_SIZE(g_table_locale_base); i++) {
		if (locale->valid_channels & (1 << i))
			brcms_c_locale_add_channels(channels,
						g_table_locale_base[i]);
	}
}

/*
 * Locale Definitions - 2.4 GHz
 */
static const struct locale_info locale_i = {	/* locale i. channel 1 - 13 */
	LOCALE_CHAN_01_11 | LOCALE_CHAN_12_13,
	LOCALE_RESTRICTED_SET_2G_SHORT,
	{QDB(19), QDB(19), QDB(19),
	 QDB(19), QDB(19), QDB(19)},
	BRCMS_EIRP
};

/*
 * Locale Definitions - 5 GHz
 */
static const struct locale_info locale_11 = {
	/* locale 11. channel 36 - 48, 52 - 64, 100 - 140, 149 - 165 */
	LOCALE_CHAN_36_64 | LOCALE_CHAN_100_140 | LOCALE_CHAN_149_165,
	LOCALE_RESTRICTED_NONE,
	{QDB(21), QDB(21), QDB(21), QDB(21), QDB(21)},
	BRCMS_EIRP | BRCMS_DFS_EU
};

static const struct locale_info *g_locale_2g_table[] = {
	&locale_i
};

static const struct locale_info *g_locale_5g_table[] = {
	&locale_11
};

/*
 * MIMO Locale Definitions - 2.4 GHz
 */
static const struct locale_mimo_info locale_bn = {
	{QDB(13), QDB(13), QDB(13), QDB(13), QDB(13),
	 QDB(13), QDB(13), QDB(13), QDB(13), QDB(13),
	 QDB(13), QDB(13), QDB(13)},
	{0, 0, QDB(13), QDB(13), QDB(13),
	 QDB(13), QDB(13), QDB(13), QDB(13), QDB(13),
	 QDB(13), 0, 0},
	0
};

static const struct locale_mimo_info *g_mimo_2g_table[] = {
	&locale_bn
};

/*
 * MIMO Locale Definitions - 5 GHz
 */
static const struct locale_mimo_info locale_11n = {
	{ /* 12.5 dBm */ 50, 50, 50, QDB(15), QDB(15)},
	{QDB(14), QDB(15), QDB(15), QDB(15), QDB(15)},
	0
};

static const struct locale_mimo_info *g_mimo_5g_table[] = {
	&locale_11n
};

static const struct brcms_regd cntry_locales[] = {
	/* Worldwide RoW 2, must always be at index 0 */
	{
		.country = LOCALES(i, 11, bn, 11n),
		.regdomain = &brcms_regdom_x2,
	},
};

static const struct locale_info *brcms_c_get_locale_2g(u8 locale_idx)
{
	if (locale_idx >= ARRAY_SIZE(g_locale_2g_table))
		return NULL; /* error condition */

	return g_locale_2g_table[locale_idx];
}

static const struct locale_info *brcms_c_get_locale_5g(u8 locale_idx)
{
	if (locale_idx >= ARRAY_SIZE(g_locale_5g_table))
		return NULL; /* error condition */

	return g_locale_5g_table[locale_idx];
}

static const struct locale_mimo_info *brcms_c_get_mimo_2g(u8 locale_idx)
{
	if (locale_idx >= ARRAY_SIZE(g_mimo_2g_table))
		return NULL;

	return g_mimo_2g_table[locale_idx];
}

static const struct locale_mimo_info *brcms_c_get_mimo_5g(u8 locale_idx)
{
	if (locale_idx >= ARRAY_SIZE(g_mimo_5g_table))
		return NULL;

	return g_mimo_5g_table[locale_idx];
}

/*
 * Indicates whether the country provided is valid to pass
 * to cfg80211 or not.
 *
 * returns true if valid; false if not.
 */
static bool brcms_c_country_valid(const char *ccode)
{
	/*
	 * only allow ascii alpha uppercase for the first 2
	 * chars.
	 */
	if (!((0x80 & ccode[0]) == 0 && ccode[0] >= 0x41 && ccode[0] <= 0x5A &&
	      (0x80 & ccode[1]) == 0 && ccode[1] >= 0x41 && ccode[1] <= 0x5A &&
	      ccode[2] == '\0'))
		return false;

	/*
	 * do not match ISO 3166-1 user assigned country codes
	 * that may be in the driver table
	 */
	if (!strcmp("AA", ccode) ||        /* AA */
	    !strcmp("ZZ", ccode) ||        /* ZZ */
	    ccode[0] == 'X' ||             /* XA - XZ */
	    (ccode[0] == 'Q' &&            /* QM - QZ */
	     (ccode[1] >= 'M' && ccode[1] <= 'Z')))
		return false;

	if (!strcmp("NA", ccode))
		return false;

	return true;
}

static const struct brcms_regd *brcms_world_regd(const char *regdom, int len)
{
	const struct brcms_regd *regd = NULL;
	int i;

	for (i = 0; i < ARRAY_SIZE(cntry_locales); i++) {
		if (!strncmp(regdom, cntry_locales[i].regdomain->alpha2, len)) {
			regd = &cntry_locales[i];
			break;
		}
	}

	return regd;
}

static const struct brcms_regd *brcms_default_world_regd(void)
{
	return &cntry_locales[0];
}

/*
 * reset the quiet channels vector to the union
 * of the restricted and radar channel sets
 */
static void brcms_c_quiet_channels_reset(struct brcms_cm_info *wlc_cm)
{
	struct brcms_c_info *wlc = wlc_cm->wlc;
	uint i, j;
	struct brcms_band *band;
	const struct brcms_chanvec *chanvec;

	memset(&wlc_cm->quiet_channels, 0, sizeof(struct brcms_chanvec));

	band = wlc->band;
	for (i = 0; i < wlc->pub->_nbands;
	     i++, band = wlc->bandstate[OTHERBANDUNIT(wlc)]) {

		/* initialize quiet channels for restricted channels */
		chanvec = wlc_cm->bandstate[band->bandunit].restricted_channels;
		for (j = 0; j < sizeof(struct brcms_chanvec); j++)
			wlc_cm->quiet_channels.vec[j] |= chanvec->vec[j];

	}
}

/* Is the channel valid for the current locale and current band? */
static bool brcms_c_valid_channel20(struct brcms_cm_info *wlc_cm, uint val)
{
	struct brcms_c_info *wlc = wlc_cm->wlc;

	return ((val < MAXCHANNEL) &&
		isset(wlc_cm->bandstate[wlc->band->bandunit].valid_channels.vec,
		      val));
}

/* Is the channel valid for the current locale and specified band? */
static bool brcms_c_valid_channel20_in_band(struct brcms_cm_info *wlc_cm,
					    uint bandunit, uint val)
{
	return ((val < MAXCHANNEL)
		&& isset(wlc_cm->bandstate[bandunit].valid_channels.vec, val));
}

/* Is the channel valid for the current locale? (but don't consider channels not
 *   available due to bandlocking)
 */
static bool brcms_c_valid_channel20_db(struct brcms_cm_info *wlc_cm, uint val)
{
	struct brcms_c_info *wlc = wlc_cm->wlc;

	return brcms_c_valid_channel20(wlc->cmi, val) ||
		(!wlc->bandlocked
		 && brcms_c_valid_channel20_in_band(wlc->cmi,
						    OTHERBANDUNIT(wlc), val));
}

/* JP, J1 - J10 are Japan ccodes */
static bool brcms_c_japan_ccode(const char *ccode)
{
	return (ccode[0] == 'J' &&
		(ccode[1] == 'P' || (ccode[1] >= '1' && ccode[1] <= '9')));
}

static void
brcms_c_channel_min_txpower_limits_with_local_constraint(
		struct brcms_cm_info *wlc_cm, struct txpwr_limits *txpwr,
		u8 local_constraint_qdbm)
{
	int j;

	/* CCK Rates */
	for (j = 0; j < WL_TX_POWER_CCK_NUM; j++)
		txpwr->cck[j] = min(txpwr->cck[j], local_constraint_qdbm);

	/* 20 MHz Legacy OFDM SISO */
	for (j = 0; j < WL_TX_POWER_OFDM_NUM; j++)
		txpwr->ofdm[j] = min(txpwr->ofdm[j], local_constraint_qdbm);

	/* 20 MHz Legacy OFDM CDD */
	for (j = 0; j < BRCMS_NUM_RATES_OFDM; j++)
		txpwr->ofdm_cdd[j] =
		    min(txpwr->ofdm_cdd[j], local_constraint_qdbm);

	/* 40 MHz Legacy OFDM SISO */
	for (j = 0; j < BRCMS_NUM_RATES_OFDM; j++)
		txpwr->ofdm_40_siso[j] =
		    min(txpwr->ofdm_40_siso[j], local_constraint_qdbm);

	/* 40 MHz Legacy OFDM CDD */
	for (j = 0; j < BRCMS_NUM_RATES_OFDM; j++)
		txpwr->ofdm_40_cdd[j] =
		    min(txpwr->ofdm_40_cdd[j], local_constraint_qdbm);

	/* 20MHz MCS 0-7 SISO */
	for (j = 0; j < BRCMS_NUM_RATES_MCS_1_STREAM; j++)
		txpwr->mcs_20_siso[j] =
		    min(txpwr->mcs_20_siso[j], local_constraint_qdbm);

	/* 20MHz MCS 0-7 CDD */
	for (j = 0; j < BRCMS_NUM_RATES_MCS_1_STREAM; j++)
		txpwr->mcs_20_cdd[j] =
		    min(txpwr->mcs_20_cdd[j], local_constraint_qdbm);

	/* 20MHz MCS 0-7 STBC */
	for (j = 0; j < BRCMS_NUM_RATES_MCS_1_STREAM; j++)
		txpwr->mcs_20_stbc[j] =
		    min(txpwr->mcs_20_stbc[j], local_constraint_qdbm);

	/* 20MHz MCS 8-15 MIMO */
	for (j = 0; j < BRCMS_NUM_RATES_MCS_2_STREAM; j++)
		txpwr->mcs_20_mimo[j] =
		    min(txpwr->mcs_20_mimo[j], local_constraint_qdbm);

	/* 40MHz MCS 0-7 SISO */
	for (j = 0; j < BRCMS_NUM_RATES_MCS_1_STREAM; j++)
		txpwr->mcs_40_siso[j] =
		    min(txpwr->mcs_40_siso[j], local_constraint_qdbm);

	/* 40MHz MCS 0-7 CDD */
	for (j = 0; j < BRCMS_NUM_RATES_MCS_1_STREAM; j++)
		txpwr->mcs_40_cdd[j] =
		    min(txpwr->mcs_40_cdd[j], local_constraint_qdbm);

	/* 40MHz MCS 0-7 STBC */
	for (j = 0; j < BRCMS_NUM_RATES_MCS_1_STREAM; j++)
		txpwr->mcs_40_stbc[j] =
		    min(txpwr->mcs_40_stbc[j], local_constraint_qdbm);

	/* 40MHz MCS 8-15 MIMO */
	for (j = 0; j < BRCMS_NUM_RATES_MCS_2_STREAM; j++)
		txpwr->mcs_40_mimo[j] =
		    min(txpwr->mcs_40_mimo[j], local_constraint_qdbm);

	/* 40MHz MCS 32 */
	txpwr->mcs32 = min(txpwr->mcs32, local_constraint_qdbm);

}

/* Update the radio state (enable/disable) and tx power targets
 * based on a new set of channel/regulatory information
 */
static void brcms_c_channels_commit(struct brcms_cm_info *wlc_cm)
{
	struct brcms_c_info *wlc = wlc_cm->wlc;
	uint chan;

	/* search for the existence of any valid channel */
	for (chan = 0; chan < MAXCHANNEL; chan++) {
		if (brcms_c_valid_channel20_db(wlc->cmi, chan))
			break;
	}
	if (chan == MAXCHANNEL)
		chan = INVCHANNEL;

	/*
	 * based on the channel search above, set or
	 * clear WL_RADIO_COUNTRY_DISABLE.
	 */
	if (chan == INVCHANNEL) {
		/*
		 * country/locale with no valid channels, set
		 * the radio disable bit
		 */
		mboolset(wlc->pub->radio_disabled, WL_RADIO_COUNTRY_DISABLE);
		wiphy_err(wlc->wiphy, "wl%d: %s: no valid channel for \"%s\" "
			  "nbands %d bandlocked %d\n", wlc->pub->unit,
			  __func__, wlc_cm->world_regd->regdomain->alpha2,
			  wlc->pub->_nbands, wlc->bandlocked);
	} else if (mboolisset(wlc->pub->radio_disabled,
			      WL_RADIO_COUNTRY_DISABLE)) {
		/*
		 * country/locale with valid channel, clear
		 * the radio disable bit
		 */
		mboolclr(wlc->pub->radio_disabled, WL_RADIO_COUNTRY_DISABLE);
	}
}

static int
brcms_c_channels_init(struct brcms_cm_info *wlc_cm,
		      const struct country_info *country)
{
	struct brcms_c_info *wlc = wlc_cm->wlc;
	uint i, j;
	struct brcms_band *band;
	const struct locale_info *li;
	struct brcms_chanvec sup_chan;
	const struct locale_mimo_info *li_mimo;

	band = wlc->band;
	for (i = 0; i < wlc->pub->_nbands;
	     i++, band = wlc->bandstate[OTHERBANDUNIT(wlc)]) {

		li = (band->bandtype == BRCM_BAND_5G) ?
		    brcms_c_get_locale_5g(country->locale_5G) :
		    brcms_c_get_locale_2g(country->locale_2G);
		wlc_cm->bandstate[band->bandunit].locale_flags = li->flags;
		li_mimo = (band->bandtype == BRCM_BAND_5G) ?
		    brcms_c_get_mimo_5g(country->locale_mimo_5G) :
		    brcms_c_get_mimo_2g(country->locale_mimo_2G);

		/* merge the mimo non-mimo locale flags */
		wlc_cm->bandstate[band->bandunit].locale_flags |=
		    li_mimo->flags;

		wlc_cm->bandstate[band->bandunit].restricted_channels =
		    g_table_restricted_chan[li->restricted_channels];

		/*
		 * set the channel availability, masking out the channels
		 * that may not be supported on this phy.
		 */
		wlc_phy_chanspec_band_validch(band->pi, band->bandtype,
					      &sup_chan);
		brcms_c_locale_get_channels(li,
					&wlc_cm->bandstate[band->bandunit].
					valid_channels);
		for (j = 0; j < sizeof(struct brcms_chanvec); j++)
			wlc_cm->bandstate[band->bandunit].valid_channels.
			    vec[j] &= sup_chan.vec[j];
	}

	brcms_c_quiet_channels_reset(wlc_cm);
	brcms_c_channels_commit(wlc_cm);

	return 0;
}

/*
 * set the driver's current country and regulatory information
 * using a country code as the source. Look up built in country
 * information found with the country code.
 */
static void
brcms_c_set_country(struct brcms_cm_info *wlc_cm,
		    const struct brcms_regd *regd)
{
	const struct country_info *country = &regd->country;
	const struct locale_info *locale;
	struct brcms_c_info *wlc = wlc_cm->wlc;

	if ((wlc->pub->_n_enab & SUPPORT_11N) !=
	    wlc->protection->nmode_user)
		brcms_c_set_nmode(wlc);

	brcms_c_stf_ss_update(wlc, wlc->bandstate[BAND_2G_INDEX]);
	brcms_c_stf_ss_update(wlc, wlc->bandstate[BAND_5G_INDEX]);
	/* set or restore gmode as required by regulatory */
	locale = brcms_c_get_locale_2g(country->locale_2G);
	if (locale && (locale->flags & BRCMS_NO_OFDM))
		brcms_c_set_gmode(wlc, GMODE_LEGACY_B, false);
	else
		brcms_c_set_gmode(wlc, wlc->protection->gmode_user, false);

	brcms_c_channels_init(wlc_cm, country);

	return;
}

struct brcms_cm_info *brcms_c_channel_mgr_attach(struct brcms_c_info *wlc)
{
	struct brcms_cm_info *wlc_cm;
	struct brcms_pub *pub = wlc->pub;
	struct ssb_sprom *sprom = &wlc->hw->d11core->bus->sprom;
	const char *ccode = sprom->alpha2;
	int ccode_len = sizeof(sprom->alpha2);

	BCMMSG(wlc->wiphy, "wl%d\n", wlc->pub->unit);

	wlc_cm = kzalloc(sizeof(struct brcms_cm_info), GFP_ATOMIC);
	if (wlc_cm == NULL)
		return NULL;
	wlc_cm->pub = pub;
	wlc_cm->wlc = wlc;
	wlc->cmi = wlc_cm;

	/* store the country code for passing up as a regulatory hint */
	wlc_cm->world_regd = brcms_world_regd(ccode, ccode_len);
	if (brcms_c_country_valid(ccode))
		strncpy(wlc->pub->srom_ccode, ccode, ccode_len);

	/*
	 * If no custom world domain is found in the SROM, use the
	 * default "X2" domain.
	 */
	if (!wlc_cm->world_regd) {
		wlc_cm->world_regd = brcms_default_world_regd();
		ccode = wlc_cm->world_regd->regdomain->alpha2;
		ccode_len = BRCM_CNTRY_BUF_SZ - 1;
	}

	/* save default country for exiting 11d regulatory mode */
	strncpy(wlc->country_default, ccode, ccode_len);

	/* initialize autocountry_default to driver default */
	strncpy(wlc->autocountry_default, ccode, ccode_len);

	brcms_c_set_country(wlc_cm, wlc_cm->world_regd);

	return wlc_cm;
}

void brcms_c_channel_mgr_detach(struct brcms_cm_info *wlc_cm)
{
	kfree(wlc_cm);
}

u8
brcms_c_channel_locale_flags_in_band(struct brcms_cm_info *wlc_cm,
				     uint bandunit)
{
	return wlc_cm->bandstate[bandunit].locale_flags;
}

static bool
brcms_c_quiet_chanspec(struct brcms_cm_info *wlc_cm, u16 chspec)
{
	return (wlc_cm->wlc->pub->_n_enab & SUPPORT_11N) &&
		CHSPEC_IS40(chspec) ?
		(isset(wlc_cm->quiet_channels.vec,
		       lower_20_sb(CHSPEC_CHANNEL(chspec))) ||
		 isset(wlc_cm->quiet_channels.vec,
		       upper_20_sb(CHSPEC_CHANNEL(chspec)))) :
		isset(wlc_cm->quiet_channels.vec, CHSPEC_CHANNEL(chspec));
}

void
brcms_c_channel_set_chanspec(struct brcms_cm_info *wlc_cm, u16 chanspec,
			 u8 local_constraint_qdbm)
{
	struct brcms_c_info *wlc = wlc_cm->wlc;
	struct txpwr_limits txpwr;

	brcms_c_channel_reg_limits(wlc_cm, chanspec, &txpwr);

	brcms_c_channel_min_txpower_limits_with_local_constraint(
		wlc_cm, &txpwr, local_constraint_qdbm
	);

	brcms_b_set_chanspec(wlc->hw, chanspec,
			      (brcms_c_quiet_chanspec(wlc_cm, chanspec) != 0),
			      &txpwr);
}

void
brcms_c_channel_reg_limits(struct brcms_cm_info *wlc_cm, u16 chanspec,
		       struct txpwr_limits *txpwr)
{
	struct brcms_c_info *wlc = wlc_cm->wlc;
	uint i;
	uint chan;
	int maxpwr;
	int delta;
	const struct country_info *country;
	struct brcms_band *band;
	const struct locale_info *li;
	int conducted_max = BRCMS_TXPWR_MAX;
	int conducted_ofdm_max = BRCMS_TXPWR_MAX;
	const struct locale_mimo_info *li_mimo;
	int maxpwr20, maxpwr40;
	int maxpwr_idx;
	uint j;

	memset(txpwr, 0, sizeof(struct txpwr_limits));

	country = &wlc_cm->world_regd->country;

	chan = CHSPEC_CHANNEL(chanspec);
	band = wlc->bandstate[chspec_bandunit(chanspec)];
	li = (band->bandtype == BRCM_BAND_5G) ?
	    brcms_c_get_locale_5g(country->locale_5G) :
	    brcms_c_get_locale_2g(country->locale_2G);

	li_mimo = (band->bandtype == BRCM_BAND_5G) ?
	    brcms_c_get_mimo_5g(country->locale_mimo_5G) :
	    brcms_c_get_mimo_2g(country->locale_mimo_2G);

	if (li->flags & BRCMS_EIRP) {
		delta = band->antgain;
	} else {
		delta = 0;
		if (band->antgain > QDB(6))
			delta = band->antgain - QDB(6);	/* Excess over 6 dB */
	}

	if (li == &locale_i) {
		conducted_max = QDB(22);
		conducted_ofdm_max = QDB(22);
	}

	/* CCK txpwr limits for 2.4G band */
	if (band->bandtype == BRCM_BAND_2G) {
		maxpwr = li->maxpwr[CHANNEL_POWER_IDX_2G_CCK(chan)];

		maxpwr = maxpwr - delta;
		maxpwr = max(maxpwr, 0);
		maxpwr = min(maxpwr, conducted_max);

		for (i = 0; i < BRCMS_NUM_RATES_CCK; i++)
			txpwr->cck[i] = (u8) maxpwr;
	}

	/* OFDM txpwr limits for 2.4G or 5G bands */
	if (band->bandtype == BRCM_BAND_2G)
		maxpwr = li->maxpwr[CHANNEL_POWER_IDX_2G_OFDM(chan)];
	else
		maxpwr = li->maxpwr[CHANNEL_POWER_IDX_5G(chan)];

	maxpwr = maxpwr - delta;
	maxpwr = max(maxpwr, 0);
	maxpwr = min(maxpwr, conducted_ofdm_max);

	/* Keep OFDM lmit below CCK limit */
	if (band->bandtype == BRCM_BAND_2G)
		maxpwr = min_t(int, maxpwr, txpwr->cck[0]);

	for (i = 0; i < BRCMS_NUM_RATES_OFDM; i++)
		txpwr->ofdm[i] = (u8) maxpwr;

	for (i = 0; i < BRCMS_NUM_RATES_OFDM; i++) {
		/*
		 * OFDM 40 MHz SISO has the same power as the corresponding
		 * MCS0-7 rate unless overriden by the locale specific code.
		 * We set this value to 0 as a flag (presumably 0 dBm isn't
		 * a possibility) and then copy the MCS0-7 value to the 40 MHz
		 * value if it wasn't explicitly set.
		 */
		txpwr->ofdm_40_siso[i] = 0;

		txpwr->ofdm_cdd[i] = (u8) maxpwr;

		txpwr->ofdm_40_cdd[i] = 0;
	}

	/* MIMO/HT specific limits */
	if (li_mimo->flags & BRCMS_EIRP) {
		delta = band->antgain;
	} else {
		delta = 0;
		if (band->antgain > QDB(6))
			delta = band->antgain - QDB(6);	/* Excess over 6 dB */
	}

	if (band->bandtype == BRCM_BAND_2G)
		maxpwr_idx = (chan - 1);
	else
		maxpwr_idx = CHANNEL_POWER_IDX_5G(chan);

	maxpwr20 = li_mimo->maxpwr20[maxpwr_idx];
	maxpwr40 = li_mimo->maxpwr40[maxpwr_idx];

	maxpwr20 = maxpwr20 - delta;
	maxpwr20 = max(maxpwr20, 0);
	maxpwr40 = maxpwr40 - delta;
	maxpwr40 = max(maxpwr40, 0);

	/* Fill in the MCS 0-7 (SISO) rates */
	for (i = 0; i < BRCMS_NUM_RATES_MCS_1_STREAM; i++) {

		/*
		 * 20 MHz has the same power as the corresponding OFDM rate
		 * unless overriden by the locale specific code.
		 */
		txpwr->mcs_20_siso[i] = txpwr->ofdm[i];
		txpwr->mcs_40_siso[i] = 0;
	}

	/* Fill in the MCS 0-7 CDD rates */
	for (i = 0; i < BRCMS_NUM_RATES_MCS_1_STREAM; i++) {
		txpwr->mcs_20_cdd[i] = (u8) maxpwr20;
		txpwr->mcs_40_cdd[i] = (u8) maxpwr40;
	}

	/*
	 * These locales have SISO expressed in the
	 * table and override CDD later
	 */
	if (li_mimo == &locale_bn) {
		if (li_mimo == &locale_bn) {
			maxpwr20 = QDB(16);
			maxpwr40 = 0;

			if (chan >= 3 && chan <= 11)
				maxpwr40 = QDB(16);
		}

		for (i = 0; i < BRCMS_NUM_RATES_MCS_1_STREAM; i++) {
			txpwr->mcs_20_siso[i] = (u8) maxpwr20;
			txpwr->mcs_40_siso[i] = (u8) maxpwr40;
		}
	}

	/* Fill in the MCS 0-7 STBC rates */
	for (i = 0; i < BRCMS_NUM_RATES_MCS_1_STREAM; i++) {
		txpwr->mcs_20_stbc[i] = 0;
		txpwr->mcs_40_stbc[i] = 0;
	}

	/* Fill in the MCS 8-15 SDM rates */
	for (i = 0; i < BRCMS_NUM_RATES_MCS_2_STREAM; i++) {
		txpwr->mcs_20_mimo[i] = (u8) maxpwr20;
		txpwr->mcs_40_mimo[i] = (u8) maxpwr40;
	}

	/* Fill in MCS32 */
	txpwr->mcs32 = (u8) maxpwr40;

	for (i = 0, j = 0; i < BRCMS_NUM_RATES_OFDM; i++, j++) {
		if (txpwr->ofdm_40_cdd[i] == 0)
			txpwr->ofdm_40_cdd[i] = txpwr->mcs_40_cdd[j];
		if (i == 0) {
			i = i + 1;
			if (txpwr->ofdm_40_cdd[i] == 0)
				txpwr->ofdm_40_cdd[i] = txpwr->mcs_40_cdd[j];
		}
	}

	/*
	 * Copy the 40 MHZ MCS 0-7 CDD value to the 40 MHZ MCS 0-7 SISO
	 * value if it wasn't provided explicitly.
	 */
	for (i = 0; i < BRCMS_NUM_RATES_MCS_1_STREAM; i++) {
		if (txpwr->mcs_40_siso[i] == 0)
			txpwr->mcs_40_siso[i] = txpwr->mcs_40_cdd[i];
	}

	for (i = 0, j = 0; i < BRCMS_NUM_RATES_OFDM; i++, j++) {
		if (txpwr->ofdm_40_siso[i] == 0)
			txpwr->ofdm_40_siso[i] = txpwr->mcs_40_siso[j];
		if (i == 0) {
			i = i + 1;
			if (txpwr->ofdm_40_siso[i] == 0)
				txpwr->ofdm_40_siso[i] = txpwr->mcs_40_siso[j];
		}
	}

	/*
	 * Copy the 20 and 40 MHz MCS0-7 CDD values to the corresponding
	 * STBC values if they weren't provided explicitly.
	 */
	for (i = 0; i < BRCMS_NUM_RATES_MCS_1_STREAM; i++) {
		if (txpwr->mcs_20_stbc[i] == 0)
			txpwr->mcs_20_stbc[i] = txpwr->mcs_20_cdd[i];

		if (txpwr->mcs_40_stbc[i] == 0)
			txpwr->mcs_40_stbc[i] = txpwr->mcs_40_cdd[i];
	}

	return;
}

/*
 * Verify the chanspec is using a legal set of parameters, i.e. that the
 * chanspec specified a band, bw, ctl_sb and channel and that the
 * combination could be legal given any set of circumstances.
 * RETURNS: true is the chanspec is malformed, false if it looks good.
 */
static bool brcms_c_chspec_malformed(u16 chanspec)
{
	/* must be 2G or 5G band */
	if (!CHSPEC_IS5G(chanspec) && !CHSPEC_IS2G(chanspec))
		return true;
	/* must be 20 or 40 bandwidth */
	if (!CHSPEC_IS40(chanspec) && !CHSPEC_IS20(chanspec))
		return true;

	/* 20MHZ b/w must have no ctl sb, 40 must have a ctl sb */
	if (CHSPEC_IS20(chanspec)) {
		if (!CHSPEC_SB_NONE(chanspec))
			return true;
	} else if (!CHSPEC_SB_UPPER(chanspec) && !CHSPEC_SB_LOWER(chanspec)) {
		return true;
	}

	return false;
}

/*
 * Validate the chanspec for this locale, for 40MHZ we need to also
 * check that the sidebands are valid 20MZH channels in this locale
 * and they are also a legal HT combination
 */
static bool
brcms_c_valid_chanspec_ext(struct brcms_cm_info *wlc_cm, u16 chspec,
			   bool dualband)
{
	struct brcms_c_info *wlc = wlc_cm->wlc;
	u8 channel = CHSPEC_CHANNEL(chspec);

	/* check the chanspec */
	if (brcms_c_chspec_malformed(chspec)) {
		wiphy_err(wlc->wiphy, "wl%d: malformed chanspec 0x%x\n",
			wlc->pub->unit, chspec);
		return false;
	}

	if (CHANNEL_BANDUNIT(wlc_cm->wlc, channel) !=
	    chspec_bandunit(chspec))
		return false;

	/* Check a 20Mhz channel */
	if (CHSPEC_IS20(chspec)) {
		if (dualband)
			return brcms_c_valid_channel20_db(wlc_cm->wlc->cmi,
							  channel);
		else
			return brcms_c_valid_channel20(wlc_cm->wlc->cmi,
						       channel);
	}

	return false;
}

bool brcms_c_valid_chanspec_db(struct brcms_cm_info *wlc_cm, u16 chspec)
{
	return brcms_c_valid_chanspec_ext(wlc_cm, chspec, true);
}

static bool brcms_is_radar_freq(u16 center_freq)
{
	return center_freq >= 5260 && center_freq <= 5700;
}

static void brcms_reg_apply_radar_flags(struct wiphy *wiphy)
{
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *ch;
	int i;

	sband = wiphy->bands[IEEE80211_BAND_5GHZ];
	if (!sband)
		return;

	for (i = 0; i < sband->n_channels; i++) {
		ch = &sband->channels[i];

		if (!brcms_is_radar_freq(ch->center_freq))
			continue;

		/*
		 * All channels in this range should be passive and have
		 * DFS enabled.
		 */
		if (!(ch->flags & IEEE80211_CHAN_DISABLED))
			ch->flags |= IEEE80211_CHAN_RADAR |
				     IEEE80211_CHAN_NO_IBSS |
				     IEEE80211_CHAN_PASSIVE_SCAN;
	}
}

static void
brcms_reg_apply_beaconing_flags(struct wiphy *wiphy,
				enum nl80211_reg_initiator initiator)
{
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *ch;
	const struct ieee80211_reg_rule *rule;
	int band, i, ret;

	for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
		sband = wiphy->bands[band];
		if (!sband)
			continue;

		for (i = 0; i < sband->n_channels; i++) {
			ch = &sband->channels[i];

			if (ch->flags &
			    (IEEE80211_CHAN_DISABLED | IEEE80211_CHAN_RADAR))
				continue;

			if (initiator == NL80211_REGDOM_SET_BY_COUNTRY_IE) {
				ret = freq_reg_info(wiphy, ch->center_freq,
						    0, &rule);
				if (ret)
					continue;

				if (!(rule->flags & NL80211_RRF_NO_IBSS))
					ch->flags &= ~IEEE80211_CHAN_NO_IBSS;
				if (!(rule->flags & NL80211_RRF_PASSIVE_SCAN))
					ch->flags &=
						~IEEE80211_CHAN_PASSIVE_SCAN;
			} else if (ch->beacon_found) {
				ch->flags &= ~(IEEE80211_CHAN_NO_IBSS |
					       IEEE80211_CHAN_PASSIVE_SCAN);
			}
		}
	}
}

static int brcms_reg_notifier(struct wiphy *wiphy,
			      struct regulatory_request *request)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct brcms_info *wl = hw->priv;
	struct brcms_c_info *wlc = wl->wlc;

	brcms_reg_apply_radar_flags(wiphy);

	if (request->initiator == NL80211_REGDOM_SET_BY_COUNTRY_IE)
		brcms_reg_apply_beaconing_flags(wiphy, request->initiator);

	if (wlc->pub->_nbands > 1 || wlc->band->bandtype == BRCM_BAND_2G)
		wlc_phy_chanspec_ch14_widefilter_set(wlc->band->pi,
					brcms_c_japan_ccode(request->alpha2));

	return 0;
}

void brcms_c_regd_init(struct brcms_c_info *wlc)
{
	struct wiphy *wiphy = wlc->wiphy;
	const struct brcms_regd *regd = wlc->cmi->world_regd;
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *ch;
	struct brcms_chanvec sup_chan;
	struct brcms_band *band;
	int band_idx, i;

	/* Disable any channels not supported by the phy */
	for (band_idx = 0; band_idx < IEEE80211_NUM_BANDS; band_idx++) {
		if (band_idx == IEEE80211_BAND_2GHZ)
			band = wlc->bandstate[BAND_2G_INDEX];
		else
			band = wlc->bandstate[BAND_5G_INDEX];
		wlc_phy_chanspec_band_validch(band->pi, band->bandtype,
					      &sup_chan);

		sband = wiphy->bands[band_idx];
		for (i = 0; i < sband->n_channels; i++) {
			ch = &sband->channels[i];
			if (!isset(sup_chan.vec, ch->hw_value))
				ch->flags |= IEEE80211_CHAN_DISABLED;
		}
	}

	wlc->wiphy->reg_notifier = brcms_reg_notifier;
	wlc->wiphy->flags |= WIPHY_FLAG_CUSTOM_REGULATORY |
			     WIPHY_FLAG_STRICT_REGULATORY;
	wiphy_apply_custom_regulatory(wlc->wiphy, regd->regdomain);
	brcms_reg_apply_beaconing_flags(wiphy, NL80211_REGDOM_SET_BY_DRIVER);
}
