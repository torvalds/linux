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
#include "dm.h"
#include "fw.h"
#include "led.h"
#include "hw.h"
#include "../pwrseqcmd.h"
#include "pwrseq.h"

#define LLT_CONFIG	5

static void _rtl92ee_set_bcn_ctrl_reg(struct ieee80211_hw *hw,
				      u8 set_bits, u8 clear_bits)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpci->reg_bcn_ctrl_val |= set_bits;
	rtlpci->reg_bcn_ctrl_val &= ~clear_bits;

	rtl_write_byte(rtlpriv, REG_BCN_CTRL, (u8)rtlpci->reg_bcn_ctrl_val);
}

static void _rtl92ee_stop_tx_beacon(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 tmp;

	tmp = rtl_read_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2);
	rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2, tmp & (~BIT(6)));
	rtl_write_byte(rtlpriv, REG_TBTT_PROHIBIT + 1, 0x64);
	tmp = rtl_read_byte(rtlpriv, REG_TBTT_PROHIBIT + 2);
	tmp &= ~(BIT(0));
	rtl_write_byte(rtlpriv, REG_TBTT_PROHIBIT + 2, tmp);
}

static void _rtl92ee_resume_tx_beacon(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 tmp;

	tmp = rtl_read_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2);
	rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2, tmp | BIT(6));
	rtl_write_byte(rtlpriv, REG_TBTT_PROHIBIT + 1, 0xff);
	tmp = rtl_read_byte(rtlpriv, REG_TBTT_PROHIBIT + 2);
	tmp |= BIT(0);
	rtl_write_byte(rtlpriv, REG_TBTT_PROHIBIT + 2, tmp);
}

static void _rtl92ee_enable_bcn_sub_func(struct ieee80211_hw *hw)
{
	_rtl92ee_set_bcn_ctrl_reg(hw, 0, BIT(1));
}

static void _rtl92ee_disable_bcn_sub_func(struct ieee80211_hw *hw)
{
	_rtl92ee_set_bcn_ctrl_reg(hw, BIT(1), 0);
}

static void _rtl92ee_set_fw_clock_on(struct ieee80211_hw *hw,
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

	if (IS_IN_LOW_POWER_STATE_92E(rtlhal->fw_ps_state)) {
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
				rtlhal->fw_ps_state = FW_PS_STATE_RF_ON_92E;
				RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
					 "Receive CPWM INT!!! PSState = %X\n",
					 rtlhal->fw_ps_state);
			}
		}

		spin_lock_bh(&rtlpriv->locks.fw_ps_lock);
		rtlhal->fw_clk_change_in_progress = false;
		spin_unlock_bh(&rtlpriv->locks.fw_ps_lock);
		if (b_schedule_timer) {
			mod_timer(&rtlpriv->works.fw_clockoff_timer,
				  jiffies + MSECS(10));
		}
	} else  {
		spin_lock_bh(&rtlpriv->locks.fw_ps_lock);
		rtlhal->fw_clk_change_in_progress = false;
		spin_unlock_bh(&rtlpriv->locks.fw_ps_lock);
	}
}

static void _rtl92ee_set_fw_clock_off(struct ieee80211_hw *hw, u8 rpwm_val)
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

static void _rtl92ee_set_fw_ps_rf_on(struct ieee80211_hw *hw)
{
	u8 rpwm_val = 0;

	rpwm_val |= (FW_PS_STATE_RF_OFF_92E | FW_PS_ACK);
	_rtl92ee_set_fw_clock_on(hw, rpwm_val, true);
}

static void _rtl92ee_set_fw_ps_rf_off_low_power(struct ieee80211_hw *hw)
{
	u8 rpwm_val = 0;

	rpwm_val |= FW_PS_STATE_RF_OFF_LOW_PWR;
	_rtl92ee_set_fw_clock_off(hw, rpwm_val);
}

void rtl92ee_fw_clk_off_timer_callback(unsigned long data)
{
	struct ieee80211_hw *hw = (struct ieee80211_hw *)data;

	_rtl92ee_set_fw_ps_rf_off_low_power(hw);
}

static void _rtl92ee_fwlps_leave(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	bool fw_current_inps = false;
	u8 rpwm_val = 0, fw_pwrmode = FW_PS_ACTIVE_MODE;

	if (ppsc->low_power_enable) {
		rpwm_val = (FW_PS_STATE_ALL_ON_92E | FW_PS_ACK);/* RF on */
		_rtl92ee_set_fw_clock_on(hw, rpwm_val, false);
		rtlhal->allow_sw_to_change_hwclc = false;
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_H2C_FW_PWRMODE,
					      (u8 *)(&fw_pwrmode));
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_FW_PSMODE_STATUS,
					      (u8 *)(&fw_current_inps));
	} else {
		rpwm_val = FW_PS_STATE_ALL_ON_92E;	/* RF on */
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_SET_RPWM,
					      (u8 *)(&rpwm_val));
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_H2C_FW_PWRMODE,
					      (u8 *)(&fw_pwrmode));
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_FW_PSMODE_STATUS,
					      (u8 *)(&fw_current_inps));
	}
}

static void _rtl92ee_fwlps_enter(struct ieee80211_hw *hw)
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
		_rtl92ee_set_fw_clock_off(hw, rpwm_val);
	} else {
		rpwm_val = FW_PS_STATE_RF_OFF_92E;	/* RF off */
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_FW_PSMODE_STATUS,
					      (u8 *)(&fw_current_inps));
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_H2C_FW_PWRMODE,
					      (u8 *)(&ppsc->fwctrl_psmode));
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_SET_RPWM,
					      (u8 *)(&rpwm_val));
	}
}

void rtl92ee_get_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val)
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
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_DMESG,
			 "switch case not process %x\n", variable);
		break;
	}
}

static void _rtl92ee_download_rsvd_page(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 tmp_regcr, tmp_reg422;
	u8 bcnvalid_reg, txbc_reg;
	u8 count = 0, dlbcn_count = 0;
	bool b_recover = false;

	/*Set REG_CR bit 8. DMA beacon by SW.*/
	tmp_regcr = rtl_read_byte(rtlpriv, REG_CR + 1);
	rtl_write_byte(rtlpriv, REG_CR + 1, tmp_regcr | BIT(0));

	/* Disable Hw protection for a time which revserd for Hw sending beacon.
	 * Fix download reserved page packet fail
	 * that access collision with the protection time.
	 * 2010.05.11. Added by tynli.
	 */
	_rtl92ee_set_bcn_ctrl_reg(hw, 0, BIT(3));
	_rtl92ee_set_bcn_ctrl_reg(hw, BIT(4), 0);

	/* Set FWHW_TXQ_CTRL 0x422[6]=0 to
	 * tell Hw the packet is not a real beacon frame.
	 */
	tmp_reg422 = rtl_read_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2);
	rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2, tmp_reg422 & (~BIT(6)));

	if (tmp_reg422 & BIT(6))
		b_recover = true;

	do {
		/* Clear beacon valid check bit */
		bcnvalid_reg = rtl_read_byte(rtlpriv, REG_DWBCN0_CTRL + 2);
		rtl_write_byte(rtlpriv, REG_DWBCN0_CTRL + 2,
			       bcnvalid_reg | BIT(0));

		/* download rsvd page */
		rtl92ee_set_fw_rsvdpagepkt(hw, false);

		txbc_reg = rtl_read_byte(rtlpriv, REG_MGQ_TXBD_NUM + 3);
		count = 0;
		while ((txbc_reg & BIT(4)) && count < 20) {
			count++;
			udelay(10);
			txbc_reg = rtl_read_byte(rtlpriv, REG_MGQ_TXBD_NUM + 3);
		}
		rtl_write_byte(rtlpriv, REG_MGQ_TXBD_NUM + 3,
			       txbc_reg | BIT(4));

		/* check rsvd page download OK. */
		bcnvalid_reg = rtl_read_byte(rtlpriv, REG_DWBCN0_CTRL + 2);
		count = 0;
		while (!(bcnvalid_reg & BIT(0)) && count < 20) {
			count++;
			udelay(50);
			bcnvalid_reg = rtl_read_byte(rtlpriv,
						     REG_DWBCN0_CTRL + 2);
		}

		if (bcnvalid_reg & BIT(0))
			rtl_write_byte(rtlpriv, REG_DWBCN0_CTRL + 2, BIT(0));

		dlbcn_count++;
	} while (!(bcnvalid_reg & BIT(0)) && dlbcn_count < 5);

	if (!(bcnvalid_reg & BIT(0)))
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Download RSVD page failed!\n");

	/* Enable Bcn */
	_rtl92ee_set_bcn_ctrl_reg(hw, BIT(3), 0);
	_rtl92ee_set_bcn_ctrl_reg(hw, 0, BIT(4));

	if (b_recover)
		rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2, tmp_reg422);

	tmp_regcr = rtl_read_byte(rtlpriv, REG_CR + 1);
	rtl_write_byte(rtlpriv, REG_CR + 1, tmp_regcr & (~BIT(0)));
}

