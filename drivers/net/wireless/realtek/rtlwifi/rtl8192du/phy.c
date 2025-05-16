// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2024  Realtek Corporation.*/

#include "../wifi.h"
#include "../ps.h"
#include "../core.h"
#include "../efuse.h"
#include "../usb.h"
#include "../rtl8192d/reg.h"
#include "../rtl8192d/def.h"
#include "../rtl8192d/phy_common.h"
#include "../rtl8192d/rf_common.h"
#include "phy.h"
#include "rf.h"
#include "table.h"

#define MAX_RF_IMR_INDEX			12
#define MAX_RF_IMR_INDEX_NORMAL			13
#define RF_REG_NUM_FOR_C_CUT_5G			6
#define RF_REG_NUM_FOR_C_CUT_5G_INTERNALPA	7
#define RF_REG_NUM_FOR_C_CUT_2G			5
#define RF_CHNL_NUM_5G				19
#define RF_CHNL_NUM_5G_40M			17
#define CV_CURVE_CNT				64

static const u32 rf_reg_for_5g_swchnl_normal[MAX_RF_IMR_INDEX_NORMAL] = {
	0, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x0
};

static const u8 rf_reg_for_c_cut_5g[RF_REG_NUM_FOR_C_CUT_5G] = {
	RF_SYN_G1, RF_SYN_G2, RF_SYN_G3, RF_SYN_G4, RF_SYN_G5, RF_SYN_G6
};

static const u8 rf_reg_for_c_cut_2g[RF_REG_NUM_FOR_C_CUT_2G] = {
	RF_SYN_G1, RF_SYN_G2, RF_SYN_G3, RF_SYN_G7, RF_SYN_G8
};

static const u8 rf_for_c_cut_5g_internal_pa[RF_REG_NUM_FOR_C_CUT_5G_INTERNALPA] = {
	0x0B, 0x48, 0x49, 0x4B, 0x03, 0x04, 0x0E
};

static const u32 rf_reg_mask_for_c_cut_2g[RF_REG_NUM_FOR_C_CUT_2G] = {
	BIT(19) | BIT(18) | BIT(17) | BIT(14) | BIT(1),
	BIT(10) | BIT(9),
	BIT(18) | BIT(17) | BIT(16) | BIT(1),
	BIT(2) | BIT(1),
	BIT(15) | BIT(14) | BIT(13) | BIT(12) | BIT(11)
};

static const u8 rf_chnl_5g[RF_CHNL_NUM_5G] = {
	36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108,
	112, 116, 120, 124, 128, 132, 136, 140
};

static const u8 rf_chnl_5g_40m[RF_CHNL_NUM_5G_40M] = {
	38, 42, 46, 50, 54, 58, 62, 102, 106, 110, 114,
	118, 122, 126, 130, 134, 138
};

static const u32 rf_reg_pram_c_5g[5][RF_REG_NUM_FOR_C_CUT_5G] = {
	{0xE43BE, 0xFC638, 0x77C0A, 0xDE471, 0xd7110, 0x8EB04},
	{0xE43BE, 0xFC078, 0xF7C1A, 0xE0C71, 0xD7550, 0xAEB04},
	{0xE43BF, 0xFF038, 0xF7C0A, 0xDE471, 0xE5550, 0xAEB04},
	{0xE43BF, 0xFF079, 0xF7C1A, 0xDE471, 0xE5550, 0xAEB04},
	{0xE43BF, 0xFF038, 0xF7C1A, 0xDE471, 0xd7550, 0xAEB04}
};

static const u32 rf_reg_param_for_c_cut_2g[3][RF_REG_NUM_FOR_C_CUT_2G] = {
	{0x643BC, 0xFC038, 0x77C1A, 0x41289, 0x01840},
	{0x643BC, 0xFC038, 0x07C1A, 0x41289, 0x01840},
	{0x243BC, 0xFC438, 0x07C1A, 0x4128B, 0x0FC41}
};

static const u32 rf_syn_g4_for_c_cut_2g = 0xD1C31 & 0x7FF;

static const u32 rf_pram_c_5g_int_pa[3][RF_REG_NUM_FOR_C_CUT_5G_INTERNALPA] = {
	{0x01a00, 0x40443, 0x00eb5, 0x89bec, 0x94a12, 0x94a12, 0x94a12},
	{0x01800, 0xc0443, 0x00730, 0x896ee, 0x94a52, 0x94a52, 0x94a52},
	{0x01800, 0xc0443, 0x00730, 0x896ee, 0x94a12, 0x94a12, 0x94a12}
};

/* [patha+b][reg] */
static const u32 rf_imr_param_normal[3][MAX_RF_IMR_INDEX_NORMAL] = {
	/* channels 1-14. */
	{
		0x70000, 0x00ff0, 0x4400f, 0x00ff0, 0x0, 0x0, 0x0,
		0x0, 0x0, 0x64888, 0xe266c, 0x00090, 0x22fff
	},
	/* channels 36-64 */
	{
		0x70000, 0x22880, 0x4470f, 0x55880, 0x00070, 0x88000,
		0x0, 0x88080, 0x70000, 0x64a82, 0xe466c, 0x00090,
		0x32c9a
	},
	/* channels 100-165 */
	{
		0x70000, 0x44880, 0x4477f, 0x77880, 0x00070, 0x88000,
		0x0, 0x880b0, 0x0, 0x64b82, 0xe466c, 0x00090, 0x32c9a
	}
};

static const u32 targetchnl_5g[TARGET_CHNL_NUM_5G] = {
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
static const u32 targetchnl_2g[TARGET_CHNL_NUM_2G] = {
	26084, 26030, 25976, 25923, 25869, 25816, 25764,
	25711, 25658, 25606, 25554, 25502, 25451, 25328
};

u32 rtl92du_phy_query_bb_reg(struct ieee80211_hw *hw, u32 regaddr, u32 bitmask)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	u32 returnvalue, originalvalue, bitshift;

	rtl_dbg(rtlpriv, COMP_RF, DBG_TRACE, "regaddr(%#x), bitmask(%#x)\n",
		regaddr, bitmask);

	if (rtlhal->during_mac1init_radioa)
		regaddr |= MAC1_ACCESS_PHY0;
	else if (rtlhal->during_mac0init_radiob)
		regaddr |= MAC0_ACCESS_PHY1;

	originalvalue = rtl_read_dword(rtlpriv, regaddr);
	bitshift = calculate_bit_shift(bitmask);
	returnvalue = (originalvalue & bitmask) >> bitshift;
	rtl_dbg(rtlpriv, COMP_RF, DBG_TRACE,
		"BBR MASK=0x%x Addr[0x%x]=0x%x\n",
		bitmask, regaddr, originalvalue);
	return returnvalue;
}

void rtl92du_phy_set_bb_reg(struct ieee80211_hw *hw,
			    u32 regaddr, u32 bitmask, u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	u32 originalvalue, bitshift;

	rtl_dbg(rtlpriv, COMP_RF, DBG_TRACE,
		"regaddr(%#x), bitmask(%#x), data(%#x)\n",
		regaddr, bitmask, data);

	if (rtlhal->during_mac1init_radioa)
		regaddr |= MAC1_ACCESS_PHY0;
	else if (rtlhal->during_mac0init_radiob)
		regaddr |= MAC0_ACCESS_PHY1;

	if (bitmask != MASKDWORD) {
		originalvalue = rtl_read_dword(rtlpriv, regaddr);
		bitshift = calculate_bit_shift(bitmask);
		data = (originalvalue & (~bitmask)) |
			((data << bitshift) & bitmask);
	}

	rtl_write_dword(rtlpriv, regaddr, data);
	rtl_dbg(rtlpriv, COMP_RF, DBG_TRACE,
		"regaddr(%#x), bitmask(%#x), data(%#x)\n",
		regaddr, bitmask, data);
}

/* To avoid miswrite Reg0x800 for 92D */
static void rtl92du_phy_set_bb_reg_1byte(struct ieee80211_hw *hw,
					 u32 regaddr, u32 bitmask, u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 originalvalue, bitshift, offset;
	u8 value;

	/* BitMask only support bit0~bit7 or bit8~bit15, bit16~bit23,
	 * bit24~bit31, should be in 1 byte scale;
	 */
	bitshift = calculate_bit_shift(bitmask);
	offset = bitshift / 8;

	originalvalue = rtl_read_dword(rtlpriv, regaddr);
	data = (originalvalue & (~bitmask)) | ((data << bitshift) & bitmask);

	value = data >> (8 * offset);

	rtl_write_byte(rtlpriv, regaddr + offset, value);
}

bool rtl92du_phy_mac_config(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 arraylength;
	const u32 *ptrarray;
	u32 i;

	rtl_dbg(rtlpriv, COMP_INIT, DBG_TRACE, "Read Rtl819XMACPHY_Array\n");

	arraylength = MAC_2T_ARRAYLENGTH;
	ptrarray = rtl8192du_mac_2tarray;

	for (i = 0; i < arraylength; i = i + 2)
		rtl_write_byte(rtlpriv, ptrarray[i], (u8)ptrarray[i + 1]);

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

static bool _rtl92du_phy_config_bb(struct ieee80211_hw *hw, u8 configtype)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	u16 phy_reg_arraylen, agctab_arraylen = 0;
	const u32 *agctab_array_table = NULL;
	const u32 *phy_regarray_table;
	int i;

	/* Normal chip, Mac0 use AGC_TAB.txt for 2G and 5G band. */
	if (rtlhal->interfaceindex == 0) {
		agctab_arraylen = AGCTAB_ARRAYLENGTH;
		agctab_array_table = rtl8192du_agctab_array;
		rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
			" ===> phy:MAC0, Rtl819XAGCTAB_Array\n");
	} else {
		if (rtlhal->current_bandtype == BAND_ON_2_4G) {
			agctab_arraylen = AGCTAB_2G_ARRAYLENGTH;
			agctab_array_table = rtl8192du_agctab_2garray;
			rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
				" ===> phy:MAC1, Rtl819XAGCTAB_2GArray\n");
		} else {
			agctab_arraylen = AGCTAB_5G_ARRAYLENGTH;
			agctab_array_table = rtl8192du_agctab_5garray;
			rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
				" ===> phy:MAC1, Rtl819XAGCTAB_5GArray\n");
		}
	}
	phy_reg_arraylen = PHY_REG_2T_ARRAYLENGTH;
	phy_regarray_table = rtl8192du_phy_reg_2tarray;
	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
		" ===> phy:Rtl819XPHY_REG_Array_PG\n");

	if (configtype == BASEBAND_CONFIG_PHY_REG) {
		for (i = 0; i < phy_reg_arraylen; i = i + 2) {
			rtl_addr_delay(phy_regarray_table[i]);
			rtl_set_bbreg(hw, phy_regarray_table[i], MASKDWORD,
				      phy_regarray_table[i + 1]);
			udelay(1);
			rtl_dbg(rtlpriv, COMP_INIT, DBG_TRACE,
				"The phy_regarray_table[0] is %x Rtl819XPHY_REGArray[1] is %x\n",
				phy_regarray_table[i],
				phy_regarray_table[i + 1]);
		}
	} else if (configtype == BASEBAND_CONFIG_AGC_TAB) {
		for (i = 0; i < agctab_arraylen; i = i + 2) {
			rtl_set_bbreg(hw, agctab_array_table[i],
				      MASKDWORD, agctab_array_table[i + 1]);

			/* Add 1us delay between BB/RF register setting. */
			udelay(1);

			rtl_dbg(rtlpriv, COMP_INIT, DBG_TRACE,
				"AGC table %u %u\n",
				agctab_array_table[i],
				agctab_array_table[i + 1]);
		}
		rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
			"Normal Chip, loaded AGC table\n");
	}
	return true;
}

static bool _rtl92du_phy_config_bb_pg(struct ieee80211_hw *hw, u8 configtype)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	const u32 *phy_regarray_table_pg;
	u16 phy_regarray_pg_len;
	int i;

	phy_regarray_pg_len = PHY_REG_ARRAY_PG_LENGTH;
	phy_regarray_table_pg = rtl8192du_phy_reg_array_pg;

	if (configtype == BASEBAND_CONFIG_PHY_REG) {
		for (i = 0; i < phy_regarray_pg_len; i = i + 3) {
			rtl_addr_delay(phy_regarray_table_pg[i]);
			rtl92d_store_pwrindex_diffrate_offset(hw,
				phy_regarray_table_pg[i],
				phy_regarray_table_pg[i + 1],
				phy_regarray_table_pg[i + 2]);
		}
	} else {
		rtl_dbg(rtlpriv, COMP_SEND, DBG_TRACE,
			"configtype != BaseBand_Config_PHY_REG\n");
	}
	return true;
}

static bool _rtl92du_phy_bb_config(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtlpriv);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	bool ret;

	rtl_dbg(rtlpriv, COMP_INIT, DBG_TRACE, "==>\n");
	ret = _rtl92du_phy_config_bb(hw, BASEBAND_CONFIG_PHY_REG);
	if (!ret) {
		pr_err("Write BB Reg Fail!!\n");
		return false;
	}

	if (!rtlefuse->autoload_failflag) {
		rtlphy->pwrgroup_cnt = 0;
		ret = _rtl92du_phy_config_bb_pg(hw, BASEBAND_CONFIG_PHY_REG);
	}
	if (!ret) {
		pr_err("BB_PG Reg Fail!!\n");
		return false;
	}

	ret = _rtl92du_phy_config_bb(hw, BASEBAND_CONFIG_AGC_TAB);
	if (!ret) {
		pr_err("AGC Table Fail\n");
		return false;
	}

	rtlphy->cck_high_power = (bool)rtl_get_bbreg(hw,
						     RFPGA0_XA_HSSIPARAMETER2,
						     0x200);

	return true;
}

bool rtl92du_phy_bb_config(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	bool rtstatus;
	u32 regvaldw;
	u16 regval;
	u8 value;

	rtl92d_phy_init_bb_rf_register_definition(hw);

	regval = rtl_read_word(rtlpriv, REG_SYS_FUNC_EN);
	rtl_write_word(rtlpriv, REG_SYS_FUNC_EN,
		       regval | BIT(13) | BIT(0) | BIT(1));

	rtl_write_byte(rtlpriv, REG_AFE_PLL_CTRL, 0x83);
	rtl_write_byte(rtlpriv, REG_AFE_PLL_CTRL + 1, 0xdb);

	/* 0x1f bit7 bit6 represent for mac0/mac1 driver ready */
	value = rtl_read_byte(rtlpriv, REG_RF_CTRL);
	rtl_write_byte(rtlpriv, REG_RF_CTRL, value | RF_EN | RF_RSTB |
		RF_SDMRSTB);

	value = FEN_BB_GLB_RSTN | FEN_BBRSTB;
	if (rtlhal->interface == INTF_PCI)
		value |= FEN_PPLL | FEN_PCIEA | FEN_DIO_PCIE;
	else if (rtlhal->interface == INTF_USB)
		value |= FEN_USBA | FEN_USBD;
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, value);

	regvaldw = rtl_read_dword(rtlpriv, RFPGA0_XCD_RFPARAMETER);
	regvaldw &= ~BIT(31);
	rtl_write_dword(rtlpriv, RFPGA0_XCD_RFPARAMETER, regvaldw);

	/* To Fix MAC loopback mode fail. */
	rtl_write_byte(rtlpriv, REG_LDOHCI12_CTRL, 0x0f);
	rtl_write_byte(rtlpriv, 0x15, 0xe9);

	rtl_write_byte(rtlpriv, REG_AFE_XTAL_CTRL + 1, 0x80);
	if (!(IS_92D_SINGLEPHY(rtlpriv->rtlhal.version)) &&
	    rtlhal->interface == INTF_PCI) {
		regvaldw = rtl_read_dword(rtlpriv, REG_LEDCFG0);
		rtl_write_dword(rtlpriv, REG_LEDCFG0, regvaldw | BIT(23));
	}

	rtstatus = _rtl92du_phy_bb_config(hw);

	/* Crystal calibration */
	rtl_set_bbreg(hw, REG_AFE_XTAL_CTRL, 0xf0,
		      rtlpriv->efuse.crystalcap & 0x0f);
	rtl_set_bbreg(hw, REG_AFE_PLL_CTRL, 0xf0000000,
		      (rtlpriv->efuse.crystalcap & 0xf0) >> 4);

	return rtstatus;
}

bool rtl92du_phy_rf_config(struct ieee80211_hw *hw)
{
	return rtl92du_phy_rf6052_config(hw);
}

