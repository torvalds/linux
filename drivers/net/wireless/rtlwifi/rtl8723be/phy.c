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
#include "../pci.h"
#include "../ps.h"
#include "../core.h"
#include "reg.h"
#include "def.h"
#include "phy.h"
#include "../rtl8723com/phy_common.h"
#include "rf.h"
#include "dm.h"
#include "table.h"
#include "trx.h"

static bool _rtl8723be_phy_bb8723b_config_parafile(struct ieee80211_hw *hw);
static bool _rtl8723be_phy_config_bb_with_pgheaderfile(struct ieee80211_hw *hw,
						       u8 configtype);
static bool rtl8723be_phy_sw_chn_step_by_step(struct ieee80211_hw *hw,
					      u8 channel, u8 *stage,
					      u8 *step, u32 *delay);
static bool _rtl8723be_check_condition(struct ieee80211_hw *hw,
				       const u32  condition)
{
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u32 _board = rtlefuse->board_type; /*need efuse define*/
	u32 _interface = rtlhal->interface;
	u32 _platform = 0x08;/*SupportPlatform */
	u32 cond = condition;

	if (condition == 0xCDCDCDCD)
		return true;

	cond = condition & 0xFF;
	if ((_board & cond) == 0 && cond != 0x1F)
		return false;

	cond = condition & 0xFF00;
	cond = cond >> 8;
	if ((_interface & cond) == 0 && cond != 0x07)
		return false;

	cond = condition & 0xFF0000;
	cond = cond >> 16;
	if ((_platform & cond) == 0 && cond != 0x0F)
		return false;
	return true;
}

static bool _rtl8723be_phy_config_mac_with_headerfile(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 i;
	u32 arraylength;
	u32 *ptrarray;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE, "Read rtl8723beMACPHY_Array\n");
	arraylength = RTL8723BEMAC_1T_ARRAYLEN;
	ptrarray = RTL8723BEMAC_1T_ARRAY;
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "Img:RTL8723bEMAC_1T_ARRAY LEN %d\n", arraylength);
	for (i = 0; i < arraylength; i = i + 2)
		rtl_write_byte(rtlpriv, ptrarray[i], (u8) ptrarray[i + 1]);
	return true;
}

static bool _rtl8723be_phy_config_bb_with_headerfile(struct ieee80211_hw *hw,
						     u8 configtype)
{
	#define READ_NEXT_PAIR(v1, v2, i) \
		do { \
			i += 2; \
			v1 = array_table[i];\
			v2 = array_table[i+1]; \
		} while (0)

	int i;
	u32 *array_table;
	u16 arraylen;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 v1 = 0, v2 = 0;

	if (configtype == BASEBAND_CONFIG_PHY_REG) {
		arraylen = RTL8723BEPHY_REG_1TARRAYLEN;
		array_table = RTL8723BEPHY_REG_1TARRAY;

		for (i = 0; i < arraylen; i = i + 2) {
			v1 = array_table[i];
			v2 = array_table[i+1];
			if (v1 < 0xcdcdcdcd) {
				rtl_bb_delay(hw, v1, v2);
			} else {/*This line is the start line of branch.*/
				if (!_rtl8723be_check_condition(hw, array_table[i])) {
					/*Discard the following (offset, data) pairs*/
					READ_NEXT_PAIR(v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD &&
					       i < arraylen - 2) {
						READ_NEXT_PAIR(v1, v2, i);
					}
					i -= 2; /* prevent from for-loop += 2*/
				/* Configure matched pairs and
				 * skip to end of if-else.
				 */
				} else {
					READ_NEXT_PAIR(v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD &&
					       i < arraylen - 2) {
						rtl_bb_delay(hw,
								    v1, v2);
						READ_NEXT_PAIR(v1, v2, i);
					}

					while (v2 != 0xDEAD && i < arraylen - 2)
						READ_NEXT_PAIR(v1, v2, i);
				}
			}
		}
	} else if (configtype == BASEBAND_CONFIG_AGC_TAB) {
		arraylen = RTL8723BEAGCTAB_1TARRAYLEN;
		array_table = RTL8723BEAGCTAB_1TARRAY;

		for (i = 0; i < arraylen; i = i + 2) {
			v1 = array_table[i];
			v2 = array_table[i+1];
			if (v1 < 0xCDCDCDCD) {
				rtl_set_bbreg(hw, array_table[i],
					      MASKDWORD,
					      array_table[i + 1]);
				udelay(1);
				continue;
			} else {/*This line is the start line of branch.*/
				if (!_rtl8723be_check_condition(hw, array_table[i])) {
					/* Discard the following
					 * (offset, data) pairs
					 */
					READ_NEXT_PAIR(v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD &&
					       i < arraylen - 2) {
						READ_NEXT_PAIR(v1, v2, i);
					}
					i -= 2; /* prevent from for-loop += 2*/
				/*Configure matched pairs and
				 *skip to end of if-else.
				 */
				} else {
					READ_NEXT_PAIR(v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD &&
					       i < arraylen - 2) {
						rtl_set_bbreg(hw, array_table[i],
							      MASKDWORD,
							      array_table[i + 1]);
						udelay(1);
						READ_NEXT_PAIR(v1, v2, i);
					}

					while (v2 != 0xDEAD && i < arraylen - 2)
						READ_NEXT_PAIR(v1, v2, i);
				}
			}
			RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
				 "The agctab_array_table[0] is "
				  "%x Rtl818EEPHY_REGArray[1] is %x\n",
				  array_table[i], array_table[i + 1]);
		}
	}
	return true;
}

static u8 _rtl8723be_get_rate_section_index(u32 regaddr)
{
	u8 index = 0;

	switch (regaddr) {
	case RTXAGC_A_RATE18_06:
	case RTXAGC_B_RATE18_06:
		index = 0;
		break;
	case RTXAGC_A_RATE54_24:
	case RTXAGC_B_RATE54_24:
		index = 1;
		break;
	case RTXAGC_A_CCK1_MCS32:
	case RTXAGC_B_CCK1_55_MCS32:
		index = 2;
		break;
	case RTXAGC_B_CCK11_A_CCK2_11:
		index = 3;
		break;
	case RTXAGC_A_MCS03_MCS00:
	case RTXAGC_B_MCS03_MCS00:
		index = 4;
		break;
	case RTXAGC_A_MCS07_MCS04:
	case RTXAGC_B_MCS07_MCS04:
		index = 5;
		break;
	case RTXAGC_A_MCS11_MCS08:
	case RTXAGC_B_MCS11_MCS08:
		index = 6;
		break;
	case RTXAGC_A_MCS15_MCS12:
	case RTXAGC_B_MCS15_MCS12:
		index = 7;
		break;
	default:
		regaddr &= 0xFFF;
		if (regaddr >= 0xC20 && regaddr <= 0xC4C)
			index = (u8) ((regaddr - 0xC20) / 4);
		else if (regaddr >= 0xE20 && regaddr <= 0xE4C)
			index = (u8) ((regaddr - 0xE20) / 4);
		break;
	};
	return index;
}

u32 rtl8723be_phy_query_rf_reg(struct ieee80211_hw *hw, enum radio_path rfpath,
			       u32 regaddr, u32 bitmask)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 original_value, readback_value, bitshift;
	unsigned long flags;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), rfpath(%#x), bitmask(%#x)\n",
		  regaddr, rfpath, bitmask);

	spin_lock_irqsave(&rtlpriv->locks.rf_lock, flags);

	original_value = rtl8723_phy_rf_serial_read(hw, rfpath, regaddr);
	bitshift = rtl8723_phy_calculate_bit_shift(bitmask);
	readback_value = (original_value & bitmask) >> bitshift;

	spin_unlock_irqrestore(&rtlpriv->locks.rf_lock, flags);

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), rfpath(%#x), "
		  "bitmask(%#x), original_value(%#x)\n",
		  regaddr, rfpath, bitmask, original_value);

	return readback_value;
}

void rtl8723be_phy_set_rf_reg(struct ieee80211_hw *hw, enum radio_path path,
			      u32 regaddr, u32 bitmask, u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 original_value, bitshift;
	unsigned long flags;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), bitmask(%#x), data(%#x), rfpath(%#x)\n",
		  regaddr, bitmask, data, path);

	spin_lock_irqsave(&rtlpriv->locks.rf_lock, flags);

	if (bitmask != RFREG_OFFSET_MASK) {
			original_value = rtl8723_phy_rf_serial_read(hw, path,
								    regaddr);
			bitshift = rtl8723_phy_calculate_bit_shift(bitmask);
			data = ((original_value & (~bitmask)) |
				(data << bitshift));
		}

	rtl8723_phy_rf_serial_write(hw, path, regaddr, data);

	spin_unlock_irqrestore(&rtlpriv->locks.rf_lock, flags);

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), bitmask(%#x), data(%#x), rfpath(%#x)\n",
		  regaddr, bitmask, data, path);
}

bool rtl8723be_phy_mac_config(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	bool rtstatus = _rtl8723be_phy_config_mac_with_headerfile(hw);

	rtl_write_byte(rtlpriv, 0x04CA, 0x0B);
	return rtstatus;
}

bool rtl8723be_phy_bb_config(struct ieee80211_hw *hw)
{
	bool rtstatus = true;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u16 regval;
	u8 reg_hwparafile = 1;
	u32 tmp;
	u8 crystalcap = rtlpriv->efuse.crystalcap;
	rtl8723_phy_init_bb_rf_reg_def(hw);
	regval = rtl_read_word(rtlpriv, REG_SYS_FUNC_EN);
	rtl_write_word(rtlpriv, REG_SYS_FUNC_EN,
		       regval | BIT(13) | BIT(0) | BIT(1));

	rtl_write_byte(rtlpriv, REG_RF_CTRL, RF_EN | RF_RSTB | RF_SDMRSTB);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN,
		       FEN_PPLL | FEN_PCIEA | FEN_DIO_PCIE |
		       FEN_BB_GLB_RSTN | FEN_BBRSTB);
	tmp = rtl_read_dword(rtlpriv, 0x4c);
	rtl_write_dword(rtlpriv, 0x4c, tmp | BIT(23));

	rtl_write_byte(rtlpriv, REG_AFE_XTAL_CTRL + 1, 0x80);

	if (reg_hwparafile == 1)
		rtstatus = _rtl8723be_phy_bb8723b_config_parafile(hw);

	crystalcap = crystalcap & 0x3F;
	rtl_set_bbreg(hw, REG_MAC_PHY_CTRL, 0xFFF000,
		      (crystalcap | crystalcap << 6));

	return rtstatus;
}

