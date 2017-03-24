/******************************************************************************
 *
 * Copyright(c) 2009-2014  Realtek Corporation.
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
#include "../rtl8723com/phy_common.h"
#include "dm.h"
#include "../rtl8723com/dm_common.h"
#include "fw.h"
#include "../rtl8723com/fw_common.h"
#include "led.h"
#include "hw.h"
#include "../pwrseqcmd.h"
#include "pwrseq.h"
#include "../btcoexist/rtl_btc.h"

#define LLT_CONFIG	5

static void _rtl8723be_return_beacon_queue_skb(struct ieee80211_hw *hw)
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

static void _rtl8723be_set_bcn_ctrl_reg(struct ieee80211_hw *hw,
					u8 set_bits, u8 clear_bits)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpci->reg_bcn_ctrl_val |= set_bits;
	rtlpci->reg_bcn_ctrl_val &= ~clear_bits;

	rtl_write_byte(rtlpriv, REG_BCN_CTRL, (u8)rtlpci->reg_bcn_ctrl_val);
}

static void _rtl8723be_stop_tx_beacon(struct ieee80211_hw *hw)
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

static void _rtl8723be_resume_tx_beacon(struct ieee80211_hw *hw)
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

static void _rtl8723be_enable_bcn_sub_func(struct ieee80211_hw *hw)
{
	_rtl8723be_set_bcn_ctrl_reg(hw, 0, BIT(1));
}

static void _rtl8723be_disable_bcn_sub_func(struct ieee80211_hw *hw)
{
	_rtl8723be_set_bcn_ctrl_reg(hw, BIT(1), 0);
}

static void _rtl8723be_set_fw_clock_on(struct ieee80211_hw *hw, u8 rpwm_val,
				       bool b_need_turn_off_ckk)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	bool b_support_remote_wake_up;
	u32 count = 0, isr_regaddr, content;
	bool b_schedule_timer = b_need_turn_off_ckk;
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

	if (IS_IN_LOW_POWER_STATE(rtlhal->fw_ps_state)) {
		rtlpriv->cfg->ops->get_hw_reg(hw, HW_VAR_SET_RPWM,
					      (u8 *)(&rpwm_val));
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
				rtlhal->fw_ps_state = FW_PS_STATE_RF_ON;
				RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
					 "Receive CPWM INT!!! Set pHalData->FwPSState = %X\n",
					 rtlhal->fw_ps_state);
			}
		}

		spin_lock_bh(&rtlpriv->locks.fw_ps_lock);
		rtlhal->fw_clk_change_in_progress = false;
		spin_unlock_bh(&rtlpriv->locks.fw_ps_lock);
		if (b_schedule_timer)
			mod_timer(&rtlpriv->works.fw_clockoff_timer,
				  jiffies + MSECS(10));
	} else  {
		spin_lock_bh(&rtlpriv->locks.fw_ps_lock);
		rtlhal->fw_clk_change_in_progress = false;
		spin_unlock_bh(&rtlpriv->locks.fw_ps_lock);
	}
}

static void _rtl8723be_set_fw_clock_off(struct ieee80211_hw *hw, u8 rpwm_val)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl8192_tx_ring *ring;
	enum rf_pwrstate rtstate;
	bool b_schedule_timer = false;
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
			b_schedule_timer = true;
			break;
		}
	}

	if (b_schedule_timer) {
		mod_timer(&rtlpriv->works.fw_clockoff_timer,
			  jiffies + MSECS(10));
		return;
	}

	if (FW_PS_STATE(rtlhal->fw_ps_state) != FW_PS_STATE_RF_OFF_LOW_PWR) {
		spin_lock_bh(&rtlpriv->locks.fw_ps_lock);
		if (!rtlhal->fw_clk_change_in_progress) {
			rtlhal->fw_clk_change_in_progress = true;
			spin_unlock_bh(&rtlpriv->locks.fw_ps_lock);
			rtlhal->fw_ps_state = FW_PS_STATE(rpwm_val);
			rtl_write_word(rtlpriv, REG_HISR, 0x0100);
			rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_SET_RPWM,
						      (u8 *)(&rpwm_val));
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

static void _rtl8723be_set_fw_ps_rf_on(struct ieee80211_hw *hw)
{
	u8 rpwm_val = 0;
	rpwm_val |= (FW_PS_STATE_RF_OFF | FW_PS_ACK);
	_rtl8723be_set_fw_clock_on(hw, rpwm_val, true);
}

static void _rtl8723be_fwlps_leave(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	bool fw_current_inps = false;
	u8 rpwm_val = 0, fw_pwrmode = FW_PS_ACTIVE_MODE;

	if (ppsc->low_power_enable) {
		rpwm_val = (FW_PS_STATE_ALL_ON | FW_PS_ACK);/* RF on */
		_rtl8723be_set_fw_clock_on(hw, rpwm_val, false);
		rtlhal->allow_sw_to_change_hwclc = false;
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_H2C_FW_PWRMODE,
					      (u8 *)(&fw_pwrmode));
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_FW_PSMODE_STATUS,
					      (u8 *)(&fw_current_inps));
	} else {
		rpwm_val = FW_PS_STATE_ALL_ON;	/* RF on */
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_SET_RPWM,
					      (u8 *)(&rpwm_val));
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_H2C_FW_PWRMODE,
					      (u8 *)(&fw_pwrmode));
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_FW_PSMODE_STATUS,
					      (u8 *)(&fw_current_inps));
	}

}

static void _rtl8723be_fwlps_enter(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	bool fw_current_inps = true;
	u8 rpwm_val;

	if (ppsc->low_power_enable) {
		rpwm_val = FW_PS_STATE_RF_OFF_LOW_PWR;	/* RF off */
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_FW_PSMODE_STATUS,
					      (u8 *)(&fw_current_inps));
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_H2C_FW_PWRMODE,
					      (u8 *)(&ppsc->fwctrl_psmode));
		rtlhal->allow_sw_to_change_hwclc = true;
		_rtl8723be_set_fw_clock_off(hw, rpwm_val);
	} else {
		rpwm_val = FW_PS_STATE_RF_OFF;	/* RF off */
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_FW_PSMODE_STATUS,
					      (u8 *)(&fw_current_inps));
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_H2C_FW_PWRMODE,
					      (u8 *)(&ppsc->fwctrl_psmode));
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_SET_RPWM,
					      (u8 *)(&rpwm_val));
	}

}

void rtl8723be_get_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val)
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
		enum rf_pwrstate rfState;
		u32 val_rcr;

		rtlpriv->cfg->ops->get_hw_reg(hw, HW_VAR_RF_STATE,
					      (u8 *)(&rfState));
		if (rfState == ERFOFF) {
			*((bool *)(val)) = true;
		} else {
			val_rcr = rtl_read_dword(rtlpriv, REG_RCR);
			val_rcr &= 0x00070000;
			if (val_rcr)
				*((bool *)(val)) = false;
			else
				*((bool *)(val)) = true;
		}
		}
		break;
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
		}
		break;
	case HAL_DEF_WOWLAN:
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_LOUD,
			 "switch case %#x not processed\n", variable);
		break;
	}
}

static void _rtl8723be_download_rsvd_page(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 tmp_regcr, tmp_reg422, bcnvalid_reg;
	u8 count = 0, dlbcn_count = 0;
	bool b_recover = false;

	tmp_regcr = rtl_read_byte(rtlpriv, REG_CR + 1);
	rtl_write_byte(rtlpriv, REG_CR + 1,
		       (tmp_regcr | BIT(0)));

	_rtl8723be_set_bcn_ctrl_reg(hw, 0, BIT(3));
	_rtl8723be_set_bcn_ctrl_reg(hw, BIT(4), 0);

	tmp_reg422 = rtl_read_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2);
	rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2, tmp_reg422 & (~BIT(6)));
	if (tmp_reg422 & BIT(6))
		b_recover = true;

	do {
		bcnvalid_reg = rtl_read_byte(rtlpriv, REG_TDECTRL + 2);
		rtl_write_byte(rtlpriv, REG_TDECTRL + 2,
			       (bcnvalid_reg | BIT(0)));
		_rtl8723be_return_beacon_queue_skb(hw);

		rtl8723be_set_fw_rsvdpagepkt(hw, 0);
		bcnvalid_reg = rtl_read_byte(rtlpriv, REG_TDECTRL + 2);
		count = 0;
		while (!(bcnvalid_reg & BIT(0)) && count < 20) {
			count++;
			udelay(10);
			bcnvalid_reg = rtl_read_byte(rtlpriv,
						     REG_TDECTRL + 2);
		}
		dlbcn_count++;
	} while (!(bcnvalid_reg & BIT(0)) && dlbcn_count < 5);

	if (bcnvalid_reg & BIT(0))
		rtl_write_byte(rtlpriv, REG_TDECTRL + 2, BIT(0));

	_rtl8723be_set_bcn_ctrl_reg(hw, BIT(3), 0);
	_rtl8723be_set_bcn_ctrl_reg(hw, 0, BIT(4));

	if (b_recover)
		rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2, tmp_reg422);

	tmp_regcr = rtl_read_byte(rtlpriv, REG_CR + 1);
	rtl_write_byte(rtlpriv, REG_CR + 1, (tmp_regcr & ~(BIT(0))));
}

