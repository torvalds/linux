// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2024  Realtek Corporation.*/

#include "../wifi.h"
#include "../rtl8192d/reg.h"
#include "../rtl8192d/phy_common.h"
#include "phy.h"
#include "rf.h"

bool rtl92du_phy_enable_anotherphy(struct ieee80211_hw *hw, bool bmac0)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = &rtlpriv->rtlhal;
	u8 mac_on_bit = bmac0 ? MAC1_ON : MAC0_ON;
	u8 mac_reg = bmac0 ? REG_MAC1 : REG_MAC0;
	bool bresult = true; /* true: need to enable BB/RF power */
	u32 maskforphyset = 0;
	u16 val16;
	u8 u1btmp;

	rtlhal->during_mac0init_radiob = false;
	rtlhal->during_mac1init_radioa = false;
	rtl_dbg(rtlpriv, COMP_RF, DBG_LOUD, "===>\n");

	/* MAC0 Need PHY1 load radio_b.txt . Driver use DBI to write. */
	u1btmp = rtl_read_byte(rtlpriv, mac_reg);
	if (!(u1btmp & mac_on_bit)) {
		rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD, "enable BB & RF\n");
		/* Enable BB and RF power */

		maskforphyset = bmac0 ? MAC0_ACCESS_PHY1 : MAC1_ACCESS_PHY0;

		val16 = rtl_read_word(rtlpriv, REG_SYS_FUNC_EN | maskforphyset);
		val16 &= 0xfffc;
		rtl_write_word(rtlpriv, REG_SYS_FUNC_EN | maskforphyset, val16);

		val16 = rtl_read_word(rtlpriv, REG_SYS_FUNC_EN | maskforphyset);
		val16 |= BIT(13) | BIT(0) | BIT(1);
		rtl_write_word(rtlpriv, REG_SYS_FUNC_EN | maskforphyset, val16);
	} else {
		/* We think if MAC1 is ON,then radio_a.txt
		 * and radio_b.txt has been load.
		 */
		bresult = false;
	}
	rtl_dbg(rtlpriv, COMP_RF, DBG_LOUD, "<===\n");
	return bresult;
}

void rtl92du_phy_powerdown_anotherphy(struct ieee80211_hw *hw, bool bmac0)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = &rtlpriv->rtlhal;
	u8 mac_on_bit = bmac0 ? MAC1_ON : MAC0_ON;
	u8 mac_reg = bmac0 ? REG_MAC1 : REG_MAC0;
	u32 maskforphyset = 0;
	u8 u1btmp;

	rtlhal->during_mac0init_radiob = false;
	rtlhal->during_mac1init_radioa = false;
	rtl_dbg(rtlpriv, COMP_RF, DBG_LOUD, "====>\n");

	/* check MAC0 enable or not again now, if
	 * enabled, not power down radio A.
	 */
	u1btmp = rtl_read_byte(rtlpriv, mac_reg);
	if (!(u1btmp & mac_on_bit)) {
		rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD, "power down\n");
		/* power down RF radio A according to YuNan's advice. */
		maskforphyset = bmac0 ? MAC0_ACCESS_PHY1 : MAC1_ACCESS_PHY0;
		rtl_write_dword(rtlpriv, RFPGA0_XA_LSSIPARAMETER | maskforphyset,
				0x00000000);
	}
	rtl_dbg(rtlpriv, COMP_RF, DBG_LOUD, "<====\n");
}

