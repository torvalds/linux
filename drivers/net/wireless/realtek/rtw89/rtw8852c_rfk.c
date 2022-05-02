// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2022  Realtek Corporation
 */

#include "coex.h"
#include "debug.h"
#include "phy.h"
#include "reg.h"
#include "rtw8852c.h"
#include "rtw8852c_rfk.h"
#include "rtw8852c_rfk_table.h"
#include "rtw8852c_table.h"

#define _TSSI_DE_MASK GENMASK(21, 12)
static const u32 _tssi_de_cck_long[RF_PATH_NUM_8852C] = {0x5858, 0x7858};
static const u32 _tssi_de_cck_short[RF_PATH_NUM_8852C] = {0x5860, 0x7860};
static const u32 _tssi_de_mcs_20m[RF_PATH_NUM_8852C] = {0x5838, 0x7838};
static const u32 _tssi_de_mcs_40m[RF_PATH_NUM_8852C] = {0x5840, 0x7840};
static const u32 _tssi_de_mcs_80m[RF_PATH_NUM_8852C] = {0x5848, 0x7848};
static const u32 _tssi_de_mcs_80m_80m[RF_PATH_NUM_8852C] = {0x5850, 0x7850};
static const u32 _tssi_de_mcs_5m[RF_PATH_NUM_8852C] = {0x5828, 0x7828};
static const u32 _tssi_de_mcs_10m[RF_PATH_NUM_8852C] = {0x5830, 0x7830};

static u8 _kpath(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx)
{
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[RFK]dbcc_en: %x,  PHY%d\n",
		    rtwdev->dbcc_en, phy_idx);

	if (!rtwdev->dbcc_en)
		return RF_AB;

	if (phy_idx == RTW89_PHY_0)
		return RF_A;
	else
		return RF_B;
}

static void _dack_dump(struct rtw89_dev *rtwdev)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;
	u8 i;
	u8 t;

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DACK]S0 ADC_DCK ic = 0x%x, qc = 0x%x\n",
		    dack->addck_d[0][0], dack->addck_d[0][1]);
	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DACK]S1 ADC_DCK ic = 0x%x, qc = 0x%x\n",
		    dack->addck_d[1][0], dack->addck_d[1][1]);
	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DACK]S0 DAC_DCK ic = 0x%x, qc = 0x%x\n",
		    dack->dadck_d[0][0], dack->dadck_d[0][1]);
	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DACK]S1 DAC_DCK ic = 0x%x, qc = 0x%x\n",
		    dack->dadck_d[1][0], dack->dadck_d[1][1]);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DACK]S0 biask ic = 0x%x, qc = 0x%x\n",
		    dack->biask_d[0][0], dack->biask_d[0][1]);
	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DACK]S1 biask ic = 0x%x, qc = 0x%x\n",
		    dack->biask_d[1][0], dack->biask_d[1][1]);

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
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S1 MSBK ic:\n");
	for (i = 0; i < RTW89_DACK_MSBK_NR; i++) {
		t = dack->msbk_d[1][0][i];
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]0x%x\n", t);
	}
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S1 MSBK qc:\n");
	for (i = 0; i < RTW89_DACK_MSBK_NR; i++) {
		t = dack->msbk_d[1][1][i];
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]0x%x\n", t);
	}
}

static void _addck_backup(struct rtw89_dev *rtwdev)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;

	rtw89_phy_write32_mask(rtwdev, R_ADDCK0, B_ADDCK0, 0x0);
	dack->addck_d[0][0] = rtw89_phy_read32_mask(rtwdev, R_ADDCKR0,
						    B_ADDCKR0_A0);
	dack->addck_d[0][1] = rtw89_phy_read32_mask(rtwdev, R_ADDCKR0,
						    B_ADDCKR0_A1);

	rtw89_phy_write32_mask(rtwdev, R_ADDCK1, B_ADDCK1, 0x0);
	dack->addck_d[1][0] = rtw89_phy_read32_mask(rtwdev, R_ADDCKR1,
						    B_ADDCKR1_A0);
	dack->addck_d[1][1] = rtw89_phy_read32_mask(rtwdev, R_ADDCKR1,
						    B_ADDCKR1_A1);
}

static void _addck_reload(struct rtw89_dev *rtwdev)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;

	rtw89_phy_write32_mask(rtwdev, R_ADDCK0_RL, B_ADDCK0_RL1,
			       dack->addck_d[0][0]);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK0_RL, B_ADDCK0_RL0,
			       dack->addck_d[0][1]);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK0_RL, B_ADDCK0_RLS, 0x3);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK1_RL, B_ADDCK1_RL1,
			       dack->addck_d[1][0]);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK1_RL, B_ADDCK1_RL0,
			       dack->addck_d[1][1]);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK1_RL, B_ADDCK1_RLS, 0x3);
}

static void _dack_backup_s0(struct rtw89_dev *rtwdev)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;
	u8 i;

	rtw89_phy_write32_mask(rtwdev, R_P0_NRBW, B_P0_NRBW_DBG, 0x1);
	for (i = 0; i < RTW89_DACK_MSBK_NR; i++) {
		rtw89_phy_write32_mask(rtwdev, R_DCOF0, B_DCOF0_V, i);
		dack->msbk_d[0][0][i] = rtw89_phy_read32_mask(rtwdev,
							      R_DACK_S0P2,
							      B_DACK_S0M0);
		rtw89_phy_write32_mask(rtwdev, R_DCOF8, B_DCOF8_V, i);
		dack->msbk_d[0][1][i] = rtw89_phy_read32_mask(rtwdev,
							      R_DACK_S0P3,
							      B_DACK_S0M1);
	}
	dack->biask_d[0][0] = rtw89_phy_read32_mask(rtwdev, R_DACK_BIAS00,
						    B_DACK_BIAS00);
	dack->biask_d[0][1] = rtw89_phy_read32_mask(rtwdev, R_DACK_BIAS01,
						    B_DACK_BIAS01);
	dack->dadck_d[0][0] = rtw89_phy_read32_mask(rtwdev, R_DACK_DADCK00,
						    B_DACK_DADCK00);
	dack->dadck_d[0][1] = rtw89_phy_read32_mask(rtwdev, R_DACK_DADCK01,
						    B_DACK_DADCK01);
}

static void _dack_backup_s1(struct rtw89_dev *rtwdev)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;
	u8 i;

	rtw89_phy_write32_mask(rtwdev, R_P1_DBGMOD, B_P1_DBGMOD_ON, 0x1);
	for (i = 0; i < RTW89_DACK_MSBK_NR; i++) {
		rtw89_phy_write32_mask(rtwdev, R_DACK10, B_DACK10, i);
		dack->msbk_d[1][0][i] = rtw89_phy_read32_mask(rtwdev,
							      R_DACK10S,
							      B_DACK10S);
		rtw89_phy_write32_mask(rtwdev, R_DACK11, B_DACK11, i);
		dack->msbk_d[1][1][i] = rtw89_phy_read32_mask(rtwdev,
							      R_DACK11S,
							      B_DACK11S);
	}
	dack->biask_d[1][0] = rtw89_phy_read32_mask(rtwdev, R_DACK_BIAS10,
						    B_DACK_BIAS10);
	dack->biask_d[1][1] = rtw89_phy_read32_mask(rtwdev, R_DACK_BIAS11,
						    B_DACK_BIAS11);
	dack->dadck_d[1][0] = rtw89_phy_read32_mask(rtwdev, R_DACK_DADCK10,
						    B_DACK_DADCK10);
	dack->dadck_d[1][1] = rtw89_phy_read32_mask(rtwdev, R_DACK_DADCK11,
						    B_DACK_DADCK11);
}

