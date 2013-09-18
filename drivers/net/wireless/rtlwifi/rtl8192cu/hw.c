/******************************************************************************
 *
 * Copyright(c) 2009-2012  Realtek Corporation. All rights reserved.
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
#include "../efuse.h"
#include "../base.h"
#include "../cam.h"
#include "../ps.h"
#include "../usb.h"
#include "reg.h"
#include "def.h"
#include "phy.h"
#include "mac.h"
#include "dm.h"
#include "hw.h"
#include "../rtl8192ce/hw.h"
#include "trx.h"
#include "led.h"
#include "table.h"

static void _rtl92cu_phy_param_tab_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtlpriv);

	rtlphy->hwparam_tables[MAC_REG].length = RTL8192CUMAC_2T_ARRAYLENGTH;
	rtlphy->hwparam_tables[MAC_REG].pdata = RTL8192CUMAC_2T_ARRAY;
	if (IS_HIGHT_PA(rtlefuse->board_type)) {
		rtlphy->hwparam_tables[PHY_REG_PG].length =
			RTL8192CUPHY_REG_Array_PG_HPLength;
		rtlphy->hwparam_tables[PHY_REG_PG].pdata =
			RTL8192CUPHY_REG_Array_PG_HP;
	} else {
		rtlphy->hwparam_tables[PHY_REG_PG].length =
			RTL8192CUPHY_REG_ARRAY_PGLENGTH;
		rtlphy->hwparam_tables[PHY_REG_PG].pdata =
			RTL8192CUPHY_REG_ARRAY_PG;
	}
	/* 2T */
	rtlphy->hwparam_tables[PHY_REG_2T].length =
			RTL8192CUPHY_REG_2TARRAY_LENGTH;
	rtlphy->hwparam_tables[PHY_REG_2T].pdata =
			RTL8192CUPHY_REG_2TARRAY;
	rtlphy->hwparam_tables[RADIOA_2T].length =
			RTL8192CURADIOA_2TARRAYLENGTH;
	rtlphy->hwparam_tables[RADIOA_2T].pdata =
			RTL8192CURADIOA_2TARRAY;
	rtlphy->hwparam_tables[RADIOB_2T].length =
			RTL8192CURADIOB_2TARRAYLENGTH;
	rtlphy->hwparam_tables[RADIOB_2T].pdata =
			RTL8192CU_RADIOB_2TARRAY;
	rtlphy->hwparam_tables[AGCTAB_2T].length =
			RTL8192CUAGCTAB_2TARRAYLENGTH;
	rtlphy->hwparam_tables[AGCTAB_2T].pdata =
			RTL8192CUAGCTAB_2TARRAY;
	/* 1T */
	if (IS_HIGHT_PA(rtlefuse->board_type)) {
		rtlphy->hwparam_tables[PHY_REG_1T].length =
			RTL8192CUPHY_REG_1T_HPArrayLength;
		rtlphy->hwparam_tables[PHY_REG_1T].pdata =
			RTL8192CUPHY_REG_1T_HPArray;
		rtlphy->hwparam_tables[RADIOA_1T].length =
			RTL8192CURadioA_1T_HPArrayLength;
		rtlphy->hwparam_tables[RADIOA_1T].pdata =
			RTL8192CURadioA_1T_HPArray;
		rtlphy->hwparam_tables[RADIOB_1T].length =
			RTL8192CURADIOB_1TARRAYLENGTH;
		rtlphy->hwparam_tables[RADIOB_1T].pdata =
			RTL8192CU_RADIOB_1TARRAY;
		rtlphy->hwparam_tables[AGCTAB_1T].length =
			RTL8192CUAGCTAB_1T_HPArrayLength;
		rtlphy->hwparam_tables[AGCTAB_1T].pdata =
			Rtl8192CUAGCTAB_1T_HPArray;
	} else {
		rtlphy->hwparam_tables[PHY_REG_1T].length =
			 RTL8192CUPHY_REG_1TARRAY_LENGTH;
		rtlphy->hwparam_tables[PHY_REG_1T].pdata =
			RTL8192CUPHY_REG_1TARRAY;
		rtlphy->hwparam_tables[RADIOA_1T].length =
			RTL8192CURADIOA_1TARRAYLENGTH;
		rtlphy->hwparam_tables[RADIOA_1T].pdata =
			RTL8192CU_RADIOA_1TARRAY;
		rtlphy->hwparam_tables[RADIOB_1T].length =
			RTL8192CURADIOB_1TARRAYLENGTH;
		rtlphy->hwparam_tables[RADIOB_1T].pdata =
			RTL8192CU_RADIOB_1TARRAY;
		rtlphy->hwparam_tables[AGCTAB_1T].length =
			RTL8192CUAGCTAB_1TARRAYLENGTH;
		rtlphy->hwparam_tables[AGCTAB_1T].pdata =
			RTL8192CUAGCTAB_1TARRAY;
	}
}

static void _rtl92cu_read_txpower_info_from_hwpg(struct ieee80211_hw *hw,
						 bool autoload_fail,
						 u8 *hwinfo)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u8 rf_path, index, tempval;
	u16 i;

	for (rf_path = 0; rf_path < 2; rf_path++) {
		for (i = 0; i < 3; i++) {
			if (!autoload_fail) {
				rtlefuse->
				    eeprom_chnlarea_txpwr_cck[rf_path][i] =
				    hwinfo[EEPROM_TXPOWERCCK + rf_path * 3 + i];
				rtlefuse->
				    eeprom_chnlarea_txpwr_ht40_1s[rf_path][i] =
				    hwinfo[EEPROM_TXPOWERHT40_1S + rf_path * 3 +
					   i];
			} else {
				rtlefuse->
				    eeprom_chnlarea_txpwr_cck[rf_path][i] =
				    EEPROM_DEFAULT_TXPOWERLEVEL;
				rtlefuse->
				    eeprom_chnlarea_txpwr_ht40_1s[rf_path][i] =
				    EEPROM_DEFAULT_TXPOWERLEVEL;
			}
		}
	}
	for (i = 0; i < 3; i++) {
		if (!autoload_fail)
			tempval = hwinfo[EEPROM_TXPOWERHT40_2SDIFF + i];
		else
			tempval = EEPROM_DEFAULT_HT40_2SDIFF;
		rtlefuse->eprom_chnl_txpwr_ht40_2sdf[RF90_PATH_A][i] =
		    (tempval & 0xf);
		rtlefuse->eprom_chnl_txpwr_ht40_2sdf[RF90_PATH_B][i] =
		    ((tempval & 0xf0) >> 4);
	}
	for (rf_path = 0; rf_path < 2; rf_path++)
		for (i = 0; i < 3; i++)
			RTPRINT(rtlpriv, FINIT, INIT_EEPROM,
				"RF(%d) EEPROM CCK Area(%d) = 0x%x\n",
				rf_path, i,
				rtlefuse->
				eeprom_chnlarea_txpwr_cck[rf_path][i]);
	for (rf_path = 0; rf_path < 2; rf_path++)
		for (i = 0; i < 3; i++)
			RTPRINT(rtlpriv, FINIT, INIT_EEPROM,
				"RF(%d) EEPROM HT40 1S Area(%d) = 0x%x\n",
				rf_path, i,
				rtlefuse->
				eeprom_chnlarea_txpwr_ht40_1s[rf_path][i]);
	for (rf_path = 0; rf_path < 2; rf_path++)
		for (i = 0; i < 3; i++)
			RTPRINT(rtlpriv, FINIT, INIT_EEPROM,
				"RF(%d) EEPROM HT40 2S Diff Area(%d) = 0x%x\n",
				rf_path, i,
				rtlefuse->
				eprom_chnl_txpwr_ht40_2sdf[rf_path][i]);
	for (rf_path = 0; rf_path < 2; rf_path++) {
		for (i = 0; i < 14; i++) {
			index = _rtl92c_get_chnl_group((u8) i);
			rtlefuse->txpwrlevel_cck[rf_path][i] =
			    rtlefuse->eeprom_chnlarea_txpwr_cck[rf_path][index];
			rtlefuse->txpwrlevel_ht40_1s[rf_path][i] =
			    rtlefuse->
			    eeprom_chnlarea_txpwr_ht40_1s[rf_path][index];
			if ((rtlefuse->
			     eeprom_chnlarea_txpwr_ht40_1s[rf_path][index] -
			     rtlefuse->
			     eprom_chnl_txpwr_ht40_2sdf[rf_path][index])
			    > 0) {
				rtlefuse->txpwrlevel_ht40_2s[rf_path][i] =
				    rtlefuse->
				    eeprom_chnlarea_txpwr_ht40_1s[rf_path]
				    [index] - rtlefuse->
				    eprom_chnl_txpwr_ht40_2sdf[rf_path]
				    [index];
			} else {
				rtlefuse->txpwrlevel_ht40_2s[rf_path][i] = 0;
			}
		}
		for (i = 0; i < 14; i++) {
			RTPRINT(rtlpriv, FINIT, INIT_TXPOWER,
				"RF(%d)-Ch(%d) [CCK / HT40_1S / HT40_2S] = [0x%x / 0x%x / 0x%x]\n", rf_path, i,
				rtlefuse->txpwrlevel_cck[rf_path][i],
				rtlefuse->txpwrlevel_ht40_1s[rf_path][i],
				rtlefuse->txpwrlevel_ht40_2s[rf_path][i]);
		}
	}
	for (i = 0; i < 3; i++) {
		if (!autoload_fail) {
			rtlefuse->eeprom_pwrlimit_ht40[i] =
			    hwinfo[EEPROM_TXPWR_GROUP + i];
			rtlefuse->eeprom_pwrlimit_ht20[i] =
			    hwinfo[EEPROM_TXPWR_GROUP + 3 + i];
		} else {
			rtlefuse->eeprom_pwrlimit_ht40[i] = 0;
			rtlefuse->eeprom_pwrlimit_ht20[i] = 0;
		}
	}
	for (rf_path = 0; rf_path < 2; rf_path++) {
		for (i = 0; i < 14; i++) {
			index = _rtl92c_get_chnl_group((u8) i);
			if (rf_path == RF90_PATH_A) {
				rtlefuse->pwrgroup_ht20[rf_path][i] =
				    (rtlefuse->eeprom_pwrlimit_ht20[index]
				     & 0xf);
				rtlefuse->pwrgroup_ht40[rf_path][i] =
				    (rtlefuse->eeprom_pwrlimit_ht40[index]
				     & 0xf);
			} else if (rf_path == RF90_PATH_B) {
				rtlefuse->pwrgroup_ht20[rf_path][i] =
				    ((rtlefuse->eeprom_pwrlimit_ht20[index]
				      & 0xf0) >> 4);
				rtlefuse->pwrgroup_ht40[rf_path][i] =
				    ((rtlefuse->eeprom_pwrlimit_ht40[index]
				      & 0xf0) >> 4);
			}
			RTPRINT(rtlpriv, FINIT, INIT_TXPOWER,
				"RF-%d pwrgroup_ht20[%d] = 0x%x\n",
				rf_path, i,
				rtlefuse->pwrgroup_ht20[rf_path][i]);
			RTPRINT(rtlpriv, FINIT, INIT_TXPOWER,
				"RF-%d pwrgroup_ht40[%d] = 0x%x\n",
				rf_path, i,
				rtlefuse->pwrgroup_ht40[rf_path][i]);
		}
	}
	for (i = 0; i < 14; i++) {
		index = _rtl92c_get_chnl_group((u8) i);
		if (!autoload_fail)
			tempval = hwinfo[EEPROM_TXPOWERHT20DIFF + index];
		else
			tempval = EEPROM_DEFAULT_HT20_DIFF;
		rtlefuse->txpwr_ht20diff[RF90_PATH_A][i] = (tempval & 0xF);
		rtlefuse->txpwr_ht20diff[RF90_PATH_B][i] =
		    ((tempval >> 4) & 0xF);
		if (rtlefuse->txpwr_ht20diff[RF90_PATH_A][i] & BIT(3))
			rtlefuse->txpwr_ht20diff[RF90_PATH_A][i] |= 0xF0;
		if (rtlefuse->txpwr_ht20diff[RF90_PATH_B][i] & BIT(3))
			rtlefuse->txpwr_ht20diff[RF90_PATH_B][i] |= 0xF0;
		index = _rtl92c_get_chnl_group((u8) i);
		if (!autoload_fail)
			tempval = hwinfo[EEPROM_TXPOWER_OFDMDIFF + index];
		else
			tempval = EEPROM_DEFAULT_LEGACYHTTXPOWERDIFF;
		rtlefuse->txpwr_legacyhtdiff[RF90_PATH_A][i] = (tempval & 0xF);
		rtlefuse->txpwr_legacyhtdiff[RF90_PATH_B][i] =
		    ((tempval >> 4) & 0xF);
	}
	rtlefuse->legacy_ht_txpowerdiff =
	    rtlefuse->txpwr_legacyhtdiff[RF90_PATH_A][7];
	for (i = 0; i < 14; i++)
		RTPRINT(rtlpriv, FINIT, INIT_TXPOWER,
			"RF-A Ht20 to HT40 Diff[%d] = 0x%x\n",
			i, rtlefuse->txpwr_ht20diff[RF90_PATH_A][i]);
	for (i = 0; i < 14; i++)
		RTPRINT(rtlpriv, FINIT, INIT_TXPOWER,
			"RF-A Legacy to Ht40 Diff[%d] = 0x%x\n",
			i, rtlefuse->txpwr_legacyhtdiff[RF90_PATH_A][i]);
	for (i = 0; i < 14; i++)
		RTPRINT(rtlpriv, FINIT, INIT_TXPOWER,
			"RF-B Ht20 to HT40 Diff[%d] = 0x%x\n",
			i, rtlefuse->txpwr_ht20diff[RF90_PATH_B][i]);
	for (i = 0; i < 14; i++)
		RTPRINT(rtlpriv, FINIT, INIT_TXPOWER,
			"RF-B Legacy to HT40 Diff[%d] = 0x%x\n",
			i, rtlefuse->txpwr_legacyhtdiff[RF90_PATH_B][i]);
	if (!autoload_fail)
		rtlefuse->eeprom_regulatory = (hwinfo[RF_OPTION1] & 0x7);
	else
		rtlefuse->eeprom_regulatory = 0;
	RTPRINT(rtlpriv, FINIT, INIT_TXPOWER,
		"eeprom_regulatory = 0x%x\n", rtlefuse->eeprom_regulatory);
	if (!autoload_fail) {
		rtlefuse->eeprom_tssi[RF90_PATH_A] = hwinfo[EEPROM_TSSI_A];
		rtlefuse->eeprom_tssi[RF90_PATH_B] = hwinfo[EEPROM_TSSI_B];
	} else {
		rtlefuse->eeprom_tssi[RF90_PATH_A] = EEPROM_DEFAULT_TSSI;
		rtlefuse->eeprom_tssi[RF90_PATH_B] = EEPROM_DEFAULT_TSSI;
	}
	RTPRINT(rtlpriv, FINIT, INIT_TXPOWER,
		"TSSI_A = 0x%x, TSSI_B = 0x%x\n",
		rtlefuse->eeprom_tssi[RF90_PATH_A],
		rtlefuse->eeprom_tssi[RF90_PATH_B]);
	if (!autoload_fail)
		tempval = hwinfo[EEPROM_THERMAL_METER];
	else
		tempval = EEPROM_DEFAULT_THERMALMETER;
	rtlefuse->eeprom_thermalmeter = (tempval & 0x1f);
	if (rtlefuse->eeprom_thermalmeter < 0x06 ||
	    rtlefuse->eeprom_thermalmeter > 0x1c)
		rtlefuse->eeprom_thermalmeter = 0x12;
	if (rtlefuse->eeprom_thermalmeter == 0x1f || autoload_fail)
		rtlefuse->apk_thermalmeterignore = true;
	rtlefuse->thermalmeter[0] = rtlefuse->eeprom_thermalmeter;
	RTPRINT(rtlpriv, FINIT, INIT_TXPOWER,
		"thermalmeter = 0x%x\n", rtlefuse->eeprom_thermalmeter);
}

