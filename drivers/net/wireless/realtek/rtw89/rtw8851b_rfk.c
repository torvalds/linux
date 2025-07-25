// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2022-2023  Realtek Corporation
 */

#include "coex.h"
#include "debug.h"
#include "mac.h"
#include "phy.h"
#include "reg.h"
#include "rtw8851b.h"
#include "rtw8851b_rfk.h"
#include "rtw8851b_rfk_table.h"
#include "rtw8851b_table.h"

#define DPK_VER_8851B 0x11
#define DPK_KIP_REG_NUM_8851B 8
#define DPK_RF_REG_NUM_8851B 4
#define DPK_KSET_NUM 4
#define RTW8851B_RXK_GROUP_NR 4
#define RTW8851B_RXK_GROUP_IDX_NR 2
#define RTW8851B_TXK_GROUP_NR 1
#define RTW8851B_IQK_VER 0x14
#define RTW8851B_IQK_SS 1
#define RTW8851B_LOK_GRAM 10
#define RTW8851B_TSSI_PATH_NR 1

#define _TSSI_DE_MASK GENMASK(21, 12)

enum dpk_id {
	LBK_RXIQK	= 0x06,
	SYNC		= 0x10,
	MDPK_IDL	= 0x11,
	MDPK_MPA	= 0x12,
	GAIN_LOSS	= 0x13,
	GAIN_CAL	= 0x14,
	DPK_RXAGC	= 0x15,
	KIP_PRESET	= 0x16,
	KIP_RESTORE	= 0x17,
	DPK_TXAGC	= 0x19,
	D_KIP_PRESET	= 0x28,
	D_TXAGC		= 0x29,
	D_RXAGC		= 0x2a,
	D_SYNC		= 0x2b,
	D_GAIN_LOSS	= 0x2c,
	D_MDPK_IDL	= 0x2d,
	D_MDPK_LDL	= 0x2e,
	D_GAIN_NORM	= 0x2f,
	D_KIP_THERMAL	= 0x30,
	D_KIP_RESTORE	= 0x31
};

enum dpk_agc_step {
	DPK_AGC_STEP_SYNC_DGAIN,
	DPK_AGC_STEP_GAIN_LOSS_IDX,
	DPK_AGC_STEP_GL_GT_CRITERION,
	DPK_AGC_STEP_GL_LT_CRITERION,
	DPK_AGC_STEP_SET_TX_GAIN,
};

enum rtw8851b_iqk_type {
	ID_TXAGC = 0x0,
	ID_FLOK_COARSE = 0x1,
	ID_FLOK_FINE = 0x2,
	ID_TXK = 0x3,
	ID_RXAGC = 0x4,
	ID_RXK = 0x5,
	ID_NBTXK = 0x6,
	ID_NBRXK = 0x7,
	ID_FLOK_VBUFFER = 0x8,
	ID_A_FLOK_COARSE = 0x9,
	ID_G_FLOK_COARSE = 0xa,
	ID_A_FLOK_FINE = 0xb,
	ID_G_FLOK_FINE = 0xc,
	ID_IQK_RESTORE = 0x10,
};

enum rf_mode {
	RF_SHUT_DOWN = 0x0,
	RF_STANDBY = 0x1,
	RF_TX = 0x2,
	RF_RX = 0x3,
	RF_TXIQK = 0x4,
	RF_DPK = 0x5,
	RF_RXK1 = 0x6,
	RF_RXK2 = 0x7,
};

enum adc_ck {
	ADC_NA = 0,
	ADC_480M = 1,
	ADC_960M = 2,
	ADC_1920M = 3,
};

enum dac_ck {
	DAC_40M = 0,
	DAC_80M = 1,
	DAC_120M = 2,
	DAC_160M = 3,
	DAC_240M = 4,
	DAC_320M = 5,
	DAC_480M = 6,
	DAC_960M = 7,
};

static const u32 _tssi_de_cck_long[RF_PATH_NUM_8851B] = {0x5858};
static const u32 _tssi_de_cck_short[RF_PATH_NUM_8851B] = {0x5860};
static const u32 _tssi_de_mcs_20m[RF_PATH_NUM_8851B] = {0x5838};
static const u32 _tssi_de_mcs_40m[RF_PATH_NUM_8851B] = {0x5840};
static const u32 _tssi_de_mcs_80m[RF_PATH_NUM_8851B] = {0x5848};
static const u32 _tssi_de_mcs_80m_80m[RF_PATH_NUM_8851B] = {0x5850};
static const u32 _tssi_de_mcs_5m[RF_PATH_NUM_8851B] = {0x5828};
static const u32 _tssi_de_mcs_10m[RF_PATH_NUM_8851B] = {0x5830};
static const u32 g_idxrxgain[RTW8851B_RXK_GROUP_NR] = {0x10e, 0x116, 0x28e, 0x296};
static const u32 g_idxattc2[RTW8851B_RXK_GROUP_NR] = {0x0, 0xf, 0x0, 0xf};
static const u32 g_idxrxagc[RTW8851B_RXK_GROUP_NR] = {0x0, 0x1, 0x2, 0x3};
static const u32 a_idxrxgain[RTW8851B_RXK_GROUP_IDX_NR] = {0x10C, 0x28c};
static const u32 a_idxattc2[RTW8851B_RXK_GROUP_IDX_NR] = {0xf, 0xf};
static const u32 a_idxrxagc[RTW8851B_RXK_GROUP_IDX_NR] = {0x4, 0x6};
static const u32 a_power_range[RTW8851B_TXK_GROUP_NR] = {0x0};
static const u32 a_track_range[RTW8851B_TXK_GROUP_NR] = {0x6};
static const u32 a_gain_bb[RTW8851B_TXK_GROUP_NR] = {0x0a};
static const u32 a_itqt[RTW8851B_TXK_GROUP_NR] = {0x12};
static const u32 g_power_range[RTW8851B_TXK_GROUP_NR] = {0x0};
static const u32 g_track_range[RTW8851B_TXK_GROUP_NR] = {0x6};
static const u32 g_gain_bb[RTW8851B_TXK_GROUP_NR] = {0x10};
static const u32 g_itqt[RTW8851B_TXK_GROUP_NR] = {0x12};

static const u32 rtw8851b_backup_bb_regs[] = {0xc0d4, 0xc0d8, 0xc0c4, 0xc0ec, 0xc0e8};
static const u32 rtw8851b_backup_rf_regs[] = {
	0xef, 0xde, 0x0, 0x1e, 0x2, 0x85, 0x90, 0x5};

#define BACKUP_BB_REGS_NR ARRAY_SIZE(rtw8851b_backup_bb_regs)
#define BACKUP_RF_REGS_NR ARRAY_SIZE(rtw8851b_backup_rf_regs)

static const u32 dpk_kip_reg[DPK_KIP_REG_NUM_8851B] = {
	0x813c, 0x8124, 0xc0ec, 0xc0e8, 0xc0c4, 0xc0d4, 0xc0d8, 0x12a0};
static const u32 dpk_rf_reg[DPK_RF_REG_NUM_8851B] = {0xde, 0x8f, 0x5, 0x10005};

static void _set_ch(struct rtw89_dev *rtwdev, u32 val);

static u8 _rxk_5ghz_group_from_idx(u8 idx)
{
	/* There are four RXK groups (RTW8851B_RXK_GROUP_NR), but only group 0
	 * and 2 are used in 5 GHz band, so reduce elements to 2.
	 */
	if (idx < RTW8851B_RXK_GROUP_IDX_NR)
		return idx * 2;

	return 0;
}

static u8 _kpath(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx)
{
	return RF_A;
}

static void _adc_fifo_rst(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx,
			  u8 path)
{
	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_RXK, 0x0101);
	fsleep(10);
	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_RXK, 0x1111);
}

static void _rfk_rf_direct_cntrl(struct rtw89_dev *rtwdev,
				 enum rtw89_rf_path path, bool is_bybb)
{
	if (is_bybb)
		rtw89_write_rf(rtwdev, path, RR_RSV1, RR_RSV1_RST, 0x1);
	else
		rtw89_write_rf(rtwdev, path, RR_RSV1, RR_RSV1_RST, 0x0);
}

static void _rfk_drf_direct_cntrl(struct rtw89_dev *rtwdev,
				  enum rtw89_rf_path path, bool is_bybb)
{
	if (is_bybb)
		rtw89_write_rf(rtwdev, path, RR_BBDC, RR_BBDC_SEL, 0x1);
	else
		rtw89_write_rf(rtwdev, path, RR_BBDC, RR_BBDC_SEL, 0x0);
}

static void _txck_force(struct rtw89_dev *rtwdev, enum rtw89_rf_path path,
			bool force, enum dac_ck ck)
{
	rtw89_phy_write32_mask(rtwdev, R_P0_RXCK | (path << 13), B_P0_TXCK_ON, 0x0);

	if (!force)
		return;

	rtw89_phy_write32_mask(rtwdev, R_P0_RXCK | (path << 13), B_P0_TXCK_VAL, ck);
	rtw89_phy_write32_mask(rtwdev, R_P0_RXCK | (path << 13), B_P0_TXCK_ON, 0x1);
}

static void _rxck_force(struct rtw89_dev *rtwdev, enum rtw89_rf_path path,
			bool force, enum adc_ck ck)
{
	static const u32 ck960_8851b[] = {0x8, 0x2, 0x2, 0x4, 0xf, 0xa, 0x93};
	static const u32 ck1920_8851b[] = {0x9, 0x0, 0x0, 0x3, 0xf, 0xa, 0x49};
	const u32 *data;

	rtw89_phy_write32_mask(rtwdev, R_P0_RXCK | (path << 13), B_P0_RXCK_ON, 0x0);
	if (!force)
		return;

	rtw89_phy_write32_mask(rtwdev, R_P0_RXCK | (path << 13), B_P0_RXCK_VAL, ck);
	rtw89_phy_write32_mask(rtwdev, R_P0_RXCK | (path << 13), B_P0_RXCK_ON, 0x1);

	switch (ck) {
	case ADC_960M:
		data = ck960_8851b;
		break;
	case ADC_1920M:
	default:
		data = ck1920_8851b;
		break;
	}

	rtw89_phy_write32_mask(rtwdev, R_P0_CFCH_BW0 | (path << 8), B_P0_CFCH_CTL, data[0]);
	rtw89_phy_write32_mask(rtwdev, R_P0_CFCH_BW0 | (path << 8), B_P0_CFCH_EN, data[1]);
	rtw89_phy_write32_mask(rtwdev, R_P0_CFCH_BW0 | (path << 8), B_P0_CFCH_BW0, data[2]);
	rtw89_phy_write32_mask(rtwdev, R_P0_CFCH_BW1 | (path << 8), B_P0_CFCH_BW1, data[3]);
	rtw89_phy_write32_mask(rtwdev, R_DRCK | (path << 8), B_DRCK_MUL, data[4]);
	rtw89_phy_write32_mask(rtwdev, R_ADCMOD | (path << 8), B_ADCMOD_LP, data[5]);
	rtw89_phy_write32_mask(rtwdev, R_P0_RXCK | (path << 8), B_P0_RXCK_ADJ, data[6]);
}

static void _wait_rx_mode(struct rtw89_dev *rtwdev, u8 kpath)
{
	u32 rf_mode;
	u8 path;
	int ret;

	for (path = 0; path < RF_PATH_MAX; path++) {
		if (!(kpath & BIT(path)))
			continue;

		ret = read_poll_timeout_atomic(rtw89_read_rf, rf_mode,
					       rf_mode != 2, 2, 5000, false,
					       rtwdev, path, 0x00, RR_MOD_MASK);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[RFK] Wait S%d to Rx mode!! (ret = %d)\n",
			    path, ret);
	}
}

static void _dack_reset(struct rtw89_dev *rtwdev, enum rtw89_rf_path path)
{
	rtw89_phy_write32_mask(rtwdev, R_DCOF0, B_DCOF0_RST, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_DCOF0, B_DCOF0_RST, 0x1);
}

static void _drck(struct rtw89_dev *rtwdev)
{
	u32 rck_d;
	u32 val;
	int ret;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]Ddie RCK start!!!\n");

	rtw89_phy_write32_mask(rtwdev, R_DRCK, B_DRCK_IDLE, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_DRCK, B_DRCK_EN, 0x1);

	ret = read_poll_timeout_atomic(rtw89_phy_read32_mask, val, val,
				       1, 10000, false,
				       rtwdev, R_DRCK_RES, B_DRCK_POL);
	if (ret)
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]DRCK timeout\n");

	rtw89_phy_write32_mask(rtwdev, R_DRCK, B_DRCK_EN, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_DRCK_FH, B_DRCK_LAT, 0x1);
	udelay(1);
	rtw89_phy_write32_mask(rtwdev, R_DRCK_FH, B_DRCK_LAT, 0x0);

	rck_d = rtw89_phy_read32_mask(rtwdev, R_DRCK_RES, 0x7c00);
	rtw89_phy_write32_mask(rtwdev, R_DRCK, B_DRCK_IDLE, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_DRCK, B_DRCK_VAL, rck_d);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]0xc0c4 = 0x%x\n",
		    rtw89_phy_read32_mask(rtwdev, R_DRCK, MASKDWORD));
}

static void _addck_backup(struct rtw89_dev *rtwdev)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;

	rtw89_phy_write32_mask(rtwdev, R_ADDCK0, B_ADDCK0, 0x0);

	dack->addck_d[0][0] = rtw89_phy_read32_mask(rtwdev, R_ADDCKR0, B_ADDCKR0_A0);
	dack->addck_d[0][1] = rtw89_phy_read32_mask(rtwdev, R_ADDCKR0, B_ADDCKR0_A1);
}

static void _addck_reload(struct rtw89_dev *rtwdev)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;

	rtw89_phy_write32_mask(rtwdev, R_ADDCK0_RL, B_ADDCK0_RL1, dack->addck_d[0][0]);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK0_RL, B_ADDCK0_RL0, dack->addck_d[0][1]);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK0_RL, B_ADDCK0_RLS, 0x3);
}

static void _dack_backup_s0(struct rtw89_dev *rtwdev)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;
	u8 i;

	rtw89_phy_write32_mask(rtwdev, R_P0_NRBW, B_P0_NRBW_DBG, 0x1);

	for (i = 0; i < RTW89_DACK_MSBK_NR; i++) {
		rtw89_phy_write32_mask(rtwdev, R_DCOF0, B_DCOF0_V, i);
		dack->msbk_d[0][0][i] =
			rtw89_phy_read32_mask(rtwdev, R_DACK_S0P2, B_DACK_S0M0);

		rtw89_phy_write32_mask(rtwdev, R_DCOF8, B_DCOF8_V, i);
		dack->msbk_d[0][1][i] =
			rtw89_phy_read32_mask(rtwdev, R_DACK_S0P3, B_DACK_S0M1);
	}

	dack->biask_d[0][0] =
		rtw89_phy_read32_mask(rtwdev, R_DACK_BIAS00, B_DACK_BIAS00);
	dack->biask_d[0][1] =
		rtw89_phy_read32_mask(rtwdev, R_DACK_BIAS01, B_DACK_BIAS01);
	dack->dadck_d[0][0] =
		rtw89_phy_read32_mask(rtwdev, R_DACK_DADCK00, B_DACK_DADCK00) + 24;
	dack->dadck_d[0][1] =
		rtw89_phy_read32_mask(rtwdev, R_DACK_DADCK01, B_DACK_DADCK01) + 24;
}

static void _dack_reload_by_path(struct rtw89_dev *rtwdev,
				 enum rtw89_rf_path path, u8 index)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;
	u32 idx_offset, path_offset;
	u32 offset, reg;
	u32 tmp;
	u8 i;

	if (index == 0)
		idx_offset = 0;
	else
		idx_offset = 0x14;

	if (path == RF_PATH_A)
		path_offset = 0;
	else
		path_offset = 0x28;

	offset = idx_offset + path_offset;

	rtw89_phy_write32_mask(rtwdev, R_DCOF1, B_DCOF1_RST, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_DCOF9, B_DCOF9_RST, 0x1);

	/* msbk_d: 15/14/13/12 */
	tmp = 0x0;
	for (i = 0; i < 4; i++)
		tmp |= dack->msbk_d[path][index][i + 12] << (i * 8);
	reg = 0xc200 + offset;
	rtw89_phy_write32(rtwdev, reg, tmp);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]0x%x=0x%x\n", reg,
		    rtw89_phy_read32_mask(rtwdev, reg, MASKDWORD));

	/* msbk_d: 11/10/9/8 */
	tmp = 0x0;
	for (i = 0; i < 4; i++)
		tmp |= dack->msbk_d[path][index][i + 8] << (i * 8);
	reg = 0xc204 + offset;
	rtw89_phy_write32(rtwdev, reg, tmp);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]0x%x=0x%x\n", reg,
		    rtw89_phy_read32_mask(rtwdev, reg, MASKDWORD));

	/* msbk_d: 7/6/5/4 */
	tmp = 0x0;
	for (i = 0; i < 4; i++)
		tmp |= dack->msbk_d[path][index][i + 4] << (i * 8);
	reg = 0xc208 + offset;
	rtw89_phy_write32(rtwdev, reg, tmp);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]0x%x=0x%x\n", reg,
		    rtw89_phy_read32_mask(rtwdev, reg, MASKDWORD));

	/* msbk_d: 3/2/1/0 */
	tmp = 0x0;
	for (i = 0; i < 4; i++)
		tmp |= dack->msbk_d[path][index][i] << (i * 8);
	reg = 0xc20c + offset;
	rtw89_phy_write32(rtwdev, reg, tmp);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]0x%x=0x%x\n", reg,
		    rtw89_phy_read32_mask(rtwdev, reg, MASKDWORD));

	/* dadak_d/biask_d */
	tmp = 0x0;
	tmp = (dack->biask_d[path][index] << 22) |
	      (dack->dadck_d[path][index] << 14);
	reg = 0xc210 + offset;
	rtw89_phy_write32(rtwdev, reg, tmp);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]0x%x=0x%x\n", reg,
		    rtw89_phy_read32_mask(rtwdev, reg, MASKDWORD));

	rtw89_phy_write32_mask(rtwdev, R_DACKN0_CTL + offset, B_DACKN0_EN, 0x1);
}

static void _dack_reload(struct rtw89_dev *rtwdev, enum rtw89_rf_path path)
{
	u8 index;

	for (index = 0; index < 2; index++)
		_dack_reload_by_path(rtwdev, path, index);
}

static void _addck(struct rtw89_dev *rtwdev)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;
	u32 val;
	int ret;

	rtw89_phy_write32_mask(rtwdev, R_ADDCK0, B_ADDCK0_RST, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK0, B_ADDCK0_EN, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK0, B_ADDCK0_EN, 0x0);
	udelay(1);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK0, B_ADDCK0, 0x1);

	ret = read_poll_timeout_atomic(rtw89_phy_read32_mask, val, val,
				       1, 10000, false,
				       rtwdev, R_ADDCKR0, BIT(0));
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S0 ADDCK timeout\n");
		dack->addck_timeout[0] = true;
	}

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]ADDCK ret = %d\n", ret);

	rtw89_phy_write32_mask(rtwdev, R_ADDCK0, B_ADDCK0_RST, 0x0);
}

static void _new_dadck(struct rtw89_dev *rtwdev)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;
	u32 i_dc, q_dc, ic, qc;
	u32 val;
	int ret;

	rtw89_rfk_parser(rtwdev, &rtw8851b_dadck_setup_defs_tbl);

	ret = read_poll_timeout_atomic(rtw89_phy_read32_mask, val, val,
				       1, 10000, false,
				       rtwdev, R_ADDCKR0, BIT(0));
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S0 DADCK timeout\n");
		dack->addck_timeout[0] = true;
	}

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]DADCK ret = %d\n", ret);

	rtw89_phy_write32_mask(rtwdev, R_ADDCK0, B_ADDCK0_IQ, 0x0);
	i_dc = rtw89_phy_read32_mask(rtwdev, R_ADDCKR0, B_ADDCKR0_DC);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK0, B_ADDCK0_IQ, 0x1);
	q_dc = rtw89_phy_read32_mask(rtwdev, R_ADDCKR0, B_ADDCKR0_DC);

	ic = 0x80 - sign_extend32(i_dc, 11) * 6;
	qc = 0x80 - sign_extend32(q_dc, 11) * 6;

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DACK]before DADCK, i_dc=0x%x, q_dc=0x%x\n", i_dc, q_dc);

	dack->dadck_d[0][0] = ic;
	dack->dadck_d[0][1] = qc;

	rtw89_phy_write32_mask(rtwdev, R_DACKN0_CTL, B_DACKN0_V, dack->dadck_d[0][0]);
	rtw89_phy_write32_mask(rtwdev, R_DACKN1_CTL, B_DACKN1_V, dack->dadck_d[0][1]);
	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DACK]after DADCK, 0xc210=0x%x, 0xc224=0x%x\n",
		    rtw89_phy_read32_mask(rtwdev, R_DACKN0_CTL, MASKDWORD),
		    rtw89_phy_read32_mask(rtwdev, R_DACKN1_CTL, MASKDWORD));

	rtw89_rfk_parser(rtwdev, &rtw8851b_dadck_post_defs_tbl);
}

