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

	.set_txpwr_byrate = rtw89_phy_set_txpwr_byrate_be,
	.set_txpwr_offset = rtw89_phy_set_txpwr_offset_be,
	.set_txpwr_limit = rtw89_phy_set_txpwr_limit_be,
	.set_txpwr_limit_ru = rtw89_phy_set_txpwr_limit_ru_be,
};
EXPORT_SYMBOL(rtw89_phy_gen_be);
