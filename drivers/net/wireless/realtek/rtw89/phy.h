/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#ifndef __RTW89_PHY_H__
#define __RTW89_PHY_H__

#include "core.h"

#define RTW89_BBMCU_ADDR_OFFSET	0x30000
#define RTW89_RF_ADDR_ADSEL_MASK  BIT(16)

#define get_phy_headline(addr)		FIELD_GET(GENMASK(31, 28), addr)
#define PHY_HEADLINE_VALID	0xf
#define get_phy_target(addr)		FIELD_GET(GENMASK(27, 0), addr)
#define get_phy_compare(rfe, cv)	(FIELD_PREP(GENMASK(23, 16), rfe) | \
					 FIELD_PREP(GENMASK(7, 0), cv))

#define get_phy_cond(addr)		FIELD_GET(GENMASK(31, 28), addr)
#define get_phy_cond_rfe(addr)		FIELD_GET(GENMASK(23, 16), addr)
#define get_phy_cond_pkg(addr)		FIELD_GET(GENMASK(15, 8), addr)
#define get_phy_cond_cv(addr)		FIELD_GET(GENMASK(7, 0), addr)
#define phy_div(a, b) ({typeof(b) _b = (b); (_b) ? ((a) / (_b)) : 0; })
#define PHY_COND_BRANCH_IF	0x8
#define PHY_COND_BRANCH_ELIF	0x9
#define PHY_COND_BRANCH_ELSE	0xa
#define PHY_COND_BRANCH_END	0xb
#define PHY_COND_CHECK		0x4
#define PHY_COND_DONT_CARE	0xff

#define RA_MASK_CCK_RATES	GENMASK_ULL(3, 0)
#define RA_MASK_OFDM_RATES	GENMASK_ULL(11, 4)
#define RA_MASK_SUBCCK_RATES	0x5ULL
#define RA_MASK_SUBOFDM_RATES	0x10ULL
#define RA_MASK_HT_1SS_RATES	GENMASK_ULL(19, 12)
#define RA_MASK_HT_2SS_RATES	GENMASK_ULL(31, 24)
#define RA_MASK_HT_3SS_RATES	GENMASK_ULL(43, 36)
#define RA_MASK_HT_4SS_RATES	GENMASK_ULL(55, 48)
#define RA_MASK_HT_RATES	GENMASK_ULL(55, 12)
#define RA_MASK_VHT_1SS_RATES	GENMASK_ULL(21, 12)
#define RA_MASK_VHT_2SS_RATES	GENMASK_ULL(33, 24)
#define RA_MASK_VHT_3SS_RATES	GENMASK_ULL(45, 36)
#define RA_MASK_VHT_4SS_RATES	GENMASK_ULL(57, 48)
#define RA_MASK_VHT_RATES	GENMASK_ULL(57, 12)
#define RA_MASK_HE_1SS_RATES	GENMASK_ULL(23, 12)
#define RA_MASK_HE_2SS_RATES	GENMASK_ULL(35, 24)
#define RA_MASK_HE_3SS_RATES	GENMASK_ULL(47, 36)
#define RA_MASK_HE_4SS_RATES	GENMASK_ULL(59, 48)
#define RA_MASK_HE_RATES	GENMASK_ULL(59, 12)
#define RA_MASK_EHT_1SS_RATES	GENMASK_ULL(27, 12)
#define RA_MASK_EHT_2SS_RATES	GENMASK_ULL(43, 28)
#define RA_MASK_EHT_3SS_RATES	GENMASK_ULL(59, 44)
#define RA_MASK_EHT_4SS_RATES	GENMASK_ULL(62, 60)
#define RA_MASK_EHT_1SS_MCS0_11	GENMASK_ULL(23, 12)
#define RA_MASK_EHT_2SS_MCS0_11	GENMASK_ULL(39, 28)
#define RA_MASK_EHT_3SS_MCS0_11	GENMASK_ULL(55, 44)
#define RA_MASK_EHT_4SS_MCS0_11	GENMASK_ULL(62, 60)
#define RA_MASK_EHT_RATES	GENMASK_ULL(62, 12)

#define CFO_TRK_ENABLE_TH (2 << 2)
#define CFO_TRK_STOP_TH_4 (30 << 2)
#define CFO_TRK_STOP_TH_3 (20 << 2)
#define CFO_TRK_STOP_TH_2 (10 << 2)
#define CFO_TRK_STOP_TH_1 (03 << 2)
#define CFO_TRK_STOP_TH (2 << 2)
#define CFO_SW_COMP_FINE_TUNE (2 << 2)
#define CFO_PERIOD_CNT 15
#define CFO_BOUND 64
#define CFO_TP_UPPER 100
#define CFO_TP_LOWER 50
#define CFO_COMP_PERIOD 250
#define CFO_COMP_WEIGHT 8
#define MAX_CFO_TOLERANCE 30
#define CFO_TF_CNT_TH 300

#define UL_TB_TF_CNT_L2H_TH 100
#define UL_TB_TF_CNT_H2L_TH 70

#define ANTDIV_TRAINNING_CNT 2
#define ANTDIV_TRAINNING_INTVL 30
#define ANTDIV_DELAY 110
#define ANTDIV_TP_DIFF_TH_HIGH 100
#define ANTDIV_TP_DIFF_TH_LOW 5
#define ANTDIV_EVM_DIFF_TH 8
#define ANTDIV_RSSI_DIFF_TH 3

#define CCX_MAX_PERIOD 2097
#define CCX_MAX_PERIOD_UNIT 32
#define MS_TO_4US_RATIO 250
#define ENV_MNTR_FAIL_DWORD 0xffffffff
#define ENV_MNTR_IFSCLM_HIS_MAX 127
#define PERMIL 1000
#define PERCENT 100
#define IFS_CLM_TH0_UPPER 64
#define IFS_CLM_TH_MUL 4
#define IFS_CLM_TH_START_IDX 0

#define TIA0_GAIN_A 12
#define TIA0_GAIN_G 16
#define LNA0_GAIN (-24)
#define U4_MAX_BIT 3
#define U8_MAX_BIT 7
#define DIG_GAIN_SHIFT 2
#define DIG_GAIN 8

#define LNA_IDX_MAX 6
#define LNA_IDX_MIN 0
#define TIA_IDX_MAX 1
#define TIA_IDX_MIN 0
#define RXB_IDX_MAX 31
#define RXB_IDX_MIN 0

