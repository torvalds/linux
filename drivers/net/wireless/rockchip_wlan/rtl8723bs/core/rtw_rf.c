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
#define _RTW_RF_C_

#include <drv_types.h>
#include <hal_data.h>

u8 center_ch_2g[CENTER_CH_2G_NUM] = {
/* G00 */1, 2,
/* G01 */3, 4, 5,
/* G02 */6, 7, 8,
/* G03 */9, 10, 11,
/* G04 */12, 13,
/* G05 */14
};

u8 center_ch_2g_40m[CENTER_CH_2G_40M_NUM] = {
	3,
	4,
	5,
	6,
	7,
	8,
	9,
	10,
	11,
};

u8 op_chs_of_cch_2g_40m[CENTER_CH_2G_40M_NUM][2] = {
	{1, 5}, /* 3 */
	{2, 6}, /* 4 */
	{3, 7}, /* 5 */
	{4, 8}, /* 6 */
	{5, 9}, /* 7 */
	{6, 10}, /* 8 */
	{7, 11}, /* 9 */
	{8, 12}, /* 10 */
	{9, 13}, /* 11 */
};

u8 center_ch_5g_all[CENTER_CH_5G_ALL_NUM] = {
/* G00 */36, 38, 40,
	42,
/* G01 */44, 46, 48,
	/* 50, */
/* G02 */52, 54, 56,
	58,
/* G03 */60, 62, 64,
/* G04 */100, 102, 104,
	106,
/* G05 */108, 110, 112,
	/* 114, */
/* G06 */116, 118, 120,
	122,
/* G07 */124, 126, 128,
/* G08 */132, 134, 136,
	138,
/* G09 */140, 142, 144,
/* G10 */149, 151, 153,
	155,
/* G11 */157, 159, 161,
	/* 163, */
/* G12 */165, 167, 169,
	171,
/* G13 */173, 175, 177
};

u8 center_ch_5g_20m[CENTER_CH_5G_20M_NUM] = {
/* G00 */36, 40,
/* G01 */44, 48,
/* G02 */52, 56,
/* G03 */60, 64,
/* G04 */100, 104,
/* G05 */108, 112,
/* G06 */116, 120,
/* G07 */124, 128,
/* G08 */132, 136,
/* G09 */140, 144,
/* G10 */149, 153,
/* G11 */157, 161,
/* G12 */165, 169,
/* G13 */173, 177
};

u8 center_ch_5g_40m[CENTER_CH_5G_40M_NUM] = {
/* G00 */38,
/* G01 */46,
/* G02 */54,
/* G03 */62,
/* G04 */102,
/* G05 */110,
/* G06 */118,
/* G07 */126,
/* G08 */134,
/* G09 */142,
/* G10 */151,
/* G11 */159,
/* G12 */167,
/* G13 */175
};

u8 center_ch_5g_20m_40m[CENTER_CH_5G_20M_NUM + CENTER_CH_5G_40M_NUM] = {
/* G00 */36, 38, 40,
/* G01 */44, 46, 48,
/* G02 */52, 54, 56,
/* G03 */60, 62, 64,
/* G04 */100, 102, 104,
/* G05 */108, 110, 112,
/* G06 */116, 118, 120,
/* G07 */124, 126, 128,
/* G08 */132, 134, 136,
/* G09 */140, 142, 144,
/* G10 */149, 151, 153,
/* G11 */157, 159, 161,
/* G12 */165, 167, 169,
/* G13 */173, 175, 177
};

u8 op_chs_of_cch_5g_40m[CENTER_CH_5G_40M_NUM][2] = {
	{36, 40}, /* 38 */
	{44, 48}, /* 46 */
	{52, 56}, /* 54 */
	{60, 64}, /* 62 */
	{100, 104}, /* 102 */
	{108, 112}, /* 110 */
	{116, 120}, /* 118 */
	{124, 128}, /* 126 */
	{132, 136}, /* 134 */
	{140, 144}, /* 142 */
	{149, 153}, /* 151 */
	{157, 161}, /* 159 */
	{165, 169}, /* 167 */
	{173, 177}, /* 175 */
};

u8 center_ch_5g_80m[CENTER_CH_5G_80M_NUM] = {
/* G00 ~ G01*/42,
/* G02 ~ G03*/58,
/* G04 ~ G05*/106,
/* G06 ~ G07*/122,
/* G08 ~ G09*/138,
/* G10 ~ G11*/155,
/* G12 ~ G13*/171
};

u8 op_chs_of_cch_5g_80m[CENTER_CH_5G_80M_NUM][4] = {
	{36, 40, 44, 48}, /* 42 */
	{52, 56, 60, 64}, /* 58 */
	{100, 104, 108, 112}, /* 106 */
	{116, 120, 124, 128}, /* 122 */
	{132, 136, 140, 144}, /* 138 */
	{149, 153, 157, 161}, /* 155 */
	{165, 169, 173, 177}, /* 171 */
};

u8 center_ch_5g_160m[CENTER_CH_5G_160M_NUM] = {
/* G00 ~ G03*/50,
/* G04 ~ G07*/114,
/* G10 ~ G13*/163
};

u8 op_chs_of_cch_5g_160m[CENTER_CH_5G_160M_NUM][8] = {
	{36, 40, 44, 48, 52, 56, 60, 64}, /* 50 */
	{100, 104, 108, 112, 116, 120, 124, 128}, /* 114 */
	{149, 153, 157, 161, 165, 169, 173, 177}, /* 163 */
};

struct center_chs_ent_t {
	u8 ch_num;
	u8 *chs;
};

struct center_chs_ent_t center_chs_2g_by_bw[] = {
	{CENTER_CH_2G_NUM, center_ch_2g},
	{CENTER_CH_2G_40M_NUM, center_ch_2g_40m},
};

struct center_chs_ent_t center_chs_5g_by_bw[] = {
	{CENTER_CH_5G_20M_NUM, center_ch_5g_20m},
	{CENTER_CH_5G_40M_NUM, center_ch_5g_40m},
	{CENTER_CH_5G_80M_NUM, center_ch_5g_80m},
	{CENTER_CH_5G_160M_NUM, center_ch_5g_160m},
};

/*
 * Get center channel of smaller bandwidth by @param cch, @param bw, @param offset
 * @cch: the given center channel
 * @bw: the given bandwidth
 * @offset: the given primary SC offset of the given bandwidth
 *
 * return center channel of smaller bandiwdth if valid, or 0
 */
u8 rtw_get_scch_by_cch_offset(u8 cch, u8 bw, u8 offset)
{
	int i;
	u8 t_cch = 0;

	if (bw == CHANNEL_WIDTH_20) {
		t_cch = cch;
		goto exit;
	}

	if (offset == HAL_PRIME_CHNL_OFFSET_DONT_CARE) {
		rtw_warn_on(1);
		goto exit;
	}

	/* 2.4G, 40MHz */
	if (cch >= 3 && cch <= 11 && bw == CHANNEL_WIDTH_40) {
		t_cch = (offset == HAL_PRIME_CHNL_OFFSET_UPPER) ? cch + 2 : cch - 2;
		goto exit;
	}

	/* 5G, 160MHz */
	if (cch >= 50 && cch <= 163 && bw == CHANNEL_WIDTH_160) {
		t_cch = (offset == HAL_PRIME_CHNL_OFFSET_UPPER) ? cch + 8 : cch - 8;
		goto exit;

	/* 5G, 80MHz */
	} else if (cch >= 42 && cch <= 171 && bw == CHANNEL_WIDTH_80) {
		t_cch = (offset == HAL_PRIME_CHNL_OFFSET_UPPER) ? cch + 4 : cch - 4;
		goto exit;

	/* 5G, 40MHz */
	} else if (cch >= 38 && cch <= 175 && bw == CHANNEL_WIDTH_40) {
		t_cch = (offset == HAL_PRIME_CHNL_OFFSET_UPPER) ? cch + 2 : cch - 2;
		goto exit;

	} else {
		rtw_warn_on(1);
		goto exit;
	}

exit:
	return t_cch;
}

struct op_chs_ent_t {
	u8 ch_num;
	u8 *chs;
};

struct op_chs_ent_t op_chs_of_cch_2g_by_bw[] = {
	{1, center_ch_2g},
	{2, (u8 *)op_chs_of_cch_2g_40m},
};

struct op_chs_ent_t op_chs_of_cch_5g_by_bw[] = {
	{1, center_ch_5g_20m},
	{2, (u8 *)op_chs_of_cch_5g_40m},
	{4, (u8 *)op_chs_of_cch_5g_80m},
	{8, (u8 *)op_chs_of_cch_5g_160m},
};

inline u8 center_chs_2g_num(u8 bw)
{
	if (bw > CHANNEL_WIDTH_40)
		return 0;

	return center_chs_2g_by_bw[bw].ch_num;
}

inline u8 center_chs_2g(u8 bw, u8 id)
{
	if (bw > CHANNEL_WIDTH_40)
		return 0;

	if (id >= center_chs_2g_num(bw))
		return 0;

	return center_chs_2g_by_bw[bw].chs[id];
}

inline u8 center_chs_5g_num(u8 bw)
{
	if (bw > CHANNEL_WIDTH_80)
		return 0;

	return center_chs_5g_by_bw[bw].ch_num;
}

inline u8 center_chs_5g(u8 bw, u8 id)
{
	if (bw > CHANNEL_WIDTH_80)
		return 0;

	if (id >= center_chs_5g_num(bw))
		return 0;

	return center_chs_5g_by_bw[bw].chs[id];
}

/*
 * Get available op channels by @param cch, @param bw
 * @cch: the given center channel
 * @bw: the given bandwidth
 * @op_chs: the pointer to return pointer of op channel array
 * @op_ch_num: the pointer to return pointer of op channel number
 *
 * return valid (1) or not (0)
 */
u8 rtw_get_op_chs_by_cch_bw(u8 cch, u8 bw, u8 **op_chs, u8 *op_ch_num)
{
	int i;
	struct center_chs_ent_t *c_chs_ent = NULL;
	struct op_chs_ent_t *op_chs_ent = NULL;
	u8 valid = 1;

	if (cch <= 14
		&& bw >= CHANNEL_WIDTH_20 && bw <= CHANNEL_WIDTH_40
	) {
		c_chs_ent = &center_chs_2g_by_bw[bw];
		op_chs_ent = &op_chs_of_cch_2g_by_bw[bw];
	} else if (cch >= 36 && cch <= 177
		&& bw >= CHANNEL_WIDTH_20 && bw <= CHANNEL_WIDTH_160
	) {
		c_chs_ent = &center_chs_5g_by_bw[bw];
		op_chs_ent = &op_chs_of_cch_5g_by_bw[bw];
	} else {
		valid = 0;
		goto exit;
	}

	for (i = 0; i < c_chs_ent->ch_num; i++)
		if (cch == *(c_chs_ent->chs + i))
			break;

	if (i == c_chs_ent->ch_num) {
		valid = 0;
		goto exit;
	}

	*op_chs = op_chs_ent->chs + op_chs_ent->ch_num * i;
	*op_ch_num = op_chs_ent->ch_num;

exit:
	return valid;
}