static void _rtl92cu_read_board_type(struct ieee80211_hw *hw, u8 *contents)
{
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 boardType;

	if (IS_NORMAL_CHIP(rtlhal->version)) {
		boardType = ((contents[EEPROM_RF_OPT1]) &
			    BOARD_TYPE_NORMAL_MASK) >> 5; /*bit[7:5]*/
	} else {
		boardType = contents[EEPROM_RF_OPT4];
		boardType &= BOARD_TYPE_TEST_MASK;
	}
	rtlefuse->board_type = boardType;
	if (IS_HIGHT_PA(rtlefuse->board_type))
		rtlefuse->external_pa = 1;
	pr_info("Board Type %x\n", rtlefuse->board_type);
}

static void _rtl92cu_read_adapter_info(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u16 i, usvalue;
	u8 hwinfo[HWSET_MAX_SIZE] = {0};
	u16 eeprom_id;

	if (rtlefuse->epromtype == EEPROM_BOOT_EFUSE) {
		rtl_efuse_shadow_map_update(hw);
		memcpy((void *)hwinfo,
		       (void *)&rtlefuse->efuse_map[EFUSE_INIT_MAP][0],
		       HWSET_MAX_SIZE);
	} else if (rtlefuse->epromtype == EEPROM_93C46) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "RTL819X Not boot from eeprom, check it !!\n");
	}
	RT_PRINT_DATA(rtlpriv, COMP_INIT, DBG_LOUD, "MAP",
		      hwinfo, HWSET_MAX_SIZE);
	eeprom_id = le16_to_cpu(*((__le16 *)&hwinfo[0]));
	if (eeprom_id != RTL8190_EEPROM_ID) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "EEPROM ID(%#x) is invalid!!\n", eeprom_id);
		rtlefuse->autoload_failflag = true;
	} else {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "Autoload OK\n");
		rtlefuse->autoload_failflag = false;
	}
	if (rtlefuse->autoload_failflag)
		return;
	for (i = 0; i < 6; i += 2) {
		usvalue = *(u16 *)&hwinfo[EEPROM_MAC_ADDR + i];
		*((u16 *) (&rtlefuse->dev_addr[i])) = usvalue;
	}
	pr_info("MAC address: %pM\n", rtlefuse->dev_addr);
	_rtl92cu_read_txpower_info_from_hwpg(hw,
					   rtlefuse->autoload_failflag, hwinfo);
	rtlefuse->eeprom_vid = le16_to_cpu(*(__le16 *)&hwinfo[EEPROM_VID]);
	rtlefuse->eeprom_did = le16_to_cpu(*(__le16 *)&hwinfo[EEPROM_DID]);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, " VID = 0x%02x PID = 0x%02x\n",
		 rtlefuse->eeprom_vid, rtlefuse->eeprom_did);
	rtlefuse->eeprom_channelplan = hwinfo[EEPROM_CHANNELPLAN];
	rtlefuse->eeprom_version =
			 le16_to_cpu(*(__le16 *)&hwinfo[EEPROM_VERSION]);
	rtlefuse->txpwr_fromeprom = true;
	rtlefuse->eeprom_oemid = hwinfo[EEPROM_CUSTOMER_ID];
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "EEPROM Customer ID: 0x%2x\n",
		 rtlefuse->eeprom_oemid);
	if (rtlhal->oem_id == RT_CID_DEFAULT) {
		switch (rtlefuse->eeprom_oemid) {
		case EEPROM_CID_DEFAULT:
			if (rtlefuse->eeprom_did == 0x8176) {
				if ((rtlefuse->eeprom_svid == 0x103C &&
				     rtlefuse->eeprom_smid == 0x1629))
					rtlhal->oem_id = RT_CID_819x_HP;
				else
					rtlhal->oem_id = RT_CID_DEFAULT;
			} else {
				rtlhal->oem_id = RT_CID_DEFAULT;
			}
			break;
		case EEPROM_CID_TOSHIBA:
			rtlhal->oem_id = RT_CID_TOSHIBA;
			break;
		case EEPROM_CID_QMI:
			rtlhal->oem_id = RT_CID_819x_QMI;
			break;
		case EEPROM_CID_WHQL:
		default:
			rtlhal->oem_id = RT_CID_DEFAULT;
			break;
		}
	}
	_rtl92cu_read_board_type(hw, hwinfo);
}

static void _rtl92cu_hal_customized_behavior(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_usb_priv *usb_priv = rtl_usbpriv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	switch (rtlhal->oem_id) {
	case RT_CID_819x_HP:
		usb_priv->ledctl.led_opendrain = true;
		break;
	case RT_CID_819x_Lenovo:
	case RT_CID_DEFAULT:
	case RT_CID_TOSHIBA:
	case RT_CID_CCX:
	case RT_CID_819x_Acer:
	case RT_CID_WHQL:
	default:
		break;
	}
	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "RT Customized ID: 0x%02X\n",
		 rtlhal->oem_id);
}

void rtl92cu_read_eeprom_info(struct ieee80211_hw *hw)
{

	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 tmp_u1b;

	if (!IS_NORMAL_CHIP(rtlhal->version))
		return;
	tmp_u1b = rtl_read_byte(rtlpriv, REG_9346CR);
	rtlefuse->epromtype = (tmp_u1b & BOOT_FROM_EEPROM) ?
			       EEPROM_93C46 : EEPROM_BOOT_EFUSE;
	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "Boot from %s\n",
		 tmp_u1b & BOOT_FROM_EEPROM ? "EERROM" : "EFUSE");
	rtlefuse->autoload_failflag = (tmp_u1b & EEPROM_EN) ? false : true;
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "Autoload %s\n",
		 tmp_u1b & EEPROM_EN ? "OK!!" : "ERR!!");
	_rtl92cu_read_adapter_info(hw);
	_rtl92cu_hal_customized_behavior(hw);
	return;
}

static int _rtl92cu_init_power_on(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	int		status = 0;
	u16		value16;
	u8		value8;
	/*  polling autoload done. */
	u32	pollingCount = 0;

	do {
		if (rtl_read_byte(rtlpriv, REG_APS_FSMCO) & PFM_ALDN) {
			RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
				 "Autoload Done!\n");
			break;
		}
		if (pollingCount++ > 100) {
			RT_TRACE(rtlpriv, COMP_INIT, DBG_EMERG,
				 "Failed to polling REG_APS_FSMCO[PFM_ALDN] done!\n");
			return -ENODEV;
		}
	} while (true);
	/* 0. RSV_CTRL 0x1C[7:0] = 0 unlock ISO/CLK/Power control register */
	rtl_write_byte(rtlpriv, REG_RSV_CTRL, 0x0);
	/* Power on when re-enter from IPS/Radio off/card disable */
	/* enable SPS into PWM mode */
	rtl_write_byte(rtlpriv, REG_SPS0_CTRL, 0x2b);
	udelay(100);
	value8 = rtl_read_byte(rtlpriv, REG_LDOV12D_CTRL);
	if (0 == (value8 & LDV12_EN)) {
		value8 |= LDV12_EN;
		rtl_write_byte(rtlpriv, REG_LDOV12D_CTRL, value8);
		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
			 " power-on :REG_LDOV12D_CTRL Reg0x21:0x%02x\n",
			 value8);
		udelay(100);
		value8 = rtl_read_byte(rtlpriv, REG_SYS_ISO_CTRL);
		value8 &= ~ISO_MD2PP;
		rtl_write_byte(rtlpriv, REG_SYS_ISO_CTRL, value8);
	}
	/*  auto enable WLAN */
	pollingCount = 0;
	value16 = rtl_read_word(rtlpriv, REG_APS_FSMCO);
	value16 |= APFM_ONMAC;
	rtl_write_word(rtlpriv, REG_APS_FSMCO, value16);
	do {
		if (!(rtl_read_word(rtlpriv, REG_APS_FSMCO) & APFM_ONMAC)) {
			pr_info("MAC auto ON okay!\n");
			break;
		}
		if (pollingCount++ > 100) {
			RT_TRACE(rtlpriv, COMP_INIT, DBG_EMERG,
				 "Failed to polling REG_APS_FSMCO[APFM_ONMAC] done!\n");
			return -ENODEV;
		}
	} while (true);
	/* Enable Radio ,GPIO ,and LED function */
	rtl_write_word(rtlpriv, REG_APS_FSMCO, 0x0812);
	/* release RF digital isolation */
	value16 = rtl_read_word(rtlpriv, REG_SYS_ISO_CTRL);
	value16 &= ~ISO_DIOR;
	rtl_write_word(rtlpriv, REG_SYS_ISO_CTRL, value16);
	/* Reconsider when to do this operation after asking HWSD. */
	pollingCount = 0;
	rtl_write_byte(rtlpriv, REG_APSD_CTRL, (rtl_read_byte(rtlpriv,
						REG_APSD_CTRL) & ~BIT(6)));
	do {
		pollingCount++;
	} while ((pollingCount < 200) &&
		 (rtl_read_byte(rtlpriv, REG_APSD_CTRL) & BIT(7)));
	/* Enable MAC DMA/WMAC/SCHEDULE/SEC block */
	value16 = rtl_read_word(rtlpriv,  REG_CR);
	value16 |= (HCI_TXDMA_EN | HCI_RXDMA_EN | TXDMA_EN | RXDMA_EN |
		    PROTOCOL_EN | SCHEDULE_EN | MACTXEN | MACRXEN | ENSEC);
	rtl_write_word(rtlpriv, REG_CR, value16);
	return status;
}

