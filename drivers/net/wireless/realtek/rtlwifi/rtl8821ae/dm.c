// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2009-2010  Realtek Corporation.*/

#include "../wifi.h"
#include "../base.h"
#include "../pci.h"
#include "../core.h"
#include "reg.h"
#include "def.h"
#include "phy.h"
#include "dm.h"
#include "fw.h"
#include "trx.h"
#include "../btcoexist/rtl_btc.h"

static const u32 txscaling_tbl[TXSCALE_TABLE_SIZE] = {
	0x081, /* 0, -12.0dB */
	0x088, /* 1, -11.5dB */
	0x090, /* 2, -11.0dB */
	0x099, /* 3, -10.5dB */
	0x0A2, /* 4, -10.0dB */
	0x0AC, /* 5, -9.5dB */
	0x0B6, /* 6, -9.0dB */
	0x0C0, /* 7, -8.5dB */
	0x0CC, /* 8, -8.0dB */
	0x0D8, /* 9, -7.5dB */
	0x0E5, /* 10, -7.0dB */
	0x0F2, /* 11, -6.5dB */
	0x101, /* 12, -6.0dB */
	0x110, /* 13, -5.5dB */
	0x120, /* 14, -5.0dB */
	0x131, /* 15, -4.5dB */
	0x143, /* 16, -4.0dB */
	0x156, /* 17, -3.5dB */
	0x16A, /* 18, -3.0dB */
	0x180, /* 19, -2.5dB */
	0x197, /* 20, -2.0dB */
	0x1AF, /* 21, -1.5dB */
	0x1C8, /* 22, -1.0dB */
	0x1E3, /* 23, -0.5dB */
	0x200, /* 24, +0  dB */
	0x21E, /* 25, +0.5dB */
	0x23E, /* 26, +1.0dB */
	0x261, /* 27, +1.5dB */
	0x285, /* 28, +2.0dB */
	0x2AB, /* 29, +2.5dB */
	0x2D3, /* 30, +3.0dB */
	0x2FE, /* 31, +3.5dB */
	0x32B, /* 32, +4.0dB */
	0x35C, /* 33, +4.5dB */
	0x38E, /* 34, +5.0dB */
	0x3C4, /* 35, +5.5dB */
	0x3FE  /* 36, +6.0dB */
};

static const u32 rtl8821ae_txscaling_table[TXSCALE_TABLE_SIZE] = {
	0x081, /* 0, -12.0dB */
	0x088, /* 1, -11.5dB */
	0x090, /* 2, -11.0dB */
	0x099, /* 3, -10.5dB */
	0x0A2, /* 4, -10.0dB */
	0x0AC, /* 5, -9.5dB */
	0x0B6, /* 6, -9.0dB */
	0x0C0, /* 7, -8.5dB */
	0x0CC, /* 8, -8.0dB */
	0x0D8, /* 9, -7.5dB */
	0x0E5, /* 10, -7.0dB */
	0x0F2, /* 11, -6.5dB */
	0x101, /* 12, -6.0dB */
	0x110, /* 13, -5.5dB */
	0x120, /* 14, -5.0dB */
	0x131, /* 15, -4.5dB */
	0x143, /* 16, -4.0dB */
	0x156, /* 17, -3.5dB */
	0x16A, /* 18, -3.0dB */
	0x180, /* 19, -2.5dB */
	0x197, /* 20, -2.0dB */
	0x1AF, /* 21, -1.5dB */
	0x1C8, /* 22, -1.0dB */
	0x1E3, /* 23, -0.5dB */
	0x200, /* 24, +0  dB */
	0x21E, /* 25, +0.5dB */
	0x23E, /* 26, +1.0dB */
	0x261, /* 27, +1.5dB */
	0x285, /* 28, +2.0dB */
	0x2AB, /* 29, +2.5dB */
	0x2D3, /* 30, +3.0dB */
	0x2FE, /* 31, +3.5dB */
	0x32B, /* 32, +4.0dB */
	0x35C, /* 33, +4.5dB */
	0x38E, /* 34, +5.0dB */
	0x3C4, /* 35, +5.5dB */
	0x3FE  /* 36, +6.0dB */
};

static const u32 edca_setting_dl[PEER_MAX] = {
	0xa44f,		/* 0 UNKNOWN */
	0x5ea44f,	/* 1 REALTEK_90 */
	0x5e4322,	/* 2 REALTEK_92SE */
	0x5ea42b,		/* 3 BROAD	*/
	0xa44f,		/* 4 RAL */
	0xa630,		/* 5 ATH */
	0x5ea630,		/* 6 CISCO */
	0x5ea42b,		/* 7 MARVELL */
};

static const u32 edca_setting_ul[PEER_MAX] = {
	0x5e4322,	/* 0 UNKNOWN */
	0xa44f,		/* 1 REALTEK_90 */
	0x5ea44f,	/* 2 REALTEK_92SE */
	0x5ea32b,	/* 3 BROAD */
	0x5ea422,	/* 4 RAL */
	0x5ea322,	/* 5 ATH */
	0x3ea430,	/* 6 CISCO */
	0x5ea44f,	/* 7 MARV */
};

static const u8 rtl8818e_delta_swing_table_idx_24gb_p[] = {
	0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 4, 4, 4,
	4, 4, 4, 5, 5, 7, 7, 8, 8, 8, 9, 9, 9, 9, 9};

static const u8 rtl8818e_delta_swing_table_idx_24gb_n[] = {
	0, 0, 0, 2, 2, 3, 3, 4, 4, 4, 4, 5, 5, 6, 6,
	7, 7, 7, 7, 8, 8, 9, 9, 10, 10, 10, 11, 11, 11, 11};

static const u8 rtl8812ae_delta_swing_table_idx_24gb_n[]  = {
	0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 5, 6,
	6, 6, 7, 8, 9, 9, 9, 9, 10, 10, 10, 10, 11, 11};

static const u8 rtl8812ae_delta_swing_table_idx_24gb_p[] = {
	0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6,
	6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 9, 9, 9};

static const u8 rtl8812ae_delta_swing_table_idx_24ga_n[] = {
	0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 5, 6,
	6, 6, 7, 8, 8, 9, 9, 9, 10, 10, 10, 10, 11, 11};

static const u8 rtl8812ae_delta_swing_table_idx_24ga_p[] = {
	0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6,
	6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 9, 9, 9};

static const u8 rtl8812ae_delta_swing_table_idx_24gcckb_n[] = {
	0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 5, 6,
	6, 6, 7, 8, 9, 9, 9, 9, 10, 10, 10, 10, 11, 11};

static const u8 rtl8812ae_delta_swing_table_idx_24gcckb_p[] = {
	0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6,
	6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 9, 9, 9};

static const u8 rtl8812ae_delta_swing_table_idx_24gccka_n[] = {
	0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 5, 6,
	6, 6, 7, 8, 8, 9, 9, 9, 10, 10, 10, 10, 11, 11};

static const u8 rtl8812ae_delta_swing_table_idx_24gccka_p[] = {
	0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6,
	6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 9, 9, 9};

static const u8 rtl8812ae_delta_swing_table_idx_5gb_n[][DEL_SW_IDX_SZ] = {
	{0, 1, 1, 2, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 6, 7, 7,
	7, 8, 8, 9, 9, 9, 10, 10, 11, 11, 12, 12, 13},
	{0, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7,
	7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 12, 13, 13},
	{0, 1, 1, 2, 3, 3, 4, 4, 5, 6, 6, 7, 8, 9, 10, 11,
	12, 12, 13, 14, 14, 14, 15, 16, 17, 17, 17, 18, 18, 18},
};

static const u8 rtl8812ae_delta_swing_table_idx_5gb_p[][DEL_SW_IDX_SZ] = {
	{0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 6, 7, 7, 8,
	8, 9, 9, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11},
	{0, 1, 1, 2, 2, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 7, 8,
	8, 9, 9, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11},
	{0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 7, 7, 8, 8, 9,
	9, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11},
};

static const u8 rtl8812ae_delta_swing_table_idx_5ga_n[][DEL_SW_IDX_SZ] = {
	{0, 1, 1, 2, 2, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 7, 8,
	8, 9, 9, 10, 10, 11, 11, 12, 12, 12, 13, 13, 13},
	{0, 1, 1, 2, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 8, 9,
	9, 10, 10, 11, 11, 11, 12, 12, 12, 12, 12, 13, 13},
	{0, 1, 1, 2, 2, 3, 3, 4, 5, 6, 7, 8, 8, 9, 10, 11,
	12, 13, 14, 14, 15, 15, 15, 16, 16, 16, 17, 17, 18, 18},
};

static const u8 rtl8812ae_delta_swing_table_idx_5ga_p[][DEL_SW_IDX_SZ] = {
	{0, 1, 1, 2, 2, 3, 3, 4, 4, 4, 4, 5, 5, 6, 7, 7, 8,
	8, 9, 9, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11},
	{0, 1, 1, 2, 2, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 7, 8,
	9, 9, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11},
	{0, 1, 1, 2, 3, 3, 4, 4, 5, 6, 6, 7, 7, 8, 9, 9,
	10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11},
};

static const u8 rtl8821ae_delta_swing_table_idx_24ga_n[]  = {
	0, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 5, 6, 6,
	6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 10, 10, 10};

static const u8 rtl8821ae_delta_swing_table_idx_24ga_p[] = {
	0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8,
	8, 9, 9, 10, 10, 11, 11, 12, 12, 12, 12, 12, 12};

static const u8 rtl8821ae_delta_swing_table_idx_24gccka_n[] = {
	0, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 5, 6, 6,
	6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 10, 10, 10};

static const u8 rtl8821ae_delta_swing_table_idx_24gccka_p[] = {
	0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8,
	8, 9, 9, 10, 10, 11, 11, 12, 12, 12, 12, 12, 12};

static const u8 rtl8821ae_delta_swing_table_idx_5ga_n[][DEL_SW_IDX_SZ] = {
	{0, 0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 9, 9, 10, 11,
	12, 12, 13, 14, 15, 15, 16, 16, 16, 16, 16, 16, 16, 16},
	{0, 0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 9, 9, 10, 11,
	12, 12, 13, 14, 15, 15, 16, 16, 16, 16, 16, 16, 16, 16},
	{0, 0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 9, 9, 10, 11,
	12, 12, 13, 14, 15, 15, 16, 16, 16, 16, 16, 16, 16, 16},
};

static const u8 rtl8821ae_delta_swing_table_idx_5ga_p[][DEL_SW_IDX_SZ] = {
	{0, 0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 9, 9, 10, 11,
	12, 12, 13, 14, 15, 15, 16, 16, 16, 16, 16, 16, 16, 16},
	{0, 0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 9, 9, 10, 11,
	12, 12, 13, 14, 15, 15, 16, 16, 16, 16, 16, 16, 16, 16},
	{0, 0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 9, 9, 10, 11,
	12, 12, 13, 14, 15, 15, 16, 16, 16, 16, 16, 16, 16, 16},
};

void rtl8821ae_dm_txpower_track_adjust(struct ieee80211_hw *hw,
				       u8 type, u8 *pdirection,
				       u32 *poutwrite_val)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_dm *rtldm = rtl_dm(rtl_priv(hw));
	u8 pwr_val = 0;

	if (type == 0) {
		if (rtlpriv->dm.swing_idx_ofdm[RF90_PATH_A] <=
			rtlpriv->dm.swing_idx_ofdm_base[RF90_PATH_A]) {
			*pdirection = 1;
			pwr_val = rtldm->swing_idx_ofdm_base[RF90_PATH_A] -
					rtldm->swing_idx_ofdm[RF90_PATH_A];
		} else {
			*pdirection = 2;
			pwr_val = rtldm->swing_idx_ofdm[RF90_PATH_A] -
				rtldm->swing_idx_ofdm_base[RF90_PATH_A];
		}
	} else if (type == 1) {
		if (rtldm->swing_idx_cck <= rtldm->swing_idx_cck_base) {
			*pdirection = 1;
			pwr_val = rtldm->swing_idx_cck_base -
					rtldm->swing_idx_cck;
		} else {
			*pdirection = 2;
			pwr_val = rtldm->swing_idx_cck -
				rtldm->swing_idx_cck_base;
		}
	}

	if (pwr_val >= TXPWRTRACK_MAX_IDX && (*pdirection == 1))
		pwr_val = TXPWRTRACK_MAX_IDX;

	*poutwrite_val = pwr_val | (pwr_val << 8)|
				(pwr_val << 16)|
				(pwr_val << 24);
}

void rtl8821ae_dm_clear_txpower_tracking_state(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_dm *rtldm = rtl_dm(rtlpriv);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtlpriv);
	u8 p = 0;

	rtldm->swing_idx_cck_base = rtldm->default_cck_index;
	rtldm->swing_idx_cck = rtldm->default_cck_index;
	rtldm->cck_index = 0;

	for (p = RF90_PATH_A; p <= RF90_PATH_B; ++p) {
		rtldm->swing_idx_ofdm_base[p] = rtldm->default_ofdm_index;
		rtldm->swing_idx_ofdm[p] = rtldm->default_ofdm_index;
		rtldm->ofdm_index[p] = rtldm->default_ofdm_index;

		rtldm->power_index_offset[p] = 0;
		rtldm->delta_power_index[p] = 0;
		rtldm->delta_power_index_last[p] = 0;
		/*Initial Mix mode power tracking*/
		rtldm->absolute_ofdm_swing_idx[p] = 0;
		rtldm->remnant_ofdm_swing_idx[p] = 0;
	}
	/*Initial at Modify Tx Scaling Mode*/
	rtldm->modify_txagc_flag_path_a = false;
	/*Initial at Modify Tx Scaling Mode*/
	rtldm->modify_txagc_flag_path_b = false;
	rtldm->remnant_cck_idx = 0;
	rtldm->thermalvalue = rtlefuse->eeprom_thermalmeter;
	rtldm->thermalvalue_iqk = rtlefuse->eeprom_thermalmeter;
	rtldm->thermalvalue_lck = rtlefuse->eeprom_thermalmeter;
}

static u8  rtl8821ae_dm_get_swing_index(struct ieee80211_hw *hw)
{
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 i = 0;
	u32  bb_swing;

	bb_swing = phy_get_tx_swing_8812A(hw, rtlhal->current_bandtype,
					  RF90_PATH_A);

	for (i = 0; i < TXSCALE_TABLE_SIZE; ++i)
		if (bb_swing == rtl8821ae_txscaling_table[i])
			break;

	return i;
}