void rtl8723be_set_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	u8 idx;

	switch (variable) {
	case HW_VAR_ETHER_ADDR:
		for (idx = 0; idx < ETH_ALEN; idx++)
			rtl_write_byte(rtlpriv, (REG_MACID + idx), val[idx]);
		break;
	case HW_VAR_BASIC_RATE:{
		u16 b_rate_cfg = ((u16 *)val)[0];
		u8 rate_index = 0;
		b_rate_cfg = b_rate_cfg & 0x15f;
		b_rate_cfg |= 0x01;
		rtl_write_byte(rtlpriv, REG_RRSR, b_rate_cfg & 0xff);
		rtl_write_byte(rtlpriv, REG_RRSR + 1, (b_rate_cfg >> 8) & 0xff);
		while (b_rate_cfg > 0x1) {
			b_rate_cfg = (b_rate_cfg >> 1);
			rate_index++;
		}
		rtl_write_byte(rtlpriv, REG_INIRTS_RATE_SEL, rate_index);
		}
		break;
	case HW_VAR_BSSID:
		for (idx = 0; idx < ETH_ALEN; idx++)
			rtl_write_byte(rtlpriv, (REG_BSSID + idx), val[idx]);

		break;
	case HW_VAR_SIFS:
		rtl_write_byte(rtlpriv, REG_SIFS_CTX + 1, val[0]);
		rtl_write_byte(rtlpriv, REG_SIFS_TRX + 1, val[1]);

		rtl_write_byte(rtlpriv, REG_SPEC_SIFS + 1, val[0]);
		rtl_write_byte(rtlpriv, REG_MAC_SPEC_SIFS + 1, val[0]);

		if (!mac->ht_enable)
			rtl_write_word(rtlpriv, REG_RESP_SIFS_OFDM, 0x0e0e);
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
						      (u8 *)(&e_aci));
		}
		}
		break;
	case HW_VAR_ACK_PREAMBLE:{
		u8 reg_tmp;
		u8 short_preamble = (bool)(*(u8 *)val);
		reg_tmp = rtl_read_byte(rtlpriv, REG_TRXPTCL_CTL + 2);
		if (short_preamble) {
			reg_tmp |= 0x02;
			rtl_write_byte(rtlpriv, REG_TRXPTCL_CTL + 2, reg_tmp);
		} else {
			reg_tmp &= 0xFD;
			rtl_write_byte(rtlpriv, REG_TRXPTCL_CTL + 2, reg_tmp);
		}
		}
		break;
	case HW_VAR_WPA_CONFIG:
		rtl_write_byte(rtlpriv, REG_SECCFG, *((u8 *)val));
		break;
	case HW_VAR_AMPDU_MIN_SPACE:{
		u8 min_spacing_to_set;
		u8 sec_min_space;

		min_spacing_to_set = *((u8 *)val);
		if (min_spacing_to_set <= 7) {
			sec_min_space = 0;

			if (min_spacing_to_set < sec_min_space)
				min_spacing_to_set = sec_min_space;

			mac->min_space_cfg = ((mac->min_space_cfg & 0xf8) |
					      min_spacing_to_set);

			*val = min_spacing_to_set;

			RT_TRACE(rtlpriv, COMP_MLME, DBG_LOUD,
				 "Set HW_VAR_AMPDU_MIN_SPACE: %#x\n",
				  mac->min_space_cfg);

			rtl_write_byte(rtlpriv, REG_AMPDU_MIN_SPACE,
				       mac->min_space_cfg);
		}
		}
		break;
	case HW_VAR_SHORTGI_DENSITY:{
		u8 density_to_set;

		density_to_set = *((u8 *)val);
		mac->min_space_cfg |= (density_to_set << 3);

		RT_TRACE(rtlpriv, COMP_MLME, DBG_LOUD,
			 "Set HW_VAR_SHORTGI_DENSITY: %#x\n",
			  mac->min_space_cfg);

		rtl_write_byte(rtlpriv, REG_AMPDU_MIN_SPACE,
			       mac->min_space_cfg);
		}
		break;
	case HW_VAR_AMPDU_FACTOR:{
		u8 regtoset_normal[4] = {0x41, 0xa8, 0x72, 0xb9};
		u8 factor_toset;
		u8 *p_regtoset = NULL;
		u8 index = 0;

		p_regtoset = regtoset_normal;

		factor_toset = *((u8 *)val);
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

				if ((p_regtoset[index] & 0x0f) > factor_toset)
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
		}
		break;
	case HW_VAR_AC_PARAM:{
		u8 e_aci = *((u8 *)val);
		rtl8723_dm_init_edca_turbo(hw);

		if (rtlpci->acm_method != EACMWAY2_SW)
			rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_ACM_CTRL,
						      (u8 *)(&e_aci));
		}
		break;
	case HW_VAR_ACM_CTRL:{
		u8 e_aci = *((u8 *)val);
		union aci_aifsn *p_aci_aifsn =
				(union aci_aifsn *)(&(mac->ac[0].aifs));
		u8 acm = p_aci_aifsn->f.acm;
		u8 acm_ctrl = rtl_read_byte(rtlpriv, REG_ACMHWCTRL);

		acm_ctrl =
		    acm_ctrl | ((rtlpci->acm_method == 2) ? 0x0 : 0x1);

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
				acm_ctrl &= (~ACMHW_VOQEN);
				break;
			default:
				RT_TRACE(rtlpriv, COMP_ERR, DBG_LOUD,
					 "switch case %#x not processed\n",
					 e_aci);
				break;
			}
		}

		RT_TRACE(rtlpriv, COMP_QOS, DBG_TRACE,
			 "SetHwReg8190pci(): [HW_VAR_ACM_CTRL] Write 0x%X\n",
			 acm_ctrl);
		rtl_write_byte(rtlpriv, REG_ACMHWCTRL, acm_ctrl);
		}
		break;
	case HW_VAR_RCR:
		rtl_write_dword(rtlpriv, REG_RCR, ((u32 *)(val))[0]);
		rtlpci->receive_config = ((u32 *)(val))[0];
		break;
	case HW_VAR_RETRY_LIMIT:{
		u8 retry_limit = ((u8 *)(val))[0];

		rtl_write_word(rtlpriv, REG_RL,
			       retry_limit << RETRY_LIMIT_SHORT_SHIFT |
			       retry_limit << RETRY_LIMIT_LONG_SHIFT);
		}
		break;
	case HW_VAR_DUAL_TSF_RST:
		rtl_write_byte(rtlpriv, REG_DUAL_TSF_RST, (BIT(0) | BIT(1)));
		break;
	case HW_VAR_EFUSE_BYTES:
		rtlefuse->efuse_usedbytes = *((u16 *)val);
		break;
	case HW_VAR_EFUSE_USAGE:
		rtlefuse->efuse_usedpercentage = *((u8 *)val);
		break;
	case HW_VAR_IO_CMD:
		rtl8723be_phy_set_io_cmd(hw, (*(enum io_type *)val));
		break;
	case HW_VAR_SET_RPWM:{
		u8 rpwm_val;

		rpwm_val = rtl_read_byte(rtlpriv, REG_PCIE_HRPWM);
		udelay(1);

		if (rpwm_val & BIT(7)) {
			rtl_write_byte(rtlpriv, REG_PCIE_HRPWM, (*(u8 *)val));
		} else {
			rtl_write_byte(rtlpriv, REG_PCIE_HRPWM,
				       ((*(u8 *)val) | BIT(7)));
		}
		}
		break;
	case HW_VAR_H2C_FW_PWRMODE:
		rtl8723be_set_fw_pwrmode_cmd(hw, (*(u8 *)val));
		break;
	case HW_VAR_FW_PSMODE_STATUS:
		ppsc->fw_current_inpsmode = *((bool *)val);
		break;
	case HW_VAR_RESUME_CLK_ON:
		_rtl8723be_set_fw_ps_rf_on(hw);
		break;
	case HW_VAR_FW_LPS_ACTION:{
		bool b_enter_fwlps = *((bool *)val);

		if (b_enter_fwlps)
			_rtl8723be_fwlps_enter(hw);
		else
			_rtl8723be_fwlps_leave(hw);
		}
		break;
	case HW_VAR_H2C_FW_JOINBSSRPT:{
		u8 mstatus = (*(u8 *)val);

		if (mstatus == RT_MEDIA_CONNECT) {
			rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_AID, NULL);
			_rtl8723be_download_rsvd_page(hw);
		}
		rtl8723be_set_fw_media_status_rpt_cmd(hw, mstatus);
		}
		break;
	case HW_VAR_H2C_FW_P2P_PS_OFFLOAD:
		rtl8723be_set_p2p_ps_offload_cmd(hw, (*(u8 *)val));
		break;
	case HW_VAR_AID:{
		u16 u2btmp;
		u2btmp = rtl_read_word(rtlpriv, REG_BCN_PSR_RPT);
		u2btmp &= 0xC000;
		rtl_write_word(rtlpriv, REG_BCN_PSR_RPT,
			       (u2btmp | mac->assoc_id));
		}
		break;
	case HW_VAR_CORRECT_TSF:{
		u8 btype_ibss = ((u8 *)(val))[0];

		if (btype_ibss)
			_rtl8723be_stop_tx_beacon(hw);

		_rtl8723be_set_bcn_ctrl_reg(hw, 0, BIT(3));

		rtl_write_dword(rtlpriv, REG_TSFTR,
				(u32) (mac->tsf & 0xffffffff));
		rtl_write_dword(rtlpriv, REG_TSFTR + 4,
				(u32) ((mac->tsf >> 32) & 0xffffffff));

		_rtl8723be_set_bcn_ctrl_reg(hw, BIT(3), 0);

		if (btype_ibss)
			_rtl8723be_resume_tx_beacon(hw);
		}
		break;
	case HW_VAR_KEEP_ALIVE:{
		u8 array[2];
		array[0] = 0xff;
		array[1] = *((u8 *)val);
		rtl8723be_fill_h2c_cmd(hw, H2C_8723B_KEEP_ALIVE_CTRL, 2, array);
		}
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_LOUD,
			 "switch case %#x not processed\n", variable);
		break;
	}
}

static bool _rtl8723be_llt_write(struct ieee80211_hw *hw, u32 address, u32 data)
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
			pr_err("Failed to polling write LLT done at address %d!\n",
			       address);
			status = false;
			break;
		}
	} while (++count);

	return status;
}

