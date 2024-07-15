// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#include "../wifi.h"
#include "../base.h"
#include "../cam.h"
#include "../efuse.h"
#include "../pci.h"
#include "../regd.h"
#include "def.h"
#include "reg.h"
#include "dm_common.h"
#include "fw_common.h"
#include "hw_common.h"
#include "phy_common.h"

void rtl92de_stop_tx_beacon(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 tmp1byte;

	tmp1byte = rtl_read_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2);
	rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2, tmp1byte & (~BIT(6)));
	rtl_write_byte(rtlpriv, REG_BCN_MAX_ERR, 0xff);
	rtl_write_byte(rtlpriv, REG_TBTT_PROHIBIT + 1, 0x64);
	tmp1byte = rtl_read_byte(rtlpriv, REG_TBTT_PROHIBIT + 2);
	tmp1byte &= ~(BIT(0));
	rtl_write_byte(rtlpriv, REG_TBTT_PROHIBIT + 2, tmp1byte);
}
EXPORT_SYMBOL_GPL(rtl92de_stop_tx_beacon);

void rtl92de_resume_tx_beacon(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 tmp1byte;

	tmp1byte = rtl_read_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2);
	rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2, tmp1byte | BIT(6));
	rtl_write_byte(rtlpriv, REG_BCN_MAX_ERR, 0x0a);
	rtl_write_byte(rtlpriv, REG_TBTT_PROHIBIT + 1, 0xff);
	tmp1byte = rtl_read_byte(rtlpriv, REG_TBTT_PROHIBIT + 2);
	tmp1byte |= BIT(0);
	rtl_write_byte(rtlpriv, REG_TBTT_PROHIBIT + 2, tmp1byte);
}
EXPORT_SYMBOL_GPL(rtl92de_resume_tx_beacon);

void rtl92d_get_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));

	switch (variable) {
	case HW_VAR_RF_STATE:
		*((enum rf_pwrstate *)(val)) = ppsc->rfpwr_state;
		break;
	case HW_VAR_FWLPS_RF_ON:{
		enum rf_pwrstate rfstate;
		u32 val_rcr;

		rtlpriv->cfg->ops->get_hw_reg(hw, HW_VAR_RF_STATE,
					      (u8 *)(&rfstate));
		if (rfstate == ERFOFF) {
			*((bool *)(val)) = true;
		} else {
			val_rcr = rtl_read_dword(rtlpriv, REG_RCR);
			val_rcr &= 0x00070000;
			if (val_rcr)
				*((bool *)(val)) = false;
			else
				*((bool *)(val)) = true;
		}
		break;
	}
	case HW_VAR_FW_PSMODE_STATUS:
		*((bool *)(val)) = ppsc->fw_current_inpsmode;
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
	case HW_VAR_INT_MIGRATION:
		*((bool *)(val)) = rtlpriv->dm.interrupt_migration;
		break;
	case HW_VAR_INT_AC:
		*((bool *)(val)) = rtlpriv->dm.disable_tx_int;
		break;
	case HAL_DEF_WOWLAN:
		break;
	default:
		pr_err("switch case %#x not processed\n", variable);
		break;
	}
}
EXPORT_SYMBOL_GPL(rtl92d_get_hw_reg);

