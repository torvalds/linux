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
#include "../rtl8192c/dm_common.h"
#include "../rtl8192c/fw_common.h"
#include "../rtl8192c/phy_common.h"
#include "dm.h"
#include "led.h"
#include "hw.h"

#define LLT_CONFIG	5

static void _rtl92ce_set_bcn_ctrl_reg(struct ieee80211_hw *hw,
				      u8 set_bits, u8 clear_bits)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpci->reg_bcn_ctrl_val |= set_bits;
	rtlpci->reg_bcn_ctrl_val &= ~clear_bits;

	rtl_write_byte(rtlpriv, REG_BCN_CTRL, (u8)rtlpci->reg_bcn_ctrl_val);
}

static void _rtl92ce_stop_tx_beacon(struct ieee80211_hw *hw)
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

static void _rtl92ce_resume_tx_beacon(struct ieee80211_hw *hw)
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

static void _rtl92ce_enable_bcn_sub_func(struct ieee80211_hw *hw)
{
	_rtl92ce_set_bcn_ctrl_reg(hw, 0, BIT(1));
}

static void _rtl92ce_disable_bcn_sub_func(struct ieee80211_hw *hw)
{
	_rtl92ce_set_bcn_ctrl_reg(hw, BIT(1), 0);
}

void rtl92ce_get_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val)
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
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "switch case not processed\n");
		break;
	}
}

void rtl92ce_set_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
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
			u16 rate_cfg = ((u16 *) val)[0];
			u8 rate_index = 0;
			rate_cfg &= 0x15f;
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
			break;
		}
	case HW_VAR_BSSID:{
			for (idx = 0; idx < ETH_ALEN; idx++) {
				rtl_write_byte(rtlpriv, (REG_BSSID + idx),
					       val[idx]);
			}
			break;
		}
	case HW_VAR_SIFS:{
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
		}
	case HW_VAR_SLOT_TIME:{
			u8 e_aci;

			RT_TRACE(rtlpriv, COMP_MLME, DBG_LOUD,
				 "HW_VAR_SLOT_TIME %x\n", val[0]);

			rtl_write_byte(rtlpriv, REG_SLOT, val[0]);

			for (e_aci = 0; e_aci < AC_MAX; e_aci++) {
				rtlpriv->cfg->ops->set_hw_reg(hw,
							      HW_VAR_AC_PARAM,
							      &e_aci);
			}
			break;
		}
	case HW_VAR_ACK_PREAMBLE:{
			u8 reg_tmp;
			u8 short_preamble = (bool)*val;
			reg_tmp = (mac->cur_40_prime_sc) << 5;
			if (short_preamble)
				reg_tmp |= 0x80;

			rtl_write_byte(rtlpriv, REG_RRSR + 2, reg_tmp);
			break;
		}
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
			break;
		}
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
			u8 regtoset_normal[4] = {0x41, 0xa8, 0x72, 0xb9};
			u8 regtoset_bt[4] = {0x31, 0x74, 0x42, 0x97};

			u8 factor_toset;
			u8 *p_regtoset = NULL;
			u8 index = 0;

			if ((rtlpcipriv->bt_coexist.bt_coexistence) &&
			    (rtlpcipriv->bt_coexist.bt_coexist_type ==
			    BT_CSR_BC4))
				p_regtoset = regtoset_bt;
			else
				p_regtoset = regtoset_normal;

			factor_toset = *(val);
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
			break;
		}
	case HW_VAR_AC_PARAM:{
			u8 e_aci = *(val);
			rtl92c_dm_init_edca_turbo(hw);

			if (rtlpci->acm_method != EACMWAY2_SW)
				rtlpriv->cfg->ops->set_hw_reg(hw,
							      HW_VAR_ACM_CTRL,
							      (&e_aci));
			break;
		}
	case HW_VAR_ACM_CTRL:{
			u8 e_aci = *(val);
			union aci_aifsn *p_aci_aifsn =
			    (union aci_aifsn *)(&(mac->ac[0].aifs));
			u8 acm = p_aci_aifsn->f.acm;
			u8 acm_ctrl = rtl_read_byte(rtlpriv, REG_ACMHWCTRL);

			acm_ctrl =
			    acm_ctrl | ((rtlpci->acm_method == 2) ? 0x0 : 0x1);

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
	case HW_VAR_RCR:{
			rtl_write_dword(rtlpriv, REG_RCR, ((u32 *) (val))[0]);
			rtlpci->receive_config = ((u32 *) (val))[0];
			break;
		}
	case HW_VAR_RETRY_LIMIT:{
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
		rtl92c_phy_set_io_cmd(hw, (*(enum io_type *)val));
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
				rtl_write_byte(rtlpriv, REG_PCIE_HRPWM,
					       *val | BIT(7));
			}

			break;
		}
	case HW_VAR_H2C_FW_PWRMODE:{
			u8 psmode = *val;

			if ((psmode != FW_PS_ACTIVE_MODE) &&
			    (!IS_92C_SERIAL(rtlhal->version))) {
				rtl92c_dm_rf_saving(hw, true);
			}

			rtl92c_set_fw_pwrmode_cmd(hw, *val);
			break;
		}
	case HW_VAR_FW_PSMODE_STATUS:
		ppsc->fw_current_inpsmode = *((bool *) val);
		break;
	case HW_VAR_H2C_FW_JOINBSSRPT:{
			u8 mstatus = *val;
			u8 tmp_regcr, tmp_reg422;
			bool recover = false;

			if (mstatus == RT_MEDIA_CONNECT) {
				rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_AID,
							      NULL);

				tmp_regcr = rtl_read_byte(rtlpriv, REG_CR + 1);
				rtl_write_byte(rtlpriv, REG_CR + 1,
					       (tmp_regcr | BIT(0)));

				_rtl92ce_set_bcn_ctrl_reg(hw, 0, BIT(3));
				_rtl92ce_set_bcn_ctrl_reg(hw, BIT(4), 0);

				tmp_reg422 =
				    rtl_read_byte(rtlpriv,
						  REG_FWHW_TXQ_CTRL + 2);
				if (tmp_reg422 & BIT(6))
					recover = true;
				rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 2,
					       tmp_reg422 & (~BIT(6)));

				rtl92c_set_fw_rsvdpagepkt(hw, NULL);

				_rtl92ce_set_bcn_ctrl_reg(hw, BIT(3), 0);
				_rtl92ce_set_bcn_ctrl_reg(hw, 0, BIT(4));

				if (recover) {
					rtl_write_byte(rtlpriv,
						       REG_FWHW_TXQ_CTRL + 2,
						       tmp_reg422);
				}

				rtl_write_byte(rtlpriv, REG_CR + 1,
					       (tmp_regcr & ~(BIT(0))));
			}
			rtl92c_set_fw_joinbss_report_cmd(hw, *val);

			break;
		}
	case HW_VAR_H2C_FW_P2P_PS_OFFLOAD:
		rtl92c_set_p2p_ps_offload_cmd(hw, *val);
		break;
	case HW_VAR_AID:{
			u16 u2btmp;
			u2btmp = rtl_read_word(rtlpriv, REG_BCN_PSR_RPT);
			u2btmp &= 0xC000;
			rtl_write_word(rtlpriv, REG_BCN_PSR_RPT, (u2btmp |
						mac->assoc_id));

			break;
		}
	case HW_VAR_CORRECT_TSF:{
			u8 btype_ibss = val[0];

			if (btype_ibss)
				_rtl92ce_stop_tx_beacon(hw);

			_rtl92ce_set_bcn_ctrl_reg(hw, 0, BIT(3));

			rtl_write_dword(rtlpriv, REG_TSFTR,
					(u32) (mac->tsf & 0xffffffff));
			rtl_write_dword(rtlpriv, REG_TSFTR + 4,
					(u32) ((mac->tsf >> 32) & 0xffffffff));

			_rtl92ce_set_bcn_ctrl_reg(hw, BIT(3), 0);

			if (btype_ibss)
				_rtl92ce_resume_tx_beacon(hw);

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

				rtlpriv->cfg->ops->set_hw_reg(hw,
							      HW_VAR_SET_RPWM,
							      &rpwm_val);
			} else {
				rpwm_val = 0x0C;	/* RF on */
				fw_pwrmode = FW_PS_ACTIVE_MODE;
				fw_current_inps = false;
				rtlpriv->cfg->ops->set_hw_reg(hw,
							      HW_VAR_SET_RPWM,
							      &rpwm_val);
				rtlpriv->cfg->ops->set_hw_reg(hw,
						HW_VAR_H2C_FW_PWRMODE,
						&fw_pwrmode);

				rtlpriv->cfg->ops->set_hw_reg(hw,
						HW_VAR_FW_PSMODE_STATUS,
						(u8 *)(&fw_current_inps));
			}
		break; }
	case HW_VAR_KEEP_ALIVE: {
		u8 array[2];

		array[0] = 0xff;
		array[1] = *((u8 *)val);
		rtl92c_fill_h2c_cmd(hw, H2C_92C_KEEP_ALIVE_CTRL, 2, array);
		break; }
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "switch case %d not processed\n", variable);
		break;
	}
}