static void _rtl92cu_init_queue_reserved_page(struct ieee80211_hw *hw,
					      bool wmm_enable,
					      u8 out_ep_num,
					      u8 queue_sel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	bool isChipN = IS_NORMAL_CHIP(rtlhal->version);
	u32 outEPNum = (u32)out_ep_num;
	u32 numHQ = 0;
	u32 numLQ = 0;
	u32 numNQ = 0;
	u32 numPubQ;
	u32 value32;
	u8 value8;
	u32 txQPageNum, txQPageUnit, txQRemainPage;

	if (!wmm_enable) {
		numPubQ = (isChipN) ? CHIP_B_PAGE_NUM_PUBQ :
			  CHIP_A_PAGE_NUM_PUBQ;
		txQPageNum = TX_TOTAL_PAGE_NUMBER - numPubQ;

		txQPageUnit = txQPageNum/outEPNum;
		txQRemainPage = txQPageNum % outEPNum;
		if (queue_sel & TX_SELE_HQ)
			numHQ = txQPageUnit;
		if (queue_sel & TX_SELE_LQ)
			numLQ = txQPageUnit;
		/* HIGH priority queue always present in the configuration of
		 * 2 out-ep. Remainder pages have assigned to High queue */
		if ((outEPNum > 1) && (txQRemainPage))
			numHQ += txQRemainPage;
		/* NOTE: This step done before writting REG_RQPN. */
		if (isChipN) {
			if (queue_sel & TX_SELE_NQ)
				numNQ = txQPageUnit;
			value8 = (u8)_NPQ(numNQ);
			rtl_write_byte(rtlpriv,  REG_RQPN_NPQ, value8);
		}
	} else {
		/* for WMM ,number of out-ep must more than or equal to 2! */
		numPubQ = isChipN ? WMM_CHIP_B_PAGE_NUM_PUBQ :
			  WMM_CHIP_A_PAGE_NUM_PUBQ;
		if (queue_sel & TX_SELE_HQ) {
			numHQ = isChipN ? WMM_CHIP_B_PAGE_NUM_HPQ :
				WMM_CHIP_A_PAGE_NUM_HPQ;
		}
		if (queue_sel & TX_SELE_LQ) {
			numLQ = isChipN ? WMM_CHIP_B_PAGE_NUM_LPQ :
				WMM_CHIP_A_PAGE_NUM_LPQ;
		}
		/* NOTE: This step done before writting REG_RQPN. */
		if (isChipN) {
			if (queue_sel & TX_SELE_NQ)
				numNQ = WMM_CHIP_B_PAGE_NUM_NPQ;
			value8 = (u8)_NPQ(numNQ);
			rtl_write_byte(rtlpriv, REG_RQPN_NPQ, value8);
		}
	}
	/* TX DMA */
	value32 = _HPQ(numHQ) | _LPQ(numLQ) | _PUBQ(numPubQ) | LD_RQPN;
	rtl_write_dword(rtlpriv, REG_RQPN, value32);
}

static void _rtl92c_init_trx_buffer(struct ieee80211_hw *hw, bool wmm_enable)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8	txpktbuf_bndy;
	u8	value8;

	if (!wmm_enable)
		txpktbuf_bndy = TX_PAGE_BOUNDARY;
	else /* for WMM */
		txpktbuf_bndy = (IS_NORMAL_CHIP(rtlhal->version))
						? WMM_CHIP_B_TX_PAGE_BOUNDARY
						: WMM_CHIP_A_TX_PAGE_BOUNDARY;
	rtl_write_byte(rtlpriv, REG_TXPKTBUF_BCNQ_BDNY, txpktbuf_bndy);
	rtl_write_byte(rtlpriv, REG_TXPKTBUF_MGQ_BDNY, txpktbuf_bndy);
	rtl_write_byte(rtlpriv, REG_TXPKTBUF_WMAC_LBK_BF_HD, txpktbuf_bndy);
	rtl_write_byte(rtlpriv, REG_TRXFF_BNDY, txpktbuf_bndy);
	rtl_write_byte(rtlpriv, REG_TDECTRL+1, txpktbuf_bndy);
	rtl_write_word(rtlpriv,  (REG_TRXFF_BNDY + 2), 0x27FF);
	value8 = _PSRX(RX_PAGE_SIZE_REG_VALUE) | _PSTX(PBP_128);
	rtl_write_byte(rtlpriv, REG_PBP, value8);
}

static void _rtl92c_init_chipN_reg_priority(struct ieee80211_hw *hw, u16 beQ,
					    u16 bkQ, u16 viQ, u16 voQ,
					    u16 mgtQ, u16 hiQ)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u16 value16 = (rtl_read_word(rtlpriv, REG_TRXDMA_CTRL) & 0x7);

	value16 |= _TXDMA_BEQ_MAP(beQ) | _TXDMA_BKQ_MAP(bkQ) |
		   _TXDMA_VIQ_MAP(viQ) | _TXDMA_VOQ_MAP(voQ) |
		   _TXDMA_MGQ_MAP(mgtQ) | _TXDMA_HIQ_MAP(hiQ);
	rtl_write_word(rtlpriv,  REG_TRXDMA_CTRL, value16);
}

static void _rtl92cu_init_chipN_one_out_ep_priority(struct ieee80211_hw *hw,
						    bool wmm_enable,
						    u8 queue_sel)
{
	u16 uninitialized_var(value);

	switch (queue_sel) {
	case TX_SELE_HQ:
		value = QUEUE_HIGH;
		break;
	case TX_SELE_LQ:
		value = QUEUE_LOW;
		break;
	case TX_SELE_NQ:
		value = QUEUE_NORMAL;
		break;
	default:
		WARN_ON(1); /* Shall not reach here! */
		break;
	}
	_rtl92c_init_chipN_reg_priority(hw, value, value, value, value,
					value, value);
	pr_info("Tx queue select: 0x%02x\n", queue_sel);
}

static void _rtl92cu_init_chipN_two_out_ep_priority(struct ieee80211_hw *hw,
								bool wmm_enable,
								u8 queue_sel)
{
	u16 beQ, bkQ, viQ, voQ, mgtQ, hiQ;
	u16 uninitialized_var(valueHi);
	u16 uninitialized_var(valueLow);

	switch (queue_sel) {
	case (TX_SELE_HQ | TX_SELE_LQ):
		valueHi = QUEUE_HIGH;
		valueLow = QUEUE_LOW;
		break;
	case (TX_SELE_NQ | TX_SELE_LQ):
		valueHi = QUEUE_NORMAL;
		valueLow = QUEUE_LOW;
		break;
	case (TX_SELE_HQ | TX_SELE_NQ):
		valueHi = QUEUE_HIGH;
		valueLow = QUEUE_NORMAL;
		break;
	default:
		WARN_ON(1);
		break;
	}
	if (!wmm_enable) {
		beQ = valueLow;
		bkQ = valueLow;
		viQ = valueHi;
		voQ = valueHi;
		mgtQ = valueHi;
		hiQ = valueHi;
	} else {/* for WMM ,CONFIG_OUT_EP_WIFI_MODE */
		beQ = valueHi;
		bkQ = valueLow;
		viQ = valueLow;
		voQ = valueHi;
		mgtQ = valueHi;
		hiQ = valueHi;
	}
	_rtl92c_init_chipN_reg_priority(hw, beQ, bkQ, viQ, voQ, mgtQ, hiQ);
	pr_info("Tx queue select: 0x%02x\n", queue_sel);
}

static void _rtl92cu_init_chipN_three_out_ep_priority(struct ieee80211_hw *hw,
						      bool wmm_enable,
						      u8 queue_sel)
{
	u16 beQ, bkQ, viQ, voQ, mgtQ, hiQ;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (!wmm_enable) { /* typical setting */
		beQ	= QUEUE_LOW;
		bkQ	= QUEUE_LOW;
		viQ	= QUEUE_NORMAL;
		voQ	= QUEUE_HIGH;
		mgtQ	= QUEUE_HIGH;
		hiQ	= QUEUE_HIGH;
	} else { /* for WMM */
		beQ	= QUEUE_LOW;
		bkQ	= QUEUE_NORMAL;
		viQ	= QUEUE_NORMAL;
		voQ	= QUEUE_HIGH;
		mgtQ	= QUEUE_HIGH;
		hiQ	= QUEUE_HIGH;
	}
	_rtl92c_init_chipN_reg_priority(hw, beQ, bkQ, viQ, voQ, mgtQ, hiQ);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_EMERG, "Tx queue select :0x%02x..\n",
		 queue_sel);
}

static void _rtl92cu_init_chipN_queue_priority(struct ieee80211_hw *hw,
					       bool wmm_enable,
					       u8 out_ep_num,
					       u8 queue_sel)
{
	switch (out_ep_num) {
	case 1:
		_rtl92cu_init_chipN_one_out_ep_priority(hw, wmm_enable,
							queue_sel);
		break;
	case 2:
		_rtl92cu_init_chipN_two_out_ep_priority(hw, wmm_enable,
							queue_sel);
		break;
	case 3:
		_rtl92cu_init_chipN_three_out_ep_priority(hw, wmm_enable,
							  queue_sel);
		break;
	default:
		WARN_ON(1); /* Shall not reach here! */
		break;
	}
}

static void _rtl92cu_init_chipT_queue_priority(struct ieee80211_hw *hw,
					       bool wmm_enable,
					       u8 out_ep_num,
					       u8 queue_sel)
{
	u8 hq_sele = 0;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	switch (out_ep_num) {
	case 2:	/* (TX_SELE_HQ|TX_SELE_LQ) */
		if (!wmm_enable) /* typical setting */
			hq_sele =  HQSEL_VOQ | HQSEL_VIQ | HQSEL_MGTQ |
				   HQSEL_HIQ;
		else	/* for WMM */
			hq_sele = HQSEL_VOQ | HQSEL_BEQ | HQSEL_MGTQ |
				  HQSEL_HIQ;
		break;
	case 1:
		if (TX_SELE_LQ == queue_sel) {
			/* map all endpoint to Low queue */
			hq_sele = 0;
		} else if (TX_SELE_HQ == queue_sel) {
			/* map all endpoint to High queue */
			hq_sele =  HQSEL_VOQ | HQSEL_VIQ | HQSEL_BEQ |
				   HQSEL_BKQ | HQSEL_MGTQ | HQSEL_HIQ;
		}
		break;
	default:
		WARN_ON(1); /* Shall not reach here! */
		break;
	}
	rtl_write_byte(rtlpriv, (REG_TRXDMA_CTRL+1), hq_sele);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_EMERG, "Tx queue select :0x%02x..\n",
		 hq_sele);
}

static void _rtl92cu_init_queue_priority(struct ieee80211_hw *hw,
						bool wmm_enable,
						u8 out_ep_num,
						u8 queue_sel)
{
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	if (IS_NORMAL_CHIP(rtlhal->version))
		_rtl92cu_init_chipN_queue_priority(hw, wmm_enable, out_ep_num,
						   queue_sel);
	else
		_rtl92cu_init_chipT_queue_priority(hw, wmm_enable, out_ep_num,
						   queue_sel);
}

static void _rtl92cu_init_usb_aggregation(struct ieee80211_hw *hw)
{
}

static void _rtl92cu_init_wmac_setting(struct ieee80211_hw *hw)
{
	u16			value16;

	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));

	mac->rx_conf = (RCR_APM | RCR_AM | RCR_ADF | RCR_AB | RCR_APPFCS |
		      RCR_APP_ICV | RCR_AMF | RCR_HTC_LOC_CTRL |
		      RCR_APP_MIC | RCR_APP_PHYSTS | RCR_ACRC32);
	rtl_write_dword(rtlpriv, REG_RCR, mac->rx_conf);
	/* Accept all multicast address */
	rtl_write_dword(rtlpriv,  REG_MAR, 0xFFFFFFFF);
	rtl_write_dword(rtlpriv,  REG_MAR + 4, 0xFFFFFFFF);
	/* Accept all management frames */
	value16 = 0xFFFF;
	rtl92c_set_mgt_filter(hw, value16);
	/* Reject all control frame - default value is 0 */
	rtl92c_set_ctrl_filter(hw, 0x0);
	/* Accept all data frames */
	value16 = 0xFFFF;
	rtl92c_set_data_filter(hw, value16);
}

