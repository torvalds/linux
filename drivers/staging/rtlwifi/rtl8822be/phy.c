// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2016  Realtek Corporation.
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
#include "../pci.h"
#include "../ps.h"
#include "../base.h"
#include "reg.h"
#include "def.h"
#include "phy.h"
#include "trx.h"
#include "../btcoexist/halbt_precomp.h"
#include "hw.h"
#include "../efuse.h"

static u32 _rtl8822be_phy_calculate_bit_shift(u32 bitmask);
static void
_rtl8822be_phy_init_bb_rf_register_definition(struct ieee80211_hw *hw);

static long _rtl8822be_phy_txpwr_idx_to_dbm(struct ieee80211_hw *hw,
					    enum wireless_mode wirelessmode,
					    u8 txpwridx);
static void rtl8822be_phy_set_rf_on(struct ieee80211_hw *hw);
static void rtl8822be_phy_set_io(struct ieee80211_hw *hw);

static u8 cck_rates[] = {DESC_RATE1M, DESC_RATE2M, DESC_RATE5_5M, DESC_RATE11M};
static u8 sizes_of_cck_retes = 4;
static u8 ofdm_rates[] = {DESC_RATE6M,  DESC_RATE9M,  DESC_RATE12M,
			  DESC_RATE18M, DESC_RATE24M, DESC_RATE36M,
			  DESC_RATE48M, DESC_RATE54M};
static u8 sizes_of_ofdm_retes = 8;
static u8 ht_rates_1t[] = {DESC_RATEMCS0, DESC_RATEMCS1, DESC_RATEMCS2,
			   DESC_RATEMCS3, DESC_RATEMCS4, DESC_RATEMCS5,
			   DESC_RATEMCS6, DESC_RATEMCS7};
static u8 sizes_of_ht_retes_1t = 8;
static u8 ht_rates_2t[] = {DESC_RATEMCS8,  DESC_RATEMCS9,  DESC_RATEMCS10,
			   DESC_RATEMCS11, DESC_RATEMCS12, DESC_RATEMCS13,
			   DESC_RATEMCS14, DESC_RATEMCS15};
static u8 sizes_of_ht_retes_2t = 8;
static u8 vht_rates_1t[] = {DESC_RATEVHT1SS_MCS0, DESC_RATEVHT1SS_MCS1,
			    DESC_RATEVHT1SS_MCS2, DESC_RATEVHT1SS_MCS3,
			    DESC_RATEVHT1SS_MCS4, DESC_RATEVHT1SS_MCS5,
			    DESC_RATEVHT1SS_MCS6, DESC_RATEVHT1SS_MCS7,
			    DESC_RATEVHT1SS_MCS8, DESC_RATEVHT1SS_MCS9};
static u8 vht_rates_2t[] = {DESC_RATEVHT2SS_MCS0, DESC_RATEVHT2SS_MCS1,
			    DESC_RATEVHT2SS_MCS2, DESC_RATEVHT2SS_MCS3,
			    DESC_RATEVHT2SS_MCS4, DESC_RATEVHT2SS_MCS5,
			    DESC_RATEVHT2SS_MCS6, DESC_RATEVHT2SS_MCS7,
			    DESC_RATEVHT2SS_MCS8, DESC_RATEVHT2SS_MCS9};
static u8 sizes_of_vht_retes = 10;

u32 rtl8822be_phy_query_bb_reg(struct ieee80211_hw *hw, u32 regaddr,
			       u32 bitmask)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 returnvalue, originalvalue, bitshift;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE, "regaddr(%#x), bitmask(%#x)\n",
		 regaddr, bitmask);
	originalvalue = rtl_read_dword(rtlpriv, regaddr);
	bitshift = _rtl8822be_phy_calculate_bit_shift(bitmask);
	returnvalue = (originalvalue & bitmask) >> bitshift;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE, "BBR MASK=0x%x Addr[0x%x]=0x%x\n",
		 bitmask, regaddr, originalvalue);

	return returnvalue;
}

void rtl8822be_phy_set_bb_reg(struct ieee80211_hw *hw, u32 regaddr, u32 bitmask,
			      u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 originalvalue, bitshift;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), bitmask(%#x), data(%#x)\n", regaddr, bitmask,
		 data);

	if (bitmask != MASKDWORD) {
		originalvalue = rtl_read_dword(rtlpriv, regaddr);
		bitshift = _rtl8822be_phy_calculate_bit_shift(bitmask);
		data = ((originalvalue & (~bitmask)) |
			((data << bitshift) & bitmask));
	}

	rtl_write_dword(rtlpriv, regaddr, data);

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), bitmask(%#x), data(%#x)\n", regaddr, bitmask,
		 data);
}

u32 rtl8822be_phy_query_rf_reg(struct ieee80211_hw *hw, enum radio_path rfpath,
			       u32 regaddr, u32 bitmask)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 /*original_value,*/ readback_value /*, bitshift*/;
	unsigned long flags;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), rfpath(%#x), bitmask(%#x)\n", regaddr, rfpath,
		 bitmask);

	spin_lock_irqsave(&rtlpriv->locks.rf_lock, flags);

	readback_value = rtlpriv->phydm.ops->phydm_read_rf_reg(
		rtlpriv, rfpath, regaddr, bitmask);

	spin_unlock_irqrestore(&rtlpriv->locks.rf_lock, flags);

	return readback_value;
}

void rtl8822be_phy_set_rf_reg(struct ieee80211_hw *hw, enum radio_path rfpath,
			      u32 regaddr, u32 bitmask, u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	unsigned long flags;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), bitmask(%#x), data(%#x), rfpath(%#x)\n",
		 regaddr, bitmask, data, rfpath);

	spin_lock_irqsave(&rtlpriv->locks.rf_lock, flags);

	rtlpriv->phydm.ops->phydm_write_rf_reg(rtlpriv, rfpath, regaddr,
					       bitmask, data);

	spin_unlock_irqrestore(&rtlpriv->locks.rf_lock, flags);

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), bitmask(%#x), data(%#x), rfpath(%#x)\n",
		 regaddr, bitmask, data, rfpath);
}

static u32 _rtl8822be_phy_calculate_bit_shift(u32 bitmask)
{
	u32 i;

	for (i = 0; i <= 31; i++) {
		if (((bitmask >> i) & 0x1) == 1)
			break;
	}
	return i;
}

bool rtl8822be_halmac_cb_init_mac_register(struct rtl_priv *rtlpriv)
{
	return rtlpriv->phydm.ops->phydm_phy_mac_config(rtlpriv);
}

bool rtl8822be_phy_bb_config(struct ieee80211_hw *hw)
{
	bool rtstatus = true;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u8 crystal_cap;
	/* u32 tmp; */

	rtstatus = rtlpriv->phydm.ops->phydm_phy_bb_config(rtlpriv);

	/* write 0x28[6:1] = 0x24[30:25] = CrystalCap */
	crystal_cap = rtlefuse->crystalcap & 0x3F;
	rtl_set_bbreg(hw, REG_AFE_XTAL_CTRL_8822B, 0x7E000000, crystal_cap);
	rtl_set_bbreg(hw, REG_AFE_PLL_CTRL_8822B, 0x7E, crystal_cap);

	/*rtlphy->reg_837 = rtl_read_byte(rtlpriv, 0x837);*/ /*unused*/

	return rtstatus;
}

bool rtl8822be_phy_rf_config(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;

	if (rtlphy->rf_type == RF_1T1R)
		rtlphy->num_total_rfpath = 1;
	else
		rtlphy->num_total_rfpath = 2;

	return rtlpriv->phydm.ops->phydm_phy_rf_config(rtlpriv);
}

bool rtl8822be_halmac_cb_init_bb_rf_register(struct rtl_priv *rtlpriv)
{
	struct ieee80211_hw *hw = rtlpriv->hw;
	enum radio_mask txpath, rxpath;
	bool tx2path;
	bool ret = false;

	_rtl8822be_phy_init_bb_rf_register_definition(hw);

	rtlpriv->halmac.ops->halmac_phy_power_switch(rtlpriv, 1);

	/* beofre bb/rf config */
	rtlpriv->phydm.ops->phydm_parameter_init(rtlpriv, 0);

	/* do bb/rf config */
	if (rtl8822be_phy_bb_config(hw) && rtl8822be_phy_rf_config(hw))
		ret = true;

	/* after bb/rf config */
	rtlpriv->phydm.ops->phydm_parameter_init(rtlpriv, 1);

	/* set trx mode (keep it to be last, r17376) */
	txpath = RF_MASK_A | RF_MASK_B;
	rxpath = RF_MASK_A | RF_MASK_B;
	tx2path = false;
	return rtlpriv->phydm.ops->phydm_trx_mode(rtlpriv, txpath, rxpath,
						 tx2path);
}

static void _rtl8822be_phy_init_tx_power_by_rate(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;

	u8 band, rfpath, txnum, rate;

	for (band = BAND_ON_2_4G; band <= BAND_ON_5G; ++band)
		for (rfpath = 0; rfpath < TX_PWR_BY_RATE_NUM_RF; ++rfpath)
			for (txnum = 0; txnum < TX_PWR_BY_RATE_NUM_RF; ++txnum)
				for (rate = 0; rate < TX_PWR_BY_RATE_NUM_RATE;
				     ++rate)
					rtlphy->tx_power_by_rate_offset
						[band][rfpath][txnum][rate] = 0;
}

static void _rtl8822be_phy_set_txpower_by_rate_base(struct ieee80211_hw *hw,
						    u8 band, u8 path,
						    u8 rate_section, u8 txnum,
						    u8 value)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;

	if (path > RF90_PATH_D) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Invalid Rf Path %d in phy_SetTxPowerByRatBase()\n",
			 path);
		return;
	}

	if (band != BAND_ON_2_4G && band != BAND_ON_5G) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Invalid band %d in phy_SetTxPowerByRatBase()\n",
			 band);
		return;
	}

	if (rate_section >= MAX_RATE_SECTION ||
	    (band == BAND_ON_5G && rate_section == CCK)) {
		RT_TRACE(
			rtlpriv, COMP_INIT, DBG_LOUD,
			"Invalid rate_section %d in phy_SetTxPowerByRatBase()\n",
			rate_section);
		return;
	}

	if (band == BAND_ON_2_4G)
		rtlphy->txpwr_by_rate_base_24g[path][txnum][rate_section] =
			value;
	else /* BAND_ON_5G */
		rtlphy->txpwr_by_rate_base_5g[path][txnum][rate_section - 1] =
			value;
}

