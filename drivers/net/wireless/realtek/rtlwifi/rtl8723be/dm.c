/******************************************************************************
 *
 * Copyright(c) 2009-2014  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#include "../wifi.h"
#include "../base.h"
#include "../pci.h"
#include "../core.h"
#include "reg.h"
#include "def.h"
#include "phy.h"
#include "dm.h"
#include "../rtl8723com/dm_common.h"
#include "fw.h"
#include "trx.h"
#include "../btcoexist/rtl_btc.h"

static const u32 ofdmswing_table[] = {
	0x0b40002d, /* 0,  -15.0dB */
	0x0c000030, /* 1,  -14.5dB */
	0x0cc00033, /* 2,  -14.0dB */
	0x0d800036, /* 3,  -13.5dB */
	0x0e400039, /* 4,  -13.0dB */
	0x0f00003c, /* 5,  -12.5dB */
	0x10000040, /* 6,  -12.0dB */
	0x11000044, /* 7,  -11.5dB */
	0x12000048, /* 8,  -11.0dB */
	0x1300004c, /* 9,  -10.5dB */
	0x14400051, /* 10, -10.0dB */
	0x15800056, /* 11, -9.5dB */
	0x16c0005b, /* 12, -9.0dB */
	0x18000060, /* 13, -8.5dB */
	0x19800066, /* 14, -8.0dB */
	0x1b00006c, /* 15, -7.5dB */
	0x1c800072, /* 16, -7.0dB */
	0x1e400079, /* 17, -6.5dB */
	0x20000080, /* 18, -6.0dB */
	0x22000088, /* 19, -5.5dB */
	0x24000090, /* 20, -5.0dB */
	0x26000098, /* 21, -4.5dB */
	0x288000a2, /* 22, -4.0dB */
	0x2ac000ab, /* 23, -3.5dB */
	0x2d4000b5, /* 24, -3.0dB */
	0x300000c0, /* 25, -2.5dB */
	0x32c000cb, /* 26, -2.0dB */
	0x35c000d7, /* 27, -1.5dB */
	0x390000e4, /* 28, -1.0dB */
	0x3c8000f2, /* 29, -0.5dB */
	0x40000100, /* 30, +0dB */
	0x43c0010f, /* 31, +0.5dB */
	0x47c0011f, /* 32, +1.0dB */
	0x4c000130, /* 33, +1.5dB */
	0x50800142, /* 34, +2.0dB */
	0x55400155, /* 35, +2.5dB */
	0x5a400169, /* 36, +3.0dB */
	0x5fc0017f, /* 37, +3.5dB */
	0x65400195, /* 38, +4.0dB */
	0x6b8001ae, /* 39, +4.5dB */
	0x71c001c7, /* 40, +5.0dB */
	0x788001e2, /* 41, +5.5dB */
	0x7f8001fe  /* 42, +6.0dB */
};

static const u8 cckswing_table_ch1ch13[CCK_TABLE_SIZE][8] = {
	{0x09, 0x08, 0x07, 0x06, 0x04, 0x03, 0x01, 0x01}, /*  0, -16.0dB */
	{0x09, 0x09, 0x08, 0x06, 0x05, 0x03, 0x01, 0x01}, /*  1, -15.5dB */
	{0x0a, 0x09, 0x08, 0x07, 0x05, 0x03, 0x02, 0x01}, /*  2, -15.0dB */
	{0x0a, 0x0a, 0x09, 0x07, 0x05, 0x03, 0x02, 0x01}, /*  3, -14.5dB */
	{0x0b, 0x0a, 0x09, 0x08, 0x06, 0x04, 0x02, 0x01}, /*  4, -14.0dB */
	{0x0b, 0x0b, 0x0a, 0x08, 0x06, 0x04, 0x02, 0x01}, /*  5, -13.5dB */
	{0x0c, 0x0c, 0x0a, 0x09, 0x06, 0x04, 0x02, 0x01}, /*  6, -13.0dB */
	{0x0d, 0x0c, 0x0b, 0x09, 0x07, 0x04, 0x02, 0x01}, /*  7, -12.5dB */
	{0x0d, 0x0d, 0x0c, 0x0a, 0x07, 0x05, 0x02, 0x01}, /*  8, -12.0dB */
	{0x0e, 0x0e, 0x0c, 0x0a, 0x08, 0x05, 0x02, 0x01}, /*  9, -11.5dB */
	{0x0f, 0x0f, 0x0d, 0x0b, 0x08, 0x05, 0x03, 0x01}, /* 10, -11.0dB */
	{0x10, 0x10, 0x0e, 0x0b, 0x08, 0x05, 0x03, 0x01}, /* 11, -10.5dB */
	{0x11, 0x11, 0x0f, 0x0c, 0x09, 0x06, 0x03, 0x01}, /* 12, -10.0dB */
	{0x12, 0x12, 0x0f, 0x0c, 0x09, 0x06, 0x03, 0x01}, /* 13, -9.5dB */
	{0x13, 0x13, 0x10, 0x0d, 0x0a, 0x06, 0x03, 0x01}, /* 14, -9.0dB */
	{0x14, 0x14, 0x11, 0x0e, 0x0b, 0x07, 0x03, 0x02}, /* 15, -8.5dB */
	{0x16, 0x15, 0x12, 0x0f, 0x0b, 0x07, 0x04, 0x01}, /* 16, -8.0dB */
	{0x17, 0x16, 0x13, 0x10, 0x0c, 0x08, 0x04, 0x02}, /* 17, -7.5dB */
	{0x18, 0x17, 0x15, 0x11, 0x0c, 0x08, 0x04, 0x02}, /* 18, -7.0dB */
	{0x1a, 0x19, 0x16, 0x12, 0x0d, 0x09, 0x04, 0x02}, /* 19, -6.5dB */
	{0x1b, 0x1a, 0x17, 0x13, 0x0e, 0x09, 0x04, 0x02}, /* 20, -6.0dB */
	{0x1d, 0x1c, 0x18, 0x14, 0x0f, 0x0a, 0x05, 0x02}, /* 21, -5.5dB */
	{0x1f, 0x1e, 0x1a, 0x15, 0x10, 0x0a, 0x05, 0x02}, /* 22, -5.0dB */
	{0x20, 0x20, 0x1b, 0x16, 0x11, 0x08, 0x05, 0x02}, /* 23, -4.5dB */
	{0x22, 0x21, 0x1d, 0x18, 0x11, 0x0b, 0x06, 0x02}, /* 24, -4.0dB */
	{0x24, 0x23, 0x1f, 0x19, 0x13, 0x0c, 0x06, 0x03}, /* 25, -3.5dB */
	{0x26, 0x25, 0x21, 0x1b, 0x14, 0x0d, 0x06, 0x03}, /* 26, -3.0dB */
	{0x28, 0x28, 0x22, 0x1c, 0x15, 0x0d, 0x07, 0x03}, /* 27, -2.5dB */
	{0x2b, 0x2a, 0x25, 0x1e, 0x16, 0x0e, 0x07, 0x03}, /* 28, -2.0dB */
	{0x2d, 0x2d, 0x27, 0x1f, 0x18, 0x0f, 0x08, 0x03}, /* 29, -1.5dB */
	{0x30, 0x2f, 0x29, 0x21, 0x19, 0x10, 0x08, 0x03}, /* 30, -1.0dB */
	{0x33, 0x32, 0x2b, 0x23, 0x1a, 0x11, 0x08, 0x04}, /* 31, -0.5dB */
	{0x36, 0x35, 0x2e, 0x25, 0x1c, 0x12, 0x09, 0x04}  /* 32, +0dB */
};