static int _rtl92cu_init_mac(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_usb_priv *usb_priv = rtl_usbpriv(hw);
	struct rtl_usb *rtlusb = rtl_usbdev(usb_priv);
	int err = 0;
	u32	boundary = 0;
	u8 wmm_enable = false; /* TODO */
	u8 out_ep_nums = rtlusb->out_ep_nums;
	u8 queue_sel = rtlusb->out_queue_sel;
	err = _rtl92cu_init_power_on(hw);

	if (err) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "Failed to init power on!\n");
		return err;
	}
	if (!wmm_enable) {
		boundary = TX_PAGE_BOUNDARY;
	} else { /* for WMM */
		boundary = (IS_NORMAL_CHIP(rtlhal->version))
					? WMM_CHIP_B_TX_PAGE_BOUNDARY
					: WMM_CHIP_A_TX_PAGE_BOUNDARY;
	}
	if (false == rtl92c_init_llt_table(hw, boundary)) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "Failed to init LLT Table!\n");
		return -EINVAL;
	}
	_rtl92cu_init_queue_reserved_page(hw, wmm_enable, out_ep_nums,
					  queue_sel);
	_rtl92c_init_trx_buffer(hw, wmm_enable);
	_rtl92cu_init_queue_priority(hw, wmm_enable, out_ep_nums,
				     queue_sel);
	/* Get Rx PHY status in order to report RSSI and others. */
	rtl92c_init_driver_info_size(hw, RTL92C_DRIVER_INFO_SIZE);
	rtl92c_init_interrupt(hw);
	rtl92c_init_network_type(hw);
	_rtl92cu_init_wmac_setting(hw);
	rtl92c_init_adaptive_ctrl(hw);
	rtl92c_init_edca(hw);
	rtl92c_init_rate_fallback(hw);
	rtl92c_init_retry_function(hw);
	_rtl92cu_init_usb_aggregation(hw);
	rtlpriv->cfg->ops->set_bw_mode(hw, NL80211_CHAN_HT20);
	rtl92c_set_min_space(hw, IS_92C_SERIAL(rtlhal->version));
	rtl92c_init_beacon_parameters(hw, rtlhal->version);
	rtl92c_init_ampdu_aggregation(hw);
	rtl92c_init_beacon_max_error(hw, true);
	return err;
}

void rtl92cu_enable_hw_security_config(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 sec_reg_value = 0x0;
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "PairwiseEncAlgorithm = %d GroupEncAlgorithm = %d\n",
		 rtlpriv->sec.pairwise_enc_algorithm,
		 rtlpriv->sec.group_enc_algorithm);
	if (rtlpriv->cfg->mod_params->sw_crypto || rtlpriv->sec.use_sw_sec) {
		RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG,
			 "not open sw encryption\n");
		return;
	}
	sec_reg_value = SCR_TxEncEnable | SCR_RxDecEnable;
	if (rtlpriv->sec.use_defaultkey) {
		sec_reg_value |= SCR_TxUseDK;
		sec_reg_value |= SCR_RxUseDK;
	}
	if (IS_NORMAL_CHIP(rtlhal->version))
		sec_reg_value |= (SCR_RXBCUSEDK | SCR_TXBCUSEDK);
	rtl_write_byte(rtlpriv, REG_CR + 1, 0x02);
	RT_TRACE(rtlpriv, COMP_SEC, DBG_LOUD, "The SECR-value %x\n",
		 sec_reg_value);
	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_WPA_CONFIG, &sec_reg_value);
}

static void _rtl92cu_hw_configure(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_usb *rtlusb = rtl_usbdev(rtl_usbpriv(hw));

	/* To Fix MAC loopback mode fail. */
	rtl_write_byte(rtlpriv, REG_LDOHCI12_CTRL, 0x0f);
	rtl_write_byte(rtlpriv, 0x15, 0xe9);
	/* HW SEQ CTRL */
	/* set 0x0 to 0xFF by tynli. Default enable HW SEQ NUM. */
	rtl_write_byte(rtlpriv, REG_HWSEQ_CTRL, 0xFF);
	/* fixed USB interface interference issue */
	rtl_write_byte(rtlpriv, 0xfe40, 0xe0);
	rtl_write_byte(rtlpriv, 0xfe41, 0x8d);
	rtl_write_byte(rtlpriv, 0xfe42, 0x80);
	rtlusb->reg_bcn_ctrl_val = 0x18;
	rtl_write_byte(rtlpriv, REG_BCN_CTRL, (u8)rtlusb->reg_bcn_ctrl_val);
}

static void _InitPABias(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 pa_setting;

	/* FIXED PA current issue */
	pa_setting = efuse_read_1byte(hw, 0x1FA);
	if (!(pa_setting & BIT(0))) {
		rtl_set_rfreg(hw, RF90_PATH_A, 0x15, 0x0FFFFF, 0x0F406);
		rtl_set_rfreg(hw, RF90_PATH_A, 0x15, 0x0FFFFF, 0x4F406);
		rtl_set_rfreg(hw, RF90_PATH_A, 0x15, 0x0FFFFF, 0x8F406);
		rtl_set_rfreg(hw, RF90_PATH_A, 0x15, 0x0FFFFF, 0xCF406);
	}
	if (!(pa_setting & BIT(1)) && IS_NORMAL_CHIP(rtlhal->version) &&
	    IS_92C_SERIAL(rtlhal->version)) {
		rtl_set_rfreg(hw, RF90_PATH_B, 0x15, 0x0FFFFF, 0x0F406);
		rtl_set_rfreg(hw, RF90_PATH_B, 0x15, 0x0FFFFF, 0x4F406);
		rtl_set_rfreg(hw, RF90_PATH_B, 0x15, 0x0FFFFF, 0x8F406);
		rtl_set_rfreg(hw, RF90_PATH_B, 0x15, 0x0FFFFF, 0xCF406);
	}
	if (!(pa_setting & BIT(4))) {
		pa_setting = rtl_read_byte(rtlpriv, 0x16);
		pa_setting &= 0x0F;
		rtl_write_byte(rtlpriv, 0x16, pa_setting | 0x90);
	}
}

static void _update_mac_setting(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));

	mac->rx_conf = rtl_read_dword(rtlpriv, REG_RCR);
	mac->rx_mgt_filter = rtl_read_word(rtlpriv, REG_RXFLTMAP0);
	mac->rx_ctrl_filter = rtl_read_word(rtlpriv, REG_RXFLTMAP1);
	mac->rx_data_filter = rtl_read_word(rtlpriv, REG_RXFLTMAP2);
}

int rtl92cu_hw_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	int err = 0;
	static bool iqk_initialized;

	rtlhal->hw_type = HARDWARE_TYPE_RTL8192CU;
	err = _rtl92cu_init_mac(hw);
	if (err) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "init mac failed!\n");
		return err;
	}
	err = rtl92c_download_fw(hw);
	if (err) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 "Failed to download FW. Init HW without FW now..\n");
		err = 1;
		return err;
	}
	rtlhal->last_hmeboxnum = 0; /* h2c */
	_rtl92cu_phy_param_tab_init(hw);
	rtl92cu_phy_mac_config(hw);
	rtl92cu_phy_bb_config(hw);
	rtlphy->rf_mode = RF_OP_BY_SW_3WIRE;
	rtl92c_phy_rf_config(hw);
	if (IS_VENDOR_UMC_A_CUT(rtlhal->version) &&
	    !IS_92C_SERIAL(rtlhal->version)) {
		rtl_set_rfreg(hw, RF90_PATH_A, RF_RX_G1, MASKDWORD, 0x30255);
		rtl_set_rfreg(hw, RF90_PATH_A, RF_RX_G2, MASKDWORD, 0x50a00);
	}
	rtlphy->rfreg_chnlval[0] = rtl_get_rfreg(hw, (enum radio_path)0,
						 RF_CHNLBW, RFREG_OFFSET_MASK);
	rtlphy->rfreg_chnlval[1] = rtl_get_rfreg(hw, (enum radio_path)1,
						 RF_CHNLBW, RFREG_OFFSET_MASK);
	rtl92cu_bb_block_on(hw);
	rtl_cam_reset_all_entry(hw);
	rtl92cu_enable_hw_security_config(hw);
	ppsc->rfpwr_state = ERFON;
	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_ETHER_ADDR, mac->mac_addr);
	if (ppsc->rfpwr_state == ERFON) {
		rtl92c_phy_set_rfpath_switch(hw, 1);
		if (iqk_initialized) {
			rtl92c_phy_iq_calibrate(hw, false);
		} else {
			rtl92c_phy_iq_calibrate(hw, false);
			iqk_initialized = true;
		}
		rtl92c_dm_check_txpower_tracking(hw);
		rtl92c_phy_lc_calibrate(hw);
	}
	_rtl92cu_hw_configure(hw);
	_InitPABias(hw);
	_update_mac_setting(hw);
	rtl92c_dm_init(hw);
	return err;
}

static void _DisableRFAFEAndResetBB(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
/**************************************
a.	TXPAUSE 0x522[7:0] = 0xFF	Pause MAC TX queue
b.	RF path 0 offset 0x00 = 0x00	disable RF
c.	APSD_CTRL 0x600[7:0] = 0x40
d.	SYS_FUNC_EN 0x02[7:0] = 0x16	reset BB state machine
e.	SYS_FUNC_EN 0x02[7:0] = 0x14	reset BB state machine
***************************************/
	u8 eRFPath = 0, value8 = 0;
	rtl_write_byte(rtlpriv, REG_TXPAUSE, 0xFF);
	rtl_set_rfreg(hw, (enum radio_path)eRFPath, 0x0, MASKBYTE0, 0x0);

	value8 |= APSDOFF;
	rtl_write_byte(rtlpriv, REG_APSD_CTRL, value8); /*0x40*/
	value8 = 0;
	value8 |= (FEN_USBD | FEN_USBA | FEN_BB_GLB_RSTn);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, value8);/*0x16*/
	value8 &= (~FEN_BB_GLB_RSTn);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, value8); /*0x14*/
}

static void  _ResetDigitalProcedure1(struct ieee80211_hw *hw, bool bWithoutHWSM)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	if (rtlhal->fw_version <=  0x20) {
		/*****************************
		f. MCUFWDL 0x80[7:0]=0		reset MCU ready status
		g. SYS_FUNC_EN 0x02[10]= 0	reset MCU reg, (8051 reset)
		h. SYS_FUNC_EN 0x02[15-12]= 5	reset MAC reg, DCORE
		i. SYS_FUNC_EN 0x02[10]= 1	enable MCU reg, (8051 enable)
		******************************/
		u16 valu16 = 0;

		rtl_write_byte(rtlpriv, REG_MCUFWDL, 0);
		valu16 = rtl_read_word(rtlpriv, REG_SYS_FUNC_EN);
		rtl_write_word(rtlpriv, REG_SYS_FUNC_EN, (valu16 &
			       (~FEN_CPUEN))); /* reset MCU ,8051 */
		valu16 = rtl_read_word(rtlpriv, REG_SYS_FUNC_EN)&0x0FFF;
		rtl_write_word(rtlpriv, REG_SYS_FUNC_EN, (valu16 |
			      (FEN_HWPDN|FEN_ELDR))); /* reset MAC */
		valu16 = rtl_read_word(rtlpriv, REG_SYS_FUNC_EN);
		rtl_write_word(rtlpriv, REG_SYS_FUNC_EN, (valu16 |
			       FEN_CPUEN)); /* enable MCU ,8051 */
	} else {
		u8 retry_cnts = 0;

		/* IF fw in RAM code, do reset */
		if (rtl_read_byte(rtlpriv, REG_MCUFWDL) & BIT(1)) {
			/* reset MCU ready status */
			rtl_write_byte(rtlpriv, REG_MCUFWDL, 0);
			/* 8051 reset by self */
			rtl_write_byte(rtlpriv, REG_HMETFR+3, 0x20);
			while ((retry_cnts++ < 100) &&
			       (FEN_CPUEN & rtl_read_word(rtlpriv,
			       REG_SYS_FUNC_EN))) {
				udelay(50);
			}
			if (retry_cnts >= 100) {
				RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
					 "#####=> 8051 reset failed!.........................\n");
				/* if 8051 reset fail, reset MAC. */
				rtl_write_byte(rtlpriv,
					       REG_SYS_FUNC_EN + 1,
					       0x50);
				udelay(100);
			}
		}
		/* Reset MAC and Enable 8051 */
		rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN + 1, 0x54);
		rtl_write_byte(rtlpriv, REG_MCUFWDL, 0);
	}
	if (bWithoutHWSM) {
		/*****************************
		  Without HW auto state machine
		g.SYS_CLKR 0x08[15:0] = 0x30A3		disable MAC clock
		h.AFE_PLL_CTRL 0x28[7:0] = 0x80		disable AFE PLL
		i.AFE_XTAL_CTRL 0x24[15:0] = 0x880F	gated AFE DIG_CLOCK
		j.SYS_ISu_CTRL 0x00[7:0] = 0xF9		isolated digital to PON
		******************************/
		rtl_write_word(rtlpriv, REG_SYS_CLKR, 0x70A3);
		rtl_write_byte(rtlpriv, REG_AFE_PLL_CTRL, 0x80);
		rtl_write_word(rtlpriv, REG_AFE_XTAL_CTRL, 0x880F);
		rtl_write_byte(rtlpriv, REG_SYS_ISO_CTRL, 0xF9);
	}
}

