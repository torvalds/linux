// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2009-2013  Realtek Corporation.*/

#include "../wifi.h"
#include "../pci.h"
#include "../ps.h"
#include "reg.h"
#include "def.h"
#include "phy.h"
#include "rf.h"
#include "dm.h"
#include "table.h"

static u32 _rtl88e_phy_rf_serial_read(struct ieee80211_hw *hw,
				      enum radio_path rfpath, u32 offset);
static void _rtl88e_phy_rf_serial_write(struct ieee80211_hw *hw,
					enum radio_path rfpath, u32 offset,
					u32 data);
static u32 _rtl88e_phy_calculate_bit_shift(u32 bitmask)
{
	u32 i = ffs(bitmask);

	return i ? i - 1 : 32;
}
static bool _rtl88e_phy_bb8188e_config_parafile(struct ieee80211_hw *hw);
static bool _rtl88e_phy_config_mac_with_headerfile(struct ieee80211_hw *hw);
static bool phy_config_bb_with_headerfile(struct ieee80211_hw *hw,
					  u8 configtype);
static bool phy_config_bb_with_pghdr(struct ieee80211_hw *hw,
				     u8 configtype);
static void _rtl88e_phy_init_bb_rf_register_definition(struct ieee80211_hw *hw);
static bool _rtl88e_phy_set_sw_chnl_cmdarray(struct swchnlcmd *cmdtable,
					     u32 cmdtableidx, u32 cmdtablesz,
					     enum swchnlcmd_id cmdid, u32 para1,
					     u32 para2, u32 msdelay);
static bool _rtl88e_phy_sw_chnl_step_by_step(struct ieee80211_hw *hw,
					     u8 channel, u8 *stage, u8 *step,
					     u32 *delay);

static long _rtl88e_phy_txpwr_idx_to_dbm(struct ieee80211_hw *hw,
					 enum wireless_mode wirelessmode,
					 u8 txpwridx);
static void rtl88ee_phy_set_rf_on(struct ieee80211_hw *hw);
static void rtl88e_phy_set_io(struct ieee80211_hw *hw);

u32 rtl88e_phy_query_bb_reg(struct ieee80211_hw *hw, u32 regaddr, u32 bitmask)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 returnvalue, originalvalue, bitshift;

	rtl_dbg(rtlpriv, COMP_RF, DBG_TRACE,
		"regaddr(%#x), bitmask(%#x)\n", regaddr, bitmask);
	originalvalue = rtl_read_dword(rtlpriv, regaddr);
	bitshift = _rtl88e_phy_calculate_bit_shift(bitmask);
	returnvalue = (originalvalue & bitmask) >> bitshift;

	rtl_dbg(rtlpriv, COMP_RF, DBG_TRACE,
		"BBR MASK=0x%x Addr[0x%x]=0x%x\n", bitmask,
		regaddr, originalvalue);

	return returnvalue;

}

void rtl88e_phy_set_bb_reg(struct ieee80211_hw *hw,
			   u32 regaddr, u32 bitmask, u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 originalvalue, bitshift;

	rtl_dbg(rtlpriv, COMP_RF, DBG_TRACE,
		"regaddr(%#x), bitmask(%#x), data(%#x)\n",
		regaddr, bitmask, data);

	if (bitmask != MASKDWORD) {
		originalvalue = rtl_read_dword(rtlpriv, regaddr);
		bitshift = _rtl88e_phy_calculate_bit_shift(bitmask);
		data = ((originalvalue & (~bitmask)) | (data << bitshift));
	}

	rtl_write_dword(rtlpriv, regaddr, data);

	rtl_dbg(rtlpriv, COMP_RF, DBG_TRACE,
		"regaddr(%#x), bitmask(%#x), data(%#x)\n",
		regaddr, bitmask, data);
}

u32 rtl88e_phy_query_rf_reg(struct ieee80211_hw *hw,
			    enum radio_path rfpath, u32 regaddr, u32 bitmask)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 original_value, readback_value, bitshift;

	rtl_dbg(rtlpriv, COMP_RF, DBG_TRACE,
		"regaddr(%#x), rfpath(%#x), bitmask(%#x)\n",
		regaddr, rfpath, bitmask);

	spin_lock(&rtlpriv->locks.rf_lock);


	original_value = _rtl88e_phy_rf_serial_read(hw, rfpath, regaddr);
	bitshift = _rtl88e_phy_calculate_bit_shift(bitmask);
	readback_value = (original_value & bitmask) >> bitshift;

	spin_unlock(&rtlpriv->locks.rf_lock);

	rtl_dbg(rtlpriv, COMP_RF, DBG_TRACE,
		"regaddr(%#x), rfpath(%#x), bitmask(%#x), original_value(%#x)\n",
		regaddr, rfpath, bitmask, original_value);
	return readback_value;
}

void rtl88e_phy_set_rf_reg(struct ieee80211_hw *hw,
			   enum radio_path rfpath,
			   u32 regaddr, u32 bitmask, u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 original_value, bitshift;

	rtl_dbg(rtlpriv, COMP_RF, DBG_TRACE,
		"regaddr(%#x), bitmask(%#x), data(%#x), rfpath(%#x)\n",
		regaddr, bitmask, data, rfpath);

	spin_lock(&rtlpriv->locks.rf_lock);

	if (bitmask != RFREG_OFFSET_MASK) {
			original_value = _rtl88e_phy_rf_serial_read(hw,
								    rfpath,
								    regaddr);
			bitshift = _rtl88e_phy_calculate_bit_shift(bitmask);
			data =
			    ((original_value & (~bitmask)) |
			     (data << bitshift));
		}

	_rtl88e_phy_rf_serial_write(hw, rfpath, regaddr, data);


	spin_unlock(&rtlpriv->locks.rf_lock);

	rtl_dbg(rtlpriv, COMP_RF, DBG_TRACE,
		"regaddr(%#x), bitmask(%#x), data(%#x), rfpath(%#x)\n",
		regaddr, bitmask, data, rfpath);
}

static u32 _rtl88e_phy_rf_serial_read(struct ieee80211_hw *hw,
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
		pr_err("return all one\n");
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
	udelay(10);
	rtl_set_bbreg(hw, pphyreg->rfhssi_para2, MASKDWORD, tmplong2);
	udelay(120);
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
	rtl_dbg(rtlpriv, COMP_RF, DBG_TRACE,
		"RFR-%d Addr[0x%x]=0x%x\n",
		rfpath, pphyreg->rf_rb, retvalue);
	return retvalue;
}

static void _rtl88e_phy_rf_serial_write(struct ieee80211_hw *hw,
					enum radio_path rfpath, u32 offset,
					u32 data)
{
	u32 data_and_addr;
	u32 newoffset;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct bb_reg_def *pphyreg = &rtlphy->phyreg_def[rfpath];

	if (RT_CANNOT_IO(hw)) {
		pr_err("stop\n");
		return;
	}
	offset &= 0xff;
	newoffset = offset;
	data_and_addr = ((newoffset << 20) | (data & 0x000fffff)) & 0x0fffffff;
	rtl_set_bbreg(hw, pphyreg->rf3wire_offset, MASKDWORD, data_and_addr);
	rtl_dbg(rtlpriv, COMP_RF, DBG_TRACE,
		"RFW-%d Addr[0x%x]=0x%x\n",
		rfpath, pphyreg->rf3wire_offset, data_and_addr);
}

bool rtl88e_phy_mac_config(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	bool rtstatus = _rtl88e_phy_config_mac_with_headerfile(hw);

	rtl_write_byte(rtlpriv, 0x04CA, 0x0B);
	return rtstatus;
}

bool rtl88e_phy_bb_config(struct ieee80211_hw *hw)
{
	bool rtstatus = true;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u16 regval;
	u8 b_reg_hwparafile = 1;
	u32 tmp;
	_rtl88e_phy_init_bb_rf_register_definition(hw);
	regval = rtl_read_word(rtlpriv, REG_SYS_FUNC_EN);
	rtl_write_word(rtlpriv, REG_SYS_FUNC_EN,
		       regval | BIT(13) | BIT(0) | BIT(1));

	rtl_write_byte(rtlpriv, REG_RF_CTRL, RF_EN | RF_RSTB | RF_SDMRSTB);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN,
		       FEN_PPLL | FEN_PCIEA | FEN_DIO_PCIE |
		       FEN_BB_GLB_RSTN | FEN_BBRSTB);
	tmp = rtl_read_dword(rtlpriv, 0x4c);
	rtl_write_dword(rtlpriv, 0x4c, tmp | BIT(23));
	if (b_reg_hwparafile == 1)
		rtstatus = _rtl88e_phy_bb8188e_config_parafile(hw);
	return rtstatus;
}

bool rtl88e_phy_rf_config(struct ieee80211_hw *hw)
{
	return rtl88e_phy_rf6052_config(hw);
}