static bool _rtl92ce_llt_write(struct ieee80211_hw *hw, u32 address, u32 data)
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

static bool _rtl92ce_llt_table_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	unsigned short i;
	u8 txpktbuf_bndy;
	u8 maxPage;
	bool status;

#if LLT_CONFIG == 1
	maxPage = 255;
	txpktbuf_bndy = 252;
#elif LLT_CONFIG == 2
	maxPage = 127;
	txpktbuf_bndy = 124;
#elif LLT_CONFIG == 3
	maxPage = 255;
	txpktbuf_bndy = 174;
#elif LLT_CONFIG == 4
	maxPage = 255;
	txpktbuf_bndy = 246;
#elif LLT_CONFIG == 5
	maxPage = 255;
	txpktbuf_bndy = 246;
#endif

#if LLT_CONFIG == 1
	rtl_write_byte(rtlpriv, REG_RQPN_NPQ, 0x1c);
	rtl_write_dword(rtlpriv, REG_RQPN, 0x80a71c1c);
#elif LLT_CONFIG == 2
	rtl_write_dword(rtlpriv, REG_RQPN, 0x845B1010);
#elif LLT_CONFIG == 3
	rtl_write_dword(rtlpriv, REG_RQPN, 0x84838484);
#elif LLT_CONFIG == 4
	rtl_write_dword(rtlpriv, REG_RQPN, 0x80bd1c1c);
#elif LLT_CONFIG == 5
	rtl_write_word(rtlpriv, REG_RQPN_NPQ, 0x0000);

	rtl_write_dword(rtlpriv, REG_RQPN, 0x80b01c29);
#endif

	rtl_write_dword(rtlpriv, REG_TRXFF_BNDY, (0x27FF0000 | txpktbuf_bndy));
	rtl_write_byte(rtlpriv, REG_TDECTRL + 1, txpktbuf_bndy);

	rtl_write_byte(rtlpriv, REG_TXPKTBUF_BCNQ_BDNY, txpktbuf_bndy);
	rtl_write_byte(rtlpriv, REG_TXPKTBUF_MGQ_BDNY, txpktbuf_bndy);

	rtl_write_byte(rtlpriv, 0x45D, txpktbuf_bndy);
	rtl_write_byte(rtlpriv, REG_PBP, 0x11);
	rtl_write_byte(rtlpriv, REG_RX_DRVINFO_SZ, 0x4);

	for (i = 0; i < (txpktbuf_bndy - 1); i++) {
		status = _rtl92ce_llt_write(hw, i, i + 1);
		if (true != status)
			return status;
	}

	status = _rtl92ce_llt_write(hw, (txpktbuf_bndy - 1), 0xFF);
	if (true != status)
		return status;

	for (i = txpktbuf_bndy; i < maxPage; i++) {
		status = _rtl92ce_llt_write(hw, i, (i + 1));
		if (true != status)
			return status;
	}

	status = _rtl92ce_llt_write(hw, maxPage, txpktbuf_bndy);
	if (true != status)
		return status;

	return true;
}

static void _rtl92ce_gen_refresh_led_state(struct ieee80211_hw *hw)
{
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_led *pLed0 = &(pcipriv->ledctl.sw_led0);

	if (rtlpci->up_first_time)
		return;

	if (ppsc->rfoff_reason == RF_CHANGE_BY_IPS)
		rtl92ce_sw_led_on(hw, pLed0);
	else if (ppsc->rfoff_reason == RF_CHANGE_BY_INIT)
		rtl92ce_sw_led_on(hw, pLed0);
	else
		rtl92ce_sw_led_off(hw, pLed0);
}