static bool _rtl8723be_llt_table_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	unsigned short i;
	u8 txpktbuf_bndy;
	u8 maxPage;
	bool status;

	maxPage = 255;
	txpktbuf_bndy = 245;

	rtl_write_dword(rtlpriv, REG_TRXFF_BNDY,
			(0x27FF0000 | txpktbuf_bndy));
	rtl_write_byte(rtlpriv, REG_TDECTRL + 1, txpktbuf_bndy);

	rtl_write_byte(rtlpriv, REG_TXPKTBUF_BCNQ_BDNY, txpktbuf_bndy);
	rtl_write_byte(rtlpriv, REG_TXPKTBUF_MGQ_BDNY, txpktbuf_bndy);

	rtl_write_byte(rtlpriv, 0x45D, txpktbuf_bndy);
	rtl_write_byte(rtlpriv, REG_PBP, 0x31);
	rtl_write_byte(rtlpriv, REG_RX_DRVINFO_SZ, 0x4);

	for (i = 0; i < (txpktbuf_bndy - 1); i++) {
		status = _rtl8723be_llt_write(hw, i, i + 1);
		if (!status)
			return status;
	}

	status = _rtl8723be_llt_write(hw, (txpktbuf_bndy - 1), 0xFF);

	if (!status)
		return status;

	for (i = txpktbuf_bndy; i < maxPage; i++) {
		status = _rtl8723be_llt_write(hw, i, (i + 1));
		if (!status)
			return status;
	}

	status = _rtl8723be_llt_write(hw, maxPage, txpktbuf_bndy);
	if (!status)
		return status;

	rtl_write_dword(rtlpriv, REG_RQPN, 0x80e40808);
	rtl_write_byte(rtlpriv, REG_RQPN_NPQ, 0x00);

	return true;
}

static void _rtl8723be_gen_refresh_led_state(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_led *pled0 = &rtlpriv->ledctl.sw_led0;

	if (rtlpriv->rtlhal.up_first_time)
		return;

	if (ppsc->rfoff_reason == RF_CHANGE_BY_IPS)
		rtl8723be_sw_led_on(hw, pled0);
	else if (ppsc->rfoff_reason == RF_CHANGE_BY_INIT)
		rtl8723be_sw_led_on(hw, pled0);
	else
		rtl8723be_sw_led_off(hw, pled0);
}

static bool _rtl8723be_init_mac(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	unsigned char bytetmp;
	unsigned short wordtmp;

	rtl_write_byte(rtlpriv, REG_RSV_CTRL, 0x00);

	/*Auto Power Down to CHIP-off State*/
	bytetmp = rtl_read_byte(rtlpriv, REG_APS_FSMCO + 1) & (~BIT(7));
	rtl_write_byte(rtlpriv, REG_APS_FSMCO + 1, bytetmp);

	/* HW Power on sequence */
	if (!rtl_hal_pwrseqcmdparsing(rtlpriv, PWR_CUT_ALL_MSK,
				      PWR_FAB_ALL_MSK, PWR_INTF_PCI_MSK,
				      RTL8723_NIC_ENABLE_FLOW)) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "init MAC Fail as power on failure\n");
		return false;
	}

	bytetmp = rtl_read_byte(rtlpriv, REG_MULTI_FUNC_CTRL);
	rtl_write_byte(rtlpriv, REG_MULTI_FUNC_CTRL, bytetmp | BIT(3));

	bytetmp = rtl_read_byte(rtlpriv, REG_APS_FSMCO) | BIT(4);
	rtl_write_byte(rtlpriv, REG_APS_FSMCO, bytetmp);

	bytetmp = rtl_read_byte(rtlpriv, REG_CR);
	bytetmp = 0xff;
	rtl_write_byte(rtlpriv, REG_CR, bytetmp);
	mdelay(2);

	bytetmp = rtl_read_byte(rtlpriv, REG_HWSEQ_CTRL);
	bytetmp |= 0x7f;
	rtl_write_byte(rtlpriv, REG_HWSEQ_CTRL, bytetmp);
	mdelay(2);

	bytetmp = rtl_read_byte(rtlpriv, REG_SYS_CFG + 3);
	if (bytetmp & BIT(0)) {
		bytetmp = rtl_read_byte(rtlpriv, 0x7c);
		rtl_write_byte(rtlpriv, 0x7c, bytetmp | BIT(6));
	}

	bytetmp = rtl_read_byte(rtlpriv, REG_SYS_CLKR);
	rtl_write_byte(rtlpriv, REG_SYS_CLKR, bytetmp | BIT(3));
	bytetmp = rtl_read_byte(rtlpriv, REG_GPIO_MUXCFG + 1);
	rtl_write_byte(rtlpriv, REG_GPIO_MUXCFG + 1, bytetmp & (~BIT(4)));

	rtl_write_word(rtlpriv, REG_CR, 0x2ff);

	if (!rtlhal->mac_func_enable) {
		if (_rtl8723be_llt_table_init(hw) == false)
			return false;
	}

	rtl_write_dword(rtlpriv, REG_HISR, 0xffffffff);
	rtl_write_dword(rtlpriv, REG_HISRE, 0xffffffff);

	/* Enable FW Beamformer Interrupt */
	bytetmp = rtl_read_byte(rtlpriv, REG_FWIMR + 3);
	rtl_write_byte(rtlpriv, REG_FWIMR + 3, bytetmp | BIT(6));

	wordtmp = rtl_read_word(rtlpriv, REG_TRXDMA_CTRL);
	wordtmp &= 0xf;
	wordtmp |= 0xF5B1;
	rtl_write_word(rtlpriv, REG_TRXDMA_CTRL, wordtmp);

	rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 1, 0x1F);
	rtl_write_dword(rtlpriv, REG_RCR, rtlpci->receive_config);
	rtl_write_word(rtlpriv, REG_RXFLTMAP2, 0xFFFF);
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

	bytetmp = rtl_read_byte(rtlpriv, REG_PCIE_CTRL_REG + 3);
	rtl_write_byte(rtlpriv, REG_PCIE_CTRL_REG + 3, bytetmp | 0x77);

	rtl_write_dword(rtlpriv, REG_INT_MIG, 0);

	rtl_write_dword(rtlpriv, REG_MCUTST_1, 0x0);

	rtl_write_byte(rtlpriv, REG_SECONDARY_CCA_CTRL, 0x3);

	/* <20130114, Kordan> The following setting is
	 * only for DPDT and Fixed board type.
	 * TODO:  A better solution is configure it
	 * according EFUSE during the run-time.
	 */
	rtl_set_bbreg(hw, 0x64, BIT(20), 0x0);/* 0x66[4]=0 */
	rtl_set_bbreg(hw, 0x64, BIT(24), 0x0);/* 0x66[8]=0 */
	rtl_set_bbreg(hw, 0x40, BIT(4), 0x0)/* 0x40[4]=0 */;
	rtl_set_bbreg(hw, 0x40, BIT(3), 0x1)/* 0x40[3]=1 */;
	rtl_set_bbreg(hw, 0x4C, BIT(24) | BIT(23), 0x2)/* 0x4C[24:23]=10 */;
	rtl_set_bbreg(hw, 0x944, BIT(1) | BIT(0), 0x3)/* 0x944[1:0]=11 */;
	rtl_set_bbreg(hw, 0x930, MASKBYTE0, 0x77)/* 0x930[7:0]=77 */;
	rtl_set_bbreg(hw, 0x38, BIT(11), 0x1)/* 0x38[11]=1 */;

	bytetmp = rtl_read_byte(rtlpriv, REG_RXDMA_CONTROL);
	rtl_write_byte(rtlpriv, REG_RXDMA_CONTROL, bytetmp & (~BIT(2)));

	_rtl8723be_gen_refresh_led_state(hw);
	return true;
}

static void _rtl8723be_hw_configure(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u32 reg_rrsr;

	reg_rrsr = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
	/* Init value for RRSR. */
	rtl_write_dword(rtlpriv, REG_RRSR, reg_rrsr);

	/* ARFB table 9 for 11ac 5G 2SS */
	rtl_write_dword(rtlpriv, REG_ARFR0 + 4, 0xfffff000);

	/* ARFB table 10 for 11ac 5G 1SS */
	rtl_write_dword(rtlpriv, REG_ARFR1 + 4, 0x003ff000);

	/* CF-End setting. */
	rtl_write_word(rtlpriv, REG_FWHW_TXQ_CTRL, 0x1F00);

	/* 0x456 = 0x70, sugguested by Zhilin */
	rtl_write_byte(rtlpriv, REG_AMPDU_MAX_TIME, 0x70);

	/* Set retry limit */
	rtl_write_word(rtlpriv, REG_RL, 0x0707);

	/* Set Data / Response auto rate fallack retry count */
	rtl_write_dword(rtlpriv, REG_DARFRC, 0x01000000);
	rtl_write_dword(rtlpriv, REG_DARFRC + 4, 0x07060504);
	rtl_write_dword(rtlpriv, REG_RARFRC, 0x01000000);
	rtl_write_dword(rtlpriv, REG_RARFRC + 4, 0x07060504);

	rtlpci->reg_bcn_ctrl_val = 0x1d;
	rtl_write_byte(rtlpriv, REG_BCN_CTRL, rtlpci->reg_bcn_ctrl_val);

	/* TBTT prohibit hold time. Suggested by designer TimChen. */
	rtl_write_byte(rtlpriv, REG_TBTT_PROHIBIT + 1, 0xff); /* 8 ms */

	rtl_write_word(rtlpriv, REG_NAV_PROT_LEN, 0x0040);

	/*For Rx TP. Suggested by SD1 Richard. Added by tynli. 2010.04.12.*/
	rtl_write_dword(rtlpriv, REG_FAST_EDCA_CTRL, 0x03086666);

	rtl_write_byte(rtlpriv, REG_HT_SINGLE_AMPDU, 0x80);

	rtl_write_byte(rtlpriv, REG_RX_PKT_LIMIT, 0x20);

	rtl_write_byte(rtlpriv, REG_MAX_AGGR_NUM, 0x1F);
}

static u8 _rtl8723be_dbi_read(struct rtl_priv *rtlpriv, u16 addr)
{
	u16 read_addr = addr & 0xfffc;
	u8 ret = 0, tmp = 0, count = 0;

	rtl_write_word(rtlpriv, REG_DBI_ADDR, read_addr);
	rtl_write_byte(rtlpriv, REG_DBI_FLAG, 0x2);
	tmp = rtl_read_byte(rtlpriv, REG_DBI_FLAG);
	count = 0;
	while (tmp && count < 20) {
		udelay(10);
		tmp = rtl_read_byte(rtlpriv, REG_DBI_FLAG);
		count++;
	}
	if (0 == tmp) {
		read_addr = REG_DBI_RDATA + addr % 4;
		ret = rtl_read_byte(rtlpriv, read_addr);
	}

	return ret;
}

