/******************************************************************************
 *
 * Copyright(c) 2009-2012  Realtek Corporation.
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
#include "../pci.h"
#include "../ps.h"
#include "reg.h"
#include "def.h"
#include "phy.h"
#include "rf.h"
#include "dm.h"
#include "table.h"
#include "sw.h"
#include "hw.h"

#define MAX_RF_IMR_INDEX			12
#define MAX_RF_IMR_INDEX_NORMAL			13
#define RF_REG_NUM_FOR_C_CUT_5G			6
#define RF_REG_NUM_FOR_C_CUT_5G_INTERNALPA	7
#define RF_REG_NUM_FOR_C_CUT_2G			5
#define RF_CHNL_NUM_5G				19
#define RF_CHNL_NUM_5G_40M			17
#define TARGET_CHNL_NUM_5G			221
#define TARGET_CHNL_NUM_2G			14
#define CV_CURVE_CNT				64

static u32 rf_reg_for_5g_swchnl_normal[MAX_RF_IMR_INDEX_NORMAL] = {
	0, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x0
};

static u8 rf_reg_for_c_cut_5g[RF_REG_NUM_FOR_C_CUT_5G] = {
	RF_SYN_G1, RF_SYN_G2, RF_SYN_G3, RF_SYN_G4, RF_SYN_G5, RF_SYN_G6
};

static u8 rf_reg_for_c_cut_2g[RF_REG_NUM_FOR_C_CUT_2G] = {
	RF_SYN_G1, RF_SYN_G2, RF_SYN_G3, RF_SYN_G7, RF_SYN_G8
};

static u8 rf_for_c_cut_5g_internal_pa[RF_REG_NUM_FOR_C_CUT_5G_INTERNALPA] = {
	0x0B, 0x48, 0x49, 0x4B, 0x03, 0x04, 0x0E
};

static u32 rf_reg_mask_for_c_cut_2g[RF_REG_NUM_FOR_C_CUT_2G] = {
	BIT(19) | BIT(18) | BIT(17) | BIT(14) | BIT(1),
	BIT(10) | BIT(9),
	BIT(18) | BIT(17) | BIT(16) | BIT(1),
	BIT(2) | BIT(1),
	BIT(15) | BIT(14) | BIT(13) | BIT(12) | BIT(11)
};

static u8 rf_chnl_5g[RF_CHNL_NUM_5G] = {
	36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108,
	112, 116, 120, 124, 128, 132, 136, 140
};

static u8 rf_chnl_5g_40m[RF_CHNL_NUM_5G_40M] = {
	38, 42, 46, 50, 54, 58, 62, 102, 106, 110, 114,
	118, 122, 126, 130, 134, 138
};
static u32 rf_reg_pram_c_5g[5][RF_REG_NUM_FOR_C_CUT_5G] = {
	{0xE43BE, 0xFC638, 0x77C0A, 0xDE471, 0xd7110, 0x8EB04},
	{0xE43BE, 0xFC078, 0xF7C1A, 0xE0C71, 0xD7550, 0xAEB04},
	{0xE43BF, 0xFF038, 0xF7C0A, 0xDE471, 0xE5550, 0xAEB04},
	{0xE43BF, 0xFF079, 0xF7C1A, 0xDE471, 0xE5550, 0xAEB04},
	{0xE43BF, 0xFF038, 0xF7C1A, 0xDE471, 0xd7550, 0xAEB04}
};

static u32 rf_reg_param_for_c_cut_2g[3][RF_REG_NUM_FOR_C_CUT_2G] = {
	{0x643BC, 0xFC038, 0x77C1A, 0x41289, 0x01840},
	{0x643BC, 0xFC038, 0x07C1A, 0x41289, 0x01840},
	{0x243BC, 0xFC438, 0x07C1A, 0x4128B, 0x0FC41}
};

static u32 rf_syn_g4_for_c_cut_2g = 0xD1C31 & 0x7FF;

static u32 rf_pram_c_5g_int_pa[3][RF_REG_NUM_FOR_C_CUT_5G_INTERNALPA] = {
	{0x01a00, 0x40443, 0x00eb5, 0x89bec, 0x94a12, 0x94a12, 0x94a12},
	{0x01800, 0xc0443, 0x00730, 0x896ee, 0x94a52, 0x94a52, 0x94a52},
	{0x01800, 0xc0443, 0x00730, 0x896ee, 0x94a12, 0x94a12, 0x94a12}
};

/* [mode][patha+b][reg] */
static u32 rf_imr_param_normal[1][3][MAX_RF_IMR_INDEX_NORMAL] = {
	{
		/* channel 1-14. */
		{
			0x70000, 0x00ff0, 0x4400f, 0x00ff0, 0x0, 0x0, 0x0,
			0x0, 0x0, 0x64888, 0xe266c, 0x00090, 0x22fff
		},
		/* path 36-64 */
		{
			0x70000, 0x22880, 0x4470f, 0x55880, 0x00070, 0x88000,
			0x0, 0x88080, 0x70000, 0x64a82, 0xe466c, 0x00090,
			0x32c9a
		},
		/* 100 -165 */
		{
			0x70000, 0x44880, 0x4477f, 0x77880, 0x00070, 0x88000,
			0x0, 0x880b0, 0x0, 0x64b82, 0xe466c, 0x00090, 0x32c9a
		}
	}
};

static u32 curveindex_5g[TARGET_CHNL_NUM_5G] = {0};

static u32 curveindex_2g[TARGET_CHNL_NUM_2G] = {0};

static u32 targetchnl_5g[TARGET_CHNL_NUM_5G] = {
	25141, 25116, 25091, 25066, 25041,
	25016, 24991, 24966, 24941, 24917,
	24892, 24867, 24843, 24818, 24794,
	24770, 24765, 24721, 24697, 24672,
	24648, 24624, 24600, 24576, 24552,
	24528, 24504, 24480, 24457, 24433,
	24409, 24385, 24362, 24338, 24315,
	24291, 24268, 24245, 24221, 24198,
	24175, 24151, 24128, 24105, 24082,
	24059, 24036, 24013, 23990, 23967,
	23945, 23922, 23899, 23876, 23854,
	23831, 23809, 23786, 23764, 23741,
	23719, 23697, 23674, 23652, 23630,
	23608, 23586, 23564, 23541, 23519,
	23498, 23476, 23454, 23432, 23410,
	23388, 23367, 23345, 23323, 23302,
	23280, 23259, 23237, 23216, 23194,
	23173, 23152, 23130, 23109, 23088,
	23067, 23046, 23025, 23003, 22982,
	22962, 22941, 22920, 22899, 22878,
	22857, 22837, 22816, 22795, 22775,
	22754, 22733, 22713, 22692, 22672,
	22652, 22631, 22611, 22591, 22570,
	22550, 22530, 22510, 22490, 22469,
	22449, 22429, 22409, 22390, 22370,
	22350, 22336, 22310, 22290, 22271,
	22251, 22231, 22212, 22192, 22173,
	22153, 22134, 22114, 22095, 22075,
	22056, 22037, 22017, 21998, 21979,
	21960, 21941, 21921, 21902, 21883,
	21864, 21845, 21826, 21807, 21789,
	21770, 21751, 21732, 21713, 21695,
	21676, 21657, 21639, 21620, 21602,
	21583, 21565, 21546, 21528, 21509,
	21491, 21473, 21454, 21436, 21418,
	21400, 21381, 21363, 21345, 21327,
	21309, 21291, 21273, 21255, 21237,
	21219, 21201, 21183, 21166, 21148,
	21130, 21112, 21095, 21077, 21059,
	21042, 21024, 21007, 20989, 20972,
	25679, 25653, 25627, 25601, 25575,
	25549, 25523, 25497, 25471, 25446,
	25420, 25394, 25369, 25343, 25318,
	25292, 25267, 25242, 25216, 25191,
	25166
};

/* channel 1~14 */
static u32 targetchnl_2g[TARGET_CHNL_NUM_2G] = {
	26084, 26030, 25976, 25923, 25869, 25816, 25764,
	25711, 25658, 25606, 25554, 25502, 25451, 25328
};

static u32 _rtl92d_phy_calculate_bit_shift(u32 bitmask)
{
	u32 i;

	for (i = 0; i <= 31; i++) {
		if (((bitmask >> i) & 0x1) == 1)
			break;
	}

	return i;
}

u32 rtl92d_phy_query_bb_reg(struct ieee80211_hw *hw, u32 regaddr, u32 bitmask)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	u32 returnvalue, originalvalue, bitshift;
	u8 dbi_direct;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE, "regaddr(%#x), bitmask(%#x)\n",
		 regaddr, bitmask);
	if (rtlhal->during_mac1init_radioa || rtlhal->during_mac0init_radiob) {
		/* mac1 use phy0 read radio_b. */
		/* mac0 use phy1 read radio_b. */
		if (rtlhal->during_mac1init_radioa)
			dbi_direct = BIT(3);
		else if (rtlhal->during_mac0init_radiob)
			dbi_direct = BIT(3) | BIT(2);
		originalvalue = rtl92de_read_dword_dbi(hw, (u16)regaddr,
			dbi_direct);
	} else {
		originalvalue = rtl_read_dword(rtlpriv, regaddr);
	}
	bitshift = _rtl92d_phy_calculate_bit_shift(bitmask);
	returnvalue = (originalvalue & bitmask) >> bitshift;
	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "BBR MASK=0x%x Addr[0x%x]=0x%x\n",
		 bitmask, regaddr, originalvalue);
	return returnvalue;
}

void rtl92d_phy_set_bb_reg(struct ieee80211_hw *hw,
			   u32 regaddr, u32 bitmask, u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	u8 dbi_direct = 0;
	u32 originalvalue, bitshift;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), bitmask(%#x), data(%#x)\n",
		 regaddr, bitmask, data);
	if (rtlhal->during_mac1init_radioa)
		dbi_direct = BIT(3);
	else if (rtlhal->during_mac0init_radiob)
		/* mac0 use phy1 write radio_b. */
		dbi_direct = BIT(3) | BIT(2);
	if (bitmask != BMASKDWORD) {
		if (rtlhal->during_mac1init_radioa ||
		    rtlhal->during_mac0init_radiob)
			originalvalue = rtl92de_read_dword_dbi(hw,
					(u16) regaddr,
					dbi_direct);
		else
			originalvalue = rtl_read_dword(rtlpriv, regaddr);
		bitshift = _rtl92d_phy_calculate_bit_shift(bitmask);
		data = ((originalvalue & (~bitmask)) | (data << bitshift));
	}
	if (rtlhal->during_mac1init_radioa || rtlhal->during_mac0init_radiob)
		rtl92de_write_dword_dbi(hw, (u16) regaddr, data, dbi_direct);
	else
		rtl_write_dword(rtlpriv, regaddr, data);
	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), bitmask(%#x), data(%#x)\n",
		 regaddr, bitmask, data);
}

static u32 _rtl92d_phy_rf_serial_read(struct ieee80211_hw *hw,
				      enum radio_path rfpath, u32 offset)
{

	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct bb_reg_def *pphyreg = &rtlphy->phyreg_def[rfpath];
	u32 newoffset;
	u32 tmplong, tmplong2;
	u8 rfpi_enable = 0;
	u32 retvalue;

	newoffset = offset;
	tmplong = rtl_get_bbreg(hw, RFPGA0_XA_HSSIPARAMETER2, BMASKDWORD);
	if (rfpath == RF90_PATH_A)
		tmplong2 = tmplong;
	else
		tmplong2 = rtl_get_bbreg(hw, pphyreg->rfhssi_para2, BMASKDWORD);
	tmplong2 = (tmplong2 & (~BLSSIREADADDRESS)) |
		(newoffset << 23) | BLSSIREADEDGE;
	rtl_set_bbreg(hw, RFPGA0_XA_HSSIPARAMETER2, BMASKDWORD,
		tmplong & (~BLSSIREADEDGE));
	udelay(10);
	rtl_set_bbreg(hw, pphyreg->rfhssi_para2, BMASKDWORD, tmplong2);
	udelay(50);
	udelay(50);
	rtl_set_bbreg(hw, RFPGA0_XA_HSSIPARAMETER2, BMASKDWORD,
		tmplong | BLSSIREADEDGE);
	udelay(10);
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
	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE, "RFR-%d Addr[0x%x] = 0x%x\n",
		 rfpath, pphyreg->rflssi_readback, retvalue);
	return retvalue;
}

static void _rtl92d_phy_rf_serial_write(struct ieee80211_hw *hw,
					enum radio_path rfpath,
					u32 offset, u32 data)
{
	u32 data_and_addr;
	u32 newoffset;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct bb_reg_def *pphyreg = &rtlphy->phyreg_def[rfpath];

	newoffset = offset;
	/* T65 RF */
	data_and_addr = ((newoffset << 20) | (data & 0x000fffff)) & 0x0fffffff;
	rtl_set_bbreg(hw, pphyreg->rf3wire_offset, BMASKDWORD, data_and_addr);
	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE, "RFW-%d Addr[0x%x]=0x%x\n",
		 rfpath, pphyreg->rf3wire_offset, data_and_addr);
}

u32 rtl92d_phy_query_rf_reg(struct ieee80211_hw *hw,
			    enum radio_path rfpath, u32 regaddr, u32 bitmask)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 original_value, readback_value, bitshift;
	unsigned long flags;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), rfpath(%#x), bitmask(%#x)\n",
		 regaddr, rfpath, bitmask);
	spin_lock_irqsave(&rtlpriv->locks.rf_lock, flags);
	original_value = _rtl92d_phy_rf_serial_read(hw, rfpath, regaddr);
	bitshift = _rtl92d_phy_calculate_bit_shift(bitmask);
	readback_value = (original_value & bitmask) >> bitshift;
	spin_unlock_irqrestore(&rtlpriv->locks.rf_lock, flags);
	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), rfpath(%#x), bitmask(%#x), original_value(%#x)\n",
		 regaddr, rfpath, bitmask, original_value);
	return readback_value;
}

void rtl92d_phy_set_rf_reg(struct ieee80211_hw *hw, enum radio_path rfpath,
	u32 regaddr, u32 bitmask, u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u32 original_value, bitshift;
	unsigned long flags;

	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), bitmask(%#x), data(%#x), rfpath(%#x)\n",
		 regaddr, bitmask, data, rfpath);
	if (bitmask == 0)
		return;
	spin_lock_irqsave(&rtlpriv->locks.rf_lock, flags);
	if (rtlphy->rf_mode != RF_OP_BY_FW) {
		if (bitmask != BRFREGOFFSETMASK) {
			original_value = _rtl92d_phy_rf_serial_read(hw,
				rfpath, regaddr);
			bitshift = _rtl92d_phy_calculate_bit_shift(bitmask);
			data = ((original_value & (~bitmask)) |
				(data << bitshift));
		}
		_rtl92d_phy_rf_serial_write(hw, rfpath, regaddr, data);
	}
	spin_unlock_irqrestore(&rtlpriv->locks.rf_lock, flags);
	RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
		 "regaddr(%#x), bitmask(%#x), data(%#x), rfpath(%#x)\n",
		 regaddr, bitmask, data, rfpath);
}

bool rtl92d_phy_mac_config(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 i;
	u32 arraylength;
	u32 *ptrarray;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE, "Read Rtl819XMACPHY_Array\n");
	arraylength = MAC_2T_ARRAYLENGTH;
	ptrarray = rtl8192de_mac_2tarray;
	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE, "Img:Rtl819XMAC_Array\n");
	for (i = 0; i < arraylength; i = i + 2)
		rtl_write_byte(rtlpriv, ptrarray[i], (u8) ptrarray[i + 1]);
	if (rtlpriv->rtlhal.macphymode == SINGLEMAC_SINGLEPHY) {
		/* improve 2-stream TX EVM */
		/* rtl_write_byte(rtlpriv, 0x14,0x71); */
		/* AMPDU aggregation number 9 */
		/* rtl_write_word(rtlpriv, REG_MAX_AGGR_NUM, MAX_AGGR_NUM); */
		rtl_write_byte(rtlpriv, REG_MAX_AGGR_NUM, 0x0B);
	} else {
		/* 92D need to test to decide the num. */
		rtl_write_byte(rtlpriv, REG_MAX_AGGR_NUM, 0x07);
	}
	return true;
}

static void _rtl92d_phy_init_bb_rf_register_definition(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);

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

	/* Addr of LSSI. Wirte RF register by driver */
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

	/* Tranceiver A~D HSSI Parameter-1 */
	/* wire control parameter1 */
	rtlphy->phyreg_def[RF90_PATH_A].rfhssi_para1 = RFPGA0_XA_HSSIPARAMETER1;
	/* wire control parameter1 */
	rtlphy->phyreg_def[RF90_PATH_B].rfhssi_para1 = RFPGA0_XB_HSSIPARAMETER1;

	/* Tranceiver A~D HSSI Parameter-2 */
	/* wire control parameter2 */
	rtlphy->phyreg_def[RF90_PATH_A].rfhssi_para2 = RFPGA0_XA_HSSIPARAMETER2;
	/* wire control parameter2 */
	rtlphy->phyreg_def[RF90_PATH_B].rfhssi_para2 = RFPGA0_XB_HSSIPARAMETER2;

	/* RF switch Control */
	/* TR/Ant switch control */
	rtlphy->phyreg_def[RF90_PATH_A].rfswitch_control =
		RFPGA0_XAB_SWITCHCONTROL;
	rtlphy->phyreg_def[RF90_PATH_B].rfswitch_control =
	    RFPGA0_XAB_SWITCHCONTROL;
	rtlphy->phyreg_def[RF90_PATH_C].rfswitch_control =
	    RFPGA0_XCD_SWITCHCONTROL;
	rtlphy->phyreg_def[RF90_PATH_D].rfswitch_control =
	    RFPGA0_XCD_SWITCHCONTROL;

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
	rtlphy->phyreg_def[RF90_PATH_A].rfrxiq_imbalance =
	    ROFDM0_XARXIQIMBALANCE;
	rtlphy->phyreg_def[RF90_PATH_B].rfrxiq_imbalance =
	    ROFDM0_XBRXIQIMBALANCE;
	rtlphy->phyreg_def[RF90_PATH_C].rfrxiq_imbalance =
	    ROFDM0_XCRXIQIMBALANCE;
	rtlphy->phyreg_def[RF90_PATH_D].rfrxiq_imbalance =
	    ROFDM0_XDRXIQIMBALANCE;

	/*RX AFE control 1 */
	rtlphy->phyreg_def[RF90_PATH_A].rfrx_afe = ROFDM0_XARXAFE;
	rtlphy->phyreg_def[RF90_PATH_B].rfrx_afe = ROFDM0_XBRXAFE;
	rtlphy->phyreg_def[RF90_PATH_C].rfrx_afe = ROFDM0_XCRXAFE;
	rtlphy->phyreg_def[RF90_PATH_D].rfrx_afe = ROFDM0_XDRXAFE;

	/* Tx AFE control 1 */
	rtlphy->phyreg_def[RF90_PATH_A].rftxiq_imbalance =
	    ROFDM0_XATxIQIMBALANCE;
	rtlphy->phyreg_def[RF90_PATH_B].rftxiq_imbalance =
	    ROFDM0_XBTxIQIMBALANCE;
	rtlphy->phyreg_def[RF90_PATH_C].rftxiq_imbalance =
	    ROFDM0_XCTxIQIMBALANCE;
	rtlphy->phyreg_def[RF90_PATH_D].rftxiq_imbalance =
	    ROFDM0_XDTxIQIMBALANCE;

	/* Tx AFE control 2 */
	rtlphy->phyreg_def[RF90_PATH_A].rftx_afe = ROFDM0_XATxAFE;
	rtlphy->phyreg_def[RF90_PATH_B].rftx_afe = ROFDM0_XBTxAFE;
	rtlphy->phyreg_def[RF90_PATH_C].rftx_afe = ROFDM0_XCTxAFE;
	rtlphy->phyreg_def[RF90_PATH_D].rftx_afe = ROFDM0_XDTxAFE;

	/* Tranceiver LSSI Readback SI mode */
	rtlphy->phyreg_def[RF90_PATH_A].rflssi_readback =
	    RFPGA0_XA_LSSIREADBACK;
	rtlphy->phyreg_def[RF90_PATH_B].rflssi_readback =
	    RFPGA0_XB_LSSIREADBACK;
	rtlphy->phyreg_def[RF90_PATH_C].rflssi_readback =
	    RFPGA0_XC_LSSIREADBACK;
	rtlphy->phyreg_def[RF90_PATH_D].rflssi_readback =
	    RFPGA0_XD_LSSIREADBACK;

	/* Tranceiver LSSI Readback PI mode */
	rtlphy->phyreg_def[RF90_PATH_A].rflssi_readbackpi =
	    TRANSCEIVERA_HSPI_READBACK;
	rtlphy->phyreg_def[RF90_PATH_B].rflssi_readbackpi =
	    TRANSCEIVERB_HSPI_READBACK;
}

