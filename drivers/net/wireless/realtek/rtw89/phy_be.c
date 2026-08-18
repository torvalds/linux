// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2023  Realtek Corporation
 */

#include "chan.h"
#include "debug.h"
#include "mac.h"
#include "phy.h"
#include "reg.h"

static const struct rtw89_ccx_regs rtw89_ccx_regs_be = {
	.setting_addr = R_CCX,
	.edcca_opt_mask = B_CCX_EDCCA_OPT_MSK_V1,
	.measurement_trig_mask = B_MEASUREMENT_TRIG_MSK,
	.trig_opt_mask = B_CCX_TRIG_OPT_MSK,
	.en_mask = B_CCX_EN_MSK,
	.ifs_cnt_addr = R_IFS_COUNTER,
	.ifs_clm_period_mask = B_IFS_CLM_PERIOD_MSK,
	.ifs_clm_cnt_unit_mask = B_IFS_CLM_COUNTER_UNIT_MSK,
	.ifs_clm_cnt_clear_mask = B_IFS_COUNTER_CLR_MSK,
	.ifs_collect_en_mask = B_IFS_COLLECT_EN,
	.ifs_t1_addr = R_IFS_T1,
	.ifs_t1_th_h_mask = B_IFS_T1_TH_HIGH_MSK,
	.ifs_t1_en_mask = B_IFS_T1_EN_MSK,
	.ifs_t1_th_l_mask = B_IFS_T1_TH_LOW_MSK,
	.ifs_t2_addr = R_IFS_T2,
	.ifs_t2_th_h_mask = B_IFS_T2_TH_HIGH_MSK,
	.ifs_t2_en_mask = B_IFS_T2_EN_MSK,
	.ifs_t2_th_l_mask = B_IFS_T2_TH_LOW_MSK,
	.ifs_t3_addr = R_IFS_T3,
	.ifs_t3_th_h_mask = B_IFS_T3_TH_HIGH_MSK,
	.ifs_t3_en_mask = B_IFS_T3_EN_MSK,
	.ifs_t3_th_l_mask = B_IFS_T3_TH_LOW_MSK,
	.ifs_t4_addr = R_IFS_T4,
	.ifs_t4_th_h_mask = B_IFS_T4_TH_HIGH_MSK,
	.ifs_t4_en_mask = B_IFS_T4_EN_MSK,
	.ifs_t4_th_l_mask = B_IFS_T4_TH_LOW_MSK,
	.ifs_clm_tx_cnt_addr = R_IFS_CLM_TX_CNT_V1,
	.ifs_clm_edcca_excl_cca_fa_mask = B_IFS_CLM_EDCCA_EXCLUDE_CCA_FA_MSK,
	.ifs_clm_tx_cnt_msk = B_IFS_CLM_TX_CNT_MSK,
	.ifs_clm_cca_addr = R_IFS_CLM_CCA_V1,
	.ifs_clm_ofdmcca_excl_fa_mask = B_IFS_CLM_OFDMCCA_EXCLUDE_FA_MSK,
	.ifs_clm_cckcca_excl_fa_mask = B_IFS_CLM_CCKCCA_EXCLUDE_FA_MSK,
	.ifs_clm_fa_addr = R_IFS_CLM_FA_V1,
	.ifs_clm_ofdm_fa_mask = B_IFS_CLM_OFDM_FA_MSK,
	.ifs_clm_cck_fa_mask = B_IFS_CLM_CCK_FA_MSK,
	.ifs_his_addr = R_IFS_HIS_V1,
	.ifs_his_addr2 = R_IFS_HIS_V1,
	.ifs_t4_his_mask = B_IFS_T4_HIS_MSK,
	.ifs_t3_his_mask = B_IFS_T3_HIS_MSK,
	.ifs_t2_his_mask = B_IFS_T2_HIS_MSK,
	.ifs_t1_his_mask = B_IFS_T1_HIS_MSK,
	.ifs_avg_l_addr = R_IFS_AVG_L_V1,
	.ifs_t2_avg_mask = B_IFS_T2_AVG_MSK,
	.ifs_t1_avg_mask = B_IFS_T1_AVG_MSK,
	.ifs_avg_h_addr = R_IFS_AVG_H_V1,
	.ifs_t4_avg_mask = B_IFS_T4_AVG_MSK,
	.ifs_t3_avg_mask = B_IFS_T3_AVG_MSK,
	.ifs_cca_l_addr = R_IFS_CCA_L_V1,
	.ifs_t2_cca_mask = B_IFS_T2_CCA_MSK,
	.ifs_t1_cca_mask = B_IFS_T1_CCA_MSK,
	.ifs_cca_h_addr = R_IFS_CCA_H_V1,
	.ifs_t4_cca_mask = B_IFS_T4_CCA_MSK,
	.ifs_t3_cca_mask = B_IFS_T3_CCA_MSK,
	.ifs_total_addr = R_IFSCNT_V1,
	.ifs_cnt_done_mask = B_IFSCNT_DONE_MSK,
	.ifs_total_mask = B_IFSCNT_TOTAL_CNT_MSK,
	.nhm = R_NHM_BE,
	.nhm_ready = B_NHM_READY_BE_MSK,
	.nhm_config = R_NHM_CFG,
	.nhm_period_mask = B_NHM_PERIOD_MSK,
	.nhm_unit_mask = B_NHM_COUNTER_MSK,
	.nhm_include_cca_mask = B_NHM_INCLUDE_CCA_MSK,
	.nhm_en_mask = B_NHM_EN_MSK,
	.nhm_method = R_NHM_TH9,
	.nhm_pwr_method_msk = B_NHM_PWDB_METHOD_MSK,
	.edcca_clm_rdy = R_CLM_EDCCA_RDY_V1,
	.edcca_clm_rdy_mask = B_CLM_EDCCA_RDY,
	.edcca_clm_cnt = R_CLM_EDCCA_RESULT_V1,
	.edcca_clm_cnt_mask = B_CLM_EDCCA_RESULT,
};

static const struct rtw89_ccx_regs rtw89_ccx_regs_be_v1 = {
	.setting_addr = R_CCX_BE4,
	.edcca_opt_mask = B_CCX_EDCCA_OPT_MSK_V1,
	.measurement_trig_mask = B_MEASUREMENT_TRIG_MSK,
	.trig_opt_mask = B_CCX_TRIG_OPT_MSK,
	.en_mask = B_CCX_EN_MSK,
	.ifs_cnt_addr = R_IFS_COUNTER_BE4,
	.ifs_clm_period_mask = B_IFS_CLM_PERIOD_MSK,
	.ifs_clm_cnt_unit_mask = B_IFS_CLM_COUNTER_UNIT_MSK,
	.ifs_clm_cnt_clear_mask = B_IFS_COUNTER_CLR_MSK,
	.ifs_collect_en_mask = B_IFS_COLLECT_EN,
	.ifs_t1_addr = R_IFS_T1_BE4,
	.ifs_t1_th_h_mask = B_IFS_T1_TH_HIGH_MSK,
	.ifs_t1_en_mask = B_IFS_T1_EN_MSK,
	.ifs_t1_th_l_mask = B_IFS_T1_TH_LOW_MSK,
	.ifs_t2_addr = R_IFS_T2_BE4,
	.ifs_t2_th_h_mask = B_IFS_T2_TH_HIGH_MSK,
	.ifs_t2_en_mask = B_IFS_T2_EN_MSK,
	.ifs_t2_th_l_mask = B_IFS_T2_TH_LOW_MSK,
	.ifs_t3_addr = R_IFS_T3_BE4,
	.ifs_t3_th_h_mask = B_IFS_T3_TH_HIGH_MSK,
	.ifs_t3_en_mask = B_IFS_T3_EN_MSK,
	.ifs_t3_th_l_mask = B_IFS_T3_TH_LOW_MSK,
	.ifs_t4_addr = R_IFS_T4_BE4,
	.ifs_t4_th_h_mask = B_IFS_T4_TH_HIGH_MSK,
	.ifs_t4_en_mask = B_IFS_T4_EN_MSK,
	.ifs_t4_th_l_mask = B_IFS_T4_TH_LOW_MSK,
	.ifs_clm_tx_cnt_addr = R_IFS_CLM_TX_CNT_BE4,
	.ifs_clm_edcca_excl_cca_fa_mask = B_IFS_CLM_EDCCA_EXCLUDE_CCA_FA_MSK,
	.ifs_clm_tx_cnt_msk = B_IFS_CLM_TX_CNT_MSK,
	.ifs_clm_cca_addr = R_IFS_CLM_CCA_BE4,
	.ifs_clm_ofdmcca_excl_fa_mask = B_IFS_CLM_OFDMCCA_EXCLUDE_FA_MSK,
	.ifs_clm_cckcca_excl_fa_mask = B_IFS_CLM_CCKCCA_EXCLUDE_FA_MSK,
	.ifs_clm_fa_addr = R_IFS_CLM_FA_BE4,
	.ifs_clm_ofdm_fa_mask = B_IFS_CLM_OFDM_FA_MSK,
	.ifs_clm_cck_fa_mask = B_IFS_CLM_CCK_FA_MSK,
	.ifs_his_addr = R_IFS_T1_HIS_BE4,
	.ifs_his_addr2 = R_IFS_T3_HIS_BE4, /* for 3/4 */
	.ifs_t4_his_mask = B_IFS_T4_HIS_BE4,
	.ifs_t3_his_mask = B_IFS_T3_HIS_BE4,
	.ifs_t2_his_mask = B_IFS_T2_HIS_BE4,
	.ifs_t1_his_mask = B_IFS_T1_HIS_BE4,
	.ifs_avg_l_addr = R_IFS_T1_AVG_BE4,
	.ifs_t2_avg_mask = B_IFS_T2_AVG_BE4,
	.ifs_t1_avg_mask = B_IFS_T1_AVG_BE4,
	.ifs_avg_h_addr = R_IFS_T3_AVG_BE4,
	.ifs_t4_avg_mask = B_IFS_T4_AVG_BE4,
	.ifs_t3_avg_mask = B_IFS_T3_AVG_BE4,
	.ifs_cca_l_addr = R_IFS_T1_CLM_BE4,
	.ifs_t2_cca_mask = B_IFS_T2_CLM_BE4,
	.ifs_t1_cca_mask = B_IFS_T1_CLM_BE4,
	.ifs_cca_h_addr = R_IFS_T3_CLM_BE4,
	.ifs_t4_cca_mask = B_IFS_T4_CLM_BE4,
	.ifs_t3_cca_mask = B_IFS_T3_CLM_BE4,
	.ifs_total_addr = R_IFS_TOTAL_BE4,
	.ifs_cnt_done_mask = B_IFS_CNT_DONE_BE4,
	.ifs_total_mask = B_IFS_TOTAL_BE4,
	.edcca_clm_rdy = R_CLM_EDCCA_RDY_BE4,
	.edcca_clm_rdy_mask = B_CLM_EDCCA_RDY,
	.edcca_clm_cnt = R_CLM_EDCCA_RESULT_BE4,
	.edcca_clm_cnt_mask = B_CLM_EDCCA_RESULT,
};

static const u32 rtw89_tx_info_reg_be[] = {
	R_TX_INFO_0_0_COMB_V1,
	R_TX_INFO_0_1_COMB_V1,
	R_TX_INFO_1_0_COMB_V1,
	R_TX_INFO_1_1_COMB_V1,
	R_TX_INFO_2_0_COMB_V1,
	R_TX_INFO_2_1_COMB_V1
};

static const u32 rtw89_tx_common_ctrl_reg_be[] = {
	R_TX_COMMON_CTRL_0_0_COMB_V1,
	R_TX_COMMON_CTRL_0_1_COMB_V1
};

static const struct rtw89_reg_def rtw89_txpwr_be[] = {
	{.addr = R_PATH0_TXPWR_V1, .mask = B_PATH0_TXPWR},
	{.addr = R_PATH1_TXPWR_V1, .mask = B_PATH1_TXPWR}
};

static const struct rtw89_physts_regs rtw89_physts_regs_be = {
	.setting_addr = R_PLCP_HISTOGRAM,
	.dis_trigger_fail_mask = B_STS_DIS_TRIG_BY_FAIL,
	.dis_trigger_brk_mask = B_STS_DIS_TRIG_BY_BRK,
	.mac_phy_intf_sel = {R_INTF_R_INTF_RPT_SEL, B_INTF_R_INTF_RPT_SEL},
	.txpwr = rtw89_txpwr_be,
	.tx_info = RTW89_REGS_DEF(rtw89_tx_info_reg_be),
	.tx_common_ctrl = RTW89_REGS_DEF(rtw89_tx_common_ctrl_reg_be),
};

static const u32 rtw89_tx_info_reg_be_v1[] = {
	R_TX_INFO_0_0_COMB_BE4,
	R_TX_INFO_0_1_COMB_BE4,
	R_TX_INFO_1_0_COMB_BE4,
	R_TX_INFO_1_1_COMB_BE4,
	R_TX_INFO_2_0_COMB_BE4,
	R_TX_INFO_2_1_COMB_BE4
};

static const u32 rtw89_tx_common_ctrl_reg_be_v1[] = {
	R_TX_COMMON_CTRL_0_0_COMB_BE4,
	R_TX_COMMON_CTRL_0_1_COMB_BE4
};

static const struct rtw89_reg_def rtw89_txpwr_be_v1[] = {
	{.addr = R_PATH0_TXPWR_BE4, .mask = B_PATH0_TXPWR},
	{.addr = R_PATH1_TXPWR_BE4, .mask = B_PATH1_TXPWR}
};