void rtl8821ae_dm_initialize_txpower_tracking_thermalmeter(
				struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_dm *rtldm = rtl_dm(rtlpriv);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtlpriv);
	u8 default_swing_index  = 0;
	u8 p = 0;

	rtlpriv->dm.txpower_track_control = true;
	rtldm->thermalvalue = rtlefuse->eeprom_thermalmeter;
	rtldm->thermalvalue_iqk = rtlefuse->eeprom_thermalmeter;
	rtldm->thermalvalue_lck = rtlefuse->eeprom_thermalmeter;
	default_swing_index = rtl8821ae_dm_get_swing_index(hw);

	rtldm->default_ofdm_index =
		(default_swing_index == TXSCALE_TABLE_SIZE) ?
		24 : default_swing_index;
	rtldm->default_cck_index = 24;

	rtldm->swing_idx_cck_base = rtldm->default_cck_index;
	rtldm->cck_index = rtldm->default_cck_index;

	for (p = RF90_PATH_A; p < MAX_RF_PATH; ++p) {
		rtldm->swing_idx_ofdm_base[p] =
			rtldm->default_ofdm_index;
		rtldm->ofdm_index[p] = rtldm->default_ofdm_index;
		rtldm->delta_power_index[p] = 0;
		rtldm->power_index_offset[p] = 0;
		rtldm->delta_power_index_last[p] = 0;
	}
}

void rtl8821ae_dm_init_edca_turbo(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpriv->dm.current_turbo_edca = false;
	rtlpriv->dm.is_any_nonbepkts = false;
	rtlpriv->dm.is_cur_rdlstate = false;
}

void rtl8821ae_dm_init_rate_adaptive_mask(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rate_adaptive *p_ra = &rtlpriv->ra;

	p_ra->ratr_state = DM_RATR_STA_INIT;
	p_ra->pre_ratr_state = DM_RATR_STA_INIT;

	rtlpriv->dm.dm_type = DM_TYPE_BYDRIVER;
	if (rtlpriv->dm.dm_type == DM_TYPE_BYDRIVER)
		rtlpriv->dm.useramask = true;
	else
		rtlpriv->dm.useramask = false;

	p_ra->high_rssi_thresh_for_ra = 50;
	p_ra->low_rssi_thresh_for_ra40m = 20;
}

static void rtl8821ae_dm_init_dynamic_atc_switch(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpriv->dm.crystal_cap = rtlpriv->efuse.crystalcap;

	rtlpriv->dm.atc_status = rtl_get_bbreg(hw, ROFDM1_CFOTRACKING, BIT(11));
	rtlpriv->dm.cfo_threshold = CFO_THRESHOLD_XTAL;
}

static void rtl8821ae_dm_common_info_self_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 tmp;

	rtlphy->cck_high_power =
		(bool)rtl_get_bbreg(hw, ODM_REG_CCK_RPT_FORMAT_11AC,
				    ODM_BIT_CCK_RPT_FORMAT_11AC);

	tmp = (u8)rtl_get_bbreg(hw, ODM_REG_BB_RX_PATH_11AC,
				ODM_BIT_BB_RX_PATH_11AC);
	if (tmp & BIT(0))
		rtlpriv->dm.rfpath_rxenable[0] = true;
	if (tmp & BIT(1))
		rtlpriv->dm.rfpath_rxenable[1] = true;
}

void rtl8821ae_dm_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u32 cur_igvalue = rtl_get_bbreg(hw, ROFDM0_XAAGCCORE1, 0x7f);

	spin_lock(&rtlpriv->locks.iqk_lock);
	rtlphy->lck_inprogress = false;
	spin_unlock(&rtlpriv->locks.iqk_lock);

	rtlpriv->dm.dm_type = DM_TYPE_BYDRIVER;
	rtl8821ae_dm_common_info_self_init(hw);
	rtl_dm_diginit(hw, cur_igvalue);
	rtl8821ae_dm_init_rate_adaptive_mask(hw);
	rtl8821ae_dm_init_edca_turbo(hw);
	rtl8821ae_dm_initialize_txpower_tracking_thermalmeter(hw);
	rtl8821ae_dm_init_dynamic_atc_switch(hw);
}

static void rtl8821ae_dm_find_minimum_rssi(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct dig_t *rtl_dm_dig = &rtlpriv->dm_digtable;
	struct rtl_mac *mac = rtl_mac(rtlpriv);

	/* Determine the minimum RSSI  */
	if ((mac->link_state < MAC80211_LINKED) &&
	    (rtlpriv->dm.entry_min_undec_sm_pwdb == 0)) {
		rtl_dm_dig->min_undec_pwdb_for_dm = 0;
		pr_debug("rtl8821ae: Not connected to any AP\n");
	}
	if (mac->link_state >= MAC80211_LINKED) {
		if (mac->opmode == NL80211_IFTYPE_AP ||
		    mac->opmode == NL80211_IFTYPE_ADHOC) {
			rtl_dm_dig->min_undec_pwdb_for_dm =
			    rtlpriv->dm.entry_min_undec_sm_pwdb;
			rtl_dbg(rtlpriv, COMP_BB_POWERSAVING, DBG_LOUD,
				"AP Client PWDB = 0x%lx\n",
				rtlpriv->dm.entry_min_undec_sm_pwdb);
		} else {
			rtl_dm_dig->min_undec_pwdb_for_dm =
			    rtlpriv->dm.undec_sm_pwdb;
			rtl_dbg(rtlpriv, COMP_BB_POWERSAVING, DBG_LOUD,
				"STA Default Port PWDB = 0x%x\n",
				rtl_dm_dig->min_undec_pwdb_for_dm);
		}
	} else {
		rtl_dm_dig->min_undec_pwdb_for_dm =
		    rtlpriv->dm.entry_min_undec_sm_pwdb;
		rtl_dbg(rtlpriv, COMP_BB_POWERSAVING, DBG_LOUD,
			"AP Ext Port or disconnect PWDB = 0x%x\n",
			rtl_dm_dig->min_undec_pwdb_for_dm);
	}
	rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD,
		"MinUndecoratedPWDBForDM =%d\n",
		rtl_dm_dig->min_undec_pwdb_for_dm);
}

static void  rtl8812ae_dm_rssi_dump_to_register(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_write_byte(rtlpriv, RA_RSSI_DUMP,
		       rtlpriv->stats.rx_rssi_percentage[0]);
	rtl_write_byte(rtlpriv, RB_RSSI_DUMP,
		       rtlpriv->stats.rx_rssi_percentage[1]);

	/* Rx EVM*/
	rtl_write_byte(rtlpriv, RS1_RX_EVM_DUMP,
		       rtlpriv->stats.rx_evm_dbm[0]);
	rtl_write_byte(rtlpriv, RS2_RX_EVM_DUMP,
		       rtlpriv->stats.rx_evm_dbm[1]);

	/*Rx SNR*/
	rtl_write_byte(rtlpriv, RA_RX_SNR_DUMP,
		       (u8)(rtlpriv->stats.rx_snr_db[0]));
	rtl_write_byte(rtlpriv, RB_RX_SNR_DUMP,
		       (u8)(rtlpriv->stats.rx_snr_db[1]));

	/*Rx Cfo_Short*/
	rtl_write_word(rtlpriv, RA_CFO_SHORT_DUMP,
		       rtlpriv->stats.rx_cfo_short[0]);
	rtl_write_word(rtlpriv, RB_CFO_SHORT_DUMP,
		       rtlpriv->stats.rx_cfo_short[1]);

	/*Rx Cfo_Tail*/
	rtl_write_word(rtlpriv, RA_CFO_LONG_DUMP,
		       rtlpriv->stats.rx_cfo_tail[0]);
	rtl_write_word(rtlpriv, RB_CFO_LONG_DUMP,
		       rtlpriv->stats.rx_cfo_tail[1]);
}

static void rtl8821ae_dm_check_rssi_monitor(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct dig_t *dm_digtable = &rtlpriv->dm_digtable;
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_sta_info *drv_priv;
	u8 h2c_parameter[4] = { 0 };
	long tmp_entry_max_pwdb = 0, tmp_entry_min_pwdb = 0xff;
	u8 stbc_tx = 0;
	u64 cur_rxokcnt = 0;
	static u64 last_txokcnt = 0, last_rxokcnt;

	cur_rxokcnt = rtlpriv->stats.rxbytesunicast - last_rxokcnt;
	last_txokcnt = rtlpriv->stats.txbytesunicast;
	last_rxokcnt = rtlpriv->stats.rxbytesunicast;
	if (cur_rxokcnt > (last_txokcnt * 6))
		h2c_parameter[3] = 0x01;
	else
		h2c_parameter[3] = 0x00;

	/* AP & ADHOC & MESH */
	if (mac->opmode == NL80211_IFTYPE_AP ||
	    mac->opmode == NL80211_IFTYPE_ADHOC ||
	    mac->opmode == NL80211_IFTYPE_MESH_POINT) {
		spin_lock_bh(&rtlpriv->locks.entry_list_lock);
		list_for_each_entry(drv_priv, &rtlpriv->entry_list, list) {
			if (drv_priv->rssi_stat.undec_sm_pwdb <
					tmp_entry_min_pwdb)
				tmp_entry_min_pwdb =
					drv_priv->rssi_stat.undec_sm_pwdb;
			if (drv_priv->rssi_stat.undec_sm_pwdb >
					tmp_entry_max_pwdb)
				tmp_entry_max_pwdb =
					drv_priv->rssi_stat.undec_sm_pwdb;
		}
		spin_unlock_bh(&rtlpriv->locks.entry_list_lock);

		/* If associated entry is found */
		if (tmp_entry_max_pwdb != 0) {
			rtlpriv->dm.entry_max_undec_sm_pwdb =
				tmp_entry_max_pwdb;
			RTPRINT(rtlpriv, FDM, DM_PWDB,
				"EntryMaxPWDB = 0x%lx(%ld)\n",
				tmp_entry_max_pwdb, tmp_entry_max_pwdb);
		} else {
			rtlpriv->dm.entry_max_undec_sm_pwdb = 0;
		}
		/* If associated entry is found */
		if (tmp_entry_min_pwdb != 0xff) {
			rtlpriv->dm.entry_min_undec_sm_pwdb =
				tmp_entry_min_pwdb;
			RTPRINT(rtlpriv, FDM, DM_PWDB,
				"EntryMinPWDB = 0x%lx(%ld)\n",
				tmp_entry_min_pwdb, tmp_entry_min_pwdb);
		} else {
			rtlpriv->dm.entry_min_undec_sm_pwdb = 0;
		}
	}
	/* Indicate Rx signal strength to FW. */
	if (rtlpriv->dm.useramask) {
		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE) {
			if (mac->mode == WIRELESS_MODE_AC_24G ||
			    mac->mode == WIRELESS_MODE_AC_5G ||
			    mac->mode == WIRELESS_MODE_AC_ONLY)
				stbc_tx = (mac->vht_cur_stbc &
					   STBC_VHT_ENABLE_TX) ? 1 : 0;
			else
				stbc_tx = (mac->ht_cur_stbc &
					   STBC_HT_ENABLE_TX) ? 1 : 0;
			h2c_parameter[3] |= stbc_tx << 1;
		}
		h2c_parameter[2] =
			(u8)(rtlpriv->dm.undec_sm_pwdb & 0xFF);
		h2c_parameter[1] = 0x20;
		h2c_parameter[0] = 0;
		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE)
			rtl8821ae_fill_h2c_cmd(hw, H2C_RSSI_21AE_REPORT, 4,
					       h2c_parameter);
		else
			rtl8821ae_fill_h2c_cmd(hw, H2C_RSSI_21AE_REPORT, 3,
					       h2c_parameter);
	} else {
		rtl_write_byte(rtlpriv, 0x4fe, rtlpriv->dm.undec_sm_pwdb);
	}
	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE)
		rtl8812ae_dm_rssi_dump_to_register(hw);
	rtl8821ae_dm_find_minimum_rssi(hw);
	dm_digtable->rssi_val_min = rtlpriv->dm_digtable.min_undec_pwdb_for_dm;
}

void rtl8821ae_dm_write_cck_cca_thres(struct ieee80211_hw *hw, u8 current_cca)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct dig_t *dm_digtable = &rtlpriv->dm_digtable;

	if (dm_digtable->cur_cck_cca_thres != current_cca)
		rtl_write_byte(rtlpriv, DM_REG_CCK_CCA_11AC, current_cca);

	dm_digtable->pre_cck_cca_thres = dm_digtable->cur_cck_cca_thres;
	dm_digtable->cur_cck_cca_thres = current_cca;
}

void rtl8821ae_dm_write_dig(struct ieee80211_hw *hw, u8 current_igi)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct dig_t *dm_digtable = &rtlpriv->dm_digtable;

	if (dm_digtable->stop_dig)
		return;

	if (dm_digtable->cur_igvalue != current_igi) {
		rtl_set_bbreg(hw, DM_REG_IGI_A_11AC,
			      DM_BIT_IGI_11AC, current_igi);
		if (rtlpriv->phy.rf_type != RF_1T1R)
			rtl_set_bbreg(hw, DM_REG_IGI_B_11AC,
				      DM_BIT_IGI_11AC, current_igi);
	}
	dm_digtable->cur_igvalue = current_igi;
}

