// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#include "../wifi.h"
#include "def.h"
#include "reg.h"
#include "phy_common.h"
#include "rf_common.h"

void rtl92d_phy_rf6052_set_bandwidth(struct ieee80211_hw *hw, u8 bandwidth)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 rfpath;

	switch (bandwidth) {
	case HT_CHANNEL_WIDTH_20:
		for (rfpath = 0; rfpath < rtlphy->num_total_rfpath; rfpath++) {
			rtlphy->rfreg_chnlval[rfpath] &= 0xfffff3ff;
			rtlphy->rfreg_chnlval[rfpath] |= 0x0400;

			rtl_set_rfreg(hw, rfpath, RF_CHNLBW,
				      BIT(10) | BIT(11), 0x01);

			rtl_dbg(rtlpriv, COMP_RF, DBG_LOUD,
				"20M RF 0x18 = 0x%x\n",
				rtlphy->rfreg_chnlval[rfpath]);
		}

		break;
	case HT_CHANNEL_WIDTH_20_40:
		for (rfpath = 0; rfpath < rtlphy->num_total_rfpath; rfpath++) {
			rtlphy->rfreg_chnlval[rfpath] &= 0xfffff3ff;

			rtl_set_rfreg(hw, rfpath, RF_CHNLBW,
				      BIT(10) | BIT(11), 0x00);

			rtl_dbg(rtlpriv, COMP_RF, DBG_LOUD,
				"40M RF 0x18 = 0x%x\n",
				rtlphy->rfreg_chnlval[rfpath]);
		}
		break;
	default:
		pr_err("unknown bandwidth: %#X\n", bandwidth);
		break;
	}
}
EXPORT_SYMBOL_GPL(rtl92d_phy_rf6052_set_bandwidth);

void rtl92d_phy_rf6052_set_cck_txpower(struct ieee80211_hw *hw,
				       u8 *ppowerlevel)
{
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
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
		ptr = (u8 *)(&tx_agc[idx1]);
		for (idx2 = 0; idx2 < 4; idx2++) {
			if (*ptr > RF6052_MAX_TX_PWR)
				*ptr = RF6052_MAX_TX_PWR;
			ptr++;
		}
	}

	tmpval = tx_agc[RF90_PATH_A] & 0xff;
	rtl_set_bbreg(hw, RTXAGC_A_CCK1_MCS32, MASKBYTE1, tmpval);
	RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
		"CCK PWR 1M (rf-A) = 0x%x (reg 0x%x)\n",
		tmpval, RTXAGC_A_CCK1_MCS32);
	tmpval = tx_agc[RF90_PATH_A] >> 8;
	rtl_set_bbreg(hw, RTXAGC_B_CCK11_A_CCK2_11, 0xffffff00, tmpval);
	RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
		"CCK PWR 2~11M (rf-A) = 0x%x (reg 0x%x)\n",
		tmpval, RTXAGC_B_CCK11_A_CCK2_11);
	tmpval = tx_agc[RF90_PATH_B] >> 24;
	rtl_set_bbreg(hw, RTXAGC_B_CCK11_A_CCK2_11, MASKBYTE0, tmpval);
	RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
		"CCK PWR 11M (rf-B) = 0x%x (reg 0x%x)\n",
		tmpval, RTXAGC_B_CCK11_A_CCK2_11);
	tmpval = tx_agc[RF90_PATH_B] & 0x00ffffff;
	rtl_set_bbreg(hw, RTXAGC_B_CCK1_55_MCS32, 0xffffff00, tmpval);
	RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
		"CCK PWR 1~5.5M (rf-B) = 0x%x (reg 0x%x)\n",
		tmpval, RTXAGC_B_CCK1_55_MCS32);
}
EXPORT_SYMBOL_GPL(rtl92d_phy_rf6052_set_cck_txpower);

