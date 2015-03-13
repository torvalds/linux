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
#include "pwrseq.h"

#define LLT_CONFIG		5

static void _rtl88ee_set_bcn_ctrl_reg(struct ieee80211_hw *hw,
				      u8 set_bits, u8 clear_bits)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpci->reg_bcn_ctrl_val |= set_bits;
	rtlpci->reg_bcn_ctrl_val &= ~clear_bits;

	rtl_write_byte(rtlpriv, REG_BCN_CTRL, (u8) rtlpci->reg_bcn_ctrl_val);
}

static void _rtl88ee_stop_tx_beacon(struct ieee80211_hw *hw)
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

static void _rtl88ee_resume_tx_beacon(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 tmp1byte;

	tmp1byte = rtl_read_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2);
	rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2, tmp1byte | BIT(6));
	rtl_write_byte(rtlpriv, REG_TBTT_PROHIBIT + 1, 0xff);
	tmp1byte = rtl_read_byte(rtlpriv, REG_TBTT_PROHIBIT + 2);
	tmp1byte |= BIT(0);
	rtl_write_byte(rtlpriv, REG_TBTT_PROHIBIT + 2, tmp1byte);
}

static void _rtl88ee_enable_bcn_sub_func(struct ieee80211_hw *hw)
{
	_rtl88ee_set_bcn_ctrl_reg(hw, 0, BIT(1));
}

static void _rtl88ee_return_beacon_queue_skb(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl8192_tx_ring *ring = &rtlpci->tx_ring[BEACON_QUEUE];
	unsigned long flags;

	spin_lock_irqsave(&rtlpriv->locks.irq_th_lock, flags);
	while (skb_queue_len(&ring->queue)) {
		struct rtl_tx_desc *entry = &ring->desc[ring->idx];
		struct sk_buff *skb = __skb_dequeue(&ring->queue);

		pci_unmap_single(rtlpci->pdev,
				 rtlpriv->cfg->ops->get_desc(
				 (u8 *)entry, true, HW_DESC_TXBUFF_ADDR),
				 skb->len, PCI_DMA_TODEVICE);
		kfree_skb(skb);
		ring->idx = (ring->idx + 1) % ring->entries;
	}
	spin_unlock_irqrestore(&rtlpriv->locks.irq_th_lock, flags);
}

static void _rtl88ee_disable_bcn_sub_func(struct ieee80211_hw *hw)
{
	_rtl88ee_set_bcn_ctrl_reg(hw, BIT(1), 0);
}

static void _rtl88ee_set_fw_clock_on(struct ieee80211_hw *hw,
				     u8 rpwm_val, bool b_need_turn_off_ckk)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	bool b_support_remote_wake_up;
	u32 count = 0, isr_regaddr, content;
	bool schedule_timer = b_need_turn_off_ckk;
	rtlpriv->cfg->ops->get_hw_reg(hw, HAL_DEF_WOWLAN,
					(u8 *)(&b_support_remote_wake_up));

	if (!rtlhal->fw_ready)
		return;
	if (!rtlpriv->psc.fw_current_inpsmode)
		return;

	while (1) {
		spin_lock_bh(&rtlpriv->locks.fw_ps_lock);
		if (rtlhal->fw_clk_change_in_progress) {
			while (rtlhal->fw_clk_change_in_progress) {
				spin_unlock_bh(&rtlpriv->locks.fw_ps_lock);
				count++;
				udelay(100);
				if (count > 1000)
					return;
				spin_lock_bh(&rtlpriv->locks.fw_ps_lock);
			}
			spin_unlock_bh(&rtlpriv->locks.fw_ps_lock);
		} else {
			rtlhal->fw_clk_change_in_progress = false;
			spin_unlock_bh(&rtlpriv->locks.fw_ps_lock);
			break;
		}
	}

	if (IS_IN_LOW_POWER_STATE_88E(rtlhal->fw_ps_state)) {
		rtlpriv->cfg->ops->get_hw_reg(hw, HW_VAR_SET_RPWM, &rpwm_val);
		if (FW_PS_IS_ACK(rpwm_val)) {
			isr_regaddr = REG_HISR;
			content = rtl_read_dword(rtlpriv, isr_regaddr);
			while (!(content & IMR_CPWM) && (count < 500)) {
				udelay(50);
				count++;
				content = rtl_read_dword(rtlpriv, isr_regaddr);
			}

			if (content & IMR_CPWM) {
				rtl_write_word(rtlpriv, isr_regaddr, 0x0100);
				rtlhal->fw_ps_state = FW_PS_STATE_RF_ON_88E;
				RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
					 "Receive CPWM INT!!! Set pHalData->FwPSState = %X\n",
					 rtlhal->fw_ps_state);
			}
		}

		spin_lock_bh(&rtlpriv->locks.fw_ps_lock);
		rtlhal->fw_clk_change_in_progress = false;
		spin_unlock_bh(&rtlpriv->locks.fw_ps_lock);
		if (schedule_timer) {
			mod_timer(&rtlpriv->works.fw_clockoff_timer,
				  jiffies + MSECS(10));
		}

	} else  {
		spin_lock_bh(&rtlpriv->locks.fw_ps_lock);
		rtlhal->fw_clk_change_in_progress = false;
		spin_unlock_bh(&rtlpriv->locks.fw_ps_lock);
	}
}

static void _rtl88ee_set_fw_clock_off(struct ieee80211_hw *hw,
				      u8 rpwm_val)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl8192_tx_ring *ring;
	enum rf_pwrstate rtstate;
	bool schedule_timer = false;
	u8 queue;

	if (!rtlhal->fw_ready)
		return;
	if (!rtlpriv->psc.fw_current_inpsmode)
		return;
	if (!rtlhal->allow_sw_to_change_hwclc)
		return;
	rtlpriv->cfg->ops->get_hw_reg(hw, HW_VAR_RF_STATE, (u8 *)(&rtstate));
	if (rtstate == ERFOFF || rtlpriv->psc.inactive_pwrstate == ERFOFF)
		return;

	for (queue = 0; queue < RTL_PCI_MAX_TX_QUEUE_COUNT; queue++) {
		ring = &rtlpci->tx_ring[queue];
		if (skb_queue_len(&ring->queue)) {
			schedule_timer = true;
			break;
		}
	}

	if (schedule_timer) {
		mod_timer(&rtlpriv->works.fw_clockoff_timer,
			  jiffies + MSECS(10));
		return;
	}

	if (FW_PS_STATE(rtlhal->fw_ps_state) !=
	    FW_PS_STATE_RF_OFF_LOW_PWR_88E) {
		spin_lock_bh(&rtlpriv->locks.fw_ps_lock);
		if (!rtlhal->fw_clk_change_in_progress) {
			rtlhal->fw_clk_change_in_progress = true;
			spin_unlock_bh(&rtlpriv->locks.fw_ps_lock);
			rtlhal->fw_ps_state = FW_PS_STATE(rpwm_val);
			rtl_write_word(rtlpriv, REG_HISR, 0x0100);
			rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_SET_RPWM,
						      &rpwm_val);
			spin_lock_bh(&rtlpriv->locks.fw_ps_lock);
			rtlhal->fw_clk_change_in_progress = false;
			spin_unlock_bh(&rtlpriv->locks.fw_ps_lock);
		} else {
			spin_unlock_bh(&rtlpriv->locks.fw_ps_lock);
			mod_timer(&rtlpriv->works.fw_clockoff_timer,
				  jiffies + MSECS(10));
		}
	}
}

static void _rtl88ee_set_fw_ps_rf_on(struct ieee80211_hw *hw)
{
	u8 rpwm_val = 0;

	rpwm_val |= (FW_PS_STATE_RF_OFF_88E | FW_PS_ACK);
	_rtl88ee_set_fw_clock_on(hw, rpwm_val, true);
}

static void _rtl88ee_set_fw_ps_rf_off_low_power(struct ieee80211_hw *hw)
{
	u8 rpwm_val = 0;
	rpwm_val |= FW_PS_STATE_RF_OFF_LOW_PWR_88E;
	_rtl88ee_set_fw_clock_off(hw, rpwm_val);
}
void rtl88ee_fw_clk_off_timer_callback(unsigned long data)
{
	struct ieee80211_hw *hw = (struct ieee80211_hw *)data;

	_rtl88ee_set_fw_ps_rf_off_low_power(hw);
}

static void _rtl88ee_fwlps_leave(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	bool fw_current_inps = false;
	u8 rpwm_val = 0, fw_pwrmode = FW_PS_ACTIVE_MODE;

	if (ppsc->low_power_enable) {
		rpwm_val = (FW_PS_STATE_ALL_ON_88E|FW_PS_ACK);/* RF on */
		_rtl88ee_set_fw_clock_on(hw, rpwm_val, false);
		rtlhal->allow_sw_to_change_hwclc = false;
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_H2C_FW_PWRMODE,
					      &fw_pwrmode);
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_FW_PSMODE_STATUS,
					      (u8 *)(&fw_current_inps));
	} else {
		rpwm_val = FW_PS_STATE_ALL_ON_88E;	/* RF on */
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_SET_RPWM, &rpwm_val);
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_H2C_FW_PWRMODE,
					      &fw_pwrmode);
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_FW_PSMODE_STATUS,
					      (u8 *)(&fw_current_inps));
	}
}

static void _rtl88ee_fwlps_enter(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	bool fw_current_inps = true;
	u8 rpwm_val;

	if (ppsc->low_power_enable) {
		rpwm_val = FW_PS_STATE_RF_OFF_LOW_PWR_88E;	/* RF off */
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_FW_PSMODE_STATUS,
					      (u8 *)(&fw_current_inps));
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_H2C_FW_PWRMODE,
					      &ppsc->fwctrl_psmode);
		rtlhal->allow_sw_to_change_hwclc = true;
		_rtl88ee_set_fw_clock_off(hw, rpwm_val);
	} else {
		rpwm_val = FW_PS_STATE_RF_OFF_88E;	/* RF off */
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_FW_PSMODE_STATUS,
					      (u8 *)(&fw_current_inps));
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_H2C_FW_PWRMODE,
					      &ppsc->fwctrl_psmode);
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_SET_RPWM, &rpwm_val);
	}
}

