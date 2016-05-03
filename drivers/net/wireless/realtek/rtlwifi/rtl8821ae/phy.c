/******************************************************************************
 *
 * Copyright(c) 2009-2010  Realtek Corporation.
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
#include "trx.h"
#include "../btcoexist/halbt_precomp.h"
#include "hw.h"
#include "../efuse.h"

#define READ_NEXT_PAIR(array_table, v1, v2, i) \
	do { \
		i += 2; \
		v1 = array_table[i]; \
		v2 = array_table[i+1]; \
	} while (0)

static u32 _rtl8821ae_phy_rf_serial_read(struct ieee80211_hw *hw,
					 enum radio_path rfpath, u32 offset);
static void _rtl8821ae_phy_rf_serial_write(struct ieee80211_hw *hw,
					   enum radio_path rfpath, u32 offset,
					   u32 data);
static u32 _rtl8821ae_phy_calculate_bit_shift(u32 bitmask);
static bool _rtl8821ae_phy_bb8821a_config_parafile(struct ieee80211_hw *hw);
/*static bool _rtl8812ae_phy_config_mac_with_headerfile(struct ieee80211_hw *hw);*/
static bool _rtl8821ae_phy_config_mac_with_headerfile(struct ieee80211_hw *hw);
static bool _rtl8821ae_phy_config_bb_with_headerfile(struct ieee80211_hw *hw,
						     u8 configtype);
static bool _rtl8821ae_phy_config_bb_with_pgheaderfile(struct ieee80211_hw *hw,
						       u8 configtype);
static void phy_init_bb_rf_register_definition(struct ieee80211_hw *hw);

static long _rtl8821ae_phy_txpwr_idx_to_dbm(struct ieee80211_hw *hw,
					    enum wireless_mode wirelessmode,
					    u8 txpwridx);
static void rtl8821ae_phy_set_rf_on(struct ieee80211_hw *hw);
static void rtl8821ae_phy_set_io(struct ieee80211_hw *hw);

static void rtl8812ae_fixspur(struct ieee80211_hw *hw,
			      enum ht_channel_width band_width, u8 channel)
{
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	/*C cut Item12 ADC FIFO CLOCK*/
	if (IS_VENDOR_8812A_C_CUT(rtlhal->version)) {
		if (band_width == HT_CHANNEL_WIDTH_20_40 && channel == 11)
			rtl_set_bbreg(hw, RRFMOD, 0xC00, 0x3);
			/* 0x8AC[11:10] = 2'b11*/
		else
			rtl_set_bbreg(hw, RRFMOD, 0xC00, 0x2);
			/* 0x8AC[11:10] = 2'b10*/

		/* <20120914, Kordan> A workarould to resolve
		 * 2480Mhz spur by setting ADC clock as 160M. (Asked by Binson)
		 */
		if (band_width == HT_CHANNEL_WIDTH_20 &&
		    (channel == 13 || channel == 14)) {
			rtl_set_bbreg(hw, RRFMOD, 0x300, 0x3);
			/*0x8AC[9:8] = 2'b11*/
			rtl_set_bbreg(hw, RADC_BUF_CLK, BIT(30), 1);
			/* 0x8C4[30] = 1*/
		} else if (band_width == HT_CHANNEL_WIDTH_20_40 &&
			   channel == 11) {
			rtl_set_bbreg(hw, RADC_BUF_CLK, BIT(30), 1);
			/*0x8C4[30] = 1*/
		} else if (band_width != HT_CHANNEL_WIDTH_80) {
			rtl_set_bbreg(hw, RRFMOD, 0x300, 0x2);
			/*0x8AC[9:8] = 2'b10*/
			rtl_set_bbreg(hw, RADC_BUF_CLK, BIT(30), 0);
			/*0x8C4[30] = 0*/
		}
	} else if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE) {
		/* <20120914, Kordan> A workarould to resolve
		 * 2480Mhz spur by setting ADC clock as 160M.
		 */
		if (band_width == HT_CHANNEL_WIDTH_20 &&
		    (channel == 13 || channel == 14))
			rtl_set_bbreg(hw, RRFMOD, 0x300, 0x3);
			/*0x8AC[9:8] = 11*/
		else if (channel  <= 14) /*2.4G only*/
			rtl_set_bbreg(hw, RRFMOD, 0x300, 0x2);
			/*0x8AC[9:8] = 10*/
	}
}

u32 rtl8821ae_phy_query_bb_reg(struct ieee80211_hw *hw, u32 regaddr,
			       u32 bitmask)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 returnvalue, originalvalue, bitshift;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), bitmask(%#x)\n",
		 regaddr, bitmask);
	originalvalue = rtl_read_dword(rtlpriv, regaddr);
	bitshift = _rtl8821ae_phy_calculate_bit_shift(bitmask);
	returnvalue = (originalvalue & bitmask) >> bitshift;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "BBR MASK=0x%x Addr[0x%x]=0x%x\n",
		 bitmask, regaddr, originalvalue);
	return returnvalue;
}

void rtl8821ae_phy_set_bb_reg(struct ieee80211_hw *hw,
			      u32 regaddr, u32 bitmask, u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 originalvalue, bitshift;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), bitmask(%#x), data(%#x)\n",
		 regaddr, bitmask, data);

	if (bitmask != MASKDWORD) {
		originalvalue = rtl_read_dword(rtlpriv, regaddr);
		bitshift = _rtl8821ae_phy_calculate_bit_shift(bitmask);
		data = ((originalvalue & (~bitmask)) |
			((data << bitshift) & bitmask));
	}

	rtl_write_dword(rtlpriv, regaddr, data);

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), bitmask(%#x), data(%#x)\n",
		 regaddr, bitmask, data);
}

u32 rtl8821ae_phy_query_rf_reg(struct ieee80211_hw *hw,
			       enum radio_path rfpath, u32 regaddr,
			       u32 bitmask)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 original_value, readback_value, bitshift;
	unsigned long flags;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), rfpath(%#x), bitmask(%#x)\n",
		 regaddr, rfpath, bitmask);

	spin_lock_irqsave(&rtlpriv->locks.rf_lock, flags);

	original_value = _rtl8821ae_phy_rf_serial_read(hw, rfpath, regaddr);
	bitshift = _rtl8821ae_phy_calculate_bit_shift(bitmask);
	readback_value = (original_value & bitmask) >> bitshift;

	spin_unlock_irqrestore(&rtlpriv->locks.rf_lock, flags);

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), rfpath(%#x), bitmask(%#x), original_value(%#x)\n",
		 regaddr, rfpath, bitmask, original_value);

	return readback_value;
}

void rtl8821ae_phy_set_rf_reg(struct ieee80211_hw *hw,
			   enum radio_path rfpath,
			   u32 regaddr, u32 bitmask, u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 original_value, bitshift;
	unsigned long flags;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), bitmask(%#x), data(%#x), rfpath(%#x)\n",
		  regaddr, bitmask, data, rfpath);

	spin_lock_irqsave(&rtlpriv->locks.rf_lock, flags);

	if (bitmask != RFREG_OFFSET_MASK) {
		original_value =
		   _rtl8821ae_phy_rf_serial_read(hw, rfpath, regaddr);
		bitshift = _rtl8821ae_phy_calculate_bit_shift(bitmask);
		data = ((original_value & (~bitmask)) | (data << bitshift));
	}

	_rtl8821ae_phy_rf_serial_write(hw, rfpath, regaddr, data);

	spin_unlock_irqrestore(&rtlpriv->locks.rf_lock, flags);

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), bitmask(%#x), data(%#x), rfpath(%#x)\n",
		 regaddr, bitmask, data, rfpath);
}

static u32 _rtl8821ae_phy_rf_serial_read(struct ieee80211_hw *hw,
					 enum radio_path rfpath, u32 offset)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	bool is_pi_mode = false;
	u32 retvalue = 0;

	/* 2009/06/17 MH We can not execute IO for power
	save or other accident mode.*/
	if (RT_CANNOT_IO(hw)) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "return all one\n");
		return 0xFFFFFFFF;
	}
	/* <20120809, Kordan> CCA OFF(when entering),
		asked by James to avoid reading the wrong value.
	    <20120828, Kordan> Toggling CCA would affect RF 0x0, skip it!*/
	if (offset != 0x0 &&
	    !((rtlhal->hw_type == HARDWARE_TYPE_RTL8821AE) ||
	    (IS_VENDOR_8812A_C_CUT(rtlhal->version))))
		rtl_set_bbreg(hw, RCCAONSEC, 0x8, 1);
	offset &= 0xff;

	if (rfpath == RF90_PATH_A)
		is_pi_mode = (bool)rtl_get_bbreg(hw, 0xC00, 0x4);
	else if (rfpath == RF90_PATH_B)
		is_pi_mode = (bool)rtl_get_bbreg(hw, 0xE00, 0x4);

	rtl_set_bbreg(hw, RHSSIREAD_8821AE, 0xff, offset);

	if ((rtlhal->hw_type == HARDWARE_TYPE_RTL8821AE) ||
	    (IS_VENDOR_8812A_C_CUT(rtlhal->version)))
		udelay(20);

	if (is_pi_mode) {
		if (rfpath == RF90_PATH_A)
			retvalue =
			  rtl_get_bbreg(hw, RA_PIREAD_8821A, BLSSIREADBACKDATA);
		else if (rfpath == RF90_PATH_B)
			retvalue =
			  rtl_get_bbreg(hw, RB_PIREAD_8821A, BLSSIREADBACKDATA);
	} else {
		if (rfpath == RF90_PATH_A)
			retvalue =
			  rtl_get_bbreg(hw, RA_SIREAD_8821A, BLSSIREADBACKDATA);
		else if (rfpath == RF90_PATH_B)
			retvalue =
			  rtl_get_bbreg(hw, RB_SIREAD_8821A, BLSSIREADBACKDATA);
	}

	/*<20120809, Kordan> CCA ON(when exiting),
	 * asked by James to avoid reading the wrong value.
	 *   <20120828, Kordan> Toggling CCA would affect RF 0x0, skip it!
	 */
	if (offset != 0x0 &&
	    !((rtlhal->hw_type == HARDWARE_TYPE_RTL8821AE) ||
	    (IS_VENDOR_8812A_C_CUT(rtlhal->version))))
		rtl_set_bbreg(hw, RCCAONSEC, 0x8, 0);
	return retvalue;
}

static void _rtl8821ae_phy_rf_serial_write(struct ieee80211_hw *hw,
					   enum radio_path rfpath, u32 offset,
					   u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct bb_reg_def *pphyreg = &rtlphy->phyreg_def[rfpath];
	u32 data_and_addr;
	u32 newoffset;

	if (RT_CANNOT_IO(hw)) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "stop\n");
		return;
	}
	offset &= 0xff;
	newoffset = offset;
	data_and_addr = ((newoffset << 20) |
			 (data & 0x000fffff)) & 0x0fffffff;
	rtl_set_bbreg(hw, pphyreg->rf3wire_offset, MASKDWORD, data_and_addr);
	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "RFW-%d Addr[0x%x]=0x%x\n",
		 rfpath, pphyreg->rf3wire_offset, data_and_addr);
}

static u32 _rtl8821ae_phy_calculate_bit_shift(u32 bitmask)
{
	u32 i;

	for (i = 0; i <= 31; i++) {
		if (((bitmask >> i) & 0x1) == 1)
			break;
	}
	return i;
}

bool rtl8821ae_phy_mac_config(struct ieee80211_hw *hw)
{
	bool rtstatus = 0;

	rtstatus = _rtl8821ae_phy_config_mac_with_headerfile(hw);

	return rtstatus;
}

bool rtl8821ae_phy_bb_config(struct ieee80211_hw *hw)
{
	bool rtstatus = true;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 regval;
	u8 crystal_cap;

	phy_init_bb_rf_register_definition(hw);

	regval = rtl_read_byte(rtlpriv, REG_SYS_FUNC_EN);
	regval |= FEN_PCIEA;
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, regval);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN,
		       regval | FEN_BB_GLB_RSTN | FEN_BBRSTB);

	rtl_write_byte(rtlpriv, REG_RF_CTRL, 0x7);
	rtl_write_byte(rtlpriv, REG_OPT_CTRL + 2, 0x7);

	rtstatus = _rtl8821ae_phy_bb8821a_config_parafile(hw);

	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE) {
		crystal_cap = rtlefuse->crystalcap & 0x3F;
		rtl_set_bbreg(hw, REG_MAC_PHY_CTRL, 0x7FF80000,
			      (crystal_cap | (crystal_cap << 6)));
	} else {
		crystal_cap = rtlefuse->crystalcap & 0x3F;
		rtl_set_bbreg(hw, REG_MAC_PHY_CTRL, 0xFFF000,
			      (crystal_cap | (crystal_cap << 6)));
	}
	rtlphy->reg_837 = rtl_read_byte(rtlpriv, 0x837);

	return rtstatus;
}

bool rtl8821ae_phy_rf_config(struct ieee80211_hw *hw)
{
	return rtl8821ae_phy_rf6052_config(hw);
}

u32 phy_get_tx_swing_8812A(struct ieee80211_hw *hw, u8	band,
			   u8 rf_path)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_dm *rtldm = rtl_dm(rtlpriv);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	char reg_swing_2g = -1;/* 0xff; */
	char reg_swing_5g = -1;/* 0xff; */
	char swing_2g = -1 * reg_swing_2g;
	char swing_5g = -1 * reg_swing_5g;
	u32  out = 0x200;
	const char auto_temp = -1;

	RT_TRACE(rtlpriv, COMP_SCAN, DBG_LOUD,
		 "===> PHY_GetTxBBSwing_8812A, bbSwing_2G: %d, bbSwing_5G: %d,autoload_failflag=%d.\n",
		 (int)swing_2g, (int)swing_5g,
		 (int)rtlefuse->autoload_failflag);

	if (rtlefuse->autoload_failflag) {
		if (band == BAND_ON_2_4G) {
			rtldm->swing_diff_2g = swing_2g;
			if (swing_2g == 0) {
				out = 0x200; /* 0 dB */
			} else if (swing_2g == -3) {
				out = 0x16A; /* -3 dB */
			} else if (swing_2g == -6) {
				out = 0x101; /* -6 dB */
			} else if (swing_2g == -9) {
				out = 0x0B6; /* -9 dB */
			} else {
				rtldm->swing_diff_2g = 0;
				out = 0x200;
			}
		} else if (band == BAND_ON_5G) {
			rtldm->swing_diff_5g = swing_5g;
			if (swing_5g == 0) {
				out = 0x200; /* 0 dB */
			} else if (swing_5g == -3) {
				out = 0x16A; /* -3 dB */
			} else if (swing_5g == -6) {
				out = 0x101; /* -6 dB */
			} else if (swing_5g == -9) {
				out = 0x0B6; /* -9 dB */
			} else {
				if (rtlhal->hw_type == HARDWARE_TYPE_RTL8821AE) {
					rtldm->swing_diff_5g = -3;
					out = 0x16A;
				} else {
					rtldm->swing_diff_5g = 0;
					out = 0x200;
				}
			}
		} else {
			rtldm->swing_diff_2g = -3;
			rtldm->swing_diff_5g = -3;
			out = 0x16A; /* -3 dB */
		}
	} else {
		u32 swing = 0, swing_a = 0, swing_b = 0;

		if (band == BAND_ON_2_4G) {
			if (reg_swing_2g == auto_temp) {
				efuse_shadow_read(hw, 1, 0xC6, (u32 *)&swing);
				swing = (swing == 0xFF) ? 0x00 : swing;
			} else if (swing_2g ==  0) {
				swing = 0x00; /* 0 dB */
			} else if (swing_2g == -3) {
				swing = 0x05; /* -3 dB */
			} else if (swing_2g == -6) {
				swing = 0x0A; /* -6 dB */
			} else if (swing_2g == -9) {
				swing = 0xFF; /* -9 dB */
			} else {
				swing = 0x00;
			}
		} else {
			if (reg_swing_5g == auto_temp) {
				efuse_shadow_read(hw, 1, 0xC7, (u32 *)&swing);
				swing = (swing == 0xFF) ? 0x00 : swing;
			} else if (swing_5g ==  0) {
				swing = 0x00; /* 0 dB */
			} else if (swing_5g == -3) {
				swing = 0x05; /* -3 dB */
			} else if (swing_5g == -6) {
				swing = 0x0A; /* -6 dB */
			} else if (swing_5g == -9) {
				swing = 0xFF; /* -9 dB */
			} else {
				swing = 0x00;
			}
		}

		swing_a = (swing & 0x3) >> 0; /* 0xC6/C7[1:0] */
		swing_b = (swing & 0xC) >> 2; /* 0xC6/C7[3:2] */
		RT_TRACE(rtlpriv, COMP_SCAN, DBG_LOUD,
			 "===> PHY_GetTxBBSwing_8812A, swingA: 0x%X, swingB: 0x%X\n",
			 swing_a, swing_b);

		/* 3 Path-A */
		if (swing_a == 0x0) {
			if (band == BAND_ON_2_4G)
				rtldm->swing_diff_2g = 0;
			else
				rtldm->swing_diff_5g = 0;
			out = 0x200; /* 0 dB */
		} else if (swing_a == 0x1) {
			if (band == BAND_ON_2_4G)
				rtldm->swing_diff_2g = -3;
			else
				rtldm->swing_diff_5g = -3;
			out = 0x16A; /* -3 dB */
		} else if (swing_a == 0x2) {
			if (band == BAND_ON_2_4G)
				rtldm->swing_diff_2g = -6;
			else
				rtldm->swing_diff_5g = -6;
			out = 0x101; /* -6 dB */
		} else if (swing_a == 0x3) {
			if (band == BAND_ON_2_4G)
				rtldm->swing_diff_2g = -9;
			else
				rtldm->swing_diff_5g = -9;
			out = 0x0B6; /* -9 dB */
		}
		/* 3 Path-B */
		if (swing_b == 0x0) {
			if (band == BAND_ON_2_4G)
				rtldm->swing_diff_2g = 0;
			else
				rtldm->swing_diff_5g = 0;
			out = 0x200; /* 0 dB */
		} else if (swing_b == 0x1) {
			if (band == BAND_ON_2_4G)
				rtldm->swing_diff_2g = -3;
			else
				rtldm->swing_diff_5g = -3;
			out = 0x16A; /* -3 dB */
		} else if (swing_b == 0x2) {
			if (band == BAND_ON_2_4G)
				rtldm->swing_diff_2g = -6;
			else
				rtldm->swing_diff_5g = -6;
			out = 0x101; /* -6 dB */
		} else if (swing_b == 0x3) {
			if (band == BAND_ON_2_4G)
				rtldm->swing_diff_2g = -9;
			else
				rtldm->swing_diff_5g = -9;
			out = 0x0B6; /* -9 dB */
		}
	}

	RT_TRACE(rtlpriv, COMP_SCAN, DBG_LOUD,
		 "<=== PHY_GetTxBBSwing_8812A, out = 0x%X\n", out);
	return out;
}

void rtl8821ae_phy_switch_wirelessband(struct ieee80211_hw *hw, u8 band)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_dm *rtldm = rtl_dm(rtlpriv);
	u8 current_band = rtlhal->current_bandtype;
	u32 txpath, rxpath;
	char bb_diff_between_band;

	txpath = rtl8821ae_phy_query_bb_reg(hw, RTXPATH, 0xf0);
	rxpath = rtl8821ae_phy_query_bb_reg(hw, RCCK_RX, 0x0f000000);
	rtlhal->current_bandtype = (enum band_type) band;
	/* reconfig BB/RF according to wireless mode */
	if (rtlhal->current_bandtype == BAND_ON_2_4G) {
		/* BB & RF Config */
		rtl_set_bbreg(hw, ROFDMCCKEN, BOFDMEN|BCCKEN, 0x03);

		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8821AE) {
			/* 0xCB0[15:12] = 0x7 (LNA_On)*/
			rtl_set_bbreg(hw, RA_RFE_PINMUX, 0xF000, 0x7);
			/* 0xCB0[7:4] = 0x7 (PAPE_A)*/
			rtl_set_bbreg(hw, RA_RFE_PINMUX, 0xF0, 0x7);
		}

		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE) {
			/*0x834[1:0] = 0x1*/
			rtl_set_bbreg(hw, 0x834, 0x3, 0x1);
		}

		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8821AE) {
			/* 0xC1C[11:8] = 0 */
			rtl_set_bbreg(hw, RA_TXSCALE, 0xF00, 0);
		} else {
			/* 0x82C[1:0] = 2b'00 */
			rtl_set_bbreg(hw, 0x82c, 0x3, 0);
		}
		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE) {
			rtl_set_bbreg(hw, RA_RFE_PINMUX, BMASKDWORD,
				      0x77777777);
			rtl_set_bbreg(hw, RB_RFE_PINMUX, BMASKDWORD,
				      0x77777777);
			rtl_set_bbreg(hw, RA_RFE_INV, 0x3ff00000, 0x000);
			rtl_set_bbreg(hw, RB_RFE_INV, 0x3ff00000, 0x000);
		}

		rtl_set_bbreg(hw, RTXPATH, 0xf0, 0x1);
		rtl_set_bbreg(hw, RCCK_RX, 0x0f000000, 0x1);

		rtl_write_byte(rtlpriv, REG_CCK_CHECK, 0x0);
	} else {/* 5G band */
		u16 count, reg_41a;

		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8821AE) {
			/*0xCB0[15:12] = 0x5 (LNA_On)*/
			rtl_set_bbreg(hw, RA_RFE_PINMUX, 0xF000, 0x5);
			/*0xCB0[7:4] = 0x4 (PAPE_A)*/
			rtl_set_bbreg(hw, RA_RFE_PINMUX, 0xF0, 0x4);
		}
		/*CCK_CHECK_en*/
		rtl_write_byte(rtlpriv, REG_CCK_CHECK, 0x80);

		count = 0;
		reg_41a = rtl_read_word(rtlpriv, REG_TXPKT_EMPTY);
		RT_TRACE(rtlpriv, COMP_SCAN, DBG_LOUD,
			 "Reg41A value %d", reg_41a);
		reg_41a &= 0x30;
		while ((reg_41a != 0x30) && (count < 50)) {
			udelay(50);
			RT_TRACE(rtlpriv, COMP_SCAN, DBG_LOUD, "Delay 50us\n");

			reg_41a = rtl_read_word(rtlpriv, REG_TXPKT_EMPTY);
			reg_41a &= 0x30;
			count++;
			RT_TRACE(rtlpriv, COMP_SCAN, DBG_LOUD,
				 "Reg41A value %d", reg_41a);
		}
		if (count != 0)
			RT_TRACE(rtlpriv, COMP_MLME, DBG_LOUD,
				 "PHY_SwitchWirelessBand8812(): Switch to 5G Band. Count = %d reg41A=0x%x\n",
				 count, reg_41a);

		/* 2012/02/01, Sinda add registry to switch workaround
		without long-run verification for scan issue. */
		rtl_set_bbreg(hw, ROFDMCCKEN, BOFDMEN|BCCKEN, 0x03);

		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE) {
			/*0x834[1:0] = 0x2*/
			rtl_set_bbreg(hw, 0x834, 0x3, 0x2);
		}

		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8821AE) {
			/* AGC table select */
			/* 0xC1C[11:8] = 1*/
			rtl_set_bbreg(hw, RA_TXSCALE, 0xF00, 1);
		} else
			/* 0x82C[1:0] = 2'b00 */
			rtl_set_bbreg(hw, 0x82c, 0x3, 1);

		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE) {
			rtl_set_bbreg(hw, RA_RFE_PINMUX, BMASKDWORD,
				      0x77337777);
			rtl_set_bbreg(hw, RB_RFE_PINMUX, BMASKDWORD,
				      0x77337777);
			rtl_set_bbreg(hw, RA_RFE_INV, 0x3ff00000, 0x010);
			rtl_set_bbreg(hw, RB_RFE_INV, 0x3ff00000, 0x010);
		}

		rtl_set_bbreg(hw, RTXPATH, 0xf0, 0);
		rtl_set_bbreg(hw, RCCK_RX, 0x0f000000, 0xf);

		RT_TRACE(rtlpriv, COMP_SCAN, DBG_LOUD,
			 "==>PHY_SwitchWirelessBand8812() BAND_ON_5G settings OFDM index 0x%x\n",
			 rtlpriv->dm.ofdm_index[RF90_PATH_A]);
	}

	if ((rtlhal->hw_type == HARDWARE_TYPE_RTL8821AE) ||
	    (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE)) {
		/* 0xC1C[31:21] */
		rtl_set_bbreg(hw, RA_TXSCALE, 0xFFE00000,
			      phy_get_tx_swing_8812A(hw, band, RF90_PATH_A));
		/* 0xE1C[31:21] */
		rtl_set_bbreg(hw, RB_TXSCALE, 0xFFE00000,
			      phy_get_tx_swing_8812A(hw, band, RF90_PATH_B));

		/* <20121005, Kordan> When TxPowerTrack is ON,
		 *	we should take care of the change of BB swing.
		 *   That is, reset all info to trigger Tx power tracking.
		 */
		if (band != current_band) {
			bb_diff_between_band =
				(rtldm->swing_diff_2g - rtldm->swing_diff_5g);
			bb_diff_between_band = (band == BAND_ON_2_4G) ?
						bb_diff_between_band :
						(-1 * bb_diff_between_band);
			rtldm->default_ofdm_index += bb_diff_between_band * 2;
		}
		rtl8821ae_dm_clear_txpower_tracking_state(hw);
	}

	RT_TRACE(rtlpriv, COMP_SCAN, DBG_TRACE,
		 "<==rtl8821ae_phy_switch_wirelessband():Switch Band OK.\n");
	return;
}