void rtl92ee_set_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_efuse *efuse = rtl_efuse(rtl_priv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	u8 idx;

	switch (variable) {
	case HW_VAR_ETHER_ADDR:
		for (idx = 0; idx < ETH_ALEN; idx++)
			rtl_write_byte(rtlpriv, (REG_MACID + idx), val[idx]);
		break;
	case HW_VAR_BASIC_RATE:{
		u16 b_rate_cfg = ((u16 *)val)[0];

		b_rate_cfg = b_rate_cfg & 0x15f;
		b_rate_cfg |= 0x01;
		b_rate_cfg = (b_rate_cfg | 0xd) & (~BIT(1));
		rtl_write_byte(rtlpriv, REG_RRSR, b_rate_cfg & 0xff);
		rtl_write_byte(rtlpriv, REG_RRSR + 1, (b_rate_cfg >> 8) & 0xff);
		break; }
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

		RT_TRACE(rtlpriv, COMP_MLME, DBG_TRACE,
			 "HW_VAR_SLOT_TIME %x\n", val[0]);

		rtl_write_byte(rtlpriv, REG_SLOT, val[0]);

		for (e_aci = 0; e_aci < AC_MAX; e_aci++) {
			rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_AC_PARAM,
						      (u8 *)(&e_aci));
		}
		break; }
	case HW_VAR_ACK_PREAMBLE:{
		u8 reg_tmp;
		u8 short_preamble = (bool)(*(u8 *)val);

		reg_tmp = (rtlpriv->mac80211.cur_40_prime_sc) << 5;
		if (short_preamble)
			reg_tmp |= 0x80;
		rtl_write_byte(rtlpriv, REG_RRSR + 2, reg_tmp);
		rtlpriv->mac80211.short_preamble = short_preamble;
		}
		break;
	case HW_VAR_WPA_CONFIG:
		rtl_write_byte(rtlpriv, REG_SECCFG, *((u8 *)val));
		break;
	case HW_VAR_AMPDU_FACTOR:{
		u8 regtoset_normal[4] = { 0x41, 0xa8, 0x72, 0xb9 };
		u8 fac;
		u8 *reg = NULL;
		u8 i = 0;

		reg = regtoset_normal;

		fac = *((u8 *)val);
		if (fac <= 3) {
			fac = (1 << (fac + 2));
			if (fac > 0xf)
				fac = 0xf;
			for (i = 0; i < 4; i++) {
				if ((reg[i] & 0xf0) > (fac << 4))
					reg[i] = (reg[i] & 0x0f) |
						(fac << 4);
				if ((reg[i] & 0x0f) > fac)
					reg[i] = (reg[i] & 0xf0) | fac;
				rtl_write_byte(rtlpriv,
					       (REG_AGGLEN_LMT + i),
					       reg[i]);
			}
			RT_TRACE(rtlpriv, COMP_MLME, DBG_LOUD,
				 "Set HW_VAR_AMPDU_FACTOR:%#x\n", fac);
		}
		}
		break;
	case HW_VAR_AC_PARAM:{
		u8 e_aci = *((u8 *)val);

		if (rtlpci->acm_method != EACMWAY2_SW)
			rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_ACM_CTRL,
						      (u8 *)(&e_aci));
		}
		break;
	case HW_VAR_ACM_CTRL:{
		u8 e_aci = *((u8 *)val);
		union aci_aifsn *aifs = (union aci_aifsn *)(&mac->ac[0].aifs);

		u8 acm = aifs->f.acm;
		u8 acm_ctrl = rtl_read_byte(rtlpriv, REG_ACMHWCTRL);

		acm_ctrl = acm_ctrl | ((rtlpci->acm_method == 2) ? 0x0 : 0x1);

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
				RT_TRACE(rtlpriv, COMP_ERR, DBG_DMESG,
					 "switch case not process\n");
				break;
			}
		}

		RT_TRACE(rtlpriv, COMP_QOS, DBG_TRACE,
			 "SetHwReg8190pci(): [HW_VAR_ACM_CTRL] Write 0x%X\n",
			  acm_ctrl);
		rtl_write_byte(rtlpriv, REG_ACMHWCTRL, acm_ctrl);
		}
		break;
	case HW_VAR_RCR:{
		rtl_write_dword(rtlpriv, REG_RCR, ((u32 *)(val))[0]);
		rtlpci->receive_config = ((u32 *)(val))[0];
		}
		break;
	case HW_VAR_RETRY_LIMIT:{
		u8 retry_limit = ((u8 *)(val))[0];

		rtl_write_word(rtlpriv, REG_RETRY_LIMIT,
			       retry_limit << RETRY_LIMIT_SHORT_SHIFT |
			       retry_limit << RETRY_LIMIT_LONG_SHIFT);
		}
		break;
	case HW_VAR_DUAL_TSF_RST:
		rtl_write_byte(rtlpriv, REG_DUAL_TSF_RST, (BIT(0) | BIT(1)));
		break;
	case HW_VAR_EFUSE_BYTES:
		efuse->efuse_usedbytes = *((u16 *)val);
		break;
	case HW_VAR_EFUSE_USAGE:
		efuse->efuse_usedpercentage = *((u8 *)val);
		break;
	case HW_VAR_IO_CMD:
		rtl92ee_phy_set_io_cmd(hw, (*(enum io_type *)val));
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
		rtl92ee_set_fw_pwrmode_cmd(hw, (*(u8 *)val));
		break;
	case HW_VAR_FW_PSMODE_STATUS:
		ppsc->fw_current_inpsmode = *((bool *)val);
		break;
	case HW_VAR_RESUME_CLK_ON:
		_rtl92ee_set_fw_ps_rf_on(hw);
		break;
	case HW_VAR_FW_LPS_ACTION:{
		bool b_enter_fwlps = *((bool *)val);

		if (b_enter_fwlps)
			_rtl92ee_fwlps_enter(hw);
		else
			_rtl92ee_fwlps_leave(hw);
		}
		break;
	case HW_VAR_H2C_FW_JOINBSSRPT:{
		u8 mstatus = (*(u8 *)val);

		if (mstatus == RT_MEDIA_CONNECT) {
			rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_AID, NULL);
			_rtl92ee_download_rsvd_page(hw);
		}
		rtl92ee_set_fw_media_status_rpt_cmd(hw, mstatus);
		}
		break;
	case HW_VAR_H2C_FW_P2P_PS_OFFLOAD:
		rtl92ee_set_p2p_ps_offload_cmd(hw, (*(u8 *)val));
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
			_rtl92ee_stop_tx_beacon(hw);

		_rtl92ee_set_bcn_ctrl_reg(hw, 0, BIT(3));

		rtl_write_dword(rtlpriv, REG_TSFTR,
				(u32)(mac->tsf & 0xffffffff));
		rtl_write_dword(rtlpriv, REG_TSFTR + 4,
				(u32)((mac->tsf >> 32) & 0xffffffff));

		_rtl92ee_set_bcn_ctrl_reg(hw, BIT(3), 0);

		if (btype_ibss)
			_rtl92ee_resume_tx_beacon(hw);
		}
		break;
	case HW_VAR_KEEP_ALIVE: {
		u8 array[2];

		array[0] = 0xff;
		array[1] = *((u8 *)val);
		rtl92ee_fill_h2c_cmd(hw, H2C_92E_KEEP_ALIVE_CTRL, 2, array);
		}
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_DMESG,
			 "switch case not process %x\n", variable);
		break;
	}
}

static bool _rtl92ee_llt_table_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 txpktbuf_bndy;
	u8 u8tmp, testcnt = 0;

	txpktbuf_bndy = 0xFA;

	rtl_write_dword(rtlpriv, REG_RQPN, 0x80E90808);

	rtl_write_byte(rtlpriv, REG_TRXFF_BNDY, txpktbuf_bndy);
	rtl_write_word(rtlpriv, REG_TRXFF_BNDY + 2, 0x3d00 - 1);

	rtl_write_byte(rtlpriv, REG_DWBCN0_CTRL + 1, txpktbuf_bndy);
	rtl_write_byte(rtlpriv, REG_DWBCN1_CTRL + 1, txpktbuf_bndy);

	rtl_write_byte(rtlpriv, REG_BCNQ_BDNY, txpktbuf_bndy);
	rtl_write_byte(rtlpriv, REG_BCNQ1_BDNY, txpktbuf_bndy);

	rtl_write_byte(rtlpriv, REG_MGQ_BDNY, txpktbuf_bndy);
	rtl_write_byte(rtlpriv, 0x45D, txpktbuf_bndy);

	rtl_write_byte(rtlpriv, REG_PBP, 0x31);
	rtl_write_byte(rtlpriv, REG_RX_DRVINFO_SZ, 0x4);

	u8tmp = rtl_read_byte(rtlpriv, REG_AUTO_LLT + 2);
	rtl_write_byte(rtlpriv, REG_AUTO_LLT + 2, u8tmp | BIT(0));

	while (u8tmp & BIT(0)) {
		u8tmp = rtl_read_byte(rtlpriv, REG_AUTO_LLT + 2);
		udelay(10);
		testcnt++;
		if (testcnt > 10)
			break;
	}

	return true;
}

static void _rtl92ee_gen_refresh_led_state(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_led *pled0 = &pcipriv->ledctl.sw_led0;

	if (rtlpriv->rtlhal.up_first_time)
		return;

	if (ppsc->rfoff_reason == RF_CHANGE_BY_IPS)
		rtl92ee_sw_led_on(hw, pled0);
	else if (ppsc->rfoff_reason == RF_CHANGE_BY_INIT)
		rtl92ee_sw_led_on(hw, pled0);
	else
		rtl92ee_sw_led_off(hw, pled0);
}