static bool _rtl92d_phy_config_bb_with_headerfile(struct ieee80211_hw *hw,
	u8 configtype)
{
	int i;
	u32 *phy_regarray_table;
	u32 *agctab_array_table = NULL;
	u32 *agctab_5garray_table;
	u16 phy_reg_arraylen, agctab_arraylen = 0, agctab_5garraylen;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	/* Normal chip,Mac0 use AGC_TAB.txt for 2G and 5G band. */
	if (rtlhal->interfaceindex == 0) {
		agctab_arraylen = AGCTAB_ARRAYLENGTH;
		agctab_array_table = rtl8192de_agctab_array;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 " ===> phy:MAC0, Rtl819XAGCTAB_Array\n");
	} else {
		if (rtlhal->current_bandtype == BAND_ON_2_4G) {
			agctab_arraylen = AGCTAB_2G_ARRAYLENGTH;
			agctab_array_table = rtl8192de_agctab_2garray;
			RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
				 " ===> phy:MAC1, Rtl819XAGCTAB_2GArray\n");
		} else {
			agctab_5garraylen = AGCTAB_5G_ARRAYLENGTH;
			agctab_5garray_table = rtl8192de_agctab_5garray;
			RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
				 " ===> phy:MAC1, Rtl819XAGCTAB_5GArray\n");

		}
	}
	phy_reg_arraylen = PHY_REG_2T_ARRAYLENGTH;
	phy_regarray_table = rtl8192de_phy_reg_2tarray;
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 " ===> phy:Rtl819XPHY_REG_Array_PG\n");
	if (configtype == BASEBAND_CONFIG_PHY_REG) {
		for (i = 0; i < phy_reg_arraylen; i = i + 2) {
			if (phy_regarray_table[i] == 0xfe)
				mdelay(50);
			else if (phy_regarray_table[i] == 0xfd)
				mdelay(5);
			else if (phy_regarray_table[i] == 0xfc)
				mdelay(1);
			else if (phy_regarray_table[i] == 0xfb)
				udelay(50);
			else if (phy_regarray_table[i] == 0xfa)
				udelay(5);
			else if (phy_regarray_table[i] == 0xf9)
				udelay(1);
			rtl_set_bbreg(hw, phy_regarray_table[i], BMASKDWORD,
				      phy_regarray_table[i + 1]);
			udelay(1);
			RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
				 "The phy_regarray_table[0] is %x Rtl819XPHY_REGArray[1] is %x\n",
				 phy_regarray_table[i],
				 phy_regarray_table[i + 1]);
		}
	} else if (configtype == BASEBAND_CONFIG_AGC_TAB) {
		if (rtlhal->interfaceindex == 0) {
			for (i = 0; i < agctab_arraylen; i = i + 2) {
				rtl_set_bbreg(hw, agctab_array_table[i],
					BMASKDWORD,
					agctab_array_table[i + 1]);
				/* Add 1us delay between BB/RF register
				 * setting. */
				udelay(1);
				RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
					 "The Rtl819XAGCTAB_Array_Table[0] is %ul Rtl819XPHY_REGArray[1] is %ul\n",
					 agctab_array_table[i],
					 agctab_array_table[i + 1]);
			}
			RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
				 "Normal Chip, MAC0, load Rtl819XAGCTAB_Array\n");
		} else {
			if (rtlhal->current_bandtype == BAND_ON_2_4G) {
				for (i = 0; i < agctab_arraylen; i = i + 2) {
					rtl_set_bbreg(hw, agctab_array_table[i],
						BMASKDWORD,
						agctab_array_table[i + 1]);
					/* Add 1us delay between BB/RF register
					 * setting. */
					udelay(1);
					RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
						 "The Rtl819XAGCTAB_Array_Table[0] is %ul Rtl819XPHY_REGArray[1] is %ul\n",
						 agctab_array_table[i],
						 agctab_array_table[i + 1]);
				}
				RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
					 "Load Rtl819XAGCTAB_2GArray\n");
			} else {
				for (i = 0; i < agctab_5garraylen; i = i + 2) {
					rtl_set_bbreg(hw,
						agctab_5garray_table[i],
						BMASKDWORD,
						agctab_5garray_table[i + 1]);
					/* Add 1us delay between BB/RF registeri
					 * setting. */
					udelay(1);
					RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
						 "The Rtl819XAGCTAB_5GArray_Table[0] is %ul Rtl819XPHY_REGArray[1] is %ul\n",
						 agctab_5garray_table[i],
						 agctab_5garray_table[i + 1]);
				}
				RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
					 "Load Rtl819XAGCTAB_5GArray\n");
			}
		}
	}
	return true;
}

static void _rtl92d_store_pwrindex_diffrate_offset(struct ieee80211_hw *hw,
						   u32 regaddr, u32 bitmask,
						   u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
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

	rtlphy->mcs_txpwrlevel_origoffset[rtlphy->pwrgroup_cnt][index] = data;
	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
		 "MCSTxPowerLevelOriginalOffset[%d][%d] = 0x%ulx\n",
		 rtlphy->pwrgroup_cnt, index,
		 rtlphy->mcs_txpwrlevel_origoffset
		 [rtlphy->pwrgroup_cnt][index]);
	if (index == 13)
		rtlphy->pwrgroup_cnt++;
}

static bool _rtl92d_phy_config_bb_with_pgheaderfile(struct ieee80211_hw *hw,
	u8 configtype)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	int i;
	u32 *phy_regarray_table_pg;
	u16 phy_regarray_pg_len;

	phy_regarray_pg_len = PHY_REG_ARRAY_PG_LENGTH;
	phy_regarray_table_pg = rtl8192de_phy_reg_array_pg;
	if (configtype == BASEBAND_CONFIG_PHY_REG) {
		for (i = 0; i < phy_regarray_pg_len; i = i + 3) {
			if (phy_regarray_table_pg[i] == 0xfe)
				mdelay(50);
			else if (phy_regarray_table_pg[i] == 0xfd)
				mdelay(5);
			else if (phy_regarray_table_pg[i] == 0xfc)
				mdelay(1);
			else if (phy_regarray_table_pg[i] == 0xfb)
				udelay(50);
			else if (phy_regarray_table_pg[i] == 0xfa)
				udelay(5);
			else if (phy_regarray_table_pg[i] == 0xf9)
				udelay(1);
			_rtl92d_store_pwrindex_diffrate_offset(hw,
				phy_regarray_table_pg[i],
				phy_regarray_table_pg[i + 1],
				phy_regarray_table_pg[i + 2]);
		}
	} else {
		RT_TRACE(rtlpriv, COMP_SEND, DBG_TRACE,
			 "configtype != BaseBand_Config_PHY_REG\n");
	}
	return true;
}

static bool _rtl92d_phy_bb_config(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	bool rtstatus = true;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE, "==>\n");
	rtstatus = _rtl92d_phy_config_bb_with_headerfile(hw,
		BASEBAND_CONFIG_PHY_REG);
	if (!rtstatus) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "Write BB Reg Fail!!\n");
		return false;
	}

	/* if (rtlphy->rf_type == RF_1T2R) {
	 *      _rtl92c_phy_bb_config_1t(hw);
	 *     RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE, "Config to 1T!!\n");
	 *} */

	if (rtlefuse->autoload_failflag == false) {
		rtlphy->pwrgroup_cnt = 0;
		rtstatus = _rtl92d_phy_config_bb_with_pgheaderfile(hw,
			BASEBAND_CONFIG_PHY_REG);
	}
	if (!rtstatus) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "BB_PG Reg Fail!!\n");
		return false;
	}
	rtstatus = _rtl92d_phy_config_bb_with_headerfile(hw,
		BASEBAND_CONFIG_AGC_TAB);
	if (!rtstatus) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "AGC Table Fail\n");
		return false;
	}
	rtlphy->cck_high_power = (bool) (rtl_get_bbreg(hw,
		RFPGA0_XA_HSSIPARAMETER2, 0x200));

	return true;
}

bool rtl92d_phy_bb_config(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u16 regval;
	u32 regvaldw;
	u8 value;

	_rtl92d_phy_init_bb_rf_register_definition(hw);
	regval = rtl_read_word(rtlpriv, REG_SYS_FUNC_EN);
	rtl_write_word(rtlpriv, REG_SYS_FUNC_EN,
		       regval | BIT(13) | BIT(0) | BIT(1));
	rtl_write_byte(rtlpriv, REG_AFE_PLL_CTRL, 0x83);
	rtl_write_byte(rtlpriv, REG_AFE_PLL_CTRL + 1, 0xdb);
	/* 0x1f bit7 bit6 represent for mac0/mac1 driver ready */
	value = rtl_read_byte(rtlpriv, REG_RF_CTRL);
	rtl_write_byte(rtlpriv, REG_RF_CTRL, value | RF_EN | RF_RSTB |
		RF_SDMRSTB);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, FEN_PPLL | FEN_PCIEA |
		FEN_DIO_PCIE | FEN_BB_GLB_RSTn | FEN_BBRSTB);
	rtl_write_byte(rtlpriv, REG_AFE_XTAL_CTRL + 1, 0x80);
	if (!(IS_92D_SINGLEPHY(rtlpriv->rtlhal.version))) {
		regvaldw = rtl_read_dword(rtlpriv, REG_LEDCFG0);
		rtl_write_dword(rtlpriv, REG_LEDCFG0, regvaldw | BIT(23));
	}

	return _rtl92d_phy_bb_config(hw);
}

bool rtl92d_phy_rf_config(struct ieee80211_hw *hw)
{
	return rtl92d_phy_rf6052_config(hw);
}

bool rtl92d_phy_config_rf_with_headerfile(struct ieee80211_hw *hw,
					  enum rf_content content,
					  enum radio_path rfpath)
{
	int i;
	u32 *radioa_array_table;
	u32 *radiob_array_table;
	u16 radioa_arraylen, radiob_arraylen;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	radioa_arraylen = RADIOA_2T_ARRAYLENGTH;
	radioa_array_table = rtl8192de_radioa_2tarray;
	radiob_arraylen = RADIOB_2T_ARRAYLENGTH;
	radiob_array_table = rtl8192de_radiob_2tarray;
	if (rtlpriv->efuse.internal_pa_5g[0]) {
		radioa_arraylen = RADIOA_2T_INT_PA_ARRAYLENGTH;
		radioa_array_table = rtl8192de_radioa_2t_int_paarray;
	}
	if (rtlpriv->efuse.internal_pa_5g[1]) {
		radiob_arraylen = RADIOB_2T_INT_PA_ARRAYLENGTH;
		radiob_array_table = rtl8192de_radiob_2t_int_paarray;
	}
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "PHY_ConfigRFWithHeaderFile() Radio_A:Rtl819XRadioA_1TArray\n");
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "PHY_ConfigRFWithHeaderFile() Radio_B:Rtl819XRadioB_1TArray\n");
	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE, "Radio No %x\n", rfpath);

	/* this only happens when DMDP, mac0 start on 2.4G,
	 * mac1 start on 5G, mac 0 has to set phy0&phy1
	 * pathA or mac1 has to set phy0&phy1 pathA */
	if ((content == radiob_txt) && (rfpath == RF90_PATH_A)) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 " ===> althougth Path A, we load radiob.txt\n");
		radioa_arraylen = radiob_arraylen;
		radioa_array_table = radiob_array_table;
	}
	switch (rfpath) {
	case RF90_PATH_A:
		for (i = 0; i < radioa_arraylen; i = i + 2) {
			if (radioa_array_table[i] == 0xfe) {
				mdelay(50);
			} else if (radioa_array_table[i] == 0xfd) {
				/* delay_ms(5); */
				mdelay(5);
			} else if (radioa_array_table[i] == 0xfc) {
				/* delay_ms(1); */
				mdelay(1);
			} else if (radioa_array_table[i] == 0xfb) {
				udelay(50);
			} else if (radioa_array_table[i] == 0xfa) {
				udelay(5);
			} else if (radioa_array_table[i] == 0xf9) {
				udelay(1);
			} else {
				rtl_set_rfreg(hw, rfpath, radioa_array_table[i],
					      BRFREGOFFSETMASK,
					      radioa_array_table[i + 1]);
				/*  Add 1us delay between BB/RF register set. */
				udelay(1);
			}
		}
		break;
	case RF90_PATH_B:
		for (i = 0; i < radiob_arraylen; i = i + 2) {
			if (radiob_array_table[i] == 0xfe) {
				/* Delay specific ms. Only RF configuration
				 * requires delay. */
				mdelay(50);
			} else if (radiob_array_table[i] == 0xfd) {
				/* delay_ms(5); */
				mdelay(5);
			} else if (radiob_array_table[i] == 0xfc) {
				/* delay_ms(1); */
				mdelay(1);
			} else if (radiob_array_table[i] == 0xfb) {
				udelay(50);
			} else if (radiob_array_table[i] == 0xfa) {
				udelay(5);
			} else if (radiob_array_table[i] == 0xf9) {
				udelay(1);
			} else {
				rtl_set_rfreg(hw, rfpath, radiob_array_table[i],
					      BRFREGOFFSETMASK,
					      radiob_array_table[i + 1]);
				/*  Add 1us delay between BB/RF register set. */
				udelay(1);
			}
		}
		break;
	case RF90_PATH_C:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "switch case not processed\n");
		break;
	case RF90_PATH_D:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "switch case not processed\n");
		break;
	}
	return true;
}

void rtl92d_phy_get_hw_reg_originalvalue(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);

	rtlphy->default_initialgain[0] =
	    (u8) rtl_get_bbreg(hw, ROFDM0_XAAGCCORE1, BMASKBYTE0);
	rtlphy->default_initialgain[1] =
	    (u8) rtl_get_bbreg(hw, ROFDM0_XBAGCCORE1, BMASKBYTE0);
	rtlphy->default_initialgain[2] =
	    (u8) rtl_get_bbreg(hw, ROFDM0_XCAGCCORE1, BMASKBYTE0);
	rtlphy->default_initialgain[3] =
	    (u8) rtl_get_bbreg(hw, ROFDM0_XDAGCCORE1, BMASKBYTE0);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
		 "Default initial gain (c50=0x%x, c58=0x%x, c60=0x%x, c68=0x%x\n",
		 rtlphy->default_initialgain[0],
		 rtlphy->default_initialgain[1],
		 rtlphy->default_initialgain[2],
		 rtlphy->default_initialgain[3]);
	rtlphy->framesync = (u8)rtl_get_bbreg(hw, ROFDM0_RXDETECTOR3,
					      BMASKBYTE0);
	rtlphy->framesync_c34 = rtl_get_bbreg(hw, ROFDM0_RXDETECTOR2,
					      BMASKDWORD);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
		 "Default framesync (0x%x) = 0x%x\n",
		 ROFDM0_RXDETECTOR3, rtlphy->framesync);
}

static void _rtl92d_get_txpower_index(struct ieee80211_hw *hw, u8 channel,
	u8 *cckpowerlevel, u8 *ofdmpowerlevel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_hal *rtlhal = &(rtlpriv->rtlhal);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u8 index = (channel - 1);

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
	u8 channel, u8 *cckpowerlevel, u8 *ofdmpowerlevel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);

	rtlphy->cur_cck_txpwridx = cckpowerlevel[0];
	rtlphy->cur_ofdm24g_txpwridx = ofdmpowerlevel[0];
}