static const u8 cckswing_table_ch14[CCK_TABLE_SIZE][8] = {
	{0x09, 0x08, 0x07, 0x04, 0x00, 0x00, 0x00, 0x00}, /*  0, -16.0dB */
	{0x09, 0x09, 0x08, 0x05, 0x00, 0x00, 0x00, 0x00}, /*  1, -15.5dB */
	{0x0a, 0x09, 0x08, 0x05, 0x00, 0x00, 0x00, 0x00}, /*  2, -15.0dB */
	{0x0a, 0x0a, 0x09, 0x05, 0x00, 0x00, 0x00, 0x00}, /*  3, -14.5dB */
	{0x0b, 0x0a, 0x09, 0x05, 0x00, 0x00, 0x00, 0x00}, /*  4, -14.0dB */
	{0x0b, 0x0b, 0x0a, 0x06, 0x00, 0x00, 0x00, 0x00}, /*  5, -13.5dB */
	{0x0c, 0x0c, 0x0a, 0x06, 0x00, 0x00, 0x00, 0x00}, /*  6, -13.0dB */
	{0x0d, 0x0c, 0x0b, 0x06, 0x00, 0x00, 0x00, 0x00}, /*  7, -12.5dB */
	{0x0d, 0x0d, 0x0c, 0x07, 0x00, 0x00, 0x00, 0x00}, /*  8, -12.0dB */
	{0x0e, 0x0e, 0x0c, 0x07, 0x00, 0x00, 0x00, 0x00}, /*  9, -11.5dB */
	{0x0f, 0x0f, 0x0d, 0x08, 0x00, 0x00, 0x00, 0x00}, /* 10, -11.0dB */
	{0x10, 0x10, 0x0e, 0x08, 0x00, 0x00, 0x00, 0x00}, /* 11, -10.5dB */
	{0x11, 0x11, 0x0f, 0x09, 0x00, 0x00, 0x00, 0x00}, /* 12, -10.0dB */
	{0x12, 0x12, 0x0f, 0x09, 0x00, 0x00, 0x00, 0x00}, /* 13, -9.5dB */
	{0x13, 0x13, 0x10, 0x0a, 0x00, 0x00, 0x00, 0x00}, /* 14, -9.0dB */
	{0x14, 0x14, 0x11, 0x0a, 0x00, 0x00, 0x00, 0x00}, /* 15, -8.5dB */
	{0x16, 0x15, 0x12, 0x0b, 0x00, 0x00, 0x00, 0x00}, /* 16, -8.0dB */
	{0x17, 0x16, 0x13, 0x0b, 0x00, 0x00, 0x00, 0x00}, /* 17, -7.5dB */
	{0x18, 0x17, 0x15, 0x0c, 0x00, 0x00, 0x00, 0x00}, /* 18, -7.0dB */
	{0x1a, 0x19, 0x16, 0x0d, 0x00, 0x00, 0x00, 0x00}, /* 19, -6.5dB */
	{0x1b, 0x1a, 0x17, 0x0e, 0x00, 0x00, 0x00, 0x00}, /* 20, -6.0dB */
	{0x1d, 0x1c, 0x18, 0x0e, 0x00, 0x00, 0x00, 0x00}, /* 21, -5.5dB */
	{0x1f, 0x1e, 0x1a, 0x0f, 0x00, 0x00, 0x00, 0x00}, /* 22, -5.0dB */
	{0x20, 0x20, 0x1b, 0x10, 0x00, 0x00, 0x00, 0x00}, /* 23, -4.5dB */
	{0x22, 0x21, 0x1d, 0x11, 0x00, 0x00, 0x00, 0x00}, /* 24, -4.0dB */
	{0x24, 0x23, 0x1f, 0x12, 0x00, 0x00, 0x00, 0x00}, /* 25, -3.5dB */
	{0x26, 0x25, 0x21, 0x13, 0x00, 0x00, 0x00, 0x00}, /* 26, -3.0dB */
	{0x28, 0x28, 0x24, 0x14, 0x00, 0x00, 0x00, 0x00}, /* 27, -2.5dB */
	{0x2b, 0x2a, 0x25, 0x15, 0x00, 0x00, 0x00, 0x00}, /* 28, -2.0dB */
	{0x2d, 0x2d, 0x17, 0x17, 0x00, 0x00, 0x00, 0x00}, /* 29, -1.5dB */
	{0x30, 0x2f, 0x29, 0x18, 0x00, 0x00, 0x00, 0x00}, /* 30, -1.0dB */
	{0x33, 0x32, 0x2b, 0x19, 0x00, 0x00, 0x00, 0x00}, /* 31, -0.5dB */
	{0x36, 0x35, 0x2e, 0x1b, 0x00, 0x00, 0x00, 0x00}  /* 32, +0dB */
};

static const u32 edca_setting_dl[PEER_MAX] = {
	0xa44f,		/* 0 UNKNOWN */
	0x5ea44f,	/* 1 REALTEK_90 */
	0x5e4322,	/* 2 REALTEK_92SE */
	0x5ea42b,	/* 3 BROAD */
	0xa44f,		/* 4 RAL */
	0xa630,		/* 5 ATH */
	0x5ea630,	/* 6 CISCO */
	0x5ea42b,	/* 7 MARVELL */
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

void rtl8723be_dm_txpower_track_adjust(struct ieee80211_hw *hw, u8 type,
				       u8 *pdirection, u32 *poutwrite_val)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_dm *rtldm = rtl_dm(rtl_priv(hw));
	u8 pwr_val = 0;
	u8 ofdm_base = rtlpriv->dm.swing_idx_ofdm_base[RF90_PATH_A];
	u8 ofdm_val = rtlpriv->dm.swing_idx_ofdm[RF90_PATH_A];
	u8 cck_base = rtldm->swing_idx_cck_base;
	u8 cck_val = rtldm->swing_idx_cck;

	if (type == 0) {
		if (ofdm_val <= ofdm_base) {
			*pdirection = 1;
			pwr_val = ofdm_base - ofdm_val;
		} else {
			*pdirection = 2;
			pwr_val = ofdm_val - ofdm_base;
		}
	} else if (type == 1) {
		if (cck_val <= cck_base) {
			*pdirection = 1;
			pwr_val = cck_base - cck_val;
		} else {
			*pdirection = 2;
			pwr_val = cck_val - cck_base;
		}
	}

	if (pwr_val >= TXPWRTRACK_MAX_IDX && (*pdirection == 1))
		pwr_val = TXPWRTRACK_MAX_IDX;

	*poutwrite_val = pwr_val | (pwr_val << 8) |
		(pwr_val << 16) | (pwr_val << 24);
}

