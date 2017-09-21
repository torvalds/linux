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
#include "../pwrseqcmd.h"
#include "pwrseq.h"
#include "../btcoexist/rtl_btc.h"

#define LLT_CONFIG	5

static void _rtl8821ae_return_beacon_queue_skb(struct ieee80211_hw *hw)
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

static void _rtl8821ae_set_bcn_ctrl_reg(struct ieee80211_hw *hw,
					u8 set_bits, u8 clear_bits)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpci->reg_bcn_ctrl_val |= set_bits;
	rtlpci->reg_bcn_ctrl_val &= ~clear_bits;

	rtl_write_byte(rtlpriv, REG_BCN_CTRL, (u8)rtlpci->reg_bcn_ctrl_val);
}

void _rtl8821ae_stop_tx_beacon(struct ieee80211_hw *hw)
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

void _rtl8821ae_resume_tx_beacon(struct ieee80211_hw *hw)
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

static void _rtl8821ae_enable_bcn_sub_func(struct ieee80211_hw *hw)
{
	_rtl8821ae_set_bcn_ctrl_reg(hw, 0, BIT(1));
}

static void _rtl8821ae_disable_bcn_sub_func(struct ieee80211_hw *hw)
{
	_rtl8821ae_set_bcn_ctrl_reg(hw, BIT(1), 0);
}

static void _rtl8821ae_set_fw_clock_on(struct ieee80211_hw *hw,
				       u8 rpwm_val, bool b_need_turn_off_ckk)
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
					goto change_done;
				spin_lock_bh(&rtlpriv->locks.fw_ps_lock);
			}
			spin_unlock_bh(&rtlpriv->locks.fw_ps_lock);
		} else {
			rtlhal->fw_clk_change_in_progress = false;
			spin_unlock_bh(&rtlpriv->locks.fw_ps_lock);
			goto change_done;
		}
	}
change_done:
	if (IS_IN_LOW_POWER_STATE_8821AE(rtlhal->fw_ps_state)) {
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
				rtlhal->fw_ps_state = FW_PS_STATE_RF_ON_8821AE;
				RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
					 "Receive CPWM INT!!! Set rtlhal->FwPSState = %X\n",
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

static void _rtl8821ae_set_fw_clock_off(struct ieee80211_hw *hw,
					u8 rpwm_val)
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

	if (FW_PS_STATE(rtlhal->fw_ps_state) !=
		FW_PS_STATE_RF_OFF_LOW_PWR_8821AE) {
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

static void _rtl8821ae_set_fw_ps_rf_on(struct ieee80211_hw *hw)
{
	u8 rpwm_val = 0;

	rpwm_val |= (FW_PS_STATE_RF_OFF_8821AE | FW_PS_ACK);
	_rtl8821ae_set_fw_clock_on(hw, rpwm_val, true);
}

static void _rtl8821ae_fwlps_leave(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	bool fw_current_inps = false;
	u8 rpwm_val = 0, fw_pwrmode = FW_PS_ACTIVE_MODE;

	if (ppsc->low_power_enable) {
		rpwm_val = (FW_PS_STATE_ALL_ON_8821AE|FW_PS_ACK);/* RF on */
		_rtl8821ae_set_fw_clock_on(hw, rpwm_val, false);
		rtlhal->allow_sw_to_change_hwclc = false;
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_H2C_FW_PWRMODE,
				(u8 *)(&fw_pwrmode));
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_FW_PSMODE_STATUS,
				(u8 *)(&fw_current_inps));
	} else {
		rpwm_val = FW_PS_STATE_ALL_ON_8821AE;	/* RF on */
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_SET_RPWM,
				(u8 *)(&rpwm_val));
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_H2C_FW_PWRMODE,
				(u8 *)(&fw_pwrmode));
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_FW_PSMODE_STATUS,
				(u8 *)(&fw_current_inps));
	}
}

static void _rtl8821ae_fwlps_enter(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	bool fw_current_inps = true;
	u8 rpwm_val;

	if (ppsc->low_power_enable) {
		rpwm_val = FW_PS_STATE_RF_OFF_LOW_PWR_8821AE;	/* RF off */
		rtlpriv->cfg->ops->set_hw_reg(hw,
				HW_VAR_FW_PSMODE_STATUS,
				(u8 *)(&fw_current_inps));
		rtlpriv->cfg->ops->set_hw_reg(hw,
				HW_VAR_H2C_FW_PWRMODE,
				(u8 *)(&ppsc->fwctrl_psmode));
		rtlhal->allow_sw_to_change_hwclc = true;
		_rtl8821ae_set_fw_clock_off(hw, rpwm_val);
	} else {
		rpwm_val = FW_PS_STATE_RF_OFF_8821AE;	/* RF off */
		rtlpriv->cfg->ops->set_hw_reg(hw,
				HW_VAR_FW_PSMODE_STATUS,
				(u8 *)(&fw_current_inps));
		rtlpriv->cfg->ops->set_hw_reg(hw,
				HW_VAR_H2C_FW_PWRMODE,
				(u8 *)(&ppsc->fwctrl_psmode));
		rtlpriv->cfg->ops->set_hw_reg(hw,
				HW_VAR_SET_RPWM,
				(u8 *)(&rpwm_val));
	}
}

static void _rtl8821ae_download_rsvd_page(struct ieee80211_hw *hw,
					  bool dl_whole_packets)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	u8 tmp_regcr, tmp_reg422, bcnvalid_reg;
	u8 count = 0, dlbcn_count = 0;
	bool send_beacon = false;

	tmp_regcr = rtl_read_byte(rtlpriv, REG_CR + 1);
	rtl_write_byte(rtlpriv, REG_CR + 1, (tmp_regcr | BIT(0)));

	_rtl8821ae_set_bcn_ctrl_reg(hw, 0, BIT(3));
	_rtl8821ae_set_bcn_ctrl_reg(hw, BIT(4), 0);

	tmp_reg422 = rtl_read_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2);
	rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2,
		       tmp_reg422 & (~BIT(6)));
	if (tmp_reg422 & BIT(6))
		send_beacon = true;

	do {
		bcnvalid_reg = rtl_read_byte(rtlpriv, REG_TDECTRL + 2);
		rtl_write_byte(rtlpriv, REG_TDECTRL + 2,
			       (bcnvalid_reg | BIT(0)));
		_rtl8821ae_return_beacon_queue_skb(hw);

		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE)
			rtl8812ae_set_fw_rsvdpagepkt(hw, false,
						     dl_whole_packets);
		else
			rtl8821ae_set_fw_rsvdpagepkt(hw, false,
						     dl_whole_packets);

		bcnvalid_reg = rtl_read_byte(rtlpriv, REG_TDECTRL + 2);
		count = 0;
		while (!(bcnvalid_reg & BIT(0)) && count < 20) {
			count++;
			udelay(10);
			bcnvalid_reg = rtl_read_byte(rtlpriv, REG_TDECTRL + 2);
		}
		dlbcn_count++;
	} while (!(bcnvalid_reg & BIT(0)) && dlbcn_count < 5);

	if (!(bcnvalid_reg & BIT(0)))
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Download RSVD page failed!\n");
	if (bcnvalid_reg & BIT(0) && rtlhal->enter_pnp_sleep) {
		rtl_write_byte(rtlpriv, REG_TDECTRL + 2, bcnvalid_reg | BIT(0));
		_rtl8821ae_return_beacon_queue_skb(hw);
		if (send_beacon) {
			dlbcn_count = 0;
			do {
				rtl_write_byte(rtlpriv, REG_TDECTRL + 2,
					       bcnvalid_reg | BIT(0));

				_rtl8821ae_return_beacon_queue_skb(hw);

				if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE)
					rtl8812ae_set_fw_rsvdpagepkt(hw, true,
								     false);
				else
					rtl8821ae_set_fw_rsvdpagepkt(hw, true,
								     false);

				/* check rsvd page download OK. */
				bcnvalid_reg = rtl_read_byte(rtlpriv,
							     REG_TDECTRL + 2);
				count = 0;
				while (!(bcnvalid_reg & BIT(0)) && count < 20) {
					count++;
					udelay(10);
					bcnvalid_reg =
					  rtl_read_byte(rtlpriv,
							REG_TDECTRL + 2);
				}
				dlbcn_count++;
			} while (!(bcnvalid_reg & BIT(0)) && dlbcn_count < 5);

			if (!(bcnvalid_reg & BIT(0)))
				RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
					 "2 Download RSVD page failed!\n");
		}
	}

	if (bcnvalid_reg & BIT(0))
		rtl_write_byte(rtlpriv, REG_TDECTRL + 2, BIT(0));

	_rtl8821ae_set_bcn_ctrl_reg(hw, BIT(3), 0);
	_rtl8821ae_set_bcn_ctrl_reg(hw, 0, BIT(4));

	if (send_beacon)
		rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2, tmp_reg422);

	if (!rtlhal->enter_pnp_sleep) {
		tmp_regcr = rtl_read_byte(rtlpriv, REG_CR + 1);
		rtl_write_byte(rtlpriv, REG_CR + 1, (tmp_regcr & ~(BIT(0))));
	}
}

void rtl8821ae_get_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));

	switch (variable) {
	case HW_VAR_ETHER_ADDR:
		*((u32 *)(val)) = rtl_read_dword(rtlpriv, REG_MACID);
		*((u16 *)(val+4)) = rtl_read_word(rtlpriv, REG_MACID + 4);
		break;
	case HW_VAR_BSSID:
		*((u32 *)(val)) = rtl_read_dword(rtlpriv, REG_BSSID);
		*((u16 *)(val+4)) = rtl_read_word(rtlpriv, REG_BSSID+4);
		break;
	case HW_VAR_MEDIA_STATUS:
		val[0] = rtl_read_byte(rtlpriv, MSR) & 0x3;
		break;
	case HW_VAR_SLOT_TIME:
		*((u8 *)(val)) = mac->slot_time;
		break;
	case HW_VAR_BEACON_INTERVAL:
		*((u16 *)(val)) = rtl_read_word(rtlpriv, REG_BCN_INTERVAL);
		break;
	case HW_VAR_ATIM_WINDOW:
		*((u16 *)(val)) =  rtl_read_word(rtlpriv, REG_ATIMWND);
		break;
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
	case HAL_DEF_WOWLAN:
		if (ppsc->wo_wlan_mode)
			*((bool *)(val)) = true;
		else
			*((bool *)(val)) = false;
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_LOUD,
			 "switch case %#x not processed\n", variable);
		break;
	}
}

void rtl8821ae_set_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 idx;

	switch (variable) {
	case HW_VAR_ETHER_ADDR:{
			for (idx = 0; idx < ETH_ALEN; idx++) {
				rtl_write_byte(rtlpriv, (REG_MACID + idx),
					       val[idx]);
			}
			break;
		}
	case HW_VAR_BASIC_RATE:{
			u16 b_rate_cfg = ((u16 *)val)[0];
			b_rate_cfg = b_rate_cfg & 0x15f;
			rtl_write_word(rtlpriv, REG_RRSR, b_rate_cfg);
			break;
		}
	case HW_VAR_BSSID:{
			for (idx = 0; idx < ETH_ALEN; idx++) {
				rtl_write_byte(rtlpriv, (REG_BSSID + idx),
					       val[idx]);
			}
			break;
		}
	case HW_VAR_SIFS:
		rtl_write_byte(rtlpriv, REG_SIFS_CTX + 1, val[0]);
		rtl_write_byte(rtlpriv, REG_SIFS_TRX + 1, val[0]);

		rtl_write_byte(rtlpriv, REG_SPEC_SIFS + 1, val[0]);
		rtl_write_byte(rtlpriv, REG_MAC_SPEC_SIFS + 1, val[0]);

		rtl_write_byte(rtlpriv, REG_RESP_SIFS_OFDM + 1, val[0]);
		rtl_write_byte(rtlpriv, REG_RESP_SIFS_OFDM, val[0]);
		break;
	case HW_VAR_R2T_SIFS:
		rtl_write_byte(rtlpriv, REG_RESP_SIFS_OFDM + 1, val[0]);
		break;
	case HW_VAR_SLOT_TIME:{
		u8 e_aci;

		RT_TRACE(rtlpriv, COMP_MLME, DBG_LOUD,
			 "HW_VAR_SLOT_TIME %x\n", val[0]);

		rtl_write_byte(rtlpriv, REG_SLOT, val[0]);

		for (e_aci = 0; e_aci < AC_MAX; e_aci++) {
			rtlpriv->cfg->ops->set_hw_reg(hw,
						      HW_VAR_AC_PARAM,
						      (u8 *)(&e_aci));
		}
		break; }
	case HW_VAR_ACK_PREAMBLE:{
		u8 reg_tmp;
		u8 short_preamble = (bool)(*(u8 *)val);

		reg_tmp = rtl_read_byte(rtlpriv, REG_TRXPTCL_CTL+2);
		if (short_preamble) {
			reg_tmp |= BIT(1);
			rtl_write_byte(rtlpriv, REG_TRXPTCL_CTL + 2,
				       reg_tmp);
		} else {
			reg_tmp &= (~BIT(1));
			rtl_write_byte(rtlpriv,
				REG_TRXPTCL_CTL + 2,
				reg_tmp);
		}
		break; }
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

		density_to_set = *((u8 *)val);
		mac->min_space_cfg |= (density_to_set << 3);

		RT_TRACE(rtlpriv, COMP_MLME, DBG_LOUD,
			 "Set HW_VAR_SHORTGI_DENSITY: %#x\n",
			  mac->min_space_cfg);

		rtl_write_byte(rtlpriv, REG_AMPDU_MIN_SPACE,
			       mac->min_space_cfg);

		break; }
	case HW_VAR_AMPDU_FACTOR:{
		u32	ampdu_len =  (*((u8 *)val));

		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE) {
			if (ampdu_len < VHT_AGG_SIZE_128K)
				ampdu_len =
					(0x2000 << (*((u8 *)val))) - 1;
			else
				ampdu_len = 0x1ffff;
		} else if (rtlhal->hw_type == HARDWARE_TYPE_RTL8821AE) {
			if (ampdu_len < HT_AGG_SIZE_64K)
				ampdu_len =
					(0x2000 << (*((u8 *)val))) - 1;
			else
				ampdu_len = 0xffff;
		}
		ampdu_len |= BIT(31);

		rtl_write_dword(rtlpriv,
			REG_AMPDU_MAX_LENGTH_8812, ampdu_len);
		break; }
	case HW_VAR_AC_PARAM:{
		u8 e_aci = *((u8 *)val);

		rtl8821ae_dm_init_edca_turbo(hw);
		if (rtlpci->acm_method != EACMWAY2_SW)
			rtlpriv->cfg->ops->set_hw_reg(hw,
						      HW_VAR_ACM_CTRL,
						      (u8 *)(&e_aci));
		break; }
	case HW_VAR_ACM_CTRL:{
		u8 e_aci = *((u8 *)val);
		union aci_aifsn *p_aci_aifsn =
		    (union aci_aifsn *)(&mac->ac[0].aifs);
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
		break; }
	case HW_VAR_RCR:
		rtl_write_dword(rtlpriv, REG_RCR, ((u32 *)(val))[0]);
		rtlpci->receive_config = ((u32 *)(val))[0];
		break;
	case HW_VAR_RETRY_LIMIT:{
		u8 retry_limit = ((u8 *)(val))[0];

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
		rtlefuse->efuse_usedpercentage = *((u8 *)val);
		break;
	case HW_VAR_IO_CMD:
		rtl8821ae_phy_set_io_cmd(hw, (*(enum io_type *)val));
		break;
	case HW_VAR_SET_RPWM:{
		u8 rpwm_val;

		rpwm_val = rtl_read_byte(rtlpriv, REG_PCIE_HRPWM);
		udelay(1);

		if (rpwm_val & BIT(7)) {
			rtl_write_byte(rtlpriv, REG_PCIE_HRPWM,
				       (*(u8 *)val));
		} else {
			rtl_write_byte(rtlpriv, REG_PCIE_HRPWM,
				       ((*(u8 *)val) | BIT(7)));
		}

		break; }
	case HW_VAR_H2C_FW_PWRMODE:
		rtl8821ae_set_fw_pwrmode_cmd(hw, (*(u8 *)val));
		break;
	case HW_VAR_FW_PSMODE_STATUS:
		ppsc->fw_current_inpsmode = *((bool *)val);
		break;
	case HW_VAR_INIT_RTS_RATE:
		break;
	case HW_VAR_RESUME_CLK_ON:
		_rtl8821ae_set_fw_ps_rf_on(hw);
		break;
	case HW_VAR_FW_LPS_ACTION:{
		bool b_enter_fwlps = *((bool *)val);

		if (b_enter_fwlps)
			_rtl8821ae_fwlps_enter(hw);
		 else
			_rtl8821ae_fwlps_leave(hw);
		 break; }
	case HW_VAR_H2C_FW_JOINBSSRPT:{
		u8 mstatus = (*(u8 *)val);

		if (mstatus == RT_MEDIA_CONNECT) {
			rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_AID,
						      NULL);
			_rtl8821ae_download_rsvd_page(hw, false);
		}
		rtl8821ae_set_fw_media_status_rpt_cmd(hw, mstatus);

		break; }
	case HW_VAR_H2C_FW_P2P_PS_OFFLOAD:
		rtl8821ae_set_p2p_ps_offload_cmd(hw, (*(u8 *)val));
		break;
	case HW_VAR_AID:{
		u16 u2btmp;
		u2btmp = rtl_read_word(rtlpriv, REG_BCN_PSR_RPT);
		u2btmp &= 0xC000;
		rtl_write_word(rtlpriv, REG_BCN_PSR_RPT, (u2btmp |
			       mac->assoc_id));
		break; }
	case HW_VAR_CORRECT_TSF:{
		u8 btype_ibss = ((u8 *)(val))[0];

		if (btype_ibss)
			_rtl8821ae_stop_tx_beacon(hw);

		_rtl8821ae_set_bcn_ctrl_reg(hw, 0, BIT(3));

		rtl_write_dword(rtlpriv, REG_TSFTR,
				(u32)(mac->tsf & 0xffffffff));
		rtl_write_dword(rtlpriv, REG_TSFTR + 4,
				(u32)((mac->tsf >> 32) & 0xffffffff));

		_rtl8821ae_set_bcn_ctrl_reg(hw, BIT(3), 0);

		if (btype_ibss)
			_rtl8821ae_resume_tx_beacon(hw);
		break; }
	case HW_VAR_NAV_UPPER: {
		u32	us_nav_upper = *(u32 *)val;

		if (us_nav_upper > HAL_92C_NAV_UPPER_UNIT * 0xFF) {
			RT_TRACE(rtlpriv, COMP_INIT , DBG_WARNING,
				 "The setting value (0x%08X us) of NAV_UPPER is larger than (%d * 0xFF)!!!\n",
				 us_nav_upper, HAL_92C_NAV_UPPER_UNIT);
			break;
		}
		rtl_write_byte(rtlpriv, REG_NAV_UPPER,
			       ((u8)((us_nav_upper +
				HAL_92C_NAV_UPPER_UNIT - 1) /
				HAL_92C_NAV_UPPER_UNIT)));
		break; }
	case HW_VAR_KEEP_ALIVE: {
		u8 array[2];
		array[0] = 0xff;
		array[1] = *((u8 *)val);
		rtl8821ae_fill_h2c_cmd(hw, H2C_8821AE_KEEP_ALIVE_CTRL, 2,
				       array);
		break; }
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_LOUD,
			 "switch case %#x not processed\n", variable);
		break;
	}
}