static bool _rtl92ce_init_mac(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	unsigned char bytetmp;
	unsigned short wordtmp;
	u16 retry;

	rtl_write_byte(rtlpriv, REG_RSV_CTRL, 0x00);
	if (rtlpcipriv->bt_coexist.bt_coexistence) {
		u32 value32;
		value32 = rtl_read_dword(rtlpriv, REG_APS_FSMCO);
		value32 |= (SOP_ABG | SOP_AMB | XOP_BTCK);
		rtl_write_dword(rtlpriv, REG_APS_FSMCO, value32);
	}
	rtl_write_byte(rtlpriv, REG_SPS0_CTRL, 0x2b);
	rtl_write_byte(rtlpriv, REG_AFE_XTAL_CTRL, 0x0F);

	if (rtlpcipriv->bt_coexist.bt_coexistence) {
		u32 u4b_tmp = rtl_read_dword(rtlpriv, REG_AFE_XTAL_CTRL);

		u4b_tmp &= (~0x00024800);
		rtl_write_dword(rtlpriv, REG_AFE_XTAL_CTRL, u4b_tmp);
	}

	bytetmp = rtl_read_byte(rtlpriv, REG_APS_FSMCO + 1) | BIT(0);
	udelay(2);

	rtl_write_byte(rtlpriv, REG_APS_FSMCO + 1, bytetmp);
	udelay(2);

	bytetmp = rtl_read_byte(rtlpriv, REG_APS_FSMCO + 1);
	udelay(2);

	retry = 0;
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "reg0xec:%x:%x\n",
		 rtl_read_dword(rtlpriv, 0xEC), bytetmp);

	while ((bytetmp & BIT(0)) && retry < 1000) {
		retry++;
		udelay(50);
		bytetmp = rtl_read_byte(rtlpriv, REG_APS_FSMCO + 1);
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "reg0xec:%x:%x\n",
			 rtl_read_dword(rtlpriv, 0xEC), bytetmp);
		udelay(50);
	}

	rtl_write_word(rtlpriv, REG_APS_FSMCO, 0x1012);

	rtl_write_byte(rtlpriv, REG_SYS_ISO_CTRL + 1, 0x82);
	udelay(2);

	if (rtlpcipriv->bt_coexist.bt_coexistence) {
		bytetmp = rtl_read_byte(rtlpriv, REG_AFE_XTAL_CTRL+2) & 0xfd;
		rtl_write_byte(rtlpriv, REG_AFE_XTAL_CTRL+2, bytetmp);
	}

	rtl_write_word(rtlpriv, REG_CR, 0x2ff);

	if (!_rtl92ce_llt_table_init(hw))
		return false;

	rtl_write_dword(rtlpriv, REG_HISR, 0xffffffff);
	rtl_write_byte(rtlpriv, REG_HISRE, 0xff);

	rtl_write_word(rtlpriv, REG_TRXFF_BNDY + 2, 0x27ff);

	wordtmp = rtl_read_word(rtlpriv, REG_TRXDMA_CTRL);
	wordtmp &= 0xf;
	wordtmp |= 0xF771;
	rtl_write_word(rtlpriv, REG_TRXDMA_CTRL, wordtmp);

	rtl_write_byte(rtlpriv, REG_FWHW_TXQ_CTRL + 1, 0x1F);
	rtl_write_dword(rtlpriv, REG_RCR, rtlpci->receive_config);
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

	if (IS_92C_SERIAL(rtlhal->version))
		rtl_write_byte(rtlpriv, REG_PCIE_CTRL_REG + 3, 0x77);
	else
		rtl_write_byte(rtlpriv, REG_PCIE_CTRL_REG + 3, 0x22);

	rtl_write_dword(rtlpriv, REG_INT_MIG, 0);

	bytetmp = rtl_read_byte(rtlpriv, REG_APSD_CTRL);
	rtl_write_byte(rtlpriv, REG_APSD_CTRL, bytetmp & ~BIT(6));
	do {
		retry++;
		bytetmp = rtl_read_byte(rtlpriv, REG_APSD_CTRL);
	} while ((retry < 200) && (bytetmp & BIT(7)));

	_rtl92ce_gen_refresh_led_state(hw);

	rtl_write_dword(rtlpriv, REG_MCUTST_1, 0x0);

	return true;
}

static void _rtl92ce_hw_configure(struct ieee80211_hw *hw)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
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

	if ((rtlpcipriv->bt_coexist.bt_coexistence) &&
	    (rtlpcipriv->bt_coexist.bt_coexist_type == BT_CSR_BC4))
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

	if ((rtlpcipriv->bt_coexist.bt_coexistence) &&
	    (rtlpcipriv->bt_coexist.bt_coexist_type == BT_CSR_BC4)) {
		rtl_write_word(rtlpriv, REG_NAV_PROT_LEN, 0x0020);
		rtl_write_word(rtlpriv, REG_PROT_MODE_CTRL, 0x0402);
	} else {
		rtl_write_word(rtlpriv, REG_NAV_PROT_LEN, 0x0020);
		rtl_write_word(rtlpriv, REG_NAV_PROT_LEN, 0x0020);
	}

	if ((rtlpcipriv->bt_coexist.bt_coexistence) &&
	     (rtlpcipriv->bt_coexist.bt_coexist_type == BT_CSR_BC4))
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

}

static void _rtl92ce_enable_aspm_back_door(struct ieee80211_hw *hw)
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

void rtl92ce_enable_hw_security_config(struct ieee80211_hw *hw)
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

	sec_reg_value = SCR_TxEncEnable | SCR_RxDecEnable;

	if (rtlpriv->sec.use_defaultkey) {
		sec_reg_value |= SCR_TxUseDK;
		sec_reg_value |= SCR_RxUseDK;
	}

	sec_reg_value |= (SCR_RXBCUSEDK | SCR_TXBCUSEDK);

	rtl_write_byte(rtlpriv, REG_CR + 1, 0x02);

	RT_TRACE(rtlpriv, COMP_SEC, DBG_LOUD,
		 "The SECR-value %x\n", sec_reg_value);

	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_WPA_CONFIG, &sec_reg_value);

}