static bool _rtl88e_check_condition(struct ieee80211_hw *hw,
				    const u32  condition)
{
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u32 _board = rtlefuse->board_type; /*need efuse define*/
	u32 _interface = rtlhal->interface;
	u32 _platform = 0x08;/*SupportPlatform */
	u32 cond;

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

static void _rtl8188e_config_rf_reg(struct ieee80211_hw *hw, u32 addr,
				    u32 data, enum radio_path rfpath,
				    u32 regaddr)
{
	if (addr == 0xffe) {
		mdelay(50);
	} else if (addr == 0xfd) {
		mdelay(5);
	} else if (addr == 0xfc) {
		mdelay(1);
	} else if (addr == 0xfb) {
		udelay(50);
	} else if (addr == 0xfa) {
		udelay(5);
	} else if (addr == 0xf9) {
		udelay(1);
	} else {
		rtl_set_rfreg(hw, rfpath, regaddr,
			      RFREG_OFFSET_MASK,
			      data);
		udelay(1);
	}
}

static void _rtl8188e_config_rf_radio_a(struct ieee80211_hw *hw,
					u32 addr, u32 data)
{
	u32 content = 0x1000; /*RF Content: radio_a_txt*/
	u32 maskforphyset = (u32)(content & 0xE000);

	_rtl8188e_config_rf_reg(hw, addr, data, RF90_PATH_A,
		addr | maskforphyset);
}

static void _rtl8188e_config_bb_reg(struct ieee80211_hw *hw,
				    u32 addr, u32 data)
{
	if (addr == 0xfe) {
		mdelay(50);
	} else if (addr == 0xfd) {
		mdelay(5);
	} else if (addr == 0xfc) {
		mdelay(1);
	} else if (addr == 0xfb) {
		udelay(50);
	} else if (addr == 0xfa) {
		udelay(5);
	} else if (addr == 0xf9) {
		udelay(1);
	} else {
		rtl_set_bbreg(hw, addr, MASKDWORD, data);
		udelay(1);
	}
}

static bool _rtl88e_phy_bb8188e_config_parafile(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	bool rtstatus;

	rtstatus = phy_config_bb_with_headerfile(hw, BASEBAND_CONFIG_PHY_REG);
	if (!rtstatus) {
		pr_err("Write BB Reg Fail!!\n");
		return false;
	}

	if (!rtlefuse->autoload_failflag) {
		rtlphy->pwrgroup_cnt = 0;
		rtstatus =
		  phy_config_bb_with_pghdr(hw, BASEBAND_CONFIG_PHY_REG);
	}
	if (!rtstatus) {
		pr_err("BB_PG Reg Fail!!\n");
		return false;
	}
	rtstatus =
	  phy_config_bb_with_headerfile(hw, BASEBAND_CONFIG_AGC_TAB);
	if (!rtstatus) {
		pr_err("AGC Table Fail\n");
		return false;
	}
	rtlphy->cck_high_power =
	  (bool)(rtl_get_bbreg(hw, RFPGA0_XA_HSSIPARAMETER2, 0x200));

	return true;
}

static bool _rtl88e_phy_config_mac_with_headerfile(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 i;
	u32 arraylength;
	u32 *ptrarray;

	rtl_dbg(rtlpriv, COMP_INIT, DBG_TRACE, "Read Rtl8188EMACPHY_Array\n");
	arraylength = RTL8188EEMAC_1T_ARRAYLEN;
	ptrarray = RTL8188EEMAC_1T_ARRAY;
	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
		"Img:RTL8188EEMAC_1T_ARRAY LEN %d\n", arraylength);
	for (i = 0; i < arraylength; i = i + 2)
		rtl_write_byte(rtlpriv, ptrarray[i], (u8)ptrarray[i + 1]);
	return true;
}

#define READ_NEXT_PAIR(v1, v2, i)			\
	do {						\
		i += 2; v1 = array_table[i];		\
		v2 = array_table[i+1];			\
	} while (0)

static void handle_branch1(struct ieee80211_hw *hw, u16 arraylen,
			   u32 *array_table)
{
	u32 v1;
	u32 v2;
	int i;

	for (i = 0; i < arraylen; i = i + 2) {
		v1 = array_table[i];
		v2 = array_table[i+1];
		if (v1 < 0xcdcdcdcd) {
			_rtl8188e_config_bb_reg(hw, v1, v2);
		} else { /*This line is the start line of branch.*/
			/* to protect READ_NEXT_PAIR not overrun */
			if (i >= arraylen - 2)
				break;

			if (!_rtl88e_check_condition(hw, array_table[i])) {
				/*Discard the following (offset, data) pairs*/
				READ_NEXT_PAIR(v1, v2, i);
				while (v2 != 0xDEAD &&
				       v2 != 0xCDEF &&
				       v2 != 0xCDCD && i < arraylen - 2)
					READ_NEXT_PAIR(v1, v2, i);
				i -= 2; /* prevent from for-loop += 2*/
			} else { /* Configure matched pairs and skip
				  * to end of if-else.
				  */
				READ_NEXT_PAIR(v1, v2, i);
				while (v2 != 0xDEAD &&
				       v2 != 0xCDEF &&
				       v2 != 0xCDCD && i < arraylen - 2) {
					_rtl8188e_config_bb_reg(hw, v1, v2);
					READ_NEXT_PAIR(v1, v2, i);
				}

				while (v2 != 0xDEAD && i < arraylen - 2)
					READ_NEXT_PAIR(v1, v2, i);
			}
		}
	}
}

