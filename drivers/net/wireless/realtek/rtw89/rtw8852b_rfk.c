// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2022  Realtek Corporation
 */

#include "coex.h"
#include "debug.h"
#include "mac.h"
#include "phy.h"
#include "reg.h"
#include "rtw8852b.h"
#include "rtw8852b_rfk.h"
#include "rtw8852b_rfk_table.h"
#include "rtw8852b_table.h"

#define RTW8852B_RXDCK_VER 0x1
#define RTW8852B_IQK_VER 0x2a
#define RTW8852B_IQK_SS 2
#define RTW8852B_RXK_GROUP_NR 4
#define RTW8852B_TSSI_PATH_NR 2

#define _TSSI_DE_MASK GENMASK(21, 12)
#define ADDC_T_AVG 100

enum rtw8852b_iqk_type {
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

static const u32 _tssi_trigger[RTW8852B_TSSI_PATH_NR] = {0x5820, 0x7820};
static const u32 _tssi_cw_rpt_addr[RTW8852B_TSSI_PATH_NR] = {0x1c18, 0x3c18};
static const u32 _tssi_cw_default_addr[RTW8852B_TSSI_PATH_NR][4] = {
	{0x5634, 0x5630, 0x5630, 0x5630},
	{0x7634, 0x7630, 0x7630, 0x7630} };
static const u32 _tssi_cw_default_mask[4] = {
	0x000003ff, 0x3ff00000, 0x000ffc00, 0x000003ff};
static const u32 _tssi_de_cck_long[RF_PATH_NUM_8852B] = {0x5858, 0x7858};
static const u32 _tssi_de_cck_short[RF_PATH_NUM_8852B] = {0x5860, 0x7860};
static const u32 _tssi_de_mcs_20m[RF_PATH_NUM_8852B] = {0x5838, 0x7838};
static const u32 _tssi_de_mcs_40m[RF_PATH_NUM_8852B] = {0x5840, 0x7840};
static const u32 _tssi_de_mcs_80m[RF_PATH_NUM_8852B] = {0x5848, 0x7848};
static const u32 _tssi_de_mcs_80m_80m[RF_PATH_NUM_8852B] = {0x5850, 0x7850};
static const u32 _tssi_de_mcs_5m[RF_PATH_NUM_8852B] = {0x5828, 0x7828};
static const u32 _tssi_de_mcs_10m[RF_PATH_NUM_8852B] = {0x5830, 0x7830};
static const u32 _a_idxrxgain[RTW8852B_RXK_GROUP_NR] = {0x190, 0x198, 0x350, 0x352};
static const u32 _a_idxattc2[RTW8852B_RXK_GROUP_NR] = {0x0f, 0x0f, 0x3f, 0x7f};
static const u32 _a_idxattc1[RTW8852B_RXK_GROUP_NR] = {0x3, 0x1, 0x0, 0x0};
static const u32 _g_idxrxgain[RTW8852B_RXK_GROUP_NR] = {0x212, 0x21c, 0x350, 0x360};
static const u32 _g_idxattc2[RTW8852B_RXK_GROUP_NR] = {0x00, 0x00, 0x28, 0x5f};
static const u32 _g_idxattc1[RTW8852B_RXK_GROUP_NR] = {0x3, 0x3, 0x2, 0x1};
static const u32 _a_power_range[RTW8852B_RXK_GROUP_NR] = {0x0, 0x0, 0x0, 0x0};
static const u32 _a_track_range[RTW8852B_RXK_GROUP_NR] = {0x3, 0x3, 0x6, 0x6};
static const u32 _a_gain_bb[RTW8852B_RXK_GROUP_NR] = {0x08, 0x0e, 0x06, 0x0e};
static const u32 _a_itqt[RTW8852B_RXK_GROUP_NR] = {0x12, 0x12, 0x12, 0x1b};
static const u32 _g_power_range[RTW8852B_RXK_GROUP_NR] = {0x0, 0x0, 0x0, 0x0};
static const u32 _g_track_range[RTW8852B_RXK_GROUP_NR] = {0x4, 0x4, 0x6, 0x6};
static const u32 _g_gain_bb[RTW8852B_RXK_GROUP_NR] = {0x08, 0x0e, 0x06, 0x0e};
static const u32 _g_itqt[RTW8852B_RXK_GROUP_NR] = {0x09, 0x12, 0x1b, 0x24};

static const u32 rtw8852b_backup_bb_regs[] = {0x2344, 0x5800, 0x7800};
static const u32 rtw8852b_backup_rf_regs[] = {
	0xde, 0xdf, 0x8b, 0x90, 0x97, 0x85, 0x1e, 0x0, 0x2, 0x5, 0x10005
};

#define BACKUP_BB_REGS_NR ARRAY_SIZE(rtw8852b_backup_bb_regs)
#define BACKUP_RF_REGS_NR ARRAY_SIZE(rtw8852b_backup_rf_regs)

static const struct rtw89_reg3_def rtw8852b_set_nondbcc_path01[] = {
	{0x20fc, 0xffff0000, 0x0303},
	{0x5864, 0x18000000, 0x3},
	{0x7864, 0x18000000, 0x3},
	{0x12b8, 0x40000000, 0x1},
	{0x32b8, 0x40000000, 0x1},
	{0x030c, 0xff000000, 0x13},
	{0x032c, 0xffff0000, 0x0041},
	{0x12b8, 0x10000000, 0x1},
	{0x58c8, 0x01000000, 0x1},
	{0x78c8, 0x01000000, 0x1},
	{0x5864, 0xc0000000, 0x3},
	{0x7864, 0xc0000000, 0x3},
	{0x2008, 0x01ffffff, 0x1ffffff},
	{0x0c1c, 0x00000004, 0x1},
	{0x0700, 0x08000000, 0x1},
	{0x0c70, 0x000003ff, 0x3ff},
	{0x0c60, 0x00000003, 0x3},
	{0x0c6c, 0x00000001, 0x1},
	{0x58ac, 0x08000000, 0x1},
	{0x78ac, 0x08000000, 0x1},
	{0x0c3c, 0x00000200, 0x1},
	{0x2344, 0x80000000, 0x1},
	{0x4490, 0x80000000, 0x1},
	{0x12a0, 0x00007000, 0x7},
	{0x12a0, 0x00008000, 0x1},
	{0x12a0, 0x00070000, 0x3},
	{0x12a0, 0x00080000, 0x1},
	{0x32a0, 0x00070000, 0x3},
	{0x32a0, 0x00080000, 0x1},
	{0x0700, 0x01000000, 0x1},
	{0x0700, 0x06000000, 0x2},
	{0x20fc, 0xffff0000, 0x3333},
};

static const struct rtw89_reg3_def rtw8852b_restore_nondbcc_path01[] = {
	{0x20fc, 0xffff0000, 0x0303},
	{0x12b8, 0x40000000, 0x0},
	{0x32b8, 0x40000000, 0x0},
	{0x5864, 0xc0000000, 0x0},
	{0x7864, 0xc0000000, 0x0},
	{0x2008, 0x01ffffff, 0x0000000},
	{0x0c1c, 0x00000004, 0x0},
	{0x0700, 0x08000000, 0x0},
	{0x0c70, 0x0000001f, 0x03},
	{0x0c70, 0x000003e0, 0x03},
	{0x12a0, 0x000ff000, 0x00},
	{0x32a0, 0x000ff000, 0x00},
	{0x0700, 0x07000000, 0x0},
	{0x20fc, 0xffff0000, 0x0000},
	{0x58c8, 0x01000000, 0x0},
	{0x78c8, 0x01000000, 0x0},
	{0x0c3c, 0x00000200, 0x0},
	{0x2344, 0x80000000, 0x0},
};

static void _rfk_backup_bb_reg(struct rtw89_dev *rtwdev, u32 backup_bb_reg_val[])
{
	u32 i;

	for (i = 0; i < BACKUP_BB_REGS_NR; i++) {
		backup_bb_reg_val[i] =
			rtw89_phy_read32_mask(rtwdev, rtw8852b_backup_bb_regs[i],
					      MASKDWORD);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[RFK]backup bb reg : %x, value =%x\n",
			    rtw8852b_backup_bb_regs[i], backup_bb_reg_val[i]);
	}
}

static void _rfk_backup_rf_reg(struct rtw89_dev *rtwdev, u32 backup_rf_reg_val[],
			       u8 rf_path)
{
	u32 i;

	for (i = 0; i < BACKUP_RF_REGS_NR; i++) {
		backup_rf_reg_val[i] =
			rtw89_read_rf(rtwdev, rf_path,
				      rtw8852b_backup_rf_regs[i], RFREG_MASK);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[RFK]backup rf S%d reg : %x, value =%x\n", rf_path,
			    rtw8852b_backup_rf_regs[i], backup_rf_reg_val[i]);
	}
}

static void _rfk_restore_bb_reg(struct rtw89_dev *rtwdev,
				const u32 backup_bb_reg_val[])
{
	u32 i;

	for (i = 0; i < BACKUP_BB_REGS_NR; i++) {
		rtw89_phy_write32_mask(rtwdev, rtw8852b_backup_bb_regs[i],
				       MASKDWORD, backup_bb_reg_val[i]);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[RFK]restore bb reg : %x, value =%x\n",
			    rtw8852b_backup_bb_regs[i], backup_bb_reg_val[i]);
	}
}

static void _rfk_restore_rf_reg(struct rtw89_dev *rtwdev,
				const u32 backup_rf_reg_val[], u8 rf_path)
{
	u32 i;

	for (i = 0; i < BACKUP_RF_REGS_NR; i++) {
		rtw89_write_rf(rtwdev, rf_path, rtw8852b_backup_rf_regs[i],
			       RFREG_MASK, backup_rf_reg_val[i]);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[RFK]restore rf S%d reg: %x, value =%x\n", rf_path,
			    rtw8852b_backup_rf_regs[i], backup_rf_reg_val[i]);
	}
}

static bool _iqk_check_cal(struct rtw89_dev *rtwdev, u8 path)
{
	bool fail = true;
	u32 val;
	int ret;

	ret = read_poll_timeout_atomic(rtw89_phy_read32_mask, val, val == 0x55,
				       1, 8200, false, rtwdev, 0xbff8, MASKBYTE0);
	if (ret)
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]NCTL1 IQK timeout!!!\n");

	udelay(200);

	if (!ret)
		fail = rtw89_phy_read32_mask(rtwdev, R_NCTL_RPT, B_NCTL_RPT_FLG);
	rtw89_phy_write32_mask(rtwdev, R_NCTL_N1, MASKBYTE0, 0x0);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, ret=%d\n", path, ret);
	val = rtw89_phy_read32_mask(rtwdev, R_NCTL_RPT, MASKDWORD);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, 0x8008 = 0x%x\n", path, val);

	return fail;
}

static u8 _kpath(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx)
{
	u8 val;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[RFK]dbcc_en: %x,PHY%d\n",
		    rtwdev->dbcc_en, phy_idx);

	if (!rtwdev->dbcc_en) {
		val = RF_AB;
	} else {
		if (phy_idx == RTW89_PHY_0)
			val = RF_A;
		else
			val = RF_B;
	}
	return val;
}

static void _set_rx_dck(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			enum rtw89_rf_path path)
{
	rtw89_write_rf(rtwdev, path, RR_DCK1, RR_DCK1_CLR, 0x0);
	rtw89_write_rf(rtwdev, path, RR_DCK, RR_DCK_LV, 0x0);
	rtw89_write_rf(rtwdev, path, RR_DCK, RR_DCK_LV, 0x1);
	mdelay(1);
}

