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
#include "../efuse.h"
#include "../base.h"
#include "../regd.h"
#include "../cam.h"
#include "../ps.h"
#include "../pci.h"
#include "reg.h"
#include "def.h"
#include "phy.h"
#include "dm.h"
#include "fw.h"
#include "led.h"
#include "hw.h"

void rtl92se_get_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	switch (variable) {
	case HW_VAR_RCR: {
			*((u32 *) (val)) = rtlpci->receive_config;
			break;
		}
	case HW_VAR_RF_STATE: {
			*((enum rf_pwrstate *)(val)) = ppsc->rfpwr_state;
			break;
		}
	case HW_VAR_FW_PSMODE_STATUS: {
			*((bool *) (val)) = ppsc->fw_current_inpsmode;
			break;
		}
	case HW_VAR_CORRECT_TSF: {
			u64 tsf;
			u32 *ptsf_low = (u32 *)&tsf;
			u32 *ptsf_high = ((u32 *)&tsf) + 1;

			*ptsf_high = rtl_read_dword(rtlpriv, (TSFR + 4));
			*ptsf_low = rtl_read_dword(rtlpriv, TSFR);

			*((u64 *) (val)) = tsf;

			break;
		}
	case HW_VAR_MRC: {
			*((bool *)(val)) = rtlpriv->dm.current_mrc_switch;
			break;
		}
	default: {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "switch case %#x not processed\n", variable);
			break;
		}
	}
}

void rtl92se_set_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));

	switch (variable) {
	case HW_VAR_ETHER_ADDR:{
			rtl_write_dword(rtlpriv, IDR0, ((u32 *)(val))[0]);
			rtl_write_word(rtlpriv, IDR4, ((u16 *)(val + 4))[0]);
			break;
		}
	case HW_VAR_BASIC_RATE:{
			u16 rate_cfg = ((u16 *) val)[0];
			u8 rate_index = 0;

			if (rtlhal->version == VERSION_8192S_ACUT)
				rate_cfg = rate_cfg & 0x150;
			else
				rate_cfg = rate_cfg & 0x15f;

			rate_cfg |= 0x01;

			rtl_write_byte(rtlpriv, RRSR, rate_cfg & 0xff);
			rtl_write_byte(rtlpriv, RRSR + 1,
				       (rate_cfg >> 8) & 0xff);

			while (rate_cfg > 0x1) {
				rate_cfg = (rate_cfg >> 1);
				rate_index++;
			}
			rtl_write_byte(rtlpriv, INIRTSMCS_SEL, rate_index);

			break;
		}
	case HW_VAR_BSSID:{
			rtl_write_dword(rtlpriv, BSSIDR, ((u32 *)(val))[0]);
			rtl_write_word(rtlpriv, BSSIDR + 4,
				       ((u16 *)(val + 4))[0]);
			break;
		}
	case HW_VAR_SIFS:{
			rtl_write_byte(rtlpriv, SIFS_OFDM, val[0]);
			rtl_write_byte(rtlpriv, SIFS_OFDM + 1, val[1]);
			break;
		}
	case HW_VAR_SLOT_TIME:{
			u8 e_aci;

			RT_TRACE(rtlpriv, COMP_MLME, DBG_LOUD,
				 "HW_VAR_SLOT_TIME %x\n", val[0]);

			rtl_write_byte(rtlpriv, SLOT_TIME, val[0]);

			for (e_aci = 0; e_aci < AC_MAX; e_aci++) {
				rtlpriv->cfg->ops->set_hw_reg(hw,
						HW_VAR_AC_PARAM,
						(&e_aci));
			}
			break;
		}
	case HW_VAR_ACK_PREAMBLE:{
			u8 reg_tmp;
			u8 short_preamble = (bool) (*val);
			reg_tmp = (mac->cur_40_prime_sc) << 5;
			if (short_preamble)
				reg_tmp |= 0x80;

			rtl_write_byte(rtlpriv, RRSR + 2, reg_tmp);
			break;
		}
	case HW_VAR_AMPDU_MIN_SPACE:{
			u8 min_spacing_to_set;
			u8 sec_min_space;

			min_spacing_to_set = *val;
			if (min_spacing_to_set <= 7) {
				if (rtlpriv->sec.pairwise_enc_algorithm ==
				    NO_ENCRYPTION)
					sec_min_space = 0;
				else
					sec_min_space = 1;

				if (min_spacing_to_set < sec_min_space)
					min_spacing_to_set = sec_min_space;
				if (min_spacing_to_set > 5)
					min_spacing_to_set = 5;

				mac->min_space_cfg =
						((mac->min_space_cfg & 0xf8) |
						min_spacing_to_set);

				*val = min_spacing_to_set;

				RT_TRACE(rtlpriv, COMP_MLME, DBG_LOUD,
					 "Set HW_VAR_AMPDU_MIN_SPACE: %#x\n",
					 mac->min_space_cfg);

				rtl_write_byte(rtlpriv, AMPDU_MIN_SPACE,
					       mac->min_space_cfg);
			}
			break;
		}
	case HW_VAR_SHORTGI_DENSITY:{
			u8 density_to_set;

			density_to_set = *val;
			mac->min_space_cfg = rtlpriv->rtlhal.minspace_cfg;
			mac->min_space_cfg |= (density_to_set << 3);

			RT_TRACE(rtlpriv, COMP_MLME, DBG_LOUD,
				 "Set HW_VAR_SHORTGI_DENSITY: %#x\n",
				 mac->min_space_cfg);

			rtl_write_byte(rtlpriv, AMPDU_MIN_SPACE,
				       mac->min_space_cfg);

			break;
		}
	case HW_VAR_AMPDU_FACTOR:{
			u8 factor_toset;
			u8 regtoset;
			u8 factorlevel[18] = {
				2, 4, 4, 7, 7, 13, 13,
				13, 2, 7, 7, 13, 13,
				15, 15, 15, 15, 0};
			u8 index = 0;

			factor_toset = *val;
			if (factor_toset <= 3) {
				factor_toset = (1 << (factor_toset + 2));
				if (factor_toset > 0xf)
					factor_toset = 0xf;

				for (index = 0; index < 17; index++) {
					if (factorlevel[index] > factor_toset)
						factorlevel[index] =
								 factor_toset;
				}

				for (index = 0; index < 8; index++) {
					regtoset = ((factorlevel[index * 2]) |
						    (factorlevel[index *
						    2 + 1] << 4));
					rtl_write_byte(rtlpriv,
						       AGGLEN_LMT_L + index,
						       regtoset);
				}

				regtoset = ((factorlevel[16]) |
					    (factorlevel[17] << 4));
				rtl_write_byte(rtlpriv, AGGLEN_LMT_H, regtoset);

				RT_TRACE(rtlpriv, COMP_MLME, DBG_LOUD,
					 "Set HW_VAR_AMPDU_FACTOR: %#x\n",
					 factor_toset);
			}
			break;
		}
	case HW_VAR_AC_PARAM:{
			u8 e_aci = *val;
			rtl92s_dm_init_edca_turbo(hw);

			if (rtlpci->acm_method != EACMWAY2_SW)
				rtlpriv->cfg->ops->set_hw_reg(hw,
						 HW_VAR_ACM_CTRL,
						 &e_aci);
			break;
		}
	case HW_VAR_ACM_CTRL:{
			u8 e_aci = *val;
			union aci_aifsn *p_aci_aifsn = (union aci_aifsn *)(&(
							mac->ac[0].aifs));
			u8 acm = p_aci_aifsn->f.acm;
			u8 acm_ctrl = rtl_read_byte(rtlpriv, AcmHwCtrl);

			acm_ctrl = acm_ctrl | ((rtlpci->acm_method == 2) ?
				   0x0 : 0x1);

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
					acm_ctrl &= (~AcmHw_VoqEn);
					break;
				default:
					RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
						 "switch case %#x not processed\n",
						 e_aci);
					break;
				}
			}

			RT_TRACE(rtlpriv, COMP_QOS, DBG_TRACE,
				 "HW_VAR_ACM_CTRL Write 0x%X\n", acm_ctrl);
			rtl_write_byte(rtlpriv, AcmHwCtrl, acm_ctrl);
			break;
		}
	case HW_VAR_RCR:{
			rtl_write_dword(rtlpriv, RCR, ((u32 *) (val))[0]);
			rtlpci->receive_config = ((u32 *) (val))[0];
			break;
		}
	case HW_VAR_RETRY_LIMIT:{
			u8 retry_limit = val[0];

			rtl_write_word(rtlpriv, RETRY_LIMIT,
				       retry_limit << RETRY_LIMIT_SHORT_SHIFT |
				       retry_limit << RETRY_LIMIT_LONG_SHIFT);
			break;
		}
	case HW_VAR_DUAL_TSF_RST: {
			break;
		}
	case HW_VAR_EFUSE_BYTES: {
			rtlefuse->efuse_usedbytes = *((u16 *) val);
			break;
		}
	case HW_VAR_EFUSE_USAGE: {
			rtlefuse->efuse_usedpercentage = *val;
			break;
		}
	case HW_VAR_IO_CMD: {
			break;
		}
	case HW_VAR_WPA_CONFIG: {
			rtl_write_byte(rtlpriv, REG_SECR, *val);
			break;
		}
	case HW_VAR_SET_RPWM:{
			break;
		}
	case HW_VAR_H2C_FW_PWRMODE:{
			break;
		}
	case HW_VAR_FW_PSMODE_STATUS: {
			ppsc->fw_current_inpsmode = *((bool *) val);
			break;
		}
	case HW_VAR_H2C_FW_JOINBSSRPT:{
			break;
		}
	case HW_VAR_AID:{
			break;
		}
	case HW_VAR_CORRECT_TSF:{
			break;
		}
	case HW_VAR_MRC: {
			bool bmrc_toset = *((bool *)val);
			u8 u1bdata = 0;

			if (bmrc_toset) {
				rtl_set_bbreg(hw, ROFDM0_TRXPATHENABLE,
					      MASKBYTE0, 0x33);
				u1bdata = (u8)rtl_get_bbreg(hw,
						ROFDM1_TRXPATHENABLE,
						MASKBYTE0);
				rtl_set_bbreg(hw, ROFDM1_TRXPATHENABLE,
					      MASKBYTE0,
					      ((u1bdata & 0xf0) | 0x03));
				u1bdata = (u8)rtl_get_bbreg(hw,
						ROFDM0_TRXPATHENABLE,
						MASKBYTE1);
				rtl_set_bbreg(hw, ROFDM0_TRXPATHENABLE,
					      MASKBYTE1,
					      (u1bdata | 0x04));

				/* Update current settings. */
				rtlpriv->dm.current_mrc_switch = bmrc_toset;
			} else {
				rtl_set_bbreg(hw, ROFDM0_TRXPATHENABLE,
					      MASKBYTE0, 0x13);
				u1bdata = (u8)rtl_get_bbreg(hw,
						 ROFDM1_TRXPATHENABLE,
						 MASKBYTE0);
				rtl_set_bbreg(hw, ROFDM1_TRXPATHENABLE,
					      MASKBYTE0,
					      ((u1bdata & 0xf0) | 0x01));
				u1bdata = (u8)rtl_get_bbreg(hw,
						ROFDM0_TRXPATHENABLE,
						MASKBYTE1);
				rtl_set_bbreg(hw, ROFDM0_TRXPATHENABLE,
					      MASKBYTE1, (u1bdata & 0xfb));

				/* Update current settings. */
				rtlpriv->dm.current_mrc_switch = bmrc_toset;
			}

			break;
		}
	case HW_VAR_FW_LPS_ACTION: {
		bool enter_fwlps = *((bool *)val);
		u8 rpwm_val, fw_pwrmode;
		bool fw_current_inps;

		if (enter_fwlps) {
			rpwm_val = 0x02;	/* RF off */
			fw_current_inps = true;
			rtlpriv->cfg->ops->set_hw_reg(hw,
					HW_VAR_FW_PSMODE_STATUS,
					(u8 *)(&fw_current_inps));
			rtlpriv->cfg->ops->set_hw_reg(hw,
					HW_VAR_H2C_FW_PWRMODE,
					&ppsc->fwctrl_psmode);

			rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_SET_RPWM,
						      &rpwm_val);
		} else {
			rpwm_val = 0x0C;	/* RF on */
			fw_pwrmode = FW_PS_ACTIVE_MODE;
			fw_current_inps = false;
			rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_SET_RPWM,
						      &rpwm_val);
			rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_H2C_FW_PWRMODE,
						      &fw_pwrmode);

			rtlpriv->cfg->ops->set_hw_reg(hw,
					HW_VAR_FW_PSMODE_STATUS,
					(u8 *)(&fw_current_inps));
		}
		break; }
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "switch case %#x not processed\n", variable);
		break;
	}

}