static void handle_branch2(struct ieee80211_hw *hw, u16 arraylen,
			   u32 *array_table)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 v1;
	u32 v2;
	int i;

	for (i = 0; i < arraylen; i = i + 2) {
		v1 = array_table[i];
		v2 = array_table[i+1];
		if (v1 < 0xCDCDCDCD) {
			rtl_set_bbreg(hw, array_table[i], MASKDWORD,
				      array_table[i + 1]);
			udelay(1);
			continue;
		} else { /*This line is the start line of branch.*/
			/* to protect READ_NEXT_PAIR not overrun */
			if (i >= arraylen - 2)
				break;

			if (!_rtl88e_check_condition(hw, array_table[i])) {
				/*Discard the following (offset, data) pairs*/
				READ_NEXT_PAIR(v1, v2, i);
				while (v2 != 0xDEAD &&
				       v2 != 0xCDEF &&
				       v2 != 0xCDCD && i < arraylen - 2)
					READ_NEXT_PAIR(v1, v2, i);
				i -= 2; /* prevent from for-loop += 2*/
			} else { /* Configure matched pairs and skip
				  * to end of if-else.
				  */
				READ_NEXT_PAIR(v1, v2, i);
				while (v2 != 0xDEAD &&
				       v2 != 0xCDEF &&
				       v2 != 0xCDCD && i < arraylen - 2) {
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
		rtl_dbg(rtlpriv, COMP_INIT, DBG_TRACE,
			"The agctab_array_table[0] is %x Rtl818EEPHY_REGArray[1] is %x\n",
			array_table[i], array_table[i + 1]);
	}
}

static bool phy_config_bb_with_headerfile(struct ieee80211_hw *hw,
					  u8 configtype)
{
	u32 *array_table;
	u16 arraylen;

	if (configtype == BASEBAND_CONFIG_PHY_REG) {
		arraylen = RTL8188EEPHY_REG_1TARRAYLEN;
		array_table = RTL8188EEPHY_REG_1TARRAY;
		handle_branch1(hw, arraylen, array_table);
	} else if (configtype == BASEBAND_CONFIG_AGC_TAB) {
		arraylen = RTL8188EEAGCTAB_1TARRAYLEN;
		array_table = RTL8188EEAGCTAB_1TARRAY;
		handle_branch2(hw, arraylen, array_table);
	}
	return true;
}

static void store_pwrindex_rate_offset(struct ieee80211_hw *hw,
				       u32 regaddr, u32 bitmask,
				       u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	int count = rtlphy->pwrgroup_cnt;

	if (regaddr == RTXAGC_A_RATE18_06) {
		rtlphy->mcs_txpwrlevel_origoffset[count][0] = data;
		rtl_dbg(rtlpriv, COMP_INIT, DBG_TRACE,
			"MCSTxPowerLevelOriginalOffset[%d][0] = 0x%x\n",
			count,
			rtlphy->mcs_txpwrlevel_origoffset[count][0]);
	}
	if (regaddr == RTXAGC_A_RATE54_24) {
		rtlphy->mcs_txpwrlevel_origoffset[count][1] = data;
		rtl_dbg(rtlpriv, COMP_INIT, DBG_TRACE,
			"MCSTxPowerLevelOriginalOffset[%d][1] = 0x%x\n",
			count,
			rtlphy->mcs_txpwrlevel_origoffset[count][1]);
	}
	if (regaddr == RTXAGC_A_CCK1_MCS32) {
		rtlphy->mcs_txpwrlevel_origoffset[count][6] = data;
		rtl_dbg(rtlpriv, COMP_INIT, DBG_TRACE,
			"MCSTxPowerLevelOriginalOffset[%d][6] = 0x%x\n",
			count,
			rtlphy->mcs_txpwrlevel_origoffset[count][6]);
	}
	if (regaddr == RTXAGC_B_CCK11_A_CCK2_11 && bitmask == 0xffffff00) {
		rtlphy->mcs_txpwrlevel_origoffset[count][7] = data;
		rtl_dbg(rtlpriv, COMP_INIT, DBG_TRACE,
			"MCSTxPowerLevelOriginalOffset[%d][7] = 0x%x\n",
			count,
			rtlphy->mcs_txpwrlevel_origoffset[count][7]);
	}
	if (regaddr == RTXAGC_A_MCS03_MCS00) {
		rtlphy->mcs_txpwrlevel_origoffset[count][2] = data;
		rtl_dbg(rtlpriv, COMP_INIT, DBG_TRACE,
			"MCSTxPowerLevelOriginalOffset[%d][2] = 0x%x\n",
			count,
			rtlphy->mcs_txpwrlevel_origoffset[count][2]);
	}
	if (regaddr == RTXAGC_A_MCS07_MCS04) {
		rtlphy->mcs_txpwrlevel_origoffset[count][3] = data;
		rtl_dbg(rtlpriv, COMP_INIT, DBG_TRACE,
			"MCSTxPowerLevelOriginalOffset[%d][3] = 0x%x\n",
			count,
			rtlphy->mcs_txpwrlevel_origoffset[count][3]);
	}
	if (regaddr == RTXAGC_A_MCS11_MCS08) {
		rtlphy->mcs_txpwrlevel_origoffset[count][4] = data;
		rtl_dbg(rtlpriv, COMP_INIT, DBG_TRACE,
			"MCSTxPowerLevelOriginalOffset[%d][4] = 0x%x\n",
			count,
			rtlphy->mcs_txpwrlevel_origoffset[count][4]);
	}
	if (regaddr == RTXAGC_A_MCS15_MCS12) {
		rtlphy->mcs_txpwrlevel_origoffset[count][5] = data;
		if (get_rf_type(rtlphy) == RF_1T1R) {
			count++;
			rtlphy->pwrgroup_cnt = count;
		}
		rtl_dbg(rtlpriv, COMP_INIT, DBG_TRACE,
			"MCSTxPowerLevelOriginalOffset[%d][5] = 0x%x\n",
			count,
			rtlphy->mcs_txpwrlevel_origoffset[count][5]);
	}
	if (regaddr == RTXAGC_B_RATE18_06) {
		rtlphy->mcs_txpwrlevel_origoffset[count][8] = data;
		rtl_dbg(rtlpriv, COMP_INIT, DBG_TRACE,
			"MCSTxPowerLevelOriginalOffset[%d][8] = 0x%x\n",
			count,
			rtlphy->mcs_txpwrlevel_origoffset[count][8]);
	}
	if (regaddr == RTXAGC_B_RATE54_24) {
		rtlphy->mcs_txpwrlevel_origoffset[count][9] = data;
		rtl_dbg(rtlpriv, COMP_INIT, DBG_TRACE,
			"MCSTxPowerLevelOriginalOffset[%d][9] = 0x%x\n",
			count,
			rtlphy->mcs_txpwrlevel_origoffset[count][9]);
	}
	if (regaddr == RTXAGC_B_CCK1_55_MCS32) {
		rtlphy->mcs_txpwrlevel_origoffset[count][14] = data;
		rtl_dbg(rtlpriv, COMP_INIT, DBG_TRACE,
			"MCSTxPowerLevelOriginalOffset[%d][14] = 0x%x\n",
			count,
			rtlphy->mcs_txpwrlevel_origoffset[count][14]);
	}
	if (regaddr == RTXAGC_B_CCK11_A_CCK2_11 && bitmask == 0x000000ff) {
		rtlphy->mcs_txpwrlevel_origoffset[count][15] = data;
		rtl_dbg(rtlpriv, COMP_INIT, DBG_TRACE,
			"MCSTxPowerLevelOriginalOffset[%d][15] = 0x%x\n",
			count,
			rtlphy->mcs_txpwrlevel_origoffset[count][15]);
	}
	if (regaddr == RTXAGC_B_MCS03_MCS00) {
		rtlphy->mcs_txpwrlevel_origoffset[count][10] = data;
		rtl_dbg(rtlpriv, COMP_INIT, DBG_TRACE,
			"MCSTxPowerLevelOriginalOffset[%d][10] = 0x%x\n",
			count,
			rtlphy->mcs_txpwrlevel_origoffset[count][10]);
	}
	if (regaddr == RTXAGC_B_MCS07_MCS04) {
		rtlphy->mcs_txpwrlevel_origoffset[count][11] = data;
		rtl_dbg(rtlpriv, COMP_INIT, DBG_TRACE,
			"MCSTxPowerLevelOriginalOffset[%d][11] = 0x%x\n",
			count,
			rtlphy->mcs_txpwrlevel_origoffset[count][11]);
	}
	if (regaddr == RTXAGC_B_MCS11_MCS08) {
		rtlphy->mcs_txpwrlevel_origoffset[count][12] = data;
		rtl_dbg(rtlpriv, COMP_INIT, DBG_TRACE,
			"MCSTxPowerLevelOriginalOffset[%d][12] = 0x%x\n",
			count,
			rtlphy->mcs_txpwrlevel_origoffset[count][12]);
	}
	if (regaddr == RTXAGC_B_MCS15_MCS12) {
		rtlphy->mcs_txpwrlevel_origoffset[count][13] = data;
		rtl_dbg(rtlpriv, COMP_INIT, DBG_TRACE,
			"MCSTxPowerLevelOriginalOffset[%d][13] = 0x%x\n",
			count,
			rtlphy->mcs_txpwrlevel_origoffset[count][13]);
		if (get_rf_type(rtlphy) != RF_1T1R) {
			count++;
			rtlphy->pwrgroup_cnt = count;
		}
	}
}

static bool phy_config_bb_with_pghdr(struct ieee80211_hw *hw, u8 configtype)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	int i;
	u32 *phy_reg_page;
	u16 phy_reg_page_len;
	u32 v1 = 0, v2 = 0;

	phy_reg_page_len = RTL8188EEPHY_REG_ARRAY_PGLEN;
	phy_reg_page = RTL8188EEPHY_REG_ARRAY_PG;

	if (configtype == BASEBAND_CONFIG_PHY_REG) {
		for (i = 0; i < phy_reg_page_len; i = i + 3) {
			v1 = phy_reg_page[i];
			v2 = phy_reg_page[i+1];

			if (v1 < 0xcdcdcdcd) {
				if (phy_reg_page[i] == 0xfe)
					mdelay(50);
				else if (phy_reg_page[i] == 0xfd)
					mdelay(5);
				else if (phy_reg_page[i] == 0xfc)
					mdelay(1);
				else if (phy_reg_page[i] == 0xfb)
					udelay(50);
				else if (phy_reg_page[i] == 0xfa)
					udelay(5);
				else if (phy_reg_page[i] == 0xf9)
					udelay(1);

				store_pwrindex_rate_offset(hw, phy_reg_page[i],
							   phy_reg_page[i + 1],
							   phy_reg_page[i + 2]);
				continue;
			} else {
				if (!_rtl88e_check_condition(hw,
							     phy_reg_page[i])) {
					/*don't need the hw_body*/
				    i += 2; /* skip the pair of expression*/
				    /* to protect 'i+1' 'i+2' not overrun */
				    if (i >= phy_reg_page_len - 2)
					break;

				    v1 = phy_reg_page[i];
				    v2 = phy_reg_page[i+1];
				    while (v2 != 0xDEAD &&
					   i < phy_reg_page_len - 5) {
					i += 3;
					v1 = phy_reg_page[i];
					v2 = phy_reg_page[i+1];
				    }
				}
			}
		}
	} else {
		rtl_dbg(rtlpriv, COMP_SEND, DBG_TRACE,
			"configtype != BaseBand_Config_PHY_REG\n");
	}
	return true;
}

#define READ_NEXT_RF_PAIR(v1, v2, i) \
do { \
	i += 2; \
	v1 = radioa_array_table[i]; \
	v2 = radioa_array_table[i+1]; \
} while (0)

static void process_path_a(struct ieee80211_hw *hw,
			   u16  radioa_arraylen,
			   u32 *radioa_array_table)
{
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u32 v1, v2;
	int i;

	for (i = 0; i < radioa_arraylen; i = i + 2) {
		v1 = radioa_array_table[i];
		v2 = radioa_array_table[i+1];
		if (v1 < 0xcdcdcdcd) {
			_rtl8188e_config_rf_radio_a(hw, v1, v2);
		} else { /*This line is the start line of branch.*/
			/* to protect READ_NEXT_PAIR not overrun */
			if (i >= radioa_arraylen - 2)
				break;

			if (!_rtl88e_check_condition(hw, radioa_array_table[i])) {
				/*Discard the following (offset, data) pairs*/
				READ_NEXT_RF_PAIR(v1, v2, i);
				while (v2 != 0xDEAD &&
				       v2 != 0xCDEF &&
				       v2 != 0xCDCD &&
				       i < radioa_arraylen - 2) {
					READ_NEXT_RF_PAIR(v1, v2, i);
				}
				i -= 2; /* prevent from for-loop += 2*/
			} else { /* Configure matched pairs and
				  * skip to end of if-else.
				  */
				READ_NEXT_RF_PAIR(v1, v2, i);
				while (v2 != 0xDEAD &&
				       v2 != 0xCDEF &&
				       v2 != 0xCDCD &&
				       i < radioa_arraylen - 2) {
					_rtl8188e_config_rf_radio_a(hw, v1, v2);
					READ_NEXT_RF_PAIR(v1, v2, i);
				}

				while (v2 != 0xDEAD &&
				       i < radioa_arraylen - 2)
					READ_NEXT_RF_PAIR(v1, v2, i);
			}
		}
	}

	if (rtlhal->oem_id == RT_CID_819X_HP)
		_rtl8188e_config_rf_radio_a(hw, 0x52, 0x7E4BD);
}

bool rtl88e_phy_config_rf_with_headerfile(struct ieee80211_hw *hw,
					  enum radio_path rfpath)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 *radioa_array_table;
	u16 radioa_arraylen;

	radioa_arraylen = RTL8188EE_RADIOA_1TARRAYLEN;
	radioa_array_table = RTL8188EE_RADIOA_1TARRAY;
	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
		"Radio_A:RTL8188EE_RADIOA_1TARRAY %d\n", radioa_arraylen);
	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD, "Radio No %x\n", rfpath);
	switch (rfpath) {
	case RF90_PATH_A:
		process_path_a(hw, radioa_arraylen, radioa_array_table);
		break;
	case RF90_PATH_B:
	case RF90_PATH_C:
	case RF90_PATH_D:
		break;
	}
	return true;
}