void rtl88ee_get_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	switch (variable) {
	case HW_VAR_RCR:
		*((u32 *)(val)) = rtlpci->receive_config;
		break;
	case HW_VAR_RF_STATE:
		*((enum rf_pwrstate *)(val)) = ppsc->rfpwr_state;
		break;
	case HW_VAR_FWLPS_RF_ON:{
		enum rf_pwrstate rfstate;
		u32 val_rcr;

		rtlpriv->cfg->ops->get_hw_reg(hw,
					      HW_VAR_RF_STATE,
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
		break; }
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
		break; }
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "switch case not process %x\n", variable);
		break;
	}
}

void rtl88ee_set_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
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
		u16 b_rate_cfg = ((u16 *)val)[0];
		u8 rate_index = 0;
		b_rate_cfg = b_rate_cfg & 0x15f;
		b_rate_cfg |= 0x01;
		rtl_write_byte(rtlpriv, REG_RRSR, b_rate_cfg & 0xff);
		rtl_write_byte(rtlpriv, REG_RRSR + 1,
			       (b_rate_cfg >> 8) & 0xff);
		while (b_rate_cfg > 0x1) {
			b_rate_cfg = (b_rate_cfg >> 1);
			rate_index++;
		}
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
	case HW_VAR_SLOT_TIME:{
		u8 e_aci;

		RT_TRACE(rtlpriv, COMP_MLME, DBG_LOUD,
			 "HW_VAR_SLOT_TIME %x\n", val[0]);

		rtl_write_byte(rtlpriv, REG_SLOT, val[0]);

		for (e_aci = 0; e_aci < AC_MAX; e_aci++) {
			rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_AC_PARAM,
						      &e_aci);
		}
		break;
		}
	case HW_VAR_ACK_PREAMBLE:{
		u8 reg_tmp;
		u8 short_preamble = (bool)*val;
		reg_tmp = rtl_read_byte(rtlpriv, REG_TRXPTCL_CTL+2);
		if (short_preamble) {
			reg_tmp |= 0x02;
			rtl_write_byte(rtlpriv, REG_TRXPTCL_CTL +
				       2, reg_tmp);
		} else {
			reg_tmp |= 0xFD;
			rtl_write_byte(rtlpriv, REG_TRXPTCL_CTL +
				       2, reg_tmp);
		}
		break; }
	case HW_VAR_WPA_CONFIG:
		rtl_write_byte(rtlpriv, REG_SECCFG, *val);
		break;
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
		break;
		}
	case HW_VAR_AMPDU_FACTOR:{
		u8 regtoset_normal[4] = { 0x41, 0xa8, 0x72, 0xb9 };
		u8 factor_toset;
		u8 *p_regtoset = NULL;
		u8 index = 0;

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
		rtl88e_dm_init_edca_turbo(hw);

		if (rtlpci->acm_method != EACMWAY2_SW)
			rtlpriv->cfg->ops->set_hw_reg(hw,
						      HW_VAR_ACM_CTRL,
						      &e_aci);
		break; }
	case HW_VAR_ACM_CTRL:{
		u8 e_aci = *val;
		union aci_aifsn *p_aci_aifsn =
		    (union aci_aifsn *)(&(mac->ac[0].aifs));
		u8 acm = p_aci_aifsn->f.acm;
		u8 acm_ctrl = rtl_read_byte(rtlpriv, REG_ACMHWCTRL);

		acm_ctrl = acm_ctrl |
			   ((rtlpci->acm_method == 2) ? 0x0 : 0x1);

		if (acm) {
			switch (e_aci) {
			case AC0_BE:
				acm_ctrl |= ACMHW_BEQEN;
				break;
			case AC2_VI:
				acm_ctrl |= ACMHW_VIQEN;
				break;
			case AC3_VO:
				acm_ctrl |= ACMHW_VOQEN;
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
				acm_ctrl &= (~ACMHW_BEQEN);
				break;
			case AC2_VI:
				acm_ctrl &= (~ACMHW_VIQEN);
				break;
			case AC3_VO:
				acm_ctrl &= (~ACMHW_BEQEN);
				break;
			default:
				RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
					 "switch case not process\n");
				break;
			}
		}

		RT_TRACE(rtlpriv, COMP_QOS, DBG_TRACE,
			 "SetHwReg8190pci(): [HW_VAR_ACM_CTRL] Write 0x%X\n",
			 acm_ctrl);
		rtl_write_byte(rtlpriv, REG_ACMHWCTRL, acm_ctrl);
		break; }
	case HW_VAR_RCR:
		rtl_write_dword(rtlpriv, REG_RCR, ((u32 *)(val))[0]);
		rtlpci->receive_config = ((u32 *)(val))[0];
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
		rtlefuse->efuse_usedbytes = *((u16 *)val);
		break;
	case HW_VAR_EFUSE_USAGE:
		rtlefuse->efuse_usedpercentage = *val;
		break;
	case HW_VAR_IO_CMD:
		rtl88e_phy_set_io_cmd(hw, (*(enum io_type *)val));
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
	case HW_VAR_H2C_FW_PWRMODE:
		rtl88e_set_fw_pwrmode_cmd(hw, *val);
		break;
	case HW_VAR_FW_PSMODE_STATUS:
		ppsc->fw_current_inpsmode = *((bool *)val);
		break;
	case HW_VAR_RESUME_CLK_ON:
		_rtl88ee_set_fw_ps_rf_on(hw);
		break;
	case HW_VAR_FW_LPS_ACTION:{
		bool enter_fwlps = *((bool *)val);

		if (enter_fwlps)
			_rtl88ee_fwlps_enter(hw);
		 else
			_rtl88ee_fwlps_leave(hw);

		 break; }
	case HW_VAR_H2C_FW_JOINBSSRPT:{
		u8 mstatus = *val;
		u8 tmp_regcr, tmp_reg422, bcnvalid_reg;
		u8 count = 0, dlbcn_count = 0;
		bool b_recover = false;

		if (mstatus == RT_MEDIA_CONNECT) {
			rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_AID,
						      NULL);

			tmp_regcr = rtl_read_byte(rtlpriv, REG_CR + 1);
			rtl_write_byte(rtlpriv, REG_CR + 1,
				       (tmp_regcr | BIT(0)));

			_rtl88ee_set_bcn_ctrl_reg(hw, 0, BIT(3));
			_rtl88ee_set_bcn_ctrl_reg(hw, BIT(4), 0);

			tmp_reg422 =
			    rtl_read_byte(rtlpriv,
					  REG_FWHW_TXQ_CTRL + 2);
			rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2,
				       tmp_reg422 & (~BIT(6)));
			if (tmp_reg422 & BIT(6))
				b_recover = true;

			do {
				bcnvalid_reg = rtl_read_byte(rtlpriv,
							     REG_TDECTRL+2);
				rtl_write_byte(rtlpriv, REG_TDECTRL+2,
					       (bcnvalid_reg | BIT(0)));
				_rtl88ee_return_beacon_queue_skb(hw);

				rtl88e_set_fw_rsvdpagepkt(hw, 0);
				bcnvalid_reg = rtl_read_byte(rtlpriv,
							     REG_TDECTRL+2);
				count = 0;
				while (!(bcnvalid_reg & BIT(0)) && count < 20) {
					count++;
					udelay(10);
					bcnvalid_reg =
					  rtl_read_byte(rtlpriv, REG_TDECTRL+2);
				}
				dlbcn_count++;
			} while (!(bcnvalid_reg & BIT(0)) && dlbcn_count < 5);

			if (bcnvalid_reg & BIT(0))
				rtl_write_byte(rtlpriv, REG_TDECTRL+2, BIT(0));

			_rtl88ee_set_bcn_ctrl_reg(hw, BIT(3), 0);
			_rtl88ee_set_bcn_ctrl_reg(hw, 0, BIT(4));

			if (b_recover) {
				rtl_write_byte(rtlpriv,
					       REG_FWHW_TXQ_CTRL + 2,
					       tmp_reg422);
			}

			rtl_write_byte(rtlpriv, REG_CR + 1,
				       (tmp_regcr & ~(BIT(0))));
		}
		rtl88e_set_fw_joinbss_report_cmd(hw, (*(u8 *)val));
		break; }
	case HW_VAR_H2C_FW_P2P_PS_OFFLOAD:
		rtl88e_set_p2p_ps_offload_cmd(hw, *val);
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

		if (btype_ibss)
			_rtl88ee_stop_tx_beacon(hw);

		_rtl88ee_set_bcn_ctrl_reg(hw, 0, BIT(3));

		rtl_write_dword(rtlpriv, REG_TSFTR,
				(u32)(mac->tsf & 0xffffffff));
		rtl_write_dword(rtlpriv, REG_TSFTR + 4,
				(u32)((mac->tsf >> 32) & 0xffffffff));

		_rtl88ee_set_bcn_ctrl_reg(hw, BIT(3), 0);

		if (btype_ibss)
			_rtl88ee_resume_tx_beacon(hw);
		break; }
	case HW_VAR_KEEP_ALIVE: {
		u8 array[2];

		array[0] = 0xff;
		array[1] = *((u8 *)val);
		rtl88e_fill_h2c_cmd(hw, H2C_88E_KEEP_ALIVE_CTRL,
				    2, array);
		break; }
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "switch case not process %x\n", variable);
		break;
	}
}

static bool _rtl88ee_llt_write(struct ieee80211_hw *hw, u32 address, u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	bool status = true;
	long count = 0;
	u32 value = _LLT_INIT_ADDR(address) | _LLT_INIT_DATA(data) |
		    _LLT_OP(_LLT_WRITE_ACCESS);

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

static bool _rtl88ee_llt_table_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	unsigned short i;
	u8 txpktbuf_bndy;
	u8 maxpage;
	bool status;

	maxpage = 0xAF;
	txpktbuf_bndy = 0xAB;

	rtl_write_byte(rtlpriv, REG_RQPN_NPQ, 0x01);
	rtl_write_dword(rtlpriv, REG_RQPN, 0x80730d29);

	/*0x2600   MaxRxBuff=10k-max(TxReportSize(64*8), WOLPattern(16*24)) */
	rtl_write_dword(rtlpriv, REG_TRXFF_BNDY, (0x25FF0000 | txpktbuf_bndy));
	rtl_write_byte(rtlpriv, REG_TDECTRL + 1, txpktbuf_bndy);

	rtl_write_byte(rtlpriv, REG_TXPKTBUF_BCNQ_BDNY, txpktbuf_bndy);
	rtl_write_byte(rtlpriv, REG_TXPKTBUF_MGQ_BDNY, txpktbuf_bndy);

	rtl_write_byte(rtlpriv, 0x45D, txpktbuf_bndy);
	rtl_write_byte(rtlpriv, REG_PBP, 0x11);
	rtl_write_byte(rtlpriv, REG_RX_DRVINFO_SZ, 0x4);

	for (i = 0; i < (txpktbuf_bndy - 1); i++) {
		status = _rtl88ee_llt_write(hw, i, i + 1);
		if (true != status)
			return status;
	}

	status = _rtl88ee_llt_write(hw, (txpktbuf_bndy - 1), 0xFF);
	if (true != status)
		return status;

	for (i = txpktbuf_bndy; i < maxpage; i++) {
		status = _rtl88ee_llt_write(hw, i, (i + 1));
		if (true != status)
			return status;
	}

	status = _rtl88ee_llt_write(hw, maxpage, txpktbuf_bndy);
	if (true != status)
		return status;

	return true;
}