static bool _dack_s0_poll(struct rtw89_dev *rtwdev)
{
	if (rtw89_phy_read32_mask(rtwdev, R_DACK_S0P0, B_DACK_S0P0_OK) == 0 ||
	    rtw89_phy_read32_mask(rtwdev, R_DACK_S0P1, B_DACK_S0P1_OK) == 0 ||
	    rtw89_phy_read32_mask(rtwdev, R_DACK_S0P2, B_DACK_S0P2_OK) == 0 ||
	    rtw89_phy_read32_mask(rtwdev, R_DACK_S0P3, B_DACK_S0P3_OK) == 0)
		return false;

	return true;
}

static void _dack_s0(struct rtw89_dev *rtwdev)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;
	bool done;
	int ret;

	rtw89_rfk_parser(rtwdev, &rtw8851b_dack_s0_1_defs_tbl);
	_dack_reset(rtwdev, RF_PATH_A);
	rtw89_phy_write32_mask(rtwdev, R_DCOF1, B_DCOF1_S, 0x1);

	ret = read_poll_timeout_atomic(_dack_s0_poll, done, done,
				       1, 10000, false, rtwdev);
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S0 DACK timeout\n");
		dack->msbk_timeout[0] = true;
	}

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]DACK ret = %d\n", ret);

	rtw89_rfk_parser(rtwdev, &rtw8851b_dack_s0_2_defs_tbl);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]after S0 DADCK\n");

	_dack_backup_s0(rtwdev);
	_dack_reload(rtwdev, RF_PATH_A);

	rtw89_phy_write32_mask(rtwdev, R_P0_NRBW, B_P0_NRBW_DBG, 0x0);
}

static void _dack(struct rtw89_dev *rtwdev)
{
	_dack_s0(rtwdev);
}

static void _dack_dump(struct rtw89_dev *rtwdev)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;
	u8 i;
	u8 t;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S0 ADC_DCK ic = 0x%x, qc = 0x%x\n",
		    dack->addck_d[0][0], dack->addck_d[0][1]);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S0 DAC_DCK ic = 0x%x, qc = 0x%x\n",
		    dack->dadck_d[0][0], dack->dadck_d[0][1]);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S0 biask ic = 0x%x, qc = 0x%x\n",
		    dack->biask_d[0][0], dack->biask_d[0][1]);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S0 MSBK ic:\n");
	for (i = 0; i < RTW89_DACK_MSBK_NR; i++) {
		t = dack->msbk_d[0][0][i];
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]0x%x\n", t);
	}

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S0 MSBK qc:\n");
	for (i = 0; i < RTW89_DACK_MSBK_NR; i++) {
		t = dack->msbk_d[0][1][i];
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]0x%x\n", t);
	}
}

static void _dack_manual_off(struct rtw89_dev *rtwdev)
{
	rtw89_rfk_parser(rtwdev, &rtw8851b_dack_manual_off_defs_tbl);
}

static void _dac_cal(struct rtw89_dev *rtwdev, bool force)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;
	u32 rf0_0;

	dack->dack_done = false;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]DACK 0x2\n");
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]DACK start!!!\n");
	rf0_0 = rtw89_read_rf(rtwdev, RF_PATH_A, RR_MOD, RFREG_MASK);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]RF0=0x%x\n", rf0_0);

	_drck(rtwdev);
	_dack_manual_off(rtwdev);
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_MOD, RFREG_MASK, 0x337e1);
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_RSV1, RR_RSV1_RST, 0x0);

	_addck(rtwdev);
	_addck_backup(rtwdev);
	_addck_reload(rtwdev);
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_MOD, RFREG_MASK, 0x40001);

	_dack(rtwdev);
	_new_dadck(rtwdev);
	_dack_dump(rtwdev);
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_RSV1, RR_RSV1_RST, 0x1);

	dack->dack_done = true;
	dack->dack_cnt++;
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]DACK finish!!!\n");
}

static void _rx_dck_info(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			 enum rtw89_rf_path path, bool is_afe,
			 enum rtw89_chanctx_idx chanctx_idx)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, chanctx_idx);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[RX_DCK] ==== S%d RX DCK (%s / CH%d / %s / by %s)====\n", path,
		    chan->band_type == RTW89_BAND_2G ? "2G" :
		    chan->band_type == RTW89_BAND_5G ? "5G" : "6G",
		    chan->channel,
		    chan->band_width == RTW89_CHANNEL_WIDTH_20 ? "20M" :
		    chan->band_width == RTW89_CHANNEL_WIDTH_40 ? "40M" : "80M",
		    is_afe ? "AFE" : "RFC");
}

static void _rxbb_ofst_swap(struct rtw89_dev *rtwdev, enum rtw89_rf_path path, u8 rf_mode)
{
	u32 val, val_i, val_q;

	val_i = rtw89_read_rf(rtwdev, path, RR_DCK, RR_DCK_S1);
	val_q = rtw89_read_rf(rtwdev, path, RR_DCK1, RR_DCK1_S1);

	val = val_q << 4 | val_i;

	rtw89_write_rf(rtwdev, path, RR_LUTWE2, RR_LUTWE2_DIS, 0x1);
	rtw89_write_rf(rtwdev, path, RR_LUTWA, RFREG_MASK, rf_mode);
	rtw89_write_rf(rtwdev, path, RR_LUTWD0, RFREG_MASK, val);
	rtw89_write_rf(rtwdev, path, RR_LUTWE2, RR_LUTWE2_DIS, 0x0);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[RX_DCK] val_i = 0x%x, val_q = 0x%x, 0x3F = 0x%x\n",
		    val_i, val_q, val);
}

static void _set_rx_dck(struct rtw89_dev *rtwdev, enum rtw89_rf_path path, u8 rf_mode)
{
	u32 val;
	int ret;

	rtw89_write_rf(rtwdev, path, RR_DCK, RR_DCK_LV, 0x0);
	rtw89_write_rf(rtwdev, path, RR_DCK, RR_DCK_LV, 0x1);

	ret = read_poll_timeout_atomic(rtw89_read_rf, val, val,
				       2, 2000, false,
				       rtwdev, path, RR_DCK, BIT(8));

	rtw89_write_rf(rtwdev, path, RR_DCK, RR_DCK_LV, 0x0);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[RX_DCK] S%d RXDCK finish (ret = %d)\n",
		    path, ret);

	_rxbb_ofst_swap(rtwdev, path, rf_mode);
}

static void _rx_dck(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy, bool is_afe,
		    enum rtw89_chanctx_idx chanctx_idx)
{
	u32 rf_reg5;
	u8 path;

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[RX_DCK] ****** RXDCK Start (Ver: 0x%x, Cv: %d) ******\n",
		    0x2, rtwdev->hal.cv);

	for (path = 0; path < RF_PATH_NUM_8851B; path++) {
		_rx_dck_info(rtwdev, phy, path, is_afe, chanctx_idx);

		rf_reg5 = rtw89_read_rf(rtwdev, path, RR_RSV1, RFREG_MASK);

		if (rtwdev->is_tssi_mode[path])
			rtw89_phy_write32_mask(rtwdev,
					       R_P0_TSSI_TRK + (path << 13),
					       B_P0_TSSI_TRK_EN, 0x1);

		rtw89_write_rf(rtwdev, path, RR_RSV1, RR_RSV1_RST, 0x0);
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_MASK, RF_RX);
		_set_rx_dck(rtwdev, path, RF_RX);
		rtw89_write_rf(rtwdev, path, RR_RSV1, RFREG_MASK, rf_reg5);

		if (rtwdev->is_tssi_mode[path])
			rtw89_phy_write32_mask(rtwdev,
					       R_P0_TSSI_TRK + (path << 13),
					       B_P0_TSSI_TRK_EN, 0x0);
	}
}

static void _iqk_sram(struct rtw89_dev *rtwdev, u8 path)
{
	u32 i;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]===>%s\n", __func__);

	rtw89_phy_write32_mask(rtwdev, R_KIP_RPT1, MASKDWORD, 0x00020000);
	rtw89_phy_write32_mask(rtwdev, R_MDPK_RX_DCK, MASKDWORD, 0x80000000);
	rtw89_phy_write32_mask(rtwdev, R_SRAM_IQRX2, MASKDWORD, 0x00000080);
	rtw89_phy_write32_mask(rtwdev, R_SRAM_IQRX, MASKDWORD, 0x00010000);
	rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_TXT, 0x009);

	for (i = 0; i <= 0x9f; i++) {
		rtw89_phy_write32_mask(rtwdev, R_SRAM_IQRX, MASKDWORD,
				       0x00010000 + i);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]0x%x\n",
			    rtw89_phy_read32_mask(rtwdev, R_RPT_COM, B_PRT_COM_DCI));
	}

	for (i = 0; i <= 0x9f; i++) {
		rtw89_phy_write32_mask(rtwdev, R_SRAM_IQRX, MASKDWORD,
				       0x00010000 + i);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]0x%x\n",
			    rtw89_phy_read32_mask(rtwdev, R_RPT_COM, B_PRT_COM_DCQ));
	}

	rtw89_phy_write32_mask(rtwdev, R_SRAM_IQRX2, MASKDWORD, 0x00000000);
	rtw89_phy_write32_mask(rtwdev, R_SRAM_IQRX, MASKDWORD, 0x00000000);
}

static void _iqk_rxk_setting(struct rtw89_dev *rtwdev, u8 path)
{
	rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_MASK, 0xc);
	rtw89_write_rf(rtwdev, path, RR_RXKPLL, RR_RXKPLL_POW, 0x0);
	rtw89_write_rf(rtwdev, path, RR_RXKPLL, RR_RXKPLL_POW, 0x1);
}

static bool _iqk_check_cal(struct rtw89_dev *rtwdev, u8 path)
{
	bool fail1 = false, fail2 = false;
	u32 val;
	int ret;

	ret = read_poll_timeout_atomic(rtw89_phy_read32_mask, val, val == 0x55,
				       10, 8200, false,
				       rtwdev, 0xbff8, MASKBYTE0);
	if (ret) {
		fail1 = true;
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK]NCTL1 IQK timeout!!!\n");
	}

	fsleep(10);

	ret = read_poll_timeout_atomic(rtw89_phy_read32_mask, val, val == 0x8000,
				       10, 200, false,
				       rtwdev, R_RPT_COM, B_RPT_COM_RDY);
	if (ret) {
		fail2 = true;
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK]NCTL2 IQK timeout!!!\n");
	}

	fsleep(10);
	rtw89_phy_write32_mask(rtwdev, R_NCTL_N1, MASKBYTE0, 0x0);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[IQK]S%x, ret = %d, notready = %x fail=%d,%d\n",
		    path, ret, fail1 || fail2, fail1, fail2);

	return fail1 || fail2;
}

static bool _iqk_one_shot(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx,
			  u8 path, u8 ktype)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	bool notready;
	u32 iqk_cmd;

	switch (ktype) {
	case ID_A_FLOK_COARSE:
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK]============ S%d ID_A_FLOK_COARSE ============\n", path);
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK, B_IQK_RFC_ON, 0x1);
		iqk_cmd = 0x108 | (1 << (4 + path));
		break;
	case ID_G_FLOK_COARSE:
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK]============ S%d ID_G_FLOK_COARSE ============\n", path);
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK, B_IQK_RFC_ON, 0x1);
		iqk_cmd = 0x108 | (1 << (4 + path));
		break;
	case ID_A_FLOK_FINE:
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK]============ S%d ID_A_FLOK_FINE ============\n", path);
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK, B_IQK_RFC_ON, 0x1);
		iqk_cmd = 0x308 | (1 << (4 + path));
		break;
	case ID_G_FLOK_FINE:
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK]============ S%d ID_G_FLOK_FINE ============\n", path);
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK, B_IQK_RFC_ON, 0x1);
		iqk_cmd = 0x308 | (1 << (4 + path));
		break;
	case ID_TXK:
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK]============ S%d ID_TXK ============\n", path);
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK, B_IQK_RFC_ON, 0x0);
		iqk_cmd = 0x008 | (1 << (path + 4)) |
			  (((0x8 + iqk_info->iqk_bw[path]) & 0xf) << 8);
		break;
	case ID_RXAGC:
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK]============ S%d ID_RXAGC ============\n", path);
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK, B_IQK_RFC_ON, 0x1);
		iqk_cmd = 0x708 | (1 << (4 + path)) | (path << 1);
		break;
	case ID_RXK:
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK]============ S%d ID_RXK ============\n", path);
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK, B_IQK_RFC_ON, 0x1);
		iqk_cmd = 0x008 | (1 << (path + 4)) |
			  (((0xc + iqk_info->iqk_bw[path]) & 0xf) << 8);
		break;
	case ID_NBTXK:
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK]============ S%d ID_NBTXK ============\n", path);
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK, B_IQK_RFC_ON, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_TXT,
				       0x00b);
		iqk_cmd = 0x408 | (1 << (4 + path));
		break;
	case ID_NBRXK:
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK]============ S%d ID_NBRXK ============\n", path);
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK, B_IQK_RFC_ON, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_RXT,
				       0x011);
		iqk_cmd = 0x608 | (1 << (4 + path));
		break;
	default:
		return false;
	}

	rtw89_phy_write32_mask(rtwdev, R_NCTL_CFG, MASKDWORD, iqk_cmd + 1);
	notready = _iqk_check_cal(rtwdev, path);
	if (iqk_info->iqk_sram_en &&
	    (ktype == ID_NBRXK || ktype == ID_RXK))
		_iqk_sram(rtwdev, path);

	rtw89_phy_write32_mask(rtwdev, R_UPD_CLK, B_IQK_RFC_ON, 0x0);
	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[IQK]S%x, ktype= %x, id = %x, notready = %x\n",
		    path, ktype, iqk_cmd + 1, notready);

	return notready;
}

static bool _rxk_2g_group_sel(struct rtw89_dev *rtwdev,
			      enum rtw89_phy_idx phy_idx, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	bool kfail = false;
	bool notready;
	u32 rf_0;
	u8 gp;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]===>%s\n", __func__);

	for (gp = 0; gp < RTW8851B_RXK_GROUP_NR; gp++) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, gp = %x\n", path, gp);

		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_RGM, g_idxrxgain[gp]);
		rtw89_write_rf(rtwdev, path, RR_RXBB, RR_RXBB_C2, g_idxattc2[gp]);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT, B_CFIR_LUT_SEL, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT, B_CFIR_LUT_G3, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT, B_CFIR_LUT_GP_V1, gp);

		rtw89_write_rf(rtwdev, path, RR_RXKPLL, RFREG_MASK, 0x80013);
		fsleep(10);
		rf_0 = rtw89_read_rf(rtwdev, path, RR_MOD, RFREG_MASK);
		rtw89_phy_write32_mask(rtwdev, R_IQK_DIF2, B_IQK_DIF2_RXPI, rf_0);
		rtw89_phy_write32_mask(rtwdev, R_IQK_RXA, B_IQK_RXAGC, g_idxrxagc[gp]);
		rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_RXT, 0x11);

		notready = _iqk_one_shot(rtwdev, phy_idx, path, ID_RXAGC);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK]S%x, RXAGC 0x8008 = 0x%x, rxbb = %x\n", path,
			    rtw89_phy_read32_mask(rtwdev, R_NCTL_RPT, MASKDWORD),
			    rtw89_read_rf(rtwdev, path, RR_MOD, 0x003e0));

		rtw89_write_rf(rtwdev, path, RR_RXKPLL, RR_RXKPLL_OFF, 0x13);
		rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_RXT, 0x011);
		notready = _iqk_one_shot(rtwdev, phy_idx, path, ID_NBRXK);
		iqk_info->nb_rxcfir[path] =
			rtw89_phy_read32_mask(rtwdev, R_RXIQC, MASKDWORD) | 0x2;

		notready = _iqk_one_shot(rtwdev, phy_idx, path, ID_RXK);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK]S%x, WBRXK 0x8008 = 0x%x\n", path,
			    rtw89_phy_read32_mask(rtwdev, R_NCTL_RPT, MASKDWORD));
	}

	if (!notready)
		kfail = !!rtw89_phy_read32_mask(rtwdev, R_NCTL_RPT, B_NCTL_RPT_FLG);

	if (kfail)
		_iqk_sram(rtwdev, path);

	if (kfail) {
		rtw89_phy_write32_mask(rtwdev, R_RXIQC + (path << 8),
				       MASKDWORD, iqk_info->nb_rxcfir[path] | 0x2);
		iqk_info->is_wb_txiqk[path] = false;
	} else {
		rtw89_phy_write32_mask(rtwdev, R_RXIQC + (path << 8),
				       MASKDWORD, 0x40000000);
		iqk_info->is_wb_txiqk[path] = true;
	}

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[IQK]S%x, kfail = 0x%x, 0x8%x3c = 0x%x\n", path, kfail,
		    1 << path, iqk_info->nb_rxcfir[path]);
	return kfail;
}

static bool _rxk_5g_group_sel(struct rtw89_dev *rtwdev,
			      enum rtw89_phy_idx phy_idx, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	bool kfail = false;
	bool notready;
	u32 rf_0;
	u8 idx;
	u8 gp;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]===>%s\n", __func__);

	for (idx = 0; idx < RTW8851B_RXK_GROUP_IDX_NR; idx++) {
		gp = _rxk_5ghz_group_from_idx(idx);

		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, gp = %x\n", path, gp);

		rtw89_write_rf(rtwdev, RF_PATH_A, RR_MOD, RR_MOD_RGM, a_idxrxgain[idx]);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_RXA2, RR_RXA2_ATT, a_idxattc2[idx]);

		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT, B_CFIR_LUT_SEL, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT, B_CFIR_LUT_G3, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT, B_CFIR_LUT_GP_V1, gp);

		rtw89_write_rf(rtwdev, path, RR_RXKPLL, RFREG_MASK, 0x80013);
		fsleep(100);
		rf_0 = rtw89_read_rf(rtwdev, path, RR_MOD, RFREG_MASK);
		rtw89_phy_write32_mask(rtwdev, R_IQK_DIF2, B_IQK_DIF2_RXPI, rf_0);
		rtw89_phy_write32_mask(rtwdev, R_IQK_RXA, B_IQK_RXAGC, a_idxrxagc[idx]);
		rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_RXT, 0x11);
		notready = _iqk_one_shot(rtwdev, phy_idx, path, ID_RXAGC);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK]S%x, RXAGC 0x8008 = 0x%x, rxbb = %x\n", path,
			    rtw89_phy_read32_mask(rtwdev, R_NCTL_RPT, MASKDWORD),
			    rtw89_read_rf(rtwdev, path, RR_MOD, RR_MOD_RXB));

		rtw89_write_rf(rtwdev, path, RR_RXKPLL, RR_RXKPLL_OFF, 0x13);
		rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_RXT, 0x011);
		notready = _iqk_one_shot(rtwdev, phy_idx, path, ID_NBRXK);
		iqk_info->nb_rxcfir[path] =
			rtw89_phy_read32_mask(rtwdev, R_RXIQC, MASKDWORD) | 0x2;

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK]S%x, NBRXK 0x8008 = 0x%x\n", path,
			    rtw89_phy_read32_mask(rtwdev, R_NCTL_RPT, MASKDWORD));

		notready = _iqk_one_shot(rtwdev, phy_idx, path, ID_RXK);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK]S%x, WBRXK 0x8008 = 0x%x\n", path,
			    rtw89_phy_read32_mask(rtwdev, R_NCTL_RPT, MASKDWORD));
	}

	if (!notready)
		kfail = !!rtw89_phy_read32_mask(rtwdev, R_NCTL_RPT, B_NCTL_RPT_FLG);

	if (kfail)
		_iqk_sram(rtwdev, path);

	if (kfail) {
		rtw89_phy_write32_mask(rtwdev, R_RXIQC + (path << 8), MASKDWORD,
				       iqk_info->nb_rxcfir[path] | 0x2);
		iqk_info->is_wb_txiqk[path] = false;
	} else {
		rtw89_phy_write32_mask(rtwdev, R_RXIQC + (path << 8), MASKDWORD,
				       0x40000000);
		iqk_info->is_wb_txiqk[path] = true;
	}

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[IQK]S%x, kfail = 0x%x, 0x8%x3c = 0x%x\n", path, kfail,
		    1 << path, iqk_info->nb_rxcfir[path]);
	return kfail;
}