static u8 _rtl8822be_phy_get_txpower_by_rate_base(struct ieee80211_hw *hw,
						  u8 band, u8 path, u8 txnum,
						  u8 rate_section)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 value;

	if (path > RF90_PATH_D) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Invalid Rf Path %d in phy_GetTxPowerByRatBase()\n",
			 path);
		return 0;
	}

	if (band != BAND_ON_2_4G && band != BAND_ON_5G) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Invalid band %d in phy_GetTxPowerByRatBase()\n",
			 band);
		return 0;
	}

	if (rate_section >= MAX_RATE_SECTION ||
	    (band == BAND_ON_5G && rate_section == CCK)) {
		RT_TRACE(
			rtlpriv, COMP_INIT, DBG_LOUD,
			"Invalid rate_section %d in phy_GetTxPowerByRatBase()\n",
			rate_section);
		return 0;
	}

	if (band == BAND_ON_2_4G)
		value = rtlphy->txpwr_by_rate_base_24g[path][txnum]
						      [rate_section];
	else /* BAND_ON_5G */
		value = rtlphy->txpwr_by_rate_base_5g[path][txnum]
						     [rate_section - 1];

	return value;
}

static void _rtl8822be_phy_store_txpower_by_rate_base(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;

	struct {
		enum rtl_desc_rate rate;
		enum rate_section section;
	} rate_sec_base[] = {
		{DESC_RATE11M, CCK},
		{DESC_RATE54M, OFDM},
		{DESC_RATEMCS7, HT_MCS0_MCS7},
		{DESC_RATEMCS15, HT_MCS8_MCS15},
		{DESC_RATEVHT1SS_MCS7, VHT_1SSMCS0_1SSMCS9},
		{DESC_RATEVHT2SS_MCS7, VHT_2SSMCS0_2SSMCS9},
	};

	u8 band, path, rs, tx_num, base;
	u8 rate, section;

	for (band = BAND_ON_2_4G; band <= BAND_ON_5G; band++) {
		for (path = RF90_PATH_A; path <= RF90_PATH_B; path++) {
			for (rs = 0; rs < MAX_RATE_SECTION; rs++) {
				rate = rate_sec_base[rs].rate;
				section = rate_sec_base[rs].section;

				if (IS_1T_RATE(rate))
					tx_num = RF_1TX;
				else
					tx_num = RF_2TX;

				if (band == BAND_ON_5G &&
				    RX_HAL_IS_CCK_RATE(rate))
					continue;

				base = rtlphy->tx_power_by_rate_offset
					       [band][path][tx_num][rate];
				_rtl8822be_phy_set_txpower_by_rate_base(
					hw, band, path, section, tx_num, base);
			}
		}
	}
}

static void __rtl8822be_phy_cross_reference_core(struct ieee80211_hw *hw,
						 u8 regulation, u8 bw,
						 u8 channel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 rs, ref_rs;
	s8 pwrlmt, ref_pwrlmt;

	for (rs = 0; rs < MAX_RATE_SECTION_NUM; ++rs) {
		/*5G 20M 40M VHT and HT can cross reference*/
		if (bw != HT_CHANNEL_WIDTH_20 && bw != HT_CHANNEL_WIDTH_20_40)
			continue;

		if (rs == HT_MCS0_MCS7)
			ref_rs = VHT_1SSMCS0_1SSMCS9;
		else if (rs == HT_MCS8_MCS15)
			ref_rs = VHT_2SSMCS0_2SSMCS9;
		else if (rs == VHT_1SSMCS0_1SSMCS9)
			ref_rs = HT_MCS0_MCS7;
		else if (rs == VHT_2SSMCS0_2SSMCS9)
			ref_rs = HT_MCS8_MCS15;
		else
			continue;

		ref_pwrlmt = rtlphy->txpwr_limit_5g[regulation][bw][ref_rs]
						   [channel][RF90_PATH_A];
		if (ref_pwrlmt == MAX_POWER_INDEX)
			continue;

		pwrlmt = rtlphy->txpwr_limit_5g[regulation][bw][rs][channel]
					       [RF90_PATH_A];
		if (pwrlmt != MAX_POWER_INDEX)
			continue;

		rtlphy->txpwr_limit_5g[regulation][bw][rs][channel]
				      [RF90_PATH_A] = ref_pwrlmt;
	}
}

static void
_rtl8822be_phy_cross_reference_ht_and_vht_txpower_limit(struct ieee80211_hw *hw)
{
	u8 regulation, bw, channel;

	for (regulation = 0; regulation < MAX_REGULATION_NUM; ++regulation) {
		for (bw = 0; bw < MAX_5G_BANDWIDTH_NUM; ++bw) {
			for (channel = 0; channel < CHANNEL_MAX_NUMBER_5G;
			     ++channel) {
				__rtl8822be_phy_cross_reference_core(
					hw, regulation, bw, channel);
			}
		}
	}
}

static void __rtl8822be_txpwr_limit_to_index_2g(struct ieee80211_hw *hw,
						u8 regulation, u8 bw,
						u8 channel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 bw40_pwr_base_dbm2_4G;
	u8 rate_section;
	s8 temp_pwrlmt;
	enum rf_tx_num txnum;
	s8 temp_value;
	u8 rf_path;

	for (rate_section = 0; rate_section < MAX_RATE_SECTION_NUM;
	     ++rate_section) {
		/* obtain the base dBm values in 2.4G band
		 * CCK => 11M, OFDM => 54M, HT 1T => MCS7, HT 2T => MCS15
		 */

		temp_pwrlmt =
			rtlphy->txpwr_limit_2_4g[regulation][bw][rate_section]
						[channel][RF90_PATH_A];
		txnum = IS_1T_RATESEC(rate_section) ? RF_1TX : RF_2TX;

		if (temp_pwrlmt == MAX_POWER_INDEX)
			continue;

		for (rf_path = RF90_PATH_A; rf_path < MAX_RF_PATH_NUM;
		     ++rf_path) {
			bw40_pwr_base_dbm2_4G =
				_rtl8822be_phy_get_txpower_by_rate_base(
					hw, BAND_ON_2_4G, rf_path, txnum,
					rate_section);

			temp_value = temp_pwrlmt - bw40_pwr_base_dbm2_4G;
			rtlphy->txpwr_limit_2_4g[regulation][bw][rate_section]
						[channel][rf_path] = temp_value;

			RT_TRACE(
				rtlpriv, COMP_INIT, DBG_TRACE,
				"TxPwrLimit_2_4G[regulation %d][bw %d][rateSection %d][channel %d] = %d\n(TxPwrLimit in dBm %d - BW40PwrLmt2_4G[channel %d][rfPath %d] %d)\n",
				regulation, bw, rate_section, channel,
				rtlphy->txpwr_limit_2_4g[regulation][bw]
							[rate_section][channel]
							[rf_path],
				(temp_pwrlmt == 63) ? 0 : temp_pwrlmt / 2,
				channel, rf_path, bw40_pwr_base_dbm2_4G);
		}
	}
}

static void __rtl8822be_txpwr_limit_to_index_5g(struct ieee80211_hw *hw,
						u8 regulation, u8 bw,
						u8 channel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 bw40_pwr_base_dbm5G;
	u8 rate_section;
	s8 temp_pwrlmt;
	enum rf_tx_num txnum;
	s8 temp_value;
	u8 rf_path;

	for (rate_section = 0; rate_section < MAX_RATE_SECTION_NUM;
	     ++rate_section) {
		/* obtain the base dBm values in 5G band
		 * OFDM => 54M, HT 1T => MCS7, HT 2T => MCS15,
		 * VHT => 1SSMCS7, VHT 2T => 2SSMCS7
		 */

		temp_pwrlmt =
			rtlphy->txpwr_limit_5g[regulation][bw][rate_section]
					      [channel][RF90_PATH_A];
		txnum = IS_1T_RATESEC(rate_section) ? RF_1TX : RF_2TX;

		if (temp_pwrlmt == MAX_POWER_INDEX)
			continue;

		for (rf_path = RF90_PATH_A; rf_path < MAX_RF_PATH_NUM;
		     ++rf_path) {
			bw40_pwr_base_dbm5G =
				_rtl8822be_phy_get_txpower_by_rate_base(
					hw, BAND_ON_5G, rf_path, txnum,
					rate_section);

			temp_value = temp_pwrlmt - bw40_pwr_base_dbm5G;
			rtlphy->txpwr_limit_5g[regulation][bw][rate_section]
					      [channel][rf_path] = temp_value;

			RT_TRACE(
				rtlpriv, COMP_INIT, DBG_TRACE,
				"TxPwrLimit_5G[regulation %d][bw %d][rateSection %d][channel %d] =%d\n(TxPwrLimit in dBm %d - BW40PwrLmt5G[chnl group %d][rfPath %d] %d)\n",
				regulation, bw, rate_section, channel,
				rtlphy->txpwr_limit_5g[regulation][bw]
						      [rate_section][channel]
						      [rf_path],
				temp_pwrlmt, channel, rf_path,
				bw40_pwr_base_dbm5G);
		}
	}
}

static void
_rtl8822be_phy_convert_txpower_limit_to_power_index(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 regulation, bw, channel;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE, "=====> %s()\n", __func__);

	_rtl8822be_phy_cross_reference_ht_and_vht_txpower_limit(hw);

	for (regulation = 0; regulation < MAX_REGULATION_NUM; ++regulation) {
		for (bw = 0; bw < MAX_2_4G_BANDWIDTH_NUM; ++bw) {
			for (channel = 0; channel < CHANNEL_MAX_NUMBER_2G;
			     ++channel) {
				__rtl8822be_txpwr_limit_to_index_2g(
					hw, regulation, bw, channel);
			}
		}
	}

	for (regulation = 0; regulation < MAX_REGULATION_NUM; ++regulation) {
		for (bw = 0; bw < MAX_5G_BANDWIDTH_NUM; ++bw) {
			for (channel = 0; channel < CHANNEL_MAX_NUMBER_5G;
			     ++channel) {
				__rtl8822be_txpwr_limit_to_index_5g(
					hw, regulation, bw, channel);
			}
		}
	}
	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE, "<===== %s()\n", __func__);
}

