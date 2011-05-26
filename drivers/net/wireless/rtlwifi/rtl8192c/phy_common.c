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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
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
#include "../rtl8192ce/reg.h"
#include "../rtl8192ce/def.h"
#include "dm_common.h"
#include "phy_common.h"

/* Define macro to shorten lines */
#define MCS_TXPWR	mcs_txpwrlevel_origoffset

u32 rtl92c_phy_query_bb_reg(struct ieee80211_hw *hw, u32 regaddr, u32 bitmask)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 returnvalue, originalvalue, bitshift;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE, ("regaddr(%#x), "
					       "bitmask(%#x)\n", regaddr,
					       bitmask));
	originalvalue = rtl_read_dword(rtlpriv, regaddr);
	bitshift = _rtl92c_phy_calculate_bit_shift(bitmask);
	returnvalue = (originalvalue & bitmask) >> bitshift;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE, ("BBR MASK=0x%x "
					       "Addr[0x%x]=0x%x\n", bitmask,
					       regaddr, originalvalue));

	return returnvalue;

}
EXPORT_SYMBOL(rtl92c_phy_query_bb_reg);

void rtl92c_phy_set_bb_reg(struct ieee80211_hw *hw,
			   u32 regaddr, u32 bitmask, u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 originalvalue, bitshift;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE, ("regaddr(%#x), bitmask(%#x),"
					       " data(%#x)\n", regaddr, bitmask,
					       data));

	if (bitmask != MASKDWORD) {
		originalvalue = rtl_read_dword(rtlpriv, regaddr);
		bitshift = _rtl92c_phy_calculate_bit_shift(bitmask);
		data = ((originalvalue & (~bitmask)) | (data << bitshift));
	}

	rtl_write_dword(rtlpriv, regaddr, data);

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE, ("regaddr(%#x), bitmask(%#x),"
					       " data(%#x)\n", regaddr, bitmask,
					       data));
}
EXPORT_SYMBOL(rtl92c_phy_set_bb_reg);

u32 _rtl92c_phy_fw_rf_serial_read(struct ieee80211_hw *hw,
					 enum radio_path rfpath, u32 offset)
{
	RT_ASSERT(false, ("deprecated!\n"));
	return 0;
}
EXPORT_SYMBOL(_rtl92c_phy_fw_rf_serial_read);

void _rtl92c_phy_fw_rf_serial_write(struct ieee80211_hw *hw,
					   enum radio_path rfpath, u32 offset,
					   u32 data)
{
	RT_ASSERT(false, ("deprecated!\n"));
}
EXPORT_SYMBOL(_rtl92c_phy_fw_rf_serial_write);

u32 _rtl92c_phy_rf_serial_read(struct ieee80211_hw *hw,
				      enum radio_path rfpath, u32 offset)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct bb_reg_def *pphyreg = &rtlphy->phyreg_def[rfpath];
	u32 newoffset;
	u32 tmplong, tmplong2;
	u8 rfpi_enable = 0;
	u32 retvalue;

	offset &= 0x3f;
	newoffset = offset;
	if (RT_CANNOT_IO(hw)) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, ("return all one\n"));
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
	mdelay(1);
	rtl_set_bbreg(hw, RFPGA0_XA_HSSIPARAMETER2, MASKDWORD,
		      tmplong | BLSSIREADEDGE);
	mdelay(1);
	if (rfpath == RF90_PATH_A)
		rfpi_enable = (u8) rtl_get_bbreg(hw, RFPGA0_XA_HSSIPARAMETER1,
						 BIT(8));
	else if (rfpath == RF90_PATH_B)
		rfpi_enable = (u8) rtl_get_bbreg(hw, RFPGA0_XB_HSSIPARAMETER1,
						 BIT(8));
	if (rfpi_enable)
		retvalue = rtl_get_bbreg(hw, pphyreg->rflssi_readbackpi,
					 BLSSIREADBACKDATA);
	else
		retvalue = rtl_get_bbreg(hw, pphyreg->rflssi_readback,
					 BLSSIREADBACKDATA);
	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE, ("RFR-%d Addr[0x%x]=0x%x\n",
					       rfpath, pphyreg->rflssi_readback,
					       retvalue));
	return retvalue;
}
EXPORT_SYMBOL(_rtl92c_phy_rf_serial_read);

void _rtl92c_phy_rf_serial_write(struct ieee80211_hw *hw,
					enum radio_path rfpath, u32 offset,
					u32 data)
{
	u32 data_and_addr;
	u32 newoffset;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct bb_reg_def *pphyreg = &rtlphy->phyreg_def[rfpath];

	if (RT_CANNOT_IO(hw)) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, ("stop\n"));
		return;
	}
	offset &= 0x3f;
	newoffset = offset;
	data_and_addr = ((newoffset << 20) | (data & 0x000fffff)) & 0x0fffffff;
	rtl_set_bbreg(hw, pphyreg->rf3wire_offset, MASKDWORD, data_and_addr);
	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE, ("RFW-%d Addr[0x%x]=0x%x\n",
					       rfpath, pphyreg->rf3wire_offset,
					       data_and_addr));
}
EXPORT_SYMBOL(_rtl92c_phy_rf_serial_write);

u32 _rtl92c_phy_calculate_bit_shift(u32 bitmask)
{
	u32 i;

	for (i = 0; i <= 31; i++) {
		if (((bitmask >> i) & 0x1) == 1)
			break;
	}
	return i;
}
EXPORT_SYMBOL(_rtl92c_phy_calculate_bit_shift);

static void _rtl92c_phy_bb_config_1t(struct ieee80211_hw *hw)
{
	rtl_set_bbreg(hw, RFPGA0_TXINFO, 0x3, 0x2);
	rtl_set_bbreg(hw, RFPGA1_TXINFO, 0x300033, 0x200022);
	rtl_set_bbreg(hw, RCCK0_AFESETTING, MASKBYTE3, 0x45);
	rtl_set_bbreg(hw, ROFDM0_TRXPATHENABLE, MASKBYTE0, 0x23);
	rtl_set_bbreg(hw, ROFDM0_AGCPARAMETER1, 0x30, 0x1);
	rtl_set_bbreg(hw, 0xe74, 0x0c000000, 0x2);
	rtl_set_bbreg(hw, 0xe78, 0x0c000000, 0x2);
	rtl_set_bbreg(hw, 0xe7c, 0x0c000000, 0x2);
	rtl_set_bbreg(hw, 0xe80, 0x0c000000, 0x2);
	rtl_set_bbreg(hw, 0xe88, 0x0c000000, 0x2);
}
bool rtl92c_phy_rf_config(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	return rtlpriv->cfg->ops->phy_rf6052_config(hw);
}
EXPORT_SYMBOL(rtl92c_phy_rf_config);

bool _rtl92c_phy_bb8192c_config_parafile(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	bool rtstatus;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE, ("==>\n"));
	rtstatus = rtlpriv->cfg->ops->config_bb_with_headerfile(hw,
						 BASEBAND_CONFIG_PHY_REG);
	if (rtstatus != true) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, ("Write BB Reg Fail!!"));
		return false;
	}
	if (rtlphy->rf_type == RF_1T2R) {
		_rtl92c_phy_bb_config_1t(hw);
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE, ("Config to 1T!!\n"));
	}
	if (rtlefuse->autoload_failflag == false) {
		rtlphy->pwrgroup_cnt = 0;
		rtstatus = rtlpriv->cfg->ops->config_bb_with_pgheaderfile(hw,
						   BASEBAND_CONFIG_PHY_REG);
	}
	if (rtstatus != true) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, ("BB_PG Reg Fail!!"));
		return false;
	}
	rtstatus = rtlpriv->cfg->ops->config_bb_with_headerfile(hw,
						 BASEBAND_CONFIG_AGC_TAB);
	if (rtstatus != true) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, ("AGC Table Fail\n"));
		return false;
	}
	rtlphy->cck_high_power = (bool) (rtl_get_bbreg(hw,
						RFPGA0_XA_HSSIPARAMETER2,
						0x200));
	return true;
}
EXPORT_SYMBOL(_rtl92c_phy_bb8192c_config_parafile);