void rtl8723be_dm_init_rate_adaptive_mask(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rate_adaptive *p_ra = &rtlpriv->ra;

	p_ra->ratr_state = DM_RATR_STA_INIT;
	p_ra->pre_ratr_state = DM_RATR_STA_INIT;

	if (rtlpriv->dm.dm_type == DM_TYPE_BYDRIVER)
		rtlpriv->dm.useramask = true;
	else
		rtlpriv->dm.useramask = false;

	p_ra->high_rssi_thresh_for_ra = 50;
	p_ra->low_rssi_thresh_for_ra40m = 20;
}

static void rtl8723be_dm_init_txpower_tracking(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpriv->dm.txpower_tracking = true;
	rtlpriv->dm.txpower_track_control = true;
	rtlpriv->dm.thermalvalue = 0;

	rtlpriv->dm.ofdm_index[0] = 30;
	rtlpriv->dm.cck_index = 20;

	rtlpriv->dm.swing_idx_cck_base = rtlpriv->dm.cck_index;

	rtlpriv->dm.swing_idx_ofdm_base[0] = rtlpriv->dm.ofdm_index[0];
	rtlpriv->dm.delta_power_index[RF90_PATH_A] = 0;
	rtlpriv->dm.delta_power_index_last[RF90_PATH_A] = 0;
	rtlpriv->dm.power_index_offset[RF90_PATH_A] = 0;

	RT_TRACE(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
		 "  rtlpriv->dm.txpower_tracking = %d\n",
		  rtlpriv->dm.txpower_tracking);
}

static void rtl8723be_dm_init_dynamic_atc_switch(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpriv->dm.crystal_cap = rtlpriv->efuse.crystalcap;

	rtlpriv->dm.atc_status = rtl_get_bbreg(hw, ROFDM1_CFOTRACKING, 0x800);
	rtlpriv->dm.cfo_threshold = CFO_THRESHOLD_XTAL;
}

void rtl8723be_dm_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 cur_igvalue = rtl_get_bbreg(hw, ROFDM0_XAAGCCORE1, 0x7f);

	rtlpriv->dm.dm_type = DM_TYPE_BYDRIVER;
	rtl_dm_diginit(hw, cur_igvalue);
	rtl8723be_dm_init_rate_adaptive_mask(hw);
	rtl8723_dm_init_edca_turbo(hw);
	rtl8723_dm_init_dynamic_bb_powersaving(hw);
	rtl8723_dm_init_dynamic_txpower(hw);
	rtl8723be_dm_init_txpower_tracking(hw);
	rtl8723be_dm_init_dynamic_atc_switch(hw);
}

static void rtl8723be_dm_find_minimum_rssi(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct dig_t *rtl_dm_dig = &rtlpriv->dm_digtable;
	struct rtl_mac *mac = rtl_mac(rtlpriv);

	/* Determine the minimum RSSI  */
	if ((mac->link_state < MAC80211_LINKED) &&
	    (rtlpriv->dm.entry_min_undec_sm_pwdb == 0)) {
		rtl_dm_dig->min_undec_pwdb_for_dm = 0;
		RT_TRACE(rtlpriv, COMP_BB_POWERSAVING, DBG_LOUD,
			 "Not connected to any\n");
	}
	if (mac->link_state >= MAC80211_LINKED) {
		if (mac->opmode == NL80211_IFTYPE_AP ||
		    mac->opmode == NL80211_IFTYPE_ADHOC) {
			rtl_dm_dig->min_undec_pwdb_for_dm =
			    rtlpriv->dm.entry_min_undec_sm_pwdb;
			RT_TRACE(rtlpriv, COMP_BB_POWERSAVING, DBG_LOUD,
				 "AP Client PWDB = 0x%lx\n",
			       rtlpriv->dm.entry_min_undec_sm_pwdb);
		} else {
			rtl_dm_dig->min_undec_pwdb_for_dm =
			    rtlpriv->dm.undec_sm_pwdb;
			RT_TRACE(rtlpriv, COMP_BB_POWERSAVING, DBG_LOUD,
				 "STA Default Port PWDB = 0x%x\n",
				  rtl_dm_dig->min_undec_pwdb_for_dm);
		}
	} else {
		rtl_dm_dig->min_undec_pwdb_for_dm =
				rtlpriv->dm.entry_min_undec_sm_pwdb;
		RT_TRACE(rtlpriv, COMP_BB_POWERSAVING, DBG_LOUD,
			 "AP Ext Port or disconnect PWDB = 0x%x\n",
			  rtl_dm_dig->min_undec_pwdb_for_dm);
	}
	RT_TRACE(rtlpriv, COMP_DIG, DBG_LOUD, "MinUndecoratedPWDBForDM =%d\n",
		 rtl_dm_dig->min_undec_pwdb_for_dm);
}

static void rtl8723be_dm_check_rssi_monitor(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct dig_t *dm_digtable = &rtlpriv->dm_digtable;
	struct rtl_sta_info *drv_priv;
	u8 h2c_parameter[3] = { 0 };
	long tmp_entry_max_pwdb = 0, tmp_entry_min_pwdb = 0xff;

	/* AP & ADHOC & MESH */
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
	/* Indicate Rx signal strength to FW. */
	if (rtlpriv->dm.useramask) {
		h2c_parameter[2] =
			(u8)(rtlpriv->dm.undec_sm_pwdb & 0xFF);
		h2c_parameter[1] = 0x20;
		h2c_parameter[0] = 0;
		rtl8723be_fill_h2c_cmd(hw, H2C_RSSIBE_REPORT, 3, h2c_parameter);
	} else {
		rtl_write_byte(rtlpriv, 0x4fe,
			       rtlpriv->dm.undec_sm_pwdb);
	}
	rtl8723be_dm_find_minimum_rssi(hw);
	dm_digtable->rssi_val_min =
			rtlpriv->dm_digtable.min_undec_pwdb_for_dm;
}

void rtl8723be_dm_write_dig(struct ieee80211_hw *hw, u8 current_igi)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct dig_t *dm_digtable = &rtlpriv->dm_digtable;

	if (dm_digtable->stop_dig)
		return;

	if (dm_digtable->cur_igvalue != current_igi) {
		rtl_set_bbreg(hw, ROFDM0_XAAGCCORE1, 0x7f, current_igi);
		if (rtlpriv->phy.rf_type != RF_1T1R)
			rtl_set_bbreg(hw, ROFDM0_XBAGCCORE1,
				      0x7f, current_igi);
	}
	dm_digtable->pre_igvalue = dm_digtable->cur_igvalue;
	dm_digtable->cur_igvalue = current_igi;
}