static u8 _rtl92c_phy_get_rightchnlplace(u8 chnl)
{
	u8 channel_5g[59] = {
		1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
		36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58,
		60, 62, 64, 100, 102, 104, 106, 108, 110, 112,
		114, 116, 118, 120, 122, 124, 126, 128,
		130, 132, 134, 136, 138, 140, 149, 151,
		153, 155, 157, 159, 161, 163, 165
	};
	u8 place = chnl;

	if (chnl > 14) {
		for (place = 14; place < sizeof(channel_5g); place++) {
			if (channel_5g[place] == chnl) {
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

void rtl92d_phy_scan_operation_backup(struct ieee80211_hw *hw, u8 operation)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	enum io_type iotype;

	if (!is_hal_stop(rtlhal)) {
		switch (operation) {
		case SCAN_OPT_BACKUP:
			rtlhal->current_bandtypebackup =
						 rtlhal->current_bandtype;
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
				 "Unknown Scan Backup operation\n");
			break;
		}
	}
}

void rtl92d_phy_set_bw_mode(struct ieee80211_hw *hw,
			    enum nl80211_channel_type ch_type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	unsigned long flag = 0;
	u8 reg_prsr_rsc;
	u8 reg_bw_opmode;

	if (rtlphy->set_bwmode_inprogress)
		return;
	if ((is_hal_stop(rtlhal)) || (RT_CANNOT_IO(hw))) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 "FALSE driver sleep or unload\n");
		return;
	}
	rtlphy->set_bwmode_inprogress = true;
	RT_TRACE(rtlpriv, COMP_SCAN, DBG_TRACE, "Switch to %s bandwidth\n",
		 rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_20 ?
		 "20MHz" : "40MHz");
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
		/* SET BIT10 BIT11  for receive cck */
		rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER2, BIT(10) |
			      BIT(11), 3);
		break;
	case HT_CHANNEL_WIDTH_20_40:
		rtl_set_bbreg(hw, RFPGA0_RFMOD, BRFMOD, 0x1);
		rtl_set_bbreg(hw, RFPGA1_RFMOD, BRFMOD, 0x1);
		/* Set Control channel to upper or lower.
		 * These settings are required only for 40MHz */
		if (rtlhal->current_bandtype == BAND_ON_2_4G) {
			rtl92d_acquire_cckandrw_pagea_ctl(hw, &flag);
			rtl_set_bbreg(hw, RCCK0_SYSTEM, BCCKSIDEBAND,
				(mac->cur_40_prime_sc >> 1));
			rtl92d_release_cckandrw_pagea_ctl(hw, &flag);
		}
		rtl_set_bbreg(hw, ROFDM1_LSTF, 0xC00, mac->cur_40_prime_sc);
		/* SET BIT10 BIT11  for receive cck */
		rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER2, BIT(10) |
			      BIT(11), 0);
		rtl_set_bbreg(hw, 0x818, (BIT(26) | BIT(27)),
			(mac->cur_40_prime_sc ==
			HAL_PRIME_CHNL_OFFSET_LOWER) ? 2 : 1);
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "unknown bandwidth: %#X\n", rtlphy->current_chan_bw);
		break;

	}
	rtl92d_phy_rf6052_set_bandwidth(hw, rtlphy->current_chan_bw);
	rtlphy->set_bwmode_inprogress = false;
	RT_TRACE(rtlpriv, COMP_SCAN, DBG_TRACE, "<==\n");
}

static void _rtl92d_phy_stop_trx_before_changeband(struct ieee80211_hw *hw)
{
	rtl_set_bbreg(hw, RFPGA0_RFMOD, BCCKEN, 0);
	rtl_set_bbreg(hw, RFPGA0_RFMOD, BOFDMEN, 0);
	rtl_set_bbreg(hw, ROFDM0_TRXPATHENABLE, BMASKBYTE0, 0x00);
	rtl_set_bbreg(hw, ROFDM1_TRXPATHENABLE, BDWORD, 0x0);
}

static void rtl92d_phy_switch_wirelessband(struct ieee80211_hw *hw, u8 band)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 value8;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "==>\n");
	rtlhal->bandset = band;
	rtlhal->current_bandtype = band;
	if (IS_92D_SINGLEPHY(rtlhal->version))
		rtlhal->bandset = BAND_ON_BOTH;
	/* stop RX/Tx */
	_rtl92d_phy_stop_trx_before_changeband(hw);
	/* reconfig BB/RF according to wireless mode */
	if (rtlhal->current_bandtype == BAND_ON_2_4G) {
		/* BB & RF Config */
		RT_TRACE(rtlpriv, COMP_CMD, DBG_DMESG, "====>2.4G\n");
		if (rtlhal->interfaceindex == 1)
			_rtl92d_phy_config_bb_with_headerfile(hw,
				BASEBAND_CONFIG_AGC_TAB);
	} else {
		/* 5G band */
		RT_TRACE(rtlpriv, COMP_CMD, DBG_DMESG, "====>5G\n");
		if (rtlhal->interfaceindex == 1)
			_rtl92d_phy_config_bb_with_headerfile(hw,
				BASEBAND_CONFIG_AGC_TAB);
	}
	rtl92d_update_bbrf_configuration(hw);
	if (rtlhal->current_bandtype == BAND_ON_2_4G)
		rtl_set_bbreg(hw, RFPGA0_RFMOD, BCCKEN, 0x1);
	rtl_set_bbreg(hw, RFPGA0_RFMOD, BOFDMEN, 0x1);

	/* 20M BW. */
	/* rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER2, BIT(10), 1); */
	rtlhal->reloadtxpowerindex = true;
	/* notice fw know band status  0x81[1]/0x53[1] = 0: 5G, 1: 2G */
	if (rtlhal->current_bandtype == BAND_ON_2_4G) {
		value8 = rtl_read_byte(rtlpriv,	(rtlhal->interfaceindex ==
			0 ? REG_MAC0 : REG_MAC1));
		value8 |= BIT(1);
		rtl_write_byte(rtlpriv, (rtlhal->interfaceindex ==
			0 ? REG_MAC0 : REG_MAC1), value8);
	} else {
		value8 = rtl_read_byte(rtlpriv, (rtlhal->interfaceindex ==
			0 ? REG_MAC0 : REG_MAC1));
		value8 &= (~BIT(1));
		rtl_write_byte(rtlpriv, (rtlhal->interfaceindex ==
			0 ? REG_MAC0 : REG_MAC1), value8);
	}
	mdelay(1);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "<==Switch Band OK\n");
}

static void _rtl92d_phy_reload_imr_setting(struct ieee80211_hw *hw,
	u8 channel, u8 rfpath)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 imr_num = MAX_RF_IMR_INDEX;
	u32 rfmask = BRFREGOFFSETMASK;
	u8 group, i;
	unsigned long flag = 0;

	RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD, "====>path %d\n", rfpath);
	if (rtlpriv->rtlhal.current_bandtype == BAND_ON_5G) {
		RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD, "====>5G\n");
		rtl_set_bbreg(hw, RFPGA0_RFMOD, BIT(25) | BIT(24), 0);
		rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER4, 0x00f00000, 0xf);
		/* fc area 0xd2c */
		if (channel > 99)
			rtl_set_bbreg(hw, ROFDM1_CFOTRACKING, BIT(13) |
				      BIT(14), 2);
		else
			rtl_set_bbreg(hw, ROFDM1_CFOTRACKING, BIT(13) |
				      BIT(14), 1);
		/* leave 0 for channel1-14. */
		group = channel <= 64 ? 1 : 2;
		imr_num = MAX_RF_IMR_INDEX_NORMAL;
		for (i = 0; i < imr_num; i++)
			rtl_set_rfreg(hw, (enum radio_path)rfpath,
				      rf_reg_for_5g_swchnl_normal[i], rfmask,
				      rf_imr_param_normal[0][group][i]);
		rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER4, 0x00f00000, 0);
		rtl_set_bbreg(hw, RFPGA0_RFMOD, BOFDMEN, 1);
	} else {
		/* G band. */
		RT_TRACE(rtlpriv, COMP_SCAN, DBG_LOUD,
			 "Load RF IMR parameters for G band. IMR already setting %d\n",
			 rtlpriv->rtlhal.load_imrandiqk_setting_for2g);
		RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD, "====>2.4G\n");
		if (!rtlpriv->rtlhal.load_imrandiqk_setting_for2g) {
			RT_TRACE(rtlpriv, COMP_SCAN, DBG_LOUD,
				 "Load RF IMR parameters for G band. %d\n",
				 rfpath);
			rtl92d_acquire_cckandrw_pagea_ctl(hw, &flag);
			rtl_set_bbreg(hw, RFPGA0_RFMOD, BIT(25) | BIT(24), 0);
			rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER4,
				      0x00f00000, 0xf);
			imr_num = MAX_RF_IMR_INDEX_NORMAL;
			for (i = 0; i < imr_num; i++) {
				rtl_set_rfreg(hw, (enum radio_path)rfpath,
					      rf_reg_for_5g_swchnl_normal[i],
					      BRFREGOFFSETMASK,
					      rf_imr_param_normal[0][0][i]);
			}
			rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER4,
				      0x00f00000, 0);
			rtl_set_bbreg(hw, RFPGA0_RFMOD, BOFDMEN | BCCKEN, 3);
			rtl92d_release_cckandrw_pagea_ctl(hw, &flag);
		}
	}
	RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD, "<====\n");
}

static void _rtl92d_phy_enable_rf_env(struct ieee80211_hw *hw,
	u8 rfpath, u32 *pu4_regval)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct bb_reg_def *pphyreg = &rtlphy->phyreg_def[rfpath];

	RT_TRACE(rtlpriv, COMP_RF, DBG_LOUD, "====>\n");
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
	RT_TRACE(rtlpriv, COMP_RF, DBG_LOUD, "<====\n");
}

static void _rtl92d_phy_restore_rf_env(struct ieee80211_hw *hw, u8 rfpath,
				       u32 *pu4_regval)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct bb_reg_def *pphyreg = &rtlphy->phyreg_def[rfpath];

	RT_TRACE(rtlpriv, COMP_RF, DBG_LOUD, "=====>\n");
	/*----Restore RFENV control type----*/ ;
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
	RT_TRACE(rtlpriv, COMP_RF, DBG_LOUD, "<=====\n");
}

static void _rtl92d_phy_switch_rf_setting(struct ieee80211_hw *hw, u8 channel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_hal *rtlhal = &(rtlpriv->rtlhal);
	u8 path = rtlhal->current_bandtype ==
	    BAND_ON_5G ? RF90_PATH_A : RF90_PATH_B;
	u8 index = 0, i = 0, rfpath = RF90_PATH_A;
	bool need_pwr_down = false, internal_pa = false;
	u32 u4regvalue, mask = 0x1C000, value = 0, u4tmp, u4tmp2;

	RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD, "====>\n");
	/* config path A for 5G */
	if (rtlhal->current_bandtype == BAND_ON_5G) {
		RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD, "====>5G\n");
		u4tmp = curveindex_5g[channel - 1];
		RTPRINT(rtlpriv, FINIT, INIT_IQK,
			"ver 1 set RF-A, 5G, 0x28 = 0x%x !!\n", u4tmp);
		for (i = 0; i < RF_CHNL_NUM_5G; i++) {
			if (channel == rf_chnl_5g[i] && channel <= 140)
				index = 0;
		}
		for (i = 0; i < RF_CHNL_NUM_5G_40M; i++) {
			if (channel == rf_chnl_5g_40m[i] && channel <= 140)
				index = 1;
		}
		if (channel == 149 || channel == 155 || channel == 161)
			index = 2;
		else if (channel == 151 || channel == 153 || channel == 163
			 || channel == 165)
			index = 3;
		else if (channel == 157 || channel == 159)
			index = 4;

		if (rtlhal->macphymode == DUALMAC_DUALPHY
		    && rtlhal->interfaceindex == 1) {
			need_pwr_down = rtl92d_phy_enable_anotherphy(hw, false);
			rtlhal->during_mac1init_radioa = true;
			/* asume no this case */
			if (need_pwr_down)
				_rtl92d_phy_enable_rf_env(hw, path,
							  &u4regvalue);
		}
		for (i = 0; i < RF_REG_NUM_FOR_C_CUT_5G; i++) {
			if (i == 0 && (rtlhal->macphymode == DUALMAC_DUALPHY)) {
				rtl_set_rfreg(hw, (enum radio_path)path,
					      rf_reg_for_c_cut_5g[i],
					      BRFREGOFFSETMASK, 0xE439D);
			} else if (rf_reg_for_c_cut_5g[i] == RF_SYN_G4) {
				u4tmp2 = (rf_reg_pram_c_5g[index][i] &
				     0x7FF) | (u4tmp << 11);
				if (channel == 36)
					u4tmp2 &= ~(BIT(7) | BIT(6));
				rtl_set_rfreg(hw, (enum radio_path)path,
					      rf_reg_for_c_cut_5g[i],
					      BRFREGOFFSETMASK, u4tmp2);
			} else {
				rtl_set_rfreg(hw, (enum radio_path)path,
					      rf_reg_for_c_cut_5g[i],
					      BRFREGOFFSETMASK,
					      rf_reg_pram_c_5g[index][i]);
			}
			RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
				 "offset 0x%x value 0x%x path %d index %d readback 0x%x\n",
				 rf_reg_for_c_cut_5g[i],
				 rf_reg_pram_c_5g[index][i],
				 path, index,
				 rtl_get_rfreg(hw, (enum radio_path)path,
					       rf_reg_for_c_cut_5g[i],
					       BRFREGOFFSETMASK));
		}
		if (need_pwr_down)
			_rtl92d_phy_restore_rf_env(hw, path, &u4regvalue);
		if (rtlhal->during_mac1init_radioa)
			rtl92d_phy_powerdown_anotherphy(hw, false);
		if (channel < 149)
			value = 0x07;
		else if (channel >= 149)
			value = 0x02;
		if (channel >= 36 && channel <= 64)
			index = 0;
		else if (channel >= 100 && channel <= 140)
			index = 1;
		else
			index = 2;
		for (rfpath = RF90_PATH_A; rfpath < rtlphy->num_total_rfpath;
			rfpath++) {
			if (rtlhal->macphymode == DUALMAC_DUALPHY &&
				rtlhal->interfaceindex == 1)	/* MAC 1 5G */
				internal_pa = rtlpriv->efuse.internal_pa_5g[1];
			else
				internal_pa =
					 rtlpriv->efuse.internal_pa_5g[rfpath];
			if (internal_pa) {
				for (i = 0;
				     i < RF_REG_NUM_FOR_C_CUT_5G_INTERNALPA;
				     i++) {
					rtl_set_rfreg(hw, rfpath,
						rf_for_c_cut_5g_internal_pa[i],
						BRFREGOFFSETMASK,
						rf_pram_c_5g_int_pa[index][i]);
					RT_TRACE(rtlpriv, COMP_RF, DBG_LOUD,
						 "offset 0x%x value 0x%x path %d index %d\n",
						 rf_for_c_cut_5g_internal_pa[i],
						 rf_pram_c_5g_int_pa[index][i],
						 rfpath, index);
				}
			} else {
				rtl_set_rfreg(hw, (enum radio_path)rfpath, 0x0B,
					      mask, value);
			}
		}
	} else if (rtlhal->current_bandtype == BAND_ON_2_4G) {
		RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD, "====>2.4G\n");
		u4tmp = curveindex_2g[channel - 1];
		RTPRINT(rtlpriv, FINIT, INIT_IQK,
			"ver 3 set RF-B, 2G, 0x28 = 0x%x !!\n", u4tmp);
		if (channel == 1 || channel == 2 || channel == 4 || channel == 9
		    || channel == 10 || channel == 11 || channel == 12)
			index = 0;
		else if (channel == 3 || channel == 13 || channel == 14)
			index = 1;
		else if (channel >= 5 && channel <= 8)
			index = 2;
		if (rtlhal->macphymode == DUALMAC_DUALPHY) {
			path = RF90_PATH_A;
			if (rtlhal->interfaceindex == 0) {
				need_pwr_down =
					 rtl92d_phy_enable_anotherphy(hw, true);
				rtlhal->during_mac0init_radiob = true;

				if (need_pwr_down)
					_rtl92d_phy_enable_rf_env(hw, path,
								  &u4regvalue);
			}
		}
		for (i = 0; i < RF_REG_NUM_FOR_C_CUT_2G; i++) {
			if (rf_reg_for_c_cut_2g[i] == RF_SYN_G7)
				rtl_set_rfreg(hw, (enum radio_path)path,
					rf_reg_for_c_cut_2g[i],
					BRFREGOFFSETMASK,
					(rf_reg_param_for_c_cut_2g[index][i] |
					BIT(17)));
			else
				rtl_set_rfreg(hw, (enum radio_path)path,
					      rf_reg_for_c_cut_2g[i],
					      BRFREGOFFSETMASK,
					      rf_reg_param_for_c_cut_2g
					      [index][i]);
			RT_TRACE(rtlpriv, COMP_RF, DBG_TRACE,
				 "offset 0x%x value 0x%x mak 0x%x path %d index %d readback 0x%x\n",
				 rf_reg_for_c_cut_2g[i],
				 rf_reg_param_for_c_cut_2g[index][i],
				 rf_reg_mask_for_c_cut_2g[i], path, index,
				 rtl_get_rfreg(hw, (enum radio_path)path,
					       rf_reg_for_c_cut_2g[i],
					       BRFREGOFFSETMASK));
		}
		RTPRINT(rtlpriv, FINIT, INIT_IQK,
			"cosa ver 3 set RF-B, 2G, 0x28 = 0x%x !!\n",
			rf_syn_g4_for_c_cut_2g | (u4tmp << 11));

		rtl_set_rfreg(hw, (enum radio_path)path, RF_SYN_G4,
			      BRFREGOFFSETMASK,
			      rf_syn_g4_for_c_cut_2g | (u4tmp << 11));
		if (need_pwr_down)
			_rtl92d_phy_restore_rf_env(hw, path, &u4regvalue);
		if (rtlhal->during_mac0init_radiob)
			rtl92d_phy_powerdown_anotherphy(hw, true);
	}
	RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD, "<====\n");
}

u8 rtl92d_get_rightchnlplace_for_iqk(u8 chnl)
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

#define MAX_TOLERANCE		5
#define IQK_DELAY_TIME		1	/* ms */
#define MAX_TOLERANCE_92D	3

