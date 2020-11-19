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

const char *const _ch_width_str[CHANNEL_WIDTH_MAX] = {
	"20MHz",
	"40MHz",
	"80MHz",
	"160MHz",
	"80_80MHz",
	"5MHz",
	"10MHz",
};

const u8 _ch_width_to_bw_cap[CHANNEL_WIDTH_MAX] = {
	BW_CAP_20M,
	BW_CAP_40M,
	BW_CAP_80M,
	BW_CAP_160M,
	BW_CAP_80_80M,
	BW_CAP_5M,
	BW_CAP_10M,
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

const char *const _regd_str[] = {
	"NONE",
	"FCC",
	"MKK",
	"ETSI",
	"IC",
	"KCC",
	"ACMA",
	"CHILE",
	"WW",
};

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
	#ifdef CONFIG_IEEE80211_BAND_5GHZ
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

						/* dump limit in db */
						RTW_PRINT_SEL(sel, "%3u ", ch);
						head = &rfctl->txpwr_lmt_list;
						cur = get_next(head);
						while ((rtw_end_of_queue_search(head, cur)) == _FALSE) {
							ent = LIST_CONTAINOR(cur, struct txpwr_lmt_ent, list);
							cur = get_next(cur);
							lmt = phy_get_txpwr_lmt_abs(adapter, ent->regd_name, band, bw, tlrs, ntx_idx, ch, 0);
							if (lmt == hal_spec->txgi_max) {
								sprintf(fmt, "%%%zus ", strlen(ent->regd_name) >= 6 ? strlen(ent->regd_name) + 1 : 6);
								snprintf(tmp_str, TMP_STR_LEN, fmt, "NA");
								_RTW_PRINT_SEL(sel, "%s", tmp_str);
							} else if (lmt > -hal_spec->txgi_pdbm && lmt < 0) { /* -0.xx */
								sprintf(fmt, "%%%zus-0.%%d ", strlen(ent->regd_name) >= 6 ? strlen(ent->regd_name) - 4 : 1);
								snprintf(tmp_str, TMP_STR_LEN, fmt, "", (rtw_abs(lmt) % hal_spec->txgi_pdbm) * 100 / hal_spec->txgi_pdbm);
								_RTW_PRINT_SEL(sel, "%s", tmp_str);
							} else if (lmt % hal_spec->txgi_pdbm) { /* d.xx */
								sprintf(fmt, "%%%zud.%%d ", strlen(ent->regd_name) >= 6 ? strlen(ent->regd_name) - 2 : 3);
								snprintf(tmp_str, TMP_STR_LEN, fmt, lmt / hal_spec->txgi_pdbm, (rtw_abs(lmt) % hal_spec->txgi_pdbm) * 100 / hal_spec->txgi_pdbm);
								_RTW_PRINT_SEL(sel, "%s", tmp_str);
							} else { /* d */
								sprintf(fmt, "%%%zud ", strlen(ent->regd_name) >= 6 ? strlen(ent->regd_name) + 1 : 6);
								snprintf(tmp_str, TMP_STR_LEN, fmt, lmt / hal_spec->txgi_pdbm);
								_RTW_PRINT_SEL(sel, "%s", tmp_str);
							}
						}
						lmt = phy_get_txpwr_lmt_abs(adapter, regd_str(TXPWR_LMT_WW), band, bw, tlrs, ntx_idx, ch, 0);
						if (lmt == hal_spec->txgi_max) {
							sprintf(fmt, "%%%zus ", strlen(regd_str(TXPWR_LMT_WW)) >= 6 ? strlen(regd_str(TXPWR_LMT_WW)) + 1 : 6);
							snprintf(tmp_str, TMP_STR_LEN, fmt, "NA");
							_RTW_PRINT_SEL(sel, "%s", tmp_str);
						} else if (lmt > -hal_spec->txgi_pdbm && lmt < 0) { /* -0.xx */
							sprintf(fmt, "%%%zus-0.%%d ", strlen(regd_str(TXPWR_LMT_WW)) >= 6 ? strlen(regd_str(TXPWR_LMT_WW)) - 4 : 1);
							snprintf(tmp_str, TMP_STR_LEN, fmt, "", (rtw_abs(lmt) % hal_spec->txgi_pdbm) * 100 / hal_spec->txgi_pdbm);
							_RTW_PRINT_SEL(sel, "%s", tmp_str);
						} else if (lmt % hal_spec->txgi_pdbm) { /* d.xx */
							sprintf(fmt, "%%%zud.%%d ", strlen(regd_str(TXPWR_LMT_WW)) >= 6 ? strlen(regd_str(TXPWR_LMT_WW)) - 2 : 3);
							snprintf(tmp_str, TMP_STR_LEN, fmt, lmt / hal_spec->txgi_pdbm, (rtw_abs(lmt) % hal_spec->txgi_pdbm) * 100 / hal_spec->txgi_pdbm);
							_RTW_PRINT_SEL(sel, "%s", tmp_str);
						} else { /* d */
							sprintf(fmt, "%%%zud ", strlen(regd_str(TXPWR_LMT_WW)) >= 6 ? strlen(regd_str(TXPWR_LMT_WW)) + 1 : 6);
							snprintf(tmp_str, TMP_STR_LEN, fmt, lmt / hal_spec->txgi_pdbm);
							_RTW_PRINT_SEL(sel, "%s", tmp_str);
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
								if (lmt_offset == hal_spec->txgi_max) {
									*(lmt_idx + i * RF_PATH_MAX + path) = hal_spec->txgi_max;
									_RTW_PRINT_SEL(sel, "%3s ", "NA");
								} else {
									*(lmt_idx + i * RF_PATH_MAX + path) = lmt_offset + base;
									_RTW_PRINT_SEL(sel, "%3d ", lmt_offset);
								}
								i++;
							}
							lmt_offset = phy_get_txpwr_lmt(adapter, regd_str(TXPWR_LMT_WW), band, bw, path, rs, ntx_idx, ch, 0);
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
		#ifdef CONFIG_IEEE80211_BAND_5GHZ
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
	#ifdef CONFIG_IEEE80211_BAND_5GHZ
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
#if !defined(CONFIG_RTL8814A) && !defined(CONFIG_RTL8822B) && !defined(CONFIG_RTL8821C) && !defined(CONFIG_RTL8822C)
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
#if defined(CONFIG_RTL8814A) || defined(CONFIG_RTL8822B) || defined(CONFIG_RTL8821C) || defined(CONFIG_RTL8192F) || defined(CONFIG_RTL8822C)
	case RTL8814A:
	case RTL8822B:
	case RTL8822C:	
	case RTL8821C:
	case RTL8192F:
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
