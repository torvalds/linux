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
#include "sw.h"
#include "hw.h"

u32 rtl92de_read_dword_dbi(struct ieee80211_hw *hw, u16 offset, u8 direct)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 value;

	rtl_write_word(rtlpriv, REG_DBI_CTRL, (offset & 0xFFC));
	rtl_write_byte(rtlpriv, REG_DBI_FLAG, BIT(1) | direct);
	udelay(10);
	value = rtl_read_dword(rtlpriv, REG_DBI_RDATA);
	return value;
}

void rtl92de_write_dword_dbi(struct ieee80211_hw *hw,
			     u16 offset, u32 value, u8 direct)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_write_word(rtlpriv, REG_DBI_CTRL, ((offset & 0xFFC) | 0xF000));
	rtl_write_dword(rtlpriv, REG_DBI_WDATA, value);
	rtl_write_byte(rtlpriv, REG_DBI_FLAG, BIT(0) | direct);
}

static void _rtl92de_set_bcn_ctrl_reg(struct ieee80211_hw *hw,
				      u8 set_bits, u8 clear_bits)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpci->reg_bcn_ctrl_val |= set_bits;
	rtlpci->reg_bcn_ctrl_val &= ~clear_bits;
	rtl_write_byte(rtlpriv, REG_BCN_CTRL, (u8) rtlpci->reg_bcn_ctrl_val);
}

static void _rtl92de_stop_tx_beacon(struct ieee80211_hw *hw)
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

static void _rtl92de_resume_tx_beacon(struct ieee80211_hw *hw)
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

static void _rtl92de_enable_bcn_sub_func(struct ieee80211_hw *hw)
{
	_rtl92de_set_bcn_ctrl_reg(hw, 0, BIT(1));
}

static void _rtl92de_disable_bcn_sub_func(struct ieee80211_hw *hw)
{
	_rtl92de_set_bcn_ctrl_reg(hw, BIT(1), 0);
}

void rtl92de_get_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val)
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

		rtlpriv->cfg->ops->get_hw_reg(hw, HW_VAR_RF_STATE,
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
		break;
	}
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
		break;
	}
	case HW_VAR_INT_MIGRATION:
		*((bool *)(val)) = rtlpriv->dm.interrupt_migration;
		break;
	case HW_VAR_INT_AC:
		*((bool *)(val)) = rtlpriv->dm.disable_tx_int;
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "switch case not processed\n");
		break;
	}
}

void rtl92de_set_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
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
		u16 rate_cfg = ((u16 *) val)[0];
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
				       *((u16 *) val));
		break;
	case HW_VAR_SLOT_TIME: {
		u8 e_aci;

		RT_TRACE(rtlpriv, COMP_MLME, DBG_LOUD,
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
		u8 short_preamble = (bool) (*val);

		reg_tmp = (mac->cur_40_prime_sc) << 5;
		if (short_preamble)
			reg_tmp |= 0x80;
		rtl_write_byte(rtlpriv, REG_RRSR + 2, reg_tmp);
		break;
	}
	case HW_VAR_AMPDU_MIN_SPACE: {
		u8 min_spacing_to_set;
		u8 sec_min_space;

		min_spacing_to_set = *val;
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
		break;
	}
	case HW_VAR_SHORTGI_DENSITY: {
		u8 density_to_set;

		density_to_set = *val;
		mac->min_space_cfg = rtlpriv->rtlhal.minspace_cfg;
		mac->min_space_cfg |= (density_to_set << 3);
		RT_TRACE(rtlpriv, COMP_MLME, DBG_LOUD,
			 "Set HW_VAR_SHORTGI_DENSITY: %#x\n",
			 mac->min_space_cfg);
		rtl_write_byte(rtlpriv, REG_AMPDU_MIN_SPACE,
			       mac->min_space_cfg);
		break;
	}
	case HW_VAR_AMPDU_FACTOR: {
		u8 factor_toset;
		u32 regtoSet;
		u8 *ptmp_byte = NULL;
		u8 index;

		if (rtlhal->macphymode == DUALMAC_DUALPHY)
			regtoSet = 0xb9726641;
		else if (rtlhal->macphymode == DUALMAC_SINGLEPHY)
			regtoSet = 0x66626641;
		else
			regtoSet = 0xb972a841;
		factor_toset = *val;
		if (factor_toset <= 3) {
			factor_toset = (1 << (factor_toset + 2));
			if (factor_toset > 0xf)
				factor_toset = 0xf;
			for (index = 0; index < 4; index++) {
				ptmp_byte = (u8 *) (&regtoSet) + index;
				if ((*ptmp_byte & 0xf0) >
				    (factor_toset << 4))
					*ptmp_byte = (*ptmp_byte & 0x0f)
						 | (factor_toset << 4);
				if ((*ptmp_byte & 0x0f) > factor_toset)
					*ptmp_byte = (*ptmp_byte & 0xf0)
						     | (factor_toset);
			}
			rtl_write_dword(rtlpriv, REG_AGGLEN_LMT, regtoSet);
			RT_TRACE(rtlpriv, COMP_MLME, DBG_LOUD,
				 "Set HW_VAR_AMPDU_FACTOR: %#x\n",
				 factor_toset);
		}
		break;
	}
	case HW_VAR_AC_PARAM: {
		u8 e_aci = *val;
		rtl92d_dm_init_edca_turbo(hw);
		if (rtlpci->acm_method != eAcmWay2_SW)
			rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_ACM_CTRL,
						      &e_aci);
		break;
	}
	case HW_VAR_ACM_CTRL: {
		u8 e_aci = *val;
		union aci_aifsn *p_aci_aifsn =
		    (union aci_aifsn *)(&(mac->ac[0].aifs));
		u8 acm = p_aci_aifsn->f.acm;
		u8 acm_ctrl = rtl_read_byte(rtlpriv, REG_ACMHWCTRL);

		acm_ctrl = acm_ctrl | ((rtlpci->acm_method == 2) ?  0x0 : 0x1);
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
				RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
					 "switch case not processed\n");
				break;
			}
		}
		RT_TRACE(rtlpriv, COMP_QOS, DBG_TRACE,
			 "SetHwReg8190pci(): [HW_VAR_ACM_CTRL] Write 0x%X\n",
			 acm_ctrl);
		rtl_write_byte(rtlpriv, REG_ACMHWCTRL, acm_ctrl);
		break;
	}
	case HW_VAR_RCR:
		rtl_write_dword(rtlpriv, REG_RCR, ((u32 *) (val))[0]);
		rtlpci->receive_config = ((u32 *) (val))[0];
		break;
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
		rtlefuse->efuse_usedbytes = *((u16 *) val);
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
		ppsc->fw_current_inpsmode = *((bool *) val);
		break;
	case HW_VAR_H2C_FW_JOINBSSRPT: {
		u8 mstatus = (*val);
		u8 tmp_regcr, tmp_reg422;
		bool recover = false;

		if (mstatus == RT_MEDIA_CONNECT) {
			rtlpriv->cfg->ops->set_hw_reg(hw,
						      HW_VAR_AID, NULL);
			tmp_regcr = rtl_read_byte(rtlpriv, REG_CR + 1);
			rtl_write_byte(rtlpriv, REG_CR + 1,
				       (tmp_regcr | BIT(0)));
			_rtl92de_set_bcn_ctrl_reg(hw, 0, BIT(3));
			_rtl92de_set_bcn_ctrl_reg(hw, BIT(4), 0);
			tmp_reg422 = rtl_read_byte(rtlpriv,
						 REG_FWHW_TXQ_CTRL + 2);
			if (tmp_reg422 & BIT(6))
				recover = true;
			rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2,
				       tmp_reg422 & (~BIT(6)));
			rtl92d_set_fw_rsvdpagepkt(hw, 0);
			_rtl92de_set_bcn_ctrl_reg(hw, BIT(3), 0);
			_rtl92de_set_bcn_ctrl_reg(hw, 0, BIT(4));
			if (recover)
				rtl_write_byte(rtlpriv,
					       REG_FWHW_TXQ_CTRL + 2,
					       tmp_reg422);
			rtl_write_byte(rtlpriv, REG_CR + 1,
				       (tmp_regcr & ~(BIT(0))));
		}
		rtl92d_set_fw_joinbss_report_cmd(hw, (*val));
		break;
	}
	case HW_VAR_AID: {
		u16 u2btmp;
		u2btmp = rtl_read_word(rtlpriv, REG_BCN_PSR_RPT);
		u2btmp &= 0xC000;
		rtl_write_word(rtlpriv, REG_BCN_PSR_RPT, (u2btmp |
			       mac->assoc_id));
		break;
	}
	case HW_VAR_CORRECT_TSF: {
		u8 btype_ibss = val[0];

		if (btype_ibss)
			_rtl92de_stop_tx_beacon(hw);
		_rtl92de_set_bcn_ctrl_reg(hw, 0, BIT(3));
		rtl_write_dword(rtlpriv, REG_TSFTR,
				(u32) (mac->tsf & 0xffffffff));
		rtl_write_dword(rtlpriv, REG_TSFTR + 4,
				(u32) ((mac->tsf >> 32) & 0xffffffff));
		_rtl92de_set_bcn_ctrl_reg(hw, BIT(3), 0);
		if (btype_ibss)
			_rtl92de_resume_tx_beacon(hw);

		break;
	}
	case HW_VAR_INT_MIGRATION: {
		bool int_migration = *(bool *) (val);

		if (int_migration) {
			/* Set interrupt migration timer and
			 * corresponding Tx/Rx counter.
			 * timer 25ns*0xfa0=100us for 0xf packets.
			 * 0x306:Rx, 0x307:Tx */
			rtl_write_dword(rtlpriv, REG_INT_MIG, 0xfe000fa0);
			rtlpriv->dm.interrupt_migration = int_migration;
		} else {
			/* Reset all interrupt migration settings. */
			rtl_write_dword(rtlpriv, REG_INT_MIG, 0);
			rtlpriv->dm.interrupt_migration = int_migration;
		}
		break;
	}
	case HW_VAR_INT_AC: {
		bool disable_ac_int = *((bool *) val);

		/* Disable four ACs interrupts. */
		if (disable_ac_int) {
			/* Disable VO, VI, BE and BK four AC interrupts
			 * to gain more efficient CPU utilization.
			 * When extremely highly Rx OK occurs,
			 * we will disable Tx interrupts.
			 */
			rtlpriv->cfg->ops->update_interrupt_mask(hw, 0,
						 RT_AC_INT_MASKS);
			rtlpriv->dm.disable_tx_int = disable_ac_int;
		/* Enable four ACs interrupts. */
		} else {
			rtlpriv->cfg->ops->update_interrupt_mask(hw,
						 RT_AC_INT_MASKS, 0);
			rtlpriv->dm.disable_tx_int = disable_ac_int;
		}
		break;
	}
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "switch case not processed\n");
		break;
	}
}