#define IGI_RSSI_MAX 110
#define PD_TH_MAX_RSSI 70
#define PD_TH_MIN_RSSI 8
#define CCKPD_TH_MIN_RSSI (-18)
#define PD_TH_BW160_CMP_VAL 9
#define PD_TH_BW80_CMP_VAL 6
#define PD_TH_BW40_CMP_VAL 3
#define PD_TH_BW20_CMP_VAL 0
#define PD_TH_CMP_VAL 3
#define PD_TH_SB_FLTR_CMP_VAL 7

#define PHYSTS_MGNT BIT(RTW89_RX_TYPE_MGNT)
#define PHYSTS_CTRL BIT(RTW89_RX_TYPE_CTRL)
#define PHYSTS_DATA BIT(RTW89_RX_TYPE_DATA)
#define PHYSTS_RSVD BIT(RTW89_RX_TYPE_RSVD)
#define PPDU_FILTER_BITMAP (PHYSTS_MGNT | PHYSTS_DATA)

#define EDCCA_MAX 249
#define EDCCA_TH_L2H_LB 66
#define EDCCA_TH_REF 3
#define EDCCA_HL_DIFF_NORMAL 8
#define RSSI_UNIT_CONVER 110
#define EDCCA_UNIT_CONVER 128
#define EDCCA_PWROFST_DEFAULT 18

enum rtw89_phy_c2h_ra_func {
	RTW89_PHY_C2H_FUNC_STS_RPT,
	RTW89_PHY_C2H_FUNC_MU_GPTBL_RPT,
	RTW89_PHY_C2H_FUNC_TXSTS,
	RTW89_PHY_C2H_FUNC_RA_MAX,
};

enum rtw89_phy_c2h_rfk_log_func {
	RTW89_PHY_C2H_RFK_LOG_FUNC_IQK = 0,
	RTW89_PHY_C2H_RFK_LOG_FUNC_DPK = 1,
	RTW89_PHY_C2H_RFK_LOG_FUNC_DACK = 2,
	RTW89_PHY_C2H_RFK_LOG_FUNC_RXDCK = 3,
	RTW89_PHY_C2H_RFK_LOG_FUNC_TSSI = 4,
	RTW89_PHY_C2H_RFK_LOG_FUNC_TXGAPK = 5,

	RTW89_PHY_C2H_RFK_LOG_FUNC_NUM,
};

enum rtw89_phy_c2h_rfk_report_func {
	RTW89_PHY_C2H_RFK_REPORT_FUNC_STATE = 0,
	RTW89_PHY_C2H_RFK_LOG_TAS_PWR = 6,
};

enum rtw89_phy_c2h_dm_func {
	RTW89_PHY_C2H_DM_FUNC_FW_TEST,
	RTW89_PHY_C2H_DM_FUNC_FW_TRIG_TX_RPT,
	RTW89_PHY_C2H_DM_FUNC_SIGB,
	RTW89_PHY_C2H_DM_FUNC_LOWRT_RTY,
	RTW89_PHY_C2H_DM_FUNC_MCC_DIG,
	RTW89_PHY_C2H_DM_FUNC_NUM,
};

enum rtw89_phy_c2h_class {
	RTW89_PHY_C2H_CLASS_RUA,
	RTW89_PHY_C2H_CLASS_RA,
	RTW89_PHY_C2H_CLASS_DM,
	RTW89_PHY_C2H_RFK_LOG = 0x8,
	RTW89_PHY_C2H_RFK_REPORT = 0x9,
	RTW89_PHY_C2H_CLASS_BTC_MIN = 0x10,
	RTW89_PHY_C2H_CLASS_BTC_MAX = 0x17,
	RTW89_PHY_C2H_CLASS_MAX,
};

enum rtw89_env_monitor_result_level {
	RTW89_PHY_ENV_MON_CCX_FAIL = 0,
	RTW89_PHY_ENV_MON_NHM = BIT(0),
	RTW89_PHY_ENV_MON_CLM = BIT(1),
	RTW89_PHY_ENV_MON_FAHM = BIT(2),
	RTW89_PHY_ENV_MON_IFS_CLM = BIT(3),
	RTW89_PHY_ENV_MON_EDCCA_CLM = BIT(4),
};

#define CCX_US_BASE_RATIO 4
enum rtw89_ccx_unit {
	RTW89_CCX_4_US = 0,
	RTW89_CCX_8_US = 1,
	RTW89_CCX_16_US = 2,
	RTW89_CCX_32_US = 3
};

enum rtw89_phy_status_ie_type {
	RTW89_PHYSTS_IE00_CMN_CCK			= 0,
	RTW89_PHYSTS_IE01_CMN_OFDM			= 1,
	RTW89_PHYSTS_IE02_CMN_EXT_AX			= 2,
	RTW89_PHYSTS_IE03_CMN_EXT_SEG_1			= 3,
	RTW89_PHYSTS_IE04_CMN_EXT_PATH_A		= 4,
	RTW89_PHYSTS_IE05_CMN_EXT_PATH_B		= 5,
	RTW89_PHYSTS_IE06_CMN_EXT_PATH_C		= 6,
	RTW89_PHYSTS_IE07_CMN_EXT_PATH_D		= 7,
	RTW89_PHYSTS_IE08_FTR_CH			= 8,
	RTW89_PHYSTS_IE09_FTR_0				= 9,
	RTW89_PHYSTS_IE10_FTR_PLCP_EXT			= 10,
	RTW89_PHYSTS_IE11_FTR_PLCP_HISTOGRAM		= 11,
	RTW89_PHYSTS_IE12_MU_EIGEN_INFO			= 12,
	RTW89_PHYSTS_IE13_DL_MU_DEF			= 13,
	RTW89_PHYSTS_IE14_TB_UL_CQI			= 14,
	RTW89_PHYSTS_IE15_TB_UL_DEF			= 15,
	RTW89_PHYSTS_IE16_RSVD16			= 16,
	RTW89_PHYSTS_IE17_TB_UL_CTRL			= 17,
	RTW89_PHYSTS_IE18_DBG_OFDM_FD_CMN		= 18,
	RTW89_PHYSTS_IE19_DBG_OFDM_TD_CMN		= 19,
	RTW89_PHYSTS_IE20_DBG_OFDM_FD_USER_SEG_0	= 20,
	RTW89_PHYSTS_IE21_DBG_OFDM_FD_USER_SEG_1	= 21,
	RTW89_PHYSTS_IE22_DBG_OFDM_FD_USER_AGC		= 22,
	RTW89_PHYSTS_IE23_RSVD23			= 23,
	RTW89_PHYSTS_IE24_OFDM_TD_PATH_A		= 24,
	RTW89_PHYSTS_IE25_OFDM_TD_PATH_B		= 25,
	RTW89_PHYSTS_IE26_OFDM_TD_PATH_C		= 26,
	RTW89_PHYSTS_IE27_OFDM_TD_PATH_D		= 27,
	RTW89_PHYSTS_IE28_DBG_CCK_PATH_A		= 28,
	RTW89_PHYSTS_IE29_DBG_CCK_PATH_B		= 29,
	RTW89_PHYSTS_IE30_DBG_CCK_PATH_C		= 30,
	RTW89_PHYSTS_IE31_DBG_CCK_PATH_D		= 31,

