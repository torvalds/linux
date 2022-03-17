// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#include "coex.h"
#include "debug.h"
#include "mac.h"
#include "phy.h"
#include "reg.h"
#include "rtw8852a.h"
#include "rtw8852a_rfk.h"
#include "rtw8852a_rfk_table.h"
#include "rtw8852a_table.h"

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

static const u32 rtw8852a_backup_bb_regs[] = {0x2344, 0x58f0, 0x78f0};
static const u32 rtw8852a_backup_rf_regs[] = {0xef, 0xde, 0x0, 0x1e, 0x2, 0x85, 0x90, 0x5};
#define BACKUP_BB_REGS_NR ARRAY_SIZE(rtw8852a_backup_bb_regs)
#define BACKUP_RF_REGS_NR ARRAY_SIZE(rtw8852a_backup_rf_regs)

static void _rfk_backup_bb_reg(struct rtw89_dev *rtwdev, u32 backup_bb_reg_val[])
{
	u32 i;

	for (i = 0; i < BACKUP_BB_REGS_NR; i++) {
		backup_bb_reg_val[i] =
			rtw89_phy_read32_mask(rtwdev, rtw8852a_backup_bb_regs[i],
					      MASKDWORD);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK]backup bb reg : %x, value =%x\n",
			    rtw8852a_backup_bb_regs[i], backup_bb_reg_val[i]);
	}
}

static void _rfk_backup_rf_reg(struct rtw89_dev *rtwdev, u32 backup_rf_reg_val[],
			       u8 rf_path)
{
	u32 i;

	for (i = 0; i < BACKUP_RF_REGS_NR; i++) {
		backup_rf_reg_val[i] =
			rtw89_read_rf(rtwdev, rf_path,
				      rtw8852a_backup_rf_regs[i], RFREG_MASK);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK]backup rf S%d reg : %x, value =%x\n", rf_path,
			    rtw8852a_backup_rf_regs[i], backup_rf_reg_val[i]);
	}
}

static void _rfk_restore_bb_reg(struct rtw89_dev *rtwdev,
				u32 backup_bb_reg_val[])
{
	u32 i;

	for (i = 0; i < BACKUP_BB_REGS_NR; i++) {
		rtw89_phy_write32_mask(rtwdev, rtw8852a_backup_bb_regs[i],
				       MASKDWORD, backup_bb_reg_val[i]);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK]restore bb reg : %x, value =%x\n",
			    rtw8852a_backup_bb_regs[i], backup_bb_reg_val[i]);
	}
}

static void _rfk_restore_rf_reg(struct rtw89_dev *rtwdev,
				u32 backup_rf_reg_val[], u8 rf_path)
{
	u32 i;

	for (i = 0; i < BACKUP_RF_REGS_NR; i++) {
		rtw89_write_rf(rtwdev, rf_path, rtw8852a_backup_rf_regs[i],
			       RFREG_MASK, backup_rf_reg_val[i]);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK]restore rf S%d reg: %x, value =%x\n", rf_path,
			    rtw8852a_backup_rf_regs[i], backup_rf_reg_val[i]);
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

static void _afe_init(struct rtw89_dev *rtwdev)
{
	rtw89_rfk_parser(rtwdev, &rtw8852a_rfk_afe_init_defs_tbl);
}

static void _addck_backup(struct rtw89_dev *rtwdev)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;

	rtw89_phy_write32_clr(rtwdev, R_S0_RXDC2, B_S0_RXDC2_SEL);
	dack->addck_d[0][0] = (u16)rtw89_phy_read32_mask(rtwdev, R_S0_ADDCK,
							 B_S0_ADDCK_Q);
	dack->addck_d[0][1] = (u16)rtw89_phy_read32_mask(rtwdev, R_S0_ADDCK,
							 B_S0_ADDCK_I);

	rtw89_phy_write32_clr(rtwdev, R_S1_RXDC2, B_S1_RXDC2_SEL);
	dack->addck_d[1][0] = (u16)rtw89_phy_read32_mask(rtwdev, R_S1_ADDCK,
							 B_S1_ADDCK_Q);
	dack->addck_d[1][1] = (u16)rtw89_phy_read32_mask(rtwdev, R_S1_ADDCK,
							 B_S1_ADDCK_I);
}

static void _addck_reload(struct rtw89_dev *rtwdev)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;

	rtw89_phy_write32_mask(rtwdev, R_S0_RXDC, B_S0_RXDC_I, dack->addck_d[0][0]);
	rtw89_phy_write32_mask(rtwdev, R_S0_RXDC2, B_S0_RXDC2_Q2,
			       (dack->addck_d[0][1] >> 6));
	rtw89_phy_write32_mask(rtwdev, R_S0_RXDC, B_S0_RXDC_Q,
			       (dack->addck_d[0][1] & 0x3f));
	rtw89_phy_write32_set(rtwdev, R_S0_RXDC2, B_S0_RXDC2_MEN);
	rtw89_phy_write32_mask(rtwdev, R_S1_RXDC, B_S1_RXDC_I, dack->addck_d[1][0]);
	rtw89_phy_write32_mask(rtwdev, R_S1_RXDC2, B_S1_RXDC2_Q2,
			       (dack->addck_d[1][1] >> 6));
	rtw89_phy_write32_mask(rtwdev, R_S1_RXDC, B_S1_RXDC_Q,
			       (dack->addck_d[1][1] & 0x3f));
	rtw89_phy_write32_set(rtwdev, R_S1_RXDC2, B_S1_RXDC2_EN);
}

static void _dack_backup_s0(struct rtw89_dev *rtwdev)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;
	u8 i;

	rtw89_phy_write32_set(rtwdev, R_S0_DACKI, B_S0_DACKI_EN);
	rtw89_phy_write32_set(rtwdev, R_S0_DACKQ, B_S0_DACKQ_EN);
	rtw89_phy_write32_set(rtwdev, R_P0_NRBW, B_P0_NRBW_DBG);

	for (i = 0; i < RTW89_DACK_MSBK_NR; i++) {
		rtw89_phy_write32_mask(rtwdev, R_S0_DACKI, B_S0_DACKI_AR, i);
		dack->msbk_d[0][0][i] =
			(u8)rtw89_phy_read32_mask(rtwdev, R_S0_DACKI7, B_S0_DACKI7_K);
		rtw89_phy_write32_mask(rtwdev, R_S0_DACKQ, B_S0_DACKQ_AR, i);
		dack->msbk_d[0][1][i] =
			(u8)rtw89_phy_read32_mask(rtwdev, R_S0_DACKQ7, B_S0_DACKQ7_K);
	}
	dack->biask_d[0][0] = (u16)rtw89_phy_read32_mask(rtwdev, R_S0_DACKI2,
							 B_S0_DACKI2_K);
	dack->biask_d[0][1] = (u16)rtw89_phy_read32_mask(rtwdev, R_S0_DACKQ2,
							 B_S0_DACKQ2_K);
	dack->dadck_d[0][0] = (u8)rtw89_phy_read32_mask(rtwdev, R_S0_DACKI8,
							B_S0_DACKI8_K) - 8;
	dack->dadck_d[0][1] = (u8)rtw89_phy_read32_mask(rtwdev, R_S0_DACKQ8,
							B_S0_DACKQ8_K) - 8;
}

static void _dack_backup_s1(struct rtw89_dev *rtwdev)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;
	u8 i;

	rtw89_phy_write32_set(rtwdev, R_S1_DACKI, B_S1_DACKI_EN);
	rtw89_phy_write32_set(rtwdev, R_S1_DACKQ, B_S1_DACKQ_EN);
	rtw89_phy_write32_set(rtwdev, R_P1_DBGMOD, B_P1_DBGMOD_ON);

	for (i = 0; i < RTW89_DACK_MSBK_NR; i++) {
		rtw89_phy_write32_mask(rtwdev, R_S1_DACKI, B_S1_DACKI_AR, i);
		dack->msbk_d[1][0][i] =
			(u8)rtw89_phy_read32_mask(rtwdev, R_S1_DACKI7, B_S1_DACKI_K);
		rtw89_phy_write32_mask(rtwdev, R_S1_DACKQ, B_S1_DACKQ_AR, i);
		dack->msbk_d[1][1][i] =
			(u8)rtw89_phy_read32_mask(rtwdev, R_S1_DACKQ7, B_S1_DACKQ7_K);
	}
	dack->biask_d[1][0] =
		(u16)rtw89_phy_read32_mask(rtwdev, R_S1_DACKI2, B_S1_DACKI2_K);
	dack->biask_d[1][1] =
		(u16)rtw89_phy_read32_mask(rtwdev, R_S1_DACKQ2, B_S1_DACKQ2_K);
	dack->dadck_d[1][0] =
		(u8)rtw89_phy_read32_mask(rtwdev, R_S1_DACKI8, B_S1_DACKI8_K) - 8;
	dack->dadck_d[1][1] =
		(u8)rtw89_phy_read32_mask(rtwdev, R_S1_DACKQ8, B_S1_DACKQ8_K) - 8;
}

static void _dack_reload_by_path(struct rtw89_dev *rtwdev,
				 enum rtw89_rf_path path, u8 index)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;
	u32 tmp = 0, tmp_offset, tmp_reg;
	u8 i;
	u32 idx_offset, path_offset;

	if (index == 0)
		idx_offset = 0;
	else
		idx_offset = 0x50;

	if (path == RF_PATH_A)
		path_offset = 0;
	else
		path_offset = 0x2000;

	tmp_offset = idx_offset + path_offset;
	/* msbk_d: 15/14/13/12 */
	tmp = 0x0;
	for (i = 0; i < RTW89_DACK_MSBK_NR / 4; i++)
		tmp |= dack->msbk_d[path][index][i + 12] << (i * 8);
	tmp_reg = 0x5e14 + tmp_offset;
	rtw89_phy_write32(rtwdev, tmp_reg, tmp);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]0x%x=0x%x\n", tmp_reg,
		    rtw89_phy_read32_mask(rtwdev, tmp_reg, MASKDWORD));
	/* msbk_d: 11/10/9/8 */
	tmp = 0x0;
	for (i = 0; i < RTW89_DACK_MSBK_NR / 4; i++)
		tmp |= dack->msbk_d[path][index][i + 8] << (i * 8);
	tmp_reg = 0x5e18 + tmp_offset;
	rtw89_phy_write32(rtwdev, tmp_reg, tmp);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]0x%x=0x%x\n", tmp_reg,
		    rtw89_phy_read32_mask(rtwdev, tmp_reg, MASKDWORD));
	/* msbk_d: 7/6/5/4 */
	tmp = 0x0;
	for (i = 0; i < RTW89_DACK_MSBK_NR / 4; i++)
		tmp |= dack->msbk_d[path][index][i + 4] << (i * 8);
	tmp_reg = 0x5e1c + tmp_offset;
	rtw89_phy_write32(rtwdev, tmp_reg, tmp);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]0x%x=0x%x\n", tmp_reg,
		    rtw89_phy_read32_mask(rtwdev, tmp_reg, MASKDWORD));
	/* msbk_d: 3/2/1/0 */
	tmp = 0x0;
	for (i = 0; i < RTW89_DACK_MSBK_NR / 4; i++)
		tmp |= dack->msbk_d[path][index][i] << (i * 8);
	tmp_reg = 0x5e20 + tmp_offset;
	rtw89_phy_write32(rtwdev, tmp_reg, tmp);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]0x%x=0x%x\n", tmp_reg,
		    rtw89_phy_read32_mask(rtwdev, tmp_reg, MASKDWORD));
	/* dadak_d/biask_d */
	tmp = 0x0;
	tmp = (dack->biask_d[path][index] << 22) |
	       (dack->dadck_d[path][index] << 14);
	tmp_reg = 0x5e24 + tmp_offset;
	rtw89_phy_write32(rtwdev, tmp_reg, tmp);
}

static void _dack_reload(struct rtw89_dev *rtwdev, enum rtw89_rf_path path)
{
	u8 i;

	for (i = 0; i < 2; i++)
		_dack_reload_by_path(rtwdev, path, i);

	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852a_rfk_dack_reload_defs_a_tbl,
				 &rtw8852a_rfk_dack_reload_defs_b_tbl);
}

#define ADDC_T_AVG 100
static void _check_addc(struct rtw89_dev *rtwdev, enum rtw89_rf_path path)
{
	s32 dc_re = 0, dc_im = 0;
	u32 tmp;
	u32 i;

	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852a_rfk_check_addc_defs_a_tbl,
				 &rtw8852a_rfk_check_addc_defs_b_tbl);

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
	rtw89_rfk_parser(rtwdev, &rtw8852a_rfk_addck_reset_defs_a_tbl);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]before S0 ADDCK\n");
	_check_addc(rtwdev, RF_PATH_A);

	rtw89_rfk_parser(rtwdev, &rtw8852a_rfk_addck_trigger_defs_a_tbl);

	ret = read_poll_timeout_atomic(rtw89_phy_read32_mask, val, val, 1, 10000,
				       false, rtwdev, 0x1e00, BIT(0));
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S0 ADDCK timeout\n");
		dack->addck_timeout[0] = true;
	}
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]ADDCK ret = %d\n", ret);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]after S0 ADDCK\n");
	_check_addc(rtwdev, RF_PATH_A);

	rtw89_rfk_parser(rtwdev, &rtw8852a_rfk_addck_restore_defs_a_tbl);

	/* S1 */
	rtw89_rfk_parser(rtwdev, &rtw8852a_rfk_addck_reset_defs_b_tbl);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]before S1 ADDCK\n");
	_check_addc(rtwdev, RF_PATH_B);

	rtw89_rfk_parser(rtwdev, &rtw8852a_rfk_addck_trigger_defs_b_tbl);

	ret = read_poll_timeout_atomic(rtw89_phy_read32_mask, val, val, 1, 10000,
				       false, rtwdev, 0x3e00, BIT(0));
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S1 ADDCK timeout\n");
		dack->addck_timeout[1] = true;
	}
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]ADDCK ret = %d\n", ret);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]after S1 ADDCK\n");
	_check_addc(rtwdev, RF_PATH_B);

	rtw89_rfk_parser(rtwdev, &rtw8852a_rfk_addck_restore_defs_b_tbl);
}

static void _check_dadc(struct rtw89_dev *rtwdev, enum rtw89_rf_path path)
{
	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852a_rfk_check_dadc_defs_f_a_tbl,
				 &rtw8852a_rfk_check_dadc_defs_f_b_tbl);

	_check_addc(rtwdev, path);

	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852a_rfk_check_dadc_defs_r_a_tbl,
				 &rtw8852a_rfk_check_dadc_defs_r_b_tbl);
}

static void _dack_s0(struct rtw89_dev *rtwdev)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;
	u32 val;
	int ret;

	rtw89_rfk_parser(rtwdev, &rtw8852a_rfk_dack_defs_f_a_tbl);

	ret = read_poll_timeout_atomic(rtw89_phy_read32_mask, val, val, 1, 10000,
				       false, rtwdev, 0x5e28, BIT(15));
	ret |= read_poll_timeout_atomic(rtw89_phy_read32_mask, val, val, 1, 10000,
					false, rtwdev, 0x5e78, BIT(15));
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S0 MSBK timeout\n");
		dack->msbk_timeout[0] = true;
	}
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]DACK ret = %d\n", ret);

	rtw89_rfk_parser(rtwdev, &rtw8852a_rfk_dack_defs_m_a_tbl);

	ret = read_poll_timeout_atomic(rtw89_phy_read32_mask, val, val, 1, 10000,
				       false, rtwdev, 0x5e48, BIT(17));
	ret |= read_poll_timeout_atomic(rtw89_phy_read32_mask, val, val, 1, 10000,
					false, rtwdev, 0x5e98, BIT(17));
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S0 DADACK timeout\n");
		dack->dadck_timeout[0] = true;
	}
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]DACK ret = %d\n", ret);

	rtw89_rfk_parser(rtwdev, &rtw8852a_rfk_dack_defs_r_a_tbl);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]after S0 DADCK\n");
	_check_dadc(rtwdev, RF_PATH_A);

	_dack_backup_s0(rtwdev);
	_dack_reload(rtwdev, RF_PATH_A);

	rtw89_phy_write32_clr(rtwdev, R_P0_NRBW, B_P0_NRBW_DBG);
}

static void _dack_s1(struct rtw89_dev *rtwdev)
{
	struct rtw89_dack_info *dack = &rtwdev->dack;
	u32 val;
	int ret;

	rtw89_rfk_parser(rtwdev, &rtw8852a_rfk_dack_defs_f_b_tbl);

	ret = read_poll_timeout_atomic(rtw89_phy_read32_mask, val, val, 1, 10000,
				       false, rtwdev, 0x7e28, BIT(15));
	ret |= read_poll_timeout_atomic(rtw89_phy_read32_mask, val, val, 1, 10000,
					false, rtwdev, 0x7e78, BIT(15));
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S1 MSBK timeout\n");
		dack->msbk_timeout[1] = true;
	}
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]DACK ret = %d\n", ret);

	rtw89_rfk_parser(rtwdev, &rtw8852a_rfk_dack_defs_m_b_tbl);

	ret = read_poll_timeout_atomic(rtw89_phy_read32_mask, val, val, 1, 10000,
				       false, rtwdev, 0x7e48, BIT(17));
	ret |= read_poll_timeout_atomic(rtw89_phy_read32_mask, val, val, 1, 10000,
					false, rtwdev, 0x7e98, BIT(17));
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]S1 DADCK timeout\n");
		dack->dadck_timeout[1] = true;
	}
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]DACK ret = %d\n", ret);

	rtw89_rfk_parser(rtwdev, &rtw8852a_rfk_dack_defs_r_b_tbl);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DACK]after S1 DADCK\n");
	_check_dadc(rtwdev, RF_PATH_B);

	_dack_backup_s1(rtwdev);
	_dack_reload(rtwdev, RF_PATH_B);

	rtw89_phy_write32_clr(rtwdev, R_P1_DBGMOD, B_P1_DBGMOD_ON);
}

static void _dack(struct rtw89_dev *rtwdev)
{
	_dack_s0(rtwdev);
	_dack_s1(rtwdev);
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
	_afe_init(rtwdev);
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_RSV1, RR_RSV1_RST, 0x0);
	rtw89_write_rf(rtwdev, RF_PATH_B, RR_RSV1, RR_RSV1_RST, 0x0);
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_MOD, RFREG_MASK, 0x30001);
	rtw89_write_rf(rtwdev, RF_PATH_B, RR_MOD, RFREG_MASK, 0x30001);
	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_DACK, BTC_WRFK_ONESHOT_START);
	_addck(rtwdev);
	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_DACK, BTC_WRFK_ONESHOT_STOP);
	_addck_backup(rtwdev);
	_addck_reload(rtwdev);
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_MOD, RFREG_MASK, 0x40001);
	rtw89_write_rf(rtwdev, RF_PATH_B, RR_MOD, RFREG_MASK, 0x40001);
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