static void _rtl88ee_gen_refresh_led_state(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_led *pLed0 = &(pcipriv->ledctl.sw_led0);

	if (rtlpriv->rtlhal.up_first_time)
		return;

	if (ppsc->rfoff_reason == RF_CHANGE_BY_IPS)
		rtl88ee_sw_led_on(hw, pLed0);
	else if (ppsc->rfoff_reason == RF_CHANGE_BY_INIT)
		rtl88ee_sw_led_on(hw, pLed0);
	else
		rtl88ee_sw_led_off(hw, pLed0);
}

static bool _rtl88ee_init_mac(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	u8 bytetmp;
	u16 wordtmp;

	/*Disable XTAL OUTPUT for power saving. YJ,add,111206. */
	bytetmp = rtl_read_byte(rtlpriv, REG_XCK_OUT_CTRL) & (~BIT(0));
	rtl_write_byte(rtlpriv, REG_XCK_OUT_CTRL, bytetmp);
	/*Auto Power Down to CHIP-off State*/
	bytetmp = rtl_read_byte(rtlpriv, REG_APS_FSMCO + 1) & (~BIT(7));
	rtl_write_byte(rtlpriv, REG_APS_FSMCO + 1, bytetmp);

	rtl_write_byte(rtlpriv, REG_RSV_CTRL, 0x00);
	/* HW Power on sequence */
	if (!rtl_hal_pwrseqcmdparsing(rtlpriv, PWR_CUT_ALL_MSK,
				      PWR_FAB_ALL_MSK, PWR_INTF_PCI_MSK,
				      RTL8188EE_NIC_ENABLE_FLOW)) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "init MAC Fail as rtl_hal_pwrseqcmdparsing\n");
		return false;
	}

	bytetmp = rtl_read_byte(rtlpriv, REG_APS_FSMCO) | BIT(4);
	rtl_write_byte(rtlpriv, REG_APS_FSMCO, bytetmp);

	bytetmp = rtl_read_byte(rtlpriv, REG_PCIE_CTRL_REG+2);
	rtl_write_byte(rtlpriv, REG_PCIE_CTRL_REG+2, bytetmp|BIT(2));

	bytetmp = rtl_read_byte(rtlpriv, REG_WATCH_DOG+1);
	rtl_write_byte(rtlpriv, REG_WATCH_DOG+1, bytetmp|BIT(7));

	bytetmp = rtl_read_byte(rtlpriv, REG_AFE_XTAL_CTRL_EXT+1);
	rtl_write_byte(rtlpriv, REG_AFE_XTAL_CTRL_EXT+1, bytetmp|BIT(1));

	bytetmp = rtl_read_byte(rtlpriv, REG_TX_RPT_CTRL);
	rtl_write_byte(rtlpriv, REG_TX_RPT_CTRL, bytetmp|BIT(1)|BIT(0));
	rtl_write_byte(rtlpriv, REG_TX_RPT_CTRL+1, 2);
	rtl_write_word(rtlpriv, REG_TX_RPT_TIME, 0xcdf0);

	/*Add for wake up online*/
	bytetmp = rtl_read_byte(rtlpriv, REG_SYS_CLKR);

	rtl_write_byte(rtlpriv, REG_SYS_CLKR, bytetmp|BIT(3));
	bytetmp = rtl_read_byte(rtlpriv, REG_GPIO_MUXCFG+1);
	rtl_write_byte(rtlpriv, REG_GPIO_MUXCFG+1, (bytetmp & (~BIT(4))));
	rtl_write_byte(rtlpriv, 0x367, 0x80);

	rtl_write_word(rtlpriv, REG_CR, 0x2ff);
	rtl_write_byte(rtlpriv, REG_CR+1, 0x06);
	rtl_write_byte(rtlpriv, REG_CR+2, 0x00);

	if (!rtlhal->mac_func_enable) {
		if (_rtl88ee_llt_table_init(hw) == false) {
			RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
				 "LLT table init fail\n");
			return false;
		}
	}
	rtl_write_dword(rtlpriv, REG_HISR, 0xffffffff);
	rtl_write_dword(rtlpriv, REG_HISRE, 0xffffffff);

	wordtmp = rtl_read_word(rtlpriv, REG_TRXDMA_CTRL);
	wordtmp &= 0xf;
	wordtmp |= 0xE771;
	rtl_write_word(rtlpriv, REG_TRXDMA_CTRL, wordtmp);

	rtl_write_dword(rtlpriv, REG_RCR, rtlpci->receive_config);
	rtl_write_word(rtlpriv, REG_RXFLTMAP2, 0xffff);
	rtl_write_dword(rtlpriv, REG_TCR, rtlpci->transmit_config);

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

	/* if we want to support 64 bit DMA, we should set it here,
	 * but now we do not support 64 bit DMA
	 */
	rtl_write_dword(rtlpriv, REG_INT_MIG, 0);

	rtl_write_dword(rtlpriv, REG_MCUTST_1, 0x0);
	rtl_write_byte(rtlpriv, REG_PCIE_CTRL_REG+1, 0);/*Enable RX DMA */

	if (rtlhal->earlymode_enable) {/*Early mode enable*/
		bytetmp = rtl_read_byte(rtlpriv, REG_EARLY_MODE_CONTROL);
		bytetmp |= 0x1f;
		rtl_write_byte(rtlpriv, REG_EARLY_MODE_CONTROL, bytetmp);
		rtl_write_byte(rtlpriv, REG_EARLY_MODE_CONTROL+3, 0x81);
	}
	_rtl88ee_gen_refresh_led_state(hw);
	return true;
}

static void _rtl88ee_hw_configure(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 reg_bw_opmode;
	u32 reg_ratr, reg_prsr;

	reg_bw_opmode = BW_OPMODE_20MHZ;
	reg_ratr = RATE_ALL_CCK | RATE_ALL_OFDM_AG |
	    RATE_ALL_OFDM_1SS | RATE_ALL_OFDM_2SS;
	reg_prsr = RATE_ALL_CCK | RATE_ALL_OFDM_AG;

	rtl_write_dword(rtlpriv, REG_RRSR, reg_prsr);
	rtl_write_byte(rtlpriv, REG_HWSEQ_CTRL, 0xFF);
}

static void _rtl88ee_enable_aspm_back_door(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	u8 tmp1byte = 0;
	u32 tmp4byte = 0, count = 0;

	rtl_write_word(rtlpriv, 0x354, 0x8104);
	rtl_write_word(rtlpriv, 0x358, 0x24);

	rtl_write_word(rtlpriv, 0x350, 0x70c);
	rtl_write_byte(rtlpriv, 0x352, 0x2);
	tmp1byte = rtl_read_byte(rtlpriv, 0x352);
	count = 0;
	while (tmp1byte && count < 20) {
		udelay(10);
		tmp1byte = rtl_read_byte(rtlpriv, 0x352);
		count++;
	}
	if (0 == tmp1byte) {
		tmp4byte = rtl_read_dword(rtlpriv, 0x34c);
		rtl_write_dword(rtlpriv, 0x348, tmp4byte|BIT(31));
		rtl_write_word(rtlpriv, 0x350, 0xf70c);
		rtl_write_byte(rtlpriv, 0x352, 0x1);
	}

	tmp1byte = rtl_read_byte(rtlpriv, 0x352);
	count = 0;
	while (tmp1byte && count < 20) {
		udelay(10);
		tmp1byte = rtl_read_byte(rtlpriv, 0x352);
		count++;
	}

	rtl_write_word(rtlpriv, 0x350, 0x718);
	rtl_write_byte(rtlpriv, 0x352, 0x2);
	tmp1byte = rtl_read_byte(rtlpriv, 0x352);
	count = 0;
	while (tmp1byte && count < 20) {
		udelay(10);
		tmp1byte = rtl_read_byte(rtlpriv, 0x352);
		count++;
	}

	if (ppsc->support_backdoor || (0 == tmp1byte)) {
		tmp4byte = rtl_read_dword(rtlpriv, 0x34c);
		rtl_write_dword(rtlpriv, 0x348, tmp4byte|BIT(11)|BIT(12));
		rtl_write_word(rtlpriv, 0x350, 0xf718);
		rtl_write_byte(rtlpriv, 0x352, 0x1);
	}

	tmp1byte = rtl_read_byte(rtlpriv, 0x352);
	count = 0;
	while (tmp1byte && count < 20) {
		udelay(10);
		tmp1byte = rtl_read_byte(rtlpriv, 0x352);
		count++;
	}
}

void rtl88ee_enable_hw_security_config(struct ieee80211_hw *hw)
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

	sec_reg_value = SCR_TXENCENABLE | SCR_RXDECENABLE;

	if (rtlpriv->sec.use_defaultkey) {
		sec_reg_value |= SCR_TXUSEDK;
		sec_reg_value |= SCR_RXUSEDK;
	}

	sec_reg_value |= (SCR_RXBCUSEDK | SCR_TXBCUSEDK);

	rtl_write_byte(rtlpriv, REG_CR + 1, 0x02);

	RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG,
		 "The SECR-value %x\n", sec_reg_value);

	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_WPA_CONFIG, &sec_reg_value);
}