int rtl92ce_hw_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	bool rtstatus = true;
	bool is92c;
	int err;
	u8 tmp_u1b;
	unsigned long flags;

	rtlpci->being_init_adapter = true;

	/* Since this function can take a very long time (up to 350 ms)
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
	rtstatus = _rtl92ce_init_mac(hw);
	if (!rtstatus) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "Init MAC failed\n");
		err = 1;
		goto exit;
	}

	err = rtl92c_download_fw(hw);
	if (err) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 "Failed to download FW. Init HW without FW now..\n");
		err = 1;
		goto exit;
	}

	rtlhal->fw_ready = true;
	rtlhal->last_hmeboxnum = 0;
	rtl92c_phy_mac_config(hw);
	/* because last function modify RCR, so we update
	 * rcr var here, or TP will unstable for receive_config
	 * is wrong, RX RCR_ACRC32 will cause TP unstabel & Rx
	 * RCR_APP_ICV will cause mac80211 unassoc for cisco 1252*/
	rtlpci->receive_config = rtl_read_dword(rtlpriv, REG_RCR);
	rtlpci->receive_config &= ~(RCR_ACRC32 | RCR_AICV);
	rtl_write_dword(rtlpriv, REG_RCR, rtlpci->receive_config);
	rtl92c_phy_bb_config(hw);
	rtlphy->rf_mode = RF_OP_BY_SW_3WIRE;
	rtl92c_phy_rf_config(hw);
	if (IS_VENDOR_UMC_A_CUT(rtlhal->version) &&
	    !IS_92C_SERIAL(rtlhal->version)) {
		rtl_set_rfreg(hw, RF90_PATH_A, RF_RX_G1, MASKDWORD, 0x30255);
		rtl_set_rfreg(hw, RF90_PATH_A, RF_RX_G2, MASKDWORD, 0x50a00);
	} else if (IS_81XXC_VENDOR_UMC_B_CUT(rtlhal->version)) {
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
	_rtl92ce_hw_configure(hw);
	rtl_cam_reset_all_entry(hw);
	rtl92ce_enable_hw_security_config(hw);

	ppsc->rfpwr_state = ERFON;

	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_ETHER_ADDR, mac->mac_addr);
	_rtl92ce_enable_aspm_back_door(hw);
	rtlpriv->intf_ops->enable_aspm(hw);

	rtl8192ce_bt_hw_init(hw);

	if (ppsc->rfpwr_state == ERFON) {
		rtl92c_phy_set_rfpath_switch(hw, 1);
		if (rtlphy->iqk_initialized) {
			rtl92c_phy_iq_calibrate(hw, true);
		} else {
			rtl92c_phy_iq_calibrate(hw, false);
			rtlphy->iqk_initialized = true;
		}

		rtl92c_dm_check_txpower_tracking(hw);
		rtl92c_phy_lc_calibrate(hw);
	}

	is92c = IS_92C_SERIAL(rtlhal->version);
	tmp_u1b = efuse_read_1byte(hw, 0x1FA);
	if (!(tmp_u1b & BIT(0))) {
		rtl_set_rfreg(hw, RF90_PATH_A, 0x15, 0x0F, 0x05);
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE, "PA BIAS path A\n");
	}

	if (!(tmp_u1b & BIT(1)) && is92c) {
		rtl_set_rfreg(hw, RF90_PATH_B, 0x15, 0x0F, 0x05);
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE, "PA BIAS path B\n");
	}

	if (!(tmp_u1b & BIT(4))) {
		tmp_u1b = rtl_read_byte(rtlpriv, 0x16);
		tmp_u1b &= 0x0F;
		rtl_write_byte(rtlpriv, 0x16, tmp_u1b | 0x80);
		udelay(10);
		rtl_write_byte(rtlpriv, 0x16, tmp_u1b | 0x90);
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE, "under 1.5V\n");
	}
	rtl92c_dm_init(hw);
exit:
	local_irq_restore(flags);
	rtlpci->being_init_adapter = false;
	return err;
}

static enum version_8192c _rtl92ce_read_chip_version(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	enum version_8192c version = VERSION_UNKNOWN;
	u32 value32;
	const char *versionid;

	value32 = rtl_read_dword(rtlpriv, REG_SYS_CFG);
	if (value32 & TRP_VAUX_EN) {
		version = (value32 & TYPE_ID) ? VERSION_A_CHIP_92C :
			   VERSION_A_CHIP_88C;
	} else {
		version = (enum version_8192c) (CHIP_VER_B |
				((value32 & TYPE_ID) ? CHIP_92C_BITMASK : 0) |
				((value32 & VENDOR_ID) ? CHIP_VENDOR_UMC : 0));
		if ((!IS_CHIP_VENDOR_UMC(version)) && (value32 &
		     CHIP_VER_RTL_MASK)) {
			version = (enum version_8192c)(version |
				   ((((value32 & CHIP_VER_RTL_MASK) == BIT(12))
				   ? CHIP_VENDOR_UMC_B_CUT : CHIP_UNKNOWN) |
				   CHIP_VENDOR_UMC));
		}
		if (IS_92C_SERIAL(version)) {
			value32 = rtl_read_dword(rtlpriv, REG_HPON_FSM);
			version = (enum version_8192c)(version |
				   ((CHIP_BONDING_IDENTIFIER(value32)
				   == CHIP_BONDING_92C_1T2R) ?
				   RF_TYPE_1T2R : 0));
		}
	}

	switch (version) {
	case VERSION_B_CHIP_92C:
		versionid = "B_CHIP_92C";
		break;
	case VERSION_B_CHIP_88C:
		versionid = "B_CHIP_88C";
		break;
	case VERSION_A_CHIP_92C:
		versionid = "A_CHIP_92C";
		break;
	case VERSION_A_CHIP_88C:
		versionid = "A_CHIP_88C";
		break;
	case VERSION_NORMAL_UMC_CHIP_92C_1T2R_A_CUT:
		versionid = "A_CUT_92C_1T2R";
		break;
	case VERSION_NORMAL_UMC_CHIP_92C_A_CUT:
		versionid = "A_CUT_92C";
		break;
	case VERSION_NORMAL_UMC_CHIP_88C_A_CUT:
		versionid = "A_CUT_88C";
		break;
	case VERSION_NORMAL_UMC_CHIP_92C_1T2R_B_CUT:
		versionid = "B_CUT_92C_1T2R";
		break;
	case VERSION_NORMAL_UMC_CHIP_92C_B_CUT:
		versionid = "B_CUT_92C";
		break;
	case VERSION_NORMAL_UMC_CHIP_88C_B_CUT:
		versionid = "B_CUT_88C";
		break;
	default:
		versionid = "Unknown. Bug?";
		break;
	}

	RT_TRACE(rtlpriv, COMP_INIT, DBG_EMERG,
		 "Chip Version ID: %s\n", versionid);

	switch (version & 0x3) {
	case CHIP_88C:
		rtlphy->rf_type = RF_1T1R;
		break;
	case CHIP_92C:
		rtlphy->rf_type = RF_2T2R;
		break;
	case CHIP_92C_1T2R:
		rtlphy->rf_type = RF_1T2R;
		break;
	default:
		rtlphy->rf_type = RF_1T1R;
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "ERROR RF_Type is set!!\n");
		break;
	}

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "Chip RF Type: %s\n",
		 rtlphy->rf_type == RF_2T2R ? "RF_2T2R" : "RF_1T1R");

	return version;
}