static void _ResetDigitalProcedure2(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
/*****************************
k. SYS_FUNC_EN 0x03[7:0] = 0x44		disable ELDR runction
l. SYS_CLKR 0x08[15:0] = 0x3083		disable ELDR clock
m. SYS_ISO_CTRL 0x01[7:0] = 0x83	isolated ELDR to PON
******************************/
	rtl_write_word(rtlpriv, REG_SYS_CLKR, 0x70A3);
	rtl_write_byte(rtlpriv, REG_SYS_ISO_CTRL+1, 0x82);
}

static void _DisableGPIO(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
/***************************************
j. GPIO_PIN_CTRL 0x44[31:0]=0x000
k. Value = GPIO_PIN_CTRL[7:0]
l.  GPIO_PIN_CTRL 0x44[31:0] = 0x00FF0000 | (value <<8); write ext PIN level
m. GPIO_MUXCFG 0x42 [15:0] = 0x0780
n. LEDCFG 0x4C[15:0] = 0x8080
***************************************/
	u8	value8;
	u16	value16;
	u32	value32;

	/* 1. Disable GPIO[7:0] */
	rtl_write_word(rtlpriv, REG_GPIO_PIN_CTRL+2, 0x0000);
	value32 = rtl_read_dword(rtlpriv, REG_GPIO_PIN_CTRL) & 0xFFFF00FF;
	value8 = (u8) (value32&0x000000FF);
	value32 |= ((value8<<8) | 0x00FF0000);
	rtl_write_dword(rtlpriv, REG_GPIO_PIN_CTRL, value32);
	/* 2. Disable GPIO[10:8] */
	rtl_write_byte(rtlpriv, REG_GPIO_MUXCFG+3, 0x00);
	value16 = rtl_read_word(rtlpriv, REG_GPIO_MUXCFG+2) & 0xFF0F;
	value8 = (u8) (value16&0x000F);
	value16 |= ((value8<<4) | 0x0780);
	rtl_write_word(rtlpriv, REG_GPIO_PIN_CTRL+2, value16);
	/* 3. Disable LED0 & 1 */
	rtl_write_word(rtlpriv, REG_LEDCFG0, 0x8080);
}

static void _DisableAnalog(struct ieee80211_hw *hw, bool bWithoutHWSM)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u16 value16 = 0;
	u8 value8 = 0;

	if (bWithoutHWSM) {
		/*****************************
		n. LDOA15_CTRL 0x20[7:0] = 0x04	 disable A15 power
		o. LDOV12D_CTRL 0x21[7:0] = 0x54 disable digital core power
		r. When driver call disable, the ASIC will turn off remaining
		   clock automatically
		******************************/
		rtl_write_byte(rtlpriv, REG_LDOA15_CTRL, 0x04);
		value8 = rtl_read_byte(rtlpriv, REG_LDOV12D_CTRL);
		value8 &= (~LDV12_EN);
		rtl_write_byte(rtlpriv, REG_LDOV12D_CTRL, value8);
	}

/*****************************
h. SPS0_CTRL 0x11[7:0] = 0x23		enter PFM mode
i. APS_FSMCO 0x04[15:0] = 0x4802	set USB suspend
******************************/
	rtl_write_byte(rtlpriv, REG_SPS0_CTRL, 0x23);
	value16 |= (APDM_HOST | AFSM_HSUS | PFM_ALDN);
	rtl_write_word(rtlpriv, REG_APS_FSMCO, (u16)value16);
	rtl_write_byte(rtlpriv, REG_RSV_CTRL, 0x0E);
}

static void _CardDisableHWSM(struct ieee80211_hw *hw)
{
	/* ==== RF Off Sequence ==== */
	_DisableRFAFEAndResetBB(hw);
	/* ==== Reset digital sequence   ====== */
	_ResetDigitalProcedure1(hw, false);
	/*  ==== Pull GPIO PIN to balance level and LED control ====== */
	_DisableGPIO(hw);
	/* ==== Disable analog sequence === */
	_DisableAnalog(hw, false);
}

static void _CardDisableWithoutHWSM(struct ieee80211_hw *hw)
{
	/*==== RF Off Sequence ==== */
	_DisableRFAFEAndResetBB(hw);
	/*  ==== Reset digital sequence   ====== */
	_ResetDigitalProcedure1(hw, true);
	/*  ==== Pull GPIO PIN to balance level and LED control ====== */
	_DisableGPIO(hw);
	/*  ==== Reset digital sequence   ====== */
	_ResetDigitalProcedure2(hw);
	/*  ==== Disable analog sequence === */
	_DisableAnalog(hw, true);
}

static void _rtl92cu_set_bcn_ctrl_reg(struct ieee80211_hw *hw,
				      u8 set_bits, u8 clear_bits)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_usb *rtlusb = rtl_usbdev(rtl_usbpriv(hw));

	rtlusb->reg_bcn_ctrl_val |= set_bits;
	rtlusb->reg_bcn_ctrl_val &= ~clear_bits;
	rtl_write_byte(rtlpriv, REG_BCN_CTRL, (u8) rtlusb->reg_bcn_ctrl_val);
}

static void _rtl92cu_stop_tx_beacon(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	u8 tmp1byte = 0;
	if (IS_NORMAL_CHIP(rtlhal->version)) {
		tmp1byte = rtl_read_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2);
		rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2,
			       tmp1byte & (~BIT(6)));
		rtl_write_byte(rtlpriv, REG_TBTT_PROHIBIT + 1, 0x64);
		tmp1byte = rtl_read_byte(rtlpriv, REG_TBTT_PROHIBIT + 2);
		tmp1byte &= ~(BIT(0));
		rtl_write_byte(rtlpriv, REG_TBTT_PROHIBIT + 2, tmp1byte);
	} else {
		rtl_write_byte(rtlpriv, REG_TXPAUSE,
			       rtl_read_byte(rtlpriv, REG_TXPAUSE) | BIT(6));
	}
}

static void _rtl92cu_resume_tx_beacon(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	u8 tmp1byte = 0;

	if (IS_NORMAL_CHIP(rtlhal->version)) {
		tmp1byte = rtl_read_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2);
		rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2,
			       tmp1byte | BIT(6));
		rtl_write_byte(rtlpriv, REG_TBTT_PROHIBIT + 1, 0xff);
		tmp1byte = rtl_read_byte(rtlpriv, REG_TBTT_PROHIBIT + 2);
		tmp1byte |= BIT(0);
		rtl_write_byte(rtlpriv, REG_TBTT_PROHIBIT + 2, tmp1byte);
	} else {
		rtl_write_byte(rtlpriv, REG_TXPAUSE,
			       rtl_read_byte(rtlpriv, REG_TXPAUSE) & (~BIT(6)));
	}
}

static void _rtl92cu_enable_bcn_sub_func(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);

	if (IS_NORMAL_CHIP(rtlhal->version))
		_rtl92cu_set_bcn_ctrl_reg(hw, 0, BIT(1));
	else
		_rtl92cu_set_bcn_ctrl_reg(hw, 0, BIT(4));
}

static void _rtl92cu_disable_bcn_sub_func(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);

	if (IS_NORMAL_CHIP(rtlhal->version))
		_rtl92cu_set_bcn_ctrl_reg(hw, BIT(1), 0);
	else
		_rtl92cu_set_bcn_ctrl_reg(hw, BIT(4), 0);
}

static int _rtl92cu_set_media_status(struct ieee80211_hw *hw,
				     enum nl80211_iftype type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 bt_msr = rtl_read_byte(rtlpriv, MSR);
	enum led_ctl_mode ledaction = LED_CTL_NO_LINK;

	bt_msr &= 0xfc;
	rtl_write_byte(rtlpriv, REG_BCN_MAX_ERR, 0xFF);
	if (type == NL80211_IFTYPE_UNSPECIFIED || type ==
	    NL80211_IFTYPE_STATION) {
		_rtl92cu_stop_tx_beacon(hw);
		_rtl92cu_enable_bcn_sub_func(hw);
	} else if (type == NL80211_IFTYPE_ADHOC || type == NL80211_IFTYPE_AP) {
		_rtl92cu_resume_tx_beacon(hw);
		_rtl92cu_disable_bcn_sub_func(hw);
	} else {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 "Set HW_VAR_MEDIA_STATUS:No such media status(%x)\n",
			 type);
	}
	switch (type) {
	case NL80211_IFTYPE_UNSPECIFIED:
		bt_msr |= MSR_NOLINK;
		ledaction = LED_CTL_LINK;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "Set Network type to NO LINK!\n");
		break;
	case NL80211_IFTYPE_ADHOC:
		bt_msr |= MSR_ADHOC;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "Set Network type to Ad Hoc!\n");
		break;
	case NL80211_IFTYPE_STATION:
		bt_msr |= MSR_INFRA;
		ledaction = LED_CTL_LINK;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "Set Network type to STA!\n");
		break;
	case NL80211_IFTYPE_AP:
		bt_msr |= MSR_AP;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "Set Network type to AP!\n");
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "Network type %d not supported!\n", type);
		goto error_out;
	}
	rtl_write_byte(rtlpriv, (MSR), bt_msr);
	rtlpriv->cfg->ops->led_control(hw, ledaction);
	if ((bt_msr & 0xfc) == MSR_AP)
		rtl_write_byte(rtlpriv, REG_BCNTCFG + 1, 0x00);
	else
		rtl_write_byte(rtlpriv, REG_BCNTCFG + 1, 0x66);
	return 0;
error_out:
	return 1;
}

void rtl92cu_card_disable(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_usb *rtlusb = rtl_usbdev(rtl_usbpriv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	enum nl80211_iftype opmode;

	mac->link_state = MAC80211_NOLINK;
	opmode = NL80211_IFTYPE_UNSPECIFIED;
	_rtl92cu_set_media_status(hw, opmode);
	rtlpriv->cfg->ops->led_control(hw, LED_CTL_POWER_OFF);
	RT_SET_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC);
	if (rtlusb->disableHWSM)
		_CardDisableHWSM(hw);
	else
		_CardDisableWithoutHWSM(hw);
}

void rtl92cu_set_check_bssid(struct ieee80211_hw *hw, bool check_bssid)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	u32 reg_rcr = rtl_read_dword(rtlpriv, REG_RCR);

	if (rtlpriv->psc.rfpwr_state != ERFON)
		return;

	if (check_bssid) {
		u8 tmp;
		if (IS_NORMAL_CHIP(rtlhal->version)) {
			reg_rcr |= (RCR_CBSSID_DATA | RCR_CBSSID_BCN);
			tmp = BIT(4);
		} else {
			reg_rcr |= RCR_CBSSID;
			tmp = BIT(4) | BIT(5);
		}
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_RCR,
					      (u8 *) (&reg_rcr));
		_rtl92cu_set_bcn_ctrl_reg(hw, 0, tmp);
	} else {
		u8 tmp;
		if (IS_NORMAL_CHIP(rtlhal->version)) {
			reg_rcr &= ~(RCR_CBSSID_DATA | RCR_CBSSID_BCN);
			tmp = BIT(4);
		} else {
			reg_rcr &= ~RCR_CBSSID;
			tmp = BIT(4) | BIT(5);
		}
		reg_rcr &= (~(RCR_CBSSID_DATA | RCR_CBSSID_BCN));
		rtlpriv->cfg->ops->set_hw_reg(hw,
					      HW_VAR_RCR, (u8 *) (&reg_rcr));
		_rtl92cu_set_bcn_ctrl_reg(hw, tmp, 0);
	}
}