static void _rtl8723be_dbi_write(struct rtl_priv *rtlpriv, u16 addr, u8 data)
{
	u8 tmp = 0, count = 0;
	u16 write_addr = 0, remainder = addr % 4;

	/* Write DBI 1Byte Data */
	write_addr = REG_DBI_WDATA + remainder;
	rtl_write_byte(rtlpriv, write_addr, data);

	/* Write DBI 2Byte Address & Write Enable */
	write_addr = (addr & 0xfffc) | (BIT(0) << (remainder + 12));
	rtl_write_word(rtlpriv, REG_DBI_ADDR, write_addr);

	/* Write DBI Write Flag */
	rtl_write_byte(rtlpriv, REG_DBI_FLAG, 0x1);

	tmp = rtl_read_byte(rtlpriv, REG_DBI_FLAG);
	count = 0;
	while (tmp && count < 20) {
		udelay(10);
		tmp = rtl_read_byte(rtlpriv, REG_DBI_FLAG);
		count++;
	}
}

static u16 _rtl8723be_mdio_read(struct rtl_priv *rtlpriv, u8 addr)
{
	u16 ret = 0;
	u8 tmp = 0, count = 0;

	rtl_write_byte(rtlpriv, REG_MDIO_CTL, addr | BIT(6));
	tmp = rtl_read_byte(rtlpriv, REG_MDIO_CTL) & BIT(6);
	count = 0;
	while (tmp && count < 20) {
		udelay(10);
		tmp = rtl_read_byte(rtlpriv, REG_MDIO_CTL) & BIT(6);
		count++;
	}

	if (0 == tmp)
		ret = rtl_read_word(rtlpriv, REG_MDIO_RDATA);

	return ret;
}

static void _rtl8723be_mdio_write(struct rtl_priv *rtlpriv, u8 addr, u16 data)
{
	u8 tmp = 0, count = 0;

	rtl_write_word(rtlpriv, REG_MDIO_WDATA, data);
	rtl_write_byte(rtlpriv, REG_MDIO_CTL, addr | BIT(5));
	tmp = rtl_read_byte(rtlpriv, REG_MDIO_CTL) & BIT(5);
	count = 0;
	while (tmp && count < 20) {
		udelay(10);
		tmp = rtl_read_byte(rtlpriv, REG_MDIO_CTL) & BIT(5);
		count++;
	}
}

static void _rtl8723be_enable_aspm_back_door(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 tmp8 = 0;
	u16 tmp16 = 0;

	/* <Roger_Notes> Overwrite following ePHY parameter for
	 * some platform compatibility issue,
	 * especially when CLKReq is enabled, 2012.11.09.
	 */
	tmp16 = _rtl8723be_mdio_read(rtlpriv, 0x01);
	if (tmp16 != 0x0663)
		_rtl8723be_mdio_write(rtlpriv, 0x01, 0x0663);

	tmp16 = _rtl8723be_mdio_read(rtlpriv, 0x04);
	if (tmp16 != 0x7544)
		_rtl8723be_mdio_write(rtlpriv, 0x04, 0x7544);

	tmp16 = _rtl8723be_mdio_read(rtlpriv, 0x06);
	if (tmp16 != 0xB880)
		_rtl8723be_mdio_write(rtlpriv, 0x06, 0xB880);

	tmp16 = _rtl8723be_mdio_read(rtlpriv, 0x07);
	if (tmp16 != 0x4000)
		_rtl8723be_mdio_write(rtlpriv, 0x07, 0x4000);

	tmp16 = _rtl8723be_mdio_read(rtlpriv, 0x08);
	if (tmp16 != 0x9003)
		_rtl8723be_mdio_write(rtlpriv, 0x08, 0x9003);

	tmp16 = _rtl8723be_mdio_read(rtlpriv, 0x09);
	if (tmp16 != 0x0D03)
		_rtl8723be_mdio_write(rtlpriv, 0x09, 0x0D03);

	tmp16 = _rtl8723be_mdio_read(rtlpriv, 0x0A);
	if (tmp16 != 0x4037)
		_rtl8723be_mdio_write(rtlpriv, 0x0A, 0x4037);

	tmp16 = _rtl8723be_mdio_read(rtlpriv, 0x0B);
	if (tmp16 != 0x0070)
		_rtl8723be_mdio_write(rtlpriv, 0x0B, 0x0070);

	/* Configuration Space offset 0x70f BIT7 is used to control L0S */
	tmp8 = _rtl8723be_dbi_read(rtlpriv, 0x70f);
	_rtl8723be_dbi_write(rtlpriv, 0x70f, tmp8 | BIT(7));

	/* Configuration Space offset 0x719 Bit3 is for L1
	 * BIT4 is for clock request
	 */
	tmp8 = _rtl8723be_dbi_read(rtlpriv, 0x719);
	_rtl8723be_dbi_write(rtlpriv, 0x719, tmp8 | BIT(3) | BIT(4));
}

void rtl8723be_enable_hw_security_config(struct ieee80211_hw *hw)
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

static void _rtl8723be_poweroff_adapter(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 u1b_tmp;

	rtlhal->mac_func_enable = false;
	/* Combo (PCIe + USB) Card and PCIe-MF Card */
	/* 1. Run LPS WL RFOFF flow */
	rtl_hal_pwrseqcmdparsing(rtlpriv, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK,
				 PWR_INTF_PCI_MSK, RTL8723_NIC_LPS_ENTER_FLOW);

	/* 2. 0x1F[7:0] = 0 */
	/* turn off RF */
	/* rtl_write_byte(rtlpriv, REG_RF_CTRL, 0x00); */
	if ((rtl_read_byte(rtlpriv, REG_MCUFWDL) & BIT(7)) &&
	    rtlhal->fw_ready) {
		rtl8723be_firmware_selfreset(hw);
	}

	/* Reset MCU. Suggested by Filen. */
	u1b_tmp = rtl_read_byte(rtlpriv, REG_SYS_FUNC_EN + 1);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN + 1, (u1b_tmp & (~BIT(2))));

	/* g.	MCUFWDL 0x80[1:0]=0	 */
	/* reset MCU ready status */
	rtl_write_byte(rtlpriv, REG_MCUFWDL, 0x00);

	/* HW card disable configuration. */
	rtl_hal_pwrseqcmdparsing(rtlpriv, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK,
				 PWR_INTF_PCI_MSK, RTL8723_NIC_DISABLE_FLOW);

	/* Reset MCU IO Wrapper */
	u1b_tmp = rtl_read_byte(rtlpriv, REG_RSV_CTRL + 1);
	rtl_write_byte(rtlpriv, REG_RSV_CTRL + 1, (u1b_tmp & (~BIT(0))));
	u1b_tmp = rtl_read_byte(rtlpriv, REG_RSV_CTRL + 1);
	rtl_write_byte(rtlpriv, REG_RSV_CTRL + 1, u1b_tmp | BIT(0));

	/* 7. RSV_CTRL 0x1C[7:0] = 0x0E */
	/* lock ISO/CLK/Power control register */
	rtl_write_byte(rtlpriv, REG_RSV_CTRL, 0x0e);
}

static bool _rtl8723be_check_pcie_dma_hang(struct rtl_priv *rtlpriv)
{
	u8 tmp;

	/* write reg 0x350 Bit[26]=1. Enable debug port. */
	tmp = rtl_read_byte(rtlpriv, REG_DBI_CTRL + 3);
	if (!(tmp & BIT(2))) {
		rtl_write_byte(rtlpriv, REG_DBI_CTRL + 3, (tmp | BIT(2)));
		mdelay(100); /* Suggested by DD Justin_tsai. */
	}

	/* read reg 0x350 Bit[25] if 1 : RX hang
	 * read reg 0x350 Bit[24] if 1 : TX hang
	 */
	tmp = rtl_read_byte(rtlpriv, REG_DBI_CTRL + 3);
	if ((tmp & BIT(0)) || (tmp & BIT(1))) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "CheckPcieDMAHang8723BE(): true!!\n");
		return true;
	}
	return false;
}