static int _rtl92ce_set_media_status(struct ieee80211_hw *hw,
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
	case NL80211_IFTYPE_MESH_POINT:
		mode = MSR_ADHOC;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "Set Network type to Mesh Point!\n");
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "Network type %d not supported!\n", type);
		return 1;

	}

	/* MSR_INFRA == Link in infrastructure network;
	 * MSR_ADHOC == Link in ad hoc network;
	 * Therefore, check link state is necessary.
	 *
	 * MSR_AP == AP mode; link state does not matter here.
	 */
	if (mode != MSR_AP &&
	    rtlpriv->mac80211.link_state < MAC80211_LINKED) {
		mode = MSR_NOLINK;
		ledaction = LED_CTL_NO_LINK;
	}
	if (mode == MSR_NOLINK || mode == MSR_INFRA) {
		_rtl92ce_stop_tx_beacon(hw);
		_rtl92ce_enable_bcn_sub_func(hw);
	} else if (mode == MSR_ADHOC || mode == MSR_AP) {
		_rtl92ce_resume_tx_beacon(hw);
		_rtl92ce_disable_bcn_sub_func(hw);
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

void rtl92ce_set_check_bssid(struct ieee80211_hw *hw, bool check_bssid)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 reg_rcr;

	if (rtlpriv->psc.rfpwr_state != ERFON)
		return;

	rtlpriv->cfg->ops->get_hw_reg(hw, HW_VAR_RCR, (u8 *)(&reg_rcr));

	if (check_bssid) {
		reg_rcr |= (RCR_CBSSID_DATA | RCR_CBSSID_BCN);
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_RCR,
					      (u8 *) (&reg_rcr));
		_rtl92ce_set_bcn_ctrl_reg(hw, 0, BIT(4));
	} else if (!check_bssid) {
		reg_rcr &= (~(RCR_CBSSID_DATA | RCR_CBSSID_BCN));
		_rtl92ce_set_bcn_ctrl_reg(hw, BIT(4), 0);
		rtlpriv->cfg->ops->set_hw_reg(hw,
					      HW_VAR_RCR, (u8 *) (&reg_rcr));
	}

}

int rtl92ce_set_network_type(struct ieee80211_hw *hw, enum nl80211_iftype type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (_rtl92ce_set_media_status(hw, type))
		return -EOPNOTSUPP;

	if (rtlpriv->mac80211.link_state == MAC80211_LINKED) {
		if (type != NL80211_IFTYPE_AP &&
		    type != NL80211_IFTYPE_MESH_POINT)
			rtl92ce_set_check_bssid(hw, true);
	} else {
		rtl92ce_set_check_bssid(hw, false);
	}

	return 0;
}

/* don't set REG_EDCA_BE_PARAM here because mac80211 will send pkt when scan */
void rtl92ce_set_qos(struct ieee80211_hw *hw, int aci)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	rtl92c_dm_init_edca_turbo(hw);
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

void rtl92ce_enable_interrupt(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	rtl_write_dword(rtlpriv, REG_HIMR, rtlpci->irq_mask[0] & 0xFFFFFFFF);
	rtl_write_dword(rtlpriv, REG_HIMRE, rtlpci->irq_mask[1] & 0xFFFFFFFF);
	rtlpci->irq_enabled = true;
}

void rtl92ce_disable_interrupt(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	rtl_write_dword(rtlpriv, REG_HIMR, IMR8190_DISABLED);
	rtl_write_dword(rtlpriv, REG_HIMRE, IMR8190_DISABLED);
	rtlpci->irq_enabled = false;
}

static void _rtl92ce_poweroff_adapter(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	u8 u1b_tmp;
	u32 u4b_tmp;

	rtlpriv->intf_ops->enable_aspm(hw);
	rtl_write_byte(rtlpriv, REG_TXPAUSE, 0xFF);
	rtl_set_rfreg(hw, RF90_PATH_A, 0x00, RFREG_OFFSET_MASK, 0x00);
	rtl_write_byte(rtlpriv, REG_RF_CTRL, 0x00);
	rtl_write_byte(rtlpriv, REG_APSD_CTRL, 0x40);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE2);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN, 0xE0);
	if (rtl_read_byte(rtlpriv, REG_MCUFWDL) & BIT(7))
		rtl92c_firmware_selfreset(hw);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN + 1, 0x51);
	rtl_write_byte(rtlpriv, REG_MCUFWDL, 0x00);
	rtl_write_dword(rtlpriv, REG_GPIO_PIN_CTRL, 0x00000000);
	u1b_tmp = rtl_read_byte(rtlpriv, REG_GPIO_PIN_CTRL);
	if ((rtlpcipriv->bt_coexist.bt_coexistence) &&
	     ((rtlpcipriv->bt_coexist.bt_coexist_type == BT_CSR_BC4) ||
	     (rtlpcipriv->bt_coexist.bt_coexist_type == BT_CSR_BC8))) {
		rtl_write_dword(rtlpriv, REG_GPIO_PIN_CTRL, 0x00F30000 |
				(u1b_tmp << 8));
	} else {
		rtl_write_dword(rtlpriv, REG_GPIO_PIN_CTRL, 0x00FF0000 |
				(u1b_tmp << 8));
	}
	rtl_write_word(rtlpriv, REG_GPIO_IO_SEL, 0x0790);
	rtl_write_word(rtlpriv, REG_LEDCFG0, 0x8080);
	rtl_write_byte(rtlpriv, REG_AFE_PLL_CTRL, 0x80);
	if (!IS_81XXC_VENDOR_UMC_B_CUT(rtlhal->version))
		rtl_write_byte(rtlpriv, REG_SPS0_CTRL, 0x23);
	if (rtlpcipriv->bt_coexist.bt_coexistence) {
		u4b_tmp = rtl_read_dword(rtlpriv, REG_AFE_XTAL_CTRL);
		u4b_tmp |= 0x03824800;
		rtl_write_dword(rtlpriv, REG_AFE_XTAL_CTRL, u4b_tmp);
	} else {
		rtl_write_dword(rtlpriv, REG_AFE_XTAL_CTRL, 0x0e);
	}

	rtl_write_byte(rtlpriv, REG_RSV_CTRL, 0x0e);
	rtl_write_byte(rtlpriv, REG_APS_FSMCO + 1, 0x10);
}