static bool _iqk_5g_nbrxk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx,
			  u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	bool kfail = false;
	bool notready;
	u8 idx = 0x1;
	u32 rf_0;
	u8 gp;

	gp = _rxk_5ghz_group_from_idx(idx);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]===>%s\n", __func__);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, gp = %x\n", path, gp);

	rtw89_write_rf(rtwdev, RF_PATH_A, RR_MOD, RR_MOD_RGM, a_idxrxgain[idx]);
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_RXA2, RR_RXA2_ATT, a_idxattc2[idx]);

	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT, B_CFIR_LUT_SEL, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT, B_CFIR_LUT_G3, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT, B_CFIR_LUT_GP_V1, gp);

	rtw89_write_rf(rtwdev, path, RR_RXKPLL, RFREG_MASK, 0x80013);
	fsleep(100);
	rf_0 = rtw89_read_rf(rtwdev, path, RR_MOD, RFREG_MASK);
	rtw89_phy_write32_mask(rtwdev, R_IQK_DIF2, B_IQK_DIF2_RXPI, rf_0);
	rtw89_phy_write32_mask(rtwdev, R_IQK_RXA, B_IQK_RXAGC, a_idxrxagc[idx]);
	rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_RXT, 0x11);
	notready = _iqk_one_shot(rtwdev, phy_idx, path, ID_RXAGC);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[IQK]S%x, RXAGC 0x8008 = 0x%x, rxbb = %x\n", path,
		    rtw89_phy_read32_mask(rtwdev, R_NCTL_RPT, MASKDWORD),
		    rtw89_read_rf(rtwdev, path, RR_MOD, 0x003e0));

	rtw89_write_rf(rtwdev, path, RR_RXKPLL, RR_RXKPLL_OFF, 0x13);
	rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_RXT, 0x011);
	notready = _iqk_one_shot(rtwdev, phy_idx, path, ID_NBRXK);
	iqk_info->nb_rxcfir[path] =
		rtw89_phy_read32_mask(rtwdev, R_RXIQC, MASKDWORD) | 0x2;

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[IQK]S%x, NBRXK 0x8008 = 0x%x\n", path,
		    rtw89_phy_read32_mask(rtwdev, R_NCTL_RPT, MASKDWORD));

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, WBRXK 0x8008 = 0x%x\n",
		    path, rtw89_phy_read32_mask(rtwdev, R_NCTL_RPT, MASKDWORD));

	if (!notready)
		kfail = !!rtw89_phy_read32_mask(rtwdev, R_NCTL_RPT, B_NCTL_RPT_FLG);

	if (kfail) {
		rtw89_phy_write32_mask(rtwdev, R_RXIQC + (path << 8),
				       MASKDWORD, 0x40000002);
		iqk_info->is_wb_rxiqk[path] = false;
	} else {
		iqk_info->is_wb_rxiqk[path] = false;
	}

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[IQK]S%x, kfail = 0x%x, 0x8%x3c = 0x%x\n", path, kfail,
		    1 << path, iqk_info->nb_rxcfir[path]);

	return kfail;
}

static bool _iqk_2g_nbrxk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx,
			  u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	bool kfail = false;
	bool notready;
	u8 gp = 0x3;
	u32 rf_0;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]===>%s\n", __func__);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, gp = %x\n", path, gp);

	rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_RGM, g_idxrxgain[gp]);
	rtw89_write_rf(rtwdev, path, RR_RXBB, RR_RXBB_C2, g_idxattc2[gp]);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT, B_CFIR_LUT_SEL, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT, B_CFIR_LUT_G3, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT, B_CFIR_LUT_GP_V1, gp);

	rtw89_write_rf(rtwdev, path, RR_RXKPLL, RFREG_MASK, 0x80013);
	fsleep(10);
	rf_0 = rtw89_read_rf(rtwdev, path, RR_MOD, RFREG_MASK);
	rtw89_phy_write32_mask(rtwdev, R_IQK_DIF2, B_IQK_DIF2_RXPI, rf_0);
	rtw89_phy_write32_mask(rtwdev, R_IQK_RXA, B_IQK_RXAGC, g_idxrxagc[gp]);
	rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_RXT, 0x11);
	notready = _iqk_one_shot(rtwdev, phy_idx, path, ID_RXAGC);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[IQK]S%x, RXAGC 0x8008 = 0x%x, rxbb = %x\n",
		    path, rtw89_phy_read32_mask(rtwdev, R_NCTL_RPT, MASKDWORD),
		    rtw89_read_rf(rtwdev, path, RR_MOD, 0x003e0));

	rtw89_write_rf(rtwdev, path, RR_RXKPLL, RR_RXKPLL_OFF, 0x13);
	rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_RXT, 0x011);
	notready = _iqk_one_shot(rtwdev, phy_idx, path, ID_NBRXK);
	iqk_info->nb_rxcfir[path] =
		rtw89_phy_read32_mask(rtwdev, R_RXIQC, MASKDWORD) | 0x2;

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[IQK]S%x, NBRXK 0x8008 = 0x%x\n", path,
		    rtw89_phy_read32_mask(rtwdev, R_NCTL_RPT, MASKDWORD));

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, WBRXK 0x8008 = 0x%x\n",
		    path, rtw89_phy_read32_mask(rtwdev, R_NCTL_RPT, MASKDWORD));

	if (!notready)
		kfail = !!rtw89_phy_read32_mask(rtwdev, R_NCTL_RPT, B_NCTL_RPT_FLG);

	if (kfail) {
		rtw89_phy_write32_mask(rtwdev, R_RXIQC + (path << 8),
				       MASKDWORD, 0x40000002);
		iqk_info->is_wb_rxiqk[path] = false;
	} else {
		iqk_info->is_wb_rxiqk[path] = false;
	}

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[IQK]S%x, kfail = 0x%x, 0x8%x3c = 0x%x\n", path, kfail,
		    1 << path, iqk_info->nb_rxcfir[path]);
	return kfail;
}

static void _iqk_rxclk_setting(struct rtw89_dev *rtwdev, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;

	rtw89_write_rf(rtwdev, path, RR_RXBB2, RR_RXBB2_CKT, 0x1);

	if (iqk_info->iqk_bw[path] == RTW89_CHANNEL_WIDTH_80) {
		rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_RXK, 0x0101);
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK, B_DPD_GDIS, 0x1);

		_rxck_force(rtwdev, path, true, ADC_960M);

		rtw89_rfk_parser(rtwdev, &rtw8851b_iqk_rxclk_80_defs_tbl);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_RXK, 0x0101);
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK, B_DPD_GDIS, 0x1);

		_rxck_force(rtwdev, path, true, ADC_960M);

		rtw89_rfk_parser(rtwdev, &rtw8851b_iqk_rxclk_others_defs_tbl);
	}

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, (2)before RXK IQK\n", path);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, 0x%x[07:10] = 0x%x\n", path,
		    0xc0d4, rtw89_phy_read32_mask(rtwdev, 0xc0d4, GENMASK(10, 7)));
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, 0x%x[11:14] = 0x%x\n", path,
		    0xc0d4, rtw89_phy_read32_mask(rtwdev, 0xc0d4, GENMASK(14, 11)));
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, 0x%x[26:27] = 0x%x\n", path,
		    0xc0d4, rtw89_phy_read32_mask(rtwdev, 0xc0d4, GENMASK(27, 26)));
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, 0x%x[05:08] = 0x%x\n", path,
		    0xc0d8, rtw89_phy_read32_mask(rtwdev, 0xc0d8, GENMASK(8, 5)));
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, 0x%x[17:21] = 0x%x\n", path,
		    0xc0c4, rtw89_phy_read32_mask(rtwdev, 0xc0c4, GENMASK(21, 17)));
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, 0x%x[16:31] = 0x%x\n", path,
		    0xc0e8, rtw89_phy_read32_mask(rtwdev, 0xc0e8, GENMASK(31, 16)));
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, 0x%x[04:05] = 0x%x\n", path,
		    0xc0e4, rtw89_phy_read32_mask(rtwdev, 0xc0e4, GENMASK(5, 4)));
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, 0x%x[23:31]  = 0x%x\n", path,
		    0x12a0, rtw89_phy_read32_mask(rtwdev, 0x12a0, GENMASK(31, 23)));
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, 0x%x[13:14] = 0x%x\n", path,
		    0xc0ec, rtw89_phy_read32_mask(rtwdev, 0xc0ec, GENMASK(14, 13)));
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, 0x%x[16:23] = 0x%x\n", path,
		    0xc0ec, rtw89_phy_read32_mask(rtwdev, 0xc0ec, GENMASK(23, 16)));
}

static bool _txk_5g_group_sel(struct rtw89_dev *rtwdev,
			      enum rtw89_phy_idx phy_idx, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	bool kfail = false;
	bool notready;
	u8 gp;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]===>%s\n", __func__);

	for (gp = 0x0; gp < RTW8851B_TXK_GROUP_NR; gp++) {
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR0, a_power_range[gp]);
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR1, a_track_range[gp]);
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, a_gain_bb[gp]);

		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT, B_CFIR_LUT_SEL, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT, B_CFIR_LUT_G3, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT, B_CFIR_LUT_G2, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT, B_CFIR_LUT_GP, gp);
		rtw89_phy_write32_mask(rtwdev, R_NCTL_N1, B_NCTL_N1_CIP, 0x00);
		rtw89_phy_write32_mask(rtwdev, R_KIP_IQP, MASKDWORD, a_itqt[gp]);

		notready = _iqk_one_shot(rtwdev, phy_idx, path, ID_NBTXK);
		iqk_info->nb_txcfir[path] =
			rtw89_phy_read32_mask(rtwdev, R_TXIQC, MASKDWORD)  | 0x2;

		rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8),
				       MASKDWORD, a_itqt[gp]);
		notready = _iqk_one_shot(rtwdev, phy_idx, path, ID_TXK);
	}

	if (!notready)
		kfail = !!rtw89_phy_read32_mask(rtwdev, R_NCTL_RPT, B_NCTL_RPT_FLG);

	if (kfail) {
		rtw89_phy_write32_mask(rtwdev, R_TXIQC + (path << 8),
				       MASKDWORD, iqk_info->nb_txcfir[path] | 0x2);
		iqk_info->is_wb_txiqk[path] = false;
	} else {
		rtw89_phy_write32_mask(rtwdev, R_TXIQC + (path << 8),
				       MASKDWORD, 0x40000000);
		iqk_info->is_wb_txiqk[path] = true;
	}

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[IQK]S%x, kfail = 0x%x, 0x8%x38 = 0x%x\n", path, kfail,
		    1 << path, iqk_info->nb_txcfir[path]);
	return kfail;
}

static bool _txk_2g_group_sel(struct rtw89_dev *rtwdev,
			      enum rtw89_phy_idx phy_idx, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	bool kfail = false;
	bool notready;
	u8 gp;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]===>%s\n", __func__);

	for (gp = 0x0; gp < RTW8851B_TXK_GROUP_NR; gp++) {
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR0, g_power_range[gp]);
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR1, g_track_range[gp]);
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, g_gain_bb[gp]);

		rtw89_phy_write32_mask(rtwdev, R_KIP_IQP, MASKDWORD, g_itqt[gp]);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT, B_CFIR_LUT_SEL, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT, B_CFIR_LUT_G3, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT, B_CFIR_LUT_G2, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT, B_CFIR_LUT_GP, gp);
		rtw89_phy_write32_mask(rtwdev, R_NCTL_N1, B_NCTL_N1_CIP, 0x00);

		notready = _iqk_one_shot(rtwdev, phy_idx, path, ID_NBTXK);
		iqk_info->nb_txcfir[path] =
			rtw89_phy_read32_mask(rtwdev, R_TXIQC, MASKDWORD)  | 0x2;

		rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8),
				       MASKDWORD, g_itqt[gp]);
		notready = _iqk_one_shot(rtwdev, phy_idx, path, ID_TXK);
	}

	if (!notready)
		kfail = !!rtw89_phy_read32_mask(rtwdev, R_NCTL_RPT, B_NCTL_RPT_FLG);

	if (kfail) {
		rtw89_phy_write32_mask(rtwdev, R_TXIQC + (path << 8),
				       MASKDWORD, iqk_info->nb_txcfir[path] | 0x2);
		iqk_info->is_wb_txiqk[path] = false;
	} else {
		rtw89_phy_write32_mask(rtwdev, R_TXIQC + (path << 8),
				       MASKDWORD, 0x40000000);
		iqk_info->is_wb_txiqk[path] = true;
	}

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[IQK]S%x, kfail = 0x%x, 0x8%x38 = 0x%x\n", path, kfail,
		    1 << path, iqk_info->nb_txcfir[path]);
	return kfail;
}

static bool _iqk_5g_nbtxk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx,
			  u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	bool kfail = false;
	bool notready;
	u8 gp;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]===>%s\n", __func__);

	for (gp = 0x0; gp < RTW8851B_TXK_GROUP_NR; gp++) {
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR0, a_power_range[gp]);
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR1, a_track_range[gp]);
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, a_gain_bb[gp]);

		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT, B_CFIR_LUT_SEL, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT, B_CFIR_LUT_G3, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT, B_CFIR_LUT_G2, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT, B_CFIR_LUT_GP, gp);
		rtw89_phy_write32_mask(rtwdev, R_NCTL_N1, B_NCTL_N1_CIP, 0x00);
		rtw89_phy_write32_mask(rtwdev, R_KIP_IQP, MASKDWORD, a_itqt[gp]);

		notready = _iqk_one_shot(rtwdev, phy_idx, path, ID_NBTXK);
		iqk_info->nb_txcfir[path] =
			rtw89_phy_read32_mask(rtwdev, R_TXIQC, MASKDWORD)  | 0x2;
	}

	if (!notready)
		kfail = !!rtw89_phy_read32_mask(rtwdev, R_NCTL_RPT, B_NCTL_RPT_FLG);

	if (kfail) {
		rtw89_phy_write32_mask(rtwdev, R_TXIQC + (path << 8),
				       MASKDWORD, 0x40000002);
		iqk_info->is_wb_rxiqk[path] = false;
	} else {
		iqk_info->is_wb_rxiqk[path] = false;
	}

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[IQK]S%x, kfail = 0x%x, 0x8%x38 = 0x%x\n", path, kfail,
		    1 << path, iqk_info->nb_txcfir[path]);
	return kfail;
}

static bool _iqk_2g_nbtxk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx,
			  u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	bool kfail = false;
	bool notready;
	u8 gp;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]===>%s\n", __func__);

	for (gp = 0x0; gp < RTW8851B_TXK_GROUP_NR; gp++) {
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR0, g_power_range[gp]);
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR1, g_track_range[gp]);
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, g_gain_bb[gp]);

		rtw89_phy_write32_mask(rtwdev, R_KIP_IQP, MASKDWORD, g_itqt[gp]);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT, B_CFIR_LUT_SEL, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT, B_CFIR_LUT_G3, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT, B_CFIR_LUT_G2, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT, B_CFIR_LUT_GP, gp);
		rtw89_phy_write32_mask(rtwdev, R_NCTL_N1, B_NCTL_N1_CIP, 0x00);

		notready = _iqk_one_shot(rtwdev, phy_idx, path, ID_NBTXK);
		iqk_info->nb_txcfir[path] =
			rtw89_phy_read32_mask(rtwdev, R_TXIQC + (path << 8),
					      MASKDWORD)  | 0x2;
	}

	if (!notready)
		kfail = !!rtw89_phy_read32_mask(rtwdev, R_NCTL_RPT, B_NCTL_RPT_FLG);

	if (kfail) {
		rtw89_phy_write32_mask(rtwdev, R_TXIQC + (path << 8),
				       MASKDWORD, 0x40000002);
		iqk_info->is_wb_rxiqk[path] = false;
	} else {
		iqk_info->is_wb_rxiqk[path] = false;
	}

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[IQK]S%x, kfail = 0x%x, 0x8%x38 = 0x%x\n", path, kfail,
		    1 << path, iqk_info->nb_txcfir[path]);
	return kfail;
}

static bool _iqk_2g_lok(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx,
			u8 path)
{
	static const u32 g_txbb[RTW8851B_LOK_GRAM] = {
		0x02, 0x06, 0x0a, 0x0c, 0x0e, 0x10, 0x12, 0x14, 0x16, 0x17};
	static const u32 g_itqt[RTW8851B_LOK_GRAM] = {
		0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x12, 0x12, 0x12, 0x1b};
	static const u32 g_wa[RTW8851B_LOK_GRAM] = {
		0x00, 0x04, 0x08, 0x0c, 0x0e, 0x10, 0x12, 0x14, 0x16, 0x17};
	bool fail = false;
	u8 i;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]===>%s\n", __func__);

	rtw89_write_rf(rtwdev, RF_PATH_A, RR_LUTDBG, RR_LUTDBG_LOK, 0x0);
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_TXIG, RR_TXIG_GR0, 0x0);
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_TXIG, RR_TXIG_GR1, 0x6);

	for (i = 0; i < RTW8851B_LOK_GRAM; i++) {
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_TXIG, RR_TXIG_TG, g_txbb[i]);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_LUTWA, RR_LUTWA_M1, g_wa[i]);
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK, B_IQK_RFC_ON, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_KIP_IQP, B_KIP_IQP_IQSW, g_itqt[i]);
		rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_TXT, 0x021);
		rtw89_phy_write32_mask(rtwdev, R_NCTL_CFG, MASKDWORD,
				       0x00000109 | (1 << (4 + path)));
		fail |= _iqk_check_cal(rtwdev, path);

		rtw89_phy_write32_mask(rtwdev, R_NCTL_N1, B_NCTL_N1_CIP, 0x00);
		rtw89_phy_write32_mask(rtwdev, R_KIP_IQP, B_KIP_IQP_IQSW, g_itqt[i]);
		rtw89_phy_write32_mask(rtwdev, R_NCTL_CFG, MASKDWORD,
				       0x00000309 | (1 << (4 + path)));
		fail |= _iqk_check_cal(rtwdev, path);

		rtw89_phy_write32_mask(rtwdev, R_NCTL_N1, B_NCTL_N1_CIP, 0x00);
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK, B_IQK_RFC_ON, 0x0);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK]S0, i = %x, 0x8[19:15] = 0x%x,0x8[09:05] = 0x%x\n", i,
			    rtw89_read_rf(rtwdev, RF_PATH_A, RR_DTXLOK, 0xf8000),
			    rtw89_read_rf(rtwdev, RF_PATH_A, RR_DTXLOK, 0x003e0));
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK]S0, i = %x, 0x9[19:16] = 0x%x,0x9[09:06] = 0x%x\n", i,
			    rtw89_read_rf(rtwdev, RF_PATH_A, RR_RSV2, 0xf0000),
			    rtw89_read_rf(rtwdev, RF_PATH_A, RR_RSV2, 0x003c0));
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK]S0, i = %x, 0x58 = %x\n", i,
			    rtw89_read_rf(rtwdev, RF_PATH_A, RR_TXMO, RFREG_MASK));
	}

	return fail;
}