void _rtl92c_store_pwrIndex_diffrate_offset(struct ieee80211_hw *hw,
						   u32 regaddr, u32 bitmask,
						   u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);

	if (regaddr == RTXAGC_A_RATE18_06) {
		rtlphy->MCS_TXPWR[rtlphy->pwrgroup_cnt][0] = data;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 ("MCSTxPowerLevelOriginalOffset[%d][0] = 0x%x\n",
			  rtlphy->pwrgroup_cnt,
			  rtlphy->MCS_TXPWR[rtlphy->pwrgroup_cnt][0]));
	}
	if (regaddr == RTXAGC_A_RATE54_24) {
		rtlphy->MCS_TXPWR[rtlphy->pwrgroup_cnt][1] = data;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 ("MCSTxPowerLevelOriginalOffset[%d][1] = 0x%x\n",
			  rtlphy->pwrgroup_cnt,
			  rtlphy->MCS_TXPWR[rtlphy->pwrgroup_cnt][1]));
	}
	if (regaddr == RTXAGC_A_CCK1_MCS32) {
		rtlphy->MCS_TXPWR[rtlphy->pwrgroup_cnt][6] = data;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 ("MCSTxPowerLevelOriginalOffset[%d][6] = 0x%x\n",
			  rtlphy->pwrgroup_cnt,
			  rtlphy->MCS_TXPWR[rtlphy->pwrgroup_cnt][6]));
	}
	if (regaddr == RTXAGC_B_CCK11_A_CCK2_11 && bitmask == 0xffffff00) {
		rtlphy->MCS_TXPWR[rtlphy->pwrgroup_cnt][7] = data;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 ("MCSTxPowerLevelOriginalOffset[%d][7] = 0x%x\n",
			  rtlphy->pwrgroup_cnt,
			  rtlphy->MCS_TXPWR[rtlphy->pwrgroup_cnt][7]));
	}
	if (regaddr == RTXAGC_A_MCS03_MCS00) {
		rtlphy->MCS_TXPWR[rtlphy->pwrgroup_cnt][2] = data;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 ("MCSTxPowerLevelOriginalOffset[%d][2] = 0x%x\n",
			  rtlphy->pwrgroup_cnt,
			  rtlphy->MCS_TXPWR[rtlphy->pwrgroup_cnt][2]));
	}
	if (regaddr == RTXAGC_A_MCS07_MCS04) {
		rtlphy->MCS_TXPWR[rtlphy->pwrgroup_cnt][3] = data;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 ("MCSTxPowerLevelOriginalOffset[%d][3] = 0x%x\n",
			  rtlphy->pwrgroup_cnt,
			  rtlphy->MCS_TXPWR[rtlphy->pwrgroup_cnt][3]));
	}
	if (regaddr == RTXAGC_A_MCS11_MCS08) {
		rtlphy->MCS_TXPWR[rtlphy->pwrgroup_cnt][4] = data;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 ("MCSTxPowerLevelOriginalOffset[%d][4] = 0x%x\n",
			  rtlphy->pwrgroup_cnt,
			  rtlphy->MCS_TXPWR[rtlphy->pwrgroup_cnt][4]));
	}
	if (regaddr == RTXAGC_A_MCS15_MCS12) {
		rtlphy->MCS_TXPWR[rtlphy->pwrgroup_cnt][5] = data;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 ("MCSTxPowerLevelOriginalOffset[%d][5] = 0x%x\n",
			  rtlphy->pwrgroup_cnt,
			  rtlphy->MCS_TXPWR[rtlphy->pwrgroup_cnt][5]));
	}
	if (regaddr == RTXAGC_B_RATE18_06) {
		rtlphy->MCS_TXPWR[rtlphy->pwrgroup_cnt][8] = data;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 ("MCSTxPowerLevelOriginalOffset[%d][8] = 0x%x\n",
			  rtlphy->pwrgroup_cnt,
			  rtlphy->MCS_TXPWR[rtlphy->pwrgroup_cnt][8]));
	}
	if (regaddr == RTXAGC_B_RATE54_24) {
		rtlphy->MCS_TXPWR[rtlphy->pwrgroup_cnt][9] = data;

		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 ("MCSTxPowerLevelOriginalOffset[%d][9] = 0x%x\n",
			  rtlphy->pwrgroup_cnt,
			  rtlphy->MCS_TXPWR[rtlphy->pwrgroup_cnt][9]));
	}

	if (regaddr == RTXAGC_B_CCK1_55_MCS32) {
		rtlphy->MCS_TXPWR[rtlphy->pwrgroup_cnt][14] = data;

		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 ("MCSTxPowerLevelOriginalOffset[%d][14] = 0x%x\n",
			  rtlphy->pwrgroup_cnt,
			  rtlphy->MCS_TXPWR[rtlphy->pwrgroup_cnt][14]));
	}

	if (regaddr == RTXAGC_B_CCK11_A_CCK2_11 && bitmask == 0x000000ff) {
		rtlphy->MCS_TXPWR[rtlphy->pwrgroup_cnt][15] = data;

		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 ("MCSTxPowerLevelOriginalOffset[%d][15] = 0x%x\n",
			  rtlphy->pwrgroup_cnt,
			  rtlphy->MCS_TXPWR[rtlphy->pwrgroup_cnt][15]));
	}

	if (regaddr == RTXAGC_B_MCS03_MCS00) {
		rtlphy->MCS_TXPWR[rtlphy->pwrgroup_cnt][10] = data;

		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 ("MCSTxPowerLevelOriginalOffset[%d][10] = 0x%x\n",
			  rtlphy->pwrgroup_cnt,
			  rtlphy->MCS_TXPWR[rtlphy->pwrgroup_cnt][10]));
	}

	if (regaddr == RTXAGC_B_MCS07_MCS04) {
		rtlphy->MCS_TXPWR[rtlphy->pwrgroup_cnt][11] = data;

		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 ("MCSTxPowerLevelOriginalOffset[%d][11] = 0x%x\n",
			  rtlphy->pwrgroup_cnt,
			  rtlphy->MCS_TXPWR[rtlphy->pwrgroup_cnt][11]));
	}

	if (regaddr == RTXAGC_B_MCS11_MCS08) {
		rtlphy->MCS_TXPWR[rtlphy->pwrgroup_cnt][12] = data;

		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 ("MCSTxPowerLevelOriginalOffset[%d][12] = 0x%x\n",
			  rtlphy->pwrgroup_cnt,
			  rtlphy->MCS_TXPWR[rtlphy->pwrgroup_cnt][12]));
	}

	if (regaddr == RTXAGC_B_MCS15_MCS12) {
		rtlphy->MCS_TXPWR[rtlphy->pwrgroup_cnt][13] = data;

		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 ("MCSTxPowerLevelOriginalOffset[%d][13] = 0x%x\n",
			  rtlphy->pwrgroup_cnt,
			  rtlphy->MCS_TXPWR[rtlphy->pwrgroup_cnt][13]));

		rtlphy->pwrgroup_cnt++;
	}
}
EXPORT_SYMBOL(_rtl92c_store_pwrIndex_diffrate_offset);

void rtl92c_phy_get_hw_reg_originalvalue(struct ieee80211_hw *hw)
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
		 ("Default initial gain (c50=0x%x, "
		  "c58=0x%x, c60=0x%x, c68=0x%x\n",
		  rtlphy->default_initialgain[0],
		  rtlphy->default_initialgain[1],
		  rtlphy->default_initialgain[2],
		  rtlphy->default_initialgain[3]));

	rtlphy->framesync = (u8) rtl_get_bbreg(hw,
					       ROFDM0_RXDETECTOR3, MASKBYTE0);
	rtlphy->framesync_c34 = rtl_get_bbreg(hw,
					      ROFDM0_RXDETECTOR2, MASKDWORD);

	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
		 ("Default framesync (0x%x) = 0x%x\n",
		  ROFDM0_RXDETECTOR3, rtlphy->framesync));
}