int rtl88ee_hw_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	bool rtstatus = true;
	int err = 0;
	u8 tmp_u1b, u1byte;
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
	rtlhal->fw_ready = false;

	rtlpriv->intf_ops->disable_aspm(hw);

	tmp_u1b = rtl_read_byte(rtlpriv, REG_SYS_CLKR+1);
	u1byte = rtl_read_byte(rtlpriv, REG_CR);
	if ((tmp_u1b & BIT(3)) && (u1byte != 0 && u1byte != 0xEA)) {
		rtlhal->mac_func_enable = true;
	} else {
		rtlhal->mac_func_enable = false;
		rtlhal->fw_ps_state = FW_PS_STATE_ALL_ON_88E;
	}

	rtstatus = _rtl88ee_init_mac(hw);
	if (rtstatus != true) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "Init MAC failed\n");
		err = 1;
		goto exit;
	}

	err = rtl88e_download_fw(hw, false);
	if (err) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 "Failed to download FW. Init HW without FW now..\n");
		err = 1;
		goto exit;
	}
	rtlhal->fw_ready = true;
	/*fw related variable initialize */
	rtlhal->last_hmeboxnum = 0;
	rtlhal->fw_ps_state = FW_PS_STATE_ALL_ON_88E;
	rtlhal->fw_clk_change_in_progress = false;
	rtlhal->allow_sw_to_change_hwclc = false;
	ppsc->fw_current_inpsmode = false;

	rtl88e_phy_mac_config(hw);
	/* because last function modify RCR, so we update
	 * rcr var here, or TP will unstable for receive_config
	 * is wrong, RX RCR_ACRC32 will cause TP unstabel & Rx
	 * RCR_APP_ICV will cause mac80211 unassoc for cisco 1252
	 */
	rtlpci->receive_config &= ~(RCR_ACRC32 | RCR_AICV);
	rtl_write_dword(rtlpriv, REG_RCR, rtlpci->receive_config);

	rtl88e_phy_bb_config(hw);
	rtl_set_bbreg(hw, RFPGA0_RFMOD, BCCKEN, 0x1);
	rtl_set_bbreg(hw, RFPGA0_RFMOD, BOFDMEN, 0x1);

	rtlphy->rf_mode = RF_OP_BY_SW_3WIRE;
	rtl88e_phy_rf_config(hw);

	rtlphy->rfreg_chnlval[0] = rtl_get_rfreg(hw, (enum radio_path)0,
						 RF_CHNLBW, RFREG_OFFSET_MASK);
	rtlphy->rfreg_chnlval[0] = rtlphy->rfreg_chnlval[0] & 0xfff00fff;

	_rtl88ee_hw_configure(hw);
	rtl_cam_reset_all_entry(hw);
	rtl88ee_enable_hw_security_config(hw);

	rtlhal->mac_func_enable = true;
	ppsc->rfpwr_state = ERFON;

	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_ETHER_ADDR, mac->mac_addr);
	_rtl88ee_enable_aspm_back_door(hw);
	rtlpriv->intf_ops->enable_aspm(hw);

	if (ppsc->rfpwr_state == ERFON) {
		if ((rtlefuse->antenna_div_type == CGCS_RX_HW_ANTDIV) ||
		    ((rtlefuse->antenna_div_type == CG_TRX_HW_ANTDIV) &&
		     (rtlhal->oem_id == RT_CID_819X_HP))) {
			rtl88e_phy_set_rfpath_switch(hw, true);
			rtlpriv->dm.fat_table.rx_idle_ant = MAIN_ANT;
		} else {
			rtl88e_phy_set_rfpath_switch(hw, false);
			rtlpriv->dm.fat_table.rx_idle_ant = AUX_ANT;
		}
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "rx idle ant %s\n",
			 (rtlpriv->dm.fat_table.rx_idle_ant == MAIN_ANT) ?
			 ("MAIN_ANT") : ("AUX_ANT"));

		if (rtlphy->iqk_initialized) {
			rtl88e_phy_iq_calibrate(hw, true);
		} else {
			rtl88e_phy_iq_calibrate(hw, false);
			rtlphy->iqk_initialized = true;
		}

		rtl88e_dm_check_txpower_tracking(hw);
		rtl88e_phy_lc_calibrate(hw);
	}

	tmp_u1b = efuse_read_1byte(hw, 0x1FA);
	if (!(tmp_u1b & BIT(0))) {
		rtl_set_rfreg(hw, RF90_PATH_A, 0x15, 0x0F, 0x05);
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "PA BIAS path A\n");
	}

	if (!(tmp_u1b & BIT(4))) {
		tmp_u1b = rtl_read_byte(rtlpriv, 0x16);
		tmp_u1b &= 0x0F;
		rtl_write_byte(rtlpriv, 0x16, tmp_u1b | 0x80);
		udelay(10);
		rtl_write_byte(rtlpriv, 0x16, tmp_u1b | 0x90);
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "under 1.5V\n");
	}
	rtl_write_byte(rtlpriv, REG_NAV_CTRL+2,  ((30000+127)/128));
	rtl88e_dm_init(hw);
exit:
	local_irq_restore(flags);
	rtlpriv->rtlhal.being_init_adapter = false;
	return err;
}

static enum version_8188e _rtl88ee_read_chip_version(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	enum version_8188e version = VERSION_UNKNOWN;
	u32 value32;

	value32 = rtl_read_dword(rtlpriv, REG_SYS_CFG);
	if (value32 & TRP_VAUX_EN) {
		version = (enum version_8188e) VERSION_TEST_CHIP_88E;
	} else {
		version = NORMAL_CHIP;
		version = version | ((value32 & TYPE_ID) ? RF_TYPE_2T2R : 0);
		version = version | ((value32 & VENDOR_ID) ?
			  CHIP_VENDOR_UMC : 0);
	}

	rtlphy->rf_type = RF_1T1R;
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "Chip RF Type: %s\n", (rtlphy->rf_type == RF_2T2R) ?
		 "RF_2T2R" : "RF_1T1R");

	return version;
}

static int _rtl88ee_set_media_status(struct ieee80211_hw *hw,
				     enum nl80211_iftype type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 bt_msr = rtl_read_byte(rtlpriv, MSR) & 0xfc;
	enum led_ctl_mode ledaction = LED_CTL_NO_LINK;
	u8 mode = MSR_NOLINK;

	switch (type) {
	case NL80211_IFTYPE_UNSPECIFIED:
		mode = MSR_NOLINK;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "Set Network type to NO LINK!\n");
		break;
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_MESH_POINT:
		mode = MSR_ADHOC;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "Set Network type to Ad Hoc!\n");
		break;
	case NL80211_IFTYPE_STATION:
		mode = MSR_INFRA;
		ledaction = LED_CTL_LINK;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "Set Network type to STA!\n");
		break;
	case NL80211_IFTYPE_AP:
		mode = MSR_AP;
		ledaction = LED_CTL_LINK;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "Set Network type to AP!\n");
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "Network type %d not support!\n", type);
		return 1;
		break;
	}

	/* MSR_INFRA == Link in infrastructure network;
	 * MSR_ADHOC == Link in ad hoc network;
	 * Therefore, check link state is necessary.
	 *
	 * MSR_AP == AP mode; link state is not cared here.
	 */
	if (mode != MSR_AP && rtlpriv->mac80211.link_state < MAC80211_LINKED) {
		mode = MSR_NOLINK;
		ledaction = LED_CTL_NO_LINK;
	}

	if (mode == MSR_NOLINK || mode == MSR_INFRA) {
		_rtl88ee_stop_tx_beacon(hw);
		_rtl88ee_enable_bcn_sub_func(hw);
	} else if (mode == MSR_ADHOC || mode == MSR_AP) {
		_rtl88ee_resume_tx_beacon(hw);
		_rtl88ee_disable_bcn_sub_func(hw);
	} else {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 "Set HW_VAR_MEDIA_STATUS: No such media status(%x).\n",
			 mode);
	}

	rtl_write_byte(rtlpriv, (MSR), bt_msr | mode);
	rtlpriv->cfg->ops->led_control(hw, ledaction);
	if (mode == MSR_AP)
		rtl_write_byte(rtlpriv, REG_BCNTCFG + 1, 0x00);
	else
		rtl_write_byte(rtlpriv, REG_BCNTCFG + 1, 0x66);
	return 0;
}

void rtl88ee_set_check_bssid(struct ieee80211_hw *hw, bool check_bssid)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u32 reg_rcr = rtlpci->receive_config;

	if (rtlpriv->psc.rfpwr_state != ERFON)
		return;

	if (check_bssid == true) {
		reg_rcr |= (RCR_CBSSID_DATA | RCR_CBSSID_BCN);
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_RCR,
					      (u8 *)(&reg_rcr));
		_rtl88ee_set_bcn_ctrl_reg(hw, 0, BIT(4));
	} else if (check_bssid == false) {
		reg_rcr &= (~(RCR_CBSSID_DATA | RCR_CBSSID_BCN));
		_rtl88ee_set_bcn_ctrl_reg(hw, BIT(4), 0);
		rtlpriv->cfg->ops->set_hw_reg(hw,
			HW_VAR_RCR, (u8 *)(&reg_rcr));
	}

}

int rtl88ee_set_network_type(struct ieee80211_hw *hw,
			     enum nl80211_iftype type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (_rtl88ee_set_media_status(hw, type))
		return -EOPNOTSUPP;

	if (rtlpriv->mac80211.link_state == MAC80211_LINKED) {
		if (type != NL80211_IFTYPE_AP &&
		    type != NL80211_IFTYPE_MESH_POINT)
			rtl88ee_set_check_bssid(hw, true);
	} else {
		rtl88ee_set_check_bssid(hw, false);
	}

	return 0;
}

/* don't set REG_EDCA_BE_PARAM here
 * because mac80211 will send pkt when scan
 */