static bool _iqk_5g_lok(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx,
			u8 path)
{
	static const u32 a_txbb[RTW8851B_LOK_GRAM] = {
		0x02, 0x06, 0x0a, 0x0c, 0x0e, 0x10, 0x12, 0x14, 0x16, 0x17};
	static const u32 a_itqt[RTW8851B_LOK_GRAM] = {
		0x09, 0x09, 0x09, 0x12, 0x12, 0x12, 0x1b, 0x1b, 0x1b, 0x1b};
	static const u32 a_wa[RTW8851B_LOK_GRAM] = {
		0x80, 0x84, 0x88, 0x8c, 0x8e, 0x90, 0x92, 0x94, 0x96, 0x97};
	bool fail = false;
	u8 i;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]===>%s\n", __func__);

	rtw89_write_rf(rtwdev, RF_PATH_A, RR_LUTDBG, RR_LUTDBG_LOK, 0x0);
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_TXIG, RR_TXIG_GR0, 0x0);
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_TXIG, RR_TXIG_GR1, 0x7);

	for (i = 0; i < RTW8851B_LOK_GRAM; i++) {
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_TXIG, RR_TXIG_TG, a_txbb[i]);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_LUTWA, RR_LUTWA_M1, a_wa[i]);
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK, B_IQK_RFC_ON, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_KIP_IQP, B_KIP_IQP_IQSW, a_itqt[i]);
		rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_TXT, 0x021);
		rtw89_phy_write32_mask(rtwdev, R_NCTL_CFG, MASKDWORD,
				       0x00000109 | (1 << (4 + path)));
		fail |= _iqk_check_cal(rtwdev, path);

		rtw89_phy_write32_mask(rtwdev, R_NCTL_N1, B_NCTL_N1_CIP, 0x00);
		rtw89_phy_write32_mask(rtwdev, R_KIP_IQP, B_KIP_IQP_IQSW, a_itqt[i]);
		rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_TXT, 0x021);
		rtw89_phy_write32_mask(rtwdev, R_NCTL_CFG, MASKDWORD,
				       0x00000309 | (1 << (4 + path)));
		fail |= _iqk_check_cal(rtwdev, path);

		rtw89_phy_write32_mask(rtwdev, R_NCTL_N1, B_NCTL_N1_CIP, 0x00);
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK, B_IQK_RFC_ON, 0x0);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK]S0, i = %x, 0x8[19:15] = 0x%x,0x8[09:05] = 0x%x\n", i,
			    rtw89_read_rf(rtwdev, RF_PATH_A, RR_DTXLOK, 0xf8000),
			    rtw89_read_rf(rtwdev, RF_PATH_A, RR_DTXLOK, 0x003e0));
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK]S0, i = %x, 0x9[19:16] = 0x%x,0x9[09:06] = 0x%x\n", i,
			    rtw89_read_rf(rtwdev, RF_PATH_A, RR_RSV2, 0xf0000),
			    rtw89_read_rf(rtwdev, RF_PATH_A, RR_RSV2, 0x003c0));
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK]S0, i = %x, 0x58 = %x\n", i,
			    rtw89_read_rf(rtwdev, RF_PATH_A, RR_TXMO, RFREG_MASK));
	}

	return fail;
}

static void _iqk_txk_setting(struct rtw89_dev *rtwdev, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;

	switch (iqk_info->iqk_band[path]) {
	case RTW89_BAND_2G:
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]RTW89_BAND_2G\n");
		rtw89_rfk_parser(rtwdev, &rtw8851b_iqk_txk_2ghz_defs_tbl);
		break;
	case RTW89_BAND_5G:
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]RTW89_BAND_5G\n");
		rtw89_rfk_parser(rtwdev, &rtw8851b_iqk_txk_5ghz_defs_tbl);
		break;
	default:
		break;
	}
}

#define IQK_LOK_RETRY 1

static void _iqk_by_path(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx,
			 u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	bool lok_is_fail;
	u8 i;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]===>%s\n", __func__);

	for (i = 0; i < IQK_LOK_RETRY; i++) {
		_iqk_txk_setting(rtwdev, path);
		if (iqk_info->iqk_band[path] == RTW89_BAND_2G)
			lok_is_fail = _iqk_2g_lok(rtwdev, phy_idx, path);
		else
			lok_is_fail = _iqk_5g_lok(rtwdev, phy_idx, path);

		if (!lok_is_fail)
			break;
	}

	if (iqk_info->is_nbiqk) {
		if (iqk_info->iqk_band[path] == RTW89_BAND_2G)
			iqk_info->iqk_tx_fail[0][path] =
				_iqk_2g_nbtxk(rtwdev, phy_idx, path);
		else
			iqk_info->iqk_tx_fail[0][path] =
				_iqk_5g_nbtxk(rtwdev, phy_idx, path);
	} else {
		if (iqk_info->iqk_band[path] == RTW89_BAND_2G)
			iqk_info->iqk_tx_fail[0][path] =
				_txk_2g_group_sel(rtwdev, phy_idx, path);
		else
			iqk_info->iqk_tx_fail[0][path] =
				_txk_5g_group_sel(rtwdev, phy_idx, path);
	}

	_iqk_rxclk_setting(rtwdev, path);
	_iqk_rxk_setting(rtwdev, path);
	_adc_fifo_rst(rtwdev, phy_idx, path);

	if (iqk_info->is_nbiqk) {
		if (iqk_info->iqk_band[path] == RTW89_BAND_2G)
			iqk_info->iqk_rx_fail[0][path] =
				_iqk_2g_nbrxk(rtwdev, phy_idx, path);
		else
			iqk_info->iqk_rx_fail[0][path] =
				_iqk_5g_nbrxk(rtwdev, phy_idx, path);
	} else {
		if (iqk_info->iqk_band[path] == RTW89_BAND_2G)
			iqk_info->iqk_rx_fail[0][path] =
				_rxk_2g_group_sel(rtwdev, phy_idx, path);
		else
			iqk_info->iqk_rx_fail[0][path] =
				_rxk_5g_group_sel(rtwdev, phy_idx, path);
	}
}

static void _rfk_backup_bb_reg(struct rtw89_dev *rtwdev,
			       u32 backup_bb_reg_val[])
{
	u32 i;

	for (i = 0; i < BACKUP_BB_REGS_NR; i++) {
		backup_bb_reg_val[i] =
			rtw89_phy_read32_mask(rtwdev, rtw8851b_backup_bb_regs[i],
					      MASKDWORD);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[RFK]backup bb reg : %x, value =%x\n",
			    rtw8851b_backup_bb_regs[i], backup_bb_reg_val[i]);
	}
}

static void _rfk_backup_rf_reg(struct rtw89_dev *rtwdev,
			       u32 backup_rf_reg_val[], u8 rf_path)
{
	u32 i;

	for (i = 0; i < BACKUP_RF_REGS_NR; i++) {
		backup_rf_reg_val[i] =
			rtw89_read_rf(rtwdev, rf_path,
				      rtw8851b_backup_rf_regs[i], RFREG_MASK);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[RFK]backup rf S%d reg : %x, value =%x\n", rf_path,
			    rtw8851b_backup_rf_regs[i], backup_rf_reg_val[i]);
	}
}

static void _rfk_restore_bb_reg(struct rtw89_dev *rtwdev,
				const u32 backup_bb_reg_val[])
{
	u32 i;

	for (i = 0; i < BACKUP_BB_REGS_NR; i++) {
		rtw89_phy_write32_mask(rtwdev, rtw8851b_backup_bb_regs[i],
				       MASKDWORD, backup_bb_reg_val[i]);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[RFK]restore bb reg : %x, value =%x\n",
			    rtw8851b_backup_bb_regs[i], backup_bb_reg_val[i]);
	}
}

static void _rfk_restore_rf_reg(struct rtw89_dev *rtwdev,
				const u32 backup_rf_reg_val[], u8 rf_path)
{
	u32 i;

	for (i = 0; i < BACKUP_RF_REGS_NR; i++) {
		rtw89_write_rf(rtwdev, rf_path, rtw8851b_backup_rf_regs[i],
			       RFREG_MASK, backup_rf_reg_val[i]);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[RFK]restore rf S%d reg: %x, value =%x\n", rf_path,
			    rtw8851b_backup_rf_regs[i], backup_rf_reg_val[i]);
	}
}

static void _iqk_get_ch_info(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			     u8 path, enum rtw89_chanctx_idx chanctx_idx)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, chanctx_idx);
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	u8 idx = 0;

	iqk_info->iqk_band[path] = chan->band_type;
	iqk_info->iqk_bw[path] = chan->band_width;
	iqk_info->iqk_ch[path] = chan->channel;
	iqk_info->iqk_table_idx[path] = idx;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%d (PHY%d): / DBCC %s/ %s/ CH%d/ %s\n",
		    path, phy, rtwdev->dbcc_en ? "on" : "off",
		    iqk_info->iqk_band[path] == 0 ? "2G" :
		    iqk_info->iqk_band[path] == 1 ? "5G" : "6G",
		    iqk_info->iqk_ch[path],
		    iqk_info->iqk_bw[path] == 0 ? "20M" :
		    iqk_info->iqk_bw[path] == 1 ? "40M" : "80M");
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]times = 0x%x, ch =%x\n",
		    iqk_info->iqk_times, idx);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, iqk_info->syn1to2= 0x%x\n",
		    path, iqk_info->syn1to2);
}

static void _iqk_start_iqk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx,
			   u8 path)
{
	_iqk_by_path(rtwdev, phy_idx, path);
}

static void _iqk_restore(struct rtw89_dev *rtwdev, u8 path)
{
	bool fail;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]===>%s\n", __func__);

	rtw89_phy_write32_mask(rtwdev, R_NCTL_CFG, MASKDWORD, 0x00001219);
	fsleep(10);
	fail = _iqk_check_cal(rtwdev, path);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK] restore fail=%d\n", fail);

	rtw89_write_rf(rtwdev, RF_PATH_A, RR_LUTWE, RR_LUTWE_LOK, 0x0);
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_LUTDBG, RR_LUTDBG_TIA, 0x0);

	rtw89_phy_write32_mask(rtwdev, R_NCTL_N1, B_NCTL_N1_CIP, 0x00);
	rtw89_phy_write32_mask(rtwdev, R_NCTL_RPT, MASKDWORD, 0x00000000);
	rtw89_phy_write32_mask(rtwdev, R_KIP_SYSCFG, MASKDWORD, 0x80000000);
}

static void _iqk_afebb_restore(struct rtw89_dev *rtwdev,
			       enum rtw89_phy_idx phy_idx, u8 path)
{
	rtw89_rfk_parser(rtwdev, &rtw8851b_iqk_afebb_restore_defs_tbl);
}

static void _iqk_preset(struct rtw89_dev *rtwdev, u8 path)
{
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]===>%s\n", __func__);

	rtw89_write_rf(rtwdev, path, RR_RSV1, RR_RSV1_RST, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_NCTL_RPT, MASKDWORD, 0x00000080);
	rtw89_phy_write32_mask(rtwdev, R_KIP_SYSCFG, MASKDWORD, 0x81ff010a);
}

static void _iqk_macbb_setting(struct rtw89_dev *rtwdev,
			       enum rtw89_phy_idx phy_idx, u8 path)
{
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]===>%s\n", __func__);

	rtw89_rfk_parser(rtwdev, &rtw8851b_iqk_macbb_defs_tbl);

	_txck_force(rtwdev, path, true, DAC_960M);

	rtw89_phy_write32_mask(rtwdev, R_UPD_CLK, B_DPD_GDIS, 0x1);

	_rxck_force(rtwdev, path, true, ADC_1920M);

	rtw89_rfk_parser(rtwdev, &rtw8851b_iqk_macbb_bh_defs_tbl);
}

static void _iqk_init(struct rtw89_dev *rtwdev)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	u8 idx, path;

	rtw89_phy_write32_mask(rtwdev, R_IQKINF, MASKDWORD, 0x0);

	if (iqk_info->is_iqk_init)
		return;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]===>%s\n", __func__);

	iqk_info->is_iqk_init = true;
	iqk_info->is_nbiqk = false;
	iqk_info->iqk_fft_en = false;
	iqk_info->iqk_sram_en = false;
	iqk_info->iqk_cfir_en = false;
	iqk_info->iqk_xym_en = false;
	iqk_info->iqk_times = 0x0;

	for (idx = 0; idx < RTW89_IQK_CHS_NR; idx++) {
		iqk_info->iqk_channel[idx] = 0x0;
		for (path = 0; path < RF_PATH_NUM_8851B; path++) {
			iqk_info->lok_cor_fail[idx][path] = false;
			iqk_info->lok_fin_fail[idx][path] = false;
			iqk_info->iqk_tx_fail[idx][path] = false;
			iqk_info->iqk_rx_fail[idx][path] = false;
			iqk_info->iqk_table_idx[path] = 0x0;
		}
	}
}

static void _doiqk(struct rtw89_dev *rtwdev, bool force,
		   enum rtw89_phy_idx phy_idx, u8 path,
		   enum rtw89_chanctx_idx chanctx_idx)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	u8 phy_map = rtw89_btc_phymap(rtwdev, phy_idx, RF_AB, chanctx_idx);
	u32 backup_rf_val[RTW8851B_IQK_SS][BACKUP_RF_REGS_NR];
	u32 backup_bb_val[BACKUP_BB_REGS_NR];

	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_IQK,
			      BTC_WRFK_ONESHOT_START);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[IQK]==========IQK start!!!!!==========\n");
	iqk_info->iqk_times++;
	iqk_info->version = RTW8851B_IQK_VER;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]Test Ver 0x%x\n", iqk_info->version);
	_iqk_get_ch_info(rtwdev, phy_idx, path, chanctx_idx);

	_rfk_backup_bb_reg(rtwdev, &backup_bb_val[0]);
	_rfk_backup_rf_reg(rtwdev, &backup_rf_val[path][0], path);
	_iqk_macbb_setting(rtwdev, phy_idx, path);
	_iqk_preset(rtwdev, path);
	_iqk_start_iqk(rtwdev, phy_idx, path);
	_iqk_restore(rtwdev, path);
	_iqk_afebb_restore(rtwdev, phy_idx, path);
	_rfk_restore_bb_reg(rtwdev, &backup_bb_val[0]);
	_rfk_restore_rf_reg(rtwdev, &backup_rf_val[path][0], path);

	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_IQK,
			      BTC_WRFK_ONESHOT_STOP);
}

static void _iqk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx,
		 bool force, enum rtw89_chanctx_idx chanctx_idx)
{
	_doiqk(rtwdev, force, phy_idx, RF_PATH_A, chanctx_idx);
}

static void _dpk_bkup_kip(struct rtw89_dev *rtwdev, const u32 *reg,
			  u32 reg_bkup[][DPK_KIP_REG_NUM_8851B], u8 path)
{
	u8 i;

	for (i = 0; i < DPK_KIP_REG_NUM_8851B; i++) {
		reg_bkup[path][i] =
			rtw89_phy_read32_mask(rtwdev, reg[i] + (path << 8), MASKDWORD);

		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] Backup 0x%x = %x\n",
			    reg[i] + (path << 8), reg_bkup[path][i]);
	}
}

static void _dpk_bkup_rf(struct rtw89_dev *rtwdev, const u32 *rf_reg,
			 u32 rf_bkup[][DPK_RF_REG_NUM_8851B], u8 path)
{
	u8 i;

	for (i = 0; i < DPK_RF_REG_NUM_8851B; i++) {
		rf_bkup[path][i] = rtw89_read_rf(rtwdev, path, rf_reg[i], RFREG_MASK);

		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] Backup RF S%d 0x%x = %x\n",
			    path, rf_reg[i], rf_bkup[path][i]);
	}
}

static void _dpk_reload_kip(struct rtw89_dev *rtwdev, const u32 *reg,
			    u32 reg_bkup[][DPK_KIP_REG_NUM_8851B], u8 path)
{
	u8 i;

	for (i = 0; i < DPK_KIP_REG_NUM_8851B; i++) {
		rtw89_phy_write32_mask(rtwdev, reg[i] + (path << 8), MASKDWORD,
				       reg_bkup[path][i]);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[DPK] Reload 0x%x = %x\n",
			    reg[i] + (path << 8), reg_bkup[path][i]);
	}
}

static void _dpk_reload_rf(struct rtw89_dev *rtwdev, const u32 *rf_reg,
			   u32 rf_bkup[][DPK_RF_REG_NUM_8851B], u8 path)
{
	u8 i;

	for (i = 0; i < DPK_RF_REG_NUM_8851B; i++) {
		rtw89_write_rf(rtwdev, path, rf_reg[i], RFREG_MASK, rf_bkup[path][i]);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[DPK] Reload RF S%d 0x%x = %x\n", path,
			    rf_reg[i], rf_bkup[path][i]);
	}
}

static void _dpk_one_shot(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			  enum rtw89_rf_path path, enum dpk_id id)
{
	u16 dpk_cmd;
	u32 val;
	int ret;

	dpk_cmd = ((id << 8) | (0x19 + path * 0x12));
	rtw89_phy_write32_mask(rtwdev, R_NCTL_CFG, MASKDWORD, dpk_cmd);

	ret = read_poll_timeout_atomic(rtw89_phy_read32_mask, val, val == 0x55,
				       10, 20000, false,
				       rtwdev, 0xbff8, MASKBYTE0);
	if (ret)
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] one-shot 1 timeout\n");

	udelay(1);

	ret = read_poll_timeout_atomic(rtw89_phy_read32_mask, val, val == 0x8000,
				       1, 2000, false,
				       rtwdev, R_RPT_COM, MASKLWORD);
	if (ret)
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] one-shot 2 timeout\n");

	rtw89_phy_write32_mask(rtwdev, R_NCTL_N1, MASKBYTE0, 0x0);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DPK] one-shot for %s = 0x%04x\n",
		    id == 0x28 ? "KIP_PRESET" :
		    id == 0x29 ? "DPK_TXAGC" :
		    id == 0x2a ? "DPK_RXAGC" :
		    id == 0x2b ? "SYNC" :
		    id == 0x2c ? "GAIN_LOSS" :
		    id == 0x2d ? "MDPK_IDL" :
		    id == 0x2f ? "DPK_GAIN_NORM" :
		    id == 0x31 ? "KIP_RESTORE" :
		    id == 0x6 ? "LBK_RXIQK" : "Unknown id",
		    dpk_cmd);
}

static void _dpk_onoff(struct rtw89_dev *rtwdev, enum rtw89_rf_path path,
		       bool off)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;
	u8 kidx = dpk->cur_idx[path];
	u8 off_reverse = off ? 0 : 1;
	u8 val;

	val = dpk->is_dpk_enable * off_reverse * dpk->bp[path][kidx].path_ok;

	rtw89_phy_write32_mask(rtwdev, R_DPD_CH0A + (path << 8) + (kidx << 2),
			       0xf0000000, val);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] S%d[%d] DPK %s !!!\n", path,
		    kidx, val == 0 ? "disable" : "enable");
}

static void _dpk_init(struct rtw89_dev *rtwdev, enum rtw89_rf_path path)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;

	u8 kidx = dpk->cur_idx[path];

	dpk->bp[path][kidx].path_ok = 0;
}

static void _dpk_information(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			     enum rtw89_rf_path path, enum rtw89_chanctx_idx chanctx_idx)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, chanctx_idx);
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;

	u8 kidx = dpk->cur_idx[path];

	dpk->bp[path][kidx].band = chan->band_type;
	dpk->bp[path][kidx].ch = chan->band_width;
	dpk->bp[path][kidx].bw = chan->channel;

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DPK] S%d[%d] (PHY%d): TSSI %s/ DBCC %s/ %s/ CH%d/ %s\n",
		    path, dpk->cur_idx[path], phy,
		    rtwdev->is_tssi_mode[path] ? "on" : "off",
		    rtwdev->dbcc_en ? "on" : "off",
		    dpk->bp[path][kidx].band == 0 ? "2G" :
		    dpk->bp[path][kidx].band == 1 ? "5G" : "6G",
		    dpk->bp[path][kidx].ch,
		    dpk->bp[path][kidx].bw == 0 ? "20M" :
		    dpk->bp[path][kidx].bw == 1 ? "40M" :
		    dpk->bp[path][kidx].bw == 2 ? "80M" : "160M");
}