static bool _rtl8821ae_llt_write(struct ieee80211_hw *hw, u32 address, u32 data)
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

static bool _rtl8821ae_llt_table_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	unsigned short i;
	u8 txpktbuf_bndy;
	u32 rqpn;
	u8 maxpage;
	bool status;

	maxpage = 255;
	txpktbuf_bndy = 0xF7;
	rqpn = 0x80e60808;

	rtl_write_byte(rtlpriv, REG_TRXFF_BNDY, txpktbuf_bndy);
	rtl_write_word(rtlpriv, REG_TRXFF_BNDY + 2, MAX_RX_DMA_BUFFER_SIZE - 1);

	rtl_write_byte(rtlpriv, REG_TDECTRL + 1, txpktbuf_bndy);

	rtl_write_byte(rtlpriv, REG_TXPKTBUF_BCNQ_BDNY, txpktbuf_bndy);
	rtl_write_byte(rtlpriv, REG_TXPKTBUF_MGQ_BDNY, txpktbuf_bndy);

	rtl_write_byte(rtlpriv, REG_PBP, 0x31);
	rtl_write_byte(rtlpriv, REG_RX_DRVINFO_SZ, 0x4);

	for (i = 0; i < (txpktbuf_bndy - 1); i++) {
		status = _rtl8821ae_llt_write(hw, i, i + 1);
		if (!status)
			return status;
	}

	status = _rtl8821ae_llt_write(hw, (txpktbuf_bndy - 1), 0xFF);
	if (!status)
		return status;

	for (i = txpktbuf_bndy; i < maxpage; i++) {
		status = _rtl8821ae_llt_write(hw, i, (i + 1));
		if (!status)
			return status;
	}

	status = _rtl8821ae_llt_write(hw, maxpage, txpktbuf_bndy);
	if (!status)
		return status;

	rtl_write_dword(rtlpriv, REG_RQPN, rqpn);

	rtl_write_byte(rtlpriv, REG_RQPN_NPQ, 0x00);

	return true;
}

static void _rtl8821ae_gen_refresh_led_state(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_led *pled0 = &rtlpriv->ledctl.sw_led0;
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	if (rtlpriv->rtlhal.up_first_time)
		return;

	if (ppsc->rfoff_reason == RF_CHANGE_BY_IPS)
		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE)
			rtl8812ae_sw_led_on(hw, pled0);
		else
			rtl8821ae_sw_led_on(hw, pled0);
	else if (ppsc->rfoff_reason == RF_CHANGE_BY_INIT)
		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE)
			rtl8812ae_sw_led_on(hw, pled0);
		else
			rtl8821ae_sw_led_on(hw, pled0);
	else
		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE)
			rtl8812ae_sw_led_off(hw, pled0);
		else
			rtl8821ae_sw_led_off(hw, pled0);
}

static bool _rtl8821ae_init_mac(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	u8 bytetmp = 0;
	u16 wordtmp = 0;
	bool mac_func_enable = rtlhal->mac_func_enable;

	rtl_write_byte(rtlpriv, REG_RSV_CTRL, 0x00);

	/*Auto Power Down to CHIP-off State*/
	bytetmp = rtl_read_byte(rtlpriv, REG_APS_FSMCO + 1) & (~BIT(7));
	rtl_write_byte(rtlpriv, REG_APS_FSMCO + 1, bytetmp);

	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE) {
		/* HW Power on sequence*/
		if (!rtl_hal_pwrseqcmdparsing(rtlpriv, PWR_CUT_ALL_MSK,
					      PWR_FAB_ALL_MSK, PWR_INTF_PCI_MSK,
					      RTL8812_NIC_ENABLE_FLOW)) {
				RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
					 "init 8812 MAC Fail as power on failure\n");
				return false;
		}
	} else {
		/* HW Power on sequence */
		if (!rtl_hal_pwrseqcmdparsing(rtlpriv, PWR_CUT_A_MSK,
					      PWR_FAB_ALL_MSK, PWR_INTF_PCI_MSK,
					      RTL8821A_NIC_ENABLE_FLOW)){
			RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
				"init 8821 MAC Fail as power on failure\n");
			return false;
		}
	}

	bytetmp = rtl_read_byte(rtlpriv, REG_APS_FSMCO) | BIT(4);
	rtl_write_byte(rtlpriv, REG_APS_FSMCO, bytetmp);

	bytetmp = rtl_read_byte(rtlpriv, REG_CR);
	bytetmp = 0xff;
	rtl_write_byte(rtlpriv, REG_CR, bytetmp);
	mdelay(2);

	bytetmp = 0xff;
	rtl_write_byte(rtlpriv, REG_HWSEQ_CTRL, bytetmp);
	mdelay(2);

	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8821AE) {
		bytetmp = rtl_read_byte(rtlpriv, REG_SYS_CFG + 3);
		if (bytetmp & BIT(0)) {
			bytetmp = rtl_read_byte(rtlpriv, 0x7c);
			bytetmp |= BIT(6);
			rtl_write_byte(rtlpriv, 0x7c, bytetmp);
		}
	}

	bytetmp = rtl_read_byte(rtlpriv, REG_GPIO_MUXCFG + 1);
	bytetmp &= ~BIT(4);
	rtl_write_byte(rtlpriv, REG_GPIO_MUXCFG + 1, bytetmp);

	rtl_write_word(rtlpriv, REG_CR, 0x2ff);

	if (!mac_func_enable) {
		if (!_rtl8821ae_llt_table_init(hw))
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
	/*low address*/
	rtl_write_dword(rtlpriv, REG_BCNQ_DESA,
			rtlpci->tx_ring[BEACON_QUEUE].dma & DMA_BIT_MASK(32));
	rtl_write_dword(rtlpriv, REG_MGQ_DESA,
			rtlpci->tx_ring[MGNT_QUEUE].dma & DMA_BIT_MASK(32));
	rtl_write_dword(rtlpriv, REG_VOQ_DESA,
			rtlpci->tx_ring[VO_QUEUE].dma & DMA_BIT_MASK(32));
	rtl_write_dword(rtlpriv, REG_VIQ_DESA,
			rtlpci->tx_ring[VI_QUEUE].dma & DMA_BIT_MASK(32));
	rtl_write_dword(rtlpriv, REG_BEQ_DESA,
			rtlpci->tx_ring[BE_QUEUE].dma & DMA_BIT_MASK(32));
	rtl_write_dword(rtlpriv, REG_BKQ_DESA,
			rtlpci->tx_ring[BK_QUEUE].dma & DMA_BIT_MASK(32));
	rtl_write_dword(rtlpriv, REG_HQ_DESA,
			rtlpci->tx_ring[HIGH_QUEUE].dma & DMA_BIT_MASK(32));
	rtl_write_dword(rtlpriv, REG_RX_DESA,
			rtlpci->rx_ring[RX_MPDU_QUEUE].dma & DMA_BIT_MASK(32));

	rtl_write_byte(rtlpriv, REG_PCIE_CTRL_REG + 3, 0x77);

	rtl_write_dword(rtlpriv, REG_INT_MIG, 0);

	rtl_write_dword(rtlpriv, REG_MCUTST_1, 0);

	rtl_write_byte(rtlpriv, REG_SECONDARY_CCA_CTRL, 0x3);
	_rtl8821ae_gen_refresh_led_state(hw);

	return true;
}

static void _rtl8821ae_hw_configure(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u32 reg_rrsr;

	reg_rrsr = RATE_ALL_CCK | RATE_ALL_OFDM_AG;

	rtl_write_dword(rtlpriv, REG_RRSR, reg_rrsr);
	/* ARFB table 9 for 11ac 5G 2SS */
	rtl_write_dword(rtlpriv, REG_ARFR0 + 4, 0xfffff000);
	/* ARFB table 10 for 11ac 5G 1SS */
	rtl_write_dword(rtlpriv, REG_ARFR1 + 4, 0x003ff000);
	/* ARFB table 11 for 11ac 24G 1SS */
	rtl_write_dword(rtlpriv, REG_ARFR2, 0x00000015);
	rtl_write_dword(rtlpriv, REG_ARFR2 + 4, 0x003ff000);
	/* ARFB table 12 for 11ac 24G 1SS */
	rtl_write_dword(rtlpriv, REG_ARFR3, 0x00000015);
	rtl_write_dword(rtlpriv, REG_ARFR3 + 4, 0xffcff000);
	/* 0x420[7] = 0 , enable retry AMPDU in new AMPD not singal MPDU. */
	rtl_write_word(rtlpriv, REG_FWHW_TXQ_CTRL, 0x1F00);
	rtl_write_byte(rtlpriv, REG_AMPDU_MAX_TIME, 0x70);

	/*Set retry limit*/
	rtl_write_word(rtlpriv, REG_RL, 0x0707);

	/* Set Data / Response auto rate fallack retry count*/
	rtl_write_dword(rtlpriv, REG_DARFRC, 0x01000000);
	rtl_write_dword(rtlpriv, REG_DARFRC + 4, 0x07060504);
	rtl_write_dword(rtlpriv, REG_RARFRC, 0x01000000);
	rtl_write_dword(rtlpriv, REG_RARFRC + 4, 0x07060504);

	rtlpci->reg_bcn_ctrl_val = 0x1d;
	rtl_write_byte(rtlpriv, REG_BCN_CTRL, rtlpci->reg_bcn_ctrl_val);

	/* TBTT prohibit hold time. Suggested by designer TimChen. */
	rtl_write_byte(rtlpriv, REG_TBTT_PROHIBIT + 1, 0xff);

	/* AGGR_BK_TIME Reg51A 0x16 */
	rtl_write_word(rtlpriv, REG_NAV_PROT_LEN, 0x0040);

	/*For Rx TP. Suggested by SD1 Richard. Added by tynli. 2010.04.12.*/
	rtl_write_dword(rtlpriv, REG_FAST_EDCA_CTRL, 0x03086666);

	rtl_write_byte(rtlpriv, REG_HT_SINGLE_AMPDU, 0x80);
	rtl_write_byte(rtlpriv, REG_RX_PKT_LIMIT, 0x20);
	rtl_write_word(rtlpriv, REG_MAX_AGGR_NUM, 0x1F1F);
}

static u16 _rtl8821ae_mdio_read(struct rtl_priv *rtlpriv, u8 addr)
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

static void _rtl8821ae_mdio_write(struct rtl_priv *rtlpriv, u8 addr, u16 data)
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

static u8 _rtl8821ae_dbi_read(struct rtl_priv *rtlpriv, u16 addr)
{
	u16 read_addr = addr & 0xfffc;
	u8 tmp = 0, count = 0, ret = 0;

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

static void _rtl8821ae_dbi_write(struct rtl_priv *rtlpriv, u16 addr, u8 data)
{
	u8 tmp = 0, count = 0;
	u16 wrtie_addr, remainder = addr % 4;

	wrtie_addr = REG_DBI_WDATA + remainder;
	rtl_write_byte(rtlpriv, wrtie_addr, data);

	wrtie_addr = (addr & 0xfffc) | (BIT(0) << (remainder + 12));
	rtl_write_word(rtlpriv, REG_DBI_ADDR, wrtie_addr);

	rtl_write_byte(rtlpriv, REG_DBI_FLAG, 0x1);

	tmp = rtl_read_byte(rtlpriv, REG_DBI_FLAG);
	count = 0;
	while (tmp && count < 20) {
		udelay(10);
		tmp = rtl_read_byte(rtlpriv, REG_DBI_FLAG);
		count++;
	}
}

static void _rtl8821ae_enable_aspm_back_door(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 tmp;

	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8821AE) {
		if (_rtl8821ae_mdio_read(rtlpriv, 0x04) != 0x8544)
			_rtl8821ae_mdio_write(rtlpriv, 0x04, 0x8544);

		if (_rtl8821ae_mdio_read(rtlpriv, 0x0b) != 0x0070)
			_rtl8821ae_mdio_write(rtlpriv, 0x0b, 0x0070);
	}

	tmp = _rtl8821ae_dbi_read(rtlpriv, 0x70f);
	_rtl8821ae_dbi_write(rtlpriv, 0x70f, tmp | BIT(7));

	tmp = _rtl8821ae_dbi_read(rtlpriv, 0x719);
	_rtl8821ae_dbi_write(rtlpriv, 0x719, tmp | BIT(3) | BIT(4));

	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE) {
		tmp  = _rtl8821ae_dbi_read(rtlpriv, 0x718);
		_rtl8821ae_dbi_write(rtlpriv, 0x718, tmp|BIT(4));
	}
}

void rtl8821ae_enable_hw_security_config(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 sec_reg_value;
	u8 tmp;

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

	tmp = rtl_read_byte(rtlpriv, REG_CR + 1);
	rtl_write_byte(rtlpriv, REG_CR + 1, tmp | BIT(1));

	RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG,
		 "The SECR-value %x\n", sec_reg_value);

	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_WPA_CONFIG, &sec_reg_value);
}

/* Static MacID Mapping (cf. Used in MacIdDoStaticMapping) ---------- */
#define MAC_ID_STATIC_FOR_DEFAULT_PORT				0
#define MAC_ID_STATIC_FOR_BROADCAST_MULTICAST		1
#define MAC_ID_STATIC_FOR_BT_CLIENT_START				2
#define MAC_ID_STATIC_FOR_BT_CLIENT_END				3
/* ----------------------------------------------------------- */

static void rtl8821ae_macid_initialize_mediastatus(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8	media_rpt[4] = {RT_MEDIA_CONNECT, 1,
		MAC_ID_STATIC_FOR_BROADCAST_MULTICAST,
		MAC_ID_STATIC_FOR_BT_CLIENT_END};

	rtlpriv->cfg->ops->set_hw_reg(hw,
		HW_VAR_H2C_FW_MEDIASTATUSRPT, media_rpt);

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "Initialize MacId media status: from %d to %d\n",
		 MAC_ID_STATIC_FOR_BROADCAST_MULTICAST,
		 MAC_ID_STATIC_FOR_BT_CLIENT_END);
}

static bool _rtl8821ae_check_pcie_dma_hang(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 tmp;

	/* write reg 0x350 Bit[26]=1. Enable debug port. */
	tmp = rtl_read_byte(rtlpriv, REG_DBI_CTRL + 3);
	if (!(tmp & BIT(2))) {
		rtl_write_byte(rtlpriv, REG_DBI_CTRL + 3, (tmp | BIT(2)));
		mdelay(100);
	}

	/* read reg 0x350 Bit[25] if 1 : RX hang */
	/* read reg 0x350 Bit[24] if 1 : TX hang */
	tmp = rtl_read_byte(rtlpriv, REG_DBI_CTRL + 3);
	if ((tmp & BIT(0)) || (tmp & BIT(1))) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "CheckPcieDMAHang8821AE(): true! Reset PCIE DMA!\n");
		return true;
	} else {
		return false;
	}
}