static void _rx_dck(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy)
{
	u8 path, dck_tune;
	u32 rf_reg5;

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[RX_DCK] ****** RXDCK Start (Ver: 0x%x, CV : 0x%x) ******\n",
		    RTW8852B_RXDCK_VER, rtwdev->hal.cv);

	for (path = 0; path < RF_PATH_NUM_8852B; path++) {
		rf_reg5 = rtw89_read_rf(rtwdev, path, RR_RSV1, RFREG_MASK);
		dck_tune = rtw89_read_rf(rtwdev, path, RR_DCK, RR_DCK_FINE);

		if (rtwdev->is_tssi_mode[path])
			rtw89_phy_write32_mask(rtwdev,
					       R_P0_TSSI_TRK + (path << 13),
					       B_P0_TSSI_TRK_EN, 0x1);

		rtw89_write_rf(rtwdev, path, RR_RSV1, RR_RSV1_RST, 0x0);
		rtw89_write_rf(rtwdev, path, RR_DCK, RR_DCK_FINE, 0x0);
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_MASK, RR_MOD_V_RX);
		_set_rx_dck(rtwdev, phy, path);
		rtw89_write_rf(rtwdev, path, RR_DCK, RR_DCK_FINE, dck_tune);
		rtw89_write_rf(rtwdev, path, RR_RSV1, RFREG_MASK, rf_reg5);

		if (rtwdev->is_tssi_mode[path])
			rtw89_phy_write32_mask(rtwdev,
					       R_P0_TSSI_TRK + (path << 13),
					       B_P0_TSSI_TRK_EN, 0x0);
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

static void _afe_init(struct rtw89_dev *rtwdev)
{
	rtw89_write32(rtwdev, R_AX_PHYREG_SET, 0xf);

	rtw89_rfk_parser(rtwdev, &rtw8852b_afe_init_defs_tbl);
}

static void _drck(struct rtw89_dev *rtwdev)
{
	u32 rck_d;
	u32 val;
	int ret;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]Ddie RCK start!!!\n");
	rtw89_phy_write32_mask(rtwdev, R_DRCK_V1, B_DRCK_V1_KICK, 0x1);

	ret = read_poll_timeout_atomic(rtw89_phy_read32_mask, val, val, 1, 10000,
				       false, rtwdev, R_DRCK_RS, B_DRCK_RS_DONE);
	if (ret)
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]DRCK timeout\n");

	rtw89_phy_write32_mask(rtwdev, R_DRCK_V1, B_DRCK_V1_KICK, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_DRCK_FH, B_DRCK_LAT, 0x1);
	udelay(1);
	rtw89_phy_write32_mask(rtwdev, R_DRCK_FH, B_DRCK_LAT, 0x0);
	rck_d = rtw89_phy_read32_mask(rtwdev, R_DRCK_RS, B_DRCK_RS_LPS);
	rtw89_phy_write32_mask(rtwdev, R_DRCK_V1, B_DRCK_V1_SEL, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_DRCK_V1, B_DRCK_V1_CV, rck_d);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]0xc0cc = 0x%x\n",
		    rtw89_phy_read32_mask(rtwdev, R_DRCK_V1, MASKDWORD));
}

static void _addck_backup(struct rtw89_dev *rtwdev)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;

	rtw89_phy_write32_mask(rtwdev, R_ADDCK0, B_ADDCK0, 0x0);
	dack->addck_d[0][0] = rtw89_phy_read32_mask(rtwdev, R_ADDCKR0, B_ADDCKR0_A0);
	dack->addck_d[0][1] = rtw89_phy_read32_mask(rtwdev, R_ADDCKR0, B_ADDCKR0_A1);

	rtw89_phy_write32_mask(rtwdev, R_ADDCK1, B_ADDCK1, 0x0);
	dack->addck_d[1][0] = rtw89_phy_read32_mask(rtwdev, R_ADDCKR1, B_ADDCKR1_A0);
	dack->addck_d[1][1] = rtw89_phy_read32_mask(rtwdev, R_ADDCKR1, B_ADDCKR1_A1);
}

static void _addck_reload(struct rtw89_dev *rtwdev)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;

	/* S0 */
	rtw89_phy_write32_mask(rtwdev, R_ADDCK0D, B_ADDCK0D_VAL, dack->addck_d[0][0]);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK0, B_ADDCK0_VAL, dack->addck_d[0][1] >> 6);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK0D, B_ADDCK0D_VAL2, dack->addck_d[0][1] & 0x3f);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK0, B_ADDCK0_MAN, 0x3);

	/* S1 */
	rtw89_phy_write32_mask(rtwdev, R_ADDCK1D, B_ADDCK1D_VAL, dack->addck_d[1][0]);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK1, B_ADDCK0_VAL, dack->addck_d[1][1] >> 6);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK1D, B_ADDCK1D_VAL2, dack->addck_d[1][1] & 0x3f);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK1, B_ADDCK1_MAN, 0x3);
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
		rtw89_phy_read32_mask(rtwdev, R_DACK_DADCK00, B_DACK_DADCK00);
	dack->dadck_d[0][1] =
		rtw89_phy_read32_mask(rtwdev, R_DACK_DADCK01, B_DACK_DADCK01);
}

static void _dack_backup_s1(struct rtw89_dev *rtwdev)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;
	u8 i;

	rtw89_phy_write32_mask(rtwdev, R_P1_DBGMOD, B_P1_DBGMOD_ON, 0x1);

	for (i = 0; i < RTW89_DACK_MSBK_NR; i++) {
		rtw89_phy_write32_mask(rtwdev, R_DACK10, B_DACK10, i);
		dack->msbk_d[1][0][i] =
			rtw89_phy_read32_mask(rtwdev, R_DACK10S, B_DACK10S);
		rtw89_phy_write32_mask(rtwdev, R_DACK11, B_DACK11, i);
		dack->msbk_d[1][1][i] =
			rtw89_phy_read32_mask(rtwdev, R_DACK11S, B_DACK11S);
	}

	dack->biask_d[1][0] =
		rtw89_phy_read32_mask(rtwdev, R_DACK_BIAS10, B_DACK_BIAS10);
	dack->biask_d[1][1] =
		rtw89_phy_read32_mask(rtwdev, R_DACK_BIAS11, B_DACK_BIAS11);

	dack->dadck_d[1][0] =
		rtw89_phy_read32_mask(rtwdev, R_DACK_DADCK10, B_DACK_DADCK10);
	dack->dadck_d[1][1] =
		rtw89_phy_read32_mask(rtwdev, R_DACK_DADCK11, B_DACK_DADCK11);
}

static void _check_addc(struct rtw89_dev *rtwdev, enum rtw89_rf_path path)
{
	s32 dc_re = 0, dc_im = 0;
	u32 tmp;
	u32 i;

	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852b_check_addc_defs_a_tbl,
				 &rtw8852b_check_addc_defs_b_tbl);

	for (i = 0; i < ADDC_T_AVG; i++) {
		tmp = rtw89_phy_read32_mask(rtwdev, R_DBG32_D, MASKDWORD);
		dc_re += sign_extend32(FIELD_GET(0xfff000, tmp), 11);
		dc_im += sign_extend32(FIELD_GET(0xfff, tmp), 11);
	}

	dc_re /= ADDC_T_AVG;
	dc_im /= ADDC_T_AVG;

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DACK]S%d,dc_re = 0x%x,dc_im =0x%x\n", path, dc_re, dc_im);
}

static void _addck(struct rtw89_dev *rtwdev)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;
	u32 val;
	int ret;

	/* S0 */
	rtw89_phy_write32_mask(rtwdev, R_ADDCK0, B_ADDCK0_MAN, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_PATH1_SAMPL_DLY_T_V1, 0x30, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_P0_NRBW, B_P0_NRBW_DBG, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_ADCCLK, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_FLTRST, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_FLTRST, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR_PW15, B_ANAPAR_PW15_H, 0xf);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_EN, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_PATH0_SAMPL_DLY_T_V1, BIT(1), 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR_PW15, B_ANAPAR_PW15_H, 0x3);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]before S0 ADDCK\n");
	_check_addc(rtwdev, RF_PATH_A);

	rtw89_phy_write32_mask(rtwdev, R_ADDCK0, B_ADDCK0_TRG, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK0, B_ADDCK0_TRG, 0x0);
	udelay(1);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK0, B_ADDCK0, 0x1);

	ret = read_poll_timeout_atomic(rtw89_phy_read32_mask, val, val, 1, 10000,
				       false, rtwdev, R_ADDCKR0, BIT(0));
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S0 ADDCK timeout\n");
		dack->addck_timeout[0] = true;
	}
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]ADDCK ret = %d\n", ret);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]after S0 ADDCK\n");
	_check_addc(rtwdev, RF_PATH_A);

	rtw89_phy_write32_mask(rtwdev, R_PATH0_SAMPL_DLY_T_V1, BIT(1), 0x0);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_EN, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR_PW15, B_ANAPAR_PW15_H, 0xc);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_ADCCLK, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_P0_NRBW, B_P0_NRBW_DBG, 0x0);

	/* S1 */
	rtw89_phy_write32_mask(rtwdev, R_P1_DBGMOD, B_P1_DBGMOD_ON, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_ADCCLK, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_FLTRST, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_FLTRST, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR_PW15, B_ANAPAR_PW15_H, 0xf);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_EN, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_PATH1_SAMPL_DLY_T_V1, BIT(1), 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR_PW15, B_ANAPAR_PW15_H, 0x3);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]before S1 ADDCK\n");
	_check_addc(rtwdev, RF_PATH_B);

	rtw89_phy_write32_mask(rtwdev, R_ADDCK1, B_ADDCK1_TRG, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK1, B_ADDCK1_TRG, 0x0);
	udelay(1);
	rtw89_phy_write32_mask(rtwdev, R_ADDCK1, B_ADDCK1, 0x1);

	ret = read_poll_timeout_atomic(rtw89_phy_read32_mask, val, val, 1, 10000,
				       false, rtwdev, R_ADDCKR1, BIT(0));
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S1 ADDCK timeout\n");
		dack->addck_timeout[1] = true;
	}
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]ADDCK ret = %d\n", ret);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]after S1 ADDCK\n");
	_check_addc(rtwdev, RF_PATH_B);

	rtw89_phy_write32_mask(rtwdev, R_PATH1_SAMPL_DLY_T_V1, BIT(1), 0x0);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_EN, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR_PW15, B_ANAPAR_PW15_H, 0xc);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_ADCCLK, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_P1_DBGMOD, B_P1_DBGMOD_ON, 0x0);
}

static void _check_dadc(struct rtw89_dev *rtwdev, enum rtw89_rf_path path)
{
	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852b_check_dadc_en_defs_a_tbl,
				 &rtw8852b_check_dadc_en_defs_b_tbl);

	_check_addc(rtwdev, path);

	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852b_check_dadc_dis_defs_a_tbl,
				 &rtw8852b_check_dadc_dis_defs_b_tbl);
}

static bool _dack_s0_check_done(struct rtw89_dev *rtwdev, bool part1)
{
	if (part1) {
		if (rtw89_phy_read32_mask(rtwdev, R_DACK_S0P0, B_DACK_S0P0_OK) == 0 ||
		    rtw89_phy_read32_mask(rtwdev, R_DACK_S0P1, B_DACK_S0P1_OK) == 0)
			return false;
	} else {
		if (rtw89_phy_read32_mask(rtwdev, R_DACK_S0P2, B_DACK_S0P2_OK) == 0 ||
		    rtw89_phy_read32_mask(rtwdev, R_DACK_S0P3, B_DACK_S0P3_OK) == 0)
			return false;
	}

	return true;
}