bool rtl92du_phy_config_rf_with_headerfile(struct ieee80211_hw *hw,
					   enum rf_content content,
					   enum radio_path rfpath)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u16 radioa_arraylen, radiob_arraylen;
	const u32 *radioa_array_table;
	const u32 *radiob_array_table;
	int i;

	radioa_arraylen = RADIOA_2T_ARRAYLENGTH;
	radioa_array_table = rtl8192du_radioa_2tarray;
	radiob_arraylen = RADIOB_2T_ARRAYLENGTH;
	radiob_array_table = rtl8192du_radiob_2tarray;
	if (rtlpriv->efuse.internal_pa_5g[0]) {
		radioa_arraylen = RADIOA_2T_INT_PA_ARRAYLENGTH;
		radioa_array_table = rtl8192du_radioa_2t_int_paarray;
	}
	if (rtlpriv->efuse.internal_pa_5g[1]) {
		radiob_arraylen = RADIOB_2T_INT_PA_ARRAYLENGTH;
		radiob_array_table = rtl8192du_radiob_2t_int_paarray;
	}
	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
		"PHY_ConfigRFWithHeaderFile() Radio_A:Rtl819XRadioA_1TArray\n");
	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
		"PHY_ConfigRFWithHeaderFile() Radio_B:Rtl819XRadioB_1TArray\n");
	rtl_dbg(rtlpriv, COMP_INIT, DBG_TRACE, "Radio No %x\n", rfpath);

	/* this only happens when DMDP, mac0 start on 2.4G,
	 * mac1 start on 5G, mac 0 has to set phy0 & phy1
	 * pathA or mac1 has to set phy0 & phy1 pathA
	 */
	if (content == radiob_txt && rfpath == RF90_PATH_A) {
		rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
			" ===> althougth Path A, we load radiob.txt\n");
		radioa_arraylen = radiob_arraylen;
		radioa_array_table = radiob_array_table;
	}

	switch (rfpath) {
	case RF90_PATH_A:
		for (i = 0; i < radioa_arraylen; i = i + 2) {
			rtl_rfreg_delay(hw, rfpath, radioa_array_table[i],
					RFREG_OFFSET_MASK,
					radioa_array_table[i + 1]);
		}
		break;
	case RF90_PATH_B:
		for (i = 0; i < radiob_arraylen; i = i + 2) {
			rtl_rfreg_delay(hw, rfpath, radiob_array_table[i],
					RFREG_OFFSET_MASK,
					radiob_array_table[i + 1]);
		}
		break;
	case RF90_PATH_C:
	case RF90_PATH_D:
		pr_err("switch case %#x not processed\n", rfpath);
		break;
	}

	return true;
}

void rtl92du_phy_set_bw_mode(struct ieee80211_hw *hw,
			     enum nl80211_channel_type ch_type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_mac *mac = rtl_mac(rtlpriv);
	u8 reg_bw_opmode;
	u8 reg_prsr_rsc;

	if (rtlphy->set_bwmode_inprogress)
		return;

	if ((is_hal_stop(rtlhal)) || (RT_CANNOT_IO(hw))) {
		rtl_dbg(rtlpriv, COMP_ERR, DBG_WARNING,
			"FALSE driver sleep or unload\n");
		return;
	}

	rtlphy->set_bwmode_inprogress = true;

	rtl_dbg(rtlpriv, COMP_SCAN, DBG_TRACE, "Switch to %s bandwidth\n",
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
		pr_err("unknown bandwidth: %#X\n",
		       rtlphy->current_chan_bw);
		break;
	}

	switch (rtlphy->current_chan_bw) {
	case HT_CHANNEL_WIDTH_20:
		rtl92du_phy_set_bb_reg_1byte(hw, RFPGA0_RFMOD, BRFMOD, 0x0);
		rtl_set_bbreg(hw, RFPGA1_RFMOD, BRFMOD, 0x0);
		/* SET BIT10 BIT11  for receive cck */
		rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER2, BIT(10) | BIT(11), 3);
		break;
	case HT_CHANNEL_WIDTH_20_40:
		rtl92du_phy_set_bb_reg_1byte(hw, RFPGA0_RFMOD, BRFMOD, 0x1);
		rtl_set_bbreg(hw, RFPGA1_RFMOD, BRFMOD, 0x1);
		/* Set Control channel to upper or lower.
		 * These settings are required only for 40MHz
		 */
		if (rtlhal->current_bandtype == BAND_ON_2_4G)
			rtl_set_bbreg(hw, RCCK0_SYSTEM, BCCKSIDEBAND,
				      mac->cur_40_prime_sc >> 1);
		rtl_set_bbreg(hw, ROFDM1_LSTF, 0xC00, mac->cur_40_prime_sc);
		/* SET BIT10 BIT11  for receive cck */
		rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER2,
			      BIT(10) | BIT(11), 0);
		rtl_set_bbreg(hw, 0x818, BIT(26) | BIT(27),
			      mac->cur_40_prime_sc ==
			      HAL_PRIME_CHNL_OFFSET_LOWER ? 2 : 1);
		break;
	default:
		pr_err("unknown bandwidth: %#X\n",
		       rtlphy->current_chan_bw);
		break;
	}

	rtl92d_phy_rf6052_set_bandwidth(hw, rtlphy->current_chan_bw);

	rtlphy->set_bwmode_inprogress = false;
	rtl_dbg(rtlpriv, COMP_SCAN, DBG_TRACE, "<==\n");
}

static void _rtl92du_phy_stop_trx_before_changeband(struct ieee80211_hw *hw)
{
	rtl92du_phy_set_bb_reg_1byte(hw, RFPGA0_RFMOD, BCCKEN | BOFDMEN, 0);
	rtl_set_bbreg(hw, ROFDM0_TRXPATHENABLE, MASKBYTE0, 0x00);
	rtl_set_bbreg(hw, ROFDM1_TRXPATHENABLE, BDWORD, 0x0);
}

static void rtl92du_phy_switch_wirelessband(struct ieee80211_hw *hw, u8 band)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	u16 basic_rates;
	u32 reg_mac;
	u8 value8;

	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD, "==>\n");
	rtlhal->bandset = band;
	rtlhal->current_bandtype = band;
	if (IS_92D_SINGLEPHY(rtlhal->version))
		rtlhal->bandset = BAND_ON_BOTH;

	/* stop RX/Tx */
	_rtl92du_phy_stop_trx_before_changeband(hw);

	/* reconfig BB/RF according to wireless mode */
	if (rtlhal->current_bandtype == BAND_ON_2_4G)
		/* BB & RF Config */
		rtl_dbg(rtlpriv, COMP_CMD, DBG_DMESG, "====>2.4G\n");
	else
		/* 5G band */
		rtl_dbg(rtlpriv, COMP_CMD, DBG_DMESG, "====>5G\n");

	if (rtlhal->interfaceindex == 1)
		_rtl92du_phy_config_bb(hw, BASEBAND_CONFIG_AGC_TAB);

	rtl92du_update_bbrf_configuration(hw);

	basic_rates = RRSR_6M | RRSR_12M | RRSR_24M;
	if (rtlhal->current_bandtype == BAND_ON_2_4G)
		basic_rates |= RRSR_1M | RRSR_2M | RRSR_5_5M | RRSR_11M;
	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_BASIC_RATE,
				      (u8 *)&basic_rates);

	rtl92du_phy_set_bb_reg_1byte(hw, RFPGA0_RFMOD, BCCKEN | BOFDMEN, 0x3);

	/* 20M BW. */
	/* rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER2, BIT(10), 1); */
	rtlhal->reloadtxpowerindex = true;

	reg_mac = rtlhal->interfaceindex == 0 ? REG_MAC0 : REG_MAC1;

	/* notice fw know band status  0x81[1]/0x53[1] = 0: 5G, 1: 2G */
	if (rtlhal->current_bandtype == BAND_ON_2_4G) {
		value8 = rtl_read_byte(rtlpriv,	reg_mac);
		value8 |= BIT(1);
		rtl_write_byte(rtlpriv, reg_mac, value8);
	} else {
		value8 = rtl_read_byte(rtlpriv, reg_mac);
		value8 &= ~BIT(1);
		rtl_write_byte(rtlpriv, reg_mac, value8);
	}
	mdelay(1);
	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD, "<==Switch Band OK\n");
}

static void _rtl92du_phy_reload_imr_setting(struct ieee80211_hw *hw,
					    u8 channel, u8 rfpath)
{
	struct rtl_usb *rtlusb = rtl_usbdev(rtl_usbpriv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 group, i;

	if (rtlusb->udev->speed != USB_SPEED_HIGH)
		return;

	rtl_dbg(rtlpriv, COMP_CMD, DBG_LOUD, "====>path %d\n", rfpath);
	if (rtlpriv->rtlhal.current_bandtype == BAND_ON_5G) {
		rtl_dbg(rtlpriv, COMP_CMD, DBG_LOUD, "====>5G\n");
		rtl92du_phy_set_bb_reg_1byte(hw, RFPGA0_RFMOD,
					     BOFDMEN | BCCKEN, 0);
		rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER4, 0x00f00000, 0xf);

		/* fc area 0xd2c */
		if (channel >= 149)
			rtl_set_bbreg(hw, ROFDM1_CFOTRACKING, BIT(13) |
				      BIT(14), 2);
		else
			rtl_set_bbreg(hw, ROFDM1_CFOTRACKING, BIT(13) |
				      BIT(14), 1);

		/* leave 0 for channel1-14. */
		group = channel <= 64 ? 1 : 2;
		for (i = 0; i < MAX_RF_IMR_INDEX_NORMAL; i++)
			rtl_set_rfreg(hw, (enum radio_path)rfpath,
				      rf_reg_for_5g_swchnl_normal[i],
				      RFREG_OFFSET_MASK,
				      rf_imr_param_normal[group][i]);

		rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER4, 0x00f00000, 0);
		rtl92du_phy_set_bb_reg_1byte(hw, RFPGA0_RFMOD,
					     BOFDMEN | BCCKEN, 3);
	} else {
		/* G band. */
		rtl_dbg(rtlpriv, COMP_SCAN, DBG_LOUD,
			"Load RF IMR parameters for G band. IMR already setting %d\n",
			rtlpriv->rtlhal.load_imrandiqk_setting_for2g);
		rtl_dbg(rtlpriv, COMP_CMD, DBG_LOUD, "====>2.4G\n");

		if (!rtlpriv->rtlhal.load_imrandiqk_setting_for2g) {
			rtl_dbg(rtlpriv, COMP_SCAN, DBG_LOUD,
				"Load RF IMR parameters for G band. %d\n",
				rfpath);
			rtl92du_phy_set_bb_reg_1byte(hw, RFPGA0_RFMOD,
						     BOFDMEN | BCCKEN, 0);
			rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER4,
				      0x00f00000, 0xf);

			for (i = 0; i < MAX_RF_IMR_INDEX_NORMAL; i++) {
				rtl_set_rfreg(hw, (enum radio_path)rfpath,
					      rf_reg_for_5g_swchnl_normal[i],
					      RFREG_OFFSET_MASK,
					      rf_imr_param_normal[0][i]);
			}

			rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER4,
				      0x00f00000, 0);
			rtl92du_phy_set_bb_reg_1byte(hw, RFPGA0_RFMOD,
						     BOFDMEN | BCCKEN, 3);
		}
	}
	rtl_dbg(rtlpriv, COMP_CMD, DBG_LOUD, "<====\n");
}

static void _rtl92du_phy_switch_rf_setting(struct ieee80211_hw *hw, u8 channel)
{
	struct rtl_usb *rtlusb = rtl_usbdev(rtl_usbpriv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = &rtlpriv->rtlhal;
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 path = rtlhal->current_bandtype == BAND_ON_5G ? RF90_PATH_A
							 : RF90_PATH_B;
	u32 u4regvalue, mask = 0x1C000, value = 0, u4tmp, u4tmp2;
	bool need_pwr_down = false, internal_pa = false;
	u32 regb30 = rtl_get_bbreg(hw, 0xb30, BIT(27));
	u8 index = 0, i, rfpath;

	if (rtlusb->udev->speed != USB_SPEED_HIGH)
		return;

	rtl_dbg(rtlpriv, COMP_CMD, DBG_LOUD, "====>\n");
	/* config path A for 5G */
	if (rtlhal->current_bandtype == BAND_ON_5G) {
		rtl_dbg(rtlpriv, COMP_CMD, DBG_LOUD, "====>5G\n");
		u4tmp = rtlpriv->curveindex_5g[channel - 1];
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
		else if (channel == 151 || channel == 153 || channel == 163 ||
			 channel == 165)
			index = 3;
		else if (channel == 157 || channel == 159)
			index = 4;

		if (rtlhal->macphymode == DUALMAC_DUALPHY &&
		    rtlhal->interfaceindex == 1) {
			need_pwr_down = rtl92du_phy_enable_anotherphy(hw, false);
			rtlhal->during_mac1init_radioa = true;
			/* asume no this case */
			if (need_pwr_down)
				rtl92d_phy_enable_rf_env(hw, path,
							 &u4regvalue);
		}

		/* DMDP, if band = 5G, Mac0 need to set PHY1 when regB30[27]=1 */
		if (regb30 && rtlhal->interfaceindex == 0) {
			need_pwr_down = rtl92du_phy_enable_anotherphy(hw, true);
			rtlhal->during_mac0init_radiob = true;
			if (need_pwr_down)
				rtl92d_phy_enable_rf_env(hw, path,
							 &u4regvalue);
		}

		for (i = 0; i < RF_REG_NUM_FOR_C_CUT_5G; i++) {
			if (i == 0 && rtlhal->macphymode == DUALMAC_DUALPHY) {
				rtl_set_rfreg(hw, (enum radio_path)path,
					      rf_reg_for_c_cut_5g[i],
					      RFREG_OFFSET_MASK, 0xE439D);
			} else if (rf_reg_for_c_cut_5g[i] == RF_SYN_G4) {
				u4tmp2 = (rf_reg_pram_c_5g[index][i] &
				     0x7FF) | (u4tmp << 11);
				if (channel == 36)
					u4tmp2 &= ~(BIT(7) | BIT(6));
				rtl_set_rfreg(hw, (enum radio_path)path,
					      rf_reg_for_c_cut_5g[i],
					      RFREG_OFFSET_MASK, u4tmp2);
			} else {
				rtl_set_rfreg(hw, (enum radio_path)path,
					      rf_reg_for_c_cut_5g[i],
					      RFREG_OFFSET_MASK,
					      rf_reg_pram_c_5g[index][i]);
			}
			rtl_dbg(rtlpriv, COMP_RF, DBG_TRACE,
				"offset 0x%x value 0x%x path %d index %d readback 0x%x\n",
				rf_reg_for_c_cut_5g[i],
				rf_reg_pram_c_5g[index][i],
				path, index,
				rtl_get_rfreg(hw, (enum radio_path)path,
					      rf_reg_for_c_cut_5g[i],
					      RFREG_OFFSET_MASK));
		}
		if (rtlhal->macphymode == DUALMAC_DUALPHY &&
		    rtlhal->interfaceindex == 1) {
			if (need_pwr_down)
				rtl92d_phy_restore_rf_env(hw, path, &u4regvalue);

			rtl92du_phy_powerdown_anotherphy(hw, false);
		}

		if (regb30 && rtlhal->interfaceindex == 0) {
			if (need_pwr_down)
				rtl92d_phy_restore_rf_env(hw, path, &u4regvalue);

			rtl92du_phy_powerdown_anotherphy(hw, true);
		}

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
			    rtlhal->interfaceindex == 1) /* MAC 1 5G */
				internal_pa = rtlpriv->efuse.internal_pa_5g[1];
			else
				internal_pa =
					 rtlpriv->efuse.internal_pa_5g[rfpath];

			if (internal_pa) {
				for (i = 0;
				     i < RF_REG_NUM_FOR_C_CUT_5G_INTERNALPA;
				     i++) {
					if (rf_for_c_cut_5g_internal_pa[i] == 0x03 &&
					    channel >= 36 && channel <= 64)
						rtl_set_rfreg(hw, rfpath,
							rf_for_c_cut_5g_internal_pa[i],
							RFREG_OFFSET_MASK,
							0x7bdef);
					else
						rtl_set_rfreg(hw, rfpath,
							rf_for_c_cut_5g_internal_pa[i],
							RFREG_OFFSET_MASK,
							rf_pram_c_5g_int_pa[index][i]);
					rtl_dbg(rtlpriv, COMP_RF, DBG_LOUD,
						"offset 0x%x value 0x%x path %d index %d\n",
						rf_for_c_cut_5g_internal_pa[i],
						rf_pram_c_5g_int_pa[index][i],
						rfpath, index);
				}
			} else {
				rtl_set_rfreg(hw, (enum radio_path)rfpath, RF_TXPA_AG,
					      mask, value);
			}
		}
	} else if (rtlhal->current_bandtype == BAND_ON_2_4G) {
		rtl_dbg(rtlpriv, COMP_CMD, DBG_LOUD, "====>2.4G\n");
		u4tmp = rtlpriv->curveindex_2g[channel - 1];
		RTPRINT(rtlpriv, FINIT, INIT_IQK,
			"ver 3 set RF-B, 2G, 0x28 = 0x%x !!\n", u4tmp);

		if (channel == 1 || channel == 2 || channel == 4 ||
		    channel == 9 || channel == 10 || channel == 11 ||
		    channel == 12)
			index = 0;
		else if (channel == 3 || channel == 13 || channel == 14)
			index = 1;
		else if (channel >= 5 && channel <= 8)
			index = 2;

		if (rtlhal->macphymode == DUALMAC_DUALPHY) {
			path = RF90_PATH_A;
			if (rtlhal->interfaceindex == 0) {
				need_pwr_down =
					 rtl92du_phy_enable_anotherphy(hw, true);
				rtlhal->during_mac0init_radiob = true;

				if (need_pwr_down)
					rtl92d_phy_enable_rf_env(hw, path,
								 &u4regvalue);
			}

			/* DMDP, if band = 2G, MAC1 need to set PHY0 when regB30[27]=1 */
			if (regb30 && rtlhal->interfaceindex == 1) {
				need_pwr_down =
					 rtl92du_phy_enable_anotherphy(hw, false);
				rtlhal->during_mac1init_radioa = true;

				if (need_pwr_down)
					rtl92d_phy_enable_rf_env(hw, path,
								 &u4regvalue);
			}
		}

		for (i = 0; i < RF_REG_NUM_FOR_C_CUT_2G; i++) {
			if (rf_reg_for_c_cut_2g[i] == RF_SYN_G7)
				rtl_set_rfreg(hw, (enum radio_path)path,
					      rf_reg_for_c_cut_2g[i],
					      RFREG_OFFSET_MASK,
					      rf_reg_param_for_c_cut_2g[index][i] |
					      BIT(17));
			else
				rtl_set_rfreg(hw, (enum radio_path)path,
					      rf_reg_for_c_cut_2g[i],
					      RFREG_OFFSET_MASK,
					      rf_reg_param_for_c_cut_2g
					      [index][i]);

			rtl_dbg(rtlpriv, COMP_RF, DBG_TRACE,
				"offset 0x%x value 0x%x mak 0x%x path %d index %d readback 0x%x\n",
				rf_reg_for_c_cut_2g[i],
				rf_reg_param_for_c_cut_2g[index][i],
				rf_reg_mask_for_c_cut_2g[i], path, index,
				rtl_get_rfreg(hw, (enum radio_path)path,
					      rf_reg_for_c_cut_2g[i],
					      RFREG_OFFSET_MASK));
		}
		RTPRINT(rtlpriv, FINIT, INIT_IQK,
			"cosa ver 3 set RF-B, 2G, 0x28 = 0x%x !!\n",
			rf_syn_g4_for_c_cut_2g | (u4tmp << 11));

		rtl_set_rfreg(hw, (enum radio_path)path, RF_SYN_G4,
			      RFREG_OFFSET_MASK,
			      rf_syn_g4_for_c_cut_2g | (u4tmp << 11));

		if (rtlhal->macphymode == DUALMAC_DUALPHY &&
		    rtlhal->interfaceindex == 0) {
			if (need_pwr_down)
				rtl92d_phy_restore_rf_env(hw, path, &u4regvalue);

			rtl92du_phy_powerdown_anotherphy(hw, true);
		}

		if (regb30 && rtlhal->interfaceindex == 1) {
			if (need_pwr_down)
				rtl92d_phy_restore_rf_env(hw, path, &u4regvalue);

			rtl92du_phy_powerdown_anotherphy(hw, false);
		}
	}
	rtl_dbg(rtlpriv, COMP_CMD, DBG_LOUD, "<====\n");
}

