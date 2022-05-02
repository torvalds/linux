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