static void rtl8821ae_dm_dig(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct dig_t *dm_digtable = &rtlpriv->dm_digtable;
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 dig_min_0;
	u8 dig_max_of_min;
	bool first_connect, first_disconnect;
	u8 dm_dig_max, dm_dig_min, offset;
	u8 current_igi = dm_digtable->cur_igvalue;

	rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD, "\n");

	if (mac->act_scanning) {
		rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD,
			"Return: In Scan Progress\n");
		return;
	}

	/*add by Neil Chen to avoid PSD is processing*/
	dig_min_0 = dm_digtable->dig_min_0;
	first_connect = (mac->link_state >= MAC80211_LINKED) &&
			(!dm_digtable->media_connect_0);
	first_disconnect = (mac->link_state < MAC80211_LINKED) &&
			(dm_digtable->media_connect_0);

	/*1 Boundary Decision*/

	dm_dig_max = 0x5A;

	if (rtlhal->hw_type != HARDWARE_TYPE_RTL8821AE)
		dm_dig_min = DM_DIG_MIN;
	else
		dm_dig_min = 0x1C;

	dig_max_of_min = DM_DIG_MAX_AP;

	if (mac->link_state >= MAC80211_LINKED) {
		if (rtlhal->hw_type != HARDWARE_TYPE_RTL8821AE)
			offset = 20;
		else
			offset = 10;

		if ((dm_digtable->rssi_val_min + offset) > dm_dig_max)
			dm_digtable->rx_gain_max = dm_dig_max;
		else if ((dm_digtable->rssi_val_min + offset) < dm_dig_min)
			dm_digtable->rx_gain_max = dm_dig_min;
		else
			dm_digtable->rx_gain_max =
				dm_digtable->rssi_val_min + offset;

		rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD,
			"dm_digtable->rssi_val_min=0x%x,dm_digtable->rx_gain_max = 0x%x\n",
			dm_digtable->rssi_val_min,
			dm_digtable->rx_gain_max);
		if (rtlpriv->dm.one_entry_only) {
			offset = 0;

			if (dm_digtable->rssi_val_min - offset < dm_dig_min)
				dig_min_0 = dm_dig_min;
			else if (dm_digtable->rssi_val_min -
				offset > dig_max_of_min)
				dig_min_0 = dig_max_of_min;
			else
				dig_min_0 =
					dm_digtable->rssi_val_min - offset;

			rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD,
				"bOneEntryOnly=TRUE, dig_min_0=0x%x\n",
				dig_min_0);
		} else {
			dig_min_0 = dm_dig_min;
		}
	} else {
		dm_digtable->rx_gain_max = dm_dig_max;
		dig_min_0 = dm_dig_min;
		rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD, "No Link\n");
	}

	if (rtlpriv->falsealm_cnt.cnt_all > 10000) {
		rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD,
			"Abnormally false alarm case.\n");

		if (dm_digtable->large_fa_hit != 3)
			dm_digtable->large_fa_hit++;
		if (dm_digtable->forbidden_igi < current_igi) {
			dm_digtable->forbidden_igi = current_igi;
			dm_digtable->large_fa_hit = 1;
		}

		if (dm_digtable->large_fa_hit >= 3) {
			if ((dm_digtable->forbidden_igi + 1) >
				dm_digtable->rx_gain_max)
				dm_digtable->rx_gain_min =
					dm_digtable->rx_gain_max;
			else
				dm_digtable->rx_gain_min =
					(dm_digtable->forbidden_igi + 1);
			dm_digtable->recover_cnt = 3600;
		}
	} else {
		/*Recovery mechanism for IGI lower bound*/
		if (dm_digtable->recover_cnt != 0) {
			dm_digtable->recover_cnt--;
		} else {
			if (dm_digtable->large_fa_hit < 3) {
				if ((dm_digtable->forbidden_igi - 1) <
				    dig_min_0) {
					dm_digtable->forbidden_igi =
						dig_min_0;
					dm_digtable->rx_gain_min =
						dig_min_0;
					rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD,
						"Normal Case: At Lower Bound\n");
				} else {
					dm_digtable->forbidden_igi--;
					dm_digtable->rx_gain_min =
					  (dm_digtable->forbidden_igi + 1);
					rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD,
						"Normal Case: Approach Lower Bound\n");
				}
			} else {
				dm_digtable->large_fa_hit = 0;
			}
		}
	}
	rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD,
		"pDM_DigTable->LargeFAHit=%d\n",
		dm_digtable->large_fa_hit);

	if (rtlpriv->dm.dbginfo.num_qry_beacon_pkt < 10)
		dm_digtable->rx_gain_min = dm_dig_min;

	if (dm_digtable->rx_gain_min > dm_digtable->rx_gain_max)
		dm_digtable->rx_gain_min = dm_digtable->rx_gain_max;

	/*Adjust initial gain by false alarm*/
	if (mac->link_state >= MAC80211_LINKED) {
		rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD,
			"DIG AfterLink\n");
		if (first_connect) {
			if (dm_digtable->rssi_val_min <= dig_max_of_min)
				current_igi = dm_digtable->rssi_val_min;
			else
				current_igi = dig_max_of_min;
			rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD,
				"First Connect\n");
		} else {
			if (rtlpriv->falsealm_cnt.cnt_all > DM_DIG_FA_TH2)
				current_igi = current_igi + 4;
			else if (rtlpriv->falsealm_cnt.cnt_all > DM_DIG_FA_TH1)
				current_igi = current_igi + 2;
			else if (rtlpriv->falsealm_cnt.cnt_all < DM_DIG_FA_TH0)
				current_igi = current_igi - 2;

			if ((rtlpriv->dm.dbginfo.num_qry_beacon_pkt < 10) &&
			    (rtlpriv->falsealm_cnt.cnt_all < DM_DIG_FA_TH1)) {
				current_igi = dm_digtable->rx_gain_min;
				rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD,
					"Beacon is less than 10 and FA is less than 768, IGI GOES TO 0x1E!!!!!!!!!!!!\n");
			}
		}
	} else {
		rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD,
			"DIG BeforeLink\n");
		if (first_disconnect) {
			current_igi = dm_digtable->rx_gain_min;
			rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD,
				"First DisConnect\n");
		} else {
			/* 2012.03.30 LukeLee: enable DIG before
			 * link but with very high thresholds
			 */
			if (rtlpriv->falsealm_cnt.cnt_all > 2000)
				current_igi = current_igi + 4;
			else if (rtlpriv->falsealm_cnt.cnt_all > 600)
				current_igi = current_igi + 2;
			else if (rtlpriv->falsealm_cnt.cnt_all < 300)
				current_igi = current_igi - 2;

			if (current_igi >= 0x3e)
				current_igi = 0x3e;

			rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD, "England DIG\n");
		}
	}
	rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD,
		"DIG End Adjust IGI\n");
	/* Check initial gain by upper/lower bound*/

	if (current_igi > dm_digtable->rx_gain_max)
		current_igi = dm_digtable->rx_gain_max;
	if (current_igi < dm_digtable->rx_gain_min)
		current_igi = dm_digtable->rx_gain_min;

	rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD,
		"rx_gain_max=0x%x, rx_gain_min=0x%x\n",
		dm_digtable->rx_gain_max, dm_digtable->rx_gain_min);
	rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD,
		"TotalFA=%d\n", rtlpriv->falsealm_cnt.cnt_all);
	rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD,
		"CurIGValue=0x%x\n", current_igi);

	rtl8821ae_dm_write_dig(hw, current_igi);
	dm_digtable->media_connect_0 =
		((mac->link_state >= MAC80211_LINKED) ? true : false);
	dm_digtable->dig_min_0 = dig_min_0;
}

static void rtl8821ae_dm_common_info_self_update(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 cnt;

	rtlpriv->dm.tx_rate = 0xff;

	rtlpriv->dm.one_entry_only = false;

	if (rtlpriv->mac80211.opmode == NL80211_IFTYPE_STATION &&
	    rtlpriv->mac80211.link_state >= MAC80211_LINKED) {
		rtlpriv->dm.one_entry_only = true;
		return;
	}

	if (rtlpriv->mac80211.opmode == NL80211_IFTYPE_AP ||
	    rtlpriv->mac80211.opmode == NL80211_IFTYPE_ADHOC ||
	    rtlpriv->mac80211.opmode == NL80211_IFTYPE_MESH_POINT) {
		spin_lock_bh(&rtlpriv->locks.entry_list_lock);
		cnt = list_count_nodes(&rtlpriv->entry_list);
		spin_unlock_bh(&rtlpriv->locks.entry_list_lock);

		if (cnt == 1)
			rtlpriv->dm.one_entry_only = true;
	}
}

static void rtl8821ae_dm_false_alarm_counter_statistics(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct false_alarm_statistics *falsealm_cnt = &rtlpriv->falsealm_cnt;
	u32 cck_enable = 0;

	/*read OFDM FA counter*/
	falsealm_cnt->cnt_ofdm_fail =
		rtl_get_bbreg(hw, ODM_REG_OFDM_FA_11AC, BMASKLWORD);
	falsealm_cnt->cnt_cck_fail =
		rtl_get_bbreg(hw, ODM_REG_CCK_FA_11AC, BMASKLWORD);

	cck_enable =  rtl_get_bbreg(hw, ODM_REG_BB_RX_PATH_11AC, BIT(28));
	if (cck_enable)  /*if(pDM_Odm->pBandType == ODM_BAND_2_4G)*/
		falsealm_cnt->cnt_all = falsealm_cnt->cnt_ofdm_fail +
					falsealm_cnt->cnt_cck_fail;
	else
		falsealm_cnt->cnt_all = falsealm_cnt->cnt_ofdm_fail;

	/*reset OFDM FA counter*/
	rtl_set_bbreg(hw, ODM_REG_OFDM_FA_RST_11AC, BIT(17), 1);
	rtl_set_bbreg(hw, ODM_REG_OFDM_FA_RST_11AC, BIT(17), 0);
	/* reset CCK FA counter*/
	rtl_set_bbreg(hw, ODM_REG_CCK_FA_RST_11AC, BIT(15), 0);
	rtl_set_bbreg(hw, ODM_REG_CCK_FA_RST_11AC, BIT(15), 1);

	rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD, "Cnt_Cck_fail=%d\n",
		falsealm_cnt->cnt_cck_fail);
	rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD, "cnt_ofdm_fail=%d\n",
		falsealm_cnt->cnt_ofdm_fail);
	rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD, "Total False Alarm=%d\n",
		falsealm_cnt->cnt_all);
}

static void rtl8812ae_dm_check_txpower_tracking_thermalmeter(
		struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (!rtlpriv->dm.tm_trigger) {
		rtl_set_rfreg(hw, RF90_PATH_A, RF_T_METER_88E,
			      BIT(17) | BIT(16), 0x03);
		rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
			"Trigger 8812 Thermal Meter!!\n");
		rtlpriv->dm.tm_trigger = 1;
		return;
	}
	rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
		"Schedule TxPowerTracking direct call!!\n");
	rtl8812ae_dm_txpower_tracking_callback_thermalmeter(hw);
}

static void rtl8821ae_dm_iq_calibrate(struct ieee80211_hw *hw)
{
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_dm *rtldm = rtl_dm(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	if (mac->link_state >= MAC80211_LINKED) {
		if (rtldm->linked_interval < 3)
			rtldm->linked_interval++;

		if (rtldm->linked_interval == 2) {
			if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE)
				rtl8812ae_phy_iq_calibrate(hw, false);
			else
				rtl8821ae_phy_iq_calibrate(hw, false);
		}
	} else {
		rtldm->linked_interval = 0;
	}
}

static void rtl8812ae_get_delta_swing_table(struct ieee80211_hw *hw,
					    const u8 **up_a,
					    const u8 **down_a,
					    const u8 **up_b,
					    const u8 **down_b)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_dm *rtldm = rtl_dm(rtl_priv(hw));
	u8 channel = rtlphy->current_channel;
	u8 rate = rtldm->tx_rate;

	if (1 <= channel && channel <= 14) {
		if (RTL8821AE_RX_HAL_IS_CCK_RATE(rate)) {
			*up_a = rtl8812ae_delta_swing_table_idx_24gccka_p;
			*down_a = rtl8812ae_delta_swing_table_idx_24gccka_n;
			*up_b = rtl8812ae_delta_swing_table_idx_24gcckb_p;
			*down_b = rtl8812ae_delta_swing_table_idx_24gcckb_n;
		} else {
			*up_a = rtl8812ae_delta_swing_table_idx_24ga_p;
			*down_a = rtl8812ae_delta_swing_table_idx_24ga_n;
			*up_b = rtl8812ae_delta_swing_table_idx_24gb_p;
			*down_b = rtl8812ae_delta_swing_table_idx_24gb_n;
		}
	} else if (36 <= channel && channel <= 64) {
		*up_a = rtl8812ae_delta_swing_table_idx_5ga_p[0];
		*down_a = rtl8812ae_delta_swing_table_idx_5ga_n[0];
		*up_b = rtl8812ae_delta_swing_table_idx_5gb_p[0];
		*down_b = rtl8812ae_delta_swing_table_idx_5gb_n[0];
	} else if (100 <= channel && channel <= 140) {
		*up_a = rtl8812ae_delta_swing_table_idx_5ga_p[1];
		*down_a = rtl8812ae_delta_swing_table_idx_5ga_n[1];
		*up_b = rtl8812ae_delta_swing_table_idx_5gb_p[1];
		*down_b = rtl8812ae_delta_swing_table_idx_5gb_n[1];
	} else if (149 <= channel && channel <= 173) {
		*up_a = rtl8812ae_delta_swing_table_idx_5ga_p[2];
		*down_a = rtl8812ae_delta_swing_table_idx_5ga_n[2];
		*up_b = rtl8812ae_delta_swing_table_idx_5gb_p[2];
		*down_b = rtl8812ae_delta_swing_table_idx_5gb_n[2];
	} else {
		*up_a = rtl8818e_delta_swing_table_idx_24gb_p;
		*down_a = rtl8818e_delta_swing_table_idx_24gb_n;
		*up_b = rtl8818e_delta_swing_table_idx_24gb_p;
		*down_b = rtl8818e_delta_swing_table_idx_24gb_n;
	}
}

void rtl8821ae_dm_update_init_rate(struct ieee80211_hw *hw, u8 rate)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_dm	*rtldm = rtl_dm(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 p = 0;

	rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
		"Get C2H Command! Rate=0x%x\n", rate);

	rtldm->tx_rate = rate;

	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8821AE) {
		rtl8821ae_dm_txpwr_track_set_pwr(hw, MIX_MODE, RF90_PATH_A, 0);
	} else {
		for (p = RF90_PATH_A; p < MAX_PATH_NUM_8812A; p++)
			rtl8812ae_dm_txpwr_track_set_pwr(hw, MIX_MODE, p, 0);
	}
}