void rtl92se_enable_hw_security_config(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 sec_reg_value = 0x0;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "PairwiseEncAlgorithm = %d GroupEncAlgorithm = %d\n",
		 rtlpriv->sec.pairwise_enc_algorithm,
		 rtlpriv->sec.group_enc_algorithm);

	if (rtlpriv->cfg->mod_params->sw_crypto || rtlpriv->sec.use_sw_sec) {
		RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG,
			 "not open hw encryption\n");
		return;
	}

	sec_reg_value = SCR_TXENCENABLE | SCR_RXENCENABLE;

	if (rtlpriv->sec.use_defaultkey) {
		sec_reg_value |= SCR_TXUSEDK;
		sec_reg_value |= SCR_RXUSEDK;
	}

	RT_TRACE(rtlpriv, COMP_SEC, DBG_LOUD, "The SECR-value %x\n",
		 sec_reg_value);

	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_WPA_CONFIG, &sec_reg_value);

}

static u8 _rtl92se_halset_sysclk(struct ieee80211_hw *hw, u8 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 waitcount = 100;
	bool bresult = false;
	u8 tmpvalue;

	rtl_write_byte(rtlpriv, SYS_CLKR + 1, data);

	/* Wait the MAC synchronized. */
	udelay(400);

	/* Check if it is set ready. */
	tmpvalue = rtl_read_byte(rtlpriv, SYS_CLKR + 1);
	bresult = ((tmpvalue & BIT(7)) == (data & BIT(7)));

	if ((data & (BIT(6) | BIT(7))) == false) {
		waitcount = 100;
		tmpvalue = 0;

		while (1) {
			waitcount--;

			tmpvalue = rtl_read_byte(rtlpriv, SYS_CLKR + 1);
			if ((tmpvalue & BIT(6)))
				break;

			pr_err("wait for BIT(6) return value %x\n", tmpvalue);
			if (waitcount == 0)
				break;

			udelay(10);
		}

		if (waitcount == 0)
			bresult = false;
		else
			bresult = true;
	}

	return bresult;
}

void rtl8192se_gpiobit3_cfg_inputmode(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 u1tmp;

	/* The following config GPIO function */
	rtl_write_byte(rtlpriv, MAC_PINMUX_CFG, (GPIOMUX_EN | GPIOSEL_GPIO));
	u1tmp = rtl_read_byte(rtlpriv, GPIO_IO_SEL);

	/* config GPIO3 to input */
	u1tmp &= HAL_8192S_HW_GPIO_OFF_MASK;
	rtl_write_byte(rtlpriv, GPIO_IO_SEL, u1tmp);

}

static u8 _rtl92se_rf_onoff_detect(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 u1tmp;
	u8 retval = ERFON;

	/* The following config GPIO function */
	rtl_write_byte(rtlpriv, MAC_PINMUX_CFG, (GPIOMUX_EN | GPIOSEL_GPIO));
	u1tmp = rtl_read_byte(rtlpriv, GPIO_IO_SEL);

	/* config GPIO3 to input */
	u1tmp &= HAL_8192S_HW_GPIO_OFF_MASK;
	rtl_write_byte(rtlpriv, GPIO_IO_SEL, u1tmp);

	/* On some of the platform, driver cannot read correct
	 * value without delay between Write_GPIO_SEL and Read_GPIO_IN */
	mdelay(10);

	/* check GPIO3 */
	u1tmp = rtl_read_byte(rtlpriv, GPIO_IN_SE);
	retval = (u1tmp & HAL_8192S_HW_GPIO_OFF_BIT) ? ERFON : ERFOFF;

	return retval;
}

static void _rtl92se_macconfig_before_fwdownload(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));

	u8 i;
	u8 tmpu1b;
	u16 tmpu2b;
	u8 pollingcnt = 20;

	if (rtlpci->first_init) {
		/* Reset PCIE Digital */
		tmpu1b = rtl_read_byte(rtlpriv, REG_SYS_FUNC_EN + 1);
		tmpu1b &= 0xFE;
		rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN + 1, tmpu1b);
		udelay(1);
		rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN + 1, tmpu1b | BIT(0));
	}

	/* Switch to SW IO control */
	tmpu1b = rtl_read_byte(rtlpriv, (SYS_CLKR + 1));
	if (tmpu1b & BIT(7)) {
		tmpu1b &= ~(BIT(6) | BIT(7));

		/* Set failed, return to prevent hang. */
		if (!_rtl92se_halset_sysclk(hw, tmpu1b))
			return;
	}

	rtl_write_byte(rtlpriv, AFE_PLL_CTRL, 0x0);
	udelay(50);
	rtl_write_byte(rtlpriv, LDOA15_CTRL, 0x34);
	udelay(50);

	/* Clear FW RPWM for FW control LPS.*/
	rtl_write_byte(rtlpriv, RPWM, 0x0);

	/* Reset MAC-IO and CPU and Core Digital BIT(10)/11/15 */
	tmpu1b = rtl_read_byte(rtlpriv, REG_SYS_FUNC_EN + 1);
	tmpu1b &= 0x73;
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN + 1, tmpu1b);
	/* wait for BIT 10/11/15 to pull high automatically!! */
	mdelay(1);

	rtl_write_byte(rtlpriv, CMDR, 0);
	rtl_write_byte(rtlpriv, TCR, 0);

	/* Data sheet not define 0x562!!! Copy from WMAC!!!!! */
	tmpu1b = rtl_read_byte(rtlpriv, 0x562);
	tmpu1b |= 0x08;
	rtl_write_byte(rtlpriv, 0x562, tmpu1b);
	tmpu1b &= ~(BIT(3));
	rtl_write_byte(rtlpriv, 0x562, tmpu1b);

	/* Enable AFE clock source */
	tmpu1b = rtl_read_byte(rtlpriv, AFE_XTAL_CTRL);
	rtl_write_byte(rtlpriv, AFE_XTAL_CTRL, (tmpu1b | 0x01));
	/* Delay 1.5ms */
	mdelay(2);
	tmpu1b = rtl_read_byte(rtlpriv, AFE_XTAL_CTRL + 1);
	rtl_write_byte(rtlpriv, AFE_XTAL_CTRL + 1, (tmpu1b & 0xfb));

	/* Enable AFE Macro Block's Bandgap */
	tmpu1b = rtl_read_byte(rtlpriv, AFE_MISC);
	rtl_write_byte(rtlpriv, AFE_MISC, (tmpu1b | BIT(0)));
	mdelay(1);

	/* Enable AFE Mbias */
	tmpu1b = rtl_read_byte(rtlpriv, AFE_MISC);
	rtl_write_byte(rtlpriv, AFE_MISC, (tmpu1b | 0x02));
	mdelay(1);

	/* Enable LDOA15 block	*/
	tmpu1b = rtl_read_byte(rtlpriv, LDOA15_CTRL);
	rtl_write_byte(rtlpriv, LDOA15_CTRL, (tmpu1b | BIT(0)));

	/* Set Digital Vdd to Retention isolation Path. */
	tmpu2b = rtl_read_word(rtlpriv, REG_SYS_ISO_CTRL);
	rtl_write_word(rtlpriv, REG_SYS_ISO_CTRL, (tmpu2b | BIT(11)));

	/* For warm reboot NIC disappera bug. */
	tmpu2b = rtl_read_word(rtlpriv, REG_SYS_FUNC_EN);
	rtl_write_word(rtlpriv, REG_SYS_FUNC_EN, (tmpu2b | BIT(13)));

	rtl_write_byte(rtlpriv, REG_SYS_ISO_CTRL + 1, 0x68);

	/* Enable AFE PLL Macro Block */
	/* We need to delay 100u before enabling PLL. */
	udelay(200);
	tmpu1b = rtl_read_byte(rtlpriv, AFE_PLL_CTRL);
	rtl_write_byte(rtlpriv, AFE_PLL_CTRL, (tmpu1b | BIT(0) | BIT(4)));

	/* for divider reset  */
	udelay(100);
	rtl_write_byte(rtlpriv, AFE_PLL_CTRL, (tmpu1b | BIT(0) |
		       BIT(4) | BIT(6)));
	udelay(10);
	rtl_write_byte(rtlpriv, AFE_PLL_CTRL, (tmpu1b | BIT(0) | BIT(4)));
	udelay(10);

	/* Enable MAC 80MHZ clock  */
	tmpu1b = rtl_read_byte(rtlpriv, AFE_PLL_CTRL + 1);
	rtl_write_byte(rtlpriv, AFE_PLL_CTRL + 1, (tmpu1b | BIT(0)));
	mdelay(1);

	/* Release isolation AFE PLL & MD */
	rtl_write_byte(rtlpriv, REG_SYS_ISO_CTRL, 0xA6);

	/* Enable MAC clock */
	tmpu2b = rtl_read_word(rtlpriv, SYS_CLKR);
	rtl_write_word(rtlpriv, SYS_CLKR, (tmpu2b | BIT(12) | BIT(11)));

	/* Enable Core digital and enable IOREG R/W */
	tmpu2b = rtl_read_word(rtlpriv, REG_SYS_FUNC_EN);
	rtl_write_word(rtlpriv, REG_SYS_FUNC_EN, (tmpu2b | BIT(11)));

	tmpu1b = rtl_read_byte(rtlpriv, REG_SYS_FUNC_EN + 1);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN + 1, tmpu1b & ~(BIT(7)));

	/* enable REG_EN */
	rtl_write_word(rtlpriv, REG_SYS_FUNC_EN, (tmpu2b | BIT(11) | BIT(15)));

	/* Switch the control path. */
	tmpu2b = rtl_read_word(rtlpriv, SYS_CLKR);
	rtl_write_word(rtlpriv, SYS_CLKR, (tmpu2b & (~BIT(2))));

	tmpu1b = rtl_read_byte(rtlpriv, (SYS_CLKR + 1));
	tmpu1b = ((tmpu1b | BIT(7)) & (~BIT(6)));
	if (!_rtl92se_halset_sysclk(hw, tmpu1b))
		return; /* Set failed, return to prevent hang. */

	rtl_write_word(rtlpriv, CMDR, 0x07FC);

	/* MH We must enable the section of code to prevent load IMEM fail. */
	/* Load MAC register from WMAc temporarily We simulate macreg. */
	/* txt HW will provide MAC txt later  */
	rtl_write_byte(rtlpriv, 0x6, 0x30);
	rtl_write_byte(rtlpriv, 0x49, 0xf0);

	rtl_write_byte(rtlpriv, 0x4b, 0x81);

	rtl_write_byte(rtlpriv, 0xb5, 0x21);

	rtl_write_byte(rtlpriv, 0xdc, 0xff);
	rtl_write_byte(rtlpriv, 0xdd, 0xff);
	rtl_write_byte(rtlpriv, 0xde, 0xff);
	rtl_write_byte(rtlpriv, 0xdf, 0xff);

	rtl_write_byte(rtlpriv, 0x11a, 0x00);
	rtl_write_byte(rtlpriv, 0x11b, 0x00);

	for (i = 0; i < 32; i++)
		rtl_write_byte(rtlpriv, INIMCS_SEL + i, 0x1b);

	rtl_write_byte(rtlpriv, 0x236, 0xff);

	rtl_write_byte(rtlpriv, 0x503, 0x22);

	if (ppsc->support_aspm && !ppsc->support_backdoor)
		rtl_write_byte(rtlpriv, 0x560, 0x40);
	else
		rtl_write_byte(rtlpriv, 0x560, 0x00);

	rtl_write_byte(rtlpriv, DBG_PORT, 0x91);

	/* Set RX Desc Address */
	rtl_write_dword(rtlpriv, RDQDA, rtlpci->rx_ring[RX_MPDU_QUEUE].dma);
	rtl_write_dword(rtlpriv, RCDA, rtlpci->rx_ring[RX_CMD_QUEUE].dma);

	/* Set TX Desc Address */
	rtl_write_dword(rtlpriv, TBKDA, rtlpci->tx_ring[BK_QUEUE].dma);
	rtl_write_dword(rtlpriv, TBEDA, rtlpci->tx_ring[BE_QUEUE].dma);
	rtl_write_dword(rtlpriv, TVIDA, rtlpci->tx_ring[VI_QUEUE].dma);
	rtl_write_dword(rtlpriv, TVODA, rtlpci->tx_ring[VO_QUEUE].dma);
	rtl_write_dword(rtlpriv, TBDA, rtlpci->tx_ring[BEACON_QUEUE].dma);
	rtl_write_dword(rtlpriv, TCDA, rtlpci->tx_ring[TXCMD_QUEUE].dma);
	rtl_write_dword(rtlpriv, TMDA, rtlpci->tx_ring[MGNT_QUEUE].dma);
	rtl_write_dword(rtlpriv, THPDA, rtlpci->tx_ring[HIGH_QUEUE].dma);
	rtl_write_dword(rtlpriv, HDA, rtlpci->tx_ring[HCCA_QUEUE].dma);

	rtl_write_word(rtlpriv, CMDR, 0x37FC);

	/* To make sure that TxDMA can ready to download FW. */
	/* We should reset TxDMA if IMEM RPT was not ready. */
	do {
		tmpu1b = rtl_read_byte(rtlpriv, TCR);
		if ((tmpu1b & TXDMA_INIT_VALUE) == TXDMA_INIT_VALUE)
			break;

		udelay(5);
	} while (pollingcnt--);

	if (pollingcnt <= 0) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "Polling TXDMA_INIT_VALUE timeout!! Current TCR(%#x)\n",
			 tmpu1b);
		tmpu1b = rtl_read_byte(rtlpriv, CMDR);
		rtl_write_byte(rtlpriv, CMDR, tmpu1b & (~TXDMA_EN));
		udelay(2);
		/* Reset TxDMA */
		rtl_write_byte(rtlpriv, CMDR, tmpu1b | TXDMA_EN);
	}

	/* After MACIO reset,we must refresh LED state. */
	if ((ppsc->rfoff_reason == RF_CHANGE_BY_IPS) ||
	   (ppsc->rfoff_reason == 0)) {
		struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
		struct rtl_led *pLed0 = &(pcipriv->ledctl.sw_led0);
		enum rf_pwrstate rfpwr_state_toset;
		rfpwr_state_toset = _rtl92se_rf_onoff_detect(hw);

		if (rfpwr_state_toset == ERFON)
			rtl92se_sw_led_on(hw, pLed0);
	}
}

