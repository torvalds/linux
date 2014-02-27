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
#include "reg.h"
#include "def.h"
#include "phy.h"
#include "rf.h"
#include "dm.h"
#include "hw.h"

void rtl92d_phy_rf6052_set_bandwidth(struct ieee80211_hw *hw, u8 bandwidth)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u8 rfpath;

	switch (bandwidth) {
	case HT_CHANNEL_WIDTH_20:
		for (rfpath = 0; rfpath < rtlphy->num_total_rfpath; rfpath++) {
			rtlphy->rfreg_chnlval[rfpath] = ((rtlphy->rfreg_chnlval
					[rfpath] & 0xfffff3ff) | 0x0400);
			rtl_set_rfreg(hw, rfpath, RF_CHNLBW, BIT(10) |
				      BIT(11), 0x01);

			RT_TRACE(rtlpriv, COMP_RF, DBG_LOUD,
				 "20M RF 0x18 = 0x%x\n",
				 rtlphy->rfreg_chnlval[rfpath]);
		}

		break;
	case HT_CHANNEL_WIDTH_20_40:
		for (rfpath = 0; rfpath < rtlphy->num_total_rfpath; rfpath++) {
			rtlphy->rfreg_chnlval[rfpath] =
			    ((rtlphy->rfreg_chnlval[rfpath] & 0xfffff3ff));
			rtl_set_rfreg(hw, rfpath, RF_CHNLBW, BIT(10) | BIT(11),
				      0x00);
			RT_TRACE(rtlpriv, COMP_RF, DBG_LOUD,
				 "40M RF 0x18 = 0x%x\n",
				 rtlphy->rfreg_chnlval[rfpath]);
		}
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "unknown bandwidth: %#X\n", bandwidth);
		break;
	}
}

void rtl92d_phy_rf6052_set_cck_txpower(struct ieee80211_hw *hw,
				       u8 *ppowerlevel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u32 tx_agc[2] = {0, 0}, tmpval;
	bool turbo_scanoff = false;
	u8 idx1, idx2;
	u8 *ptr;

	if (rtlefuse->eeprom_regulatory != 0)
		turbo_scanoff = true;
	if (mac->act_scanning) {
		tx_agc[RF90_PATH_A] = 0x3f3f3f3f;
		tx_agc[RF90_PATH_B] = 0x3f3f3f3f;
		if (turbo_scanoff) {
			for (idx1 = RF90_PATH_A; idx1 <= RF90_PATH_B; idx1++) {
				tx_agc[idx1] = ppowerlevel[idx1] |
				    (ppowerlevel[idx1] << 8) |
				    (ppowerlevel[idx1] << 16) |
				    (ppowerlevel[idx1] << 24);
			}
		}
	} else {
		for (idx1 = RF90_PATH_A; idx1 <= RF90_PATH_B; idx1++) {
			tx_agc[idx1] = ppowerlevel[idx1] |
			    (ppowerlevel[idx1] << 8) |
			    (ppowerlevel[idx1] << 16) |
			    (ppowerlevel[idx1] << 24);
		}
		if (rtlefuse->eeprom_regulatory == 0) {
			tmpval = (rtlphy->mcs_offset[0][6]) +
			    (rtlphy->mcs_offset[0][7] << 8);
			tx_agc[RF90_PATH_A] += tmpval;
			tmpval = (rtlphy->mcs_offset[0][14]) +
			    (rtlphy->mcs_offset[0][15] << 24);
			tx_agc[RF90_PATH_B] += tmpval;
		}
	}

	for (idx1 = RF90_PATH_A; idx1 <= RF90_PATH_B; idx1++) {
		ptr = (u8 *) (&(tx_agc[idx1]));
		for (idx2 = 0; idx2 < 4; idx2++) {
			if (*ptr > RF6052_MAX_TX_PWR)
				*ptr = RF6052_MAX_TX_PWR;
			ptr++;
		}
	}

	tmpval = tx_agc[RF90_PATH_A] & 0xff;
	rtl_set_bbreg(hw, RTXAGC_A_CCK1_MCS32, BMASKBYTE1, tmpval);
	RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
		"CCK PWR 1M (rf-A) = 0x%x (reg 0x%x)\n",
		tmpval, RTXAGC_A_CCK1_MCS32);
	tmpval = tx_agc[RF90_PATH_A] >> 8;
	rtl_set_bbreg(hw, RTXAGC_B_CCK11_A_CCK2_11, 0xffffff00, tmpval);
	RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
		"CCK PWR 2~11M (rf-A) = 0x%x (reg 0x%x)\n",
		tmpval, RTXAGC_B_CCK11_A_CCK2_11);
	tmpval = tx_agc[RF90_PATH_B] >> 24;
	rtl_set_bbreg(hw, RTXAGC_B_CCK11_A_CCK2_11, BMASKBYTE0, tmpval);
	RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
		"CCK PWR 11M (rf-B) = 0x%x (reg 0x%x)\n",
		tmpval, RTXAGC_B_CCK11_A_CCK2_11);
	tmpval = tx_agc[RF90_PATH_B] & 0x00ffffff;
	rtl_set_bbreg(hw, RTXAGC_B_CCK1_55_MCS32, 0xffffff00, tmpval);
	RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
		"CCK PWR 1~5.5M (rf-B) = 0x%x (reg 0x%x)\n",
		tmpval, RTXAGC_B_CCK1_55_MCS32);
}