static const struct rtw89_physts_regs rtw89_physts_regs_be_v1 = {
	.setting_addr = R_PLCP_HISTOGRAM_BE_V1,
	.dis_trigger_fail_mask = B_STS_DIS_TRIG_BY_FAIL,
	.dis_trigger_brk_mask = B_STS_DIS_TRIG_BY_BRK,
	.mac_phy_intf_sel = {R_INTF_R_INTF_RPT_SEL_BE4, B_INTF_R_INTF_RPT_SEL},
	.txpwr = rtw89_txpwr_be_v1,
	.tx_info = RTW89_REGS_DEF(rtw89_tx_info_reg_be_v1),
	.tx_common_ctrl = RTW89_REGS_DEF(rtw89_tx_common_ctrl_reg_be_v1),
};

static const struct rtw89_cfo_regs rtw89_cfo_regs_be = {
	.comp = R_DCFO_WEIGHT_BE,
	.weighting_mask = B_DCFO_WEIGHT_MSK_BE,
	.comp_seg0 = R_DCFO_OPT_BE,
	.valid_0_mask = B_DCFO_OPT_EN_BE,
};

static const struct rtw89_cfo_regs rtw89_cfo_regs_be_v1 = {
	.comp = R_DCFO_WEIGHT_BE_V1,
	.weighting_mask = B_DCFO_WEIGHT_MSK_BE,
	.comp_seg0 = R_DCFO_OPT_BE_V1,
	.valid_0_mask = B_DCFO_OPT_EN_BE,
};

static const struct rtw89_bb_wrap_regs rtw89_bb_wrap_regs_be = {
	.pwr_macid_lmt = R_BE_PWR_MACID_LMT_BASE,
	.pwr_macid_path = R_BE_PWR_MACID_PATH_BASE,
};

static const struct rtw89_bb_wrap_regs rtw89_bb_wrap_regs_be_v1 = {
	.pwr_macid_lmt = R_BE_PWR_MACID_LMT_BASE_V1,
	.pwr_macid_path = R_BE_PWR_MACID_PATH_BASE_V1,
};

static u32 rtw89_phy0_phy1_offset_be(struct rtw89_dev *rtwdev, u32 addr)
{
	u32 phy_page = addr >> 8;
	u32 ofst = 0;

	if ((phy_page >= 0x4 && phy_page <= 0xF) ||
	    (phy_page >= 0x20 && phy_page <= 0x2B) ||
	    (phy_page >= 0x40 && phy_page <= 0x4f) ||
	    (phy_page >= 0x60 && phy_page <= 0x6f) ||
	    (phy_page >= 0xE4 && phy_page <= 0xE5) ||
	    (phy_page >= 0xE8 && phy_page <= 0xED))
		ofst = 0x1000;
	else
		ofst = 0x0;

	return ofst;
}

static u32 rtw89_phy0_phy1_offset_be_v1(struct rtw89_dev *rtwdev, u32 addr)
{
	u32 phy_page = addr >> 8;
	u32 ofst = 0;

	if ((phy_page >= 0x204 && phy_page <= 0x20F) ||
	    (phy_page >= 0x220 && phy_page <= 0x22F) ||
	    (phy_page >= 0x240 && phy_page <= 0x24f) ||
	    (phy_page >= 0x260 && phy_page <= 0x26f) ||
	    (phy_page >= 0x2C0 && phy_page <= 0x2C9) ||
	    (phy_page >= 0x2E0 && phy_page <= 0x2E8) ||
	    phy_page == 0x2EE)
		ofst = 0x1000;
	else
		ofst = 0x0;

	return ofst;
}

union rtw89_phy_bb_gain_arg_be {
	u32 addr;
	struct {
		u8 type;
#define BB_GAIN_TYPE_SUB0_BE GENMASK(3, 0)
#define BB_GAIN_TYPE_SUB1_BE GENMASK(7, 4)
		u8 path_bw;
#define BB_GAIN_PATH_BE GENMASK(3, 0)
#define BB_GAIN_BW_BE GENMASK(7, 4)
		u8 gain_band;
		u8 cfg_type;
	} __packed;
} __packed;

static void
rtw89_phy_cfg_bb_gain_error_be(struct rtw89_dev *rtwdev,
			       union rtw89_phy_bb_gain_arg_be arg, u32 data)
{
	struct rtw89_phy_bb_gain_info_be *gain = &rtwdev->bb_gain.be;
	u8 bw_type = u8_get_bits(arg.path_bw, BB_GAIN_BW_BE);
	u8 path = u8_get_bits(arg.path_bw, BB_GAIN_PATH_BE);
	u8 gband = arg.gain_band;
	u8 type = arg.type;
	int i;

	switch (type) {
	case 0:
		for (i = 0; i < 4; i++, data >>= 8)
			gain->lna_gain[gband][bw_type][path][i] = data & 0xff;
		break;
	case 1:
		for (i = 4; i < 7; i++, data >>= 8)
			gain->lna_gain[gband][bw_type][path][i] = data & 0xff;
		break;
	case 2:
		for (i = 0; i < 2; i++, data >>= 8)
			gain->tia_gain[gband][bw_type][path][i] = data & 0xff;
		break;
	default:
		rtw89_warn(rtwdev,
			   "bb gain error {0x%x:0x%x} with unknown type: %d\n",
			   arg.addr, data, type);
		break;
	}
}

static void
rtw89_phy_cfg_bb_rpl_ofst_be(struct rtw89_dev *rtwdev,
			     union rtw89_phy_bb_gain_arg_be arg, u32 data)
{
	struct rtw89_phy_bb_gain_info_be *gain = &rtwdev->bb_gain.be;
	u8 type_sub0 = u8_get_bits(arg.type, BB_GAIN_TYPE_SUB0_BE);
	u8 type_sub1 = u8_get_bits(arg.type, BB_GAIN_TYPE_SUB1_BE);
	u8 path = u8_get_bits(arg.path_bw, BB_GAIN_PATH_BE);
	u8 gband = arg.gain_band;
	u8 ofst = 0;
	int i;

	switch (type_sub1) {
	case RTW89_CMAC_BW_20M:
		gain->rpl_ofst_20[gband][path][0] = (s8)data;
		break;
	case RTW89_CMAC_BW_40M:
		for (i = 0; i < RTW89_BW20_SC_40M; i++, data >>= 8)
			gain->rpl_ofst_40[gband][path][i] = data & 0xff;
		break;
	case RTW89_CMAC_BW_80M:
		for (i = 0; i < RTW89_BW20_SC_80M; i++, data >>= 8)
			gain->rpl_ofst_80[gband][path][i] = data & 0xff;
		break;
	case RTW89_CMAC_BW_160M:
		if (type_sub0 == 0)
			ofst = 0;
		else
			ofst = RTW89_BW20_SC_80M;

		for (i = 0; i < RTW89_BW20_SC_80M; i++, data >>= 8)
			gain->rpl_ofst_160[gband][path][i + ofst] = data & 0xff;
		break;
	default:
		rtw89_warn(rtwdev,
			   "bb rpl ofst {0x%x:0x%x} with unknown type_sub1: %d\n",
			   arg.addr, data, type_sub1);
		break;
	}
}

static void
rtw89_phy_cfg_bb_gain_op1db_be(struct rtw89_dev *rtwdev,
			       union rtw89_phy_bb_gain_arg_be arg, u32 data)
{
	struct rtw89_phy_bb_gain_info_be *gain = &rtwdev->bb_gain.be;
	u8 bw_type = u8_get_bits(arg.path_bw, BB_GAIN_BW_BE);
	u8 path = u8_get_bits(arg.path_bw, BB_GAIN_PATH_BE);
	u8 gband = arg.gain_band;
	u8 type = arg.type;
	int i;

	switch (type) {
	case 0:
		for (i = 0; i < 4; i++, data >>= 8)
			gain->lna_op1db[gband][bw_type][path][i] = data & 0xff;
		break;
	case 1:
		for (i = 4; i < 7; i++, data >>= 8)
			gain->lna_op1db[gband][bw_type][path][i] = data & 0xff;
		break;
	case 2:
		for (i = 0; i < 4; i++, data >>= 8)
			gain->tia_lna_op1db[gband][bw_type][path][i] = data & 0xff;
		break;
	case 3:
		for (i = 4; i < 8; i++, data >>= 8)
			gain->tia_lna_op1db[gband][bw_type][path][i] = data & 0xff;
		break;
	default:
		rtw89_warn(rtwdev,
			   "bb gain op1db {0x%x:0x%x} with unknown type: %d\n",
			   arg.addr, data, type);
		break;
	}
}

static void rtw89_phy_config_bb_gain_be(struct rtw89_dev *rtwdev,
					const struct rtw89_reg2_def *reg,
					enum rtw89_rf_path rf_path,
					void *extra_data)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	union rtw89_phy_bb_gain_arg_be arg = { .addr = reg->addr };
	struct rtw89_efuse *efuse = &rtwdev->efuse;
	u8 bw_type = u8_get_bits(arg.path_bw, BB_GAIN_BW_BE);
	u8 path = u8_get_bits(arg.path_bw, BB_GAIN_PATH_BE);

	if (bw_type >= RTW89_BB_BW_NR_BE)
		return;

	if (arg.gain_band >= RTW89_BB_GAIN_BAND_NR_BE)
		return;

	if (path >= chip->rf_path_num)
		return;

	if (arg.addr >= 0xf9 && arg.addr <= 0xfe) {
		rtw89_warn(rtwdev, "bb gain table with flow ctrl\n");
		return;
	}

	switch (arg.cfg_type) {
	case 0:
		rtw89_phy_cfg_bb_gain_error_be(rtwdev, arg, reg->data);
		break;
	case 1:
		rtw89_phy_cfg_bb_rpl_ofst_be(rtwdev, arg, reg->data);
		break;
	case 2:
		/* ignore BB gain bypass */
		break;
	case 3:
		rtw89_phy_cfg_bb_gain_op1db_be(rtwdev, arg, reg->data);
		break;
	case 15:
		rtw89_phy_write32_idx(rtwdev, reg->addr & 0xFFFFF, MASKHWORD,
				      reg->data, RTW89_PHY_0);
		break;
	case 4:
		/* This cfg_type is only used by rfe_type >= 50 with eFEM */
		if (efuse->rfe_type < 50)
			break;
		fallthrough;
	default:
		rtw89_warn(rtwdev,
			   "bb gain {0x%x:0x%x} with unknown cfg type: %d\n",
			   arg.addr, reg->data, arg.cfg_type);
		break;
	}
}

static void rtw89_phy_preinit_rf_nctl_be(struct rtw89_dev *rtwdev)
{
	rtw89_phy_write32_mask(rtwdev, R_GOTX_IQKDPK_C0, B_GOTX_IQKDPK, 0x3);
	rtw89_phy_write32_mask(rtwdev, R_GOTX_IQKDPK_C1, B_GOTX_IQKDPK, 0x3);
	rtw89_phy_write32_mask(rtwdev, R_IQKDPK_HC, B_IQKDPK_HC, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_CLK_GCK, B_CLK_GCK, 0x00fffff);
	rtw89_phy_write32_mask(rtwdev, R_IOQ_IQK_DPK, B_IOQ_IQK_DPK_CLKEN, 0x3);
	rtw89_phy_write32_mask(rtwdev, R_IQK_DPK_RST, B_IQK_DPK_RST, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_IQK_DPK_PRST, B_IQK_DPK_PRST, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_IQK_DPK_PRST_C1, B_IQK_DPK_PRST, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_TXRFC, B_TXRFC_RST, 0x1);

	if (rtwdev->dbcc_en) {
		rtw89_phy_write32_mask(rtwdev, R_IQK_DPK_RST_C1, B_IQK_DPK_RST, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_TXRFC_C1, B_TXRFC_RST, 0x1);
	}
}

static void rtw89_phy_preinit_rf_nctl_be_v1(struct rtw89_dev *rtwdev)
{
	rtw89_phy_write32_mask(rtwdev, R_GOTX_IQKDPK_C0_BE4, B_GOTX_IQKDPK, 0x3);
	rtw89_phy_write32_mask(rtwdev, R_GOTX_IQKDPK_C1_BE4, B_GOTX_IQKDPK, 0x3);
	rtw89_phy_write32_mask(rtwdev, R_IOQ_IQK_DPK_BE4, B_IOQ_IQK_DPK_RST, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_IQK_DPK_RST_BE4, B_IQK_DPK_RST, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_IQK_DPK_PRST_BE4, B_IQK_DPK_PRST, 0x1);
	rtw89_phy_write32_mask(rtwdev, R_IQK_DPK_PRST_C1_BE4, B_IQK_DPK_PRST, 0x1);
}

static u32 rtw89_phy_bb_wrap_flush_addr(struct rtw89_dev *rtwdev, u32 addr)
{
	struct rtw89_hal *hal = &rtwdev->hal;

	if (!test_bit(RTW89_FLAG_RUNNING, rtwdev->flags))
		return 0;

	if (rtwdev->chip->chip_id == RTL8922D && hal->cid == RTL8922D_CID7025) {
		if (addr >= R_BE_PWR_MACID_PATH_BASE_V1 &&
		    addr <= R_BE_PWR_MACID_PATH_BASE_V1 + 0xFF)
			return addr + 0x800;

		if (addr >= R_BE_PWR_MACID_LMT_BASE_V1 &&
		    addr <= R_BE_PWR_MACID_LMT_BASE_V1 + 0xFF)
			return addr - 0x800;
	}

	return 0;
}

