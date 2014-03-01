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
#include "reg.h"
#include "def.h"
#include "phy.h"
#include "rf.h"
#include "dm.h"

static bool _rtl8821ae_phy_rf6052_config_parafile(struct ieee80211_hw *hw);

void rtl8821ae_phy_rf6052_set_bandwidth(struct ieee80211_hw *hw, u8 bandwidth)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	switch (bandwidth) {
	case HT_CHANNEL_WIDTH_20:
		rtl_set_rfreg(hw, RF90_PATH_A, RF_CHNLBW, BIT(11)|BIT(10), 3);
		rtl_set_rfreg(hw, RF90_PATH_B, RF_CHNLBW, BIT(11)|BIT(10), 3);
		break;
	case HT_CHANNEL_WIDTH_20_40:
		rtl_set_rfreg(hw, RF90_PATH_A, RF_CHNLBW, BIT(11)|BIT(10), 1);
		rtl_set_rfreg(hw, RF90_PATH_B, RF_CHNLBW, BIT(11)|BIT(10), 1);
		break;
	case HT_CHANNEL_WIDTH_80:
		rtl_set_rfreg(hw, RF90_PATH_A, RF_CHNLBW, BIT(11)|BIT(10), 0);
		rtl_set_rfreg(hw, RF90_PATH_B, RF_CHNLBW, BIT(11)|BIT(10), 0);
		break;
	default:
		RT_TRACE(COMP_ERR, DBG_EMERG,
			 ("unknown bandwidth: %#X\n", bandwidth));
		break;
	}
}

void rtl8821ae_phy_rf6052_set_cck_txpower(struct ieee80211_hw *hw,
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
	u8 direction;
	u32 pwrtrac_value;

	if (rtlefuse->eeprom_regulatory != 0)
		turbo_scanoff = true;

	if (mac->act_scanning == true) {
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
			tmpval =
			    (rtlphy->mcs_txpwrlevel_origoffset[0][6]) +
			    (rtlphy->mcs_txpwrlevel_origoffset[0][7] <<
			     8);
			tx_agc[RF90_PATH_A] += tmpval;

			tmpval = (rtlphy->mcs_txpwrlevel_origoffset[0][14]) +
			    (rtlphy->mcs_txpwrlevel_origoffset[0][15] <<
			     24);
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
	rtl8821ae_dm_txpower_track_adjust(hw,1,&direction,&pwrtrac_value);
	if (direction ==1){
		tx_agc[0] += pwrtrac_value;
		tx_agc[1] += pwrtrac_value;
	} else if (direction == 2){
		tx_agc[0] -= pwrtrac_value;
		tx_agc[1] -= pwrtrac_value;
	}
	tmpval = tx_agc[RF90_PATH_A] ;
	rtl_set_bbreg(hw, RTXAGC_A_CCK11_CCK1, MASKDWORD, tmpval);

	RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
		("CCK PWR 1~11M (rf-A) = 0x%x (reg 0x%x)\n", tmpval,
		 RTXAGC_A_CCK11_CCK1));

	tmpval = tx_agc[RF90_PATH_B] ;
	rtl_set_bbreg(hw, RTXAGC_B_CCK11_CCK1, MASKDWORD, tmpval);

	RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
		("CCK PWR 11M (rf-B) = 0x%x (reg 0x%x)\n", tmpval,
		 RTXAGC_B_CCK11_CCK1));
}

static void rtl8821ae_phy_get_power_base(struct ieee80211_hw *hw,
				      u8 *ppowerlevel_ofdm, u8 *ppowerlevel_bw20, u8 *ppowerlevel_bw40, u8 channel,
				      u32 *ofdmbase, u32 *mcsbase)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u32 powerBase0, powerBase1;
	u8 i, powerlevel[2];

	for (i = 0; i < 2; i++) {
		powerBase0 = ppowerlevel_ofdm[i];

		powerBase0 = (powerBase0 << 24) | (powerBase0 << 16) |
		    (powerBase0 << 8) | powerBase0;
		*(ofdmbase + i) = powerBase0;
		RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
			(" [OFDM power base index rf(%c) = 0x%x]\n",
			 ((i == 0) ? 'A' : 'B'), *(ofdmbase + i)));
	}

	for (i = 0; i < 2; i++) {
		if (rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_20) {
			powerlevel[i] = ppowerlevel_bw20[i];
		}else{
			powerlevel[i] = ppowerlevel_bw40[i];
		}
		powerBase1 = powerlevel[i];
		powerBase1 = (powerBase1 << 24) |
		    (powerBase1 << 16) | (powerBase1 << 8) | powerBase1;

		*(mcsbase + i) = powerBase1;

		RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
			(" [MCS power base index rf(%c) = 0x%x]\n",
			 ((i == 0) ? 'A' : 'B'), *(mcsbase + i)));
	}
}