#define RTW8852A_NCTL_VER 0xd
#define RTW8852A_IQK_VER 0x2a
#define RTW8852A_IQK_SS 2
#define RTW8852A_IQK_THR_REK 8
#define RTW8852A_IQK_CFIR_GROUP_NR 4

enum rtw8852a_iqk_type {
	ID_TXAGC,
	ID_FLOK_COARSE,
	ID_FLOK_FINE,
	ID_TXK,
	ID_RXAGC,
	ID_RXK,
	ID_NBTXK,
	ID_NBRXK,
};

static void _iqk_read_fft_dbcc0(struct rtw89_dev *rtwdev, u8 path)
{
	u8 i = 0x0;
	u32 fft[6] = {0x0};

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]===>%s\n", __func__);
	rtw89_phy_write32_mask(rtwdev, R_KIP_RPT1, MASKDWORD, 0x00160000);
	fft[0] = rtw89_phy_read32_mask(rtwdev, R_RPT_COM, MASKDWORD);
	rtw89_phy_write32_mask(rtwdev, R_KIP_RPT1, MASKDWORD, 0x00170000);
	fft[1] = rtw89_phy_read32_mask(rtwdev, R_RPT_COM, MASKDWORD);
	rtw89_phy_write32_mask(rtwdev, R_KIP_RPT1, MASKDWORD, 0x00180000);
	fft[2] = rtw89_phy_read32_mask(rtwdev, R_RPT_COM, MASKDWORD);
	rtw89_phy_write32_mask(rtwdev, R_KIP_RPT1, MASKDWORD, 0x00190000);
	fft[3] = rtw89_phy_read32_mask(rtwdev, R_RPT_COM, MASKDWORD);
	rtw89_phy_write32_mask(rtwdev, R_KIP_RPT1, MASKDWORD, 0x001a0000);
	fft[4] = rtw89_phy_read32_mask(rtwdev, R_RPT_COM, MASKDWORD);
	rtw89_phy_write32_mask(rtwdev, R_KIP_RPT1, MASKDWORD, 0x001b0000);
	fft[5] = rtw89_phy_read32_mask(rtwdev, R_RPT_COM, MASKDWORD);
	for (i = 0; i < 6; i++)
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x,fft[%x]= %x\n",
			    path, i, fft[i]);
}

static void _iqk_read_xym_dbcc0(struct rtw89_dev *rtwdev, u8 path)
{
	u8 i = 0x0;
	u32 tmp = 0x0;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]===>%s\n", __func__);
	rtw89_phy_write32_mask(rtwdev, R_NCTL_CFG, B_NCTL_CFG_SPAGE, path);
	rtw89_phy_write32_mask(rtwdev, R_IQK_DIF, B_IQK_DIF_TRX, 0x1);

	for (i = 0x0; i < 0x18; i++) {
		rtw89_phy_write32_mask(rtwdev, R_NCTL_N2, MASKDWORD, 0x000000c0 + i);
		rtw89_phy_write32_clr(rtwdev, R_NCTL_N2, MASKDWORD);
		tmp = rtw89_phy_read32_mask(rtwdev, R_TXIQC + (path << 8), MASKDWORD);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, 0x8%lx38 = %x\n",
			    path, BIT(path), tmp);
		udelay(1);
	}
	rtw89_phy_write32_clr(rtwdev, R_IQK_DIF, B_IQK_DIF_TRX);
	rtw89_phy_write32_mask(rtwdev, R_TXIQC + (path << 8), MASKDWORD, 0x40000000);
	rtw89_phy_write32_mask(rtwdev, R_NCTL_N2, MASKDWORD, 0x80010100);
	udelay(1);
}

static void _iqk_read_txcfir_dbcc0(struct rtw89_dev *rtwdev, u8 path,
				   u8 group)
{
	static const u32 base_addrs[RTW8852A_IQK_SS][RTW8852A_IQK_CFIR_GROUP_NR] = {
		{0x8f20, 0x8f54, 0x8f88, 0x8fbc},
		{0x9320, 0x9354, 0x9388, 0x93bc},
	};
	u8 idx = 0x0;
	u32 tmp = 0x0;
	u32 base_addr;

	if (path >= RTW8852A_IQK_SS) {
		rtw89_warn(rtwdev, "cfir path %d out of range\n", path);
		return;
	}
	if (group >= RTW8852A_IQK_CFIR_GROUP_NR) {
		rtw89_warn(rtwdev, "cfir group %d out of range\n", group);
		return;
	}

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]===>%s\n", __func__);
	rtw89_phy_write32_mask(rtwdev, R_W_COEF + (path << 8), MASKDWORD, 0x00000001);

	base_addr = base_addrs[path][group];

	for (idx = 0; idx < 0x0d; idx++) {
		tmp = rtw89_phy_read32_mask(rtwdev, base_addr + (idx << 2), MASKDWORD);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK] %x = %x\n",
			    base_addr + (idx << 2), tmp);
	}

	if (path == 0x0) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]\n");
		tmp = rtw89_phy_read32_mask(rtwdev, R_TXCFIR_P0C0, MASKDWORD);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK] 0x8f50 = %x\n", tmp);
		tmp = rtw89_phy_read32_mask(rtwdev, R_TXCFIR_P0C1, MASKDWORD);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK] 0x8f84 = %x\n", tmp);
		tmp = rtw89_phy_read32_mask(rtwdev, R_TXCFIR_P0C2, MASKDWORD);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK] 0x8fb8 = %x\n", tmp);
		tmp = rtw89_phy_read32_mask(rtwdev, R_TXCFIR_P0C3, MASKDWORD);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK] 0x8fec = %x\n", tmp);
	} else {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]\n");
		tmp = rtw89_phy_read32_mask(rtwdev, R_TXCFIR_P1C0, MASKDWORD);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK] 0x9350 = %x\n", tmp);
		tmp = rtw89_phy_read32_mask(rtwdev, R_TXCFIR_P1C1, MASKDWORD);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK] 0x9384 = %x\n", tmp);
		tmp = rtw89_phy_read32_mask(rtwdev, R_TXCFIR_P1C2, MASKDWORD);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK] 0x93b8 = %x\n", tmp);
		tmp = rtw89_phy_read32_mask(rtwdev, R_TXCFIR_P1C3, MASKDWORD);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK] 0x93ec = %x\n", tmp);
	}
	rtw89_phy_write32_clr(rtwdev, R_W_COEF + (path << 8), MASKDWORD);
	rtw89_phy_write32_mask(rtwdev, R_KIP_RPT + (path << 8), B_KIP_RPT_SEL, 0xc);
	udelay(1);
	tmp = rtw89_phy_read32_mask(rtwdev, R_RPT_PER + (path << 8), MASKDWORD);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, 0x8%lxfc = %x\n", path,
		    BIT(path), tmp);
}

static void _iqk_read_rxcfir_dbcc0(struct rtw89_dev *rtwdev, u8 path,
				   u8 group)
{
	static const u32 base_addrs[RTW8852A_IQK_SS][RTW8852A_IQK_CFIR_GROUP_NR] = {
		{0x8d00, 0x8d44, 0x8d88, 0x8dcc},
		{0x9100, 0x9144, 0x9188, 0x91cc},
	};
	u8 idx = 0x0;
	u32 tmp = 0x0;
	u32 base_addr;

	if (path >= RTW8852A_IQK_SS) {
		rtw89_warn(rtwdev, "cfir path %d out of range\n", path);
		return;
	}
	if (group >= RTW8852A_IQK_CFIR_GROUP_NR) {
		rtw89_warn(rtwdev, "cfir group %d out of range\n", group);
		return;
	}

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]===>%s\n", __func__);
	rtw89_phy_write32_mask(rtwdev, R_W_COEF + (path << 8), MASKDWORD, 0x00000001);

	base_addr = base_addrs[path][group];
	for (idx = 0; idx < 0x10; idx++) {
		tmp = rtw89_phy_read32_mask(rtwdev, base_addr + (idx << 2), MASKDWORD);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[IQK]%x = %x\n",
			    base_addr + (idx << 2), tmp);
	}

	if (path == 0x0) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]\n");
		tmp = rtw89_phy_read32_mask(rtwdev, R_RXCFIR_P0C0, MASKDWORD);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK] 0x8d40 = %x\n", tmp);
		tmp = rtw89_phy_read32_mask(rtwdev, R_RXCFIR_P0C1, MASKDWORD);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK] 0x8d84 = %x\n", tmp);
		tmp = rtw89_phy_read32_mask(rtwdev, R_RXCFIR_P0C2, MASKDWORD);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK] 0x8dc8 = %x\n", tmp);
		tmp = rtw89_phy_read32_mask(rtwdev, R_RXCFIR_P0C3, MASKDWORD);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK] 0x8e0c = %x\n", tmp);
	} else {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]\n");
		tmp = rtw89_phy_read32_mask(rtwdev, R_RXCFIR_P1C0, MASKDWORD);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK] 0x9140 = %x\n", tmp);
		tmp = rtw89_phy_read32_mask(rtwdev, R_RXCFIR_P1C1, MASKDWORD);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK] 0x9184 = %x\n", tmp);
		tmp = rtw89_phy_read32_mask(rtwdev, R_RXCFIR_P1C2, MASKDWORD);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK] 0x91c8 = %x\n", tmp);
		tmp = rtw89_phy_read32_mask(rtwdev, R_RXCFIR_P1C3, MASKDWORD);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK] 0x920c = %x\n", tmp);
	}
	rtw89_phy_write32_clr(rtwdev, R_W_COEF + (path << 8), MASKDWORD);
	rtw89_phy_write32_mask(rtwdev, R_KIP_RPT + (path << 8), B_KIP_RPT_SEL, 0xd);
	tmp = rtw89_phy_read32_mask(rtwdev, R_RPT_PER + (path << 8), MASKDWORD);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, 0x8%lxfc = %x\n", path,
		    BIT(path), tmp);
}

static void _iqk_sram(struct rtw89_dev *rtwdev, u8 path)
{
	u32 tmp = 0x0;
	u32 i = 0x0;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]===>%s\n", __func__);
	rtw89_phy_write32_mask(rtwdev, R_KIP_RPT1, MASKDWORD, 0x00020000);
	rtw89_phy_write32_mask(rtwdev, R_SRAM_IQRX2, MASKDWORD, 0x00000080);
	rtw89_phy_write32_mask(rtwdev, R_SRAM_IQRX, MASKDWORD, 0x00010000);
	rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_TXT, 0x009);

	for (i = 0; i <= 0x9f; i++) {
		rtw89_phy_write32_mask(rtwdev, R_SRAM_IQRX, MASKDWORD, 0x00010000 + i);
		tmp = rtw89_phy_read32_mask(rtwdev, R_RPT_COM, B_PRT_COM_DCI);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]0x%x\n", tmp);
	}

	for (i = 0; i <= 0x9f; i++) {
		rtw89_phy_write32_mask(rtwdev, R_SRAM_IQRX, MASKDWORD, 0x00010000 + i);
		tmp = rtw89_phy_read32_mask(rtwdev, R_RPT_COM, B_PRT_COM_DCQ);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]0x%x\n", tmp);
	}
	rtw89_phy_write32_clr(rtwdev, R_SRAM_IQRX2, MASKDWORD);
	rtw89_phy_write32_clr(rtwdev, R_SRAM_IQRX, MASKDWORD);
}

static void _iqk_rxk_setting(struct rtw89_dev *rtwdev, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	u32 tmp = 0x0;

	rtw89_phy_write32_set(rtwdev, R_P0_NRBW + (path << 13), B_P0_NRBW_DBG);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR_PW15, B_ANAPAR_PW15, 0x3);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_15, 0xa041);
	udelay(1);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR_PW15, B_ANAPAR_PW15_H2, 0x3);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_FLTRST, 0x0);
	udelay(1);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_FLTRST, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR_PW15, B_ANAPAR_PW15_H2, 0x0);
	udelay(1);
	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_RST, 0x0303);
	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_RST, 0x0000);

	switch (iqk_info->iqk_band[path]) {
	case RTW89_BAND_2G:
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_MASK, RR_MOD_V_RXK2);
		rtw89_write_rf(rtwdev, path, RR_RXK, RR_RXK_SEL2G, 0x1);
		break;
	case RTW89_BAND_5G:
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_MASK, RR_MOD_V_RXK2);
		rtw89_write_rf(rtwdev, path, RR_WLSEL, RR_WLSEL_AG, 0x5);
		rtw89_write_rf(rtwdev, path, RR_RXK, RR_RXK_SEL5G, 0x1);
		break;
	default:
		break;
	}
	tmp = rtw89_read_rf(rtwdev, path, RR_CFGCH, RFREG_MASK);
	rtw89_write_rf(rtwdev, path, RR_RSV4, RFREG_MASK, tmp);
	rtw89_write_rf(rtwdev, path, RR_RXKPLL, RR_RXKPLL_OFF, 0x13);
	rtw89_write_rf(rtwdev, path, RR_RXKPLL, RR_RXKPLL_POW, 0x0);
	rtw89_write_rf(rtwdev, path, RR_RXKPLL, RR_RXKPLL_POW, 0x1);
	fsleep(128);
}

static bool _iqk_check_cal(struct rtw89_dev *rtwdev, u8 path, u8 ktype)
{
	u32 tmp;
	u32 val;
	int ret;

	ret = read_poll_timeout_atomic(rtw89_phy_read32_mask, val, val == 0x55, 1, 8200,
				       false, rtwdev, 0xbff8, MASKBYTE0);
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
	bool fail = false;
	u32 iqk_cmd = 0x0;
	u8 phy_map = rtw89_btc_path_phymap(rtwdev, phy_idx, path);
	u32 addr_rfc_ctl = 0x0;

	if (path == RF_PATH_A)
		addr_rfc_ctl = 0x5864;
	else
		addr_rfc_ctl = 0x7864;

	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_IQK, BTC_WRFK_ONESHOT_START);
	switch (ktype) {
	case ID_TXAGC:
		iqk_cmd = 0x008 | (1 << (4 + path)) | (path << 1);
		break;
	case ID_FLOK_COARSE:
		rtw89_phy_write32_set(rtwdev, addr_rfc_ctl, 0x20000000);
		rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_TXT, 0x009);
		iqk_cmd = 0x108 | (1 << (4 + path));
		break;
	case ID_FLOK_FINE:
		rtw89_phy_write32_set(rtwdev, addr_rfc_ctl, 0x20000000);
		rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_TXT, 0x009);
		iqk_cmd = 0x208 | (1 << (4 + path));
		break;
	case ID_TXK:
		rtw89_phy_write32_clr(rtwdev, addr_rfc_ctl, 0x20000000);
		rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_TXT, 0x025);
		iqk_cmd = 0x008 | (1 << (path + 4)) |
			  (((0x8 + iqk_info->iqk_bw[path]) & 0xf) << 8);
		break;
	case ID_RXAGC:
		iqk_cmd = 0x508 | (1 << (4 + path)) | (path << 1);
		break;
	case ID_RXK:
		rtw89_phy_write32_set(rtwdev, addr_rfc_ctl, 0x20000000);
		rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_RXT, 0x011);
		iqk_cmd = 0x008 | (1 << (path + 4)) |
			  (((0xb + iqk_info->iqk_bw[path]) & 0xf) << 8);
		break;
	case ID_NBTXK:
		rtw89_phy_write32_clr(rtwdev, addr_rfc_ctl, 0x20000000);
		rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_TXT, 0x025);
		iqk_cmd = 0x308 | (1 << (4 + path));
		break;
	case ID_NBRXK:
		rtw89_phy_write32_set(rtwdev, addr_rfc_ctl, 0x20000000);
		rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_RXT, 0x011);
		iqk_cmd = 0x608 | (1 << (4 + path));
		break;
	default:
		return false;
	}

	rtw89_phy_write32_mask(rtwdev, R_NCTL_CFG, MASKDWORD, iqk_cmd + 1);
	rtw89_phy_write32_set(rtwdev, R_DPK_CTL, B_DPK_CTL_EN);
	udelay(1);
	fail = _iqk_check_cal(rtwdev, path, ktype);
	if (iqk_info->iqk_xym_en)
		_iqk_read_xym_dbcc0(rtwdev, path);
	if (iqk_info->iqk_fft_en)
		_iqk_read_fft_dbcc0(rtwdev, path);
	if (iqk_info->iqk_sram_en)
		_iqk_sram(rtwdev, path);
	if (iqk_info->iqk_cfir_en) {
		if (ktype == ID_TXK) {
			_iqk_read_txcfir_dbcc0(rtwdev, path, 0x0);
			_iqk_read_txcfir_dbcc0(rtwdev, path, 0x1);
			_iqk_read_txcfir_dbcc0(rtwdev, path, 0x2);
			_iqk_read_txcfir_dbcc0(rtwdev, path, 0x3);
		} else {
			_iqk_read_rxcfir_dbcc0(rtwdev, path, 0x0);
			_iqk_read_rxcfir_dbcc0(rtwdev, path, 0x1);
			_iqk_read_rxcfir_dbcc0(rtwdev, path, 0x2);
			_iqk_read_rxcfir_dbcc0(rtwdev, path, 0x3);
		}
	}

	rtw89_phy_write32_clr(rtwdev, addr_rfc_ctl, 0x20000000);

	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_IQK, BTC_WRFK_ONESHOT_STOP);

	return fail;
}