bool rtl8723be_phy_rf_config(struct ieee80211_hw *hw)
{
	return rtl8723be_phy_rf6052_config(hw);
}

static void _rtl8723be_config_rf_reg(struct ieee80211_hw *hw, u32 addr,
				     u32 data, enum radio_path rfpath,
				     u32 regaddr)
{
	if (addr == 0xfe || addr == 0xffe) {
		mdelay(50);
	} else {
		rtl_set_rfreg(hw, rfpath, regaddr, RFREG_OFFSET_MASK, data);
		udelay(1);
	}
}

static void _rtl8723be_config_rf_radio_a(struct ieee80211_hw *hw,
					 u32 addr, u32 data)
{
	u32 content = 0x1000; /*RF Content: radio_a_txt*/
	u32 maskforphyset = (u32)(content & 0xE000);

	_rtl8723be_config_rf_reg(hw, addr, data, RF90_PATH_A,
				 addr | maskforphyset);
}

static void _rtl8723be_phy_init_tx_power_by_rate(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);

	u8 band, path, txnum, section;

	for (band = BAND_ON_2_4G; band <= BAND_ON_5G; ++band)
		for (path = 0; path < TX_PWR_BY_RATE_NUM_RF; ++path)
			for (txnum = 0; txnum < TX_PWR_BY_RATE_NUM_RF; ++txnum)
				for (section = 0;
				     section < TX_PWR_BY_RATE_NUM_SECTION;
				     ++section)
					rtlphy->tx_power_by_rate_offset[band]
						[path][txnum][section] = 0;
}

static void phy_set_txpwr_by_rate_base(struct ieee80211_hw *hw, u8 band,
				       u8 path, u8 rate_section,
				       u8 txnum, u8 value)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);

	if (path > RF90_PATH_D) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Invalid Rf Path %d in phy_SetTxPowerByRatBase()\n",
			  path);
		return;
	}

	if (band == BAND_ON_2_4G) {
		switch (rate_section) {
		case CCK:
			rtlphy->txpwr_by_rate_base_24g[path][txnum][0] = value;
			break;
		case OFDM:
			rtlphy->txpwr_by_rate_base_24g[path][txnum][1] = value;
			break;
		case HT_MCS0_MCS7:
			rtlphy->txpwr_by_rate_base_24g[path][txnum][2] = value;
			break;
		case HT_MCS8_MCS15:
			rtlphy->txpwr_by_rate_base_24g[path][txnum][3] = value;
			break;
		default:
			RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
				 "Invalid RateSection %d in Band 2.4G, Rf Path"
				  " %d, %dTx in PHY_SetTxPowerByRateBase()\n",
				  rate_section, path, txnum);
			break;
		};
	} else {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Invalid Band %d in PHY_SetTxPowerByRateBase()\n",
			  band);
	}
}

static u8 phy_get_txpwr_by_rate_base(struct ieee80211_hw *hw, u8 band, u8 path,
				     u8 txnum, u8 rate_section)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u8 value = 0;
	if (path > RF90_PATH_D) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Invalid Rf Path %d in PHY_GetTxPowerByRateBase()\n",
			  path);
		return 0;
	}

	if (band == BAND_ON_2_4G) {
		switch (rate_section) {
		case CCK:
			value = rtlphy->txpwr_by_rate_base_24g[path][txnum][0];
			break;
		case OFDM:
			value = rtlphy->txpwr_by_rate_base_24g[path][txnum][1];
			break;
		case HT_MCS0_MCS7:
			value = rtlphy->txpwr_by_rate_base_24g[path][txnum][2];
			break;
		case HT_MCS8_MCS15:
			value = rtlphy->txpwr_by_rate_base_24g[path][txnum][3];
			break;
		default:
			RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
				 "Invalid RateSection %d in Band 2.4G, Rf Path"
				  " %d, %dTx in PHY_GetTxPowerByRateBase()\n",
				  rate_section, path, txnum);
			break;
		};
	} else {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Invalid Band %d in PHY_GetTxPowerByRateBase()\n",
			  band);
	}

	return value;
}

static void _rtl8723be_phy_store_txpower_by_rate_base(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u16 raw_value = 0;
	u8 base = 0, path = 0;

	for (path = RF90_PATH_A; path <= RF90_PATH_B; ++path) {
		if (path == RF90_PATH_A) {
			raw_value = (u16) (rtlphy->tx_power_by_rate_offset
				[BAND_ON_2_4G][path][RF_1TX][3] >> 24) & 0xFF;
			base = (raw_value >> 4) * 10 + (raw_value & 0xF);
			phy_set_txpwr_by_rate_base(hw, BAND_ON_2_4G, path, CCK,
						   RF_1TX, base);
		} else if (path == RF90_PATH_B) {
			raw_value = (u16) (rtlphy->tx_power_by_rate_offset
				[BAND_ON_2_4G][path][RF_1TX][3] >> 0) & 0xFF;
			base = (raw_value >> 4) * 10 + (raw_value & 0xF);
			phy_set_txpwr_by_rate_base(hw, BAND_ON_2_4G, path,
						   CCK, RF_1TX, base);
		}
		raw_value = (u16) (rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G]
					  [path][RF_1TX][1] >> 24) & 0xFF;
		base = (raw_value >> 4) * 10 + (raw_value & 0xF);
		phy_set_txpwr_by_rate_base(hw, BAND_ON_2_4G, path, OFDM, RF_1TX,
					   base);

		raw_value = (u16) (rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G]
					  [path][RF_1TX][5] >> 24) & 0xFF;
		base = (raw_value >> 4) * 10 + (raw_value & 0xF);
		phy_set_txpwr_by_rate_base(hw, BAND_ON_2_4G, path, HT_MCS0_MCS7,
					   RF_1TX, base);

		raw_value = (u16) (rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G]
					  [path][RF_2TX][7] >> 24) & 0xFF;
		base = (raw_value >> 4) * 10 + (raw_value & 0xF);
		phy_set_txpwr_by_rate_base(hw, BAND_ON_2_4G, path,
					   HT_MCS8_MCS15, RF_2TX, base);
	}
}

static void phy_conv_dbm_to_rel(u32 *data, u8 start, u8 end, u8 base_val)
{
	char i = 0;
	u8 temp_value = 0;
	u32 temp_data = 0;

	for (i = 3; i >= 0; --i) {
		if (i >= start && i <= end) {
			/* Get the exact value */
			temp_value = (u8) (*data >> (i * 8)) & 0xF;
			temp_value += ((u8) ((*data >> (i*8 + 4)) & 0xF)) * 10;

			/* Change the value to a relative value */
			temp_value = (temp_value > base_val) ?
				     temp_value - base_val :
				     base_val - temp_value;
		} else {
			temp_value = (u8) (*data >> (i * 8)) & 0xFF;
		}
		temp_data <<= 8;
		temp_data |= temp_value;
	}
	*data = temp_data;
}

static void conv_dbm_to_rel(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u8 base = 0, rfpath = RF90_PATH_A;

	base = phy_get_txpwr_by_rate_base(hw, BAND_ON_2_4G, rfpath,
					  RF_1TX, CCK);
	phy_conv_dbm_to_rel(&(rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G]
			    [rfpath][RF_1TX][2]), 1, 1, base);
	phy_conv_dbm_to_rel(&(rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G]
			    [rfpath][RF_1TX][3]), 1, 3, base);

	base = phy_get_txpwr_by_rate_base(hw, BAND_ON_2_4G, rfpath,
					  RF_1TX, OFDM);
	phy_conv_dbm_to_rel(&(rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G]
			    [rfpath][RF_1TX][0]), 0, 3, base);
	phy_conv_dbm_to_rel(&(rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G]
			    [rfpath][RF_1TX][1]), 0, 3, base);

	base = phy_get_txpwr_by_rate_base(hw, BAND_ON_2_4G, rfpath,
					  RF_1TX, HT_MCS0_MCS7);
	phy_conv_dbm_to_rel(&(rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G]
			    [rfpath][RF_1TX][4]), 0, 3, base);
	phy_conv_dbm_to_rel(&(rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G]
			    [rfpath][RF_1TX][5]), 0, 3, base);

	base = phy_get_txpwr_by_rate_base(hw, BAND_ON_2_4G, rfpath,
					  RF_2TX, HT_MCS8_MCS15);
	phy_conv_dbm_to_rel(&(rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G]
			    [rfpath][RF_2TX][6]), 0, 3, base);

	phy_conv_dbm_to_rel(&(rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G]
			    [rfpath][RF_2TX][7]), 0, 3, base);

	RT_TRACE(rtlpriv, COMP_POWER, DBG_TRACE,
		 "<=== conv_dbm_to_rel()\n");
}

static void _rtl8723be_phy_txpower_by_rate_configuration(
							struct ieee80211_hw *hw)
{
	_rtl8723be_phy_store_txpower_by_rate_base(hw);
	conv_dbm_to_rel(hw);
}

