// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2016  Realtek Corporation.
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
#include "fw.h"
#include "led.h"
#include "hw.h"

#define LLT_CONFIG	5

u8 rtl_channel5g[CHANNEL_MAX_NUMBER_5G] = {
	36,  38,  40,  42,  44,  46,  48, /* Band 1 */
	52,  54,  56,  58,  60,  62,  64, /* Band 2 */
	100, 102, 104, 106, 108, 110, 112, /* Band 3 */
	116, 118, 120, 122, 124, 126, 128, /* Band 3 */
	132, 134, 136, 138, 140, 142, 144, /* Band 3 */
	149, 151, 153, 155, 157, 159, 161, /* Band 4 */
	165, 167, 169, 171, 173, 175, 177}; /* Band 4 */
u8 rtl_channel5g_80m[CHANNEL_MAX_NUMBER_5G_80M] = {42,  58,  106, 122,
						   138, 155, 171};

static void _rtl8822be_set_bcn_ctrl_reg(struct ieee80211_hw *hw, u8 set_bits,
					u8 clear_bits)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpci->reg_bcn_ctrl_val |= set_bits;
	rtlpci->reg_bcn_ctrl_val &= ~clear_bits;

	rtl_write_byte(rtlpriv, REG_BCN_CTRL_8822B,
		       (u8)rtlpci->reg_bcn_ctrl_val);
}

static void _rtl8822be_stop_tx_beacon(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 tmp;

	tmp = rtl_read_byte(rtlpriv, REG_FWHW_TXQ_CTRL_8822B + 2);
	rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL_8822B + 2, tmp & (~BIT(6)));
	rtl_write_byte(rtlpriv, REG_TBTT_PROHIBIT_8822B + 1, 0x64);
	tmp = rtl_read_byte(rtlpriv, REG_TBTT_PROHIBIT_8822B + 2);
	tmp &= ~(BIT(0));
	rtl_write_byte(rtlpriv, REG_TBTT_PROHIBIT_8822B + 2, tmp);
}

static void _rtl8822be_resume_tx_beacon(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 tmp;

	tmp = rtl_read_byte(rtlpriv, REG_FWHW_TXQ_CTRL_8822B + 2);
	rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL_8822B + 2, tmp | BIT(6));
	rtl_write_byte(rtlpriv, REG_TBTT_PROHIBIT_8822B + 1, 0xff);
	tmp = rtl_read_byte(rtlpriv, REG_TBTT_PROHIBIT_8822B + 2);
	tmp |= BIT(0);
	rtl_write_byte(rtlpriv, REG_TBTT_PROHIBIT_8822B + 2, tmp);
}

static void _rtl8822be_enable_bcn_sub_func(struct ieee80211_hw *hw)
{
	_rtl8822be_set_bcn_ctrl_reg(hw, 0, BIT(1));
}

static void _rtl8822be_disable_bcn_sub_func(struct ieee80211_hw *hw)
{
	_rtl8822be_set_bcn_ctrl_reg(hw, BIT(1), 0);
}

static void _rtl8822be_set_fw_clock_on(struct ieee80211_hw *hw, u8 rpwm_val,
				       bool b_need_turn_off_ckk)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u32 count = 0, isr_regaddr, content;
	bool b_schedule_timer = b_need_turn_off_ckk;

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

	if (IS_IN_LOW_POWER_STATE_8822B(rtlhal->fw_ps_state)) {
		rtlpriv->cfg->ops->get_hw_reg(hw, HW_VAR_SET_RPWM,
					      (u8 *)(&rpwm_val));
		if (FW_PS_IS_ACK(rpwm_val)) {
			isr_regaddr = REG_HISR0_8822B;
			content = rtl_read_dword(rtlpriv, isr_regaddr);
			while (!(content & IMR_CPWM) && (count < 500)) {
				udelay(50);
				count++;
				content = rtl_read_dword(rtlpriv, isr_regaddr);
			}

			if (content & IMR_CPWM) {
				rtl_write_word(rtlpriv, isr_regaddr, 0x0100);
				rtlhal->fw_ps_state = FW_PS_STATE_RF_ON_8822B;
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

	} else {
		spin_lock_bh(&rtlpriv->locks.fw_ps_lock);
		rtlhal->fw_clk_change_in_progress = false;
		spin_unlock_bh(&rtlpriv->locks.fw_ps_lock);
	}
}

static void _rtl8822be_set_fw_clock_off(struct ieee80211_hw *hw, u8 rpwm_val)
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
			rtl_write_word(rtlpriv, REG_HISR0_8822B, 0x0100);
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

static void _rtl8822be_set_fw_ps_rf_on(struct ieee80211_hw *hw)
{
	u8 rpwm_val = 0;

	rpwm_val |= (FW_PS_STATE_RF_OFF_8822B | FW_PS_ACK);
	_rtl8822be_set_fw_clock_on(hw, rpwm_val, true);
}

static void _rtl8822be_set_fw_ps_rf_off_low_power(struct ieee80211_hw *hw)
{
	u8 rpwm_val = 0;

	rpwm_val |= FW_PS_STATE_RF_OFF_LOW_PWR;
	_rtl8822be_set_fw_clock_off(hw, rpwm_val);
}

void rtl8822be_fw_clk_off_timer_callback(unsigned long data)
{
	struct ieee80211_hw *hw = (struct ieee80211_hw *)data;

	_rtl8822be_set_fw_ps_rf_off_low_power(hw);
}

static void _rtl8822be_fwlps_leave(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	bool fw_current_inps = false;
	u8 rpwm_val = 0, fw_pwrmode = FW_PS_ACTIVE_MODE;

	if (ppsc->low_power_enable) {
		rpwm_val = (FW_PS_STATE_ALL_ON_8822B | FW_PS_ACK); /* RF on */
		_rtl8822be_set_fw_clock_on(hw, rpwm_val, false);
		rtlhal->allow_sw_to_change_hwclc = false;
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_H2C_FW_PWRMODE,
					      (u8 *)(&fw_pwrmode));
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_FW_PSMODE_STATUS,
					      (u8 *)(&fw_current_inps));
	} else {
		rpwm_val = FW_PS_STATE_ALL_ON_8822B; /* RF on */
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_SET_RPWM,
					      (u8 *)(&rpwm_val));
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_H2C_FW_PWRMODE,
					      (u8 *)(&fw_pwrmode));
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_FW_PSMODE_STATUS,
					      (u8 *)(&fw_current_inps));
	}
}

static void _rtl8822be_fwlps_enter(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	bool fw_current_inps = true;
	u8 rpwm_val;

	if (ppsc->low_power_enable) {
		rpwm_val = FW_PS_STATE_RF_OFF_LOW_PWR; /* RF off */
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_FW_PSMODE_STATUS,
					      (u8 *)(&fw_current_inps));
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_H2C_FW_PWRMODE,
					      (u8 *)(&ppsc->fwctrl_psmode));
		rtlhal->allow_sw_to_change_hwclc = true;
		_rtl8822be_set_fw_clock_off(hw, rpwm_val);
	} else {
		rpwm_val = FW_PS_STATE_RF_OFF_8822B; /* RF off */
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_FW_PSMODE_STATUS,
					      (u8 *)(&fw_current_inps));
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_H2C_FW_PWRMODE,
					      (u8 *)(&ppsc->fwctrl_psmode));
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_SET_RPWM,
					      (u8 *)(&rpwm_val));
	}
}

void rtl8822be_get_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val)
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
	case HW_VAR_FWLPS_RF_ON: {
		enum rf_pwrstate rf_state;
		u32 val_rcr;

		rtlpriv->cfg->ops->get_hw_reg(hw, HW_VAR_RF_STATE,
					      (u8 *)(&rf_state));
		if (rf_state == ERFOFF) {
			*((bool *)(val)) = true;
		} else {
			val_rcr = rtl_read_dword(rtlpriv, REG_RCR_8822B);
			val_rcr &= 0x00070000;
			if (val_rcr)
				*((bool *)(val)) = false;
			else
				*((bool *)(val)) = true;
		}
	} break;
	case HW_VAR_FW_PSMODE_STATUS:
		*((bool *)(val)) = ppsc->fw_current_inpsmode;
		break;
	case HW_VAR_CORRECT_TSF: {
		u64 tsf;
		u32 *ptsf_low = (u32 *)&tsf;
		u32 *ptsf_high = ((u32 *)&tsf) + 1;

		*ptsf_high = rtl_read_dword(rtlpriv, (REG_TSFTR_8822B + 4));
		*ptsf_low = rtl_read_dword(rtlpriv, REG_TSFTR_8822B);

		*((u64 *)(val)) = tsf;

	} break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_DMESG,
			 "switch case not process %x\n", variable);
		break;
	}
}

static void _rtl8822be_download_rsvd_page(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 tmp_regcr, tmp_reg422;
	u8 bcnvalid_reg /*, txbc_reg*/;
	u8 count = 0, dlbcn_count = 0;
	bool b_recover = false;

	/*Set REG_CR_8822B bit 8. DMA beacon by SW.*/
	tmp_regcr = rtl_read_byte(rtlpriv, REG_CR_8822B + 1);
	rtl_write_byte(rtlpriv, REG_CR_8822B + 1, tmp_regcr | BIT(0));

	/* Disable Hw protection for a time which revserd for Hw sending beacon.
	 * Fix download reserved page packet fail
	 * that access collision with the protection time.
	 * 2010.05.11. Added by tynli.
	 */
	_rtl8822be_set_bcn_ctrl_reg(hw, 0, BIT(3));
	_rtl8822be_set_bcn_ctrl_reg(hw, BIT(4), 0);

	/* Set FWHW_TXQ_CTRL 0x422[6]=0 to
	 * tell Hw the packet is not a real beacon frame.
	 */
	tmp_reg422 = rtl_read_byte(rtlpriv, REG_FWHW_TXQ_CTRL_8822B + 2);
	rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL_8822B + 2,
		       tmp_reg422 & (~BIT(6)));

	if (tmp_reg422 & BIT(6))
		b_recover = true;

	do {
		/* Clear beacon valid check bit */
		bcnvalid_reg =
			rtl_read_byte(rtlpriv, REG_FIFOPAGE_CTRL_2_8822B + 1);
		bcnvalid_reg = bcnvalid_reg | BIT(7);
		rtl_write_byte(rtlpriv, REG_FIFOPAGE_CTRL_2_8822B + 1,
			       bcnvalid_reg);

		/* download rsvd page */
		rtl8822be_set_fw_rsvdpagepkt(hw, false);

		/* check rsvd page download OK. */
		bcnvalid_reg =
			rtl_read_byte(rtlpriv, REG_FIFOPAGE_CTRL_2_8822B + 1);

		count = 0;
		while (!(BIT(7) & bcnvalid_reg) && count < 20) {
			count++;
			udelay(50);
			bcnvalid_reg = rtl_read_byte(
				rtlpriv, REG_FIFOPAGE_CTRL_2_8822B + 1);
		}

		dlbcn_count++;
	} while (!(BIT(7) & bcnvalid_reg) && dlbcn_count < 5);

	if (!(BIT(7) & bcnvalid_reg))
		RT_TRACE(rtlpriv, COMP_INIT, DBG_WARNING,
			 "Download RSVD page failed!\n");

	/* Enable Bcn */
	_rtl8822be_set_bcn_ctrl_reg(hw, BIT(3), 0);
	_rtl8822be_set_bcn_ctrl_reg(hw, 0, BIT(4));

	if (b_recover)
		rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL_8822B + 2,
			       tmp_reg422);
}