static bool _rxk_group_sel(struct rtw89_dev *rtwdev,
			   enum rtw89_phy_idx phy_idx, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	static const u32 rxgn_a[4] = {0x18C, 0x1A0, 0x28C, 0x2A0};
	static const u32 attc2_a[4] = {0x0, 0x0, 0x07, 0x30};
	static const u32 attc1_a[4] = {0x7, 0x5, 0x1, 0x1};
	static const u32 rxgn_g[4] = {0x1CC, 0x1E0, 0x2CC, 0x2E0};
	static const u32 attc2_g[4] = {0x0, 0x15, 0x3, 0x1a};
	static const u32 attc1_g[4] = {0x1, 0x0, 0x1, 0x0};
	u8 gp = 0x0;
	bool fail = false;
	u32 rf0 = 0x0;

	for (gp = 0; gp < 0x4; gp++) {
		switch (iqk_info->iqk_band[path]) {
		case RTW89_BAND_2G:
			rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_M_RXG, rxgn_g[gp]);
			rtw89_write_rf(rtwdev, path, RR_RXBB, RR_RXBB_C2G, attc2_g[gp]);
			rtw89_write_rf(rtwdev, path, RR_RXBB, RR_RXBB_C1G, attc1_g[gp]);
			break;
		case RTW89_BAND_5G:
			rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_M_RXG, rxgn_a[gp]);
			rtw89_write_rf(rtwdev, path, RR_RXA2, RR_RXA2_C2, attc2_a[gp]);
			rtw89_write_rf(rtwdev, path, RR_RXA2, RR_RXA2_C1, attc1_a[gp]);
			break;
		default:
			break;
		}
		rtw89_phy_write32_set(rtwdev, R_IQK_CFG, B_IQK_CFG_SET);
		rf0 = rtw89_read_rf(rtwdev, path, RR_MOD, RFREG_MASK);
		rtw89_phy_write32_mask(rtwdev, R_IQK_DIF2, B_IQK_DIF2_RXPI,
				       rf0 | iqk_info->syn1to2);
		rtw89_phy_write32_mask(rtwdev, R_IQK_COM, MASKDWORD, 0x40010100);
		rtw89_phy_write32_clr(rtwdev, R_IQK_RES + (path << 8), B_IQK_RES_RXCFIR);
		rtw89_phy_write32_set(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_SEL);
		rtw89_phy_write32_clr(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_G3);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_GP, gp);
		rtw89_phy_write32_mask(rtwdev, R_IOQ_IQK_DPK, B_IOQ_IQK_DPK_EN, 0x1);
		rtw89_phy_write32_clr(rtwdev, R_NCTL_N1, B_NCTL_N1_CIP);
		fail = _iqk_one_shot(rtwdev, phy_idx, path, ID_RXK);
		rtw89_phy_write32_mask(rtwdev, R_IQKINF, BIT(16 + gp + path * 4), fail);
	}

	switch (iqk_info->iqk_band[path]) {
	case RTW89_BAND_2G:
		rtw89_write_rf(rtwdev, path, RR_RXK, RR_RXK_SEL2G, 0x0);
		rtw89_write_rf(rtwdev, path, RR_RXKPLL, RR_RXKPLL_POW, 0x0);
		break;
	case RTW89_BAND_5G:
		rtw89_write_rf(rtwdev, path, RR_RXK, RR_RXK_SEL5G, 0x0);
		rtw89_write_rf(rtwdev, path, RR_RXKPLL, RR_RXKPLL_POW, 0x0);
		rtw89_write_rf(rtwdev, path, RR_WLSEL, RR_WLSEL_AG, 0x0);
		break;
	default:
		break;
	}
	iqk_info->nb_rxcfir[path] = 0x40000000;
	rtw89_phy_write32_mask(rtwdev, R_IQK_RES + (path << 8),
			       B_IQK_RES_RXCFIR, 0x5);
	iqk_info->is_wb_rxiqk[path] = true;
	return false;
}

static bool _iqk_nbrxk(struct rtw89_dev *rtwdev,
		       enum rtw89_phy_idx phy_idx, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	u8 group = 0x0;
	u32 rf0 = 0x0, tmp = 0x0;
	u32 idxrxgain_a = 0x1a0;
	u32 idxattc2_a = 0x00;
	u32 idxattc1_a = 0x5;
	u32 idxrxgain_g = 0x1E0;
	u32 idxattc2_g = 0x15;
	u32 idxattc1_g = 0x0;
	bool fail = false;

	switch (iqk_info->iqk_band[path]) {
	case RTW89_BAND_2G:
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_M_RXG, idxrxgain_g);
		rtw89_write_rf(rtwdev, path, RR_RXBB, RR_RXBB_C2G, idxattc2_g);
		rtw89_write_rf(rtwdev, path, RR_RXBB, RR_RXBB_C1G, idxattc1_g);
		break;
	case RTW89_BAND_5G:
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_M_RXG, idxrxgain_a);
		rtw89_write_rf(rtwdev, path, RR_RXA2, RR_RXA2_C2, idxattc2_a);
		rtw89_write_rf(rtwdev, path, RR_RXA2, RR_RXA2_C1, idxattc1_a);
		break;
	default:
		break;
	}
	rtw89_phy_write32_set(rtwdev, R_IQK_CFG, B_IQK_CFG_SET);
	rf0 = rtw89_read_rf(rtwdev, path, RR_MOD, RFREG_MASK);
	rtw89_phy_write32_mask(rtwdev, R_IQK_DIF2, B_IQK_DIF2_RXPI,
			       rf0 | iqk_info->syn1to2);
	rtw89_phy_write32_mask(rtwdev, R_IQK_COM, MASKDWORD, 0x40010100);
	rtw89_phy_write32_clr(rtwdev, R_IQK_RES + (path << 8), B_IQK_RES_RXCFIR);
	rtw89_phy_write32_set(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_SEL);
	rtw89_phy_write32_clr(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_G3);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8),
			       B_CFIR_LUT_GP, group);
	rtw89_phy_write32_set(rtwdev, R_IOQ_IQK_DPK, B_IOQ_IQK_DPK_EN);
	rtw89_phy_write32_clr(rtwdev, R_NCTL_N1, B_NCTL_N1_CIP);
	fail = _iqk_one_shot(rtwdev, phy_idx, path, ID_NBRXK);

	switch (iqk_info->iqk_band[path]) {
	case RTW89_BAND_2G:
		rtw89_write_rf(rtwdev, path, RR_RXK, RR_RXK_SEL2G, 0x0);
		rtw89_write_rf(rtwdev, path, RR_RXKPLL, RR_RXKPLL_POW, 0x0);
		break;
	case RTW89_BAND_5G:
		rtw89_write_rf(rtwdev, path, RR_RXK, RR_RXK_SEL5G, 0x0);
		rtw89_write_rf(rtwdev, path, RR_RXKPLL, RR_RXKPLL_POW, 0x0);
		rtw89_write_rf(rtwdev, path, RR_WLSEL, RR_WLSEL_AG, 0x0);
		break;
	default:
		break;
	}
	if (!fail) {
		tmp = rtw89_phy_read32_mask(rtwdev, R_RXIQC + (path << 8), MASKDWORD);
		iqk_info->nb_rxcfir[path] = tmp | 0x2;
	} else {
		iqk_info->nb_rxcfir[path] = 0x40000002;
	}
	return fail;
}

static void _iqk_rxclk_setting(struct rtw89_dev *rtwdev, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;

	if (iqk_info->iqk_bw[path] == RTW89_CHANNEL_WIDTH_80) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]===>%s\n", __func__);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_SYS + (path << 8),
				       MASKDWORD, 0x4d000a08);
		rtw89_phy_write32_mask(rtwdev, R_P0_RXCK + (path << 13),
				       B_P0_RXCK_VAL, 0x2);
		rtw89_phy_write32_set(rtwdev, R_P0_RXCK + (path << 13), B_P0_RXCK_ON);
		rtw89_phy_write32_set(rtwdev, R_UPD_CLK_ADC, B_UPD_CLK_ADC_ON);
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK_ADC, B_UPD_CLK_ADC_VAL, 0x1);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_CFIR_SYS + (path << 8),
				       MASKDWORD, 0x44000a08);
		rtw89_phy_write32_mask(rtwdev, R_P0_RXCK + (path << 13),
				       B_P0_RXCK_VAL, 0x1);
		rtw89_phy_write32_set(rtwdev, R_P0_RXCK + (path << 13), B_P0_RXCK_ON);
		rtw89_phy_write32_set(rtwdev, R_UPD_CLK_ADC, B_UPD_CLK_ADC_ON);
		rtw89_phy_write32_clr(rtwdev, R_UPD_CLK_ADC, B_UPD_CLK_ADC_VAL);
	}
}

static bool _txk_group_sel(struct rtw89_dev *rtwdev,
			   enum rtw89_phy_idx phy_idx, u8 path)
{
	static const u32 a_txgain[4] = {0xE466, 0x646D, 0xE4E2, 0x64ED};
	static const u32 g_txgain[4] = {0x60e8, 0x60f0, 0x61e8, 0x61ED};
	static const u32 a_itqt[4] = {0x12, 0x12, 0x12, 0x1b};
	static const u32 g_itqt[4] = {0x09, 0x12, 0x12, 0x12};
	static const u32 g_attsmxr[4] = {0x0, 0x1, 0x1, 0x1};
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	bool fail = false;
	u8 gp = 0x0;
	u32 tmp = 0x0;

	for (gp = 0x0; gp < 0x4; gp++) {
		switch (iqk_info->iqk_band[path]) {
		case RTW89_BAND_2G:
			rtw89_phy_write32_mask(rtwdev, R_RFGAIN_BND + (path << 8),
					       B_RFGAIN_BND, 0x08);
			rtw89_write_rf(rtwdev, path, RR_GAINTX, RR_GAINTX_ALL,
				       g_txgain[gp]);
			rtw89_write_rf(rtwdev, path, RR_TXG1, RR_TXG1_ATT1,
				       g_attsmxr[gp]);
			rtw89_write_rf(rtwdev, path, RR_TXG2, RR_TXG2_ATT0,
				       g_attsmxr[gp]);
			rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8),
					       MASKDWORD, g_itqt[gp]);
			break;
		case RTW89_BAND_5G:
			rtw89_phy_write32_mask(rtwdev, R_RFGAIN_BND + (path << 8),
					       B_RFGAIN_BND, 0x04);
			rtw89_write_rf(rtwdev, path, RR_GAINTX, RR_GAINTX_ALL,
				       a_txgain[gp]);
			rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8),
					       MASKDWORD, a_itqt[gp]);
			break;
		default:
			break;
		}
		rtw89_phy_write32_clr(rtwdev, R_IQK_RES + (path << 8), B_IQK_RES_TXCFIR);
		rtw89_phy_write32_set(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_SEL);
		rtw89_phy_write32_set(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_G3);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8),
				       B_CFIR_LUT_GP, gp);
		rtw89_phy_write32_clr(rtwdev, R_NCTL_N1, B_NCTL_N1_CIP);
		fail = _iqk_one_shot(rtwdev, phy_idx, path, ID_TXK);
		rtw89_phy_write32_mask(rtwdev, R_IQKINF, BIT(8 + gp + path * 4), fail);
	}

	iqk_info->nb_txcfir[path] = 0x40000000;
	rtw89_phy_write32_mask(rtwdev, R_IQK_RES + (path << 8),
			       B_IQK_RES_TXCFIR, 0x5);
	iqk_info->is_wb_txiqk[path] = true;
	tmp = rtw89_phy_read32_mask(rtwdev, R_TXIQC + (path << 8), MASKDWORD);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, 0x8%lx38 = 0x%x\n", path,
		    BIT(path), tmp);
	return false;
}

static bool _iqk_nbtxk(struct rtw89_dev *rtwdev,
		       enum rtw89_phy_idx phy_idx, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	u8 group = 0x2;
	u32 a_mode_txgain = 0x64e2;
	u32 g_mode_txgain = 0x61e8;
	u32 attsmxr = 0x1;
	u32 itqt = 0x12;
	u32 tmp = 0x0;
	bool fail = false;

	switch (iqk_info->iqk_band[path]) {
	case RTW89_BAND_2G:
		rtw89_phy_write32_mask(rtwdev, R_RFGAIN_BND + (path << 8),
				       B_RFGAIN_BND, 0x08);
		rtw89_write_rf(rtwdev, path, RR_GAINTX, RR_GAINTX_ALL, g_mode_txgain);
		rtw89_write_rf(rtwdev, path, RR_TXG1, RR_TXG1_ATT1, attsmxr);
		rtw89_write_rf(rtwdev, path, RR_TXG2, RR_TXG2_ATT0, attsmxr);
		break;
	case RTW89_BAND_5G:
		rtw89_phy_write32_mask(rtwdev, R_RFGAIN_BND + (path << 8),
				       B_RFGAIN_BND, 0x04);
		rtw89_write_rf(rtwdev, path, RR_GAINTX, RR_GAINTX_ALL, a_mode_txgain);
		break;
	default:
		break;
	}
	rtw89_phy_write32_clr(rtwdev, R_IQK_RES + (path << 8), B_IQK_RES_TXCFIR);
	rtw89_phy_write32_set(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_SEL);
	rtw89_phy_write32_set(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_G3);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_GP, group);
	rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8), MASKDWORD, itqt);
	rtw89_phy_write32_clr(rtwdev, R_NCTL_N1, B_NCTL_N1_CIP);
	fail = _iqk_one_shot(rtwdev, phy_idx, path, ID_NBTXK);
	if (!fail) {
		tmp = rtw89_phy_read32_mask(rtwdev, R_TXIQC + (path << 8), MASKDWORD);
		iqk_info->nb_txcfir[path] = tmp | 0x2;
	} else {
		iqk_info->nb_txcfir[path] = 0x40000002;
	}
	tmp = rtw89_phy_read32_mask(rtwdev, R_TXIQC + (path << 8), MASKDWORD);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, 0x8%lx38 = 0x%x\n", path,
		    BIT(path), tmp);
	return fail;
}

static void _lok_res_table(struct rtw89_dev *rtwdev, u8 path, u8 ibias)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, ibias = %x\n", path, ibias);
	rtw89_write_rf(rtwdev, path, RR_LUTWE, RFREG_MASK, 0x2);
	if (iqk_info->iqk_band[path] == RTW89_BAND_2G)
		rtw89_write_rf(rtwdev, path, RR_LUTWA, RFREG_MASK, 0x0);
	else
		rtw89_write_rf(rtwdev, path, RR_LUTWA, RFREG_MASK, 0x1);
	rtw89_write_rf(rtwdev, path, RR_LUTWD0, RFREG_MASK, ibias);
	rtw89_write_rf(rtwdev, path, RR_LUTWE, RFREG_MASK, 0x0);
}

static bool _lok_finetune_check(struct rtw89_dev *rtwdev, u8 path)
{
	bool is_fail = false;
	u32 tmp = 0x0;
	u32 core_i = 0x0;
	u32 core_q = 0x0;

	tmp = rtw89_read_rf(rtwdev, path, RR_TXMO, RFREG_MASK);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK][FineLOK] S%x, 0x58 = 0x%x\n",
		    path, tmp);
	core_i = FIELD_GET(RR_TXMO_COI, tmp);
	core_q = FIELD_GET(RR_TXMO_COQ, tmp);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, i = 0x%x\n", path, core_i);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]S%x, q = 0x%x\n", path, core_q);

	if (core_i < 0x2 || core_i > 0x1d || core_q < 0x2 || core_q > 0x1d)
		is_fail = true;
	return is_fail;
}

static bool _iqk_lok(struct rtw89_dev *rtwdev,
		     enum rtw89_phy_idx phy_idx, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	u32 rf0 = 0x0;
	u8 itqt = 0x12;
	bool fail = false;
	bool tmp = false;

	switch (iqk_info->iqk_band[path]) {
	case RTW89_BAND_2G:
		rtw89_write_rf(rtwdev, path, RR_GAINTX, RR_GAINTX_ALL, 0xe5e0);
		itqt = 0x09;
		break;
	case RTW89_BAND_5G:
		rtw89_write_rf(rtwdev, path, RR_GAINTX, RR_GAINTX_ALL, 0xe4e0);
		itqt = 0x12;
		break;
	default:
		break;
	}
	rtw89_phy_write32_set(rtwdev, R_IQK_CFG, B_IQK_CFG_SET);
	rf0 = rtw89_read_rf(rtwdev, path, RR_MOD, RFREG_MASK);
	rtw89_phy_write32_mask(rtwdev, R_IQK_DIF1, B_IQK_DIF1_TXPI,
			       rf0 | iqk_info->syn1to2);
	rtw89_phy_write32_clr(rtwdev, R_IQK_RES + (path << 8), B_IQK_RES_TXCFIR);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_SEL, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_G3, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_GP, 0x0);
	rtw89_phy_write32_set(rtwdev, R_IOQ_IQK_DPK, B_IOQ_IQK_DPK_EN);
	rtw89_phy_write32_clr(rtwdev, R_NCTL_N1, B_NCTL_N1_CIP);
	rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8), MASKDWORD, itqt);
	tmp = _iqk_one_shot(rtwdev, phy_idx, path, ID_FLOK_COARSE);
	iqk_info->lok_cor_fail[0][path] = tmp;
	fsleep(10);
	rtw89_phy_write32_mask(rtwdev, R_KIP_IQP + (path << 8), MASKDWORD, itqt);
	tmp = _iqk_one_shot(rtwdev, phy_idx, path, ID_FLOK_FINE);
	iqk_info->lok_fin_fail[0][path] = tmp;
	fail = _lok_finetune_check(rtwdev, path);
	return fail;
}

static void _iqk_txk_setting(struct rtw89_dev *rtwdev, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;

	rtw89_phy_write32_set(rtwdev, R_P0_NRBW + (path << 13), B_P0_NRBW_DBG);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR_PW15, B_ANAPAR_PW15, 0x1f);
	udelay(1);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR_PW15, B_ANAPAR_PW15, 0x13);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_15, 0x0001);
	udelay(1);
	rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_15, 0x0041);
	udelay(1);
	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_RST, 0x0303);
	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_RST, 0x0000);
	switch (iqk_info->iqk_band[path]) {
	case RTW89_BAND_2G:
		rtw89_write_rf(rtwdev, path, RR_XALNA2, RR_XALNA2_SW, 0x00);
		rtw89_write_rf(rtwdev, path, RR_RCKD, RR_RCKD_POW, 0x3f);
		rtw89_write_rf(rtwdev, path, RR_TXG1, RR_TXG1_ATT2, 0x0);
		rtw89_write_rf(rtwdev, path, RR_TXG1, RR_TXG1_ATT1, 0x1);
		rtw89_write_rf(rtwdev, path, RR_TXG2, RR_TXG2_ATT0, 0x1);
		rtw89_write_rf(rtwdev, path, RR_TXGA, RR_TXGA_LOK_EN, 0x0);
		rtw89_write_rf(rtwdev, path, RR_LUTWE, RR_LUTWE_LOK, 0x1);
		rtw89_write_rf(rtwdev, path, RR_LUTDBG, RR_LUTDBG_LOK, 0x0);
		rtw89_write_rf(rtwdev, path, RR_LUTWA, RR_LUTWA_MASK, 0x000);
		rtw89_write_rf(rtwdev, path, RR_RSV2, RFREG_MASK, 0x80200);
		rtw89_write_rf(rtwdev, path, RR_DTXLOK, RFREG_MASK, 0x80200);
		rtw89_write_rf(rtwdev, path, RR_MOD, RFREG_MASK,
			       0x403e0 | iqk_info->syn1to2);
		udelay(1);
		break;
	case RTW89_BAND_5G:
		rtw89_write_rf(rtwdev, path, RR_XGLNA2, RR_XGLNA2_SW, 0x00);
		rtw89_write_rf(rtwdev, path, RR_RCKD, RR_RCKD_POW, 0x3f);
		rtw89_write_rf(rtwdev, path, RR_BIASA, RR_BIASA_A, 0x7);
		rtw89_write_rf(rtwdev, path, RR_TXGA, RR_TXGA_LOK_EN, 0x0);
		rtw89_write_rf(rtwdev, path, RR_LUTWE, RR_LUTWE_LOK, 0x1);
		rtw89_write_rf(rtwdev, path, RR_LUTDBG, RR_LUTDBG_LOK, 0x0);
		rtw89_write_rf(rtwdev, path, RR_LUTWA, RR_LUTWA_MASK, 0x100);
		rtw89_write_rf(rtwdev, path, RR_RSV2, RFREG_MASK, 0x80200);
		rtw89_write_rf(rtwdev, path, RR_DTXLOK, RFREG_MASK, 0x80200);
		rtw89_write_rf(rtwdev, path, RR_LUTWD0, RFREG_MASK, 0x1);
		rtw89_write_rf(rtwdev, path, RR_LUTWD0, RFREG_MASK, 0x0);
		rtw89_write_rf(rtwdev, path, RR_MOD, RFREG_MASK,
			       0x403e0 | iqk_info->syn1to2);
		udelay(1);
		break;
	default:
		break;
	}
}