void _rtl92c_phy_init_bb_rf_register_definition(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);

	rtlphy->phyreg_def[RF90_PATH_A].rfintfs = RFPGA0_XAB_RFINTERFACESW;
	rtlphy->phyreg_def[RF90_PATH_B].rfintfs = RFPGA0_XAB_RFINTERFACESW;
	rtlphy->phyreg_def[RF90_PATH_C].rfintfs = RFPGA0_XCD_RFINTERFACESW;
	rtlphy->phyreg_def[RF90_PATH_D].rfintfs = RFPGA0_XCD_RFINTERFACESW;

	rtlphy->phyreg_def[RF90_PATH_A].rfintfi = RFPGA0_XAB_RFINTERFACERB;
	rtlphy->phyreg_def[RF90_PATH_B].rfintfi = RFPGA0_XAB_RFINTERFACERB;
	rtlphy->phyreg_def[RF90_PATH_C].rfintfi = RFPGA0_XCD_RFINTERFACERB;
	rtlphy->phyreg_def[RF90_PATH_D].rfintfi = RFPGA0_XCD_RFINTERFACERB;

	rtlphy->phyreg_def[RF90_PATH_A].rfintfo = RFPGA0_XA_RFINTERFACEOE;
	rtlphy->phyreg_def[RF90_PATH_B].rfintfo = RFPGA0_XB_RFINTERFACEOE;

	rtlphy->phyreg_def[RF90_PATH_A].rfintfe = RFPGA0_XA_RFINTERFACEOE;
	rtlphy->phyreg_def[RF90_PATH_B].rfintfe = RFPGA0_XB_RFINTERFACEOE;

	rtlphy->phyreg_def[RF90_PATH_A].rf3wire_offset =
	    RFPGA0_XA_LSSIPARAMETER;
	rtlphy->phyreg_def[RF90_PATH_B].rf3wire_offset =
	    RFPGA0_XB_LSSIPARAMETER;

	rtlphy->phyreg_def[RF90_PATH_A].rflssi_select = rFPGA0_XAB_RFPARAMETER;
	rtlphy->phyreg_def[RF90_PATH_B].rflssi_select = rFPGA0_XAB_RFPARAMETER;
	rtlphy->phyreg_def[RF90_PATH_C].rflssi_select = rFPGA0_XCD_RFPARAMETER;
	rtlphy->phyreg_def[RF90_PATH_D].rflssi_select = rFPGA0_XCD_RFPARAMETER;

	rtlphy->phyreg_def[RF90_PATH_A].rftxgain_stage = RFPGA0_TXGAINSTAGE;
	rtlphy->phyreg_def[RF90_PATH_B].rftxgain_stage = RFPGA0_TXGAINSTAGE;
	rtlphy->phyreg_def[RF90_PATH_C].rftxgain_stage = RFPGA0_TXGAINSTAGE;
	rtlphy->phyreg_def[RF90_PATH_D].rftxgain_stage = RFPGA0_TXGAINSTAGE;

	rtlphy->phyreg_def[RF90_PATH_A].rfhssi_para1 = RFPGA0_XA_HSSIPARAMETER1;
	rtlphy->phyreg_def[RF90_PATH_B].rfhssi_para1 = RFPGA0_XB_HSSIPARAMETER1;

	rtlphy->phyreg_def[RF90_PATH_A].rfhssi_para2 = RFPGA0_XA_HSSIPARAMETER2;
	rtlphy->phyreg_def[RF90_PATH_B].rfhssi_para2 = RFPGA0_XB_HSSIPARAMETER2;

	rtlphy->phyreg_def[RF90_PATH_A].rfswitch_control =
	    RFPGA0_XAB_SWITCHCONTROL;
	rtlphy->phyreg_def[RF90_PATH_B].rfswitch_control =
	    RFPGA0_XAB_SWITCHCONTROL;
	rtlphy->phyreg_def[RF90_PATH_C].rfswitch_control =
	    RFPGA0_XCD_SWITCHCONTROL;
	rtlphy->phyreg_def[RF90_PATH_D].rfswitch_control =
	    RFPGA0_XCD_SWITCHCONTROL;

	rtlphy->phyreg_def[RF90_PATH_A].rfagc_control1 = ROFDM0_XAAGCCORE1;
	rtlphy->phyreg_def[RF90_PATH_B].rfagc_control1 = ROFDM0_XBAGCCORE1;
	rtlphy->phyreg_def[RF90_PATH_C].rfagc_control1 = ROFDM0_XCAGCCORE1;
	rtlphy->phyreg_def[RF90_PATH_D].rfagc_control1 = ROFDM0_XDAGCCORE1;

	rtlphy->phyreg_def[RF90_PATH_A].rfagc_control2 = ROFDM0_XAAGCCORE2;
	rtlphy->phyreg_def[RF90_PATH_B].rfagc_control2 = ROFDM0_XBAGCCORE2;
	rtlphy->phyreg_def[RF90_PATH_C].rfagc_control2 = ROFDM0_XCAGCCORE2;
	rtlphy->phyreg_def[RF90_PATH_D].rfagc_control2 = ROFDM0_XDAGCCORE2;

	rtlphy->phyreg_def[RF90_PATH_A].rfrxiq_imbalance =
	    ROFDM0_XARXIQIMBALANCE;
	rtlphy->phyreg_def[RF90_PATH_B].rfrxiq_imbalance =
	    ROFDM0_XBRXIQIMBALANCE;
	rtlphy->phyreg_def[RF90_PATH_C].rfrxiq_imbalance =
	    ROFDM0_XCRXIQIMBANLANCE;
	rtlphy->phyreg_def[RF90_PATH_D].rfrxiq_imbalance =
	    ROFDM0_XDRXIQIMBALANCE;

	rtlphy->phyreg_def[RF90_PATH_A].rfrx_afe = ROFDM0_XARXAFE;
	rtlphy->phyreg_def[RF90_PATH_B].rfrx_afe = ROFDM0_XBRXAFE;
	rtlphy->phyreg_def[RF90_PATH_C].rfrx_afe = ROFDM0_XCRXAFE;
	rtlphy->phyreg_def[RF90_PATH_D].rfrx_afe = ROFDM0_XDRXAFE;

	rtlphy->phyreg_def[RF90_PATH_A].rftxiq_imbalance =
	    ROFDM0_XATXIQIMBALANCE;
	rtlphy->phyreg_def[RF90_PATH_B].rftxiq_imbalance =
	    ROFDM0_XBTXIQIMBALANCE;
	rtlphy->phyreg_def[RF90_PATH_C].rftxiq_imbalance =
	    ROFDM0_XCTXIQIMBALANCE;
	rtlphy->phyreg_def[RF90_PATH_D].rftxiq_imbalance =
	    ROFDM0_XDTXIQIMBALANCE;

	rtlphy->phyreg_def[RF90_PATH_A].rftx_afe = ROFDM0_XATXAFE;
	rtlphy->phyreg_def[RF90_PATH_B].rftx_afe = ROFDM0_XBTXAFE;
	rtlphy->phyreg_def[RF90_PATH_C].rftx_afe = ROFDM0_XCTXAFE;
	rtlphy->phyreg_def[RF90_PATH_D].rftx_afe = ROFDM0_XDTXAFE;

	rtlphy->phyreg_def[RF90_PATH_A].rflssi_readback =
	    RFPGA0_XA_LSSIREADBACK;
	rtlphy->phyreg_def[RF90_PATH_B].rflssi_readback =
	    RFPGA0_XB_LSSIREADBACK;
	rtlphy->phyreg_def[RF90_PATH_C].rflssi_readback =
	    RFPGA0_XC_LSSIREADBACK;
	rtlphy->phyreg_def[RF90_PATH_D].rflssi_readback =
	    RFPGA0_XD_LSSIREADBACK;

	rtlphy->phyreg_def[RF90_PATH_A].rflssi_readbackpi =
	    TRANSCEIVEA_HSPI_READBACK;
	rtlphy->phyreg_def[RF90_PATH_B].rflssi_readbackpi =
	    TRANSCEIVEB_HSPI_READBACK;

}
EXPORT_SYMBOL(_rtl92c_phy_init_bb_rf_register_definition);

void rtl92c_phy_get_txpower_level(struct ieee80211_hw *hw, long *powerlevel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u8 txpwr_level;
	long txpwr_dbm;

	txpwr_level = rtlphy->cur_cck_txpwridx;
	txpwr_dbm = _rtl92c_phy_txpwr_idx_to_dbm(hw,
						 WIRELESS_MODE_B, txpwr_level);
	txpwr_level = rtlphy->cur_ofdm24g_txpwridx +
	    rtlefuse->legacy_ht_txpowerdiff;
	if (_rtl92c_phy_txpwr_idx_to_dbm(hw,
					 WIRELESS_MODE_G,
					 txpwr_level) > txpwr_dbm)
		txpwr_dbm =
		    _rtl92c_phy_txpwr_idx_to_dbm(hw, WIRELESS_MODE_G,
						 txpwr_level);
	txpwr_level = rtlphy->cur_ofdm24g_txpwridx;
	if (_rtl92c_phy_txpwr_idx_to_dbm(hw,
					 WIRELESS_MODE_N_24G,
					 txpwr_level) > txpwr_dbm)
		txpwr_dbm =
		    _rtl92c_phy_txpwr_idx_to_dbm(hw, WIRELESS_MODE_N_24G,
						 txpwr_level);
	*powerlevel = txpwr_dbm;
}

static void _rtl92c_get_txpower_index(struct ieee80211_hw *hw, u8 channel,
				      u8 *cckpowerlevel, u8 *ofdmpowerlevel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u8 index = (channel - 1);

	cckpowerlevel[RF90_PATH_A] =
	    rtlefuse->txpwrlevel_cck[RF90_PATH_A][index];
	cckpowerlevel[RF90_PATH_B] =
	    rtlefuse->txpwrlevel_cck[RF90_PATH_B][index];
	if (get_rf_type(rtlphy) == RF_1T2R || get_rf_type(rtlphy) == RF_1T1R) {
		ofdmpowerlevel[RF90_PATH_A] =
		    rtlefuse->txpwrlevel_ht40_1s[RF90_PATH_A][index];
		ofdmpowerlevel[RF90_PATH_B] =
		    rtlefuse->txpwrlevel_ht40_1s[RF90_PATH_B][index];
	} else if (get_rf_type(rtlphy) == RF_2T2R) {
		ofdmpowerlevel[RF90_PATH_A] =
		    rtlefuse->txpwrlevel_ht40_2s[RF90_PATH_A][index];
		ofdmpowerlevel[RF90_PATH_B] =
		    rtlefuse->txpwrlevel_ht40_2s[RF90_PATH_B][index];
	}
}

static void _rtl92c_ccxpower_index_check(struct ieee80211_hw *hw,
					 u8 channel, u8 *cckpowerlevel,
					 u8 *ofdmpowerlevel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);

	rtlphy->cur_cck_txpwridx = cckpowerlevel[0];
	rtlphy->cur_ofdm24g_txpwridx = ofdmpowerlevel[0];
}

void rtl92c_phy_set_txpower_level(struct ieee80211_hw *hw, u8 channel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtlpriv);
	u8 cckpowerlevel[2], ofdmpowerlevel[2];

	if (rtlefuse->txpwr_fromeprom == false)
		return;
	_rtl92c_get_txpower_index(hw, channel,
				  &cckpowerlevel[0], &ofdmpowerlevel[0]);
	_rtl92c_ccxpower_index_check(hw,
				     channel, &cckpowerlevel[0],
				     &ofdmpowerlevel[0]);
	rtlpriv->cfg->ops->phy_rf6052_set_cck_txpower(hw, &cckpowerlevel[0]);
	rtlpriv->cfg->ops->phy_rf6052_set_ofdm_txpower(hw, &ofdmpowerlevel[0],
						       channel);
}
EXPORT_SYMBOL(rtl92c_phy_set_txpower_level);