/* bit0 = 1 => Tx OK, bit1 = 1 => Rx OK */
static u8 _rtl92d_phy_patha_iqk(struct ieee80211_hw *hw, bool configpathb)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u32 regeac, rege94, rege9c, regea4;
	u8 result = 0;

	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "Path A IQK!\n");
	/* path-A IQK setting */
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "Path-A IQK setting!\n");
	if (rtlhal->interfaceindex == 0) {
		rtl_set_bbreg(hw, 0xe30, BMASKDWORD, 0x10008c1f);
		rtl_set_bbreg(hw, 0xe34, BMASKDWORD, 0x10008c1f);
	} else {
		rtl_set_bbreg(hw, 0xe30, BMASKDWORD, 0x10008c22);
		rtl_set_bbreg(hw, 0xe34, BMASKDWORD, 0x10008c22);
	}
	rtl_set_bbreg(hw, 0xe38, BMASKDWORD, 0x82140102);
	rtl_set_bbreg(hw, 0xe3c, BMASKDWORD, 0x28160206);
	/* path-B IQK setting */
	if (configpathb) {
		rtl_set_bbreg(hw, 0xe50, BMASKDWORD, 0x10008c22);
		rtl_set_bbreg(hw, 0xe54, BMASKDWORD, 0x10008c22);
		rtl_set_bbreg(hw, 0xe58, BMASKDWORD, 0x82140102);
		rtl_set_bbreg(hw, 0xe5c, BMASKDWORD, 0x28160206);
	}
	/* LO calibration setting */
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "LO calibration setting!\n");
	rtl_set_bbreg(hw, 0xe4c, BMASKDWORD, 0x00462911);
	/* One shot, path A LOK & IQK */
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "One shot, path A LOK & IQK!\n");
	rtl_set_bbreg(hw, 0xe48, BMASKDWORD, 0xf9000000);
	rtl_set_bbreg(hw, 0xe48, BMASKDWORD, 0xf8000000);
	/* delay x ms */
	RTPRINT(rtlpriv, FINIT, INIT_IQK,
		"Delay %d ms for One shot, path A LOK & IQK\n",
		IQK_DELAY_TIME);
	mdelay(IQK_DELAY_TIME);
	/* Check failed */
	regeac = rtl_get_bbreg(hw, 0xeac, BMASKDWORD);
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "0xeac = 0x%x\n", regeac);
	rege94 = rtl_get_bbreg(hw, 0xe94, BMASKDWORD);
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "0xe94 = 0x%x\n", rege94);
	rege9c = rtl_get_bbreg(hw, 0xe9c, BMASKDWORD);
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "0xe9c = 0x%x\n", rege9c);
	regea4 = rtl_get_bbreg(hw, 0xea4, BMASKDWORD);
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "0xea4 = 0x%x\n", regea4);
	if (!(regeac & BIT(28)) && (((rege94 & 0x03FF0000) >> 16) != 0x142) &&
	    (((rege9c & 0x03FF0000) >> 16) != 0x42))
		result |= 0x01;
	else			/* if Tx not OK, ignore Rx */
		return result;
	/* if Tx is OK, check whether Rx is OK */
	if (!(regeac & BIT(27)) && (((regea4 & 0x03FF0000) >> 16) != 0x132) &&
	    (((regeac & 0x03FF0000) >> 16) != 0x36))
		result |= 0x02;
	else
		RTPRINT(rtlpriv, FINIT, INIT_IQK,  "Path A Rx IQK fail!!\n");
	return result;
}

/* bit0 = 1 => Tx OK, bit1 = 1 => Rx OK */
static u8 _rtl92d_phy_patha_iqk_5g_normal(struct ieee80211_hw *hw,
					  bool configpathb)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u32 regeac, rege94, rege9c, regea4;
	u8 result = 0;
	u8 i;
	u8 retrycount = 2;
	u32 TxOKBit = BIT(28), RxOKBit = BIT(27);

	if (rtlhal->interfaceindex == 1) {	/* PHY1 */
		TxOKBit = BIT(31);
		RxOKBit = BIT(30);
	}
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "Path A IQK!\n");
	/* path-A IQK setting */
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "Path-A IQK setting!\n");
	rtl_set_bbreg(hw, 0xe30, BMASKDWORD, 0x18008c1f);
	rtl_set_bbreg(hw, 0xe34, BMASKDWORD, 0x18008c1f);
	rtl_set_bbreg(hw, 0xe38, BMASKDWORD, 0x82140307);
	rtl_set_bbreg(hw, 0xe3c, BMASKDWORD, 0x68160960);
	/* path-B IQK setting */
	if (configpathb) {
		rtl_set_bbreg(hw, 0xe50, BMASKDWORD, 0x18008c2f);
		rtl_set_bbreg(hw, 0xe54, BMASKDWORD, 0x18008c2f);
		rtl_set_bbreg(hw, 0xe58, BMASKDWORD, 0x82110000);
		rtl_set_bbreg(hw, 0xe5c, BMASKDWORD, 0x68110000);
	}
	/* LO calibration setting */
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "LO calibration setting!\n");
	rtl_set_bbreg(hw, 0xe4c, BMASKDWORD, 0x00462911);
	/* path-A PA on */
	rtl_set_bbreg(hw, RFPGA0_XAB_RFINTERFACESW, BMASKDWORD, 0x07000f60);
	rtl_set_bbreg(hw, RFPGA0_XA_RFINTERFACEOE, BMASKDWORD, 0x66e60e30);
	for (i = 0; i < retrycount; i++) {
		/* One shot, path A LOK & IQK */
		RTPRINT(rtlpriv, FINIT, INIT_IQK,
			"One shot, path A LOK & IQK!\n");
		rtl_set_bbreg(hw, 0xe48, BMASKDWORD, 0xf9000000);
		rtl_set_bbreg(hw, 0xe48, BMASKDWORD, 0xf8000000);
		/* delay x ms */
		RTPRINT(rtlpriv, FINIT, INIT_IQK,
			"Delay %d ms for One shot, path A LOK & IQK.\n",
			IQK_DELAY_TIME);
		mdelay(IQK_DELAY_TIME * 10);
		/* Check failed */
		regeac = rtl_get_bbreg(hw, 0xeac, BMASKDWORD);
		RTPRINT(rtlpriv, FINIT, INIT_IQK,  "0xeac = 0x%x\n", regeac);
		rege94 = rtl_get_bbreg(hw, 0xe94, BMASKDWORD);
		RTPRINT(rtlpriv, FINIT, INIT_IQK,  "0xe94 = 0x%x\n", rege94);
		rege9c = rtl_get_bbreg(hw, 0xe9c, BMASKDWORD);
		RTPRINT(rtlpriv, FINIT, INIT_IQK,  "0xe9c = 0x%x\n", rege9c);
		regea4 = rtl_get_bbreg(hw, 0xea4, BMASKDWORD);
		RTPRINT(rtlpriv, FINIT, INIT_IQK,  "0xea4 = 0x%x\n", regea4);
		if (!(regeac & TxOKBit) &&
		     (((rege94 & 0x03FF0000) >> 16) != 0x142)) {
			result |= 0x01;
		} else { /* if Tx not OK, ignore Rx */
			RTPRINT(rtlpriv, FINIT, INIT_IQK,
				"Path A Tx IQK fail!!\n");
			continue;
		}

		/* if Tx is OK, check whether Rx is OK */
		if (!(regeac & RxOKBit) &&
		    (((regea4 & 0x03FF0000) >> 16) != 0x132)) {
			result |= 0x02;
			break;
		} else {
			RTPRINT(rtlpriv, FINIT, INIT_IQK,
				"Path A Rx IQK fail!!\n");
		}
	}
	/* path A PA off */
	rtl_set_bbreg(hw, RFPGA0_XAB_RFINTERFACESW, BMASKDWORD,
		      rtlphy->iqk_bb_backup[0]);
	rtl_set_bbreg(hw, RFPGA0_XA_RFINTERFACEOE, BMASKDWORD,
		      rtlphy->iqk_bb_backup[1]);
	return result;
}

/* bit0 = 1 => Tx OK, bit1 = 1 => Rx OK */
static u8 _rtl92d_phy_pathb_iqk(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 regeac, regeb4, regebc, regec4, regecc;
	u8 result = 0;

	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "Path B IQK!\n");
	/* One shot, path B LOK & IQK */
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "One shot, path A LOK & IQK!\n");
	rtl_set_bbreg(hw, 0xe60, BMASKDWORD, 0x00000002);
	rtl_set_bbreg(hw, 0xe60, BMASKDWORD, 0x00000000);
	/* delay x ms  */
	RTPRINT(rtlpriv, FINIT, INIT_IQK,
		"Delay %d ms for One shot, path B LOK & IQK\n", IQK_DELAY_TIME);
	mdelay(IQK_DELAY_TIME);
	/* Check failed */
	regeac = rtl_get_bbreg(hw, 0xeac, BMASKDWORD);
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "0xeac = 0x%x\n", regeac);
	regeb4 = rtl_get_bbreg(hw, 0xeb4, BMASKDWORD);
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "0xeb4 = 0x%x\n", regeb4);
	regebc = rtl_get_bbreg(hw, 0xebc, BMASKDWORD);
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "0xebc = 0x%x\n", regebc);
	regec4 = rtl_get_bbreg(hw, 0xec4, BMASKDWORD);
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "0xec4 = 0x%x\n", regec4);
	regecc = rtl_get_bbreg(hw, 0xecc, BMASKDWORD);
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "0xecc = 0x%x\n", regecc);
	if (!(regeac & BIT(31)) && (((regeb4 & 0x03FF0000) >> 16) != 0x142) &&
	    (((regebc & 0x03FF0000) >> 16) != 0x42))
		result |= 0x01;
	else
		return result;
	if (!(regeac & BIT(30)) && (((regec4 & 0x03FF0000) >> 16) != 0x132) &&
	    (((regecc & 0x03FF0000) >> 16) != 0x36))
		result |= 0x02;
	else
		RTPRINT(rtlpriv, FINIT, INIT_IQK,  "Path B Rx IQK fail!!\n");
	return result;
}

/* bit0 = 1 => Tx OK, bit1 = 1 => Rx OK */
static u8 _rtl92d_phy_pathb_iqk_5g_normal(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u32 regeac, regeb4, regebc, regec4, regecc;
	u8 result = 0;
	u8 i;
	u8 retrycount = 2;

	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "Path B IQK!\n");
	/* path-A IQK setting */
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "Path-A IQK setting!\n");
	rtl_set_bbreg(hw, 0xe30, BMASKDWORD, 0x18008c1f);
	rtl_set_bbreg(hw, 0xe34, BMASKDWORD, 0x18008c1f);
	rtl_set_bbreg(hw, 0xe38, BMASKDWORD, 0x82110000);
	rtl_set_bbreg(hw, 0xe3c, BMASKDWORD, 0x68110000);

	/* path-B IQK setting */
	rtl_set_bbreg(hw, 0xe50, BMASKDWORD, 0x18008c2f);
	rtl_set_bbreg(hw, 0xe54, BMASKDWORD, 0x18008c2f);
	rtl_set_bbreg(hw, 0xe58, BMASKDWORD, 0x82140307);
	rtl_set_bbreg(hw, 0xe5c, BMASKDWORD, 0x68160960);

	/* LO calibration setting */
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "LO calibration setting!\n");
	rtl_set_bbreg(hw, 0xe4c, BMASKDWORD, 0x00462911);

	/* path-B PA on */
	rtl_set_bbreg(hw, RFPGA0_XAB_RFINTERFACESW, BMASKDWORD, 0x0f600700);
	rtl_set_bbreg(hw, RFPGA0_XB_RFINTERFACEOE, BMASKDWORD, 0x061f0d30);

	for (i = 0; i < retrycount; i++) {
		/* One shot, path B LOK & IQK */
		RTPRINT(rtlpriv, FINIT, INIT_IQK,
			"One shot, path A LOK & IQK!\n");
		rtl_set_bbreg(hw, 0xe48, BMASKDWORD, 0xfa000000);
		rtl_set_bbreg(hw, 0xe48, BMASKDWORD, 0xf8000000);

		/* delay x ms */
		RTPRINT(rtlpriv, FINIT, INIT_IQK,
			"Delay %d ms for One shot, path B LOK & IQK.\n", 10);
		mdelay(IQK_DELAY_TIME * 10);

		/* Check failed */
		regeac = rtl_get_bbreg(hw, 0xeac, BMASKDWORD);
		RTPRINT(rtlpriv, FINIT, INIT_IQK,  "0xeac = 0x%x\n", regeac);
		regeb4 = rtl_get_bbreg(hw, 0xeb4, BMASKDWORD);
		RTPRINT(rtlpriv, FINIT, INIT_IQK,  "0xeb4 = 0x%x\n", regeb4);
		regebc = rtl_get_bbreg(hw, 0xebc, BMASKDWORD);
		RTPRINT(rtlpriv, FINIT, INIT_IQK,  "0xebc = 0x%x\n", regebc);
		regec4 = rtl_get_bbreg(hw, 0xec4, BMASKDWORD);
		RTPRINT(rtlpriv, FINIT, INIT_IQK,  "0xec4 = 0x%x\n", regec4);
		regecc = rtl_get_bbreg(hw, 0xecc, BMASKDWORD);
		RTPRINT(rtlpriv, FINIT, INIT_IQK,  "0xecc = 0x%x\n", regecc);
		if (!(regeac & BIT(31)) &&
		    (((regeb4 & 0x03FF0000) >> 16) != 0x142))
			result |= 0x01;
		else
			continue;
		if (!(regeac & BIT(30)) &&
		    (((regec4 & 0x03FF0000) >> 16) != 0x132)) {
			result |= 0x02;
			break;
		} else {
			RTPRINT(rtlpriv, FINIT, INIT_IQK,
				"Path B Rx IQK fail!!\n");
		}
	}

	/* path B PA off */
	rtl_set_bbreg(hw, RFPGA0_XAB_RFINTERFACESW, BMASKDWORD,
		      rtlphy->iqk_bb_backup[0]);
	rtl_set_bbreg(hw, RFPGA0_XB_RFINTERFACEOE, BMASKDWORD,
		      rtlphy->iqk_bb_backup[2]);
	return result;
}

static void _rtl92d_phy_save_adda_registers(struct ieee80211_hw *hw,
					    u32 *adda_reg, u32 *adda_backup,
					    u32 regnum)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 i;

	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "Save ADDA parameters.\n");
	for (i = 0; i < regnum; i++)
		adda_backup[i] = rtl_get_bbreg(hw, adda_reg[i], BMASKDWORD);
}

static void _rtl92d_phy_save_mac_registers(struct ieee80211_hw *hw,
	u32 *macreg, u32 *macbackup)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 i;

	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "Save MAC parameters.\n");
	for (i = 0; i < (IQK_MAC_REG_NUM - 1); i++)
		macbackup[i] = rtl_read_byte(rtlpriv, macreg[i]);
	macbackup[i] = rtl_read_dword(rtlpriv, macreg[i]);
}

static void _rtl92d_phy_reload_adda_registers(struct ieee80211_hw *hw,
					      u32 *adda_reg, u32 *adda_backup,
					      u32 regnum)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 i;

	RTPRINT(rtlpriv, FINIT, INIT_IQK,
		"Reload ADDA power saving parameters !\n");
	for (i = 0; i < regnum; i++)
		rtl_set_bbreg(hw, adda_reg[i], BMASKDWORD, adda_backup[i]);
}

static void _rtl92d_phy_reload_mac_registers(struct ieee80211_hw *hw,
					     u32 *macreg, u32 *macbackup)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 i;

	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "Reload MAC parameters !\n");
	for (i = 0; i < (IQK_MAC_REG_NUM - 1); i++)
		rtl_write_byte(rtlpriv, macreg[i], (u8) macbackup[i]);
	rtl_write_byte(rtlpriv, macreg[i], macbackup[i]);
}

static void _rtl92d_phy_path_adda_on(struct ieee80211_hw *hw,
		u32 *adda_reg, bool patha_on, bool is2t)
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
		rtl_set_bbreg(hw, adda_reg[i], BMASKDWORD, pathon);
}

static void _rtl92d_phy_mac_setting_calibration(struct ieee80211_hw *hw,
						u32 *macreg, u32 *macbackup)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 i;

	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "MAC settings for Calibration.\n");
	rtl_write_byte(rtlpriv, macreg[0], 0x3F);

	for (i = 1; i < (IQK_MAC_REG_NUM - 1); i++)
		rtl_write_byte(rtlpriv, macreg[i], (u8)(macbackup[i] &
			       (~BIT(3))));
	rtl_write_byte(rtlpriv, macreg[i], (u8) (macbackup[i] & (~BIT(5))));
}

static void _rtl92d_phy_patha_standby(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "Path-A standby mode!\n");

	rtl_set_bbreg(hw, 0xe28, BMASKDWORD, 0x0);
	rtl_set_bbreg(hw, RFPGA0_XA_LSSIPARAMETER, BMASKDWORD, 0x00010000);
	rtl_set_bbreg(hw, 0xe28, BMASKDWORD, 0x80800000);
}

static void _rtl92d_phy_pimode_switch(struct ieee80211_hw *hw, bool pi_mode)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 mode;

	RTPRINT(rtlpriv, FINIT, INIT_IQK,
		"BB Switch to %s mode!\n", pi_mode ? "PI" : "SI");
	mode = pi_mode ? 0x01000100 : 0x01000000;
	rtl_set_bbreg(hw, 0x820, BMASKDWORD, mode);
	rtl_set_bbreg(hw, 0x828, BMASKDWORD, mode);
}

