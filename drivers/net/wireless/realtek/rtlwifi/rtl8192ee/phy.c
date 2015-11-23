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
#include "reg.h"
#include "def.h"
#include "phy.h"
#include "rf.h"
#include "dm.h"
#include "table.h"

static u32 _rtl92ee_phy_rf_serial_read(struct ieee80211_hw *hw,
				       enum radio_path rfpath, u32 offset);
static void _rtl92ee_phy_rf_serial_write(struct ieee80211_hw *hw,
					 enum radio_path rfpath, u32 offset,
					 u32 data);
static u32 _rtl92ee_phy_calculate_bit_shift(u32 bitmask);
static bool _rtl92ee_phy_bb8192ee_config_parafile(struct ieee80211_hw *hw);
static bool _rtl92ee_phy_config_mac_with_headerfile(struct ieee80211_hw *hw);
static bool phy_config_bb_with_hdr_file(struct ieee80211_hw *hw,
					u8 configtype);
static bool phy_config_bb_with_pghdrfile(struct ieee80211_hw *hw,
					 u8 configtype);
static void phy_init_bb_rf_register_def(struct ieee80211_hw *hw);
static bool _rtl92ee_phy_set_sw_chnl_cmdarray(struct swchnlcmd *cmdtable,
					      u32 cmdtableidx, u32 cmdtablesz,
					      enum swchnlcmd_id cmdid,
					      u32 para1, u32 para2,
					      u32 msdelay);
static bool _rtl92ee_phy_sw_chnl_step_by_step(struct ieee80211_hw *hw,
					      u8 channel, u8 *stage,
					      u8 *step, u32 *delay);
static long _rtl92ee_phy_txpwr_idx_to_dbm(struct ieee80211_hw *hw,
					  enum wireless_mode wirelessmode,
					  u8 txpwridx);
static void rtl92ee_phy_set_rf_on(struct ieee80211_hw *hw);
static void rtl92ee_phy_set_io(struct ieee80211_hw *hw);

u32 rtl92ee_phy_query_bb_reg(struct ieee80211_hw *hw, u32 regaddr, u32 bitmask)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 returnvalue, originalvalue, bitshift;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), bitmask(%#x)\n", regaddr, bitmask);
	originalvalue = rtl_read_dword(rtlpriv, regaddr);
	bitshift = _rtl92ee_phy_calculate_bit_shift(bitmask);
	returnvalue = (originalvalue & bitmask) >> bitshift;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "BBR MASK=0x%x Addr[0x%x]=0x%x\n",
		  bitmask, regaddr, originalvalue);

	return returnvalue;
}

void rtl92ee_phy_set_bb_reg(struct ieee80211_hw *hw, u32 regaddr,
			    u32 bitmask, u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 originalvalue, bitshift;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), bitmask(%#x), data(%#x)\n",
		  regaddr, bitmask, data);

	if (bitmask != MASKDWORD) {
		originalvalue = rtl_read_dword(rtlpriv, regaddr);
		bitshift = _rtl92ee_phy_calculate_bit_shift(bitmask);
		data = ((originalvalue & (~bitmask)) | (data << bitshift));
	}

	rtl_write_dword(rtlpriv, regaddr, data);

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), bitmask(%#x), data(%#x)\n",
		  regaddr, bitmask, data);
}

u32 rtl92ee_phy_query_rf_reg(struct ieee80211_hw *hw,
			     enum radio_path rfpath, u32 regaddr, u32 bitmask)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 original_value, readback_value, bitshift;
	unsigned long flags;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), rfpath(%#x), bitmask(%#x)\n",
		  regaddr, rfpath, bitmask);

	spin_lock_irqsave(&rtlpriv->locks.rf_lock, flags);

	original_value = _rtl92ee_phy_rf_serial_read(hw , rfpath, regaddr);
	bitshift = _rtl92ee_phy_calculate_bit_shift(bitmask);
	readback_value = (original_value & bitmask) >> bitshift;

	spin_unlock_irqrestore(&rtlpriv->locks.rf_lock, flags);

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x),rfpath(%#x),bitmask(%#x),original_value(%#x)\n",
		  regaddr, rfpath, bitmask, original_value);

	return readback_value;
}

void rtl92ee_phy_set_rf_reg(struct ieee80211_hw *hw,
			    enum radio_path rfpath,
			    u32 addr, u32 bitmask, u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 original_value, bitshift;
	unsigned long flags;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), bitmask(%#x), data(%#x), rfpath(%#x)\n",
		  addr, bitmask, data, rfpath);

	spin_lock_irqsave(&rtlpriv->locks.rf_lock, flags);

	if (bitmask != RFREG_OFFSET_MASK) {
		original_value = _rtl92ee_phy_rf_serial_read(hw, rfpath, addr);
		bitshift = _rtl92ee_phy_calculate_bit_shift(bitmask);
		data = (original_value & (~bitmask)) | (data << bitshift);
	}

	_rtl92ee_phy_rf_serial_write(hw, rfpath, addr, data);

	spin_unlock_irqrestore(&rtlpriv->locks.rf_lock, flags);

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), bitmask(%#x), data(%#x), rfpath(%#x)\n",
		  addr, bitmask, data, rfpath);
}

static u32 _rtl92ee_phy_rf_serial_read(struct ieee80211_hw *hw,
				       enum radio_path rfpath, u32 offset)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct bb_reg_def *pphyreg = &rtlphy->phyreg_def[rfpath];
	u32 newoffset;
	u32 tmplong, tmplong2;
	u8 rfpi_enable = 0;
	u32 retvalue;

	offset &= 0xff;
	newoffset = offset;
	if (RT_CANNOT_IO(hw)) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "return all one\n");
		return 0xFFFFFFFF;
	}
	tmplong = rtl_get_bbreg(hw, RFPGA0_XA_HSSIPARAMETER2, MASKDWORD);
	if (rfpath == RF90_PATH_A)
		tmplong2 = tmplong;
	else
		tmplong2 = rtl_get_bbreg(hw, pphyreg->rfhssi_para2, MASKDWORD);
	tmplong2 = (tmplong2 & (~BLSSIREADADDRESS)) |
		   (newoffset << 23) | BLSSIREADEDGE;
	rtl_set_bbreg(hw, RFPGA0_XA_HSSIPARAMETER2, MASKDWORD,
		      tmplong & (~BLSSIREADEDGE));
	mdelay(1);
	rtl_set_bbreg(hw, pphyreg->rfhssi_para2, MASKDWORD, tmplong2);
	mdelay(2);
	if (rfpath == RF90_PATH_A)
		rfpi_enable = (u8)rtl_get_bbreg(hw, RFPGA0_XA_HSSIPARAMETER1,
						BIT(8));
	else if (rfpath == RF90_PATH_B)
		rfpi_enable = (u8)rtl_get_bbreg(hw, RFPGA0_XB_HSSIPARAMETER1,
						BIT(8));
	if (rfpi_enable)
		retvalue = rtl_get_bbreg(hw, pphyreg->rf_rbpi,
					 BLSSIREADBACKDATA);
	else
		retvalue = rtl_get_bbreg(hw, pphyreg->rf_rb,
					 BLSSIREADBACKDATA);
	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "RFR-%d Addr[0x%x]=0x%x\n",
		  rfpath, pphyreg->rf_rb, retvalue);
	return retvalue;
}

static void _rtl92ee_phy_rf_serial_write(struct ieee80211_hw *hw,
					 enum radio_path rfpath, u32 offset,
					 u32 data)
{
	u32 data_and_addr;
	u32 newoffset;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct bb_reg_def *pphyreg = &rtlphy->phyreg_def[rfpath];

	if (RT_CANNOT_IO(hw)) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "stop\n");
		return;
	}
	offset &= 0xff;
	newoffset = offset;
	data_and_addr = ((newoffset << 20) | (data & 0x000fffff)) & 0x0fffffff;
	rtl_set_bbreg(hw, pphyreg->rf3wire_offset, MASKDWORD, data_and_addr);
	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "RFW-%d Addr[0x%x]=0x%x\n", rfpath,
		 pphyreg->rf3wire_offset, data_and_addr);
}

static u32 _rtl92ee_phy_calculate_bit_shift(u32 bitmask)
{
	u32 i;

	for (i = 0; i <= 31; i++) {
		if (((bitmask >> i) & 0x1) == 1)
			break;
	}
	return i;
}

bool rtl92ee_phy_mac_config(struct ieee80211_hw *hw)
{
	return _rtl92ee_phy_config_mac_with_headerfile(hw);
}

bool rtl92ee_phy_bb_config(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	bool rtstatus = true;
	u16 regval;
	u32 tmp;
	u8 crystal_cap;

	phy_init_bb_rf_register_def(hw);
	regval = rtl_read_word(rtlpriv, REG_SYS_FUNC_EN);
	rtl_write_word(rtlpriv, REG_SYS_FUNC_EN,
		       regval | BIT(13) | BIT(0) | BIT(1));

	rtl_write_byte(rtlpriv, REG_RF_CTRL, RF_EN | RF_RSTB | RF_SDMRSTB);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN,
		       FEN_PPLL | FEN_PCIEA | FEN_DIO_PCIE |
		       FEN_BB_GLB_RSTN | FEN_BBRSTB);

	rtl_write_byte(rtlpriv, REG_AFE_XTAL_CTRL + 1, 0x80);

	tmp = rtl_read_dword(rtlpriv, 0x4c);
	rtl_write_dword(rtlpriv, 0x4c, tmp | BIT(23));

	rtstatus = _rtl92ee_phy_bb8192ee_config_parafile(hw);

	crystal_cap = rtlpriv->efuse.eeprom_crystalcap & 0x3F;
	rtl_set_bbreg(hw, REG_MAC_PHY_CTRL, 0xFFF000,
		      (crystal_cap | (crystal_cap << 6)));
	return rtstatus;
}

bool rtl92ee_phy_rf_config(struct ieee80211_hw *hw)
{
	return rtl92ee_phy_rf6052_config(hw);
}

static bool _check_condition(struct ieee80211_hw *hw,
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
	if ((_board != cond) && (cond != 0xFF))
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

static void _rtl92ee_config_rf_reg(struct ieee80211_hw *hw, u32 addr, u32 data,
				   enum radio_path rfpath, u32 regaddr)
{
	if (addr == 0xfe || addr == 0xffe) {
		mdelay(50);
	} else {
		rtl_set_rfreg(hw, rfpath, regaddr, RFREG_OFFSET_MASK, data);
		udelay(1);

		if (addr == 0xb6) {
			u32 getvalue;
			u8 count = 0;

			getvalue = rtl_get_rfreg(hw, rfpath, addr, MASKDWORD);
			udelay(1);

			while ((getvalue >> 8) != (data >> 8)) {
				count++;
				rtl_set_rfreg(hw, rfpath, regaddr,
					      RFREG_OFFSET_MASK, data);
				udelay(1);
				getvalue = rtl_get_rfreg(hw, rfpath, addr,
							 MASKDWORD);
				if (count > 5)
					break;
			}
		}

		if (addr == 0xb2) {
			u32 getvalue;
			u8 count = 0;

			getvalue = rtl_get_rfreg(hw, rfpath, addr, MASKDWORD);
			udelay(1);

			while (getvalue != data) {
				count++;
				rtl_set_rfreg(hw, rfpath, regaddr,
					      RFREG_OFFSET_MASK, data);
				udelay(1);
				rtl_set_rfreg(hw, rfpath, 0x18,
					      RFREG_OFFSET_MASK, 0x0fc07);
				udelay(1);
				getvalue = rtl_get_rfreg(hw, rfpath, addr,
							 MASKDWORD);
				if (count > 5)
					break;
			}
		}
	}
}

static void _rtl92ee_config_rf_radio_a(struct ieee80211_hw *hw,
				       u32 addr, u32 data)
{
	u32 content = 0x1000; /*RF Content: radio_a_txt*/
	u32 maskforphyset = (u32)(content & 0xE000);

	_rtl92ee_config_rf_reg(hw, addr, data, RF90_PATH_A,
			       addr | maskforphyset);
}

static void _rtl92ee_config_rf_radio_b(struct ieee80211_hw *hw,
				       u32 addr, u32 data)
{
	u32 content = 0x1001; /*RF Content: radio_b_txt*/
	u32 maskforphyset = (u32)(content & 0xE000);

	_rtl92ee_config_rf_reg(hw, addr, data, RF90_PATH_B,
			       addr | maskforphyset);
}

static void _rtl92ee_config_bb_reg(struct ieee80211_hw *hw,
				   u32 addr, u32 data)
{
	if (addr == 0xfe)
		mdelay(50);
	else if (addr == 0xfd)
		mdelay(5);
	else if (addr == 0xfc)
		mdelay(1);
	else if (addr == 0xfb)
		udelay(50);
	else if (addr == 0xfa)
		udelay(5);
	else if (addr == 0xf9)
		udelay(1);
	else
		rtl_set_bbreg(hw, addr, MASKDWORD , data);

	udelay(1);
}

static void _rtl92ee_phy_init_tx_power_by_rate(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;

	u8 band = BAND_ON_2_4G, rf = 0, txnum = 0, sec = 0;

	for (; band <= BAND_ON_5G; ++band)
		for (; rf < TX_PWR_BY_RATE_NUM_RF; ++rf)
			for (; txnum < TX_PWR_BY_RATE_NUM_RF; ++txnum)
				for (; sec < TX_PWR_BY_RATE_NUM_SECTION; ++sec)
					rtlphy->tx_power_by_rate_offset
					     [band][rf][txnum][sec] = 0;
}

static void _rtl92ee_phy_set_txpower_by_rate_base(struct ieee80211_hw *hw,
						  u8 band, u8 path,
						  u8 rate_section, u8 txnum,
						  u8 value)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;

	if (path > RF90_PATH_D) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Invalid Rf Path %d\n", path);
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
				 "Invalid RateSection %d in 2.4G,Rf %d,%dTx\n",
				  rate_section, path, txnum);
			break;
		}
	} else {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Invalid Band %d\n", band);
	}
}