static void _dack_reload_by_path(struct rtw89_dev *rtwdev,
				 enum rtw89_rf_path path, u8 index)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;
	u32 idx_offset, path_offset;
	u32 val32, offset, addr;
	u8 i;

	idx_offset = (index == 0 ? 0 : 0x14);
	path_offset = (path == RF_PATH_A ? 0 : 0x28);
	offset = idx_offset + path_offset;

	rtw89_rfk_parser(rtwdev, &rtw8852c_dack_reload_defs_tbl);

	/* msbk_d: 15/14/13/12 */
	val32 = 0x0;
	for (i = 0; i < RTW89_DACK_MSBK_NR / 4; i++)
		val32 |= dack->msbk_d[path][index][i + 12] << (i * 8);
	addr = 0xc200 + offset;
	rtw89_phy_write32(rtwdev, addr, val32);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]0x%x=0x%x\n", addr,
		    rtw89_phy_read32_mask(rtwdev, addr, MASKDWORD));

	/* msbk_d: 11/10/9/8 */
	val32 = 0x0;
	for (i = 0; i < RTW89_DACK_MSBK_NR / 4; i++)
		val32 |= dack->msbk_d[path][index][i + 8] << (i * 8);
	addr = 0xc204 + offset;
	rtw89_phy_write32(rtwdev, addr, val32);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]0x%x=0x%x\n", addr,
		    rtw89_phy_read32_mask(rtwdev, addr, MASKDWORD));

	/* msbk_d: 7/6/5/4 */
	val32 = 0x0;
	for (i = 0; i < RTW89_DACK_MSBK_NR / 4; i++)
		val32 |= dack->msbk_d[path][index][i + 4] << (i * 8);
	addr = 0xc208 + offset;
	rtw89_phy_write32(rtwdev, addr, val32);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]0x%x=0x%x\n", addr,
		    rtw89_phy_read32_mask(rtwdev, addr, MASKDWORD));

	/* msbk_d: 3/2/1/0 */
	val32 = 0x0;
	for (i = 0; i < RTW89_DACK_MSBK_NR / 4; i++)
		val32 |= dack->msbk_d[path][index][i] << (i * 8);
	addr = 0xc20c + offset;
	rtw89_phy_write32(rtwdev, addr, val32);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]0x%x=0x%x\n", addr,
		    rtw89_phy_read32_mask(rtwdev, addr, MASKDWORD));

	/* dadak_d/biask_d */
	val32 = (dack->biask_d[path][index] << 22) |
		(dack->dadck_d[path][index] << 14);
	addr = 0xc210 + offset;
	rtw89_phy_write32(rtwdev, addr, val32);
	rtw89_phy_write32_set(rtwdev, addr, BIT(1));
}

static void _dack_reload(struct rtw89_dev *rtwdev, enum rtw89_rf_path path)
{
	u8 i;

	for (i = 0; i < 2; i++)
		_dack_reload_by_path(rtwdev, path, i);
}

static void _addck(struct rtw89_dev *rtwdev)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;
	u32 val;
	int ret;

	/* S0 */
	rtw89_phy_write32_mask(rtwdev, R_ADDCK0, B_ADDCK0_RST, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK0, B_ADDCK0_EN, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK0, B_ADDCK0_EN, 0x0);
	fsleep(1);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK0, B_ADDCK0, 0x1);

	ret = read_poll_timeout_atomic(rtw89_phy_read32_mask, val, val,
				       1, 10000, false, rtwdev, 0xc0fc, BIT(0));
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S0 ADDCK timeout\n");
		dack->addck_timeout[0] = true;
	}

	rtw89_phy_write32_mask(rtwdev, R_ADDCK0, B_ADDCK0_RST, 0x0);

	/* S1 */
	rtw89_phy_write32_mask(rtwdev, R_ADDCK1, B_ADDCK1_RST, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK1, B_ADDCK1_EN, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK1, B_ADDCK1_EN, 0x0);
	udelay(1);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK1, B_ADDCK1, 0x1);

	ret = read_poll_timeout_atomic(rtw89_phy_read32_mask, val, val,
				       1, 10000, false, rtwdev, 0xc1fc, BIT(0));
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S1 ADDCK timeout\n");
		dack->addck_timeout[0] = true;
	}
	rtw89_phy_write32_mask(rtwdev, R_ADDCK1, B_ADDCK1_RST, 0x0);
}

static void _dack_reset(struct rtw89_dev *rtwdev, u8 path)
{
	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852c_dack_reset_defs_a_tbl,
				 &rtw8852c_dack_reset_defs_b_tbl);
}

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

static void rtw8852c_txck_force(struct rtw89_dev *rtwdev, u8 path, bool force,
				enum dac_ck ck)
{
	rtw89_phy_write32_mask(rtwdev, R_P0_RXCK | (path << 13), B_P0_TXCK_ON, 0x0);

	if (!force)
		return;

	rtw89_phy_write32_mask(rtwdev, R_P0_RXCK | (path << 13), B_P0_TXCK_VAL, ck);
	rtw89_phy_write32_mask(rtwdev, R_P0_RXCK | (path << 13), B_P0_TXCK_ON, 0x1);
}

static void rtw8852c_rxck_force(struct rtw89_dev *rtwdev, u8 path, bool force,
				enum adc_ck ck)
{
	rtw89_phy_write32_mask(rtwdev, R_P0_RXCK | (path << 13), B_P0_RXCK_ON, 0x0);

	if (!force)
		return;

	rtw89_phy_write32_mask(rtwdev, R_P0_RXCK | (path << 13), B_P0_RXCK_VAL, ck);
	rtw89_phy_write32_mask(rtwdev, R_P0_RXCK | (path << 13), B_P0_RXCK_ON, 0x1);
}

static bool _check_dack_done(struct rtw89_dev *rtwdev, bool s0)
{
	if (s0) {
		if (rtw89_phy_read32_mask(rtwdev, R_DACK_S0P0, B_DACK_S0P0_OK) == 0 ||
		    rtw89_phy_read32_mask(rtwdev, R_DACK_S0P1, B_DACK_S0P1_OK) == 0 ||
		    rtw89_phy_read32_mask(rtwdev, R_DACK_S0P2, B_DACK_S0P2_OK) == 0 ||
		    rtw89_phy_read32_mask(rtwdev, R_DACK_S0P3, B_DACK_S0P3_OK) == 0)
			return false;
	} else {
		if (rtw89_phy_read32_mask(rtwdev, R_DACK_S1P0, B_DACK_S1P0_OK) == 0 ||
		    rtw89_phy_read32_mask(rtwdev, R_DACK_S1P1, B_DACK_S1P1_OK) == 0 ||
		    rtw89_phy_read32_mask(rtwdev, R_DACK_S1P2, B_DACK_S1P2_OK) == 0 ||
		    rtw89_phy_read32_mask(rtwdev, R_DACK_S1P3, B_DACK_S1P3_OK) == 0)
			return false;
	}

	return true;
}

static void _dack_s0(struct rtw89_dev *rtwdev)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;
	bool done;
	int ret;

	rtw8852c_txck_force(rtwdev, RF_PATH_A, true, DAC_160M);
	rtw89_rfk_parser(rtwdev, &rtw8852c_dack_defs_s0_tbl);

	_dack_reset(rtwdev, RF_PATH_A);

	rtw89_phy_write32_mask(rtwdev, R_DCOF1, B_DCOF1_S, 0x1);
	ret = read_poll_timeout_atomic(_check_dack_done, done, done,
				       1, 10000, false, rtwdev, true);
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S0 DACK timeout\n");
		dack->msbk_timeout[0] = true;
	}
	rtw89_phy_write32_mask(rtwdev, R_DCOF1, B_DCOF1_S, 0x0);
	rtw8852c_txck_force(rtwdev, RF_PATH_A, false, DAC_960M);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]after S0 DADCK\n");

	_dack_backup_s0(rtwdev);
	_dack_reload(rtwdev, RF_PATH_A);
	rtw89_phy_write32_mask(rtwdev, R_P0_NRBW, B_P0_NRBW_DBG, 0x0);
}