static void _rtl92d_phy_iq_calibrate(struct ieee80211_hw *hw, long result[][8],
				     u8 t, bool is2t)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u32 i;
	u8 patha_ok, pathb_ok;
	static u32 adda_reg[IQK_ADDA_REG_NUM] = {
		RFPGA0_XCD_SWITCHCONTROL, 0xe6c, 0xe70, 0xe74,
		0xe78, 0xe7c, 0xe80, 0xe84,
		0xe88, 0xe8c, 0xed0, 0xed4,
		0xed8, 0xedc, 0xee0, 0xeec
	};
	static u32 iqk_mac_reg[IQK_MAC_REG_NUM] = {
		0x522, 0x550, 0x551, 0x040
	};
	static u32 iqk_bb_reg[IQK_BB_REG_NUM] = {
		RFPGA0_XAB_RFINTERFACESW, RFPGA0_XA_RFINTERFACEOE,
		RFPGA0_XB_RFINTERFACEOE, ROFDM0_TRMUXPAR,
		RFPGA0_XCD_RFINTERFACESW, ROFDM0_TRXPATHENABLE,
		RFPGA0_RFMOD, RFPGA0_ANALOGPARAMETER4,
		ROFDM0_XAAGCCORE1, ROFDM0_XBAGCCORE1
	};
	const u32 retrycount = 2;
	u32 bbvalue;

	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "IQK for 2.4G :Start!!!\n");
	if (t == 0) {
		bbvalue = rtl_get_bbreg(hw, RFPGA0_RFMOD, BMASKDWORD);
		RTPRINT(rtlpriv, FINIT, INIT_IQK,  "==>0x%08x\n", bbvalue);
		RTPRINT(rtlpriv, FINIT, INIT_IQK, "IQ Calibration for %s\n",
			is2t ? "2T2R" : "1T1R");

		/*  Save ADDA parameters, turn Path A ADDA on */
		_rtl92d_phy_save_adda_registers(hw, adda_reg,
			rtlphy->adda_backup, IQK_ADDA_REG_NUM);
		_rtl92d_phy_save_mac_registers(hw, iqk_mac_reg,
			rtlphy->iqk_mac_backup);
		_rtl92d_phy_save_adda_registers(hw, iqk_bb_reg,
			rtlphy->iqk_bb_backup, IQK_BB_REG_NUM);
	}
	_rtl92d_phy_path_adda_on(hw, adda_reg, true, is2t);
	if (t == 0)
		rtlphy->rfpi_enable = (u8) rtl_get_bbreg(hw,
				RFPGA0_XA_HSSIPARAMETER1, BIT(8));

	/*  Switch BB to PI mode to do IQ Calibration. */
	if (!rtlphy->rfpi_enable)
		_rtl92d_phy_pimode_switch(hw, true);

	rtl_set_bbreg(hw, RFPGA0_RFMOD, BIT(24), 0x00);
	rtl_set_bbreg(hw, ROFDM0_TRXPATHENABLE, BMASKDWORD, 0x03a05600);
	rtl_set_bbreg(hw, ROFDM0_TRMUXPAR, BMASKDWORD, 0x000800e4);
	rtl_set_bbreg(hw, RFPGA0_XCD_RFINTERFACESW, BMASKDWORD, 0x22204000);
	rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER4, 0xf00000, 0x0f);
	if (is2t) {
		rtl_set_bbreg(hw, RFPGA0_XA_LSSIPARAMETER, BMASKDWORD,
			      0x00010000);
		rtl_set_bbreg(hw, RFPGA0_XB_LSSIPARAMETER, BMASKDWORD,
			      0x00010000);
	}
	/* MAC settings */
	_rtl92d_phy_mac_setting_calibration(hw, iqk_mac_reg,
					    rtlphy->iqk_mac_backup);
	/* Page B init */
	rtl_set_bbreg(hw, 0xb68, BMASKDWORD, 0x0f600000);
	if (is2t)
		rtl_set_bbreg(hw, 0xb6c, BMASKDWORD, 0x0f600000);
	/* IQ calibration setting */
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "IQK setting!\n");
	rtl_set_bbreg(hw, 0xe28, BMASKDWORD, 0x80800000);
	rtl_set_bbreg(hw, 0xe40, BMASKDWORD, 0x01007c00);
	rtl_set_bbreg(hw, 0xe44, BMASKDWORD, 0x01004800);
	for (i = 0; i < retrycount; i++) {
		patha_ok = _rtl92d_phy_patha_iqk(hw, is2t);
		if (patha_ok == 0x03) {
			RTPRINT(rtlpriv, FINIT, INIT_IQK,
				"Path A IQK Success!!\n");
			result[t][0] = (rtl_get_bbreg(hw, 0xe94, BMASKDWORD) &
					0x3FF0000) >> 16;
			result[t][1] = (rtl_get_bbreg(hw, 0xe9c, BMASKDWORD) &
					0x3FF0000) >> 16;
			result[t][2] = (rtl_get_bbreg(hw, 0xea4, BMASKDWORD) &
					0x3FF0000) >> 16;
			result[t][3] = (rtl_get_bbreg(hw, 0xeac, BMASKDWORD) &
					0x3FF0000) >> 16;
			break;
		} else if (i == (retrycount - 1) && patha_ok == 0x01) {
			/* Tx IQK OK */
			RTPRINT(rtlpriv, FINIT, INIT_IQK,
				"Path A IQK Only  Tx Success!!\n");

			result[t][0] = (rtl_get_bbreg(hw, 0xe94, BMASKDWORD) &
					0x3FF0000) >> 16;
			result[t][1] = (rtl_get_bbreg(hw, 0xe9c, BMASKDWORD) &
					0x3FF0000) >> 16;
		}
	}
	if (0x00 == patha_ok)
		RTPRINT(rtlpriv, FINIT, INIT_IQK,  "Path A IQK failed!!\n");
	if (is2t) {
		_rtl92d_phy_patha_standby(hw);
		/* Turn Path B ADDA on */
		_rtl92d_phy_path_adda_on(hw, adda_reg, false, is2t);
		for (i = 0; i < retrycount; i++) {
			pathb_ok = _rtl92d_phy_pathb_iqk(hw);
			if (pathb_ok == 0x03) {
				RTPRINT(rtlpriv, FINIT, INIT_IQK,
					"Path B IQK Success!!\n");
				result[t][4] = (rtl_get_bbreg(hw, 0xeb4,
					       BMASKDWORD) & 0x3FF0000) >> 16;
				result[t][5] = (rtl_get_bbreg(hw, 0xebc,
					       BMASKDWORD) & 0x3FF0000) >> 16;
				result[t][6] = (rtl_get_bbreg(hw, 0xec4,
					       BMASKDWORD) & 0x3FF0000) >> 16;
				result[t][7] = (rtl_get_bbreg(hw, 0xecc,
					       BMASKDWORD) & 0x3FF0000) >> 16;
				break;
			} else if (i == (retrycount - 1) && pathb_ok == 0x01) {
				/* Tx IQK OK */
				RTPRINT(rtlpriv, FINIT, INIT_IQK,
					"Path B Only Tx IQK Success!!\n");
				result[t][4] = (rtl_get_bbreg(hw, 0xeb4,
					       BMASKDWORD) & 0x3FF0000) >> 16;
				result[t][5] = (rtl_get_bbreg(hw, 0xebc,
					       BMASKDWORD) & 0x3FF0000) >> 16;
			}
		}
		if (0x00 == pathb_ok)
			RTPRINT(rtlpriv, FINIT, INIT_IQK,
				"Path B IQK failed!!\n");
	}

	/* Back to BB mode, load original value */
	RTPRINT(rtlpriv, FINIT, INIT_IQK,
		"IQK:Back to BB mode, load original value!\n");

	rtl_set_bbreg(hw, 0xe28, BMASKDWORD, 0);
	if (t != 0) {
		/* Switch back BB to SI mode after finish IQ Calibration. */
		if (!rtlphy->rfpi_enable)
			_rtl92d_phy_pimode_switch(hw, false);
		/* Reload ADDA power saving parameters */
		_rtl92d_phy_reload_adda_registers(hw, adda_reg,
				rtlphy->adda_backup, IQK_ADDA_REG_NUM);
		/* Reload MAC parameters */
		_rtl92d_phy_reload_mac_registers(hw, iqk_mac_reg,
					rtlphy->iqk_mac_backup);
		if (is2t)
			_rtl92d_phy_reload_adda_registers(hw, iqk_bb_reg,
							  rtlphy->iqk_bb_backup,
							  IQK_BB_REG_NUM);
		else
			_rtl92d_phy_reload_adda_registers(hw, iqk_bb_reg,
							  rtlphy->iqk_bb_backup,
							  IQK_BB_REG_NUM - 1);
		/* load 0xe30 IQC default value */
		rtl_set_bbreg(hw, 0xe30, BMASKDWORD, 0x01008c00);
		rtl_set_bbreg(hw, 0xe34, BMASKDWORD, 0x01008c00);
	}
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "<==\n");
}

static void _rtl92d_phy_iq_calibrate_5g_normal(struct ieee80211_hw *hw,
					       long result[][8], u8 t)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_hal *rtlhal = &(rtlpriv->rtlhal);
	u8 patha_ok, pathb_ok;
	static u32 adda_reg[IQK_ADDA_REG_NUM] = {
		RFPGA0_XCD_SWITCHCONTROL, 0xe6c, 0xe70, 0xe74,
		0xe78, 0xe7c, 0xe80, 0xe84,
		0xe88, 0xe8c, 0xed0, 0xed4,
		0xed8, 0xedc, 0xee0, 0xeec
	};
	static u32 iqk_mac_reg[IQK_MAC_REG_NUM] = {
		0x522, 0x550, 0x551, 0x040
	};
	static u32 iqk_bb_reg[IQK_BB_REG_NUM] = {
		RFPGA0_XAB_RFINTERFACESW, RFPGA0_XA_RFINTERFACEOE,
		RFPGA0_XB_RFINTERFACEOE, ROFDM0_TRMUXPAR,
		RFPGA0_XCD_RFINTERFACESW, ROFDM0_TRXPATHENABLE,
		RFPGA0_RFMOD, RFPGA0_ANALOGPARAMETER4,
		ROFDM0_XAAGCCORE1, ROFDM0_XBAGCCORE1
	};
	u32 bbvalue;
	bool is2t = IS_92D_SINGLEPHY(rtlhal->version);

	/* Note: IQ calibration must be performed after loading
	 * PHY_REG.txt , and radio_a, radio_b.txt */

	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "IQK for 5G NORMAL:Start!!!\n");
	mdelay(IQK_DELAY_TIME * 20);
	if (t == 0) {
		bbvalue = rtl_get_bbreg(hw, RFPGA0_RFMOD, BMASKDWORD);
		RTPRINT(rtlpriv, FINIT, INIT_IQK,  "==>0x%08x\n", bbvalue);
		RTPRINT(rtlpriv, FINIT, INIT_IQK, "IQ Calibration for %s\n",
			is2t ? "2T2R" : "1T1R");
		/* Save ADDA parameters, turn Path A ADDA on */
		_rtl92d_phy_save_adda_registers(hw, adda_reg,
						rtlphy->adda_backup,
						IQK_ADDA_REG_NUM);
		_rtl92d_phy_save_mac_registers(hw, iqk_mac_reg,
					       rtlphy->iqk_mac_backup);
		if (is2t)
			_rtl92d_phy_save_adda_registers(hw, iqk_bb_reg,
							rtlphy->iqk_bb_backup,
							IQK_BB_REG_NUM);
		else
			_rtl92d_phy_save_adda_registers(hw, iqk_bb_reg,
							rtlphy->iqk_bb_backup,
							IQK_BB_REG_NUM - 1);
	}
	_rtl92d_phy_path_adda_on(hw, adda_reg, true, is2t);
	/* MAC settings */
	_rtl92d_phy_mac_setting_calibration(hw, iqk_mac_reg,
			rtlphy->iqk_mac_backup);
	if (t == 0)
		rtlphy->rfpi_enable = (u8) rtl_get_bbreg(hw,
			RFPGA0_XA_HSSIPARAMETER1, BIT(8));
	/*  Switch BB to PI mode to do IQ Calibration. */
	if (!rtlphy->rfpi_enable)
		_rtl92d_phy_pimode_switch(hw, true);
	rtl_set_bbreg(hw, RFPGA0_RFMOD, BIT(24), 0x00);
	rtl_set_bbreg(hw, ROFDM0_TRXPATHENABLE, BMASKDWORD, 0x03a05600);
	rtl_set_bbreg(hw, ROFDM0_TRMUXPAR, BMASKDWORD, 0x000800e4);
	rtl_set_bbreg(hw, RFPGA0_XCD_RFINTERFACESW, BMASKDWORD, 0x22208000);
	rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER4, 0xf00000, 0x0f);

	/* Page B init */
	rtl_set_bbreg(hw, 0xb68, BMASKDWORD, 0x0f600000);
	if (is2t)
		rtl_set_bbreg(hw, 0xb6c, BMASKDWORD, 0x0f600000);
	/* IQ calibration setting  */
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "IQK setting!\n");
	rtl_set_bbreg(hw, 0xe28, BMASKDWORD, 0x80800000);
	rtl_set_bbreg(hw, 0xe40, BMASKDWORD, 0x10007c00);
	rtl_set_bbreg(hw, 0xe44, BMASKDWORD, 0x01004800);
	patha_ok = _rtl92d_phy_patha_iqk_5g_normal(hw, is2t);
	if (patha_ok == 0x03) {
		RTPRINT(rtlpriv, FINIT, INIT_IQK,  "Path A IQK Success!!\n");
		result[t][0] = (rtl_get_bbreg(hw, 0xe94, BMASKDWORD) &
				0x3FF0000) >> 16;
		result[t][1] = (rtl_get_bbreg(hw, 0xe9c, BMASKDWORD) &
				0x3FF0000) >> 16;
		result[t][2] = (rtl_get_bbreg(hw, 0xea4, BMASKDWORD) &
				0x3FF0000) >> 16;
		result[t][3] = (rtl_get_bbreg(hw, 0xeac, BMASKDWORD) &
				0x3FF0000) >> 16;
	} else if (patha_ok == 0x01) {	/* Tx IQK OK */
		RTPRINT(rtlpriv, FINIT, INIT_IQK,
			"Path A IQK Only  Tx Success!!\n");

		result[t][0] = (rtl_get_bbreg(hw, 0xe94, BMASKDWORD) &
				0x3FF0000) >> 16;
		result[t][1] = (rtl_get_bbreg(hw, 0xe9c, BMASKDWORD) &
				0x3FF0000) >> 16;
	} else {
		RTPRINT(rtlpriv, FINIT, INIT_IQK,  "Path A IQK Fail!!\n");
	}
	if (is2t) {
		/* _rtl92d_phy_patha_standby(hw); */
		/* Turn Path B ADDA on  */
		_rtl92d_phy_path_adda_on(hw, adda_reg, false, is2t);
		pathb_ok = _rtl92d_phy_pathb_iqk_5g_normal(hw);
		if (pathb_ok == 0x03) {
			RTPRINT(rtlpriv, FINIT, INIT_IQK,
				"Path B IQK Success!!\n");
			result[t][4] = (rtl_get_bbreg(hw, 0xeb4, BMASKDWORD) &
			     0x3FF0000) >> 16;
			result[t][5] = (rtl_get_bbreg(hw, 0xebc, BMASKDWORD) &
			     0x3FF0000) >> 16;
			result[t][6] = (rtl_get_bbreg(hw, 0xec4, BMASKDWORD) &
			     0x3FF0000) >> 16;
			result[t][7] = (rtl_get_bbreg(hw, 0xecc, BMASKDWORD) &
			     0x3FF0000) >> 16;
		} else if (pathb_ok == 0x01) { /* Tx IQK OK */
			RTPRINT(rtlpriv, FINIT, INIT_IQK,
				"Path B Only Tx IQK Success!!\n");
			result[t][4] = (rtl_get_bbreg(hw, 0xeb4, BMASKDWORD) &
			     0x3FF0000) >> 16;
			result[t][5] = (rtl_get_bbreg(hw, 0xebc, BMASKDWORD) &
			     0x3FF0000) >> 16;
		} else {
			RTPRINT(rtlpriv, FINIT, INIT_IQK,
				"Path B IQK failed!!\n");
		}
	}

	/* Back to BB mode, load original value */
	RTPRINT(rtlpriv, FINIT, INIT_IQK,
		"IQK:Back to BB mode, load original value!\n");
	rtl_set_bbreg(hw, 0xe28, BMASKDWORD, 0);
	if (t != 0) {
		if (is2t)
			_rtl92d_phy_reload_adda_registers(hw, iqk_bb_reg,
							  rtlphy->iqk_bb_backup,
							  IQK_BB_REG_NUM);
		else
			_rtl92d_phy_reload_adda_registers(hw, iqk_bb_reg,
							  rtlphy->iqk_bb_backup,
							  IQK_BB_REG_NUM - 1);
		/* Reload MAC parameters */
		_rtl92d_phy_reload_mac_registers(hw, iqk_mac_reg,
				rtlphy->iqk_mac_backup);
		/*  Switch back BB to SI mode after finish IQ Calibration. */
		if (!rtlphy->rfpi_enable)
			_rtl92d_phy_pimode_switch(hw, false);
		/* Reload ADDA power saving parameters */
		_rtl92d_phy_reload_adda_registers(hw, adda_reg,
						  rtlphy->adda_backup,
						  IQK_ADDA_REG_NUM);
	}
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "<==\n");
}

static bool _rtl92d_phy_simularity_compare(struct ieee80211_hw *hw,
	long result[][8], u8 c1, u8 c2)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = &(rtlpriv->rtlhal);
	u32 i, j, diff, sim_bitmap, bound;
	u8 final_candidate[2] = {0xFF, 0xFF};	/* for path A and path B */
	bool bresult = true;
	bool is2t = IS_92D_SINGLEPHY(rtlhal->version);

	if (is2t)
		bound = 8;
	else
		bound = 4;
	sim_bitmap = 0;
	for (i = 0; i < bound; i++) {
		diff = (result[c1][i] > result[c2][i]) ? (result[c1][i] -
		       result[c2][i]) : (result[c2][i] - result[c1][i]);
		if (diff > MAX_TOLERANCE_92D) {
			if ((i == 2 || i == 6) && !sim_bitmap) {
				if (result[c1][i] + result[c1][i + 1] == 0)
					final_candidate[(i / 4)] = c2;
				else if (result[c2][i] + result[c2][i + 1] == 0)
					final_candidate[(i / 4)] = c1;
				else
					sim_bitmap = sim_bitmap | (1 << i);
			} else {
				sim_bitmap = sim_bitmap | (1 << i);
			}
		}
	}
	if (sim_bitmap == 0) {
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
	if (!(sim_bitmap & 0x0F)) { /* path A OK */
		for (i = 0; i < 4; i++)
			result[3][i] = result[c1][i];
	} else if (!(sim_bitmap & 0x03)) { /* path A, Tx OK */
		for (i = 0; i < 2; i++)
			result[3][i] = result[c1][i];
	}
	if (!(sim_bitmap & 0xF0) && is2t) { /* path B OK */
		for (i = 4; i < 8; i++)
			result[3][i] = result[c1][i];
	} else if (!(sim_bitmap & 0x30)) { /* path B, Tx OK */
		for (i = 4; i < 6; i++)
			result[3][i] = result[c1][i];
	}
	return false;
}

static void _rtl92d_phy_patha_fill_iqk_matrix(struct ieee80211_hw *hw,
					      bool iqk_ok, long result[][8],
					      u8 final_candidate, bool txonly)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = &(rtlpriv->rtlhal);
	u32 oldval_0, val_x, tx0_a, reg;
	long val_y, tx0_c;
	bool is2t = IS_92D_SINGLEPHY(rtlhal->version) ||
	    rtlhal->macphymode == DUALMAC_DUALPHY;

	RTPRINT(rtlpriv, FINIT, INIT_IQK,
		"Path A IQ Calibration %s !\n", iqk_ok ? "Success" : "Failed");
	if (final_candidate == 0xFF) {
		return;
	} else if (iqk_ok) {
		oldval_0 = (rtl_get_bbreg(hw, ROFDM0_XATxIQIMBALANCE,
			BMASKDWORD) >> 22) & 0x3FF;	/* OFDM0_D */
		val_x = result[final_candidate][0];
		if ((val_x & 0x00000200) != 0)
			val_x = val_x | 0xFFFFFC00;
		tx0_a = (val_x * oldval_0) >> 8;
		RTPRINT(rtlpriv, FINIT, INIT_IQK,
			"X = 0x%x, tx0_a = 0x%x, oldval_0 0x%x\n",
			val_x, tx0_a, oldval_0);
		rtl_set_bbreg(hw, ROFDM0_XATxIQIMBALANCE, 0x3FF, tx0_a);
		rtl_set_bbreg(hw, ROFDM0_ECCATHRESHOLD, BIT(24),
			      ((val_x * oldval_0 >> 7) & 0x1));
		val_y = result[final_candidate][1];
		if ((val_y & 0x00000200) != 0)
			val_y = val_y | 0xFFFFFC00;
		/* path B IQK result + 3 */
		if (rtlhal->interfaceindex == 1 &&
			rtlhal->current_bandtype == BAND_ON_5G)
			val_y += 3;
		tx0_c = (val_y * oldval_0) >> 8;
		RTPRINT(rtlpriv, FINIT, INIT_IQK,
			"Y = 0x%lx, tx0_c = 0x%lx\n",
			val_y, tx0_c);
		rtl_set_bbreg(hw, ROFDM0_XCTxAFE, 0xF0000000,
			      ((tx0_c & 0x3C0) >> 6));
		rtl_set_bbreg(hw, ROFDM0_XATxIQIMBALANCE, 0x003F0000,
			      (tx0_c & 0x3F));
		if (is2t)
			rtl_set_bbreg(hw, ROFDM0_ECCATHRESHOLD, BIT(26),
				      ((val_y * oldval_0 >> 7) & 0x1));
		RTPRINT(rtlpriv, FINIT, INIT_IQK, "0xC80 = 0x%x\n",
			rtl_get_bbreg(hw, ROFDM0_XATxIQIMBALANCE,
				      BMASKDWORD));
		if (txonly) {
			RTPRINT(rtlpriv, FINIT, INIT_IQK,  "only Tx OK\n");
			return;
		}
		reg = result[final_candidate][2];
		rtl_set_bbreg(hw, ROFDM0_XARXIQIMBALANCE, 0x3FF, reg);
		reg = result[final_candidate][3] & 0x3F;
		rtl_set_bbreg(hw, ROFDM0_XARXIQIMBALANCE, 0xFC00, reg);
		reg = (result[final_candidate][3] >> 6) & 0xF;
		rtl_set_bbreg(hw, 0xca0, 0xF0000000, reg);
	}
}