static bool _rtl8723be_phy_bb8723b_config_parafile(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	bool rtstatus;

	rtstatus = _rtl8723be_phy_config_bb_with_headerfile(hw,
						BASEBAND_CONFIG_PHY_REG);
	if (!rtstatus) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "Write BB Reg Fail!!");
		return false;
	}
	_rtl8723be_phy_init_tx_power_by_rate(hw);
	if (!rtlefuse->autoload_failflag) {
		rtlphy->pwrgroup_cnt = 0;
		rtstatus = _rtl8723be_phy_config_bb_with_pgheaderfile(hw,
						BASEBAND_CONFIG_PHY_REG);
	}
	_rtl8723be_phy_txpower_by_rate_configuration(hw);
	if (!rtstatus) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "BB_PG Reg Fail!!");
		return false;
	}
	rtstatus = _rtl8723be_phy_config_bb_with_headerfile(hw,
						BASEBAND_CONFIG_AGC_TAB);
	if (!rtstatus) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "AGC Table Fail\n");
		return false;
	}
	rtlphy->cck_high_power = (bool) (rtl_get_bbreg(hw,
						       RFPGA0_XA_HSSIPARAMETER2,
						       0x200));
	return true;
}

static void _rtl8723be_store_tx_power_by_rate(struct ieee80211_hw *hw,
					      u32 band, u32 rfpath,
					      u32 txnum, u32 regaddr,
					      u32 bitmask, u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u8 rate_section = _rtl8723be_get_rate_section_index(regaddr);

	if (band != BAND_ON_2_4G && band != BAND_ON_5G) {
		RT_TRACE(rtlpriv, COMP_POWER, PHY_TXPWR,
			 "Invalid Band %d\n", band);
		return;
	}

	if (rfpath > TX_PWR_BY_RATE_NUM_RF) {
		RT_TRACE(rtlpriv, COMP_POWER, PHY_TXPWR,
			 "Invalid RfPath %d\n", rfpath);
		return;
	}
	if (txnum > TX_PWR_BY_RATE_NUM_RF) {
		RT_TRACE(rtlpriv, COMP_POWER, PHY_TXPWR,
			 "Invalid TxNum %d\n", txnum);
		return;
	}
	rtlphy->tx_power_by_rate_offset[band][rfpath][txnum][rate_section] =
									data;
}

static bool _rtl8723be_phy_config_bb_with_pgheaderfile(struct ieee80211_hw *hw,
						       u8 configtype)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	int i;
	u32 *phy_regarray_table_pg;
	u16 phy_regarray_pg_len;
	u32 v1 = 0, v2 = 0, v3 = 0, v4 = 0, v5 = 0, v6 = 0;

	phy_regarray_pg_len = RTL8723BEPHY_REG_ARRAY_PGLEN;
	phy_regarray_table_pg = RTL8723BEPHY_REG_ARRAY_PG;

	if (configtype == BASEBAND_CONFIG_PHY_REG) {
		for (i = 0; i < phy_regarray_pg_len; i = i + 6) {
			v1 = phy_regarray_table_pg[i];
			v2 = phy_regarray_table_pg[i+1];
			v3 = phy_regarray_table_pg[i+2];
			v4 = phy_regarray_table_pg[i+3];
			v5 = phy_regarray_table_pg[i+4];
			v6 = phy_regarray_table_pg[i+5];

			if (v1 < 0xcdcdcdcd) {
				if (phy_regarray_table_pg[i] == 0xfe ||
				    phy_regarray_table_pg[i] == 0xffe)
					mdelay(50);
				else
					_rtl8723be_store_tx_power_by_rate(hw,
							v1, v2, v3, v4, v5, v6);
				continue;
			} else {
				/*don't need the hw_body*/
				if (!_rtl8723be_check_condition(hw,
						phy_regarray_table_pg[i])) {
					i += 2; /* skip the pair of expression*/
					v1 = phy_regarray_table_pg[i];
					v2 = phy_regarray_table_pg[i+1];
					v3 = phy_regarray_table_pg[i+2];
					while (v2 != 0xDEAD) {
						i += 3;
						v1 = phy_regarray_table_pg[i];
						v2 = phy_regarray_table_pg[i+1];
						v3 = phy_regarray_table_pg[i+2];
					}
				}
			}
		}
	} else {
		RT_TRACE(rtlpriv, COMP_SEND, DBG_TRACE,
			 "configtype != BaseBand_Config_PHY_REG\n");
	}
	return true;
}

bool rtl8723be_phy_config_rf_with_headerfile(struct ieee80211_hw *hw,
					     enum radio_path rfpath)
{
	#define READ_NEXT_RF_PAIR(v1, v2, i) \
		do { \
			i += 2; \
			v1 = radioa_array_table[i]; \
			v2 = radioa_array_table[i+1]; \
		} while (0)

	int i;
	bool rtstatus = true;
	u32 *radioa_array_table;
	u16 radioa_arraylen;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u32 v1 = 0, v2 = 0;

	radioa_arraylen = RTL8723BE_RADIOA_1TARRAYLEN;
	radioa_array_table = RTL8723BE_RADIOA_1TARRAY;
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "Radio_A:RTL8723BE_RADIOA_1TARRAY %d\n", radioa_arraylen);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "Radio No %x\n", rfpath);
	rtstatus = true;
	switch (rfpath) {
	case RF90_PATH_A:
		for (i = 0; i < radioa_arraylen; i = i + 2) {
			v1 = radioa_array_table[i];
			v2 = radioa_array_table[i+1];
			if (v1 < 0xcdcdcdcd) {
				_rtl8723be_config_rf_radio_a(hw, v1, v2);
			} else { /*This line is the start line of branch.*/
				if (!_rtl8723be_check_condition(hw,
						radioa_array_table[i])) {
					/* Discard the following
					 * (offset, data) pairs
					 */
					READ_NEXT_RF_PAIR(v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD &&
					       i < radioa_arraylen - 2)
						READ_NEXT_RF_PAIR(v1, v2, i);
					i -= 2; /* prevent from for-loop += 2*/
				} else {
					/* Configure matched pairs
					 * and skip to end of if-else.
					 */
					READ_NEXT_RF_PAIR(v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD &&
					       i < radioa_arraylen - 2) {
						_rtl8723be_config_rf_radio_a(hw,
									v1, v2);
						READ_NEXT_RF_PAIR(v1, v2, i);
					}

					while (v2 != 0xDEAD &&
					       i < radioa_arraylen - 2) {
						READ_NEXT_RF_PAIR(v1, v2, i);
					}
				}
			}
		}

		if (rtlhal->oem_id == RT_CID_819X_HP)
			_rtl8723be_config_rf_radio_a(hw, 0x52, 0x7E4BD);

		break;
	case RF90_PATH_B:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "switch case not process\n");
		break;
	case RF90_PATH_C:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "switch case not process\n");
		break;
	case RF90_PATH_D:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "switch case not process\n");
		break;
	}
	return true;
}

void rtl8723be_phy_get_hw_reg_originalvalue(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);

	rtlphy->default_initialgain[0] =
	    (u8) rtl_get_bbreg(hw, ROFDM0_XAAGCCORE1, MASKBYTE0);
	rtlphy->default_initialgain[1] =
	    (u8) rtl_get_bbreg(hw, ROFDM0_XBAGCCORE1, MASKBYTE0);
	rtlphy->default_initialgain[2] =
	    (u8) rtl_get_bbreg(hw, ROFDM0_XCAGCCORE1, MASKBYTE0);
	rtlphy->default_initialgain[3] =
	    (u8) rtl_get_bbreg(hw, ROFDM0_XDAGCCORE1, MASKBYTE0);

	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
		 "Default initial gain (c50 = 0x%x, "
		  "c58 = 0x%x, c60 = 0x%x, c68 = 0x%x\n",
		  rtlphy->default_initialgain[0],
		  rtlphy->default_initialgain[1],
		  rtlphy->default_initialgain[2],
		  rtlphy->default_initialgain[3]);

	rtlphy->framesync = (u8) rtl_get_bbreg(hw, ROFDM0_RXDETECTOR3,
					       MASKBYTE0);
	rtlphy->framesync_c34 = rtl_get_bbreg(hw, ROFDM0_RXDETECTOR2,
					      MASKDWORD);

	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
		 "Default framesync (0x%x) = 0x%x\n",
		  ROFDM0_RXDETECTOR3, rtlphy->framesync);
}

void rtl8723be_phy_get_txpower_level(struct ieee80211_hw *hw, long *powerlevel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u8 txpwr_level;
	long txpwr_dbm;

	txpwr_level = rtlphy->cur_cck_txpwridx;
	txpwr_dbm = rtl8723_phy_txpwr_idx_to_dbm(hw, WIRELESS_MODE_B,
						 txpwr_level);
	txpwr_level = rtlphy->cur_ofdm24g_txpwridx;
	if (rtl8723_phy_txpwr_idx_to_dbm(hw, WIRELESS_MODE_G, txpwr_level) >
	    txpwr_dbm)
		txpwr_dbm =
		    rtl8723_phy_txpwr_idx_to_dbm(hw, WIRELESS_MODE_G,
						 txpwr_level);
	txpwr_level = rtlphy->cur_ofdm24g_txpwridx;
	if (rtl8723_phy_txpwr_idx_to_dbm(hw, WIRELESS_MODE_N_24G,
					 txpwr_level) > txpwr_dbm)
		txpwr_dbm =
		    rtl8723_phy_txpwr_idx_to_dbm(hw, WIRELESS_MODE_N_24G,
						 txpwr_level);
	*powerlevel = txpwr_dbm;
}

