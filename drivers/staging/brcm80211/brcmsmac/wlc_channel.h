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

#ifndef _WLC_CHANNEL_H_
#define _WLC_CHANNEL_H_

#define WLC_TXPWR_DB_FACTOR 4	/* conversion for phy txpwr cacluations that use .25 dB units */

struct wlc_info;

/* maxpwr mapping to 5GHz band channels:
 * maxpwr[0] - channels [34-48]
 * maxpwr[1] - channels [52-60]
 * maxpwr[2] - channels [62-64]
 * maxpwr[3] - channels [100-140]
 * maxpwr[4] - channels [149-165]
 */
#define BAND_5G_PWR_LVLS	5	/* 5 power levels for 5G */

/* power level in group of 2.4GHz band channels:
 * maxpwr[0] - CCK  channels [1]
 * maxpwr[1] - CCK  channels [2-10]
 * maxpwr[2] - CCK  channels [11-14]
 * maxpwr[3] - OFDM channels [1]
 * maxpwr[4] - OFDM channels [2-10]
 * maxpwr[5] - OFDM channels [11-14]
 */

/* macro to get 2.4 GHz channel group index for tx power */
#define CHANNEL_POWER_IDX_2G_CCK(c) (((c) < 2) ? 0 : (((c) < 11) ? 1 : 2))	/* cck index */
#define CHANNEL_POWER_IDX_2G_OFDM(c) (((c) < 2) ? 3 : (((c) < 11) ? 4 : 5))	/* ofdm index */

/* macro to get 5 GHz channel group index for tx power */
#define CHANNEL_POWER_IDX_5G(c) \
	(((c) < 52) ? 0 : (((c) < 62) ? 1 : (((c) < 100) ? 2 : (((c) < 149) ? 3 : 4))))

#define WLC_MAXPWR_TBL_SIZE		6	/* max of BAND_5G_PWR_LVLS and 6 for 2.4 GHz */
#define WLC_MAXPWR_MIMO_TBL_SIZE	14	/* max of BAND_5G_PWR_LVLS and 14 for 2.4 GHz */

/* locale channel and power info. */
typedef struct {
	u32 valid_channels;
	u8 radar_channels;	/* List of radar sensitive channels */
	u8 restricted_channels;	/* List of channels used only if APs are detected */
	s8 maxpwr[WLC_MAXPWR_TBL_SIZE];	/* Max tx pwr in qdBm for each sub-band */
	s8 pub_maxpwr[BAND_5G_PWR_LVLS];	/* Country IE advertised max tx pwr in dBm
						 * per sub-band
						 */
	u8 flags;
} locale_info_t;

/* bits for locale_info flags */
#define WLC_PEAK_CONDUCTED	0x00	/* Peak for locals */
#define WLC_EIRP		0x01	/* Flag for EIRP */
#define WLC_DFS_TPC		0x02	/* Flag for DFS TPC */
#define WLC_NO_OFDM		0x04	/* Flag for No OFDM */
#define WLC_NO_40MHZ		0x08	/* Flag for No MIMO 40MHz */
#define WLC_NO_MIMO		0x10	/* Flag for No MIMO, 20 or 40 MHz */
#define WLC_RADAR_TYPE_EU       0x20	/* Flag for EU */
#define WLC_DFS_FCC             WLC_DFS_TPC	/* Flag for DFS FCC */
#define WLC_DFS_EU              (WLC_DFS_TPC | WLC_RADAR_TYPE_EU)	/* Flag for DFS EU */

#define ISDFS_EU(fl)		(((fl) & WLC_DFS_EU) == WLC_DFS_EU)

/* locale per-channel tx power limits for MIMO frames
 * maxpwr arrays are index by channel for 2.4 GHz limits, and
 * by sub-band for 5 GHz limits using CHANNEL_POWER_IDX_5G(channel)
 */
typedef struct {
	s8 maxpwr20[WLC_MAXPWR_MIMO_TBL_SIZE];	/* tx 20 MHz power limits, qdBm units */
	s8 maxpwr40[WLC_MAXPWR_MIMO_TBL_SIZE];	/* tx 40 MHz power limits, qdBm units */
	u8 flags;
} locale_mimo_info_t;

extern const chanvec_t chanvec_all_2G;
extern const chanvec_t chanvec_all_5G;

/*
 * Country names and abbreviations with locale defined from ISO 3166
 */
struct country_info {
	const u8 locale_2G;	/* 2.4G band locale */
	const u8 locale_5G;	/* 5G band locale */
	const u8 locale_mimo_2G;	/* 2.4G mimo info */
	const u8 locale_mimo_5G;	/* 5G mimo info */
};

typedef struct country_info country_info_t;

typedef struct wlc_cm_info wlc_cm_info_t;

extern wlc_cm_info_t *wlc_channel_mgr_attach(struct wlc_info *wlc);
extern void wlc_channel_mgr_detach(wlc_cm_info_t *wlc_cm);

extern u8 wlc_channel_locale_flags_in_band(wlc_cm_info_t *wlc_cm,
					   uint bandunit);

extern bool wlc_valid_chanspec_db(wlc_cm_info_t *wlc_cm, chanspec_t chspec);

extern void wlc_channel_reg_limits(wlc_cm_info_t *wlc_cm,
				   chanspec_t chanspec,
				   struct txpwr_limits *txpwr);
extern void wlc_channel_set_chanspec(wlc_cm_info_t *wlc_cm,
				     chanspec_t chanspec,
				     u8 local_constraint_qdbm);

#endif				/* _WLC_CHANNEL_H */