static void rtl8723be_dm_dig(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct dig_t *dm_digtable = &rtlpriv->dm_digtable;
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u8 dig_min_0, dig_maxofmin;
	bool bfirstconnect, bfirstdisconnect;
	u8 dm_dig_max, dm_dig_min;
	u8 current_igi = dm_digtable->cur_igvalue;
	u8 offset;

	/* AP,BT */
	if (mac->act_scanning)
		return;

	dig_min_0 = dm_digtable->dig_min_0;
	bfirstconnect = (mac->link_state >= MAC80211_LINKED) &&
			!dm_digtable->media_connect_0;
	bfirstdisconnect = (mac->link_state < MAC80211_LINKED) &&
			(dm_digtable->media_connect_0);

	dm_dig_max = 0x5a;
	dm_dig_min = DM_DIG_MIN;
	dig_maxofmin = DM_DIG_MAX_AP;

	if (mac->link_state >= MAC80211_LINKED) {
		if ((dm_digtable->rssi_val_min + 10) > dm_dig_max)
			dm_digtable->rx_gain_max = dm_dig_max;
		else if ((dm_digtable->rssi_val_min + 10) < dm_dig_min)
			dm_digtable->rx_gain_max = dm_dig_min;
		else
			dm_digtable->rx_gain_max =
				dm_digtable->rssi_val_min + 10;

		if (rtlpriv->dm.one_entry_only) {
			offset = 12;
			if (dm_digtable->rssi_val_min - offset < dm_dig_min)
				dig_min_0 = dm_dig_min;
			else if (dm_digtable->rssi_val_min - offset >
							dig_maxofmin)
				dig_min_0 = dig_maxofmin;
			else
				dig_min_0 =
					dm_digtable->rssi_val_min - offset;
		} else {
			dig_min_0 = dm_dig_min;
		}

	} else {
		dm_digtable->rx_gain_max = dm_dig_max;
		dig_min_0 = dm_dig_min;
		RT_TRACE(rtlpriv, COMP_DIG, DBG_LOUD, "no link\n");
	}

	if (rtlpriv->falsealm_cnt.cnt_all > 10000) {
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
						dm_digtable->forbidden_igi + 1;
			dm_digtable->recover_cnt = 3600;
		}
	} else {
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
				} else {
					dm_digtable->forbidden_igi--;
					dm_digtable->rx_gain_min =
						dm_digtable->forbidden_igi + 1;
				}
			} else {
				dm_digtable->large_fa_hit = 0;
			}
		}
	}
	if (dm_digtable->rx_gain_min > dm_digtable->rx_gain_max)
		dm_digtable->rx_gain_min = dm_digtable->rx_gain_max;

	if (mac->link_state >= MAC80211_LINKED) {
		if (bfirstconnect) {
			if (dm_digtable->rssi_val_min <= dig_maxofmin)
				current_igi = dm_digtable->rssi_val_min;
			else
				current_igi = dig_maxofmin;

			dm_digtable->large_fa_hit = 0;
		} else {
			if (rtlpriv->falsealm_cnt.cnt_all > DM_DIG_FA_TH2)
				current_igi += 4;
			else if (rtlpriv->falsealm_cnt.cnt_all > DM_DIG_FA_TH1)
				current_igi += 2;
			else if (rtlpriv->falsealm_cnt.cnt_all < DM_DIG_FA_TH0)
				current_igi -= 2;
		}
	} else {
		if (bfirstdisconnect) {
			current_igi = dm_digtable->rx_gain_min;
		} else {
			if (rtlpriv->falsealm_cnt.cnt_all > 10000)
				current_igi += 4;
			else if (rtlpriv->falsealm_cnt.cnt_all > 8000)
				current_igi += 2;
			else if (rtlpriv->falsealm_cnt.cnt_all < 500)
				current_igi -= 2;
		}
	}

	if (current_igi > dm_digtable->rx_gain_max)
		current_igi = dm_digtable->rx_gain_max;
	else if (current_igi < dm_digtable->rx_gain_min)
		current_igi = dm_digtable->rx_gain_min;

	rtl8723be_dm_write_dig(hw, current_igi);
	dm_digtable->media_connect_0 =
		((mac->link_state >= MAC80211_LINKED) ? true : false);
	dm_digtable->dig_min_0 = dig_min_0;
}

static void rtl8723be_dm_false_alarm_counter_statistics(
					struct ieee80211_hw *hw)
{
	u32 ret_value;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct false_alarm_statistics *falsealm_cnt = &rtlpriv->falsealm_cnt;

	rtl_set_bbreg(hw, DM_REG_OFDM_FA_HOLDC_11N, BIT(31), 1);
	rtl_set_bbreg(hw, DM_REG_OFDM_FA_RSTD_11N, BIT(31), 1);

	ret_value = rtl_get_bbreg(hw, DM_REG_OFDM_FA_TYPE1_11N, MASKDWORD);
	falsealm_cnt->cnt_fast_fsync_fail = ret_value & 0xffff;
	falsealm_cnt->cnt_sb_search_fail = (ret_value & 0xffff0000) >> 16;

	ret_value = rtl_get_bbreg(hw, DM_REG_OFDM_FA_TYPE2_11N, MASKDWORD);
	falsealm_cnt->cnt_ofdm_cca = ret_value & 0xffff;
	falsealm_cnt->cnt_parity_fail = (ret_value & 0xffff0000) >> 16;

	ret_value = rtl_get_bbreg(hw, DM_REG_OFDM_FA_TYPE3_11N, MASKDWORD);
	falsealm_cnt->cnt_rate_illegal = ret_value & 0xffff;
	falsealm_cnt->cnt_crc8_fail = (ret_value & 0xffff0000) >> 16;

	ret_value = rtl_get_bbreg(hw, DM_REG_OFDM_FA_TYPE4_11N, MASKDWORD);
	falsealm_cnt->cnt_mcs_fail = ret_value & 0xffff;

	falsealm_cnt->cnt_ofdm_fail = falsealm_cnt->cnt_parity_fail +
				      falsealm_cnt->cnt_rate_illegal +
				      falsealm_cnt->cnt_crc8_fail +
				      falsealm_cnt->cnt_mcs_fail +
				      falsealm_cnt->cnt_fast_fsync_fail +
				      falsealm_cnt->cnt_sb_search_fail;

	rtl_set_bbreg(hw, DM_REG_CCK_FA_RST_11N, BIT(12), 1);
	rtl_set_bbreg(hw, DM_REG_CCK_FA_RST_11N, BIT(14), 1);

	ret_value = rtl_get_bbreg(hw, DM_REG_CCK_FA_RST_11N, MASKBYTE0);
	falsealm_cnt->cnt_cck_fail = ret_value;

	ret_value = rtl_get_bbreg(hw, DM_REG_CCK_FA_MSB_11N, MASKBYTE3);
	falsealm_cnt->cnt_cck_fail += (ret_value & 0xff) << 8;

	ret_value = rtl_get_bbreg(hw, DM_REG_CCK_CCA_CNT_11N, MASKDWORD);
	falsealm_cnt->cnt_cck_cca = ((ret_value & 0xff) << 8) |
				    ((ret_value & 0xff00) >> 8);

	falsealm_cnt->cnt_all = falsealm_cnt->cnt_fast_fsync_fail +
				falsealm_cnt->cnt_sb_search_fail +
				falsealm_cnt->cnt_parity_fail +
				falsealm_cnt->cnt_rate_illegal +
				falsealm_cnt->cnt_crc8_fail +
				falsealm_cnt->cnt_mcs_fail +
				falsealm_cnt->cnt_cck_fail;

	falsealm_cnt->cnt_cca_all = falsealm_cnt->cnt_ofdm_cca +
				    falsealm_cnt->cnt_cck_cca;

	rtl_set_bbreg(hw, DM_REG_OFDM_FA_RSTC_11N, BIT(31), 1);
	rtl_set_bbreg(hw, DM_REG_OFDM_FA_RSTC_11N, BIT(31), 0);
	rtl_set_bbreg(hw, DM_REG_OFDM_FA_RSTD_11N, BIT(27), 1);
	rtl_set_bbreg(hw, DM_REG_OFDM_FA_RSTD_11N, BIT(27), 0);