static u8 _rtl92ee_phy_get_txpower_by_rate_base(struct ieee80211_hw *hw,
						u8 band, u8 path, u8 txnum,
						u8 rate_section)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 value = 0;

	if (path > RF90_PATH_D) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Invalid Rf Path %d\n", path);
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
				 "Invalid RateSection %d in 2.4G,Rf %d,%dTx\n",
				  rate_section, path, txnum);
			break;
		}
	} else {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Invalid Band %d()\n", band);
	}
	return value;
}

static void _rtl92ee_phy_store_txpower_by_rate_base(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u16 raw = 0;
	u8 base = 0, path = 0;

	for (path = RF90_PATH_A; path <= RF90_PATH_B; ++path) {
		if (path == RF90_PATH_A) {
			raw = (u16)(rtlphy->tx_power_by_rate_offset
				    [BAND_ON_2_4G][path][RF_1TX][3] >> 24) &
				    0xFF;
			base = (raw >> 4) * 10 + (raw & 0xF);
			_rtl92ee_phy_set_txpower_by_rate_base(hw, BAND_ON_2_4G,
							      path, CCK, RF_1TX,
							      base);
		} else if (path == RF90_PATH_B) {
			raw = (u16)(rtlphy->tx_power_by_rate_offset
				    [BAND_ON_2_4G][path][RF_1TX][3] >> 0) &
				    0xFF;
			base = (raw >> 4) * 10 + (raw & 0xF);
			_rtl92ee_phy_set_txpower_by_rate_base(hw, BAND_ON_2_4G,
							      path, CCK, RF_1TX,
							      base);
		}
		raw = (u16)(rtlphy->tx_power_by_rate_offset
			    [BAND_ON_2_4G][path][RF_1TX][1] >> 24) & 0xFF;
		base = (raw >> 4) * 10 + (raw & 0xF);
		_rtl92ee_phy_set_txpower_by_rate_base(hw, BAND_ON_2_4G, path,
						      OFDM, RF_1TX, base);

		raw = (u16)(rtlphy->tx_power_by_rate_offset
			    [BAND_ON_2_4G][path][RF_1TX][5] >> 24) & 0xFF;
		base = (raw >> 4) * 10 + (raw & 0xF);
		_rtl92ee_phy_set_txpower_by_rate_base(hw, BAND_ON_2_4G, path,
						      HT_MCS0_MCS7, RF_1TX,
						      base);

		raw = (u16)(rtlphy->tx_power_by_rate_offset
			    [BAND_ON_2_4G][path][RF_2TX][7] >> 24) & 0xFF;
		base = (raw >> 4) * 10 + (raw & 0xF);
		_rtl92ee_phy_set_txpower_by_rate_base(hw, BAND_ON_2_4G, path,
						      HT_MCS8_MCS15, RF_2TX,
						      base);
	}
}

static void _phy_convert_txpower_dbm_to_relative_value(u32 *data, u8 start,
						       u8 end, u8 base)
{
	char i = 0;
	u8 tmp = 0;
	u32 temp_data = 0;

	for (i = 3; i >= 0; --i) {
		if (i >= start && i <= end) {
			/* Get the exact value */
			tmp = (u8)(*data >> (i * 8)) & 0xF;
			tmp += ((u8)((*data >> (i * 8 + 4)) & 0xF)) * 10;

			/* Change the value to a relative value */
			tmp = (tmp > base) ? tmp - base : base - tmp;
		} else {
			tmp = (u8)(*data >> (i * 8)) & 0xFF;
		}
		temp_data <<= 8;
		temp_data |= tmp;
	}
	*data = temp_data;
}

static void phy_convert_txpwr_dbm_to_rel_val(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 base = 0, rf = 0, band = BAND_ON_2_4G;

	for (rf = RF90_PATH_A; rf <= RF90_PATH_B; ++rf) {
		if (rf == RF90_PATH_A) {
			base = _rtl92ee_phy_get_txpower_by_rate_base(hw, band,
								     rf, RF_1TX,
								     CCK);
			_phy_convert_txpower_dbm_to_relative_value(
				&rtlphy->tx_power_by_rate_offset
				[band][rf][RF_1TX][2],
				1, 1, base);
			_phy_convert_txpower_dbm_to_relative_value(
				&rtlphy->tx_power_by_rate_offset
				[band][rf][RF_1TX][3],
				1, 3, base);
		} else if (rf == RF90_PATH_B) {
			base = _rtl92ee_phy_get_txpower_by_rate_base(hw, band,
								     rf, RF_1TX,
								     CCK);
			_phy_convert_txpower_dbm_to_relative_value(
				&rtlphy->tx_power_by_rate_offset
				[band][rf][RF_1TX][3],
				0, 0, base);
			_phy_convert_txpower_dbm_to_relative_value(
				&rtlphy->tx_power_by_rate_offset
				[band][rf][RF_1TX][2],
				1, 3, base);
		}
		base = _rtl92ee_phy_get_txpower_by_rate_base(hw, band, rf,
							     RF_1TX, OFDM);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[band][rf][RF_1TX][0],
			0, 3, base);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[band][rf][RF_1TX][1],
			0, 3, base);

		base = _rtl92ee_phy_get_txpower_by_rate_base(hw, band, rf,
							     RF_1TX,
							     HT_MCS0_MCS7);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[band][rf][RF_1TX][4],
			0, 3, base);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[band][rf][RF_1TX][5],
			0, 3, base);

		base = _rtl92ee_phy_get_txpower_by_rate_base(hw, band, rf,
							     RF_2TX,
							     HT_MCS8_MCS15);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[band][rf][RF_2TX][6],
			0, 3, base);

		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[band][rf][RF_2TX][7],
			0, 3, base);
	}

	RT_TRACE(rtlpriv, COMP_POWER, DBG_TRACE,
		 "<==phy_convert_txpwr_dbm_to_rel_val()\n");
}

static void _rtl92ee_phy_txpower_by_rate_configuration(struct ieee80211_hw *hw)
{
	_rtl92ee_phy_store_txpower_by_rate_base(hw);
	phy_convert_txpwr_dbm_to_rel_val(hw);
}

static bool _rtl92ee_phy_bb8192ee_config_parafile(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	bool rtstatus;

	rtstatus = phy_config_bb_with_hdr_file(hw, BASEBAND_CONFIG_PHY_REG);
	if (!rtstatus) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "Write BB Reg Fail!!");
		return false;
	}

	_rtl92ee_phy_init_tx_power_by_rate(hw);
	if (!rtlefuse->autoload_failflag) {
		rtlphy->pwrgroup_cnt = 0;
		rtstatus =
		  phy_config_bb_with_pghdrfile(hw, BASEBAND_CONFIG_PHY_REG);
	}
	_rtl92ee_phy_txpower_by_rate_configuration(hw);
	if (!rtstatus) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "BB_PG Reg Fail!!");
		return false;
	}
	rtstatus = phy_config_bb_with_hdr_file(hw, BASEBAND_CONFIG_AGC_TAB);
	if (!rtstatus) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "AGC Table Fail\n");
		return false;
	}
	rtlphy->cck_high_power = (bool)(rtl_get_bbreg(hw,
						      RFPGA0_XA_HSSIPARAMETER2,
						      0x200));

	return true;
}

static bool _rtl92ee_phy_config_mac_with_headerfile(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 i;
	u32 arraylength;
	u32 *ptrarray;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE, "Read Rtl8192EMACPHY_Array\n");
	arraylength = RTL8192EE_MAC_ARRAY_LEN;
	ptrarray = RTL8192EE_MAC_ARRAY;
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "Img:RTL8192EE_MAC_ARRAY LEN %d\n" , arraylength);
	for (i = 0; i < arraylength; i = i + 2)
		rtl_write_byte(rtlpriv, ptrarray[i], (u8)ptrarray[i + 1]);
	return true;
}

#define READ_NEXT_PAIR(v1, v2, i) \
	do { \
		i += 2; \
		v1 = array[i]; \
		v2 = array[i+1]; \
	} while (0)

static bool phy_config_bb_with_hdr_file(struct ieee80211_hw *hw,
					u8 configtype)
{
	int i;
	u32 *array;
	u16 len;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 v1 = 0, v2 = 0;

	if (configtype == BASEBAND_CONFIG_PHY_REG) {
		len = RTL8192EE_PHY_REG_ARRAY_LEN;
		array = RTL8192EE_PHY_REG_ARRAY;

		for (i = 0; i < len; i = i + 2) {
			v1 = array[i];
			v2 = array[i+1];
			if (v1 < 0xcdcdcdcd) {
				_rtl92ee_config_bb_reg(hw, v1, v2);
			} else {/*This line is the start line of branch.*/
				/* to protect READ_NEXT_PAIR not overrun */
				if (i >= len - 2)
					break;

				if (!_check_condition(hw , array[i])) {
					/*Discard the following pairs*/
					READ_NEXT_PAIR(v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD && i < len - 2) {
						READ_NEXT_PAIR(v1, v2, i);
					}
					i -= 2; /* prevent from for-loop += 2*/
				} else {
					/* Configure matched pairs and
					 * skip to end of if-else.
					 */
					READ_NEXT_PAIR(v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD && i < len - 2) {
						_rtl92ee_config_bb_reg(hw, v1,
								       v2);
						READ_NEXT_PAIR(v1, v2, i);
					}

					while (v2 != 0xDEAD && i < len - 2)
						READ_NEXT_PAIR(v1, v2, i);
				}
			}
		}
	} else if (configtype == BASEBAND_CONFIG_AGC_TAB) {
		len = RTL8192EE_AGC_TAB_ARRAY_LEN;
		array = RTL8192EE_AGC_TAB_ARRAY;

		for (i = 0; i < len; i = i + 2) {
			v1 = array[i];
			v2 = array[i+1];
			if (v1 < 0xCDCDCDCD) {
				rtl_set_bbreg(hw, array[i], MASKDWORD,
					      array[i + 1]);
				udelay(1);
				continue;
		    } else{/*This line is the start line of branch.*/
			  /* to protect READ_NEXT_PAIR not overrun */
				if (i >= len - 2)
					break;

				if (!_check_condition(hw , array[i])) {
					/*Discard the following pairs*/
					READ_NEXT_PAIR(v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD &&
					       i < len - 2) {
						READ_NEXT_PAIR(v1, v2, i);
					}
					i -= 2; /* prevent from for-loop += 2*/
				} else {
					/* Configure matched pairs and
					 * skip to end of if-else.
					 */
					READ_NEXT_PAIR(v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD &&
					       i < len - 2) {
						rtl_set_bbreg(hw,
							      array[i],
							      MASKDWORD,
							      array[i + 1]);
						udelay(1);
						READ_NEXT_PAIR(v1 , v2 , i);
					}

					while (v2 != 0xDEAD &&
					       i < len - 2) {
						READ_NEXT_PAIR(v1 , v2 , i);
					}
				}
			}
			RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
				 "The agctab_array_table[0] is %x Rtl818EEPHY_REGArray[1] is %x\n",
				 array[i],
				 array[i + 1]);
		}
	}
	return true;
}

static u8 _rtl92ee_get_rate_section_index(u32 regaddr)
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
			index = (u8)((regaddr - 0xC20) / 4);
		else if (regaddr >= 0xE20 && regaddr <= 0xE4C)
			index = (u8)((regaddr - 0xE20) / 4);
		break;
	}
	return index;
}

static void _rtl92ee_store_tx_power_by_rate(struct ieee80211_hw *hw,
					    enum band_type band,
					    enum radio_path rfpath,
					    u32 txnum, u32 regaddr,
					    u32 bitmask, u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 section = _rtl92ee_get_rate_section_index(regaddr);

	if (band != BAND_ON_2_4G && band != BAND_ON_5G) {
		RT_TRACE(rtlpriv, FPHY, PHY_TXPWR, "Invalid Band %d\n", band);
		return;
	}

	if (rfpath > MAX_RF_PATH - 1) {
		RT_TRACE(rtlpriv, FPHY, PHY_TXPWR,
			 "Invalid RfPath %d\n", rfpath);
		return;
	}
	if (txnum > MAX_RF_PATH - 1) {
		RT_TRACE(rtlpriv, FPHY, PHY_TXPWR, "Invalid TxNum %d\n", txnum);
		return;
	}

	rtlphy->tx_power_by_rate_offset[band][rfpath][txnum][section] = data;
}

static bool phy_config_bb_with_pghdrfile(struct ieee80211_hw *hw,
					 u8 configtype)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	int i;
	u32 *phy_regarray_table_pg;
	u16 phy_regarray_pg_len;
	u32 v1 = 0, v2 = 0, v3 = 0, v4 = 0, v5 = 0, v6 = 0;

	phy_regarray_pg_len = RTL8192EE_PHY_REG_ARRAY_PG_LEN;
	phy_regarray_table_pg = RTL8192EE_PHY_REG_ARRAY_PG;

	if (configtype == BASEBAND_CONFIG_PHY_REG) {
		for (i = 0; i < phy_regarray_pg_len; i = i + 6) {
			v1 = phy_regarray_table_pg[i];
			v2 = phy_regarray_table_pg[i+1];
			v3 = phy_regarray_table_pg[i+2];
			v4 = phy_regarray_table_pg[i+3];
			v5 = phy_regarray_table_pg[i+4];
			v6 = phy_regarray_table_pg[i+5];

			if (v1 < 0xcdcdcdcd) {
				_rtl92ee_store_tx_power_by_rate(hw, v1, v2, v3,
								v4, v5, v6);
				continue;
			}
		}
	} else {
		RT_TRACE(rtlpriv, COMP_SEND, DBG_TRACE,
			 "configtype != BaseBand_Config_PHY_REG\n");
	}
	return true;
}