bool rtl92c_phy_update_txpower_dbm(struct ieee80211_hw *hw, long power_indbm)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u8 idx;
	u8 rf_path;

	u8 ccktxpwridx = _rtl92c_phy_dbm_to_txpwr_Idx(hw,
						      WIRELESS_MODE_B,
						      power_indbm);
	u8 ofdmtxpwridx = _rtl92c_phy_dbm_to_txpwr_Idx(hw,
						       WIRELESS_MODE_N_24G,
						       power_indbm);
	if (ofdmtxpwridx - rtlefuse->legacy_ht_txpowerdiff > 0)
		ofdmtxpwridx -= rtlefuse->legacy_ht_txpowerdiff;
	else
		ofdmtxpwridx = 0;
	RT_TRACE(rtlpriv, COMP_TXAGC, DBG_TRACE,
		 ("%lx dBm, ccktxpwridx = %d, ofdmtxpwridx = %d\n",
		  power_indbm, ccktxpwridx, ofdmtxpwridx));
	for (idx = 0; idx < 14; idx++) {
		for (rf_path = 0; rf_path < 2; rf_path++) {
			rtlefuse->txpwrlevel_cck[rf_path][idx] = ccktxpwridx;
			rtlefuse->txpwrlevel_ht40_1s[rf_path][idx] =
			    ofdmtxpwridx;
			rtlefuse->txpwrlevel_ht40_2s[rf_path][idx] =
			    ofdmtxpwridx;
		}
	}
	rtl92c_phy_set_txpower_level(hw, rtlphy->current_channel);
	return true;
}
EXPORT_SYMBOL(rtl92c_phy_update_txpower_dbm);

void rtl92c_phy_set_beacon_hw_reg(struct ieee80211_hw *hw, u16 beaconinterval)
{
}
EXPORT_SYMBOL(rtl92c_phy_set_beacon_hw_reg);

u8 _rtl92c_phy_dbm_to_txpwr_Idx(struct ieee80211_hw *hw,
				enum wireless_mode wirelessmode,
				long power_indbm)
{
	u8 txpwridx;
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

	if ((power_indbm - offset) > 0)
		txpwridx = (u8) ((power_indbm - offset) * 2);
	else
		txpwridx = 0;

	if (txpwridx > MAX_TXPWR_IDX_NMODE_92S)
		txpwridx = MAX_TXPWR_IDX_NMODE_92S;

	return txpwridx;
}
EXPORT_SYMBOL(_rtl92c_phy_dbm_to_txpwr_Idx);

long _rtl92c_phy_txpwr_idx_to_dbm(struct ieee80211_hw *hw,
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
EXPORT_SYMBOL(_rtl92c_phy_txpwr_idx_to_dbm);

void rtl92c_phy_scan_operation_backup(struct ieee80211_hw *hw, u8 operation)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	enum io_type iotype;

	if (!is_hal_stop(rtlhal)) {
		switch (operation) {
		case SCAN_OPT_BACKUP:
			iotype = IO_CMD_PAUSE_DM_BY_SCAN;
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
				 ("Unknown Scan Backup operation.\n"));
			break;
		}
	}
}
EXPORT_SYMBOL(rtl92c_phy_scan_operation_backup);

void rtl92c_phy_set_bw_mode(struct ieee80211_hw *hw,
			    enum nl80211_channel_type ch_type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 tmp_bw = rtlphy->current_chan_bw;

	if (rtlphy->set_bwmode_inprogress)
		return;
	rtlphy->set_bwmode_inprogress = true;
	if ((!is_hal_stop(rtlhal)) && !(RT_CANNOT_IO(hw)))
		rtlpriv->cfg->ops->phy_set_bw_mode_callback(hw);
	else {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 ("FALSE driver sleep or unload\n"));
		rtlphy->set_bwmode_inprogress = false;
		rtlphy->current_chan_bw = tmp_bw;
	}
}
EXPORT_SYMBOL(rtl92c_phy_set_bw_mode);

void rtl92c_phy_sw_chnl_callback(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u32 delay;

	RT_TRACE(rtlpriv, COMP_SCAN, DBG_TRACE,
		 ("switch to channel%d\n", rtlphy->current_channel));
	if (is_hal_stop(rtlhal))
		return;
	do {
		if (!rtlphy->sw_chnl_inprogress)
			break;
		if (!_rtl92c_phy_sw_chnl_step_by_step
		    (hw, rtlphy->current_channel, &rtlphy->sw_chnl_stage,
		     &rtlphy->sw_chnl_step, &delay)) {
			if (delay > 0)
				mdelay(delay);
			else
				continue;
		} else
			rtlphy->sw_chnl_inprogress = false;
		break;
	} while (true);
	RT_TRACE(rtlpriv, COMP_SCAN, DBG_TRACE, ("<==\n"));
}
EXPORT_SYMBOL(rtl92c_phy_sw_chnl_callback);

u8 rtl92c_phy_sw_chnl(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	if (rtlphy->sw_chnl_inprogress)
		return 0;
	if (rtlphy->set_bwmode_inprogress)
		return 0;
	RT_ASSERT((rtlphy->current_channel <= 14),
		  ("WIRELESS_MODE_G but channel>14"));
	rtlphy->sw_chnl_inprogress = true;
	rtlphy->sw_chnl_stage = 0;
	rtlphy->sw_chnl_step = 0;
	if (!(is_hal_stop(rtlhal)) && !(RT_CANNOT_IO(hw))) {
		rtl92c_phy_sw_chnl_callback(hw);
		RT_TRACE(rtlpriv, COMP_CHAN, DBG_LOUD,
			 ("sw_chnl_inprogress false schdule workitem\n"));
		rtlphy->sw_chnl_inprogress = false;
	} else {
		RT_TRACE(rtlpriv, COMP_CHAN, DBG_LOUD,
			 ("sw_chnl_inprogress false driver sleep or"
			  " unload\n"));
		rtlphy->sw_chnl_inprogress = false;
	}
	return 1;
}
EXPORT_SYMBOL(rtl92c_phy_sw_chnl);

static bool _rtl92c_phy_sw_chnl_step_by_step(struct ieee80211_hw *hw,
					     u8 channel, u8 *stage, u8 *step,
					     u32 *delay)
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
	_rtl92c_phy_set_sw_chnl_cmdarray(precommoncmd, precommoncmdcnt++,
					 MAX_PRECMD_CNT,
					 CMDID_SET_TXPOWEROWER_LEVEL, 0, 0, 0);
	_rtl92c_phy_set_sw_chnl_cmdarray(precommoncmd, precommoncmdcnt++,
					 MAX_PRECMD_CNT, CMDID_END, 0, 0, 0);

	postcommoncmdcnt = 0;

	_rtl92c_phy_set_sw_chnl_cmdarray(postcommoncmd, postcommoncmdcnt++,
					 MAX_POSTCMD_CNT, CMDID_END, 0, 0, 0);

	rfdependcmdcnt = 0;

	RT_ASSERT((channel >= 1 && channel <= 14),
		  ("illegal channel for Zebra: %d\n", channel));

	_rtl92c_phy_set_sw_chnl_cmdarray(rfdependcmd, rfdependcmdcnt++,
					 MAX_RFDEPENDCMD_CNT, CMDID_RF_WRITEREG,
					 RF_CHNLBW, channel, 10);

	_rtl92c_phy_set_sw_chnl_cmdarray(rfdependcmd, rfdependcmdcnt++,
					 MAX_RFDEPENDCMD_CNT, CMDID_END, 0, 0,
					 0);

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
			rtl92c_phy_set_txpower_level(hw, channel);
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
				 ("switch case not process\n"));
			break;
		}

		break;
	} while (true);

	(*delay) = currentcmd->msdelay;
	(*step)++;
	return false;
}