void rtl92d_set_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	u8 idx;

	switch (variable) {
	case HW_VAR_ETHER_ADDR:
		for (idx = 0; idx < ETH_ALEN; idx++) {
			rtl_write_byte(rtlpriv, (REG_MACID + idx),
				       val[idx]);
		}
		break;
	case HW_VAR_BASIC_RATE: {
		u16 rate_cfg = ((u16 *)val)[0];
		u8 rate_index = 0;

		rate_cfg = rate_cfg & 0x15f;
		if (mac->vendor == PEER_CISCO &&
		    ((rate_cfg & 0x150) == 0))
			rate_cfg |= 0x01;
		rtl_write_byte(rtlpriv, REG_RRSR, rate_cfg & 0xff);
		rtl_write_byte(rtlpriv, REG_RRSR + 1,
			       (rate_cfg >> 8) & 0xff);
		while (rate_cfg > 0x1) {
			rate_cfg = (rate_cfg >> 1);
			rate_index++;
		}
		if (rtlhal->fw_version > 0xe)
			rtl_write_byte(rtlpriv, REG_INIRTS_RATE_SEL,
				       rate_index);
		break;
	}
	case HW_VAR_BSSID:
		for (idx = 0; idx < ETH_ALEN; idx++) {
			rtl_write_byte(rtlpriv, (REG_BSSID + idx),
				       val[idx]);
		}
		break;
	case HW_VAR_SIFS:
		rtl_write_byte(rtlpriv, REG_SIFS_CTX + 1, val[0]);
		rtl_write_byte(rtlpriv, REG_SIFS_TRX + 1, val[1]);
		rtl_write_byte(rtlpriv, REG_SPEC_SIFS + 1, val[0]);
		rtl_write_byte(rtlpriv, REG_MAC_SPEC_SIFS + 1, val[0]);
		if (!mac->ht_enable)
			rtl_write_word(rtlpriv, REG_RESP_SIFS_OFDM,
				       0x0e0e);
		else
			rtl_write_word(rtlpriv, REG_RESP_SIFS_OFDM,
				       *((u16 *)val));
		break;
	case HW_VAR_SLOT_TIME: {
		u8 e_aci;

		rtl_dbg(rtlpriv, COMP_MLME, DBG_LOUD,
			"HW_VAR_SLOT_TIME %x\n", val[0]);
		rtl_write_byte(rtlpriv, REG_SLOT, val[0]);
		for (e_aci = 0; e_aci < AC_MAX; e_aci++)
			rtlpriv->cfg->ops->set_hw_reg(hw,
						      HW_VAR_AC_PARAM,
						      (&e_aci));
		break;
	}
	case HW_VAR_ACK_PREAMBLE: {
		u8 reg_tmp;
		u8 short_preamble = (bool)(*val);

		reg_tmp = (mac->cur_40_prime_sc) << 5;
		if (short_preamble)
			reg_tmp |= 0x80;
		rtl_write_byte(rtlpriv, REG_RRSR + 2, reg_tmp);
		break;
	}
	case HW_VAR_AMPDU_MIN_SPACE: {
		u8 min_spacing_to_set;

		min_spacing_to_set = *val;
		if (min_spacing_to_set <= 7) {
			mac->min_space_cfg = ((mac->min_space_cfg & 0xf8) |
					      min_spacing_to_set);
			*val = min_spacing_to_set;
			rtl_dbg(rtlpriv, COMP_MLME, DBG_LOUD,
				"Set HW_VAR_AMPDU_MIN_SPACE: %#x\n",
				mac->min_space_cfg);
			rtl_write_byte(rtlpriv, REG_AMPDU_MIN_SPACE,
				       mac->min_space_cfg);
		}
		break;
	}
	case HW_VAR_SHORTGI_DENSITY: {
		u8 density_to_set;

		density_to_set = *val;
		mac->min_space_cfg = rtlpriv->rtlhal.minspace_cfg;
		mac->min_space_cfg |= (density_to_set << 3);
		rtl_dbg(rtlpriv, COMP_MLME, DBG_LOUD,
			"Set HW_VAR_SHORTGI_DENSITY: %#x\n",
			mac->min_space_cfg);
		rtl_write_byte(rtlpriv, REG_AMPDU_MIN_SPACE,
			       mac->min_space_cfg);
		break;
	}
	case HW_VAR_AMPDU_FACTOR: {
		u8 factor_toset;
		u32 regtoset;
		u8 *ptmp_byte = NULL;
		u8 index;

		if (rtlhal->macphymode == DUALMAC_DUALPHY)
			regtoset = 0xb9726641;
		else if (rtlhal->macphymode == DUALMAC_SINGLEPHY)
			regtoset = 0x66626641;
		else
			regtoset = 0xb972a841;
		factor_toset = *val;
		if (factor_toset <= 3) {
			factor_toset = (1 << (factor_toset + 2));
			if (factor_toset > 0xf)
				factor_toset = 0xf;
			for (index = 0; index < 4; index++) {
				ptmp_byte = (u8 *)(&regtoset) + index;
				if ((*ptmp_byte & 0xf0) >
				    (factor_toset << 4))
					*ptmp_byte = (*ptmp_byte & 0x0f)
						 | (factor_toset << 4);
				if ((*ptmp_byte & 0x0f) > factor_toset)
					*ptmp_byte = (*ptmp_byte & 0xf0)
						     | (factor_toset);
			}
			rtl_write_dword(rtlpriv, REG_AGGLEN_LMT, regtoset);
			rtl_dbg(rtlpriv, COMP_MLME, DBG_LOUD,
				"Set HW_VAR_AMPDU_FACTOR: %#x\n",
				factor_toset);
		}
		break;
	}
	case HW_VAR_RETRY_LIMIT: {
		u8 retry_limit = val[0];

		rtl_write_word(rtlpriv, REG_RL,
			       retry_limit << RETRY_LIMIT_SHORT_SHIFT |
			       retry_limit << RETRY_LIMIT_LONG_SHIFT);
		break;
	}
	case HW_VAR_DUAL_TSF_RST:
		rtl_write_byte(rtlpriv, REG_DUAL_TSF_RST, (BIT(0) | BIT(1)));
		break;
	case HW_VAR_EFUSE_BYTES:
		rtlefuse->efuse_usedbytes = *((u16 *)val);
		break;
	case HW_VAR_EFUSE_USAGE:
		rtlefuse->efuse_usedpercentage = *val;
		break;
	case HW_VAR_IO_CMD:
		rtl92d_phy_set_io_cmd(hw, (*(enum io_type *)val));
		break;
	case HW_VAR_WPA_CONFIG:
		rtl_write_byte(rtlpriv, REG_SECCFG, *val);
		break;
	case HW_VAR_SET_RPWM:
		rtl92d_fill_h2c_cmd(hw, H2C_PWRM, 1, (val));
		break;
	case HW_VAR_H2C_FW_PWRMODE:
		break;
	case HW_VAR_FW_PSMODE_STATUS:
		ppsc->fw_current_inpsmode = *((bool *)val);
		break;
	case HW_VAR_AID: {
		u16 u2btmp;

		u2btmp = rtl_read_word(rtlpriv, REG_BCN_PSR_RPT);
		u2btmp &= 0xC000;
		rtl_write_word(rtlpriv, REG_BCN_PSR_RPT, (u2btmp |
			       mac->assoc_id));
		break;
	}
	default:
		pr_err("switch case %#x not processed\n", variable);
		break;
	}
}
EXPORT_SYMBOL_GPL(rtl92d_set_hw_reg);

bool rtl92de_llt_write(struct ieee80211_hw *hw, u32 address, u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	bool status = true;
	long count = 0;
	u32 value = _LLT_INIT_ADDR(address) |
	    _LLT_INIT_DATA(data) | _LLT_OP(_LLT_WRITE_ACCESS);

	rtl_write_dword(rtlpriv, REG_LLT_INIT, value);
	do {
		value = rtl_read_dword(rtlpriv, REG_LLT_INIT);
		if (_LLT_OP_VALUE(value) == _LLT_NO_ACTIVE)
			break;
		if (count > POLLING_LLT_THRESHOLD) {
			pr_err("Failed to polling write LLT done at address %d!\n",
			       address);
			status = false;
			break;
		}
	} while (++count);
	return status;
}
EXPORT_SYMBOL_GPL(rtl92de_llt_write);