static bool _rtl92de_llt_write(struct ieee80211_hw *hw, u32 address, u32 data)
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

static bool _rtl92de_llt_table_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	unsigned short i;
	u8 txpktbuf_bndy;
	u8 maxPage;
	bool status;
	u32 value32; /* High+low page number */
	u8 value8;	 /* normal page number */

	if (rtlpriv->rtlhal.macphymode == SINGLEMAC_SINGLEPHY) {
		maxPage = 255;
		txpktbuf_bndy = 246;
		value8 = 0;
		value32 = 0x80bf0d29;
	} else if (rtlpriv->rtlhal.macphymode != SINGLEMAC_SINGLEPHY) {
		maxPage = 127;
		txpktbuf_bndy = 123;
		value8 = 0;
		value32 = 0x80750005;
	}

	/* Set reserved page for each queue */
	/* 11.  RQPN 0x200[31:0] = 0x80BD1C1C */
	/* load RQPN */
	rtl_write_byte(rtlpriv, REG_RQPN_NPQ, value8);
	rtl_write_dword(rtlpriv, REG_RQPN, value32);

	/* 12.  TXRKTBUG_PG_BNDY 0x114[31:0] = 0x27FF00F6 */
	/* TXRKTBUG_PG_BNDY */
	rtl_write_dword(rtlpriv, REG_TRXFF_BNDY,
			(rtl_read_word(rtlpriv, REG_TRXFF_BNDY + 2) << 16 |
			txpktbuf_bndy));

	/* 13.  TDECTRL[15:8] 0x209[7:0] = 0xF6 */
	/* Beacon Head for TXDMA */
	rtl_write_byte(rtlpriv, REG_TDECTRL + 1, txpktbuf_bndy);

	/* 14.  BCNQ_PGBNDY 0x424[7:0] =  0xF6 */
	/* BCNQ_PGBNDY */
	rtl_write_byte(rtlpriv, REG_TXPKTBUF_BCNQ_BDNY, txpktbuf_bndy);
	rtl_write_byte(rtlpriv, REG_TXPKTBUF_MGQ_BDNY, txpktbuf_bndy);

	/* 15.  WMAC_LBK_BF_HD 0x45D[7:0] =  0xF6 */
	/* WMAC_LBK_BF_HD */
	rtl_write_byte(rtlpriv, 0x45D, txpktbuf_bndy);

	/* Set Tx/Rx page size (Tx must be 128 Bytes, */
	/* Rx can be 64,128,256,512,1024 bytes) */
	/* 16.  PBP [7:0] = 0x11 */
	/* TRX page size */
	rtl_write_byte(rtlpriv, REG_PBP, 0x11);

	/* 17.  DRV_INFO_SZ = 0x04 */
	rtl_write_byte(rtlpriv, REG_RX_DRVINFO_SZ, 0x4);

	/* 18.  LLT_table_init(Adapter);  */
	for (i = 0; i < (txpktbuf_bndy - 1); i++) {
		status = _rtl92de_llt_write(hw, i, i + 1);
		if (true != status)
			return status;
	}

	/* end of list */
	status = _rtl92de_llt_write(hw, (txpktbuf_bndy - 1), 0xFF);
	if (true != status)
		return status;

	/* Make the other pages as ring buffer */
	/* This ring buffer is used as beacon buffer if we */
	/* config this MAC as two MAC transfer. */
	/* Otherwise used as local loopback buffer.  */
	for (i = txpktbuf_bndy; i < maxPage; i++) {
		status = _rtl92de_llt_write(hw, i, (i + 1));
		if (true != status)
			return status;
	}

	/* Let last entry point to the start entry of ring buffer */
	status = _rtl92de_llt_write(hw, maxPage, txpktbuf_bndy);
	if (true != status)
		return status;

	return true;
}

static void _rtl92de_gen_refresh_led_state(struct ieee80211_hw *hw)
{
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_led *pLed0 = &(pcipriv->ledctl.sw_led0);

	if (rtlpci->up_first_time)
		return;
	if (ppsc->rfoff_reason == RF_CHANGE_BY_IPS)
		rtl92de_sw_led_on(hw, pLed0);
	else if (ppsc->rfoff_reason == RF_CHANGE_BY_INIT)
		rtl92de_sw_led_on(hw, pLed0);
	else
		rtl92de_sw_led_off(hw, pLed0);
}