	/* keep last */
	RTW89_PHYSTS_IE_NUM,
	RTW89_PHYSTS_IE_MAX = RTW89_PHYSTS_IE_NUM - 1
};

enum rtw89_phy_status_bitmap {
	RTW89_TD_SEARCH_FAIL  = 0,
	RTW89_BRK_BY_TX_PKT   = 1,
	RTW89_CCA_SPOOF       = 2,
	RTW89_OFDM_BRK        = 3,
	RTW89_CCK_BRK         = 4,
	RTW89_DL_MU_SPOOFING  = 5,
	RTW89_HE_MU           = 6,
	RTW89_VHT_MU          = 7,
	RTW89_UL_TB_SPOOFING  = 8,
	RTW89_RSVD_9          = 9,
	RTW89_TRIG_BASE_PPDU  = 10,
	RTW89_CCK_PKT         = 11,
	RTW89_LEGACY_OFDM_PKT = 12,
	RTW89_HT_PKT          = 13,
	RTW89_VHT_PKT         = 14,
	RTW89_HE_PKT          = 15,

	RTW89_PHYSTS_BITMAP_NUM
};

enum rtw89_dig_gain_type {
	RTW89_DIG_GAIN_LNA_G = 0,
	RTW89_DIG_GAIN_TIA_G = 1,
	RTW89_DIG_GAIN_LNA_A = 2,
	RTW89_DIG_GAIN_TIA_A = 3,
	RTW89_DIG_GAIN_MAX = 4
};

enum rtw89_dig_gain_lna_idx {
	RTW89_DIG_GAIN_LNA_IDX1 = 1,
	RTW89_DIG_GAIN_LNA_IDX2 = 2,
	RTW89_DIG_GAIN_LNA_IDX3 = 3,
	RTW89_DIG_GAIN_LNA_IDX4 = 4,
	RTW89_DIG_GAIN_LNA_IDX5 = 5,
	RTW89_DIG_GAIN_LNA_IDX6 = 6
};

enum rtw89_dig_gain_tia_idx {
	RTW89_DIG_GAIN_TIA_IDX0 = 0,
	RTW89_DIG_GAIN_TIA_IDX1 = 1
};

enum rtw89_tssi_bandedge_cfg {
	RTW89_TSSI_BANDEDGE_FLAT,
	RTW89_TSSI_BANDEDGE_LOW,
	RTW89_TSSI_BANDEDGE_MID,
	RTW89_TSSI_BANDEDGE_HIGH,

	RTW89_TSSI_CFG_NUM,
};

enum rtw89_tssi_sbw_idx {
	RTW89_TSSI_SBW20,
	RTW89_TSSI_SBW40_0,
	RTW89_TSSI_SBW40_1,
	RTW89_TSSI_SBW80_0,
	RTW89_TSSI_SBW80_1,
	RTW89_TSSI_SBW80_2,
	RTW89_TSSI_SBW80_3,
	RTW89_TSSI_SBW160_0,
	RTW89_TSSI_SBW160_1,
	RTW89_TSSI_SBW160_2,
	RTW89_TSSI_SBW160_3,
	RTW89_TSSI_SBW160_4,
	RTW89_TSSI_SBW160_5,
	RTW89_TSSI_SBW160_6,
	RTW89_TSSI_SBW160_7,

	RTW89_TSSI_SBW_NUM,
};

struct rtw89_txpwr_byrate_cfg {
	enum rtw89_band band;
	enum rtw89_nss nss;
	enum rtw89_rate_section rs;
	u8 shf;
	u8 len;
	u32 data;
};

struct rtw89_txpwr_track_cfg {
	const s8 (*delta_swingidx_6gb_n)[DELTA_SWINGIDX_SIZE];
	const s8 (*delta_swingidx_6gb_p)[DELTA_SWINGIDX_SIZE];
	const s8 (*delta_swingidx_6ga_n)[DELTA_SWINGIDX_SIZE];
	const s8 (*delta_swingidx_6ga_p)[DELTA_SWINGIDX_SIZE];
	const s8 (*delta_swingidx_5gb_n)[DELTA_SWINGIDX_SIZE];
	const s8 (*delta_swingidx_5gb_p)[DELTA_SWINGIDX_SIZE];
	const s8 (*delta_swingidx_5ga_n)[DELTA_SWINGIDX_SIZE];
	const s8 (*delta_swingidx_5ga_p)[DELTA_SWINGIDX_SIZE];
	const s8 *delta_swingidx_2gb_n;
	const s8 *delta_swingidx_2gb_p;
	const s8 *delta_swingidx_2ga_n;
	const s8 *delta_swingidx_2ga_p;
	const s8 *delta_swingidx_2g_cck_b_n;
	const s8 *delta_swingidx_2g_cck_b_p;
	const s8 *delta_swingidx_2g_cck_a_n;
	const s8 *delta_swingidx_2g_cck_a_p;
};

struct rtw89_phy_dig_gain_cfg {
	const struct rtw89_reg_def *table;
	u8 size;
};

struct rtw89_phy_dig_gain_table {
	const struct rtw89_phy_dig_gain_cfg *cfg_lna_g;
	const struct rtw89_phy_dig_gain_cfg *cfg_tia_g;
	const struct rtw89_phy_dig_gain_cfg *cfg_lna_a;
	const struct rtw89_phy_dig_gain_cfg *cfg_tia_a;
};

struct rtw89_phy_tssi_dbw_table {
	u32 data[RTW89_TSSI_CFG_NUM][RTW89_TSSI_SBW_NUM];
};

struct rtw89_phy_reg3_tbl {
	const struct rtw89_reg3_def *reg3;
	int size;
};