u8 rtl8821ae_hw_rate_to_mrate(struct ieee80211_hw *hw, u8 rate)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 ret_rate = MGN_1M;

	switch (rate) {
	case DESC_RATE1M:
		ret_rate = MGN_1M;
		break;
	case DESC_RATE2M:
		ret_rate = MGN_2M;
		break;
	case DESC_RATE5_5M:
		ret_rate = MGN_5_5M;
		break;
	case DESC_RATE11M:
		ret_rate = MGN_11M;
		break;
	case DESC_RATE6M:
		ret_rate = MGN_6M;
		break;
	case DESC_RATE9M:
		ret_rate = MGN_9M;
		break;
	case DESC_RATE12M:
		ret_rate = MGN_12M;
		break;
	case DESC_RATE18M:
		ret_rate = MGN_18M;
		break;
	case DESC_RATE24M:
		ret_rate = MGN_24M;
		break;
	case DESC_RATE36M:
		ret_rate = MGN_36M;
		break;
	case DESC_RATE48M:
		ret_rate = MGN_48M;
		break;
	case DESC_RATE54M:
		ret_rate = MGN_54M;
		break;
	case DESC_RATEMCS0:
		ret_rate = MGN_MCS0;
		break;
	case DESC_RATEMCS1:
		ret_rate = MGN_MCS1;
		break;
	case DESC_RATEMCS2:
		ret_rate = MGN_MCS2;
		break;
	case DESC_RATEMCS3:
		ret_rate = MGN_MCS3;
		break;
	case DESC_RATEMCS4:
		ret_rate = MGN_MCS4;
		break;
	case DESC_RATEMCS5:
		ret_rate = MGN_MCS5;
		break;
	case DESC_RATEMCS6:
		ret_rate = MGN_MCS6;
		break;
	case DESC_RATEMCS7:
		ret_rate = MGN_MCS7;
		break;
	case DESC_RATEMCS8:
		ret_rate = MGN_MCS8;
		break;
	case DESC_RATEMCS9:
		ret_rate = MGN_MCS9;
		break;
	case DESC_RATEMCS10:
		ret_rate = MGN_MCS10;
		break;
	case DESC_RATEMCS11:
		ret_rate = MGN_MCS11;
		break;
	case DESC_RATEMCS12:
		ret_rate = MGN_MCS12;
		break;
	case DESC_RATEMCS13:
		ret_rate = MGN_MCS13;
		break;
	case DESC_RATEMCS14:
		ret_rate = MGN_MCS14;
		break;
	case DESC_RATEMCS15:
		ret_rate = MGN_MCS15;
		break;
	case DESC_RATEVHT1SS_MCS0:
		ret_rate = MGN_VHT1SS_MCS0;
		break;
	case DESC_RATEVHT1SS_MCS1:
		ret_rate = MGN_VHT1SS_MCS1;
		break;
	case DESC_RATEVHT1SS_MCS2:
		ret_rate = MGN_VHT1SS_MCS2;
		break;
	case DESC_RATEVHT1SS_MCS3:
		ret_rate = MGN_VHT1SS_MCS3;
		break;
	case DESC_RATEVHT1SS_MCS4:
		ret_rate = MGN_VHT1SS_MCS4;
		break;
	case DESC_RATEVHT1SS_MCS5:
		ret_rate = MGN_VHT1SS_MCS5;
		break;
	case DESC_RATEVHT1SS_MCS6:
		ret_rate = MGN_VHT1SS_MCS6;
		break;
	case DESC_RATEVHT1SS_MCS7:
		ret_rate = MGN_VHT1SS_MCS7;
		break;
	case DESC_RATEVHT1SS_MCS8:
		ret_rate = MGN_VHT1SS_MCS8;
		break;
	case DESC_RATEVHT1SS_MCS9:
		ret_rate = MGN_VHT1SS_MCS9;
		break;
	case DESC_RATEVHT2SS_MCS0:
		ret_rate = MGN_VHT2SS_MCS0;
		break;
	case DESC_RATEVHT2SS_MCS1:
		ret_rate = MGN_VHT2SS_MCS1;
		break;
	case DESC_RATEVHT2SS_MCS2:
		ret_rate = MGN_VHT2SS_MCS2;
		break;
	case DESC_RATEVHT2SS_MCS3:
		ret_rate = MGN_VHT2SS_MCS3;
		break;
	case DESC_RATEVHT2SS_MCS4:
		ret_rate = MGN_VHT2SS_MCS4;
		break;
	case DESC_RATEVHT2SS_MCS5:
		ret_rate = MGN_VHT2SS_MCS5;
		break;
	case DESC_RATEVHT2SS_MCS6:
		ret_rate = MGN_VHT2SS_MCS6;
		break;
	case DESC_RATEVHT2SS_MCS7:
		ret_rate = MGN_VHT2SS_MCS7;
		break;
	case DESC_RATEVHT2SS_MCS8:
		ret_rate = MGN_VHT2SS_MCS8;
		break;
	case DESC_RATEVHT2SS_MCS9:
		ret_rate = MGN_VHT2SS_MCS9;
		break;
	default:
		rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
			"HwRateToMRate8812(): Non supported Rate [%x]!!!\n",
			rate);
		break;
	}
	return ret_rate;
}

/*-----------------------------------------------------------------------------
 * Function:	odm_TxPwrTrackSetPwr88E()
 *
 * Overview:	88E change all channel tx power accordign to flag.
 *				OFDM & CCK are all different.
 *
 * Input:		NONE
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	04/23/2012	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------
 */
void rtl8812ae_dm_txpwr_track_set_pwr(struct ieee80211_hw *hw,
				      enum pwr_track_control_method method,
				      u8 rf_path, u8 channel_mapped_index)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_dm	*rtldm = rtl_dm(rtl_priv(hw));
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u32 final_swing_idx[2];
	u8 pwr_tracking_limit = 26; /*+1.0dB*/
	u8 tx_rate = 0xFF;
	s8 final_ofdm_swing_index = 0;

	if (rtldm->tx_rate != 0xFF)
		tx_rate =
			rtl8821ae_hw_rate_to_mrate(hw, rtldm->tx_rate);

	rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
		"===>%s\n", __func__);
	/*20130429 Mimic Modify High Rate BBSwing Limit.*/
	if (tx_rate != 0xFF) {
		/*CCK*/
		if ((tx_rate >= MGN_1M) && (tx_rate <= MGN_11M))
			pwr_tracking_limit = 32; /*+4dB*/
		/*OFDM*/
		else if ((tx_rate >= MGN_6M) && (tx_rate <= MGN_48M))
			pwr_tracking_limit = 30; /*+3dB*/
		else if (tx_rate == MGN_54M)
			pwr_tracking_limit = 28; /*+2dB*/
		/*HT*/
		 /*QPSK/BPSK*/
		else if ((tx_rate >= MGN_MCS0) && (tx_rate <= MGN_MCS2))
			pwr_tracking_limit = 34; /*+5dB*/
		 /*16QAM*/
		else if ((tx_rate >= MGN_MCS3) && (tx_rate <= MGN_MCS4))
			pwr_tracking_limit = 30; /*+3dB*/
		 /*64QAM*/
		else if ((tx_rate >= MGN_MCS5) && (tx_rate <= MGN_MCS7))
			pwr_tracking_limit = 28; /*+2dB*/
		 /*QPSK/BPSK*/
		else if ((tx_rate >= MGN_MCS8) && (tx_rate <= MGN_MCS10))
			pwr_tracking_limit = 34; /*+5dB*/
		 /*16QAM*/
		else if ((tx_rate >= MGN_MCS11) && (tx_rate <= MGN_MCS12))
			pwr_tracking_limit = 30; /*+3dB*/
		 /*64QAM*/
		else if ((tx_rate >= MGN_MCS13) && (tx_rate <= MGN_MCS15))
			pwr_tracking_limit = 28; /*+2dB*/

		/*2 VHT*/
		 /*QPSK/BPSK*/
		else if ((tx_rate >= MGN_VHT1SS_MCS0) &&
			 (tx_rate <= MGN_VHT1SS_MCS2))
			pwr_tracking_limit = 34; /*+5dB*/
		 /*16QAM*/
		else if ((tx_rate >= MGN_VHT1SS_MCS3) &&
			 (tx_rate <= MGN_VHT1SS_MCS4))
			pwr_tracking_limit = 30; /*+3dB*/
		 /*64QAM*/
		else if ((tx_rate >= MGN_VHT1SS_MCS5) &&
			 (tx_rate <= MGN_VHT1SS_MCS6))
			pwr_tracking_limit = 28; /*+2dB*/
		else if (tx_rate == MGN_VHT1SS_MCS7) /*64QAM*/
			pwr_tracking_limit = 26; /*+1dB*/
		else if (tx_rate == MGN_VHT1SS_MCS8) /*256QAM*/
			pwr_tracking_limit = 24; /*+0dB*/
		else if (tx_rate == MGN_VHT1SS_MCS9) /*256QAM*/
			pwr_tracking_limit = 22; /*-1dB*/
		 /*QPSK/BPSK*/
		else if ((tx_rate >= MGN_VHT2SS_MCS0) &&
			 (tx_rate <= MGN_VHT2SS_MCS2))
			pwr_tracking_limit = 34; /*+5dB*/
		 /*16QAM*/
		else if ((tx_rate >= MGN_VHT2SS_MCS3) &&
			 (tx_rate <= MGN_VHT2SS_MCS4))
			pwr_tracking_limit = 30; /*+3dB*/
		 /*64QAM*/
		else if ((tx_rate >= MGN_VHT2SS_MCS5) &&
			 (tx_rate <= MGN_VHT2SS_MCS6))
			pwr_tracking_limit = 28; /*+2dB*/
		else if (tx_rate == MGN_VHT2SS_MCS7) /*64QAM*/
			pwr_tracking_limit = 26; /*+1dB*/
		else if (tx_rate == MGN_VHT2SS_MCS8) /*256QAM*/
			pwr_tracking_limit = 24; /*+0dB*/
		else if (tx_rate == MGN_VHT2SS_MCS9) /*256QAM*/
			pwr_tracking_limit = 22; /*-1dB*/
		else
			pwr_tracking_limit = 24;
	}
	rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
		"TxRate=0x%x, PwrTrackingLimit=%d\n",
		tx_rate, pwr_tracking_limit);

	if (method == BBSWING) {
		rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
			"===>%s\n", __func__);

		if (rf_path == RF90_PATH_A) {
			u32 tmp;

			final_swing_idx[RF90_PATH_A] =
				(rtldm->ofdm_index[RF90_PATH_A] >
				pwr_tracking_limit) ?
				pwr_tracking_limit :
				rtldm->ofdm_index[RF90_PATH_A];
			tmp = final_swing_idx[RF90_PATH_A];
			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"pDM_Odm->RFCalibrateInfo.OFDM_index[ODM_RF_PATH_A]=%d,pDM_Odm->RealBbSwingIdx[ODM_RF_PATH_A]=%d\n",
				rtldm->ofdm_index[RF90_PATH_A],
				final_swing_idx[RF90_PATH_A]);

			rtl_set_bbreg(hw, RA_TXSCALE, 0xFFE00000,
				      txscaling_tbl[tmp]);
		} else {
			u32 tmp;

			final_swing_idx[RF90_PATH_B] =
				rtldm->ofdm_index[RF90_PATH_B] >
				pwr_tracking_limit ?
				pwr_tracking_limit :
				rtldm->ofdm_index[RF90_PATH_B];
			tmp = final_swing_idx[RF90_PATH_B];
			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"pDM_Odm->RFCalibrateInfo.OFDM_index[ODM_RF_PATH_B]=%d, pDM_Odm->RealBbSwingIdx[ODM_RF_PATH_B]=%d\n",
				rtldm->ofdm_index[RF90_PATH_B],
				final_swing_idx[RF90_PATH_B]);

			rtl_set_bbreg(hw, RB_TXSCALE, 0xFFE00000,
				      txscaling_tbl[tmp]);
		}
	} else if (method == MIX_MODE) {
		rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
			"pDM_Odm->DefaultOfdmIndex=%d, pDM_Odm->Absolute_OFDMSwingIdx[RFPath]=%d, RF_Path = %d\n",
			rtldm->default_ofdm_index,
			rtldm->absolute_ofdm_swing_idx[rf_path],
			rf_path);

		final_ofdm_swing_index = rtldm->default_ofdm_index +
				rtldm->absolute_ofdm_swing_idx[rf_path];

		if (rf_path == RF90_PATH_A) {
			/*BBSwing higher then Limit*/
			if (final_ofdm_swing_index > pwr_tracking_limit) {
				rtldm->remnant_cck_idx =
					final_ofdm_swing_index -
					pwr_tracking_limit;
				/* CCK Follow the same compensation value
				 * as Path A
				 */
				rtldm->remnant_ofdm_swing_idx[rf_path] =
					final_ofdm_swing_index -
					pwr_tracking_limit;

				rtl_set_bbreg(hw, RA_TXSCALE, 0xFFE00000,
					      txscaling_tbl[pwr_tracking_limit]);

				rtldm->modify_txagc_flag_path_a = true;

				/*Set TxAGC Page C{};*/
				rtl8821ae_phy_set_txpower_level_by_path(hw,
					rtlphy->current_channel,
					RF90_PATH_A);

				rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
					"******Path_A Over BBSwing Limit ,PwrTrackingLimit = %d ,Remnant TxAGC Value = %d\n",
					pwr_tracking_limit,
					rtldm->remnant_ofdm_swing_idx[rf_path]);
			} else if (final_ofdm_swing_index < 0) {
				rtldm->remnant_cck_idx = final_ofdm_swing_index;
				/* CCK Follow the same compensate value as Path A*/
				rtldm->remnant_ofdm_swing_idx[rf_path] =
					final_ofdm_swing_index;

				rtl_set_bbreg(hw, RA_TXSCALE, 0xFFE00000,
					txscaling_tbl[0]);

				rtldm->modify_txagc_flag_path_a = true;

				/*Set TxAGC Page C{};*/
				rtl8821ae_phy_set_txpower_level_by_path(hw,
					rtlphy->current_channel, RF90_PATH_A);

				rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
					"******Path_A Lower then BBSwing lower bound  0 , Remnant TxAGC Value = %d\n",
					rtldm->remnant_ofdm_swing_idx[rf_path]);
			} else {
				rtl_set_bbreg(hw, RA_TXSCALE, 0xFFE00000,
					txscaling_tbl[(u8)final_ofdm_swing_index]);

				rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
					"******Path_A Compensate with BBSwing, Final_OFDM_Swing_Index = %d\n",
					final_ofdm_swing_index);
				/*If TxAGC has changed, reset TxAGC again*/
				if (rtldm->modify_txagc_flag_path_a) {
					rtldm->remnant_cck_idx = 0;
					rtldm->remnant_ofdm_swing_idx[rf_path] = 0;

					/*Set TxAGC Page C{};*/
					rtl8821ae_phy_set_txpower_level_by_path(hw,
						rtlphy->current_channel, RF90_PATH_A);
					rtldm->modify_txagc_flag_path_a = false;

					rtl_dbg(rtlpriv, COMP_POWER_TRACKING,
						DBG_LOUD,
						"******Path_A pDM_Odm->Modify_TxAGC_Flag = FALSE\n");
				}
			}
		}
		/*BBSwing higher then Limit*/
		if (rf_path == RF90_PATH_B) {
			if (final_ofdm_swing_index > pwr_tracking_limit) {
				rtldm->remnant_ofdm_swing_idx[rf_path] =
					final_ofdm_swing_index -
					pwr_tracking_limit;

				rtl_set_bbreg(hw, RB_TXSCALE,
					0xFFE00000,
					txscaling_tbl[pwr_tracking_limit]);

				rtldm->modify_txagc_flag_path_b = true;

				/*Set TxAGC Page E{};*/
				rtl8821ae_phy_set_txpower_level_by_path(hw,
					rtlphy->current_channel, RF90_PATH_B);

				rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
					"******Path_B Over BBSwing Limit , PwrTrackingLimit = %d , Remnant TxAGC Value = %d\n",
					pwr_tracking_limit,
					 rtldm->remnant_ofdm_swing_idx[rf_path]);
			} else if (final_ofdm_swing_index < 0) {
				rtldm->remnant_ofdm_swing_idx[rf_path] =
					final_ofdm_swing_index;

				rtl_set_bbreg(hw, RB_TXSCALE, 0xFFE00000,
					      txscaling_tbl[0]);

				rtldm->modify_txagc_flag_path_b = true;

				/*Set TxAGC Page E{};*/
				rtl8821ae_phy_set_txpower_level_by_path(hw,
					rtlphy->current_channel, RF90_PATH_B);

				rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
					"******Path_B Lower then BBSwing lower bound  0 , Remnant TxAGC Value = %d\n",
					rtldm->remnant_ofdm_swing_idx[rf_path]);
			} else {
				rtl_set_bbreg(hw, RB_TXSCALE, 0xFFE00000,
					txscaling_tbl[(u8)final_ofdm_swing_index]);

				rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
					"******Path_B Compensate with BBSwing ,Final_OFDM_Swing_Index = %d\n",
					final_ofdm_swing_index);
				 /*If TxAGC has changed, reset TxAGC again*/
				if (rtldm->modify_txagc_flag_path_b) {
					rtldm->remnant_ofdm_swing_idx[rf_path] = 0;

					/*Set TxAGC Page E{};*/
					rtl8821ae_phy_set_txpower_level_by_path(hw,
					rtlphy->current_channel, RF90_PATH_B);

					rtldm->modify_txagc_flag_path_b =
						false;

					rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
						"******Path_B pDM_Odm->Modify_TxAGC_Flag = FALSE\n");
				}
			}
		}
	} else {
		return;
	}
}