static bool _rtl8821ae_check_condition(struct ieee80211_hw *hw,
				       const u32 condition)
{
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u32 _board = rtlefuse->board_type; /*need efuse define*/
	u32 _interface = 0x01; /* ODM_ITRF_PCIE */
	u32 _platform = 0x08;/* ODM_WIN */
	u32 cond = condition;

	if (condition == 0xCDCDCDCD)
		return true;

	cond = condition & 0xFF;
	if ((_board != cond) && cond != 0xFF)
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

static void _rtl8821ae_config_rf_reg(struct ieee80211_hw *hw,
				     u32 addr, u32 data,
				     enum radio_path rfpath, u32 regaddr)
{
	if (addr == 0xfe || addr == 0xffe) {
		/* In order not to disturb BT music when
		 * wifi init.(1ant NIC only)
		 */
		mdelay(50);
	} else {
		rtl_set_rfreg(hw, rfpath, regaddr, RFREG_OFFSET_MASK, data);
		udelay(1);
	}
}

static void _rtl8821ae_config_rf_radio_a(struct ieee80211_hw *hw,
					 u32 addr, u32 data)
{
	u32 content = 0x1000; /*RF Content: radio_a_txt*/
	u32 maskforphyset = (u32)(content & 0xE000);

	_rtl8821ae_config_rf_reg(hw, addr, data,
				 RF90_PATH_A, addr | maskforphyset);
}

static void _rtl8821ae_config_rf_radio_b(struct ieee80211_hw *hw,
					 u32 addr, u32 data)
{
	u32 content = 0x1001; /*RF Content: radio_b_txt*/
	u32 maskforphyset = (u32)(content & 0xE000);

	_rtl8821ae_config_rf_reg(hw, addr, data,
				 RF90_PATH_B, addr | maskforphyset);
}

static void _rtl8821ae_config_bb_reg(struct ieee80211_hw *hw,
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
		rtl_set_bbreg(hw, addr, MASKDWORD, data);

	udelay(1);
}

static void _rtl8821ae_phy_init_tx_power_by_rate(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 band, rfpath, txnum, rate_section;

	for (band = BAND_ON_2_4G; band <= BAND_ON_5G; ++band)
		for (rfpath = 0; rfpath < TX_PWR_BY_RATE_NUM_RF; ++rfpath)
			for (txnum = 0; txnum < TX_PWR_BY_RATE_NUM_RF; ++txnum)
				for (rate_section = 0;
				     rate_section < TX_PWR_BY_RATE_NUM_SECTION;
				     ++rate_section)
					rtlphy->tx_power_by_rate_offset[band]
					    [rfpath][txnum][rate_section] = 0;
}

static void _rtl8821ae_phy_set_txpower_by_rate_base(struct ieee80211_hw *hw,
					  u8 band, u8 path,
					  u8 rate_section,
					  u8 txnum, u8 value)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;

	if (path > RF90_PATH_D) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			"Invalid Rf Path %d in phy_SetTxPowerByRatBase()\n", path);
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
		case VHT_1SSMCS0_1SSMCS9:
			rtlphy->txpwr_by_rate_base_24g[path][txnum][4] = value;
			break;
		case VHT_2SSMCS0_2SSMCS9:
			rtlphy->txpwr_by_rate_base_24g[path][txnum][5] = value;
			break;
		default:
			RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
				 "Invalid RateSection %d in Band 2.4G,Rf Path %d, %dTx in PHY_SetTxPowerByRateBase()\n",
				 rate_section, path, txnum);
			break;
		}
	} else if (band == BAND_ON_5G) {
		switch (rate_section) {
		case OFDM:
			rtlphy->txpwr_by_rate_base_5g[path][txnum][0] = value;
			break;
		case HT_MCS0_MCS7:
			rtlphy->txpwr_by_rate_base_5g[path][txnum][1] = value;
			break;
		case HT_MCS8_MCS15:
			rtlphy->txpwr_by_rate_base_5g[path][txnum][2] = value;
			break;
		case VHT_1SSMCS0_1SSMCS9:
			rtlphy->txpwr_by_rate_base_5g[path][txnum][3] = value;
			break;
		case VHT_2SSMCS0_2SSMCS9:
			rtlphy->txpwr_by_rate_base_5g[path][txnum][4] = value;
			break;
		default:
			RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
				"Invalid RateSection %d in Band 5G, Rf Path %d, %dTx in PHY_SetTxPowerByRateBase()\n",
				rate_section, path, txnum);
			break;
		}
	} else {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			"Invalid Band %d in PHY_SetTxPowerByRateBase()\n", band);
	}
}

static u8 _rtl8821ae_phy_get_txpower_by_rate_base(struct ieee80211_hw *hw,
						  u8 band, u8 path,
						  u8 txnum, u8 rate_section)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
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
		case VHT_1SSMCS0_1SSMCS9:
			value = rtlphy->txpwr_by_rate_base_24g[path][txnum][4];
			break;
		case VHT_2SSMCS0_2SSMCS9:
			value = rtlphy->txpwr_by_rate_base_24g[path][txnum][5];
			break;
		default:
			RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
				 "Invalid RateSection %d in Band 2.4G, Rf Path %d, %dTx in PHY_GetTxPowerByRateBase()\n",
				 rate_section, path, txnum);
			break;
		}
	} else if (band == BAND_ON_5G) {
		switch (rate_section) {
		case OFDM:
			value = rtlphy->txpwr_by_rate_base_5g[path][txnum][0];
			break;
		case HT_MCS0_MCS7:
			value = rtlphy->txpwr_by_rate_base_5g[path][txnum][1];
			break;
		case HT_MCS8_MCS15:
			value = rtlphy->txpwr_by_rate_base_5g[path][txnum][2];
			break;
		case VHT_1SSMCS0_1SSMCS9:
			value = rtlphy->txpwr_by_rate_base_5g[path][txnum][3];
			break;
		case VHT_2SSMCS0_2SSMCS9:
			value = rtlphy->txpwr_by_rate_base_5g[path][txnum][4];
			break;
		default:
			RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
				 "Invalid RateSection %d in Band 5G, Rf Path %d, %dTx in PHY_GetTxPowerByRateBase()\n",
				 rate_section, path, txnum);
			break;
		}
	} else {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Invalid Band %d in PHY_GetTxPowerByRateBase()\n", band);
	}

	return value;
}

static void _rtl8821ae_phy_store_txpower_by_rate_base(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u16 rawValue = 0;
	u8 base = 0, path = 0;

	for (path = RF90_PATH_A; path <= RF90_PATH_B; ++path) {
		rawValue = (u16)(rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][path][RF_1TX][0] >> 24) & 0xFF;
		base = (rawValue >> 4) * 10 + (rawValue & 0xF);
		_rtl8821ae_phy_set_txpower_by_rate_base(hw, BAND_ON_2_4G, path, CCK, RF_1TX, base);

		rawValue = (u16)(rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][path][RF_1TX][2] >> 24) & 0xFF;
		base = (rawValue >> 4) * 10 + (rawValue & 0xF);
		_rtl8821ae_phy_set_txpower_by_rate_base(hw, BAND_ON_2_4G, path, OFDM, RF_1TX, base);

		rawValue = (u16)(rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][path][RF_1TX][4] >> 24) & 0xFF;
		base = (rawValue >> 4) * 10 + (rawValue & 0xF);
		_rtl8821ae_phy_set_txpower_by_rate_base(hw, BAND_ON_2_4G, path, HT_MCS0_MCS7, RF_1TX, base);

		rawValue = (u16)(rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][path][RF_2TX][6] >> 24) & 0xFF;
		base = (rawValue >> 4) * 10 + (rawValue & 0xF);
		_rtl8821ae_phy_set_txpower_by_rate_base(hw, BAND_ON_2_4G, path, HT_MCS8_MCS15, RF_2TX, base);

		rawValue = (u16)(rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][path][RF_1TX][8] >> 24) & 0xFF;
		base = (rawValue >> 4) * 10 + (rawValue & 0xF);
		_rtl8821ae_phy_set_txpower_by_rate_base(hw, BAND_ON_2_4G, path, VHT_1SSMCS0_1SSMCS9, RF_1TX, base);

		rawValue = (u16)(rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][path][RF_2TX][11] >> 8) & 0xFF;
		base = (rawValue >> 4) * 10 + (rawValue & 0xF);
		_rtl8821ae_phy_set_txpower_by_rate_base(hw, BAND_ON_2_4G, path, VHT_2SSMCS0_2SSMCS9, RF_2TX, base);

		rawValue = (u16)(rtlphy->tx_power_by_rate_offset[BAND_ON_5G][path][RF_1TX][2] >> 24) & 0xFF;
		base = (rawValue >> 4) * 10 + (rawValue & 0xF);
		_rtl8821ae_phy_set_txpower_by_rate_base(hw, BAND_ON_5G, path, OFDM, RF_1TX, base);

		rawValue = (u16)(rtlphy->tx_power_by_rate_offset[BAND_ON_5G][path][RF_1TX][4] >> 24) & 0xFF;
		base = (rawValue >> 4) * 10 + (rawValue & 0xF);
		_rtl8821ae_phy_set_txpower_by_rate_base(hw, BAND_ON_5G, path, HT_MCS0_MCS7, RF_1TX, base);

		rawValue = (u16)(rtlphy->tx_power_by_rate_offset[BAND_ON_5G][path][RF_2TX][6] >> 24) & 0xFF;
		base = (rawValue >> 4) * 10 + (rawValue & 0xF);
		_rtl8821ae_phy_set_txpower_by_rate_base(hw, BAND_ON_5G, path, HT_MCS8_MCS15, RF_2TX, base);

		rawValue = (u16)(rtlphy->tx_power_by_rate_offset[BAND_ON_5G][path][RF_1TX][8] >> 24) & 0xFF;
		base = (rawValue >> 4) * 10 + (rawValue & 0xF);
		_rtl8821ae_phy_set_txpower_by_rate_base(hw, BAND_ON_5G, path, VHT_1SSMCS0_1SSMCS9, RF_1TX, base);

		rawValue = (u16)(rtlphy->tx_power_by_rate_offset[BAND_ON_5G][path][RF_2TX][11] >> 8) & 0xFF;
		base = (rawValue >> 4) * 10 + (rawValue & 0xF);
		_rtl8821ae_phy_set_txpower_by_rate_base(hw, BAND_ON_5G, path, VHT_2SSMCS0_2SSMCS9, RF_2TX, base);
	}
}

static void _phy_convert_txpower_dbm_to_relative_value(u32 *data, u8 start,
						u8 end, u8 base_val)
{
	int i;
	u8 temp_value = 0;
	u32 temp_data = 0;

	for (i = 3; i >= 0; --i) {
		if (i >= start && i <= end) {
			/* Get the exact value */
			temp_value = (u8)(*data >> (i * 8)) & 0xF;
			temp_value += ((u8)((*data >> (i * 8 + 4)) & 0xF)) * 10;

			/* Change the value to a relative value */
			temp_value = (temp_value > base_val) ? temp_value -
					base_val : base_val - temp_value;
		} else {
			temp_value = (u8)(*data >> (i * 8)) & 0xFF;
		}
		temp_data <<= 8;
		temp_data |= temp_value;
	}
	*data = temp_data;
}

static void _rtl8812ae_phy_cross_reference_ht_and_vht_txpower_limit(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 regulation, bw, channel, rate_section;
	char temp_pwrlmt = 0;

	for (regulation = 0; regulation < MAX_REGULATION_NUM; ++regulation) {
		for (bw = 0; bw < MAX_5G_BANDWITH_NUM; ++bw) {
			for (channel = 0; channel < CHANNEL_MAX_NUMBER_5G; ++channel) {
				for (rate_section = 0; rate_section < MAX_RATE_SECTION_NUM; ++rate_section) {
					temp_pwrlmt = rtlphy->txpwr_limit_5g[regulation]
						[bw][rate_section][channel][RF90_PATH_A];
					if (temp_pwrlmt == MAX_POWER_INDEX) {
						if (bw == 0 || bw == 1) { /*5G 20M 40M VHT and HT can cross reference*/
							RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
								"No power limit table of the specified band %d, bandwidth %d, ratesection %d, channel %d, rf path %d\n",
								1, bw, rate_section, channel, RF90_PATH_A);
							if (rate_section == 2) {
								rtlphy->txpwr_limit_5g[regulation][bw][2][channel][RF90_PATH_A] =
									rtlphy->txpwr_limit_5g[regulation][bw][4][channel][RF90_PATH_A];
							} else if (rate_section == 4) {
								rtlphy->txpwr_limit_5g[regulation][bw][4][channel][RF90_PATH_A] =
									rtlphy->txpwr_limit_5g[regulation][bw][2][channel][RF90_PATH_A];
							} else if (rate_section == 3) {
								rtlphy->txpwr_limit_5g[regulation][bw][3][channel][RF90_PATH_A] =
									rtlphy->txpwr_limit_5g[regulation][bw][5][channel][RF90_PATH_A];
							} else if (rate_section == 5) {
								rtlphy->txpwr_limit_5g[regulation][bw][5][channel][RF90_PATH_A] =
									rtlphy->txpwr_limit_5g[regulation][bw][3][channel][RF90_PATH_A];
							}

							RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE, "use other value %d", temp_pwrlmt);
						}
					}
				}
			}
		}
	}
}

static u8 _rtl8812ae_phy_get_txpower_by_rate_base_index(struct ieee80211_hw *hw,
						   enum band_type band, u8 rate)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 index = 0;
	if (band == BAND_ON_2_4G) {
		switch (rate) {
		case MGN_1M:
		case MGN_2M:
		case MGN_5_5M:
		case MGN_11M:
			index = 0;
			break;

		case MGN_6M:
		case MGN_9M:
		case MGN_12M:
		case MGN_18M:
		case MGN_24M:
		case MGN_36M:
		case MGN_48M:
		case MGN_54M:
			index = 1;
			break;

		case MGN_MCS0:
		case MGN_MCS1:
		case MGN_MCS2:
		case MGN_MCS3:
		case MGN_MCS4:
		case MGN_MCS5:
		case MGN_MCS6:
		case MGN_MCS7:
			index = 2;
			break;

		case MGN_MCS8:
		case MGN_MCS9:
		case MGN_MCS10:
		case MGN_MCS11:
		case MGN_MCS12:
		case MGN_MCS13:
		case MGN_MCS14:
		case MGN_MCS15:
			index = 3;
			break;

		default:
			RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
				"Wrong rate 0x%x to obtain index in 2.4G in PHY_GetTxPowerByRateBaseIndex()\n",
				rate);
			break;
		}
	} else if (band == BAND_ON_5G) {
		switch (rate) {
		case MGN_6M:
		case MGN_9M:
		case MGN_12M:
		case MGN_18M:
		case MGN_24M:
		case MGN_36M:
		case MGN_48M:
		case MGN_54M:
			index = 0;
			break;

		case MGN_MCS0:
		case MGN_MCS1:
		case MGN_MCS2:
		case MGN_MCS3:
		case MGN_MCS4:
		case MGN_MCS5:
		case MGN_MCS6:
		case MGN_MCS7:
			index = 1;
			break;

		case MGN_MCS8:
		case MGN_MCS9:
		case MGN_MCS10:
		case MGN_MCS11:
		case MGN_MCS12:
		case MGN_MCS13:
		case MGN_MCS14:
		case MGN_MCS15:
			index = 2;
			break;

		case MGN_VHT1SS_MCS0:
		case MGN_VHT1SS_MCS1:
		case MGN_VHT1SS_MCS2:
		case MGN_VHT1SS_MCS3:
		case MGN_VHT1SS_MCS4:
		case MGN_VHT1SS_MCS5:
		case MGN_VHT1SS_MCS6:
		case MGN_VHT1SS_MCS7:
		case MGN_VHT1SS_MCS8:
		case MGN_VHT1SS_MCS9:
			index = 3;
			break;

		case MGN_VHT2SS_MCS0:
		case MGN_VHT2SS_MCS1:
		case MGN_VHT2SS_MCS2:
		case MGN_VHT2SS_MCS3:
		case MGN_VHT2SS_MCS4:
		case MGN_VHT2SS_MCS5:
		case MGN_VHT2SS_MCS6:
		case MGN_VHT2SS_MCS7:
		case MGN_VHT2SS_MCS8:
		case MGN_VHT2SS_MCS9:
			index = 4;
			break;

		default:
			RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
				"Wrong rate 0x%x to obtain index in 5G in PHY_GetTxPowerByRateBaseIndex()\n",
				rate);
			break;
		}
	}

	return index;
}

static void _rtl8812ae_phy_convert_txpower_limit_to_power_index(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 bw40_pwr_base_dbm2_4G, bw40_pwr_base_dbm5G;
	u8 regulation, bw, channel, rate_section;
	u8 base_index2_4G = 0;
	u8 base_index5G = 0;
	char temp_value = 0, temp_pwrlmt = 0;
	u8 rf_path = 0;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
		"=====> _rtl8812ae_phy_convert_txpower_limit_to_power_index()\n");

	_rtl8812ae_phy_cross_reference_ht_and_vht_txpower_limit(hw);

	for (regulation = 0; regulation < MAX_REGULATION_NUM; ++regulation) {
		for (bw = 0; bw < MAX_2_4G_BANDWITH_NUM; ++bw) {
			for (channel = 0; channel < CHANNEL_MAX_NUMBER_2G; ++channel) {
				for (rate_section = 0; rate_section < MAX_RATE_SECTION_NUM; ++rate_section) {
					/* obtain the base dBm values in 2.4G band
					 CCK => 11M, OFDM => 54M, HT 1T => MCS7, HT 2T => MCS15*/
					if (rate_section == 0) { /*CCK*/
						base_index2_4G =
							_rtl8812ae_phy_get_txpower_by_rate_base_index(hw,
							BAND_ON_2_4G, MGN_11M);
					} else if (rate_section == 1) { /*OFDM*/
						base_index2_4G =
							_rtl8812ae_phy_get_txpower_by_rate_base_index(hw,
							BAND_ON_2_4G, MGN_54M);
					} else if (rate_section == 2) { /*HT IT*/
						base_index2_4G =
							_rtl8812ae_phy_get_txpower_by_rate_base_index(hw,
							BAND_ON_2_4G, MGN_MCS7);
					} else if (rate_section == 3) { /*HT 2T*/
						base_index2_4G =
							_rtl8812ae_phy_get_txpower_by_rate_base_index(hw,
							BAND_ON_2_4G, MGN_MCS15);
					}

					temp_pwrlmt = rtlphy->txpwr_limit_2_4g[regulation]
						[bw][rate_section][channel][RF90_PATH_A];

					for (rf_path = RF90_PATH_A;
						rf_path < MAX_RF_PATH_NUM;
						++rf_path) {
						if (rate_section == 3)
							bw40_pwr_base_dbm2_4G =
							rtlphy->txpwr_by_rate_base_24g[rf_path][RF_2TX][base_index2_4G];
						else
							bw40_pwr_base_dbm2_4G =
							rtlphy->txpwr_by_rate_base_24g[rf_path][RF_1TX][base_index2_4G];

						if (temp_pwrlmt != MAX_POWER_INDEX) {
							temp_value = temp_pwrlmt - bw40_pwr_base_dbm2_4G;
							rtlphy->txpwr_limit_2_4g[regulation]
								[bw][rate_section][channel][rf_path] =
								temp_value;
						}

						RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
							"TxPwrLimit_2_4G[regulation %d][bw %d][rateSection %d][channel %d] = %d\n(TxPwrLimit in dBm %d - BW40PwrLmt2_4G[channel %d][rfPath %d] %d)\n",
							regulation, bw, rate_section, channel,
							rtlphy->txpwr_limit_2_4g[regulation][bw]
							[rate_section][channel][rf_path], (temp_pwrlmt == 63)
							? 0 : temp_pwrlmt/2, channel, rf_path,
							bw40_pwr_base_dbm2_4G);
					}
				}
			}
		}
	}
	for (regulation = 0; regulation < MAX_REGULATION_NUM; ++regulation) {
		for (bw = 0; bw < MAX_5G_BANDWITH_NUM; ++bw) {
			for (channel = 0; channel < CHANNEL_MAX_NUMBER_5G; ++channel) {
				for (rate_section = 0; rate_section < MAX_RATE_SECTION_NUM; ++rate_section) {
					/* obtain the base dBm values in 5G band
					 OFDM => 54M, HT 1T => MCS7, HT 2T => MCS15,
					VHT => 1SSMCS7, VHT 2T => 2SSMCS7*/
					if (rate_section == 1) { /*OFDM*/
						base_index5G =
							_rtl8812ae_phy_get_txpower_by_rate_base_index(hw,
							BAND_ON_5G, MGN_54M);
					} else if (rate_section == 2) { /*HT 1T*/
						base_index5G =
							_rtl8812ae_phy_get_txpower_by_rate_base_index(hw,
							BAND_ON_5G, MGN_MCS7);
					} else if (rate_section == 3) { /*HT 2T*/
						base_index5G =
							_rtl8812ae_phy_get_txpower_by_rate_base_index(hw,
							BAND_ON_5G, MGN_MCS15);
					} else if (rate_section == 4) { /*VHT 1T*/
						base_index5G =
							_rtl8812ae_phy_get_txpower_by_rate_base_index(hw,
							BAND_ON_5G, MGN_VHT1SS_MCS7);
					} else if (rate_section == 5) { /*VHT 2T*/
						base_index5G =
							_rtl8812ae_phy_get_txpower_by_rate_base_index(hw,
							BAND_ON_5G, MGN_VHT2SS_MCS7);
					}

					temp_pwrlmt = rtlphy->txpwr_limit_5g[regulation]
						[bw][rate_section][channel]
						[RF90_PATH_A];

					for (rf_path = RF90_PATH_A;
					     rf_path < MAX_RF_PATH_NUM;
					     ++rf_path) {
						if (rate_section == 3 || rate_section == 5)
							bw40_pwr_base_dbm5G =
							rtlphy->txpwr_by_rate_base_5g[rf_path]
							[RF_2TX][base_index5G];
						else
							bw40_pwr_base_dbm5G =
							rtlphy->txpwr_by_rate_base_5g[rf_path]
							[RF_1TX][base_index5G];

						if (temp_pwrlmt != MAX_POWER_INDEX) {
							temp_value =
								temp_pwrlmt - bw40_pwr_base_dbm5G;
							rtlphy->txpwr_limit_5g[regulation]
								[bw][rate_section][channel]
								[rf_path] = temp_value;
						}

						RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
							"TxPwrLimit_5G[regulation %d][bw %d][rateSection %d][channel %d] =%d\n(TxPwrLimit in dBm %d - BW40PwrLmt5G[chnl group %d][rfPath %d] %d)\n",
							regulation, bw, rate_section,
							channel, rtlphy->txpwr_limit_5g[regulation]
							[bw][rate_section][channel][rf_path],
							temp_pwrlmt, channel, rf_path, bw40_pwr_base_dbm5G);
					}
				}
			}
		}
	}
	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
		 "<===== _rtl8812ae_phy_convert_txpower_limit_to_power_index()\n");
}