static void _rtl92d_phy_pathb_fill_iqk_matrix(struct ieee80211_hw *hw,
	bool iqk_ok, long result[][8], u8 final_candidate, bool txonly)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = &(rtlpriv->rtlhal);
	u32 oldval_1, val_x, tx1_a, reg;
	long val_y, tx1_c;

	RTPRINT(rtlpriv, FINIT, INIT_IQK, "Path B IQ Calibration %s !\n",
		iqk_ok ? "Success" : "Failed");
	if (final_candidate == 0xFF) {
		return;
	} else if (iqk_ok) {
		oldval_1 = (rtl_get_bbreg(hw, ROFDM0_XBTxIQIMBALANCE,
					  BMASKDWORD) >> 22) & 0x3FF;
		val_x = result[final_candidate][4];
		if ((val_x & 0x00000200) != 0)
			val_x = val_x | 0xFFFFFC00;
		tx1_a = (val_x * oldval_1) >> 8;
		RTPRINT(rtlpriv, FINIT, INIT_IQK, "X = 0x%x, tx1_a = 0x%x\n",
			val_x, tx1_a);
		rtl_set_bbreg(hw, ROFDM0_XBTxIQIMBALANCE, 0x3FF, tx1_a);
		rtl_set_bbreg(hw, ROFDM0_ECCATHRESHOLD, BIT(28),
			      ((val_x * oldval_1 >> 7) & 0x1));
		val_y = result[final_candidate][5];
		if ((val_y & 0x00000200) != 0)
			val_y = val_y | 0xFFFFFC00;
		if (rtlhal->current_bandtype == BAND_ON_5G)
			val_y += 3;
		tx1_c = (val_y * oldval_1) >> 8;
		RTPRINT(rtlpriv, FINIT, INIT_IQK, "Y = 0x%lx, tx1_c = 0x%lx\n",
			val_y, tx1_c);
		rtl_set_bbreg(hw, ROFDM0_XDTxAFE, 0xF0000000,
			      ((tx1_c & 0x3C0) >> 6));
		rtl_set_bbreg(hw, ROFDM0_XBTxIQIMBALANCE, 0x003F0000,
			      (tx1_c & 0x3F));
		rtl_set_bbreg(hw, ROFDM0_ECCATHRESHOLD, BIT(30),
			      ((val_y * oldval_1 >> 7) & 0x1));
		if (txonly)
			return;
		reg = result[final_candidate][6];
		rtl_set_bbreg(hw, ROFDM0_XBRXIQIMBALANCE, 0x3FF, reg);
		reg = result[final_candidate][7] & 0x3F;
		rtl_set_bbreg(hw, ROFDM0_XBRXIQIMBALANCE, 0xFC00, reg);
		reg = (result[final_candidate][7] >> 6) & 0xF;
		rtl_set_bbreg(hw, ROFDM0_AGCRSSITABLE, 0x0000F000, reg);
	}
}

void rtl92d_phy_iq_calibrate(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_hal *rtlhal = &(rtlpriv->rtlhal);
	long result[4][8];
	u8 i, final_candidate, indexforchannel;
	bool patha_ok, pathb_ok;
	long rege94, rege9c, regea4, regeac, regeb4;
	long regebc, regec4, regecc, regtmp = 0;
	bool is12simular, is13simular, is23simular;
	unsigned long flag = 0;

	RTPRINT(rtlpriv, FINIT, INIT_IQK,
		"IQK:Start!!!channel %d\n", rtlphy->current_channel);
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
	RTPRINT(rtlpriv, FINIT, INIT_IQK,
		"IQK !!!currentband %d\n", rtlhal->current_bandtype);
	rtl92d_acquire_cckandrw_pagea_ctl(hw, &flag);
	for (i = 0; i < 3; i++) {
		if (rtlhal->current_bandtype == BAND_ON_5G) {
			_rtl92d_phy_iq_calibrate_5g_normal(hw, result, i);
		} else if (rtlhal->current_bandtype == BAND_ON_2_4G) {
			if (IS_92D_SINGLEPHY(rtlhal->version))
				_rtl92d_phy_iq_calibrate(hw, result, i, true);
			else
				_rtl92d_phy_iq_calibrate(hw, result, i, false);
		}
		if (i == 1) {
			is12simular = _rtl92d_phy_simularity_compare(hw, result,
								     0, 1);
			if (is12simular) {
				final_candidate = 0;
				break;
			}
		}
		if (i == 2) {
			is13simular = _rtl92d_phy_simularity_compare(hw, result,
								     0, 2);
			if (is13simular) {
				final_candidate = 0;
				break;
			}
			is23simular = _rtl92d_phy_simularity_compare(hw, result,
								     1, 2);
			if (is23simular) {
				final_candidate = 1;
			} else {
				for (i = 0; i < 8; i++)
					regtmp += result[3][i];

				if (regtmp != 0)
					final_candidate = 3;
				else
					final_candidate = 0xFF;
			}
		}
	}
	rtl92d_release_cckandrw_pagea_ctl(hw, &flag);
	for (i = 0; i < 4; i++) {
		rege94 = result[i][0];
		rege9c = result[i][1];
		regea4 = result[i][2];
		regeac = result[i][3];
		regeb4 = result[i][4];
		regebc = result[i][5];
		regec4 = result[i][6];
		regecc = result[i][7];
		RTPRINT(rtlpriv, FINIT, INIT_IQK,
			"IQK: rege94=%lx rege9c=%lx regea4=%lx regeac=%lx regeb4=%lx regebc=%lx regec4=%lx regecc=%lx\n",
			rege94, rege9c, regea4, regeac, regeb4, regebc, regec4,
			regecc);
	}
	if (final_candidate != 0xff) {
		rtlphy->reg_e94 = rege94 = result[final_candidate][0];
		rtlphy->reg_e9c = rege9c = result[final_candidate][1];
		regea4 = result[final_candidate][2];
		regeac = result[final_candidate][3];
		rtlphy->reg_eb4 = regeb4 = result[final_candidate][4];
		rtlphy->reg_ebc = regebc = result[final_candidate][5];
		regec4 = result[final_candidate][6];
		regecc = result[final_candidate][7];
		RTPRINT(rtlpriv, FINIT, INIT_IQK,
			"IQK: final_candidate is %x\n", final_candidate);
		RTPRINT(rtlpriv, FINIT, INIT_IQK,
			"IQK: rege94=%lx rege9c=%lx regea4=%lx regeac=%lx regeb4=%lx regebc=%lx regec4=%lx regecc=%lx\n",
			rege94, rege9c, regea4, regeac, regeb4, regebc, regec4,
			regecc);
		patha_ok = pathb_ok = true;
	} else {
		rtlphy->reg_e94 = rtlphy->reg_eb4 = 0x100; /* X default value */
		rtlphy->reg_e9c = rtlphy->reg_ebc = 0x0;   /* Y default value */
	}
	if ((rege94 != 0) /*&&(regea4 != 0) */)
		_rtl92d_phy_patha_fill_iqk_matrix(hw, patha_ok, result,
				final_candidate, (regea4 == 0));
	if (IS_92D_SINGLEPHY(rtlhal->version)) {
		if ((regeb4 != 0) /*&&(regec4 != 0) */)
			_rtl92d_phy_pathb_fill_iqk_matrix(hw, pathb_ok, result,
						final_candidate, (regec4 == 0));
	}
	if (final_candidate != 0xFF) {
		indexforchannel = rtl92d_get_rightchnlplace_for_iqk(
				  rtlphy->current_channel);

		for (i = 0; i < IQK_MATRIX_REG_NUM; i++)
			rtlphy->iqk_matrix_regsetting[indexforchannel].
				value[0][i] = result[final_candidate][i];
		rtlphy->iqk_matrix_regsetting[indexforchannel].iqk_done =
			true;

		RT_TRACE(rtlpriv, COMP_SCAN | COMP_MLME, DBG_LOUD,
			 "IQK OK indexforchannel %d\n", indexforchannel);
	}
}

void rtl92d_phy_reload_iqk_setting(struct ieee80211_hw *hw, u8 channel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_hal *rtlhal = &(rtlpriv->rtlhal);
	u8 indexforchannel;

	RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD, "channel %d\n", channel);
	/*------Do IQK for normal chip and test chip 5G band------- */
	indexforchannel = rtl92d_get_rightchnlplace_for_iqk(channel);
	RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD, "indexforchannel %d done %d\n",
		 indexforchannel,
		 rtlphy->iqk_matrix_regsetting[indexforchannel].iqk_done);
	if (0 && !rtlphy->iqk_matrix_regsetting[indexforchannel].iqk_done &&
		rtlphy->need_iqk) {
		/* Re Do IQK. */
		RT_TRACE(rtlpriv, COMP_SCAN | COMP_INIT, DBG_LOUD,
			 "Do IQK Matrix reg for channel:%d....\n", channel);
		rtl92d_phy_iq_calibrate(hw);
	} else {
		/* Just load the value. */
		/* 2G band just load once. */
		if (((!rtlhal->load_imrandiqk_setting_for2g) &&
		    indexforchannel == 0) || indexforchannel > 0) {
			RT_TRACE(rtlpriv, COMP_SCAN, DBG_LOUD,
				 "Just Read IQK Matrix reg for channel:%d....\n",
				 channel);
			if ((rtlphy->iqk_matrix_regsetting[indexforchannel].
			     value[0] != NULL)
				/*&&(regea4 != 0) */)
				_rtl92d_phy_patha_fill_iqk_matrix(hw, true,
					rtlphy->iqk_matrix_regsetting[
					indexforchannel].value,	0,
					(rtlphy->iqk_matrix_regsetting[
					indexforchannel].value[0][2] == 0));
			if (IS_92D_SINGLEPHY(rtlhal->version)) {
				if ((rtlphy->iqk_matrix_regsetting[
					indexforchannel].value[0][4] != 0)
					/*&&(regec4 != 0) */)
					_rtl92d_phy_pathb_fill_iqk_matrix(hw,
						true,
						rtlphy->iqk_matrix_regsetting[
						indexforchannel].value, 0,
						(rtlphy->iqk_matrix_regsetting[
						indexforchannel].value[0][6]
						== 0));
			}
		}
	}
	rtlphy->need_iqk = false;
	RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD, "<====\n");
}

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
	u8 channel_5g[45] = {
		36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58,
		60, 62, 64, 100, 102, 104, 106, 108, 110, 112,
		114, 116, 118, 120, 122, 124, 126, 128, 130, 132,
		134, 136, 138, 140, 149, 151, 153, 155, 157, 159,
		161, 163, 165
	};

	for (i = 0; i < sizeof(channel_5g); i++)
		if (channel == channel_5g[i])
			return true;
	return false;
}

static void _rtl92d_phy_calc_curvindex(struct ieee80211_hw *hw,
				       u32 *targetchnl, u32 * curvecount_val,
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

static void _rtl92d_phy_reload_lck_setting(struct ieee80211_hw *hw,
		u8 channel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 erfpath = rtlpriv->rtlhal.current_bandtype ==
		BAND_ON_5G ? RF90_PATH_A :
		IS_92D_SINGLEPHY(rtlpriv->rtlhal.version) ?
		RF90_PATH_B : RF90_PATH_A;
	u32 u4tmp = 0, u4regvalue = 0;
	bool bneed_powerdown_radio = false;

	RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD, "path %d\n", erfpath);
	RTPRINT(rtlpriv, FINIT, INIT_IQK, "band type = %d\n",
		rtlpriv->rtlhal.current_bandtype);
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "channel = %d\n", channel);
	if (rtlpriv->rtlhal.current_bandtype == BAND_ON_5G) {/* Path-A for 5G */
		u4tmp = curveindex_5g[channel-1];
		RTPRINT(rtlpriv, FINIT, INIT_IQK,
			"ver 1 set RF-A, 5G,	0x28 = 0x%ulx !!\n", u4tmp);
		if (rtlpriv->rtlhal.macphymode == DUALMAC_DUALPHY &&
			rtlpriv->rtlhal.interfaceindex == 1) {
			bneed_powerdown_radio =
				rtl92d_phy_enable_anotherphy(hw, false);
			rtlpriv->rtlhal.during_mac1init_radioa = true;
			/* asume no this case */
			if (bneed_powerdown_radio)
				_rtl92d_phy_enable_rf_env(hw, erfpath,
							  &u4regvalue);
		}
		rtl_set_rfreg(hw, erfpath, RF_SYN_G4, 0x3f800, u4tmp);
		if (bneed_powerdown_radio)
			_rtl92d_phy_restore_rf_env(hw, erfpath, &u4regvalue);
		if (rtlpriv->rtlhal.during_mac1init_radioa)
			rtl92d_phy_powerdown_anotherphy(hw, false);
	} else if (rtlpriv->rtlhal.current_bandtype == BAND_ON_2_4G) {
		u4tmp = curveindex_2g[channel-1];
		RTPRINT(rtlpriv, FINIT, INIT_IQK,
			"ver 3 set RF-B, 2G, 0x28 = 0x%ulx !!\n", u4tmp);
		if (rtlpriv->rtlhal.macphymode == DUALMAC_DUALPHY &&
			rtlpriv->rtlhal.interfaceindex == 0) {
			bneed_powerdown_radio =
				rtl92d_phy_enable_anotherphy(hw, true);
			rtlpriv->rtlhal.during_mac0init_radiob = true;
			if (bneed_powerdown_radio)
				_rtl92d_phy_enable_rf_env(hw, erfpath,
							  &u4regvalue);
		}
		rtl_set_rfreg(hw, erfpath, RF_SYN_G4, 0x3f800, u4tmp);
		RTPRINT(rtlpriv, FINIT, INIT_IQK,
			"ver 3 set RF-B, 2G, 0x28 = 0x%ulx !!\n",
			rtl_get_rfreg(hw,  erfpath, RF_SYN_G4, 0x3f800));
		if (bneed_powerdown_radio)
			_rtl92d_phy_restore_rf_env(hw, erfpath, &u4regvalue);
		if (rtlpriv->rtlhal.during_mac0init_radiob)
			rtl92d_phy_powerdown_anotherphy(hw, true);
	}
	RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD, "<====\n");
}

static void _rtl92d_phy_lc_calibrate_sw(struct ieee80211_hw *hw, bool is2t)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = &(rtlpriv->rtlhal);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u8 tmpreg, index, rf_mode[2];
	u8 path = is2t ? 2 : 1;
	u8 i;
	u32 u4tmp, offset;
	u32 curvecount_val[CV_CURVE_CNT * 2] = {0};
	u16 timeout = 800, timecount = 0;

	/* Check continuous TX and Packet TX */
	tmpreg = rtl_read_byte(rtlpriv, 0xd03);
	/* if Deal with contisuous TX case, disable all continuous TX */
	/* if Deal with Packet TX case, block all queues */
	if ((tmpreg & 0x70) != 0)
		rtl_write_byte(rtlpriv, 0xd03, tmpreg & 0x8F);
	else
		rtl_write_byte(rtlpriv, REG_TXPAUSE, 0xFF);
	rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER4, 0xF00000, 0x0F);
	for (index = 0; index < path; index++) {
		/* 1. Read original RF mode */
		offset = index == 0 ? ROFDM0_XAAGCCORE1 : ROFDM0_XBAGCCORE1;
		rf_mode[index] = rtl_read_byte(rtlpriv, offset);
		/* 2. Set RF mode = standby mode */
		rtl_set_rfreg(hw, (enum radio_path)index, RF_AC,
			      BRFREGOFFSETMASK, 0x010000);
		if (rtlpci->init_ready) {
			/* switch CV-curve control by LC-calibration */
			rtl_set_rfreg(hw, (enum radio_path)index, RF_SYN_G7,
				      BIT(17), 0x0);
			/* 4. Set LC calibration begin */
			rtl_set_rfreg(hw, (enum radio_path)index, RF_CHNLBW,
				      0x08000, 0x01);
		}
		u4tmp = rtl_get_rfreg(hw, (enum radio_path)index, RF_SYN_G6,
				  BRFREGOFFSETMASK);
		while ((!(u4tmp & BIT(11))) && timecount <= timeout) {
			mdelay(50);
			timecount += 50;
			u4tmp = rtl_get_rfreg(hw, (enum radio_path)index,
					      RF_SYN_G6, BRFREGOFFSETMASK);
		}
		RTPRINT(rtlpriv, FINIT, INIT_IQK,
			"PHY_LCK finish delay for %d ms=2\n", timecount);
		u4tmp = rtl_get_rfreg(hw, index, RF_SYN_G4, BRFREGOFFSETMASK);
		if (index == 0 && rtlhal->interfaceindex == 0) {
			RTPRINT(rtlpriv, FINIT, INIT_IQK,
				"path-A / 5G LCK\n");
		} else {
			RTPRINT(rtlpriv, FINIT, INIT_IQK,
				"path-B / 2.4G LCK\n");
		}
		memset(&curvecount_val[0], 0, CV_CURVE_CNT * 2);
		/* Set LC calibration off */
		rtl_set_rfreg(hw, (enum radio_path)index, RF_CHNLBW,
			      0x08000, 0x0);
		RTPRINT(rtlpriv, FINIT, INIT_IQK,  "set RF 0x18[15] = 0\n");
		/* save Curve-counting number */
		for (i = 0; i < CV_CURVE_CNT; i++) {
			u32 readval = 0, readval2 = 0;
			rtl_set_rfreg(hw, (enum radio_path)index, 0x3F,
				      0x7f, i);

			rtl_set_rfreg(hw, (enum radio_path)index, 0x4D,
				BRFREGOFFSETMASK, 0x0);
			readval = rtl_get_rfreg(hw, (enum radio_path)index,
					  0x4F, BRFREGOFFSETMASK);
			curvecount_val[2 * i + 1] = (readval & 0xfffe0) >> 5;
			/* reg 0x4f [4:0] */
			/* reg 0x50 [19:10] */
			readval2 = rtl_get_rfreg(hw, (enum radio_path)index,
						 0x50, 0xffc00);
			curvecount_val[2 * i] = (((readval & 0x1F) << 10) |
						 readval2);
		}
		if (index == 0 && rtlhal->interfaceindex == 0)
			_rtl92d_phy_calc_curvindex(hw, targetchnl_5g,
						   curvecount_val,
						   true, curveindex_5g);
		else
			_rtl92d_phy_calc_curvindex(hw, targetchnl_2g,
						   curvecount_val,
						   false, curveindex_2g);
		/* switch CV-curve control mode */
		rtl_set_rfreg(hw, (enum radio_path)index, RF_SYN_G7,
			      BIT(17), 0x1);
	}

	/* Restore original situation  */
	for (index = 0; index < path; index++) {
		offset = index == 0 ? ROFDM0_XAAGCCORE1 : ROFDM0_XBAGCCORE1;
		rtl_write_byte(rtlpriv, offset, 0x50);
		rtl_write_byte(rtlpriv, offset, rf_mode[index]);
	}
	if ((tmpreg & 0x70) != 0)
		rtl_write_byte(rtlpriv, 0xd03, tmpreg);
	else /*Deal with Packet TX case */
		rtl_write_byte(rtlpriv, REG_TXPAUSE, 0x00);
	rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER4, 0xF00000, 0x00);
	_rtl92d_phy_reload_lck_setting(hw, rtlpriv->phy.current_channel);
}