void rtl8812ae_dm_txpower_tracking_callback_thermalmeter(
	struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_dm	*rtldm = rtl_dm(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 thermal_value = 0, delta, delta_lck, delta_iqk, p = 0, i = 0;
	u8 thermal_value_avg_count = 0;
	u32 thermal_value_avg = 0;
	/* OFDM BB Swing should be less than +3.0dB, */
	u8 ofdm_min_index = 6;
	 /* GetRightChnlPlaceforIQK(pHalData->CurrentChannel)*/
	u8 index_for_channel = 0;
	/* 1. The following TWO tables decide
	 * the final index of OFDM/CCK swing table.
	 */
	const u8 *delta_swing_table_idx_tup_a;
	const u8 *delta_swing_table_idx_tdown_a;
	const u8 *delta_swing_table_idx_tup_b;
	const u8 *delta_swing_table_idx_tdown_b;

	/*2. Initialization ( 7 steps in total )*/
	rtl8812ae_get_delta_swing_table(hw,
		&delta_swing_table_idx_tup_a,
		&delta_swing_table_idx_tdown_a,
		&delta_swing_table_idx_tup_b,
		&delta_swing_table_idx_tdown_b);

	rtldm->txpower_trackinginit = true;

	rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
		"pDM_Odm->BbSwingIdxCckBase: %d, pDM_Odm->BbSwingIdxOfdmBase[A]:%d, pDM_Odm->DefaultOfdmIndex: %d\n",
		rtldm->swing_idx_cck_base,
		rtldm->swing_idx_ofdm_base[RF90_PATH_A],
		rtldm->default_ofdm_index);

	thermal_value = (u8)rtl_get_rfreg(hw, RF90_PATH_A,
		/*0x42: RF Reg[15:10] 88E*/
		RF_T_METER_8812A, 0xfc00);
	rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
		"Thermal Meter = 0x%X, EFUSE Thermal Base = 0x%X\n",
		thermal_value, rtlefuse->eeprom_thermalmeter);
	if (!rtldm->txpower_track_control ||
	    rtlefuse->eeprom_thermalmeter == 0 ||
	    rtlefuse->eeprom_thermalmeter == 0xFF)
		return;

	/* 3. Initialize ThermalValues of RFCalibrateInfo*/

	if (rtlhal->reloadtxpowerindex)
		rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
			"reload ofdm index for band switch\n");

	/*4. Calculate average thermal meter*/
	rtldm->thermalvalue_avg[rtldm->thermalvalue_avg_index] = thermal_value;
	rtldm->thermalvalue_avg_index++;
	if (rtldm->thermalvalue_avg_index == AVG_THERMAL_NUM_8812A)
		/*Average times =  c.AverageThermalNum*/
		rtldm->thermalvalue_avg_index = 0;

	for (i = 0; i < AVG_THERMAL_NUM_8812A; i++) {
		if (rtldm->thermalvalue_avg[i]) {
			thermal_value_avg += rtldm->thermalvalue_avg[i];
			thermal_value_avg_count++;
		}
	}
	/*Calculate Average ThermalValue after average enough times*/
	if (thermal_value_avg_count) {
		thermal_value = (u8)(thermal_value_avg /
				thermal_value_avg_count);
		rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
			"AVG Thermal Meter = 0x%X, EFUSE Thermal Base = 0x%X\n",
			thermal_value, rtlefuse->eeprom_thermalmeter);
	}

	/*5. Calculate delta, delta_LCK, delta_IQK.
	 *"delta" here is used to determine whether
	 *thermal value changes or not.
	 */
	delta = (thermal_value > rtldm->thermalvalue) ?
		(thermal_value - rtldm->thermalvalue) :
		(rtldm->thermalvalue - thermal_value);
	delta_lck = (thermal_value > rtldm->thermalvalue_lck) ?
		(thermal_value - rtldm->thermalvalue_lck) :
		(rtldm->thermalvalue_lck - thermal_value);
	delta_iqk = (thermal_value > rtldm->thermalvalue_iqk) ?
		(thermal_value - rtldm->thermalvalue_iqk) :
		(rtldm->thermalvalue_iqk - thermal_value);

	rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
		"(delta, delta_LCK, delta_IQK) = (%d, %d, %d)\n",
		delta, delta_lck, delta_iqk);

	/* 6. If necessary, do LCK.
	 * Delta temperature is equal to or larger than 20 centigrade.
	 */
	if (delta_lck >= IQK_THRESHOLD) {
		rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
			"delta_LCK(%d) >= Threshold_IQK(%d)\n",
			delta_lck, IQK_THRESHOLD);
		rtldm->thermalvalue_lck = thermal_value;
		rtl8821ae_phy_lc_calibrate(hw);
	}

	/*7. If necessary, move the index of swing table to adjust Tx power.*/

	if (delta > 0 && rtldm->txpower_track_control) {
		/* "delta" here is used to record the
		 * absolute value of differrence.
		 */
		delta = thermal_value > rtlefuse->eeprom_thermalmeter ?
			(thermal_value - rtlefuse->eeprom_thermalmeter) :
			(rtlefuse->eeprom_thermalmeter - thermal_value);

		if (delta >= TXPWR_TRACK_TABLE_SIZE)
			delta = TXPWR_TRACK_TABLE_SIZE - 1;

		/*7.1 The Final Power Index = BaseIndex + PowerIndexOffset*/

		if (thermal_value > rtlefuse->eeprom_thermalmeter) {
			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"delta_swing_table_idx_tup_a[%d] = %d\n",
				delta, delta_swing_table_idx_tup_a[delta]);
			rtldm->delta_power_index_last[RF90_PATH_A] =
				rtldm->delta_power_index[RF90_PATH_A];
			rtldm->delta_power_index[RF90_PATH_A] =
				delta_swing_table_idx_tup_a[delta];

			rtldm->absolute_ofdm_swing_idx[RF90_PATH_A] =
				delta_swing_table_idx_tup_a[delta];
			/*Record delta swing for mix mode power tracking*/

			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"******Temp is higher and pDM_Odm->Absolute_OFDMSwingIdx[ODM_RF_PATH_A] = %d\n",
			rtldm->absolute_ofdm_swing_idx[RF90_PATH_A]);

			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"delta_swing_table_idx_tup_b[%d] = %d\n",
				delta, delta_swing_table_idx_tup_b[delta]);
			rtldm->delta_power_index_last[RF90_PATH_B] =
				rtldm->delta_power_index[RF90_PATH_B];
			rtldm->delta_power_index[RF90_PATH_B] =
				delta_swing_table_idx_tup_b[delta];

			rtldm->absolute_ofdm_swing_idx[RF90_PATH_B] =
				delta_swing_table_idx_tup_b[delta];
			/*Record delta swing for mix mode power tracking*/

			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"******Temp is higher and pDM_Odm->Absolute_OFDMSwingIdx[ODM_RF_PATH_B] = %d\n",
				rtldm->absolute_ofdm_swing_idx[RF90_PATH_B]);
		} else {
			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"delta_swing_table_idx_tdown_a[%d] = %d\n",
				delta, delta_swing_table_idx_tdown_a[delta]);

			rtldm->delta_power_index_last[RF90_PATH_A] =
				rtldm->delta_power_index[RF90_PATH_A];
			rtldm->delta_power_index[RF90_PATH_A] =
				-1 * delta_swing_table_idx_tdown_a[delta];

			rtldm->absolute_ofdm_swing_idx[RF90_PATH_A] =
				-1 * delta_swing_table_idx_tdown_a[delta];
			/* Record delta swing for mix mode power tracking*/
			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"******Temp is lower and pDM_Odm->Absolute_OFDMSwingIdx[ODM_RF_PATH_A] = %d\n",
				rtldm->absolute_ofdm_swing_idx[RF90_PATH_A]);

			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"deltaSwingTableIdx_TDOWN_B[%d] = %d\n",
				delta, delta_swing_table_idx_tdown_b[delta]);

			rtldm->delta_power_index_last[RF90_PATH_B] =
				rtldm->delta_power_index[RF90_PATH_B];
			rtldm->delta_power_index[RF90_PATH_B] =
				-1 * delta_swing_table_idx_tdown_b[delta];

			rtldm->absolute_ofdm_swing_idx[RF90_PATH_B] =
				-1 * delta_swing_table_idx_tdown_b[delta];
			/*Record delta swing for mix mode power tracking*/

			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"******Temp is lower and pDM_Odm->Absolute_OFDMSwingIdx[ODM_RF_PATH_B] = %d\n",
				rtldm->absolute_ofdm_swing_idx[RF90_PATH_B]);
		}

		for (p = RF90_PATH_A; p < MAX_PATH_NUM_8812A; p++) {
			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"============================= [Path-%c]Calculating PowerIndexOffset =============================\n",
				(p == RF90_PATH_A ? 'A' : 'B'));

			if (rtldm->delta_power_index[p] ==
				rtldm->delta_power_index_last[p])
				/*If Thermal value changes but lookup
				table value still the same*/
				rtldm->power_index_offset[p] = 0;
			else
				rtldm->power_index_offset[p] =
					rtldm->delta_power_index[p] -
					rtldm->delta_power_index_last[p];
				/* Power Index Diff between 2
				 * times Power Tracking
				 */
			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"[Path-%c] PowerIndexOffset(%d) =DeltaPowerIndex(%d) -DeltaPowerIndexLast(%d)\n",
				(p == RF90_PATH_A ? 'A' : 'B'),
				rtldm->power_index_offset[p],
				rtldm->delta_power_index[p],
				rtldm->delta_power_index_last[p]);

			rtldm->ofdm_index[p] =
					rtldm->swing_idx_ofdm_base[p] +
					rtldm->power_index_offset[p];
			rtldm->cck_index =
					rtldm->swing_idx_cck_base +
					rtldm->power_index_offset[p];

			rtldm->swing_idx_cck = rtldm->cck_index;
			rtldm->swing_idx_ofdm[p] = rtldm->ofdm_index[p];

			/****Print BB Swing Base and Index Offset */

			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"The 'CCK' final index(%d) = BaseIndex(%d) + PowerIndexOffset(%d)\n",
				rtldm->swing_idx_cck,
				rtldm->swing_idx_cck_base,
				rtldm->power_index_offset[p]);
			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"The 'OFDM' final index(%d) = BaseIndex[%c](%d) + PowerIndexOffset(%d)\n",
				rtldm->swing_idx_ofdm[p],
				(p == RF90_PATH_A ? 'A' : 'B'),
				rtldm->swing_idx_ofdm_base[p],
				rtldm->power_index_offset[p]);

			/*7.1 Handle boundary conditions of index.*/

			if (rtldm->ofdm_index[p] > TXSCALE_TABLE_SIZE - 1)
				rtldm->ofdm_index[p] = TXSCALE_TABLE_SIZE - 1;
			else if (rtldm->ofdm_index[p] < ofdm_min_index)
				rtldm->ofdm_index[p] = ofdm_min_index;
		}
		rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
			"\n\n====================================================================================\n");
		if (rtldm->cck_index > TXSCALE_TABLE_SIZE - 1)
			rtldm->cck_index = TXSCALE_TABLE_SIZE - 1;
		else if (rtldm->cck_index < 0)
			rtldm->cck_index = 0;
	} else {
		rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
			"The thermal meter is unchanged or TxPowerTracking OFF(%d): ThermalValue: %d , pDM_Odm->RFCalibrateInfo.ThermalValue: %d\n",
			rtldm->txpower_track_control,
			thermal_value,
			rtldm->thermalvalue);

		for (p = RF90_PATH_A; p < MAX_PATH_NUM_8812A; p++)
			rtldm->power_index_offset[p] = 0;
	}
	/*Print Swing base & current*/
	rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
		"TxPowerTracking: [CCK] Swing Current Index: %d,Swing Base Index: %d\n",
		rtldm->cck_index, rtldm->swing_idx_cck_base);
	for (p = RF90_PATH_A; p < MAX_PATH_NUM_8812A; p++) {
		rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
			"TxPowerTracking: [OFDM] Swing Current Index: %d,Swing Base Index[%c]: %d\n",
			rtldm->ofdm_index[p],
			(p == RF90_PATH_A ? 'A' : 'B'),
			rtldm->swing_idx_ofdm_base[p]);
	}

	if ((rtldm->power_index_offset[RF90_PATH_A] != 0 ||
		rtldm->power_index_offset[RF90_PATH_B] != 0) &&
		rtldm->txpower_track_control) {
		/*7.2 Configure the Swing Table to adjust Tx Power.
		 *Always TRUE after Tx Power is adjusted by power tracking.
		 *
		 *2012/04/23 MH According to Luke's suggestion,
		 *we can not write BB digital
		 *to increase TX power. Otherwise, EVM will be bad.
		 *
		 *2012/04/25 MH Add for tx power tracking to set
		 *tx power in tx agc for 88E.
		 */
		if (thermal_value > rtldm->thermalvalue) {
			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"Temperature Increasing(A): delta_pi: %d , delta_t: %d, Now_t: %d,EFUSE_t: %d, Last_t: %d\n",
				rtldm->power_index_offset[RF90_PATH_A],
				delta, thermal_value,
				rtlefuse->eeprom_thermalmeter,
				rtldm->thermalvalue);

			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"Temperature Increasing(B): delta_pi: %d ,delta_t: %d, Now_t: %d, EFUSE_t: %d, Last_t: %d\n",
				rtldm->power_index_offset[RF90_PATH_B],
				delta, thermal_value,
				rtlefuse->eeprom_thermalmeter,
				rtldm->thermalvalue);
		} else if (thermal_value < rtldm->thermalvalue) { /*Low temperature*/
			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"Temperature Decreasing(A): delta_pi: %d , delta_t: %d, Now_t: %d, EFUSE_t: %d, Last_t: %d\n",
				rtldm->power_index_offset[RF90_PATH_A],
				delta, thermal_value,
				rtlefuse->eeprom_thermalmeter,
				rtldm->thermalvalue);

			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"Temperature Decreasing(B): delta_pi: %d , delta_t: %d, Now_t: %d, EFUSE_t: %d, Last_t: %d\n",
				rtldm->power_index_offset[RF90_PATH_B],
				delta, thermal_value,
				rtlefuse->eeprom_thermalmeter,
				rtldm->thermalvalue);
		}

		if (thermal_value > rtlefuse->eeprom_thermalmeter) {
			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"Temperature(%d) higher than PG value(%d)\n",
				thermal_value, rtlefuse->eeprom_thermalmeter);

			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"**********Enter POWER Tracking MIX_MODE**********\n");
			for (p = RF90_PATH_A; p < MAX_PATH_NUM_8812A; p++)
				rtl8812ae_dm_txpwr_track_set_pwr(hw, MIX_MODE,
								 p, 0);
		} else {
			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"Temperature(%d) lower than PG value(%d)\n",
				thermal_value, rtlefuse->eeprom_thermalmeter);

			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"**********Enter POWER Tracking MIX_MODE**********\n");
			for (p = RF90_PATH_A; p < MAX_PATH_NUM_8812A; p++)
				rtl8812ae_dm_txpwr_track_set_pwr(hw, MIX_MODE,
								 p, index_for_channel);
		}
		/*Record last time Power Tracking result as base.*/
		rtldm->swing_idx_cck_base = rtldm->swing_idx_cck;
		for (p = RF90_PATH_A; p < MAX_PATH_NUM_8812A; p++)
				rtldm->swing_idx_ofdm_base[p] =
					rtldm->swing_idx_ofdm[p];

		rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
			"pDM_Odm->RFCalibrateInfo.ThermalValue =%d ThermalValue= %d\n",
			rtldm->thermalvalue, thermal_value);
		/*Record last Power Tracking Thermal Value*/
		rtldm->thermalvalue = thermal_value;
	}
	/*Delta temperature is equal to or larger than
	20 centigrade (When threshold is 8).*/
	if (delta_iqk >= IQK_THRESHOLD)
		rtl8812ae_do_iqk(hw, delta_iqk, thermal_value, 8);

	rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
		"<===%s\n", __func__);
}