void rtl92de_enable_hw_security_config(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 sec_reg_value;

	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
		"PairwiseEncAlgorithm = %d GroupEncAlgorithm = %d\n",
		rtlpriv->sec.pairwise_enc_algorithm,
		rtlpriv->sec.group_enc_algorithm);
	if (rtlpriv->cfg->mod_params->sw_crypto || rtlpriv->sec.use_sw_sec) {
		rtl_dbg(rtlpriv, COMP_SEC, DBG_DMESG,
			"not open hw encryption\n");
		return;
	}
	sec_reg_value = SCR_TXENCENABLE | SCR_RXENCENABLE;
	if (rtlpriv->sec.use_defaultkey) {
		sec_reg_value |= SCR_TXUSEDK;
		sec_reg_value |= SCR_RXUSEDK;
	}
	sec_reg_value |= (SCR_RXBCUSEDK | SCR_TXBCUSEDK);
	rtl_write_byte(rtlpriv, REG_CR + 1, 0x02);
	rtl_dbg(rtlpriv, COMP_SEC, DBG_LOUD,
		"The SECR-value %x\n", sec_reg_value);
	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_WPA_CONFIG, &sec_reg_value);
}
EXPORT_SYMBOL_GPL(rtl92de_enable_hw_security_config);

/* don't set REG_EDCA_BE_PARAM here because
 * mac80211 will send pkt when scan
 */
void rtl92de_set_qos(struct ieee80211_hw *hw, int aci)
{
	rtl92d_dm_init_edca_turbo(hw);
}
EXPORT_SYMBOL_GPL(rtl92de_set_qos);

static enum version_8192d _rtl92d_read_chip_version(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	enum version_8192d version = VERSION_NORMAL_CHIP_92D_SINGLEPHY;
	u32 value32;

	value32 = rtl_read_dword(rtlpriv, REG_SYS_CFG);
	if (!(value32 & 0x000f0000)) {
		version = VERSION_TEST_CHIP_92D_SINGLEPHY;
		rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD, "TEST CHIP!!!\n");
	} else {
		version = VERSION_NORMAL_CHIP_92D_SINGLEPHY;
		rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD, "Normal CHIP!!!\n");
	}
	return version;
}

static void _rtl92de_readpowervalue_fromprom(struct txpower_info *pwrinfo,
					     u8 *efuse, bool autoloadfail)
{
	u32 rfpath, eeaddr, group, offset, offset1, offset2;
	u8 i, val8;

	memset(pwrinfo, 0, sizeof(struct txpower_info));
	if (autoloadfail) {
		for (group = 0; group < CHANNEL_GROUP_MAX; group++) {
			for (rfpath = 0; rfpath < RF6052_MAX_PATH; rfpath++) {
				if (group < CHANNEL_GROUP_MAX_2G) {
					pwrinfo->cck_index[rfpath][group] =
					    EEPROM_DEFAULT_TXPOWERLEVEL_2G;
					pwrinfo->ht40_1sindex[rfpath][group] =
					    EEPROM_DEFAULT_TXPOWERLEVEL_2G;
				} else {
					pwrinfo->ht40_1sindex[rfpath][group] =
					    EEPROM_DEFAULT_TXPOWERLEVEL_5G;
				}
				pwrinfo->ht40_2sindexdiff[rfpath][group] =
				    EEPROM_DEFAULT_HT40_2SDIFF;
				pwrinfo->ht20indexdiff[rfpath][group] =
				    EEPROM_DEFAULT_HT20_DIFF;
				pwrinfo->ofdmindexdiff[rfpath][group] =
				    EEPROM_DEFAULT_LEGACYHTTXPOWERDIFF;
				pwrinfo->ht40maxoffset[rfpath][group] =
				    EEPROM_DEFAULT_HT40_PWRMAXOFFSET;
				pwrinfo->ht20maxoffset[rfpath][group] =
				    EEPROM_DEFAULT_HT20_PWRMAXOFFSET;
			}
		}
		for (i = 0; i < 3; i++) {
			pwrinfo->tssi_a[i] = EEPROM_DEFAULT_TSSI;
			pwrinfo->tssi_b[i] = EEPROM_DEFAULT_TSSI;
		}
		return;
	}

	/* Maybe autoload OK,buf the tx power index value is not filled.
	 * If we find it, we set it to default value.
	 */
	for (rfpath = 0; rfpath < RF6052_MAX_PATH; rfpath++) {
		for (group = 0; group < CHANNEL_GROUP_MAX_2G; group++) {
			eeaddr = EEPROM_CCK_TX_PWR_INX_2G + (rfpath * 3) + group;

			pwrinfo->cck_index[rfpath][group] =
				efuse[eeaddr] == 0xFF ?
				(eeaddr > 0x7B ?
				 EEPROM_DEFAULT_TXPOWERLEVEL_5G :
				 EEPROM_DEFAULT_TXPOWERLEVEL_2G) :
				efuse[eeaddr];
		}
	}
	for (rfpath = 0; rfpath < RF6052_MAX_PATH; rfpath++) {
		for (group = 0; group < CHANNEL_GROUP_MAX; group++) {
			offset1 = group / 3;
			offset2 = group % 3;
			eeaddr = EEPROM_HT40_1S_TX_PWR_INX_2G + (rfpath * 3);
			eeaddr += offset2 + offset1 * 21;

			pwrinfo->ht40_1sindex[rfpath][group] =
				efuse[eeaddr] == 0xFF ?
				(eeaddr > 0x7B ?
				 EEPROM_DEFAULT_TXPOWERLEVEL_5G :
				 EEPROM_DEFAULT_TXPOWERLEVEL_2G) :
				efuse[eeaddr];
		}
	}

	/* These just for 92D efuse offset. */
	for (group = 0; group < CHANNEL_GROUP_MAX; group++) {
		for (rfpath = 0; rfpath < RF6052_MAX_PATH; rfpath++) {
			offset1 = group / 3;
			offset2 = group % 3;
			offset = offset2 + offset1 * 21;

			val8 = efuse[EEPROM_HT40_2S_TX_PWR_INX_DIFF_2G + offset];
			if (val8 != 0xFF)
				pwrinfo->ht40_2sindexdiff[rfpath][group] =
				    (val8 >> (rfpath * 4)) & 0xF;
			else
				pwrinfo->ht40_2sindexdiff[rfpath][group] =
				    EEPROM_DEFAULT_HT40_2SDIFF;

			val8 = efuse[EEPROM_HT20_TX_PWR_INX_DIFF_2G + offset];
			if (val8 != 0xFF)
				pwrinfo->ht20indexdiff[rfpath][group] =
				    (val8 >> (rfpath * 4)) & 0xF;
			else
				pwrinfo->ht20indexdiff[rfpath][group] =
				    EEPROM_DEFAULT_HT20_DIFF;

			val8 = efuse[EEPROM_OFDM_TX_PWR_INX_DIFF_2G + offset];
			if (val8 != 0xFF)
				pwrinfo->ofdmindexdiff[rfpath][group] =
				    (val8 >> (rfpath * 4)) & 0xF;
			else
				pwrinfo->ofdmindexdiff[rfpath][group] =
				    EEPROM_DEFAULT_LEGACYHTTXPOWERDIFF;

			val8 = efuse[EEPROM_HT40_MAX_PWR_OFFSET_2G + offset];
			if (val8 != 0xFF)
				pwrinfo->ht40maxoffset[rfpath][group] =
				    (val8 >> (rfpath * 4)) & 0xF;
			else
				pwrinfo->ht40maxoffset[rfpath][group] =
				    EEPROM_DEFAULT_HT40_PWRMAXOFFSET;

			val8 = efuse[EEPROM_HT20_MAX_PWR_OFFSET_2G + offset];
			if (val8 != 0xFF)
				pwrinfo->ht20maxoffset[rfpath][group] =
				    (val8 >> (rfpath * 4)) & 0xF;
			else
				pwrinfo->ht20maxoffset[rfpath][group] =
				    EEPROM_DEFAULT_HT20_PWRMAXOFFSET;
		}
	}

	if (efuse[EEPROM_TSSI_A_5G] != 0xFF) {
		/* 5GL */
		pwrinfo->tssi_a[0] = efuse[EEPROM_TSSI_A_5G] & 0x3F;
		pwrinfo->tssi_b[0] = efuse[EEPROM_TSSI_B_5G] & 0x3F;
		/* 5GM */
		pwrinfo->tssi_a[1] = efuse[EEPROM_TSSI_AB_5G] & 0x3F;
		pwrinfo->tssi_b[1] = (efuse[EEPROM_TSSI_AB_5G] & 0xC0) >> 6 |
				     (efuse[EEPROM_TSSI_AB_5G + 1] & 0x0F) << 2;
		/* 5GH */
		pwrinfo->tssi_a[2] = (efuse[EEPROM_TSSI_AB_5G + 1] & 0xF0) >> 4 |
				     (efuse[EEPROM_TSSI_AB_5G + 2] & 0x03) << 4;
		pwrinfo->tssi_b[2] = (efuse[EEPROM_TSSI_AB_5G + 2] & 0xFC) >> 2;
	} else {
		for (i = 0; i < 3; i++) {
			pwrinfo->tssi_a[i] = EEPROM_DEFAULT_TSSI;
			pwrinfo->tssi_b[i] = EEPROM_DEFAULT_TSSI;
		}
	}
}