#define READ_NEXT_RF_PAIR(v1, v2, i) \
	do { \
		i += 2; \
		v1 = array[i]; \
		v2 = array[i+1]; \
	} while (0)

bool rtl92ee_phy_config_rf_with_headerfile(struct ieee80211_hw  *hw,
					   enum radio_path rfpath)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	int i;
	u32 *array;
	u16 len;
	u32 v1 = 0, v2 = 0;

	switch (rfpath) {
	case RF90_PATH_A:
		len = RTL8192EE_RADIOA_ARRAY_LEN;
		array = RTL8192EE_RADIOA_ARRAY;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Radio_A:RTL8192EE_RADIOA_ARRAY %d\n" , len);
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "Radio No %x\n", rfpath);
		for (i = 0; i < len; i = i + 2) {
			v1 = array[i];
			v2 = array[i+1];
			if (v1 < 0xcdcdcdcd) {
				_rtl92ee_config_rf_radio_a(hw, v1, v2);
				continue;
			} else {/*This line is the start line of branch.*/
				/* to protect READ_NEXT_PAIR not overrun */
				if (i >= len - 2)
					break;

				if (!_check_condition(hw , array[i])) {
					/*Discard the following pairs*/
					READ_NEXT_RF_PAIR(v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD && i < len - 2) {
						READ_NEXT_RF_PAIR(v1, v2, i);
					}
					i -= 2; /* prevent from for-loop += 2*/
				} else {
					/* Configure matched pairs and
					 * skip to end of if-else.
					 */
					READ_NEXT_RF_PAIR(v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD && i < len - 2) {
						_rtl92ee_config_rf_radio_a(hw,
									   v1,
									   v2);
						READ_NEXT_RF_PAIR(v1, v2, i);
					}

					while (v2 != 0xDEAD && i < len - 2)
						READ_NEXT_RF_PAIR(v1, v2, i);
				}
			}
		}
		break;

	case RF90_PATH_B:
		len = RTL8192EE_RADIOB_ARRAY_LEN;
		array = RTL8192EE_RADIOB_ARRAY;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Radio_A:RTL8192EE_RADIOB_ARRAY %d\n" , len);
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "Radio No %x\n", rfpath);
		for (i = 0; i < len; i = i + 2) {
			v1 = array[i];
			v2 = array[i+1];
			if (v1 < 0xcdcdcdcd) {
				_rtl92ee_config_rf_radio_b(hw, v1, v2);
				continue;
			} else {/*This line is the start line of branch.*/
				/* to protect READ_NEXT_PAIR not overrun */
				if (i >= len - 2)
					break;

				if (!_check_condition(hw , array[i])) {
					/*Discard the following pairs*/
					READ_NEXT_RF_PAIR(v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD && i < len - 2) {
						READ_NEXT_RF_PAIR(v1, v2, i);
					}
					i -= 2; /* prevent from for-loop += 2*/
				} else {
					/* Configure matched pairs and
					 * skip to end of if-else.
					 */
					READ_NEXT_RF_PAIR(v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD && i < len - 2) {
						_rtl92ee_config_rf_radio_b(hw,
									   v1,
									   v2);
						READ_NEXT_RF_PAIR(v1, v2, i);
					}

					while (v2 != 0xDEAD && i < len - 2)
						READ_NEXT_RF_PAIR(v1, v2, i);
				}
			}
		}
		break;
	case RF90_PATH_C:
	case RF90_PATH_D:
		break;
	}
	return true;
}

void rtl92ee_phy_get_hw_reg_originalvalue(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;

	rtlphy->default_initialgain[0] =
		(u8)rtl_get_bbreg(hw, ROFDM0_XAAGCCORE1, MASKBYTE0);
	rtlphy->default_initialgain[1] =
		(u8)rtl_get_bbreg(hw, ROFDM0_XBAGCCORE1, MASKBYTE0);
	rtlphy->default_initialgain[2] =
		(u8)rtl_get_bbreg(hw, ROFDM0_XCAGCCORE1, MASKBYTE0);
	rtlphy->default_initialgain[3] =
		(u8)rtl_get_bbreg(hw, ROFDM0_XDAGCCORE1, MASKBYTE0);

	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
		 "Default initial gain (c50=0x%x, c58=0x%x, c60=0x%x, c68=0x%x\n",
		  rtlphy->default_initialgain[0],
		  rtlphy->default_initialgain[1],
		  rtlphy->default_initialgain[2],
		  rtlphy->default_initialgain[3]);

	rtlphy->framesync = (u8)rtl_get_bbreg(hw,
					      ROFDM0_RXDETECTOR3, MASKBYTE0);
	rtlphy->framesync_c34 = rtl_get_bbreg(hw,
					      ROFDM0_RXDETECTOR2, MASKDWORD);

	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
		 "Default framesync (0x%x) = 0x%x\n",
		  ROFDM0_RXDETECTOR3, rtlphy->framesync);
}

static void phy_init_bb_rf_register_def(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;

	rtlphy->phyreg_def[RF90_PATH_A].rfintfs = RFPGA0_XAB_RFINTERFACESW;
	rtlphy->phyreg_def[RF90_PATH_B].rfintfs = RFPGA0_XAB_RFINTERFACESW;

	rtlphy->phyreg_def[RF90_PATH_A].rfintfo = RFPGA0_XA_RFINTERFACEOE;
	rtlphy->phyreg_def[RF90_PATH_B].rfintfo = RFPGA0_XB_RFINTERFACEOE;

	rtlphy->phyreg_def[RF90_PATH_A].rfintfe = RFPGA0_XA_RFINTERFACEOE;
	rtlphy->phyreg_def[RF90_PATH_B].rfintfe = RFPGA0_XB_RFINTERFACEOE;

	rtlphy->phyreg_def[RF90_PATH_A].rf3wire_offset =
							RFPGA0_XA_LSSIPARAMETER;
	rtlphy->phyreg_def[RF90_PATH_B].rf3wire_offset =
							RFPGA0_XB_LSSIPARAMETER;

	rtlphy->phyreg_def[RF90_PATH_A].rfhssi_para2 = RFPGA0_XA_HSSIPARAMETER2;
	rtlphy->phyreg_def[RF90_PATH_B].rfhssi_para2 = RFPGA0_XB_HSSIPARAMETER2;

	rtlphy->phyreg_def[RF90_PATH_A].rf_rb = RFPGA0_XA_LSSIREADBACK;
	rtlphy->phyreg_def[RF90_PATH_B].rf_rb = RFPGA0_XB_LSSIREADBACK;

	rtlphy->phyreg_def[RF90_PATH_A].rf_rbpi = TRANSCEIVEA_HSPI_READBACK;
	rtlphy->phyreg_def[RF90_PATH_B].rf_rbpi = TRANSCEIVEB_HSPI_READBACK;
}

void rtl92ee_phy_get_txpower_level(struct ieee80211_hw *hw, long *powerlevel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 txpwr_level;
	long txpwr_dbm;

	txpwr_level = rtlphy->cur_cck_txpwridx;
	txpwr_dbm = _rtl92ee_phy_txpwr_idx_to_dbm(hw, WIRELESS_MODE_B,
						  txpwr_level);
	txpwr_level = rtlphy->cur_ofdm24g_txpwridx;
	if (_rtl92ee_phy_txpwr_idx_to_dbm(hw, WIRELESS_MODE_G, txpwr_level) >
	    txpwr_dbm)
		txpwr_dbm = _rtl92ee_phy_txpwr_idx_to_dbm(hw, WIRELESS_MODE_G,
							  txpwr_level);
	txpwr_level = rtlphy->cur_ofdm24g_txpwridx;
	if (_rtl92ee_phy_txpwr_idx_to_dbm(hw, WIRELESS_MODE_N_24G,
					  txpwr_level) > txpwr_dbm)
		txpwr_dbm = _rtl92ee_phy_txpwr_idx_to_dbm(hw,
							  WIRELESS_MODE_N_24G,
							  txpwr_level);
	*powerlevel = txpwr_dbm;
}

static u8 _rtl92ee_phy_get_ratesection_intxpower_byrate(enum radio_path path,
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

static u8 _rtl92ee_get_txpower_by_rate(struct ieee80211_hw *hw,
				       enum band_type band,
				       enum radio_path rf, u8 rate)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 shift = 0, sec, tx_num;
	char diff = 0;

	sec = _rtl92ee_phy_get_ratesection_intxpower_byrate(rf, rate);
	tx_num = RF_TX_NUM_NONIMPLEMENT;

	if (tx_num == RF_TX_NUM_NONIMPLEMENT) {
		if ((rate >= DESC92C_RATEMCS8 && rate <= DESC92C_RATEMCS15))
			tx_num = RF_2TX;
		else
			tx_num = RF_1TX;
	}

	switch (rate) {
	case DESC92C_RATE1M:
	case DESC92C_RATE6M:
	case DESC92C_RATE24M:
	case DESC92C_RATEMCS0:
	case DESC92C_RATEMCS4:
	case DESC92C_RATEMCS8:
	case DESC92C_RATEMCS12:
		shift = 0;
		break;
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

	diff = (u8)(rtlphy->tx_power_by_rate_offset[band][rf][tx_num][sec] >>
		    shift) & 0xff;

	return	diff;
}

static u8 _rtl92ee_get_txpower_index(struct ieee80211_hw *hw,
				     enum radio_path rfpath, u8 rate,
				     u8 bw, u8 channel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtlpriv);
	u8 index = (channel - 1);
	u8 tx_power = 0;
	u8 diff = 0;

	if (channel < 1 || channel > 14) {
		index = 0;
		RT_TRACE(rtlpriv, COMP_POWER_TRACKING, DBG_DMESG,
			 "Illegal channel!!\n");
	}

	if (IS_CCK_RATE(rate))
		tx_power = rtlefuse->txpwrlevel_cck[rfpath][index];
	else if (DESC92C_RATE6M <= rate)
		tx_power = rtlefuse->txpwrlevel_ht40_1s[rfpath][index];

	/* OFDM-1T*/
	if (DESC92C_RATE6M <= rate && rate <= DESC92C_RATE54M &&
	    !IS_CCK_RATE(rate))
		tx_power += rtlefuse->txpwr_legacyhtdiff[rfpath][TX_1S];

	/* BW20-1S, BW20-2S */
	if (bw == HT_CHANNEL_WIDTH_20) {
		if (DESC92C_RATEMCS0 <= rate && rate <= DESC92C_RATEMCS15)
			tx_power += rtlefuse->txpwr_ht20diff[rfpath][TX_1S];
		if (DESC92C_RATEMCS8 <= rate && rate <= DESC92C_RATEMCS15)
			tx_power += rtlefuse->txpwr_ht20diff[rfpath][TX_2S];
	} else if (bw == HT_CHANNEL_WIDTH_20_40) {/* BW40-1S, BW40-2S */
		if (DESC92C_RATEMCS0 <= rate && rate <= DESC92C_RATEMCS15)
			tx_power += rtlefuse->txpwr_ht40diff[rfpath][TX_1S];
		if (DESC92C_RATEMCS8 <= rate && rate <= DESC92C_RATEMCS15)
			tx_power += rtlefuse->txpwr_ht40diff[rfpath][TX_2S];
	}

	if (rtlefuse->eeprom_regulatory != 2)
		diff = _rtl92ee_get_txpower_by_rate(hw, BAND_ON_2_4G,
						    rfpath, rate);

	tx_power += diff;

	if (tx_power > MAX_POWER_INDEX)
		tx_power = MAX_POWER_INDEX;

	return tx_power;
}