static void _rtl8821ae_get_txpower_writeval_by_regulatory(struct ieee80211_hw *hw,
						       u8 channel, u8 index,
						       u32 *powerBase0,
						       u32 *powerBase1,
						       u32 *p_outwriteval)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u8 i, chnlgroup = 0, pwr_diff_limit[4],pwr_diff = 0,customer_pwr_diff;
	u32 writeVal, customer_limit, rf;

	for (rf = 0; rf < 2; rf++) {
		switch (rtlefuse->eeprom_regulatory) {
		case 0:
			chnlgroup = 0;

			writeVal =
			    rtlphy->mcs_txpwrlevel_origoffset[chnlgroup][index +
									 (rf ? 8 : 0)]
			    + ((index < 2) ? powerBase0[rf] : powerBase1[rf]);

			RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
				("RTK better performance, "
				 "writeVal(%c) = 0x%x\n",
				 ((rf == 0) ? 'A' : 'B'), writeVal));
			break;
		case 1:
			if (rtlphy->pwrgroup_cnt == 1)
				chnlgroup = 0;
			else {
				if(channel<3)
					chnlgroup = 0;
				else if (channel <6)
					chnlgroup = 1;
				else if (channel <9)
					chnlgroup = 2;
				else if (channel <12)
					chnlgroup = 3;
				else if (channel < 14)
					chnlgroup = 4;
				else if (channel == 14)
					chnlgroup = 5;
			}

			writeVal =
			    rtlphy->mcs_txpwrlevel_origoffset[chnlgroup]
			    [index + (rf ? 8 : 0)] + ((index < 2) ?
						      powerBase0[rf] :
						      powerBase1[rf]);

			RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
				("Realtek regulatory, 20MHz, "
				 "writeVal(%c) = 0x%x\n",
				 ((rf == 0) ? 'A' : 'B'), writeVal));

			break;
		case 2:
			writeVal =
			    ((index < 2) ? powerBase0[rf] : powerBase1[rf]);

			RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
				("Better regulatory, "
				 "writeVal(%c) = 0x%x\n",
				 ((rf == 0) ? 'A' : 'B'), writeVal));
			break;
		case 3:
			chnlgroup = 0;

			if (rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_20_40) {
				RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
					("customer's limit, 40MHz "
					 "rf(%c) = 0x%x\n",
					 ((rf == 0) ? 'A' : 'B'),
					 rtlefuse->pwrgroup_ht40[rf][channel -
								     1]));
			} else {
				RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
					("customer's limit, 20MHz "
					 "rf(%c) = 0x%x\n",
					 ((rf == 0) ? 'A' : 'B'),
					 rtlefuse->pwrgroup_ht20[rf][channel -
								     1]));
			}

			if (index < 2)
				pwr_diff = rtlefuse->txpwr_legacyhtdiff[rf][channel-1];
			else if (rtlphy->current_chan_bw ==  HT_CHANNEL_WIDTH_20)
				pwr_diff = rtlefuse->txpwr_ht20diff[rf][channel-1];

			if (rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_20_40)
				customer_pwr_diff = rtlefuse->pwrgroup_ht40[rf][channel-1];
			else
				customer_pwr_diff = rtlefuse->pwrgroup_ht20[rf][channel-1];

			if (pwr_diff > customer_pwr_diff)
				pwr_diff = 0;
			else
				pwr_diff = customer_pwr_diff - pwr_diff;

			for (i = 0; i < 4; i++) {
				pwr_diff_limit[i] =
				    (u8) ((rtlphy->mcs_txpwrlevel_origoffset
					   [chnlgroup][index + (rf ? 8 : 0)] & (0x7f <<
									(i * 8))) >> (i * 8));

					if(pwr_diff_limit[i] > pwr_diff)
						pwr_diff_limit[i] = pwr_diff;
			}

			customer_limit = (pwr_diff_limit[3] << 24) |
			    (pwr_diff_limit[2] << 16) |
			    (pwr_diff_limit[1] << 8) | (pwr_diff_limit[0]);

			RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
				("Customer's limit rf(%c) = 0x%x\n",
				 ((rf == 0) ? 'A' : 'B'), customer_limit));

			writeVal = customer_limit +
			    ((index < 2) ? powerBase0[rf] : powerBase1[rf]);

			RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
				("Customer, writeVal rf(%c)= 0x%x\n",
				 ((rf == 0) ? 'A' : 'B'), writeVal));
			break;
		default:
			chnlgroup = 0;
			writeVal =
			    rtlphy->mcs_txpwrlevel_origoffset[chnlgroup]
			    [index + (rf ? 8 : 0)]
			    + ((index < 2) ? powerBase0[rf] : powerBase1[rf]);

			RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
				("RTK better performance, writeVal "
				 "rf(%c) = 0x%x\n",
				 ((rf == 0) ? 'A' : 'B'), writeVal));
			break;
		}

		if (rtlpriv->dm.dynamic_txhighpower_lvl == TXHIGHPWRLEVEL_BT1)
			writeVal = writeVal - 0x06060606;
		else if (rtlpriv->dm.dynamic_txhighpower_lvl ==
			 TXHIGHPWRLEVEL_BT2)
			writeVal = writeVal - 0x0c0c0c0c;
		*(p_outwriteval + rf) = writeVal;
	}
}