static bool _rtl92ee_init_mac(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	u8 bytetmp;
	u16 wordtmp;
	u32 dwordtmp;

	rtl_write_byte(rtlpriv, REG_RSV_CTRL, 0x0);

	dwordtmp = rtl_read_dword(rtlpriv, REG_SYS_CFG1);
	if (dwordtmp & BIT(24)) {
		rtl_write_byte(rtlpriv, 0x7c, 0xc3);
	} else {
		bytetmp = rtl_read_byte(rtlpriv, 0x16);
		rtl_write_byte(rtlpriv, 0x16, bytetmp | BIT(4) | BIT(6));
		rtl_write_byte(rtlpriv, 0x7c, 0x83);
	}
	/* 1. 40Mhz crystal source*/
	bytetmp = rtl_read_byte(rtlpriv, REG_AFE_CTRL2);
	bytetmp &= 0xfb;
	rtl_write_byte(rtlpriv, REG_AFE_CTRL2, bytetmp);

	dwordtmp = rtl_read_dword(rtlpriv, REG_AFE_CTRL4);
	dwordtmp &= 0xfffffc7f;
	rtl_write_dword(rtlpriv, REG_AFE_CTRL4, dwordtmp);

	/* 2. 92E AFE parameter
	 * MP chip then check version
	 */
	bytetmp = rtl_read_byte(rtlpriv, REG_AFE_CTRL2);
	bytetmp &= 0xbf;
	rtl_write_byte(rtlpriv, REG_AFE_CTRL2, bytetmp);

	dwordtmp = rtl_read_dword(rtlpriv, REG_AFE_CTRL4);
	dwordtmp &= 0xffdfffff;
	rtl_write_dword(rtlpriv, REG_AFE_CTRL4, dwordtmp);

	/* HW Power on sequence */
	if (!rtl_hal_pwrseqcmdparsing(rtlpriv, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK,
				      PWR_INTF_PCI_MSK,
				      RTL8192E_NIC_ENABLE_FLOW)) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "init MAC Fail as rtl_hal_pwrseqcmdparsing\n");
		return false;
	}

	/* Release MAC IO register reset */
	bytetmp = rtl_read_byte(rtlpriv, REG_CR);
	bytetmp = 0xff;
	rtl_write_byte(rtlpriv, REG_CR, bytetmp);
	mdelay(2);
	bytetmp = 0x7f;
	rtl_write_byte(rtlpriv, REG_HWSEQ_CTRL, bytetmp);
	mdelay(2);

	/* Add for wakeup online */
	bytetmp = rtl_read_byte(rtlpriv, REG_SYS_CLKR);
	rtl_write_byte(rtlpriv, REG_SYS_CLKR, bytetmp | BIT(3));
	bytetmp = rtl_read_byte(rtlpriv, REG_GPIO_MUXCFG + 1);
	rtl_write_byte(rtlpriv, REG_GPIO_MUXCFG + 1, bytetmp & (~BIT(4)));
	/* Release MAC IO register reset */
	rtl_write_word(rtlpriv, REG_CR, 0x2ff);

	if (!rtlhal->mac_func_enable) {
		if (_rtl92ee_llt_table_init(hw) == false) {
			RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
				 "LLT table init fail\n");
			return false;
		}
	}

	rtl_write_dword(rtlpriv, REG_HISR, 0xffffffff);
	rtl_write_dword(rtlpriv, REG_HISRE, 0xffffffff);

	wordtmp = rtl_read_word(rtlpriv, REG_TRXDMA_CTRL);
	wordtmp &= 0xf;
	wordtmp |= 0xF5B1;
	rtl_write_word(rtlpriv, REG_TRXDMA_CTRL, wordtmp);
	/* Reported Tx status from HW for rate adaptive.*/
	rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 1, 0x1F);

	/* Set RCR register */
	rtl_write_dword(rtlpriv, REG_RCR, rtlpci->receive_config);
	rtl_write_word(rtlpriv, REG_RXFLTMAP2, 0xffff);

	/* Set TCR register */
	rtl_write_dword(rtlpriv, REG_TCR, rtlpci->transmit_config);

	/* Set TX/RX descriptor physical address(from OS API). */
	rtl_write_dword(rtlpriv, REG_BCNQ_DESA,
			((u64)rtlpci->tx_ring[BEACON_QUEUE].buffer_desc_dma) &
			DMA_BIT_MASK(32));
	rtl_write_dword(rtlpriv, REG_MGQ_DESA,
			(u64)rtlpci->tx_ring[MGNT_QUEUE].buffer_desc_dma &
			DMA_BIT_MASK(32));
	rtl_write_dword(rtlpriv, REG_VOQ_DESA,
			(u64)rtlpci->tx_ring[VO_QUEUE].buffer_desc_dma &
			DMA_BIT_MASK(32));
	rtl_write_dword(rtlpriv, REG_VIQ_DESA,
			(u64)rtlpci->tx_ring[VI_QUEUE].buffer_desc_dma &
			DMA_BIT_MASK(32));

	rtl_write_dword(rtlpriv, REG_BEQ_DESA,
			(u64)rtlpci->tx_ring[BE_QUEUE].buffer_desc_dma &
			DMA_BIT_MASK(32));

	dwordtmp = rtl_read_dword(rtlpriv, REG_BEQ_DESA);

	rtl_write_dword(rtlpriv, REG_BKQ_DESA,
			(u64)rtlpci->tx_ring[BK_QUEUE].buffer_desc_dma &
			DMA_BIT_MASK(32));
	rtl_write_dword(rtlpriv, REG_HQ0_DESA,
			(u64)rtlpci->tx_ring[HIGH_QUEUE].buffer_desc_dma &
			DMA_BIT_MASK(32));

	rtl_write_dword(rtlpriv, REG_RX_DESA,
			(u64)rtlpci->rx_ring[RX_MPDU_QUEUE].dma &
			DMA_BIT_MASK(32));

	/* if we want to support 64 bit DMA, we should set it here,
	 * but now we do not support 64 bit DMA
	 */

	rtl_write_dword(rtlpriv, REG_TSFTIMER_HCI, 0x3fffffff);

	bytetmp = rtl_read_byte(rtlpriv, REG_PCIE_CTRL_REG + 3);
	rtl_write_byte(rtlpriv, REG_PCIE_CTRL_REG + 3, bytetmp | 0xF7);

	rtl_write_dword(rtlpriv, REG_INT_MIG, 0);

	rtl_write_dword(rtlpriv, REG_MCUTST_1, 0x0);

	rtl_write_word(rtlpriv, REG_MGQ_TXBD_NUM,
		       TX_DESC_NUM_92E | ((RTL8192EE_SEG_NUM << 12) & 0x3000));
	rtl_write_word(rtlpriv, REG_VOQ_TXBD_NUM,
		       TX_DESC_NUM_92E | ((RTL8192EE_SEG_NUM << 12) & 0x3000));
	rtl_write_word(rtlpriv, REG_VIQ_TXBD_NUM,
		       TX_DESC_NUM_92E | ((RTL8192EE_SEG_NUM << 12) & 0x3000));
	rtl_write_word(rtlpriv, REG_BEQ_TXBD_NUM,
		       TX_DESC_NUM_92E | ((RTL8192EE_SEG_NUM << 12) & 0x3000));
	rtl_write_word(rtlpriv, REG_VOQ_TXBD_NUM,
		       TX_DESC_NUM_92E | ((RTL8192EE_SEG_NUM << 12) & 0x3000));
	rtl_write_word(rtlpriv, REG_BKQ_TXBD_NUM,
		       TX_DESC_NUM_92E | ((RTL8192EE_SEG_NUM << 12) & 0x3000));
	rtl_write_word(rtlpriv, REG_HI0Q_TXBD_NUM,
		       TX_DESC_NUM_92E | ((RTL8192EE_SEG_NUM << 12) & 0x3000));
	rtl_write_word(rtlpriv, REG_HI1Q_TXBD_NUM,
		       TX_DESC_NUM_92E | ((RTL8192EE_SEG_NUM << 12) & 0x3000));
	rtl_write_word(rtlpriv, REG_HI2Q_TXBD_NUM,
		       TX_DESC_NUM_92E | ((RTL8192EE_SEG_NUM << 12) & 0x3000));
	rtl_write_word(rtlpriv, REG_HI3Q_TXBD_NUM,
		       TX_DESC_NUM_92E | ((RTL8192EE_SEG_NUM << 12) & 0x3000));
	rtl_write_word(rtlpriv, REG_HI4Q_TXBD_NUM,
		       TX_DESC_NUM_92E | ((RTL8192EE_SEG_NUM << 12) & 0x3000));
	rtl_write_word(rtlpriv, REG_HI5Q_TXBD_NUM,
		       TX_DESC_NUM_92E | ((RTL8192EE_SEG_NUM << 12) & 0x3000));
	rtl_write_word(rtlpriv, REG_HI6Q_TXBD_NUM,
		       TX_DESC_NUM_92E | ((RTL8192EE_SEG_NUM << 12) & 0x3000));
	rtl_write_word(rtlpriv, REG_HI7Q_TXBD_NUM,
		       TX_DESC_NUM_92E | ((RTL8192EE_SEG_NUM << 12) & 0x3000));
	/*Rx*/
#if (DMA_IS_64BIT == 1)
	rtl_write_word(rtlpriv, REG_RX_RXBD_NUM,
		       RX_DESC_NUM_92E |
		       ((RTL8192EE_SEG_NUM << 13) & 0x6000) | 0x8000);
#else
	rtl_write_word(rtlpriv, REG_RX_RXBD_NUM,
		       RX_DESC_NUM_92E |
		       ((RTL8192EE_SEG_NUM << 13) & 0x6000) | 0x0000);
#endif

	rtl_write_dword(rtlpriv, REG_TSFTIMER_HCI, 0XFFFFFFFF);

	_rtl92ee_gen_refresh_led_state(hw);
	return true;
}