static void _rtl8822be_phy_init_txpower_limit(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 i, j, k, l, m;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "=====> %s()!\n", __func__);

	for (i = 0; i < MAX_REGULATION_NUM; ++i) {
		for (j = 0; j < MAX_2_4G_BANDWIDTH_NUM; ++j)
			for (k = 0; k < MAX_RATE_SECTION_NUM; ++k)
				for (m = 0; m < CHANNEL_MAX_NUMBER_2G; ++m)
					for (l = 0; l < MAX_RF_PATH_NUM; ++l)
						rtlphy->txpwr_limit_2_4g[i][j]
									[k][m]
									[l] =
							MAX_POWER_INDEX;
	}
	for (i = 0; i < MAX_REGULATION_NUM; ++i) {
		for (j = 0; j < MAX_5G_BANDWIDTH_NUM; ++j)
			for (k = 0; k < MAX_RATE_SECTION_NUM; ++k)
				for (m = 0; m < CHANNEL_MAX_NUMBER_5G; ++m)
					for (l = 0; l < MAX_RF_PATH_NUM; ++l)
						rtlphy->txpwr_limit_5g[i][j][k]
								      [m][l] =
							MAX_POWER_INDEX;
	}

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "<===== %s()!\n", __func__);
}

static void
_rtl8822be_phy_convert_txpower_dbm_to_relative_value(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;

	u8 base = 0, i = 0, value = 0, band = 0, path = 0, txnum = 0;

	for (band = BAND_ON_2_4G; band <= BAND_ON_5G; ++band) {
		for (path = RF90_PATH_A; path <= RF90_PATH_B; ++path) {
			for (txnum = RF_1TX; txnum <= RF_2TX; ++txnum) {
				/* CCK */
				base = rtlphy->tx_power_by_rate_offset
					       [band][path][txnum]
					       [DESC_RATE11M];
				for (i = 0; i < sizeof(cck_rates); ++i) {
					value = rtlphy->tx_power_by_rate_offset
							[band][path][txnum]
							[cck_rates[i]];
					rtlphy->tx_power_by_rate_offset
						[band][path][txnum]
						[cck_rates[i]] = value - base;
				}

				/* OFDM */
				base = rtlphy->tx_power_by_rate_offset
					       [band][path][txnum]
					       [DESC_RATE54M];
				for (i = 0; i < sizeof(ofdm_rates); ++i) {
					value = rtlphy->tx_power_by_rate_offset
							[band][path][txnum]
							[ofdm_rates[i]];
					rtlphy->tx_power_by_rate_offset
						[band][path][txnum]
						[ofdm_rates[i]] = value - base;
				}

				/* HT MCS0~7 */
				base = rtlphy->tx_power_by_rate_offset
					       [band][path][txnum]
					       [DESC_RATEMCS7];
				for (i = 0; i < sizeof(ht_rates_1t); ++i) {
					value = rtlphy->tx_power_by_rate_offset
							[band][path][txnum]
							[ht_rates_1t[i]];
					rtlphy->tx_power_by_rate_offset
						[band][path][txnum]
						[ht_rates_1t[i]] = value - base;
				}

				/* HT MCS8~15 */
				base = rtlphy->tx_power_by_rate_offset
					       [band][path][txnum]
					       [DESC_RATEMCS15];
				for (i = 0; i < sizeof(ht_rates_2t); ++i) {
					value = rtlphy->tx_power_by_rate_offset
							[band][path][txnum]
							[ht_rates_2t[i]];
					rtlphy->tx_power_by_rate_offset
						[band][path][txnum]
						[ht_rates_2t[i]] = value - base;
				}

				/* VHT 1SS */
				base = rtlphy->tx_power_by_rate_offset
					       [band][path][txnum]
					       [DESC_RATEVHT1SS_MCS7];
				for (i = 0; i < sizeof(vht_rates_1t); ++i) {
					value = rtlphy->tx_power_by_rate_offset
							[band][path][txnum]
							[vht_rates_1t[i]];
					rtlphy->tx_power_by_rate_offset
						[band][path][txnum]
						[vht_rates_1t[i]] =
						value - base;
				}

				/* VHT 2SS */
				base = rtlphy->tx_power_by_rate_offset
					       [band][path][txnum]
					       [DESC_RATEVHT2SS_MCS7];
				for (i = 0; i < sizeof(vht_rates_2t); ++i) {
					value = rtlphy->tx_power_by_rate_offset
							[band][path][txnum]
							[vht_rates_2t[i]];
					rtlphy->tx_power_by_rate_offset
						[band][path][txnum]
						[vht_rates_2t[i]] =
						value - base;
				}
			}
		}
	}

	RT_TRACE(rtlpriv, COMP_POWER, DBG_TRACE, "<===%s()\n", __func__);
}

static void
_rtl8822be_phy_txpower_by_rate_configuration(struct ieee80211_hw *hw)
{
	/* copy rate_section from
	 * tx_power_by_rate_offset[][rate] to txpwr_by_rate_base_24g/_5g[][rs]
	 */
	_rtl8822be_phy_store_txpower_by_rate_base(hw);

	/* convert tx_power_by_rate_offset[] to relative value */
	_rtl8822be_phy_convert_txpower_dbm_to_relative_value(hw);
}

/* string is in decimal */
static bool _rtl8822be_get_integer_from_string(char *str, u8 *pint)
{
	u16 i = 0;
	*pint = 0;

	while (str[i] != '\0') {
		if (str[i] >= '0' && str[i] <= '9') {
			*pint *= 10;
			*pint += (str[i] - '0');
		} else {
			return false;
		}
		++i;
	}

	return true;
}

static bool _rtl8822be_eq_n_byte(u8 *str1, u8 *str2, u32 num)
{
	if (num == 0)
		return false;
	while (num > 0) {
		num--;
		if (str1[num] != str2[num])
			return false;
	}
	return true;
}

static char _rtl8822be_phy_get_chnl_idx_of_txpwr_lmt(struct ieee80211_hw *hw,
						     u8 band, u8 channel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	char channel_index = -1;
	u8 i = 0;

	if (band == BAND_ON_2_4G) {
		channel_index = channel - 1;
	} else if (band == BAND_ON_5G) {
		for (i = 0; i < sizeof(rtl_channel5g) / sizeof(u8); ++i) {
			if (rtl_channel5g[i] == channel)
				channel_index = i;
		}
	} else {
		RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD, "Invalid Band %d in %s",
			 band, __func__);
	}

	if (channel_index == -1)
		RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
			 "Invalid Channel %d of Band %d in %s", channel, band,
			 __func__);

	return channel_index;
}

void rtl8822be_phy_set_txpower_limit(struct ieee80211_hw *hw, u8 *pregulation,
				     u8 *pband, u8 *pbandwidth,
				     u8 *prate_section, u8 *prf_path,
				     u8 *pchannel, u8 *ppower_limit)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 regulation = 0, bandwidth = 0, rate_section = 0, channel;
	u8 channel_index;
	char power_limit = 0, prev_power_limit, ret;

	if (!_rtl8822be_get_integer_from_string((char *)pchannel, &channel) ||
	    !_rtl8822be_get_integer_from_string((char *)ppower_limit,
						&power_limit)) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "Illegal index of pwr_lmt table [chnl %d][val %d]\n",
			 channel, power_limit);
	}

	power_limit =
		power_limit > MAX_POWER_INDEX ? MAX_POWER_INDEX : power_limit;

	if (_rtl8822be_eq_n_byte(pregulation, (u8 *)("FCC"), 3))
		regulation = 0;
	else if (_rtl8822be_eq_n_byte(pregulation, (u8 *)("MKK"), 3))
		regulation = 1;
	else if (_rtl8822be_eq_n_byte(pregulation, (u8 *)("ETSI"), 4))
		regulation = 2;
	else if (_rtl8822be_eq_n_byte(pregulation, (u8 *)("WW13"), 4))
		regulation = 3;

	if (_rtl8822be_eq_n_byte(prate_section, (u8 *)("CCK"), 3))
		rate_section = CCK;
	else if (_rtl8822be_eq_n_byte(prate_section, (u8 *)("OFDM"), 4))
		rate_section = OFDM;
	else if (_rtl8822be_eq_n_byte(prate_section, (u8 *)("HT"), 2) &&
		 _rtl8822be_eq_n_byte(prf_path, (u8 *)("1T"), 2))
		rate_section = HT_MCS0_MCS7;
	else if (_rtl8822be_eq_n_byte(prate_section, (u8 *)("HT"), 2) &&
		 _rtl8822be_eq_n_byte(prf_path, (u8 *)("2T"), 2))
		rate_section = HT_MCS8_MCS15;
	else if (_rtl8822be_eq_n_byte(prate_section, (u8 *)("VHT"), 3) &&
		 _rtl8822be_eq_n_byte(prf_path, (u8 *)("1T"), 2))
		rate_section = VHT_1SSMCS0_1SSMCS9;
	else if (_rtl8822be_eq_n_byte(prate_section, (u8 *)("VHT"), 3) &&
		 _rtl8822be_eq_n_byte(prf_path, (u8 *)("2T"), 2))
		rate_section = VHT_2SSMCS0_2SSMCS9;

	if (_rtl8822be_eq_n_byte(pbandwidth, (u8 *)("20M"), 3))
		bandwidth = HT_CHANNEL_WIDTH_20;
	else if (_rtl8822be_eq_n_byte(pbandwidth, (u8 *)("40M"), 3))
		bandwidth = HT_CHANNEL_WIDTH_20_40;
	else if (_rtl8822be_eq_n_byte(pbandwidth, (u8 *)("80M"), 3))
		bandwidth = HT_CHANNEL_WIDTH_80;
	else if (_rtl8822be_eq_n_byte(pbandwidth, (u8 *)("160M"), 4))
		bandwidth = 3;

	if (_rtl8822be_eq_n_byte(pband, (u8 *)("2.4G"), 4)) {
		ret = _rtl8822be_phy_get_chnl_idx_of_txpwr_lmt(hw, BAND_ON_2_4G,
							       channel);

		if (ret == -1)
			return;

		channel_index = ret;

		prev_power_limit =
			rtlphy->txpwr_limit_2_4g[regulation][bandwidth]
						[rate_section][channel_index]
						[RF90_PATH_A];

		if (power_limit < prev_power_limit)
			rtlphy->txpwr_limit_2_4g[regulation][bandwidth]
						[rate_section][channel_index]
						[RF90_PATH_A] = power_limit;

		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "2.4G [regula %d][bw %d][sec %d][chnl %d][val %d]\n",
			 regulation, bandwidth, rate_section, channel_index,
			 rtlphy->txpwr_limit_2_4g[regulation][bandwidth]
						 [rate_section][channel_index]
						 [RF90_PATH_A]);
	} else if (_rtl8822be_eq_n_byte(pband, (u8 *)("5G"), 2)) {
		ret = _rtl8822be_phy_get_chnl_idx_of_txpwr_lmt(hw, BAND_ON_5G,
							       channel);

		if (ret == -1)
			return;

		channel_index = ret;

		prev_power_limit =
			rtlphy->txpwr_limit_5g[regulation][bandwidth]
					      [rate_section][channel_index]
					      [RF90_PATH_A];

		if (power_limit < prev_power_limit)
			rtlphy->txpwr_limit_5g[regulation][bandwidth]
					      [rate_section][channel_index]
					      [RF90_PATH_A] = power_limit;

		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "5G: [regul %d][bw %d][sec %d][chnl %d][val %d]\n",
			 regulation, bandwidth, rate_section, channel,
			 rtlphy->txpwr_limit_5g[regulation][bandwidth]
					       [rate_section][channel_index]
					       [RF90_PATH_A]);

	} else {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "Cannot recognize the band info in %s\n", pband);
		return;
	}
}