static void _dpk_rxagc_onoff(struct rtw89_dev *rtwdev, enum rtw89_rf_path path,
			     bool turn_on)
{
	if (path == RF_PATH_A)
		rtw89_phy_write32_mask(rtwdev, R_P0_AGC_CTL, B_P0_AGC_EN, turn_on);
	else
		rtw89_phy_write32_mask(rtwdev, R_P1_AGC_CTL, B_P1_AGC_EN, turn_on);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] S%d RXAGC is %s\n", path,
		    turn_on ? "turn_on" : "turn_off");
}

static void _dpk_bb_afe_setting(struct rtw89_dev *rtwdev, enum rtw89_rf_path path)
{
	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, BIT(16 + path), 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, BIT(20 + path), 0x0);
	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, BIT(24 + path), 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, BIT(28 + path), 0x0);
	rtw89_phy_write32_mask(rtwdev, R_UPD_CLK + (path << 13), MASKDWORD, 0xd801dffd);

	_txck_force(rtwdev, path, true, DAC_960M);
	_rxck_force(rtwdev, path, true, ADC_1920M);

	rtw89_phy_write32_mask(rtwdev, R_DCIM, B_DCIM_FR, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_ADCMOD, B_ADCMOD_AUTO_RST, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_P0_NRBW, B_P0_NRBW_DBG, 0x1);
	udelay(1);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR_PW15, B_ANAPAR_PW15, 0x1f);
	udelay(10);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR_PW15, B_ANAPAR_PW15, 0x13);
	udelay(2);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_15, 0x0001);
	udelay(2);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_15, 0x0041);
	udelay(10);

	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, BIT(20 + path), 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, BIT(28 + path), 0x1);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] S%d BB/AFE setting\n", path);
}

static void _dpk_bb_afe_restore(struct rtw89_dev *rtwdev, enum rtw89_rf_path path)
{
	rtw89_phy_write32_mask(rtwdev, R_P0_NRBW + (path << 13), B_P0_NRBW_DBG, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, BIT(16 + path), 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, BIT(20 + path), 0x0);
	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, BIT(24 + path), 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, BIT(28 + path), 0x0);
	rtw89_phy_write32_mask(rtwdev, R_UPD_CLK + (path << 13), MASKDWORD, 0x00000000);
	rtw89_phy_write32_mask(rtwdev, R_P0_RXCK + (path << 13), B_P0_TXCK_ALL, 0x00);
	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, BIT(16 + path), 0x0);
	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, BIT(24 + path), 0x0);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] S%d BB/AFE restore\n", path);
}

static void _dpk_tssi_pause(struct rtw89_dev *rtwdev, enum rtw89_rf_path path,
			    bool is_pause)
{
	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_TRK + (path << 13),
			       B_P0_TSSI_TRK_EN, is_pause);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] S%d TSSI %s\n", path,
		    is_pause ? "pause" : "resume");
}

static
void _dpk_tssi_slope_k_onoff(struct rtw89_dev *rtwdev, enum rtw89_rf_path path,
			     bool is_on)
{
	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_SLOPE_CAL + (path << 13),
			       B_P0_TSSI_SLOPE_CAL_EN, is_on);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] S%d TSSI slpoe_k %s\n", path,
		    str_on_off(is_on));
}

static void _dpk_tpg_sel(struct rtw89_dev *rtwdev, enum rtw89_rf_path path, u8 kidx)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;

	if (dpk->bp[path][kidx].bw == RTW89_CHANNEL_WIDTH_80) {
		rtw89_phy_write32_mask(rtwdev, R_TPG_MOD, B_TPG_MOD_F, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_TPG_SEL, MASKDWORD, 0xffe0fa00);
	} else if (dpk->bp[path][kidx].bw == RTW89_CHANNEL_WIDTH_40) {
		rtw89_phy_write32_mask(rtwdev, R_TPG_MOD, B_TPG_MOD_F, 0x2);
		rtw89_phy_write32_mask(rtwdev, R_TPG_SEL, MASKDWORD, 0xff4009e0);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_TPG_MOD, B_TPG_MOD_F, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_TPG_SEL, MASKDWORD, 0xf9f007d0);
	}

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] TPG Select for %s\n",
		    dpk->bp[path][kidx].bw == RTW89_CHANNEL_WIDTH_80 ? "80M" :
		    dpk->bp[path][kidx].bw == RTW89_CHANNEL_WIDTH_40 ? "40M" : "20M");
}

static void _dpk_txpwr_bb_force(struct rtw89_dev *rtwdev,
				enum rtw89_rf_path path, bool force)
{
	rtw89_phy_write32_mask(rtwdev, R_TXPWRB + (path << 13), B_TXPWRB_ON, force);
	rtw89_phy_write32_mask(rtwdev, R_TXPWRB_H + (path << 13), B_TXPWRB_RDY, force);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] S%d txpwr_bb_force %s\n",
		    path, force ? "on" : "off");
}

static void _dpk_kip_pwr_clk_onoff(struct rtw89_dev *rtwdev, bool turn_on)
{
	if (turn_on) {
		rtw89_phy_write32_mask(rtwdev, R_NCTL_RPT, MASKDWORD, 0x00000080);
		rtw89_phy_write32_mask(rtwdev, R_KIP_SYSCFG, MASKDWORD, 0x807f030a);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_NCTL_RPT, MASKDWORD, 0x00000000);
		rtw89_phy_write32_mask(rtwdev, R_KIP_SYSCFG, MASKDWORD, 0x80000000);
		rtw89_phy_write32_mask(rtwdev, R_DPK_WR, BIT(18), 0x1);
	}
}

static void _dpk_kip_control_rfc(struct rtw89_dev *rtwdev,
				 enum rtw89_rf_path path, bool ctrl_by_kip)
{
	rtw89_phy_write32_mask(rtwdev, R_UPD_CLK + (path << 13),
			       B_IQK_RFC_ON, ctrl_by_kip);
}

static void _dpk_kip_preset(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			    enum rtw89_rf_path path, u8 kidx)
{
	rtw89_phy_write32_mask(rtwdev, R_KIP_MOD, B_KIP_MOD,
			       rtw89_read_rf(rtwdev, path, RR_MOD, RFREG_MASK));
	rtw89_phy_write32_mask(rtwdev, R_DPD_CH0A + (path << 8) + (kidx << 2),
			       B_DPD_SEL, 0x01);

	_dpk_kip_control_rfc(rtwdev, path, true);
	_dpk_one_shot(rtwdev, phy, path, D_KIP_PRESET);
}

static void _dpk_kip_restore(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			     enum rtw89_rf_path path)
{
	_dpk_one_shot(rtwdev, phy, path, D_KIP_RESTORE);
	_dpk_kip_control_rfc(rtwdev, path, false);
	_dpk_txpwr_bb_force(rtwdev, path, false);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] S%d restore KIP\n", path);
}

static void _dpk_kset_query(struct rtw89_dev *rtwdev, enum rtw89_rf_path path)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;

	rtw89_phy_write32_mask(rtwdev, R_KIP_RPT + (path << 8), B_KIP_RPT_SEL, 0x10);

	dpk->cur_k_set =
		rtw89_phy_read32_mask(rtwdev, R_RPT_PER + (path << 8), B_RPT_PER_KSET) - 1;
}

static void _dpk_para_query(struct rtw89_dev *rtwdev, enum rtw89_rf_path path, u8 kidx)
{
	static const u32 reg[RTW89_DPK_BKUP_NUM][DPK_KSET_NUM] = {
		{0x8190, 0x8194, 0x8198, 0x81a4},
		{0x81a8, 0x81c4, 0x81c8, 0x81e8}
	};
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;
	u8 cur_k_set = dpk->cur_k_set;
	u32 para;

	if (cur_k_set >= DPK_KSET_NUM) {
		rtw89_warn(rtwdev, "DPK cur_k_set = %d\n", cur_k_set);
		cur_k_set = 2;
	}

	para = rtw89_phy_read32_mask(rtwdev, reg[kidx][cur_k_set] + (path << 8),
				     MASKDWORD);

	dpk->bp[path][kidx].txagc_dpk = (para >> 10) & 0x3f;
	dpk->bp[path][kidx].ther_dpk = (para >> 26) & 0x3f;

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DPK] thermal/ txagc_RF (K%d) = 0x%x/ 0x%x\n",
		    dpk->cur_k_set, dpk->bp[path][kidx].ther_dpk,
		    dpk->bp[path][kidx].txagc_dpk);
}

static bool _dpk_sync_check(struct rtw89_dev *rtwdev, enum rtw89_rf_path path, u8 kidx)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;
	u8 corr_val, corr_idx, rxbb;
	u16 dc_i, dc_q;
	u8 rxbb_ov;

	rtw89_phy_write32_mask(rtwdev, R_KIP_RPT1, B_KIP_RPT1_SEL, 0x0);

	corr_idx = rtw89_phy_read32_mask(rtwdev, R_RPT_COM, B_PRT_COM_CORI);
	corr_val = rtw89_phy_read32_mask(rtwdev, R_RPT_COM, B_PRT_COM_CORV);
	dpk->corr_idx[path][kidx] = corr_idx;
	dpk->corr_val[path][kidx] = corr_val;

	rtw89_phy_write32_mask(rtwdev, R_KIP_RPT1, B_KIP_RPT1_SEL, 0x9);

	dc_i = rtw89_phy_read32_mask(rtwdev, R_RPT_COM, B_PRT_COM_DCI);
	dc_q = rtw89_phy_read32_mask(rtwdev, R_RPT_COM, B_PRT_COM_DCQ);

	dc_i = abs(sign_extend32(dc_i, 11));
	dc_q = abs(sign_extend32(dc_q, 11));

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DPK] S%d Corr_idx/ Corr_val /DC I/Q, = %d / %d / %d / %d\n",
		    path, corr_idx, corr_val, dc_i, dc_q);

	dpk->dc_i[path][kidx] = dc_i;
	dpk->dc_q[path][kidx] = dc_q;

	rtw89_phy_write32_mask(rtwdev, R_KIP_RPT1, B_KIP_RPT1_SEL, 0x8);
	rxbb = rtw89_phy_read32_mask(rtwdev, R_RPT_COM, B_PRT_COM_RXBB);

	rtw89_phy_write32_mask(rtwdev, R_KIP_RPT1, B_KIP_RPT1_SEL, 0x31);
	rxbb_ov = rtw89_phy_read32_mask(rtwdev, R_RPT_COM, B_PRT_COM_RXOV);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DPK] S%d RXBB/ RXAGC_done /RXBB_ovlmt = %d / %d / %d\n",
		    path, rxbb,
		    rtw89_phy_read32_mask(rtwdev, R_RPT_COM, B_PRT_COM_DONE),
		    rxbb_ov);

	if (dc_i > 200 || dc_q > 200 || corr_val < 170)
		return true;
	else
		return false;
}

static void _dpk_kip_set_txagc(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			       enum rtw89_rf_path path, u8 dbm,
			       bool set_from_bb)
{
	if (set_from_bb) {
		dbm = clamp_t(u8, dbm, 7, 24);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[DPK] set S%d txagc to %ddBm\n", path, dbm);
		rtw89_phy_write32_mask(rtwdev, R_TXPWRB + (path << 13),
				       B_TXPWRB_VAL, dbm << 2);
	}

	_dpk_one_shot(rtwdev, phy, path, D_TXAGC);
	_dpk_kset_query(rtwdev, path);
}

static bool _dpk_kip_set_rxagc(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			       enum rtw89_rf_path path, u8 kidx)
{
	_dpk_kip_control_rfc(rtwdev, path, false);
	rtw89_phy_write32_mask(rtwdev, R_KIP_MOD, B_KIP_MOD,
			       rtw89_read_rf(rtwdev, path, RR_MOD, RFREG_MASK));
	_dpk_kip_control_rfc(rtwdev, path, true);

	_dpk_one_shot(rtwdev, phy, path, D_RXAGC);
	return _dpk_sync_check(rtwdev, path, kidx);
}

static void _dpk_lbk_rxiqk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			   enum rtw89_rf_path path)
{
	u32 rf_11, reg_81cc;
	u8 cur_rxbb;

	rtw89_phy_write32_mask(rtwdev, R_DPD_V1 + (path << 8), B_DPD_LBK, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_MDPK_RX_DCK, B_MDPK_RX_DCK_EN, 0x1);

	_dpk_kip_control_rfc(rtwdev, path, false);

	cur_rxbb = rtw89_read_rf(rtwdev, path, RR_MOD, RR_MOD_RXB);
	rf_11 = rtw89_read_rf(rtwdev, path, RR_TXIG, RFREG_MASK);
	reg_81cc = rtw89_phy_read32_mask(rtwdev, R_KIP_IQP + (path << 8),
					 B_KIP_IQP_SW);

	rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR0, 0x0);
	rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR1, 0x3);
	rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, 0xd);
	rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_RXB, 0x1f);

	rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8), B_KIP_IQP_IQSW, 0x12);
	rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8), B_KIP_IQP_SW, 0x3);

	_dpk_kip_control_rfc(rtwdev, path, true);

	rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, MASKDWORD, 0x00250025);

	_dpk_one_shot(rtwdev, phy, path, LBK_RXIQK);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] S%d LBK RXIQC = 0x%x\n", path,
		    rtw89_phy_read32_mask(rtwdev, R_RXIQC + (path << 8), MASKDWORD));

	_dpk_kip_control_rfc(rtwdev, path, false);

	rtw89_write_rf(rtwdev, path, RR_TXIG, RFREG_MASK, rf_11);
	rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_RXB, cur_rxbb);
	rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8), B_KIP_IQP_SW, reg_81cc);

	rtw89_phy_write32_mask(rtwdev, R_MDPK_RX_DCK, B_MDPK_RX_DCK_EN, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_KPATH_CFG, B_KPATH_CFG_ED, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_LOAD_COEF + (path << 8), B_LOAD_COEF_DI, 0x1);

	_dpk_kip_control_rfc(rtwdev, path, true);
}

static void _dpk_rf_setting(struct rtw89_dev *rtwdev, enum rtw89_rf_path path, u8 kidx)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;

	if (dpk->bp[path][kidx].band == RTW89_BAND_2G) {
		rtw89_write_rf(rtwdev, path, RR_MOD, RFREG_MASK, 0x50521);
		rtw89_write_rf(rtwdev, path, RR_MOD_V1, RR_MOD_MASK, RF_DPK);
		rtw89_write_rf(rtwdev, path, RR_RXBB, RR_RXBB_ATTC, 0x0);
		rtw89_write_rf(rtwdev, path, RR_RXBB, RR_RXBB_ATTR, 0x7);
	} else {
		rtw89_write_rf(rtwdev, path, RR_MOD, RFREG_MASK,
			       0x50521 | BIT(rtwdev->dbcc_en));
		rtw89_write_rf(rtwdev, path, RR_MOD_V1, RR_MOD_MASK, RF_DPK);
		rtw89_write_rf(rtwdev, path, RR_RXA2, RR_RAA2_SATT, 0x3);
	}

	rtw89_write_rf(rtwdev, path, RR_RCKD, RR_RCKD_BW, 0x1);
	rtw89_write_rf(rtwdev, path, RR_BTC, RR_BTC_TXBB, dpk->bp[path][kidx].bw + 1);
	rtw89_write_rf(rtwdev, path, RR_BTC, RR_BTC_RXBB, 0x0);
	rtw89_write_rf(rtwdev, path, RR_RXBB2, RR_RXBB2_EBW, 0x0);
}

static void _dpk_bypass_rxiqc(struct rtw89_dev *rtwdev, enum rtw89_rf_path path)
{
	rtw89_phy_write32_mask(rtwdev, R_DPD_V1 + (path << 8), B_DPD_LBK, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_RXIQC + (path << 8), MASKDWORD, 0x40000002);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] Bypass RXIQC\n");
}

static u16 _dpk_dgain_read(struct rtw89_dev *rtwdev)
{
	u16 dgain;

	rtw89_phy_write32_mask(rtwdev, R_KIP_RPT1, B_KIP_RPT1_SEL, 0x0);
	dgain = rtw89_phy_read32_mask(rtwdev, R_RPT_COM, B_PRT_COM_DCI);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] DGain = 0x%x\n", dgain);

	return dgain;
}

static u8 _dpk_gainloss_read(struct rtw89_dev *rtwdev)
{
	u8 result;

	rtw89_phy_write32_mask(rtwdev, R_KIP_RPT1, B_KIP_RPT1_SEL, 0x6);
	rtw89_phy_write32_mask(rtwdev, R_DPK_CFG2, B_DPK_CFG2_ST, 0x1);
	result = rtw89_phy_read32_mask(rtwdev, R_RPT_COM, B_PRT_COM_GL);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] tmp GL = %d\n", result);

	return result;
}

static u8 _dpk_gainloss(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			enum rtw89_rf_path path, u8 kidx)
{
	_dpk_one_shot(rtwdev, phy, path, D_GAIN_LOSS);
	_dpk_kip_set_txagc(rtwdev, phy, path, 0xff, false);

	rtw89_phy_write32_mask(rtwdev, R_DPK_GL + (path << 8), B_DPK_GL_A1, 0xf078);
	rtw89_phy_write32_mask(rtwdev, R_DPK_GL + (path << 8), B_DPK_GL_A0, 0x0);

	return _dpk_gainloss_read(rtwdev);
}

static u8 _dpk_pas_read(struct rtw89_dev *rtwdev, u8 is_check)
{
	u32 val1_i = 0, val1_q = 0, val2_i = 0, val2_q = 0;
	u32 val1_sqrt_sum, val2_sqrt_sum;
	u8 i;

	rtw89_phy_write32_mask(rtwdev, R_KIP_RPT1, MASKBYTE2, 0x06);
	rtw89_phy_write32_mask(rtwdev, R_DPK_CFG2, B_DPK_CFG2_ST, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_DPK_CFG3, MASKBYTE2, 0x08);

	if (is_check) {
		rtw89_phy_write32_mask(rtwdev, R_DPK_CFG3, MASKBYTE3, 0x00);
		val1_i = rtw89_phy_read32_mask(rtwdev, R_RPT_COM, MASKHWORD);
		val1_i = abs(sign_extend32(val1_i, 11));
		val1_q = rtw89_phy_read32_mask(rtwdev, R_RPT_COM, MASKLWORD);
		val1_q = abs(sign_extend32(val1_q, 11));

		rtw89_phy_write32_mask(rtwdev, R_DPK_CFG3, MASKBYTE3, 0x1f);
		val2_i = rtw89_phy_read32_mask(rtwdev, R_RPT_COM, MASKHWORD);
		val2_i = abs(sign_extend32(val2_i, 11));
		val2_q = rtw89_phy_read32_mask(rtwdev, R_RPT_COM, MASKLWORD);
		val2_q = abs(sign_extend32(val2_q, 11));

		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] PAS_delta = 0x%x\n",
			    phy_div(val1_i * val1_i + val1_q * val1_q,
				    val2_i * val2_i + val2_q * val2_q));
	} else {
		for (i = 0; i < 32; i++) {
			rtw89_phy_write32_mask(rtwdev, R_DPK_CFG3, MASKBYTE3, i);
			rtw89_debug(rtwdev, RTW89_DBG_RFK,
				    "[DPK] PAS_Read[%02d]= 0x%08x\n", i,
				    rtw89_phy_read32_mask(rtwdev, R_RPT_COM, MASKDWORD));
		}
	}

	val1_sqrt_sum = val1_i * val1_i + val1_q * val1_q;
	val2_sqrt_sum = val2_i * val2_i + val2_q * val2_q;

	if (val1_sqrt_sum < val2_sqrt_sum)
		return 2;
	else if (val1_sqrt_sum >= val2_sqrt_sum * 8 / 5)
		return 1;
	else
		return 0;
}