static bool _rtl92c_phy_set_sw_chnl_cmdarray(struct swchnlcmd *cmdtable,
					     u32 cmdtableidx, u32 cmdtablesz,
					     enum swchnlcmd_id cmdid,
					     u32 para1, u32 para2, u32 msdelay)
{
	struct swchnlcmd *pcmd;

	if (cmdtable == NULL) {
		RT_ASSERT(false, ("cmdtable cannot be NULL.\n"));
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

bool rtl8192_phy_check_is_legal_rfpath(struct ieee80211_hw *hw, u32 rfpath)
{
	return true;
}
EXPORT_SYMBOL(rtl8192_phy_check_is_legal_rfpath);

static u8 _rtl92c_phy_path_a_iqk(struct ieee80211_hw *hw, bool config_pathb)
{
	u32 reg_eac, reg_e94, reg_e9c, reg_ea4;
	u8 result = 0x00;

	rtl_set_bbreg(hw, 0xe30, MASKDWORD, 0x10008c1f);
	rtl_set_bbreg(hw, 0xe34, MASKDWORD, 0x10008c1f);
	rtl_set_bbreg(hw, 0xe38, MASKDWORD, 0x82140102);
	rtl_set_bbreg(hw, 0xe3c, MASKDWORD,
		      config_pathb ? 0x28160202 : 0x28160502);

	if (config_pathb) {
		rtl_set_bbreg(hw, 0xe50, MASKDWORD, 0x10008c22);
		rtl_set_bbreg(hw, 0xe54, MASKDWORD, 0x10008c22);
		rtl_set_bbreg(hw, 0xe58, MASKDWORD, 0x82140102);
		rtl_set_bbreg(hw, 0xe5c, MASKDWORD, 0x28160202);
	}

	rtl_set_bbreg(hw, 0xe4c, MASKDWORD, 0x001028d1);
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
	else
		return result;

	if (!(reg_eac & BIT(27)) &&
	    (((reg_ea4 & 0x03FF0000) >> 16) != 0x132) &&
	    (((reg_eac & 0x03FF0000) >> 16) != 0x36))
		result |= 0x02;
	return result;
}

static u8 _rtl92c_phy_path_b_iqk(struct ieee80211_hw *hw)
{
	u32 reg_eac, reg_eb4, reg_ebc, reg_ec4, reg_ecc;
	u8 result = 0x00;

	rtl_set_bbreg(hw, 0xe60, MASKDWORD, 0x00000002);
	rtl_set_bbreg(hw, 0xe60, MASKDWORD, 0x00000000);
	mdelay(IQK_DELAY_TIME);
	reg_eac = rtl_get_bbreg(hw, 0xeac, MASKDWORD);
	reg_eb4 = rtl_get_bbreg(hw, 0xeb4, MASKDWORD);
	reg_ebc = rtl_get_bbreg(hw, 0xebc, MASKDWORD);
	reg_ec4 = rtl_get_bbreg(hw, 0xec4, MASKDWORD);
	reg_ecc = rtl_get_bbreg(hw, 0xecc, MASKDWORD);
	if (!(reg_eac & BIT(31)) &&
	    (((reg_eb4 & 0x03FF0000) >> 16) != 0x142) &&
	    (((reg_ebc & 0x03FF0000) >> 16) != 0x42))
		result |= 0x01;
	else
		return result;

	if (!(reg_eac & BIT(30)) &&
	    (((reg_ec4 & 0x03FF0000) >> 16) != 0x132) &&
	    (((reg_ecc & 0x03FF0000) >> 16) != 0x36))
		result |= 0x02;
	return result;
}

static void _rtl92c_phy_path_a_fill_iqk_matrix(struct ieee80211_hw *hw,
					       bool iqk_ok, long result[][8],
					       u8 final_candidate, bool btxonly)
{
	u32 oldval_0, x, tx0_a, reg;
	long y, tx0_c;

	if (final_candidate == 0xFF)
		return;
	else if (iqk_ok) {
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
		rtl_set_bbreg(hw, 0xca0, 0xF0000000, reg);
	}
}

static void _rtl92c_phy_path_b_fill_iqk_matrix(struct ieee80211_hw *hw,
					       bool iqk_ok, long result[][8],
					       u8 final_candidate, bool btxonly)
{
	u32 oldval_1, x, tx1_a, reg;
	long y, tx1_c;

	if (final_candidate == 0xFF)
		return;
	else if (iqk_ok) {
		oldval_1 = (rtl_get_bbreg(hw, ROFDM0_XBTXIQIMBALANCE,
					  MASKDWORD) >> 22) & 0x3FF;
		x = result[final_candidate][4];
		if ((x & 0x00000200) != 0)
			x = x | 0xFFFFFC00;
		tx1_a = (x * oldval_1) >> 8;
		rtl_set_bbreg(hw, ROFDM0_XBTXIQIMBALANCE, 0x3FF, tx1_a);
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
		rtl_set_bbreg(hw, ROFDM0_AGCRSSITABLE, 0x0000F000, reg);
	}
}

static void _rtl92c_phy_save_adda_registers(struct ieee80211_hw *hw,
					    u32 *addareg, u32 *addabackup,
					    u32 registernum)
{
	u32 i;

	for (i = 0; i < registernum; i++)
		addabackup[i] = rtl_get_bbreg(hw, addareg[i], MASKDWORD);
}

static void _rtl92c_phy_save_mac_registers(struct ieee80211_hw *hw,
					   u32 *macreg, u32 *macbackup)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 i;

	for (i = 0; i < (IQK_MAC_REG_NUM - 1); i++)
		macbackup[i] = rtl_read_byte(rtlpriv, macreg[i]);
	macbackup[i] = rtl_read_dword(rtlpriv, macreg[i]);
}

static void _rtl92c_phy_reload_adda_registers(struct ieee80211_hw *hw,
					      u32 *addareg, u32 *addabackup,
					      u32 regiesternum)
{
	u32 i;

	for (i = 0; i < regiesternum; i++)
		rtl_set_bbreg(hw, addareg[i], MASKDWORD, addabackup[i]);
}

static void _rtl92c_phy_reload_mac_registers(struct ieee80211_hw *hw,
					     u32 *macreg, u32 *macbackup)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 i;

	for (i = 0; i < (IQK_MAC_REG_NUM - 1); i++)
		rtl_write_byte(rtlpriv, macreg[i], (u8) macbackup[i]);
	rtl_write_dword(rtlpriv, macreg[i], macbackup[i]);
}

static void _rtl92c_phy_path_adda_on(struct ieee80211_hw *hw,
				     u32 *addareg, bool is_patha_on, bool is2t)
{
	u32 pathOn;
	u32 i;

	pathOn = is_patha_on ? 0x04db25a4 : 0x0b1b25a4;
	if (false == is2t) {
		pathOn = 0x0bdb25a0;
		rtl_set_bbreg(hw, addareg[0], MASKDWORD, 0x0b1b25a0);
	} else {
		rtl_set_bbreg(hw, addareg[0], MASKDWORD, pathOn);
	}

	for (i = 1; i < IQK_ADDA_REG_NUM; i++)
		rtl_set_bbreg(hw, addareg[i], MASKDWORD, pathOn);
}

static void _rtl92c_phy_mac_setting_calibration(struct ieee80211_hw *hw,
						u32 *macreg, u32 *macbackup)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 i;

	rtl_write_byte(rtlpriv, macreg[0], 0x3F);

	for (i = 1; i < (IQK_MAC_REG_NUM - 1); i++)
		rtl_write_byte(rtlpriv, macreg[i],
			       (u8) (macbackup[i] & (~BIT(3))));
	rtl_write_byte(rtlpriv, macreg[i], (u8) (macbackup[i] & (~BIT(5))));
}

static void _rtl92c_phy_path_a_standby(struct ieee80211_hw *hw)
{
	rtl_set_bbreg(hw, 0xe28, MASKDWORD, 0x0);
	rtl_set_bbreg(hw, 0x840, MASKDWORD, 0x00010000);
	rtl_set_bbreg(hw, 0xe28, MASKDWORD, 0x80800000);
}

static void _rtl92c_phy_pi_mode_switch(struct ieee80211_hw *hw, bool pi_mode)
{
	u32 mode;

	mode = pi_mode ? 0x01000100 : 0x01000000;
	rtl_set_bbreg(hw, 0x820, MASKDWORD, mode);
	rtl_set_bbreg(hw, 0x828, MASKDWORD, mode);
}

static bool _rtl92c_phy_simularity_compare(struct ieee80211_hw *hw,
					   long result[][8], u8 c1, u8 c2)
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
					simularity_bitmap = simularity_bitmap |
					    (1 << i);
			} else
				simularity_bitmap =
				    simularity_bitmap | (1 << i);
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