static
void rtw89_write_bb_wrap_flush(struct rtw89_dev *rtwdev, u32 addr, u32 data)
{
	/* To write registers of pwr_macid_lmt and pwr_macid_path with flush */
	u32 flush_addr;
	u32 val32;

	flush_addr = rtw89_phy_bb_wrap_flush_addr(rtwdev, addr);
	if (flush_addr) {
		val32 = rtw89_read32(rtwdev, flush_addr);
		rtw89_write32(rtwdev, flush_addr, val32);
	}

	rtw89_write32(rtwdev, addr, data);
}

static
void rtw89_phy_bb_wrap_pwr_by_macid_init(struct rtw89_dev *rtwdev)
{
	const struct rtw89_phy_gen_def *phy = rtwdev->chip->phy_def;
	const struct rtw89_bb_wrap_regs *bb_wrap = phy->bb_wrap;
	u32 max_macid = rtwdev->chip->support_macid_num;
	u32 macid_idx, cr, base_macid_lmt;

	base_macid_lmt = bb_wrap->pwr_macid_lmt;

	for (macid_idx = 0; macid_idx < 4 * max_macid; macid_idx += 4) {
		cr = base_macid_lmt + macid_idx;
		rtw89_write_bb_wrap_flush(rtwdev, cr, 0);
	}
}

static
void rtw89_phy_bb_wrap_tx_path_by_macid_init(struct rtw89_dev *rtwdev)
{
	const struct rtw89_phy_gen_def *phy = rtwdev->chip->phy_def;
	const struct rtw89_bb_wrap_regs *bb_wrap = phy->bb_wrap;
	u32 max_macid = rtwdev->chip->support_macid_num;
	u32 cr = bb_wrap->pwr_macid_path;
	int i;

	for (i = 0; i < max_macid; i++, cr += 4)
		rtw89_write_bb_wrap_flush(rtwdev, cr, 0);
}

static void rtw89_phy_bb_wrap_tpu_set_all(struct rtw89_dev *rtwdev,
					  enum rtw89_mac_idx mac_idx)
{
	u32 addr, t;

	addr = rtw89_mac_reg_by_idx(rtwdev, R_BE_PWR_FTM_SS, mac_idx);
	rtw89_write32_mask(rtwdev, addr, B_BE_PWR_BY_RATE_DBW_ON, 0x3);

	for (addr = R_BE_PWR_BY_RATE; addr <= R_BE_PWR_BY_RATE_END; addr += 4) {
		t = rtw89_mac_reg_by_idx(rtwdev, addr, mac_idx);
		rtw89_write32(rtwdev, t, 0);
	}
	for (addr = R_BE_PWR_RULMT_START; addr <= R_BE_PWR_RULMT_END; addr += 4) {
		t = rtw89_mac_reg_by_idx(rtwdev, addr, mac_idx);
		rtw89_write32(rtwdev, t, 0);
	}
	for (addr = R_BE_PWR_RATE_OFST_CTRL; addr <= R_BE_PWR_RATE_OFST_END; addr += 4) {
		t = rtw89_mac_reg_by_idx(rtwdev, addr, mac_idx);
		rtw89_write32(rtwdev, t, 0);
	}

	addr = rtw89_mac_reg_by_idx(rtwdev, R_BE_PWR_REF_CTRL, mac_idx);
	rtw89_write32_mask(rtwdev, addr, B_BE_PWR_OFST_LMT_DB, 0);
	addr = rtw89_mac_reg_by_idx(rtwdev, R_BE_PWR_OFST_LMTBF, mac_idx);
	rtw89_write32_mask(rtwdev, addr, B_BE_PWR_OFST_LMTBF_DB, 0);
	addr = rtw89_mac_reg_by_idx(rtwdev, R_BE_PWR_RATE_CTRL, mac_idx);
	rtw89_write32_mask(rtwdev, addr, B_BE_PWR_OFST_BYRATE_DB, 0);
	addr = rtw89_mac_reg_by_idx(rtwdev, R_BE_PWR_OFST_RULMT, mac_idx);
	rtw89_write32_mask(rtwdev, addr, B_BE_PWR_OFST_RULMT_DB, 0);
	addr = rtw89_mac_reg_by_idx(rtwdev, R_BE_PWR_OFST_SW, mac_idx);
	rtw89_write32_mask(rtwdev, addr, B_BE_PWR_OFST_SW_DB, 0);
}

static
void rtw89_phy_bb_wrap_listen_path_en_init(struct rtw89_dev *rtwdev)
{
	u32 addr;
	int ret;

	ret = rtw89_mac_check_mac_en(rtwdev, RTW89_MAC_1, RTW89_CMAC_SEL);
	if (ret)
		return;

	addr = rtw89_mac_reg_by_idx(rtwdev, R_BE_PWR_LISTEN_PATH, RTW89_MAC_1);
	rtw89_write32_mask(rtwdev, addr, B_BE_PWR_LISTEN_PATH_EN, 0x2);
}

static void rtw89_phy_bb_wrap_force_cr_init(struct rtw89_dev *rtwdev,
					    enum rtw89_mac_idx mac_idx)
{
	u32 addr;

	addr = rtw89_mac_reg_by_idx(rtwdev, R_BE_PWR_FORCE_LMT, mac_idx);
	rtw89_write32_mask(rtwdev, addr, B_BE_PWR_FORCE_LMT_ON, 0);
	addr = rtw89_mac_reg_by_idx(rtwdev, R_BE_PWR_RATE_CTRL, mac_idx);
	rtw89_write32_mask(rtwdev, addr, B_BE_FORCE_PWR_BY_RATE_EN, 0);
	addr = rtw89_mac_reg_by_idx(rtwdev, R_BE_PWR_OFST_RULMT, mac_idx);
	rtw89_write32_mask(rtwdev, addr, B_BE_PWR_FORCE_RU_ENON, 0);
	rtw89_write32_mask(rtwdev, addr, B_BE_PWR_FORCE_RU_ON, 0);
	addr = rtw89_mac_reg_by_idx(rtwdev, R_BE_PWR_FORCE_MACID, mac_idx);
	rtw89_write32_mask(rtwdev, addr, B_BE_PWR_FORCE_MACID_ALL, 0);
	addr = rtw89_mac_reg_by_idx(rtwdev, R_BE_PWR_COEX_CTRL, mac_idx);
	rtw89_write32_mask(rtwdev, addr, B_BE_PWR_FORCE_COEX_ON, 0);
	addr = rtw89_mac_reg_by_idx(rtwdev, R_BE_PWR_BOOST, mac_idx);
	rtw89_write32_mask(rtwdev, addr, B_BE_PWR_FORCE_RATE_ON, 0);
}

static void rtw89_phy_bb_wrap_ftm_init(struct rtw89_dev *rtwdev,
				       enum rtw89_mac_idx mac_idx)
{
	u32 addr;

	addr = rtw89_mac_reg_by_idx(rtwdev, R_BE_PWR_FTM, mac_idx);
	rtw89_write32(rtwdev, addr, 0xE4E431);

	addr = rtw89_mac_reg_by_idx(rtwdev, R_BE_PWR_FTM_SS, mac_idx);
	rtw89_write32_mask(rtwdev, addr, 0x7, 0);
}

static u32 rtw89_phy_bb_wrap_be_bandedge_decision(struct rtw89_dev *rtwdev,
						  const struct rtw89_chan *chan)
{
	u8 pri_ch = chan->primary_channel;
	u32 val = 0;

	switch (chan->band_type) {
	default:
	case RTW89_BAND_2G:
		if (pri_ch == 1 || pri_ch == 13)
			val = BIT(1) | BIT(0);
		else if (pri_ch == 3 || pri_ch == 11)
			val = BIT(1);
		break;
	case RTW89_BAND_5G:
		if (pri_ch == 36 || pri_ch == 64 || pri_ch == 100)
			val = BIT(3) | BIT(2) | BIT(1) | BIT(0);
		else if (pri_ch == 40 || pri_ch == 60 || pri_ch == 104)
			val = BIT(3) | BIT(2) | BIT(1);
		else if ((pri_ch > 40 && pri_ch < 60) || pri_ch == 108 || pri_ch == 112)
			val = BIT(3) | BIT(2);
		else if (pri_ch > 112 && pri_ch < 132)
			val = BIT(3);
		break;
	case RTW89_BAND_6G:
		if (pri_ch == 233)
			val = BIT(0);
		break;
	}

	return val;
}

void rtw89_phy_bb_wrap_set_rfsi_ct_opt(struct rtw89_dev *rtwdev,
				       enum rtw89_rfsi_ctrl_band rfsi_band,
				       enum rtw89_phy_idx phy_idx)
{
	const struct rtw89_bb_wrap_data *d = rtwdev->phy_info.bb_wrap_data;
	const u32 *val;
	u32 reg;

	if (!d || !d->common)
		return;

	val = d->common->bands[rfsi_band].rfsi_ct_opt;

	reg = rtw89_mac_reg_by_idx(rtwdev, R_RFSI_CT_OPT_0_BE4, phy_idx);
	rtw89_write32(rtwdev, reg, val[0]);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_RFSI_CT_OPT_8_BE4, phy_idx);
	rtw89_write32(rtwdev, reg, val[1]);
}
EXPORT_SYMBOL(rtw89_phy_bb_wrap_set_rfsi_ct_opt);

void rtw89_phy_bb_wrap_set_rfsi_bandedge_ch(struct rtw89_dev *rtwdev,
					    const struct rtw89_chan *chan,
					    enum rtw89_phy_idx phy_idx)
{
	u32 reg;
	u32 val;

	val = rtw89_phy_bb_wrap_be_bandedge_decision(rtwdev, chan);

	rtw89_phy_write32_idx(rtwdev, R_TX_CFR_MANUAL_EN_BE4, B_TX_CFR_MANUAL_EN_BE4_M,
			      chan->primary_channel == 13, phy_idx);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BANDEDGE_DBWX_BE4, phy_idx);
	rtw89_write32_mask(rtwdev, reg, B_BANDEDGE_DBW20_BE4, val & BIT(0));
	reg = rtw89_mac_reg_by_idx(rtwdev, R_BANDEDGE_DBWX_BE4, phy_idx);
	rtw89_write32_mask(rtwdev, reg, B_BANDEDGE_DBW40_BE4, (val & BIT(1)) >> 1);
	reg = rtw89_mac_reg_by_idx(rtwdev, R_BANDEDGE_DBWX_BE4, phy_idx);
	rtw89_write32_mask(rtwdev, reg, B_BANDEDGE_DBW80_BE4, (val & BIT(2)) >> 2);
	reg = rtw89_mac_reg_by_idx(rtwdev, R_BANDEDGE_DBWY_BE4, phy_idx);
	rtw89_write32_mask(rtwdev, reg, B_BANDEDGE_DBW160_BE4, (val & BIT(3)) >> 3);
}
EXPORT_SYMBOL(rtw89_phy_bb_wrap_set_rfsi_bandedge_ch);

static void rtw89_phy_bb_wrap_tx_rfsi_qam_comp_th_init(struct rtw89_dev *rtwdev,
						       enum rtw89_mac_idx mac_idx)
{
	const struct rtw89_bb_wrap_data *d = rtwdev->phy_info.bb_wrap_data;
	u8 th0, th1, th2;

	if (!d || !d->common)
		return;

	th0 = d->common->qam_th[0];
	th1 = d->common->qam_th[1];
	th2 = d->common->qam_th[2];