static void _dack_s0(struct rtw89_dev *rtwdev)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;
	bool done;
	int ret;

	rtw89_rfk_parser(rtwdev, &rtw8852b_dack_s0_1_defs_tbl);

	ret = read_poll_timeout_atomic(_dack_s0_check_done, done, done, 1, 10000,
				       false, rtwdev, true);
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S0 MSBK timeout\n");
		dack->msbk_timeout[0] = true;
	}
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]DACK ret = %d\n", ret);

	rtw89_rfk_parser(rtwdev, &rtw8852b_dack_s0_2_defs_tbl);

	ret = read_poll_timeout_atomic(_dack_s0_check_done, done, done, 1, 10000,
				       false, rtwdev, false);
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S0 DADCK timeout\n");
		dack->dadck_timeout[0] = true;
	}
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]DACK ret = %d\n", ret);

	rtw89_rfk_parser(rtwdev, &rtw8852b_dack_s0_3_defs_tbl);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]after S0 DADCK\n");

	_dack_backup_s0(rtwdev);
	rtw89_phy_write32_mask(rtwdev, R_P0_NRBW, B_P0_NRBW_DBG, 0x0);
}

static bool _dack_s1_check_done(struct rtw89_dev *rtwdev, bool part1)
{
	if (part1) {
		if (rtw89_phy_read32_mask(rtwdev, R_DACK_S1P0, B_DACK_S1P0_OK) == 0 &&
		    rtw89_phy_read32_mask(rtwdev, R_DACK_S1P1, B_DACK_S1P1_OK) == 0)
			return false;
	} else {
		if (rtw89_phy_read32_mask(rtwdev, R_DACK10S, B_DACK_S1P2_OK) == 0 &&
		    rtw89_phy_read32_mask(rtwdev, R_DACK11S, B_DACK_S1P3_OK) == 0)
			return false;
	}

	return true;
}

static void _dack_s1(struct rtw89_dev *rtwdev)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;
	bool done;
	int ret;

	rtw89_rfk_parser(rtwdev, &rtw8852b_dack_s1_1_defs_tbl);

	ret = read_poll_timeout_atomic(_dack_s1_check_done, done, done, 1, 10000,
				       false, rtwdev, true);
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S1 MSBK timeout\n");
		dack->msbk_timeout[1] = true;
	}
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]DACK ret = %d\n", ret);

	rtw89_rfk_parser(rtwdev, &rtw8852b_dack_s1_2_defs_tbl);

	ret = read_poll_timeout_atomic(_dack_s1_check_done, done, done, 1, 10000,
				       false, rtwdev, false);
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S1 DADCK timeout\n");
		dack->dadck_timeout[1] = true;
	}
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]DACK ret = %d\n", ret);

	rtw89_rfk_parser(rtwdev, &rtw8852b_dack_s1_3_defs_tbl);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]after S1 DADCK\n");

	_check_dadc(rtwdev, RF_PATH_B);
	_dack_backup_s1(rtwdev);
	rtw89_phy_write32_mask(rtwdev, R_P1_DBGMOD, B_P1_DBGMOD_ON, 0x0);
}

static void _dack(struct rtw89_dev *rtwdev)
{
	_dack_s0(rtwdev);
	_dack_s1(rtwdev);
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
	for (i = 0; i < 0x10; i++) {
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

static void _dac_cal(struct rtw89_dev *rtwdev, bool force)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;
	u32 rf0_0, rf1_0;

	dack->dack_done = false;
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]DACK 0x1\n");
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]DACK start!!!\n");

	rf0_0 = rtw89_read_rf(rtwdev, RF_PATH_A, RR_MOD, RFREG_MASK);
	rf1_0 = rtw89_read_rf(rtwdev, RF_PATH_B, RR_MOD, RFREG_MASK);
	_afe_init(rtwdev);
	_drck(rtwdev);

	rtw89_write_rf(rtwdev, RF_PATH_A, RR_RSV1, RR_RSV1_RST, 0x0);
	rtw89_write_rf(rtwdev, RF_PATH_B, RR_RSV1, RR_RSV1_RST, 0x0);
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_MOD, RFREG_MASK, 0x337e1);
	rtw89_write_rf(rtwdev, RF_PATH_B, RR_MOD, RFREG_MASK, 0x337e1);
	_addck(rtwdev);
	_addck_backup(rtwdev);
	_addck_reload(rtwdev);

	rtw89_write_rf(rtwdev, RF_PATH_A, RR_MODOPT, RFREG_MASK, 0x0);
	rtw89_write_rf(rtwdev, RF_PATH_B, RR_MODOPT, RFREG_MASK, 0x0);
	_dack(rtwdev);
	_dack_dump(rtwdev);
	dack->dack_done = true;

	rtw89_write_rf(rtwdev, RF_PATH_A, RR_MOD, RFREG_MASK, rf0_0);
	rtw89_write_rf(rtwdev, RF_PATH_B, RR_MOD, RFREG_MASK, rf1_0);
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_RSV1, RR_RSV1_RST, 0x1);
	rtw89_write_rf(rtwdev, RF_PATH_B, RR_RSV1, RR_RSV1_RST, 0x1);
	dack->dack_cnt++;
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]DACK finish!!!\n");
}

static void _iqk_rxk_setting(struct rtw89_dev *rtwdev, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	u32 tmp;

	switch (iqk_info->iqk_band[path]) {
	case RTW89_BAND_2G:
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_MASK, 0xc);
		rtw89_write_rf(rtwdev, path, RR_RXK, RR_RXK_SEL2G, 0x1);
		tmp = rtw89_read_rf(rtwdev, path, RR_CFGCH, RFREG_MASK);
		rtw89_write_rf(rtwdev, path, RR_RSV4, RFREG_MASK, tmp);
		break;
	case RTW89_BAND_5G:
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_MASK, 0xc);
		rtw89_write_rf(rtwdev, path, RR_RXK, RR_RXK_SEL5G, 0x1);
		tmp = rtw89_read_rf(rtwdev, path, RR_CFGCH, RFREG_MASK);
		rtw89_write_rf(rtwdev, path, RR_RSV4, RFREG_MASK, tmp);
		break;
	default:
		break;
	}
}

static bool _iqk_one_shot(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx,
			  u8 path, u8 ktype)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	u32 iqk_cmd;
	bool fail;

	switch (ktype) {
	case ID_FLOK_COARSE:
		rtw89_phy_write32_mask(rtwdev, R_P0_RFCTM, B_P0_RFCTM_EN, 0x1);
		iqk_cmd = 0x108 | (1 << (4 + path));
		break;
	case ID_FLOK_FINE:
		rtw89_phy_write32_mask(rtwdev, R_P0_RFCTM, B_P0_RFCTM_EN, 0x1);
		iqk_cmd = 0x208 | (1 << (4 + path));
		break;
	case ID_FLOK_VBUFFER:
		rtw89_phy_write32_mask(rtwdev, R_P0_RFCTM, B_P0_RFCTM_EN, 0x1);
		iqk_cmd = 0x308 | (1 << (4 + path));
		break;
	case ID_TXK:
		rtw89_phy_write32_mask(rtwdev, R_P0_RFCTM, B_P0_RFCTM_EN, 0x0);
		iqk_cmd = 0x008 | (1 << (path + 4)) |
			  (((0x8 + iqk_info->iqk_bw[path]) & 0xf) << 8);
		break;
	case ID_RXAGC:
		iqk_cmd = 0x508 | (1 << (4 + path)) | (path << 1);
		break;
	case ID_RXK:
		rtw89_phy_write32_mask(rtwdev, R_P0_RFCTM, B_P0_RFCTM_EN, 0x1);
		iqk_cmd = 0x008 | (1 << (path + 4)) |
			  (((0xb + iqk_info->iqk_bw[path]) & 0xf) << 8);
		break;
	case ID_NBTXK:
		rtw89_phy_write32_mask(rtwdev, R_P0_RFCTM, B_P0_RFCTM_EN, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_TXT, 0x011);
		iqk_cmd = 0x308 | (1 << (4 + path));
		break;
	case ID_NBRXK:
		rtw89_phy_write32_mask(rtwdev, R_P0_RFCTM, B_P0_RFCTM_EN, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_RXT, 0x011);
		iqk_cmd = 0x608 | (1 << (4 + path));
		break;
	default:
		return false;
	}

	rtw89_phy_write32_mask(rtwdev, R_NCTL_CFG, MASKDWORD, iqk_cmd + 1);
	udelay(1);
	fail = _iqk_check_cal(rtwdev, path);
	rtw89_phy_write32_mask(rtwdev, R_P0_RFCTM, B_P0_RFCTM_EN, 0x0);

	return fail;
}

static bool _rxk_group_sel(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx,
			   u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	bool kfail = false;
	bool fail;
	u8 gp;

	for (gp = 0; gp < RTW8852B_RXK_GROUP_NR; gp++) {
		switch (iqk_info->iqk_band[path]) {
		case RTW89_BAND_2G:
			rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_RGM,
				       _g_idxrxgain[gp]);
			rtw89_write_rf(rtwdev, path, RR_RXBB, RR_RXBB_C2G,
				       _g_idxattc2[gp]);
			rtw89_write_rf(rtwdev, path, RR_RXBB, RR_RXBB_C1G,
				       _g_idxattc1[gp]);
			break;
		case RTW89_BAND_5G:
			rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_RGM,
				       _a_idxrxgain[gp]);
			rtw89_write_rf(rtwdev, path, RR_RXA2, RR_RXA2_HATT,
				       _a_idxattc2[gp]);
			rtw89_write_rf(rtwdev, path, RR_RXA2, RR_RXA2_CC2,
				       _a_idxattc1[gp]);
			break;
		default:
			break;
		}

		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8),
				       B_CFIR_LUT_SEL, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8),
				       B_CFIR_LUT_SET, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8),
				       B_CFIR_LUT_GP_V1, gp);
		fail = _iqk_one_shot(rtwdev, phy_idx, path, ID_RXK);
		rtw89_phy_write32_mask(rtwdev, R_IQKINF,
				       BIT(16 + gp + path * 4), fail);
		kfail |= fail;
	}
	rtw89_write_rf(rtwdev, path, RR_RXK, RR_RXK_SEL5G, 0x0);

	if (kfail) {
		iqk_info->nb_rxcfir[path] = 0x40000002;
		rtw89_phy_write32_mask(rtwdev, R_IQK_RES + (path << 8),
				       B_IQK_RES_RXCFIR, 0x0);
		iqk_info->is_wb_rxiqk[path] = false;
	} else {
		iqk_info->nb_rxcfir[path] = 0x40000000;
		rtw89_phy_write32_mask(rtwdev, R_IQK_RES + (path << 8),
				       B_IQK_RES_RXCFIR, 0x5);
		iqk_info->is_wb_rxiqk[path] = true;
	}

	return kfail;
}

static bool _iqk_nbrxk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx,
		       u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	const u8 gp = 0x3;
	bool kfail = false;
	bool fail;

	switch (iqk_info->iqk_band[path]) {
	case RTW89_BAND_2G:
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_RGM,
			       _g_idxrxgain[gp]);
		rtw89_write_rf(rtwdev, path, RR_RXBB, RR_RXBB_C2G,
			       _g_idxattc2[gp]);
		rtw89_write_rf(rtwdev, path, RR_RXBB, RR_RXBB_C1G,
			       _g_idxattc1[gp]);
		break;
	case RTW89_BAND_5G:
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_RGM,
			       _a_idxrxgain[gp]);
		rtw89_write_rf(rtwdev, path, RR_RXA2, RR_RXA2_HATT,
			       _a_idxattc2[gp]);
		rtw89_write_rf(rtwdev, path, RR_RXA2, RR_RXA2_CC2,
			       _a_idxattc1[gp]);
		break;
	default:
		break;
	}

	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_SEL, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_SET, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_GP_V1, gp);
	rtw89_write_rf(rtwdev, path, RR_RXKPLL, RFREG_MASK, 0x80013);
	udelay(1);

	fail = _iqk_one_shot(rtwdev, phy_idx, path, ID_NBRXK);
	rtw89_phy_write32_mask(rtwdev, R_IQKINF, BIT(16 + gp + path * 4), fail);
	kfail |= fail;
	rtw89_write_rf(rtwdev, path, RR_RXK, RR_RXK_SEL5G, 0x0);

	if (!kfail)
		iqk_info->nb_rxcfir[path] =
			 rtw89_phy_read32_mask(rtwdev, R_RXIQC + (path << 8), MASKDWORD) | 0x2;
	else
		iqk_info->nb_rxcfir[path] = 0x40000002;

	return kfail;
}