static bool _rtl92de_init_mac(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	unsigned char bytetmp;
	unsigned short wordtmp;
	u16 retry;

	rtl92d_phy_set_poweron(hw);
	/* Add for resume sequence of power domain according
	 * to power document V11. Chapter V.11....  */
	/* 0.   RSV_CTRL 0x1C[7:0] = 0x00  */
	/* unlock ISO/CLK/Power control register */
	rtl_write_byte(rtlpriv, REG_RSV_CTRL, 0x00);
	rtl_write_byte(rtlpriv, REG_LDOA15_CTRL, 0x05);

	/* 1.   AFE_XTAL_CTRL [7:0] = 0x0F  enable XTAL */
	/* 2.   SPS0_CTRL 0x11[7:0] = 0x2b  enable SPS into PWM mode  */
	/* 3.   delay (1ms) this is not necessary when initially power on */

	/* C.   Resume Sequence */
	/* a.   SPS0_CTRL 0x11[7:0] = 0x2b */
	rtl_write_byte(rtlpriv, REG_SPS0_CTRL, 0x2b);

	/* b.   AFE_XTAL_CTRL [7:0] = 0x0F */
	rtl_write_byte(rtlpriv, REG_AFE_XTAL_CTRL, 0x0F);

	/* c.   DRV runs power on init flow */

	/* auto enable WLAN */
	/* 4.   APS_FSMCO 0x04[8] = 1; wait till 0x04[8] = 0   */
	/* Power On Reset for MAC Block */
	bytetmp = rtl_read_byte(rtlpriv, REG_APS_FSMCO + 1) | BIT(0);
	udelay(2);
	rtl_write_byte(rtlpriv, REG_APS_FSMCO + 1, bytetmp);
	udelay(2);

	/* 5.   Wait while 0x04[8] == 0 goto 2, otherwise goto 1 */
	bytetmp = rtl_read_byte(rtlpriv, REG_APS_FSMCO + 1);
	udelay(50);
	retry = 0;
	while ((bytetmp & BIT(0)) && retry < 1000) {
		retry++;
		bytetmp = rtl_read_byte(rtlpriv, REG_APS_FSMCO + 1);
		udelay(50);
	}

	/* Enable Radio off, GPIO, and LED function */
	/* 6.   APS_FSMCO 0x04[15:0] = 0x0012  when enable HWPDN */
	rtl_write_word(rtlpriv, REG_APS_FSMCO, 0x1012);

	/* release RF digital isolation  */
	/* 7.  SYS_ISO_CTRL 0x01[1]    = 0x0;  */
	/*Set REG_SYS_ISO_CTRL 0x1=0x82 to prevent wake# problem. */
	rtl_write_byte(rtlpriv, REG_SYS_ISO_CTRL + 1, 0x82);
	udelay(2);

	/* make sure that BB reset OK. */
	/* rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE3); */

	/* Disable REG_CR before enable it to assure reset */
	rtl_write_word(rtlpriv, REG_CR, 0x0);

	/* Release MAC IO register reset */
	rtl_write_word(rtlpriv, REG_CR, 0x2ff);

	/* clear stopping tx/rx dma   */
	rtl_write_byte(rtlpriv, REG_PCIE_CTRL_REG + 1, 0x0);

	/* rtl_write_word(rtlpriv,REG_CR+2, 0x2); */

	/* System init */
	/* 18.  LLT_table_init(Adapter);  */
	if (!_rtl92de_llt_table_init(hw))
		return false;

	/* Clear interrupt and enable interrupt */
	/* 19.  HISR 0x124[31:0] = 0xffffffff;  */
	/*      HISRE 0x12C[7:0] = 0xFF */
	rtl_write_dword(rtlpriv, REG_HISR, 0xffffffff);
	rtl_write_byte(rtlpriv, REG_HISRE, 0xff);

	/* 20.  HIMR 0x120[31:0] |= [enable INT mask bit map];  */
	/* 21.  HIMRE 0x128[7:0] = [enable INT mask bit map] */
	/* The IMR should be enabled later after all init sequence
	 * is finished. */

	/* 22.  PCIE configuration space configuration */
	/* 23.  Ensure PCIe Device 0x80[15:0] = 0x0143 (ASPM+CLKREQ),  */
	/*      and PCIe gated clock function is enabled.    */
	/* PCIE configuration space will be written after
	 * all init sequence.(Or by BIOS) */

	rtl92d_phy_config_maccoexist_rfpage(hw);

	/* THe below section is not related to power document Vxx . */
	/* This is only useful for driver and OS setting. */
	/* -------------------Software Relative Setting---------------------- */
	wordtmp = rtl_read_word(rtlpriv, REG_TRXDMA_CTRL);
	wordtmp &= 0xf;
	wordtmp |= 0xF771;
	rtl_write_word(rtlpriv, REG_TRXDMA_CTRL, wordtmp);

	/* Reported Tx status from HW for rate adaptive. */
	/* This should be realtive to power on step 14. But in document V11  */
	/* still not contain the description.!!! */
	rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 1, 0x1F);

	/* Set Tx/Rx page size (Tx must be 128 Bytes,
	 * Rx can be 64,128,256,512,1024 bytes) */
	/* rtl_write_byte(rtlpriv,REG_PBP, 0x11); */

	/* Set RCR register */
	rtl_write_dword(rtlpriv, REG_RCR, rtlpci->receive_config);
	/* rtl_write_byte(rtlpriv,REG_RX_DRVINFO_SZ, 4); */

	/*  Set TCR register */
	rtl_write_dword(rtlpriv, REG_TCR, rtlpci->transmit_config);

	/* disable earlymode */
	rtl_write_byte(rtlpriv, 0x4d0, 0x0);

	/* Set TX/RX descriptor physical address(from OS API). */
	rtl_write_dword(rtlpriv, REG_BCNQ_DESA,
			rtlpci->tx_ring[BEACON_QUEUE].dma);
	rtl_write_dword(rtlpriv, REG_MGQ_DESA, rtlpci->tx_ring[MGNT_QUEUE].dma);
	rtl_write_dword(rtlpriv, REG_VOQ_DESA, rtlpci->tx_ring[VO_QUEUE].dma);
	rtl_write_dword(rtlpriv, REG_VIQ_DESA, rtlpci->tx_ring[VI_QUEUE].dma);
	rtl_write_dword(rtlpriv, REG_BEQ_DESA, rtlpci->tx_ring[BE_QUEUE].dma);
	rtl_write_dword(rtlpriv, REG_BKQ_DESA, rtlpci->tx_ring[BK_QUEUE].dma);
	rtl_write_dword(rtlpriv, REG_HQ_DESA, rtlpci->tx_ring[HIGH_QUEUE].dma);
	/* Set RX Desc Address */
	rtl_write_dword(rtlpriv, REG_RX_DESA,
			rtlpci->rx_ring[RX_MPDU_QUEUE].dma);

	/* if we want to support 64 bit DMA, we should set it here,
	 * but now we do not support 64 bit DMA*/

	rtl_write_byte(rtlpriv, REG_PCIE_CTRL_REG + 3, 0x33);

	/* Reset interrupt migration setting when initialization */
	rtl_write_dword(rtlpriv, REG_INT_MIG, 0);

	/* Reconsider when to do this operation after asking HWSD. */
	bytetmp = rtl_read_byte(rtlpriv, REG_APSD_CTRL);
	rtl_write_byte(rtlpriv, REG_APSD_CTRL, bytetmp & ~BIT(6));
	do {
		retry++;
		bytetmp = rtl_read_byte(rtlpriv, REG_APSD_CTRL);
	} while ((retry < 200) && !(bytetmp & BIT(7)));

	/* After MACIO reset,we must refresh LED state. */
	_rtl92de_gen_refresh_led_state(hw);

	/* Reset H2C protection register */
	rtl_write_dword(rtlpriv, REG_MCUTST_1, 0x0);

	return true;
}

