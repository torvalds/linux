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

#define RTW8851B_RXK_GROUP_NR 4
#define RTW8851B_TXK_GROUP_NR 1
#define RTW8851B_IQK_VER 0x2a
#define RTW8851B_IQK_SS 1
#define RTW8851B_LOK_GRAM 10

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

static const u32 g_idxrxgain[RTW8851B_RXK_GROUP_NR] = {0x10e, 0x116, 0x28e, 0x296};
static const u32 g_idxattc2[RTW8851B_RXK_GROUP_NR] = {0x0, 0xf, 0x0, 0xf};
static const u32 g_idxrxagc[RTW8851B_RXK_GROUP_NR] = {0x0, 0x1, 0x2, 0x3};
static const u32 a_idxrxgain[RTW8851B_RXK_GROUP_NR] = {0x10C, 0x112, 0x28c, 0x292};
static const u32 a_idxattc2[RTW8851B_RXK_GROUP_NR] = {0xf, 0xf, 0xf, 0xf};
static const u32 a_idxrxagc[RTW8851B_RXK_GROUP_NR] = {0x4, 0x5, 0x6, 0x7};
static const u32 a_power_range[RTW8851B_TXK_GROUP_NR] = {0x0};
static const u32 a_track_range[RTW8851B_TXK_GROUP_NR] = {0x6};
static const u32 a_gain_bb[RTW8851B_TXK_GROUP_NR] = {0x0a};
static const u32 a_itqt[RTW8851B_TXK_GROUP_NR] = {0x12};
static const u32 g_power_range[RTW8851B_TXK_GROUP_NR] = {0x0};
static const u32 g_track_range[RTW8851B_TXK_GROUP_NR] = {0x6};
static const u32 g_gain_bb[RTW8851B_TXK_GROUP_NR] = {0x10};
static const u32 g_itqt[RTW8851B_TXK_GROUP_NR] = {0x12};

static const u32 rtw8851b_backup_bb_regs[] = {0xc0ec, 0xc0e8};
static const u32 rtw8851b_backup_rf_regs[] = {
	0xef, 0xde, 0x0, 0x1e, 0x2, 0x85, 0x90, 0x5};

