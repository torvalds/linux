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

static const u32 rtw8852c_backup_bb_regs[] = {
	0x8120, 0xc0d4, 0xc0d8, 0xc0e8, 0x8220, 0xc1d4, 0xc1d8, 0xc1e8
};

static const u32 rtw8852c_backup_rf_regs[] = {
	0xdf, 0x8f, 0x97, 0xa3, 0x5, 0x10005
};

#define BACKUP_BB_REGS_NR ARRAY_SIZE(rtw8852c_backup_bb_regs)
#define BACKUP_RF_REGS_NR ARRAY_SIZE(rtw8852c_backup_rf_regs)

#define RXK_GROUP_NR 4
static const u32 _rxk_a6_idxrxgain[RXK_GROUP_NR] = {0x190, 0x196, 0x290, 0x316};
static const u32 _rxk_a6_idxattc2[RXK_GROUP_NR] = {0x00, 0x0, 0x00, 0x00};
static const u32 _rxk_a_idxrxgain[RXK_GROUP_NR] = {0x190, 0x198, 0x310, 0x318};
static const u32 _rxk_a_idxattc2[RXK_GROUP_NR] = {0x00, 0x00, 0x00, 0x00};
static const u32 _rxk_g_idxrxgain[RXK_GROUP_NR] = {0x252, 0x26c, 0x350, 0x360};
static const u32 _rxk_g_idxattc2[RXK_GROUP_NR] = {0x00, 0x07, 0x00, 0x3};

#define TXK_GROUP_NR 3
static const u32 _txk_a6_power_range[TXK_GROUP_NR] = {0x0, 0x0, 0x0};
static const u32 _txk_a6_track_range[TXK_GROUP_NR] = {0x6, 0x7, 0x7};
static const u32 _txk_a6_gain_bb[TXK_GROUP_NR] = {0x12, 0x09, 0x0e};
static const u32 _txk_a6_itqt[TXK_GROUP_NR] = {0x12, 0x12, 0x12};
static const u32 _txk_a_power_range[TXK_GROUP_NR] = {0x0, 0x0, 0x0};
static const u32 _txk_a_track_range[TXK_GROUP_NR] = {0x5, 0x6, 0x7};
static const u32 _txk_a_gain_bb[TXK_GROUP_NR] = {0x12, 0x09, 0x0e};
static const u32 _txk_a_itqt[TXK_GROUP_NR] = {0x12, 0x12, 0x12};
static const u32 _txk_g_power_range[TXK_GROUP_NR] = {0x0, 0x0, 0x0};
static const u32 _txk_g_track_range[TXK_GROUP_NR] = {0x5, 0x6, 0x6};
static const u32 _txk_g_gain_bb[TXK_GROUP_NR] = {0x0e, 0x0a, 0x0e};
static const u32 _txk_g_itqt[TXK_GROUP_NR] = { 0x12, 0x12, 0x12};

static const u32 dpk_par_regs[RTW89_DPK_RF_PATH][4] = {
	{0x8190, 0x8194, 0x8198, 0x81a4},
	{0x81a8, 0x81c4, 0x81c8, 0x81e8},
};

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

static void _rfk_backup_bb_reg(struct rtw89_dev *rtwdev, u32 backup_bb_reg_val[])
{
	u32 i;

	for (i = 0; i < BACKUP_BB_REGS_NR; i++) {
		backup_bb_reg_val[i] =
			rtw89_phy_read32_mask(rtwdev, rtw8852c_backup_bb_regs[i],
					      MASKDWORD);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK]backup bb reg : %x, value =%x\n",
			    rtw8852c_backup_bb_regs[i], backup_bb_reg_val[i]);
	}
}

static void _rfk_backup_rf_reg(struct rtw89_dev *rtwdev, u32 backup_rf_reg_val[],
			       u8 rf_path)
{
	u32 i;

	for (i = 0; i < BACKUP_RF_REGS_NR; i++) {
		backup_rf_reg_val[i] =
			rtw89_read_rf(rtwdev, rf_path,
				      rtw8852c_backup_rf_regs[i], RFREG_MASK);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK]backup rf S%d reg : %x, value =%x\n", rf_path,
			    rtw8852c_backup_rf_regs[i], backup_rf_reg_val[i]);
	}
}

static void _rfk_restore_bb_reg(struct rtw89_dev *rtwdev, u32 backup_bb_reg_val[])
{
	u32 i;

	for (i = 0; i < BACKUP_BB_REGS_NR; i++) {
		rtw89_phy_write32_mask(rtwdev, rtw8852c_backup_bb_regs[i],
				       MASKDWORD, backup_bb_reg_val[i]);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK]restore bb reg : %x, value =%x\n",
			    rtw8852c_backup_bb_regs[i], backup_bb_reg_val[i]);
	}
}

static void _rfk_restore_rf_reg(struct rtw89_dev *rtwdev, u32 backup_rf_reg_val[],
				u8 rf_path)
{
	u32 i;

	for (i = 0; i < BACKUP_RF_REGS_NR; i++) {
		rtw89_write_rf(rtwdev, rf_path, rtw8852c_backup_rf_regs[i],
			       RFREG_MASK, backup_rf_reg_val[i]);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK]restore rf S%d reg: %x, value =%x\n", rf_path,
			    rtw8852c_backup_rf_regs[i], backup_rf_reg_val[i]);
	}
}

static void _wait_rx_mode(struct rtw89_dev *rtwdev, u8 kpath)
{
	u8 path;
	u32 rf_mode;
	int ret;

	for (path = 0; path < RF_PATH_MAX; path++) {
		if (!(kpath & BIT(path)))
			continue;

		ret = read_poll_timeout_atomic(rtw89_read_rf, rf_mode, rf_mode != 2,
					       2, 5000, false, rtwdev, path, 0x00,
					       RR_MOD_MASK);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[RFK] Wait S%d to Rx mode!! (ret = %d)\n",
			    path, ret);
	}
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

#define RTW8852C_NCTL_VER 0xd
#define RTW8852C_IQK_VER 0x2a
#define RTW8852C_IQK_SS 2
#define RTW8852C_IQK_THR_REK 8
#define RTW8852C_IQK_CFIR_GROUP_NR 4

enum rtw8852c_iqk_type {
	ID_TXAGC,
	ID_G_FLOK_COARSE,
	ID_A_FLOK_COARSE,
	ID_G_FLOK_FINE,
	ID_A_FLOK_FINE,
	ID_FLOK_VBUFFER,
	ID_TXK,
	ID_RXAGC,
	ID_RXK,
	ID_NBTXK,
	ID_NBRXK,
};

static void rtw8852c_disable_rxagc(struct rtw89_dev *rtwdev, u8 path, u8 en_rxgac)
{
	if (path == RF_PATH_A)
		rtw89_phy_write32_mask(rtwdev, R_P0_AGC_CTL, B_P0_AGC_EN, en_rxgac);
	else
		rtw89_phy_write32_mask(rtwdev, R_P1_AGC_CTL, B_P1_AGC_EN, en_rxgac);
}

static void _iqk_rxk_setting(struct rtw89_dev *rtwdev, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;

	if (path == RF_PATH_A)
		rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_RXK, 0x0101);
	else
		rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_RXK, 0x0202);

	switch (iqk_info->iqk_bw[path]) {
	case RTW89_CHANNEL_WIDTH_20:
	case RTW89_CHANNEL_WIDTH_40:
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK + (path << 13), B_DPD_GDIS, 0x1);
		rtw8852c_rxck_force(rtwdev, path, true, ADC_480M);
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK + (path << 13), B_ACK_VAL, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_P0_CFCH_BW0 + (path << 8), B_P0_CFCH_BW0, 0x3);
		rtw89_phy_write32_mask(rtwdev, R_P0_CFCH_BW1 + (path << 8), B_P0_CFCH_BW1, 0xf);
		rtw89_write_rf(rtwdev, path, RR_RXBB2, RR_RXBB2_CKT, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_P0_NRBW + (path << 13), B_P0_NRBW_DBG, 0x1);
		break;
	case RTW89_CHANNEL_WIDTH_80:
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK + (path << 13), B_DPD_GDIS, 0x1);
		rtw8852c_rxck_force(rtwdev, path, true, ADC_960M);
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK + (path << 13), B_ACK_VAL, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_P0_CFCH_BW0 + (path << 8), B_P0_CFCH_BW0, 0x2);
		rtw89_phy_write32_mask(rtwdev, R_P0_CFCH_BW1 + (path << 8), B_P0_CFCH_BW1, 0xd);
		rtw89_write_rf(rtwdev, path, RR_RXBB2, RR_RXBB2_CKT, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_P0_NRBW + (path << 13), B_P0_NRBW_DBG, 0x1);
	break;
	case RTW89_CHANNEL_WIDTH_160:
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK + (path << 13), B_DPD_GDIS, 0x1);
		rtw8852c_rxck_force(rtwdev, path, true, ADC_1920M);
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK + (path << 13), B_ACK_VAL, 0x2);
		rtw89_phy_write32_mask(rtwdev, R_P0_CFCH_BW0 + (path << 8), B_P0_CFCH_BW0, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_P0_CFCH_BW1 + (path << 8), B_P0_CFCH_BW1, 0xb);
		rtw89_write_rf(rtwdev, path, RR_RXBB2, RR_RXBB2_CKT, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_P0_NRBW + (path << 13), B_P0_NRBW_DBG, 0x1);
		break;
	default:
		break;
	}

	rtw89_rfk_parser(rtwdev, &rtw8852c_iqk_rxk_cfg_defs_tbl);

	if (path == RF_PATH_A)
		rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_RXK, 0x1101);
	else
		rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_RXK, 0x2202);
}

static bool _iqk_check_cal(struct rtw89_dev *rtwdev, u8 path, u8 ktype)
{
	u32 tmp;
	u32 val;
	int ret;

	ret = read_poll_timeout_atomic(rtw89_phy_read32_mask, val, val == 0x55,
				       1, 8200, false, rtwdev, 0xbff8, MASKBYTE0);
	if (ret)
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]IQK timeout!!!\n");

	rtw89_phy_write32_clr(rtwdev, R_NCTL_N1, MASKBYTE0);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, ret=%d\n", path, ret);
	tmp = rtw89_phy_read32_mask(rtwdev, R_NCTL_RPT, MASKDWORD);
	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[IQK]S%x, type= %x, 0x8008 = 0x%x\n", path, ktype, tmp);

	return false;
}

static bool _iqk_one_shot(struct rtw89_dev *rtwdev,
			  enum rtw89_phy_idx phy_idx, u8 path, u8 ktype)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	u32 addr_rfc_ctl = R_UPD_CLK + (path << 13);
	u32 iqk_cmd;
	bool fail;

	switch (ktype) {
	case ID_TXAGC:
		iqk_cmd = 0x008 | (1 << (4 + path)) | (path << 1);
		break;
	case ID_A_FLOK_COARSE:
		rtw89_phy_write32_mask(rtwdev, addr_rfc_ctl, 0x00000002, 0x1);
		iqk_cmd = 0x008 | (1 << (4 + path));
		break;
	case ID_G_FLOK_COARSE:
		rtw89_phy_write32_mask(rtwdev, addr_rfc_ctl, 0x00000002, 0x1);
		iqk_cmd = 0x108 | (1 << (4 + path));
		break;
	case ID_A_FLOK_FINE:
		rtw89_phy_write32_mask(rtwdev, addr_rfc_ctl, 0x00000002, 0x1);
		iqk_cmd = 0x508 | (1 << (4 + path));
		break;
	case ID_G_FLOK_FINE:
		rtw89_phy_write32_mask(rtwdev, addr_rfc_ctl, 0x00000002, 0x1);
		iqk_cmd = 0x208 | (1 << (4 + path));
		break;
	case ID_FLOK_VBUFFER:
		rtw89_phy_write32_mask(rtwdev, addr_rfc_ctl, 0x00000002, 0x1);
		iqk_cmd = 0x308 | (1 << (4 + path));
		break;
	case ID_TXK:
		rtw89_phy_write32_mask(rtwdev, addr_rfc_ctl, 0x00000002, 0x0);
		iqk_cmd = 0x008 | (1 << (4 + path)) | ((0x8 + iqk_info->iqk_bw[path]) << 8);
		break;
	case ID_RXAGC:
		iqk_cmd = 0x508 | (1 << (4 + path)) | (path << 1);
		break;
	case ID_RXK:
		rtw89_phy_write32_mask(rtwdev, addr_rfc_ctl, 0x00000002, 0x1);
		iqk_cmd = 0x008 | (1 << (4 + path)) | ((0xc + iqk_info->iqk_bw[path]) << 8);
		break;
	case ID_NBTXK:
		rtw89_phy_write32_mask(rtwdev, addr_rfc_ctl, 0x00000002, 0x0);
		iqk_cmd = 0x408 | (1 << (4 + path));
		break;
	case ID_NBRXK:
		rtw89_phy_write32_mask(rtwdev, addr_rfc_ctl, 0x00000002, 0x1);
		iqk_cmd = 0x608 | (1 << (4 + path));
		break;
	default:
		return false;
	}

	rtw89_phy_write32_mask(rtwdev, R_NCTL_CFG, MASKDWORD, iqk_cmd + 1);
	fsleep(15);
	fail = _iqk_check_cal(rtwdev, path, ktype);
	rtw89_phy_write32_mask(rtwdev, addr_rfc_ctl, 0x00000002, 0x0);

	return fail;
}