static void _rtl92de_hw_configure(struct ieee80211_hw *hw)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 reg_bw_opmode = BW_OPMODE_20MHZ;
	u32 reg_rrsr;

	reg_rrsr = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
	rtl_write_byte(rtlpriv, REG_INIRTS_RATE_SEL, 0x8);
	rtl_write_byte(rtlpriv, REG_BWOPMODE, reg_bw_opmode);
	rtl_write_dword(rtlpriv, REG_RRSR, reg_rrsr);
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
	/* Aggregation threshold */
	if (rtlhal->macphymode == DUALMAC_DUALPHY)
		rtl_write_dword(rtlpriv, REG_AGGLEN_LMT, 0xb9726641);
	else if (rtlhal->macphymode == DUALMAC_SINGLEPHY)
		rtl_write_dword(rtlpriv, REG_AGGLEN_LMT, 0x66626641);
	else
		rtl_write_dword(rtlpriv, REG_AGGLEN_LMT, 0xb972a841);
	rtl_write_byte(rtlpriv, REG_ATIMWND, 0x2);
	rtl_write_byte(rtlpriv, REG_BCN_MAX_ERR, 0x0a);
	rtlpci->reg_bcn_ctrl_val = 0x1f;
	rtl_write_byte(rtlpriv, REG_BCN_CTRL, rtlpci->reg_bcn_ctrl_val);
	rtl_write_byte(rtlpriv, REG_TBTT_PROHIBIT + 1, 0xff);
	rtl_write_byte(rtlpriv, REG_PIFS, 0x1C);
	rtl_write_byte(rtlpriv, REG_AGGR_BREAK_TIME, 0x16);
	rtl_write_word(rtlpriv, REG_NAV_PROT_LEN, 0x0020);
	/* For throughput */
	rtl_write_word(rtlpriv, REG_FAST_EDCA_CTRL, 0x6666);
	/* ACKTO for IOT issue. */
	rtl_write_byte(rtlpriv, REG_ACKTO, 0x40);
	/* Set Spec SIFS (used in NAV) */
	rtl_write_word(rtlpriv, REG_SPEC_SIFS, 0x1010);
	rtl_write_word(rtlpriv, REG_MAC_SPEC_SIFS, 0x1010);
	/* Set SIFS for CCK */
	rtl_write_word(rtlpriv, REG_SIFS_CTX, 0x1010);
	/* Set SIFS for OFDM */
	rtl_write_word(rtlpriv, REG_SIFS_TRX, 0x1010);
	/* Set Multicast Address. */
	rtl_write_dword(rtlpriv, REG_MAR, 0xffffffff);
	rtl_write_dword(rtlpriv, REG_MAR + 4, 0xffffffff);
	switch (rtlpriv->phy.rf_type) {
	case RF_1T2R:
	case RF_1T1R:
		rtlhal->minspace_cfg = (MAX_MSS_DENSITY_1T << 3);
		break;
	case RF_2T2R:
	case RF_2T2R_GREEN:
		rtlhal->minspace_cfg = (MAX_MSS_DENSITY_2T << 3);
		break;
	}
}

static void _rtl92de_enable_aspm_back_door(struct ieee80211_hw *hw)
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

void rtl92de_enable_hw_security_config(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 sec_reg_value;

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
	sec_reg_value |= (SCR_RXBCUSEDK | SCR_TXBCUSEDK);
	rtl_write_byte(rtlpriv, REG_CR + 1, 0x02);
	RT_TRACE(rtlpriv, COMP_SEC, DBG_LOUD,
		 "The SECR-value %x\n", sec_reg_value);
	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_WPA_CONFIG, &sec_reg_value);
}

int rtl92de_hw_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	bool rtstatus = true;
	u8 tmp_u1b;
	int i;
	int err;
	unsigned long flags;

	rtlpci->being_init_adapter = true;
	rtlpci->init_ready = false;
	spin_lock_irqsave(&globalmutex_for_power_and_efuse, flags);
	/* we should do iqk after disable/enable */
	rtl92d_phy_reset_iqk_result(hw);
	/* rtlpriv->intf_ops->disable_aspm(hw); */
	rtstatus = _rtl92de_init_mac(hw);
	if (!rtstatus) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "Init MAC failed\n");
		err = 1;
		spin_unlock_irqrestore(&globalmutex_for_power_and_efuse, flags);
		return err;
	}
	err = rtl92d_download_fw(hw);
	spin_unlock_irqrestore(&globalmutex_for_power_and_efuse, flags);
	if (err) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 "Failed to download FW. Init HW without FW..\n");
		return 1;
	}
	rtlhal->last_hmeboxnum = 0;
	rtlpriv->psc.fw_current_inpsmode = false;

	tmp_u1b = rtl_read_byte(rtlpriv, 0x605);
	tmp_u1b = tmp_u1b | 0x30;
	rtl_write_byte(rtlpriv, 0x605, tmp_u1b);

	if (rtlhal->earlymode_enable) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "EarlyMode Enabled!!!\n");

		tmp_u1b = rtl_read_byte(rtlpriv, 0x4d0);
		tmp_u1b = tmp_u1b | 0x1f;
		rtl_write_byte(rtlpriv, 0x4d0, tmp_u1b);

		rtl_write_byte(rtlpriv, 0x4d3, 0x80);

		tmp_u1b = rtl_read_byte(rtlpriv, 0x605);
		tmp_u1b = tmp_u1b | 0x40;
		rtl_write_byte(rtlpriv, 0x605, tmp_u1b);
	}

	if (mac->rdg_en) {
		rtl_write_byte(rtlpriv, REG_RD_CTRL, 0xff);
		rtl_write_word(rtlpriv, REG_RD_NAV_NXT, 0x200);
		rtl_write_byte(rtlpriv, REG_RD_RESP_PKT_TH, 0x05);
	}

	rtl92d_phy_mac_config(hw);
	/* because last function modify RCR, so we update
	 * rcr var here, or TP will unstable for receive_config
	 * is wrong, RX RCR_ACRC32 will cause TP unstabel & Rx
	 * RCR_APP_ICV will cause mac80211 unassoc for cisco 1252*/
	rtlpci->receive_config = rtl_read_dword(rtlpriv, REG_RCR);
	rtlpci->receive_config &= ~(RCR_ACRC32 | RCR_AICV);

	rtl92d_phy_bb_config(hw);

	rtlphy->rf_mode = RF_OP_BY_SW_3WIRE;
	/* set before initialize RF */
	rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER4, 0x00f00000, 0xf);

	/* config RF */
	rtl92d_phy_rf_config(hw);

	/* After read predefined TXT, we must set BB/MAC/RF
	 * register as our requirement */
	/* After load BB,RF params,we need do more for 92D. */
	rtl92d_update_bbrf_configuration(hw);
	/* set default value after initialize RF,  */
	rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER4, 0x00f00000, 0);
	rtlphy->rfreg_chnlval[0] = rtl_get_rfreg(hw, (enum radio_path)0,
			RF_CHNLBW, BRFREGOFFSETMASK);
	rtlphy->rfreg_chnlval[1] = rtl_get_rfreg(hw, (enum radio_path)1,
			RF_CHNLBW, BRFREGOFFSETMASK);

	/*---- Set CCK and OFDM Block "ON"----*/
	if (rtlhal->current_bandtype == BAND_ON_2_4G)
		rtl_set_bbreg(hw, RFPGA0_RFMOD, BCCKEN, 0x1);
	rtl_set_bbreg(hw, RFPGA0_RFMOD, BOFDMEN, 0x1);
	if (rtlhal->interfaceindex == 0) {
		/* RFPGA0_ANALOGPARAMETER2: cck clock select,
		 *  set to 20MHz by default */
		rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER2, BIT(10) |
			      BIT(11), 3);
	} else {
		/* Mac1 */
		rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER2, BIT(11) |
			      BIT(10), 3);
	}

	_rtl92de_hw_configure(hw);

	/* reset hw sec */
	rtl_cam_reset_all_entry(hw);
	rtl92de_enable_hw_security_config(hw);

	/* Read EEPROM TX power index and PHY_REG_PG.txt to capture correct */
	/* TX power index for different rate set. */
	rtl92d_phy_get_hw_reg_originalvalue(hw);
	rtl92d_phy_set_txpower_level(hw, rtlphy->current_channel);

	ppsc->rfpwr_state = ERFON;

	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_ETHER_ADDR, mac->mac_addr);

	_rtl92de_enable_aspm_back_door(hw);
	/* rtlpriv->intf_ops->enable_aspm(hw); */

	rtl92d_dm_init(hw);
	rtlpci->being_init_adapter = false;

	if (ppsc->rfpwr_state == ERFON) {
		rtl92d_phy_lc_calibrate(hw);
		/* 5G and 2.4G must wait sometime to let RF LO ready */
		if (rtlhal->macphymode == DUALMAC_DUALPHY) {
			u32 tmp_rega;
			for (i = 0; i < 10000; i++) {
				udelay(MAX_STALL_TIME);

				tmp_rega = rtl_get_rfreg(hw,
						  (enum radio_path)RF90_PATH_A,
						  0x2a, BMASKDWORD);

				if (((tmp_rega & BIT(11)) == BIT(11)))
					break;
			}
			/* check that loop was successful. If not, exit now */
			if (i == 10000) {
				rtlpci->init_ready = false;
				return 1;
			}
		}
	}
	rtlpci->init_ready = true;
	return err;
}