	rtl_set_bbreg(hw, DM_REG_OFDM_FA_HOLDC_11N, BIT(31), 0);
	rtl_set_bbreg(hw, DM_REG_OFDM_FA_RSTD_11N, BIT(31), 0);

	rtl_set_bbreg(hw, DM_REG_CCK_FA_RST_11N, BIT(13) | BIT(12), 0);
	rtl_set_bbreg(hw, DM_REG_CCK_FA_RST_11N, BIT(13) | BIT(12), 2);

	rtl_set_bbreg(hw, DM_REG_CCK_FA_RST_11N, BIT(15) | BIT(14), 0);
	rtl_set_bbreg(hw, DM_REG_CCK_FA_RST_11N, BIT(15) | BIT(14), 2);

	RT_TRACE(rtlpriv, COMP_DIG, DBG_TRACE,
		 "cnt_parity_fail = %d, cnt_rate_illegal = %d, cnt_crc8_fail = %d, cnt_mcs_fail = %d\n",
		 falsealm_cnt->cnt_parity_fail,
		 falsealm_cnt->cnt_rate_illegal,
		 falsealm_cnt->cnt_crc8_fail,
		 falsealm_cnt->cnt_mcs_fail);

	RT_TRACE(rtlpriv, COMP_DIG, DBG_TRACE,
		 "cnt_ofdm_fail = %x, cnt_cck_fail = %x, cnt_all = %x\n",
		 falsealm_cnt->cnt_ofdm_fail,
		 falsealm_cnt->cnt_cck_fail,
		 falsealm_cnt->cnt_all);
}

static void rtl8723be_dm_dynamic_txpower(struct ieee80211_hw *hw)
{
	/* 8723BE does not support ODM_BB_DYNAMIC_TXPWR*/
	return;
}

static void rtl8723be_set_iqk_matrix(struct ieee80211_hw *hw, u8 ofdm_index,
				     u8 rfpath, long iqk_result_x,
				     long iqk_result_y)
{
	long ele_a = 0, ele_d, ele_c = 0, value32;

	if (ofdm_index >= 43)
		ofdm_index = 43 - 1;

	ele_d = (ofdmswing_table[ofdm_index] & 0xFFC00000) >> 22;

	if (iqk_result_x != 0) {
		if ((iqk_result_x & 0x00000200) != 0)
			iqk_result_x = iqk_result_x | 0xFFFFFC00;
		ele_a = ((iqk_result_x * ele_d) >> 8) & 0x000003FF;

		if ((iqk_result_y & 0x00000200) != 0)
			iqk_result_y = iqk_result_y | 0xFFFFFC00;
		ele_c = ((iqk_result_y * ele_d) >> 8) & 0x000003FF;

		switch (rfpath) {
		case RF90_PATH_A:
			value32 = (ele_d << 22) |
				((ele_c & 0x3F) << 16) | ele_a;
			rtl_set_bbreg(hw, ROFDM0_XATXIQIMBALANCE, MASKDWORD,
				      value32);
			value32 = (ele_c & 0x000003C0) >> 6;
			rtl_set_bbreg(hw, ROFDM0_XCTXAFE, MASKH4BITS, value32);
			value32 = ((iqk_result_x * ele_d) >> 7) & 0x01;
			rtl_set_bbreg(hw, ROFDM0_ECCATHRESHOLD, BIT(24),
				      value32);
			break;
		default:
			break;
		}
	} else {
		switch (rfpath) {
		case RF90_PATH_A:
			rtl_set_bbreg(hw, ROFDM0_XATXIQIMBALANCE, MASKDWORD,
				      ofdmswing_table[ofdm_index]);
			rtl_set_bbreg(hw, ROFDM0_XCTXAFE, MASKH4BITS, 0x00);
			rtl_set_bbreg(hw, ROFDM0_ECCATHRESHOLD, BIT(24), 0x00);
			break;
		default:
			break;
		}
	}
}

static void rtl8723be_dm_tx_power_track_set_power(struct ieee80211_hw *hw,
					enum pwr_track_control_method method,
					u8 rfpath, u8 idx)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_dm *rtldm = rtl_dm(rtl_priv(hw));
	u8 swing_idx_ofdm_limit = 36;

	if (method == TXAGC) {
		rtl8723be_phy_set_txpower_level(hw, rtlphy->current_channel);
	} else if (method == BBSWING) {
		if (rtldm->swing_idx_cck >= CCK_TABLE_SIZE)
			rtldm->swing_idx_cck = CCK_TABLE_SIZE - 1;

		if (!rtldm->cck_inch14) {
			rtl_write_byte(rtlpriv, 0xa22,
			    cckswing_table_ch1ch13[rtldm->swing_idx_cck][0]);
			rtl_write_byte(rtlpriv, 0xa23,
			    cckswing_table_ch1ch13[rtldm->swing_idx_cck][1]);
			rtl_write_byte(rtlpriv, 0xa24,
			    cckswing_table_ch1ch13[rtldm->swing_idx_cck][2]);
			rtl_write_byte(rtlpriv, 0xa25,
			    cckswing_table_ch1ch13[rtldm->swing_idx_cck][3]);
			rtl_write_byte(rtlpriv, 0xa26,
			    cckswing_table_ch1ch13[rtldm->swing_idx_cck][4]);
			rtl_write_byte(rtlpriv, 0xa27,
			    cckswing_table_ch1ch13[rtldm->swing_idx_cck][5]);
			rtl_write_byte(rtlpriv, 0xa28,
			    cckswing_table_ch1ch13[rtldm->swing_idx_cck][6]);
			rtl_write_byte(rtlpriv, 0xa29,
			    cckswing_table_ch1ch13[rtldm->swing_idx_cck][7]);
		} else {
			rtl_write_byte(rtlpriv, 0xa22,
			    cckswing_table_ch14[rtldm->swing_idx_cck][0]);
			rtl_write_byte(rtlpriv, 0xa23,
			    cckswing_table_ch14[rtldm->swing_idx_cck][1]);
			rtl_write_byte(rtlpriv, 0xa24,
			    cckswing_table_ch14[rtldm->swing_idx_cck][2]);
			rtl_write_byte(rtlpriv, 0xa25,
			    cckswing_table_ch14[rtldm->swing_idx_cck][3]);
			rtl_write_byte(rtlpriv, 0xa26,
			    cckswing_table_ch14[rtldm->swing_idx_cck][4]);
			rtl_write_byte(rtlpriv, 0xa27,
			    cckswing_table_ch14[rtldm->swing_idx_cck][5]);
			rtl_write_byte(rtlpriv, 0xa28,
			    cckswing_table_ch14[rtldm->swing_idx_cck][6]);
			rtl_write_byte(rtlpriv, 0xa29,
			    cckswing_table_ch14[rtldm->swing_idx_cck][7]);
		}

		if (rfpath == RF90_PATH_A) {
			if (rtldm->swing_idx_ofdm[RF90_PATH_A] <
			    swing_idx_ofdm_limit)
				swing_idx_ofdm_limit =
					rtldm->swing_idx_ofdm[RF90_PATH_A];

			rtl8723be_set_iqk_matrix(hw,
				rtldm->swing_idx_ofdm[rfpath], rfpath,
				rtlphy->iqk_matrix[idx].value[0][0],
				rtlphy->iqk_matrix[idx].value[0][1]);
		} else if (rfpath == RF90_PATH_B) {
			if (rtldm->swing_idx_ofdm[RF90_PATH_B] <
			    swing_idx_ofdm_limit)
				swing_idx_ofdm_limit =
					rtldm->swing_idx_ofdm[RF90_PATH_B];

			rtl8723be_set_iqk_matrix(hw,
				rtldm->swing_idx_ofdm[rfpath], rfpath,
				rtlphy->iqk_matrix[idx].value[0][4],
				rtlphy->iqk_matrix[idx].value[0][5]);
		}
	} else {
		return;
	}
}

