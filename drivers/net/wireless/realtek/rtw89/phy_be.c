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

const struct rtw89_phy_gen_def rtw89_phy_gen_be = {
	.cr_base = 0x20000,
	.ccx = &rtw89_ccx_regs_be,
	.physts = &rtw89_physts_regs_be,

	.set_txpwr_byrate = rtw89_phy_set_txpwr_byrate_be,
};
EXPORT_SYMBOL(rtw89_phy_gen_be);