static void rtl8821ae_get_delta_swing_table(struct ieee80211_hw *hw,
					    const u8 **up_a,
					    const u8 **down_a)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_dm *rtldm = rtl_dm(rtl_priv(hw));
	u8 channel = rtlphy->current_channel;
	u8 rate = rtldm->tx_rate;

	if (1 <= channel && channel <= 14) {
		if (RTL8821AE_RX_HAL_IS_CCK_RATE(rate)) {
			*up_a = rtl8821ae_delta_swing_table_idx_24gccka_p;
			*down_a = rtl8821ae_delta_swing_table_idx_24gccka_n;
		} else {
			*up_a = rtl8821ae_delta_swing_table_idx_24ga_p;
			*down_a = rtl8821ae_delta_swing_table_idx_24ga_n;
		}
	} else if (36 <= channel && channel <= 64) {
		*up_a = rtl8821ae_delta_swing_table_idx_5ga_p[0];
		*down_a = rtl8821ae_delta_swing_table_idx_5ga_n[0];
	} else if (100 <= channel && channel <= 140) {
		*up_a = rtl8821ae_delta_swing_table_idx_5ga_p[1];
		*down_a = rtl8821ae_delta_swing_table_idx_5ga_n[1];
	} else if (149 <= channel && channel <= 173) {
		*up_a = rtl8821ae_delta_swing_table_idx_5ga_p[2];
		*down_a = rtl8821ae_delta_swing_table_idx_5ga_n[2];
	} else {
		*up_a = rtl8818e_delta_swing_table_idx_24gb_p;
		*down_a = rtl8818e_delta_swing_table_idx_24gb_n;
	}
	return;
}

/*-----------------------------------------------------------------------------
 * Function:	odm_TxPwrTrackSetPwr88E()
 *
 * Overview:	88E change all channel tx power accordign to flag.
 *				OFDM & CCK are all different.
 *
 * Input:		NONE
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	04/23/2012	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------
 */
void rtl8821ae_dm_txpwr_track_set_pwr(struct ieee80211_hw *hw,
				      enum pwr_track_control_method method,
				      u8 rf_path, u8 channel_mapped_index)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_dm	*rtldm = rtl_dm(rtl_priv(hw));
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u32 final_swing_idx[1];
	u8 pwr_tracking_limit = 26; /*+1.0dB*/
	u8 tx_rate = 0xFF;
	s8 final_ofdm_swing_index = 0;

	if (rtldm->tx_rate != 0xFF)
		tx_rate = rtl8821ae_hw_rate_to_mrate(hw, rtldm->tx_rate);

	rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD, "===>%s\n", __func__);

	if (tx_rate != 0xFF) { /* Mimic Modify High Rate BBSwing Limit.*/
		/*CCK*/
		if ((tx_rate >= MGN_1M) && (tx_rate <= MGN_11M))
			pwr_tracking_limit = 32; /*+4dB*/
		/*OFDM*/
		else if ((tx_rate >= MGN_6M) && (tx_rate <= MGN_48M))
			pwr_tracking_limit = 30; /*+3dB*/
		else if (tx_rate == MGN_54M)
			pwr_tracking_limit = 28; /*+2dB*/
		/*HT*/
		/*QPSK/BPSK*/
		else if ((tx_rate >= MGN_MCS0) && (tx_rate <= MGN_MCS2))
			pwr_tracking_limit = 34; /*+5dB*/
		/*16QAM*/
		else if ((tx_rate >= MGN_MCS3) && (tx_rate <= MGN_MCS4))
			pwr_tracking_limit = 30; /*+3dB*/
		/*64QAM*/
		else if ((tx_rate >= MGN_MCS5) && (tx_rate <= MGN_MCS7))
			pwr_tracking_limit = 28; /*+2dB*/
		/*2 VHT*/
		/*QPSK/BPSK*/
		else if ((tx_rate >= MGN_VHT1SS_MCS0) &&
			(tx_rate <= MGN_VHT1SS_MCS2))
			pwr_tracking_limit = 34; /*+5dB*/
		/*16QAM*/
		else if ((tx_rate >= MGN_VHT1SS_MCS3) &&
			(tx_rate <= MGN_VHT1SS_MCS4))
			pwr_tracking_limit = 30; /*+3dB*/
		/*64QAM*/
		else if ((tx_rate >= MGN_VHT1SS_MCS5) &&
			(tx_rate <= MGN_VHT1SS_MCS6))
			pwr_tracking_limit = 28; /*+2dB*/
		else if (tx_rate == MGN_VHT1SS_MCS7) /*64QAM*/
			pwr_tracking_limit = 26; /*+1dB*/
		else if (tx_rate == MGN_VHT1SS_MCS8) /*256QAM*/
			pwr_tracking_limit = 24; /*+0dB*/
		else if (tx_rate == MGN_VHT1SS_MCS9) /*256QAM*/
			pwr_tracking_limit = 22; /*-1dB*/
		else
			pwr_tracking_limit = 24;
	}
	rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
		"TxRate=0x%x, PwrTrackingLimit=%d\n",
		tx_rate, pwr_tracking_limit);

	if (method == BBSWING) {
		rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
			"===>%s\n", __func__);
		if (rf_path == RF90_PATH_A) {
			final_swing_idx[RF90_PATH_A] =
				(rtldm->ofdm_index[RF90_PATH_A] >
				pwr_tracking_limit) ?
				pwr_tracking_limit :
				rtldm->ofdm_index[RF90_PATH_A];
			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"pDM_Odm->RFCalibrateInfo.OFDM_index[ODM_RF_PATH_A]=%d,pDM_Odm->RealBbSwingIdx[ODM_RF_PATH_A]=%d\n",
				rtldm->ofdm_index[RF90_PATH_A],
				final_swing_idx[RF90_PATH_A]);

			rtl_set_bbreg(hw, RA_TXSCALE, 0xFFE00000,
				txscaling_tbl[final_swing_idx[RF90_PATH_A]]);
		}
	} else if (method == MIX_MODE) {
		rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
			"pDM_Odm->DefaultOfdmIndex=%d,pDM_Odm->Absolute_OFDMSwingIdx[RFPath]=%d, RF_Path = %d\n",
			rtldm->default_ofdm_index,
			rtldm->absolute_ofdm_swing_idx[rf_path],
			rf_path);

		final_ofdm_swing_index =
			rtldm->default_ofdm_index +
			rtldm->absolute_ofdm_swing_idx[rf_path];
		/*BBSwing higher then Limit*/
		if (rf_path == RF90_PATH_A) {
			if (final_ofdm_swing_index > pwr_tracking_limit) {
				rtldm->remnant_cck_idx =
					final_ofdm_swing_index -
					pwr_tracking_limit;
				/* CCK Follow the same compensate value as Path A*/
				rtldm->remnant_ofdm_swing_idx[rf_path] =
					final_ofdm_swing_index -
					pwr_tracking_limit;

				rtl_set_bbreg(hw, RA_TXSCALE,
					0xFFE00000,
					txscaling_tbl[pwr_tracking_limit]);

				rtldm->modify_txagc_flag_path_a = true;

				/*Set TxAGC Page C{};*/
				rtl8821ae_phy_set_txpower_level_by_path(hw,
					rtlphy->current_channel,
					RF90_PATH_A);

				rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
					" ******Path_A Over BBSwing Limit , PwrTrackingLimit = %d , Remnant TxAGC Value = %d\n",
					pwr_tracking_limit,
					rtldm->remnant_ofdm_swing_idx[rf_path]);
			} else if (final_ofdm_swing_index < 0) {
				rtldm->remnant_cck_idx = final_ofdm_swing_index;
				/* CCK Follow the same compensate value as Path A*/
				rtldm->remnant_ofdm_swing_idx[rf_path] =
					final_ofdm_swing_index;

				rtl_set_bbreg(hw, RA_TXSCALE, 0xFFE00000,
					txscaling_tbl[0]);

				rtldm->modify_txagc_flag_path_a = true;

				/*Set TxAGC Page C{};*/
				rtl8821ae_phy_set_txpower_level_by_path(hw,
					rtlphy->current_channel, RF90_PATH_A);

				rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
					"******Path_A Lower then BBSwing lower bound  0 , Remnant TxAGC Value = %d\n",
					rtldm->remnant_ofdm_swing_idx[rf_path]);
			} else {
				rtl_set_bbreg(hw, RA_TXSCALE, 0xFFE00000,
					txscaling_tbl[(u8)final_ofdm_swing_index]);

				rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
					"******Path_A Compensate with BBSwing ,Final_OFDM_Swing_Index = %d\n",
					final_ofdm_swing_index);
				/*If TxAGC has changed, reset TxAGC again*/
				if (rtldm->modify_txagc_flag_path_a) {
					rtldm->remnant_cck_idx = 0;
					rtldm->remnant_ofdm_swing_idx[rf_path] = 0;

					/*Set TxAGC Page C{};*/
					rtl8821ae_phy_set_txpower_level_by_path(hw,
						rtlphy->current_channel, RF90_PATH_A);

					rtldm->modify_txagc_flag_path_a = false;

					rtl_dbg(rtlpriv, COMP_POWER_TRACKING,
						DBG_LOUD,
						"******Path_A pDM_Odm->Modify_TxAGC_Flag= FALSE\n");
				}
			}
		}
	} else {
		return;
	}
}