static void _rtl92se_macconfig_after_fwdownload(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u8 i;
	u16 tmpu2b;

	/* 1. System Configure Register (Offset: 0x0000 - 0x003F) */

	/* 2. Command Control Register (Offset: 0x0040 - 0x004F) */
	/* Turn on 0x40 Command register */
	rtl_write_word(rtlpriv, CMDR, (BBRSTN | BB_GLB_RSTN |
			SCHEDULE_EN | MACRXEN | MACTXEN | DDMA_EN | FW2HW_EN |
			RXDMA_EN | TXDMA_EN | HCI_RXDMA_EN | HCI_TXDMA_EN));

	/* Set TCR TX DMA pre 2 FULL enable bit	*/
	rtl_write_dword(rtlpriv, TCR, rtl_read_dword(rtlpriv, TCR) |
			TXDMAPRE2FULL);

	/* Set RCR	*/
	rtl_write_dword(rtlpriv, RCR, rtlpci->receive_config);

	/* 3. MACID Setting Register (Offset: 0x0050 - 0x007F) */

	/* 4. Timing Control Register  (Offset: 0x0080 - 0x009F) */
	/* Set CCK/OFDM SIFS */
	/* CCK SIFS shall always be 10us. */
	rtl_write_word(rtlpriv, SIFS_CCK, 0x0a0a);
	rtl_write_word(rtlpriv, SIFS_OFDM, 0x1010);

	/* Set AckTimeout */
	rtl_write_byte(rtlpriv, ACK_TIMEOUT, 0x40);

	/* Beacon related */
	rtl_write_word(rtlpriv, BCN_INTERVAL, 100);
	rtl_write_word(rtlpriv, ATIMWND, 2);

	/* 5. FIFO Control Register (Offset: 0x00A0 - 0x015F) */
	/* 5.1 Initialize Number of Reserved Pages in Firmware Queue */
	/* Firmware allocate now, associate with FW internal setting.!!! */

	/* 5.2 Setting TX/RX page size 0/1/2/3/4=64/128/256/512/1024 */
	/* 5.3 Set driver info, we only accept PHY status now. */
	/* 5.4 Set RXDMA arbitration to control RXDMA/MAC/FW R/W for RXFIFO  */
	rtl_write_byte(rtlpriv, RXDMA, rtl_read_byte(rtlpriv, RXDMA) | BIT(6));

	/* 6. Adaptive Control Register  (Offset: 0x0160 - 0x01CF) */
	/* Set RRSR to all legacy rate and HT rate
	 * CCK rate is supported by default.
	 * CCK rate will be filtered out only when associated
	 * AP does not support it.
	 * Only enable ACK rate to OFDM 24M
	 * Disable RRSR for CCK rate in A-Cut	*/

	if (rtlhal->version == VERSION_8192S_ACUT)
		rtl_write_byte(rtlpriv, RRSR, 0xf0);
	else if (rtlhal->version == VERSION_8192S_BCUT)
		rtl_write_byte(rtlpriv, RRSR, 0xff);
	rtl_write_byte(rtlpriv, RRSR + 1, 0x01);
	rtl_write_byte(rtlpriv, RRSR + 2, 0x00);

	/* A-Cut IC do not support CCK rate. We forbid ARFR to */
	/* fallback to CCK rate */
	for (i = 0; i < 8; i++) {
		/*Disable RRSR for CCK rate in A-Cut */
		if (rtlhal->version == VERSION_8192S_ACUT)
			rtl_write_dword(rtlpriv, ARFR0 + i * 4, 0x1f0ff0f0);
	}

	/* Different rate use different AMPDU size */
	/* MCS32/ MCS15_SG use max AMPDU size 15*2=30K */
	rtl_write_byte(rtlpriv, AGGLEN_LMT_H, 0x0f);
	/* MCS0/1/2/3 use max AMPDU size 4*2=8K */
	rtl_write_word(rtlpriv, AGGLEN_LMT_L, 0x7442);
	/* MCS4/5 use max AMPDU size 8*2=16K 6/7 use 10*2=20K */
	rtl_write_word(rtlpriv, AGGLEN_LMT_L + 2, 0xddd7);
	/* MCS8/9 use max AMPDU size 8*2=16K 10/11 use 10*2=20K */
	rtl_write_word(rtlpriv, AGGLEN_LMT_L + 4, 0xd772);
	/* MCS12/13/14/15 use max AMPDU size 15*2=30K */
	rtl_write_word(rtlpriv, AGGLEN_LMT_L + 6, 0xfffd);

	/* Set Data / Response auto rate fallack retry count */
	rtl_write_dword(rtlpriv, DARFRC, 0x04010000);
	rtl_write_dword(rtlpriv, DARFRC + 4, 0x09070605);
	rtl_write_dword(rtlpriv, RARFRC, 0x04010000);
	rtl_write_dword(rtlpriv, RARFRC + 4, 0x09070605);

	/* 7. EDCA Setting Register (Offset: 0x01D0 - 0x01FF) */
	/* Set all rate to support SG */
	rtl_write_word(rtlpriv, SG_RATE, 0xFFFF);

	/* 8. WMAC, BA, and CCX related Register (Offset: 0x0200 - 0x023F) */
	/* Set NAV protection length */
	rtl_write_word(rtlpriv, NAV_PROT_LEN, 0x0080);
	/* CF-END Threshold */
	rtl_write_byte(rtlpriv, CFEND_TH, 0xFF);
	/* Set AMPDU minimum space */
	rtl_write_byte(rtlpriv, AMPDU_MIN_SPACE, 0x07);
	/* Set TXOP stall control for several queue/HI/BCN/MGT/ */
	rtl_write_byte(rtlpriv, TXOP_STALL_CTRL, 0x00);

	/* 9. Security Control Register (Offset: 0x0240 - 0x025F) */
	/* 10. Power Save Control Register (Offset: 0x0260 - 0x02DF) */
	/* 11. General Purpose Register (Offset: 0x02E0 - 0x02FF) */
	/* 12. Host Interrupt Status Register (Offset: 0x0300 - 0x030F) */
	/* 13. Test Mode and Debug Control Register (Offset: 0x0310 - 0x034F) */

	/* 14. Set driver info, we only accept PHY status now. */
	rtl_write_byte(rtlpriv, RXDRVINFO_SZ, 4);

	/* 15. For EEPROM R/W Workaround */
	/* 16. For EFUSE to share REG_SYS_FUNC_EN with EEPROM!!! */
	tmpu2b = rtl_read_byte(rtlpriv, REG_SYS_FUNC_EN);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, tmpu2b | BIT(13));
	tmpu2b = rtl_read_byte(rtlpriv, REG_SYS_ISO_CTRL);
	rtl_write_byte(rtlpriv, REG_SYS_ISO_CTRL, tmpu2b & (~BIT(8)));

	/* 17. For EFUSE */
	/* We may R/W EFUSE in EEPROM mode */
	if (rtlefuse->epromtype == EEPROM_BOOT_EFUSE) {
		u8	tempval;

		tempval = rtl_read_byte(rtlpriv, REG_SYS_ISO_CTRL + 1);
		tempval &= 0xFE;
		rtl_write_byte(rtlpriv, REG_SYS_ISO_CTRL + 1, tempval);

		/* Change Program timing */
		rtl_write_byte(rtlpriv, REG_EFUSE_CTRL + 3, 0x72);
		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "EFUSE CONFIG OK\n");
	}

	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "OK\n");

}