#define BACKUP_BB_REGS_NR ARRAY_SIZE(rtw8851b_backup_bb_regs)
#define BACKUP_RF_REGS_NR ARRAY_SIZE(rtw8851b_backup_rf_regs)

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

		if (gp == 0x3) {
			rtw89_write_rf(rtwdev, path, RR_RXKPLL, RR_RXKPLL_OFF, 0x13);
			rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_RXT, 0x011);
			notready = _iqk_one_shot(rtwdev, phy_idx, path, ID_NBRXK);
			iqk_info->nb_rxcfir[path] =
				rtw89_phy_read32_mask(rtwdev, R_RXIQC, MASKDWORD) | 0x2;

			rtw89_debug(rtwdev, RTW89_DBG_RFK,
				    "[IQK]S%x, NBRXK 0x8008 = 0x%x\n", path,
				    rtw89_phy_read32_mask(rtwdev, R_NCTL_RPT, MASKDWORD));
		}

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
	u8 gp;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]===>%s\n", __func__);

	for (gp = 0; gp < RTW8851B_RXK_GROUP_NR; gp++) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, gp = %x\n", path, gp);

		rtw89_write_rf(rtwdev, RF_PATH_A, RR_MOD, 0x03ff0, a_idxrxgain[gp]);
		rtw89_write_rf(rtwdev, RF_PATH_A, RR_RXA2, RR_RXA2_ATT, a_idxattc2[gp]);

		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT, B_CFIR_LUT_SEL, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT, B_CFIR_LUT_G3, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT, B_CFIR_LUT_GP_V1, gp);

		rtw89_write_rf(rtwdev, path, RR_RXKPLL, RFREG_MASK, 0x80013);
		fsleep(100);
		rf_0 = rtw89_read_rf(rtwdev, path, RR_MOD, RFREG_MASK);
		rtw89_phy_write32_mask(rtwdev, R_IQK_DIF2, B_IQK_DIF2_RXPI, rf_0);
		rtw89_phy_write32_mask(rtwdev, R_IQK_RXA, B_IQK_RXAGC, a_idxrxagc[gp]);
		rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_RXT, 0x11);
		notready = _iqk_one_shot(rtwdev, phy_idx, path, ID_RXAGC);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK]S%x, RXAGC 0x8008 = 0x%x, rxbb = %x\n", path,
			    rtw89_phy_read32_mask(rtwdev, R_NCTL_RPT, MASKDWORD),
			    rtw89_read_rf(rtwdev, path, RR_MOD, RR_MOD_RXB));

		if (gp == 0x3) {
			rtw89_write_rf(rtwdev, path, RR_RXKPLL, RR_RXKPLL_OFF, 0x13);
			rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_RXT, 0x011);
			notready = _iqk_one_shot(rtwdev, phy_idx, path, ID_NBRXK);
			iqk_info->nb_rxcfir[path] =
				rtw89_phy_read32_mask(rtwdev, R_RXIQC, MASKDWORD) | 0x2;

			rtw89_debug(rtwdev, RTW89_DBG_RFK,
				    "[IQK]S%x, NBRXK 0x8008 = 0x%x\n", path,
				    rtw89_phy_read32_mask(rtwdev, R_NCTL_RPT, MASKDWORD));
		}

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
	u8 gp = 0x3;
	u32 rf_0;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]===>%s\n", __func__);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, gp = %x\n", path, gp);

	rtw89_write_rf(rtwdev, RF_PATH_A, RR_MOD, RR_MOD_RGM, a_idxrxgain[gp]);
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_RXA2, RR_RXA2_ATT, a_idxattc2[gp]);

	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT, B_CFIR_LUT_SEL, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT, B_CFIR_LUT_G3, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT, B_CFIR_LUT_GP_V1, gp);

	rtw89_write_rf(rtwdev, path, RR_RXKPLL, RFREG_MASK, 0x80013);
	fsleep(100);
	rf_0 = rtw89_read_rf(rtwdev, path, RR_MOD, RFREG_MASK);
	rtw89_phy_write32_mask(rtwdev, R_IQK_DIF2, B_IQK_DIF2_RXPI, rf_0);
	rtw89_phy_write32_mask(rtwdev, R_IQK_RXA, B_IQK_RXAGC, a_idxrxagc[gp]);
	rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_RXT, 0x11);
	notready = _iqk_one_shot(rtwdev, phy_idx, path, ID_RXAGC);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[IQK]S%x, RXAGC 0x8008 = 0x%x, rxbb = %x\n", path,
		    rtw89_phy_read32_mask(rtwdev, R_NCTL_RPT, MASKDWORD),
		    rtw89_read_rf(rtwdev, path, RR_MOD, 0x003e0));

	if (gp == 0x3) {
		rtw89_write_rf(rtwdev, path, RR_RXKPLL, RR_RXKPLL_OFF, 0x13);
		rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_RXT, 0x011);
		notready = _iqk_one_shot(rtwdev, phy_idx, path, ID_NBRXK);
		iqk_info->nb_rxcfir[path] =
			rtw89_phy_read32_mask(rtwdev, R_RXIQC, MASKDWORD) | 0x2;

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK]S%x, NBRXK 0x8008 = 0x%x\n", path,
			    rtw89_phy_read32_mask(rtwdev, R_NCTL_RPT, MASKDWORD));
	}

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

	if (gp == 0x3) {
		rtw89_write_rf(rtwdev, path, RR_RXKPLL, RR_RXKPLL_OFF, 0x13);
		rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_RXT, 0x011);
		notready = _iqk_one_shot(rtwdev, phy_idx, path, ID_NBRXK);
		iqk_info->nb_rxcfir[path] =
			rtw89_phy_read32_mask(rtwdev, R_RXIQC, MASKDWORD) | 0x2;

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK]S%x, NBRXK 0x8008 = 0x%x\n", path,
			    rtw89_phy_read32_mask(rtwdev, R_NCTL_RPT, MASKDWORD));
	}

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

	if (iqk_info->iqk_bw[path] == RTW89_CHANNEL_WIDTH_80)
		rtw89_rfk_parser(rtwdev, &rtw8851b_iqk_rxclk_80_defs_tbl);
	else
		rtw89_rfk_parser(rtwdev, &rtw8851b_iqk_rxclk_others_defs_tbl);
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
			     u8 path)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
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
	iqk_info->thermal_rek_en = false;
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
		   enum rtw89_phy_idx phy_idx, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	u8 phy_map = rtw89_btc_phymap(rtwdev, phy_idx, RF_AB);
	u32 backup_rf_val[RTW8851B_IQK_SS][BACKUP_RF_REGS_NR];
	u32 backup_bb_val[BACKUP_BB_REGS_NR];

	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_IQK,
			      BTC_WRFK_ONESHOT_START);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[IQK]==========IQK strat!!!!!==========\n");
	iqk_info->iqk_times++;
	iqk_info->kcount = 0;
	iqk_info->version = RTW8851B_IQK_VER;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]Test Ver 0x%x\n", iqk_info->version);
	_iqk_get_ch_info(rtwdev, phy_idx, path);

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

static void _iqk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx, bool force)
{
	_doiqk(rtwdev, force, phy_idx, RF_PATH_A);
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

void rtw8851b_aack(struct rtw89_dev *rtwdev)
{
	u32 tmp05, ib[4];
	u32 tmp;
	int ret;
	int rek;
	int i;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[LCK]DO AACK\n");

	tmp05 = rtw89_read_rf(rtwdev, RF_PATH_A, RR_RSV1, RFREG_MASK);
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_MOD, RR_MOD_MASK, 0x3);
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_RSV1, RFREG_MASK, 0x0);

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
}

void rtw8851b_rck(struct rtw89_dev *rtwdev)
{
	_rck(rtwdev, RF_PATH_A);
}

void rtw8851b_dack(struct rtw89_dev *rtwdev)
{
	_dac_cal(rtwdev, false);
}

void rtw8851b_iqk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx)
{
	u8 phy_map = rtw89_btc_phymap(rtwdev, phy_idx, 0);
	u32 tx_en;

	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_IQK, BTC_WRFK_START);
	rtw89_chip_stop_sch_tx(rtwdev, phy_idx, &tx_en, RTW89_SCH_TX_SEL_ALL);
	_wait_rx_mode(rtwdev, _kpath(rtwdev, phy_idx));

	_iqk_init(rtwdev);
	_iqk(rtwdev, phy_idx, false);

	rtw89_chip_resume_sch_tx(rtwdev, phy_idx, tx_en);
	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_IQK, BTC_WRFK_STOP);
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