static void _rtl8723be_reset_pcie_interface_dma(struct rtl_priv *rtlpriv,
						bool mac_power_on)
{
	u8 tmp;
	bool release_mac_rx_pause;
	u8 backup_pcie_dma_pause;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "ResetPcieInterfaceDMA8723BE()\n");

	/* Revise Note: Follow the document "PCIe RX DMA Hang Reset Flow_v03"
	 * released by SD1 Alan.
	 * 2013.05.07, by tynli.
	 */

	/* 1. disable register write lock
	 *	write 0x1C bit[1:0] = 2'h0
	 *	write 0xCC bit[2] = 1'b1
	 */
	tmp = rtl_read_byte(rtlpriv, REG_RSV_CTRL);
	tmp &= ~(BIT(1) | BIT(0));
	rtl_write_byte(rtlpriv, REG_RSV_CTRL, tmp);
	tmp = rtl_read_byte(rtlpriv, REG_PMC_DBG_CTRL2);
	tmp |= BIT(2);
	rtl_write_byte(rtlpriv, REG_PMC_DBG_CTRL2, tmp);

	/* 2. Check and pause TRX DMA
	 *	write 0x284 bit[18] = 1'b1
	 *	write 0x301 = 0xFF
	 */
	tmp = rtl_read_byte(rtlpriv, REG_RXDMA_CONTROL);
	if (tmp & BIT(2)) {
		/* Already pause before the function for another purpose. */
		release_mac_rx_pause = false;
	} else {
		rtl_write_byte(rtlpriv, REG_RXDMA_CONTROL, (tmp | BIT(2)));
		release_mac_rx_pause = true;
	}

	backup_pcie_dma_pause = rtl_read_byte(rtlpriv, REG_PCIE_CTRL_REG + 1);
	if (backup_pcie_dma_pause != 0xFF)
		rtl_write_byte(rtlpriv, REG_PCIE_CTRL_REG + 1, 0xFF);

	if (mac_power_on) {
		/* 3. reset TRX function
		 *	write 0x100 = 0x00
		 */
		rtl_write_byte(rtlpriv, REG_CR, 0);
	}

	/* 4. Reset PCIe DMA
	 *	write 0x003 bit[0] = 0
	 */
	tmp = rtl_read_byte(rtlpriv, REG_SYS_FUNC_EN + 1);
	tmp &= ~(BIT(0));
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN + 1, tmp);

	/* 5. Enable PCIe DMA
	 *	write 0x003 bit[0] = 1
	 */
	tmp = rtl_read_byte(rtlpriv, REG_SYS_FUNC_EN + 1);
	tmp |= BIT(0);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN + 1, tmp);

	if (mac_power_on) {
		/* 6. enable TRX function
		 *	write 0x100 = 0xFF
		 */
		rtl_write_byte(rtlpriv, REG_CR, 0xFF);

		/* We should init LLT & RQPN and
		 * prepare Tx/Rx descrptor address later
		 * because MAC function is reset.
		 */
	}

	/* 7. Restore PCIe autoload down bit
	 *	write 0xF8 bit[17] = 1'b1
	 */
	tmp = rtl_read_byte(rtlpriv, REG_MAC_PHY_CTRL_NORMAL + 2);
	tmp |= BIT(1);
	rtl_write_byte(rtlpriv, REG_MAC_PHY_CTRL_NORMAL + 2, tmp);

	/* In MAC power on state, BB and RF maybe in ON state,
	 * if we release TRx DMA here
	 * it will cause packets to be started to Tx/Rx,
	 * so we release Tx/Rx DMA later.
	 */
	if (!mac_power_on) {
		/* 8. release TRX DMA
		 *	write 0x284 bit[18] = 1'b0
		 *	write 0x301 = 0x00
		 */
		if (release_mac_rx_pause) {
			tmp = rtl_read_byte(rtlpriv, REG_RXDMA_CONTROL);
			rtl_write_byte(rtlpriv, REG_RXDMA_CONTROL,
				       (tmp & (~BIT(2))));
		}
		rtl_write_byte(rtlpriv, REG_PCIE_CTRL_REG + 1,
			       backup_pcie_dma_pause);
	}

	/* 9. lock system register
	 *	write 0xCC bit[2] = 1'b0
	 */
	tmp = rtl_read_byte(rtlpriv, REG_PMC_DBG_CTRL2);
	tmp &= ~(BIT(2));
	rtl_write_byte(rtlpriv, REG_PMC_DBG_CTRL2, tmp);
}

int rtl8723be_hw_init(struct ieee80211_hw *hw)
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

	/* reenable interrupts to not interfere with other devices */
	local_save_flags(flags);
	local_irq_enable();

	rtlhal->fw_ready = false;
	rtlpriv->rtlhal.being_init_adapter = true;
	rtlpriv->intf_ops->disable_aspm(hw);

	tmp_u1b = rtl_read_byte(rtlpriv, REG_CR);
	if (tmp_u1b != 0 && tmp_u1b != 0xea) {
		rtlhal->mac_func_enable = true;
	} else {
		rtlhal->mac_func_enable = false;
		rtlhal->fw_ps_state = FW_PS_STATE_ALL_ON;
	}

	if (_rtl8723be_check_pcie_dma_hang(rtlpriv)) {
		_rtl8723be_reset_pcie_interface_dma(rtlpriv,
						    rtlhal->mac_func_enable);
		rtlhal->mac_func_enable = false;
	}
	if (rtlhal->mac_func_enable) {
		_rtl8723be_poweroff_adapter(hw);
		rtlhal->mac_func_enable = false;
	}
	rtstatus = _rtl8723be_init_mac(hw);
	if (!rtstatus) {
		pr_err("Init MAC failed\n");
		err = 1;
		goto exit;
	}

	tmp_u1b = rtl_read_byte(rtlpriv, REG_SYS_CFG);
	rtl_write_byte(rtlpriv, REG_SYS_CFG, tmp_u1b & 0x7F);

	err = rtl8723_download_fw(hw, true, FW_8723B_POLLING_TIMEOUT_COUNT);
	if (err) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 "Failed to download FW. Init HW without FW now..\n");
		err = 1;
		goto exit;
	}
	rtlhal->fw_ready = true;

	rtlhal->last_hmeboxnum = 0;
	rtl8723be_phy_mac_config(hw);
	/* because last function modify RCR, so we update
	 * rcr var here, or TP will unstable for receive_config
	 * is wrong, RX RCR_ACRC32 will cause TP unstable & Rx
	 * RCR_APP_ICV will cause mac80211 unassoc for cisco 1252
	 */
	rtlpci->receive_config = rtl_read_dword(rtlpriv, REG_RCR);
	rtlpci->receive_config &= ~(RCR_ACRC32 | RCR_AICV);
	rtl_write_dword(rtlpriv, REG_RCR, rtlpci->receive_config);

	rtl8723be_phy_bb_config(hw);
	rtl8723be_phy_rf_config(hw);

	rtlphy->rfreg_chnlval[0] = rtl_get_rfreg(hw, (enum radio_path)0,
						 RF_CHNLBW, RFREG_OFFSET_MASK);
	rtlphy->rfreg_chnlval[1] = rtl_get_rfreg(hw, (enum radio_path)1,
						 RF_CHNLBW, RFREG_OFFSET_MASK);
	rtlphy->rfreg_chnlval[0] &= 0xFFF03FF;
	rtlphy->rfreg_chnlval[0] |= (BIT(10) | BIT(11));

	_rtl8723be_hw_configure(hw);
	rtlhal->mac_func_enable = true;
	rtl_cam_reset_all_entry(hw);
	rtl8723be_enable_hw_security_config(hw);

	ppsc->rfpwr_state = ERFON;

	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_ETHER_ADDR, mac->mac_addr);
	_rtl8723be_enable_aspm_back_door(hw);
	rtlpriv->intf_ops->enable_aspm(hw);

	rtl8723be_bt_hw_init(hw);

	if (ppsc->rfpwr_state == ERFON) {
		rtl8723be_phy_set_rfpath_switch(hw, 1);
		/* when use 1ant NIC, iqk will disturb BT music
		 * root cause is not clear now, is something
		 * related with 'mdelay' and Reg[0x948]
		 */
		if (rtlpriv->btcoexist.btc_info.ant_num == ANT_X2 ||
		    !rtlpriv->cfg->ops->get_btc_status()) {
			rtl8723be_phy_iq_calibrate(hw, false);
			rtlphy->iqk_initialized = true;
		}
		rtl8723be_dm_check_txpower_tracking(hw);
		rtl8723be_phy_lc_calibrate(hw);
	}
	rtl_write_byte(rtlpriv, REG_NAV_UPPER, ((30000 + 127) / 128));

	/* Release Rx DMA. */
	tmp_u1b = rtl_read_byte(rtlpriv, REG_RXDMA_CONTROL);
	if (tmp_u1b & BIT(2)) {
		/* Release Rx DMA if needed */
		tmp_u1b &= (~BIT(2));
		rtl_write_byte(rtlpriv, REG_RXDMA_CONTROL, tmp_u1b);
	}
	/* Release Tx/Rx PCIE DMA. */
	rtl_write_byte(rtlpriv, REG_PCIE_CTRL_REG + 1, 0);

	rtl8723be_dm_init(hw);
exit:
	local_irq_restore(flags);
	rtlpriv->rtlhal.being_init_adapter = false;
	return err;
}

static enum version_8723e _rtl8723be_read_chip_version(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	enum version_8723e version = VERSION_UNKNOWN;
	u32 value32;

	value32 = rtl_read_dword(rtlpriv, REG_SYS_CFG1);
	if ((value32 & (CHIP_8723B)) != CHIP_8723B)
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "unknown chip version\n");
	else
		version = (enum version_8723e)CHIP_8723B;

	rtlphy->rf_type = RF_1T1R;

	/* treat rtl8723be chip as  MP version in default */
	version = (enum version_8723e)(version | NORMAL_CHIP);

	value32 = rtl_read_dword(rtlpriv, REG_SYS_CFG);
	/* cut version */
	version |= (enum version_8723e)(value32 & CHIP_VER_RTL_MASK);
	/* Manufacture */
	if (((value32 & EXT_VENDOR_ID) >> 18) == 0x01)
		version = (enum version_8723e)(version | CHIP_VENDOR_SMIC);

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "Chip RF Type: %s\n", (rtlphy->rf_type == RF_2T2R) ?
		  "RF_2T2R" : "RF_1T1R");

	return version;
}

static int _rtl8723be_set_media_status(struct ieee80211_hw *hw,
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
		pr_err("Network type %d not support!\n", type);
		return 1;
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
		_rtl8723be_stop_tx_beacon(hw);
		_rtl8723be_enable_bcn_sub_func(hw);
	} else if (mode == MSR_ADHOC || mode == MSR_AP) {
		_rtl8723be_resume_tx_beacon(hw);
		_rtl8723be_disable_bcn_sub_func(hw);
	} else {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 "Set HW_VAR_MEDIA_STATUS: No such media status(%x).\n",
			 mode);
	}

	rtl_write_byte(rtlpriv, MSR, bt_msr | mode);
	rtlpriv->cfg->ops->led_control(hw, ledaction);
	if (mode == MSR_AP)
		rtl_write_byte(rtlpriv, REG_BCNTCFG + 1, 0x00);
	else
		rtl_write_byte(rtlpriv, REG_BCNTCFG + 1, 0x66);
	return 0;
}

void rtl8723be_set_check_bssid(struct ieee80211_hw *hw, bool check_bssid)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u32 reg_rcr = rtlpci->receive_config;

	if (rtlpriv->psc.rfpwr_state != ERFON)
		return;

	if (check_bssid) {
		reg_rcr |= (RCR_CBSSID_DATA | RCR_CBSSID_BCN);
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_RCR,
					      (u8 *)(&reg_rcr));
		_rtl8723be_set_bcn_ctrl_reg(hw, 0, BIT(4));
	} else if (!check_bssid) {
		reg_rcr &= (~(RCR_CBSSID_DATA | RCR_CBSSID_BCN));
		_rtl8723be_set_bcn_ctrl_reg(hw, BIT(4), 0);
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_RCR,
					      (u8 *)(&reg_rcr));
	}

}

