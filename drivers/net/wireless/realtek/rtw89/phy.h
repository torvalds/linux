/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#ifndef __RTW89_PHY_H__
#define __RTW89_PHY_H__

#include "core.h"

#define RTW89_PHY_ADDR_OFFSET	0x10000
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

#define CFO_TRK_ENABLE_TH (2 << 2)
#define CFO_TRK_STOP_TH_4 (30 << 2)
#define CFO_TRK_STOP_TH_3 (20 << 2)
#define CFO_TRK_STOP_TH_2 (10 << 2)
#define CFO_TRK_STOP_TH_1 (00 << 2)
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

enum rtw89_phy_c2h_ra_func {
	RTW89_PHY_C2H_FUNC_STS_RPT,
	RTW89_PHY_C2H_FUNC_MU_GPTBL_RPT,
	RTW89_PHY_C2H_FUNC_TXSTS,
	RTW89_PHY_C2H_FUNC_RA_MAX,
};

enum rtw89_phy_c2h_class {
	RTW89_PHY_C2H_CLASS_RUA,
	RTW89_PHY_C2H_CLASS_RA,
	RTW89_PHY_C2H_CLASS_DM,
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

#define DELTA_SWINGIDX_SIZE 30

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

extern const u8 rtw89_rs_idx_max[RTW89_RS_MAX];
extern const u8 rtw89_rs_nss_max[RTW89_RS_MAX];

static inline void rtw89_phy_write8(struct rtw89_dev *rtwdev,
				    u32 addr, u8 data)
{
	rtw89_write8(rtwdev, addr | RTW89_PHY_ADDR_OFFSET, data);
}

static inline void rtw89_phy_write16(struct rtw89_dev *rtwdev,
				     u32 addr, u16 data)
{
	rtw89_write16(rtwdev, addr | RTW89_PHY_ADDR_OFFSET, data);
}

static inline void rtw89_phy_write32(struct rtw89_dev *rtwdev,
				     u32 addr, u32 data)
{
	rtw89_write32(rtwdev, addr | RTW89_PHY_ADDR_OFFSET, data);
}

static inline void rtw89_phy_write32_set(struct rtw89_dev *rtwdev,
					 u32 addr, u32 bits)
{
	rtw89_write32_set(rtwdev, addr | RTW89_PHY_ADDR_OFFSET, bits);
}

static inline void rtw89_phy_write32_clr(struct rtw89_dev *rtwdev,
					 u32 addr, u32 bits)
{
	rtw89_write32_clr(rtwdev, addr | RTW89_PHY_ADDR_OFFSET, bits);
}

static inline void rtw89_phy_write32_mask(struct rtw89_dev *rtwdev,
					  u32 addr, u32 mask, u32 data)
{
	rtw89_write32_mask(rtwdev, addr | RTW89_PHY_ADDR_OFFSET, mask, data);
}

static inline u8 rtw89_phy_read8(struct rtw89_dev *rtwdev, u32 addr)
{
	return rtw89_read8(rtwdev, addr | RTW89_PHY_ADDR_OFFSET);
}

static inline u16 rtw89_phy_read16(struct rtw89_dev *rtwdev, u32 addr)
{
	return rtw89_read16(rtwdev, addr | RTW89_PHY_ADDR_OFFSET);
}

static inline u32 rtw89_phy_read32(struct rtw89_dev *rtwdev, u32 addr)
{
	return rtw89_read32(rtwdev, addr | RTW89_PHY_ADDR_OFFSET);
}

static inline u32 rtw89_phy_read32_mask(struct rtw89_dev *rtwdev,
					u32 addr, u32 mask)
{
	return rtw89_read32_mask(rtwdev, addr | RTW89_PHY_ADDR_OFFSET, mask);
}

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
u32 rtw89_phy_read_rf(struct rtw89_dev *rtwdev, enum rtw89_rf_path rf_path,
		      u32 addr, u32 mask);
u32 rtw89_phy_read_rf_v1(struct rtw89_dev *rtwdev, enum rtw89_rf_path rf_path,
			 u32 addr, u32 mask);
bool rtw89_phy_write_rf(struct rtw89_dev *rtwdev, enum rtw89_rf_path rf_path,
			u32 addr, u32 mask, u32 data);
bool rtw89_phy_write_rf_v1(struct rtw89_dev *rtwdev, enum rtw89_rf_path rf_path,
			   u32 addr, u32 mask, u32 data);
void rtw89_phy_init_bb_reg(struct rtw89_dev *rtwdev);
void rtw89_phy_init_rf_reg(struct rtw89_dev *rtwdev);
void rtw89_phy_config_rf_reg_v1(struct rtw89_dev *rtwdev,
				const struct rtw89_reg2_def *reg,
				enum rtw89_rf_path rf_path,
				void *extra_data);
void rtw89_phy_dm_init(struct rtw89_dev *rtwdev);
void rtw89_phy_write32_idx(struct rtw89_dev *rtwdev, u32 addr, u32 mask,
			   u32 data, enum rtw89_phy_idx phy_idx);
void rtw89_phy_load_txpwr_byrate(struct rtw89_dev *rtwdev,
				 const struct rtw89_txpwr_table *tbl);
s8 rtw89_phy_read_txpwr_byrate(struct rtw89_dev *rtwdev, u8 band,
			       const struct rtw89_rate_desc *rate_desc);
void rtw89_phy_fill_txpwr_limit(struct rtw89_dev *rtwdev,
				const struct rtw89_chan *chan,
				struct rtw89_txpwr_limit *lmt,
				u8 ntx);
void rtw89_phy_fill_txpwr_limit_ru(struct rtw89_dev *rtwdev,
				   const struct rtw89_chan *chan,
				   struct rtw89_txpwr_limit_ru *lmt_ru,
				   u8 ntx);
s8 rtw89_phy_read_txpwr_limit(struct rtw89_dev *rtwdev, u8 band,
			      u8 bw, u8 ntx, u8 rs, u8 bf, u8 ch);
void rtw89_phy_ra_assoc(struct rtw89_dev *rtwdev, struct ieee80211_sta *sta);
void rtw89_phy_ra_update(struct rtw89_dev *rtwdev);
void rtw89_phy_ra_updata_sta(struct rtw89_dev *rtwdev, struct ieee80211_sta *sta,
			     u32 changed);
void rtw89_phy_rate_pattern_vif(struct rtw89_dev *rtwdev,
				struct ieee80211_vif *vif,
				const struct cfg80211_bitrate_mask *mask);
void rtw89_phy_c2h_handle(struct rtw89_dev *rtwdev, struct sk_buff *skb,
			  u32 len, u8 class, u8 func);
void rtw89_phy_cfo_track(struct rtw89_dev *rtwdev);
void rtw89_phy_cfo_track_work(struct work_struct *work);
void rtw89_phy_cfo_parse(struct rtw89_dev *rtwdev, s16 cfo_val,
			 struct rtw89_rx_phy_ppdu *phy_ppdu);
void rtw89_phy_stat_track(struct rtw89_dev *rtwdev);
void rtw89_phy_env_monitor_track(struct rtw89_dev *rtwdev);
void rtw89_phy_set_phy_regs(struct rtw89_dev *rtwdev, u32 addr, u32 mask,
			    u32 val);
void rtw89_phy_dig_reset(struct rtw89_dev *rtwdev);
void rtw89_phy_dig(struct rtw89_dev *rtwdev);
void rtw89_phy_set_bss_color(struct rtw89_dev *rtwdev, struct ieee80211_vif *vif);
void rtw89_phy_tssi_ctrl_set_bandedge_cfg(struct rtw89_dev *rtwdev,
					  enum rtw89_mac_idx mac_idx,
					  enum rtw89_tssi_bandedge_cfg bandedge_cfg);

#endif