static u8 _rtl8723be_phy_get_ratesection_intxpower_byrate(enum radio_path path,
							  u8 rate)
{
	u8 rate_section = 0;

	switch (rate) {
	case DESC92C_RATE1M:
		rate_section = 2;
		break;
	case DESC92C_RATE2M:
	case DESC92C_RATE5_5M:
		if (path == RF90_PATH_A)
			rate_section = 3;
		else if (path == RF90_PATH_B)
			rate_section = 2;
		break;
	case DESC92C_RATE11M:
		rate_section = 3;
		break;
	case DESC92C_RATE6M:
	case DESC92C_RATE9M:
	case DESC92C_RATE12M:
	case DESC92C_RATE18M:
		rate_section = 0;
		break;
	case DESC92C_RATE24M:
	case DESC92C_RATE36M:
	case DESC92C_RATE48M:
	case DESC92C_RATE54M:
		rate_section = 1;
		break;
	case DESC92C_RATEMCS0:
	case DESC92C_RATEMCS1:
	case DESC92C_RATEMCS2:
	case DESC92C_RATEMCS3:
		rate_section = 4;
		break;
	case DESC92C_RATEMCS4:
	case DESC92C_RATEMCS5:
	case DESC92C_RATEMCS6:
	case DESC92C_RATEMCS7:
		rate_section = 5;
		break;
	case DESC92C_RATEMCS8:
	case DESC92C_RATEMCS9:
	case DESC92C_RATEMCS10:
	case DESC92C_RATEMCS11:
		rate_section = 6;
		break;
	case DESC92C_RATEMCS12:
	case DESC92C_RATEMCS13:
	case DESC92C_RATEMCS14:
	case DESC92C_RATEMCS15:
		rate_section = 7;
		break;
	default:
		RT_ASSERT(true, "Rate_Section is Illegal\n");
		break;
	}
	return rate_section;
}

static u8 _rtl8723be_get_txpower_by_rate(struct ieee80211_hw *hw,
					 enum band_type band,
					 enum radio_path rfpath, u8 rate)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u8 shift = 0, rate_section, tx_num;
	char tx_pwr_diff = 0;

	rate_section = _rtl8723be_phy_get_ratesection_intxpower_byrate(rfpath,
								       rate);
	tx_num = RF_TX_NUM_NONIMPLEMENT;

	if (tx_num == RF_TX_NUM_NONIMPLEMENT) {
		if (rate >= DESC92C_RATEMCS8 && rate <= DESC92C_RATEMCS15)
			tx_num = RF_2TX;
		else
			tx_num = RF_1TX;
	}

	switch (rate) {
	case DESC92C_RATE6M:
	case DESC92C_RATE24M:
	case DESC92C_RATEMCS0:
	case DESC92C_RATEMCS4:
	case DESC92C_RATEMCS8:
	case DESC92C_RATEMCS12:
		shift = 0;
		break;
	case DESC92C_RATE1M:
	case DESC92C_RATE2M:
	case DESC92C_RATE9M:
	case DESC92C_RATE36M:
	case DESC92C_RATEMCS1:
	case DESC92C_RATEMCS5:
	case DESC92C_RATEMCS9:
	case DESC92C_RATEMCS13:
		shift = 8;
		break;
	case DESC92C_RATE5_5M:
	case DESC92C_RATE12M:
	case DESC92C_RATE48M:
	case DESC92C_RATEMCS2:
	case DESC92C_RATEMCS6:
	case DESC92C_RATEMCS10:
	case DESC92C_RATEMCS14:
		shift = 16;
		break;
	case DESC92C_RATE11M:
	case DESC92C_RATE18M:
	case DESC92C_RATE54M:
	case DESC92C_RATEMCS3:
	case DESC92C_RATEMCS7:
	case DESC92C_RATEMCS11:
	case DESC92C_RATEMCS15:
		shift = 24;
		break;
	default:
		RT_ASSERT(true, "Rate_Section is Illegal\n");
		break;
	}
	tx_pwr_diff = (u8)(rtlphy->tx_power_by_rate_offset[band][rfpath][tx_num]
					  [rate_section] >> shift) & 0xff;

	return	tx_pwr_diff;
}

static u8 _rtl8723be_get_txpower_index(struct ieee80211_hw *hw, u8 path,
				       u8 rate, u8 bandwidth, u8 channel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u8 index = (channel - 1);
	u8 txpower;
	u8 power_diff_byrate = 0;

	if (channel > 14 || channel < 1) {
		index = 0;
		RT_TRACE(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
			 "Illegal channel!\n");
	}
	if (RTL8723E_RX_HAL_IS_CCK_RATE(rate))
		txpower = rtlefuse->txpwrlevel_cck[path][index];
	else if (DESC92C_RATE6M <= rate)
		txpower = rtlefuse->txpwrlevel_ht40_1s[path][index];
	else
		RT_TRACE(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
			 "invalid rate\n");

	if (DESC92C_RATE6M <= rate && rate <= DESC92C_RATE54M &&
	    !RTL8723E_RX_HAL_IS_CCK_RATE(rate))
		txpower += rtlefuse->txpwr_legacyhtdiff[0][TX_1S];

	if (bandwidth == HT_CHANNEL_WIDTH_20) {
		if (DESC92C_RATEMCS0 <= rate && rate <= DESC92C_RATEMCS15)
			txpower += rtlefuse->txpwr_ht20diff[0][TX_1S];
		if (DESC92C_RATEMCS8 <= rate && rate <= DESC92C_RATEMCS15)
			txpower += rtlefuse->txpwr_ht20diff[0][TX_2S];
	} else if (bandwidth == HT_CHANNEL_WIDTH_20_40) {
		if (DESC92C_RATEMCS0 <= rate && rate <= DESC92C_RATEMCS15)
			txpower += rtlefuse->txpwr_ht40diff[0][TX_1S];
		if (DESC92C_RATEMCS8 <= rate && rate <= DESC92C_RATEMCS15)
			txpower += rtlefuse->txpwr_ht40diff[0][TX_2S];
	}
	if (rtlefuse->eeprom_regulatory != 2)
		power_diff_byrate = _rtl8723be_get_txpower_by_rate(hw,
								   BAND_ON_2_4G,
								   path, rate);

	txpower += power_diff_byrate;

	if (txpower > MAX_POWER_INDEX)
		txpower = MAX_POWER_INDEX;

	return txpower;
}

static void _rtl8723be_phy_set_txpower_index(struct ieee80211_hw *hw,
					     u8 power_index, u8 path, u8 rate)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	if (path == RF90_PATH_A) {
		switch (rate) {
		case DESC92C_RATE1M:
			rtl8723_phy_set_bb_reg(hw, RTXAGC_A_CCK1_MCS32,
					       MASKBYTE1, power_index);
			break;
		case DESC92C_RATE2M:
			rtl8723_phy_set_bb_reg(hw, RTXAGC_B_CCK11_A_CCK2_11,
					       MASKBYTE1, power_index);
			break;
		case DESC92C_RATE5_5M:
			rtl8723_phy_set_bb_reg(hw, RTXAGC_B_CCK11_A_CCK2_11,
					       MASKBYTE2, power_index);
			break;
		case DESC92C_RATE11M:
			rtl8723_phy_set_bb_reg(hw, RTXAGC_B_CCK11_A_CCK2_11,
					       MASKBYTE3, power_index);
			break;
		case DESC92C_RATE6M:
			rtl8723_phy_set_bb_reg(hw, RTXAGC_A_RATE18_06,
					       MASKBYTE0, power_index);
			break;
		case DESC92C_RATE9M:
			rtl8723_phy_set_bb_reg(hw, RTXAGC_A_RATE18_06,
					       MASKBYTE1, power_index);
			break;
		case DESC92C_RATE12M:
			rtl8723_phy_set_bb_reg(hw, RTXAGC_A_RATE18_06,
					       MASKBYTE2, power_index);
			break;
		case DESC92C_RATE18M:
			rtl8723_phy_set_bb_reg(hw, RTXAGC_A_RATE18_06,
					       MASKBYTE3, power_index);
			break;
		case DESC92C_RATE24M:
			rtl8723_phy_set_bb_reg(hw, RTXAGC_A_RATE54_24,
					       MASKBYTE0, power_index);
			break;
		case DESC92C_RATE36M:
			rtl8723_phy_set_bb_reg(hw, RTXAGC_A_RATE54_24,
					       MASKBYTE1, power_index);
			break;
		case DESC92C_RATE48M:
			rtl8723_phy_set_bb_reg(hw, RTXAGC_A_RATE54_24,
					       MASKBYTE2, power_index);
			break;
		case DESC92C_RATE54M:
			rtl8723_phy_set_bb_reg(hw, RTXAGC_A_RATE54_24,
					       MASKBYTE3, power_index);
			break;
		case DESC92C_RATEMCS0:
			rtl8723_phy_set_bb_reg(hw, RTXAGC_A_MCS03_MCS00,
					       MASKBYTE0, power_index);
			break;
		case DESC92C_RATEMCS1:
			rtl8723_phy_set_bb_reg(hw, RTXAGC_A_MCS03_MCS00,
					       MASKBYTE1, power_index);
			break;
		case DESC92C_RATEMCS2:
			rtl8723_phy_set_bb_reg(hw, RTXAGC_A_MCS03_MCS00,
					       MASKBYTE2, power_index);
			break;
		case DESC92C_RATEMCS3:
			rtl8723_phy_set_bb_reg(hw, RTXAGC_A_MCS03_MCS00,
					       MASKBYTE3, power_index);
			break;
		case DESC92C_RATEMCS4:
			rtl8723_phy_set_bb_reg(hw, RTXAGC_A_MCS07_MCS04,
					       MASKBYTE0, power_index);
			break;
		case DESC92C_RATEMCS5:
			rtl8723_phy_set_bb_reg(hw, RTXAGC_A_MCS07_MCS04,
					       MASKBYTE1, power_index);
			break;
		case DESC92C_RATEMCS6:
			rtl8723_phy_set_bb_reg(hw, RTXAGC_A_MCS07_MCS04,
					       MASKBYTE2, power_index);
			break;
		case DESC92C_RATEMCS7:
			rtl8723_phy_set_bb_reg(hw, RTXAGC_A_MCS07_MCS04,
					       MASKBYTE3, power_index);
			break;
		case DESC92C_RATEMCS8:
			rtl8723_phy_set_bb_reg(hw, RTXAGC_A_MCS11_MCS08,
					       MASKBYTE0, power_index);
			break;
		case DESC92C_RATEMCS9:
			rtl8723_phy_set_bb_reg(hw, RTXAGC_A_MCS11_MCS08,
					       MASKBYTE1, power_index);
			break;
		case DESC92C_RATEMCS10:
			rtl8723_phy_set_bb_reg(hw, RTXAGC_A_MCS11_MCS08,
					       MASKBYTE2, power_index);
			break;
		case DESC92C_RATEMCS11:
			rtl8723_phy_set_bb_reg(hw, RTXAGC_A_MCS11_MCS08,
					       MASKBYTE3, power_index);
			break;
		default:
			RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
				 "Invalid Rate!!\n");
			break;
		}
	} else {
		RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD, "Invalid RFPath!!\n");
	}
}