int rtl8723be_set_network_type(struct ieee80211_hw *hw,
			       enum nl80211_iftype type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (_rtl8723be_set_media_status(hw, type))
		return -EOPNOTSUPP;

	if (rtlpriv->mac80211.link_state == MAC80211_LINKED) {
		if (type != NL80211_IFTYPE_AP)
			rtl8723be_set_check_bssid(hw, true);
	} else {
		rtl8723be_set_check_bssid(hw, false);
	}

	return 0;
}

/* don't set REG_EDCA_BE_PARAM here
 * because mac80211 will send pkt when scan
 */
void rtl8723be_set_qos(struct ieee80211_hw *hw, int aci)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl8723_dm_init_edca_turbo(hw);
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
		WARN_ONCE(true, "rtl8723be: invalid aci: %d !\n", aci);
		break;
	}
}

void rtl8723be_enable_interrupt(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	rtl_write_dword(rtlpriv, REG_HIMR, rtlpci->irq_mask[0] & 0xFFFFFFFF);
	rtl_write_dword(rtlpriv, REG_HIMRE, rtlpci->irq_mask[1] & 0xFFFFFFFF);
	rtlpci->irq_enabled = true;

	/*enable system interrupt*/
	rtl_write_dword(rtlpriv, REG_HSIMR, rtlpci->sys_irq_mask & 0xFFFFFFFF);
}

void rtl8723be_disable_interrupt(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	rtl_write_dword(rtlpriv, REG_HIMR, IMR_DISABLED);
	rtl_write_dword(rtlpriv, REG_HIMRE, IMR_DISABLED);
	rtlpci->irq_enabled = false;
	/*synchronize_irq(rtlpci->pdev->irq);*/
}

void rtl8723be_card_disable(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	enum nl80211_iftype opmode;

	mac->link_state = MAC80211_NOLINK;
	opmode = NL80211_IFTYPE_UNSPECIFIED;
	_rtl8723be_set_media_status(hw, opmode);
	if (rtlpriv->rtlhal.driver_is_goingto_unload ||
	    ppsc->rfoff_reason > RF_CHANGE_BY_PS)
		rtlpriv->cfg->ops->led_control(hw, LED_CTL_POWER_OFF);
	RT_SET_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC);
	_rtl8723be_poweroff_adapter(hw);

	/* after power off we should do iqk again */
	rtlpriv->phy.iqk_initialized = false;
}

void rtl8723be_interrupt_recognized(struct ieee80211_hw *hw,
				    u32 *p_inta, u32 *p_intb)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	*p_inta = rtl_read_dword(rtlpriv, ISR) & rtlpci->irq_mask[0];
	rtl_write_dword(rtlpriv, ISR, *p_inta);

	*p_intb = rtl_read_dword(rtlpriv, REG_HISRE) &
					rtlpci->irq_mask[1];
	rtl_write_dword(rtlpriv, REG_HISRE, *p_intb);
}

void rtl8723be_set_beacon_related_registers(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u16 bcn_interval, atim_window;

	bcn_interval = mac->beacon_interval;
	atim_window = 2;	/*FIX MERGE */
	rtl8723be_disable_interrupt(hw);
	rtl_write_word(rtlpriv, REG_ATIMWND, atim_window);
	rtl_write_word(rtlpriv, REG_BCN_INTERVAL, bcn_interval);
	rtl_write_word(rtlpriv, REG_BCNTCFG, 0x660f);
	rtl_write_byte(rtlpriv, REG_RXTSF_OFFSET_CCK, 0x18);
	rtl_write_byte(rtlpriv, REG_RXTSF_OFFSET_OFDM, 0x18);
	rtl_write_byte(rtlpriv, 0x606, 0x30);
	rtl8723be_enable_interrupt(hw);
}

void rtl8723be_set_beacon_interval(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u16 bcn_interval = mac->beacon_interval;

	RT_TRACE(rtlpriv, COMP_BEACON, DBG_DMESG,
		 "beacon_interval:%d\n", bcn_interval);
	rtl8723be_disable_interrupt(hw);
	rtl_write_word(rtlpriv, REG_BCN_INTERVAL, bcn_interval);
	rtl8723be_enable_interrupt(hw);
}

void rtl8723be_update_interrupt_mask(struct ieee80211_hw *hw,
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
	rtl8723be_disable_interrupt(hw);
	rtl8723be_enable_interrupt(hw);
}

static u8 _rtl8723be_get_chnl_group(u8 chnl)
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

static void _rtl8723be_read_power_value_fromprom(struct ieee80211_hw *hw,
					struct txpower_info_2g *pw2g,
					struct txpower_info_5g *pw5g,
					bool autoload_fail, u8 *hwinfo)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 path, addr = EEPROM_TX_PWR_INX, group, cnt = 0;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "hal_ReadPowerValueFromPROM8723BE(): PROMContent[0x%x]=0x%x\n",
		 (addr + 1), hwinfo[addr + 1]);
	if (0xFF == hwinfo[addr + 1])  /*YJ,add,120316*/
		autoload_fail = true;

	if (autoload_fail) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "auto load fail : Use Default value!\n");
		for (path = 0; path < MAX_RF_PATH; path++) {
			/* 2.4G default value */
			for (group = 0 ; group < MAX_CHNL_GROUP_24G; group++) {
				pw2g->index_cck_base[path][group] = 0x2D;
				pw2g->index_bw40_base[path][group] = 0x2D;
			}
			for (cnt = 0; cnt < MAX_TX_COUNT; cnt++) {
				if (cnt == 0) {
					pw2g->bw20_diff[path][0] = 0x02;
					pw2g->ofdm_diff[path][0] = 0x04;
				} else {
					pw2g->bw20_diff[path][cnt] = 0xFE;
					pw2g->bw40_diff[path][cnt] = 0xFE;
					pw2g->cck_diff[path][cnt] = 0xFE;
					pw2g->ofdm_diff[path][cnt] = 0xFE;
				}
			}
		}
		return;
	}

	for (path = 0; path < MAX_RF_PATH; path++) {
		/*2.4G default value*/
		for (group = 0; group < MAX_CHNL_GROUP_24G; group++) {
			pw2g->index_cck_base[path][group] = hwinfo[addr++];
			if (pw2g->index_cck_base[path][group] == 0xFF)
				pw2g->index_cck_base[path][group] = 0x2D;

		}
		for (group = 0; group < MAX_CHNL_GROUP_24G - 1; group++) {
			pw2g->index_bw40_base[path][group] = hwinfo[addr++];
			if (pw2g->index_bw40_base[path][group] == 0xFF)
				pw2g->index_bw40_base[path][group] = 0x2D;
		}
		for (cnt = 0; cnt < MAX_TX_COUNT; cnt++) {
			if (cnt == 0) {
				pw2g->bw40_diff[path][cnt] = 0;
				if (hwinfo[addr] == 0xFF) {
					pw2g->bw20_diff[path][cnt] = 0x02;
				} else {
					pw2g->bw20_diff[path][cnt] =
						(hwinfo[addr] & 0xf0) >> 4;
					/*bit sign number to 8 bit sign number*/
					if (pw2g->bw20_diff[path][cnt] & BIT(3))
						pw2g->bw20_diff[path][cnt] |=
									  0xF0;
				}

				if (hwinfo[addr] == 0xFF) {
					pw2g->ofdm_diff[path][cnt] = 0x04;
				} else {
					pw2g->ofdm_diff[path][cnt] =
							(hwinfo[addr] & 0x0f);
					/*bit sign number to 8 bit sign number*/
					if (pw2g->ofdm_diff[path][cnt] & BIT(3))
						pw2g->ofdm_diff[path][cnt] |=
									  0xF0;
				}
				pw2g->cck_diff[path][cnt] = 0;
				addr++;
			} else {
				if (hwinfo[addr] == 0xFF) {
					pw2g->bw40_diff[path][cnt] = 0xFE;
				} else {
					pw2g->bw40_diff[path][cnt] =
						(hwinfo[addr] & 0xf0) >> 4;
					if (pw2g->bw40_diff[path][cnt] & BIT(3))
						pw2g->bw40_diff[path][cnt] |=
									  0xF0;
				}

				if (hwinfo[addr] == 0xFF) {
					pw2g->bw20_diff[path][cnt] = 0xFE;
				} else {
					pw2g->bw20_diff[path][cnt] =
							(hwinfo[addr] & 0x0f);
					if (pw2g->bw20_diff[path][cnt] & BIT(3))
						pw2g->bw20_diff[path][cnt] |=
									  0xF0;
				}
				addr++;

				if (hwinfo[addr] == 0xFF) {
					pw2g->ofdm_diff[path][cnt] = 0xFE;
				} else {
					pw2g->ofdm_diff[path][cnt] =
						(hwinfo[addr] & 0xf0) >> 4;
					if (pw2g->ofdm_diff[path][cnt] & BIT(3))
						pw2g->ofdm_diff[path][cnt] |=
									  0xF0;
				}

				if (hwinfo[addr] == 0xFF)
					pw2g->cck_diff[path][cnt] = 0xFE;
				else {
					pw2g->cck_diff[path][cnt] =
							(hwinfo[addr] & 0x0f);
					if (pw2g->cck_diff[path][cnt] & BIT(3))
						pw2g->cck_diff[path][cnt] |=
									 0xF0;
				}
				addr++;
			}
		}

		/*5G default value*/
		for (group = 0; group < MAX_CHNL_GROUP_5G; group++) {
			pw5g->index_bw40_base[path][group] = hwinfo[addr++];
			if (pw5g->index_bw40_base[path][group] == 0xFF)
				pw5g->index_bw40_base[path][group] = 0xFE;
		}

		for (cnt = 0; cnt < MAX_TX_COUNT; cnt++) {
			if (cnt == 0) {
				pw5g->bw40_diff[path][cnt] = 0;

				if (hwinfo[addr] == 0xFF) {
					pw5g->bw20_diff[path][cnt] = 0;
				} else {
					pw5g->bw20_diff[path][0] =
						(hwinfo[addr] & 0xf0) >> 4;
					if (pw5g->bw20_diff[path][cnt] & BIT(3))
						pw5g->bw20_diff[path][cnt] |=
									  0xF0;
				}

				if (hwinfo[addr] == 0xFF)
					pw5g->ofdm_diff[path][cnt] = 0x04;
				else {
					pw5g->ofdm_diff[path][0] =
							(hwinfo[addr] & 0x0f);
					if (pw5g->ofdm_diff[path][cnt] & BIT(3))
						pw5g->ofdm_diff[path][cnt] |=
									  0xF0;
				}
				addr++;
			} else {
				if (hwinfo[addr] == 0xFF) {
					pw5g->bw40_diff[path][cnt] = 0xFE;
				} else {
					pw5g->bw40_diff[path][cnt] =
						(hwinfo[addr] & 0xf0) >> 4;
					if (pw5g->bw40_diff[path][cnt] & BIT(3))
						pw5g->bw40_diff[path][cnt] |= 0xF0;
				}

				if (hwinfo[addr] == 0xFF) {
					pw5g->bw20_diff[path][cnt] = 0xFE;
				} else {
					pw5g->bw20_diff[path][cnt] =
							(hwinfo[addr] & 0x0f);
					if (pw5g->bw20_diff[path][cnt] & BIT(3))
						pw5g->bw20_diff[path][cnt] |= 0xF0;
				}
				addr++;
			}
		}

		if (hwinfo[addr] == 0xFF) {
			pw5g->ofdm_diff[path][1] = 0xFE;
			pw5g->ofdm_diff[path][2] = 0xFE;
		} else {
			pw5g->ofdm_diff[path][1] = (hwinfo[addr] & 0xf0) >> 4;
			pw5g->ofdm_diff[path][2] = (hwinfo[addr] & 0x0f);
		}
		addr++;

		if (hwinfo[addr] == 0xFF)
			pw5g->ofdm_diff[path][3] = 0xFE;
		else
			pw5g->ofdm_diff[path][3] = (hwinfo[addr] & 0x0f);
		addr++;

		for (cnt = 1; cnt < MAX_TX_COUNT; cnt++) {
			if (pw5g->ofdm_diff[path][cnt] == 0xFF)
				pw5g->ofdm_diff[path][cnt] = 0xFE;
			else if (pw5g->ofdm_diff[path][cnt] & BIT(3))
				pw5g->ofdm_diff[path][cnt] |= 0xF0;
		}
	}
}