static void _rtl92se_hw_configure(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	u8 reg_bw_opmode = 0;
	u32 reg_rrsr = 0;
	u8 regtmp = 0;

	reg_bw_opmode = BW_OPMODE_20MHZ;
	reg_rrsr = RATE_ALL_CCK | RATE_ALL_OFDM_AG;

	regtmp = rtl_read_byte(rtlpriv, INIRTSMCS_SEL);
	reg_rrsr = ((reg_rrsr & 0x000fffff) << 8) | regtmp;
	rtl_write_dword(rtlpriv, INIRTSMCS_SEL, reg_rrsr);
	rtl_write_byte(rtlpriv, BW_OPMODE, reg_bw_opmode);

	/* Set Retry Limit here */
	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_RETRY_LIMIT,
			(u8 *)(&rtlpci->shortretry_limit));

	rtl_write_byte(rtlpriv, MLT, 0x8f);

	/* For Min Spacing configuration. */
	switch (rtlphy->rf_type) {
	case RF_1T2R:
	case RF_1T1R:
		rtlhal->minspace_cfg = (MAX_MSS_DENSITY_1T << 3);
		break;
	case RF_2T2R:
	case RF_2T2R_GREEN:
		rtlhal->minspace_cfg = (MAX_MSS_DENSITY_2T << 3);
		break;
	}
	rtl_write_byte(rtlpriv, AMPDU_MIN_SPACE, rtlhal->minspace_cfg);
}

int rtl92se_hw_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u8 tmp_byte = 0;
	unsigned long flags;
	bool rtstatus = true;
	u8 tmp_u1b;
	int err = false;
	u8 i;
	int wdcapra_add[] = {
		EDCAPARA_BE, EDCAPARA_BK,
		EDCAPARA_VI, EDCAPARA_VO};
	u8 secr_value = 0x0;

	rtlpci->being_init_adapter = true;

	/* As this function can take a very long time (up to 350 ms)
	 * and can be called with irqs disabled, reenable the irqs
	 * to let the other devices continue being serviced.
	 *
	 * It is safe doing so since our own interrupts will only be enabled
	 * in a subsequent step.
	 */
	local_save_flags(flags);
	local_irq_enable();

	rtlpriv->intf_ops->disable_aspm(hw);

	/* 1. MAC Initialize */
	/* Before FW download, we have to set some MAC register */
	_rtl92se_macconfig_before_fwdownload(hw);

	rtlhal->version = (enum version_8192s)((rtl_read_dword(rtlpriv,
			PMC_FSM) >> 16) & 0xF);

	rtl8192se_gpiobit3_cfg_inputmode(hw);

	/* 2. download firmware */
	rtstatus = rtl92s_download_fw(hw);
	if (!rtstatus) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 "Failed to download FW. Init HW without FW now... "
			 "Please copy FW into /lib/firmware/rtlwifi\n");
		err = 1;
		goto exit;
	}

	/* After FW download, we have to reset MAC register */
	_rtl92se_macconfig_after_fwdownload(hw);

	/*Retrieve default FW Cmd IO map. */
	rtlhal->fwcmd_iomap =	rtl_read_word(rtlpriv, LBUS_MON_ADDR);
	rtlhal->fwcmd_ioparam = rtl_read_dword(rtlpriv, LBUS_ADDR_MASK);

	/* 3. Initialize MAC/PHY Config by MACPHY_reg.txt */
	if (!rtl92s_phy_mac_config(hw)) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "MAC Config failed\n");
		err = rtstatus;
		goto exit;
	}

	/* because last function modify RCR, so we update
	 * rcr var here, or TP will unstable for receive_config
	 * is wrong, RX RCR_ACRC32 will cause TP unstabel & Rx
	 * RCR_APP_ICV will cause mac80211 unassoc for cisco 1252
	 */
	rtlpci->receive_config = rtl_read_dword(rtlpriv, RCR);
	rtlpci->receive_config &= ~(RCR_ACRC32 | RCR_AICV);
	rtl_write_dword(rtlpriv, RCR, rtlpci->receive_config);

	/* Make sure BB/RF write OK. We should prevent enter IPS. radio off. */
	/* We must set flag avoid BB/RF config period later!! */
	rtl_write_dword(rtlpriv, CMDR, 0x37FC);

	/* 4. Initialize BB After MAC Config PHY_reg.txt, AGC_Tab.txt */
	if (!rtl92s_phy_bb_config(hw)) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_EMERG, "BB Config failed\n");
		err = rtstatus;
		goto exit;
	}

	/* 5. Initiailze RF RAIO_A.txt RF RAIO_B.txt */
	/* Before initalizing RF. We can not use FW to do RF-R/W. */

	rtlphy->rf_mode = RF_OP_BY_SW_3WIRE;

	/* Before RF-R/W we must execute the IO from Scott's suggestion. */
	rtl_write_byte(rtlpriv, AFE_XTAL_CTRL + 1, 0xDB);
	if (rtlhal->version == VERSION_8192S_ACUT)
		rtl_write_byte(rtlpriv, SPS1_CTRL + 3, 0x07);
	else
		rtl_write_byte(rtlpriv, RF_CTRL, 0x07);

	if (!rtl92s_phy_rf_config(hw)) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "RF Config failed\n");
		err = rtstatus;
		goto exit;
	}

	/* After read predefined TXT, we must set BB/MAC/RF
	 * register as our requirement */

	rtlphy->rfreg_chnlval[0] = rtl92s_phy_query_rf_reg(hw,
							   (enum radio_path)0,
							   RF_CHNLBW,
							   RFREG_OFFSET_MASK);
	rtlphy->rfreg_chnlval[1] = rtl92s_phy_query_rf_reg(hw,
							   (enum radio_path)1,
							   RF_CHNLBW,
							   RFREG_OFFSET_MASK);

	/*---- Set CCK and OFDM Block "ON"----*/
	rtl_set_bbreg(hw, RFPGA0_RFMOD, BCCKEN, 0x1);
	rtl_set_bbreg(hw, RFPGA0_RFMOD, BOFDMEN, 0x1);

	/*3 Set Hardware(Do nothing now) */
	_rtl92se_hw_configure(hw);

	/* Read EEPROM TX power index and PHY_REG_PG.txt to capture correct */
	/* TX power index for different rate set. */
	/* Get original hw reg values */
	rtl92s_phy_get_hw_reg_originalvalue(hw);
	/* Write correct tx power index */
	rtl92s_phy_set_txpower(hw, rtlphy->current_channel);

	/* We must set MAC address after firmware download. */
	for (i = 0; i < 6; i++)
		rtl_write_byte(rtlpriv, MACIDR0 + i, rtlefuse->dev_addr[i]);

	/* EEPROM R/W workaround */
	tmp_u1b = rtl_read_byte(rtlpriv, MAC_PINMUX_CFG);
	rtl_write_byte(rtlpriv, MAC_PINMUX_CFG, tmp_u1b & (~BIT(3)));

	rtl_write_byte(rtlpriv, 0x4d, 0x0);

	if (hal_get_firmwareversion(rtlpriv) >= 0x49) {
		tmp_byte = rtl_read_byte(rtlpriv, FW_RSVD_PG_CRTL) & (~BIT(4));
		tmp_byte = tmp_byte | BIT(5);
		rtl_write_byte(rtlpriv, FW_RSVD_PG_CRTL, tmp_byte);
		rtl_write_dword(rtlpriv, TXDESC_MSK, 0xFFFFCFFF);
	}

	/* We enable high power and RA related mechanism after NIC
	 * initialized. */
	if (hal_get_firmwareversion(rtlpriv) >= 0x35) {
		/* Fw v.53 and later. */
		rtl92s_phy_set_fw_cmd(hw, FW_CMD_RA_INIT);
	} else if (hal_get_firmwareversion(rtlpriv) == 0x34) {
		/* Fw v.52. */
		rtl_write_dword(rtlpriv, WFM5, FW_RA_INIT);
		rtl92s_phy_chk_fwcmd_iodone(hw);
	} else {
		/* Compatible earlier FW version. */
		rtl_write_dword(rtlpriv, WFM5, FW_RA_RESET);
		rtl92s_phy_chk_fwcmd_iodone(hw);
		rtl_write_dword(rtlpriv, WFM5, FW_RA_ACTIVE);
		rtl92s_phy_chk_fwcmd_iodone(hw);
		rtl_write_dword(rtlpriv, WFM5, FW_RA_REFRESH);
		rtl92s_phy_chk_fwcmd_iodone(hw);
	}

	/* Add to prevent ASPM bug. */
	/* Always enable hst and NIC clock request. */
	rtl92s_phy_switch_ephy_parameter(hw);

	/* Security related
	 * 1. Clear all H/W keys.
	 * 2. Enable H/W encryption/decryption. */
	rtl_cam_reset_all_entry(hw);
	secr_value |= SCR_TXENCENABLE;
	secr_value |= SCR_RXENCENABLE;
	secr_value |= SCR_NOSKMC;
	rtl_write_byte(rtlpriv, REG_SECR, secr_value);

	for (i = 0; i < 4; i++)
		rtl_write_dword(rtlpriv, wdcapra_add[i], 0x5e4322);

	if (rtlphy->rf_type == RF_1T2R) {
		bool mrc2set = true;
		/* Turn on B-Path */
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_MRC, (u8 *)&mrc2set);
	}

	rtlpriv->cfg->ops->led_control(hw, LED_CTL_POWER_ON);
	rtl92s_dm_init(hw);
exit:
	local_irq_restore(flags);
	rtlpci->being_init_adapter = false;
	return err;
}

void rtl92se_set_mac_addr(struct rtl_io *io, const u8 *addr)
{
	/* This is a stub. */
}

void rtl92se_set_check_bssid(struct ieee80211_hw *hw, bool check_bssid)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 reg_rcr;

	if (rtlpriv->psc.rfpwr_state != ERFON)
		return;

	rtlpriv->cfg->ops->get_hw_reg(hw, HW_VAR_RCR, (u8 *)(&reg_rcr));

	if (check_bssid) {
		reg_rcr |= (RCR_CBSSID);
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_RCR, (u8 *)(&reg_rcr));
	} else if (!check_bssid) {
		reg_rcr &= (~RCR_CBSSID);
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_RCR, (u8 *)(&reg_rcr));
	}

}