static void _rtl8821ae_phy_init_txpower_limit(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 i, j, k, l, m;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "=====> _rtl8821ae_phy_init_txpower_limit()!\n");

	for (i = 0; i < MAX_REGULATION_NUM; ++i) {
		for (j = 0; j < MAX_2_4G_BANDWITH_NUM; ++j)
			for (k = 0; k < MAX_RATE_SECTION_NUM; ++k)
				for (m = 0; m < CHANNEL_MAX_NUMBER_2G; ++m)
					for (l = 0; l < MAX_RF_PATH_NUM; ++l)
						rtlphy->txpwr_limit_2_4g
								[i][j][k][m][l]
							= MAX_POWER_INDEX;
	}
	for (i = 0; i < MAX_REGULATION_NUM; ++i) {
		for (j = 0; j < MAX_5G_BANDWITH_NUM; ++j)
			for (k = 0; k < MAX_RATE_SECTION_NUM; ++k)
				for (m = 0; m < CHANNEL_MAX_NUMBER_5G; ++m)
					for (l = 0; l < MAX_RF_PATH_NUM; ++l)
						rtlphy->txpwr_limit_5g
								[i][j][k][m][l]
							= MAX_POWER_INDEX;
	}

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "<===== _rtl8821ae_phy_init_txpower_limit()!\n");
}

static void _rtl8821ae_phy_convert_txpower_dbm_to_relative_value(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 base = 0, rfPath = 0;

	for (rfPath = RF90_PATH_A; rfPath <= RF90_PATH_B; ++rfPath) {
		base = _rtl8821ae_phy_get_txpower_by_rate_base(hw, BAND_ON_2_4G, rfPath, RF_1TX, CCK);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][rfPath][RF_1TX][0],
			0, 3, base);

		base = _rtl8821ae_phy_get_txpower_by_rate_base(hw, BAND_ON_2_4G, rfPath, RF_1TX, OFDM);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][rfPath][RF_1TX][1],
			0, 3, base);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][rfPath][RF_1TX][2],
			0, 3, base);

		base = _rtl8821ae_phy_get_txpower_by_rate_base(hw, BAND_ON_2_4G, rfPath, RF_1TX, HT_MCS0_MCS7);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][rfPath][RF_1TX][3],
			0, 3, base);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][rfPath][RF_1TX][4],
			0, 3, base);

		base = _rtl8821ae_phy_get_txpower_by_rate_base(hw, BAND_ON_2_4G, rfPath, RF_2TX, HT_MCS8_MCS15);

		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][rfPath][RF_2TX][5],
			0, 3, base);

		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][rfPath][RF_2TX][6],
			0, 3, base);

		base = _rtl8821ae_phy_get_txpower_by_rate_base(hw, BAND_ON_2_4G, rfPath, RF_1TX, VHT_1SSMCS0_1SSMCS9);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][rfPath][RF_1TX][7],
			0, 3, base);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][rfPath][RF_1TX][8],
			0, 3, base);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][rfPath][RF_1TX][9],
			0, 1, base);

		base = _rtl8821ae_phy_get_txpower_by_rate_base(hw, BAND_ON_2_4G, rfPath, RF_2TX, VHT_2SSMCS0_2SSMCS9);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][rfPath][RF_1TX][9],
			2, 3, base);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][rfPath][RF_2TX][10],
			0, 3, base);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_2_4G][rfPath][RF_2TX][11],
			0, 3, base);

		base = _rtl8821ae_phy_get_txpower_by_rate_base(hw, BAND_ON_5G, rfPath, RF_1TX, OFDM);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_5G][rfPath][RF_1TX][1],
			0, 3, base);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_5G][rfPath][RF_1TX][2],
			0, 3, base);

		base = _rtl8821ae_phy_get_txpower_by_rate_base(hw, BAND_ON_5G, rfPath, RF_1TX, HT_MCS0_MCS7);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_5G][rfPath][RF_1TX][3],
			0, 3, base);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_5G][rfPath][RF_1TX][4],
			0, 3, base);

		base = _rtl8821ae_phy_get_txpower_by_rate_base(hw, BAND_ON_5G, rfPath, RF_2TX, HT_MCS8_MCS15);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_5G][rfPath][RF_2TX][5],
			0, 3, base);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_5G][rfPath][RF_2TX][6],
			0, 3, base);

		base = _rtl8821ae_phy_get_txpower_by_rate_base(hw, BAND_ON_5G, rfPath, RF_1TX, VHT_1SSMCS0_1SSMCS9);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_5G][rfPath][RF_1TX][7],
			0, 3, base);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_5G][rfPath][RF_1TX][8],
			0, 3, base);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_5G][rfPath][RF_1TX][9],
			0, 1, base);

		base = _rtl8821ae_phy_get_txpower_by_rate_base(hw, BAND_ON_5G, rfPath, RF_2TX, VHT_2SSMCS0_2SSMCS9);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_5G][rfPath][RF_1TX][9],
			2, 3, base);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_5G][rfPath][RF_2TX][10],
			0, 3, base);
		_phy_convert_txpower_dbm_to_relative_value(
			&rtlphy->tx_power_by_rate_offset[BAND_ON_5G][rfPath][RF_2TX][11],
			0, 3, base);
	}

	RT_TRACE(rtlpriv, COMP_POWER, DBG_TRACE,
		"<===_rtl8821ae_phy_convert_txpower_dbm_to_relative_value()\n");
}

static void _rtl8821ae_phy_txpower_by_rate_configuration(struct ieee80211_hw *hw)
{
	_rtl8821ae_phy_store_txpower_by_rate_base(hw);
	_rtl8821ae_phy_convert_txpower_dbm_to_relative_value(hw);
}

/* string is in decimal */
static bool _rtl8812ae_get_integer_from_string(char *str, u8 *pint)
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

static bool _rtl8812ae_eq_n_byte(u8 *str1, u8 *str2, u32 num)
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

static char _rtl8812ae_phy_get_chnl_idx_of_txpwr_lmt(struct ieee80211_hw *hw,
					      u8 band, u8 channel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	char channel_index = -1;
	u8  i = 0;

	if (band == BAND_ON_2_4G)
		channel_index = channel - 1;
	else if (band == BAND_ON_5G) {
		for (i = 0; i < sizeof(channel5g)/sizeof(u8); ++i) {
			if (channel5g[i] == channel)
				channel_index = i;
		}
	} else
		RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD, "Invalid Band %d in %s",
			 band,  __func__);

	if (channel_index == -1)
		RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
			 "Invalid Channel %d of Band %d in %s", channel,
			 band, __func__);

	return channel_index;
}

static void _rtl8812ae_phy_set_txpower_limit(struct ieee80211_hw *hw, u8 *pregulation,
				      u8 *pband, u8 *pbandwidth,
				      u8 *prate_section, u8 *prf_path,
				      u8 *pchannel, u8 *ppower_limit)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 regulation = 0, bandwidth = 0, rate_section = 0, channel;
	u8 channel_index;
	char power_limit = 0, prev_power_limit, ret;

	if (!_rtl8812ae_get_integer_from_string((char *)pchannel, &channel) ||
	    !_rtl8812ae_get_integer_from_string((char *)ppower_limit,
						&power_limit)) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "Illegal index of pwr_lmt table [chnl %d][val %d]\n",
			  channel, power_limit);
	}

	power_limit = power_limit > MAX_POWER_INDEX ?
		      MAX_POWER_INDEX : power_limit;

	if (_rtl8812ae_eq_n_byte(pregulation, (u8 *)("FCC"), 3))
		regulation = 0;
	else if (_rtl8812ae_eq_n_byte(pregulation, (u8 *)("MKK"), 3))
		regulation = 1;
	else if (_rtl8812ae_eq_n_byte(pregulation, (u8 *)("ETSI"), 4))
		regulation = 2;
	else if (_rtl8812ae_eq_n_byte(pregulation, (u8 *)("WW13"), 4))
		regulation = 3;

	if (_rtl8812ae_eq_n_byte(prate_section, (u8 *)("CCK"), 3))
		rate_section = 0;
	else if (_rtl8812ae_eq_n_byte(prate_section, (u8 *)("OFDM"), 4))
		rate_section = 1;
	else if (_rtl8812ae_eq_n_byte(prate_section, (u8 *)("HT"), 2) &&
		 _rtl8812ae_eq_n_byte(prf_path, (u8 *)("1T"), 2))
		rate_section = 2;
	else if (_rtl8812ae_eq_n_byte(prate_section, (u8 *)("HT"), 2) &&
		 _rtl8812ae_eq_n_byte(prf_path, (u8 *)("2T"), 2))
		rate_section = 3;
	else if (_rtl8812ae_eq_n_byte(prate_section, (u8 *)("VHT"), 3) &&
		 _rtl8812ae_eq_n_byte(prf_path, (u8 *)("1T"), 2))
		rate_section = 4;
	else if (_rtl8812ae_eq_n_byte(prate_section, (u8 *)("VHT"), 3) &&
		 _rtl8812ae_eq_n_byte(prf_path, (u8 *)("2T"), 2))
		rate_section = 5;

	if (_rtl8812ae_eq_n_byte(pbandwidth, (u8 *)("20M"), 3))
		bandwidth = 0;
	else if (_rtl8812ae_eq_n_byte(pbandwidth, (u8 *)("40M"), 3))
		bandwidth = 1;
	else if (_rtl8812ae_eq_n_byte(pbandwidth, (u8 *)("80M"), 3))
		bandwidth = 2;
	else if (_rtl8812ae_eq_n_byte(pbandwidth, (u8 *)("160M"), 4))
		bandwidth = 3;

	if (_rtl8812ae_eq_n_byte(pband, (u8 *)("2.4G"), 4)) {
		ret = _rtl8812ae_phy_get_chnl_idx_of_txpwr_lmt(hw,
							       BAND_ON_2_4G,
							       channel);

		if (ret == -1)
			return;

		channel_index = ret;

		prev_power_limit = rtlphy->txpwr_limit_2_4g[regulation]
						[bandwidth][rate_section]
						[channel_index][RF90_PATH_A];

		if (power_limit < prev_power_limit)
			rtlphy->txpwr_limit_2_4g[regulation][bandwidth]
				[rate_section][channel_index][RF90_PATH_A] =
								   power_limit;

		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "2.4G [regula %d][bw %d][sec %d][chnl %d][val %d]\n",
			  regulation, bandwidth, rate_section, channel_index,
			  rtlphy->txpwr_limit_2_4g[regulation][bandwidth]
				[rate_section][channel_index][RF90_PATH_A]);
	} else if (_rtl8812ae_eq_n_byte(pband, (u8 *)("5G"), 2)) {
		ret = _rtl8812ae_phy_get_chnl_idx_of_txpwr_lmt(hw,
							       BAND_ON_5G,
							       channel);

		if (ret == -1)
			return;

		channel_index = ret;

		prev_power_limit = rtlphy->txpwr_limit_5g[regulation][bandwidth]
						[rate_section][channel_index]
						[RF90_PATH_A];

		if (power_limit < prev_power_limit)
			rtlphy->txpwr_limit_5g[regulation][bandwidth]
			[rate_section][channel_index][RF90_PATH_A] = power_limit;

		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "5G: [regul %d][bw %d][sec %d][chnl %d][val %d]\n",
			  regulation, bandwidth, rate_section, channel,
			  rtlphy->txpwr_limit_5g[regulation][bandwidth]
				[rate_section][channel_index][RF90_PATH_A]);
	} else {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "Cannot recognize the band info in %s\n", pband);
		return;
	}
}

static void _rtl8812ae_phy_config_bb_txpwr_lmt(struct ieee80211_hw *hw,
					  u8 *regulation, u8 *band,
					  u8 *bandwidth, u8 *rate_section,
					  u8 *rf_path, u8 *channel,
					  u8 *power_limit)
{
	_rtl8812ae_phy_set_txpower_limit(hw, regulation, band, bandwidth,
					 rate_section, rf_path, channel,
					 power_limit);
}

static void _rtl8821ae_phy_read_and_config_txpwr_lmt(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	u32 i = 0;
	u32 array_len;
	u8 **array;

	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE) {
		array_len = RTL8812AE_TXPWR_LMT_ARRAY_LEN;
		array = RTL8812AE_TXPWR_LMT;
	} else {
		array_len = RTL8821AE_TXPWR_LMT_ARRAY_LEN;
		array = RTL8821AE_TXPWR_LMT;
	}

	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
		 "\n");

	for (i = 0; i < array_len; i += 7) {
		u8 *regulation = array[i];
		u8 *band = array[i+1];
		u8 *bandwidth = array[i+2];
		u8 *rate = array[i+3];
		u8 *rf_path = array[i+4];
		u8 *chnl = array[i+5];
		u8 *val = array[i+6];

		_rtl8812ae_phy_config_bb_txpwr_lmt(hw, regulation, band,
						   bandwidth, rate, rf_path,
						   chnl, val);
	}
}

static bool _rtl8821ae_phy_bb8821a_config_parafile(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	bool rtstatus;

	_rtl8821ae_phy_init_txpower_limit(hw);

	/* RegEnableTxPowerLimit == 1 for 8812a & 8821a */
	if (rtlefuse->eeprom_regulatory != 2)
		_rtl8821ae_phy_read_and_config_txpwr_lmt(hw);

	rtstatus = _rtl8821ae_phy_config_bb_with_headerfile(hw,
						       BASEBAND_CONFIG_PHY_REG);
	if (rtstatus != true) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "Write BB Reg Fail!!");
		return false;
	}
	_rtl8821ae_phy_init_tx_power_by_rate(hw);
	if (rtlefuse->autoload_failflag == false) {
		rtstatus = _rtl8821ae_phy_config_bb_with_pgheaderfile(hw,
						    BASEBAND_CONFIG_PHY_REG);
	}
	if (rtstatus != true) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "BB_PG Reg Fail!!");
		return false;
	}

	_rtl8821ae_phy_txpower_by_rate_configuration(hw);

	/* RegEnableTxPowerLimit == 1 for 8812a & 8821a */
	if (rtlefuse->eeprom_regulatory != 2)
		_rtl8812ae_phy_convert_txpower_limit_to_power_index(hw);

	rtstatus = _rtl8821ae_phy_config_bb_with_headerfile(hw,
						BASEBAND_CONFIG_AGC_TAB);

	if (rtstatus != true) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "AGC Table Fail\n");
		return false;
	}
	rtlphy->cck_high_power = (bool)(rtl_get_bbreg(hw,
			RFPGA0_XA_HSSIPARAMETER2, 0x200));
	return true;
}

static bool _rtl8821ae_phy_config_mac_with_headerfile(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	u32 i, v1, v2;
	u32 arraylength;
	u32 *ptrarray;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE, "Read MAC_REG_Array\n");
	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8821AE) {
		arraylength = RTL8821AEMAC_1T_ARRAYLEN;
		ptrarray = RTL8821AE_MAC_REG_ARRAY;
	} else {
		arraylength = RTL8812AEMAC_1T_ARRAYLEN;
		ptrarray = RTL8812AE_MAC_REG_ARRAY;
	}
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "Img: MAC_REG_ARRAY LEN %d\n", arraylength);
	for (i = 0; i < arraylength; i += 2) {
		v1 = ptrarray[i];
		v2 = (u8)ptrarray[i + 1];
		if (v1 < 0xCDCDCDCD) {
			rtl_write_byte(rtlpriv, v1, (u8)v2);
			continue;
		} else {
			if (!_rtl8821ae_check_condition(hw, v1)) {
				/*Discard the following (offset, data) pairs*/
				READ_NEXT_PAIR(ptrarray, v1, v2, i);
				while (v2 != 0xDEAD &&
				       v2 != 0xCDEF &&
				       v2 != 0xCDCD && i < arraylength - 2) {
					READ_NEXT_PAIR(ptrarray, v1, v2, i);
				}
				i -= 2; /* prevent from for-loop += 2*/
			} else {/*Configure matched pairs and skip to end of if-else.*/
				READ_NEXT_PAIR(ptrarray, v1, v2, i);
				while (v2 != 0xDEAD &&
				       v2 != 0xCDEF &&
				       v2 != 0xCDCD && i < arraylength - 2) {
					rtl_write_byte(rtlpriv, v1, v2);
					READ_NEXT_PAIR(ptrarray, v1, v2, i);
				}

				while (v2 != 0xDEAD && i < arraylength - 2)
					READ_NEXT_PAIR(ptrarray, v1, v2, i);
			}
		}
	}
	return true;
}

static bool _rtl8821ae_phy_config_bb_with_headerfile(struct ieee80211_hw *hw,
						     u8 configtype)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	int i;
	u32 *array_table;
	u16 arraylen;
	u32 v1 = 0, v2 = 0;

	if (configtype == BASEBAND_CONFIG_PHY_REG) {
		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE) {
			arraylen = RTL8812AEPHY_REG_1TARRAYLEN;
			array_table = RTL8812AE_PHY_REG_ARRAY;
		} else {
			arraylen = RTL8821AEPHY_REG_1TARRAYLEN;
			array_table = RTL8821AE_PHY_REG_ARRAY;
		}

		for (i = 0; i < arraylen; i += 2) {
			v1 = array_table[i];
			v2 = array_table[i + 1];
			if (v1 < 0xCDCDCDCD) {
				_rtl8821ae_config_bb_reg(hw, v1, v2);
				continue;
			} else {/*This line is the start line of branch.*/
				if (!_rtl8821ae_check_condition(hw, v1)) {
					/*Discard the following (offset, data) pairs*/
					READ_NEXT_PAIR(array_table, v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD &&
					       i < arraylen - 2) {
						READ_NEXT_PAIR(array_table, v1,
								v2, i);
					}

					i -= 2; /* prevent from for-loop += 2*/
				} else {/*Configure matched pairs and skip to end of if-else.*/
					READ_NEXT_PAIR(array_table, v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD &&
					       i < arraylen - 2) {
						_rtl8821ae_config_bb_reg(hw, v1,
									 v2);
						READ_NEXT_PAIR(array_table, v1,
							       v2, i);
					}

					while (v2 != 0xDEAD &&
					       i < arraylen - 2) {
						READ_NEXT_PAIR(array_table, v1,
							       v2, i);
					}
				}
			}
		}
	} else if (configtype == BASEBAND_CONFIG_AGC_TAB) {
		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE) {
			arraylen = RTL8812AEAGCTAB_1TARRAYLEN;
			array_table = RTL8812AE_AGC_TAB_ARRAY;
		} else {
			arraylen = RTL8821AEAGCTAB_1TARRAYLEN;
			array_table = RTL8821AE_AGC_TAB_ARRAY;
		}

		for (i = 0; i < arraylen; i = i + 2) {
			v1 = array_table[i];
			v2 = array_table[i+1];
			if (v1 < 0xCDCDCDCD) {
				rtl_set_bbreg(hw, v1, MASKDWORD, v2);
				udelay(1);
				continue;
			} else {/*This line is the start line of branch.*/
				if (!_rtl8821ae_check_condition(hw, v1)) {
					/*Discard the following (offset, data) pairs*/
					READ_NEXT_PAIR(array_table, v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD &&
					       i < arraylen - 2) {
						READ_NEXT_PAIR(array_table, v1,
								v2, i);
					}
					i -= 2; /* prevent from for-loop += 2*/
				} else {/*Configure matched pairs and skip to end of if-else.*/
					READ_NEXT_PAIR(array_table, v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD &&
					       i < arraylen - 2) {
						rtl_set_bbreg(hw, v1, MASKDWORD,
							      v2);
						udelay(1);
						READ_NEXT_PAIR(array_table, v1,
							       v2, i);
					}

					while (v2 != 0xDEAD &&
						i < arraylen - 2) {
						READ_NEXT_PAIR(array_table, v1,
								v2, i);
					}
				}
				RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
					 "The agctab_array_table[0] is %x Rtl818EEPHY_REGArray[1] is %x\n",
					  array_table[i],  array_table[i + 1]);
			}
		}
	}
	return true;
}

static u8 _rtl8821ae_get_rate_section_index(u32 regaddr)
{
	u8 index = 0;
	regaddr &= 0xFFF;
	if (regaddr >= 0xC20 && regaddr <= 0xC4C)
		index = (u8)((regaddr - 0xC20) / 4);
	else if (regaddr >= 0xE20 && regaddr <= 0xE4C)
		index = (u8)((regaddr - 0xE20) / 4);
	else
		RT_ASSERT(!COMP_INIT,
			  "Invalid RegAddr 0x%x\n", regaddr);
	return index;
}

static void _rtl8821ae_store_tx_power_by_rate(struct ieee80211_hw *hw,
					      u32 band, u32 rfpath,
					      u32 txnum, u32 regaddr,
					      u32 bitmask, u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 rate_section = _rtl8821ae_get_rate_section_index(regaddr);

	if (band != BAND_ON_2_4G && band != BAND_ON_5G) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_WARNING, "Invalid Band %d\n", band);
		band = BAND_ON_2_4G;
	}
	if (rfpath >= MAX_RF_PATH) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_WARNING, "Invalid RfPath %d\n", rfpath);
		rfpath = MAX_RF_PATH - 1;
	}
	if (txnum >= MAX_RF_PATH) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_WARNING, "Invalid TxNum %d\n", txnum);
		txnum = MAX_RF_PATH - 1;
	}
	rtlphy->tx_power_by_rate_offset[band][rfpath][txnum][rate_section] = data;
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "TxPwrByRateOffset[Band %d][RfPath %d][TxNum %d][RateSection %d] = 0x%x\n",
		 band, rfpath, txnum, rate_section,
		 rtlphy->tx_power_by_rate_offset[band][rfpath][txnum][rate_section]);
}