static void _iqk_txclk_setting(struct rtw89_dev *rtwdev, u8 path)
{
	rtw89_phy_write32_mask(rtwdev, R_CFIR_SYS + (path << 8), MASKDWORD, 0xce000a08);
}

static void _iqk_info_iqk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx,
			  u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	u32 tmp = 0x0;
	bool flag = 0x0;

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
	rtw89_phy_write32_mask(rtwdev, R_IQKINF, BIT(0) << (path * 4), flag);
	flag = iqk_info->lok_fin_fail[0][path];
	rtw89_phy_write32_mask(rtwdev, R_IQKINF, BIT(1) << (path * 4), flag);
	flag = iqk_info->iqk_tx_fail[0][path];
	rtw89_phy_write32_mask(rtwdev, R_IQKINF, BIT(2) << (path * 4), flag);
	flag = iqk_info->iqk_rx_fail[0][path];
	rtw89_phy_write32_mask(rtwdev, R_IQKINF, BIT(3) << (path * 4), flag);

	tmp = rtw89_phy_read32_mask(rtwdev, R_IQK_RES + (path << 8), MASKDWORD);
	iqk_info->bp_iqkenable[path] = tmp;
	tmp = rtw89_phy_read32_mask(rtwdev, R_TXIQC + (path << 8), MASKDWORD);
	iqk_info->bp_txkresult[path] = tmp;
	tmp = rtw89_phy_read32_mask(rtwdev, R_RXIQC + (path << 8), MASKDWORD);
	iqk_info->bp_rxkresult[path] = tmp;

	rtw89_phy_write32_mask(rtwdev, R_IQKINF2, B_IQKINF2_KCNT,
			       (u8)iqk_info->iqk_times);

	tmp = rtw89_phy_read32_mask(rtwdev, R_IQKINF, 0x0000000f << (path * 4));
	if (tmp != 0x0)
		iqk_info->iqk_fail_cnt++;
	rtw89_phy_write32_mask(rtwdev, R_IQKINF2, 0x00ff0000 << (path * 4),
			       iqk_info->iqk_fail_cnt);
}

static
void _iqk_by_path(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	bool lok_is_fail = false;
	u8 ibias = 0x1;
	u8 i = 0;

	_iqk_txclk_setting(rtwdev, path);

	for (i = 0; i < 3; i++) {
		_lok_res_table(rtwdev, path, ibias++);
		_iqk_txk_setting(rtwdev, path);
		lok_is_fail = _iqk_lok(rtwdev, phy_idx, path);
		if (!lok_is_fail)
			break;
	}
	if (iqk_info->is_nbiqk)
		iqk_info->iqk_tx_fail[0][path] = _iqk_nbtxk(rtwdev, phy_idx, path);
	else
		iqk_info->iqk_tx_fail[0][path] = _txk_group_sel(rtwdev, phy_idx, path);

	_iqk_rxclk_setting(rtwdev, path);
	_iqk_rxk_setting(rtwdev, path);
	if (iqk_info->is_nbiqk || rtwdev->dbcc_en || iqk_info->iqk_band[path] == RTW89_BAND_2G)
		iqk_info->iqk_rx_fail[0][path] = _iqk_nbrxk(rtwdev, phy_idx, path);
	else
		iqk_info->iqk_rx_fail[0][path] = _rxk_group_sel(rtwdev, phy_idx, path);

	_iqk_info_iqk(rtwdev, phy_idx, path);
}

static void _iqk_get_ch_info(struct rtw89_dev *rtwdev,
			     enum rtw89_phy_idx phy, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	struct rtw89_hal *hal = &rtwdev->hal;
	u32 reg_rf18 = 0x0, reg_35c = 0x0;
	u8 idx = 0;
	u8 get_empty_table = false;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]===>%s\n", __func__);
	for  (idx = 0; idx < RTW89_IQK_CHS_NR; idx++) {
		if (iqk_info->iqk_mcc_ch[idx][path] == 0) {
			get_empty_table = true;
			break;
		}
	}
	if (!get_empty_table) {
		idx = iqk_info->iqk_table_idx[path] + 1;
		if (idx > RTW89_IQK_CHS_NR - 1)
			idx = 0;
	}
	reg_rf18 = rtw89_read_rf(rtwdev, path, RR_CFGCH, RFREG_MASK);
	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]cfg ch = %d\n", reg_rf18);
	reg_35c = rtw89_phy_read32_mask(rtwdev, 0x35c, 0x00000c00);

	iqk_info->iqk_band[path] = hal->current_band_type;
	iqk_info->iqk_bw[path] = hal->current_band_width;
	iqk_info->iqk_ch[path] = hal->current_channel;

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
	if (reg_35c == 0x01)
		iqk_info->syn1to2 = 0x1;
	else
		iqk_info->syn1to2 = 0x0;

	rtw89_phy_write32_mask(rtwdev, R_IQKINF, B_IQKINF_VER, RTW8852A_IQK_VER);
	rtw89_phy_write32_mask(rtwdev, R_IQKCH, 0x000f << (path * 16),
			       (u8)iqk_info->iqk_band[path]);
	rtw89_phy_write32_mask(rtwdev, R_IQKCH, 0x00f0 << (path * 16),
			       (u8)iqk_info->iqk_bw[path]);
	rtw89_phy_write32_mask(rtwdev, R_IQKCH, 0xff00 << (path * 16),
			       (u8)iqk_info->iqk_ch[path]);

	rtw89_phy_write32_mask(rtwdev, R_IQKINF2, 0x000000ff, RTW8852A_NCTL_VER);
}

static void _iqk_start_iqk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx,
			   u8 path)
{
	_iqk_by_path(rtwdev, phy_idx, path);
}

static void _iqk_restore(struct rtw89_dev *rtwdev, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;

	rtw89_phy_write32_mask(rtwdev, R_TXIQC + (path << 8), MASKDWORD,
			       iqk_info->nb_txcfir[path]);
	rtw89_phy_write32_mask(rtwdev, R_RXIQC + (path << 8), MASKDWORD,
			       iqk_info->nb_rxcfir[path]);
	rtw89_phy_write32_clr(rtwdev, R_NCTL_RPT, MASKDWORD);
	rtw89_phy_write32_clr(rtwdev, R_MDPK_RX_DCK, MASKDWORD);
	rtw89_phy_write32_mask(rtwdev, R_KIP_SYSCFG, MASKDWORD, 0x80000000);
	rtw89_phy_write32_clr(rtwdev, R_KPATH_CFG, MASKDWORD);
	rtw89_phy_write32_clr(rtwdev, R_GAPK, B_GAPK_ADR);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_SYS + (path << 8), MASKDWORD, 0x10010000);
	rtw89_phy_write32_clr(rtwdev, R_KIP + (path << 8), B_KIP_RFGAIN);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_MAP + (path << 8), MASKDWORD, 0xe4e4e4e4);
	rtw89_phy_write32_clr(rtwdev, R_CFIR_LUT + (path << 8), B_CFIR_LUT_SEL);
	rtw89_phy_write32_clr(rtwdev, R_KIP_IQP + (path << 8), B_KIP_IQP_IQSW);
	rtw89_phy_write32_mask(rtwdev, R_LOAD_COEF + (path << 8), MASKDWORD, 0x00000002);
	rtw89_write_rf(rtwdev, path, RR_LUTWE, RR_LUTWE_LOK, 0x0);
	rtw89_write_rf(rtwdev, path, RR_RCKD, RR_RCKD_POW, 0x0);
	rtw89_write_rf(rtwdev, path, RR_LUTWE, RR_LUTWE_LOK, 0x0);
	rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_MASK, RR_MOD_V_RX);
	rtw89_write_rf(rtwdev, path, RR_TXRSV, RR_TXRSV_GAPK, 0x0);
	rtw89_write_rf(rtwdev, path, RR_BIAS, RR_BIAS_GAPK, 0x0);
	rtw89_write_rf(rtwdev, path, RR_RSV1, RR_RSV1_RST, 0x1);
}

static void _iqk_afebb_restore(struct rtw89_dev *rtwdev,
			       enum rtw89_phy_idx phy_idx, u8 path)
{
	const struct rtw89_rfk_tbl *tbl;

	switch (_kpath(rtwdev, phy_idx)) {
	case RF_A:
		tbl = &rtw8852a_rfk_iqk_restore_defs_dbcc_path0_tbl;
		break;
	case RF_B:
		tbl = &rtw8852a_rfk_iqk_restore_defs_dbcc_path1_tbl;
		break;
	default:
		tbl = &rtw8852a_rfk_iqk_restore_defs_nondbcc_path01_tbl;
		break;
	}

	rtw89_rfk_parser(rtwdev, tbl);
}

static void _iqk_preset(struct rtw89_dev *rtwdev, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	u8 idx = iqk_info->iqk_table_idx[path];

	if (rtwdev->dbcc_en) {
		rtw89_phy_write32_mask(rtwdev, R_COEF_SEL + (path << 8),
				       B_COEF_SEL_IQC, path & 0x1);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8),
				       B_CFIR_LUT_G2, path & 0x1);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_COEF_SEL + (path << 8),
				       B_COEF_SEL_IQC, idx);
		rtw89_phy_write32_mask(rtwdev, R_CFIR_LUT + (path << 8),
				       B_CFIR_LUT_G2, idx);
	}
	rtw89_write_rf(rtwdev, path, RR_RSV1, RR_RSV1_RST, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_NCTL_RPT, MASKDWORD, 0x00000080);
	rtw89_phy_write32_clr(rtwdev, R_NCTL_RW, MASKDWORD);
	rtw89_phy_write32_mask(rtwdev, R_KIP_SYSCFG, MASKDWORD, 0x81ff010a);
	rtw89_phy_write32_mask(rtwdev, R_KPATH_CFG, MASKDWORD, 0x00200000);
	rtw89_phy_write32_mask(rtwdev, R_MDPK_RX_DCK, MASKDWORD, 0x80000000);
	rtw89_phy_write32_clr(rtwdev, R_LOAD_COEF + (path << 8), MASKDWORD);
}

static void _iqk_macbb_setting(struct rtw89_dev *rtwdev,
			       enum rtw89_phy_idx phy_idx, u8 path)
{
	const struct rtw89_rfk_tbl *tbl;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[IQK]===> %s\n", __func__);

	switch (_kpath(rtwdev, phy_idx)) {
	case RF_A:
		tbl = &rtw8852a_rfk_iqk_set_defs_dbcc_path0_tbl;
		break;
	case RF_B:
		tbl = &rtw8852a_rfk_iqk_set_defs_dbcc_path1_tbl;
		break;
	default:
		tbl = &rtw8852a_rfk_iqk_set_defs_nondbcc_path01_tbl;
		break;
	}

	rtw89_rfk_parser(rtwdev, tbl);
}

static void _iqk_dbcc(struct rtw89_dev *rtwdev, u8 path)
{
	struct rtw89_iqk_info *iqk_info = &rtwdev->iqk;
	u8 phy_idx = 0x0;

	iqk_info->iqk_times++;

	if (path == 0x0)
		phy_idx = RTW89_PHY_0;
	else
		phy_idx = RTW89_PHY_1;

	_iqk_get_ch_info(rtwdev, phy_idx, path);
	_iqk_macbb_setting(rtwdev, phy_idx, path);
	_iqk_preset(rtwdev, path);
	_iqk_start_iqk(rtwdev, phy_idx, path);
	_iqk_restore(rtwdev, path);
	_iqk_afebb_restore(rtwdev, phy_idx, path);
}

static void _iqk_track(struct rtw89_dev *rtwdev)
{
	struct rtw89_iqk_info *iqk = &rtwdev->iqk;
	u8 path = 0x0;
	u8 cur_ther;

	if (iqk->iqk_band[0] == RTW89_BAND_2G)
		return;
	if (iqk->iqk_bw[0] < RTW89_CHANNEL_WIDTH_80)
		return;

	/* only check path 0 */
	for (path = 0; path < 1; path++) {
		cur_ther = ewma_thermal_read(&rtwdev->phystat.avg_thermal[path]);

		if (abs(cur_ther - iqk->thermal[path]) > RTW8852A_IQK_THR_REK)
			iqk->thermal_rek_en = true;
		else
			iqk->thermal_rek_en = false;
	}
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

	/* RCK_ADC_OFFSET */
	rtw89_write_rf(rtwdev, path, RR_RCKO, RR_RCKO_OFF, 0x4);

	rtw89_write_rf(rtwdev, path, RR_RFC, RR_RFC_CKEN, 0x1);
	rtw89_write_rf(rtwdev, path, RR_RFC, RR_RFC_CKEN, 0x0);

	rtw89_write_rf(rtwdev, path, RR_RSV1, RFREG_MASK, rf_reg5);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[RCK] RF 0x1b / 0x1c / 0x1d = 0x%x / 0x%x / 0x%x\n",
		    rtw89_read_rf(rtwdev, path, RR_RCKC, RFREG_MASK),
		    rtw89_read_rf(rtwdev, path, RR_RCKS, RFREG_MASK),
		    rtw89_read_rf(rtwdev, path, RR_RCKO, RFREG_MASK));
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
		for (path = 0; path < RTW8852A_IQK_SS; path++) {
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
	u32 backup_rf_val[RTW8852A_IQK_SS][BACKUP_RF_REGS_NR];
	u8 phy_map = rtw89_btc_phymap(rtwdev, phy_idx, RF_AB);

	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_IQK, BTC_WRFK_ONESHOT_START);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[IQK]==========IQK strat!!!!!==========\n");
	iqk_info->iqk_times++;
	iqk_info->kcount = 0;
	iqk_info->version = RTW8852A_IQK_VER;

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

#define RXDCK_VER_8852A 0xe

static void _set_rx_dck(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			enum rtw89_rf_path path, bool is_afe)
{
	u8 phy_map = rtw89_btc_path_phymap(rtwdev, phy, path);
	u32 ori_val;

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[RX_DCK] ==== S%d RX DCK (by %s)====\n",
		    path, is_afe ? "AFE" : "RFC");

	ori_val = rtw89_phy_read32_mask(rtwdev, R_P0_RXCK + (path << 13), MASKDWORD);

	if (is_afe) {
		rtw89_phy_write32_set(rtwdev, R_P0_NRBW + (path << 13), B_P0_NRBW_DBG);
		rtw89_phy_write32_set(rtwdev, R_P0_RXCK + (path << 13), B_P0_RXCK_ON);
		rtw89_phy_write32_mask(rtwdev, R_P0_RXCK + (path << 13),
				       B_P0_RXCK_VAL, 0x3);
		rtw89_phy_write32_set(rtwdev, R_S0_RXDC2 + (path << 13), B_S0_RXDC2_MEN);
		rtw89_phy_write32_mask(rtwdev, R_S0_RXDC2 + (path << 13),
				       B_S0_RXDC2_AVG, 0x3);
		rtw89_phy_write32_mask(rtwdev, R_ANAPAR_PW15, B_ANAPAR_PW15_H, 0x3);
		rtw89_phy_write32_clr(rtwdev, R_ANAPAR, B_ANAPAR_ADCCLK);
		rtw89_phy_write32_clr(rtwdev, R_ANAPAR, B_ANAPAR_FLTRST);
		rtw89_phy_write32_set(rtwdev, R_ANAPAR, B_ANAPAR_FLTRST);
		rtw89_phy_write32_mask(rtwdev, R_ANAPAR, B_ANAPAR_CRXBB, 0x1);
	}

	rtw89_write_rf(rtwdev, path, RR_DCK2, RR_DCK2_CYCLE, 0x3f);
	rtw89_write_rf(rtwdev, path, RR_DCK1, RR_DCK1_SEL, is_afe);

	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_RXDCK, BTC_WRFK_ONESHOT_START);

	rtw89_write_rf(rtwdev, path, RR_DCK, RR_DCK_LV, 0x0);
	rtw89_write_rf(rtwdev, path, RR_DCK, RR_DCK_LV, 0x1);

	fsleep(600);

	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_RXDCK, BTC_WRFK_ONESHOT_STOP);

	rtw89_write_rf(rtwdev, path, RR_DCK, RR_DCK_LV, 0x0);

	if (is_afe) {
		rtw89_phy_write32_clr(rtwdev, R_P0_NRBW + (path << 13), B_P0_NRBW_DBG);
		rtw89_phy_write32_mask(rtwdev, R_P0_RXCK + (path << 13),
				       MASKDWORD, ori_val);
	}
}