static u8 _dpk_agc(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
		   enum rtw89_rf_path path, u8 kidx, u8 init_xdbm, u8 loss_only)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;
	u8 tmp_dbm = init_xdbm, tmp_gl_idx = 0;
	u8 step = DPK_AGC_STEP_SYNC_DGAIN;
	u8 goout = 0, agc_cnt = 0;
	bool is_fail = false;
	int limit = 200;
	u8 tmp_rxbb;
	u16 dgain;

	do {
		switch (step) {
		case DPK_AGC_STEP_SYNC_DGAIN:
			is_fail = _dpk_kip_set_rxagc(rtwdev, phy, path, kidx);

			if (is_fail) {
				goout = 1;
				break;
			}

			dgain = _dpk_dgain_read(rtwdev);

			if (dgain > 0x5fc || dgain < 0x556) {
				_dpk_one_shot(rtwdev, phy, path, D_SYNC);
				_dpk_dgain_read(rtwdev);
			}

			if (agc_cnt == 0) {
				if (dpk->bp[path][kidx].band == RTW89_BAND_2G)
					_dpk_bypass_rxiqc(rtwdev, path);
				else
					_dpk_lbk_rxiqk(rtwdev, phy, path);
			}
			step = DPK_AGC_STEP_GAIN_LOSS_IDX;
			break;

		case DPK_AGC_STEP_GAIN_LOSS_IDX:
			tmp_gl_idx = _dpk_gainloss(rtwdev, phy, path, kidx);

			if (_dpk_pas_read(rtwdev, true) == 2 && tmp_gl_idx > 0)
				step = DPK_AGC_STEP_GL_LT_CRITERION;
			else if ((tmp_gl_idx == 0 && _dpk_pas_read(rtwdev, true) == 1) ||
				 tmp_gl_idx >= 7)
				step = DPK_AGC_STEP_GL_GT_CRITERION;
			else if (tmp_gl_idx == 0)
				step = DPK_AGC_STEP_GL_LT_CRITERION;
			else
				step = DPK_AGC_STEP_SET_TX_GAIN;
			break;

		case DPK_AGC_STEP_GL_GT_CRITERION:
			if (tmp_dbm <= 7) {
				goout = 1;
				rtw89_debug(rtwdev, RTW89_DBG_RFK,
					    "[DPK] Txagc@lower bound!!\n");
			} else {
				tmp_dbm = max_t(u8, tmp_dbm - 3, 7);
				_dpk_kip_set_txagc(rtwdev, phy, path, tmp_dbm, true);
			}
			step = DPK_AGC_STEP_SYNC_DGAIN;
			agc_cnt++;
			break;

		case DPK_AGC_STEP_GL_LT_CRITERION:
			if (tmp_dbm >= 24) {
				goout = 1;
				rtw89_debug(rtwdev, RTW89_DBG_RFK,
					    "[DPK] Txagc@upper bound!!\n");
			} else {
				tmp_dbm = min_t(u8, tmp_dbm + 2, 24);
				_dpk_kip_set_txagc(rtwdev, phy, path, tmp_dbm, true);
			}
			step = DPK_AGC_STEP_SYNC_DGAIN;
			agc_cnt++;
			break;

		case DPK_AGC_STEP_SET_TX_GAIN:
			_dpk_kip_control_rfc(rtwdev, path, false);
			tmp_rxbb = rtw89_read_rf(rtwdev, path, RR_MOD, RR_MOD_RXB);
			tmp_rxbb = min_t(u8, tmp_rxbb + tmp_gl_idx, 0x1f);

			rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_RXB, tmp_rxbb);

			rtw89_debug(rtwdev, RTW89_DBG_RFK,
				    "[DPK] Adjust RXBB (%+d) = 0x%x\n",
				    tmp_gl_idx, tmp_rxbb);
			_dpk_kip_control_rfc(rtwdev, path, true);
			goout = 1;
			break;
		default:
			goout = 1;
			break;
		}
	} while (!goout && agc_cnt < 6 && limit-- > 0);

	return is_fail;
}

static void _dpk_set_mdpd_para(struct rtw89_dev *rtwdev, u8 order)
{
	switch (order) {
	case 0: /* (5,3,1) */
		rtw89_phy_write32_mask(rtwdev, R_LDL_NORM, B_LDL_NORM_OP, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_DPK_IDL, B_DPK_IDL_SEL, 0x2);
		rtw89_phy_write32_mask(rtwdev, R_LDL_NORM, B_LDL_NORM_PN, 0x3);
		rtw89_phy_write32_mask(rtwdev, R_MDPK_SYNC, B_MDPK_SYNC_DMAN, 0x1);
		break;
	case 1: /* (5,3,0) */
		rtw89_phy_write32_mask(rtwdev, R_LDL_NORM, B_LDL_NORM_OP, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_DPK_IDL, B_DPK_IDL_SEL, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_LDL_NORM, B_LDL_NORM_PN, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_MDPK_SYNC, B_MDPK_SYNC_DMAN, 0x0);
		break;
	case 2: /* (5,0,0) */
		rtw89_phy_write32_mask(rtwdev, R_LDL_NORM, B_LDL_NORM_OP, 0x2);
		rtw89_phy_write32_mask(rtwdev, R_DPK_IDL, B_DPK_IDL_SEL, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_LDL_NORM, B_LDL_NORM_PN, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_MDPK_SYNC, B_MDPK_SYNC_DMAN, 0x0);
		break;
	case 3: /* (7,3,1) */
		rtw89_phy_write32_mask(rtwdev, R_LDL_NORM, B_LDL_NORM_OP, 0x3);
		rtw89_phy_write32_mask(rtwdev, R_DPK_IDL, B_DPK_IDL_SEL, 0x3);
		rtw89_phy_write32_mask(rtwdev, R_LDL_NORM, B_LDL_NORM_PN, 0x4);
		rtw89_phy_write32_mask(rtwdev, R_MDPK_SYNC, B_MDPK_SYNC_DMAN, 0x1);
		break;
	default:
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[DPK] Wrong MDPD order!!(0x%x)\n", order);
		break;
	}

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] Set %s for IDL\n",
		    order == 0x0 ? "(5,3,1)" :
		    order == 0x1 ? "(5,3,0)" :
		    order == 0x2 ? "(5,0,0)" : "(7,3,1)");
}

static void _dpk_idl_mpa(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			 enum rtw89_rf_path path, u8 kidx)
{
	if (rtw89_phy_read32_mask(rtwdev, R_IDL_MPA, B_IDL_MD500) == 0x1)
		_dpk_set_mdpd_para(rtwdev, 0x2);
	else if (rtw89_phy_read32_mask(rtwdev, R_IDL_MPA, B_IDL_MD530) == 0x1)
		_dpk_set_mdpd_para(rtwdev, 0x1);
	else
		_dpk_set_mdpd_para(rtwdev, 0x0);

	rtw89_phy_write32_mask(rtwdev, R_DPK_IDL, B_DPK_IDL, 0x0);
	fsleep(1000);

	_dpk_one_shot(rtwdev, phy, path, D_MDPK_IDL);
}

static u8 _dpk_order_convert(struct rtw89_dev *rtwdev)
{
	u32 order;
	u8 val;

	order = rtw89_phy_read32_mask(rtwdev, R_LDL_NORM, B_LDL_NORM_OP);

	switch (order) {
	case 0: /* (5,3,1) */
		val = 0x6;
		break;
	case 1: /* (5,3,0) */
		val = 0x2;
		break;
	case 2: /* (5,0,0) */
		val = 0x0;
		break;
	default:
		val = 0xff;
		break;
	}

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] convert MDPD order to 0x%x\n", val);

	return val;
}

static void _dpk_gain_normalize(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
				enum rtw89_rf_path path, u8 kidx, bool is_execute)
{
	static const u32 reg[RTW89_DPK_BKUP_NUM][DPK_KSET_NUM] = {
		{0x8190, 0x8194, 0x8198, 0x81a4},
		{0x81a8, 0x81c4, 0x81c8, 0x81e8}
	};
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;
	u8 cur_k_set = dpk->cur_k_set;

	if (cur_k_set >= DPK_KSET_NUM) {
		rtw89_warn(rtwdev, "DPK cur_k_set = %d\n", cur_k_set);
		cur_k_set = 2;
	}

	if (is_execute) {
		rtw89_phy_write32_mask(rtwdev, R_DPK_GN + (path << 8),
				       B_DPK_GN_AG, 0x200);
		rtw89_phy_write32_mask(rtwdev, R_DPK_GN + (path << 8),
				       B_DPK_GN_EN, 0x3);

		_dpk_one_shot(rtwdev, phy, path, D_GAIN_NORM);
	} else {
		rtw89_phy_write32_mask(rtwdev, reg[kidx][cur_k_set] + (path << 8),
				       0x0000007F, 0x5b);
	}

	dpk->bp[path][kidx].gs =
		rtw89_phy_read32_mask(rtwdev, reg[kidx][cur_k_set] + (path << 8),
				      0x0000007F);
}

static void _dpk_on(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
		    enum rtw89_rf_path path, u8 kidx)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;

	rtw89_phy_write32_mask(rtwdev, R_LOAD_COEF + (path << 8), B_LOAD_COEF_MDPD, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_LOAD_COEF + (path << 8), B_LOAD_COEF_MDPD, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_DPD_CH0A + (path << 8) + (kidx << 2),
			       B_DPD_ORDER, _dpk_order_convert(rtwdev));

	dpk->bp[path][kidx].path_ok =
		dpk->bp[path][kidx].path_ok | BIT(dpk->cur_k_set);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] S%d[%d] path_ok = 0x%x\n",
		    path, kidx, dpk->bp[path][kidx].path_ok);

	rtw89_phy_write32_mask(rtwdev, R_DPD_CH0A + (path << 8) + (kidx << 2),
			       B_DPD_MEN, dpk->bp[path][kidx].path_ok);

	_dpk_gain_normalize(rtwdev, phy, path, kidx, false);
}

static bool _dpk_main(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
		      enum rtw89_rf_path path)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;
	u8 kidx = dpk->cur_idx[path];
	u8 init_xdbm = 17;
	bool is_fail;

	_dpk_kip_control_rfc(rtwdev, path, false);
	_rfk_rf_direct_cntrl(rtwdev, path, false);
	rtw89_write_rf(rtwdev, path, RR_BBDC, RFREG_MASK, 0x03ffd);

	_dpk_rf_setting(rtwdev, path, kidx);
	_set_rx_dck(rtwdev, path, RF_DPK);

	_dpk_kip_pwr_clk_onoff(rtwdev, true);
	_dpk_kip_preset(rtwdev, phy, path, kidx);
	_dpk_txpwr_bb_force(rtwdev, path, true);
	_dpk_kip_set_txagc(rtwdev, phy, path, init_xdbm, true);
	_dpk_tpg_sel(rtwdev, path, kidx);
	is_fail = _dpk_agc(rtwdev, phy, path, kidx, init_xdbm, false);
	if (is_fail)
		goto _error;

	_dpk_idl_mpa(rtwdev, phy, path, kidx);
	_dpk_para_query(rtwdev, path, kidx);

	_dpk_on(rtwdev, phy, path, kidx);
_error:
	_dpk_kip_control_rfc(rtwdev, path, false);
	rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_MASK, RF_RX);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] S%d[%d]_K%d %s\n", path, kidx,
		    dpk->cur_k_set, is_fail ? "need Check" : "is Success");

	return is_fail;
}

static void _dpk_cal_select(struct rtw89_dev *rtwdev, bool force,
			    enum rtw89_phy_idx phy, u8 kpath,
			    enum rtw89_chanctx_idx chanctx_idx)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;
	u32 kip_bkup[RF_PATH_NUM_8851B][DPK_KIP_REG_NUM_8851B] = {};
	u32 rf_bkup[RF_PATH_NUM_8851B][DPK_RF_REG_NUM_8851B] = {};
	bool is_fail;
	u8 path;

	for (path = 0; path < RF_PATH_NUM_8851B; path++)
		dpk->cur_idx[path] = 0;

	for (path = 0; path < RF_PATH_NUM_8851B; path++) {
		if (!(kpath & BIT(path)))
			continue;
		_dpk_bkup_kip(rtwdev, dpk_kip_reg, kip_bkup, path);
		_dpk_bkup_rf(rtwdev, dpk_rf_reg, rf_bkup, path);
		_dpk_information(rtwdev, phy, path, chanctx_idx);
		_dpk_init(rtwdev, path);

		if (rtwdev->is_tssi_mode[path])
			_dpk_tssi_pause(rtwdev, path, true);
	}

	for (path = 0; path < RF_PATH_NUM_8851B; path++) {
		if (!(kpath & BIT(path)))
			continue;

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[DPK] ========= S%d[%d] DPK Start =========\n",
			    path, dpk->cur_idx[path]);

		_dpk_tssi_slope_k_onoff(rtwdev, path, false);
		_dpk_rxagc_onoff(rtwdev, path, false);
		_rfk_drf_direct_cntrl(rtwdev, path, false);
		_dpk_bb_afe_setting(rtwdev, path);

		is_fail = _dpk_main(rtwdev, phy, path);
		_dpk_onoff(rtwdev, path, is_fail);
	}

	for (path = 0; path < RF_PATH_NUM_8851B; path++) {
		if (!(kpath & BIT(path)))
			continue;

		_dpk_kip_restore(rtwdev, phy, path);
		_dpk_reload_kip(rtwdev, dpk_kip_reg, kip_bkup, path);
		_dpk_reload_rf(rtwdev, dpk_rf_reg, rf_bkup, path);
		_dpk_bb_afe_restore(rtwdev, path);
		_dpk_rxagc_onoff(rtwdev, path, true);
		_dpk_tssi_slope_k_onoff(rtwdev, path, true);
		if (rtwdev->is_tssi_mode[path])
			_dpk_tssi_pause(rtwdev, path, false);
	}

	_dpk_kip_pwr_clk_onoff(rtwdev, false);
}

static void _dpk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy, bool force,
		 enum rtw89_chanctx_idx chanctx_idx)
{
	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DPK] ****** 8851B DPK Start (Ver: 0x%x, Cv: %d) ******\n",
		    DPK_VER_8851B, rtwdev->hal.cv);

	_dpk_cal_select(rtwdev, force, phy, _kpath(rtwdev, phy), chanctx_idx);
}

static void _dpk_track(struct rtw89_dev *rtwdev)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;
	s8 txagc_bb, txagc_bb_tp, txagc_ofst;
	s16 pwsf_tssi_ofst;
	s8 delta_ther = 0;
	u8 path, kidx;
	u8 txagc_rf;
	u8 cur_ther;

	for (path = 0; path < RF_PATH_NUM_8851B; path++) {
		kidx = dpk->cur_idx[path];

		rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK,
			    "[DPK_TRK] ================[S%d[%d] (CH %d)]================\n",
			    path, kidx, dpk->bp[path][kidx].ch);

		txagc_rf = rtw89_phy_read32_mask(rtwdev, R_TXAGC_BB + (path << 13),
						 B_TXAGC_RF);
		txagc_bb = rtw89_phy_read32_mask(rtwdev, R_TXAGC_BB + (path << 13),
						 MASKBYTE2);
		txagc_bb_tp = rtw89_phy_read32_mask(rtwdev, R_TXAGC_BTP + (path << 13),
						    B_TXAGC_BTP);

		rtw89_phy_write32_mask(rtwdev, R_KIP_RPT + (path << 8),
				       B_KIP_RPT_SEL, 0xf);
		cur_ther = rtw89_phy_read32_mask(rtwdev, R_RPT_PER + (path << 8),
						 B_RPT_PER_TH);
		txagc_ofst = rtw89_phy_read32_mask(rtwdev, R_RPT_PER + (path << 8),
						   B_RPT_PER_OF);
		pwsf_tssi_ofst = rtw89_phy_read32_mask(rtwdev, R_RPT_PER + (path << 8),
						       B_RPT_PER_TSSI);
		pwsf_tssi_ofst = sign_extend32(pwsf_tssi_ofst, 12);

		delta_ther = cur_ther - dpk->bp[path][kidx].ther_dpk;

		delta_ther = delta_ther * 2 / 3;

		rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK,
			    "[DPK_TRK] extra delta_ther = %d (0x%x / 0x%x@k)\n",
			    delta_ther, cur_ther, dpk->bp[path][kidx].ther_dpk);

		rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK,
			    "[DPK_TRK] delta_txagc = %d (0x%x / 0x%x@k)\n",
			    txagc_rf - dpk->bp[path][kidx].txagc_dpk,
			    txagc_rf, dpk->bp[path][kidx].txagc_dpk);

		rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK,
			    "[DPK_TRK] txagc_offset / pwsf_tssi_ofst = 0x%x / %+d\n",
			    txagc_ofst, pwsf_tssi_ofst);

		rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK,
			    "[DPK_TRK] txagc_bb_tp / txagc_bb = 0x%x / 0x%x\n",
			    txagc_bb_tp, txagc_bb);

		if (rtw89_phy_read32_mask(rtwdev, R_IDL_MPA, B_IDL_DN) == 0x0 &&
		    txagc_rf != 0) {
			rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK,
				    "[DPK_TRK] New pwsf = 0x%x\n", 0x78 - delta_ther);

			rtw89_phy_write32_mask(rtwdev,
					       R_DPD_BND + (path << 8) + (kidx << 2),
					       0x07FC0000, 0x78 - delta_ther);
		}
	}
}

static void _rck(struct rtw89_dev *rtwdev, enum rtw89_rf_path path)
{
	u32 rf_reg5;
	u32 rck_val;
	u32 val;
	int ret;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[RCK] ====== S%d RCK ======\n", path);

	rf_reg5 = rtw89_read_rf(rtwdev, path, RR_RSV1, RFREG_MASK);

	rtw89_write_rf(rtwdev, path, RR_RSV1, RR_RSV1_RST, 0x0);
	rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_MASK, RR_MOD_V_RX);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[RCK] RF0x00 = 0x%05x\n",
		    rtw89_read_rf(rtwdev, path, RR_MOD, RFREG_MASK));

	/* RCK trigger */
	rtw89_write_rf(rtwdev, path, RR_RCKC, RFREG_MASK, 0x00240);

	ret = read_poll_timeout_atomic(rtw89_read_rf, val, val, 2, 30,
				       false, rtwdev, path, RR_RCKS, BIT(3));

	rck_val = rtw89_read_rf(rtwdev, path, RR_RCKC, RR_RCKC_CA);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[RCK] rck_val = 0x%x, ret = %d\n",
		    rck_val, ret);

	rtw89_write_rf(rtwdev, path, RR_RCKC, RFREG_MASK, rck_val);
	rtw89_write_rf(rtwdev, path, RR_RSV1, RFREG_MASK, rf_reg5);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[RCK] RF 0x1b = 0x%x\n",
		    rtw89_read_rf(rtwdev, path, RR_RCKC, RFREG_MASK));
}

static void _tssi_set_sys(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			  enum rtw89_rf_path path, const struct rtw89_chan *chan)
{
	enum rtw89_band band = chan->band_type;

	rtw89_rfk_parser(rtwdev, &rtw8851b_tssi_sys_defs_tbl);

	rtw89_rfk_parser_by_cond(rtwdev, band == RTW89_BAND_2G,
				 &rtw8851b_tssi_sys_a_defs_2g_tbl,
				 &rtw8851b_tssi_sys_a_defs_5g_tbl);
}

static void _tssi_ini_txpwr_ctrl_bb(struct rtw89_dev *rtwdev,
				    enum rtw89_phy_idx phy,
				    enum rtw89_rf_path path)
{
	rtw89_rfk_parser(rtwdev, &rtw8851b_tssi_init_txpwr_defs_a_tbl);
}

static void _tssi_ini_txpwr_ctrl_bb_he_tb(struct rtw89_dev *rtwdev,
					  enum rtw89_phy_idx phy,
					  enum rtw89_rf_path path)
{
	rtw89_rfk_parser(rtwdev, &rtw8851b_tssi_init_txpwr_he_tb_defs_a_tbl);
}

static void _tssi_set_dck(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			  enum rtw89_rf_path path)
{
	rtw89_rfk_parser(rtwdev, &rtw8851b_tssi_dck_defs_a_tbl);
}

static void _tssi_set_tmeter_tbl(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
				 enum rtw89_rf_path path, const struct rtw89_chan *chan)
{
#define RTW8851B_TSSI_GET_VAL(ptr, idx)			\
({							\
	s8 *__ptr = (ptr);				\
	u8 __idx = (idx), __i, __v;			\
	u32 __val = 0;					\
	for (__i = 0; __i < 4; __i++) {			\
		__v = (__ptr[__idx + __i]);		\
		__val |= (__v << (8 * __i));		\
	}						\
	__val;						\
})
	struct rtw89_tssi_info *tssi_info = &rtwdev->tssi;
	u8 ch = chan->channel;
	u8 subband = chan->subband_type;
	const s8 *thm_up_a = NULL;
	const s8 *thm_down_a = NULL;
	u8 thermal = 0xff;
	s8 thm_ofst[64] = {0};
	u32 tmp = 0;
	u8 i, j;