u8 rtw_get_ch_group(u8 ch, u8 *group, u8 *cck_group)
{
	BAND_TYPE band = BAND_MAX;
	s8 gp = -1, cck_gp = -1;

	if (ch <= 14) {
		band = BAND_ON_2_4G;

		if (1 <= ch && ch <= 2)
			gp = 0;
		else if (3  <= ch && ch <= 5)
			gp = 1;
		else if (6  <= ch && ch <= 8)
			gp = 2;
		else if (9  <= ch && ch <= 11)
			gp = 3;
		else if (12 <= ch && ch <= 14)
			gp = 4;
		else
			band = BAND_MAX;

		if (ch == 14)
			cck_gp = 5;
		else
			cck_gp = gp;
	} else {
		band = BAND_ON_5G;

		if (36 <= ch && ch <= 42)
			gp = 0;
		else if (44   <= ch && ch <=  48)
			gp = 1;
		else if (50   <= ch && ch <=  58)
			gp = 2;
		else if (60   <= ch && ch <=  64)
			gp = 3;
		else if (100  <= ch && ch <= 106)
			gp = 4;
		else if (108  <= ch && ch <= 114)
			gp = 5;
		else if (116  <= ch && ch <= 122)
			gp = 6;
		else if (124  <= ch && ch <= 130)
			gp = 7;
		else if (132  <= ch && ch <= 138)
			gp = 8;
		else if (140  <= ch && ch <= 144)
			gp = 9;
		else if (149  <= ch && ch <= 155)
			gp = 10;
		else if (157  <= ch && ch <= 161)
			gp = 11;
		else if (165  <= ch && ch <= 171)
			gp = 12;
		else if (173  <= ch && ch <= 177)
			gp = 13;
		else
			band = BAND_MAX;
	}

	if (band == BAND_MAX
		|| (band == BAND_ON_2_4G && cck_gp == -1)
		|| gp == -1
	) {
		RTW_WARN("%s invalid channel:%u", __func__, ch);
		rtw_warn_on(1);
		goto exit;
	}

	if (group)
		*group = gp;
	if (cck_group && band == BAND_ON_2_4G)
		*cck_group = cck_gp;

exit:
	return band;
}

int rtw_ch2freq(int chan)
{
	/* see 802.11 17.3.8.3.2 and Annex J
	* there are overlapping channel numbers in 5GHz and 2GHz bands */

	/*
	* RTK: don't consider the overlapping channel numbers: 5G channel <= 14,
	* because we don't support it. simply judge from channel number
	*/

	if (chan >= 1 && chan <= 14) {
		if (chan == 14)
			return 2484;
		else if (chan < 14)
			return 2407 + chan * 5;
	} else if (chan >= 36 && chan <= 177)
		return 5000 + chan * 5;

	return 0; /* not supported */
}

int rtw_freq2ch(int freq)
{
	/* see 802.11 17.3.8.3.2 and Annex J */
	if (freq == 2484)
		return 14;
	else if (freq < 2484)
		return (freq - 2407) / 5;
	else if (freq >= 4910 && freq <= 4980)
		return (freq - 4000) / 5;
	else if (freq <= 45000) /* DMG band lower limit */
		return (freq - 5000) / 5;
	else if (freq >= 58320 && freq <= 64800)
		return (freq - 56160) / 2160;
	else
		return 0;
}

bool rtw_chbw_to_freq_range(u8 ch, u8 bw, u8 offset, u32 *hi, u32 *lo)
{
	u8 c_ch;
	u32 freq;
	u32 hi_ret = 0, lo_ret = 0;
	int i;
	bool valid = _FALSE;

	if (hi)
		*hi = 0;
	if (lo)
		*lo = 0;

	c_ch = rtw_get_center_ch(ch, bw, offset);
	freq = rtw_ch2freq(c_ch);

	if (!freq) {
		rtw_warn_on(1);
		goto exit;
	}

	if (bw == CHANNEL_WIDTH_80) {
		hi_ret = freq + 40;
		lo_ret = freq - 40;
	} else if (bw == CHANNEL_WIDTH_40) {
		hi_ret = freq + 20;
		lo_ret = freq - 20;
	} else if (bw == CHANNEL_WIDTH_20) {
		hi_ret = freq + 10;
		lo_ret = freq - 10;
	} else
		rtw_warn_on(1);

	if (hi)
		*hi = hi_ret;
	if (lo)
		*lo = lo_ret;

	valid = _TRUE;

exit:
	return valid;
}

const char *const _ch_width_str[] = {
	"20MHz",
	"40MHz",
	"80MHz",
	"160MHz",
	"80_80MHz",
	"CHANNEL_WIDTH_MAX",
};

const u8 _ch_width_to_bw_cap[] = {
	BW_CAP_20M,
	BW_CAP_40M,
	BW_CAP_80M,
	BW_CAP_160M,
	BW_CAP_80_80M,
	0,
};

const char *const _band_str[] = {
	"2.4G",
	"5G",
	"BOTH",
	"BAND_MAX",
};

const u8 _band_to_band_cap[] = {
	BAND_CAP_2G,
	BAND_CAP_5G,
	0,
	0,
};

const u8 _rf_type_to_rf_tx_cnt[] = {
	1, /*RF_1T1R*/
	1, /*RF_1T2R*/
	2, /*RF_2T2R*/
	2, /*RF_2T3R*/
	2, /*RF_2T4R*/
	3, /*RF_3T3R*/
	3, /*RF_3T4R*/
	4, /*RF_4T4R*/
	1, /*RF_TYPE_MAX*/
};

const u8 _rf_type_to_rf_rx_cnt[] = {
	1, /*RF_1T1R*/
	2, /*RF_1T2R*/
	2, /*RF_2T2R*/
	3, /*RF_2T3R*/
	4, /*RF_2T4R*/
	3, /*RF_3T3R*/
	4, /*RF_3T4R*/
	4, /*RF_4T4R*/
	1, /*RF_TYPE_MAX*/
};

#ifdef CONFIG_80211AC_VHT
#define COUNTRY_CHPLAN_ASSIGN_EN_11AC(_val) , .en_11ac = (_val)
#else
#define COUNTRY_CHPLAN_ASSIGN_EN_11AC(_val)
#endif

#if RTW_DEF_MODULE_REGULATORY_CERT
#define COUNTRY_CHPLAN_ASSIGN_DEF_MODULE_FLAGS(_val) , .def_module_flags = (_val)
#else
#define COUNTRY_CHPLAN_ASSIGN_DEF_MODULE_FLAGS(_val)
#endif

/* has def_module_flags specified, used by common map and HAL dfference map */
#define COUNTRY_CHPLAN_ENT(_alpha2, _chplan, _en_11ac, _def_module_flags) \
	{.alpha2 = (_alpha2), .chplan = (_chplan) \
		COUNTRY_CHPLAN_ASSIGN_EN_11AC(_en_11ac) \
		COUNTRY_CHPLAN_ASSIGN_DEF_MODULE_FLAGS(_def_module_flags) \
	}

#ifdef CONFIG_CUSTOMIZED_COUNTRY_CHPLAN_MAP

#include "../platform/custom_country_chplan.h"

#elif RTW_DEF_MODULE_REGULATORY_CERT

#if (RTW_DEF_MODULE_REGULATORY_CERT & RTW_MODULE_RTL8821AE_HMC_M2) /* 2013 certify */
static const struct country_chplan RTL8821AE_HMC_M2_country_chplan_exc_map[] = {
	COUNTRY_CHPLAN_ENT("CA", 0x34, 1, 0x3FB), /* Canada */
	COUNTRY_CHPLAN_ENT("CL", 0x30, 1, 0x3F1), /* Chile */
	COUNTRY_CHPLAN_ENT("CN", 0x51, 1, 0x3FB), /* China */
	COUNTRY_CHPLAN_ENT("MX", 0x34, 1, 0x3F1), /* Mexico */
	COUNTRY_CHPLAN_ENT("MY", 0x47, 1, 0x3F1), /* Malaysia */
	COUNTRY_CHPLAN_ENT("TW", 0x39, 1, 0x3FF), /* Taiwan */
	COUNTRY_CHPLAN_ENT("UA", 0x36, 0, 0x3FB), /* Ukraine */
	COUNTRY_CHPLAN_ENT("US", 0x34, 1, 0x3FF), /* United States of America (USA) */
};
#endif

#if (RTW_DEF_MODULE_REGULATORY_CERT & RTW_MODULE_RTL8821AU) /* 2014 certify */
static const struct country_chplan RTL8821AU_country_chplan_exc_map[] = {
	COUNTRY_CHPLAN_ENT("CA", 0x34, 1, 0x3FB), /* Canada */
	COUNTRY_CHPLAN_ENT("RU", 0x59, 0, 0x3FB), /* Russia(fac/gost), Kaliningrad */
	COUNTRY_CHPLAN_ENT("TW", 0x39, 1, 0x3FF), /* Taiwan */
	COUNTRY_CHPLAN_ENT("UA", 0x36, 0, 0x3FB), /* Ukraine */
	COUNTRY_CHPLAN_ENT("US", 0x34, 1, 0x3FF), /* United States of America (USA) */
};
#endif

#if (RTW_DEF_MODULE_REGULATORY_CERT & RTW_MODULE_RTL8812AENF_NGFF) /* 2014 certify */
static const struct country_chplan RTL8812AENF_NGFF_country_chplan_exc_map[] = {
	COUNTRY_CHPLAN_ENT("TW", 0x39, 1, 0x3FF), /* Taiwan */
	COUNTRY_CHPLAN_ENT("US", 0x34, 1, 0x3FF), /* United States of America (USA) */
};
#endif

#if (RTW_DEF_MODULE_REGULATORY_CERT & RTW_MODULE_RTL8812AEBT_HMC) /* 2013 certify */
static const struct country_chplan RTL8812AEBT_HMC_country_chplan_exc_map[] = {
	COUNTRY_CHPLAN_ENT("CA", 0x34, 1, 0x3FB), /* Canada */
	COUNTRY_CHPLAN_ENT("RU", 0x59, 0, 0x3FB), /* Russia(fac/gost), Kaliningrad */
	COUNTRY_CHPLAN_ENT("TW", 0x39, 1, 0x3FF), /* Taiwan */
	COUNTRY_CHPLAN_ENT("UA", 0x36, 0, 0x3FB), /* Ukraine */
	COUNTRY_CHPLAN_ENT("US", 0x34, 1, 0x3FF), /* United States of America (USA) */
};
#endif

#if (RTW_DEF_MODULE_REGULATORY_CERT & RTW_MODULE_RTL8188EE_HMC_M2) /* 2012 certify */
static const struct country_chplan RTL8188EE_HMC_M2_country_chplan_exc_map[] = {
	COUNTRY_CHPLAN_ENT("CA", 0x20, 1, 0x3FB), /* Canada */
	COUNTRY_CHPLAN_ENT("MX", 0x34, 1, 0x3F1), /* Mexico */
	COUNTRY_CHPLAN_ENT("TW", 0x39, 1, 0x3FF), /* Taiwan */
	COUNTRY_CHPLAN_ENT("US", 0x34, 1, 0x3FF), /* United States of America (USA) */
};
#endif

#if (RTW_DEF_MODULE_REGULATORY_CERT & RTW_MODULE_RTL8723BE_HMC_M2) /* 2013 certify */
static const struct country_chplan RTL8723BE_HMC_M2_country_chplan_exc_map[] = {
	COUNTRY_CHPLAN_ENT("CA", 0x20, 1, 0x3FB), /* Canada */
	COUNTRY_CHPLAN_ENT("MX", 0x34, 1, 0x3F1), /* Mexico */
	COUNTRY_CHPLAN_ENT("TW", 0x39, 1, 0x3FF), /* Taiwan */
	COUNTRY_CHPLAN_ENT("US", 0x34, 1, 0x3FF), /* United States of America (USA) */
};
#endif