static void _rx_dck(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
		    bool is_afe)
{
	u8 path, kpath, dck_tune;
	u32 rf_reg5;
	u32 addr;

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[RX_DCK] ****** RXDCK Start (Ver: 0x%x, Cv: %d) ******\n",
		    RXDCK_VER_8852A, rtwdev->hal.cv);

	kpath = _kpath(rtwdev, phy);

	for (path = 0; path < 2; path++) {
		if (!(kpath & BIT(path)))
			continue;

		rf_reg5 = rtw89_read_rf(rtwdev, path, RR_RSV1, RFREG_MASK);
		dck_tune = (u8)rtw89_read_rf(rtwdev, path, RR_DCK, RR_DCK_FINE);

		if (rtwdev->is_tssi_mode[path]) {
			addr = 0x5818 + (path << 13);
			/* TSSI pause */
			rtw89_phy_write32_set(rtwdev, addr, BIT(30));
		}

		rtw89_write_rf(rtwdev, path, RR_RSV1, RR_RSV1_RST, 0x0);
		rtw89_write_rf(rtwdev, path, RR_DCK, RR_DCK_FINE, 0x0);
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_MASK, RR_MOD_V_RX);
		_set_rx_dck(rtwdev, phy, path, is_afe);
		rtw89_write_rf(rtwdev, path, RR_DCK, RR_DCK_FINE, dck_tune);
		rtw89_write_rf(rtwdev, path, RR_RSV1, RFREG_MASK, rf_reg5);

		if (rtwdev->is_tssi_mode[path]) {
			addr = 0x5818 + (path << 13);
			/* TSSI resume */
			rtw89_phy_write32_clr(rtwdev, addr, BIT(30));
		}
	}
}

#define RTW8852A_RF_REL_VERSION 34
#define RTW8852A_DPK_VER 0x10
#define RTW8852A_DPK_TH_AVG_NUM 4
#define RTW8852A_DPK_RF_PATH 2
#define RTW8852A_DPK_KIP_REG_NUM 2

enum rtw8852a_dpk_id {
	LBK_RXIQK	= 0x06,
	SYNC		= 0x10,
	MDPK_IDL	= 0x11,
	MDPK_MPA	= 0x12,
	GAIN_LOSS	= 0x13,
	GAIN_CAL	= 0x14,
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

static void _dpk_bkup_kip(struct rtw89_dev *rtwdev, u32 *reg,
			  u32 reg_bkup[][RTW8852A_DPK_KIP_REG_NUM],
			  u8 path)
{
	u8 i;

	for (i = 0; i < RTW8852A_DPK_KIP_REG_NUM; i++) {
		reg_bkup[path][i] = rtw89_phy_read32_mask(rtwdev,
							  reg[i] + (path << 8),
							  MASKDWORD);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] Backup 0x%x = %x\n",
			    reg[i] + (path << 8), reg_bkup[path][i]);
	}
}

static void _dpk_reload_kip(struct rtw89_dev *rtwdev, u32 *reg,
			    u32 reg_bkup[][RTW8852A_DPK_KIP_REG_NUM], u8 path)
{
	u8 i;

	for (i = 0; i < RTW8852A_DPK_KIP_REG_NUM; i++) {
		rtw89_phy_write32_mask(rtwdev, reg[i] + (path << 8),
				       MASKDWORD, reg_bkup[path][i]);
		rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] Reload 0x%x = %x\n",
			    reg[i] + (path << 8), reg_bkup[path][i]);
	}
}

static u8 _dpk_one_shot(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			enum rtw89_rf_path path, enum rtw8852a_dpk_id id)
{
	u8 phy_map  = rtw89_btc_path_phymap(rtwdev, phy, path);
	u16 dpk_cmd = 0x0;
	u32 val;
	int ret;

	dpk_cmd = (u16)((id << 8) | (0x19 + (path << 4)));

	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_DPK, BTC_WRFK_ONESHOT_START);

	rtw89_phy_write32_mask(rtwdev, R_NCTL_CFG, MASKDWORD, dpk_cmd);
	rtw89_phy_write32_set(rtwdev, R_DPK_CTL, B_DPK_CTL_EN);

	ret = read_poll_timeout_atomic(rtw89_phy_read32_mask, val, val == 0x55,
				       10, 20000, false, rtwdev, 0xbff8, MASKBYTE0);

	rtw89_phy_write32_clr(rtwdev, R_NCTL_N1, MASKBYTE0);

	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_DPK, BTC_WRFK_ONESHOT_STOP);

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

static void _dpk_rx_dck(struct rtw89_dev *rtwdev,
			enum rtw89_phy_idx phy,
			enum rtw89_rf_path path)
{
	rtw89_write_rf(rtwdev, path, RR_RXBB2, RR_EN_TIA_IDA, 0x3);
	_set_rx_dck(rtwdev, phy, path, false);
}

static void _dpk_information(struct rtw89_dev *rtwdev,
			     enum rtw89_phy_idx phy,
			     enum rtw89_rf_path path)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;
	struct rtw89_hal *hal = &rtwdev->hal;

	u8 kidx = dpk->cur_idx[path];

	dpk->bp[path][kidx].band = hal->current_band_type;
	dpk->bp[path][kidx].ch = hal->current_channel;
	dpk->bp[path][kidx].bw = hal->current_band_width;

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
	switch (kpath) {
	case RF_A:
		rtw89_rfk_parser(rtwdev, &rtw8852a_rfk_dpk_bb_afe_sf_defs_a_tbl);

		if (rtw89_phy_read32_mask(rtwdev, R_2P4G_BAND, B_2P4G_BAND_SEL) == 0x0)
			rtw89_phy_write32_set(rtwdev, R_RXCCA, B_RXCCA_DIS);

		rtw89_rfk_parser(rtwdev, &rtw8852a_rfk_dpk_bb_afe_sr_defs_a_tbl);
		break;
	case RF_B:
		rtw89_rfk_parser(rtwdev, &rtw8852a_rfk_dpk_bb_afe_sf_defs_b_tbl);

		if (rtw89_phy_read32_mask(rtwdev, R_2P4G_BAND, B_2P4G_BAND_SEL) == 0x1)
			rtw89_phy_write32_set(rtwdev, R_RXCCA, B_RXCCA_DIS);

		rtw89_rfk_parser(rtwdev, &rtw8852a_rfk_dpk_bb_afe_sr_defs_b_tbl);
		break;
	case RF_AB:
		rtw89_rfk_parser(rtwdev, &rtw8852a_rfk_dpk_bb_afe_s_defs_ab_tbl);
		break;
	default:
		break;
	}
	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DPK] Set BB/AFE for PHY%d (kpath=%d)\n", phy, kpath);
}

static void _dpk_bb_afe_restore(struct rtw89_dev *rtwdev,
				enum rtw89_phy_idx phy,
				enum rtw89_rf_path path, u8 kpath)
{
	switch (kpath) {
	case RF_A:
		rtw89_rfk_parser(rtwdev, &rtw8852a_rfk_dpk_bb_afe_r_defs_a_tbl);
		break;
	case RF_B:
		rtw89_rfk_parser(rtwdev, &rtw8852a_rfk_dpk_bb_afe_r_defs_b_tbl);
		break;
	case RF_AB:
		rtw89_rfk_parser(rtwdev, &rtw8852a_rfk_dpk_bb_afe_r_defs_ab_tbl);
		break;
	default:
		break;
	}
	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DPK] Restore BB/AFE for PHY%d (kpath=%d)\n", phy, kpath);
}

static void _dpk_tssi_pause(struct rtw89_dev *rtwdev,
			    enum rtw89_rf_path path, bool is_pause)
{
	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_TRK + (path << 13),
			       B_P0_TSSI_TRK_EN, is_pause);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] S%d TSSI %s\n", path,
		    is_pause ? "pause" : "resume");
}

static void _dpk_kip_setting(struct rtw89_dev *rtwdev,
			     enum rtw89_rf_path path, u8 kidx)
{
	rtw89_phy_write32_mask(rtwdev, R_NCTL_RPT, MASKDWORD, 0x00000080);
	rtw89_phy_write32_mask(rtwdev, R_KIP_CLK, MASKDWORD, 0x00093f3f);
	rtw89_phy_write32_mask(rtwdev, R_KIP_SYSCFG, MASKDWORD, 0x807f030a);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_SYS + (path << 8), MASKDWORD, 0xce000a08);
	rtw89_phy_write32_mask(rtwdev, R_DPK_CFG, B_DPK_CFG_IDX, 0x2);
	rtw89_phy_write32_mask(rtwdev, R_NCTL_CFG, B_NCTL_CFG_SPAGE, path); /*subpage_id*/
	rtw89_phy_write32_mask(rtwdev, R_DPD_CH0 + (path << 8) + (kidx << 2),
			       MASKDWORD, 0x003f2e2e);
	rtw89_phy_write32_mask(rtwdev, R_DPD_CH0A + (path << 8) + (kidx << 2),
			       MASKDWORD, 0x005b5b5b);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] KIP setting for S%d[%d]!!\n",
		    path, kidx);
}

static void _dpk_kip_restore(struct rtw89_dev *rtwdev,
			     enum rtw89_rf_path path)
{
	rtw89_phy_write32_clr(rtwdev, R_NCTL_RPT, MASKDWORD);
	rtw89_phy_write32_mask(rtwdev, R_KIP_SYSCFG, MASKDWORD, 0x80000000);
	rtw89_phy_write32_mask(rtwdev, R_CFIR_SYS + (path << 8), MASKDWORD, 0x10010000);
	rtw89_phy_write32_clr(rtwdev, R_KIP_CLK, MASKDWORD);

	if (rtwdev->hal.cv > CHIP_CBV)
		rtw89_phy_write32_mask(rtwdev, R_DPD_COM + (path << 8), BIT(15), 0x1);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] S%d restore KIP\n", path);
}

static void _dpk_lbk_rxiqk(struct rtw89_dev *rtwdev,
			   enum rtw89_phy_idx phy,
			   enum rtw89_rf_path path)
{
	u8 cur_rxbb;

	cur_rxbb = (u8)rtw89_read_rf(rtwdev, path, RR_MOD, RR_MOD_M_RXBB);

	rtw89_rfk_parser(rtwdev, &rtw8852a_rfk_dpk_lbk_rxiqk_defs_f_tbl);

	rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_MASK, 0xc);
	rtw89_write_rf(rtwdev, path, RR_RXK, RR_RXK_PLLEN, 0x1);
	rtw89_write_rf(rtwdev, path, RR_RXPOW, RR_RXPOW_IQK, 0x2);
	rtw89_write_rf(rtwdev, path, RR_RSV4, RFREG_MASK,
		       rtw89_read_rf(rtwdev, path, RR_CFGCH, RFREG_MASK));
	rtw89_write_rf(rtwdev, path, RR_RXKPLL, RR_RXKPLL_OFF, 0x13);
	rtw89_write_rf(rtwdev, path, RR_RXKPLL, RR_RXKPLL_POW, 0x0);
	rtw89_write_rf(rtwdev, path, RR_RXKPLL, RR_RXKPLL_POW, 0x1);

	fsleep(70);

	rtw89_write_rf(rtwdev, path, RR_RXIQGEN, RR_RXIQGEN_ATTL, 0x1f);

	if (cur_rxbb <= 0xa)
		rtw89_write_rf(rtwdev, path, RR_RXIQGEN, RR_RXIQGEN_ATTH, 0x3);
	else if (cur_rxbb <= 0x10 && cur_rxbb >= 0xb)
		rtw89_write_rf(rtwdev, path, RR_RXIQGEN, RR_RXIQGEN_ATTH, 0x1);
	else
		rtw89_write_rf(rtwdev, path, RR_RXIQGEN, RR_RXIQGEN_ATTH, 0x0);

	rtw89_phy_write32_mask(rtwdev, R_IQK_DIF4, B_IQK_DIF4_RXT, 0x11);

	_dpk_one_shot(rtwdev, phy, path, LBK_RXIQK);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] S%d LBK RXIQC = 0x%x\n", path,
		    rtw89_phy_read32_mask(rtwdev, R_RXIQC, MASKDWORD));

	rtw89_write_rf(rtwdev, path, RR_RXK, RR_RXK_PLLEN, 0x0);
	rtw89_write_rf(rtwdev, path, RR_RXPOW, RR_RXPOW_IQK, 0x0);
	rtw89_write_rf(rtwdev, path, RR_RXKPLL, RR_RXKPLL_POW, 0x0); /*POW IQKPLL*/
	rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_MASK, RR_MOD_V_DPK);

	rtw89_rfk_parser(rtwdev, &rtw8852a_rfk_dpk_lbk_rxiqk_defs_r_tbl);
}

static void _dpk_get_thermal(struct rtw89_dev *rtwdev, u8 kidx,
			     enum rtw89_rf_path path)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;

	dpk->bp[path][kidx].ther_dpk =
		ewma_thermal_read(&rtwdev->phystat.avg_thermal[path]);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] thermal@DPK = 0x%x\n",
		    dpk->bp[path][kidx].ther_dpk);
}

static u8 _dpk_set_tx_pwr(struct rtw89_dev *rtwdev, u8 gain,
			  enum rtw89_rf_path path)
{
	u8 txagc_ori = 0x38;

	rtw89_write_rf(rtwdev, path, RR_MODOPT, RFREG_MASK, txagc_ori);

	return txagc_ori;
}

static void _dpk_rf_setting(struct rtw89_dev *rtwdev, u8 gain,
			    enum rtw89_rf_path path, u8 kidx)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;

	if (dpk->bp[path][kidx].band == RTW89_BAND_2G) {
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_DPK, 0x280b);
		rtw89_write_rf(rtwdev, path, RR_RXBB, RR_RXBB_ATTC, 0x0);
		rtw89_write_rf(rtwdev, path, RR_RXBB, RR_RXBB_ATTR, 0x4);
		rtw89_write_rf(rtwdev, path, RR_MIXER, RR_MIXER_GN, 0x0);
	} else {
		rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_DPK, 0x282e);
		rtw89_write_rf(rtwdev, path, RR_BIASA2, RR_BIASA2_LB, 0x7);
		rtw89_write_rf(rtwdev, path, RR_TXATANK, RR_TXATANK_LBSW, 0x3);
		rtw89_write_rf(rtwdev, path, RR_RXA, RR_RXA_DPK, 0x3);
	}
	rtw89_write_rf(rtwdev, path, RR_RCKD, RR_RCKD_BW, 0x1);
	rtw89_write_rf(rtwdev, path, RR_BTC, RR_BTC_TXBB, dpk->bp[path][kidx].bw + 1);
	rtw89_write_rf(rtwdev, path, RR_BTC, RR_BTC_RXBB, 0x0);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DPK] RF 0x0/0x1/0x1a = 0x%x/ 0x%x/ 0x%x\n",
		    rtw89_read_rf(rtwdev, path, RR_MOD, RFREG_MASK),
		    rtw89_read_rf(rtwdev, path, RR_MODOPT, RFREG_MASK),
		    rtw89_read_rf(rtwdev, path, RR_BTC, RFREG_MASK));
}

static void _dpk_manual_txcfir(struct rtw89_dev *rtwdev,
			       enum rtw89_rf_path path, bool is_manual)
{
	u8 tmp_pad, tmp_txbb;

	if (is_manual) {
		rtw89_phy_write32_mask(rtwdev, R_KIP + (path << 8), B_KIP_RFGAIN, 0x1);
		tmp_pad = (u8)rtw89_read_rf(rtwdev, path, RR_GAINTX, RR_GAINTX_PAD);
		rtw89_phy_write32_mask(rtwdev, R_RFGAIN + (path << 8),
				       B_RFGAIN_PAD, tmp_pad);

		tmp_txbb = (u8)rtw89_read_rf(rtwdev, path, RR_GAINTX, RR_GAINTX_BB);
		rtw89_phy_write32_mask(rtwdev, R_RFGAIN + (path << 8),
				       B_RFGAIN_TXBB, tmp_txbb);

		rtw89_phy_write32_mask(rtwdev, R_LOAD_COEF + (path << 8),
				       B_LOAD_COEF_CFIR, 0x1);
		rtw89_phy_write32_clr(rtwdev, R_LOAD_COEF + (path << 8),
				      B_LOAD_COEF_CFIR);

		rtw89_phy_write32_mask(rtwdev, R_LOAD_COEF + (path << 8), BIT(1), 0x1);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[DPK] PAD_man / TXBB_man = 0x%x / 0x%x\n", tmp_pad,
			    tmp_txbb);
	} else {
		rtw89_phy_write32_clr(rtwdev, R_KIP + (path << 8), B_KIP_RFGAIN);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[DPK] disable manual switch TXCFIR\n");
	}
}

static void _dpk_bypass_rxcfir(struct rtw89_dev *rtwdev,
			       enum rtw89_rf_path path, bool is_bypass)
{
	if (is_bypass) {
		rtw89_phy_write32_mask(rtwdev, R_RXIQC + (path << 8),
				       B_RXIQC_BYPASS2, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_RXIQC + (path << 8),
				       B_RXIQC_BYPASS, 0x1);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[DPK] Bypass RXIQC (0x8%d3c = 0x%x)\n", 1 + path,
			    rtw89_phy_read32_mask(rtwdev, R_RXIQC + (path << 8),
						  MASKDWORD));
	} else {
		rtw89_phy_write32_clr(rtwdev, R_RXIQC + (path << 8), B_RXIQC_BYPASS2);
		rtw89_phy_write32_clr(rtwdev, R_RXIQC + (path << 8), B_RXIQC_BYPASS);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[DPK] restore 0x8%d3c = 0x%x\n", 1 + path,
			    rtw89_phy_read32_mask(rtwdev, R_RXIQC + (path << 8),
						  MASKDWORD));
	}
}

static
void _dpk_tpg_sel(struct rtw89_dev *rtwdev, enum rtw89_rf_path path, u8 kidx)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;

	if (dpk->bp[path][kidx].bw == RTW89_CHANNEL_WIDTH_80)
		rtw89_phy_write32_clr(rtwdev, R_TPG_MOD, B_TPG_MOD_F);
	else if (dpk->bp[path][kidx].bw == RTW89_CHANNEL_WIDTH_40)
		rtw89_phy_write32_mask(rtwdev, R_TPG_MOD, B_TPG_MOD_F, 0x2);
	else
		rtw89_phy_write32_mask(rtwdev, R_TPG_MOD, B_TPG_MOD_F, 0x1);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] TPG_Select for %s\n",
		    dpk->bp[path][kidx].bw == RTW89_CHANNEL_WIDTH_80 ? "80M" :
		    dpk->bp[path][kidx].bw == RTW89_CHANNEL_WIDTH_40 ? "40M" : "20M");
}

static void _dpk_table_select(struct rtw89_dev *rtwdev,
			      enum rtw89_rf_path path, u8 kidx, u8 gain)
{
	u8 val;

	val = 0x80 + kidx * 0x20 + gain * 0x10;
	rtw89_phy_write32_mask(rtwdev, R_DPD_CH0 + (path << 8), MASKBYTE3, val);
	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DPK] table select for Kidx[%d], Gain[%d] (0x%x)\n", kidx,
		    gain, val);
}