void rtl88ee_set_qos(struct ieee80211_hw *hw, int aci)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	rtl88e_dm_init_edca_turbo(hw);
	switch (aci) {
	case AC1_BK:
		rtl_write_dword(rtlpriv, REG_EDCA_BK_PARAM, 0xa44f);
		break;
	case AC0_BE:
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

static void rtl88ee_clear_interrupt(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 tmp;

	tmp = rtl_read_dword(rtlpriv, REG_HISR);
	rtl_write_dword(rtlpriv, REG_HISR, tmp);

	tmp = rtl_read_dword(rtlpriv, REG_HISRE);
	rtl_write_dword(rtlpriv, REG_HISRE, tmp);

	tmp = rtl_read_dword(rtlpriv, REG_HSISR);
	rtl_write_dword(rtlpriv, REG_HSISR, tmp);
}

void rtl88ee_enable_interrupt(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	rtl88ee_clear_interrupt(hw);/*clear it here first*/
	rtl_write_dword(rtlpriv, REG_HIMR,
			rtlpci->irq_mask[0] & 0xFFFFFFFF);
	rtl_write_dword(rtlpriv, REG_HIMRE,
			rtlpci->irq_mask[1] & 0xFFFFFFFF);
	rtlpci->irq_enabled = true;
	/* there are some C2H CMDs have been sent
	 * before system interrupt is enabled, e.g., C2H, CPWM.
	 * So we need to clear all C2H events that FW has notified,
	 * otherwise FW won't schedule any commands anymore.
	 */
	rtl_write_byte(rtlpriv, REG_C2HEVT_CLEAR, 0);
	/*enable system interrupt*/
	rtl_write_dword(rtlpriv, REG_HSIMR,
			rtlpci->sys_irq_mask & 0xFFFFFFFF);
}

void rtl88ee_disable_interrupt(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	rtl_write_dword(rtlpriv, REG_HIMR, IMR_DISABLED);
	rtl_write_dword(rtlpriv, REG_HIMRE, IMR_DISABLED);
	rtlpci->irq_enabled = false;
	/*synchronize_irq(rtlpci->pdev->irq);*/
}

static void _rtl88ee_poweroff_adapter(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 u1b_tmp;
	u32 count = 0;
	rtlhal->mac_func_enable = false;
	rtlpriv->intf_ops->enable_aspm(hw);

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "POWER OFF adapter\n");
	u1b_tmp = rtl_read_byte(rtlpriv, REG_TX_RPT_CTRL);
	rtl_write_byte(rtlpriv, REG_TX_RPT_CTRL, u1b_tmp & (~BIT(1)));

	u1b_tmp = rtl_read_byte(rtlpriv, REG_RXDMA_CONTROL);
	while (!(u1b_tmp & BIT(1)) && (count++ < 100)) {
		udelay(10);
		u1b_tmp = rtl_read_byte(rtlpriv, REG_RXDMA_CONTROL);
		count++;
	}
	rtl_write_byte(rtlpriv, REG_PCIE_CTRL_REG+1, 0xFF);

	rtl_hal_pwrseqcmdparsing(rtlpriv, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK,
				 PWR_INTF_PCI_MSK,
				 RTL8188EE_NIC_LPS_ENTER_FLOW);

	rtl_write_byte(rtlpriv, REG_RF_CTRL, 0x00);

	if ((rtl_read_byte(rtlpriv, REG_MCUFWDL) & BIT(7)) && rtlhal->fw_ready)
		rtl88e_firmware_selfreset(hw);

	u1b_tmp = rtl_read_byte(rtlpriv, REG_SYS_FUNC_EN+1);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN + 1, (u1b_tmp & (~BIT(2))));
	rtl_write_byte(rtlpriv, REG_MCUFWDL, 0x00);

	u1b_tmp = rtl_read_byte(rtlpriv, REG_32K_CTRL);
	rtl_write_byte(rtlpriv, REG_32K_CTRL, (u1b_tmp & (~BIT(0))));

	rtl_hal_pwrseqcmdparsing(rtlpriv, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK,
				 PWR_INTF_PCI_MSK, RTL8188EE_NIC_DISABLE_FLOW);

	u1b_tmp = rtl_read_byte(rtlpriv, REG_RSV_CTRL+1);
	rtl_write_byte(rtlpriv, REG_RSV_CTRL+1, (u1b_tmp & (~BIT(3))));
	u1b_tmp = rtl_read_byte(rtlpriv, REG_RSV_CTRL+1);
	rtl_write_byte(rtlpriv, REG_RSV_CTRL+1, (u1b_tmp | BIT(3)));

	rtl_write_byte(rtlpriv, REG_RSV_CTRL, 0x0E);

	u1b_tmp = rtl_read_byte(rtlpriv, GPIO_IN);
	rtl_write_byte(rtlpriv, GPIO_OUT, u1b_tmp);
	rtl_write_byte(rtlpriv, GPIO_IO_SEL, 0x7F);

	u1b_tmp = rtl_read_byte(rtlpriv, REG_GPIO_IO_SEL);
	rtl_write_byte(rtlpriv, REG_GPIO_IO_SEL, (u1b_tmp << 4) | u1b_tmp);
	u1b_tmp = rtl_read_byte(rtlpriv, REG_GPIO_IO_SEL+1);
	rtl_write_byte(rtlpriv, REG_GPIO_IO_SEL+1, u1b_tmp | 0x0F);

	rtl_write_dword(rtlpriv, REG_GPIO_IO_SEL_2+2, 0x00080808);
}

void rtl88ee_card_disable(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	enum nl80211_iftype opmode;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "RTL8188ee card disable\n");

	mac->link_state = MAC80211_NOLINK;
	opmode = NL80211_IFTYPE_UNSPECIFIED;

	_rtl88ee_set_media_status(hw, opmode);

	if (rtlpriv->rtlhal.driver_is_goingto_unload ||
	    ppsc->rfoff_reason > RF_CHANGE_BY_PS)
		rtlpriv->cfg->ops->led_control(hw, LED_CTL_POWER_OFF);

	RT_SET_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC);
	_rtl88ee_poweroff_adapter(hw);

	/* after power off we should do iqk again */
	rtlpriv->phy.iqk_initialized = false;
}

void rtl88ee_interrupt_recognized(struct ieee80211_hw *hw,
				  u32 *p_inta, u32 *p_intb)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	*p_inta = rtl_read_dword(rtlpriv, ISR) & rtlpci->irq_mask[0];
	rtl_write_dword(rtlpriv, ISR, *p_inta);

	*p_intb = rtl_read_dword(rtlpriv, REG_HISRE) & rtlpci->irq_mask[1];
	rtl_write_dword(rtlpriv, REG_HISRE, *p_intb);

}

void rtl88ee_set_beacon_related_registers(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u16 bcn_interval, atim_window;

	bcn_interval = mac->beacon_interval;
	atim_window = 2;	/*FIX MERGE */
	rtl88ee_disable_interrupt(hw);
	rtl_write_word(rtlpriv, REG_ATIMWND, atim_window);
	rtl_write_word(rtlpriv, REG_BCN_INTERVAL, bcn_interval);
	rtl_write_word(rtlpriv, REG_BCNTCFG, 0x660f);
	rtl_write_byte(rtlpriv, REG_RXTSF_OFFSET_CCK, 0x18);
	rtl_write_byte(rtlpriv, REG_RXTSF_OFFSET_OFDM, 0x18);
	rtl_write_byte(rtlpriv, 0x606, 0x30);
	rtlpci->reg_bcn_ctrl_val |= BIT(3);
	rtl_write_byte(rtlpriv, REG_BCN_CTRL, (u8) rtlpci->reg_bcn_ctrl_val);
	/*rtl88ee_enable_interrupt(hw);*/
}

void rtl88ee_set_beacon_interval(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u16 bcn_interval = mac->beacon_interval;

	RT_TRACE(rtlpriv, COMP_BEACON, DBG_DMESG,
		 "beacon_interval:%d\n", bcn_interval);
	/*rtl88ee_disable_interrupt(hw);*/
	rtl_write_word(rtlpriv, REG_BCN_INTERVAL, bcn_interval);
	/*rtl88ee_enable_interrupt(hw);*/
}

void rtl88ee_update_interrupt_mask(struct ieee80211_hw *hw,
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
	rtl88ee_disable_interrupt(hw);
	rtl88ee_enable_interrupt(hw);
}

static u8 _rtl88e_get_chnl_group(u8 chnl)
{
	u8 group = 0;

	if (chnl < 3)
		group = 0;
	else if (chnl < 6)
		group = 1;
	else if (chnl < 9)
		group = 2;
	else if (chnl < 12)
		group = 3;
	else if (chnl < 14)
		group = 4;
	else if (chnl == 14)
		group = 5;

	return group;
}

static void set_24g_base(struct txpower_info_2g *pwrinfo24g, u32 rfpath)
{
	int group, txcnt;

	for (group = 0 ; group < MAX_CHNL_GROUP_24G; group++) {
		pwrinfo24g->index_cck_base[rfpath][group] = 0x2D;
		pwrinfo24g->index_bw40_base[rfpath][group] = 0x2D;
	}
	for (txcnt = 0; txcnt < MAX_TX_COUNT; txcnt++) {
		if (txcnt == 0) {
			pwrinfo24g->bw20_diff[rfpath][0] = 0x02;
			pwrinfo24g->ofdm_diff[rfpath][0] = 0x04;
		} else {
			pwrinfo24g->bw20_diff[rfpath][txcnt] = 0xFE;
			pwrinfo24g->bw40_diff[rfpath][txcnt] = 0xFE;
			pwrinfo24g->cck_diff[rfpath][txcnt] =	0xFE;
			pwrinfo24g->ofdm_diff[rfpath][txcnt] = 0xFE;
		}
	}
}