static void _rtl92d_phy_get_power_base(struct ieee80211_hw *hw,
				       u8 *ppowerlevel, u8 channel,
				       u32 *ofdmbase, u32 *mcsbase)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u32 powerbase0, powerbase1;
	u8 legacy_pwrdiff, ht20_pwrdiff;
	u8 i, powerlevel[2];

	for (i = 0; i < 2; i++) {
		powerlevel[i] = ppowerlevel[i];
		legacy_pwrdiff = rtlefuse->txpwr_legacyhtdiff[i][channel - 1];
		powerbase0 = powerlevel[i] + legacy_pwrdiff;
		powerbase0 = (powerbase0 << 24) | (powerbase0 << 16) |
		    (powerbase0 << 8) | powerbase0;
		*(ofdmbase + i) = powerbase0;
		RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
			" [OFDM power base index rf(%c) = 0x%x]\n",
			i == 0 ? 'A' : 'B', *(ofdmbase + i));
	}

	for (i = 0; i < 2; i++) {
		if (rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_20) {
			ht20_pwrdiff = rtlefuse->txpwr_ht20diff[i][channel - 1];
			powerlevel[i] += ht20_pwrdiff;
		}
		powerbase1 = powerlevel[i];
		powerbase1 = (powerbase1 << 24) | (powerbase1 << 16) |
			     (powerbase1 << 8) | powerbase1;
		*(mcsbase + i) = powerbase1;
		RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
			" [MCS power base index rf(%c) = 0x%x]\n",
			i == 0 ? 'A' : 'B', *(mcsbase + i));
	}
}

static u8 _rtl92d_phy_get_chnlgroup_bypg(u8 chnlindex)
{
	u8 group;
	u8 channel_info[59] = {
		1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
		36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58,
		60, 62, 64, 100, 102, 104, 106, 108, 110, 112,
		114, 116, 118, 120, 122, 124, 126, 128,	130, 132,
		134, 136, 138, 140, 149, 151, 153, 155, 157, 159,
		161, 163, 165
	};

	if (channel_info[chnlindex] <= 3)	/* Chanel 1-3 */
		group = 0;
	else if (channel_info[chnlindex] <= 9)	/* Channel 4-9 */
		group = 1;
	else if (channel_info[chnlindex] <= 14)	/* Channel 10-14 */
		group = 2;
	else if (channel_info[chnlindex] <= 64)
		group = 6;
	else if (channel_info[chnlindex] <= 140)
		group = 7;
	else
		group = 8;
	return group;
}