static bool _dpk_sync_check(struct rtw89_dev *rtwdev,
			    enum rtw89_rf_path path)
{
#define DPK_SYNC_TH_DC_I 200
#define DPK_SYNC_TH_DC_Q 200
#define DPK_SYNC_TH_CORR 170
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;
	u16 dc_i, dc_q;
	u8 corr_val, corr_idx;

	rtw89_phy_write32_clr(rtwdev, R_KIP_RPT1, B_KIP_RPT1_SEL);

	corr_idx = (u8)rtw89_phy_read32_mask(rtwdev, R_RPT_COM, B_PRT_COM_CORI);
	corr_val = (u8)rtw89_phy_read32_mask(rtwdev, R_RPT_COM, B_PRT_COM_CORV);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DPK] S%d Corr_idx / Corr_val = %d / %d\n", path, corr_idx,
		    corr_val);

	dpk->corr_idx[path] = corr_idx;
	dpk->corr_val[path] = corr_val;

	rtw89_phy_write32_mask(rtwdev, R_KIP_RPT1, B_KIP_RPT1_SEL, 0x9);

	dc_i = (u16)rtw89_phy_read32_mask(rtwdev, R_RPT_COM, B_PRT_COM_DCI);
	dc_q = (u16)rtw89_phy_read32_mask(rtwdev, R_RPT_COM, B_PRT_COM_DCQ);

	dc_i = abs(sign_extend32(dc_i, 11));
	dc_q = abs(sign_extend32(dc_q, 11));

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] S%d DC I/Q, = %d / %d\n",
		    path, dc_i, dc_q);

	dpk->dc_i[path] = dc_i;
	dpk->dc_q[path] = dc_q;

	if (dc_i > DPK_SYNC_TH_DC_I || dc_q > DPK_SYNC_TH_DC_Q ||
	    corr_val < DPK_SYNC_TH_CORR)
		return true;
	else
		return false;
}

static bool _dpk_sync(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
		      enum rtw89_rf_path path, u8 kidx)
{
	_dpk_tpg_sel(rtwdev, path, kidx);
	_dpk_one_shot(rtwdev, phy, path, SYNC);
	return _dpk_sync_check(rtwdev, path); /*1= fail*/
}

static u16 _dpk_dgain_read(struct rtw89_dev *rtwdev)
{
	u16 dgain = 0x0;

	rtw89_phy_write32_clr(rtwdev, R_KIP_RPT1, B_KIP_RPT1_SEL);

	rtw89_phy_read32_mask(rtwdev, R_RPT_COM, B_PRT_COM_SYNERR);

	dgain = (u16)rtw89_phy_read32_mask(rtwdev, R_RPT_COM, B_PRT_COM_DCI);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] DGain = 0x%x (%d)\n", dgain,
		    dgain);

	return dgain;
}

static s8 _dpk_dgain_mapping(struct rtw89_dev *rtwdev, u16 dgain)
{
	s8 offset;

	if (dgain >= 0x783)
		offset = 0x6;
	else if (dgain <= 0x782 && dgain >= 0x551)
		offset = 0x3;
	else if (dgain <= 0x550 && dgain >= 0x3c4)
		offset = 0x0;
	else if (dgain <= 0x3c3 && dgain >= 0x2aa)
		offset = -3;
	else if (dgain <= 0x2a9 && dgain >= 0x1e3)
		offset = -6;
	else if (dgain <= 0x1e2 && dgain >= 0x156)
		offset = -9;
	else if (dgain <= 0x155)
		offset = -12;
	else
		offset = 0x0;

	return offset;
}

static u8 _dpk_gainloss_read(struct rtw89_dev *rtwdev)
{
	rtw89_phy_write32_mask(rtwdev, R_KIP_RPT1, B_KIP_RPT1_SEL, 0x6);
	rtw89_phy_write32_mask(rtwdev, R_DPK_CFG2, B_DPK_CFG2_ST, 0x1);
	return rtw89_phy_read32_mask(rtwdev, R_RPT_COM, B_PRT_COM_GL);
}

static void _dpk_gainloss(struct rtw89_dev *rtwdev,
			  enum rtw89_phy_idx phy, enum rtw89_rf_path path,
			  u8 kidx)
{
	_dpk_table_select(rtwdev, path, kidx, 1);
	_dpk_one_shot(rtwdev, phy, path, GAIN_LOSS);
}

#define DPK_TXAGC_LOWER 0x2e
#define DPK_TXAGC_UPPER 0x3f
#define DPK_TXAGC_INVAL 0xff

static u8 _dpk_set_offset(struct rtw89_dev *rtwdev,
			  enum rtw89_rf_path path, s8 gain_offset)
{
	u8 txagc;

	txagc = (u8)rtw89_read_rf(rtwdev, path, RR_MODOPT, RFREG_MASK);

	if (txagc - gain_offset < DPK_TXAGC_LOWER)
		txagc = DPK_TXAGC_LOWER;
	else if (txagc - gain_offset > DPK_TXAGC_UPPER)
		txagc = DPK_TXAGC_UPPER;
	else
		txagc = txagc - gain_offset;

	rtw89_write_rf(rtwdev, path, RR_MODOPT, RFREG_MASK, txagc);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] tmp_txagc (GL=%d) = 0x%x\n",
		    gain_offset, txagc);
	return txagc;
}

enum dpk_agc_step {
	DPK_AGC_STEP_SYNC_DGAIN,
	DPK_AGC_STEP_GAIN_ADJ,
	DPK_AGC_STEP_GAIN_LOSS_IDX,
	DPK_AGC_STEP_GL_GT_CRITERION,
	DPK_AGC_STEP_GL_LT_CRITERION,
	DPK_AGC_STEP_SET_TX_GAIN,
};

static u8 _dpk_pas_read(struct rtw89_dev *rtwdev, bool is_check)
{
	u32 val1_i = 0, val1_q = 0, val2_i = 0, val2_q = 0;
	u8 i;

	rtw89_rfk_parser(rtwdev, &rtw8852a_rfk_dpk_pas_read_defs_tbl);

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
			    (val1_i * val1_i + val1_q * val1_q) /
			    (val2_i * val2_i + val2_q * val2_q));

	} else {
		for (i = 0; i < 32; i++) {
			rtw89_phy_write32_mask(rtwdev, R_DPK_CFG3, MASKBYTE3, i);
			rtw89_debug(rtwdev, RTW89_DBG_RFK,
				    "[DPK] PAS_Read[%02d]= 0x%08x\n", i,
				    rtw89_phy_read32_mask(rtwdev, R_RPT_COM, MASKDWORD));
		}
	}
	if ((val1_i * val1_i + val1_q * val1_q) >=
	    ((val2_i * val2_i + val2_q * val2_q) * 8 / 5))
		return 1;
	else
		return 0;
}

static u8 _dpk_agc(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
		   enum rtw89_rf_path path, u8 kidx, u8 init_txagc,
		   bool loss_only)
{
#define DPK_AGC_ADJ_LMT 6
#define DPK_DGAIN_UPPER 1922
#define DPK_DGAIN_LOWER 342
#define DPK_RXBB_UPPER 0x1f
#define DPK_RXBB_LOWER 0
#define DPK_GL_CRIT 7
	u8 tmp_txagc, tmp_rxbb = 0, tmp_gl_idx = 0;
	u8 agc_cnt = 0;
	bool limited_rxbb = false;
	s8 offset = 0;
	u16 dgain = 0;
	u8 step = DPK_AGC_STEP_SYNC_DGAIN;
	bool goout = false;

	tmp_txagc = init_txagc;

	do {
		switch (step) {
		case DPK_AGC_STEP_SYNC_DGAIN:
			if (_dpk_sync(rtwdev, phy, path, kidx)) {
				tmp_txagc = DPK_TXAGC_INVAL;
				goout = true;
				break;
			}

			dgain = _dpk_dgain_read(rtwdev);

			if (loss_only || limited_rxbb)
				step = DPK_AGC_STEP_GAIN_LOSS_IDX;
			else
				step = DPK_AGC_STEP_GAIN_ADJ;
			break;

		case DPK_AGC_STEP_GAIN_ADJ:
			tmp_rxbb = (u8)rtw89_read_rf(rtwdev, path, RR_MOD, RR_MOD_M_RXBB);
			offset = _dpk_dgain_mapping(rtwdev, dgain);

			if (tmp_rxbb + offset > DPK_RXBB_UPPER) {
				tmp_rxbb = DPK_RXBB_UPPER;
				limited_rxbb = true;
			} else if (tmp_rxbb + offset < DPK_RXBB_LOWER) {
				tmp_rxbb = DPK_RXBB_LOWER;
				limited_rxbb = true;
			} else {
				tmp_rxbb = tmp_rxbb + offset;
			}

			rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_M_RXBB, tmp_rxbb);
			rtw89_debug(rtwdev, RTW89_DBG_RFK,
				    "[DPK] Adjust RXBB (%d) = 0x%x\n", offset,
				    tmp_rxbb);
			if (offset != 0 || agc_cnt == 0) {
				if (rtwdev->hal.current_band_width < RTW89_CHANNEL_WIDTH_80)
					_dpk_bypass_rxcfir(rtwdev, path, true);
				else
					_dpk_lbk_rxiqk(rtwdev, phy, path);
			}
			if (dgain > DPK_DGAIN_UPPER || dgain < DPK_DGAIN_LOWER)
				step = DPK_AGC_STEP_SYNC_DGAIN;
			else
				step = DPK_AGC_STEP_GAIN_LOSS_IDX;

			agc_cnt++;
			break;

		case DPK_AGC_STEP_GAIN_LOSS_IDX:
			_dpk_gainloss(rtwdev, phy, path, kidx);
			tmp_gl_idx = _dpk_gainloss_read(rtwdev);

			if ((tmp_gl_idx == 0 && _dpk_pas_read(rtwdev, true)) ||
			    tmp_gl_idx > DPK_GL_CRIT)
				step = DPK_AGC_STEP_GL_GT_CRITERION;
			else if (tmp_gl_idx == 0)
				step = DPK_AGC_STEP_GL_LT_CRITERION;
			else
				step = DPK_AGC_STEP_SET_TX_GAIN;
			break;

		case DPK_AGC_STEP_GL_GT_CRITERION:
			if (tmp_txagc == DPK_TXAGC_LOWER) {
				goout = true;
				rtw89_debug(rtwdev, RTW89_DBG_RFK,
					    "[DPK] Txagc@lower bound!!\n");
			} else {
				tmp_txagc = _dpk_set_offset(rtwdev, path, 3);
			}
			step = DPK_AGC_STEP_GAIN_LOSS_IDX;
			agc_cnt++;
			break;

		case DPK_AGC_STEP_GL_LT_CRITERION:
			if (tmp_txagc == DPK_TXAGC_UPPER) {
				goout = true;
				rtw89_debug(rtwdev, RTW89_DBG_RFK,
					    "[DPK] Txagc@upper bound!!\n");
			} else {
				tmp_txagc = _dpk_set_offset(rtwdev, path, -2);
			}
			step = DPK_AGC_STEP_GAIN_LOSS_IDX;
			agc_cnt++;
			break;

		case DPK_AGC_STEP_SET_TX_GAIN:
			tmp_txagc = _dpk_set_offset(rtwdev, path, tmp_gl_idx);
			goout = true;
			agc_cnt++;
			break;

		default:
			goout = true;
			break;
		}
	} while (!goout && (agc_cnt < DPK_AGC_ADJ_LMT));

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DPK] Txagc / RXBB for DPK = 0x%x / 0x%x\n", tmp_txagc,
		    tmp_rxbb);

	return tmp_txagc;
}

static void _dpk_set_mdpd_para(struct rtw89_dev *rtwdev, u8 order)
{
	switch (order) {
	case 0:
		rtw89_phy_write32_mask(rtwdev, R_LDL_NORM, B_LDL_NORM_OP, order);
		rtw89_phy_write32_mask(rtwdev, R_LDL_NORM, B_LDL_NORM_PN, 0x3);
		rtw89_phy_write32_mask(rtwdev, R_MDPK_SYNC, B_MDPK_SYNC_MAN, 0x1);
		break;
	case 1:
		rtw89_phy_write32_mask(rtwdev, R_LDL_NORM, B_LDL_NORM_OP, order);
		rtw89_phy_write32_clr(rtwdev, R_LDL_NORM, B_LDL_NORM_PN);
		rtw89_phy_write32_clr(rtwdev, R_MDPK_SYNC, B_MDPK_SYNC_MAN);
		break;
	case 2:
		rtw89_phy_write32_mask(rtwdev, R_LDL_NORM, B_LDL_NORM_OP, order);
		rtw89_phy_write32_clr(rtwdev, R_LDL_NORM, B_LDL_NORM_PN);
		rtw89_phy_write32_clr(rtwdev, R_MDPK_SYNC, B_MDPK_SYNC_MAN);
		break;
	default:
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[DPK] Wrong MDPD order!!(0x%x)\n", order);
		break;
	}

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DPK] Set MDPD order to 0x%x for IDL\n", order);
}

static void _dpk_idl_mpa(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			 enum rtw89_rf_path path, u8 kidx, u8 gain)
{
	_dpk_set_mdpd_para(rtwdev, 0x0);
	_dpk_table_select(rtwdev, path, kidx, 1);
	_dpk_one_shot(rtwdev, phy, path, MDPK_IDL);
}

static void _dpk_fill_result(struct rtw89_dev *rtwdev,
			     enum rtw89_rf_path path, u8 kidx, u8 gain,
			     u8 txagc)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;

	u16 pwsf = 0x78;
	u8 gs = 0x5b;

	rtw89_phy_write32_mask(rtwdev, R_COEF_SEL + (path << 8), B_COEF_SEL_MDPD, kidx);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DPK] Fill txagc/ pwsf/ gs = 0x%x/ 0x%x/ 0x%x\n", txagc,
		    pwsf, gs);

	dpk->bp[path][kidx].txagc_dpk = txagc;
	rtw89_phy_write32_mask(rtwdev, R_TXAGC_RFK + (path << 8),
			       0x3F << ((gain << 3) + (kidx << 4)), txagc);

	dpk->bp[path][kidx].pwsf = pwsf;
	rtw89_phy_write32_mask(rtwdev, R_DPD_BND + (path << 8) + (kidx << 2),
			       0x1FF << (gain << 4), pwsf);

	rtw89_phy_write32_mask(rtwdev, R_LOAD_COEF + (path << 8), B_LOAD_COEF_MDPD, 0x1);
	rtw89_phy_write32_clr(rtwdev, R_LOAD_COEF + (path << 8), B_LOAD_COEF_MDPD);

	dpk->bp[path][kidx].gs = gs;
	rtw89_phy_write32_mask(rtwdev, R_DPD_CH0A + (path << 8) + (kidx << 2),
			       MASKDWORD, 0x065b5b5b);

	rtw89_phy_write32_clr(rtwdev, R_DPD_V1 + (path << 8), MASKDWORD);

	rtw89_phy_write32_clr(rtwdev, R_MDPK_SYNC, B_MDPK_SYNC_SEL);
}

static bool _dpk_reload_check(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			      enum rtw89_rf_path path)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;
	bool is_reload = false;
	u8 idx, cur_band, cur_ch;

	cur_band = rtwdev->hal.current_band_type;
	cur_ch = rtwdev->hal.current_channel;

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

static bool _dpk_main(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
		      enum rtw89_rf_path path, u8 gain)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;
	u8 txagc = 0, kidx = dpk->cur_idx[path];
	bool is_fail = false;

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DPK] ========= S%d[%d] DPK Start =========\n", path,
		    kidx);

	_rf_direct_cntrl(rtwdev, path, false);
	txagc = _dpk_set_tx_pwr(rtwdev, gain, path);
	_dpk_rf_setting(rtwdev, gain, path, kidx);
	_dpk_rx_dck(rtwdev, phy, path);

	_dpk_kip_setting(rtwdev, path, kidx);
	_dpk_manual_txcfir(rtwdev, path, true);
	txagc = _dpk_agc(rtwdev, phy, path, kidx, txagc, false);
	if (txagc == DPK_TXAGC_INVAL)
		is_fail = true;
	_dpk_get_thermal(rtwdev, kidx, path);

	_dpk_idl_mpa(rtwdev, phy, path, kidx, gain);
	rtw89_write_rf(rtwdev, path, RR_MOD, RR_MOD_MASK, RR_MOD_V_RX);
	_dpk_fill_result(rtwdev, path, kidx, gain, txagc);
	_dpk_manual_txcfir(rtwdev, path, false);

	if (!is_fail)
		dpk->bp[path][kidx].path_ok = true;
	else
		dpk->bp[path][kidx].path_ok = false;

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] S%d[%d] DPK %s\n", path, kidx,
		    is_fail ? "Check" : "Success");

	return is_fail;
}

static void _dpk_cal_select(struct rtw89_dev *rtwdev, bool force,
			    enum rtw89_phy_idx phy, u8 kpath)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;
	u32 backup_bb_val[BACKUP_BB_REGS_NR];
	u32 backup_rf_val[RTW8852A_DPK_RF_PATH][BACKUP_RF_REGS_NR];
	u32 kip_bkup[RTW8852A_DPK_RF_PATH][RTW8852A_DPK_KIP_REG_NUM] = {{0}};
	u32 kip_reg[] = {R_RXIQC, R_IQK_RES};
	u8 path;
	bool is_fail = true, reloaded[RTW8852A_DPK_RF_PATH] = {false};

	if (dpk->is_dpk_reload_en) {
		for (path = 0; path < RTW8852A_DPK_RF_PATH; path++) {
			if (!(kpath & BIT(path)))
				continue;

			reloaded[path] = _dpk_reload_check(rtwdev, phy, path);
			if (!reloaded[path] && dpk->bp[path][0].ch != 0)
				dpk->cur_idx[path] = !dpk->cur_idx[path];
			else
				_dpk_onoff(rtwdev, path, false);
		}
	} else {
		for (path = 0; path < RTW8852A_DPK_RF_PATH; path++)
			dpk->cur_idx[path] = 0;
	}

	if ((kpath == RF_A && reloaded[RF_PATH_A]) ||
	    (kpath == RF_B && reloaded[RF_PATH_B]) ||
	    (kpath == RF_AB && reloaded[RF_PATH_A] && reloaded[RF_PATH_B]))
		return;

	_rfk_backup_bb_reg(rtwdev, &backup_bb_val[0]);

	for (path = 0; path < RTW8852A_DPK_RF_PATH; path++) {
		if (!(kpath & BIT(path)) || reloaded[path])
			continue;
		if (rtwdev->is_tssi_mode[path])
			_dpk_tssi_pause(rtwdev, path, true);
		_dpk_bkup_kip(rtwdev, kip_reg, kip_bkup, path);
		_rfk_backup_rf_reg(rtwdev, &backup_rf_val[path][0], path);
		_dpk_information(rtwdev, phy, path);
	}

	_dpk_bb_afe_setting(rtwdev, phy, path, kpath);

	for (path = 0; path < RTW8852A_DPK_RF_PATH; path++) {
		if (!(kpath & BIT(path)) || reloaded[path])
			continue;

		is_fail = _dpk_main(rtwdev, phy, path, 1);
		_dpk_onoff(rtwdev, path, is_fail);
	}

	_dpk_bb_afe_restore(rtwdev, phy, path, kpath);
	_rfk_restore_bb_reg(rtwdev, &backup_bb_val[0]);

	for (path = 0; path < RTW8852A_DPK_RF_PATH; path++) {
		if (!(kpath & BIT(path)) || reloaded[path])
			continue;

		_dpk_kip_restore(rtwdev, path);
		_dpk_reload_kip(rtwdev, kip_reg, kip_bkup, path);
		_rfk_restore_rf_reg(rtwdev, &backup_rf_val[path][0], path);
		if (rtwdev->is_tssi_mode[path])
			_dpk_tssi_pause(rtwdev, path, false);
	}
}

