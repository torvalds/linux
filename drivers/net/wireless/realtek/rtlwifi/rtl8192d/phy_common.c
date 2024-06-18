// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#include "../wifi.h"
#include "../core.h"
#include "def.h"
#include "reg.h"
#include "dm_common.h"
#include "phy_common.h"
#include "rf_common.h"

static const u8 channel_all[59] = {
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
	36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58,
	60, 62, 64, 100, 102, 104, 106, 108, 110, 112,
	114, 116, 118, 120, 122, 124, 126, 128,	130,
	132, 134, 136, 138, 140, 149, 151, 153, 155,
	157, 159, 161, 163, 165
};

static u32 _rtl92d_phy_rf_serial_read(struct ieee80211_hw *hw,
				      enum radio_path rfpath, u32 offset)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct bb_reg_def *pphyreg = &rtlphy->phyreg_def[rfpath];
	u32 newoffset;
	u32 tmplong, tmplong2;
	u8 rfpi_enable = 0;
	u32 retvalue;

	newoffset = offset;
	tmplong = rtl_get_bbreg(hw, RFPGA0_XA_HSSIPARAMETER2, MASKDWORD);
	if (rfpath == RF90_PATH_A)
		tmplong2 = tmplong;
	else
		tmplong2 = rtl_get_bbreg(hw, pphyreg->rfhssi_para2, MASKDWORD);
	tmplong2 = (tmplong2 & (~BLSSIREADADDRESS)) |
		(newoffset << 23) | BLSSIREADEDGE;
	rtl_set_bbreg(hw, RFPGA0_XA_HSSIPARAMETER2, MASKDWORD,
		      tmplong & (~BLSSIREADEDGE));
	udelay(10);
	rtl_set_bbreg(hw, pphyreg->rfhssi_para2, MASKDWORD, tmplong2);
	udelay(100);
	rtl_set_bbreg(hw, RFPGA0_XA_HSSIPARAMETER2, MASKDWORD,
		      tmplong | BLSSIREADEDGE);
	udelay(10);
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
	rtl_dbg(rtlpriv, COMP_RF, DBG_TRACE, "RFR-%d Addr[0x%x] = 0x%x\n",
		rfpath, pphyreg->rf_rb, retvalue);
	return retvalue;
}

static void _rtl92d_phy_rf_serial_write(struct ieee80211_hw *hw,
					enum radio_path rfpath,
					u32 offset, u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct bb_reg_def *pphyreg = &rtlphy->phyreg_def[rfpath];
	u32 data_and_addr;
	u32 newoffset;

	newoffset = offset;
	/* T65 RF */
	data_and_addr = ((newoffset << 20) | (data & 0x000fffff)) & 0x0fffffff;
	rtl_set_bbreg(hw, pphyreg->rf3wire_offset, MASKDWORD, data_and_addr);
	rtl_dbg(rtlpriv, COMP_RF, DBG_TRACE, "RFW-%d Addr[0x%x]=0x%x\n",
		rfpath, pphyreg->rf3wire_offset, data_and_addr);
}

u32 rtl92d_phy_query_rf_reg(struct ieee80211_hw *hw, enum radio_path rfpath,
			    u32 regaddr, u32 bitmask)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 original_value, readback_value, bitshift;

	rtl_dbg(rtlpriv, COMP_RF, DBG_TRACE,
		"regaddr(%#x), rfpath(%#x), bitmask(%#x)\n",
		regaddr, rfpath, bitmask);
	rtl92d_pci_lock(rtlpriv);
	original_value = _rtl92d_phy_rf_serial_read(hw, rfpath, regaddr);
	bitshift = calculate_bit_shift(bitmask);
	readback_value = (original_value & bitmask) >> bitshift;
	rtl92d_pci_unlock(rtlpriv);
	rtl_dbg(rtlpriv, COMP_RF, DBG_TRACE,
		"regaddr(%#x), rfpath(%#x), bitmask(%#x), original_value(%#x)\n",
		regaddr, rfpath, bitmask, original_value);
	return readback_value;
}
EXPORT_SYMBOL_GPL(rtl92d_phy_query_rf_reg);

void rtl92d_phy_set_rf_reg(struct ieee80211_hw *hw, enum radio_path rfpath,
			   u32 regaddr, u32 bitmask, u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u32 original_value, bitshift;

	rtl_dbg(rtlpriv, COMP_RF, DBG_TRACE,
		"regaddr(%#x), bitmask(%#x), data(%#x), rfpath(%#x)\n",
		regaddr, bitmask, data, rfpath);
	if (bitmask == 0)
		return;
	rtl92d_pci_lock(rtlpriv);
	if (rtlphy->rf_mode != RF_OP_BY_FW) {
		if (bitmask != RFREG_OFFSET_MASK) {
			original_value = _rtl92d_phy_rf_serial_read(hw,
								    rfpath,
								    regaddr);
			bitshift = calculate_bit_shift(bitmask);
			data = ((original_value & (~bitmask)) |
				(data << bitshift));
		}
		_rtl92d_phy_rf_serial_write(hw, rfpath, regaddr, data);
	}
	rtl92d_pci_unlock(rtlpriv);
	rtl_dbg(rtlpriv, COMP_RF, DBG_TRACE,
		"regaddr(%#x), bitmask(%#x), data(%#x), rfpath(%#x)\n",
		regaddr, bitmask, data, rfpath);
}
EXPORT_SYMBOL_GPL(rtl92d_phy_set_rf_reg);