#define DECLARE_PHY_REG3_TBL(_name)			\
const struct rtw89_phy_reg3_tbl _name ## _tbl = {	\
	.reg3 = _name,					\
	.size = ARRAY_SIZE(_name),			\
}

struct rtw89_nbi_reg_def {
	struct rtw89_reg_def notch1_idx;
	struct rtw89_reg_def notch1_frac_idx;
	struct rtw89_reg_def notch1_en;
	struct rtw89_reg_def notch2_idx;
	struct rtw89_reg_def notch2_frac_idx;
	struct rtw89_reg_def notch2_en;
};

struct rtw89_ccx_regs {
	u32 setting_addr;
	u32 edcca_opt_mask;
	u32 measurement_trig_mask;
	u32 trig_opt_mask;
	u32 en_mask;
	u32 ifs_cnt_addr;
	u32 ifs_clm_period_mask;
	u32 ifs_clm_cnt_unit_mask;
	u32 ifs_clm_cnt_clear_mask;
	u32 ifs_collect_en_mask;
	u32 ifs_t1_addr;
	u32 ifs_t1_th_h_mask;
	u32 ifs_t1_en_mask;
	u32 ifs_t1_th_l_mask;
	u32 ifs_t2_addr;
	u32 ifs_t2_th_h_mask;
	u32 ifs_t2_en_mask;
	u32 ifs_t2_th_l_mask;
	u32 ifs_t3_addr;
	u32 ifs_t3_th_h_mask;
	u32 ifs_t3_en_mask;
	u32 ifs_t3_th_l_mask;
	u32 ifs_t4_addr;
	u32 ifs_t4_th_h_mask;
	u32 ifs_t4_en_mask;
	u32 ifs_t4_th_l_mask;
	u32 ifs_clm_tx_cnt_addr;
	u32 ifs_clm_edcca_excl_cca_fa_mask;
	u32 ifs_clm_tx_cnt_msk;
	u32 ifs_clm_cca_addr;
	u32 ifs_clm_ofdmcca_excl_fa_mask;
	u32 ifs_clm_cckcca_excl_fa_mask;
	u32 ifs_clm_fa_addr;
	u32 ifs_clm_ofdm_fa_mask;
	u32 ifs_clm_cck_fa_mask;
	u32 ifs_his_addr;
	u32 ifs_t4_his_mask;
	u32 ifs_t3_his_mask;
	u32 ifs_t2_his_mask;
	u32 ifs_t1_his_mask;
	u32 ifs_avg_l_addr;
	u32 ifs_t2_avg_mask;
	u32 ifs_t1_avg_mask;
	u32 ifs_avg_h_addr;
	u32 ifs_t4_avg_mask;
	u32 ifs_t3_avg_mask;
	u32 ifs_cca_l_addr;
	u32 ifs_t2_cca_mask;
	u32 ifs_t1_cca_mask;
	u32 ifs_cca_h_addr;
	u32 ifs_t4_cca_mask;
	u32 ifs_t3_cca_mask;
	u32 ifs_total_addr;
	u32 ifs_cnt_done_mask;
	u32 ifs_total_mask;
};

struct rtw89_physts_regs {
	u32 setting_addr;
	u32 dis_trigger_fail_mask;
	u32 dis_trigger_brk_mask;
};

struct rtw89_cfo_regs {
	u32 comp;
	u32 weighting_mask;
	u32 comp_seg0;
	u32 valid_0_mask;
};

enum rtw89_bandwidth_section_num_ax {
	RTW89_BW20_SEC_NUM_AX = 8,
	RTW89_BW40_SEC_NUM_AX = 4,
	RTW89_BW80_SEC_NUM_AX = 2,
};

enum rtw89_bandwidth_section_num_be {
	RTW89_BW20_SEC_NUM_BE = 16,
	RTW89_BW40_SEC_NUM_BE = 8,
	RTW89_BW80_SEC_NUM_BE = 4,
	RTW89_BW160_SEC_NUM_BE = 2,
};

#define RTW89_TXPWR_LMT_PAGE_SIZE_AX 40

struct rtw89_txpwr_limit_ax {
	s8 cck_20m[RTW89_BF_NUM];
	s8 cck_40m[RTW89_BF_NUM];
	s8 ofdm[RTW89_BF_NUM];
	s8 mcs_20m[RTW89_BW20_SEC_NUM_AX][RTW89_BF_NUM];
	s8 mcs_40m[RTW89_BW40_SEC_NUM_AX][RTW89_BF_NUM];
	s8 mcs_80m[RTW89_BW80_SEC_NUM_AX][RTW89_BF_NUM];
	s8 mcs_160m[RTW89_BF_NUM];
	s8 mcs_40m_0p5[RTW89_BF_NUM];
	s8 mcs_40m_2p5[RTW89_BF_NUM];
};

#define RTW89_TXPWR_LMT_PAGE_SIZE_BE 76

struct rtw89_txpwr_limit_be {
	s8 cck_20m[RTW89_BF_NUM];
	s8 cck_40m[RTW89_BF_NUM];
	s8 ofdm[RTW89_BF_NUM];
	s8 mcs_20m[RTW89_BW20_SEC_NUM_BE][RTW89_BF_NUM];
	s8 mcs_40m[RTW89_BW40_SEC_NUM_BE][RTW89_BF_NUM];
	s8 mcs_80m[RTW89_BW80_SEC_NUM_BE][RTW89_BF_NUM];
	s8 mcs_160m[RTW89_BW160_SEC_NUM_BE][RTW89_BF_NUM];
	s8 mcs_320m[RTW89_BF_NUM];
	s8 mcs_40m_0p5[RTW89_BF_NUM];
	s8 mcs_40m_2p5[RTW89_BF_NUM];
	s8 mcs_40m_4p5[RTW89_BF_NUM];
	s8 mcs_40m_6p5[RTW89_BF_NUM];
};

#define RTW89_RU_SEC_NUM_AX 8

#define RTW89_TXPWR_LMT_RU_PAGE_SIZE_AX 24

struct rtw89_txpwr_limit_ru_ax {
	s8 ru26[RTW89_RU_SEC_NUM_AX];
	s8 ru52[RTW89_RU_SEC_NUM_AX];
	s8 ru106[RTW89_RU_SEC_NUM_AX];
};

#define RTW89_RU_SEC_NUM_BE 16

#define RTW89_TXPWR_LMT_RU_PAGE_SIZE_BE 80