/* bit0 = 1 => Tx OK, bit1 = 1 => Rx OK */
static u8 _rtl92du_phy_patha_iqk(struct ieee80211_hw *hw, bool configpathb)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u32 regeac, rege94, rege9c, regea4;
	u8 result = 0;

	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "Path-A IQK setting!\n");

	if (rtlhal->interfaceindex == 0) {
		rtl_set_bbreg(hw, RTX_IQK_TONE_A, MASKDWORD, 0x10008c1f);
		rtl_set_bbreg(hw, RRX_IQK_TONE_A, MASKDWORD, 0x10008c1f);
	} else {
		rtl_set_bbreg(hw, RTX_IQK_TONE_A, MASKDWORD, 0x10008c22);
		rtl_set_bbreg(hw, RRX_IQK_TONE_A, MASKDWORD, 0x10008c22);
	}
	rtl_set_bbreg(hw, RTX_IQK_PI_A, MASKDWORD, 0x82140102);
	rtl_set_bbreg(hw, RRX_IQK_PI_A, MASKDWORD,
		      configpathb ? 0x28160202 : 0x28160502);
	/* path-B IQK setting */
	if (configpathb) {
		rtl_set_bbreg(hw, RTX_IQK_TONE_B, MASKDWORD, 0x10008c22);
		rtl_set_bbreg(hw, RRX_IQK_TONE_B, MASKDWORD, 0x10008c22);
		rtl_set_bbreg(hw, RTX_IQK_PI_B, MASKDWORD, 0x82140102);
		rtl_set_bbreg(hw, RRX_IQK_PI_B, MASKDWORD, 0x28160206);
	}

	/* LO calibration setting */
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "LO calibration setting!\n");
	rtl_set_bbreg(hw, RIQK_AGC_RSP, MASKDWORD, 0x00462911);

	/* One shot, path A LOK & IQK */
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "One shot, path A LOK & IQK!\n");
	rtl_set_bbreg(hw, RIQK_AGC_PTS, MASKDWORD, 0xf9000000);
	rtl_set_bbreg(hw, RIQK_AGC_PTS, MASKDWORD, 0xf8000000);

	RTPRINT(rtlpriv, FINIT, INIT_IQK,
		"Delay %d ms for One shot, path A LOK & IQK\n",
		IQK_DELAY_TIME);
	mdelay(IQK_DELAY_TIME);

	/* Check failed */
	regeac = rtl_get_bbreg(hw, RRX_POWER_AFTER_IQK_A_2, MASKDWORD);
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "0xeac = 0x%x\n", regeac);
	rege94 = rtl_get_bbreg(hw, RTX_POWER_BEFORE_IQK_A, MASKDWORD);
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "0xe94 = 0x%x\n", rege94);
	rege9c = rtl_get_bbreg(hw, RTX_POWER_AFTER_IQK_A, MASKDWORD);
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "0xe9c = 0x%x\n", rege9c);
	regea4 = rtl_get_bbreg(hw, RRX_POWER_BEFORE_IQK_A_2, MASKDWORD);
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "0xea4 = 0x%x\n", regea4);

	if (!(regeac & BIT(28)) &&
	    (((rege94 & 0x03FF0000) >> 16) != 0x142) &&
	    (((rege9c & 0x03FF0000) >> 16) != 0x42))
		result |= 0x01;
	else /* if Tx not OK, ignore Rx */
		return result;

	/* if Tx is OK, check whether Rx is OK */
	if (!(regeac & BIT(27)) &&
	    (((regea4 & 0x03FF0000) >> 16) != 0x132) &&
	    (((regeac & 0x03FF0000) >> 16) != 0x36))
		result |= 0x02;
	else
		RTPRINT(rtlpriv, FINIT, INIT_IQK,  "Path A Rx IQK fail!!\n");

	return result;
}

/* bit0 = 1 => Tx OK, bit1 = 1 => Rx OK */
static u8 _rtl92du_phy_patha_iqk_5g_normal(struct ieee80211_hw *hw,
					   bool configpathb)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u32 TXOKBIT = BIT(28), RXOKBIT = BIT(27);
	u32 regeac, rege94, rege9c, regea4;
	u8 timeout = 20, timecount = 0;
	u8 retrycount = 2;
	u8 result = 0;
	u8 i;

	if (rtlhal->interfaceindex == 1) { /* PHY1 */
		TXOKBIT = BIT(31);
		RXOKBIT = BIT(30);
	}

	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "Path-A IQK setting!\n");
	rtl_set_bbreg(hw, RTX_IQK_TONE_A, MASKDWORD, 0x18008c1f);
	rtl_set_bbreg(hw, RRX_IQK_TONE_A, MASKDWORD, 0x18008c1f);
	rtl_set_bbreg(hw, RTX_IQK_PI_A, MASKDWORD, 0x82140307);
	rtl_set_bbreg(hw, RRX_IQK_PI_A, MASKDWORD, 0x68160960);
	/* path-B IQK setting */
	if (configpathb) {
		rtl_set_bbreg(hw, RTX_IQK_TONE_B, MASKDWORD, 0x18008c2f);
		rtl_set_bbreg(hw, RRX_IQK_TONE_B, MASKDWORD, 0x18008c2f);
		rtl_set_bbreg(hw, RTX_IQK_PI_B, MASKDWORD, 0x82110000);
		rtl_set_bbreg(hw, RRX_IQK_PI_B, MASKDWORD, 0x68110000);
	}

	/* LO calibration setting */
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "LO calibration setting!\n");
	rtl_set_bbreg(hw, RIQK_AGC_RSP, MASKDWORD, 0x00462911);

	/* path-A PA on */
	rtl_set_bbreg(hw, RFPGA0_XAB_RFINTERFACESW, MASKDWORD, 0x07000f60);
	rtl_set_bbreg(hw, RFPGA0_XA_RFINTERFACEOE, MASKDWORD, 0x66e60e30);

	for (i = 0; i < retrycount; i++) {
		/* One shot, path A LOK & IQK */
		RTPRINT(rtlpriv, FINIT, INIT_IQK,
			"One shot, path A LOK & IQK!\n");
		rtl_set_bbreg(hw, RIQK_AGC_PTS, MASKDWORD, 0xf9000000);
		rtl_set_bbreg(hw, RIQK_AGC_PTS, MASKDWORD, 0xf8000000);

		RTPRINT(rtlpriv, FINIT, INIT_IQK,
			"Delay %d ms for One shot, path A LOK & IQK.\n",
			IQK_DELAY_TIME);
		mdelay(IQK_DELAY_TIME * 10);

		while (timecount < timeout &&
		       rtl_get_bbreg(hw, RRX_POWER_AFTER_IQK_A_2, BIT(26)) == 0) {
			udelay(IQK_DELAY_TIME * 1000 * 2);
			timecount++;
		}

		timecount = 0;
		while (timecount < timeout &&
		       rtl_get_bbreg(hw, RRX_POWER_BEFORE_IQK_A_2, MASK_IQK_RESULT) == 0) {
			udelay(IQK_DELAY_TIME * 1000 * 2);
			timecount++;
		}

		/* Check failed */
		regeac = rtl_get_bbreg(hw, RRX_POWER_AFTER_IQK_A_2, MASKDWORD);
		RTPRINT(rtlpriv, FINIT, INIT_IQK,  "0xeac = 0x%x\n", regeac);
		rege94 = rtl_get_bbreg(hw, RTX_POWER_BEFORE_IQK_A, MASKDWORD);
		RTPRINT(rtlpriv, FINIT, INIT_IQK,  "0xe94 = 0x%x\n", rege94);
		rege9c = rtl_get_bbreg(hw, RTX_POWER_AFTER_IQK_A, MASKDWORD);
		RTPRINT(rtlpriv, FINIT, INIT_IQK,  "0xe9c = 0x%x\n", rege9c);
		regea4 = rtl_get_bbreg(hw, RRX_POWER_BEFORE_IQK_A_2, MASKDWORD);
		RTPRINT(rtlpriv, FINIT, INIT_IQK,  "0xea4 = 0x%x\n", regea4);

		if (!(regeac & TXOKBIT) &&
		    (((rege94 & 0x03FF0000) >> 16) != 0x142)) {
			result |= 0x01;
		} else { /* if Tx not OK, ignore Rx */
			RTPRINT(rtlpriv, FINIT, INIT_IQK,
				"Path A Tx IQK fail!!\n");
			continue;
		}

		/* if Tx is OK, check whether Rx is OK */
		if (!(regeac & RXOKBIT) &&
		    (((regea4 & 0x03FF0000) >> 16) != 0x132)) {
			result |= 0x02;
			break;
		}
		RTPRINT(rtlpriv, FINIT, INIT_IQK, "Path A Rx IQK fail!!\n");
	}

	/* path A PA off */
	rtl_set_bbreg(hw, RFPGA0_XAB_RFINTERFACESW, MASKDWORD,
		      rtlphy->iqk_bb_backup[0]);
	rtl_set_bbreg(hw, RFPGA0_XA_RFINTERFACEOE, MASKDWORD,
		      rtlphy->iqk_bb_backup[1]);

	if (!(result & 0x01)) /* Tx IQK fail */
		rtl_set_bbreg(hw, RTX_IQK_TONE_A, MASKDWORD, 0x19008c00);

	if (!(result & 0x02)) { /* Rx IQK fail */
		rtl_set_bbreg(hw, ROFDM0_XARXIQIMBALANCE, MASKDWORD, 0x40000100);
		rtl_set_bbreg(hw, RRX_IQK_TONE_A, MASKDWORD, 0x19008c00);

		RTPRINT(rtlpriv, FINIT, INIT_IQK,
			"Path A Rx IQK fail!! 0xe34 = %#x\n",
			rtl_get_bbreg(hw, RRX_IQK_TONE_A, MASKDWORD));
	}

	return result;
}

/* bit0 = 1 => Tx OK, bit1 = 1 => Rx OK */
static u8 _rtl92du_phy_pathb_iqk(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 regeac, regeb4, regebc, regec4, regecc;
	u8 result = 0;

	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "One shot, path B LOK & IQK!\n");
	rtl_set_bbreg(hw, RIQK_AGC_CONT, MASKDWORD, 0x00000002);
	rtl_set_bbreg(hw, RIQK_AGC_CONT, MASKDWORD, 0x00000000);

	RTPRINT(rtlpriv, FINIT, INIT_IQK,
		"Delay %d ms for One shot, path B LOK & IQK\n", IQK_DELAY_TIME);
	mdelay(IQK_DELAY_TIME);

	/* Check failed */
	regeac = rtl_get_bbreg(hw, RRX_POWER_AFTER_IQK_A_2, MASKDWORD);
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "0xeac = 0x%x\n", regeac);
	regeb4 = rtl_get_bbreg(hw, RTX_POWER_BEFORE_IQK_B, MASKDWORD);
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "0xeb4 = 0x%x\n", regeb4);
	regebc = rtl_get_bbreg(hw, RTX_POWER_AFTER_IQK_B, MASKDWORD);
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "0xebc = 0x%x\n", regebc);
	regec4 = rtl_get_bbreg(hw, RRX_POWER_BEFORE_IQK_B_2, MASKDWORD);
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "0xec4 = 0x%x\n", regec4);
	regecc = rtl_get_bbreg(hw, RRX_POWER_AFTER_IQK_B_2, MASKDWORD);
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "0xecc = 0x%x\n", regecc);

	if (!(regeac & BIT(31)) &&
	    (((regeb4 & 0x03FF0000) >> 16) != 0x142) &&
	    (((regebc & 0x03FF0000) >> 16) != 0x42))
		result |= 0x01;
	else
		return result;

	if (!(regeac & BIT(30)) &&
	    (((regec4 & 0x03FF0000) >> 16) != 0x132) &&
	    (((regecc & 0x03FF0000) >> 16) != 0x36))
		result |= 0x02;
	else
		RTPRINT(rtlpriv, FINIT, INIT_IQK,  "Path B Rx IQK fail!!\n");

	return result;
}