static bool _rxk_group_sel(struct rtw89_dev *rtwdev,
			   enum rtw89_phy_idx phy_idx, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	bool fail;
	u32 tmp;
	u32 bkrf0;
	u8 gp;

	bkrf0 = rtw89_read_rf(rtwdev, path, RR_MOD, RR_MOD_NBW);
	if (path == RF_PATH_B) {
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_IQKPLL, RR_IQKPLL_MOD, 0x3);
		tmp = rtw89_read_rf(rtwdev, RF_PATH_B, RR_CHTR, RR_CHTR_MOD);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_RSV4, RR_RSV4_AGH, tmp);
		tmp = rtw89_read_rf(rtwdev, RF_PATH_B, RR_CHTR, RR_CHTR_TXRX);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_RSV4, RR_RSV4_PLLCH, tmp);
	}

	switch (iqk_info->iqk_band[path]) {
	case RTW89_BAND_2G:
	default:
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_MASK, 0xc);
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_NBW, 0x0);
		rtw89_write_rf(rtwdev, path, RR_RXG, RR_RXG_IQKMOD, 0x9);
		break;
	case RTW89_BAND_5G:
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_MASK, 0xc);
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_NBW, 0x0);
		rtw89_write_rf(rtwdev, path, RR_RXAE, RR_RXAE_IQKMOD, 0x8);
		break;
	case RTW89_BAND_6G:
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_MASK, 0xc);
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_NBW, 0x0);
		rtw89_write_rf(rtwdev, path, RR_RXAE, RR_RXAE_IQKMOD, 0x9);
		break;
	}

	fsleep(10);

	for (gp = 0; gp < RXK_GROUP_NR; gp++) {
		switch (iqk_info->iqk_band[path]) {
		case RTW89_BAND_2G:
		default:
			rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_M_RXG,
				       _rxk_g_idxrxgain[gp]);
			rtw89_write_rf(rtwdev, path, RR_RXBB, RR_RXBB_VOBUF,
				       _rxk_g_idxattc2[gp]);
			break;
		case RTW89_BAND_5G:
			rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_M_RXG,
				       _rxk_a_idxrxgain[gp]);
			rtw89_write_rf(rtwdev, path, RR_RXA2, RR_RXA2_IATT,
				       _rxk_a_idxattc2[gp]);
			break;
		case RTW89_BAND_6G:
			rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_M_RXG,
				       _rxk_a6_idxrxgain[gp]);
			rtw89_write_rf(rtwdev, path, RR_RXA2, RR_RXA2_IATT,
				       _rxk_a6_idxattc2[gp]);
			break;
		}
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8),
				       B_CFIR_LUT_SEL, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8),
				       B_CFIR_LUT_SET, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8),
				       B_CFIR_LUT_GP_V1, gp);
		fail = _iqk_one_shot(rtwdev, phy_idx, path, ID_RXK);
	}

	if (path == RF_PATH_B)
		rtw89_write_rf(rtwdev, path, RR_IQKPLL, RR_IQKPLL_MOD, 0x0);
	rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_NBW, bkrf0);

	if (fail) {
		iqk_info->nb_rxcfir[path] = 0x40000002;
		iqk_info->is_wb_rxiqk[path] = false;
	} else {
		iqk_info->nb_rxcfir[path] = 0x40000000;
		iqk_info->is_wb_rxiqk[path] = true;
	}

	return false;
}

static bool _iqk_nbrxk(struct rtw89_dev *rtwdev,
		       enum rtw89_phy_idx phy_idx, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	bool fail;
	u32 tmp;
	u32 bkrf0;
	u8 gp = 0x2;

	bkrf0 = rtw89_read_rf(rtwdev, path, RR_MOD, RR_MOD_NBW);
	if (path == RF_PATH_B) {
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_IQKPLL, RR_IQKPLL_MOD, 0x3);
		tmp = rtw89_read_rf(rtwdev, RF_PATH_B, RR_CHTR, RR_CHTR_MOD);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_RSV4, RR_RSV4_AGH, tmp);
		tmp = rtw89_read_rf(rtwdev, RF_PATH_B, RR_CHTR, RR_CHTR_TXRX);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_RSV4, RR_RSV4_PLLCH, tmp);
	}

	switch (iqk_info->iqk_band[path]) {
	case RTW89_BAND_2G:
	default:
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_MASK, 0xc);
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_NBW, 0x0);
		rtw89_write_rf(rtwdev, path, RR_RXG, RR_RXG_IQKMOD, 0x9);
		break;
	case RTW89_BAND_5G:
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_MASK, 0xc);
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_NBW, 0x0);
		rtw89_write_rf(rtwdev, path, RR_RXAE, RR_RXAE_IQKMOD, 0x8);
		break;
	case RTW89_BAND_6G:
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_MASK, 0xc);
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_NBW, 0x0);
		rtw89_write_rf(rtwdev, path, RR_RXAE, RR_RXAE_IQKMOD, 0x9);
		break;
	}

	fsleep(10);

	switch (iqk_info->iqk_band[path]) {
	case RTW89_BAND_2G:
	default:
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_M_RXG, _rxk_g_idxrxgain[gp]);
		rtw89_write_rf(rtwdev, path, RR_RXBB, RR_RXBB_VOBUF, _rxk_g_idxattc2[gp]);
		break;
	case RTW89_BAND_5G:
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_M_RXG, _rxk_a_idxrxgain[gp]);
		rtw89_write_rf(rtwdev, path, RR_RXA2, RR_RXA2_IATT, _rxk_a_idxattc2[gp]);
		break;
	case RTW89_BAND_6G:
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_M_RXG, _rxk_a6_idxrxgain[gp]);
		rtw89_write_rf(rtwdev, path, RR_RXA2, RR_RXA2_IATT, _rxk_a6_idxattc2[gp]);
		break;
	}

	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_SEL, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_SET, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_GP_V1, gp);
	fail = _iqk_one_shot(rtwdev, phy_idx, path, ID_RXK);

	if (path == RF_PATH_B)
		rtw89_write_rf(rtwdev, path, RR_IQKPLL, RR_IQKPLL_MOD, 0x0);

	rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_NBW, bkrf0);

	if (fail)
		iqk_info->nb_rxcfir[path] =
			rtw89_phy_read32_mask(rtwdev, R_RXIQC + (path << 8),
					      MASKDWORD) | 0x2;
	else
		iqk_info->nb_rxcfir[path] = 0x40000002;

	iqk_info->is_wb_rxiqk[path] = false;
	return fail;
}

static bool _txk_group_sel(struct rtw89_dev *rtwdev,
			   enum rtw89_phy_idx phy_idx, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	bool fail;
	u8 gp;

	for (gp = 0; gp < TXK_GROUP_NR; gp++) {
		switch (iqk_info->iqk_band[path]) {
		case RTW89_BAND_2G:
			rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR0,
				       _txk_g_power_range[gp]);
			rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR1,
				       _txk_g_track_range[gp]);
			rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG,
				       _txk_g_gain_bb[gp]);
			rtw89_phy_write32_mask(rtwdev,
					       R_KIP_IQP + (path << 8),
					       MASKDWORD, _txk_g_itqt[gp]);
			break;
		case RTW89_BAND_5G:
			rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR0,
				       _txk_a_power_range[gp]);
			rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR1,
				       _txk_a_track_range[gp]);
			rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG,
				       _txk_a_gain_bb[gp]);
			rtw89_phy_write32_mask(rtwdev,
					       R_KIP_IQP + (path << 8),
					       MASKDWORD, _txk_a_itqt[gp]);
			break;
		case RTW89_BAND_6G:
			rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR0,
				       _txk_a6_power_range[gp]);
			rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR1,
				       _txk_a6_track_range[gp]);
			rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG,
				       _txk_a6_gain_bb[gp]);
			rtw89_phy_write32_mask(rtwdev,
					       R_KIP_IQP + (path << 8),
					       MASKDWORD, _txk_a6_itqt[gp]);
			break;
		default:
			break;
		}
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8),
				       B_CFIR_LUT_SEL, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8),
				       B_CFIR_LUT_SET, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8),
				       B_CFIR_LUT_G2, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8),
				       B_CFIR_LUT_GP, gp + 1);
		rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_TXT, 0x00b);
		rtw89_phy_write32_mask(rtwdev, R_NCTL_N1, B_NCTL_N1_CIP, 0x00);
		fail = _iqk_one_shot(rtwdev, phy_idx, path, ID_TXK);
	}

	if (fail) {
		iqk_info->nb_txcfir[path] = 0x40000002;
		iqk_info->is_wb_txiqk[path] = false;
	} else {
		iqk_info->nb_txcfir[path] = 0x40000000;
		iqk_info->is_wb_txiqk[path] = true;
	}

	return fail;
}

static bool _iqk_nbtxk(struct rtw89_dev *rtwdev,
		       enum rtw89_phy_idx phy_idx, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	bool fail;
	u8 gp = 0x2;

	switch (iqk_info->iqk_band[path]) {
	case RTW89_BAND_2G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR0, _txk_g_power_range[gp]);
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR1, _txk_g_track_range[gp]);
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, _txk_g_gain_bb[gp]);
		rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8),
				       MASKDWORD, _txk_g_itqt[gp]);
		break;
	case RTW89_BAND_5G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR0, _txk_a_power_range[gp]);
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR1, _txk_a_track_range[gp]);
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, _txk_a_gain_bb[gp]);
		rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8),
				       MASKDWORD, _txk_a_itqt[gp]);
		break;
	case RTW89_BAND_6G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR0, _txk_a6_power_range[gp]);
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR1, _txk_a6_track_range[gp]);
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, _txk_a6_gain_bb[gp]);
		rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8),
				       MASKDWORD, _txk_a6_itqt[gp]);
	break;
	default:
		break;
	}

	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_SEL, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_SET, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_G2, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_GP, gp + 1);
	rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_TXT, 0x00b);
	rtw89_phy_write32_mask(rtwdev, R_NCTL_N1, B_NCTL_N1_CIP, 0x00);
	fail = _iqk_one_shot(rtwdev, phy_idx, path, ID_NBTXK);

	if (!fail)
		iqk_info->nb_txcfir[path] =
			rtw89_phy_read32_mask(rtwdev, R_TXIQC + (path << 8),
					      MASKDWORD) | 0x2;
	else
		iqk_info->nb_txcfir[path] = 0x40000002;

	iqk_info->is_wb_txiqk[path] = false;

	return fail;
}