static void read_power_value_fromprom(struct ieee80211_hw *hw,
				      struct txpower_info_2g *pwrinfo24g,
				      struct txpower_info_5g *pwrinfo5g,
				      bool autoload_fail, u8 *hwinfo)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 rfpath, eeaddr = EEPROM_TX_PWR_INX, group, txcnt = 0;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "hal_ReadPowerValueFromPROM88E():PROMContent[0x%x]=0x%x\n",
		 (eeaddr+1), hwinfo[eeaddr+1]);
	if (0xFF == hwinfo[eeaddr+1])  /*YJ,add,120316*/
		autoload_fail = true;

	if (autoload_fail) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "auto load fail : Use Default value!\n");
		for (rfpath = 0 ; rfpath < MAX_RF_PATH ; rfpath++) {
			/* 2.4G default value */
			set_24g_base(pwrinfo24g, rfpath);
		}
		return;
	}

	for (rfpath = 0 ; rfpath < MAX_RF_PATH ; rfpath++) {
		/*2.4G default value*/
		for (group = 0 ; group < MAX_CHNL_GROUP_24G; group++) {
			pwrinfo24g->index_cck_base[rfpath][group] =
			  hwinfo[eeaddr++];
			if (pwrinfo24g->index_cck_base[rfpath][group] == 0xFF)
				pwrinfo24g->index_cck_base[rfpath][group] =
				  0x2D;
		}
		for (group = 0 ; group < MAX_CHNL_GROUP_24G-1; group++) {
			pwrinfo24g->index_bw40_base[rfpath][group] =
				hwinfo[eeaddr++];
			if (pwrinfo24g->index_bw40_base[rfpath][group] == 0xFF)
				pwrinfo24g->index_bw40_base[rfpath][group] =
					0x2D;
		}
		pwrinfo24g->bw40_diff[rfpath][0] = 0;
		if (hwinfo[eeaddr] == 0xFF) {
			pwrinfo24g->bw20_diff[rfpath][0] = 0x02;
		} else {
			pwrinfo24g->bw20_diff[rfpath][0] =
				(hwinfo[eeaddr]&0xf0)>>4;
			/*bit sign number to 8 bit sign number*/
			if (pwrinfo24g->bw20_diff[rfpath][0] & BIT(3))
				pwrinfo24g->bw20_diff[rfpath][0] |= 0xF0;
		}

		if (hwinfo[eeaddr] == 0xFF) {
			pwrinfo24g->ofdm_diff[rfpath][0] = 0x04;
		} else {
			pwrinfo24g->ofdm_diff[rfpath][0] =
				(hwinfo[eeaddr]&0x0f);
				/*bit sign number to 8 bit sign number*/
			if (pwrinfo24g->ofdm_diff[rfpath][0] & BIT(3))
				pwrinfo24g->ofdm_diff[rfpath][0] |= 0xF0;
		}
		pwrinfo24g->cck_diff[rfpath][0] = 0;
		eeaddr++;
		for (txcnt = 1; txcnt < MAX_TX_COUNT; txcnt++) {
			if (hwinfo[eeaddr] == 0xFF) {
				pwrinfo24g->bw40_diff[rfpath][txcnt] = 0xFE;
			} else {
				pwrinfo24g->bw40_diff[rfpath][txcnt] =
				  (hwinfo[eeaddr]&0xf0)>>4;
				if (pwrinfo24g->bw40_diff[rfpath][txcnt] &
				    BIT(3))
					pwrinfo24g->bw40_diff[rfpath][txcnt] |=
					  0xF0;
			}

			if (hwinfo[eeaddr] == 0xFF) {
				pwrinfo24g->bw20_diff[rfpath][txcnt] =
					0xFE;
			} else {
				pwrinfo24g->bw20_diff[rfpath][txcnt] =
				  (hwinfo[eeaddr]&0x0f);
				if (pwrinfo24g->bw20_diff[rfpath][txcnt] &
				    BIT(3))
					pwrinfo24g->bw20_diff[rfpath][txcnt] |=
					  0xF0;
			}
			eeaddr++;

			if (hwinfo[eeaddr] == 0xFF) {
				pwrinfo24g->ofdm_diff[rfpath][txcnt] = 0xFE;
			} else {
				pwrinfo24g->ofdm_diff[rfpath][txcnt] =
				  (hwinfo[eeaddr]&0xf0)>>4;
				if (pwrinfo24g->ofdm_diff[rfpath][txcnt] &
				    BIT(3))
					pwrinfo24g->ofdm_diff[rfpath][txcnt] |=
					  0xF0;
			}

			if (hwinfo[eeaddr] == 0xFF) {
				pwrinfo24g->cck_diff[rfpath][txcnt] =	0xFE;
			} else {
				pwrinfo24g->cck_diff[rfpath][txcnt] =
				  (hwinfo[eeaddr]&0x0f);
				if (pwrinfo24g->cck_diff[rfpath][txcnt] &
				    BIT(3))
					pwrinfo24g->cck_diff[rfpath][txcnt] |=
					  0xF0;
			}
			eeaddr++;
		}

		/*5G default value*/
		for (group = 0 ; group < MAX_CHNL_GROUP_5G; group++) {
			pwrinfo5g->index_bw40_base[rfpath][group] =
				hwinfo[eeaddr++];
			if (pwrinfo5g->index_bw40_base[rfpath][group] == 0xFF)
				pwrinfo5g->index_bw40_base[rfpath][group] =
				  0xFE;
		}

		pwrinfo5g->bw40_diff[rfpath][0] = 0;

		if (hwinfo[eeaddr] == 0xFF) {
			pwrinfo5g->bw20_diff[rfpath][0] = 0;
		} else {
			pwrinfo5g->bw20_diff[rfpath][0] =
			  (hwinfo[eeaddr]&0xf0)>>4;
			if (pwrinfo5g->bw20_diff[rfpath][0] & BIT(3))
				pwrinfo5g->bw20_diff[rfpath][0] |= 0xF0;
		}

		if (hwinfo[eeaddr] == 0xFF) {
			pwrinfo5g->ofdm_diff[rfpath][0] = 0x04;
		} else {
			pwrinfo5g->ofdm_diff[rfpath][0] = (hwinfo[eeaddr]&0x0f);
			if (pwrinfo5g->ofdm_diff[rfpath][0] & BIT(3))
				pwrinfo5g->ofdm_diff[rfpath][0] |= 0xF0;
		}
		eeaddr++;
		for (txcnt = 1; txcnt < MAX_TX_COUNT; txcnt++) {
			if (hwinfo[eeaddr] == 0xFF) {
				pwrinfo5g->bw40_diff[rfpath][txcnt] =	0xFE;
			} else {
				pwrinfo5g->bw40_diff[rfpath][txcnt] =
				  (hwinfo[eeaddr]&0xf0)>>4;
				if (pwrinfo5g->bw40_diff[rfpath][txcnt] &
				    BIT(3))
					pwrinfo5g->bw40_diff[rfpath][txcnt] |=
					  0xF0;
			}

			if (hwinfo[eeaddr] == 0xFF) {
				pwrinfo5g->bw20_diff[rfpath][txcnt] =	0xFE;
			} else {
				pwrinfo5g->bw20_diff[rfpath][txcnt] =
				  (hwinfo[eeaddr]&0x0f);
				if (pwrinfo5g->bw20_diff[rfpath][txcnt] &
				    BIT(3))
					pwrinfo5g->bw20_diff[rfpath][txcnt] |=
					  0xF0;
			}
			eeaddr++;
		}

		if (hwinfo[eeaddr] == 0xFF) {
			pwrinfo5g->ofdm_diff[rfpath][1] = 0xFE;
			pwrinfo5g->ofdm_diff[rfpath][2] = 0xFE;
		} else {
			pwrinfo5g->ofdm_diff[rfpath][1] =
					(hwinfo[eeaddr]&0xf0)>>4;
			pwrinfo5g->ofdm_diff[rfpath][2] =
					(hwinfo[eeaddr]&0x0f);
		}
		eeaddr++;

		if (hwinfo[eeaddr] == 0xFF)
			pwrinfo5g->ofdm_diff[rfpath][3] = 0xFE;
		else
			pwrinfo5g->ofdm_diff[rfpath][3] = (hwinfo[eeaddr]&0x0f);
		eeaddr++;

		for (txcnt = 1; txcnt < MAX_TX_COUNT; txcnt++) {
			if (pwrinfo5g->ofdm_diff[rfpath][txcnt] == 0xFF)
				pwrinfo5g->ofdm_diff[rfpath][txcnt] =	0xFE;
			else if (pwrinfo5g->ofdm_diff[rfpath][txcnt] & BIT(3))
				pwrinfo5g->ofdm_diff[rfpath][txcnt] |= 0xF0;
		}
	}
}

static void _rtl88ee_read_txpower_info_from_hwpg(struct ieee80211_hw *hw,
						 bool autoload_fail,
						 u8 *hwinfo)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct txpower_info_2g pwrinfo24g;
	struct txpower_info_5g pwrinfo5g;
	u8 rf_path, index;
	u8 i;

	read_power_value_fromprom(hw, &pwrinfo24g,
				  &pwrinfo5g, autoload_fail, hwinfo);

	for (rf_path = 0; rf_path < 2; rf_path++) {
		for (i = 0; i < 14; i++) {
			index = _rtl88e_get_chnl_group(i+1);

			rtlefuse->txpwrlevel_cck[rf_path][i] =
				pwrinfo24g.index_cck_base[rf_path][index];
			rtlefuse->txpwrlevel_ht40_1s[rf_path][i] =
				pwrinfo24g.index_bw40_base[rf_path][index];
			rtlefuse->txpwr_ht20diff[rf_path][i] =
				pwrinfo24g.bw20_diff[rf_path][0];
			rtlefuse->txpwr_legacyhtdiff[rf_path][i] =
				pwrinfo24g.ofdm_diff[rf_path][0];
		}

		for (i = 0; i < 14; i++) {
			RTPRINT(rtlpriv, FINIT, INIT_TXPOWER,
				"RF(%d)-Ch(%d) [CCK / HT40_1S ] = [0x%x / 0x%x ]\n",
				rf_path, i,
				rtlefuse->txpwrlevel_cck[rf_path][i],
				rtlefuse->txpwrlevel_ht40_1s[rf_path][i]);
		}
	}

	if (!autoload_fail)
		rtlefuse->eeprom_thermalmeter =
			hwinfo[EEPROM_THERMAL_METER_88E];
	else
		rtlefuse->eeprom_thermalmeter = EEPROM_DEFAULT_THERMALMETER;

	if (rtlefuse->eeprom_thermalmeter == 0xff || autoload_fail) {
		rtlefuse->apk_thermalmeterignore = true;
		rtlefuse->eeprom_thermalmeter = EEPROM_DEFAULT_THERMALMETER;
	}

	rtlefuse->thermalmeter[0] = rtlefuse->eeprom_thermalmeter;
	RTPRINT(rtlpriv, FINIT, INIT_TXPOWER,
		"thermalmeter = 0x%x\n", rtlefuse->eeprom_thermalmeter);

	if (!autoload_fail) {
		rtlefuse->eeprom_regulatory =
			hwinfo[EEPROM_RF_BOARD_OPTION_88E] & 0x07;/*bit0~2*/
		if (hwinfo[EEPROM_RF_BOARD_OPTION_88E] == 0xFF)
			rtlefuse->eeprom_regulatory = 0;
	} else {
		rtlefuse->eeprom_regulatory = 0;
	}
	RTPRINT(rtlpriv, FINIT, INIT_TXPOWER,
		"eeprom_regulatory = 0x%x\n", rtlefuse->eeprom_regulatory);
}