static void _rtl92de_read_txpower_info(struct ieee80211_hw *hw,
				       bool autoload_fail, u8 *hwinfo)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct txpower_info pwrinfo;
	u8 tempval[2], i, pwr, diff;
	u32 ch, rfpath, group;

	_rtl92de_readpowervalue_fromprom(&pwrinfo, hwinfo, autoload_fail);
	if (!autoload_fail) {
		/* bit0~2 */
		rtlefuse->eeprom_regulatory = (hwinfo[EEPROM_RF_OPT1] & 0x7);
		rtlefuse->eeprom_thermalmeter =
			 hwinfo[EEPROM_THERMAL_METER] & 0x1f;
		rtlefuse->crystalcap = hwinfo[EEPROM_XTAL_K];
		tempval[0] = hwinfo[EEPROM_IQK_DELTA] & 0x03;
		tempval[1] = (hwinfo[EEPROM_LCK_DELTA] & 0x0C) >> 2;
		rtlefuse->txpwr_fromeprom = true;
		if (IS_92D_D_CUT(rtlpriv->rtlhal.version) ||
		    IS_92D_E_CUT(rtlpriv->rtlhal.version)) {
			rtlefuse->internal_pa_5g[0] =
				!((hwinfo[EEPROM_TSSI_A_5G] & BIT(6)) >> 6);
			rtlefuse->internal_pa_5g[1] =
				!((hwinfo[EEPROM_TSSI_B_5G] & BIT(6)) >> 6);
			rtl_dbg(rtlpriv, COMP_INIT, DBG_DMESG,
				"Is D cut,Internal PA0 %d Internal PA1 %d\n",
				rtlefuse->internal_pa_5g[0],
				rtlefuse->internal_pa_5g[1]);
		}
		rtlefuse->eeprom_c9 = hwinfo[EEPROM_RF_OPT6];
		rtlefuse->eeprom_cc = hwinfo[EEPROM_RF_OPT7];
	} else {
		rtlefuse->eeprom_regulatory = 0;
		rtlefuse->eeprom_thermalmeter = EEPROM_DEFAULT_THERMALMETER;
		rtlefuse->crystalcap = EEPROM_DEFAULT_CRYSTALCAP;
		tempval[0] = 3;
		tempval[1] = tempval[0];
	}

	/* Use default value to fill parameters if
	 * efuse is not filled on some place.
	 */

	/* ThermalMeter from EEPROM */
	if (rtlefuse->eeprom_thermalmeter < 0x06 ||
	    rtlefuse->eeprom_thermalmeter > 0x1c)
		rtlefuse->eeprom_thermalmeter = 0x12;
	rtlefuse->thermalmeter[0] = rtlefuse->eeprom_thermalmeter;

	/* check XTAL_K */
	if (rtlefuse->crystalcap == 0xFF)
		rtlefuse->crystalcap = 0;
	if (rtlefuse->eeprom_regulatory > 3)
		rtlefuse->eeprom_regulatory = 0;

	for (i = 0; i < 2; i++) {
		switch (tempval[i]) {
		case 0:
			tempval[i] = 5;
			break;
		case 1:
			tempval[i] = 4;
			break;
		case 2:
			tempval[i] = 3;
			break;
		case 3:
		default:
			tempval[i] = 0;
			break;
		}
	}

	rtlefuse->delta_iqk = tempval[0];
	if (tempval[1] > 0)
		rtlefuse->delta_lck = tempval[1] - 1;
	if (rtlefuse->eeprom_c9 == 0xFF)
		rtlefuse->eeprom_c9 = 0x00;
	rtl_dbg(rtlpriv, COMP_INTR, DBG_LOUD,
		"EEPROMRegulatory = 0x%x\n", rtlefuse->eeprom_regulatory);
	rtl_dbg(rtlpriv, COMP_INTR, DBG_LOUD,
		"ThermalMeter = 0x%x\n", rtlefuse->eeprom_thermalmeter);
	rtl_dbg(rtlpriv, COMP_INTR, DBG_LOUD,
		"CrystalCap = 0x%x\n", rtlefuse->crystalcap);
	rtl_dbg(rtlpriv, COMP_INTR, DBG_LOUD,
		"Delta_IQK = 0x%x Delta_LCK = 0x%x\n",
		rtlefuse->delta_iqk, rtlefuse->delta_lck);

	for (rfpath = 0; rfpath < RF6052_MAX_PATH; rfpath++) {
		for (ch = 0; ch < CHANNEL_MAX_NUMBER; ch++) {
			group = rtl92d_get_chnlgroup_fromarray((u8)ch);
			if (ch < CHANNEL_MAX_NUMBER_2G)
				rtlefuse->txpwrlevel_cck[rfpath][ch] =
				    pwrinfo.cck_index[rfpath][group];
			rtlefuse->txpwrlevel_ht40_1s[rfpath][ch] =
				    pwrinfo.ht40_1sindex[rfpath][group];
			rtlefuse->txpwr_ht20diff[rfpath][ch] =
				    pwrinfo.ht20indexdiff[rfpath][group];
			rtlefuse->txpwr_legacyhtdiff[rfpath][ch] =
				    pwrinfo.ofdmindexdiff[rfpath][group];
			rtlefuse->pwrgroup_ht20[rfpath][ch] =
				    pwrinfo.ht20maxoffset[rfpath][group];
			rtlefuse->pwrgroup_ht40[rfpath][ch] =
				    pwrinfo.ht40maxoffset[rfpath][group];
			pwr = pwrinfo.ht40_1sindex[rfpath][group];
			diff = pwrinfo.ht40_2sindexdiff[rfpath][group];
			rtlefuse->txpwrlevel_ht40_2s[rfpath][ch] =
				    (pwr > diff) ? (pwr - diff) : 0;
		}
	}
}

