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
#include "../rtl8723com/dm_common.h"
#include "fw.h"
#include "../rtl8723com/fw_common.h"
#include "led.h"
#include "hw.h"
#include "pwrseqcmd.h"
#include "pwrseq.h"
#include "btc.h"

static void _rtl8723ae_set_bcn_ctrl_reg(struct ieee80211_hw *hw,
					u8 set_bits, u8 clear_bits)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpci->reg_bcn_ctrl_val |= set_bits;
	rtlpci->reg_bcn_ctrl_val &= ~clear_bits;

	rtl_write_byte(rtlpriv, REG_BCN_CTRL, (u8) rtlpci->reg_bcn_ctrl_val);
}

static void _rtl8723ae_stop_tx_beacon(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 tmp1byte;

	tmp1byte = rtl_read_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2);
	rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2, tmp1byte & (~BIT(6)));
	rtl_write_byte(rtlpriv, REG_TBTT_PROHIBIT + 1, 0x64);
	tmp1byte = rtl_read_byte(rtlpriv, REG_TBTT_PROHIBIT + 2);
	tmp1byte &= ~(BIT(0));
	rtl_write_byte(rtlpriv, REG_TBTT_PROHIBIT + 2, tmp1byte);
}

static void _rtl8723ae_resume_tx_beacon(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 tmp1byte;

	tmp1byte = rtl_read_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2);
	rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2, tmp1byte | BIT(6));
	rtl_write_byte(rtlpriv, REG_TBTT_PROHIBIT + 1, 0xff);
	tmp1byte = rtl_read_byte(rtlpriv, REG_TBTT_PROHIBIT + 2);
	tmp1byte |= BIT(1);
	rtl_write_byte(rtlpriv, REG_TBTT_PROHIBIT + 2, tmp1byte);
}

static void _rtl8723ae_enable_bcn_sufunc(struct ieee80211_hw *hw)
{
	_rtl8723ae_set_bcn_ctrl_reg(hw, 0, BIT(1));
}

static void _rtl8723ae_disable_bcn_sufunc(struct ieee80211_hw *hw)
{
	_rtl8723ae_set_bcn_ctrl_reg(hw, BIT(1), 0);
}

void rtl8723ae_get_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	switch (variable) {
	case HW_VAR_RCR:
		*((u32 *) (val)) = rtlpci->receive_config;
		break;
	case HW_VAR_RF_STATE:
		*((enum rf_pwrstate *)(val)) = ppsc->rfpwr_state;
		break;
	case HW_VAR_FWLPS_RF_ON:{
		enum rf_pwrstate rfState;
		u32 val_rcr;

		rtlpriv->cfg->ops->get_hw_reg(hw,
					      HW_VAR_RF_STATE,
					      (u8 *) (&rfState));
		if (rfState == ERFOFF) {
			*((bool *) (val)) = true;
		} else {
			val_rcr = rtl_read_dword(rtlpriv, REG_RCR);
			val_rcr &= 0x00070000;
			if (val_rcr)
				*((bool *) (val)) = false;
			else
				*((bool *) (val)) = true;
		}
		break; }
	case HW_VAR_FW_PSMODE_STATUS:
		*((bool *) (val)) = ppsc->fw_current_inpsmode;
		break;
	case HW_VAR_CORRECT_TSF:{
		u64 tsf;
		u32 *ptsf_low = (u32 *)&tsf;
		u32 *ptsf_high = ((u32 *)&tsf) + 1;

		*ptsf_high = rtl_read_dword(rtlpriv, (REG_TSFTR + 4));
		*ptsf_low = rtl_read_dword(rtlpriv, REG_TSFTR);

		*((u64 *) (val)) = tsf;

		break; }
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "switch case not process\n");
		break;
	}
}

void rtl8723ae_set_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
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
	case HW_VAR_BASIC_RATE:{
		u16 rate_cfg = ((u16 *) val)[0];
		u8 rate_index = 0;
		rate_cfg = rate_cfg & 0x15f;
		rate_cfg |= 0x01;
		rtl_write_byte(rtlpriv, REG_RRSR, rate_cfg & 0xff);
		rtl_write_byte(rtlpriv, REG_RRSR + 1,
			       (rate_cfg >> 8) & 0xff);
		while (rate_cfg > 0x1) {
			rate_cfg = (rate_cfg >> 1);
			rate_index++;
		}
		rtl_write_byte(rtlpriv, REG_INIRTS_RATE_SEL,
			       rate_index);
		break; }
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
				       *((u16 *) val));
		break;
	case HW_VAR_SLOT_TIME:{
		u8 e_aci;

		RT_TRACE(rtlpriv, COMP_MLME, DBG_LOUD,
			 "HW_VAR_SLOT_TIME %x\n", val[0]);

		rtl_write_byte(rtlpriv, REG_SLOT, val[0]);

		for (e_aci = 0; e_aci < AC_MAX; e_aci++) {
			rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_AC_PARAM,
						      &e_aci);
		}
		break; }
	case HW_VAR_ACK_PREAMBLE:{
		u8 reg_tmp;
		u8 short_preamble = (bool)*val;
		reg_tmp = (mac->cur_40_prime_sc) << 5;
		if (short_preamble)
			reg_tmp |= 0x80;

		rtl_write_byte(rtlpriv, REG_RRSR + 2, reg_tmp);
		break; }
	case HW_VAR_AMPDU_MIN_SPACE:{
		u8 min_spacing_to_set;
		u8 sec_min_space;

		min_spacing_to_set = *val;
		if (min_spacing_to_set <= 7) {
			sec_min_space = 0;

			if (min_spacing_to_set < sec_min_space)
				min_spacing_to_set = sec_min_space;

			mac->min_space_cfg = ((mac->min_space_cfg &
					       0xf8) |
					      min_spacing_to_set);

			*val = min_spacing_to_set;

			RT_TRACE(rtlpriv, COMP_MLME, DBG_LOUD,
				 "Set HW_VAR_AMPDU_MIN_SPACE: %#x\n",
				  mac->min_space_cfg);

			rtl_write_byte(rtlpriv, REG_AMPDU_MIN_SPACE,
				       mac->min_space_cfg);
		}
		break; }
	case HW_VAR_SHORTGI_DENSITY:{
		u8 density_to_set;

		density_to_set = *val;
		mac->min_space_cfg |= (density_to_set << 3);

		RT_TRACE(rtlpriv, COMP_MLME, DBG_LOUD,
			 "Set HW_VAR_SHORTGI_DENSITY: %#x\n",
			 mac->min_space_cfg);

		rtl_write_byte(rtlpriv, REG_AMPDU_MIN_SPACE,
			       mac->min_space_cfg);

		break; }
	case HW_VAR_AMPDU_FACTOR:{
		u8 regtoset_normal[4] = {0x41, 0xa8, 0x72, 0xb9};
		u8 regtoset_bt[4] = {0x31, 0x74, 0x42, 0x97};
		u8 factor_toset;
		u8 *p_regtoset = NULL;
		u8 index;

		if ((pcipriv->bt_coexist.bt_coexistence) &&
		    (pcipriv->bt_coexist.bt_coexist_type == BT_CSR_BC4))
			p_regtoset = regtoset_bt;
		else
			p_regtoset = regtoset_normal;

		factor_toset = *val;
		if (factor_toset <= 3) {
			factor_toset = (1 << (factor_toset + 2));
			if (factor_toset > 0xf)
				factor_toset = 0xf;

			for (index = 0; index < 4; index++) {
				if ((p_regtoset[index] & 0xf0) >
				    (factor_toset << 4))
					p_regtoset[index] =
					    (p_regtoset[index] & 0x0f) |
					    (factor_toset << 4);

				if ((p_regtoset[index] & 0x0f) >
				    factor_toset)
					p_regtoset[index] =
					    (p_regtoset[index] & 0xf0) |
					    (factor_toset);

				rtl_write_byte(rtlpriv,
					       (REG_AGGLEN_LMT + index),
					       p_regtoset[index]);

			}

			RT_TRACE(rtlpriv, COMP_MLME, DBG_LOUD,
				 "Set HW_VAR_AMPDU_FACTOR: %#x\n",
				 factor_toset);
		}
		break; }
	case HW_VAR_AC_PARAM:{
		u8 e_aci = *val;
		rtl8723_dm_init_edca_turbo(hw);

		if (rtlpci->acm_method != EACMWAY2_SW)
			rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_ACM_CTRL,
						      &e_aci);
		break; }
	case HW_VAR_ACM_CTRL:{
		u8 e_aci = *val;
		union aci_aifsn *p_aci_aifsn =
		    (union aci_aifsn *)(&(mac->ac[0].aifs));
		u8 acm = p_aci_aifsn->f.acm;
		u8 acm_ctrl = rtl_read_byte(rtlpriv, REG_ACMHWCTRL);

		acm_ctrl |= ((rtlpci->acm_method == 2) ? 0x0 : 0x1);

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
				acm_ctrl &= (~AcmHw_BeqEn);
				break;
			default:
				RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
					 "switch case not processed\n");
				break;
			}
		}

		RT_TRACE(rtlpriv, COMP_QOS, DBG_TRACE,
			 "SetHwReg8190pci(): [HW_VAR_ACM_CTRL] Write 0x%X\n",
			 acm_ctrl);
		rtl_write_byte(rtlpriv, REG_ACMHWCTRL, acm_ctrl);
		break; }
	case HW_VAR_RCR:
		rtl_write_dword(rtlpriv, REG_RCR, ((u32 *) (val))[0]);
		rtlpci->receive_config = ((u32 *) (val))[0];
		break;
	case HW_VAR_RETRY_LIMIT:{
		u8 retry_limit = *val;

		rtl_write_word(rtlpriv, REG_RL,
			       retry_limit << RETRY_LIMIT_SHORT_SHIFT |
			       retry_limit << RETRY_LIMIT_LONG_SHIFT);
		break; }
	case HW_VAR_DUAL_TSF_RST:
		rtl_write_byte(rtlpriv, REG_DUAL_TSF_RST, (BIT(0) | BIT(1)));
		break;
	case HW_VAR_EFUSE_BYTES:
		rtlefuse->efuse_usedbytes = *((u16 *) val);
		break;
	case HW_VAR_EFUSE_USAGE:
		rtlefuse->efuse_usedpercentage = *val;
		break;
	case HW_VAR_IO_CMD:
		rtl8723ae_phy_set_io_cmd(hw, (*(enum io_type *)val));
		break;
	case HW_VAR_WPA_CONFIG:
		rtl_write_byte(rtlpriv, REG_SECCFG, *val);
		break;
	case HW_VAR_SET_RPWM:{
		u8 rpwm_val;

		rpwm_val = rtl_read_byte(rtlpriv, REG_PCIE_HRPWM);
		udelay(1);

		if (rpwm_val & BIT(7)) {
			rtl_write_byte(rtlpriv, REG_PCIE_HRPWM, *val);
		} else {
			rtl_write_byte(rtlpriv, REG_PCIE_HRPWM, *val | BIT(7));
		}

		break; }
	case HW_VAR_H2C_FW_PWRMODE:{
		u8 psmode = *val;

		if (psmode != FW_PS_ACTIVE_MODE)
			rtl8723ae_dm_rf_saving(hw, true);

		rtl8723ae_set_fw_pwrmode_cmd(hw, *val);
		break; }
	case HW_VAR_FW_PSMODE_STATUS:
		ppsc->fw_current_inpsmode = *((bool *) val);
		break;
	case HW_VAR_H2C_FW_JOINBSSRPT:{
		u8 mstatus = *val;
		u8 tmp_regcr, tmp_reg422;
		bool recover = false;

		if (mstatus == RT_MEDIA_CONNECT) {
			rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_AID, NULL);

			tmp_regcr = rtl_read_byte(rtlpriv, REG_CR + 1);
			rtl_write_byte(rtlpriv, REG_CR + 1,
				       (tmp_regcr | BIT(0)));

			_rtl8723ae_set_bcn_ctrl_reg(hw, 0, BIT(3));
			_rtl8723ae_set_bcn_ctrl_reg(hw, BIT(4), 0);

			tmp_reg422 = rtl_read_byte(rtlpriv,
				     REG_FWHW_TXQ_CTRL + 2);
			if (tmp_reg422 & BIT(6))
				recover = true;
			rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2,
				       tmp_reg422 & (~BIT(6)));

			rtl8723ae_set_fw_rsvdpagepkt(hw, 0);

			_rtl8723ae_set_bcn_ctrl_reg(hw, BIT(3), 0);
			_rtl8723ae_set_bcn_ctrl_reg(hw, 0, BIT(4));

			if (recover)
				rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2,
					       tmp_reg422);

			rtl_write_byte(rtlpriv, REG_CR + 1,
				       (tmp_regcr & ~(BIT(0))));
		}
		rtl8723ae_set_fw_joinbss_report_cmd(hw, *val);

		break; }
	case HW_VAR_H2C_FW_P2P_PS_OFFLOAD:
		rtl8723ae_set_p2p_ps_offload_cmd(hw, *val);
		break;
	case HW_VAR_AID:{
		u16 u2btmp;
		u2btmp = rtl_read_word(rtlpriv, REG_BCN_PSR_RPT);
		u2btmp &= 0xC000;
		rtl_write_word(rtlpriv, REG_BCN_PSR_RPT, (u2btmp |
				mac->assoc_id));
		break; }
	case HW_VAR_CORRECT_TSF:{
		u8 btype_ibss = *val;

		if (btype_ibss == true)
			_rtl8723ae_stop_tx_beacon(hw);

		_rtl8723ae_set_bcn_ctrl_reg(hw, 0, BIT(3));

		rtl_write_dword(rtlpriv, REG_TSFTR,
				(u32) (mac->tsf & 0xffffffff));
		rtl_write_dword(rtlpriv, REG_TSFTR + 4,
				(u32) ((mac->tsf >> 32) & 0xffffffff));

		_rtl8723ae_set_bcn_ctrl_reg(hw, BIT(3), 0);

		if (btype_ibss == true)
			_rtl8723ae_resume_tx_beacon(hw);
		break; }
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
			 "switch case not processed\n");
		break;
	}
}