static void _rtl88ee_read_adapter_info(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u16 i, usvalue;
	u8 hwinfo[HWSET_MAX_SIZE];
	u16 eeprom_id;

	if (rtlefuse->epromtype == EEPROM_BOOT_EFUSE) {
		rtl_efuse_shadow_map_update(hw);

		memcpy(hwinfo, &rtlefuse->efuse_map[EFUSE_INIT_MAP][0],
		       HWSET_MAX_SIZE);
	} else if (rtlefuse->epromtype == EEPROM_93C46) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "RTL819X Not boot from eeprom, check it !!");
		return;
	} else {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "boot from neither eeprom nor efuse, check it !!");
		return;
	}

	RT_PRINT_DATA(rtlpriv, COMP_INIT, DBG_DMESG, "MAP\n",
		      hwinfo, HWSET_MAX_SIZE);

	eeprom_id = *((u16 *)&hwinfo[0]);
	if (eeprom_id != RTL8188E_EEPROM_ID) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 "EEPROM ID(%#x) is invalid!!\n", eeprom_id);
		rtlefuse->autoload_failflag = true;
	} else {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "Autoload OK\n");
		rtlefuse->autoload_failflag = false;
	}

	if (rtlefuse->autoload_failflag == true)
		return;
	/*VID DID SVID SDID*/
	rtlefuse->eeprom_vid = *(u16 *)&hwinfo[EEPROM_VID];
	rtlefuse->eeprom_did = *(u16 *)&hwinfo[EEPROM_DID];
	rtlefuse->eeprom_svid = *(u16 *)&hwinfo[EEPROM_SVID];
	rtlefuse->eeprom_smid = *(u16 *)&hwinfo[EEPROM_SMID];
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
	/*customer ID*/
	rtlefuse->eeprom_oemid = hwinfo[EEPROM_CUSTOMER_ID];
	if (rtlefuse->eeprom_oemid == 0xFF)
		rtlefuse->eeprom_oemid =	0;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "EEPROM Customer ID: 0x%2x\n", rtlefuse->eeprom_oemid);
	/*EEPROM version*/
	rtlefuse->eeprom_version = *(u16 *)&hwinfo[EEPROM_VERSION];
	/*mac address*/
	for (i = 0; i < 6; i += 2) {
		usvalue = *(u16 *)&hwinfo[EEPROM_MAC_ADDR + i];
		*((u16 *)(&rtlefuse->dev_addr[i])) = usvalue;
	}

	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
		 "dev_addr: %pM\n", rtlefuse->dev_addr);
	/*channel plan */
	rtlefuse->eeprom_channelplan = hwinfo[EEPROM_CHANNELPLAN];
	/* set channel paln to world wide 13 */
	rtlefuse->channel_plan = COUNTRY_CODE_WORLD_WIDE_13;
	/*tx power*/
	_rtl88ee_read_txpower_info_from_hwpg(hw,
					     rtlefuse->autoload_failflag,
					     hwinfo);
	rtlefuse->txpwr_fromeprom = true;

	rtl8188ee_read_bt_coexist_info_from_hwpg(hw,
						 rtlefuse->autoload_failflag,
						 hwinfo);

	/*board type*/
	rtlefuse->board_type =
		((hwinfo[EEPROM_RF_BOARD_OPTION_88E] & 0xE0) >> 5);
	rtlhal->board_type = rtlefuse->board_type;
	/*Wake on wlan*/
	rtlefuse->wowlan_enable =
		((hwinfo[EEPROM_RF_FEATURE_OPTION_88E] & 0x40) >> 6);
	/*parse xtal*/
	rtlefuse->crystalcap = hwinfo[EEPROM_XTAL_88E];
	if (hwinfo[EEPROM_XTAL_88E])
		rtlefuse->crystalcap = 0x20;
	/*antenna diversity*/
	rtlefuse->antenna_div_cfg =
		(hwinfo[EEPROM_RF_BOARD_OPTION_88E] & 0x18) >> 3;
	if (hwinfo[EEPROM_RF_BOARD_OPTION_88E] == 0xFF)
		rtlefuse->antenna_div_cfg = 0;
	if (rtlpriv->btcoexist.eeprom_bt_coexist != 0 &&
	    rtlpriv->btcoexist.eeprom_bt_ant_num == ANT_X1)
		rtlefuse->antenna_div_cfg = 0;

	rtlefuse->antenna_div_type = hwinfo[EEPROM_RF_ANTENNA_OPT_88E];
	if (rtlefuse->antenna_div_type == 0xFF)
		rtlefuse->antenna_div_type = 0x01;
	if (rtlefuse->antenna_div_type == CG_TRX_HW_ANTDIV ||
		rtlefuse->antenna_div_type == CGCS_RX_HW_ANTDIV)
		rtlefuse->antenna_div_cfg = 1;

	if (rtlhal->oem_id == RT_CID_DEFAULT) {
		switch (rtlefuse->eeprom_oemid) {
		case EEPROM_CID_DEFAULT:
			if (rtlefuse->eeprom_did == 0x8179) {
				if (rtlefuse->eeprom_svid == 0x1025) {
					rtlhal->oem_id = RT_CID_819X_ACER;
				} else if ((rtlefuse->eeprom_svid == 0x10EC &&
				     rtlefuse->eeprom_smid == 0x0179) ||
				     (rtlefuse->eeprom_svid == 0x17AA &&
				     rtlefuse->eeprom_smid == 0x0179)) {
					rtlhal->oem_id = RT_CID_819X_LENOVO;
				} else if (rtlefuse->eeprom_svid == 0x103c &&
					   rtlefuse->eeprom_smid == 0x197d) {
					rtlhal->oem_id = RT_CID_819X_HP;
				} else {
					rtlhal->oem_id = RT_CID_DEFAULT;
				}
			} else {
				rtlhal->oem_id = RT_CID_DEFAULT;
			}
			break;
		case EEPROM_CID_TOSHIBA:
			rtlhal->oem_id = RT_CID_TOSHIBA;
			break;
		case EEPROM_CID_QMI:
			rtlhal->oem_id = RT_CID_819X_QMI;
			break;
		case EEPROM_CID_WHQL:
		default:
			rtlhal->oem_id = RT_CID_DEFAULT;
			break;

		}
	}
}

static void _rtl88ee_hal_customized_behavior(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	pcipriv->ledctl.led_opendrain = true;

	switch (rtlhal->oem_id) {
	case RT_CID_819X_HP:
		pcipriv->ledctl.led_opendrain = true;
		break;
	case RT_CID_819X_LENOVO:
	case RT_CID_DEFAULT:
	case RT_CID_TOSHIBA:
	case RT_CID_CCX:
	case RT_CID_819X_ACER:
	case RT_CID_WHQL:
	default:
		break;
	}
	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
		 "RT Customized ID: 0x%02X\n", rtlhal->oem_id);
}

void rtl88ee_read_eeprom_info(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 tmp_u1b;

	rtlhal->version = _rtl88ee_read_chip_version(hw);
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
		_rtl88ee_read_adapter_info(hw);
	} else {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "Autoload ERR!!\n");
	}
	_rtl88ee_hal_customized_behavior(hw);
}

static void rtl88ee_update_hal_rate_table(struct ieee80211_hw *hw,
		struct ieee80211_sta *sta)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u32 ratr_value;
	u8 ratr_index = 0;
	u8 b_nmode = mac->ht_enable;
	/*u8 mimo_ps = IEEE80211_SMPS_OFF;*/
	u16 shortgi_rate;
	u32 tmp_ratr_value;
	u8 curtxbw_40mhz = mac->bw_40;
	u8 curshortgi_40mhz = (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_40) ?
				1 : 0;
	u8 curshortgi_20mhz = (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_20) ?
				1 : 0;
	enum wireless_mode wirelessmode = mac->mode;
	u32 ratr_mask;

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
		b_nmode = 1;
		if (get_rf_type(rtlphy) == RF_1T2R ||
		    get_rf_type(rtlphy) == RF_1T1R)
			ratr_mask = 0x000ff005;
		else
			ratr_mask = 0x0f0ff005;

		ratr_value &= ratr_mask;
		break;
	default:
		if (rtlphy->rf_type == RF_1T2R)
			ratr_value &= 0x000ff0ff;
		else
			ratr_value &= 0x0f0ff0ff;

		break;
	}

	if ((rtlpriv->btcoexist.bt_coexistence) &&
	    (rtlpriv->btcoexist.bt_coexist_type == BT_CSR_BC4) &&
	    (rtlpriv->btcoexist.bt_cur_state) &&
	    (rtlpriv->btcoexist.bt_ant_isolation) &&
	    ((rtlpriv->btcoexist.bt_service == BT_SCO) ||
	     (rtlpriv->btcoexist.bt_service == BT_BUSY)))
		ratr_value &= 0x0fffcfc0;
	else
		ratr_value &= 0x0FFFFFFF;

	if (b_nmode &&
	    ((curtxbw_40mhz && curshortgi_40mhz) ||
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

	RT_TRACE(rtlpriv, COMP_RATR, DBG_DMESG,
		 "%x\n", rtl_read_dword(rtlpriv, REG_ARFR0));
}

static void rtl88ee_update_hal_rate_mask(struct ieee80211_hw *hw,
		struct ieee80211_sta *sta, u8 rssi_level)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_sta_info *sta_entry = NULL;
	u32 ratr_bitmap;
	u8 ratr_index;
	u8 curtxbw_40mhz = (sta->ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40)
				? 1 : 0;
	u8 curshortgi_40mhz = (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_40) ?
				1 : 0;
	u8 curshortgi_20mhz = (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_20) ?
				1 : 0;
	enum wireless_mode wirelessmode = 0;
	bool b_shortgi = false;
	u8 rate_mask[5];
	u8 macid = 0;
	/*u8 mimo_ps = IEEE80211_SMPS_OFF;*/

	sta_entry = (struct rtl_sta_info *)sta->drv_priv;
	wirelessmode = sta_entry->wireless_mode;
	if (mac->opmode == NL80211_IFTYPE_STATION ||
		mac->opmode == NL80211_IFTYPE_MESH_POINT)
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
	case WIRELESS_MODE_N_24G:
	case WIRELESS_MODE_N_5G:
		ratr_index = RATR_INX_WIRELESS_NGB;
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
					ratr_bitmap &= 0x0f8f0000;
				else if (rssi_level == 2)
					ratr_bitmap &= 0x0f8ff000;
				else
					ratr_bitmap &= 0x0f8ff015;
			} else {
				if (rssi_level == 1)
					ratr_bitmap &= 0x0f8f0000;
				else if (rssi_level == 2)
					ratr_bitmap &= 0x0f8ff000;
				else
					ratr_bitmap &= 0x0f8ff005;
			}
		}
		/*}*/

		if ((curtxbw_40mhz && curshortgi_40mhz) ||
		    (!curtxbw_40mhz && curshortgi_20mhz)) {

			if (macid == 0)
				b_shortgi = true;
			else if (macid == 1)
				b_shortgi = false;
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
	*(u32 *)&rate_mask = (ratr_bitmap & 0x0fffffff) |
			     (ratr_index << 28);
	rate_mask[4] = macid | (b_shortgi ? 0x20 : 0x00) | 0x80;
	RT_TRACE(rtlpriv, COMP_RATR, DBG_DMESG,
		 "Rate_index:%x, ratr_val:%x, %x:%x:%x:%x:%x\n",
		 ratr_index, ratr_bitmap,
		 rate_mask[0], rate_mask[1],
		 rate_mask[2], rate_mask[3],
		 rate_mask[4]);
	rtl88e_fill_h2c_cmd(hw, H2C_88E_RA_MASK, 5, rate_mask);
	_rtl88ee_set_bcn_ctrl_reg(hw, BIT(3), 0);
}