	switch (subband) {
	default:
	case RTW89_CH_2G:
		thm_up_a = rtw89_8851b_trk_cfg.delta_swingidx_2ga_p;
		thm_down_a = rtw89_8851b_trk_cfg.delta_swingidx_2ga_n;
		break;
	case RTW89_CH_5G_BAND_1:
		thm_up_a = rtw89_8851b_trk_cfg.delta_swingidx_5ga_p[0];
		thm_down_a = rtw89_8851b_trk_cfg.delta_swingidx_5ga_n[0];
		break;
	case RTW89_CH_5G_BAND_3:
		thm_up_a = rtw89_8851b_trk_cfg.delta_swingidx_5ga_p[1];
		thm_down_a = rtw89_8851b_trk_cfg.delta_swingidx_5ga_n[1];
		break;
	case RTW89_CH_5G_BAND_4:
		thm_up_a = rtw89_8851b_trk_cfg.delta_swingidx_5ga_p[2];
		thm_down_a = rtw89_8851b_trk_cfg.delta_swingidx_5ga_n[2];
		break;
	}

	if (path == RF_PATH_A) {
		thermal = tssi_info->thermal[RF_PATH_A];

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI] ch=%d thermal_pathA=0x%x\n", ch, thermal);

		rtw89_phy_write32_mask(rtwdev, R_P0_TMETER, B_P0_TMETER_DIS, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_P0_TMETER, B_P0_TMETER_TRK, 0x1);

		if (thermal == 0xff) {
			rtw89_phy_write32_mask(rtwdev, R_P0_TMETER, B_P0_TMETER, 32);
			rtw89_phy_write32_mask(rtwdev, R_P0_RFCTM, B_P0_RFCTM_VAL, 32);

			for (i = 0; i < 64; i += 4) {
				rtw89_phy_write32(rtwdev, R_P0_TSSI_BASE + i, 0x0);

				rtw89_debug(rtwdev, RTW89_DBG_TSSI,
					    "[TSSI] write 0x%x val=0x%08x\n",
					    R_P0_TSSI_BASE + i, 0x0);
			}

		} else {
			rtw89_phy_write32_mask(rtwdev, R_P0_TMETER, B_P0_TMETER,
					       thermal);
			rtw89_phy_write32_mask(rtwdev, R_P0_RFCTM, B_P0_RFCTM_VAL,
					       thermal);

			i = 0;
			for (j = 0; j < 32; j++)
				thm_ofst[j] = i < DELTA_SWINGIDX_SIZE ?
					      -thm_down_a[i++] :
					      -thm_down_a[DELTA_SWINGIDX_SIZE - 1];

			i = 1;
			for (j = 63; j >= 32; j--)
				thm_ofst[j] = i < DELTA_SWINGIDX_SIZE ?
					      thm_up_a[i++] :
					      thm_up_a[DELTA_SWINGIDX_SIZE - 1];

			for (i = 0; i < 64; i += 4) {
				tmp = RTW8851B_TSSI_GET_VAL(thm_ofst, i);
				rtw89_phy_write32(rtwdev, R_P0_TSSI_BASE + i, tmp);

				rtw89_debug(rtwdev, RTW89_DBG_TSSI,
					    "[TSSI] write 0x%x val=0x%08x\n",
					    0x5c00 + i, tmp);
			}
		}
		rtw89_phy_write32_mask(rtwdev, R_P0_RFCTM, R_P0_RFCTM_RDY, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_P0_RFCTM, R_P0_RFCTM_RDY, 0x0);
	}
#undef RTW8851B_TSSI_GET_VAL
}

static void _tssi_set_dac_gain_tbl(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
				   enum rtw89_rf_path path)
{
	rtw89_rfk_parser(rtwdev, &rtw8851b_tssi_dac_gain_defs_a_tbl);
}

static void _tssi_slope_cal_org(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
				enum rtw89_rf_path path, const struct rtw89_chan *chan)
{
	enum rtw89_band band = chan->band_type;

	rtw89_rfk_parser_by_cond(rtwdev, band == RTW89_BAND_2G,
				 &rtw8851b_tssi_slope_a_defs_2g_tbl,
				 &rtw8851b_tssi_slope_a_defs_5g_tbl);
}

static void _tssi_alignment_default(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
				    enum rtw89_rf_path path, bool all,
				    const struct rtw89_chan *chan)
{
	enum rtw89_band band = chan->band_type;

	rtw89_rfk_parser_by_cond(rtwdev, band == RTW89_BAND_2G,
				 &rtw8851b_tssi_align_a_2g_defs_tbl,
				 &rtw8851b_tssi_align_a_5g_defs_tbl);
}

static void _tssi_set_tssi_slope(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
				 enum rtw89_rf_path path)
{
	rtw89_rfk_parser(rtwdev, &rtw8851b_tssi_slope_defs_a_tbl);
}

static void _tssi_set_tssi_track(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
				 enum rtw89_rf_path path)
{
	rtw89_rfk_parser(rtwdev, &rtw8851b_tssi_track_defs_a_tbl);
}

static void _tssi_set_txagc_offset_mv_avg(struct rtw89_dev *rtwdev,
					  enum rtw89_phy_idx phy,
					  enum rtw89_rf_path path)
{
	rtw89_rfk_parser(rtwdev, &rtw8851b_tssi_mv_avg_defs_a_tbl);
}

static void _tssi_enable(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy)
{
	_tssi_set_tssi_track(rtwdev, phy, RF_PATH_A);
	_tssi_set_txagc_offset_mv_avg(rtwdev, phy, RF_PATH_A);

	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_MV_AVG, B_P0_TSSI_MV_CLR, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_AVG, B_P0_TSSI_EN, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_AVG, B_P0_TSSI_EN, 0x1);
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_TXGA_V1, RR_TXGA_V1_TRK_EN, 0x1);

	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_TRK, B_P0_TSSI_OFT_EN, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_TRK, B_P0_TSSI_RFC, 0x3);
	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_TRK, B_P0_TSSI_OFT, 0xc0);
	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_TRK, B_P0_TSSI_OFT_EN, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_TRK, B_P0_TSSI_OFT_EN, 0x1);

	rtwdev->is_tssi_mode[RF_PATH_A] = true;
}

static void _tssi_disable(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy)
{
	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_AVG, B_P0_TSSI_EN, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_TRK, B_P0_TSSI_OFT_EN, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_TRK, B_P0_TSSI_OFT_EN, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_TRK, B_P0_TSSI_OFT_EN, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_MV_AVG, B_P0_TSSI_MV_CLR, 0x1);

	rtwdev->is_tssi_mode[RF_PATH_A] = false;
}

static u32 _tssi_get_cck_group(struct rtw89_dev *rtwdev, u8 ch)
{
	switch (ch) {
	case 1 ... 2:
		return 0;
	case 3 ... 5:
		return 1;
	case 6 ... 8:
		return 2;
	case 9 ... 11:
		return 3;
	case 12 ... 13:
		return 4;
	case 14:
		return 5;
	}

	return 0;
}

#define TSSI_EXTRA_GROUP_BIT (BIT(31))
#define TSSI_EXTRA_GROUP(idx) (TSSI_EXTRA_GROUP_BIT | (idx))
#define IS_TSSI_EXTRA_GROUP(group) ((group) & TSSI_EXTRA_GROUP_BIT)
#define TSSI_EXTRA_GET_GROUP_IDX1(group) ((group) & ~TSSI_EXTRA_GROUP_BIT)
#define TSSI_EXTRA_GET_GROUP_IDX2(group) (TSSI_EXTRA_GET_GROUP_IDX1(group) + 1)

static u32 _tssi_get_ofdm_group(struct rtw89_dev *rtwdev, u8 ch)
{
	switch (ch) {
	case 1 ... 2:
		return 0;
	case 3 ... 5:
		return 1;
	case 6 ... 8:
		return 2;
	case 9 ... 11:
		return 3;
	case 12 ... 14:
		return 4;
	case 36 ... 40:
		return 5;
	case 41 ... 43:
		return TSSI_EXTRA_GROUP(5);
	case 44 ... 48:
		return 6;
	case 49 ... 51:
		return TSSI_EXTRA_GROUP(6);
	case 52 ... 56:
		return 7;
	case 57 ... 59:
		return TSSI_EXTRA_GROUP(7);
	case 60 ... 64:
		return 8;
	case 100 ... 104:
		return 9;
	case 105 ... 107:
		return TSSI_EXTRA_GROUP(9);
	case 108 ... 112:
		return 10;
	case 113 ... 115:
		return TSSI_EXTRA_GROUP(10);
	case 116 ... 120:
		return 11;
	case 121 ... 123:
		return TSSI_EXTRA_GROUP(11);
	case 124 ... 128:
		return 12;
	case 129 ... 131:
		return TSSI_EXTRA_GROUP(12);
	case 132 ... 136:
		return 13;
	case 137 ... 139:
		return TSSI_EXTRA_GROUP(13);
	case 140 ... 144:
		return 14;
	case 149 ... 153:
		return 15;
	case 154 ... 156:
		return TSSI_EXTRA_GROUP(15);
	case 157 ... 161:
		return 16;
	case 162 ... 164:
		return TSSI_EXTRA_GROUP(16);
	case 165 ... 169:
		return 17;
	case 170 ... 172:
		return TSSI_EXTRA_GROUP(17);
	case 173 ... 177:
		return 18;
	}

	return 0;
}

static u32 _tssi_get_trim_group(struct rtw89_dev *rtwdev, u8 ch)
{
	switch (ch) {
	case 1 ... 8:
		return 0;
	case 9 ... 14:
		return 1;
	case 36 ... 48:
		return 2;
	case 52 ... 64:
		return 3;
	case 100 ... 112:
		return 4;
	case 116 ... 128:
		return 5;
	case 132 ... 144:
		return 6;
	case 149 ... 177:
		return 7;
	}

	return 0;
}

static s8 _tssi_get_ofdm_de(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			    enum rtw89_rf_path path, const struct rtw89_chan *chan)
{
	struct rtw89_tssi_info *tssi_info = &rtwdev->tssi;
	u32 gidx, gidx_1st, gidx_2nd;
	u8 ch = chan->channel;
	s8 de_1st;
	s8 de_2nd;
	s8 val;

	gidx = _tssi_get_ofdm_group(rtwdev, ch);

	rtw89_debug(rtwdev, RTW89_DBG_TSSI,
		    "[TSSI][TRIM]: path=%d mcs group_idx=0x%x\n", path, gidx);

	if (IS_TSSI_EXTRA_GROUP(gidx)) {
		gidx_1st = TSSI_EXTRA_GET_GROUP_IDX1(gidx);
		gidx_2nd = TSSI_EXTRA_GET_GROUP_IDX2(gidx);
		de_1st = tssi_info->tssi_mcs[path][gidx_1st];
		de_2nd = tssi_info->tssi_mcs[path][gidx_2nd];
		val = (de_1st + de_2nd) / 2;

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI][TRIM]: path=%d mcs de=%d 1st=%d 2nd=%d\n",
			    path, val, de_1st, de_2nd);
	} else {
		val = tssi_info->tssi_mcs[path][gidx];

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI][TRIM]: path=%d mcs de=%d\n", path, val);
	}

	return val;
}

static s8 _tssi_get_ofdm_trim_de(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
				 enum rtw89_rf_path path, const struct rtw89_chan *chan)
{
	struct rtw89_tssi_info *tssi_info = &rtwdev->tssi;
	u32 tgidx, tgidx_1st, tgidx_2nd;
	u8 ch = chan->channel;
	s8 tde_1st;
	s8 tde_2nd;
	s8 val;

	tgidx = _tssi_get_trim_group(rtwdev, ch);

	rtw89_debug(rtwdev, RTW89_DBG_TSSI,
		    "[TSSI][TRIM]: path=%d mcs trim_group_idx=0x%x\n",
		    path, tgidx);

	if (IS_TSSI_EXTRA_GROUP(tgidx)) {
		tgidx_1st = TSSI_EXTRA_GET_GROUP_IDX1(tgidx);
		tgidx_2nd = TSSI_EXTRA_GET_GROUP_IDX2(tgidx);
		tde_1st = tssi_info->tssi_trim[path][tgidx_1st];
		tde_2nd = tssi_info->tssi_trim[path][tgidx_2nd];
		val = (tde_1st + tde_2nd) / 2;

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI][TRIM]: path=%d mcs trim_de=%d 1st=%d 2nd=%d\n",
			    path, val, tde_1st, tde_2nd);
	} else {
		val = tssi_info->tssi_trim[path][tgidx];

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI][TRIM]: path=%d mcs trim_de=%d\n",
			    path, val);
	}

	return val;
}

static void _tssi_set_efuse_to_de(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
				  const struct rtw89_chan *chan)
{
	struct rtw89_tssi_info *tssi_info = &rtwdev->tssi;
	u8 ch = chan->channel;
	u8 gidx;
	s8 ofdm_de;
	s8 trim_de;
	s32 val;
	u32 i;

	rtw89_debug(rtwdev, RTW89_DBG_TSSI, "[TSSI][TRIM]: phy=%d ch=%d\n",
		    phy, ch);

	for (i = RF_PATH_A; i < RTW8851B_TSSI_PATH_NR; i++) {
		gidx = _tssi_get_cck_group(rtwdev, ch);
		trim_de = _tssi_get_ofdm_trim_de(rtwdev, phy, i, chan);
		val = tssi_info->tssi_cck[i][gidx] + trim_de;

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI][TRIM]: path=%d cck[%d]=0x%x trim=0x%x\n",
			    i, gidx, tssi_info->tssi_cck[i][gidx], trim_de);

		rtw89_phy_write32_mask(rtwdev, _tssi_de_cck_long[i], _TSSI_DE_MASK, val);
		rtw89_phy_write32_mask(rtwdev, _tssi_de_cck_short[i], _TSSI_DE_MASK, val);

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI] Set TSSI CCK DE 0x%x[21:12]=0x%x\n",
			    _tssi_de_cck_long[i],
			    rtw89_phy_read32_mask(rtwdev, _tssi_de_cck_long[i],
						  _TSSI_DE_MASK));

		ofdm_de = _tssi_get_ofdm_de(rtwdev, phy, i, chan);
		trim_de = _tssi_get_ofdm_trim_de(rtwdev, phy, i, chan);
		val = ofdm_de + trim_de;

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI][TRIM]: path=%d mcs=0x%x trim=0x%x\n",
			    i, ofdm_de, trim_de);

		rtw89_phy_write32_mask(rtwdev, _tssi_de_mcs_20m[i], _TSSI_DE_MASK, val);
		rtw89_phy_write32_mask(rtwdev, _tssi_de_mcs_40m[i], _TSSI_DE_MASK, val);
		rtw89_phy_write32_mask(rtwdev, _tssi_de_mcs_80m[i], _TSSI_DE_MASK, val);
		rtw89_phy_write32_mask(rtwdev, _tssi_de_mcs_80m_80m[i], _TSSI_DE_MASK, val);
		rtw89_phy_write32_mask(rtwdev, _tssi_de_mcs_5m[i], _TSSI_DE_MASK, val);
		rtw89_phy_write32_mask(rtwdev, _tssi_de_mcs_10m[i], _TSSI_DE_MASK, val);

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI] Set TSSI MCS DE 0x%x[21:12]=0x%x\n",
			    _tssi_de_mcs_20m[i],
			    rtw89_phy_read32_mask(rtwdev, _tssi_de_mcs_20m[i],
						  _TSSI_DE_MASK));
	}
}

static void _tssi_alimentk_dump_result(struct rtw89_dev *rtwdev, enum rtw89_rf_path path)
{
	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[TSSI PA K]\n0x%x = 0x%08x\n0x%x = 0x%08x\n0x%x = 0x%08x\n0x%x = 0x%08x\n"
		    "0x%x = 0x%08x\n0x%x = 0x%08x\n0x%x = 0x%08x\n0x%x = 0x%08x\n",
		    R_TSSI_PA_K1 + (path << 13),
		    rtw89_phy_read32_mask(rtwdev, R_TSSI_PA_K1 + (path << 13), MASKDWORD),
		    R_TSSI_PA_K2 + (path << 13),
		    rtw89_phy_read32_mask(rtwdev, R_TSSI_PA_K2 + (path << 13), MASKDWORD),
		    R_P0_TSSI_ALIM1 + (path << 13),
		    rtw89_phy_read32_mask(rtwdev, R_P0_TSSI_ALIM1 + (path << 13), MASKDWORD),
		    R_P0_TSSI_ALIM3 + (path << 13),
		    rtw89_phy_read32_mask(rtwdev, R_P0_TSSI_ALIM3 + (path << 13), MASKDWORD),
		    R_TSSI_PA_K5 + (path << 13),
		    rtw89_phy_read32_mask(rtwdev, R_TSSI_PA_K5 + (path << 13), MASKDWORD),
		    R_P0_TSSI_ALIM2 + (path << 13),
		    rtw89_phy_read32_mask(rtwdev, R_P0_TSSI_ALIM2 + (path << 13), MASKDWORD),
		    R_P0_TSSI_ALIM4 + (path << 13),
		    rtw89_phy_read32_mask(rtwdev, R_P0_TSSI_ALIM4 + (path << 13), MASKDWORD),
		    R_TSSI_PA_K8 + (path << 13),
		    rtw89_phy_read32_mask(rtwdev, R_TSSI_PA_K8 + (path << 13), MASKDWORD));
}

static void _tssi_alimentk_done(struct rtw89_dev *rtwdev,
				enum rtw89_phy_idx phy, enum rtw89_rf_path path,
				const struct rtw89_chan *chan)
{
	struct rtw89_tssi_info *tssi_info = &rtwdev->tssi;
	u8 channel = chan->channel;
	u8 band;

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "======>%s   phy=%d   path=%d\n", __func__, phy, path);

	if (channel >= 1 && channel <= 14)
		band = TSSI_ALIMK_2G;
	else if (channel >= 36 && channel <= 64)
		band = TSSI_ALIMK_5GL;
	else if (channel >= 100 && channel <= 144)
		band = TSSI_ALIMK_5GM;
	else if (channel >= 149 && channel <= 177)
		band = TSSI_ALIMK_5GH;
	else
		band = TSSI_ALIMK_2G;

	if (tssi_info->alignment_done[path][band]) {
		rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_ALIM1 + (path << 13), MASKDWORD,
				       tssi_info->alignment_value[path][band][0]);
		rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_ALIM3 + (path << 13), MASKDWORD,
				       tssi_info->alignment_value[path][band][1]);
		rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_ALIM2 + (path << 13), MASKDWORD,
				       tssi_info->alignment_value[path][band][2]);
		rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_ALIM4 + (path << 13), MASKDWORD,
				       tssi_info->alignment_value[path][band][3]);
	}

	_tssi_alimentk_dump_result(rtwdev, path);
}

static void rtw8851b_by_rate_dpd(struct rtw89_dev *rtwdev)
{
	rtw89_write32_mask(rtwdev, R_AX_PWR_SWING_OTHER_CTRL0,
			   B_AX_CFIR_BY_RATE_OFF_MASK, 0x21861);
}

void rtw8851b_dpk_init(struct rtw89_dev *rtwdev)
{
	rtw8851b_by_rate_dpd(rtwdev);
}