#if (RTW_DEF_MODULE_REGULATORY_CERT & RTW_MODULE_RTL8723BS_NGFF1216) /* 2014 certify */
static const struct country_chplan RTL8723BS_NGFF1216_country_chplan_exc_map[] = {
	COUNTRY_CHPLAN_ENT("CA", 0x20, 1, 0x3FB), /* Canada */
	COUNTRY_CHPLAN_ENT("MX", 0x34, 1, 0x3F1), /* Mexico */
	COUNTRY_CHPLAN_ENT("TW", 0x39, 1, 0x3FF), /* Taiwan */
	COUNTRY_CHPLAN_ENT("US", 0x34, 1, 0x3FF), /* United States of America (USA) */
};
#endif

#if (RTW_DEF_MODULE_REGULATORY_CERT & RTW_MODULE_RTL8192EEBT_HMC_M2) /* 2013 certify */
static const struct country_chplan RTL8192EEBT_HMC_M2_country_chplan_exc_map[] = {
	COUNTRY_CHPLAN_ENT("CA", 0x20, 1, 0x3FB), /* Canada */
	COUNTRY_CHPLAN_ENT("MX", 0x34, 1, 0x3F1), /* Mexico */
	COUNTRY_CHPLAN_ENT("TW", 0x39, 1, 0x3FF), /* Taiwan */
	COUNTRY_CHPLAN_ENT("US", 0x34, 1, 0x3FF), /* United States of America (USA) */
};
#endif

#if (RTW_DEF_MODULE_REGULATORY_CERT & RTW_MODULE_RTL8723DE_NGFF1630) /* 2016 certify */
static const struct country_chplan RTL8723DE_NGFF1630_country_chplan_exc_map[] = {
	COUNTRY_CHPLAN_ENT("CA", 0x2A, 1, 0x3FB), /* Canada */
	COUNTRY_CHPLAN_ENT("MX", 0x34, 1, 0x3F1), /* Mexico */
};
#endif

#if (RTW_DEF_MODULE_REGULATORY_CERT & RTW_MODULE_RTL8822BE) /* 2016 certify */
static const struct country_chplan RTL8822BE_country_chplan_exc_map[] = {
	COUNTRY_CHPLAN_ENT("CL", 0x30, 1, 0x3F1), /* Chile */
};
#endif

/**
 * rtw_def_module_get_chplan_from_country -
 * @country_code: string of country code
 * @return:
 * Return NULL for case referring to common map
 */
static const struct country_chplan *rtw_def_module_get_chplan_from_country(const char *country_code)
{
	const struct country_chplan *ent = NULL;
	const struct country_chplan *hal_map = NULL;
	u16 hal_map_sz = 0;
	int i;

	/* TODO: runtime selection for multi driver */
#if (RTW_DEF_MODULE_REGULATORY_CERT == RTW_MODULE_RTL8821AE_HMC_M2)
	hal_map = RTL8821AE_HMC_M2_country_chplan_exc_map;
	hal_map_sz = sizeof(RTL8821AE_HMC_M2_country_chplan_exc_map) / sizeof(struct country_chplan);
#elif (RTW_DEF_MODULE_REGULATORY_CERT == RTW_MODULE_RTL8821AU)
	hal_map = RTL8821AU_country_chplan_exc_map;
	hal_map_sz = sizeof(RTL8821AU_country_chplan_exc_map) / sizeof(struct country_chplan);
#elif (RTW_DEF_MODULE_REGULATORY_CERT == RTW_MODULE_RTL8812AENF_NGFF)
	hal_map = RTL8812AENF_NGFF_country_chplan_exc_map;
	hal_map_sz = sizeof(RTL8812AENF_NGFF_country_chplan_exc_map) / sizeof(struct country_chplan);
#elif (RTW_DEF_MODULE_REGULATORY_CERT == RTW_MODULE_RTL8812AEBT_HMC)
	hal_map = RTL8812AEBT_HMC_country_chplan_exc_map;
	hal_map_sz = sizeof(RTL8812AEBT_HMC_country_chplan_exc_map) / sizeof(struct country_chplan);
#elif (RTW_DEF_MODULE_REGULATORY_CERT == RTW_MODULE_RTL8188EE_HMC_M2)
	hal_map = RTL8188EE_HMC_M2_country_chplan_exc_map;
	hal_map_sz = sizeof(RTL8188EE_HMC_M2_country_chplan_exc_map) / sizeof(struct country_chplan);
#elif (RTW_DEF_MODULE_REGULATORY_CERT == RTW_MODULE_RTL8723BE_HMC_M2)
	hal_map = RTL8723BE_HMC_M2_country_chplan_exc_map;
	hal_map_sz = sizeof(RTL8723BE_HMC_M2_country_chplan_exc_map) / sizeof(struct country_chplan);
#elif (RTW_DEF_MODULE_REGULATORY_CERT == RTW_MODULE_RTL8723BS_NGFF1216)
	hal_map = RTL8723BS_NGFF1216_country_chplan_exc_map;
	hal_map_sz = sizeof(RTL8723BS_NGFF1216_country_chplan_exc_map) / sizeof(struct country_chplan);
#elif (RTW_DEF_MODULE_REGULATORY_CERT == RTW_MODULE_RTL8192EEBT_HMC_M2)
	hal_map = RTL8192EEBT_HMC_M2_country_chplan_exc_map;
	hal_map_sz = sizeof(RTL8192EEBT_HMC_M2_country_chplan_exc_map) / sizeof(struct country_chplan);
#elif (RTW_DEF_MODULE_REGULATORY_CERT == RTW_MODULE_RTL8723DE_NGFF1630)
	hal_map = RTL8723DE_NGFF1630_country_chplan_exc_map;
	hal_map_sz = sizeof(RTL8723DE_NGFF1630_country_chplan_exc_map) / sizeof(struct country_chplan);
#elif (RTW_DEF_MODULE_REGULATORY_CERT == RTW_MODULE_RTL8822BE)
	hal_map = RTL8822BE_country_chplan_exc_map;
	hal_map_sz = sizeof(RTL8822BE_country_chplan_exc_map) / sizeof(struct country_chplan);
#endif

	if (hal_map == NULL || hal_map_sz == 0)
		goto exit;

	for (i = 0; i < hal_map_sz; i++) {
		if (strncmp(country_code, hal_map[i].alpha2, 2) == 0) {
			ent = &hal_map[i];
			break;
		}
	}

exit:
	return ent;
}
#endif /* CONFIG_CUSTOMIZED_COUNTRY_CHPLAN_MAP or RTW_DEF_MODULE_REGULATORY_CERT */