static void _rtl92ee_set_txpower_index(struct ieee80211_hw *hw, u8 pwr_idx,
				       enum radio_path rfpath, u8 rate)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (rfpath == RF90_PATH_A) {
		switch (rate) {
		case DESC92C_RATE1M:
			rtl_set_bbreg(hw, RTXAGC_A_CCK1_MCS32, MASKBYTE1,
				      pwr_idx);
			break;
		case DESC92C_RATE2M:
			rtl_set_bbreg(hw, RTXAGC_B_CCK11_A_CCK2_11, MASKBYTE1,
				      pwr_idx);
			break;
		case DESC92C_RATE5_5M:
			rtl_set_bbreg(hw, RTXAGC_B_CCK11_A_CCK2_11, MASKBYTE2,
				      pwr_idx);
			break;
		case DESC92C_RATE11M:
			rtl_set_bbreg(hw, RTXAGC_B_CCK11_A_CCK2_11, MASKBYTE3,
				      pwr_idx);
			break;
		case DESC92C_RATE6M:
			rtl_set_bbreg(hw, RTXAGC_A_RATE18_06, MASKBYTE0,
				      pwr_idx);
			break;
		case DESC92C_RATE9M:
			rtl_set_bbreg(hw, RTXAGC_A_RATE18_06, MASKBYTE1,
				      pwr_idx);
			break;
		case DESC92C_RATE12M:
			rtl_set_bbreg(hw, RTXAGC_A_RATE18_06, MASKBYTE2,
				      pwr_idx);
			break;
		case DESC92C_RATE18M:
			rtl_set_bbreg(hw, RTXAGC_A_RATE18_06, MASKBYTE3,
				      pwr_idx);
			break;
		case DESC92C_RATE24M:
			rtl_set_bbreg(hw, RTXAGC_A_RATE54_24, MASKBYTE0,
				      pwr_idx);
			break;
		case DESC92C_RATE36M:
			rtl_set_bbreg(hw, RTXAGC_A_RATE54_24, MASKBYTE1,
				      pwr_idx);
			break;
		case DESC92C_RATE48M:
			rtl_set_bbreg(hw, RTXAGC_A_RATE54_24, MASKBYTE2,
				      pwr_idx);
			break;
		case DESC92C_RATE54M:
			rtl_set_bbreg(hw, RTXAGC_A_RATE54_24, MASKBYTE3,
				      pwr_idx);
			break;
		case DESC92C_RATEMCS0:
			rtl_set_bbreg(hw, RTXAGC_A_MCS03_MCS00, MASKBYTE0,
				      pwr_idx);
			break;
		case DESC92C_RATEMCS1:
			rtl_set_bbreg(hw, RTXAGC_A_MCS03_MCS00, MASKBYTE1,
				      pwr_idx);
			break;
		case DESC92C_RATEMCS2:
			rtl_set_bbreg(hw, RTXAGC_A_MCS03_MCS00, MASKBYTE2,
				      pwr_idx);
			break;
		case DESC92C_RATEMCS3:
			rtl_set_bbreg(hw, RTXAGC_A_MCS03_MCS00, MASKBYTE3,
				      pwr_idx);
			break;
		case DESC92C_RATEMCS4:
			rtl_set_bbreg(hw, RTXAGC_A_MCS07_MCS04, MASKBYTE0,
				      pwr_idx);
			break;
		case DESC92C_RATEMCS5:
			rtl_set_bbreg(hw, RTXAGC_A_MCS07_MCS04, MASKBYTE1,
				      pwr_idx);
			break;
		case DESC92C_RATEMCS6:
			rtl_set_bbreg(hw, RTXAGC_A_MCS07_MCS04, MASKBYTE2,
				      pwr_idx);
			break;
		case DESC92C_RATEMCS7:
			rtl_set_bbreg(hw, RTXAGC_A_MCS07_MCS04, MASKBYTE3,
				      pwr_idx);
			break;
		case DESC92C_RATEMCS8:
			rtl_set_bbreg(hw, RTXAGC_A_MCS11_MCS08, MASKBYTE0,
				      pwr_idx);
			break;
		case DESC92C_RATEMCS9:
			rtl_set_bbreg(hw, RTXAGC_A_MCS11_MCS08, MASKBYTE1,
				      pwr_idx);
			break;
		case DESC92C_RATEMCS10:
			rtl_set_bbreg(hw, RTXAGC_A_MCS11_MCS08, MASKBYTE2,
				      pwr_idx);
			break;
		case DESC92C_RATEMCS11:
			rtl_set_bbreg(hw, RTXAGC_A_MCS11_MCS08, MASKBYTE3,
				      pwr_idx);
			break;
		case DESC92C_RATEMCS12:
			rtl_set_bbreg(hw, RTXAGC_A_MCS15_MCS12, MASKBYTE0,
				      pwr_idx);
			break;
		case DESC92C_RATEMCS13:
			rtl_set_bbreg(hw, RTXAGC_A_MCS15_MCS12, MASKBYTE1,
				      pwr_idx);
			break;
		case DESC92C_RATEMCS14:
			rtl_set_bbreg(hw, RTXAGC_A_MCS15_MCS12, MASKBYTE2,
				      pwr_idx);
			break;
		case DESC92C_RATEMCS15:
			rtl_set_bbreg(hw, RTXAGC_A_MCS15_MCS12, MASKBYTE3,
				      pwr_idx);
			break;
		default:
			RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
				 "Invalid Rate!!\n");
			break;
		}
	} else if (rfpath == RF90_PATH_B) {
		switch (rate) {
		case DESC92C_RATE1M:
			rtl_set_bbreg(hw, RTXAGC_B_CCK1_55_MCS32, MASKBYTE1,
				      pwr_idx);
			break;
		case DESC92C_RATE2M:
			rtl_set_bbreg(hw, RTXAGC_B_CCK1_55_MCS32, MASKBYTE2,
				      pwr_idx);
			break;
		case DESC92C_RATE5_5M:
			rtl_set_bbreg(hw, RTXAGC_B_CCK1_55_MCS32, MASKBYTE3,
				      pwr_idx);
			break;
		case DESC92C_RATE11M:
			rtl_set_bbreg(hw, RTXAGC_B_CCK11_A_CCK2_11, MASKBYTE0,
				      pwr_idx);
			break;
		case DESC92C_RATE6M:
			rtl_set_bbreg(hw, RTXAGC_B_RATE18_06, MASKBYTE0,
				      pwr_idx);
			break;
		case DESC92C_RATE9M:
			rtl_set_bbreg(hw, RTXAGC_B_RATE18_06, MASKBYTE1,
				      pwr_idx);
			break;
		case DESC92C_RATE12M:
			rtl_set_bbreg(hw, RTXAGC_B_RATE18_06, MASKBYTE2,
				      pwr_idx);
			break;
		case DESC92C_RATE18M:
			rtl_set_bbreg(hw, RTXAGC_B_RATE18_06, MASKBYTE3,
				      pwr_idx);
			break;
		case DESC92C_RATE24M:
			rtl_set_bbreg(hw, RTXAGC_B_RATE54_24, MASKBYTE0,
				      pwr_idx);
			break;
		case DESC92C_RATE36M:
			rtl_set_bbreg(hw, RTXAGC_B_RATE54_24, MASKBYTE1,
				      pwr_idx);
			break;
		case DESC92C_RATE48M:
			rtl_set_bbreg(hw, RTXAGC_B_RATE54_24, MASKBYTE2,
				      pwr_idx);
			break;
		case DESC92C_RATE54M:
			rtl_set_bbreg(hw, RTXAGC_B_RATE54_24, MASKBYTE3,
				      pwr_idx);
			break;
		case DESC92C_RATEMCS0:
			rtl_set_bbreg(hw, RTXAGC_B_MCS03_MCS00, MASKBYTE0,
				      pwr_idx);
			break;
		case DESC92C_RATEMCS1:
			rtl_set_bbreg(hw, RTXAGC_B_MCS03_MCS00, MASKBYTE1,
				      pwr_idx);
			break;
		case DESC92C_RATEMCS2:
			rtl_set_bbreg(hw, RTXAGC_B_MCS03_MCS00, MASKBYTE2,
				      pwr_idx);
			break;
		case DESC92C_RATEMCS3:
			rtl_set_bbreg(hw, RTXAGC_B_MCS03_MCS00, MASKBYTE3,
				      pwr_idx);
			break;
		case DESC92C_RATEMCS4:
			rtl_set_bbreg(hw, RTXAGC_B_MCS07_MCS04, MASKBYTE0,
				      pwr_idx);
			break;
		case DESC92C_RATEMCS5:
			rtl_set_bbreg(hw, RTXAGC_B_MCS07_MCS04, MASKBYTE1,
				      pwr_idx);
			break;
		case DESC92C_RATEMCS6:
			rtl_set_bbreg(hw, RTXAGC_B_MCS07_MCS04, MASKBYTE2,
				      pwr_idx);
			break;
		case DESC92C_RATEMCS7:
			rtl_set_bbreg(hw, RTXAGC_B_MCS07_MCS04, MASKBYTE3,
				      pwr_idx);
			break;
		case DESC92C_RATEMCS8:
			rtl_set_bbreg(hw, RTXAGC_B_MCS11_MCS08, MASKBYTE0,
				      pwr_idx);
			break;
		case DESC92C_RATEMCS9:
			rtl_set_bbreg(hw, RTXAGC_B_MCS11_MCS08, MASKBYTE1,
				      pwr_idx);
			break;
		case DESC92C_RATEMCS10:
			rtl_set_bbreg(hw, RTXAGC_B_MCS11_MCS08, MASKBYTE2,
				      pwr_idx);
			break;
		case DESC92C_RATEMCS11:
			rtl_set_bbreg(hw, RTXAGC_B_MCS11_MCS08, MASKBYTE3,
				      pwr_idx);
			break;
		case DESC92C_RATEMCS12:
			rtl_set_bbreg(hw, RTXAGC_B_MCS15_MCS12, MASKBYTE0,
				      pwr_idx);
			break;
		case DESC92C_RATEMCS13:
			rtl_set_bbreg(hw, RTXAGC_B_MCS15_MCS12, MASKBYTE1,
				      pwr_idx);
			break;
		case DESC92C_RATEMCS14:
			rtl_set_bbreg(hw, RTXAGC_B_MCS15_MCS12, MASKBYTE2,
				      pwr_idx);
			break;
		case DESC92C_RATEMCS15:
			rtl_set_bbreg(hw, RTXAGC_B_MCS15_MCS12, MASKBYTE3,
				      pwr_idx);
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

static void phy_set_txpower_index_by_rate_array(struct ieee80211_hw *hw,
						enum radio_path rfpath, u8 bw,
						u8 channel, u8 *rates, u8 size)
{
	u8 i;
	u8 power_index;

	for (i = 0; i < size; i++) {
		power_index = _rtl92ee_get_txpower_index(hw, rfpath, rates[i],
							 bw, channel);
		_rtl92ee_set_txpower_index(hw, power_index, rfpath, rates[i]);
	}
}

static void phy_set_txpower_index_by_rate_section(struct ieee80211_hw *hw,
						  enum radio_path rfpath,
						  u8 channel,
						  enum rate_section section)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	struct rtl_phy *rtlphy = &rtlpriv->phy;

	if (section == CCK) {
		u8 cck_rates[] = {DESC92C_RATE1M, DESC92C_RATE2M,
				  DESC92C_RATE5_5M, DESC92C_RATE11M};
		if (rtlhal->current_bandtype == BAND_ON_2_4G)
			phy_set_txpower_index_by_rate_array(hw, rfpath,
							rtlphy->current_chan_bw,
							channel, cck_rates, 4);
	} else if (section == OFDM) {
		u8 ofdm_rates[] = {DESC92C_RATE6M, DESC92C_RATE9M,
				   DESC92C_RATE12M, DESC92C_RATE18M,
				   DESC92C_RATE24M, DESC92C_RATE36M,
				   DESC92C_RATE48M, DESC92C_RATE54M};
		phy_set_txpower_index_by_rate_array(hw, rfpath,
						    rtlphy->current_chan_bw,
						    channel, ofdm_rates, 8);
	} else if (section == HT_MCS0_MCS7) {
		u8 ht_rates1t[]  = {DESC92C_RATEMCS0, DESC92C_RATEMCS1,
				    DESC92C_RATEMCS2, DESC92C_RATEMCS3,
				    DESC92C_RATEMCS4, DESC92C_RATEMCS5,
				    DESC92C_RATEMCS6, DESC92C_RATEMCS7};
		phy_set_txpower_index_by_rate_array(hw, rfpath,
						    rtlphy->current_chan_bw,
						    channel, ht_rates1t, 8);
	} else if (section == HT_MCS8_MCS15) {
		u8 ht_rates2t[]  = {DESC92C_RATEMCS8, DESC92C_RATEMCS9,
				    DESC92C_RATEMCS10, DESC92C_RATEMCS11,
				    DESC92C_RATEMCS12, DESC92C_RATEMCS13,
				    DESC92C_RATEMCS14, DESC92C_RATEMCS15};
		phy_set_txpower_index_by_rate_array(hw, rfpath,
						    rtlphy->current_chan_bw,
						    channel, ht_rates2t, 8);
	} else
		RT_TRACE(rtlpriv, FPHY, PHY_TXPWR,
			 "Invalid RateSection %d\n", section);
}

void rtl92ee_phy_set_txpower_level(struct ieee80211_hw *hw, u8 channel)
{
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_phy *rtlphy = &rtl_priv(hw)->phy;
	enum radio_path rfpath;

	if (!rtlefuse->txpwr_fromeprom)
		return;
	for (rfpath = RF90_PATH_A; rfpath < rtlphy->num_total_rfpath;
	     rfpath++) {
		phy_set_txpower_index_by_rate_section(hw, rfpath,
						      channel, CCK);
		phy_set_txpower_index_by_rate_section(hw, rfpath,
						      channel, OFDM);
		phy_set_txpower_index_by_rate_section(hw, rfpath,
						      channel,
						      HT_MCS0_MCS7);

		if (rtlphy->num_total_rfpath >= 2)
			phy_set_txpower_index_by_rate_section(hw,
							      rfpath, channel,
							      HT_MCS8_MCS15);
	}
}

static long _rtl92ee_phy_txpwr_idx_to_dbm(struct ieee80211_hw *hw,
					  enum wireless_mode wirelessmode,
					  u8 txpwridx)
{
	long offset;
	long pwrout_dbm;

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
	pwrout_dbm = txpwridx / 2 + offset;
	return pwrout_dbm;
}

void rtl92ee_phy_scan_operation_backup(struct ieee80211_hw *hw, u8 operation)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	enum io_type iotype;

	if (!is_hal_stop(rtlhal)) {
		switch (operation) {
		case SCAN_OPT_BACKUP_BAND0:
			iotype = IO_CMD_PAUSE_BAND0_DM_BY_SCAN;
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

void rtl92ee_phy_set_bw_mode_callback(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_phy *rtlphy = &rtlpriv->phy;
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
		rtl_set_bbreg(hw, ROFDM0_TXPSEUDONOISEWGT,
			      (BIT(31) | BIT(30)), 0);
		break;
	case HT_CHANNEL_WIDTH_20_40:
		rtl_set_bbreg(hw, RFPGA0_RFMOD, BRFMOD, 0x1);
		rtl_set_bbreg(hw, RFPGA1_RFMOD, BRFMOD, 0x1);
		rtl_set_bbreg(hw, RCCK0_SYSTEM, BCCK_SIDEBAND,
			      (mac->cur_40_prime_sc >> 1));
		rtl_set_bbreg(hw, ROFDM1_LSTF, 0xC00,
			      mac->cur_40_prime_sc);

		rtl_set_bbreg(hw, 0x818, (BIT(26) | BIT(27)),
			      (mac->cur_40_prime_sc ==
			       HAL_PRIME_CHNL_OFFSET_LOWER) ? 2 : 1);
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "unknown bandwidth: %#X\n", rtlphy->current_chan_bw);
		break;
	}
	rtl92ee_phy_rf6052_set_bandwidth(hw, rtlphy->current_chan_bw);
	rtlphy->set_bwmode_inprogress = false;
	RT_TRACE(rtlpriv, COMP_SCAN, DBG_LOUD, "\n");
}

void rtl92ee_phy_set_bw_mode(struct ieee80211_hw *hw,
			     enum nl80211_channel_type ch_type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 tmp_bw = rtlphy->current_chan_bw;

	if (rtlphy->set_bwmode_inprogress)
		return;
	rtlphy->set_bwmode_inprogress = true;
	if ((!is_hal_stop(rtlhal)) && !(RT_CANNOT_IO(hw))) {
		rtl92ee_phy_set_bw_mode_callback(hw);
	} else {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 "false driver sleep or unload\n");
		rtlphy->set_bwmode_inprogress = false;
		rtlphy->current_chan_bw = tmp_bw;
	}
}