static enum version_8192d _rtl92de_read_chip_version(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	enum version_8192d version = VERSION_NORMAL_CHIP_92D_SINGLEPHY;
	u32 value32;

	value32 = rtl_read_dword(rtlpriv, REG_SYS_CFG);
	if (!(value32 & 0x000f0000)) {
		version = VERSION_TEST_CHIP_92D_SINGLEPHY;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "TEST CHIP!!!\n");
	} else {
		version = VERSION_NORMAL_CHIP_92D_SINGLEPHY;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "Normal CHIP!!!\n");
	}
	return version;
}

static int _rtl92de_set_media_status(struct ieee80211_hw *hw,
				     enum nl80211_iftype type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 bt_msr = rtl_read_byte(rtlpriv, MSR);
	enum led_ctl_mode ledaction = LED_CTL_NO_LINK;
	u8 bcnfunc_enable;

	bt_msr &= 0xfc;

	if (type == NL80211_IFTYPE_UNSPECIFIED ||
	    type == NL80211_IFTYPE_STATION) {
		_rtl92de_stop_tx_beacon(hw);
		_rtl92de_enable_bcn_sub_func(hw);
	} else if (type == NL80211_IFTYPE_ADHOC ||
		type == NL80211_IFTYPE_AP) {
		_rtl92de_resume_tx_beacon(hw);
		_rtl92de_disable_bcn_sub_func(hw);
	} else {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 "Set HW_VAR_MEDIA_STATUS: No such media status(%x)\n",
			 type);
	}
	bcnfunc_enable = rtl_read_byte(rtlpriv, REG_BCN_CTRL);
	switch (type) {
	case NL80211_IFTYPE_UNSPECIFIED:
		bt_msr |= MSR_NOLINK;
		ledaction = LED_CTL_LINK;
		bcnfunc_enable &= 0xF7;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "Set Network type to NO LINK!\n");
		break;
	case NL80211_IFTYPE_ADHOC:
		bt_msr |= MSR_ADHOC;
		bcnfunc_enable |= 0x08;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "Set Network type to Ad Hoc!\n");
		break;
	case NL80211_IFTYPE_STATION:
		bt_msr |= MSR_INFRA;
		ledaction = LED_CTL_LINK;
		bcnfunc_enable &= 0xF7;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "Set Network type to STA!\n");
		break;
	case NL80211_IFTYPE_AP:
		bt_msr |= MSR_AP;
		bcnfunc_enable |= 0x08;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "Set Network type to AP!\n");
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "Network type %d not supported!\n", type);
		return 1;
		break;

	}
	rtl_write_byte(rtlpriv, REG_CR + 2, bt_msr);
	rtlpriv->cfg->ops->led_control(hw, ledaction);
	if ((bt_msr & 0xfc) == MSR_AP)
		rtl_write_byte(rtlpriv, REG_BCNTCFG + 1, 0x00);
	else
		rtl_write_byte(rtlpriv, REG_BCNTCFG + 1, 0x66);
	return 0;
}

void rtl92de_set_check_bssid(struct ieee80211_hw *hw, bool check_bssid)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u32 reg_rcr = rtlpci->receive_config;

	if (rtlpriv->psc.rfpwr_state != ERFON)
		return;
	if (check_bssid) {
		reg_rcr |= (RCR_CBSSID_DATA | RCR_CBSSID_BCN);
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_RCR, (u8 *)(&reg_rcr));
		_rtl92de_set_bcn_ctrl_reg(hw, 0, BIT(4));
	} else if (!check_bssid) {
		reg_rcr &= (~(RCR_CBSSID_DATA | RCR_CBSSID_BCN));
		_rtl92de_set_bcn_ctrl_reg(hw, BIT(4), 0);
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_RCR, (u8 *)(&reg_rcr));
	}
}

int rtl92de_set_network_type(struct ieee80211_hw *hw, enum nl80211_iftype type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (_rtl92de_set_media_status(hw, type))
		return -EOPNOTSUPP;

	/* check bssid */
	if (rtlpriv->mac80211.link_state == MAC80211_LINKED) {
		if (type != NL80211_IFTYPE_AP)
			rtl92de_set_check_bssid(hw, true);
	} else {
		rtl92de_set_check_bssid(hw, false);
	}
	return 0;
}

/* do iqk or reload iqk */
/* windows just rtl92d_phy_reload_iqk_setting in set channel,
 * but it's very strict for time sequence so we add
 * rtl92d_phy_reload_iqk_setting here */
void rtl92d_linked_set_reg(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u8 indexforchannel;
	u8 channel = rtlphy->current_channel;

	indexforchannel = rtl92d_get_rightchnlplace_for_iqk(channel);
	if (!rtlphy->iqk_matrix_regsetting[indexforchannel].iqk_done) {
		RT_TRACE(rtlpriv, COMP_SCAN | COMP_INIT, DBG_DMESG,
			 "Do IQK for channel:%d\n", channel);
		rtl92d_phy_iq_calibrate(hw);
	}
}

/* don't set REG_EDCA_BE_PARAM here because
 * mac80211 will send pkt when scan */
void rtl92de_set_qos(struct ieee80211_hw *hw, int aci)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	rtl92d_dm_init_edca_turbo(hw);
	return;
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

void rtl92de_enable_interrupt(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	rtl_write_dword(rtlpriv, REG_HIMR, rtlpci->irq_mask[0] & 0xFFFFFFFF);
	rtl_write_dword(rtlpriv, REG_HIMRE, rtlpci->irq_mask[1] & 0xFFFFFFFF);
}

void rtl92de_disable_interrupt(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	rtl_write_dword(rtlpriv, REG_HIMR, IMR8190_DISABLED);
	rtl_write_dword(rtlpriv, REG_HIMRE, IMR8190_DISABLED);
	synchronize_irq(rtlpci->pdev->irq);
}

static void _rtl92de_poweroff_adapter(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 u1b_tmp;
	unsigned long flags;

	rtlpriv->intf_ops->enable_aspm(hw);
	rtl_write_byte(rtlpriv, REG_RF_CTRL, 0x00);
	rtl_set_bbreg(hw, RFPGA0_XCD_RFPARAMETER, BIT(3), 0);
	rtl_set_bbreg(hw, RFPGA0_XCD_RFPARAMETER, BIT(15), 0);

	/* 0x20:value 05-->04 */
	rtl_write_byte(rtlpriv, REG_LDOA15_CTRL, 0x04);

	/*  ==== Reset digital sequence   ====== */
	rtl92d_firmware_selfreset(hw);

	/* f.   SYS_FUNC_EN 0x03[7:0]=0x51 reset MCU, MAC register, DCORE */
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN + 1, 0x51);

	/* g.   MCUFWDL 0x80[1:0]=0 reset MCU ready status */
	rtl_write_byte(rtlpriv, REG_MCUFWDL, 0x00);

	/*  ==== Pull GPIO PIN to balance level and LED control ====== */

	/* h.     GPIO_PIN_CTRL 0x44[31:0]=0x000  */
	rtl_write_dword(rtlpriv, REG_GPIO_PIN_CTRL, 0x00000000);

	/* i.    Value = GPIO_PIN_CTRL[7:0] */
	u1b_tmp = rtl_read_byte(rtlpriv, REG_GPIO_PIN_CTRL);

	/* j.    GPIO_PIN_CTRL 0x44[31:0] = 0x00FF0000 | (value <<8); */
	/* write external PIN level  */
	rtl_write_dword(rtlpriv, REG_GPIO_PIN_CTRL,
			0x00FF0000 | (u1b_tmp << 8));

	/* k.   GPIO_MUXCFG 0x42 [15:0] = 0x0780 */
	rtl_write_word(rtlpriv, REG_GPIO_IO_SEL, 0x0790);

	/* l.   LEDCFG 0x4C[15:0] = 0x8080 */
	rtl_write_word(rtlpriv, REG_LEDCFG0, 0x8080);

	/*  ==== Disable analog sequence === */

	/* m.   AFE_PLL_CTRL[7:0] = 0x80  disable PLL */
	rtl_write_byte(rtlpriv, REG_AFE_PLL_CTRL, 0x80);

	/* n.   SPS0_CTRL 0x11[7:0] = 0x22  enter PFM mode */
	rtl_write_byte(rtlpriv, REG_SPS0_CTRL, 0x23);

	/* o.   AFE_XTAL_CTRL 0x24[7:0] = 0x0E  disable XTAL, if No BT COEX */
	rtl_write_byte(rtlpriv, REG_AFE_XTAL_CTRL, 0x0e);

	/* p.   RSV_CTRL 0x1C[7:0] = 0x0E lock ISO/CLK/Power control register */
	rtl_write_byte(rtlpriv, REG_RSV_CTRL, 0x0e);

	/*  ==== interface into suspend === */

	/* q.   APS_FSMCO[15:8] = 0x58 PCIe suspend mode */
	/* According to power document V11, we need to set this */
	/* value as 0x18. Otherwise, we may not L0s sometimes. */
	/* This indluences power consumption. Bases on SD1's test, */
	/* set as 0x00 do not affect power current. And if it */
	/* is set as 0x18, they had ever met auto load fail problem. */
	rtl_write_byte(rtlpriv, REG_APS_FSMCO + 1, 0x10);

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "In PowerOff,reg0x%x=%X\n",
		 REG_SPS0_CTRL, rtl_read_byte(rtlpriv, REG_SPS0_CTRL));
	/* r.   Note: for PCIe interface, PON will not turn */
	/* off m-bias and BandGap in PCIe suspend mode.  */

	/* 0x17[7] 1b': power off in process  0b' : power off over */
	if (rtlpriv->rtlhal.macphymode != SINGLEMAC_SINGLEPHY) {
		spin_lock_irqsave(&globalmutex_power, flags);
		u1b_tmp = rtl_read_byte(rtlpriv, REG_POWER_OFF_IN_PROCESS);
		u1b_tmp &= (~BIT(7));
		rtl_write_byte(rtlpriv, REG_POWER_OFF_IN_PROCESS, u1b_tmp);
		spin_unlock_irqrestore(&globalmutex_power, flags);
	}

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "<=======\n");
}