static void _rtl92de_read_macphymode_from_prom(struct ieee80211_hw *hw,
					       u8 *content)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	bool is_single_mac = true;

	if (rtlhal->interface == INTF_PCI)
		is_single_mac = !!(content[EEPROM_MAC_FUNCTION] & BIT(3));
	else if (rtlhal->interface == INTF_USB)
		is_single_mac = !(content[EEPROM_ENDPOINT_SETTING] & BIT(0));

	if (is_single_mac) {
		rtlhal->macphymode = SINGLEMAC_SINGLEPHY;
		rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
			"MacPhyMode SINGLEMAC_SINGLEPHY\n");
	} else {
		rtlhal->macphymode = DUALMAC_DUALPHY;
		rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
			"MacPhyMode DUALMAC_DUALPHY\n");
	}
}

static void _rtl92de_read_macphymode_and_bandtype(struct ieee80211_hw *hw,
						  u8 *content)
{
	_rtl92de_read_macphymode_from_prom(hw, content);
	rtl92d_phy_config_macphymode(hw);
	rtl92d_phy_config_macphymode_info(hw);
}

static void _rtl92de_efuse_update_chip_version(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	enum version_8192d chipver = rtlpriv->rtlhal.version;
	u8 cutvalue[2];
	u16 chipvalue;

	read_efuse_byte(hw, EEPROME_CHIP_VERSION_H, &cutvalue[1]);
	read_efuse_byte(hw, EEPROME_CHIP_VERSION_L, &cutvalue[0]);
	chipvalue = (cutvalue[1] << 8) | cutvalue[0];
	switch (chipvalue) {
	case 0xAA55:
		chipver |= CHIP_92D_C_CUT;
		rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD, "C-CUT!!!\n");
		break;
	case 0x9966:
		chipver |= CHIP_92D_D_CUT;
		rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD, "D-CUT!!!\n");
		break;
	case 0xCC33:
	case 0x33CC:
		chipver |= CHIP_92D_E_CUT;
		rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD, "E-CUT!!!\n");
		break;
	default:
		chipver |= CHIP_92D_D_CUT;
		pr_err("Unknown CUT!\n");
		break;
	}
	rtlpriv->rtlhal.version = chipver;
}