static void _iqk_rxclk_setting(struct rtw89_dev *rtwdev, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;

	if (iqk_info->iqk_bw[path] == RTW89_CHANNEL_WIDTH_80) {
		rtw89_phy_write32_mask(rtwdev, R_P0_NRBW, B_P0_NRBW_DBG, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_P1_DBGMOD, B_P1_DBGMOD_ON, 0x1);
		udelay(1);
		rtw89_phy_write32_mask(rtwdev, R_ANAPAR_PW15, B_ANAPAR_PW15, 0x0f);
		udelay(1);
		rtw89_phy_write32_mask(rtwdev, R_ANAPAR_PW15, B_ANAPAR_PW15, 0x03);
		rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_15, 0xa001);
		udelay(1);
		rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_15, 0xa041);
		rtw89_phy_write32_mask(rtwdev, R_P0_RXCK, B_P0_RXCK_VAL, 0x2);
		rtw89_phy_write32_mask(rtwdev, R_P0_RXCK, B_P0_RXCK_ON, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_P1_RXCK, B_P1_RXCK_VAL, 0x2);
		rtw89_phy_write32_mask(rtwdev, R_P1_RXCK, B_P1_RXCK_ON, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK_ADC, B_UPD_CLK_ADC_ON, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK_ADC, B_UPD_CLK_ADC_VAL, 0x1);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_P0_NRBW, B_P0_NRBW_DBG, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_P1_DBGMOD, B_P1_DBGMOD_ON, 0x1);
		udelay(1);
		rtw89_phy_write32_mask(rtwdev, R_ANAPAR_PW15, B_ANAPAR_PW15, 0x0f);
		udelay(1);
		rtw89_phy_write32_mask(rtwdev, R_ANAPAR_PW15, B_ANAPAR_PW15, 0x03);
		rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_15, 0xa001);
		udelay(1);
		rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_15, 0xa041);
		rtw89_phy_write32_mask(rtwdev, R_P0_RXCK, B_P0_RXCK_VAL, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_P0_RXCK, B_P0_RXCK_ON, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_P1_RXCK, B_P1_RXCK_VAL, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_P1_RXCK, B_P1_RXCK_ON, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK_ADC, B_UPD_CLK_ADC_ON, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK_ADC, B_UPD_CLK_ADC_VAL, 0x0);
	}
}

static bool _txk_group_sel(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	bool kfail = false;
	bool fail;
	u8 gp;

	for (gp = 0x0; gp < RTW8852B_RXK_GROUP_NR; gp++) {
		switch (iqk_info->iqk_band[path]) {
		case RTW89_BAND_2G:
			rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR0,
				       _g_power_range[gp]);
			rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR1,
				       _g_track_range[gp]);
			rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG,
				       _g_gain_bb[gp]);
			rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8),
					       MASKDWORD, _g_itqt[gp]);
			break;
		case RTW89_BAND_5G:
			rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR0,
				       _a_power_range[gp]);
			rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR1,
				       _a_track_range[gp]);
			rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG,
				       _a_gain_bb[gp]);
			rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8),
					       MASKDWORD, _a_itqt[gp]);
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
				       B_CFIR_LUT_GP, gp);
		rtw89_phy_write32_mask(rtwdev, R_NCTL_N1, B_NCTL_N1_CIP, 0x00);
		fail = _iqk_one_shot(rtwdev, phy_idx, path, ID_TXK);
		rtw89_phy_write32_mask(rtwdev, R_IQKINF,
				       BIT(8 + gp + path * 4), fail);
		kfail |= fail;
	}

	if (kfail) {
		iqk_info->nb_txcfir[path] = 0x40000002;
		rtw89_phy_write32_mask(rtwdev, R_IQK_RES + (path << 8),
				       B_IQK_RES_TXCFIR, 0x0);
		iqk_info->is_wb_txiqk[path] = false;
	} else {
		iqk_info->nb_txcfir[path] = 0x40000000;
		rtw89_phy_write32_mask(rtwdev, R_IQK_RES + (path << 8),
				       B_IQK_RES_TXCFIR, 0x5);
		iqk_info->is_wb_txiqk[path] = true;
	}

	return kfail;
}

static bool _iqk_nbtxk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	bool kfail;
	u8 gp = 0x3;

	switch (iqk_info->iqk_band[path]) {
	case RTW89_BAND_2G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR0,
			       _g_power_range[gp]);
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR1,
			       _g_track_range[gp]);
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG,
			       _g_gain_bb[gp]);
		rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8),
				       MASKDWORD, _g_itqt[gp]);
		break;
	case RTW89_BAND_5G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR0,
			       _a_power_range[gp]);
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR1,
			       _a_track_range[gp]);
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG,
			       _a_gain_bb[gp]);
		rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8),
				       MASKDWORD, _a_itqt[gp]);
		break;
	default:
		break;
	}

	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_SEL, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_SET, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_G2, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_GP, gp);
	rtw89_phy_write32_mask(rtwdev, R_NCTL_N1, B_NCTL_N1_CIP, 0x00);
	kfail = _iqk_one_shot(rtwdev, phy_idx, path, ID_NBTXK);

	if (!kfail)
		iqk_info->nb_txcfir[path] =
			rtw89_phy_read32_mask(rtwdev, R_TXIQC + (path << 8),
					      MASKDWORD) | 0x2;
	else
		iqk_info->nb_txcfir[path] = 0x40000002;

	return kfail;
}

static void _lok_res_table(struct rtw89_dev *rtwdev, u8 path, u8 ibias)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]===>%s\n", __func__);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, ibias = %x\n", path, ibias);

	rtw89_write_rf(rtwdev, path, RR_LUTWE, RFREG_MASK, 0x2);
	if (iqk_info->iqk_band[path] == RTW89_BAND_2G)
		rtw89_write_rf(rtwdev, path, RR_LUTWA, RFREG_MASK, 0x0);
	else
		rtw89_write_rf(rtwdev, path, RR_LUTWA, RFREG_MASK, 0x1);
	rtw89_write_rf(rtwdev, path, RR_LUTWD0, RFREG_MASK, ibias);
	rtw89_write_rf(rtwdev, path, RR_LUTWE, RFREG_MASK, 0x0);
	rtw89_write_rf(rtwdev, path, RR_TXVBUF, RR_TXVBUF_DACEN, 0x1);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, 0x7c = %x\n", path,
		    rtw89_read_rf(rtwdev, path, RR_TXVBUF, RFREG_MASK));
}

static bool _lok_finetune_check(struct rtw89_dev *rtwdev, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	bool is_fail1, is_fail2;
	u32 vbuff_i;
	u32 vbuff_q;
	u32 core_i;
	u32 core_q;
	u32 tmp;
	u8 ch;

	tmp = rtw89_read_rf(rtwdev, path, RR_TXMO, RFREG_MASK);
	core_i = FIELD_GET(RR_TXMO_COI, tmp);
	core_q = FIELD_GET(RR_TXMO_COQ, tmp);
	ch = (iqk_info->iqk_times / 2) % RTW89_IQK_CHS_NR;

	if (core_i < 0x2 || core_i > 0x1d || core_q < 0x2 || core_q > 0x1d)
		is_fail1 = true;
	else
		is_fail1 = false;

	iqk_info->lok_idac[ch][path] = tmp;

	tmp = rtw89_read_rf(rtwdev, path, RR_LOKVB, RFREG_MASK);
	vbuff_i = FIELD_GET(RR_LOKVB_COI, tmp);
	vbuff_q = FIELD_GET(RR_LOKVB_COQ, tmp);

	if (vbuff_i < 0x2 || vbuff_i > 0x3d || vbuff_q < 0x2 || vbuff_q > 0x3d)
		is_fail2 = true;
	else
		is_fail2 = false;

	iqk_info->lok_vbuf[ch][path] = tmp;

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[IQK]S%x, lok_idac[%x][%x] = 0x%x\n", path, ch, path,
		    iqk_info->lok_idac[ch][path]);
	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[IQK]S%x, lok_vbuf[%x][%x] = 0x%x\n", path, ch, path,
		    iqk_info->lok_vbuf[ch][path]);

	return is_fail1 | is_fail2;
}

static bool _iqk_lok(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	bool tmp;

	rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_TXT, 0x021);

	switch (iqk_info->iqk_band[path]) {
	case RTW89_BAND_2G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR0, 0x0);
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR1, 0x6);
		break;
	case RTW89_BAND_5G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR0, 0x0);
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_GR1, 0x4);
		break;
	default:
		break;
	}

	switch (iqk_info->iqk_band[path]) {
	case RTW89_BAND_2G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, 0x0);
		break;
	case RTW89_BAND_5G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, 0x0);
		break;
	default:
		break;
	}

	rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8), MASKDWORD, 0x9);
	tmp = _iqk_one_shot(rtwdev, phy_idx, path, ID_FLOK_COARSE);
	iqk_info->lok_cor_fail[0][path] = tmp;

	switch (iqk_info->iqk_band[path]) {
	case RTW89_BAND_2G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, 0x12);
		break;
	case RTW89_BAND_5G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, 0x12);
		break;
	default:
		break;
	}

	rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8), MASKDWORD, 0x24);
	tmp = _iqk_one_shot(rtwdev, phy_idx, path, ID_FLOK_VBUFFER);

	switch (iqk_info->iqk_band[path]) {
	case RTW89_BAND_2G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, 0x0);
		break;
	case RTW89_BAND_5G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, 0x0);
		break;
	default:
		break;
	}

	rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8), MASKDWORD, 0x9);
	rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_TXT, 0x021);
	tmp = _iqk_one_shot(rtwdev, phy_idx, path, ID_FLOK_FINE);
	iqk_info->lok_fin_fail[0][path] = tmp;

	switch (iqk_info->iqk_band[path]) {
	case RTW89_BAND_2G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, 0x12);
		break;
	case RTW89_BAND_5G:
		rtw89_write_rf(rtwdev, path, RR_TXIG, RR_TXIG_TG, 0x12);
		break;
	default:
		break;
	}

	rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8), MASKDWORD, 0x24);
	_iqk_one_shot(rtwdev, phy_idx, path, ID_FLOK_VBUFFER);

	return _lok_finetune_check(rtwdev, path);
}

static void _iqk_txk_setting(struct rtw89_dev *rtwdev, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;

	switch (iqk_info->iqk_band[path]) {
	case RTW89_BAND_2G:
		rtw89_write_rf(rtwdev, path, RR_XALNA2, RR_XALNA2_SW2, 0x00);
		rtw89_write_rf(rtwdev, path, RR_TXG1, RR_TXG1_ATT2, 0x0);
		rtw89_write_rf(rtwdev, path, RR_TXG1, RR_TXG1_ATT1, 0x0);
		rtw89_write_rf(rtwdev, path, RR_TXG2, RR_TXG2_ATT0, 0x1);
		rtw89_write_rf(rtwdev, path, RR_TXGA, RR_TXGA_LOK_EXT, 0x0);
		rtw89_write_rf(rtwdev, path, RR_LUTWE, RR_LUTWE_LOK, 0x1);
		rtw89_write_rf(rtwdev, path, RR_LUTWA, RR_LUTWA_M1, 0x00);
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_IQK, 0x403e);
		udelay(1);
		break;
	case RTW89_BAND_5G:
		rtw89_write_rf(rtwdev, path, RR_XGLNA2, RR_XGLNA2_SW, 0x00);
		rtw89_write_rf(rtwdev, path, RR_BIASA, RR_BIASA_A, 0x1);
		rtw89_write_rf(rtwdev, path, RR_TXGA, RR_TXGA_LOK_EXT, 0x0);
		rtw89_write_rf(rtwdev, path, RR_LUTWE, RR_LUTWE_LOK, 0x1);
		rtw89_write_rf(rtwdev, path, RR_LUTWA, RR_LUTWA_M1, 0x80);
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_IQK, 0x403e);
		udelay(1);
		break;
	default:
		break;
	}
}