void rtl88e_phy_get_hw_reg_originalvalue(struct ieee80211_hw *hw)
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

	rtl_dbg(rtlpriv, COMP_INIT, DBG_TRACE,
		"Default initial gain (c50=0x%x, c58=0x%x, c60=0x%x, c68=0x%x\n",
		rtlphy->default_initialgain[0],
		rtlphy->default_initialgain[1],
		rtlphy->default_initialgain[2],
		rtlphy->default_initialgain[3]);

	rtlphy->framesync = (u8)rtl_get_bbreg(hw, ROFDM0_RXDETECTOR3,
					      MASKBYTE0);
	rtlphy->framesync_c34 = rtl_get_bbreg(hw, ROFDM0_RXDETECTOR2,
					      MASKDWORD);

	rtl_dbg(rtlpriv, COMP_INIT, DBG_TRACE,
		"Default framesync (0x%x) = 0x%x\n",
		ROFDM0_RXDETECTOR3, rtlphy->framesync);
}

static void _rtl88e_phy_init_bb_rf_register_definition(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;

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

	rtlphy->phyreg_def[RF90_PATH_A].rflssi_select = RFPGA0_XAB_RFPARAMETER;
	rtlphy->phyreg_def[RF90_PATH_B].rflssi_select = RFPGA0_XAB_RFPARAMETER;
	rtlphy->phyreg_def[RF90_PATH_C].rflssi_select = RFPGA0_XCD_RFPARAMETER;
	rtlphy->phyreg_def[RF90_PATH_D].rflssi_select = RFPGA0_XCD_RFPARAMETER;

	rtlphy->phyreg_def[RF90_PATH_A].rftxgain_stage = RFPGA0_TXGAINSTAGE;
	rtlphy->phyreg_def[RF90_PATH_B].rftxgain_stage = RFPGA0_TXGAINSTAGE;
	rtlphy->phyreg_def[RF90_PATH_C].rftxgain_stage = RFPGA0_TXGAINSTAGE;
	rtlphy->phyreg_def[RF90_PATH_D].rftxgain_stage = RFPGA0_TXGAINSTAGE;

	rtlphy->phyreg_def[RF90_PATH_A].rfhssi_para1 = RFPGA0_XA_HSSIPARAMETER1;
	rtlphy->phyreg_def[RF90_PATH_B].rfhssi_para1 = RFPGA0_XB_HSSIPARAMETER1;

	rtlphy->phyreg_def[RF90_PATH_A].rfhssi_para2 = RFPGA0_XA_HSSIPARAMETER2;
	rtlphy->phyreg_def[RF90_PATH_B].rfhssi_para2 = RFPGA0_XB_HSSIPARAMETER2;

	rtlphy->phyreg_def[RF90_PATH_A].rfsw_ctrl =
	    RFPGA0_XAB_SWITCHCONTROL;
	rtlphy->phyreg_def[RF90_PATH_B].rfsw_ctrl =
	    RFPGA0_XAB_SWITCHCONTROL;
	rtlphy->phyreg_def[RF90_PATH_C].rfsw_ctrl =
	    RFPGA0_XCD_SWITCHCONTROL;
	rtlphy->phyreg_def[RF90_PATH_D].rfsw_ctrl =
	    RFPGA0_XCD_SWITCHCONTROL;

	rtlphy->phyreg_def[RF90_PATH_A].rfagc_control1 = ROFDM0_XAAGCCORE1;
	rtlphy->phyreg_def[RF90_PATH_B].rfagc_control1 = ROFDM0_XBAGCCORE1;
	rtlphy->phyreg_def[RF90_PATH_C].rfagc_control1 = ROFDM0_XCAGCCORE1;
	rtlphy->phyreg_def[RF90_PATH_D].rfagc_control1 = ROFDM0_XDAGCCORE1;

	rtlphy->phyreg_def[RF90_PATH_A].rfagc_control2 = ROFDM0_XAAGCCORE2;
	rtlphy->phyreg_def[RF90_PATH_B].rfagc_control2 = ROFDM0_XBAGCCORE2;
	rtlphy->phyreg_def[RF90_PATH_C].rfagc_control2 = ROFDM0_XCAGCCORE2;
	rtlphy->phyreg_def[RF90_PATH_D].rfagc_control2 = ROFDM0_XDAGCCORE2;

	rtlphy->phyreg_def[RF90_PATH_A].rfrxiq_imbal = ROFDM0_XARXIQIMBALANCE;
	rtlphy->phyreg_def[RF90_PATH_B].rfrxiq_imbal = ROFDM0_XBRXIQIMBALANCE;
	rtlphy->phyreg_def[RF90_PATH_C].rfrxiq_imbal = ROFDM0_XCRXIQIMBANLANCE;
	rtlphy->phyreg_def[RF90_PATH_D].rfrxiq_imbal = ROFDM0_XDRXIQIMBALANCE;

	rtlphy->phyreg_def[RF90_PATH_A].rfrx_afe = ROFDM0_XARXAFE;
	rtlphy->phyreg_def[RF90_PATH_B].rfrx_afe = ROFDM0_XBRXAFE;
	rtlphy->phyreg_def[RF90_PATH_C].rfrx_afe = ROFDM0_XCRXAFE;
	rtlphy->phyreg_def[RF90_PATH_D].rfrx_afe = ROFDM0_XDRXAFE;

	rtlphy->phyreg_def[RF90_PATH_A].rftxiq_imbal = ROFDM0_XATXIQIMBALANCE;
	rtlphy->phyreg_def[RF90_PATH_B].rftxiq_imbal = ROFDM0_XBTXIQIMBALANCE;
	rtlphy->phyreg_def[RF90_PATH_C].rftxiq_imbal = ROFDM0_XCTXIQIMBALANCE;
	rtlphy->phyreg_def[RF90_PATH_D].rftxiq_imbal = ROFDM0_XDTXIQIMBALANCE;

	rtlphy->phyreg_def[RF90_PATH_A].rftx_afe = ROFDM0_XATXAFE;
	rtlphy->phyreg_def[RF90_PATH_B].rftx_afe = ROFDM0_XBTXAFE;

	rtlphy->phyreg_def[RF90_PATH_A].rf_rb = RFPGA0_XA_LSSIREADBACK;
	rtlphy->phyreg_def[RF90_PATH_B].rf_rb = RFPGA0_XB_LSSIREADBACK;

	rtlphy->phyreg_def[RF90_PATH_A].rf_rbpi = TRANSCEIVEA_HSPI_READBACK;
	rtlphy->phyreg_def[RF90_PATH_B].rf_rbpi = TRANSCEIVEB_HSPI_READBACK;
}

void rtl88e_phy_get_txpower_level(struct ieee80211_hw *hw, long *powerlevel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 txpwr_level;
	long txpwr_dbm;

	txpwr_level = rtlphy->cur_cck_txpwridx;
	txpwr_dbm = _rtl88e_phy_txpwr_idx_to_dbm(hw,
						 WIRELESS_MODE_B, txpwr_level);
	txpwr_level = rtlphy->cur_ofdm24g_txpwridx;
	if (_rtl88e_phy_txpwr_idx_to_dbm(hw,
					 WIRELESS_MODE_G,
					 txpwr_level) > txpwr_dbm)
		txpwr_dbm =
		    _rtl88e_phy_txpwr_idx_to_dbm(hw, WIRELESS_MODE_G,
						 txpwr_level);
	txpwr_level = rtlphy->cur_ofdm24g_txpwridx;
	if (_rtl88e_phy_txpwr_idx_to_dbm(hw,
					 WIRELESS_MODE_N_24G,
					 txpwr_level) > txpwr_dbm)
		txpwr_dbm =
		    _rtl88e_phy_txpwr_idx_to_dbm(hw, WIRELESS_MODE_N_24G,
						 txpwr_level);
	*powerlevel = txpwr_dbm;
}