/*========================================================================== */

int rtl92cu_set_network_type(struct ieee80211_hw *hw, enum nl80211_iftype type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (_rtl92cu_set_media_status(hw, type))
		return -EOPNOTSUPP;

	if (rtlpriv->mac80211.link_state == MAC80211_LINKED) {
		if (type != NL80211_IFTYPE_AP)
			rtl92cu_set_check_bssid(hw, true);
	} else {
		rtl92cu_set_check_bssid(hw, false);
	}

	return 0;
}

static void _InitBeaconParameters(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);

	rtl_write_word(rtlpriv, REG_BCN_CTRL, 0x1010);

	/* TODO: Remove these magic number */
	rtl_write_word(rtlpriv, REG_TBTT_PROHIBIT, 0x6404);
	rtl_write_byte(rtlpriv, REG_DRVERLYINT, DRIVER_EARLY_INT_TIME);
	rtl_write_byte(rtlpriv, REG_BCNDMATIM, BCN_DMA_ATIME_INT_TIME);
	/* Change beacon AIFS to the largest number
	 * beacause test chip does not contension before sending beacon. */
	if (IS_NORMAL_CHIP(rtlhal->version))
		rtl_write_word(rtlpriv, REG_BCNTCFG, 0x660F);
	else
		rtl_write_word(rtlpriv, REG_BCNTCFG, 0x66FF);
}

static void _beacon_function_enable(struct ieee80211_hw *hw, bool Enable,
				    bool Linked)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	_rtl92cu_set_bcn_ctrl_reg(hw, (BIT(4) | BIT(3) | BIT(1)), 0x00);
	rtl_write_byte(rtlpriv, REG_RD_CTRL+1, 0x6F);
}

void rtl92cu_set_beacon_related_registers(struct ieee80211_hw *hw)
{

	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u16 bcn_interval, atim_window;
	u32 value32;

	bcn_interval = mac->beacon_interval;
	atim_window = 2;	/*FIX MERGE */
	rtl_write_word(rtlpriv, REG_ATIMWND, atim_window);
	rtl_write_word(rtlpriv, REG_BCN_INTERVAL, bcn_interval);
	_InitBeaconParameters(hw);
	rtl_write_byte(rtlpriv, REG_SLOT, 0x09);
	/*
	 * Force beacon frame transmission even after receiving beacon frame
	 * from other ad hoc STA
	 *
	 *
	 * Reset TSF Timer to zero, added by Roger. 2008.06.24
	 */
	value32 = rtl_read_dword(rtlpriv, REG_TCR);
	value32 &= ~TSFRST;
	rtl_write_dword(rtlpriv, REG_TCR, value32);
	value32 |= TSFRST;
	rtl_write_dword(rtlpriv, REG_TCR, value32);
	RT_TRACE(rtlpriv, COMP_INIT|COMP_BEACON, DBG_LOUD,
		 "SetBeaconRelatedRegisters8192CUsb(): Set TCR(%x)\n",
		 value32);
	/* TODO: Modify later (Find the right parameters)
	 * NOTE: Fix test chip's bug (about contention windows's randomness) */
	if ((mac->opmode == NL80211_IFTYPE_ADHOC) ||
	    (mac->opmode == NL80211_IFTYPE_AP)) {
		rtl_write_byte(rtlpriv, REG_RXTSF_OFFSET_CCK, 0x50);
		rtl_write_byte(rtlpriv, REG_RXTSF_OFFSET_OFDM, 0x50);
	}
	_beacon_function_enable(hw, true, true);
}

void rtl92cu_set_beacon_interval(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u16 bcn_interval = mac->beacon_interval;

	RT_TRACE(rtlpriv, COMP_BEACON, DBG_DMESG, "beacon_interval:%d\n",
		 bcn_interval);
	rtl_write_word(rtlpriv, REG_BCN_INTERVAL, bcn_interval);
}

void rtl92cu_update_interrupt_mask(struct ieee80211_hw *hw,
				   u32 add_msr, u32 rm_msr)
{
}

void rtl92cu_get_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));

	switch (variable) {
	case HW_VAR_RCR:
		*((u32 *)(val)) = mac->rx_conf;
		break;
	case HW_VAR_RF_STATE:
		*((enum rf_pwrstate *)(val)) = ppsc->rfpwr_state;
		break;
	case HW_VAR_FWLPS_RF_ON:{
			enum rf_pwrstate rfState;
			u32 val_rcr;

			rtlpriv->cfg->ops->get_hw_reg(hw, HW_VAR_RF_STATE,
						      (u8 *)(&rfState));
			if (rfState == ERFOFF) {
				*((bool *) (val)) = true;
			} else {
				val_rcr = rtl_read_dword(rtlpriv, REG_RCR);
				val_rcr &= 0x00070000;
				if (val_rcr)
					*((bool *) (val)) = false;
				else
					*((bool *) (val)) = true;
			}
			break;
		}
	case HW_VAR_FW_PSMODE_STATUS:
		*((bool *) (val)) = ppsc->fw_current_inpsmode;
		break;
	case HW_VAR_CORRECT_TSF:{
			u64 tsf;
			u32 *ptsf_low = (u32 *)&tsf;
			u32 *ptsf_high = ((u32 *)&tsf) + 1;

			*ptsf_high = rtl_read_dword(rtlpriv, (REG_TSFTR + 4));
			*ptsf_low = rtl_read_dword(rtlpriv, REG_TSFTR);
			*((u64 *)(val)) = tsf;
			break;
		}
	case HW_VAR_MGT_FILTER:
		*((u16 *) (val)) = rtl_read_word(rtlpriv, REG_RXFLTMAP0);
		break;
	case HW_VAR_CTRL_FILTER:
		*((u16 *) (val)) = rtl_read_word(rtlpriv, REG_RXFLTMAP1);
		break;
	case HW_VAR_DATA_FILTER:
		*((u16 *) (val)) = rtl_read_word(rtlpriv, REG_RXFLTMAP2);
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "switch case not processed\n");
		break;
	}
}