void rtl92de_card_disable(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	enum nl80211_iftype opmode;

	mac->link_state = MAC80211_NOLINK;
	opmode = NL80211_IFTYPE_UNSPECIFIED;
	_rtl92de_set_media_status(hw, opmode);

	if (rtlpci->driver_is_goingto_unload ||
	    ppsc->rfoff_reason > RF_CHANGE_BY_PS)
		rtlpriv->cfg->ops->led_control(hw, LED_CTL_POWER_OFF);
	RT_SET_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC);
	/* Power sequence for each MAC. */
	/* a. stop tx DMA  */
	/* b. close RF */
	/* c. clear rx buf */
	/* d. stop rx DMA */
	/* e.  reset MAC */

	/* a. stop tx DMA */
	rtl_write_byte(rtlpriv, REG_PCIE_CTRL_REG + 1, 0xFE);
	udelay(50);

	/* b. TXPAUSE 0x522[7:0] = 0xFF Pause MAC TX queue */

	/* c. ========RF OFF sequence==========  */
	/* 0x88c[23:20] = 0xf. */
	rtl_set_bbreg(hw, RFPGA0_ANALOGPARAMETER4, 0x00f00000, 0xf);
	rtl_set_rfreg(hw, RF90_PATH_A, 0x00, BRFREGOFFSETMASK, 0x00);

	/* APSD_CTRL 0x600[7:0] = 0x40 */
	rtl_write_byte(rtlpriv, REG_APSD_CTRL, 0x40);

	/* Close antenna 0,0xc04,0xd04 */
	rtl_set_bbreg(hw, ROFDM0_TRXPATHENABLE, BMASKBYTE0, 0);
	rtl_set_bbreg(hw, ROFDM1_TRXPATHENABLE, BDWORD, 0);

	/*  SYS_FUNC_EN 0x02[7:0] = 0xE2   reset BB state machine */
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE2);

	/* Mac0 can not do Global reset. Mac1 can do. */
	/* SYS_FUNC_EN 0x02[7:0] = 0xE0  reset BB state machine  */
	if (rtlpriv->rtlhal.interfaceindex == 1)
		rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE0);
	udelay(50);

	/* d.  stop tx/rx dma before disable REG_CR (0x100) to fix */
	/* dma hang issue when disable/enable device.  */
	rtl_write_byte(rtlpriv, REG_PCIE_CTRL_REG + 1, 0xff);
	udelay(50);
	rtl_write_byte(rtlpriv, REG_CR, 0x0);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "==> Do power off.......\n");
	if (rtl92d_phy_check_poweroff(hw))
		_rtl92de_poweroff_adapter(hw);
	return;
}

void rtl92de_interrupt_recognized(struct ieee80211_hw *hw,
				  u32 *p_inta, u32 *p_intb)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	*p_inta = rtl_read_dword(rtlpriv, ISR) & rtlpci->irq_mask[0];
	rtl_write_dword(rtlpriv, ISR, *p_inta);

	/*
	 * *p_intb = rtl_read_dword(rtlpriv, REG_HISRE) & rtlpci->irq_mask[1];
	 * rtl_write_dword(rtlpriv, ISR + 4, *p_intb);
	 */
}

void rtl92de_set_beacon_related_registers(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u16 bcn_interval, atim_window;

	bcn_interval = mac->beacon_interval;
	atim_window = 2;
	/*rtl92de_disable_interrupt(hw);  */
	rtl_write_word(rtlpriv, REG_ATIMWND, atim_window);
	rtl_write_word(rtlpriv, REG_BCN_INTERVAL, bcn_interval);
	rtl_write_word(rtlpriv, REG_BCNTCFG, 0x660f);
	rtl_write_byte(rtlpriv, REG_RXTSF_OFFSET_CCK, 0x20);
	if (rtlpriv->rtlhal.current_bandtype == BAND_ON_5G)
		rtl_write_byte(rtlpriv, REG_RXTSF_OFFSET_OFDM, 0x30);
	else
		rtl_write_byte(rtlpriv, REG_RXTSF_OFFSET_OFDM, 0x20);
	rtl_write_byte(rtlpriv, 0x606, 0x30);
}

void rtl92de_set_beacon_interval(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u16 bcn_interval = mac->beacon_interval;

	RT_TRACE(rtlpriv, COMP_BEACON, DBG_DMESG,
		 "beacon_interval:%d\n", bcn_interval);
	/* rtl92de_disable_interrupt(hw); */
	rtl_write_word(rtlpriv, REG_BCN_INTERVAL, bcn_interval);
	/* rtl92de_enable_interrupt(hw); */
}

void rtl92de_update_interrupt_mask(struct ieee80211_hw *hw,
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
	rtl92de_disable_interrupt(hw);
	rtl92de_enable_interrupt(hw);
}