static void _rtl8821ae_write_ofdm_power_reg(struct ieee80211_hw *hw,
					 u8 index, u32 *pValue)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u16 regoffset_a[6] = {
		RTXAGC_A_OFDM18_OFDM6, RTXAGC_A_OFDM54_OFDM24,
		RTXAGC_A_MCS03_MCS00, RTXAGC_A_MCS07_MCS04,
		RTXAGC_A_MCS11_MCS08, RTXAGC_A_MCS15_MCS12
	};
	u16 regoffset_b[6] = {
		RTXAGC_B_OFDM18_OFDM6, RTXAGC_B_OFDM54_OFDM24,
		RTXAGC_B_MCS03_MCS00, RTXAGC_B_MCS07_MCS04,
		RTXAGC_B_MCS11_MCS08, RTXAGC_B_MCS15_MCS12
	};
	u8 i, rf, pwr_val[4];
	u32 writeVal;
	u16 regoffset;

	for (rf = 0; rf < 2; rf++) {
		writeVal = pValue[rf];
		for (i = 0; i < 4; i++) {
			pwr_val[i] = (u8) ((writeVal & (0x7f <<
							(i * 8))) >> (i * 8));

			if (pwr_val[i] > RF6052_MAX_TX_PWR)
				pwr_val[i] = RF6052_MAX_TX_PWR;
		}
		writeVal = (pwr_val[3] << 24) | (pwr_val[2] << 16) |
		    (pwr_val[1] << 8) | pwr_val[0];

		if (rf == 0)
			regoffset = regoffset_a[index];
		else
			regoffset = regoffset_b[index];
		rtl_set_bbreg(hw, regoffset, MASKDWORD, writeVal);

		RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
			("Set 0x%x = %08x\n", regoffset, writeVal));
	}
}

void rtl8821ae_phy_rf6052_set_ofdm_txpower(struct ieee80211_hw *hw,
					      u8 *ppowerlevel_ofdm, u8 *ppowerlevel_bw20, u8 *ppowerlevel_bw40, u8 channel)
{
	u32 writeVal[2], powerBase0[2], powerBase1[2];
	u8 index;
	u8 direction;
	u32 pwrtrac_value;

	rtl8821ae_phy_get_power_base(hw, ppowerlevel_ofdm, ppowerlevel_bw20, ppowerlevel_bw40,
				  channel, &powerBase0[0], &powerBase1[0]);

	rtl8821ae_dm_txpower_track_adjust(hw,1,&direction,&pwrtrac_value);

	for (index = 0; index < 6; index++) {
		_rtl8821ae_get_txpower_writeval_by_regulatory(hw,
							   channel, index,
							   &powerBase0[0],
							   &powerBase1[0],
							   &writeVal[0]);
		if (direction ==1){
			writeVal[0] += pwrtrac_value;
			writeVal[1] += pwrtrac_value;
		} else if (direction == 2){
			writeVal[0] -= pwrtrac_value;
			writeVal[1] -= pwrtrac_value;
		}
		_rtl8821ae_write_ofdm_power_reg(hw, index, &writeVal[0]);
	}
}

bool rtl8821ae_phy_rf6052_config(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);

	if (rtlphy->rf_type == RF_1T1R)
		rtlphy->num_total_rfpath = 1;
	else
		rtlphy->num_total_rfpath = 2;

	return _rtl8821ae_phy_rf6052_config_parafile(hw);

}

static bool _rtl8821ae_phy_rf6052_config_parafile(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	//u32 u4_regvalue = 0;
	u8 rfpath;
	bool rtstatus = true;
	//struct bb_reg_def *pphyreg;

	for (rfpath = 0; rfpath < rtlphy->num_total_rfpath; rfpath++) {
		switch (rfpath) {
		case RF90_PATH_A: {
			if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE)
				rtstatus = rtl8812ae_phy_config_rf_with_headerfile(hw,
									(enum radio_path)rfpath);
			else
				rtstatus = rtl8821ae_phy_config_rf_with_headerfile(hw,
									(enum radio_path)rfpath);
			break;
			}
		case RF90_PATH_B: {
			if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE)
				rtstatus = rtl8812ae_phy_config_rf_with_headerfile(hw,
									(enum radio_path)rfpath);
			else
				rtstatus = rtl8821ae_phy_config_rf_with_headerfile(hw,
									(enum radio_path)rfpath);
			break;
			}
		case RF90_PATH_C:
			break;
		case RF90_PATH_D:
			break;
		}

		if (rtstatus != true) {
			RT_TRACE(COMP_INIT, DBG_TRACE,
				 ("Radio[%d] Fail!!", rfpath));
			return false;
		}

	}

	/*put arrays in dm.c*/
	/*_rtl8821ae_config_rf_txpwr_track_headerfile(hw);*/
	RT_TRACE(COMP_INIT, DBG_TRACE, ("\n"));
	return rtstatus;
}