static void _rtl92d_phy_lc_calibrate(struct ieee80211_hw *hw, bool is2t)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "cosa PHY_LCK ver=2\n");
	_rtl92d_phy_lc_calibrate_sw(hw, is2t);
}

void rtl92d_phy_lc_calibrate(struct ieee80211_hw *hw)
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
	RTPRINT(rtlpriv, FINIT, INIT_IQK,
		"LCK:Start!!! currentband %x delay %d ms\n",
		rtlhal->current_bandtype, timecount);
	if (IS_92D_SINGLEPHY(rtlhal->version)) {
		_rtl92d_phy_lc_calibrate(hw, true);
	} else {
		/* For 1T1R */
		_rtl92d_phy_lc_calibrate(hw, false);
	}
	rtlphy->lck_inprogress = false;
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "LCK:Finish!!!\n");
}

void rtl92d_phy_ap_calibrate(struct ieee80211_hw *hw, char delta)
{
	return;
}

static bool _rtl92d_phy_set_sw_chnl_cmdarray(struct swchnlcmd *cmdtable,
		u32 cmdtableidx, u32 cmdtablesz, enum swchnlcmd_id cmdid,
		u32 para1, u32 para2, u32 msdelay)
{
	struct swchnlcmd *pcmd;

	if (cmdtable == NULL) {
		RT_ASSERT(false, "cmdtable cannot be NULL\n");
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

void rtl92d_phy_reset_iqk_result(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u8 i;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "settings regs %d default regs %d\n",
		 (int)(sizeof(rtlphy->iqk_matrix_regsetting) /
		       sizeof(struct iqk_matrix_regs)),
		 IQK_MATRIX_REG_NUM);
	/* 0xe94, 0xe9c, 0xea4, 0xeac, 0xeb4, 0xebc, 0xec4, 0xecc */
	for (i = 0; i < IQK_MATRIX_SETTINGS_NUM; i++) {
		rtlphy->iqk_matrix_regsetting[i].value[0][0] = 0x100;
		rtlphy->iqk_matrix_regsetting[i].value[0][2] = 0x100;
		rtlphy->iqk_matrix_regsetting[i].value[0][4] = 0x100;
		rtlphy->iqk_matrix_regsetting[i].value[0][6] = 0x100;
		rtlphy->iqk_matrix_regsetting[i].value[0][1] = 0x0;
		rtlphy->iqk_matrix_regsetting[i].value[0][3] = 0x0;
		rtlphy->iqk_matrix_regsetting[i].value[0][5] = 0x0;
		rtlphy->iqk_matrix_regsetting[i].value[0][7] = 0x0;
		rtlphy->iqk_matrix_regsetting[i].iqk_done = false;
	}
}

static bool _rtl92d_phy_sw_chnl_step_by_step(struct ieee80211_hw *hw,
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
	_rtl92d_phy_set_sw_chnl_cmdarray(precommoncmd, precommoncmdcnt++,
					 MAX_PRECMD_CNT,
					 CMDID_SET_TXPOWEROWER_LEVEL, 0, 0, 0);
	_rtl92d_phy_set_sw_chnl_cmdarray(precommoncmd, precommoncmdcnt++,
					 MAX_PRECMD_CNT, CMDID_END, 0, 0, 0);
	postcommoncmdcnt = 0;
	_rtl92d_phy_set_sw_chnl_cmdarray(postcommoncmd, postcommoncmdcnt++,
					 MAX_POSTCMD_CNT, CMDID_END, 0, 0, 0);
	rfdependcmdcnt = 0;
	_rtl92d_phy_set_sw_chnl_cmdarray(rfdependcmd, rfdependcmdcnt++,
					 MAX_RFDEPENDCMD_CNT, CMDID_RF_WRITEREG,
					 RF_CHNLBW, channel, 0);
	_rtl92d_phy_set_sw_chnl_cmdarray(rfdependcmd, rfdependcmdcnt++,
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
			rtl92d_phy_set_txpower_level(hw, channel);
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
					0xffffff00) | currentcmd->para2);
				if (rtlpriv->rtlhal.current_bandtype ==
				    BAND_ON_5G) {
					if (currentcmd->para2 > 99)
						rtlphy->rfreg_chnlval[rfpath] =
						    rtlphy->rfreg_chnlval
						    [rfpath] | (BIT(18));
					else
						rtlphy->rfreg_chnlval[rfpath] =
						    rtlphy->rfreg_chnlval
						    [rfpath] & (~BIT(18));
					rtlphy->rfreg_chnlval[rfpath] |=
						 (BIT(16) | BIT(8));
				} else {
					rtlphy->rfreg_chnlval[rfpath] &=
						~(BIT(8) | BIT(16) | BIT(18));
				}
				rtl_set_rfreg(hw, (enum radio_path)rfpath,
					      currentcmd->para1,
					      BRFREGOFFSETMASK,
					      rtlphy->rfreg_chnlval[rfpath]);
				_rtl92d_phy_reload_imr_setting(hw, channel,
							       rfpath);
			}
			_rtl92d_phy_switch_rf_setting(hw, channel);
			/* do IQK when all parameters are ready */
			rtl92d_phy_reload_iqk_setting(hw, channel);
			break;
		default:
			RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
				 "switch case not processed\n");
			break;
		}
		break;
	} while (true);
	(*delay) = currentcmd->msdelay;
	(*step)++;
	return false;
}

u8 rtl92d_phy_sw_chnl(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u32 delay;
	u32 timeout = 1000, timecount = 0;
	u8 channel = rtlphy->current_channel;
	u32 ret_value;

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
	if (rtlhal->macphymode == SINGLEMAC_SINGLEPHY &&
	    rtlhal->bandset == BAND_ON_BOTH) {
		ret_value = rtl_get_bbreg(hw, RFPGA0_XAB_RFPARAMETER,
					  BMASKDWORD);
		if (rtlphy->current_channel > 14 && !(ret_value & BIT(0)))
			rtl92d_phy_switch_wirelessband(hw, BAND_ON_5G);
		else if (rtlphy->current_channel <= 14 && (ret_value & BIT(0)))
			rtl92d_phy_switch_wirelessband(hw, BAND_ON_2_4G);
	}
	switch (rtlhal->current_bandtype) {
	case BAND_ON_5G:
		/* Get first channel error when change between
		 * 5G and 2.4G band. */
		if (channel <= 14)
			return 0;
		RT_ASSERT((channel > 14), "5G but channel<=14\n");
		break;
	case BAND_ON_2_4G:
		/* Get first channel error when change between
		 * 5G and 2.4G band. */
		if (channel > 14)
			return 0;
		RT_ASSERT((channel <= 14), "2G but channel>14\n");
		break;
	default:
		RT_ASSERT(false, "Invalid WirelessMode(%#x)!!\n",
			  rtlpriv->mac80211.mode);
		break;
	}
	rtlphy->sw_chnl_inprogress = true;
	if (channel == 0)
		channel = 1;
	rtlphy->sw_chnl_stage = 0;
	rtlphy->sw_chnl_step = 0;
	RT_TRACE(rtlpriv, COMP_SCAN, DBG_TRACE,
		 "switch to channel%d\n", rtlphy->current_channel);

	do {
		if (!rtlphy->sw_chnl_inprogress)
			break;
		if (!_rtl92d_phy_sw_chnl_step_by_step(hw,
						      rtlphy->current_channel,
		    &rtlphy->sw_chnl_stage, &rtlphy->sw_chnl_step, &delay)) {
			if (delay > 0)
				mdelay(delay);
			else
				continue;
		} else {
			rtlphy->sw_chnl_inprogress = false;
		}
		break;
	} while (true);
	RT_TRACE(rtlpriv, COMP_SCAN, DBG_TRACE, "<==\n");
	rtlphy->sw_chnl_inprogress = false;
	return 1;
}

static void rtl92d_phy_set_io(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);

	RT_TRACE(rtlpriv, COMP_CMD, DBG_TRACE,
		 "--->Cmd(%#x), set_io_inprogress(%d)\n",
		 rtlphy->current_io_type, rtlphy->set_io_inprogress);
	switch (rtlphy->current_io_type) {
	case IO_CMD_RESUME_DM_BY_SCAN:
		de_digtable.cur_igvalue = rtlphy->initgain_backup.xaagccore1;
		rtl92d_dm_write_dig(hw);
		rtl92d_phy_set_txpower_level(hw, rtlphy->current_channel);
		break;
	case IO_CMD_PAUSE_DM_BY_SCAN:
		rtlphy->initgain_backup.xaagccore1 = de_digtable.cur_igvalue;
		de_digtable.cur_igvalue = 0x37;
		rtl92d_dm_write_dig(hw);
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "switch case not processed\n");
		break;
	}
	rtlphy->set_io_inprogress = false;
	RT_TRACE(rtlpriv, COMP_CMD, DBG_TRACE, "<---(%#x)\n",
		 rtlphy->current_io_type);
}

bool rtl92d_phy_set_io_cmd(struct ieee80211_hw *hw, enum io_type iotype)
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
				 "[IO CMD] Resume DM after scan\n");
			postprocessing = true;
			break;
		case IO_CMD_PAUSE_DM_BY_SCAN:
			RT_TRACE(rtlpriv, COMP_CMD, DBG_TRACE,
				 "[IO CMD] Pause DM before scan\n");
			postprocessing = true;
			break;
		default:
			RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
				 "switch case not processed\n");
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
	RT_TRACE(rtlpriv, COMP_CMD, DBG_TRACE, "<--IO Type(%#x)\n", iotype);
	return true;
}

static void _rtl92d_phy_set_rfon(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	/* a.  SYS_CLKR 0x08[11] = 1  restore MAC clock */
	/* b.  SPS_CTRL 0x11[7:0] = 0x2b */
	if (rtlpriv->rtlhal.macphymode == SINGLEMAC_SINGLEPHY)
		rtl_write_byte(rtlpriv, REG_SPS0_CTRL, 0x2b);
	/* c.  For PCIE: SYS_FUNC_EN 0x02[7:0] = 0xE3 enable BB TRX function */
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE3);
	/* RF_ON_EXCEP(d~g): */
	/* d.  APSD_CTRL 0x600[7:0] = 0x00 */
	rtl_write_byte(rtlpriv, REG_APSD_CTRL, 0x00);
	/* e.  SYS_FUNC_EN 0x02[7:0] = 0xE2  reset BB TRX function again */
	/* f.  SYS_FUNC_EN 0x02[7:0] = 0xE3  enable BB TRX function*/
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE2);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE3);
	/* g.   txpause 0x522[7:0] = 0x00  enable mac tx queue */
	rtl_write_byte(rtlpriv, REG_TXPAUSE, 0x00);
}

static void _rtl92d_phy_set_rfsleep(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 u4btmp;
	u8 delay = 5;

	/* a.   TXPAUSE 0x522[7:0] = 0xFF  Pause MAC TX queue  */
	rtl_write_byte(rtlpriv, REG_TXPAUSE, 0xFF);
	/* b.   RF path 0 offset 0x00 = 0x00  disable RF  */
	rtl_set_rfreg(hw, RF90_PATH_A, 0x00, BRFREGOFFSETMASK, 0x00);
	/* c.   APSD_CTRL 0x600[7:0] = 0x40 */
	rtl_write_byte(rtlpriv, REG_APSD_CTRL, 0x40);
	/* d. APSD_CTRL 0x600[7:0] = 0x00
	 * APSD_CTRL 0x600[7:0] = 0x00
	 * RF path 0 offset 0x00 = 0x00
	 * APSD_CTRL 0x600[7:0] = 0x40
	 * */
	u4btmp = rtl_get_rfreg(hw, RF90_PATH_A, 0, BRFREGOFFSETMASK);
	while (u4btmp != 0 && delay > 0) {
		rtl_write_byte(rtlpriv, REG_APSD_CTRL, 0x0);
		rtl_set_rfreg(hw, RF90_PATH_A, 0x00, BRFREGOFFSETMASK, 0x00);
		rtl_write_byte(rtlpriv, REG_APSD_CTRL, 0x40);
		u4btmp = rtl_get_rfreg(hw, RF90_PATH_A, 0, BRFREGOFFSETMASK);
		delay--;
	}
	if (delay == 0) {
		/* Jump out the LPS turn off sequence to RF_ON_EXCEP */
		rtl_write_byte(rtlpriv, REG_APSD_CTRL, 0x00);

		rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE2);
		rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE3);
		rtl_write_byte(rtlpriv, REG_TXPAUSE, 0x00);
		RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
			 "Fail !!! Switch RF timeout\n");
		return;
	}
	/* e.   For PCIE: SYS_FUNC_EN 0x02[7:0] = 0xE2 reset BB TRX function */
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE2);
	/* f.   SPS_CTRL 0x11[7:0] = 0x22 */
	if (rtlpriv->rtlhal.macphymode == SINGLEMAC_SINGLEPHY)
		rtl_write_byte(rtlpriv, REG_SPS0_CTRL, 0x22);
	/* g.    SYS_CLKR 0x08[11] = 0  gated MAC clock */
}

bool rtl92d_phy_set_rf_power_state(struct ieee80211_hw *hw,
				   enum rf_pwrstate rfpwr_state)
{

	bool bresult = true;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u8 i, queue_id;
	struct rtl8192_tx_ring *ring = NULL;

	if (rfpwr_state == ppsc->rfpwr_state)
		return false;
	switch (rfpwr_state) {
	case ERFON:
		if ((ppsc->rfpwr_state == ERFOFF) &&
		    RT_IN_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC)) {
			bool rtstatus;
			u32 InitializeCount = 0;
			do {
				InitializeCount++;
				RT_TRACE(rtlpriv, COMP_RF, DBG_DMESG,
					 "IPS Set eRf nic enable\n");
				rtstatus = rtl_ps_enable_nic(hw);
			} while (!rtstatus && (InitializeCount < 10));

			RT_CLEAR_PS_LEVEL(ppsc,
					  RT_RF_OFF_LEVL_HALT_NIC);
		} else {
			RT_TRACE(rtlpriv, COMP_POWER, DBG_DMESG,
				 "awake, sleeped:%d ms state_inap:%x\n",
				 jiffies_to_msecs(jiffies -
						  ppsc->last_sleep_jiffies),
				 rtlpriv->psc.state_inap);
			ppsc->last_awake_jiffies = jiffies;
			_rtl92d_phy_set_rfon(hw);
		}

		if (mac->link_state == MAC80211_LINKED)
			rtlpriv->cfg->ops->led_control(hw,
					 LED_CTL_LINK);
		else
			rtlpriv->cfg->ops->led_control(hw,
					 LED_CTL_NO_LINK);
		break;
	case ERFOFF:
		if (ppsc->reg_rfps_level & RT_RF_OFF_LEVL_HALT_NIC) {
			RT_TRACE(rtlpriv, COMP_RF, DBG_DMESG,
				 "IPS Set eRf nic disable\n");
			rtl_ps_disable_nic(hw);
			RT_SET_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC);
		} else {
			if (ppsc->rfoff_reason == RF_CHANGE_BY_IPS)
				rtlpriv->cfg->ops->led_control(hw,
						 LED_CTL_NO_LINK);
			else
				rtlpriv->cfg->ops->led_control(hw,
						 LED_CTL_POWER_OFF);
		}
		break;
	case ERFSLEEP:
		if (ppsc->rfpwr_state == ERFOFF)
			return false;

		for (queue_id = 0, i = 0;
		     queue_id < RTL_PCI_MAX_TX_QUEUE_COUNT;) {
			ring = &pcipriv->dev.tx_ring[queue_id];
			if (skb_queue_len(&ring->queue) == 0 ||
			    queue_id == BEACON_QUEUE) {
				queue_id++;
				continue;
			} else if (rtlpci->pdev->current_state != PCI_D0) {
				RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
					 "eRf Off/Sleep: %d times TcbBusyQueue[%d] !=0 but lower power state!\n",
					 i + 1, queue_id);
				break;
			} else {
				RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
					 "eRf Off/Sleep: %d times TcbBusyQueue[%d] =%d before doze!\n",
					 i + 1, queue_id,
					 skb_queue_len(&ring->queue));
				udelay(10);
				i++;
			}

			if (i >= MAX_DOZE_WAITING_TIMES_9x) {
				RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
					 "ERFOFF: %d times TcbBusyQueue[%d] = %d !\n",
					 MAX_DOZE_WAITING_TIMES_9x, queue_id,
					 skb_queue_len(&ring->queue));
				break;
			}
		}
		RT_TRACE(rtlpriv, COMP_POWER, DBG_DMESG,
			 "Set rfsleep awaked:%d ms\n",
			 jiffies_to_msecs(jiffies - ppsc->last_awake_jiffies));
		RT_TRACE(rtlpriv, COMP_POWER, DBG_DMESG,
			 "sleep awaked:%d ms state_inap:%x\n",
			 jiffies_to_msecs(jiffies -
					  ppsc->last_awake_jiffies),
			 rtlpriv->psc.state_inap);
		ppsc->last_sleep_jiffies = jiffies;
		_rtl92d_phy_set_rfsleep(hw);
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "switch case not processed\n");
		bresult = false;
		break;
	}
	if (bresult)
		ppsc->rfpwr_state = rfpwr_state;
	return bresult;
}