static bool _rtl8821ae_reset_pcie_interface_dma(struct ieee80211_hw *hw,
					 bool mac_power_on,
					 bool in_watchdog)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 tmp;
	bool release_mac_rx_pause;
	u8 backup_pcie_dma_pause;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "\n");

	/* 1. Disable register write lock. 0x1c[1] = 0 */
	tmp = rtl_read_byte(rtlpriv, REG_RSV_CTRL);
	tmp &= ~(BIT(1));
	rtl_write_byte(rtlpriv, REG_RSV_CTRL, tmp);
	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8821AE) {
		/* write 0xCC bit[2] = 1'b1 */
		tmp = rtl_read_byte(rtlpriv, REG_PMC_DBG_CTRL2);
		tmp |= BIT(2);
		rtl_write_byte(rtlpriv, REG_PMC_DBG_CTRL2, tmp);
	}

	/* 2. Check and pause TRX DMA */
	/* write 0x284 bit[18] = 1'b1 */
	/* write 0x301 = 0xFF */
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
		/* 3. reset TRX function */
		/* write 0x100 = 0x00 */
		rtl_write_byte(rtlpriv, REG_CR, 0);
	}

	/* 4. Reset PCIe DMA. 0x3[0] = 0 */
	tmp = rtl_read_byte(rtlpriv, REG_SYS_FUNC_EN + 1);
	tmp &= ~(BIT(0));
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN + 1, tmp);

	/* 5. Enable PCIe DMA. 0x3[0] = 1 */
	tmp = rtl_read_byte(rtlpriv, REG_SYS_FUNC_EN + 1);
	tmp |= BIT(0);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN + 1, tmp);

	if (mac_power_on) {
		/* 6. enable TRX function */
		/* write 0x100 = 0xFF */
		rtl_write_byte(rtlpriv, REG_CR, 0xFF);

		/* We should init LLT & RQPN and
		 * prepare Tx/Rx descrptor address later
		 * because MAC function is reset.*/
	}

	/* 7. Restore PCIe autoload down bit */
	/* 8812AE does not has the defination. */
	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8821AE) {
		/* write 0xF8 bit[17] = 1'b1 */
		tmp = rtl_read_byte(rtlpriv, REG_MAC_PHY_CTRL_NORMAL + 2);
		tmp |= BIT(1);
		rtl_write_byte(rtlpriv, REG_MAC_PHY_CTRL_NORMAL + 2, tmp);
	}

	/* In MAC power on state, BB and RF maybe in ON state,
	 * if we release TRx DMA here.
	 * it will cause packets to be started to Tx/Rx,
	 * so we release Tx/Rx DMA later.*/
	if (!mac_power_on/* || in_watchdog*/) {
		/* 8. release TRX DMA */
		/* write 0x284 bit[18] = 1'b0 */
		/* write 0x301 = 0x00 */
		if (release_mac_rx_pause) {
			tmp = rtl_read_byte(rtlpriv, REG_RXDMA_CONTROL);
			rtl_write_byte(rtlpriv, REG_RXDMA_CONTROL,
				       tmp & (~BIT(2)));
		}
		rtl_write_byte(rtlpriv, REG_PCIE_CTRL_REG + 1,
			       backup_pcie_dma_pause);
	}

	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8821AE) {
		/* 9. lock system register */
		/* write 0xCC bit[2] = 1'b0 */
		tmp = rtl_read_byte(rtlpriv, REG_PMC_DBG_CTRL2);
		tmp &= ~(BIT(2));
		rtl_write_byte(rtlpriv, REG_PMC_DBG_CTRL2, tmp);
	}
	return true;
}

static void _rtl8821ae_get_wakeup_reason(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtlpriv);
	u8 fw_reason = 0;
	struct timeval ts;

	fw_reason = rtl_read_byte(rtlpriv, REG_MCUTST_WOWLAN);

	RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD, "WOL Read 0x1c7 = %02X\n",
		 fw_reason);

	ppsc->wakeup_reason = 0;

	rtlhal->last_suspend_sec = ts.tv_sec;

	switch (fw_reason) {
	case FW_WOW_V2_PTK_UPDATE_EVENT:
		ppsc->wakeup_reason = WOL_REASON_PTK_UPDATE;
		do_gettimeofday(&ts);
		ppsc->last_wakeup_time = ts.tv_sec*1000 + ts.tv_usec/1000;
		RT_TRACE(rtlpriv, COMP_POWER, DBG_DMESG,
			 "It's a WOL PTK Key update event!\n");
		break;
	case FW_WOW_V2_GTK_UPDATE_EVENT:
		ppsc->wakeup_reason = WOL_REASON_GTK_UPDATE;
		do_gettimeofday(&ts);
		ppsc->last_wakeup_time = ts.tv_sec*1000 + ts.tv_usec/1000;
		RT_TRACE(rtlpriv, COMP_POWER, DBG_DMESG,
			 "It's a WOL GTK Key update event!\n");
		break;
	case FW_WOW_V2_DISASSOC_EVENT:
		ppsc->wakeup_reason = WOL_REASON_DISASSOC;
		RT_TRACE(rtlpriv, COMP_POWER, DBG_DMESG,
			 "It's a disassociation event!\n");
		break;
	case FW_WOW_V2_DEAUTH_EVENT:
		ppsc->wakeup_reason = WOL_REASON_DEAUTH;
		RT_TRACE(rtlpriv, COMP_POWER, DBG_DMESG,
			 "It's a deauth event!\n");
		break;
	case FW_WOW_V2_FW_DISCONNECT_EVENT:
		ppsc->wakeup_reason = WOL_REASON_AP_LOST;
		RT_TRACE(rtlpriv, COMP_POWER, DBG_DMESG,
			 "It's a Fw disconnect decision (AP lost) event!\n");
	break;
	case FW_WOW_V2_MAGIC_PKT_EVENT:
		ppsc->wakeup_reason = WOL_REASON_MAGIC_PKT;
		RT_TRACE(rtlpriv, COMP_POWER, DBG_DMESG,
			 "It's a magic packet event!\n");
		break;
	case FW_WOW_V2_UNICAST_PKT_EVENT:
		ppsc->wakeup_reason = WOL_REASON_UNICAST_PKT;
		RT_TRACE(rtlpriv, COMP_POWER, DBG_DMESG,
			 "It's an unicast packet event!\n");
		break;
	case FW_WOW_V2_PATTERN_PKT_EVENT:
		ppsc->wakeup_reason = WOL_REASON_PATTERN_PKT;
		RT_TRACE(rtlpriv, COMP_POWER, DBG_DMESG,
			 "It's a pattern match event!\n");
		break;
	case FW_WOW_V2_RTD3_SSID_MATCH_EVENT:
		ppsc->wakeup_reason = WOL_REASON_RTD3_SSID_MATCH;
		RT_TRACE(rtlpriv, COMP_POWER, DBG_DMESG,
			 "It's an RTD3 Ssid match event!\n");
		break;
	case FW_WOW_V2_REALWOW_V2_WAKEUPPKT:
		ppsc->wakeup_reason = WOL_REASON_REALWOW_V2_WAKEUPPKT;
		RT_TRACE(rtlpriv, COMP_POWER, DBG_DMESG,
			 "It's an RealWoW wake packet event!\n");
		break;
	case FW_WOW_V2_REALWOW_V2_ACKLOST:
		ppsc->wakeup_reason = WOL_REASON_REALWOW_V2_ACKLOST;
		RT_TRACE(rtlpriv, COMP_POWER, DBG_DMESG,
			 "It's an RealWoW ack lost event!\n");
		break;
	default:
		RT_TRACE(rtlpriv, COMP_POWER, DBG_DMESG,
			 "WOL Read 0x1c7 = %02X, Unknown reason!\n",
			  fw_reason);
		break;
	}
}

static void _rtl8821ae_init_trx_desc_hw_address(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	/*low address*/
	rtl_write_dword(rtlpriv, REG_BCNQ_DESA,
			rtlpci->tx_ring[BEACON_QUEUE].dma & DMA_BIT_MASK(32));
	rtl_write_dword(rtlpriv, REG_MGQ_DESA,
			rtlpci->tx_ring[MGNT_QUEUE].dma & DMA_BIT_MASK(32));
	rtl_write_dword(rtlpriv, REG_VOQ_DESA,
			rtlpci->tx_ring[VO_QUEUE].dma & DMA_BIT_MASK(32));
	rtl_write_dword(rtlpriv, REG_VIQ_DESA,
			rtlpci->tx_ring[VI_QUEUE].dma & DMA_BIT_MASK(32));
	rtl_write_dword(rtlpriv, REG_BEQ_DESA,
			rtlpci->tx_ring[BE_QUEUE].dma & DMA_BIT_MASK(32));
	rtl_write_dword(rtlpriv, REG_BKQ_DESA,
			rtlpci->tx_ring[BK_QUEUE].dma & DMA_BIT_MASK(32));
	rtl_write_dword(rtlpriv, REG_HQ_DESA,
			rtlpci->tx_ring[HIGH_QUEUE].dma & DMA_BIT_MASK(32));
	rtl_write_dword(rtlpriv, REG_RX_DESA,
			rtlpci->rx_ring[RX_MPDU_QUEUE].dma & DMA_BIT_MASK(32));
}

static bool _rtl8821ae_init_llt_table(struct ieee80211_hw *hw, u32 boundary)
{
	bool status = true;
	u32 i;
	u32 txpktbuf_bndy = boundary;
	u32 last_entry_of_txpktbuf = LAST_ENTRY_OF_TX_PKT_BUFFER;

	for (i = 0 ; i < (txpktbuf_bndy - 1) ; i++) {
		status = _rtl8821ae_llt_write(hw, i , i + 1);
		if (!status)
			return status;
	}

	status = _rtl8821ae_llt_write(hw, (txpktbuf_bndy - 1), 0xFF);
	if (!status)
		return status;

	for (i = txpktbuf_bndy ; i < last_entry_of_txpktbuf ; i++) {
		status = _rtl8821ae_llt_write(hw, i, (i + 1));
		if (!status)
			return status;
	}

	status = _rtl8821ae_llt_write(hw, last_entry_of_txpktbuf,
				      txpktbuf_bndy);
	if (!status)
		return status;

	return status;
}

static bool _rtl8821ae_dynamic_rqpn(struct ieee80211_hw *hw, u32 boundary,
			     u16 npq_rqpn_value, u32 rqpn_val)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 tmp;
	bool ret = true;
	u16 count = 0, tmp16;
	bool support_remote_wakeup;

	rtlpriv->cfg->ops->get_hw_reg(hw, HAL_DEF_WOWLAN,
				      (u8 *)(&support_remote_wakeup));

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "boundary=%#X, NPQ_RQPNValue=%#X, RQPNValue=%#X\n",
		  boundary, npq_rqpn_value, rqpn_val);

	/* stop PCIe DMA
	 * 1. 0x301[7:0] = 0xFE */
	rtl_write_byte(rtlpriv, REG_PCIE_CTRL_REG + 1, 0xFE);

	/* wait TXFF empty
	 * 2. polling till 0x41A[15:0]=0x07FF */
	tmp16 = rtl_read_word(rtlpriv, REG_TXPKT_EMPTY);
	while ((tmp16 & 0x07FF) != 0x07FF) {
		udelay(100);
		tmp16 = rtl_read_word(rtlpriv, REG_TXPKT_EMPTY);
		count++;
		if ((count % 200) == 0) {
			RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
				 "Tx queue is not empty for 20ms!\n");
		}
		if (count >= 1000) {
			RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
				 "Wait for Tx FIFO empty timeout!\n");
			break;
		}
	}

	/* TX pause
	 * 3. reg 0x522=0xFF */
	rtl_write_byte(rtlpriv, REG_TXPAUSE, 0xFF);

	/* Wait TX State Machine OK
	 * 4. polling till reg 0x5FB~0x5F8 = 0x00000000 for 50ms */
	count = 0;
	while (rtl_read_byte(rtlpriv, REG_SCH_TXCMD) != 0) {
		udelay(100);
		count++;
		if (count >= 500) {
			RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
				 "Wait for TX State Machine ready timeout !!\n");
			break;
		}
	}

	/* stop RX DMA path
	 * 5.	0x284[18] = 1
	 * 6.	wait till 0x284[17] == 1
	 * wait RX DMA idle */
	count = 0;
	tmp = rtl_read_byte(rtlpriv, REG_RXDMA_CONTROL);
	rtl_write_byte(rtlpriv, REG_RXDMA_CONTROL, (tmp | BIT(2)));
	do {
		tmp = rtl_read_byte(rtlpriv, REG_RXDMA_CONTROL);
		udelay(10);
		count++;
	} while (!(tmp & BIT(1)) && count < 100);

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "Wait until Rx DMA Idle. count=%d REG[0x286]=0x%x\n",
		  count, tmp);

	/* reset BB
	 * 7.	0x02 [0] = 0 */
	tmp = rtl_read_byte(rtlpriv, REG_SYS_FUNC_EN);
	tmp &= ~(BIT(0));
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, tmp);

	/* Reset TRX MAC
	 * 8.	 0x100 = 0x00
	 * Delay (1ms) */
	rtl_write_byte(rtlpriv, REG_CR, 0x00);
	udelay(1000);

	/* Disable MAC Security Engine
	 * 9.	0x100 bit[9]=0 */
	tmp = rtl_read_byte(rtlpriv, REG_CR + 1);
	tmp &= ~(BIT(1));
	rtl_write_byte(rtlpriv, REG_CR + 1, tmp);

	/* To avoid DD-Tim Circuit hang
	 * 10.	0x553 bit[5]=1 */
	tmp = rtl_read_byte(rtlpriv, REG_DUAL_TSF_RST);
	rtl_write_byte(rtlpriv, REG_DUAL_TSF_RST, (tmp | BIT(5)));

	/* Enable MAC Security Engine
	 * 11.	0x100 bit[9]=1 */
	tmp = rtl_read_byte(rtlpriv, REG_CR + 1);
	rtl_write_byte(rtlpriv, REG_CR + 1, (tmp | BIT(1)));

	/* Enable TRX MAC
	 * 12.	 0x100 = 0xFF
	 *	Delay (1ms) */
	rtl_write_byte(rtlpriv, REG_CR, 0xFF);
	udelay(1000);

	/* Enable BB
	 * 13.	0x02 [0] = 1 */
	tmp = rtl_read_byte(rtlpriv, REG_SYS_FUNC_EN);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, (tmp | BIT(0)));

	/* beacon setting
	 * 14,15. set beacon head page (reg 0x209 and 0x424) */
	rtl_write_byte(rtlpriv, REG_TDECTRL + 1, (u8)boundary);
	rtl_write_byte(rtlpriv, REG_TXPKTBUF_BCNQ_BDNY, (u8)boundary);
	rtl_write_byte(rtlpriv, REG_TXPKTBUF_MGQ_BDNY, (u8)boundary);

	/* 16.	WMAC_LBK_BF_HD 0x45D[7:0]
	 * WMAC_LBK_BF_HD */
	rtl_write_byte(rtlpriv, REG_TXPKTBUF_WMAC_LBK_BF_HD,
		       (u8)boundary);

	rtl_write_word(rtlpriv, REG_TRXFF_BNDY, boundary);

	/* init LLT
	 * 17. init LLT */
	if (!_rtl8821ae_init_llt_table(hw, boundary)) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_WARNING,
			 "Failed to init LLT table!\n");
		return false;
	}

	/* reallocate RQPN
	 * 18. reallocate RQPN and init LLT */
	rtl_write_word(rtlpriv, REG_RQPN_NPQ, npq_rqpn_value);
	rtl_write_dword(rtlpriv, REG_RQPN, rqpn_val);

	/* release Tx pause
	 * 19. 0x522=0x00 */
	rtl_write_byte(rtlpriv, REG_TXPAUSE, 0x00);

	/* enable PCIE DMA
	 * 20. 0x301[7:0] = 0x00
	 * 21. 0x284[18] = 0 */
	rtl_write_byte(rtlpriv, REG_PCIE_CTRL_REG + 1, 0x00);
	tmp = rtl_read_byte(rtlpriv, REG_RXDMA_CONTROL);
	rtl_write_byte(rtlpriv, REG_RXDMA_CONTROL, (tmp&~BIT(2)));

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "End.\n");
	return ret;
}

static void _rtl8821ae_simple_initialize_adapter(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtlpriv);

#if (USE_SPECIFIC_FW_TO_SUPPORT_WOWLAN == 1)
	/* Re-download normal Fw. */
	rtl8821ae_set_fw_related_for_wowlan(hw, false);
#endif

	/* Re-Initialize LLT table. */
	if (rtlhal->re_init_llt_table) {
		u32 rqpn = 0x80e70808;
		u8 rqpn_npq = 0, boundary = 0xF8;
		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE) {
			rqpn = 0x80e90808;
			boundary = 0xFA;
		}
		if (_rtl8821ae_dynamic_rqpn(hw, boundary, rqpn_npq, rqpn))
			rtlhal->re_init_llt_table = false;
	}

	ppsc->rfpwr_state = ERFON;
}