static void _rtl92d_phy_get_power_base(struct ieee80211_hw *hw,
				       u8 *ppowerlevel, u8 channel,
				       u32 *ofdmbase, u32 *mcsbase)
{
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
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

static void _rtl92d_get_pwr_diff_limit(struct ieee80211_hw *hw, u8 channel,
				       u8 index, u8 rf, u8 pwr_diff_limit[4])
{
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u32 mcs_offset;
	u8 limit;
	int i;

	mcs_offset = rtlphy->mcs_offset[0][index + (rf ? 8 : 0)];

	for (i = 0; i < 4; i++) {
		pwr_diff_limit[i] = (mcs_offset >> (i * 8)) & 0x7f;

		if (rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_20_40)
			limit = rtlefuse->pwrgroup_ht40[rf][channel - 1];
		else
			limit = rtlefuse->pwrgroup_ht20[rf][channel - 1];

		if (pwr_diff_limit[i] > limit)
			pwr_diff_limit[i] = limit;
	}
}

static void _rtl92d_get_txpower_writeval_by_regulatory(struct ieee80211_hw *hw,
						       u8 channel, u8 index,
						       u32 *powerbase0,
						       u32 *powerbase1,
						       u32 *p_outwriteval)
{
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u32 writeval = 0, customer_limit, rf;
	u8 chnlgroup = 0, pwr_diff_limit[4];

	for (rf = 0; rf < 2; rf++) {
		switch (rtlefuse->eeprom_regulatory) {
		case 0:
			writeval = rtlphy->mcs_offset[0][index + (rf ? 8 : 0)];

			RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
				"RTK better performance\n");
			break;
		case 1:
			if (rtlphy->pwrgroup_cnt == 1)
				chnlgroup = 0;

			if (rtlphy->pwrgroup_cnt < MAX_PG_GROUP)
				break;

			chnlgroup = rtl92d_phy_get_chnlgroup_bypg(channel - 1);
			if (rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_20)
				chnlgroup++;
			else
				chnlgroup += 4;

			writeval = rtlphy->mcs_offset
					[chnlgroup][index + (rf ? 8 : 0)];

			RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
				"Realtek regulatory, 20MHz\n");
			break;
		case 2:
			writeval = 0;

			RTPRINT(rtlpriv, FPHY, PHY_TXPWR, "Better regulatory\n");
			break;
		case 3:
			if (rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_20_40) {
				RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
					"customer's limit, 40MHz rf(%c) = 0x%x\n",
					rf == 0 ? 'A' : 'B',
					rtlefuse->pwrgroup_ht40[rf][channel - 1]);
			} else {
				RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
					"customer's limit, 20MHz rf(%c) = 0x%x\n",
					rf == 0 ? 'A' : 'B',
					rtlefuse->pwrgroup_ht20[rf][channel - 1]);
			}

			_rtl92d_get_pwr_diff_limit(hw, channel, index, rf,
						   pwr_diff_limit);

			customer_limit = (pwr_diff_limit[3] << 24) |
					 (pwr_diff_limit[2] << 16) |
					 (pwr_diff_limit[1] << 8) |
					 (pwr_diff_limit[0]);

			RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
				"Customer's limit rf(%c) = 0x%x\n",
				rf == 0 ? 'A' : 'B', customer_limit);

			writeval = customer_limit;

			RTPRINT(rtlpriv, FPHY, PHY_TXPWR, "Customer\n");
			break;
		default:
			writeval = rtlphy->mcs_offset[0][index + (rf ? 8 : 0)];

			RTPRINT(rtlpriv, FPHY, PHY_TXPWR,
				"RTK better performance\n");
			break;
		}

		if (index < 2)
			writeval += powerbase0[rf];
		else
			writeval += powerbase1[rf];

		RTPRINT(rtlpriv, FPHY, PHY_TXPWR, "writeval rf(%c)= 0x%x\n",
			rf == 0 ? 'A' : 'B', writeval);

		*(p_outwriteval + rf) = writeval;
	}
}

static void _rtl92d_write_ofdm_power_reg(struct ieee80211_hw *hw,
					 u8 index, u32 *pvalue)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	static const u16 regoffset_a[6] = {
		RTXAGC_A_RATE18_06, RTXAGC_A_RATE54_24,
		RTXAGC_A_MCS03_MCS00, RTXAGC_A_MCS07_MCS04,
		RTXAGC_A_MCS11_MCS08, RTXAGC_A_MCS15_MCS12
	};
	static const u16 regoffset_b[6] = {
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
			pwr_val[i] = (u8)((writeval & (0x7f <<
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
				rtl_write_byte(rtlpriv, (u32)(regoffset + i),
					       (u8)writeval);
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
				   &powerbase0[0], &powerbase1[0]);
	for (index = 0; index < 6; index++) {
		_rtl92d_get_txpower_writeval_by_regulatory(hw, channel, index,
							   &powerbase0[0],
							   &powerbase1[0],
							   &writeval[0]);
		_rtl92d_write_ofdm_power_reg(hw, index, &writeval[0]);
	}
}
EXPORT_SYMBOL_GPL(rtl92d_phy_rf6052_set_ofdm_txpower);