	/* TH0 */
	rtw89_write32_idx(rtwdev, R_QAM_TH0_BE4, B_QAM_TH0_0_BE4, th0, mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_TH0_BE4, B_QAM_TH0_3_BE4, th0, mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_TH1_BE4, B_QAM_TH1_1_BE4, th0, mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_TH1_BE4, B_QAM_TH1_4_BE4, th0, mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_TH1_BE4, B_QAM_TH1_7_BE4, th0, mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_TH2_BE4, B_QAM_TH2_0_BE4, th0, mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_TH2_BE4, B_QAM_TH2_3_BE4, th0, mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_TH2_BE4, B_QAM_TH2_6_BE4, th0, mac_idx);
	/* TH1 */
	rtw89_write32_idx(rtwdev, R_QAM_TH0_BE4, B_QAM_TH0_1_BE4, th1, mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_TH0_BE4, B_QAM_TH0_4_BE4, th1, mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_TH1_BE4, B_QAM_TH1_2_BE4, th1, mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_TH1_BE4, B_QAM_TH1_5_BE4, th1, mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_TH1_BE4, B_QAM_TH1_8_BE4, th1, mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_TH2_BE4, B_QAM_TH2_1_BE4, th1, mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_TH2_BE4, B_QAM_TH2_4_BE4, th1, mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_TH2_BE4, B_QAM_TH2_7_BE4, th1, mac_idx);
	/* TH2 */
	rtw89_write32_idx(rtwdev, R_QAM_TH0_BE4, B_QAM_TH0_2_BE4, th2, mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_TH1_BE4, B_QAM_TH1_0_BE4, th2, mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_TH1_BE4, B_QAM_TH1_3_BE4, th2, mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_TH1_BE4, B_QAM_TH1_6_BE4, th2, mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_TH1_BE4, B_QAM_TH1_9_BE4, th2, mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_TH2_BE4, B_QAM_TH2_2_BE4, th2, mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_TH2_BE4, B_QAM_TH2_5_BE4, th2, mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_TH2_BE4, B_QAM_TH2_8_BE4, th2, mac_idx);
	/* DPD 160M */
	rtw89_write32_idx(rtwdev, R_DPD_DBW160_TH0_BE4, B_DPD_DBW160_TH0_0_BE4, th0, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_DBW160_TH0_BE4, B_DPD_DBW160_TH0_1_BE4, th0, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_DBW160_TH0_BE4, B_DPD_DBW160_TH0_2_BE4, th0, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_DBW160_TH0_BE4, B_DPD_DBW160_TH0_3_BE4, th0, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_DBW160_TH0_BE4, B_DPD_DBW160_TH0_4_BE4, th0, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_DBW160_TH1_BE4, B_DPD_DBW160_TH1_5_BE4, th0, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_DBW160_TH1_BE4, B_DPD_DBW160_TH1_6_BE4, th0, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_DBW160_TH1_BE4, B_DPD_DBW160_TH1_7_BE4, th0, mac_idx);
	/* DPD 20M */
	rtw89_write32_idx(rtwdev, R_DPD_CBW_TH0_BE4, B_DPD_CBW20_TH0_0_BE4, 0x2, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_CBW_TH0_BE4, B_DPD_CBW20_TH0_1_BE4, 0x2, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_CBW_TH0_BE4, B_DPD_CBW20_TH0_2_BE4, 0x2, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_CBW_TH0_BE4, B_DPD_CBW20_TH0_3_BE4, 0x2, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_CBW_TH0_BE4, B_DPD_CBW20_TH0_4_BE4, 0x2, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_CBW_TH0_BE4, B_DPD_CBW20_TH0_5_BE4, 0x2, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_CBW_TH0_BE4, B_DPD_CBW20_TH0_6_BE4, 0x2, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_CBW_TH1_BE4, B_DPD_CBW20_TH1_7_BE4, 0x2, mac_idx);
	/* DPD 40M */
	rtw89_write32_idx(rtwdev, R_DPD_CBW_TH1_BE4, B_DPD_CBW40_TH1_0_BE4, 0x2, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_CBW_TH1_BE4, B_DPD_CBW40_TH1_1_BE4, 0x2, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_CBW_TH1_BE4, B_DPD_CBW40_TH1_2_BE4, 0x2, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_CBW_TH1_BE4, B_DPD_CBW40_TH1_3_BE4, 0x2, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_CBW_TH1_BE4, B_DPD_CBW40_TH1_4_BE4, 0x2, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_CBW_TH1_BE4, B_DPD_CBW20_TH0_3_BE4, 0x2, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_CBW_TH1_BE4, B_DPD_CBW20_TH0_4_BE4, 0x2, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_CBW_TH1_BE4, B_DPD_CBW20_TH0_5_BE4, 0x2, mac_idx);
	/* DPD 80M */
	rtw89_write32_idx(rtwdev, R_DPD_CBW_TH1_BE4, B_DPD_CBW80_TH1_0_BE4, 0x2, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_CBW_TH2_BE4, B_DPD_CBW80_TH2_1_BE4, 0x2, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_CBW_TH2_BE4, B_DPD_CBW80_TH2_2_BE4, 0x2, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_CBW_TH2_BE4, B_DPD_CBW80_TH2_3_BE4, 0x2, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_CBW_TH2_BE4, B_DPD_CBW80_TH2_4_BE4, 0x2, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_CBW_TH2_BE4, B_DPD_CBW80_TH2_5_BE4, 0x2, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_CBW_TH2_BE4, B_DPD_CBW80_TH2_6_BE4, 0x2, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_CBW_TH2_BE4, B_DPD_CBW80_TH2_7_BE4, 0x2, mac_idx);
	/* CIM3K */
	rtw89_write32_idx(rtwdev, R_COMP_CIM3K_BE4, B_COMP_CIM3K_TH2_BE4, 0x2, mac_idx);
}

static void rtw89_phy_bb_wrap_tx_rfsi_qam_comp_th_gen3_init(struct rtw89_dev *rtwdev,
							    const struct rtw89_chan *chan,
							    enum rtw89_mac_idx mac_idx)
{
	const struct rtw89_bb_wrap_data *d = rtwdev->phy_info.bb_wrap_data;
	const struct rtw89_bb_wrap_common_data_gen3 *common_gen3;
	const u8 *ths;

	if (!d || !d->common_gen3)
		return;

	common_gen3 = d->common_gen3;
	ths = common_gen3->bands[chan->rfsi_band].qam_th;

	rtw89_write32_idx(rtwdev, R_QAM3_TH0_BE4, B_QAM3_TH0_0_BE4, ths[0], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH0_BE4, B_QAM3_TH0_1_BE4, ths[0], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH0_BE4, B_QAM3_TH0_2_BE4, ths[0], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH0_BE4, B_QAM3_TH0_3_BE4, ths[0], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH0_BE4, B_QAM3_TH0_4_BE4, ths[0], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH0_BE4, B_QAM3_TH0_5_BE4, ths[0], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH0_BE4, B_QAM3_TH0_6_BE4, ths[0], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH0_BE4, B_QAM3_TH0_7_BE4, ths[0], mac_idx);

	rtw89_write32_idx(rtwdev, R_QAM3_TH0_BE4, B_QAM3_TH1_0_BE4, ths[1], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH0_BE4, B_QAM3_TH1_1_BE4, ths[1], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH1_BE4, B_QAM3_TH1_2_BE4, ths[1], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH1_BE4, B_QAM3_TH1_3_BE4, ths[1], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH1_BE4, B_QAM3_TH1_4_BE4, ths[1], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH1_BE4, B_QAM3_TH1_5_BE4, ths[1], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH1_BE4, B_QAM3_TH1_6_BE4, ths[1], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH1_BE4, B_QAM3_TH1_7_BE4, ths[1], mac_idx);

	rtw89_write32_idx(rtwdev, R_QAM3_TH1_BE4, B_QAM3_TH2_0_BE4, ths[2], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH1_BE4, B_QAM3_TH2_1_BE4, ths[2], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH1_BE4, B_QAM3_TH2_2_BE4, ths[2], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH1_BE4, B_QAM3_TH2_3_BE4, ths[2], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH2_BE4, B_QAM3_TH2_4_BE4, ths[2], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH2_BE4, B_QAM3_TH2_5_BE4, ths[2], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH2_BE4, B_QAM3_TH2_6_BE4, ths[2], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH2_BE4, B_QAM3_TH2_7_BE4, ths[2], mac_idx);

	rtw89_write32_idx(rtwdev, R_QAM3_TH2_BE4, B_QAM3_TH3_0_BE4, ths[3], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH2_BE4, B_QAM3_TH3_1_BE4, ths[3], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH2_BE4, B_QAM3_TH3_2_BE4, ths[3], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH2_BE4, B_QAM3_TH3_3_BE4, ths[3], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH2_BE4, B_QAM3_TH3_4_BE4, ths[3], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH2_BE4, B_QAM3_TH3_5_BE4, ths[3], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH3_BE4, B_QAM3_TH3_6_BE4, ths[3], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH3_BE4, B_QAM3_TH3_7_BE4, ths[3], mac_idx);

	rtw89_write32_idx(rtwdev, R_QAM3_TH4_BE4, B_QAM3_TH4_0_BE4, ths[4], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH4_BE4, B_QAM3_TH4_1_BE4, ths[4], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH4_BE4, B_QAM3_TH4_2_BE4, ths[4], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH4_BE4, B_QAM3_TH4_3_BE4, ths[4], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH4_BE4, B_QAM3_TH4_4_BE4, ths[4], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH4_BE4, B_QAM3_TH4_5_BE4, ths[4], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH4_BE4, B_QAM3_TH4_6_BE4, ths[4], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH4_BE4, B_QAM3_TH4_7_BE4, ths[4], mac_idx);

	rtw89_write32_idx(rtwdev, R_QAM3_TH3_BE4, B_QAM3_TH5_0_BE4, ths[5], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH3_BE4, B_QAM3_TH5_1_BE4, ths[5], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH3_BE4, B_QAM3_TH5_2_BE4, ths[5], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH3_BE4, B_QAM3_TH5_3_BE4, ths[5], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH3_BE4, B_QAM3_TH5_4_BE4, ths[5], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH3_BE4, B_QAM3_TH5_5_BE4, ths[5], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH3_BE4, B_QAM3_TH5_6_BE4, ths[5], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH3_BE4, B_QAM3_TH5_7_BE4, ths[5], mac_idx);
}

static void rtw89_phy_bb_wrap_tx_rfsi_scenario_def(struct rtw89_dev *rtwdev,
						   enum rtw89_mac_idx mac_idx)
{
	const struct rtw89_bb_wrap_data *d = rtwdev->phy_info.bb_wrap_data;
	u8 pb_tb = 0;

	if (d && d->common)
		pb_tb = d->common->bands[0].pb_tb;

	rtw89_write32_idx(rtwdev, R_RFSI_CT_DEF_BE4, B_RFSI_CT_ER_BE4, 0x0, mac_idx);
	rtw89_write32_idx(rtwdev, R_RFSI_CT_DEF_BE4, B_RFSI_CT_SUBF_BE4, 0x0, mac_idx);
	rtw89_write32_idx(rtwdev, R_RFSI_CT_DEF_BE4, B_RFSI_CT_FTM_BE4, 0x0, mac_idx);
	rtw89_write32_idx(rtwdev, R_RFSI_CT_DEF_BE4, B_RFSI_CT_SENS_BE4, 0x0, mac_idx);

	rtw89_write32_idx(rtwdev, R_FBTB_CT_DEF_BE4, B_FBTB_CT_DEF_BE, 0x0, mac_idx);
	rtw89_write32_idx(rtwdev, R_FBTB_CT_DEF_BE4, B_FBTB_CT_PB_BE4, pb_tb, mac_idx);
	rtw89_write32_idx(rtwdev, R_FBTB_CT_DEF_BE4, B_FBTB_CT_DL_WO_BE4, 0x0, mac_idx);
	rtw89_write32_idx(rtwdev, R_FBTB_CT_DEF_BE4, B_FBTB_CT_DL_BF_BE4, 0x0, mac_idx);
	rtw89_write32_idx(rtwdev, R_FBTB_CT_DEF_BE4, B_FBTB_CT_MUMIMO_BE4, 0x0, mac_idx);
	rtw89_write32_idx(rtwdev, R_FBTB_CT_DEF_BE4, B_FBTB_CT_FTM_BE4, 0x0, mac_idx);
	rtw89_write32_idx(rtwdev, R_FBTB_CT_DEF_BE4, B_FBTB_CT_SENS_BE4, 0x0, mac_idx);
}

static void rtw89_phy_bb_wrap_tx_rfsi_qam_comp_val(struct rtw89_dev *rtwdev,
						   enum rtw89_mac_idx mac_idx)
{
	const struct rtw89_bb_wrap_data *d = rtwdev->phy_info.bb_wrap_data;
	const u16 *th;

	if (!d || !d->common)
		return;

	th = d->bands[0].qam_comp_th0;
	rtw89_write32_idx(rtwdev, R_QAM_COMP_TH0_BE4, MASKLWORD, th[0], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_COMP_TH0_BE4, MASKHWORD, th[1], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_COMP_TH1_BE4, MASKLWORD, th[2], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_COMP_TH1_BE4, MASKHWORD, th[3], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_COMP_TH2_BE4, MASKLWORD, th[4], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_COMP_TH2_BE4, MASKHWORD, th[5], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_COMP_TH3_BE4, MASKLWORD, th[6], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_COMP_TH3_BE4, MASKHWORD, th[7], mac_idx);

	th = d->bands[0].qam_comp_th1;
	rtw89_write32_idx(rtwdev, R_QAM_COMP_TH4_BE4, B_QAM_COMP_TH4_L, th[0], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_COMP_TH4_BE4, B_QAM_COMP_TH4_M, th[1], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_COMP_TH4_BE4, B_QAM_COMP_TH4_H, th[2], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_COMP_TH5_BE4, B_QAM_COMP_TH5_L, th[3], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_COMP_TH5_BE4, B_QAM_COMP_TH5_M, th[4], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_COMP_TH5_BE4, B_QAM_COMP_TH5_H, th[5], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_COMP_TH6_BE4, B_QAM_COMP_TH6_L, th[6], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_COMP_TH6_BE4, B_QAM_COMP_TH6_M, th[7], mac_idx);

	th = d->bands[0].qam_comp_th2;
	rtw89_write32_idx(rtwdev, R_QAM_COMP_TH4_BE4, B_QAM_COMP_TH4_2L, th[0], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_COMP_TH4_BE4, B_QAM_COMP_TH4_2M, th[1], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_COMP_TH4_BE4, B_QAM_COMP_TH4_2H, th[2], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_COMP_TH5_BE4, B_QAM_COMP_TH5_2L, th[3], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_COMP_TH5_BE4, B_QAM_COMP_TH5_2M, th[4], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_COMP_TH5_BE4, B_QAM_COMP_TH5_2H, th[5], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_COMP_TH6_BE4, B_QAM_COMP_TH6_2L, th[6], mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM_COMP_TH6_BE4, B_QAM_COMP_TH6_2M, th[7], mac_idx);

	th = d->bands[0].qam_comp_ow;
	rtw89_write32_idx(rtwdev, R_OW_VAL_0_BE4, MASKLWORD, th[0], mac_idx);
	rtw89_write32_idx(rtwdev, R_OW_VAL_0_BE4, MASKHWORD, th[1], mac_idx);
	rtw89_write32_idx(rtwdev, R_OW_VAL_1_BE4, MASKLWORD, th[2], mac_idx);
	rtw89_write32_idx(rtwdev, R_OW_VAL_1_BE4, MASKHWORD, th[3], mac_idx);
	rtw89_write32_idx(rtwdev, R_OW_VAL_2_BE4, MASKLWORD, th[4], mac_idx);
	rtw89_write32_idx(rtwdev, R_OW_VAL_2_BE4, MASKHWORD, th[5], mac_idx);
	rtw89_write32_idx(rtwdev, R_OW_VAL_3_BE4, MASKLWORD, th[6], mac_idx);
	rtw89_write32_idx(rtwdev, R_OW_VAL_3_BE4, MASKHWORD, th[7], mac_idx);
}