void rtl92ce_card_disable(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	enum nl80211_iftype opmode;

	mac->link_state = MAC80211_NOLINK;
	opmode = NL80211_IFTYPE_UNSPECIFIED;
	_rtl92ce_set_media_status(hw, opmode);
	if (rtlpci->driver_is_goingto_unload ||
	    ppsc->rfoff_reason > RF_CHANGE_BY_PS)
		rtlpriv->cfg->ops->led_control(hw, LED_CTL_POWER_OFF);
	RT_SET_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC);
	_rtl92ce_poweroff_adapter(hw);

	/* after power off we should do iqk again */
	rtlpriv->phy.iqk_initialized = false;
}

void rtl92ce_interrupt_recognized(struct ieee80211_hw *hw,
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

void rtl92ce_set_beacon_related_registers(struct ieee80211_hw *hw)
{

	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u16 bcn_interval, atim_window;

	bcn_interval = mac->beacon_interval;
	atim_window = 2;	/*FIX MERGE */
	rtl92ce_disable_interrupt(hw);
	rtl_write_word(rtlpriv, REG_ATIMWND, atim_window);
	rtl_write_word(rtlpriv, REG_BCN_INTERVAL, bcn_interval);
	rtl_write_word(rtlpriv, REG_BCNTCFG, 0x660f);
	rtl_write_byte(rtlpriv, REG_RXTSF_OFFSET_CCK, 0x18);
	rtl_write_byte(rtlpriv, REG_RXTSF_OFFSET_OFDM, 0x18);
	rtl_write_byte(rtlpriv, 0x606, 0x30);
	rtl92ce_enable_interrupt(hw);
}

void rtl92ce_set_beacon_interval(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u16 bcn_interval = mac->beacon_interval;

	RT_TRACE(rtlpriv, COMP_BEACON, DBG_DMESG,
		 "beacon_interval:%d\n", bcn_interval);
	rtl92ce_disable_interrupt(hw);
	rtl_write_word(rtlpriv, REG_BCN_INTERVAL, bcn_interval);
	rtl92ce_enable_interrupt(hw);
}

void rtl92ce_update_interrupt_mask(struct ieee80211_hw *hw,
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
	rtl92ce_disable_interrupt(hw);
	rtl92ce_enable_interrupt(hw);
}

static void _rtl92ce_read_txpower_info_from_hwpg(struct ieee80211_hw *hw,
						 bool autoload_fail,
						 u8 *hwinfo)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u8 rf_path, index, tempval;
	u16 i;

	for (rf_path = 0; rf_path < 2; rf_path++) {
		for (i = 0; i < 3; i++) {
			if (!autoload_fail) {
				rtlefuse->
				    eeprom_chnlarea_txpwr_cck[rf_path][i] =
				    hwinfo[EEPROM_TXPOWERCCK + rf_path * 3 + i];
				rtlefuse->
				    eeprom_chnlarea_txpwr_ht40_1s[rf_path][i] =
				    hwinfo[EEPROM_TXPOWERHT40_1S + rf_path * 3 +
					   i];
			} else {
				rtlefuse->
				    eeprom_chnlarea_txpwr_cck[rf_path][i] =
				    EEPROM_DEFAULT_TXPOWERLEVEL;
				rtlefuse->
				    eeprom_chnlarea_txpwr_ht40_1s[rf_path][i] =
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
				"RF(%d) EEPROM CCK Area(%d) = 0x%x\n",
				rf_path, i,
				rtlefuse->
				eeprom_chnlarea_txpwr_cck[rf_path][i]);
	for (rf_path = 0; rf_path < 2; rf_path++)
		for (i = 0; i < 3; i++)
			RTPRINT(rtlpriv, FINIT, INIT_EEPROM,
				"RF(%d) EEPROM HT40 1S Area(%d) = 0x%x\n",
				rf_path, i,
				rtlefuse->
				eeprom_chnlarea_txpwr_ht40_1s[rf_path][i]);
	for (rf_path = 0; rf_path < 2; rf_path++)
		for (i = 0; i < 3; i++)
			RTPRINT(rtlpriv, FINIT, INIT_EEPROM,
				"RF(%d) EEPROM HT40 2S Diff Area(%d) = 0x%x\n",
				rf_path, i,
				rtlefuse->
				eprom_chnl_txpwr_ht40_2sdf[rf_path][i]);

	for (rf_path = 0; rf_path < 2; rf_path++) {
		for (i = 0; i < 14; i++) {
			index = rtl92c_get_chnl_group((u8)i);

			rtlefuse->txpwrlevel_cck[rf_path][i] =
			    rtlefuse->eeprom_chnlarea_txpwr_cck[rf_path][index];
			rtlefuse->txpwrlevel_ht40_1s[rf_path][i] =
			    rtlefuse->
			    eeprom_chnlarea_txpwr_ht40_1s[rf_path][index];

			if ((rtlefuse->
			     eeprom_chnlarea_txpwr_ht40_1s[rf_path][index] -
			     rtlefuse->
			     eprom_chnl_txpwr_ht40_2sdf[rf_path][index])
			    > 0) {
				rtlefuse->txpwrlevel_ht40_2s[rf_path][i] =
				    rtlefuse->
				    eeprom_chnlarea_txpwr_ht40_1s[rf_path]
				    [index] -
				    rtlefuse->
				    eprom_chnl_txpwr_ht40_2sdf[rf_path]
				    [index];
			} else {
				rtlefuse->txpwrlevel_ht40_2s[rf_path][i] = 0;
			}
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
			index = rtl92c_get_chnl_group((u8)i);

			if (rf_path == RF90_PATH_A) {
				rtlefuse->pwrgroup_ht20[rf_path][i] =
				    (rtlefuse->eeprom_pwrlimit_ht20[index]
				     & 0xf);
				rtlefuse->pwrgroup_ht40[rf_path][i] =
				    (rtlefuse->eeprom_pwrlimit_ht40[index]
				     & 0xf);
			} else if (rf_path == RF90_PATH_B) {
				rtlefuse->pwrgroup_ht20[rf_path][i] =
				    ((rtlefuse->eeprom_pwrlimit_ht20[index]
				      & 0xf0) >> 4);
				rtlefuse->pwrgroup_ht40[rf_path][i] =
				    ((rtlefuse->eeprom_pwrlimit_ht40[index]
				      & 0xf0) >> 4);
			}

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
		index = rtl92c_get_chnl_group((u8)i);

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

		index = rtl92c_get_chnl_group((u8)i);

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

	if (!autoload_fail)
		rtlefuse->eeprom_regulatory = (hwinfo[RF_OPTION1] & 0x7);
	else
		rtlefuse->eeprom_regulatory = 0;
	RTPRINT(rtlpriv, FINIT, INIT_TXPOWER,
		"eeprom_regulatory = 0x%x\n", rtlefuse->eeprom_regulatory);

	if (!autoload_fail) {
		rtlefuse->eeprom_tssi[RF90_PATH_A] = hwinfo[EEPROM_TSSI_A];
		rtlefuse->eeprom_tssi[RF90_PATH_B] = hwinfo[EEPROM_TSSI_B];
	} else {
		rtlefuse->eeprom_tssi[RF90_PATH_A] = EEPROM_DEFAULT_TSSI;
		rtlefuse->eeprom_tssi[RF90_PATH_B] = EEPROM_DEFAULT_TSSI;
	}
	RTPRINT(rtlpriv, FINIT, INIT_TXPOWER, "TSSI_A = 0x%x, TSSI_B = 0x%x\n",
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

static void _rtl92ce_read_adapter_info(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u16 i, usvalue;
	u8 hwinfo[HWSET_MAX_SIZE];
	u16 eeprom_id;

	if (rtlefuse->epromtype == EEPROM_BOOT_EFUSE) {
		rtl_efuse_shadow_map_update(hw);

		memcpy((void *)hwinfo,
		       (void *)&rtlefuse->efuse_map[EFUSE_INIT_MAP][0],
		       HWSET_MAX_SIZE);
	} else if (rtlefuse->epromtype == EEPROM_93C46) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "RTL819X Not boot from eeprom, check it !!");
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

	if (rtlefuse->autoload_failflag)
		return;

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

	for (i = 0; i < 6; i += 2) {
		usvalue = *(u16 *)&hwinfo[EEPROM_MAC_ADDR + i];
		*((u16 *) (&rtlefuse->dev_addr[i])) = usvalue;
	}

	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "%pM\n", rtlefuse->dev_addr);

	_rtl92ce_read_txpower_info_from_hwpg(hw,
					     rtlefuse->autoload_failflag,
					     hwinfo);

	rtl8192ce_read_bt_coexist_info_from_hwpg(hw,
						 rtlefuse->autoload_failflag,
						 hwinfo);

	rtlefuse->eeprom_channelplan = *&hwinfo[EEPROM_CHANNELPLAN];
	rtlefuse->eeprom_version = *(u16 *)&hwinfo[EEPROM_VERSION];
	rtlefuse->txpwr_fromeprom = true;
	rtlefuse->eeprom_oemid = *&hwinfo[EEPROM_CUSTOMER_ID];

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "EEPROM Customer ID: 0x%2x\n", rtlefuse->eeprom_oemid);

	/* set channel paln to world wide 13 */
	rtlefuse->channel_plan = COUNTRY_CODE_WORLD_WIDE_13;

	if (rtlhal->oem_id == RT_CID_DEFAULT) {
		switch (rtlefuse->eeprom_oemid) {
		case EEPROM_CID_DEFAULT:
			if (rtlefuse->eeprom_did == 0x8176) {
				if ((rtlefuse->eeprom_svid == 0x103C &&
				     rtlefuse->eeprom_smid == 0x1629))
					rtlhal->oem_id = RT_CID_819X_HP;
				else
					rtlhal->oem_id = RT_CID_DEFAULT;
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

static void _rtl92ce_hal_customized_behavior(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

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

void rtl92ce_read_eeprom_info(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 tmp_u1b;

	rtlhal->version = _rtl92ce_read_chip_version(hw);
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
		_rtl92ce_read_adapter_info(hw);
	} else {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "Autoload ERR!!\n");
	}
	_rtl92ce_hal_customized_behavior(hw);
}

static void rtl92ce_update_hal_rate_table(struct ieee80211_hw *hw,
		struct ieee80211_sta *sta)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u32 ratr_value;
	u8 ratr_index = 0;
	u8 nmode = mac->ht_enable;
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
		nmode = 1;
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

	if ((rtlpcipriv->bt_coexist.bt_coexistence) &&
	    (rtlpcipriv->bt_coexist.bt_coexist_type == BT_CSR_BC4) &&
	    (rtlpcipriv->bt_coexist.bt_cur_state) &&
	    (rtlpcipriv->bt_coexist.bt_ant_isolation) &&
	    ((rtlpcipriv->bt_coexist.bt_service == BT_SCO) ||
	    (rtlpcipriv->bt_coexist.bt_service == BT_BUSY)))
		ratr_value &= 0x0fffcfc0;
	else
		ratr_value &= 0x0FFFFFFF;

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
	}

	rtl_write_dword(rtlpriv, REG_ARFR0 + ratr_index * 4, ratr_value);

	RT_TRACE(rtlpriv, COMP_RATR, DBG_DMESG, "%x\n",
		 rtl_read_dword(rtlpriv, REG_ARFR0));
}

static void rtl92ce_update_hal_rate_mask(struct ieee80211_hw *hw,
		struct ieee80211_sta *sta, u8 rssi_level)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_sta_info *sta_entry = NULL;
	u32 ratr_bitmap;
	u8 ratr_index;
	u8 curtxbw_40mhz = (sta->ht_cap.cap &
			    IEEE80211_HT_CAP_SUP_WIDTH_20_40) ? 1 : 0;
	u8 curshortgi_40mhz = (sta->ht_cap.cap &
			       IEEE80211_HT_CAP_SGI_40) ?  1 : 0;
	u8 curshortgi_20mhz = (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_20) ?
				1 : 0;
	enum wireless_mode wirelessmode = 0;
	bool shortgi = false;
	u8 rate_mask[5];
	u8 macid = 0;

	sta_entry = (struct rtl_sta_info *) sta->drv_priv;
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
	case WIRELESS_MODE_A:
		ratr_index = RATR_INX_WIRELESS_A;
		ratr_bitmap &= 0x00000ff0;
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
	rate_mask[4] = macid | (shortgi ? 0x20 : 0x00) | 0x80;
	RT_TRACE(rtlpriv, COMP_RATR, DBG_DMESG,
		 "Rate_index:%x, ratr_val:%x, %5phC\n",
		 ratr_index, ratr_bitmap, rate_mask);
	rtl92c_fill_h2c_cmd(hw, H2C_RA_MASK, 5, rate_mask);
}

void rtl92ce_update_hal_rate_tbl(struct ieee80211_hw *hw,
		struct ieee80211_sta *sta, u8 rssi_level)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (rtlpriv->dm.useramask)
		rtl92ce_update_hal_rate_mask(hw, sta, rssi_level);
	else
		rtl92ce_update_hal_rate_table(hw, sta);
}

void rtl92ce_update_channel_access_setting(struct ieee80211_hw *hw)
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

bool rtl92ce_gpio_radio_on_off_checking(struct ieee80211_hw *hw, u8 *valid)
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
		       REG_MAC_PINMUX_CFG)&~(BIT(3)));

	u1tmp = rtl_read_byte(rtlpriv, REG_GPIO_IO_SEL);
	e_rfpowerstate_toset = (u1tmp & BIT(3)) ? ERFON : ERFOFF;

	if ((ppsc->hwradiooff) && (e_rfpowerstate_toset == ERFON)) {
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

void rtl92ce_set_key(struct ieee80211_hw *hw, u32 key_index,
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
					      rtlpriv->sec.
					      key_len[PAIRWISE_KEYIDX]);

				RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG,
					 "set Pairwise key\n");

				rtl_cam_add_one_entry(hw, macaddr, key_index,
						      entry_id, enc_algo,
						      CAM_CONFIG_NO_USEDK,
						      rtlpriv->sec.
						      key_buf[key_index]);
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