static int _rtl92se_set_media_status(struct ieee80211_hw *hw,
				     enum nl80211_iftype type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 bt_msr = rtl_read_byte(rtlpriv, MSR);
	u32 temp;
	bt_msr &= ~MSR_LINK_MASK;

	switch (type) {
	case NL80211_IFTYPE_UNSPECIFIED:
		bt_msr |= (MSR_LINK_NONE << MSR_LINK_SHIFT);
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "Set Network type to NO LINK!\n");
		break;
	case NL80211_IFTYPE_ADHOC:
		bt_msr |= (MSR_LINK_ADHOC << MSR_LINK_SHIFT);
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "Set Network type to Ad Hoc!\n");
		break;
	case NL80211_IFTYPE_STATION:
		bt_msr |= (MSR_LINK_MANAGED << MSR_LINK_SHIFT);
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "Set Network type to STA!\n");
		break;
	case NL80211_IFTYPE_AP:
		bt_msr |= (MSR_LINK_MASTER << MSR_LINK_SHIFT);
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "Set Network type to AP!\n");
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "Network type %d not supported!\n", type);
		return 1;

	}

	if (type != NL80211_IFTYPE_AP &&
	    rtlpriv->mac80211.link_state < MAC80211_LINKED)
		bt_msr = rtl_read_byte(rtlpriv, MSR) & ~MSR_LINK_MASK;
	rtl_write_byte(rtlpriv, MSR, bt_msr);

	temp = rtl_read_dword(rtlpriv, TCR);
	rtl_write_dword(rtlpriv, TCR, temp & (~BIT(8)));
	rtl_write_dword(rtlpriv, TCR, temp | BIT(8));


	return 0;
}

/* HW_VAR_MEDIA_STATUS & HW_VAR_CECHK_BSSID */
int rtl92se_set_network_type(struct ieee80211_hw *hw, enum nl80211_iftype type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (_rtl92se_set_media_status(hw, type))
		return -EOPNOTSUPP;

	if (rtlpriv->mac80211.link_state == MAC80211_LINKED) {
		if (type != NL80211_IFTYPE_AP)
			rtl92se_set_check_bssid(hw, true);
	} else {
		rtl92se_set_check_bssid(hw, false);
	}

	return 0;
}

/* don't set REG_EDCA_BE_PARAM here because mac80211 will send pkt when scan */
void rtl92se_set_qos(struct ieee80211_hw *hw, int aci)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	rtl92s_dm_init_edca_turbo(hw);

	switch (aci) {
	case AC1_BK:
		rtl_write_dword(rtlpriv, EDCAPARA_BK, 0xa44f);
		break;
	case AC0_BE:
		/* rtl_write_dword(rtlpriv, EDCAPARA_BE, u4b_ac_param); */
		break;
	case AC2_VI:
		rtl_write_dword(rtlpriv, EDCAPARA_VI, 0x5e4322);
		break;
	case AC3_VO:
		rtl_write_dword(rtlpriv, EDCAPARA_VO, 0x2f3222);
		break;
	default:
		RT_ASSERT(false, "invalid aci: %d !\n", aci);
		break;
	}
}

void rtl92se_enable_interrupt(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	rtl_write_dword(rtlpriv, INTA_MASK, rtlpci->irq_mask[0]);
	/* Support Bit 32-37(Assign as Bit 0-5) interrupt setting now */
	rtl_write_dword(rtlpriv, INTA_MASK + 4, rtlpci->irq_mask[1] & 0x3F);
	rtlpci->irq_enabled = true;
}

void rtl92se_disable_interrupt(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv;
	struct rtl_pci *rtlpci;

	rtlpriv = rtl_priv(hw);
	/* if firmware not available, no interrupts */
	if (!rtlpriv || !rtlpriv->max_fw_size)
		return;
	rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	rtl_write_dword(rtlpriv, INTA_MASK, 0);
	rtl_write_dword(rtlpriv, INTA_MASK + 4, 0);
	rtlpci->irq_enabled = false;
}

static u8 _rtl92s_set_sysclk(struct ieee80211_hw *hw, u8 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 waitcnt = 100;
	bool result = false;
	u8 tmp;

	rtl_write_byte(rtlpriv, SYS_CLKR + 1, data);

	/* Wait the MAC synchronized. */
	udelay(400);

	/* Check if it is set ready. */
	tmp = rtl_read_byte(rtlpriv, SYS_CLKR + 1);
	result = ((tmp & BIT(7)) == (data & BIT(7)));

	if ((data & (BIT(6) | BIT(7))) == false) {
		waitcnt = 100;
		tmp = 0;

		while (1) {
			waitcnt--;
			tmp = rtl_read_byte(rtlpriv, SYS_CLKR + 1);

			if ((tmp & BIT(6)))
				break;

			pr_err("wait for BIT(6) return value %x\n", tmp);

			if (waitcnt == 0)
				break;
			udelay(10);
		}

		if (waitcnt == 0)
			result = false;
		else
			result = true;
	}

	return result;
}

static void _rtl92s_phy_set_rfhalt(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	u8 u1btmp;

	if (rtlhal->driver_going2unload)
		rtl_write_byte(rtlpriv, 0x560, 0x0);

	/* Power save for BB/RF */
	u1btmp = rtl_read_byte(rtlpriv, LDOV12D_CTRL);
	u1btmp |= BIT(0);
	rtl_write_byte(rtlpriv, LDOV12D_CTRL, u1btmp);
	rtl_write_byte(rtlpriv, SPS1_CTRL, 0x0);
	rtl_write_byte(rtlpriv, TXPAUSE, 0xFF);
	rtl_write_word(rtlpriv, CMDR, 0x57FC);
	udelay(100);
	rtl_write_word(rtlpriv, CMDR, 0x77FC);
	rtl_write_byte(rtlpriv, PHY_CCA, 0x0);
	udelay(10);
	rtl_write_word(rtlpriv, CMDR, 0x37FC);
	udelay(10);
	rtl_write_word(rtlpriv, CMDR, 0x77FC);
	udelay(10);
	rtl_write_word(rtlpriv, CMDR, 0x57FC);
	rtl_write_word(rtlpriv, CMDR, 0x0000);

	if (rtlhal->driver_going2unload) {
		u1btmp = rtl_read_byte(rtlpriv, (REG_SYS_FUNC_EN + 1));
		u1btmp &= ~(BIT(0));
		rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN + 1, u1btmp);
	}

	u1btmp = rtl_read_byte(rtlpriv, (SYS_CLKR + 1));

	/* Add description. After switch control path. register
	 * after page1 will be invisible. We can not do any IO
	 * for register>0x40. After resume&MACIO reset, we need
	 * to remember previous reg content. */
	if (u1btmp & BIT(7)) {
		u1btmp &= ~(BIT(6) | BIT(7));
		if (!_rtl92s_set_sysclk(hw, u1btmp)) {
			pr_err("Switch ctrl path fail\n");
			return;
		}
	}

	/* Power save for MAC */
	if (ppsc->rfoff_reason == RF_CHANGE_BY_IPS  &&
		!rtlhal->driver_going2unload) {
		/* enable LED function */
		rtl_write_byte(rtlpriv, 0x03, 0xF9);
	/* SW/HW radio off or halt adapter!! For example S3/S4 */
	} else {
		/* LED function disable. Power range is about 8mA now. */
		/* if write 0xF1 disconnet_pci power
		 *	 ifconfig wlan0 down power are both high 35:70 */
		/* if write oxF9 disconnet_pci power
		 * ifconfig wlan0 down power are both low  12:45*/
		rtl_write_byte(rtlpriv, 0x03, 0xF9);
	}

	rtl_write_byte(rtlpriv, SYS_CLKR + 1, 0x70);
	rtl_write_byte(rtlpriv, AFE_PLL_CTRL + 1, 0x68);
	rtl_write_byte(rtlpriv,  AFE_PLL_CTRL, 0x00);
	rtl_write_byte(rtlpriv, LDOA15_CTRL, 0x34);
	rtl_write_byte(rtlpriv, AFE_XTAL_CTRL, 0x0E);
	RT_SET_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC);

}

static void _rtl92se_gen_refreshledstate(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_led *pLed0 = &(pcipriv->ledctl.sw_led0);

	if (rtlpci->up_first_time == 1)
		return;

	if (rtlpriv->psc.rfoff_reason == RF_CHANGE_BY_IPS)
		rtl92se_sw_led_on(hw, pLed0);
	else
		rtl92se_sw_led_off(hw, pLed0);
}


static void _rtl92se_power_domain_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u16 tmpu2b;
	u8 tmpu1b;

	rtlpriv->psc.pwrdomain_protect = true;

	tmpu1b = rtl_read_byte(rtlpriv, (SYS_CLKR + 1));
	if (tmpu1b & BIT(7)) {
		tmpu1b &= ~(BIT(6) | BIT(7));
		if (!_rtl92s_set_sysclk(hw, tmpu1b)) {
			rtlpriv->psc.pwrdomain_protect = false;
			return;
		}
	}

	rtl_write_byte(rtlpriv, AFE_PLL_CTRL, 0x0);
	rtl_write_byte(rtlpriv, LDOA15_CTRL, 0x34);

	/* Reset MAC-IO and CPU and Core Digital BIT10/11/15 */
	tmpu1b = rtl_read_byte(rtlpriv, REG_SYS_FUNC_EN + 1);

	/* If IPS we need to turn LED on. So we not
	 * not disable BIT 3/7 of reg3. */
	if (rtlpriv->psc.rfoff_reason & (RF_CHANGE_BY_IPS | RF_CHANGE_BY_HW))
		tmpu1b &= 0xFB;
	else
		tmpu1b &= 0x73;

	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN + 1, tmpu1b);
	/* wait for BIT 10/11/15 to pull high automatically!! */
	mdelay(1);

	rtl_write_byte(rtlpriv, CMDR, 0);
	rtl_write_byte(rtlpriv, TCR, 0);

	/* Data sheet not define 0x562!!! Copy from WMAC!!!!! */
	tmpu1b = rtl_read_byte(rtlpriv, 0x562);
	tmpu1b |= 0x08;
	rtl_write_byte(rtlpriv, 0x562, tmpu1b);
	tmpu1b &= ~(BIT(3));
	rtl_write_byte(rtlpriv, 0x562, tmpu1b);

	/* Enable AFE clock source */
	tmpu1b = rtl_read_byte(rtlpriv, AFE_XTAL_CTRL);
	rtl_write_byte(rtlpriv, AFE_XTAL_CTRL, (tmpu1b | 0x01));
	/* Delay 1.5ms */
	udelay(1500);
	tmpu1b = rtl_read_byte(rtlpriv, AFE_XTAL_CTRL + 1);
	rtl_write_byte(rtlpriv, AFE_XTAL_CTRL + 1, (tmpu1b & 0xfb));

	/* Enable AFE Macro Block's Bandgap */
	tmpu1b = rtl_read_byte(rtlpriv, AFE_MISC);
	rtl_write_byte(rtlpriv, AFE_MISC, (tmpu1b | BIT(0)));
	mdelay(1);

	/* Enable AFE Mbias */
	tmpu1b = rtl_read_byte(rtlpriv, AFE_MISC);
	rtl_write_byte(rtlpriv, AFE_MISC, (tmpu1b | 0x02));
	mdelay(1);

	/* Enable LDOA15 block */
	tmpu1b = rtl_read_byte(rtlpriv, LDOA15_CTRL);
	rtl_write_byte(rtlpriv, LDOA15_CTRL, (tmpu1b | BIT(0)));

	/* Set Digital Vdd to Retention isolation Path. */
	tmpu2b = rtl_read_word(rtlpriv, REG_SYS_ISO_CTRL);
	rtl_write_word(rtlpriv, REG_SYS_ISO_CTRL, (tmpu2b | BIT(11)));


	/* For warm reboot NIC disappera bug. */
	tmpu2b = rtl_read_word(rtlpriv, REG_SYS_FUNC_EN);
	rtl_write_word(rtlpriv, REG_SYS_FUNC_EN, (tmpu2b | BIT(13)));

	rtl_write_byte(rtlpriv, REG_SYS_ISO_CTRL + 1, 0x68);

	/* Enable AFE PLL Macro Block */
	tmpu1b = rtl_read_byte(rtlpriv, AFE_PLL_CTRL);
	rtl_write_byte(rtlpriv, AFE_PLL_CTRL, (tmpu1b | BIT(0) | BIT(4)));
	/* Enable MAC 80MHZ clock */
	tmpu1b = rtl_read_byte(rtlpriv, AFE_PLL_CTRL + 1);
	rtl_write_byte(rtlpriv, AFE_PLL_CTRL + 1, (tmpu1b | BIT(0)));
	mdelay(1);

	/* Release isolation AFE PLL & MD */
	rtl_write_byte(rtlpriv, REG_SYS_ISO_CTRL, 0xA6);

	/* Enable MAC clock */
	tmpu2b = rtl_read_word(rtlpriv, SYS_CLKR);
	rtl_write_word(rtlpriv, SYS_CLKR, (tmpu2b | BIT(12) | BIT(11)));

	/* Enable Core digital and enable IOREG R/W */
	tmpu2b = rtl_read_word(rtlpriv, REG_SYS_FUNC_EN);
	rtl_write_word(rtlpriv, REG_SYS_FUNC_EN, (tmpu2b | BIT(11)));
	/* enable REG_EN */
	rtl_write_word(rtlpriv, REG_SYS_FUNC_EN, (tmpu2b | BIT(11) | BIT(15)));

	/* Switch the control path. */
	tmpu2b = rtl_read_word(rtlpriv, SYS_CLKR);
	rtl_write_word(rtlpriv, SYS_CLKR, (tmpu2b & (~BIT(2))));

	tmpu1b = rtl_read_byte(rtlpriv, (SYS_CLKR + 1));
	tmpu1b = ((tmpu1b | BIT(7)) & (~BIT(6)));
	if (!_rtl92s_set_sysclk(hw, tmpu1b)) {
		rtlpriv->psc.pwrdomain_protect = false;
		return;
	}

	rtl_write_word(rtlpriv, CMDR, 0x37FC);

	/* After MACIO reset,we must refresh LED state. */
	_rtl92se_gen_refreshledstate(hw);

	rtlpriv->psc.pwrdomain_protect = false;
}