static void _rtl92c_phy_iq_calibrate(struct ieee80211_hw *hw,
				     long result[][8], u8 t, bool is2t)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u32 i;
	u8 patha_ok, pathb_ok;
	u32 adda_reg[IQK_ADDA_REG_NUM] = {
		0x85c, 0xe6c, 0xe70, 0xe74,
		0xe78, 0xe7c, 0xe80, 0xe84,
		0xe88, 0xe8c, 0xed0, 0xed4,
		0xed8, 0xedc, 0xee0, 0xeec
	};

	u32 iqk_mac_reg[IQK_MAC_REG_NUM] = {
		0x522, 0x550, 0x551, 0x040
	};

	const u32 retrycount = 2;

	u32 bbvalue;

	if (t == 0) {
		bbvalue = rtl_get_bbreg(hw, 0x800, MASKDWORD);

		_rtl92c_phy_save_adda_registers(hw, adda_reg,
						rtlphy->adda_backup, 16);
		_rtl92c_phy_save_mac_registers(hw, iqk_mac_reg,
					       rtlphy->iqk_mac_backup);
	}
	_rtl92c_phy_path_adda_on(hw, adda_reg, true, is2t);
	if (t == 0) {
		rtlphy->rfpi_enable = (u8) rtl_get_bbreg(hw,
						   RFPGA0_XA_HSSIPARAMETER1,
						   BIT(8));
	}
	if (!rtlphy->rfpi_enable)
		_rtl92c_phy_pi_mode_switch(hw, true);
	if (t == 0) {
		rtlphy->reg_c04 = rtl_get_bbreg(hw, 0xc04, MASKDWORD);
		rtlphy->reg_c08 = rtl_get_bbreg(hw, 0xc08, MASKDWORD);
		rtlphy->reg_874 = rtl_get_bbreg(hw, 0x874, MASKDWORD);
	}
	rtl_set_bbreg(hw, 0xc04, MASKDWORD, 0x03a05600);
	rtl_set_bbreg(hw, 0xc08, MASKDWORD, 0x000800e4);
	rtl_set_bbreg(hw, 0x874, MASKDWORD, 0x22204000);
	if (is2t) {
		rtl_set_bbreg(hw, 0x840, MASKDWORD, 0x00010000);
		rtl_set_bbreg(hw, 0x844, MASKDWORD, 0x00010000);
	}
	_rtl92c_phy_mac_setting_calibration(hw, iqk_mac_reg,
					    rtlphy->iqk_mac_backup);
	rtl_set_bbreg(hw, 0xb68, MASKDWORD, 0x00080000);
	if (is2t)
		rtl_set_bbreg(hw, 0xb6c, MASKDWORD, 0x00080000);
	rtl_set_bbreg(hw, 0xe28, MASKDWORD, 0x80800000);
	rtl_set_bbreg(hw, 0xe40, MASKDWORD, 0x01007c00);
	rtl_set_bbreg(hw, 0xe44, MASKDWORD, 0x01004800);
	for (i = 0; i < retrycount; i++) {
		patha_ok = _rtl92c_phy_path_a_iqk(hw, is2t);
		if (patha_ok == 0x03) {
			result[t][0] = (rtl_get_bbreg(hw, 0xe94, MASKDWORD) &
					0x3FF0000) >> 16;
			result[t][1] = (rtl_get_bbreg(hw, 0xe9c, MASKDWORD) &
					0x3FF0000) >> 16;
			result[t][2] = (rtl_get_bbreg(hw, 0xea4, MASKDWORD) &
					0x3FF0000) >> 16;
			result[t][3] = (rtl_get_bbreg(hw, 0xeac, MASKDWORD) &
					0x3FF0000) >> 16;
			break;
		} else if (i == (retrycount - 1) && patha_ok == 0x01)
			result[t][0] = (rtl_get_bbreg(hw, 0xe94,
						      MASKDWORD) & 0x3FF0000) >>
						      16;
		result[t][1] =
		    (rtl_get_bbreg(hw, 0xe9c, MASKDWORD) & 0x3FF0000) >> 16;

	}

	if (is2t) {
		_rtl92c_phy_path_a_standby(hw);
		_rtl92c_phy_path_adda_on(hw, adda_reg, false, is2t);
		for (i = 0; i < retrycount; i++) {
			pathb_ok = _rtl92c_phy_path_b_iqk(hw);
			if (pathb_ok == 0x03) {
				result[t][4] = (rtl_get_bbreg(hw,
						      0xeb4,
						      MASKDWORD) &
						0x3FF0000) >> 16;
				result[t][5] =
				    (rtl_get_bbreg(hw, 0xebc, MASKDWORD) &
				     0x3FF0000) >> 16;
				result[t][6] =
				    (rtl_get_bbreg(hw, 0xec4, MASKDWORD) &
				     0x3FF0000) >> 16;
				result[t][7] =
				    (rtl_get_bbreg(hw, 0xecc, MASKDWORD) &
				     0x3FF0000) >> 16;
				break;
			} else if (i == (retrycount - 1) && pathb_ok == 0x01) {
				result[t][4] = (rtl_get_bbreg(hw,
						      0xeb4,
						      MASKDWORD) &
						0x3FF0000) >> 16;
			}
			result[t][5] = (rtl_get_bbreg(hw, 0xebc, MASKDWORD) &
					0x3FF0000) >> 16;
		}
	}
	rtl_set_bbreg(hw, 0xc04, MASKDWORD, rtlphy->reg_c04);
	rtl_set_bbreg(hw, 0x874, MASKDWORD, rtlphy->reg_874);
	rtl_set_bbreg(hw, 0xc08, MASKDWORD, rtlphy->reg_c08);
	rtl_set_bbreg(hw, 0xe28, MASKDWORD, 0);
	rtl_set_bbreg(hw, 0x840, MASKDWORD, 0x00032ed3);
	if (is2t)
		rtl_set_bbreg(hw, 0x844, MASKDWORD, 0x00032ed3);
	if (t != 0) {
		if (!rtlphy->rfpi_enable)
			_rtl92c_phy_pi_mode_switch(hw, false);
		_rtl92c_phy_reload_adda_registers(hw, adda_reg,
						  rtlphy->adda_backup, 16);
		_rtl92c_phy_reload_mac_registers(hw, iqk_mac_reg,
						 rtlphy->iqk_mac_backup);
	}
}