void rtl8822be_set_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_efuse *efuse = rtl_efuse(rtl_priv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));

	switch (variable) {
	case HW_VAR_ETHER_ADDR:
		rtlpriv->halmac.ops->halmac_set_mac_address(rtlpriv, 0, val);
		break;
	case HW_VAR_BASIC_RATE: {
		u16 b_rate_cfg = ((u16 *)val)[0];

		b_rate_cfg = b_rate_cfg & 0x15f;
		b_rate_cfg |= 0x01;
		b_rate_cfg = (b_rate_cfg | 0xd) & (~BIT(1));
		rtl_write_byte(rtlpriv, REG_RRSR_8822B, b_rate_cfg & 0xff);
		rtl_write_byte(rtlpriv, REG_RRSR_8822B + 1,
			       (b_rate_cfg >> 8) & 0xff);
	} break;
	case HW_VAR_BSSID:
		rtlpriv->halmac.ops->halmac_set_bssid(rtlpriv, 0, val);
		break;
	case HW_VAR_SIFS:
		rtl_write_byte(rtlpriv, REG_SIFS_8822B + 1, val[0]);
		rtl_write_byte(rtlpriv, REG_SIFS_TRX_8822B + 1, val[1]);

		rtl_write_byte(rtlpriv, REG_SPEC_SIFS_8822B + 1, val[0]);
		rtl_write_byte(rtlpriv, REG_MAC_SPEC_SIFS_8822B + 1, val[0]);

		if (!mac->ht_enable)
			rtl_write_word(rtlpriv, REG_RESP_SIFS_OFDM_8822B,
				       0x0e0e);
		else
			rtl_write_word(rtlpriv, REG_RESP_SIFS_OFDM_8822B,
				       *((u16 *)val));
		break;
	case HW_VAR_SLOT_TIME: {
		u8 e_aci;

		RT_TRACE(rtlpriv, COMP_MLME, DBG_TRACE, "HW_VAR_SLOT_TIME %x\n",
			 val[0]);

		rtl_write_byte(rtlpriv, REG_SLOT_8822B, val[0]);

		for (e_aci = 0; e_aci < AC_MAX; e_aci++) {
			rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_AC_PARAM,
						      (u8 *)(&e_aci));
		}
	} break;
	case HW_VAR_ACK_PREAMBLE: {
		u8 reg_tmp;
		u8 short_preamble = (bool)(*(u8 *)val);

		reg_tmp = (rtlpriv->mac80211.cur_40_prime_sc) << 5;
		if (short_preamble)
			reg_tmp |= 0x80;
		rtl_write_byte(rtlpriv, REG_RRSR_8822B + 2, reg_tmp);
		rtlpriv->mac80211.short_preamble = short_preamble;
	} break;
	case HW_VAR_WPA_CONFIG:
		rtl_write_byte(rtlpriv, REG_SECCFG_8822B, *((u8 *)val));
		break;
	case HW_VAR_AMPDU_FACTOR: {
		u32 ampdu_len = (*((u8 *)val));

		ampdu_len = (0x2000 << ampdu_len) - 1;
		rtl_write_dword(rtlpriv, REG_AMPDU_MAX_LENGTH_8822B, ampdu_len);
	} break;
	case HW_VAR_AC_PARAM: {
		u8 e_aci = *((u8 *)val);

		if (mac->vif && mac->vif->bss_conf.assoc && !mac->act_scanning)
			rtl8822be_set_qos(hw, e_aci);

		if (rtlpci->acm_method != EACMWAY2_SW)
			rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_ACM_CTRL,
						      (u8 *)(&e_aci));
	} break;
	case HW_VAR_ACM_CTRL: {
		u8 e_aci = *((u8 *)val);
		union aci_aifsn *aifs = (union aci_aifsn *)&mac->ac[0].aifs;

		u8 acm = aifs->f.acm;
		u8 acm_ctrl = rtl_read_byte(rtlpriv, REG_ACMHWCTRL_8822B);

		acm_ctrl = acm_ctrl | ((rtlpci->acm_method == 2) ? 0x0 : 0x1);

		if (acm) {
			switch (e_aci) {
			case AC0_BE:
				acm_ctrl |= ACMHW_BEQ_EN;
				break;
			case AC2_VI:
				acm_ctrl |= ACMHW_VIQ_EN;
				break;
			case AC3_VO:
				acm_ctrl |= ACMHW_VOQ_EN;
				break;
			default:
				RT_TRACE(
					rtlpriv, COMP_ERR, DBG_WARNING,
					"HW_VAR_ACM_CTRL acm set failed: eACI is %d\n",
					acm);
				break;
			}
		} else {
			switch (e_aci) {
			case AC0_BE:
				acm_ctrl &= (~ACMHW_BEQ_EN);
				break;
			case AC2_VI:
				acm_ctrl &= (~ACMHW_VIQ_EN);
				break;
			case AC3_VO:
				acm_ctrl &= (~ACMHW_VOQ_EN);
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
		rtl_write_byte(rtlpriv, REG_ACMHWCTRL_8822B, acm_ctrl);
	} break;
	case HW_VAR_RCR: {
		rtl_write_dword(rtlpriv, REG_RCR_8822B, ((u32 *)(val))[0]);
		rtlpci->receive_config = ((u32 *)(val))[0];
	} break;
	case HW_VAR_RETRY_LIMIT: {
		u8 retry_limit = ((u8 *)(val))[0];

		rtl_write_word(rtlpriv, REG_RETRY_LIMIT_8822B,
			       retry_limit << RETRY_LIMIT_SHORT_SHIFT |
				       retry_limit << RETRY_LIMIT_LONG_SHIFT);
	} break;
	case HW_VAR_DUAL_TSF_RST:
		rtl_write_byte(rtlpriv, REG_DUAL_TSF_RST_8822B,
			       (BIT(0) | BIT(1)));
		break;
	case HW_VAR_EFUSE_BYTES:
		efuse->efuse_usedbytes = *((u16 *)val);
		break;
	case HW_VAR_EFUSE_USAGE:
		efuse->efuse_usedpercentage = *((u8 *)val);
		break;
	case HW_VAR_IO_CMD:
		rtl8822be_phy_set_io_cmd(hw, (*(enum io_type *)val));
		break;
	case HW_VAR_SET_RPWM:
		break;
	case HW_VAR_H2C_FW_PWRMODE:
		rtl8822be_set_fw_pwrmode_cmd(hw, (*(u8 *)val));
		break;
	case HW_VAR_FW_PSMODE_STATUS:
		ppsc->fw_current_inpsmode = *((bool *)val);
		break;
	case HW_VAR_RESUME_CLK_ON:
		_rtl8822be_set_fw_ps_rf_on(hw);
		break;
	case HW_VAR_FW_LPS_ACTION: {
		bool b_enter_fwlps = *((bool *)val);

		if (b_enter_fwlps)
			_rtl8822be_fwlps_enter(hw);
		else
			_rtl8822be_fwlps_leave(hw);
	} break;
	case HW_VAR_H2C_FW_JOINBSSRPT: {
		u8 mstatus = (*(u8 *)val);

		if (mstatus == RT_MEDIA_CONNECT) {
			rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_AID, NULL);
			_rtl8822be_download_rsvd_page(hw);
		}
		rtl8822be_set_default_port_id_cmd(hw);
		rtl8822be_set_fw_media_status_rpt_cmd(hw, mstatus);
	} break;
	case HW_VAR_H2C_FW_P2P_PS_OFFLOAD:
		rtl8822be_set_p2p_ps_offload_cmd(hw, (*(u8 *)val));
		break;
	case HW_VAR_AID: {
		u16 u2btmp;

		u2btmp = rtl_read_word(rtlpriv, REG_BCN_PSR_RPT_8822B);
		u2btmp &= 0xC000;
		rtl_write_word(rtlpriv, REG_BCN_PSR_RPT_8822B,
			       (u2btmp | mac->assoc_id));
	} break;
	case HW_VAR_CORRECT_TSF: {
		u8 btype_ibss = ((u8 *)(val))[0];

		if (btype_ibss)
			_rtl8822be_stop_tx_beacon(hw);

		_rtl8822be_set_bcn_ctrl_reg(hw, 0, BIT(3));

		rtl_write_dword(rtlpriv, REG_TSFTR_8822B,
				(u32)(mac->tsf & 0xffffffff));
		rtl_write_dword(rtlpriv, REG_TSFTR_8822B + 4,
				(u32)((mac->tsf >> 32) & 0xffffffff));

		_rtl8822be_set_bcn_ctrl_reg(hw, BIT(3), 0);

		if (btype_ibss)
			_rtl8822be_resume_tx_beacon(hw);
	} break;
	case HW_VAR_KEEP_ALIVE: {
		u8 array[2];

		array[0] = 0xff;
		array[1] = *((u8 *)val);
		rtl8822be_fill_h2c_cmd(hw, H2C_8822B_KEEP_ALIVE_CTRL, 2, array);
	} break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_DMESG,
			 "switch case not process %x\n", variable);
		break;
	}
}

static void _rtl8822be_gen_refresh_led_state(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_led *led0 = &pcipriv->ledctl.sw_led0;

	if (rtlpriv->rtlhal.up_first_time)
		return;

	if (ppsc->rfoff_reason == RF_CHANGE_BY_IPS)
		rtl8822be_sw_led_on(hw, led0);
	else if (ppsc->rfoff_reason == RF_CHANGE_BY_INIT)
		rtl8822be_sw_led_on(hw, led0);
	else
		rtl8822be_sw_led_off(hw, led0);
}