static bool _dpk_bypass_check(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy)
{
	struct rtw89_fem_info *fem = &rtwdev->fem;

	if (fem->epa_2g && rtwdev->hal.current_band_type == RTW89_BAND_2G) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[DPK] Skip DPK due to 2G_ext_PA exist!!\n");
		return true;
	} else if (fem->epa_5g && rtwdev->hal.current_band_type == RTW89_BAND_5G) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[DPK] Skip DPK due to 5G_ext_PA exist!!\n");
		return true;
	}

	return false;
}

static void _dpk_force_bypass(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy)
{
	u8 path, kpath;

	kpath = _kpath(rtwdev, phy);

	for (path = 0; path < RTW8852A_DPK_RF_PATH; path++) {
		if (kpath & BIT(path))
			_dpk_onoff(rtwdev, path, true);
	}
}

static void _dpk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy, bool force)
{
	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[DPK] ****** DPK Start (Ver: 0x%x, Cv: %d, RF_para: %d) ******\n",
		    RTW8852A_DPK_VER, rtwdev->hal.cv,
		    RTW8852A_RF_REL_VERSION);

	if (_dpk_bypass_check(rtwdev, phy))
		_dpk_force_bypass(rtwdev, phy);
	else
		_dpk_cal_select(rtwdev, force, phy, _kpath(rtwdev, phy));
}

static void _dpk_onoff(struct rtw89_dev *rtwdev,
		       enum rtw89_rf_path path, bool off)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;
	u8 val, kidx = dpk->cur_idx[path];

	val = dpk->is_dpk_enable && !off && dpk->bp[path][kidx].path_ok;

	rtw89_phy_write32_mask(rtwdev, R_DPD_CH0A + (path << 8) + (kidx << 2),
			       MASKBYTE3, 0x6 | val);

	rtw89_debug(rtwdev, RTW89_DBG_RFK, "[DPK] S%d[%d] DPK %s !!!\n", path,
		    kidx, dpk->is_dpk_enable && !off ? "enable" : "disable");
}

static void _dpk_track(struct rtw89_dev *rtwdev)
{
	struct rtw89_dpk_info *dpk = &rtwdev->dpk;
	struct rtw89_tssi_info *tssi_info = &rtwdev->tssi;
	u8 path, kidx;
	u8 trk_idx = 0, txagc_rf = 0;
	s8 txagc_bb = 0, txagc_bb_tp = 0, ini_diff = 0, txagc_ofst = 0;
	u16 pwsf[2];
	u8 cur_ther;
	s8 delta_ther[2] = {0};

	for (path = 0; path < RTW8852A_DPK_RF_PATH; path++) {
		kidx = dpk->cur_idx[path];

		rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK,
			    "[DPK_TRK] ================[S%d[%d] (CH %d)]================\n",
			    path, kidx, dpk->bp[path][kidx].ch);

		cur_ther = ewma_thermal_read(&rtwdev->phystat.avg_thermal[path]);

		rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK,
			    "[DPK_TRK] thermal now = %d\n", cur_ther);

		if (dpk->bp[path][kidx].ch != 0 && cur_ther != 0)
			delta_ther[path] = dpk->bp[path][kidx].ther_dpk - cur_ther;

		if (dpk->bp[path][kidx].band == RTW89_BAND_2G)
			delta_ther[path] = delta_ther[path] * 3 / 2;
		else
			delta_ther[path] = delta_ther[path] * 5 / 2;

		txagc_rf = (u8)rtw89_phy_read32_mask(rtwdev, R_TXAGC_BB  + (path << 13),
						     RR_MODOPT_M_TXPWR);

		if (rtwdev->is_tssi_mode[path]) {
			trk_idx = (u8)rtw89_read_rf(rtwdev, path, RR_TXA, RR_TXA_TRK);

			rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK,
				    "[DPK_TRK] txagc_RF / track_idx = 0x%x / %d\n",
				    txagc_rf, trk_idx);

			txagc_bb =
				(s8)rtw89_phy_read32_mask(rtwdev,
							  R_TXAGC_BB + (path << 13),
							  MASKBYTE2);
			txagc_bb_tp =
				(u8)rtw89_phy_read32_mask(rtwdev,
							  R_TXAGC_TP + (path << 13),
							  B_TXAGC_TP);

			rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK,
				    "[DPK_TRK] txagc_bb_tp / txagc_bb = 0x%x / 0x%x\n",
				    txagc_bb_tp, txagc_bb);

			txagc_ofst =
				(s8)rtw89_phy_read32_mask(rtwdev,
							  R_TXAGC_BB + (path << 13),
							  MASKBYTE3);

			rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK,
				    "[DPK_TRK] txagc_offset / delta_ther = %d / %d\n",
				    txagc_ofst, delta_ther[path]);

			if (rtw89_phy_read32_mask(rtwdev, R_DPD_COM + (path << 8),
						  BIT(15)) == 0x1)
				txagc_ofst = 0;

			if (txagc_rf != 0 && cur_ther != 0)
				ini_diff = txagc_ofst + delta_ther[path];

			if (rtw89_phy_read32_mask(rtwdev, R_P0_TXDPD + (path << 13),
						  B_P0_TXDPD) == 0x0) {
				pwsf[0] = dpk->bp[path][kidx].pwsf + txagc_bb_tp -
					  txagc_bb + ini_diff +
					  tssi_info->extra_ofst[path];
				pwsf[1] = dpk->bp[path][kidx].pwsf + txagc_bb_tp -
					  txagc_bb + ini_diff +
					  tssi_info->extra_ofst[path];
			} else {
				pwsf[0] = dpk->bp[path][kidx].pwsf + ini_diff +
					  tssi_info->extra_ofst[path];
				pwsf[1] = dpk->bp[path][kidx].pwsf + ini_diff +
					  tssi_info->extra_ofst[path];
			}

		} else {
			pwsf[0] = (dpk->bp[path][kidx].pwsf + delta_ther[path]) & 0x1ff;
			pwsf[1] = (dpk->bp[path][kidx].pwsf + delta_ther[path]) & 0x1ff;
		}

		if (rtw89_phy_read32_mask(rtwdev, R_DPK_TRK, B_DPK_TRK_DIS) == 0x0 &&
		    txagc_rf != 0) {
			rtw89_debug(rtwdev, RTW89_DBG_RFK_TRACK,
				    "[DPK_TRK] New pwsf[0] / pwsf[1] = 0x%x / 0x%x\n",
				    pwsf[0], pwsf[1]);

			rtw89_phy_write32_mask(rtwdev, R_DPD_BND + (path << 8) + (kidx << 2),
					       0x000001FF, pwsf[0]);
			rtw89_phy_write32_mask(rtwdev, R_DPD_BND + (path << 8) + (kidx << 2),
					       0x01FF0000, pwsf[1]);
		}
	}
}

static void _tssi_rf_setting(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			     enum rtw89_rf_path path)
{
	enum rtw89_band band = rtwdev->hal.current_band_type;

	if (band == RTW89_BAND_2G)
		rtw89_write_rf(rtwdev, path, RR_TXPOW, RR_TXPOW_TXG, 0x1);
	else
		rtw89_write_rf(rtwdev, path, RR_TXPOW, RR_TXPOW_TXA, 0x1);
}

static void _tssi_set_sys(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy)
{
	enum rtw89_band band = rtwdev->hal.current_band_type;

	rtw89_rfk_parser(rtwdev, &rtw8852a_tssi_sys_defs_tbl);
	rtw89_rfk_parser_by_cond(rtwdev, band == RTW89_BAND_2G,
				 &rtw8852a_tssi_sys_defs_2g_tbl,
				 &rtw8852a_tssi_sys_defs_5g_tbl);
}

static void _tssi_ini_txpwr_ctrl_bb(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
				    enum rtw89_rf_path path)
{
	enum rtw89_band band = rtwdev->hal.current_band_type;

	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852a_tssi_txpwr_ctrl_bb_defs_a_tbl,
				 &rtw8852a_tssi_txpwr_ctrl_bb_defs_b_tbl);
	rtw89_rfk_parser_by_cond(rtwdev, band == RTW89_BAND_2G,
				 &rtw8852a_tssi_txpwr_ctrl_bb_defs_2g_tbl,
				 &rtw8852a_tssi_txpwr_ctrl_bb_defs_5g_tbl);
}

static void _tssi_ini_txpwr_ctrl_bb_he_tb(struct rtw89_dev *rtwdev,
					  enum rtw89_phy_idx phy,
					  enum rtw89_rf_path path)
{
	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852a_tssi_txpwr_ctrl_bb_he_tb_defs_a_tbl,
				 &rtw8852a_tssi_txpwr_ctrl_bb_he_tb_defs_b_tbl);
}

static void _tssi_set_dck(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			  enum rtw89_rf_path path)
{
	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852a_tssi_dck_defs_a_tbl,
				 &rtw8852a_tssi_dck_defs_b_tbl);
}

static void _tssi_set_tmeter_tbl(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
				 enum rtw89_rf_path path)
{
#define __get_val(ptr, idx)				\
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
	const u8 *thm_up_a = NULL;
	const u8 *thm_down_a = NULL;
	const u8 *thm_up_b = NULL;
	const u8 *thm_down_b = NULL;
	u8 thermal = 0xff;
	s8 thm_ofst[64] = {0};
	u32 tmp = 0;
	u8 i, j;

	switch (subband) {
	default:
	case RTW89_CH_2G:
		thm_up_a = rtw89_8852a_trk_cfg.delta_swingidx_2ga_p;
		thm_down_a = rtw89_8852a_trk_cfg.delta_swingidx_2ga_n;
		thm_up_b = rtw89_8852a_trk_cfg.delta_swingidx_2gb_p;
		thm_down_b = rtw89_8852a_trk_cfg.delta_swingidx_2gb_n;
		break;
	case RTW89_CH_5G_BAND_1:
		thm_up_a = rtw89_8852a_trk_cfg.delta_swingidx_5ga_p[0];
		thm_down_a = rtw89_8852a_trk_cfg.delta_swingidx_5ga_n[0];
		thm_up_b = rtw89_8852a_trk_cfg.delta_swingidx_5gb_p[0];
		thm_down_b = rtw89_8852a_trk_cfg.delta_swingidx_5gb_n[0];
		break;
	case RTW89_CH_5G_BAND_3:
		thm_up_a = rtw89_8852a_trk_cfg.delta_swingidx_5ga_p[1];
		thm_down_a = rtw89_8852a_trk_cfg.delta_swingidx_5ga_n[1];
		thm_up_b = rtw89_8852a_trk_cfg.delta_swingidx_5gb_p[1];
		thm_down_b = rtw89_8852a_trk_cfg.delta_swingidx_5gb_n[1];
		break;
	case RTW89_CH_5G_BAND_4:
		thm_up_a = rtw89_8852a_trk_cfg.delta_swingidx_5ga_p[2];
		thm_down_a = rtw89_8852a_trk_cfg.delta_swingidx_5ga_n[2];
		thm_up_b = rtw89_8852a_trk_cfg.delta_swingidx_5gb_p[2];
		thm_down_b = rtw89_8852a_trk_cfg.delta_swingidx_5gb_n[2];
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
				tmp = __get_val(thm_ofst, i);
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
				tmp = __get_val(thm_ofst, i);
				rtw89_phy_write32(rtwdev, R_TSSI_THOF + i, tmp);

				rtw89_debug(rtwdev, RTW89_DBG_TSSI,
					    "[TSSI] write 0x%x val=0x%08x\n",
					    0x7c00 + i, tmp);
			}
		}
		rtw89_phy_write32_mask(rtwdev, R_P1_RFCTM, R_P1_RFCTM_RDY, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_P1_RFCTM, R_P1_RFCTM_RDY, 0x0);
	}
#undef __get_val
}

static void _tssi_set_dac_gain_tbl(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
				   enum rtw89_rf_path path)
{
	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852a_tssi_dac_gain_tbl_defs_a_tbl,
				 &rtw8852a_tssi_dac_gain_tbl_defs_b_tbl);
}

static void _tssi_slope_cal_org(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
				enum rtw89_rf_path path)
{
	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852a_tssi_slope_cal_org_defs_a_tbl,
				 &rtw8852a_tssi_slope_cal_org_defs_b_tbl);
}

static void _tssi_set_rf_gap_tbl(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
				 enum rtw89_rf_path path)
{
	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852a_tssi_rf_gap_tbl_defs_a_tbl,
				 &rtw8852a_tssi_rf_gap_tbl_defs_b_tbl);
}

static void _tssi_set_slope(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			    enum rtw89_rf_path path)
{
	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852a_tssi_slope_defs_a_tbl,
				 &rtw8852a_tssi_slope_defs_b_tbl);
}

static void _tssi_set_track(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			    enum rtw89_rf_path path)
{
	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852a_tssi_track_defs_a_tbl,
				 &rtw8852a_tssi_track_defs_b_tbl);
}

static void _tssi_set_txagc_offset_mv_avg(struct rtw89_dev *rtwdev,
					  enum rtw89_phy_idx phy,
					  enum rtw89_rf_path path)
{
	rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
				 &rtw8852a_tssi_txagc_ofst_mv_avg_defs_a_tbl,
				 &rtw8852a_tssi_txagc_ofst_mv_avg_defs_b_tbl);
}

static void _tssi_pak(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
		      enum rtw89_rf_path path)
{
	u8 subband = rtwdev->hal.current_subband;

	switch (subband) {
	default:
	case RTW89_CH_2G:
		rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
					 &rtw8852a_tssi_pak_defs_a_2g_tbl,
					 &rtw8852a_tssi_pak_defs_b_2g_tbl);
		break;
	case RTW89_CH_5G_BAND_1:
		rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
					 &rtw8852a_tssi_pak_defs_a_5g_1_tbl,
					 &rtw8852a_tssi_pak_defs_b_5g_1_tbl);
		break;
	case RTW89_CH_5G_BAND_3:
		rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
					 &rtw8852a_tssi_pak_defs_a_5g_3_tbl,
					 &rtw8852a_tssi_pak_defs_b_5g_3_tbl);
		break;
	case RTW89_CH_5G_BAND_4:
		rtw89_rfk_parser_by_cond(rtwdev, path == RF_PATH_A,
					 &rtw8852a_tssi_pak_defs_a_5g_4_tbl,
					 &rtw8852a_tssi_pak_defs_b_5g_4_tbl);
		break;
	}
}

static void _tssi_enable(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy)
{
	struct rtw89_tssi_info *tssi_info = &rtwdev->tssi;
	u8 i;

	for (i = 0; i < RF_PATH_NUM_8852A; i++) {
		_tssi_set_track(rtwdev, phy, i);
		_tssi_set_txagc_offset_mv_avg(rtwdev, phy, i);

		rtw89_rfk_parser_by_cond(rtwdev, i == RF_PATH_A,
					 &rtw8852a_tssi_enable_defs_a_tbl,
					 &rtw8852a_tssi_enable_defs_b_tbl);

		tssi_info->base_thermal[i] =
			ewma_thermal_read(&rtwdev->phystat.avg_thermal[i]);
		rtwdev->is_tssi_mode[i] = true;
	}
}

static void _tssi_disable(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy)
{
	rtw89_rfk_parser(rtwdev, &rtw8852a_tssi_disable_defs_tbl);

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
	u8 ch = rtwdev->hal.current_channel;
	u32 gidx, gidx_1st, gidx_2nd;
	s8 de_1st = 0;
	s8 de_2nd = 0;
	s8 val;

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

	return val;
}

static s8 _tssi_get_ofdm_trim_de(struct rtw89_dev *rtwdev,
				 enum rtw89_phy_idx phy,
				 enum rtw89_rf_path path)
{
	struct rtw89_tssi_info *tssi_info = &rtwdev->tssi;
	u8 ch = rtwdev->hal.current_channel;
	u32 tgidx, tgidx_1st, tgidx_2nd;
	s8 tde_1st = 0;
	s8 tde_2nd = 0;
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

static void _tssi_set_efuse_to_de(struct rtw89_dev *rtwdev,
				  enum rtw89_phy_idx phy)
{
#define __DE_MASK 0x003ff000
	struct rtw89_tssi_info *tssi_info = &rtwdev->tssi;
	static const u32 r_cck_long[RF_PATH_NUM_8852A] = {0x5858, 0x7858};
	static const u32 r_cck_short[RF_PATH_NUM_8852A] = {0x5860, 0x7860};
	static const u32 r_mcs_20m[RF_PATH_NUM_8852A] = {0x5838, 0x7838};
	static const u32 r_mcs_40m[RF_PATH_NUM_8852A] = {0x5840, 0x7840};
	static const u32 r_mcs_80m[RF_PATH_NUM_8852A] = {0x5848, 0x7848};
	static const u32 r_mcs_80m_80m[RF_PATH_NUM_8852A] = {0x5850, 0x7850};
	static const u32 r_mcs_5m[RF_PATH_NUM_8852A] = {0x5828, 0x7828};
	static const u32 r_mcs_10m[RF_PATH_NUM_8852A] = {0x5830, 0x7830};
	u8 ch = rtwdev->hal.current_channel;
	u8 i, gidx;
	s8 ofdm_de;
	s8 trim_de;
	s32 val;