static void _rtl92de_readpowervalue_fromprom(struct txpower_info *pwrinfo,
				 u8 *rom_content, bool autoLoadfail)
{
	u32 rfpath, eeaddr, group, offset1, offset2;
	u8 i;

	memset(pwrinfo, 0, sizeof(struct txpower_info));
	if (autoLoadfail) {
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
	 * If we find it, we set it to default value. */
	for (rfpath = 0; rfpath < RF6052_MAX_PATH; rfpath++) {
		for (group = 0; group < CHANNEL_GROUP_MAX_2G; group++) {
			eeaddr = EEPROM_CCK_TX_PWR_INX_2G + (rfpath * 3)
				 + group;
			pwrinfo->cck_index[rfpath][group] =
					(rom_content[eeaddr] == 0xFF) ?
					     (eeaddr > 0x7B ?
					     EEPROM_DEFAULT_TXPOWERLEVEL_5G :
					     EEPROM_DEFAULT_TXPOWERLEVEL_2G) :
					     rom_content[eeaddr];
		}
	}
	for (rfpath = 0; rfpath < RF6052_MAX_PATH; rfpath++) {
		for (group = 0; group < CHANNEL_GROUP_MAX; group++) {
			offset1 = group / 3;
			offset2 = group % 3;
			eeaddr = EEPROM_HT40_1S_TX_PWR_INX_2G + (rfpath * 3) +
			    offset2 + offset1 * 21;
			pwrinfo->ht40_1sindex[rfpath][group] =
			    (rom_content[eeaddr] == 0xFF) ? (eeaddr > 0x7B ?
					     EEPROM_DEFAULT_TXPOWERLEVEL_5G :
					     EEPROM_DEFAULT_TXPOWERLEVEL_2G) :
						 rom_content[eeaddr];
		}
	}
	/* These just for 92D efuse offset. */
	for (group = 0; group < CHANNEL_GROUP_MAX; group++) {
		for (rfpath = 0; rfpath < RF6052_MAX_PATH; rfpath++) {
			int base1 = EEPROM_HT40_2S_TX_PWR_INX_DIFF_2G;

			offset1 = group / 3;
			offset2 = group % 3;

			if (rom_content[base1 + offset2 + offset1 * 21] != 0xFF)
				pwrinfo->ht40_2sindexdiff[rfpath][group] =
				    (rom_content[base1 +
				     offset2 + offset1 * 21] >> (rfpath * 4))
				     & 0xF;
			else
				pwrinfo->ht40_2sindexdiff[rfpath][group] =
				    EEPROM_DEFAULT_HT40_2SDIFF;
			if (rom_content[EEPROM_HT20_TX_PWR_INX_DIFF_2G + offset2
			    + offset1 * 21] != 0xFF)
				pwrinfo->ht20indexdiff[rfpath][group] =
				    (rom_content[EEPROM_HT20_TX_PWR_INX_DIFF_2G
				    + offset2 + offset1 * 21] >> (rfpath * 4))
				    & 0xF;
			else
				pwrinfo->ht20indexdiff[rfpath][group] =
				    EEPROM_DEFAULT_HT20_DIFF;
			if (rom_content[EEPROM_OFDM_TX_PWR_INX_DIFF_2G + offset2
			    + offset1 * 21] != 0xFF)
				pwrinfo->ofdmindexdiff[rfpath][group] =
				    (rom_content[EEPROM_OFDM_TX_PWR_INX_DIFF_2G
				     + offset2 + offset1 * 21] >> (rfpath * 4))
				     & 0xF;
			else
				pwrinfo->ofdmindexdiff[rfpath][group] =
				    EEPROM_DEFAULT_LEGACYHTTXPOWERDIFF;
			if (rom_content[EEPROM_HT40_MAX_PWR_OFFSET_2G + offset2
			    + offset1 * 21] != 0xFF)
				pwrinfo->ht40maxoffset[rfpath][group] =
				    (rom_content[EEPROM_HT40_MAX_PWR_OFFSET_2G
				    + offset2 + offset1 * 21] >> (rfpath * 4))
				    & 0xF;
			else
				pwrinfo->ht40maxoffset[rfpath][group] =
				    EEPROM_DEFAULT_HT40_PWRMAXOFFSET;
			if (rom_content[EEPROM_HT20_MAX_PWR_OFFSET_2G + offset2
			    + offset1 * 21] != 0xFF)
				pwrinfo->ht20maxoffset[rfpath][group] =
				    (rom_content[EEPROM_HT20_MAX_PWR_OFFSET_2G +
				     offset2 + offset1 * 21] >> (rfpath * 4)) &
				     0xF;
			else
				pwrinfo->ht20maxoffset[rfpath][group] =
				    EEPROM_DEFAULT_HT20_PWRMAXOFFSET;
		}
	}
	if (rom_content[EEPROM_TSSI_A_5G] != 0xFF) {
		/* 5GL */
		pwrinfo->tssi_a[0] = rom_content[EEPROM_TSSI_A_5G] & 0x3F;
		pwrinfo->tssi_b[0] = rom_content[EEPROM_TSSI_B_5G] & 0x3F;
		/* 5GM */
		pwrinfo->tssi_a[1] = rom_content[EEPROM_TSSI_AB_5G] & 0x3F;
		pwrinfo->tssi_b[1] =
		    (rom_content[EEPROM_TSSI_AB_5G] & 0xC0) >> 6 |
		    (rom_content[EEPROM_TSSI_AB_5G + 1] & 0x0F) << 2;
		/* 5GH */
		pwrinfo->tssi_a[2] = (rom_content[EEPROM_TSSI_AB_5G + 1] &
				      0xF0) >> 4 |
		    (rom_content[EEPROM_TSSI_AB_5G + 2] & 0x03) << 4;
		pwrinfo->tssi_b[2] = (rom_content[EEPROM_TSSI_AB_5G + 2] &
				      0xFC) >> 2;
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
	u32 ch, rfPath, group;

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
			RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
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
		tempval[0] = tempval[1] = 3;
	}

	/* Use default value to fill parameters if
	 * efuse is not filled on some place. */

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
	RT_TRACE(rtlpriv, COMP_INTR, DBG_LOUD,
		 "EEPROMRegulatory = 0x%x\n", rtlefuse->eeprom_regulatory);
	RT_TRACE(rtlpriv, COMP_INTR, DBG_LOUD,
		 "ThermalMeter = 0x%x\n", rtlefuse->eeprom_thermalmeter);
	RT_TRACE(rtlpriv, COMP_INTR, DBG_LOUD,
		 "CrystalCap = 0x%x\n", rtlefuse->crystalcap);
	RT_TRACE(rtlpriv, COMP_INTR, DBG_LOUD,
		 "Delta_IQK = 0x%x Delta_LCK = 0x%x\n",
		 rtlefuse->delta_iqk, rtlefuse->delta_lck);

	for (rfPath = 0; rfPath < RF6052_MAX_PATH; rfPath++) {
		for (ch = 0; ch < CHANNEL_MAX_NUMBER; ch++) {
			group = rtl92d_get_chnlgroup_fromarray((u8) ch);
			if (ch < CHANNEL_MAX_NUMBER_2G)
				rtlefuse->txpwrlevel_cck[rfPath][ch] =
				    pwrinfo.cck_index[rfPath][group];
			rtlefuse->txpwrlevel_ht40_1s[rfPath][ch] =
				    pwrinfo.ht40_1sindex[rfPath][group];
			rtlefuse->txpwr_ht20diff[rfPath][ch] =
				    pwrinfo.ht20indexdiff[rfPath][group];
			rtlefuse->txpwr_legacyhtdiff[rfPath][ch] =
				    pwrinfo.ofdmindexdiff[rfPath][group];
			rtlefuse->pwrgroup_ht20[rfPath][ch] =
				    pwrinfo.ht20maxoffset[rfPath][group];
			rtlefuse->pwrgroup_ht40[rfPath][ch] =
				    pwrinfo.ht40maxoffset[rfPath][group];
			pwr = pwrinfo.ht40_1sindex[rfPath][group];
			diff = pwrinfo.ht40_2sindexdiff[rfPath][group];
			rtlefuse->txpwrlevel_ht40_2s[rfPath][ch] =
				    (pwr > diff) ? (pwr - diff) : 0;
		}
	}
}

static void _rtl92de_read_macphymode_from_prom(struct ieee80211_hw *hw,
					       u8 *content)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 macphy_crvalue = content[EEPROM_MAC_FUNCTION];

	if (macphy_crvalue & BIT(3)) {
		rtlhal->macphymode = SINGLEMAC_SINGLEPHY;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "MacPhyMode SINGLEMAC_SINGLEPHY\n");
	} else {
		rtlhal->macphymode = DUALMAC_DUALPHY;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
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

	rtlpriv->intf_ops->read_efuse_byte(hw, EEPROME_CHIP_VERSION_H,
					   &cutvalue[1]);
	rtlpriv->intf_ops->read_efuse_byte(hw, EEPROME_CHIP_VERSION_L,
					   &cutvalue[0]);
	chipvalue = (cutvalue[1] << 8) | cutvalue[0];
	switch (chipvalue) {
	case 0xAA55:
		chipver |= CHIP_92D_C_CUT;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "C-CUT!!!\n");
		break;
	case 0x9966:
		chipver |= CHIP_92D_D_CUT;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "D-CUT!!!\n");
		break;
	case 0xCC33:
		chipver |= CHIP_92D_E_CUT;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "E-CUT!!!\n");
		break;
	default:
		chipver |= CHIP_92D_D_CUT;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_EMERG, "Unknown CUT!\n");
		break;
	}
	rtlpriv->rtlhal.version = chipver;
}