static bool _rtl8822be_init_trxbd(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	/*struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));*/

	u8 bytetmp;
	/*u16 wordtmp;*/
	u32 dwordtmp;

	/* Set TX/RX descriptor physical address -- HI part */
	if (!rtlpriv->cfg->mod_params->dma64)
		goto dma64_end;

	rtl_write_dword(rtlpriv, REG_H2CQ_TXBD_DESA_8822B + 4,
			((u64)rtlpci->tx_ring[H2C_QUEUE].buffer_desc_dma) >>
				32);
	rtl_write_dword(rtlpriv, REG_BCNQ_TXBD_DESA_8822B + 4,
			((u64)rtlpci->tx_ring[BEACON_QUEUE].buffer_desc_dma) >>
				32);
	rtl_write_dword(rtlpriv, REG_MGQ_TXBD_DESA_8822B + 4,
			(u64)rtlpci->tx_ring[MGNT_QUEUE].buffer_desc_dma >> 32);
	rtl_write_dword(rtlpriv, REG_VOQ_TXBD_DESA_8822B + 4,
			(u64)rtlpci->tx_ring[VO_QUEUE].buffer_desc_dma >> 32);
	rtl_write_dword(rtlpriv, REG_VIQ_TXBD_DESA_8822B + 4,
			(u64)rtlpci->tx_ring[VI_QUEUE].buffer_desc_dma >> 32);
	rtl_write_dword(rtlpriv, REG_BEQ_TXBD_DESA_8822B + 4,
			(u64)rtlpci->tx_ring[BE_QUEUE].buffer_desc_dma >> 32);
	rtl_write_dword(rtlpriv, REG_BKQ_TXBD_DESA_8822B + 4,
			(u64)rtlpci->tx_ring[BK_QUEUE].buffer_desc_dma >> 32);
	rtl_write_dword(rtlpriv, REG_HI0Q_TXBD_DESA_8822B + 4,
			(u64)rtlpci->tx_ring[HIGH_QUEUE].buffer_desc_dma >> 32);

	rtl_write_dword(rtlpriv, REG_RXQ_RXBD_DESA_8822B + 4,
			(u64)rtlpci->rx_ring[RX_MPDU_QUEUE].dma >> 32);

dma64_end:
	/* Set TX/RX descriptor physical address(from OS API). */
	rtl_write_dword(rtlpriv, REG_H2CQ_TXBD_DESA_8822B,
			((u64)rtlpci->tx_ring[H2C_QUEUE].buffer_desc_dma) &
				DMA_BIT_MASK(32));
	rtl_write_dword(rtlpriv, REG_BCNQ_TXBD_DESA_8822B,
			((u64)rtlpci->tx_ring[BEACON_QUEUE].buffer_desc_dma) &
				DMA_BIT_MASK(32));
	rtl_write_dword(rtlpriv, REG_MGQ_TXBD_DESA_8822B,
			(u64)rtlpci->tx_ring[MGNT_QUEUE].buffer_desc_dma &
				DMA_BIT_MASK(32));
	rtl_write_dword(rtlpriv, REG_VOQ_TXBD_DESA_8822B,
			(u64)rtlpci->tx_ring[VO_QUEUE].buffer_desc_dma &
				DMA_BIT_MASK(32));
	rtl_write_dword(rtlpriv, REG_VIQ_TXBD_DESA_8822B,
			(u64)rtlpci->tx_ring[VI_QUEUE].buffer_desc_dma &
				DMA_BIT_MASK(32));
	rtl_write_dword(rtlpriv, REG_BEQ_TXBD_DESA_8822B,
			(u64)rtlpci->tx_ring[BE_QUEUE].buffer_desc_dma &
				DMA_BIT_MASK(32));
	dwordtmp = rtl_read_dword(rtlpriv, REG_BEQ_TXBD_DESA_8822B); /* need? */
	rtl_write_dword(rtlpriv, REG_BKQ_TXBD_DESA_8822B,
			(u64)rtlpci->tx_ring[BK_QUEUE].buffer_desc_dma &
				DMA_BIT_MASK(32));
	rtl_write_dword(rtlpriv, REG_HI0Q_TXBD_DESA_8822B,
			(u64)rtlpci->tx_ring[HIGH_QUEUE].buffer_desc_dma &
				DMA_BIT_MASK(32));

	rtl_write_dword(rtlpriv, REG_RXQ_RXBD_DESA_8822B,
			(u64)rtlpci->rx_ring[RX_MPDU_QUEUE].dma &
				DMA_BIT_MASK(32));

	/* Reset R/W point */
	rtl_write_dword(rtlpriv, REG_BD_RWPTR_CLR_8822B, 0x3fffffff);

	/* Reset the H2CQ R/W point index to 0 */
	dwordtmp = rtl_read_dword(rtlpriv, REG_H2CQ_CSR_8822B);
	rtl_write_dword(rtlpriv, REG_H2CQ_CSR_8822B,
			(dwordtmp | BIT(8) | BIT(16)));

	bytetmp = rtl_read_byte(rtlpriv, REG_PCIE_CTRL_8822B + 3);
	rtl_write_byte(rtlpriv, REG_PCIE_CTRL_8822B + 3, bytetmp | 0xF7);

	rtl_write_dword(rtlpriv, REG_INT_MIG_8822B, 0);

	rtl_write_dword(rtlpriv, REG_MCUTST_I_8822B, 0x0);

	rtl_write_word(rtlpriv, REG_H2CQ_TXBD_NUM_8822B,
		       TX_DESC_NUM_8822B |
			       ((RTL8822BE_SEG_NUM << 12) & 0x3000));
	rtl_write_word(rtlpriv, REG_MGQ_TXBD_NUM_8822B,
		       TX_DESC_NUM_8822B |
			       ((RTL8822BE_SEG_NUM << 12) & 0x3000));
	rtl_write_word(rtlpriv, REG_VOQ_TXBD_NUM_8822B,
		       TX_DESC_NUM_8822B |
			       ((RTL8822BE_SEG_NUM << 12) & 0x3000));
	rtl_write_word(rtlpriv, REG_VIQ_TXBD_NUM_8822B,
		       TX_DESC_NUM_8822B |
			       ((RTL8822BE_SEG_NUM << 12) & 0x3000));
	rtl_write_word(rtlpriv, REG_BEQ_TXBD_NUM_8822B,
		       TX_DESC_NUM_8822B |
			       ((RTL8822BE_SEG_NUM << 12) & 0x3000));
	rtl_write_word(rtlpriv, REG_VOQ_TXBD_NUM_8822B,
		       TX_DESC_NUM_8822B |
			       ((RTL8822BE_SEG_NUM << 12) & 0x3000));
	rtl_write_word(rtlpriv, REG_BKQ_TXBD_NUM_8822B,
		       TX_DESC_NUM_8822B |
			       ((RTL8822BE_SEG_NUM << 12) & 0x3000));
	rtl_write_word(rtlpriv, REG_HI0Q_TXBD_NUM_8822B,
		       TX_DESC_NUM_8822B |
			       ((RTL8822BE_SEG_NUM << 12) & 0x3000));
	rtl_write_word(rtlpriv, REG_HI1Q_TXBD_NUM_8822B,
		       TX_DESC_NUM_8822B |
			       ((RTL8822BE_SEG_NUM << 12) & 0x3000));
	rtl_write_word(rtlpriv, REG_HI2Q_TXBD_NUM_8822B,
		       TX_DESC_NUM_8822B |
			       ((RTL8822BE_SEG_NUM << 12) & 0x3000));
	rtl_write_word(rtlpriv, REG_HI3Q_TXBD_NUM_8822B,
		       TX_DESC_NUM_8822B |
			       ((RTL8822BE_SEG_NUM << 12) & 0x3000));
	rtl_write_word(rtlpriv, REG_HI4Q_TXBD_NUM_8822B,
		       TX_DESC_NUM_8822B |
			       ((RTL8822BE_SEG_NUM << 12) & 0x3000));
	rtl_write_word(rtlpriv, REG_HI5Q_TXBD_NUM_8822B,
		       TX_DESC_NUM_8822B |
			       ((RTL8822BE_SEG_NUM << 12) & 0x3000));
	rtl_write_word(rtlpriv, REG_HI6Q_TXBD_NUM_8822B,
		       TX_DESC_NUM_8822B |
			       ((RTL8822BE_SEG_NUM << 12) & 0x3000));
	rtl_write_word(rtlpriv, REG_HI7Q_TXBD_NUM_8822B,
		       TX_DESC_NUM_8822B |
			       ((RTL8822BE_SEG_NUM << 12) & 0x3000));
	/*Rx*/
	rtl_write_word(rtlpriv, REG_RX_RXBD_NUM_8822B,
		       RX_DESC_NUM_8822BE |
		       ((RTL8822BE_SEG_NUM << 13) & 0x6000) | 0x8000);

	rtl_write_dword(rtlpriv, REG_BD_RWPTR_CLR_8822B, 0XFFFFFFFF);

	_rtl8822be_gen_refresh_led_state(hw);

	return true;
}

static void _rtl8822be_enable_aspm_back_door(struct ieee80211_hw *hw)
{
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u8 tmp;

	if (!ppsc->support_backdoor)
		return;

	pci_read_config_byte(rtlpci->pdev, 0x70f, &tmp);
	pci_write_config_byte(rtlpci->pdev, 0x70f, tmp | ASPM_L1_LATENCY << 3);

	pci_read_config_byte(rtlpci->pdev, 0x719, &tmp);
	pci_write_config_byte(rtlpci->pdev, 0x719, tmp | BIT(3) | BIT(4));
}

void rtl8822be_enable_hw_security_config(struct ieee80211_hw *hw)
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

	sec_reg_value = SCR_TX_ENC_ENABLE | SRC_RX_DEC_ENABLE;

	if (rtlpriv->sec.use_defaultkey) {
		sec_reg_value |= SCR_TX_USE_DK;
		sec_reg_value |= SCR_RX_USE_DK;
	}

	sec_reg_value |= (SCR_RXBCUSEDK | SCR_TXBCUSEDK);

	tmp = rtl_read_byte(rtlpriv, REG_CR_8822B + 1);
	rtl_write_byte(rtlpriv, REG_CR_8822B + 1, tmp | BIT(1));

	RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG, "The SECR-value %x\n",
		 sec_reg_value);

	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_WPA_CONFIG, &sec_reg_value);
}

static bool _rtl8822be_check_pcie_dma_hang(struct rtl_priv *rtlpriv)
{
	u8 tmp;

	/* write reg 0x350 Bit[26]=1. Enable debug port. */
	tmp = rtl_read_byte(rtlpriv, REG_DBI_FLAG_V1_8822B + 3);
	if (!(tmp & BIT(2))) {
		rtl_write_byte(rtlpriv, REG_DBI_FLAG_V1_8822B + 3,
			       (tmp | BIT(2)));
		mdelay(100); /* Suggested by DD Justin_tsai. */
	}

	/* read reg 0x350 Bit[25] if 1 : RX hang
	 * read reg 0x350 Bit[24] if 1 : TX hang
	 */
	tmp = rtl_read_byte(rtlpriv, REG_DBI_FLAG_V1_8822B + 3);
	if ((tmp & BIT(0)) || (tmp & BIT(1))) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "CheckPcieDMAHang8822BE(): true!!\n");
		return true;
	} else {
		return false;
	}
}