	rtw89_debug(rtwdev, RTW89_DBG_TSSI, "[TSSI][TRIM]: phy=%d ch=%d\n",
		    phy, ch);

	for (i = 0; i < RF_PATH_NUM_8852A; i++) {
		gidx = _tssi_get_cck_group(rtwdev, ch);
		trim_de = _tssi_get_ofdm_trim_de(rtwdev, phy, i);
		val = tssi_info->tssi_cck[i][gidx] + trim_de;

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI][TRIM]: path=%d cck[%d]=0x%x trim=0x%x\n",
			    i, gidx, tssi_info->tssi_cck[i][gidx], trim_de);

		rtw89_phy_write32_mask(rtwdev, r_cck_long[i], __DE_MASK, val);
		rtw89_phy_write32_mask(rtwdev, r_cck_short[i], __DE_MASK, val);

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI] Set TSSI CCK DE 0x%x[21:12]=0x%x\n",
			    r_cck_long[i],
			    rtw89_phy_read32_mask(rtwdev, r_cck_long[i],
						  __DE_MASK));

		ofdm_de = _tssi_get_ofdm_de(rtwdev, phy, i);
		trim_de = _tssi_get_ofdm_trim_de(rtwdev, phy, i);
		val = ofdm_de + trim_de;

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI][TRIM]: path=%d mcs=0x%x trim=0x%x\n",
			    i, ofdm_de, trim_de);

		rtw89_phy_write32_mask(rtwdev, r_mcs_20m[i], __DE_MASK, val);
		rtw89_phy_write32_mask(rtwdev, r_mcs_40m[i], __DE_MASK, val);
		rtw89_phy_write32_mask(rtwdev, r_mcs_80m[i], __DE_MASK, val);
		rtw89_phy_write32_mask(rtwdev, r_mcs_80m_80m[i], __DE_MASK, val);
		rtw89_phy_write32_mask(rtwdev, r_mcs_5m[i], __DE_MASK, val);
		rtw89_phy_write32_mask(rtwdev, r_mcs_10m[i], __DE_MASK, val);

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI] Set TSSI MCS DE 0x%x[21:12]=0x%x\n",
			    r_mcs_20m[i],
			    rtw89_phy_read32_mask(rtwdev, r_mcs_20m[i],
						  __DE_MASK));
	}
#undef __DE_MASK
}

static void _tssi_track(struct rtw89_dev *rtwdev)
{
	static const u32 tx_gain_scale_table[] = {
		0x400, 0x40e, 0x41d, 0x427, 0x43c, 0x44c, 0x45c, 0x46c,
		0x400, 0x39d, 0x3ab, 0x3b8, 0x3c6, 0x3d4, 0x3e2, 0x3f1
	};
	struct rtw89_tssi_info *tssi_info = &rtwdev->tssi;
	u8 path;
	u8 cur_ther;
	s32 delta_ther = 0, gain_offset_int, gain_offset_float;
	s8 gain_offset;

	rtw89_debug(rtwdev, RTW89_DBG_TSSI, "[TSSI][TRK] %s:\n",
		    __func__);

	if (!rtwdev->is_tssi_mode[RF_PATH_A])
		return;
	if (!rtwdev->is_tssi_mode[RF_PATH_B])
		return;

	for (path = RF_PATH_A; path < RF_PATH_NUM_8852A; path++) {
		if (!tssi_info->tssi_tracking_check[path]) {
			rtw89_debug(rtwdev, RTW89_DBG_TSSI, "[TSSI][TRK] return!!!\n");
			continue;
		}

		cur_ther = (u8)rtw89_phy_read32_mask(rtwdev,
						  R_TSSI_THER + (path << 13),
						  B_TSSI_THER);

		if (cur_ther == 0 || tssi_info->base_thermal[path] == 0)
			continue;

		delta_ther = cur_ther - tssi_info->base_thermal[path];

		gain_offset = (s8)delta_ther * 15 / 10;

		tssi_info->extra_ofst[path] = gain_offset;

		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI][TRK] base_thermal=%d gain_offset=0x%x path=%d\n",
			    tssi_info->base_thermal[path], gain_offset, path);

		gain_offset_int = gain_offset >> 3;
		gain_offset_float = gain_offset & 7;

		if (gain_offset_int > 15)
			gain_offset_int = 15;
		else if (gain_offset_int < -16)
			gain_offset_int = -16;

		rtw89_phy_write32_mask(rtwdev, R_DPD_OFT_EN + (path << 13),
				       B_DPD_OFT_EN, 0x1);

		rtw89_phy_write32_mask(rtwdev, R_TXGAIN_SCALE + (path << 13),
				       B_TXGAIN_SCALE_EN, 0x1);

		rtw89_phy_write32_mask(rtwdev, R_DPD_OFT_ADDR + (path << 13),
				       B_DPD_OFT_ADDR, gain_offset_int);

		rtw89_phy_write32_mask(rtwdev, R_TXGAIN_SCALE + (path << 13),
				       B_TXGAIN_SCALE_OFT,
				       tx_gain_scale_table[gain_offset_float]);
	}
}

static void _tssi_high_power(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy)
{
	struct rtw89_tssi_info *tssi_info = &rtwdev->tssi;
	u8 ch = rtwdev->hal.current_channel, ch_tmp;
	u8 bw = rtwdev->hal.current_band_width;
	u8 subband = rtwdev->hal.current_subband;
	s8 power;
	s32 xdbm;

	if (bw == RTW89_CHANNEL_WIDTH_40)
		ch_tmp = ch - 2;
	else if (bw == RTW89_CHANNEL_WIDTH_80)
		ch_tmp = ch - 6;
	else
		ch_tmp = ch;

	power = rtw89_phy_read_txpwr_limit(rtwdev, bw, RTW89_1TX,
					   RTW89_RS_MCS, RTW89_NONBF, ch_tmp);

	xdbm = power * 100 / 4;

	rtw89_debug(rtwdev, RTW89_DBG_TSSI, "[TSSI] %s: phy=%d xdbm=%d\n",
		    __func__, phy, xdbm);

	if (xdbm > 1800 && subband == RTW89_CH_2G) {
		tssi_info->tssi_tracking_check[RF_PATH_A] = true;
		tssi_info->tssi_tracking_check[RF_PATH_B] = true;
	} else {
		rtw89_rfk_parser(rtwdev, &rtw8852a_tssi_tracking_defs_tbl);
		tssi_info->extra_ofst[RF_PATH_A] = 0;
		tssi_info->extra_ofst[RF_PATH_B] = 0;
		tssi_info->tssi_tracking_check[RF_PATH_A] = false;
		tssi_info->tssi_tracking_check[RF_PATH_B] = false;
	}
}

static void _tssi_hw_tx(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			u8 path, s16 pwr_dbm, u8 enable)
{
	rtw8852a_bb_set_plcp_tx(rtwdev);
	rtw8852a_bb_cfg_tx_path(rtwdev, path);
	rtw8852a_bb_set_power(rtwdev, pwr_dbm, phy);
	rtw8852a_bb_set_pmac_pkt_tx(rtwdev, enable, 20, 5000, 0, phy);
}

static void _tssi_pre_tx(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy)
{
	struct rtw89_tssi_info *tssi_info = &rtwdev->tssi;
	const struct rtw89_chip_info *mac_reg = rtwdev->chip;
	u8 ch = rtwdev->hal.current_channel, ch_tmp;
	u8 bw = rtwdev->hal.current_band_width;
	u32 tx_en;
	u8 phy_map = rtw89_btc_phymap(rtwdev, phy, 0);
	s8 power;
	s16 xdbm;
	u32 i, tx_counter = 0;

	if (bw == RTW89_CHANNEL_WIDTH_40)
		ch_tmp = ch - 2;
	else if (bw == RTW89_CHANNEL_WIDTH_80)
		ch_tmp = ch - 6;
	else
		ch_tmp = ch;

	power = rtw89_phy_read_txpwr_limit(rtwdev, RTW89_CHANNEL_WIDTH_20, RTW89_1TX,
					   RTW89_RS_OFDM, RTW89_NONBF, ch_tmp);

	xdbm = (power * 100) >> mac_reg->txpwr_factor_mac;

	if (xdbm > 1800)
		xdbm = 68;
	else
		xdbm = power * 2;

	rtw89_debug(rtwdev, RTW89_DBG_TSSI,
		    "[TSSI] %s: phy=%d org_power=%d xdbm=%d\n",
		    __func__, phy, power, xdbm);

	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_DPK, BTC_WRFK_START);
	rtw89_chip_stop_sch_tx(rtwdev, phy, &tx_en, RTW89_SCH_TX_SEL_ALL);
	_wait_rx_mode(rtwdev, _kpath(rtwdev, phy));
	tx_counter = rtw89_phy_read32_mask(rtwdev, R_TX_COUNTER, MASKLWORD);

	_tssi_hw_tx(rtwdev, phy, RF_PATH_AB, xdbm, true);
	mdelay(15);
	_tssi_hw_tx(rtwdev, phy, RF_PATH_AB, xdbm, false);

	tx_counter = rtw89_phy_read32_mask(rtwdev, R_TX_COUNTER, MASKLWORD) -
		    tx_counter;

	if (rtw89_phy_read32_mask(rtwdev, R_TXAGC_BB, MASKHWORD) != 0xc000 &&
	    rtw89_phy_read32_mask(rtwdev, R_TXAGC_BB, MASKHWORD) != 0x0) {
		for (i = 0; i < 6; i++) {
			tssi_info->default_txagc_offset[RF_PATH_A] =
				rtw89_phy_read32_mask(rtwdev, R_TXAGC_BB,
						      MASKBYTE3);

			if (tssi_info->default_txagc_offset[RF_PATH_A] != 0x0)
				break;
		}
	}

	if (rtw89_phy_read32_mask(rtwdev, R_TXAGC_BB_S1, MASKHWORD) != 0xc000 &&
	    rtw89_phy_read32_mask(rtwdev, R_TXAGC_BB_S1, MASKHWORD) != 0x0) {
		for (i = 0; i < 6; i++) {
			tssi_info->default_txagc_offset[RF_PATH_B] =
				rtw89_phy_read32_mask(rtwdev, R_TXAGC_BB_S1,
						      MASKBYTE3);

			if (tssi_info->default_txagc_offset[RF_PATH_B] != 0x0)
				break;
		}
	}

	rtw89_debug(rtwdev, RTW89_DBG_TSSI,
		    "[TSSI] %s: tx counter=%d\n",
		    __func__, tx_counter);

	rtw89_debug(rtwdev, RTW89_DBG_TSSI,
		    "[TSSI] Backup R_TXAGC_BB=0x%x R_TXAGC_BB_S1=0x%x\n",
		    tssi_info->default_txagc_offset[RF_PATH_A],
		    tssi_info->default_txagc_offset[RF_PATH_B]);

	rtw8852a_bb_tx_mode_switch(rtwdev, phy, 0);

	rtw89_chip_resume_sch_tx(rtwdev, phy, tx_en);
	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_DPK, BTC_WRFK_STOP);
}

void rtw8852a_rck(struct rtw89_dev *rtwdev)
{
	u8 path;

	for (path = 0; path < 2; path++)
		_rck(rtwdev, path);
}

void rtw8852a_dack(struct rtw89_dev *rtwdev)
{
	u8 phy_map = rtw89_btc_phymap(rtwdev, RTW89_PHY_0, 0);

	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_DACK, BTC_WRFK_START);
	_dac_cal(rtwdev, false);
	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_DACK, BTC_WRFK_STOP);
}

void rtw8852a_iqk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx)
{
	u32 tx_en;
	u8 phy_map = rtw89_btc_phymap(rtwdev, phy_idx, 0);

	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_IQK, BTC_WRFK_START);
	rtw89_chip_stop_sch_tx(rtwdev, phy_idx, &tx_en, RTW89_SCH_TX_SEL_ALL);
	_wait_rx_mode(rtwdev, _kpath(rtwdev, phy_idx));

	_iqk_init(rtwdev);
	if (rtwdev->dbcc_en)
		_iqk_dbcc(rtwdev, phy_idx);
	else
		_iqk(rtwdev, phy_idx, false);

	rtw89_chip_resume_sch_tx(rtwdev, phy_idx, tx_en);
	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_IQK, BTC_WRFK_STOP);
}

void rtw8852a_iqk_track(struct rtw89_dev *rtwdev)
{
	_iqk_track(rtwdev);
}

void rtw8852a_rx_dck(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx,
		     bool is_afe)
{
	u32 tx_en;
	u8 phy_map = rtw89_btc_phymap(rtwdev, phy_idx, 0);

	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_RXDCK, BTC_WRFK_START);
	rtw89_chip_stop_sch_tx(rtwdev, phy_idx, &tx_en, RTW89_SCH_TX_SEL_ALL);
	_wait_rx_mode(rtwdev, _kpath(rtwdev, phy_idx));

	_rx_dck(rtwdev, phy_idx, is_afe);

	rtw89_chip_resume_sch_tx(rtwdev, phy_idx, tx_en);
	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_RXDCK, BTC_WRFK_STOP);
}

void rtw8852a_dpk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx)
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

void rtw8852a_dpk_track(struct rtw89_dev *rtwdev)
{
	_dpk_track(rtwdev);
}

void rtw8852a_tssi(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy)
{
	u8 i;

	rtw89_debug(rtwdev, RTW89_DBG_TSSI, "[TSSI] %s: phy=%d\n",
		    __func__, phy);

	_tssi_disable(rtwdev, phy);

	for (i = RF_PATH_A; i < RF_PATH_NUM_8852A; i++) {
		_tssi_rf_setting(rtwdev, phy, i);
		_tssi_set_sys(rtwdev, phy);
		_tssi_ini_txpwr_ctrl_bb(rtwdev, phy, i);
		_tssi_ini_txpwr_ctrl_bb_he_tb(rtwdev, phy, i);
		_tssi_set_dck(rtwdev, phy, i);
		_tssi_set_tmeter_tbl(rtwdev, phy, i);
		_tssi_set_dac_gain_tbl(rtwdev, phy, i);
		_tssi_slope_cal_org(rtwdev, phy, i);
		_tssi_set_rf_gap_tbl(rtwdev, phy, i);
		_tssi_set_slope(rtwdev, phy, i);
		_tssi_pak(rtwdev, phy, i);
	}

	_tssi_enable(rtwdev, phy);
	_tssi_set_efuse_to_de(rtwdev, phy);
	_tssi_high_power(rtwdev, phy);
	_tssi_pre_tx(rtwdev, phy);
}

void rtw8852a_tssi_scan(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy)
{
	u8 i;

	rtw89_debug(rtwdev, RTW89_DBG_TSSI, "[TSSI] %s: phy=%d\n",
		    __func__, phy);

	if (!rtwdev->is_tssi_mode[RF_PATH_A])
		return;
	if (!rtwdev->is_tssi_mode[RF_PATH_B])
		return;

	_tssi_disable(rtwdev, phy);

	for (i = RF_PATH_A; i < RF_PATH_NUM_8852A; i++) {
		_tssi_rf_setting(rtwdev, phy, i);
		_tssi_set_sys(rtwdev, phy);
		_tssi_set_tmeter_tbl(rtwdev, phy, i);
		_tssi_pak(rtwdev, phy, i);
	}

	_tssi_enable(rtwdev, phy);
	_tssi_set_efuse_to_de(rtwdev, phy);
}

void rtw8852a_tssi_track(struct rtw89_dev *rtwdev)
{
	_tssi_track(rtwdev);
}

static
void _rtw8852a_tssi_avg_scan(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy)
{
	if (!rtwdev->is_tssi_mode[RF_PATH_A] && !rtwdev->is_tssi_mode[RF_PATH_B])
		return;

	/* disable */
	rtw89_rfk_parser(rtwdev, &rtw8852a_tssi_disable_defs_tbl);

	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_AVG, B_P0_TSSI_AVG, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_MV_AVG, B_P0_TSSI_MV_AVG, 0x0);

	rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_AVG, B_P1_TSSI_AVG, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_MV_AVG, B_P1_TSSI_MV_AVG, 0x0);

	/* enable */
	rtw89_rfk_parser(rtwdev, &rtw8852a_tssi_enable_defs_ab_tbl);
}

static
void _rtw8852a_tssi_set_avg(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy)
{
	if (!rtwdev->is_tssi_mode[RF_PATH_A] && !rtwdev->is_tssi_mode[RF_PATH_B])
		return;

	/* disable */
	rtw89_rfk_parser(rtwdev, &rtw8852a_tssi_disable_defs_tbl);

	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_AVG, B_P0_TSSI_AVG, 0x4);
	rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_MV_AVG, B_P0_TSSI_MV_AVG, 0x2);

	rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_AVG, B_P1_TSSI_AVG, 0x4);
	rtw89_phy_write32_mask(rtwdev, R_P1_TSSI_MV_AVG, B_P1_TSSI_MV_AVG, 0x2);

	/* enable */
	rtw89_rfk_parser(rtwdev, &rtw8852a_tssi_enable_defs_ab_tbl);
}

static void rtw8852a_tssi_set_avg(struct rtw89_dev *rtwdev,
				  enum rtw89_phy_idx phy, bool enable)
{
	if (!rtwdev->is_tssi_mode[RF_PATH_A] && !rtwdev->is_tssi_mode[RF_PATH_B])
		return;

	if (enable) {
		/* SCAN_START */
		_rtw8852a_tssi_avg_scan(rtwdev, phy);
	} else {
		/* SCAN_END */
		_rtw8852a_tssi_set_avg(rtwdev, phy);
	}
}

static void rtw8852a_tssi_default_txagc(struct rtw89_dev *rtwdev,
					enum rtw89_phy_idx phy, bool enable)
{
	struct rtw89_tssi_info *tssi_info = &rtwdev->tssi;
	u8 i;

	if (!rtwdev->is_tssi_mode[RF_PATH_A] && !rtwdev->is_tssi_mode[RF_PATH_B])
		return;

	if (enable) {
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

void rtw8852a_wifi_scan_notify(struct rtw89_dev *rtwdev,
			       bool scan_start, enum rtw89_phy_idx phy_idx)
{
	if (scan_start) {
		rtw8852a_tssi_default_txagc(rtwdev, phy_idx, true);
		rtw8852a_tssi_set_avg(rtwdev, phy_idx, true);
	} else {
		rtw8852a_tssi_default_txagc(rtwdev, phy_idx, false);
		rtw8852a_tssi_set_avg(rtwdev, phy_idx, false);
	}
}