static void _rtl92ee_hw_configure(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u32 reg_rrsr;

	reg_rrsr = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
	/* Init value for RRSR. */
	rtl_write_dword(rtlpriv, REG_RRSR, reg_rrsr);

	/* ARFB table 9 for 11ac 5G 2SS */
	rtl_write_dword(rtlpriv, REG_ARFR0, 0x00000010);
	rtl_write_dword(rtlpriv, REG_ARFR0 + 4, 0x3e0ff000);

	/* ARFB table 10 for 11ac 5G 1SS */
	rtl_write_dword(rtlpriv, REG_ARFR1, 0x00000010);
	rtl_write_dword(rtlpriv, REG_ARFR1 + 4, 0x000ff000);

	/* Set SLOT time */
	rtl_write_byte(rtlpriv, REG_SLOT, 0x09);

	/* CF-End setting. */
	rtl_write_word(rtlpriv, REG_FWHW_TXQ_CTRL, 0x1F80);

	/* Set retry limit */
	rtl_write_word(rtlpriv, REG_RETRY_LIMIT, 0x0707);

	/* BAR settings */
	rtl_write_dword(rtlpriv, REG_BAR_MODE_CTRL, 0x0201ffff);

	/* Set Data / Response auto rate fallack retry count */
	rtl_write_dword(rtlpriv, REG_DARFRC, 0x01000000);
	rtl_write_dword(rtlpriv, REG_DARFRC + 4, 0x07060504);
	rtl_write_dword(rtlpriv, REG_RARFRC, 0x01000000);
	rtl_write_dword(rtlpriv, REG_RARFRC + 4, 0x07060504);

	/* Beacon related, for rate adaptive */
	rtl_write_byte(rtlpriv, REG_ATIMWND, 0x2);
	rtl_write_byte(rtlpriv, REG_BCN_MAX_ERR, 0xff);

	rtlpci->reg_bcn_ctrl_val = 0x1d;
	rtl_write_byte(rtlpriv, REG_BCN_CTRL, rtlpci->reg_bcn_ctrl_val);

	/* Marked out by Bruce, 2010-09-09.
	 * This register is configured for the 2nd Beacon (multiple BSSID).
	 * We shall disable this register if we only support 1 BSSID.
	 * vivi guess 92d also need this, also 92d now doesnot set this reg
	 */
	rtl_write_byte(rtlpriv, REG_BCN_CTRL_1, 0);

	/* TBTT prohibit hold time. Suggested by designer TimChen. */
	rtl_write_byte(rtlpriv, REG_TBTT_PROHIBIT + 1, 0xff); /* 8 ms */

	rtl_write_byte(rtlpriv, REG_PIFS, 0);
	rtl_write_byte(rtlpriv, REG_AGGR_BREAK_TIME, 0x16);

	rtl_write_word(rtlpriv, REG_NAV_PROT_LEN, 0x0040);
	rtl_write_word(rtlpriv, REG_PROT_MODE_CTRL, 0x08ff);

	/* For Rx TP. Suggested by SD1 Richard. Added by tynli. 2010.04.12.*/
	rtl_write_dword(rtlpriv, REG_FAST_EDCA_CTRL, 0x03086666);

	/* ACKTO for IOT issue. */
	rtl_write_byte(rtlpriv, REG_ACKTO, 0x40);

	/* Set Spec SIFS (used in NAV) */
	rtl_write_word(rtlpriv, REG_SPEC_SIFS, 0x100a);
	rtl_write_word(rtlpriv, REG_MAC_SPEC_SIFS, 0x100a);

	/* Set SIFS for CCK */
	rtl_write_word(rtlpriv, REG_SIFS_CTX, 0x100a);

	/* Set SIFS for OFDM */
	rtl_write_word(rtlpriv, REG_SIFS_TRX, 0x100a);

	/* Note Data sheet don't define */
	rtl_write_word(rtlpriv, 0x4C7, 0x80);

	rtl_write_byte(rtlpriv, REG_RX_PKT_LIMIT, 0x20);

	rtl_write_word(rtlpriv, REG_MAX_AGGR_NUM, 0x1717);

	/* Set Multicast Address. 2009.01.07. by tynli. */
	rtl_write_dword(rtlpriv, REG_MAR, 0xffffffff);
	rtl_write_dword(rtlpriv, REG_MAR + 4, 0xffffffff);
}

static void _rtl92ee_enable_aspm_back_door(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	u32 tmp32 = 0, count = 0;
	u8 tmp8 = 0;

	rtl_write_word(rtlpriv, REG_BACKDOOR_DBI_DATA, 0x78);
	rtl_write_byte(rtlpriv, REG_BACKDOOR_DBI_DATA + 2, 0x2);
	tmp8 = rtl_read_byte(rtlpriv, REG_BACKDOOR_DBI_DATA + 2);
	count = 0;
	while (tmp8 && count < 20) {
		udelay(10);
		tmp8 = rtl_read_byte(rtlpriv, REG_BACKDOOR_DBI_DATA + 2);
		count++;
	}

	if (0 == tmp8) {
		tmp32 = rtl_read_dword(rtlpriv, REG_BACKDOOR_DBI_RDATA);
		if ((tmp32 & 0xff00) != 0x2000) {
			tmp32 &= 0xffff00ff;
			rtl_write_dword(rtlpriv, REG_BACKDOOR_DBI_WDATA,
					tmp32 | BIT(13));
			rtl_write_word(rtlpriv, REG_BACKDOOR_DBI_DATA, 0xf078);
			rtl_write_byte(rtlpriv, REG_BACKDOOR_DBI_DATA + 2, 0x1);

			tmp8 = rtl_read_byte(rtlpriv,
					     REG_BACKDOOR_DBI_DATA + 2);
			count = 0;
			while (tmp8 && count < 20) {
				udelay(10);
				tmp8 = rtl_read_byte(rtlpriv,
						     REG_BACKDOOR_DBI_DATA + 2);
				count++;
			}
		}
	}

	rtl_write_word(rtlpriv, REG_BACKDOOR_DBI_DATA, 0x70c);
	rtl_write_byte(rtlpriv, REG_BACKDOOR_DBI_DATA + 2, 0x2);
	tmp8 = rtl_read_byte(rtlpriv, REG_BACKDOOR_DBI_DATA + 2);
	count = 0;
	while (tmp8 && count < 20) {
		udelay(10);
		tmp8 = rtl_read_byte(rtlpriv, REG_BACKDOOR_DBI_DATA + 2);
		count++;
	}
	if (0 == tmp8) {
		tmp32 = rtl_read_dword(rtlpriv, REG_BACKDOOR_DBI_RDATA);
		rtl_write_dword(rtlpriv, REG_BACKDOOR_DBI_WDATA,
				tmp32 | BIT(31));
		rtl_write_word(rtlpriv, REG_BACKDOOR_DBI_DATA, 0xf70c);
		rtl_write_byte(rtlpriv, REG_BACKDOOR_DBI_DATA + 2, 0x1);
	}

	tmp8 = rtl_read_byte(rtlpriv, REG_BACKDOOR_DBI_DATA + 2);
	count = 0;
	while (tmp8 && count < 20) {
		udelay(10);
		tmp8 = rtl_read_byte(rtlpriv, REG_BACKDOOR_DBI_DATA + 2);
		count++;
	}

	rtl_write_word(rtlpriv, REG_BACKDOOR_DBI_DATA, 0x718);
	rtl_write_byte(rtlpriv, REG_BACKDOOR_DBI_DATA + 2, 0x2);
	tmp8 = rtl_read_byte(rtlpriv, REG_BACKDOOR_DBI_DATA + 2);
	count = 0;
	while (tmp8 && count < 20) {
		udelay(10);
		tmp8 = rtl_read_byte(rtlpriv, REG_BACKDOOR_DBI_DATA + 2);
		count++;
	}
	if (ppsc->support_backdoor || (0 == tmp8)) {
		tmp32 = rtl_read_dword(rtlpriv, REG_BACKDOOR_DBI_RDATA);
		rtl_write_dword(rtlpriv, REG_BACKDOOR_DBI_WDATA,
				tmp32 | BIT(11) | BIT(12));
		rtl_write_word(rtlpriv, REG_BACKDOOR_DBI_DATA, 0xf718);
		rtl_write_byte(rtlpriv, REG_BACKDOOR_DBI_DATA + 2, 0x1);
	}
	tmp8 = rtl_read_byte(rtlpriv, REG_BACKDOOR_DBI_DATA + 2);
	count = 0;
	while (tmp8 && count < 20) {
		udelay(10);
		tmp8 = rtl_read_byte(rtlpriv, REG_BACKDOOR_DBI_DATA + 2);
		count++;
	}
}

void rtl92ee_enable_hw_security_config(struct ieee80211_hw *hw)
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

static bool _rtl8192ee_check_pcie_dma_hang(struct rtl_priv *rtlpriv)
{
	u8 tmp;

	/* write reg 0x350 Bit[26]=1. Enable debug port. */
	tmp = rtl_read_byte(rtlpriv, REG_BACKDOOR_DBI_DATA + 3);
	if (!(tmp & BIT(2))) {
		rtl_write_byte(rtlpriv, REG_BACKDOOR_DBI_DATA + 3,
			       tmp | BIT(2));
		mdelay(100); /* Suggested by DD Justin_tsai. */
	}

	/* read reg 0x350 Bit[25] if 1 : RX hang
	 * read reg 0x350 Bit[24] if 1 : TX hang
	 */
	tmp = rtl_read_byte(rtlpriv, REG_BACKDOOR_DBI_DATA + 3);
	if ((tmp & BIT(0)) || (tmp & BIT(1))) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "CheckPcieDMAHang8192EE(): true!!\n");
		return true;
	}
	return false;
}

static void _rtl8192ee_reset_pcie_interface_dma(struct rtl_priv *rtlpriv,
						bool mac_power_on)
{
	u8 tmp;
	bool release_mac_rx_pause;
	u8 backup_pcie_dma_pause;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "ResetPcieInterfaceDMA8192EE()\n");

	/* Revise Note: Follow the document "PCIe RX DMA Hang Reset Flow_v03"
	 * released by SD1 Alan.
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
		/* Already pause before the function for another reason. */
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