static void rtl8192ce_bt_var_init(struct ieee80211_hw *hw)
{
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);

	rtlpcipriv->bt_coexist.bt_coexistence =
			rtlpcipriv->bt_coexist.eeprom_bt_coexist;
	rtlpcipriv->bt_coexist.bt_ant_num =
			rtlpcipriv->bt_coexist.eeprom_bt_ant_num;
	rtlpcipriv->bt_coexist.bt_coexist_type =
			rtlpcipriv->bt_coexist.eeprom_bt_type;

	if (rtlpcipriv->bt_coexist.reg_bt_iso == 2)
		rtlpcipriv->bt_coexist.bt_ant_isolation =
			rtlpcipriv->bt_coexist.eeprom_bt_ant_isol;
	else
		rtlpcipriv->bt_coexist.bt_ant_isolation =
			rtlpcipriv->bt_coexist.reg_bt_iso;

	rtlpcipriv->bt_coexist.bt_radio_shared_type =
			rtlpcipriv->bt_coexist.eeprom_bt_radio_shared;

	if (rtlpcipriv->bt_coexist.bt_coexistence) {

		if (rtlpcipriv->bt_coexist.reg_bt_sco == 1)
			rtlpcipriv->bt_coexist.bt_service = BT_OTHER_ACTION;
		else if (rtlpcipriv->bt_coexist.reg_bt_sco == 2)
			rtlpcipriv->bt_coexist.bt_service = BT_SCO;
		else if (rtlpcipriv->bt_coexist.reg_bt_sco == 4)
			rtlpcipriv->bt_coexist.bt_service = BT_BUSY;
		else if (rtlpcipriv->bt_coexist.reg_bt_sco == 5)
			rtlpcipriv->bt_coexist.bt_service = BT_OTHERBUSY;
		else
			rtlpcipriv->bt_coexist.bt_service = BT_IDLE;

		rtlpcipriv->bt_coexist.bt_edca_ul = 0;
		rtlpcipriv->bt_coexist.bt_edca_dl = 0;
		rtlpcipriv->bt_coexist.bt_rssi_state = 0xff;
	}
}