static void _rtl92c_phy_ap_calibrate(struct ieee80211_hw *hw,
				     char delta, bool is2t)
{
	/* This routine is deliberately dummied out for later fixes */
#if 0
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));

	u32 reg_d[PATH_NUM];
	u32 tmpreg, index, offset, path, i, pathbound = PATH_NUM, apkbound;

	u32 bb_backup[APK_BB_REG_NUM];
	u32 bb_reg[APK_BB_REG_NUM] = {
		0x904, 0xc04, 0x800, 0xc08, 0x874
	};
	u32 bb_ap_mode[APK_BB_REG_NUM] = {
		0x00000020, 0x00a05430, 0x02040000,
		0x000800e4, 0x00204000
	};
	u32 bb_normal_ap_mode[APK_BB_REG_NUM] = {
		0x00000020, 0x00a05430, 0x02040000,
		0x000800e4, 0x22204000
	};

	u32 afe_backup[APK_AFE_REG_NUM];
	u32 afe_reg[APK_AFE_REG_NUM] = {
		0x85c, 0xe6c, 0xe70, 0xe74, 0xe78,
		0xe7c, 0xe80, 0xe84, 0xe88, 0xe8c,
		0xed0, 0xed4, 0xed8, 0xedc, 0xee0,
		0xeec
	};

	u32 mac_backup[IQK_MAC_REG_NUM];
	u32 mac_reg[IQK_MAC_REG_NUM] = {
		0x522, 0x550, 0x551, 0x040
	};

	u32 apk_rf_init_value[PATH_NUM][APK_BB_REG_NUM] = {
		{0x0852c, 0x1852c, 0x5852c, 0x1852c, 0x5852c},
		{0x2852e, 0x0852e, 0x3852e, 0x0852e, 0x0852e}
	};

	u32 apk_normal_rf_init_value[PATH_NUM][APK_BB_REG_NUM] = {
		{0x0852c, 0x0a52c, 0x3a52c, 0x5a52c, 0x5a52c},
		{0x0852c, 0x0a52c, 0x5a52c, 0x5a52c, 0x5a52c}
	};

	u32 apk_rf_value_0[PATH_NUM][APK_BB_REG_NUM] = {
		{0x52019, 0x52014, 0x52013, 0x5200f, 0x5208d},
		{0x5201a, 0x52019, 0x52016, 0x52033, 0x52050}
	};

	u32 apk_normal_rf_value_0[PATH_NUM][APK_BB_REG_NUM] = {
		{0x52019, 0x52017, 0x52010, 0x5200d, 0x5206a},
		{0x52019, 0x52017, 0x52010, 0x5200d, 0x5206a}
	};

	u32 afe_on_off[PATH_NUM] = {
		0x04db25a4, 0x0b1b25a4
	};

	u32 apk_offset[PATH_NUM] = { 0xb68, 0xb6c };

	u32 apk_normal_offset[PATH_NUM] = { 0xb28, 0xb98 };

	u32 apk_value[PATH_NUM] = { 0x92fc0000, 0x12fc0000 };

	u32 apk_normal_value[PATH_NUM] = { 0x92680000, 0x12680000 };

	const char apk_delta_mapping[APK_BB_REG_NUM][13] = {
		{-4, -3, -2, -2, -1, -1, 0, 1, 2, 3, 4, 5, 6},
		{-4, -3, -2, -2, -1, -1, 0, 1, 2, 3, 4, 5, 6},
		{-6, -4, -2, -2, -1, -1, 0, 1, 2, 3, 4, 5, 6},
		{-1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 4, 5, 6},
		{-11, -9, -7, -5, -3, -1, 0, 0, 0, 0, 0, 0, 0}
	};

	const u32 apk_normal_setting_value_1[13] = {
		0x01017018, 0xf7ed8f84, 0x1b1a1816, 0x2522201e, 0x322e2b28,
		0x433f3a36, 0x5b544e49, 0x7b726a62, 0xa69a8f84, 0xdfcfc0b3,
		0x12680000, 0x00880000, 0x00880000
	};

	const u32 apk_normal_setting_value_2[16] = {
		0x01c7021d, 0x01670183, 0x01000123, 0x00bf00e2, 0x008d00a3,
		0x0068007b, 0x004d0059, 0x003a0042, 0x002b0031, 0x001f0025,
		0x0017001b, 0x00110014, 0x000c000f, 0x0009000b, 0x00070008,
		0x00050006
	};

	const u32 apk_result[PATH_NUM][APK_BB_REG_NUM];

	long bb_offset, delta_v, delta_offset;

	if (!is2t)
		pathbound = 1;

	for (index = 0; index < PATH_NUM; index++) {
		apk_offset[index] = apk_normal_offset[index];
		apk_value[index] = apk_normal_value[index];
		afe_on_off[index] = 0x6fdb25a4;
	}

	for (index = 0; index < APK_BB_REG_NUM; index++) {
		for (path = 0; path < pathbound; path++) {
			apk_rf_init_value[path][index] =
			    apk_normal_rf_init_value[path][index];
			apk_rf_value_0[path][index] =
			    apk_normal_rf_value_0[path][index];
		}
		bb_ap_mode[index] = bb_normal_ap_mode[index];

		apkbound = 6;
	}

	for (index = 0; index < APK_BB_REG_NUM; index++) {
		if (index == 0)
			continue;
		bb_backup[index] = rtl_get_bbreg(hw, bb_reg[index], MASKDWORD);
	}

	_rtl92c_phy_save_mac_registers(hw, mac_reg, mac_backup);

	_rtl92c_phy_save_adda_registers(hw, afe_reg, afe_backup, 16);

	for (path = 0; path < pathbound; path++) {
		if (path == RF90_PATH_A) {
			offset = 0xb00;
			for (index = 0; index < 11; index++) {
				rtl_set_bbreg(hw, offset, MASKDWORD,
					      apk_normal_setting_value_1
					      [index]);

				offset += 0x04;
			}

			rtl_set_bbreg(hw, 0xb98, MASKDWORD, 0x12680000);

			offset = 0xb68;
			for (; index < 13; index++) {
				rtl_set_bbreg(hw, offset, MASKDWORD,
					      apk_normal_setting_value_1
					      [index]);

				offset += 0x04;
			}

			rtl_set_bbreg(hw, 0xe28, MASKDWORD, 0x40000000);

			offset = 0xb00;
			for (index = 0; index < 16; index++) {
				rtl_set_bbreg(hw, offset, MASKDWORD,
					      apk_normal_setting_value_2
					      [index]);

				offset += 0x04;
			}
			rtl_set_bbreg(hw, 0xe28, MASKDWORD, 0x00000000);
		} else if (path == RF90_PATH_B) {
			offset = 0xb70;
			for (index = 0; index < 10; index++) {
				rtl_set_bbreg(hw, offset, MASKDWORD,
					      apk_normal_setting_value_1
					      [index]);

				offset += 0x04;
			}
			rtl_set_bbreg(hw, 0xb28, MASKDWORD, 0x12680000);
			rtl_set_bbreg(hw, 0xb98, MASKDWORD, 0x12680000);

			offset = 0xb68;
			index = 11;
			for (; index < 13; index++) {
				rtl_set_bbreg(hw, offset, MASKDWORD,
					      apk_normal_setting_value_1
					      [index]);

				offset += 0x04;
			}

			rtl_set_bbreg(hw, 0xe28, MASKDWORD, 0x40000000);

			offset = 0xb60;
			for (index = 0; index < 16; index++) {
				rtl_set_bbreg(hw, offset, MASKDWORD,
					      apk_normal_setting_value_2
					      [index]);

				offset += 0x04;
			}
			rtl_set_bbreg(hw, 0xe28, MASKDWORD, 0x00000000);
		}

		reg_d[path] = rtl_get_rfreg(hw, (enum radio_path)path,
					    0xd, MASKDWORD);

		for (index = 0; index < APK_AFE_REG_NUM; index++)
			rtl_set_bbreg(hw, afe_reg[index], MASKDWORD,
				      afe_on_off[path]);

		if (path == RF90_PATH_A) {
			for (index = 0; index < APK_BB_REG_NUM; index++) {
				if (index == 0)
					continue;
				rtl_set_bbreg(hw, bb_reg[index], MASKDWORD,
					      bb_ap_mode[index]);
			}
		}

		_rtl92c_phy_mac_setting_calibration(hw, mac_reg, mac_backup);

		if (path == 0) {
			rtl_set_rfreg(hw, RF90_PATH_B, 0x0, MASKDWORD, 0x10000);
		} else {
			rtl_set_rfreg(hw, RF90_PATH_A, 0x00, MASKDWORD,
				      0x10000);
			rtl_set_rfreg(hw, RF90_PATH_A, 0x10, MASKDWORD,
				      0x1000f);
			rtl_set_rfreg(hw, RF90_PATH_A, 0x11, MASKDWORD,
				      0x20103);
		}

		delta_offset = ((delta + 14) / 2);
		if (delta_offset < 0)
			delta_offset = 0;
		else if (delta_offset > 12)
			delta_offset = 12;

		for (index = 0; index < APK_BB_REG_NUM; index++) {
			if (index != 1)
				continue;

			tmpreg = apk_rf_init_value[path][index];

			if (!rtlefuse->apk_thermalmeterignore) {
				bb_offset = (tmpreg & 0xF0000) >> 16;

				if (!(tmpreg & BIT(15)))
					bb_offset = -bb_offset;

				delta_v =
				    apk_delta_mapping[index][delta_offset];

				bb_offset += delta_v;

				if (bb_offset < 0) {
					tmpreg = tmpreg & (~BIT(15));
					bb_offset = -bb_offset;
				} else {
					tmpreg = tmpreg | BIT(15);
				}

				tmpreg =
				    (tmpreg & 0xFFF0FFFF) | (bb_offset << 16);
			}

			rtl_set_rfreg(hw, (enum radio_path)path, 0xc,
				      MASKDWORD, 0x8992e);
			rtl_set_rfreg(hw, (enum radio_path)path, 0x0,
				      MASKDWORD, apk_rf_value_0[path][index]);
			rtl_set_rfreg(hw, (enum radio_path)path, 0xd,
				      MASKDWORD, tmpreg);

			i = 0;
			do {
				rtl_set_bbreg(hw, 0xe28, MASKDWORD, 0x80000000);
				rtl_set_bbreg(hw, apk_offset[path],
					      MASKDWORD, apk_value[0]);
				RTPRINT(rtlpriv, FINIT, INIT_IQK,
					("PHY_APCalibrate() offset 0x%x "
					 "value 0x%x\n",
					 apk_offset[path],
					 rtl_get_bbreg(hw, apk_offset[path],
						       MASKDWORD)));

				mdelay(3);

				rtl_set_bbreg(hw, apk_offset[path],
					      MASKDWORD, apk_value[1]);
				RTPRINT(rtlpriv, FINIT, INIT_IQK,
					("PHY_APCalibrate() offset 0x%x "
					 "value 0x%x\n",
					 apk_offset[path],
					 rtl_get_bbreg(hw, apk_offset[path],
						       MASKDWORD)));

				mdelay(20);

				rtl_set_bbreg(hw, 0xe28, MASKDWORD, 0x00000000);

				if (path == RF90_PATH_A)
					tmpreg = rtl_get_bbreg(hw, 0xbd8,
							       0x03E00000);
				else
					tmpreg = rtl_get_bbreg(hw, 0xbd8,
							       0xF8000000);

				RTPRINT(rtlpriv, FINIT, INIT_IQK,
					("PHY_APCalibrate() offset "
					 "0xbd8[25:21] %x\n", tmpreg));

				i++;

			} while (tmpreg > apkbound && i < 4);

			apk_result[path][index] = tmpreg;
		}
	}

	_rtl92c_phy_reload_mac_registers(hw, mac_reg, mac_backup);

	for (index = 0; index < APK_BB_REG_NUM; index++) {
		if (index == 0)
			continue;
		rtl_set_bbreg(hw, bb_reg[index], MASKDWORD, bb_backup[index]);
	}

	_rtl92c_phy_reload_adda_registers(hw, afe_reg, afe_backup, 16);

	for (path = 0; path < pathbound; path++) {
		rtl_set_rfreg(hw, (enum radio_path)path, 0xd,
			      MASKDWORD, reg_d[path]);

		if (path == RF90_PATH_B) {
			rtl_set_rfreg(hw, RF90_PATH_A, 0x10, MASKDWORD,
				      0x1000f);
			rtl_set_rfreg(hw, RF90_PATH_A, 0x11, MASKDWORD,
				      0x20101);
		}

		if (apk_result[path][1] > 6)
			apk_result[path][1] = 6;
	}

	for (path = 0; path < pathbound; path++) {
		rtl_set_rfreg(hw, (enum radio_path)path, 0x3, MASKDWORD,
			      ((apk_result[path][1] << 15) |
			       (apk_result[path][1] << 10) |
			       (apk_result[path][1] << 5) |
			       apk_result[path][1]));

		if (path == RF90_PATH_A)
			rtl_set_rfreg(hw, (enum radio_path)path, 0x4, MASKDWORD,
				      ((apk_result[path][1] << 15) |
				       (apk_result[path][1] << 10) |
				       (0x00 << 5) | 0x05));
		else
			rtl_set_rfreg(hw, (enum radio_path)path, 0x4, MASKDWORD,
				      ((apk_result[path][1] << 15) |
				       (apk_result[path][1] << 10) |
				       (0x02 << 5) | 0x05));

		rtl_set_rfreg(hw, (enum radio_path)path, 0xe, MASKDWORD,
			      ((0x08 << 15) | (0x08 << 10) | (0x08 << 5) |
			       0x08));

	}

	rtlphy->apk_done = true;
#endif
}

static void _rtl92c_phy_set_rfpath_switch(struct ieee80211_hw *hw,
					  bool bmain, bool is2t)
{
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	if (is_hal_stop(rtlhal)) {
		rtl_set_bbreg(hw, REG_LEDCFG0, BIT(23), 0x01);
		rtl_set_bbreg(hw, rFPGA0_XAB_RFPARAMETER, BIT(13), 0x01);
	}
	if (is2t) {
		if (bmain)
			rtl_set_bbreg(hw, RFPGA0_XB_RFINTERFACEOE,
				      BIT(5) | BIT(6), 0x1);
		else
			rtl_set_bbreg(hw, RFPGA0_XB_RFINTERFACEOE,
				      BIT(5) | BIT(6), 0x2);
	} else {
		if (bmain)
			rtl_set_bbreg(hw, RFPGA0_XA_RFINTERFACEOE, 0x300, 0x2);
		else
			rtl_set_bbreg(hw, RFPGA0_XA_RFINTERFACEOE, 0x300, 0x1);

	}
}

#undef IQK_ADDA_REG_NUM
#undef IQK_DELAY_TIME