static bool _rtl8723ae_llt_write(struct ieee80211_hw *hw, u32 address, u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	bool status = true;
	long count = 0;
	u32 value = _LLT_INIT_ADDR(address) |
	    _LLT_INIT_DATA(data) | _LLT_OP(_LLT_WRITE_ACCESS);

	rtl_write_dword(rtlpriv, REG_LLT_INIT, value);

	do {
		value = rtl_read_dword(rtlpriv, REG_LLT_INIT);
		if (_LLT_NO_ACTIVE == _LLT_OP_VALUE(value))
			break;

		if (count > POLLING_LLT_THRESHOLD) {
			RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
				 "Failed to polling write LLT done at address %d!\n",
				 address);
			status = false;
			break;
		}
	} while (++count);

	return status;
}

static bool _rtl8723ae_llt_table_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	unsigned short i;
	u8 txpktbuf_bndy;
	u8 maxPage;
	bool status;
	u8 ubyte;

	maxPage = 255;
	txpktbuf_bndy = 246;

	rtl_write_byte(rtlpriv, REG_CR, 0x8B);

	rtl_write_word(rtlpriv, REG_RQPN_NPQ, 0x0000);

	rtl_write_dword(rtlpriv, REG_RQPN, 0x80ac1c29);
	rtl_write_byte(rtlpriv, REG_RQPN_NPQ, 0x03);

	rtl_write_dword(rtlpriv, REG_TRXFF_BNDY, (0x27FF0000 | txpktbuf_bndy));
	rtl_write_byte(rtlpriv, REG_TDECTRL + 1, txpktbuf_bndy);

	rtl_write_byte(rtlpriv, REG_TXPKTBUF_BCNQ_BDNY, txpktbuf_bndy);
	rtl_write_byte(rtlpriv, REG_TXPKTBUF_MGQ_BDNY, txpktbuf_bndy);

	rtl_write_byte(rtlpriv, 0x45D, txpktbuf_bndy);
	rtl_write_byte(rtlpriv, REG_PBP, 0x11);
	rtl_write_byte(rtlpriv, REG_RX_DRVINFO_SZ, 0x4);

	for (i = 0; i < (txpktbuf_bndy - 1); i++) {
		status = _rtl8723ae_llt_write(hw, i, i + 1);
		if (true != status)
			return status;
	}

	status = _rtl8723ae_llt_write(hw, (txpktbuf_bndy - 1), 0xFF);
	if (true != status)
		return status;

	for (i = txpktbuf_bndy; i < maxPage; i++) {
		status = _rtl8723ae_llt_write(hw, i, (i + 1));
		if (true != status)
			return status;
	}

	status = _rtl8723ae_llt_write(hw, maxPage, txpktbuf_bndy);
	if (true != status)
		return status;

	rtl_write_byte(rtlpriv, REG_CR, 0xff);
	ubyte = rtl_read_byte(rtlpriv, REG_RQPN + 3);
	rtl_write_byte(rtlpriv, REG_RQPN + 3, ubyte | BIT(7));

	return true;
}

static void _rtl8723ae_gen_refresh_led_state(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_led *pLed0 = &(pcipriv->ledctl.sw_led0);

	if (rtlpriv->rtlhal.up_first_time)
		return;

	if (ppsc->rfoff_reason == RF_CHANGE_BY_IPS)
		rtl8723ae_sw_led_on(hw, pLed0);
	else if (ppsc->rfoff_reason == RF_CHANGE_BY_INIT)
		rtl8723ae_sw_led_on(hw, pLed0);
	else
		rtl8723ae_sw_led_off(hw, pLed0);
}