void rtl92ee_phy_sw_chnl_callback(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u32 delay;

	RT_TRACE(rtlpriv, COMP_SCAN, DBG_TRACE,
		 "switch to channel%d\n", rtlphy->current_channel);
	if (is_hal_stop(rtlhal))
		return;
	do {
		if (!rtlphy->sw_chnl_inprogress)
			break;
		if (!_rtl92ee_phy_sw_chnl_step_by_step
		    (hw, rtlphy->current_channel, &rtlphy->sw_chnl_stage,
		     &rtlphy->sw_chnl_step, &delay)) {
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

u8 rtl92ee_phy_sw_chnl(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
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
		rtl92ee_phy_sw_chnl_callback(hw);
		RT_TRACE(rtlpriv, COMP_CHAN, DBG_LOUD,
			 "sw_chnl_inprogress false schdule workitem current channel %d\n",
			 rtlphy->current_channel);
		rtlphy->sw_chnl_inprogress = false;
	} else {
		RT_TRACE(rtlpriv, COMP_CHAN, DBG_LOUD,
			 "sw_chnl_inprogress false driver sleep or unload\n");
		rtlphy->sw_chnl_inprogress = false;
	}
	return 1;
}

static bool _rtl92ee_phy_sw_chnl_step_by_step(struct ieee80211_hw *hw,
					      u8 channel, u8 *stage, u8 *step,
					      u32 *delay)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
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
	_rtl92ee_phy_set_sw_chnl_cmdarray(precommoncmd, precommoncmdcnt++,
					  MAX_PRECMD_CNT,
					  CMDID_SET_TXPOWEROWER_LEVEL, 0, 0, 0);
	_rtl92ee_phy_set_sw_chnl_cmdarray(precommoncmd, precommoncmdcnt++,
					  MAX_PRECMD_CNT, CMDID_END, 0, 0, 0);

	postcommoncmdcnt = 0;

	_rtl92ee_phy_set_sw_chnl_cmdarray(postcommoncmd, postcommoncmdcnt++,
					  MAX_POSTCMD_CNT, CMDID_END, 0, 0, 0);

	rfdependcmdcnt = 0;

	RT_ASSERT((channel >= 1 && channel <= 14),
		  "illegal channel for Zebra: %d\n", channel);

	_rtl92ee_phy_set_sw_chnl_cmdarray(rfdependcmd, rfdependcmdcnt++,
					  MAX_RFDEPENDCMD_CNT,
					  CMDID_RF_WRITEREG,
					  RF_CHNLBW, channel, 10);

	_rtl92ee_phy_set_sw_chnl_cmdarray(rfdependcmd, rfdependcmdcnt++,
					  MAX_RFDEPENDCMD_CNT, CMDID_END,
					  0, 0, 0);

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
		default:
			RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
				 "Invalid 'stage' = %d, Check it!\n" , *stage);
			return true;
		}

		if (currentcmd->cmdid == CMDID_END) {
			if ((*stage) == 2)
				return true;
			(*stage)++;
			(*step) = 0;
			continue;
		}

		switch (currentcmd->cmdid) {
		case CMDID_SET_TXPOWEROWER_LEVEL:
			rtl92ee_phy_set_txpower_level(hw, channel);
			break;
		case CMDID_WRITEPORT_ULONG:
			rtl_write_dword(rtlpriv, currentcmd->para1,
					currentcmd->para2);
			break;
		case CMDID_WRITEPORT_USHORT:
			rtl_write_word(rtlpriv, currentcmd->para1,
				       (u16)currentcmd->para2);
			break;
		case CMDID_WRITEPORT_UCHAR:
			rtl_write_byte(rtlpriv, currentcmd->para1,
				       (u8)currentcmd->para2);
			break;
		case CMDID_RF_WRITEREG:
			for (rfpath = 0; rfpath < num_total_rfpath; rfpath++) {
				rtlphy->rfreg_chnlval[rfpath] =
					((rtlphy->rfreg_chnlval[rfpath] &
					  0xfffff00) | currentcmd->para2);

				rtl_set_rfreg(hw, (enum radio_path)rfpath,
					      currentcmd->para1,
					      0x3ff,
					      rtlphy->rfreg_chnlval[rfpath]);
			}
			break;
		default:
			RT_TRACE(rtlpriv, COMP_ERR, DBG_LOUD,
				 "switch case not process\n");
			break;
		}

		break;
	} while (true);

	(*delay) = currentcmd->msdelay;
	(*step)++;
	return false;
}

static bool _rtl92ee_phy_set_sw_chnl_cmdarray(struct swchnlcmd *cmdtable,
					      u32 cmdtableidx, u32 cmdtablesz,
					      enum swchnlcmd_id cmdid,
					      u32 para1, u32 para2, u32 msdelay)
{
	struct swchnlcmd *pcmd;

	if (cmdtable == NULL) {
		RT_ASSERT(false, "cmdtable cannot be NULL.\n");
		return false;
	}

	if (cmdtableidx >= cmdtablesz)
		return false;

	pcmd = cmdtable + cmdtableidx;
	pcmd->cmdid = cmdid;
	pcmd->para1 = para1;
	pcmd->para2 = para2;
	pcmd->msdelay = msdelay;
	return true;
}

static u8 _rtl92ee_phy_path_a_iqk(struct ieee80211_hw *hw, bool config_pathb)
{
	u32 reg_eac, reg_e94, reg_e9c;
	u8 result = 0x00;
	/* path-A IQK setting */
	/* PA/PAD controlled by 0x0 */
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x00000000);
	rtl_set_rfreg(hw, RF90_PATH_A, 0xdf, RFREG_OFFSET_MASK, 0x180);
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x80800000);

	rtl_set_bbreg(hw, RTX_IQK_TONE_A, MASKDWORD, 0x18008c1c);
	rtl_set_bbreg(hw, RRX_IQK_TONE_A, MASKDWORD, 0x38008c1c);
	rtl_set_bbreg(hw, RTX_IQK_TONE_B, MASKDWORD, 0x38008c1c);
	rtl_set_bbreg(hw, RRX_IQK_TONE_B, MASKDWORD, 0x38008c1c);

	rtl_set_bbreg(hw, RTX_IQK_PI_A, MASKDWORD, 0x82140303);
	rtl_set_bbreg(hw, RRX_IQK_PI_A, MASKDWORD, 0x68160000);

	/*LO calibration setting*/
	rtl_set_bbreg(hw, RIQK_AGC_RSP, MASKDWORD, 0x00462911);

	/*One shot, path A LOK & IQK*/
	rtl_set_bbreg(hw, RIQK_AGC_PTS, MASKDWORD, 0xf9000000);
	rtl_set_bbreg(hw, RIQK_AGC_PTS, MASKDWORD, 0xf8000000);

	mdelay(IQK_DELAY_TIME);

	reg_eac = rtl_get_bbreg(hw, 0xeac, MASKDWORD);
	reg_e94 = rtl_get_bbreg(hw, 0xe94, MASKDWORD);
	reg_e9c = rtl_get_bbreg(hw, 0xe9c, MASKDWORD);

	if (!(reg_eac & BIT(28)) &&
	    (((reg_e94 & 0x03FF0000) >> 16) != 0x142) &&
	    (((reg_e9c & 0x03FF0000) >> 16) != 0x42))
		result |= 0x01;
	else
		return result;

	return result;
}

static u8 _rtl92ee_phy_path_b_iqk(struct ieee80211_hw *hw)
{
	u32 reg_eac, reg_eb4, reg_ebc;
	u8 result = 0x00;

	/* PA/PAD controlled by 0x0 */
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x00000000);
	rtl_set_rfreg(hw, RF90_PATH_B, 0xdf, RFREG_OFFSET_MASK, 0x180);
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x80800000);

	rtl_set_bbreg(hw, 0xe28, MASKDWORD, 0x00000000);
	rtl_set_bbreg(hw, 0xe28, MASKDWORD, 0x80800000);

	rtl_set_bbreg(hw, RTX_IQK_TONE_A, MASKDWORD, 0x38008c1c);
	rtl_set_bbreg(hw, RRX_IQK_TONE_A, MASKDWORD, 0x38008c1c);
	rtl_set_bbreg(hw, RTX_IQK_TONE_B, MASKDWORD, 0x18008c1c);
	rtl_set_bbreg(hw, RRX_IQK_TONE_B, MASKDWORD, 0x38008c1c);

	rtl_set_bbreg(hw, RTX_IQK_PI_B, MASKDWORD, 0x821403e2);
	rtl_set_bbreg(hw, RRX_IQK_PI_B, MASKDWORD, 0x68160000);

	/* LO calibration setting */
	rtl_set_bbreg(hw, RIQK_AGC_RSP, MASKDWORD, 0x00462911);

	/*One shot, path B LOK & IQK*/
	rtl_set_bbreg(hw, RIQK_AGC_PTS, MASKDWORD, 0xfa000000);
	rtl_set_bbreg(hw, RIQK_AGC_PTS, MASKDWORD, 0xf8000000);

	mdelay(IQK_DELAY_TIME);

	reg_eac = rtl_get_bbreg(hw, 0xeac, MASKDWORD);
	reg_eb4 = rtl_get_bbreg(hw, 0xeb4, MASKDWORD);
	reg_ebc = rtl_get_bbreg(hw, 0xebc, MASKDWORD);

	if (!(reg_eac & BIT(31)) &&
	    (((reg_eb4 & 0x03FF0000) >> 16) != 0x142) &&
	    (((reg_ebc & 0x03FF0000) >> 16) != 0x42))
		result |= 0x01;
	else
		return result;

	return result;
}