static void _rtl92de_read_adapter_info(struct ieee80211_hw *hw)
{
	static const int params_pci[] = {
		RTL8190_EEPROM_ID, EEPROM_VID, EEPROM_DID,
		EEPROM_SVID, EEPROM_SMID, EEPROM_MAC_ADDR_MAC0_92D,
		EEPROM_CHANNEL_PLAN, EEPROM_VERSION, EEPROM_CUSTOMER_ID,
		COUNTRY_CODE_WORLD_WIDE_13
	};
	static const int params_usb[] = {
		RTL8190_EEPROM_ID, EEPROM_VID_USB, EEPROM_PID_USB,
		EEPROM_VID_USB, EEPROM_PID_USB, EEPROM_MAC_ADDR_MAC0_92DU,
		EEPROM_CHANNEL_PLAN, EEPROM_VERSION, EEPROM_CUSTOMER_ID,
		COUNTRY_CODE_WORLD_WIDE_13
	};
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	const int *params = params_pci;
	u8 *hwinfo;

	if (rtlhal->interface == INTF_USB)
		params = params_usb;

	hwinfo = kzalloc(HWSET_MAX_SIZE, GFP_KERNEL);
	if (!hwinfo)
		return;

	if (rtl_get_hwinfo(hw, rtlpriv, HWSET_MAX_SIZE, hwinfo, params))
		goto exit;

	_rtl92de_efuse_update_chip_version(hw);
	_rtl92de_read_macphymode_and_bandtype(hw, hwinfo);

	/* Read Permanent MAC address for 2nd interface */
	if (rtlhal->interfaceindex != 0)
		ether_addr_copy(rtlefuse->dev_addr,
				&hwinfo[EEPROM_MAC_ADDR_MAC1_92D]);

	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_ETHER_ADDR,
				      rtlefuse->dev_addr);
	rtl_dbg(rtlpriv, COMP_INIT, DBG_DMESG, "%pM\n", rtlefuse->dev_addr);
	_rtl92de_read_txpower_info(hw, rtlefuse->autoload_failflag, hwinfo);

	/* Read Channel Plan */
	switch (rtlhal->bandset) {
	case BAND_ON_2_4G:
		rtlefuse->channel_plan = COUNTRY_CODE_TELEC;
		break;
	case BAND_ON_5G:
		rtlefuse->channel_plan = COUNTRY_CODE_FCC;
		break;
	case BAND_ON_BOTH:
		rtlefuse->channel_plan = COUNTRY_CODE_FCC;
		break;
	default:
		rtlefuse->channel_plan = COUNTRY_CODE_FCC;
		break;
	}
	rtlefuse->txpwr_fromeprom = true;
exit:
	kfree(hwinfo);
}

void rtl92de_read_eeprom_info(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 tmp_u1b;

	rtlhal->version = _rtl92d_read_chip_version(hw);
	tmp_u1b = rtl_read_byte(rtlpriv, REG_9346CR);
	rtlefuse->autoload_status = tmp_u1b;
	if (tmp_u1b & BIT(4)) {
		rtl_dbg(rtlpriv, COMP_INIT, DBG_DMESG, "Boot from EEPROM\n");
		rtlefuse->epromtype = EEPROM_93C46;
	} else {
		rtl_dbg(rtlpriv, COMP_INIT, DBG_DMESG, "Boot from EFUSE\n");
		rtlefuse->epromtype = EEPROM_BOOT_EFUSE;
	}
	if (tmp_u1b & BIT(5)) {
		rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD, "Autoload OK\n");

		rtlefuse->autoload_failflag = false;
		_rtl92de_read_adapter_info(hw);
	} else {
		pr_err("Autoload ERR!!\n");
	}
}
EXPORT_SYMBOL_GPL(rtl92de_read_eeprom_info);

static void rtl92de_update_hal_rate_table(struct ieee80211_hw *hw,
					  struct ieee80211_sta *sta)
{
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	enum wireless_mode wirelessmode;
	u8 mimo_ps = IEEE80211_SMPS_OFF;
	u8 curtxbw_40mhz = mac->bw_40;
	u8 nmode = mac->ht_enable;
	u8 curshortgi_40mhz;
	u8 curshortgi_20mhz;
	u32 tmp_ratr_value;
	u8 ratr_index = 0;
	u16 shortgi_rate;
	u32 ratr_value;

	curshortgi_40mhz = !!(sta->deflink.ht_cap.cap & IEEE80211_HT_CAP_SGI_40);
	curshortgi_20mhz = !!(sta->deflink.ht_cap.cap & IEEE80211_HT_CAP_SGI_20);
	wirelessmode = mac->mode;