/* bit0 = 1 => Tx OK, bit1 = 1 => Rx OK */
static u8 _rtl92du_phy_pathb_iqk_5g_normal(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u32 regeac, regeb4, regebc, regec4, regecc;
	u8 timeout = 20, timecount = 0;
	u8 retrycount = 2;
	u8 result = 0;
	u8 i;

	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "Path-B IQK setting!\n");
	rtl_set_bbreg(hw, RTX_IQK_TONE_A, MASKDWORD, 0x18008c1f);
	rtl_set_bbreg(hw, RRX_IQK_TONE_A, MASKDWORD, 0x18008c1f);
	rtl_set_bbreg(hw, RTX_IQK_PI_A, MASKDWORD, 0x82110000);
	rtl_set_bbreg(hw, RRX_IQK_PI_A, MASKDWORD, 0x68110000);

	/* path-B IQK setting */
	rtl_set_bbreg(hw, RTX_IQK_TONE_B, MASKDWORD, 0x18008c2f);
	rtl_set_bbreg(hw, RRX_IQK_TONE_B, MASKDWORD, 0x18008c2f);
	rtl_set_bbreg(hw, RTX_IQK_PI_B, MASKDWORD, 0x82140307);
	rtl_set_bbreg(hw, RRX_IQK_PI_B, MASKDWORD, 0x68160960);

	/* LO calibration setting */
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "LO calibration setting!\n");
	rtl_set_bbreg(hw, RIQK_AGC_RSP, MASKDWORD, 0x00462911);

	/* path-B PA on */
	rtl_set_bbreg(hw, RFPGA0_XAB_RFINTERFACESW, MASKDWORD, 0x0f600700);
	rtl_set_bbreg(hw, RFPGA0_XB_RFINTERFACEOE, MASKDWORD, 0x061f0d30);

	for (i = 0; i < retrycount; i++) {
		/* One shot, path B LOK & IQK */
		RTPRINT(rtlpriv, FINIT, INIT_IQK,
			"One shot, path A LOK & IQK!\n");
		rtl_set_bbreg(hw, RIQK_AGC_PTS, MASKDWORD, 0xfa000000);
		rtl_set_bbreg(hw, RIQK_AGC_PTS, MASKDWORD, 0xf8000000);

		RTPRINT(rtlpriv, FINIT, INIT_IQK,
			"Delay %d ms for One shot, path B LOK & IQK.\n", 10);
		mdelay(IQK_DELAY_TIME * 10);

		while (timecount < timeout &&
		       rtl_get_bbreg(hw, RRX_POWER_AFTER_IQK_A_2, BIT(29)) == 0) {
			udelay(IQK_DELAY_TIME * 1000 * 2);
			timecount++;
		}

		timecount = 0;
		while (timecount < timeout &&
		       rtl_get_bbreg(hw, RRX_POWER_BEFORE_IQK_B_2, MASK_IQK_RESULT) == 0) {
			udelay(IQK_DELAY_TIME * 1000 * 2);
			timecount++;
		}

		/* Check failed */
		regeac = rtl_get_bbreg(hw, RRX_POWER_AFTER_IQK_A_2, MASKDWORD);
		RTPRINT(rtlpriv, FINIT, INIT_IQK,  "0xeac = 0x%x\n", regeac);
		regeb4 = rtl_get_bbreg(hw, RTX_POWER_BEFORE_IQK_B, MASKDWORD);
		RTPRINT(rtlpriv, FINIT, INIT_IQK,  "0xeb4 = 0x%x\n", regeb4);
		regebc = rtl_get_bbreg(hw, RTX_POWER_AFTER_IQK_B, MASKDWORD);
		RTPRINT(rtlpriv, FINIT, INIT_IQK,  "0xebc = 0x%x\n", regebc);
		regec4 = rtl_get_bbreg(hw, RRX_POWER_BEFORE_IQK_B_2, MASKDWORD);
		RTPRINT(rtlpriv, FINIT, INIT_IQK,  "0xec4 = 0x%x\n", regec4);
		regecc = rtl_get_bbreg(hw, RRX_POWER_AFTER_IQK_B_2, MASKDWORD);
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
		}

		RTPRINT(rtlpriv, FINIT, INIT_IQK, "Path B Rx IQK fail!!\n");
	}

	/* path B PA off */
	rtl_set_bbreg(hw, RFPGA0_XAB_RFINTERFACESW, MASKDWORD,
		      rtlphy->iqk_bb_backup[0]);
	rtl_set_bbreg(hw, RFPGA0_XB_RFINTERFACEOE, MASKDWORD,
		      rtlphy->iqk_bb_backup[2]);

	if (!(result & 0x01))
		rtl_set_bbreg(hw, RTX_IQK_TONE_B, MASKDWORD, 0x19008c00);

	if (!(result & 0x02)) {
		rtl_set_bbreg(hw, ROFDM0_XBRXIQIMBALANCE, MASKDWORD, 0x40000100);
		rtl_set_bbreg(hw, RRX_IQK_TONE_B, MASKDWORD, 0x19008c00);

		RTPRINT(rtlpriv, FINIT, INIT_IQK,
			"Path B Rx IQK fail!! 0xe54 = %#x\n",
			rtl_get_bbreg(hw, RRX_IQK_TONE_B, MASKDWORD));
	}

	return result;
}

static void _rtl92du_phy_reload_adda_registers(struct ieee80211_hw *hw,
					       const u32 *adda_reg,
					       u32 *adda_backup, u32 regnum)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 i;

	RTPRINT(rtlpriv, FINIT, INIT_IQK,
		"Reload ADDA power saving parameters !\n");
	for (i = 0; i < regnum; i++) {
		/* path-A/B BB to initial gain */
		if (adda_reg[i] == ROFDM0_XAAGCCORE1 ||
		    adda_reg[i] == ROFDM0_XBAGCCORE1)
			rtl_set_bbreg(hw, adda_reg[i], MASKDWORD, 0x50);

		rtl_set_bbreg(hw, adda_reg[i], MASKDWORD, adda_backup[i]);
	}
}

static void _rtl92du_phy_reload_mac_registers(struct ieee80211_hw *hw,
					      const u32 *macreg, u32 *macbackup)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 i;

	RTPRINT(rtlpriv, FINIT, INIT_IQK, "Reload MAC parameters !\n");
	for (i = 0; i < (IQK_MAC_REG_NUM - 1); i++)
		rtl_write_byte(rtlpriv, macreg[i], (u8)macbackup[i]);
	rtl_write_dword(rtlpriv, macreg[i], macbackup[i]);
}

static void _rtl92du_phy_patha_standby(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	RTPRINT(rtlpriv, FINIT, INIT_IQK, "Path-A standby mode!\n");

	rtl_set_bbreg(hw, RFPGA0_IQK, MASKH3BYTES, 0x0);
	rtl_set_bbreg(hw, RFPGA0_XA_LSSIPARAMETER, MASKDWORD, 0x00010000);
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKH3BYTES, 0x808000);
}

static void _rtl92du_phy_pimode_switch(struct ieee80211_hw *hw, bool pi_mode)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 mode;

	RTPRINT(rtlpriv, FINIT, INIT_IQK,
		"BB Switch to %s mode!\n", pi_mode ? "PI" : "SI");
	mode = pi_mode ? 0x01000100 : 0x01000000;
	rtl_set_bbreg(hw, RFPGA0_XA_HSSIPARAMETER1, MASKDWORD, mode);
	rtl_set_bbreg(hw, RFPGA0_XB_HSSIPARAMETER1, MASKDWORD, mode);
}

static void _rtl92du_phy_iq_calibrate(struct ieee80211_hw *hw, long result[][8],
				      u8 t, bool is2t)
{
	static const u32 adda_reg[IQK_ADDA_REG_NUM] = {
		RFPGA0_XCD_SWITCHCONTROL, RBLUE_TOOTH, RRX_WAIT_CCA,
		RTX_CCK_RFON, RTX_CCK_BBON, RTX_OFDM_RFON, RTX_OFDM_BBON,
		RTX_TO_RX, RTX_TO_TX, RRX_CCK, RRX_OFDM, RRX_WAIT_RIFS,
		RRX_TO_RX, RSTANDBY, RSLEEP, RPMPD_ANAEN
	};
	static const u32 iqk_mac_reg[IQK_MAC_REG_NUM] = {
		REG_TXPAUSE, REG_BCN_CTRL, REG_BCN_CTRL_1, REG_GPIO_MUXCFG
	};
	static const u32 iqk_bb_reg[IQK_BB_REG_NUM] = {
		RFPGA0_XAB_RFINTERFACESW, RFPGA0_XA_RFINTERFACEOE,
		RFPGA0_XB_RFINTERFACEOE, ROFDM0_TRMUXPAR,
		RFPGA0_XCD_RFINTERFACESW, ROFDM0_TRXPATHENABLE,
		RFPGA0_RFMOD, RFPGA0_ANALOGPARAMETER4,
		ROFDM0_XAAGCCORE1, ROFDM0_XBAGCCORE1
	};
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	const u32 retrycount = 2;
	u8 patha_ok, pathb_ok;
	u32 bbvalue;
	u32 i;

	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "IQK for 2.4G :Start!!!\n");
	if (t == 0) {
		bbvalue = rtl_get_bbreg(hw, RFPGA0_RFMOD, MASKDWORD);
		RTPRINT(rtlpriv, FINIT, INIT_IQK,  "==>0x%08x\n", bbvalue);
		RTPRINT(rtlpriv, FINIT, INIT_IQK, "IQ Calibration for %s\n",
			is2t ? "2T2R" : "1T1R");

		/*  Save ADDA parameters, turn Path A ADDA on */
		rtl92d_phy_save_adda_registers(hw, adda_reg,
					       rtlphy->adda_backup,
					       IQK_ADDA_REG_NUM);
		rtl92d_phy_save_mac_registers(hw, iqk_mac_reg,
					      rtlphy->iqk_mac_backup);
		rtl92d_phy_save_adda_registers(hw, iqk_bb_reg,
					       rtlphy->iqk_bb_backup,
					       IQK_BB_REG_NUM);
	}
	rtl92d_phy_path_adda_on(hw, adda_reg, true, is2t);

	rtl_set_bbreg(hw, RPDP_ANTA, MASKDWORD, 0x01017038);

	if (t == 0)
		rtlphy->rfpi_enable = (u8)rtl_get_bbreg(hw,
				RFPGA0_XA_HSSIPARAMETER1, BIT(8));

	/*  Switch BB to PI mode to do IQ Calibration. */
	if (!rtlphy->rfpi_enable)
		_rtl92du_phy_pimode_switch(hw, true);

	rtl92du_phy_set_bb_reg_1byte(hw, RFPGA0_RFMOD, BCCKEN, 0x00);
	rtl_set_bbreg(hw, ROFDM0_TRXPATHENABLE, MASKDWORD, 0x03a05600);
	rtl_set_bbreg(hw, ROFDM0_TRMUXPAR, MASKDWORD, 0x000800e4);
	rtl_set_bbreg(hw, RFPGA0_XCD_RFINTERFACESW, MASKDWORD, 0x22204000);
	rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER4, 0xf00000, 0x0f);
	if (is2t) {
		rtl_set_bbreg(hw, RFPGA0_XA_LSSIPARAMETER, MASKDWORD,
			      0x00010000);
		rtl_set_bbreg(hw, RFPGA0_XB_LSSIPARAMETER, MASKDWORD,
			      0x00010000);
	}

	/* MAC settings */
	rtl92d_phy_mac_setting_calibration(hw, iqk_mac_reg,
					   rtlphy->iqk_mac_backup);

	/* Page B init */
	rtl_set_bbreg(hw, RCONFIG_ANTA, MASKDWORD, 0x0f600000);
	if (is2t)
		rtl_set_bbreg(hw, RCONFIG_ANTB, MASKDWORD, 0x0f600000);

	/* IQ calibration setting */
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "IQK setting!\n");
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKH3BYTES, 0x808000);
	rtl_set_bbreg(hw, RTX_IQK, MASKDWORD, 0x01007c00);
	rtl_set_bbreg(hw, RRX_IQK, MASKDWORD, 0x01004800);

	for (i = 0; i < retrycount; i++) {
		patha_ok = _rtl92du_phy_patha_iqk(hw, is2t);
		if (patha_ok == 0x03) {
			RTPRINT(rtlpriv, FINIT, INIT_IQK,
				"Path A IQK Success!!\n");
			result[t][0] = rtl_get_bbreg(hw, RTX_POWER_BEFORE_IQK_A,
						     MASK_IQK_RESULT);
			result[t][1] = rtl_get_bbreg(hw, RTX_POWER_AFTER_IQK_A,
						     MASK_IQK_RESULT);
			result[t][2] = rtl_get_bbreg(hw, RRX_POWER_BEFORE_IQK_A_2,
						     MASK_IQK_RESULT);
			result[t][3] = rtl_get_bbreg(hw, RRX_POWER_AFTER_IQK_A_2,
						     MASK_IQK_RESULT);
			break;
		} else if (i == (retrycount - 1) && patha_ok == 0x01) {
			/* Tx IQK OK */
			RTPRINT(rtlpriv, FINIT, INIT_IQK,
				"Path A IQK Only  Tx Success!!\n");

			result[t][0] = rtl_get_bbreg(hw, RTX_POWER_BEFORE_IQK_A,
						     MASK_IQK_RESULT);
			result[t][1] = rtl_get_bbreg(hw, RTX_POWER_AFTER_IQK_A,
						     MASK_IQK_RESULT);
		}
	}
	if (patha_ok == 0x00)
		RTPRINT(rtlpriv, FINIT, INIT_IQK, "Path A IQK failed!!\n");

	if (is2t) {
		_rtl92du_phy_patha_standby(hw);
		/* Turn Path B ADDA on */
		rtl92d_phy_path_adda_on(hw, adda_reg, false, is2t);

		for (i = 0; i < retrycount; i++) {
			pathb_ok = _rtl92du_phy_pathb_iqk(hw);
			if (pathb_ok == 0x03) {
				RTPRINT(rtlpriv, FINIT, INIT_IQK,
					"Path B IQK Success!!\n");
				result[t][4] = rtl_get_bbreg(hw, RTX_POWER_BEFORE_IQK_B,
							     MASK_IQK_RESULT);
				result[t][5] = rtl_get_bbreg(hw, RTX_POWER_AFTER_IQK_B,
							     MASK_IQK_RESULT);
				result[t][6] = rtl_get_bbreg(hw, RRX_POWER_BEFORE_IQK_B_2,
							     MASK_IQK_RESULT);
				result[t][7] = rtl_get_bbreg(hw, RRX_POWER_AFTER_IQK_B_2,
							     MASK_IQK_RESULT);
				break;
			} else if (i == (retrycount - 1) && pathb_ok == 0x01) {
				/* Tx IQK OK */
				RTPRINT(rtlpriv, FINIT, INIT_IQK,
					"Path B Only Tx IQK Success!!\n");
				result[t][4] = rtl_get_bbreg(hw, RTX_POWER_BEFORE_IQK_B,
							     MASK_IQK_RESULT);
				result[t][5] = rtl_get_bbreg(hw, RTX_POWER_AFTER_IQK_B,
							     MASK_IQK_RESULT);
			}
		}
		if (pathb_ok == 0x00)
			RTPRINT(rtlpriv, FINIT, INIT_IQK,
				"Path B IQK failed!!\n");
	}

	/* Back to BB mode, load original value */
	RTPRINT(rtlpriv, FINIT, INIT_IQK,
		"IQK:Back to BB mode, load original value!\n");

	rtl_set_bbreg(hw, RFPGA0_IQK, MASKH3BYTES, 0x000000);

	if (t != 0) {
		/* Switch back BB to SI mode after finish IQ Calibration. */
		if (!rtlphy->rfpi_enable)
			_rtl92du_phy_pimode_switch(hw, false);

		/* Reload ADDA power saving parameters */
		_rtl92du_phy_reload_adda_registers(hw, adda_reg,
						   rtlphy->adda_backup,
						   IQK_ADDA_REG_NUM);

		/* Reload MAC parameters */
		_rtl92du_phy_reload_mac_registers(hw, iqk_mac_reg,
						  rtlphy->iqk_mac_backup);

		if (is2t)
			_rtl92du_phy_reload_adda_registers(hw, iqk_bb_reg,
							   rtlphy->iqk_bb_backup,
							   IQK_BB_REG_NUM);
		else
			_rtl92du_phy_reload_adda_registers(hw, iqk_bb_reg,
							   rtlphy->iqk_bb_backup,
							   IQK_BB_REG_NUM - 1);

		/* load 0xe30 IQC default value */
		rtl_set_bbreg(hw, RTX_IQK_TONE_A, MASKDWORD, 0x01008c00);
		rtl_set_bbreg(hw, RRX_IQK_TONE_A, MASKDWORD, 0x01008c00);
	}
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "<==\n");
}