void rtl92se_card_disable(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	enum nl80211_iftype opmode;
	u8 wait = 30;

	rtlpriv->intf_ops->enable_aspm(hw);

	if (rtlpci->driver_is_goingto_unload ||
		ppsc->rfoff_reason > RF_CHANGE_BY_PS)
		rtlpriv->cfg->ops->led_control(hw, LED_CTL_POWER_OFF);

	/* we should chnge GPIO to input mode
	 * this will drop away current about 25mA*/
	rtl8192se_gpiobit3_cfg_inputmode(hw);

	/* this is very important for ips power save */
	while (wait-- >= 10 && rtlpriv->psc.pwrdomain_protect) {
		if (rtlpriv->psc.pwrdomain_protect)
			mdelay(20);
		else
			break;
	}

	mac->link_state = MAC80211_NOLINK;
	opmode = NL80211_IFTYPE_UNSPECIFIED;
	_rtl92se_set_media_status(hw, opmode);

	_rtl92s_phy_set_rfhalt(hw);
	udelay(100);
}

void rtl92se_interrupt_recognized(struct ieee80211_hw *hw, u32 *p_inta,
			     u32 *p_intb)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	*p_inta = rtl_read_dword(rtlpriv, ISR) & rtlpci->irq_mask[0];
	rtl_write_dword(rtlpriv, ISR, *p_inta);

	*p_intb = rtl_read_dword(rtlpriv, ISR + 4) & rtlpci->irq_mask[1];
	rtl_write_dword(rtlpriv, ISR + 4, *p_intb);
}

void rtl92se_set_beacon_related_registers(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u16 bcntime_cfg = 0;
	u16 bcn_cw = 6, bcn_ifs = 0xf;
	u16 atim_window = 2;

	/* ATIM Window (in unit of TU). */
	rtl_write_word(rtlpriv, ATIMWND, atim_window);

	/* Beacon interval (in unit of TU). */
	rtl_write_word(rtlpriv, BCN_INTERVAL, mac->beacon_interval);

	/* DrvErlyInt (in unit of TU). (Time to send
	 * interrupt to notify driver to change
	 * beacon content) */
	rtl_write_word(rtlpriv, BCN_DRV_EARLY_INT, 10 << 4);

	/* BcnDMATIM(in unit of us). Indicates the
	 * time before TBTT to perform beacon queue DMA  */
	rtl_write_word(rtlpriv, BCN_DMATIME, 256);

	/* Force beacon frame transmission even
	 * after receiving beacon frame from
	 * other ad hoc STA */
	rtl_write_byte(rtlpriv, BCN_ERR_THRESH, 100);

	/* Beacon Time Configuration */
	if (mac->opmode == NL80211_IFTYPE_ADHOC)
		bcntime_cfg |= (bcn_cw << BCN_TCFG_CW_SHIFT);

	/* TODO: bcn_ifs may required to be changed on ASIC */
	bcntime_cfg |= bcn_ifs << BCN_TCFG_IFS;

	/*for beacon changed */
	rtl92s_phy_set_beacon_hwreg(hw, mac->beacon_interval);
}

void rtl92se_set_beacon_interval(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u16 bcn_interval = mac->beacon_interval;

	/* Beacon interval (in unit of TU). */
	rtl_write_word(rtlpriv, BCN_INTERVAL, bcn_interval);
	/* 2008.10.24 added by tynli for beacon changed. */
	rtl92s_phy_set_beacon_hwreg(hw, bcn_interval);
}

void rtl92se_update_interrupt_mask(struct ieee80211_hw *hw,
		u32 add_msr, u32 rm_msr)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	RT_TRACE(rtlpriv, COMP_INTR, DBG_LOUD, "add_msr:%x, rm_msr:%x\n",
		 add_msr, rm_msr);

	if (add_msr)
		rtlpci->irq_mask[0] |= add_msr;

	if (rm_msr)
		rtlpci->irq_mask[0] &= (~rm_msr);

	rtl92se_disable_interrupt(hw);
	rtl92se_enable_interrupt(hw);
}

static void _rtl8192se_get_IC_Inferiority(struct ieee80211_hw *hw)
{
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 efuse_id;

	rtlhal->ic_class = IC_INFERIORITY_A;

	/* Only retrieving while using EFUSE. */
	if ((rtlefuse->epromtype == EEPROM_BOOT_EFUSE) &&
		!rtlefuse->autoload_failflag) {
		efuse_id = efuse_read_1byte(hw, EFUSE_IC_ID_OFFSET);

		if (efuse_id == 0xfe)
			rtlhal->ic_class = IC_INFERIORITY_B;
	}
}