static void _dack_s1(struct rtw89_dev *rtwdev)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;
	bool done;
	int ret;

	rtw8852c_txck_force(rtwdev, RF_PATH_B, true, DAC_160M);
	rtw89_rfk_parser(rtwdev, &rtw8852c_dack_defs_s1_tbl);

	_dack_reset(rtwdev, RF_PATH_B);

	rtw89_phy_write32_mask(rtwdev, R_DACK1_K, B_DACK1_EN, 0x1);
	ret = read_poll_timeout_atomic(_check_dack_done, done, done,
				       1, 10000, false, rtwdev, false);
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S1 DACK timeout\n");
		dack->msbk_timeout[0] = true;
	}
	rtw89_phy_write32_mask(rtwdev, R_DACK1_K, B_DACK1_EN, 0x0);
	rtw8852c_txck_force(rtwdev, RF_PATH_B, false, DAC_960M);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]after S1 DADCK\n");

	_dack_backup_s1(rtwdev);
	_dack_reload(rtwdev, RF_PATH_B);
	rtw89_phy_write32_mask(rtwdev, R_P1_DBGMOD, B_P1_DBGMOD_ON, 0x0);
}

static void _dack(struct rtw89_dev *rtwdev)
{
	_dack_s0(rtwdev);
	_dack_s1(rtwdev);
}

static void _drck(struct rtw89_dev *rtwdev)
{
	u32 val;
	int ret;

	rtw89_phy_write32_mask(rtwdev, R_DRCK, B_DRCK_EN, 0x1);
	ret = read_poll_timeout_atomic(rtw89_phy_read32_mask, val, val,
				       1, 10000, false, rtwdev, 0xc0c8, BIT(3));
	if (ret)
		rtw89_debug(rtwdev, RTW89_DBG_RFK,  "[DACK]DRCK timeout\n");

	rtw89_rfk_parser(rtwdev, &rtw8852c_drck_defs_tbl);

	val = rtw89_phy_read32_mask(rtwdev, R_DRCK_RES, B_DRCK_RES);
	rtw89_phy_write32_mask(rtwdev, R_DRCK, B_DRCK_IDLE, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_DRCK, B_DRCK_VAL, val);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]0xc0c4 = 0x%x\n",
		    rtw89_phy_read32_mask(rtwdev, R_DRCK, MASKDWORD));
}

static void _dac_cal(struct rtw89_dev *rtwdev, bool force)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;
	u32 rf0_0, rf1_0;
	u8 phy_map = rtw89_btc_phymap(rtwdev, RTW89_PHY_0, RF_AB);

	dack->dack_done = false;
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]DACK b\n");
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]DACK start!!!\n");
	rf0_0 = rtw89_read_rf(rtwdev, RF_PATH_A, RR_MOD, RFREG_MASK);
	rf1_0 = rtw89_read_rf(rtwdev, RF_PATH_B, RR_MOD, RFREG_MASK);
	_drck(rtwdev);

	rtw89_write_rf(rtwdev, RF_PATH_A, RR_RSV1, RR_RSV1_RST, 0x0);
	rtw89_write_rf(rtwdev, RF_PATH_B, RR_RSV1, RR_RSV1_RST, 0x0);
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_MOD, RFREG_MASK, 0x337e1);
	rtw89_write_rf(rtwdev, RF_PATH_B, RR_MOD, RFREG_MASK, 0x337e1);
	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_DACK, BTC_WRFK_ONESHOT_START);
	_addck(rtwdev);
	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_DACK, BTC_WRFK_ONESHOT_STOP);

	_addck_backup(rtwdev);
	_addck_reload(rtwdev);
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_MODOPT, RFREG_MASK, 0x0);
	rtw89_write_rf(rtwdev, RF_PATH_B, RR_MODOPT, RFREG_MASK, 0x0);
	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_DACK, BTC_WRFK_ONESHOT_START);
	_dack(rtwdev);
	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_DACK, BTC_WRFK_ONESHOT_STOP);

	_dack_dump(rtwdev);
	dack->dack_done = true;
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_MOD, RFREG_MASK, rf0_0);
	rtw89_write_rf(rtwdev, RF_PATH_B, RR_MOD, RFREG_MASK, rf1_0);
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_RSV1, RR_RSV1_RST, 0x1);
	rtw89_write_rf(rtwdev, RF_PATH_B, RR_RSV1, RR_RSV1_RST, 0x1);
	dack->dack_cnt++;
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]DACK finish!!!\n");
}

static void _tssi_set_sys(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			  enum rtw89_rf_path path)
{
	enum rtw89_band band = rtwdev->hal.current_band_type;

	rtw89_rfk_parser(rtwdev, &rtw8852c_tssi_sys_defs_tbl);

	if (path == RF_PATH_A)
		rtw89_rfk_parser_by_cond(rtwdev, band == RTW89_BAND_2G,
					 &rtw8852c_tssi_sys_defs_2g_a_tbl,
					 &rtw8852c_tssi_sys_defs_5g_a_tbl);
	else
		rtw89_rfk_parser_by_cond(rtwdev, band == RTW89_BAND_2G,
					 &rtw8852c_tssi_sys_defs_2g_b_tbl,
					 &rtw8852c_tssi_sys_defs_5g_b_tbl);
}

static void _tssi_ini_txpwr_ctrl_bb(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
				    enum rtw89_rf_path path)
{
	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852c_tssi_txpwr_ctrl_bb_defs_a_tbl,
				 &rtw8852c_tssi_txpwr_ctrl_bb_defs_b_tbl);
}

static void _tssi_ini_txpwr_ctrl_bb_he_tb(struct rtw89_dev *rtwdev,
					  enum rtw89_phy_idx phy,
					  enum rtw89_rf_path path)
{
	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852c_tssi_txpwr_ctrl_bb_he_tb_defs_a_tbl,
				 &rtw8852c_tssi_txpwr_ctrl_bb_he_tb_defs_b_tbl);
}

static void _tssi_set_dck(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			  enum rtw89_rf_path path)
{
	enum rtw89_band band = rtwdev->hal.current_band_type;

	if (path == RF_PATH_A) {
		rtw89_rfk_parser(rtwdev, &rtw8852c_tssi_dck_defs_a_tbl);
		rtw89_rfk_parser_by_cond(rtwdev, band == RTW89_BAND_2G,
					 &rtw8852c_tssi_dck_defs_2g_a_tbl,
					 &rtw8852c_tssi_dck_defs_5g_a_tbl);
	} else {
		rtw89_rfk_parser(rtwdev, &rtw8852c_tssi_dck_defs_b_tbl);
		rtw89_rfk_parser_by_cond(rtwdev, band == RTW89_BAND_2G,
					 &rtw8852c_tssi_dck_defs_2g_b_tbl,
					 &rtw8852c_tssi_dck_defs_5g_b_tbl);
	}
}

static void _tssi_set_bbgain_split(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
				   enum rtw89_rf_path path)
{
	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852c_tssi_set_bbgain_split_a_tbl,
				 &rtw8852c_tssi_set_bbgain_split_b_tbl);
}

static void _tssi_set_tmeter_tbl(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
				 enum rtw89_rf_path path)
{
#define RTW8852C_TSSI_GET_VAL(ptr, idx)			\
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
	u8 ch = rtwdev->hal.current_channel;
	u8 subband = rtwdev->hal.current_subband;
	const s8 *thm_up_a = NULL;
	const s8 *thm_down_a = NULL;
	const s8 *thm_up_b = NULL;
	const s8 *thm_down_b = NULL;
	u8 thermal = 0xff;
	s8 thm_ofst[64] = {0};
	u32 tmp = 0;
	u8 i, j;