static void _iqk_txclk_setting(struct rtw89_dev *rtwdev, u8 path)
{
	rtw89_phy_write32_mask(rtwdev, R_P0_NRBW, B_P0_NRBW_DBG, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_P1_DBGMOD, B_P1_DBGMOD_ON, 0x1);
	udelay(1);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR_PW15, B_ANAPAR_PW15, 0x1f);
	udelay(1);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR_PW15, B_ANAPAR_PW15, 0x13);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_15, 0x0001);
	udelay(1);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_15, 0x0041);
}

static void _iqk_info_iqk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	u32 tmp;
	bool flag;

	iqk_info->thermal[path] =
		ewma_thermal_read(&rtwdev->phystat.avg_thermal[path]);
	iqk_info->thermal_rek_en = false;

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

	rtw89_phy_write32_mask(rtwdev, R_IQKINF2, B_IQKINF2_KCNT, iqk_info->iqk_times);

	tmp = rtw89_phy_read32_mask(rtwdev, R_IQKINF, B_IQKINF_FAIL << (path * 4));
	if (tmp)
		iqk_info->iqk_fail_cnt++;
	rtw89_phy_write32_mask(rtwdev, R_IQKINF2, B_IQKINF2_FCNT << (path * 4),
			       iqk_info->iqk_fail_cnt);
}

static void _iqk_by_path(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	bool lok_is_fail = false;
	const int try = 3;
	u8 ibias = 0x1;
	u8 i;

	_iqk_txclk_setting(rtwdev, path);

	/* LOK */
	for (i = 0; i < try; i++) {
		_lok_res_table(rtwdev, path, ibias++);
		_iqk_txk_setting(rtwdev, path);
		lok_is_fail = _iqk_lok(rtwdev, phy_idx, path);
		if (!lok_is_fail)
			break;
	}

	if (lok_is_fail)
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK] LOK (%d) fail\n", path);

	/* TXK */
	if (iqk_info->is_nbiqk)
		iqk_info->iqk_tx_fail[0][path] = _iqk_nbtxk(rtwdev, phy_idx, path);
	else
		iqk_info->iqk_tx_fail[0][path] = _txk_group_sel(rtwdev, phy_idx, path);

	/* RX */
	_iqk_rxclk_setting(rtwdev, path);
	_iqk_rxk_setting(rtwdev, path);
	if (iqk_info->is_nbiqk)
		iqk_info->iqk_rx_fail[0][path] = _iqk_nbrxk(rtwdev, phy_idx, path);
	else
		iqk_info->iqk_rx_fail[0][path] = _rxk_group_sel(rtwdev, phy_idx, path);

	_iqk_info_iqk(rtwdev, phy_idx, path);
}

static void _iqk_get_ch_info(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy, u8 path)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	u32 reg_rf18;
	u32 reg_35c;
	u8 idx;
	u8 get_empty_table = false;

	for (idx = 0; idx < RTW89_IQK_CHS_NR; idx++) {
		if (iqk_info->iqk_mcc_ch[idx][path] == 0) {
			get_empty_table = true;
			break;
		}
	}
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK] (1)idx = %x\n", idx);

	if (!get_empty_table) {
		idx = iqk_info->iqk_table_idx[path] + 1;
		if (idx > 1)
			idx = 0;
	}
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK] (2)idx = %x\n", idx);

	reg_rf18 = rtw89_read_rf(rtwdev, path, RR_CFGCH, RFREG_MASK);
	reg_35c = rtw89_phy_read32_mask(rtwdev, R_CIRST, B_CIRST_SYN);

	iqk_info->iqk_band[path] = chan->band_type;
	iqk_info->iqk_bw[path] = chan->band_width;
	iqk_info->iqk_ch[path] = chan->channel;
	iqk_info->iqk_mcc_ch[idx][path] = chan->channel;
	iqk_info->iqk_table_idx[path] = idx;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, 0x18= 0x%x, idx = %x\n",
		    path, reg_rf18, idx);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, 0x18= 0x%x\n",
		    path, reg_rf18);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]times = 0x%x, ch =%x\n",
		    iqk_info->iqk_times, idx);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]iqk_mcc_ch[%x][%x] = 0x%x\n",
		    idx, path, iqk_info->iqk_mcc_ch[idx][path]);

	if (reg_35c == 0x01)
		iqk_info->syn1to2 = 0x1;
	else
		iqk_info->syn1to2 = 0x0;

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[IQK]S%x, iqk_info->syn1to2= 0x%x\n", path,
		    iqk_info->syn1to2);

	rtw89_phy_write32_mask(rtwdev, R_IQKINF, B_IQKINF_VER, RTW8852B_IQK_VER);
	/* 2GHz/5GHz/6GHz = 0/1/2 */
	rtw89_phy_write32_mask(rtwdev, R_IQKCH, B_IQKCH_BAND << (path * 16),
			       iqk_info->iqk_band[path]);
	/* 20/40/80 = 0/1/2 */
	rtw89_phy_write32_mask(rtwdev, R_IQKCH, B_IQKCH_BW << (path * 16),
			       iqk_info->iqk_bw[path]);
	rtw89_phy_write32_mask(rtwdev, R_IQKCH, B_IQKCH_CH << (path * 16),
			       iqk_info->iqk_ch[path]);
}

static void _iqk_start_iqk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx, u8 path)
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
			       0x00000e19 + (path << 4));
	fail = _iqk_check_cal(rtwdev, path);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "%s result =%x\n", __func__, fail);

	rtw89_phy_write32_mask(rtwdev, R_NCTL_N1, B_NCTL_N1_CIP, 0x00);
	rtw89_phy_write32_mask(rtwdev, R_NCTL_RPT, MASKDWORD, 0x00000000);
	rtw89_phy_write32_mask(rtwdev, R_KIP_SYSCFG, MASKDWORD, 0x80000000);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_SYS, B_IQK_RES_K, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_IQRSN, B_IQRSN_K1, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_IQRSN, B_IQRSN_K2, 0x0);
	rtw89_write_rf(rtwdev, path, RR_LUTWE, RR_LUTWE_LOK, 0x0);
	rtw89_write_rf(rtwdev, path, RR_LUTWE, RR_LUTWE_LOK, 0x0);
	rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_MASK, 0x3);
	rtw89_write_rf(rtwdev, path, RR_RSV1, RR_RSV1_RST, 0x1);
	rtw89_write_rf(rtwdev, path, RR_BBDC, RR_BBDC_SEL, 0x1);
}

static void _iqk_afebb_restore(struct rtw89_dev *rtwdev,
			       enum rtw89_phy_idx phy_idx, u8 path)
{
	const struct rtw89_reg3_def *def;
	int size;
	u8 kpath;
	int i;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "===> %s\n", __func__);

	kpath = _kpath(rtwdev, phy_idx);

	switch (kpath) {
	case RF_A:
	case RF_B:
		return;
	default:
		size = ARRAY_SIZE(rtw8852b_restore_nondbcc_path01);
		def = rtw8852b_restore_nondbcc_path01;
		break;
	}

	for (i = 0; i < size; i++, def++)
		rtw89_phy_write32_mask(rtwdev, def->addr, def->mask, def->data);
}

static void _iqk_preset(struct rtw89_dev *rtwdev, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	u8 idx;

	idx = iqk_info->iqk_table_idx[path];
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK] (3)idx = %x\n", idx);

	rtw89_phy_write32_mask(rtwdev, R_COEF_SEL + (path << 8), B_COEF_SEL_IQC, idx);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_G3, idx);

	rtw89_write_rf(rtwdev, path, RR_RSV1, RR_RSV1_RST, 0x0);
	rtw89_write_rf(rtwdev, path, RR_BBDC, RR_BBDC_SEL, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_NCTL_RPT, MASKDWORD, 0x00000080);
	rtw89_phy_write32_mask(rtwdev, R_KIP_SYSCFG, MASKDWORD, 0x81ff010a);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK](1)S%x, 0x8%x54 = 0x%x\n", path, 1 << path,
		    rtw89_phy_read32_mask(rtwdev, R_CFIR_LUT + (path << 8), MASKDWORD));
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK](1)S%x, 0x8%x04 = 0x%x\n", path, 1 << path,
		    rtw89_phy_read32_mask(rtwdev, R_COEF_SEL + (path << 8), MASKDWORD));
}

static void _iqk_macbb_setting(struct rtw89_dev *rtwdev,
			       enum rtw89_phy_idx phy_idx, u8 path)
{
	const struct rtw89_reg3_def *def;
	int size;
	u8 kpath;
	int i;

	kpath = _kpath(rtwdev, phy_idx);

	switch (kpath) {
	case RF_A:
	case RF_B:
		return;
	default:
		size = ARRAY_SIZE(rtw8852b_set_nondbcc_path01);
		def = rtw8852b_set_nondbcc_path01;
		break;
	}

	for (i = 0; i < size; i++, def++)
		rtw89_phy_write32_mask(rtwdev, def->addr, def->mask, def->data);
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
		for (path = 0; path < RTW8852B_IQK_SS; path++) {
			iqk_info->lok_cor_fail[idx][path] = false;
			iqk_info->lok_fin_fail[idx][path] = false;
			iqk_info->iqk_tx_fail[idx][path] = false;
			iqk_info->iqk_rx_fail[idx][path] = false;
			iqk_info->iqk_mcc_ch[idx][path] = 0x0;
			iqk_info->iqk_table_idx[path] = 0x0;
		}
	}
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
					       rtwdev, path, RR_MOD, RR_MOD_MASK);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[RFK] Wait S%d to Rx mode!! (ret = %d)\n", path, ret);
	}
}

static void _tmac_tx_pause(struct rtw89_dev *rtwdev, enum rtw89_phy_idx band_idx,
			   bool is_pause)
{
	if (!is_pause)
		return;

	_wait_rx_mode(rtwdev, _kpath(rtwdev, band_idx));
}

static void _doiqk(struct rtw89_dev *rtwdev, bool force,
		   enum rtw89_phy_idx phy_idx, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	u32 backup_bb_val[BACKUP_BB_REGS_NR];
	u32 backup_rf_val[RTW8852B_IQK_SS][BACKUP_RF_REGS_NR];
	u8 phy_map = rtw89_btc_phymap(rtwdev, phy_idx, RF_AB);

	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_IQK, BTC_WRFK_ONESHOT_START);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[IQK]==========IQK strat!!!!!==========\n");
	iqk_info->iqk_times++;
	iqk_info->kcount = 0;
	iqk_info->version = RTW8852B_IQK_VER;

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

	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_IQK, BTC_WRFK_ONESHOT_STOP);
}