static void _rtl8723be_read_txpower_info_from_hwpg(struct ieee80211_hw *hw,
						   bool autoload_fail,
						   u8 *hwinfo)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct txpower_info_2g pw2g;
	struct txpower_info_5g pw5g;
	u8 rf_path, index;
	u8 i;

	_rtl8723be_read_power_value_fromprom(hw, &pw2g, &pw5g, autoload_fail,
					     hwinfo);

	for (rf_path = 0; rf_path < 2; rf_path++) {
		for (i = 0; i < 14; i++) {
			index = _rtl8723be_get_chnl_group(i+1);

			rtlefuse->txpwrlevel_cck[rf_path][i] =
					pw2g.index_cck_base[rf_path][index];
			rtlefuse->txpwrlevel_ht40_1s[rf_path][i] =
					pw2g.index_bw40_base[rf_path][index];
		}
		for (i = 0; i < MAX_TX_COUNT; i++) {
			rtlefuse->txpwr_ht20diff[rf_path][i] =
						pw2g.bw20_diff[rf_path][i];
			rtlefuse->txpwr_ht40diff[rf_path][i] =
						pw2g.bw40_diff[rf_path][i];
			rtlefuse->txpwr_legacyhtdiff[rf_path][i] =
						pw2g.ofdm_diff[rf_path][i];
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

static u8 _rtl8723be_read_package_type(struct ieee80211_hw *hw)
{
	u8 package_type;
	u8 value;

	efuse_power_switch(hw, false, true);
	if (!efuse_one_byte_read(hw, 0x1FB, &value))
		value = 0;
	efuse_power_switch(hw, false, false);

	switch (value & 0x7) {
	case 0x4:
		package_type = PACKAGE_TFBGA79;
		break;
	case 0x5:
		package_type = PACKAGE_TFBGA90;
		break;
	case 0x6:
		package_type = PACKAGE_QFN68;
		break;
	case 0x7:
		package_type = PACKAGE_TFBGA80;
		break;
	default:
		package_type = PACKAGE_DEFAULT;
		break;
	}

	return package_type;
}

static void _rtl8723be_read_adapter_info(struct ieee80211_hw *hw,
					 bool pseudo_test)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	int params[] = {RTL8723BE_EEPROM_ID, EEPROM_VID, EEPROM_DID,
			EEPROM_SVID, EEPROM_SMID, EEPROM_MAC_ADDR,
			EEPROM_CHANNELPLAN, EEPROM_VERSION, EEPROM_CUSTOMER_ID,
			COUNTRY_CODE_WORLD_WIDE_13};
	u8 *hwinfo;
	int i;
	bool is_toshiba_smid1 = false;
	bool is_toshiba_smid2 = false;
	bool is_samsung_smid = false;
	bool is_lenovo_smid = false;
	u16 toshiba_smid1[] = {
		0x6151, 0x6152, 0x6154, 0x6155, 0x6177, 0x6178, 0x6179, 0x6180,
		0x7151, 0x7152, 0x7154, 0x7155, 0x7177, 0x7178, 0x7179, 0x7180,
		0x8151, 0x8152, 0x8154, 0x8155, 0x8181, 0x8182, 0x8184, 0x8185,
		0x9151, 0x9152, 0x9154, 0x9155, 0x9181, 0x9182, 0x9184, 0x9185
	};
	u16 toshiba_smid2[] = {
		0x6181, 0x6184, 0x6185, 0x7181, 0x7182, 0x7184, 0x7185, 0x8181,
		0x8182, 0x8184, 0x8185, 0x9181, 0x9182, 0x9184, 0x9185
	};
	u16 samsung_smid[] = {
		0x6191, 0x6192, 0x6193, 0x7191, 0x7192, 0x7193, 0x8191, 0x8192,
		0x8193, 0x9191, 0x9192, 0x9193
	};
	u16 lenovo_smid[] = {
		0x8195, 0x9195, 0x7194, 0x8200, 0x8201, 0x8202, 0x9199, 0x9200
	};

	if (pseudo_test) {
		/* needs to be added */
		return;
	}

	hwinfo = kzalloc(HWSET_MAX_SIZE, GFP_KERNEL);
	if (!hwinfo)
		return;

	if (rtl_get_hwinfo(hw, rtlpriv, HWSET_MAX_SIZE, hwinfo, params))
		goto exit;

	/*parse xtal*/
	rtlefuse->crystalcap = hwinfo[EEPROM_XTAL_8723BE];
	if (rtlefuse->crystalcap == 0xFF)
		rtlefuse->crystalcap = 0x20;

	_rtl8723be_read_txpower_info_from_hwpg(hw, rtlefuse->autoload_failflag,
					       hwinfo);

	rtl8723be_read_bt_coexist_info_from_hwpg(hw,
						 rtlefuse->autoload_failflag,
						 hwinfo);

	rtlhal->package_type = _rtl8723be_read_package_type(hw);

	/* set channel plan from efuse */
	rtlefuse->channel_plan = rtlefuse->eeprom_channelplan;

	if (rtlhal->oem_id == RT_CID_DEFAULT) {
		/* Does this one have a Toshiba SMID from group 1? */
		for (i = 0; i < sizeof(toshiba_smid1) / sizeof(u16); i++) {
			if (rtlefuse->eeprom_smid == toshiba_smid1[i]) {
				is_toshiba_smid1 = true;
				break;
			}
		}
		/* Does this one have a Toshiba SMID from group 2? */
		for (i = 0; i < sizeof(toshiba_smid2) / sizeof(u16); i++) {
			if (rtlefuse->eeprom_smid == toshiba_smid2[i]) {
				is_toshiba_smid2 = true;
				break;
			}
		}
		/* Does this one have a Samsung SMID? */
		for (i = 0; i < sizeof(samsung_smid) / sizeof(u16); i++) {
			if (rtlefuse->eeprom_smid == samsung_smid[i]) {
				is_samsung_smid = true;
				break;
			}
		}
		/* Does this one have a Lenovo SMID? */
		for (i = 0; i < sizeof(lenovo_smid) / sizeof(u16); i++) {
			if (rtlefuse->eeprom_smid == lenovo_smid[i]) {
				is_lenovo_smid = true;
				break;
			}
		}
		switch (rtlefuse->eeprom_oemid) {
		case EEPROM_CID_DEFAULT:
			if (rtlefuse->eeprom_did == 0x8176) {
				if (rtlefuse->eeprom_svid == 0x10EC &&
				    is_toshiba_smid1) {
					rtlhal->oem_id = RT_CID_TOSHIBA;
				} else if (rtlefuse->eeprom_svid == 0x1025) {
					rtlhal->oem_id = RT_CID_819X_ACER;
				} else if (rtlefuse->eeprom_svid == 0x10EC &&
					   is_samsung_smid) {
					rtlhal->oem_id = RT_CID_819X_SAMSUNG;
				} else if (rtlefuse->eeprom_svid == 0x10EC &&
					   is_lenovo_smid) {
					rtlhal->oem_id = RT_CID_819X_LENOVO;
				} else if ((rtlefuse->eeprom_svid == 0x10EC &&
					    rtlefuse->eeprom_smid == 0x8197) ||
					   (rtlefuse->eeprom_svid == 0x10EC &&
					    rtlefuse->eeprom_smid == 0x9196)) {
					rtlhal->oem_id = RT_CID_819X_CLEVO;
				} else if ((rtlefuse->eeprom_svid == 0x1028 &&
					    rtlefuse->eeprom_smid == 0x8194) ||
					   (rtlefuse->eeprom_svid == 0x1028 &&
					    rtlefuse->eeprom_smid == 0x8198) ||
					   (rtlefuse->eeprom_svid == 0x1028 &&
					    rtlefuse->eeprom_smid == 0x9197) ||
					   (rtlefuse->eeprom_svid == 0x1028 &&
					    rtlefuse->eeprom_smid == 0x9198)) {
					rtlhal->oem_id = RT_CID_819X_DELL;
				} else if ((rtlefuse->eeprom_svid == 0x103C &&
					    rtlefuse->eeprom_smid == 0x1629)) {
					rtlhal->oem_id = RT_CID_819X_HP;
				} else if ((rtlefuse->eeprom_svid == 0x1A32 &&
					   rtlefuse->eeprom_smid == 0x2315)) {
					rtlhal->oem_id = RT_CID_819X_QMI;
				} else if ((rtlefuse->eeprom_svid == 0x10EC &&
					   rtlefuse->eeprom_smid == 0x8203)) {
					rtlhal->oem_id = RT_CID_819X_PRONETS;
				} else if ((rtlefuse->eeprom_svid == 0x1043 &&
					   rtlefuse->eeprom_smid == 0x84B5)) {
					rtlhal->oem_id = RT_CID_819X_EDIMAX_ASUS;
				} else {
					rtlhal->oem_id = RT_CID_DEFAULT;
				}
			} else if (rtlefuse->eeprom_did == 0x8178) {
				if (rtlefuse->eeprom_svid == 0x10EC &&
				    is_toshiba_smid2)
					rtlhal->oem_id = RT_CID_TOSHIBA;
				else if (rtlefuse->eeprom_svid == 0x1025)
					rtlhal->oem_id = RT_CID_819X_ACER;
				else if ((rtlefuse->eeprom_svid == 0x10EC &&
					  rtlefuse->eeprom_smid == 0x8186))
					rtlhal->oem_id = RT_CID_819X_PRONETS;
				else if ((rtlefuse->eeprom_svid == 0x1043 &&
					  rtlefuse->eeprom_smid == 0x84B6))
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
exit:
	kfree(hwinfo);
}

static void _rtl8723be_hal_customized_behavior(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	rtlpriv->ledctl.led_opendrain = true;
	switch (rtlhal->oem_id) {
	case RT_CID_819X_HP:
		rtlpriv->ledctl.led_opendrain = true;
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

void rtl8723be_read_eeprom_info(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 tmp_u1b;

	rtlhal->version = _rtl8723be_read_chip_version(hw);
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
		_rtl8723be_read_adapter_info(hw, false);
	} else {
		pr_err("Autoload ERR!!\n");
	}
	_rtl8723be_hal_customized_behavior(hw);
}

static u8 _rtl8723be_mrate_idx_to_arfr_id(struct ieee80211_hw *hw,
					  u8 rate_index)
{
	u8 ret = 0;
	switch (rate_index) {
	case RATR_INX_WIRELESS_NGB:
		ret = 1;
		break;
	case RATR_INX_WIRELESS_N:
	case RATR_INX_WIRELESS_NG:
		ret = 5;
		break;
	case RATR_INX_WIRELESS_NB:
		ret = 3;
		break;
	case RATR_INX_WIRELESS_GB:
		ret = 6;
		break;
	case RATR_INX_WIRELESS_G:
		ret = 7;
		break;
	case RATR_INX_WIRELESS_B:
		ret = 8;
		break;
	default:
		ret = 0;
		break;
	}
	return ret;
}

static void rtl8723be_update_hal_rate_mask(struct ieee80211_hw *hw,
					   struct ieee80211_sta *sta,
					   u8 rssi_level)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_sta_info *sta_entry = NULL;
	u32 ratr_bitmap;
	u8 ratr_index;
	u8 curtxbw_40mhz = (sta->ht_cap.cap &
			      IEEE80211_HT_CAP_SUP_WIDTH_20_40) ? 1 : 0;
	u8 curshortgi_40mhz = (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_40) ?
				1 : 0;
	u8 curshortgi_20mhz = (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_20) ?
				1 : 0;
	enum wireless_mode wirelessmode = 0;
	bool shortgi = false;
	u8 rate_mask[7];
	u8 macid = 0;

	sta_entry = (struct rtl_sta_info *)sta->drv_priv;
	wirelessmode = sta_entry->wireless_mode;
	if (mac->opmode == NL80211_IFTYPE_STATION ||
	    mac->opmode == NL80211_IFTYPE_MESH_POINT)
		curtxbw_40mhz = mac->bw_40;
	else if (mac->opmode == NL80211_IFTYPE_AP ||
		 mac->opmode == NL80211_IFTYPE_ADHOC)
		macid = sta->aid + 1;

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
		if (rtlphy->rf_type == RF_1T1R) {
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
	*(u32 *)&rate_mask = (ratr_bitmap & 0x0fffffff) |
				       (ratr_index << 28);
	rate_mask[0] = macid;
	rate_mask[1] = _rtl8723be_mrate_idx_to_arfr_id(hw, ratr_index) |
						      (shortgi ? 0x80 : 0x00);
	rate_mask[2] = curtxbw_40mhz;

	rate_mask[3] = (u8)(ratr_bitmap & 0x000000ff);
	rate_mask[4] = (u8)((ratr_bitmap & 0x0000ff00) >> 8);
	rate_mask[5] = (u8)((ratr_bitmap & 0x00ff0000) >> 16);
	rate_mask[6] = (u8)((ratr_bitmap & 0xff000000) >> 24);

	RT_TRACE(rtlpriv, COMP_RATR, DBG_DMESG,
		 "Rate_index:%x, ratr_val:%x, %x:%x:%x:%x:%x:%x:%x\n",
		 ratr_index, ratr_bitmap,
		 rate_mask[0], rate_mask[1],
		 rate_mask[2], rate_mask[3],
		 rate_mask[4], rate_mask[5],
		 rate_mask[6]);
	rtl8723be_fill_h2c_cmd(hw, H2C_8723B_RA_MASK, 7, rate_mask);
	_rtl8723be_set_bcn_ctrl_reg(hw, BIT(3), 0);
}

void rtl8723be_update_hal_rate_tbl(struct ieee80211_hw *hw,
				   struct ieee80211_sta *sta,
				   u8 rssi_level)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	if (rtlpriv->dm.useramask)
		rtl8723be_update_hal_rate_mask(hw, sta, rssi_level);
}

void rtl8723be_update_channel_access_setting(struct ieee80211_hw *hw)
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

bool rtl8723be_gpio_radio_on_off_checking(struct ieee80211_hw *hw, u8 *valid)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	enum rf_pwrstate e_rfpowerstate_toset, cur_rfstate;
	u8 u1tmp;
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

	rtl_write_byte(rtlpriv, REG_GPIO_IO_SEL_2,
		       rtl_read_byte(rtlpriv, REG_GPIO_IO_SEL_2) & ~(BIT(1)));

	u1tmp = rtl_read_byte(rtlpriv, REG_GPIO_PIN_CTRL_2);

	if (rtlphy->polarity_ctl)
		e_rfpowerstate_toset = (u1tmp & BIT(1)) ? ERFOFF : ERFON;
	else
		e_rfpowerstate_toset = (u1tmp & BIT(1)) ? ERFON : ERFOFF;

	if ((ppsc->hwradiooff) && (e_rfpowerstate_toset == ERFON)) {
		RT_TRACE(rtlpriv, COMP_RF, DBG_DMESG,
			 "GPIOChangeRF  - HW Radio ON, RF ON\n");

		e_rfpowerstate_toset = ERFON;
		ppsc->hwradiooff = false;
		b_actuallyset = true;
	} else if (!ppsc->hwradiooff && (e_rfpowerstate_toset == ERFOFF)) {
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

void rtl8723be_set_key(struct ieee80211_hw *hw, u32 key_index,
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
			RT_TRACE(rtlpriv, COMP_ERR, DBG_LOUD,
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

void rtl8723be_read_bt_coexist_info_from_hwpg(struct ieee80211_hw *hw,
					      bool auto_load_fail, u8 *hwinfo)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mod_params *mod_params = rtlpriv->cfg->mod_params;
	u8 value;
	u32 tmpu_32;

	if (!auto_load_fail) {
		tmpu_32 = rtl_read_dword(rtlpriv, REG_MULTI_FUNC_CTRL);
		if (tmpu_32 & BIT(18))
			rtlpriv->btcoexist.btc_info.btcoexist = 1;
		else
			rtlpriv->btcoexist.btc_info.btcoexist = 0;
		value = hwinfo[EEPROM_RF_BT_SETTING_8723B];
		rtlpriv->btcoexist.btc_info.bt_type = BT_RTL8723B;
		rtlpriv->btcoexist.btc_info.ant_num = (value & 0x1);
		rtlpriv->btcoexist.btc_info.single_ant_path =
			 (value & 0x40);	/*0xc3[6]*/
	} else {
		rtlpriv->btcoexist.btc_info.btcoexist = 0;
		rtlpriv->btcoexist.btc_info.bt_type = BT_RTL8723B;
		rtlpriv->btcoexist.btc_info.ant_num = ANT_X2;
		rtlpriv->btcoexist.btc_info.single_ant_path = 0;
	}

	/* override ant_num / ant_path */
	if (mod_params->ant_sel) {
		rtlpriv->btcoexist.btc_info.ant_num =
			(mod_params->ant_sel == 1 ? ANT_X2 : ANT_X1);

		rtlpriv->btcoexist.btc_info.single_ant_path =
			(mod_params->ant_sel == 1 ? 0 : 1);
	}
}

void rtl8723be_bt_reg_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	/* 0:Low, 1:High, 2:From Efuse. */
	rtlpriv->btcoexist.reg_bt_iso = 2;
	/* 0:Idle, 1:None-SCO, 2:SCO, 3:From Counter. */
	rtlpriv->btcoexist.reg_bt_sco = 3;
	/* 0:Disable BT control A-MPDU, 1:Enable BT control A-MPDU. */
	rtlpriv->btcoexist.reg_bt_sco = 0;
}

void rtl8723be_bt_hw_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (rtlpriv->cfg->ops->get_btc_status())
		rtlpriv->btcoexist.btc_ops->btc_init_hw_config(rtlpriv);

}

void rtl8723be_suspend(struct ieee80211_hw *hw)
{
}

void rtl8723be_resume(struct ieee80211_hw *hw)
{
}