static void _rtl92du_phy_iq_calibrate_5g_normal(struct ieee80211_hw *hw,
						long result[][8], u8 t)
{
	static const u32 adda_reg[IQK_ADDA_REG_NUM] = {
		RFPGA0_XCD_SWITCHCONTROL, RBLUE_TOOTH, RRX_WAIT_CCA,
		RTX_CCK_RFON, RTX_CCK_BBON, RTX_OFDM_RFON, RTX_OFDM_BBON,
		RTX_TO_RX, RTX_TO_TX, RRX_CCK, RRX_OFDM, RRX_WAIT_RIFS,
		RRX_TO_RX, RSTANDBY, RSLEEP, RPMPD_ANAEN
	};
	static const u32 iqk_mac_reg[IQK_MAC_REG_NUM] = {
		REG_TXPAUSE, REG_BCN_CTRL, REG_BCN_CTRL_1, REG_GPIO_MUXCFG
	};
	static const u32 iqk_bb_reg[IQK_BB_REG_NUM] = {
		RFPGA0_XAB_RFINTERFACESW, RFPGA0_XA_RFINTERFACEOE,
		RFPGA0_XB_RFINTERFACEOE, ROFDM0_TRMUXPAR,
		RFPGA0_XCD_RFINTERFACESW, ROFDM0_TRXPATHENABLE,
		RFPGA0_RFMOD, RFPGA0_ANALOGPARAMETER4,
		ROFDM0_XAAGCCORE1, ROFDM0_XBAGCCORE1
	};
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_hal *rtlhal = &rtlpriv->rtlhal;
	bool is2t = IS_92D_SINGLEPHY(rtlhal->version);
	u8 patha_ok, pathb_ok;
	bool rf_path_div;
	u32 bbvalue;

	/* Note: IQ calibration must be performed after loading
	 * PHY_REG.txt , and radio_a, radio_b.txt
	 */

	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "IQK for 5G NORMAL:Start!!!\n");

	mdelay(IQK_DELAY_TIME * 20);

	if (t == 0) {
		bbvalue = rtl_get_bbreg(hw, RFPGA0_RFMOD, MASKDWORD);
		RTPRINT(rtlpriv, FINIT, INIT_IQK,  "==>0x%08x\n", bbvalue);
		RTPRINT(rtlpriv, FINIT, INIT_IQK, "IQ Calibration for %s\n",
			is2t ? "2T2R" : "1T1R");

		/* Save ADDA parameters, turn Path A ADDA on */
		rtl92d_phy_save_adda_registers(hw, adda_reg,
					       rtlphy->adda_backup,
					       IQK_ADDA_REG_NUM);
		rtl92d_phy_save_mac_registers(hw, iqk_mac_reg,
					      rtlphy->iqk_mac_backup);
		if (is2t)
			rtl92d_phy_save_adda_registers(hw, iqk_bb_reg,
						       rtlphy->iqk_bb_backup,
						       IQK_BB_REG_NUM);
		else
			rtl92d_phy_save_adda_registers(hw, iqk_bb_reg,
						       rtlphy->iqk_bb_backup,
						       IQK_BB_REG_NUM - 1);
	}

	rf_path_div = rtl_get_bbreg(hw, 0xb30, BIT(27));
	rtl92d_phy_path_adda_on(hw, adda_reg, !rf_path_div, is2t);

	if (t == 0)
		rtlphy->rfpi_enable = rtl_get_bbreg(hw,
						    RFPGA0_XA_HSSIPARAMETER1,
						    BIT(8));

	/*  Switch BB to PI mode to do IQ Calibration. */
	if (!rtlphy->rfpi_enable)
		_rtl92du_phy_pimode_switch(hw, true);

	/* MAC settings */
	rtl92d_phy_mac_setting_calibration(hw, iqk_mac_reg,
					   rtlphy->iqk_mac_backup);

	rtl92du_phy_set_bb_reg_1byte(hw, RFPGA0_RFMOD, BCCKEN, 0x00);
	rtl_set_bbreg(hw, ROFDM0_TRXPATHENABLE, MASKDWORD, 0x03a05600);
	rtl_set_bbreg(hw, ROFDM0_TRMUXPAR, MASKDWORD, 0x000800e4);
	rtl_set_bbreg(hw, RFPGA0_XCD_RFINTERFACESW, MASKDWORD, 0x22208000);
	rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER4, 0xf00000, 0x0f);

	/* Page A AP setting for IQK */
	rtl_set_bbreg(hw, RPDP_ANTA, MASKDWORD, 0);
	rtl_set_bbreg(hw, RCONFIG_ANTA, MASKDWORD, 0x20000000);
	if (is2t) {
		/* Page B AP setting for IQK */
		rtl_set_bbreg(hw, RPDP_ANTB, MASKDWORD, 0);
		rtl_set_bbreg(hw, RCONFIG_ANTB, MASKDWORD, 0x20000000);
	}

	/* IQ calibration setting  */
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "IQK setting!\n");
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKH3BYTES, 0x808000);
	rtl_set_bbreg(hw, RTX_IQK, MASKDWORD, 0x10007c00);
	rtl_set_bbreg(hw, RRX_IQK, MASKDWORD, 0x01004800);

	patha_ok = _rtl92du_phy_patha_iqk_5g_normal(hw, is2t);
	if (patha_ok == 0x03) {
		RTPRINT(rtlpriv, FINIT, INIT_IQK,  "Path A IQK Success!!\n");
		result[t][0] = rtl_get_bbreg(hw, RTX_POWER_BEFORE_IQK_A,
					     MASK_IQK_RESULT);
		result[t][1] = rtl_get_bbreg(hw, RTX_POWER_AFTER_IQK_A,
					     MASK_IQK_RESULT);
		result[t][2] = rtl_get_bbreg(hw, RRX_POWER_BEFORE_IQK_A_2,
					     MASK_IQK_RESULT);
		result[t][3] = rtl_get_bbreg(hw, RRX_POWER_AFTER_IQK_A_2,
					     MASK_IQK_RESULT);
	} else if (patha_ok == 0x01) {	/* Tx IQK OK */
		RTPRINT(rtlpriv, FINIT, INIT_IQK,
			"Path A IQK Only  Tx Success!!\n");

		result[t][0] = rtl_get_bbreg(hw, RTX_POWER_BEFORE_IQK_A,
					     MASK_IQK_RESULT);
		result[t][1] = rtl_get_bbreg(hw, RTX_POWER_AFTER_IQK_A,
					     MASK_IQK_RESULT);
	} else {
		rtl_set_bbreg(hw, RFPGA0_IQK, MASKH3BYTES, 0x000000);
		RTPRINT(rtlpriv, FINIT, INIT_IQK, "0xe70 = %#x\n",
			rtl_get_bbreg(hw, RRX_WAIT_CCA, MASKDWORD));
		RTPRINT(rtlpriv, FINIT, INIT_IQK, "RF path A 0x0 = %#x\n",
			rtl_get_rfreg(hw, RF90_PATH_A, RF_AC, RFREG_OFFSET_MASK));
		rtl_set_bbreg(hw, RFPGA0_IQK, MASKH3BYTES, 0x808000);
		RTPRINT(rtlpriv, FINIT, INIT_IQK,  "Path A IQK Fail!!\n");
	}

	if (is2t) {
		/* _rtl92d_phy_patha_standby(hw); */
		/* Turn Path B ADDA on  */
		rtl92d_phy_path_adda_on(hw, adda_reg, false, is2t);

		pathb_ok = _rtl92du_phy_pathb_iqk_5g_normal(hw);
		if (pathb_ok == 0x03) {
			RTPRINT(rtlpriv, FINIT, INIT_IQK,
				"Path B IQK Success!!\n");
			result[t][4] = rtl_get_bbreg(hw, RTX_POWER_BEFORE_IQK_B,
						     MASK_IQK_RESULT);
			result[t][5] = rtl_get_bbreg(hw, RTX_POWER_AFTER_IQK_B,
						     MASK_IQK_RESULT);
			result[t][6] = rtl_get_bbreg(hw, RRX_POWER_BEFORE_IQK_B_2,
						     MASK_IQK_RESULT);
			result[t][7] = rtl_get_bbreg(hw, RRX_POWER_AFTER_IQK_B_2,
						     MASK_IQK_RESULT);
		} else if (pathb_ok == 0x01) { /* Tx IQK OK */
			RTPRINT(rtlpriv, FINIT, INIT_IQK,
				"Path B Only Tx IQK Success!!\n");
			result[t][4] = rtl_get_bbreg(hw, RTX_POWER_BEFORE_IQK_B,
						     MASK_IQK_RESULT);
			result[t][5] = rtl_get_bbreg(hw, RTX_POWER_AFTER_IQK_B,
						     MASK_IQK_RESULT);
		} else {
			RTPRINT(rtlpriv, FINIT, INIT_IQK,
				"Path B IQK failed!!\n");
		}
	}

	/* Back to BB mode, load original value */
	RTPRINT(rtlpriv, FINIT, INIT_IQK,
		"IQK:Back to BB mode, load original value!\n");
	rtl_set_bbreg(hw, RFPGA0_IQK, MASKH3BYTES, 0);

	if (is2t)
		_rtl92du_phy_reload_adda_registers(hw, iqk_bb_reg,
						   rtlphy->iqk_bb_backup,
						   IQK_BB_REG_NUM);
	else
		_rtl92du_phy_reload_adda_registers(hw, iqk_bb_reg,
						   rtlphy->iqk_bb_backup,
						   IQK_BB_REG_NUM - 1);

	/* path A IQ path to DP block */
	rtl_set_bbreg(hw, RPDP_ANTA, MASKDWORD, 0x010170b8);
	if (is2t) /* path B IQ path to DP block */
		rtl_set_bbreg(hw, RPDP_ANTB, MASKDWORD, 0x010170b8);

	/* Reload MAC parameters */
	_rtl92du_phy_reload_mac_registers(hw, iqk_mac_reg,
					  rtlphy->iqk_mac_backup);

	/* Switch back BB to SI mode after finish IQ Calibration. */
	if (!rtlphy->rfpi_enable)
		_rtl92du_phy_pimode_switch(hw, false);

	/* Reload ADDA power saving parameters */
	_rtl92du_phy_reload_adda_registers(hw, adda_reg,
					   rtlphy->adda_backup,
					   IQK_ADDA_REG_NUM);

	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "<==\n");
}

static bool _rtl92du_phy_simularity_compare(struct ieee80211_hw *hw,
					    long result[][8], u8 c1, u8 c2)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = &rtlpriv->rtlhal;
	u32 i, j, diff, sim_bitmap, bound, u4temp = 0;
	u8 final_candidate[2] = {0xFF, 0xFF};	/* for path A and path B */
	bool is2t = IS_92D_SINGLEPHY(rtlhal->version);
	bool bresult = true;

	if (is2t)
		bound = 8;
	else
		bound = 4;

	sim_bitmap = 0;

	for (i = 0; i < bound; i++) {
		diff = abs_diff(result[c1][i], result[c2][i]);

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

		for (i = 0; i < bound; i++)
			u4temp += result[c1][i] + result[c2][i];

		if (u4temp == 0) /* IQK fail for c1 & c2 */
			bresult = false;

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

static void _rtl92du_phy_patha_fill_iqk_matrix_5g_normal(struct ieee80211_hw *hw,
							 bool iqk_ok,
							 long result[][8],
							 u8 final_candidate,
							 bool txonly)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = &rtlpriv->rtlhal;
	u32 val_x, reg;
	int val_y;

	RTPRINT(rtlpriv, FINIT, INIT_IQK,
		"Path A IQ Calibration %s !\n", iqk_ok ? "Success" : "Failed");
	if (iqk_ok && final_candidate != 0xFF) {
		val_x = result[final_candidate][0];
		if ((val_x & 0x00000200) != 0)
			val_x = val_x | 0xFFFFFC00;

		RTPRINT(rtlpriv, FINIT, INIT_IQK, "X = 0x%x\n", val_x);
		rtl_set_bbreg(hw, RTX_IQK_TONE_A, 0x3FF0000, val_x);
		rtl_set_bbreg(hw, ROFDM0_ECCATHRESHOLD, BIT(24), 0);

		val_y = result[final_candidate][1];
		if ((val_y & 0x00000200) != 0)
			val_y = val_y | 0xFFFFFC00;

		/* path B IQK result + 3 */
		if (rtlhal->current_bandtype == BAND_ON_5G)
			val_y += 3;

		RTPRINT(rtlpriv, FINIT, INIT_IQK, "Y = 0x%x\n", val_y);

		rtl_set_bbreg(hw, RTX_IQK_TONE_A, 0x3FF, val_y);
		rtl_set_bbreg(hw, ROFDM0_ECCATHRESHOLD, BIT(26), 0);

		RTPRINT(rtlpriv, FINIT, INIT_IQK, "0xe30 = 0x%x\n",
			rtl_get_bbreg(hw, RTX_IQK_TONE_A, MASKDWORD));

		if (txonly) {
			RTPRINT(rtlpriv, FINIT, INIT_IQK, "only Tx OK\n");
			return;
		}

		reg = result[final_candidate][2];
		rtl_set_bbreg(hw, ROFDM0_XARXIQIMBALANCE, 0x3FF, reg);
		reg = result[final_candidate][3] & 0x3F;
		rtl_set_bbreg(hw, ROFDM0_XARXIQIMBALANCE, 0xFC00, reg);
		reg = (result[final_candidate][3] >> 6) & 0xF;
		rtl_set_bbreg(hw, ROFDM0_RXIQEXTANTA, 0xF0000000, reg);
	} else {
		RTPRINT(rtlpriv, FINIT, INIT_IQK,
			"%s: Tx/Rx fail restore default value\n", __func__);

		rtl_set_bbreg(hw, RTX_IQK_TONE_A, MASKDWORD, 0x19008c00);
		rtl_set_bbreg(hw, ROFDM0_XARXIQIMBALANCE, MASKDWORD, 0x40000100);
		rtl_set_bbreg(hw, RRX_IQK_TONE_A, MASKDWORD, 0x19008c00);
	}
}

static void _rtl92du_phy_patha_fill_iqk_matrix(struct ieee80211_hw *hw,
					       bool iqk_ok, long result[][8],
					       u8 final_candidate, bool txonly)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = &rtlpriv->rtlhal;
	u32 oldval_0, val_x, tx0_a, reg;
	long val_y, tx0_c;
	bool is2t = IS_92D_SINGLEPHY(rtlhal->version) ||
		    rtlhal->macphymode == DUALMAC_DUALPHY;

	if (rtlhal->current_bandtype == BAND_ON_5G) {
		_rtl92du_phy_patha_fill_iqk_matrix_5g_normal(hw, iqk_ok, result,
							     final_candidate,
							     txonly);
		return;
	}

	RTPRINT(rtlpriv, FINIT, INIT_IQK,
		"Path A IQ Calibration %s !\n", iqk_ok ? "Success" : "Failed");
	if (final_candidate == 0xFF || !iqk_ok)
		return;

	/* OFDM0_D */
	oldval_0 = rtl_get_bbreg(hw, ROFDM0_XATXIQIMBALANCE, 0xffc00000);

	val_x = result[final_candidate][0];
	if ((val_x & 0x00000200) != 0)
		val_x = val_x | 0xFFFFFC00;

	tx0_a = (val_x * oldval_0) >> 8;
	RTPRINT(rtlpriv, FINIT, INIT_IQK,
		"X = 0x%x, tx0_a = 0x%x, oldval_0 0x%x\n",
		val_x, tx0_a, oldval_0);
	rtl_set_bbreg(hw, ROFDM0_XATXIQIMBALANCE, 0x3FF, tx0_a);
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

	rtl_set_bbreg(hw, ROFDM0_XCTXAFE, 0xF0000000, (tx0_c & 0x3C0) >> 6);
	rtl_set_bbreg(hw, ROFDM0_XATXIQIMBALANCE, 0x003F0000, tx0_c & 0x3F);
	if (is2t)
		rtl_set_bbreg(hw, ROFDM0_ECCATHRESHOLD, BIT(26),
			      (val_y * oldval_0 >> 7) & 0x1);

	RTPRINT(rtlpriv, FINIT, INIT_IQK, "0xC80 = 0x%x\n",
		rtl_get_bbreg(hw, ROFDM0_XATXIQIMBALANCE,
			      MASKDWORD));

	if (txonly) {
		RTPRINT(rtlpriv, FINIT, INIT_IQK, "only Tx OK\n");
		return;
	}

	reg = result[final_candidate][2];
	rtl_set_bbreg(hw, ROFDM0_XARXIQIMBALANCE, 0x3FF, reg);
	reg = result[final_candidate][3] & 0x3F;
	rtl_set_bbreg(hw, ROFDM0_XARXIQIMBALANCE, 0xFC00, reg);
	reg = (result[final_candidate][3] >> 6) & 0xF;
	rtl_set_bbreg(hw, ROFDM0_RXIQEXTANTA, 0xF0000000, reg);
}