static u8 _rtl92ee_phy_path_a_rx_iqk(struct ieee80211_hw *hw, bool config_pathb)
{
	u32 reg_eac, reg_e94, reg_e9c, reg_ea4 , u32temp;
	u8 result = 0x00;

	/*Get TXIMR Setting*/
	/*Modify RX IQK mode table*/
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x00000000);

	rtl_set_rfreg(hw, RF90_PATH_A, RF_WE_LUT, RFREG_OFFSET_MASK, 0x800a0);
	rtl_set_rfreg(hw, RF90_PATH_A, RF_RCK_OS, RFREG_OFFSET_MASK, 0x30000);
	rtl_set_rfreg(hw, RF90_PATH_A, RF_TXPA_G1, RFREG_OFFSET_MASK, 0x0000f);
	rtl_set_rfreg(hw, RF90_PATH_A, RF_TXPA_G2, RFREG_OFFSET_MASK, 0xf117b);

	/*PA/PAD control by 0x56, and set = 0x0*/
	rtl_set_rfreg(hw, RF90_PATH_A, 0xdf, RFREG_OFFSET_MASK, 0x980);
	rtl_set_rfreg(hw, RF90_PATH_A, 0x56, RFREG_OFFSET_MASK, 0x51000);

	/*enter IQK mode*/
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x80800000);

	/*IQK Setting*/
	rtl_set_bbreg(hw, RTX_IQK, MASKDWORD, 0x01007c00);
	rtl_set_bbreg(hw, RRX_IQK, MASKDWORD, 0x01004800);

	/*path a IQK setting*/
	rtl_set_bbreg(hw, RTX_IQK_TONE_A, MASKDWORD, 0x18008c1c);
	rtl_set_bbreg(hw, RRX_IQK_TONE_A, MASKDWORD, 0x38008c1c);
	rtl_set_bbreg(hw, RTX_IQK_TONE_B, MASKDWORD, 0x38008c1c);
	rtl_set_bbreg(hw, RRX_IQK_TONE_B, MASKDWORD, 0x38008c1c);

	rtl_set_bbreg(hw, RTX_IQK_PI_A, MASKDWORD, 0x82160c1f);
	rtl_set_bbreg(hw, RRX_IQK_PI_A, MASKDWORD, 0x68160c1f);

	/*LO calibration Setting*/
	rtl_set_bbreg(hw, RIQK_AGC_RSP, MASKDWORD, 0x0046a911);

	/*one shot,path A LOK & iqk*/
	rtl_set_bbreg(hw, RIQK_AGC_PTS, MASKDWORD, 0xfa000000);
	rtl_set_bbreg(hw, RIQK_AGC_PTS, MASKDWORD, 0xf8000000);

	mdelay(IQK_DELAY_TIME);

	/* Check failed */
	reg_eac = rtl_get_bbreg(hw, RRX_POWER_AFTER_IQK_A_2, MASKDWORD);
	reg_e94 = rtl_get_bbreg(hw, RTX_POWER_BEFORE_IQK_A, MASKDWORD);
	reg_e9c = rtl_get_bbreg(hw, RTX_POWER_AFTER_IQK_A, MASKDWORD);

	if (!(reg_eac & BIT(28)) &&
	    (((reg_e94 & 0x03FF0000) >> 16) != 0x142) &&
	    (((reg_e9c & 0x03FF0000) >> 16) != 0x42)) {
		result |= 0x01;
	} else {
		/*	PA/PAD controlled by 0x0 */
		rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x00000000);
		rtl_set_rfreg(hw, RF90_PATH_A, 0xdf, RFREG_OFFSET_MASK, 0x180);
		return result;
	}

	u32temp = 0x80007C00 | (reg_e94 & 0x3FF0000)  |
		  ((reg_e9c & 0x3FF0000) >> 16);
	rtl_set_bbreg(hw, RTX_IQK, MASKDWORD, u32temp);
	/*RX IQK*/
	/*Modify RX IQK mode table*/
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x00000000);

	rtl_set_rfreg(hw, RF90_PATH_A, RF_WE_LUT, RFREG_OFFSET_MASK, 0x800a0);

	rtl_set_rfreg(hw, RF90_PATH_A, RF_RCK_OS, RFREG_OFFSET_MASK, 0x30000);
	rtl_set_rfreg(hw, RF90_PATH_A, RF_TXPA_G1, RFREG_OFFSET_MASK, 0x0000f);
	rtl_set_rfreg(hw, RF90_PATH_A, RF_TXPA_G2, RFREG_OFFSET_MASK, 0xf7ffa);

	/*PA/PAD control by 0x56, and set = 0x0*/
	rtl_set_rfreg(hw, RF90_PATH_A, 0xdf, RFREG_OFFSET_MASK, 0x980);
	rtl_set_rfreg(hw, RF90_PATH_A, 0x56, RFREG_OFFSET_MASK, 0x51000);

	/*enter IQK mode*/
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x80800000);

	/*IQK Setting*/
	rtl_set_bbreg(hw, RRX_IQK, MASKDWORD, 0x01004800);

	/*path a IQK setting*/
	rtl_set_bbreg(hw, RTX_IQK_TONE_A, MASKDWORD, 0x38008c1c);
	rtl_set_bbreg(hw, RRX_IQK_TONE_A, MASKDWORD, 0x18008c1c);
	rtl_set_bbreg(hw, RTX_IQK_TONE_B, MASKDWORD, 0x38008c1c);
	rtl_set_bbreg(hw, RRX_IQK_TONE_B, MASKDWORD, 0x38008c1c);

	rtl_set_bbreg(hw, RTX_IQK_PI_A, MASKDWORD, 0x82160c1f);
	rtl_set_bbreg(hw, RRX_IQK_PI_A, MASKDWORD, 0x28160c1f);

	/*LO calibration Setting*/
	rtl_set_bbreg(hw, RIQK_AGC_RSP, MASKDWORD, 0x0046a891);
	/*one shot,path A LOK & iqk*/
	rtl_set_bbreg(hw, RIQK_AGC_PTS, MASKDWORD, 0xfa000000);
	rtl_set_bbreg(hw, RIQK_AGC_PTS, MASKDWORD, 0xf8000000);

	mdelay(IQK_DELAY_TIME);
	/*Check failed*/
	reg_eac = rtl_get_bbreg(hw, RRX_POWER_AFTER_IQK_A_2, MASKDWORD);
	reg_ea4 = rtl_get_bbreg(hw, RRX_POWER_BEFORE_IQK_A_2, MASKDWORD);

	/*PA/PAD controlled by 0x0*/
	/*leave IQK mode*/
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x00000000);
	rtl_set_rfreg(hw, RF90_PATH_A, 0xdf, RFREG_OFFSET_MASK, 0x180);
	/*if Tx is OK, check whether Rx is OK*/
	if (!(reg_eac & BIT(27)) &&
	    (((reg_ea4 & 0x03FF0000) >> 16) != 0x132) &&
	    (((reg_eac & 0x03FF0000) >> 16) != 0x36))
		result |= 0x02;

	return result;
}

static u8 _rtl92ee_phy_path_b_rx_iqk(struct ieee80211_hw *hw, bool config_pathb)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 reg_eac, reg_eb4, reg_ebc, reg_ecc, reg_ec4, u32temp;
	u8 result = 0x00;

	/*Get TXIMR Setting*/
	/*Modify RX IQK mode table*/
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x00000000);

	rtl_set_rfreg(hw, RF90_PATH_B, RF_WE_LUT, RFREG_OFFSET_MASK, 0x800a0);
	rtl_set_rfreg(hw, RF90_PATH_B, RF_RCK_OS, RFREG_OFFSET_MASK, 0x30000);
	rtl_set_rfreg(hw, RF90_PATH_B, RF_TXPA_G1, RFREG_OFFSET_MASK, 0x0000f);
	rtl_set_rfreg(hw, RF90_PATH_B, RF_TXPA_G2, RFREG_OFFSET_MASK, 0xf117b);

	/*PA/PAD all off*/
	rtl_set_rfreg(hw, RF90_PATH_B, 0xdf, RFREG_OFFSET_MASK, 0x980);
	rtl_set_rfreg(hw, RF90_PATH_B, 0x56, RFREG_OFFSET_MASK, 0x51000);

	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x80800000);

	/*IQK Setting*/
	rtl_set_bbreg(hw, RTX_IQK, MASKDWORD, 0x01007c00);
	rtl_set_bbreg(hw, RRX_IQK, MASKDWORD, 0x01004800);

	/*path a IQK setting*/
	rtl_set_bbreg(hw, RTX_IQK_TONE_A, MASKDWORD, 0x38008c1c);
	rtl_set_bbreg(hw, RRX_IQK_TONE_A, MASKDWORD, 0x38008c1c);
	rtl_set_bbreg(hw, RTX_IQK_TONE_B, MASKDWORD, 0x18008c1c);
	rtl_set_bbreg(hw, RRX_IQK_TONE_B, MASKDWORD, 0x38008c1c);

	rtl_set_bbreg(hw, RTX_IQK_PI_B, MASKDWORD, 0x82160c1f);
	rtl_set_bbreg(hw, RRX_IQK_PI_B, MASKDWORD, 0x68160c1f);

	/*LO calibration Setting*/
	rtl_set_bbreg(hw, RIQK_AGC_RSP, MASKDWORD, 0x0046a911);

	/*one shot,path A LOK & iqk*/
	rtl_set_bbreg(hw, RIQK_AGC_PTS, MASKDWORD, 0xfa000000);
	rtl_set_bbreg(hw, RIQK_AGC_PTS, MASKDWORD, 0xf8000000);

	mdelay(IQK_DELAY_TIME);

	/* Check failed */
	reg_eac = rtl_get_bbreg(hw, RRX_POWER_AFTER_IQK_A_2, MASKDWORD);
	reg_eb4 = rtl_get_bbreg(hw, RTX_POWER_BEFORE_IQK_B, MASKDWORD);
	reg_ebc = rtl_get_bbreg(hw, RTX_POWER_AFTER_IQK_B, MASKDWORD);

	if (!(reg_eac & BIT(31)) &&
	    (((reg_eb4 & 0x03FF0000) >> 16) != 0x142) &&
	    (((reg_ebc & 0x03FF0000) >> 16) != 0x42)) {
		result |= 0x01;
	} else {
		/*	PA/PAD controlled by 0x0 */
		rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x00000000);
		rtl_set_rfreg(hw, RF90_PATH_B, 0xdf, RFREG_OFFSET_MASK, 0x180);
		return result;
	}

	u32temp = 0x80007C00 | (reg_eb4 & 0x3FF0000) |
		  ((reg_ebc & 0x3FF0000) >> 16);
	rtl_set_bbreg(hw, RTX_IQK, MASKDWORD, u32temp);
	/*RX IQK*/
	/*Modify RX IQK mode table*/
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x00000000);
	rtl_set_rfreg(hw, RF90_PATH_B, RF_WE_LUT, RFREG_OFFSET_MASK, 0x800a0);

	rtl_set_rfreg(hw, RF90_PATH_B, RF_RCK_OS, RFREG_OFFSET_MASK, 0x30000);
	rtl_set_rfreg(hw, RF90_PATH_B, RF_TXPA_G1, RFREG_OFFSET_MASK, 0x0000f);
	rtl_set_rfreg(hw, RF90_PATH_B, RF_TXPA_G2, RFREG_OFFSET_MASK, 0xf7ffa);

	/*PA/PAD all off*/
	rtl_set_rfreg(hw, RF90_PATH_B, 0xdf, RFREG_OFFSET_MASK, 0x980);
	rtl_set_rfreg(hw, RF90_PATH_B, 0x56, RFREG_OFFSET_MASK, 0x51000);

	/*enter IQK mode*/
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x80800000);

	/*IQK Setting*/
	rtl_set_bbreg(hw, RRX_IQK, MASKDWORD, 0x01004800);

	/*path b IQK setting*/
	rtl_set_bbreg(hw, RTX_IQK_TONE_A, MASKDWORD, 0x38008c1c);
	rtl_set_bbreg(hw, RRX_IQK_TONE_A, MASKDWORD, 0x38008c1c);
	rtl_set_bbreg(hw, RTX_IQK_TONE_B, MASKDWORD, 0x38008c1c);
	rtl_set_bbreg(hw, RRX_IQK_TONE_B, MASKDWORD, 0x18008c1c);

	rtl_set_bbreg(hw, RTX_IQK_PI_B, MASKDWORD, 0x82160c1f);
	rtl_set_bbreg(hw, RRX_IQK_PI_B, MASKDWORD, 0x28160c1f);

	/*LO calibration Setting*/
	rtl_set_bbreg(hw, RIQK_AGC_RSP, MASKDWORD, 0x0046a891);
	/*one shot,path A LOK & iqk*/
	rtl_set_bbreg(hw, RIQK_AGC_PTS, MASKDWORD, 0xfa000000);
	rtl_set_bbreg(hw, RIQK_AGC_PTS, MASKDWORD, 0xf8000000);

	mdelay(IQK_DELAY_TIME);
	/*Check failed*/
	reg_eac = rtl_get_bbreg(hw, RRX_POWER_AFTER_IQK_A_2, MASKDWORD);
	reg_ec4 = rtl_get_bbreg(hw, RRX_POWER_BEFORE_IQK_B_2, MASKDWORD);
	reg_ecc = rtl_get_bbreg(hw, RRX_POWER_AFTER_IQK_B_2, MASKDWORD);
	/*PA/PAD controlled by 0x0*/
	/*leave IQK mode*/
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x00000000);
	rtl_set_rfreg(hw, RF90_PATH_B, 0xdf, RFREG_OFFSET_MASK, 0x180);
	/*if Tx is OK, check whether Rx is OK*/
	if (!(reg_eac & BIT(30)) &&
	    (((reg_ec4 & 0x03FF0000) >> 16) != 0x132) &&
	    (((reg_ecc & 0x03FF0000) >> 16) != 0x36))
		result |= 0x02;
	else
		RT_TRACE(rtlpriv, COMP_RF, DBG_LOUD, "Path B Rx IQK fail!!\n");

	return result;
}

static void _rtl92ee_phy_path_a_fill_iqk_matrix(struct ieee80211_hw *hw,
						bool b_iqk_ok, long result[][8],
						u8 final_candidate,
						bool btxonly)
{
	u32 oldval_0, x, tx0_a, reg;
	long y, tx0_c;

	if (final_candidate == 0xFF) {
		return;
	} else if (b_iqk_ok) {
		oldval_0 = (rtl_get_bbreg(hw, ROFDM0_XATXIQIMBALANCE,
					  MASKDWORD) >> 22) & 0x3FF;
		x = result[final_candidate][0];
		if ((x & 0x00000200) != 0)
			x = x | 0xFFFFFC00;
		tx0_a = (x * oldval_0) >> 8;
		rtl_set_bbreg(hw, ROFDM0_XATXIQIMBALANCE, 0x3FF, tx0_a);
		rtl_set_bbreg(hw, ROFDM0_ECCATHRESHOLD, BIT(31),
			      ((x * oldval_0 >> 7) & 0x1));
		y = result[final_candidate][1];
		if ((y & 0x00000200) != 0)
			y = y | 0xFFFFFC00;
		tx0_c = (y * oldval_0) >> 8;
		rtl_set_bbreg(hw, ROFDM0_XCTXAFE, 0xF0000000,
			      ((tx0_c & 0x3C0) >> 6));
		rtl_set_bbreg(hw, ROFDM0_XATXIQIMBALANCE, 0x003F0000,
			      (tx0_c & 0x3F));
		rtl_set_bbreg(hw, ROFDM0_ECCATHRESHOLD, BIT(29),
			      ((y * oldval_0 >> 7) & 0x1));

		if (btxonly)
			return;

		reg = result[final_candidate][2];
		rtl_set_bbreg(hw, ROFDM0_XARXIQIMBALANCE, 0x3FF, reg);

		reg = result[final_candidate][3] & 0x3F;
		rtl_set_bbreg(hw, ROFDM0_XARXIQIMBALANCE, 0xFC00, reg);

		reg = (result[final_candidate][3] >> 6) & 0xF;
		rtl_set_bbreg(hw, ROFDM0_RXIQEXTANTA, 0xF0000000, reg);
	}
}