static void _rtl8821ae_enable_l1off(struct ieee80211_hw *hw)
{
	u8 tmp  = 0;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "--->\n");

	tmp = _rtl8821ae_dbi_read(rtlpriv, 0x160);
	if (!(tmp & (BIT(2) | BIT(3)))) {
		RT_TRACE(rtlpriv, COMP_POWER | COMP_INIT, DBG_LOUD,
			 "0x160(%#x)return!!\n", tmp);
		return;
	}

	tmp = _rtl8821ae_mdio_read(rtlpriv, 0x1b);
	_rtl8821ae_mdio_write(rtlpriv, 0x1b, (tmp | BIT(4)));

	tmp = _rtl8821ae_dbi_read(rtlpriv, 0x718);
	_rtl8821ae_dbi_write(rtlpriv, 0x718, tmp | BIT(5));

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "<---\n");
}

static void _rtl8821ae_enable_ltr(struct ieee80211_hw *hw)
{
	u8 tmp  = 0;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "--->\n");

	/* Check 0x98[10] */
	tmp = _rtl8821ae_dbi_read(rtlpriv, 0x99);
	if (!(tmp & BIT(2))) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "<---0x99(%#x) return!!\n", tmp);
		return;
	}

	/* LTR idle latency, 0x90 for 144us */
	rtl_write_dword(rtlpriv, 0x798, 0x88908890);

	/* LTR active latency, 0x3c for 60us */
	rtl_write_dword(rtlpriv, 0x79c, 0x883c883c);

	tmp = rtl_read_byte(rtlpriv, 0x7a4);
	rtl_write_byte(rtlpriv, 0x7a4, (tmp | BIT(4)));

	tmp = rtl_read_byte(rtlpriv, 0x7a4);
	rtl_write_byte(rtlpriv, 0x7a4, (tmp & (~BIT(0))));
	rtl_write_byte(rtlpriv, 0x7a4, (tmp | BIT(0)));

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "<---\n");
}

static bool _rtl8821ae_wowlan_initialize_adapter(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	bool init_finished = true;
	u8 tmp = 0;

	/* Get Fw wake up reason. */
	_rtl8821ae_get_wakeup_reason(hw);

	/* Patch Pcie Rx DMA hang after S3/S4 several times.
	 * The root cause has not be found. */
	if (_rtl8821ae_check_pcie_dma_hang(hw))
		_rtl8821ae_reset_pcie_interface_dma(hw, true, false);

	/* Prepare Tx/Rx Desc Hw address. */
	_rtl8821ae_init_trx_desc_hw_address(hw);

	/* Release Pcie Interface Rx DMA to allow wake packet DMA. */
	rtl_write_byte(rtlpriv, REG_PCIE_CTRL_REG + 1, 0xFE);
	RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD, "Enable PCIE Rx DMA.\n");

	/* Check wake up event.
	 * We should check wake packet bit before disable wowlan by H2C or
	 * Fw will clear the bit. */
	tmp = rtl_read_byte(rtlpriv, REG_FTISR + 3);
	RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
		 "Read REG_FTISR 0x13f = %#X\n", tmp);

	/* Set the WoWLAN related function control disable. */
	rtl8821ae_set_fw_wowlan_mode(hw, false);
	rtl8821ae_set_fw_remote_wake_ctrl_cmd(hw, 0);

	if (rtlhal->hw_rof_enable) {
		tmp = rtl_read_byte(rtlpriv, REG_HSISR + 3);
		if (tmp & BIT(1)) {
			/* Clear GPIO9 ISR */
			rtl_write_byte(rtlpriv, REG_HSISR + 3, tmp | BIT(1));
			init_finished = false;
		} else {
			init_finished = true;
		}
	}

	if (init_finished) {
		_rtl8821ae_simple_initialize_adapter(hw);

		/* Release Pcie Interface Tx DMA. */
		rtl_write_byte(rtlpriv, REG_PCIE_CTRL_REG + 1, 0x00);
		/* Release Pcie RX DMA */
		rtl_write_byte(rtlpriv, REG_RXDMA_CONTROL, 0x02);

		tmp = rtl_read_byte(rtlpriv, REG_CR + 1);
		rtl_write_byte(rtlpriv, REG_CR + 1, (tmp & (~BIT(0))));

		_rtl8821ae_enable_l1off(hw);
		_rtl8821ae_enable_ltr(hw);
	}

	return init_finished;
}

static void _rtl8812ae_bb8812_config_1t(struct ieee80211_hw *hw)
{
	/* BB OFDM RX Path_A */
	rtl_set_bbreg(hw, 0x808, 0xff, 0x11);
	/* BB OFDM TX Path_A */
	rtl_set_bbreg(hw, 0x80c, MASKLWORD, 0x1111);
	/* BB CCK R/Rx Path_A */
	rtl_set_bbreg(hw, 0xa04, 0x0c000000, 0x0);
	/* MCS support */
	rtl_set_bbreg(hw, 0x8bc, 0xc0000060, 0x4);
	/* RF Path_B HSSI OFF */
	rtl_set_bbreg(hw, 0xe00, 0xf, 0x4);
	/* RF Path_B Power Down */
	rtl_set_bbreg(hw, 0xe90, MASKDWORD, 0);
	/* ADDA Path_B OFF */
	rtl_set_bbreg(hw, 0xe60, MASKDWORD, 0);
	rtl_set_bbreg(hw, 0xe64, MASKDWORD, 0);
}

static void _rtl8821ae_poweroff_adapter(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 u1b_tmp;

	rtlhal->mac_func_enable = false;

	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8821AE) {
		/* Combo (PCIe + USB) Card and PCIe-MF Card */
		/* 1. Run LPS WL RFOFF flow */
		/* RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		"=====>CardDisableRTL8812E,RTL8821A_NIC_LPS_ENTER_FLOW\n");
		*/
		rtl_hal_pwrseqcmdparsing(rtlpriv,
			PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK,
			PWR_INTF_PCI_MSK, RTL8821A_NIC_LPS_ENTER_FLOW);
	}
	/* 2. 0x1F[7:0] = 0 */
	/* turn off RF */
	/* rtl_write_byte(rtlpriv, REG_RF_CTRL, 0x00); */
	if ((rtl_read_byte(rtlpriv, REG_MCUFWDL) & BIT(7)) &&
		rtlhal->fw_ready) {
		rtl8821ae_firmware_selfreset(hw);
	}

	/* Reset MCU. Suggested by Filen. */
	u1b_tmp = rtl_read_byte(rtlpriv, REG_SYS_FUNC_EN+1);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN+1, (u1b_tmp & (~BIT(2))));

	/* g.	MCUFWDL 0x80[1:0]=0	 */
	/* reset MCU ready status */
	rtl_write_byte(rtlpriv, REG_MCUFWDL, 0x00);

	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8821AE) {
		/* HW card disable configuration. */
		rtl_hal_pwrseqcmdparsing(rtlpriv, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK,
			PWR_INTF_PCI_MSK, RTL8821A_NIC_DISABLE_FLOW);
	} else {
		/* HW card disable configuration. */
		rtl_hal_pwrseqcmdparsing(rtlpriv, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK,
			PWR_INTF_PCI_MSK, RTL8812_NIC_DISABLE_FLOW);
	}

	/* Reset MCU IO Wrapper */
	u1b_tmp = rtl_read_byte(rtlpriv, REG_RSV_CTRL + 1);
	rtl_write_byte(rtlpriv, REG_RSV_CTRL + 1, (u1b_tmp & (~BIT(0))));
	u1b_tmp = rtl_read_byte(rtlpriv, REG_RSV_CTRL + 1);
	rtl_write_byte(rtlpriv, REG_RSV_CTRL + 1, u1b_tmp | BIT(0));

	/* 7. RSV_CTRL 0x1C[7:0] = 0x0E */
	/* lock ISO/CLK/Power control register */
	rtl_write_byte(rtlpriv, REG_RSV_CTRL, 0x0e);
}

int rtl8821ae_hw_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	bool rtstatus = true;
	int err;
	u8 tmp_u1b;
	bool support_remote_wakeup;
	u32 nav_upper = WIFI_NAV_UPPER_US;

	rtlhal->being_init_adapter = true;
	rtlpriv->cfg->ops->get_hw_reg(hw, HAL_DEF_WOWLAN,
				      (u8 *)(&support_remote_wakeup));
	rtlpriv->intf_ops->disable_aspm(hw);

	/*YP wowlan not considered*/

	tmp_u1b = rtl_read_byte(rtlpriv, REG_CR);
	if (tmp_u1b != 0 && tmp_u1b != 0xEA) {
		rtlhal->mac_func_enable = true;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "MAC has already power on.\n");
	} else {
		rtlhal->mac_func_enable = false;
		rtlhal->fw_ps_state = FW_PS_STATE_ALL_ON_8821AE;
	}

	if (support_remote_wakeup &&
		rtlhal->wake_from_pnp_sleep &&
		rtlhal->mac_func_enable) {
		if (_rtl8821ae_wowlan_initialize_adapter(hw)) {
			rtlhal->being_init_adapter = false;
			return 0;
		}
	}

	if (_rtl8821ae_check_pcie_dma_hang(hw)) {
		_rtl8821ae_reset_pcie_interface_dma(hw,
						    rtlhal->mac_func_enable,
						    false);
		rtlhal->mac_func_enable = false;
	}

	/* Reset MAC/BB/RF status if it is not powered off
	 * before calling initialize Hw flow to prevent
	 * from interface and MAC status mismatch.
	 * 2013.06.21, by tynli. Suggested by SD1 JackieLau. */
	if (rtlhal->mac_func_enable) {
		_rtl8821ae_poweroff_adapter(hw);
		rtlhal->mac_func_enable = false;
	}

	rtstatus = _rtl8821ae_init_mac(hw);
	if (rtstatus != true) {
		pr_err("Init MAC failed\n");
		err = 1;
		return err;
	}

	tmp_u1b = rtl_read_byte(rtlpriv, REG_SYS_CFG);
	tmp_u1b &= 0x7F;
	rtl_write_byte(rtlpriv, REG_SYS_CFG, tmp_u1b);

	err = rtl8821ae_download_fw(hw, false);
	if (err) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 "Failed to download FW. Init HW without FW now\n");
		err = 1;
		rtlhal->fw_ready = false;
		return err;
	} else {
		rtlhal->fw_ready = true;
	}
	ppsc->fw_current_inpsmode = false;
	rtlhal->fw_ps_state = FW_PS_STATE_ALL_ON_8821AE;
	rtlhal->fw_clk_change_in_progress = false;
	rtlhal->allow_sw_to_change_hwclc = false;
	rtlhal->last_hmeboxnum = 0;

	/*SIC_Init(Adapter);
	if(rtlhal->AMPDUBurstMode)
		rtl_write_byte(rtlpriv,REG_AMPDU_BURST_MODE_8812,  0x7F);*/

	rtl8821ae_phy_mac_config(hw);
	/* because last function modify RCR, so we update
	 * rcr var here, or TP will unstable for receive_config
	 * is wrong, RX RCR_ACRC32 will cause TP unstabel & Rx
	 * RCR_APP_ICV will cause mac80211 unassoc for cisco 1252
	rtlpci->receive_config = rtl_read_dword(rtlpriv, REG_RCR);
	rtlpci->receive_config &= ~(RCR_ACRC32 | RCR_AICV);
	rtl_write_dword(rtlpriv, REG_RCR, rtlpci->receive_config);*/
	rtl8821ae_phy_bb_config(hw);

	rtl8821ae_phy_rf_config(hw);

	if (rtlpriv->phy.rf_type == RF_1T1R &&
		rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE)
		_rtl8812ae_bb8812_config_1t(hw);

	_rtl8821ae_hw_configure(hw);

	rtl8821ae_phy_switch_wirelessband(hw, BAND_ON_2_4G);

	/*set wireless mode*/

	rtlhal->mac_func_enable = true;

	rtl_cam_reset_all_entry(hw);

	rtl8821ae_enable_hw_security_config(hw);

	ppsc->rfpwr_state = ERFON;

	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_ETHER_ADDR, mac->mac_addr);
	_rtl8821ae_enable_aspm_back_door(hw);
	rtlpriv->intf_ops->enable_aspm(hw);

	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE &&
	    (rtlhal->rfe_type == 1 || rtlhal->rfe_type == 5))
		rtl_set_bbreg(hw, 0x900, 0x00000303, 0x0302);

	rtl8821ae_bt_hw_init(hw);
	rtlpriv->rtlhal.being_init_adapter = false;

	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_NAV_UPPER, (u8 *)&nav_upper);

	/* rtl8821ae_dm_check_txpower_tracking(hw); */
	/* rtl8821ae_phy_lc_calibrate(hw); */
	if (support_remote_wakeup)
		rtl_write_byte(rtlpriv, REG_WOW_CTRL, 0);

	/* Release Rx DMA*/
	tmp_u1b = rtl_read_byte(rtlpriv, REG_RXDMA_CONTROL);
	if (tmp_u1b & BIT(2)) {
		/* Release Rx DMA if needed*/
		tmp_u1b &= ~BIT(2);
		rtl_write_byte(rtlpriv, REG_RXDMA_CONTROL, tmp_u1b);
	}

	/* Release Tx/Rx PCIE DMA if*/
	rtl_write_byte(rtlpriv, REG_PCIE_CTRL_REG + 1, 0);

	rtl8821ae_dm_init(hw);
	rtl8821ae_macid_initialize_mediastatus(hw);

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "rtl8821ae_hw_init() <====\n");
	return err;
}

static enum version_8821ae _rtl8821ae_read_chip_version(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	enum version_8821ae version = VERSION_UNKNOWN;
	u32 value32;

	value32 = rtl_read_dword(rtlpriv, REG_SYS_CFG);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "ReadChipVersion8812A 0xF0 = 0x%x\n", value32);

	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE)
		rtlphy->rf_type = RF_2T2R;
	else if (rtlhal->hw_type == HARDWARE_TYPE_RTL8821AE)
		rtlphy->rf_type = RF_1T1R;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "RF_Type is %x!!\n", rtlphy->rf_type);

	if (value32 & TRP_VAUX_EN) {
		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE) {
			if (rtlphy->rf_type == RF_2T2R)
				version = VERSION_TEST_CHIP_2T2R_8812;
			else
				version = VERSION_TEST_CHIP_1T1R_8812;
		} else
			version = VERSION_TEST_CHIP_8821;
	} else {
		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE) {
			u32 rtl_id = ((value32 & CHIP_VER_RTL_MASK) >> 12) + 1;

			if (rtlphy->rf_type == RF_2T2R)
				version =
					(enum version_8821ae)(CHIP_8812
					| NORMAL_CHIP |
					RF_TYPE_2T2R);
			else
				version = (enum version_8821ae)(CHIP_8812
					| NORMAL_CHIP);

			version = (enum version_8821ae)(version | (rtl_id << 12));
		} else if (rtlhal->hw_type == HARDWARE_TYPE_RTL8821AE) {
			u32 rtl_id = value32 & CHIP_VER_RTL_MASK;

			version = (enum version_8821ae)(CHIP_8821
				| NORMAL_CHIP | rtl_id);
		}
	}

	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8821AE) {
		/*WL_HWROF_EN.*/
		value32 = rtl_read_dword(rtlpriv, REG_MULTI_FUNC_CTRL);
		rtlhal->hw_rof_enable = ((value32 & WL_HWROF_EN) ? 1 : 0);
	}

	switch (version) {
	case VERSION_TEST_CHIP_1T1R_8812:
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Chip Version ID: VERSION_TEST_CHIP_1T1R_8812\n");
		break;
	case VERSION_TEST_CHIP_2T2R_8812:
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Chip Version ID: VERSION_TEST_CHIP_2T2R_8812\n");
		break;
	case VERSION_NORMAL_TSMC_CHIP_1T1R_8812:
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Chip Version ID:VERSION_NORMAL_TSMC_CHIP_1T1R_8812\n");
		break;
	case VERSION_NORMAL_TSMC_CHIP_2T2R_8812:
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Chip Version ID: VERSION_NORMAL_TSMC_CHIP_2T2R_8812\n");
		break;
	case VERSION_NORMAL_TSMC_CHIP_1T1R_8812_C_CUT:
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Chip Version ID: VERSION_NORMAL_TSMC_CHIP_1T1R_8812 C CUT\n");
		break;
	case VERSION_NORMAL_TSMC_CHIP_2T2R_8812_C_CUT:
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Chip Version ID: VERSION_NORMAL_TSMC_CHIP_2T2R_8812 C CUT\n");
		break;
	case VERSION_TEST_CHIP_8821:
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Chip Version ID: VERSION_TEST_CHIP_8821\n");
		break;
	case VERSION_NORMAL_TSMC_CHIP_8821:
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Chip Version ID: VERSION_NORMAL_TSMC_CHIP_8821 A CUT\n");
		break;
	case VERSION_NORMAL_TSMC_CHIP_8821_B_CUT:
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Chip Version ID: VERSION_NORMAL_TSMC_CHIP_8821 B CUT\n");
		break;
	default:
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Chip Version ID: Unknown (0x%X)\n", version);
		break;
	}

	return version;
}