void rtl92d_phy_init_bb_rf_register_definition(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;

	/* RF Interface Sowrtware Control */
	/* 16 LSBs if read 32-bit from 0x870 */
	rtlphy->phyreg_def[RF90_PATH_A].rfintfs = RFPGA0_XAB_RFINTERFACESW;
	/* 16 MSBs if read 32-bit from 0x870 (16-bit for 0x872) */
	rtlphy->phyreg_def[RF90_PATH_B].rfintfs = RFPGA0_XAB_RFINTERFACESW;
	/* 16 LSBs if read 32-bit from 0x874 */
	rtlphy->phyreg_def[RF90_PATH_C].rfintfs = RFPGA0_XCD_RFINTERFACESW;
	/* 16 MSBs if read 32-bit from 0x874 (16-bit for 0x876) */

	rtlphy->phyreg_def[RF90_PATH_D].rfintfs = RFPGA0_XCD_RFINTERFACESW;
	/* RF Interface Readback Value */
	/* 16 LSBs if read 32-bit from 0x8E0 */
	rtlphy->phyreg_def[RF90_PATH_A].rfintfi = RFPGA0_XAB_RFINTERFACERB;
	/* 16 MSBs if read 32-bit from 0x8E0 (16-bit for 0x8E2) */
	rtlphy->phyreg_def[RF90_PATH_B].rfintfi = RFPGA0_XAB_RFINTERFACERB;
	/* 16 LSBs if read 32-bit from 0x8E4 */
	rtlphy->phyreg_def[RF90_PATH_C].rfintfi = RFPGA0_XCD_RFINTERFACERB;
	/* 16 MSBs if read 32-bit from 0x8E4 (16-bit for 0x8E6) */
	rtlphy->phyreg_def[RF90_PATH_D].rfintfi = RFPGA0_XCD_RFINTERFACERB;

	/* RF Interface Output (and Enable) */
	/* 16 LSBs if read 32-bit from 0x860 */
	rtlphy->phyreg_def[RF90_PATH_A].rfintfo = RFPGA0_XA_RFINTERFACEOE;
	/* 16 LSBs if read 32-bit from 0x864 */
	rtlphy->phyreg_def[RF90_PATH_B].rfintfo = RFPGA0_XB_RFINTERFACEOE;

	/* RF Interface (Output and)  Enable */
	/* 16 MSBs if read 32-bit from 0x860 (16-bit for 0x862) */
	rtlphy->phyreg_def[RF90_PATH_A].rfintfe = RFPGA0_XA_RFINTERFACEOE;
	/* 16 MSBs if read 32-bit from 0x864 (16-bit for 0x866) */
	rtlphy->phyreg_def[RF90_PATH_B].rfintfe = RFPGA0_XB_RFINTERFACEOE;

	/* Addr of LSSI. Write RF register by driver */
	/* LSSI Parameter */
	rtlphy->phyreg_def[RF90_PATH_A].rf3wire_offset =
				 RFPGA0_XA_LSSIPARAMETER;
	rtlphy->phyreg_def[RF90_PATH_B].rf3wire_offset =
				 RFPGA0_XB_LSSIPARAMETER;

	/* RF parameter */
	/* BB Band Select */
	rtlphy->phyreg_def[RF90_PATH_A].rflssi_select = RFPGA0_XAB_RFPARAMETER;
	rtlphy->phyreg_def[RF90_PATH_B].rflssi_select = RFPGA0_XAB_RFPARAMETER;
	rtlphy->phyreg_def[RF90_PATH_C].rflssi_select = RFPGA0_XCD_RFPARAMETER;
	rtlphy->phyreg_def[RF90_PATH_D].rflssi_select = RFPGA0_XCD_RFPARAMETER;

	/* Tx AGC Gain Stage (same for all path. Should we remove this?) */
	/* Tx gain stage */
	rtlphy->phyreg_def[RF90_PATH_A].rftxgain_stage = RFPGA0_TXGAINSTAGE;
	/* Tx gain stage */
	rtlphy->phyreg_def[RF90_PATH_B].rftxgain_stage = RFPGA0_TXGAINSTAGE;
	/* Tx gain stage */
	rtlphy->phyreg_def[RF90_PATH_C].rftxgain_stage = RFPGA0_TXGAINSTAGE;
	/* Tx gain stage */
	rtlphy->phyreg_def[RF90_PATH_D].rftxgain_stage = RFPGA0_TXGAINSTAGE;

	/* Transceiver A~D HSSI Parameter-1 */
	/* wire control parameter1 */
	rtlphy->phyreg_def[RF90_PATH_A].rfhssi_para1 = RFPGA0_XA_HSSIPARAMETER1;
	/* wire control parameter1 */
	rtlphy->phyreg_def[RF90_PATH_B].rfhssi_para1 = RFPGA0_XB_HSSIPARAMETER1;

	/* Transceiver A~D HSSI Parameter-2 */
	/* wire control parameter2 */
	rtlphy->phyreg_def[RF90_PATH_A].rfhssi_para2 = RFPGA0_XA_HSSIPARAMETER2;
	/* wire control parameter2 */
	rtlphy->phyreg_def[RF90_PATH_B].rfhssi_para2 = RFPGA0_XB_HSSIPARAMETER2;

	/* RF switch Control */
	/* TR/Ant switch control */
	rtlphy->phyreg_def[RF90_PATH_A].rfsw_ctrl = RFPGA0_XAB_SWITCHCONTROL;
	rtlphy->phyreg_def[RF90_PATH_B].rfsw_ctrl = RFPGA0_XAB_SWITCHCONTROL;
	rtlphy->phyreg_def[RF90_PATH_C].rfsw_ctrl = RFPGA0_XCD_SWITCHCONTROL;
	rtlphy->phyreg_def[RF90_PATH_D].rfsw_ctrl = RFPGA0_XCD_SWITCHCONTROL;

	/* AGC control 1 */
	rtlphy->phyreg_def[RF90_PATH_A].rfagc_control1 = ROFDM0_XAAGCCORE1;
	rtlphy->phyreg_def[RF90_PATH_B].rfagc_control1 = ROFDM0_XBAGCCORE1;
	rtlphy->phyreg_def[RF90_PATH_C].rfagc_control1 = ROFDM0_XCAGCCORE1;
	rtlphy->phyreg_def[RF90_PATH_D].rfagc_control1 = ROFDM0_XDAGCCORE1;

	/* AGC control 2  */
	rtlphy->phyreg_def[RF90_PATH_A].rfagc_control2 = ROFDM0_XAAGCCORE2;
	rtlphy->phyreg_def[RF90_PATH_B].rfagc_control2 = ROFDM0_XBAGCCORE2;
	rtlphy->phyreg_def[RF90_PATH_C].rfagc_control2 = ROFDM0_XCAGCCORE2;
	rtlphy->phyreg_def[RF90_PATH_D].rfagc_control2 = ROFDM0_XDAGCCORE2;

	/* RX AFE control 1 */
	rtlphy->phyreg_def[RF90_PATH_A].rfrxiq_imbal = ROFDM0_XARXIQIMBALANCE;
	rtlphy->phyreg_def[RF90_PATH_B].rfrxiq_imbal = ROFDM0_XBRXIQIMBALANCE;
	rtlphy->phyreg_def[RF90_PATH_C].rfrxiq_imbal = ROFDM0_XCRXIQIMBALANCE;
	rtlphy->phyreg_def[RF90_PATH_D].rfrxiq_imbal = ROFDM0_XDRXIQIMBALANCE;

	/*RX AFE control 1 */
	rtlphy->phyreg_def[RF90_PATH_A].rfrx_afe = ROFDM0_XARXAFE;
	rtlphy->phyreg_def[RF90_PATH_B].rfrx_afe = ROFDM0_XBRXAFE;
	rtlphy->phyreg_def[RF90_PATH_C].rfrx_afe = ROFDM0_XCRXAFE;
	rtlphy->phyreg_def[RF90_PATH_D].rfrx_afe = ROFDM0_XDRXAFE;

	/* Tx AFE control 1 */
	rtlphy->phyreg_def[RF90_PATH_A].rftxiq_imbal = ROFDM0_XATXIQIMBALANCE;
	rtlphy->phyreg_def[RF90_PATH_B].rftxiq_imbal = ROFDM0_XBTXIQIMBALANCE;
	rtlphy->phyreg_def[RF90_PATH_C].rftxiq_imbal = ROFDM0_XCTXIQIMBALANCE;
	rtlphy->phyreg_def[RF90_PATH_D].rftxiq_imbal = ROFDM0_XDTXIQIMBALANCE;

	/* Tx AFE control 2 */
	rtlphy->phyreg_def[RF90_PATH_A].rftx_afe = ROFDM0_XATXAFE;
	rtlphy->phyreg_def[RF90_PATH_B].rftx_afe = ROFDM0_XBTXAFE;
	rtlphy->phyreg_def[RF90_PATH_C].rftx_afe = ROFDM0_XCTXAFE;
	rtlphy->phyreg_def[RF90_PATH_D].rftx_afe = ROFDM0_XDTXAFE;

	/* Transceiver LSSI Readback SI mode */
	rtlphy->phyreg_def[RF90_PATH_A].rf_rb = RFPGA0_XA_LSSIREADBACK;
	rtlphy->phyreg_def[RF90_PATH_B].rf_rb = RFPGA0_XB_LSSIREADBACK;
	rtlphy->phyreg_def[RF90_PATH_C].rf_rb = RFPGA0_XC_LSSIREADBACK;
	rtlphy->phyreg_def[RF90_PATH_D].rf_rb = RFPGA0_XD_LSSIREADBACK;

	/* Transceiver LSSI Readback PI mode */
	rtlphy->phyreg_def[RF90_PATH_A].rf_rbpi = TRANSCEIVERA_HSPI_READBACK;
	rtlphy->phyreg_def[RF90_PATH_B].rf_rbpi = TRANSCEIVERB_HSPI_READBACK;
}
EXPORT_SYMBOL_GPL(rtl92d_phy_init_bb_rf_register_definition);