static void _rtl8822be_reset_pcie_interface_dma(struct rtl_priv *rtlpriv,
						bool mac_power_on)
{
	u8 tmp;
	bool release_mac_rx_pause;
	u8 backup_pcie_dma_pause;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "ResetPcieInterfaceDMA8822BE()\n");

	/* Revise Note: Follow the document "PCIe RX DMA Hang Reset Flow_v03"
	 * released by SD1 Alan.
	 * 2013.05.07, by tynli.
	 */

	/* 1. disable register write lock
	 *	write 0x1C bit[1:0] = 2'h0
	 *	write 0xCC bit[2] = 1'b1
	 */
	tmp = rtl_read_byte(rtlpriv, REG_RSV_CTRL_8822B);
	tmp &= ~(BIT(1) | BIT(0));
	rtl_write_byte(rtlpriv, REG_RSV_CTRL_8822B, tmp);
	tmp = rtl_read_byte(rtlpriv, REG_PMC_DBG_CTRL2_8822B);
	tmp |= BIT(2);
	rtl_write_byte(rtlpriv, REG_PMC_DBG_CTRL2_8822B, tmp);

	/* 2. Check and pause TRX DMA
	 *	write 0x284 bit[18] = 1'b1
	 *	write 0x301 = 0xFF
	 */
	tmp = rtl_read_byte(rtlpriv, REG_RXDMA_CONTROL_8822B);
	if (tmp & BIT(2)) {
		/* Already pause before the function for another purpose. */
		release_mac_rx_pause = false;
	} else {
		rtl_write_byte(rtlpriv, REG_RXDMA_CONTROL_8822B,
			       (tmp | BIT(2)));
		release_mac_rx_pause = true;
	}

	backup_pcie_dma_pause = rtl_read_byte(rtlpriv, REG_PCIE_CTRL_8822B + 1);
	if (backup_pcie_dma_pause != 0xFF)
		rtl_write_byte(rtlpriv, REG_PCIE_CTRL_8822B + 1, 0xFF);

	if (mac_power_on) {
		/* 3. reset TRX function
		 *	write 0x100 = 0x00
		 */
		rtl_write_byte(rtlpriv, REG_CR_8822B, 0);
	}

	/* 4. Reset PCIe DMA
	 *	write 0x003 bit[0] = 0
	 */
	tmp = rtl_read_byte(rtlpriv, REG_SYS_FUNC_EN_8822B + 1);
	tmp &= ~(BIT(0));
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN_8822B + 1, tmp);

	/* 5. Enable PCIe DMA
	 *	write 0x003 bit[0] = 1
	 */
	tmp = rtl_read_byte(rtlpriv, REG_SYS_FUNC_EN_8822B + 1);
	tmp |= BIT(0);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN_8822B + 1, tmp);

	if (mac_power_on) {
		/* 6. enable TRX function
		 *	write 0x100 = 0xFF
		 */
		rtl_write_byte(rtlpriv, REG_CR_8822B, 0xFF);

		/* We should init LLT & RQPN and
		 * prepare Tx/Rx descrptor address later
		 * because MAC function is reset.
		 */
	}

	/* 7. Restore PCIe autoload down bit
	 *	write 0xF8 bit[17] = 1'b1
	 */
	tmp = rtl_read_byte(rtlpriv, REG_SYS_STATUS2_8822B + 2);
	tmp |= BIT(1);
	rtl_write_byte(rtlpriv, REG_SYS_STATUS2_8822B + 2, tmp);

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
			tmp = rtl_read_byte(rtlpriv, REG_RXDMA_CONTROL_8822B);
			rtl_write_byte(rtlpriv, REG_RXDMA_CONTROL_8822B,
				       (tmp & (~BIT(2))));
		}
		rtl_write_byte(rtlpriv, REG_PCIE_CTRL_8822B + 1,
			       backup_pcie_dma_pause);
	}

	/* 9. lock system register
	 *	write 0xCC bit[2] = 1'b0
	 */
	tmp = rtl_read_byte(rtlpriv, REG_PMC_DBG_CTRL2_8822B);
	tmp &= ~(BIT(2));
	rtl_write_byte(rtlpriv, REG_PMC_DBG_CTRL2_8822B, tmp);
}

int rtl8822be_hw_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	int err = 0;
	u8 tmp_u1b;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, " Rtl8822BE hw init\n");
	rtlpriv->rtlhal.being_init_adapter = true;
	rtlpriv->intf_ops->disable_aspm(hw);

	if (_rtl8822be_check_pcie_dma_hang(rtlpriv)) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "8822be dma hang!\n");
		_rtl8822be_reset_pcie_interface_dma(rtlpriv,
						    rtlhal->mac_func_enable);
		rtlhal->mac_func_enable = false;
	}

	/* init TRX BD */
	_rtl8822be_init_trxbd(hw);

	/* use halmac to init */
	err = rtlpriv->halmac.ops->halmac_init_hal(rtlpriv);
	if (err) {
		pr_err("halmac_init_hal failed\n");
		rtlhal->fw_ready = false;
		return err;
	}

	rtlhal->fw_ready = true;

	/* have to init after halmac init */
	tmp_u1b = rtl_read_byte(rtlpriv, REG_PCIE_CTRL_8822B + 2);
	rtl_write_byte(rtlpriv, REG_PCIE_CTRL_8822B + 2, (tmp_u1b | BIT(4)));

	/*rtl_write_word(rtlpriv, REG_PCIE_CTRL_8822B, 0x8000);*/
	rtlhal->rx_tag = 0;

	rtl_write_byte(rtlpriv, REG_RX_DRVINFO_SZ_8822B, 0x4);

	/*fw related variable initialize */
	ppsc->fw_current_inpsmode = false;
	rtlhal->fw_ps_state = FW_PS_STATE_ALL_ON_8822B;
	rtlhal->fw_clk_change_in_progress = false;
	rtlhal->allow_sw_to_change_hwclc = false;
	rtlhal->last_hmeboxnum = 0;

	rtlphy->rfreg_chnlval[0] =
		rtl_get_rfreg(hw, RF90_PATH_A, RF_CHNLBW, RFREG_OFFSET_MASK);
	rtlphy->rfreg_chnlval[1] =
		rtl_get_rfreg(hw, RF90_PATH_B, RF_CHNLBW, RFREG_OFFSET_MASK);
	rtlphy->backup_rf_0x1a = (u32)rtl_get_rfreg(hw, RF90_PATH_A, RF_RX_G1,
						    RFREG_OFFSET_MASK);
	rtlphy->rfreg_chnlval[0] =
		(rtlphy->rfreg_chnlval[0] & 0xfffff3ff) | BIT(10) | BIT(11);

	rtlhal->mac_func_enable = true;

	if (rtlpriv->cfg->ops->get_btc_status())
		rtlpriv->btcoexist.btc_ops->btc_power_on_setting(rtlpriv);

	/* reset cam / set security */
	rtl_cam_reset_all_entry(hw);
	rtl8822be_enable_hw_security_config(hw);

	/* check RCR/ICV bit */
	rtlpci->receive_config &= ~(RCR_ACRC32 | RCR_AICV);
	rtl_write_dword(rtlpriv, REG_RCR_8822B, rtlpci->receive_config);

	/* clear rx ctrl frame */
	rtl_write_word(rtlpriv, REG_RXFLTMAP1_8822B, 0);

	ppsc->rfpwr_state = ERFON;

	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_ETHER_ADDR, mac->mac_addr);
	_rtl8822be_enable_aspm_back_door(hw);
	rtlpriv->intf_ops->enable_aspm(hw);

	if (rtlpriv->cfg->ops->get_btc_status())
		rtlpriv->btcoexist.btc_ops->btc_init_hw_config(rtlpriv);
	else
		rtlpriv->btcoexist.btc_ops->btc_init_hw_config_wifi_only(
								rtlpriv);

	rtlpriv->rtlhal.being_init_adapter = false;

	rtlpriv->phydm.ops->phydm_init_dm(rtlpriv);

	/* clear ISR, and IMR will be on later */
	rtl_write_dword(rtlpriv, REG_HISR0_8822B,
			rtl_read_dword(rtlpriv, REG_HISR0_8822B));

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "end of Rtl8822BE hw init %x\n",
		 err);
	return 0;
}

static u32 _rtl8822be_read_chip_version(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	/*enum version_8822b version = VERSION_UNKNOWN;*/
	u32 version;
	u32 value32;

	rtlphy->rf_type = RF_2T2R;

	value32 = rtl_read_dword(rtlpriv, REG_SYS_CFG1_8822B);

	version = value32;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "Chip RF Type: %s\n",
		 (rtlphy->rf_type == RF_2T2R) ? "RF_2T2R" : "RF_1T1R");

	return version;
}

static int _rtl8822be_set_media_status(struct ieee80211_hw *hw,
				       enum nl80211_iftype type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 bt_msr = rtl_read_byte(rtlpriv, MSR);
	enum led_ctl_mode ledaction = LED_CTL_NO_LINK;
	u8 mode = MSR_NOLINK;

	bt_msr &= 0xfc;

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
		_rtl8822be_stop_tx_beacon(hw);
		_rtl8822be_enable_bcn_sub_func(hw);
	} else if (mode == MSR_ADHOC || mode == MSR_AP) {
		_rtl8822be_resume_tx_beacon(hw);
		_rtl8822be_disable_bcn_sub_func(hw);
	} else {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 "Set HW_VAR_MEDIA_STATUS: No such media status(%x).\n",
			 mode);
	}

	rtl_write_byte(rtlpriv, (MSR), bt_msr | mode);
	rtlpriv->cfg->ops->led_control(hw, ledaction);
	if (mode == MSR_AP)
		rtl_write_byte(rtlpriv, REG_BCNTCFG_8822B + 1, 0x00);
	else
		rtl_write_byte(rtlpriv, REG_BCNTCFG_8822B + 1, 0x66);
	return 0;
}

void rtl8822be_set_check_bssid(struct ieee80211_hw *hw, bool check_bssid)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u32 reg_rcr = rtlpci->receive_config;

	if (rtlpriv->psc.rfpwr_state != ERFON)
		return;

	if (check_bssid) {
		reg_rcr |= (RCR_CBSSID_DATA | RCR_CBSSID_BCN);
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_RCR, (u8 *)(&reg_rcr));
		_rtl8822be_set_bcn_ctrl_reg(hw, 0, BIT(4));
	} else if (!check_bssid) {
		reg_rcr &= (~(RCR_CBSSID_DATA | RCR_CBSSID_BCN));
		_rtl8822be_set_bcn_ctrl_reg(hw, BIT(4), 0);
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_RCR, (u8 *)(&reg_rcr));
	}
}