static void rtl8723be_dm_txpower_tracking_callback_thermalmeter(
							struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_dm	*rtldm = rtl_dm(rtl_priv(hw));
	u8 thermalvalue = 0, delta, delta_lck, delta_iqk;
	u8 thermalvalue_avg_count = 0;
	u32 thermalvalue_avg = 0;
	int i = 0;

	u8 ofdm_min_index = 6;
	u8 index_for_channel = 0;

	s8 delta_swing_table_idx_tup_a[TXSCALE_TABLE_SIZE] = {
		0, 0, 1, 2, 2, 2, 3, 3, 3, 4,  5,
		5, 6, 6, 7, 7, 8, 8, 9, 9, 9, 10,
		10, 11, 11, 12, 12, 13, 14, 15};
	s8 delta_swing_table_idx_tdown_a[TXSCALE_TABLE_SIZE] = {
		0, 0, 1, 2, 2, 2, 3, 3, 3, 4,  5,
		5, 6, 6, 6, 6, 7, 7, 7, 8, 8,  9,
		9, 10, 10, 11, 12, 13, 14, 15};

	/*Initilization ( 7 steps in total )*/
	rtlpriv->dm.txpower_trackinginit = true;
	RT_TRACE(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
		 "rtl8723be_dm_txpower_tracking_callback_thermalmeter\n");

	thermalvalue = (u8)rtl_get_rfreg(hw,
		RF90_PATH_A, RF_T_METER, 0xfc00);
	if (!rtlpriv->dm.txpower_track_control || thermalvalue == 0 ||
	    rtlefuse->eeprom_thermalmeter == 0xFF)
		return;
	RT_TRACE(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
		 "Readback Thermal Meter = 0x%x pre thermal meter 0x%x eeprom_thermalmeter 0x%x\n",
		 thermalvalue, rtldm->thermalvalue,
		 rtlefuse->eeprom_thermalmeter);
	/*3 Initialize ThermalValues of RFCalibrateInfo*/
	if (!rtldm->thermalvalue) {
		rtlpriv->dm.thermalvalue_lck = thermalvalue;
		rtlpriv->dm.thermalvalue_iqk = thermalvalue;
	}

	/*4 Calculate average thermal meter*/
	rtldm->thermalvalue_avg[rtldm->thermalvalue_avg_index] = thermalvalue;
	rtldm->thermalvalue_avg_index++;
	if (rtldm->thermalvalue_avg_index == AVG_THERMAL_NUM_8723BE)
		rtldm->thermalvalue_avg_index = 0;

	for (i = 0; i < AVG_THERMAL_NUM_8723BE; i++) {
		if (rtldm->thermalvalue_avg[i]) {
			thermalvalue_avg += rtldm->thermalvalue_avg[i];
			thermalvalue_avg_count++;
		}
	}

	if (thermalvalue_avg_count)
		thermalvalue = (u8)(thermalvalue_avg / thermalvalue_avg_count);

	/* 5 Calculate delta, delta_LCK, delta_IQK.*/
	delta = (thermalvalue > rtlpriv->dm.thermalvalue) ?
		(thermalvalue - rtlpriv->dm.thermalvalue) :
		(rtlpriv->dm.thermalvalue - thermalvalue);
	delta_lck = (thermalvalue > rtlpriv->dm.thermalvalue_lck) ?
		    (thermalvalue - rtlpriv->dm.thermalvalue_lck) :
		    (rtlpriv->dm.thermalvalue_lck - thermalvalue);
	delta_iqk = (thermalvalue > rtlpriv->dm.thermalvalue_iqk) ?
		    (thermalvalue - rtlpriv->dm.thermalvalue_iqk) :
		    (rtlpriv->dm.thermalvalue_iqk - thermalvalue);

	RT_TRACE(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
		 "Readback Thermal Meter = 0x%x pre thermal meter 0x%x eeprom_thermalmeter 0x%x delta 0x%x delta_lck 0x%x delta_iqk 0x%x\n",
		 thermalvalue, rtlpriv->dm.thermalvalue,
		 rtlefuse->eeprom_thermalmeter, delta, delta_lck, delta_iqk);
	/* 6 If necessary, do LCK.*/
	if (delta_lck >= IQK_THRESHOLD) {
		rtlpriv->dm.thermalvalue_lck = thermalvalue;
		rtl8723be_phy_lc_calibrate(hw);
	}

	/* 7 If necessary, move the index of
	 * swing table to adjust Tx power.
	 */
	if (delta > 0 && rtlpriv->dm.txpower_track_control) {
		delta = (thermalvalue > rtlefuse->eeprom_thermalmeter) ?
			(thermalvalue - rtlefuse->eeprom_thermalmeter) :
			(rtlefuse->eeprom_thermalmeter - thermalvalue);

		if (delta >= TXSCALE_TABLE_SIZE)
			delta = TXSCALE_TABLE_SIZE - 1;
		/* 7.1 Get the final CCK_index and
		 * OFDM_index for each swing table.
		 */
		if (thermalvalue > rtlefuse->eeprom_thermalmeter) {
			rtldm->delta_power_index_last[RF90_PATH_A] =
					rtldm->delta_power_index[RF90_PATH_A];
			rtldm->delta_power_index[RF90_PATH_A] =
					delta_swing_table_idx_tup_a[delta];
		} else {
			rtldm->delta_power_index_last[RF90_PATH_A] =
					rtldm->delta_power_index[RF90_PATH_A];
			rtldm->delta_power_index[RF90_PATH_A] =
				-1 * delta_swing_table_idx_tdown_a[delta];
		}

		/* 7.2 Handle boundary conditions of index.*/
		if (rtldm->delta_power_index[RF90_PATH_A] ==
		    rtldm->delta_power_index_last[RF90_PATH_A])
			rtldm->power_index_offset[RF90_PATH_A] = 0;
		else
			rtldm->power_index_offset[RF90_PATH_A] =
				rtldm->delta_power_index[RF90_PATH_A] -
				rtldm->delta_power_index_last[RF90_PATH_A];

		rtldm->ofdm_index[0] =
			rtldm->swing_idx_ofdm_base[RF90_PATH_A] +
			rtldm->power_index_offset[RF90_PATH_A];
		rtldm->cck_index = rtldm->swing_idx_cck_base +
				   rtldm->power_index_offset[RF90_PATH_A];

		rtldm->swing_idx_cck = rtldm->cck_index;
		rtldm->swing_idx_ofdm[0] = rtldm->ofdm_index[0];

		if (rtldm->ofdm_index[0] > OFDM_TABLE_SIZE - 1)
			rtldm->ofdm_index[0] = OFDM_TABLE_SIZE - 1;
		else if (rtldm->ofdm_index[0] < ofdm_min_index)
			rtldm->ofdm_index[0] = ofdm_min_index;

		if (rtldm->cck_index > CCK_TABLE_SIZE - 1)
			rtldm->cck_index = CCK_TABLE_SIZE - 1;
		else if (rtldm->cck_index < 0)
			rtldm->cck_index = 0;
	} else {
		rtldm->power_index_offset[RF90_PATH_A] = 0;
	}

	if ((rtldm->power_index_offset[RF90_PATH_A] != 0) &&
	    (rtldm->txpower_track_control)) {
		rtldm->done_txpower = true;
		rtl8723be_dm_tx_power_track_set_power(hw, BBSWING, 0,
						      index_for_channel);

		rtldm->swing_idx_cck_base = rtldm->swing_idx_cck;
		rtldm->swing_idx_ofdm_base[RF90_PATH_A] =
						rtldm->swing_idx_ofdm[0];
		rtldm->thermalvalue = thermalvalue;
	}

	if (delta_iqk >= IQK_THRESHOLD) {
		rtldm->thermalvalue_iqk = thermalvalue;
		rtl8723be_phy_iq_calibrate(hw, false);
	}

	rtldm->txpowercount = 0;
	RT_TRACE(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD, "end\n");

}