bool rtl8822be_load_txpower_by_rate(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	bool rtstatus = true;

	_rtl8822be_phy_init_tx_power_by_rate(hw);

	rtstatus = rtlpriv->phydm.ops->phydm_load_txpower_by_rate(rtlpriv);

	if (!rtstatus) {
		pr_err("BB_PG Reg Fail!!\n");
		return false;
	}

	_rtl8822be_phy_txpower_by_rate_configuration(hw);

	return true;
}

bool rtl8822be_load_txpower_limit(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtlpriv);
	bool rtstatus = true;

	_rtl8822be_phy_init_txpower_limit(hw);

	if (rtlefuse->eeprom_regulatory == 1)
		;
	else
		return true;

	rtstatus = rtlpriv->phydm.ops->phydm_load_txpower_limit(rtlpriv);

	if (!rtstatus) {
		pr_err("RF TxPwr Limit Fail!!\n");
		return false;
	}

	_rtl8822be_phy_convert_txpower_limit_to_power_index(hw);

	return true;
}

static void _rtl8822be_get_rate_values_of_tx_power_by_rate(
	struct ieee80211_hw *hw, u32 reg_addr, u32 bit_mask, u32 value,
	u8 *rate, s8 *pwr_by_rate_val, u8 *rate_num)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 /*index = 0,*/ i = 0;

	switch (reg_addr) {
	case 0xE00: /*rTxAGC_A_Rate18_06:*/
	case 0x830: /*rTxAGC_B_Rate18_06:*/
		rate[0] = DESC_RATE6M;
		rate[1] = DESC_RATE9M;
		rate[2] = DESC_RATE12M;
		rate[3] = DESC_RATE18M;
		for (i = 0; i < 4; ++i) {
			pwr_by_rate_val[i] =
				(s8)((((value >> (i * 8 + 4)) & 0xF)) * 10 +
				     ((value >> (i * 8)) & 0xF));
		}
		*rate_num = 4;
		break;

	case 0xE04: /*rTxAGC_A_Rate54_24:*/
	case 0x834: /*rTxAGC_B_Rate54_24:*/
		rate[0] = DESC_RATE24M;
		rate[1] = DESC_RATE36M;
		rate[2] = DESC_RATE48M;
		rate[3] = DESC_RATE54M;
		for (i = 0; i < 4; ++i) {
			pwr_by_rate_val[i] =
				(s8)((((value >> (i * 8 + 4)) & 0xF)) * 10 +
				     ((value >> (i * 8)) & 0xF));
		}
		*rate_num = 4;
		break;

	case 0xE08: /*rTxAGC_A_CCK1_Mcs32:*/
		rate[0] = DESC_RATE1M;
		pwr_by_rate_val[0] = (s8)((((value >> (8 + 4)) & 0xF)) * 10 +
					  ((value >> 8) & 0xF));
		*rate_num = 1;
		break;

	case 0x86C: /*rTxAGC_B_CCK11_A_CCK2_11:*/
		if (bit_mask == 0xffffff00) {
			rate[0] = DESC_RATE2M;
			rate[1] = DESC_RATE5_5M;
			rate[2] = DESC_RATE11M;
			for (i = 1; i < 4; ++i) {
				pwr_by_rate_val[i - 1] = (s8)(
					(((value >> (i * 8 + 4)) & 0xF)) * 10 +
					((value >> (i * 8)) & 0xF));
			}
			*rate_num = 3;
		} else if (bit_mask == 0x000000ff) {
			rate[0] = DESC_RATE11M;
			pwr_by_rate_val[0] = (s8)((((value >> 4) & 0xF)) * 10 +
						  (value & 0xF));
			*rate_num = 1;
		}
		break;

	case 0xE10: /*rTxAGC_A_Mcs03_Mcs00:*/
	case 0x83C: /*rTxAGC_B_Mcs03_Mcs00:*/
		rate[0] = DESC_RATEMCS0;
		rate[1] = DESC_RATEMCS1;
		rate[2] = DESC_RATEMCS2;
		rate[3] = DESC_RATEMCS3;
		for (i = 0; i < 4; ++i) {
			pwr_by_rate_val[i] =
				(s8)((((value >> (i * 8 + 4)) & 0xF)) * 10 +
				     ((value >> (i * 8)) & 0xF));
		}
		*rate_num = 4;
		break;

	case 0xE14: /*rTxAGC_A_Mcs07_Mcs04:*/
	case 0x848: /*rTxAGC_B_Mcs07_Mcs04:*/
		rate[0] = DESC_RATEMCS4;
		rate[1] = DESC_RATEMCS5;
		rate[2] = DESC_RATEMCS6;
		rate[3] = DESC_RATEMCS7;
		for (i = 0; i < 4; ++i) {
			pwr_by_rate_val[i] =
				(s8)((((value >> (i * 8 + 4)) & 0xF)) * 10 +
				     ((value >> (i * 8)) & 0xF));
		}
		*rate_num = 4;
		break;

	case 0xE18: /*rTxAGC_A_Mcs11_Mcs08:*/
	case 0x84C: /*rTxAGC_B_Mcs11_Mcs08:*/
		rate[0] = DESC_RATEMCS8;
		rate[1] = DESC_RATEMCS9;
		rate[2] = DESC_RATEMCS10;
		rate[3] = DESC_RATEMCS11;
		for (i = 0; i < 4; ++i) {
			pwr_by_rate_val[i] =
				(s8)((((value >> (i * 8 + 4)) & 0xF)) * 10 +
				     ((value >> (i * 8)) & 0xF));
		}
		*rate_num = 4;
		break;

	case 0xE1C: /*rTxAGC_A_Mcs15_Mcs12:*/
	case 0x868: /*rTxAGC_B_Mcs15_Mcs12:*/
		rate[0] = DESC_RATEMCS12;
		rate[1] = DESC_RATEMCS13;
		rate[2] = DESC_RATEMCS14;
		rate[3] = DESC_RATEMCS15;
		for (i = 0; i < 4; ++i) {
			pwr_by_rate_val[i] =
				(s8)((((value >> (i * 8 + 4)) & 0xF)) * 10 +
				     ((value >> (i * 8)) & 0xF));
		}
		*rate_num = 4;

		break;

	case 0x838: /*rTxAGC_B_CCK1_55_Mcs32:*/
		rate[0] = DESC_RATE1M;
		rate[1] = DESC_RATE2M;
		rate[2] = DESC_RATE5_5M;
		for (i = 1; i < 4; ++i) {
			pwr_by_rate_val[i - 1] =
				(s8)((((value >> (i * 8 + 4)) & 0xF)) * 10 +
				     ((value >> (i * 8)) & 0xF));
		}
		*rate_num = 3;
		break;

	case 0xC20:
	case 0xE20:
	case 0x1820:
	case 0x1a20:
		rate[0] = DESC_RATE1M;
		rate[1] = DESC_RATE2M;
		rate[2] = DESC_RATE5_5M;
		rate[3] = DESC_RATE11M;
		for (i = 0; i < 4; ++i) {
			pwr_by_rate_val[i] =
				(s8)((((value >> (i * 8 + 4)) & 0xF)) * 10 +
				     ((value >> (i * 8)) & 0xF));
		}
		*rate_num = 4;
		break;

	case 0xC24:
	case 0xE24:
	case 0x1824:
	case 0x1a24:
		rate[0] = DESC_RATE6M;
		rate[1] = DESC_RATE9M;
		rate[2] = DESC_RATE12M;
		rate[3] = DESC_RATE18M;
		for (i = 0; i < 4; ++i) {
			pwr_by_rate_val[i] =
				(s8)((((value >> (i * 8 + 4)) & 0xF)) * 10 +
				     ((value >> (i * 8)) & 0xF));
		}
		*rate_num = 4;
		break;

	case 0xC28:
	case 0xE28:
	case 0x1828:
	case 0x1a28:
		rate[0] = DESC_RATE24M;
		rate[1] = DESC_RATE36M;
		rate[2] = DESC_RATE48M;
		rate[3] = DESC_RATE54M;
		for (i = 0; i < 4; ++i) {
			pwr_by_rate_val[i] =
				(s8)((((value >> (i * 8 + 4)) & 0xF)) * 10 +
				     ((value >> (i * 8)) & 0xF));
		}
		*rate_num = 4;
		break;

	case 0xC2C:
	case 0xE2C:
	case 0x182C:
	case 0x1a2C:
		rate[0] = DESC_RATEMCS0;
		rate[1] = DESC_RATEMCS1;
		rate[2] = DESC_RATEMCS2;
		rate[3] = DESC_RATEMCS3;
		for (i = 0; i < 4; ++i) {
			pwr_by_rate_val[i] =
				(s8)((((value >> (i * 8 + 4)) & 0xF)) * 10 +
				     ((value >> (i * 8)) & 0xF));
		}
		*rate_num = 4;
		break;

	case 0xC30:
	case 0xE30:
	case 0x1830:
	case 0x1a30:
		rate[0] = DESC_RATEMCS4;
		rate[1] = DESC_RATEMCS5;
		rate[2] = DESC_RATEMCS6;
		rate[3] = DESC_RATEMCS7;
		for (i = 0; i < 4; ++i) {
			pwr_by_rate_val[i] =
				(s8)((((value >> (i * 8 + 4)) & 0xF)) * 10 +
				     ((value >> (i * 8)) & 0xF));
		}
		*rate_num = 4;
		break;

	case 0xC34:
	case 0xE34:
	case 0x1834:
	case 0x1a34:
		rate[0] = DESC_RATEMCS8;
		rate[1] = DESC_RATEMCS9;
		rate[2] = DESC_RATEMCS10;
		rate[3] = DESC_RATEMCS11;
		for (i = 0; i < 4; ++i) {
			pwr_by_rate_val[i] =
				(s8)((((value >> (i * 8 + 4)) & 0xF)) * 10 +
				     ((value >> (i * 8)) & 0xF));
		}
		*rate_num = 4;
		break;

	case 0xC38:
	case 0xE38:
	case 0x1838:
	case 0x1a38:
		rate[0] = DESC_RATEMCS12;
		rate[1] = DESC_RATEMCS13;
		rate[2] = DESC_RATEMCS14;
		rate[3] = DESC_RATEMCS15;
		for (i = 0; i < 4; ++i) {
			pwr_by_rate_val[i] =
				(s8)((((value >> (i * 8 + 4)) & 0xF)) * 10 +
				     ((value >> (i * 8)) & 0xF));
		}
		*rate_num = 4;
		break;

	case 0xC3C:
	case 0xE3C:
	case 0x183C:
	case 0x1a3C:
		rate[0] = DESC_RATEVHT1SS_MCS0;
		rate[1] = DESC_RATEVHT1SS_MCS1;
		rate[2] = DESC_RATEVHT1SS_MCS2;
		rate[3] = DESC_RATEVHT1SS_MCS3;
		for (i = 0; i < 4; ++i) {
			pwr_by_rate_val[i] =
				(s8)((((value >> (i * 8 + 4)) & 0xF)) * 10 +
				     ((value >> (i * 8)) & 0xF));
		}
		*rate_num = 4;
		break;

	case 0xC40:
	case 0xE40:
	case 0x1840:
	case 0x1a40:
		rate[0] = DESC_RATEVHT1SS_MCS4;
		rate[1] = DESC_RATEVHT1SS_MCS5;
		rate[2] = DESC_RATEVHT1SS_MCS6;
		rate[3] = DESC_RATEVHT1SS_MCS7;
		for (i = 0; i < 4; ++i) {
			pwr_by_rate_val[i] =
				(s8)((((value >> (i * 8 + 4)) & 0xF)) * 10 +
				     ((value >> (i * 8)) & 0xF));
		}
		*rate_num = 4;
		break;

	case 0xC44:
	case 0xE44:
	case 0x1844:
	case 0x1a44:
		rate[0] = DESC_RATEVHT1SS_MCS8;
		rate[1] = DESC_RATEVHT1SS_MCS9;
		rate[2] = DESC_RATEVHT2SS_MCS0;
		rate[3] = DESC_RATEVHT2SS_MCS1;
		for (i = 0; i < 4; ++i) {
			pwr_by_rate_val[i] =
				(s8)((((value >> (i * 8 + 4)) & 0xF)) * 10 +
				     ((value >> (i * 8)) & 0xF));
		}
		*rate_num = 4;
		break;

	case 0xC48:
	case 0xE48:
	case 0x1848:
	case 0x1a48:
		rate[0] = DESC_RATEVHT2SS_MCS2;
		rate[1] = DESC_RATEVHT2SS_MCS3;
		rate[2] = DESC_RATEVHT2SS_MCS4;
		rate[3] = DESC_RATEVHT2SS_MCS5;
		for (i = 0; i < 4; ++i) {
			pwr_by_rate_val[i] =
				(s8)((((value >> (i * 8 + 4)) & 0xF)) * 10 +
				     ((value >> (i * 8)) & 0xF));
		}
		*rate_num = 4;
		break;

	case 0xC4C:
	case 0xE4C:
	case 0x184C:
	case 0x1a4C:
		rate[0] = DESC_RATEVHT2SS_MCS6;
		rate[1] = DESC_RATEVHT2SS_MCS7;
		rate[2] = DESC_RATEVHT2SS_MCS8;
		rate[3] = DESC_RATEVHT2SS_MCS9;
		for (i = 0; i < 4; ++i) {
			pwr_by_rate_val[i] =
				(s8)((((value >> (i * 8 + 4)) & 0xF)) * 10 +
				     ((value >> (i * 8)) & 0xF));
		}
		*rate_num = 4;
		break;

	default:
		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
			 "Invalid reg_addr 0x%x in %s()\n", reg_addr, __func__);
		break;
	}
}