void rtl92cu_set_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_usb *rtlusb = rtl_usbdev(rtl_usbpriv(hw));
	enum wireless_mode wirelessmode = mac->mode;
	u8 idx = 0;

	switch (variable) {
	case HW_VAR_ETHER_ADDR:{
			for (idx = 0; idx < ETH_ALEN; idx++) {
				rtl_write_byte(rtlpriv, (REG_MACID + idx),
					       val[idx]);
			}
			break;
		}
	case HW_VAR_BASIC_RATE:{
			u16 rate_cfg = ((u16 *) val)[0];
			u8 rate_index = 0;

			rate_cfg &= 0x15f;
			/* TODO */
			/* if (mac->current_network.vender == HT_IOT_PEER_CISCO
			 *     && ((rate_cfg & 0x150) == 0)) {
			 *	  rate_cfg |= 0x010;
			 * } */
			rate_cfg |= 0x01;
			rtl_write_byte(rtlpriv, REG_RRSR, rate_cfg & 0xff);
			rtl_write_byte(rtlpriv, REG_RRSR + 1,
				       (rate_cfg >> 8) & 0xff);
			while (rate_cfg > 0x1) {
				rate_cfg >>= 1;
				rate_index++;
			}
			rtl_write_byte(rtlpriv, REG_INIRTS_RATE_SEL,
				       rate_index);
			break;
		}
	case HW_VAR_BSSID:{
			for (idx = 0; idx < ETH_ALEN; idx++) {
				rtl_write_byte(rtlpriv, (REG_BSSID + idx),
					       val[idx]);
			}
			break;
		}
	case HW_VAR_SIFS:{
			rtl_write_byte(rtlpriv, REG_SIFS_CCK + 1, val[0]);
			rtl_write_byte(rtlpriv, REG_SIFS_OFDM + 1, val[1]);
			rtl_write_byte(rtlpriv, REG_SPEC_SIFS + 1, val[0]);
			rtl_write_byte(rtlpriv, REG_MAC_SPEC_SIFS + 1, val[0]);
			rtl_write_byte(rtlpriv, REG_R2T_SIFS+1, val[0]);
			rtl_write_byte(rtlpriv, REG_T2T_SIFS+1, val[0]);
			RT_TRACE(rtlpriv, COMP_MLME, DBG_LOUD, "HW_VAR_SIFS\n");
			break;
		}
	case HW_VAR_SLOT_TIME:{
			u8 e_aci;
			u8 QOS_MODE = 1;

			rtl_write_byte(rtlpriv, REG_SLOT, val[0]);
			RT_TRACE(rtlpriv, COMP_MLME, DBG_LOUD,
				 "HW_VAR_SLOT_TIME %x\n", val[0]);
			if (QOS_MODE) {
				for (e_aci = 0; e_aci < AC_MAX; e_aci++)
					rtlpriv->cfg->ops->set_hw_reg(hw,
								HW_VAR_AC_PARAM,
								&e_aci);
			} else {
				u8 sifstime = 0;
				u8	u1bAIFS;

				if (IS_WIRELESS_MODE_A(wirelessmode) ||
				    IS_WIRELESS_MODE_N_24G(wirelessmode) ||
				    IS_WIRELESS_MODE_N_5G(wirelessmode))
					sifstime = 16;
				else
					sifstime = 10;
				u1bAIFS = sifstime + (2 *  val[0]);
				rtl_write_byte(rtlpriv, REG_EDCA_VO_PARAM,
					       u1bAIFS);
				rtl_write_byte(rtlpriv, REG_EDCA_VI_PARAM,
					       u1bAIFS);
				rtl_write_byte(rtlpriv, REG_EDCA_BE_PARAM,
					       u1bAIFS);
				rtl_write_byte(rtlpriv, REG_EDCA_BK_PARAM,
					       u1bAIFS);
			}
			break;
		}
	case HW_VAR_ACK_PREAMBLE:{
			u8 reg_tmp;
			u8 short_preamble = (bool)*val;
			reg_tmp = 0;
			if (short_preamble)
				reg_tmp |= 0x80;
			rtl_write_byte(rtlpriv, REG_RRSR + 2, reg_tmp);
			break;
		}
	case HW_VAR_AMPDU_MIN_SPACE:{
			u8 min_spacing_to_set;
			u8 sec_min_space;

			min_spacing_to_set = *val;
			if (min_spacing_to_set <= 7) {
				switch (rtlpriv->sec.pairwise_enc_algorithm) {
				case NO_ENCRYPTION:
				case AESCCMP_ENCRYPTION:
					sec_min_space = 0;
					break;
				case WEP40_ENCRYPTION:
				case WEP104_ENCRYPTION:
				case TKIP_ENCRYPTION:
					sec_min_space = 6;
					break;
				default:
					sec_min_space = 7;
					break;
				}
				if (min_spacing_to_set < sec_min_space)
					min_spacing_to_set = sec_min_space;
				mac->min_space_cfg = ((mac->min_space_cfg &
						     0xf8) |
						     min_spacing_to_set);
				*val = min_spacing_to_set;
				RT_TRACE(rtlpriv, COMP_MLME, DBG_LOUD,
					 "Set HW_VAR_AMPDU_MIN_SPACE: %#x\n",
					 mac->min_space_cfg);
				rtl_write_byte(rtlpriv, REG_AMPDU_MIN_SPACE,
					       mac->min_space_cfg);
			}
			break;
		}
	case HW_VAR_SHORTGI_DENSITY:{
			u8 density_to_set;

			density_to_set = *val;
			density_to_set &= 0x1f;
			mac->min_space_cfg &= 0x07;
			mac->min_space_cfg |= (density_to_set << 3);
			RT_TRACE(rtlpriv, COMP_MLME, DBG_LOUD,
				 "Set HW_VAR_SHORTGI_DENSITY: %#x\n",
				 mac->min_space_cfg);
			rtl_write_byte(rtlpriv, REG_AMPDU_MIN_SPACE,
				       mac->min_space_cfg);
			break;
		}
	case HW_VAR_AMPDU_FACTOR:{
			u8 regtoset_normal[4] = {0x41, 0xa8, 0x72, 0xb9};
			u8 factor_toset;
			u8 *p_regtoset = NULL;
			u8 index = 0;

			p_regtoset = regtoset_normal;
			factor_toset = *val;
			if (factor_toset <= 3) {
				factor_toset = (1 << (factor_toset + 2));
				if (factor_toset > 0xf)
					factor_toset = 0xf;
				for (index = 0; index < 4; index++) {
					if ((p_regtoset[index] & 0xf0) >
					    (factor_toset << 4))
						p_regtoset[index] =
						     (p_regtoset[index] & 0x0f)
						     | (factor_toset << 4);
					if ((p_regtoset[index] & 0x0f) >
					     factor_toset)
						p_regtoset[index] =
						     (p_regtoset[index] & 0xf0)
						     | (factor_toset);
					rtl_write_byte(rtlpriv,
						       (REG_AGGLEN_LMT + index),
						       p_regtoset[index]);
				}
				RT_TRACE(rtlpriv, COMP_MLME, DBG_LOUD,
					 "Set HW_VAR_AMPDU_FACTOR: %#x\n",
					 factor_toset);
			}
			break;
		}
	case HW_VAR_AC_PARAM:{
			u8 e_aci = *val;
			u32 u4b_ac_param;
			u16 cw_min = le16_to_cpu(mac->ac[e_aci].cw_min);
			u16 cw_max = le16_to_cpu(mac->ac[e_aci].cw_max);
			u16 tx_op = le16_to_cpu(mac->ac[e_aci].tx_op);

			u4b_ac_param = (u32) mac->ac[e_aci].aifs;
			u4b_ac_param |= (u32) ((cw_min & 0xF) <<
					 AC_PARAM_ECW_MIN_OFFSET);
			u4b_ac_param |= (u32) ((cw_max & 0xF) <<
					 AC_PARAM_ECW_MAX_OFFSET);
			u4b_ac_param |= (u32) tx_op << AC_PARAM_TXOP_OFFSET;
			RT_TRACE(rtlpriv, COMP_MLME, DBG_LOUD,
				 "queue:%x, ac_param:%x\n",
				 e_aci, u4b_ac_param);
			switch (e_aci) {
			case AC1_BK:
				rtl_write_dword(rtlpriv, REG_EDCA_BK_PARAM,
						u4b_ac_param);
				break;
			case AC0_BE:
				rtl_write_dword(rtlpriv, REG_EDCA_BE_PARAM,
						u4b_ac_param);
				break;
			case AC2_VI:
				rtl_write_dword(rtlpriv, REG_EDCA_VI_PARAM,
						u4b_ac_param);
				break;
			case AC3_VO:
				rtl_write_dword(rtlpriv, REG_EDCA_VO_PARAM,
						u4b_ac_param);
				break;
			default:
				RT_ASSERT(false,
					  "SetHwReg8185(): invalid aci: %d !\n",
					  e_aci);
				break;
			}
			if (rtlusb->acm_method != eAcmWay2_SW)
				rtlpriv->cfg->ops->set_hw_reg(hw,
					 HW_VAR_ACM_CTRL, &e_aci);
			break;
		}
	case HW_VAR_ACM_CTRL:{
			u8 e_aci = *val;
			union aci_aifsn *p_aci_aifsn = (union aci_aifsn *)
							(&(mac->ac[0].aifs));
			u8 acm = p_aci_aifsn->f.acm;
			u8 acm_ctrl = rtl_read_byte(rtlpriv, REG_ACMHWCTRL);

			acm_ctrl =
			    acm_ctrl | ((rtlusb->acm_method == 2) ? 0x0 : 0x1);
			if (acm) {
				switch (e_aci) {
				case AC0_BE:
					acm_ctrl |= AcmHw_BeqEn;
					break;
				case AC2_VI:
					acm_ctrl |= AcmHw_ViqEn;
					break;
				case AC3_VO:
					acm_ctrl |= AcmHw_VoqEn;
					break;
				default:
					RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
						 "HW_VAR_ACM_CTRL acm set failed: eACI is %d\n",
						 acm);
					break;
				}
			} else {
				switch (e_aci) {
				case AC0_BE:
					acm_ctrl &= (~AcmHw_BeqEn);
					break;
				case AC2_VI:
					acm_ctrl &= (~AcmHw_ViqEn);
					break;
				case AC3_VO:
					acm_ctrl &= (~AcmHw_BeqEn);
					break;
				default:
					RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
						 "switch case not processed\n");
					break;
				}
			}
			RT_TRACE(rtlpriv, COMP_QOS, DBG_TRACE,
				 "SetHwReg8190pci(): [HW_VAR_ACM_CTRL] Write 0x%X\n",
				 acm_ctrl);
			rtl_write_byte(rtlpriv, REG_ACMHWCTRL, acm_ctrl);
			break;
		}
	case HW_VAR_RCR:{
			rtl_write_dword(rtlpriv, REG_RCR, ((u32 *) (val))[0]);
			mac->rx_conf = ((u32 *) (val))[0];
			RT_TRACE(rtlpriv, COMP_RECV, DBG_DMESG,
				 "### Set RCR(0x%08x) ###\n", mac->rx_conf);
			break;
		}
	case HW_VAR_RETRY_LIMIT:{
			u8 retry_limit = val[0];

			rtl_write_word(rtlpriv, REG_RL,
				       retry_limit << RETRY_LIMIT_SHORT_SHIFT |
				       retry_limit << RETRY_LIMIT_LONG_SHIFT);
			RT_TRACE(rtlpriv, COMP_MLME, DBG_DMESG,
				 "Set HW_VAR_RETRY_LIMIT(0x%08x)\n",
				 retry_limit);
			break;
		}
	case HW_VAR_DUAL_TSF_RST:
		rtl_write_byte(rtlpriv, REG_DUAL_TSF_RST, (BIT(0) | BIT(1)));
		break;
	case HW_VAR_EFUSE_BYTES:
		rtlefuse->efuse_usedbytes = *((u16 *) val);
		break;
	case HW_VAR_EFUSE_USAGE:
		rtlefuse->efuse_usedpercentage = *val;
		break;
	case HW_VAR_IO_CMD:
		rtl92c_phy_set_io_cmd(hw, (*(enum io_type *)val));
		break;
	case HW_VAR_WPA_CONFIG:
		rtl_write_byte(rtlpriv, REG_SECCFG, *val);
		break;
	case HW_VAR_SET_RPWM:{
			u8 rpwm_val = rtl_read_byte(rtlpriv, REG_USB_HRPWM);

			if (rpwm_val & BIT(7))
				rtl_write_byte(rtlpriv, REG_USB_HRPWM, *val);
			else
				rtl_write_byte(rtlpriv, REG_USB_HRPWM,
					       *val | BIT(7));
			break;
		}
	case HW_VAR_H2C_FW_PWRMODE:{
			u8 psmode = *val;

			if ((psmode != FW_PS_ACTIVE_MODE) &&
			   (!IS_92C_SERIAL(rtlhal->version)))
				rtl92c_dm_rf_saving(hw, true);
			rtl92c_set_fw_pwrmode_cmd(hw, (*val));
			break;
		}
	case HW_VAR_FW_PSMODE_STATUS:
		ppsc->fw_current_inpsmode = *((bool *) val);
		break;
	case HW_VAR_H2C_FW_JOINBSSRPT:{
			u8 mstatus = *val;
			u8 tmp_reg422;
			bool recover = false;

			if (mstatus == RT_MEDIA_CONNECT) {
				rtlpriv->cfg->ops->set_hw_reg(hw,
							 HW_VAR_AID, NULL);
				rtl_write_byte(rtlpriv, REG_CR + 1, 0x03);
				_rtl92cu_set_bcn_ctrl_reg(hw, 0, BIT(3));
				_rtl92cu_set_bcn_ctrl_reg(hw, BIT(4), 0);
				tmp_reg422 = rtl_read_byte(rtlpriv,
							REG_FWHW_TXQ_CTRL + 2);
				if (tmp_reg422 & BIT(6))
					recover = true;
				rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2,
					       tmp_reg422 & (~BIT(6)));
				rtl92c_set_fw_rsvdpagepkt(hw, 0);
				_rtl92cu_set_bcn_ctrl_reg(hw, BIT(3), 0);
				_rtl92cu_set_bcn_ctrl_reg(hw, 0, BIT(4));
				if (recover)
					rtl_write_byte(rtlpriv,
						 REG_FWHW_TXQ_CTRL + 2,
						tmp_reg422 | BIT(6));
				rtl_write_byte(rtlpriv, REG_CR + 1, 0x02);
			}
			rtl92c_set_fw_joinbss_report_cmd(hw, (*val));
			break;
		}
	case HW_VAR_AID:{
			u16 u2btmp;

			u2btmp = rtl_read_word(rtlpriv, REG_BCN_PSR_RPT);
			u2btmp &= 0xC000;
			rtl_write_word(rtlpriv, REG_BCN_PSR_RPT,
				       (u2btmp | mac->assoc_id));
			break;
		}
	case HW_VAR_CORRECT_TSF:{
			u8 btype_ibss = val[0];

			if (btype_ibss)
				_rtl92cu_stop_tx_beacon(hw);
			_rtl92cu_set_bcn_ctrl_reg(hw, 0, BIT(3));
			rtl_write_dword(rtlpriv, REG_TSFTR, (u32)(mac->tsf &
					0xffffffff));
			rtl_write_dword(rtlpriv, REG_TSFTR + 4,
					(u32)((mac->tsf >> 32) & 0xffffffff));
			_rtl92cu_set_bcn_ctrl_reg(hw, BIT(3), 0);
			if (btype_ibss)
				_rtl92cu_resume_tx_beacon(hw);
			break;
		}
	case HW_VAR_MGT_FILTER:
		rtl_write_word(rtlpriv, REG_RXFLTMAP0, *(u16 *)val);
		break;
	case HW_VAR_CTRL_FILTER:
		rtl_write_word(rtlpriv, REG_RXFLTMAP1, *(u16 *)val);
		break;
	case HW_VAR_DATA_FILTER:
		rtl_write_word(rtlpriv, REG_RXFLTMAP2, *(u16 *)val);
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "switch case not processed\n");
		break;
	}
}

static void rtl92cu_update_hal_rate_table(struct ieee80211_hw *hw,
					  struct ieee80211_sta *sta)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u32 ratr_value;
	u8 ratr_index = 0;
	u8 nmode = mac->ht_enable;
	u8 mimo_ps = IEEE80211_SMPS_OFF;
	u16 shortgi_rate;
	u32 tmp_ratr_value;
	u8 curtxbw_40mhz = mac->bw_40;
	u8 curshortgi_40mhz = (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_40) ?
			       1 : 0;
	u8 curshortgi_20mhz = (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_20) ?
			       1 : 0;
	enum wireless_mode wirelessmode = mac->mode;

	if (rtlhal->current_bandtype == BAND_ON_5G)
		ratr_value = sta->supp_rates[1] << 4;
	else
		ratr_value = sta->supp_rates[0];
	if (mac->opmode == NL80211_IFTYPE_ADHOC)
		ratr_value = 0xfff;

	ratr_value |= (sta->ht_cap.mcs.rx_mask[1] << 20 |
			sta->ht_cap.mcs.rx_mask[0] << 12);
	switch (wirelessmode) {
	case WIRELESS_MODE_B:
		if (ratr_value & 0x0000000c)
			ratr_value &= 0x0000000d;
		else
			ratr_value &= 0x0000000f;
		break;
	case WIRELESS_MODE_G:
		ratr_value &= 0x00000FF5;
		break;
	case WIRELESS_MODE_N_24G:
	case WIRELESS_MODE_N_5G:
		nmode = 1;
		if (mimo_ps == IEEE80211_SMPS_STATIC) {
			ratr_value &= 0x0007F005;
		} else {
			u32 ratr_mask;

			if (get_rf_type(rtlphy) == RF_1T2R ||
			    get_rf_type(rtlphy) == RF_1T1R)
				ratr_mask = 0x000ff005;
			else
				ratr_mask = 0x0f0ff005;

			ratr_value &= ratr_mask;
		}
		break;
	default:
		if (rtlphy->rf_type == RF_1T2R)
			ratr_value &= 0x000ff0ff;
		else
			ratr_value &= 0x0f0ff0ff;

		break;
	}

	ratr_value &= 0x0FFFFFFF;

	if (nmode && ((curtxbw_40mhz &&
			 curshortgi_40mhz) || (!curtxbw_40mhz &&
					       curshortgi_20mhz))) {

		ratr_value |= 0x10000000;
		tmp_ratr_value = (ratr_value >> 12);

		for (shortgi_rate = 15; shortgi_rate > 0; shortgi_rate--) {
			if ((1 << shortgi_rate) & tmp_ratr_value)
				break;
		}

		shortgi_rate = (shortgi_rate << 12) | (shortgi_rate << 8) |
		    (shortgi_rate << 4) | (shortgi_rate);
	}

	rtl_write_dword(rtlpriv, REG_ARFR0 + ratr_index * 4, ratr_value);

	RT_TRACE(rtlpriv, COMP_RATR, DBG_DMESG, "%x\n",
		 rtl_read_dword(rtlpriv, REG_ARFR0));
}