void rtl92d_phy_config_macphymode(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 offset = REG_MAC_PHY_CTRL_NORMAL;

	switch (rtlhal->macphymode) {
	case DUALMAC_DUALPHY:
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "MacPhyMode: DUALMAC_DUALPHY\n");
		rtl_write_byte(rtlpriv, offset, 0xF3);
		break;
	case SINGLEMAC_SINGLEPHY:
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "MacPhyMode: SINGLEMAC_SINGLEPHY\n");
		rtl_write_byte(rtlpriv, offset, 0xF4);
		break;
	case DUALMAC_SINGLEPHY:
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "MacPhyMode: DUALMAC_SINGLEPHY\n");
		rtl_write_byte(rtlpriv, offset, 0xF1);
		break;
	}
}

void rtl92d_phy_config_macphymode_info(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_phy *rtlphy = &(rtlpriv->phy);

	switch (rtlhal->macphymode) {
	case DUALMAC_SINGLEPHY:
		rtlphy->rf_type = RF_2T2R;
		rtlhal->version |= CHIP_92D_SINGLEPHY;
		rtlhal->bandset = BAND_ON_BOTH;
		rtlhal->current_bandtype = BAND_ON_2_4G;
		break;

	case SINGLEMAC_SINGLEPHY:
		rtlphy->rf_type = RF_2T2R;
		rtlhal->version |= CHIP_92D_SINGLEPHY;
		rtlhal->bandset = BAND_ON_BOTH;
		rtlhal->current_bandtype = BAND_ON_2_4G;
		break;

	case DUALMAC_DUALPHY:
		rtlphy->rf_type = RF_1T1R;
		rtlhal->version &= (~CHIP_92D_SINGLEPHY);
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

u8 rtl92d_get_chnlgroup_fromarray(u8 chnl)
{
	u8 group;
	u8 channel_info[59] = {
		1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
		36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56,
		58, 60, 62, 64, 100, 102, 104, 106, 108,
		110, 112, 114, 116, 118, 120, 122, 124,
		126, 128, 130, 132, 134, 136, 138, 140,
		149, 151, 153, 155, 157, 159, 161, 163,
		165
	};

	if (channel_info[chnl] <= 3)
		group = 0;
	else if (channel_info[chnl] <= 9)
		group = 1;
	else if (channel_info[chnl] <= 14)
		group = 2;
	else if (channel_info[chnl] <= 44)
		group = 3;
	else if (channel_info[chnl] <= 54)
		group = 4;
	else if (channel_info[chnl] <= 64)
		group = 5;
	else if (channel_info[chnl] <= 112)
		group = 6;
	else if (channel_info[chnl] <= 126)
		group = 7;
	else if (channel_info[chnl] <= 140)
		group = 8;
	else if (channel_info[chnl] <= 153)
		group = 9;
	else if (channel_info[chnl] <= 159)
		group = 10;
	else
		group = 11;
	return group;
}

void rtl92d_phy_set_poweron(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	unsigned long flags;
	u8 value8;
	u16 i;
	u32 mac_reg = (rtlhal->interfaceindex == 0 ? REG_MAC0 : REG_MAC1);

	/* notice fw know band status  0x81[1]/0x53[1] = 0: 5G, 1: 2G */
	if (rtlhal->current_bandtype == BAND_ON_2_4G) {
		value8 = rtl_read_byte(rtlpriv, mac_reg);
		value8 |= BIT(1);
		rtl_write_byte(rtlpriv, mac_reg, value8);
	} else {
		value8 = rtl_read_byte(rtlpriv, mac_reg);
		value8 &= (~BIT(1));
		rtl_write_byte(rtlpriv, mac_reg, value8);
	}

	if (rtlhal->macphymode == SINGLEMAC_SINGLEPHY) {
		value8 = rtl_read_byte(rtlpriv, REG_MAC0);
		rtl_write_byte(rtlpriv, REG_MAC0, value8 | MAC0_ON);
	} else {
		spin_lock_irqsave(&globalmutex_power, flags);
		if (rtlhal->interfaceindex == 0) {
			value8 = rtl_read_byte(rtlpriv, REG_MAC0);
			rtl_write_byte(rtlpriv, REG_MAC0, value8 | MAC0_ON);
		} else {
			value8 = rtl_read_byte(rtlpriv, REG_MAC1);
			rtl_write_byte(rtlpriv, REG_MAC1, value8 | MAC1_ON);
		}
		value8 = rtl_read_byte(rtlpriv, REG_POWER_OFF_IN_PROCESS);
		spin_unlock_irqrestore(&globalmutex_power, flags);
		for (i = 0; i < 200; i++) {
			if ((value8 & BIT(7)) == 0) {
				break;
			} else {
				udelay(500);
				spin_lock_irqsave(&globalmutex_power, flags);
				value8 = rtl_read_byte(rtlpriv,
						    REG_POWER_OFF_IN_PROCESS);
				spin_unlock_irqrestore(&globalmutex_power,
						       flags);
			}
		}
		if (i == 200)
			RT_ASSERT(false, "Another mac power off over time\n");
	}
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

void rtl92d_update_bbrf_configuration(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u8 rfpath, i;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "==>\n");
	/* r_select_5G for path_A/B 0 for 2.4G, 1 for 5G */
	if (rtlhal->current_bandtype == BAND_ON_2_4G) {
		/* r_select_5G for path_A/B,0x878 */
		rtl_set_bbreg(hw, RFPGA0_XAB_RFPARAMETER, BIT(0), 0x0);
		rtl_set_bbreg(hw, RFPGA0_XAB_RFPARAMETER, BIT(15), 0x0);
		if (rtlhal->macphymode != DUALMAC_DUALPHY) {
			rtl_set_bbreg(hw, RFPGA0_XAB_RFPARAMETER, BIT(16), 0x0);
			rtl_set_bbreg(hw, RFPGA0_XAB_RFPARAMETER, BIT(31), 0x0);
		}
		/* rssi_table_select:index 0 for 2.4G.1~3 for 5G,0xc78 */
		rtl_set_bbreg(hw, ROFDM0_AGCRSSITABLE, BIT(6) | BIT(7), 0x0);
		/* fc_area  0xd2c */
		rtl_set_bbreg(hw, ROFDM1_CFOTRACKING, BIT(14) | BIT(13), 0x0);
		/* 5G LAN ON */
		rtl_set_bbreg(hw, 0xB30, 0x00F00000, 0xa);
		/* TX BB gain shift*1,Just for testchip,0xc80,0xc88 */
		rtl_set_bbreg(hw, ROFDM0_XATxIQIMBALANCE, BMASKDWORD,
			      0x40000100);
		rtl_set_bbreg(hw, ROFDM0_XBTxIQIMBALANCE, BMASKDWORD,
			      0x40000100);
		if (rtlhal->macphymode == DUALMAC_DUALPHY) {
			rtl_set_bbreg(hw, RFPGA0_XAB_RFINTERFACESW,
				      BIT(10) | BIT(6) | BIT(5),
				      ((rtlefuse->eeprom_c9 & BIT(3)) >> 3) |
				      (rtlefuse->eeprom_c9 & BIT(1)) |
				      ((rtlefuse->eeprom_cc & BIT(1)) << 4));
			rtl_set_bbreg(hw, RFPGA0_XA_RFINTERFACEOE,
				      BIT(10) | BIT(6) | BIT(5),
				      ((rtlefuse->eeprom_c9 & BIT(2)) >> 2) |
				      ((rtlefuse->eeprom_c9 & BIT(0)) << 1) |
				      ((rtlefuse->eeprom_cc & BIT(0)) << 5));
			rtl_set_bbreg(hw, RFPGA0_XAB_RFPARAMETER, BIT(15), 0);
		} else {
			rtl_set_bbreg(hw, RFPGA0_XAB_RFINTERFACESW,
				      BIT(26) | BIT(22) | BIT(21) | BIT(10) |
				      BIT(6) | BIT(5),
				      ((rtlefuse->eeprom_c9 & BIT(3)) >> 3) |
				      (rtlefuse->eeprom_c9 & BIT(1)) |
				      ((rtlefuse->eeprom_cc & BIT(1)) << 4) |
				      ((rtlefuse->eeprom_c9 & BIT(7)) << 9) |
				      ((rtlefuse->eeprom_c9 & BIT(5)) << 12) |
				      ((rtlefuse->eeprom_cc & BIT(3)) << 18));
			rtl_set_bbreg(hw, RFPGA0_XA_RFINTERFACEOE,
				      BIT(10) | BIT(6) | BIT(5),
				      ((rtlefuse->eeprom_c9 & BIT(2)) >> 2) |
				      ((rtlefuse->eeprom_c9 & BIT(0)) << 1) |
				      ((rtlefuse->eeprom_cc & BIT(0)) << 5));
			rtl_set_bbreg(hw, RFPGA0_XB_RFINTERFACEOE,
				      BIT(10) | BIT(6) | BIT(5),
				      ((rtlefuse->eeprom_c9 & BIT(6)) >> 6) |
				      ((rtlefuse->eeprom_c9 & BIT(4)) >> 3) |
				      ((rtlefuse->eeprom_cc & BIT(2)) << 3));
			rtl_set_bbreg(hw, RFPGA0_XAB_RFPARAMETER,
				      BIT(31) | BIT(15), 0);
		}
		/* 1.5V_LDO */
	} else {
		/* r_select_5G for path_A/B */
		rtl_set_bbreg(hw, RFPGA0_XAB_RFPARAMETER, BIT(0), 0x1);
		rtl_set_bbreg(hw, RFPGA0_XAB_RFPARAMETER, BIT(15), 0x1);
		if (rtlhal->macphymode != DUALMAC_DUALPHY) {
			rtl_set_bbreg(hw, RFPGA0_XAB_RFPARAMETER, BIT(16), 0x1);
			rtl_set_bbreg(hw, RFPGA0_XAB_RFPARAMETER, BIT(31), 0x1);
		}
		/* rssi_table_select:index 0 for 2.4G.1~3 for 5G */
		rtl_set_bbreg(hw, ROFDM0_AGCRSSITABLE, BIT(6) | BIT(7), 0x1);
		/* fc_area */
		rtl_set_bbreg(hw, ROFDM1_CFOTRACKING, BIT(14) | BIT(13), 0x1);
		/* 5G LAN ON */
		rtl_set_bbreg(hw, 0xB30, 0x00F00000, 0x0);
		/* TX BB gain shift,Just for testchip,0xc80,0xc88 */
		if (rtlefuse->internal_pa_5g[0])
			rtl_set_bbreg(hw, ROFDM0_XATxIQIMBALANCE, BMASKDWORD,
				      0x2d4000b5);
		else
			rtl_set_bbreg(hw, ROFDM0_XATxIQIMBALANCE, BMASKDWORD,
				      0x20000080);
		if (rtlefuse->internal_pa_5g[1])
			rtl_set_bbreg(hw, ROFDM0_XBTxIQIMBALANCE, BMASKDWORD,
				      0x2d4000b5);
		else
			rtl_set_bbreg(hw, ROFDM0_XBTxIQIMBALANCE, BMASKDWORD,
				      0x20000080);
		if (rtlhal->macphymode == DUALMAC_DUALPHY) {
			rtl_set_bbreg(hw, RFPGA0_XAB_RFINTERFACESW,
				      BIT(10) | BIT(6) | BIT(5),
				      (rtlefuse->eeprom_cc & BIT(5)));
			rtl_set_bbreg(hw, RFPGA0_XA_RFINTERFACEOE, BIT(10),
				      ((rtlefuse->eeprom_cc & BIT(4)) >> 4));
			rtl_set_bbreg(hw, RFPGA0_XAB_RFPARAMETER, BIT(15),
				      (rtlefuse->eeprom_cc & BIT(4)) >> 4);
		} else {
			rtl_set_bbreg(hw, RFPGA0_XAB_RFINTERFACESW,
				      BIT(26) | BIT(22) | BIT(21) | BIT(10) |
				      BIT(6) | BIT(5),
				      (rtlefuse->eeprom_cc & BIT(5)) |
				      ((rtlefuse->eeprom_cc & BIT(7)) << 14));
			rtl_set_bbreg(hw, RFPGA0_XA_RFINTERFACEOE, BIT(10),
				      ((rtlefuse->eeprom_cc & BIT(4)) >> 4));
			rtl_set_bbreg(hw, RFPGA0_XB_RFINTERFACEOE, BIT(10),
				      ((rtlefuse->eeprom_cc & BIT(6)) >> 6));
			rtl_set_bbreg(hw, RFPGA0_XAB_RFPARAMETER,
				      BIT(31) | BIT(15),
				      ((rtlefuse->eeprom_cc & BIT(4)) >> 4) |
				      ((rtlefuse->eeprom_cc & BIT(6)) << 10));
		}
	}
	/* update IQK related settings */
	rtl_set_bbreg(hw, ROFDM0_XARXIQIMBALANCE, BMASKDWORD, 0x40000100);
	rtl_set_bbreg(hw, ROFDM0_XBRXIQIMBALANCE, BMASKDWORD, 0x40000100);
	rtl_set_bbreg(hw, ROFDM0_XCTxAFE, 0xF0000000, 0x00);
	rtl_set_bbreg(hw, ROFDM0_ECCATHRESHOLD, BIT(30) | BIT(28) |
		      BIT(26) | BIT(24), 0x00);
	rtl_set_bbreg(hw, ROFDM0_XDTxAFE, 0xF0000000, 0x00);
	rtl_set_bbreg(hw, 0xca0, 0xF0000000, 0x00);
	rtl_set_bbreg(hw, ROFDM0_AGCRSSITABLE, 0x0000F000, 0x00);

	/* Update RF */
	for (rfpath = RF90_PATH_A; rfpath < rtlphy->num_total_rfpath;
	     rfpath++) {
		if (rtlhal->current_bandtype == BAND_ON_2_4G) {
			/* MOD_AG for RF paht_A 0x18 BIT8,BIT16 */
			rtl_set_rfreg(hw, rfpath, RF_CHNLBW, BIT(8) | BIT(16) |
				      BIT(18), 0);
			/* RF0x0b[16:14] =3b'111 */
			rtl_set_rfreg(hw, (enum radio_path)rfpath, 0x0B,
				      0x1c000, 0x07);
		} else {
			/* MOD_AG for RF paht_A 0x18 BIT8,BIT16 */
			rtl_set_rfreg(hw, rfpath, RF_CHNLBW, BIT(8) |
				      BIT(16) | BIT(18),
				      (BIT(16) | BIT(8)) >> 8);
		}
	}
	/* Update for all band. */
	/* DMDP */
	if (rtlphy->rf_type == RF_1T1R) {
		/* Use antenna 0,0xc04,0xd04 */
		rtl_set_bbreg(hw, ROFDM0_TRXPATHENABLE, BMASKBYTE0, 0x11);
		rtl_set_bbreg(hw, ROFDM1_TRXPATHENABLE, BDWORD, 0x1);

		/* enable ad/da clock1 for dual-phy reg0x888 */
		if (rtlhal->interfaceindex == 0) {
			rtl_set_bbreg(hw, RFPGA0_ADDALLOCKEN, BIT(12) |
				      BIT(13), 0x3);
		} else {
			rtl92d_phy_enable_anotherphy(hw, false);
			RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
				 "MAC1 use DBI to update 0x888\n");
			/* 0x888 */
			rtl92de_write_dword_dbi(hw, RFPGA0_ADDALLOCKEN,
						rtl92de_read_dword_dbi(hw,
						RFPGA0_ADDALLOCKEN,
						BIT(3)) | BIT(12) | BIT(13),
						BIT(3));
			rtl92d_phy_powerdown_anotherphy(hw, false);
		}
	} else {
		/* Single PHY */
		/* Use antenna 0 & 1,0xc04,0xd04 */
		rtl_set_bbreg(hw, ROFDM0_TRXPATHENABLE, BMASKBYTE0, 0x33);
		rtl_set_bbreg(hw, ROFDM1_TRXPATHENABLE, BDWORD, 0x3);
		/* disable ad/da clock1,0x888 */
		rtl_set_bbreg(hw, RFPGA0_ADDALLOCKEN, BIT(12) | BIT(13), 0);
	}
	for (rfpath = RF90_PATH_A; rfpath < rtlphy->num_total_rfpath;
	     rfpath++) {
		rtlphy->rfreg_chnlval[rfpath] = rtl_get_rfreg(hw, rfpath,
						RF_CHNLBW, BRFREGOFFSETMASK);
		rtlphy->reg_rf3c[rfpath] = rtl_get_rfreg(hw, rfpath, 0x3C,
			BRFREGOFFSETMASK);
	}
	for (i = 0; i < 2; i++)
		RT_TRACE(rtlpriv, COMP_RF, DBG_LOUD, "RF 0x18 = 0x%x\n",
			 rtlphy->rfreg_chnlval[i]);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "<==\n");

}

bool rtl92d_phy_check_poweroff(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 u1btmp;
	unsigned long flags;

	if (rtlhal->macphymode == SINGLEMAC_SINGLEPHY) {
		u1btmp = rtl_read_byte(rtlpriv, REG_MAC0);
		rtl_write_byte(rtlpriv, REG_MAC0, u1btmp & (~MAC0_ON));
		return true;
	}
	spin_lock_irqsave(&globalmutex_power, flags);
	if (rtlhal->interfaceindex == 0) {
		u1btmp = rtl_read_byte(rtlpriv, REG_MAC0);
		rtl_write_byte(rtlpriv, REG_MAC0, u1btmp & (~MAC0_ON));
		u1btmp = rtl_read_byte(rtlpriv, REG_MAC1);
		u1btmp &= MAC1_ON;
	} else {
		u1btmp = rtl_read_byte(rtlpriv, REG_MAC1);
		rtl_write_byte(rtlpriv, REG_MAC1, u1btmp & (~MAC1_ON));
		u1btmp = rtl_read_byte(rtlpriv, REG_MAC0);
		u1btmp &= MAC0_ON;
	}
	if (u1btmp) {
		spin_unlock_irqrestore(&globalmutex_power, flags);
		return false;
	}
	u1btmp = rtl_read_byte(rtlpriv, REG_POWER_OFF_IN_PROCESS);
	u1btmp |= BIT(7);
	rtl_write_byte(rtlpriv, REG_POWER_OFF_IN_PROCESS, u1btmp);
	spin_unlock_irqrestore(&globalmutex_power, flags);
	return true;
}