static bool _rtl8712e_init_mac(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	unsigned char bytetmp;
	unsigned short wordtmp;
	u16 retry = 0;
	u16 tmpu2b;
	bool mac_func_enable;

	rtl_write_byte(rtlpriv, REG_RSV_CTRL, 0x00);
	bytetmp = rtl_read_byte(rtlpriv, REG_CR);
	if (bytetmp == 0xFF)
		mac_func_enable = true;
	else
		mac_func_enable = false;


	/* HW Power on sequence */
	if (!rtl_hal_pwrseqcmdparsing(rtlpriv, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK,
		PWR_INTF_PCI_MSK, Rtl8723_NIC_ENABLE_FLOW))
		return false;

	bytetmp = rtl_read_byte(rtlpriv, REG_PCIE_CTRL_REG+2);
	rtl_write_byte(rtlpriv, REG_PCIE_CTRL_REG+2, bytetmp | BIT(4));

	/* eMAC time out function enable, 0x369[7]=1 */
	bytetmp = rtl_read_byte(rtlpriv, 0x369);
	rtl_write_byte(rtlpriv, 0x369, bytetmp | BIT(7));

	/* ePHY reg 0x1e bit[4]=1 using MDIO interface,
	 * we should do this before Enabling ASPM backdoor.
	 */
	do {
		rtl_write_word(rtlpriv, 0x358, 0x5e);
		udelay(100);
		rtl_write_word(rtlpriv, 0x356, 0xc280);
		rtl_write_word(rtlpriv, 0x354, 0xc290);
		rtl_write_word(rtlpriv, 0x358, 0x3e);
		udelay(100);
		rtl_write_word(rtlpriv, 0x358, 0x5e);
		udelay(100);
		tmpu2b = rtl_read_word(rtlpriv, 0x356);
		retry++;
	} while (tmpu2b != 0xc290 && retry < 100);

	if (retry >= 100) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "InitMAC(): ePHY configure fail!!!\n");
		return false;
	}

	rtl_write_word(rtlpriv, REG_CR, 0x2ff);
	rtl_write_word(rtlpriv, REG_CR + 1, 0x06);

	if (!mac_func_enable) {
		if (_rtl8723ae_llt_table_init(hw) == false)
			return false;
	}

	rtl_write_dword(rtlpriv, REG_HISR, 0xffffffff);
	rtl_write_byte(rtlpriv, REG_HISRE, 0xff);

	rtl_write_word(rtlpriv, REG_TRXFF_BNDY + 2, 0x27ff);

	wordtmp = rtl_read_word(rtlpriv, REG_TRXDMA_CTRL) & 0xf;
	wordtmp |= 0xF771;
	rtl_write_word(rtlpriv, REG_TRXDMA_CTRL, wordtmp);

	rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 1, 0x1F);
	rtl_write_dword(rtlpriv, REG_RCR, rtlpci->receive_config);
	rtl_write_word(rtlpriv, REG_RXFLTMAP2, 0xFFFF);
	rtl_write_dword(rtlpriv, REG_TCR, rtlpci->transmit_config);

	rtl_write_byte(rtlpriv, 0x4d0, 0x0);

	rtl_write_dword(rtlpriv, REG_BCNQ_DESA,
			((u64) rtlpci->tx_ring[BEACON_QUEUE].dma) &
			DMA_BIT_MASK(32));
	rtl_write_dword(rtlpriv, REG_MGQ_DESA,
			(u64) rtlpci->tx_ring[MGNT_QUEUE].dma &
			DMA_BIT_MASK(32));
	rtl_write_dword(rtlpriv, REG_VOQ_DESA,
			(u64) rtlpci->tx_ring[VO_QUEUE].dma & DMA_BIT_MASK(32));
	rtl_write_dword(rtlpriv, REG_VIQ_DESA,
			(u64) rtlpci->tx_ring[VI_QUEUE].dma & DMA_BIT_MASK(32));
	rtl_write_dword(rtlpriv, REG_BEQ_DESA,
			(u64) rtlpci->tx_ring[BE_QUEUE].dma & DMA_BIT_MASK(32));
	rtl_write_dword(rtlpriv, REG_BKQ_DESA,
			(u64) rtlpci->tx_ring[BK_QUEUE].dma & DMA_BIT_MASK(32));
	rtl_write_dword(rtlpriv, REG_HQ_DESA,
			(u64) rtlpci->tx_ring[HIGH_QUEUE].dma &
			DMA_BIT_MASK(32));
	rtl_write_dword(rtlpriv, REG_RX_DESA,
			(u64) rtlpci->rx_ring[RX_MPDU_QUEUE].dma &
			DMA_BIT_MASK(32));

	rtl_write_byte(rtlpriv, REG_PCIE_CTRL_REG + 3, 0x74);

	rtl_write_dword(rtlpriv, REG_INT_MIG, 0);

	bytetmp = rtl_read_byte(rtlpriv, REG_APSD_CTRL);
	rtl_write_byte(rtlpriv, REG_APSD_CTRL, bytetmp & ~BIT(6));
	do {
		retry++;
		bytetmp = rtl_read_byte(rtlpriv, REG_APSD_CTRL);
	} while ((retry < 200) && (bytetmp & BIT(7)));

	_rtl8723ae_gen_refresh_led_state(hw);

	rtl_write_dword(rtlpriv, REG_MCUTST_1, 0x0);

	return true;
}

static void _rtl8723ae_hw_configure(struct ieee80211_hw *hw)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	u8 reg_bw_opmode;
	u32 reg_prsr;

	reg_bw_opmode = BW_OPMODE_20MHZ;
	reg_prsr = RATE_ALL_CCK | RATE_ALL_OFDM_AG;

	rtl_write_byte(rtlpriv, REG_INIRTS_RATE_SEL, 0x8);

	rtl_write_byte(rtlpriv, REG_BWOPMODE, reg_bw_opmode);

	rtl_write_dword(rtlpriv, REG_RRSR, reg_prsr);

	rtl_write_byte(rtlpriv, REG_SLOT, 0x09);

	rtl_write_byte(rtlpriv, REG_AMPDU_MIN_SPACE, 0x0);

	rtl_write_word(rtlpriv, REG_FWHW_TXQ_CTRL, 0x1F80);

	rtl_write_word(rtlpriv, REG_RL, 0x0707);

	rtl_write_dword(rtlpriv, REG_BAR_MODE_CTRL, 0x02012802);

	rtl_write_byte(rtlpriv, REG_HWSEQ_CTRL, 0xFF);

	rtl_write_dword(rtlpriv, REG_DARFRC, 0x01000000);
	rtl_write_dword(rtlpriv, REG_DARFRC + 4, 0x07060504);
	rtl_write_dword(rtlpriv, REG_RARFRC, 0x01000000);
	rtl_write_dword(rtlpriv, REG_RARFRC + 4, 0x07060504);

	if ((pcipriv->bt_coexist.bt_coexistence) &&
	    (pcipriv->bt_coexist.bt_coexist_type == BT_CSR_BC4))
		rtl_write_dword(rtlpriv, REG_AGGLEN_LMT, 0x97427431);
	else
		rtl_write_dword(rtlpriv, REG_AGGLEN_LMT, 0xb972a841);

	rtl_write_byte(rtlpriv, REG_ATIMWND, 0x2);

	rtl_write_byte(rtlpriv, REG_BCN_MAX_ERR, 0xff);

	rtlpci->reg_bcn_ctrl_val = 0x1f;
	rtl_write_byte(rtlpriv, REG_BCN_CTRL, rtlpci->reg_bcn_ctrl_val);

	rtl_write_byte(rtlpriv, REG_TBTT_PROHIBIT + 1, 0xff);

	rtl_write_byte(rtlpriv, REG_TBTT_PROHIBIT + 1, 0xff);

	rtl_write_byte(rtlpriv, REG_PIFS, 0x1C);
	rtl_write_byte(rtlpriv, REG_AGGR_BREAK_TIME, 0x16);

	if ((pcipriv->bt_coexist.bt_coexistence) &&
	    (pcipriv->bt_coexist.bt_coexist_type == BT_CSR_BC4)) {
		rtl_write_word(rtlpriv, REG_NAV_PROT_LEN, 0x0020);
		rtl_write_word(rtlpriv, REG_PROT_MODE_CTRL, 0x0402);
	} else {
		rtl_write_word(rtlpriv, REG_NAV_PROT_LEN, 0x0020);
		rtl_write_word(rtlpriv, REG_NAV_PROT_LEN, 0x0020);
	}

	if ((pcipriv->bt_coexist.bt_coexistence) &&
	     (pcipriv->bt_coexist.bt_coexist_type == BT_CSR_BC4))
		rtl_write_dword(rtlpriv, REG_FAST_EDCA_CTRL, 0x03086666);
	else
		rtl_write_dword(rtlpriv, REG_FAST_EDCA_CTRL, 0x086666);

	rtl_write_byte(rtlpriv, REG_ACKTO, 0x40);

	rtl_write_word(rtlpriv, REG_SPEC_SIFS, 0x1010);
	rtl_write_word(rtlpriv, REG_MAC_SPEC_SIFS, 0x1010);

	rtl_write_word(rtlpriv, REG_SIFS_CTX, 0x1010);

	rtl_write_word(rtlpriv, REG_SIFS_TRX, 0x1010);

	rtl_write_dword(rtlpriv, REG_MAR, 0xffffffff);
	rtl_write_dword(rtlpriv, REG_MAR + 4, 0xffffffff);

	rtl_write_dword(rtlpriv, 0x394, 0x1);
}

static void _rtl8723ae_enable_aspm_back_door(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));

	rtl_write_byte(rtlpriv, 0x34b, 0x93);
	rtl_write_word(rtlpriv, 0x350, 0x870c);
	rtl_write_byte(rtlpriv, 0x352, 0x1);

	if (ppsc->support_backdoor)
		rtl_write_byte(rtlpriv, 0x349, 0x1b);
	else
		rtl_write_byte(rtlpriv, 0x349, 0x03);

	rtl_write_word(rtlpriv, 0x350, 0x2718);
	rtl_write_byte(rtlpriv, 0x352, 0x1);
}

void rtl8723ae_enable_hw_security_config(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 sec_reg_value;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
		 "PairwiseEncAlgorithm = %d GroupEncAlgorithm = %d\n",
		 rtlpriv->sec.pairwise_enc_algorithm,
		 rtlpriv->sec.group_enc_algorithm);

	if (rtlpriv->cfg->mod_params->sw_crypto || rtlpriv->sec.use_sw_sec) {
		RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG,
			 "not open hw encryption\n");
		return;
	}

	sec_reg_value = SCR_TxEncEnable | SCR_RxDecEnable;

	if (rtlpriv->sec.use_defaultkey) {
		sec_reg_value |= SCR_TxUseDK;
		sec_reg_value |= SCR_RxUseDK;
	}

	sec_reg_value |= (SCR_RXBCUSEDK | SCR_TXBCUSEDK);

	rtl_write_byte(rtlpriv, REG_CR + 1, 0x02);

	RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG,
		 "The SECR-value %x\n", sec_reg_value);

	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_WPA_CONFIG, &sec_reg_value);

}