static void _rtl92de_read_adapter_info(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u16 i, usvalue;
	u8 hwinfo[HWSET_MAX_SIZE];
	u16 eeprom_id;
	unsigned long flags;

	if (rtlefuse->epromtype == EEPROM_BOOT_EFUSE) {
		spin_lock_irqsave(&globalmutex_for_power_and_efuse, flags);
		rtl_efuse_shadow_map_update(hw);
		_rtl92de_efuse_update_chip_version(hw);
		spin_unlock_irqrestore(&globalmutex_for_power_and_efuse, flags);
		memcpy((void *)hwinfo, (void *)&rtlefuse->efuse_map
		       [EFUSE_INIT_MAP][0],
		       HWSET_MAX_SIZE);
	} else if (rtlefuse->epromtype == EEPROM_93C46) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "RTL819X Not boot from eeprom, check it !!\n");
	}
	RT_PRINT_DATA(rtlpriv, COMP_INIT, DBG_DMESG, "MAP",
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
	if (rtlefuse->autoload_failflag) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "RTL819X Not boot from eeprom, check it !!\n");
		return;
	}
	rtlefuse->eeprom_oemid = hwinfo[EEPROM_CUSTOMER_ID];
	_rtl92de_read_macphymode_and_bandtype(hw, hwinfo);

	/* VID, DID  SE     0xA-D */
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

	/* Read Permanent MAC address */
	if (rtlhal->interfaceindex == 0) {
		for (i = 0; i < 6; i += 2) {
			usvalue = *(u16 *)&hwinfo[EEPROM_MAC_ADDR_MAC0_92D + i];
			*((u16 *) (&rtlefuse->dev_addr[i])) = usvalue;
		}
	} else {
		for (i = 0; i < 6; i += 2) {
			usvalue = *(u16 *)&hwinfo[EEPROM_MAC_ADDR_MAC1_92D + i];
			*((u16 *) (&rtlefuse->dev_addr[i])) = usvalue;
		}
	}
	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_ETHER_ADDR,
				      rtlefuse->dev_addr);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "%pM\n", rtlefuse->dev_addr);
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
	rtlefuse->eeprom_version = *(u16 *)&hwinfo[EEPROM_VERSION];
	rtlefuse->txpwr_fromeprom = true;
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "EEPROM Customer ID: 0x%2x\n", rtlefuse->eeprom_oemid);
}

void rtl92de_read_eeprom_info(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 tmp_u1b;

	rtlhal->version = _rtl92de_read_chip_version(hw);
	tmp_u1b = rtl_read_byte(rtlpriv, REG_9346CR);
	rtlefuse->autoload_status = tmp_u1b;
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
		_rtl92de_read_adapter_info(hw);
	} else {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "Autoload ERR!!\n");
	}
	return;
}

static void rtl92de_update_hal_rate_table(struct ieee80211_hw *hw,
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
	u16 shortgi_rate;
	u32 tmp_ratr_value;
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
	ratr_value |= (sta->ht_cap.mcs.rx_mask[1] << 20 |
		       sta->ht_cap.mcs.rx_mask[0] << 12);
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
	RT_TRACE(rtlpriv, COMP_RATR, DBG_DMESG, "%x\n",
		 rtl_read_dword(rtlpriv, REG_ARFR0));
}

static void rtl92de_update_hal_rate_mask(struct ieee80211_hw *hw,
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
	u32 value[2];
	u8 macid = 0;
	u8 mimo_ps = IEEE80211_SMPS_OFF;

	sta_entry = (struct rtl_sta_info *) sta->drv_priv;
	mimo_ps = sta_entry->mimo_ps;
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

	value[0] = (ratr_bitmap & 0x0fffffff) | (ratr_index << 28);
	value[1] = macid | (shortgi ? 0x20 : 0x00) | 0x80;
	RT_TRACE(rtlpriv, COMP_RATR, DBG_DMESG,
		 "ratr_bitmap :%x value0:%x value1:%x\n",
		 ratr_bitmap, value[0], value[1]);
	rtl92d_fill_h2c_cmd(hw, H2C_RA_MASK, 5, (u8 *) value);
	if (macid != 0)
		sta_entry->ratr_index = ratr_index;
}

void rtl92de_update_hal_rate_tbl(struct ieee80211_hw *hw,
		struct ieee80211_sta *sta, u8 rssi_level)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (rtlpriv->dm.useramask)
		rtl92de_update_hal_rate_mask(hw, sta, rssi_level);
	else
		rtl92de_update_hal_rate_table(hw, sta);
}

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

bool rtl92de_gpio_radio_on_off_checking(struct ieee80211_hw *hw, u8 *valid)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	enum rf_pwrstate e_rfpowerstate_toset;
	u8 u1tmp;
	bool actuallyset = false;
	unsigned long flag;

	if (rtlpci->being_init_adapter)
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
	rtl_write_byte(rtlpriv, REG_MAC_PINMUX_CFG, rtl_read_byte(rtlpriv,
			  REG_MAC_PINMUX_CFG) & ~(BIT(3)));
	u1tmp = rtl_read_byte(rtlpriv, REG_GPIO_IO_SEL);
	e_rfpowerstate_toset = (u1tmp & BIT(3)) ? ERFON : ERFOFF;
	if (ppsc->hwradiooff && (e_rfpowerstate_toset == ERFON)) {
		RT_TRACE(rtlpriv, COMP_RF, DBG_DMESG,
			 "GPIOChangeRF  - HW Radio ON, RF ON\n");
		e_rfpowerstate_toset = ERFON;
		ppsc->hwradiooff = false;
		actuallyset = true;
	} else if (!ppsc->hwradiooff && (e_rfpowerstate_toset == ERFOFF)) {
		RT_TRACE(rtlpriv, COMP_RF, DBG_DMESG,
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

void rtl92de_set_key(struct ieee80211_hw *hw, u32 key_index,
		     u8 *p_macaddr, bool is_group, u8 enc_algo,
		     bool is_wepkey, bool clear_all)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u8 *macaddr = p_macaddr;
	u32 entry_id;
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
		u8 idx;
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
			if (mac->opmode == NL80211_IFTYPE_AP)
				rtl_cam_del_entry(hw, p_macaddr);
			rtl_cam_delete_one_entry(hw, p_macaddr, entry_id);
		} else {
			RT_TRACE(rtlpriv, COMP_SEC, DBG_LOUD,
				 "The insert KEY length is %d\n",
				 rtlpriv->sec.key_len[PAIRWISE_KEYIDX]);
			RT_TRACE(rtlpriv, COMP_SEC, DBG_LOUD,
				 "The insert KEY is %x %x\n",
				 rtlpriv->sec.key_buf[0][0],
				 rtlpriv->sec.key_buf[0][1]);
			RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG,
				 "add one entry\n");
			if (is_pairwise) {
				RT_PRINT_DATA(rtlpriv, COMP_SEC, DBG_LOUD,
					      "Pairwise Key content",
					      rtlpriv->sec.pairwise_key,
					      rtlpriv->
					      sec.key_len[PAIRWISE_KEYIDX]);
				RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG,
					 "set Pairwise key\n");
				rtl_cam_add_one_entry(hw, macaddr, key_index,
						      entry_id, enc_algo,
						      CAM_CONFIG_NO_USEDK,
						      rtlpriv->
						      sec.key_buf[key_index]);
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
						rtlpriv->sec.key_buf
						[entry_id]);
			}
		}
	}
}

void rtl92de_suspend(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpriv->rtlhal.macphyctl_reg = rtl_read_byte(rtlpriv,
		REG_MAC_PHY_CTRL_NORMAL);
}

void rtl92de_resume(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_write_byte(rtlpriv, REG_MAC_PHY_CTRL_NORMAL,
		       rtlpriv->rtlhal.macphyctl_reg);
}