static bool _lok_finetune_check(struct rtw89_dev *rtwdev, u8 path)
{
	struct rtw89_rfk_mcc_info *rfk_mcc = &rtwdev->rfk_mcc;
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	u8 idx = rfk_mcc->table_idx;
	bool is_fail1,  is_fail2;
	u32 val;
	u32 core_i;
	u32 core_q;
	u32 vbuff_i;
	u32 vbuff_q;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]===>%s\n", __func__);
	val = rtw89_read_rf(rtwdev,  path, RR_TXMO, RFREG_MASK);
	core_i = FIELD_GET(RR_TXMO_COI, val);
	core_q = FIELD_GET(RR_TXMO_COQ, val);

	if (core_i < 0x2 || core_i > 0x1d || core_q < 0x2 || core_q > 0x1d)
		is_fail1 = true;
	else
		is_fail1 = false;

	iqk_info->lok_idac[idx][path] = val;

	val = rtw89_read_rf(rtwdev, path, RR_LOKVB, RFREG_MASK);
	vbuff_i = FIELD_GET(RR_LOKVB_COI, val);
	vbuff_q = FIELD_GET(RR_LOKVB_COQ, val);

	if (vbuff_i < 0x2 || vbuff_i > 0x3d || vbuff_q < 0x2 || vbuff_q > 0x3d)
		is_fail2 = true;
	else
		is_fail2 = false;

	iqk_info->lok_vbuf[idx][path] = val;

	return is_fail1 || is_fail2;
}

static bool _iqk_lok(struct rtw89_dev *rtwdev,
		     enum rtw89_phy_idx phy_idx, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	u8 tmp_id = 0x0;
	bool fail = false;
	bool tmp = false;

	/* Step 0: Init RF gain & tone idx= 8.25Mhz */
	rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_TXT, IQK_DF4_TXT_8_25MHZ);

	/* Step 1  START: _lok_coarse_fine_wi_swap */
	switch (iqk_info->iqk_band[path]) {
	case RTW89_BAND_2G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, 0x6);
		rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8),
				       B_KIP_IQP_IQSW, 0x9);
		tmp_id = ID_G_FLOK_COARSE;
		break;
	case RTW89_BAND_5G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, 0x6);
		rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8),
				       B_KIP_IQP_IQSW, 0x9);
		tmp_id = ID_A_FLOK_COARSE;
		break;
	case RTW89_BAND_6G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, 0x6);
		rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8),
				       B_KIP_IQP_IQSW, 0x9);
		tmp_id = ID_A_FLOK_COARSE;
		break;
	default:
		break;
	}
	tmp = _iqk_one_shot(rtwdev, phy_idx, path, tmp_id);
	iqk_info->lok_cor_fail[0][path] = tmp;

	/* Step 2 */
	switch (iqk_info->iqk_band[path]) {
	case RTW89_BAND_2G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, 0x12);
		rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8),
				       B_KIP_IQP_IQSW, 0x1b);
		break;
	case RTW89_BAND_5G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, 0x12);
		rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8),
				       B_KIP_IQP_IQSW, 0x1b);
		break;
	case RTW89_BAND_6G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, 0x12);
		rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8),
				       B_KIP_IQP_IQSW, 0x1b);
		break;
	default:
		break;
	}
	tmp = _iqk_one_shot(rtwdev, phy_idx, path, ID_FLOK_VBUFFER);

	/* Step 3 */
	switch (iqk_info->iqk_band[path]) {
	case RTW89_BAND_2G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, 0x6);
		rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8),
				       B_KIP_IQP_IQSW, 0x9);
		tmp_id = ID_G_FLOK_FINE;
		break;
	case RTW89_BAND_5G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, 0x6);
		rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8),
				       B_KIP_IQP_IQSW, 0x9);
		tmp_id = ID_A_FLOK_FINE;
		break;
	case RTW89_BAND_6G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, 0x6);
		rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8),
				       B_KIP_IQP_IQSW, 0x9);
		tmp_id = ID_A_FLOK_FINE;
		break;
	default:
		break;
	}
	tmp = _iqk_one_shot(rtwdev, phy_idx, path, tmp_id);
	iqk_info->lok_fin_fail[0][path] = tmp;

	/* Step 4 large rf gain */
	switch (iqk_info->iqk_band[path]) {
	case RTW89_BAND_2G:
	default:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, 0x12);
		rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8),
				       B_KIP_IQP_IQSW, 0x1b);
		break;
	case RTW89_BAND_5G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, 0x12);
		rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8),
				       B_KIP_IQP_IQSW, 0x1b);
		break;
	case RTW89_BAND_6G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, 0x12);
		rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8),
				       B_KIP_IQP_IQSW, 0x1b);
		break;
	}
	tmp = _iqk_one_shot(rtwdev, phy_idx, path, ID_FLOK_VBUFFER);
	fail = _lok_finetune_check(rtwdev, path);

	return fail;
}

static void _iqk_txk_setting(struct rtw89_dev *rtwdev, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;

	switch (iqk_info->iqk_band[path]) {
	case RTW89_BAND_2G:
	default:
		rtw89_write_rf(rtwdev, path, RR_TXG1, RR_TXG1_ATT2, 0x0);
		rtw89_write_rf(rtwdev, path, RR_TXG1, RR_TXG1_ATT1, 0x0);
		rtw89_write_rf(rtwdev, path, RR_TXG2, RR_TXG2_ATT0, 0x1);
		rtw89_write_rf(rtwdev, path, RR_TXA2, RR_TXA2_LDO, 0xf);
		rtw89_write_rf(rtwdev, path, RR_TXGA, RR_TXGA_LOK_EXT, 0x0);
		rtw89_write_rf(rtwdev, path, RR_LUTWE, RR_LUTWE_LOK, 0x1);
		rtw89_write_rf(rtwdev, path, RR_MOD, RFREG_MASK,
			       0x403e0 | iqk_info->syn1to2);
		fsleep(10);
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR0, 0x0);
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR1, 0x6);
		break;
	case RTW89_BAND_5G:
		rtw89_write_rf(rtwdev, path, RR_TXATANK, RR_TXATANK_LBSW2, 0x0);
		rtw89_write_rf(rtwdev, path, RR_TXPOW, RR_TXPOW_TXAS, 0x1);
		rtw89_write_rf(rtwdev, path, RR_TXA2, RR_TXA2_LDO, 0xf);
		rtw89_write_rf(rtwdev, path, RR_TXGA, RR_TXGA_LOK_EXT, 0x0);
		rtw89_write_rf(rtwdev, path, RR_LUTWE, RR_LUTWE_LOK, 0x1);
		rtw89_write_rf(rtwdev, path, RR_MOD, RFREG_MASK,
			       0x403e0 | iqk_info->syn1to2);
		fsleep(10);
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR0, 0x0);
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR1, 0x6);
		break;
	case RTW89_BAND_6G:
		rtw89_write_rf(rtwdev, path, RR_TXATANK, RR_TXATANK_LBSW2, 0x0);
		rtw89_write_rf(rtwdev, path, RR_TXPOW, RR_TXPOW_TXAS, 0x1);
		rtw89_write_rf(rtwdev, path, RR_TXA2, RR_TXA2_LDO, 0xf);
		rtw89_write_rf(rtwdev, path, RR_TXGA, RR_TXGA_LOK_EXT, 0x0);
		rtw89_write_rf(rtwdev, path, RR_LUTWE, RR_LUTWE_LOK, 0x1);
		rtw89_write_rf(rtwdev, path, RR_MOD, RFREG_MASK,
			       0x403e0  | iqk_info->syn1to2);
		fsleep(10);
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR0, 0x0);
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR1, 0x6);
		break;
	}
}

static void _iqk_info_iqk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx,
			  u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	u32 tmp;
	bool flag;

	iqk_info->thermal[path] =
		ewma_thermal_read(&rtwdev->phystat.avg_thermal[path]);
	iqk_info->thermal_rek_en = false;
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%d_thermal = %d\n", path,
		    iqk_info->thermal[path]);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%d_LOK_COR_fail= %d\n", path,
		    iqk_info->lok_cor_fail[0][path]);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%d_LOK_FIN_fail= %d\n", path,
		    iqk_info->lok_fin_fail[0][path]);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%d_TXIQK_fail = %d\n", path,
		    iqk_info->iqk_tx_fail[0][path]);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%d_RXIQK_fail= %d,\n", path,
		    iqk_info->iqk_rx_fail[0][path]);

	flag = iqk_info->lok_cor_fail[0][path];
	rtw89_phy_write32_mask(rtwdev, R_IQKINF, B_IQKINF_FCOR << (path * 4), flag);
	flag = iqk_info->lok_fin_fail[0][path];
	rtw89_phy_write32_mask(rtwdev, R_IQKINF, B_IQKINF_FFIN << (path * 4), flag);
	flag = iqk_info->iqk_tx_fail[0][path];
	rtw89_phy_write32_mask(rtwdev, R_IQKINF, B_IQKINF_FTX << (path * 4), flag);
	flag = iqk_info->iqk_rx_fail[0][path];
	rtw89_phy_write32_mask(rtwdev, R_IQKINF, B_IQKINF_F_RX << (path * 4), flag);

	tmp = rtw89_phy_read32_mask(rtwdev, R_IQK_RES + (path << 8), MASKDWORD);
	iqk_info->bp_iqkenable[path] = tmp;
	tmp = rtw89_phy_read32_mask(rtwdev, R_TXIQC + (path << 8), MASKDWORD);
	iqk_info->bp_txkresult[path] = tmp;
	tmp = rtw89_phy_read32_mask(rtwdev, R_RXIQC + (path << 8), MASKDWORD);
	iqk_info->bp_rxkresult[path] = tmp;

	rtw89_phy_write32_mask(rtwdev, R_IQKINF2, B_IQKINF2_KCNT,
			       iqk_info->iqk_times);

	tmp = rtw89_phy_read32_mask(rtwdev, R_IQKINF, B_IQKINF_FAIL << (path * 4));
	if (tmp != 0x0)
		iqk_info->iqk_fail_cnt++;
	rtw89_phy_write32_mask(rtwdev, R_IQKINF2, B_IQKINF2_FCNT << (path * 4),
			       iqk_info->iqk_fail_cnt);
}

static void _iqk_by_path(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;

	_iqk_txk_setting(rtwdev, path);
	iqk_info->lok_fail[path] = _iqk_lok(rtwdev, phy_idx, path);

	if (iqk_info->is_nbiqk)
		iqk_info->iqk_tx_fail[0][path] = _iqk_nbtxk(rtwdev, phy_idx, path);
	else
		iqk_info->iqk_tx_fail[0][path] = _txk_group_sel(rtwdev, phy_idx, path);

	_iqk_rxk_setting(rtwdev, path);
	if (iqk_info->is_nbiqk)
		iqk_info->iqk_rx_fail[0][path] = _iqk_nbrxk(rtwdev, phy_idx, path);
	else
		iqk_info->iqk_rx_fail[0][path] = _rxk_group_sel(rtwdev, phy_idx, path);

	_iqk_info_iqk(rtwdev, phy_idx, path);
}