void rtl8723be_phy_set_txpower_level(struct ieee80211_hw *hw, u8 channel)
{
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u8 cck_rates[]  = {DESC92C_RATE1M, DESC92C_RATE2M,
			   DESC92C_RATE5_5M, DESC92C_RATE11M};
	u8 ofdm_rates[]  = {DESC92C_RATE6M, DESC92C_RATE9M,
			    DESC92C_RATE12M, DESC92C_RATE18M,
			    DESC92C_RATE24M, DESC92C_RATE36M,
			    DESC92C_RATE48M, DESC92C_RATE54M};
	u8 ht_rates_1t[]  = {DESC92C_RATEMCS0, DESC92C_RATEMCS1,
			     DESC92C_RATEMCS2, DESC92C_RATEMCS3,
			     DESC92C_RATEMCS4, DESC92C_RATEMCS5,
			     DESC92C_RATEMCS6, DESC92C_RATEMCS7};
	u8 i, size;
	u8 power_index;

	if (!rtlefuse->txpwr_fromeprom)
		return;

	size = sizeof(cck_rates) / sizeof(u8);
	for (i = 0; i < size; i++) {
		power_index = _rtl8723be_get_txpower_index(hw, RF90_PATH_A,
					cck_rates[i],
					rtl_priv(hw)->phy.current_chan_bw,
					channel);
		_rtl8723be_phy_set_txpower_index(hw, power_index, RF90_PATH_A,
						 cck_rates[i]);
	}
	size = sizeof(ofdm_rates) / sizeof(u8);
	for (i = 0; i < size; i++) {
		power_index = _rtl8723be_get_txpower_index(hw, RF90_PATH_A,
					ofdm_rates[i],
					rtl_priv(hw)->phy.current_chan_bw,
					channel);
		_rtl8723be_phy_set_txpower_index(hw, power_index, RF90_PATH_A,
						 ofdm_rates[i]);
	}
	size = sizeof(ht_rates_1t) / sizeof(u8);
	for (i = 0; i < size; i++) {
		power_index = _rtl8723be_get_txpower_index(hw, RF90_PATH_A,
					ht_rates_1t[i],
					rtl_priv(hw)->phy.current_chan_bw,
					channel);
		_rtl8723be_phy_set_txpower_index(hw, power_index, RF90_PATH_A,
						 ht_rates_1t[i]);
	}
}

void rtl8723be_phy_scan_operation_backup(struct ieee80211_hw *hw, u8 operation)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	enum io_type iotype;

	if (!is_hal_stop(rtlhal)) {
		switch (operation) {
		case SCAN_OPT_BACKUP:
			iotype = IO_CMD_PAUSE_DM_BY_SCAN;
			rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_IO_CMD,
						      (u8 *)&iotype);
			break;
		case SCAN_OPT_RESTORE:
			iotype = IO_CMD_RESUME_DM_BY_SCAN;
			rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_IO_CMD,
						      (u8 *)&iotype);
			break;
		default:
			RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
				 "Unknown Scan Backup operation.\n");
			break;
		}
	}
}

void rtl8723be_phy_set_bw_mode_callback(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u8 reg_bw_opmode;
	u8 reg_prsr_rsc;

	RT_TRACE(rtlpriv, COMP_SCAN, DBG_TRACE,
		 "Switch to %s bandwidth\n",
		 rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_20 ?
		 "20MHz" : "40MHz");

	if (is_hal_stop(rtlhal)) {
		rtlphy->set_bwmode_inprogress = false;
		return;
	}

	reg_bw_opmode = rtl_read_byte(rtlpriv, REG_BWOPMODE);
	reg_prsr_rsc = rtl_read_byte(rtlpriv, REG_RRSR + 2);

	switch (rtlphy->current_chan_bw) {
	case HT_CHANNEL_WIDTH_20:
		reg_bw_opmode |= BW_OPMODE_20MHZ;
		rtl_write_byte(rtlpriv, REG_BWOPMODE, reg_bw_opmode);
		break;
	case HT_CHANNEL_WIDTH_20_40:
		reg_bw_opmode &= ~BW_OPMODE_20MHZ;
		rtl_write_byte(rtlpriv, REG_BWOPMODE, reg_bw_opmode);
		reg_prsr_rsc = (reg_prsr_rsc & 0x90) |
			       (mac->cur_40_prime_sc << 5);
		rtl_write_byte(rtlpriv, REG_RRSR + 2, reg_prsr_rsc);
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "unknown bandwidth: %#X\n", rtlphy->current_chan_bw);
		break;
	}

	switch (rtlphy->current_chan_bw) {
	case HT_CHANNEL_WIDTH_20:
		rtl_set_bbreg(hw, RFPGA0_RFMOD, BRFMOD, 0x0);
		rtl_set_bbreg(hw, RFPGA1_RFMOD, BRFMOD, 0x0);
		break;
	case HT_CHANNEL_WIDTH_20_40:
		rtl_set_bbreg(hw, RFPGA0_RFMOD, BRFMOD, 0x1);
		rtl_set_bbreg(hw, RFPGA1_RFMOD, BRFMOD, 0x1);
		rtl_set_bbreg(hw, RCCK0_SYSTEM, BCCK_SIDEBAND,
			      (mac->cur_40_prime_sc >> 1));
		rtl_set_bbreg(hw, ROFDM1_LSTF, 0xC00, mac->cur_40_prime_sc);
		rtl_set_bbreg(hw, 0x818, (BIT(26) | BIT(27)),
			      (mac->cur_40_prime_sc ==
			       HAL_PRIME_CHNL_OFFSET_LOWER) ? 2 : 1);
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "unknown bandwidth: %#X\n", rtlphy->current_chan_bw);
		break;
	}
	rtl8723be_phy_rf6052_set_bandwidth(hw, rtlphy->current_chan_bw);
	rtlphy->set_bwmode_inprogress = false;
	RT_TRACE(rtlpriv, COMP_SCAN, DBG_LOUD, "\n");
}

void rtl8723be_phy_set_bw_mode(struct ieee80211_hw *hw,
			    enum nl80211_channel_type ch_type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 tmp_bw = rtlphy->current_chan_bw;

	if (rtlphy->set_bwmode_inprogress)
		return;
	rtlphy->set_bwmode_inprogress = true;
	if ((!is_hal_stop(rtlhal)) && !(RT_CANNOT_IO(hw))) {
		rtl8723be_phy_set_bw_mode_callback(hw);
	} else {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 "false driver sleep or unload\n");
		rtlphy->set_bwmode_inprogress = false;
		rtlphy->current_chan_bw = tmp_bw;
	}
}

void rtl8723be_phy_sw_chnl_callback(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u32 delay;

	RT_TRACE(rtlpriv, COMP_SCAN, DBG_TRACE,
		 "switch to channel%d\n", rtlphy->current_channel);
	if (is_hal_stop(rtlhal))
		return;
	do {
		if (!rtlphy->sw_chnl_inprogress)
			break;
		if (!rtl8723be_phy_sw_chn_step_by_step(hw,
						       rtlphy->current_channel,
						       &rtlphy->sw_chnl_stage,
						       &rtlphy->sw_chnl_step,
						       &delay)) {
			if (delay > 0)
				mdelay(delay);
			else
				continue;
		} else {
			rtlphy->sw_chnl_inprogress = false;
		}
		break;
	} while (true);
	RT_TRACE(rtlpriv, COMP_SCAN, DBG_TRACE, "\n");
}

u8 rtl8723be_phy_sw_chnl(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	if (rtlphy->sw_chnl_inprogress)
		return 0;
	if (rtlphy->set_bwmode_inprogress)
		return 0;
	RT_ASSERT((rtlphy->current_channel <= 14),
		  "WIRELESS_MODE_G but channel>14");
	rtlphy->sw_chnl_inprogress = true;
	rtlphy->sw_chnl_stage = 0;
	rtlphy->sw_chnl_step = 0;
	if (!(is_hal_stop(rtlhal)) && !(RT_CANNOT_IO(hw))) {
		rtl8723be_phy_sw_chnl_callback(hw);
		RT_TRACE(rtlpriv, COMP_CHAN, DBG_LOUD,
			 "sw_chnl_inprogress false schdule "
			  "workitem current channel %d\n",
			  rtlphy->current_channel);
		rtlphy->sw_chnl_inprogress = false;
	} else {
		RT_TRACE(rtlpriv, COMP_CHAN, DBG_LOUD,
			 "sw_chnl_inprogress false driver sleep or"
			  " unload\n");
		rtlphy->sw_chnl_inprogress = false;
	}
	return 1;
}