void rtl8822be_store_tx_power_by_rate(struct ieee80211_hw *hw, u32 band,
				      u32 rfpath, u32 txnum, u32 regaddr,
				      u32 bitmask, u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 i = 0, rates[4] = {0}, rate_num = 0;
	s8 pwr_by_rate_val[4] = {0};

	_rtl8822be_get_rate_values_of_tx_power_by_rate(
		hw, regaddr, bitmask, data, rates, pwr_by_rate_val, &rate_num);

	if (band != BAND_ON_2_4G && band != BAND_ON_5G) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_WARNING, "Invalid Band %d\n",
			 band);
		band = BAND_ON_2_4G;
	}
	if (rfpath >= MAX_RF_PATH) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_WARNING, "Invalid RfPath %d\n",
			 rfpath);
		rfpath = MAX_RF_PATH - 1;
	}
	if (txnum >= MAX_RF_PATH) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_WARNING, "Invalid TxNum %d\n",
			 txnum);
		txnum = MAX_RF_PATH - 1;
	}

	for (i = 0; i < rate_num; ++i) {
		u8 rate_idx = rates[i];

		if (IS_1T_RATE(rates[i]))
			txnum = RF_1TX;
		else if (IS_2T_RATE(rates[i]))
			txnum = RF_2TX;
		else
			WARN_ON(1);

		rtlphy->tx_power_by_rate_offset[band][rfpath][txnum][rate_idx] =
			pwr_by_rate_val[i];

		RT_TRACE(
			rtlpriv, COMP_INIT, DBG_LOUD,
			"TxPwrByRateOffset[Band %d][RfPath %d][TxNum %d][rate_idx %d] = 0x%x\n",
			band, rfpath, txnum, rate_idx,
			rtlphy->tx_power_by_rate_offset[band][rfpath][txnum]
						       [rate_idx]);
	}
}

static void
_rtl8822be_phy_init_bb_rf_register_definition(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;

	rtlphy->phyreg_def[RF90_PATH_A].rfintfs = RFPGA0_XAB_RFINTERFACESW;
	rtlphy->phyreg_def[RF90_PATH_B].rfintfs = RFPGA0_XAB_RFINTERFACESW;

	rtlphy->phyreg_def[RF90_PATH_A].rfintfo = RFPGA0_XA_RFINTERFACEOE;
	rtlphy->phyreg_def[RF90_PATH_B].rfintfo = RFPGA0_XB_RFINTERFACEOE;

	rtlphy->phyreg_def[RF90_PATH_A].rfintfe = RFPGA0_XA_RFINTERFACEOE;
	rtlphy->phyreg_def[RF90_PATH_B].rfintfe = RFPGA0_XB_RFINTERFACEOE;

	rtlphy->phyreg_def[RF90_PATH_A].rf3wire_offset = RA_LSSIWRITE_8822B;
	rtlphy->phyreg_def[RF90_PATH_B].rf3wire_offset = RB_LSSIWRITE_8822B;

	rtlphy->phyreg_def[RF90_PATH_A].rfhssi_para2 = RHSSIREAD_8822BE;
	rtlphy->phyreg_def[RF90_PATH_B].rfhssi_para2 = RHSSIREAD_8822BE;

	rtlphy->phyreg_def[RF90_PATH_A].rf_rb = RA_SIREAD_8822B;
	rtlphy->phyreg_def[RF90_PATH_B].rf_rb = RB_SIREAD_8822B;

	rtlphy->phyreg_def[RF90_PATH_A].rf_rbpi = RA_PIREAD_8822B;
	rtlphy->phyreg_def[RF90_PATH_B].rf_rbpi = RB_PIREAD_8822B;
}

void rtl8822be_phy_get_txpower_level(struct ieee80211_hw *hw, long *powerlevel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 txpwr_level;
	long txpwr_dbm;

	txpwr_level = rtlphy->cur_cck_txpwridx;
	txpwr_dbm = _rtl8822be_phy_txpwr_idx_to_dbm(hw, WIRELESS_MODE_B,
						    txpwr_level);
	txpwr_level = rtlphy->cur_ofdm24g_txpwridx;
	if (_rtl8822be_phy_txpwr_idx_to_dbm(hw, WIRELESS_MODE_G, txpwr_level) >
	    txpwr_dbm)
		txpwr_dbm = _rtl8822be_phy_txpwr_idx_to_dbm(hw, WIRELESS_MODE_G,
							    txpwr_level);
	txpwr_level = rtlphy->cur_ofdm24g_txpwridx;
	if (_rtl8822be_phy_txpwr_idx_to_dbm(hw, WIRELESS_MODE_N_24G,
					    txpwr_level) > txpwr_dbm)
		txpwr_dbm = _rtl8822be_phy_txpwr_idx_to_dbm(
			hw, WIRELESS_MODE_N_24G, txpwr_level);
	*powerlevel = txpwr_dbm;
}

static bool _rtl8822be_phy_get_chnl_index(u8 channel, u8 *chnl_index)
{
	u8 rtl_channel5g[CHANNEL_MAX_NUMBER_5G] = {
		36,  38,  40,  42,  44,  46,  48, /* Band 1 */
		52,  54,  56,  58,  60,  62,  64, /* Band 2 */
		100, 102, 104, 106, 108, 110, 112, /* Band 3 */
		116, 118, 120, 122, 124, 126, 128, /* Band 3 */
		132, 134, 136, 138, 140, 142, 144, /* Band 3 */
		149, 151, 153, 155, 157, 159, 161, /* Band 4 */
		165, 167, 169, 171, 173, 175, 177}; /* Band 4 */
	u8 i = 0;
	bool in_24g = true;

	if (channel <= 14) {
		in_24g = true;
		*chnl_index = channel - 1;
	} else {
		in_24g = false;

		for (i = 0; i < CHANNEL_MAX_NUMBER_5G; ++i) {
			if (rtl_channel5g[i] == channel) {
				*chnl_index = i;
				return in_24g;
			}
		}
	}
	return in_24g;
}

static char _rtl8822be_phy_get_world_wide_limit(char *limit_table)
{
	char min = limit_table[0];
	u8 i = 0;

	for (i = 0; i < MAX_REGULATION_NUM; ++i) {
		if (limit_table[i] < min)
			min = limit_table[i];
	}
	return min;
}