static void _rtl92d_get_txpower_writeval_by_regulatory(struct ieee80211_hw *hw,
						       u8 channel, u8 index,
						       u32 *powerbase0,
						       u32 *powerbase1,
						       u32 *p_outwriteval)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u8 i, chnlgroup = 0, pwr_diff_limit[4];
	u32 writeval = 0, customer_limit, rf;

	for (rf = 0; rf < 2; rf++) {
		switch (rtlefuse->eeprom_regulatory) {
		case 0:
			chnlgroup = 0;
			writeval = rtlphy->mcs_offset
					[chnlgroup][index +
					(rf ? 8 : 0)] + ((index < 2) ?
					powerbase0[rf] :
					powerbase1[rf]);
			RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
				"RTK better performance, writeval(%c) = 0x%x\n",
				rf == 0 ? 'A' : 'B', writeval);
			break;
		case 1:
			if (rtlphy->pwrgroup_cnt == 1)
				chnlgroup = 0;
			if (rtlphy->pwrgroup_cnt >= MAX_PG_GROUP) {
				chnlgroup = _rtl92d_phy_get_chnlgroup_bypg(
								channel - 1);
				if (rtlphy->current_chan_bw ==
				    HT_CHANNEL_WIDTH_20)
					chnlgroup++;
				else
					chnlgroup += 4;
				writeval = rtlphy->mcs_offset
						[chnlgroup][index +
						(rf ? 8 : 0)] + ((index < 2) ?
						powerbase0[rf] :
						powerbase1[rf]);
				RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
					"Realtek regulatory, 20MHz, writeval(%c) = 0x%x\n",
					rf == 0 ? 'A' : 'B', writeval);
			}
			break;
		case 2:
			writeval = ((index < 2) ? powerbase0[rf] :
				   powerbase1[rf]);
			RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
				"Better regulatory, writeval(%c) = 0x%x\n",
				rf == 0 ? 'A' : 'B', writeval);
			break;
		case 3:
			chnlgroup = 0;
			if (rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_20_40) {
				RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
					"customer's limit, 40MHz rf(%c) = 0x%x\n",
					rf == 0 ? 'A' : 'B',
					rtlefuse->pwrgroup_ht40[rf]
					[channel - 1]);
			} else {
				RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
					"customer's limit, 20MHz rf(%c) = 0x%x\n",
					rf == 0 ? 'A' : 'B',
					rtlefuse->pwrgroup_ht20[rf]
					[channel - 1]);
			}
			for (i = 0; i < 4; i++) {
				pwr_diff_limit[i] = (u8)((rtlphy->mcs_offset
					[chnlgroup][index + (rf ? 8 : 0)] &
					(0x7f << (i * 8))) >> (i * 8));
				if (rtlphy->current_chan_bw ==
				    HT_CHANNEL_WIDTH_20_40) {
					if (pwr_diff_limit[i] >
					    rtlefuse->pwrgroup_ht40[rf]
					   [channel - 1])
						pwr_diff_limit[i] =
							rtlefuse->pwrgroup_ht40
							[rf][channel - 1];
				} else {
					if (pwr_diff_limit[i] >
					    rtlefuse->pwrgroup_ht20[rf][
						channel - 1])
						pwr_diff_limit[i] =
						   rtlefuse->pwrgroup_ht20[rf]
						   [channel - 1];
				}
			}
			customer_limit = (pwr_diff_limit[3] << 24) |
					 (pwr_diff_limit[2] << 16) |
					 (pwr_diff_limit[1] << 8) |
					 (pwr_diff_limit[0]);
			RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
				"Customer's limit rf(%c) = 0x%x\n",
				rf == 0 ? 'A' : 'B', customer_limit);
			writeval = customer_limit + ((index < 2) ?
				   powerbase0[rf] : powerbase1[rf]);
			RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
				"Customer, writeval rf(%c)= 0x%x\n",
				rf == 0 ? 'A' : 'B', writeval);
			break;
		default:
			chnlgroup = 0;
			writeval = rtlphy->mcs_offset[chnlgroup][index +
				   (rf ? 8 : 0)] + ((index < 2) ?
				   powerbase0[rf] : powerbase1[rf]);
			RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
				"RTK better performance, writeval rf(%c) = 0x%x\n",
				rf == 0 ? 'A' : 'B', writeval);
			break;
		}
		*(p_outwriteval + rf) = writeval;
	}
}