bool rtl92du_phy_rf6052_config(struct ieee80211_hw *hw)
{
	bool mac1_initradioa_first = false, mac0_initradiob_first = false;
	bool need_pwrdown_radioa = false, need_pwrdown_radiob = false;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = &rtlpriv->rtlhal;
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct bb_reg_def *pphyreg;
	bool true_bpath = false;
	bool rtstatus = true;
	u32 u4_regvalue = 0;
	u8 rfpath;

	if (rtlphy->rf_type == RF_1T1R)
		rtlphy->num_total_rfpath = 1;
	else
		rtlphy->num_total_rfpath = 2;

	/* Single phy mode: use radio_a radio_b config path_A path_B
	 * separately by MAC0, and MAC1 needn't configure RF;
	 * Dual PHY mode: MAC0 use radio_a config 1st phy path_A,
	 * MAC1 use radio_b config 2nd PHY path_A.
	 * DMDP, MAC0 on G band, MAC1 on A band.
	 */
	if (rtlhal->macphymode == DUALMAC_DUALPHY) {
		if (rtlhal->current_bandtype == BAND_ON_2_4G &&
		    rtlhal->interfaceindex == 0) {
			/* MAC0 needs PHY1 load radio_b.txt. */
			if (rtl92du_phy_enable_anotherphy(hw, true)) {
				rtlphy->num_total_rfpath = 2;
				mac0_initradiob_first = true;
			} else {
				/* We think if MAC1 is ON,then radio_a.txt and
				 * radio_b.txt has been load.
				 */
				return rtstatus;
			}
		} else if (rtlhal->current_bandtype == BAND_ON_5G &&
			   rtlhal->interfaceindex == 1) {
			/* MAC1 needs PHY0 load radio_a.txt. */
			if (rtl92du_phy_enable_anotherphy(hw, false)) {
				rtlphy->num_total_rfpath = 2;
				mac1_initradioa_first = true;
			} else {
				/* We think if MAC0 is ON, then radio_a.txt and
				 * radio_b.txt has been load.
				 */
				return rtstatus;
			}
		} else if (rtlhal->interfaceindex == 1) {
			/* MAC0 enabled, only init radia B.   */
			true_bpath = true;
		}
	}

	for (rfpath = 0; rfpath < rtlphy->num_total_rfpath; rfpath++) {
		/* Mac1 use PHY0 write */
		if (mac1_initradioa_first) {
			if (rfpath == RF90_PATH_A) {
				rtlhal->during_mac1init_radioa = true;
				need_pwrdown_radioa = true;
			} else if (rfpath == RF90_PATH_B) {
				rtlhal->during_mac1init_radioa = false;
				mac1_initradioa_first = false;
				rfpath = RF90_PATH_A;
				true_bpath = true;
				rtlphy->num_total_rfpath = 1;
			}
		} else if (mac0_initradiob_first) {
			/* Mac0 use PHY1 write */
			if (rfpath == RF90_PATH_A)
				rtlhal->during_mac0init_radiob = false;
			if (rfpath == RF90_PATH_B) {
				rtlhal->during_mac0init_radiob = true;
				mac0_initradiob_first = false;
				need_pwrdown_radiob = true;
				rfpath = RF90_PATH_A;
				true_bpath = true;
				rtlphy->num_total_rfpath = 1;
			}
		}

		pphyreg = &rtlphy->phyreg_def[rfpath];

		switch (rfpath) {
		case RF90_PATH_A:
		case RF90_PATH_C:
			u4_regvalue = rtl_get_bbreg(hw, pphyreg->rfintfs,
						    BRFSI_RFENV);
			break;
		case RF90_PATH_B:
		case RF90_PATH_D:
			u4_regvalue = rtl_get_bbreg(hw, pphyreg->rfintfs,
						    BRFSI_RFENV << 16);
			break;
		}

		rtl_set_bbreg(hw, pphyreg->rfintfe, BRFSI_RFENV << 16, 0x1);
		udelay(1);
		rtl_set_bbreg(hw, pphyreg->rfintfo, BRFSI_RFENV, 0x1);
		udelay(1);

		/* Set bit number of Address and Data for RF register */
		rtl_set_bbreg(hw, pphyreg->rfhssi_para2,
			      B3WIREADDRESSLENGTH, 0x0);
		udelay(1);
		rtl_set_bbreg(hw, pphyreg->rfhssi_para2, B3WIREDATALENGTH, 0x0);
		udelay(1);

		switch (rfpath) {
		case RF90_PATH_A:
			if (true_bpath)
				rtstatus = rtl92du_phy_config_rf_with_headerfile(
						hw, radiob_txt,
						(enum radio_path)rfpath);
			else
				rtstatus = rtl92du_phy_config_rf_with_headerfile(
						hw, radioa_txt,
						(enum radio_path)rfpath);
			break;
		case RF90_PATH_B:
			rtstatus =
			    rtl92du_phy_config_rf_with_headerfile(hw, radiob_txt,
						(enum radio_path)rfpath);
			break;
		case RF90_PATH_C:
			break;
		case RF90_PATH_D:
			break;
		}

		switch (rfpath) {
		case RF90_PATH_A:
		case RF90_PATH_C:
			rtl_set_bbreg(hw, pphyreg->rfintfs, BRFSI_RFENV,
				      u4_regvalue);
			break;
		case RF90_PATH_B:
		case RF90_PATH_D:
			rtl_set_bbreg(hw, pphyreg->rfintfs, BRFSI_RFENV << 16,
				      u4_regvalue);
			break;
		}

		if (!rtstatus) {
			rtl_dbg(rtlpriv, COMP_INIT, DBG_TRACE,
				"Radio[%d] Fail!!\n", rfpath);
			return rtstatus;
		}
	}

	/* check MAC0 enable or not again, if enabled,
	 * not power down radio A.
	 * check MAC1 enable or not again, if enabled,
	 * not power down radio B.
	 */
	if (need_pwrdown_radioa)
		rtl92du_phy_powerdown_anotherphy(hw, false);
	else if (need_pwrdown_radiob)
		rtl92du_phy_powerdown_anotherphy(hw, true);
	rtl_dbg(rtlpriv, COMP_INIT, DBG_TRACE, "<---\n");

	return rtstatus;
}
