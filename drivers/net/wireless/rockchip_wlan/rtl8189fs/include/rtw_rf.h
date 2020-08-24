/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/
#ifndef	__RTW_RF_H_
#define __RTW_RF_H_

#define NumRates	(13)
#define	B_MODE_RATE_NUM	(4)
#define	G_MODE_RATE_NUM	(8)
#define	G_MODE_BASIC_RATE_NUM	(3)
/* slot time for 11g */
#define SHORT_SLOT_TIME					9
#define NON_SHORT_SLOT_TIME				20

#define CENTER_CH_2G_40M_NUM	9
#define CENTER_CH_2G_NUM		14
#define CENTER_CH_5G_20M_NUM	28	/* 20M center channels */
#define CENTER_CH_5G_40M_NUM	14	/* 40M center channels */
#define CENTER_CH_5G_80M_NUM	7	/* 80M center channels */
#define CENTER_CH_5G_160M_NUM	3	/* 160M center channels */
#define CENTER_CH_5G_ALL_NUM	(CENTER_CH_5G_20M_NUM + CENTER_CH_5G_40M_NUM + CENTER_CH_5G_80M_NUM)

#define	MAX_CHANNEL_NUM_2G	CENTER_CH_2G_NUM
#define	MAX_CHANNEL_NUM_5G	CENTER_CH_5G_20M_NUM
#define	MAX_CHANNEL_NUM		(MAX_CHANNEL_NUM_2G + MAX_CHANNEL_NUM_5G)

extern u8 center_ch_2g[CENTER_CH_2G_NUM];
extern u8 center_ch_2g_40m[CENTER_CH_2G_40M_NUM];

u8 center_chs_2g_num(u8 bw);
u8 center_chs_2g(u8 bw, u8 id);

extern u8 center_ch_5g_20m[CENTER_CH_5G_20M_NUM];
extern u8 center_ch_5g_40m[CENTER_CH_5G_40M_NUM];
extern u8 center_ch_5g_20m_40m[CENTER_CH_5G_20M_NUM + CENTER_CH_5G_40M_NUM];
extern u8 center_ch_5g_80m[CENTER_CH_5G_80M_NUM];
extern u8 center_ch_5g_all[CENTER_CH_5G_ALL_NUM];

u8 center_chs_5g_num(u8 bw);
u8 center_chs_5g(u8 bw, u8 id);

u8 rtw_get_scch_by_cch_offset(u8 cch, u8 bw, u8 offset);

u8 rtw_get_op_chs_by_cch_bw(u8 cch, u8 bw, u8 **op_chs, u8 *op_ch_num);

u8 rtw_get_ch_group(u8 ch, u8 *group, u8 *cck_group);

typedef enum _CAPABILITY {
	cESS			= 0x0001,
	cIBSS			= 0x0002,
	cPollable		= 0x0004,
	cPollReq			= 0x0008,
	cPrivacy		= 0x0010,
	cShortPreamble	= 0x0020,
	cPBCC			= 0x0040,
	cChannelAgility	= 0x0080,
	cSpectrumMgnt	= 0x0100,
	cQos			= 0x0200,	/* For HCCA, use with CF-Pollable and CF-PollReq */
	cShortSlotTime	= 0x0400,
	cAPSD			= 0x0800,
	cRM				= 0x1000,	/* RRM (Radio Request Measurement) */
	cDSSS_OFDM	= 0x2000,
	cDelayedBA		= 0x4000,
	cImmediateBA	= 0x8000,
} CAPABILITY, *PCAPABILITY;

enum	_REG_PREAMBLE_MODE {
	PREAMBLE_LONG	= 1,
	PREAMBLE_AUTO	= 2,
	PREAMBLE_SHORT	= 3,
};

#define rf_path_char(path) (((path) >= RF_PATH_MAX) ? 'X' : 'A' + (path))

/* Bandwidth Offset */
#define HAL_PRIME_CHNL_OFFSET_DONT_CARE	0
#define HAL_PRIME_CHNL_OFFSET_LOWER	1
#define HAL_PRIME_CHNL_OFFSET_UPPER	2

typedef enum _BAND_TYPE {
	BAND_ON_2_4G = 0,
	BAND_ON_5G = 1,
	BAND_MAX,
} BAND_TYPE, *PBAND_TYPE;