static bool rtl8723be_phy_sw_chn_step_by_step(struct ieee80211_hw *hw,
					      u8 channel, u8 *stage,
					      u8 *step, u32 *delay)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct swchnlcmd precommoncmd[MAX_PRECMD_CNT];
	u32 precommoncmdcnt;
	struct swchnlcmd postcommoncmd[MAX_POSTCMD_CNT];
	u32 postcommoncmdcnt;
	struct swchnlcmd rfdependcmd[MAX_RFDEPENDCMD_CNT];
	u32 rfdependcmdcnt;
	struct swchnlcmd *currentcmd = NULL;
	u8 rfpath;
	u8 num_total_rfpath = rtlphy->num_total_rfpath;

	precommoncmdcnt = 0;
	rtl8723_phy_set_sw_chnl_cmdarray(precommoncmd, precommoncmdcnt++,
					 MAX_PRECMD_CNT,
					 CMDID_SET_TXPOWEROWER_LEVEL,
					 0, 0, 0);
	rtl8723_phy_set_sw_chnl_cmdarray(precommoncmd, precommoncmdcnt++,
					 MAX_PRECMD_CNT, CMDID_END, 0, 0, 0);
	postcommoncmdcnt = 0;
	rtl8723_phy_set_sw_chnl_cmdarray(postcommoncmd, postcommoncmdcnt++,
					 MAX_POSTCMD_CNT, CMDID_END,
					 0, 0, 0);
	rfdependcmdcnt = 0;

	RT_ASSERT((channel >= 1 && channel <= 14),
		  "illegal channel for Zebra: %d\n", channel);

	rtl8723_phy_set_sw_chnl_cmdarray(rfdependcmd, rfdependcmdcnt++,
					 MAX_RFDEPENDCMD_CNT,
					 CMDID_RF_WRITEREG,
					 RF_CHNLBW, channel, 10);

	rtl8723_phy_set_sw_chnl_cmdarray(rfdependcmd, rfdependcmdcnt++,
					 MAX_RFDEPENDCMD_CNT,
					 CMDID_END, 0, 0, 0);

	do {
		switch (*stage) {
		case 0:
			currentcmd = &precommoncmd[*step];
			break;
		case 1:
			currentcmd = &rfdependcmd[*step];
			break;
		case 2:
			currentcmd = &postcommoncmd[*step];
			break;
		}

		if (currentcmd->cmdid == CMDID_END) {
			if ((*stage) == 2) {
				return true;
			} else {
				(*stage)++;
				(*step) = 0;
				continue;
			}
		}

		switch (currentcmd->cmdid) {
		case CMDID_SET_TXPOWEROWER_LEVEL:
			rtl8723be_phy_set_txpower_level(hw, channel);
			break;
		case CMDID_WRITEPORT_ULONG:
			rtl_write_dword(rtlpriv, currentcmd->para1,
					currentcmd->para2);
			break;
		case CMDID_WRITEPORT_USHORT:
			rtl_write_word(rtlpriv, currentcmd->para1,
				       (u16) currentcmd->para2);
			break;
		case CMDID_WRITEPORT_UCHAR:
			rtl_write_byte(rtlpriv, currentcmd->para1,
				       (u8) currentcmd->para2);
			break;
		case CMDID_RF_WRITEREG:
			for (rfpath = 0; rfpath < num_total_rfpath; rfpath++) {
				rtlphy->rfreg_chnlval[rfpath] =
				    ((rtlphy->rfreg_chnlval[rfpath] &
				      0xfffffc00) | currentcmd->para2);

				rtl_set_rfreg(hw, (enum radio_path)rfpath,
					      currentcmd->para1,
					      RFREG_OFFSET_MASK,
					      rtlphy->rfreg_chnlval[rfpath]);
			}
			break;
		default:
			RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
				 "switch case not process\n");
			break;
		}

		break;
	} while (true);

	(*delay) = currentcmd->msdelay;
	(*step)++;
	return false;
}

static u8 _rtl8723be_phy_path_a_iqk(struct ieee80211_hw *hw, bool config_pathb)
{
	u32 reg_eac, reg_e94, reg_e9c, reg_ea4;
	u8 result = 0x00;

	rtl_set_bbreg(hw, 0xe30, MASKDWORD, 0x10008c1c);
	rtl_set_bbreg(hw, 0xe34, MASKDWORD, 0x30008c1c);
	rtl_set_bbreg(hw, 0xe38, MASKDWORD, 0x8214032a);
	rtl_set_bbreg(hw, 0xe3c, MASKDWORD, 0x28160000);

	rtl_set_bbreg(hw, 0xe4c, MASKDWORD, 0x00462911);
	rtl_set_bbreg(hw, 0xe48, MASKDWORD, 0xf9000000);
	rtl_set_bbreg(hw, 0xe48, MASKDWORD, 0xf8000000);

	mdelay(IQK_DELAY_TIME);

	reg_eac = rtl_get_bbreg(hw, 0xeac, MASKDWORD);
	reg_e94 = rtl_get_bbreg(hw, 0xe94, MASKDWORD);
	reg_e9c = rtl_get_bbreg(hw, 0xe9c, MASKDWORD);
	reg_ea4 = rtl_get_bbreg(hw, 0xea4, MASKDWORD);

	if (!(reg_eac & BIT(28)) &&
	    (((reg_e94 & 0x03FF0000) >> 16) != 0x142) &&
	    (((reg_e9c & 0x03FF0000) >> 16) != 0x42))
		result |= 0x01;
	return result;
}

static bool phy_similarity_cmp(struct ieee80211_hw *hw, long result[][8],
			       u8 c1, u8 c2)
{
	u32 i, j, diff, simularity_bitmap, bound;
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	u8 final_candidate[2] = { 0xFF, 0xFF };
	bool bresult = true, is2t = IS_92C_SERIAL(rtlhal->version);

	if (is2t)
		bound = 8;
	else
		bound = 4;

	simularity_bitmap = 0;

	for (i = 0; i < bound; i++) {
		diff = (result[c1][i] > result[c2][i]) ?
		    (result[c1][i] - result[c2][i]) :
		    (result[c2][i] - result[c1][i]);

		if (diff > MAX_TOLERANCE) {
			if ((i == 2 || i == 6) && !simularity_bitmap) {
				if (result[c1][i] + result[c1][i + 1] == 0)
					final_candidate[(i / 4)] = c2;
				else if (result[c2][i] + result[c2][i + 1] == 0)
					final_candidate[(i / 4)] = c1;
				else
					simularity_bitmap |= (1 << i);
			} else {
				simularity_bitmap |= (1 << i);
			}
		}
	}

	if (simularity_bitmap == 0) {
		for (i = 0; i < (bound / 4); i++) {
			if (final_candidate[i] != 0xFF) {
				for (j = i * 4; j < (i + 1) * 4 - 2; j++)
					result[3][j] =
						result[final_candidate[i]][j];
				bresult = false;
			}
		}
		return bresult;
	} else if (!(simularity_bitmap & 0x0F)) {
		for (i = 0; i < 4; i++)
			result[3][i] = result[c1][i];
		return false;
	} else if (!(simularity_bitmap & 0xF0) && is2t) {
		for (i = 4; i < 8; i++)
			result[3][i] = result[c1][i];
		return false;
	} else {
		return false;
	}
}

static void _rtl8723be_phy_iq_calibrate(struct ieee80211_hw *hw,
					long result[][8], u8 t, bool is2t)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u32 i;
	u8 patha_ok;
	u32 adda_reg[IQK_ADDA_REG_NUM] = {
		0x85c, 0xe6c, 0xe70, 0xe74,
		0xe78, 0xe7c, 0xe80, 0xe84,
		0xe88, 0xe8c, 0xed0, 0xed4,
		0xed8, 0xedc, 0xee0, 0xeec
	};

	u32 iqk_mac_reg[IQK_MAC_REG_NUM] = {
		0x522, 0x550, 0x551, 0x040
	};
	u32 iqk_bb_reg[IQK_BB_REG_NUM] = {
		ROFDM0_TRXPATHENABLE, ROFDM0_TRMUXPAR,
		RFPGA0_XCD_RFINTERFACESW, 0xb68, 0xb6c,
		0x870, 0x860,
		0x864, 0x800
	};
	const u32 retrycount = 2;
	u32 path_sel_bb, path_sel_rf;
	u8 tmp_reg_c50, tmp_reg_c58;

	tmp_reg_c50 = rtl_get_bbreg(hw, 0xc50, MASKBYTE0);
	tmp_reg_c58 = rtl_get_bbreg(hw, 0xc58, MASKBYTE0);

	if (t == 0) {
		rtl8723_save_adda_registers(hw, adda_reg,
					    rtlphy->adda_backup, 16);
		rtl8723_phy_save_mac_registers(hw, iqk_mac_reg,
					       rtlphy->iqk_mac_backup);
		rtl8723_save_adda_registers(hw, iqk_bb_reg,
					    rtlphy->iqk_bb_backup,
					    IQK_BB_REG_NUM);
	}
	rtl8723_phy_path_adda_on(hw, adda_reg, true, is2t);
	if (t == 0) {
		rtlphy->rfpi_enable = (u8) rtl_get_bbreg(hw,
						RFPGA0_XA_HSSIPARAMETER1,
						BIT(8));
	}
	if (!rtlphy->rfpi_enable)
		rtl8723_phy_pi_mode_switch(hw, true);

	path_sel_bb = rtl_get_bbreg(hw, 0x948, MASKDWORD);
	path_sel_rf = rtl_get_rfreg(hw, RF90_PATH_A, 0xb0, 0xfffff);

	/*BB Setting*/
	rtl_set_bbreg(hw, 0x800, BIT(24), 0x00);
	rtl_set_bbreg(hw, 0xc04, MASKDWORD, 0x03a05600);
	rtl_set_bbreg(hw, 0xc08, MASKDWORD, 0x000800e4);
	rtl_set_bbreg(hw, 0x874, MASKDWORD, 0x22204000);

	rtl_set_bbreg(hw, 0x870, BIT(10), 0x01);
	rtl_set_bbreg(hw, 0x870, BIT(26), 0x01);
	rtl_set_bbreg(hw, 0x860, BIT(10), 0x00);
	rtl_set_bbreg(hw, 0x864, BIT(10), 0x00);

	if (is2t)
		rtl_set_rfreg(hw, RF90_PATH_B, 0x00, MASKDWORD, 0x10000);
	rtl8723_phy_mac_setting_calibration(hw, iqk_mac_reg,
					    rtlphy->iqk_mac_backup);
	rtl_set_bbreg(hw, 0xb68, MASKDWORD, 0x0f600000);

	rtl_set_bbreg(hw, 0xe28, MASKDWORD, 0x80800000);
	rtl_set_bbreg(hw, 0xe40, MASKDWORD, 0x01007c00);
	rtl_set_bbreg(hw, 0xe44, MASKDWORD, 0x81004800);
	for (i = 0; i < retrycount; i++) {
		patha_ok = _rtl8723be_phy_path_a_iqk(hw, is2t);
		if (patha_ok == 0x01) {
			RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
				 "Path A Tx IQK Success!!\n");
			result[t][0] = (rtl_get_bbreg(hw, 0xe94, MASKDWORD) &
					0x3FF0000) >> 16;
			result[t][1] = (rtl_get_bbreg(hw, 0xe9c, MASKDWORD) &
					0x3FF0000) >> 16;
			break;
		}
	}

	if (0 == patha_ok)
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Path A IQK Success!!\n");
	if (is2t) {
		rtl8723_phy_path_a_standby(hw);
		rtl8723_phy_path_adda_on(hw, adda_reg, false, is2t);
	}

	rtl_set_bbreg(hw, 0xe28, MASKDWORD, 0);

	if (t != 0) {
		if (!rtlphy->rfpi_enable)
			rtl8723_phy_pi_mode_switch(hw, false);
		rtl8723_phy_reload_adda_registers(hw, adda_reg,
						  rtlphy->adda_backup, 16);
		rtl8723_phy_reload_mac_registers(hw, iqk_mac_reg,
						 rtlphy->iqk_mac_backup);
		rtl8723_phy_reload_adda_registers(hw, iqk_bb_reg,
						  rtlphy->iqk_bb_backup,
						  IQK_BB_REG_NUM);

		rtl_set_bbreg(hw, 0x948, MASKDWORD, path_sel_bb);
		rtl_set_rfreg(hw, RF90_PATH_B, 0xb0, 0xfffff, path_sel_rf);

		rtl_set_bbreg(hw, 0xc50, MASKBYTE0, 0x50);
		rtl_set_bbreg(hw, 0xc50, MASKBYTE0, tmp_reg_c50);
		if (is2t) {
			rtl_set_bbreg(hw, 0xc58, MASKBYTE0, 0x50);
			rtl_set_bbreg(hw, 0xc58, MASKBYTE0, tmp_reg_c58);
		}
		rtl_set_bbreg(hw, 0xe30, MASKDWORD, 0x01008c00);
		rtl_set_bbreg(hw, 0xe34, MASKDWORD, 0x01008c00);
	}
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "8723be IQK Finish!!\n");
}