static void _rtl92se_read_adapter_info(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct device *dev = &rtl_pcipriv(hw)->dev.pdev->dev;
	u16 i, usvalue;
	u16	eeprom_id;
	u8 tempval;
	u8 hwinfo[HWSET_MAX_SIZE_92S];
	u8 rf_path, index;

	switch (rtlefuse->epromtype) {
	case EEPROM_BOOT_EFUSE:
		rtl_efuse_shadow_map_update(hw);
		break;

	case EEPROM_93C46:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "RTL819X Not boot from eeprom, check it !!\n");
		return;

	default:
		dev_warn(dev, "no efuse data\n");
		return;
	}

	memcpy(hwinfo, &rtlefuse->efuse_map[EFUSE_INIT_MAP][0],
	       HWSET_MAX_SIZE_92S);

	RT_PRINT_DATA(rtlpriv, COMP_INIT, DBG_DMESG, "MAP",
		      hwinfo, HWSET_MAX_SIZE_92S);

	eeprom_id = *((u16 *)&hwinfo[0]);
	if (eeprom_id != RTL8190_EEPROM_ID) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 "EEPROM ID(%#x) is invalid!!\n", eeprom_id);
		rtlefuse->autoload_failflag = true;
	} else {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "Autoload OK\n");
		rtlefuse->autoload_failflag = false;
	}

	if (rtlefuse->autoload_failflag)
		return;

	_rtl8192se_get_IC_Inferiority(hw);

	/* Read IC Version && Channel Plan */
	/* VID, DID	 SE	0xA-D */
	rtlefuse->eeprom_vid = *(u16 *)&hwinfo[EEPROM_VID];
	rtlefuse->eeprom_did = *(u16 *)&hwinfo[EEPROM_DID];
	rtlefuse->eeprom_svid = *(u16 *)&hwinfo[EEPROM_SVID];
	rtlefuse->eeprom_smid = *(u16 *)&hwinfo[EEPROM_SMID];
	rtlefuse->eeprom_version = *(u16 *)&hwinfo[EEPROM_VERSION];

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "EEPROMId = 0x%4x\n", eeprom_id);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "EEPROM VID = 0x%4x\n", rtlefuse->eeprom_vid);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "EEPROM DID = 0x%4x\n", rtlefuse->eeprom_did);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "EEPROM SVID = 0x%4x\n", rtlefuse->eeprom_svid);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "EEPROM SMID = 0x%4x\n", rtlefuse->eeprom_smid);

	for (i = 0; i < 6; i += 2) {
		usvalue = *(u16 *)&hwinfo[EEPROM_MAC_ADDR + i];
		*((u16 *) (&rtlefuse->dev_addr[i])) = usvalue;
	}

	for (i = 0; i < 6; i++)
		rtl_write_byte(rtlpriv, MACIDR0 + i, rtlefuse->dev_addr[i]);

	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "%pM\n", rtlefuse->dev_addr);

	/* Get Tx Power Level by Channel */
	/* Read Tx power of Channel 1 ~ 14 from EEPROM. */
	/* 92S suupport RF A & B */
	for (rf_path = 0; rf_path < 2; rf_path++) {
		for (i = 0; i < 3; i++) {
			/* Read CCK RF A & B Tx power  */
			rtlefuse->eeprom_chnlarea_txpwr_cck[rf_path][i] =
			hwinfo[EEPROM_TXPOWERBASE + rf_path * 3 + i];

			/* Read OFDM RF A & B Tx power for 1T */
			rtlefuse->eeprom_chnlarea_txpwr_ht40_1s[rf_path][i] =
			hwinfo[EEPROM_TXPOWERBASE + 6 + rf_path * 3 + i];

			/* Read OFDM RF A & B Tx power for 2T */
			rtlefuse->eprom_chnl_txpwr_ht40_2sdf[rf_path][i]
				 = hwinfo[EEPROM_TXPOWERBASE + 12 +
				   rf_path * 3 + i];
		}
	}

	for (rf_path = 0; rf_path < 2; rf_path++)
		for (i = 0; i < 3; i++)
			RTPRINT(rtlpriv, FINIT, INIT_EEPROM,
				"RF(%d) EEPROM CCK Area(%d) = 0x%x\n",
				rf_path, i,
				rtlefuse->eeprom_chnlarea_txpwr_cck
				[rf_path][i]);
	for (rf_path = 0; rf_path < 2; rf_path++)
		for (i = 0; i < 3; i++)
			RTPRINT(rtlpriv, FINIT, INIT_EEPROM,
				"RF(%d) EEPROM HT40 1S Area(%d) = 0x%x\n",
				rf_path, i,
				rtlefuse->eeprom_chnlarea_txpwr_ht40_1s
				[rf_path][i]);
	for (rf_path = 0; rf_path < 2; rf_path++)
		for (i = 0; i < 3; i++)
			RTPRINT(rtlpriv, FINIT, INIT_EEPROM,
				"RF(%d) EEPROM HT40 2S Diff Area(%d) = 0x%x\n",
				rf_path, i,
				rtlefuse->eprom_chnl_txpwr_ht40_2sdf
				[rf_path][i]);

	for (rf_path = 0; rf_path < 2; rf_path++) {

		/* Assign dedicated channel tx power */
		for (i = 0; i < 14; i++)	{
			/* channel 1~3 use the same Tx Power Level. */
			if (i < 3)
				index = 0;
			/* Channel 4-8 */
			else if (i < 8)
				index = 1;
			/* Channel 9-14 */
			else
				index = 2;

			/* Record A & B CCK /OFDM - 1T/2T Channel area
			 * tx power */
			rtlefuse->txpwrlevel_cck[rf_path][i]  =
				rtlefuse->eeprom_chnlarea_txpwr_cck
							[rf_path][index];
			rtlefuse->txpwrlevel_ht40_1s[rf_path][i]  =
				rtlefuse->eeprom_chnlarea_txpwr_ht40_1s
							[rf_path][index];
			rtlefuse->txpwrlevel_ht40_2s[rf_path][i]  =
				rtlefuse->eprom_chnl_txpwr_ht40_2sdf
							[rf_path][index];
		}

		for (i = 0; i < 14; i++) {
			RTPRINT(rtlpriv, FINIT, INIT_TXPOWER,
				"RF(%d)-Ch(%d) [CCK / HT40_1S / HT40_2S] = [0x%x / 0x%x / 0x%x]\n",
				rf_path, i,
				rtlefuse->txpwrlevel_cck[rf_path][i],
				rtlefuse->txpwrlevel_ht40_1s[rf_path][i],
				rtlefuse->txpwrlevel_ht40_2s[rf_path][i]);
		}
	}

	for (rf_path = 0; rf_path < 2; rf_path++) {
		for (i = 0; i < 3; i++) {
			/* Read Power diff limit. */
			rtlefuse->eeprom_pwrgroup[rf_path][i] =
				hwinfo[EEPROM_TXPWRGROUP + rf_path * 3 + i];
		}
	}

	for (rf_path = 0; rf_path < 2; rf_path++) {
		/* Fill Pwr group */
		for (i = 0; i < 14; i++) {
			/* Chanel 1-3 */
			if (i < 3)
				index = 0;
			/* Channel 4-8 */
			else if (i < 8)
				index = 1;
			/* Channel 9-13 */
			else
				index = 2;

			rtlefuse->pwrgroup_ht20[rf_path][i] =
				(rtlefuse->eeprom_pwrgroup[rf_path][index] &
				0xf);
			rtlefuse->pwrgroup_ht40[rf_path][i] =
				((rtlefuse->eeprom_pwrgroup[rf_path][index] &
				0xf0) >> 4);

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
		/* Read tx power difference between HT OFDM 20/40 MHZ */
		/* channel 1-3 */
		if (i < 3)
			index = 0;
		/* Channel 4-8 */
		else if (i < 8)
			index = 1;
		/* Channel 9-14 */
		else
			index = 2;

		tempval = hwinfo[EEPROM_TX_PWR_HT20_DIFF + index] & 0xff;
		rtlefuse->txpwr_ht20diff[RF90_PATH_A][i] = (tempval & 0xF);
		rtlefuse->txpwr_ht20diff[RF90_PATH_B][i] =
						 ((tempval >> 4) & 0xF);

		/* Read OFDM<->HT tx power diff */
		/* Channel 1-3 */
		if (i < 3)
			index = 0;
		/* Channel 4-8 */
		else if (i < 8)
			index = 0x11;
		/* Channel 9-14 */
		else
			index = 1;

		tempval = hwinfo[EEPROM_TX_PWR_OFDM_DIFF + index] & 0xff;
		rtlefuse->txpwr_legacyhtdiff[RF90_PATH_A][i] =
				 (tempval & 0xF);
		rtlefuse->txpwr_legacyhtdiff[RF90_PATH_B][i] =
				 ((tempval >> 4) & 0xF);

		tempval = hwinfo[TX_PWR_SAFETY_CHK];
		rtlefuse->txpwr_safetyflag = (tempval & 0x01);
	}

	rtlefuse->eeprom_regulatory = 0;
	if (rtlefuse->eeprom_version >= 2) {
		/* BIT(0)~2 */
		if (rtlefuse->eeprom_version >= 4)
			rtlefuse->eeprom_regulatory =
				 (hwinfo[EEPROM_REGULATORY] & 0x7);
		else /* BIT(0) */
			rtlefuse->eeprom_regulatory =
				 (hwinfo[EEPROM_REGULATORY] & 0x1);
	}
	RTPRINT(rtlpriv, FINIT, INIT_TXPOWER,
		"eeprom_regulatory = 0x%x\n", rtlefuse->eeprom_regulatory);

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

	RTPRINT(rtlpriv, FINIT, INIT_TXPOWER,
		"TxPwrSafetyFlag = %d\n", rtlefuse->txpwr_safetyflag);

	/* Read RF-indication and Tx Power gain
	 * index diff of legacy to HT OFDM rate. */
	tempval = hwinfo[EEPROM_RFIND_POWERDIFF] & 0xff;
	rtlefuse->eeprom_txpowerdiff = tempval;
	rtlefuse->legacy_httxpowerdiff =
		rtlefuse->txpwr_legacyhtdiff[RF90_PATH_A][0];

	RTPRINT(rtlpriv, FINIT, INIT_TXPOWER,
		"TxPowerDiff = %#x\n", rtlefuse->eeprom_txpowerdiff);

	/* Get TSSI value for each path. */
	usvalue = *(u16 *)&hwinfo[EEPROM_TSSI_A];
	rtlefuse->eeprom_tssi[RF90_PATH_A] = (u8)((usvalue & 0xff00) >> 8);
	usvalue = hwinfo[EEPROM_TSSI_B];
	rtlefuse->eeprom_tssi[RF90_PATH_B] = (u8)(usvalue & 0xff);

	RTPRINT(rtlpriv, FINIT, INIT_TXPOWER, "TSSI_A = 0x%x, TSSI_B = 0x%x\n",
		rtlefuse->eeprom_tssi[RF90_PATH_A],
		rtlefuse->eeprom_tssi[RF90_PATH_B]);

	/* Read antenna tx power offset of B/C/D to A  from EEPROM */
	/* and read ThermalMeter from EEPROM */
	tempval = hwinfo[EEPROM_THERMALMETER];
	rtlefuse->eeprom_thermalmeter = tempval;
	RTPRINT(rtlpriv, FINIT, INIT_TXPOWER,
		"thermalmeter = 0x%x\n", rtlefuse->eeprom_thermalmeter);

	/* ThermalMeter, BIT(0)~3 for RFIC1, BIT(4)~7 for RFIC2 */
	rtlefuse->thermalmeter[0] = (rtlefuse->eeprom_thermalmeter & 0x1f);
	rtlefuse->tssi_13dbm = rtlefuse->eeprom_thermalmeter * 100;

	/* Read CrystalCap from EEPROM */
	tempval = hwinfo[EEPROM_CRYSTALCAP] >> 4;
	rtlefuse->eeprom_crystalcap = tempval;
	/* CrystalCap, BIT(12)~15 */
	rtlefuse->crystalcap = rtlefuse->eeprom_crystalcap;

	/* Read IC Version && Channel Plan */
	/* Version ID, Channel plan */
	rtlefuse->eeprom_channelplan = hwinfo[EEPROM_CHANNELPLAN];
	rtlefuse->txpwr_fromeprom = true;
	RTPRINT(rtlpriv, FINIT, INIT_TXPOWER,
		"EEPROM ChannelPlan = 0x%4x\n", rtlefuse->eeprom_channelplan);

	/* Read Customer ID or Board Type!!! */
	tempval = hwinfo[EEPROM_BOARDTYPE];
	/* Change RF type definition */
	if (tempval == 0)
		rtlphy->rf_type = RF_2T2R;
	else if (tempval == 1)
		rtlphy->rf_type = RF_1T2R;
	else if (tempval == 2)
		rtlphy->rf_type = RF_1T2R;
	else if (tempval == 3)
		rtlphy->rf_type = RF_1T1R;

	/* 1T2R but 1SS (1x1 receive combining) */
	rtlefuse->b1x1_recvcombine = false;
	if (rtlphy->rf_type == RF_1T2R) {
		tempval = rtl_read_byte(rtlpriv, 0x07);
		if (!(tempval & BIT(0))) {
			rtlefuse->b1x1_recvcombine = true;
			RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
				 "RF_TYPE=1T2R but only 1SS\n");
		}
	}
	rtlefuse->b1ss_support = rtlefuse->b1x1_recvcombine;
	rtlefuse->eeprom_oemid = *&hwinfo[EEPROM_CUSTOMID];

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "EEPROM Customer ID: 0x%2x\n",
		 rtlefuse->eeprom_oemid);

	/* set channel paln to world wide 13 */
	rtlefuse->channel_plan = COUNTRY_CODE_WORLD_WIDE_13;
}

void rtl92se_read_eeprom_info(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u8 tmp_u1b = 0;

	tmp_u1b = rtl_read_byte(rtlpriv, EPROM_CMD);

	if (tmp_u1b & BIT(4)) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "Boot from EEPROM\n");
		rtlefuse->epromtype = EEPROM_93C46;
	} else {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "Boot from EFUSE\n");
		rtlefuse->epromtype = EEPROM_BOOT_EFUSE;
	}

	if (tmp_u1b & BIT(5)) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "Autoload OK\n");
		rtlefuse->autoload_failflag = false;
		_rtl92se_read_adapter_info(hw);
	} else {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "Autoload ERR!!\n");
		rtlefuse->autoload_failflag = true;
	}
}

static void rtl92se_update_hal_rate_table(struct ieee80211_hw *hw,
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
	u16 shortgi_rate = 0;
	u32 tmp_ratr_value = 0;
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
		ratr_value &= 0x0000000D;
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
				if (curtxbw_40mhz)
					ratr_mask = 0x000ff015;
				else
					ratr_mask = 0x000ff005;
			} else {
				if (curtxbw_40mhz)
					ratr_mask = 0x0f0ff015;
				else
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

	if (rtlpriv->rtlhal.version >= VERSION_8192S_BCUT)
		ratr_value &= 0x0FFFFFFF;
	else if (rtlpriv->rtlhal.version == VERSION_8192S_ACUT)
		ratr_value &= 0x0FFFFFF0;

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

		rtl_write_byte(rtlpriv, SG_RATE, shortgi_rate);
	}

	rtl_write_dword(rtlpriv, ARFR0 + ratr_index * 4, ratr_value);
	if (ratr_value & 0xfffff000)
		rtl92s_phy_set_fw_cmd(hw, FW_CMD_RA_REFRESH_N);
	else
		rtl92s_phy_set_fw_cmd(hw, FW_CMD_RA_REFRESH_BG);

	RT_TRACE(rtlpriv, COMP_RATR, DBG_DMESG, "%x\n",
		 rtl_read_dword(rtlpriv, ARFR0));
}

