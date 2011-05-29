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

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>

#include <bcmdefs.h>
#include <bcmutils.h>
#include <siutils.h>
#include <sbhnddma.h>
#include <wlioctl.h>

#include "wlc_types.h"
#include "d11.h"
#include "wlc_cfg.h"
#include "wlc_scb.h"
#include "wlc_pub.h"
#include "wlc_key.h"
#include "phy/wlc_phy_hal.h"
#include "wlc_bmac.h"
#include "wlc_rate.h"
#include "wlc_channel.h"
#include "wlc_main.h"
#include "wlc_stf.h"
#include "wl_dbg.h"

#define	VALID_CHANNEL20_DB(wlc, val) wlc_valid_channel20_db((wlc)->cmi, val)
#define	VALID_CHANNEL20_IN_BAND(wlc, bandunit, val) \
	wlc_valid_channel20_in_band((wlc)->cmi, bandunit, val)
#define	VALID_CHANNEL20(wlc, val) wlc_valid_channel20((wlc)->cmi, val)

typedef struct wlc_cm_band {
	u8 locale_flags;	/* locale_info_t flags */
	chanvec_t valid_channels;	/* List of valid channels in the country */
	const chanvec_t *restricted_channels;	/* List of restricted use channels */
	const chanvec_t *radar_channels;	/* List of radar sensitive channels */
	u8 PAD[8];
} wlc_cm_band_t;

struct wlc_cm_info {
	struct wlc_pub *pub;
	struct wlc_info *wlc;
	char srom_ccode[WLC_CNTRY_BUF_SZ];	/* Country Code in SROM */
	uint srom_regrev;	/* Regulatory Rev for the SROM ccode */
	const country_info_t *country;	/* current country def */
	char ccode[WLC_CNTRY_BUF_SZ];	/* current internal Country Code */
	uint regrev;		/* current Regulatory Revision */
	char country_abbrev[WLC_CNTRY_BUF_SZ];	/* current advertised ccode */
	wlc_cm_band_t bandstate[MAXBANDS];	/* per-band state (one per phy/radio) */
	/* quiet channels currently for radar sensitivity or 11h support */
	chanvec_t quiet_channels;	/* channels on which we cannot transmit */
};

static int wlc_channels_init(wlc_cm_info_t *wlc_cm,
			     const country_info_t *country);
static void wlc_set_country_common(wlc_cm_info_t *wlc_cm,
				   const char *country_abbrev,
				   const char *ccode, uint regrev,
				   const country_info_t *country);
static int wlc_set_countrycode(wlc_cm_info_t *wlc_cm, const char *ccode);
static int wlc_set_countrycode_rev(wlc_cm_info_t *wlc_cm,
				   const char *country_abbrev,
				   const char *ccode, int regrev);
static int wlc_country_aggregate_map(wlc_cm_info_t *wlc_cm, const char *ccode,
				     char *mapped_ccode, uint *mapped_regrev);
static const country_info_t *wlc_country_lookup_direct(const char *ccode,
						       uint regrev);
static const country_info_t *wlc_countrycode_map(wlc_cm_info_t *wlc_cm,
						 const char *ccode,
						 char *mapped_ccode,
						 uint *mapped_regrev);
static void wlc_channels_commit(wlc_cm_info_t *wlc_cm);
static void wlc_quiet_channels_reset(wlc_cm_info_t *wlc_cm);
static bool wlc_quiet_chanspec(wlc_cm_info_t *wlc_cm, chanspec_t chspec);
static bool wlc_valid_channel20_db(wlc_cm_info_t *wlc_cm, uint val);
static bool wlc_valid_channel20_in_band(wlc_cm_info_t *wlc_cm, uint bandunit,
					uint val);
static bool wlc_valid_channel20(wlc_cm_info_t *wlc_cm, uint val);
static const country_info_t *wlc_country_lookup(struct wlc_info *wlc,
						const char *ccode);
static void wlc_locale_get_channels(const locale_info_t *locale,
				    chanvec_t *valid_channels);
static const locale_info_t *wlc_get_locale_2g(u8 locale_idx);
static const locale_info_t *wlc_get_locale_5g(u8 locale_idx);
static bool wlc_japan(struct wlc_info *wlc);
static bool wlc_japan_ccode(const char *ccode);
static void wlc_channel_min_txpower_limits_with_local_constraint(wlc_cm_info_t *
								 wlc_cm,
								 struct
								 txpwr_limits
								 *txpwr,
								 u8
								 local_constraint_qdbm);
void wlc_locale_add_channels(chanvec_t *target, const chanvec_t *channels);
static const locale_mimo_info_t *wlc_get_mimo_2g(u8 locale_idx);
static const locale_mimo_info_t *wlc_get_mimo_5g(u8 locale_idx);

/* QDB() macro takes a dB value and converts to a quarter dB value */
#ifdef QDB
#undef QDB
#endif
#define QDB(n) ((n) * WLC_TXPWR_DB_FACTOR)

/* Regulatory Matrix Spreadsheet (CLM) MIMO v3.7.9 */

/*
 * Some common channel sets
 */