int rtl8723ae_hw_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	bool rtstatus = true;
	int err;
	u8 tmp_u1b;
	unsigned long flags;

	rtlpriv->rtlhal.being_init_adapter = true;
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
	rtstatus = _rtl8712e_init_mac(hw);
	if (rtstatus != true) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "Init MAC failed\n");
		err = 1;
		goto exit;
	}

	err = rtl8723_download_fw(hw, false);
	if (err) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 "Failed to download FW. Init HW without FW now..\n");
		err = 1;
		goto exit;
	} else {
		rtlhal->fw_ready = true;
	}

	rtlhal->last_hmeboxnum = 0;
	rtl8723ae_phy_mac_config(hw);
	/* because the last function modifies RCR, we update
	 * rcr var here, or TP will be unstable as ther receive_config
	 * is wrong, RX RCR_ACRC32 will cause TP unstable & Rx
	 * RCR_APP_ICV will cause mac80211 unassoc for cisco 1252
	 */
	rtlpci->receive_config = rtl_read_dword(rtlpriv, REG_RCR);
	rtlpci->receive_config &= ~(RCR_ACRC32 | RCR_AICV);
	rtl_write_dword(rtlpriv, REG_RCR, rtlpci->receive_config);

	rtl8723ae_phy_bb_config(hw);
	rtlphy->rf_mode = RF_OP_BY_SW_3WIRE;
	rtl8723ae_phy_rf_config(hw);
	if (IS_VENDOR_UMC_A_CUT(rtlhal->version)) {
		rtl_set_rfreg(hw, RF90_PATH_A, RF_RX_G1, MASKDWORD, 0x30255);
		rtl_set_rfreg(hw, RF90_PATH_A, RF_RX_G2, MASKDWORD, 0x50a00);
	} else if (IS_81xxC_VENDOR_UMC_B_CUT(rtlhal->version)) {
		rtl_set_rfreg(hw, RF90_PATH_A, 0x0C, MASKDWORD, 0x894AE);
		rtl_set_rfreg(hw, RF90_PATH_A, 0x0A, MASKDWORD, 0x1AF31);
		rtl_set_rfreg(hw, RF90_PATH_A, RF_IPA, MASKDWORD, 0x8F425);
		rtl_set_rfreg(hw, RF90_PATH_A, RF_SYN_G2, MASKDWORD, 0x4F200);
		rtl_set_rfreg(hw, RF90_PATH_A, RF_RCK1, MASKDWORD, 0x44053);
		rtl_set_rfreg(hw, RF90_PATH_A, RF_RCK2, MASKDWORD, 0x80201);
	}
	rtlphy->rfreg_chnlval[0] = rtl_get_rfreg(hw, (enum radio_path)0,
						 RF_CHNLBW, RFREG_OFFSET_MASK);
	rtlphy->rfreg_chnlval[1] = rtl_get_rfreg(hw, (enum radio_path)1,
						 RF_CHNLBW, RFREG_OFFSET_MASK);
	rtl_set_bbreg(hw, RFPGA0_RFMOD, BCCKEN, 0x1);
	rtl_set_bbreg(hw, RFPGA0_RFMOD, BOFDMEN, 0x1);
	rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER2, BIT(10), 1);
	_rtl8723ae_hw_configure(hw);
	rtl_cam_reset_all_entry(hw);
	rtl8723ae_enable_hw_security_config(hw);

	ppsc->rfpwr_state = ERFON;

	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_ETHER_ADDR, mac->mac_addr);
	_rtl8723ae_enable_aspm_back_door(hw);
	rtlpriv->intf_ops->enable_aspm(hw);

	rtl8723ae_bt_hw_init(hw);

	if (ppsc->rfpwr_state == ERFON) {
		rtl8723ae_phy_set_rfpath_switch(hw, 1);
		if (rtlphy->iqk_initialized) {
			rtl8723ae_phy_iq_calibrate(hw, true);
		} else {
			rtl8723ae_phy_iq_calibrate(hw, false);
			rtlphy->iqk_initialized = true;
		}

		rtl8723ae_phy_lc_calibrate(hw);
	}

	tmp_u1b = efuse_read_1byte(hw, 0x1FA);
	if (!(tmp_u1b & BIT(0))) {
		rtl_set_rfreg(hw, RF90_PATH_A, 0x15, 0x0F, 0x05);
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE, "PA BIAS path A\n");
	}

	if (!(tmp_u1b & BIT(4))) {
		tmp_u1b = rtl_read_byte(rtlpriv, 0x16) & 0x0F;
		rtl_write_byte(rtlpriv, 0x16, tmp_u1b | 0x80);
		udelay(10);
		rtl_write_byte(rtlpriv, 0x16, tmp_u1b | 0x90);
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE, "under 1.5V\n");
	}
	rtl8723ae_dm_init(hw);
exit:
	local_irq_restore(flags);
	rtlpriv->rtlhal.being_init_adapter = false;
	return err;
}

static enum version_8723e _rtl8723ae_read_chip_version(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	enum version_8723e version = 0x0000;
	u32 value32;

	value32 = rtl_read_dword(rtlpriv, REG_SYS_CFG);
	if (value32 & TRP_VAUX_EN) {
		version = (enum version_8723e)(version |
			  ((value32 & VENDOR_ID) ? CHIP_VENDOR_UMC : 0));
		/* RTL8723 with BT function. */
		version = (enum version_8723e)(version |
			  ((value32 & BT_FUNC) ? CHIP_8723 : 0));

	} else {
		/* Normal mass production chip. */
		version = (enum version_8723e) NORMAL_CHIP;
		version = (enum version_8723e)(version |
			  ((value32 & VENDOR_ID) ? CHIP_VENDOR_UMC : 0));
		/* RTL8723 with BT function. */
		version = (enum version_8723e)(version |
			  ((value32 & BT_FUNC) ? CHIP_8723 : 0));
		if (IS_CHIP_VENDOR_UMC(version))
			version = (enum version_8723e)(version |
			((value32 & CHIP_VER_RTL_MASK)));/* IC version (CUT) */
		if (IS_8723_SERIES(version)) {
			value32 = rtl_read_dword(rtlpriv, REG_GPIO_OUTSTS);
			/* ROM code version */
			version = (enum version_8723e)(version |
				  ((value32 & RF_RL_ID)>>20));
		}
	}

	if (IS_8723_SERIES(version)) {
		value32 = rtl_read_dword(rtlpriv, REG_MULTI_FUNC_CTRL);
		rtlphy->polarity_ctl = ((value32 & WL_HWPDN_SL) ?
				       RT_POLARITY_HIGH_ACT :
				       RT_POLARITY_LOW_ACT);
	}
	switch (version) {
	case VERSION_TEST_UMC_CHIP_8723:
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "Chip Version ID: VERSION_TEST_UMC_CHIP_8723.\n");
		break;
	case VERSION_NORMAL_UMC_CHIP_8723_1T1R_A_CUT:
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "Chip Version ID: VERSION_NORMAL_UMC_CHIP_8723_1T1R_A_CUT.\n");
		break;
	case VERSION_NORMAL_UMC_CHIP_8723_1T1R_B_CUT:
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "Chip Version ID: VERSION_NORMAL_UMC_CHIP_8723_1T1R_B_CUT.\n");
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "Chip Version ID: Unknown. Bug?\n");
		break;
	}

	if (IS_8723_SERIES(version))
		rtlphy->rf_type = RF_1T1R;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "Chip RF Type: %s\n",
		(rtlphy->rf_type == RF_2T2R) ? "RF_2T2R" : "RF_1T1R");

	return version;
}

static int _rtl8723ae_set_media_status(struct ieee80211_hw *hw,
				     enum nl80211_iftype type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 bt_msr = rtl_read_byte(rtlpriv, MSR) & 0xfc;
	enum led_ctl_mode ledaction = LED_CTL_NO_LINK;

	rtl_write_dword(rtlpriv, REG_BCN_CTRL, 0);
	RT_TRACE(rtlpriv, COMP_BEACON, DBG_LOUD,
		 "clear 0x550 when set HW_VAR_MEDIA_STATUS\n");

	if (type == NL80211_IFTYPE_UNSPECIFIED ||
	    type == NL80211_IFTYPE_STATION) {
		_rtl8723ae_stop_tx_beacon(hw);
		_rtl8723ae_enable_bcn_sufunc(hw);
	} else if (type == NL80211_IFTYPE_ADHOC ||
		type == NL80211_IFTYPE_AP) {
		_rtl8723ae_resume_tx_beacon(hw);
		_rtl8723ae_disable_bcn_sufunc(hw);
	} else {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 "Set HW_VAR_MEDIA_STATUS: No such media status(%x).\n",
			 type);
	}

	switch (type) {
	case NL80211_IFTYPE_UNSPECIFIED:
		bt_msr |= MSR_NOLINK;
		ledaction = LED_CTL_LINK;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "Set Network type to NO LINK!\n");
		break;
	case NL80211_IFTYPE_ADHOC:
		bt_msr |= MSR_ADHOC;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "Set Network type to Ad Hoc!\n");
		break;
	case NL80211_IFTYPE_STATION:
		bt_msr |= MSR_INFRA;
		ledaction = LED_CTL_LINK;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "Set Network type to STA!\n");
		break;
	case NL80211_IFTYPE_AP:
		bt_msr |= MSR_AP;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "Set Network type to AP!\n");
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "Network type %d not supported!\n",
			 type);
		return 1;

	}

	rtl_write_byte(rtlpriv, (MSR), bt_msr);
	rtlpriv->cfg->ops->led_control(hw, ledaction);
	if ((bt_msr & MSR_MASK) == MSR_AP)
		rtl_write_byte(rtlpriv, REG_BCNTCFG + 1, 0x00);
	else
		rtl_write_byte(rtlpriv, REG_BCNTCFG + 1, 0x66);
	return 0;
}

void rtl8723ae_set_check_bssid(struct ieee80211_hw *hw, bool check_bssid)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 reg_rcr;

	if (rtlpriv->psc.rfpwr_state != ERFON)
		return;

	rtlpriv->cfg->ops->get_hw_reg(hw, HW_VAR_RCR, (u8 *)(&reg_rcr));

	if (check_bssid == true) {
		reg_rcr |= (RCR_CBSSID_DATA | RCR_CBSSID_BCN);
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_RCR,
					      (u8 *)(&reg_rcr));
		_rtl8723ae_set_bcn_ctrl_reg(hw, 0, BIT(4));
	} else if (check_bssid == false) {
		reg_rcr &= (~(RCR_CBSSID_DATA | RCR_CBSSID_BCN));
		_rtl8723ae_set_bcn_ctrl_reg(hw, BIT(4), 0);
		rtlpriv->cfg->ops->set_hw_reg(hw,
			HW_VAR_RCR, (u8 *) (&reg_rcr));
	}
}

int rtl8723ae_set_network_type(struct ieee80211_hw *hw,
			       enum nl80211_iftype type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (_rtl8723ae_set_media_status(hw, type))
		return -EOPNOTSUPP;

	if (rtlpriv->mac80211.link_state == MAC80211_LINKED) {
		if (type != NL80211_IFTYPE_AP)
			rtl8723ae_set_check_bssid(hw, true);
	} else {
		rtl8723ae_set_check_bssid(hw, false);
	}
	return 0;
}