static void _iqk_get_ch_info(struct rtw89_dev *rtwdev,
			     enum rtw89_phy_idx phy, u8 path)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]===>%s\n", __func__);

	iqk_info->iqk_band[path] = chan->band_type;
	iqk_info->iqk_bw[path] = chan->band_width;
	iqk_info->iqk_ch[path] = chan->channel;

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[IQK]iqk_info->iqk_band[%x] = 0x%x\n", path,
		    iqk_info->iqk_band[path]);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]iqk_info->iqk_bw[%x] = 0x%x\n",
		    path, iqk_info->iqk_bw[path]);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]iqk_info->iqk_ch[%x] = 0x%x\n",
		    path, iqk_info->iqk_ch[path]);
	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[IQK]S%d (PHY%d): / DBCC %s/ %s/ CH%d/ %s\n", path, phy,
		    rtwdev->dbcc_en ? "on" : "off",
		    iqk_info->iqk_band[path] == 0 ? "2G" :
		    iqk_info->iqk_band[path] == 1 ? "5G" : "6G",
		    iqk_info->iqk_ch[path],
		    iqk_info->iqk_bw[path] == 0 ? "20M" :
		    iqk_info->iqk_bw[path] == 1 ? "40M" : "80M");
	if (!rtwdev->dbcc_en)
		iqk_info->syn1to2 = 0x1;
	else
		iqk_info->syn1to2 = 0x3;

	rtw89_phy_write32_mask(rtwdev, R_IQKINF, B_IQKINF_VER, RTW8852C_IQK_VER);
	rtw89_phy_write32_mask(rtwdev, R_IQKCH, B_IQKCH_BAND << (path * 16),
			       iqk_info->iqk_band[path]);
	rtw89_phy_write32_mask(rtwdev, R_IQKCH, B_IQKCH_BW << (path * 16),
			       iqk_info->iqk_bw[path]);
	rtw89_phy_write32_mask(rtwdev, R_IQKCH, B_IQKCH_CH << (path * 16),
			       iqk_info->iqk_ch[path]);

	rtw89_phy_write32_mask(rtwdev, R_IQKINF2, B_IQKINF2_NCTLV, RTW8852C_NCTL_VER);
}

static void _iqk_start_iqk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx,
			   u8 path)
{
	_iqk_by_path(rtwdev, phy_idx, path);
}

static void _iqk_restore(struct rtw89_dev *rtwdev, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	bool fail;

	rtw89_phy_write32_mask(rtwdev, R_TXIQC + (path << 8), MASKDWORD,
			       iqk_info->nb_txcfir[path]);
	rtw89_phy_write32_mask(rtwdev, R_RXIQC + (path << 8), MASKDWORD,
			       iqk_info->nb_rxcfir[path]);
	rtw89_phy_write32_mask(rtwdev, R_NCTL_CFG, MASKDWORD,
			       0x00001219 + (path << 4));
	fsleep(200);
	fail = _iqk_check_cal(rtwdev, path, 0x12);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK] restore fail  = %x\n", fail);

	rtw89_phy_write32_mask(rtwdev, R_NCTL_N1, B_NCTL_N1_CIP, 0x00);
	rtw89_phy_write32_mask(rtwdev, R_NCTL_RPT, MASKDWORD, 0x00000000);
	rtw89_phy_write32_mask(rtwdev, R_KIP_SYSCFG, MASKDWORD, 0x80000000);

	rtw89_write_rf(rtwdev, path, RR_LUTWE, RR_LUTWE_LOK, 0x0);
	rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_MASK, RR_MOD_V_RX);
	rtw89_write_rf(rtwdev, path, RR_RSV1, RR_RSV1_RST, 0x1);
}

static void _iqk_afebb_restore(struct rtw89_dev *rtwdev,
			       enum rtw89_phy_idx phy_idx, u8 path)
{
	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852c_iqk_afebb_restore_defs_a_tbl,
				 &rtw8852c_iqk_afebb_restore_defs_b_tbl);

	rtw8852c_disable_rxagc(rtwdev, path, 0x1);
}

static void _iqk_preset(struct rtw89_dev *rtwdev, u8 path)
{
	struct rtw89_rfk_mcc_info *rfk_mcc = &rtwdev->rfk_mcc;
	u8 idx = 0;

	idx = rfk_mcc->table_idx;
	rtw89_phy_write32_mask(rtwdev, R_COEF_SEL + (path << 8), B_COEF_SEL_IQC, idx);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_G3, idx);
	rtw89_write_rf(rtwdev, path, RR_RSV1, RR_RSV1_RST, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_NCTL_RPT, MASKDWORD, 0x00000080);
	rtw89_phy_write32_mask(rtwdev, R_KIP_SYSCFG, MASKDWORD, 0x81ff010a);
}

static void _iqk_macbb_setting(struct rtw89_dev *rtwdev,
			       enum rtw89_phy_idx phy_idx, u8 path)
{
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]===> %s\n", __func__);

	/* 01_BB_AFE_for DPK_S0_20210820 */
	rtw89_write_rf(rtwdev,  path, RR_BBDC, RR_BBDC_SEL, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_A0 << path, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_A1 << path, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_A2 << path, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_A3 << path, 0x0);

	/* disable rxgac */
	rtw8852c_disable_rxagc(rtwdev, path, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_UPD_CLK | (path << 13), MASKDWORD, 0xf801fffd);
	rtw89_phy_write32_mask(rtwdev, R_UPD_CLK | (path << 13), B_DPD_DIS, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_UPD_CLK | (path << 13), B_DAC_VAL, 0x1);

	rtw8852c_txck_force(rtwdev, path, true, DAC_960M);
	rtw89_phy_write32_mask(rtwdev, R_UPD_CLK | (path << 13), B_DPD_GDIS, 0x1);

	rtw8852c_rxck_force(rtwdev, path, true, ADC_1920M);
	rtw89_phy_write32_mask(rtwdev, R_UPD_CLK | (path << 13), B_ACK_VAL, 0x2);

	rtw89_phy_write32_mask(rtwdev, R_P0_CFCH_BW0 | (path << 8), B_P0_CFCH_BW0, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_P0_CFCH_BW1 | (path << 8), B_P0_CFCH_BW1, 0xb);
	rtw89_phy_write32_mask(rtwdev, R_P0_NRBW | (path << 13), B_P0_NRBW_DBG, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR_PW15, B_ANAPAR_PW15, 0x1f);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR_PW15, B_ANAPAR_PW15, 0x13);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_15, 0x0001);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_15, 0x0041);
	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_A1 << path, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_A3 << path, 0x1);
}

static void _rck(struct rtw89_dev *rtwdev, enum rtw89_rf_path path)
{
	u32 rf_reg5, rck_val = 0;
	u32 val;
	int ret;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[RCK] ====== S%d RCK ======\n", path);

	rf_reg5 = rtw89_read_rf(rtwdev, path, RR_RSV1, RFREG_MASK);

	rtw89_write_rf(rtwdev, path, RR_RSV1, RR_RSV1_RST, 0x0);
	rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_MASK, RR_MOD_V_RX);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[RCK] RF0x00 = 0x%x\n",
		    rtw89_read_rf(rtwdev, path, RR_MOD, RFREG_MASK));

	/* RCK trigger */
	rtw89_write_rf(rtwdev, path, RR_RCKC, RFREG_MASK, 0x00240);

	ret = read_poll_timeout_atomic(rtw89_read_rf, val, val, 2, 20,
				       false, rtwdev, path, 0x1c, BIT(3));
	if (ret)
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[RCK] RCK timeout\n");

	rck_val = rtw89_read_rf(rtwdev, path, RR_RCKC, RR_RCKC_CA);
	rtw89_write_rf(rtwdev, path, RR_RCKC, RFREG_MASK, rck_val);

	rtw89_write_rf(rtwdev, path, RR_RSV1, RFREG_MASK, rf_reg5);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[RCK] RF 0x1b / 0x1c = 0x%x / 0x%x\n",
		    rtw89_read_rf(rtwdev, path, RR_RCKC, RFREG_MASK),
		    rtw89_read_rf(rtwdev, path, RR_RCKS, RFREG_MASK));
}

static void _iqk_init(struct rtw89_dev *rtwdev)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	u8 ch, path;

	rtw89_phy_write32_clr(rtwdev, R_IQKINF, MASKDWORD);
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

	for (ch = 0; ch < RTW89_IQK_CHS_NR; ch++) {
		iqk_info->iqk_channel[ch] = 0x0;
		for (path = 0; path < RTW8852C_IQK_SS; path++) {
			iqk_info->lok_cor_fail[ch][path] = false;
			iqk_info->lok_fin_fail[ch][path] = false;
			iqk_info->iqk_tx_fail[ch][path] = false;
			iqk_info->iqk_rx_fail[ch][path] = false;
			iqk_info->iqk_mcc_ch[ch][path] = 0x0;
			iqk_info->iqk_table_idx[path] = 0x0;
		}
	}
}

static void _doiqk(struct rtw89_dev *rtwdev, bool force,
		   enum rtw89_phy_idx phy_idx, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	u32 backup_bb_val[BACKUP_BB_REGS_NR];
	u32 backup_rf_val[RTW8852C_IQK_SS][BACKUP_RF_REGS_NR];
	u8 phy_map = rtw89_btc_phymap(rtwdev, phy_idx, RF_AB);

	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_IQK, BTC_WRFK_ONESHOT_START);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[IQK]==========IQK strat!!!!!==========\n");
	iqk_info->iqk_times++;
	iqk_info->kcount = 0;
	iqk_info->version = RTW8852C_IQK_VER;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]Test Ver 0x%x\n", iqk_info->version);
	_iqk_get_ch_info(rtwdev, phy_idx, path);
	_rfk_backup_bb_reg(rtwdev, backup_bb_val);
	_rfk_backup_rf_reg(rtwdev, backup_rf_val[path], path);
	_iqk_macbb_setting(rtwdev, phy_idx, path);
	_iqk_preset(rtwdev, path);
	_iqk_start_iqk(rtwdev, phy_idx, path);
	_iqk_restore(rtwdev, path);
	_iqk_afebb_restore(rtwdev, phy_idx, path);
	_rfk_restore_bb_reg(rtwdev, backup_bb_val);
	_rfk_restore_rf_reg(rtwdev, backup_rf_val[path], path);
	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_IQK, BTC_WRFK_ONESHOT_STOP);
}

static void _iqk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx, bool force)
{
	switch (_kpath(rtwdev, phy_idx)) {
	case RF_A:
		_doiqk(rtwdev, force, phy_idx, RF_PATH_A);
		break;
	case RF_B:
		_doiqk(rtwdev, force, phy_idx, RF_PATH_B);
		break;
	case RF_AB:
		_doiqk(rtwdev, force, phy_idx, RF_PATH_A);
		_doiqk(rtwdev, force, phy_idx, RF_PATH_B);
		break;
	default:
		break;
	}
}

static void _rx_dck_toggle(struct rtw89_dev *rtwdev, u8 path)
{
	int ret;
	u32 val;

	rtw89_write_rf(rtwdev, path, RR_DCK, RR_DCK_LV, 0x0);
	rtw89_write_rf(rtwdev, path, RR_DCK, RR_DCK_LV, 0x1);

	ret = read_poll_timeout_atomic(rtw89_read_rf, val, val,
				       2, 2000, false, rtwdev, path,
				       RR_DCK1, RR_DCK1_DONE);
	if (ret)
		rtw89_warn(rtwdev, "[RX_DCK] S%d RXDCK timeout\n", path);
	else
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[RX_DCK] S%d RXDCK finish\n", path);

	rtw89_write_rf(rtwdev, path, RR_DCK, RR_DCK_LV, 0x0);
}

static void _set_rx_dck(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy, u8 path,
			bool is_afe)
{
	u8 res;

	rtw89_write_rf(rtwdev, path, RR_DCK1, RR_DCK1_CLR, 0x0);

	_rx_dck_toggle(rtwdev, path);
	if (rtw89_read_rf(rtwdev, path, RR_DCKC, RR_DCKC_CHK) == 0)
		return;
	res = rtw89_read_rf(rtwdev, path, RR_DCK, RR_DCK_DONE);
	if (res > 1) {
		rtw89_write_rf(rtwdev, path, RR_RXBB2, RR_RXBB2_IDAC, res);
		_rx_dck_toggle(rtwdev, path);
		rtw89_write_rf(rtwdev, path, RR_RXBB2, RR_RXBB2_IDAC, 0x1);
	}
}

#define RTW8852C_RF_REL_VERSION 34
#define RTW8852C_DPK_VER 0x10
#define RTW8852C_DPK_TH_AVG_NUM 4
#define RTW8852C_DPK_RF_PATH 2
#define RTW8852C_DPK_KIP_REG_NUM 5
#define RTW8852C_DPK_RXSRAM_DBG 0