void rtl92d_store_pwrindex_diffrate_offset(struct ieee80211_hw *hw,
					   u32 regaddr, u32 bitmask, u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	int index;

	if (regaddr == RTXAGC_A_RATE18_06)
		index = 0;
	else if (regaddr == RTXAGC_A_RATE54_24)
		index = 1;
	else if (regaddr == RTXAGC_A_CCK1_MCS32)
		index = 6;
	else if (regaddr == RTXAGC_B_CCK11_A_CCK2_11 && bitmask == 0xffffff00)
		index = 7;
	else if (regaddr == RTXAGC_A_MCS03_MCS00)
		index = 2;
	else if (regaddr == RTXAGC_A_MCS07_MCS04)
		index = 3;
	else if (regaddr == RTXAGC_A_MCS11_MCS08)
		index = 4;
	else if (regaddr == RTXAGC_A_MCS15_MCS12)
		index = 5;
	else if (regaddr == RTXAGC_B_RATE18_06)
		index = 8;
	else if (regaddr == RTXAGC_B_RATE54_24)
		index = 9;
	else if (regaddr == RTXAGC_B_CCK1_55_MCS32)
		index = 14;
	else if (regaddr == RTXAGC_B_CCK11_A_CCK2_11 && bitmask == 0x000000ff)
		index = 15;
	else if (regaddr == RTXAGC_B_MCS03_MCS00)
		index = 10;
	else if (regaddr == RTXAGC_B_MCS07_MCS04)
		index = 11;
	else if (regaddr == RTXAGC_B_MCS11_MCS08)
		index = 12;
	else if (regaddr == RTXAGC_B_MCS15_MCS12)
		index = 13;
	else
		return;

	rtlphy->mcs_offset[rtlphy->pwrgroup_cnt][index] = data;
	rtl_dbg(rtlpriv, COMP_INIT, DBG_TRACE,
		"MCSTxPowerLevelOriginalOffset[%d][%d] = 0x%x\n",
		rtlphy->pwrgroup_cnt, index,
		rtlphy->mcs_offset[rtlphy->pwrgroup_cnt][index]);
	if (index == 13)
		rtlphy->pwrgroup_cnt++;
}
EXPORT_SYMBOL_GPL(rtl92d_store_pwrindex_diffrate_offset);