void rtl92c_phy_iq_calibrate(struct ieee80211_hw *hw, bool recovery)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	long result[4][8];
	u8 i, final_candidate;
	bool patha_ok, pathb_ok;
	long reg_e94, reg_e9c, reg_ea4, reg_eac, reg_eb4, reg_ebc, reg_ec4,
	    reg_ecc, reg_tmp = 0;
	bool is12simular, is13simular, is23simular;
	bool start_conttx = false, singletone = false;
	u32 iqk_bb_reg[10] = {
		ROFDM0_XARXIQIMBALANCE,
		ROFDM0_XBRXIQIMBALANCE,
		ROFDM0_ECCATHRESHOLD,
		ROFDM0_AGCRSSITABLE,
		ROFDM0_XATXIQIMBALANCE,
		ROFDM0_XBTXIQIMBALANCE,
		ROFDM0_XCTXIQIMBALANCE,
		ROFDM0_XCTXAFE,
		ROFDM0_XDTXAFE,
		ROFDM0_RXIQEXTANTA
	};

	if (recovery) {
		_rtl92c_phy_reload_adda_registers(hw,
						  iqk_bb_reg,
						  rtlphy->iqk_bb_backup, 10);
		return;
	}
	if (start_conttx || singletone)
		return;
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
		if (IS_92C_SERIAL(rtlhal->version))
			_rtl92c_phy_iq_calibrate(hw, result, i, true);
		else
			_rtl92c_phy_iq_calibrate(hw, result, i, false);
		if (i == 1) {
			is12simular = _rtl92c_phy_simularity_compare(hw,
								     result, 0,
								     1);
			if (is12simular) {
				final_candidate = 0;
				break;
			}
		}
		if (i == 2) {
			is13simular = _rtl92c_phy_simularity_compare(hw,
								     result, 0,
								     2);
			if (is13simular) {
				final_candidate = 0;
				break;
			}
			is23simular = _rtl92c_phy_simularity_compare(hw,
								     result, 1,
								     2);
			if (is23simular)
				final_candidate = 1;
			else {
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
		rtlphy->reg_e94 = reg_e94 = result[final_candidate][0];
		rtlphy->reg_e9c = reg_e9c = result[final_candidate][1];
		reg_ea4 = result[final_candidate][2];
		reg_eac = result[final_candidate][3];
		rtlphy->reg_eb4 = reg_eb4 = result[final_candidate][4];
		rtlphy->reg_ebc = reg_ebc = result[final_candidate][5];
		reg_ec4 = result[final_candidate][6];
		reg_ecc = result[final_candidate][7];
		patha_ok = pathb_ok = true;
	} else {
		rtlphy->reg_e94 = rtlphy->reg_eb4 = 0x100;
		rtlphy->reg_e9c = rtlphy->reg_ebc = 0x0;
	}
	if (reg_e94 != 0) /*&&(reg_ea4 != 0) */
		_rtl92c_phy_path_a_fill_iqk_matrix(hw, patha_ok, result,
						   final_candidate,
						   (reg_ea4 == 0));
	if (IS_92C_SERIAL(rtlhal->version)) {
		if (reg_eb4 != 0) /*&&(reg_ec4 != 0) */
			_rtl92c_phy_path_b_fill_iqk_matrix(hw, pathb_ok,
							   result,
							   final_candidate,
							   (reg_ec4 == 0));
	}
	_rtl92c_phy_save_adda_registers(hw, iqk_bb_reg,
					rtlphy->iqk_bb_backup, 10);
}
EXPORT_SYMBOL(rtl92c_phy_iq_calibrate);

void rtl92c_phy_lc_calibrate(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	bool start_conttx = false, singletone = false;

	if (start_conttx || singletone)
		return;
	if (IS_92C_SERIAL(rtlhal->version))
		rtlpriv->cfg->ops->phy_lc_calibrate(hw, true);
	else
		rtlpriv->cfg->ops->phy_lc_calibrate(hw, false);
}
EXPORT_SYMBOL(rtl92c_phy_lc_calibrate);

void rtl92c_phy_ap_calibrate(struct ieee80211_hw *hw, char delta)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	if (rtlphy->apk_done)
		return;
	if (IS_92C_SERIAL(rtlhal->version))
		_rtl92c_phy_ap_calibrate(hw, delta, true);
	else
		_rtl92c_phy_ap_calibrate(hw, delta, false);
}
EXPORT_SYMBOL(rtl92c_phy_ap_calibrate);

void rtl92c_phy_set_rfpath_switch(struct ieee80211_hw *hw, bool bmain)
{
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	if (IS_92C_SERIAL(rtlhal->version))
		_rtl92c_phy_set_rfpath_switch(hw, bmain, true);
	else
		_rtl92c_phy_set_rfpath_switch(hw, bmain, false);
}
EXPORT_SYMBOL(rtl92c_phy_set_rfpath_switch);

bool rtl92c_phy_set_io_cmd(struct ieee80211_hw *hw, enum io_type iotype)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	bool postprocessing = false;

	RT_TRACE(rtlpriv, COMP_CMD, DBG_TRACE,
		 ("-->IO Cmd(%#x), set_io_inprogress(%d)\n",
		  iotype, rtlphy->set_io_inprogress));
	do {
		switch (iotype) {
		case IO_CMD_RESUME_DM_BY_SCAN:
			RT_TRACE(rtlpriv, COMP_CMD, DBG_TRACE,
				 ("[IO CMD] Resume DM after scan.\n"));
			postprocessing = true;
			break;
		case IO_CMD_PAUSE_DM_BY_SCAN:
			RT_TRACE(rtlpriv, COMP_CMD, DBG_TRACE,
				 ("[IO CMD] Pause DM before scan.\n"));
			postprocessing = true;
			break;
		default:
			RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
				 ("switch case not process\n"));
			break;
		}
	} while (false);
	if (postprocessing && !rtlphy->set_io_inprogress) {
		rtlphy->set_io_inprogress = true;
		rtlphy->current_io_type = iotype;
	} else {
		return false;
	}
	rtl92c_phy_set_io(hw);
	RT_TRACE(rtlpriv, COMP_CMD, DBG_TRACE, ("<--IO Type(%#x)\n", iotype));
	return true;
}
EXPORT_SYMBOL(rtl92c_phy_set_io_cmd);

void rtl92c_phy_set_io(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);

	RT_TRACE(rtlpriv, COMP_CMD, DBG_TRACE,
		 ("--->Cmd(%#x), set_io_inprogress(%d)\n",
		  rtlphy->current_io_type, rtlphy->set_io_inprogress));
	switch (rtlphy->current_io_type) {
	case IO_CMD_RESUME_DM_BY_SCAN:
		dm_digtable.cur_igvalue = rtlphy->initgain_backup.xaagccore1;
		rtl92c_dm_write_dig(hw);
		rtl92c_phy_set_txpower_level(hw, rtlphy->current_channel);
		break;
	case IO_CMD_PAUSE_DM_BY_SCAN:
		rtlphy->initgain_backup.xaagccore1 = dm_digtable.cur_igvalue;
		dm_digtable.cur_igvalue = 0x17;
		rtl92c_dm_write_dig(hw);
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 ("switch case not process\n"));
		break;
	}
	rtlphy->set_io_inprogress = false;
	RT_TRACE(rtlpriv, COMP_CMD, DBG_TRACE,
		 ("<---(%#x)\n", rtlphy->current_io_type));
}
EXPORT_SYMBOL(rtl92c_phy_set_io);

void rtl92ce_phy_set_rf_on(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_write_byte(rtlpriv, REG_SPS0_CTRL, 0x2b);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE3);
	rtl_write_byte(rtlpriv, REG_APSD_CTRL, 0x00);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE2);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE3);
	rtl_write_byte(rtlpriv, REG_TXPAUSE, 0x00);
}
EXPORT_SYMBOL(rtl92ce_phy_set_rf_on);

void _rtl92c_phy_set_rf_sleep(struct ieee80211_hw *hw)
{
	u32 u4b_tmp;
	u8 delay = 5;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_write_byte(rtlpriv, REG_TXPAUSE, 0xFF);
	rtl_set_rfreg(hw, RF90_PATH_A, 0x00, RFREG_OFFSET_MASK, 0x00);
	rtl_write_byte(rtlpriv, REG_APSD_CTRL, 0x40);
	u4b_tmp = rtl_get_rfreg(hw, RF90_PATH_A, 0, RFREG_OFFSET_MASK);
	while (u4b_tmp != 0 && delay > 0) {
		rtl_write_byte(rtlpriv, REG_APSD_CTRL, 0x0);
		rtl_set_rfreg(hw, RF90_PATH_A, 0x00, RFREG_OFFSET_MASK, 0x00);
		rtl_write_byte(rtlpriv, REG_APSD_CTRL, 0x40);
		u4b_tmp = rtl_get_rfreg(hw, RF90_PATH_A, 0, RFREG_OFFSET_MASK);
		delay--;
	}
	if (delay == 0) {
		rtl_write_byte(rtlpriv, REG_APSD_CTRL, 0x00);
		rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE2);
		rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE3);
		rtl_write_byte(rtlpriv, REG_TXPAUSE, 0x00);
		RT_TRACE(rtlpriv, COMP_POWER, DBG_TRACE,
			 ("Switch RF timeout !!!.\n"));
		return;
	}
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE2);
	rtl_write_byte(rtlpriv, REG_SPS0_CTRL, 0x22);
}
EXPORT_SYMBOL(_rtl92c_phy_set_rf_sleep);
