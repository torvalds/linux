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

#ifndef _BRCM_CHANNEL_H_
#define _BRCM_CHANNEL_H_

/* conversion for phy txpwr calculations that use .25 dB units */
#define BRCMS_TXPWR_DB_FACTOR 4


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

/* max of BAND_5G_PWR_LVLS and 6 for 2.4 GHz */
#define BRCMS_MAXPWR_TBL_SIZE		6
/* max of BAND_5G_PWR_LVLS and 14 for 2.4 GHz */
#define BRCMS_MAXPWR_MIMO_TBL_SIZE	14

#define NBANDS(wlc) ((wlc)->pub->_nbands)
#define NBANDS_PUB(pub) ((pub)->_nbands)
#define NBANDS_HW(hw) ((hw)->_nbands)

#define IS_SINGLEBAND_5G(device)	0

/* locale channel and power info. */
struct locale_info {
	u32 valid_channels;
	/* List of radar sensitive channels */
	u8 radar_channels;
	/* List of channels used only if APs are detected */
	u8 restricted_channels;
	/* Max tx pwr in qdBm for each sub-band */
	s8 maxpwr[BRCMS_MAXPWR_TBL_SIZE];
	s8 pub_maxpwr[BAND_5G_PWR_LVLS];	/* Country IE advertised max tx pwr in dBm
						 * per sub-band
						 */
	u8 flags;
};

/* bits for locale_info flags */
#define BRCMS_PEAK_CONDUCTED	0x00	/* Peak for locals */
#define BRCMS_EIRP		0x01	/* Flag for EIRP */
#define BRCMS_DFS_TPC		0x02	/* Flag for DFS TPC */
#define BRCMS_NO_OFDM		0x04	/* Flag for No OFDM */
#define BRCMS_NO_40MHZ		0x08	/* Flag for No MIMO 40MHz */
#define BRCMS_NO_MIMO		0x10	/* Flag for No MIMO, 20 or 40 MHz */
#define BRCMS_RADAR_TYPE_EU       0x20	/* Flag for EU */
#define BRCMS_DFS_FCC             BRCMS_DFS_TPC	/* Flag for DFS FCC */
#define BRCMS_DFS_EU (BRCMS_DFS_TPC | BRCMS_RADAR_TYPE_EU) /* Flag for DFS EU */

#define ISDFS_EU(fl)		(((fl) & BRCMS_DFS_EU) == BRCMS_DFS_EU)

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

extern struct brcms_cm_info *
brcms_c_channel_mgr_attach(struct brcms_c_info *wlc);

extern void brcms_c_channel_mgr_detach(struct brcms_cm_info *wlc_cm);

extern u8 brcms_c_channel_locale_flags_in_band(struct brcms_cm_info *wlc_cm,
					   uint bandunit);

extern bool brcms_c_valid_chanspec_db(struct brcms_cm_info *wlc_cm,
				      chanspec_t chspec);

extern void brcms_c_channel_reg_limits(struct brcms_cm_info *wlc_cm,
				   chanspec_t chanspec,
				   struct txpwr_limits *txpwr);
extern void brcms_c_channel_set_chanspec(struct brcms_cm_info *wlc_cm,
				     chanspec_t chanspec,
				     u8 local_constraint_qdbm);

#endif				/* _WLC_CHANNEL_H */