/* don't set REG_EDCA_BE_PARAM here because mac80211 will send pkt when scan */
void rtl8723ae_set_qos(struct ieee80211_hw *hw, int aci)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl8723_dm_init_edca_turbo(hw);
	switch (aci) {
	case AC1_BK:
		rtl_write_dword(rtlpriv, REG_EDCA_BK_PARAM, 0xa44f);
		break;
	case AC0_BE:
		/* rtl_write_dword(rtlpriv, REG_EDCA_BE_PARAM, u4ac_param); */
		break;
	case AC2_VI:
		rtl_write_dword(rtlpriv, REG_EDCA_VI_PARAM, 0x5e4322);
		break;
	case AC3_VO:
		rtl_write_dword(rtlpriv, REG_EDCA_VO_PARAM, 0x2f3222);
		break;
	default:
		RT_ASSERT(false, "invalid aci: %d !\n", aci);
		break;
	}
}

void rtl8723ae_enable_interrupt(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	rtl_write_dword(rtlpriv, 0x3a8, rtlpci->irq_mask[0] & 0xFFFFFFFF);
	rtl_write_dword(rtlpriv, 0x3ac, rtlpci->irq_mask[1] & 0xFFFFFFFF);
	rtlpci->irq_enabled = true;
}

void rtl8723ae_disable_interrupt(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	rtl_write_dword(rtlpriv, 0x3a8, IMR8190_DISABLED);
	rtl_write_dword(rtlpriv, 0x3ac, IMR8190_DISABLED);
	rtlpci->irq_enabled = false;
	synchronize_irq(rtlpci->pdev->irq);
}

static void _rtl8723ae_poweroff_adapter(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 u1tmp;

	/* Combo (PCIe + USB) Card and PCIe-MF Card */
	/* 1. Run LPS WL RFOFF flow */
	rtl_hal_pwrseqcmdparsing(rtlpriv, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK,
		PWR_INTF_PCI_MSK, Rtl8723_NIC_LPS_ENTER_FLOW);

	/* 2. 0x1F[7:0] = 0 */
	/* turn off RF */
	rtl_write_byte(rtlpriv, REG_RF_CTRL, 0x00);
	if ((rtl_read_byte(rtlpriv, REG_MCUFWDL) & BIT(7)) && rtlhal->fw_ready)
		rtl8723ae_firmware_selfreset(hw);

	/* Reset MCU. Suggested by Filen. */
	u1tmp = rtl_read_byte(rtlpriv, REG_SYS_FUNC_EN+1);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN+1, (u1tmp & (~BIT(2))));

	/* g.	MCUFWDL 0x80[1:0]=0	 */
	/* reset MCU ready status */
	rtl_write_byte(rtlpriv, REG_MCUFWDL, 0x00);

	/* HW card disable configuration. */
	rtl_hal_pwrseqcmdparsing(rtlpriv, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK,
		PWR_INTF_PCI_MSK, Rtl8723_NIC_DISABLE_FLOW);

	/* Reset MCU IO Wrapper */
	u1tmp = rtl_read_byte(rtlpriv, REG_RSV_CTRL + 1);
	rtl_write_byte(rtlpriv, REG_RSV_CTRL + 1, (u1tmp & (~BIT(0))));
	u1tmp = rtl_read_byte(rtlpriv, REG_RSV_CTRL + 1);
	rtl_write_byte(rtlpriv, REG_RSV_CTRL + 1, u1tmp | BIT(0));

	/* 7. RSV_CTRL 0x1C[7:0] = 0x0E */
	/* lock ISO/CLK/Power control register */
	rtl_write_byte(rtlpriv, REG_RSV_CTRL, 0x0e);
}

void rtl8723ae_card_disable(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	enum nl80211_iftype opmode;

	mac->link_state = MAC80211_NOLINK;
	opmode = NL80211_IFTYPE_UNSPECIFIED;
	_rtl8723ae_set_media_status(hw, opmode);
	if (rtlpci->driver_is_goingto_unload ||
	    ppsc->rfoff_reason > RF_CHANGE_BY_PS)
		rtlpriv->cfg->ops->led_control(hw, LED_CTL_POWER_OFF);
	RT_SET_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC);
	_rtl8723ae_poweroff_adapter(hw);

	/* after power off we should do iqk again */
	rtlpriv->phy.iqk_initialized = false;
}

void rtl8723ae_interrupt_recognized(struct ieee80211_hw *hw,
				    u32 *p_inta, u32 *p_intb)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	*p_inta = rtl_read_dword(rtlpriv, 0x3a0) & rtlpci->irq_mask[0];
	rtl_write_dword(rtlpriv, 0x3a0, *p_inta);
}

void rtl8723ae_set_beacon_related_registers(struct ieee80211_hw *hw)
{

	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u16 bcn_interval, atim_window;

	bcn_interval = mac->beacon_interval;
	atim_window = 2;	/*FIX MERGE */
	rtl8723ae_disable_interrupt(hw);
	rtl_write_word(rtlpriv, REG_ATIMWND, atim_window);
	rtl_write_word(rtlpriv, REG_BCN_INTERVAL, bcn_interval);
	rtl_write_word(rtlpriv, REG_BCNTCFG, 0x660f);
	rtl_write_byte(rtlpriv, REG_RXTSF_OFFSET_CCK, 0x18);
	rtl_write_byte(rtlpriv, REG_RXTSF_OFFSET_OFDM, 0x18);
	rtl_write_byte(rtlpriv, 0x606, 0x30);
	rtl8723ae_enable_interrupt(hw);
}

void rtl8723ae_set_beacon_interval(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u16 bcn_interval = mac->beacon_interval;

	RT_TRACE(rtlpriv, COMP_BEACON, DBG_DMESG,
		 "beacon_interval:%d\n", bcn_interval);
	rtl8723ae_disable_interrupt(hw);
	rtl_write_word(rtlpriv, REG_BCN_INTERVAL, bcn_interval);
	rtl8723ae_enable_interrupt(hw);
}

void rtl8723ae_update_interrupt_mask(struct ieee80211_hw *hw,
				     u32 add_msr, u32 rm_msr)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	RT_TRACE(rtlpriv, COMP_INTR, DBG_LOUD,
		 "add_msr:%x, rm_msr:%x\n", add_msr, rm_msr);

	if (add_msr)
		rtlpci->irq_mask[0] |= add_msr;
	if (rm_msr)
		rtlpci->irq_mask[0] &= (~rm_msr);
	rtl8723ae_disable_interrupt(hw);
	rtl8723ae_enable_interrupt(hw);
}

static u8 _rtl8723ae_get_chnl_group(u8 chnl)
{
	u8 group;

	if (chnl < 3)
		group = 0;
	else if (chnl < 9)
		group = 1;
	else
		group = 2;
	return group;
}