void rtl8723be_dm_check_txpower_tracking(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (!rtlpriv->dm.txpower_tracking)
		return;

	if (!rtlpriv->dm.tm_trigger) {
		rtl_set_rfreg(hw, RF90_PATH_A, RF_T_METER, BIT(17) | BIT(16),
			      0x03);
		RT_TRACE(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
			 "Trigger 8723be Thermal Meter!!\n");
		rtlpriv->dm.tm_trigger = 1;
		return;
	} else {
		RT_TRACE(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
			 "Schedule TxPowerTracking !!\n");
		rtl8723be_dm_txpower_tracking_callback_thermalmeter(hw);
		rtlpriv->dm.tm_trigger = 0;
	}
}

static void rtl8723be_dm_refresh_rate_adaptive_mask(struct ieee80211_hw *hw)
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
		RT_TRACE(rtlpriv, COMP_RATE, DBG_LOUD,
			 "driver is going to unload\n");
		return;
	}

	if (!rtlpriv->dm.useramask) {
		RT_TRACE(rtlpriv, COMP_RATE, DBG_LOUD,
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
			RT_TRACE(rtlpriv, COMP_RATE, DBG_LOUD,
				 "RSSI = %ld\n",
				 rtlpriv->dm.undec_sm_pwdb);
			RT_TRACE(rtlpriv, COMP_RATE, DBG_LOUD,
				 "RSSI_LEVEL = %d\n", p_ra->ratr_state);
			RT_TRACE(rtlpriv, COMP_RATE, DBG_LOUD,
				 "PreState = %d, CurState = %d\n",
				  p_ra->pre_ratr_state, p_ra->ratr_state);

			rcu_read_lock();
			sta = rtl_find_sta(hw, mac->bssid);
			if (sta)
				rtlpriv->cfg->ops->update_rate_tbl(hw, sta,
							   p_ra->ratr_state);
			rcu_read_unlock();

			p_ra->pre_ratr_state = p_ra->ratr_state;
		}
	}
}

static bool rtl8723be_dm_is_edca_turbo_disable(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (rtlpriv->mac80211.mode == WIRELESS_MODE_B)
		return true;

	return false;
}

static void rtl8723be_dm_check_edca_turbo(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));

	static u64 last_txok_cnt;
	static u64 last_rxok_cnt;
	u64 cur_txok_cnt = 0;
	u64 cur_rxok_cnt = 0;
	u32 edca_be_ul = 0x6ea42b;
	u32 edca_be_dl = 0x6ea42b;/*not sure*/
	u32 edca_be = 0x5ea42b;
	u32 iot_peer = 0;
	bool b_is_cur_rdlstate;
	bool b_last_is_cur_rdlstate = false;
	bool b_bias_on_rx = false;
	bool b_edca_turbo_on = false;

	b_last_is_cur_rdlstate = rtlpriv->dm.is_cur_rdlstate;

	cur_txok_cnt = rtlpriv->stats.txbytesunicast - last_txok_cnt;
	cur_rxok_cnt = rtlpriv->stats.rxbytesunicast - last_rxok_cnt;

	iot_peer = rtlpriv->mac80211.vendor;
	b_bias_on_rx = (iot_peer == PEER_RAL || iot_peer == PEER_ATH) ?
		       true : false;
	b_edca_turbo_on = ((!rtlpriv->dm.is_any_nonbepkts) &&
			   (!rtlpriv->dm.disable_framebursting)) ?
			   true : false;

	if ((iot_peer == PEER_CISCO) &&
	    (mac->mode == WIRELESS_MODE_N_24G)) {
		edca_be_dl = edca_setting_dl[iot_peer];
		edca_be_ul = edca_setting_ul[iot_peer];
	}
	if (rtl8723be_dm_is_edca_turbo_disable(hw))
		goto exit;

	if (b_edca_turbo_on) {
		if (b_bias_on_rx)
			b_is_cur_rdlstate = (cur_txok_cnt > cur_rxok_cnt * 4) ?
					    false : true;
		else
			b_is_cur_rdlstate = (cur_rxok_cnt > cur_txok_cnt * 4) ?
					    true : false;

		edca_be = (b_is_cur_rdlstate) ? edca_be_dl : edca_be_ul;
		rtl_write_dword(rtlpriv, REG_EDCA_BE_PARAM, edca_be);
		rtlpriv->dm.is_cur_rdlstate = b_is_cur_rdlstate;
		rtlpriv->dm.current_turbo_edca = true;
	} else {
		if (rtlpriv->dm.current_turbo_edca) {
			u8 tmp = AC0_BE;
			rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_AC_PARAM,
						      (u8 *)(&tmp));
		}
		rtlpriv->dm.current_turbo_edca = false;
	}

exit:
	rtlpriv->dm.is_any_nonbepkts = false;
	last_txok_cnt = rtlpriv->stats.txbytesunicast;
	last_rxok_cnt = rtlpriv->stats.rxbytesunicast;
}

static void rtl8723be_dm_cck_packet_detection_thresh(struct ieee80211_hw *hw)
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
		rtl_set_bbreg(hw, RCCK0_CCA, MASKBYTE2, cur_cck_cca_thresh);

	dm_digtable->pre_cck_cca_thres = dm_digtable->cur_cck_cca_thres;
	dm_digtable->cur_cck_cca_thres = cur_cck_cca_thresh;
	RT_TRACE(rtlpriv, COMP_DIG, DBG_TRACE,
		 "CCK cca thresh hold =%x\n", dm_digtable->cur_cck_cca_thres);
}