static void handle_path_a(struct rtl_efuse *rtlefuse, u8 index,
			  u8 *cckpowerlevel, u8 *ofdmpowerlevel,
			  u8 *bw20powerlevel, u8 *bw40powerlevel)
{
	cckpowerlevel[RF90_PATH_A] =
	    rtlefuse->txpwrlevel_cck[RF90_PATH_A][index];
		/*-8~7 */
	if (rtlefuse->txpwr_ht20diff[RF90_PATH_A][index] > 0x0f)
		bw20powerlevel[RF90_PATH_A] =
		  rtlefuse->txpwrlevel_ht40_1s[RF90_PATH_A][index] -
		  (~(rtlefuse->txpwr_ht20diff[RF90_PATH_A][index]) + 1);
	else
		bw20powerlevel[RF90_PATH_A] =
		  rtlefuse->txpwrlevel_ht40_1s[RF90_PATH_A][index] +
		  rtlefuse->txpwr_ht20diff[RF90_PATH_A][index];
	if (rtlefuse->txpwr_legacyhtdiff[RF90_PATH_A][index] > 0xf)
		ofdmpowerlevel[RF90_PATH_A] =
		  rtlefuse->txpwrlevel_ht40_1s[RF90_PATH_A][index] -
		  (~(rtlefuse->txpwr_legacyhtdiff[RF90_PATH_A][index])+1);
	else
		ofdmpowerlevel[RF90_PATH_A] =
		rtlefuse->txpwrlevel_ht40_1s[RF90_PATH_A][index] +
		  rtlefuse->txpwr_legacyhtdiff[RF90_PATH_A][index];
	bw40powerlevel[RF90_PATH_A] =
	  rtlefuse->txpwrlevel_ht40_1s[RF90_PATH_A][index];
}

static void _rtl88e_get_txpower_index(struct ieee80211_hw *hw, u8 channel,
				      u8 *cckpowerlevel, u8 *ofdmpowerlevel,
				      u8 *bw20powerlevel, u8 *bw40powerlevel)
{
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u8 index = (channel - 1);
	u8 rf_path = 0;

	for (rf_path = 0; rf_path < 2; rf_path++) {
		if (rf_path == RF90_PATH_A) {
			handle_path_a(rtlefuse, index, cckpowerlevel,
				      ofdmpowerlevel, bw20powerlevel,
				      bw40powerlevel);
		} else if (rf_path == RF90_PATH_B) {
			cckpowerlevel[RF90_PATH_B] =
			  rtlefuse->txpwrlevel_cck[RF90_PATH_B][index];
			bw20powerlevel[RF90_PATH_B] =
			  rtlefuse->txpwrlevel_ht40_1s[RF90_PATH_B][index] +
			  rtlefuse->txpwr_ht20diff[RF90_PATH_B][index];
			ofdmpowerlevel[RF90_PATH_B] =
			  rtlefuse->txpwrlevel_ht40_1s[RF90_PATH_B][index] +
			  rtlefuse->txpwr_legacyhtdiff[RF90_PATH_B][index];
			bw40powerlevel[RF90_PATH_B] =
			  rtlefuse->txpwrlevel_ht40_1s[RF90_PATH_B][index];
		}
	}

}

static void _rtl88e_ccxpower_index_check(struct ieee80211_hw *hw,
					 u8 channel, u8 *cckpowerlevel,
					 u8 *ofdmpowerlevel, u8 *bw20powerlevel,
					 u8 *bw40powerlevel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;

	rtlphy->cur_cck_txpwridx = cckpowerlevel[0];
	rtlphy->cur_ofdm24g_txpwridx = ofdmpowerlevel[0];
	rtlphy->cur_bw20_txpwridx = bw20powerlevel[0];
	rtlphy->cur_bw40_txpwridx = bw40powerlevel[0];

}

void rtl88e_phy_set_txpower_level(struct ieee80211_hw *hw, u8 channel)
{
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u8 cckpowerlevel[MAX_TX_COUNT]  = {0};
	u8 ofdmpowerlevel[MAX_TX_COUNT] = {0};
	u8 bw20powerlevel[MAX_TX_COUNT] = {0};
	u8 bw40powerlevel[MAX_TX_COUNT] = {0};

	if (!rtlefuse->txpwr_fromeprom)
		return;
	_rtl88e_get_txpower_index(hw, channel,
				  &cckpowerlevel[0], &ofdmpowerlevel[0],
				  &bw20powerlevel[0], &bw40powerlevel[0]);
	_rtl88e_ccxpower_index_check(hw, channel,
				     &cckpowerlevel[0], &ofdmpowerlevel[0],
				     &bw20powerlevel[0], &bw40powerlevel[0]);
	rtl88e_phy_rf6052_set_cck_txpower(hw, &cckpowerlevel[0]);
	rtl88e_phy_rf6052_set_ofdm_txpower(hw, &ofdmpowerlevel[0],
					   &bw20powerlevel[0],
					   &bw40powerlevel[0], channel);
}

static long _rtl88e_phy_txpwr_idx_to_dbm(struct ieee80211_hw *hw,
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

void rtl88e_phy_scan_operation_backup(struct ieee80211_hw *hw, u8 operation)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	enum io_type iotype;

	if (!is_hal_stop(rtlhal)) {
		switch (operation) {
		case SCAN_OPT_BACKUP_BAND0:
			iotype = IO_CMD_PAUSE_BAND0_DM_BY_SCAN;
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
			pr_err("Unknown Scan Backup operation.\n");
			break;
		}
	}
}

void rtl88e_phy_set_bw_mode_callback(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u8 reg_bw_opmode;
	u8 reg_prsr_rsc;

	rtl_dbg(rtlpriv, COMP_SCAN, DBG_TRACE,
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
		reg_prsr_rsc =
		    (reg_prsr_rsc & 0x90) | (mac->cur_40_prime_sc << 5);
		rtl_write_byte(rtlpriv, REG_RRSR + 2, reg_prsr_rsc);
		break;
	default:
		pr_err("unknown bandwidth: %#X\n",
		       rtlphy->current_chan_bw);
		break;
	}

	switch (rtlphy->current_chan_bw) {
	case HT_CHANNEL_WIDTH_20:
		rtl_set_bbreg(hw, RFPGA0_RFMOD, BRFMOD, 0x0);
		rtl_set_bbreg(hw, RFPGA1_RFMOD, BRFMOD, 0x0);
	/*	rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER2, BIT(10), 1);*/
		break;
	case HT_CHANNEL_WIDTH_20_40:
		rtl_set_bbreg(hw, RFPGA0_RFMOD, BRFMOD, 0x1);
		rtl_set_bbreg(hw, RFPGA1_RFMOD, BRFMOD, 0x1);

		rtl_set_bbreg(hw, RCCK0_SYSTEM, BCCK_SIDEBAND,
			      (mac->cur_40_prime_sc >> 1));
		rtl_set_bbreg(hw, ROFDM1_LSTF, 0xC00, mac->cur_40_prime_sc);
		/*rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER2, BIT(10), 0);*/

		rtl_set_bbreg(hw, 0x818, (BIT(26) | BIT(27)),
			      (mac->cur_40_prime_sc ==
			       HAL_PRIME_CHNL_OFFSET_LOWER) ? 2 : 1);
		break;
	default:
		pr_err("unknown bandwidth: %#X\n",
		       rtlphy->current_chan_bw);
		break;
	}
	rtl88e_phy_rf6052_set_bandwidth(hw, rtlphy->current_chan_bw);
	rtlphy->set_bwmode_inprogress = false;
	rtl_dbg(rtlpriv, COMP_SCAN, DBG_LOUD, "\n");
}

void rtl88e_phy_set_bw_mode(struct ieee80211_hw *hw,
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
		rtl88e_phy_set_bw_mode_callback(hw);
	} else {
		rtl_dbg(rtlpriv, COMP_ERR, DBG_WARNING,
			"false driver sleep or unload\n");
		rtlphy->set_bwmode_inprogress = false;
		rtlphy->current_chan_bw = tmp_bw;
	}
}

void rtl88e_phy_sw_chnl_callback(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u32 delay;

	rtl_dbg(rtlpriv, COMP_SCAN, DBG_TRACE,
		"switch to channel%d\n", rtlphy->current_channel);
	if (is_hal_stop(rtlhal))
		return;
	do {
		if (!rtlphy->sw_chnl_inprogress)
			break;
		if (!_rtl88e_phy_sw_chnl_step_by_step
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
	rtl_dbg(rtlpriv, COMP_SCAN, DBG_TRACE, "\n");
}

u8 rtl88e_phy_sw_chnl(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	if (rtlphy->sw_chnl_inprogress)
		return 0;
	if (rtlphy->set_bwmode_inprogress)
		return 0;
	WARN_ONCE((rtlphy->current_channel > 14),
		  "rtl8188ee: WIRELESS_MODE_G but channel>14");
	rtlphy->sw_chnl_inprogress = true;
	rtlphy->sw_chnl_stage = 0;
	rtlphy->sw_chnl_step = 0;
	if (!(is_hal_stop(rtlhal)) && !(RT_CANNOT_IO(hw))) {
		rtl88e_phy_sw_chnl_callback(hw);
		rtl_dbg(rtlpriv, COMP_CHAN, DBG_LOUD,
			"sw_chnl_inprogress false schedule workitem current channel %d\n",
			rtlphy->current_channel);
		rtlphy->sw_chnl_inprogress = false;
	} else {
		rtl_dbg(rtlpriv, COMP_CHAN, DBG_LOUD,
			"sw_chnl_inprogress false driver sleep or unload\n");
		rtlphy->sw_chnl_inprogress = false;
	}
	return 1;
}

static bool _rtl88e_phy_sw_chnl_step_by_step(struct ieee80211_hw *hw,
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
	_rtl88e_phy_set_sw_chnl_cmdarray(precommoncmd, precommoncmdcnt++,
					 MAX_PRECMD_CNT,
					 CMDID_SET_TXPOWEROWER_LEVEL, 0, 0, 0);
	_rtl88e_phy_set_sw_chnl_cmdarray(precommoncmd, precommoncmdcnt++,
					 MAX_PRECMD_CNT, CMDID_END, 0, 0, 0);

	postcommoncmdcnt = 0;

	_rtl88e_phy_set_sw_chnl_cmdarray(postcommoncmd, postcommoncmdcnt++,
					 MAX_POSTCMD_CNT, CMDID_END, 0, 0, 0);

	rfdependcmdcnt = 0;

	WARN_ONCE((channel < 1 || channel > 14),
		  "rtl8188ee: illegal channel for Zebra: %d\n", channel);

	_rtl88e_phy_set_sw_chnl_cmdarray(rfdependcmd, rfdependcmdcnt++,
					 MAX_RFDEPENDCMD_CNT, CMDID_RF_WRITEREG,
					 RF_CHNLBW, channel, 10);

	_rtl88e_phy_set_sw_chnl_cmdarray(rfdependcmd, rfdependcmdcnt++,
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
		default:
			pr_err("Invalid 'stage' = %d, Check it!\n",
			       *stage);
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
			rtl88e_phy_set_txpower_level(hw, channel);
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
				      0xfffffc00) | currentcmd->para2);

				rtl_set_rfreg(hw, (enum radio_path)rfpath,
					      currentcmd->para1,
					      RFREG_OFFSET_MASK,
					      rtlphy->rfreg_chnlval[rfpath]);
			}
			break;
		default:
			rtl_dbg(rtlpriv, COMP_ERR, DBG_LOUD,
				"switch case %#x not processed\n",
				currentcmd->cmdid);
			break;
		}

		break;
	} while (true);

	(*delay) = currentcmd->msdelay;
	(*step)++;
	return false;
}