static const struct country_chplan country_chplan_map[] = {
	COUNTRY_CHPLAN_ENT("AD", 0x26, 1, 0x000), /* Andorra */
	COUNTRY_CHPLAN_ENT("AE", 0x26, 1, 0x3FB), /* United Arab Emirates */
	COUNTRY_CHPLAN_ENT("AF", 0x42, 1, 0x000), /* Afghanistan */
	COUNTRY_CHPLAN_ENT("AG", 0x26, 1, 0x000), /* Antigua & Barbuda */
	COUNTRY_CHPLAN_ENT("AI", 0x26, 1, 0x000), /* Anguilla(UK) */
	COUNTRY_CHPLAN_ENT("AL", 0x26, 1, 0x3F1), /* Albania */
	COUNTRY_CHPLAN_ENT("AM", 0x26, 1, 0x2B0), /* Armenia */
	COUNTRY_CHPLAN_ENT("AN", 0x26, 1, 0x3F1), /* Netherlands Antilles */
	COUNTRY_CHPLAN_ENT("AO", 0x47, 1, 0x2E0), /* Angola */
	COUNTRY_CHPLAN_ENT("AQ", 0x26, 1, 0x000), /* Antarctica */
	COUNTRY_CHPLAN_ENT("AR", 0x57, 1, 0x3F3), /* Argentina */
	COUNTRY_CHPLAN_ENT("AS", 0x34, 1, 0x000), /* American Samoa */
	COUNTRY_CHPLAN_ENT("AT", 0x26, 1, 0x3FB), /* Austria */
	COUNTRY_CHPLAN_ENT("AU", 0x45, 1, 0x3FB), /* Australia */
	COUNTRY_CHPLAN_ENT("AW", 0x34, 1, 0x0B0), /* Aruba */
	COUNTRY_CHPLAN_ENT("AZ", 0x26, 1, 0x3F1), /* Azerbaijan */
	COUNTRY_CHPLAN_ENT("BA", 0x26, 1, 0x3F1), /* Bosnia & Herzegovina */
	COUNTRY_CHPLAN_ENT("BB", 0x34, 1, 0x250), /* Barbados */
	COUNTRY_CHPLAN_ENT("BD", 0x26, 1, 0x3F1), /* Bangladesh */
	COUNTRY_CHPLAN_ENT("BE", 0x26, 1, 0x3FB), /* Belgium */
	COUNTRY_CHPLAN_ENT("BF", 0x26, 1, 0x2B0), /* Burkina Faso */
	COUNTRY_CHPLAN_ENT("BG", 0x26, 1, 0x3F1), /* Bulgaria */
	COUNTRY_CHPLAN_ENT("BH", 0x47, 1, 0x3F1), /* Bahrain */
	COUNTRY_CHPLAN_ENT("BI", 0x26, 1, 0x2B0), /* Burundi */
	COUNTRY_CHPLAN_ENT("BJ", 0x26, 1, 0x2B0), /* Benin */
	COUNTRY_CHPLAN_ENT("BN", 0x47, 1, 0x210), /* Brunei */
	COUNTRY_CHPLAN_ENT("BO", 0x73, 1, 0x3F1), /* Bolivia */
	COUNTRY_CHPLAN_ENT("BR", 0x34, 1, 0x3F1), /* Brazil */
	COUNTRY_CHPLAN_ENT("BS", 0x34, 1, 0x220), /* Bahamas */
	COUNTRY_CHPLAN_ENT("BW", 0x26, 1, 0x2F1), /* Botswana */
	COUNTRY_CHPLAN_ENT("BY", 0x26, 1, 0x3F1), /* Belarus */
	COUNTRY_CHPLAN_ENT("BZ", 0x34, 1, 0x000), /* Belize */
	COUNTRY_CHPLAN_ENT("CA", 0x2B, 1, 0x3FB), /* Canada */
	COUNTRY_CHPLAN_ENT("CC", 0x26, 1, 0x000), /* Cocos (Keeling) Islands (Australia) */
	COUNTRY_CHPLAN_ENT("CD", 0x26, 1, 0x2B0), /* Congo, Republic of the */
	COUNTRY_CHPLAN_ENT("CF", 0x26, 1, 0x2B0), /* Central African Republic */
	COUNTRY_CHPLAN_ENT("CG", 0x26, 1, 0x2B0), /* Congo, Democratic Republic of the. Zaire */
	COUNTRY_CHPLAN_ENT("CH", 0x26, 1, 0x3FB), /* Switzerland */
	COUNTRY_CHPLAN_ENT("CI", 0x26, 1, 0x3F1), /* Cote d'Ivoire */
	COUNTRY_CHPLAN_ENT("CK", 0x26, 1, 0x000), /* Cook Islands */
	COUNTRY_CHPLAN_ENT("CL", 0x73, 1, 0x3F1), /* Chile */
	COUNTRY_CHPLAN_ENT("CM", 0x26, 1, 0x2B0), /* Cameroon */
	COUNTRY_CHPLAN_ENT("CN", 0x48, 1, 0x3FB), /* China */
	COUNTRY_CHPLAN_ENT("CO", 0x34, 1, 0x3F1), /* Colombia */
	COUNTRY_CHPLAN_ENT("CR", 0x34, 1, 0x3F1), /* Costa Rica */
	COUNTRY_CHPLAN_ENT("CV", 0x26, 1, 0x2B0), /* Cape Verde */
	COUNTRY_CHPLAN_ENT("CX", 0x45, 1, 0x000), /* Christmas Island (Australia) */
	COUNTRY_CHPLAN_ENT("CY", 0x26, 1, 0x3FB), /* Cyprus */
	COUNTRY_CHPLAN_ENT("CZ", 0x26, 1, 0x3FB), /* Czech Republic */
	COUNTRY_CHPLAN_ENT("DE", 0x26, 1, 0x3FB), /* Germany */
	COUNTRY_CHPLAN_ENT("DJ", 0x26, 1, 0x280), /* Djibouti */
	COUNTRY_CHPLAN_ENT("DK", 0x26, 1, 0x3FB), /* Denmark */
	COUNTRY_CHPLAN_ENT("DM", 0x34, 1, 0x000), /* Dominica */
	COUNTRY_CHPLAN_ENT("DO", 0x34, 1, 0x3F1), /* Dominican Republic */
	COUNTRY_CHPLAN_ENT("DZ", 0x26, 1, 0x3F1), /* Algeria */
	COUNTRY_CHPLAN_ENT("EC", 0x34, 1, 0x3F1), /* Ecuador */
	COUNTRY_CHPLAN_ENT("EE", 0x26, 1, 0x3FB), /* Estonia */
	COUNTRY_CHPLAN_ENT("EG", 0x47, 0, 0x3F1), /* Egypt */
	COUNTRY_CHPLAN_ENT("EH", 0x47, 1, 0x280), /* Western Sahara */
	COUNTRY_CHPLAN_ENT("ER", 0x26, 1, 0x000), /* Eritrea */
	COUNTRY_CHPLAN_ENT("ES", 0x26, 1, 0x3FB), /* Spain, Canary Islands, Ceuta, Melilla */
	COUNTRY_CHPLAN_ENT("ET", 0x26, 1, 0x0B0), /* Ethiopia */
	COUNTRY_CHPLAN_ENT("FI", 0x26, 1, 0x3FB), /* Finland */
	COUNTRY_CHPLAN_ENT("FJ", 0x34, 1, 0x200), /* Fiji */
	COUNTRY_CHPLAN_ENT("FK", 0x26, 1, 0x000), /* Falkland Islands (Islas Malvinas) (UK) */
	COUNTRY_CHPLAN_ENT("FM", 0x34, 1, 0x000), /* Micronesia, Federated States of (USA) */
	COUNTRY_CHPLAN_ENT("FO", 0x26, 1, 0x000), /* Faroe Islands (Denmark) */
	COUNTRY_CHPLAN_ENT("FR", 0x26, 1, 0x3FB), /* France */
	COUNTRY_CHPLAN_ENT("GA", 0x26, 1, 0x2B0), /* Gabon */
	COUNTRY_CHPLAN_ENT("GB", 0x26, 1, 0x3FB), /* Great Britain (United Kingdom; England) */
	COUNTRY_CHPLAN_ENT("GD", 0x34, 1, 0x0B0), /* Grenada */
	COUNTRY_CHPLAN_ENT("GE", 0x26, 1, 0x200), /* Georgia */
	COUNTRY_CHPLAN_ENT("GF", 0x26, 1, 0x080), /* French Guiana */
	COUNTRY_CHPLAN_ENT("GG", 0x26, 1, 0x000), /* Guernsey (UK) */
	COUNTRY_CHPLAN_ENT("GH", 0x26, 1, 0x3F1), /* Ghana */
	COUNTRY_CHPLAN_ENT("GI", 0x26, 1, 0x200), /* Gibraltar (UK) */
	COUNTRY_CHPLAN_ENT("GL", 0x26, 1, 0x200), /* Greenland (Denmark) */
	COUNTRY_CHPLAN_ENT("GM", 0x26, 1, 0x2B0), /* Gambia */
	COUNTRY_CHPLAN_ENT("GN", 0x26, 1, 0x210), /* Guinea */
	COUNTRY_CHPLAN_ENT("GP", 0x26, 1, 0x200), /* Guadeloupe (France) */
	COUNTRY_CHPLAN_ENT("GQ", 0x26, 1, 0x2B0), /* Equatorial Guinea */
	COUNTRY_CHPLAN_ENT("GR", 0x26, 1, 0x3FB), /* Greece */
	COUNTRY_CHPLAN_ENT("GS", 0x26, 1, 0x000), /* South Georgia and the Sandwich Islands (UK) */
	COUNTRY_CHPLAN_ENT("GT", 0x34, 1, 0x3F1), /* Guatemala */
	COUNTRY_CHPLAN_ENT("GU", 0x34, 1, 0x200), /* Guam (USA) */
	COUNTRY_CHPLAN_ENT("GW", 0x26, 1, 0x2B0), /* Guinea-Bissau */
	COUNTRY_CHPLAN_ENT("GY", 0x44, 1, 0x000), /* Guyana */
	COUNTRY_CHPLAN_ENT("HK", 0x26, 1, 0x3FB), /* Hong Kong */
	COUNTRY_CHPLAN_ENT("HM", 0x45, 1, 0x000), /* Heard and McDonald Islands (Australia) */
	COUNTRY_CHPLAN_ENT("HN", 0x32, 1, 0x3F1), /* Honduras */
	COUNTRY_CHPLAN_ENT("HR", 0x26, 1, 0x3F9), /* Croatia */
	COUNTRY_CHPLAN_ENT("HT", 0x34, 1, 0x250), /* Haiti */
	COUNTRY_CHPLAN_ENT("HU", 0x26, 1, 0x3FB), /* Hungary */
	COUNTRY_CHPLAN_ENT("ID", 0x54, 0, 0x3F3), /* Indonesia */
	COUNTRY_CHPLAN_ENT("IE", 0x26, 1, 0x3FB), /* Ireland */
	COUNTRY_CHPLAN_ENT("IL", 0x47, 1, 0x3F1), /* Israel */
	COUNTRY_CHPLAN_ENT("IM", 0x26, 1, 0x000), /* Isle of Man (UK) */
	COUNTRY_CHPLAN_ENT("IN", 0x48, 1, 0x3F1), /* India */
	COUNTRY_CHPLAN_ENT("IQ", 0x26, 1, 0x000), /* Iraq */
	COUNTRY_CHPLAN_ENT("IR", 0x26, 0, 0x000), /* Iran */
	COUNTRY_CHPLAN_ENT("IS", 0x26, 1, 0x3FB), /* Iceland */
	COUNTRY_CHPLAN_ENT("IT", 0x26, 1, 0x3FB), /* Italy */
	COUNTRY_CHPLAN_ENT("JE", 0x26, 1, 0x000), /* Jersey (UK) */
	COUNTRY_CHPLAN_ENT("JM", 0x51, 1, 0x3F1), /* Jamaica */
	COUNTRY_CHPLAN_ENT("JO", 0x49, 1, 0x3FB), /* Jordan */
	COUNTRY_CHPLAN_ENT("JP", 0x27, 1, 0x3FF), /* Japan- Telec */
	COUNTRY_CHPLAN_ENT("KE", 0x47, 1, 0x3F9), /* Kenya */
	COUNTRY_CHPLAN_ENT("KG", 0x26, 1, 0x3F1), /* Kyrgyzstan */
	COUNTRY_CHPLAN_ENT("KH", 0x26, 1, 0x3F1), /* Cambodia */
	COUNTRY_CHPLAN_ENT("KI", 0x26, 1, 0x000), /* Kiribati */
	COUNTRY_CHPLAN_ENT("KN", 0x34, 1, 0x000), /* Saint Kitts and Nevis */
	COUNTRY_CHPLAN_ENT("KR", 0x28, 1, 0x3FB), /* South Korea */
	COUNTRY_CHPLAN_ENT("KW", 0x47, 1, 0x3FB), /* Kuwait */
	COUNTRY_CHPLAN_ENT("KY", 0x34, 1, 0x000), /* Cayman Islands (UK) */
	COUNTRY_CHPLAN_ENT("KZ", 0x26, 1, 0x300), /* Kazakhstan */
	COUNTRY_CHPLAN_ENT("LA", 0x26, 1, 0x000), /* Laos */
	COUNTRY_CHPLAN_ENT("LB", 0x26, 1, 0x3F1), /* Lebanon */
	COUNTRY_CHPLAN_ENT("LC", 0x34, 1, 0x000), /* Saint Lucia */
	COUNTRY_CHPLAN_ENT("LI", 0x26, 1, 0x3FB), /* Liechtenstein */
	COUNTRY_CHPLAN_ENT("LK", 0x26, 1, 0x3F1), /* Sri Lanka */
	COUNTRY_CHPLAN_ENT("LR", 0x26, 1, 0x2B0), /* Liberia */
	COUNTRY_CHPLAN_ENT("LS", 0x26, 1, 0x3F1), /* Lesotho */
	COUNTRY_CHPLAN_ENT("LT", 0x26, 1, 0x3FB), /* Lithuania */
	COUNTRY_CHPLAN_ENT("LU", 0x26, 1, 0x3FB), /* Luxembourg */
	COUNTRY_CHPLAN_ENT("LV", 0x26, 1, 0x3FB), /* Latvia */
	COUNTRY_CHPLAN_ENT("LY", 0x26, 1, 0x000), /* Libya */
	COUNTRY_CHPLAN_ENT("MA", 0x47, 1, 0x3F1), /* Morocco */
	COUNTRY_CHPLAN_ENT("MC", 0x26, 1, 0x3FB), /* Monaco */
	COUNTRY_CHPLAN_ENT("MD", 0x26, 1, 0x3F1), /* Moldova */
	COUNTRY_CHPLAN_ENT("ME", 0x26, 1, 0x3F1), /* Montenegro */
	COUNTRY_CHPLAN_ENT("MF", 0x34, 1, 0x000), /* Saint Martin */
	COUNTRY_CHPLAN_ENT("MG", 0x26, 1, 0x220), /* Madagascar */
	COUNTRY_CHPLAN_ENT("MH", 0x34, 1, 0x000), /* Marshall Islands (USA) */
	COUNTRY_CHPLAN_ENT("MK", 0x26, 1, 0x3F1), /* Republic of Macedonia (FYROM) */
	COUNTRY_CHPLAN_ENT("ML", 0x26, 1, 0x2B0), /* Mali */
	COUNTRY_CHPLAN_ENT("MM", 0x26, 1, 0x000), /* Burma (Myanmar) */
	COUNTRY_CHPLAN_ENT("MN", 0x26, 1, 0x000), /* Mongolia */
	COUNTRY_CHPLAN_ENT("MO", 0x26, 1, 0x200), /* Macau */
	COUNTRY_CHPLAN_ENT("MP", 0x34, 1, 0x000), /* Northern Mariana Islands (USA) */
	COUNTRY_CHPLAN_ENT("MQ", 0x26, 1, 0x240), /* Martinique (France) */
	COUNTRY_CHPLAN_ENT("MR", 0x26, 1, 0x2A0), /* Mauritania */
	COUNTRY_CHPLAN_ENT("MS", 0x26, 1, 0x000), /* Montserrat (UK) */
	COUNTRY_CHPLAN_ENT("MT", 0x26, 1, 0x3FB), /* Malta */
	COUNTRY_CHPLAN_ENT("MU", 0x26, 1, 0x2B0), /* Mauritius */
	COUNTRY_CHPLAN_ENT("MV", 0x47, 1, 0x000), /* Maldives */
	COUNTRY_CHPLAN_ENT("MW", 0x26, 1, 0x2B0), /* Malawi */
	COUNTRY_CHPLAN_ENT("MX", 0x61, 1, 0x3F1), /* Mexico */
	COUNTRY_CHPLAN_ENT("MY", 0x63, 1, 0x3F1), /* Malaysia */
	COUNTRY_CHPLAN_ENT("MZ", 0x26, 1, 0x3F1), /* Mozambique */
	COUNTRY_CHPLAN_ENT("NA", 0x26, 1, 0x300), /* Namibia */
	COUNTRY_CHPLAN_ENT("NC", 0x26, 1, 0x000), /* New Caledonia */
	COUNTRY_CHPLAN_ENT("NE", 0x26, 1, 0x2B0), /* Niger */
	COUNTRY_CHPLAN_ENT("NF", 0x45, 1, 0x000), /* Norfolk Island (Australia) */
	COUNTRY_CHPLAN_ENT("NG", 0x75, 1, 0x3F9), /* Nigeria */
	COUNTRY_CHPLAN_ENT("NI", 0x34, 1, 0x3F1), /* Nicaragua */
	COUNTRY_CHPLAN_ENT("NL", 0x26, 1, 0x3FB), /* Netherlands */
	COUNTRY_CHPLAN_ENT("NO", 0x26, 1, 0x3FB), /* Norway */
	COUNTRY_CHPLAN_ENT("NP", 0x47, 1, 0x2F0), /* Nepal */
	COUNTRY_CHPLAN_ENT("NR", 0x26, 1, 0x000), /* Nauru */
	COUNTRY_CHPLAN_ENT("NU", 0x45, 1, 0x000), /* Niue */
	COUNTRY_CHPLAN_ENT("NZ", 0x45, 1, 0x3FB), /* New Zealand */
	COUNTRY_CHPLAN_ENT("OM", 0x26, 1, 0x3F9), /* Oman */
	COUNTRY_CHPLAN_ENT("PA", 0x34, 1, 0x3F1), /* Panama */
	COUNTRY_CHPLAN_ENT("PE", 0x34, 1, 0x3F1), /* Peru */
	COUNTRY_CHPLAN_ENT("PF", 0x26, 1, 0x000), /* French Polynesia (France) */
	COUNTRY_CHPLAN_ENT("PG", 0x26, 1, 0x3F1), /* Papua New Guinea */
	COUNTRY_CHPLAN_ENT("PH", 0x26, 1, 0x3F1), /* Philippines */
	COUNTRY_CHPLAN_ENT("PK", 0x51, 1, 0x3F1), /* Pakistan */
	COUNTRY_CHPLAN_ENT("PL", 0x26, 1, 0x3FB), /* Poland */
	COUNTRY_CHPLAN_ENT("PM", 0x26, 1, 0x000), /* Saint Pierre and Miquelon (France) */
	COUNTRY_CHPLAN_ENT("PR", 0x34, 1, 0x3F1), /* Puerto Rico */
	COUNTRY_CHPLAN_ENT("PT", 0x26, 1, 0x3FB), /* Portugal */
	COUNTRY_CHPLAN_ENT("PW", 0x34, 1, 0x000), /* Palau */
	COUNTRY_CHPLAN_ENT("PY", 0x34, 1, 0x3F1), /* Paraguay */
	COUNTRY_CHPLAN_ENT("QA", 0x51, 1, 0x3F9), /* Qatar */
	COUNTRY_CHPLAN_ENT("RE", 0x26, 1, 0x000), /* Reunion (France) */
	COUNTRY_CHPLAN_ENT("RO", 0x26, 1, 0x3F1), /* Romania */
	COUNTRY_CHPLAN_ENT("RS", 0x26, 1, 0x3F1), /* Serbia, Kosovo */
	COUNTRY_CHPLAN_ENT("RU", 0x59, 1, 0x3FB), /* Russia(fac/gost), Kaliningrad */
	COUNTRY_CHPLAN_ENT("RW", 0x26, 1, 0x2B0), /* Rwanda */
	COUNTRY_CHPLAN_ENT("SA", 0x26, 1, 0x3FB), /* Saudi Arabia */
	COUNTRY_CHPLAN_ENT("SB", 0x26, 1, 0x000), /* Solomon Islands */
	COUNTRY_CHPLAN_ENT("SC", 0x34, 1, 0x290), /* Seychelles */
	COUNTRY_CHPLAN_ENT("SE", 0x26, 1, 0x3FB), /* Sweden */
	COUNTRY_CHPLAN_ENT("SG", 0x26, 1, 0x3FB), /* Singapore */
	COUNTRY_CHPLAN_ENT("SH", 0x26, 1, 0x000), /* Saint Helena (UK) */
	COUNTRY_CHPLAN_ENT("SI", 0x26, 1, 0x3FB), /* Slovenia */
	COUNTRY_CHPLAN_ENT("SJ", 0x26, 1, 0x000), /* Svalbard (Norway) */
	COUNTRY_CHPLAN_ENT("SK", 0x26, 1, 0x3FB), /* Slovakia */
	COUNTRY_CHPLAN_ENT("SL", 0x26, 1, 0x2B0), /* Sierra Leone */
	COUNTRY_CHPLAN_ENT("SM", 0x26, 1, 0x000), /* San Marino */
	COUNTRY_CHPLAN_ENT("SN", 0x26, 1, 0x3F1), /* Senegal */
	COUNTRY_CHPLAN_ENT("SO", 0x26, 1, 0x000), /* Somalia */
	COUNTRY_CHPLAN_ENT("SR", 0x74, 1, 0x000), /* Suriname */
	COUNTRY_CHPLAN_ENT("ST", 0x34, 1, 0x280), /* Sao Tome and Principe */
	COUNTRY_CHPLAN_ENT("SV", 0x30, 1, 0x3F1), /* El Salvador */
	COUNTRY_CHPLAN_ENT("SX", 0x34, 1, 0x000), /* Sint Marteen */
	COUNTRY_CHPLAN_ENT("SZ", 0x26, 1, 0x020), /* Swaziland */
	COUNTRY_CHPLAN_ENT("TC", 0x26, 1, 0x000), /* Turks and Caicos Islands (UK) */
	COUNTRY_CHPLAN_ENT("TD", 0x26, 1, 0x2B0), /* Chad */
	COUNTRY_CHPLAN_ENT("TF", 0x26, 1, 0x280), /* French Southern and Antarctic Lands (FR Southern Territories) */
	COUNTRY_CHPLAN_ENT("TG", 0x26, 1, 0x2B0), /* Togo */
	COUNTRY_CHPLAN_ENT("TH", 0x26, 1, 0x3F1), /* Thailand */
	COUNTRY_CHPLAN_ENT("TJ", 0x26, 1, 0x240), /* Tajikistan */
	COUNTRY_CHPLAN_ENT("TK", 0x45, 1, 0x000), /* Tokelau */
	COUNTRY_CHPLAN_ENT("TM", 0x26, 1, 0x000), /* Turkmenistan */
	COUNTRY_CHPLAN_ENT("TN", 0x47, 1, 0x3F1), /* Tunisia */
	COUNTRY_CHPLAN_ENT("TO", 0x26, 1, 0x000), /* Tonga */
	COUNTRY_CHPLAN_ENT("TR", 0x26, 1, 0x3F1), /* Turkey, Northern Cyprus */
	COUNTRY_CHPLAN_ENT("TT", 0x42, 1, 0x3F1), /* Trinidad & Tobago */
	COUNTRY_CHPLAN_ENT("TW", 0x76, 1, 0x3FF), /* Taiwan */
	COUNTRY_CHPLAN_ENT("TZ", 0x26, 1, 0x2F0), /* Tanzania */
	COUNTRY_CHPLAN_ENT("UA", 0x36, 1, 0x3FB), /* Ukraine */
	COUNTRY_CHPLAN_ENT("UG", 0x26, 1, 0x2F1), /* Uganda */
	COUNTRY_CHPLAN_ENT("US", 0x76, 1, 0x3FF), /* United States of America (USA) */
	COUNTRY_CHPLAN_ENT("UY", 0x30, 1, 0x3F1), /* Uruguay */
	COUNTRY_CHPLAN_ENT("UZ", 0x47, 1, 0x2F0), /* Uzbekistan */
	COUNTRY_CHPLAN_ENT("VA", 0x26, 1, 0x000), /* Holy See (Vatican City) */
	COUNTRY_CHPLAN_ENT("VC", 0x34, 1, 0x010), /* Saint Vincent and the Grenadines */
	COUNTRY_CHPLAN_ENT("VE", 0x30, 1, 0x3F1), /* Venezuela */
	COUNTRY_CHPLAN_ENT("VI", 0x34, 1, 0x000), /* United States Virgin Islands (USA) */
	COUNTRY_CHPLAN_ENT("VN", 0x26, 1, 0x3F1), /* Vietnam */
	COUNTRY_CHPLAN_ENT("VU", 0x26, 1, 0x000), /* Vanuatu */
	COUNTRY_CHPLAN_ENT("WF", 0x26, 1, 0x000), /* Wallis and Futuna (France) */
	COUNTRY_CHPLAN_ENT("WS", 0x34, 1, 0x000), /* Samoa */
	COUNTRY_CHPLAN_ENT("YE", 0x26, 1, 0x040), /* Yemen */
	COUNTRY_CHPLAN_ENT("YT", 0x26, 1, 0x280), /* Mayotte (France) */
	COUNTRY_CHPLAN_ENT("ZA", 0x26, 1, 0x3F1), /* South Africa */
	COUNTRY_CHPLAN_ENT("ZM", 0x26, 1, 0x2B0), /* Zambia */
	COUNTRY_CHPLAN_ENT("ZW", 0x26, 1, 0x3F1), /* Zimbabwe */
};