static void rtl92se_update_hal_rate_mask(struct ieee80211_hw *hw,
					 struct ieee80211_sta *sta,
					 u8 rssi_level)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_sta_info *sta_entry = NULL;
	u32 ratr_bitmap;
	u8 ratr_index = 0;
	u8 curtxbw_40mhz = (sta->bandwidth >= IEEE80211_STA_RX_BW_40) ? 1 : 0;
	u8 curshortgi_40mhz = (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_40) ?
				1 : 0;
	u8 curshortgi_20mhz = (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_20) ?
				1 : 0;
	enum wireless_mode wirelessmode = 0;
	bool shortgi = false;
	u32 ratr_value = 0;
	u8 shortgi_rate = 0;
	u32 mask = 0;
	u32 band = 0;
	bool bmulticast = false;
	u8 macid = 0;
	u8 mimo_ps = IEEE80211_SMPS_OFF;

	sta_entry = (struct rtl_sta_info *) sta->drv_priv;
	wirelessmode = sta_entry->wireless_mode;
	if (mac->opmode == NL80211_IFTYPE_STATION)
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
		band |= WIRELESS_11B;
		ratr_index = RATR_INX_WIRELESS_B;
		if (ratr_bitmap & 0x0000000c)
			ratr_bitmap &= 0x0000000d;
		else
			ratr_bitmap &= 0x0000000f;
		break;
	case WIRELESS_MODE_G:
		band |= (WIRELESS_11G | WIRELESS_11B);
		ratr_index = RATR_INX_WIRELESS_GB;

		if (rssi_level == 1)
			ratr_bitmap &= 0x00000f00;
		else if (rssi_level == 2)
			ratr_bitmap &= 0x00000ff0;
		else
			ratr_bitmap &= 0x00000ff5;
		break;
	case WIRELESS_MODE_A:
		band |= WIRELESS_11A;
		ratr_index = RATR_INX_WIRELESS_A;
		ratr_bitmap &= 0x00000ff0;
		break;
	case WIRELESS_MODE_N_24G:
	case WIRELESS_MODE_N_5G:
		band |= (WIRELESS_11N | WIRELESS_11G | WIRELESS_11B);
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
				if (rssi_level == 1) {
						ratr_bitmap &= 0x000f0000;
				} else if (rssi_level == 3) {
					ratr_bitmap &= 0x000fc000;
				} else if (rssi_level == 5) {
						ratr_bitmap &= 0x000ff000;
				} else {
					if (curtxbw_40mhz)
						ratr_bitmap &= 0x000ff015;
					else
						ratr_bitmap &= 0x000ff005;
				}
			} else {
				if (rssi_level == 1) {
					ratr_bitmap &= 0x0f8f0000;
				} else if (rssi_level == 3) {
					ratr_bitmap &= 0x0f8fc000;
				} else if (rssi_level == 5) {
					ratr_bitmap &= 0x0f8ff000;
				} else {
					if (curtxbw_40mhz)
						ratr_bitmap &= 0x0f8ff015;
					else
						ratr_bitmap &= 0x0f8ff005;
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
		band |= (WIRELESS_11N | WIRELESS_11G | WIRELESS_11B);
		ratr_index = RATR_INX_WIRELESS_NGB;

		if (rtlphy->rf_type == RF_1T2R)
			ratr_bitmap &= 0x000ff0ff;
		else
			ratr_bitmap &= 0x0f8ff0ff;
		break;
	}
	sta_entry->ratr_index = ratr_index;

	if (rtlpriv->rtlhal.version >= VERSION_8192S_BCUT)
		ratr_bitmap &= 0x0FFFFFFF;
	else if (rtlpriv->rtlhal.version == VERSION_8192S_ACUT)
		ratr_bitmap &= 0x0FFFFFF0;

	if (shortgi) {
		ratr_bitmap |= 0x10000000;
		/* Get MAX MCS available. */
		ratr_value = (ratr_bitmap >> 12);
		for (shortgi_rate = 15; shortgi_rate > 0; shortgi_rate--) {
			if ((1 << shortgi_rate) & ratr_value)
				break;
		}

		shortgi_rate = (shortgi_rate << 12) | (shortgi_rate << 8) |
			(shortgi_rate << 4) | (shortgi_rate);
		rtl_write_byte(rtlpriv, SG_RATE, shortgi_rate);
	}

	mask |= (bmulticast ? 1 : 0) << 9 | (macid & 0x1f) << 4 | (band & 0xf);

	RT_TRACE(rtlpriv, COMP_RATR, DBG_TRACE, "mask = %x, bitmap = %x\n",
		 mask, ratr_bitmap);
	rtl_write_dword(rtlpriv, 0x2c4, ratr_bitmap);
	rtl_write_dword(rtlpriv, WFM5, (FW_RA_UPDATE_MASK | (mask << 8)));

	if (macid != 0)
		sta_entry->ratr_index = ratr_index;
}

void rtl92se_update_hal_rate_tbl(struct ieee80211_hw *hw,
		struct ieee80211_sta *sta, u8 rssi_level)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (rtlpriv->dm.useramask)
		rtl92se_update_hal_rate_mask(hw, sta, rssi_level);
	else
		rtl92se_update_hal_rate_table(hw, sta);
}

void rtl92se_update_channel_access_setting(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u16 sifs_timer;

	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_SLOT_TIME,
				      &mac->slot_time);
	sifs_timer = 0x0e0e;
	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_SIFS, (u8 *)&sifs_timer);

}

/* this ifunction is for RFKILL, it's different with windows,
 * because UI will disable wireless when GPIO Radio Off.
 * And here we not check or Disable/Enable ASPM like windows*/
bool rtl92se_gpio_radio_on_off_checking(struct ieee80211_hw *hw, u8 *valid)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	enum rf_pwrstate rfpwr_toset /*, cur_rfstate */;
	unsigned long flag = 0;
	bool actuallyset = false;
	bool turnonbypowerdomain = false;

	/* just 8191se can check gpio before firstup, 92c/92d have fixed it */
	if ((rtlpci->up_first_time == 1) || (rtlpci->being_init_adapter))
		return false;

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

	/* cur_rfstate = ppsc->rfpwr_state;*/

	/* because after _rtl92s_phy_set_rfhalt, all power
	 * closed, so we must open some power for GPIO check,
	 * or we will always check GPIO RFOFF here,
	 * And we should close power after GPIO check */
	if (RT_IN_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC)) {
		_rtl92se_power_domain_init(hw);
		turnonbypowerdomain = true;
	}

	rfpwr_toset = _rtl92se_rf_onoff_detect(hw);

	if ((ppsc->hwradiooff) && (rfpwr_toset == ERFON)) {
		RT_TRACE(rtlpriv, COMP_RF, DBG_DMESG,
			 "RFKILL-HW Radio ON, RF ON\n");

		rfpwr_toset = ERFON;
		ppsc->hwradiooff = false;
		actuallyset = true;
	} else if ((!ppsc->hwradiooff) && (rfpwr_toset == ERFOFF)) {
		RT_TRACE(rtlpriv, COMP_RF,
			 DBG_DMESG, "RFKILL-HW Radio OFF, RF OFF\n");

		rfpwr_toset = ERFOFF;
		ppsc->hwradiooff = true;
		actuallyset = true;
	}

	if (actuallyset) {
		spin_lock_irqsave(&rtlpriv->locks.rf_ps_lock, flag);
		ppsc->rfchange_inprogress = false;
		spin_unlock_irqrestore(&rtlpriv->locks.rf_ps_lock, flag);

	/* this not include ifconfig wlan0 down case */
	/* } else if (rfpwr_toset == ERFOFF || cur_rfstate == ERFOFF) { */
	} else {
		/* because power_domain_init may be happen when
		 * _rtl92s_phy_set_rfhalt, this will open some powers
		 * and cause current increasing about 40 mA for ips,
		 * rfoff and ifconfig down, so we set
		 * _rtl92s_phy_set_rfhalt again here */
		if (ppsc->reg_rfps_level & RT_RF_OFF_LEVL_HALT_NIC &&
			turnonbypowerdomain) {
			_rtl92s_phy_set_rfhalt(hw);
			RT_SET_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC);
		}

		spin_lock_irqsave(&rtlpriv->locks.rf_ps_lock, flag);
		ppsc->rfchange_inprogress = false;
		spin_unlock_irqrestore(&rtlpriv->locks.rf_ps_lock, flag);
	}

	*valid = 1;
	return !ppsc->hwradiooff;

}

/* Is_wepkey just used for WEP used as group & pairwise key
 * if pairwise is AES ang group is WEP Is_wepkey == false.*/
void rtl92se_set_key(struct ieee80211_hw *hw, u32 key_index, u8 *p_macaddr,
	bool is_group, u8 enc_algo, bool is_wepkey, bool clear_all)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u8 *macaddr = p_macaddr;

	u32 entry_id = 0;
	bool is_pairwise = false;

	static u8 cam_const_addr[4][6] = {
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x01},
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x02},
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x03}
	};
	static u8 cam_const_broad[] = {
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	};

	if (clear_all) {
		u8 idx = 0;
		u8 cam_offset = 0;
		u8 clear_number = 5;

		RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG, "clear_all\n");

		for (idx = 0; idx < clear_number; idx++) {
			rtl_cam_mark_invalid(hw, cam_offset + idx);
			rtl_cam_empty_entry(hw, cam_offset + idx);

			if (idx < 5) {
				memset(rtlpriv->sec.key_buf[idx], 0,
				       MAX_KEY_LEN);
				rtlpriv->sec.key_len[idx] = 0;
			}
		}

	} else {
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
			RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
				 "switch case %#x not processed\n", enc_algo);
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
					entry_id = rtl_cam_get_free_entry(hw,
								 p_macaddr);
					if (entry_id >=  TOTAL_CAM_ENTRY) {
						RT_TRACE(rtlpriv,
							 COMP_SEC, DBG_EMERG,
							 "Can not find free hw security cam entry\n");
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
			RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG,
				 "delete one entry, entry_id is %d\n",
				 entry_id);
			if (mac->opmode == NL80211_IFTYPE_AP)
				rtl_cam_del_entry(hw, p_macaddr);
			rtl_cam_delete_one_entry(hw, p_macaddr, entry_id);
		} else {
			RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG,
				 "add one entry\n");
			if (is_pairwise) {
				RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG,
					 "set Pairwise key\n");

				rtl_cam_add_one_entry(hw, macaddr, key_index,
					entry_id, enc_algo,
					CAM_CONFIG_NO_USEDK,
					rtlpriv->sec.key_buf[key_index]);
			} else {
				RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG,
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
					      rtlpriv->sec.key_buf[entry_id]);
			}

		}
	}
}

void rtl92se_suspend(struct ieee80211_hw *hw)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	rtlpci->up_first_time = true;
}

void rtl92se_resume(struct ieee80211_hw *hw)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u32 val;

	pci_read_config_dword(rtlpci->pdev, 0x40, &val);
	if ((val & 0x0000ff00) != 0)
		pci_write_config_dword(rtlpci->pdev, 0x40,
			val & 0xffff00ff);
}