int rtl8822be_set_network_type(struct ieee80211_hw *hw,
			       enum nl80211_iftype type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (_rtl8822be_set_media_status(hw, type))
		return -EOPNOTSUPP;

	if (rtlpriv->mac80211.link_state == MAC80211_LINKED) {
		if (type != NL80211_IFTYPE_AP &&
		    type != NL80211_IFTYPE_MESH_POINT)
			rtl8822be_set_check_bssid(hw, true);
	} else {
		rtl8822be_set_check_bssid(hw, false);
	}

	return 0;
}

void rtl8822be_set_qos(struct ieee80211_hw *hw, int aci)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtlpriv);
	u32 ac_param;

	ac_param = rtl_get_hal_edca_param(hw, mac->vif, mac->mode,
					  &mac->edca_param[aci]);

	switch (aci) {
	case AC1_BK:
		rtl_write_dword(rtlpriv, REG_EDCA_BK_PARAM_8822B, ac_param);
		break;
	case AC0_BE:
		rtl_write_dword(rtlpriv, REG_EDCA_BE_PARAM_8822B, ac_param);
		break;
	case AC2_VI:
		rtl_write_dword(rtlpriv, REG_EDCA_VI_PARAM_8822B, ac_param);
		break;
	case AC3_VO:
		rtl_write_dword(rtlpriv, REG_EDCA_VO_PARAM_8822B, ac_param);
		break;
	default:
		WARN_ONCE(true, "invalid aci: %d !\n", aci);
		break;
	}
}

void rtl8822be_enable_interrupt(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	rtl_write_dword(rtlpriv, REG_HIMR0_8822B,
			rtlpci->irq_mask[0] & 0xFFFFFFFF);
	rtl_write_dword(rtlpriv, REG_HIMR1_8822B,
			rtlpci->irq_mask[1] & 0xFFFFFFFF);
	rtl_write_dword(rtlpriv, REG_HIMR3_8822B,
			rtlpci->irq_mask[3] & 0xFFFFFFFF);
	rtlpci->irq_enabled = true;
}

void rtl8822be_disable_interrupt(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	rtl_write_dword(rtlpriv, REG_HIMR0_8822B, IMR_DISABLED);
	rtl_write_dword(rtlpriv, REG_HIMR1_8822B, IMR_DISABLED);
	rtl_write_dword(rtlpriv, REG_HIMR3_8822B, IMR_DISABLED);
	rtlpci->irq_enabled = false;
	/*synchronize_irq(rtlpci->pdev->irq);*/
}

void rtl8822be_card_disable(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	enum nl80211_iftype opmode;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "RTL8822be card disable\n");

	RT_SET_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC);

	mac->link_state = MAC80211_NOLINK;
	opmode = NL80211_IFTYPE_UNSPECIFIED;

	_rtl8822be_set_media_status(hw, opmode);

	if (rtlpriv->rtlhal.driver_is_goingto_unload ||
	    ppsc->rfoff_reason > RF_CHANGE_BY_PS)
		rtlpriv->cfg->ops->led_control(hw, LED_CTL_POWER_OFF);

	rtlpriv->phydm.ops->phydm_deinit_dm(rtlpriv);

	rtlpriv->halmac.ops->halmac_deinit_hal(rtlpriv);

	/* after power off we should do iqk again */
	if (!rtlpriv->cfg->ops->get_btc_status())
		rtlpriv->phy.iqk_initialized = false;
}

void rtl8822be_interrupt_recognized(struct ieee80211_hw *hw, u32 *p_inta,
				    u32 *p_intb, u32 *p_intc, u32 *p_intd)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	*p_inta =
		rtl_read_dword(rtlpriv, REG_HISR0_8822B) & rtlpci->irq_mask[0];
	rtl_write_dword(rtlpriv, REG_HISR0_8822B, *p_inta);

	*p_intb =
		rtl_read_dword(rtlpriv, REG_HISR1_8822B) & rtlpci->irq_mask[1];
	rtl_write_dword(rtlpriv, REG_HISR1_8822B, *p_intb);

	*p_intd =
		rtl_read_dword(rtlpriv, REG_HISR3_8822B) & rtlpci->irq_mask[3];
	rtl_write_dword(rtlpriv, REG_HISR3_8822B, *p_intd);
}

void rtl8822be_set_beacon_related_registers(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u16 bcn_interval, atim_window;

	bcn_interval = mac->beacon_interval;
	atim_window = 2; /*FIX MERGE */
	rtl8822be_disable_interrupt(hw);
	rtl_write_word(rtlpriv, REG_ATIMWND_8822B, atim_window);
	rtl_write_word(rtlpriv, REG_MBSSID_BCN_SPACE_8822B, bcn_interval);
	rtl_write_word(rtlpriv, REG_BCNTCFG_8822B, 0x660f);
	rtl_write_byte(rtlpriv, REG_RXTSF_OFFSET_CCK_8822B, 0x18);
	rtl_write_byte(rtlpriv, REG_RXTSF_OFFSET_OFDM_8822B, 0x18);
	rtl_write_byte(rtlpriv, 0x606, 0x30);
	rtlpci->reg_bcn_ctrl_val |= BIT(3);
	rtl_write_byte(rtlpriv, REG_BCN_CTRL_8822B,
		       (u8)rtlpci->reg_bcn_ctrl_val);
}

void rtl8822be_set_beacon_interval(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u16 bcn_interval = mac->beacon_interval;

	RT_TRACE(rtlpriv, COMP_BEACON, DBG_DMESG, "beacon_interval:%d\n",
		 bcn_interval);
	rtl_write_word(rtlpriv, REG_MBSSID_BCN_SPACE_8822B, bcn_interval);
}

void rtl8822be_update_interrupt_mask(struct ieee80211_hw *hw, u32 add_msr,
				     u32 rm_msr)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	RT_TRACE(rtlpriv, COMP_INTR, DBG_LOUD, "add_msr:%x, rm_msr:%x\n",
		 add_msr, rm_msr);

	if (add_msr)
		rtlpci->irq_mask[0] |= add_msr;
	if (rm_msr)
		rtlpci->irq_mask[0] &= (~rm_msr);
	rtl8822be_disable_interrupt(hw);
	rtl8822be_enable_interrupt(hw);
}

static bool _rtl8822be_get_chnl_group(u8 chnl, u8 *group)
{
	bool in_24g;

	if (chnl <= 14) {
		in_24g = true;

		if (chnl >= 1 && chnl <= 2)
			*group = 0;
		else if (chnl >= 3 && chnl <= 5)
			*group = 1;
		else if (chnl >= 6 && chnl <= 8)
			*group = 2;
		else if (chnl >= 9 && chnl <= 11)
			*group = 3;
		else if (chnl >= 12 && chnl <= 14)
			*group = 4;
	} else {
		in_24g = false;

		if (chnl >= 36 && chnl <= 42)
			*group = 0;
		else if (chnl >= 44 && chnl <= 48)
			*group = 1;
		else if (chnl >= 50 && chnl <= 58)
			*group = 2;
		else if (chnl >= 60 && chnl <= 64)
			*group = 3;
		else if (chnl >= 100 && chnl <= 106)
			*group = 4;
		else if (chnl >= 108 && chnl <= 114)
			*group = 5;
		else if (chnl >= 116 && chnl <= 122)
			*group = 6;
		else if (chnl >= 124 && chnl <= 130)
			*group = 7;
		else if (chnl >= 132 && chnl <= 138)
			*group = 8;
		else if (chnl >= 140 && chnl <= 144)
			*group = 9;
		else if (chnl >= 149 && chnl <= 155)
			*group = 10;
		else if (chnl >= 157 && chnl <= 161)
			*group = 11;
		else if (chnl >= 165 && chnl <= 171)
			*group = 12;
		else if (chnl >= 173 && chnl <= 177)
			*group = 13;
	}
	return in_24g;
}

static inline bool power_valid(u8 power)
{
	if (power <= 63)
		return true;

	return false;
}

static inline s8 power_diff(s8 diff)
{
	/* bit sign number to 8 bit sign number */
	if (diff & BIT(3))
		diff |= 0xF0;

	return diff;
}

static void _rtl8822be_read_power_value_fromprom(struct ieee80211_hw *hw,
						 struct txpower_info_2g *pwr2g,
						 struct txpower_info_5g *pwr5g,
						 bool autoload_fail, u8 *hwinfo)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 rf, addr = EEPROM_TX_PWR_INX_8822B, group, i = 0;
	u8 power;
	s8 diff;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "hal_ReadPowerValueFromPROM8822B(): PROMContent[0x%x]=0x%x\n",
		 (addr + 1), hwinfo[addr + 1]);
	if (hwinfo[addr + 1] == 0xFF) /*YJ,add,120316*/
		autoload_fail = true;

	memset(pwr2g, 0, sizeof(struct txpower_info_2g));
	memset(pwr5g, 0, sizeof(struct txpower_info_5g));

	if (autoload_fail) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "auto load fail : Use Default value!\n");
		for (rf = 0; rf < MAX_RF_PATH; rf++) {
			/* 2.4G default value */
			for (group = 0; group < MAX_CHNL_GROUP_24G; group++) {
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
			for (group = 0; group < MAX_CHNL_GROUP_5G; group++)
				pwr5g->index_bw40_base[rf][group] = 0x2A;

			for (i = 0; i < MAX_TX_COUNT; i++) {
				if (i == 0) {
					pwr5g->ofdm_diff[rf][0] = 0x04;
					pwr5g->bw20_diff[rf][0] = 0x00;
					pwr5g->bw80_diff[rf][0] = 0xFE;
					pwr5g->bw160_diff[rf][0] = 0xFE;
				} else {
					pwr5g->ofdm_diff[rf][i] = 0xFE;
					pwr5g->bw20_diff[rf][i] = 0xFE;
					pwr5g->bw40_diff[rf][i] = 0xFE;
					pwr5g->bw80_diff[rf][i] = 0xFE;
					pwr5g->bw160_diff[rf][i] = 0xFE;
				}
			}
		}
		return;
	}

	rtl_priv(hw)->efuse.txpwr_fromeprom = true;

	for (rf = 0; rf < 2 /*MAX_RF_PATH*/; rf++) {
		/*2.4G default value*/
		for (group = 0; group < MAX_CHNL_GROUP_24G; group++) {
			power = hwinfo[addr++];
			if (power_valid(power))
				pwr2g->index_cck_base[rf][group] = power;
		}
		for (group = 0; group < MAX_CHNL_GROUP_24G - 1; group++) {
			power = hwinfo[addr++];
			if (power_valid(power))
				pwr2g->index_bw40_base[rf][group] = power;
		}
		for (i = 0; i < MAX_TX_COUNT; i++) {
			if (i == 0) {
				pwr2g->bw40_diff[rf][i] = 0;

				diff = (hwinfo[addr] & 0xF0) >> 4;
				pwr2g->bw20_diff[rf][i] = power_diff(diff);

				diff = hwinfo[addr] & 0x0F;
				pwr2g->ofdm_diff[rf][i] = power_diff(diff);

				pwr2g->cck_diff[rf][i] = 0;

				addr++;
			} else {
				diff = (hwinfo[addr] & 0xF0) >> 4;
				pwr2g->bw40_diff[rf][i] = power_diff(diff);

				diff = hwinfo[addr] & 0x0F;
				pwr2g->bw20_diff[rf][i] = power_diff(diff);

				addr++;

				diff = (hwinfo[addr] & 0xF0) >> 4;
				pwr2g->ofdm_diff[rf][i] = power_diff(diff);

				diff = hwinfo[addr] & 0x0F;
				pwr2g->cck_diff[rf][i] = power_diff(diff);

				addr++;
			}
		}

		/*5G default value*/
		for (group = 0; group < MAX_CHNL_GROUP_5G; group++) {
			power = hwinfo[addr++];
			if (power_valid(power))
				pwr5g->index_bw40_base[rf][group] = power;
		}

		for (i = 0; i < MAX_TX_COUNT; i++) {
			if (i == 0) {
				pwr5g->bw40_diff[rf][i] = 0;

				diff = (hwinfo[addr] & 0xF0) >> 4;
				pwr5g->bw20_diff[rf][i] = power_diff(diff);

				diff = hwinfo[addr] & 0x0F;
				pwr5g->ofdm_diff[rf][i] = power_diff(diff);

				addr++;
			} else {
				diff = (hwinfo[addr] & 0xF0) >> 4;
				pwr5g->bw40_diff[rf][i] = power_diff(diff);

				diff = hwinfo[addr] & 0x0F;
				pwr5g->bw20_diff[rf][i] = power_diff(diff);

				addr++;
			}
		}

		diff = (hwinfo[addr] & 0xF0) >> 4;
		pwr5g->ofdm_diff[rf][1] = power_diff(diff);

		diff = hwinfo[addr] & 0x0F;
		pwr5g->ofdm_diff[rf][2] = power_diff(diff);

		addr++;

		diff = hwinfo[addr] & 0x0F;
		pwr5g->ofdm_diff[rf][3] = power_diff(diff);

		addr++;

		for (i = 0; i < MAX_TX_COUNT; i++) {
			diff = (hwinfo[addr] & 0xF0) >> 4;
			pwr5g->bw80_diff[rf][i] = power_diff(diff);

			diff = hwinfo[addr] & 0x0F;
			pwr5g->bw160_diff[rf][i] = power_diff(diff);

			addr++;
		}
	}
}