static void _iqk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx, bool force)
{
	u8 kpath = _kpath(rtwdev, phy_idx);

	switch (kpath) {
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

static void _tssi_rf_setting(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			     enum rtw89_rf_path path)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	enum rtw89_band band = chan->band_type;

	if (band == RTW89_BAND_2G)
		rtw89_write_rf(rtwdev, path, RR_TXPOW, RR_TXPOW_TXG, 0x1);
	else
		rtw89_write_rf(rtwdev, path, RR_TXPOW, RR_TXPOW_TXA, 0x1);
}

static void _tssi_set_sys(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			  enum rtw89_rf_path path)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	enum rtw89_band band = chan->band_type;

	rtw89_rfk_parser(rtwdev, &rtw8852b_tssi_sys_defs_tbl);

	if (path == RF_PATH_A)
		rtw89_rfk_parser_by_cond(rtwdev, band == RTW89_BAND_2G,
					 &rtw8852b_tssi_sys_a_defs_2g_tbl,
					 &rtw8852b_tssi_sys_a_defs_5g_tbl);
	else
		rtw89_rfk_parser_by_cond(rtwdev, band == RTW89_BAND_2G,
					 &rtw8852b_tssi_sys_b_defs_2g_tbl,
					 &rtw8852b_tssi_sys_b_defs_5g_tbl);
}

static void _tssi_ini_txpwr_ctrl_bb(struct rtw89_dev *rtwdev,
				    enum rtw89_phy_idx phy,
				    enum rtw89_rf_path path)
{
	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852b_tssi_init_txpwr_defs_a_tbl,
				 &rtw8852b_tssi_init_txpwr_defs_b_tbl);
}

static void _tssi_ini_txpwr_ctrl_bb_he_tb(struct rtw89_dev *rtwdev,
					  enum rtw89_phy_idx phy,
					  enum rtw89_rf_path path)
{
	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852b_tssi_init_txpwr_he_tb_defs_a_tbl,
				 &rtw8852b_tssi_init_txpwr_he_tb_defs_b_tbl);
}

static void _tssi_set_dck(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			  enum rtw89_rf_path path)
{
	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852b_tssi_dck_defs_a_tbl,
				 &rtw8852b_tssi_dck_defs_b_tbl);
}

static void _tssi_set_tmeter_tbl(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
				 enum rtw89_rf_path path)
{
#define RTW8852B_TSSI_GET_VAL(ptr, idx)			\
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
		thm_up_a = rtw89_8852b_trk_cfg.delta_swingidx_2ga_p;
		thm_down_a = rtw89_8852b_trk_cfg.delta_swingidx_2ga_n;
		thm_up_b = rtw89_8852b_trk_cfg.delta_swingidx_2gb_p;
		thm_down_b = rtw89_8852b_trk_cfg.delta_swingidx_2gb_n;
		break;
	case RTW89_CH_5G_BAND_1:
		thm_up_a = rtw89_8852b_trk_cfg.delta_swingidx_5ga_p[0];
		thm_down_a = rtw89_8852b_trk_cfg.delta_swingidx_5ga_n[0];
		thm_up_b = rtw89_8852b_trk_cfg.delta_swingidx_5gb_p[0];
		thm_down_b = rtw89_8852b_trk_cfg.delta_swingidx_5gb_n[0];
		break;
	case RTW89_CH_5G_BAND_3:
		thm_up_a = rtw89_8852b_trk_cfg.delta_swingidx_5ga_p[1];
		thm_down_a = rtw89_8852b_trk_cfg.delta_swingidx_5ga_n[1];
		thm_up_b = rtw89_8852b_trk_cfg.delta_swingidx_5gb_p[1];
		thm_down_b = rtw89_8852b_trk_cfg.delta_swingidx_5gb_n[1];
		break;
	case RTW89_CH_5G_BAND_4:
		thm_up_a = rtw89_8852b_trk_cfg.delta_swingidx_5ga_p[2];
		thm_down_a = rtw89_8852b_trk_cfg.delta_swingidx_5ga_n[2];
		thm_up_b = rtw89_8852b_trk_cfg.delta_swingidx_5gb_p[2];
		thm_down_b = rtw89_8852b_trk_cfg.delta_swingidx_5gb_n[2];
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
				tmp = RTW8852B_TSSI_GET_VAL(thm_ofst, i);
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
				tmp = RTW8852B_TSSI_GET_VAL(thm_ofst, i);
				rtw89_phy_write32(rtwdev, R_TSSI_THOF + i, tmp);

				rtw89_debug(rtwdev, RTW89_DBG_TSSI,
					    "[TSSI] write 0x%x val=0x%08x\n",
					    0x7c00 + i, tmp);
			}
		}
		rtw89_phy_write32_mask(rtwdev, R_P1_RFCTM, R_P1_RFCTM_RDY, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_P1_RFCTM, R_P1_RFCTM_RDY, 0x0);
	}
#undef RTW8852B_TSSI_GET_VAL
}

static void _tssi_set_dac_gain_tbl(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
				   enum rtw89_rf_path path)
{
	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852b_tssi_dac_gain_defs_a_tbl,
				 &rtw8852b_tssi_dac_gain_defs_b_tbl);
}

static void _tssi_slope_cal_org(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
				enum rtw89_rf_path path)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	enum rtw89_band band = chan->band_type;

	if (path == RF_PATH_A)
		rtw89_rfk_parser_by_cond(rtwdev, band == RTW89_BAND_2G,
					 &rtw8852b_tssi_slope_a_defs_2g_tbl,
					 &rtw8852b_tssi_slope_a_defs_5g_tbl);
	else
		rtw89_rfk_parser_by_cond(rtwdev, band == RTW89_BAND_2G,
					 &rtw8852b_tssi_slope_b_defs_2g_tbl,
					 &rtw8852b_tssi_slope_b_defs_5g_tbl);
}

static void _tssi_alignment_default(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
				    enum rtw89_rf_path path, bool all)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	enum rtw89_band band = chan->band_type;
	const struct rtw89_rfk_tbl *tbl = NULL;
	u8 ch = chan->channel;

	if (path == RF_PATH_A) {
		if (band == RTW89_BAND_2G) {
			if (all)
				tbl = &rtw8852b_tssi_align_a_2g_all_defs_tbl;
			else
				tbl = &rtw8852b_tssi_align_a_2g_part_defs_tbl;
		} else if (ch >= 36 && ch <= 64) {
			if (all)
				tbl = &rtw8852b_tssi_align_a_5g1_all_defs_tbl;
			else
				tbl = &rtw8852b_tssi_align_a_5g1_part_defs_tbl;
		} else if (ch >= 100 && ch <= 144) {
			if (all)
				tbl = &rtw8852b_tssi_align_a_5g2_all_defs_tbl;
			else
				tbl = &rtw8852b_tssi_align_a_5g2_part_defs_tbl;
		} else if (ch >= 149 && ch <= 177) {
			if (all)
				tbl = &rtw8852b_tssi_align_a_5g3_all_defs_tbl;
			else
				tbl = &rtw8852b_tssi_align_a_5g3_part_defs_tbl;
		}
	} else {
		if (ch >= 1 && ch <= 14) {
			if (all)
				tbl = &rtw8852b_tssi_align_b_2g_all_defs_tbl;
			else
				tbl = &rtw8852b_tssi_align_b_2g_part_defs_tbl;
		} else if (ch >= 36 && ch <= 64) {
			if (all)
				tbl = &rtw8852b_tssi_align_b_5g1_all_defs_tbl;
			else
				tbl = &rtw8852b_tssi_align_b_5g1_part_defs_tbl;
		} else if (ch >= 100 && ch <= 144) {
			if (all)
				tbl = &rtw8852b_tssi_align_b_5g2_all_defs_tbl;
			else
				tbl = &rtw8852b_tssi_align_b_5g2_part_defs_tbl;
		} else if (ch >= 149 && ch <= 177) {
			if (all)
				tbl = &rtw8852b_tssi_align_b_5g3_all_defs_tbl;
			else
				tbl = &rtw8852b_tssi_align_b_5g3_part_defs_tbl;
		}
	}

	if (tbl)
		rtw89_rfk_parser(rtwdev, tbl);
}

static void _tssi_set_tssi_slope(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
				 enum rtw89_rf_path path)
{
	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852b_tssi_slope_defs_a_tbl,
				 &rtw8852b_tssi_slope_defs_b_tbl);
}

static void _tssi_set_tssi_track(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
				 enum rtw89_rf_path path)
{
	if (path == RF_PATH_A)
		rtw89_phy_write32_mask(rtwdev, R_P0_TSSIC, B_P0_TSSIC_BYPASS, 0x0);
	else
		rtw89_phy_write32_mask(rtwdev, R_P1_TSSIC, B_P1_TSSIC_BYPASS, 0x0);
}

static void _tssi_set_txagc_offset_mv_avg(struct rtw89_dev *rtwdev,
					  enum rtw89_phy_idx phy,
					  enum rtw89_rf_path path)
{
	rtw89_debug(rtwdev, RTW89_DBG_TSSI, "======>%s   path=%d\n", __func__,
		    path);

	if (path == RF_PATH_A)
		rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_MV_AVG, B_P0_TSSI_MV_MIX, 0x010);
	else
		rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_MV_AVG, B_P1_RFCTM_DEL, 0x010);
}

static void _tssi_enable(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy)
{
	u8 i;

	for (i = 0; i < RF_PATH_NUM_8852B; i++) {
		_tssi_set_tssi_track(rtwdev, phy, i);
		_tssi_set_txagc_offset_mv_avg(rtwdev, phy, i);

		if (i == RF_PATH_A) {
			rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_MV_AVG,
					       B_P0_TSSI_MV_CLR, 0x0);
			rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_AVG,
					       B_P0_TSSI_EN, 0x0);
			rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_AVG,
					       B_P0_TSSI_EN, 0x1);
			rtw89_write_rf(rtwdev, i, RR_TXGA_V1,
				       RR_TXGA_V1_TRK_EN, 0x1);
			rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_TRK,
					       B_P0_TSSI_RFC, 0x3);

			rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_TRK,
					       B_P0_TSSI_OFT, 0xc0);
			rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_TRK,
					       B_P0_TSSI_OFT_EN, 0x0);
			rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_TRK,
					       B_P0_TSSI_OFT_EN, 0x1);

			rtwdev->is_tssi_mode[RF_PATH_A] = true;
		} else {
			rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_MV_AVG,
					       B_P1_TSSI_MV_CLR, 0x0);
			rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_AVG,
					       B_P1_TSSI_EN, 0x0);
			rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_AVG,
					       B_P1_TSSI_EN, 0x1);
			rtw89_write_rf(rtwdev, i, RR_TXGA_V1,
				       RR_TXGA_V1_TRK_EN, 0x1);
			rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_TRK,
					       B_P1_TSSI_RFC, 0x3);

			rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_TRK,
					       B_P1_TSSI_OFT, 0xc0);
			rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_TRK,
					       B_P1_TSSI_OFT_EN, 0x0);
			rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_TRK,
					       B_P1_TSSI_OFT_EN, 0x1);

			rtwdev->is_tssi_mode[RF_PATH_B] = true;
		}
	}
}