static char _rtl8822be_phy_get_txpower_limit(struct ieee80211_hw *hw, u8 band,
					     enum ht_channel_width bandwidth,
					     enum radio_path rf_path, u8 rate,
					     u8 channel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtlpriv);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	short regulation = -1, rate_section = -1, channel_index = -1;
	char power_limit = MAX_POWER_INDEX;

	if (rtlefuse->eeprom_regulatory == 2)
		return MAX_POWER_INDEX;

	regulation = TXPWR_LMT_WW;

	switch (rate) {
	case DESC_RATE1M:
	case DESC_RATE2M:
	case DESC_RATE5_5M:
	case DESC_RATE11M:
		rate_section = CCK;
		break;

	case DESC_RATE6M:
	case DESC_RATE9M:
	case DESC_RATE12M:
	case DESC_RATE18M:
	case DESC_RATE24M:
	case DESC_RATE36M:
	case DESC_RATE48M:
	case DESC_RATE54M:
		rate_section = OFDM;
		break;

	case DESC_RATEMCS0:
	case DESC_RATEMCS1:
	case DESC_RATEMCS2:
	case DESC_RATEMCS3:
	case DESC_RATEMCS4:
	case DESC_RATEMCS5:
	case DESC_RATEMCS6:
	case DESC_RATEMCS7:
		rate_section = HT_MCS0_MCS7;
		break;

	case DESC_RATEMCS8:
	case DESC_RATEMCS9:
	case DESC_RATEMCS10:
	case DESC_RATEMCS11:
	case DESC_RATEMCS12:
	case DESC_RATEMCS13:
	case DESC_RATEMCS14:
	case DESC_RATEMCS15:
		rate_section = HT_MCS8_MCS15;
		break;

	case DESC_RATEVHT1SS_MCS0:
	case DESC_RATEVHT1SS_MCS1:
	case DESC_RATEVHT1SS_MCS2:
	case DESC_RATEVHT1SS_MCS3:
	case DESC_RATEVHT1SS_MCS4:
	case DESC_RATEVHT1SS_MCS5:
	case DESC_RATEVHT1SS_MCS6:
	case DESC_RATEVHT1SS_MCS7:
	case DESC_RATEVHT1SS_MCS8:
	case DESC_RATEVHT1SS_MCS9:
		rate_section = VHT_1SSMCS0_1SSMCS9;
		break;

	case DESC_RATEVHT2SS_MCS0:
	case DESC_RATEVHT2SS_MCS1:
	case DESC_RATEVHT2SS_MCS2:
	case DESC_RATEVHT2SS_MCS3:
	case DESC_RATEVHT2SS_MCS4:
	case DESC_RATEVHT2SS_MCS5:
	case DESC_RATEVHT2SS_MCS6:
	case DESC_RATEVHT2SS_MCS7:
	case DESC_RATEVHT2SS_MCS8:
	case DESC_RATEVHT2SS_MCS9:
		rate_section = VHT_2SSMCS0_2SSMCS9;
		break;

	default:
		RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD, "Wrong rate 0x%x\n",
			 rate);
		break;
	}

	if (band == BAND_ON_5G && rate_section == 0)
		RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
			 "Wrong rate 0x%x: No CCK in 5G Band\n", rate);

	/* workaround for wrong index combination to obtain tx power limit,
	 * OFDM only exists in BW 20M
	 */
	if (rate_section == 1)
		bandwidth = 0;

	/* workaround for wrong index combination to obtain tx power limit,
	 * CCK table will only be given in BW 20M
	 */
	if (rate_section == 0)
		bandwidth = 0;

	/* workaround for wrong indxe combination to obtain tx power limit,
	 * HT on 80M will reference to HT on 40M
	 */
	if ((rate_section == 2 || rate_section == 3) && band == BAND_ON_5G &&
	    bandwidth == 2)
		bandwidth = 1;

	if (band == BAND_ON_2_4G)
		channel_index = _rtl8822be_phy_get_chnl_idx_of_txpwr_lmt(
			hw, BAND_ON_2_4G, channel);
	else if (band == BAND_ON_5G)
		channel_index = _rtl8822be_phy_get_chnl_idx_of_txpwr_lmt(
			hw, BAND_ON_5G, channel);
	else if (band == BAND_ON_BOTH)
		; /* BAND_ON_BOTH don't care temporarily */

	if (band >= BANDMAX || regulation == -1 || bandwidth == -1 ||
	    rate_section == -1 || channel_index == -1) {
		RT_TRACE(
			rtlpriv, COMP_POWER, DBG_LOUD,
			"Wrong index value to access power limit table [band %d][regulation %d][bandwidth %d][rf_path %d][rate_section %d][chnl %d]\n",
			band, regulation, bandwidth, rf_path, rate_section,
			channel_index);
		return MAX_POWER_INDEX;
	}

	if (band == BAND_ON_2_4G) {
		char limits[10] = {0};
		u8 i = 0;

		for (i = 0; i < 4; ++i)
			limits[i] = rtlphy->txpwr_limit_2_4g[i][bandwidth]
							    [rate_section]
							    [channel_index]
							    [rf_path];

		power_limit =
			(regulation == TXPWR_LMT_WW) ?
				_rtl8822be_phy_get_world_wide_limit(limits) :
				rtlphy->txpwr_limit_2_4g[regulation][bandwidth]
							[rate_section]
							[channel_index]
							[rf_path];

	} else if (band == BAND_ON_5G) {
		char limits[10] = {0};
		u8 i = 0;

		for (i = 0; i < MAX_REGULATION_NUM; ++i)
			limits[i] =
				rtlphy->txpwr_limit_5g[i][bandwidth]
						      [rate_section]
						      [channel_index][rf_path];

		power_limit =
			(regulation == TXPWR_LMT_WW) ?
				_rtl8822be_phy_get_world_wide_limit(limits) :
				rtlphy->txpwr_limit_5g[regulation]
						      [channel_index]
						      [rate_section]
						      [channel_index][rf_path];
	} else {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "No power limit table of the specified band\n");
	}

	return power_limit;
}

static char
_rtl8822be_phy_get_txpower_by_rate(struct ieee80211_hw *hw, u8 band, u8 path,
				   u8 rate /* enum rtl_desc8822b_rate */)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 tx_num;
	char tx_pwr_diff = 0;

	if (band != BAND_ON_2_4G && band != BAND_ON_5G)
		return tx_pwr_diff;

	if (path > RF90_PATH_B)
		return tx_pwr_diff;

	if ((rate >= DESC_RATEMCS8 && rate <= DESC_RATEMCS15) ||
	    (rate >= DESC_RATEVHT2SS_MCS0 && rate <= DESC_RATEVHT2SS_MCS9))
		tx_num = RF_2TX;
	else
		tx_num = RF_1TX;

	return (char)(rtlphy->tx_power_by_rate_offset[band][path][tx_num]
							    [rate] &
			     0xff);
}

u8 rtl8822be_get_txpower_index(struct ieee80211_hw *hw, u8 path, u8 rate,
			       u8 bandwidth, u8 channel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u8 index = (channel - 1);
	u8 txpower = 0;
	bool in_24g = false;
	char limit;
	char powerdiff_byrate = 0;

	if ((rtlhal->current_bandtype == BAND_ON_2_4G &&
	     (channel > 14 || channel < 1)) ||
	    (rtlhal->current_bandtype == BAND_ON_5G && channel <= 14)) {
		index = 0;
		RT_TRACE(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
			 "Illegal channel!!\n");
	}

	/* 1. base tx power */
	in_24g = _rtl8822be_phy_get_chnl_index(channel, &index);
	if (in_24g) {
		if (RX_HAL_IS_CCK_RATE(rate))
			txpower = rtlefuse->txpwrlevel_cck[path][index];
		else if (rate >= DESC_RATE6M)
			txpower = rtlefuse->txpwrlevel_ht40_1s[path][index];
		else
			RT_TRACE(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
				 "invalid rate\n");

		if (rate >= DESC_RATE6M && rate <= DESC_RATE54M &&
		    !RX_HAL_IS_CCK_RATE(rate))
			txpower += rtlefuse->txpwr_legacyhtdiff[path][TX_1S];

		if (bandwidth == HT_CHANNEL_WIDTH_20) {
			if ((rate >= DESC_RATEMCS0 && rate <= DESC_RATEMCS15) ||
			    (rate >= DESC_RATEVHT1SS_MCS0 &&
			     rate <= DESC_RATEVHT2SS_MCS9))
				txpower +=
					rtlefuse->txpwr_ht20diff[path][TX_1S];
			if ((rate >= DESC_RATEMCS8 && rate <= DESC_RATEMCS15) ||
			    (rate >= DESC_RATEVHT2SS_MCS0 &&
			     rate <= DESC_RATEVHT2SS_MCS9))
				txpower +=
					rtlefuse->txpwr_ht20diff[path][TX_2S];
		} else if (bandwidth == HT_CHANNEL_WIDTH_20_40) {
			if ((rate >= DESC_RATEMCS0 && rate <= DESC_RATEMCS15) ||
			    (rate >= DESC_RATEVHT1SS_MCS0 &&
			     rate <= DESC_RATEVHT2SS_MCS9))
				txpower +=
					rtlefuse->txpwr_ht40diff[path][TX_1S];
			if ((rate >= DESC_RATEMCS8 && rate <= DESC_RATEMCS15) ||
			    (rate >= DESC_RATEVHT2SS_MCS0 &&
			     rate <= DESC_RATEVHT2SS_MCS9))
				txpower +=
					rtlefuse->txpwr_ht40diff[path][TX_2S];
		} else if (bandwidth == HT_CHANNEL_WIDTH_80) {
			if ((rate >= DESC_RATEMCS0 && rate <= DESC_RATEMCS15) ||
			    (rate >= DESC_RATEVHT1SS_MCS0 &&
			     rate <= DESC_RATEVHT2SS_MCS9))
				txpower +=
					rtlefuse->txpwr_ht40diff[path][TX_1S];
			if ((rate >= DESC_RATEMCS8 && rate <= DESC_RATEMCS15) ||
			    (rate >= DESC_RATEVHT2SS_MCS0 &&
			     rate <= DESC_RATEVHT2SS_MCS9))
				txpower +=
					rtlefuse->txpwr_ht40diff[path][TX_2S];
		}

	} else {
		if (rate >= DESC_RATE6M)
			txpower = rtlefuse->txpwr_5g_bw40base[path][index];
		else
			RT_TRACE(rtlpriv, COMP_POWER_TRACKING, DBG_WARNING,
				 "INVALID Rate.\n");

		if (rate >= DESC_RATE6M && rate <= DESC_RATE54M &&
		    !RX_HAL_IS_CCK_RATE(rate))
			txpower += rtlefuse->txpwr_5g_ofdmdiff[path][TX_1S];

		if (bandwidth == HT_CHANNEL_WIDTH_20) {
			if ((rate >= DESC_RATEMCS0 && rate <= DESC_RATEMCS15) ||
			    (rate >= DESC_RATEVHT1SS_MCS0 &&
			     rate <= DESC_RATEVHT2SS_MCS9))
				txpower += rtlefuse->txpwr_5g_bw20diff[path]
								      [TX_1S];
			if ((rate >= DESC_RATEMCS8 && rate <= DESC_RATEMCS15) ||
			    (rate >= DESC_RATEVHT2SS_MCS0 &&
			     rate <= DESC_RATEVHT2SS_MCS9))
				txpower += rtlefuse->txpwr_5g_bw20diff[path]
								      [TX_2S];
		} else if (bandwidth == HT_CHANNEL_WIDTH_20_40) {
			if ((rate >= DESC_RATEMCS0 && rate <= DESC_RATEMCS15) ||
			    (rate >= DESC_RATEVHT1SS_MCS0 &&
			     rate <= DESC_RATEVHT2SS_MCS9))
				txpower += rtlefuse->txpwr_5g_bw40diff[path]
								      [TX_1S];
			if ((rate >= DESC_RATEMCS8 && rate <= DESC_RATEMCS15) ||
			    (rate >= DESC_RATEVHT2SS_MCS0 &&
			     rate <= DESC_RATEVHT2SS_MCS9))
				txpower += rtlefuse->txpwr_5g_bw40diff[path]
								      [TX_2S];
		} else if (bandwidth == HT_CHANNEL_WIDTH_80) {
			u8 i = 0;

			for (i = 0; i < sizeof(rtl_channel5g_80m) / sizeof(u8);
			     ++i)
				if (rtl_channel5g_80m[i] == channel)
					index = i;

			txpower = rtlefuse->txpwr_5g_bw80base[path][index];

			if ((rate >= DESC_RATEMCS0 && rate <= DESC_RATEMCS15) ||
			    (rate >= DESC_RATEVHT1SS_MCS0 &&
			     rate <= DESC_RATEVHT2SS_MCS9))
				txpower += rtlefuse->txpwr_5g_bw80diff[path]
								      [TX_1S];
			if ((rate >= DESC_RATEMCS8 && rate <= DESC_RATEMCS15) ||
			    (rate >= DESC_RATEVHT2SS_MCS0 &&
			     rate <= DESC_RATEVHT2SS_MCS9))
				txpower += rtlefuse->txpwr_5g_bw80diff[path]
								      [TX_2S];
		}
	}

	/* 2. tx power by rate */
	if (rtlefuse->eeprom_regulatory != 2)
		powerdiff_byrate = _rtl8822be_phy_get_txpower_by_rate(
			hw, (u8)(!in_24g), path, rate);

	/* 3. tx power limit */
	if (rtlefuse->eeprom_regulatory == 1)
		limit = _rtl8822be_phy_get_txpower_limit(
			hw, (u8)(!in_24g), bandwidth, path, rate,
			channel);
	else
		limit = MAX_POWER_INDEX;

	/* ----- */
	powerdiff_byrate = powerdiff_byrate > limit ? limit : powerdiff_byrate;

	txpower += powerdiff_byrate;

	if (txpower > MAX_POWER_INDEX)
		txpower = MAX_POWER_INDEX;

	return txpower;
}