static void _rtl92ee_phy_path_b_fill_iqk_matrix(struct ieee80211_hw *hw,
						bool b_iqk_ok, long result[][8],
						u8 final_candidate,
						bool btxonly)
{
	u32 oldval_1, x, tx1_a, reg;
	long y, tx1_c;

	if (final_candidate == 0xFF) {
		return;
	} else if (b_iqk_ok) {
		oldval_1 = (rtl_get_bbreg(hw, ROFDM0_XATXIQIMBALANCE,
					  MASKDWORD) >> 22) & 0x3FF;
		x = result[final_candidate][4];
		if ((x & 0x00000200) != 0)
			x = x | 0xFFFFFC00;
		tx1_a = (x * oldval_1) >> 8;
		rtl_set_bbreg(hw, ROFDM0_XATXIQIMBALANCE, 0x3FF, tx1_a);
		rtl_set_bbreg(hw, ROFDM0_ECCATHRESHOLD, BIT(27),
			      ((x * oldval_1 >> 7) & 0x1));
		y = result[final_candidate][5];
		if ((y & 0x00000200) != 0)
			y = y | 0xFFFFFC00;
		tx1_c = (y * oldval_1) >> 8;
		rtl_set_bbreg(hw, ROFDM0_XDTXAFE, 0xF0000000,
			      ((tx1_c & 0x3C0) >> 6));
		rtl_set_bbreg(hw, ROFDM0_XBTXIQIMBALANCE, 0x003F0000,
			      (tx1_c & 0x3F));
		rtl_set_bbreg(hw, ROFDM0_ECCATHRESHOLD, BIT(25),
			      ((y * oldval_1 >> 7) & 0x1));

		if (btxonly)
			return;

		reg = result[final_candidate][6];
		rtl_set_bbreg(hw, ROFDM0_XBRXIQIMBALANCE, 0x3FF, reg);

		reg = result[final_candidate][7] & 0x3F;
		rtl_set_bbreg(hw, ROFDM0_XBRXIQIMBALANCE, 0xFC00, reg);

		reg = (result[final_candidate][7] >> 6) & 0xF;
		rtl_set_bbreg(hw, ROFDM0_AGCRSSITABLE, 0xF0000000, reg);
	}
}

static void _rtl92ee_phy_save_adda_registers(struct ieee80211_hw *hw,
					     u32 *addareg, u32 *addabackup,
					     u32 registernum)
{
	u32 i;

	for (i = 0; i < registernum; i++)
		addabackup[i] = rtl_get_bbreg(hw, addareg[i], MASKDWORD);
}

static void _rtl92ee_phy_save_mac_registers(struct ieee80211_hw *hw,
					    u32 *macreg, u32 *macbackup)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 i;

	for (i = 0; i < (IQK_MAC_REG_NUM - 1); i++)
		macbackup[i] = rtl_read_byte(rtlpriv, macreg[i]);

	macbackup[i] = rtl_read_dword(rtlpriv, macreg[i]);
}

static void _rtl92ee_phy_reload_adda_registers(struct ieee80211_hw *hw,
					       u32 *addareg, u32 *addabackup,
					       u32 regiesternum)
{
	u32 i;

	for (i = 0; i < regiesternum; i++)
		rtl_set_bbreg(hw, addareg[i], MASKDWORD, addabackup[i]);
}

static void _rtl92ee_phy_reload_mac_registers(struct ieee80211_hw *hw,
					      u32 *macreg, u32 *macbackup)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 i;

	for (i = 0; i < (IQK_MAC_REG_NUM - 1); i++)
		rtl_write_byte(rtlpriv, macreg[i], (u8)macbackup[i]);
	rtl_write_dword(rtlpriv, macreg[i], macbackup[i]);
}

static void _rtl92ee_phy_path_adda_on(struct ieee80211_hw *hw, u32 *addareg,
				      bool is_patha_on, bool is2t)
{
	u32 pathon;
	u32 i;

	pathon = is_patha_on ? 0x0fc01616 : 0x0fc01616;
	if (!is2t) {
		pathon = 0x0fc01616;
		rtl_set_bbreg(hw, addareg[0], MASKDWORD, 0x0fc01616);
	} else {
		rtl_set_bbreg(hw, addareg[0], MASKDWORD, pathon);
	}

	for (i = 1; i < IQK_ADDA_REG_NUM; i++)
		rtl_set_bbreg(hw, addareg[i], MASKDWORD, pathon);
}

static void _rtl92ee_phy_mac_setting_calibration(struct ieee80211_hw *hw,
						 u32 *macreg, u32 *macbackup)
{
	rtl_set_bbreg(hw, 0x520, 0x00ff0000, 0xff);
}

static void _rtl92ee_phy_path_a_standby(struct ieee80211_hw *hw)
{
	rtl_set_bbreg(hw, 0xe28, MASKDWORD, 0x0);
	rtl_set_rfreg(hw, RF90_PATH_A, 0, RFREG_OFFSET_MASK, 0x10000);
	rtl_set_bbreg(hw, 0xe28, MASKDWORD, 0x80800000);
}

static bool _rtl92ee_phy_simularity_compare(struct ieee80211_hw *hw,
					    long result[][8], u8 c1, u8 c2)
{
	u32 i, j, diff, simularity_bitmap, bound;

	u8 final_candidate[2] = { 0xFF, 0xFF };
	bool bresult = true/*, is2t = true*/;
	s32 tmp1, tmp2;

	bound = 8;

	simularity_bitmap = 0;

	for (i = 0; i < bound; i++) {
		if ((i == 1) || (i == 3) || (i == 5) || (i == 7)) {
			if ((result[c1][i] & 0x00000200) != 0)
				tmp1 = result[c1][i] | 0xFFFFFC00;
			else
				tmp1 = result[c1][i];

			if ((result[c2][i] & 0x00000200) != 0)
				tmp2 = result[c2][i] | 0xFFFFFC00;
			else
				tmp2 = result[c2][i];
		} else {
			tmp1 = result[c1][i];
			tmp2 = result[c2][i];
		}

		diff = (tmp1 > tmp2) ? (tmp1 - tmp2) : (tmp2 - tmp1);

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
	}
	if (!(simularity_bitmap & 0x03)) {/*path A TX OK*/
		for (i = 0; i < 2; i++)
			result[3][i] = result[c1][i];
	}
	if (!(simularity_bitmap & 0x0c)) {/*path A RX OK*/
		for (i = 2; i < 4; i++)
			result[3][i] = result[c1][i];
	}
	if (!(simularity_bitmap & 0x30)) {/*path B TX OK*/
		for (i = 4; i < 6; i++)
			result[3][i] = result[c1][i];
	}
	if (!(simularity_bitmap & 0xc0)) {/*path B RX OK*/
		for (i = 6; i < 8; i++)
			result[3][i] = result[c1][i];
	}
	return false;
}

static void _rtl92ee_phy_iq_calibrate(struct ieee80211_hw *hw,
				      long result[][8], u8 t, bool is2t)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u32 i;
	u8 patha_ok, pathb_ok;
	u8 tmp_0xc50 = (u8)rtl_get_bbreg(hw, 0xc50, MASKBYTE0);
	u8 tmp_0xc58 = (u8)rtl_get_bbreg(hw, 0xc58, MASKBYTE0);
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

	if (t == 0) {
		_rtl92ee_phy_save_adda_registers(hw, adda_reg,
						 rtlphy->adda_backup,
						 IQK_ADDA_REG_NUM);
		_rtl92ee_phy_save_mac_registers(hw, iqk_mac_reg,
						rtlphy->iqk_mac_backup);
		_rtl92ee_phy_save_adda_registers(hw, iqk_bb_reg,
						 rtlphy->iqk_bb_backup,
						 IQK_BB_REG_NUM);
	}

	_rtl92ee_phy_path_adda_on(hw, adda_reg, true, is2t);

	/*BB setting*/
	rtl_set_bbreg(hw, RFPGA0_RFMOD, BIT(24), 0x00);
	rtl_set_bbreg(hw, ROFDM0_TRXPATHENABLE, MASKDWORD, 0x03a05600);
	rtl_set_bbreg(hw, ROFDM0_TRMUXPAR, MASKDWORD, 0x000800e4);
	rtl_set_bbreg(hw, RFPGA0_XCD_RFINTERFACESW, MASKDWORD, 0x22208200);

	rtl_set_bbreg(hw, RFPGA0_XAB_RFINTERFACESW, BIT(10), 0x01);
	rtl_set_bbreg(hw, RFPGA0_XAB_RFINTERFACESW, BIT(26), 0x01);
	rtl_set_bbreg(hw, RFPGA0_XA_RFINTERFACEOE, BIT(10), 0x01);
	rtl_set_bbreg(hw, RFPGA0_XB_RFINTERFACEOE, BIT(10), 0x01);

	_rtl92ee_phy_mac_setting_calibration(hw, iqk_mac_reg,
					     rtlphy->iqk_mac_backup);
	/* Page B init*/
	/* IQ calibration setting*/
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x80800000);
	rtl_set_bbreg(hw, RTX_IQK, MASKDWORD, 0x01007c00);
	rtl_set_bbreg(hw, RRX_IQK, MASKDWORD, 0x01004800);

	for (i = 0 ; i < retrycount ; i++) {
		patha_ok = _rtl92ee_phy_path_a_iqk(hw, is2t);

		if (patha_ok == 0x01) {
			RT_TRACE(rtlpriv, COMP_RF, DBG_LOUD,
				 "Path A Tx IQK Success!!\n");
			result[t][0] = (rtl_get_bbreg(hw,
						      RTX_POWER_BEFORE_IQK_A,
						      MASKDWORD) & 0x3FF0000)
						      >> 16;
			result[t][1] = (rtl_get_bbreg(hw, RTX_POWER_AFTER_IQK_A,
						      MASKDWORD) & 0x3FF0000)
						      >> 16;
			break;
		}
		RT_TRACE(rtlpriv, COMP_RF, DBG_LOUD,
			 "Path A Tx IQK Fail!!, ret = 0x%x\n",
			 patha_ok);
	}

	for (i = 0 ; i < retrycount ; i++) {
		patha_ok = _rtl92ee_phy_path_a_rx_iqk(hw, is2t);

		if (patha_ok == 0x03) {
			RT_TRACE(rtlpriv, COMP_RF, DBG_LOUD,
				 "Path A Rx IQK Success!!\n");
			result[t][2] = (rtl_get_bbreg(hw,
						      RRX_POWER_BEFORE_IQK_A_2,
						      MASKDWORD) & 0x3FF0000)
						      >> 16;
			result[t][3] = (rtl_get_bbreg(hw,
						      RRX_POWER_AFTER_IQK_A_2,
						      MASKDWORD) & 0x3FF0000)
						      >> 16;
			break;
		}
		RT_TRACE(rtlpriv, COMP_RF, DBG_LOUD,
			 "Path A Rx IQK Fail!!, ret = 0x%x\n",
			  patha_ok);
	}

	if (0x00 == patha_ok)
		RT_TRACE(rtlpriv, COMP_RF, DBG_LOUD,
			 "Path A IQK failed!!, ret = 0\n");
	if (is2t) {
		_rtl92ee_phy_path_a_standby(hw);
		/* Turn Path B ADDA on */
		_rtl92ee_phy_path_adda_on(hw, adda_reg, false, is2t);

		/* IQ calibration setting */
		rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x80800000);
		rtl_set_bbreg(hw, RTX_IQK, MASKDWORD, 0x01007c00);
		rtl_set_bbreg(hw, RRX_IQK, MASKDWORD, 0x01004800);

		for (i = 0 ; i < retrycount ; i++) {
			pathb_ok = _rtl92ee_phy_path_b_iqk(hw);
			if (pathb_ok == 0x01) {
				RT_TRACE(rtlpriv, COMP_RF, DBG_LOUD,
					 "Path B Tx IQK Success!!\n");
				result[t][4] = (rtl_get_bbreg(hw,
							RTX_POWER_BEFORE_IQK_B,
							MASKDWORD) & 0x3FF0000)
							>> 16;
				result[t][5] = (rtl_get_bbreg(hw,
							RTX_POWER_AFTER_IQK_B,
							MASKDWORD) & 0x3FF0000)
							>> 16;
				break;
			}
			RT_TRACE(rtlpriv, COMP_RF, DBG_LOUD,
				 "Path B Tx IQK Fail!!, ret = 0x%x\n",
				 pathb_ok);
		}

		for (i = 0 ; i < retrycount ; i++) {
			pathb_ok = _rtl92ee_phy_path_b_rx_iqk(hw, is2t);
			if (pathb_ok == 0x03) {
				RT_TRACE(rtlpriv, COMP_RF, DBG_LOUD,
					 "Path B Rx IQK Success!!\n");
				result[t][6] = (rtl_get_bbreg(hw,
						       RRX_POWER_BEFORE_IQK_B_2,
						       MASKDWORD) & 0x3FF0000)
						       >> 16;
				result[t][7] = (rtl_get_bbreg(hw,
						       RRX_POWER_AFTER_IQK_B_2,
						       MASKDWORD) & 0x3FF0000)
						       >> 16;
				break;
			}
			RT_TRACE(rtlpriv, COMP_RF, DBG_LOUD,
				 "Path B Rx IQK Fail!!, ret = 0x%x\n",
				 pathb_ok);
		}

		if (0x00 == pathb_ok)
			RT_TRACE(rtlpriv, COMP_RF, DBG_LOUD,
				 "Path B IQK failed!!, ret = 0\n");
	}
	/* Back to BB mode, load original value */
	RT_TRACE(rtlpriv, COMP_RF, DBG_LOUD,
		 "IQK:Back to BB mode, load original value!\n");
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0);

	if (t != 0) {
		/* Reload ADDA power saving parameters */
		_rtl92ee_phy_reload_adda_registers(hw, adda_reg,
						   rtlphy->adda_backup,
						   IQK_ADDA_REG_NUM);

		/* Reload MAC parameters */
		_rtl92ee_phy_reload_mac_registers(hw, iqk_mac_reg,
						  rtlphy->iqk_mac_backup);

		_rtl92ee_phy_reload_adda_registers(hw, iqk_bb_reg,
						   rtlphy->iqk_bb_backup,
						   IQK_BB_REG_NUM);

		/* Restore RX initial gain */
		rtl_set_bbreg(hw, 0xc50, MASKBYTE0, 0x50);
		rtl_set_bbreg(hw, 0xc50, MASKBYTE0, tmp_0xc50);
		if (is2t) {
			rtl_set_bbreg(hw, 0xc50, MASKBYTE0, 0x50);
			rtl_set_bbreg(hw, 0xc58, MASKBYTE0, tmp_0xc58);
		}

		/* load 0xe30 IQC default value */
		rtl_set_bbreg(hw, RTX_IQK_TONE_A, MASKDWORD, 0x01008c00);
		rtl_set_bbreg(hw, RRX_IQK_TONE_A, MASKDWORD, 0x01008c00);
	}
}