static void _rtl8723be_phy_lc_calibrate(struct ieee80211_hw *hw, bool is2t)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 tmpreg;
	u32 rf_a_mode = 0, rf_b_mode = 0, lc_cal;

	tmpreg = rtl_read_byte(rtlpriv, 0xd03);

	if ((tmpreg & 0x70) != 0)
		rtl_write_byte(rtlpriv, 0xd03, tmpreg & 0x8F);
	else
		rtl_write_byte(rtlpriv, REG_TXPAUSE, 0xFF);

	if ((tmpreg & 0x70) != 0) {
		rf_a_mode = rtl_get_rfreg(hw, RF90_PATH_A, 0x00, MASK12BITS);

		if (is2t)
			rf_b_mode = rtl_get_rfreg(hw, RF90_PATH_B, 0x00,
						  MASK12BITS);

		rtl_set_rfreg(hw, RF90_PATH_A, 0x00, MASK12BITS,
			      (rf_a_mode & 0x8FFFF) | 0x10000);

		if (is2t)
			rtl_set_rfreg(hw, RF90_PATH_B, 0x00, MASK12BITS,
				      (rf_b_mode & 0x8FFFF) | 0x10000);
	}
	lc_cal = rtl_get_rfreg(hw, RF90_PATH_A, 0x18, MASK12BITS);

	rtl_set_rfreg(hw, RF90_PATH_A, 0xb0, RFREG_OFFSET_MASK, 0xdfbe0);
	rtl_set_rfreg(hw, RF90_PATH_A, 0x18, MASK12BITS, 0x8c0a);

	mdelay(100);

	rtl_set_rfreg(hw, RF90_PATH_A, 0xb0, RFREG_OFFSET_MASK, 0xdffe0);

	if ((tmpreg & 0x70) != 0) {
		rtl_write_byte(rtlpriv, 0xd03, tmpreg);
		rtl_set_rfreg(hw, RF90_PATH_A, 0x00, MASK12BITS, rf_a_mode);

		if (is2t)
			rtl_set_rfreg(hw, RF90_PATH_B, 0x00,
				      MASK12BITS, rf_b_mode);
	} else {
		rtl_write_byte(rtlpriv, REG_TXPAUSE, 0x00);
	}
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "\n");
}

static void _rtl8723be_phy_set_rfpath_switch(struct ieee80211_hw *hw,
					     bool bmain, bool is2t)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "\n");

	if (is_hal_stop(rtlhal)) {
		u8 u1btmp;
		u1btmp = rtl_read_byte(rtlpriv, REG_LEDCFG0);
		rtl_write_byte(rtlpriv, REG_LEDCFG0, u1btmp | BIT(7));
		rtl_set_bbreg(hw, RFPGA0_XAB_RFPARAMETER, BIT(13), 0x01);
	}
	if (is2t) {
		if (bmain)
			rtl_set_bbreg(hw, RFPGA0_XB_RFINTERFACEOE,
				      BIT(5) | BIT(6), 0x1);
		else
			rtl_set_bbreg(hw, RFPGA0_XB_RFINTERFACEOE,
				      BIT(5) | BIT(6), 0x2);
	} else {
		rtl_set_bbreg(hw, RFPGA0_XAB_RFINTERFACESW, BIT(8) | BIT(9), 0);
		rtl_set_bbreg(hw, 0x914, MASKLWORD, 0x0201);

		/* We use the RF definition of MAIN and AUX,
		 * left antenna and right antenna repectively.
		 * Default output at AUX.
		 */
		if (bmain) {
			rtl_set_bbreg(hw, RFPGA0_XA_RFINTERFACEOE,
				      BIT(14) | BIT(13) | BIT(12), 0);
			rtl_set_bbreg(hw, RFPGA0_XB_RFINTERFACEOE,
				      BIT(5) | BIT(4) | BIT(3), 0);
			if (rtlefuse->antenna_div_type == CGCS_RX_HW_ANTDIV)
				rtl_set_bbreg(hw, CONFIG_RAM64X16, BIT(31), 0);
		} else {
			rtl_set_bbreg(hw, RFPGA0_XA_RFINTERFACEOE,
				      BIT(14) | BIT(13) | BIT(12), 1);
			rtl_set_bbreg(hw, RFPGA0_XB_RFINTERFACEOE,
				      BIT(5) | BIT(4) | BIT(3), 1);
			if (rtlefuse->antenna_div_type == CGCS_RX_HW_ANTDIV)
				rtl_set_bbreg(hw, CONFIG_RAM64X16, BIT(31), 1);
		}
	}
}

#undef IQK_ADDA_REG_NUM
#undef IQK_DELAY_TIME

void rtl8723be_phy_iq_calibrate(struct ieee80211_hw *hw, bool recovery)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	long result[4][8];
	u8 i, final_candidate;
	bool patha_ok, pathb_ok;
	long reg_e94, reg_e9c, reg_ea4, reg_eac, reg_eb4, reg_ebc, reg_ec4,
	    reg_ecc, reg_tmp = 0;
	bool is12simular, is13simular, is23simular;
	u32 iqk_bb_reg[9] = {
		ROFDM0_XARXIQIMBALANCE,
		ROFDM0_XBRXIQIMBALANCE,
		ROFDM0_ECCATHRESHOLD,
		ROFDM0_AGCRSSITABLE,
		ROFDM0_XATXIQIMBALANCE,
		ROFDM0_XBTXIQIMBALANCE,
		ROFDM0_XCTXAFE,
		ROFDM0_XDTXAFE,
		ROFDM0_RXIQEXTANTA
	};

	if (recovery) {
		rtl8723_phy_reload_adda_registers(hw, iqk_bb_reg,
						  rtlphy->iqk_bb_backup, 9);
		return;
	}

	for (i = 0; i < 8; i++) {
		result[0][i] = 0;
		result[1][i] = 0;
		result[2][i] = 0;
		result[3][i] = 0;
	}
	final_candidate = 0xff;
	patha_ok = false;
	pathb_ok = false;
	is12simular = false;
	is23simular = false;
	is13simular = false;
	for (i = 0; i < 3; i++) {
		if (get_rf_type(rtlphy) == RF_2T2R)
			_rtl8723be_phy_iq_calibrate(hw, result, i, true);
		else
			_rtl8723be_phy_iq_calibrate(hw, result, i, false);
		if (i == 1) {
			is12simular = phy_similarity_cmp(hw, result, 0, 1);
			if (is12simular) {
				final_candidate = 0;
				break;
			}
		}
		if (i == 2) {
			is13simular = phy_similarity_cmp(hw, result, 0, 2);
			if (is13simular) {
				final_candidate = 0;
				break;
			}
			is23simular = phy_similarity_cmp(hw, result, 1, 2);
			if (is23simular) {
				final_candidate = 1;
			} else {
				for (i = 0; i < 8; i++)
					reg_tmp += result[3][i];

				if (reg_tmp != 0)
					final_candidate = 3;
				else
					final_candidate = 0xFF;
			}
		}
	}
	for (i = 0; i < 4; i++) {
		reg_e94 = result[i][0];
		reg_e9c = result[i][1];
		reg_ea4 = result[i][2];
		reg_eac = result[i][3];
		reg_eb4 = result[i][4];
		reg_ebc = result[i][5];
		reg_ec4 = result[i][6];
		reg_ecc = result[i][7];
	}
	if (final_candidate != 0xff) {
		reg_e94 = result[final_candidate][0];
		rtlphy->reg_e94 = reg_e94;
		reg_e9c = result[final_candidate][1];
		rtlphy->reg_e9c = reg_e9c;
		reg_ea4 = result[final_candidate][2];
		reg_eac = result[final_candidate][3];
		reg_eb4 = result[final_candidate][4];
		rtlphy->reg_eb4 = reg_eb4;
		reg_ebc = result[final_candidate][5];
		rtlphy->reg_ebc = reg_ebc;
		reg_ec4 = result[final_candidate][6];
		reg_ecc = result[final_candidate][7];
		patha_ok = true;
		pathb_ok = true;
	} else {
		rtlphy->reg_e94 = 0x100;
		rtlphy->reg_eb4 = 0x100;
		rtlphy->reg_e9c = 0x0;
		rtlphy->reg_ebc = 0x0;
	}
	if (reg_e94 != 0) /*&&(reg_ea4 != 0) */
		rtl8723_phy_path_a_fill_iqk_matrix(hw, patha_ok, result,
						   final_candidate,
						   (reg_ea4 == 0));
	if (final_candidate != 0xFF) {
		for (i = 0; i < IQK_MATRIX_REG_NUM; i++)
			rtlphy->iqk_matrix[0].value[0][i] =
						result[final_candidate][i];
		rtlphy->iqk_matrix[0].iqk_done = true;
	}
	rtl8723_save_adda_registers(hw, iqk_bb_reg, rtlphy->iqk_bb_backup, 9);
}