void rtl8821ae_dm_txpower_tracking_callback_thermalmeter(
	struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_dm	*rtldm = rtl_dm(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_phy *rtlphy = &rtlpriv->phy;

	u8 thermal_value = 0, delta, delta_lck, delta_iqk, p = 0, i = 0;
	u8 thermal_value_avg_count = 0;
	u32 thermal_value_avg = 0;

	u8 ofdm_min_index = 6;  /*OFDM BB Swing should be less than +3.0dB */
	/* GetRightChnlPlaceforIQK(pHalData->CurrentChannel)*/
	u8 index_for_channel = 0;

	/* 1. The following TWO tables decide the final
	 * index of OFDM/CCK swing table.
	 */
	const u8 *delta_swing_table_idx_tup_a;
	const u8 *delta_swing_table_idx_tdown_a;

	/*2. Initilization ( 7 steps in total )*/
	rtl8821ae_get_delta_swing_table(hw,
					&delta_swing_table_idx_tup_a,
					&delta_swing_table_idx_tdown_a);

	rtldm->txpower_trackinginit = true;

	rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
		"===>%s,\n pDM_Odm->BbSwingIdxCckBase: %d,pDM_Odm->BbSwingIdxOfdmBase[A]:%d, pDM_Odm->DefaultOfdmIndex: %d\n",
		__func__,
		rtldm->swing_idx_cck_base,
		rtldm->swing_idx_ofdm_base[RF90_PATH_A],
		rtldm->default_ofdm_index);
	/*0x42: RF Reg[15:10] 88E*/
	thermal_value = (u8)rtl_get_rfreg(hw,
		RF90_PATH_A, RF_T_METER_8812A, 0xfc00);
	if (!rtldm->txpower_track_control ||
		rtlefuse->eeprom_thermalmeter == 0 ||
		rtlefuse->eeprom_thermalmeter == 0xFF)
		return;

	/* 3. Initialize ThermalValues of RFCalibrateInfo*/

	if (rtlhal->reloadtxpowerindex) {
		rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
			"reload ofdm index for band switch\n");
	}

	/*4. Calculate average thermal meter*/
	rtldm->thermalvalue_avg[rtldm->thermalvalue_avg_index] = thermal_value;
	rtldm->thermalvalue_avg_index++;
	if (rtldm->thermalvalue_avg_index == AVG_THERMAL_NUM_8812A)
		/*Average times =  c.AverageThermalNum*/
		rtldm->thermalvalue_avg_index = 0;

	for (i = 0; i < AVG_THERMAL_NUM_8812A; i++) {
		if (rtldm->thermalvalue_avg[i]) {
			thermal_value_avg += rtldm->thermalvalue_avg[i];
			thermal_value_avg_count++;
		}
	}
	/*Calculate Average ThermalValue after average enough times*/
	if (thermal_value_avg_count) {
		thermal_value = (u8)(thermal_value_avg /
				thermal_value_avg_count);
		rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
			"AVG Thermal Meter = 0x%X, EFUSE Thermal Base = 0x%X\n",
			thermal_value, rtlefuse->eeprom_thermalmeter);
	}

	/*5. Calculate delta, delta_LCK, delta_IQK.
	 *"delta" here is used to determine whether
	 * thermal value changes or not.
	 */
	delta = (thermal_value > rtldm->thermalvalue) ?
		(thermal_value - rtldm->thermalvalue) :
		(rtldm->thermalvalue - thermal_value);
	delta_lck = (thermal_value > rtldm->thermalvalue_lck) ?
		(thermal_value - rtldm->thermalvalue_lck) :
		(rtldm->thermalvalue_lck - thermal_value);
	delta_iqk = (thermal_value > rtldm->thermalvalue_iqk) ?
		(thermal_value - rtldm->thermalvalue_iqk) :
		(rtldm->thermalvalue_iqk - thermal_value);

	rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
		"(delta, delta_LCK, delta_IQK) = (%d, %d, %d)\n",
		delta, delta_lck, delta_iqk);

	/* 6. If necessary, do LCK.	*/
	/*Delta temperature is equal to or larger than 20 centigrade.*/
	if (delta_lck >= IQK_THRESHOLD) {
		rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
			"delta_LCK(%d) >= Threshold_IQK(%d)\n",
			delta_lck, IQK_THRESHOLD);
		rtldm->thermalvalue_lck = thermal_value;
		rtl8821ae_phy_lc_calibrate(hw);
	}

	/*7. If necessary, move the index of swing table to adjust Tx power.*/

	if (delta > 0 && rtldm->txpower_track_control) {
		/*"delta" here is used to record the
		 * absolute value of differrence.
		 */
		delta = thermal_value > rtlefuse->eeprom_thermalmeter ?
			(thermal_value - rtlefuse->eeprom_thermalmeter) :
			(rtlefuse->eeprom_thermalmeter - thermal_value);

		if (delta >= TXSCALE_TABLE_SIZE)
			delta = TXSCALE_TABLE_SIZE - 1;

		/*7.1 The Final Power Index = BaseIndex + PowerIndexOffset*/

		if (thermal_value > rtlefuse->eeprom_thermalmeter) {
			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"delta_swing_table_idx_tup_a[%d] = %d\n",
				delta, delta_swing_table_idx_tup_a[delta]);
			rtldm->delta_power_index_last[RF90_PATH_A] =
				rtldm->delta_power_index[RF90_PATH_A];
			rtldm->delta_power_index[RF90_PATH_A] =
				delta_swing_table_idx_tup_a[delta];

			rtldm->absolute_ofdm_swing_idx[RF90_PATH_A] =
				delta_swing_table_idx_tup_a[delta];
			/*Record delta swing for mix mode power tracking*/

			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"******Temp is higher and pDM_Odm->Absolute_OFDMSwingIdx[ODM_RF_PATH_A] = %d\n",
				rtldm->absolute_ofdm_swing_idx[RF90_PATH_A]);
		} else {
			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"delta_swing_table_idx_tdown_a[%d] = %d\n",
				delta, delta_swing_table_idx_tdown_a[delta]);

			rtldm->delta_power_index_last[RF90_PATH_A] =
				rtldm->delta_power_index[RF90_PATH_A];
			rtldm->delta_power_index[RF90_PATH_A] =
				-1 * delta_swing_table_idx_tdown_a[delta];

			rtldm->absolute_ofdm_swing_idx[RF90_PATH_A] =
				-1 * delta_swing_table_idx_tdown_a[delta];
			/* Record delta swing for mix mode power tracking*/
			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"******Temp is lower and pDM_Odm->Absolute_OFDMSwingIdx[ODM_RF_PATH_A] = %d\n",
				rtldm->absolute_ofdm_swing_idx[RF90_PATH_A]);
		}

		for (p = RF90_PATH_A; p < MAX_PATH_NUM_8821A; p++) {
			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"\n\n================================ [Path-%c]Calculating PowerIndexOffset ================================\n",
				(p == RF90_PATH_A ? 'A' : 'B'));
			/*If Thermal value changes but lookup table value
			 * still the same
			 */
			if (rtldm->delta_power_index[p] ==
				rtldm->delta_power_index_last[p])

				rtldm->power_index_offset[p] = 0;
			else
				rtldm->power_index_offset[p] =
					rtldm->delta_power_index[p] -
					rtldm->delta_power_index_last[p];
			/*Power Index Diff between 2 times Power Tracking*/

			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"[Path-%c] PowerIndexOffset(%d) = DeltaPowerIndex(%d) - DeltaPowerIndexLast(%d)\n",
				(p == RF90_PATH_A ? 'A' : 'B'),
				rtldm->power_index_offset[p],
				rtldm->delta_power_index[p] ,
				rtldm->delta_power_index_last[p]);

			rtldm->ofdm_index[p] =
					rtldm->swing_idx_ofdm_base[p] +
					rtldm->power_index_offset[p];
			rtldm->cck_index =
					rtldm->swing_idx_cck_base +
					rtldm->power_index_offset[p];

			rtldm->swing_idx_cck = rtldm->cck_index;
			rtldm->swing_idx_ofdm[p] = rtldm->ofdm_index[p];

			/*********Print BB Swing Base and Index Offset********/

			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"The 'CCK' final index(%d) = BaseIndex(%d) + PowerIndexOffset(%d)\n",
				rtldm->swing_idx_cck,
				rtldm->swing_idx_cck_base,
				rtldm->power_index_offset[p]);
			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"The 'OFDM' final index(%d) = BaseIndex[%c](%d) + PowerIndexOffset(%d)\n",
				rtldm->swing_idx_ofdm[p],
				(p == RF90_PATH_A ? 'A' : 'B'),
				rtldm->swing_idx_ofdm_base[p],
				rtldm->power_index_offset[p]);

			/*7.1 Handle boundary conditions of index.*/

			if (rtldm->ofdm_index[p] > TXSCALE_TABLE_SIZE - 1)
				rtldm->ofdm_index[p] = TXSCALE_TABLE_SIZE - 1;
			else if (rtldm->ofdm_index[p] < ofdm_min_index)
				rtldm->ofdm_index[p] = ofdm_min_index;
		}
		rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
			"\n\n========================================================================================================\n");
		if (rtldm->cck_index > TXSCALE_TABLE_SIZE - 1)
			rtldm->cck_index = TXSCALE_TABLE_SIZE - 1;
		else if (rtldm->cck_index < 0)
			rtldm->cck_index = 0;
	} else {
		rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
			"The thermal meter is unchanged or TxPowerTracking OFF(%d):ThermalValue: %d , pDM_Odm->RFCalibrateInfo.ThermalValue: %d\n",
			rtldm->txpower_track_control,
			thermal_value,
			rtldm->thermalvalue);

		for (p = RF90_PATH_A; p < MAX_PATH_NUM_8821A; p++)
			rtldm->power_index_offset[p] = 0;
	}
	rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
		"TxPowerTracking: [CCK] Swing Current Index: %d, Swing Base Index: %d\n",
		/*Print Swing base & current*/
		rtldm->cck_index, rtldm->swing_idx_cck_base);
	for (p = RF90_PATH_A; p < MAX_PATH_NUM_8821A; p++) {
		rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
			"TxPowerTracking: [OFDM] Swing Current Index: %d, Swing Base Index[%c]: %d\n",
			rtldm->ofdm_index[p],
			(p == RF90_PATH_A ? 'A' : 'B'),
			rtldm->swing_idx_ofdm_base[p]);
	}

	if ((rtldm->power_index_offset[RF90_PATH_A] != 0 ||
		rtldm->power_index_offset[RF90_PATH_B] != 0) &&
		rtldm->txpower_track_control) {
		/*7.2 Configure the Swing Table to adjust Tx Power.*/
		/*Always TRUE after Tx Power is adjusted by power tracking.*/
		/*
		 *  2012/04/23 MH According to Luke's suggestion,
		 *  we can not write BB digital
		 *  to increase TX power. Otherwise, EVM will be bad.
		 *
		 *  2012/04/25 MH Add for tx power tracking to
		 *  set tx power in tx agc for 88E.
		 */
		if (thermal_value > rtldm->thermalvalue) {
			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"Temperature Increasing(A): delta_pi: %d , delta_t: %d,Now_t: %d, EFUSE_t: %d, Last_t: %d\n",
				rtldm->power_index_offset[RF90_PATH_A],
				delta, thermal_value,
				rtlefuse->eeprom_thermalmeter,
				rtldm->thermalvalue);
		} else if (thermal_value < rtldm->thermalvalue) { /*Low temperature*/
			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"Temperature Decreasing(A): delta_pi: %d , delta_t: %d, Now_t: %d, EFUSE_t: %d, Last_t: %d\n",
				rtldm->power_index_offset[RF90_PATH_A],
				delta, thermal_value,
				rtlefuse->eeprom_thermalmeter,
				rtldm->thermalvalue);
		}

		if (thermal_value > rtlefuse->eeprom_thermalmeter) {
			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"Temperature(%d) higher than PG value(%d)\n",
				thermal_value, rtlefuse->eeprom_thermalmeter);

			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"****Enter POWER Tracking MIX_MODE****\n");
			for (p = RF90_PATH_A; p < MAX_PATH_NUM_8821A; p++)
					rtl8821ae_dm_txpwr_track_set_pwr(hw,
						MIX_MODE, p, index_for_channel);
		} else {
			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"Temperature(%d) lower than PG value(%d)\n",
				thermal_value, rtlefuse->eeprom_thermalmeter);

			rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				"*****Enter POWER Tracking MIX_MODE*****\n");
			for (p = RF90_PATH_A; p < MAX_PATH_NUM_8821A; p++)
				rtl8812ae_dm_txpwr_track_set_pwr(hw,
					MIX_MODE, p, index_for_channel);
		}
		/*Record last time Power Tracking result as base.*/
		rtldm->swing_idx_cck_base = rtldm->swing_idx_cck;
		for (p = RF90_PATH_A; p < MAX_PATH_NUM_8821A; p++)
			rtldm->swing_idx_ofdm_base[p] = rtldm->swing_idx_ofdm[p];

		rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
			"pDM_Odm->RFCalibrateInfo.ThermalValue = %d ThermalValue= %d\n",
			rtldm->thermalvalue, thermal_value);
		/*Record last Power Tracking Thermal Value*/
		rtldm->thermalvalue = thermal_value;
	}
	/* Delta temperature is equal to or larger than
	 * 20 centigrade (When threshold is 8).
	 */
	if (delta_iqk >= IQK_THRESHOLD) {
		if (!rtlphy->lck_inprogress) {
			spin_lock(&rtlpriv->locks.iqk_lock);
			rtlphy->lck_inprogress = true;
			spin_unlock(&rtlpriv->locks.iqk_lock);

			rtl8821ae_do_iqk(hw, delta_iqk, thermal_value, 8);

			spin_lock(&rtlpriv->locks.iqk_lock);
			rtlphy->lck_inprogress = false;
			spin_unlock(&rtlpriv->locks.iqk_lock);
		}
	}

	rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD, "<===%s\n", __func__);
}

void rtl8821ae_dm_check_txpower_tracking_thermalmeter(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	if (!rtlpriv->dm.tm_trigger) {
		rtl_set_rfreg(hw, RF90_PATH_A, RF_T_METER_88E, BIT(17)|BIT(16),
			      0x03);
		rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
			"Trigger 8821ae Thermal Meter!!\n");
		rtlpriv->dm.tm_trigger = 1;
		return;
	} else {
		rtl_dbg(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
			"Schedule TxPowerTracking !!\n");

		rtl8821ae_dm_txpower_tracking_callback_thermalmeter(hw);
		rtlpriv->dm.tm_trigger = 0;
	}
}

static void rtl8821ae_dm_refresh_rate_adaptive_mask(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rate_adaptive *p_ra = &rtlpriv->ra;
	u32 low_rssithresh_for_ra = p_ra->low2high_rssi_thresh_for_ra40m;
	u32 high_rssithresh_for_ra = p_ra->high_rssi_thresh_for_ra;
	u8 go_up_gap = 5;
	struct ieee80211_sta *sta = NULL;

	if (is_hal_stop(rtlhal)) {
		rtl_dbg(rtlpriv, COMP_RATE, DBG_LOUD,
			"driver is going to unload\n");
		return;
	}

	if (!rtlpriv->dm.useramask) {
		rtl_dbg(rtlpriv, COMP_RATE, DBG_LOUD,
			"driver does not control rate adaptive mask\n");
		return;
	}

	if (mac->link_state == MAC80211_LINKED &&
		mac->opmode == NL80211_IFTYPE_STATION) {
		switch (p_ra->pre_ratr_state) {
		case DM_RATR_STA_MIDDLE:
			high_rssithresh_for_ra += go_up_gap;
			break;
		case DM_RATR_STA_LOW:
			high_rssithresh_for_ra += go_up_gap;
			low_rssithresh_for_ra += go_up_gap;
			break;
		default:
			break;
		}

		if (rtlpriv->dm.undec_sm_pwdb >
		    (long)high_rssithresh_for_ra)
			p_ra->ratr_state = DM_RATR_STA_HIGH;
		else if (rtlpriv->dm.undec_sm_pwdb >
			 (long)low_rssithresh_for_ra)
			p_ra->ratr_state = DM_RATR_STA_MIDDLE;
		else
			p_ra->ratr_state = DM_RATR_STA_LOW;

		if (p_ra->pre_ratr_state != p_ra->ratr_state) {
			rtl_dbg(rtlpriv, COMP_RATE, DBG_LOUD,
				"RSSI = %ld\n",
				rtlpriv->dm.undec_sm_pwdb);
			rtl_dbg(rtlpriv, COMP_RATE, DBG_LOUD,
				"RSSI_LEVEL = %d\n", p_ra->ratr_state);
			rtl_dbg(rtlpriv, COMP_RATE, DBG_LOUD,
				"PreState = %d, CurState = %d\n",
				p_ra->pre_ratr_state, p_ra->ratr_state);

			rcu_read_lock();
			sta = rtl_find_sta(hw, mac->bssid);
			if (sta)
				rtlpriv->cfg->ops->update_rate_tbl(hw,
						sta, p_ra->ratr_state, true);
			rcu_read_unlock();

			p_ra->pre_ratr_state = p_ra->ratr_state;
		}
	}
}

static void rtl8821ae_dm_refresh_basic_rate_mask(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct dig_t *dm_digtable = &rtlpriv->dm_digtable;
	struct rtl_mac *mac = &rtlpriv->mac80211;
	static u8 stage;
	u8 cur_stage = 0;
	u16 basic_rate = RRSR_1M | RRSR_2M | RRSR_5_5M | RRSR_11M | RRSR_6M;

	if (mac->link_state < MAC80211_LINKED)
		cur_stage = 0;
	else if (dm_digtable->rssi_val_min < 25)
		cur_stage = 1;
	else if (dm_digtable->rssi_val_min > 30)
		cur_stage = 3;
	else
		cur_stage = 2;

	if (cur_stage != stage) {
		if (cur_stage == 1) {
			basic_rate &= (!(basic_rate ^ mac->basic_rates));
			rtlpriv->cfg->ops->set_hw_reg(hw,
				HW_VAR_BASIC_RATE, (u8 *)&basic_rate);
		} else if (cur_stage == 3 && (stage == 1 || stage == 2)) {
			rtlpriv->cfg->ops->set_hw_reg(hw,
				HW_VAR_BASIC_RATE, (u8 *)&mac->basic_rates);
		}
	}
	stage = cur_stage;
}

static void rtl8821ae_dm_edca_choose_traffic_idx(
	struct ieee80211_hw *hw, u64 cur_tx_bytes,
	u64 cur_rx_bytes, bool b_bias_on_rx,
	bool *pb_is_cur_rdl_state)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (b_bias_on_rx) {
		if (cur_tx_bytes > (cur_rx_bytes*4)) {
			*pb_is_cur_rdl_state = false;
			rtl_dbg(rtlpriv, COMP_TURBO, DBG_LOUD,
				"Uplink Traffic\n");
		} else {
			*pb_is_cur_rdl_state = true;
			rtl_dbg(rtlpriv, COMP_TURBO, DBG_LOUD,
				"Balance Traffic\n");
		}
	} else {
		if (cur_rx_bytes > (cur_tx_bytes*4)) {
			*pb_is_cur_rdl_state = true;
			rtl_dbg(rtlpriv, COMP_TURBO, DBG_LOUD,
				"Downlink	Traffic\n");
		} else {
			*pb_is_cur_rdl_state = false;
			rtl_dbg(rtlpriv, COMP_TURBO, DBG_LOUD,
				"Balance Traffic\n");
		}
	}
	return;
}