extern const char *const _band_str[];
#define band_str(band) (((band) >= BAND_MAX) ? _band_str[BAND_MAX] : _band_str[(band)])

extern const u8 _band_to_band_cap[];
#define band_to_band_cap(band) (((band) >= BAND_MAX) ? _band_to_band_cap[BAND_MAX] : _band_to_band_cap[(band)])


extern const char *const _ch_width_str[];
#define ch_width_str(bw) (((bw) < CHANNEL_WIDTH_MAX) ? _ch_width_str[(bw)] : "CHANNEL_WIDTH_MAX")

extern const u8 _ch_width_to_bw_cap[];
#define ch_width_to_bw_cap(bw) (((bw) < CHANNEL_WIDTH_MAX) ? _ch_width_to_bw_cap[(bw)] : 0)

/*
 * Represent Extention Channel Offset in HT Capabilities
 * This is available only in 40Mhz mode.
 *   */
typedef enum _EXTCHNL_OFFSET {
	EXTCHNL_OFFSET_NO_EXT = 0,
	EXTCHNL_OFFSET_UPPER = 1,
	EXTCHNL_OFFSET_NO_DEF = 2,
	EXTCHNL_OFFSET_LOWER = 3,
} EXTCHNL_OFFSET, *PEXTCHNL_OFFSET;

typedef enum _VHT_DATA_SC {
	VHT_DATA_SC_DONOT_CARE = 0,
	VHT_DATA_SC_20_UPPER_OF_80MHZ = 1,
	VHT_DATA_SC_20_LOWER_OF_80MHZ = 2,
	VHT_DATA_SC_20_UPPERST_OF_80MHZ = 3,
	VHT_DATA_SC_20_LOWEST_OF_80MHZ = 4,
	VHT_DATA_SC_20_RECV1 = 5,
	VHT_DATA_SC_20_RECV2 = 6,
	VHT_DATA_SC_20_RECV3 = 7,
	VHT_DATA_SC_20_RECV4 = 8,
	VHT_DATA_SC_40_UPPER_OF_80MHZ = 9,
	VHT_DATA_SC_40_LOWER_OF_80MHZ = 10,
} VHT_DATA_SC, *PVHT_DATA_SC_E;

typedef enum _PROTECTION_MODE {
	PROTECTION_MODE_AUTO = 0,
	PROTECTION_MODE_FORCE_ENABLE = 1,
	PROTECTION_MODE_FORCE_DISABLE = 2,
} PROTECTION_MODE, *PPROTECTION_MODE;

#define RF_TYPE_VALID(rf_type) (rf_type < RF_TYPE_MAX)

extern const u8 _rf_type_to_rf_tx_cnt[];
#define rf_type_to_rf_tx_cnt(rf_type) (RF_TYPE_VALID(rf_type) ? _rf_type_to_rf_tx_cnt[rf_type] : 0)

extern const u8 _rf_type_to_rf_rx_cnt[];
#define rf_type_to_rf_rx_cnt(rf_type) (RF_TYPE_VALID(rf_type) ? _rf_type_to_rf_rx_cnt[rf_type] : 0)

int rtw_ch2freq(int chan);
int rtw_freq2ch(int freq);
bool rtw_chbw_to_freq_range(u8 ch, u8 bw, u8 offset, u32 *hi, u32 *lo);

struct rf_ctl_t;

typedef enum _REGULATION_TXPWR_LMT {
	TXPWR_LMT_NONE = 0, /* no limit */
	TXPWR_LMT_FCC = 1,
	TXPWR_LMT_MKK = 2,
	TXPWR_LMT_ETSI = 3,
	TXPWR_LMT_IC = 4,
	TXPWR_LMT_KCC = 5,
	TXPWR_LMT_ACMA = 6,
	TXPWR_LMT_CHILE = 7,
	TXPWR_LMT_MEXICO = 8,
	TXPWR_LMT_WW = 9, /* smallest of all available limit, keep last */
} REGULATION_TXPWR_LMT;

extern const char *const _regd_str[];
#define regd_str(regd) (((regd) > TXPWR_LMT_WW) ? _regd_str[TXPWR_LMT_WW] : _regd_str[(regd)])