static void _tssi_disable(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy)
{
	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_AVG, B_P0_TSSI_EN, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_TRK, B_P0_TSSI_RFC, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_MV_AVG, B_P0_TSSI_MV_CLR, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_AVG, B_P1_TSSI_EN, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_TRK, B_P1_TSSI_RFC, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_MV_AVG, B_P1_TSSI_MV_CLR, 0x1);

	rtwdev->is_tssi_mode[RF_PATH_A] = false;
	rtwdev->is_tssi_mode[RF_PATH_B] = false;
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
			    enum rtw89_rf_path path)
{
	struct rtw89_tssi_info *tssi_info = &rtwdev->tssi;
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	u8 ch = chan->channel;
	u32 gidx, gidx_1st, gidx_2nd;
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
				 enum rtw89_rf_path path)
{
	struct rtw89_tssi_info *tssi_info = &rtwdev->tssi;
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	u8 ch = chan->channel;
	u32 tgidx, tgidx_1st, tgidx_2nd;
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

static void _tssi_set_efuse_to_de(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy)
{
	struct rtw89_tssi_info *tssi_info = &rtwdev->tssi;
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	u8 ch = chan->channel;
	u8 gidx;
	s8 ofdm_de;
	s8 trim_de;
	s32 val;
	u32 i;

	rtw89_debug(rtwdev, RTW89_DBG_TSSI, "[TSSI][TRIM]: phy=%d ch=%d\n",
		    phy, ch);

	for (i = RF_PATH_A; i < RF_PATH_NUM_8852B; i++) {
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
				enum rtw89_phy_idx phy, enum rtw89_rf_path path)
{
	struct rtw89_tssi_info *tssi_info = &rtwdev->tssi;
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
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

static void _tssi_hw_tx(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			enum rtw89_rf_path path, u16 cnt, u16 period, s16 pwr_dbm,
			u8 enable)
{
	enum rtw89_rf_path_bit rx_path;

	if (path == RF_PATH_A)
		rx_path = RF_A;
	else if (path == RF_PATH_B)
		rx_path = RF_B;
	else if (path == RF_PATH_AB)
		rx_path = RF_AB;
	else
		rx_path = RF_ABCD; /* don't change path, but still set others */

	if (enable) {
		rtw8852b_bb_set_plcp_tx(rtwdev);
		rtw8852b_bb_cfg_tx_path(rtwdev, path);
		rtw8852b_bb_ctrl_rx_path(rtwdev, rx_path);
		rtw8852b_bb_set_power(rtwdev, pwr_dbm, phy);
	}

	rtw8852b_bb_set_pmac_pkt_tx(rtwdev, enable, cnt, period, 20, phy);
}

static void _tssi_backup_bb_registers(struct rtw89_dev *rtwdev,
				      enum rtw89_phy_idx phy, const u32 reg[],
				      u32 reg_backup[], u32 reg_num)
{
	u32 i;

	for (i = 0; i < reg_num; i++) {
		reg_backup[i] = rtw89_phy_read32_mask(rtwdev, reg[i], MASKDWORD);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[TSSI] Backup BB 0x%x = 0x%x\n", reg[i],
			    reg_backup[i]);
	}
}

static void _tssi_reload_bb_registers(struct rtw89_dev *rtwdev,
				      enum rtw89_phy_idx phy, const u32 reg[],
				      u32 reg_backup[], u32 reg_num)

{
	u32 i;

	for (i = 0; i < reg_num; i++) {
		rtw89_phy_write32_mask(rtwdev, reg[i], MASKDWORD, reg_backup[i]);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[TSSI] Reload BB 0x%x = 0x%x\n", reg[i],
			    reg_backup[i]);
	}
}

static u8 _tssi_ch_to_idx(struct rtw89_dev *rtwdev, u8 channel)
{
	u8 channel_index;

	if (channel >= 1 && channel <= 14)
		channel_index = channel - 1;
	else if (channel >= 36 && channel <= 64)
		channel_index = (channel - 36) / 2 + 14;
	else if (channel >= 100 && channel <= 144)
		channel_index = ((channel - 100) / 2) + 15 + 14;
	else if (channel >= 149 && channel <= 177)
		channel_index = ((channel - 149) / 2) + 38 + 14;
	else
		channel_index = 0;

	return channel_index;
}

static bool _tssi_get_cw_report(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
				enum rtw89_rf_path path, const s16 *power,
				u32 *tssi_cw_rpt)
{
	u32 tx_counter, tx_counter_tmp;
	const int retry = 100;
	u32 tmp;
	int j, k;

	for (j = 0; j < RTW8852B_TSSI_PATH_NR; j++) {
		rtw89_phy_write32_mask(rtwdev, _tssi_trigger[path], B_P0_TSSI_EN, 0x0);
		rtw89_phy_write32_mask(rtwdev, _tssi_trigger[path], B_P0_TSSI_EN, 0x1);

		tx_counter = rtw89_phy_read32_mask(rtwdev, R_TX_COUNTER, MASKLWORD);

		tmp = rtw89_phy_read32_mask(rtwdev, _tssi_trigger[path], MASKDWORD);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[TSSI PA K] 0x%x = 0x%08x   path=%d\n",
			    _tssi_trigger[path], tmp, path);

		if (j == 0)
			_tssi_hw_tx(rtwdev, phy, path, 100, 5000, power[j], true);
		else
			_tssi_hw_tx(rtwdev, phy, RF_PATH_ABCD, 100, 5000, power[j], true);

		tx_counter_tmp = rtw89_phy_read32_mask(rtwdev, R_TX_COUNTER, MASKLWORD);
		tx_counter_tmp -= tx_counter;

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[TSSI PA K] First HWTXcounter=%d path=%d\n",
			    tx_counter_tmp, path);

		for (k = 0; k < retry; k++) {
			tmp = rtw89_phy_read32_mask(rtwdev, _tssi_cw_rpt_addr[path],
						    B_TSSI_CWRPT_RDY);
			if (tmp)
				break;

			udelay(30);

			tx_counter_tmp =
				rtw89_phy_read32_mask(rtwdev, R_TX_COUNTER, MASKLWORD);
			tx_counter_tmp -= tx_counter;

			rtw89_debug(rtwdev, RTW89_DBG_RFK,
				    "[TSSI PA K] Flow k = %d HWTXcounter=%d path=%d\n",
				    k, tx_counter_tmp, path);
		}

		if (k >= retry) {
			rtw89_debug(rtwdev, RTW89_DBG_RFK,
				    "[TSSI PA K] TSSI finish bit k > %d mp:100ms normal:30us path=%d\n",
				    k, path);

			_tssi_hw_tx(rtwdev, phy, path, 100, 5000, power[j], false);
			return false;
		}

		tssi_cw_rpt[j] =
			rtw89_phy_read32_mask(rtwdev, _tssi_cw_rpt_addr[path], B_TSSI_CWRPT);

		_tssi_hw_tx(rtwdev, phy, path, 100, 5000, power[j], false);

		tx_counter_tmp = rtw89_phy_read32_mask(rtwdev, R_TX_COUNTER, MASKLWORD);
		tx_counter_tmp -= tx_counter;

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[TSSI PA K] Final HWTXcounter=%d path=%d\n",
			    tx_counter_tmp, path);
	}

	return true;
}

static void _tssi_alimentk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			   enum rtw89_rf_path path)
{
	static const u32 bb_reg[8] = {0x5820, 0x7820, 0x4978, 0x58e4,
				      0x78e4, 0x49c0, 0x0d18, 0x0d80};
	static const s16 power_2g[4] = {48, 20, 4, 4};
	static const s16 power_5g[4] = {48, 20, 4, 4};
	struct rtw89_tssi_info *tssi_info = &rtwdev->tssi;
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	s32 tssi_alim_offset_1, tssi_alim_offset_2, tssi_alim_offset_3;
	u32 tssi_cw_rpt[RTW8852B_TSSI_PATH_NR] = {0};
	u8 channel = chan->channel;
	u8 ch_idx = _tssi_ch_to_idx(rtwdev, channel);
	struct rtw8852b_bb_tssi_bak tssi_bak;
	s32 aliment_diff, tssi_cw_default;
	u32 start_time, finish_time;
	u32 bb_reg_backup[8] = {0};
	const s16 *power;
	u8 band;
	bool ok;
	u32 tmp;
	u8 j;

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "======> %s   channel=%d   path=%d\n", __func__, channel,
		    path);

	if (tssi_info->check_backup_aligmk[path][ch_idx]) {
		rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_ALIM1 + (path << 13), MASKDWORD,
				       tssi_info->alignment_backup_by_ch[path][ch_idx][0]);
		rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_ALIM3 + (path << 13), MASKDWORD,
				       tssi_info->alignment_backup_by_ch[path][ch_idx][1]);
		rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_ALIM2 + (path << 13), MASKDWORD,
				       tssi_info->alignment_backup_by_ch[path][ch_idx][2]);
		rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_ALIM4 + (path << 13), MASKDWORD,
				       tssi_info->alignment_backup_by_ch[path][ch_idx][3]);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "======> %s   Reload TSSI Alignment !!!\n", __func__);
		_tssi_alimentk_dump_result(rtwdev, path);
		return;
	}

	start_time = ktime_get_ns();

	if (chan->band_type == RTW89_BAND_2G)
		power = power_2g;
	else
		power = power_5g;

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

	rtw8852b_bb_backup_tssi(rtwdev, phy, &tssi_bak);
	_tssi_backup_bb_registers(rtwdev, phy, bb_reg, bb_reg_backup, ARRAY_SIZE(bb_reg_backup));

	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_AVG, B_P0_TSSI_AVG, 0x8);
	rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_AVG, B_P1_TSSI_AVG, 0x8);
	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_MV_AVG, B_P0_TSSI_MV_AVG, 0x2);
	rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_MV_AVG, B_P1_TSSI_MV_AVG, 0x2);

	ok = _tssi_get_cw_report(rtwdev, phy, path, power, tssi_cw_rpt);
	if (!ok)
		goto out;

	for (j = 0; j < RTW8852B_TSSI_PATH_NR; j++) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[TSSI PA K] power[%d]=%d  tssi_cw_rpt[%d]=%d\n", j,
			    power[j], j, tssi_cw_rpt[j]);
	}

	tmp = rtw89_phy_read32_mask(rtwdev, _tssi_cw_default_addr[path][1],
				    _tssi_cw_default_mask[1]);
	tssi_cw_default = sign_extend32(tmp, 8);
	tssi_alim_offset_1 = tssi_cw_rpt[0] - ((power[0] - power[1]) * 2) -
			     tssi_cw_rpt[1] + tssi_cw_default;
	aliment_diff = tssi_alim_offset_1 - tssi_cw_default;

	tmp = rtw89_phy_read32_mask(rtwdev, _tssi_cw_default_addr[path][2],
				    _tssi_cw_default_mask[2]);
	tssi_cw_default = sign_extend32(tmp, 8);
	tssi_alim_offset_2 = tssi_cw_default + aliment_diff;

	tmp = rtw89_phy_read32_mask(rtwdev, _tssi_cw_default_addr[path][3],
				    _tssi_cw_default_mask[3]);
	tssi_cw_default = sign_extend32(tmp, 8);
	tssi_alim_offset_3 = tssi_cw_default + aliment_diff;

	if (path == RF_PATH_A) {
		tmp = FIELD_PREP(B_P1_TSSI_ALIM11, tssi_alim_offset_1) |
		      FIELD_PREP(B_P1_TSSI_ALIM12, tssi_alim_offset_2) |
		      FIELD_PREP(B_P1_TSSI_ALIM13, tssi_alim_offset_3);

		rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_ALIM1, B_P0_TSSI_ALIM1, tmp);
		rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_ALIM2, B_P0_TSSI_ALIM2, tmp);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[TSSI PA K] tssi_alim_offset = 0x%x   0x%x   0x%x   0x%x\n",
			    rtw89_phy_read32_mask(rtwdev, R_P0_TSSI_ALIM3, B_P0_TSSI_ALIM31),
			    rtw89_phy_read32_mask(rtwdev, R_P0_TSSI_ALIM1, B_P0_TSSI_ALIM11),
			    rtw89_phy_read32_mask(rtwdev, R_P0_TSSI_ALIM1, B_P0_TSSI_ALIM12),
			    rtw89_phy_read32_mask(rtwdev, R_P0_TSSI_ALIM1, B_P0_TSSI_ALIM13));
	} else {
		tmp = FIELD_PREP(B_P1_TSSI_ALIM11, tssi_alim_offset_1) |
		      FIELD_PREP(B_P1_TSSI_ALIM12, tssi_alim_offset_2) |
		      FIELD_PREP(B_P1_TSSI_ALIM13, tssi_alim_offset_3);

		rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_ALIM1, B_P1_TSSI_ALIM1, tmp);
		rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_ALIM2, B_P1_TSSI_ALIM2, tmp);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[TSSI PA K] tssi_alim_offset = 0x%x   0x%x   0x%x   0x%x\n",
			    rtw89_phy_read32_mask(rtwdev, R_P1_TSSI_ALIM3, B_P1_TSSI_ALIM31),
			    rtw89_phy_read32_mask(rtwdev, R_P1_TSSI_ALIM1, B_P1_TSSI_ALIM11),
			    rtw89_phy_read32_mask(rtwdev, R_P1_TSSI_ALIM1, B_P1_TSSI_ALIM12),
			    rtw89_phy_read32_mask(rtwdev, R_P1_TSSI_ALIM1, B_P1_TSSI_ALIM13));
	}

	tssi_info->alignment_done[path][band] = true;
	tssi_info->alignment_value[path][band][0] =
		rtw89_phy_read32_mask(rtwdev, R_P0_TSSI_ALIM1 + (path << 13), MASKDWORD);
	tssi_info->alignment_value[path][band][1] =
		rtw89_phy_read32_mask(rtwdev, R_P0_TSSI_ALIM3 + (path << 13), MASKDWORD);
	tssi_info->alignment_value[path][band][2] =
		rtw89_phy_read32_mask(rtwdev, R_P0_TSSI_ALIM2 + (path << 13), MASKDWORD);
	tssi_info->alignment_value[path][band][3] =
		rtw89_phy_read32_mask(rtwdev, R_P0_TSSI_ALIM4 + (path << 13), MASKDWORD);

	tssi_info->check_backup_aligmk[path][ch_idx] = true;
	tssi_info->alignment_backup_by_ch[path][ch_idx][0] =
		rtw89_phy_read32_mask(rtwdev, R_P0_TSSI_ALIM1 + (path << 13), MASKDWORD);
	tssi_info->alignment_backup_by_ch[path][ch_idx][1] =
		rtw89_phy_read32_mask(rtwdev, R_P0_TSSI_ALIM3 + (path << 13), MASKDWORD);
	tssi_info->alignment_backup_by_ch[path][ch_idx][2] =
		rtw89_phy_read32_mask(rtwdev, R_P0_TSSI_ALIM2 + (path << 13), MASKDWORD);
	tssi_info->alignment_backup_by_ch[path][ch_idx][3] =
		rtw89_phy_read32_mask(rtwdev, R_P0_TSSI_ALIM4 + (path << 13), MASKDWORD);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[TSSI PA K] tssi_info->alignment_value[path=%d][band=%d][0], 0x%x = 0x%08x\n",
		    path, band, R_P0_TSSI_ALIM1 + (path << 13),
		    tssi_info->alignment_value[path][band][0]);
	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[TSSI PA K] tssi_info->alignment_value[path=%d][band=%d][1], 0x%x = 0x%08x\n",
		    path, band, R_P0_TSSI_ALIM3 + (path << 13),
		    tssi_info->alignment_value[path][band][1]);
	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[TSSI PA K] tssi_info->alignment_value[path=%d][band=%d][2], 0x%x = 0x%08x\n",
		    path, band, R_P0_TSSI_ALIM2 + (path << 13),
		    tssi_info->alignment_value[path][band][2]);
	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[TSSI PA K] tssi_info->alignment_value[path=%d][band=%d][3], 0x%x = 0x%08x\n",
		    path, band, R_P0_TSSI_ALIM4 + (path << 13),
		    tssi_info->alignment_value[path][band][3]);