	switch (subband) {
	default:
	case RTW89_CH_2G:
		thm_up_a = rtw89_8852c_trk_cfg.delta_swingidx_2ga_p;
		thm_down_a = rtw89_8852c_trk_cfg.delta_swingidx_2ga_n;
		thm_up_b = rtw89_8852c_trk_cfg.delta_swingidx_2gb_p;
		thm_down_b = rtw89_8852c_trk_cfg.delta_swingidx_2gb_n;
		break;
	case RTW89_CH_5G_BAND_1:
		thm_up_a = rtw89_8852c_trk_cfg.delta_swingidx_5ga_p[0];
		thm_down_a = rtw89_8852c_trk_cfg.delta_swingidx_5ga_n[0];
		thm_up_b = rtw89_8852c_trk_cfg.delta_swingidx_5gb_p[0];
		thm_down_b = rtw89_8852c_trk_cfg.delta_swingidx_5gb_n[0];
		break;
	case RTW89_CH_5G_BAND_3:
		thm_up_a = rtw89_8852c_trk_cfg.delta_swingidx_5ga_p[1];
		thm_down_a = rtw89_8852c_trk_cfg.delta_swingidx_5ga_n[1];
		thm_up_b = rtw89_8852c_trk_cfg.delta_swingidx_5gb_p[1];
		thm_down_b = rtw89_8852c_trk_cfg.delta_swingidx_5gb_n[1];
		break;
	case RTW89_CH_5G_BAND_4:
		thm_up_a = rtw89_8852c_trk_cfg.delta_swingidx_5ga_p[2];
		thm_down_a = rtw89_8852c_trk_cfg.delta_swingidx_5ga_n[2];
		thm_up_b = rtw89_8852c_trk_cfg.delta_swingidx_5gb_p[2];
		thm_down_b = rtw89_8852c_trk_cfg.delta_swingidx_5gb_n[2];
		break;
	case RTW89_CH_6G_BAND_IDX0:
	case RTW89_CH_6G_BAND_IDX1:
		thm_up_a = rtw89_8852c_trk_cfg.delta_swingidx_6ga_p[0];
		thm_down_a = rtw89_8852c_trk_cfg.delta_swingidx_6ga_n[0];
		thm_up_b = rtw89_8852c_trk_cfg.delta_swingidx_6gb_p[0];
		thm_down_b = rtw89_8852c_trk_cfg.delta_swingidx_6gb_n[0];
		break;
	case RTW89_CH_6G_BAND_IDX2:
	case RTW89_CH_6G_BAND_IDX3:
		thm_up_a = rtw89_8852c_trk_cfg.delta_swingidx_6ga_p[1];
		thm_down_a = rtw89_8852c_trk_cfg.delta_swingidx_6ga_n[1];
		thm_up_b = rtw89_8852c_trk_cfg.delta_swingidx_6gb_p[1];
		thm_down_b = rtw89_8852c_trk_cfg.delta_swingidx_6gb_n[1];
		break;
	case RTW89_CH_6G_BAND_IDX4:
	case RTW89_CH_6G_BAND_IDX5:
		thm_up_a = rtw89_8852c_trk_cfg.delta_swingidx_6ga_p[2];
		thm_down_a = rtw89_8852c_trk_cfg.delta_swingidx_6ga_n[2];
		thm_up_b = rtw89_8852c_trk_cfg.delta_swingidx_6gb_p[2];
		thm_down_b = rtw89_8852c_trk_cfg.delta_swingidx_6gb_n[2];
		break;
	case RTW89_CH_6G_BAND_IDX6:
	case RTW89_CH_6G_BAND_IDX7:
		thm_up_a = rtw89_8852c_trk_cfg.delta_swingidx_6ga_p[3];
		thm_down_a = rtw89_8852c_trk_cfg.delta_swingidx_6ga_n[3];
		thm_up_b = rtw89_8852c_trk_cfg.delta_swingidx_6gb_p[3];
		thm_down_b = rtw89_8852c_trk_cfg.delta_swingidx_6gb_n[3];
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
					    0x5c00 + i, 0x0);
			}

		} else {
			rtw89_phy_write32_mask(rtwdev, R_P0_TMETER, B_P0_TMETER, thermal);
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
				tmp = RTW8852C_TSSI_GET_VAL(thm_ofst, i);
				rtw89_phy_write32(rtwdev, R_P0_TSSI_BASE + i, tmp);

				rtw89_debug(rtwdev, RTW89_DBG_TSSI,
					    "[TSSI] write 0x%x val=0x%08x\n",
					    0x5c00 + i, tmp);
			}
		}
		rtw89_phy_write32_mask(rtwdev, R_P0_RFCTM, R_P0_RFCTM_RDY, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_P0_RFCTM, R_P0_RFCTM_RDY, 0x0);

	} else {
		thermal = tssi_info->thermal[RF_PATH_B];

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI] ch=%d thermal_pathB=0x%x\n", ch, thermal);

		rtw89_phy_write32_mask(rtwdev, R_P1_TMETER, B_P1_TMETER_DIS, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_P1_TMETER, B_P1_TMETER_TRK, 0x1);

		if (thermal == 0xff) {
			rtw89_phy_write32_mask(rtwdev, R_P1_TMETER, B_P1_TMETER, 32);
			rtw89_phy_write32_mask(rtwdev, R_P1_RFCTM, B_P1_RFCTM_VAL, 32);

			for (i = 0; i < 64; i += 4) {
				rtw89_phy_write32(rtwdev, R_TSSI_THOF + i, 0x0);

				rtw89_debug(rtwdev, RTW89_DBG_TSSI,
					    "[TSSI] write 0x%x val=0x%08x\n",
					    0x7c00 + i, 0x0);
			}

		} else {
			rtw89_phy_write32_mask(rtwdev, R_P1_TMETER, B_P1_TMETER, thermal);
			rtw89_phy_write32_mask(rtwdev, R_P1_RFCTM, B_P1_RFCTM_VAL,
					       thermal);

			i = 0;
			for (j = 0; j < 32; j++)
				thm_ofst[j] = i < DELTA_SWINGIDX_SIZE ?
					      -thm_down_b[i++] :
					      -thm_down_b[DELTA_SWINGIDX_SIZE - 1];

			i = 1;
			for (j = 63; j >= 32; j--)
				thm_ofst[j] = i < DELTA_SWINGIDX_SIZE ?
					      thm_up_b[i++] :
					      thm_up_b[DELTA_SWINGIDX_SIZE - 1];

			for (i = 0; i < 64; i += 4) {
				tmp = RTW8852C_TSSI_GET_VAL(thm_ofst, i);
				rtw89_phy_write32(rtwdev, R_TSSI_THOF + i, tmp);

				rtw89_debug(rtwdev, RTW89_DBG_TSSI,
					    "[TSSI] write 0x%x val=0x%08x\n",
					    0x7c00 + i, tmp);
			}
		}
		rtw89_phy_write32_mask(rtwdev, R_P1_RFCTM, R_P1_RFCTM_RDY, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_P1_RFCTM, R_P1_RFCTM_RDY, 0x0);
	}
#undef RTW8852C_TSSI_GET_VAL
}

static void _tssi_slope_cal_org(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
				enum rtw89_rf_path path)
{
	enum rtw89_band band = rtwdev->hal.current_band_type;

	if (path == RF_PATH_A) {
		rtw89_rfk_parser_by_cond(rtwdev, band == RTW89_BAND_2G,
					 &rtw8852c_tssi_slope_cal_org_defs_2g_a_tbl,
					 &rtw8852c_tssi_slope_cal_org_defs_5g_a_tbl);
	} else {
		rtw89_rfk_parser_by_cond(rtwdev, band == RTW89_BAND_2G,
					 &rtw8852c_tssi_slope_cal_org_defs_2g_b_tbl,
					 &rtw8852c_tssi_slope_cal_org_defs_5g_b_tbl);
	}
}

static void _tssi_set_aligk_default(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
				    enum rtw89_rf_path path)
{
	enum rtw89_band band = rtwdev->hal.current_band_type;
	const struct rtw89_rfk_tbl *tbl;