static void rtw89_phy_bb_set_oob_dpd_qam_comp_val(struct rtw89_dev *rtwdev,
						  enum rtw89_mac_idx mac_idx)
{
	const struct rtw89_bb_wrap_data *d = rtwdev->phy_info.bb_wrap_data;
	u8 th;

	if (!d)
		return;

	th = d->bands[0].oob_dpd_by_cbw[0];
	rtw89_write32_idx(rtwdev, R_OOB_CBW20_BE4, B_OOB_CBW20_CCK0_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW20_BE4, B_OOB_CBW20_CCK1_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW20_BE4, B_OOB_CBW20_CCK2_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW20_BE4, B_OOB_CBW20_CCK3_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW20_BE4, B_OOB_CBW20_CCK4_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW20_BE4, B_OOB_CBW20_CCK5_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW20_BE4, B_OOB_CBW20_CCK6_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW20_BE4, B_OOB_CBW20_CCK7_BE4, th, mac_idx);

	th = d->bands[0].oob_dpd_by_cbw[1];
	rtw89_write32_idx(rtwdev, R_OOB_CBW40_BE4, B_OOB_CBW40_CCK0_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW40_BE4, B_OOB_CBW40_CCK1_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW40_BE4, B_OOB_CBW40_CCK2_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW40_BE4, B_OOB_CBW40_CCK3_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW40_BE4, B_OOB_CBW40_CCK4_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW40_BE4, B_OOB_CBW40_CCK5_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW40_BE4, B_OOB_CBW40_CCK6_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW40_BE4, B_OOB_CBW40_CCK7_BE4, th, mac_idx);

	th = d->bands[0].oob_dpd_by_cbw[2];
	rtw89_write32_idx(rtwdev, R_OOB_CBW20_BE4, B_OOB_CBW20_TH0_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW20_BE4, B_OOB_CBW20_TH1_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW20_BE4, B_OOB_CBW20_TH2_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW20_BE4, B_OOB_CBW20_TH3_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW20_BE4, B_OOB_CBW20_TH4_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW20_BE4, B_OOB_CBW20_TH5_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW20_BE4, B_OOB_CBW20_TH6_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW20_BE4, B_OOB_CBW20_TH7_BE4, th, mac_idx);

	th = d->bands[0].oob_dpd_by_cbw[3];
	rtw89_write32_idx(rtwdev, R_OOB_CBW40_BE4, B_OOB_CBW40_TH0_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW40_BE4, B_OOB_CBW40_TH1_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW40_BE4, B_OOB_CBW40_TH2_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW40_BE4, B_OOB_CBW40_TH3_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW40_BE4, B_OOB_CBW40_TH4_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW40_BE4, B_OOB_CBW40_TH5_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW40_BE4, B_OOB_CBW40_TH6_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW40_BE4, B_OOB_CBW40_TH7_BE4, th, mac_idx);

	th = d->bands[0].oob_dpd_by_cbw[4];
	rtw89_write32_idx(rtwdev, R_OOB_CBW80_BE4, B_OOB_CBW80_TH0_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW80_BE4, B_OOB_CBW80_TH1_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW80_BE4, B_OOB_CBW80_TH2_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW80_BE4, B_OOB_CBW80_TH3_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW80_BE4, B_OOB_CBW80_TH4_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW80_BE4, B_OOB_CBW80_TH5_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW80_BE4, B_OOB_CBW80_TH6_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW80_BE4, B_OOB_CBW80_TH7_BE4, th, mac_idx);

	th = d->bands[0].oob_dpd_by_cbw[5];
	rtw89_write32_idx(rtwdev, R_OOB_CBW40_BE4, B_OOB_CBW20_OW0_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW40_BE4, B_OOB_CBW20_OW1_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW40_BE4, B_OOB_CBW20_OW2_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW40_BE4, B_OOB_CBW20_OW3_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW40_BE4, B_OOB_CBW20_OW4_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW40_BE4, B_OOB_CBW20_OW5_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW40_BE4, B_OOB_CBW20_OW6_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW40_BE4, B_OOB_CBW20_OW7_BE4, th, mac_idx);

	th = d->bands[0].oob_dpd_by_cbw[6];
	rtw89_write32_idx(rtwdev, R_OOB_CBW40_BE4, B_OOB_CBW40_OW0_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW40_BE4, B_OOB_CBW40_OW1_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW40_BE4, B_OOB_CBW40_OW2_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW40_BE4, B_OOB_CBW40_OW3_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW40_BE4, B_OOB_CBW40_OW4_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW40_BE4, B_OOB_CBW40_OW5_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW40_BE4, B_OOB_CBW40_OW6_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW40_BE4, B_OOB_CBW40_OW7_BE4, th, mac_idx);

	th = d->bands[0].oob_dpd_by_cbw[7];
	rtw89_write32_idx(rtwdev, R_OOB_CBW80_BE4, B_OOB_CBW80_OW0_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW80_BE4, B_OOB_CBW80_OW1_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW80_BE4, B_OOB_CBW80_OW2_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW80_BE4, B_OOB_CBW80_OW3_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW80_BE4, B_OOB_CBW80_OW4_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW80_BE4, B_OOB_CBW80_OW5_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW80_BE4, B_OOB_CBW80_OW6_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_OOB_CBW80_BE4, B_OOB_CBW80_OW7_BE4, th, mac_idx);
}

static void rtw89_phy_bb_set_mdpd_qam_comp_val(struct rtw89_dev *rtwdev,
					       enum rtw89_mac_idx mac_idx)
{
	const struct rtw89_bb_wrap_data *d = rtwdev->phy_info.bb_wrap_data;
	u8 th;

	if (!d)
		return;

	th = d->mdpd_by_dbw[0];
	rtw89_write32_idx(rtwdev, R_DPD_CBW160_BE4, B_DPD_CBW160_TH0_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_CBW160_BE4, B_DPD_CBW160_TH1_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_CBW160_BE4, B_DPD_CBW160_TH2_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_CBW160_BE4, B_DPD_CBW160_TH3_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_CBW160_BE4, B_DPD_CBW160_TH4_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_CBW160_BE4, B_DPD_CBW160_TH5_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_CBW160_BE4, B_DPD_CBW160_TH6_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_CBW160_BE4, B_DPD_CBW160_TH7_BE4, th, mac_idx);

	th = d->mdpd_by_dbw[2];
	rtw89_write32_idx(rtwdev, R_DPD_CBW160_BE4, B_DPD_CBW160_OW0_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_CBW160_BE4, B_DPD_CBW160_OW1_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_CBW160_BE4, B_DPD_CBW160_OW2_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_CBW160_BE4, B_DPD_CBW160_OW3_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_CBW160_BE4, B_DPD_CBW160_OW4_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_CBW160_BE4, B_DPD_CBW160_OW5_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_CBW160_BE4, B_DPD_CBW160_OW6_BE4, th, mac_idx);
	rtw89_write32_idx(rtwdev, R_DPD_CBW160_BE4, B_DPD_CBW160_OW7_BE4, th, mac_idx);
}

static void rtw89_phy_bb_set_cim3k_val(struct rtw89_dev *rtwdev,
				       enum rtw89_mac_idx mac_idx)
{
	const struct rtw89_bb_wrap_data *d = rtwdev->phy_info.bb_wrap_data;
	const struct rtw89_bb_wrap_data_cim3k *p;

	if (!d || !d->common)
		return;

	p = &d->common->bands[0].cim3k;

	rtw89_write32_idx(rtwdev, R_COMP_CIM3K_BE4, B_COMP_CIM3K_TH_BE4, p->th, mac_idx);
	rtw89_write32_idx(rtwdev, R_COMP_CIM3K_BE4, B_COMP_CIM3K_OW_BE4, p->ow, mac_idx);
	rtw89_write32_idx(rtwdev, R_COMP_CIM3K_BE4, B_COMP_CIM3K_NONBE_BE4,
			  p->non_bandedge, mac_idx);
	rtw89_write32_idx(rtwdev, R_COMP_CIM3K_BE4, B_COMP_CIM3K_BANDEDGE_BE4,
			  p->bandedge, mac_idx);

	if (rtwdev->chip->chip_id != RTL8922D)
		return;

	rtw89_write32_idx(rtwdev, R_CIM3K_SU_FORCE, B_CIM3K_SU_FORCE_EN, 1, mac_idx);
	rtw89_write32_idx(rtwdev, R_CIM3K_SU_FORCE, B_CIM3K_SU_FORCE_VAL, 0, mac_idx);
}

static void rtw89_phy_bb_set_cck_cfir_filter_val_gen3(struct rtw89_dev *rtwdev,
						      enum rtw89_mac_idx mac_idx)
{
	const struct rtw89_bb_wrap_data *d = rtwdev->phy_info.bb_wrap_data;
	const struct rtw89_bb_wrap_common_data_gen3 *common_gen3;
	u8 cck0, cck1;

	if (!d || !d->common_gen3)
		return;

	common_gen3 = d->common_gen3;
	cck0 = common_gen3->cck_val[0];
	cck1 = common_gen3->cck_val[1];

	rtw89_write32_idx(rtwdev, R_QAM3_TH4_BE4, B_QAM3_CFIR0_BE4, cck0, mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH4_BE4, B_QAM3_CFIR1_BE4, cck0, mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH4_BE4, B_QAM3_CFIR2_BE4, cck0, mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH4_BE4, B_QAM3_CFIR3_BE4, cck0, mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH4_BE4, B_QAM3_CFIR4_BE4, cck0, mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH4_BE4, B_QAM3_CFIR5_BE4, cck0, mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH4_BE4, B_QAM3_CFIR6_BE4, cck0, mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_TH4_BE4, B_QAM3_CFIR7_BE4, cck0, mac_idx);

	rtw89_write32_idx(rtwdev, R_QAM3_FLTR_BE4, B_QAM3_FLTR0_BE4, cck1, mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_FLTR_BE4, B_QAM3_FLTR1_BE4, cck1, mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_FLTR_BE4, B_QAM3_FLTR2_BE4, cck1, mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_FLTR_BE4, B_QAM3_FLTR3_BE4, cck1, mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_FLTR_BE4, B_QAM3_FLTR4_BE4, cck1, mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_FLTR_BE4, B_QAM3_FLTR5_BE4, cck1, mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_FLTR_BE4, B_QAM3_FLTR6_BE4, cck1, mac_idx);
	rtw89_write32_idx(rtwdev, R_QAM3_FLTR_BE4, B_QAM3_FLTR7_BE4, cck1, mac_idx);
}

static void rtw89_phy_bb_wrap_tx_rfsi_ctrl_init(struct rtw89_dev *rtwdev,
						enum rtw89_mac_idx mac_idx)
{
	enum rtw89_phy_idx phy_idx = mac_idx != RTW89_MAC_0 ? RTW89_PHY_1 : RTW89_PHY_0;
	enum rtw89_core_chip_id chip_id = rtwdev->chip->chip_id;
	struct rtw89_entity_conf conf;
	const struct rtw89_chan *chan;

	if (chip_id != RTL8922D)
		return;

	rtw89_entity_get_conf(rtwdev, &conf);
	chan = conf.chans[phy_idx];

	rtw89_phy_bb_wrap_tx_rfsi_qam_comp_th_init(rtwdev, mac_idx);
	rtw89_phy_bb_wrap_tx_rfsi_qam_comp_th_gen3_init(rtwdev, chan, mac_idx);
	rtw89_phy_bb_wrap_tx_rfsi_scenario_def(rtwdev, mac_idx);
	rtw89_phy_bb_wrap_tx_rfsi_qam_comp_val(rtwdev, mac_idx);
	rtw89_phy_bb_set_oob_dpd_qam_comp_val(rtwdev, mac_idx);
	rtw89_phy_bb_set_mdpd_qam_comp_val(rtwdev, mac_idx);
	rtw89_phy_bb_set_cim3k_val(rtwdev, mac_idx);
	rtw89_phy_bb_set_cck_cfir_filter_val_gen3(rtwdev, mac_idx);
	rtw89_phy_bb_wrap_set_rfsi_ct_opt(rtwdev, 0, phy_idx);
	rtw89_phy_bb_wrap_set_rfsi_bandedge_ch(rtwdev, chan, phy_idx);
}

static void rtw89_phy_bb_wrap_ul_pwr(struct rtw89_dev *rtwdev)
{
	enum rtw89_core_chip_id chip_id = rtwdev->chip->chip_id;
	u8 mac_idx;
	u32 addr;

	if (chip_id != RTL8922A)
		return;

	for (mac_idx = 0; mac_idx < RTW89_MAC_NUM; mac_idx++) {
		addr = rtw89_mac_reg_by_idx(rtwdev, R_BE_PWR_RSSI_TARGET_LMT, mac_idx);
		rtw89_write32(rtwdev, addr, 0x0201FE00);
		addr = rtw89_mac_reg_by_idx(rtwdev, R_BE_PWR_TH, mac_idx);
		rtw89_write32(rtwdev, addr, 0x00FFEC7E);
	}
}