void rtl8723be_phy_lc_calibrate(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_hal *rtlhal = &(rtlpriv->rtlhal);
	u32 timeout = 2000, timecount = 0;

	while (rtlpriv->mac80211.act_scanning && timecount < timeout) {
		udelay(50);
		timecount += 50;
	}

	rtlphy->lck_inprogress = true;
	RTPRINT(rtlpriv, FINIT, INIT_EEPROM,
		"LCK:Start!!! currentband %x delay %d ms\n",
		rtlhal->current_bandtype, timecount);

	_rtl8723be_phy_lc_calibrate(hw, false);

	rtlphy->lck_inprogress = false;
}

void rtl23b_phy_ap_calibrate(struct ieee80211_hw *hw, char delta)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);

	if (rtlphy->apk_done)
		return;

	return;
}

void rtl8723be_phy_set_rfpath_switch(struct ieee80211_hw *hw, bool bmain)
{
	_rtl8723be_phy_set_rfpath_switch(hw, bmain, false);
}

static void rtl8723be_phy_set_io(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);

	RT_TRACE(rtlpriv, COMP_CMD, DBG_TRACE,
		 "--->Cmd(%#x), set_io_inprogress(%d)\n",
		  rtlphy->current_io_type, rtlphy->set_io_inprogress);
	switch (rtlphy->current_io_type) {
	case IO_CMD_RESUME_DM_BY_SCAN:
		rtlpriv->dm_digtable.cur_igvalue =
				 rtlphy->initgain_backup.xaagccore1;
		/*rtl92c_dm_write_dig(hw);*/
		rtl8723be_phy_set_txpower_level(hw, rtlphy->current_channel);
		rtl_set_bbreg(hw, RCCK0_CCA, 0xff0000, 0x83);
		break;
	case IO_CMD_PAUSE_DM_BY_SCAN:
		rtlphy->initgain_backup.xaagccore1 =
				 rtlpriv->dm_digtable.cur_igvalue;
		rtlpriv->dm_digtable.cur_igvalue = 0x17;
		rtl_set_bbreg(hw, RCCK0_CCA, 0xff0000, 0x40);
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "switch case not process\n");
		break;
	}
	rtlphy->set_io_inprogress = false;
	RT_TRACE(rtlpriv, COMP_CMD, DBG_TRACE,
		 "(%#x)\n", rtlphy->current_io_type);
}

bool rtl8723be_phy_set_io_cmd(struct ieee80211_hw *hw, enum io_type iotype)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	bool postprocessing = false;

	RT_TRACE(rtlpriv, COMP_CMD, DBG_TRACE,
		 "-->IO Cmd(%#x), set_io_inprogress(%d)\n",
		  iotype, rtlphy->set_io_inprogress);
	do {
		switch (iotype) {
		case IO_CMD_RESUME_DM_BY_SCAN:
			RT_TRACE(rtlpriv, COMP_CMD, DBG_TRACE,
				 "[IO CMD] Resume DM after scan.\n");
			postprocessing = true;
			break;
		case IO_CMD_PAUSE_DM_BY_SCAN:
			RT_TRACE(rtlpriv, COMP_CMD, DBG_TRACE,
				 "[IO CMD] Pause DM before scan.\n");
			postprocessing = true;
			break;
		default:
			RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
				 "switch case not process\n");
			break;
		}
	} while (false);
	if (postprocessing && !rtlphy->set_io_inprogress) {
		rtlphy->set_io_inprogress = true;
		rtlphy->current_io_type = iotype;
	} else {
		return false;
	}
	rtl8723be_phy_set_io(hw);
	RT_TRACE(rtlpriv, COMP_CMD, DBG_TRACE, "IO Type(%#x)\n", iotype);
	return true;
}

static void rtl8723be_phy_set_rf_on(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_write_byte(rtlpriv, REG_SPS0_CTRL, 0x2b);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE3);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE2);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE3);
	rtl_write_byte(rtlpriv, REG_TXPAUSE, 0x00);
}

static void _rtl8723be_phy_set_rf_sleep(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_write_byte(rtlpriv, REG_TXPAUSE, 0xFF);
	rtl_set_rfreg(hw, RF90_PATH_A, 0x00, RFREG_OFFSET_MASK, 0x00);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE2);
	rtl_write_byte(rtlpriv, REG_SPS0_CTRL, 0x22);
}

static bool _rtl8723be_phy_set_rf_power_state(struct ieee80211_hw *hw,
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
		if ((ppsc->rfpwr_state == ERFOFF) &&
		    RT_IN_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC)) {
			bool rtstatus;
			u32 initialize_count = 0;
			do {
				initialize_count++;
				RT_TRACE(rtlpriv, COMP_RF, DBG_DMESG,
					 "IPS Set eRf nic enable\n");
				rtstatus = rtl_ps_enable_nic(hw);
			} while (!rtstatus && (initialize_count < 10));
				RT_CLEAR_PS_LEVEL(ppsc,
						  RT_RF_OFF_LEVL_HALT_NIC);
		} else {
			RT_TRACE(rtlpriv, COMP_RF, DBG_DMESG,
				 "Set ERFON sleeped:%d ms\n",
				  jiffies_to_msecs(jiffies -
						   ppsc->last_sleep_jiffies));
			ppsc->last_awake_jiffies = jiffies;
			rtl8723be_phy_set_rf_on(hw);
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
			if (skb_queue_len(&ring->queue) == 0) {
				queue_id++;
				continue;
			} else {
				RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
					 "eRf Off/Sleep: %d times "
					  "TcbBusyQueue[%d] =%d before "
					  "doze!\n", (i + 1), queue_id,
					  skb_queue_len(&ring->queue));

				udelay(10);
				i++;
			}
			if (i >= MAX_DOZE_WAITING_TIMES_9x) {
				RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
					 "\n ERFSLEEP: %d times "
					  "TcbBusyQueue[%d] = %d !\n",
					  MAX_DOZE_WAITING_TIMES_9x,
					  queue_id,
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
				rtlpriv->cfg->ops->led_control(hw,
							     LED_CTL_POWER_OFF);
			}
		}
		break;
	case ERFSLEEP:
		if (ppsc->rfpwr_state == ERFOFF)
			break;
		for (queue_id = 0, i = 0;
		     queue_id < RTL_PCI_MAX_TX_QUEUE_COUNT;) {
			ring = &pcipriv->dev.tx_ring[queue_id];
			if (skb_queue_len(&ring->queue) == 0) {
				queue_id++;
				continue;
			} else {
				RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
					 "eRf Off/Sleep: %d times "
					  "TcbBusyQueue[%d] =%d before "
					  "doze!\n", (i + 1), queue_id,
					  skb_queue_len(&ring->queue));

				udelay(10);
				i++;
			}
			if (i >= MAX_DOZE_WAITING_TIMES_9x) {
				RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
					 "\n ERFSLEEP: %d times "
					  "TcbBusyQueue[%d] = %d !\n",
					  MAX_DOZE_WAITING_TIMES_9x,
					  queue_id,
					  skb_queue_len(&ring->queue));
				break;
			}
		}
		RT_TRACE(rtlpriv, COMP_RF, DBG_DMESG,
			 "Set ERFSLEEP awaked:%d ms\n",
			  jiffies_to_msecs(jiffies -
					   ppsc->last_awake_jiffies));
		ppsc->last_sleep_jiffies = jiffies;
		_rtl8723be_phy_set_rf_sleep(hw);
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "switch case not process\n");
		bresult = false;
		break;
	}
	if (bresult)
		ppsc->rfpwr_state = rfpwr_state;
	return bresult;
}

bool rtl8723be_phy_set_rf_power_state(struct ieee80211_hw *hw,
				      enum rf_pwrstate rfpwr_state)
{
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));

	bool bresult = false;

	if (rfpwr_state == ppsc->rfpwr_state)
		return bresult;
	bresult = _rtl8723be_phy_set_rf_power_state(hw, rfpwr_state);
	return bresult;
}