	if (path == RF_PATH_A) {
		if (band == RTW89_BAND_2G)
			tbl = &rtw8852c_tssi_set_aligk_default_defs_2g_a_tbl;
		else if (band == RTW89_BAND_6G)
			tbl = &rtw8852c_tssi_set_aligk_default_defs_6g_a_tbl;
		else
			tbl = &rtw8852c_tssi_set_aligk_default_defs_5g_a_tbl;
	} else {
		if (band == RTW89_BAND_2G)
			tbl = &rtw8852c_tssi_set_aligk_default_defs_2g_b_tbl;
		else if (band == RTW89_BAND_6G)
			tbl = &rtw8852c_tssi_set_aligk_default_defs_6g_b_tbl;
		else
			tbl = &rtw8852c_tssi_set_aligk_default_defs_5g_b_tbl;
	}

	rtw89_rfk_parser(rtwdev, tbl);
}

static void _tssi_set_slope(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			    enum rtw89_rf_path path)
{
	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852c_tssi_slope_defs_a_tbl,
				 &rtw8852c_tssi_slope_defs_b_tbl);
}

static void _tssi_run_slope(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			    enum rtw89_rf_path path)
{
	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852c_tssi_run_slope_defs_a_tbl,
				 &rtw8852c_tssi_run_slope_defs_b_tbl);
}

static void _tssi_set_track(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			    enum rtw89_rf_path path)
{
	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852c_tssi_track_defs_a_tbl,
				 &rtw8852c_tssi_track_defs_b_tbl);
}

static void _tssi_set_txagc_offset_mv_avg(struct rtw89_dev *rtwdev,
					  enum rtw89_phy_idx phy,
					  enum rtw89_rf_path path)
{
	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852c_tssi_txagc_ofst_mv_avg_defs_a_tbl,
				 &rtw8852c_tssi_txagc_ofst_mv_avg_defs_b_tbl);
}

static void _tssi_enable(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy)
{
	struct rtw89_tssi_info *tssi_info = &rtwdev->tssi;
	u32 i, path = RF_PATH_A, path_max = RF_PATH_NUM_8852C;

	if (rtwdev->dbcc_en) {
		if (phy == RTW89_PHY_0) {
			path = RF_PATH_A;
			path_max = RF_PATH_B;
		} else if (phy == RTW89_PHY_1) {
			path = RF_PATH_B;
			path_max = RF_PATH_NUM_8852C;
		}
	}

	for (i = path; i < path_max; i++) {
		_tssi_set_track(rtwdev, phy, i);
		_tssi_set_txagc_offset_mv_avg(rtwdev, phy, i);

		rtw89_rfk_parser_by_cond(rtwdev, i == RF_PATH_A,
					 &rtw8852c_tssi_enable_defs_a_tbl,
					 &rtw8852c_tssi_enable_defs_b_tbl);

		tssi_info->base_thermal[i] =
			ewma_thermal_read(&rtwdev->phystat.avg_thermal[i]);
		rtwdev->is_tssi_mode[i] = true;
	}
}

static void _tssi_disable(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy)
{
	u32 i, path = RF_PATH_A, path_max = RF_PATH_NUM_8852C;

	if (rtwdev->dbcc_en) {
		if (phy == RTW89_PHY_0) {
			path = RF_PATH_A;
			path_max = RF_PATH_B;
		} else if (phy == RTW89_PHY_1) {
			path = RF_PATH_B;
			path_max = RF_PATH_NUM_8852C;
		}
	}

	for (i = path; i < path_max; i++) {
		if (i == RF_PATH_A) {
			rtw89_rfk_parser(rtwdev, &rtw8852c_tssi_disable_defs_a_tbl);
			rtwdev->is_tssi_mode[RF_PATH_A] = false;
		}  else if (i == RF_PATH_B) {
			rtw89_rfk_parser(rtwdev, &rtw8852c_tssi_disable_defs_b_tbl);
			rtwdev->is_tssi_mode[RF_PATH_B] = false;
		}
	}
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

static u32 _tssi_get_6g_ofdm_group(struct rtw89_dev *rtwdev, u8 ch)
{
	switch (ch) {
	case 1 ... 5:
		return 0;
	case 6 ... 8:
		return TSSI_EXTRA_GROUP(0);
	case 9 ... 13:
		return 1;
	case 14 ... 16:
		return TSSI_EXTRA_GROUP(1);
	case 17 ... 21:
		return 2;
	case 22 ... 24:
		return TSSI_EXTRA_GROUP(2);
	case 25 ... 29:
		return 3;
	case 33 ... 37:
		return 4;
	case 38 ... 40:
		return TSSI_EXTRA_GROUP(4);
	case 41 ... 45:
		return 5;
	case 46 ... 48:
		return TSSI_EXTRA_GROUP(5);
	case 49 ... 53:
		return 6;
	case 54 ... 56:
		return TSSI_EXTRA_GROUP(6);
	case 57 ... 61:
		return 7;
	case 65 ... 69:
		return 8;
	case 70 ... 72:
		return TSSI_EXTRA_GROUP(8);
	case 73 ... 77:
		return 9;
	case 78 ... 80:
		return TSSI_EXTRA_GROUP(9);
	case 81 ... 85:
		return 10;
	case 86 ... 88:
		return TSSI_EXTRA_GROUP(10);
	case 89 ... 93:
		return 11;
	case 97 ... 101:
		return 12;
	case 102 ... 104:
		return TSSI_EXTRA_GROUP(12);
	case 105 ... 109:
		return 13;
	case 110 ... 112:
		return TSSI_EXTRA_GROUP(13);
	case 113 ... 117:
		return 14;
	case 118 ... 120:
		return TSSI_EXTRA_GROUP(14);
	case 121 ... 125:
		return 15;
	case 129 ... 133:
		return 16;
	case 134 ... 136:
		return TSSI_EXTRA_GROUP(16);
	case 137 ... 141:
		return 17;
	case 142 ... 144:
		return TSSI_EXTRA_GROUP(17);
	case 145 ... 149:
		return 18;
	case 150 ... 152:
		return TSSI_EXTRA_GROUP(18);
	case 153 ... 157:
		return 19;
	case 161 ... 165:
		return 20;
	case 166 ... 168:
		return TSSI_EXTRA_GROUP(20);
	case 169 ... 173:
		return 21;
	case 174 ... 176:
		return TSSI_EXTRA_GROUP(21);
	case 177 ... 181:
		return 22;
	case 182 ... 184:
		return TSSI_EXTRA_GROUP(22);
	case 185 ... 189:
		return 23;
	case 193 ... 197:
		return 24;
	case 198 ... 200:
		return TSSI_EXTRA_GROUP(24);
	case 201 ... 205:
		return 25;
	case 206 ... 208:
		return TSSI_EXTRA_GROUP(25);
	case 209 ... 213:
		return 26;
	case 214 ... 216:
		return TSSI_EXTRA_GROUP(26);
	case 217 ... 221:
		return 27;
	case 225 ... 229:
		return 28;
	case 230 ... 232:
		return TSSI_EXTRA_GROUP(28);
	case 233 ... 237:
		return 29;
	case 238 ... 240:
		return TSSI_EXTRA_GROUP(29);
	case 241 ... 245:
		return 30;
	case 246 ... 248:
		return TSSI_EXTRA_GROUP(30);
	case 249 ... 253:
		return 31;
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
	case 49 ... 51:
		return TSSI_EXTRA_GROUP(2);
	case 52 ... 64:
		return 3;
	case 100 ... 112:
		return 4;
	case 113 ... 115:
		return TSSI_EXTRA_GROUP(4);
	case 116 ... 128:
		return 5;
	case 132 ... 144:
		return 6;
	case 149 ... 177:
		return 7;
	}

	return 0;
}

static u32 _tssi_get_6g_trim_group(struct rtw89_dev *rtwdev, u8 ch)
{
	switch (ch) {
	case 1 ... 13:
		return 0;
	case 14 ... 16:
		return TSSI_EXTRA_GROUP(0);
	case 17 ... 29:
		return 1;
	case 33 ... 45:
		return 2;
	case 46 ... 48:
		return TSSI_EXTRA_GROUP(2);
	case 49 ... 61:
		return 3;
	case 65 ... 77:
		return 4;
	case 78 ... 80:
		return TSSI_EXTRA_GROUP(4);
	case 81 ... 93:
		return 5;
	case 97 ... 109:
		return 6;
	case 110 ... 112:
		return TSSI_EXTRA_GROUP(6);
	case 113 ... 125:
		return 7;
	case 129 ... 141:
		return 8;
	case 142 ... 144:
		return TSSI_EXTRA_GROUP(8);
	case 145 ... 157:
		return 9;
	case 161 ... 173:
		return 10;
	case 174 ... 176:
		return TSSI_EXTRA_GROUP(10);
	case 177 ... 189:
		return 11;
	case 193 ... 205:
		return 12;
	case 206 ... 208:
		return TSSI_EXTRA_GROUP(12);
	case 209 ... 221:
		return 13;
	case 225 ... 237:
		return 14;
	case 238 ... 240:
		return TSSI_EXTRA_GROUP(14);
	case 241 ... 253:
		return 15;
	}

	return 0;
}

static s8 _tssi_get_ofdm_de(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			    enum rtw89_rf_path path)
{
	struct rtw89_tssi_info *tssi_info = &rtwdev->tssi;
	enum rtw89_band band = rtwdev->hal.current_band_type;
	u8 ch = rtwdev->hal.current_channel;
	u32 gidx, gidx_1st, gidx_2nd;
	s8 de_1st;
	s8 de_2nd;
	s8 val;

	if (band == RTW89_BAND_2G || band == RTW89_BAND_5G) {
		gidx = _tssi_get_ofdm_group(rtwdev, ch);

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI][TRIM]: path=%d mcs group_idx=0x%x\n",
			    path, gidx);

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
	} else {
		gidx = _tssi_get_6g_ofdm_group(rtwdev, ch);

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI][TRIM]: path=%d mcs group_idx=0x%x\n",
			    path, gidx);

		if (IS_TSSI_EXTRA_GROUP(gidx)) {
			gidx_1st = TSSI_EXTRA_GET_GROUP_IDX1(gidx);
			gidx_2nd = TSSI_EXTRA_GET_GROUP_IDX2(gidx);
			de_1st = tssi_info->tssi_6g_mcs[path][gidx_1st];
			de_2nd = tssi_info->tssi_6g_mcs[path][gidx_2nd];
			val = (de_1st + de_2nd) / 2;

			rtw89_debug(rtwdev, RTW89_DBG_TSSI,
				    "[TSSI][TRIM]: path=%d mcs de=%d 1st=%d 2nd=%d\n",
				    path, val, de_1st, de_2nd);
		} else {
			val = tssi_info->tssi_6g_mcs[path][gidx];

			rtw89_debug(rtwdev, RTW89_DBG_TSSI,
				    "[TSSI][TRIM]: path=%d mcs de=%d\n", path, val);
		}
	}

	return val;
}