static bool _rtl88e_phy_set_sw_chnl_cmdarray(struct swchnlcmd *cmdtable,
					     u32 cmdtableidx, u32 cmdtablesz,
					     enum swchnlcmd_id cmdid,
					     u32 para1, u32 para2, u32 msdelay)
{
	struct swchnlcmd *pcmd;

	if (cmdtable == NULL) {
		WARN_ONCE(true, "rtl8188ee: cmdtable cannot be NULL.\n");
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

static u8 _rtl88e_phy_path_a_iqk(struct ieee80211_hw *hw, bool config_pathb)
{
	u32 reg_eac, reg_e94, reg_e9c;
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
	rtl_get_bbreg(hw, 0xea4, MASKDWORD);

	if (!(reg_eac & BIT(28)) &&
	    (((reg_e94 & 0x03FF0000) >> 16) != 0x142) &&
	    (((reg_e9c & 0x03FF0000) >> 16) != 0x42))
		result |= 0x01;
	return result;
}

static u8 _rtl88e_phy_path_b_iqk(struct ieee80211_hw *hw)
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

static u8 _rtl88e_phy_path_a_rx_iqk(struct ieee80211_hw *hw, bool config_pathb)
{
	u32 reg_eac, reg_e94, reg_e9c, reg_ea4, u32temp;
	u8 result = 0x00;

	/*Get TXIMR Setting*/
	/*Modify RX IQK mode table*/
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x00000000);
	rtl_set_rfreg(hw, RF90_PATH_A, RF_WE_LUT, RFREG_OFFSET_MASK, 0x800a0);
	rtl_set_rfreg(hw, RF90_PATH_A, RF_RCK_OS, RFREG_OFFSET_MASK, 0x30000);
	rtl_set_rfreg(hw, RF90_PATH_A, RF_TXPA_G1, RFREG_OFFSET_MASK, 0x0000f);
	rtl_set_rfreg(hw, RF90_PATH_A, RF_TXPA_G2, RFREG_OFFSET_MASK, 0xf117b);
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x80800000);

	/*IQK Setting*/
	rtl_set_bbreg(hw, RTX_IQK, MASKDWORD, 0x01007c00);
	rtl_set_bbreg(hw, RRX_IQK, MASKDWORD, 0x81004800);

	/*path a IQK setting*/
	rtl_set_bbreg(hw, RTX_IQK_TONE_A, MASKDWORD, 0x10008c1c);
	rtl_set_bbreg(hw, RRX_IQK_TONE_A, MASKDWORD, 0x30008c1c);
	rtl_set_bbreg(hw, RTX_IQK_PI_A, MASKDWORD, 0x82160804);
	rtl_set_bbreg(hw, RRX_IQK_PI_A, MASKDWORD, 0x28160000);

	/*LO calibration Setting*/
	rtl_set_bbreg(hw, RIQK_AGC_RSP, MASKDWORD, 0x0046a911);
	/*one shot,path A LOK & iqk*/
	rtl_set_bbreg(hw, RIQK_AGC_PTS, MASKDWORD, 0xf9000000);
	rtl_set_bbreg(hw, RIQK_AGC_PTS, MASKDWORD, 0xf8000000);

	mdelay(IQK_DELAY_TIME);

	reg_eac = rtl_get_bbreg(hw, RRX_POWER_AFTER_IQK_A_2, MASKDWORD);
	reg_e94 = rtl_get_bbreg(hw, RTX_POWER_BEFORE_IQK_A, MASKDWORD);
	reg_e9c = rtl_get_bbreg(hw, RTX_POWER_AFTER_IQK_A, MASKDWORD);


	if (!(reg_eac & BIT(28)) &&
	    (((reg_e94 & 0x03FF0000) >> 16) != 0x142) &&
	    (((reg_e9c & 0x03FF0000) >> 16) != 0x42))
		result |= 0x01;
	else
		return result;

	u32temp = 0x80007C00 | (reg_e94&0x3FF0000) |
		  ((reg_e9c&0x3FF0000) >> 16);
	rtl_set_bbreg(hw, RTX_IQK, MASKDWORD, u32temp);
	/*RX IQK*/
	/*Modify RX IQK mode table*/
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x00000000);
	rtl_set_rfreg(hw, RF90_PATH_A, RF_WE_LUT, RFREG_OFFSET_MASK, 0x800a0);
	rtl_set_rfreg(hw, RF90_PATH_A, RF_RCK_OS, RFREG_OFFSET_MASK, 0x30000);
	rtl_set_rfreg(hw, RF90_PATH_A, RF_TXPA_G1, RFREG_OFFSET_MASK, 0x0000f);
	rtl_set_rfreg(hw, RF90_PATH_A, RF_TXPA_G2, RFREG_OFFSET_MASK, 0xf7ffa);
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKDWORD, 0x80800000);

	/*IQK Setting*/
	rtl_set_bbreg(hw, RRX_IQK, MASKDWORD, 0x01004800);

	/*path a IQK setting*/
	rtl_set_bbreg(hw, RTX_IQK_TONE_A, MASKDWORD, 0x30008c1c);
	rtl_set_bbreg(hw, RRX_IQK_TONE_A, MASKDWORD, 0x10008c1c);
	rtl_set_bbreg(hw, RTX_IQK_PI_A, MASKDWORD, 0x82160c05);
	rtl_set_bbreg(hw, RRX_IQK_PI_A, MASKDWORD, 0x28160c05);

	/*LO calibration Setting*/
	rtl_set_bbreg(hw, RIQK_AGC_RSP, MASKDWORD, 0x0046a911);
	/*one shot,path A LOK & iqk*/
	rtl_set_bbreg(hw, RIQK_AGC_PTS, MASKDWORD, 0xf9000000);
	rtl_set_bbreg(hw, RIQK_AGC_PTS, MASKDWORD, 0xf8000000);

	mdelay(IQK_DELAY_TIME);

	reg_eac = rtl_get_bbreg(hw, RRX_POWER_AFTER_IQK_A_2, MASKDWORD);
	reg_e94 = rtl_get_bbreg(hw, RTX_POWER_BEFORE_IQK_A, MASKDWORD);
	reg_e9c = rtl_get_bbreg(hw, RTX_POWER_AFTER_IQK_A, MASKDWORD);
	reg_ea4 = rtl_get_bbreg(hw, RRX_POWER_BEFORE_IQK_A_2, MASKDWORD);

	if (!(reg_eac & BIT(27)) &&
	    (((reg_ea4 & 0x03FF0000) >> 16) != 0x132) &&
	    (((reg_eac & 0x03FF0000) >> 16) != 0x36))
		result |= 0x02;
	return result;
}

static void _rtl88e_phy_path_a_fill_iqk_matrix(struct ieee80211_hw *hw,
					       bool iqk_ok, long result[][8],
					       u8 final_candidate, bool btxonly)
{
	u32 oldval_0, x, tx0_a, reg;
	long y, tx0_c;

	if (final_candidate == 0xFF) {
		return;
	} else if (iqk_ok) {
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

static void _rtl88e_phy_save_adda_registers(struct ieee80211_hw *hw,
					    u32 *addareg, u32 *addabackup,
					    u32 registernum)
{
	u32 i;

	for (i = 0; i < registernum; i++)
		addabackup[i] = rtl_get_bbreg(hw, addareg[i], MASKDWORD);
}

static void _rtl88e_phy_save_mac_registers(struct ieee80211_hw *hw,
					   u32 *macreg, u32 *macbackup)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 i;

	for (i = 0; i < (IQK_MAC_REG_NUM - 1); i++)
		macbackup[i] = rtl_read_byte(rtlpriv, macreg[i]);
	macbackup[i] = rtl_read_dword(rtlpriv, macreg[i]);
}

static void _rtl88e_phy_reload_adda_registers(struct ieee80211_hw *hw,
					      u32 *addareg, u32 *addabackup,
					      u32 regiesternum)
{
	u32 i;

	for (i = 0; i < regiesternum; i++)
		rtl_set_bbreg(hw, addareg[i], MASKDWORD, addabackup[i]);
}

static void _rtl88e_phy_reload_mac_registers(struct ieee80211_hw *hw,
					     u32 *macreg, u32 *macbackup)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 i;

	for (i = 0; i < (IQK_MAC_REG_NUM - 1); i++)
		rtl_write_byte(rtlpriv, macreg[i], (u8) macbackup[i]);
	rtl_write_dword(rtlpriv, macreg[i], macbackup[i]);
}

