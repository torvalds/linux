/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef	__RTW_RF_H_
#define __RTW_RF_H_


#define OFDM_PHY		1
#define MIXED_PHY		2
#define CCK_PHY		3

#define b_mode_rate_num 4
#define g_mode_rate_num 8
#define NumRates	(13)

/* slot time for 11g */
#define SHORT_SLOT_TIME					9
#define NON_SHORT_SLOT_TIME				20

#define RTL8711_RF_MAX_SENS 6
#define RTL8711_RF_DEF_SENS 4

/*
 * We now define the following channels as the max channels in each channel plan.
 * 2G, total 14 chnls
 * {1,2,3,4,5,6,7,8,9,10,11,12,13,14}
 * 5G, total 25 chnls
 * {36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,132,136,140,144,149,153,157,161,165}
 */
#define	MAX_CHANNEL_NUM_2G	14
#define	MAX_CHANNEL_NUM_5G	25
#define	MAX_CHANNEL_NUM		(MAX_CHANNEL_NUM_2G + MAX_CHANNEL_NUM_5G)

#define CENTER_CH_2G_40M_NUM	9
#define CENTER_CH_2G_NUM		14
#define CENTER_CH_5G_20M_NUM	28	/* 20M center channels */
#define CENTER_CH_5G_40M_NUM	14	/* 40M center channels */
#define CENTER_CH_5G_80M_NUM	7	/* 80M center channels */
#define CENTER_CH_5G_160M_NUM	3	/* 160M center channels */
#define CENTER_CH_5G_ALL_NUM	(CENTER_CH_5G_20M_NUM + CENTER_CH_5G_40M_NUM + CENTER_CH_5G_80M_NUM)

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

/* #define NUM_REGULATORYS	21 */
#define NUM_REGULATORYS	1

/* Country codes */
#define USA							0x555320
#define EUROPE						0x1 /* temp, should be provided later	 */
#define JAPAN						0x2 /* temp, should be provided later	 */

struct	regulatory_class {
	u32	starting_freq;					/* MHz, */
	u8	channel_set[MAX_CHANNEL_NUM];
	u8	channel_cck_power[MAX_CHANNEL_NUM];/* dbm */
	u8	channel_ofdm_power[MAX_CHANNEL_NUM];/* dbm */
	u8	txpower_limit;  				/* dbm */
	u8	channel_spacing;				/* MHz */
	u8	modem;
};

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


enum _RTL8712_RF_MIMO_CONFIG_ {
	RTL8712_RFCONFIG_1T = 0x10,
	RTL8712_RFCONFIG_2T = 0x20,
	RTL8712_RFCONFIG_1R = 0x01,
	RTL8712_RFCONFIG_2R = 0x02,
	RTL8712_RFCONFIG_1T1R = 0x11,
	RTL8712_RFCONFIG_1T2R = 0x12,
	RTL8712_RFCONFIG_TURBO = 0x92,
	RTL8712_RFCONFIG_2T2R = 0x22
};

typedef enum _RF_PATH {
	RF_PATH_A = 0,
	RF_PATH_B = 1,
	RF_PATH_C = 2,
	RF_PATH_D = 3,
} RF_PATH, *PRF_PATH;

#define rf_path_char(path) (((path) >= RF_PATH_MAX) ? 'X' : 'A' + (path))

/* Bandwidth Offset */
#define HAL_PRIME_CHNL_OFFSET_DONT_CARE	0
#define HAL_PRIME_CHNL_OFFSET_LOWER	1
#define HAL_PRIME_CHNL_OFFSET_UPPER	2

typedef enum _BAND_TYPE {
	BAND_ON_2_4G = 0,
	BAND_ON_5G = 1,
	BAND_ON_BOTH = 2,
	BAND_MAX = 3,
} BAND_TYPE, *PBAND_TYPE;

extern const char *const _band_str[];
#define band_str(band) (((band) >= BAND_MAX) ? _band_str[BAND_MAX] : _band_str[(band)])

extern const u8 _band_to_band_cap[];
#define band_to_band_cap(band) (((band) >= BAND_MAX) ? _band_to_band_cap[BAND_MAX] : _band_to_band_cap[(band)])

/* Represent Channel Width in HT Capabilities
 *   */