static s8 _tssi_get_ofdm_trim_de(struct rtw89_dev *rtwdev,
				 enum rtw89_phy_idx phy,
				 enum rtw89_rf_path path)
{
	struct rtw89_tssi_info *tssi_info = &rtwdev->tssi;
	enum rtw89_band band = rtwdev->hal.current_band_type;
	u8 ch = rtwdev->hal.current_channel;
	u32 tgidx, tgidx_1st, tgidx_2nd;
	s8 tde_1st = 0;
	s8 tde_2nd = 0;
	s8 val;

	if (band == RTW89_BAND_2G || band == RTW89_BAND_5G) {
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
	} else {
		tgidx = _tssi_get_6g_trim_group(rtwdev, ch);

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI][TRIM]: path=%d mcs trim_group_idx=0x%x\n",
			    path, tgidx);

		if (IS_TSSI_EXTRA_GROUP(tgidx)) {
			tgidx_1st = TSSI_EXTRA_GET_GROUP_IDX1(tgidx);
			tgidx_2nd = TSSI_EXTRA_GET_GROUP_IDX2(tgidx);
			tde_1st = tssi_info->tssi_trim_6g[path][tgidx_1st];
			tde_2nd = tssi_info->tssi_trim_6g[path][tgidx_2nd];
			val = (tde_1st + tde_2nd) / 2;

			rtw89_debug(rtwdev, RTW89_DBG_TSSI,
				    "[TSSI][TRIM]: path=%d mcs trim_de=%d 1st=%d 2nd=%d\n",
				    path, val, tde_1st, tde_2nd);
		} else {
			val = tssi_info->tssi_trim_6g[path][tgidx];

			rtw89_debug(rtwdev, RTW89_DBG_TSSI,
				    "[TSSI][TRIM]: path=%d mcs trim_de=%d\n",
				    path, val);
		}
	}

	return val;
}

static void _tssi_set_efuse_to_de(struct rtw89_dev *rtwdev,
				  enum rtw89_phy_idx phy)
{
	struct rtw89_tssi_info *tssi_info = &rtwdev->tssi;
	u8 ch = rtwdev->hal.current_channel;
	u8 gidx;
	s8 ofdm_de;
	s8 trim_de;
	s32 val;
	u32 i, path = RF_PATH_A, path_max = RF_PATH_NUM_8852C;

	rtw89_debug(rtwdev, RTW89_DBG_TSSI, "[TSSI][TRIM]: phy=%d ch=%d\n",
		    phy, ch);

	if (rtwdev->dbcc_en) {
		if (phy == RTW89_PHY_0) {
			path = RF_PATH_A;
			path_max = RF_PATH_B;
		} else if (phy == RTW89_PHY_1) {
			path = RF_PATH_B;
			path_max = RF_PATH_NUM_8852C;
		}
	}