static void _rtl92d_write_ofdm_power_reg(struct ieee80211_hw *hw,
					 u8 index, u32 *pvalue)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	static u16 regoffset_a[6] = {
		RTXAGC_A_RATE18_06, RTXAGC_A_RATE54_24,
		RTXAGC_A_MCS03_MCS00, RTXAGC_A_MCS07_MCS04,
		RTXAGC_A_MCS11_MCS08, RTXAGC_A_MCS15_MCS12
	};
	static u16 regoffset_b[6] = {
		RTXAGC_B_RATE18_06, RTXAGC_B_RATE54_24,
		RTXAGC_B_MCS03_MCS00, RTXAGC_B_MCS07_MCS04,
		RTXAGC_B_MCS11_MCS08, RTXAGC_B_MCS15_MCS12
	};
	u8 i, rf, pwr_val[4];
	u32 writeval;
	u16 regoffset;

	for (rf = 0; rf < 2; rf++) {
		writeval = pvalue[rf];
		for (i = 0; i < 4; i++) {
			pwr_val[i] = (u8) ((writeval & (0x7f <<
				     (i * 8))) >> (i * 8));
			if (pwr_val[i] > RF6052_MAX_TX_PWR)
				pwr_val[i] = RF6052_MAX_TX_PWR;
		}
		writeval = (pwr_val[3] << 24) | (pwr_val[2] << 16) |
			   (pwr_val[1] << 8) | pwr_val[0];
		if (rf == 0)
			regoffset = regoffset_a[index];
		else
			regoffset = regoffset_b[index];
		rtl_set_bbreg(hw, regoffset, BMASKDWORD, writeval);
		RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
			"Set 0x%x = %08x\n", regoffset, writeval);
		if (((get_rf_type(rtlphy) == RF_2T2R) &&
		    (regoffset == RTXAGC_A_MCS15_MCS12 ||
		    regoffset == RTXAGC_B_MCS15_MCS12)) ||
		    ((get_rf_type(rtlphy) != RF_2T2R) &&
		    (regoffset == RTXAGC_A_MCS07_MCS04 ||
		    regoffset == RTXAGC_B_MCS07_MCS04))) {
			writeval = pwr_val[3];
			if (regoffset == RTXAGC_A_MCS15_MCS12 ||
			    regoffset == RTXAGC_A_MCS07_MCS04)
				regoffset = 0xc90;
			if (regoffset == RTXAGC_B_MCS15_MCS12 ||
			    regoffset == RTXAGC_B_MCS07_MCS04)
				regoffset = 0xc98;
			for (i = 0; i < 3; i++) {
				if (i != 2)
					writeval = (writeval > 8) ?
						   (writeval - 8) : 0;
				else
					writeval = (writeval > 6) ?
						   (writeval - 6) : 0;
				rtl_write_byte(rtlpriv, (u32) (regoffset + i),
					       (u8) writeval);
			}
		}
	}
}

void rtl92d_phy_rf6052_set_ofdm_txpower(struct ieee80211_hw *hw,
					u8 *ppowerlevel, u8 channel)
{
	u32 writeval[2], powerbase0[2], powerbase1[2];
	u8 index;

	_rtl92d_phy_get_power_base(hw, ppowerlevel, channel,
			&powerbase0[0],	&powerbase1[0]);
	for (index = 0; index < 6; index++) {
		_rtl92d_get_txpower_writeval_by_regulatory(hw,
				channel, index,	&powerbase0[0],
				&powerbase1[0],	&writeval[0]);
		_rtl92d_write_ofdm_power_reg(hw, index, &writeval[0]);
	}
}

bool rtl92d_phy_enable_anotherphy(struct ieee80211_hw *hw, bool bmac0)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = &(rtlpriv->rtlhal);
	u8 u1btmp;
	u8 direct = bmac0 ? BIT(3) | BIT(2) : BIT(3);
	u8 mac_reg = bmac0 ? REG_MAC1 : REG_MAC0;
	u8 mac_on_bit = bmac0 ? MAC1_ON : MAC0_ON;
	bool bresult = true; /* true: need to enable BB/RF power */

	rtlhal->during_mac0init_radiob = false;
	rtlhal->during_mac1init_radioa = false;
	RT_TRACE(rtlpriv, COMP_RF, DBG_LOUD, "===>\n");
	/* MAC0 Need PHY1 load radio_b.txt . Driver use DBI to write. */
	u1btmp = rtl_read_byte(rtlpriv, mac_reg);
	if (!(u1btmp & mac_on_bit)) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "enable BB & RF\n");
		/* Enable BB and RF power */
		rtl92de_write_dword_dbi(hw, REG_SYS_ISO_CTRL,
			rtl92de_read_dword_dbi(hw, REG_SYS_ISO_CTRL, direct) |
				BIT(29) | BIT(16) | BIT(17), direct);
	} else {
		/* We think if MAC1 is ON,then radio_a.txt
		 * and radio_b.txt has been load. */
		bresult = false;
	}
	RT_TRACE(rtlpriv, COMP_RF, DBG_LOUD, "<===\n");
	return bresult;

}