void rtl92d_phy_get_hw_reg_originalvalue(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;

	rtlphy->default_initialgain[0] =
	    rtl_get_bbreg(hw, ROFDM0_XAAGCCORE1, MASKBYTE0);
	rtlphy->default_initialgain[1] =
	    rtl_get_bbreg(hw, ROFDM0_XBAGCCORE1, MASKBYTE0);
	rtlphy->default_initialgain[2] =
	    rtl_get_bbreg(hw, ROFDM0_XCAGCCORE1, MASKBYTE0);
	rtlphy->default_initialgain[3] =
	    rtl_get_bbreg(hw, ROFDM0_XDAGCCORE1, MASKBYTE0);
	rtl_dbg(rtlpriv, COMP_INIT, DBG_TRACE,
		"Default initial gain (c50=0x%x, c58=0x%x, c60=0x%x, c68=0x%x\n",
		rtlphy->default_initialgain[0],
		rtlphy->default_initialgain[1],
		rtlphy->default_initialgain[2],
		rtlphy->default_initialgain[3]);
	rtlphy->framesync = rtl_get_bbreg(hw, ROFDM0_RXDETECTOR3, MASKBYTE0);
	rtlphy->framesync_c34 = rtl_get_bbreg(hw, ROFDM0_RXDETECTOR2, MASKDWORD);
	rtl_dbg(rtlpriv, COMP_INIT, DBG_TRACE,
		"Default framesync (0x%x) = 0x%x\n",
		ROFDM0_RXDETECTOR3, rtlphy->framesync);
}
EXPORT_SYMBOL_GPL(rtl92d_phy_get_hw_reg_originalvalue);