static void __rtw89_phy_bb_wrap_init_be(struct rtw89_dev *rtwdev,
					enum rtw89_mac_idx mac_idx)
{
	rtw89_phy_bb_wrap_tx_path_by_macid_init(rtwdev);
	rtw89_phy_bb_wrap_pwr_by_macid_init(rtwdev);
	rtw89_phy_bb_wrap_tpu_set_all(rtwdev, mac_idx);
	rtw89_phy_bb_wrap_tx_rfsi_ctrl_init(rtwdev, mac_idx);
	rtw89_phy_bb_wrap_force_cr_init(rtwdev, mac_idx);
	rtw89_phy_bb_wrap_ftm_init(rtwdev, mac_idx);
	rtw89_phy_bb_wrap_listen_path_en_init(rtwdev);
	rtw89_phy_bb_wrap_ul_pwr(rtwdev);
}

static void rtw89_phy_bb_wrap_init_be(struct rtw89_dev *rtwdev)
{
	__rtw89_phy_bb_wrap_init_be(rtwdev, RTW89_MAC_0);
	if (rtwdev->dbcc_en)
		__rtw89_phy_bb_wrap_init_be(rtwdev, RTW89_MAC_1);
}

static void rtw89_phy_ch_info_init_be(struct rtw89_dev *rtwdev)
{
	rtw89_phy_write32_mask(rtwdev, R_CHINFO_SEG, B_CHINFO_SEG_LEN, 0x0);
	rtw89_phy_write32_mask(rtwdev, R_CHINFO_SEG, B_CHINFO_SEG, 0xf);
	rtw89_phy_write32_mask(rtwdev, R_CHINFO_DATA, B_CHINFO_DATA_BITMAP, 0x1);
	rtw89_phy_set_phy_regs(rtwdev, R_CHINFO_ELM_SRC, B_CHINFO_ELM_BITMAP, 0x40303);
	rtw89_phy_set_phy_regs(rtwdev, R_CHINFO_ELM_SRC, B_CHINFO_SRC, 0x0);
	rtw89_phy_set_phy_regs(rtwdev, R_CHINFO_TYPE_SCAL, B_CHINFO_TYPE, 0x3);
	rtw89_phy_set_phy_regs(rtwdev, R_CHINFO_TYPE_SCAL, B_CHINFO_SCAL, 0x0);
}

static void rtw89_phy_ch_info_init_be_v1(struct rtw89_dev *rtwdev)
{
	rtw89_phy_write32_mask(rtwdev, R_CHINFO_SEG_BE4, B_CHINFO_SEG_LEN_BE4, 0);
	rtw89_phy_set_phy_regs(rtwdev, R_CHINFO_OPT_BE4, B_CHINFO_OPT_BE4, 0x3);
	rtw89_phy_set_phy_regs(rtwdev, R_CHINFO_NX_BE4, B_CHINFO_NX_BE4, 0x669);
	rtw89_phy_set_phy_regs(rtwdev, R_CHINFO_ALG_BE4, B_CHINFO_ALG_BE4, 0);
}

struct rtw89_byr_spec_ent_be {
	struct rtw89_rate_desc init;
	u8 num_of_idx;
	bool no_over_bw40;
	bool no_multi_nss;
};

static const struct rtw89_byr_spec_ent_be rtw89_byr_spec_be[] = {
	{
		.init = { .rs = RTW89_RS_CCK },
		.num_of_idx = RTW89_RATE_CCK_NUM,
		.no_over_bw40 = true,
		.no_multi_nss = true,
	},
	{
		.init = { .rs = RTW89_RS_OFDM },
		.num_of_idx = RTW89_RATE_OFDM_NUM,
		.no_multi_nss = true,
	},
	{
		.init = { .rs = RTW89_RS_MCS, .idx = 14, .ofdma = RTW89_NON_OFDMA },
		.num_of_idx = 2,
		.no_multi_nss = true,
	},
	{
		.init = { .rs = RTW89_RS_MCS, .idx = 14, .ofdma = RTW89_OFDMA },
		.num_of_idx = 2,
		.no_multi_nss = true,
	},
	{
		.init = { .rs = RTW89_RS_MCS, .ofdma = RTW89_NON_OFDMA },
		.num_of_idx = 14,
	},
	{
		.init = { .rs = RTW89_RS_HEDCM, .ofdma = RTW89_NON_OFDMA },
		.num_of_idx = RTW89_RATE_HEDCM_NUM,
	},
	{
		.init = { .rs = RTW89_RS_MCS, .ofdma = RTW89_OFDMA },
		.num_of_idx = 14,
	},
	{
		.init = { .rs = RTW89_RS_HEDCM, .ofdma = RTW89_OFDMA },
		.num_of_idx = RTW89_RATE_HEDCM_NUM,
	},
};

static
void __phy_set_txpwr_byrate_be(struct rtw89_dev *rtwdev, u8 band, u8 bw,
			       u8 nss, u32 *addr, enum rtw89_phy_idx phy_idx)
{
	const struct rtw89_byr_spec_ent_be *ent;
	struct rtw89_rate_desc desc;
	int pos = 0;
	int i, j;
	u32 val;
	s8 v[4];

	for (i = 0; i < ARRAY_SIZE(rtw89_byr_spec_be); i++) {
		ent = &rtw89_byr_spec_be[i];

		if (bw > RTW89_CHANNEL_WIDTH_40 && ent->no_over_bw40)
			continue;
		if (nss > RTW89_NSS_1 && ent->no_multi_nss)
			continue;

		desc = ent->init;
		desc.nss = nss;
		for (j = 0; j < ent->num_of_idx; j++, desc.idx++) {
			v[pos] = rtw89_phy_read_txpwr_byrate(rtwdev, band, bw,
							     &desc);
			pos = (pos + 1) % 4;
			if (pos)
				continue;

			val = u32_encode_bits(v[0], GENMASK(7, 0)) |
			      u32_encode_bits(v[1], GENMASK(15, 8)) |
			      u32_encode_bits(v[2], GENMASK(23, 16)) |
			      u32_encode_bits(v[3], GENMASK(31, 24));

			rtw89_mac_txpwr_write32(rtwdev, phy_idx, *addr, val);
			*addr += 4;
		}
	}
}

static void rtw89_phy_set_txpwr_byrate_be(struct rtw89_dev *rtwdev,
					  const struct rtw89_chan *chan,
					  enum rtw89_phy_idx phy_idx)
{
	u32 addr = R_BE_PWR_BY_RATE;
	u8 band = chan->band_type;
	u8 bw, nss;

	rtw89_debug(rtwdev, RTW89_DBG_TXPWR,
		    "[TXPWR] set txpwr byrate on band %d\n", band);

	for (bw = 0; bw <= RTW89_CHANNEL_WIDTH_320; bw++)
		for (nss = 0; nss <= RTW89_NSS_2; nss++)
			__phy_set_txpwr_byrate_be(rtwdev, band, bw, nss,
						  &addr, phy_idx);
}

static void rtw89_phy_set_txpwr_offset_be(struct rtw89_dev *rtwdev,
					  const struct rtw89_chan *chan,
					  enum rtw89_phy_idx phy_idx)
{
	struct rtw89_rate_desc desc = {
		.nss = RTW89_NSS_1,
		.rs = RTW89_RS_OFFSET,
	};
	u8 band = chan->band_type;
	s8 v[RTW89_RATE_OFFSET_NUM_BE] = {};
	u32 val;

	rtw89_debug(rtwdev, RTW89_DBG_TXPWR,
		    "[TXPWR] set txpwr offset on band %d\n", band);

	for (desc.idx = 0; desc.idx < RTW89_RATE_OFFSET_NUM_BE; desc.idx++)
		v[desc.idx] = rtw89_phy_read_txpwr_byrate(rtwdev, band, 0, &desc);

	val = u32_encode_bits(v[RTW89_RATE_OFFSET_CCK], GENMASK(3, 0)) |
	      u32_encode_bits(v[RTW89_RATE_OFFSET_OFDM], GENMASK(7, 4)) |
	      u32_encode_bits(v[RTW89_RATE_OFFSET_HT], GENMASK(11, 8)) |
	      u32_encode_bits(v[RTW89_RATE_OFFSET_VHT], GENMASK(15, 12)) |
	      u32_encode_bits(v[RTW89_RATE_OFFSET_HE], GENMASK(19, 16)) |
	      u32_encode_bits(v[RTW89_RATE_OFFSET_EHT], GENMASK(23, 20)) |
	      u32_encode_bits(v[RTW89_RATE_OFFSET_DLRU_HE], GENMASK(27, 24)) |
	      u32_encode_bits(v[RTW89_RATE_OFFSET_DLRU_EHT], GENMASK(31, 28));

	rtw89_mac_txpwr_write32(rtwdev, phy_idx, R_BE_PWR_RATE_OFST_CTRL, val);
}

static void
fill_limit_nonbf_bf(struct rtw89_dev *rtwdev, s8 (*ptr)[RTW89_BF_NUM],
		    u8 band, u8 bw, u8 ntx, u8 rs, u8 ch)
{
	int bf;

	for (bf = 0; bf < RTW89_BF_NUM; bf++)
		(*ptr)[bf] = rtw89_phy_read_txpwr_limit(rtwdev, band, bw, ntx,
							rs, bf, ch);
}

static void
fill_limit_nonbf_bf_min(struct rtw89_dev *rtwdev, s8 (*ptr)[RTW89_BF_NUM],
			u8 band, u8 bw, u8 ntx, u8 rs, u8 ch1, u8 ch2)
{
	s8 v1[RTW89_BF_NUM];
	s8 v2[RTW89_BF_NUM];
	int bf;

	fill_limit_nonbf_bf(rtwdev, &v1, band, bw, ntx, rs, ch1);
	fill_limit_nonbf_bf(rtwdev, &v2, band, bw, ntx, rs, ch2);

	for (bf = 0; bf < RTW89_BF_NUM; bf++)
		(*ptr)[bf] = min(v1[bf], v2[bf]);
}

static void phy_fill_limit_20m_be(struct rtw89_dev *rtwdev,
				  struct rtw89_txpwr_limit_be *lmt,
				  u8 band, u8 ntx, u8 ch)
{
	fill_limit_nonbf_bf(rtwdev, &lmt->cck_20m, band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_CCK, ch);
	fill_limit_nonbf_bf(rtwdev, &lmt->cck_40m, band,
			    RTW89_CHANNEL_WIDTH_40, ntx, RTW89_RS_CCK, ch);
	fill_limit_nonbf_bf(rtwdev, &lmt->ofdm, band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_OFDM, ch);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_20m[0], band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_MCS, ch);
}

static void phy_fill_limit_40m_be(struct rtw89_dev *rtwdev,
				  struct rtw89_txpwr_limit_be *lmt,
				  u8 band, u8 ntx, u8 ch, u8 pri_ch)
{
	fill_limit_nonbf_bf(rtwdev, &lmt->cck_20m, band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_CCK, ch - 2);
	fill_limit_nonbf_bf(rtwdev, &lmt->cck_40m, band,
			    RTW89_CHANNEL_WIDTH_40, ntx, RTW89_RS_CCK, ch);

	fill_limit_nonbf_bf(rtwdev, &lmt->ofdm, band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_OFDM, pri_ch);

	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_20m[0], band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_MCS, ch - 2);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_20m[1], band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_MCS, ch + 2);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_40m[0], band,
			    RTW89_CHANNEL_WIDTH_40, ntx, RTW89_RS_MCS, ch);
}

static void phy_fill_limit_80m_be(struct rtw89_dev *rtwdev,
				  struct rtw89_txpwr_limit_be *lmt,
				  u8 band, u8 ntx, u8 ch, u8 pri_ch)
{
	fill_limit_nonbf_bf(rtwdev, &lmt->ofdm, band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_OFDM, pri_ch);

	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_20m[0], band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_MCS, ch - 6);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_20m[1], band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_MCS, ch - 2);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_20m[2], band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_MCS, ch + 2);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_20m[3], band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_MCS, ch + 6);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_40m[0], band,
			    RTW89_CHANNEL_WIDTH_40, ntx, RTW89_RS_MCS, ch - 4);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_40m[1], band,
			    RTW89_CHANNEL_WIDTH_40, ntx, RTW89_RS_MCS, ch + 4);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_80m[0], band,
			    RTW89_CHANNEL_WIDTH_80, ntx, RTW89_RS_MCS, ch);

	fill_limit_nonbf_bf_min(rtwdev, &lmt->mcs_40m_0p5, band,
				RTW89_CHANNEL_WIDTH_40, ntx, RTW89_RS_MCS,
				ch - 4, ch + 4);
}