struct rtw89_txpwr_limit_ru_be {
	s8 ru26[RTW89_RU_SEC_NUM_BE];
	s8 ru52[RTW89_RU_SEC_NUM_BE];
	s8 ru106[RTW89_RU_SEC_NUM_BE];
	s8 ru52_26[RTW89_RU_SEC_NUM_BE];
	s8 ru106_26[RTW89_RU_SEC_NUM_BE];
};

struct rtw89_phy_rfk_log_fmt {
	const struct rtw89_fw_element_hdr *elm[RTW89_PHY_C2H_RFK_LOG_FUNC_NUM];
};

struct rtw89_phy_gen_def {
	u32 cr_base;
	const struct rtw89_ccx_regs *ccx;
	const struct rtw89_physts_regs *physts;
	const struct rtw89_cfo_regs *cfo;
	u32 (*phy0_phy1_offset)(struct rtw89_dev *rtwdev, u32 addr);
	void (*config_bb_gain)(struct rtw89_dev *rtwdev,
			       const struct rtw89_reg2_def *reg,
			       enum rtw89_rf_path rf_path,
			       void *extra_data);
	void (*preinit_rf_nctl)(struct rtw89_dev *rtwdev);
	void (*bb_wrap_init)(struct rtw89_dev *rtwdev);
	void (*ch_info_init)(struct rtw89_dev *rtwdev);

	void (*set_txpwr_byrate)(struct rtw89_dev *rtwdev,
				 const struct rtw89_chan *chan,
				 enum rtw89_phy_idx phy_idx);
	void (*set_txpwr_offset)(struct rtw89_dev *rtwdev,
				 const struct rtw89_chan *chan,
				 enum rtw89_phy_idx phy_idx);
	void (*set_txpwr_limit)(struct rtw89_dev *rtwdev,
				const struct rtw89_chan *chan,
				enum rtw89_phy_idx phy_idx);
	void (*set_txpwr_limit_ru)(struct rtw89_dev *rtwdev,
				   const struct rtw89_chan *chan,
				   enum rtw89_phy_idx phy_idx);
};

extern const struct rtw89_phy_gen_def rtw89_phy_gen_ax;
extern const struct rtw89_phy_gen_def rtw89_phy_gen_be;

static inline void rtw89_phy_write8(struct rtw89_dev *rtwdev,
				    u32 addr, u8 data)
{
	const struct rtw89_phy_gen_def *phy = rtwdev->chip->phy_def;

	rtw89_write8(rtwdev, addr + phy->cr_base, data);
}

static inline void rtw89_phy_write16(struct rtw89_dev *rtwdev,
				     u32 addr, u16 data)
{
	const struct rtw89_phy_gen_def *phy = rtwdev->chip->phy_def;

	rtw89_write16(rtwdev, addr + phy->cr_base, data);
}

static inline void rtw89_phy_write32(struct rtw89_dev *rtwdev,
				     u32 addr, u32 data)
{
	const struct rtw89_phy_gen_def *phy = rtwdev->chip->phy_def;

	rtw89_write32(rtwdev, addr + phy->cr_base, data);
}

static inline void rtw89_phy_write32_set(struct rtw89_dev *rtwdev,
					 u32 addr, u32 bits)
{
	const struct rtw89_phy_gen_def *phy = rtwdev->chip->phy_def;

	rtw89_write32_set(rtwdev, addr + phy->cr_base, bits);
}

static inline void rtw89_phy_write32_clr(struct rtw89_dev *rtwdev,
					 u32 addr, u32 bits)
{
	const struct rtw89_phy_gen_def *phy = rtwdev->chip->phy_def;

	rtw89_write32_clr(rtwdev, addr + phy->cr_base, bits);
}

static inline void rtw89_phy_write32_mask(struct rtw89_dev *rtwdev,
					  u32 addr, u32 mask, u32 data)
{
	const struct rtw89_phy_gen_def *phy = rtwdev->chip->phy_def;

	rtw89_write32_mask(rtwdev, addr + phy->cr_base, mask, data);
}

static inline u8 rtw89_phy_read8(struct rtw89_dev *rtwdev, u32 addr)
{
	const struct rtw89_phy_gen_def *phy = rtwdev->chip->phy_def;

	return rtw89_read8(rtwdev, addr + phy->cr_base);
}

static inline u16 rtw89_phy_read16(struct rtw89_dev *rtwdev, u32 addr)
{
	const struct rtw89_phy_gen_def *phy = rtwdev->chip->phy_def;

	return rtw89_read16(rtwdev, addr + phy->cr_base);
}

static inline u32 rtw89_phy_read32(struct rtw89_dev *rtwdev, u32 addr)
{
	const struct rtw89_phy_gen_def *phy = rtwdev->chip->phy_def;

	return rtw89_read32(rtwdev, addr + phy->cr_base);
}

static inline u32 rtw89_phy_read32_mask(struct rtw89_dev *rtwdev,
					u32 addr, u32 mask)
{
	const struct rtw89_phy_gen_def *phy = rtwdev->chip->phy_def;

	return rtw89_read32_mask(rtwdev, addr + phy->cr_base, mask);
}

static inline void rtw89_bbmcu_write32(struct rtw89_dev *rtwdev,
				       u32 addr, u32 data, enum rtw89_phy_idx phy_idx)
{
	if (phy_idx && addr < 0x10000)
		addr += 0x20000;

	rtw89_write32(rtwdev, addr + RTW89_BBMCU_ADDR_OFFSET, data);
}

static inline
enum rtw89_gain_offset rtw89_subband_to_gain_offset_band_of_ofdm(enum rtw89_subband subband)
{
	switch (subband) {
	default:
	case RTW89_CH_2G:
		return RTW89_GAIN_OFFSET_2G_OFDM;
	case RTW89_CH_5G_BAND_1:
		return RTW89_GAIN_OFFSET_5G_LOW;
	case RTW89_CH_5G_BAND_3:
		return RTW89_GAIN_OFFSET_5G_MID;
	case RTW89_CH_5G_BAND_4:
		return RTW89_GAIN_OFFSET_5G_HIGH;
	case RTW89_CH_6G_BAND_IDX0:
		return RTW89_GAIN_OFFSET_6G_L0;
	case RTW89_CH_6G_BAND_IDX1:
		return RTW89_GAIN_OFFSET_6G_L1;
	case RTW89_CH_6G_BAND_IDX2:
		return RTW89_GAIN_OFFSET_6G_M0;
	case RTW89_CH_6G_BAND_IDX3:
		return RTW89_GAIN_OFFSET_6G_M1;
	case RTW89_CH_6G_BAND_IDX4:
		return RTW89_GAIN_OFFSET_6G_H0;
	case RTW89_CH_6G_BAND_IDX5:
		return RTW89_GAIN_OFFSET_6G_H1;
	case RTW89_CH_6G_BAND_IDX6:
		return RTW89_GAIN_OFFSET_6G_UH0;
	case RTW89_CH_6G_BAND_IDX7:
		return RTW89_GAIN_OFFSET_6G_UH1;
	}
}