void rtw8851b_aack(struct rtw89_dev *rtwdev)
{
	u32 tmp05, tmpd3, ib[4];
	u32 tmp;
	int ret;
	int rek;
	int i;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[LCK]DO AACK\n");

	tmp05 = rtw89_read_rf(rtwdev, RF_PATH_A, RR_RSV1, RFREG_MASK);
	tmpd3 = rtw89_read_rf(rtwdev, RF_PATH_A, RR_LCK_TRG, RFREG_MASK);
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_MOD, RR_MOD_MASK, 0x3);
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_RSV1, RFREG_MASK, 0x0);
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_LCK_TRG, RR_LCK_ST, 0x0);

	for (rek = 0; rek < 4; rek++) {
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_AACK, RFREG_MASK, 0x8201e);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_AACK, RFREG_MASK, 0x8201f);
		fsleep(100);

		ret = read_poll_timeout_atomic(rtw89_read_rf, tmp, tmp,
					       1, 1000, false,
					       rtwdev, RF_PATH_A, 0xd0, BIT(16));
		if (ret)
			rtw89_warn(rtwdev, "[LCK]AACK timeout\n");

		rtw89_write_rf(rtwdev, RF_PATH_A, RR_VCI, RR_VCI_ON, 0x1);
		for (i = 0; i < 4; i++) {
			rtw89_write_rf(rtwdev, RF_PATH_A, RR_VCO, RR_VCO_SEL, i);
			ib[i] = rtw89_read_rf(rtwdev, RF_PATH_A, RR_IBD, RR_IBD_VAL);
		}
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_VCI, RR_VCI_ON, 0x0);

		if (ib[0] != 0 && ib[1] != 0 && ib[2] != 0 && ib[3] != 0)
			break;
	}

	if (rek != 0)
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[LCK]AACK rek = %d\n", rek);

	rtw89_write_rf(rtwdev, RF_PATH_A, RR_RSV1, RFREG_MASK, tmp05);
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_LCK_TRG, RFREG_MASK, tmpd3);
}

static void _lck_keep_thermal(struct rtw89_dev *rtwdev)
{
	struct rtw89_lck_info *lck = &rtwdev->lck;

	lck->thermal[RF_PATH_A] =
		ewma_thermal_read(&rtwdev->phystat.avg_thermal[RF_PATH_A]);
	rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK,
		    "[LCK] path=%d thermal=0x%x", RF_PATH_A, lck->thermal[RF_PATH_A]);
}

static void rtw8851b_lck(struct rtw89_dev *rtwdev)
{
	u32 tmp05, tmp18, tmpd3;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[LCK]DO LCK\n");

	tmp05 = rtw89_read_rf(rtwdev, RF_PATH_A, RR_RSV1, RFREG_MASK);
	tmp18 = rtw89_read_rf(rtwdev, RF_PATH_A, RR_CFGCH, RFREG_MASK);
	tmpd3 = rtw89_read_rf(rtwdev, RF_PATH_A, RR_LCK_TRG, RFREG_MASK);

	rtw89_write_rf(rtwdev, RF_PATH_A, RR_MOD, RR_MOD_MASK, 0x3);
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_RSV1, RFREG_MASK, 0x0);
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_LCK_TRG, RR_LCK_TRGSEL, 0x1);

	_set_ch(rtwdev, tmp18);
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_LCK_TRG, RFREG_MASK, tmpd3);
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_RSV1, RFREG_MASK, tmp05);

	_lck_keep_thermal(rtwdev);
}

#define RTW8851B_LCK_TH 8

void rtw8851b_lck_track(struct rtw89_dev *rtwdev)
{
	struct rtw89_lck_info *lck = &rtwdev->lck;
	u8 cur_thermal;
	int delta;

	cur_thermal =
		ewma_thermal_read(&rtwdev->phystat.avg_thermal[RF_PATH_A]);
	delta = abs((int)cur_thermal - lck->thermal[RF_PATH_A]);

	rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK,
		    "[LCK] path=%d current thermal=0x%x delta=0x%x\n",
		    RF_PATH_A, cur_thermal, delta);

	if (delta >= RTW8851B_LCK_TH) {
		rtw8851b_aack(rtwdev);
		rtw8851b_lck(rtwdev);
	}
}

void rtw8851b_lck_init(struct rtw89_dev *rtwdev)
{
	_lck_keep_thermal(rtwdev);
}

void rtw8851b_rck(struct rtw89_dev *rtwdev)
{
	_rck(rtwdev, RF_PATH_A);
}

void rtw8851b_dack(struct rtw89_dev *rtwdev)
{
	_dac_cal(rtwdev, false);
}

void rtw8851b_iqk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx,
		  enum rtw89_chanctx_idx chanctx_idx)
{
	u8 phy_map = rtw89_btc_phymap(rtwdev, phy_idx, 0, chanctx_idx);
	u32 tx_en;

	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_IQK, BTC_WRFK_START);
	rtw89_chip_stop_sch_tx(rtwdev, phy_idx, &tx_en, RTW89_SCH_TX_SEL_ALL);
	_wait_rx_mode(rtwdev, _kpath(rtwdev, phy_idx));

	_iqk_init(rtwdev);
	_iqk(rtwdev, phy_idx, false, chanctx_idx);

	rtw89_chip_resume_sch_tx(rtwdev, phy_idx, tx_en);
	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_IQK, BTC_WRFK_STOP);
}

void rtw8851b_rx_dck(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx,
		     enum rtw89_chanctx_idx chanctx_idx)
{
	u8 phy_map = rtw89_btc_phymap(rtwdev, phy_idx, 0, chanctx_idx);
	u32 tx_en;

	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_RXDCK, BTC_WRFK_START);
	rtw89_chip_stop_sch_tx(rtwdev, phy_idx, &tx_en, RTW89_SCH_TX_SEL_ALL);
	_wait_rx_mode(rtwdev, _kpath(rtwdev, phy_idx));

	_rx_dck(rtwdev, phy_idx, false, chanctx_idx);

	rtw89_chip_resume_sch_tx(rtwdev, phy_idx, tx_en);
	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_RXDCK, BTC_WRFK_STOP);
}

void rtw8851b_dpk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx,
		  enum rtw89_chanctx_idx chanctx_idx)
{
	u8 phy_map = rtw89_btc_phymap(rtwdev, phy_idx, 0, chanctx_idx);
	u32 tx_en;

	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_DPK, BTC_WRFK_START);
	rtw89_chip_stop_sch_tx(rtwdev, phy_idx, &tx_en, RTW89_SCH_TX_SEL_ALL);
	_wait_rx_mode(rtwdev, _kpath(rtwdev, phy_idx));

	rtwdev->dpk.is_dpk_enable = true;
	rtwdev->dpk.is_dpk_reload_en = false;
	_dpk(rtwdev, phy_idx, false, chanctx_idx);

	rtw89_chip_resume_sch_tx(rtwdev, phy_idx, tx_en);
	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_DPK, BTC_WRFK_STOP);
}

void rtw8851b_dpk_track(struct rtw89_dev *rtwdev)
{
	_dpk_track(rtwdev);
}

void rtw8851b_tssi(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
		   bool hwtx_en, enum rtw89_chanctx_idx chanctx_idx)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, chanctx_idx);
	u8 phy_map = rtw89_btc_phymap(rtwdev, phy, RF_A, chanctx_idx);
	u8 i;

	rtw89_debug(rtwdev, RTW89_DBG_TSSI, "[TSSI] %s: phy=%d\n", __func__, phy);
	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_IQK, BTC_WRFK_ONESHOT_START);

	_tssi_disable(rtwdev, phy);

	for (i = RF_PATH_A; i < RF_PATH_NUM_8851B; i++) {
		_tssi_set_sys(rtwdev, phy, i, chan);
		_tssi_ini_txpwr_ctrl_bb(rtwdev, phy, i);
		_tssi_ini_txpwr_ctrl_bb_he_tb(rtwdev, phy, i);
		_tssi_set_dck(rtwdev, phy, i);
		_tssi_set_tmeter_tbl(rtwdev, phy, i, chan);
		_tssi_set_dac_gain_tbl(rtwdev, phy, i);
		_tssi_slope_cal_org(rtwdev, phy, i, chan);
		_tssi_alignment_default(rtwdev, phy, i, true, chan);
		_tssi_set_tssi_slope(rtwdev, phy, i);
	}

	_tssi_enable(rtwdev, phy);
	_tssi_set_efuse_to_de(rtwdev, phy, chan);

	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_IQK, BTC_WRFK_ONESHOT_STOP);
}

void rtw8851b_tssi_scan(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			const struct rtw89_chan *chan)
{
	u8 channel = chan->channel;
	u32 i;

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "======>%s   phy=%d  channel=%d\n", __func__, phy, channel);

	_tssi_disable(rtwdev, phy);

	for (i = RF_PATH_A; i < RF_PATH_NUM_8851B; i++) {
		_tssi_set_sys(rtwdev, phy, i, chan);
		_tssi_set_tmeter_tbl(rtwdev, phy, i, chan);
		_tssi_slope_cal_org(rtwdev, phy, i, chan);
		_tssi_alignment_default(rtwdev, phy, i, true, chan);
	}

	_tssi_enable(rtwdev, phy);
	_tssi_set_efuse_to_de(rtwdev, phy, chan);
}

static void rtw8851b_tssi_default_txagc(struct rtw89_dev *rtwdev,
					enum rtw89_phy_idx phy, bool enable,
					enum rtw89_chanctx_idx chanctx_idx)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, chanctx_idx);
	u8 channel = chan->channel;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "======> %s   ch=%d\n",
		    __func__, channel);

	if (enable)
		return;

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "======>%s 1 SCAN_END Set 0x5818[7:0]=0x%x\n",
		    __func__,
		    rtw89_phy_read32_mask(rtwdev, R_P0_TSSI_TRK, B_P0_TSSI_OFT));

	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_TRK, B_P0_TSSI_OFT, 0xc0);
	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_TRK, B_P0_TSSI_OFT_EN, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_TRK, B_P0_TSSI_OFT_EN, 0x1);

	_tssi_alimentk_done(rtwdev, phy, RF_PATH_A, chan);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "======>%s 2 SCAN_END Set 0x5818[7:0]=0x%x\n",
		    __func__,
		    rtw89_phy_read32_mask(rtwdev, R_P0_TSSI_TRK, B_P0_TSSI_OFT));

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "======> %s   SCAN_END\n", __func__);
}

void rtw8851b_wifi_scan_notify(struct rtw89_dev *rtwdev, bool scan_start,
			       enum rtw89_phy_idx phy_idx,
			       enum rtw89_chanctx_idx chanctx_idx)
{
	if (scan_start)
		rtw8851b_tssi_default_txagc(rtwdev, phy_idx, true, chanctx_idx);
	else
		rtw8851b_tssi_default_txagc(rtwdev, phy_idx, false, chanctx_idx);
}

static void _bw_setting(struct rtw89_dev *rtwdev, enum rtw89_rf_path path,
			enum rtw89_bandwidth bw, bool dav)
{
	u32 reg18_addr = dav ? RR_CFGCH : RR_CFGCH_V1;
	u32 rf_reg18;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[RFK]===> %s\n", __func__);

	rf_reg18 = rtw89_read_rf(rtwdev, path, reg18_addr, RFREG_MASK);
	if (rf_reg18 == INV_RF_DATA) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[RFK]Invalid RF_0x18 for Path-%d\n", path);
		return;
	}
	rf_reg18 &= ~RR_CFGCH_BW;

	switch (bw) {
	case RTW89_CHANNEL_WIDTH_5:
	case RTW89_CHANNEL_WIDTH_10:
	case RTW89_CHANNEL_WIDTH_20:
		rf_reg18 |= FIELD_PREP(RR_CFGCH_BW, CFGCH_BW_20M);
		break;
	case RTW89_CHANNEL_WIDTH_40:
		rf_reg18 |= FIELD_PREP(RR_CFGCH_BW, CFGCH_BW_40M);
		break;
	case RTW89_CHANNEL_WIDTH_80:
		rf_reg18 |= FIELD_PREP(RR_CFGCH_BW, CFGCH_BW_80M);
		break;
	default:
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[RFK]Fail to set CH\n");
	}

	rf_reg18 &= ~(RR_CFGCH_POW_LCK | RR_CFGCH_TRX_AH | RR_CFGCH_BCN |
		      RR_CFGCH_BW2) & RFREG_MASK;
	rf_reg18 |= RR_CFGCH_BW2;
	rtw89_write_rf(rtwdev, path, reg18_addr, RFREG_MASK, rf_reg18);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[RFK] set %x at path%d, %x =0x%x\n",
		    bw, path, reg18_addr,
		    rtw89_read_rf(rtwdev, path, reg18_addr, RFREG_MASK));
}

static void _ctrl_bw(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
		     enum rtw89_bandwidth bw)
{
	_bw_setting(rtwdev, RF_PATH_A, bw, true);
	_bw_setting(rtwdev, RF_PATH_A, bw, false);
}

static bool _set_s0_arfc18(struct rtw89_dev *rtwdev, u32 val)
{
	u32 bak;
	u32 tmp;
	int ret;

	bak = rtw89_read_rf(rtwdev, RF_PATH_A, RR_LDO, RFREG_MASK);
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_LDO, RR_LDO_SEL, 0x1);
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_CFGCH, RFREG_MASK, val);

	ret = read_poll_timeout_atomic(rtw89_read_rf, tmp, tmp == 0, 1, 1000,
				       false, rtwdev, RF_PATH_A, RR_LPF, RR_LPF_BUSY);
	if (ret)
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[LCK]LCK timeout\n");

	rtw89_write_rf(rtwdev, RF_PATH_A, RR_LDO, RFREG_MASK, bak);

	return !!ret;
}

static void _lck_check(struct rtw89_dev *rtwdev)
{
	u32 tmp;

	if (rtw89_read_rf(rtwdev, RF_PATH_A, RR_SYNFB, RR_SYNFB_LK) == 0) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[LCK]SYN MMD reset\n");

		rtw89_write_rf(rtwdev, RF_PATH_A, RR_MMD, RR_MMD_RST_EN, 0x1);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_MMD, RR_MMD_RST_SYN, 0x0);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_MMD, RR_MMD_RST_SYN, 0x1);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_MMD, RR_MMD_RST_EN, 0x0);
	}

	udelay(10);

	if (rtw89_read_rf(rtwdev, RF_PATH_A, RR_SYNFB, RR_SYNFB_LK) == 0) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[LCK]re-set RF 0x18\n");

		rtw89_write_rf(rtwdev, RF_PATH_A, RR_LCK_TRG, RR_LCK_TRGSEL, 0x1);
		tmp = rtw89_read_rf(rtwdev, RF_PATH_A, RR_CFGCH, RFREG_MASK);
		_set_s0_arfc18(rtwdev, tmp);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_LCK_TRG, RR_LCK_TRGSEL, 0x0);
	}

	if (rtw89_read_rf(rtwdev, RF_PATH_A, RR_SYNFB, RR_SYNFB_LK) == 0) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[LCK]SYN off/on\n");

		tmp = rtw89_read_rf(rtwdev, RF_PATH_A, RR_POW, RFREG_MASK);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_POW, RFREG_MASK, tmp);
		tmp = rtw89_read_rf(rtwdev, RF_PATH_A, RR_SX, RFREG_MASK);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_SX, RFREG_MASK, tmp);

		rtw89_write_rf(rtwdev, RF_PATH_A, RR_SYNLUT, RR_SYNLUT_MOD, 0x1);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_POW, RR_POW_SYN, 0x0);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_POW, RR_POW_SYN, 0x3);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_SYNLUT, RR_SYNLUT_MOD, 0x0);

		rtw89_write_rf(rtwdev, RF_PATH_A, RR_LCK_TRG, RR_LCK_TRGSEL, 0x1);
		tmp = rtw89_read_rf(rtwdev, RF_PATH_A, RR_CFGCH, RFREG_MASK);
		_set_s0_arfc18(rtwdev, tmp);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_LCK_TRG, RR_LCK_TRGSEL, 0x0);

		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[LCK]0xb2=%x, 0xc5=%x\n",
			    rtw89_read_rf(rtwdev, RF_PATH_A, RR_VCO, RFREG_MASK),
			    rtw89_read_rf(rtwdev, RF_PATH_A, RR_SYNFB, RFREG_MASK));
	}
}

static void _set_ch(struct rtw89_dev *rtwdev, u32 val)
{
	bool timeout;

	timeout = _set_s0_arfc18(rtwdev, val);
	if (!timeout)
		_lck_check(rtwdev);
}

static void _ch_setting(struct rtw89_dev *rtwdev, enum rtw89_rf_path path,
			u8 central_ch, bool dav)
{
	u32 reg18_addr = dav ? RR_CFGCH : RR_CFGCH_V1;
	bool is_2g_ch = central_ch <= 14;
	u32 rf_reg18;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[RFK]===> %s\n", __func__);

	rf_reg18 = rtw89_read_rf(rtwdev, path, reg18_addr, RFREG_MASK);
	rf_reg18 &= ~(RR_CFGCH_BAND1 | RR_CFGCH_POW_LCK | RR_CFGCH_TRX_AH |
		      RR_CFGCH_BCN | RR_CFGCH_BAND0 | RR_CFGCH_CH);
	rf_reg18 |= FIELD_PREP(RR_CFGCH_CH, central_ch);

	if (!is_2g_ch)
		rf_reg18 |= FIELD_PREP(RR_CFGCH_BAND1, CFGCH_BAND1_5G) |
			    FIELD_PREP(RR_CFGCH_BAND0, CFGCH_BAND0_5G);

	rf_reg18 &= ~(RR_CFGCH_POW_LCK | RR_CFGCH_TRX_AH | RR_CFGCH_BCN |
		      RR_CFGCH_BW2) & RFREG_MASK;
	rf_reg18 |= RR_CFGCH_BW2;

	if (path == RF_PATH_A && dav)
		_set_ch(rtwdev, rf_reg18);
	else
		rtw89_write_rf(rtwdev, path, reg18_addr, RFREG_MASK, rf_reg18);

	rtw89_write_rf(rtwdev, path, RR_LCKST, RR_LCKST_BIN, 0);
	rtw89_write_rf(rtwdev, path, RR_LCKST, RR_LCKST_BIN, 1);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[RFK]CH: %d for Path-%d, reg0x%x = 0x%x\n",
		    central_ch, path, reg18_addr,
		    rtw89_read_rf(rtwdev, path, reg18_addr, RFREG_MASK));
}

static void _ctrl_ch(struct rtw89_dev *rtwdev, u8 central_ch)
{
	_ch_setting(rtwdev, RF_PATH_A, central_ch, true);
	_ch_setting(rtwdev, RF_PATH_A, central_ch, false);
}

static void _set_rxbb_bw(struct rtw89_dev *rtwdev, enum rtw89_bandwidth bw,
			 enum rtw89_rf_path path)
{
	rtw89_write_rf(rtwdev, path, RR_LUTWE2, RR_LUTWE2_RTXBW, 0x1);
	rtw89_write_rf(rtwdev, path, RR_LUTWA, RR_LUTWA_M2, 0x12);

	if (bw == RTW89_CHANNEL_WIDTH_20)
		rtw89_write_rf(rtwdev, path, RR_LUTWD0, RR_LUTWD0_LB, 0x1b);
	else if (bw == RTW89_CHANNEL_WIDTH_40)
		rtw89_write_rf(rtwdev, path, RR_LUTWD0, RR_LUTWD0_LB, 0x13);
	else if (bw == RTW89_CHANNEL_WIDTH_80)
		rtw89_write_rf(rtwdev, path, RR_LUTWD0, RR_LUTWD0_LB, 0xb);
	else
		rtw89_write_rf(rtwdev, path, RR_LUTWD0, RR_LUTWD0_LB, 0x3);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[RFK] set S%d RXBB BW 0x3F = 0x%x\n", path,
		    rtw89_read_rf(rtwdev, path, RR_LUTWD0, RR_LUTWD0_LB));

	rtw89_write_rf(rtwdev, path, RR_LUTWE2, RR_LUTWE2_RTXBW, 0x0);
}

static void _rxbb_bw(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
		     enum rtw89_bandwidth bw)
{
	u8 kpath, path;

	kpath = _kpath(rtwdev, phy);

	for (path = 0; path < RF_PATH_NUM_8851B; path++) {
		if (!(kpath & BIT(path)))
			continue;

		_set_rxbb_bw(rtwdev, bw, path);
	}
}

static void rtw8851b_ctrl_bw_ch(struct rtw89_dev *rtwdev,
				enum rtw89_phy_idx phy, u8 central_ch,
				enum rtw89_band band, enum rtw89_bandwidth bw)
{
	_ctrl_ch(rtwdev, central_ch);
	_ctrl_bw(rtwdev, phy, bw);
	_rxbb_bw(rtwdev, phy, bw);
}

void rtw8851b_set_channel_rf(struct rtw89_dev *rtwdev,
			     const struct rtw89_chan *chan,
			     enum rtw89_phy_idx phy_idx)
{
	rtw8851b_ctrl_bw_ch(rtwdev, phy_idx, chan->channel, chan->band_type,
			    chan->band_width);
}