#if CONFIG_TXPWR_LIMIT
struct regd_exc_ent {
	_list list;
	char country[2];
	u8 domain;
	char regd_name[0];
};

void dump_regd_exc_list(void *sel, struct rf_ctl_t *rfctl);
void rtw_regd_exc_add_with_nlen(struct rf_ctl_t *rfctl, const char *country, u8 domain, const char *regd_name, u32 nlen);
void rtw_regd_exc_add(struct rf_ctl_t *rfctl, const char *country, u8 domain, const char *regd_name);
struct regd_exc_ent *_rtw_regd_exc_search(struct rf_ctl_t *rfctl, const char *country, u8 domain);
struct regd_exc_ent *rtw_regd_exc_search(struct rf_ctl_t *rfctl, const char *country, u8 domain);
void rtw_regd_exc_list_free(struct rf_ctl_t *rfctl);

void dump_txpwr_lmt(void *sel, _adapter *adapter);
void rtw_txpwr_lmt_add_with_nlen(struct rf_ctl_t *rfctl, const char *regd_name, u32 nlen
	, u8 band, u8 bw, u8 tlrs, u8 ntx_idx, u8 ch_idx, s8 lmt);
void rtw_txpwr_lmt_add(struct rf_ctl_t *rfctl, const char *regd_name
	, u8 band, u8 bw, u8 tlrs, u8 ntx_idx, u8 ch_idx, s8 lmt);
struct txpwr_lmt_ent *_rtw_txpwr_lmt_get_by_name(struct rf_ctl_t *rfctl, const char *regd_name);
struct txpwr_lmt_ent *rtw_txpwr_lmt_get_by_name(struct rf_ctl_t *rfctl, const char *regd_name);
void rtw_txpwr_lmt_list_free(struct rf_ctl_t *rfctl);
#endif /* CONFIG_TXPWR_LIMIT */

#define BB_GAIN_2G 0
#ifdef CONFIG_IEEE80211_BAND_5GHZ
#define BB_GAIN_5GLB1 1
#define BB_GAIN_5GLB2 2
#define BB_GAIN_5GMB1 3
#define BB_GAIN_5GMB2 4
#define BB_GAIN_5GHB 5
#endif

#ifdef CONFIG_IEEE80211_BAND_5GHZ
#define BB_GAIN_NUM 6
#else
#define BB_GAIN_NUM 1
#endif

int rtw_ch_to_bb_gain_sel(int ch);
void rtw_rf_set_tx_gain_offset(_adapter *adapter, u8 path, s8 offset);
void rtw_rf_apply_tx_gain_offset(_adapter *adapter, u8 ch);

/* only check channel ranges */
#define rtw_is_2g_ch(ch) (ch >= 1 && ch <= 14)
#define rtw_is_5g_ch(ch) ((ch) >= 36 && (ch) <= 177)
#define rtw_is_same_band(a, b) \
	((rtw_is_2g_ch(a) && rtw_is_2g_ch(b)) \
	|| (rtw_is_5g_ch(a) && rtw_is_5g_ch(b)))

#define rtw_is_5g_band1(ch) ((ch) >= 36 && (ch) <= 48)
#define rtw_is_5g_band2(ch) ((ch) >= 52 && (ch) <= 64)
#define rtw_is_5g_band3(ch) ((ch) >= 100 && (ch) <= 144)
#define rtw_is_5g_band4(ch) ((ch) >= 149 && (ch) <= 177)
#define rtw_is_same_5g_band(a, b) \
	((rtw_is_5g_band1(a) && rtw_is_5g_band1(b)) \
	|| (rtw_is_5g_band2(a) && rtw_is_5g_band2(b)) \
	|| (rtw_is_5g_band3(a) && rtw_is_5g_band3(b)) \
	|| (rtw_is_5g_band4(a) && rtw_is_5g_band4(b)))

u8 rtw_is_dfs_range(u32 hi, u32 lo);
u8 rtw_is_dfs_ch(u8 ch);
u8 rtw_is_dfs_chbw(u8 ch, u8 bw, u8 offset);
bool rtw_is_long_cac_range(u32 hi, u32 lo, u8 dfs_region);
bool rtw_is_long_cac_ch(u8 ch, u8 bw, u8 offset, u8 dfs_region);

#endif /* _RTL8711_RF_H_ */