enum rtw8852c_dpk_id {
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
	D_GAIN_NORM	= 0x2f,
	D_KIP_THERMAL	= 0x30,
	D_KIP_RESTORE	= 0x31
};

#define DPK_TXAGC_LOWER 0x2e
#define DPK_TXAGC_UPPER 0x3f
#define DPK_TXAGC_INVAL 0xff

enum dpk_agc_step {
	DPK_AGC_STEP_SYNC_DGAIN,
	DPK_AGC_STEP_GAIN_LOSS_IDX,
	DPK_AGC_STEP_GL_GT_CRITERION,
	DPK_AGC_STEP_GL_LT_CRITERION,
	DPK_AGC_STEP_SET_TX_GAIN,
};

static void _rf_direct_cntrl(struct rtw89_dev *rtwdev,
			     enum rtw89_rf_path path, bool is_bybb)
{
	if (is_bybb)
		rtw89_write_rf(rtwdev, path, RR_RSV1, RR_RSV1_RST, 0x1);
	else
		rtw89_write_rf(rtwdev, path, RR_RSV1, RR_RSV1_RST, 0x0);
}

static void _dpk_onoff(struct rtw89_dev *rtwdev,
		       enum rtw89_rf_path path, bool off);

static void _dpk_bkup_kip(struct rtw89_dev *rtwdev, const u32 reg[],
			  u32 reg_bkup[][RTW8852C_DPK_KIP_REG_NUM], u8 path)
{
	u8 i;

	for (i = 0; i < RTW8852C_DPK_KIP_REG_NUM; i++) {
		reg_bkup[path][i] =
			rtw89_phy_read32_mask(rtwdev, reg[i] + (path << 8), MASKDWORD);

		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] Backup 0x%x = %x\n",
			    reg[i] + (path << 8), reg_bkup[path][i]);
	}
}

static void _dpk_reload_kip(struct rtw89_dev *rtwdev, const u32 reg[],
			    u32 reg_bkup[][RTW8852C_DPK_KIP_REG_NUM], u8 path)
{
	u8 i;

	for (i = 0; i < RTW8852C_DPK_KIP_REG_NUM; i++) {
		rtw89_phy_write32_mask(rtwdev, reg[i] + (path << 8),
				       MASKDWORD, reg_bkup[path][i]);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] Reload 0x%x = %x\n",
			    reg[i] + (path << 8), reg_bkup[path][i]);
	}
}

static u8 _dpk_one_shot(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			enum rtw89_rf_path path, enum rtw8852c_dpk_id id)
{
	u16 dpk_cmd;
	u32 val;
	int ret;

	dpk_cmd = (u16)((id << 8) | (0x19 + path * 0x12));

	rtw89_phy_write32_mask(rtwdev, R_NCTL_CFG, MASKDWORD, dpk_cmd);

	ret = read_poll_timeout_atomic(rtw89_phy_read32_mask, val, val == 0x55,
				       10, 20000, false, rtwdev, 0xbff8, MASKBYTE0);
	udelay(10);
	rtw89_phy_write32_clr(rtwdev, R_NCTL_N1, MASKBYTE0);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DPK] one-shot for %s = 0x%x (ret=%d)\n",
		    id == 0x06 ? "LBK_RXIQK" :
		    id == 0x10 ? "SYNC" :
		    id == 0x11 ? "MDPK_IDL" :
		    id == 0x12 ? "MDPK_MPA" :
		    id == 0x13 ? "GAIN_LOSS" : "PWR_CAL",
		    dpk_cmd, ret);

	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[DPK] one-shot over 20ms!!!!\n");
		return 1;
	}

	return 0;
}

static void _dpk_information(struct rtw89_dev *rtwdev,
			     enum rtw89_phy_idx phy,
			     enum rtw89_rf_path path)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;

	u8 kidx = dpk->cur_idx[path];

	dpk->bp[path][kidx].band = chan->band_type;
	dpk->bp[path][kidx].ch = chan->channel;
	dpk->bp[path][kidx].bw = chan->band_width;

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DPK] S%d[%d] (PHY%d): TSSI %s/ DBCC %s/ %s/ CH%d/ %s\n",
		    path, dpk->cur_idx[path], phy,
		    rtwdev->is_tssi_mode[path] ? "on" : "off",
		    rtwdev->dbcc_en ? "on" : "off",
		    dpk->bp[path][kidx].band == 0 ? "2G" :
		    dpk->bp[path][kidx].band == 1 ? "5G" : "6G",
		    dpk->bp[path][kidx].ch,
		    dpk->bp[path][kidx].bw == 0 ? "20M" :
		    dpk->bp[path][kidx].bw == 1 ? "40M" : "80M");
}

static void _dpk_bb_afe_setting(struct rtw89_dev *rtwdev,
				enum rtw89_phy_idx phy,
				enum rtw89_rf_path path, u8 kpath)
{
	/*1. Keep ADC_fifo reset*/
	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_A0 << path, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_A1 << path, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_A2 << path, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_A3 << path, 0x0);

	/*2. BB for IQK DBG mode*/
	rtw89_phy_write32_mask(rtwdev, R_UPD_CLK + (path << 13), MASKDWORD, 0xd801dffd);

	/*3.Set DAC clk*/
	rtw8852c_txck_force(rtwdev, path, true, DAC_960M);

	/*4. Set ADC clk*/
	rtw8852c_rxck_force(rtwdev, path, true, ADC_1920M);
	rtw89_phy_write32_mask(rtwdev, R_P0_CFCH_BW0 + (path << 8), B_P0_CFCH_BW0, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_P0_CFCH_BW1 + (path << 8), B_P0_CFCH_BW1, 0xb);
	rtw89_phy_write32_mask(rtwdev, R_P0_NRBW + (path << 13),
			       B_P0_NRBW_DBG, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR_PW15, MASKBYTE3, 0x1f);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR_PW15, MASKBYTE3, 0x13);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, MASKHWORD, 0x0001);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, MASKHWORD, 0x0041);

	/*5. ADDA fifo rst*/
	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_A1 << path, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_A3 << path, 0x1);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] S%d BB/AFE setting\n", path);
}

static void _dpk_bb_afe_restore(struct rtw89_dev *rtwdev, u8 path)
{
	rtw89_phy_write32_mask(rtwdev, R_P0_NRBW + (path << 13),
			       B_P0_NRBW_DBG, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_A0 << path, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_A1 << path, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_A2 << path, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_A3 << path, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_UPD_CLK + (path << 13), MASKDWORD, 0x00000000);
	rtw89_phy_write32_mask(rtwdev, R_P0_RXCK + (path << 13), B_P0_TXCK_ALL, 0x00);
	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_A0 << path, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_A2 << path, 0x0);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] S%d BB/AFE restore\n", path);
}

static void _dpk_tssi_pause(struct rtw89_dev *rtwdev,
			    enum rtw89_rf_path path, bool is_pause)
{
	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_TRK + (path << 13),
			       B_P0_TSSI_TRK_EN, is_pause);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] S%d TSSI %s\n", path,
		    is_pause ? "pause" : "resume");
}

static void _dpk_kip_control_rfc(struct rtw89_dev *rtwdev, u8 path, bool ctrl_by_kip)
{
	rtw89_phy_write32_mask(rtwdev, R_UPD_CLK + (path << 13), B_IQK_RFC_ON, ctrl_by_kip);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] RFC is controlled by %s\n",
		    ctrl_by_kip ? "KIP" : "BB");
}

static void _dpk_txpwr_bb_force(struct rtw89_dev *rtwdev, u8 path, bool force)
{
	rtw89_phy_write32_mask(rtwdev, R_TXPWRB + (path << 13), B_TXPWRB_ON, force);
	rtw89_phy_write32_mask(rtwdev, R_TXPWRB_H + (path << 13), B_TXPWRB_RDY, force);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,  "[DPK] S%d txpwr_bb_force %s\n",
		    path, force ? "on" : "off");
}

static void _dpk_kip_restore(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			     enum rtw89_rf_path path)
{
	_dpk_one_shot(rtwdev, phy, path, D_KIP_RESTORE);
	_dpk_kip_control_rfc(rtwdev, path, false);
	_dpk_txpwr_bb_force(rtwdev, path, false);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] S%d restore KIP\n", path);
}

static void _dpk_lbk_rxiqk(struct rtw89_dev *rtwdev,
			   enum rtw89_phy_idx phy,
			   enum rtw89_rf_path path)
{
#define RX_TONE_IDX 0x00250025 /* Q.2 9.25MHz */
	u8 cur_rxbb;
	u32 rf_11, reg_81cc;

	rtw89_phy_write32_mask(rtwdev, R_DPD_V1 + (path << 8), B_DPD_LBK, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_MDPK_RX_DCK, B_MDPK_RX_DCK_EN, 0x1);

	_dpk_kip_control_rfc(rtwdev, path, false);

	cur_rxbb = rtw89_read_rf(rtwdev, path, RR_MOD, RR_MOD_M_RXBB);
	rf_11 = rtw89_read_rf(rtwdev, path, RR_TXIG, RFREG_MASK);
	reg_81cc = rtw89_phy_read32_mask(rtwdev, R_KIP_IQP + (path << 8),
					 B_KIP_IQP_SW);

	rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR0, 0x0);
	rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR1, 0x3);
	rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, 0xd);
	rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_M_RXBB, 0x1f);

	rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8), B_KIP_IQP_IQSW, 0x12);
	rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8), B_KIP_IQP_SW, 0x3);

	_dpk_kip_control_rfc(rtwdev, path, true);

	rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, MASKDWORD, RX_TONE_IDX);

	_dpk_one_shot(rtwdev, phy, path, LBK_RXIQK);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] S%d LBK RXIQC = 0x%x\n", path,
		    rtw89_phy_read32_mask(rtwdev, R_RXIQC + (path << 8), MASKDWORD));

	_dpk_kip_control_rfc(rtwdev, path, false);

	rtw89_write_rf(rtwdev, path, RR_TXIG, RFREG_MASK, rf_11);
	rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_M_RXBB, cur_rxbb);
	rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8), B_KIP_IQP_SW, reg_81cc);

	rtw89_phy_write32_mask(rtwdev, R_MDPK_RX_DCK, B_MDPK_RX_DCK_EN, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_KPATH_CFG, B_KPATH_CFG_ED, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_LOAD_COEF + (path << 8), B_LOAD_COEF_DI, 0x1);

	_dpk_kip_control_rfc(rtwdev, path, true);
}