static inline
enum rtw89_phy_bb_gain_band rtw89_subband_to_bb_gain_band(enum rtw89_subband subband)
{
	switch (subband) {
	default:
	case RTW89_CH_2G:
		return RTW89_BB_GAIN_BAND_2G;
	case RTW89_CH_5G_BAND_1:
		return RTW89_BB_GAIN_BAND_5G_L;
	case RTW89_CH_5G_BAND_3:
		return RTW89_BB_GAIN_BAND_5G_M;
	case RTW89_CH_5G_BAND_4:
		return RTW89_BB_GAIN_BAND_5G_H;
	case RTW89_CH_6G_BAND_IDX0:
	case RTW89_CH_6G_BAND_IDX1:
		return RTW89_BB_GAIN_BAND_6G_L;
	case RTW89_CH_6G_BAND_IDX2:
	case RTW89_CH_6G_BAND_IDX3:
		return RTW89_BB_GAIN_BAND_6G_M;
	case RTW89_CH_6G_BAND_IDX4:
	case RTW89_CH_6G_BAND_IDX5:
		return RTW89_BB_GAIN_BAND_6G_H;
	case RTW89_CH_6G_BAND_IDX6:
	case RTW89_CH_6G_BAND_IDX7:
		return RTW89_BB_GAIN_BAND_6G_UH;
	}
}

static inline
enum rtw89_phy_gain_band_be rtw89_subband_to_gain_band_be(enum rtw89_subband subband)
{
	switch (subband) {
	default:
	case RTW89_CH_2G:
		return RTW89_BB_GAIN_BAND_2G_BE;
	case RTW89_CH_5G_BAND_1:
		return RTW89_BB_GAIN_BAND_5G_L_BE;
	case RTW89_CH_5G_BAND_3:
		return RTW89_BB_GAIN_BAND_5G_M_BE;
	case RTW89_CH_5G_BAND_4:
		return RTW89_BB_GAIN_BAND_5G_H_BE;
	case RTW89_CH_6G_BAND_IDX0:
		return RTW89_BB_GAIN_BAND_6G_L0_BE;
	case RTW89_CH_6G_BAND_IDX1:
		return RTW89_BB_GAIN_BAND_6G_L1_BE;
	case RTW89_CH_6G_BAND_IDX2:
		return RTW89_BB_GAIN_BAND_6G_M0_BE;
	case RTW89_CH_6G_BAND_IDX3:
		return RTW89_BB_GAIN_BAND_6G_M1_BE;
	case RTW89_CH_6G_BAND_IDX4:
		return RTW89_BB_GAIN_BAND_6G_H0_BE;
	case RTW89_CH_6G_BAND_IDX5:
		return RTW89_BB_GAIN_BAND_6G_H1_BE;
	case RTW89_CH_6G_BAND_IDX6:
		return RTW89_BB_GAIN_BAND_6G_UH0_BE;
	case RTW89_CH_6G_BAND_IDX7:
		return RTW89_BB_GAIN_BAND_6G_UH1_BE;
	}
}

struct rtw89_rfk_chan_desc {
	/* desc is valid iff ch is non-zero */
	u8 ch;

	/* To avoid us from extending old chip code every time, each new
	 * field must be defined along with a bool flag in positivte way.
	 */
	bool has_band;
	u8 band;
	bool has_bw;
	u8 bw;
};

enum rtw89_rfk_flag {
	RTW89_RFK_F_WRF = 0,
	RTW89_RFK_F_WM = 1,
	RTW89_RFK_F_WS = 2,
	RTW89_RFK_F_WC = 3,
	RTW89_RFK_F_DELAY = 4,
	RTW89_RFK_F_NUM,
};

struct rtw89_rfk_tbl {
	const struct rtw89_reg5_def *defs;
	u32 size;
};

#define RTW89_DECLARE_RFK_TBL(_name)		\
const struct rtw89_rfk_tbl _name ## _tbl = {	\
	.defs = _name,				\
	.size = ARRAY_SIZE(_name),		\
}

#define RTW89_DECL_RFK_WRF(_path, _addr, _mask, _data)	\
	{.flag = RTW89_RFK_F_WRF,			\
	 .path = _path,					\
	 .addr = _addr,					\
	 .mask = _mask,					\
	 .data = _data,}

#define RTW89_DECL_RFK_WM(_addr, _mask, _data)	\
	{.flag = RTW89_RFK_F_WM,		\
	 .addr = _addr,				\
	 .mask = _mask,				\
	 .data = _data,}

#define RTW89_DECL_RFK_WS(_addr, _mask)	\
	{.flag = RTW89_RFK_F_WS,	\
	 .addr = _addr,			\
	 .mask = _mask,}

#define RTW89_DECL_RFK_WC(_addr, _mask)	\
	{.flag = RTW89_RFK_F_WC,	\
	 .addr = _addr,			\
	 .mask = _mask,}

#define RTW89_DECL_RFK_DELAY(_data)	\
	{.flag = RTW89_RFK_F_DELAY,	\
	 .data = _data,}

void
rtw89_rfk_parser(struct rtw89_dev *rtwdev, const struct rtw89_rfk_tbl *tbl);

#define rtw89_rfk_parser_by_cond(dev, cond, tbl_t, tbl_f)	\
	do {							\
		typeof(dev) __dev = (dev);			\
		if (cond)					\
			rtw89_rfk_parser(__dev, (tbl_t));	\
		else						\
			rtw89_rfk_parser(__dev, (tbl_f));	\
	} while (0)

void rtw89_phy_write_reg3_tbl(struct rtw89_dev *rtwdev,
			      const struct rtw89_phy_reg3_tbl *tbl);
u8 rtw89_phy_get_txsc(struct rtw89_dev *rtwdev,
		      const struct rtw89_chan *chan,
		      enum rtw89_bandwidth dbw);
u8 rtw89_phy_get_txsb(struct rtw89_dev *rtwdev, const struct rtw89_chan *chan,
		      enum rtw89_bandwidth dbw);