static void _rtl92d_get_txpower_index(struct ieee80211_hw *hw, u8 channel,
				      u8 *cckpowerlevel, u8 *ofdmpowerlevel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_hal *rtlhal = &rtlpriv->rtlhal;
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u8 index = channel - 1;

	/* 1. CCK */
	if (rtlhal->current_bandtype == BAND_ON_2_4G) {
		/* RF-A */
		cckpowerlevel[RF90_PATH_A] =
				 rtlefuse->txpwrlevel_cck[RF90_PATH_A][index];
		/* RF-B */
		cckpowerlevel[RF90_PATH_B] =
				 rtlefuse->txpwrlevel_cck[RF90_PATH_B][index];
	} else {
		cckpowerlevel[RF90_PATH_A] = 0;
		cckpowerlevel[RF90_PATH_B] = 0;
	}
	/* 2. OFDM for 1S or 2S */
	if (rtlphy->rf_type == RF_1T2R || rtlphy->rf_type == RF_1T1R) {
		/*  Read HT 40 OFDM TX power */
		ofdmpowerlevel[RF90_PATH_A] =
		    rtlefuse->txpwrlevel_ht40_1s[RF90_PATH_A][index];
		ofdmpowerlevel[RF90_PATH_B] =
		    rtlefuse->txpwrlevel_ht40_1s[RF90_PATH_B][index];
	} else if (rtlphy->rf_type == RF_2T2R) {
		/* Read HT 40 OFDM TX power */
		ofdmpowerlevel[RF90_PATH_A] =
		    rtlefuse->txpwrlevel_ht40_2s[RF90_PATH_A][index];
		ofdmpowerlevel[RF90_PATH_B] =
		    rtlefuse->txpwrlevel_ht40_2s[RF90_PATH_B][index];
	}
}

static void _rtl92d_ccxpower_index_check(struct ieee80211_hw *hw,
					 u8 channel, u8 *cckpowerlevel,
					 u8 *ofdmpowerlevel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;

	rtlphy->cur_cck_txpwridx = cckpowerlevel[0];
	rtlphy->cur_ofdm24g_txpwridx = ofdmpowerlevel[0];
}

static u8 _rtl92c_phy_get_rightchnlplace(u8 chnl)
{
	u8 place = chnl;

	if (chnl > 14) {
		for (place = 14; place < ARRAY_SIZE(channel_all); place++) {
			if (channel_all[place] == chnl) {
				place++;
				break;
			}
		}
	}
	return place;
}

void rtl92d_phy_set_txpower_level(struct ieee80211_hw *hw, u8 channel)
{
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 cckpowerlevel[2], ofdmpowerlevel[2];

	if (!rtlefuse->txpwr_fromeprom)
		return;
	channel = _rtl92c_phy_get_rightchnlplace(channel);
	_rtl92d_get_txpower_index(hw, channel, &cckpowerlevel[0],
				  &ofdmpowerlevel[0]);
	if (rtlpriv->rtlhal.current_bandtype == BAND_ON_2_4G)
		_rtl92d_ccxpower_index_check(hw, channel, &cckpowerlevel[0],
					     &ofdmpowerlevel[0]);
	if (rtlpriv->rtlhal.current_bandtype == BAND_ON_2_4G)
		rtl92d_phy_rf6052_set_cck_txpower(hw, &cckpowerlevel[0]);
	rtl92d_phy_rf6052_set_ofdm_txpower(hw, &ofdmpowerlevel[0], channel);
}
EXPORT_SYMBOL_GPL(rtl92d_phy_set_txpower_level);

void rtl92d_phy_enable_rf_env(struct ieee80211_hw *hw, u8 rfpath,
			      u32 *pu4_regval)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct bb_reg_def *pphyreg = &rtlphy->phyreg_def[rfpath];

	rtl_dbg(rtlpriv, COMP_RF, DBG_LOUD, "====>\n");
	/*----Store original RFENV control type----*/
	switch (rfpath) {
	case RF90_PATH_A:
	case RF90_PATH_C:
		*pu4_regval = rtl_get_bbreg(hw, pphyreg->rfintfs, BRFSI_RFENV);
		break;
	case RF90_PATH_B:
	case RF90_PATH_D:
		*pu4_regval =
		    rtl_get_bbreg(hw, pphyreg->rfintfs, BRFSI_RFENV << 16);
		break;
	}
	/*----Set RF_ENV enable----*/
	rtl_set_bbreg(hw, pphyreg->rfintfe, BRFSI_RFENV << 16, 0x1);
	udelay(1);
	/*----Set RF_ENV output high----*/
	rtl_set_bbreg(hw, pphyreg->rfintfo, BRFSI_RFENV, 0x1);
	udelay(1);
	/* Set bit number of Address and Data for RF register */
	/* Set 1 to 4 bits for 8255 */
	rtl_set_bbreg(hw, pphyreg->rfhssi_para2, B3WIREADDRESSLENGTH, 0x0);
	udelay(1);
	/*Set 0 to 12 bits for 8255 */
	rtl_set_bbreg(hw, pphyreg->rfhssi_para2, B3WIREDATALENGTH, 0x0);
	udelay(1);
	rtl_dbg(rtlpriv, COMP_RF, DBG_LOUD, "<====\n");
}
EXPORT_SYMBOL_GPL(rtl92d_phy_enable_rf_env);