/*
* rtw_get_chplan_from_country -
* @country_code: string of country code
*
* Return pointer of struct country_chplan entry or NULL when unsupported country_code is given
*/
const struct country_chplan *rtw_get_chplan_from_country(const char *country_code)
{
	const struct country_chplan *ent = NULL;
	const struct country_chplan *map = NULL;
	u16 map_sz = 0;
	char code[2];
	int i;

	code[0] = alpha_to_upper(country_code[0]);
	code[1] = alpha_to_upper(country_code[1]);

#if !defined(CONFIG_CUSTOMIZED_COUNTRY_CHPLAN_MAP) && RTW_DEF_MODULE_REGULATORY_CERT
	ent = rtw_def_module_get_chplan_from_country(code);
	if (ent != NULL)
		goto exit;
#endif

#ifdef CONFIG_CUSTOMIZED_COUNTRY_CHPLAN_MAP
	map = CUSTOMIZED_country_chplan_map;
	map_sz = sizeof(CUSTOMIZED_country_chplan_map) / sizeof(struct country_chplan);
#else
	map = country_chplan_map;
	map_sz = sizeof(country_chplan_map) / sizeof(struct country_chplan);
#endif

	for (i = 0; i < map_sz; i++) {
		if (strncmp(code, map[i].alpha2, 2) == 0) {
			ent = &map[i];
			break;
		}
	}

exit:
	#if RTW_DEF_MODULE_REGULATORY_CERT
	if (ent && !(COUNTRY_CHPLAN_DEF_MODULE_FALGS(ent) & RTW_DEF_MODULE_REGULATORY_CERT))
		ent = NULL;
	#endif

	return ent;
}