static void _rtl8723ae_read_txpower_info_from_hwpg(struct ieee80211_hw *hw,
						   bool autoload_fail,
						   u8 *hwinfo)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u8 rf_path, index, tempval;
	u16 i;

	for (rf_path = 0; rf_path < 1; rf_path++) {
		for (i = 0; i < 3; i++) {
			if (!autoload_fail) {
				rtlefuse->eeprom_chnlarea_txpwr_cck
				    [rf_path][i] =
				    hwinfo[EEPROM_TXPOWERCCK + rf_path * 3 + i];
				rtlefuse->eeprom_chnlarea_txpwr_ht40_1s
				    [rf_path][i] =
				    hwinfo[EEPROM_TXPOWERHT40_1S + rf_path *
				    3 + i];
			} else {
				rtlefuse->eeprom_chnlarea_txpwr_cck
				    [rf_path][i] =
				    EEPROM_DEFAULT_TXPOWERLEVEL;
				rtlefuse->eeprom_chnlarea_txpwr_ht40_1s
				    [rf_path][i] =
				    EEPROM_DEFAULT_TXPOWERLEVEL;
			}
		}
	}

	for (i = 0; i < 3; i++) {
		if (!autoload_fail)
			tempval = hwinfo[EEPROM_TXPOWERHT40_2SDIFF + i];
		else
			tempval = EEPROM_DEFAULT_HT40_2SDIFF;
		rtlefuse->eprom_chnl_txpwr_ht40_2sdf[RF90_PATH_A][i] =
		    (tempval & 0xf);
		rtlefuse->eprom_chnl_txpwr_ht40_2sdf[RF90_PATH_B][i] =
		    ((tempval & 0xf0) >> 4);
	}

	for (rf_path = 0; rf_path < 2; rf_path++)
		for (i = 0; i < 3; i++)
			RTPRINT(rtlpriv, FINIT, INIT_EEPROM,
				"RF(%d) EEPROM CCK Area(%d) = 0x%x\n", rf_path,
				i, rtlefuse->eeprom_chnlarea_txpwr_cck
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
		for (i = 0; i < 14; i++) {
			index = _rtl8723ae_get_chnl_group((u8) i);

			rtlefuse->txpwrlevel_cck[rf_path][i] =
				rtlefuse->eeprom_chnlarea_txpwr_cck
							[rf_path][index];
			rtlefuse->txpwrlevel_ht40_1s[rf_path][i] =
				rtlefuse->eeprom_chnlarea_txpwr_ht40_1s
							[rf_path][index];

			if ((rtlefuse->eeprom_chnlarea_txpwr_ht40_1s
			    [rf_path][index] -
			    rtlefuse->eprom_chnl_txpwr_ht40_2sdf[rf_path]
			    [index]) > 0) {
				rtlefuse->txpwrlevel_ht40_2s[rf_path][i] =
					rtlefuse->eeprom_chnlarea_txpwr_ht40_1s
					[rf_path][index] -
					rtlefuse->eprom_chnl_txpwr_ht40_2sdf
					[rf_path][index];
			} else {
				rtlefuse->txpwrlevel_ht40_2s[rf_path][i] = 0;
			}
		}

		for (i = 0; i < 14; i++) {
			RTPRINT(rtlpriv, FINIT, INIT_TXPOWER,
				"RF(%d)-Ch(%d) [CCK / HT40_1S / HT40_2S] = "
				"[0x%x / 0x%x / 0x%x]\n", rf_path, i,
				rtlefuse->txpwrlevel_cck[rf_path][i],
				rtlefuse->txpwrlevel_ht40_1s[rf_path][i],
				rtlefuse->txpwrlevel_ht40_2s[rf_path][i]);
		}
	}

	for (i = 0; i < 3; i++) {
		if (!autoload_fail) {
			rtlefuse->eeprom_pwrlimit_ht40[i] =
			    hwinfo[EEPROM_TXPWR_GROUP + i];
			rtlefuse->eeprom_pwrlimit_ht20[i] =
			    hwinfo[EEPROM_TXPWR_GROUP + 3 + i];
		} else {
			rtlefuse->eeprom_pwrlimit_ht40[i] = 0;
			rtlefuse->eeprom_pwrlimit_ht20[i] = 0;
		}
	}

	for (rf_path = 0; rf_path < 2; rf_path++) {
		for (i = 0; i < 14; i++) {
			index = _rtl8723ae_get_chnl_group((u8) i);

			if (rf_path == RF90_PATH_A) {
				rtlefuse->pwrgroup_ht20[rf_path][i] =
				    (rtlefuse->eeprom_pwrlimit_ht20[index] &
				    0xf);
				rtlefuse->pwrgroup_ht40[rf_path][i] =
				    (rtlefuse->eeprom_pwrlimit_ht40[index] &
				    0xf);
			} else if (rf_path == RF90_PATH_B) {
				rtlefuse->pwrgroup_ht20[rf_path][i] =
				    ((rtlefuse->eeprom_pwrlimit_ht20[index] &
				    0xf0) >> 4);
				rtlefuse->pwrgroup_ht40[rf_path][i] =
				    ((rtlefuse->eeprom_pwrlimit_ht40[index] &
				    0xf0) >> 4);
			}

			RTPRINT(rtlpriv, FINIT, INIT_TXPOWER,
				"RF-%d pwrgroup_ht20[%d] = 0x%x\n", rf_path, i,
				rtlefuse->pwrgroup_ht20[rf_path][i]);
			RTPRINT(rtlpriv, FINIT, INIT_TXPOWER,
				"RF-%d pwrgroup_ht40[%d] = 0x%x\n", rf_path, i,
				rtlefuse->pwrgroup_ht40[rf_path][i]);
		}
	}

	for (i = 0; i < 14; i++) {
		index = _rtl8723ae_get_chnl_group((u8) i);

		if (!autoload_fail)
			tempval = hwinfo[EEPROM_TXPOWERHT20DIFF + index];
		else
			tempval = EEPROM_DEFAULT_HT20_DIFF;

		rtlefuse->txpwr_ht20diff[RF90_PATH_A][i] = (tempval & 0xF);
		rtlefuse->txpwr_ht20diff[RF90_PATH_B][i] =
		    ((tempval >> 4) & 0xF);

		if (rtlefuse->txpwr_ht20diff[RF90_PATH_A][i] & BIT(3))
			rtlefuse->txpwr_ht20diff[RF90_PATH_A][i] |= 0xF0;

		if (rtlefuse->txpwr_ht20diff[RF90_PATH_B][i] & BIT(3))
			rtlefuse->txpwr_ht20diff[RF90_PATH_B][i] |= 0xF0;

		index = _rtl8723ae_get_chnl_group((u8) i);

		if (!autoload_fail)
			tempval = hwinfo[EEPROM_TXPOWER_OFDMDIFF + index];
		else
			tempval = EEPROM_DEFAULT_LEGACYHTTXPOWERDIFF;

		rtlefuse->txpwr_legacyhtdiff[RF90_PATH_A][i] = (tempval & 0xF);
		rtlefuse->txpwr_legacyhtdiff[RF90_PATH_B][i] =
		    ((tempval >> 4) & 0xF);
	}

	rtlefuse->legacy_ht_txpowerdiff =
	    rtlefuse->txpwr_legacyhtdiff[RF90_PATH_A][7];

	for (i = 0; i < 14; i++)
		RTPRINT(rtlpriv, FINIT, INIT_TXPOWER,
			"RF-A Ht20 to HT40 Diff[%d] = 0x%x\n", i,
			rtlefuse->txpwr_ht20diff[RF90_PATH_A][i]);
	for (i = 0; i < 14; i++)
		RTPRINT(rtlpriv, FINIT, INIT_TXPOWER,
			"RF-A Legacy to Ht40 Diff[%d] = 0x%x\n", i,
			rtlefuse->txpwr_legacyhtdiff[RF90_PATH_A][i]);
	for (i = 0; i < 14; i++)
		RTPRINT(rtlpriv, FINIT, INIT_TXPOWER,
			"RF-B Ht20 to HT40 Diff[%d] = 0x%x\n", i,
			rtlefuse->txpwr_ht20diff[RF90_PATH_B][i]);
	for (i = 0; i < 14; i++)
		RTPRINT(rtlpriv, FINIT, INIT_TXPOWER,
			"RF-B Legacy to HT40 Diff[%d] = 0x%x\n", i,
			rtlefuse->txpwr_legacyhtdiff[RF90_PATH_B][i]);

	if (!autoload_fail)
		rtlefuse->eeprom_regulatory = (hwinfo[RF_OPTION1] & 0x7);
	else
		rtlefuse->eeprom_regulatory = 0;
	RTPRINT(rtlpriv, FINIT, INIT_TXPOWER,
		"eeprom_regulatory = 0x%x\n", rtlefuse->eeprom_regulatory);

	if (!autoload_fail)
		rtlefuse->eeprom_tssi[RF90_PATH_A] = hwinfo[EEPROM_TSSI_A];
	else
		rtlefuse->eeprom_tssi[RF90_PATH_A] = EEPROM_DEFAULT_TSSI;
	RTPRINT(rtlpriv, FINIT, INIT_TXPOWER,
		"TSSI_A = 0x%x, TSSI_B = 0x%x\n",
		rtlefuse->eeprom_tssi[RF90_PATH_A],
		rtlefuse->eeprom_tssi[RF90_PATH_B]);

	if (!autoload_fail)
		tempval = hwinfo[EEPROM_THERMAL_METER];
	else
		tempval = EEPROM_DEFAULT_THERMALMETER;
	rtlefuse->eeprom_thermalmeter = (tempval & 0x1f);

	if (rtlefuse->eeprom_thermalmeter == 0x1f || autoload_fail)
		rtlefuse->apk_thermalmeterignore = true;

	rtlefuse->thermalmeter[0] = rtlefuse->eeprom_thermalmeter;
	RTPRINT(rtlpriv, FINIT, INIT_TXPOWER,
		"thermalmeter = 0x%x\n", rtlefuse->eeprom_thermalmeter);
}

