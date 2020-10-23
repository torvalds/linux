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

#define ch_to_cch_2g_idx(ch) ((ch) - 1)

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

#define ch_to_cch_5g_20m_idx(ch) \
	( \
		((ch) >= 36 && (ch) <= 64) ? (((ch) - 36) >> 2) : \
		((ch) >= 100 && (ch) <= 144) ? 8 + (((ch) - 100) >> 2) : \
		((ch) >= 149 && (ch) <= 177) ? 20 + (((ch) - 149) >> 2) : 255 \
	)

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

/*
 * Get center channel of smaller bandwidth by @param cch, @param bw, @param opch
 * @cch: the given center channel
 * @bw: the given bandwidth
 * @opch: the given operating channel
 *
 * return center channel of smaller bandiwdth if valid, or 0
 */
u8 rtw_get_scch_by_cch_opch(u8 cch, u8 bw, u8 opch)
{
	u8 offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;

	if (opch > cch)
		offset = HAL_PRIME_CHNL_OFFSET_UPPER;
	else if (opch < cch)
		offset = HAL_PRIME_CHNL_OFFSET_LOWER;

	return rtw_get_scch_by_cch_offset(cch, bw, offset);
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
	if (bw > CHANNEL_WIDTH_160)
		return 0;

	return center_chs_5g_by_bw[bw].ch_num;
}