static void _rtl8822be_read_txpower_info_from_hwpg(struct ieee80211_hw *hw,
						   bool autoload_fail,
						   u8 *hwinfo)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *efu = rtl_efuse(rtl_priv(hw));
	struct txpower_info_2g pwr2g;
	struct txpower_info_5g pwr5g;
	u8 channel5g[CHANNEL_MAX_NUMBER_5G] = {
		36,  38,  40,  42,  44,  46,  48, /* Band 1 */
		52,  54,  56,  58,  60,  62,  64, /* Band 2 */
		100, 102, 104, 106, 108, 110, 112, /* Band 3 */
		116, 118, 120, 122, 124, 126, 128, /* Band 3 */
		132, 134, 136, 138, 140, 142, 144, /* Band 3 */
		149, 151, 153, 155, 157, 159, 161, /* Band 4 */
		165, 167, 169, 171, 173, 175, 177}; /* Band 4 */
	u8 channel5g_80m[CHANNEL_MAX_NUMBER_5G_80M] = {42,  58,  106, 122,
						       138, 155, 171};
	u8 rf, group;
	u8 i;

	_rtl8822be_read_power_value_fromprom(hw, &pwr2g, &pwr5g, autoload_fail,
					     hwinfo);

	for (rf = 0; rf < MAX_RF_PATH; rf++) {
		for (i = 0; i < CHANNEL_MAX_NUMBER_2G; i++) {
			_rtl8822be_get_chnl_group(i + 1, &group);

			if (i == CHANNEL_MAX_NUMBER_2G - 1) {
				efu->txpwrlevel_cck[rf][i] =
					pwr2g.index_cck_base[rf][5];
				efu->txpwrlevel_ht40_1s[rf][i] =
					pwr2g.index_bw40_base[rf][group];
			} else {
				efu->txpwrlevel_cck[rf][i] =
					pwr2g.index_cck_base[rf][group];
				efu->txpwrlevel_ht40_1s[rf][i] =
					pwr2g.index_bw40_base[rf][group];
			}
		}
		for (i = 0; i < CHANNEL_MAX_NUMBER_5G; i++) {
			_rtl8822be_get_chnl_group(channel5g[i], &group);
			efu->txpwr_5g_bw40base[rf][i] =
				pwr5g.index_bw40_base[rf][group];
		}
		for (i = 0; i < CHANNEL_MAX_NUMBER_5G_80M; i++) {
			u8 upper, lower;

			_rtl8822be_get_chnl_group(channel5g_80m[i], &group);
			upper = pwr5g.index_bw40_base[rf][group];
			lower = pwr5g.index_bw40_base[rf][group + 1];

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
		efu->eeprom_thermalmeter = hwinfo[EEPROM_THERMAL_METER_8822B];
	else
		efu->eeprom_thermalmeter = EEPROM_DEFAULT_THERMALMETER;

	if (efu->eeprom_thermalmeter == 0xff || autoload_fail) {
		efu->apk_thermalmeterignore = true;
		efu->eeprom_thermalmeter = EEPROM_DEFAULT_THERMALMETER;
	}

	efu->thermalmeter[0] = efu->eeprom_thermalmeter;
	RTPRINT(rtlpriv, FINIT, INIT_TXPOWER, "thermalmeter = 0x%x\n",
		efu->eeprom_thermalmeter);

	if (!autoload_fail) {
		efu->eeprom_regulatory =
			hwinfo[EEPROM_RF_BOARD_OPTION_8822B] & 0x07;
		if (hwinfo[EEPROM_RF_BOARD_OPTION_8822B] == 0xFF)
			efu->eeprom_regulatory = 0;
	} else {
		efu->eeprom_regulatory = 0;
	}
	RTPRINT(rtlpriv, FINIT, INIT_TXPOWER, "eeprom_regulatory = 0x%x\n",
		efu->eeprom_regulatory);
}

static void _rtl8822be_read_pa_type(struct ieee80211_hw *hw, u8 *hwinfo,
				    bool autoload_fail)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);

	if (!autoload_fail) {
		rtlhal->pa_type_2g = hwinfo[EEPROM_2G_5G_PA_TYPE_8822B];
		rtlhal->lna_type_2g =
			hwinfo[EEPROM_2G_LNA_TYPE_GAIN_SEL_AB_8822B];
		if (rtlhal->pa_type_2g == 0xFF)
			rtlhal->pa_type_2g = 0;
		if (rtlhal->lna_type_2g == 0xFF)
			rtlhal->lna_type_2g = 0;

		rtlhal->external_pa_2g = (rtlhal->pa_type_2g & BIT(4)) ? 1 : 0;
		rtlhal->external_lna_2g =
			(rtlhal->lna_type_2g & BIT(3)) ? 1 : 0;

		rtlhal->pa_type_5g = hwinfo[EEPROM_2G_5G_PA_TYPE_8822B];
		rtlhal->lna_type_5g =
			hwinfo[EEPROM_5G_LNA_TYPE_GAIN_SEL_AB_8822B];
		if (rtlhal->pa_type_5g == 0xFF)
			rtlhal->pa_type_5g = 0;
		if (rtlhal->lna_type_5g == 0xFF)
			rtlhal->lna_type_5g = 0;

		rtlhal->external_pa_5g = (rtlhal->pa_type_5g & BIT(0)) ? 1 : 0;
		rtlhal->external_lna_5g =
			(rtlhal->lna_type_5g & BIT(3)) ? 1 : 0;
	} else {
		rtlhal->external_pa_2g = 0;
		rtlhal->external_lna_2g = 0;
		rtlhal->external_pa_5g = 0;
		rtlhal->external_lna_5g = 0;
	}
}

static void _rtl8822be_read_amplifier_type(struct ieee80211_hw *hw, u8 *hwinfo,
					   bool autoload_fail)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);

	u8 ext_type_pa_2g_a =
		(hwinfo[EEPROM_2G_LNA_TYPE_GAIN_SEL_AB_8822B] & BIT(2)) >>
		2; /* 0xBD[2] */
	u8 ext_type_pa_2g_b =
		(hwinfo[EEPROM_2G_LNA_TYPE_GAIN_SEL_AB_8822B] & BIT(6)) >>
		6; /* 0xBD[6] */
	u8 ext_type_pa_5g_a =
		(hwinfo[EEPROM_5G_LNA_TYPE_GAIN_SEL_AB_8822B] & BIT(2)) >>
		2; /* 0xBF[2] */
	u8 ext_type_pa_5g_b =
		(hwinfo[EEPROM_5G_LNA_TYPE_GAIN_SEL_AB_8822B] & BIT(6)) >>
		6; /* 0xBF[6] */
	u8 ext_type_lna_2g_a = (hwinfo[EEPROM_2G_LNA_TYPE_GAIN_SEL_AB_8822B] &
				(BIT(1) | BIT(0))) >>
			       0; /* 0xBD[1:0] */
	u8 ext_type_lna_2g_b = (hwinfo[EEPROM_2G_LNA_TYPE_GAIN_SEL_AB_8822B] &
				(BIT(5) | BIT(4))) >>
			       4; /* 0xBD[5:4] */
	u8 ext_type_lna_5g_a = (hwinfo[EEPROM_5G_LNA_TYPE_GAIN_SEL_AB_8822B] &
				(BIT(1) | BIT(0))) >>
			       0; /* 0xBF[1:0] */
	u8 ext_type_lna_5g_b = (hwinfo[EEPROM_5G_LNA_TYPE_GAIN_SEL_AB_8822B] &
				(BIT(5) | BIT(4))) >>
			       4; /* 0xBF[5:4] */

	_rtl8822be_read_pa_type(hw, hwinfo, autoload_fail);

	/* [2.4G] Path A and B are both extPA */
	if ((rtlhal->pa_type_2g & (BIT(5) | BIT(4))) == (BIT(5) | BIT(4)))
		rtlhal->type_gpa = ext_type_pa_2g_b << 2 | ext_type_pa_2g_a;

	/* [5G] Path A and B are both extPA */
	if ((rtlhal->pa_type_5g & (BIT(1) | BIT(0))) == (BIT(1) | BIT(0)))
		rtlhal->type_apa = ext_type_pa_5g_b << 2 | ext_type_pa_5g_a;

	/* [2.4G] Path A and B are both extLNA */
	if ((rtlhal->lna_type_2g & (BIT(7) | BIT(3))) == (BIT(7) | BIT(3)))
		rtlhal->type_glna = ext_type_lna_2g_b << 2 | ext_type_lna_2g_a;

	/* [5G] Path A and B are both extLNA */
	if ((rtlhal->lna_type_5g & (BIT(7) | BIT(3))) == (BIT(7) | BIT(3)))
		rtlhal->type_alna = ext_type_lna_5g_b << 2 | ext_type_lna_5g_a;
}