static void _rtl8723ae_read_adapter_info(struct ieee80211_hw *hw,
					 bool pseudo_test)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u16 i, usvalue;
	u8 hwinfo[HWSET_MAX_SIZE];
	u16 eeprom_id;

	if (pseudo_test) {
		/* need add */
		return;
	}
	if (rtlefuse->epromtype == EEPROM_BOOT_EFUSE) {
		rtl_efuse_shadow_map_update(hw);

		memcpy(hwinfo, &rtlefuse->efuse_map[EFUSE_INIT_MAP][0],
		       HWSET_MAX_SIZE);
	} else if (rtlefuse->epromtype == EEPROM_93C46) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "RTL819X Not boot from eeprom, check it !!");
	}

	RT_PRINT_DATA(rtlpriv, COMP_INIT, DBG_DMESG, ("MAP\n"),
		      hwinfo, HWSET_MAX_SIZE);

	eeprom_id = *((u16 *)&hwinfo[0]);
	if (eeprom_id != RTL8190_EEPROM_ID) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 "EEPROM ID(%#x) is invalid!!\n", eeprom_id);
		rtlefuse->autoload_failflag = true;
	} else {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "Autoload OK\n");
		rtlefuse->autoload_failflag = false;
	}

	if (rtlefuse->autoload_failflag == true)
		return;

	rtlefuse->eeprom_vid = *(u16 *) &hwinfo[EEPROM_VID];
	rtlefuse->eeprom_did = *(u16 *) &hwinfo[EEPROM_DID];
	rtlefuse->eeprom_svid = *(u16 *) &hwinfo[EEPROM_SVID];
	rtlefuse->eeprom_smid = *(u16 *) &hwinfo[EEPROM_SMID];
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

	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
		 "dev_addr: %pM\n", rtlefuse->dev_addr);

	_rtl8723ae_read_txpower_info_from_hwpg(hw,
			rtlefuse->autoload_failflag, hwinfo);

	rtl8723ae_read_bt_coexist_info_from_hwpg(hw,
			rtlefuse->autoload_failflag, hwinfo);

	rtlefuse->eeprom_channelplan = hwinfo[EEPROM_CHANNELPLAN];
	rtlefuse->eeprom_version = *(u16 *)&hwinfo[EEPROM_VERSION];
	rtlefuse->txpwr_fromeprom = true;
	rtlefuse->eeprom_oemid = hwinfo[EEPROM_CUSTOMER_ID];

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "EEPROM Customer ID: 0x%2x\n", rtlefuse->eeprom_oemid);

	/* set channel paln to world wide 13 */
	rtlefuse->channel_plan = COUNTRY_CODE_WORLD_WIDE_13;

	if (rtlhal->oem_id == RT_CID_DEFAULT) {
		switch (rtlefuse->eeprom_oemid) {
		case EEPROM_CID_DEFAULT:
			if (rtlefuse->eeprom_did == 0x8176) {
				if (CHK_SVID_SMID(0x10EC, 0x6151) ||
				    CHK_SVID_SMID(0x10EC, 0x6152) ||
				    CHK_SVID_SMID(0x10EC, 0x6154) ||
				    CHK_SVID_SMID(0x10EC, 0x6155) ||
				    CHK_SVID_SMID(0x10EC, 0x6177) ||
				    CHK_SVID_SMID(0x10EC, 0x6178) ||
				    CHK_SVID_SMID(0x10EC, 0x6179) ||
				    CHK_SVID_SMID(0x10EC, 0x6180) ||
				    CHK_SVID_SMID(0x10EC, 0x8151) ||
				    CHK_SVID_SMID(0x10EC, 0x8152) ||
				    CHK_SVID_SMID(0x10EC, 0x8154) ||
				    CHK_SVID_SMID(0x10EC, 0x8155) ||
				    CHK_SVID_SMID(0x10EC, 0x8181) ||
				    CHK_SVID_SMID(0x10EC, 0x8182) ||
				    CHK_SVID_SMID(0x10EC, 0x8184) ||
				    CHK_SVID_SMID(0x10EC, 0x8185) ||
				    CHK_SVID_SMID(0x10EC, 0x9151) ||
				    CHK_SVID_SMID(0x10EC, 0x9152) ||
				    CHK_SVID_SMID(0x10EC, 0x9154) ||
				    CHK_SVID_SMID(0x10EC, 0x9155) ||
				    CHK_SVID_SMID(0x10EC, 0x9181) ||
				    CHK_SVID_SMID(0x10EC, 0x9182) ||
				    CHK_SVID_SMID(0x10EC, 0x9184) ||
				    CHK_SVID_SMID(0x10EC, 0x9185))
					rtlhal->oem_id = RT_CID_TOSHIBA;
				else if (rtlefuse->eeprom_svid == 0x1025)
					rtlhal->oem_id = RT_CID_819X_ACER;
				else if (CHK_SVID_SMID(0x10EC, 0x6191) ||
					 CHK_SVID_SMID(0x10EC, 0x6192) ||
					 CHK_SVID_SMID(0x10EC, 0x6193) ||
					 CHK_SVID_SMID(0x10EC, 0x7191) ||
					 CHK_SVID_SMID(0x10EC, 0x7192) ||
					 CHK_SVID_SMID(0x10EC, 0x7193) ||
					 CHK_SVID_SMID(0x10EC, 0x8191) ||
					 CHK_SVID_SMID(0x10EC, 0x8192) ||
					 CHK_SVID_SMID(0x10EC, 0x8193))
					rtlhal->oem_id = RT_CID_819X_SAMSUNG;
				else if (CHK_SVID_SMID(0x10EC, 0x8195) ||
					 CHK_SVID_SMID(0x10EC, 0x9195) ||
					 CHK_SVID_SMID(0x10EC, 0x7194) ||
					 CHK_SVID_SMID(0x10EC, 0x8200) ||
					 CHK_SVID_SMID(0x10EC, 0x8201) ||
					 CHK_SVID_SMID(0x10EC, 0x8202) ||
					 CHK_SVID_SMID(0x10EC, 0x9200))
					rtlhal->oem_id = RT_CID_819X_LENOVO;
				else if (CHK_SVID_SMID(0x10EC, 0x8197) ||
					 CHK_SVID_SMID(0x10EC, 0x9196))
					rtlhal->oem_id = RT_CID_819X_CLEVO;
				else if (CHK_SVID_SMID(0x1028, 0x8194) ||
					 CHK_SVID_SMID(0x1028, 0x8198) ||
					 CHK_SVID_SMID(0x1028, 0x9197) ||
					 CHK_SVID_SMID(0x1028, 0x9198))
					rtlhal->oem_id = RT_CID_819X_DELL;
				else if (CHK_SVID_SMID(0x103C, 0x1629))
					rtlhal->oem_id = RT_CID_819X_HP;
				else if (CHK_SVID_SMID(0x1A32, 0x2315))
					rtlhal->oem_id = RT_CID_819X_QMI;
				else if (CHK_SVID_SMID(0x10EC, 0x8203))
					rtlhal->oem_id = RT_CID_819X_PRONETS;
				else if (CHK_SVID_SMID(0x1043, 0x84B5))
					rtlhal->oem_id =
						 RT_CID_819X_EDIMAX_ASUS;
				else
					rtlhal->oem_id = RT_CID_DEFAULT;
			} else if (rtlefuse->eeprom_did == 0x8178) {
				if (CHK_SVID_SMID(0x10EC, 0x6181) ||
				    CHK_SVID_SMID(0x10EC, 0x6182) ||
				    CHK_SVID_SMID(0x10EC, 0x6184) ||
				    CHK_SVID_SMID(0x10EC, 0x6185) ||
				    CHK_SVID_SMID(0x10EC, 0x7181) ||
				    CHK_SVID_SMID(0x10EC, 0x7182) ||
				    CHK_SVID_SMID(0x10EC, 0x7184) ||
				    CHK_SVID_SMID(0x10EC, 0x7185) ||
				    CHK_SVID_SMID(0x10EC, 0x8181) ||
				    CHK_SVID_SMID(0x10EC, 0x8182) ||
				    CHK_SVID_SMID(0x10EC, 0x8184) ||
				    CHK_SVID_SMID(0x10EC, 0x8185) ||
				    CHK_SVID_SMID(0x10EC, 0x9181) ||
				    CHK_SVID_SMID(0x10EC, 0x9182) ||
				    CHK_SVID_SMID(0x10EC, 0x9184) ||
				    CHK_SVID_SMID(0x10EC, 0x9185))
					rtlhal->oem_id = RT_CID_TOSHIBA;
				else if (rtlefuse->eeprom_svid == 0x1025)
					rtlhal->oem_id = RT_CID_819X_ACER;
				else if (CHK_SVID_SMID(0x10EC, 0x8186))
					rtlhal->oem_id = RT_CID_819X_PRONETS;
				else if (CHK_SVID_SMID(0x1043, 0x8486))
					rtlhal->oem_id =
						     RT_CID_819X_EDIMAX_ASUS;
				else
					rtlhal->oem_id = RT_CID_DEFAULT;
			} else {
					rtlhal->oem_id = RT_CID_DEFAULT;
			}
			break;
		case EEPROM_CID_TOSHIBA:
			rtlhal->oem_id = RT_CID_TOSHIBA;
			break;
		case EEPROM_CID_CCX:
			rtlhal->oem_id = RT_CID_CCX;
			break;
		case EEPROM_CID_QMI:
			rtlhal->oem_id = RT_CID_819X_QMI;
			break;
		case EEPROM_CID_WHQL:
				break;
		default:
			rtlhal->oem_id = RT_CID_DEFAULT;
			break;

		}
	}
}

static void _rtl8723ae_hal_customized_behavior(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	pcipriv->ledctl.led_opendrain = true;
	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
		 "RT Customized ID: 0x%02X\n", rtlhal->oem_id);
}

void rtl8723ae_read_eeprom_info(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 tmp_u1b;
	u32 value32;

	value32 = rtl_read_dword(rtlpriv, rtlpriv->cfg->maps[EFUSE_TEST]);
	value32 = (value32 & ~EFUSE_SEL_MASK) | EFUSE_SEL(EFUSE_WIFI_SEL_0);
	rtl_write_dword(rtlpriv, rtlpriv->cfg->maps[EFUSE_TEST], value32);

	rtlhal->version = _rtl8723ae_read_chip_version(hw);

	if (get_rf_type(rtlphy) == RF_1T1R)
		rtlpriv->dm.rfpath_rxenable[0] = true;
	else
		rtlpriv->dm.rfpath_rxenable[0] =
		    rtlpriv->dm.rfpath_rxenable[1] = true;
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "VersionID = 0x%4x\n",
		 rtlhal->version);

	tmp_u1b = rtl_read_byte(rtlpriv, REG_9346CR);
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
		_rtl8723ae_read_adapter_info(hw, false);
	} else {
		rtlefuse->autoload_failflag = true;
		_rtl8723ae_read_adapter_info(hw, false);
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "Autoload ERR!!\n");
	}
	_rtl8723ae_hal_customized_behavior(hw);
}

static void rtl8723ae_update_hal_rate_table(struct ieee80211_hw *hw,
					    struct ieee80211_sta *sta)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u32 ratr_value;
	u8 ratr_index = 0;
	u8 nmode = mac->ht_enable;
	u8 mimo_ps = IEEE80211_SMPS_OFF;
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
			    get_rf_type(rtlphy) == RF_1T1R)
				ratr_mask = 0x000ff005;
			else
				ratr_mask = 0x0f0ff005;

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

	if ((pcipriv->bt_coexist.bt_coexistence) &&
	    (pcipriv->bt_coexist.bt_coexist_type == BT_CSR_BC4) &&
	    (pcipriv->bt_coexist.bt_cur_state) &&
	    (pcipriv->bt_coexist.bt_ant_isolation) &&
	    ((pcipriv->bt_coexist.bt_service == BT_SCO) ||
	    (pcipriv->bt_coexist.bt_service == BT_BUSY)))
		ratr_value &= 0x0fffcfc0;
	else
		ratr_value &= 0x0FFFFFFF;

	if (nmode && ((curtxbw_40mhz && curshortgi_40mhz) ||
	   (!curtxbw_40mhz && curshortgi_20mhz)))
		ratr_value |= 0x10000000;

	rtl_write_dword(rtlpriv, REG_ARFR0 + ratr_index * 4, ratr_value);

	RT_TRACE(rtlpriv, COMP_RATR, DBG_DMESG,
		 "%x\n", rtl_read_dword(rtlpriv, REG_ARFR0));
}

static void rtl8723ae_update_hal_rate_mask(struct ieee80211_hw *hw,
		struct ieee80211_sta *sta, u8 rssi_level)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_sta_info *sta_entry = NULL;
	u32 ratr_bitmap;
	u8 ratr_index;
	u8 curtxbw_40mhz = (sta->bandwidth >= IEEE80211_STA_RX_BW_40) ? 1 : 0;
	u8 curshortgi_40mhz = (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_40) ?
				1 : 0;
	u8 curshortgi_20mhz = (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_20) ?
				1 : 0;
	enum wireless_mode wirelessmode = 0;
	bool shortgi = false;
	u8 rate_mask[5];
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
		ratr_index = RATR_INX_WIRELESS_A;
		ratr_bitmap &= 0x00000ff0;
		break;
	case WIRELESS_MODE_N_24G:
	case WIRELESS_MODE_N_5G:
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
	sta_entry->ratr_index = ratr_index;

	RT_TRACE(rtlpriv, COMP_RATR, DBG_DMESG,
		 "ratr_bitmap :%x\n", ratr_bitmap);
	/* convert ratr_bitmap to le byte array */
	rate_mask[0] = ratr_bitmap;
	rate_mask[1] = (ratr_bitmap >>= 8);
	rate_mask[2] = (ratr_bitmap >>= 8);
	rate_mask[3] = ((ratr_bitmap >> 8) & 0x0f) | (ratr_index << 4);
	rate_mask[4] = macid | (shortgi ? 0x20 : 0x00) | 0x80;
	RT_TRACE(rtlpriv, COMP_RATR, DBG_DMESG,
		 "Rate_index:%x, ratr_bitmap: %*phC\n",
		 ratr_index, 5, rate_mask);
	rtl8723ae_fill_h2c_cmd(hw, H2C_RA_MASK, 5, rate_mask);
}