void rtl92d_phy_restore_rf_env(struct ieee80211_hw *hw, u8 rfpath,
			       u32 *pu4_regval)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct bb_reg_def *pphyreg = &rtlphy->phyreg_def[rfpath];

	rtl_dbg(rtlpriv, COMP_RF, DBG_LOUD, "=====>\n");
	/*----Restore RFENV control type----*/
	switch (rfpath) {
	case RF90_PATH_A:
	case RF90_PATH_C:
		rtl_set_bbreg(hw, pphyreg->rfintfs, BRFSI_RFENV, *pu4_regval);
		break;
	case RF90_PATH_B:
	case RF90_PATH_D:
		rtl_set_bbreg(hw, pphyreg->rfintfs, BRFSI_RFENV << 16,
			      *pu4_regval);
		break;
	}
	rtl_dbg(rtlpriv, COMP_RF, DBG_LOUD, "<=====\n");
}
EXPORT_SYMBOL_GPL(rtl92d_phy_restore_rf_env);

u8 rtl92d_get_rightchnlplace_for_iqk(u8 chnl)
{
	u8 place;

	if (chnl > 14) {
		for (place = 14; place < ARRAY_SIZE(channel_all); place++) {
			if (channel_all[place] == chnl)
				return place - 13;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rtl92d_get_rightchnlplace_for_iqk);

void rtl92d_phy_save_adda_registers(struct ieee80211_hw *hw, const u32 *adda_reg,
				    u32 *adda_backup, u32 regnum)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 i;

	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "Save ADDA parameters.\n");
	for (i = 0; i < regnum; i++)
		adda_backup[i] = rtl_get_bbreg(hw, adda_reg[i], MASKDWORD);
}
EXPORT_SYMBOL_GPL(rtl92d_phy_save_adda_registers);

void rtl92d_phy_save_mac_registers(struct ieee80211_hw *hw,
				   const u32 *macreg, u32 *macbackup)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 i;

	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "Save MAC parameters.\n");
	for (i = 0; i < (IQK_MAC_REG_NUM - 1); i++)
		macbackup[i] = rtl_read_byte(rtlpriv, macreg[i]);
	macbackup[i] = rtl_read_dword(rtlpriv, macreg[i]);
}
EXPORT_SYMBOL_GPL(rtl92d_phy_save_mac_registers);

void rtl92d_phy_path_adda_on(struct ieee80211_hw *hw,
			     const u32 *adda_reg, bool patha_on, bool is2t)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 pathon;
	u32 i;

	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "ADDA ON.\n");
	pathon = patha_on ? 0x04db25a4 : 0x0b1b25a4;
	if (patha_on)
		pathon = rtlpriv->rtlhal.interfaceindex == 0 ?
		    0x04db25a4 : 0x0b1b25a4;
	for (i = 0; i < IQK_ADDA_REG_NUM; i++)
		rtl_set_bbreg(hw, adda_reg[i], MASKDWORD, pathon);
}
EXPORT_SYMBOL_GPL(rtl92d_phy_path_adda_on);

void rtl92d_phy_mac_setting_calibration(struct ieee80211_hw *hw,
					const u32 *macreg, u32 *macbackup)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 i;

	RTPRINT(rtlpriv, FINIT, INIT_IQK, "MAC settings for Calibration.\n");
	rtl_write_byte(rtlpriv, macreg[0], 0x3F);

	for (i = 1; i < (IQK_MAC_REG_NUM - 1); i++)
		rtl_write_byte(rtlpriv, macreg[i], (u8)(macbackup[i] &
			       (~BIT(3))));
	rtl_write_byte(rtlpriv, macreg[i], (u8)(macbackup[i] & (~BIT(5))));
}
EXPORT_SYMBOL_GPL(rtl92d_phy_mac_setting_calibration);

static u32 _rtl92d_phy_get_abs(u32 val1, u32 val2)
{
	u32 ret;

	if (val1 >= val2)
		ret = val1 - val2;
	else
		ret = val2 - val1;
	return ret;
}

static bool _rtl92d_is_legal_5g_channel(struct ieee80211_hw *hw, u8 channel)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(channel5g); i++)
		if (channel == channel5g[i])
			return true;
	return false;
}