static void _rtl8822be_phy_set_txpower_index(struct ieee80211_hw *hw,
					     u8 power_index, u8 path, u8 rate)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 shift = 0;
	static u32 index;

	/*
	 * For 8822B, phydm api use 4 bytes txagc value driver must
	 * combine every four 1 byte to one 4 byte and send to phydm
	 */
	shift = rate & 0x03;
	index |= ((u32)power_index << (shift * 8));

	if (shift == 3) {
		rate = rate - 3;

		if (!rtlpriv->phydm.ops->phydm_write_txagc(rtlpriv, index, path,
							   rate)) {
			RT_TRACE(rtlpriv, COMP_TXAGC, DBG_LOUD,
				 "%s(index:%d, rfpath:%d, rate:0x%02x) fail\n",
				 __func__, index, path, rate);

			WARN_ON(1);
		}
		index = 0;
	}
}

static void _rtl8822be_phy_set_txpower_level_by_path(struct ieee80211_hw *hw,
						     u8 *array, u8 path,
						     u8 channel, u8 size)
{
	struct rtl_phy *rtlphy = &(rtl_priv(hw)->phy);
	u8 i;
	u8 power_index;

	for (i = 0; i < size; i++) {
		power_index = rtl8822be_get_txpower_index(
			hw, path, array[i], rtlphy->current_chan_bw, channel);
		_rtl8822be_phy_set_txpower_index(hw, power_index, path,
						 array[i]);
	}
}

void rtl8822be_phy_set_txpower_level_by_path(struct ieee80211_hw *hw,
					     u8 channel, u8 path)
{
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	/*
	 * Below order is *VERY* important!
	 * Because _rtl8822be_phy_set_txpower_index() do actually writing
	 * every four power values.
	 */
	if (rtlhal->current_bandtype == BAND_ON_2_4G)
		_rtl8822be_phy_set_txpower_level_by_path(
			hw, cck_rates, path, channel, sizes_of_cck_retes);
	_rtl8822be_phy_set_txpower_level_by_path(hw, ofdm_rates, path, channel,
						 sizes_of_ofdm_retes);
	_rtl8822be_phy_set_txpower_level_by_path(hw, ht_rates_1t, path, channel,
						 sizes_of_ht_retes_1t);
	_rtl8822be_phy_set_txpower_level_by_path(hw, ht_rates_2t, path, channel,
						 sizes_of_ht_retes_2t);
	_rtl8822be_phy_set_txpower_level_by_path(hw, vht_rates_1t, path,
						 channel, sizes_of_vht_retes);
	_rtl8822be_phy_set_txpower_level_by_path(hw, vht_rates_2t, path,
						 channel, sizes_of_vht_retes);
}

void rtl8822be_phy_set_tx_power_index_by_rs(struct ieee80211_hw *hw, u8 channel,
					    u8 path, enum rate_section rs)
{
	struct {
		u8 *array;
		u8 size;
	} rs_ref[MAX_RATE_SECTION] = {
		{cck_rates, sizes_of_cck_retes},
		{ofdm_rates, sizes_of_ofdm_retes},
		{ht_rates_1t, sizes_of_ht_retes_1t},
		{ht_rates_2t, sizes_of_ht_retes_2t},
		{vht_rates_1t, sizes_of_vht_retes},
		{vht_rates_2t, sizes_of_vht_retes},
	};

	if (rs >= MAX_RATE_SECTION)
		return;

	_rtl8822be_phy_set_txpower_level_by_path(hw, rs_ref[rs].array, path,
						 channel, rs_ref[rs].size);
}

void rtl8822be_phy_set_txpower_level(struct ieee80211_hw *hw, u8 channel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 path = 0;

	for (path = RF90_PATH_A; path < rtlphy->num_total_rfpath; ++path)
		rtl8822be_phy_set_txpower_level_by_path(hw, channel, path);
}

static long _rtl8822be_phy_txpwr_idx_to_dbm(struct ieee80211_hw *hw,
					    enum wireless_mode wirelessmode,
					    u8 txpwridx)
{
	long offset;

	switch (wirelessmode) {
	case WIRELESS_MODE_B:
		offset = -7;
		break;
	case WIRELESS_MODE_G:
	case WIRELESS_MODE_N_24G:
		offset = -8;
		break;
	default:
		offset = -8;
		break;
	}
	return txpwridx / 2 + offset;
}

void rtl8822be_phy_scan_operation_backup(struct ieee80211_hw *hw, u8 operation)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	enum io_type iotype = IO_CMD_PAUSE_BAND0_DM_BY_SCAN;

	if (!is_hal_stop(rtlhal)) {
		switch (operation) {
		case SCAN_OPT_BACKUP_BAND0:
			iotype = IO_CMD_PAUSE_BAND0_DM_BY_SCAN;
			rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_IO_CMD,
						      (u8 *)&iotype);

			break;
		case SCAN_OPT_BACKUP_BAND1:
			iotype = IO_CMD_PAUSE_BAND1_DM_BY_SCAN;
			rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_IO_CMD,
						      (u8 *)&iotype);

			break;
		case SCAN_OPT_RESTORE:
			iotype = IO_CMD_RESUME_DM_BY_SCAN;
			rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_IO_CMD,
						      (u8 *)&iotype);
			break;
		default:
			pr_err("Unknown Scan Backup operation.\n");
			break;
		}
	}
}

static u8 _rtl8822be_phy_get_pri_ch_id(struct rtl_priv *rtlpriv)
{
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_mac *mac = rtl_mac(rtlpriv);
	u8 pri_ch_idx = 0;

	if (rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_80) {
		/* primary channel is at lower subband of 80MHz & 40MHz */
		if (mac->cur_40_prime_sc == HAL_PRIME_CHNL_OFFSET_LOWER &&
		    mac->cur_80_prime_sc == HAL_PRIME_CHNL_OFFSET_LOWER) {
			pri_ch_idx = VHT_DATA_SC_20_LOWEST_OF_80MHZ;
		/* primary channel is at
		 * lower subband of 80MHz & upper subband of 40MHz
		 */
		} else if ((mac->cur_40_prime_sc ==
			    HAL_PRIME_CHNL_OFFSET_UPPER) &&
			   (mac->cur_80_prime_sc ==
			    HAL_PRIME_CHNL_OFFSET_LOWER)) {
			pri_ch_idx = VHT_DATA_SC_20_LOWER_OF_80MHZ;
		/* primary channel is at
		 * upper subband of 80MHz & lower subband of 40MHz
		 */
		} else if ((mac->cur_40_prime_sc ==
			  HAL_PRIME_CHNL_OFFSET_LOWER) &&
			 (mac->cur_80_prime_sc ==
			  HAL_PRIME_CHNL_OFFSET_UPPER)) {
			pri_ch_idx = VHT_DATA_SC_20_UPPER_OF_80MHZ;
		/* primary channel is at
		 * upper subband of 80MHz & upper subband of 40MHz
		 */
		} else if ((mac->cur_40_prime_sc ==
			    HAL_PRIME_CHNL_OFFSET_UPPER) &&
			   (mac->cur_80_prime_sc ==
			    HAL_PRIME_CHNL_OFFSET_UPPER)) {
			pri_ch_idx = VHT_DATA_SC_20_UPPERST_OF_80MHZ;
		} else {
			if (mac->cur_80_prime_sc == HAL_PRIME_CHNL_OFFSET_LOWER)
				pri_ch_idx = VHT_DATA_SC_40_LOWER_OF_80MHZ;
			else if (mac->cur_80_prime_sc ==
				 HAL_PRIME_CHNL_OFFSET_UPPER)
				pri_ch_idx = VHT_DATA_SC_40_UPPER_OF_80MHZ;
		}
	} else if (rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_20_40) {
		/* primary channel is at upper subband of 40MHz */
		if (mac->cur_40_prime_sc == HAL_PRIME_CHNL_OFFSET_UPPER)
			pri_ch_idx = VHT_DATA_SC_20_UPPER_OF_80MHZ;
		/* primary channel is at lower subband of 40MHz */
		else if (mac->cur_40_prime_sc == HAL_PRIME_CHNL_OFFSET_LOWER)
			pri_ch_idx = VHT_DATA_SC_20_LOWER_OF_80MHZ;
		else
			;
	}

	return pri_ch_idx;
}