void rtl8723ae_update_hal_rate_tbl(struct ieee80211_hw *hw,
		struct ieee80211_sta *sta, u8 rssi_level)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (rtlpriv->dm.useramask)
		rtl8723ae_update_hal_rate_mask(hw, sta, rssi_level);
	else
		rtl8723ae_update_hal_rate_table(hw, sta);
}

void rtl8723ae_update_channel_access_setting(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u16 sifs_timer;

	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_SLOT_TIME, &mac->slot_time);
	if (!mac->ht_enable)
		sifs_timer = 0x0a0a;
	else
		sifs_timer = 0x1010;
	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_SIFS, (u8 *)&sifs_timer);
}

bool rtl8723ae_gpio_radio_on_off_checking(struct ieee80211_hw *hw, u8 *valid)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	enum rf_pwrstate e_rfpowerstate_toset;
	u8 u1tmp;
	bool actuallyset = false;

	if (rtlpriv->rtlhal.being_init_adapter)
		return false;

	if (ppsc->swrf_processing)
		return false;

	spin_lock(&rtlpriv->locks.rf_ps_lock);
	if (ppsc->rfchange_inprogress) {
		spin_unlock(&rtlpriv->locks.rf_ps_lock);
		return false;
	} else {
		ppsc->rfchange_inprogress = true;
		spin_unlock(&rtlpriv->locks.rf_ps_lock);
	}

	rtl_write_byte(rtlpriv, REG_GPIO_IO_SEL_2,
		       rtl_read_byte(rtlpriv, REG_GPIO_IO_SEL_2)&~(BIT(1)));

	u1tmp = rtl_read_byte(rtlpriv, REG_GPIO_PIN_CTRL_2);

	if (rtlphy->polarity_ctl)
		e_rfpowerstate_toset = (u1tmp & BIT(1)) ? ERFOFF : ERFON;
	else
		e_rfpowerstate_toset = (u1tmp & BIT(1)) ? ERFON : ERFOFF;

	if ((ppsc->hwradiooff == true) && (e_rfpowerstate_toset == ERFON)) {
		RT_TRACE(rtlpriv, COMP_RF, DBG_DMESG,
			 "GPIOChangeRF  - HW Radio ON, RF ON\n");

		e_rfpowerstate_toset = ERFON;
		ppsc->hwradiooff = false;
		actuallyset = true;
	} else if ((ppsc->hwradiooff == false)
		   && (e_rfpowerstate_toset == ERFOFF)) {
		RT_TRACE(rtlpriv, COMP_RF, DBG_DMESG,
			 "GPIOChangeRF  - HW Radio OFF, RF OFF\n");

		e_rfpowerstate_toset = ERFOFF;
		ppsc->hwradiooff = true;
		actuallyset = true;
	}

	if (actuallyset) {
		spin_lock(&rtlpriv->locks.rf_ps_lock);
		ppsc->rfchange_inprogress = false;
		spin_unlock(&rtlpriv->locks.rf_ps_lock);
	} else {
		if (ppsc->reg_rfps_level & RT_RF_OFF_LEVL_HALT_NIC)
			RT_SET_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC);

		spin_lock(&rtlpriv->locks.rf_ps_lock);
		ppsc->rfchange_inprogress = false;
		spin_unlock(&rtlpriv->locks.rf_ps_lock);
	}

	*valid = 1;
	return !ppsc->hwradiooff;
}

void rtl8723ae_set_key(struct ieee80211_hw *hw, u32 key_index,
		       u8 *p_macaddr, bool is_group, u8 enc_algo,
		       bool is_wepkey, bool clear_all)
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
				 "switch case not processed\n");
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
								macaddr);
					if (entry_id >=  TOTAL_CAM_ENTRY) {
						RT_TRACE(rtlpriv, COMP_SEC,
							 DBG_EMERG,
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
					 "set Pairwiase key\n");

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
						enc_algo,
						CAM_CONFIG_NO_USEDK,
						rtlpriv->sec.key_buf
						[entry_id]);
				}

				rtl_cam_add_one_entry(hw, macaddr, key_index,
						entry_id, enc_algo,
						CAM_CONFIG_NO_USEDK,
						rtlpriv->sec.key_buf[entry_id]);
			}

		}
	}
}

static void rtl8723ae_bt_var_init(struct ieee80211_hw *hw)
{
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	pcipriv->bt_coexist.bt_coexistence =
					pcipriv->bt_coexist.eeprom_bt_coexist;
	pcipriv->bt_coexist.bt_ant_num =
					pcipriv->bt_coexist.eeprom_bt_ant_num;
	pcipriv->bt_coexist.bt_coexist_type =
					pcipriv->bt_coexist.eeprom_bt_type;

		pcipriv->bt_coexist.bt_ant_isolation =
				pcipriv->bt_coexist.eeprom_bt_ant_isol;

	pcipriv->bt_coexist.bt_radio_shared_type =
				pcipriv->bt_coexist.eeprom_bt_radio_shared;

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
		 "BT Coexistance = 0x%x\n",
		 pcipriv->bt_coexist.bt_coexistence);

	if (pcipriv->bt_coexist.bt_coexistence) {
		pcipriv->bt_coexist.bt_busy_traffic = false;
		pcipriv->bt_coexist.bt_traffic_mode_set = false;
		pcipriv->bt_coexist.bt_non_traffic_mode_set = false;

		pcipriv->bt_coexist.cstate = 0;
		pcipriv->bt_coexist.previous_state = 0;

		if (pcipriv->bt_coexist.bt_ant_num == ANT_X2) {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
				 "BlueTooth BT_Ant_Num = Antx2\n");
		} else if (pcipriv->bt_coexist.bt_ant_num == ANT_X1) {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
				 "BlueTooth BT_Ant_Num = Antx1\n");
		}

		switch (pcipriv->bt_coexist.bt_coexist_type) {
		case BT_2WIRE:
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
				 "BlueTooth BT_CoexistType = BT_2Wire\n");
			break;
		case BT_ISSC_3WIRE:
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
				 "BlueTooth BT_CoexistType = BT_ISSC_3Wire\n");
			break;
		case BT_ACCEL:
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
				 "BlueTooth BT_CoexistType = BT_ACCEL\n");
			break;
		case BT_CSR_BC4:
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
				 "BlueTooth BT_CoexistType = BT_CSR_BC4\n");
			break;
		case BT_CSR_BC8:
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
				 "BlueTooth BT_CoexistType = BT_CSR_BC8\n");
			break;
		case BT_RTL8756:
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
				 "BlueTooth BT_CoexistType = BT_RTL8756\n");
			break;
		default:
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
				 "BlueTooth BT_CoexistType = Unknown\n");
			break;
		}
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
			 "BlueTooth BT_Ant_isolation = %d\n",
			 pcipriv->bt_coexist.bt_ant_isolation);
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
			 "BT_RadioSharedType = 0x%x\n",
			 pcipriv->bt_coexist.bt_radio_shared_type);
		pcipriv->bt_coexist.bt_active_zero_cnt = 0;
		pcipriv->bt_coexist.cur_bt_disabled = false;
		pcipriv->bt_coexist.pre_bt_disabled = false;
	}
}

void rtl8723ae_read_bt_coexist_info_from_hwpg(struct ieee80211_hw *hw,
					      bool auto_load_fail, u8 *hwinfo)
{
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 value;
	u32 tmpu_32;

	if (!auto_load_fail) {
		tmpu_32 = rtl_read_dword(rtlpriv, REG_MULTI_FUNC_CTRL);
		if (tmpu_32 & BIT(18))
			pcipriv->bt_coexist.eeprom_bt_coexist = 1;
		else
			pcipriv->bt_coexist.eeprom_bt_coexist = 0;
		value = hwinfo[RF_OPTION4];
		pcipriv->bt_coexist.eeprom_bt_type = BT_RTL8723A;
		pcipriv->bt_coexist.eeprom_bt_ant_num = (value & 0x1);
		pcipriv->bt_coexist.eeprom_bt_ant_isol = ((value & 0x10) >> 4);
		pcipriv->bt_coexist.eeprom_bt_radio_shared =
				((value & 0x20) >> 5);
	} else {
		pcipriv->bt_coexist.eeprom_bt_coexist = 0;
		pcipriv->bt_coexist.eeprom_bt_type = BT_RTL8723A;
		pcipriv->bt_coexist.eeprom_bt_ant_num = ANT_X2;
		pcipriv->bt_coexist.eeprom_bt_ant_isol = 0;
		pcipriv->bt_coexist.eeprom_bt_radio_shared = BT_RADIO_SHARED;
	}

	rtl8723ae_bt_var_init(hw);
}

void rtl8723ae_bt_reg_init(struct ieee80211_hw *hw)
{
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);

	/* 0:Low, 1:High, 2:From Efuse. */
	pcipriv->bt_coexist.reg_bt_iso = 2;
	/* 0:Idle, 1:None-SCO, 2:SCO, 3:From Counter. */
	pcipriv->bt_coexist.reg_bt_sco = 3;
	/* 0:Disable BT control A-MPDU, 1:Enable BT control A-MPDU. */
	pcipriv->bt_coexist.reg_bt_sco = 0;
}


void rtl8723ae_bt_hw_init(struct ieee80211_hw *hw)
{
}

void rtl8723ae_suspend(struct ieee80211_hw *hw)
{
}

void rtl8723ae_resume(struct ieee80211_hw *hw)
{
}