static void phy_fill_limit_160m_be(struct rtw89_dev *rtwdev,
				   struct rtw89_txpwr_limit_be *lmt,
				   u8 band, u8 ntx, u8 ch, u8 pri_ch)
{
	fill_limit_nonbf_bf(rtwdev, &lmt->ofdm, band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_OFDM, pri_ch);

	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_20m[0], band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_MCS, ch - 14);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_20m[1], band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_MCS, ch - 10);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_20m[2], band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_MCS, ch - 6);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_20m[3], band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_MCS, ch - 2);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_20m[4], band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_MCS, ch + 2);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_20m[5], band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_MCS, ch + 6);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_20m[6], band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_MCS, ch + 10);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_20m[7], band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_MCS, ch + 14);

	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_40m[0], band,
			    RTW89_CHANNEL_WIDTH_40, ntx, RTW89_RS_MCS, ch - 12);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_40m[1], band,
			    RTW89_CHANNEL_WIDTH_40, ntx, RTW89_RS_MCS, ch - 4);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_40m[2], band,
			    RTW89_CHANNEL_WIDTH_40, ntx, RTW89_RS_MCS, ch + 4);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_40m[3], band,
			    RTW89_CHANNEL_WIDTH_40, ntx, RTW89_RS_MCS, ch + 12);

	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_80m[0], band,
			    RTW89_CHANNEL_WIDTH_80, ntx, RTW89_RS_MCS, ch - 8);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_80m[1], band,
			    RTW89_CHANNEL_WIDTH_80, ntx, RTW89_RS_MCS, ch + 8);

	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_160m[0], band,
			    RTW89_CHANNEL_WIDTH_160, ntx, RTW89_RS_MCS, ch);

	fill_limit_nonbf_bf_min(rtwdev, &lmt->mcs_40m_0p5, band,
				RTW89_CHANNEL_WIDTH_40, ntx, RTW89_RS_MCS,
				ch - 12, ch - 4);
	fill_limit_nonbf_bf_min(rtwdev, &lmt->mcs_40m_2p5, band,
				RTW89_CHANNEL_WIDTH_40, ntx, RTW89_RS_MCS,
				ch + 4, ch + 12);
}

static void phy_fill_limit_320m_be(struct rtw89_dev *rtwdev,
				   struct rtw89_txpwr_limit_be *lmt,
				   u8 band, u8 ntx, u8 ch, u8 pri_ch)
{
	fill_limit_nonbf_bf(rtwdev, &lmt->ofdm, band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_OFDM, pri_ch);

	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_20m[0], band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_MCS, ch - 30);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_20m[1], band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_MCS, ch - 26);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_20m[2], band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_MCS, ch - 22);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_20m[3], band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_MCS, ch - 18);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_20m[4], band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_MCS, ch - 14);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_20m[5], band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_MCS, ch - 10);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_20m[6], band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_MCS, ch - 6);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_20m[7], band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_MCS, ch - 2);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_20m[8], band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_MCS, ch + 2);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_20m[9], band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_MCS, ch + 6);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_20m[10], band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_MCS, ch + 10);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_20m[11], band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_MCS, ch + 14);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_20m[12], band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_MCS, ch + 18);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_20m[13], band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_MCS, ch + 22);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_20m[14], band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_MCS, ch + 26);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_20m[15], band,
			    RTW89_CHANNEL_WIDTH_20, ntx, RTW89_RS_MCS, ch + 30);

	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_40m[0], band,
			    RTW89_CHANNEL_WIDTH_40, ntx, RTW89_RS_MCS, ch - 28);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_40m[1], band,
			    RTW89_CHANNEL_WIDTH_40, ntx, RTW89_RS_MCS, ch - 20);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_40m[2], band,
			    RTW89_CHANNEL_WIDTH_40, ntx, RTW89_RS_MCS, ch - 12);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_40m[3], band,
			    RTW89_CHANNEL_WIDTH_40, ntx, RTW89_RS_MCS, ch - 4);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_40m[4], band,
			    RTW89_CHANNEL_WIDTH_40, ntx, RTW89_RS_MCS, ch + 4);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_40m[5], band,
			    RTW89_CHANNEL_WIDTH_40, ntx, RTW89_RS_MCS, ch + 12);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_40m[6], band,
			    RTW89_CHANNEL_WIDTH_40, ntx, RTW89_RS_MCS, ch + 20);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_40m[7], band,
			    RTW89_CHANNEL_WIDTH_40, ntx, RTW89_RS_MCS, ch + 28);

	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_80m[0], band,
			    RTW89_CHANNEL_WIDTH_80, ntx, RTW89_RS_MCS, ch - 24);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_80m[1], band,
			    RTW89_CHANNEL_WIDTH_80, ntx, RTW89_RS_MCS, ch - 8);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_80m[2], band,
			    RTW89_CHANNEL_WIDTH_80, ntx, RTW89_RS_MCS, ch + 8);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_80m[3], band,
			    RTW89_CHANNEL_WIDTH_80, ntx, RTW89_RS_MCS, ch + 24);

	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_160m[0], band,
			    RTW89_CHANNEL_WIDTH_160, ntx, RTW89_RS_MCS, ch - 16);
	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_160m[1], band,
			    RTW89_CHANNEL_WIDTH_160, ntx, RTW89_RS_MCS, ch + 16);

	fill_limit_nonbf_bf(rtwdev, &lmt->mcs_320m, band,
			    RTW89_CHANNEL_WIDTH_320, ntx, RTW89_RS_MCS, ch);

	fill_limit_nonbf_bf_min(rtwdev, &lmt->mcs_40m_0p5, band,
				RTW89_CHANNEL_WIDTH_40, ntx, RTW89_RS_MCS,
				ch - 28, ch - 20);
	fill_limit_nonbf_bf_min(rtwdev, &lmt->mcs_40m_2p5, band,
				RTW89_CHANNEL_WIDTH_40, ntx, RTW89_RS_MCS,
				ch - 12, ch - 4);
	fill_limit_nonbf_bf_min(rtwdev, &lmt->mcs_40m_4p5, band,
				RTW89_CHANNEL_WIDTH_40, ntx, RTW89_RS_MCS,
				ch + 4, ch + 12);
	fill_limit_nonbf_bf_min(rtwdev, &lmt->mcs_40m_6p5, band,
				RTW89_CHANNEL_WIDTH_40, ntx, RTW89_RS_MCS,
				ch + 20, ch + 28);
}

static void rtw89_phy_fill_limit_be(struct rtw89_dev *rtwdev,
				    const struct rtw89_chan *chan,
				    struct rtw89_txpwr_limit_be *lmt,
				    u8 ntx)
{
	u8 band = chan->band_type;
	u8 pri_ch = chan->primary_channel;
	u8 ch = chan->channel;
	u8 bw = chan->band_width;

	memset(lmt, 0, sizeof(*lmt));

	switch (bw) {
	case RTW89_CHANNEL_WIDTH_20:
		phy_fill_limit_20m_be(rtwdev, lmt, band, ntx, ch);
		break;
	case RTW89_CHANNEL_WIDTH_40:
		phy_fill_limit_40m_be(rtwdev, lmt, band, ntx, ch, pri_ch);
		break;
	case RTW89_CHANNEL_WIDTH_80:
		phy_fill_limit_80m_be(rtwdev, lmt, band, ntx, ch, pri_ch);
		break;
	case RTW89_CHANNEL_WIDTH_160:
		phy_fill_limit_160m_be(rtwdev, lmt, band, ntx, ch, pri_ch);
		break;
	case RTW89_CHANNEL_WIDTH_320:
		phy_fill_limit_320m_be(rtwdev, lmt, band, ntx, ch, pri_ch);
		break;
	}
}

static void rtw89_phy_set_txpwr_limit_be(struct rtw89_dev *rtwdev,
					 const struct rtw89_chan *chan,
					 enum rtw89_phy_idx phy_idx)
{
	struct rtw89_txpwr_limit_be lmt;
	const s8 *ptr;
	u32 addr, val;
	u8 i, j;

	BUILD_BUG_ON(sizeof(struct rtw89_txpwr_limit_be) !=
		     RTW89_TXPWR_LMT_PAGE_SIZE_BE);

	rtw89_debug(rtwdev, RTW89_DBG_TXPWR,
		    "[TXPWR] set txpwr limit on band %d bw %d\n",
		    chan->band_type, chan->band_width);

	addr = R_BE_PWR_LMT;
	for (i = 0; i <= RTW89_NSS_2; i++) {
		rtw89_phy_fill_limit_be(rtwdev, chan, &lmt, i);

		ptr = (s8 *)&lmt;
		for (j = 0; j < RTW89_TXPWR_LMT_PAGE_SIZE_BE;
		     j += 4, addr += 4, ptr += 4) {
			val = u32_encode_bits(ptr[0], GENMASK(7, 0)) |
			      u32_encode_bits(ptr[1], GENMASK(15, 8)) |
			      u32_encode_bits(ptr[2], GENMASK(23, 16)) |
			      u32_encode_bits(ptr[3], GENMASK(31, 24));

			rtw89_mac_txpwr_write32(rtwdev, phy_idx, addr, val);
		}
	}
}

static void fill_limit_ru_each(struct rtw89_dev *rtwdev, u8 index,
			       struct rtw89_txpwr_limit_ru_be *lmt_ru,
			       u8 band, u8 ntx, u8 ch)
{
	lmt_ru->ru26[index] =
		rtw89_phy_read_txpwr_limit_ru(rtwdev, band, RTW89_RU26, ntx, ch);
	lmt_ru->ru52[index] =
		rtw89_phy_read_txpwr_limit_ru(rtwdev, band, RTW89_RU52, ntx, ch);
	lmt_ru->ru106[index] =
		rtw89_phy_read_txpwr_limit_ru(rtwdev, band, RTW89_RU106, ntx, ch);
	lmt_ru->ru52_26[index] =
		rtw89_phy_read_txpwr_limit_ru(rtwdev, band, RTW89_RU52_26, ntx, ch);
	lmt_ru->ru106_26[index] =
		rtw89_phy_read_txpwr_limit_ru(rtwdev, band, RTW89_RU106_26, ntx, ch);
}

static void phy_fill_limit_ru_20m_be(struct rtw89_dev *rtwdev,
				     struct rtw89_txpwr_limit_ru_be *lmt_ru,
				     u8 band, u8 ntx, u8 ch)
{
	fill_limit_ru_each(rtwdev, 0, lmt_ru, band, ntx, ch);
}

static void phy_fill_limit_ru_40m_be(struct rtw89_dev *rtwdev,
				     struct rtw89_txpwr_limit_ru_be *lmt_ru,
				     u8 band, u8 ntx, u8 ch)
{
	fill_limit_ru_each(rtwdev, 0, lmt_ru, band, ntx, ch - 2);
	fill_limit_ru_each(rtwdev, 1, lmt_ru, band, ntx, ch + 2);
}

static void phy_fill_limit_ru_80m_be(struct rtw89_dev *rtwdev,
				     struct rtw89_txpwr_limit_ru_be *lmt_ru,
				     u8 band, u8 ntx, u8 ch)
{
	fill_limit_ru_each(rtwdev, 0, lmt_ru, band, ntx, ch - 6);
	fill_limit_ru_each(rtwdev, 1, lmt_ru, band, ntx, ch - 2);
	fill_limit_ru_each(rtwdev, 2, lmt_ru, band, ntx, ch + 2);
	fill_limit_ru_each(rtwdev, 3, lmt_ru, band, ntx, ch + 6);
}

static void phy_fill_limit_ru_160m_be(struct rtw89_dev *rtwdev,
				      struct rtw89_txpwr_limit_ru_be *lmt_ru,
				      u8 band, u8 ntx, u8 ch)
{
	fill_limit_ru_each(rtwdev, 0, lmt_ru, band, ntx, ch - 14);
	fill_limit_ru_each(rtwdev, 1, lmt_ru, band, ntx, ch - 10);
	fill_limit_ru_each(rtwdev, 2, lmt_ru, band, ntx, ch - 6);
	fill_limit_ru_each(rtwdev, 3, lmt_ru, band, ntx, ch - 2);
	fill_limit_ru_each(rtwdev, 4, lmt_ru, band, ntx, ch + 2);
	fill_limit_ru_each(rtwdev, 5, lmt_ru, band, ntx, ch + 6);
	fill_limit_ru_each(rtwdev, 6, lmt_ru, band, ntx, ch + 10);
	fill_limit_ru_each(rtwdev, 7, lmt_ru, band, ntx, ch + 14);
}

static void phy_fill_limit_ru_320m_be(struct rtw89_dev *rtwdev,
				      struct rtw89_txpwr_limit_ru_be *lmt_ru,
				      u8 band, u8 ntx, u8 ch)
{
	fill_limit_ru_each(rtwdev, 0, lmt_ru, band, ntx, ch - 30);
	fill_limit_ru_each(rtwdev, 1, lmt_ru, band, ntx, ch - 26);
	fill_limit_ru_each(rtwdev, 2, lmt_ru, band, ntx, ch - 22);
	fill_limit_ru_each(rtwdev, 3, lmt_ru, band, ntx, ch - 18);
	fill_limit_ru_each(rtwdev, 4, lmt_ru, band, ntx, ch - 14);
	fill_limit_ru_each(rtwdev, 5, lmt_ru, band, ntx, ch - 10);
	fill_limit_ru_each(rtwdev, 6, lmt_ru, band, ntx, ch - 6);
	fill_limit_ru_each(rtwdev, 7, lmt_ru, band, ntx, ch - 2);
	fill_limit_ru_each(rtwdev, 8, lmt_ru, band, ntx, ch + 2);
	fill_limit_ru_each(rtwdev, 9, lmt_ru, band, ntx, ch + 6);
	fill_limit_ru_each(rtwdev, 10, lmt_ru, band, ntx, ch + 10);
	fill_limit_ru_each(rtwdev, 11, lmt_ru, band, ntx, ch + 14);
	fill_limit_ru_each(rtwdev, 12, lmt_ru, band, ntx, ch + 18);
	fill_limit_ru_each(rtwdev, 13, lmt_ru, band, ntx, ch + 22);
	fill_limit_ru_each(rtwdev, 14, lmt_ru, band, ntx, ch + 26);
	fill_limit_ru_each(rtwdev, 15, lmt_ru, band, ntx, ch + 30);
}