int rtl92ee_hw_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	bool rtstatus = true;
	int err = 0;
	u8 tmp_u1b, u1byte;
	u32 tmp_u4b;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, " Rtl8192EE hw init\n");
	rtlpriv->rtlhal.being_init_adapter = true;
	rtlpriv->intf_ops->disable_aspm(hw);

	tmp_u1b = rtl_read_byte(rtlpriv, REG_SYS_CLKR+1);
	u1byte = rtl_read_byte(rtlpriv, REG_CR);
	if ((tmp_u1b & BIT(3)) && (u1byte != 0 && u1byte != 0xEA)) {
		rtlhal->mac_func_enable = true;
	} else {
		rtlhal->mac_func_enable = false;
		rtlhal->fw_ps_state = FW_PS_STATE_ALL_ON_92E;
	}

	if (_rtl8192ee_check_pcie_dma_hang(rtlpriv)) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "92ee dma hang!\n");
		_rtl8192ee_reset_pcie_interface_dma(rtlpriv,
						    rtlhal->mac_func_enable);
		rtlhal->mac_func_enable = false;
	}

	rtstatus = _rtl92ee_init_mac(hw);

	rtl_write_byte(rtlpriv, 0x577, 0x03);

	/*for Crystal 40 Mhz setting */
	rtl_write_byte(rtlpriv, REG_AFE_CTRL4, 0x2A);
	rtl_write_byte(rtlpriv, REG_AFE_CTRL4 + 1, 0x00);
	rtl_write_byte(rtlpriv, REG_AFE_CTRL2, 0x83);

	/*Forced the antenna b to wifi */
	if (rtlpriv->btcoexist.btc_info.btcoexist == 1) {
		rtl_write_byte(rtlpriv, 0x64, 0);
		rtl_write_byte(rtlpriv, 0x65, 1);
	}
	if (!rtstatus) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "Init MAC failed\n");
		err = 1;
		return err;
	}
	rtlhal->rx_tag = 0;
	rtl_write_word(rtlpriv, REG_PCIE_CTRL_REG, 0x8000);
	err = rtl92ee_download_fw(hw, false);
	if (err) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 "Failed to download FW. Init HW without FW now..\n");
		err = 1;
		rtlhal->fw_ready = false;
		return err;
	}
	rtlhal->fw_ready = true;
	/*fw related variable initialize */
	ppsc->fw_current_inpsmode = false;
	rtlhal->fw_ps_state = FW_PS_STATE_ALL_ON_92E;
	rtlhal->fw_clk_change_in_progress = false;
	rtlhal->allow_sw_to_change_hwclc = false;
	rtlhal->last_hmeboxnum = 0;

	rtl92ee_phy_mac_config(hw);

	rtl92ee_phy_bb_config(hw);

	rtl92ee_phy_rf_config(hw);

	rtlphy->rfreg_chnlval[0] = rtl_get_rfreg(hw, RF90_PATH_A,
						 RF_CHNLBW, RFREG_OFFSET_MASK);
	rtlphy->rfreg_chnlval[1] = rtl_get_rfreg(hw, RF90_PATH_B,
						 RF_CHNLBW, RFREG_OFFSET_MASK);
	rtlphy->backup_rf_0x1a = (u32)rtl_get_rfreg(hw, RF90_PATH_A, RF_RX_G1,
						    RFREG_OFFSET_MASK);
	rtlphy->rfreg_chnlval[0] = (rtlphy->rfreg_chnlval[0] & 0xfffff3ff) |
				   BIT(10) | BIT(11);

	rtl_set_rfreg(hw, RF90_PATH_A, RF_CHNLBW, RFREG_OFFSET_MASK,
		      rtlphy->rfreg_chnlval[0]);
	rtl_set_rfreg(hw, RF90_PATH_B, RF_CHNLBW, RFREG_OFFSET_MASK,
		      rtlphy->rfreg_chnlval[0]);

	/*---- Set CCK and OFDM Block "ON"----*/
	rtl_set_bbreg(hw, RFPGA0_RFMOD, BCCKEN, 0x1);
	rtl_set_bbreg(hw, RFPGA0_RFMOD, BOFDMEN, 0x1);

	/* Must set this,
	 * otherwise the rx sensitivity will be very pool. Maddest
	 */
	rtl_set_rfreg(hw, RF90_PATH_A, 0xB1, RFREG_OFFSET_MASK, 0x54418);

	/*Set Hardware(MAC default setting.)*/
	_rtl92ee_hw_configure(hw);

	rtlhal->mac_func_enable = true;

	rtl_cam_reset_all_entry(hw);
	rtl92ee_enable_hw_security_config(hw);

	ppsc->rfpwr_state = ERFON;

	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_ETHER_ADDR, mac->mac_addr);
	_rtl92ee_enable_aspm_back_door(hw);
	rtlpriv->intf_ops->enable_aspm(hw);

	rtl92ee_bt_hw_init(hw);

	rtlpriv->rtlhal.being_init_adapter = false;

	if (ppsc->rfpwr_state == ERFON) {
		if (rtlphy->iqk_initialized) {
			rtl92ee_phy_iq_calibrate(hw, true);
		} else {
			rtl92ee_phy_iq_calibrate(hw, false);
			rtlphy->iqk_initialized = true;
		}
	}

	rtlphy->rfpath_rx_enable[0] = true;
	if (rtlphy->rf_type == RF_2T2R)
		rtlphy->rfpath_rx_enable[1] = true;

	efuse_one_byte_read(hw, 0x1FA, &tmp_u1b);
	if (!(tmp_u1b & BIT(0))) {
		rtl_set_rfreg(hw, RF90_PATH_A, 0x15, 0x0F, 0x05);
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "PA BIAS path A\n");
	}

	if ((!(tmp_u1b & BIT(1))) && (rtlphy->rf_type == RF_2T2R)) {
		rtl_set_rfreg(hw, RF90_PATH_B, 0x15, 0x0F, 0x05);
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "PA BIAS path B\n");
	}

	rtl_write_byte(rtlpriv, REG_NAV_UPPER, ((30000 + 127) / 128));

	/*Fixed LDPC rx hang issue. */
	tmp_u4b = rtl_read_dword(rtlpriv, REG_SYS_SWR_CTRL1);
	rtl_write_byte(rtlpriv, REG_SYS_SWR_CTRL2, 0x75);
	tmp_u4b =  (tmp_u4b & 0xfff00fff) | (0x7E << 12);
	rtl_write_dword(rtlpriv, REG_SYS_SWR_CTRL1, tmp_u4b);

	rtl92ee_dm_init(hw);

	rtl_write_dword(rtlpriv, 0x4fc, 0);

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "end of Rtl8192EE hw init %x\n", err);
	return 0;
}

static enum version_8192e _rtl92ee_read_chip_version(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	enum version_8192e version = VERSION_UNKNOWN;
	u32 value32;

	rtlphy->rf_type = RF_2T2R;

	value32 = rtl_read_dword(rtlpriv, REG_SYS_CFG1);
	if (value32 & TRP_VAUX_EN)
		version = (enum version_8192e)VERSION_TEST_CHIP_2T2R_8192E;
	else
		version = (enum version_8192e)VERSION_NORMAL_CHIP_2T2R_8192E;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "Chip RF Type: %s\n", (rtlphy->rf_type == RF_2T2R) ?
		  "RF_2T2R" : "RF_1T1R");

	return version;
}

static int _rtl92ee_set_media_status(struct ieee80211_hw *hw,
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
		_rtl92ee_stop_tx_beacon(hw);
		_rtl92ee_enable_bcn_sub_func(hw);
	} else if (mode == MSR_ADHOC || mode == MSR_AP) {
		_rtl92ee_resume_tx_beacon(hw);
		_rtl92ee_disable_bcn_sub_func(hw);
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

void rtl92ee_set_check_bssid(struct ieee80211_hw *hw, bool check_bssid)
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
		_rtl92ee_set_bcn_ctrl_reg(hw, 0, BIT(4));
	} else {
		reg_rcr &= (~(RCR_CBSSID_DATA | RCR_CBSSID_BCN));
		_rtl92ee_set_bcn_ctrl_reg(hw, BIT(4), 0);
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_RCR,
					      (u8 *)(&reg_rcr));
	}
}

int rtl92ee_set_network_type(struct ieee80211_hw *hw, enum nl80211_iftype type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (_rtl92ee_set_media_status(hw, type))
		return -EOPNOTSUPP;

	if (rtlpriv->mac80211.link_state == MAC80211_LINKED) {
		if (type != NL80211_IFTYPE_AP &&
		    type != NL80211_IFTYPE_MESH_POINT)
			rtl92ee_set_check_bssid(hw, true);
	} else {
		rtl92ee_set_check_bssid(hw, false);
	}

	return 0;
}

/* don't set REG_EDCA_BE_PARAM here because mac80211 will send pkt when scan */
void rtl92ee_set_qos(struct ieee80211_hw *hw, int aci)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl92ee_dm_init_edca_turbo(hw);
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
		RT_ASSERT(false, "invalid aci: %d !\n", aci);
		break;
	}
}

void rtl92ee_enable_interrupt(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	rtl_write_dword(rtlpriv, REG_HIMR, rtlpci->irq_mask[0] & 0xFFFFFFFF);
	rtl_write_dword(rtlpriv, REG_HIMRE, rtlpci->irq_mask[1] & 0xFFFFFFFF);
	rtlpci->irq_enabled = true;
}

void rtl92ee_disable_interrupt(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	rtl_write_dword(rtlpriv, REG_HIMR, IMR_DISABLED);
	rtl_write_dword(rtlpriv, REG_HIMRE, IMR_DISABLED);
	rtlpci->irq_enabled = false;
	/*synchronize_irq(rtlpci->pdev->irq);*/
}

static void _rtl92ee_poweroff_adapter(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 u1b_tmp;

	rtlhal->mac_func_enable = false;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "POWER OFF adapter\n");

	/* Run LPS WL RFOFF flow */
	rtl_hal_pwrseqcmdparsing(rtlpriv, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK,
				 PWR_INTF_PCI_MSK, RTL8192E_NIC_LPS_ENTER_FLOW);
	/* turn off RF */
	rtl_write_byte(rtlpriv, REG_RF_CTRL, 0x00);

	/* ==== Reset digital sequence   ======  */
	if ((rtl_read_byte(rtlpriv, REG_MCUFWDL) & BIT(7)) && rtlhal->fw_ready)
		rtl92ee_firmware_selfreset(hw);

	/* Reset MCU  */
	u1b_tmp = rtl_read_byte(rtlpriv, REG_SYS_FUNC_EN + 1);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN + 1, (u1b_tmp & (~BIT(2))));

	/* reset MCU ready status */
	rtl_write_byte(rtlpriv, REG_MCUFWDL, 0x00);

	/* HW card disable configuration. */
	rtl_hal_pwrseqcmdparsing(rtlpriv, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK,
				 PWR_INTF_PCI_MSK, RTL8192E_NIC_DISABLE_FLOW);

	/* Reset MCU IO Wrapper */
	u1b_tmp = rtl_read_byte(rtlpriv, REG_RSV_CTRL + 1);
	rtl_write_byte(rtlpriv, REG_RSV_CTRL + 1, (u1b_tmp & (~BIT(0))));
	u1b_tmp = rtl_read_byte(rtlpriv, REG_RSV_CTRL + 1);
	rtl_write_byte(rtlpriv, REG_RSV_CTRL + 1, (u1b_tmp | BIT(0)));

	/* lock ISO/CLK/Power control register */
	rtl_write_byte(rtlpriv, REG_RSV_CTRL, 0x0E);
}