	for (i = path; i < path_max; i++) {
		gidx = _tssi_get_cck_group(rtwdev, ch);
		trim_de = _tssi_get_ofdm_trim_de(rtwdev, phy, i);
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

		ofdm_de = _tssi_get_ofdm_de(rtwdev, phy, i);
		trim_de = _tssi_get_ofdm_trim_de(rtwdev, phy, i);
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

static void rtw8852c_tssi_cont_en(struct rtw89_dev *rtwdev, bool en,
				  enum rtw89_rf_path path)
{
	static const u32 tssi_trk[2] = {0x5818, 0x7818};
	static const u32 tssi_en[2] = {0x5820, 0x7820};

	if (en) {
		rtw89_phy_write32_mask(rtwdev, tssi_trk[path], BIT(30), 0x0);
		rtw89_phy_write32_mask(rtwdev, tssi_en[path], BIT(31), 0x0);
		if (rtwdev->dbcc_en && path == RF_PATH_B)
			_tssi_set_efuse_to_de(rtwdev, RTW89_PHY_1);
		else
			_tssi_set_efuse_to_de(rtwdev, RTW89_PHY_0);
	} else {
		rtw89_phy_write32_mask(rtwdev, tssi_trk[path], BIT(30), 0x1);
		rtw89_phy_write32_mask(rtwdev, tssi_en[path], BIT(31), 0x1);
	}
}

void rtw8852c_tssi_cont_en_phyidx(struct rtw89_dev *rtwdev, bool en, u8 phy_idx)
{
	if (!rtwdev->dbcc_en) {
		rtw8852c_tssi_cont_en(rtwdev, en, RF_PATH_A);
		rtw8852c_tssi_cont_en(rtwdev, en, RF_PATH_B);
	} else {
		if (phy_idx == RTW89_PHY_0)
			rtw8852c_tssi_cont_en(rtwdev, en, RF_PATH_A);
		else
			rtw8852c_tssi_cont_en(rtwdev, en, RF_PATH_B);
	}
}

static void _bw_setting(struct rtw89_dev *rtwdev, enum rtw89_rf_path path,
			enum rtw89_bandwidth bw, bool is_dav)
{
	u32 rf_reg18;
	u32 reg_reg18_addr;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[RFK]===>%s\n", __func__);
	if (is_dav)
		reg_reg18_addr = RR_CFGCH;
	else
		reg_reg18_addr = RR_CFGCH_V1;

	rf_reg18 = rtw89_read_rf(rtwdev, path, reg_reg18_addr, RFREG_MASK);
	rf_reg18 &= ~RR_CFGCH_BW;

	switch (bw) {
	case RTW89_CHANNEL_WIDTH_5:
	case RTW89_CHANNEL_WIDTH_10:
	case RTW89_CHANNEL_WIDTH_20:
		rf_reg18 |= FIELD_PREP(RR_CFGCH_BW, CFGCH_BW_20M);
		rtw89_phy_write32_mask(rtwdev, R_P0_CFCH_BW0 | (path << 8), B_P0_CFCH_BW0, 0x3);
		rtw89_phy_write32_mask(rtwdev, R_P0_CFCH_BW1 | (path << 8), B_P0_CFCH_BW1, 0xf);
		break;
	case RTW89_CHANNEL_WIDTH_40:
		rf_reg18 |= FIELD_PREP(RR_CFGCH_BW, CFGCH_BW_40M);
		rtw89_phy_write32_mask(rtwdev, R_P0_CFCH_BW0 | (path << 8), B_P0_CFCH_BW0, 0x3);
		rtw89_phy_write32_mask(rtwdev, R_P0_CFCH_BW1 | (path << 8), B_P0_CFCH_BW1, 0xf);
		break;
	case RTW89_CHANNEL_WIDTH_80:
		rf_reg18 |= FIELD_PREP(RR_CFGCH_BW, CFGCH_BW_80M);
		rtw89_phy_write32_mask(rtwdev, R_P0_CFCH_BW0 | (path << 8), B_P0_CFCH_BW0, 0x2);
		rtw89_phy_write32_mask(rtwdev, R_P0_CFCH_BW1 | (path << 8), B_P0_CFCH_BW1, 0xd);
		break;
	case RTW89_CHANNEL_WIDTH_160:
		rf_reg18 |= FIELD_PREP(RR_CFGCH_BW, CFGCH_BW_160M);
		rtw89_phy_write32_mask(rtwdev, R_P0_CFCH_BW0 | (path << 8), B_P0_CFCH_BW0, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_P0_CFCH_BW1 | (path << 8), B_P0_CFCH_BW1, 0xb);
		break;
	default:
		break;
	}

	rtw89_write_rf(rtwdev, path, reg_reg18_addr, RFREG_MASK, rf_reg18);
}

static void _ctrl_bw(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
		     enum rtw89_bandwidth bw)
{
	bool is_dav;
	u8 kpath, path;
	u32 tmp = 0;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[RFK]===>%s\n", __func__);
	kpath = _kpath(rtwdev, phy);

	for (path = 0; path < 2; path++) {
		if (!(kpath & BIT(path)))
			continue;

		is_dav = true;
		_bw_setting(rtwdev, path, bw, is_dav);
		is_dav = false;
		_bw_setting(rtwdev, path, bw, is_dav);
		if (rtwdev->dbcc_en)
			continue;

		if (path == RF_PATH_B && rtwdev->hal.cv == CHIP_CAV) {
			rtw89_write_rf(rtwdev, RF_PATH_B, RR_RSV1, RR_RSV1_RST, 0x0);
			tmp = rtw89_read_rf(rtwdev, RF_PATH_A, RR_CFGCH, RFREG_MASK);
			rtw89_write_rf(rtwdev, RF_PATH_B, RR_APK, RR_APK_MOD, 0x3);
			rtw89_write_rf(rtwdev, RF_PATH_B, RR_CFGCH, RFREG_MASK, tmp);
			fsleep(100);
			rtw89_write_rf(rtwdev, RF_PATH_B, RR_RSV1, RR_RSV1_RST, 0x1);
		}
	}
}

static void _ch_setting(struct rtw89_dev *rtwdev, enum rtw89_rf_path path,
			u8 central_ch, enum rtw89_band band, bool is_dav)
{
	u32 rf_reg18;
	u32 reg_reg18_addr;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[RFK]===>%s\n", __func__);
	if (is_dav)
		reg_reg18_addr = 0x18;
	else
		reg_reg18_addr = 0x10018;

	rf_reg18 = rtw89_read_rf(rtwdev, path, reg_reg18_addr, RFREG_MASK);
	rf_reg18 &= ~(RR_CFGCH_BAND1 | RR_CFGCH_BAND0 | RR_CFGCH_CH);
	rf_reg18 |= FIELD_PREP(RR_CFGCH_CH, central_ch);

	switch (band) {
	case RTW89_BAND_2G:
		rf_reg18 |= FIELD_PREP(RR_CFGCH_BAND1, CFGCH_BAND1_2G);
		rf_reg18 |= FIELD_PREP(RR_CFGCH_BAND0, CFGCH_BAND0_2G);
		break;
	case RTW89_BAND_5G:
		rf_reg18 |= FIELD_PREP(RR_CFGCH_BAND1, CFGCH_BAND1_5G);
		rf_reg18 |= FIELD_PREP(RR_CFGCH_BAND0, CFGCH_BAND0_5G);
		break;
	case RTW89_BAND_6G:
		rf_reg18 |= FIELD_PREP(RR_CFGCH_BAND1, CFGCH_BAND1_6G);
		rf_reg18 |= FIELD_PREP(RR_CFGCH_BAND0, CFGCH_BAND0_6G);
		break;
	default:
		break;
	}
	rtw89_write_rf(rtwdev, path, reg_reg18_addr, RFREG_MASK, rf_reg18);
	fsleep(100);
}

static void _ctrl_ch(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
		     u8 central_ch, enum rtw89_band band)
{
	u8 kpath, path;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[RFK]===>%s\n", __func__);
	if (band != RTW89_BAND_6G) {
		if ((central_ch > 14 && central_ch < 36) ||
		    (central_ch > 64 && central_ch < 100) ||
		    (central_ch > 144 && central_ch < 149) || central_ch > 177)
			return;
	} else {
		if (central_ch > 253 || central_ch  == 2)
			return;
	}

	kpath = _kpath(rtwdev, phy);

	for (path = 0; path < 2; path++) {
		if (kpath & BIT(path)) {
			_ch_setting(rtwdev, path, central_ch, band, true);
			_ch_setting(rtwdev, path, central_ch, band, false);
		}
	}
}

static void _rxbb_bw(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
		     enum rtw89_bandwidth bw)
{
	u8 kpath;
	u8 path;
	u32 val;

	kpath = _kpath(rtwdev, phy);
	for (path = 0; path < 2; path++) {
		if (!(kpath & BIT(path)))
			continue;

		rtw89_write_rf(rtwdev, path, RR_LUTWE2, RR_LUTWE2_RTXBW, 0x1);
		rtw89_write_rf(rtwdev, path, RR_LUTWA, RR_LUTWA_M2, 0xa);
		switch (bw) {
		case RTW89_CHANNEL_WIDTH_20:
			val = 0x1b;
			break;
		case RTW89_CHANNEL_WIDTH_40:
			val = 0x13;
			break;
		case RTW89_CHANNEL_WIDTH_80:
			val = 0xb;
			break;
		case RTW89_CHANNEL_WIDTH_160:
		default:
			val = 0x3;
			break;
		}
		rtw89_write_rf(rtwdev, path, RR_LUTWD0, RR_LUTWD0_LB, val);
		rtw89_write_rf(rtwdev, path, RR_LUTWE2, RR_LUTWE2_RTXBW, 0x0);
	}
}

static void _lck_keep_thermal(struct rtw89_dev *rtwdev)
{
	struct rtw89_lck_info *lck = &rtwdev->lck;
	int path;

	for (path = 0; path < rtwdev->chip->rf_path_num; path++) {
		lck->thermal[path] =
			ewma_thermal_read(&rtwdev->phystat.avg_thermal[path]);
		rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK,
			    "[LCK] path=%d thermal=0x%x", path, lck->thermal[path]);
	}
}

static void _lck(struct rtw89_dev *rtwdev)
{
	u32 tmp18[2];
	int path = rtwdev->dbcc_en ? 2 : 1;
	int i;

	rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK, "[LCK] DO LCK\n");