out:
	_tssi_reload_bb_registers(rtwdev, phy, bb_reg, bb_reg_backup, ARRAY_SIZE(bb_reg_backup));
	rtw8852b_bb_restore_tssi(rtwdev, phy, &tssi_bak);
	rtw8852b_bb_tx_mode_switch(rtwdev, phy, 0);

	finish_time = ktime_get_ns();
	tssi_info->tssi_alimk_time += finish_time - start_time;

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[TSSI PA K] %s processing time = %d ms\n", __func__,
		    tssi_info->tssi_alimk_time);
}

void rtw8852b_rck(struct rtw89_dev *rtwdev)
{
	u8 path;

	for (path = 0; path < RF_PATH_NUM_8852B; path++)
		_rck(rtwdev, path);
}

void rtw8852b_dack(struct rtw89_dev *rtwdev)
{
	u8 phy_map = rtw89_btc_phymap(rtwdev, RTW89_PHY_0, 0);

	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_DACK, BTC_WRFK_START);
	_dac_cal(rtwdev, false);
	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_DACK, BTC_WRFK_STOP);
}

void rtw8852b_iqk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx)
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

void rtw8852b_rx_dck(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx)
{
	u8 phy_map = rtw89_btc_phymap(rtwdev, phy_idx, 0);
	u32 tx_en;

	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_RXDCK, BTC_WRFK_START);
	rtw89_chip_stop_sch_tx(rtwdev, phy_idx, &tx_en, RTW89_SCH_TX_SEL_ALL);
	_wait_rx_mode(rtwdev, _kpath(rtwdev, phy_idx));

	_rx_dck(rtwdev, phy_idx);

	rtw89_chip_resume_sch_tx(rtwdev, phy_idx, tx_en);
	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_RXDCK, BTC_WRFK_STOP);
}

void rtw8852b_tssi(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy, bool hwtx_en)
{
	u8 phy_map = rtw89_btc_phymap(rtwdev, phy, RF_AB);
	u32 tx_en;
	u8 i;

	rtw89_debug(rtwdev, RTW89_DBG_TSSI, "[TSSI] %s: phy=%d\n", __func__, phy);
	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_IQK, BTC_WRFK_ONESHOT_START);

	_tssi_disable(rtwdev, phy);

	for (i = RF_PATH_A; i < RF_PATH_NUM_8852B; i++) {
		_tssi_rf_setting(rtwdev, phy, i);
		_tssi_set_sys(rtwdev, phy, i);
		_tssi_ini_txpwr_ctrl_bb(rtwdev, phy, i);
		_tssi_ini_txpwr_ctrl_bb_he_tb(rtwdev, phy, i);
		_tssi_set_dck(rtwdev, phy, i);
		_tssi_set_tmeter_tbl(rtwdev, phy, i);
		_tssi_set_dac_gain_tbl(rtwdev, phy, i);
		_tssi_slope_cal_org(rtwdev, phy, i);
		_tssi_alignment_default(rtwdev, phy, i, true);
		_tssi_set_tssi_slope(rtwdev, phy, i);

		rtw89_chip_stop_sch_tx(rtwdev, phy, &tx_en, RTW89_SCH_TX_SEL_ALL);
		_tmac_tx_pause(rtwdev, phy, true);
		if (hwtx_en)
			_tssi_alimentk(rtwdev, phy, i);
		_tmac_tx_pause(rtwdev, phy, false);
		rtw89_chip_resume_sch_tx(rtwdev, phy, tx_en);
	}

	_tssi_enable(rtwdev, phy);
	_tssi_set_efuse_to_de(rtwdev, phy);

	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_IQK, BTC_WRFK_ONESHOT_STOP);
}

void rtw8852b_tssi_scan(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	struct rtw89_tssi_info *tssi_info = &rtwdev->tssi;
	u8 channel = chan->channel;
	u8 band;
	u32 i;

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "======>%s   phy=%d  channel=%d\n", __func__, phy, channel);

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

	_tssi_disable(rtwdev, phy);

	for (i = RF_PATH_A; i < RTW8852B_TSSI_PATH_NR; i++) {
		_tssi_rf_setting(rtwdev, phy, i);
		_tssi_set_sys(rtwdev, phy, i);
		_tssi_set_tmeter_tbl(rtwdev, phy, i);

		if (tssi_info->alignment_done[i][band])
			_tssi_alimentk_done(rtwdev, phy, i);
		else
			_tssi_alignment_default(rtwdev, phy, i, true);
	}

	_tssi_enable(rtwdev, phy);
	_tssi_set_efuse_to_de(rtwdev, phy);
}

static void rtw8852b_tssi_default_txagc(struct rtw89_dev *rtwdev,
					enum rtw89_phy_idx phy, bool enable)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	u8 channel = chan->channel;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "======> %s   ch=%d\n",
		    __func__, channel);

	if (enable) {
		if (!rtwdev->is_tssi_mode[RF_PATH_A] && !rtwdev->is_tssi_mode[RF_PATH_B])
			rtw8852b_tssi(rtwdev, phy, true);
		return;
	}

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "======>%s 1 SCAN_END Set 0x5818[7:0]=0x%x 0x7818[7:0]=0x%x\n",
		    __func__,
		    rtw89_phy_read32_mask(rtwdev, R_P0_TSSI_TRK, B_P0_TSSI_OFT),
		    rtw89_phy_read32_mask(rtwdev, R_P1_TSSI_TRK, B_P1_TSSI_OFT));

	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_TRK, B_P0_TSSI_OFT, 0xc0);
	rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_TRK, B_P1_TSSI_OFT,  0xc0);
	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_TRK, B_P0_TSSI_OFT_EN, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_TRK, B_P0_TSSI_OFT_EN, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_TRK, B_P1_TSSI_OFT_EN, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_TRK, B_P1_TSSI_OFT_EN, 0x1);

	_tssi_alimentk_done(rtwdev, phy, RF_PATH_A);
	_tssi_alimentk_done(rtwdev, phy, RF_PATH_B);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "======>%s 2 SCAN_END Set 0x5818[7:0]=0x%x 0x7818[7:0]=0x%x\n",
		    __func__,
		    rtw89_phy_read32_mask(rtwdev, R_P0_TSSI_TRK, B_P0_TSSI_OFT),
		    rtw89_phy_read32_mask(rtwdev, R_P1_TSSI_TRK, B_P1_TSSI_OFT));

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "======> %s   SCAN_END\n", __func__);
}

void rtw8852b_wifi_scan_notify(struct rtw89_dev *rtwdev, bool scan_start,
			       enum rtw89_phy_idx phy_idx)
{
	if (scan_start)
		rtw8852b_tssi_default_txagc(rtwdev, phy_idx, true);
	else
		rtw8852b_tssi_default_txagc(rtwdev, phy_idx, false);
}

static void _bw_setting(struct rtw89_dev *rtwdev, enum rtw89_rf_path path,
			enum rtw89_bandwidth bw, bool dav)
{
	u32 rf_reg18;
	u32 reg18_addr = dav ? RR_CFGCH : RR_CFGCH_V1;

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
	_bw_setting(rtwdev, RF_PATH_B, bw, true);
	_bw_setting(rtwdev, RF_PATH_A, bw, false);
	_bw_setting(rtwdev, RF_PATH_B, bw, false);
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
	_ch_setting(rtwdev, RF_PATH_B, central_ch, true);
	_ch_setting(rtwdev, RF_PATH_A, central_ch, false);
	_ch_setting(rtwdev, RF_PATH_B, central_ch, false);
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

	for (path = 0; path < RF_PATH_NUM_8852B; path++) {
		if (!(kpath & BIT(path)))
			continue;

		_set_rxbb_bw(rtwdev, bw, path);
	}
}

static void rtw8852b_ctrl_bw_ch(struct rtw89_dev *rtwdev,
				enum rtw89_phy_idx phy, u8 central_ch,
				enum rtw89_band band, enum rtw89_bandwidth bw)
{
	_ctrl_ch(rtwdev, central_ch);
	_ctrl_bw(rtwdev, phy, bw);
	_rxbb_bw(rtwdev, phy, bw);
}

void rtw8852b_set_channel_rf(struct rtw89_dev *rtwdev,
			     const struct rtw89_chan *chan,
			     enum rtw89_phy_idx phy_idx)
{
	rtw8852b_ctrl_bw_ch(rtwdev, phy_idx, chan->channel, chan->band_type,
			    chan->band_width);
}
