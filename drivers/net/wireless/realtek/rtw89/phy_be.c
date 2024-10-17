// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2023  Realtek Corporation
 */

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
};

static const struct rtw89_physts_regs rtw89_physts_regs_be = {
	.setting_addr = R_PLCP_HISTOGRAM,
	.dis_trigger_fail_mask = B_STS_DIS_TRIG_BY_FAIL,
	.dis_trigger_brk_mask = B_STS_DIS_TRIG_BY_BRK,
};

static const struct rtw89_cfo_regs rtw89_cfo_regs_be = {
	.comp = R_DCFO_WEIGHT_V1,
	.weighting_mask = B_DCFO_WEIGHT_MSK_V1,
	.comp_seg0 = R_DCFO_OPT_V1,
	.valid_0_mask = B_DCFO_OPT_EN_V1,
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

static
void rtw89_phy_bb_wrap_pwr_by_macid_init(struct rtw89_dev *rtwdev)
{
	u32 macid_idx, cr, base_macid_lmt, max_macid = 32;

	base_macid_lmt = R_BE_PWR_MACID_LMT_BASE;

	for (macid_idx = 0; macid_idx < 4 * max_macid; macid_idx += 4) {
		cr = base_macid_lmt + macid_idx;
		rtw89_write32(rtwdev, cr, 0x03007F7F);
	}
}

static
void rtw89_phy_bb_wrap_tx_path_by_macid_init(struct rtw89_dev *rtwdev)
{
	int i, max_macid = 32;
	u32 cr = R_BE_PWR_MACID_PATH_BASE;

	for (i = 0; i < max_macid; i++, cr += 4)
		rtw89_write32(rtwdev, cr, 0x03C86000);
}

static void rtw89_phy_bb_wrap_tpu_set_all(struct rtw89_dev *rtwdev,
					  enum rtw89_mac_idx mac_idx)
{
	u32 addr;

	for (addr = R_BE_PWR_BY_RATE; addr <= R_BE_PWR_BY_RATE_END; addr += 4)
		rtw89_write32(rtwdev, addr, 0);
	for (addr = R_BE_PWR_RULMT_START; addr <= R_BE_PWR_RULMT_END; addr += 4)
		rtw89_write32(rtwdev, addr, 0);
	for (addr = R_BE_PWR_RATE_OFST_CTRL; addr <= R_BE_PWR_RATE_OFST_END; addr += 4)
		rtw89_write32(rtwdev, addr, 0);

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
	addr = rtw89_mac_reg_by_idx(rtwdev, R_BE_PWR_BOOST, mac_idx);
	rtw89_write32_mask(rtwdev, addr, B_BE_PWR_FORCE_RATE_ON, 0);
	addr = rtw89_mac_reg_by_idx(rtwdev, R_BE_PWR_OFST_RULMT, mac_idx);
	rtw89_write32_mask(rtwdev, addr, B_BE_PWR_FORCE_RU_ENON, 0);
	rtw89_write32_mask(rtwdev, addr, B_BE_PWR_FORCE_RU_ON, 0);
	addr = rtw89_mac_reg_by_idx(rtwdev, R_BE_PWR_FORCE_MACID, mac_idx);
	rtw89_write32_mask(rtwdev, addr, B_BE_PWR_FORCE_MACID_ON, 0);
	addr = rtw89_mac_reg_by_idx(rtwdev, R_BE_PWR_COEX_CTRL, mac_idx);
	rtw89_write32_mask(rtwdev, addr, B_BE_PWR_FORCE_COEX_ON, 0);
	addr = rtw89_mac_reg_by_idx(rtwdev, R_BE_PWR_RATE_CTRL, mac_idx);
	rtw89_write32_mask(rtwdev, addr, B_BE_FORCE_PWR_BY_RATE_EN, 0);
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
	rtw89_phy_bb_wrap_pwr_by_macid_init(rtwdev);
	rtw89_phy_bb_wrap_tx_path_by_macid_init(rtwdev);
	rtw89_phy_bb_wrap_listen_path_en_init(rtwdev);
	rtw89_phy_bb_wrap_force_cr_init(rtwdev, mac_idx);
	rtw89_phy_bb_wrap_ftm_init(rtwdev, mac_idx);
	rtw89_phy_bb_wrap_tpu_set_all(rtwdev, mac_idx);
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

static void rtw89_phy_set_txpwr_limit_ru_be(struct rtw89_dev *rtwdev,
					    const struct rtw89_chan *chan,
					    enum rtw89_phy_idx phy_idx)
{
	struct rtw89_txpwr_limit_ru_be lmt_ru;
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
}

const struct rtw89_phy_gen_def rtw89_phy_gen_be = {
	.cr_base = 0x20000,
	.ccx = &rtw89_ccx_regs_be,
	.physts = &rtw89_physts_regs_be,
	.cfo = &rtw89_cfo_regs_be,
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