static int _rtl8821ae_set_media_status(struct ieee80211_hw *hw,
				     enum nl80211_iftype type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 bt_msr = rtl_read_byte(rtlpriv, MSR);
	enum led_ctl_mode ledaction = LED_CTL_NO_LINK;
	bt_msr &= 0xfc;

	rtl_write_dword(rtlpriv, REG_BCN_CTRL, 0);
	RT_TRACE(rtlpriv, COMP_BEACON, DBG_LOUD,
		"clear 0x550 when set HW_VAR_MEDIA_STATUS\n");

	if (type == NL80211_IFTYPE_UNSPECIFIED ||
	    type == NL80211_IFTYPE_STATION) {
		_rtl8821ae_stop_tx_beacon(hw);
		_rtl8821ae_enable_bcn_sub_func(hw);
	} else if (type == NL80211_IFTYPE_ADHOC ||
		type == NL80211_IFTYPE_AP) {
		_rtl8821ae_resume_tx_beacon(hw);
		_rtl8821ae_disable_bcn_sub_func(hw);
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
		pr_err("Network type %d not support!\n", type);
		return 1;
	}

	rtl_write_byte(rtlpriv, MSR, bt_msr);
	rtlpriv->cfg->ops->led_control(hw, ledaction);
	if ((bt_msr & MSR_MASK) == MSR_AP)
		rtl_write_byte(rtlpriv, REG_BCNTCFG + 1, 0x00);
	else
		rtl_write_byte(rtlpriv, REG_BCNTCFG + 1, 0x66);

	return 0;
}

void rtl8821ae_set_check_bssid(struct ieee80211_hw *hw, bool check_bssid)
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
		_rtl8821ae_set_bcn_ctrl_reg(hw, 0, BIT(4));
	} else if (!check_bssid) {
		reg_rcr &= (~(RCR_CBSSID_DATA | RCR_CBSSID_BCN));
		_rtl8821ae_set_bcn_ctrl_reg(hw, BIT(4), 0);
		rtlpriv->cfg->ops->set_hw_reg(hw,
			HW_VAR_RCR, (u8 *)(&reg_rcr));
	}
}

int rtl8821ae_set_network_type(struct ieee80211_hw *hw, enum nl80211_iftype type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "rtl8821ae_set_network_type!\n");

	if (_rtl8821ae_set_media_status(hw, type))
		return -EOPNOTSUPP;

	if (rtlpriv->mac80211.link_state == MAC80211_LINKED) {
		if (type != NL80211_IFTYPE_AP)
			rtl8821ae_set_check_bssid(hw, true);
	} else {
		rtl8821ae_set_check_bssid(hw, false);
	}

	return 0;
}

/* don't set REG_EDCA_BE_PARAM here because mac80211 will send pkt when scan */
void rtl8821ae_set_qos(struct ieee80211_hw *hw, int aci)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	rtl8821ae_dm_init_edca_turbo(hw);
	switch (aci) {
	case AC1_BK:
		rtl_write_dword(rtlpriv, REG_EDCA_BK_PARAM, 0xa44f);
		break;
	case AC0_BE:
		/* rtl_write_dword(rtlpriv, REG_EDCA_BE_PARAM, u4b_ac_param); */
		break;
	case AC2_VI:
		rtl_write_dword(rtlpriv, REG_EDCA_VI_PARAM, 0x5e4322);
		break;
	case AC3_VO:
		rtl_write_dword(rtlpriv, REG_EDCA_VO_PARAM, 0x2f3222);
		break;
	default:
		WARN_ONCE(true, "rtl8821ae: invalid aci: %d !\n", aci);
		break;
	}
}

static void rtl8821ae_clear_interrupt(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 tmp = rtl_read_dword(rtlpriv, REG_HISR);

	rtl_write_dword(rtlpriv, REG_HISR, tmp);

	tmp = rtl_read_dword(rtlpriv, REG_HISRE);
	rtl_write_dword(rtlpriv, REG_HISRE, tmp);

	tmp = rtl_read_dword(rtlpriv, REG_HSISR);
	rtl_write_dword(rtlpriv, REG_HSISR, tmp);
}

void rtl8821ae_enable_interrupt(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	if (rtlpci->int_clear)
		rtl8821ae_clear_interrupt(hw);/*clear it here first*/

	rtl_write_dword(rtlpriv, REG_HIMR, rtlpci->irq_mask[0] & 0xFFFFFFFF);
	rtl_write_dword(rtlpriv, REG_HIMRE, rtlpci->irq_mask[1] & 0xFFFFFFFF);
	rtlpci->irq_enabled = true;
	/* there are some C2H CMDs have been sent before
	system interrupt is enabled, e.g., C2H, CPWM.
	*So we need to clear all C2H events that FW has
	notified, otherwise FW won't schedule any commands anymore.
	*/
	/* rtl_write_byte(rtlpriv, REG_C2HEVT_CLEAR, 0); */
	/*enable system interrupt*/
	rtl_write_dword(rtlpriv, REG_HSIMR, rtlpci->sys_irq_mask & 0xFFFFFFFF);
}

void rtl8821ae_disable_interrupt(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	rtl_write_dword(rtlpriv, REG_HIMR, IMR_DISABLED);
	rtl_write_dword(rtlpriv, REG_HIMRE, IMR_DISABLED);
	rtlpci->irq_enabled = false;
	/*synchronize_irq(rtlpci->pdev->irq);*/
}

static void _rtl8821ae_clear_pci_pme_status(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u16 cap_hdr;
	u8 cap_pointer;
	u8 cap_id = 0xff;
	u8 pmcs_reg;
	u8 cnt = 0;

	/* Get the Capability pointer first,
	 * the Capability Pointer is located at
	 * offset 0x34 from the Function Header */

	pci_read_config_byte(rtlpci->pdev, 0x34, &cap_pointer);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "PCI configuration 0x34 = 0x%2x\n", cap_pointer);

	do {
		pci_read_config_word(rtlpci->pdev, cap_pointer, &cap_hdr);
		cap_id = cap_hdr & 0xFF;

		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "in pci configuration, cap_pointer%x = %x\n",
			  cap_pointer, cap_id);

		if (cap_id == 0x01) {
			break;
		} else {
			/* point to next Capability */
			cap_pointer = (cap_hdr >> 8) & 0xFF;
			/* 0: end of pci capability, 0xff: invalid value */
			if (cap_pointer == 0x00 || cap_pointer == 0xff) {
				cap_id = 0xff;
				break;
			}
		}
	} while (cnt++ < 200);

	if (cap_id == 0x01) {
		/* Get the PM CSR (Control/Status Register),
		 * The PME_Status is located at PM Capatibility offset 5, bit 7
		 */
		pci_read_config_byte(rtlpci->pdev, cap_pointer + 5, &pmcs_reg);

		if (pmcs_reg & BIT(7)) {
			/* PME event occured, clear the PM_Status by write 1 */
			pmcs_reg = pmcs_reg | BIT(7);

			pci_write_config_byte(rtlpci->pdev, cap_pointer + 5,
					      pmcs_reg);
			/* Read it back to check */
			pci_read_config_byte(rtlpci->pdev, cap_pointer + 5,
					     &pmcs_reg);
			RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
				 "Clear PME status 0x%2x to 0x%2x\n",
				  cap_pointer + 5, pmcs_reg);
		} else {
			RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
				 "PME status(0x%2x) = 0x%2x\n",
				  cap_pointer + 5, pmcs_reg);
		}
	} else {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_WARNING,
			 "Cannot find PME Capability\n");
	}
}

void rtl8821ae_card_disable(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtlpriv);
	struct rtl_mac *mac = rtl_mac(rtlpriv);
	enum nl80211_iftype opmode;
	bool support_remote_wakeup;
	u8 tmp;
	u32 count = 0;

	rtlpriv->cfg->ops->get_hw_reg(hw, HAL_DEF_WOWLAN,
				      (u8 *)(&support_remote_wakeup));

	RT_SET_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC);

	if (!(support_remote_wakeup && mac->opmode == NL80211_IFTYPE_STATION)
	    || !rtlhal->enter_pnp_sleep) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "Normal Power off\n");
		mac->link_state = MAC80211_NOLINK;
		opmode = NL80211_IFTYPE_UNSPECIFIED;
		_rtl8821ae_set_media_status(hw, opmode);
		_rtl8821ae_poweroff_adapter(hw);
	} else {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "Wowlan Supported.\n");
		/* 3 <1> Prepare for configuring wowlan related infomations */
		/* Clear Fw WoWLAN event. */
		rtl_write_byte(rtlpriv, REG_MCUTST_WOWLAN, 0x0);

#if (USE_SPECIFIC_FW_TO_SUPPORT_WOWLAN == 1)
		rtl8821ae_set_fw_related_for_wowlan(hw, true);
#endif
		/* Dynamically adjust Tx packet boundary
		 * for download reserved page packet.
		 * reserve 30 pages for rsvd page */
		if (_rtl8821ae_dynamic_rqpn(hw, 0xE0, 0x3, 0x80c20d0d))
			rtlhal->re_init_llt_table = true;

		/* 3 <2> Set Fw releted H2C cmd. */

		/* Set WoWLAN related security information. */
		rtl8821ae_set_fw_global_info_cmd(hw);

		_rtl8821ae_download_rsvd_page(hw, true);

		/* Just enable AOAC related functions when we connect to AP. */
		printk("mac->link_state = %d\n", mac->link_state);
		if (mac->link_state >= MAC80211_LINKED &&
		    mac->opmode == NL80211_IFTYPE_STATION) {
			rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_AID, NULL);
			rtl8821ae_set_fw_media_status_rpt_cmd(hw,
							      RT_MEDIA_CONNECT);

			rtl8821ae_set_fw_wowlan_mode(hw, true);
			/* Enable Fw Keep alive mechanism. */
			rtl8821ae_set_fw_keep_alive_cmd(hw, true);

			/* Enable disconnect decision control. */
			rtl8821ae_set_fw_disconnect_decision_ctrl_cmd(hw, true);
		}

		/* 3 <3> Hw Configutations */

		/* Wait untill Rx DMA Finished before host sleep.
		 * FW Pause Rx DMA may happens when received packet doing dma.
		 */
		rtl_write_byte(rtlpriv, REG_RXDMA_CONTROL, BIT(2));

		tmp = rtl_read_byte(rtlpriv, REG_RXDMA_CONTROL);
		count = 0;
		while (!(tmp & BIT(1)) && (count++ < 100)) {
			udelay(10);
			tmp = rtl_read_byte(rtlpriv, REG_RXDMA_CONTROL);
		}
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Wait Rx DMA Finished before host sleep. count=%d\n",
			  count);

		/* reset trx ring */
		rtlpriv->intf_ops->reset_trx_ring(hw);

		rtl_write_byte(rtlpriv, REG_APS_FSMCO + 1, 0x0);

		_rtl8821ae_clear_pci_pme_status(hw);
		tmp = rtl_read_byte(rtlpriv, REG_SYS_CLKR);
		rtl_write_byte(rtlpriv, REG_SYS_CLKR, tmp | BIT(3));
		/* prevent 8051 to be reset by PERST */
		rtl_write_byte(rtlpriv, REG_RSV_CTRL, 0x20);
		rtl_write_byte(rtlpriv, REG_RSV_CTRL, 0x60);
	}

	if (rtlpriv->rtlhal.driver_is_goingto_unload ||
	    ppsc->rfoff_reason > RF_CHANGE_BY_PS)
		rtlpriv->cfg->ops->led_control(hw, LED_CTL_POWER_OFF);
	/* For wowlan+LPS+32k. */
	if (support_remote_wakeup && rtlhal->enter_pnp_sleep) {
		/* Set the WoWLAN related function control enable.
		 * It should be the last H2C cmd in the WoWLAN flow. */
		rtl8821ae_set_fw_remote_wake_ctrl_cmd(hw, 1);

		/* Stop Pcie Interface Tx DMA. */
		rtl_write_byte(rtlpriv, REG_PCIE_CTRL_REG + 1, 0xff);
		RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD, "Stop PCIE Tx DMA.\n");

		/* Wait for TxDMA idle. */
		count = 0;
		do {
			tmp = rtl_read_byte(rtlpriv, REG_PCIE_CTRL_REG);
			udelay(10);
			count++;
		} while ((tmp != 0) && (count < 100));
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Wait Tx DMA Finished before host sleep. count=%d\n",
			  count);

		if (rtlhal->hw_rof_enable) {
			printk("hw_rof_enable\n");
			tmp = rtl_read_byte(rtlpriv, REG_HSISR + 3);
			rtl_write_byte(rtlpriv, REG_HSISR + 3, tmp | BIT(1));
		}
	}
	/* after power off we should do iqk again */
	rtlpriv->phy.iqk_initialized = false;
}

void rtl8821ae_interrupt_recognized(struct ieee80211_hw *hw,
				  u32 *p_inta, u32 *p_intb)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	*p_inta = rtl_read_dword(rtlpriv, ISR) & rtlpci->irq_mask[0];
	rtl_write_dword(rtlpriv, ISR, *p_inta);

	*p_intb = rtl_read_dword(rtlpriv, REG_HISRE) & rtlpci->irq_mask[1];
	rtl_write_dword(rtlpriv, REG_HISRE, *p_intb);
}

void rtl8821ae_set_beacon_related_registers(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u16 bcn_interval, atim_window;

	bcn_interval = mac->beacon_interval;
	atim_window = 2;	/*FIX MERGE */
	rtl8821ae_disable_interrupt(hw);
	rtl_write_word(rtlpriv, REG_ATIMWND, atim_window);
	rtl_write_word(rtlpriv, REG_BCN_INTERVAL, bcn_interval);
	rtl_write_word(rtlpriv, REG_BCNTCFG, 0x660f);
	rtl_write_byte(rtlpriv, REG_RXTSF_OFFSET_CCK, 0x18);
	rtl_write_byte(rtlpriv, REG_RXTSF_OFFSET_OFDM, 0x18);
	rtl_write_byte(rtlpriv, 0x606, 0x30);
	rtlpci->reg_bcn_ctrl_val |= BIT(3);
	rtl_write_byte(rtlpriv, REG_BCN_CTRL, (u8)rtlpci->reg_bcn_ctrl_val);
	rtl8821ae_enable_interrupt(hw);
}

void rtl8821ae_set_beacon_interval(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u16 bcn_interval = mac->beacon_interval;

	RT_TRACE(rtlpriv, COMP_BEACON, DBG_DMESG,
		 "beacon_interval:%d\n", bcn_interval);
	rtl8821ae_disable_interrupt(hw);
	rtl_write_word(rtlpriv, REG_BCN_INTERVAL, bcn_interval);
	rtl8821ae_enable_interrupt(hw);
}

void rtl8821ae_update_interrupt_mask(struct ieee80211_hw *hw,
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
	rtl8821ae_disable_interrupt(hw);
	rtl8821ae_enable_interrupt(hw);
}

static u8 _rtl8821ae_get_chnl_group(u8 chnl)
{
	u8 group = 0;

	if (chnl <= 14) {
		if (1 <= chnl && chnl <= 2)
			group = 0;
	else if (3 <= chnl && chnl <= 5)
			group = 1;
	else if (6 <= chnl && chnl <= 8)
			group = 2;
	else if (9 <= chnl && chnl <= 11)
			group = 3;
	else /*if (12 <= chnl && chnl <= 14)*/
			group = 4;
	} else {
		if (36 <= chnl && chnl <= 42)
			group = 0;
	else if (44 <= chnl && chnl <= 48)
			group = 1;
	else if (50 <= chnl && chnl <= 58)
			group = 2;
	else if (60 <= chnl && chnl <= 64)
			group = 3;
	else if (100 <= chnl && chnl <= 106)
			group = 4;
	else if (108 <= chnl && chnl <= 114)
			group = 5;
	else if (116 <= chnl && chnl <= 122)
			group = 6;
	else if (124 <= chnl && chnl <= 130)
			group = 7;
	else if (132 <= chnl && chnl <= 138)
			group = 8;
	else if (140 <= chnl && chnl <= 144)
			group = 9;
	else if (149 <= chnl && chnl <= 155)
			group = 10;
	else if (157 <= chnl && chnl <= 161)
			group = 11;
	else if (165 <= chnl && chnl <= 171)
			group = 12;
	else if (173 <= chnl && chnl <= 177)
			group = 13;
	else
		WARN_ONCE(true,
			  "rtl8821ae: 5G, Channel %d in Group not found\n",
			  chnl);
	}
	return group;
}