static void _dpk_rf_setting(struct rtw89_dev *rtwdev, u8 gain,
			    enum rtw89_rf_path path, u8 kidx)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;

	if (dpk->bp[path][kidx].band == RTW89_BAND_2G) {
		rtw89_write_rf(rtwdev, path, RR_MOD, RFREG_MASK,
			       0x50121 | BIT(rtwdev->dbcc_en));
		rtw89_write_rf(rtwdev, path, RR_MOD_V1, RR_MOD_MASK, RF_DPK);
		rtw89_write_rf(rtwdev, path, RR_RXBB, RR_RXBB_ATTC, 0x2);
		rtw89_write_rf(rtwdev, path, RR_RXBB, RR_RXBB_ATTR, 0x4);
		rtw89_write_rf(rtwdev, path, RR_LUTDBG, RR_LUTDBG_TIA, 0x1);
		rtw89_write_rf(rtwdev, path, RR_TIA, RR_TIA_N6, 0x1);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[DPK] RF 0x0/0x83/0x9e/0x1a/0xdf/0x1001a = 0x%x/ 0x%x/ 0x%x/ 0x%x/ 0x%x/ 0x%x\n",
			    rtw89_read_rf(rtwdev, path, RR_MOD, RFREG_MASK),
			    rtw89_read_rf(rtwdev, path, RR_RXBB, RFREG_MASK),
			    rtw89_read_rf(rtwdev, path, RR_TIA, RFREG_MASK),
			    rtw89_read_rf(rtwdev, path, RR_BTC, RFREG_MASK),
			    rtw89_read_rf(rtwdev, path, RR_LUTDBG, RFREG_MASK),
			    rtw89_read_rf(rtwdev, path, 0x1001a, RFREG_MASK));
	} else {
		rtw89_write_rf(rtwdev, path, RR_MOD, RFREG_MASK,
			       0x50101 | BIT(rtwdev->dbcc_en));
		rtw89_write_rf(rtwdev, path, RR_MOD_V1, RR_MOD_MASK, RF_DPK);

		if (dpk->bp[path][kidx].band == RTW89_BAND_6G && dpk->bp[path][kidx].ch >= 161) {
			rtw89_write_rf(rtwdev, path, RR_IQGEN, RR_IQGEN_BIAS, 0x8);
			rtw89_write_rf(rtwdev, path, RR_LOGEN, RR_LOGEN_RPT, 0xd);
		} else {
			rtw89_write_rf(rtwdev, path, RR_LOGEN, RR_LOGEN_RPT, 0xd);
		}

		rtw89_write_rf(rtwdev, path, RR_RXA2, RR_RXA2_ATT, 0x0);
		rtw89_write_rf(rtwdev, path, RR_TXIQK, RR_TXIQK_ATT2, 0x3);
		rtw89_write_rf(rtwdev, path, RR_LUTDBG, RR_LUTDBG_TIA, 0x1);
		rtw89_write_rf(rtwdev, path, RR_TIA, RR_TIA_N6, 0x1);

		if (dpk->bp[path][kidx].bw == RTW89_CHANNEL_WIDTH_160)
			rtw89_write_rf(rtwdev, path, RR_RXBB2, RR_RXBB2_EBW, 0x0);
	}
}

static void _dpk_tpg_sel(struct rtw89_dev *rtwdev, enum rtw89_rf_path path, u8 kidx)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;

	if (dpk->bp[path][kidx].bw == RTW89_CHANNEL_WIDTH_160) {
		rtw89_phy_write32_mask(rtwdev, R_TPG_MOD, B_TPG_MOD_F, 0x3);
		rtw89_phy_write32_mask(rtwdev, R_TPG_SEL, MASKDWORD, 0x0180ff30);
	} else if (dpk->bp[path][kidx].bw == RTW89_CHANNEL_WIDTH_80) {
		rtw89_phy_write32_mask(rtwdev, R_TPG_MOD, B_TPG_MOD_F, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_TPG_SEL, MASKDWORD, 0xffe0fa00);
	} else if (dpk->bp[path][kidx].bw == RTW89_CHANNEL_WIDTH_40) {
		rtw89_phy_write32_mask(rtwdev, R_TPG_MOD, B_TPG_MOD_F, 0x2);
		rtw89_phy_write32_mask(rtwdev, R_TPG_SEL, MASKDWORD, 0xff4009e0);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_TPG_MOD, B_TPG_MOD_F, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_TPG_SEL, MASKDWORD, 0xf9f007d0);
	}
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] TPG_Select for %s\n",
		    dpk->bp[path][kidx].bw == RTW89_CHANNEL_WIDTH_160 ? "160M" :
		    dpk->bp[path][kidx].bw == RTW89_CHANNEL_WIDTH_80 ? "80M" :
		    dpk->bp[path][kidx].bw == RTW89_CHANNEL_WIDTH_40 ? "40M" : "20M");
}

static bool _dpk_sync_check(struct rtw89_dev *rtwdev, enum rtw89_rf_path path, u8 kidx)
{
#define DPK_SYNC_TH_DC_I 200
#define DPK_SYNC_TH_DC_Q 200
#define DPK_SYNC_TH_CORR 170
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;
	u16 dc_i, dc_q;
	u8 corr_val, corr_idx, rxbb;
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

	if (dc_i > DPK_SYNC_TH_DC_I || dc_q > DPK_SYNC_TH_DC_Q ||
	    corr_val < DPK_SYNC_TH_CORR)
		return true;
	else
		return false;
}

static u16 _dpk_dgain_read(struct rtw89_dev *rtwdev)
{
	u16 dgain = 0x0;

	rtw89_phy_write32_clr(rtwdev, R_KIP_RPT1, B_KIP_RPT1_SEL);

	dgain = rtw89_phy_read32_mask(rtwdev, R_RPT_COM, B_PRT_COM_DCI);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] DGain = 0x%x (%d)\n", dgain, dgain);

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

static void _dpk_kset_query(struct rtw89_dev *rtwdev, enum rtw89_rf_path path)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;

	rtw89_phy_write32_mask(rtwdev, R_KIP_RPT + (path << 8), B_KIP_RPT_SEL, 0x10);
	dpk->cur_k_set =
		rtw89_phy_read32_mask(rtwdev, R_RPT_PER + (path << 8), 0xE0000000) - 1;
}

static void _dpk_kip_set_txagc(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			       enum rtw89_rf_path path, u8 dbm, bool set_from_bb)
{
	if (set_from_bb) {
		dbm = clamp_t(u8, dbm, 7, 24);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] set S%d txagc to %ddBm\n", path, dbm);
		rtw89_phy_write32_mask(rtwdev, R_TXPWRB + (path << 13), B_TXPWRB_VAL, dbm << 2);
	}
	_dpk_one_shot(rtwdev, phy, path, D_TXAGC);
	_dpk_kset_query(rtwdev, path);
}

static u8 _dpk_gainloss(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			enum rtw89_rf_path path, u8 kidx)
{
	_dpk_one_shot(rtwdev, phy, path, D_GAIN_LOSS);
	_dpk_kip_set_txagc(rtwdev, phy, path, 0xff, false);

	rtw89_phy_write32_mask(rtwdev, R_DPK_GL + (path << 8), B_DPK_GL_A1, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_DPK_GL + (path << 8), B_DPK_GL_A0, 0x0);

	return _dpk_gainloss_read(rtwdev);
}

static bool _dpk_pas_read(struct rtw89_dev *rtwdev, bool is_check)
{
	u32 val1_i = 0, val1_q = 0, val2_i = 0, val2_q = 0;
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
			rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] PAS_Read[%02d]= 0x%08x\n", i,
				    rtw89_phy_read32_mask(rtwdev, R_RPT_COM, MASKDWORD));
		}
	}

	if (val1_i * val1_i + val1_q * val1_q >= (val2_i * val2_i + val2_q * val2_q) * 8 / 5)
		return true;
	else
		return false;
}

static bool _dpk_kip_set_rxagc(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			       enum rtw89_rf_path path, u8 kidx)
{
	_dpk_one_shot(rtwdev, phy, path, D_RXAGC);

	return _dpk_sync_check(rtwdev, path, kidx);
}

static void _dpk_read_rxsram(struct rtw89_dev *rtwdev)
{
	u32 addr;

	rtw89_rfk_parser(rtwdev, &rtw8852c_read_rxsram_pre_defs_tbl);

	for (addr = 0; addr < 0x200; addr++) {
		rtw89_phy_write32_mask(rtwdev, R_SRAM_IQRX, MASKDWORD, 0x00010000 | addr);

		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] RXSRAM[%03d] = 0x%07x\n", addr,
			    rtw89_phy_read32_mask(rtwdev, R_RPT_COM, MASKDWORD));
	}

	rtw89_rfk_parser(rtwdev, &rtw8852c_read_rxsram_post_defs_tbl);
}

static void _dpk_bypass_rxiqc(struct rtw89_dev *rtwdev, enum rtw89_rf_path path)
{
	rtw89_phy_write32_mask(rtwdev, R_DPD_V1 + (path << 8), B_DPD_LBK, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_RXIQC + (path << 8), MASKDWORD, 0x40000002);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] Bypass RXIQC\n");
}

static u8 _dpk_agc(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
		   enum rtw89_rf_path path, u8 kidx, u8 init_xdbm, u8 loss_only)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;
	u8 step = DPK_AGC_STEP_SYNC_DGAIN;
	u8 tmp_dbm = init_xdbm, tmp_gl_idx = 0;
	u8 tmp_rxbb;
	u8 goout = 0, agc_cnt = 0;
	u16 dgain = 0;
	bool is_fail = false;
	int limit = 200;

	do {
		switch (step) {
		case DPK_AGC_STEP_SYNC_DGAIN:
			is_fail = _dpk_kip_set_rxagc(rtwdev, phy, path, kidx);

			if (RTW8852C_DPK_RXSRAM_DBG)
				_dpk_read_rxsram(rtwdev);

			if (is_fail) {
				goout = 1;
				break;
			}

			dgain = _dpk_dgain_read(rtwdev);

			if (dgain > 0x5fc || dgain < 0x556) {
				_dpk_one_shot(rtwdev, phy, path, D_SYNC);
				dgain = _dpk_dgain_read(rtwdev);
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

			if ((tmp_gl_idx == 0 && _dpk_pas_read(rtwdev, true)) ||
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
				rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] Txagc@lower bound!!\n");
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
				rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] Txagc@upper bound!!\n");
			} else {
				tmp_dbm = min_t(u8, tmp_dbm + 2, 24);
				_dpk_kip_set_txagc(rtwdev, phy, path, tmp_dbm, true);
			}
			step = DPK_AGC_STEP_SYNC_DGAIN;
			agc_cnt++;
			break;

		case DPK_AGC_STEP_SET_TX_GAIN:
			_dpk_kip_control_rfc(rtwdev, path, false);
			tmp_rxbb = rtw89_read_rf(rtwdev, path, RR_MOD, RR_MOD_M_RXBB);
			if (tmp_rxbb + tmp_gl_idx > 0x1f)
				tmp_rxbb = 0x1f;
			else
				tmp_rxbb = tmp_rxbb + tmp_gl_idx;

			rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_M_RXBB, tmp_rxbb);
			rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] Adjust RXBB (%+d) = 0x%x\n",
				    tmp_gl_idx, tmp_rxbb);
			_dpk_kip_control_rfc(rtwdev, path, true);
			goout = 1;
			break;
		default:
			goout = 1;
			break;
		}
	} while (!goout && agc_cnt < 6 && --limit > 0);

	if (limit <= 0)
		rtw89_warn(rtwdev, "[DPK] exceed loop limit\n");

	return is_fail;
}

static void _dpk_set_mdpd_para(struct rtw89_dev *rtwdev, u8 order)
{
	static const struct rtw89_rfk_tbl *order_tbls[] = {
		&rtw8852c_dpk_mdpd_order0_defs_tbl,
		&rtw8852c_dpk_mdpd_order1_defs_tbl,
		&rtw8852c_dpk_mdpd_order2_defs_tbl,
		&rtw8852c_dpk_mdpd_order3_defs_tbl,
	};

	if (order >= ARRAY_SIZE(order_tbls)) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] Wrong MDPD order!!(0x%x)\n", order);
		return;
	}

	rtw89_rfk_parser(rtwdev, order_tbls[order]);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] Set %s for IDL\n",
		    order == 0x0 ? "(5,3,1)" :
		    order == 0x1 ? "(5,3,0)" :
		    order == 0x2 ? "(5,0,0)" : "(7,3,1)");
}