void rtl88ee_update_hal_rate_tbl(struct ieee80211_hw *hw,
		struct ieee80211_sta *sta, u8 rssi_level)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (rtlpriv->dm.useramask)
		rtl88ee_update_hal_rate_mask(hw, sta, rssi_level);
	else
		rtl88ee_update_hal_rate_table(hw, sta);
}

void rtl88ee_update_channel_access_setting(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u16 sifs_timer;

	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_SLOT_TIME, &mac->slot_time);
	if (!mac->ht_enable)
		sifs_timer = 0x0a0a;
	else
		sifs_timer = 0x0e0e;
	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_SIFS, (u8 *)&sifs_timer);
}

bool rtl88ee_gpio_radio_on_off_checking(struct ieee80211_hw *hw, u8 *valid)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	enum rf_pwrstate e_rfpowerstate_toset, cur_rfstate;
	u32 u4tmp;
	bool b_actuallyset = false;

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

	cur_rfstate = ppsc->rfpwr_state;

	u4tmp = rtl_read_dword(rtlpriv, REG_GPIO_OUTPUT);
	e_rfpowerstate_toset = (u4tmp & BIT(31)) ? ERFON : ERFOFF;

	if (ppsc->hwradiooff && (e_rfpowerstate_toset == ERFON)) {
		RT_TRACE(rtlpriv, COMP_RF, DBG_DMESG,
			 "GPIOChangeRF  - HW Radio ON, RF ON\n");

		e_rfpowerstate_toset = ERFON;
		ppsc->hwradiooff = false;
		b_actuallyset = true;
	} else if ((!ppsc->hwradiooff) &&
		   (e_rfpowerstate_toset == ERFOFF)) {
		RT_TRACE(rtlpriv, COMP_RF, DBG_DMESG,
			 "GPIOChangeRF  - HW Radio OFF, RF OFF\n");

		e_rfpowerstate_toset = ERFOFF;
		ppsc->hwradiooff = true;
		b_actuallyset = true;
	}

	if (b_actuallyset) {
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

void rtl88ee_set_key(struct ieee80211_hw *hw, u32 key_index,
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
				 "switch case not process\n");
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
				if (mac->opmode == NL80211_IFTYPE_AP ||
				    mac->opmode == NL80211_IFTYPE_MESH_POINT) {
					entry_id =
					  rtl_cam_get_free_entry(hw, p_macaddr);
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
			if (mac->opmode == NL80211_IFTYPE_AP ||
				mac->opmode == NL80211_IFTYPE_MESH_POINT)
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

static void rtl8188ee_bt_var_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpriv->btcoexist.bt_coexistence =
		rtlpriv->btcoexist.eeprom_bt_coexist;
	rtlpriv->btcoexist.bt_ant_num = rtlpriv->btcoexist.eeprom_bt_ant_num;
	rtlpriv->btcoexist.bt_coexist_type = rtlpriv->btcoexist.eeprom_bt_type;

	if (rtlpriv->btcoexist.reg_bt_iso == 2)
		rtlpriv->btcoexist.bt_ant_isolation =
				rtlpriv->btcoexist.eeprom_bt_ant_isol;
	else
		rtlpriv->btcoexist.bt_ant_isolation =
				rtlpriv->btcoexist.reg_bt_iso;

	rtlpriv->btcoexist.bt_radio_shared_type =
		rtlpriv->btcoexist.eeprom_bt_radio_shared;

	if (rtlpriv->btcoexist.bt_coexistence) {
		if (rtlpriv->btcoexist.reg_bt_sco == 1)
			rtlpriv->btcoexist.bt_service = BT_OTHER_ACTION;
		else if (rtlpriv->btcoexist.reg_bt_sco == 2)
			rtlpriv->btcoexist.bt_service = BT_SCO;
		else if (rtlpriv->btcoexist.reg_bt_sco == 4)
			rtlpriv->btcoexist.bt_service = BT_BUSY;
		else if (rtlpriv->btcoexist.reg_bt_sco == 5)
			rtlpriv->btcoexist.bt_service = BT_OTHERBUSY;
		else
			rtlpriv->btcoexist.bt_service = BT_IDLE;

		rtlpriv->btcoexist.bt_edca_ul = 0;
		rtlpriv->btcoexist.bt_edca_dl = 0;
		rtlpriv->btcoexist.bt_rssi_state = 0xff;
	}
}

void rtl8188ee_read_bt_coexist_info_from_hwpg(struct ieee80211_hw *hw,
					      bool auto_load_fail, u8 *hwinfo)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 value;

	if (!auto_load_fail) {
		rtlpriv->btcoexist.eeprom_bt_coexist =
			((hwinfo[EEPROM_RF_FEATURE_OPTION_88E] & 0xe0) >> 5);
		if (hwinfo[EEPROM_RF_FEATURE_OPTION_88E] == 0xFF)
			rtlpriv->btcoexist.eeprom_bt_coexist  = 0;
		value = hwinfo[EEPROM_RF_BT_SETTING_88E];
		rtlpriv->btcoexist.eeprom_bt_type = ((value & 0xe) >> 1);
		rtlpriv->btcoexist.eeprom_bt_ant_num = (value & 0x1);
		rtlpriv->btcoexist.eeprom_bt_ant_isol = ((value & 0x10) >> 4);
		rtlpriv->btcoexist.eeprom_bt_radio_shared =
				 ((value & 0x20) >> 5);
	} else {
		rtlpriv->btcoexist.eeprom_bt_coexist = 0;
		rtlpriv->btcoexist.eeprom_bt_type = BT_2WIRE;
		rtlpriv->btcoexist.eeprom_bt_ant_num = ANT_X2;
		rtlpriv->btcoexist.eeprom_bt_ant_isol = 0;
		rtlpriv->btcoexist.eeprom_bt_radio_shared = BT_RADIO_SHARED;
	}

	rtl8188ee_bt_var_init(hw);
}

void rtl8188ee_bt_reg_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	/* 0:Low, 1:High, 2:From Efuse. */
	rtlpriv->btcoexist.reg_bt_iso = 2;
	/* 0:Idle, 1:None-SCO, 2:SCO, 3:From Counter. */
	rtlpriv->btcoexist.reg_bt_sco = 3;
	/* 0:Disable BT control A-MPDU, 1:Enable BT control A-MPDU. */
	rtlpriv->btcoexist.reg_bt_sco = 0;
}

void rtl8188ee_bt_hw_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 u1_tmp;

	if (rtlpriv->btcoexist.bt_coexistence &&
	    ((rtlpriv->btcoexist.bt_coexist_type == BT_CSR_BC4) ||
	      rtlpriv->btcoexist.bt_coexist_type == BT_CSR_BC8)) {
		if (rtlpriv->btcoexist.bt_ant_isolation)
			rtl_write_byte(rtlpriv, REG_GPIO_MUXCFG, 0xa0);

		u1_tmp = rtl_read_byte(rtlpriv, 0x4fd) &
			 BIT_OFFSET_LEN_MASK_32(0, 1);
		u1_tmp = u1_tmp |
			 ((rtlpriv->btcoexist.bt_ant_isolation == 1) ?
			 0 : BIT_OFFSET_LEN_MASK_32(1, 1)) |
			 ((rtlpriv->btcoexist.bt_service == BT_SCO) ?
			 0 : BIT_OFFSET_LEN_MASK_32(2, 1));
		rtl_write_byte(rtlpriv, 0x4fd, u1_tmp);

		rtl_write_dword(rtlpriv, REG_BT_COEX_TABLE+4, 0xaaaa9aaa);
		rtl_write_dword(rtlpriv, REG_BT_COEX_TABLE+8, 0xffbd0040);
		rtl_write_dword(rtlpriv, REG_BT_COEX_TABLE+0xc, 0x40000010);

		/* Config to 1T1R. */
		if (rtlphy->rf_type == RF_1T1R) {
			u1_tmp = rtl_read_byte(rtlpriv, ROFDM0_TRXPATHENABLE);
			u1_tmp &= ~(BIT_OFFSET_LEN_MASK_32(1, 1));
			rtl_write_byte(rtlpriv, ROFDM0_TRXPATHENABLE, u1_tmp);

			u1_tmp = rtl_read_byte(rtlpriv, ROFDM1_TRXPATHENABLE);
			u1_tmp &= ~(BIT_OFFSET_LEN_MASK_32(1, 1));
			rtl_write_byte(rtlpriv, ROFDM1_TRXPATHENABLE, u1_tmp);
		}
	}
}

void rtl88ee_suspend(struct ieee80211_hw *hw)
{
}

void rtl88ee_resume(struct ieee80211_hw *hw)
{
}