	tmp18[0] = rtw89_read_rf(rtwdev, RF_PATH_A, RR_CFGCH, RFREG_MASK);
	tmp18[1] = rtw89_read_rf(rtwdev, RF_PATH_B, RR_CFGCH, RFREG_MASK);

	for (i = 0; i < path; i++) {
		rtw89_write_rf(rtwdev, i, RR_LCK_TRG, RR_LCK_TRGSEL, 0x1);
		rtw89_write_rf(rtwdev, i, RR_CFGCH, RFREG_MASK, tmp18[i]);
		rtw89_write_rf(rtwdev, i, RR_LCK_TRG, RR_LCK_TRGSEL, 0x0);
	}

	_lck_keep_thermal(rtwdev);
}

#define RTW8852C_LCK_TH 8

void rtw8852c_lck_track(struct rtw89_dev *rtwdev)
{
	struct rtw89_lck_info *lck = &rtwdev->lck;
	u8 cur_thermal;
	int delta;
	int path;

	for (path = 0; path < rtwdev->chip->rf_path_num; path++) {
		cur_thermal =
			ewma_thermal_read(&rtwdev->phystat.avg_thermal[path]);
		delta = abs((int)cur_thermal - lck->thermal[path]);

		rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK,
			    "[LCK] path=%d current thermal=0x%x delta=0x%x\n",
			    path, cur_thermal, delta);

		if (delta >= RTW8852C_LCK_TH) {
			_lck(rtwdev);
			return;
		}
	}
}

void rtw8852c_lck_init(struct rtw89_dev *rtwdev)
{
	_lck_keep_thermal(rtwdev);
}

static
void rtw8852c_ctrl_bw_ch(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			 u8 central_ch, enum rtw89_band band,
			 enum rtw89_bandwidth bw)
{
	_ctrl_ch(rtwdev, phy, central_ch, band);
	_ctrl_bw(rtwdev, phy, bw);
	_rxbb_bw(rtwdev, phy, bw);
}

void rtw8852c_set_channel_rf(struct rtw89_dev *rtwdev,
			     struct rtw89_channel_params *param,
			     enum rtw89_phy_idx phy_idx)
{
	rtw8852c_ctrl_bw_ch(rtwdev, phy_idx, param->center_chan, param->band_type,
			    param->bandwidth);
}

void rtw8852c_dack(struct rtw89_dev *rtwdev)
{
	u8 phy_map = rtw89_btc_phymap(rtwdev, RTW89_PHY_0, 0);

	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_DACK, BTC_WRFK_START);
	_dac_cal(rtwdev, false);
	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_DACK, BTC_WRFK_STOP);
}

void rtw8852c_tssi(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy)
{
	u32 i, path = RF_PATH_A, path_max = RF_PATH_NUM_8852C;

	rtw89_debug(rtwdev, RTW89_DBG_TSSI, "[TSSI] %s: phy=%d\n", __func__, phy);

	if (rtwdev->dbcc_en) {
		if (phy == RTW89_PHY_0) {
			path = RF_PATH_A;
			path_max = RF_PATH_B;
		} else if (phy == RTW89_PHY_1) {
			path = RF_PATH_B;
			path_max = RF_PATH_NUM_8852C;
		}
	}

	_tssi_disable(rtwdev, phy);

	for (i = path; i < path_max; i++) {
		_tssi_set_sys(rtwdev, phy, i);
		_tssi_ini_txpwr_ctrl_bb(rtwdev, phy, i);
		_tssi_ini_txpwr_ctrl_bb_he_tb(rtwdev, phy, i);
		_tssi_set_dck(rtwdev, phy, i);
		_tssi_set_bbgain_split(rtwdev, phy, i);
		_tssi_set_tmeter_tbl(rtwdev, phy, i);
		_tssi_slope_cal_org(rtwdev, phy, i);
		_tssi_set_aligk_default(rtwdev, phy, i);
		_tssi_set_slope(rtwdev, phy, i);
		_tssi_run_slope(rtwdev, phy, i);
	}

	_tssi_enable(rtwdev, phy);
	_tssi_set_efuse_to_de(rtwdev, phy);
}

void rtw8852c_tssi_scan(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy)
{
	u32 i, path = RF_PATH_A, path_max = RF_PATH_NUM_8852C;

	rtw89_debug(rtwdev, RTW89_DBG_TSSI, "[TSSI] %s: phy=%d\n",
		    __func__, phy);

	if (!rtwdev->is_tssi_mode[RF_PATH_A])
		return;
	if (!rtwdev->is_tssi_mode[RF_PATH_B])
		return;

	if (rtwdev->dbcc_en) {
		if (phy == RTW89_PHY_0) {
			path = RF_PATH_A;
			path_max = RF_PATH_B;
		} else if (phy == RTW89_PHY_1) {
			path = RF_PATH_B;
			path_max = RF_PATH_NUM_8852C;
		}
	}

	_tssi_disable(rtwdev, phy);

	for (i = path; i < path_max; i++) {
		_tssi_set_sys(rtwdev, phy, i);
		_tssi_set_dck(rtwdev, phy, i);
		_tssi_set_tmeter_tbl(rtwdev, phy, i);
		_tssi_slope_cal_org(rtwdev, phy, i);
		_tssi_set_aligk_default(rtwdev, phy, i);
	}

	_tssi_enable(rtwdev, phy);
	_tssi_set_efuse_to_de(rtwdev, phy);
}

static void rtw8852c_tssi_default_txagc(struct rtw89_dev *rtwdev,
					enum rtw89_phy_idx phy, bool enable)
{
	struct rtw89_tssi_info *tssi_info = &rtwdev->tssi;
	u8 i;

	if (!rtwdev->is_tssi_mode[RF_PATH_A] && !rtwdev->is_tssi_mode[RF_PATH_B])
		return;

	if (enable) {
		/* SCAN_START */
		if (rtw89_phy_read32_mask(rtwdev, R_TXAGC_BB, B_TXAGC_BB_OFT) != 0xc000 &&
		    rtw89_phy_read32_mask(rtwdev, R_TXAGC_BB, B_TXAGC_BB_OFT) != 0x0) {
			for (i = 0; i < 6; i++) {
				tssi_info->default_txagc_offset[RF_PATH_A] =
					rtw89_phy_read32_mask(rtwdev, R_TXAGC_BB,
							      B_TXAGC_BB);
				if (tssi_info->default_txagc_offset[RF_PATH_A])
					break;
			}
		}

		if (rtw89_phy_read32_mask(rtwdev, R_TXAGC_BB_S1, B_TXAGC_BB_S1_OFT) != 0xc000 &&
		    rtw89_phy_read32_mask(rtwdev, R_TXAGC_BB_S1, B_TXAGC_BB_S1_OFT) != 0x0) {
			for (i = 0; i < 6; i++) {
				tssi_info->default_txagc_offset[RF_PATH_B] =
					rtw89_phy_read32_mask(rtwdev, R_TXAGC_BB_S1,
							      B_TXAGC_BB_S1);
				if (tssi_info->default_txagc_offset[RF_PATH_B])
					break;
			}
		}
	} else {
		/* SCAN_END */
		rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_TRK, B_P0_TSSI_OFT,
				       tssi_info->default_txagc_offset[RF_PATH_A]);
		rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_TRK, B_P1_TSSI_OFT,
				       tssi_info->default_txagc_offset[RF_PATH_B]);

		rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_TRK, B_P0_TSSI_OFT_EN, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_TRK, B_P0_TSSI_OFT_EN, 0x1);

		rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_TRK, B_P1_TSSI_OFT_EN, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_TRK, B_P1_TSSI_OFT_EN, 0x1);
	}
}

void rtw8852c_wifi_scan_notify(struct rtw89_dev *rtwdev,
			       bool scan_start, enum rtw89_phy_idx phy_idx)
{
	if (scan_start)
		rtw8852c_tssi_default_txagc(rtwdev, phy_idx, true);
	else
		rtw8852c_tssi_default_txagc(rtwdev, phy_idx, false);
}