	if (rtlhal->current_bandtype == BAND_ON_5G)
		ratr_value = sta->deflink.supp_rates[1] << 4;
	else
		ratr_value = sta->deflink.supp_rates[0];
	ratr_value |= (sta->deflink.ht_cap.mcs.rx_mask[1] << 20 |
		       sta->deflink.ht_cap.mcs.rx_mask[0] << 12);
	switch (wirelessmode) {
	case WIRELESS_MODE_A:
		ratr_value &= 0x00000FF0;
		break;
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
			    get_rf_type(rtlphy) == RF_1T1R) {
				ratr_mask = 0x000ff005;
			} else {
				ratr_mask = 0x0f0ff005;
			}

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
	if (nmode && ((curtxbw_40mhz && curshortgi_40mhz) ||
		      (!curtxbw_40mhz && curshortgi_20mhz))) {
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
	rtl_dbg(rtlpriv, COMP_RATR, DBG_DMESG, "%x\n",
		rtl_read_dword(rtlpriv, REG_ARFR0));
}

static void rtl92de_update_hal_rate_mask(struct ieee80211_hw *hw,
					 struct ieee80211_sta *sta,
					 u8 rssi_level, bool update_bw)
{
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl92d_rate_mask_h2c rate_mask = {};
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_sta_info *sta_entry = NULL;
	enum wireless_mode wirelessmode;
	bool shortgi = false;
	u8 curshortgi_40mhz;
	u8 curshortgi_20mhz;
	u8 curtxbw_40mhz;
	u32 ratr_bitmap;
	u8 ratr_index;
	u8 macid = 0;
	u8 mimo_ps;

	curtxbw_40mhz = sta->deflink.bandwidth >= IEEE80211_STA_RX_BW_40;
	curshortgi_40mhz = !!(sta->deflink.ht_cap.cap & IEEE80211_HT_CAP_SGI_40);
	curshortgi_20mhz = !!(sta->deflink.ht_cap.cap & IEEE80211_HT_CAP_SGI_20);

	sta_entry = (struct rtl_sta_info *)sta->drv_priv;
	mimo_ps = sta_entry->mimo_ps;
	wirelessmode = sta_entry->wireless_mode;

	if (mac->opmode == NL80211_IFTYPE_STATION)
		curtxbw_40mhz = mac->bw_40;
	else if (mac->opmode == NL80211_IFTYPE_AP ||
		 mac->opmode == NL80211_IFTYPE_ADHOC)
		macid = sta->aid + 1;

	if (rtlhal->current_bandtype == BAND_ON_5G)
		ratr_bitmap = sta->deflink.supp_rates[1] << 4;
	else
		ratr_bitmap = sta->deflink.supp_rates[0];
	ratr_bitmap |= (sta->deflink.ht_cap.mcs.rx_mask[1] << 20 |
			sta->deflink.ht_cap.mcs.rx_mask[0] << 12);

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
		ratr_index = RATR_INX_WIRELESS_G;
		ratr_bitmap &= 0x00000ff0;
		break;
	case WIRELESS_MODE_N_24G:
	case WIRELESS_MODE_N_5G:
		if (wirelessmode == WIRELESS_MODE_N_24G)
			ratr_index = RATR_INX_WIRELESS_NGB;
		else
			ratr_index = RATR_INX_WIRELESS_NG;

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

	le32p_replace_bits(&rate_mask.rate_mask_and_raid, ratr_bitmap, RATE_MASK_MASK);
	le32p_replace_bits(&rate_mask.rate_mask_and_raid, ratr_index, RAID_MASK);
	u8p_replace_bits(&rate_mask.macid_and_short_gi, macid, MACID_MASK);
	u8p_replace_bits(&rate_mask.macid_and_short_gi, shortgi, SHORT_GI_MASK);
	u8p_replace_bits(&rate_mask.macid_and_short_gi, 1, BIT(7));

	rtl_dbg(rtlpriv, COMP_RATR, DBG_DMESG,
		"Rate_index:%x, ratr_val:%x, %5phC\n",
		ratr_index, ratr_bitmap, &rate_mask);

	if (rtlhal->interface == INTF_PCI) {
		rtl92d_fill_h2c_cmd(hw, H2C_RA_MASK, sizeof(rate_mask),
				    (u8 *)&rate_mask);
	} else {
		/* rtl92d_fill_h2c_cmd() does USB I/O and will result in a
		 * "scheduled while atomic" if called directly
		 */
		memcpy(rtlpriv->rate_mask, &rate_mask,
		       sizeof(rtlpriv->rate_mask));
		schedule_work(&rtlpriv->works.fill_h2c_cmd);
	}

	if (macid != 0)
		sta_entry->ratr_index = ratr_index;
}

void rtl92de_update_hal_rate_tbl(struct ieee80211_hw *hw,
				 struct ieee80211_sta *sta,
				 u8 rssi_level, bool update_bw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (rtlpriv->dm.useramask)
		rtl92de_update_hal_rate_mask(hw, sta, rssi_level, update_bw);
	else
		rtl92de_update_hal_rate_table(hw, sta);
}
EXPORT_SYMBOL_GPL(rtl92de_update_hal_rate_tbl);

void rtl92de_update_channel_access_setting(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u16 sifs_timer;

	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_SLOT_TIME,
				      &mac->slot_time);
	if (!mac->ht_enable)
		sifs_timer = 0x0a0a;
	else
		sifs_timer = 0x1010;
	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_SIFS, (u8 *)&sifs_timer);
}
EXPORT_SYMBOL_GPL(rtl92de_update_channel_access_setting);

bool rtl92de_gpio_radio_on_off_checking(struct ieee80211_hw *hw, u8 *valid)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	enum rf_pwrstate e_rfpowerstate_toset;
	u8 u1tmp;
	bool actuallyset = false;
	unsigned long flag;

	if (rtlpriv->rtlhal.interface == INTF_PCI &&
	    rtlpci->being_init_adapter)
		return false;
	if (ppsc->swrf_processing)
		return false;
	spin_lock_irqsave(&rtlpriv->locks.rf_ps_lock, flag);
	if (ppsc->rfchange_inprogress) {
		spin_unlock_irqrestore(&rtlpriv->locks.rf_ps_lock, flag);
		return false;
	}

	ppsc->rfchange_inprogress = true;
	spin_unlock_irqrestore(&rtlpriv->locks.rf_ps_lock, flag);

	rtl_write_byte(rtlpriv, REG_MAC_PINMUX_CFG,
		       rtl_read_byte(rtlpriv, REG_MAC_PINMUX_CFG) & ~(BIT(3)));
	u1tmp = rtl_read_byte(rtlpriv, REG_GPIO_IO_SEL);
	e_rfpowerstate_toset = (u1tmp & BIT(3)) ? ERFON : ERFOFF;
	if (ppsc->hwradiooff && e_rfpowerstate_toset == ERFON) {
		rtl_dbg(rtlpriv, COMP_RF, DBG_DMESG,
			"GPIOChangeRF  - HW Radio ON, RF ON\n");
		e_rfpowerstate_toset = ERFON;
		ppsc->hwradiooff = false;
		actuallyset = true;
	} else if (!ppsc->hwradiooff && e_rfpowerstate_toset == ERFOFF) {
		rtl_dbg(rtlpriv, COMP_RF, DBG_DMESG,
			"GPIOChangeRF  - HW Radio OFF, RF OFF\n");
		e_rfpowerstate_toset = ERFOFF;
		ppsc->hwradiooff = true;
		actuallyset = true;
	}
	if (actuallyset) {
		spin_lock_irqsave(&rtlpriv->locks.rf_ps_lock, flag);
		ppsc->rfchange_inprogress = false;
		spin_unlock_irqrestore(&rtlpriv->locks.rf_ps_lock, flag);
	} else {
		if (ppsc->reg_rfps_level & RT_RF_OFF_LEVL_HALT_NIC)
			RT_SET_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC);
		spin_lock_irqsave(&rtlpriv->locks.rf_ps_lock, flag);
		ppsc->rfchange_inprogress = false;
		spin_unlock_irqrestore(&rtlpriv->locks.rf_ps_lock, flag);
	}
	*valid = 1;
	return !ppsc->hwradiooff;
}
EXPORT_SYMBOL_GPL(rtl92de_gpio_radio_on_off_checking);