static void _rtl92ee_phy_lc_calibrate(struct ieee80211_hw *hw, bool is2t)
{
	u8 tmpreg;
	u32 rf_a_mode = 0, rf_b_mode = 0, lc_cal;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

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

	rtl_set_rfreg(hw, RF90_PATH_A, 0x18, MASK12BITS, lc_cal | 0x08000);

	mdelay(100);

	if ((tmpreg & 0x70) != 0) {
		rtl_write_byte(rtlpriv, 0xd03, tmpreg);
		rtl_set_rfreg(hw, RF90_PATH_A, 0x00, MASK12BITS, rf_a_mode);

		if (is2t)
			rtl_set_rfreg(hw, RF90_PATH_B, 0x00, MASK12BITS,
				      rf_b_mode);
	} else {
		rtl_write_byte(rtlpriv, REG_TXPAUSE, 0x00);
	}
}

static void _rtl92ee_phy_set_rfpath_switch(struct ieee80211_hw *hw,
					   bool bmain, bool is2t)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));

	RT_TRACE(rtlpriv, COMP_INIT , DBG_LOUD , "\n");

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
				rtl_set_bbreg(hw, RCONFIG_RAM64x16, BIT(31), 0);
		} else {
			rtl_set_bbreg(hw, RFPGA0_XA_RFINTERFACEOE,
				      BIT(14) | BIT(13) | BIT(12), 1);
			rtl_set_bbreg(hw, RFPGA0_XB_RFINTERFACEOE,
				      BIT(5) | BIT(4) | BIT(3), 1);
			if (rtlefuse->antenna_div_type == CGCS_RX_HW_ANTDIV)
				rtl_set_bbreg(hw, RCONFIG_RAM64x16, BIT(31), 1);
		}
	}
}

#undef IQK_ADDA_REG_NUM
#undef IQK_DELAY_TIME

static u8 rtl92ee_get_rightchnlplace_for_iqk(u8 chnl)
{
	u8 channel_all[59] = {
		1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
		36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58,
		60, 62, 64, 100, 102, 104, 106, 108, 110, 112,
		114, 116, 118, 120, 122, 124, 126, 128,	130,
		132, 134, 136, 138, 140, 149, 151, 153, 155,
		157, 159, 161, 163, 165
	};
	u8 place = chnl;

	if (chnl > 14) {
		for (place = 14; place < sizeof(channel_all); place++) {
			if (channel_all[place] == chnl)
				return place - 13;
		}
	}

	return 0;
}

void rtl92ee_phy_iq_calibrate(struct ieee80211_hw *hw, bool b_recovery)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	long result[4][8];
	u8 i, final_candidate;
	bool b_patha_ok, b_pathb_ok;
	long reg_e94, reg_e9c, reg_ea4, reg_eac;
	long reg_eb4, reg_ebc, reg_ec4, reg_ecc;
	bool is12simular, is13simular, is23simular;
	u8 idx;
	u32 iqk_bb_reg[IQK_BB_REG_NUM] = {
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

	if (b_recovery) {
		_rtl92ee_phy_reload_adda_registers(hw, iqk_bb_reg,
						   rtlphy->iqk_bb_backup, 9);
		return;
	}

	for (i = 0; i < 8; i++) {
		result[0][i] = 0;
		result[1][i] = 0;
		result[2][i] = 0;

		if ((i == 0) || (i == 2) || (i == 4)  || (i == 6))
			result[3][i] = 0x100;
		else
			result[3][i] = 0;
	}
	final_candidate = 0xff;
	b_patha_ok = false;
	b_pathb_ok = false;
	is12simular = false;
	is23simular = false;
	is13simular = false;
	for (i = 0; i < 3; i++) {
		_rtl92ee_phy_iq_calibrate(hw, result, i, true);
		if (i == 1) {
			is12simular = _rtl92ee_phy_simularity_compare(hw,
								      result,
								      0, 1);
			if (is12simular) {
				final_candidate = 0;
				break;
			}
		}

		if (i == 2) {
			is13simular = _rtl92ee_phy_simularity_compare(hw,
								      result,
								      0, 2);
			if (is13simular) {
				final_candidate = 0;
				break;
			}
			is23simular = _rtl92ee_phy_simularity_compare(hw,
								      result,
								      1, 2);
			if (is23simular)
				final_candidate = 1;
			else
				final_candidate = 3;
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
		b_patha_ok = true;
		b_pathb_ok = true;
	} else {
		rtlphy->reg_e94 = 0x100;
		rtlphy->reg_eb4 = 0x100;
		rtlphy->reg_e9c = 0x0;
		rtlphy->reg_ebc = 0x0;
	}

	if (reg_e94 != 0)
		_rtl92ee_phy_path_a_fill_iqk_matrix(hw, b_patha_ok, result,
						    final_candidate,
						    (reg_ea4 == 0));

	_rtl92ee_phy_path_b_fill_iqk_matrix(hw, b_pathb_ok, result,
					    final_candidate,
					    (reg_ec4 == 0));

	idx = rtl92ee_get_rightchnlplace_for_iqk(rtlphy->current_channel);

	/* To Fix BSOD when final_candidate is 0xff */
	if (final_candidate < 4) {
		for (i = 0; i < IQK_MATRIX_REG_NUM; i++)
			rtlphy->iqk_matrix[idx].value[0][i] =
				result[final_candidate][i];

		rtlphy->iqk_matrix[idx].iqk_done = true;
	}
	_rtl92ee_phy_save_adda_registers(hw, iqk_bb_reg,
					 rtlphy->iqk_bb_backup, 9);
}

void rtl92ee_phy_lc_calibrate(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_hal *rtlhal = &rtlpriv->rtlhal;
	u32 timeout = 2000, timecount = 0;

	while (rtlpriv->mac80211.act_scanning && timecount < timeout) {
		udelay(50);
		timecount += 50;
	}

	rtlphy->lck_inprogress = true;
	RTPRINT(rtlpriv, FINIT, INIT_IQK,
		"LCK:Start!!! currentband %x delay %d ms\n",
		 rtlhal->current_bandtype, timecount);

	_rtl92ee_phy_lc_calibrate(hw, false);

	rtlphy->lck_inprogress = false;
}

void rtl92ee_phy_ap_calibrate(struct ieee80211_hw *hw, char delta)
{
}

void rtl92ee_phy_set_rfpath_switch(struct ieee80211_hw *hw, bool bmain)
{
	_rtl92ee_phy_set_rfpath_switch(hw, bmain, false);
}

bool rtl92ee_phy_set_io_cmd(struct ieee80211_hw *hw, enum io_type iotype)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
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
		case IO_CMD_PAUSE_BAND0_DM_BY_SCAN:
			RT_TRACE(rtlpriv, COMP_CMD, DBG_TRACE,
				 "[IO CMD] Pause DM before scan.\n");
			postprocessing = true;
			break;
		default:
			RT_TRACE(rtlpriv, COMP_ERR, DBG_LOUD,
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
	rtl92ee_phy_set_io(hw);
	RT_TRACE(rtlpriv, COMP_CMD, DBG_TRACE, "IO Type(%#x)\n", iotype);
	return true;
}

static void rtl92ee_phy_set_io(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct dig_t *dm_dig = &rtlpriv->dm_digtable;

	RT_TRACE(rtlpriv, COMP_CMD, DBG_TRACE,
		 "--->Cmd(%#x), set_io_inprogress(%d)\n",
		  rtlphy->current_io_type, rtlphy->set_io_inprogress);
	switch (rtlphy->current_io_type) {
	case IO_CMD_RESUME_DM_BY_SCAN:
		rtl92ee_dm_write_dig(hw, rtlphy->initgain_backup.xaagccore1);
		rtl92ee_dm_write_cck_cca_thres(hw, rtlphy->initgain_backup.cca);
		RT_TRACE(rtlpriv, COMP_CMD, DBG_TRACE , "no set txpower\n");
		rtl92ee_phy_set_txpower_level(hw, rtlphy->current_channel);
		break;
	case IO_CMD_PAUSE_BAND0_DM_BY_SCAN:
		/* 8192eebt */
		rtlphy->initgain_backup.xaagccore1 = dm_dig->cur_igvalue;
		rtl92ee_dm_write_dig(hw, 0x17);
		rtlphy->initgain_backup.cca = dm_dig->cur_cck_cca_thres;
		rtl92ee_dm_write_cck_cca_thres(hw, 0x40);
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_LOUD,
			 "switch case not process\n");
		break;
	}
	rtlphy->set_io_inprogress = false;
	RT_TRACE(rtlpriv, COMP_CMD, DBG_TRACE,
		 "(%#x)\n", rtlphy->current_io_type);
}

static void rtl92ee_phy_set_rf_on(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_write_byte(rtlpriv, REG_SPS0_CTRL, 0x2b);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE3);
	/*rtl_write_byte(rtlpriv, REG_APSD_CTRL, 0x00);*/
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE2);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE3);
	rtl_write_byte(rtlpriv, REG_TXPAUSE, 0x00);
}

static void _rtl92ee_phy_set_rf_sleep(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_write_byte(rtlpriv, REG_TXPAUSE, 0xFF);
	rtl_set_rfreg(hw, RF90_PATH_A, 0x00, RFREG_OFFSET_MASK, 0x00);

	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE2);
	rtl_write_byte(rtlpriv, REG_SPS0_CTRL, 0x22);
}

static bool _rtl92ee_phy_set_rf_power_state(struct ieee80211_hw *hw,
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
			u32 initializecount = 0;

			do {
				initializecount++;
				RT_TRACE(rtlpriv, COMP_RF, DBG_DMESG,
					 "IPS Set eRf nic enable\n");
				rtstatus = rtl_ps_enable_nic(hw);
			} while (!rtstatus && (initializecount < 10));
			RT_CLEAR_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC);
		} else {
			RT_TRACE(rtlpriv, COMP_RF, DBG_DMESG,
				 "Set ERFON sleeping:%d ms\n",
				  jiffies_to_msecs(jiffies -
						   ppsc->last_sleep_jiffies));
			ppsc->last_awake_jiffies = jiffies;
			rtl92ee_phy_set_rf_on(hw);
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
				RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
					 "eRf Off/Sleep: %d times TcbBusyQueue[%d] =%d before doze!\n",
					 (i + 1), queue_id,
					 skb_queue_len(&ring->queue));

				udelay(10);
				i++;
			}
			if (i >= MAX_DOZE_WAITING_TIMES_9x) {
				RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
					 "\n ERFSLEEP: %d times TcbBusyQueue[%d] = %d !\n",
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
					 "eRf Off/Sleep: %d times TcbBusyQueue[%d] =%d before doze!\n",
					 (i + 1), queue_id,
					 skb_queue_len(&ring->queue));
				udelay(10);
				i++;
			}
			if (i >= MAX_DOZE_WAITING_TIMES_9x) {
				RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
					 "\n ERFSLEEP: %d times TcbBusyQueue[%d] = %d !\n",
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
		_rtl92ee_phy_set_rf_sleep(hw);
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_LOUD,
			 "switch case not process\n");
		bresult = false;
		break;
	}
	if (bresult)
		ppsc->rfpwr_state = rfpwr_state;
	return bresult;
}

bool rtl92ee_phy_set_rf_power_state(struct ieee80211_hw *hw,
				    enum rf_pwrstate rfpwr_state)
{
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));

	bool bresult = false;

	if (rfpwr_state == ppsc->rfpwr_state)
		return bresult;
	bresult = _rtl92ee_phy_set_rf_power_state(hw, rfpwr_state);
	return bresult;
}