static void _rtl88e_phy_path_adda_on(struct ieee80211_hw *hw,
				     u32 *addareg, bool is_patha_on, bool is2t)
{
	u32 pathon;
	u32 i;

	pathon = is_patha_on ? 0x04db25a4 : 0x0b1b25a4;
	if (!is2t) {
		pathon = 0x0bdb25a0;
		rtl_set_bbreg(hw, addareg[0], MASKDWORD, 0x0b1b25a0);
	} else {
		rtl_set_bbreg(hw, addareg[0], MASKDWORD, pathon);
	}

	for (i = 1; i < IQK_ADDA_REG_NUM; i++)
		rtl_set_bbreg(hw, addareg[i], MASKDWORD, pathon);
}

static void _rtl88e_phy_mac_setting_calibration(struct ieee80211_hw *hw,
						u32 *macreg, u32 *macbackup)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 i = 0;

	rtl_write_byte(rtlpriv, macreg[i], 0x3F);

	for (i = 1; i < (IQK_MAC_REG_NUM - 1); i++)
		rtl_write_byte(rtlpriv, macreg[i],
			       (u8) (macbackup[i] & (~BIT(3))));
	rtl_write_byte(rtlpriv, macreg[i], (u8) (macbackup[i] & (~BIT(5))));
}

static void _rtl88e_phy_path_a_standby(struct ieee80211_hw *hw)
{
	rtl_set_bbreg(hw, 0xe28, MASKDWORD, 0x0);
	rtl_set_bbreg(hw, 0x840, MASKDWORD, 0x00010000);
	rtl_set_bbreg(hw, 0xe28, MASKDWORD, 0x80800000);
}

static void _rtl88e_phy_pi_mode_switch(struct ieee80211_hw *hw, bool pi_mode)
{
	u32 mode;

	mode = pi_mode ? 0x01000100 : 0x01000000;
	rtl_set_bbreg(hw, 0x820, MASKDWORD, mode);
	rtl_set_bbreg(hw, 0x828, MASKDWORD, mode);
}

static bool _rtl88e_phy_simularity_compare(struct ieee80211_hw *hw,
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

static void _rtl88e_phy_iq_calibrate(struct ieee80211_hw *hw,
				     long result[][8], u8 t, bool is2t)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
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
	u32 iqk_bb_reg[IQK_BB_REG_NUM] = {
		ROFDM0_TRXPATHENABLE, ROFDM0_TRMUXPAR,
		RFPGA0_XCD_RFINTERFACESW, 0xb68, 0xb6c,
		0x870, 0x860, 0x864, 0x800
	};
	const u32 retrycount = 2;

	if (t == 0) {
		_rtl88e_phy_save_adda_registers(hw, adda_reg,
						rtlphy->adda_backup, 16);
		_rtl88e_phy_save_mac_registers(hw, iqk_mac_reg,
					       rtlphy->iqk_mac_backup);
		_rtl88e_phy_save_adda_registers(hw, iqk_bb_reg,
						rtlphy->iqk_bb_backup,
						IQK_BB_REG_NUM);
	}
	_rtl88e_phy_path_adda_on(hw, adda_reg, true, is2t);
	if (t == 0) {
		rtlphy->rfpi_enable =
		  (u8)rtl_get_bbreg(hw, RFPGA0_XA_HSSIPARAMETER1, BIT(8));
	}

	if (!rtlphy->rfpi_enable)
		_rtl88e_phy_pi_mode_switch(hw, true);
	/*BB Setting*/
	rtl_set_bbreg(hw, 0x800, BIT(24), 0x00);
	rtl_set_bbreg(hw, 0xc04, MASKDWORD, 0x03a05600);
	rtl_set_bbreg(hw, 0xc08, MASKDWORD, 0x000800e4);
	rtl_set_bbreg(hw, 0x874, MASKDWORD, 0x22204000);

	rtl_set_bbreg(hw, 0x870, BIT(10), 0x01);
	rtl_set_bbreg(hw, 0x870, BIT(26), 0x01);
	rtl_set_bbreg(hw, 0x860, BIT(10), 0x00);
	rtl_set_bbreg(hw, 0x864, BIT(10), 0x00);

	if (is2t) {
		rtl_set_bbreg(hw, 0x840, MASKDWORD, 0x00010000);
		rtl_set_bbreg(hw, 0x844, MASKDWORD, 0x00010000);
	}
	_rtl88e_phy_mac_setting_calibration(hw, iqk_mac_reg,
					    rtlphy->iqk_mac_backup);
	rtl_set_bbreg(hw, 0xb68, MASKDWORD, 0x0f600000);
	if (is2t)
		rtl_set_bbreg(hw, 0xb6c, MASKDWORD, 0x0f600000);

	rtl_set_bbreg(hw, 0xe28, MASKDWORD, 0x80800000);
	rtl_set_bbreg(hw, 0xe40, MASKDWORD, 0x01007c00);
	rtl_set_bbreg(hw, 0xe44, MASKDWORD, 0x81004800);
	for (i = 0; i < retrycount; i++) {
		patha_ok = _rtl88e_phy_path_a_iqk(hw, is2t);
		if (patha_ok == 0x01) {
			rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
				"Path A Tx IQK Success!!\n");
			result[t][0] = (rtl_get_bbreg(hw, 0xe94, MASKDWORD) &
					0x3FF0000) >> 16;
			result[t][1] = (rtl_get_bbreg(hw, 0xe9c, MASKDWORD) &
					0x3FF0000) >> 16;
			break;
		}
	}

	for (i = 0; i < retrycount; i++) {
		patha_ok = _rtl88e_phy_path_a_rx_iqk(hw, is2t);
		if (patha_ok == 0x03) {
			rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
				"Path A Rx IQK Success!!\n");
			result[t][2] = (rtl_get_bbreg(hw, 0xea4, MASKDWORD) &
					0x3FF0000) >> 16;
			result[t][3] = (rtl_get_bbreg(hw, 0xeac, MASKDWORD) &
					0x3FF0000) >> 16;
			break;
		} else {
			rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
				"Path a RX iqk fail!!!\n");
		}
	}

	if (0 == patha_ok)
		rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
			"Path A IQK Success!!\n");
	if (is2t) {
		_rtl88e_phy_path_a_standby(hw);
		_rtl88e_phy_path_adda_on(hw, adda_reg, false, is2t);
		for (i = 0; i < retrycount; i++) {
			pathb_ok = _rtl88e_phy_path_b_iqk(hw);
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

	rtl_set_bbreg(hw, 0xe28, MASKDWORD, 0);

	if (t != 0) {
		if (!rtlphy->rfpi_enable)
			_rtl88e_phy_pi_mode_switch(hw, false);
		_rtl88e_phy_reload_adda_registers(hw, adda_reg,
						  rtlphy->adda_backup, 16);
		_rtl88e_phy_reload_mac_registers(hw, iqk_mac_reg,
						 rtlphy->iqk_mac_backup);
		_rtl88e_phy_reload_adda_registers(hw, iqk_bb_reg,
						  rtlphy->iqk_bb_backup,
						  IQK_BB_REG_NUM);

		rtl_set_bbreg(hw, 0x840, MASKDWORD, 0x00032ed3);
		if (is2t)
			rtl_set_bbreg(hw, 0x844, MASKDWORD, 0x00032ed3);
		rtl_set_bbreg(hw, 0xe30, MASKDWORD, 0x01008c00);
		rtl_set_bbreg(hw, 0xe34, MASKDWORD, 0x01008c00);
	}
	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD, "88ee IQK Finish!!\n");
}

static void _rtl88e_phy_lc_calibrate(struct ieee80211_hw *hw, bool is2t)
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
	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD, "\n");
}

static void _rtl88e_phy_set_rfpath_switch(struct ieee80211_hw *hw,
					  bool bmain, bool is2t)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD, "\n");

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