static void _rtl8821ae_read_power_value_fromprom(struct ieee80211_hw *hw,
	struct txpower_info_2g *pwrinfo24g,
	struct txpower_info_5g *pwrinfo5g,
	bool autoload_fail,
	u8 *hwinfo)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 rfPath, eeAddr = EEPROM_TX_PWR_INX, group, TxCount = 0;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "hal_ReadPowerValueFromPROM8821ae(): hwinfo[0x%x]=0x%x\n",
		 (eeAddr+1), hwinfo[eeAddr+1]);
	if (0xFF == hwinfo[eeAddr+1])  /*YJ,add,120316*/
		autoload_fail = true;

	if (autoload_fail) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "auto load fail : Use Default value!\n");
		for (rfPath = 0 ; rfPath < MAX_RF_PATH ; rfPath++) {
			/*2.4G default value*/
			for (group = 0 ; group < MAX_CHNL_GROUP_24G; group++) {
				pwrinfo24g->index_cck_base[rfPath][group] =	0x2D;
				pwrinfo24g->index_bw40_base[rfPath][group] = 0x2D;
			}
			for (TxCount = 0; TxCount < MAX_TX_COUNT; TxCount++) {
				if (TxCount == 0) {
					pwrinfo24g->bw20_diff[rfPath][0] = 0x02;
					pwrinfo24g->ofdm_diff[rfPath][0] = 0x04;
				} else {
					pwrinfo24g->bw20_diff[rfPath][TxCount] = 0xFE;
					pwrinfo24g->bw40_diff[rfPath][TxCount] = 0xFE;
					pwrinfo24g->cck_diff[rfPath][TxCount] =	0xFE;
					pwrinfo24g->ofdm_diff[rfPath][TxCount] = 0xFE;
				}
			}
			/*5G default value*/
			for (group = 0 ; group < MAX_CHNL_GROUP_5G; group++)
				pwrinfo5g->index_bw40_base[rfPath][group] = 0x2A;

			for (TxCount = 0; TxCount < MAX_TX_COUNT; TxCount++) {
				if (TxCount == 0) {
					pwrinfo5g->ofdm_diff[rfPath][0] = 0x04;
					pwrinfo5g->bw20_diff[rfPath][0] = 0x00;
					pwrinfo5g->bw80_diff[rfPath][0] = 0xFE;
					pwrinfo5g->bw160_diff[rfPath][0] = 0xFE;
				} else {
					pwrinfo5g->ofdm_diff[rfPath][0] = 0xFE;
					pwrinfo5g->bw20_diff[rfPath][0] = 0xFE;
					pwrinfo5g->bw40_diff[rfPath][0] = 0xFE;
					pwrinfo5g->bw80_diff[rfPath][0] = 0xFE;
					pwrinfo5g->bw160_diff[rfPath][0] = 0xFE;
				}
			}
		}
		return;
	}

	rtl_priv(hw)->efuse.txpwr_fromeprom = true;

	for (rfPath = 0 ; rfPath < MAX_RF_PATH ; rfPath++) {
		/*2.4G default value*/
		for (group = 0 ; group < MAX_CHNL_GROUP_24G; group++) {
			pwrinfo24g->index_cck_base[rfPath][group] = hwinfo[eeAddr++];
			if (pwrinfo24g->index_cck_base[rfPath][group] == 0xFF)
				pwrinfo24g->index_cck_base[rfPath][group] = 0x2D;
		}
		for (group = 0 ; group < MAX_CHNL_GROUP_24G - 1; group++) {
			pwrinfo24g->index_bw40_base[rfPath][group] = hwinfo[eeAddr++];
			if (pwrinfo24g->index_bw40_base[rfPath][group] == 0xFF)
				pwrinfo24g->index_bw40_base[rfPath][group] = 0x2D;
		}
		for (TxCount = 0; TxCount < MAX_TX_COUNT; TxCount++) {
			if (TxCount == 0) {
				pwrinfo24g->bw40_diff[rfPath][TxCount] = 0;
				/*bit sign number to 8 bit sign number*/
				pwrinfo24g->bw20_diff[rfPath][TxCount] = (hwinfo[eeAddr] & 0xf0) >> 4;
				if (pwrinfo24g->bw20_diff[rfPath][TxCount] & BIT(3))
					pwrinfo24g->bw20_diff[rfPath][TxCount] |= 0xF0;
				/*bit sign number to 8 bit sign number*/
				pwrinfo24g->ofdm_diff[rfPath][TxCount] = (hwinfo[eeAddr] & 0x0f);
				if (pwrinfo24g->ofdm_diff[rfPath][TxCount] & BIT(3))
					pwrinfo24g->ofdm_diff[rfPath][TxCount] |= 0xF0;

				pwrinfo24g->cck_diff[rfPath][TxCount] = 0;
				eeAddr++;
			} else {
				pwrinfo24g->bw40_diff[rfPath][TxCount] = (hwinfo[eeAddr]&0xf0) >> 4;
				if (pwrinfo24g->bw40_diff[rfPath][TxCount] & BIT(3))
					pwrinfo24g->bw40_diff[rfPath][TxCount] |= 0xF0;

				pwrinfo24g->bw20_diff[rfPath][TxCount] = (hwinfo[eeAddr] & 0x0f);
				if (pwrinfo24g->bw20_diff[rfPath][TxCount] & BIT(3))
					pwrinfo24g->bw20_diff[rfPath][TxCount] |= 0xF0;

				eeAddr++;

				pwrinfo24g->ofdm_diff[rfPath][TxCount] = (hwinfo[eeAddr] & 0xf0) >> 4;
				if (pwrinfo24g->ofdm_diff[rfPath][TxCount] & BIT(3))
					pwrinfo24g->ofdm_diff[rfPath][TxCount] |= 0xF0;

				pwrinfo24g->cck_diff[rfPath][TxCount] =	(hwinfo[eeAddr] & 0x0f);
				if (pwrinfo24g->cck_diff[rfPath][TxCount] & BIT(3))
					pwrinfo24g->cck_diff[rfPath][TxCount] |= 0xF0;

				eeAddr++;
			}
		}

		/*5G default value*/
		for (group = 0 ; group < MAX_CHNL_GROUP_5G; group++) {
			pwrinfo5g->index_bw40_base[rfPath][group] = hwinfo[eeAddr++];
			if (pwrinfo5g->index_bw40_base[rfPath][group] == 0xFF)
				pwrinfo5g->index_bw40_base[rfPath][group] = 0xFE;
		}

		for (TxCount = 0; TxCount < MAX_TX_COUNT; TxCount++) {
			if (TxCount == 0) {
				pwrinfo5g->bw40_diff[rfPath][TxCount] = 0;

				pwrinfo5g->bw20_diff[rfPath][0] = (hwinfo[eeAddr] & 0xf0) >> 4;
				if (pwrinfo5g->bw20_diff[rfPath][TxCount] & BIT(3))
					pwrinfo5g->bw20_diff[rfPath][TxCount] |= 0xF0;

				pwrinfo5g->ofdm_diff[rfPath][0] = (hwinfo[eeAddr] & 0x0f);
				if (pwrinfo5g->ofdm_diff[rfPath][TxCount] & BIT(3))
					pwrinfo5g->ofdm_diff[rfPath][TxCount] |= 0xF0;

				eeAddr++;
			} else {
				pwrinfo5g->bw40_diff[rfPath][TxCount] = (hwinfo[eeAddr] & 0xf0) >> 4;
				if (pwrinfo5g->bw40_diff[rfPath][TxCount] & BIT(3))
					pwrinfo5g->bw40_diff[rfPath][TxCount] |= 0xF0;

				pwrinfo5g->bw20_diff[rfPath][TxCount] = (hwinfo[eeAddr] & 0x0f);
				if (pwrinfo5g->bw20_diff[rfPath][TxCount] & BIT(3))
					pwrinfo5g->bw20_diff[rfPath][TxCount] |= 0xF0;

				eeAddr++;
			}
		}

		pwrinfo5g->ofdm_diff[rfPath][1] =	(hwinfo[eeAddr] & 0xf0) >> 4;
		pwrinfo5g->ofdm_diff[rfPath][2] =	(hwinfo[eeAddr] & 0x0f);

		eeAddr++;

		pwrinfo5g->ofdm_diff[rfPath][3] = (hwinfo[eeAddr] & 0x0f);

		eeAddr++;

		for (TxCount = 1; TxCount < MAX_TX_COUNT; TxCount++) {
			if (pwrinfo5g->ofdm_diff[rfPath][TxCount] & BIT(3))
				pwrinfo5g->ofdm_diff[rfPath][TxCount] |= 0xF0;
		}
		for (TxCount = 0; TxCount < MAX_TX_COUNT; TxCount++) {
			pwrinfo5g->bw80_diff[rfPath][TxCount] =	(hwinfo[eeAddr] & 0xf0) >> 4;
			/* 4bit sign number to 8 bit sign number */
			if (pwrinfo5g->bw80_diff[rfPath][TxCount] & BIT(3))
				pwrinfo5g->bw80_diff[rfPath][TxCount] |= 0xF0;
			/* 4bit sign number to 8 bit sign number */
			pwrinfo5g->bw160_diff[rfPath][TxCount] = (hwinfo[eeAddr] & 0x0f);
			if (pwrinfo5g->bw160_diff[rfPath][TxCount] & BIT(3))
				pwrinfo5g->bw160_diff[rfPath][TxCount] |= 0xF0;

			eeAddr++;
		}
	}
}
#if 0
static void _rtl8812ae_read_txpower_info_from_hwpg(struct ieee80211_hw *hw,
						 bool autoload_fail,
						 u8 *hwinfo)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct txpower_info_2g pwrinfo24g;
	struct txpower_info_5g pwrinfo5g;
	u8 rf_path, index;
	u8 i;

	_rtl8821ae_read_power_value_fromprom(hw, &pwrinfo24g,
					&pwrinfo5g, autoload_fail, hwinfo);

	for (rf_path = 0; rf_path < 2; rf_path++) {
		for (i = 0; i < CHANNEL_MAX_NUMBER_2G; i++) {
			index = _rtl8821ae_get_chnl_group(i + 1);

			if (i == CHANNEL_MAX_NUMBER_2G - 1) {
				rtlefuse->txpwrlevel_cck[rf_path][i] =
					pwrinfo24g.index_cck_base[rf_path][5];
				rtlefuse->txpwrlevel_ht40_1s[rf_path][i] =
					pwrinfo24g.index_bw40_base[rf_path][index];
			} else {
				rtlefuse->txpwrlevel_cck[rf_path][i] =
					pwrinfo24g.index_cck_base[rf_path][index];
				rtlefuse->txpwrlevel_ht40_1s[rf_path][i] =
					pwrinfo24g.index_bw40_base[rf_path][index];
			}
		}

		for (i = 0; i < CHANNEL_MAX_NUMBER_5G; i++) {
			index = _rtl8821ae_get_chnl_group(channel5g[i]);
			rtlefuse->txpwr_5g_bw40base[rf_path][i] =
					pwrinfo5g.index_bw40_base[rf_path][index];
		}
		for (i = 0; i < CHANNEL_MAX_NUMBER_5G_80M; i++) {
			u8 upper, lower;
			index = _rtl8821ae_get_chnl_group(channel5g_80m[i]);
			upper = pwrinfo5g.index_bw40_base[rf_path][index];
			lower = pwrinfo5g.index_bw40_base[rf_path][index + 1];

			rtlefuse->txpwr_5g_bw80base[rf_path][i] = (upper + lower) / 2;
		}
		for (i = 0; i < MAX_TX_COUNT; i++) {
			rtlefuse->txpwr_cckdiff[rf_path][i] =
				pwrinfo24g.cck_diff[rf_path][i];
			rtlefuse->txpwr_legacyhtdiff[rf_path][i] =
				pwrinfo24g.ofdm_diff[rf_path][i];
			rtlefuse->txpwr_ht20diff[rf_path][i] =
				pwrinfo24g.bw20_diff[rf_path][i];
			rtlefuse->txpwr_ht40diff[rf_path][i] =
				pwrinfo24g.bw40_diff[rf_path][i];

			rtlefuse->txpwr_5g_ofdmdiff[rf_path][i] =
				pwrinfo5g.ofdm_diff[rf_path][i];
			rtlefuse->txpwr_5g_bw20diff[rf_path][i] =
				pwrinfo5g.bw20_diff[rf_path][i];
			rtlefuse->txpwr_5g_bw40diff[rf_path][i] =
				pwrinfo5g.bw40_diff[rf_path][i];
			rtlefuse->txpwr_5g_bw80diff[rf_path][i] =
				pwrinfo5g.bw80_diff[rf_path][i];
		}
	}

	if (!autoload_fail) {
		rtlefuse->eeprom_regulatory =
			hwinfo[EEPROM_RF_BOARD_OPTION] & 0x07;/*bit0~2*/
		if (hwinfo[EEPROM_RF_BOARD_OPTION] == 0xFF)
			rtlefuse->eeprom_regulatory = 0;
	} else {
		rtlefuse->eeprom_regulatory = 0;
	}

	RTPRINT(rtlpriv, FINIT, INIT_TXPOWER,
	"eeprom_regulatory = 0x%x\n", rtlefuse->eeprom_regulatory);
}
#endif
static void _rtl8821ae_read_txpower_info_from_hwpg(struct ieee80211_hw *hw,
						 bool autoload_fail,
						 u8 *hwinfo)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct txpower_info_2g pwrinfo24g;
	struct txpower_info_5g pwrinfo5g;
	u8 rf_path, index;
	u8 i;

	_rtl8821ae_read_power_value_fromprom(hw, &pwrinfo24g,
		&pwrinfo5g, autoload_fail, hwinfo);

	for (rf_path = 0; rf_path < 2; rf_path++) {
		for (i = 0; i < CHANNEL_MAX_NUMBER_2G; i++) {
			index = _rtl8821ae_get_chnl_group(i + 1);

			if (i == CHANNEL_MAX_NUMBER_2G - 1) {
				rtlefuse->txpwrlevel_cck[rf_path][i] =
					pwrinfo24g.index_cck_base[rf_path][5];
				rtlefuse->txpwrlevel_ht40_1s[rf_path][i] =
					pwrinfo24g.index_bw40_base[rf_path][index];
			} else {
				rtlefuse->txpwrlevel_cck[rf_path][i] =
					pwrinfo24g.index_cck_base[rf_path][index];
				rtlefuse->txpwrlevel_ht40_1s[rf_path][i] =
					pwrinfo24g.index_bw40_base[rf_path][index];
			}
		}

		for (i = 0; i < CHANNEL_MAX_NUMBER_5G; i++) {
			index = _rtl8821ae_get_chnl_group(channel5g[i]);
			rtlefuse->txpwr_5g_bw40base[rf_path][i] =
				pwrinfo5g.index_bw40_base[rf_path][index];
		}
		for (i = 0; i < CHANNEL_MAX_NUMBER_5G_80M; i++) {
			u8 upper, lower;
			index = _rtl8821ae_get_chnl_group(channel5g_80m[i]);
			upper = pwrinfo5g.index_bw40_base[rf_path][index];
			lower = pwrinfo5g.index_bw40_base[rf_path][index + 1];

			rtlefuse->txpwr_5g_bw80base[rf_path][i] = (upper + lower) / 2;
		}
		for (i = 0; i < MAX_TX_COUNT; i++) {
			rtlefuse->txpwr_cckdiff[rf_path][i] =
				pwrinfo24g.cck_diff[rf_path][i];
			rtlefuse->txpwr_legacyhtdiff[rf_path][i] =
				pwrinfo24g.ofdm_diff[rf_path][i];
			rtlefuse->txpwr_ht20diff[rf_path][i] =
				pwrinfo24g.bw20_diff[rf_path][i];
			rtlefuse->txpwr_ht40diff[rf_path][i] =
				pwrinfo24g.bw40_diff[rf_path][i];

			rtlefuse->txpwr_5g_ofdmdiff[rf_path][i] =
				pwrinfo5g.ofdm_diff[rf_path][i];
			rtlefuse->txpwr_5g_bw20diff[rf_path][i] =
				pwrinfo5g.bw20_diff[rf_path][i];
			rtlefuse->txpwr_5g_bw40diff[rf_path][i] =
				pwrinfo5g.bw40_diff[rf_path][i];
			rtlefuse->txpwr_5g_bw80diff[rf_path][i] =
				pwrinfo5g.bw80_diff[rf_path][i];
		}
	}
	/*bit0~2*/
	if (!autoload_fail) {
		rtlefuse->eeprom_regulatory = hwinfo[EEPROM_RF_BOARD_OPTION] & 0x07;
		if (hwinfo[EEPROM_RF_BOARD_OPTION] == 0xFF)
			rtlefuse->eeprom_regulatory = 0;
	} else {
		rtlefuse->eeprom_regulatory = 0;
	}

	RTPRINT(rtlpriv, FINIT, INIT_TXPOWER,
	"eeprom_regulatory = 0x%x\n", rtlefuse->eeprom_regulatory);
}

static void _rtl8812ae_read_pa_type(struct ieee80211_hw *hw, u8 *hwinfo,
				    bool autoload_fail)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);

	if (!autoload_fail) {
		rtlhal->pa_type_2g = hwinfo[0xBC];
		rtlhal->lna_type_2g = hwinfo[0xBD];
		if (rtlhal->pa_type_2g == 0xFF && rtlhal->lna_type_2g == 0xFF) {
			rtlhal->pa_type_2g = 0;
			rtlhal->lna_type_2g = 0;
		}
		rtlhal->external_pa_2g = ((rtlhal->pa_type_2g & BIT(5)) &&
					  (rtlhal->pa_type_2g & BIT(4))) ?
					 1 : 0;
		rtlhal->external_lna_2g = ((rtlhal->lna_type_2g & BIT(7)) &&
					   (rtlhal->lna_type_2g & BIT(3))) ?
					  1 : 0;

		rtlhal->pa_type_5g = hwinfo[0xBC];
		rtlhal->lna_type_5g = hwinfo[0xBF];
		if (rtlhal->pa_type_5g == 0xFF && rtlhal->lna_type_5g == 0xFF) {
			rtlhal->pa_type_5g = 0;
			rtlhal->lna_type_5g = 0;
		}
		rtlhal->external_pa_5g = ((rtlhal->pa_type_5g & BIT(1)) &&
					  (rtlhal->pa_type_5g & BIT(0))) ?
					 1 : 0;
		rtlhal->external_lna_5g = ((rtlhal->lna_type_5g & BIT(7)) &&
					   (rtlhal->lna_type_5g & BIT(3))) ?
					  1 : 0;
	} else {
		rtlhal->external_pa_2g  = 0;
		rtlhal->external_lna_2g = 0;
		rtlhal->external_pa_5g  = 0;
		rtlhal->external_lna_5g = 0;
	}
}