static void rtl8821ae_dm_check_edca_turbo(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_dm *rtldm =  rtl_dm(rtl_priv(hw));

	/*Keep past Tx/Rx packet count for RT-to-RT EDCA turbo.*/
	u64 cur_tx_ok_cnt = 0;
	u64 cur_rx_ok_cnt = 0;
	u32 edca_be_ul = 0x5ea42b;
	u32 edca_be_dl = 0x5ea42b;
	u32 edca_be = 0x5ea42b;
	u8 iot_peer = 0;
	bool *pb_is_cur_rdl_state = NULL;
	bool b_bias_on_rx = false;
	bool b_edca_turbo_on = false;

	rtl_dbg(rtlpriv, COMP_TURBO, DBG_LOUD,
		"%s=====>\n", __func__);
	rtl_dbg(rtlpriv, COMP_TURBO, DBG_LOUD,
		"Original BE PARAM: 0x%x\n",
		rtl_read_dword(rtlpriv, DM_REG_EDCA_BE_11N));

	if (rtlpriv->dm.dbginfo.num_non_be_pkt > 0x100)
		rtlpriv->dm.is_any_nonbepkts = true;
	rtlpriv->dm.dbginfo.num_non_be_pkt = 0;

	/*===============================
	 * list parameter for different platform
	 *===============================
	 */
	pb_is_cur_rdl_state = &rtlpriv->dm.is_cur_rdlstate;

	cur_tx_ok_cnt = rtlpriv->stats.txbytesunicast - rtldm->last_tx_ok_cnt;
	cur_rx_ok_cnt = rtlpriv->stats.rxbytesunicast - rtldm->last_rx_ok_cnt;

	rtldm->last_tx_ok_cnt = rtlpriv->stats.txbytesunicast;
	rtldm->last_rx_ok_cnt = rtlpriv->stats.rxbytesunicast;

	iot_peer = rtlpriv->mac80211.vendor;
	b_bias_on_rx = false;
	b_edca_turbo_on = ((!rtlpriv->dm.is_any_nonbepkts) &&
			   (!rtlpriv->dm.disable_framebursting)) ?
			   true : false;

	if (rtlpriv->rtlhal.hw_type != HARDWARE_TYPE_RTL8812AE) {
		if ((iot_peer == PEER_CISCO) &&
			(mac->mode == WIRELESS_MODE_N_24G)) {
			edca_be_dl = edca_setting_dl[iot_peer];
			edca_be_ul = edca_setting_ul[iot_peer];
		}
	}

	rtl_dbg(rtlpriv, COMP_TURBO, DBG_LOUD,
		"bIsAnyNonBEPkts : 0x%x  bDisableFrameBursting : 0x%x\n",
		rtlpriv->dm.is_any_nonbepkts,
		rtlpriv->dm.disable_framebursting);

	rtl_dbg(rtlpriv, COMP_TURBO, DBG_LOUD,
		"bEdcaTurboOn : 0x%x bBiasOnRx : 0x%x\n",
		b_edca_turbo_on, b_bias_on_rx);

	if (b_edca_turbo_on) {
		rtl_dbg(rtlpriv, COMP_TURBO, DBG_LOUD,
			"curTxOkCnt : 0x%llx\n", cur_tx_ok_cnt);
		rtl_dbg(rtlpriv, COMP_TURBO, DBG_LOUD,
			"curRxOkCnt : 0x%llx\n", cur_rx_ok_cnt);
		if (b_bias_on_rx)
			rtl8821ae_dm_edca_choose_traffic_idx(hw, cur_tx_ok_cnt,
				cur_rx_ok_cnt, true, pb_is_cur_rdl_state);
		else
			rtl8821ae_dm_edca_choose_traffic_idx(hw, cur_tx_ok_cnt,
				cur_rx_ok_cnt, false, pb_is_cur_rdl_state);

		edca_be = (*pb_is_cur_rdl_state) ?  edca_be_dl : edca_be_ul;

		rtl_write_dword(rtlpriv, DM_REG_EDCA_BE_11N, edca_be);

		rtl_dbg(rtlpriv, COMP_TURBO, DBG_LOUD,
			"EDCA Turbo on: EDCA_BE:0x%x\n", edca_be);

		rtlpriv->dm.current_turbo_edca = true;

		rtl_dbg(rtlpriv, COMP_TURBO, DBG_LOUD,
			"EDCA_BE_DL : 0x%x  EDCA_BE_UL : 0x%x  EDCA_BE : 0x%x\n",
			edca_be_dl, edca_be_ul, edca_be);
	} else {
		if (rtlpriv->dm.current_turbo_edca) {
			u8 tmp = AC0_BE;
			rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_AC_PARAM,
						      (u8 *)(&tmp));
		}
		rtlpriv->dm.current_turbo_edca = false;
	}

	rtlpriv->dm.is_any_nonbepkts = false;
	rtldm->last_tx_ok_cnt = rtlpriv->stats.txbytesunicast;
	rtldm->last_rx_ok_cnt = rtlpriv->stats.rxbytesunicast;
}

static void rtl8821ae_dm_cck_packet_detection_thresh(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct dig_t *dm_digtable = &rtlpriv->dm_digtable;
	u8 cur_cck_cca_thresh;

	if (rtlpriv->mac80211.link_state >= MAC80211_LINKED) {
		if (dm_digtable->rssi_val_min > 25) {
			cur_cck_cca_thresh = 0xcd;
		} else if ((dm_digtable->rssi_val_min <= 25) &&
			   (dm_digtable->rssi_val_min > 10)) {
			cur_cck_cca_thresh = 0x83;
		} else {
			if (rtlpriv->falsealm_cnt.cnt_cck_fail > 1000)
				cur_cck_cca_thresh = 0x83;
			else
				cur_cck_cca_thresh = 0x40;
		}
	} else {
		if (rtlpriv->falsealm_cnt.cnt_cck_fail > 1000)
			cur_cck_cca_thresh = 0x83;
		else
			cur_cck_cca_thresh = 0x40;
	}

	if (dm_digtable->cur_cck_cca_thres != cur_cck_cca_thresh)
		rtl_write_byte(rtlpriv, ODM_REG_CCK_CCA_11AC,
			       cur_cck_cca_thresh);

	dm_digtable->pre_cck_cca_thres = dm_digtable->cur_cck_cca_thres;
	dm_digtable->cur_cck_cca_thres = cur_cck_cca_thresh;
	rtl_dbg(rtlpriv, COMP_DIG, DBG_TRACE,
		"CCK cca thresh hold =%x\n", dm_digtable->cur_cck_cca_thres);
}

static void rtl8821ae_dm_dynamic_atc_switch(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_dm *rtldm = rtl_dm(rtl_priv(hw));
	u8 crystal_cap;
	u32 packet_count;
	int cfo_khz_a, cfo_khz_b, cfo_ave = 0, adjust_xtal = 0;
	int cfo_ave_diff;

	if (rtlpriv->mac80211.link_state < MAC80211_LINKED) {
		/*1.Enable ATC*/
		if (rtldm->atc_status == ATC_STATUS_OFF) {
			rtl_set_bbreg(hw, RFC_AREA, BIT(14), ATC_STATUS_ON);
			rtldm->atc_status = ATC_STATUS_ON;
		}

		rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD, "No link!!\n");
		rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD,
			"atc_status = %d\n", rtldm->atc_status);

		if (rtldm->crystal_cap != rtlpriv->efuse.crystalcap) {
			rtldm->crystal_cap = rtlpriv->efuse.crystalcap;
			crystal_cap = rtldm->crystal_cap & 0x3f;
			crystal_cap = crystal_cap & 0x3f;
			if (rtlpriv->rtlhal.hw_type == HARDWARE_TYPE_RTL8812AE)
				rtl_set_bbreg(hw, REG_MAC_PHY_CTRL,
					      0x7ff80000, (crystal_cap |
					      (crystal_cap << 6)));
			else
				rtl_set_bbreg(hw, REG_MAC_PHY_CTRL,
					      0xfff000, (crystal_cap |
					      (crystal_cap << 6)));
		}
		rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD, "crystal_cap = 0x%x\n",
			rtldm->crystal_cap);
	} else{
		/*1. Calculate CFO for path-A & path-B*/
		cfo_khz_a = (int)(rtldm->cfo_tail[0] * 3125) / 1280;
		cfo_khz_b = (int)(rtldm->cfo_tail[1] * 3125) / 1280;
		packet_count = rtldm->packet_count;

		/*2.No new packet*/
		if (packet_count == rtldm->packet_count_pre) {
			rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD,
				"packet counter doesn't change\n");
			return;
		}

		rtldm->packet_count_pre = packet_count;
		rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD,
			"packet counter = %d\n",
			rtldm->packet_count);

		/*3.Average CFO*/
		if (rtlpriv->phy.rf_type == RF_1T1R)
			cfo_ave = cfo_khz_a;
		else
			cfo_ave = (cfo_khz_a + cfo_khz_b) >> 1;

		rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD,
			"cfo_khz_a = %dkHz, cfo_khz_b = %dkHz, cfo_ave = %dkHz\n",
			cfo_khz_a, cfo_khz_b, cfo_ave);

		/*4.Avoid abnormal large CFO*/
		cfo_ave_diff = (rtldm->cfo_ave_pre >= cfo_ave) ?
						(rtldm->cfo_ave_pre - cfo_ave) :
						(cfo_ave - rtldm->cfo_ave_pre);

		if (cfo_ave_diff > 20 && !rtldm->large_cfo_hit) {
			rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD,
				"first large CFO hit\n");
			rtldm->large_cfo_hit = true;
			return;
		} else
			rtldm->large_cfo_hit = false;

		rtldm->cfo_ave_pre = cfo_ave;

		/*CFO tracking by adjusting Xtal cap.*/

		/*1.Dynamic Xtal threshold*/
		if (cfo_ave >= -rtldm->cfo_threshold &&
			cfo_ave <= rtldm->cfo_threshold &&
			rtldm->is_freeze == 0) {
			if (rtldm->cfo_threshold == CFO_THRESHOLD_XTAL) {
				rtldm->cfo_threshold = CFO_THRESHOLD_XTAL + 10;
				rtldm->is_freeze = 1;
			} else {
				rtldm->cfo_threshold = CFO_THRESHOLD_XTAL;
			}
		}
		rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD,
			"Dynamic threshold = %d\n",
			rtldm->cfo_threshold);

		/* 2.Calculate Xtal offset*/
		if (cfo_ave > rtldm->cfo_threshold && rtldm->crystal_cap < 0x3f)
			adjust_xtal = ((cfo_ave - CFO_THRESHOLD_XTAL) >> 2) + 1;
		else if ((cfo_ave < -rtlpriv->dm.cfo_threshold) &&
			 rtlpriv->dm.crystal_cap > 0)
			adjust_xtal = ((cfo_ave + CFO_THRESHOLD_XTAL) >> 2) - 1;
		rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD,
			"Crystal cap = 0x%x, Crystal cap offset = %d\n",
			rtldm->crystal_cap, adjust_xtal);

		/*3.Adjudt Crystal Cap.*/
		if (adjust_xtal != 0) {
			rtldm->is_freeze = 0;
			rtldm->crystal_cap += adjust_xtal;

			if (rtldm->crystal_cap > 0x3f)
				rtldm->crystal_cap = 0x3f;
			else if (rtldm->crystal_cap < 0)
				rtldm->crystal_cap = 0;

			crystal_cap = rtldm->crystal_cap & 0x3f;
			crystal_cap = crystal_cap & 0x3f;
			if (rtlpriv->rtlhal.hw_type == HARDWARE_TYPE_RTL8812AE)
				rtl_set_bbreg(hw, REG_MAC_PHY_CTRL,
					      0x7ff80000, (crystal_cap |
					      (crystal_cap << 6)));
			else
				rtl_set_bbreg(hw, REG_MAC_PHY_CTRL,
					      0xfff000, (crystal_cap |
					      (crystal_cap << 6)));
			rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD,
				"New crystal cap = 0x%x\n",
				rtldm->crystal_cap);
		}
	}
}

void rtl8821ae_dm_watchdog(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	bool fw_current_inpsmode = false;
	bool fw_ps_awake = true;

	rtlpriv->cfg->ops->get_hw_reg(hw, HW_VAR_FW_PSMODE_STATUS,
				      (u8 *)(&fw_current_inpsmode));

	rtlpriv->cfg->ops->get_hw_reg(hw, HW_VAR_FWLPS_RF_ON,
				      (u8 *)(&fw_ps_awake));

	if (ppsc->p2p_ps_info.p2p_ps_mode)
		fw_ps_awake = false;

	spin_lock(&rtlpriv->locks.rf_ps_lock);
	if ((ppsc->rfpwr_state == ERFON) &&
	    ((!fw_current_inpsmode) && fw_ps_awake) &&
	    (!ppsc->rfchange_inprogress)) {
		rtl8821ae_dm_common_info_self_update(hw);
		rtl8821ae_dm_false_alarm_counter_statistics(hw);
		rtl8821ae_dm_check_rssi_monitor(hw);
		rtl8821ae_dm_dig(hw);
		rtl8821ae_dm_cck_packet_detection_thresh(hw);
		rtl8821ae_dm_refresh_rate_adaptive_mask(hw);
		rtl8821ae_dm_refresh_basic_rate_mask(hw);
		rtl8821ae_dm_check_edca_turbo(hw);
		rtl8821ae_dm_dynamic_atc_switch(hw);
		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE)
			rtl8812ae_dm_check_txpower_tracking_thermalmeter(hw);
		else
			rtl8821ae_dm_check_txpower_tracking_thermalmeter(hw);
		rtl8821ae_dm_iq_calibrate(hw);
	}
	spin_unlock(&rtlpriv->locks.rf_ps_lock);

	rtlpriv->dm.dbginfo.num_qry_beacon_pkt = 0;
	rtl_dbg(rtlpriv, COMP_DIG, DBG_DMESG, "\n");
}

void rtl8821ae_dm_set_tx_ant_by_tx_info(struct ieee80211_hw *hw,
					u8 *pdesc, u32 mac_id)
{
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_dm *rtldm = rtl_dm(rtl_priv(hw));
	struct fast_ant_training *pfat_table = &rtldm->fat_table;
	__le32 *pdesc32 = (__le32 *)pdesc;

	if (rtlhal->hw_type != HARDWARE_TYPE_RTL8812AE)
		return;

	if (rtlefuse->antenna_div_type == CG_TRX_HW_ANTDIV)
		set_tx_desc_tx_ant(pdesc32, pfat_table->antsel_a[mac_id]);
}