inline u8 center_chs_5g(u8 bw, u8 id)
{
	if (bw > CHANNEL_WIDTH_160)
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
		&& bw <= CHANNEL_WIDTH_40
	) {
		c_chs_ent = &center_chs_2g_by_bw[bw];
		op_chs_ent = &op_chs_of_cch_2g_by_bw[bw];
	} else if (cch >= 36 && cch <= 177
		&& bw <= CHANNEL_WIDTH_160
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

u8 rtw_get_offset_by_chbw(u8 ch, u8 bw, u8 *r_offset)
{
	u8 valid = 1;
	u8 offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;

	if (bw == CHANNEL_WIDTH_20)
		goto exit;

	if (bw >= CHANNEL_WIDTH_80 && ch <= 14) {
		valid = 0;
		goto exit;
	}

	if (ch >= 1 && ch <= 4)
		offset = HAL_PRIME_CHNL_OFFSET_LOWER;
	else if (ch >= 5 && ch <= 9) {
		if (*r_offset == HAL_PRIME_CHNL_OFFSET_LOWER || *r_offset == HAL_PRIME_CHNL_OFFSET_UPPER)
			offset = *r_offset; /* both lower and upper is valid, obey input value */
		else
			offset = HAL_PRIME_CHNL_OFFSET_UPPER; /* default use upper */
	} else if (ch >= 10 && ch <= 13)
		offset = HAL_PRIME_CHNL_OFFSET_UPPER;
	else if (ch == 14) {
		valid = 0; /* ch14 doesn't support 40MHz bandwidth */
		goto exit;
	} else if (ch >= 36 && ch <= 177) {
		switch (ch) {
		case 36:
		case 44:
		case 52:
		case 60:
		case 100:
		case 108:
		case 116:
		case 124:
		case 132:
		case 140:
		case 149:
		case 157:
		case 165:
		case 173:
			offset = HAL_PRIME_CHNL_OFFSET_LOWER;
			break;
		case 40:
		case 48:
		case 56:
		case 64:
		case 104:
		case 112:
		case 120:
		case 128:
		case 136:
		case 144:
		case 153:
		case 161:
		case 169:
		case 177:
			offset = HAL_PRIME_CHNL_OFFSET_UPPER;
			break;
		default:
			valid = 0;
			break;
		}
	} else
		valid = 0;

exit:
	if (valid && r_offset)
		*r_offset = offset;
	return valid;
}

u8 rtw_get_center_ch(u8 ch, u8 bw, u8 offset)
{
	u8 cch = ch;

	if (bw == CHANNEL_WIDTH_160) {
		if (ch % 4 == 0) {
			if (ch >= 36 && ch <= 64)
				cch = 50;
			else if (ch >= 100 && ch <= 128)
				cch = 114;
		} else if (ch % 4 == 1) {
			if (ch >= 149 && ch <= 177)
				cch = 163;
		}

	} else if (bw == CHANNEL_WIDTH_80) {
		if (ch <= 14)
			cch = 7; /* special case for 2.4G */
		else if (ch % 4 == 0) {
			if (ch >= 36 && ch <= 48)
				cch = 42;
			else if (ch >= 52 && ch <= 64)
				cch = 58;
			else if (ch >= 100 && ch <= 112)
				cch = 106;
			else if (ch >= 116 && ch <= 128)
				cch = 122;
			else if (ch >= 132 && ch <= 144)
				cch = 138;
		} else if (ch % 4 == 1) {
			if (ch >= 149 && ch <= 161)
				cch = 155;
			else if (ch >= 165 && ch <= 177)
				cch = 171;
		}

	} else if (bw == CHANNEL_WIDTH_40) {
		if (offset == HAL_PRIME_CHNL_OFFSET_LOWER)
			cch = ch + 2;
		else if (offset == HAL_PRIME_CHNL_OFFSET_UPPER)
			cch = ch - 2;

	} else if (bw == CHANNEL_WIDTH_20
		|| bw == CHANNEL_WIDTH_10
		|| bw == CHANNEL_WIDTH_5
	)
		; /* same as ch */
	else
		rtw_warn_on(1);

	return cch;
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

	if (bw == CHANNEL_WIDTH_160) {
		hi_ret = freq + 80;
		lo_ret = freq - 80;
	} else if (bw == CHANNEL_WIDTH_80) {
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

const char *const _ch_width_str[CHANNEL_WIDTH_MAX] = {
	[CHANNEL_WIDTH_20]		= "20MHz",
	[CHANNEL_WIDTH_40]		= "40MHz",
	[CHANNEL_WIDTH_80]		= "80MHz",
	[CHANNEL_WIDTH_160]		= "160MHz",
	[CHANNEL_WIDTH_80_80]	= "80_80MHz",
	[CHANNEL_WIDTH_5]		= "5MHz",
	[CHANNEL_WIDTH_10]		= "10MHz",
};

const u8 _ch_width_to_bw_cap[CHANNEL_WIDTH_MAX] = {
	[CHANNEL_WIDTH_20]		= BW_CAP_20M,
	[CHANNEL_WIDTH_40]		= BW_CAP_40M,
	[CHANNEL_WIDTH_80]		= BW_CAP_80M,
	[CHANNEL_WIDTH_160]		= BW_CAP_160M,
	[CHANNEL_WIDTH_80_80]	= BW_CAP_80_80M,
	[CHANNEL_WIDTH_5]		= BW_CAP_5M,
	[CHANNEL_WIDTH_10]		= BW_CAP_10M,
};

const char *const _band_str[] = {
	"2.4G",
	"5G",
	"BAND_MAX",
};

const u8 _band_to_band_cap[] = {
	BAND_CAP_2G,
	BAND_CAP_5G,
	0,
};

const char *const _opc_bw_str[OPC_BW_NUM] = {
	"20M ",		/* OPC_BW20 */
	"40M+",		/* OPC_BW40PLUS */
	"40M-",		/* OPC_BW40MINUS */
	"80M ",		/* OPC_BW80 */
	"160M ",	/* OPC_BW160 */
	"80+80M ",	/* OPC_BW80P80 */
};

const u8 _opc_bw_to_ch_width[OPC_BW_NUM] = {
	CHANNEL_WIDTH_20,		/* OPC_BW20 */
	CHANNEL_WIDTH_40,		/* OPC_BW40PLUS */
	CHANNEL_WIDTH_40,		/* OPC_BW40MINUS */
	CHANNEL_WIDTH_80,		/* OPC_BW80 */
	CHANNEL_WIDTH_160,		/* OPC_BW160 */
	CHANNEL_WIDTH_80_80,	/* OPC_BW80P80 */
};

/* global operating class database */

struct op_class_t {
	u8 class_id;
	BAND_TYPE band;
	enum opc_bw bw;
	u8 *len_ch_attr;
};

#define OPC_CH_LIST_LEN(_opc) (_opc.len_ch_attr[0])
#define OPC_CH_LIST_CH(_opc, _i) (_opc.len_ch_attr[_i + 1])

#define OP_CLASS_ENT(_class, _band, _bw, _len, arg...) \
	{.class_id = _class, .band = _band, .bw = _bw, .len_ch_attr = (uint8_t[_len + 1]) {_len, ##arg},}

/* 802.11-2016 Table E-4, partial */
static const struct op_class_t global_op_class[] = {
	/* 2G ch1~13, 20M */
	OP_CLASS_ENT(81,	BAND_ON_2_4G,	OPC_BW20,		13,	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13),
	/* 2G ch14, 20M */
	OP_CLASS_ENT(82,	BAND_ON_2_4G,	OPC_BW20,		1,	14),
	/* 2G, 40M */
	OP_CLASS_ENT(83,	BAND_ON_2_4G, 	OPC_BW40PLUS,	9,	1, 2, 3, 4, 5, 6, 7, 8, 9),
	OP_CLASS_ENT(84,	BAND_ON_2_4G,	OPC_BW40MINUS,	9,	5, 6, 7, 8, 9, 10, 11, 12, 13),
	/* 5G band 1, 20M & 40M */
	OP_CLASS_ENT(115,	BAND_ON_5G,		OPC_BW20,		4,	36, 40, 44, 48),
	OP_CLASS_ENT(116,	BAND_ON_5G,		OPC_BW40PLUS,	2,	36, 44),
	OP_CLASS_ENT(117,	BAND_ON_5G,		OPC_BW40MINUS,	2,	40, 48),
	/* 5G band 2, 20M & 40M */
	OP_CLASS_ENT(118,	BAND_ON_5G,		OPC_BW20,		4,	52, 56, 60, 64),
	OP_CLASS_ENT(119,	BAND_ON_5G,		OPC_BW40PLUS,	2,	52, 60),
	OP_CLASS_ENT(120,	BAND_ON_5G,		OPC_BW40MINUS,	2,	56, 64),
	/* 5G band 3, 20M & 40M */
	OP_CLASS_ENT(121,	BAND_ON_5G,		OPC_BW20,		12,	100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144),
	OP_CLASS_ENT(122,	BAND_ON_5G,		OPC_BW40PLUS,	6,	100, 108, 116, 124, 132, 140),
	OP_CLASS_ENT(123,	BAND_ON_5G,		OPC_BW40MINUS,	6,	104, 112, 120, 128, 136, 144),
	/* 5G band 4, 20M & 40M */
	OP_CLASS_ENT(124,	BAND_ON_5G,		OPC_BW20,		4,	149, 153, 157, 161),
	OP_CLASS_ENT(125,	BAND_ON_5G,		OPC_BW20,		6,	149, 153, 157, 161, 165, 169),
	OP_CLASS_ENT(126,	BAND_ON_5G,		OPC_BW40PLUS,	2,	149, 157),
	OP_CLASS_ENT(127,	BAND_ON_5G,		OPC_BW40MINUS,	2,	153, 161),
	/* 5G, 80M & 160M */
	OP_CLASS_ENT(128,	BAND_ON_5G,		OPC_BW80,		24,	36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161),
	OP_CLASS_ENT(129,	BAND_ON_5G,		OPC_BW160,		16,	36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128),
	#if 0 /* TODO */
	/* 5G, 80+80M */
	{130,	BAND_ON_5G,		OPC_BW80P80,	0x0FFFFFF},
	#endif
};

static const int global_op_class_num = sizeof(global_op_class) / sizeof(struct op_class_t);

static const struct op_class_t *get_global_op_class_by_id(u8 gid)
{
	int i;

	for (i = 0; i < global_op_class_num; i++)
		if (global_op_class[i].class_id == gid)
			break;

	return i < global_op_class_num ? &global_op_class[i] : NULL;
}

bool is_valid_global_op_class_id(u8 gid)
{
	return get_global_op_class_by_id(gid) ? 1 : 0;
}

static bool is_valid_global_op_class_ch(const struct op_class_t *opc, u8 ch)
{
	int array_idx;
	int i;

	if (opc < global_op_class
		|| (((u8 *)opc) - ((u8 *)global_op_class)) % sizeof(struct op_class_t)
	) {
		RTW_ERR("Invalid opc pointer:%p (global_op_class:%p, sizeof(struct op_class_t):%zu, %zu)\n"
			, opc, global_op_class, sizeof(struct op_class_t), (((u8 *)opc) - ((u8 *)global_op_class)) % sizeof(struct op_class_t));
		return 0;
	}

	array_idx = (((u8 *)opc) - ((u8 *)global_op_class)) / sizeof(struct op_class_t);

	for (i = 0; i < OPC_CH_LIST_LEN(global_op_class[array_idx]); i++)
		if (OPC_CH_LIST_CH(global_op_class[array_idx], i) == ch)
			break;

	return i < OPC_CH_LIST_LEN(global_op_class[array_idx]);
}

static enum opc_bw get_global_opc_bw_by_id(u8 gid)
{
	int i;

	for (i = 0; i < global_op_class_num; i++)
		if (global_op_class[i].class_id == gid)
			break;

	return i < global_op_class_num ? global_op_class[i].bw : OPC_BW_NUM;
}

/* -2: logic error, -1: error, 0: is already BW20 */
s16 get_sub_op_class(u8 gid, u8 ch)
{
	const struct op_class_t *opc = get_global_op_class_by_id(gid);
	int i;
	enum channel_width bw; 

	if (!opc)
		return -1;

	if (!is_valid_global_op_class_ch(opc, ch)) {
		return -1;
	}

	if (opc->bw == OPC_BW20)
		return 0;

	bw = opc_bw_to_ch_width(opc->bw);

	for (i = 0; i < global_op_class_num; i++) {
		if (bw != opc_bw_to_ch_width(global_op_class[i].bw) + 1)
			continue;
		if (is_valid_global_op_class_ch(&global_op_class[i], ch))
			break;
	}

	return i < global_op_class_num ? global_op_class[i].class_id : -2;
}

static void dump_op_class_ch_title(void *sel)
{
	RTW_PRINT_SEL(sel, "%-5s %-4s %-7s ch_list\n"
		, "class", "band", "bw");
}

static void dump_global_op_class_ch_single(void *sel, u8 gid)
{
	u8 i;
	char buf[100];
	char *pos = buf;

	for (i = 0; i < OPC_CH_LIST_LEN(global_op_class[gid]); i++)
		pos += snprintf(pos, 100 - (pos - buf), " %u", OPC_CH_LIST_CH(global_op_class[gid], i));

	RTW_PRINT_SEL(sel, "%5u %4s %7s%s\n"
		, global_op_class[gid].class_id
		, band_str(global_op_class[gid].band)
		, opc_bw_str(global_op_class[gid].bw), buf);
}

#ifdef CONFIG_RTW_DEBUG
static bool dbg_global_op_class_validate(u8 gid)
{
	u8 i;
	u8 ch, bw, offset, cch;
	bool ret = 1;

	switch (global_op_class[gid].bw) {
	case OPC_BW20:
		bw = CHANNEL_WIDTH_20;
		offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
		break;
	case OPC_BW40PLUS:
		bw = CHANNEL_WIDTH_40;
		offset = HAL_PRIME_CHNL_OFFSET_LOWER;
		break;
	case OPC_BW40MINUS:
		bw = CHANNEL_WIDTH_40;
		offset = HAL_PRIME_CHNL_OFFSET_UPPER;
		break;
	case OPC_BW80:
		bw = CHANNEL_WIDTH_80;
		offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
		break;
	case OPC_BW160:
		bw = CHANNEL_WIDTH_160;
		offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
		break;
	case OPC_BW80P80: /* TODO */
	default:
		RTW_ERR("%s class:%u unsupported opc_bw:%u\n"
			, __func__, global_op_class[gid].class_id, global_op_class[gid].bw);
		ret = 0;
		goto exit;
	}

	for (i = 0; i < OPC_CH_LIST_LEN(global_op_class[gid]); i++) {
		u8 *op_chs;
		u8 op_ch_num;
		u8 k;

		ch = OPC_CH_LIST_CH(global_op_class[gid], i);
		cch = rtw_get_center_ch(ch ,bw, offset);
		if (!cch) {
			RTW_ERR("%s can't get cch from class:%u ch:%u\n"
				, __func__, global_op_class[gid].class_id, ch);
			ret = 0;
			continue;
		}

		if (!rtw_get_op_chs_by_cch_bw(cch, bw, &op_chs, &op_ch_num)) {
			RTW_ERR("%s can't get op chs from class:%u cch:%u\n"
				, __func__, global_op_class[gid].class_id, cch);
			ret = 0;
			continue;
		}

		for (k = 0; k < op_ch_num; k++) {
			if (*(op_chs + k) == ch)
				break;
		}
		if (k >= op_ch_num) {
			RTW_ERR("%s can't get ch:%u from op_chs class:%u cch:%u\n"
				, __func__, ch, global_op_class[i].class_id, cch);
			ret = 0;
		}
	}

exit:
	return ret;
}
#endif /* CONFIG_RTW_DEBUG */

void dump_global_op_class(void *sel)
{
	u8 i;

	dump_op_class_ch_title(sel);

	for (i = 0; i < global_op_class_num; i++)
		dump_global_op_class_ch_single(sel, i);
}

u8 rtw_get_op_class_by_chbw(u8 ch, u8 bw, u8 offset)
{
	BAND_TYPE band = BAND_MAX;
	int i;
	u8 gid = 0; /* invalid */

	if (rtw_is_2g_ch(ch))
		band = BAND_ON_2_4G;
	else if (rtw_is_5g_ch(ch))
		band = BAND_ON_5G;
	else
		goto exit;

	switch (bw) {
	case CHANNEL_WIDTH_20:
	case CHANNEL_WIDTH_40:
	case CHANNEL_WIDTH_80:
	case CHANNEL_WIDTH_160:
	#if 0 /* TODO */
	case CHANNEL_WIDTH_80_80:
	#endif
		break;
	default:
		goto exit;
	}

	for (i = 0; i < global_op_class_num; i++) {
		if (band != global_op_class[i].band)
			continue;

		if (opc_bw_to_ch_width(global_op_class[i].bw) != bw)
			continue;

		if ((global_op_class[i].bw == OPC_BW40PLUS
				&& offset != HAL_PRIME_CHNL_OFFSET_LOWER)
			|| (global_op_class[i].bw == OPC_BW40MINUS
				&& offset != HAL_PRIME_CHNL_OFFSET_UPPER)
		)
			continue;

		if (is_valid_global_op_class_ch(&global_op_class[i], ch))
			goto get;
	}

get:
	if (i < global_op_class_num) {
		#if 0 /* TODO */
		if (bw == CHANNEL_WIDTH_80_80) {
			/* search another ch */
			if (!is_valid_global_op_class_ch(&global_op_class[i], ch2))
				goto exit;
		}
		#endif

		gid = global_op_class[i].class_id;
	}

exit:
	return gid;
}

u8 rtw_get_bw_offset_by_op_class_ch(u8 gid, u8 ch, u8 *bw, u8 *offset)
{
	enum opc_bw opc_bw;
	u8 valid = 0;
	int i;

	opc_bw = get_global_opc_bw_by_id(gid);
	if (opc_bw == OPC_BW_NUM)
		goto exit;

	*bw = opc_bw_to_ch_width(opc_bw);

	if (opc_bw == OPC_BW40PLUS)
		*offset = HAL_PRIME_CHNL_OFFSET_LOWER;
	else if (opc_bw == OPC_BW40MINUS)
		*offset = HAL_PRIME_CHNL_OFFSET_UPPER;

	if (rtw_get_offset_by_chbw(ch, *bw, offset))
		valid = 1;

exit:
	return valid;
}

static struct op_class_pref_t *opc_pref_alloc(u8 class_id)
{
	int i, j;
	struct op_class_pref_t *opc_pref = NULL;

	for (i = 0; i < global_op_class_num; i++)
		if (global_op_class[i].class_id == class_id)
			break;

	if (i >= global_op_class_num)
		goto exit;

	opc_pref = rtw_zmalloc(sizeof(*opc_pref));
	if (!opc_pref)
		goto exit;

	opc_pref->class_id = global_op_class[i].class_id;
	opc_pref->band = global_op_class[i].band;
	opc_pref->bw = global_op_class[i].bw;

	for (j = 0; j < OPC_CH_LIST_LEN(global_op_class[i]); j++) {
		opc_pref->chs[j].ch = OPC_CH_LIST_CH(global_op_class[i], j);
		opc_pref->chs[j].static_non_op = 1;
		opc_pref->chs[j].no_ir = 1;
		opc_pref->chs[j].max_txpwr = UNSPECIFIED_MBM;
	}
	opc_pref->ch_num = OPC_CH_LIST_LEN(global_op_class[i]);

exit:
	return opc_pref;
}

static void opc_pref_free(struct op_class_pref_t *opc_pref)
{
	rtw_mfree(opc_pref, sizeof(*opc_pref));
}

int op_class_pref_init(_adapter *adapter)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	struct registry_priv *regsty = adapter_to_regsty(adapter);
	u8 bw;
	struct op_class_pref_t *opc_pref;
	int i;
	u8 op_class_num = 0;
	u8 band_bmp = 0;
	u8 bw_bmp[BAND_MAX] = {0};
	int ret = _FAIL;

	rfctl->spt_op_class_ch = rtw_zmalloc(sizeof(struct op_class_pref_t *) * global_op_class_num);
	if (!rfctl->spt_op_class_ch) {
		RTW_ERR("%s alloc rfctl->spt_op_class_ch fail\n", __func__);
		goto exit;
	}

	if (IsSupported24G(regsty->wireless_mode) && hal_chk_band_cap(adapter, BAND_CAP_2G))
		band_bmp |= BAND_CAP_2G;
	if (is_supported_5g(regsty->wireless_mode) && hal_chk_band_cap(adapter, BAND_CAP_5G))
		band_bmp |= BAND_CAP_5G;

	bw_bmp[BAND_ON_2_4G] = (ch_width_to_bw_cap(REGSTY_BW_2G(regsty) + 1) - 1) & (GET_HAL_SPEC(adapter)->bw_cap);
	bw_bmp[BAND_ON_5G] = (ch_width_to_bw_cap(REGSTY_BW_5G(regsty) + 1) - 1) & (GET_HAL_SPEC(adapter)->bw_cap);
	if (!REGSTY_IS_11AC_ENABLE(regsty)
		|| !is_supported_vht(regsty->wireless_mode)
	)
		bw_bmp[BAND_ON_5G] &= ~(BW_CAP_80M | BW_CAP_160M);

	if (0) {
		RTW_INFO("REGSTY_BW_2G(regsty):%u\n", REGSTY_BW_2G(regsty));
		RTW_INFO("REGSTY_BW_5G(regsty):%u\n", REGSTY_BW_5G(regsty));
		RTW_INFO("GET_HAL_SPEC(adapter)->bw_cap:0x%x\n", GET_HAL_SPEC(adapter)->bw_cap);
		RTW_INFO("band_bmp:0x%x\n", band_bmp);
		RTW_INFO("bw_bmp[2G]:0x%x\n", bw_bmp[BAND_ON_2_4G]);
		RTW_INFO("bw_bmp[5G]:0x%x\n", bw_bmp[BAND_ON_5G]);
	}

	for (i = 0; i < global_op_class_num; i++) {
		#ifdef CONFIG_RTW_DEBUG
		rtw_warn_on(!dbg_global_op_class_validate(i));
		#endif

		if (!(band_bmp & band_to_band_cap(global_op_class[i].band)))
			continue;

		bw = opc_bw_to_ch_width(global_op_class[i].bw);
		if (bw == CHANNEL_WIDTH_MAX
			|| bw == CHANNEL_WIDTH_80_80 /* TODO */
		)
			continue;

		if (!(bw_bmp[global_op_class[i].band] & ch_width_to_bw_cap(bw)))
			continue;

		opc_pref = opc_pref_alloc(global_op_class[i].class_id);
		if (!opc_pref) {
			RTW_ERR("%s opc_pref_alloc(%u) fail\n", __func__, global_op_class[i].class_id);
			goto exit;
		}

		if (opc_pref->ch_num) {
			rfctl->spt_op_class_ch[i] = opc_pref;
			op_class_num++;
		} else
			opc_pref_free(opc_pref);
	}

	rfctl->cap_spt_op_class_num = op_class_num;
	ret = _SUCCESS;

exit:
	return ret;
}

void op_class_pref_deinit(_adapter *adapter)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	int i;

	if (!rfctl->spt_op_class_ch)
		return;

	for (i = 0; i < global_op_class_num; i++) {
		if (rfctl->spt_op_class_ch[i]) {
			opc_pref_free(rfctl->spt_op_class_ch[i]);
			rfctl->spt_op_class_ch[i] = NULL;
		}
	}

	rtw_mfree(rfctl->spt_op_class_ch, sizeof(struct op_class_pref_t *) * global_op_class_num);
	rfctl->spt_op_class_ch = NULL;
}

void op_class_pref_apply_regulatory(_adapter *adapter, u8 reason)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	RT_CHANNEL_INFO *chset = rfctl->channel_set;
	struct registry_priv *regsty = adapter_to_regsty(adapter);
	u8 ch, bw, offset, cch;
	struct op_class_pref_t *opc_pref;
	int i, j;
	u8 reg_op_class_num = 0;
	u8 op_class_num = 0;

	for (i = 0; i < global_op_class_num; i++) {
		if (!rfctl->spt_op_class_ch[i])
			continue;
		opc_pref = rfctl->spt_op_class_ch[i];

		/* reset all channel */
		for (j = 0; opc_pref->chs[j].ch != 0; j++) {
			if (reason >= REG_CHANGE)
				opc_pref->chs[j].static_non_op = 1;
			if (reason != REG_TXPWR_CHANGE)
				opc_pref->chs[j].no_ir = 1;
			if (reason >= REG_TXPWR_CHANGE)
				opc_pref->chs[j].max_txpwr = UNSPECIFIED_MBM;
		}
		if (reason >= REG_CHANGE)
			opc_pref->op_ch_num = 0;
		if (reason != REG_TXPWR_CHANGE)
			opc_pref->ir_ch_num = 0;

		switch (opc_pref->bw) {
		case OPC_BW20:
			bw = CHANNEL_WIDTH_20;
			offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
			break;
		case OPC_BW40PLUS:
			bw = CHANNEL_WIDTH_40;
			offset = HAL_PRIME_CHNL_OFFSET_LOWER;
			break;
		case OPC_BW40MINUS:
			bw = CHANNEL_WIDTH_40;
			offset = HAL_PRIME_CHNL_OFFSET_UPPER;
			break;
		case OPC_BW80:
			bw = CHANNEL_WIDTH_80;
			offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
			break;
		case OPC_BW160:
			bw = CHANNEL_WIDTH_160;
			offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
			break;
		case OPC_BW80P80: /* TODO */
		default:
			continue;
		}

		if (rfctl->country_ent && !COUNTRY_CHPLAN_EN_11AC(rfctl->country_ent)
			&& (bw == CHANNEL_WIDTH_80 || bw == CHANNEL_WIDTH_160))
			continue;

		for (j = 0; opc_pref->chs[j].ch != 0; j++) {
			u8 *op_chs;
			u8 op_ch_num;
			u8 k, l;
			int chset_idx;

			ch = opc_pref->chs[j].ch;

			if (reason >= REG_TXPWR_CHANGE)
				opc_pref->chs[j].max_txpwr = rtw_rfctl_get_reg_max_txpwr_mbm(rfctl, ch, bw, offset, 1);

			if (reason == REG_TXPWR_CHANGE)
				continue;

			cch = rtw_get_center_ch(ch ,bw, offset);
			if (!cch)
				continue;

			if (!rtw_get_op_chs_by_cch_bw(cch, bw, &op_chs, &op_ch_num))
				continue;

			for (k = 0, l = 0; k < op_ch_num; k++) {
				chset_idx = rtw_chset_search_ch(chset, *(op_chs + k));
				if (chset_idx == -1)
					break;
				if (bw >= CHANNEL_WIDTH_40) {
					if ((chset[chset_idx].flags & RTW_CHF_NO_HT40U) && k % 2 == 0)
						break;
					if ((chset[chset_idx].flags & RTW_CHF_NO_HT40L) && k % 2 == 1)
						break;
				}
				if (bw >= CHANNEL_WIDTH_80 && (chset[chset_idx].flags & RTW_CHF_NO_80MHZ))
					break;
				if (bw >= CHANNEL_WIDTH_160 && (chset[chset_idx].flags & RTW_CHF_NO_160MHZ))
					break;
				if ((chset[chset_idx].flags & RTW_CHF_DFS) && rtw_rfctl_dfs_domain_unknown(rfctl))
					continue;
				if (chset[chset_idx].flags & RTW_CHF_NO_IR)
					continue;
				l++;
			}
			if (k < op_ch_num)
				continue;

			if (reason >= REG_CHANGE) {
				opc_pref->chs[j].static_non_op = 0;
				opc_pref->op_ch_num++;
			}

			if (l >= op_ch_num) {
				opc_pref->chs[j].no_ir = 0;
				opc_pref->ir_ch_num++;
			}
		}

		if (opc_pref->op_ch_num)
			reg_op_class_num++;
		if (opc_pref->ir_ch_num)
			op_class_num++;
	}

	rfctl->reg_spt_op_class_num = reg_op_class_num;
	rfctl->cur_spt_op_class_num = op_class_num;
}

static void dump_opc_pref_single(void *sel, struct op_class_pref_t *opc_pref, bool show_snon_ocp, bool show_no_ir, bool detail)
{
	u8 i;
	u8 ch_num = 0;
	char buf[256];
	char *pos = buf;

	if (!show_snon_ocp && !opc_pref->op_ch_num)
		return;
	if (!show_no_ir && !opc_pref->ir_ch_num)
		return;

	for (i = 0; opc_pref->chs[i].ch != 0; i++) {
		if ((show_snon_ocp || !opc_pref->chs[i].static_non_op)
			&& (show_no_ir || !opc_pref->chs[i].no_ir)
		) {
			if (detail)
				pos += snprintf(pos, 256 - (pos - buf), " %4u", opc_pref->chs[i].ch);
			else
				pos += snprintf(pos, 256 - (pos - buf), " %u", opc_pref->chs[i].ch);
		}
	}

	RTW_PRINT_SEL(sel, "%5u %4s %7s%s\n"
		, opc_pref->class_id
		, band_str(opc_pref->band)
		, opc_bw_str(opc_pref->bw), buf);

	if (!detail)
		return;

	pos = buf;
	for (i = 0; opc_pref->chs[i].ch != 0; i++) {
		if ((show_snon_ocp || !opc_pref->chs[i].static_non_op)
			&& (show_no_ir || !opc_pref->chs[i].no_ir)
		) {
			pos += snprintf(pos, 256 - (pos - buf), "   %c%c"
				, opc_pref->chs[i].no_ir ? ' ' : 'I'
				, opc_pref->chs[i].static_non_op ? ' ' : 'E'
			);
		}
	}
	RTW_PRINT_SEL(sel, "                  %s\n", buf);

	pos = buf;
	for (i = 0; opc_pref->chs[i].ch != 0; i++) {
		if ((show_snon_ocp || !opc_pref->chs[i].static_non_op)
			&& (show_no_ir || !opc_pref->chs[i].no_ir)
		) {
			if (opc_pref->chs[i].max_txpwr == UNSPECIFIED_MBM)
				pos += snprintf(pos, 256 - (pos - buf), "     ");
			else
				pos += snprintf(pos, 256 - (pos - buf), " %4d", opc_pref->chs[i].max_txpwr);
		}
	}
	RTW_PRINT_SEL(sel, "                  %s\n", buf);
}

void dump_cap_spt_op_class_ch(void *sel, struct rf_ctl_t *rfctl, bool detail)
{
	u8 i;

	dump_op_class_ch_title(sel);

	for (i = 0; i < global_op_class_num; i++) {
		if (!rfctl->spt_op_class_ch[i])
			continue;
		dump_opc_pref_single(sel, rfctl->spt_op_class_ch[i], 1, 1, detail);
	}

	RTW_PRINT_SEL(sel, "op_class number:%d\n", rfctl->cap_spt_op_class_num);
}

void dump_reg_spt_op_class_ch(void *sel, struct rf_ctl_t *rfctl, bool detail)
{
	u8 i;

	dump_op_class_ch_title(sel);

	for (i = 0; i < global_op_class_num; i++) {
		if (!rfctl->spt_op_class_ch[i])
			continue;
		dump_opc_pref_single(sel, rfctl->spt_op_class_ch[i], 0, 1, detail);
	}

	RTW_PRINT_SEL(sel, "op_class number:%d\n", rfctl->reg_spt_op_class_num);
}

void dump_cur_spt_op_class_ch(void *sel, struct rf_ctl_t *rfctl, bool detail)
{
	u8 i;

	dump_op_class_ch_title(sel);

	for (i = 0; i < global_op_class_num; i++) {
		if (!rfctl->spt_op_class_ch[i])
			continue;
		dump_opc_pref_single(sel, rfctl->spt_op_class_ch[i], 0, 0, detail);
	}

	RTW_PRINT_SEL(sel, "op_class number:%d\n", rfctl->cur_spt_op_class_num);
}

const u8 _rf_type_to_rf_tx_cnt[RF_TYPE_MAX] = {
	[RF_1T1R] = 1,
	[RF_1T2R] = 1,
	[RF_1T3R] = 1,
	[RF_1T4R] = 1,
	[RF_2T1R] = 2,
	[RF_2T2R] = 2,
	[RF_2T3R] = 2,
	[RF_2T4R] = 2,
	[RF_3T1R] = 3,
	[RF_3T2R] = 3,
	[RF_3T3R] = 3,
	[RF_3T4R] = 3,
	[RF_4T1R] = 4,
	[RF_4T2R] = 4,
	[RF_4T3R] = 4,
	[RF_4T4R] = 4,
};

const u8 _rf_type_to_rf_rx_cnt[RF_TYPE_MAX] = {
	[RF_1T1R] = 1,
	[RF_1T2R] = 2,
	[RF_1T3R] = 3,
	[RF_1T4R] = 4,
	[RF_2T1R] = 1,
	[RF_2T2R] = 2,
	[RF_2T3R] = 3,
	[RF_2T4R] = 4,
	[RF_3T1R] = 1,
	[RF_3T2R] = 2,
	[RF_3T3R] = 3,
	[RF_3T4R] = 4,
	[RF_4T1R] = 1,
	[RF_4T2R] = 2,
	[RF_4T3R] = 3,
	[RF_4T4R] = 4,
};

const char *const _rf_type_to_rfpath_str[RF_TYPE_MAX] = {
	[RF_1T1R] = "RF_1T1R",
	[RF_1T2R] = "RF_1T2R",
	[RF_1T3R] = "RF_1T3R",
	[RF_1T4R] = "RF_1T4R",
	[RF_2T1R] = "RF_2T1R",
	[RF_2T2R] = "RF_2T2R",
	[RF_2T3R] = "RF_2T3R",
	[RF_2T4R] = "RF_2T4R",
	[RF_3T1R] = "RF_3T1R",
	[RF_3T2R] = "RF_3T2R",
	[RF_3T3R] = "RF_3T3R",
	[RF_3T4R] = "RF_3T4R",
	[RF_4T1R] = "RF_4T1R",
	[RF_4T2R] = "RF_4T2R",
	[RF_4T3R] = "RF_4T3R",
	[RF_4T4R] = "RF_4T4R",
};

void rf_type_to_default_trx_bmp(enum rf_type rf, enum bb_path *tx, enum bb_path *rx)
{
	u8 tx_num = rf_type_to_rf_tx_cnt(rf);
	u8 rx_num = rf_type_to_rf_rx_cnt(rf);
	int i;

	*tx = *rx = 0;

	for (i = 0; i < tx_num; i++)
		*tx |= BIT(i);
	for (i = 0; i < rx_num; i++)
		*rx |= BIT(i);
}

static const u8 _trx_num_to_rf_type[RF_PATH_MAX][RF_PATH_MAX] = {
	{RF_1T1R,	RF_1T2R,	RF_1T3R,	RF_1T4R},
	{RF_2T1R,	RF_2T2R,	RF_2T3R,	RF_2T4R},
	{RF_3T1R,	RF_3T2R,	RF_3T3R,	RF_3T4R},
	{RF_4T1R,	RF_4T2R,	RF_4T3R,	RF_4T4R},
};

enum rf_type trx_num_to_rf_type(u8 tx_num, u8 rx_num)
{
	if (tx_num > 0 && tx_num <= RF_PATH_MAX && rx_num > 0 && rx_num <= RF_PATH_MAX)
		return _trx_num_to_rf_type[tx_num - 1][rx_num - 1];
	return RF_TYPE_MAX;
}

enum rf_type trx_bmp_to_rf_type(u8 tx_bmp, u8 rx_bmp)
{
	u8 tx_num = 0;
	u8 rx_num = 0;
	int i;

	for (i = 0; i < RF_PATH_MAX; i++) {
		if (tx_bmp >> i & BIT0)
			tx_num++;
		if (rx_bmp >> i & BIT0)
			rx_num++;
	}

	return trx_num_to_rf_type(tx_num, rx_num);
}

bool rf_type_is_a_in_b(enum rf_type a, enum rf_type b)
{
	return rf_type_to_rf_tx_cnt(a) <= rf_type_to_rf_tx_cnt(b)
		&& rf_type_to_rf_rx_cnt(a) <= rf_type_to_rf_rx_cnt(b);
}

static void rtw_path_bmp_limit_from_higher(u8 *bmp, u8 *bmp_bit_cnt, u8 bit_cnt_lmt)
{
	int i;

	for (i = RF_PATH_MAX - 1; *bmp_bit_cnt > bit_cnt_lmt && i >= 0; i--) {
		if (*bmp & BIT(i)) {
			*bmp &= ~BIT(i);
			(*bmp_bit_cnt)--;
		}
	}
}

u8 rtw_restrict_trx_path_bmp_by_trx_num_lmt(u8 trx_path_bmp, u8 tx_num_lmt, u8 rx_num_lmt, u8 *tx_num, u8 *rx_num)
{
	u8 bmp_tx = (trx_path_bmp & 0xF0) >> 4;
	u8 bmp_rx = trx_path_bmp & 0x0F;
	u8 bmp_tx_num = 0, bmp_rx_num = 0;
	enum rf_type ret_type = RF_TYPE_MAX;
	int i, j;

	for (i = 0; i < RF_PATH_MAX; i++) {
		if (bmp_tx & BIT(i))
			bmp_tx_num++;
		if (bmp_rx & BIT(i))
			bmp_rx_num++;
	}

	/* limit higher bit first according to input type */
	if (tx_num_lmt)
		rtw_path_bmp_limit_from_higher(&bmp_tx, &bmp_tx_num, tx_num_lmt);
	if (rx_num_lmt)
		rtw_path_bmp_limit_from_higher(&bmp_rx, &bmp_rx_num, rx_num_lmt);

	/* search for valid rf_type (larger RX prefer) */
	for (j = bmp_rx_num; j > 0; j--) {
		for (i = bmp_tx_num; i > 0; i--) {
			ret_type = trx_num_to_rf_type(i, j);
			if (RF_TYPE_VALID(ret_type)) {
				rtw_path_bmp_limit_from_higher(&bmp_tx, &bmp_tx_num, i);
				rtw_path_bmp_limit_from_higher(&bmp_rx, &bmp_rx_num, j);
				if (tx_num)
					*tx_num = bmp_tx_num;
				if (rx_num)
					*rx_num = bmp_rx_num;
				goto exit;
			}
		}
	}

exit:
	return RF_TYPE_VALID(ret_type) ? ((bmp_tx << 4) | bmp_rx) : 0x00;
}

u8 rtw_restrict_trx_path_bmp_by_rftype(u8 trx_path_bmp, enum rf_type type, u8 *tx_num, u8 *rx_num)
{
	return rtw_restrict_trx_path_bmp_by_trx_num_lmt(trx_path_bmp
		, rf_type_to_rf_tx_cnt(type), rf_type_to_rf_rx_cnt(type), tx_num, rx_num);
}

/* config to non N-TX value, path with lower index prefer */
void tx_path_nss_set_default(enum bb_path txpath_nss[], u8 txpath_num_nss[], u8 txpath)
{
	int i, j;
	u8 cnt;

	for (i = 4; i > 0; i--) {
		cnt = 0;
		txpath_nss[i - 1] = 0;
		for (j = 0; j < RF_PATH_MAX; j++) {
			if (txpath & BIT(j)) {
				txpath_nss[i - 1] |= BIT(j);
				if (++cnt == i)
					break;
			}
		}
		txpath_num_nss[i - 1] = i;
	}
}

/* config to full N-TX value */
void tx_path_nss_set_full_tx(enum bb_path txpath_nss[], u8 txpath_num_nss[], u8 txpath)
{
	u8 tx_num = 0;
	int i;

	for (i = 0; i < RF_PATH_MAX; i++)
		if (txpath & BIT(i))
			tx_num++;

	for (i = 4; i > 0; i--) {
		txpath_nss[i - 1] = txpath;
		txpath_num_nss[i - 1] = tx_num;
	}
}

const char *const _regd_str[] = {
	"NONE",
	"FCC",
	"MKK",
	"ETSI",
	"IC",
	"KCC",
	"NCC",
	"ACMA",
	"CHILE",
	"UKRAINE",
	"MEXICO",
	"CN",
	"WW",
};

/*
* input with txpwr value in unit of txpwr index
* return string in length 6 at least (for -xx.xx)
*/
void txpwr_idx_get_dbm_str(s8 idx, u8 txgi_max, u8 txgi_pdbm, SIZE_T cwidth, char dbm_str[], u8 dbm_str_len)
{
	char fmt[16];

	if (idx == txgi_max) {
		snprintf(fmt, 16, "%%%zus", cwidth >= 6 ? cwidth + 1 : 6);
		snprintf(dbm_str, dbm_str_len, fmt, "NA");
	} else if (idx > -txgi_pdbm && idx < 0) { /* -0.xx */
		snprintf(fmt, 16, "%%%zus-0.%%02d", cwidth >= 6 ? cwidth - 4 : 1);
		snprintf(dbm_str, dbm_str_len, fmt, "", (rtw_abs(idx) % txgi_pdbm) * 100 / txgi_pdbm);
	} else if (idx % txgi_pdbm) { /* d.xx */
		snprintf(fmt, 16, "%%%zud.%%02d", cwidth >= 6 ? cwidth - 2 : 3);
		snprintf(dbm_str, dbm_str_len, fmt, idx / txgi_pdbm, (rtw_abs(idx) % txgi_pdbm) * 100 / txgi_pdbm);
	} else { /* d */
		snprintf(fmt, 16, "%%%zud", cwidth >= 6 ? cwidth + 1 : 6);
		snprintf(dbm_str, dbm_str_len, fmt, idx / txgi_pdbm);
	}
}

/*
* input with txpwr value in unit of mbm
* return string in length 6 at least (for -xx.xx)
*/
void txpwr_mbm_get_dbm_str(s16 mbm, SIZE_T cwidth, char dbm_str[], u8 dbm_str_len)
{
	char fmt[16];

	if (mbm == UNSPECIFIED_MBM) {
		snprintf(fmt, 16, "%%%zus", cwidth >= 6 ? cwidth + 1 : 6);
		snprintf(dbm_str, dbm_str_len, fmt, "NA");
	} else if (mbm > -MBM_PDBM && mbm < 0) { /* -0.xx */
		snprintf(fmt, 16, "%%%zus-0.%%02d", cwidth >= 6 ? cwidth - 4 : 1);
		snprintf(dbm_str, dbm_str_len, fmt, "", (rtw_abs(mbm) % MBM_PDBM) * 100 / MBM_PDBM);
	} else if (mbm % MBM_PDBM) { /* d.xx */
		snprintf(fmt, 16, "%%%zud.%%02d", cwidth >= 6 ? cwidth - 2 : 3);
		snprintf(dbm_str, dbm_str_len, fmt, mbm / MBM_PDBM, (rtw_abs(mbm) % MBM_PDBM) * 100 / MBM_PDBM);
	} else { /* d */
		snprintf(fmt, 16, "%%%zud", cwidth >= 6 ? cwidth + 1 : 6);
		snprintf(dbm_str, dbm_str_len, fmt, mbm / MBM_PDBM);
	}
}

static const s16 _mb_of_ntx[] = {
	0,		/* 1TX */
	301,	/* 2TX */
	477,	/* 3TX */
	602,	/* 4TX */
	699,	/* 5TX */
	778,	/* 6TX */
	845,	/* 7TX */
	903,	/* 8TX */
};

/* get mB(100 *dB) for specifc TX count relative to 1TX */
s16 mb_of_ntx(u8 ntx)
{
	if (ntx == 0 || ntx > 8) {
		RTW_ERR("ntx=%u, out of range\n", ntx);
		rtw_warn_on(1);
	}

	return _mb_of_ntx[ntx - 1];
}

#if CONFIG_TXPWR_LIMIT
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
#define TMP_STR_LEN 16
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	_irqL irqL;
	char fmt[16];
	char tmp_str[TMP_STR_LEN];
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
	#if CONFIG_IEEE80211_BAND_5GHZ
	if (IS_HARDWARE_TYPE_JAGUAR_ALL(adapter)) {
		RTW_PRINT_SEL(sel, "txpwr_lmt_5g_cck_ofdm_state:0x%02x\n", rfctl->txpwr_lmt_5g_cck_ofdm_state);
		RTW_PRINT_SEL(sel, "txpwr_lmt_5g_20_40_ref:0x%02x\n", rfctl->txpwr_lmt_5g_20_40_ref);
	}
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
				if (tlrs == TXPWR_LMT_RS_VHT && !IS_HARDWARE_TYPE_JAGUAR_ALL(adapter))
					continue;

				for (ntx_idx = RF_1TX; ntx_idx < MAX_TX_COUNT; ntx_idx++) {
					struct txpwr_lmt_ent *ent;
					_list *cur, *head;

					if (ntx_idx + 1 > hal_data->max_tx_cnt)
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
						#if CONFIG_IEEE80211_BAND_5GHZ
						if (band == BAND_ON_5G
							&& !(rfctl->txpwr_lmt_5g_cck_ofdm_state & (TXPWR_LMT_HAS_OFDM_1T << ntx_idx)))
							continue;
						#endif
					}

					/* bypass 5G 20M, 40M pure reference */
					#if CONFIG_IEEE80211_BAND_5GHZ
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

						sprintf(fmt, "%%%zus%%s ", strlen(ent->regd_name) >= 6 ? 1 : 6 - strlen(ent->regd_name));
						snprintf(tmp_str, TMP_STR_LEN, fmt
							, strcmp(ent->regd_name, rfctl->regd_name) == 0 ? "*" : ""
							, ent->regd_name);
						_RTW_PRINT_SEL(sel, "%s", tmp_str);
					}
					sprintf(fmt, "%%%zus%%s ", strlen(regd_str(TXPWR_LMT_WW)) >= 6 ? 1 : 6 - strlen(regd_str(TXPWR_LMT_WW)));
					snprintf(tmp_str, TMP_STR_LEN, fmt
						, strcmp(rfctl->regd_name, regd_str(TXPWR_LMT_WW)) == 0 ? "*" : ""
						, regd_str(TXPWR_LMT_WW));
					_RTW_PRINT_SEL(sel, "%s", tmp_str);

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

						/* dump limit in dBm */
						RTW_PRINT_SEL(sel, "%3u ", ch);
						head = &rfctl->txpwr_lmt_list;
						cur = get_next(head);
						while ((rtw_end_of_queue_search(head, cur)) == _FALSE) {
							ent = LIST_CONTAINOR(cur, struct txpwr_lmt_ent, list);
							cur = get_next(cur);
							lmt = phy_get_txpwr_lmt(adapter, ent->regd_name, band, bw, tlrs, ntx_idx, ch, 0);
							txpwr_idx_get_dbm_str(lmt, hal_spec->txgi_max, hal_spec->txgi_pdbm, strlen(ent->regd_name), tmp_str, TMP_STR_LEN);
							_RTW_PRINT_SEL(sel, "%s ", tmp_str);
						}
						lmt = phy_get_txpwr_lmt(adapter, regd_str(TXPWR_LMT_WW), band, bw, tlrs, ntx_idx, ch, 0);
						txpwr_idx_get_dbm_str(lmt, hal_spec->txgi_max, hal_spec->txgi_pdbm, strlen(regd_str(TXPWR_LMT_WW)), tmp_str, TMP_STR_LEN);
						_RTW_PRINT_SEL(sel, "%s ", tmp_str);

						/* dump limit offset of each path */
						for (path = RF_PATH_A; path < RF_PATH_MAX; path++) {
							if (path >= rfpath_num)
								break;

							base = phy_get_target_txpwr(adapter, band, path, rs);

							_RTW_PRINT_SEL(sel, "|");
							head = &rfctl->txpwr_lmt_list;
							cur = get_next(head);
							i = 0;
							while ((rtw_end_of_queue_search(head, cur)) == _FALSE) {
								ent = LIST_CONTAINOR(cur, struct txpwr_lmt_ent, list);
								cur = get_next(cur);
								lmt_offset = phy_get_txpwr_lmt_diff(adapter, ent->regd_name, band, bw, path, rs, tlrs, ntx_idx, ch, 0);
								if (lmt_offset == hal_spec->txgi_max) {
									*(lmt_idx + i * RF_PATH_MAX + path) = hal_spec->txgi_max;
									_RTW_PRINT_SEL(sel, "%3s ", "NA");
								} else {
									*(lmt_idx + i * RF_PATH_MAX + path) = lmt_offset + base;
									_RTW_PRINT_SEL(sel, "%3d ", lmt_offset);
								}
								i++;
							}
							lmt_offset = phy_get_txpwr_lmt_diff(adapter, regd_str(TXPWR_LMT_WW), band, bw, path, rs, tlrs, ntx_idx, ch, 0);
							if (lmt_offset == hal_spec->txgi_max)
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
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(dvobj_get_primary_adapter(rfctl_to_dvobj(rfctl)));
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
						ent->lmt_2g[j][k][m][l] = hal_spec->txgi_max;
		#if CONFIG_IEEE80211_BAND_5GHZ
		for (j = 0; j < MAX_5G_BANDWIDTH_NUM; ++j)
			for (k = 0; k < TXPWR_LMT_RS_NUM_5G; ++k)
				for (m = 0; m < CENTER_CH_5G_ALL_NUM; ++m)
					for (l = 0; l < MAX_TX_COUNT; ++l)
						ent->lmt_5g[j][k][m][l] = hal_spec->txgi_max;
		#endif
	}

	rtw_list_insert_tail(&ent->list, &rfctl->txpwr_lmt_list);
	rfctl->txpwr_regd_num++;

chk_lmt_val:
	if (band == BAND_ON_2_4G)
		pre_lmt = ent->lmt_2g[bw][tlrs][ch_idx][ntx_idx];
	#if CONFIG_IEEE80211_BAND_5GHZ
	else if (band == BAND_ON_5G)
		pre_lmt = ent->lmt_5g[bw][tlrs - 1][ch_idx][ntx_idx];
	#endif
	else
		goto release_lock;

	if (pre_lmt != hal_spec->txgi_max)
		RTW_PRINT("duplicate txpwr_lmt for [%s][%s][%s][%s][%uT][%d]\n"
			, regd_name, band_str(band), ch_width_str(bw), txpwr_lmt_rs_str(tlrs), ntx_idx + 1
			, band == BAND_ON_2_4G ? ch_idx + 1 : center_ch_5g_all[ch_idx]);

	lmt = rtw_min(pre_lmt, lmt);
	if (band == BAND_ON_2_4G)
		ent->lmt_2g[bw][tlrs][ch_idx][ntx_idx] = lmt;
	#if CONFIG_IEEE80211_BAND_5GHZ
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
#if CONFIG_IEEE80211_BAND_5GHZ
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
#if !defined(CONFIG_RTL8814A) && !defined(CONFIG_RTL8822B) && !defined(CONFIG_RTL8821C) && !defined(CONFIG_RTL8822C) \
    && !defined(CONFIG_RTL8723F)
	u8 write_value;
#endif
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
#ifdef CONFIG_RTL8188GTV
	case RTL8188GTV:
		write_value = RF_TX_GAIN_OFFSET_8188GTV(offset);
		rtw_hal_write_rfreg(adapter, target_path, 0x55, 0x0fc000, write_value);
		break;
#endif /* CONFIG_RTL8188GTV */
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
#if defined(CONFIG_RTL8814A) || defined(CONFIG_RTL8822B) || defined(CONFIG_RTL8821C) || defined(CONFIG_RTL8192F) || defined(CONFIG_RTL8822C) \
    || defined(CONFIG_RTL8723F)
	case RTL8814A:
	case RTL8822B:
	case RTL8822C:	
	case RTL8821C:
	case RTL8192F:
	case RTL8723F:
		RTW_INFO("\nkfree by PhyDM on the sw CH. path %d\n", path);
		break;
#endif /* CONFIG_RTL8814A || CONFIG_RTL8822B || CONFIG_RTL8821C || CONFIG_RTL8723F */

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
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	s8 kfree_offset = 0;
	s8 tx_pwr_track_offset = 0; /* TODO: 8814A should consider tx pwr track when setting tx gain offset */
	s8 total_offset;
	int i, total = 0;

	if (IS_HARDWARE_TYPE_8723D(adapter))
		total = 2; /* S1 and S0 */
	else
		total = hal_spec->rf_reg_path_num;

	for (i = 0; i < total; i++) {
		kfree_offset = rtw_rf_get_kfree_tx_gain_offset(adapter, i, ch);
		total_offset = kfree_offset + tx_pwr_track_offset;
		rtw_rf_set_tx_gain_offset(adapter, i, total_offset);
	}
}

bool rtw_is_long_cac_range(u32 hi, u32 lo, u8 dfs_region)
{
	return (dfs_region == RTW_DFS_REGD_ETSI && rtw_is_range_overlap(hi, lo, 5650, 5600)) ? _TRUE : _FALSE;
}

bool rtw_is_long_cac_ch(u8 ch, u8 bw, u8 offset, u8 dfs_region)
{
	u32 hi, lo;

	if (rtw_chbw_to_freq_range(ch, bw, offset, &hi, &lo) == _FALSE)
		return _FALSE;

	return rtw_is_long_cac_range(hi, lo, dfs_region) ? _TRUE : _FALSE;
}