void rtl92ee_card_disable(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	enum nl80211_iftype opmode;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "RTL8192ee card disable\n");

	RT_SET_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC);

	mac->link_state = MAC80211_NOLINK;
	opmode = NL80211_IFTYPE_UNSPECIFIED;

	_rtl92ee_set_media_status(hw, opmode);

	if (rtlpriv->rtlhal.driver_is_goingto_unload ||
	    ppsc->rfoff_reason > RF_CHANGE_BY_PS)
		rtlpriv->cfg->ops->led_control(hw, LED_CTL_POWER_OFF);

	_rtl92ee_poweroff_adapter(hw);

	/* after power off we should do iqk again */
	rtlpriv->phy.iqk_initialized = false;
}

void rtl92ee_interrupt_recognized(struct ieee80211_hw *hw,
				  u32 *p_inta, u32 *p_intb)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	*p_inta = rtl_read_dword(rtlpriv, ISR) & rtlpci->irq_mask[0];
	rtl_write_dword(rtlpriv, ISR, *p_inta);

	*p_intb = rtl_read_dword(rtlpriv, REG_HISRE) & rtlpci->irq_mask[1];
	rtl_write_dword(rtlpriv, REG_HISRE, *p_intb);
}

void rtl92ee_set_beacon_related_registers(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u16 bcn_interval, atim_window;

	bcn_interval = mac->beacon_interval;
	atim_window = 2;	/*FIX MERGE */
	rtl92ee_disable_interrupt(hw);
	rtl_write_word(rtlpriv, REG_ATIMWND, atim_window);
	rtl_write_word(rtlpriv, REG_BCN_INTERVAL, bcn_interval);
	rtl_write_word(rtlpriv, REG_BCNTCFG, 0x660f);
	rtl_write_byte(rtlpriv, REG_RXTSF_OFFSET_CCK, 0x18);
	rtl_write_byte(rtlpriv, REG_RXTSF_OFFSET_OFDM, 0x18);
	rtl_write_byte(rtlpriv, 0x606, 0x30);
	rtlpci->reg_bcn_ctrl_val |= BIT(3);
	rtl_write_byte(rtlpriv, REG_BCN_CTRL, (u8)rtlpci->reg_bcn_ctrl_val);
}

void rtl92ee_set_beacon_interval(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u16 bcn_interval = mac->beacon_interval;

	RT_TRACE(rtlpriv, COMP_BEACON, DBG_DMESG,
		 "beacon_interval:%d\n", bcn_interval);
	rtl_write_word(rtlpriv, REG_BCN_INTERVAL, bcn_interval);
}

void rtl92ee_update_interrupt_mask(struct ieee80211_hw *hw,
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
	rtl92ee_disable_interrupt(hw);
	rtl92ee_enable_interrupt(hw);
}

static u8 _rtl92ee_get_chnl_group(u8 chnl)
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
		else if (12 <= chnl && chnl <= 14)
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
	}
	return group;
}

static void _rtl8192ee_read_power_value_fromprom(struct ieee80211_hw *hw,
						 struct txpower_info_2g *pwr2g,
						 struct txpower_info_5g *pwr5g,
						 bool autoload_fail, u8 *hwinfo)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 rf, addr = EEPROM_TX_PWR_INX, group, i = 0;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "hal_ReadPowerValueFromPROM92E(): PROMContent[0x%x]=0x%x\n",
		 (addr + 1), hwinfo[addr + 1]);
	if (0xFF == hwinfo[addr+1])  /*YJ,add,120316*/
		autoload_fail = true;

	if (autoload_fail) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "auto load fail : Use Default value!\n");
		for (rf = 0 ; rf < MAX_RF_PATH ; rf++) {
			/* 2.4G default value */
			for (group = 0 ; group < MAX_CHNL_GROUP_24G; group++) {
				pwr2g->index_cck_base[rf][group] = 0x2D;
				pwr2g->index_bw40_base[rf][group] = 0x2D;
			}
			for (i = 0; i < MAX_TX_COUNT; i++) {
				if (i == 0) {
					pwr2g->bw20_diff[rf][0] = 0x02;
					pwr2g->ofdm_diff[rf][0] = 0x04;
				} else {
					pwr2g->bw20_diff[rf][i] = 0xFE;
					pwr2g->bw40_diff[rf][i] = 0xFE;
					pwr2g->cck_diff[rf][i] = 0xFE;
					pwr2g->ofdm_diff[rf][i] = 0xFE;
				}
			}

			/*5G default value*/
			for (group = 0 ; group < MAX_CHNL_GROUP_5G; group++)
				pwr5g->index_bw40_base[rf][group] = 0x2A;

			for (i = 0; i < MAX_TX_COUNT; i++) {
				if (i == 0) {
					pwr5g->ofdm_diff[rf][0] = 0x04;
					pwr5g->bw20_diff[rf][0] = 0x00;
					pwr5g->bw80_diff[rf][0] = 0xFE;
					pwr5g->bw160_diff[rf][0] = 0xFE;
				} else {
					pwr5g->ofdm_diff[rf][0] = 0xFE;
					pwr5g->bw20_diff[rf][0] = 0xFE;
					pwr5g->bw40_diff[rf][0] = 0xFE;
					pwr5g->bw80_diff[rf][0] = 0xFE;
					pwr5g->bw160_diff[rf][0] = 0xFE;
				}
			}
		}
		return;
	}

	rtl_priv(hw)->efuse.txpwr_fromeprom = true;

	for (rf = 0 ; rf < MAX_RF_PATH ; rf++) {
		/*2.4G default value*/
		for (group = 0 ; group < MAX_CHNL_GROUP_24G; group++) {
			pwr2g->index_cck_base[rf][group] = hwinfo[addr++];
			if (pwr2g->index_cck_base[rf][group] == 0xFF)
				pwr2g->index_cck_base[rf][group] = 0x2D;
		}
		for (group = 0 ; group < MAX_CHNL_GROUP_24G - 1; group++) {
			pwr2g->index_bw40_base[rf][group] = hwinfo[addr++];
			if (pwr2g->index_bw40_base[rf][group] == 0xFF)
				pwr2g->index_bw40_base[rf][group] = 0x2D;
		}
		for (i = 0; i < MAX_TX_COUNT; i++) {
			if (i == 0) {
				pwr2g->bw40_diff[rf][i] = 0;
				if (hwinfo[addr] == 0xFF) {
					pwr2g->bw20_diff[rf][i] = 0x02;
				} else {
					pwr2g->bw20_diff[rf][i] = (hwinfo[addr]
								   & 0xf0) >> 4;
					if (pwr2g->bw20_diff[rf][i] & BIT(3))
						pwr2g->bw20_diff[rf][i] |= 0xF0;
				}

				if (hwinfo[addr] == 0xFF) {
					pwr2g->ofdm_diff[rf][i] = 0x04;
				} else {
					pwr2g->ofdm_diff[rf][i] = (hwinfo[addr]
								   & 0x0f);
					if (pwr2g->ofdm_diff[rf][i] & BIT(3))
						pwr2g->ofdm_diff[rf][i] |= 0xF0;
				}
				pwr2g->cck_diff[rf][i] = 0;
				addr++;
			} else {
				if (hwinfo[addr] == 0xFF) {
					pwr2g->bw40_diff[rf][i] = 0xFE;
				} else {
					pwr2g->bw40_diff[rf][i] = (hwinfo[addr]
								   & 0xf0) >> 4;
					if (pwr2g->bw40_diff[rf][i] & BIT(3))
						pwr2g->bw40_diff[rf][i] |= 0xF0;
				}

				if (hwinfo[addr] == 0xFF) {
					pwr2g->bw20_diff[rf][i] = 0xFE;
				} else {
					pwr2g->bw20_diff[rf][i] = (hwinfo[addr]
								   & 0x0f);
					if (pwr2g->bw20_diff[rf][i] & BIT(3))
						pwr2g->bw20_diff[rf][i] |= 0xF0;
				}
				addr++;

				if (hwinfo[addr] == 0xFF) {
					pwr2g->ofdm_diff[rf][i] = 0xFE;
				} else {
					pwr2g->ofdm_diff[rf][i] = (hwinfo[addr]
								   & 0xf0) >> 4;
					if (pwr2g->ofdm_diff[rf][i] & BIT(3))
						pwr2g->ofdm_diff[rf][i] |= 0xF0;
				}

				if (hwinfo[addr] == 0xFF) {
					pwr2g->cck_diff[rf][i] = 0xFE;
				} else {
					pwr2g->cck_diff[rf][i] = (hwinfo[addr]
								  & 0x0f);
					if (pwr2g->cck_diff[rf][i] & BIT(3))
						pwr2g->cck_diff[rf][i] |= 0xF0;
				}
				addr++;
			}
		}

		/*5G default value*/
		for (group = 0 ; group < MAX_CHNL_GROUP_5G; group++) {
			pwr5g->index_bw40_base[rf][group] = hwinfo[addr++];
			if (pwr5g->index_bw40_base[rf][group] == 0xFF)
				pwr5g->index_bw40_base[rf][group] = 0xFE;
		}

		for (i = 0; i < MAX_TX_COUNT; i++) {
			if (i == 0) {
				pwr5g->bw40_diff[rf][i] = 0;

				if (hwinfo[addr] == 0xFF) {
					pwr5g->bw20_diff[rf][i] = 0;
				} else {
					pwr5g->bw20_diff[rf][0] = (hwinfo[addr]
								   & 0xf0) >> 4;
					if (pwr5g->bw20_diff[rf][i] & BIT(3))
						pwr5g->bw20_diff[rf][i] |= 0xF0;
				}

				if (hwinfo[addr] == 0xFF) {
					pwr5g->ofdm_diff[rf][i] = 0x04;
				} else {
					pwr5g->ofdm_diff[rf][0] = (hwinfo[addr]
								   & 0x0f);
					if (pwr5g->ofdm_diff[rf][i] & BIT(3))
						pwr5g->ofdm_diff[rf][i] |= 0xF0;
				}
				addr++;
			} else {
				if (hwinfo[addr] == 0xFF) {
					pwr5g->bw40_diff[rf][i] = 0xFE;
				} else {
					pwr5g->bw40_diff[rf][i] = (hwinfo[addr]
								  & 0xf0) >> 4;
					if (pwr5g->bw40_diff[rf][i] & BIT(3))
						pwr5g->bw40_diff[rf][i] |= 0xF0;
				}

				if (hwinfo[addr] == 0xFF) {
					pwr5g->bw20_diff[rf][i] = 0xFE;
				} else {
					pwr5g->bw20_diff[rf][i] = (hwinfo[addr]
								   & 0x0f);
					if (pwr5g->bw20_diff[rf][i] & BIT(3))
						pwr5g->bw20_diff[rf][i] |= 0xF0;
				}
				addr++;
			}
		}

		if (hwinfo[addr] == 0xFF) {
			pwr5g->ofdm_diff[rf][1] = 0xFE;
			pwr5g->ofdm_diff[rf][2] = 0xFE;
		} else {
			pwr5g->ofdm_diff[rf][1] = (hwinfo[addr] & 0xf0) >> 4;
			pwr5g->ofdm_diff[rf][2] = (hwinfo[addr] & 0x0f);
		}
		addr++;

		if (hwinfo[addr] == 0xFF)
			pwr5g->ofdm_diff[rf][3] = 0xFE;
		else
			pwr5g->ofdm_diff[rf][3] = (hwinfo[addr] & 0x0f);
		addr++;

		for (i = 1; i < MAX_TX_COUNT; i++) {
			if (pwr5g->ofdm_diff[rf][i] == 0xFF)
				pwr5g->ofdm_diff[rf][i] = 0xFE;
			else if (pwr5g->ofdm_diff[rf][i] & BIT(3))
				pwr5g->ofdm_diff[rf][i] |= 0xF0;
		}

		for (i = 0; i < MAX_TX_COUNT; i++) {
			if (hwinfo[addr] == 0xFF) {
				pwr5g->bw80_diff[rf][i] = 0xFE;
			} else {
				pwr5g->bw80_diff[rf][i] = (hwinfo[addr] & 0xf0)
							  >> 4;
				if (pwr5g->bw80_diff[rf][i] & BIT(3))
					pwr5g->bw80_diff[rf][i] |= 0xF0;
			}

			if (hwinfo[addr] == 0xFF) {
				pwr5g->bw160_diff[rf][i] = 0xFE;
			} else {
				pwr5g->bw160_diff[rf][i] =
				  (hwinfo[addr] & 0x0f);
				if (pwr5g->bw160_diff[rf][i] & BIT(3))
					pwr5g->bw160_diff[rf][i] |= 0xF0;
			}
			addr++;
		}
	}
}