void rtl92d_phy_calc_curvindex(struct ieee80211_hw *hw,
			       const u32 *targetchnl, u32 *curvecount_val,
			       bool is5g, u32 *curveindex)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 smallest_abs_val = 0xffffffff, u4tmp;
	u8 i, j;
	u8 chnl_num = is5g ? TARGET_CHNL_NUM_5G : TARGET_CHNL_NUM_2G;

	for (i = 0; i < chnl_num; i++) {
		if (is5g && !_rtl92d_is_legal_5g_channel(hw, i + 1))
			continue;
		curveindex[i] = 0;
		for (j = 0; j < (CV_CURVE_CNT * 2); j++) {
			u4tmp = _rtl92d_phy_get_abs(targetchnl[i],
						    curvecount_val[j]);

			if (u4tmp < smallest_abs_val) {
				curveindex[i] = j;
				smallest_abs_val = u4tmp;
			}
		}
		smallest_abs_val = 0xffffffff;
		RTPRINT(rtlpriv, FINIT, INIT_IQK, "curveindex[%d] = %x\n",
			i, curveindex[i]);
	}
}
EXPORT_SYMBOL_GPL(rtl92d_phy_calc_curvindex);

void rtl92d_phy_reset_iqk_result(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 i;

	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
		"settings regs %zu default regs %d\n",
		ARRAY_SIZE(rtlphy->iqk_matrix),
		IQK_MATRIX_REG_NUM);
	/* 0xe94, 0xe9c, 0xea4, 0xeac, 0xeb4, 0xebc, 0xec4, 0xecc */
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
EXPORT_SYMBOL_GPL(rtl92d_phy_reset_iqk_result);

static void rtl92d_phy_set_io(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct dig_t *de_digtable = &rtlpriv->dm_digtable;
	struct rtl_phy *rtlphy = &rtlpriv->phy;

	rtl_dbg(rtlpriv, COMP_CMD, DBG_TRACE,
		"--->Cmd(%#x), set_io_inprogress(%d)\n",
		rtlphy->current_io_type, rtlphy->set_io_inprogress);

	switch (rtlphy->current_io_type) {
	case IO_CMD_RESUME_DM_BY_SCAN:
		de_digtable->cur_igvalue = rtlphy->initgain_backup.xaagccore1;
		rtl92d_dm_write_dig(hw);
		rtl92d_phy_set_txpower_level(hw, rtlphy->current_channel);
		break;
	case IO_CMD_PAUSE_DM_BY_SCAN:
		rtlphy->initgain_backup.xaagccore1 = de_digtable->cur_igvalue;
		de_digtable->cur_igvalue = 0x37;
		if (rtlpriv->rtlhal.interface == INTF_USB)
			de_digtable->cur_igvalue = 0x17;
		rtl92d_dm_write_dig(hw);
		break;
	default:
		pr_err("switch case %#x not processed\n",
		       rtlphy->current_io_type);
		break;
	}

	rtlphy->set_io_inprogress = false;
	rtl_dbg(rtlpriv, COMP_CMD, DBG_TRACE, "<---(%#x)\n",
		rtlphy->current_io_type);
}

bool rtl92d_phy_set_io_cmd(struct ieee80211_hw *hw, enum io_type iotype)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	bool postprocessing = false;

	rtl_dbg(rtlpriv, COMP_CMD, DBG_TRACE,
		"-->IO Cmd(%#x), set_io_inprogress(%d)\n",
		 iotype, rtlphy->set_io_inprogress);

	do {
		switch (iotype) {
		case IO_CMD_RESUME_DM_BY_SCAN:
			rtl_dbg(rtlpriv, COMP_CMD, DBG_TRACE,
				"[IO CMD] Resume DM after scan\n");
			postprocessing = true;
			break;
		case IO_CMD_PAUSE_DM_BY_SCAN:
			rtl_dbg(rtlpriv, COMP_CMD, DBG_TRACE,
				"[IO CMD] Pause DM before scan\n");
			postprocessing = true;
			break;
		default:
			pr_err("switch case %#x not processed\n",
			       iotype);
			break;
		}
	} while (false);

	if (postprocessing && !rtlphy->set_io_inprogress) {
		rtlphy->set_io_inprogress = true;
		rtlphy->current_io_type = iotype;
	} else {
		return false;
	}

	rtl92d_phy_set_io(hw);
	rtl_dbg(rtlpriv, COMP_CMD, DBG_TRACE, "<--IO Type(%#x)\n", iotype);
	return true;
}
EXPORT_SYMBOL_GPL(rtl92d_phy_set_io_cmd);