static void rtl92cu_update_hal_rate_mask(struct ieee80211_hw *hw,
					 struct ieee80211_sta *sta,
					 u8 rssi_level)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_sta_info *sta_entry = NULL;
	u32 ratr_bitmap;
	u8 ratr_index;
	u8 curtxbw_40mhz = (sta->bandwidth >= IEEE80211_STA_RX_BW_40) ? 1 : 0;
	u8 curshortgi_40mhz = curtxbw_40mhz &&
			      (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_40) ?
				1 : 0;
	u8 curshortgi_20mhz = (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_20) ?
				1 : 0;
	enum wireless_mode wirelessmode = 0;
	bool shortgi = false;
	u8 rate_mask[5];
	u8 macid = 0;
	u8 mimo_ps = IEEE80211_SMPS_OFF;

	sta_entry = (struct rtl_sta_info *) sta->drv_priv;
	wirelessmode = sta_entry->wireless_mode;
	if (mac->opmode == NL80211_IFTYPE_STATION ||
	    mac->opmode == NL80211_IFTYPE_MESH_POINT)
		curtxbw_40mhz = mac->bw_40;
	else if (mac->opmode == NL80211_IFTYPE_AP ||
		mac->opmode == NL80211_IFTYPE_ADHOC)
		macid = sta->aid + 1;

	if (rtlhal->current_bandtype == BAND_ON_5G)
		ratr_bitmap = sta->supp_rates[1] << 4;
	else
		ratr_bitmap = sta->supp_rates[0];
	if (mac->opmode == NL80211_IFTYPE_ADHOC)
		ratr_bitmap = 0xfff;
	ratr_bitmap |= (sta->ht_cap.mcs.rx_mask[1] << 20 |
			sta->ht_cap.mcs.rx_mask[0] << 12);
	switch (wirelessmode) {
	case WIRELESS_MODE_B:
		ratr_index = RATR_INX_WIRELESS_B;
		if (ratr_bitmap & 0x0000000c)
			ratr_bitmap &= 0x0000000d;
		else
			ratr_bitmap &= 0x0000000f;
		break;
	case WIRELESS_MODE_G:
		ratr_index = RATR_INX_WIRELESS_GB;

		if (rssi_level == 1)
			ratr_bitmap &= 0x00000f00;
		else if (rssi_level == 2)
			ratr_bitmap &= 0x00000ff0;
		else
			ratr_bitmap &= 0x00000ff5;
		break;
	case WIRELESS_MODE_A:
		ratr_index = RATR_INX_WIRELESS_A;
		ratr_bitmap &= 0x00000ff0;
		break;
	case WIRELESS_MODE_N_24G:
	case WIRELESS_MODE_N_5G:
		ratr_index = RATR_INX_WIRELESS_NGB;

		if (mimo_ps == IEEE80211_SMPS_STATIC) {
			if (rssi_level == 1)
				ratr_bitmap &= 0x00070000;
			else if (rssi_level == 2)
				ratr_bitmap &= 0x0007f000;
			else
				ratr_bitmap &= 0x0007f005;
		} else {
			if (rtlphy->rf_type == RF_1T2R ||
			    rtlphy->rf_type == RF_1T1R) {
				if (curtxbw_40mhz) {
					if (rssi_level == 1)
						ratr_bitmap &= 0x000f0000;
					else if (rssi_level == 2)
						ratr_bitmap &= 0x000ff000;
					else
						ratr_bitmap &= 0x000ff015;
				} else {
					if (rssi_level == 1)
						ratr_bitmap &= 0x000f0000;
					else if (rssi_level == 2)
						ratr_bitmap &= 0x000ff000;
					else
						ratr_bitmap &= 0x000ff005;
				}
			} else {
				if (curtxbw_40mhz) {
					if (rssi_level == 1)
						ratr_bitmap &= 0x0f0f0000;
					else if (rssi_level == 2)
						ratr_bitmap &= 0x0f0ff000;
					else
						ratr_bitmap &= 0x0f0ff015;
				} else {
					if (rssi_level == 1)
						ratr_bitmap &= 0x0f0f0000;
					else if (rssi_level == 2)
						ratr_bitmap &= 0x0f0ff000;
					else
						ratr_bitmap &= 0x0f0ff005;
				}
			}
		}

		if ((curtxbw_40mhz && curshortgi_40mhz) ||
		    (!curtxbw_40mhz && curshortgi_20mhz)) {

			if (macid == 0)
				shortgi = true;
			else if (macid == 1)
				shortgi = false;
		}
		break;
	default:
		ratr_index = RATR_INX_WIRELESS_NGB;

		if (rtlphy->rf_type == RF_1T2R)
			ratr_bitmap &= 0x000ff0ff;
		else
			ratr_bitmap &= 0x0f0ff0ff;
		break;
	}
	sta_entry->ratr_index = ratr_index;

	RT_TRACE(rtlpriv, COMP_RATR, DBG_DMESG,
		 "ratr_bitmap :%x\n", ratr_bitmap);
	*(u32 *)&rate_mask = (ratr_bitmap & 0x0fffffff) |
				     (ratr_index << 28);
	rate_mask[4] = macid | (shortgi ? 0x20 : 0x00) | 0x80;
	RT_TRACE(rtlpriv, COMP_RATR, DBG_DMESG,
		 "Rate_index:%x, ratr_val:%x, %5phC\n",
		 ratr_index, ratr_bitmap, rate_mask);
	memcpy(rtlpriv->rate_mask, rate_mask, 5);
	/* rtl92c_fill_h2c_cmd() does USB I/O and will result in a
	 * "scheduled while atomic" if called directly */
	schedule_work(&rtlpriv->works.fill_h2c_cmd);

	if (macid != 0)
		sta_entry->ratr_index = ratr_index;
}

void rtl92cu_update_hal_rate_tbl(struct ieee80211_hw *hw,
				 struct ieee80211_sta *sta,
				 u8 rssi_level)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (rtlpriv->dm.useramask)
		rtl92cu_update_hal_rate_mask(hw, sta, rssi_level);
	else
		rtl92cu_update_hal_rate_table(hw, sta);
}

void rtl92cu_update_channel_access_setting(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u16 sifs_timer;

	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_SLOT_TIME,
				      &mac->slot_time);
	if (!mac->ht_enable)
		sifs_timer = 0x0a0a;
	else
		sifs_timer = 0x0e0e;
	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_SIFS, (u8 *)&sifs_timer);
}

bool rtl92cu_gpio_radio_on_off_checking(struct ieee80211_hw *hw, u8 * valid)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	enum rf_pwrstate e_rfpowerstate_toset, cur_rfstate;
	u8 u1tmp = 0;
	bool actuallyset = false;
	unsigned long flag = 0;
	/* to do - usb autosuspend */
	u8 usb_autosuspend = 0;

	if (ppsc->swrf_processing)
		return false;
	spin_lock_irqsave(&rtlpriv->locks.rf_ps_lock, flag);
	if (ppsc->rfchange_inprogress) {
		spin_unlock_irqrestore(&rtlpriv->locks.rf_ps_lock, flag);
		return false;
	} else {
		ppsc->rfchange_inprogress = true;
		spin_unlock_irqrestore(&rtlpriv->locks.rf_ps_lock, flag);
	}
	cur_rfstate = ppsc->rfpwr_state;
	if (usb_autosuspend) {
		/* to do................... */
	} else {
		if (ppsc->pwrdown_mode) {
			u1tmp = rtl_read_byte(rtlpriv, REG_HSISR);
			e_rfpowerstate_toset = (u1tmp & BIT(7)) ?
					       ERFOFF : ERFON;
			RT_TRACE(rtlpriv, COMP_POWER, DBG_DMESG,
				 "pwrdown, 0x5c(BIT7)=%02x\n", u1tmp);
		} else {
			rtl_write_byte(rtlpriv, REG_MAC_PINMUX_CFG,
				       rtl_read_byte(rtlpriv,
				       REG_MAC_PINMUX_CFG) & ~(BIT(3)));
			u1tmp = rtl_read_byte(rtlpriv, REG_GPIO_IO_SEL);
			e_rfpowerstate_toset  = (u1tmp & BIT(3)) ?
						 ERFON : ERFOFF;
			RT_TRACE(rtlpriv, COMP_POWER, DBG_DMESG,
				 "GPIO_IN=%02x\n", u1tmp);
		}
		RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD, "N-SS RF =%x\n",
			 e_rfpowerstate_toset);
	}
	if ((ppsc->hwradiooff) && (e_rfpowerstate_toset == ERFON)) {
		RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
			 "GPIOChangeRF  - HW Radio ON, RF ON\n");
		ppsc->hwradiooff = false;
		actuallyset = true;
	} else if ((!ppsc->hwradiooff) && (e_rfpowerstate_toset  ==
		    ERFOFF)) {
		RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
			 "GPIOChangeRF  - HW Radio OFF\n");
		ppsc->hwradiooff = true;
		actuallyset = true;
	} else {
		RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
			 "pHalData->bHwRadioOff and eRfPowerStateToSet do not match: pHalData->bHwRadioOff %x, eRfPowerStateToSet %x\n",
			 ppsc->hwradiooff, e_rfpowerstate_toset);
	}
	if (actuallyset) {
		ppsc->hwradiooff = true;
		if (e_rfpowerstate_toset == ERFON) {
			if ((ppsc->reg_rfps_level  & RT_RF_OFF_LEVL_ASPM) &&
			     RT_IN_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_ASPM))
				RT_CLEAR_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_ASPM);
			else if ((ppsc->reg_rfps_level  & RT_RF_OFF_LEVL_PCI_D3)
				 && RT_IN_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_PCI_D3))
				RT_CLEAR_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_PCI_D3);
		}
		spin_lock_irqsave(&rtlpriv->locks.rf_ps_lock, flag);
		ppsc->rfchange_inprogress = false;
		spin_unlock_irqrestore(&rtlpriv->locks.rf_ps_lock, flag);
		/* For power down module, we need to enable register block
		 * contrl reg at 0x1c. Then enable power down control bit
		 * of register 0x04 BIT4 and BIT15 as 1.
		 */
		if (ppsc->pwrdown_mode && e_rfpowerstate_toset == ERFOFF) {
			/* Enable register area 0x0-0xc. */
			rtl_write_byte(rtlpriv, REG_RSV_CTRL, 0x0);
			if (IS_HARDWARE_TYPE_8723U(rtlhal)) {
				/*
				 * We should configure HW PDn source for WiFi
				 * ONLY, and then our HW will be set in
				 * power-down mode if PDn source from all
				 * functions are configured.
				 */
				u1tmp = rtl_read_byte(rtlpriv,
						      REG_MULTI_FUNC_CTRL);
				rtl_write_byte(rtlpriv, REG_MULTI_FUNC_CTRL,
					       (u1tmp|WL_HWPDN_EN));
			} else {
				rtl_write_word(rtlpriv, REG_APS_FSMCO, 0x8812);
			}
		}
		if (e_rfpowerstate_toset == ERFOFF) {
			if (ppsc->reg_rfps_level  & RT_RF_OFF_LEVL_ASPM)
				RT_SET_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_ASPM);
			else if (ppsc->reg_rfps_level & RT_RF_OFF_LEVL_PCI_D3)
				RT_SET_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_PCI_D3);
		}
	} else if (e_rfpowerstate_toset == ERFOFF || cur_rfstate == ERFOFF) {
		/* Enter D3 or ASPM after GPIO had been done. */
		if (ppsc->reg_rfps_level  & RT_RF_OFF_LEVL_ASPM)
			RT_SET_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_ASPM);
		else if (ppsc->reg_rfps_level  & RT_RF_OFF_LEVL_PCI_D3)
			RT_SET_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_PCI_D3);
		spin_lock_irqsave(&rtlpriv->locks.rf_ps_lock, flag);
		ppsc->rfchange_inprogress = false;
		spin_unlock_irqrestore(&rtlpriv->locks.rf_ps_lock, flag);
	} else {
		spin_lock_irqsave(&rtlpriv->locks.rf_ps_lock, flag);
		ppsc->rfchange_inprogress = false;
		spin_unlock_irqrestore(&rtlpriv->locks.rf_ps_lock, flag);
	}
	*valid = 1;
	return !ppsc->hwradiooff;
}