const char *const _regd_str[] = {
	"NONE",
	"FCC",
	"MKK",
	"ETSI",
	"IC",
	"KCC",
	"WW",
};

#ifdef CONFIG_TXPWR_LIMIT
void _dump_regd_exc_list(void *sel, struct rf_ctl_t *rfctl)
{
	struct regd_exc_ent *ent;
	_list *cur, *head;

	RTW_PRINT_SEL(sel, "regd_exc_num:%u\n", rfctl->regd_exc_num);

	if (!rfctl->regd_exc_num)
		goto exit;

	RTW_PRINT_SEL(sel, "%-7s %-6s %-9s\n", "country", "domain", "regd_name");

	head = &rfctl->reg_exc_list;
	cur = get_next(head);

	while ((rtw_end_of_queue_search(head, cur)) == _FALSE) {
		u8 has_country;

		ent = LIST_CONTAINOR(cur, struct regd_exc_ent, list);
		cur = get_next(cur);
		has_country = (ent->country[0] == '\0' && ent->country[1] == '\0') ? 0 : 1;

		RTW_PRINT_SEL(sel, "     %c%c   0x%02x %s\n"
			, has_country ? ent->country[0] : '0'
			, has_country ? ent->country[1] : '0'
			, ent->domain
			, ent->regd_name
		);
	}

exit:
	return;
}

inline void dump_regd_exc_list(void *sel, struct rf_ctl_t *rfctl)
{
	_irqL irqL;

	_enter_critical_mutex(&rfctl->txpwr_lmt_mutex, &irqL);
	_dump_regd_exc_list(sel, rfctl);
	_exit_critical_mutex(&rfctl->txpwr_lmt_mutex, &irqL);
}

void rtw_regd_exc_add_with_nlen(struct rf_ctl_t *rfctl, const char *country, u8 domain, const char *regd_name, u32 nlen)
{
	struct regd_exc_ent *ent;
	_irqL irqL;

	if (!regd_name || !nlen) {
		rtw_warn_on(1);
		goto exit;
	}

	ent = (struct regd_exc_ent *)rtw_zmalloc(sizeof(struct regd_exc_ent) + nlen + 1);
	if (!ent)
		goto exit;

	_rtw_init_listhead(&ent->list);
	if (country)
		_rtw_memcpy(ent->country, country, 2);
	ent->domain = domain;
	_rtw_memcpy(ent->regd_name, regd_name, nlen);

	_enter_critical_mutex(&rfctl->txpwr_lmt_mutex, &irqL);

	rtw_list_insert_tail(&ent->list, &rfctl->reg_exc_list);
	rfctl->regd_exc_num++;

	_exit_critical_mutex(&rfctl->txpwr_lmt_mutex, &irqL);

exit:
	return;
}

inline void rtw_regd_exc_add(struct rf_ctl_t *rfctl, const char *country, u8 domain, const char *regd_name)
{
	rtw_regd_exc_add_with_nlen(rfctl, country, domain, regd_name, strlen(regd_name));
}

struct regd_exc_ent *_rtw_regd_exc_search(struct rf_ctl_t *rfctl, const char *country, u8 domain)
{
	struct regd_exc_ent *ent;
	_list *cur, *head;
	u8 match = 0;

	head = &rfctl->reg_exc_list;
	cur = get_next(head);

	while ((rtw_end_of_queue_search(head, cur)) == _FALSE) {
		u8 has_country;

		ent = LIST_CONTAINOR(cur, struct regd_exc_ent, list);
		cur = get_next(cur);
		has_country = (ent->country[0] == '\0' && ent->country[1] == '\0') ? 0 : 1;

		/* entry has country condition to match */
		if (has_country) {
			if (!country)
				continue;
			if (ent->country[0] != country[0]
				|| ent->country[1] != country[1])
				continue;
		}

		/* entry has domain condition to match */
		if (ent->domain != 0xFF) {
			if (domain == 0xFF)
				continue;
			if (ent->domain != domain)
				continue;
		}

		match = 1;
		break;
	}

exit:
	if (match)
		return ent;
	else
		return NULL;
}

inline struct regd_exc_ent *rtw_regd_exc_search(struct rf_ctl_t *rfctl, const char *country, u8 domain)
{
	struct regd_exc_ent *ent;
	_irqL irqL;

	_enter_critical_mutex(&rfctl->txpwr_lmt_mutex, &irqL);
	ent = _rtw_regd_exc_search(rfctl, country, domain);
	_exit_critical_mutex(&rfctl->txpwr_lmt_mutex, &irqL);

	return ent;
}

void rtw_regd_exc_list_free(struct rf_ctl_t *rfctl)
{
	struct regd_exc_ent *ent;
	_irqL irqL;
	_list *cur, *head;

	_enter_critical_mutex(&rfctl->txpwr_lmt_mutex, &irqL);

	head = &rfctl->reg_exc_list;
	cur = get_next(head);

	while ((rtw_end_of_queue_search(head, cur)) == _FALSE) {
		ent = LIST_CONTAINOR(cur, struct regd_exc_ent, list);
		cur = get_next(cur);
		rtw_list_delete(&ent->list);
		rtw_mfree((u8 *)ent, sizeof(struct regd_exc_ent) + strlen(ent->regd_name) + 1);
	}
	rfctl->regd_exc_num = 0;

	_exit_critical_mutex(&rfctl->txpwr_lmt_mutex, &irqL);
}