static void rtw89_phy_fill_limit_ru_be(struct rtw89_dev *rtwdev,
				       const struct rtw89_chan *chan,
				       struct rtw89_txpwr_limit_ru_be *lmt_ru,
				       u8 ntx)
{
	u8 band = chan->band_type;
	u8 ch = chan->channel;
	u8 bw = chan->band_width;

	memset(lmt_ru, 0, sizeof(*lmt_ru));

	switch (bw) {
	case RTW89_CHANNEL_WIDTH_20:
		phy_fill_limit_ru_20m_be(rtwdev, lmt_ru, band, ntx, ch);
		break;
	case RTW89_CHANNEL_WIDTH_40:
		phy_fill_limit_ru_40m_be(rtwdev, lmt_ru, band, ntx, ch);
		break;
	case RTW89_CHANNEL_WIDTH_80:
		phy_fill_limit_ru_80m_be(rtwdev, lmt_ru, band, ntx, ch);
		break;
	case RTW89_CHANNEL_WIDTH_160:
		phy_fill_limit_ru_160m_be(rtwdev, lmt_ru, band, ntx, ch);
		break;
	case RTW89_CHANNEL_WIDTH_320:
		phy_fill_limit_ru_320m_be(rtwdev, lmt_ru, band, ntx, ch);
		break;
	}
}

static
void rtw89_phy_fill_limit_ru484_242_be(struct rtw89_dev *rtwdev,
				       s8 (*lmt)[RTW89_RU484_242_SEC_NUM_BE],
				       u8 ntx, u8 band, u8 ch, u8 bw)
{
	switch (bw) {
	case RTW89_CHANNEL_WIDTH_80:
		(*lmt)[0] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							  RTW89_RU484_242,
							  ntx, ch);
		break;
	case RTW89_CHANNEL_WIDTH_160:
		(*lmt)[0] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							  RTW89_RU484_242,
							  ntx, ch - 8);
		(*lmt)[1] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							  RTW89_RU484_242,
							  ntx, ch + 8);
		break;
	case RTW89_CHANNEL_WIDTH_320:
		(*lmt)[0] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							  RTW89_RU484_242,
							  ntx, ch - 24);
		(*lmt)[1] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							  RTW89_RU484_242,
							  ntx, ch - 8);
		(*lmt)[2] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							  RTW89_RU484_242,
							  ntx, ch + 8);
		(*lmt)[3] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							  RTW89_RU484_242,
							  ntx, ch + 24);
		break;
	}
}

static
void rtw89_phy_fill_limit_ru996_484_be(struct rtw89_dev *rtwdev,
				       s8 (*lmt)[RTW89_RU996_484_SEC_NUM_BE],
				       u8 ntx, u8 band, u8 ch, u8 bw)
{
	switch (bw) {
	case RTW89_CHANNEL_WIDTH_160:
		(*lmt)[0] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							  RTW89_RU996_484,
							  ntx, ch);
		break;
	case RTW89_CHANNEL_WIDTH_320:
		(*lmt)[0] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							  RTW89_RU996_484,
							  ntx, ch - 16);
		(*lmt)[1] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							  RTW89_RU996_484,
							  ntx, ch + 16);
		break;
	}
}

static
void rtw89_phy_fill_limit_ru996_484_242_be(struct rtw89_dev *rtwdev,
					   s8 (*lmt)[RTW89_RU996_484_242_SEC_NUM_BE],
					   u8 ntx, u8 band, u8 ch, u8 bw)
{
	switch (bw) {
	case RTW89_CHANNEL_WIDTH_160:
		(*lmt)[0] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							  RTW89_RU996_484_242,
							  ntx, ch);
		break;
	case RTW89_CHANNEL_WIDTH_320:
		(*lmt)[0] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							  RTW89_RU996_484_242,
							  ntx, ch - 16);
		(*lmt)[1] = rtw89_phy_read_txpwr_limit_ru(rtwdev, band,
							  RTW89_RU996_484_242,
							  ntx, ch + 16);
		break;
	}
}

static
void rtw89_phy_fill_limit_large_mru_be(struct rtw89_dev *rtwdev,
				       const struct rtw89_chan *chan,
				       struct rtw89_txpwr_limit_large_mru_be *lmt,
				       bool has_bf)
{
	u8 band = chan->band_type;
	u8 ch = chan->channel;
	u8 bw = chan->band_width;
	int i;

	memset(lmt, 0, sizeof(*lmt));

	if (has_bf)
		return;

	for (i = 0; i <= RTW89_NSS_2; i++) {
		rtw89_phy_fill_limit_ru484_242_be(rtwdev, &lmt->ru484_242[i],
						  i, band, ch, bw);
		rtw89_phy_fill_limit_ru996_484_be(rtwdev, &lmt->ru996_484[i],
						  i, band, ch, bw);
		rtw89_phy_fill_limit_ru996_484_242_be(rtwdev, &lmt->ru996_484_242[i],
						      i, band, ch, bw);
	}
}

static
void rtw89_phy_conf_limit_large_mru_be(struct rtw89_dev *rtwdev,
				       const struct rtw89_chan *chan,
				       enum rtw89_phy_idx phy_idx,
				       bool has_bf)
{
	struct rtw89_txpwr_limit_large_mru_be lmt_lmru;
	u32 addr, val;

	rtw89_phy_fill_limit_large_mru_be(rtwdev, chan, &lmt_lmru, has_bf);

	addr = has_bf ? R_BE_TXAGC_MAX_1TX_BF_RU484_242_0 :
			R_BE_TXAGC_MAX_1TX_RU484_242_0;

	val = u32_encode_bits(lmt_lmru.ru484_242[RTW89_NSS_1][0], GENMASK(7, 0)) |
	      u32_encode_bits(lmt_lmru.ru484_242[RTW89_NSS_1][1], GENMASK(15, 8)) |
	      u32_encode_bits(lmt_lmru.ru484_242[RTW89_NSS_1][2], GENMASK(23, 16)) |
	      u32_encode_bits(lmt_lmru.ru484_242[RTW89_NSS_1][3], GENMASK(31, 24));

	rtw89_mac_txpwr_write32(rtwdev, phy_idx, addr, val);

	val = u32_encode_bits(lmt_lmru.ru484_242[RTW89_NSS_2][0], GENMASK(7, 0)) |
	      u32_encode_bits(lmt_lmru.ru484_242[RTW89_NSS_2][1], GENMASK(15, 8)) |
	      u32_encode_bits(lmt_lmru.ru484_242[RTW89_NSS_2][2], GENMASK(23, 16)) |
	      u32_encode_bits(lmt_lmru.ru484_242[RTW89_NSS_2][3], GENMASK(31, 24));

	rtw89_mac_txpwr_write32(rtwdev, phy_idx, addr + 4, val);

	addr = has_bf ? R_BE_TXAGC_MAX_1TX_BF_RU996_484_0 :
			R_BE_TXAGC_MAX_1TX_RU996_484_0;

	val = u32_encode_bits(lmt_lmru.ru996_484[RTW89_NSS_1][0], GENMASK(7, 0)) |
	      u32_encode_bits(lmt_lmru.ru996_484[RTW89_NSS_1][1], GENMASK(15, 8)) |
	      u32_encode_bits(lmt_lmru.ru996_484[RTW89_NSS_2][0], GENMASK(23, 16)) |
	      u32_encode_bits(lmt_lmru.ru996_484[RTW89_NSS_2][1], GENMASK(31, 24));

	rtw89_mac_txpwr_write32(rtwdev, phy_idx, addr, val);

	addr = has_bf ? R_BE_TXAGC_MAX_1TX_BF_RU996_484_242_0 :
			R_BE_TXAGC_MAX_1TX_RU996_484_242_0;

	val = u32_encode_bits(lmt_lmru.ru996_484_242[RTW89_NSS_1][0], GENMASK(7, 0)) |
	      u32_encode_bits(lmt_lmru.ru996_484_242[RTW89_NSS_1][1], GENMASK(15, 8)) |
	      u32_encode_bits(lmt_lmru.ru996_484_242[RTW89_NSS_2][0], GENMASK(23, 16)) |
	      u32_encode_bits(lmt_lmru.ru996_484_242[RTW89_NSS_2][1], GENMASK(31, 24));

	rtw89_mac_txpwr_write32(rtwdev, phy_idx, addr, val);
}

static void rtw89_phy_set_txpwr_limit_ru_be(struct rtw89_dev *rtwdev,
					    const struct rtw89_chan *chan,
					    enum rtw89_phy_idx phy_idx)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_txpwr_limit_ru_be lmt_ru;
	struct rtw89_hal *hal = &rtwdev->hal;
	const s8 *ptr;
	u32 addr, val;
	u8 i, j;

	BUILD_BUG_ON(sizeof(struct rtw89_txpwr_limit_ru_be) !=
		     RTW89_TXPWR_LMT_RU_PAGE_SIZE_BE);

	rtw89_debug(rtwdev, RTW89_DBG_TXPWR,
		    "[TXPWR] set txpwr limit ru on band %d bw %d\n",
		    chan->band_type, chan->band_width);

	addr = R_BE_PWR_RU_LMT;
	for (i = 0; i <= RTW89_NSS_2; i++) {
		rtw89_phy_fill_limit_ru_be(rtwdev, chan, &lmt_ru, i);

		ptr = (s8 *)&lmt_ru;
		for (j = 0; j < RTW89_TXPWR_LMT_RU_PAGE_SIZE_BE;
		     j += 4, addr += 4, ptr += 4) {
			val = u32_encode_bits(ptr[0], GENMASK(7, 0)) |
			      u32_encode_bits(ptr[1], GENMASK(15, 8)) |
			      u32_encode_bits(ptr[2], GENMASK(23, 16)) |
			      u32_encode_bits(ptr[3], GENMASK(31, 24));

			rtw89_mac_txpwr_write32(rtwdev, phy_idx, addr, val);
		}
	}

	if (!(chip->chip_id == RTL8922D && hal->cid == RTL8922D_CID7090))
		return;

	rtw89_phy_conf_limit_large_mru_be(rtwdev, chan, phy_idx, false);
	rtw89_phy_conf_limit_large_mru_be(rtwdev, chan, phy_idx, true);
}

const struct rtw89_phy_gen_def rtw89_phy_gen_be = {
	.cr_base = 0x20000,
	.physt_bmp_start = R_PHY_STS_BITMAP_ADDR_START,
	.physt_bmp_eht = R_PHY_STS_BITMAP_EHT,
	.physt_ie_len = {32, 40, 24, 24, 8, 8, 8, 8, VAR_LEN, 8, VAR_LEN, 176, VAR_LEN,
			 VAR_LEN, VAR_LEN, VAR_LEN, VAR_LEN, VAR_LEN, 88, 56, VAR_LEN,
			 VAR_LEN, VAR_LEN, 0, 24, 24, 24, 24, 32, 32, 32, 32},
	.physt_gen = 1,
	.ccx = &rtw89_ccx_regs_be,
	.physts = &rtw89_physts_regs_be,
	.cfo = &rtw89_cfo_regs_be,
	.bb_wrap = &rtw89_bb_wrap_regs_be,
	.phy0_phy1_offset = rtw89_phy0_phy1_offset_be,
	.config_bb_gain = rtw89_phy_config_bb_gain_be,
	.preinit_rf_nctl = rtw89_phy_preinit_rf_nctl_be,
	.bb_wrap_init = rtw89_phy_bb_wrap_init_be,
	.ch_info_init = rtw89_phy_ch_info_init_be,

	.set_txpwr_byrate = rtw89_phy_set_txpwr_byrate_be,
	.set_txpwr_offset = rtw89_phy_set_txpwr_offset_be,
	.set_txpwr_limit = rtw89_phy_set_txpwr_limit_be,
	.set_txpwr_limit_ru = rtw89_phy_set_txpwr_limit_ru_be,
};
EXPORT_SYMBOL(rtw89_phy_gen_be);

const struct rtw89_phy_gen_def rtw89_phy_gen_be_v1 = {
	.cr_base = 0x0,
	.physt_bmp_start = R_PHY_STS_BITMAP_ADDR_START_BE4,
	.physt_bmp_eht = R_PHY_STS_BITMAP_EHT_BE4,
	.physt_ie_len = {32, 40, 24, 24, 16, 16, 16, 16, VAR_LEN, VAR_LEN, VAR_LEN, 168,
			 VAR_LEN, VAR_LEN, VAR_LEN, VAR_LEN, VAR_LEN, VAR_LEN, 32, 56,
			 96, VAR_LEN, VAR_LEN, 0, 24, 24, 24, 24, 32, 32, 32, 32},
	.physt_gen = 2,
	.ccx = &rtw89_ccx_regs_be_v1,
	.physts = &rtw89_physts_regs_be_v1,
	.cfo = &rtw89_cfo_regs_be_v1,
	.bb_wrap = &rtw89_bb_wrap_regs_be_v1,
	.phy0_phy1_offset = rtw89_phy0_phy1_offset_be_v1,
	.config_bb_gain = rtw89_phy_config_bb_gain_be,
	.preinit_rf_nctl = rtw89_phy_preinit_rf_nctl_be_v1,
	.bb_wrap_init = rtw89_phy_bb_wrap_init_be,
	.ch_info_init = rtw89_phy_ch_info_init_be_v1,

	.set_txpwr_byrate = rtw89_phy_set_txpwr_byrate_be,
	.set_txpwr_offset = rtw89_phy_set_txpwr_offset_be,
	.set_txpwr_limit = rtw89_phy_set_txpwr_limit_be,
	.set_txpwr_limit_ru = rtw89_phy_set_txpwr_limit_ru_be,
};
EXPORT_SYMBOL(rtw89_phy_gen_be_v1);