void rtl92de_set_key(struct ieee80211_hw *hw, u32 key_index,
		     u8 *p_macaddr, bool is_group, u8 enc_algo,
		     bool is_wepkey, bool clear_all)
{
	static const u8 cam_const_addr[4][6] = {
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x01},
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x02},
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x03}
	};
	static const u8 cam_const_broad[] = {
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	};
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	const u8 *macaddr = p_macaddr;
	bool is_pairwise = false;
	u32 entry_id;

	if (clear_all) {
		u8 idx;
		u8 cam_offset = 0;
		u8 clear_number = 5;

		rtl_dbg(rtlpriv, COMP_SEC, DBG_DMESG, "clear_all\n");
		for (idx = 0; idx < clear_number; idx++) {
			rtl_cam_mark_invalid(hw, cam_offset + idx);
			rtl_cam_empty_entry(hw, cam_offset + idx);

			if (idx < 5) {
				memset(rtlpriv->sec.key_buf[idx], 0,
				       MAX_KEY_LEN);
				rtlpriv->sec.key_len[idx] = 0;
			}
		}

		return;
	}

	switch (enc_algo) {
	case WEP40_ENCRYPTION:
		enc_algo = CAM_WEP40;
		break;
	case WEP104_ENCRYPTION:
		enc_algo = CAM_WEP104;
		break;
	case TKIP_ENCRYPTION:
		enc_algo = CAM_TKIP;
		break;
	case AESCCMP_ENCRYPTION:
		enc_algo = CAM_AES;
		break;
	default:
		pr_err("switch case %#x not processed\n",
		       enc_algo);
		enc_algo = CAM_TKIP;
		break;
	}
	if (is_wepkey || rtlpriv->sec.use_defaultkey) {
		macaddr = cam_const_addr[key_index];
		entry_id = key_index;
	} else {
		if (is_group) {
			macaddr = cam_const_broad;
			entry_id = key_index;
		} else {
			if (mac->opmode == NL80211_IFTYPE_AP) {
				entry_id = rtl_cam_get_free_entry(hw, p_macaddr);
				if (entry_id >=  TOTAL_CAM_ENTRY) {
					pr_err("Can not find free hw security cam entry\n");
					return;
				}
			} else {
				entry_id = CAM_PAIRWISE_KEY_POSITION;
			}
			key_index = PAIRWISE_KEYIDX;
			is_pairwise = true;
		}
	}
	if (rtlpriv->sec.key_len[key_index] == 0) {
		rtl_dbg(rtlpriv, COMP_SEC, DBG_DMESG,
			"delete one entry, entry_id is %d\n",
			entry_id);
		if (mac->opmode == NL80211_IFTYPE_AP)
			rtl_cam_del_entry(hw, p_macaddr);
		rtl_cam_delete_one_entry(hw, p_macaddr, entry_id);
	} else {
		rtl_dbg(rtlpriv, COMP_SEC, DBG_LOUD,
			"The insert KEY length is %d\n",
			rtlpriv->sec.key_len[PAIRWISE_KEYIDX]);
		rtl_dbg(rtlpriv, COMP_SEC, DBG_LOUD,
			"The insert KEY is %x %x\n",
			rtlpriv->sec.key_buf[0][0],
			rtlpriv->sec.key_buf[0][1]);
		rtl_dbg(rtlpriv, COMP_SEC, DBG_DMESG,
			"add one entry\n");
		if (is_pairwise) {
			RT_PRINT_DATA(rtlpriv, COMP_SEC, DBG_LOUD,
				      "Pairwise Key content",
				      rtlpriv->sec.pairwise_key,
				      rtlpriv->sec.key_len[PAIRWISE_KEYIDX]);
			rtl_dbg(rtlpriv, COMP_SEC, DBG_DMESG,
				"set Pairwise key\n");
			rtl_cam_add_one_entry(hw, macaddr, key_index,
					      entry_id, enc_algo,
					      CAM_CONFIG_NO_USEDK,
					      rtlpriv->sec.key_buf[key_index]);
		} else {
			rtl_dbg(rtlpriv, COMP_SEC, DBG_DMESG,
				"set group key\n");
			if (mac->opmode == NL80211_IFTYPE_ADHOC) {
				rtl_cam_add_one_entry(hw,
					rtlefuse->dev_addr,
					PAIRWISE_KEYIDX,
					CAM_PAIRWISE_KEY_POSITION,
					enc_algo, CAM_CONFIG_NO_USEDK,
					rtlpriv->sec.key_buf[entry_id]);
			}
			rtl_cam_add_one_entry(hw, macaddr, key_index,
					      entry_id, enc_algo,
					      CAM_CONFIG_NO_USEDK,
					      rtlpriv->sec.key_buf
					      [entry_id]);
		}
	}
}
EXPORT_SYMBOL_GPL(rtl92de_set_key);