void dump_txpwr_lmt(void *sel, _adapter *adapter)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	_irqL irqL;
	char fmt[16];
	s8 *lmt_idx = NULL;
	int bw, band, ch_num, tlrs, ntx_idx, rs, i, path;
	u8 ch, n, rfpath_num;

	_enter_critical_mutex(&rfctl->txpwr_lmt_mutex, &irqL);

	_dump_regd_exc_list(sel, rfctl);
	RTW_PRINT_SEL(sel, "\n");

	if (!rfctl->txpwr_regd_num)
		goto release_lock;

	lmt_idx = rtw_malloc(sizeof(s8) * RF_PATH_MAX * rfctl->txpwr_regd_num);
	if (!lmt_idx) {
		RTW_ERR("%s alloc fail\n", __func__);
		goto release_lock;
	}

	RTW_PRINT_SEL(sel, "txpwr_lmt_2g_cck_ofdm_state:0x%02x\n", rfctl->txpwr_lmt_2g_cck_ofdm_state);
	#ifdef CONFIG_IEEE80211_BAND_5GHZ
	if (IS_HARDWARE_TYPE_JAGUAR_AND_JAGUAR2(adapter))
		RTW_PRINT_SEL(sel, "txpwr_lmt_5g_cck_ofdm_state:0x%02x\n", rfctl->txpwr_lmt_5g_cck_ofdm_state);
		RTW_PRINT_SEL(sel, "txpwr_lmt_5g_20_40_ref:0x%02x\n", rfctl->txpwr_lmt_5g_20_40_ref);
	#endif
	RTW_PRINT_SEL(sel, "\n");

	for (band = BAND_ON_2_4G; band <= BAND_ON_5G; band++) {
		if (!hal_is_band_support(adapter, band))
			continue;

		rfpath_num = (band == BAND_ON_2_4G ? hal_spec->rfpath_num_2g : hal_spec->rfpath_num_5g);

		for (bw = 0; bw < MAX_5G_BANDWIDTH_NUM; bw++) {

			if (bw >= CHANNEL_WIDTH_160)
				break;
			if (band == BAND_ON_2_4G && bw >= CHANNEL_WIDTH_80)
				break;

			if (band == BAND_ON_2_4G)
				ch_num = CENTER_CH_2G_NUM;
			else
				ch_num = center_chs_5g_num(bw);

			if (ch_num == 0) {
				rtw_warn_on(1);
				break;
			}

			for (tlrs = TXPWR_LMT_RS_CCK; tlrs < TXPWR_LMT_RS_NUM; tlrs++) {

				if (band == BAND_ON_2_4G && tlrs == TXPWR_LMT_RS_VHT)
					continue;
				if (band == BAND_ON_5G && tlrs == TXPWR_LMT_RS_CCK)
					continue;
				if (bw > CHANNEL_WIDTH_20 && (tlrs == TXPWR_LMT_RS_CCK || tlrs == TXPWR_LMT_RS_OFDM))
					continue;
				if (bw > CHANNEL_WIDTH_40 && tlrs == TXPWR_LMT_RS_HT)
					continue;
				if (tlrs == TXPWR_LMT_RS_VHT && !IS_HARDWARE_TYPE_JAGUAR_AND_JAGUAR2(adapter))
					continue;

				for (ntx_idx = RF_1TX; ntx_idx < MAX_TX_COUNT; ntx_idx++) {
					struct txpwr_lmt_ent *ent;
					_list *cur, *head;

					if (ntx_idx >= hal_spec->tx_nss_num)
						continue;

					/* bypass CCK multi-TX is not defined */
					if (tlrs == TXPWR_LMT_RS_CCK && ntx_idx > RF_1TX) {
						if (band == BAND_ON_2_4G
							&& !(rfctl->txpwr_lmt_2g_cck_ofdm_state & (TXPWR_LMT_HAS_CCK_1T << ntx_idx)))
							continue;
					}

					/* bypass OFDM multi-TX is not defined */
					if (tlrs == TXPWR_LMT_RS_OFDM && ntx_idx > RF_1TX) {
						if (band == BAND_ON_2_4G
							&& !(rfctl->txpwr_lmt_2g_cck_ofdm_state & (TXPWR_LMT_HAS_OFDM_1T << ntx_idx)))
							continue;
						#ifdef CONFIG_IEEE80211_BAND_5GHZ
						if (band == BAND_ON_5G
							&& !(rfctl->txpwr_lmt_5g_cck_ofdm_state & (TXPWR_LMT_HAS_OFDM_1T << ntx_idx)))
							continue;
						#endif
					}

					/* bypass 5G 20M, 40M pure reference */
					#ifdef CONFIG_IEEE80211_BAND_5GHZ
					if (band == BAND_ON_5G && (bw == CHANNEL_WIDTH_20 || bw == CHANNEL_WIDTH_40)) {
						if (rfctl->txpwr_lmt_5g_20_40_ref == TXPWR_LMT_REF_HT_FROM_VHT) {
							if (tlrs == TXPWR_LMT_RS_HT)
								continue;
						} else if (rfctl->txpwr_lmt_5g_20_40_ref == TXPWR_LMT_REF_VHT_FROM_HT) {
							if (tlrs == TXPWR_LMT_RS_VHT && bw <= CHANNEL_WIDTH_40)
								continue;
						}
					}
					#endif

					/* choose n-SS mapping rate section to get lmt diff value */
					if (tlrs == TXPWR_LMT_RS_CCK)
						rs = CCK;
					else if (tlrs == TXPWR_LMT_RS_OFDM)
						rs = OFDM;
					else if (tlrs == TXPWR_LMT_RS_HT)
						rs = HT_1SS + ntx_idx;
					else if (tlrs == TXPWR_LMT_RS_VHT)
						rs = VHT_1SS + ntx_idx;
					else {
						RTW_ERR("%s invalid tlrs %u\n", __func__, tlrs);
						continue;
					}

					RTW_PRINT_SEL(sel, "[%s][%s][%s][%uT]\n"
						, band_str(band)
						, ch_width_str(bw)
						, txpwr_lmt_rs_str(tlrs)
						, ntx_idx + 1
					);

					/* header for limit in db */
					RTW_PRINT_SEL(sel, "%3s ", "ch");

					head = &rfctl->txpwr_lmt_list;
					cur = get_next(head);
					while ((rtw_end_of_queue_search(head, cur)) == _FALSE) {
						ent = LIST_CONTAINOR(cur, struct txpwr_lmt_ent, list);
						cur = get_next(cur);

						sprintf(fmt, "%%%zus%%s ", strlen(ent->regd_name) < 4 ? 5 - strlen(ent->regd_name) : 1);
						_RTW_PRINT_SEL(sel, fmt
							, strcmp(ent->regd_name, rfctl->regd_name) == 0 ? "*" : ""
							, ent->regd_name
						);
					}
					sprintf(fmt, "%%%zus%%s ", strlen(regd_str(TXPWR_LMT_WW)) < 4 ? 5 - strlen(regd_str(TXPWR_LMT_WW)) : 1);
					_RTW_PRINT_SEL(sel, fmt
						, strcmp(rfctl->regd_name, regd_str(TXPWR_LMT_WW)) == 0 ? "*" : ""
						, regd_str(TXPWR_LMT_WW)
					);

					/* header for limit offset */
					for (path = 0; path < RF_PATH_MAX; path++) {
						if (path >= rfpath_num)
							break;
						_RTW_PRINT_SEL(sel, "|");
						head = &rfctl->txpwr_lmt_list;
						cur = get_next(head);
						while ((rtw_end_of_queue_search(head, cur)) == _FALSE) {
							ent = LIST_CONTAINOR(cur, struct txpwr_lmt_ent, list);
							cur = get_next(cur);
							_RTW_PRINT_SEL(sel, "%3c "
								, strcmp(ent->regd_name, rfctl->regd_name) == 0 ? rf_path_char(path) : ' ');
						}
						_RTW_PRINT_SEL(sel, "%3c "
								, strcmp(rfctl->regd_name, regd_str(TXPWR_LMT_WW)) == 0 ? rf_path_char(path) : ' ');
					}
					_RTW_PRINT_SEL(sel, "\n");

					for (n = 0; n < ch_num; n++) {
						s8 lmt;
						s8 lmt_offset;
						u8 base;

						if (band == BAND_ON_2_4G)
							ch = n + 1;
						else
							ch = center_chs_5g(bw, n);

						if (ch == 0) {
							rtw_warn_on(1);
							break;
						}

						/* dump limit in db */
						RTW_PRINT_SEL(sel, "%3u ", ch);
						head = &rfctl->txpwr_lmt_list;
						cur = get_next(head);
						while ((rtw_end_of_queue_search(head, cur)) == _FALSE) {
							ent = LIST_CONTAINOR(cur, struct txpwr_lmt_ent, list);
							cur = get_next(cur);
							lmt = phy_get_txpwr_lmt_abs(adapter, ent->regd_name, band, bw, tlrs, ntx_idx, ch, 0);
							if (lmt == MAX_POWER_INDEX) {
								sprintf(fmt, "%%%zus ", strlen(ent->regd_name) >= 5 ? strlen(ent->regd_name) + 1 : 5);
								_RTW_PRINT_SEL(sel, fmt, "NA");
							} else {
								if (lmt % 2) {
									sprintf(fmt, "%%%zud.5 ", strlen(ent->regd_name) >= 5 ? strlen(ent->regd_name) - 1 : 3);
									_RTW_PRINT_SEL(sel, fmt, lmt / 2);
								} else {
									sprintf(fmt, "%%%zud ", strlen(ent->regd_name) >= 5 ? strlen(ent->regd_name) + 1 : 5);
									_RTW_PRINT_SEL(sel, fmt, lmt / 2);
								}
							}
						}
						lmt = phy_get_txpwr_lmt_abs(adapter, regd_str(TXPWR_LMT_WW), band, bw, tlrs, ntx_idx, ch, 0);
						if (lmt == MAX_POWER_INDEX) {
							sprintf(fmt, "%%%zus ", strlen(regd_str(TXPWR_LMT_WW)) >= 5 ? strlen(regd_str(TXPWR_LMT_WW)) + 1 : 5);
							_RTW_PRINT_SEL(sel, fmt, "NA");
						} else {
							if (lmt % 2) {
								sprintf(fmt, "%%%zud.5 ", strlen(regd_str(TXPWR_LMT_WW)) >= 5 ? strlen(regd_str(TXPWR_LMT_WW)) - 1 : 3);
								_RTW_PRINT_SEL(sel, fmt, lmt / 2);
							} else {
								sprintf(fmt, "%%%zud ", strlen(regd_str(TXPWR_LMT_WW)) >= 5 ? strlen(regd_str(TXPWR_LMT_WW)) + 1 : 5);
								_RTW_PRINT_SEL(sel, fmt, lmt / 2);
							}
						}

						/* dump limit offset of each path */
						for (path = RF_PATH_A; path < RF_PATH_MAX; path++) {
							if (path >= rfpath_num)
								break;

							base = PHY_GetTxPowerByRateBase(adapter, band, path, rs);

							_RTW_PRINT_SEL(sel, "|");
							head = &rfctl->txpwr_lmt_list;
							cur = get_next(head);
							i = 0;
							while ((rtw_end_of_queue_search(head, cur)) == _FALSE) {
								ent = LIST_CONTAINOR(cur, struct txpwr_lmt_ent, list);
								cur = get_next(cur);
								lmt_offset = phy_get_txpwr_lmt(adapter, ent->regd_name, band, bw, path, rs, ntx_idx, ch, 0);
								if (lmt_offset == MAX_POWER_INDEX) {
									*(lmt_idx + i * RF_PATH_MAX + path) = MAX_POWER_INDEX;
									_RTW_PRINT_SEL(sel, "%3s ", "NA");
								} else {
									*(lmt_idx + i * RF_PATH_MAX + path) = lmt_offset + base;
									_RTW_PRINT_SEL(sel, "%3d ", lmt_offset);
								}
								i++;
							}
							lmt_offset = phy_get_txpwr_lmt(adapter, regd_str(TXPWR_LMT_WW), band, bw, path, rs, ntx_idx, ch, 0);
							if (lmt_offset == MAX_POWER_INDEX)
								_RTW_PRINT_SEL(sel, "%3s ", "NA");
							else
								_RTW_PRINT_SEL(sel, "%3d ", lmt_offset);

						}

						/* compare limit_idx of each path, print 'x' when mismatch */
						if (rfpath_num > 1) {
							for (i = 0; i < rfctl->txpwr_regd_num; i++) {
								for (path = 0; path < RF_PATH_MAX; path++) {
									if (path >= rfpath_num)
										break;
									if (*(lmt_idx + i * RF_PATH_MAX + path) != *(lmt_idx + i * RF_PATH_MAX + ((path + 1) % rfpath_num)))
										break;
								}
								if (path >= rfpath_num)
									_RTW_PRINT_SEL(sel, " ");
								else
									_RTW_PRINT_SEL(sel, "x");
							}
						}
						_RTW_PRINT_SEL(sel, "\n");

					}
					RTW_PRINT_SEL(sel, "\n");
				}
			} /* loop for rate sections */
		} /* loop for bandwidths */
	} /* loop for bands */

	if (lmt_idx)
		rtw_mfree(lmt_idx, sizeof(s8) * RF_PATH_MAX * rfctl->txpwr_regd_num);

release_lock:
	_exit_critical_mutex(&rfctl->txpwr_lmt_mutex, &irqL);
}

/* search matcing first, if not found, alloc one */
void rtw_txpwr_lmt_add_with_nlen(struct rf_ctl_t *rfctl, const char *regd_name, u32 nlen
	, u8 band, u8 bw, u8 tlrs, u8 ntx_idx, u8 ch_idx, s8 lmt)
{
	struct txpwr_lmt_ent *ent;
	_irqL irqL;
	_list *cur, *head;
	s8 pre_lmt;

	if (!regd_name || !nlen) {
		rtw_warn_on(1);
		goto exit;
	}

	_enter_critical_mutex(&rfctl->txpwr_lmt_mutex, &irqL);

	/* search for existed entry */
	head = &rfctl->txpwr_lmt_list;
	cur = get_next(head);
	while ((rtw_end_of_queue_search(head, cur)) == _FALSE) {
		ent = LIST_CONTAINOR(cur, struct txpwr_lmt_ent, list);
		cur = get_next(cur);

		if (strlen(ent->regd_name) == nlen
			&& _rtw_memcmp(ent->regd_name, regd_name, nlen) == _TRUE)
			goto chk_lmt_val;
	}

	/* alloc new one */
	ent = (struct txpwr_lmt_ent *)rtw_zvmalloc(sizeof(struct txpwr_lmt_ent) + nlen + 1);
	if (!ent)
		goto release_lock;

	_rtw_init_listhead(&ent->list);
	_rtw_memcpy(ent->regd_name, regd_name, nlen);
	{
		u8 j, k, l, m;

		for (j = 0; j < MAX_2_4G_BANDWIDTH_NUM; ++j)
			for (k = 0; k < TXPWR_LMT_RS_NUM_2G; ++k)
				for (m = 0; m < CENTER_CH_2G_NUM; ++m)
					for (l = 0; l < MAX_TX_COUNT; ++l)
						ent->lmt_2g[j][k][m][l] = MAX_POWER_INDEX;
		#ifdef CONFIG_IEEE80211_BAND_5GHZ
		for (j = 0; j < MAX_5G_BANDWIDTH_NUM; ++j)
			for (k = 0; k < TXPWR_LMT_RS_NUM_5G; ++k)
				for (m = 0; m < CENTER_CH_5G_ALL_NUM; ++m)
					for (l = 0; l < MAX_TX_COUNT; ++l)
						ent->lmt_5g[j][k][m][l] = MAX_POWER_INDEX;
		#endif
	}

	rtw_list_insert_tail(&ent->list, &rfctl->txpwr_lmt_list);
	rfctl->txpwr_regd_num++;

chk_lmt_val:
	if (band == BAND_ON_2_4G)
		pre_lmt = ent->lmt_2g[bw][tlrs][ch_idx][ntx_idx];
	#ifdef CONFIG_IEEE80211_BAND_5GHZ
	else if (band == BAND_ON_5G)
		pre_lmt = ent->lmt_5g[bw][tlrs - 1][ch_idx][ntx_idx];
	#endif
	else
		goto release_lock;

	if (pre_lmt != MAX_POWER_INDEX)
		RTW_PRINT("duplicate txpwr_lmt for [%s][%s][%s][%s][%uT][%d]\n"
			, regd_name, band_str(band), ch_width_str(bw), txpwr_lmt_rs_str(tlrs), ntx_idx + 1
			, band == BAND_ON_2_4G ? ch_idx + 1 : center_ch_5g_all[ch_idx]);

	lmt = rtw_min(pre_lmt, lmt);
	if (band == BAND_ON_2_4G)
		ent->lmt_2g[bw][tlrs][ch_idx][ntx_idx] = lmt;
	#ifdef CONFIG_IEEE80211_BAND_5GHZ
	else if (band == BAND_ON_5G)
		ent->lmt_5g[bw][tlrs - 1][ch_idx][ntx_idx] = lmt;
	#endif

	if (0)
		RTW_PRINT("%s, %4s, %6s, %7s, %uT, ch%3d = %d\n"
			, regd_name, band_str(band), ch_width_str(bw), txpwr_lmt_rs_str(tlrs), ntx_idx + 1
			, band == BAND_ON_2_4G ? ch_idx + 1 : center_ch_5g_all[ch_idx]
			, lmt);

release_lock:
	_exit_critical_mutex(&rfctl->txpwr_lmt_mutex, &irqL);

exit:
	return;
}