static bool _rtl8821ae_phy_config_bb_with_pgheaderfile(struct ieee80211_hw *hw,
							u8 configtype)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	int i;
	u32 *array;
	u16 arraylen;
	u32 v1, v2, v3, v4, v5, v6;

	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE) {
		arraylen = RTL8812AEPHY_REG_ARRAY_PGLEN;
		array = RTL8812AE_PHY_REG_ARRAY_PG;
	} else {
		arraylen = RTL8821AEPHY_REG_ARRAY_PGLEN;
		array = RTL8821AE_PHY_REG_ARRAY_PG;
	}

	if (configtype != BASEBAND_CONFIG_PHY_REG) {
		RT_TRACE(rtlpriv, COMP_SEND, DBG_TRACE,
			 "configtype != BaseBand_Config_PHY_REG\n");
		return true;
	}
	for (i = 0; i < arraylen; i += 6) {
		v1 = array[i];
		v2 = array[i+1];
		v3 = array[i+2];
		v4 = array[i+3];
		v5 = array[i+4];
		v6 = array[i+5];

		if (v1 < 0xCDCDCDCD) {
			if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE &&
				(v4 == 0xfe || v4 == 0xffe)) {
				msleep(50);
				continue;
			}

			if (rtlhal->hw_type == HARDWARE_TYPE_RTL8821AE) {
				if (v4 == 0xfe)
					msleep(50);
				else if (v4 == 0xfd)
					mdelay(5);
				else if (v4 == 0xfc)
					mdelay(1);
				else if (v4 == 0xfb)
					udelay(50);
				else if (v4 == 0xfa)
					udelay(5);
				else if (v4 == 0xf9)
					udelay(1);
			}
			_rtl8821ae_store_tx_power_by_rate(hw, v1, v2, v3,
							  v4, v5, v6);
			continue;
		} else {
			 /*don't need the hw_body*/
			if (!_rtl8821ae_check_condition(hw, v1)) {
				i += 2; /* skip the pair of expression*/
				v1 = array[i];
				v2 = array[i+1];
				v3 = array[i+2];
				while (v2 != 0xDEAD) {
					i += 3;
					v1 = array[i];
					v2 = array[i+1];
					v3 = array[i+2];
				}
			}
		}
	}

	return true;
}

bool rtl8812ae_phy_config_rf_with_headerfile(struct ieee80211_hw *hw,
					     enum radio_path rfpath)
{
	int i;
	bool rtstatus = true;
	u32 *radioa_array_table_a, *radioa_array_table_b;
	u16 radioa_arraylen_a, radioa_arraylen_b;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 v1 = 0, v2 = 0;

	radioa_arraylen_a = RTL8812AE_RADIOA_1TARRAYLEN;
	radioa_array_table_a = RTL8812AE_RADIOA_ARRAY;
	radioa_arraylen_b = RTL8812AE_RADIOB_1TARRAYLEN;
	radioa_array_table_b = RTL8812AE_RADIOB_ARRAY;
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "Radio_A:RTL8821AE_RADIOA_ARRAY %d\n", radioa_arraylen_a);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "Radio No %x\n", rfpath);
	rtstatus = true;
	switch (rfpath) {
	case RF90_PATH_A:
		for (i = 0; i < radioa_arraylen_a; i = i + 2) {
			v1 = radioa_array_table_a[i];
			v2 = radioa_array_table_a[i+1];
			if (v1 < 0xcdcdcdcd) {
				_rtl8821ae_config_rf_radio_a(hw, v1, v2);
				continue;
			} else{/*This line is the start line of branch.*/
				if (!_rtl8821ae_check_condition(hw, v1)) {
					/*Discard the following (offset, data) pairs*/
					READ_NEXT_PAIR(radioa_array_table_a, v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD && i < radioa_arraylen_a-2)
						READ_NEXT_PAIR(radioa_array_table_a, v1, v2, i);

					i -= 2; /* prevent from for-loop += 2*/
				} else {/*Configure matched pairs and skip to end of if-else.*/
					READ_NEXT_PAIR(radioa_array_table_a, v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD && i < radioa_arraylen_a - 2) {
						_rtl8821ae_config_rf_radio_a(hw, v1, v2);
						READ_NEXT_PAIR(radioa_array_table_a, v1, v2, i);
					}

					while (v2 != 0xDEAD && i < radioa_arraylen_a-2)
						READ_NEXT_PAIR(radioa_array_table_a, v1, v2, i);

				}
			}
		}
		break;
	case RF90_PATH_B:
		for (i = 0; i < radioa_arraylen_b; i = i + 2) {
			v1 = radioa_array_table_b[i];
			v2 = radioa_array_table_b[i+1];
			if (v1 < 0xcdcdcdcd) {
				_rtl8821ae_config_rf_radio_b(hw, v1, v2);
				continue;
			} else{/*This line is the start line of branch.*/
				if (!_rtl8821ae_check_condition(hw, v1)) {
					/*Discard the following (offset, data) pairs*/
					READ_NEXT_PAIR(radioa_array_table_b, v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD && i < radioa_arraylen_b-2)
						READ_NEXT_PAIR(radioa_array_table_b, v1, v2, i);

					i -= 2; /* prevent from for-loop += 2*/
				} else {/*Configure matched pairs and skip to end of if-else.*/
					READ_NEXT_PAIR(radioa_array_table_b, v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD && i < radioa_arraylen_b-2) {
						_rtl8821ae_config_rf_radio_b(hw, v1, v2);
						READ_NEXT_PAIR(radioa_array_table_b, v1, v2, i);
					}

					while (v2 != 0xDEAD && i < radioa_arraylen_b-2)
						READ_NEXT_PAIR(radioa_array_table_b, v1, v2, i);
				}
			}
		}
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

bool rtl8821ae_phy_config_rf_with_headerfile(struct ieee80211_hw *hw,
						enum radio_path rfpath)
{
	#define READ_NEXT_RF_PAIR(v1, v2, i) \
	do { \
		i += 2; \
		v1 = radioa_array_table[i]; \
		v2 = radioa_array_table[i+1]; \
	} \
	while (0)

	int i;
	bool rtstatus = true;
	u32 *radioa_array_table;
	u16 radioa_arraylen;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	/* struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw)); */
	u32 v1 = 0, v2 = 0;

	radioa_arraylen = RTL8821AE_RADIOA_1TARRAYLEN;
	radioa_array_table = RTL8821AE_RADIOA_ARRAY;
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "Radio_A:RTL8821AE_RADIOA_ARRAY %d\n", radioa_arraylen);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "Radio No %x\n", rfpath);
	rtstatus = true;
	switch (rfpath) {
	case RF90_PATH_A:
		for (i = 0; i < radioa_arraylen; i = i + 2) {
			v1 = radioa_array_table[i];
			v2 = radioa_array_table[i+1];
			if (v1 < 0xcdcdcdcd)
				_rtl8821ae_config_rf_radio_a(hw, v1, v2);
			else{/*This line is the start line of branch.*/
				if (!_rtl8821ae_check_condition(hw, v1)) {
					/*Discard the following (offset, data) pairs*/
					READ_NEXT_RF_PAIR(v1, v2, i);
					while (v2 != 0xDEAD &&
						v2 != 0xCDEF &&
						v2 != 0xCDCD && i < radioa_arraylen - 2)
						READ_NEXT_RF_PAIR(v1, v2, i);

					i -= 2; /* prevent from for-loop += 2*/
				} else {/*Configure matched pairs and skip to end of if-else.*/
					READ_NEXT_RF_PAIR(v1, v2, i);
					while (v2 != 0xDEAD &&
					       v2 != 0xCDEF &&
					       v2 != 0xCDCD && i < radioa_arraylen - 2) {
						_rtl8821ae_config_rf_radio_a(hw, v1, v2);
						READ_NEXT_RF_PAIR(v1, v2, i);
					}

					while (v2 != 0xDEAD && i < radioa_arraylen - 2)
						READ_NEXT_RF_PAIR(v1, v2, i);
				}
			}
		}
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

void rtl8821ae_phy_get_hw_reg_originalvalue(struct ieee80211_hw *hw)
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

static void phy_init_bb_rf_register_definition(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;

	rtlphy->phyreg_def[RF90_PATH_A].rfintfs = RFPGA0_XAB_RFINTERFACESW;
	rtlphy->phyreg_def[RF90_PATH_B].rfintfs = RFPGA0_XAB_RFINTERFACESW;

	rtlphy->phyreg_def[RF90_PATH_A].rfintfo = RFPGA0_XA_RFINTERFACEOE;
	rtlphy->phyreg_def[RF90_PATH_B].rfintfo = RFPGA0_XB_RFINTERFACEOE;

	rtlphy->phyreg_def[RF90_PATH_A].rfintfe = RFPGA0_XA_RFINTERFACEOE;
	rtlphy->phyreg_def[RF90_PATH_B].rfintfe = RFPGA0_XB_RFINTERFACEOE;

	rtlphy->phyreg_def[RF90_PATH_A].rf3wire_offset = RA_LSSIWRITE_8821A;
	rtlphy->phyreg_def[RF90_PATH_B].rf3wire_offset = RB_LSSIWRITE_8821A;

	rtlphy->phyreg_def[RF90_PATH_A].rfhssi_para2 = RHSSIREAD_8821AE;
	rtlphy->phyreg_def[RF90_PATH_B].rfhssi_para2 = RHSSIREAD_8821AE;

	rtlphy->phyreg_def[RF90_PATH_A].rf_rb = RA_SIREAD_8821A;
	rtlphy->phyreg_def[RF90_PATH_B].rf_rb = RB_SIREAD_8821A;

	rtlphy->phyreg_def[RF90_PATH_A].rf_rbpi = RA_PIREAD_8821A;
	rtlphy->phyreg_def[RF90_PATH_B].rf_rbpi = RB_PIREAD_8821A;
}

void rtl8821ae_phy_get_txpower_level(struct ieee80211_hw *hw, long *powerlevel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 txpwr_level;
	long txpwr_dbm;

	txpwr_level = rtlphy->cur_cck_txpwridx;
	txpwr_dbm = _rtl8821ae_phy_txpwr_idx_to_dbm(hw,
						 WIRELESS_MODE_B, txpwr_level);
	txpwr_level = rtlphy->cur_ofdm24g_txpwridx;
	if (_rtl8821ae_phy_txpwr_idx_to_dbm(hw,
					 WIRELESS_MODE_G,
					 txpwr_level) > txpwr_dbm)
		txpwr_dbm =
		    _rtl8821ae_phy_txpwr_idx_to_dbm(hw, WIRELESS_MODE_G,
						 txpwr_level);
	txpwr_level = rtlphy->cur_ofdm24g_txpwridx;
	if (_rtl8821ae_phy_txpwr_idx_to_dbm(hw,
					 WIRELESS_MODE_N_24G,
					 txpwr_level) > txpwr_dbm)
		txpwr_dbm =
		    _rtl8821ae_phy_txpwr_idx_to_dbm(hw, WIRELESS_MODE_N_24G,
						 txpwr_level);
	*powerlevel = txpwr_dbm;
}

static bool _rtl8821ae_phy_get_chnl_index(u8 channel, u8 *chnl_index)
{
	u8 i = 0;
	bool in_24g = true;

	if (channel <= 14) {
		in_24g = true;
		*chnl_index = channel - 1;
	} else {
		in_24g = false;

		for (i = 0; i < CHANNEL_MAX_NUMBER_5G; ++i) {
			if (channel5g[i] == channel) {
				*chnl_index = i;
				return in_24g;
			}
		}
	}
	return in_24g;
}

static char _rtl8821ae_phy_get_ratesection_intxpower_byrate(u8 path, u8 rate)
{
	char rate_section = 0;
	switch (rate) {
	case DESC_RATE1M:
	case DESC_RATE2M:
	case DESC_RATE5_5M:
	case DESC_RATE11M:
		rate_section = 0;
		break;
	case DESC_RATE6M:
	case DESC_RATE9M:
	case DESC_RATE12M:
	case DESC_RATE18M:
		rate_section = 1;
		break;
	case DESC_RATE24M:
	case DESC_RATE36M:
	case DESC_RATE48M:
	case DESC_RATE54M:
		rate_section = 2;
		break;
	case DESC_RATEMCS0:
	case DESC_RATEMCS1:
	case DESC_RATEMCS2:
	case DESC_RATEMCS3:
		rate_section = 3;
		break;
	case DESC_RATEMCS4:
	case DESC_RATEMCS5:
	case DESC_RATEMCS6:
	case DESC_RATEMCS7:
		rate_section = 4;
		break;
	case DESC_RATEMCS8:
	case DESC_RATEMCS9:
	case DESC_RATEMCS10:
	case DESC_RATEMCS11:
		rate_section = 5;
		break;
	case DESC_RATEMCS12:
	case DESC_RATEMCS13:
	case DESC_RATEMCS14:
	case DESC_RATEMCS15:
		rate_section = 6;
		break;
	case DESC_RATEVHT1SS_MCS0:
	case DESC_RATEVHT1SS_MCS1:
	case DESC_RATEVHT1SS_MCS2:
	case DESC_RATEVHT1SS_MCS3:
		rate_section = 7;
		break;
	case DESC_RATEVHT1SS_MCS4:
	case DESC_RATEVHT1SS_MCS5:
	case DESC_RATEVHT1SS_MCS6:
	case DESC_RATEVHT1SS_MCS7:
		rate_section = 8;
		break;
	case DESC_RATEVHT1SS_MCS8:
	case DESC_RATEVHT1SS_MCS9:
	case DESC_RATEVHT2SS_MCS0:
	case DESC_RATEVHT2SS_MCS1:
		rate_section = 9;
		break;
	case DESC_RATEVHT2SS_MCS2:
	case DESC_RATEVHT2SS_MCS3:
	case DESC_RATEVHT2SS_MCS4:
	case DESC_RATEVHT2SS_MCS5:
		rate_section = 10;
		break;
	case DESC_RATEVHT2SS_MCS6:
	case DESC_RATEVHT2SS_MCS7:
	case DESC_RATEVHT2SS_MCS8:
	case DESC_RATEVHT2SS_MCS9:
		rate_section = 11;
		break;
	default:
		RT_ASSERT(true, "Rate_Section is Illegal\n");
		break;
	}

	return rate_section;
}

static char _rtl8812ae_phy_get_world_wide_limit(char  *limit_table)
{
	char min = limit_table[0];
	u8 i = 0;

	for (i = 0; i < MAX_REGULATION_NUM; ++i) {
		if (limit_table[i] < min)
			min = limit_table[i];
	}
	return min;
}

static char _rtl8812ae_phy_get_txpower_limit(struct ieee80211_hw *hw,
					     u8 band,
					     enum ht_channel_width bandwidth,
					     enum radio_path rf_path,
					     u8 rate, u8 channel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtlpriv);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	short band_temp = -1, regulation = -1, bandwidth_temp = -1,
		 rate_section = -1, channel_temp = -1;
	u16 bd, regu, bdwidth, sec, chnl;
	char power_limit = MAX_POWER_INDEX;

	if (rtlefuse->eeprom_regulatory == 2)
		return MAX_POWER_INDEX;

	regulation = TXPWR_LMT_WW;

	if (band == BAND_ON_2_4G)
		band_temp = 0;
	else if (band == BAND_ON_5G)
		band_temp = 1;

	if (bandwidth == HT_CHANNEL_WIDTH_20)
		bandwidth_temp = 0;
	else if (bandwidth == HT_CHANNEL_WIDTH_20_40)
		bandwidth_temp = 1;
	else if (bandwidth == HT_CHANNEL_WIDTH_80)
		bandwidth_temp = 2;

	switch (rate) {
	case DESC_RATE1M:
	case DESC_RATE2M:
	case DESC_RATE5_5M:
	case DESC_RATE11M:
		rate_section = 0;
		break;
	case DESC_RATE6M:
	case DESC_RATE9M:
	case DESC_RATE12M:
	case DESC_RATE18M:
	case DESC_RATE24M:
	case DESC_RATE36M:
	case DESC_RATE48M:
	case DESC_RATE54M:
		rate_section = 1;
		break;
	case DESC_RATEMCS0:
	case DESC_RATEMCS1:
	case DESC_RATEMCS2:
	case DESC_RATEMCS3:
	case DESC_RATEMCS4:
	case DESC_RATEMCS5:
	case DESC_RATEMCS6:
	case DESC_RATEMCS7:
		rate_section = 2;
		break;
	case DESC_RATEMCS8:
	case DESC_RATEMCS9:
	case DESC_RATEMCS10:
	case DESC_RATEMCS11:
	case DESC_RATEMCS12:
	case DESC_RATEMCS13:
	case DESC_RATEMCS14:
	case DESC_RATEMCS15:
		rate_section = 3;
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
		rate_section = 4;
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
		rate_section = 5;
		break;
	default:
		RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
			"Wrong rate 0x%x\n", rate);
		break;
	}

	if (band_temp == BAND_ON_5G  && rate_section == 0)
		RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
			 "Wrong rate 0x%x: No CCK in 5G Band\n", rate);

	/*workaround for wrong index combination to obtain tx power limit,
	  OFDM only exists in BW 20M*/
	if (rate_section == 1)
		bandwidth_temp = 0;

	/*workaround for wrong index combination to obtain tx power limit,
	 *HT on 80M will reference to HT on 40M
	 */
	if ((rate_section == 2 || rate_section == 3) && band == BAND_ON_5G &&
	    bandwidth_temp == 2)
		bandwidth_temp = 1;

	if (band == BAND_ON_2_4G)
		channel_temp = _rtl8812ae_phy_get_chnl_idx_of_txpwr_lmt(hw,
		BAND_ON_2_4G, channel);
	else if (band == BAND_ON_5G)
		channel_temp = _rtl8812ae_phy_get_chnl_idx_of_txpwr_lmt(hw,
		BAND_ON_5G, channel);
	else if (band == BAND_ON_BOTH)
		;/* BAND_ON_BOTH don't care temporarily */

	if (band_temp == -1 || regulation == -1 || bandwidth_temp == -1 ||
		rate_section == -1 || channel_temp == -1) {
		RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
			 "Wrong index value to access power limit table [band %d][regulation %d][bandwidth %d][rf_path %d][rate_section %d][chnl %d]\n",
			 band_temp, regulation, bandwidth_temp, rf_path,
			 rate_section, channel_temp);
		return MAX_POWER_INDEX;
	}

	bd = band_temp;
	regu = regulation;
	bdwidth = bandwidth_temp;
	sec = rate_section;
	chnl = channel_temp;

	if (band == BAND_ON_2_4G) {
		char limits[10] = {0};
		u8 i;

		for (i = 0; i < 4; ++i)
			limits[i] = rtlphy->txpwr_limit_2_4g[i][bdwidth]
			[sec][chnl][rf_path];

		power_limit = (regulation == TXPWR_LMT_WW) ?
			_rtl8812ae_phy_get_world_wide_limit(limits) :
			rtlphy->txpwr_limit_2_4g[regu][bdwidth]
					[sec][chnl][rf_path];
	} else if (band == BAND_ON_5G) {
		char limits[10] = {0};
		u8 i;

		for (i = 0; i < MAX_REGULATION_NUM; ++i)
			limits[i] = rtlphy->txpwr_limit_5g[i][bdwidth]
			[sec][chnl][rf_path];

		power_limit = (regulation == TXPWR_LMT_WW) ?
			_rtl8812ae_phy_get_world_wide_limit(limits) :
			rtlphy->txpwr_limit_5g[regu][chnl]
			[sec][chnl][rf_path];
	} else {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "No power limit table of the specified band\n");
	}
	return power_limit;
}

static char _rtl8821ae_phy_get_txpower_by_rate(struct ieee80211_hw *hw,
					u8 band, u8 path, u8 rate)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 shift = 0, rate_section, tx_num;
	char tx_pwr_diff = 0;
	char limit = 0;

	rate_section = _rtl8821ae_phy_get_ratesection_intxpower_byrate(path, rate);
	tx_num = RF_TX_NUM_NONIMPLEMENT;

	if (tx_num == RF_TX_NUM_NONIMPLEMENT) {
		if ((rate >= DESC_RATEMCS8 && rate <= DESC_RATEMCS15) ||
			(rate >= DESC_RATEVHT2SS_MCS2 && rate <= DESC_RATEVHT2SS_MCS9))
			tx_num = RF_2TX;
		else
			tx_num = RF_1TX;
	}

	switch (rate) {
	case DESC_RATE1M:
	case DESC_RATE6M:
	case DESC_RATE24M:
	case DESC_RATEMCS0:
	case DESC_RATEMCS4:
	case DESC_RATEMCS8:
	case DESC_RATEMCS12:
	case DESC_RATEVHT1SS_MCS0:
	case DESC_RATEVHT1SS_MCS4:
	case DESC_RATEVHT1SS_MCS8:
	case DESC_RATEVHT2SS_MCS2:
	case DESC_RATEVHT2SS_MCS6:
		shift = 0;
		break;
	case DESC_RATE2M:
	case DESC_RATE9M:
	case DESC_RATE36M:
	case DESC_RATEMCS1:
	case DESC_RATEMCS5:
	case DESC_RATEMCS9:
	case DESC_RATEMCS13:
	case DESC_RATEVHT1SS_MCS1:
	case DESC_RATEVHT1SS_MCS5:
	case DESC_RATEVHT1SS_MCS9:
	case DESC_RATEVHT2SS_MCS3:
	case DESC_RATEVHT2SS_MCS7:
		shift = 8;
		break;
	case DESC_RATE5_5M:
	case DESC_RATE12M:
	case DESC_RATE48M:
	case DESC_RATEMCS2:
	case DESC_RATEMCS6:
	case DESC_RATEMCS10:
	case DESC_RATEMCS14:
	case DESC_RATEVHT1SS_MCS2:
	case DESC_RATEVHT1SS_MCS6:
	case DESC_RATEVHT2SS_MCS0:
	case DESC_RATEVHT2SS_MCS4:
	case DESC_RATEVHT2SS_MCS8:
		shift = 16;
		break;
	case DESC_RATE11M:
	case DESC_RATE18M:
	case DESC_RATE54M:
	case DESC_RATEMCS3:
	case DESC_RATEMCS7:
	case DESC_RATEMCS11:
	case DESC_RATEMCS15:
	case DESC_RATEVHT1SS_MCS3:
	case DESC_RATEVHT1SS_MCS7:
	case DESC_RATEVHT2SS_MCS1:
	case DESC_RATEVHT2SS_MCS5:
	case DESC_RATEVHT2SS_MCS9:
		shift = 24;
		break;
	default:
		RT_ASSERT(true, "Rate_Section is Illegal\n");
		break;
	}

	tx_pwr_diff = (u8)(rtlphy->tx_power_by_rate_offset[band][path]
		[tx_num][rate_section] >> shift) & 0xff;

	/* RegEnableTxPowerLimit == 1 for 8812a & 8821a */
	if (rtlpriv->efuse.eeprom_regulatory != 2) {
		limit = _rtl8812ae_phy_get_txpower_limit(hw, band,
			rtlphy->current_chan_bw, path, rate,
			rtlphy->current_channel);

		if (rate == DESC_RATEVHT1SS_MCS8 || rate == DESC_RATEVHT1SS_MCS9  ||
			 rate == DESC_RATEVHT2SS_MCS8 || rate == DESC_RATEVHT2SS_MCS9) {
			if (limit < 0) {
				if (tx_pwr_diff < (-limit))
					tx_pwr_diff = -limit;
			}
		} else {
			if (limit < 0)
				tx_pwr_diff = limit;
			else
				tx_pwr_diff = tx_pwr_diff > limit ? limit : tx_pwr_diff;
		}
		RT_TRACE(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
			"Maximum power by rate %d, final power by rate %d\n",
			limit, tx_pwr_diff);
	}

	return	tx_pwr_diff;
}