static void rtl8723be_dm_dynamic_edcca(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 reg_c50, reg_c58;
	bool fw_current_in_ps_mode = false;

	rtlpriv->cfg->ops->get_hw_reg(hw, HW_VAR_FW_PSMODE_STATUS,
				      (u8 *)(&fw_current_in_ps_mode));
	if (fw_current_in_ps_mode)
		return;

	reg_c50 = rtl_get_bbreg(hw, ROFDM0_XAAGCCORE1, MASKBYTE0);
	reg_c58 = rtl_get_bbreg(hw, ROFDM0_XBAGCCORE1, MASKBYTE0);

	if (reg_c50 > 0x28 && reg_c58 > 0x28) {
		if (!rtlpriv->rtlhal.pre_edcca_enable) {
			rtl_write_byte(rtlpriv, ROFDM0_ECCATHRESHOLD, 0x03);
			rtl_write_byte(rtlpriv, ROFDM0_ECCATHRESHOLD + 2, 0x00);
		}
	} else if (reg_c50 < 0x25 && reg_c58 < 0x25) {
		if (rtlpriv->rtlhal.pre_edcca_enable) {
			rtl_write_byte(rtlpriv, ROFDM0_ECCATHRESHOLD, 0x7f);
			rtl_write_byte(rtlpriv, ROFDM0_ECCATHRESHOLD + 2, 0x7f);
		}
	}
}

static void rtl8723be_dm_dynamic_atc_switch(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_dm *rtldm = rtl_dm(rtl_priv(hw));
	u8 crystal_cap;
	u32 packet_count;
	int cfo_khz_a, cfo_khz_b, cfo_ave = 0, adjust_xtal = 0;
	int cfo_ave_diff;

	if (rtlpriv->mac80211.link_state < MAC80211_LINKED) {
		if (rtldm->atc_status == ATC_STATUS_OFF) {
			rtl_set_bbreg(hw, ROFDM1_CFOTRACKING, BIT(11),
				      ATC_STATUS_ON);
			rtldm->atc_status = ATC_STATUS_ON;
		}
		if (rtlpriv->cfg->ops->get_btc_status()) {
			if (!rtlpriv->btcoexist.btc_ops->btc_is_bt_disabled(rtlpriv)) {
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "odm_DynamicATCSwitch(): Disable CFO tracking for BT!!\n");
				return;
			}
		}

		if (rtldm->crystal_cap != rtlpriv->efuse.crystalcap) {
			rtldm->crystal_cap = rtlpriv->efuse.crystalcap;
			crystal_cap = rtldm->crystal_cap & 0x3f;
			rtl_set_bbreg(hw, REG_MAC_PHY_CTRL, 0xFFF000,
				      (crystal_cap | (crystal_cap << 6)));
		}
	} else {
		cfo_khz_a = (int)(rtldm->cfo_tail[0] * 3125) / 1280;
		cfo_khz_b = (int)(rtldm->cfo_tail[1] * 3125) / 1280;
		packet_count = rtldm->packet_count;

		if (packet_count == rtldm->packet_count_pre)
			return;

		rtldm->packet_count_pre = packet_count;

		if (rtlpriv->phy.rf_type == RF_1T1R)
			cfo_ave = cfo_khz_a;
		else
			cfo_ave = (int)(cfo_khz_a + cfo_khz_b) >> 1;

		cfo_ave_diff = (rtldm->cfo_ave_pre >= cfo_ave) ?
			       (rtldm->cfo_ave_pre - cfo_ave) :
			       (cfo_ave - rtldm->cfo_ave_pre);

		if (cfo_ave_diff > 20 && rtldm->large_cfo_hit == 0) {
			rtldm->large_cfo_hit = 1;
			return;
		} else
			rtldm->large_cfo_hit = 0;

		rtldm->cfo_ave_pre = cfo_ave;

		if (cfo_ave >= -rtldm->cfo_threshold &&
		    cfo_ave <= rtldm->cfo_threshold && rtldm->is_freeze == 0) {
			if (rtldm->cfo_threshold == CFO_THRESHOLD_XTAL) {
				rtldm->cfo_threshold = CFO_THRESHOLD_XTAL + 10;
				rtldm->is_freeze = 1;
			} else {
				rtldm->cfo_threshold = CFO_THRESHOLD_XTAL;
			}
		}

		if (cfo_ave > rtldm->cfo_threshold && rtldm->crystal_cap < 0x3f)
			adjust_xtal = ((cfo_ave - CFO_THRESHOLD_XTAL) >> 1) + 1;
		else if ((cfo_ave < -rtlpriv->dm.cfo_threshold) &&
					rtlpriv->dm.crystal_cap > 0)
			adjust_xtal = ((cfo_ave + CFO_THRESHOLD_XTAL) >> 1) - 1;

		if (adjust_xtal != 0) {
			rtldm->is_freeze = 0;
			rtldm->crystal_cap += adjust_xtal;

			if (rtldm->crystal_cap > 0x3f)
				rtldm->crystal_cap = 0x3f;
			else if (rtldm->crystal_cap < 0)
				rtldm->crystal_cap = 0;

			crystal_cap = rtldm->crystal_cap & 0x3f;
			rtl_set_bbreg(hw, REG_MAC_PHY_CTRL, 0xFFF000,
				      (crystal_cap | (crystal_cap << 6)));
		}

		if (cfo_ave < CFO_THRESHOLD_ATC &&
		    cfo_ave > -CFO_THRESHOLD_ATC) {
			if (rtldm->atc_status == ATC_STATUS_ON) {
				rtl_set_bbreg(hw, ROFDM1_CFOTRACKING, BIT(11),
					      ATC_STATUS_OFF);
				rtldm->atc_status = ATC_STATUS_OFF;
			}
		} else {
			if (rtldm->atc_status == ATC_STATUS_OFF) {
				rtl_set_bbreg(hw, ROFDM1_CFOTRACKING, BIT(11),
					      ATC_STATUS_ON);
				rtldm->atc_status = ATC_STATUS_ON;
			}
		}
	}
}

static void rtl8723be_dm_common_info_self_update(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 cnt = 0;
	struct rtl_sta_info *drv_priv;

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
		list_for_each_entry(drv_priv, &rtlpriv->entry_list, list) {
			cnt++;
		}
		spin_unlock_bh(&rtlpriv->locks.entry_list_lock);

		if (cnt == 1)
			rtlpriv->dm.one_entry_only = true;
	}
}

void rtl8723be_dm_watchdog(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
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
		rtl8723be_dm_common_info_self_update(hw);
		rtl8723be_dm_false_alarm_counter_statistics(hw);
		rtl8723be_dm_check_rssi_monitor(hw);
		rtl8723be_dm_dig(hw);
		rtl8723be_dm_dynamic_edcca(hw);
		rtl8723be_dm_cck_packet_detection_thresh(hw);
		rtl8723be_dm_refresh_rate_adaptive_mask(hw);
		rtl8723be_dm_check_edca_turbo(hw);
		rtl8723be_dm_dynamic_atc_switch(hw);
		rtl8723be_dm_check_txpower_tracking(hw);
		rtl8723be_dm_dynamic_txpower(hw);
	}
	spin_unlock(&rtlpriv->locks.rf_ps_lock);
	rtlpriv->dm.dbginfo.num_qry_beacon_pkt = 0;
}