static void _dpk_idl_mpa(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			 enum rtw89_rf_path path, u8 kidx)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;
	u8 cnt;
	u8 ov_flag;
	u32 dpk_sync;

	rtw89_phy_write32_mask(rtwdev, R_LDL_NORM, B_LDL_NORM_MA, 0x1);

	if (rtw89_phy_read32_mask(rtwdev, R_DPK_MPA, B_DPK_MPA_T2) == 0x1)
		_dpk_set_mdpd_para(rtwdev, 0x2);
	else if (rtw89_phy_read32_mask(rtwdev, R_DPK_MPA, B_DPK_MPA_T1) == 0x1)
		_dpk_set_mdpd_para(rtwdev, 0x1);
	else if (rtw89_phy_read32_mask(rtwdev, R_DPK_MPA, B_DPK_MPA_T0) == 0x1)
		_dpk_set_mdpd_para(rtwdev, 0x0);
	else if (dpk->bp[path][kidx].bw == RTW89_CHANNEL_WIDTH_5 ||
		 dpk->bp[path][kidx].bw == RTW89_CHANNEL_WIDTH_10 ||
		 dpk->bp[path][kidx].bw == RTW89_CHANNEL_WIDTH_20)
		_dpk_set_mdpd_para(rtwdev, 0x2);
	else if (dpk->bp[path][kidx].bw == RTW89_CHANNEL_WIDTH_40 ||
		 dpk->bp[path][kidx].bw == RTW89_CHANNEL_WIDTH_80)
		_dpk_set_mdpd_para(rtwdev, 0x1);
	else
		_dpk_set_mdpd_para(rtwdev, 0x0);

	rtw89_phy_write32_mask(rtwdev, R_DPK_IDL, B_DPK_IDL, 0x0);
	fsleep(1000);

	_dpk_one_shot(rtwdev, phy, path, D_MDPK_IDL);
	rtw89_phy_write32_mask(rtwdev, R_KIP_RPT1, B_KIP_RPT1_SEL, 0x0);
	dpk_sync = rtw89_phy_read32_mask(rtwdev, R_RPT_COM, MASKDWORD);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] dpk_sync = 0x%x\n", dpk_sync);

	rtw89_phy_write32_mask(rtwdev, R_KIP_RPT1, B_KIP_RPT1_SEL, 0xf);
	ov_flag = rtw89_phy_read32_mask(rtwdev, R_RPT_COM, B_PRT_COM_SYNERR);
	for (cnt = 0; cnt < 5 && ov_flag == 0x1; cnt++) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] ReK due to MDPK ov!!!\n");
		_dpk_one_shot(rtwdev, phy, path, D_MDPK_IDL);
		rtw89_phy_write32_mask(rtwdev, R_KIP_RPT1, B_KIP_RPT1_SEL, 0xf);
		ov_flag = rtw89_phy_read32_mask(rtwdev, R_RPT_COM, B_PRT_COM_SYNERR);
	}

	if (ov_flag) {
		_dpk_set_mdpd_para(rtwdev, 0x2);
		_dpk_one_shot(rtwdev, phy, path, D_MDPK_IDL);
	}
}

static bool _dpk_reload_check(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			      enum rtw89_rf_path path)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;
	bool is_reload = false;
	u8 idx, cur_band, cur_ch;

	cur_band = chan->band_type;
	cur_ch = chan->channel;

	for (idx = 0; idx < RTW89_DPK_BKUP_NUM; idx++) {
		if (cur_band != dpk->bp[path][idx].band ||
		    cur_ch != dpk->bp[path][idx].ch)
			continue;

		rtw89_phy_write32_mask(rtwdev, R_COEF_SEL + (path << 8),
				       B_COEF_SEL_MDPD, idx);
		dpk->cur_idx[path] = idx;
		is_reload = true;
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[DPK] reload S%d[%d] success\n", path, idx);
	}

	return is_reload;
}

static void _dpk_kip_pwr_clk_onoff(struct rtw89_dev *rtwdev, bool turn_on)
{
	rtw89_rfk_parser(rtwdev, turn_on ? &rtw8852c_dpk_kip_pwr_clk_on_defs_tbl :
					   &rtw8852c_dpk_kip_pwr_clk_off_defs_tbl);
}

static void _dpk_kip_preset_8852c(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
				  enum rtw89_rf_path path, u8 kidx)
{
	rtw89_phy_write32_mask(rtwdev, R_KIP_MOD, B_KIP_MOD,
			       rtw89_read_rf(rtwdev, path, RR_MOD, RFREG_MASK));

	if (rtwdev->hal.cv == CHIP_CAV)
		rtw89_phy_write32_mask(rtwdev,
				       R_DPD_CH0A + (path << 8) + (kidx << 2),
				       B_DPD_SEL, 0x01);
	else
		rtw89_phy_write32_mask(rtwdev,
				       R_DPD_CH0A + (path << 8) + (kidx << 2),
				       B_DPD_SEL, 0x0c);

	_dpk_kip_control_rfc(rtwdev, path, true);
	rtw89_phy_write32_mask(rtwdev, R_COEF_SEL + (path << 8), B_COEF_SEL_MDPD, kidx);

	_dpk_one_shot(rtwdev, phy, path, D_KIP_PRESET);
}

static void _dpk_para_query(struct rtw89_dev *rtwdev, enum rtw89_rf_path path, u8 kidx)
{
#define _DPK_PARA_TXAGC GENMASK(15, 10)
#define _DPK_PARA_THER GENMASK(31, 26)
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;
	u32 para;

	para = rtw89_phy_read32_mask(rtwdev, dpk_par_regs[kidx][dpk->cur_k_set] + (path << 8),
				     MASKDWORD);

	dpk->bp[path][kidx].txagc_dpk = FIELD_GET(_DPK_PARA_TXAGC, para);
	dpk->bp[path][kidx].ther_dpk = FIELD_GET(_DPK_PARA_THER, para);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] thermal/ txagc_RF (K%d) = 0x%x/ 0x%x\n",
		    dpk->cur_k_set, dpk->bp[path][kidx].ther_dpk, dpk->bp[path][kidx].txagc_dpk);
}

static void _dpk_gain_normalize_8852c(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
				      enum rtw89_rf_path path, u8 kidx, bool is_execute)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;

	if (is_execute) {
		rtw89_phy_write32_mask(rtwdev, R_DPK_GN + (path << 8), B_DPK_GN_AG, 0x200);
		rtw89_phy_write32_mask(rtwdev, R_DPK_GN + (path << 8), B_DPK_GN_EN, 0x3);

		_dpk_one_shot(rtwdev, phy, path, D_GAIN_NORM);
	} else {
		rtw89_phy_write32_mask(rtwdev, dpk_par_regs[kidx][dpk->cur_k_set] + (path << 8),
				       0x0000007F, 0x5b);
	}
	dpk->bp[path][kidx].gs =
		rtw89_phy_read32_mask(rtwdev, dpk_par_regs[kidx][dpk->cur_k_set] + (path << 8),
				      0x0000007F);
}

static u8 _dpk_order_convert(struct rtw89_dev *rtwdev)
{
	u32 val32 = rtw89_phy_read32_mask(rtwdev, R_LDL_NORM, B_LDL_NORM_OP);
	u8 val;

	switch (val32) {
	case 0:
		val = 0x6;
		break;
	case 1:
		val = 0x2;
		break;
	case 2:
		val = 0x0;
		break;
	case 3:
		val = 0x7;
		break;
	default:
		val = 0xff;
		break;
	}

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] convert MDPD order to 0x%x\n", val);

	return val;
}

static void _dpk_on(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
		    enum rtw89_rf_path path, u8 kidx)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;

	rtw89_phy_write32_mask(rtwdev, R_LOAD_COEF + (path << 8), B_LOAD_COEF_MDPD, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_LOAD_COEF + (path << 8), B_LOAD_COEF_MDPD, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_DPD_CH0A + (path << 8) + (kidx << 2),
			       B_DPD_ORDER, _dpk_order_convert(rtwdev));

	dpk->bp[path][kidx].mdpd_en = BIT(dpk->cur_k_set);
	dpk->bp[path][kidx].path_ok = true;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] S%d[%d] path_ok = 0x%x\n",
		    path, kidx, dpk->bp[path][kidx].mdpd_en);

	rtw89_phy_write32_mask(rtwdev, R_DPD_CH0A + (path << 8) + (kidx << 2),
			       B_DPD_MEN, dpk->bp[path][kidx].mdpd_en);

	_dpk_gain_normalize_8852c(rtwdev, phy, path, kidx, false);
}

static bool _dpk_main(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
		      enum rtw89_rf_path path, u8 gain)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;
	u8 kidx = dpk->cur_idx[path];
	u8 init_xdbm = 15;
	bool is_fail;

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DPK] ========= S%d[%d] DPK Start =========\n", path, kidx);
	_dpk_kip_control_rfc(rtwdev, path, false);
	_rf_direct_cntrl(rtwdev, path, false);
	rtw89_write_rf(rtwdev, path, RR_BBDC, RFREG_MASK, 0x03ffd);
	_dpk_rf_setting(rtwdev, gain, path, kidx);
	_set_rx_dck(rtwdev, phy, path, false);
	_dpk_kip_pwr_clk_onoff(rtwdev, true);
	_dpk_kip_preset_8852c(rtwdev, phy, path, kidx);
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

static void _dpk_init(struct rtw89_dev *rtwdev, u8 path)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;
	u8 kidx = dpk->cur_idx[path];

	dpk->bp[path][kidx].path_ok = false;
}

static void _dpk_drf_direct_cntrl(struct rtw89_dev *rtwdev, u8 path, bool is_bybb)
{
	if (is_bybb)
		rtw89_write_rf(rtwdev,  path, RR_BBDC, RR_BBDC_SEL, 0x1);
	else
		rtw89_write_rf(rtwdev,  path, RR_BBDC, RR_BBDC_SEL, 0x0);
}

static void _dpk_cal_select(struct rtw89_dev *rtwdev, bool force,
			    enum rtw89_phy_idx phy, u8 kpath)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;
	static const u32 kip_reg[] = {0x813c, 0x8124, 0x8120, 0xc0d4, 0xc0d8};
	u32 backup_rf_val[RTW8852C_DPK_RF_PATH][BACKUP_RF_REGS_NR];
	u32 kip_bkup[RTW8852C_DPK_RF_PATH][RTW8852C_DPK_KIP_REG_NUM] = {};
	u8 path;
	bool is_fail = true, reloaded[RTW8852C_DPK_RF_PATH] = {false};

	if (dpk->is_dpk_reload_en) {
		for (path = 0; path < RTW8852C_DPK_RF_PATH; path++) {
			if (!(kpath & BIT(path)))
				continue;

			reloaded[path] = _dpk_reload_check(rtwdev, phy, path);
			if (!reloaded[path] && dpk->bp[path][0].ch != 0)
				dpk->cur_idx[path] = !dpk->cur_idx[path];
			else
				_dpk_onoff(rtwdev, path, false);
		}
	} else {
		for (path = 0; path < RTW8852C_DPK_RF_PATH; path++)
			dpk->cur_idx[path] = 0;
	}

	for (path = 0; path < RTW8852C_DPK_RF_PATH; path++) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[DPK] ========= S%d[%d] DPK Init =========\n",
			    path, dpk->cur_idx[path]);
		_dpk_bkup_kip(rtwdev, kip_reg, kip_bkup, path);
		_rfk_backup_rf_reg(rtwdev, backup_rf_val[path], path);
		_dpk_information(rtwdev, phy, path);
		_dpk_init(rtwdev, path);
		if (rtwdev->is_tssi_mode[path])
			_dpk_tssi_pause(rtwdev, path, true);
	}

	for (path = 0; path < RTW8852C_DPK_RF_PATH; path++) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[DPK] ========= S%d[%d] DPK Start =========\n",
			    path, dpk->cur_idx[path]);
		rtw8852c_disable_rxagc(rtwdev, path, 0x0);
		_dpk_drf_direct_cntrl(rtwdev, path, false);
		_dpk_bb_afe_setting(rtwdev, phy, path, kpath);
		is_fail = _dpk_main(rtwdev, phy, path, 1);
		_dpk_onoff(rtwdev, path, is_fail);
	}

	for (path = 0; path < RTW8852C_DPK_RF_PATH; path++) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[DPK] ========= S%d[%d] DPK Restore =========\n",
			    path, dpk->cur_idx[path]);
		_dpk_kip_restore(rtwdev, phy, path);
		_dpk_reload_kip(rtwdev, kip_reg, kip_bkup, path);
		_rfk_restore_rf_reg(rtwdev, backup_rf_val[path], path);
		_dpk_bb_afe_restore(rtwdev, path);
		rtw8852c_disable_rxagc(rtwdev, path, 0x1);
		if (rtwdev->is_tssi_mode[path])
			_dpk_tssi_pause(rtwdev, path, false);
	}

	_dpk_kip_pwr_clk_onoff(rtwdev, false);
}

