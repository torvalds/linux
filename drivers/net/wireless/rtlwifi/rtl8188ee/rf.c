/******************************************************************************
 *
 * Copyright(c) 2009-2013  Realtek Corporation.
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

void rtl88e_phy_rf6052_set_bandwidth(struct ieee80211_hw *hw, u8 bandwidth)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);

	switch (bandwidth) {
	case HT_CHANNEL_WIDTH_20:
		rtlphy->rfreg_chnlval[0] = ((rtlphy->rfreg_chnlval[0] &
					     0xfffff3ff) | BIT(10) | BIT(11));
		rtl_set_rfreg(hw, RF90_PATH_A, RF_CHNLBW, RFREG_OFFSET_MASK,
			      rtlphy->rfreg_chnlval[0]);
		break;
	case HT_CHANNEL_WIDTH_20_40:
		rtlphy->rfreg_chnlval[0] = ((rtlphy->rfreg_chnlval[0] &
					     0xfffff3ff) | BIT(10));
		rtl_set_rfreg(hw, RF90_PATH_A, RF_CHNLBW, RFREG_OFFSET_MASK,
			      rtlphy->rfreg_chnlval[0]);
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "unknown bandwidth: %#X\n", bandwidth);
		break;
	}
}

void rtl88e_phy_rf6052_set_cck_txpower(struct ieee80211_hw *hw,
				       u8 *plevel)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u32 tx_agc[2] = {0, 0}, tmpval;
	bool turbo_scanoff = false;
	u8 idx1, idx2;
	u8 *ptr;
	u8 direction;
	u32 pwrtrac_value;

	if (rtlefuse->eeprom_regulatory != 0)
		turbo_scanoff = true;

	if (mac->act_scanning == true) {
		tx_agc[RF90_PATH_A] = 0x3f3f3f3f;
		tx_agc[RF90_PATH_B] = 0x3f3f3f3f;

		if (turbo_scanoff) {
			for (idx1 = RF90_PATH_A; idx1 <= RF90_PATH_B; idx1++) {
				tx_agc[idx1] = plevel[idx1] |
					       (plevel[idx1] << 8) |
					       (plevel[idx1] << 16) |
					       (plevel[idx1] << 24);
			}
		}
	} else {
		for (idx1 = RF90_PATH_A; idx1 <= RF90_PATH_B; idx1++) {
			tx_agc[idx1] = plevel[idx1] | (plevel[idx1] << 8) |
				       (plevel[idx1] << 16) |
				       (plevel[idx1] << 24);
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
		ptr = (u8 *)(&(tx_agc[idx1]));
		for (idx2 = 0; idx2 < 4; idx2++) {
			if (*ptr > RF6052_MAX_TX_PWR)
				*ptr = RF6052_MAX_TX_PWR;
			ptr++;
		}
	}
	rtl88e_dm_txpower_track_adjust(hw, 1, &direction, &pwrtrac_value);
	if (direction == 1) {
		tx_agc[0] += pwrtrac_value;
		tx_agc[1] += pwrtrac_value;
	} else if (direction == 2) {
		tx_agc[0] -= pwrtrac_value;
		tx_agc[1] -= pwrtrac_value;
	}
	tmpval = tx_agc[RF90_PATH_A] & 0xff;
	rtl_set_bbreg(hw, RTXAGC_A_CCK1_MCS32, MASKBYTE1, tmpval);

	RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
		"CCK PWR 1M (rf-A) = 0x%x (reg 0x%x)\n", tmpval,
		RTXAGC_A_CCK1_MCS32);

	tmpval = tx_agc[RF90_PATH_A] >> 8;

	rtl_set_bbreg(hw, RTXAGC_B_CCK11_A_CCK2_11, 0xffffff00, tmpval);

	RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
		"CCK PWR 2~11M (rf-A) = 0x%x (reg 0x%x)\n", tmpval,
		 RTXAGC_B_CCK11_A_CCK2_11);

	tmpval = tx_agc[RF90_PATH_B] >> 24;
	rtl_set_bbreg(hw, RTXAGC_B_CCK11_A_CCK2_11, MASKBYTE0, tmpval);

	RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
		"CCK PWR 11M (rf-B) = 0x%x (reg 0x%x)\n", tmpval,
		 RTXAGC_B_CCK11_A_CCK2_11);

	tmpval = tx_agc[RF90_PATH_B] & 0x00ffffff;
	rtl_set_bbreg(hw, RTXAGC_B_CCK1_55_MCS32, 0xffffff00, tmpval);

	RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
		"CCK PWR 1~5.5M (rf-B) = 0x%x (reg 0x%x)\n", tmpval,
		 RTXAGC_B_CCK1_55_MCS32);
}

static void rtl88e_phy_get_power_base(struct ieee80211_hw *hw,
				      u8 *pwrlvlofdm, u8 *pwrlvlbw20,
				      u8 *pwrlvlbw40, u8 channel,
				      u32 *ofdmbase, u32 *mcsbase)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u32 base0, base1;
	u8 i, powerlevel[2];

	for (i = 0; i < 2; i++) {
		base0 = pwrlvlofdm[i];

		base0 = (base0 << 24) | (base0 << 16) |
			     (base0 << 8) | base0;
		*(ofdmbase + i) = base0;
		RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
			"[OFDM power base index rf(%c) = 0x%x]\n",
			((i == 0) ? 'A' : 'B'), *(ofdmbase + i));
	}

	for (i = 0; i < 2; i++) {
		if (rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_20)
			powerlevel[i] = pwrlvlbw20[i];
		else
			powerlevel[i] = pwrlvlbw40[i];
		base1 = powerlevel[i];
		base1 = (base1 << 24) |
		    (base1 << 16) | (base1 << 8) | base1;

		*(mcsbase + i) = base1;

		RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
			"[MCS power base index rf(%c) = 0x%x]\n",
			((i == 0) ? 'A' : 'B'), *(mcsbase + i));
	}
}

static void get_txpwr_by_reg(struct ieee80211_hw *hw, u8 chan, u8 index,
			     u32 *base0, u32 *base1, u32 *outval)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u8 i, chg = 0, pwr_lim[4], pwr_diff = 0, cust_pwr_dif;
	u32 writeval, cust_lim, rf, tmp;
	u8 ch = chan - 1;
	u8 j;

	for (rf = 0; rf < 2; rf++) {
		j = index + (rf ? 8 : 0);
		tmp = ((index < 2) ? base0[rf] : base1[rf]);
		switch (rtlefuse->eeprom_regulatory) {
		case 0:
			chg = 0;

			writeval = rtlphy->mcs_offset[chg][j] + tmp;

			RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
				"RTK better performance, "
				"writeval(%c) = 0x%x\n",
				((rf == 0) ? 'A' : 'B'), writeval);
			break;
		case 1:
			if (rtlphy->pwrgroup_cnt == 1) {
				chg = 0;
			} else {
				chg = chan / 3;
				if (chan == 14)
					chg = 5;
			}
			writeval = rtlphy->mcs_offset[chg][j] + tmp;

			RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
				"Realtek regulatory, 20MHz, writeval(%c) = 0x%x\n",
				 ((rf == 0) ? 'A' : 'B'), writeval);
			break;
		case 2:
			writeval = ((index < 2) ? base0[rf] : base1[rf]);

			RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
				"Better regulatory, writeval(%c) = 0x%x\n",
				 ((rf == 0) ? 'A' : 'B'), writeval);
			break;
		case 3:
			chg = 0;

			if (rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_20_40) {
				RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
					"customer's limit, 40MHz rf(%c) = 0x%x\n",
					 ((rf == 0) ? 'A' : 'B'),
					 rtlefuse->pwrgroup_ht40[rf][ch]);
			} else {
				RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
					"customer's limit, 20MHz rf(%c) = 0x%x\n",
					 ((rf == 0) ? 'A' : 'B'),
					 rtlefuse->pwrgroup_ht20[rf][ch]);
			}

			if (index < 2)
				pwr_diff = rtlefuse->txpwr_legacyhtdiff[rf][ch];
			else if (rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_20)
				pwr_diff = rtlefuse->txpwr_ht20diff[rf][ch];

			if (rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_20_40)
				cust_pwr_dif = rtlefuse->pwrgroup_ht40[rf][ch];
			else
				cust_pwr_dif = rtlefuse->pwrgroup_ht20[rf][ch];

			if (pwr_diff > cust_pwr_dif)
				pwr_diff = 0;
			else
				pwr_diff = cust_pwr_dif - pwr_diff;

			for (i = 0; i < 4; i++) {
				pwr_lim[i] = (u8)((rtlphy->mcs_offset[chg][j] &
					     (0x7f << (i * 8))) >> (i * 8));

				if (pwr_lim[i] > pwr_diff)
					pwr_lim[i] = pwr_diff;
			}

			cust_lim = (pwr_lim[3] << 24) | (pwr_lim[2] << 16) |
				   (pwr_lim[1] << 8) | (pwr_lim[0]);

			RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
				"Customer's limit rf(%c) = 0x%x\n",
				((rf == 0) ? 'A' : 'B'), cust_lim);

			writeval = cust_lim + tmp;

			RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
				"Customer, writeval rf(%c) = 0x%x\n",
				((rf == 0) ? 'A' : 'B'), writeval);
			break;
		default:
			chg = 0;
			writeval = rtlphy->mcs_offset[chg][j] + tmp;

			RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
				"RTK better performance, writeval "
				"rf(%c) = 0x%x\n",
				((rf == 0) ? 'A' : 'B'), writeval);
			break;
		}

		if (rtlpriv->dm.dynamic_txhighpower_lvl == TXHIGHPWRLEVEL_BT1)
			writeval = writeval - 0x06060606;
		else if (rtlpriv->dm.dynamic_txhighpower_lvl ==
			 TXHIGHPWRLEVEL_BT2)
			writeval -= 0x0c0c0c0c;
		*(outval + rf) = writeval;
	}
}

static void write_ofdm_pwr(struct ieee80211_hw *hw, u8 index, u32 *pvalue)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u16 regoffset_a[6] = {
		RTXAGC_A_RATE18_06, RTXAGC_A_RATE54_24,
		RTXAGC_A_MCS03_MCS00, RTXAGC_A_MCS07_MCS04,
		RTXAGC_A_MCS11_MCS08, RTXAGC_A_MCS15_MCS12
	};
	u16 regoffset_b[6] = {
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
		rtl_set_bbreg(hw, regoffset, MASKDWORD, writeval);

		RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
			"Set 0x%x = %08x\n", regoffset, writeval);
	}
}

void rtl88e_phy_rf6052_set_ofdm_txpower(struct ieee80211_hw *hw,
					u8 *pwrlvlofdm,
					u8 *pwrlvlbw20,
					u8 *pwrlvlbw40, u8 chan)
{
	u32 writeval[2], base0[2], base1[2];
	u8 index;
	u8 direction;
	u32 pwrtrac_value;

	rtl88e_phy_get_power_base(hw, pwrlvlofdm, pwrlvlbw20,
				  pwrlvlbw40, chan, &base0[0],
				  &base1[0]);

	rtl88e_dm_txpower_track_adjust(hw, 1, &direction, &pwrtrac_value);

	for (index = 0; index < 6; index++) {
		get_txpwr_by_reg(hw, chan, index, &base0[0], &base1[0],
				 &writeval[0]);
		if (direction == 1) {
			writeval[0] += pwrtrac_value;
			writeval[1] += pwrtrac_value;
		} else if (direction == 2) {
			writeval[0] -= pwrtrac_value;
			writeval[1] -= pwrtrac_value;
		}
		write_ofdm_pwr(hw, index, &writeval[0]);
	}
}

static bool rf6052_conf_para(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u32 u4val = 0;
	u8 rfpath;
	bool rtstatus = true;
	struct bb_reg_def *pphyreg;

	for (rfpath = 0; rfpath < rtlphy->num_total_rfpath; rfpath++) {
		pphyreg = &rtlphy->phyreg_def[rfpath];

		switch (rfpath) {
		case RF90_PATH_A:
		case RF90_PATH_C:
			u4val = rtl_get_bbreg(hw, pphyreg->rfintfs,
						    BRFSI_RFENV);
			break;
		case RF90_PATH_B:
		case RF90_PATH_D:
			u4val = rtl_get_bbreg(hw, pphyreg->rfintfs,
						    BRFSI_RFENV << 16);
			break;
		}

		rtl_set_bbreg(hw, pphyreg->rfintfe, BRFSI_RFENV << 16, 0x1);
		udelay(1);

		rtl_set_bbreg(hw, pphyreg->rfintfo, BRFSI_RFENV, 0x1);
		udelay(1);

		rtl_set_bbreg(hw, pphyreg->rfhssi_para2,
			      B3WIREADDREAALENGTH, 0x0);
		udelay(1);

		rtl_set_bbreg(hw, pphyreg->rfhssi_para2, B3WIREDATALENGTH, 0x0);
		udelay(1);

		switch (rfpath) {
		case RF90_PATH_A:
			rtstatus = rtl88e_phy_config_rf_with_headerfile(hw,
					(enum radio_path)rfpath);
			break;
		case RF90_PATH_B:
			rtstatus = rtl88e_phy_config_rf_with_headerfile(hw,
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
			rtl_set_bbreg(hw, pphyreg->rfintfs, BRFSI_RFENV, u4val);
			break;
		case RF90_PATH_B:
		case RF90_PATH_D:
			rtl_set_bbreg(hw, pphyreg->rfintfs, BRFSI_RFENV << 16,
				      u4val);
			break;
		}

		if (rtstatus != true) {
			RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
				 "Radio[%d] Fail!!", rfpath);
			return false;
		}
	}

	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE, "\n");
	return rtstatus;
}

bool rtl88e_phy_rf6052_config(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);

	if (rtlphy->rf_type == RF_1T1R)
		rtlphy->num_total_rfpath = 1;
	else
		rtlphy->num_total_rfpath = 2;

	return rf6052_conf_para(hw);
}