static void _rtl92du_phy_pathb_fill_iqk_matrix_5g_normal(struct ieee80211_hw *hw,
							 bool iqk_ok,
							 long result[][8],
							 u8 final_candidate,
							 bool txonly)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = &rtlpriv->rtlhal;
	u32 val_x, reg;
	int val_y;

	RTPRINT(rtlpriv, FINIT, INIT_IQK,
		"Path B IQ Calibration %s !\n", iqk_ok ? "Success" : "Failed");
	if (iqk_ok && final_candidate != 0xFF) {
		val_x = result[final_candidate][4];
		if ((val_x & 0x00000200) != 0)
			val_x = val_x | 0xFFFFFC00;

		RTPRINT(rtlpriv, FINIT, INIT_IQK, "X = 0x%x\n", val_x);
		rtl_set_bbreg(hw, RTX_IQK_TONE_B, 0x3FF0000, val_x);
		rtl_set_bbreg(hw, ROFDM0_ECCATHRESHOLD, BIT(28), 0);

		val_y = result[final_candidate][5];
		if ((val_y & 0x00000200) != 0)
			val_y = val_y | 0xFFFFFC00;

		/* path B IQK result + 3 */
		if (rtlhal->current_bandtype == BAND_ON_5G)
			val_y += 3;

		RTPRINT(rtlpriv, FINIT, INIT_IQK, "Y = 0x%x\n", val_y);

		rtl_set_bbreg(hw, RTX_IQK_TONE_B, 0x3FF, val_y);
		rtl_set_bbreg(hw, ROFDM0_ECCATHRESHOLD, BIT(30), 0);

		RTPRINT(rtlpriv, FINIT, INIT_IQK, "0xe50 = 0x%x\n",
			rtl_get_bbreg(hw, RTX_IQK_TONE_B, MASKDWORD));

		if (txonly) {
			RTPRINT(rtlpriv, FINIT, INIT_IQK, "only Tx OK\n");
			return;
		}

		reg = result[final_candidate][6];
		rtl_set_bbreg(hw, ROFDM0_XBRXIQIMBALANCE, 0x3FF, reg);
		reg = result[final_candidate][7] & 0x3F;
		rtl_set_bbreg(hw, ROFDM0_XBRXIQIMBALANCE, 0xFC00, reg);
		reg = (result[final_candidate][7] >> 6) & 0xF;
		rtl_set_bbreg(hw, ROFDM0_AGCRSSITABLE, 0x0000F000, reg);
	} else {
		RTPRINT(rtlpriv, FINIT, INIT_IQK,
			"%s: Tx/Rx fail restore default value\n", __func__);

		rtl_set_bbreg(hw, RTX_IQK_TONE_B, MASKDWORD, 0x19008c00);
		rtl_set_bbreg(hw, ROFDM0_XBRXIQIMBALANCE, MASKDWORD, 0x40000100);
		rtl_set_bbreg(hw, RRX_IQK_TONE_B, MASKDWORD, 0x19008c00);
	}
}

static void _rtl92du_phy_pathb_fill_iqk_matrix(struct ieee80211_hw *hw,
					       bool iqk_ok, long result[][8],
					       u8 final_candidate, bool txonly)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = &rtlpriv->rtlhal;
	u32 oldval_1, val_x, tx1_a, reg;
	long val_y, tx1_c;

	if (rtlhal->current_bandtype == BAND_ON_5G) {
		_rtl92du_phy_pathb_fill_iqk_matrix_5g_normal(hw, iqk_ok, result,
							     final_candidate,
							     txonly);
		return;
	}

	RTPRINT(rtlpriv, FINIT, INIT_IQK, "Path B IQ Calibration %s !\n",
		iqk_ok ? "Success" : "Failed");

	if (final_candidate == 0xFF || !iqk_ok)
		return;

	oldval_1 = rtl_get_bbreg(hw, ROFDM0_XBTXIQIMBALANCE, 0xffc00000);

	val_x = result[final_candidate][4];
	if ((val_x & 0x00000200) != 0)
		val_x = val_x | 0xFFFFFC00;

	tx1_a = (val_x * oldval_1) >> 8;
	RTPRINT(rtlpriv, FINIT, INIT_IQK, "X = 0x%x, tx1_a = 0x%x\n",
		val_x, tx1_a);
	rtl_set_bbreg(hw, ROFDM0_XBTXIQIMBALANCE, 0x3FF, tx1_a);
	rtl_set_bbreg(hw, ROFDM0_ECCATHRESHOLD, BIT(28),
		      (val_x * oldval_1 >> 7) & 0x1);

	val_y = result[final_candidate][5];
	if ((val_y & 0x00000200) != 0)
		val_y = val_y | 0xFFFFFC00;

	if (rtlhal->current_bandtype == BAND_ON_5G)
		val_y += 3;

	tx1_c = (val_y * oldval_1) >> 8;
	RTPRINT(rtlpriv, FINIT, INIT_IQK, "Y = 0x%lx, tx1_c = 0x%lx\n",
		val_y, tx1_c);

	rtl_set_bbreg(hw, ROFDM0_XDTXAFE, 0xF0000000, (tx1_c & 0x3C0) >> 6);
	rtl_set_bbreg(hw, ROFDM0_XBTXIQIMBALANCE, 0x003F0000, tx1_c & 0x3F);
	rtl_set_bbreg(hw, ROFDM0_ECCATHRESHOLD, BIT(30),
		      (val_y * oldval_1 >> 7) & 0x1);

	if (txonly)
		return;

	reg = result[final_candidate][6];
	rtl_set_bbreg(hw, ROFDM0_XBRXIQIMBALANCE, 0x3FF, reg);
	reg = result[final_candidate][7] & 0x3F;
	rtl_set_bbreg(hw, ROFDM0_XBRXIQIMBALANCE, 0xFC00, reg);
	reg = (result[final_candidate][7] >> 6) & 0xF;
	rtl_set_bbreg(hw, ROFDM0_AGCRSSITABLE, 0x0000F000, reg);
}

void rtl92du_phy_iq_calibrate(struct ieee80211_hw *hw)
{
	long rege94, rege9c, regea4, regeac, regeb4;
	bool is12simular, is13simular, is23simular;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = &rtlpriv->rtlhal;
	long regebc, regec4, regecc, regtmp = 0;
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 i, final_candidate, indexforchannel;
	bool patha_ok, pathb_ok;
	long result[4][8] = {};

	RTPRINT(rtlpriv, FINIT, INIT_IQK,
		"IQK:Start!!!channel %d\n", rtlphy->current_channel);

	final_candidate = 0xff;
	patha_ok = false;
	pathb_ok = false;
	is12simular = false;
	is23simular = false;
	is13simular = false;
	RTPRINT(rtlpriv, FINIT, INIT_IQK,
		"IQK !!!currentband %d\n", rtlhal->current_bandtype);

	for (i = 0; i < 3; i++) {
		if (rtlhal->current_bandtype == BAND_ON_5G) {
			_rtl92du_phy_iq_calibrate_5g_normal(hw, result, i);
		} else if (rtlhal->current_bandtype == BAND_ON_2_4G) {
			if (IS_92D_SINGLEPHY(rtlhal->version))
				_rtl92du_phy_iq_calibrate(hw, result, i, true);
			else
				_rtl92du_phy_iq_calibrate(hw, result, i, false);
		}

		if (i == 1) {
			is12simular = _rtl92du_phy_simularity_compare(hw, result,
								      0, 1);
			if (is12simular) {
				final_candidate = 0;
				break;
			}
		}

		if (i == 2) {
			is13simular = _rtl92du_phy_simularity_compare(hw, result,
								      0, 2);
			if (is13simular) {
				final_candidate = 0;
				break;
			}

			is23simular = _rtl92du_phy_simularity_compare(hw, result,
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
		rege94 = result[final_candidate][0];
		rtlphy->reg_e94 = rege94;
		rege9c = result[final_candidate][1];
		rtlphy->reg_e9c = rege9c;
		regea4 = result[final_candidate][2];
		regeac = result[final_candidate][3];
		regeb4 = result[final_candidate][4];
		rtlphy->reg_eb4 = regeb4;
		regebc = result[final_candidate][5];
		rtlphy->reg_ebc = regebc;
		regec4 = result[final_candidate][6];
		regecc = result[final_candidate][7];

		RTPRINT(rtlpriv, FINIT, INIT_IQK,
			"IQK: final_candidate is %x\n", final_candidate);
		RTPRINT(rtlpriv, FINIT, INIT_IQK,
			"IQK: rege94=%lx rege9c=%lx regea4=%lx regeac=%lx regeb4=%lx regebc=%lx regec4=%lx regecc=%lx\n",
			rege94, rege9c, regea4, regeac, regeb4, regebc, regec4,
			regecc);

		patha_ok = true;
		pathb_ok = true;
	} else {
		rtlphy->reg_e94 = 0x100;
		rtlphy->reg_eb4 = 0x100; /* X default value */
		rtlphy->reg_e9c = 0x0;
		rtlphy->reg_ebc = 0x0;   /* Y default value */
	}
	if (rege94 != 0 /*&& regea4 != 0*/)
		_rtl92du_phy_patha_fill_iqk_matrix(hw, patha_ok, result,
						   final_candidate,
						   regea4 == 0);
	if (IS_92D_SINGLEPHY(rtlhal->version) &&
	    regeb4 != 0 /*&& regec4 != 0*/)
		_rtl92du_phy_pathb_fill_iqk_matrix(hw, pathb_ok, result,
						   final_candidate,
						   regec4 == 0);

	if (final_candidate != 0xFF) {
		indexforchannel =
			rtl92d_get_rightchnlplace_for_iqk(rtlphy->current_channel);

		for (i = 0; i < IQK_MATRIX_REG_NUM; i++)
			rtlphy->iqk_matrix[indexforchannel].value[0][i] =
				result[final_candidate][i];

		rtlphy->iqk_matrix[indexforchannel].iqk_done = true;

		rtl_dbg(rtlpriv, COMP_SCAN | COMP_MLME, DBG_LOUD,
			"IQK OK indexforchannel %d\n", indexforchannel);
	}
}

void rtl92du_phy_reload_iqk_setting(struct ieee80211_hw *hw, u8 channel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = &rtlpriv->rtlhal;
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_mac *mac = rtl_mac(rtlpriv);
	u8 indexforchannel;
	bool need_iqk;

	rtl_dbg(rtlpriv, COMP_CMD, DBG_LOUD, "channel %d\n", channel);
	/*------Do IQK for normal chip and test chip 5G band------- */

	indexforchannel = rtl92d_get_rightchnlplace_for_iqk(channel);
	rtl_dbg(rtlpriv, COMP_CMD, DBG_LOUD, "indexforchannel %d done %d\n",
		indexforchannel,
		rtlphy->iqk_matrix[indexforchannel].iqk_done);

	/* We need to do IQK if we're about to connect to a network on 5 GHz.
	 * On 5 GHz a channel switch outside of scanning happens only before
	 * connecting.
	 */
	need_iqk = !mac->act_scanning;

	if (!rtlphy->iqk_matrix[indexforchannel].iqk_done && need_iqk) {
		rtl_dbg(rtlpriv, COMP_SCAN | COMP_INIT, DBG_LOUD,
			"Do IQK Matrix reg for channel:%d....\n", channel);
		rtl92du_phy_iq_calibrate(hw);
		return;
	}

	/* Just load the value. */
	/* 2G band just load once. */
	if ((!rtlhal->load_imrandiqk_setting_for2g && indexforchannel == 0) ||
	    indexforchannel > 0) {
		rtl_dbg(rtlpriv, COMP_SCAN, DBG_LOUD,
			"Just Read IQK Matrix reg for channel:%d....\n",
			channel);

		if (rtlphy->iqk_matrix[indexforchannel].value[0][0] != 0)
			_rtl92du_phy_patha_fill_iqk_matrix(hw, true,
				rtlphy->iqk_matrix[indexforchannel].value, 0,
				rtlphy->iqk_matrix[indexforchannel].value[0][2] == 0);

		if (IS_92D_SINGLEPHY(rtlhal->version) &&
		    rtlphy->iqk_matrix[indexforchannel].value[0][4] != 0)
			_rtl92du_phy_pathb_fill_iqk_matrix(hw, true,
				rtlphy->iqk_matrix[indexforchannel].value, 0,
				rtlphy->iqk_matrix[indexforchannel].value[0][6] == 0);
	}
	rtl_dbg(rtlpriv, COMP_CMD, DBG_LOUD, "<====\n");
}

static void _rtl92du_phy_reload_lck_setting(struct ieee80211_hw *hw, u8 channel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = &rtlpriv->rtlhal;
	u8 erfpath = rtlhal->current_bandtype == BAND_ON_5G ? RF90_PATH_A :
		IS_92D_SINGLEPHY(rtlhal->version) ? RF90_PATH_B : RF90_PATH_A;
	bool bneed_powerdown_radio = false;
	u32 u4tmp, u4regvalue;

	rtl_dbg(rtlpriv, COMP_CMD, DBG_LOUD, "path %d\n", erfpath);
	RTPRINT(rtlpriv, FINIT, INIT_IQK, "band type = %d\n",
		rtlpriv->rtlhal.current_bandtype);
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "channel = %d\n", channel);

	if (rtlpriv->rtlhal.current_bandtype == BAND_ON_5G) {/* Path-A for 5G */
		u4tmp = rtlpriv->curveindex_5g[channel - 1];
		RTPRINT(rtlpriv, FINIT, INIT_IQK,
			"ver 1 set RF-A, 5G,	0x28 = 0x%x !!\n", u4tmp);

		if (rtlpriv->rtlhal.macphymode == DUALMAC_DUALPHY &&
		    rtlpriv->rtlhal.interfaceindex == 1) {
			bneed_powerdown_radio =
				rtl92du_phy_enable_anotherphy(hw, false);
			rtlpriv->rtlhal.during_mac1init_radioa = true;
			/* asume no this case */
			if (bneed_powerdown_radio)
				rtl92d_phy_enable_rf_env(hw, erfpath,
							 &u4regvalue);
		}

		rtl_set_rfreg(hw, erfpath, RF_SYN_G4, 0x3f800, u4tmp);

		if (bneed_powerdown_radio) {
			rtl92d_phy_restore_rf_env(hw, erfpath, &u4regvalue);
			rtl92du_phy_powerdown_anotherphy(hw, false);
		}
	} else if (rtlpriv->rtlhal.current_bandtype == BAND_ON_2_4G) {
		u4tmp = rtlpriv->curveindex_2g[channel - 1];
		RTPRINT(rtlpriv, FINIT, INIT_IQK,
			"ver 3 set RF-B, 2G, 0x28 = 0x%x !!\n", u4tmp);

		if (rtlpriv->rtlhal.macphymode == DUALMAC_DUALPHY &&
		    rtlpriv->rtlhal.interfaceindex == 0) {
			bneed_powerdown_radio =
				rtl92du_phy_enable_anotherphy(hw, true);
			rtlpriv->rtlhal.during_mac0init_radiob = true;
			if (bneed_powerdown_radio)
				rtl92d_phy_enable_rf_env(hw, erfpath,
							 &u4regvalue);
		}

		rtl_set_rfreg(hw, erfpath, RF_SYN_G4, 0x3f800, u4tmp);

		RTPRINT(rtlpriv, FINIT, INIT_IQK,
			"ver 3 set RF-B, 2G, 0x28 = 0x%x !!\n",
			rtl_get_rfreg(hw,  erfpath, RF_SYN_G4, 0x3f800));

		if (bneed_powerdown_radio) {
			rtl92d_phy_restore_rf_env(hw, erfpath, &u4regvalue);
			rtl92du_phy_powerdown_anotherphy(hw, true);
		}
	}
	rtl_dbg(rtlpriv, COMP_CMD, DBG_LOUD, "<====\n");
}

static void _rtl92du_phy_lc_calibrate_sw(struct ieee80211_hw *hw, bool is2t)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = &rtlpriv->rtlhal;
	u32 curvecount_val[CV_CURVE_CNT * 2];
	u16 timeout = 800, timecount = 0;
	u32 u4tmp, offset, rf_syn_g4[2];
	u8 tmpreg, index, rf_mode[2];
	u8 path = is2t ? 2 : 1;
	u8 i;

	/* Check continuous TX and Packet TX */
	tmpreg = rtl_read_byte(rtlpriv, 0xd03);
	if ((tmpreg & 0x70) != 0)
		/* if Deal with contisuous TX case, disable all continuous TX */
		rtl_write_byte(rtlpriv, 0xd03, tmpreg & 0x8F);
	else
		/* if Deal with Packet TX case, block all queues */
		rtl_write_byte(rtlpriv, REG_TXPAUSE, 0xFF);

	rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER4, 0xF00000, 0x0F);

	for (index = 0; index < path; index++) {
		/* 1. Read original RF mode */
		offset = index == 0 ? ROFDM0_XAAGCCORE1 : ROFDM0_XBAGCCORE1;
		rf_mode[index] = rtl_read_byte(rtlpriv, offset);

		/* 2. Set RF mode = standby mode */
		rtl_set_rfreg(hw, (enum radio_path)index, RF_AC,
			      RFREG_OFFSET_MASK, 0x010000);

		rf_syn_g4[index] = rtl_get_rfreg(hw, index, RF_SYN_G4,
						 RFREG_OFFSET_MASK);
		rtl_set_rfreg(hw, index, RF_SYN_G4, 0x700, 0x7);

		/* switch CV-curve control by LC-calibration */
		rtl_set_rfreg(hw, (enum radio_path)index, RF_SYN_G7,
			      BIT(17), 0x0);

		/* 4. Set LC calibration begin */
		rtl_set_rfreg(hw, (enum radio_path)index, RF_CHNLBW,
			      0x08000, 0x01);
	}

	for (index = 0; index < path; index++) {
		u4tmp = rtl_get_rfreg(hw, (enum radio_path)index, RF_SYN_G6,
				      RFREG_OFFSET_MASK);

		while ((!(u4tmp & BIT(11))) && timecount <= timeout) {
			mdelay(50);
			timecount += 50;
			u4tmp = rtl_get_rfreg(hw, (enum radio_path)index,
					      RF_SYN_G6, RFREG_OFFSET_MASK);
		}
		RTPRINT(rtlpriv, FINIT, INIT_IQK,
			"PHY_LCK finish delay for %d ms=2\n", timecount);
	}

	if ((tmpreg & 0x70) != 0)
		rtl_write_byte(rtlpriv, 0xd03, tmpreg);
	else /* Deal with Packet TX case */
		rtl_write_byte(rtlpriv, REG_TXPAUSE, 0x00);

	rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER4, 0xF00000, 0x00);

	for (index = 0; index < path; index++) {
		rtl_get_rfreg(hw, index, RF_SYN_G4, RFREG_OFFSET_MASK);

		if (index == 0 && rtlhal->interfaceindex == 0) {
			RTPRINT(rtlpriv, FINIT, INIT_IQK,
				"path-A / 5G LCK\n");
		} else {
			RTPRINT(rtlpriv, FINIT, INIT_IQK,
				"path-B / 2.4G LCK\n");
		}

		memset(curvecount_val, 0, sizeof(curvecount_val));

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
				      RFREG_OFFSET_MASK, 0x0);

			readval = rtl_get_rfreg(hw, (enum radio_path)index,
						0x4F, RFREG_OFFSET_MASK);
			curvecount_val[2 * i + 1] = (readval & 0xfffe0) >> 5;

			/* reg 0x4f [4:0] */
			/* reg 0x50 [19:10] */
			readval2 = rtl_get_rfreg(hw, (enum radio_path)index,
						 0x50, 0xffc00);
			curvecount_val[2 * i] = (((readval & 0x1F) << 10) |
						 readval2);
		}

		if (index == 0 && rtlhal->interfaceindex == 0)
			rtl92d_phy_calc_curvindex(hw, targetchnl_5g,
						  curvecount_val,
						  true, rtlpriv->curveindex_5g);
		else
			rtl92d_phy_calc_curvindex(hw, targetchnl_2g,
						  curvecount_val,
						  false, rtlpriv->curveindex_2g);

		/* switch CV-curve control mode */
		rtl_set_rfreg(hw, (enum radio_path)index, RF_SYN_G7,
			      BIT(17), 0x1);
	}

	/* Restore original situation  */
	for (index = 0; index < path; index++) {
		rtl_set_rfreg(hw, index, RF_SYN_G4, RFREG_OFFSET_MASK,
			      rf_syn_g4[index]);

		offset = index == 0 ? ROFDM0_XAAGCCORE1 : ROFDM0_XBAGCCORE1;
		rtl_write_byte(rtlpriv, offset, 0x50);
		rtl_write_byte(rtlpriv, offset, rf_mode[index]);
	}

	_rtl92du_phy_reload_lck_setting(hw, rtlpriv->phy.current_channel);
}