static u8 _rtl8821ae_get_txpower_index(struct ieee80211_hw *hw, u8 path,
					u8 rate, u8 bandwidth, u8 channel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u8 index = (channel - 1);
	u8 txpower = 0;
	bool in_24g = false;
	char powerdiff_byrate = 0;

	if (((rtlhal->current_bandtype == BAND_ON_2_4G) &&
	    (channel > 14 || channel < 1)) ||
	    ((rtlhal->current_bandtype == BAND_ON_5G) && (channel <= 14))) {
		index = 0;
		RT_TRACE(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
			"Illegal channel!!\n");
	}

	in_24g = _rtl8821ae_phy_get_chnl_index(channel, &index);
	if (in_24g) {
		if (RTL8821AE_RX_HAL_IS_CCK_RATE(rate))
			txpower = rtlefuse->txpwrlevel_cck[path][index];
		else if (DESC_RATE6M <= rate)
			txpower = rtlefuse->txpwrlevel_ht40_1s[path][index];
		else
			RT_TRACE(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD, "invalid rate\n");

		if (DESC_RATE6M <= rate && rate <= DESC_RATE54M &&
		    !RTL8821AE_RX_HAL_IS_CCK_RATE(rate))
			txpower += rtlefuse->txpwr_legacyhtdiff[path][TX_1S];

		if (bandwidth == HT_CHANNEL_WIDTH_20) {
			if ((DESC_RATEMCS0 <= rate && rate <= DESC_RATEMCS15) ||
				(DESC_RATEVHT1SS_MCS0 <= rate && rate <= DESC_RATEVHT2SS_MCS9))
				txpower += rtlefuse->txpwr_ht20diff[path][TX_1S];
			if ((DESC_RATEMCS8 <= rate && rate <= DESC_RATEMCS15) ||
				(DESC_RATEVHT2SS_MCS0 <= rate && rate <= DESC_RATEVHT2SS_MCS9))
				txpower += rtlefuse->txpwr_ht20diff[path][TX_2S];
		} else if (bandwidth == HT_CHANNEL_WIDTH_20_40) {
			if ((DESC_RATEMCS0 <= rate && rate <= DESC_RATEMCS15) ||
				(DESC_RATEVHT1SS_MCS0 <= rate && rate <= DESC_RATEVHT2SS_MCS9))
				txpower += rtlefuse->txpwr_ht40diff[path][TX_1S];
			if ((DESC_RATEMCS8 <= rate && rate <= DESC_RATEMCS15) ||
				(DESC_RATEVHT2SS_MCS0 <= rate && rate <= DESC_RATEVHT2SS_MCS9))
				txpower += rtlefuse->txpwr_ht40diff[path][TX_2S];
		} else if (bandwidth == HT_CHANNEL_WIDTH_80) {
			if ((DESC_RATEMCS0 <= rate && rate <= DESC_RATEMCS15) ||
			    (DESC_RATEVHT1SS_MCS0 <= rate &&
			     rate <= DESC_RATEVHT2SS_MCS9))
				txpower += rtlefuse->txpwr_ht40diff[path][TX_1S];
			if ((DESC_RATEMCS8 <= rate && rate <= DESC_RATEMCS15) ||
			    (DESC_RATEVHT2SS_MCS0 <= rate &&
			     rate <= DESC_RATEVHT2SS_MCS9))
				txpower += rtlefuse->txpwr_ht40diff[path][TX_2S];
		}
	} else {
		if (DESC_RATE6M <= rate)
			txpower = rtlefuse->txpwr_5g_bw40base[path][index];
		else
			RT_TRACE(rtlpriv, COMP_POWER_TRACKING, DBG_WARNING,
				 "INVALID Rate.\n");

		if (DESC_RATE6M <= rate && rate <= DESC_RATE54M &&
		    !RTL8821AE_RX_HAL_IS_CCK_RATE(rate))
			txpower += rtlefuse->txpwr_5g_ofdmdiff[path][TX_1S];

		if (bandwidth == HT_CHANNEL_WIDTH_20) {
			if ((DESC_RATEMCS0 <= rate && rate <= DESC_RATEMCS15) ||
			    (DESC_RATEVHT1SS_MCS0 <= rate &&
			     rate <= DESC_RATEVHT2SS_MCS9))
				txpower += rtlefuse->txpwr_5g_bw20diff[path][TX_1S];
			if ((DESC_RATEMCS8 <= rate && rate <= DESC_RATEMCS15) ||
			    (DESC_RATEVHT2SS_MCS0 <= rate &&
			     rate <= DESC_RATEVHT2SS_MCS9))
				txpower += rtlefuse->txpwr_5g_bw20diff[path][TX_2S];
		} else if (bandwidth == HT_CHANNEL_WIDTH_20_40) {
			if ((DESC_RATEMCS0 <= rate && rate <= DESC_RATEMCS15) ||
			    (DESC_RATEVHT1SS_MCS0 <= rate &&
			     rate <= DESC_RATEVHT2SS_MCS9))
				txpower += rtlefuse->txpwr_5g_bw40diff[path][TX_1S];
			if ((DESC_RATEMCS8 <= rate && rate <= DESC_RATEMCS15) ||
			    (DESC_RATEVHT2SS_MCS0 <= rate &&
			     rate <= DESC_RATEVHT2SS_MCS9))
				txpower += rtlefuse->txpwr_5g_bw40diff[path][TX_2S];
		} else if (bandwidth == HT_CHANNEL_WIDTH_80) {
			u8 i;

			for (i = 0; i < sizeof(channel5g_80m) / sizeof(u8); ++i)
				if (channel5g_80m[i] == channel)
					index = i;

			if ((DESC_RATEMCS0 <= rate && rate <= DESC_RATEMCS15) ||
			    (DESC_RATEVHT1SS_MCS0 <= rate &&
			     rate <= DESC_RATEVHT2SS_MCS9))
				txpower = rtlefuse->txpwr_5g_bw80base[path][index]
					+ rtlefuse->txpwr_5g_bw80diff[path][TX_1S];
			if ((DESC_RATEMCS8 <= rate && rate <= DESC_RATEMCS15) ||
			    (DESC_RATEVHT2SS_MCS0 <= rate &&
			     rate <= DESC_RATEVHT2SS_MCS9))
				txpower = rtlefuse->txpwr_5g_bw80base[path][index]
					+ rtlefuse->txpwr_5g_bw80diff[path][TX_1S]
					+ rtlefuse->txpwr_5g_bw80diff[path][TX_2S];
		    }
	}
	if (rtlefuse->eeprom_regulatory != 2)
		powerdiff_byrate =
		  _rtl8821ae_phy_get_txpower_by_rate(hw, (u8)(!in_24g),
						     path, rate);

	if (rate == DESC_RATEVHT1SS_MCS8 || rate == DESC_RATEVHT1SS_MCS9 ||
	    rate == DESC_RATEVHT2SS_MCS8 || rate == DESC_RATEVHT2SS_MCS9)
		txpower -= powerdiff_byrate;
	else
		txpower += powerdiff_byrate;

	if (rate > DESC_RATE11M)
		txpower += rtlpriv->dm.remnant_ofdm_swing_idx[path];
	else
		txpower += rtlpriv->dm.remnant_cck_idx;

	if (txpower > MAX_POWER_INDEX)
		txpower = MAX_POWER_INDEX;

	return txpower;
}