void rtl92d_phy_config_macphymode(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 offset = REG_MAC_PHY_CTRL_NORMAL;
	u8 phy_ctrl = 0xf0;

	if (rtlhal->interface == INTF_USB) {
		phy_ctrl = rtl_read_byte(rtlpriv, offset);
		phy_ctrl &= ~(BIT(0) | BIT(1) | BIT(2));
	}

	switch (rtlhal->macphymode) {
	case DUALMAC_DUALPHY:
		rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
			"MacPhyMode: DUALMAC_DUALPHY\n");
		rtl_write_byte(rtlpriv, offset, phy_ctrl | BIT(0) | BIT(1));
		break;
	case SINGLEMAC_SINGLEPHY:
		rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
			"MacPhyMode: SINGLEMAC_SINGLEPHY\n");
		rtl_write_byte(rtlpriv, offset, phy_ctrl | BIT(2));
		break;
	case DUALMAC_SINGLEPHY:
		rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
			"MacPhyMode: DUALMAC_SINGLEPHY\n");
		rtl_write_byte(rtlpriv, offset, phy_ctrl | BIT(0));
		break;
	}
}
EXPORT_SYMBOL_GPL(rtl92d_phy_config_macphymode);

void rtl92d_phy_config_macphymode_info(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_phy *rtlphy = &rtlpriv->phy;

	switch (rtlhal->macphymode) {
	case DUALMAC_SINGLEPHY:
		rtlphy->rf_type = RF_2T2R;
		rtlhal->version |= RF_TYPE_2T2R;
		rtlhal->bandset = BAND_ON_BOTH;
		rtlhal->current_bandtype = BAND_ON_2_4G;
		break;

	case SINGLEMAC_SINGLEPHY:
		rtlphy->rf_type = RF_2T2R;
		rtlhal->version |= RF_TYPE_2T2R;
		rtlhal->bandset = BAND_ON_BOTH;
		rtlhal->current_bandtype = BAND_ON_2_4G;
		break;

	case DUALMAC_DUALPHY:
		rtlphy->rf_type = RF_1T1R;
		rtlhal->version &= RF_TYPE_1T1R;
		/* Now we let MAC0 run on 5G band. */
		if (rtlhal->interfaceindex == 0) {
			rtlhal->bandset = BAND_ON_5G;
			rtlhal->current_bandtype = BAND_ON_5G;
		} else {
			rtlhal->bandset = BAND_ON_2_4G;
			rtlhal->current_bandtype = BAND_ON_2_4G;
		}
		break;
	default:
		break;
	}
}
EXPORT_SYMBOL_GPL(rtl92d_phy_config_macphymode_info);

u8 rtl92d_get_chnlgroup_fromarray(u8 chnl)
{
	u8 group;

	if (channel_all[chnl] <= 3)
		group = 0;
	else if (channel_all[chnl] <= 9)
		group = 1;
	else if (channel_all[chnl] <= 14)
		group = 2;
	else if (channel_all[chnl] <= 44)
		group = 3;
	else if (channel_all[chnl] <= 54)
		group = 4;
	else if (channel_all[chnl] <= 64)
		group = 5;
	else if (channel_all[chnl] <= 112)
		group = 6;
	else if (channel_all[chnl] <= 126)
		group = 7;
	else if (channel_all[chnl] <= 140)
		group = 8;
	else if (channel_all[chnl] <= 153)
		group = 9;
	else if (channel_all[chnl] <= 159)
		group = 10;
	else
		group = 11;
	return group;
}
EXPORT_SYMBOL_GPL(rtl92d_get_chnlgroup_fromarray);

u8 rtl92d_phy_get_chnlgroup_bypg(u8 chnlindex)
{
	u8 group;

	if (channel_all[chnlindex] <= 3)	/* Chanel 1-3 */
		group = 0;
	else if (channel_all[chnlindex] <= 9)	/* Channel 4-9 */
		group = 1;
	else if (channel_all[chnlindex] <= 14)	/* Channel 10-14 */
		group = 2;
	else if (channel_all[chnlindex] <= 64)
		group = 6;
	else if (channel_all[chnlindex] <= 140)
		group = 7;
	else
		group = 8;
	return group;
}

void rtl92d_phy_config_maccoexist_rfpage(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	switch (rtlpriv->rtlhal.macphymode) {
	case DUALMAC_DUALPHY:
		rtl_write_byte(rtlpriv, REG_DMC, 0x0);
		rtl_write_byte(rtlpriv, REG_RX_PKT_LIMIT, 0x08);
		rtl_write_word(rtlpriv, REG_TRXFF_BNDY + 2, 0x13ff);
		break;
	case DUALMAC_SINGLEPHY:
		rtl_write_byte(rtlpriv, REG_DMC, 0xf8);
		rtl_write_byte(rtlpriv, REG_RX_PKT_LIMIT, 0x08);
		rtl_write_word(rtlpriv, REG_TRXFF_BNDY + 2, 0x13ff);
		break;
	case SINGLEMAC_SINGLEPHY:
		rtl_write_byte(rtlpriv, REG_DMC, 0x0);
		rtl_write_byte(rtlpriv, REG_RX_PKT_LIMIT, 0x10);
		rtl_write_word(rtlpriv, (REG_TRXFF_BNDY + 2), 0x27FF);
		break;
	default:
		break;
	}
}
EXPORT_SYMBOL_GPL(rtl92d_phy_config_maccoexist_rfpage);