typedef enum _CHANNEL_WIDTH {
	CHANNEL_WIDTH_20 = 0,
	CHANNEL_WIDTH_40 = 1,
	CHANNEL_WIDTH_80 = 2,
	CHANNEL_WIDTH_160 = 3,
	CHANNEL_WIDTH_80_80 = 4,
	CHANNEL_WIDTH_MAX = 5,
} CHANNEL_WIDTH, *PCHANNEL_WIDTH;

extern const char *const _ch_width_str[];
#define ch_width_str(bw) (((bw) >= CHANNEL_WIDTH_MAX) ? _ch_width_str[CHANNEL_WIDTH_MAX] : _ch_width_str[(bw)])

extern const u8 _ch_width_to_bw_cap[];
#define ch_width_to_bw_cap(bw) (((bw) >= CHANNEL_WIDTH_MAX) ? _ch_width_to_bw_cap[CHANNEL_WIDTH_MAX] : _ch_width_to_bw_cap[(bw)])

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

typedef	enum _RT_RF_TYPE_DEFINITION {
	RF_1T2R = 0,
	RF_2T4R = 1,
	RF_2T2R = 2,
	RF_1T1R = 3,
	RF_2T2R_GREEN = 4,
	RF_2T3R = 5,
	RF_3T3R = 6,
	RF_3T4R	= 7,
	RF_4T4R	= 8,

	RF_MAX_TYPE = 0xF, /* u1Byte */
} RT_RF_TYPE_DEF_E;

int rtw_ch2freq(int chan);
int rtw_freq2ch(int freq);
bool rtw_chbw_to_freq_range(u8 ch, u8 bw, u8 offset, u32 *hi, u32 *lo);

#define RTW_MODULE_RTL8821AE_HMC_M2		BIT0 /* RTL8821AE(HMC + M.2) */
#define RTW_MODULE_RTL8821AU			BIT1 /* RTL8821AU */
#define RTW_MODULE_RTL8812AENF_NGFF		BIT2 /* RTL8812AENF(8812AE+8761)_NGFF */
#define RTW_MODULE_RTL8812AEBT_HMC		BIT3 /* RTL8812AEBT(8812AE+8761)_HMC */
#define RTW_MODULE_RTL8188EE_HMC_M2		BIT4 /* RTL8188EE(HMC + M.2) */
#define RTW_MODULE_RTL8723BE_HMC_M2		BIT5 /* RTL8723BE(HMC + M.2) */
#define RTW_MODULE_RTL8723BS_NGFF1216	BIT6 /* RTL8723BS(NGFF1216) */
#define RTW_MODULE_RTL8192EEBT_HMC_M2	BIT7 /* RTL8192EEBT(8192EE+8761AU)_(HMC + M.2) */

#define IS_ALPHA2_NO_SPECIFIED(_alpha2) ((*((u16 *)(_alpha2))) == 0xFFFF)

struct country_chplan {
	char alpha2[2];
	u8 chplan;
#ifdef CONFIG_80211AC_VHT
	u8 en_11ac;
#endif
#if RTW_DEF_MODULE_REGULATORY_CERT
	u8 def_module_flags; /* RTW_MODULE_RTLXXX */
#endif
};

#ifdef CONFIG_80211AC_VHT
#define COUNTRY_CHPLAN_EN_11AC(_ent) ((_ent)->en_11ac)
#else
#define COUNTRY_CHPLAN_EN_11AC(_ent) 0
#endif

#if RTW_DEF_MODULE_REGULATORY_CERT
#define COUNTRY_CHPLAN_DEF_MODULE_FALGS(_ent) ((_ent)->def_module_flags)
#else
#define COUNTRY_CHPLAN_DEF_MODULE_FALGS(_ent) 0
#endif

const struct country_chplan *rtw_get_chplan_from_country(const char *country_code);

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

bool rtw_is_dfs_range(u32 hi, u32 lo);
bool rtw_is_dfs_ch(u8 ch, u8 bw, u8 offset);
bool rtw_is_long_cac_range(u32 hi, u32 lo, u8 dfs_region);
bool rtw_is_long_cac_ch(u8 ch, u8 bw, u8 offset, u8 dfs_region);

#endif /* _RTL8711_RF_H_ */