static void _rtl8812ae_read_amplifier_type(struct ieee80211_hw *hw, u8 *hwinfo,
					   bool autoload_fail)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);

	u8 ext_type_pa_2g_a  = (hwinfo[0xBD] & BIT(2))      >> 2; /* 0xBD[2] */
	u8 ext_type_pa_2g_b  = (hwinfo[0xBD] & BIT(6))      >> 6; /* 0xBD[6] */
	u8 ext_type_pa_5g_a  = (hwinfo[0xBF] & BIT(2))      >> 2; /* 0xBF[2] */
	u8 ext_type_pa_5g_b  = (hwinfo[0xBF] & BIT(6))      >> 6; /* 0xBF[6] */
	/* 0xBD[1:0] */
	u8 ext_type_lna_2g_a = (hwinfo[0xBD] & (BIT(1) | BIT(0))) >> 0;
	/* 0xBD[5:4] */
	u8 ext_type_lna_2g_b = (hwinfo[0xBD] & (BIT(5) | BIT(4))) >> 4;
	/* 0xBF[1:0] */
	u8 ext_type_lna_5g_a = (hwinfo[0xBF] & (BIT(1) | BIT(0))) >> 0;
	/* 0xBF[5:4] */
	u8 ext_type_lna_5g_b = (hwinfo[0xBF] & (BIT(5) | BIT(4))) >> 4;

	_rtl8812ae_read_pa_type(hw, hwinfo, autoload_fail);

	/* [2.4G] Path A and B are both extPA */
	if ((rtlhal->pa_type_2g & (BIT(5) | BIT(4))) == (BIT(5) | BIT(4)))
		rtlhal->type_gpa  = ext_type_pa_2g_b  << 2 | ext_type_pa_2g_a;

	/* [5G] Path A and B are both extPA */
	if ((rtlhal->pa_type_5g & (BIT(1) | BIT(0))) == (BIT(1) | BIT(0)))
		rtlhal->type_apa  = ext_type_pa_5g_b  << 2 | ext_type_pa_5g_a;

	/* [2.4G] Path A and B are both extLNA */
	if ((rtlhal->lna_type_2g & (BIT(7) | BIT(3))) == (BIT(7) | BIT(3)))
		rtlhal->type_glna = ext_type_lna_2g_b << 2 | ext_type_lna_2g_a;

	/* [5G] Path A and B are both extLNA */
	if ((rtlhal->lna_type_5g & (BIT(7) | BIT(3))) == (BIT(7) | BIT(3)))
		rtlhal->type_alna = ext_type_lna_5g_b << 2 | ext_type_lna_5g_a;
}

static void _rtl8821ae_read_pa_type(struct ieee80211_hw *hw, u8 *hwinfo,
				    bool autoload_fail)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);

	if (!autoload_fail) {
		rtlhal->pa_type_2g = hwinfo[0xBC];
		rtlhal->lna_type_2g = hwinfo[0xBD];
		if (rtlhal->pa_type_2g == 0xFF && rtlhal->lna_type_2g == 0xFF) {
			rtlhal->pa_type_2g = 0;
			rtlhal->lna_type_2g = 0;
		}
		rtlhal->external_pa_2g = (rtlhal->pa_type_2g & BIT(5)) ? 1 : 0;
		rtlhal->external_lna_2g = (rtlhal->lna_type_2g & BIT(7)) ? 1 : 0;

		rtlhal->pa_type_5g = hwinfo[0xBC];
		rtlhal->lna_type_5g = hwinfo[0xBF];
		if (rtlhal->pa_type_5g == 0xFF && rtlhal->lna_type_5g == 0xFF) {
			rtlhal->pa_type_5g = 0;
			rtlhal->lna_type_5g = 0;
		}
		rtlhal->external_pa_5g = (rtlhal->pa_type_5g & BIT(1)) ? 1 : 0;
		rtlhal->external_lna_5g = (rtlhal->lna_type_5g & BIT(7)) ? 1 : 0;
	} else {
		rtlhal->external_pa_2g  = 0;
		rtlhal->external_lna_2g = 0;
		rtlhal->external_pa_5g  = 0;
		rtlhal->external_lna_5g = 0;
	}
}

static void _rtl8821ae_read_rfe_type(struct ieee80211_hw *hw, u8 *hwinfo,
			      bool autoload_fail)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);

	if (!autoload_fail) {
		if (hwinfo[EEPROM_RFE_OPTION] & BIT(7)) {
			if (rtlhal->external_lna_5g) {
				if (rtlhal->external_pa_5g) {
					if (rtlhal->external_lna_2g &&
					    rtlhal->external_pa_2g)
						rtlhal->rfe_type = 3;
					else
						rtlhal->rfe_type = 0;
				} else {
					rtlhal->rfe_type = 2;
				}
			} else {
				rtlhal->rfe_type = 4;
			}
		} else {
			rtlhal->rfe_type = hwinfo[EEPROM_RFE_OPTION] & 0x3F;

			if (rtlhal->rfe_type == 4 &&
			    (rtlhal->external_pa_5g ||
			     rtlhal->external_pa_2g ||
			     rtlhal->external_lna_5g ||
			     rtlhal->external_lna_2g)) {
				if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE)
					rtlhal->rfe_type = 2;
			}
		}
	} else {
		rtlhal->rfe_type = 0x04;
	}

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "RFE Type: 0x%2x\n", rtlhal->rfe_type);
}

static void _rtl8812ae_read_bt_coexist_info_from_hwpg(struct ieee80211_hw *hw,
					      bool auto_load_fail, u8 *hwinfo)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 value;

	if (!auto_load_fail) {
		value = *(u8 *)&hwinfo[EEPROM_RF_BOARD_OPTION];
		if (((value & 0xe0) >> 5) == 0x1)
			rtlpriv->btcoexist.btc_info.btcoexist = 1;
		else
			rtlpriv->btcoexist.btc_info.btcoexist = 0;
		rtlpriv->btcoexist.btc_info.bt_type = BT_RTL8812A;

		value = hwinfo[EEPROM_RF_BT_SETTING];
		rtlpriv->btcoexist.btc_info.ant_num = (value & 0x1);
	} else {
		rtlpriv->btcoexist.btc_info.btcoexist = 0;
		rtlpriv->btcoexist.btc_info.bt_type = BT_RTL8812A;
		rtlpriv->btcoexist.btc_info.ant_num = ANT_X2;
	}
	/*move BT_InitHalVars() to init_sw_vars*/
}

static void _rtl8821ae_read_bt_coexist_info_from_hwpg(struct ieee80211_hw *hw,
					      bool auto_load_fail, u8 *hwinfo)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 value;
	u32 tmpu_32;

	if (!auto_load_fail) {
		tmpu_32 = rtl_read_dword(rtlpriv, REG_MULTI_FUNC_CTRL);
		if (tmpu_32 & BIT(18))
			rtlpriv->btcoexist.btc_info.btcoexist = 1;
		else
			rtlpriv->btcoexist.btc_info.btcoexist = 0;
		rtlpriv->btcoexist.btc_info.bt_type = BT_RTL8821A;

		value = hwinfo[EEPROM_RF_BT_SETTING];
		rtlpriv->btcoexist.btc_info.ant_num = (value & 0x1);
	} else {
		rtlpriv->btcoexist.btc_info.btcoexist = 0;
		rtlpriv->btcoexist.btc_info.bt_type = BT_RTL8821A;
		rtlpriv->btcoexist.btc_info.ant_num = ANT_X2;
	}
	/*move BT_InitHalVars() to init_sw_vars*/
}

static void _rtl8821ae_read_adapter_info(struct ieee80211_hw *hw, bool b_pseudo_test)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	int params[] = {RTL_EEPROM_ID, EEPROM_VID, EEPROM_DID,
			EEPROM_SVID, EEPROM_SMID, EEPROM_MAC_ADDR,
			EEPROM_CHANNELPLAN, EEPROM_VERSION, EEPROM_CUSTOMER_ID,
			COUNTRY_CODE_WORLD_WIDE_13};
	u8 *hwinfo;

	if (b_pseudo_test) {
		;/* need add */
	}

	hwinfo = kzalloc(HWSET_MAX_SIZE, GFP_KERNEL);
	if (!hwinfo)
		return;

	if (rtl_get_hwinfo(hw, rtlpriv, HWSET_MAX_SIZE, hwinfo, params))
		goto exit;

	_rtl8821ae_read_txpower_info_from_hwpg(hw, rtlefuse->autoload_failflag,
					       hwinfo);

	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE) {
		_rtl8812ae_read_amplifier_type(hw, hwinfo,
					       rtlefuse->autoload_failflag);
		_rtl8812ae_read_bt_coexist_info_from_hwpg(hw,
				rtlefuse->autoload_failflag, hwinfo);
	} else {
		_rtl8821ae_read_pa_type(hw, hwinfo, rtlefuse->autoload_failflag);
		_rtl8821ae_read_bt_coexist_info_from_hwpg(hw,
				rtlefuse->autoload_failflag, hwinfo);
	}

	_rtl8821ae_read_rfe_type(hw, hwinfo, rtlefuse->autoload_failflag);
	/*board type*/
	rtlefuse->board_type = ODM_BOARD_DEFAULT;
	if (rtlhal->external_lna_2g != 0)
		rtlefuse->board_type |= ODM_BOARD_EXT_LNA;
	if (rtlhal->external_lna_5g != 0)
		rtlefuse->board_type |= ODM_BOARD_EXT_LNA_5G;
	if (rtlhal->external_pa_2g != 0)
		rtlefuse->board_type |= ODM_BOARD_EXT_PA;
	if (rtlhal->external_pa_5g != 0)
		rtlefuse->board_type |= ODM_BOARD_EXT_PA_5G;

	if (rtlpriv->btcoexist.btc_info.btcoexist == 1)
		rtlefuse->board_type |= ODM_BOARD_BT;

	rtlhal->board_type = rtlefuse->board_type;
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "board_type = 0x%x\n", rtlefuse->board_type);

	rtlefuse->eeprom_channelplan = *(u8 *)&hwinfo[EEPROM_CHANNELPLAN];
	if (rtlefuse->eeprom_channelplan == 0xff)
		rtlefuse->eeprom_channelplan = 0x7F;

	/* set channel plan from efuse */
	rtlefuse->channel_plan = rtlefuse->eeprom_channelplan;

	/*parse xtal*/
	rtlefuse->crystalcap = hwinfo[EEPROM_XTAL_8821AE];
	if (rtlefuse->crystalcap == 0xFF)
		rtlefuse->crystalcap = 0x20;

	rtlefuse->eeprom_thermalmeter = *(u8 *)&hwinfo[EEPROM_THERMAL_METER];
	if ((rtlefuse->eeprom_thermalmeter == 0xff) ||
	    rtlefuse->autoload_failflag) {
		rtlefuse->apk_thermalmeterignore = true;
		rtlefuse->eeprom_thermalmeter = 0xff;
	}

	rtlefuse->thermalmeter[0] = rtlefuse->eeprom_thermalmeter;
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "thermalmeter = 0x%x\n", rtlefuse->eeprom_thermalmeter);

	if (!rtlefuse->autoload_failflag) {
		rtlefuse->antenna_div_cfg =
		  (hwinfo[EEPROM_RF_BOARD_OPTION] & 0x18) >> 3;
		if (hwinfo[EEPROM_RF_BOARD_OPTION] == 0xff)
			rtlefuse->antenna_div_cfg = 0;

		if (rtlpriv->btcoexist.btc_info.btcoexist == 1 &&
		    rtlpriv->btcoexist.btc_info.ant_num == ANT_X1)
			rtlefuse->antenna_div_cfg = 0;

		rtlefuse->antenna_div_type = hwinfo[EEPROM_RF_ANTENNA_OPT_88E];
		if (rtlefuse->antenna_div_type == 0xff)
			rtlefuse->antenna_div_type = FIXED_HW_ANTDIV;
	} else {
		rtlefuse->antenna_div_cfg = 0;
		rtlefuse->antenna_div_type = 0;
	}

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		"SWAS: bHwAntDiv = %x, TRxAntDivType = %x\n",
		rtlefuse->antenna_div_cfg, rtlefuse->antenna_div_type);

	rtlpriv->ledctl.led_opendrain = true;

	if (rtlhal->oem_id == RT_CID_DEFAULT) {
		switch (rtlefuse->eeprom_oemid) {
		case RT_CID_DEFAULT:
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
			break;
		}
	}
exit:
	kfree(hwinfo);
}

/*static void _rtl8821ae_hal_customized_behavior(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
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
}*/

void rtl8821ae_read_eeprom_info(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 tmp_u1b;

	rtlhal->version = _rtl8821ae_read_chip_version(hw);
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
		_rtl8821ae_read_adapter_info(hw, false);
	} else {
		pr_err("Autoload ERR!!\n");
	}
	/*hal_ReadRFType_8812A()*/
	/* _rtl8821ae_hal_customized_behavior(hw); */
}

static void rtl8821ae_update_hal_rate_table(struct ieee80211_hw *hw,
		struct ieee80211_sta *sta)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u32 ratr_value;
	u8 ratr_index = 0;
	u8 b_nmode = mac->ht_enable;
	u8 mimo_ps = IEEE80211_SMPS_OFF;
	u16 shortgi_rate;
	u32 tmp_ratr_value;
	u8 curtxbw_40mhz = mac->bw_40;
	u8 b_curshortgi_40mhz = (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_40) ?
				1 : 0;
	u8 b_curshortgi_20mhz = (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_20) ?
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
		b_nmode = 1;
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

	if ((rtlpriv->btcoexist.bt_coexistence) &&
	     (rtlpriv->btcoexist.bt_coexist_type == BT_CSR_BC4) &&
	     (rtlpriv->btcoexist.bt_cur_state) &&
	     (rtlpriv->btcoexist.bt_ant_isolation) &&
	     ((rtlpriv->btcoexist.bt_service == BT_SCO) ||
	     (rtlpriv->btcoexist.bt_service == BT_BUSY)))
		ratr_value &= 0x0fffcfc0;
	else
		ratr_value &= 0x0FFFFFFF;

	if (b_nmode && ((curtxbw_40mhz &&
			 b_curshortgi_40mhz) || (!curtxbw_40mhz &&
						 b_curshortgi_20mhz))) {
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

static u8 _rtl8821ae_mrate_idx_to_arfr_id(
	struct ieee80211_hw *hw, u8 rate_index,
	enum wireless_mode wirelessmode)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 ret = 0;
	switch (rate_index) {
	case RATR_INX_WIRELESS_NGB:
		if (rtlphy->rf_type == RF_1T1R)
			ret = 1;
		else
			ret = 0;
		; break;
	case RATR_INX_WIRELESS_N:
	case RATR_INX_WIRELESS_NG:
		if (rtlphy->rf_type == RF_1T1R)
			ret = 5;
		else
			ret = 4;
		; break;
	case RATR_INX_WIRELESS_NB:
		if (rtlphy->rf_type == RF_1T1R)
			ret = 3;
		else
			ret = 2;
		; break;
	case RATR_INX_WIRELESS_GB:
		ret = 6;
		break;
	case RATR_INX_WIRELESS_G:
		ret = 7;
		break;
	case RATR_INX_WIRELESS_B:
		ret = 8;
		break;
	case RATR_INX_WIRELESS_MC:
		if ((wirelessmode == WIRELESS_MODE_B)
			|| (wirelessmode == WIRELESS_MODE_G)
			|| (wirelessmode == WIRELESS_MODE_N_24G)
			|| (wirelessmode == WIRELESS_MODE_AC_24G))
			ret = 6;
		else
			ret = 7;
	case RATR_INX_WIRELESS_AC_5N:
		if (rtlphy->rf_type == RF_1T1R)
			ret = 10;
		else
			ret = 9;
		break;
	case RATR_INX_WIRELESS_AC_24N:
		if (rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_80) {
			if (rtlphy->rf_type == RF_1T1R)
				ret = 10;
			else
				ret = 9;
		} else {
			if (rtlphy->rf_type == RF_1T1R)
				ret = 11;
			else
				ret = 12;
		}
		break;
	default:
		ret = 0; break;
	}
	return ret;
}

static u32 _rtl8821ae_rate_to_bitmap_2ssvht(__le16 vht_rate)
{
	u8 i, j, tmp_rate;
	u32 rate_bitmap = 0;

	for (i = j = 0; i < 4; i += 2, j += 10) {
		tmp_rate = (le16_to_cpu(vht_rate) >> i) & 3;

		switch (tmp_rate) {
		case 2:
			rate_bitmap = rate_bitmap | (0x03ff << j);
			break;
		case 1:
			rate_bitmap = rate_bitmap | (0x01ff << j);
			break;
		case 0:
			rate_bitmap = rate_bitmap | (0x00ff << j);
			break;
		default:
			break;
		}
	}

	return rate_bitmap;
}