void rtl8192ce_read_bt_coexist_info_from_hwpg(struct ieee80211_hw *hw,
					      bool auto_load_fail, u8 *hwinfo)
{
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
	u8 val;

	if (!auto_load_fail) {
		rtlpcipriv->bt_coexist.eeprom_bt_coexist =
					((hwinfo[RF_OPTION1] & 0xe0) >> 5);
		val = hwinfo[RF_OPTION4];
		rtlpcipriv->bt_coexist.eeprom_bt_type = ((val & 0xe) >> 1);
		rtlpcipriv->bt_coexist.eeprom_bt_ant_num = (val & 0x1);
		rtlpcipriv->bt_coexist.eeprom_bt_ant_isol = ((val & 0x10) >> 4);
		rtlpcipriv->bt_coexist.eeprom_bt_radio_shared =
							 ((val & 0x20) >> 5);
	} else {
		rtlpcipriv->bt_coexist.eeprom_bt_coexist = 0;
		rtlpcipriv->bt_coexist.eeprom_bt_type = BT_2WIRE;
		rtlpcipriv->bt_coexist.eeprom_bt_ant_num = ANT_X2;
		rtlpcipriv->bt_coexist.eeprom_bt_ant_isol = 0;
		rtlpcipriv->bt_coexist.eeprom_bt_radio_shared = BT_RADIO_SHARED;
	}

	rtl8192ce_bt_var_init(hw);
}

void rtl8192ce_bt_reg_init(struct ieee80211_hw *hw)
{
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);

	/* 0:Low, 1:High, 2:From Efuse. */
	rtlpcipriv->bt_coexist.reg_bt_iso = 2;
	/* 0:Idle, 1:None-SCO, 2:SCO, 3:From Counter. */
	rtlpcipriv->bt_coexist.reg_bt_sco = 3;
	/* 0:Disable BT control A-MPDU, 1:Enable BT control A-MPDU. */
	rtlpcipriv->bt_coexist.reg_bt_sco = 0;
}


void rtl8192ce_bt_hw_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);

	u8 u1_tmp;

	if (rtlpcipriv->bt_coexist.bt_coexistence &&
	    ((rtlpcipriv->bt_coexist.bt_coexist_type == BT_CSR_BC4) ||
	      rtlpcipriv->bt_coexist.bt_coexist_type == BT_CSR_BC8)) {

		if (rtlpcipriv->bt_coexist.bt_ant_isolation)
			rtl_write_byte(rtlpriv, REG_GPIO_MUXCFG, 0xa0);

		u1_tmp = rtl_read_byte(rtlpriv, 0x4fd) &
			 BIT_OFFSET_LEN_MASK_32(0, 1);
		u1_tmp = u1_tmp |
			 ((rtlpcipriv->bt_coexist.bt_ant_isolation == 1) ?
			 0 : BIT_OFFSET_LEN_MASK_32(1, 1)) |
			 ((rtlpcipriv->bt_coexist.bt_service == BT_SCO) ?
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

void rtl92ce_suspend(struct ieee80211_hw *hw)
{
}

void rtl92ce_resume(struct ieee80211_hw *hw)
{
}