void rtl8822be_phy_set_bw_mode(struct ieee80211_hw *hw,
			       enum nl80211_channel_type ch_type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u8 tmp_bw = rtlphy->current_chan_bw;

	if (rtlphy->set_bwmode_inprogress)
		return;
	rtlphy->set_bwmode_inprogress = true;
	if ((!is_hal_stop(rtlhal)) && !(RT_CANNOT_IO(hw))) {
		/* get primary channel index */
		u8 pri_ch_idx = _rtl8822be_phy_get_pri_ch_id(rtlpriv);

		/* 3.1 set MAC register */
		rtlpriv->halmac.ops->halmac_set_bandwidth(
			rtlpriv, rtlphy->current_channel, pri_ch_idx,
			rtlphy->current_chan_bw);

		/* 3.2 set BB/RF registet */
		rtlpriv->phydm.ops->phydm_switch_bandwidth(
			rtlpriv, pri_ch_idx, rtlphy->current_chan_bw);

		if (!mac->act_scanning)
			rtlpriv->phydm.ops->phydm_iq_calibrate(rtlpriv);

		rtlphy->set_bwmode_inprogress = false;
	} else {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 "FALSE driver sleep or unload\n");
		rtlphy->set_bwmode_inprogress = false;
		rtlphy->current_chan_bw = tmp_bw;
	}
}

u8 rtl8822be_phy_sw_chnl(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u32 timeout = 1000, timecount = 0;
	u8 channel = rtlphy->current_channel;

	if (rtlphy->sw_chnl_inprogress)
		return 0;
	if (rtlphy->set_bwmode_inprogress)
		return 0;

	if ((is_hal_stop(rtlhal)) || (RT_CANNOT_IO(hw))) {
		RT_TRACE(rtlpriv, COMP_CHAN, DBG_LOUD,
			 "sw_chnl_inprogress false driver sleep or unload\n");
		return 0;
	}
	while (rtlphy->lck_inprogress && timecount < timeout) {
		mdelay(50);
		timecount += 50;
	}

	if (rtlphy->current_channel > 14)
		rtlhal->current_bandtype = BAND_ON_5G;
	else if (rtlphy->current_channel <= 14)
		rtlhal->current_bandtype = BAND_ON_2_4G;

	if (rtlpriv->cfg->ops->get_btc_status())
		rtlpriv->btcoexist.btc_ops->btc_switch_band_notify(
			rtlpriv, rtlhal->current_bandtype, mac->act_scanning);
	else
		rtlpriv->btcoexist.btc_ops->btc_switch_band_notify_wifi_only(
			rtlpriv, rtlhal->current_bandtype, mac->act_scanning);

	rtlpriv->phydm.ops->phydm_switch_band(rtlpriv, rtlphy->current_channel);

	rtlphy->sw_chnl_inprogress = true;
	if (channel == 0)
		channel = 1;

	RT_TRACE(rtlpriv, COMP_SCAN, DBG_TRACE,
		 "switch to channel%d, band type is %d\n",
		 rtlphy->current_channel, rtlhal->current_bandtype);

	rtlpriv->phydm.ops->phydm_switch_channel(rtlpriv,
						 rtlphy->current_channel);

	rtlpriv->phydm.ops->phydm_clear_txpowertracking_state(rtlpriv);

	rtl8822be_phy_set_txpower_level(hw, rtlphy->current_channel);

	RT_TRACE(rtlpriv, COMP_SCAN, DBG_TRACE, "\n");
	rtlphy->sw_chnl_inprogress = false;
	return 1;
}

bool rtl8822be_phy_set_io_cmd(struct ieee80211_hw *hw, enum io_type iotype)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	bool postprocessing = false;

	RT_TRACE(rtlpriv, COMP_CMD, DBG_TRACE,
		 "-->IO Cmd(%#x), set_io_inprogress(%d)\n", iotype,
		 rtlphy->set_io_inprogress);
	do {
		switch (iotype) {
		case IO_CMD_RESUME_DM_BY_SCAN:
			RT_TRACE(rtlpriv, COMP_CMD, DBG_TRACE,
				 "[IO CMD] Resume DM after scan.\n");
			postprocessing = true;
			break;
		case IO_CMD_PAUSE_BAND0_DM_BY_SCAN:
		case IO_CMD_PAUSE_BAND1_DM_BY_SCAN:
			RT_TRACE(rtlpriv, COMP_CMD, DBG_TRACE,
				 "[IO CMD] Pause DM before scan.\n");
			postprocessing = true;
			break;
		default:
			pr_err("switch case not process\n");
			break;
		}
	} while (false);
	if (postprocessing && !rtlphy->set_io_inprogress) {
		rtlphy->set_io_inprogress = true;
		rtlphy->current_io_type = iotype;
	} else {
		return false;
	}
	rtl8822be_phy_set_io(hw);
	RT_TRACE(rtlpriv, COMP_CMD, DBG_TRACE, "IO Type(%#x)\n", iotype);
	return true;
}

static void rtl8822be_phy_set_io(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;

	RT_TRACE(rtlpriv, COMP_CMD, DBG_TRACE,
		 "--->Cmd(%#x), set_io_inprogress(%d)\n",
		 rtlphy->current_io_type, rtlphy->set_io_inprogress);
	switch (rtlphy->current_io_type) {
	case IO_CMD_RESUME_DM_BY_SCAN:
		break;
	case IO_CMD_PAUSE_BAND0_DM_BY_SCAN:
		break;
	case IO_CMD_PAUSE_BAND1_DM_BY_SCAN:
		break;
	default:
		pr_err("switch case not process\n");
		break;
	}
	rtlphy->set_io_inprogress = false;
	RT_TRACE(rtlpriv, COMP_CMD, DBG_TRACE, "(%#x)\n",
		 rtlphy->current_io_type);
}

static void rtl8822be_phy_set_rf_on(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_write_byte(rtlpriv, REG_SPS0_CTRL_8822B, 0x2b);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN_8822B, 0xE3);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN_8822B, 0xE2);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN_8822B, 0xE3);
	rtl_write_byte(rtlpriv, REG_TXPAUSE_8822B, 0x00);
}

static bool _rtl8822be_phy_set_rf_power_state(struct ieee80211_hw *hw,
					      enum rf_pwrstate rfpwr_state)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	bool bresult = true;
	u8 i, queue_id;
	struct rtl8192_tx_ring *ring = NULL;

	switch (rfpwr_state) {
	case ERFON:
		if (ppsc->rfpwr_state == ERFOFF &&
		    RT_IN_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC)) {
			bool rtstatus = false;
			u32 initialize_count = 0;

			do {
				initialize_count++;
				RT_TRACE(rtlpriv, COMP_RF, DBG_DMESG,
					 "IPS Set eRf nic enable\n");
				rtstatus = rtl_ps_enable_nic(hw);
			} while ((!rtstatus) && (initialize_count < 10));
			RT_CLEAR_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC);
		} else {
			RT_TRACE(rtlpriv, COMP_RF, DBG_DMESG,
				 "Set ERFON slept:%d ms\n",
				 jiffies_to_msecs(jiffies -
						  ppsc->last_sleep_jiffies));
			ppsc->last_awake_jiffies = jiffies;
			rtl8822be_phy_set_rf_on(hw);
		}
		if (mac->link_state == MAC80211_LINKED)
			rtlpriv->cfg->ops->led_control(hw, LED_CTL_LINK);
		else
			rtlpriv->cfg->ops->led_control(hw, LED_CTL_NO_LINK);
		break;
	case ERFOFF:
		for (queue_id = 0, i = 0;
		     queue_id < RTL_PCI_MAX_TX_QUEUE_COUNT;) {
			ring = &pcipriv->dev.tx_ring[queue_id];
			if (queue_id == BEACON_QUEUE ||
			    skb_queue_len(&ring->queue) == 0) {
				queue_id++;
				continue;
			} else {
				RT_TRACE(
					rtlpriv, COMP_ERR, DBG_WARNING,
					"eRf Off/Sleep: %d times TcbBusyQueue[%d] =%d before doze!\n",
					(i + 1), queue_id,
					skb_queue_len(&ring->queue));

				udelay(10);
				i++;
			}
			if (i >= MAX_DOZE_WAITING_TIMES_9x) {
				RT_TRACE(
					rtlpriv, COMP_ERR, DBG_WARNING,
					"\n ERFSLEEP: %d times TcbBusyQueue[%d] = %d !\n",
					MAX_DOZE_WAITING_TIMES_9x, queue_id,
					skb_queue_len(&ring->queue));
				break;
			}
		}

		if (ppsc->reg_rfps_level & RT_RF_OFF_LEVL_HALT_NIC) {
			RT_TRACE(rtlpriv, COMP_RF, DBG_DMESG,
				 "IPS Set eRf nic disable\n");
			rtl_ps_disable_nic(hw);
			RT_SET_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC);
		} else {
			if (ppsc->rfoff_reason == RF_CHANGE_BY_IPS) {
				rtlpriv->cfg->ops->led_control(hw,
							       LED_CTL_NO_LINK);
			} else {
				rtlpriv->cfg->ops->led_control(
					hw, LED_CTL_POWER_OFF);
			}
		}
		break;
	default:
		pr_err("switch case not process\n");
		bresult = false;
		break;
	}
	if (bresult)
		ppsc->rfpwr_state = rfpwr_state;
	return bresult;
}

bool rtl8822be_phy_set_rf_power_state(struct ieee80211_hw *hw,
				      enum rf_pwrstate rfpwr_state)
{
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));

	bool bresult = false;

	if (rfpwr_state == ppsc->rfpwr_state)
		return bresult;
	return _rtl8822be_phy_set_rf_power_state(hw, rfpwr_state);
}