static void _rtl92ee_read_txpower_info_from_hwpg(struct ieee80211_hw *hw,
						 bool autoload_fail, u8 *hwinfo)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *efu = rtl_efuse(rtl_priv(hw));
	struct txpower_info_2g pwr2g;
	struct txpower_info_5g pwr5g;
	u8 rf, idx;
	u8 i;

	_rtl8192ee_read_power_value_fromprom(hw, &pwr2g, &pwr5g,
					     autoload_fail, hwinfo);

	for (rf = 0; rf < MAX_RF_PATH; rf++) {
		for (i = 0; i < 14; i++) {
			idx = _rtl92ee_get_chnl_group(i + 1);

			if (i == CHANNEL_MAX_NUMBER_2G - 1) {
				efu->txpwrlevel_cck[rf][i] =
						pwr2g.index_cck_base[rf][5];
				efu->txpwrlevel_ht40_1s[rf][i] =
						pwr2g.index_bw40_base[rf][idx];
			} else {
				efu->txpwrlevel_cck[rf][i] =
						pwr2g.index_cck_base[rf][idx];
				efu->txpwrlevel_ht40_1s[rf][i] =
						pwr2g.index_bw40_base[rf][idx];
			}
		}
		for (i = 0; i < CHANNEL_MAX_NUMBER_5G; i++) {
			idx = _rtl92ee_get_chnl_group(channel5g[i]);
			efu->txpwr_5g_bw40base[rf][i] =
					pwr5g.index_bw40_base[rf][idx];
		}
		for (i = 0; i < CHANNEL_MAX_NUMBER_5G_80M; i++) {
			u8 upper, lower;

			idx = _rtl92ee_get_chnl_group(channel5g_80m[i]);
			upper = pwr5g.index_bw40_base[rf][idx];
			lower = pwr5g.index_bw40_base[rf][idx + 1];

			efu->txpwr_5g_bw80base[rf][i] = (upper + lower) / 2;
		}
		for (i = 0; i < MAX_TX_COUNT; i++) {
			efu->txpwr_cckdiff[rf][i] = pwr2g.cck_diff[rf][i];
			efu->txpwr_legacyhtdiff[rf][i] = pwr2g.ofdm_diff[rf][i];
			efu->txpwr_ht20diff[rf][i] = pwr2g.bw20_diff[rf][i];
			efu->txpwr_ht40diff[rf][i] = pwr2g.bw40_diff[rf][i];

			efu->txpwr_5g_ofdmdiff[rf][i] = pwr5g.ofdm_diff[rf][i];
			efu->txpwr_5g_bw20diff[rf][i] = pwr5g.bw20_diff[rf][i];
			efu->txpwr_5g_bw40diff[rf][i] = pwr5g.bw40_diff[rf][i];
			efu->txpwr_5g_bw80diff[rf][i] = pwr5g.bw80_diff[rf][i];
		}
	}

	if (!autoload_fail)
		efu->eeprom_thermalmeter = hwinfo[EEPROM_THERMAL_METER_92E];
	else
		efu->eeprom_thermalmeter = EEPROM_DEFAULT_THERMALMETER;

	if (efu->eeprom_thermalmeter == 0xff || autoload_fail) {
		efu->apk_thermalmeterignore = true;
		efu->eeprom_thermalmeter = EEPROM_DEFAULT_THERMALMETER;
	}

	efu->thermalmeter[0] = efu->eeprom_thermalmeter;
	RTPRINT(rtlpriv, FINIT, INIT_TXPOWER,
		"thermalmeter = 0x%x\n", efu->eeprom_thermalmeter);

	if (!autoload_fail) {
		efu->eeprom_regulatory = hwinfo[EEPROM_RF_BOARD_OPTION_92E]
					 & 0x07;
		if (hwinfo[EEPROM_RF_BOARD_OPTION_92E] == 0xFF)
			efu->eeprom_regulatory = 0;
	} else {
		efu->eeprom_regulatory = 0;
	}
	RTPRINT(rtlpriv, FINIT, INIT_TXPOWER,
		"eeprom_regulatory = 0x%x\n", efu->eeprom_regulatory);
}

static void _rtl92ee_read_adapter_info(struct ieee80211_hw *hw)
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
	if (eeprom_id != RTL8192E_EEPROM_ID) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 "EEPROM ID(%#x) is invalid!!\n", eeprom_id);
		rtlefuse->autoload_failflag = true;
	} else {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "Autoload OK\n");
		rtlefuse->autoload_failflag = false;
	}

	if (rtlefuse->autoload_failflag)
		return;
	/*VID DID SVID SDID*/
	rtlefuse->eeprom_vid = *(u16 *)&hwinfo[EEPROM_VID];
	rtlefuse->eeprom_did = *(u16 *)&hwinfo[EEPROM_DID];
	rtlefuse->eeprom_svid = *(u16 *)&hwinfo[EEPROM_SVID];
	rtlefuse->eeprom_smid = *(u16 *)&hwinfo[EEPROM_SMID];
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "EEPROMId = 0x%4x\n", eeprom_id);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "EEPROM VID = 0x%4x\n", rtlefuse->eeprom_vid);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "EEPROM DID = 0x%4x\n", rtlefuse->eeprom_did);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "EEPROM SVID = 0x%4x\n", rtlefuse->eeprom_svid);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "EEPROM SMID = 0x%4x\n", rtlefuse->eeprom_smid);
	/*customer ID*/
	rtlefuse->eeprom_oemid = *(u8 *)&hwinfo[EEPROM_CUSTOMER_ID];
	if (rtlefuse->eeprom_oemid == 0xFF)
		rtlefuse->eeprom_oemid = 0;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "EEPROM Customer ID: 0x%2x\n", rtlefuse->eeprom_oemid);
	/*EEPROM version*/
	rtlefuse->eeprom_version = *(u8 *)&hwinfo[EEPROM_VERSION];
	/*mac address*/
	for (i = 0; i < 6; i += 2) {
		usvalue = *(u16 *)&hwinfo[EEPROM_MAC_ADDR + i];
		*((u16 *)(&rtlefuse->dev_addr[i])) = usvalue;
	}

	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
		 "dev_addr: %pM\n", rtlefuse->dev_addr);
	/*channel plan */
	rtlefuse->eeprom_channelplan = *(u8 *)&hwinfo[EEPROM_CHANNELPLAN];
	/* set channel plan from efuse */
	rtlefuse->channel_plan = rtlefuse->eeprom_channelplan;
	/*tx power*/
	_rtl92ee_read_txpower_info_from_hwpg(hw, rtlefuse->autoload_failflag,
					     hwinfo);

	rtl92ee_read_bt_coexist_info_from_hwpg(hw, rtlefuse->autoload_failflag,
					       hwinfo);

	/*board type*/
	rtlefuse->board_type = (((*(u8 *)&hwinfo[EEPROM_RF_BOARD_OPTION_92E])
				& 0xE0) >> 5);
	if ((*(u8 *)&hwinfo[EEPROM_RF_BOARD_OPTION_92E]) == 0xFF)
		rtlefuse->board_type = 0;

	rtlhal->board_type = rtlefuse->board_type;
	/*parse xtal*/
	rtlefuse->crystalcap = hwinfo[EEPROM_XTAL_92E];
	if (hwinfo[EEPROM_XTAL_92E] == 0xFF)
		rtlefuse->crystalcap = 0x20;

	/*antenna diversity*/
	rtlefuse->antenna_div_type = NO_ANTDIV;
	rtlefuse->antenna_div_cfg = 0;

	if (rtlhal->oem_id == RT_CID_DEFAULT) {
		switch (rtlefuse->eeprom_oemid) {
		case EEPROM_CID_DEFAULT:
			if (rtlefuse->eeprom_did == 0x818B) {
				if ((rtlefuse->eeprom_svid == 0x10EC) &&
				    (rtlefuse->eeprom_smid == 0x001B))
					rtlhal->oem_id = RT_CID_819X_LENOVO;
			} else {
				rtlhal->oem_id = RT_CID_DEFAULT;
			}
			break;
		default:
			rtlhal->oem_id = RT_CID_DEFAULT;
			break;
		}
	}
}