void rtl92d_phy_powerdown_anotherphy(struct ieee80211_hw *hw, bool bmac0)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = &(rtlpriv->rtlhal);
	u8 u1btmp;
	u8 direct = bmac0 ? BIT(3) | BIT(2) : BIT(3);
	u8 mac_reg = bmac0 ? REG_MAC1 : REG_MAC0;
	u8 mac_on_bit = bmac0 ? MAC1_ON : MAC0_ON;

	rtlhal->during_mac0init_radiob = false;
	rtlhal->during_mac1init_radioa = false;
	RT_TRACE(rtlpriv, COMP_RF, DBG_LOUD, "====>\n");
	/* check MAC0 enable or not again now, if
	 * enabled, not power down radio A. */
	u1btmp = rtl_read_byte(rtlpriv, mac_reg);
	if (!(u1btmp & mac_on_bit)) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "power down\n");
		/* power down RF radio A according to YuNan's advice. */
		rtl92de_write_dword_dbi(hw, RFPGA0_XA_LSSIPARAMETER,
					0x00000000, direct);
	}
	RT_TRACE(rtlpriv, COMP_RF, DBG_LOUD, "<====\n");
}

bool rtl92d_phy_rf6052_config(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	bool rtstatus = true;
	struct rtl_hal *rtlhal = &(rtlpriv->rtlhal);
	u32 u4_regvalue = 0;
	u8 rfpath;
	struct bb_reg_def *pphyreg;
	bool mac1_initradioa_first = false, mac0_initradiob_first = false;
	bool need_pwrdown_radioa = false, need_pwrdown_radiob = false;
	bool true_bpath = false;

	if (rtlphy->rf_type == RF_1T1R)
		rtlphy->num_total_rfpath = 1;
	else
		rtlphy->num_total_rfpath = 2;

	/* Single phy mode: use radio_a radio_b config path_A path_B */
	/* seperately by MAC0, and MAC1 needn't configure RF; */
	/* Dual PHY mode:MAC0 use radio_a config 1st phy path_A, */
	/* MAC1 use radio_b config 2nd PHY path_A. */
	/* DMDP,MAC0 on G band,MAC1 on A band. */
	if (rtlhal->macphymode == DUALMAC_DUALPHY) {
		if (rtlhal->current_bandtype == BAND_ON_2_4G &&
		    rtlhal->interfaceindex == 0) {
			/* MAC0 needs PHY1 load radio_b.txt.
			 * Driver use DBI to write. */
			if (rtl92d_phy_enable_anotherphy(hw, true)) {
				rtlphy->num_total_rfpath = 2;
				mac0_initradiob_first = true;
			} else {
				/* We think if MAC1 is ON,then radio_a.txt and
				 * radio_b.txt has been load. */
				return rtstatus;
			}
		} else if (rtlhal->current_bandtype == BAND_ON_5G &&
			   rtlhal->interfaceindex == 1) {
			/* MAC1 needs PHY0 load radio_a.txt.
			 * Driver use DBI to write. */
			if (rtl92d_phy_enable_anotherphy(hw, false)) {
				rtlphy->num_total_rfpath = 2;
				mac1_initradioa_first = true;
			} else {
				/* We think if MAC0 is ON,then radio_a.txt and
				 * radio_b.txt has been load. */
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
		/* Set 1 to 4 bits for 8255 */
		rtl_set_bbreg(hw, pphyreg->rfhssi_para2,
			      B3WIREADDRESSLENGTH, 0x0);
		udelay(1);
		/* Set 0 to 12  bits for 8255 */
		rtl_set_bbreg(hw, pphyreg->rfhssi_para2, B3WIREDATALENGTH, 0x0);
		udelay(1);
		switch (rfpath) {
		case RF90_PATH_A:
			if (true_bpath)
				rtstatus = rtl92d_phy_config_rf_with_headerfile(
						hw, radiob_txt,
						(enum radio_path)rfpath);
			else
				rtstatus = rtl92d_phy_config_rf_with_headerfile(
					     hw, radioa_txt,
					     (enum radio_path)rfpath);
			break;
		case RF90_PATH_B:
			rtstatus =
			    rtl92d_phy_config_rf_with_headerfile(hw, radiob_txt,
						(enum radio_path) rfpath);
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
			RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
				 "Radio[%d] Fail!!", rfpath);
			goto phy_rf_cfg_fail;
		}

	}

	/* check MAC0 enable or not again, if enabled,
	 * not power down radio A. */
	/* check MAC1 enable or not again, if enabled,
	 * not power down radio B. */
	if (need_pwrdown_radioa)
		rtl92d_phy_powerdown_anotherphy(hw, false);
	else if (need_pwrdown_radiob)
		rtl92d_phy_powerdown_anotherphy(hw, true);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE, "<---\n");
	return rtstatus;

phy_rf_cfg_fail:
	return rtstatus;
}