static void _rtl8821ae_phy_set_txpower_index(struct ieee80211_hw *hw,
					     u8 power_index, u8 path, u8 rate)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (path == RF90_PATH_A) {
		switch (rate) {
		case DESC_RATE1M:
			rtl_set_bbreg(hw, RTXAGC_A_CCK11_CCK1,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATE2M:
			rtl_set_bbreg(hw, RTXAGC_A_CCK11_CCK1,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATE5_5M:
			rtl_set_bbreg(hw, RTXAGC_A_CCK11_CCK1,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATE11M:
			rtl_set_bbreg(hw, RTXAGC_A_CCK11_CCK1,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATE6M:
			rtl_set_bbreg(hw, RTXAGC_A_OFDM18_OFDM6,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATE9M:
			rtl_set_bbreg(hw, RTXAGC_A_OFDM18_OFDM6,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATE12M:
			rtl_set_bbreg(hw, RTXAGC_A_OFDM18_OFDM6,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATE18M:
			rtl_set_bbreg(hw, RTXAGC_A_OFDM18_OFDM6,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATE24M:
			rtl_set_bbreg(hw, RTXAGC_A_OFDM54_OFDM24,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATE36M:
			rtl_set_bbreg(hw, RTXAGC_A_OFDM54_OFDM24,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATE48M:
			rtl_set_bbreg(hw, RTXAGC_A_OFDM54_OFDM24,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATE54M:
			rtl_set_bbreg(hw, RTXAGC_A_OFDM54_OFDM24,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATEMCS0:
			rtl_set_bbreg(hw, RTXAGC_A_MCS03_MCS00,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATEMCS1:
			rtl_set_bbreg(hw, RTXAGC_A_MCS03_MCS00,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATEMCS2:
			rtl_set_bbreg(hw, RTXAGC_A_MCS03_MCS00,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATEMCS3:
			rtl_set_bbreg(hw, RTXAGC_A_MCS03_MCS00,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATEMCS4:
			rtl_set_bbreg(hw, RTXAGC_A_MCS07_MCS04,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATEMCS5:
			rtl_set_bbreg(hw, RTXAGC_A_MCS07_MCS04,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATEMCS6:
			rtl_set_bbreg(hw, RTXAGC_A_MCS07_MCS04,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATEMCS7:
			rtl_set_bbreg(hw, RTXAGC_A_MCS07_MCS04,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATEMCS8:
			rtl_set_bbreg(hw, RTXAGC_A_MCS11_MCS08,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATEMCS9:
			rtl_set_bbreg(hw, RTXAGC_A_MCS11_MCS08,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATEMCS10:
			rtl_set_bbreg(hw, RTXAGC_A_MCS11_MCS08,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATEMCS11:
			rtl_set_bbreg(hw, RTXAGC_A_MCS11_MCS08,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATEMCS12:
			rtl_set_bbreg(hw, RTXAGC_A_MCS15_MCS12,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATEMCS13:
			rtl_set_bbreg(hw, RTXAGC_A_MCS15_MCS12,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATEMCS14:
			rtl_set_bbreg(hw, RTXAGC_A_MCS15_MCS12,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATEMCS15:
			rtl_set_bbreg(hw, RTXAGC_A_MCS15_MCS12,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATEVHT1SS_MCS0:
			rtl_set_bbreg(hw, RTXAGC_A_NSS1INDEX3_NSS1INDEX0,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATEVHT1SS_MCS1:
			rtl_set_bbreg(hw, RTXAGC_A_NSS1INDEX3_NSS1INDEX0,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATEVHT1SS_MCS2:
			rtl_set_bbreg(hw, RTXAGC_A_NSS1INDEX3_NSS1INDEX0,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATEVHT1SS_MCS3:
			rtl_set_bbreg(hw, RTXAGC_A_NSS1INDEX3_NSS1INDEX0,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATEVHT1SS_MCS4:
			rtl_set_bbreg(hw, RTXAGC_A_NSS1INDEX7_NSS1INDEX4,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATEVHT1SS_MCS5:
			rtl_set_bbreg(hw, RTXAGC_A_NSS1INDEX7_NSS1INDEX4,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATEVHT1SS_MCS6:
			rtl_set_bbreg(hw, RTXAGC_A_NSS1INDEX7_NSS1INDEX4,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATEVHT1SS_MCS7:
			rtl_set_bbreg(hw, RTXAGC_A_NSS1INDEX7_NSS1INDEX4,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATEVHT1SS_MCS8:
			rtl_set_bbreg(hw, RTXAGC_A_NSS2INDEX1_NSS1INDEX8,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATEVHT1SS_MCS9:
			rtl_set_bbreg(hw, RTXAGC_A_NSS2INDEX1_NSS1INDEX8,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATEVHT2SS_MCS0:
			rtl_set_bbreg(hw, RTXAGC_A_NSS2INDEX1_NSS1INDEX8,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATEVHT2SS_MCS1:
			rtl_set_bbreg(hw, RTXAGC_A_NSS2INDEX1_NSS1INDEX8,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATEVHT2SS_MCS2:
			rtl_set_bbreg(hw, RTXAGC_A_NSS2INDEX5_NSS2INDEX2,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATEVHT2SS_MCS3:
			rtl_set_bbreg(hw, RTXAGC_A_NSS2INDEX5_NSS2INDEX2,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATEVHT2SS_MCS4:
			rtl_set_bbreg(hw, RTXAGC_A_NSS2INDEX5_NSS2INDEX2,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATEVHT2SS_MCS5:
			rtl_set_bbreg(hw, RTXAGC_A_NSS2INDEX5_NSS2INDEX2,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATEVHT2SS_MCS6:
			rtl_set_bbreg(hw, RTXAGC_A_NSS2INDEX9_NSS2INDEX6,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATEVHT2SS_MCS7:
			rtl_set_bbreg(hw, RTXAGC_A_NSS2INDEX9_NSS2INDEX6,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATEVHT2SS_MCS8:
			rtl_set_bbreg(hw, RTXAGC_A_NSS2INDEX9_NSS2INDEX6,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATEVHT2SS_MCS9:
			rtl_set_bbreg(hw, RTXAGC_A_NSS2INDEX9_NSS2INDEX6,
				      MASKBYTE3, power_index);
			break;
		default:
			RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
				"Invalid Rate!!\n");
			break;
		}
	} else if (path == RF90_PATH_B) {
		switch (rate) {
		case DESC_RATE1M:
			rtl_set_bbreg(hw, RTXAGC_B_CCK11_CCK1,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATE2M:
			rtl_set_bbreg(hw, RTXAGC_B_CCK11_CCK1,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATE5_5M:
			rtl_set_bbreg(hw, RTXAGC_B_CCK11_CCK1,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATE11M:
			rtl_set_bbreg(hw, RTXAGC_B_CCK11_CCK1,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATE6M:
			rtl_set_bbreg(hw, RTXAGC_B_OFDM18_OFDM6,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATE9M:
			rtl_set_bbreg(hw, RTXAGC_B_OFDM18_OFDM6,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATE12M:
			rtl_set_bbreg(hw, RTXAGC_B_OFDM18_OFDM6,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATE18M:
			rtl_set_bbreg(hw, RTXAGC_B_OFDM18_OFDM6,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATE24M:
			rtl_set_bbreg(hw, RTXAGC_B_OFDM54_OFDM24,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATE36M:
			rtl_set_bbreg(hw, RTXAGC_B_OFDM54_OFDM24,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATE48M:
			rtl_set_bbreg(hw, RTXAGC_B_OFDM54_OFDM24,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATE54M:
			rtl_set_bbreg(hw, RTXAGC_B_OFDM54_OFDM24,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATEMCS0:
			rtl_set_bbreg(hw, RTXAGC_B_MCS03_MCS00,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATEMCS1:
			rtl_set_bbreg(hw, RTXAGC_B_MCS03_MCS00,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATEMCS2:
			rtl_set_bbreg(hw, RTXAGC_B_MCS03_MCS00,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATEMCS3:
			rtl_set_bbreg(hw, RTXAGC_B_MCS03_MCS00,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATEMCS4:
			rtl_set_bbreg(hw, RTXAGC_B_MCS07_MCS04,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATEMCS5:
			rtl_set_bbreg(hw, RTXAGC_B_MCS07_MCS04,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATEMCS6:
			rtl_set_bbreg(hw, RTXAGC_B_MCS07_MCS04,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATEMCS7:
			rtl_set_bbreg(hw, RTXAGC_B_MCS07_MCS04,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATEMCS8:
			rtl_set_bbreg(hw, RTXAGC_B_MCS11_MCS08,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATEMCS9:
			rtl_set_bbreg(hw, RTXAGC_B_MCS11_MCS08,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATEMCS10:
			rtl_set_bbreg(hw, RTXAGC_B_MCS11_MCS08,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATEMCS11:
			rtl_set_bbreg(hw, RTXAGC_B_MCS11_MCS08,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATEMCS12:
			rtl_set_bbreg(hw, RTXAGC_B_MCS15_MCS12,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATEMCS13:
			rtl_set_bbreg(hw, RTXAGC_B_MCS15_MCS12,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATEMCS14:
			rtl_set_bbreg(hw, RTXAGC_B_MCS15_MCS12,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATEMCS15:
			rtl_set_bbreg(hw, RTXAGC_B_MCS15_MCS12,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATEVHT1SS_MCS0:
			rtl_set_bbreg(hw, RTXAGC_B_NSS1INDEX3_NSS1INDEX0,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATEVHT1SS_MCS1:
			rtl_set_bbreg(hw, RTXAGC_B_NSS1INDEX3_NSS1INDEX0,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATEVHT1SS_MCS2:
			rtl_set_bbreg(hw, RTXAGC_B_NSS1INDEX3_NSS1INDEX0,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATEVHT1SS_MCS3:
			rtl_set_bbreg(hw, RTXAGC_B_NSS1INDEX3_NSS1INDEX0,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATEVHT1SS_MCS4:
			rtl_set_bbreg(hw, RTXAGC_B_NSS1INDEX7_NSS1INDEX4,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATEVHT1SS_MCS5:
			rtl_set_bbreg(hw, RTXAGC_B_NSS1INDEX7_NSS1INDEX4,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATEVHT1SS_MCS6:
			rtl_set_bbreg(hw, RTXAGC_B_NSS1INDEX7_NSS1INDEX4,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATEVHT1SS_MCS7:
			rtl_set_bbreg(hw, RTXAGC_B_NSS1INDEX7_NSS1INDEX4,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATEVHT1SS_MCS8:
			rtl_set_bbreg(hw, RTXAGC_B_NSS2INDEX1_NSS1INDEX8,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATEVHT1SS_MCS9:
			rtl_set_bbreg(hw, RTXAGC_B_NSS2INDEX1_NSS1INDEX8,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATEVHT2SS_MCS0:
			rtl_set_bbreg(hw, RTXAGC_B_NSS2INDEX1_NSS1INDEX8,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATEVHT2SS_MCS1:
			rtl_set_bbreg(hw, RTXAGC_B_NSS2INDEX1_NSS1INDEX8,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATEVHT2SS_MCS2:
			rtl_set_bbreg(hw, RTXAGC_B_NSS2INDEX5_NSS2INDEX2,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATEVHT2SS_MCS3:
			rtl_set_bbreg(hw, RTXAGC_B_NSS2INDEX5_NSS2INDEX2,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATEVHT2SS_MCS4:
			rtl_set_bbreg(hw, RTXAGC_B_NSS2INDEX5_NSS2INDEX2,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATEVHT2SS_MCS5:
			rtl_set_bbreg(hw, RTXAGC_B_NSS2INDEX5_NSS2INDEX2,
				      MASKBYTE3, power_index);
			break;
		case DESC_RATEVHT2SS_MCS6:
			rtl_set_bbreg(hw, RTXAGC_B_NSS2INDEX9_NSS2INDEX6,
				      MASKBYTE0, power_index);
			break;
		case DESC_RATEVHT2SS_MCS7:
			rtl_set_bbreg(hw, RTXAGC_B_NSS2INDEX9_NSS2INDEX6,
				      MASKBYTE1, power_index);
			break;
		case DESC_RATEVHT2SS_MCS8:
			rtl_set_bbreg(hw, RTXAGC_B_NSS2INDEX9_NSS2INDEX6,
				      MASKBYTE2, power_index);
			break;
		case DESC_RATEVHT2SS_MCS9:
			rtl_set_bbreg(hw, RTXAGC_B_NSS2INDEX9_NSS2INDEX6,
				      MASKBYTE3, power_index);
			break;
		default:
			RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
				 "Invalid Rate!!\n");
			break;
		}
	} else {
		RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
			 "Invalid RFPath!!\n");
	}
}

static void _rtl8821ae_phy_set_txpower_level_by_path(struct ieee80211_hw *hw,
						     u8 *array, u8 path,
						     u8 channel, u8 size)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 i;
	u8 power_index;

	for (i = 0; i < size; i++) {
		power_index =
		  _rtl8821ae_get_txpower_index(hw, path, array[i],
					       rtlphy->current_chan_bw,
					       channel);
		_rtl8821ae_phy_set_txpower_index(hw, power_index, path,
						 array[i]);
	}
}

static void _rtl8821ae_phy_txpower_training_by_path(struct ieee80211_hw *hw,
						    u8 bw, u8 channel, u8 path)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;

	u8 i;
	u32 power_level, data, offset;

	if (path >= rtlphy->num_total_rfpath)
		return;

	data = 0;
	if (path == RF90_PATH_A) {
		power_level =
			_rtl8821ae_get_txpower_index(hw, RF90_PATH_A,
			DESC_RATEMCS7, bw, channel);
		offset =  RA_TXPWRTRAING;
	} else {
		power_level =
			_rtl8821ae_get_txpower_index(hw, RF90_PATH_B,
			DESC_RATEMCS7, bw, channel);
		offset =  RB_TXPWRTRAING;
	}

	for (i = 0; i < 3; i++) {
		if (i == 0)
			power_level = power_level - 10;
		else if (i == 1)
			power_level = power_level - 8;
		else
			power_level = power_level - 6;

		data |= (((power_level > 2) ? (power_level) : 2) << (i * 8));
	}
	rtl_set_bbreg(hw, offset, 0xffffff, data);
}

void rtl8821ae_phy_set_txpower_level_by_path(struct ieee80211_hw *hw,
					     u8 channel, u8 path)
{
	/* struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw)); */
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 cck_rates[]  = {DESC_RATE1M, DESC_RATE2M, DESC_RATE5_5M,
			      DESC_RATE11M};
	u8 sizes_of_cck_retes = 4;
	u8 ofdm_rates[]  = {DESC_RATE6M, DESC_RATE9M, DESC_RATE12M,
				DESC_RATE18M, DESC_RATE24M, DESC_RATE36M,
				DESC_RATE48M, DESC_RATE54M};
	u8 sizes_of_ofdm_retes = 8;
	u8 ht_rates_1t[]  = {DESC_RATEMCS0, DESC_RATEMCS1, DESC_RATEMCS2,
				DESC_RATEMCS3, DESC_RATEMCS4, DESC_RATEMCS5,
				DESC_RATEMCS6, DESC_RATEMCS7};
	u8 sizes_of_ht_retes_1t = 8;
	u8 ht_rates_2t[]  = {DESC_RATEMCS8, DESC_RATEMCS9,
				DESC_RATEMCS10, DESC_RATEMCS11,
				DESC_RATEMCS12, DESC_RATEMCS13,
				DESC_RATEMCS14, DESC_RATEMCS15};
	u8 sizes_of_ht_retes_2t = 8;
	u8 vht_rates_1t[]  = {DESC_RATEVHT1SS_MCS0, DESC_RATEVHT1SS_MCS1,
				DESC_RATEVHT1SS_MCS2, DESC_RATEVHT1SS_MCS3,
				DESC_RATEVHT1SS_MCS4, DESC_RATEVHT1SS_MCS5,
				DESC_RATEVHT1SS_MCS6, DESC_RATEVHT1SS_MCS7,
			     DESC_RATEVHT1SS_MCS8, DESC_RATEVHT1SS_MCS9};
	u8 vht_rates_2t[]  = {DESC_RATEVHT2SS_MCS0, DESC_RATEVHT2SS_MCS1,
				DESC_RATEVHT2SS_MCS2, DESC_RATEVHT2SS_MCS3,
				DESC_RATEVHT2SS_MCS4, DESC_RATEVHT2SS_MCS5,
				DESC_RATEVHT2SS_MCS6, DESC_RATEVHT2SS_MCS7,
				DESC_RATEVHT2SS_MCS8, DESC_RATEVHT2SS_MCS9};
	u8 sizes_of_vht_retes = 10;

	if (rtlhal->current_bandtype == BAND_ON_2_4G)
		_rtl8821ae_phy_set_txpower_level_by_path(hw, cck_rates, path, channel,
							 sizes_of_cck_retes);

	_rtl8821ae_phy_set_txpower_level_by_path(hw, ofdm_rates, path, channel,
						 sizes_of_ofdm_retes);
	_rtl8821ae_phy_set_txpower_level_by_path(hw, ht_rates_1t, path, channel,
						 sizes_of_ht_retes_1t);
	_rtl8821ae_phy_set_txpower_level_by_path(hw, vht_rates_1t, path, channel,
						 sizes_of_vht_retes);

	if (rtlphy->num_total_rfpath >= 2) {
		_rtl8821ae_phy_set_txpower_level_by_path(hw, ht_rates_2t, path,
							 channel,
							 sizes_of_ht_retes_2t);
		_rtl8821ae_phy_set_txpower_level_by_path(hw, vht_rates_2t, path,
							 channel,
							 sizes_of_vht_retes);
	}

	_rtl8821ae_phy_txpower_training_by_path(hw, rtlphy->current_chan_bw,
						channel, path);
}

/*just in case, write txpower in DW, to reduce time*/
void rtl8821ae_phy_set_txpower_level(struct ieee80211_hw *hw, u8 channel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 path = 0;

	for (path = RF90_PATH_A; path < rtlphy->num_total_rfpath; ++path)
		rtl8821ae_phy_set_txpower_level_by_path(hw, channel, path);
}

static long _rtl8821ae_phy_txpwr_idx_to_dbm(struct ieee80211_hw *hw,
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

void rtl8821ae_phy_scan_operation_backup(struct ieee80211_hw *hw, u8 operation)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	enum io_type iotype = IO_CMD_PAUSE_BAND0_DM_BY_SCAN;

	if (!is_hal_stop(rtlhal)) {
		switch (operation) {
		case SCAN_OPT_BACKUP_BAND0:
			iotype = IO_CMD_PAUSE_BAND0_DM_BY_SCAN;
			rtlpriv->cfg->ops->set_hw_reg(hw,
						      HW_VAR_IO_CMD,
						      (u8 *)&iotype);

			break;
		case SCAN_OPT_BACKUP_BAND1:
			iotype = IO_CMD_PAUSE_BAND1_DM_BY_SCAN;
			rtlpriv->cfg->ops->set_hw_reg(hw,
						      HW_VAR_IO_CMD,
						      (u8 *)&iotype);

			break;
		case SCAN_OPT_RESTORE:
			iotype = IO_CMD_RESUME_DM_BY_SCAN;
			rtlpriv->cfg->ops->set_hw_reg(hw,
						      HW_VAR_IO_CMD,
						      (u8 *)&iotype);
			break;
		default:
			RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
				 "Unknown Scan Backup operation.\n");
			break;
		}
	}
}

static void _rtl8821ae_phy_set_reg_bw(struct rtl_priv *rtlpriv, u8 bw)
{
	u16 reg_rf_mode_bw, tmp = 0;

	reg_rf_mode_bw = rtl_read_word(rtlpriv, REG_TRXPTCL_CTL);
	switch (bw) {
	case HT_CHANNEL_WIDTH_20:
		rtl_write_word(rtlpriv, REG_TRXPTCL_CTL, reg_rf_mode_bw & 0xFE7F);
		break;
	case HT_CHANNEL_WIDTH_20_40:
		tmp = reg_rf_mode_bw | BIT(7);
		rtl_write_word(rtlpriv, REG_TRXPTCL_CTL, tmp & 0xFEFF);
		break;
	case HT_CHANNEL_WIDTH_80:
		tmp = reg_rf_mode_bw | BIT(8);
		rtl_write_word(rtlpriv, REG_TRXPTCL_CTL, tmp & 0xFF7F);
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING, "unknown Bandwidth: 0x%x\n", bw);
		break;
	}
}

static u8 _rtl8821ae_phy_get_secondary_chnl(struct rtl_priv *rtlpriv)
{
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_mac *mac = rtl_mac(rtlpriv);
	u8 sc_set_40 = 0, sc_set_20 = 0;

	if (rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_80) {
		if (mac->cur_80_prime_sc == PRIME_CHNL_OFFSET_LOWER)
			sc_set_40 = VHT_DATA_SC_40_LOWER_OF_80MHZ;
		else if (mac->cur_80_prime_sc == PRIME_CHNL_OFFSET_UPPER)
			sc_set_40 = VHT_DATA_SC_40_UPPER_OF_80MHZ;
		else
			RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
				"SCMapping: Not Correct Primary40MHz Setting\n");

		if ((mac->cur_40_prime_sc == PRIME_CHNL_OFFSET_LOWER) &&
			(mac->cur_80_prime_sc == HAL_PRIME_CHNL_OFFSET_LOWER))
			sc_set_20 = VHT_DATA_SC_20_LOWEST_OF_80MHZ;
		else if ((mac->cur_40_prime_sc == PRIME_CHNL_OFFSET_UPPER) &&
			(mac->cur_80_prime_sc == HAL_PRIME_CHNL_OFFSET_LOWER))
			sc_set_20 = VHT_DATA_SC_20_LOWER_OF_80MHZ;
		else if ((mac->cur_40_prime_sc == PRIME_CHNL_OFFSET_LOWER) &&
			(mac->cur_80_prime_sc == HAL_PRIME_CHNL_OFFSET_UPPER))
			sc_set_20 = VHT_DATA_SC_20_UPPER_OF_80MHZ;
		else if ((mac->cur_40_prime_sc == PRIME_CHNL_OFFSET_UPPER) &&
			(mac->cur_80_prime_sc == HAL_PRIME_CHNL_OFFSET_UPPER))
			sc_set_20 = VHT_DATA_SC_20_UPPERST_OF_80MHZ;
		else
			RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
				"SCMapping: Not Correct Primary40MHz Setting\n");
	} else if (rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_20_40) {
		if (mac->cur_40_prime_sc == PRIME_CHNL_OFFSET_UPPER)
			sc_set_20 = VHT_DATA_SC_20_UPPER_OF_80MHZ;
		else if (mac->cur_40_prime_sc == PRIME_CHNL_OFFSET_LOWER)
			sc_set_20 = VHT_DATA_SC_20_LOWER_OF_80MHZ;
		else
			RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "SCMapping: Not Correct Primary40MHz Setting\n");
	}
	return (sc_set_40 << 4) | sc_set_20;
}

void rtl8821ae_phy_set_bw_mode_callback(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 sub_chnl = 0;
	u8 l1pk_val = 0;

	RT_TRACE(rtlpriv, COMP_SCAN, DBG_TRACE,
		 "Switch to %s bandwidth\n",
		  (rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_20 ?
		  "20MHz" :
		  (rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_20_40 ?
		  "40MHz" : "80MHz")));

	_rtl8821ae_phy_set_reg_bw(rtlpriv, rtlphy->current_chan_bw);
	sub_chnl = _rtl8821ae_phy_get_secondary_chnl(rtlpriv);
	rtl_write_byte(rtlpriv, 0x0483, sub_chnl);

	switch (rtlphy->current_chan_bw) {
	case HT_CHANNEL_WIDTH_20:
		rtl_set_bbreg(hw, RRFMOD, 0x003003C3, 0x00300200);
		rtl_set_bbreg(hw, RADC_BUF_CLK, BIT(30), 0);

		if (rtlphy->rf_type == RF_2T2R)
			rtl_set_bbreg(hw, RL1PEAKTH, 0x03C00000, 7);
		else
			rtl_set_bbreg(hw, RL1PEAKTH, 0x03C00000, 8);
		break;
	case HT_CHANNEL_WIDTH_20_40:
		rtl_set_bbreg(hw, RRFMOD, 0x003003C3, 0x00300201);
		rtl_set_bbreg(hw, RADC_BUF_CLK, BIT(30), 0);
		rtl_set_bbreg(hw, RRFMOD, 0x3C, sub_chnl);
		rtl_set_bbreg(hw, RCCAONSEC, 0xf0000000, sub_chnl);

		if (rtlphy->reg_837 & BIT(2))
			l1pk_val = 6;
		else {
			if (rtlphy->rf_type == RF_2T2R)
				l1pk_val = 7;
			else
				l1pk_val = 8;
		}
		/* 0x848[25:22] = 0x6 */
		rtl_set_bbreg(hw, RL1PEAKTH, 0x03C00000, l1pk_val);

		if (sub_chnl == VHT_DATA_SC_20_UPPER_OF_80MHZ)
			rtl_set_bbreg(hw, RCCK_SYSTEM, BCCK_SYSTEM, 1);
		else
			rtl_set_bbreg(hw, RCCK_SYSTEM, BCCK_SYSTEM, 0);
		break;

	case HT_CHANNEL_WIDTH_80:
		 /* 0x8ac[21,20,9:6,1,0]=8'b11100010 */
		rtl_set_bbreg(hw, RRFMOD, 0x003003C3, 0x00300202);
		/* 0x8c4[30] = 1 */
		rtl_set_bbreg(hw, RADC_BUF_CLK, BIT(30), 1);
		rtl_set_bbreg(hw, RRFMOD, 0x3C, sub_chnl);
		rtl_set_bbreg(hw, RCCAONSEC, 0xf0000000, sub_chnl);

		if (rtlphy->reg_837 & BIT(2))
			l1pk_val = 5;
		else {
			if (rtlphy->rf_type == RF_2T2R)
				l1pk_val = 6;
			else
				l1pk_val = 7;
		}
		rtl_set_bbreg(hw, RL1PEAKTH, 0x03C00000, l1pk_val);

		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "unknown bandwidth: %#X\n", rtlphy->current_chan_bw);
		break;
	}

	rtl8812ae_fixspur(hw, rtlphy->current_chan_bw, rtlphy->current_channel);

	rtl8821ae_phy_rf6052_set_bandwidth(hw, rtlphy->current_chan_bw);
	rtlphy->set_bwmode_inprogress = false;

	RT_TRACE(rtlpriv, COMP_SCAN, DBG_LOUD, "\n");
}

void rtl8821ae_phy_set_bw_mode(struct ieee80211_hw *hw,
			    enum nl80211_channel_type ch_type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 tmp_bw = rtlphy->current_chan_bw;

	if (rtlphy->set_bwmode_inprogress)
		return;
	rtlphy->set_bwmode_inprogress = true;
	if ((!is_hal_stop(rtlhal)) && !(RT_CANNOT_IO(hw)))
		rtl8821ae_phy_set_bw_mode_callback(hw);
	else {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 "FALSE driver sleep or unload\n");
		rtlphy->set_bwmode_inprogress = false;
		rtlphy->current_chan_bw = tmp_bw;
	}
}

void rtl8821ae_phy_sw_chnl_callback(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 channel = rtlphy->current_channel;
	u8 path;
	u32 data;

	RT_TRACE(rtlpriv, COMP_SCAN, DBG_TRACE,
		 "switch to channel%d\n", rtlphy->current_channel);
	if (is_hal_stop(rtlhal))
		return;

	if (36 <= channel && channel <= 48)
		data = 0x494;
	else if (50 <= channel && channel <= 64)
		data = 0x453;
	else if (100 <= channel && channel <= 116)
		data = 0x452;
	else if (118 <= channel)
		data = 0x412;
	else
		data = 0x96a;
	rtl_set_bbreg(hw, RFC_AREA, 0x1ffe0000, data);

	for (path = RF90_PATH_A; path < rtlphy->num_total_rfpath; path++) {
		if (36 <= channel && channel <= 64)
			data = 0x101;
		else if (100 <= channel && channel <= 140)
			data = 0x301;
		else if (140 < channel)
			data = 0x501;
		else
			data = 0x000;
		rtl8821ae_phy_set_rf_reg(hw, path, RF_CHNLBW,
			BIT(18)|BIT(17)|BIT(16)|BIT(9)|BIT(8), data);

		rtl8821ae_phy_set_rf_reg(hw, path, RF_CHNLBW,
			BMASKBYTE0, channel);

		if (channel > 14) {
			if (rtlhal->hw_type == HARDWARE_TYPE_RTL8821AE) {
				if (36 <= channel && channel <= 64)
					data = 0x114E9;
				else if (100 <= channel && channel <= 140)
					data = 0x110E9;
				else
					data = 0x110E9;
				rtl8821ae_phy_set_rf_reg(hw, path, RF_APK,
					BRFREGOFFSETMASK, data);
			}
		}
	}
	RT_TRACE(rtlpriv, COMP_SCAN, DBG_TRACE, "\n");
}

u8 rtl8821ae_phy_sw_chnl(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
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

	if (rtlphy->current_channel > 14 && rtlhal->current_bandtype != BAND_ON_5G)
		rtl8821ae_phy_switch_wirelessband(hw, BAND_ON_5G);
	else if (rtlphy->current_channel <= 14 && rtlhal->current_bandtype != BAND_ON_2_4G)
		rtl8821ae_phy_switch_wirelessband(hw, BAND_ON_2_4G);

	rtlphy->sw_chnl_inprogress = true;
	if (channel == 0)
		channel = 1;

	RT_TRACE(rtlpriv, COMP_SCAN, DBG_TRACE,
		 "switch to channel%d, band type is %d\n",
		 rtlphy->current_channel, rtlhal->current_bandtype);

	rtl8821ae_phy_sw_chnl_callback(hw);

	rtl8821ae_dm_clear_txpower_tracking_state(hw);
	rtl8821ae_phy_set_txpower_level(hw, rtlphy->current_channel);

	RT_TRACE(rtlpriv, COMP_SCAN, DBG_TRACE, "\n");
	rtlphy->sw_chnl_inprogress = false;
	return 1;
}

u8 _rtl8812ae_get_right_chnl_place_for_iqk(u8 chnl)
{
	u8 channel_all[TARGET_CHNL_NUM_2G_5G_8812] = {
		1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
		14, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54,
		56, 58, 60, 62, 64, 100, 102, 104, 106, 108,
		110, 112, 114, 116, 118, 120, 122, 124, 126,
		128, 130, 132, 134, 136, 138, 140, 149, 151,
		153, 155, 157, 159, 161, 163, 165};
	u8 place = chnl;

	if (chnl > 14) {
		for (place = 14; place < sizeof(channel_all); place++)
			if (channel_all[place] == chnl)
				return place-13;
	}

	return 0;
}

#define MACBB_REG_NUM 10
#define AFE_REG_NUM 14
#define RF_REG_NUM 3

static void _rtl8821ae_iqk_backup_macbb(struct ieee80211_hw *hw,
					u32 *macbb_backup,
					u32 *backup_macbb_reg, u32 mac_bb_num)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 i;

	rtl_set_bbreg(hw, 0x82c, BIT(31), 0x0); /*[31] = 0 --> Page C*/
	/*save MACBB default value*/
	for (i = 0; i < mac_bb_num; i++)
		macbb_backup[i] = rtl_read_dword(rtlpriv, backup_macbb_reg[i]);

	RT_TRACE(rtlpriv, COMP_IQK, DBG_LOUD, "BackupMacBB Success!!!!\n");
}

static void _rtl8821ae_iqk_backup_afe(struct ieee80211_hw *hw, u32 *afe_backup,
				      u32 *backup_afe_REG, u32 afe_num)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 i;

	rtl_set_bbreg(hw, 0x82c, BIT(31), 0x0); /*[31] = 0 --> Page C*/
	/*Save AFE Parameters */
	for (i = 0; i < afe_num; i++)
		afe_backup[i] = rtl_read_dword(rtlpriv, backup_afe_REG[i]);
	RT_TRACE(rtlpriv, COMP_IQK, DBG_LOUD, "BackupAFE Success!!!!\n");
}

static void _rtl8821ae_iqk_backup_rf(struct ieee80211_hw *hw, u32 *rfa_backup,
				     u32 *rfb_backup, u32 *backup_rf_reg,
				     u32 rf_num)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 i;

	rtl_set_bbreg(hw, 0x82c, BIT(31), 0x0); /*[31] = 0 --> Page C*/
	/*Save RF Parameters*/
	for (i = 0; i < rf_num; i++) {
		rfa_backup[i] = rtl_get_rfreg(hw, RF90_PATH_A, backup_rf_reg[i],
					      BMASKDWORD);
		rfb_backup[i] = rtl_get_rfreg(hw, RF90_PATH_B, backup_rf_reg[i],
					      BMASKDWORD);
	}
	RT_TRACE(rtlpriv, COMP_IQK, DBG_LOUD, "BackupRF Success!!!!\n");
}

static void _rtl8821ae_iqk_configure_mac(
		struct ieee80211_hw *hw
		)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	/* ========MAC register setting========*/
	rtl_set_bbreg(hw, 0x82c, BIT(31), 0x0); /*[31] = 0 --> Page C*/
	rtl_write_byte(rtlpriv, 0x522, 0x3f);
	rtl_set_bbreg(hw, 0x550, BIT(11) | BIT(3), 0x0);
	rtl_write_byte(rtlpriv, 0x808, 0x00);		/*RX ante off*/
	rtl_set_bbreg(hw, 0x838, 0xf, 0xc);		/*CCA off*/
}

static void _rtl8821ae_iqk_tx_fill_iqc(struct ieee80211_hw *hw,
				       enum radio_path path, u32 tx_x, u32 tx_y)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	switch (path) {
	case RF90_PATH_A:
		/* [31] = 1 --> Page C1 */
		rtl_set_bbreg(hw, 0x82c, BIT(31), 0x1);
		rtl_write_dword(rtlpriv, 0xc90, 0x00000080);
		rtl_write_dword(rtlpriv, 0xcc4, 0x20040000);
		rtl_write_dword(rtlpriv, 0xcc8, 0x20000000);
		rtl_set_bbreg(hw, 0xccc, 0x000007ff, tx_y);
		rtl_set_bbreg(hw, 0xcd4, 0x000007ff, tx_x);
		RT_TRACE(rtlpriv, COMP_IQK, DBG_LOUD,
			 "TX_X = %x;;TX_Y = %x =====> fill to IQC\n",
			 tx_x, tx_y);
		RT_TRACE(rtlpriv, COMP_IQK, DBG_LOUD,
			 "0xcd4 = %x;;0xccc = %x ====>fill to IQC\n",
			 rtl_get_bbreg(hw, 0xcd4, 0x000007ff),
			 rtl_get_bbreg(hw, 0xccc, 0x000007ff));
		break;
	default:
		break;
	}
}

static void _rtl8821ae_iqk_rx_fill_iqc(struct ieee80211_hw *hw,
				       enum radio_path path, u32 rx_x, u32 rx_y)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	switch (path) {
	case RF90_PATH_A:
		rtl_set_bbreg(hw, 0x82c, BIT(31), 0x0); /* [31] = 0 --> Page C */
		rtl_set_bbreg(hw, 0xc10, 0x000003ff, rx_x>>1);
		rtl_set_bbreg(hw, 0xc10, 0x03ff0000, rx_y>>1);
		RT_TRACE(rtlpriv, COMP_IQK, DBG_LOUD,
			 "rx_x = %x;;rx_y = %x ====>fill to IQC\n",
			 rx_x>>1, rx_y>>1);
		RT_TRACE(rtlpriv, COMP_IQK, DBG_LOUD,
			 "0xc10 = %x ====>fill to IQC\n",
			 rtl_read_dword(rtlpriv, 0xc10));
		break;
	default:
		break;
	}
}

#define cal_num 10

static void _rtl8821ae_iqk_tx(struct ieee80211_hw *hw, enum radio_path path)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	u32	tx_fail, rx_fail, delay_count, iqk_ready, cal_retry, cal = 0, temp_reg65;
	int	tx_x = 0, tx_y = 0, rx_x = 0, rx_y = 0, tx_average = 0, rx_average = 0;
	int	tx_x0[cal_num], tx_y0[cal_num], tx_x0_rxk[cal_num],
		tx_y0_rxk[cal_num], rx_x0[cal_num], rx_y0[cal_num];
	bool	tx0iqkok = false, rx0iqkok = false;
	bool	vdf_enable = false;
	int	i, k, vdf_y[3], vdf_x[3], tx_dt[3], rx_dt[3],
		ii, dx = 0, dy = 0, tx_finish = 0, rx_finish = 0;

	RT_TRACE(rtlpriv, COMP_IQK, DBG_LOUD,
			"BandWidth = %d.\n",
			 rtlphy->current_chan_bw);
	if (rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_80)
		vdf_enable = true;

	while (cal < cal_num) {
		switch (path) {
		case RF90_PATH_A:
			temp_reg65 = rtl_get_rfreg(hw, path, 0x65, 0xffffffff);
			/* Path-A LOK */
			rtl_set_bbreg(hw, 0x82c, BIT(31), 0x0); /*[31] = 0 --> Page C*/
			/*========Path-A AFE all on========*/
			/*Port 0 DAC/ADC on*/
			rtl_write_dword(rtlpriv, 0xc60, 0x77777777);
			rtl_write_dword(rtlpriv, 0xc64, 0x77777777);
			rtl_write_dword(rtlpriv, 0xc68, 0x19791979);
			rtl_write_dword(rtlpriv, 0xc6c, 0x19791979);
			rtl_write_dword(rtlpriv, 0xc70, 0x19791979);
			rtl_write_dword(rtlpriv, 0xc74, 0x19791979);
			rtl_write_dword(rtlpriv, 0xc78, 0x19791979);
			rtl_write_dword(rtlpriv, 0xc7c, 0x19791979);
			rtl_write_dword(rtlpriv, 0xc80, 0x19791979);
			rtl_write_dword(rtlpriv, 0xc84, 0x19791979);

			rtl_set_bbreg(hw, 0xc00, 0xf, 0x4); /*hardware 3-wire off*/

			/* LOK Setting */
			/* ====== LOK ====== */
			/*DAC/ADC sampling rate (160 MHz)*/
			rtl_set_bbreg(hw, 0xc5c, BIT(26) | BIT(25) | BIT(24), 0x7);

			/* 2. LoK RF Setting (at BW = 20M) */
			rtl_set_rfreg(hw, path, 0xef, RFREG_OFFSET_MASK, 0x80002);
			rtl_set_rfreg(hw, path, 0x18, 0x00c00, 0x3);     /* BW 20M */
			rtl_set_rfreg(hw, path, 0x30, RFREG_OFFSET_MASK, 0x20000);
			rtl_set_rfreg(hw, path, 0x31, RFREG_OFFSET_MASK, 0x0003f);
			rtl_set_rfreg(hw, path, 0x32, RFREG_OFFSET_MASK, 0xf3fc3);
			rtl_set_rfreg(hw, path, 0x65, RFREG_OFFSET_MASK, 0x931d5);
			rtl_set_rfreg(hw, path, 0x8f, RFREG_OFFSET_MASK, 0x8a001);
			rtl_set_bbreg(hw, 0xcb8, 0xf, 0xd);
			rtl_write_dword(rtlpriv, 0x90c, 0x00008000);
			rtl_write_dword(rtlpriv, 0xb00, 0x03000100);
			rtl_set_bbreg(hw, 0xc94, BIT(0), 0x1);
			rtl_write_dword(rtlpriv, 0x978, 0x29002000);/* TX (X,Y) */
			rtl_write_dword(rtlpriv, 0x97c, 0xa9002000);/* RX (X,Y) */
			rtl_write_dword(rtlpriv, 0x984, 0x00462910);/* [0]:AGC_en, [15]:idac_K_Mask */

			rtl_set_bbreg(hw, 0x82c, BIT(31), 0x1); /* [31] = 1 --> Page C1 */
			rtl_write_dword(rtlpriv, 0xc88, 0x821403f4);

			if (rtlhal->current_bandtype)
				rtl_write_dword(rtlpriv, 0xc8c, 0x68163e96);
			else
				rtl_write_dword(rtlpriv, 0xc8c, 0x28163e96);

			rtl_write_dword(rtlpriv, 0xc80, 0x18008c10);/* TX_TONE_idx[9:0], TxK_Mask[29] TX_Tone = 16 */
			rtl_write_dword(rtlpriv, 0xc84, 0x38008c10);/* RX_TONE_idx[9:0], RxK_Mask[29] */
			rtl_write_dword(rtlpriv, 0xcb8, 0x00100000);/* cb8[20] \B1N SI/PI \A8ϥ\CE\C5v\A4\C1\B5\B9 iqk_dpk module */
			rtl_write_dword(rtlpriv, 0x980, 0xfa000000);
			rtl_write_dword(rtlpriv, 0x980, 0xf8000000);

			mdelay(10); /* Delay 10ms */
			rtl_write_dword(rtlpriv, 0xcb8, 0x00000000);

			rtl_set_bbreg(hw, 0x82c, BIT(31), 0x0); /* [31] = 0 --> Page C */
			rtl_set_rfreg(hw, path, 0x58, 0x7fe00, rtl_get_rfreg(hw, path, 0x8, 0xffc00)); /* Load LOK */

			switch (rtlphy->current_chan_bw) {
			case 1:
				rtl_set_rfreg(hw, path, 0x18, 0x00c00, 0x1);
				break;
			case 2:
				rtl_set_rfreg(hw, path, 0x18, 0x00c00, 0x0);
				break;
			default:
				break;
			}

			rtl_set_bbreg(hw, 0x82c, BIT(31), 0x1); /* [31] = 1 --> Page C1 */

			/* 3. TX RF Setting */
			rtl_set_bbreg(hw, 0x82c, BIT(31), 0x0); /* [31] = 0 --> Page C */
			rtl_set_rfreg(hw, path, 0xef, RFREG_OFFSET_MASK, 0x80000);
			rtl_set_rfreg(hw, path, 0x30, RFREG_OFFSET_MASK, 0x20000);
			rtl_set_rfreg(hw, path, 0x31, RFREG_OFFSET_MASK, 0x0003f);
			rtl_set_rfreg(hw, path, 0x32, RFREG_OFFSET_MASK, 0xf3fc3);
			rtl_set_rfreg(hw, path, 0x65, RFREG_OFFSET_MASK, 0x931d5);
			rtl_set_rfreg(hw, path, 0x8f, RFREG_OFFSET_MASK, 0x8a001);
			rtl_set_rfreg(hw, path, 0xef, RFREG_OFFSET_MASK, 0x00000);
			/* ODM_SetBBReg(pDM_Odm, 0xcb8, 0xf, 0xd); */
			rtl_write_dword(rtlpriv, 0x90c, 0x00008000);
			rtl_write_dword(rtlpriv, 0xb00, 0x03000100);
			rtl_set_bbreg(hw, 0xc94, BIT(0), 0x1);
			rtl_write_dword(rtlpriv, 0x978, 0x29002000);/* TX (X,Y) */
			rtl_write_dword(rtlpriv, 0x97c, 0xa9002000);/* RX (X,Y) */
			rtl_write_dword(rtlpriv, 0x984, 0x0046a910);/* [0]:AGC_en, [15]:idac_K_Mask */

			rtl_set_bbreg(hw, 0x82c, BIT(31), 0x1); /* [31] = 1 --> Page C1 */
			rtl_write_dword(rtlpriv, 0xc88, 0x821403f1);
			if (rtlhal->current_bandtype)
				rtl_write_dword(rtlpriv, 0xc8c, 0x40163e96);
			else
				rtl_write_dword(rtlpriv, 0xc8c, 0x00163e96);

			if (vdf_enable == 1) {
				RT_TRACE(rtlpriv, COMP_IQK, DBG_LOUD, "VDF_enable\n");
				for (k = 0; k <= 2; k++) {
					switch (k) {
					case 0:
						rtl_write_dword(rtlpriv, 0xc80, 0x18008c38);/* TX_TONE_idx[9:0], TxK_Mask[29] TX_Tone = 16 */
						rtl_write_dword(rtlpriv, 0xc84, 0x38008c38);/* RX_TONE_idx[9:0], RxK_Mask[29] */
						rtl_set_bbreg(hw, 0xce8, BIT(31), 0x0);
						break;
					case 1:
						rtl_set_bbreg(hw, 0xc80, BIT(28), 0x0);
						rtl_set_bbreg(hw, 0xc84, BIT(28), 0x0);
						rtl_set_bbreg(hw, 0xce8, BIT(31), 0x0);
						break;
					case 2:
						RT_TRACE(rtlpriv, COMP_IQK, DBG_LOUD,
							"vdf_y[1] = %x;;;vdf_y[0] = %x\n", vdf_y[1]>>21 & 0x00007ff, vdf_y[0]>>21 & 0x00007ff);
						RT_TRACE(rtlpriv, COMP_IQK, DBG_LOUD,
							"vdf_x[1] = %x;;;vdf_x[0] = %x\n", vdf_x[1]>>21 & 0x00007ff, vdf_x[0]>>21 & 0x00007ff);
						tx_dt[cal] = (vdf_y[1]>>20)-(vdf_y[0]>>20);
						tx_dt[cal] = ((16*tx_dt[cal])*10000/15708);
						tx_dt[cal] = (tx_dt[cal] >> 1)+(tx_dt[cal] & BIT(0));
						rtl_write_dword(rtlpriv, 0xc80, 0x18008c20);/* TX_TONE_idx[9:0], TxK_Mask[29] TX_Tone = 16 */
						rtl_write_dword(rtlpriv, 0xc84, 0x38008c20);/* RX_TONE_idx[9:0], RxK_Mask[29] */
						rtl_set_bbreg(hw, 0xce8, BIT(31), 0x1);
						rtl_set_bbreg(hw, 0xce8, 0x3fff0000, tx_dt[cal] & 0x00003fff);
						break;
					default:
						break;
					}
					rtl_write_dword(rtlpriv, 0xcb8, 0x00100000);/* cb8[20] \B1N SI/PI \A8ϥ\CE\C5v\A4\C1\B5\B9 iqk_dpk module */
					cal_retry = 0;
					while (1) {
						/* one shot */
						rtl_write_dword(rtlpriv, 0x980, 0xfa000000);
						rtl_write_dword(rtlpriv, 0x980, 0xf8000000);

						mdelay(10); /* Delay 10ms */
						rtl_write_dword(rtlpriv, 0xcb8, 0x00000000);
						delay_count = 0;
						while (1) {
							iqk_ready = rtl_get_bbreg(hw, 0xd00, BIT(10));
							if ((~iqk_ready) || (delay_count > 20))
								break;
							else{
								mdelay(1);
								delay_count++;
							}
						}

						if (delay_count < 20) {							/* If 20ms No Result, then cal_retry++ */
							/* ============TXIQK Check============== */
							tx_fail = rtl_get_bbreg(hw, 0xd00, BIT(12));

							if (~tx_fail) {
								rtl_write_dword(rtlpriv, 0xcb8, 0x02000000);
								vdf_x[k] = rtl_get_bbreg(hw, 0xd00, 0x07ff0000)<<21;
								rtl_write_dword(rtlpriv, 0xcb8, 0x04000000);
								vdf_y[k] = rtl_get_bbreg(hw, 0xd00, 0x07ff0000)<<21;
								tx0iqkok = true;
								break;
							} else {
								rtl_set_bbreg(hw, 0xccc, 0x000007ff, 0x0);
								rtl_set_bbreg(hw, 0xcd4, 0x000007ff, 0x200);
								tx0iqkok = false;
								cal_retry++;
								if (cal_retry == 10)
									break;
							}
						} else {
							tx0iqkok = false;
							cal_retry++;
							if (cal_retry == 10)
								break;
						}
					}
				}
				if (k == 3) {
					tx_x0[cal] = vdf_x[k-1];
					tx_y0[cal] = vdf_y[k-1];
				}
			} else {
				rtl_write_dword(rtlpriv, 0xc80, 0x18008c10);/* TX_TONE_idx[9:0], TxK_Mask[29] TX_Tone = 16 */
				rtl_write_dword(rtlpriv, 0xc84, 0x38008c10);/* RX_TONE_idx[9:0], RxK_Mask[29] */
				rtl_write_dword(rtlpriv, 0xcb8, 0x00100000);/* cb8[20] \B1N SI/PI \A8ϥ\CE\C5v\A4\C1\B5\B9 iqk_dpk module */
				cal_retry = 0;
				while (1) {
					/* one shot */
					rtl_write_dword(rtlpriv, 0x980, 0xfa000000);
					rtl_write_dword(rtlpriv, 0x980, 0xf8000000);

					mdelay(10); /* Delay 10ms */
					rtl_write_dword(rtlpriv, 0xcb8, 0x00000000);
					delay_count = 0;
					while (1) {
						iqk_ready = rtl_get_bbreg(hw, 0xd00, BIT(10));
						if ((~iqk_ready) || (delay_count > 20))
							break;
						else{
							mdelay(1);
							delay_count++;
						}
					}

					if (delay_count < 20) {							/* If 20ms No Result, then cal_retry++ */
						/* ============TXIQK Check============== */
						tx_fail = rtl_get_bbreg(hw, 0xd00, BIT(12));

						if (~tx_fail) {
							rtl_write_dword(rtlpriv, 0xcb8, 0x02000000);
							tx_x0[cal] = rtl_get_bbreg(hw, 0xd00, 0x07ff0000)<<21;
							rtl_write_dword(rtlpriv, 0xcb8, 0x04000000);
							tx_y0[cal] = rtl_get_bbreg(hw, 0xd00, 0x07ff0000)<<21;
							tx0iqkok = true;
							break;
						} else {
							rtl_set_bbreg(hw, 0xccc, 0x000007ff, 0x0);
							rtl_set_bbreg(hw, 0xcd4, 0x000007ff, 0x200);
							tx0iqkok = false;
							cal_retry++;
							if (cal_retry == 10)
								break;
						}
					} else {
						tx0iqkok = false;
						cal_retry++;
						if (cal_retry == 10)
							break;
					}
				}
			}

			if (tx0iqkok == false)
				break;				/* TXK fail, Don't do RXK */

			if (vdf_enable == 1) {
				rtl_set_bbreg(hw, 0xce8, BIT(31), 0x0);    /* TX VDF Disable */
				RT_TRACE(rtlpriv, COMP_IQK, DBG_LOUD, "RXVDF Start\n");
				for (k = 0; k <= 2; k++) {
					/* ====== RX mode TXK (RXK Step 1) ====== */
					rtl_set_bbreg(hw, 0x82c, BIT(31), 0x0); /* [31] = 0 --> Page C */
					/* 1. TX RF Setting */
					rtl_set_rfreg(hw, path, 0xef, RFREG_OFFSET_MASK, 0x80000);
					rtl_set_rfreg(hw, path, 0x30, RFREG_OFFSET_MASK, 0x30000);
					rtl_set_rfreg(hw, path, 0x31, RFREG_OFFSET_MASK, 0x00029);
					rtl_set_rfreg(hw, path, 0x32, RFREG_OFFSET_MASK, 0xd7ffb);
					rtl_set_rfreg(hw, path, 0x65, RFREG_OFFSET_MASK, temp_reg65);
					rtl_set_rfreg(hw, path, 0x8f, RFREG_OFFSET_MASK, 0x8a001);
					rtl_set_rfreg(hw, path, 0xef, RFREG_OFFSET_MASK, 0x00000);

					rtl_set_bbreg(hw, 0xcb8, 0xf, 0xd);
					rtl_write_dword(rtlpriv, 0x978, 0x29002000);/* TX (X,Y) */
					rtl_write_dword(rtlpriv, 0x97c, 0xa9002000);/* RX (X,Y) */
					rtl_write_dword(rtlpriv, 0x984, 0x0046a910);/* [0]:AGC_en, [15]:idac_K_Mask */
					rtl_write_dword(rtlpriv, 0x90c, 0x00008000);
					rtl_write_dword(rtlpriv, 0xb00, 0x03000100);
					rtl_set_bbreg(hw, 0x82c, BIT(31), 0x1); /* [31] = 1 --> Page C1 */
					switch (k) {
					case 0:
						{
							rtl_write_dword(rtlpriv, 0xc80, 0x18008c38);/* TX_TONE_idx[9:0], TxK_Mask[29] TX_Tone = 16 */
							rtl_write_dword(rtlpriv, 0xc84, 0x38008c38);/* RX_TONE_idx[9:0], RxK_Mask[29] */
							rtl_set_bbreg(hw, 0xce8, BIT(30), 0x0);
						}
						break;
					case 1:
						{
							rtl_write_dword(rtlpriv, 0xc80, 0x08008c38);/* TX_TONE_idx[9:0], TxK_Mask[29] TX_Tone = 16 */
							rtl_write_dword(rtlpriv, 0xc84, 0x28008c38);/* RX_TONE_idx[9:0], RxK_Mask[29] */
							rtl_set_bbreg(hw, 0xce8, BIT(30), 0x0);
						}
						break;
					case 2:
						{
							RT_TRACE(rtlpriv, COMP_IQK, DBG_LOUD,
							"VDF_Y[1] = %x;;;VDF_Y[0] = %x\n",
							vdf_y[1]>>21 & 0x00007ff, vdf_y[0]>>21 & 0x00007ff);
							RT_TRACE(rtlpriv, COMP_IQK, DBG_LOUD,
							"VDF_X[1] = %x;;;VDF_X[0] = %x\n",
							vdf_x[1]>>21 & 0x00007ff, vdf_x[0]>>21 & 0x00007ff);
							rx_dt[cal] = (vdf_y[1]>>20)-(vdf_y[0]>>20);
							RT_TRACE(rtlpriv, COMP_IQK, DBG_LOUD, "Rx_dt = %d\n", rx_dt[cal]);
							rx_dt[cal] = ((16*rx_dt[cal])*10000/13823);
							rx_dt[cal] = (rx_dt[cal] >> 1)+(rx_dt[cal] & BIT(0));
							rtl_write_dword(rtlpriv, 0xc80, 0x18008c20);/* TX_TONE_idx[9:0], TxK_Mask[29] TX_Tone = 16 */
							rtl_write_dword(rtlpriv, 0xc84, 0x38008c20);/* RX_TONE_idx[9:0], RxK_Mask[29] */
							rtl_set_bbreg(hw, 0xce8, 0x00003fff, rx_dt[cal] & 0x00003fff);
						}
						break;
					default:
						break;
					}
					rtl_write_dword(rtlpriv, 0xc88, 0x821603e0);
					rtl_write_dword(rtlpriv, 0xc8c, 0x68163e96);
					rtl_write_dword(rtlpriv, 0xcb8, 0x00100000);/* cb8[20] \B1N SI/PI \A8ϥ\CE\C5v\A4\C1\B5\B9 iqk_dpk module */
					cal_retry = 0;
					while (1) {
						/* one shot */
						rtl_write_dword(rtlpriv, 0x980, 0xfa000000);
						rtl_write_dword(rtlpriv, 0x980, 0xf8000000);

						mdelay(10); /* Delay 10ms */
						rtl_write_dword(rtlpriv, 0xcb8, 0x00000000);
						delay_count = 0;
						while (1) {
							iqk_ready = rtl_get_bbreg(hw, 0xd00, BIT(10));
							if ((~iqk_ready) || (delay_count > 20))
								break;
							else{
								mdelay(1);
								delay_count++;
							}
						}

						if (delay_count < 20) {							/* If 20ms No Result, then cal_retry++ */
							/* ============TXIQK Check============== */
							tx_fail = rtl_get_bbreg(hw, 0xd00, BIT(12));

							if (~tx_fail) {
								rtl_write_dword(rtlpriv, 0xcb8, 0x02000000);
								tx_x0_rxk[cal] = rtl_get_bbreg(hw, 0xd00, 0x07ff0000)<<21;
								rtl_write_dword(rtlpriv, 0xcb8, 0x04000000);
								tx_y0_rxk[cal] = rtl_get_bbreg(hw, 0xd00, 0x07ff0000)<<21;
								tx0iqkok = true;
								break;
							} else{
								tx0iqkok = false;
								cal_retry++;
								if (cal_retry == 10)
									break;
							}
						} else {
							tx0iqkok = false;
							cal_retry++;
							if (cal_retry == 10)
								break;
						}
					}

					if (tx0iqkok == false) {   /* If RX mode TXK fail, then take TXK Result */
						tx_x0_rxk[cal] = tx_x0[cal];
						tx_y0_rxk[cal] = tx_y0[cal];
						tx0iqkok = true;
						RT_TRACE(rtlpriv,
							 COMP_IQK,
							 DBG_LOUD,
							 "RXK Step 1 fail\n");
					}

					/* ====== RX IQK ====== */
					rtl_set_bbreg(hw, 0x82c, BIT(31), 0x0); /* [31] = 0 --> Page C */
					/* 1. RX RF Setting */
					rtl_set_rfreg(hw, path, 0xef, RFREG_OFFSET_MASK, 0x80000);
					rtl_set_rfreg(hw, path, 0x30, RFREG_OFFSET_MASK, 0x30000);
					rtl_set_rfreg(hw, path, 0x31, RFREG_OFFSET_MASK, 0x0002f);
					rtl_set_rfreg(hw, path, 0x32, RFREG_OFFSET_MASK, 0xfffbb);
					rtl_set_rfreg(hw, path, 0x8f, RFREG_OFFSET_MASK, 0x88001);
					rtl_set_rfreg(hw, path, 0x65, RFREG_OFFSET_MASK, 0x931d8);
					rtl_set_rfreg(hw, path, 0xef, RFREG_OFFSET_MASK, 0x00000);

					rtl_set_bbreg(hw, 0x978, 0x03FF8000, (tx_x0_rxk[cal])>>21&0x000007ff);
					rtl_set_bbreg(hw, 0x978, 0x000007FF, (tx_y0_rxk[cal])>>21&0x000007ff);
					rtl_set_bbreg(hw, 0x978, BIT(31), 0x1);
					rtl_set_bbreg(hw, 0x97c, BIT(31), 0x0);
					rtl_set_bbreg(hw, 0xcb8, 0xF, 0xe);
					rtl_write_dword(rtlpriv, 0x90c, 0x00008000);
					rtl_write_dword(rtlpriv, 0x984, 0x0046a911);

					rtl_set_bbreg(hw, 0x82c, BIT(31), 0x1); /* [31] = 1 --> Page C1 */
					rtl_set_bbreg(hw, 0xc80, BIT(29), 0x1);
					rtl_set_bbreg(hw, 0xc84, BIT(29), 0x0);
					rtl_write_dword(rtlpriv, 0xc88, 0x02140119);

					rtl_write_dword(rtlpriv, 0xc8c, 0x28160d00); /* pDM_Odm->SupportInterface == 1 */

					if (k == 2)
						rtl_set_bbreg(hw, 0xce8, BIT(30), 0x1);  /* RX VDF Enable */
					rtl_write_dword(rtlpriv, 0xcb8, 0x00100000);/* cb8[20] \B1N SI/PI \A8ϥ\CE\C5v\A4\C1\B5\B9 iqk_dpk module */

					cal_retry = 0;
					while (1) {
						/* one shot */
						rtl_write_dword(rtlpriv, 0x980, 0xfa000000);
						rtl_write_dword(rtlpriv, 0x980, 0xf8000000);

						mdelay(10); /* Delay 10ms */
						rtl_write_dword(rtlpriv, 0xcb8, 0x00000000);
						delay_count = 0;
						while (1) {
							iqk_ready = rtl_get_bbreg(hw, 0xd00, BIT(10));
							if ((~iqk_ready) || (delay_count > 20))
								break;
							else{
								mdelay(1);
								delay_count++;
							}
						}

						if (delay_count < 20) {	/* If 20ms No Result, then cal_retry++ */
							/* ============RXIQK Check============== */
							rx_fail = rtl_get_bbreg(hw, 0xd00, BIT(11));
							if (rx_fail == 0) {
								rtl_write_dword(rtlpriv, 0xcb8, 0x06000000);
								vdf_x[k] = rtl_get_bbreg(hw, 0xd00, 0x07ff0000)<<21;
								rtl_write_dword(rtlpriv, 0xcb8, 0x08000000);
								vdf_y[k] = rtl_get_bbreg(hw, 0xd00, 0x07ff0000)<<21;
								rx0iqkok = true;
								break;
							} else {
								rtl_set_bbreg(hw, 0xc10, 0x000003ff, 0x200>>1);
								rtl_set_bbreg(hw, 0xc10, 0x03ff0000, 0x0>>1);
								rx0iqkok = false;
								cal_retry++;
								if (cal_retry == 10)
									break;

							}
						} else{
							rx0iqkok = false;
							cal_retry++;
							if (cal_retry == 10)
								break;
						}
					}

				}
				if (k == 3) {
					rx_x0[cal] = vdf_x[k-1];
					rx_y0[cal] = vdf_y[k-1];
				}
				rtl_set_bbreg(hw, 0xce8, BIT(31), 0x1);    /* TX VDF Enable */
			}

			else{
				/* ====== RX mode TXK (RXK Step 1) ====== */
				rtl_set_bbreg(hw, 0x82c, BIT(31), 0x0); /* [31] = 0 --> Page C */
				/* 1. TX RF Setting */
				rtl_set_rfreg(hw, path, 0xef, RFREG_OFFSET_MASK, 0x80000);
				rtl_set_rfreg(hw, path, 0x30, RFREG_OFFSET_MASK, 0x30000);
				rtl_set_rfreg(hw, path, 0x31, RFREG_OFFSET_MASK, 0x00029);
				rtl_set_rfreg(hw, path, 0x32, RFREG_OFFSET_MASK, 0xd7ffb);
				rtl_set_rfreg(hw, path, 0x65, RFREG_OFFSET_MASK, temp_reg65);
				rtl_set_rfreg(hw, path, 0x8f, RFREG_OFFSET_MASK, 0x8a001);
				rtl_set_rfreg(hw, path, 0xef, RFREG_OFFSET_MASK, 0x00000);
				rtl_write_dword(rtlpriv, 0x90c, 0x00008000);
				rtl_write_dword(rtlpriv, 0xb00, 0x03000100);
				rtl_write_dword(rtlpriv, 0x984, 0x0046a910);/* [0]:AGC_en, [15]:idac_K_Mask */

				rtl_set_bbreg(hw, 0x82c, BIT(31), 0x1); /* [31] = 1 --> Page C1 */
				rtl_write_dword(rtlpriv, 0xc80, 0x18008c10);/* TX_TONE_idx[9:0], TxK_Mask[29] TX_Tone = 16 */
				rtl_write_dword(rtlpriv, 0xc84, 0x38008c10);/* RX_TONE_idx[9:0], RxK_Mask[29] */
				rtl_write_dword(rtlpriv, 0xc88, 0x821603e0);
				/* ODM_Write4Byte(pDM_Odm, 0xc8c, 0x68163e96); */
				rtl_write_dword(rtlpriv, 0xcb8, 0x00100000);/* cb8[20] \B1N SI/PI \A8ϥ\CE\C5v\A4\C1\B5\B9 iqk_dpk module */
				cal_retry = 0;
				while (1) {
					/* one shot */
					rtl_write_dword(rtlpriv, 0x980, 0xfa000000);
					rtl_write_dword(rtlpriv, 0x980, 0xf8000000);

					mdelay(10); /* Delay 10ms */
					rtl_write_dword(rtlpriv, 0xcb8, 0x00000000);
					delay_count = 0;
					while (1) {
						iqk_ready = rtl_get_bbreg(hw, 0xd00, BIT(10));
						if ((~iqk_ready) || (delay_count > 20))
							break;
						else{
							mdelay(1);
							delay_count++;
						}
					}

					if (delay_count < 20) {							/* If 20ms No Result, then cal_retry++ */
						/* ============TXIQK Check============== */
						tx_fail = rtl_get_bbreg(hw, 0xd00, BIT(12));

						if (~tx_fail) {
							rtl_write_dword(rtlpriv, 0xcb8, 0x02000000);
							tx_x0_rxk[cal] = rtl_get_bbreg(hw, 0xd00, 0x07ff0000)<<21;
							rtl_write_dword(rtlpriv, 0xcb8, 0x04000000);
							tx_y0_rxk[cal] = rtl_get_bbreg(hw, 0xd00, 0x07ff0000)<<21;
							tx0iqkok = true;
							break;
						} else {
							tx0iqkok = false;
							cal_retry++;
							if (cal_retry == 10)
								break;
						}
					} else{
						tx0iqkok = false;
						cal_retry++;
						if (cal_retry == 10)
							break;
					}
				}

				if (tx0iqkok == false) {   /* If RX mode TXK fail, then take TXK Result */
					tx_x0_rxk[cal] = tx_x0[cal];
					tx_y0_rxk[cal] = tx_y0[cal];
					tx0iqkok = true;
					RT_TRACE(rtlpriv, COMP_IQK,
						 DBG_LOUD, "1");
				}

				/* ====== RX IQK ====== */
				rtl_set_bbreg(hw, 0x82c, BIT(31), 0x0); /* [31] = 0 --> Page C */
				/* 1. RX RF Setting */
				rtl_set_rfreg(hw, path, 0xef, RFREG_OFFSET_MASK, 0x80000);
				rtl_set_rfreg(hw, path, 0x30, RFREG_OFFSET_MASK, 0x30000);
				rtl_set_rfreg(hw, path, 0x31, RFREG_OFFSET_MASK, 0x0002f);
				rtl_set_rfreg(hw, path, 0x32, RFREG_OFFSET_MASK, 0xfffbb);
				rtl_set_rfreg(hw, path, 0x8f, RFREG_OFFSET_MASK, 0x88001);
				rtl_set_rfreg(hw, path, 0x65, RFREG_OFFSET_MASK, 0x931d8);
				rtl_set_rfreg(hw, path, 0xef, RFREG_OFFSET_MASK, 0x00000);

				rtl_set_bbreg(hw, 0x978, 0x03FF8000, (tx_x0_rxk[cal])>>21&0x000007ff);
				rtl_set_bbreg(hw, 0x978, 0x000007FF, (tx_y0_rxk[cal])>>21&0x000007ff);
				rtl_set_bbreg(hw, 0x978, BIT(31), 0x1);
				rtl_set_bbreg(hw, 0x97c, BIT(31), 0x0);
				/* ODM_SetBBReg(pDM_Odm, 0xcb8, 0xF, 0xe); */
				rtl_write_dword(rtlpriv, 0x90c, 0x00008000);
				rtl_write_dword(rtlpriv, 0x984, 0x0046a911);

				rtl_set_bbreg(hw, 0x82c, BIT(31), 0x1); /* [31] = 1 --> Page C1 */
				rtl_write_dword(rtlpriv, 0xc80, 0x38008c10);/* TX_TONE_idx[9:0], TxK_Mask[29] TX_Tone = 16 */
				rtl_write_dword(rtlpriv, 0xc84, 0x18008c10);/* RX_TONE_idx[9:0], RxK_Mask[29] */
				rtl_write_dword(rtlpriv, 0xc88, 0x02140119);

				rtl_write_dword(rtlpriv, 0xc8c, 0x28160d00); /*pDM_Odm->SupportInterface == 1*/

				rtl_write_dword(rtlpriv, 0xcb8, 0x00100000);/* cb8[20] \B1N SI/PI \A8ϥ\CE\C5v\A4\C1\B5\B9 iqk_dpk module */

				cal_retry = 0;
				while (1) {
					/* one shot */
					rtl_write_dword(rtlpriv, 0x980, 0xfa000000);
					rtl_write_dword(rtlpriv, 0x980, 0xf8000000);

					mdelay(10); /* Delay 10ms */
					rtl_write_dword(rtlpriv, 0xcb8, 0x00000000);
					delay_count = 0;
					while (1) {
						iqk_ready = rtl_get_bbreg(hw, 0xd00, BIT(10));
						if ((~iqk_ready) || (delay_count > 20))
							break;
						else{
							mdelay(1);
							delay_count++;
						}
					}

					if (delay_count < 20) {	/* If 20ms No Result, then cal_retry++ */
						/* ============RXIQK Check============== */
						rx_fail = rtl_get_bbreg(hw, 0xd00, BIT(11));
						if (rx_fail == 0) {
							rtl_write_dword(rtlpriv, 0xcb8, 0x06000000);
							rx_x0[cal] = rtl_get_bbreg(hw, 0xd00, 0x07ff0000)<<21;
							rtl_write_dword(rtlpriv, 0xcb8, 0x08000000);
							rx_y0[cal] = rtl_get_bbreg(hw, 0xd00, 0x07ff0000)<<21;
							rx0iqkok = true;
							break;
						} else{
							rtl_set_bbreg(hw, 0xc10, 0x000003ff, 0x200>>1);
							rtl_set_bbreg(hw, 0xc10, 0x03ff0000, 0x0>>1);
							rx0iqkok = false;
							cal_retry++;
							if (cal_retry == 10)
								break;

						}
					} else{
						rx0iqkok = false;
						cal_retry++;
						if (cal_retry == 10)
							break;
					}
				}
			}

			if (tx0iqkok)
				tx_average++;
			if (rx0iqkok)
				rx_average++;
			rtl_set_bbreg(hw, 0x82c, BIT(31), 0x0); /* [31] = 0 --> Page C */
			rtl_set_rfreg(hw, path, 0x65, RFREG_OFFSET_MASK, temp_reg65);
			break;
		default:
			break;
		}
		cal++;
	}

	/* FillIQK Result */
	switch (path) {
	case RF90_PATH_A:
		RT_TRACE(rtlpriv, COMP_IQK, DBG_LOUD,
			 "========Path_A =======\n");
		if (tx_average == 0)
			break;

		for (i = 0; i < tx_average; i++) {
			RT_TRACE(rtlpriv, COMP_IQK, DBG_LOUD,
				 "TX_X0_RXK[%d] = %x ;; TX_Y0_RXK[%d] = %x\n", i,
				 (tx_x0_rxk[i])>>21&0x000007ff, i,
				 (tx_y0_rxk[i])>>21&0x000007ff);
			RT_TRACE(rtlpriv, COMP_IQK, DBG_LOUD,
				 "TX_X0[%d] = %x ;; TX_Y0[%d] = %x\n", i,
				 (tx_x0[i])>>21&0x000007ff, i,
				 (tx_y0[i])>>21&0x000007ff);
		}
		for (i = 0; i < tx_average; i++) {
			for (ii = i+1; ii < tx_average; ii++) {
				dx = (tx_x0[i]>>21) - (tx_x0[ii]>>21);
				if (dx < 3 && dx > -3) {
					dy = (tx_y0[i]>>21) - (tx_y0[ii]>>21);
					if (dy < 3 && dy > -3) {
						tx_x = ((tx_x0[i]>>21) + (tx_x0[ii]>>21))/2;
						tx_y = ((tx_y0[i]>>21) + (tx_y0[ii]>>21))/2;
						tx_finish = 1;
						break;
					}
				}
			}
			if (tx_finish == 1)
				break;
		}

		if (tx_finish == 1)
			_rtl8821ae_iqk_tx_fill_iqc(hw, path, tx_x, tx_y); /* ? */
		else
			_rtl8821ae_iqk_tx_fill_iqc(hw, path, 0x200, 0x0);

		if (rx_average == 0)
			break;

		for (i = 0; i < rx_average; i++)
			RT_TRACE(rtlpriv, COMP_IQK, DBG_LOUD,
				"RX_X0[%d] = %x ;; RX_Y0[%d] = %x\n", i,
				(rx_x0[i])>>21&0x000007ff, i,
				(rx_y0[i])>>21&0x000007ff);
		for (i = 0; i < rx_average; i++) {
			for (ii = i+1; ii < rx_average; ii++) {
				dx = (rx_x0[i]>>21) - (rx_x0[ii]>>21);
				if (dx < 4 && dx > -4) {
					dy = (rx_y0[i]>>21) - (rx_y0[ii]>>21);
					if (dy < 4 && dy > -4) {
						rx_x = ((rx_x0[i]>>21) + (rx_x0[ii]>>21))/2;
						rx_y = ((rx_y0[i]>>21) + (rx_y0[ii]>>21))/2;
						rx_finish = 1;
						break;
					}
				}
			}
			if (rx_finish == 1)
				break;
		}

		if (rx_finish == 1)
			_rtl8821ae_iqk_rx_fill_iqc(hw, path, rx_x, rx_y);
		else
			_rtl8821ae_iqk_rx_fill_iqc(hw, path, 0x200, 0x0);
		break;
	default:
		break;
	}
}

static void _rtl8821ae_iqk_restore_rf(struct ieee80211_hw *hw,
				      enum radio_path path,
				      u32 *backup_rf_reg,
				      u32 *rf_backup, u32 rf_reg_num)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 i;

	rtl_set_bbreg(hw, 0x82c, BIT(31), 0x0); /* [31] = 0 --> Page C */
	for (i = 0; i < RF_REG_NUM; i++)
		rtl_set_rfreg(hw, path, backup_rf_reg[i], RFREG_OFFSET_MASK,
			      rf_backup[i]);

	switch (path) {
	case RF90_PATH_A:
		RT_TRACE(rtlpriv, COMP_IQK, DBG_LOUD,
			 "RestoreRF Path A Success!!!!\n");
		break;
	default:
			break;
	}
}

static void _rtl8821ae_iqk_restore_afe(struct ieee80211_hw *hw,
				       u32 *afe_backup, u32 *backup_afe_reg,
				       u32 afe_num)
{
	u32 i;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_set_bbreg(hw, 0x82c, BIT(31), 0x0); /* [31] = 0 --> Page C */
	/* Reload AFE Parameters */
	for (i = 0; i < afe_num; i++)
		rtl_write_dword(rtlpriv, backup_afe_reg[i], afe_backup[i]);
	rtl_set_bbreg(hw, 0x82c, BIT(31), 0x1); /* [31] = 1 --> Page C1 */
	rtl_write_dword(rtlpriv, 0xc80, 0x0);
	rtl_write_dword(rtlpriv, 0xc84, 0x0);
	rtl_write_dword(rtlpriv, 0xc88, 0x0);
	rtl_write_dword(rtlpriv, 0xc8c, 0x3c000000);
	rtl_write_dword(rtlpriv, 0xc90, 0x00000080);
	rtl_write_dword(rtlpriv, 0xc94, 0x00000000);
	rtl_write_dword(rtlpriv, 0xcc4, 0x20040000);
	rtl_write_dword(rtlpriv, 0xcc8, 0x20000000);
	rtl_write_dword(rtlpriv, 0xcb8, 0x0);
	RT_TRACE(rtlpriv, COMP_IQK, DBG_LOUD, "RestoreAFE Success!!!!\n");
}

static void _rtl8821ae_iqk_restore_macbb(struct ieee80211_hw *hw,
					 u32 *macbb_backup,
					 u32 *backup_macbb_reg,
					 u32 macbb_num)
{
	u32 i;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_set_bbreg(hw, 0x82c, BIT(31), 0x0); /* [31] = 0 --> Page C */
	/* Reload MacBB Parameters */
	for (i = 0; i < macbb_num; i++)
		rtl_write_dword(rtlpriv, backup_macbb_reg[i], macbb_backup[i]);
	RT_TRACE(rtlpriv, COMP_IQK, DBG_LOUD, "RestoreMacBB Success!!!!\n");
}

#undef MACBB_REG_NUM
#undef AFE_REG_NUM
#undef RF_REG_NUM

#define MACBB_REG_NUM 11
#define AFE_REG_NUM 12
#define RF_REG_NUM 3

static void _rtl8821ae_phy_iq_calibrate(struct ieee80211_hw *hw)
{
	u32	macbb_backup[MACBB_REG_NUM];
	u32 afe_backup[AFE_REG_NUM];
	u32 rfa_backup[RF_REG_NUM];
	u32 rfb_backup[RF_REG_NUM];
	u32 backup_macbb_reg[MACBB_REG_NUM] = {
		0xb00, 0x520, 0x550, 0x808, 0x90c, 0xc00, 0xc50,
		0xe00, 0xe50, 0x838, 0x82c
	};
	u32 backup_afe_reg[AFE_REG_NUM] = {
		0xc5c, 0xc60, 0xc64, 0xc68, 0xc6c, 0xc70, 0xc74,
		0xc78, 0xc7c, 0xc80, 0xc84, 0xcb8
	};
	u32	backup_rf_reg[RF_REG_NUM] = {0x65, 0x8f, 0x0};

	_rtl8821ae_iqk_backup_macbb(hw, macbb_backup, backup_macbb_reg,
				    MACBB_REG_NUM);
	_rtl8821ae_iqk_backup_afe(hw, afe_backup, backup_afe_reg, AFE_REG_NUM);
	_rtl8821ae_iqk_backup_rf(hw, rfa_backup, rfb_backup, backup_rf_reg,
				 RF_REG_NUM);

	_rtl8821ae_iqk_configure_mac(hw);
	_rtl8821ae_iqk_tx(hw, RF90_PATH_A);
	_rtl8821ae_iqk_restore_rf(hw, RF90_PATH_A, backup_rf_reg, rfa_backup,
				  RF_REG_NUM);

	_rtl8821ae_iqk_restore_afe(hw, afe_backup, backup_afe_reg, AFE_REG_NUM);
	_rtl8821ae_iqk_restore_macbb(hw, macbb_backup, backup_macbb_reg,
				     MACBB_REG_NUM);
}

static void _rtl8821ae_phy_set_rfpath_switch(struct ieee80211_hw *hw, bool main)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	/* struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw)); */
	/* struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw)); */
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "\n");

	if (main)
		rtl_set_bbreg(hw, RA_RFE_PINMUX + 4, BIT(29) | BIT(28), 0x1);
	else
		rtl_set_bbreg(hw, RA_RFE_PINMUX + 4, BIT(29) | BIT(28), 0x2);
}

#undef IQK_ADDA_REG_NUM
#undef IQK_DELAY_TIME

void rtl8812ae_phy_iq_calibrate(struct ieee80211_hw *hw, bool b_recovery)
{
}

void rtl8812ae_do_iqk(struct ieee80211_hw *hw, u8 delta_thermal_index,
		      u8 thermal_value, u8 threshold)
{
	struct rtl_dm	*rtldm = rtl_dm(rtl_priv(hw));

	rtldm->thermalvalue_iqk = thermal_value;
	rtl8812ae_phy_iq_calibrate(hw, false);
}

void rtl8821ae_phy_iq_calibrate(struct ieee80211_hw *hw, bool b_recovery)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;

	if (!rtlphy->lck_inprogress) {
		spin_lock(&rtlpriv->locks.iqk_lock);
		rtlphy->lck_inprogress = true;
		spin_unlock(&rtlpriv->locks.iqk_lock);

		_rtl8821ae_phy_iq_calibrate(hw);

		spin_lock(&rtlpriv->locks.iqk_lock);
		rtlphy->lck_inprogress = false;
		spin_unlock(&rtlpriv->locks.iqk_lock);
	}
}

void rtl8821ae_reset_iqk_result(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 i;

	RT_TRACE(rtlpriv, COMP_IQK, DBG_LOUD,
		 "rtl8812ae_dm_reset_iqk_result:: settings regs %d default regs %d\n",
		 (int)(sizeof(rtlphy->iqk_matrix) /
		 sizeof(struct iqk_matrix_regs)),
		 IQK_MATRIX_SETTINGS_NUM);

	for (i = 0; i < IQK_MATRIX_SETTINGS_NUM; i++) {
		rtlphy->iqk_matrix[i].value[0][0] = 0x100;
		rtlphy->iqk_matrix[i].value[0][2] = 0x100;
		rtlphy->iqk_matrix[i].value[0][4] = 0x100;
		rtlphy->iqk_matrix[i].value[0][6] = 0x100;

		rtlphy->iqk_matrix[i].value[0][1] = 0x0;
		rtlphy->iqk_matrix[i].value[0][3] = 0x0;
		rtlphy->iqk_matrix[i].value[0][5] = 0x0;
		rtlphy->iqk_matrix[i].value[0][7] = 0x0;

		rtlphy->iqk_matrix[i].iqk_done = false;
	}
}

void rtl8821ae_do_iqk(struct ieee80211_hw *hw, u8 delta_thermal_index,
		      u8 thermal_value, u8 threshold)
{
	struct rtl_dm	*rtldm = rtl_dm(rtl_priv(hw));

	rtl8821ae_reset_iqk_result(hw);

	rtldm->thermalvalue_iqk = thermal_value;
	rtl8821ae_phy_iq_calibrate(hw, false);
}

void rtl8821ae_phy_lc_calibrate(struct ieee80211_hw *hw)
{
}

void rtl8821ae_phy_ap_calibrate(struct ieee80211_hw *hw, char delta)
{
}

void rtl8821ae_phy_set_rfpath_switch(struct ieee80211_hw *hw, bool bmain)
{
	_rtl8821ae_phy_set_rfpath_switch(hw, bmain);
}

bool rtl8821ae_phy_set_io_cmd(struct ieee80211_hw *hw, enum io_type iotype)
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
		case IO_CMD_PAUSE_BAND1_DM_BY_SCAN:
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
	rtl8821ae_phy_set_io(hw);
	RT_TRACE(rtlpriv, COMP_CMD, DBG_TRACE, "IO Type(%#x)\n", iotype);
	return true;
}

static void rtl8821ae_phy_set_io(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct dig_t *dm_digtable = &rtlpriv->dm_digtable;
	struct rtl_phy *rtlphy = &rtlpriv->phy;

	RT_TRACE(rtlpriv, COMP_CMD, DBG_TRACE,
		 "--->Cmd(%#x), set_io_inprogress(%d)\n",
		  rtlphy->current_io_type, rtlphy->set_io_inprogress);
	switch (rtlphy->current_io_type) {
	case IO_CMD_RESUME_DM_BY_SCAN:
		if (rtlpriv->mac80211.opmode == NL80211_IFTYPE_ADHOC)
			_rtl8821ae_resume_tx_beacon(hw);
		rtl8821ae_dm_write_dig(hw, rtlphy->initgain_backup.xaagccore1);
		rtl8821ae_dm_write_cck_cca_thres(hw,
						 rtlphy->initgain_backup.cca);
		break;
	case IO_CMD_PAUSE_BAND0_DM_BY_SCAN:
		if (rtlpriv->mac80211.opmode == NL80211_IFTYPE_ADHOC)
			_rtl8821ae_stop_tx_beacon(hw);
		rtlphy->initgain_backup.xaagccore1 = dm_digtable->cur_igvalue;
		rtl8821ae_dm_write_dig(hw, 0x17);
		rtlphy->initgain_backup.cca = dm_digtable->cur_cck_cca_thres;
		rtl8821ae_dm_write_cck_cca_thres(hw, 0x40);
		break;
	case IO_CMD_PAUSE_BAND1_DM_BY_SCAN:
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

static void rtl8821ae_phy_set_rf_on(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_write_byte(rtlpriv, REG_SPS0_CTRL, 0x2b);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE3);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE2);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE3);
	rtl_write_byte(rtlpriv, REG_TXPAUSE, 0x00);
}

static bool _rtl8821ae_phy_set_rf_power_state(struct ieee80211_hw *hw,
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
			bool rtstatus = false;
			u32 initializecount = 0;

			do {
				initializecount++;
				RT_TRACE(rtlpriv, COMP_RF, DBG_DMESG,
					 "IPS Set eRf nic enable\n");
				rtstatus = rtl_ps_enable_nic(hw);
			} while (!rtstatus && (initializecount < 10));
			RT_CLEAR_PS_LEVEL(ppsc,
					  RT_RF_OFF_LEVL_HALT_NIC);
		} else {
			RT_TRACE(rtlpriv, COMP_RF, DBG_DMESG,
				 "Set ERFON sleeped:%d ms\n",
				  jiffies_to_msecs(jiffies -
						   ppsc->
						   last_sleep_jiffies));
			ppsc->last_awake_jiffies = jiffies;
			rtl8821ae_phy_set_rf_on(hw);
		}
		if (mac->link_state == MAC80211_LINKED) {
			rtlpriv->cfg->ops->led_control(hw,
						       LED_CTL_LINK);
		} else {
			rtlpriv->cfg->ops->led_control(hw,
						       LED_CTL_NO_LINK);
		}
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

bool rtl8821ae_phy_set_rf_power_state(struct ieee80211_hw *hw,
				      enum rf_pwrstate rfpwr_state)
{
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));

	bool bresult = false;

	if (rfpwr_state == ppsc->rfpwr_state)
		return bresult;
	bresult = _rtl8821ae_phy_set_rf_power_state(hw, rfpwr_state);
	return bresult;
}