static void _rtl8822be_read_rfe_type(struct ieee80211_hw *hw, u8 *hwinfo,
				     bool autoload_fail)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);

	if (!autoload_fail)
		rtlhal->rfe_type = hwinfo[EEPROM_RFE_OPTION_8822B];
	else
		rtlhal->rfe_type = 0;

	if (rtlhal->rfe_type == 0xFF)
		rtlhal->rfe_type = 0;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "RFE Type: 0x%2x\n",
		 rtlhal->rfe_type);
}

static void _rtl8822be_read_adapter_info(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_halmac_ops *halmac_ops = rtlpriv->halmac.ops;
	u16 i, usvalue;
	u8 *hwinfo;
	u16 eeprom_id;
	u32 efuse_size;
	int err;

	if (rtlefuse->epromtype != EEPROM_BOOT_EFUSE) {
		pr_err("RTL8822B Not boot from efuse!!");
		return;
	}

	/* read logical efuse size (normalely, 0x0300) */
	err = halmac_ops->halmac_get_logical_efuse_size(rtlpriv, &efuse_size);

	if (err || !efuse_size) {
		pr_err("halmac_get_logical_efuse_size err=%d efuse_size=0x%X",
		       err, efuse_size);
		efuse_size = HWSET_MAX_SIZE;
	}

	if (efuse_size > HWSET_MAX_SIZE) {
		pr_err("halmac_get_logical_efuse_size efuse_size=0x%X > 0x%X",
		       efuse_size, HWSET_MAX_SIZE);
		efuse_size = HWSET_MAX_SIZE;
	}

	/* read efuse */
	hwinfo = kzalloc(efuse_size, GFP_KERNEL);

	err = halmac_ops->halmac_read_logical_efuse_map(rtlpriv, hwinfo,
							efuse_size);
	if (err) {
		pr_err("%s: <ERROR> fail to get efuse map!\n", __func__);
		goto label_end;
	}

	/* copy to efuse_map (need?) */
	memcpy(&rtlefuse->efuse_map[EFUSE_INIT_MAP][0], hwinfo,
	       EFUSE_MAX_LOGICAL_SIZE);
	memcpy(&rtlefuse->efuse_map[EFUSE_MODIFY_MAP][0], hwinfo,
	       EFUSE_MAX_LOGICAL_SIZE);

	/* parse content */
	RT_PRINT_DATA(rtlpriv, COMP_INIT, DBG_DMESG, "MAP\n", hwinfo,
		      HWSET_MAX_SIZE);

	eeprom_id = *((u16 *)&hwinfo[0]);
	if (eeprom_id != RTL8822B_EEPROM_ID) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 "EEPROM ID(%#x) is invalid!!\n", eeprom_id);
		rtlefuse->autoload_failflag = true;
	} else {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "Autoload OK\n");
		rtlefuse->autoload_failflag = false;
	}

	if (rtlefuse->autoload_failflag)
		goto label_end;

	/*VID DID SVID SDID*/
	rtlefuse->eeprom_vid = *(u16 *)&hwinfo[EEPROM_VID];
	rtlefuse->eeprom_did = *(u16 *)&hwinfo[EEPROM_DID];
	rtlefuse->eeprom_svid = *(u16 *)&hwinfo[EEPROM_SVID];
	rtlefuse->eeprom_smid = *(u16 *)&hwinfo[EEPROM_SMID];
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "EEPROMId = 0x%4x\n", eeprom_id);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "EEPROM VID = 0x%4x\n",
		 rtlefuse->eeprom_vid);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "EEPROM DID = 0x%4x\n",
		 rtlefuse->eeprom_did);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "EEPROM SVID = 0x%4x\n",
		 rtlefuse->eeprom_svid);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "EEPROM SMID = 0x%4x\n",
		 rtlefuse->eeprom_smid);
	/*customer ID*/
	rtlefuse->eeprom_oemid = *(u8 *)&hwinfo[EEPROM_CUSTOM_ID_8822B];
	if (rtlefuse->eeprom_oemid == 0xFF)
		rtlefuse->eeprom_oemid = 0;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "EEPROM Customer ID: 0x%2x\n",
		 rtlefuse->eeprom_oemid);
	/*EEPROM version*/
	rtlefuse->eeprom_version = *(u8 *)&hwinfo[EEPROM_VERSION_8822B];
	/*mac address*/
	for (i = 0; i < 6; i += 2) {
		usvalue = *(u16 *)&hwinfo[EEPROM_MAC_ADDR_8822BE + i];
		*((u16 *)(&rtlefuse->dev_addr[i])) = usvalue;
	}

	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "dev_addr: %pM\n",
		 rtlefuse->dev_addr);

	/* channel plan */
	rtlefuse->eeprom_channelplan =
		*(u8 *)&hwinfo[EEPROM_CHANNEL_PLAN_8822B];

	/* set channel plan from efuse */
	rtlefuse->channel_plan = rtlefuse->eeprom_channelplan;
	if (rtlefuse->channel_plan == 0xFF)
		rtlefuse->channel_plan = 0x7f; /* use 2G + 5G as default */

	/*tx power*/
	_rtl8822be_read_txpower_info_from_hwpg(hw, rtlefuse->autoload_failflag,
					       hwinfo);

	rtl8822be_read_bt_coexist_info_from_hwpg(
		hw, rtlefuse->autoload_failflag, hwinfo);

	/*amplifier type*/
	_rtl8822be_read_amplifier_type(hw, hwinfo, rtlefuse->autoload_failflag);

	/*rfe type*/
	_rtl8822be_read_rfe_type(hw, hwinfo, rtlefuse->autoload_failflag);

	/*board type*/
	rtlefuse->board_type =
		(((*(u8 *)&hwinfo[EEPROM_RF_BOARD_OPTION_8822B]) & 0xE0) >> 5);
	if ((*(u8 *)&hwinfo[EEPROM_RF_BOARD_OPTION_8822B]) == 0xFF)
		rtlefuse->board_type = 0;

	if (rtlpriv->btcoexist.btc_info.btcoexist == 1)
		rtlefuse->board_type |= BIT(2); /* ODM_BOARD_BT */

	/* phydm maintain rtlhal->board_type and rtlhal->package_type */
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "board_type = 0x%x\n",
		 rtlefuse->board_type);
	/*parse xtal*/
	rtlefuse->crystalcap = hwinfo[EEPROM_XTAL_8822B];
	if (hwinfo[EEPROM_XTAL_8822B] == 0xFF)
		rtlefuse->crystalcap = 0; /*0x20;*/

	/*antenna diversity*/
	rtlefuse->antenna_div_type = 0;
	rtlefuse->antenna_div_cfg = 0;

label_end:
	kfree(hwinfo);
}

static void _rtl8822be_hal_customized_behavior(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	pcipriv->ledctl.led_opendrain = true;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "RT Customized ID: 0x%02X\n",
		 rtlhal->oem_id);
}

static void _rtl8822be_read_pa_bias(struct ieee80211_hw *hw,
				    struct rtl_phydm_params *params)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_halmac_ops *halmac_ops = rtlpriv->halmac.ops;
	u32 size;
	u8 *map = NULL;

	/* fill default values */
	params->efuse0x3d7 = 0xFF;
	params->efuse0x3d8 = 0xFF;

	if (halmac_ops->halmac_get_physical_efuse_size(rtlpriv, &size))
		goto err;

	map = kmalloc(size, GFP_KERNEL);
	if (!map)
		goto err;

	if (halmac_ops->halmac_read_physical_efuse_map(rtlpriv, map, size))
		goto err;

	params->efuse0x3d7 = map[0x3d7];
	params->efuse0x3d8 = map[0x3d8];

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "efuse0x3d7 = 0x%2x, efuse0x3d8 = 0x%2x\n",
		 params->efuse0x3d7, params->efuse0x3d8);

err:
	kfree(map);
}

void rtl8822be_read_eeprom_info(struct ieee80211_hw *hw,
				struct rtl_phydm_params *params)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 tmp_u1b;

	rtlhal->version = _rtl8822be_read_chip_version(hw);

	params->mp_chip = (rtlhal->version & BIT_RTL_ID_8822B) ? 0 : 1;
	params->fab_ver = BIT_GET_VENDOR_ID_8822B(rtlhal->version) >> 2;
	params->cut_ver = BIT_GET_CHIP_VER_8822B(rtlhal->version);

	/* fab_ver mapping */
	if (params->fab_ver == 2)
		params->fab_ver = 1;
	else if (params->fab_ver == 1)
		params->fab_ver = 2;

	/* read PA bias: params->efuse0x3d7/efuse0x3d8 */
	_rtl8822be_read_pa_bias(hw, params);

	if (get_rf_type(rtlphy) == RF_1T1R)
		rtlpriv->dm.rfpath_rxenable[0] = true;
	else
		rtlpriv->dm.rfpath_rxenable[0] =
			rtlpriv->dm.rfpath_rxenable[1] = true;
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "VersionID = 0x%4x\n",
		 rtlhal->version);
	tmp_u1b = rtl_read_byte(rtlpriv, REG_SYS_EEPROM_CTRL_8822B);
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
		_rtl8822be_read_adapter_info(hw);
	} else {
		pr_err("Autoload ERR!!\n");
	}
	_rtl8822be_hal_customized_behavior(hw);

	rtlphy->rfpath_rx_enable[0] = true;
	if (rtlphy->rf_type == RF_2T2R)
		rtlphy->rfpath_rx_enable[1] = true;
}

void rtl8822be_read_eeprom_info_dummy(struct ieee80211_hw *hw)
{
	/*
	 * 8822b use halmac, so
	 * move rtl8822be_read_eeprom_info() to rtl8822be_init_sw_vars()
	 * after halmac_init_adapter().
	 */
}

static u32 _rtl8822be_rate_to_bitmap_2ssvht(__le16 vht_rate)
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

static u8 _rtl8822be_get_vht_en(enum wireless_mode wirelessmode,
				u32 ratr_bitmap)
{
	u8 ret = 0;

	if (wirelessmode < WIRELESS_MODE_N_24G) {
		ret = 0;
	} else if (wirelessmode == WIRELESS_MODE_AC_24G) {
		if (ratr_bitmap & 0xfff00000) /* Mix , 2SS */
			ret = 3;
		else /* Mix, 1SS */
			ret = 2;
	} else if (wirelessmode == WIRELESS_MODE_AC_5G) {
		ret = 1;
	} /* VHT */

	return ret << 4;
}