u32 rtw89_phy_read_rf(struct rtw89_dev *rtwdev, enum rtw89_rf_path rf_path,
		      u32 addr, u32 mask);
u32 rtw89_phy_read_rf_v1(struct rtw89_dev *rtwdev, enum rtw89_rf_path rf_path,
			 u32 addr, u32 mask);
u32 rtw89_phy_read_rf_v2(struct rtw89_dev *rtwdev, enum rtw89_rf_path rf_path,
			 u32 addr, u32 mask);
bool rtw89_phy_write_rf(struct rtw89_dev *rtwdev, enum rtw89_rf_path rf_path,
			u32 addr, u32 mask, u32 data);
bool rtw89_phy_write_rf_v1(struct rtw89_dev *rtwdev, enum rtw89_rf_path rf_path,
			   u32 addr, u32 mask, u32 data);
bool rtw89_phy_write_rf_v2(struct rtw89_dev *rtwdev, enum rtw89_rf_path rf_path,
			   u32 addr, u32 mask, u32 data);
void rtw89_phy_init_bb_reg(struct rtw89_dev *rtwdev);
void rtw89_phy_init_rf_reg(struct rtw89_dev *rtwdev, bool noio);
void rtw89_phy_config_rf_reg_v1(struct rtw89_dev *rtwdev,
				const struct rtw89_reg2_def *reg,
				enum rtw89_rf_path rf_path,
				void *extra_data);
void rtw89_phy_dm_init(struct rtw89_dev *rtwdev);
void rtw89_phy_dm_reinit(struct rtw89_dev *rtwdev);
void rtw89_phy_write32_idx(struct rtw89_dev *rtwdev, u32 addr, u32 mask,
			   u32 data, enum rtw89_phy_idx phy_idx);
void rtw89_phy_write32_idx_set(struct rtw89_dev *rtwdev, u32 addr, u32 bits,
			       enum rtw89_phy_idx phy_idx);
void rtw89_phy_write32_idx_clr(struct rtw89_dev *rtwdev, u32 addr, u32 bits,
			       enum rtw89_phy_idx phy_idx);
u32 rtw89_phy_read32_idx(struct rtw89_dev *rtwdev, u32 addr, u32 mask,
			 enum rtw89_phy_idx phy_idx);
s8 *rtw89_phy_raw_byr_seek(struct rtw89_dev *rtwdev,
			   struct rtw89_txpwr_byrate *head,
			   const struct rtw89_rate_desc *desc);
s8 rtw89_phy_read_txpwr_byrate(struct rtw89_dev *rtwdev, u8 band, u8 bw,
			       const struct rtw89_rate_desc *rate_desc);
void rtw89_phy_ant_gain_init(struct rtw89_dev *rtwdev);
s16 rtw89_phy_ant_gain_pwr_offset(struct rtw89_dev *rtwdev,
				  const struct rtw89_chan *chan);
int rtw89_print_ant_gain(struct rtw89_dev *rtwdev, char *buf, size_t bufsz,
			 const struct rtw89_chan *chan);
void rtw89_phy_load_txpwr_byrate(struct rtw89_dev *rtwdev,
				 const struct rtw89_txpwr_table *tbl);
s8 rtw89_phy_read_txpwr_limit(struct rtw89_dev *rtwdev, u8 band,
			      u8 bw, u8 ntx, u8 rs, u8 bf, u8 ch);
s8 rtw89_phy_read_txpwr_limit_ru(struct rtw89_dev *rtwdev, u8 band,
				 u8 ru, u8 ntx, u8 ch);

static inline void rtw89_phy_preinit_rf_nctl(struct rtw89_dev *rtwdev)
{
	const struct rtw89_phy_gen_def *phy = rtwdev->chip->phy_def;

	phy->preinit_rf_nctl(rtwdev);
}

static inline void rtw89_phy_bb_wrap_init(struct rtw89_dev *rtwdev)
{
	const struct rtw89_phy_gen_def *phy = rtwdev->chip->phy_def;

	if (phy->bb_wrap_init)
		phy->bb_wrap_init(rtwdev);
}

static inline void rtw89_phy_ch_info_init(struct rtw89_dev *rtwdev)
{
	const struct rtw89_phy_gen_def *phy = rtwdev->chip->phy_def;

	if (phy->ch_info_init)
		phy->ch_info_init(rtwdev);
}

static inline
void rtw89_phy_set_txpwr_byrate(struct rtw89_dev *rtwdev,
				const struct rtw89_chan *chan,
				enum rtw89_phy_idx phy_idx)
{
	const struct rtw89_phy_gen_def *phy = rtwdev->chip->phy_def;

	phy->set_txpwr_byrate(rtwdev, chan, phy_idx);
}

static inline
void rtw89_phy_set_txpwr_offset(struct rtw89_dev *rtwdev,
				const struct rtw89_chan *chan,
				enum rtw89_phy_idx phy_idx)
{
	const struct rtw89_phy_gen_def *phy = rtwdev->chip->phy_def;

	phy->set_txpwr_offset(rtwdev, chan, phy_idx);
}

static inline
void rtw89_phy_set_txpwr_limit(struct rtw89_dev *rtwdev,
			       const struct rtw89_chan *chan,
			       enum rtw89_phy_idx phy_idx)
{
	const struct rtw89_phy_gen_def *phy = rtwdev->chip->phy_def;

	phy->set_txpwr_limit(rtwdev, chan, phy_idx);
}

static inline
void rtw89_phy_set_txpwr_limit_ru(struct rtw89_dev *rtwdev,
				  const struct rtw89_chan *chan,
				  enum rtw89_phy_idx phy_idx)
{
	const struct rtw89_phy_gen_def *phy = rtwdev->chip->phy_def;

	phy->set_txpwr_limit_ru(rtwdev, chan, phy_idx);
}

static inline s8 rtw89_phy_txpwr_rf_to_bb(struct rtw89_dev *rtwdev, s8 txpwr_rf)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	return txpwr_rf << (chip->txpwr_factor_bb - chip->txpwr_factor_rf);
}

static inline s8 rtw89_phy_txpwr_bb_to_rf(struct rtw89_dev *rtwdev, s8 txpwr_bb)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	return txpwr_bb >> (chip->txpwr_factor_bb - chip->txpwr_factor_rf);
}

static inline s8 rtw89_phy_txpwr_rf_to_mac(struct rtw89_dev *rtwdev, s8 txpwr_rf)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	return txpwr_rf >> (chip->txpwr_factor_rf - chip->txpwr_factor_mac);
}