static u32 _rtl8821ae_set_ra_vht_ratr_bitmap(struct ieee80211_hw *hw,
					     enum wireless_mode wirelessmode,
					     u32 ratr_bitmap)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u32 ret_bitmap = ratr_bitmap;

	if (rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_20_40
		|| rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_80)
		ret_bitmap = ratr_bitmap;
	else if (wirelessmode == WIRELESS_MODE_AC_5G
		|| wirelessmode == WIRELESS_MODE_AC_24G) {
		if (rtlphy->rf_type == RF_1T1R)
			ret_bitmap = ratr_bitmap & (~BIT21);
		else
			ret_bitmap = ratr_bitmap & (~(BIT31|BIT21));
	}

	return ret_bitmap;
}

static u8 _rtl8821ae_get_vht_eni(enum wireless_mode wirelessmode,
			u32 ratr_bitmap)
{
	u8 ret = 0;
	if (wirelessmode < WIRELESS_MODE_N_24G)
		ret =  0;
	else if (wirelessmode == WIRELESS_MODE_AC_24G) {
		if (ratr_bitmap & 0xfff00000)	/* Mix , 2SS */
			ret = 3;
		else					/* Mix, 1SS */
			ret = 2;
	} else if (wirelessmode == WIRELESS_MODE_AC_5G) {
			ret = 1;
	} /* VHT */

	return ret << 4;
}

static u8 _rtl8821ae_get_ra_ldpc(struct ieee80211_hw *hw,
			     u8 mac_id, struct rtl_sta_info *sta_entry,
			     enum wireless_mode wirelessmode)
{
	u8 b_ldpc = 0;
	/*not support ldpc, do not open*/
	return b_ldpc << 2;
}

static u8 _rtl8821ae_get_ra_rftype(struct ieee80211_hw *hw,
			  enum wireless_mode wirelessmode,
			  u32 ratr_bitmap)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 rf_type = RF_1T1R;

	if (rtlphy->rf_type == RF_1T1R)
		rf_type = RF_1T1R;
	else if (wirelessmode == WIRELESS_MODE_AC_5G
		|| wirelessmode == WIRELESS_MODE_AC_24G
		|| wirelessmode == WIRELESS_MODE_AC_ONLY) {
		if (ratr_bitmap & 0xffc00000)
			rf_type = RF_2T2R;
	} else if (wirelessmode == WIRELESS_MODE_N_5G
		|| wirelessmode == WIRELESS_MODE_N_24G) {
		if (ratr_bitmap & 0xfff00000)
			rf_type = RF_2T2R;
	}

	return rf_type;
}

static bool _rtl8821ae_get_ra_shortgi(struct ieee80211_hw *hw, struct ieee80211_sta *sta,
			      u8 mac_id)
{
	bool b_short_gi = false;
	u8 b_curshortgi_40mhz = (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_40) ?
				1 : 0;
	u8 b_curshortgi_20mhz = (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_20) ?
				1 : 0;
	u8 b_curshortgi_80mhz = 0;
	b_curshortgi_80mhz = (sta->vht_cap.cap &
			      IEEE80211_VHT_CAP_SHORT_GI_80) ? 1 : 0;

	if (mac_id == MAC_ID_STATIC_FOR_BROADCAST_MULTICAST)
			b_short_gi = false;

	if (b_curshortgi_40mhz || b_curshortgi_80mhz
		|| b_curshortgi_20mhz)
		b_short_gi = true;

	return b_short_gi;
}

static void rtl8821ae_update_hal_rate_mask(struct ieee80211_hw *hw,
		struct ieee80211_sta *sta, u8 rssi_level)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_sta_info *sta_entry = NULL;
	u32 ratr_bitmap;
	u8 ratr_index;
	enum wireless_mode wirelessmode = 0;
	u8 curtxbw_40mhz = (sta->ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40)
				? 1 : 0;
	bool b_shortgi = false;
	u8 rate_mask[7];
	u8 macid = 0;
	u8 mimo_ps = IEEE80211_SMPS_OFF;
	u8 rf_type;

	sta_entry = (struct rtl_sta_info *)sta->drv_priv;
	wirelessmode = sta_entry->wireless_mode;

	RT_TRACE(rtlpriv, COMP_RATR, DBG_LOUD,
		 "wireless mode = 0x%x\n", wirelessmode);
	if (mac->opmode == NL80211_IFTYPE_STATION ||
		mac->opmode == NL80211_IFTYPE_MESH_POINT) {
		curtxbw_40mhz = mac->bw_40;
	} else if (mac->opmode == NL80211_IFTYPE_AP ||
		mac->opmode == NL80211_IFTYPE_ADHOC)
		macid = sta->aid + 1;
	if (wirelessmode == WIRELESS_MODE_N_5G ||
	    wirelessmode == WIRELESS_MODE_AC_5G ||
	    wirelessmode == WIRELESS_MODE_A)
		ratr_bitmap = sta->supp_rates[NL80211_BAND_5GHZ] << 4;
	else
		ratr_bitmap = sta->supp_rates[NL80211_BAND_2GHZ];

	if (mac->opmode == NL80211_IFTYPE_ADHOC)
		ratr_bitmap = 0xfff;

	if (wirelessmode == WIRELESS_MODE_N_24G
		|| wirelessmode == WIRELESS_MODE_N_5G)
		ratr_bitmap |= (sta->ht_cap.mcs.rx_mask[1] << 20 |
				sta->ht_cap.mcs.rx_mask[0] << 12);
	else if (wirelessmode == WIRELESS_MODE_AC_24G
		|| wirelessmode == WIRELESS_MODE_AC_5G
		|| wirelessmode == WIRELESS_MODE_AC_ONLY)
		ratr_bitmap |= _rtl8821ae_rate_to_bitmap_2ssvht(
				sta->vht_cap.vht_mcs.rx_mcs_map) << 12;

	b_shortgi = _rtl8821ae_get_ra_shortgi(hw, sta, macid);
	rf_type = _rtl8821ae_get_ra_rftype(hw, wirelessmode, ratr_bitmap);

/*mac id owner*/
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

		if (mimo_ps == IEEE80211_SMPS_STATIC
			|| mimo_ps == IEEE80211_SMPS_DYNAMIC) {
			if (rssi_level == 1)
				ratr_bitmap &= 0x000f0000;
			else if (rssi_level == 2)
				ratr_bitmap &= 0x000ff000;
			else
				ratr_bitmap &= 0x000ff005;
		} else {
			if (rf_type == RF_1T1R) {
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
						ratr_bitmap &= 0x0fff0000;
					else if (rssi_level == 2)
						ratr_bitmap &= 0x0ffff000;
					else
						ratr_bitmap &= 0x0ffff015;
				} else {
					if (rssi_level == 1)
						ratr_bitmap &= 0x0fff0000;
					else if (rssi_level == 2)
						ratr_bitmap &= 0x0ffff000;
					else
						ratr_bitmap &= 0x0ffff005;
				}
			}
		}
		break;

	case WIRELESS_MODE_AC_24G:
		ratr_index = RATR_INX_WIRELESS_AC_24N;
		if (rssi_level == 1)
			ratr_bitmap &= 0xfc3f0000;
		else if (rssi_level == 2)
			ratr_bitmap &= 0xfffff000;
		else
			ratr_bitmap &= 0xffffffff;
		break;

	case WIRELESS_MODE_AC_5G:
		ratr_index = RATR_INX_WIRELESS_AC_5N;

		if (rf_type == RF_1T1R) {
			if (rssi_level == 1)	/*add by Gary for ac-series*/
				ratr_bitmap &= 0x003f8000;
			else if (rssi_level == 2)
				ratr_bitmap &= 0x003ff000;
			else
				ratr_bitmap &= 0x003ff010;
		} else {
			if (rssi_level == 1)
				ratr_bitmap &= 0xfe3f8000;
			else if (rssi_level == 2)
				ratr_bitmap &= 0xfffff000;
			else
				ratr_bitmap &= 0xfffff010;
		}
		break;

	default:
		ratr_index = RATR_INX_WIRELESS_NGB;

		if (rf_type == RF_1T2R)
			ratr_bitmap &= 0x000ff0ff;
		else
			ratr_bitmap &= 0x0f8ff0ff;
		break;
	}

	ratr_index = _rtl8821ae_mrate_idx_to_arfr_id(hw, ratr_index, wirelessmode);
	sta_entry->ratr_index = ratr_index;
	ratr_bitmap = _rtl8821ae_set_ra_vht_ratr_bitmap(hw, wirelessmode,
							ratr_bitmap);

	RT_TRACE(rtlpriv, COMP_RATR, DBG_LOUD,
		 "ratr_bitmap :%x\n", ratr_bitmap);

	/* *(u32 *)& rate_mask = EF4BYTE((ratr_bitmap & 0x0fffffff) |
				       (ratr_index << 28)); */

	rate_mask[0] = macid;
	rate_mask[1] = ratr_index | (b_shortgi ? 0x80 : 0x00);
	rate_mask[2] = rtlphy->current_chan_bw
			   | _rtl8821ae_get_vht_eni(wirelessmode, ratr_bitmap)
			   | _rtl8821ae_get_ra_ldpc(hw, macid, sta_entry, wirelessmode);

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
	rtl8821ae_fill_h2c_cmd(hw, H2C_8821AE_RA_MASK, 7, rate_mask);
	_rtl8821ae_set_bcn_ctrl_reg(hw, BIT(3), 0);
}

void rtl8821ae_update_hal_rate_tbl(struct ieee80211_hw *hw,
		struct ieee80211_sta *sta, u8 rssi_level)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	if (rtlpriv->dm.useramask)
		rtl8821ae_update_hal_rate_mask(hw, sta, rssi_level);
	else
		/*RT_TRACE(rtlpriv, COMP_RATR,DBG_LOUD,
			   "rtl8821ae_update_hal_rate_tbl() Error! 8821ae FW RA Only\n");*/
		rtl8821ae_update_hal_rate_table(hw, sta);
}

void rtl8821ae_update_channel_access_setting(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u16 wireless_mode = mac->mode;
	u8 sifs_timer, r2t_sifs;

	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_SLOT_TIME,
				      (u8 *)&mac->slot_time);
	if (wireless_mode == WIRELESS_MODE_G)
		sifs_timer = 0x0a;
	else
		sifs_timer = 0x0e;
	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_SIFS, (u8 *)&sifs_timer);

	r2t_sifs = 0xa;

	if (wireless_mode == WIRELESS_MODE_AC_5G &&
	    (mac->vht_ldpc_cap & LDPC_VHT_ENABLE_RX) &&
	    (mac->vht_stbc_cap & STBC_VHT_ENABLE_RX)) {
		if (mac->vendor == PEER_ATH)
			r2t_sifs = 0x8;
		else
			r2t_sifs = 0xa;
	} else if (wireless_mode == WIRELESS_MODE_AC_5G) {
		r2t_sifs = 0xa;
	}

	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_R2T_SIFS, (u8 *)&r2t_sifs);
}

bool rtl8821ae_gpio_radio_on_off_checking(struct ieee80211_hw *hw, u8 *valid)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	enum rf_pwrstate e_rfpowerstate_toset, cur_rfstate;
	u8 u1tmp = 0;
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
			rtl_read_byte(rtlpriv,
					REG_GPIO_IO_SEL_2) & ~(BIT(1)));

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
	} else if ((!ppsc->hwradiooff)
		   && (e_rfpowerstate_toset == ERFOFF)) {
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

void rtl8821ae_set_key(struct ieee80211_hw *hw, u32 key_index,
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
					entry_id = rtl_cam_get_free_entry(hw, p_macaddr);
					if (entry_id >=  TOTAL_CAM_ENTRY) {
						pr_err("an not find free hwsecurity cam entry\n");
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

void rtl8821ae_bt_reg_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	/* 0:Low, 1:High, 2:From Efuse. */
	rtlpriv->btcoexist.reg_bt_iso = 2;
	/* 0:Idle, 1:None-SCO, 2:SCO, 3:From Counter. */
	rtlpriv->btcoexist.reg_bt_sco = 3;
	/* 0:Disable BT control A-MPDU, 1:Enable BT control A-MPDU. */
	rtlpriv->btcoexist.reg_bt_sco = 0;
}

void rtl8821ae_bt_hw_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (rtlpriv->cfg->ops->get_btc_status())
		rtlpriv->btcoexist.btc_ops->btc_init_hw_config(rtlpriv);
}

void rtl8821ae_suspend(struct ieee80211_hw *hw)
{
}

void rtl8821ae_resume(struct ieee80211_hw *hw)
{
}

/* Turn on AAP (RCR:bit 0) for promicuous mode. */
void rtl8821ae_allow_all_destaddr(struct ieee80211_hw *hw,
	bool allow_all_da, bool write_into_reg)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	if (allow_all_da) /* Set BIT0 */
		rtlpci->receive_config |= RCR_AAP;
	else /* Clear BIT0 */
		rtlpci->receive_config &= ~RCR_AAP;

	if (write_into_reg)
		rtl_write_dword(rtlpriv, REG_RCR, rtlpci->receive_config);

	RT_TRACE(rtlpriv, COMP_TURBO | COMP_INIT, DBG_LOUD,
		"receive_config=0x%08X, write_into_reg=%d\n",
		rtlpci->receive_config, write_into_reg);
}

/* WKFMCAMAddAllEntry8812 */
void rtl8821ae_add_wowlan_pattern(struct ieee80211_hw *hw,
				  struct rtl_wow_pattern *rtl_pattern,
				  u8 index)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 cam = 0;
	u8 addr = 0;
	u16 rxbuf_addr;
	u8 tmp, count = 0;
	u16 cam_start;
	u16 offset;

	/* Count the WFCAM entry start offset. */

	/* RX page size = 128 byte */
	offset = MAX_RX_DMA_BUFFER_SIZE_8812 / 128;
	/* We should start from the boundry */
	cam_start = offset * 128;

	/* Enable Rx packet buffer access. */
	rtl_write_byte(rtlpriv, REG_PKT_BUFF_ACCESS_CTRL, RXPKT_BUF_SELECT);
	for (addr = 0; addr < WKFMCAM_ADDR_NUM; addr++) {
		/* Set Rx packet buffer offset.
		 * RxBufer pointer increases 1,
		 * we can access 8 bytes in Rx packet buffer.
		 * CAM start offset (unit: 1 byte) =  index*WKFMCAM_SIZE
		 * RxBufer addr = (CAM start offset +
		 *                 per entry offset of a WKFM CAM)/8
		 *	* index: The index of the wake up frame mask
		 *	* WKFMCAM_SIZE: the total size of one WKFM CAM
		 *	* per entry offset of a WKFM CAM: Addr*4 bytes
		 */
		rxbuf_addr = (cam_start + index * WKFMCAM_SIZE + addr * 4) >> 3;
		/* Set R/W start offset */
		rtl_write_word(rtlpriv, REG_PKTBUF_DBG_CTRL, rxbuf_addr);

		if (addr == 0) {
			cam = BIT(31) | rtl_pattern->crc;

			if (rtl_pattern->type == UNICAST_PATTERN)
				cam |= BIT(24);
			else if (rtl_pattern->type == MULTICAST_PATTERN)
				cam |= BIT(25);
			else if (rtl_pattern->type == BROADCAST_PATTERN)
				cam |= BIT(26);

			rtl_write_dword(rtlpriv, REG_PKTBUF_DBG_DATA_L, cam);
			RT_TRACE(rtlpriv, COMP_POWER, DBG_TRACE,
				 "WRITE entry[%d] 0x%x: %x\n", addr,
				  REG_PKTBUF_DBG_DATA_L, cam);

			/* Write to Rx packet buffer. */
			rtl_write_word(rtlpriv, REG_RXPKTBUF_CTRL, 0x0f01);
		} else if (addr == 2 || addr == 4) {/* WKFM[127:0] */
			cam = rtl_pattern->mask[addr - 2];

			rtl_write_dword(rtlpriv, REG_PKTBUF_DBG_DATA_L, cam);
			RT_TRACE(rtlpriv, COMP_POWER, DBG_TRACE,
				 "WRITE entry[%d] 0x%x: %x\n", addr,
				  REG_PKTBUF_DBG_DATA_L, cam);

			rtl_write_word(rtlpriv, REG_RXPKTBUF_CTRL, 0x0f01);
		} else if (addr == 3 || addr == 5) {/* WKFM[127:0] */
			cam = rtl_pattern->mask[addr - 2];

			rtl_write_dword(rtlpriv, REG_PKTBUF_DBG_DATA_H, cam);
			RT_TRACE(rtlpriv, COMP_POWER, DBG_TRACE,
				 "WRITE entry[%d] 0x%x: %x\n", addr,
				  REG_PKTBUF_DBG_DATA_H, cam);

			rtl_write_word(rtlpriv, REG_RXPKTBUF_CTRL, 0xf001);
		}

		count = 0;
		do {
			tmp = rtl_read_byte(rtlpriv, REG_RXPKTBUF_CTRL);
			udelay(2);
			count++;
		} while (tmp && count < 100);

		WARN_ONCE((count >= 100),
			  "rtl8821ae: Write wake up frame mask FAIL %d value!\n",
			  tmp);
	}
	/* Disable Rx packet buffer access. */
	rtl_write_byte(rtlpriv, REG_PKT_BUFF_ACCESS_CTRL,
		       DISABLE_TRXPKT_BUF_ACCESS);
}