/* No channels */
static const chanvec_t chanvec_none = {
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

/* All 2.4 GHz HW channels */
const chanvec_t chanvec_all_2G = {
	{0xfe, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

/* All 5 GHz HW channels */
const chanvec_t chanvec_all_5G = {
	{0x00, 0x00, 0x00, 0x00, 0x54, 0x55, 0x11, 0x11,
	 0x01, 0x00, 0x00, 0x00, 0x10, 0x11, 0x11, 0x11,
	 0x11, 0x11, 0x20, 0x22, 0x22, 0x00, 0x00, 0x11,
	 0x11, 0x11, 0x11, 0x01}
};

/*
 * Radar channel sets
 */

/* No radar */
#define radar_set_none chanvec_none

static const chanvec_t radar_set1 = {	/* Channels 52 - 64, 100 - 140 */
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x11,	/* 52 - 60 */
	 0x01, 0x00, 0x00, 0x00, 0x10, 0x11, 0x11, 0x11,	/* 64, 100 - 124 */
	 0x11, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* 128 - 140 */
	 0x00, 0x00, 0x00, 0x00}
};

/*
 * Restricted channel sets
 */

#define restricted_set_none chanvec_none

/* Channels 34, 38, 42, 46 */
static const chanvec_t restricted_set_japan_legacy = {
	{0x00, 0x00, 0x00, 0x00, 0x44, 0x44, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

/* Channels 12, 13 */
static const chanvec_t restricted_set_2g_short = {
	{0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

/* Channel 165 */
static const chanvec_t restricted_chan_165 = {
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

/* Channels 36 - 48 & 149 - 165 */
static const chanvec_t restricted_low_hi = {
	{0x00, 0x00, 0x00, 0x00, 0x10, 0x11, 0x01, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x20, 0x22, 0x22, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

/* Channels 12 - 14 */
static const chanvec_t restricted_set_12_13_14 = {
	{0x00, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

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

#define  LOCALE_CHAN_36_64       (LOCALE_SET_5G_LOW1 | LOCALE_SET_5G_LOW2 | LOCALE_SET_5G_LOW3)
#define  LOCALE_CHAN_52_64       (LOCALE_SET_5G_LOW2 | LOCALE_SET_5G_LOW3)
#define  LOCALE_CHAN_100_124	 (LOCALE_SET_5G_MID1 | LOCALE_SET_5G_MID2)
#define  LOCALE_CHAN_100_140     \
	(LOCALE_SET_5G_MID1 | LOCALE_SET_5G_MID2 | LOCALE_SET_5G_MID3 | LOCALE_SET_5G_HIGH1)
#define  LOCALE_CHAN_149_165     (LOCALE_SET_5G_HIGH2 | LOCALE_SET_5G_HIGH3)
#define  LOCALE_CHAN_184_216     LOCALE_SET_5G_HIGH4

#define  LOCALE_CHAN_01_14	(LOCALE_CHAN_01_11 | LOCALE_CHAN_12_13 | LOCALE_CHAN_14)

#define  LOCALE_RADAR_SET_NONE		  0
#define  LOCALE_RADAR_SET_1		  1

#define  LOCALE_RESTRICTED_NONE		  0
#define  LOCALE_RESTRICTED_SET_2G_SHORT   1
#define  LOCALE_RESTRICTED_CHAN_165       2
#define  LOCALE_CHAN_ALL_5G		  3
#define  LOCALE_RESTRICTED_JAPAN_LEGACY   4
#define  LOCALE_RESTRICTED_11D_2G	  5
#define  LOCALE_RESTRICTED_11D_5G	  6
#define  LOCALE_RESTRICTED_LOW_HI	  7
#define  LOCALE_RESTRICTED_12_13_14	  8

/* global memory to provide working buffer for expanded locale */

static const chanvec_t *g_table_radar_set[] = {
	&chanvec_none,
	&radar_set1
};

static const chanvec_t *g_table_restricted_chan[] = {
	&chanvec_none,		/* restricted_set_none */
	&restricted_set_2g_short,
	&restricted_chan_165,
	&chanvec_all_5G,
	&restricted_set_japan_legacy,
	&chanvec_all_2G,	/* restricted_set_11d_2G */
	&chanvec_all_5G,	/* restricted_set_11d_5G */
	&restricted_low_hi,
	&restricted_set_12_13_14
};

static const chanvec_t locale_2g_01_11 = {
	{0xfe, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

static const chanvec_t locale_2g_12_13 = {
	{0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

static const chanvec_t locale_2g_14 = {
	{0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

static const chanvec_t locale_5g_LOW_JP1 = {
	{0x00, 0x00, 0x00, 0x00, 0x54, 0x55, 0x01, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

static const chanvec_t locale_5g_LOW_JP2 = {
	{0x00, 0x00, 0x00, 0x00, 0x44, 0x44, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

static const chanvec_t locale_5g_LOW1 = {
	{0x00, 0x00, 0x00, 0x00, 0x10, 0x11, 0x01, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

static const chanvec_t locale_5g_LOW2 = {
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

static const chanvec_t locale_5g_LOW3 = {
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11,
	 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

static const chanvec_t locale_5g_MID1 = {
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x10, 0x11, 0x11, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

static const chanvec_t locale_5g_MID2 = {
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

static const chanvec_t locale_5g_MID3 = {
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

static const chanvec_t locale_5g_HIGH1 = {
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x10, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

static const chanvec_t locale_5g_HIGH2 = {
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x20, 0x22, 0x02, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

static const chanvec_t locale_5g_HIGH3 = {
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

static const chanvec_t locale_5g_52_140_ALL = {
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x11,
	 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
	 0x11, 0x11, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00}
};

static const chanvec_t locale_5g_HIGH4 = {
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11,
	 0x11, 0x11, 0x11, 0x11}
};

static const chanvec_t *g_table_locale_base[] = {
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

void wlc_locale_add_channels(chanvec_t *target, const chanvec_t *channels)
{
	u8 i;
	for (i = 0; i < sizeof(chanvec_t); i++) {
		target->vec[i] |= channels->vec[i];
	}
}

static void wlc_locale_get_channels(const locale_info_t *locale,
				    chanvec_t *channels)
{
	u8 i;

	memset(channels, 0, sizeof(chanvec_t));

	for (i = 0; i < ARRAY_SIZE(g_table_locale_base); i++) {
		if (locale->valid_channels & (1 << i)) {
			wlc_locale_add_channels(channels,
						g_table_locale_base[i]);
		}
	}
}

/*
 * Locale Definitions - 2.4 GHz
 */
static const locale_info_t locale_i = {	/* locale i. channel 1 - 13 */
	LOCALE_CHAN_01_11 | LOCALE_CHAN_12_13,
	LOCALE_RADAR_SET_NONE,
	LOCALE_RESTRICTED_SET_2G_SHORT,
	{QDB(19), QDB(19), QDB(19),
	 QDB(19), QDB(19), QDB(19)},
	{20, 20, 20, 0},
	WLC_EIRP
};

/*
 * Locale Definitions - 5 GHz
 */
static const locale_info_t locale_11 = {
	/* locale 11. channel 36 - 48, 52 - 64, 100 - 140, 149 - 165 */
	LOCALE_CHAN_36_64 | LOCALE_CHAN_100_140 | LOCALE_CHAN_149_165,
	LOCALE_RADAR_SET_1,
	LOCALE_RESTRICTED_NONE,
	{QDB(21), QDB(21), QDB(21), QDB(21), QDB(21)},
	{23, 23, 23, 30, 30},
	WLC_EIRP | WLC_DFS_EU
};

#define LOCALE_2G_IDX_i			0
static const locale_info_t *g_locale_2g_table[] = {
	&locale_i
};

#define LOCALE_5G_IDX_11	0
static const locale_info_t *g_locale_5g_table[] = {
	&locale_11
};

/*
 * MIMO Locale Definitions - 2.4 GHz
 */
static const locale_mimo_info_t locale_bn = {
	{QDB(13), QDB(13), QDB(13), QDB(13), QDB(13),
	 QDB(13), QDB(13), QDB(13), QDB(13), QDB(13),
	 QDB(13), QDB(13), QDB(13)},
	{0, 0, QDB(13), QDB(13), QDB(13),
	 QDB(13), QDB(13), QDB(13), QDB(13), QDB(13),
	 QDB(13), 0, 0},
	0
};

/* locale mimo 2g indexes */
#define LOCALE_MIMO_IDX_bn			0

static const locale_mimo_info_t *g_mimo_2g_table[] = {
	&locale_bn
};

/*
 * MIMO Locale Definitions - 5 GHz
 */
static const locale_mimo_info_t locale_11n = {
	{ /* 12.5 dBm */ 50, 50, 50, QDB(15), QDB(15)},
	{QDB(14), QDB(15), QDB(15), QDB(15), QDB(15)},
	0
};

#define LOCALE_MIMO_IDX_11n			0
static const locale_mimo_info_t *g_mimo_5g_table[] = {
	&locale_11n
};

#ifdef LC
#undef LC
#endif
#define LC(id)	LOCALE_MIMO_IDX_ ## id

#ifdef LC_2G
#undef LC_2G
#endif
#define LC_2G(id)	LOCALE_2G_IDX_ ## id

#ifdef LC_5G
#undef LC_5G
#endif
#define LC_5G(id)	LOCALE_5G_IDX_ ## id

#define LOCALES(band2, band5, mimo2, mimo5)     {LC_2G(band2), LC_5G(band5), LC(mimo2), LC(mimo5)}

static const struct {
	char abbrev[WLC_CNTRY_BUF_SZ];	/* country abbreviation */
	country_info_t country;
} cntry_locales[] = {
	{
	"X2", LOCALES(i, 11, bn, 11n)},	/* Worldwide RoW 2 */
};

#ifdef SUPPORT_40MHZ
/* 20MHz channel info for 40MHz pairing support */
struct chan20_info {
	u8 sb;
	u8 adj_sbs;
};

/* indicates adjacent channels that are allowed for a 40 Mhz channel and
 * those that permitted by the HT
 */
struct chan20_info chan20_info[] = {
	/* 11b/11g */
/* 0 */ {1, (CH_UPPER_SB | CH_EWA_VALID)},
/* 1 */ {2, (CH_UPPER_SB | CH_EWA_VALID)},
/* 2 */ {3, (CH_UPPER_SB | CH_EWA_VALID)},
/* 3 */ {4, (CH_UPPER_SB | CH_EWA_VALID)},
/* 4 */ {5, (CH_UPPER_SB | CH_LOWER_SB | CH_EWA_VALID)},
/* 5 */ {6, (CH_UPPER_SB | CH_LOWER_SB | CH_EWA_VALID)},
/* 6 */ {7, (CH_UPPER_SB | CH_LOWER_SB | CH_EWA_VALID)},
/* 7 */ {8, (CH_UPPER_SB | CH_LOWER_SB | CH_EWA_VALID)},
/* 8 */ {9, (CH_UPPER_SB | CH_LOWER_SB | CH_EWA_VALID)},
/* 9 */ {10, (CH_LOWER_SB | CH_EWA_VALID)},
/* 10 */ {11, (CH_LOWER_SB | CH_EWA_VALID)},
/* 11 */ {12, (CH_LOWER_SB)},
/* 12 */ {13, (CH_LOWER_SB)},
/* 13 */ {14, (CH_LOWER_SB)},

/* 11a japan high */
/* 14 */ {34, (CH_UPPER_SB)},
/* 15 */ {38, (CH_LOWER_SB)},
/* 16 */ {42, (CH_LOWER_SB)},
/* 17 */ {46, (CH_LOWER_SB)},

/* 11a usa low */
/* 18 */ {36, (CH_UPPER_SB | CH_EWA_VALID)},
/* 19 */ {40, (CH_LOWER_SB | CH_EWA_VALID)},
/* 20 */ {44, (CH_UPPER_SB | CH_EWA_VALID)},
/* 21 */ {48, (CH_LOWER_SB | CH_EWA_VALID)},
/* 22 */ {52, (CH_UPPER_SB | CH_EWA_VALID)},
/* 23 */ {56, (CH_LOWER_SB | CH_EWA_VALID)},
/* 24 */ {60, (CH_UPPER_SB | CH_EWA_VALID)},
/* 25 */ {64, (CH_LOWER_SB | CH_EWA_VALID)},

/* 11a Europe */
/* 26 */ {100, (CH_UPPER_SB | CH_EWA_VALID)},
/* 27 */ {104, (CH_LOWER_SB | CH_EWA_VALID)},
/* 28 */ {108, (CH_UPPER_SB | CH_EWA_VALID)},
/* 29 */ {112, (CH_LOWER_SB | CH_EWA_VALID)},
/* 30 */ {116, (CH_UPPER_SB | CH_EWA_VALID)},
/* 31 */ {120, (CH_LOWER_SB | CH_EWA_VALID)},
/* 32 */ {124, (CH_UPPER_SB | CH_EWA_VALID)},
/* 33 */ {128, (CH_LOWER_SB | CH_EWA_VALID)},
/* 34 */ {132, (CH_UPPER_SB | CH_EWA_VALID)},
/* 35 */ {136, (CH_LOWER_SB | CH_EWA_VALID)},
/* 36 */ {140, (CH_LOWER_SB)},

/* 11a usa high, ref5 only */
/* The 0x80 bit in pdiv means these are REF5, other entries are REF20 */
/* 37 */ {149, (CH_UPPER_SB | CH_EWA_VALID)},
/* 38 */ {153, (CH_LOWER_SB | CH_EWA_VALID)},
/* 39 */ {157, (CH_UPPER_SB | CH_EWA_VALID)},
/* 40 */ {161, (CH_LOWER_SB | CH_EWA_VALID)},
/* 41 */ {165, (CH_LOWER_SB)},

/* 11a japan */
/* 42 */ {184, (CH_UPPER_SB)},
/* 43 */ {188, (CH_LOWER_SB)},
/* 44 */ {192, (CH_UPPER_SB)},
/* 45 */ {196, (CH_LOWER_SB)},
/* 46 */ {200, (CH_UPPER_SB)},
/* 47 */ {204, (CH_LOWER_SB)},
/* 48 */ {208, (CH_UPPER_SB)},
/* 49 */ {212, (CH_LOWER_SB)},
/* 50 */ {216, (CH_LOWER_SB)}
};
#endif				/* SUPPORT_40MHZ */

static const locale_info_t *wlc_get_locale_2g(u8 locale_idx)
{
	if (locale_idx >= ARRAY_SIZE(g_locale_2g_table)) {
		WL_ERROR("%s: locale 2g index size out of range %d\n",
			 __func__, locale_idx);
		ASSERT(locale_idx < ARRAY_SIZE(g_locale_2g_table));
		return NULL;
	}
	return g_locale_2g_table[locale_idx];
}

static const locale_info_t *wlc_get_locale_5g(u8 locale_idx)
{
	if (locale_idx >= ARRAY_SIZE(g_locale_5g_table)) {
		WL_ERROR("%s: locale 5g index size out of range %d\n",
			 __func__, locale_idx);
		ASSERT(locale_idx < ARRAY_SIZE(g_locale_5g_table));
		return NULL;
	}
	return g_locale_5g_table[locale_idx];
}

const locale_mimo_info_t *wlc_get_mimo_2g(u8 locale_idx)
{
	if (locale_idx >= ARRAY_SIZE(g_mimo_2g_table)) {
		WL_ERROR("%s: mimo 2g index size out of range %d\n",
			 __func__, locale_idx);
		return NULL;
	}
	return g_mimo_2g_table[locale_idx];
}

const locale_mimo_info_t *wlc_get_mimo_5g(u8 locale_idx)
{
	if (locale_idx >= ARRAY_SIZE(g_mimo_5g_table)) {
		WL_ERROR("%s: mimo 5g index size out of range %d\n",
			 __func__, locale_idx);
		return NULL;
	}
	return g_mimo_5g_table[locale_idx];
}

wlc_cm_info_t *wlc_channel_mgr_attach(struct wlc_info *wlc)
{
	wlc_cm_info_t *wlc_cm;
	char country_abbrev[WLC_CNTRY_BUF_SZ];
	const country_info_t *country;
	struct wlc_pub *pub = wlc->pub;
	char *ccode;

	WL_TRACE("wl%d: wlc_channel_mgr_attach\n", wlc->pub->unit);

	wlc_cm = kzalloc(sizeof(wlc_cm_info_t), GFP_ATOMIC);
	if (wlc_cm == NULL) {
		WL_ERROR("wl%d: %s: out of memory", pub->unit, __func__);
		return NULL;
	}
	wlc_cm->pub = pub;
	wlc_cm->wlc = wlc;
	wlc->cmi = wlc_cm;

	/* store the country code for passing up as a regulatory hint */
	ccode = getvar(wlc->pub->vars, "ccode");
	if (ccode) {
		strncpy(wlc->pub->srom_ccode, ccode, WLC_CNTRY_BUF_SZ - 1);
		WL_NONE("%s: SROM country code is %c%c\n",
			__func__,
			wlc->pub->srom_ccode[0], wlc->pub->srom_ccode[1]);
	}

	/* internal country information which must match regulatory constraints in firmware */
	memset(country_abbrev, 0, WLC_CNTRY_BUF_SZ);
	strncpy(country_abbrev, "X2", sizeof(country_abbrev) - 1);
	country = wlc_country_lookup(wlc, country_abbrev);

	ASSERT(country != NULL);

	/* save default country for exiting 11d regulatory mode */
	strncpy(wlc->country_default, country_abbrev, WLC_CNTRY_BUF_SZ - 1);

	/* initialize autocountry_default to driver default */
	strncpy(wlc->autocountry_default, "X2", WLC_CNTRY_BUF_SZ - 1);

	wlc_set_countrycode(wlc_cm, country_abbrev);

	return wlc_cm;
}

void wlc_channel_mgr_detach(wlc_cm_info_t *wlc_cm)
{
	kfree(wlc_cm);
}

u8 wlc_channel_locale_flags_in_band(wlc_cm_info_t *wlc_cm, uint bandunit)
{
	return wlc_cm->bandstate[bandunit].locale_flags;
}

/* set the driver's current country and regulatory information using a country code
 * as the source. Lookup built in country information found with the country code.
 */
static int wlc_set_countrycode(wlc_cm_info_t *wlc_cm, const char *ccode)
{
	char country_abbrev[WLC_CNTRY_BUF_SZ];
	strncpy(country_abbrev, ccode, WLC_CNTRY_BUF_SZ);
	return wlc_set_countrycode_rev(wlc_cm, country_abbrev, ccode, -1);
}

static int
wlc_set_countrycode_rev(wlc_cm_info_t *wlc_cm,
			const char *country_abbrev,
			const char *ccode, int regrev)
{
	const country_info_t *country;
	char mapped_ccode[WLC_CNTRY_BUF_SZ];
	uint mapped_regrev;

	WL_NONE("%s: (country_abbrev \"%s\", ccode \"%s\", regrev %d) SPROM \"%s\"/%u\n",
		__func__, country_abbrev, ccode, regrev,
		wlc_cm->srom_ccode, wlc_cm->srom_regrev);

	/* if regrev is -1, lookup the mapped country code,
	 * otherwise use the ccode and regrev directly
	 */
	if (regrev == -1) {
		/* map the country code to a built-in country code, regrev, and country_info */
		country =
		    wlc_countrycode_map(wlc_cm, ccode, mapped_ccode,
					&mapped_regrev);
	} else {
		/* find the matching built-in country definition */
		ASSERT(0);
		country = wlc_country_lookup_direct(ccode, regrev);
		strncpy(mapped_ccode, ccode, WLC_CNTRY_BUF_SZ);
		mapped_regrev = regrev;
	}

	if (country == NULL)
		return BCME_BADARG;

	/* set the driver state for the country */
	wlc_set_country_common(wlc_cm, country_abbrev, mapped_ccode,
			       mapped_regrev, country);

	return 0;
}

/* set the driver's current country and regulatory information using a country code
 * as the source. Look up built in country information found with the country code.
 */
static void
wlc_set_country_common(wlc_cm_info_t *wlc_cm,
		       const char *country_abbrev,
		       const char *ccode, uint regrev,
		       const country_info_t *country)
{
	const locale_mimo_info_t *li_mimo;
	const locale_info_t *locale;
	struct wlc_info *wlc = wlc_cm->wlc;
	char prev_country_abbrev[WLC_CNTRY_BUF_SZ];

	ASSERT(country != NULL);

	/* save current country state */
	wlc_cm->country = country;

	memset(&prev_country_abbrev, 0, WLC_CNTRY_BUF_SZ);
	strncpy(prev_country_abbrev, wlc_cm->country_abbrev,
		WLC_CNTRY_BUF_SZ - 1);

	strncpy(wlc_cm->country_abbrev, country_abbrev, WLC_CNTRY_BUF_SZ - 1);
	strncpy(wlc_cm->ccode, ccode, WLC_CNTRY_BUF_SZ - 1);
	wlc_cm->regrev = regrev;

	/* disable/restore nmode based on country regulations */
	li_mimo = wlc_get_mimo_2g(country->locale_mimo_2G);
	if (li_mimo && (li_mimo->flags & WLC_NO_MIMO)) {
		wlc_set_nmode(wlc, OFF);
		wlc->stf->no_cddstbc = true;
	} else {
		wlc->stf->no_cddstbc = false;
		if (N_ENAB(wlc->pub) != wlc->protection->nmode_user)
			wlc_set_nmode(wlc, wlc->protection->nmode_user);
	}

	wlc_stf_ss_update(wlc, wlc->bandstate[BAND_2G_INDEX]);
	wlc_stf_ss_update(wlc, wlc->bandstate[BAND_5G_INDEX]);
	/* set or restore gmode as required by regulatory */
	locale = wlc_get_locale_2g(country->locale_2G);
	if (locale && (locale->flags & WLC_NO_OFDM)) {
		wlc_set_gmode(wlc, GMODE_LEGACY_B, false);
	} else {
		wlc_set_gmode(wlc, wlc->protection->gmode_user, false);
	}

	wlc_channels_init(wlc_cm, country);

	return;
}

/* Lookup a country info structure from a null terminated country code
 * The lookup is case sensitive.
 */
static const country_info_t *wlc_country_lookup(struct wlc_info *wlc,
					 const char *ccode)
{
	const country_info_t *country;
	char mapped_ccode[WLC_CNTRY_BUF_SZ];
	uint mapped_regrev;

	/* map the country code to a built-in country code, regrev, and country_info struct */
	country =
	    wlc_countrycode_map(wlc->cmi, ccode, mapped_ccode, &mapped_regrev);

	return country;
}

static const country_info_t *wlc_countrycode_map(wlc_cm_info_t *wlc_cm,
						 const char *ccode,
						 char *mapped_ccode,
						 uint *mapped_regrev)
{
	struct wlc_info *wlc = wlc_cm->wlc;
	const country_info_t *country;
	uint srom_regrev = wlc_cm->srom_regrev;
	const char *srom_ccode = wlc_cm->srom_ccode;
	int mapped;

	/* check for currently supported ccode size */
	if (strlen(ccode) > (WLC_CNTRY_BUF_SZ - 1)) {
		WL_ERROR("wl%d: %s: ccode \"%s\" too long for match\n",
			 wlc->pub->unit, __func__, ccode);
		return NULL;
	}

	/* default mapping is the given ccode and regrev 0 */
	strncpy(mapped_ccode, ccode, WLC_CNTRY_BUF_SZ);
	*mapped_regrev = 0;

	/* If the desired country code matches the srom country code,
	 * then the mapped country is the srom regulatory rev.
	 * Otherwise look for an aggregate mapping.
	 */
	if (!strcmp(srom_ccode, ccode)) {
		*mapped_regrev = srom_regrev;
		mapped = 0;
		WL_ERROR("srom_code == ccode %s\n", __func__);
		ASSERT(0);
	} else {
		mapped =
		    wlc_country_aggregate_map(wlc_cm, ccode, mapped_ccode,
					      mapped_regrev);
	}

	/* find the matching built-in country definition */
	country = wlc_country_lookup_direct(mapped_ccode, *mapped_regrev);

	/* if there is not an exact rev match, default to rev zero */
	if (country == NULL && *mapped_regrev != 0) {
		*mapped_regrev = 0;
		ASSERT(0);
		country =
		    wlc_country_lookup_direct(mapped_ccode, *mapped_regrev);
	}

	return country;
}

static int
wlc_country_aggregate_map(wlc_cm_info_t *wlc_cm, const char *ccode,
			  char *mapped_ccode, uint *mapped_regrev)
{
	return false;
}

/* Lookup a country info structure from a null terminated country
 * abbreviation and regrev directly with no translation.
 */
static const country_info_t *wlc_country_lookup_direct(const char *ccode,
						       uint regrev)
{
	uint size, i;

	/* Should just return 0 for single locale driver. */
	/* Keep it this way in case we add more locales. (for now anyway) */

	/* all other country def arrays are for regrev == 0, so if regrev is non-zero, fail */
	if (regrev > 0)
		return NULL;

	/* find matched table entry from country code */
	size = ARRAY_SIZE(cntry_locales);
	for (i = 0; i < size; i++) {
		if (strcmp(ccode, cntry_locales[i].abbrev) == 0) {
			return &cntry_locales[i].country;
		}
	}

	WL_ERROR("%s: Returning NULL\n", __func__);
	ASSERT(0);
	return NULL;
}

static int
wlc_channels_init(wlc_cm_info_t *wlc_cm, const country_info_t *country)
{
	struct wlc_info *wlc = wlc_cm->wlc;
	uint i, j;
	struct wlcband *band;
	const locale_info_t *li;
	chanvec_t sup_chan;
	const locale_mimo_info_t *li_mimo;

	band = wlc->band;
	for (i = 0; i < NBANDS(wlc);
	     i++, band = wlc->bandstate[OTHERBANDUNIT(wlc)]) {

		li = BAND_5G(band->bandtype) ?
		    wlc_get_locale_5g(country->locale_5G) :
		    wlc_get_locale_2g(country->locale_2G);
		ASSERT(li);
		wlc_cm->bandstate[band->bandunit].locale_flags = li->flags;
		li_mimo = BAND_5G(band->bandtype) ?
		    wlc_get_mimo_5g(country->locale_mimo_5G) :
		    wlc_get_mimo_2g(country->locale_mimo_2G);
		ASSERT(li_mimo);

		/* merge the mimo non-mimo locale flags */
		wlc_cm->bandstate[band->bandunit].locale_flags |=
		    li_mimo->flags;

		wlc_cm->bandstate[band->bandunit].restricted_channels =
		    g_table_restricted_chan[li->restricted_channels];
		wlc_cm->bandstate[band->bandunit].radar_channels =
		    g_table_radar_set[li->radar_channels];

		/* set the channel availability,
		 * masking out the channels that may not be supported on this phy
		 */
		wlc_phy_chanspec_band_validch(band->pi, band->bandtype,
					      &sup_chan);
		wlc_locale_get_channels(li,
					&wlc_cm->bandstate[band->bandunit].
					valid_channels);
		for (j = 0; j < sizeof(chanvec_t); j++)
			wlc_cm->bandstate[band->bandunit].valid_channels.
			    vec[j] &= sup_chan.vec[j];
	}

	wlc_quiet_channels_reset(wlc_cm);
	wlc_channels_commit(wlc_cm);

	return 0;
}

/* Update the radio state (enable/disable) and tx power targets
 * based on a new set of channel/regulatory information
 */
static void wlc_channels_commit(wlc_cm_info_t *wlc_cm)
{
	struct wlc_info *wlc = wlc_cm->wlc;
	uint chan;
	struct txpwr_limits txpwr;

	/* search for the existence of any valid channel */
	for (chan = 0; chan < MAXCHANNEL; chan++) {
		if (VALID_CHANNEL20_DB(wlc, chan)) {
			break;
		}
	}
	if (chan == MAXCHANNEL)
		chan = INVCHANNEL;

	/* based on the channel search above, set or clear WL_RADIO_COUNTRY_DISABLE */
	if (chan == INVCHANNEL) {
		/* country/locale with no valid channels, set the radio disable bit */
		mboolset(wlc->pub->radio_disabled, WL_RADIO_COUNTRY_DISABLE);
		WL_ERROR("wl%d: %s: no valid channel for \"%s\" nbands %d bandlocked %d\n",
			 wlc->pub->unit, __func__,
			 wlc_cm->country_abbrev, NBANDS(wlc), wlc->bandlocked);
	} else
	    if (mboolisset(wlc->pub->radio_disabled,
		WL_RADIO_COUNTRY_DISABLE)) {
		/* country/locale with valid channel, clear the radio disable bit */
		mboolclr(wlc->pub->radio_disabled, WL_RADIO_COUNTRY_DISABLE);
	}

	/* Now that the country abbreviation is set, if the radio supports 2G, then
	 * set channel 14 restrictions based on the new locale.
	 */
	if (NBANDS(wlc) > 1 || BAND_2G(wlc->band->bandtype)) {
		wlc_phy_chanspec_ch14_widefilter_set(wlc->band->pi,
						     wlc_japan(wlc) ? true :
						     false);
	}

	if (wlc->pub->up && chan != INVCHANNEL) {
		wlc_channel_reg_limits(wlc_cm, wlc->chanspec, &txpwr);
		wlc_channel_min_txpower_limits_with_local_constraint(wlc_cm,
								     &txpwr,
								     WLC_TXPWR_MAX);
		wlc_phy_txpower_limit_set(wlc->band->pi, &txpwr, wlc->chanspec);
	}
}

/* reset the quiet channels vector to the union of the restricted and radar channel sets */
static void wlc_quiet_channels_reset(wlc_cm_info_t *wlc_cm)
{
	struct wlc_info *wlc = wlc_cm->wlc;
	uint i, j;
	struct wlcband *band;
	const chanvec_t *chanvec;

	memset(&wlc_cm->quiet_channels, 0, sizeof(chanvec_t));

	band = wlc->band;
	for (i = 0; i < NBANDS(wlc);
	     i++, band = wlc->bandstate[OTHERBANDUNIT(wlc)]) {

		/* initialize quiet channels for restricted channels */
		chanvec = wlc_cm->bandstate[band->bandunit].restricted_channels;
		for (j = 0; j < sizeof(chanvec_t); j++)
			wlc_cm->quiet_channels.vec[j] |= chanvec->vec[j];

	}
}

static bool wlc_quiet_chanspec(wlc_cm_info_t *wlc_cm, chanspec_t chspec)
{
	return N_ENAB(wlc_cm->wlc->pub) && CHSPEC_IS40(chspec) ?
		(isset
		 (wlc_cm->quiet_channels.vec,
		  LOWER_20_SB(CHSPEC_CHANNEL(chspec)))
		 || isset(wlc_cm->quiet_channels.vec,
			  UPPER_20_SB(CHSPEC_CHANNEL(chspec)))) : isset(wlc_cm->
									quiet_channels.
									vec,
									CHSPEC_CHANNEL
									(chspec));
}

/* Is the channel valid for the current locale? (but don't consider channels not
 *   available due to bandlocking)
 */
static bool wlc_valid_channel20_db(wlc_cm_info_t *wlc_cm, uint val)
{
	struct wlc_info *wlc = wlc_cm->wlc;

	return VALID_CHANNEL20(wlc, val) ||
		(!wlc->bandlocked
		 && VALID_CHANNEL20_IN_BAND(wlc, OTHERBANDUNIT(wlc), val));
}

/* Is the channel valid for the current locale and specified band? */
static bool
wlc_valid_channel20_in_band(wlc_cm_info_t *wlc_cm, uint bandunit, uint val)
{
	return ((val < MAXCHANNEL)
		&& isset(wlc_cm->bandstate[bandunit].valid_channels.vec, val));
}

/* Is the channel valid for the current locale and current band? */
static bool wlc_valid_channel20(wlc_cm_info_t *wlc_cm, uint val)
{
	struct wlc_info *wlc = wlc_cm->wlc;

	return ((val < MAXCHANNEL) &&
		isset(wlc_cm->bandstate[wlc->band->bandunit].valid_channels.vec,
		      val));
}

static void
wlc_channel_min_txpower_limits_with_local_constraint(wlc_cm_info_t *wlc_cm,
						     struct txpwr_limits *txpwr,
						     u8
						     local_constraint_qdbm)
{
	int j;

	/* CCK Rates */
	for (j = 0; j < WL_TX_POWER_CCK_NUM; j++) {
		txpwr->cck[j] = min(txpwr->cck[j], local_constraint_qdbm);
	}

	/* 20 MHz Legacy OFDM SISO */
	for (j = 0; j < WL_TX_POWER_OFDM_NUM; j++) {
		txpwr->ofdm[j] = min(txpwr->ofdm[j], local_constraint_qdbm);
	}

	/* 20 MHz Legacy OFDM CDD */
	for (j = 0; j < WLC_NUM_RATES_OFDM; j++) {
		txpwr->ofdm_cdd[j] =
		    min(txpwr->ofdm_cdd[j], local_constraint_qdbm);
	}

	/* 40 MHz Legacy OFDM SISO */
	for (j = 0; j < WLC_NUM_RATES_OFDM; j++) {
		txpwr->ofdm_40_siso[j] =
		    min(txpwr->ofdm_40_siso[j], local_constraint_qdbm);
	}

	/* 40 MHz Legacy OFDM CDD */
	for (j = 0; j < WLC_NUM_RATES_OFDM; j++) {
		txpwr->ofdm_40_cdd[j] =
		    min(txpwr->ofdm_40_cdd[j], local_constraint_qdbm);
	}

	/* 20MHz MCS 0-7 SISO */
	for (j = 0; j < WLC_NUM_RATES_MCS_1_STREAM; j++) {
		txpwr->mcs_20_siso[j] =
		    min(txpwr->mcs_20_siso[j], local_constraint_qdbm);
	}

	/* 20MHz MCS 0-7 CDD */
	for (j = 0; j < WLC_NUM_RATES_MCS_1_STREAM; j++) {
		txpwr->mcs_20_cdd[j] =
		    min(txpwr->mcs_20_cdd[j], local_constraint_qdbm);
	}

	/* 20MHz MCS 0-7 STBC */
	for (j = 0; j < WLC_NUM_RATES_MCS_1_STREAM; j++) {
		txpwr->mcs_20_stbc[j] =
		    min(txpwr->mcs_20_stbc[j], local_constraint_qdbm);
	}

	/* 20MHz MCS 8-15 MIMO */
	for (j = 0; j < WLC_NUM_RATES_MCS_2_STREAM; j++)
		txpwr->mcs_20_mimo[j] =
		    min(txpwr->mcs_20_mimo[j], local_constraint_qdbm);

	/* 40MHz MCS 0-7 SISO */
	for (j = 0; j < WLC_NUM_RATES_MCS_1_STREAM; j++) {
		txpwr->mcs_40_siso[j] =
		    min(txpwr->mcs_40_siso[j], local_constraint_qdbm);
	}

	/* 40MHz MCS 0-7 CDD */
	for (j = 0; j < WLC_NUM_RATES_MCS_1_STREAM; j++) {
		txpwr->mcs_40_cdd[j] =
		    min(txpwr->mcs_40_cdd[j], local_constraint_qdbm);
	}

	/* 40MHz MCS 0-7 STBC */
	for (j = 0; j < WLC_NUM_RATES_MCS_1_STREAM; j++) {
		txpwr->mcs_40_stbc[j] =
		    min(txpwr->mcs_40_stbc[j], local_constraint_qdbm);
	}

	/* 40MHz MCS 8-15 MIMO */
	for (j = 0; j < WLC_NUM_RATES_MCS_2_STREAM; j++)
		txpwr->mcs_40_mimo[j] =
		    min(txpwr->mcs_40_mimo[j], local_constraint_qdbm);

	/* 40MHz MCS 32 */
	txpwr->mcs32 = min(txpwr->mcs32, local_constraint_qdbm);

}

void
wlc_channel_set_chanspec(wlc_cm_info_t *wlc_cm, chanspec_t chanspec,
			 u8 local_constraint_qdbm)
{
	struct wlc_info *wlc = wlc_cm->wlc;
	struct txpwr_limits txpwr;

	wlc_channel_reg_limits(wlc_cm, chanspec, &txpwr);

	wlc_channel_min_txpower_limits_with_local_constraint(wlc_cm, &txpwr,
							     local_constraint_qdbm);

	wlc_bmac_set_chanspec(wlc->hw, chanspec,
			      (wlc_quiet_chanspec(wlc_cm, chanspec) != 0),
			      &txpwr);
}

#ifdef POWER_DBG
static void wlc_phy_txpower_limits_dump(txpwr_limits_t *txpwr)
{
	int i;
	char buf[80];
	char fraction[4][4] = { "   ", ".25", ".5 ", ".75" };

	sprintf(buf, "CCK                ");
	for (i = 0; i < WLC_NUM_RATES_CCK; i++) {
		sprintf(buf[strlen(buf)], " %2d%s",
			txpwr->cck[i] / WLC_TXPWR_DB_FACTOR,
			fraction[txpwr->cck[i] % WLC_TXPWR_DB_FACTOR]);
	}
	printk(KERN_DEBUG "%s\n", buf);

	sprintf(buf, "20 MHz OFDM SISO   ");
	for (i = 0; i < WLC_NUM_RATES_OFDM; i++) {
		sprintf(buf[strlen(buf)], " %2d%s",
			txpwr->ofdm[i] / WLC_TXPWR_DB_FACTOR,
			fraction[txpwr->ofdm[i] % WLC_TXPWR_DB_FACTOR]);
	}
	printk(KERN_DEBUG "%s\n", buf);

	sprintf(buf, "20 MHz OFDM CDD    ");
	for (i = 0; i < WLC_NUM_RATES_OFDM; i++) {
		sprintf(buf[strlen(buf)], " %2d%s",
			txpwr->ofdm_cdd[i] / WLC_TXPWR_DB_FACTOR,
			fraction[txpwr->ofdm_cdd[i] % WLC_TXPWR_DB_FACTOR]);
	}
	printk(KERN_DEBUG "%s\n", buf);

	sprintf(buf, "40 MHz OFDM SISO   ");
	for (i = 0; i < WLC_NUM_RATES_OFDM; i++) {
		sprintf(buf[strlen(buf)], " %2d%s",
			txpwr->ofdm_40_siso[i] / WLC_TXPWR_DB_FACTOR,
			fraction[txpwr->ofdm_40_siso[i] % WLC_TXPWR_DB_FACTOR]);
	}
	printk(KERN_DEBUG "%s\n", buf);

	sprintf(buf, "40 MHz OFDM CDD    ");
	for (i = 0; i < WLC_NUM_RATES_OFDM; i++) {
		sprintf(buf[strlen(buf)], " %2d%s",
			txpwr->ofdm_40_cdd[i] / WLC_TXPWR_DB_FACTOR,
			fraction[txpwr->ofdm_40_cdd[i] % WLC_TXPWR_DB_FACTOR]);
	}
	printk(KERN_DEBUG "%s\n", buf);

	sprintf(buf, "20 MHz MCS0-7 SISO ");
	for (i = 0; i < WLC_NUM_RATES_MCS_1_STREAM; i++) {
		sprintf(buf[strlen(buf)], " %2d%s",
			txpwr->mcs_20_siso[i] / WLC_TXPWR_DB_FACTOR,
			fraction[txpwr->mcs_20_siso[i] % WLC_TXPWR_DB_FACTOR]);
	}
	printk(KERN_DEBUG "%s\n", buf);

	sprintf(buf, "20 MHz MCS0-7 CDD  ");
	for (i = 0; i < WLC_NUM_RATES_MCS_1_STREAM; i++) {
		sprintf(buf[strlen(buf)], " %2d%s",
			txpwr->mcs_20_cdd[i] / WLC_TXPWR_DB_FACTOR,
			fraction[txpwr->mcs_20_cdd[i] % WLC_TXPWR_DB_FACTOR]);
	}
	printk(KERN_DEBUG "%s\n", buf);

	sprintf(buf, "20 MHz MCS0-7 STBC ");
	for (i = 0; i < WLC_NUM_RATES_MCS_1_STREAM; i++) {
		sprintf(buf[strlen(buf)], " %2d%s",
			txpwr->mcs_20_stbc[i] / WLC_TXPWR_DB_FACTOR,
			fraction[txpwr->mcs_20_stbc[i] % WLC_TXPWR_DB_FACTOR]);
	}
	printk(KERN_DEBUG "%s\n", buf);

	sprintf(buf, "20 MHz MCS8-15 SDM ");
	for (i = 0; i < WLC_NUM_RATES_MCS_2_STREAM; i++) {
		sprintf(buf[strlen(buf)], " %2d%s",
			txpwr->mcs_20_mimo[i] / WLC_TXPWR_DB_FACTOR,
			fraction[txpwr->mcs_20_mimo[i] % WLC_TXPWR_DB_FACTOR]);
	}
	printk(KERN_DEBUG "%s\n", buf);

	sprintf(buf, "40 MHz MCS0-7 SISO ");
	for (i = 0; i < WLC_NUM_RATES_MCS_1_STREAM; i++) {
		sprintf(buf[strlen(buf)], " %2d%s",
			txpwr->mcs_40_siso[i] / WLC_TXPWR_DB_FACTOR,
			fraction[txpwr->mcs_40_siso[i] % WLC_TXPWR_DB_FACTOR]);
	}
	printk(KERN_DEBUG "%s\n", buf);

	sprintf(buf, "40 MHz MCS0-7 CDD  ");
	for (i = 0; i < WLC_NUM_RATES_MCS_1_STREAM; i++) {
		sprintf(buf[strlen(buf)], " %2d%s",
			txpwr->mcs_40_cdd[i] / WLC_TXPWR_DB_FACTOR,
			fraction[txpwr->mcs_40_cdd[i] % WLC_TXPWR_DB_FACTOR]);
	}
	printk(KERN_DEBUG "%s\n", buf);

	sprintf(buf, "40 MHz MCS0-7 STBC ");
	for (i = 0; i < WLC_NUM_RATES_MCS_1_STREAM; i++) {
		sprintf(buf[strlen(buf)], " %2d%s",
			txpwr->mcs_40_stbc[i] / WLC_TXPWR_DB_FACTOR,
			fraction[txpwr->mcs_40_stbc[i] % WLC_TXPWR_DB_FACTOR]);
	}
	printk(KERN_DEBUG "%s\n", buf);

	sprintf(buf, "40 MHz MCS8-15 SDM ");
	for (i = 0; i < WLC_NUM_RATES_MCS_2_STREAM; i++) {
		sprintf(buf[strlen(buf)], " %2d%s",
			txpwr->mcs_40_mimo[i] / WLC_TXPWR_DB_FACTOR,
			fraction[txpwr->mcs_40_mimo[i] % WLC_TXPWR_DB_FACTOR]);
	}
	printk(KERN_DEBUG "%s\n", buf);

	printk(KERN_DEBUG "MCS32               %2d%s\n",
	       txpwr->mcs32 / WLC_TXPWR_DB_FACTOR,
	       fraction[txpwr->mcs32 % WLC_TXPWR_DB_FACTOR]);
}
#endif				/* POWER_DBG */

void
wlc_channel_reg_limits(wlc_cm_info_t *wlc_cm, chanspec_t chanspec,
		       txpwr_limits_t *txpwr)
{
	struct wlc_info *wlc = wlc_cm->wlc;
	uint i;
	uint chan;
	int maxpwr;
	int delta;
	const country_info_t *country;
	struct wlcband *band;
	const locale_info_t *li;
	int conducted_max;
	int conducted_ofdm_max;
	const locale_mimo_info_t *li_mimo;
	int maxpwr20, maxpwr40;
	int maxpwr_idx;
	uint j;

	memset(txpwr, 0, sizeof(txpwr_limits_t));

	if (!wlc_valid_chanspec_db(wlc_cm, chanspec)) {
		country = wlc_country_lookup(wlc, wlc->autocountry_default);
		if (country == NULL)
			return;
	} else {
		country = wlc_cm->country;
	}

	chan = CHSPEC_CHANNEL(chanspec);
	band = wlc->bandstate[CHSPEC_WLCBANDUNIT(chanspec)];
	li = BAND_5G(band->bandtype) ?
	    wlc_get_locale_5g(country->locale_5G) :
	    wlc_get_locale_2g(country->locale_2G);

	li_mimo = BAND_5G(band->bandtype) ?
	    wlc_get_mimo_5g(country->locale_mimo_5G) :
	    wlc_get_mimo_2g(country->locale_mimo_2G);

	if (li->flags & WLC_EIRP) {
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
	if (BAND_2G(band->bandtype)) {
		maxpwr = li->maxpwr[CHANNEL_POWER_IDX_2G_CCK(chan)];

		maxpwr = maxpwr - delta;
		maxpwr = max(maxpwr, 0);
		maxpwr = min(maxpwr, conducted_max);

		for (i = 0; i < WLC_NUM_RATES_CCK; i++)
			txpwr->cck[i] = (u8) maxpwr;
	}

	/* OFDM txpwr limits for 2.4G or 5G bands */
	if (BAND_2G(band->bandtype)) {
		maxpwr = li->maxpwr[CHANNEL_POWER_IDX_2G_OFDM(chan)];

	} else {
		maxpwr = li->maxpwr[CHANNEL_POWER_IDX_5G(chan)];
	}

	maxpwr = maxpwr - delta;
	maxpwr = max(maxpwr, 0);
	maxpwr = min(maxpwr, conducted_ofdm_max);

	/* Keep OFDM lmit below CCK limit */
	if (BAND_2G(band->bandtype))
		maxpwr = min_t(int, maxpwr, txpwr->cck[0]);

	for (i = 0; i < WLC_NUM_RATES_OFDM; i++) {
		txpwr->ofdm[i] = (u8) maxpwr;
	}

	for (i = 0; i < WLC_NUM_RATES_OFDM; i++) {
		/* OFDM 40 MHz SISO has the same power as the corresponding MCS0-7 rate unless
		 * overriden by the locale specific code. We set this value to 0 as a
		 * flag (presumably 0 dBm isn't a possibility) and then copy the MCS0-7 value
		 * to the 40 MHz value if it wasn't explicitly set.
		 */
		txpwr->ofdm_40_siso[i] = 0;

		txpwr->ofdm_cdd[i] = (u8) maxpwr;

		txpwr->ofdm_40_cdd[i] = 0;
	}

	/* MIMO/HT specific limits */
	if (li_mimo->flags & WLC_EIRP) {
		delta = band->antgain;
	} else {
		delta = 0;
		if (band->antgain > QDB(6))
			delta = band->antgain - QDB(6);	/* Excess over 6 dB */
	}

	if (BAND_2G(band->bandtype))
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
	for (i = 0; i < WLC_NUM_RATES_MCS_1_STREAM; i++) {

		/* 20 MHz has the same power as the corresponding OFDM rate unless
		 * overriden by the locale specific code.
		 */
		txpwr->mcs_20_siso[i] = txpwr->ofdm[i];
		txpwr->mcs_40_siso[i] = 0;
	}

	/* Fill in the MCS 0-7 CDD rates */
	for (i = 0; i < WLC_NUM_RATES_MCS_1_STREAM; i++) {
		txpwr->mcs_20_cdd[i] = (u8) maxpwr20;
		txpwr->mcs_40_cdd[i] = (u8) maxpwr40;
	}

	/* These locales have SISO expressed in the table and override CDD later */
	if (li_mimo == &locale_bn) {
		if (li_mimo == &locale_bn) {
			maxpwr20 = QDB(16);
			maxpwr40 = 0;

			if (chan >= 3 && chan <= 11) {
				maxpwr40 = QDB(16);
			}
		}

		for (i = 0; i < WLC_NUM_RATES_MCS_1_STREAM; i++) {
			txpwr->mcs_20_siso[i] = (u8) maxpwr20;
			txpwr->mcs_40_siso[i] = (u8) maxpwr40;
		}
	}

	/* Fill in the MCS 0-7 STBC rates */
	for (i = 0; i < WLC_NUM_RATES_MCS_1_STREAM; i++) {
		txpwr->mcs_20_stbc[i] = 0;
		txpwr->mcs_40_stbc[i] = 0;
	}

	/* Fill in the MCS 8-15 SDM rates */
	for (i = 0; i < WLC_NUM_RATES_MCS_2_STREAM; i++) {
		txpwr->mcs_20_mimo[i] = (u8) maxpwr20;
		txpwr->mcs_40_mimo[i] = (u8) maxpwr40;
	}

	/* Fill in MCS32 */
	txpwr->mcs32 = (u8) maxpwr40;

	for (i = 0, j = 0; i < WLC_NUM_RATES_OFDM; i++, j++) {
		if (txpwr->ofdm_40_cdd[i] == 0)
			txpwr->ofdm_40_cdd[i] = txpwr->mcs_40_cdd[j];
		if (i == 0) {
			i = i + 1;
			if (txpwr->ofdm_40_cdd[i] == 0)
				txpwr->ofdm_40_cdd[i] = txpwr->mcs_40_cdd[j];
		}
	}

	/* Copy the 40 MHZ MCS 0-7 CDD value to the 40 MHZ MCS 0-7 SISO value if it wasn't
	 * provided explicitly.
	 */

	for (i = 0; i < WLC_NUM_RATES_MCS_1_STREAM; i++) {
		if (txpwr->mcs_40_siso[i] == 0)
			txpwr->mcs_40_siso[i] = txpwr->mcs_40_cdd[i];
	}

	for (i = 0, j = 0; i < WLC_NUM_RATES_OFDM; i++, j++) {
		if (txpwr->ofdm_40_siso[i] == 0)
			txpwr->ofdm_40_siso[i] = txpwr->mcs_40_siso[j];
		if (i == 0) {
			i = i + 1;
			if (txpwr->ofdm_40_siso[i] == 0)
				txpwr->ofdm_40_siso[i] = txpwr->mcs_40_siso[j];
		}
	}

	/* Copy the 20 and 40 MHz MCS0-7 CDD values to the corresponding STBC values if they weren't
	 * provided explicitly.
	 */
	for (i = 0; i < WLC_NUM_RATES_MCS_1_STREAM; i++) {
		if (txpwr->mcs_20_stbc[i] == 0)
			txpwr->mcs_20_stbc[i] = txpwr->mcs_20_cdd[i];

		if (txpwr->mcs_40_stbc[i] == 0)
			txpwr->mcs_40_stbc[i] = txpwr->mcs_40_cdd[i];
	}

#ifdef POWER_DBG
	wlc_phy_txpower_limits_dump(txpwr);
#endif
	return;
}

/* Returns true if currently set country is Japan or variant */
static bool wlc_japan(struct wlc_info *wlc)
{
	return wlc_japan_ccode(wlc->cmi->country_abbrev);
}

/* JP, J1 - J10 are Japan ccodes */
static bool wlc_japan_ccode(const char *ccode)
{
	return (ccode[0] == 'J' &&
		(ccode[1] == 'P' || (ccode[1] >= '1' && ccode[1] <= '9')));
}

/*
 * Validate the chanspec for this locale, for 40MHZ we need to also check that the sidebands
 * are valid 20MZH channels in this locale and they are also a legal HT combination
 */
static bool
wlc_valid_chanspec_ext(wlc_cm_info_t *wlc_cm, chanspec_t chspec, bool dualband)
{
	struct wlc_info *wlc = wlc_cm->wlc;
	u8 channel = CHSPEC_CHANNEL(chspec);

	/* check the chanspec */
	if (wf_chspec_malformed(chspec)) {
		WL_ERROR("wl%d: malformed chanspec 0x%x\n",
			 wlc->pub->unit, chspec);
		ASSERT(0);
		return false;
	}

	if (CHANNEL_BANDUNIT(wlc_cm->wlc, channel) !=
	    CHSPEC_WLCBANDUNIT(chspec))
		return false;

	/* Check a 20Mhz channel */
	if (CHSPEC_IS20(chspec)) {
		if (dualband)
			return VALID_CHANNEL20_DB(wlc_cm->wlc, channel);
		else
			return VALID_CHANNEL20(wlc_cm->wlc, channel);
	}
#ifdef SUPPORT_40MHZ
	/* We know we are now checking a 40MHZ channel, so we should only be here
	 * for NPHYS
	 */
	if (WLCISNPHY(wlc->band) || WLCISSSLPNPHY(wlc->band)) {
		u8 upper_sideband = 0, idx;
		u8 num_ch20_entries =
		    sizeof(chan20_info) / sizeof(struct chan20_info);

		if (!VALID_40CHANSPEC_IN_BAND(wlc, CHSPEC_WLCBANDUNIT(chspec)))
			return false;

		if (dualband) {
			if (!VALID_CHANNEL20_DB(wlc, LOWER_20_SB(channel)) ||
			    !VALID_CHANNEL20_DB(wlc, UPPER_20_SB(channel)))
				return false;
		} else {
			if (!VALID_CHANNEL20(wlc, LOWER_20_SB(channel)) ||
			    !VALID_CHANNEL20(wlc, UPPER_20_SB(channel)))
				return false;
		}

		/* find the lower sideband info in the sideband array */
		for (idx = 0; idx < num_ch20_entries; idx++) {
			if (chan20_info[idx].sb == LOWER_20_SB(channel))
				upper_sideband = chan20_info[idx].adj_sbs;
		}
		/* check that the lower sideband allows an upper sideband */
		if ((upper_sideband & (CH_UPPER_SB | CH_EWA_VALID)) ==
		    (CH_UPPER_SB | CH_EWA_VALID))
			return true;
		return false;
	}
#endif				/* 40 MHZ */

	return false;
}

bool wlc_valid_chanspec_db(wlc_cm_info_t *wlc_cm, chanspec_t chspec)
{
	return wlc_valid_chanspec_ext(wlc_cm, chspec, true);
}