static inline s8 rtw89_phy_txpwr_dbm_to_mac(struct rtw89_dev *rtwdev, s8 dbm)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	return clamp_t(s16, dbm << chip->txpwr_factor_mac, -64, 63);
}

void rtw89_phy_ra_assoc(struct rtw89_dev *rtwdev, struct rtw89_sta_link *rtwsta_link);
void rtw89_phy_ra_update(struct rtw89_dev *rtwdev);
void rtw89_phy_ra_update_sta(struct rtw89_dev *rtwdev, struct ieee80211_sta *sta,
			     u32 changed);
void rtw89_phy_ra_update_sta_link(struct rtw89_dev *rtwdev,
				  struct rtw89_sta_link *rtwsta_link,
				  u32 changed);
void rtw89_phy_rate_pattern_vif(struct rtw89_dev *rtwdev,
				struct ieee80211_vif *vif,
				const struct cfg80211_bitrate_mask *mask);
bool rtw89_phy_c2h_chk_atomic(struct rtw89_dev *rtwdev, u8 class, u8 func);
void rtw89_phy_c2h_handle(struct rtw89_dev *rtwdev, struct sk_buff *skb,
			  u32 len, u8 class, u8 func);
int rtw89_phy_rfk_pre_ntfy_and_wait(struct rtw89_dev *rtwdev,
				    enum rtw89_phy_idx phy_idx,
				    unsigned int ms);
int rtw89_phy_rfk_tssi_and_wait(struct rtw89_dev *rtwdev,
				enum rtw89_phy_idx phy_idx,
				const struct rtw89_chan *chan,
				enum rtw89_tssi_mode tssi_mode,
				unsigned int ms);
int rtw89_phy_rfk_iqk_and_wait(struct rtw89_dev *rtwdev,
			       enum rtw89_phy_idx phy_idx,
			       const struct rtw89_chan *chan,
			       unsigned int ms);
int rtw89_phy_rfk_dpk_and_wait(struct rtw89_dev *rtwdev,
			       enum rtw89_phy_idx phy_idx,
			       const struct rtw89_chan *chan,
			       unsigned int ms);
int rtw89_phy_rfk_txgapk_and_wait(struct rtw89_dev *rtwdev,
				  enum rtw89_phy_idx phy_idx,
				  const struct rtw89_chan *chan,
				  unsigned int ms);
int rtw89_phy_rfk_dack_and_wait(struct rtw89_dev *rtwdev,
				enum rtw89_phy_idx phy_idx,
				const struct rtw89_chan *chan,
				unsigned int ms);
int rtw89_phy_rfk_rxdck_and_wait(struct rtw89_dev *rtwdev,
				 enum rtw89_phy_idx phy_idx,
				 const struct rtw89_chan *chan,
				 bool is_chl_k, unsigned int ms);
void rtw89_phy_rfk_tssi_fill_fwcmd_efuse_to_de(struct rtw89_dev *rtwdev,
					       enum rtw89_phy_idx phy,
					       const struct rtw89_chan *chan,
					       struct rtw89_h2c_rf_tssi *h2c);
void rtw89_phy_rfk_tssi_fill_fwcmd_tmeter_tbl(struct rtw89_dev *rtwdev,
					      enum rtw89_phy_idx phy,
					      const struct rtw89_chan *chan,
					      struct rtw89_h2c_rf_tssi *h2c);
void rtw89_phy_cfo_track(struct rtw89_dev *rtwdev);
void rtw89_phy_cfo_track_work(struct wiphy *wiphy, struct wiphy_work *work);
void rtw89_phy_cfo_parse(struct rtw89_dev *rtwdev, s16 cfo_val,
			 struct rtw89_rx_phy_ppdu *phy_ppdu);
void rtw89_phy_stat_track(struct rtw89_dev *rtwdev);
void rtw89_phy_env_monitor_track(struct rtw89_dev *rtwdev);
void rtw89_phy_set_phy_regs(struct rtw89_dev *rtwdev, u32 addr, u32 mask,
			    u32 val);
void rtw89_phy_dig_reset(struct rtw89_dev *rtwdev, struct rtw89_bb_ctx *bb);
void rtw89_phy_dig(struct rtw89_dev *rtwdev);
void rtw89_phy_tx_path_div_track(struct rtw89_dev *rtwdev);
void rtw89_phy_antdiv_parse(struct rtw89_dev *rtwdev,
			    struct rtw89_rx_phy_ppdu *phy_ppdu);
void rtw89_phy_antdiv_track(struct rtw89_dev *rtwdev);
void rtw89_phy_antdiv_work(struct wiphy *wiphy, struct wiphy_work *work);
void rtw89_phy_set_bss_color(struct rtw89_dev *rtwdev,
			     struct rtw89_vif_link *rtwvif_link);
void rtw89_phy_tssi_ctrl_set_bandedge_cfg(struct rtw89_dev *rtwdev,
					  enum rtw89_mac_idx mac_idx,
					  enum rtw89_tssi_bandedge_cfg bandedge_cfg);
void rtw89_phy_ul_tb_assoc(struct rtw89_dev *rtwdev, struct rtw89_vif_link *rtwvif_link);
void rtw89_phy_ul_tb_ctrl_track(struct rtw89_dev *rtwdev);
u8 rtw89_encode_chan_idx(struct rtw89_dev *rtwdev, u8 central_ch, u8 band);
void rtw89_decode_chan_idx(struct rtw89_dev *rtwdev, u8 chan_idx,
			   u8 *ch, enum nl80211_band *band);
void rtw89_phy_config_edcca(struct rtw89_dev *rtwdev,
			    struct rtw89_bb_ctx *bb, bool scan);
void rtw89_phy_edcca_track(struct rtw89_dev *rtwdev);
void rtw89_phy_edcca_thre_calc(struct rtw89_dev *rtwdev, struct rtw89_bb_ctx *bb);
enum rtw89_rf_path_bit rtw89_phy_get_kpath(struct rtw89_dev *rtwdev,
					   enum rtw89_phy_idx phy_idx);
enum rtw89_rf_path rtw89_phy_get_syn_sel(struct rtw89_dev *rtwdev,
					 enum rtw89_phy_idx phy_idx);
u8 rtw89_rfk_chan_lookup(struct rtw89_dev *rtwdev,
			 const struct rtw89_rfk_chan_desc *desc, u8 desc_nr,
			 const struct rtw89_chan *target_chan);

#endif