static u8 _rtl8822be_get_ra_ldpc(struct ieee80211_hw *hw, u8 mac_id,
				 struct rtl_sta_info *sta_entry,
				 enum wireless_mode wirelessmode)
{
	u8 b_ldpc = 0;
	/*not support ldpc, do not open*/
	return b_ldpc << 2;
}

static u8 _rtl8822be_get_ra_rftype(struct ieee80211_hw *hw,
				   enum wireless_mode wirelessmode,
				   u32 ratr_bitmap)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 rf_type = RF_1T1R;

	if (rtlphy->rf_type == RF_1T1R) {
		rf_type = RF_1T1R;
	} else if (wirelessmode == WIRELESS_MODE_AC_5G ||
		   wirelessmode == WIRELESS_MODE_AC_24G ||
		   wirelessmode == WIRELESS_MODE_AC_ONLY) {
		if (ratr_bitmap & 0xffc00000)
			rf_type = RF_2T2R;
	} else if (wirelessmode == WIRELESS_MODE_N_5G ||
		   wirelessmode == WIRELESS_MODE_N_24G) {
		if (ratr_bitmap & 0xfff00000)
			rf_type = RF_2T2R;
	}

	return rf_type;
}

static bool _rtl8822be_get_ra_shortgi(struct ieee80211_hw *hw,
				      struct ieee80211_sta *sta, u8 mac_id)
{
	bool b_short_gi = false;
	u8 b_curshortgi_40mhz =
		(sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_40) ? 1 : 0;
	u8 b_curshortgi_20mhz =
		(sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_20) ? 1 : 0;
	u8 b_curshortgi_80mhz = 0;

	b_curshortgi_80mhz =
		(sta->vht_cap.cap & IEEE80211_VHT_CAP_SHORT_GI_80) ? 1 : 0;

	if (mac_id == 99 /*MAC_ID_STATIC_FOR_BROADCAST_MULTICAST*/)
		b_short_gi = false;

	if (b_curshortgi_40mhz || b_curshortgi_80mhz || b_curshortgi_20mhz)
		b_short_gi = true;

	return b_short_gi;
}

static void rtl8822be_update_hal_rate_mask(struct ieee80211_hw *hw,
					   struct ieee80211_sta *sta,
					   u8 rssi_level, bool update_bw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_sta_info *sta_entry = NULL;
	u32 ratr_bitmap, ratr_bitmap_msb = 0;
	u8 ratr_index;
	enum wireless_mode wirelessmode = 0;
	u8 curtxbw_40mhz =
		(sta->ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40) ? 1 : 0;
	bool b_shortgi = false;
	u8 rate_mask[7];
	u8 macid = 0;
	u8 rf_type;

	sta_entry = (struct rtl_sta_info *)sta->drv_priv;
	wirelessmode = sta_entry->wireless_mode;

	RT_TRACE(rtlpriv, COMP_RATR, DBG_LOUD, "wireless mode = 0x%x\n",
		 wirelessmode);
	if (mac->opmode == NL80211_IFTYPE_STATION ||
	    mac->opmode == NL80211_IFTYPE_MESH_POINT) {
		curtxbw_40mhz = mac->bw_40;
	} else if (mac->opmode == NL80211_IFTYPE_AP ||
		   mac->opmode == NL80211_IFTYPE_ADHOC)
		macid = sta->aid + 1;
	if (wirelessmode == WIRELESS_MODE_N_5G ||
	    wirelessmode == WIRELESS_MODE_AC_5G ||
	    wirelessmode == WIRELESS_MODE_A)
		ratr_bitmap = (sta->supp_rates[NL80211_BAND_5GHZ]) << 4;
	else
		ratr_bitmap = sta->supp_rates[NL80211_BAND_2GHZ];

	if (mac->opmode == NL80211_IFTYPE_ADHOC)
		ratr_bitmap = 0xfff;

	if (wirelessmode == WIRELESS_MODE_N_24G ||
	    wirelessmode == WIRELESS_MODE_N_5G)
		ratr_bitmap |= (sta->ht_cap.mcs.rx_mask[1] << 20 |
				sta->ht_cap.mcs.rx_mask[0] << 12);
	else if (wirelessmode == WIRELESS_MODE_AC_24G ||
		 wirelessmode == WIRELESS_MODE_AC_5G ||
		 wirelessmode == WIRELESS_MODE_AC_ONLY)
		ratr_bitmap |= _rtl8822be_rate_to_bitmap_2ssvht(
				       sta->vht_cap.vht_mcs.rx_mcs_map)
			       << 12;

	b_shortgi = _rtl8822be_get_ra_shortgi(hw, sta, macid);
	rf_type = _rtl8822be_get_ra_rftype(hw, wirelessmode, ratr_bitmap);

	ratr_index = rtlpriv->phydm.ops->phydm_rate_id_mapping(
		rtlpriv, wirelessmode, rf_type, rtlphy->current_chan_bw);
	sta_entry->ratr_index = ratr_index;

	rtlpriv->phydm.ops->phydm_get_ra_bitmap(
		rtlpriv, wirelessmode, rf_type, rtlphy->current_chan_bw,
		rssi_level, &ratr_bitmap_msb, &ratr_bitmap);

	RT_TRACE(rtlpriv, COMP_RATR, DBG_LOUD, "ratr_bitmap :%x\n",
		 ratr_bitmap);

	rate_mask[0] = macid;
	rate_mask[1] = ratr_index | (b_shortgi ? 0x80 : 0x00);
	rate_mask[2] =
		rtlphy->current_chan_bw | ((!update_bw) << 3) |
		_rtl8822be_get_vht_en(wirelessmode, ratr_bitmap) |
		_rtl8822be_get_ra_ldpc(hw, macid, sta_entry, wirelessmode);

	rate_mask[3] = (u8)(ratr_bitmap & 0x000000ff);
	rate_mask[4] = (u8)((ratr_bitmap & 0x0000ff00) >> 8);
	rate_mask[5] = (u8)((ratr_bitmap & 0x00ff0000) >> 16);
	rate_mask[6] = (u8)((ratr_bitmap & 0xff000000) >> 24);

	RT_TRACE(
		rtlpriv, COMP_RATR, DBG_DMESG,
		"Rate_index:%x, ratr_val:%08x, %02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
		ratr_index, ratr_bitmap, rate_mask[0], rate_mask[1],
		rate_mask[2], rate_mask[3], rate_mask[4], rate_mask[5],
		rate_mask[6]);
	rtl8822be_fill_h2c_cmd(hw, H2C_8822B_MACID_CFG, 7, rate_mask);

	/* for h2c cmd 0x46, only modify cmd id & ra mask */
	/* Keep rate_mask0~2 of cmd 0x40, but clear byte3 and later */
	/* 8822B has no 3SS, so keep it zeros. */
	memset(rate_mask + 3, 0, 4);

	rtl8822be_fill_h2c_cmd(hw, H2C_8822B_MACID_CFG_3SS, 7, rate_mask);

	_rtl8822be_set_bcn_ctrl_reg(hw, BIT(3), 0);
}

void rtl8822be_update_hal_rate_tbl(struct ieee80211_hw *hw,
				   struct ieee80211_sta *sta, u8 rssi_level,
				   bool update_bw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (rtlpriv->dm.useramask)
		rtl8822be_update_hal_rate_mask(hw, sta, rssi_level, update_bw);
}

void rtl8822be_update_channel_access_setting(struct ieee80211_hw *hw)
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

bool rtl8822be_gpio_radio_on_off_checking(struct ieee80211_hw *hw, u8 *valid)
{
	*valid = 1;
	return true;
}

void rtl8822be_set_key(struct ieee80211_hw *hw, u32 key_index, u8 *p_macaddr,
		       bool is_group, u8 enc_algo, bool is_wepkey,
		       bool clear_all)
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
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x03},
	};
	static u8 cam_const_broad[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

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
				entry_id =
					rtl_cam_get_free_entry(hw, p_macaddr);
				if (entry_id >= TOTAL_CAM_ENTRY) {
					pr_err("Can not find free hwsecurity cam entry\n");
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
			 "delete one entry, entry_id is %d\n", entry_id);
		if (mac->opmode == NL80211_IFTYPE_AP)
			rtl_cam_del_entry(hw, p_macaddr);
		rtl_cam_delete_one_entry(hw, p_macaddr, entry_id);
	} else {
		RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG, "add one entry\n");
		if (is_pairwise) {
			RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG,
				 "set Pairwise key\n");

			rtl_cam_add_one_entry(hw, macaddr, key_index, entry_id,
					      enc_algo, CAM_CONFIG_NO_USEDK,
					      rtlpriv->sec.key_buf[key_index]);
		} else {
			RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG,
				 "set group key\n");

			if (mac->opmode == NL80211_IFTYPE_ADHOC) {
				rtl_cam_add_one_entry(
					hw, rtlefuse->dev_addr, PAIRWISE_KEYIDX,
					CAM_PAIRWISE_KEY_POSITION, enc_algo,
					CAM_CONFIG_NO_USEDK,
					rtlpriv->sec.key_buf[entry_id]);
			}

			rtl_cam_add_one_entry(hw, macaddr, key_index, entry_id,
					      enc_algo, CAM_CONFIG_NO_USEDK,
					      rtlpriv->sec.key_buf[entry_id]);
		}
	}
}

void rtl8822be_read_bt_coexist_info_from_hwpg(struct ieee80211_hw *hw,
					      bool auto_load_fail, u8 *hwinfo)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 value;
	u32 val32;

	val32 = rtl_read_dword(rtlpriv, REG_WL_BT_PWR_CTRL_8822B);
	if (val32 & BIT_BT_FUNC_EN_8822B)
		rtlpriv->btcoexist.btc_info.btcoexist = 1;
	else
		rtlpriv->btcoexist.btc_info.btcoexist = 0;

	if (!auto_load_fail) {
		value = hwinfo[EEPROM_RF_BT_SETTING_8822B];

		rtlpriv->btcoexist.btc_info.bt_type = BT_RTL8822B;
		rtlpriv->btcoexist.btc_info.ant_num =
			(value & BIT(0) ? ANT_TOTAL_X1 : ANT_TOTAL_X2);
	} else {
		rtlpriv->btcoexist.btc_info.bt_type = BT_RTL8822B;
		rtlpriv->btcoexist.btc_info.ant_num = ANT_TOTAL_X2;
	}
}

void rtl8822be_bt_reg_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	/* 0:Low, 1:High, 2:From Efuse. */
	rtlpriv->btcoexist.reg_bt_iso = 2;
	/* 0:Idle, 1:None-SCO, 2:SCO, 3:From Counter. */
	rtlpriv->btcoexist.reg_bt_sco = 3;
	/* 0:Disable BT control A-MPDU, 1:Enable BT control A-MPDU. */
	rtlpriv->btcoexist.reg_bt_sco = 0;
}

void rtl8822be_suspend(struct ieee80211_hw *hw) {}

void rtl8822be_resume(struct ieee80211_hw *hw) {}