static void _rtl92ee_hal_customized_behavior(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	pcipriv->ledctl.led_opendrain = true;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
		 "RT Customized ID: 0x%02X\n", rtlhal->oem_id);
}

void rtl92ee_read_eeprom_info(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 tmp_u1b;

	rtlhal->version = _rtl92ee_read_chip_version(hw);
	if (get_rf_type(rtlphy) == RF_1T1R) {
		rtlpriv->dm.rfpath_rxenable[0] = true;
	} else {
		rtlpriv->dm.rfpath_rxenable[0] = true;
		rtlpriv->dm.rfpath_rxenable[1] = true;
	}
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
		_rtl92ee_read_adapter_info(hw);
	} else {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "Autoload ERR!!\n");
	}
	_rtl92ee_hal_customized_behavior(hw);

	rtlphy->rfpath_rx_enable[0] = true;
	if (rtlphy->rf_type == RF_2T2R)
		rtlphy->rfpath_rx_enable[1] = true;
}

static u8 _rtl92ee_mrate_idx_to_arfr_id(struct ieee80211_hw *hw, u8 rate_index)
{
	u8 ret = 0;

	switch (rate_index) {
	case RATR_INX_WIRELESS_NGB:
		ret = 0;
		break;
	case RATR_INX_WIRELESS_N:
	case RATR_INX_WIRELESS_NG:
		ret = 4;
		break;
	case RATR_INX_WIRELESS_NB:
		ret = 2;
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

static void rtl92ee_update_hal_rate_mask(struct ieee80211_hw *hw,
					 struct ieee80211_sta *sta,
					 u8 rssi_level)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_sta_info *sta_entry = NULL;
	u32 ratr_bitmap;
	u8 ratr_index;
	u8 curtxbw_40mhz = (sta->ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40)
			     ? 1 : 0;
	u8 b_curshortgi_40mhz = (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_40) ?
				1 : 0;
	u8 b_curshortgi_20mhz = (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_20) ?
				1 : 0;
	enum wireless_mode wirelessmode = 0;
	bool b_shortgi = false;
	u8 rate_mask[7] = {0};
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
		if (curtxbw_40mhz)
			ratr_index = RATR_INX_WIRELESS_NGB;
		else
			ratr_index = RATR_INX_WIRELESS_NB;

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
					ratr_bitmap &= 0x0ffff000;
				else
					ratr_bitmap &= 0x0ffff015;
			} else {
				if (rssi_level == 1)
					ratr_bitmap &= 0x0f8f0000;
				else if (rssi_level == 2)
					ratr_bitmap &= 0x0ffff000;
				else
					ratr_bitmap &= 0x0ffff005;
			}
		}

		if ((curtxbw_40mhz && b_curshortgi_40mhz) ||
		    (!curtxbw_40mhz && b_curshortgi_20mhz)) {
			if (macid == 0)
				b_shortgi = true;
			else if (macid == 1)
				b_shortgi = false;
		}
		break;
	default:
		ratr_index = RATR_INX_WIRELESS_NGB;

		if (rtlphy->rf_type == RF_1T1R)
			ratr_bitmap &= 0x000ff0ff;
		else
			ratr_bitmap &= 0x0f8ff0ff;
		break;
	}
	ratr_index = _rtl92ee_mrate_idx_to_arfr_id(hw, ratr_index);
	sta_entry->ratr_index = ratr_index;

	RT_TRACE(rtlpriv, COMP_RATR, DBG_DMESG,
		 "ratr_bitmap :%x\n", ratr_bitmap);
	*(u32 *)&rate_mask = (ratr_bitmap & 0x0fffffff) |
				       (ratr_index << 28);
	rate_mask[0] = macid;
	rate_mask[1] = ratr_index | (b_shortgi ? 0x80 : 0x00);
	rate_mask[2] = curtxbw_40mhz;
	rate_mask[3] = (u8)(ratr_bitmap & 0x000000ff);
	rate_mask[4] = (u8)((ratr_bitmap & 0x0000ff00) >> 8);
	rate_mask[5] = (u8)((ratr_bitmap & 0x00ff0000) >> 16);
	rate_mask[6] = (u8)((ratr_bitmap & 0xff000000) >> 24);
	RT_TRACE(rtlpriv, COMP_RATR, DBG_DMESG,
		 "Rate_index:%x, ratr_val:%x, %x:%x:%x:%x:%x:%x:%x\n",
		  ratr_index, ratr_bitmap, rate_mask[0], rate_mask[1],
		  rate_mask[2], rate_mask[3], rate_mask[4],
		  rate_mask[5], rate_mask[6]);
	rtl92ee_fill_h2c_cmd(hw, H2C_92E_RA_MASK, 7, rate_mask);
	_rtl92ee_set_bcn_ctrl_reg(hw, BIT(3), 0);
}

void rtl92ee_update_hal_rate_tbl(struct ieee80211_hw *hw,
				 struct ieee80211_sta *sta, u8 rssi_level)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (rtlpriv->dm.useramask)
		rtl92ee_update_hal_rate_mask(hw, sta, rssi_level);
}

void rtl92ee_update_channel_access_setting(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u16 sifs_timer;

	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_SLOT_TIME,
				      (u8 *)&mac->slot_time);
	if (!mac->ht_enable)
		sifs_timer = 0x0a0a;
	else
		sifs_timer = 0x0e0e;
	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_SIFS, (u8 *)&sifs_timer);
}

bool rtl92ee_gpio_radio_on_off_checking(struct ieee80211_hw *hw, u8 *valid)
{
	*valid = 1;
	return true;
}

void rtl92ee_set_key(struct ieee80211_hw *hw, u32 key_index,
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
			RT_TRACE(rtlpriv, COMP_ERR, DBG_DMESG,
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
					entry_id = rtl_cam_get_free_entry(hw,
								     p_macaddr);
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

void rtl92ee_read_bt_coexist_info_from_hwpg(struct ieee80211_hw *hw,
					    bool auto_load_fail, u8 *hwinfo)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 value;

	if (!auto_load_fail) {
		value = hwinfo[EEPROM_RF_BOARD_OPTION_92E];
		if (((value & 0xe0) >> 5) == 0x1)
			rtlpriv->btcoexist.btc_info.btcoexist = 1;
		else
			rtlpriv->btcoexist.btc_info.btcoexist = 0;

		rtlpriv->btcoexist.btc_info.bt_type = BT_RTL8192E;
		rtlpriv->btcoexist.btc_info.ant_num = ANT_TOTAL_X2;
	} else {
		rtlpriv->btcoexist.btc_info.btcoexist = 1;
		rtlpriv->btcoexist.btc_info.bt_type = BT_RTL8192E;
		rtlpriv->btcoexist.btc_info.ant_num = ANT_TOTAL_X1;
	}
}

void rtl92ee_bt_reg_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	/* 0:Low, 1:High, 2:From Efuse. */
	rtlpriv->btcoexist.reg_bt_iso = 2;
	/* 0:Idle, 1:None-SCO, 2:SCO, 3:From Counter. */
	rtlpriv->btcoexist.reg_bt_sco = 3;
	/* 0:Disable BT control A-MPDU, 1:Enable BT control A-MPDU. */
	rtlpriv->btcoexist.reg_bt_sco = 0;
}

void rtl92ee_bt_hw_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (rtlpriv->cfg->ops->get_btc_status())
		rtlpriv->btcoexist.btc_ops->btc_init_hw_config(rtlpriv);
}

void rtl92ee_suspend(struct ieee80211_hw *hw)
{
}

void rtl92ee_resume(struct ieee80211_hw *hw)
{
}

/* Turn on AAP (RCR:bit 0) for promicuous mode. */
void rtl92ee_allow_all_destaddr(struct ieee80211_hw *hw,
				bool allow_all_da, bool write_into_reg)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	if (allow_all_da)	/* Set BIT0 */
		rtlpci->receive_config |= RCR_AAP;
	else			/* Clear BIT0 */
		rtlpci->receive_config &= ~RCR_AAP;

	if (write_into_reg)
		rtl_write_dword(rtlpriv, REG_RCR, rtlpci->receive_config);

	RT_TRACE(rtlpriv, COMP_TURBO | COMP_INIT, DBG_LOUD,
		 "receive_config=0x%08X, write_into_reg=%d\n",
		  rtlpci->receive_config, write_into_reg);
}