void rtl88e_phy_iq_calibrate(struct ieee80211_hw *hw, bool b_recovery)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	long result[4][8];
	u8 i, final_candidate;
	bool b_patha_ok;
	long reg_e94, reg_e9c, reg_ea4, reg_eb4, reg_ebc,
	    reg_tmp = 0;
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

	if (b_recovery) {
		_rtl88e_phy_reload_adda_registers(hw,
						  iqk_bb_reg,
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
	b_patha_ok = false;
	is12simular = false;
	is23simular = false;
	is13simular = false;
	for (i = 0; i < 3; i++) {
		if (get_rf_type(rtlphy) == RF_2T2R)
			_rtl88e_phy_iq_calibrate(hw, result, i, true);
		else
			_rtl88e_phy_iq_calibrate(hw, result, i, false);
		if (i == 1) {
			is12simular =
			  _rtl88e_phy_simularity_compare(hw, result, 0, 1);
			if (is12simular) {
				final_candidate = 0;
				break;
			}
		}
		if (i == 2) {
			is13simular =
			  _rtl88e_phy_simularity_compare(hw, result, 0, 2);
			if (is13simular) {
				final_candidate = 0;
				break;
			}
			is23simular =
			   _rtl88e_phy_simularity_compare(hw, result, 1, 2);
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
		reg_eb4 = result[i][4];
		reg_ebc = result[i][5];
	}
	if (final_candidate != 0xff) {
		reg_e94 = result[final_candidate][0];
		reg_e9c = result[final_candidate][1];
		reg_ea4 = result[final_candidate][2];
		reg_eb4 = result[final_candidate][4];
		reg_ebc = result[final_candidate][5];
		rtlphy->reg_eb4 = reg_eb4;
		rtlphy->reg_ebc = reg_ebc;
		rtlphy->reg_e94 = reg_e94;
		rtlphy->reg_e9c = reg_e9c;
		b_patha_ok = true;
	} else {
		rtlphy->reg_e94 = 0x100;
		rtlphy->reg_eb4 = 0x100;
		rtlphy->reg_e9c = 0x0;
		rtlphy->reg_ebc = 0x0;
	}
	if (reg_e94 != 0) /*&&(reg_ea4 != 0) */
		_rtl88e_phy_path_a_fill_iqk_matrix(hw, b_patha_ok, result,
						   final_candidate,
						   (reg_ea4 == 0));
	if (final_candidate != 0xFF) {
		for (i = 0; i < IQK_MATRIX_REG_NUM; i++)
			rtlphy->iqk_matrix[0].value[0][i] =
				result[final_candidate][i];
		rtlphy->iqk_matrix[0].iqk_done = true;

	}
	_rtl88e_phy_save_adda_registers(hw, iqk_bb_reg,
					rtlphy->iqk_bb_backup, 9);
}

void rtl88e_phy_lc_calibrate(struct ieee80211_hw *hw)
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

	_rtl88e_phy_lc_calibrate(hw, false);

	rtlphy->lck_inprogress = false;
}

void rtl88e_phy_set_rfpath_switch(struct ieee80211_hw *hw, bool bmain)
{
	_rtl88e_phy_set_rfpath_switch(hw, bmain, false);
}

bool rtl88e_phy_set_io_cmd(struct ieee80211_hw *hw, enum io_type iotype)
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
				"[IO CMD] Resume DM after scan.\n");
			postprocessing = true;
			break;
		case IO_CMD_PAUSE_BAND0_DM_BY_SCAN:
			rtl_dbg(rtlpriv, COMP_CMD, DBG_TRACE,
				"[IO CMD] Pause DM before scan.\n");
			postprocessing = true;
			break;
		default:
			rtl_dbg(rtlpriv, COMP_ERR, DBG_LOUD,
				"switch case %#x not processed\n", iotype);
			break;
		}
	} while (false);
	if (postprocessing && !rtlphy->set_io_inprogress) {
		rtlphy->set_io_inprogress = true;
		rtlphy->current_io_type = iotype;
	} else {
		return false;
	}
	rtl88e_phy_set_io(hw);
	rtl_dbg(rtlpriv, COMP_CMD, DBG_TRACE, "IO Type(%#x)\n", iotype);
	return true;
}

static void rtl88e_phy_set_io(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct dig_t *dm_digtable = &rtlpriv->dm_digtable;

	rtl_dbg(rtlpriv, COMP_CMD, DBG_TRACE,
		"--->Cmd(%#x), set_io_inprogress(%d)\n",
		rtlphy->current_io_type, rtlphy->set_io_inprogress);
	switch (rtlphy->current_io_type) {
	case IO_CMD_RESUME_DM_BY_SCAN:
		dm_digtable->cur_igvalue = rtlphy->initgain_backup.xaagccore1;
		/*rtl92c_dm_write_dig(hw);*/
		rtl88e_phy_set_txpower_level(hw, rtlphy->current_channel);
		rtl_set_bbreg(hw, RCCK0_CCA, 0xff0000, 0x83);
		break;
	case IO_CMD_PAUSE_BAND0_DM_BY_SCAN:
		rtlphy->initgain_backup.xaagccore1 = dm_digtable->cur_igvalue;
		dm_digtable->cur_igvalue = 0x17;
		rtl_set_bbreg(hw, RCCK0_CCA, 0xff0000, 0x40);
		break;
	default:
		rtl_dbg(rtlpriv, COMP_ERR, DBG_LOUD,
			"switch case %#x not processed\n",
			rtlphy->current_io_type);
		break;
	}
	rtlphy->set_io_inprogress = false;
	rtl_dbg(rtlpriv, COMP_CMD, DBG_TRACE,
		"(%#x)\n", rtlphy->current_io_type);
}

static void rtl88ee_phy_set_rf_on(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_write_byte(rtlpriv, REG_SPS0_CTRL, 0x2b);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE3);
	/*rtl_write_byte(rtlpriv, REG_APSD_CTRL, 0x00);*/
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE2);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE3);
	rtl_write_byte(rtlpriv, REG_TXPAUSE, 0x00);
}

static void _rtl88ee_phy_set_rf_sleep(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_write_byte(rtlpriv, REG_TXPAUSE, 0xFF);
	rtl_set_rfreg(hw, RF90_PATH_A, 0x00, RFREG_OFFSET_MASK, 0x00);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE2);
	rtl_write_byte(rtlpriv, REG_SPS0_CTRL, 0x22);
}

static bool _rtl88ee_phy_set_rf_power_state(struct ieee80211_hw *hw,
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
				rtl_dbg(rtlpriv, COMP_RF, DBG_DMESG,
					"IPS Set eRf nic enable\n");
				rtstatus = rtl_ps_enable_nic(hw);
			} while (!rtstatus &&
				 (initializecount < 10));
			RT_CLEAR_PS_LEVEL(ppsc,
					  RT_RF_OFF_LEVL_HALT_NIC);
		} else {
			rtl_dbg(rtlpriv, COMP_RF, DBG_DMESG,
				"Set ERFON slept:%d ms\n",
				jiffies_to_msecs(jiffies -
						 ppsc->last_sleep_jiffies));
			ppsc->last_awake_jiffies = jiffies;
			rtl88ee_phy_set_rf_on(hw);
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
				rtl_dbg(rtlpriv, COMP_ERR, DBG_WARNING,
					"eRf Off/Sleep: %d times TcbBusyQueue[%d] =%d before doze!\n",
					(i + 1), queue_id,
					skb_queue_len(&ring->queue));

				udelay(10);
				i++;
			}
			if (i >= MAX_DOZE_WAITING_TIMES_9x) {
				rtl_dbg(rtlpriv, COMP_ERR, DBG_WARNING,
					"\n ERFSLEEP: %d times TcbBusyQueue[%d] = %d !\n",
					MAX_DOZE_WAITING_TIMES_9x,
					queue_id,
					skb_queue_len(&ring->queue));
				break;
			}
		}

		if (ppsc->reg_rfps_level & RT_RF_OFF_LEVL_HALT_NIC) {
			rtl_dbg(rtlpriv, COMP_RF, DBG_DMESG,
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
	case ERFSLEEP:{
			if (ppsc->rfpwr_state == ERFOFF)
				break;
			for (queue_id = 0, i = 0;
			     queue_id < RTL_PCI_MAX_TX_QUEUE_COUNT;) {
				ring = &pcipriv->dev.tx_ring[queue_id];
				if (skb_queue_len(&ring->queue) == 0) {
					queue_id++;
					continue;
				} else {
					rtl_dbg(rtlpriv, COMP_ERR, DBG_WARNING,
						"eRf Off/Sleep: %d times TcbBusyQueue[%d] =%d before doze!\n",
						(i + 1), queue_id,
						skb_queue_len(&ring->queue));

					udelay(10);
					i++;
				}
				if (i >= MAX_DOZE_WAITING_TIMES_9x) {
					rtl_dbg(rtlpriv, COMP_ERR, DBG_WARNING,
						"\n ERFSLEEP: %d times TcbBusyQueue[%d] = %d !\n",
						MAX_DOZE_WAITING_TIMES_9x,
						queue_id,
						skb_queue_len(&ring->queue));
					break;
				}
			}
			rtl_dbg(rtlpriv, COMP_RF, DBG_DMESG,
				"Set ERFSLEEP awaked:%d ms\n",
				jiffies_to_msecs(jiffies -
				ppsc->last_awake_jiffies));
			ppsc->last_sleep_jiffies = jiffies;
			_rtl88ee_phy_set_rf_sleep(hw);
			break;
		}
	default:
		rtl_dbg(rtlpriv, COMP_ERR, DBG_LOUD,
			"switch case %#x not processed\n", rfpwr_state);
		bresult = false;
		break;
	}
	if (bresult)
		ppsc->rfpwr_state = rfpwr_state;
	return bresult;
}

bool rtl88e_phy_set_rf_power_state(struct ieee80211_hw *hw,
				   enum rf_pwrstate rfpwr_state)
{
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));

	bool bresult = false;

	if (rfpwr_state == ppsc->rfpwr_state)
		return bresult;
	bresult = _rtl88ee_phy_set_rf_power_state(hw, rfpwr_state);
	return bresult;
}