inline void rtw_txpwr_lmt_add(struct rf_ctl_t *rfctl, const char *regd_name
	, u8 band, u8 bw, u8 tlrs, u8 ntx_idx, u8 ch_idx, s8 lmt)
{
	rtw_txpwr_lmt_add_with_nlen(rfctl, regd_name, strlen(regd_name)
		, band, bw, tlrs, ntx_idx, ch_idx, lmt);
}

struct txpwr_lmt_ent *_rtw_txpwr_lmt_get_by_name(struct rf_ctl_t *rfctl, const char *regd_name)
{
	struct txpwr_lmt_ent *ent;
	_list *cur, *head;
	u8 found = 0;

	head = &rfctl->txpwr_lmt_list;
	cur = get_next(head);

	while ((rtw_end_of_queue_search(head, cur)) == _FALSE) {
		ent = LIST_CONTAINOR(cur, struct txpwr_lmt_ent, list);
		cur = get_next(cur);

		if (strcmp(ent->regd_name, regd_name) == 0) {
			found = 1;
			break;
		}
	}

	if (found)
		return ent;
	return NULL;
}

inline struct txpwr_lmt_ent *rtw_txpwr_lmt_get_by_name(struct rf_ctl_t *rfctl, const char *regd_name)
{
	struct txpwr_lmt_ent *ent;
	_irqL irqL;

	_enter_critical_mutex(&rfctl->txpwr_lmt_mutex, &irqL);
	ent = _rtw_txpwr_lmt_get_by_name(rfctl, regd_name);
	_exit_critical_mutex(&rfctl->txpwr_lmt_mutex, &irqL);

	return ent;
}

void rtw_txpwr_lmt_list_free(struct rf_ctl_t *rfctl)
{
	struct txpwr_lmt_ent *ent;
	_irqL irqL;
	_list *cur, *head;

	_enter_critical_mutex(&rfctl->txpwr_lmt_mutex, &irqL);

	head = &rfctl->txpwr_lmt_list;
	cur = get_next(head);

	while ((rtw_end_of_queue_search(head, cur)) == _FALSE) {
		ent = LIST_CONTAINOR(cur, struct txpwr_lmt_ent, list);
		cur = get_next(cur);
		if (ent->regd_name == rfctl->regd_name)
			rfctl->regd_name = regd_str(TXPWR_LMT_NONE);
		rtw_list_delete(&ent->list);
		rtw_vmfree((u8 *)ent, sizeof(struct txpwr_lmt_ent) + strlen(ent->regd_name) + 1);
	}
	rfctl->txpwr_regd_num = 0;

	_exit_critical_mutex(&rfctl->txpwr_lmt_mutex, &irqL);
}
#endif /* CONFIG_TXPWR_LIMIT */

int rtw_ch_to_bb_gain_sel(int ch)
{
	int sel = -1;

	if (ch >= 1 && ch <= 14)
		sel = BB_GAIN_2G;
#ifdef CONFIG_IEEE80211_BAND_5GHZ
	else if (ch >= 36 && ch < 48)
		sel = BB_GAIN_5GLB1;
	else if (ch >= 52 && ch <= 64)
		sel = BB_GAIN_5GLB2;
	else if (ch >= 100 && ch <= 120)
		sel = BB_GAIN_5GMB1;
	else if (ch >= 124 && ch <= 144)
		sel = BB_GAIN_5GMB2;
	else if (ch >= 149 && ch <= 177)
		sel = BB_GAIN_5GHB;
#endif

	return sel;
}

s8 rtw_rf_get_kfree_tx_gain_offset(_adapter *padapter, u8 path, u8 ch)
{
	s8 kfree_offset = 0;

#ifdef CONFIG_RF_POWER_TRIM
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(padapter);
	struct kfree_data_t *kfree_data = GET_KFREE_DATA(padapter);
	s8 bb_gain_sel = rtw_ch_to_bb_gain_sel(ch);

	if (bb_gain_sel < BB_GAIN_2G || bb_gain_sel >= BB_GAIN_NUM) {
		rtw_warn_on(1);
		goto exit;
	}

	if (kfree_data->flag & KFREE_FLAG_ON) {
		kfree_offset = kfree_data->bb_gain[bb_gain_sel][path];
		if (IS_HARDWARE_TYPE_8723D(padapter))
			RTW_INFO("%s path:%s, ch:%u, bb_gain_sel:%d, kfree_offset:%d\n"
				, __func__, (path == 0)?"S1":"S0", 
				ch, bb_gain_sel, kfree_offset);
		else
			RTW_INFO("%s path:%u, ch:%u, bb_gain_sel:%d, kfree_offset:%d\n"
				, __func__, path, ch, bb_gain_sel, kfree_offset);
	}
exit:
#endif /* CONFIG_RF_POWER_TRIM */
	return kfree_offset;
}

void rtw_rf_set_tx_gain_offset(_adapter *adapter, u8 path, s8 offset)
{
	u8 write_value;
	u8 target_path = 0;
	u32 val32 = 0;

	if (IS_HARDWARE_TYPE_8723D(adapter)) {
		target_path = RF_PATH_A; /*in 8723D case path means S0/S1*/
		if (path == PPG_8723D_S1)
			RTW_INFO("kfree gain_offset 0x55:0x%x ",
			rtw_hal_read_rfreg(adapter, target_path, 0x55, 0xffffffff));
		else if (path == PPG_8723D_S0)
			RTW_INFO("kfree gain_offset 0x65:0x%x ",
			rtw_hal_read_rfreg(adapter, target_path, 0x65, 0xffffffff));
	} else {
		target_path = path;
		RTW_INFO("kfree gain_offset 0x55:0x%x ", rtw_hal_read_rfreg(adapter, target_path, 0x55, 0xffffffff));
	}
	
	switch (rtw_get_chip_type(adapter)) {
#ifdef CONFIG_RTL8723D
	case RTL8723D:
		write_value = RF_TX_GAIN_OFFSET_8723D(offset);
		if (path == PPG_8723D_S1)
			rtw_hal_write_rfreg(adapter, target_path, 0x55, 0x0f8000, write_value);
		else if (path == PPG_8723D_S0)
			rtw_hal_write_rfreg(adapter, target_path, 0x65, 0x0f8000, write_value);
		break;
#endif /* CONFIG_RTL8723D */
#ifdef CONFIG_RTL8703B
	case RTL8703B:
		write_value = RF_TX_GAIN_OFFSET_8703B(offset);
		rtw_hal_write_rfreg(adapter, target_path, 0x55, 0x0fc000, write_value);
		break;
#endif /* CONFIG_RTL8703B */
#ifdef CONFIG_RTL8188F
	case RTL8188F:
		write_value = RF_TX_GAIN_OFFSET_8188F(offset);
		rtw_hal_write_rfreg(adapter, target_path, 0x55, 0x0fc000, write_value);
		break;
#endif /* CONFIG_RTL8188F */
#ifdef CONFIG_RTL8192E
	case RTL8192E:
		write_value = RF_TX_GAIN_OFFSET_8192E(offset);
		rtw_hal_write_rfreg(adapter, target_path, 0x55, 0x0f8000, write_value);
		break;
#endif /* CONFIG_RTL8188F */

#ifdef CONFIG_RTL8821A
	case RTL8821:
		write_value = RF_TX_GAIN_OFFSET_8821A(offset);
		rtw_hal_write_rfreg(adapter, target_path, 0x55, 0x0f8000, write_value);
		break;
#endif /* CONFIG_RTL8821A */
#if defined(CONFIG_RTL8814A) || defined(CONFIG_RTL8822B) || defined(CONFIG_RTL8821C)
	case RTL8814A:
	case RTL8822B:
	case RTL8821C:
		RTW_INFO("\nkfree by PhyDM on the sw CH. path %d\n", path);
		break;
#endif /* CONFIG_RTL8814A || CONFIG_RTL8822B || CONFIG_RTL8821C */

	default:
		rtw_warn_on(1);
		break;
	}
	
	if (IS_HARDWARE_TYPE_8723D(adapter)) {
		if (path == PPG_8723D_S1)
			val32 = rtw_hal_read_rfreg(adapter, target_path, 0x55, 0xffffffff);
		else if (path == PPG_8723D_S0)
			val32 = rtw_hal_read_rfreg(adapter, target_path, 0x65, 0xffffffff);
	} else {
		val32 = rtw_hal_read_rfreg(adapter, target_path, 0x55, 0xffffffff);
	}
	RTW_INFO(" after :0x%x\n", val32);
}

void rtw_rf_apply_tx_gain_offset(_adapter *adapter, u8 ch)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	s8 kfree_offset = 0;
	s8 tx_pwr_track_offset = 0; /* TODO: 8814A should consider tx pwr track when setting tx gain offset */
	s8 total_offset;
	int i, total = 0;

	if (IS_HARDWARE_TYPE_8723D(adapter))
		total = 2; /* S1 and S0 */
	else
		total = hal_data->NumTotalRFPath;

	for (i = 0; i < total; i++) {
		kfree_offset = rtw_rf_get_kfree_tx_gain_offset(adapter, i, ch);
		total_offset = kfree_offset + tx_pwr_track_offset;
		rtw_rf_set_tx_gain_offset(adapter, i, total_offset);
	}
}

inline u8 rtw_is_5g_band1(u8 ch)
{
	if (ch >= 36 && ch <= 48)
		return 1;
	return 0;
}

inline u8 rtw_is_5g_band2(u8 ch)
{
	if (ch >= 52 && ch <= 64)
		return 1;
	return 0;
}

inline u8 rtw_is_5g_band3(u8 ch)
{
	if (ch >= 100 && ch <= 144)
		return 1;
	return 0;
}

inline u8 rtw_is_5g_band4(u8 ch)
{
	if (ch >= 149 && ch <= 177)
		return 1;
	return 0;
}

inline u8 rtw_is_dfs_range(u32 hi, u32 lo)
{
	return rtw_is_range_overlap(hi, lo, 5720 + 10, 5260 - 10);
}

u8 rtw_is_dfs_ch(u8 ch)
{
	u32 hi, lo;

	if (!rtw_chbw_to_freq_range(ch, CHANNEL_WIDTH_20, HAL_PRIME_CHNL_OFFSET_DONT_CARE, &hi, &lo))
		return 0;

	return rtw_is_dfs_range(hi, lo);
}

u8 rtw_is_dfs_chbw(u8 ch, u8 bw, u8 offset)
{
	u32 hi, lo;

	if (!rtw_chbw_to_freq_range(ch, bw, offset, &hi, &lo))
		return 0;

	return rtw_is_dfs_range(hi, lo);
}

bool rtw_is_long_cac_range(u32 hi, u32 lo, u8 dfs_region)
{
	return (dfs_region == PHYDM_DFS_DOMAIN_ETSI && rtw_is_range_overlap(hi, lo, 5650, 5600)) ? _TRUE : _FALSE;
}

bool rtw_is_long_cac_ch(u8 ch, u8 bw, u8 offset, u8 dfs_region)
{
	u32 hi, lo;

	if (rtw_chbw_to_freq_range(ch, bw, offset, &hi, &lo) == _FALSE)
		return _FALSE;

	return rtw_is_long_cac_range(hi, lo, dfs_region) ? _TRUE : _FALSE;
}