void rtl92du_phy_lc_calibrate(struct ieee80211_hw *hw, bool is2t)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = &rtlpriv->rtlhal;
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u32 timeout = 2000, timecount = 0;

	while (rtlpriv->mac80211.act_scanning && timecount < timeout) {
		udelay(50);
		timecount += 50;
	}

	rtlphy->lck_inprogress = true;
	RTPRINT(rtlpriv, FINIT, INIT_IQK,
		"LCK:Start!!! currentband %x delay %d ms\n",
		rtlhal->current_bandtype, timecount);

	_rtl92du_phy_lc_calibrate_sw(hw, is2t);

	rtlphy->lck_inprogress = false;
	RTPRINT(rtlpriv, FINIT, INIT_IQK,  "LCK:Finish!!!\n");
}

u8 rtl92du_phy_sw_chnl(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 num_total_rfpath = rtlphy->num_total_rfpath;
	u8 channel = rtlphy->current_channel;
	u32 timeout = 1000, timecount = 0;
	u32 ret_value;
	u8 rfpath;

	if (rtlphy->sw_chnl_inprogress)
		return 0;
	if (rtlphy->set_bwmode_inprogress)
		return 0;

	if ((is_hal_stop(rtlhal)) || (RT_CANNOT_IO(hw))) {
		rtl_dbg(rtlpriv, COMP_CHAN, DBG_LOUD,
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
					  MASKDWORD);
		if (rtlphy->current_channel > 14 && !(ret_value & BIT(0)))
			rtl92du_phy_switch_wirelessband(hw, BAND_ON_5G);
		else if (rtlphy->current_channel <= 14 && (ret_value & BIT(0)))
			rtl92du_phy_switch_wirelessband(hw, BAND_ON_2_4G);
	}

	switch (rtlhal->current_bandtype) {
	case BAND_ON_5G:
		/* Get first channel error when change between
		 * 5G and 2.4G band.
		 */
		if (WARN_ONCE(channel <= 14, "rtl8192du: 5G but channel<=14\n"))
			return 0;
		break;
	case BAND_ON_2_4G:
		/* Get first channel error when change between
		 * 5G and 2.4G band.
		 */
		if (WARN_ONCE(channel > 14, "rtl8192du: 2G but channel>14\n"))
			return 0;
		break;
	default:
		WARN_ONCE(true, "rtl8192du: Invalid WirelessMode(%#x)!!\n",
			  rtlpriv->mac80211.mode);
		break;
	}

	rtlphy->sw_chnl_inprogress = true;

	rtl_dbg(rtlpriv, COMP_SCAN, DBG_TRACE,
		"switch to channel%d\n", rtlphy->current_channel);

	rtl92d_phy_set_txpower_level(hw, channel);

	for (rfpath = 0; rfpath < num_total_rfpath; rfpath++) {
		u32p_replace_bits(&rtlphy->rfreg_chnlval[rfpath],
				  channel, 0xff);

		if (rtlpriv->rtlhal.current_bandtype == BAND_ON_5G) {
			if (channel > 99)
				rtlphy->rfreg_chnlval[rfpath] |= (BIT(18));
			else
				rtlphy->rfreg_chnlval[rfpath] &= ~BIT(18);
			rtlphy->rfreg_chnlval[rfpath] |= (BIT(16) | BIT(8));
		} else {
			rtlphy->rfreg_chnlval[rfpath] &=
				~(BIT(8) | BIT(16) | BIT(18));
		}
		rtl_set_rfreg(hw, rfpath, RF_CHNLBW, RFREG_OFFSET_MASK,
			      rtlphy->rfreg_chnlval[rfpath]);

		_rtl92du_phy_reload_imr_setting(hw, channel, rfpath);
	}

	_rtl92du_phy_switch_rf_setting(hw, channel);

	/* do IQK when all parameters are ready */
	rtl92du_phy_reload_iqk_setting(hw, channel);

	rtl_dbg(rtlpriv, COMP_SCAN, DBG_TRACE, "<==\n");
	rtlphy->sw_chnl_inprogress = false;
	return 1;
}

static void _rtl92du_phy_set_rfon(struct ieee80211_hw *hw)
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

static void _rtl92du_phy_set_rfsleep(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 u4btmp;
	u8 retry = 5;

	/* a.   TXPAUSE 0x522[7:0] = 0xFF  Pause MAC TX queue  */
	rtl_write_byte(rtlpriv, REG_TXPAUSE, 0xFF);

	/* b.   RF path 0 offset 0x00 = 0x00  disable RF  */
	rtl_set_rfreg(hw, RF90_PATH_A, 0x00, RFREG_OFFSET_MASK, 0x00);

	/* c.   APSD_CTRL 0x600[7:0] = 0x40 */
	rtl_write_byte(rtlpriv, REG_APSD_CTRL, 0x40);

	/* d. APSD_CTRL 0x600[7:0] = 0x00
	 * APSD_CTRL 0x600[7:0] = 0x00
	 * RF path 0 offset 0x00 = 0x00
	 * APSD_CTRL 0x600[7:0] = 0x40
	 */
	u4btmp = rtl_get_rfreg(hw, RF90_PATH_A, 0, RFREG_OFFSET_MASK);
	while (u4btmp != 0 && retry > 0) {
		rtl_write_byte(rtlpriv, REG_APSD_CTRL, 0x0);
		rtl_set_rfreg(hw, RF90_PATH_A, 0x00, RFREG_OFFSET_MASK, 0x00);
		rtl_write_byte(rtlpriv, REG_APSD_CTRL, 0x40);
		u4btmp = rtl_get_rfreg(hw, RF90_PATH_A, 0, RFREG_OFFSET_MASK);
		retry--;
	}
	if (retry == 0) {
		/* Jump out the LPS turn off sequence to RF_ON_EXCEP */
		rtl_write_byte(rtlpriv, REG_APSD_CTRL, 0x00);

		rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE2);
		rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE3);
		rtl_write_byte(rtlpriv, REG_TXPAUSE, 0x00);
		rtl_dbg(rtlpriv, COMP_POWER, DBG_LOUD,
			"Fail !!! Switch RF timeout\n");
		return;
	}

	/* e.   For PCIE: SYS_FUNC_EN 0x02[7:0] = 0xE2 reset BB TRX function */
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE2);

	/* f.   SPS_CTRL 0x11[7:0] = 0x22 */
	if (rtlpriv->rtlhal.macphymode == SINGLEMAC_SINGLEPHY)
		rtl_write_byte(rtlpriv, REG_SPS0_CTRL, 0x22);
}

bool rtl92du_phy_set_rf_power_state(struct ieee80211_hw *hw,
				    enum rf_pwrstate rfpwr_state)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtlpriv);
	struct rtl_mac *mac = rtl_mac(rtlpriv);
	bool bresult = true;

	if (rfpwr_state == ppsc->rfpwr_state)
		return false;

	switch (rfpwr_state) {
	case ERFON:
		if (ppsc->rfpwr_state == ERFOFF &&
		    RT_IN_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC)) {
			u32 initializecount = 0;
			bool rtstatus;

			do {
				initializecount++;
				rtl_dbg(rtlpriv, COMP_RF, DBG_DMESG,
					"IPS Set eRf nic enable\n");
				rtstatus = rtl_ps_enable_nic(hw);
			} while (!rtstatus && (initializecount < 10));

			RT_CLEAR_PS_LEVEL(ppsc,
					  RT_RF_OFF_LEVL_HALT_NIC);
		} else {
			rtl_dbg(rtlpriv, COMP_POWER, DBG_DMESG,
				"awake, slept:%d ms state_inap:%x\n",
				jiffies_to_msecs(jiffies -
						 ppsc->last_sleep_jiffies),
				 rtlpriv->psc.state_inap);
			ppsc->last_awake_jiffies = jiffies;
			_rtl92du_phy_set_rfon(hw);
		}

		if (mac->link_state == MAC80211_LINKED)
			rtlpriv->cfg->ops->led_control(hw, LED_CTL_LINK);
		else
			rtlpriv->cfg->ops->led_control(hw, LED_CTL_NO_LINK);
		break;
	case ERFOFF:
		if (ppsc->reg_rfps_level & RT_RF_OFF_LEVL_HALT_NIC) {
			rtl_dbg(rtlpriv, COMP_RF, DBG_DMESG,
				"IPS Set eRf nic disable\n");
			rtl_ps_disable_nic(hw);
			RT_SET_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC);
		} else {
			if (ppsc->rfoff_reason == RF_CHANGE_BY_IPS)
				rtlpriv->cfg->ops->led_control(hw, LED_CTL_NO_LINK);
			else
				rtlpriv->cfg->ops->led_control(hw, LED_CTL_POWER_OFF);
		}
		break;
	case ERFSLEEP:
		if (ppsc->rfpwr_state == ERFOFF)
			return false;

		rtl_dbg(rtlpriv, COMP_POWER, DBG_DMESG,
			"sleep awakened:%d ms state_inap:%x\n",
			jiffies_to_msecs(jiffies -
					 ppsc->last_awake_jiffies),
			rtlpriv->psc.state_inap);
		ppsc->last_sleep_jiffies = jiffies;
		_rtl92du_phy_set_rfsleep(hw);
		break;
	default:
		pr_err("switch case %#x not processed\n",
		       rfpwr_state);
		return false;
	}

	if (bresult)
		ppsc->rfpwr_state = rfpwr_state;

	return bresult;
}

void rtl92du_phy_set_poweron(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u32 mac_reg = (rtlhal->interfaceindex == 0 ? REG_MAC0 : REG_MAC1);
	u8 value8;
	u16 i;

	/* notice fw know band status  0x81[1]/0x53[1] = 0: 5G, 1: 2G */
	if (rtlhal->current_bandtype == BAND_ON_2_4G) {
		value8 = rtl_read_byte(rtlpriv, mac_reg);
		value8 |= BIT(1);
		rtl_write_byte(rtlpriv, mac_reg, value8);
	} else {
		value8 = rtl_read_byte(rtlpriv, mac_reg);
		value8 &= ~BIT(1);
		rtl_write_byte(rtlpriv, mac_reg, value8);
	}

	if (rtlhal->macphymode == SINGLEMAC_SINGLEPHY) {
		value8 = rtl_read_byte(rtlpriv, REG_MAC0);
		rtl_write_byte(rtlpriv, REG_MAC0, value8 | MAC0_ON);
	} else {
		mutex_lock(rtlpriv->mutex_for_power_on_off);
		if (rtlhal->interfaceindex == 0) {
			value8 = rtl_read_byte(rtlpriv, REG_MAC0);
			rtl_write_byte(rtlpriv, REG_MAC0, value8 | MAC0_ON);
		} else {
			value8 = rtl_read_byte(rtlpriv, REG_MAC1);
			rtl_write_byte(rtlpriv, REG_MAC1, value8 | MAC1_ON);
		}
		value8 = rtl_read_byte(rtlpriv, REG_POWER_OFF_IN_PROCESS);
		mutex_unlock(rtlpriv->mutex_for_power_on_off);

		for (i = 0; i < 200; i++) {
			if ((value8 & BIT(7)) == 0)
				break;

			udelay(500);
			mutex_lock(rtlpriv->mutex_for_power_on_off);
			value8 = rtl_read_byte(rtlpriv,
					       REG_POWER_OFF_IN_PROCESS);
			mutex_unlock(rtlpriv->mutex_for_power_on_off);
		}
		if (i == 200)
			WARN_ONCE(true, "rtl8192du: Another mac power off over time\n");
	}
}