static bool _dpk_bypass_check(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy)
{
	struct rtw89_fem_info *fem = &rtwdev->fem;
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	u8 band = chan->band_type;

	if (rtwdev->hal.cv == CHIP_CAV && band != RTW89_BAND_2G) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] Skip DPK due to CAV & not 2G!!\n");
		return true;
	} else if (fem->epa_2g && band == RTW89_BAND_2G) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] Skip DPK due to 2G_ext_PA exist!!\n");
		return true;
	} else if (fem->epa_5g && band == RTW89_BAND_5G) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] Skip DPK due to 5G_ext_PA exist!!\n");
		return true;
	} else if (fem->epa_6g && band == RTW89_BAND_6G) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] Skip DPK due to 6G_ext_PA exist!!\n");
		return true;
	}

	return false;
}

static void _dpk_force_bypass(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy)
{
	u8 path, kpath;

	kpath = _kpath(rtwdev, phy);

	for (path = 0; path < RTW8852C_DPK_RF_PATH; path++) {
		if (kpath & BIT(path))
			_dpk_onoff(rtwdev, path, true);
	}
}

static void _dpk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy, bool force)
{
	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DPK] ****** DPK Start (Ver: 0x%x, Cv: %d, RF_para: %d) ******\n",
		    RTW8852C_DPK_VER, rtwdev->hal.cv,
		    RTW8852C_RF_REL_VERSION);

	if (_dpk_bypass_check(rtwdev, phy))
		_dpk_force_bypass(rtwdev, phy);
	else
		_dpk_cal_select(rtwdev, force, phy, _kpath(rtwdev, phy));

	if (rtw89_read_rf(rtwdev, RF_PATH_A, RR_DCKC, RR_DCKC_CHK) == 0x1)
		rtw8852c_rx_dck(rtwdev, phy, false);
}

static void _dpk_onoff(struct rtw89_dev *rtwdev,
		       enum rtw89_rf_path path, bool off)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;
	u8 val, kidx = dpk->cur_idx[path];

	val = dpk->is_dpk_enable && !off && dpk->bp[path][kidx].path_ok ?
	      dpk->bp[path][kidx].mdpd_en : 0;

	rtw89_phy_write32_mask(rtwdev, R_DPD_CH0A + (path << 8) + (kidx << 2),
			       B_DPD_MEN, val);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] S%d[%d] DPK %s !!!\n", path,
		    kidx, dpk->is_dpk_enable && !off ? "enable" : "disable");
}

static void _dpk_track(struct rtw89_dev *rtwdev)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;
	u8 path, kidx;
	u8 txagc_rf = 0;
	s8 txagc_bb = 0, txagc_bb_tp = 0, txagc_ofst = 0;
	u8 cur_ther;
	s8 delta_ther = 0;
	s16 pwsf_tssi_ofst;

	for (path = 0; path < RTW8852C_DPK_RF_PATH; path++) {
		kidx = dpk->cur_idx[path];
		rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK,
			    "[DPK_TRK] ================[S%d[%d] (CH %d)]================\n",
			    path, kidx, dpk->bp[path][kidx].ch);

		txagc_rf =
			rtw89_phy_read32_mask(rtwdev, R_TXAGC_BB + (path << 13), 0x0000003f);
		txagc_bb =
			rtw89_phy_read32_mask(rtwdev, R_TXAGC_BB + (path << 13), MASKBYTE2);
		txagc_bb_tp =
			rtw89_phy_read32_mask(rtwdev, R_TXAGC_BTP + (path << 13), B_TXAGC_BTP);

		/* report from KIP */
		rtw89_phy_write32_mask(rtwdev, R_KIP_RPT + (path << 8), B_KIP_RPT_SEL, 0xf);
		cur_ther =
			rtw89_phy_read32_mask(rtwdev, R_RPT_PER + (path << 8), B_RPT_PER_TH);
		txagc_ofst =
			rtw89_phy_read32_mask(rtwdev, R_RPT_PER + (path << 8), B_RPT_PER_OF);
		pwsf_tssi_ofst =
			rtw89_phy_read32_mask(rtwdev, R_RPT_PER + (path << 8), B_RPT_PER_TSSI);
		pwsf_tssi_ofst = sign_extend32(pwsf_tssi_ofst, 12);

		cur_ther = ewma_thermal_read(&rtwdev->phystat.avg_thermal[path]);

		rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK,
			    "[DPK_TRK] thermal now = %d\n", cur_ther);

		if (dpk->bp[path][kidx].ch != 0 && cur_ther != 0)
			delta_ther = dpk->bp[path][kidx].ther_dpk - cur_ther;

		delta_ther = delta_ther * 1 / 2;

		rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK,
			    "[DPK_TRK] extra delta_ther = %d (0x%x / 0x%x@k)\n",
			    delta_ther, cur_ther, dpk->bp[path][kidx].ther_dpk);
		rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK,
			    "[DPK_TRK] delta_txagc = %d (0x%x / 0x%x@k)\n",
			    txagc_rf - dpk->bp[path][kidx].txagc_dpk, txagc_rf,
			    dpk->bp[path][kidx].txagc_dpk);
		rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK,
			    "[DPK_TRK] txagc_offset / pwsf_tssi_ofst = 0x%x / %+d\n",
			    txagc_ofst, pwsf_tssi_ofst);
		rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK,
			    "[DPK_TRK] txagc_bb_tp / txagc_bb = 0x%x / 0x%x\n",
			    txagc_bb_tp, txagc_bb);

		if (rtw89_phy_read32_mask(rtwdev, R_DPK_WR, B_DPK_WR_ST) == 0x0 &&
		    txagc_rf != 0 && rtwdev->hal.cv == CHIP_CAV) {
			rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK,
				    "[DPK_TRK] New pwsf = 0x%x\n", 0x78 - delta_ther);

			rtw89_phy_write32_mask(rtwdev, R_DPD_BND + (path << 8) + (kidx << 2),
					       0x07FC0000, 0x78 - delta_ther);
		}
	}
}

static void _tssi_set_sys(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			  enum rtw89_rf_path path)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	enum rtw89_band band = chan->band_type;

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
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	enum rtw89_band band = chan->band_type;

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
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	u8 ch = chan->channel;
	u8 subband = chan->subband_type;
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
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	enum rtw89_band band = chan->band_type;

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
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	enum rtw89_band band = chan->band_type;
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
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	enum rtw89_band band = chan->band_type;
	u8 ch = chan->channel;
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
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	enum rtw89_band band = chan->band_type;
	u8 ch = chan->channel;
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
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	u8 ch = chan->channel;
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
			     const struct rtw89_chan *chan,
			     enum rtw89_phy_idx phy_idx)
{
	rtw8852c_ctrl_bw_ch(rtwdev, phy_idx, chan->channel,
			    chan->band_type,
			    chan->band_width);
}

void rtw8852c_mcc_get_ch_info(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	struct rtw89_rfk_mcc_info *rfk_mcc = &rtwdev->rfk_mcc;
	u8 idx = rfk_mcc->table_idx;
	int i;

	for (i = 0; i < RTW89_IQK_CHS_NR; i++) {
		if (rfk_mcc->ch[idx] == 0)
			break;
		if (++idx >= RTW89_IQK_CHS_NR)
			idx = 0;
	}

	rfk_mcc->table_idx = idx;
	rfk_mcc->ch[idx] = chan->channel;
	rfk_mcc->band[idx] = chan->band_type;
}

void rtw8852c_rck(struct rtw89_dev *rtwdev)
{
	u8 path;

	for (path = 0; path < 2; path++)
		_rck(rtwdev, path);
}

void rtw8852c_dack(struct rtw89_dev *rtwdev)
{
	u8 phy_map = rtw89_btc_phymap(rtwdev, RTW89_PHY_0, 0);

	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_DACK, BTC_WRFK_START);
	_dac_cal(rtwdev, false);
	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_DACK, BTC_WRFK_STOP);
}

void rtw8852c_iqk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx)
{
	u32 tx_en;
	u8 phy_map = rtw89_btc_phymap(rtwdev, phy_idx, 0);

	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_IQK, BTC_WRFK_START);
	rtw89_chip_stop_sch_tx(rtwdev, phy_idx, &tx_en, RTW89_SCH_TX_SEL_ALL);
	_wait_rx_mode(rtwdev, _kpath(rtwdev, phy_idx));

	_iqk_init(rtwdev);
	_iqk(rtwdev, phy_idx, false);

	rtw89_chip_resume_sch_tx(rtwdev, phy_idx, tx_en);
	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_IQK, BTC_WRFK_STOP);
}

#define RXDCK_VER_8852C 0xe

void rtw8852c_rx_dck(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy, bool is_afe)
{
	struct rtw89_rx_dck_info *rx_dck = &rtwdev->rx_dck;
	u8 path, kpath;
	u32 rf_reg5;

	kpath = _kpath(rtwdev, phy);
	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[RX_DCK] ****** RXDCK Start (Ver: 0x%x, Cv: %d) ******\n",
		    RXDCK_VER_8852C, rtwdev->hal.cv);

	for (path = 0; path < 2; path++) {
		rf_reg5 = rtw89_read_rf(rtwdev, path, RR_RSV1, RFREG_MASK);
		if (!(kpath & BIT(path)))
			continue;

		if (rtwdev->is_tssi_mode[path])
			rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_TRK + (path << 13),
					       B_P0_TSSI_TRK_EN, 0x1);
		rtw89_write_rf(rtwdev, path, RR_RSV1, RR_RSV1_RST, 0x0);
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_MASK, RR_MOD_V_RX);
		_set_rx_dck(rtwdev, phy, path, is_afe);
		rx_dck->thermal[path] = ewma_thermal_read(&rtwdev->phystat.avg_thermal[path]);
		rtw89_write_rf(rtwdev, path, RR_RSV1, RFREG_MASK, rf_reg5);

		if (rtwdev->is_tssi_mode[path])
			rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_TRK + (path << 13),
					       B_P0_TSSI_TRK_EN, 0x0);
	}
}

#define RTW8852C_RX_DCK_TH 8

void rtw8852c_rx_dck_track(struct rtw89_dev *rtwdev)
{
	struct rtw89_rx_dck_info *rx_dck = &rtwdev->rx_dck;
	u8 cur_thermal;
	int delta;
	int path;

	for (path = 0; path < RF_PATH_NUM_8852C; path++) {
		cur_thermal =
			ewma_thermal_read(&rtwdev->phystat.avg_thermal[path]);
		delta = abs((int)cur_thermal - rx_dck->thermal[path]);

		rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK,
			    "[RX_DCK] path=%d current thermal=0x%x delta=0x%x\n",
			    path, cur_thermal, delta);

		if (delta >= RTW8852C_RX_DCK_TH) {
			rtw8852c_rx_dck(rtwdev, RTW89_PHY_0, false);
			return;
		}
	}
}

void rtw8852c_dpk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx)
{
	u32 tx_en;
	u8 phy_map = rtw89_btc_phymap(rtwdev, phy_idx, 0);

	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_DPK, BTC_WRFK_START);
	rtw89_chip_stop_sch_tx(rtwdev, phy_idx, &tx_en, RTW89_SCH_TX_SEL_ALL);
	_wait_rx_mode(rtwdev, _kpath(rtwdev, phy_idx));

	rtwdev->dpk.is_dpk_enable = true;
	rtwdev->dpk.is_dpk_reload_en = false;
	_dpk(rtwdev, phy_idx, false);

	rtw89_chip_resume_sch_tx(rtwdev, phy_idx, tx_en);
	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_DPK, BTC_WRFK_STOP);
}

void rtw8852c_dpk_track(struct rtw89_dev *rtwdev)
{
	_dpk_track(rtwdev);
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