void rtl92du_update_bbrf_configuration(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtlpriv);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 rfpath, i;

	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD, "==>\n");
	/* r_select_5G for path_A/B 0 for 2.4G, 1 for 5G */
	if (rtlhal->current_bandtype == BAND_ON_2_4G) {
		/* r_select_5G for path_A/B, 0x878 */
		rtl_set_bbreg(hw, RFPGA0_XAB_RFPARAMETER, BIT(0), 0x0);
		rtl_set_bbreg(hw, RFPGA0_XAB_RFPARAMETER, BIT(15), 0x0);
		if (rtlhal->macphymode != DUALMAC_DUALPHY) {
			rtl_set_bbreg(hw, RFPGA0_XAB_RFPARAMETER, BIT(16), 0x0);
			rtl_set_bbreg(hw, RFPGA0_XAB_RFPARAMETER, BIT(31), 0x0);
		}

		/* rssi_table_select: index 0 for 2.4G. 1~3 for 5G, 0xc78 */
		rtl_set_bbreg(hw, ROFDM0_AGCRSSITABLE, BIT(6) | BIT(7), 0x0);

		/* fc_area  0xd2c */
		rtl_set_bbreg(hw, ROFDM1_CFOTRACKING, BIT(14) | BIT(13), 0x0);

		/* 5G LAN ON */
		rtl_set_bbreg(hw, 0xB30, 0x00F00000, 0xa);

		/* TX BB gain shift*1, Just for testchip, 0xc80, 0xc88 */
		rtl_set_bbreg(hw, ROFDM0_XATXIQIMBALANCE, MASKDWORD, 0x40000100);
		rtl_set_bbreg(hw, ROFDM0_XBTXIQIMBALANCE, MASKDWORD, 0x40000100);
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

			rtl_set_bbreg(hw, RPDP_ANTA, MASKDWORD, 0x01017038);
			rtl_set_bbreg(hw, RCONFIG_ANTA, MASKDWORD, 0x0f600000);
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

			rtl_set_bbreg(hw, RPDP_ANTA, MASKDWORD, 0x01017038);
			rtl_set_bbreg(hw, RPDP_ANTB, MASKDWORD, 0x01017038);
			rtl_set_bbreg(hw, RCONFIG_ANTA, MASKDWORD, 0x0f600000);
			rtl_set_bbreg(hw, RCONFIG_ANTB, MASKDWORD, 0x0f600000);
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

		/* rssi_table_select: index 0 for 2.4G. 1~3 for 5G */
		rtl_set_bbreg(hw, ROFDM0_AGCRSSITABLE, BIT(6) | BIT(7), 0x1);

		/* fc_area */
		rtl_set_bbreg(hw, ROFDM1_CFOTRACKING, BIT(14) | BIT(13), 0x1);

		/* 5G LAN ON */
		rtl_set_bbreg(hw, 0xB30, 0x00F00000, 0x0);

		/* TX BB gain shift, Just for testchip, 0xc80, 0xc88 */
		if (rtlefuse->internal_pa_5g[rtlhal->interfaceindex])
			rtl_set_bbreg(hw, ROFDM0_XATXIQIMBALANCE, MASKDWORD,
				      0x2d4000b5);
		else
			rtl_set_bbreg(hw, ROFDM0_XATXIQIMBALANCE, MASKDWORD,
				      0x20000080);

		if (rtlhal->macphymode != DUALMAC_DUALPHY) {
			if (rtlefuse->internal_pa_5g[1])
				rtl_set_bbreg(hw, ROFDM0_XBTXIQIMBALANCE,
					      MASKDWORD, 0x2d4000b5);
			else
				rtl_set_bbreg(hw, ROFDM0_XBTXIQIMBALANCE,
					      MASKDWORD, 0x20000080);
		}

		rtl_set_bbreg(hw, 0xB30, BIT(27), 0);

		if (rtlhal->macphymode == DUALMAC_DUALPHY) {
			rtl_set_bbreg(hw, RFPGA0_XAB_RFINTERFACESW,
				      BIT(10) | BIT(6) | BIT(5),
				      (rtlefuse->eeprom_cc & BIT(5)));
			rtl_set_bbreg(hw, RFPGA0_XA_RFINTERFACEOE, BIT(10),
				      ((rtlefuse->eeprom_cc & BIT(4)) >> 4));
			rtl_set_bbreg(hw, RFPGA0_XAB_RFPARAMETER, BIT(15),
				      (rtlefuse->eeprom_cc & BIT(4)) >> 4);

			rtl_set_bbreg(hw, RPDP_ANTA, MASKDWORD, 0x01017098);
			rtl_set_bbreg(hw, RCONFIG_ANTA, MASKDWORD, 0x20000000);
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

			rtl_set_bbreg(hw, RPDP_ANTA, MASKDWORD, 0x01017098);
			rtl_set_bbreg(hw, RPDP_ANTB, MASKDWORD, 0x01017098);
			rtl_set_bbreg(hw, RCONFIG_ANTA, MASKDWORD, 0x20000000);
			rtl_set_bbreg(hw, RCONFIG_ANTB, MASKDWORD, 0x20000000);
		}
	}

	/* update IQK related settings */
	rtl_set_bbreg(hw, ROFDM0_XARXIQIMBALANCE, MASKDWORD, 0x40000100);
	rtl_set_bbreg(hw, ROFDM0_XBRXIQIMBALANCE, MASKDWORD, 0x40000100);
	rtl_set_bbreg(hw, ROFDM0_XCTXAFE, 0xF0000000, 0x00);
	rtl_set_bbreg(hw, ROFDM0_ECCATHRESHOLD, BIT(30) | BIT(28) |
		      BIT(26) | BIT(24), 0x00);
	rtl_set_bbreg(hw, ROFDM0_XDTXAFE, 0xF0000000, 0x00);
	rtl_set_bbreg(hw, ROFDM0_RXIQEXTANTA, 0xF0000000, 0x00);
	rtl_set_bbreg(hw, ROFDM0_AGCRSSITABLE, 0x0000F000, 0x00);

	/* Update RF */
	for (rfpath = RF90_PATH_A; rfpath < rtlphy->num_total_rfpath;
	     rfpath++) {
		if (rtlhal->current_bandtype == BAND_ON_2_4G) {
			/* MOD_AG for RF path_A 0x18 BIT8,BIT16 */
			rtl_set_rfreg(hw, rfpath, RF_CHNLBW, BIT(8) | BIT(16) |
				      BIT(18) | 0xff, 1);

			/* RF0x0b[16:14] =3b'111 */
			rtl_set_rfreg(hw, (enum radio_path)rfpath, 0x0B,
				      0x1c000, 0x07);
		} else {
			/* MOD_AG for RF path_A 0x18 BIT8,BIT16 */
			rtl_set_rfreg(hw, RF90_PATH_A, RF_CHNLBW, RFREG_OFFSET_MASK,
				      0x97524);
		}

		/* Set right channel on RF reg0x18 for another mac. */
		if (rtlhal->interfaceindex == 0 && rtlhal->bandset == BAND_ON_2_4G) {
			/* Set MAC1 default channel if MAC1 not up. */
			if (!(rtl_read_byte(rtlpriv, REG_MAC1) & MAC1_ON)) {
				rtl92du_phy_enable_anotherphy(hw, true);
				rtlhal->during_mac0init_radiob = true;
				rtl_set_rfreg(hw, RF90_PATH_A, RF_CHNLBW,
					      RFREG_OFFSET_MASK, 0x97524);
				rtl92du_phy_powerdown_anotherphy(hw, true);
			}
		} else if (rtlhal->interfaceindex == 1 && rtlhal->bandset == BAND_ON_5G) {
			/* Set MAC0 default channel */
			if (!(rtl_read_byte(rtlpriv, REG_MAC0) & MAC0_ON)) {
				rtl92du_phy_enable_anotherphy(hw, false);
				rtlhal->during_mac1init_radioa = true;
				rtl_set_rfreg(hw, RF90_PATH_A, RF_CHNLBW,
					      RFREG_OFFSET_MASK, 0x87401);
				rtl92du_phy_powerdown_anotherphy(hw, false);
			}
		}
	}

	/* Update for all band. */
	/* DMDP */
	if (rtlphy->rf_type == RF_1T1R) {
		/* Use antenna 0, 0xc04, 0xd04 */
		rtl_set_bbreg(hw, ROFDM0_TRXPATHENABLE, MASKBYTE0, 0x11);
		rtl_set_bbreg(hw, ROFDM1_TRXPATHENABLE, BDWORD, 0x1);

		/* enable ad/da clock1 for dual-phy reg0x888 */
		if (rtlhal->interfaceindex == 0) {
			rtl_set_bbreg(hw, RFPGA0_ADDALLOCKEN, BIT(12) |
				      BIT(13), 0x3);
		} else if (rtl92du_phy_enable_anotherphy(hw, false)) {
			rtlhal->during_mac1init_radioa = true;
			rtl_set_bbreg(hw, RFPGA0_ADDALLOCKEN,
				      BIT(12) | BIT(13), 0x3);
			rtl92du_phy_powerdown_anotherphy(hw, false);
		}

		rtl_set_bbreg(hw, ROFDM1_LSTF, BIT(19) | BIT(20), 0x0);
	} else {
		/* Single PHY */
		/* Use antenna 0 & 1, 0xc04, 0xd04 */
		rtl_set_bbreg(hw, ROFDM0_TRXPATHENABLE, MASKBYTE0, 0x33);
		rtl_set_bbreg(hw, ROFDM1_TRXPATHENABLE, BDWORD, 0x3);
		/* disable ad/da clock1,0x888 */
		rtl_set_bbreg(hw, RFPGA0_ADDALLOCKEN, BIT(12) | BIT(13), 0);

		rtl_set_bbreg(hw, ROFDM1_LSTF, BIT(19) | BIT(20), 0x1);
	}

	for (rfpath = RF90_PATH_A; rfpath < rtlphy->num_total_rfpath;
	     rfpath++) {
		rtlphy->rfreg_chnlval[rfpath] = rtl_get_rfreg(hw, rfpath,
							      RF_CHNLBW,
							      RFREG_OFFSET_MASK);
		rtlphy->reg_rf3c[rfpath] = rtl_get_rfreg(hw, rfpath, 0x3C,
							 RFREG_OFFSET_MASK);
	}

	for (i = 0; i < 2; i++)
		rtl_dbg(rtlpriv, COMP_RF, DBG_LOUD, "RF 0x18 = 0x%x\n",
			rtlphy->rfreg_chnlval[i]);

	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD, "<==\n");
}

bool rtl92du_phy_check_poweroff(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 u1btmp;

	if (rtlhal->macphymode == SINGLEMAC_SINGLEPHY) {
		u1btmp = rtl_read_byte(rtlpriv, REG_MAC0);
		rtl_write_byte(rtlpriv, REG_MAC0, u1btmp & ~MAC0_ON);
		return true;
	}

	mutex_lock(rtlpriv->mutex_for_power_on_off);
	if (rtlhal->interfaceindex == 0) {
		u1btmp = rtl_read_byte(rtlpriv, REG_MAC0);
		rtl_write_byte(rtlpriv, REG_MAC0, u1btmp & ~MAC0_ON);
		u1btmp = rtl_read_byte(rtlpriv, REG_MAC1);
		u1btmp &= MAC1_ON;
	} else {
		u1btmp = rtl_read_byte(rtlpriv, REG_MAC1);
		rtl_write_byte(rtlpriv, REG_MAC1, u1btmp & ~MAC1_ON);
		u1btmp = rtl_read_byte(rtlpriv, REG_MAC0);
		u1btmp &= MAC0_ON;
	}
	if (u1btmp) {
		mutex_unlock(rtlpriv->mutex_for_power_on_off);
		return false;
	}
	u1btmp = rtl_read_byte(rtlpriv, REG_POWER_OFF_IN_PROCESS);
	u1btmp |= BIT(7);
	rtl_write_byte(rtlpriv, REG_POWER_OFF_IN_PROCESS, u1btmp);
	mutex_unlock(rtlpriv->mutex_for_power_on_off);

	return true;
}

void rtl92du_phy_init_pa_bias(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	bool is_single_mac = rtlhal->macphymode == SINGLEMAC_SINGLEPHY;
	enum radio_path rf_path;
	u8 val8;

	read_efuse_byte(hw, 0x3FA, &val8);

	rtl_dbg(rtlpriv, COMP_RF, DBG_TRACE, "%s: 0x3FA %#x\n",
		__func__, val8);

	if (!(val8 & BIT(0)) && (is_single_mac || rtlhal->interfaceindex == 0)) {
		rtl_set_rfreg(hw, RF90_PATH_A, RF_CHNLBW, RFREG_OFFSET_MASK, 0x07401);
		rtl_set_rfreg(hw, RF90_PATH_A, RF_AC, RFREG_OFFSET_MASK, 0x70000);
		rtl_set_rfreg(hw, RF90_PATH_A, RF_IPA, RFREG_OFFSET_MASK, 0x0F425);
		rtl_set_rfreg(hw, RF90_PATH_A, RF_IPA, RFREG_OFFSET_MASK, 0x4F425);
		rtl_set_rfreg(hw, RF90_PATH_A, RF_IPA, RFREG_OFFSET_MASK, 0x8F425);

		/* Back to RX Mode */
		rtl_set_rfreg(hw, RF90_PATH_A, RF_AC, RFREG_OFFSET_MASK, 0x30000);

		rtl_dbg(rtlpriv, COMP_RF, DBG_TRACE, "2G PA BIAS path A\n");
	}

	if (!(val8 & BIT(1)) && (is_single_mac || rtlhal->interfaceindex == 1)) {
		rf_path = rtlhal->interfaceindex == 1 ? RF90_PATH_A : RF90_PATH_B;

		rtl_set_rfreg(hw, rf_path, RF_CHNLBW, RFREG_OFFSET_MASK, 0x07401);
		rtl_set_rfreg(hw, rf_path, RF_AC, RFREG_OFFSET_MASK, 0x70000);
		rtl_set_rfreg(hw, rf_path, RF_IPA, RFREG_OFFSET_MASK, 0x0F425);
		rtl_set_rfreg(hw, rf_path, RF_IPA, RFREG_OFFSET_MASK, 0x4F425);
		rtl_set_rfreg(hw, rf_path, RF_IPA, RFREG_OFFSET_MASK, 0x8F425);

		/* Back to RX Mode */
		rtl_set_rfreg(hw, rf_path, RF_AC, RFREG_OFFSET_MASK, 0x30000);

		rtl_dbg(rtlpriv, COMP_RF, DBG_TRACE, "2G PA BIAS path B\n");
	}

	if (!(val8 & BIT(2)) && (is_single_mac || rtlhal->interfaceindex == 0)) {
		/* 5GL_channel */
		rtl_set_rfreg(hw, RF90_PATH_A, RF_CHNLBW, RFREG_OFFSET_MASK, 0x17524);
		rtl_set_rfreg(hw, RF90_PATH_A, RF_AC, RFREG_OFFSET_MASK, 0x70000);
		rtl_set_rfreg(hw, RF90_PATH_A, RF_IPA, RFREG_OFFSET_MASK, 0x0F496);
		rtl_set_rfreg(hw, RF90_PATH_A, RF_IPA, RFREG_OFFSET_MASK, 0x4F496);
		rtl_set_rfreg(hw, RF90_PATH_A, RF_IPA, RFREG_OFFSET_MASK, 0x8F496);

		/* 5GM_channel */
		rtl_set_rfreg(hw, RF90_PATH_A, RF_CHNLBW, RFREG_OFFSET_MASK, 0x37564);
		rtl_set_rfreg(hw, RF90_PATH_A, RF_AC, RFREG_OFFSET_MASK, 0x70000);
		rtl_set_rfreg(hw, RF90_PATH_A, RF_IPA, RFREG_OFFSET_MASK, 0x0F496);
		rtl_set_rfreg(hw, RF90_PATH_A, RF_IPA, RFREG_OFFSET_MASK, 0x4F496);
		rtl_set_rfreg(hw, RF90_PATH_A, RF_IPA, RFREG_OFFSET_MASK, 0x8F496);

		/* 5GH_channel */
		rtl_set_rfreg(hw, RF90_PATH_A, RF_CHNLBW, RFREG_OFFSET_MASK, 0x57595);
		rtl_set_rfreg(hw, RF90_PATH_A, RF_AC, RFREG_OFFSET_MASK, 0x70000);
		rtl_set_rfreg(hw, RF90_PATH_A, RF_IPA, RFREG_OFFSET_MASK, 0x0F496);
		rtl_set_rfreg(hw, RF90_PATH_A, RF_IPA, RFREG_OFFSET_MASK, 0x4F496);
		rtl_set_rfreg(hw, RF90_PATH_A, RF_IPA, RFREG_OFFSET_MASK, 0x8F496);

		/* Back to RX Mode */
		rtl_set_rfreg(hw, RF90_PATH_A, RF_AC, RFREG_OFFSET_MASK, 0x30000);

		rtl_dbg(rtlpriv, COMP_RF, DBG_TRACE, "5G PA BIAS path A\n");
	}

	if (!(val8 & BIT(3)) && (is_single_mac || rtlhal->interfaceindex == 1)) {
		rf_path = rtlhal->interfaceindex == 1 ? RF90_PATH_A : RF90_PATH_B;

		/* 5GL_channel */
		rtl_set_rfreg(hw, rf_path, RF_CHNLBW, RFREG_OFFSET_MASK, 0x17524);
		rtl_set_rfreg(hw, rf_path, RF_AC, RFREG_OFFSET_MASK, 0x70000);
		rtl_set_rfreg(hw, rf_path, RF_IPA, RFREG_OFFSET_MASK, 0x0F496);
		rtl_set_rfreg(hw, rf_path, RF_IPA, RFREG_OFFSET_MASK, 0x4F496);
		rtl_set_rfreg(hw, rf_path, RF_IPA, RFREG_OFFSET_MASK, 0x8F496);

		/* 5GM_channel */
		rtl_set_rfreg(hw, rf_path, RF_CHNLBW, RFREG_OFFSET_MASK, 0x37564);
		rtl_set_rfreg(hw, rf_path, RF_AC, RFREG_OFFSET_MASK, 0x70000);
		rtl_set_rfreg(hw, rf_path, RF_IPA, RFREG_OFFSET_MASK, 0x0F496);
		rtl_set_rfreg(hw, rf_path, RF_IPA, RFREG_OFFSET_MASK, 0x4F496);
		rtl_set_rfreg(hw, rf_path, RF_IPA, RFREG_OFFSET_MASK, 0x8F496);

		/* 5GH_channel */
		rtl_set_rfreg(hw, rf_path, RF_CHNLBW, RFREG_OFFSET_MASK, 0x57595);
		rtl_set_rfreg(hw, rf_path, RF_AC, RFREG_OFFSET_MASK, 0x70000);
		rtl_set_rfreg(hw, rf_path, RF_IPA, RFREG_OFFSET_MASK, 0x0F496);
		rtl_set_rfreg(hw, rf_path, RF_IPA, RFREG_OFFSET_MASK, 0x4F496);
		rtl_set_rfreg(hw, rf_path, RF_IPA, RFREG_OFFSET_MASK, 0x8F496);

		/* Back to RX Mode */
		rtl_set_rfreg(hw, rf_path, RF_AC, RFREG_OFFSET_MASK, 0x30000);

		rtl_dbg(rtlpriv, COMP_RF, DBG_TRACE, "5G PA BIAS path B\n");
	}
}
