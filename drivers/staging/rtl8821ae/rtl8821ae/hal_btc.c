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
#include "hal_btc.h"
#include "../pci.h"
#include "phy.h"
#include "fw.h"
#include "reg.h"
#include "def.h"
#include "../btcoexist/rtl_btc.h"

static struct bt_coexist_8821ae hal_coex_8821ae;

void rtl8821ae_dm_bt_turn_off_bt_coexist_before_enter_lps(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
    struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));

	if(!rtlpcipriv->btcoexist.bt_coexistence)
		return;

	if(ppsc->b_inactiveps) {
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,("[BT][DM], Before enter IPS, turn off all Coexist DM\n"));
		rtlpcipriv->btcoexist.current_state = 0;
		rtlpcipriv->btcoexist.previous_state = 0;
		rtlpcipriv->btcoexist.current_state_h = 0;
		rtlpcipriv->btcoexist.previous_state_h = 0;
		rtl8821ae_btdm_coex_all_off(hw);
	}
}


enum rt_media_status mgnt_link_status_query(struct ieee80211_hw *hw)
{
    struct rtl_priv *rtlpriv = rtl_priv(hw);
    struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
    enum rt_media_status    m_status = RT_MEDIA_DISCONNECT;

    u8 bibss = (mac->opmode == NL80211_IFTYPE_ADHOC) ? 1 : 0;

    if(bibss || rtlpriv->mac80211.link_state >= MAC80211_LINKED) {
            m_status = RT_MEDIA_CONNECT;
    }

    return m_status;
}

void rtl_8821ae_bt_wifi_media_status_notify(struct ieee80211_hw *hw, bool mstatus)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u8 h2c_parameter[3] ={0};
	u8 chnl;

	if(!rtlpcipriv->btcoexist.bt_coexistence)
		return;

	if(RT_MEDIA_CONNECT == mstatus)
		h2c_parameter[0] = 0x1; // 0: disconnected, 1:connected
	else
		h2c_parameter[0] = 0x0;

	if(mgnt_link_status_query(hw))	{
		chnl = rtlphy->current_channel;
		h2c_parameter[1] = chnl;
	}

	if(rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_20_40){
		h2c_parameter[2] = 0x30;
	} else {
		h2c_parameter[2] = 0x20;
	}

	RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,("[BTCoex], FW write 0x19=0x%x\n",
		h2c_parameter[0]<<16|h2c_parameter[1]<<8|h2c_parameter[2]));

	rtl8821ae_fill_h2c_cmd(hw, 0x19, 3, h2c_parameter);

}


bool rtl8821ae_dm_bt_is_wifi_busy(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	if(rtlpriv->link_info.b_busytraffic ||
		rtlpriv->link_info.b_rx_busy_traffic ||
		rtlpriv->link_info.b_tx_busy_traffic)
		return true;
	else
		return false;
}
void rtl8821ae_dm_bt_set_fw_3a(struct ieee80211_hw *hw,
						u8 byte1, u8 byte2, u8 byte3, u8 byte4, u8 byte5)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 h2c_parameter[5] ={0};
	h2c_parameter[0] = byte1;
	h2c_parameter[1] = byte2;
	h2c_parameter[2] = byte3;
	h2c_parameter[3] = byte4;
	h2c_parameter[4] = byte5;
	RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("[BTCoex], FW write 0x3a(4bytes)=0x%x%8x\n",
		h2c_parameter[0], h2c_parameter[1]<<24 | h2c_parameter[2]<<16 | h2c_parameter[3]<<8 | h2c_parameter[4]));
	rtl8821ae_fill_h2c_cmd(hw, 0x3a, 5, h2c_parameter);
}

bool rtl8821ae_dm_bt_need_to_dec_bt_pwr(struct ieee80211_hw *hw)
{
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (mgnt_link_status_query(hw) == RT_MEDIA_CONNECT) {
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("Need to decrease bt power\n"));
		rtlpcipriv->btcoexist.current_state |= BT_COEX_STATE_DEC_BT_POWER;
			return true;
	}

	rtlpcipriv->btcoexist.current_state &= ~BT_COEX_STATE_DEC_BT_POWER;
	return false;
}


bool rtl8821ae_dm_bt_is_same_coexist_state(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);

	if ((rtlpcipriv->btcoexist.previous_state
		== rtlpcipriv->btcoexist.current_state)
		&&(rtlpcipriv->btcoexist.previous_state_h
		== rtlpcipriv->btcoexist.current_state_h)) {
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
				("[DM][BT], Coexist state do not change!!\n"));
		return true;
	} else {
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
				("[DM][BT], Coexist state changed!!\n"));
		return false;
	}
}

void rtl8821ae_dm_bt_set_coex_table(struct ieee80211_hw *hw,
						u32 val_0x6c0, u32 val_0x6c8, u32 val_0x6cc)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("set coex table, set 0x6c0=0x%x\n", val_0x6c0));
	rtl_write_dword(rtlpriv, 0x6c0, val_0x6c0);

	RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("set coex table, set 0x6c8=0x%x\n", val_0x6c8));
	rtl_write_dword(rtlpriv, 0x6c8, val_0x6c8);

	RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("set coex table, set 0x6cc=0x%x\n", val_0x6cc));
	rtl_write_byte(rtlpriv, 0x6cc, val_0x6cc);
}

void rtl8821ae_dm_bt_set_hw_pta_mode(struct ieee80211_hw *hw, bool b_mode)
{
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (BT_PTA_MODE_ON == b_mode) {
		RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("PTA mode on, "));
		/*  Enable GPIO 0/1/2/3/8 pins for bt */
		rtl_write_byte(rtlpriv, 0x40, 0x20);
		rtlpcipriv->btcoexist.b_hw_coexist_all_off = false;
	} else {
		RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("PTA mode off\n"));
		rtl_write_byte(rtlpriv, 0x40, 0x0);
	}
}

void rtl8821ae_dm_bt_set_sw_rf_rx_lpf_corner(struct ieee80211_hw *hw, u8 type)
{
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (BT_RF_RX_LPF_CORNER_SHRINK == type) {
		/* Shrink RF Rx LPF corner, 0x1e[7:4]=1111 ==> [11:4] by Jenyu */
		RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("Shrink RF Rx LPF corner!!\n"));
		/* PHY_SetRFReg(Adapter, (RF_RADIO_PATH_E)PathA, 0x1e, 0xf0, 0xf); */
		rtl8821ae_phy_set_rf_reg(hw, RF90_PATH_A, 0x1e, 0xfffff, 0xf0ff7);
		rtlpcipriv->btcoexist.b_sw_coexist_all_off = false;
	} else if(BT_RF_RX_LPF_CORNER_RESUME == type) {
		/*Resume RF Rx LPF corner*/
		RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("Resume RF Rx LPF corner!!\n"));
		/* PHY_SetRFReg(Adapter, (RF_RADIO_PATH_E)PathA, 0x1e, 0xf0,
		 * pHalData->btcoexist.BtRfRegOrigin1E); */
		rtl8821ae_phy_set_rf_reg(hw, RF90_PATH_A, 0x1e, 0xfffff,
			rtlpcipriv->btcoexist.bt_rfreg_origin_1e);
	}
}

void rtl8821ae_dm_bt_set_sw_penalty_tx_rate_adaptive(struct ieee80211_hw *hw,
																	u8 ra_type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
	u8 tmp_u1;

	tmp_u1 = rtl_read_byte(rtlpriv, 0x4fd);
	tmp_u1 |= BIT(0);
	if (BT_TX_RATE_ADAPTIVE_LOW_PENALTY == ra_type) {
		RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("Tx rate adaptive, set low penalty!!\n"));
		tmp_u1 &= ~BIT(2);
		rtlpcipriv->btcoexist.b_sw_coexist_all_off = false;
	} else if(BT_TX_RATE_ADAPTIVE_NORMAL == ra_type) {
		RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("Tx rate adaptive, set normal!!\n"));
		tmp_u1 |= BIT(2);
	}

	rtl_write_byte(rtlpriv, 0x4fd, tmp_u1);
}

void rtl8821ae_dm_bt_btdm_structure_reload(struct ieee80211_hw *hw,
													struct btdm_8821ae	*p_btdm)
{
	p_btdm->b_all_off = false;
	p_btdm->b_agc_table_en = false;
	p_btdm->b_adc_back_off_on = false;
	p_btdm->b2_ant_hid_en = false;
	p_btdm->b_low_penalty_rate_adaptive = false;
	p_btdm->b_rf_rx_lpf_shrink = false;
	p_btdm->b_reject_aggre_pkt= false;

	p_btdm->b_tdma_on = false;
	p_btdm->tdma_ant = TDMA_2ANT;
	p_btdm->tdma_nav = TDMA_NAV_OFF;
	p_btdm->tdma_dac_swing = TDMA_DAC_SWING_OFF;
	p_btdm->fw_dac_swing_lvl = 0x20;

	p_btdm->b_tra_tdma_on = false;
	p_btdm->tra_tdma_ant = TDMA_2ANT;
	p_btdm->tra_tdma_nav = TDMA_NAV_OFF;
	p_btdm->b_ignore_wlan_act = false;

	p_btdm->b_ps_tdma_on = false;
	p_btdm->ps_tdma_byte[0] = 0x0;
	p_btdm->ps_tdma_byte[1] = 0x0;
	p_btdm->ps_tdma_byte[2] = 0x0;
	p_btdm->ps_tdma_byte[3] = 0x8;
	p_btdm->ps_tdma_byte[4] = 0x0;

	p_btdm->b_pta_on = true;
	p_btdm->val_0x6c0 = 0x5a5aaaaa;
	p_btdm->val_0x6c8 = 0xcc;
	p_btdm->val_0x6cc = 0x3;

	p_btdm->b_sw_dac_swing_on = false;
	p_btdm->sw_dac_swing_lvl = 0xc0;
	p_btdm->wlan_act_hi = 0x20;
	p_btdm->wlan_act_lo = 0x10;
	p_btdm->bt_retry_index = 2;

	p_btdm->b_dec_bt_pwr = false;
}

void rtl8821ae_dm_bt_btdm_structure_reload_all_off(struct ieee80211_hw *hw,
													struct btdm_8821ae	*p_btdm)
{
	rtl8821ae_dm_bt_btdm_structure_reload(hw, p_btdm);
	p_btdm->b_all_off = true;
	p_btdm->b_pta_on = false;
	p_btdm->wlan_act_hi = 0x10;
}

bool rtl8821ae_dm_bt_is_2_ant_common_action(struct ieee80211_hw *hw)
{
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct btdm_8821ae btdm8821ae;
	bool b_common = false;

	rtl8821ae_dm_bt_btdm_structure_reload(hw, &btdm8821ae);

	if(!rtl8821ae_dm_bt_is_wifi_busy(hw)
		&& !rtlpcipriv->btcoexist.b_bt_busy) {
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("Wifi idle + Bt idle, bt coex mechanism always off!!\n"));
		rtl8821ae_dm_bt_btdm_structure_reload_all_off(hw, &btdm8821ae);
		b_common = true;
	} else if (rtl8821ae_dm_bt_is_wifi_busy(hw)
		&& !rtlpcipriv->btcoexist.b_bt_busy) {
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("Wifi non-idle + Bt disabled/idle!!\n"));
		btdm8821ae.b_low_penalty_rate_adaptive = true;
		btdm8821ae.b_rf_rx_lpf_shrink = false;
		btdm8821ae.b_reject_aggre_pkt = false;

		/* sw mechanism */
		btdm8821ae.b_agc_table_en = false;
		btdm8821ae.b_adc_back_off_on = false;
		btdm8821ae.b_sw_dac_swing_on = false;

		btdm8821ae.b_pta_on = true;
		btdm8821ae.val_0x6c0 = 0x5a5aaaaa;
		btdm8821ae.val_0x6c8 = 0xcccc;
		btdm8821ae.val_0x6cc = 0x3;

		btdm8821ae.b_tdma_on = false;
		btdm8821ae.tdma_dac_swing = TDMA_DAC_SWING_OFF;
		btdm8821ae.b2_ant_hid_en = false;

		b_common = true;
	}else if (rtlpcipriv->btcoexist.b_bt_busy) {
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("Bt non-idle!\n"));
		if(mgnt_link_status_query(hw) == RT_MEDIA_CONNECT){
			RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("Wifi connection exist\n"))
			b_common = false;
		} else {
			RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
				("No Wifi connection!\n"));
			btdm8821ae.b_rf_rx_lpf_shrink = true;
			btdm8821ae.b_low_penalty_rate_adaptive = false;
			btdm8821ae.b_reject_aggre_pkt = false;

			/* sw mechanism */
			btdm8821ae.b_agc_table_en = false;
			btdm8821ae.b_adc_back_off_on = false;
			btdm8821ae.b_sw_dac_swing_on = false;

			btdm8821ae.b_pta_on = true;
			btdm8821ae.val_0x6c0 = 0x55555555;
			btdm8821ae.val_0x6c8 = 0x0000ffff;
			btdm8821ae.val_0x6cc = 0x3;

			btdm8821ae.b_tdma_on = false;
			btdm8821ae.tdma_dac_swing = TDMA_DAC_SWING_OFF;
			btdm8821ae.b2_ant_hid_en = false;

			b_common = true;
		}
	}

	if (rtl8821ae_dm_bt_need_to_dec_bt_pwr(hw)) {
		btdm8821ae.b_dec_bt_pwr = true;
	}

	if(b_common)
		 rtlpcipriv->btcoexist.current_state |= BT_COEX_STATE_BTINFO_COMMON;

	if (b_common && rtl8821ae_dm_bt_is_coexist_state_changed(hw))
		rtl8821ae_dm_bt_set_bt_dm(hw, &btdm8821ae);

	return b_common;
}

void rtl8821ae_dm_bt_set_sw_full_time_dac_swing(
		struct ieee80211_hw * hw, bool b_sw_dac_swing_on, u32 sw_dac_swing_lvl)
{
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (b_sw_dac_swing_on) {
		RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
			("[BTCoex], SwDacSwing = 0x%x\n", sw_dac_swing_lvl));
		rtl8821ae_phy_set_bb_reg(hw, 0x880, 0xff000000, sw_dac_swing_lvl);
		rtlpcipriv->btcoexist.b_sw_coexist_all_off = false;
	} else {
		RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("[BTCoex], SwDacSwing Off!\n"));
		rtl8821ae_phy_set_bb_reg(hw, 0x880, 0xff000000, 0xc0);
	}
}

void rtl8821ae_dm_bt_set_fw_dec_bt_pwr(
		struct ieee80211_hw *hw, bool b_dec_bt_pwr)
{
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 h2c_parameter[1] ={0};

	h2c_parameter[0] = 0;

	if (b_dec_bt_pwr) {
		h2c_parameter[0] |= BIT(1);
		rtlpcipriv->btcoexist.b_fw_coexist_all_off = false;
	}

	RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
		("[BTCoex], decrease Bt Power : %s, write 0x21=0x%x\n",
		(b_dec_bt_pwr? "Yes!!":"No!!"), h2c_parameter[0]));

	rtl8821ae_fill_h2c_cmd(hw, 0x21, 1, h2c_parameter);
}


void rtl8821ae_dm_bt_set_fw_2_ant_hid(struct ieee80211_hw *hw,
									bool b_enable, bool b_dac_swing_on)
{
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 h2c_parameter[1] ={0};

	if (b_enable) {
		h2c_parameter[0] |= BIT(0);
		rtlpcipriv->btcoexist.b_fw_coexist_all_off = false;
	}
	if (b_dac_swing_on) {
		h2c_parameter[0] |= BIT(1); /* Dac Swing default enable */
	}
	RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
		("[BTCoex], turn 2-Ant+HID mode %s, DACSwing:%s, write 0x15=0x%x\n",
		(b_enable ? "ON!!":"OFF!!"), (b_dac_swing_on ? "ON":"OFF"),
		h2c_parameter[0]));

	rtl8821ae_fill_h2c_cmd(hw, 0x15, 1, h2c_parameter);
}

void rtl8821ae_dm_bt_set_fw_tdma_ctrl(struct ieee80211_hw *hw,
				bool b_enable, u8 ant_num, u8 nav_en, u8 dac_swing_en)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
	u8 h2c_parameter[1] ={0};
	u8 h2c_parameter1[1] = {0};

	h2c_parameter[0] = 0;
	h2c_parameter1[0] = 0;

	if(b_enable) {
		RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
			("[BTCoex], set BT PTA update manager to trigger update!!\n"));
		h2c_parameter1[0] |= BIT(0);

		RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
			("[BTCoex], turn TDMA mode ON!!\n"));
		h2c_parameter[0] |= BIT(0);		/* function enable */
		if (TDMA_1ANT == ant_num) {
			RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("[BTCoex], TDMA_1ANT\n"));
			h2c_parameter[0] |= BIT(1);
		} else if(TDMA_2ANT == ant_num) {
			RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("[BTCoex], TDMA_2ANT\n"));
		} else {
			RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("[BTCoex], Unknown Ant\n"));
		}

		if (TDMA_NAV_OFF == nav_en) {
			RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("[BTCoex], TDMA_NAV_OFF\n"));
		} else if (TDMA_NAV_ON == nav_en) {
			RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("[BTCoex], TDMA_NAV_ON\n"));
			h2c_parameter[0] |= BIT(2);
		}

		if (TDMA_DAC_SWING_OFF == dac_swing_en) {
			RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
				("[BTCoex], TDMA_DAC_SWING_OFF\n"));
		} else if(TDMA_DAC_SWING_ON == dac_swing_en) {
			RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
				("[BTCoex], TDMA_DAC_SWING_ON\n"));
			h2c_parameter[0] |= BIT(4);
		}
		rtlpcipriv->btcoexist.b_fw_coexist_all_off = false;
	} else {
		RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
			("[BTCoex], set BT PTA update manager to no update!!\n"));
		RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
			("[BTCoex], turn TDMA mode OFF!!\n"));
	}

	RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
		("[BTCoex], FW2AntTDMA, write 0x26=0x%x\n", h2c_parameter1[0]));
	rtl8821ae_fill_h2c_cmd(hw, 0x26, 1, h2c_parameter1);

	RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
		("[BTCoex], FW2AntTDMA, write 0x14=0x%x\n", h2c_parameter[0]));
	rtl8821ae_fill_h2c_cmd(hw, 0x14, 1, h2c_parameter);

	if (!b_enable) {
		/* delay_ms(2);
		 * PlatformEFIOWrite1Byte(Adapter, 0x778, 0x1); */
	}
}


void rtl8821ae_dm_bt_set_fw_ignore_wlan_act( struct ieee80211_hw *hw, bool b_enable)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
	u8 h2c_parameter[1] ={0};

	if (b_enable) {
		RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("[BTCoex], BT Ignore Wlan_Act !!\n"));
		h2c_parameter[0] |= BIT(0);		// function enable
		rtlpcipriv->btcoexist.b_fw_coexist_all_off = false;
	} else {
		RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("[BTCoex], BT don't ignore Wlan_Act !!\n"));
	}

    RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("[BTCoex], set FW for BT Ignore Wlan_Act, write 0x25=0x%x\n",
		h2c_parameter[0]));

	rtl8821ae_fill_h2c_cmd(hw, 0x25, 1, h2c_parameter);
}


void rtl8821ae_dm_bt_set_fw_tra_tdma_ctrl(struct ieee80211_hw *hw,
		bool b_enable, u8 ant_num, u8 nav_en
	)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
	//struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	u8 h2c_parameter[2] ={0};


	if (b_enable) {
		RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
			("[BTCoex], turn TTDMA mode ON!!\n"));
		h2c_parameter[0] |= BIT(0);		// function enable
		if (TDMA_1ANT == ant_num) {
			RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("[BTCoex], TTDMA_1ANT\n"));
			h2c_parameter[0] |= BIT(1);
		} else if (TDMA_2ANT == ant_num) {
			RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("[BTCoex], TTDMA_2ANT\n"));
		} else {
			RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("[BTCoex], Unknown Ant\n"));
		}

		if (TDMA_NAV_OFF == nav_en) {
			RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("[BTCoex], TTDMA_NAV_OFF\n"));
		} else if (TDMA_NAV_ON == nav_en) {
			RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("[BTCoex], TTDMA_NAV_ON\n"));
			h2c_parameter[1] |= BIT(0);
		}

		rtlpcipriv->btcoexist.b_fw_coexist_all_off = false;
	} else {
		RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
			("[BTCoex], turn TTDMA mode OFF!!\n"));
	}

	RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
		("[BTCoex], FW Traditional TDMA, write 0x33=0x%x\n",
		h2c_parameter[0] << 8| h2c_parameter[1]));

	rtl8821ae_fill_h2c_cmd(hw, 0x33, 2, h2c_parameter);
}


void rtl8821ae_dm_bt_set_fw_dac_swing_level(struct ieee80211_hw *hw,
													u8 dac_swing_lvl)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 h2c_parameter[1] ={0};
	h2c_parameter[0] = dac_swing_lvl;

	RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
		("[BTCoex], Set Dac Swing Level=0x%x\n", dac_swing_lvl));
	RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
		("[BTCoex], write 0x29=0x%x\n", h2c_parameter[0]));

	rtl8821ae_fill_h2c_cmd(hw, 0x29, 1, h2c_parameter);
}

void rtl8821ae_dm_bt_set_fw_bt_hid_info(struct ieee80211_hw *hw, bool b_enable)
{
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 h2c_parameter[1] ={0};
	h2c_parameter[0] = 0;

	if(b_enable){
		h2c_parameter[0] |= BIT(0);
		rtlpcipriv->btcoexist.b_fw_coexist_all_off = false;
	}
	RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
		("[BTCoex], Set BT HID information=0x%x\n", b_enable));
	RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
		("[BTCoex], write 0x24=0x%x\n", h2c_parameter[0]));

	rtl8821ae_fill_h2c_cmd(hw, 0x24, 1, h2c_parameter);
}

void rtl8821ae_dm_bt_set_fw_bt_retry_index(struct ieee80211_hw *hw,
													u8 retry_index)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 h2c_parameter[1] ={0};
	h2c_parameter[0] = retry_index;

	RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
		("[BTCoex], Set BT Retry Index=%d\n", retry_index));
	RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
		("[BTCoex], write 0x23=0x%x\n", h2c_parameter[0]));

	rtl8821ae_fill_h2c_cmd(hw, 0x23, 1, h2c_parameter);
}

void rtl8821ae_dm_bt_set_fw_wlan_act(struct ieee80211_hw *hw,
											u8 wlan_act_hi, u8 wlan_act_lo)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 h2c_parameter_hi[1] ={0};
	u8 h2c_parameter_lo[1] ={0};
	h2c_parameter_hi[0] = wlan_act_hi;
	h2c_parameter_lo[0] = wlan_act_lo;

	RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
		("[BTCoex], Set WLAN_ACT Hi:Lo=0x%x/0x%x\n", wlan_act_hi, wlan_act_lo));
	RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
		("[BTCoex], write 0x22=0x%x\n", h2c_parameter_hi[0]));
	RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
		("[BTCoex], write 0x11=0x%x\n", h2c_parameter_lo[0]));

	/* WLAN_ACT = High duration, unit:ms */
	rtl8821ae_fill_h2c_cmd(hw, 0x22, 1, h2c_parameter_hi);
	/*  WLAN_ACT = Low duration, unit:3*625us */
	rtl8821ae_fill_h2c_cmd(hw, 0x11, 1, h2c_parameter_lo);
}

void rtl8821ae_dm_bt_set_bt_dm(struct ieee80211_hw *hw, struct btdm_8821ae *p_btdm)
{
	struct rtl_pci_priv	*rtlpcipriv = rtl_pcipriv(hw);
	struct rtl_priv	*rtlpriv = rtl_priv(hw);
	struct btdm_8821ae *p_btdm_8821ae = &hal_coex_8821ae.btdm;
	u8 i;

	bool b_fw_current_inpsmode = false;
    bool b_fw_ps_awake = true;

    rtlpriv->cfg->ops->get_hw_reg(hw, HW_VAR_FW_PSMODE_STATUS,
			                      (u8 *) (&b_fw_current_inpsmode));
    rtlpriv->cfg->ops->get_hw_reg(hw, HW_VAR_FWLPS_RF_ON,
			                      (u8 *) (&b_fw_ps_awake));

	// check new setting is different with the old one,
	// if all the same, don't do the setting again.
	if (memcmp(p_btdm_8821ae, p_btdm, sizeof(struct btdm_8821ae)) == 0) {
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("[BTCoex], the same coexist setting, return!!\n"));
		return;
	} else {	//save the new coexist setting
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("[BTCoex], UPDATE TO NEW COEX SETTING!!\n"));
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("[BTCoex], original/new bAllOff=0x%x/ 0x%x \n",
			p_btdm_8821ae->b_all_off, p_btdm->b_all_off));
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("[BTCoex], original/new b_agc_table_en=0x%x/ 0x%x \n",
			p_btdm_8821ae->b_agc_table_en, p_btdm->b_agc_table_en));
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("[BTCoex], original/new b_adc_back_off_on=0x%x/ 0x%x \n",
			p_btdm_8821ae->b_adc_back_off_on, p_btdm->b_adc_back_off_on));
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("[BTCoex], original/new b2_ant_hid_en=0x%x/ 0x%x \n",
			p_btdm_8821ae->b2_ant_hid_en, p_btdm->b2_ant_hid_en));
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("[BTCoex], original/new bLowPenaltyRateAdaptive=0x%x/ 0x%x \n",
			p_btdm_8821ae->b_low_penalty_rate_adaptive,
			p_btdm->b_low_penalty_rate_adaptive));
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("[BTCoex], original/new bRfRxLpfShrink=0x%x/ 0x%x \n",
			p_btdm_8821ae->b_rf_rx_lpf_shrink, p_btdm->b_rf_rx_lpf_shrink));
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("[BTCoex], original/new bRejectAggrePkt=0x%x/ 0x%x \n",
			p_btdm_8821ae->b_reject_aggre_pkt, p_btdm->b_reject_aggre_pkt));
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("[BTCoex], original/new b_tdma_on=0x%x/ 0x%x \n",
			p_btdm_8821ae->b_tdma_on, p_btdm->b_tdma_on));
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("[BTCoex], original/new tdmaAnt=0x%x/ 0x%x \n",
			p_btdm_8821ae->tdma_ant, p_btdm->tdma_ant));
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("[BTCoex], original/new tdmaNav=0x%x/ 0x%x \n",
			p_btdm_8821ae->tdma_nav, p_btdm->tdma_nav));
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("[BTCoex], original/new tdma_dac_swing=0x%x/ 0x%x \n",
			p_btdm_8821ae->tdma_dac_swing, p_btdm->tdma_dac_swing));
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("[BTCoex], original/new fw_dac_swing_lvl=0x%x/ 0x%x \n",
			p_btdm_8821ae->fw_dac_swing_lvl, p_btdm->fw_dac_swing_lvl));

		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("[BTCoex], original/new bTraTdmaOn=0x%x/ 0x%x \n",
			p_btdm_8821ae->b_tra_tdma_on, p_btdm->b_tra_tdma_on));
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("[BTCoex], original/new traTdmaAnt=0x%x/ 0x%x \n",
			p_btdm_8821ae->tra_tdma_ant, p_btdm->tra_tdma_ant));
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("[BTCoex], original/new traTdmaNav=0x%x/ 0x%x \n",
			p_btdm_8821ae->tra_tdma_nav, p_btdm->tra_tdma_nav));
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("[BTCoex], original/new bPsTdmaOn=0x%x/ 0x%x \n",
			p_btdm_8821ae->b_ps_tdma_on, p_btdm->b_ps_tdma_on));
		for(i=0; i<5; i++)
		{
			RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
				("[BTCoex], original/new psTdmaByte[i]=0x%x/ 0x%x \n",
				p_btdm_8821ae->ps_tdma_byte[i], p_btdm->ps_tdma_byte[i]));
		}
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("[BTCoex], original/new bIgnoreWlanAct=0x%x/ 0x%x \n",
			p_btdm_8821ae->b_ignore_wlan_act, p_btdm->b_ignore_wlan_act));


		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("[BTCoex], original/new bPtaOn=0x%x/ 0x%x \n",
			p_btdm_8821ae->b_pta_on, p_btdm->b_pta_on));
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("[BTCoex], original/new val_0x6c0=0x%x/ 0x%x \n",
			p_btdm_8821ae->val_0x6c0, p_btdm->val_0x6c0));
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("[BTCoex], original/new val_0x6c8=0x%x/ 0x%x \n",
			p_btdm_8821ae->val_0x6c8, p_btdm->val_0x6c8));
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("[BTCoex], original/new val_0x6cc=0x%x/ 0x%x \n",
			p_btdm_8821ae->val_0x6cc, p_btdm->val_0x6cc));
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("[BTCoex], original/new b_sw_dac_swing_on=0x%x/ 0x%x \n",
			p_btdm_8821ae->b_sw_dac_swing_on, p_btdm->b_sw_dac_swing_on));
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("[BTCoex], original/new sw_dac_swing_lvl=0x%x/ 0x%x \n",
			p_btdm_8821ae->sw_dac_swing_lvl, p_btdm->sw_dac_swing_lvl));
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("[BTCoex], original/new wlanActHi=0x%x/ 0x%x \n",
			p_btdm_8821ae->wlan_act_hi, p_btdm->wlan_act_hi));
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("[BTCoex], original/new wlanActLo=0x%x/ 0x%x \n",
			p_btdm_8821ae->wlan_act_lo, p_btdm->wlan_act_lo));
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("[BTCoex], original/new btRetryIndex=0x%x/ 0x%x \n",
			p_btdm_8821ae->bt_retry_index, p_btdm->bt_retry_index));

		memcpy(p_btdm_8821ae, p_btdm, sizeof(struct btdm_8821ae));
	}
	/*
	 * Here we only consider when Bt Operation
	 * inquiry/paging/pairing is ON
	 * we only need to turn off TDMA */

	if (rtlpcipriv->btcoexist.b_hold_for_bt_operation) {
		RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
			("[BTCoex], set to ignore wlanAct for BT OP!!\n"));
		rtl8821ae_dm_bt_set_fw_ignore_wlan_act(hw, true);
		return;
	}

	if (p_btdm->b_all_off) {
		RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
			("[BTCoex], disable all coexist mechanism !!\n"));
		rtl8821ae_btdm_coex_all_off(hw);
		return;
	}

	rtl8821ae_dm_bt_reject_ap_aggregated_packet(hw, p_btdm->b_reject_aggre_pkt);

	if(p_btdm->b_low_penalty_rate_adaptive)
		rtl8821ae_dm_bt_set_sw_penalty_tx_rate_adaptive(hw,
			BT_TX_RATE_ADAPTIVE_LOW_PENALTY);
	else
		rtl8821ae_dm_bt_set_sw_penalty_tx_rate_adaptive(hw,
			BT_TX_RATE_ADAPTIVE_NORMAL);

	if(p_btdm->b_rf_rx_lpf_shrink)
		rtl8821ae_dm_bt_set_sw_rf_rx_lpf_corner(hw, BT_RF_RX_LPF_CORNER_SHRINK);
	else
		rtl8821ae_dm_bt_set_sw_rf_rx_lpf_corner(hw, BT_RF_RX_LPF_CORNER_RESUME);

	if(p_btdm->b_agc_table_en)
		rtl8821ae_dm_bt_agc_table(hw, BT_AGCTABLE_ON);
	else
		rtl8821ae_dm_bt_agc_table(hw, BT_AGCTABLE_OFF);

	if(p_btdm->b_adc_back_off_on)
		rtl8821ae_dm_bt_bb_back_off_level(hw, BT_BB_BACKOFF_ON);
	else
		rtl8821ae_dm_bt_bb_back_off_level(hw, BT_BB_BACKOFF_OFF);

	rtl8821ae_dm_bt_set_fw_bt_retry_index(hw, p_btdm->bt_retry_index);

	rtl8821ae_dm_bt_set_fw_dac_swing_level(hw, p_btdm->fw_dac_swing_lvl);
	rtl8821ae_dm_bt_set_fw_wlan_act(hw, p_btdm->wlan_act_hi, p_btdm->wlan_act_lo);

	rtl8821ae_dm_bt_set_coex_table(hw, p_btdm->val_0x6c0,
		p_btdm->val_0x6c8, p_btdm->val_0x6cc);
	rtl8821ae_dm_bt_set_hw_pta_mode(hw, p_btdm->b_pta_on);

	/*
	 * Note: There is a constraint between TDMA and 2AntHID
	 * Only one of 2AntHid and tdma can be turn on
	 * We should turn off those mechanisms should be turned off first
	 * and then turn on those mechanisms should be turned on.
	*/
#if 1
	if(p_btdm->b2_ant_hid_en) {
		// turn off tdma
		rtl8821ae_dm_bt_set_fw_tra_tdma_ctrl(hw, p_btdm->b_tra_tdma_on,
							p_btdm->tra_tdma_ant, p_btdm->tra_tdma_nav);
		rtl8821ae_dm_bt_set_fw_tdma_ctrl(hw, false, p_btdm->tdma_ant,
							p_btdm->tdma_nav, p_btdm->tdma_dac_swing);

		// turn off Pstdma
		rtl8821ae_dm_bt_set_fw_ignore_wlan_act(hw, p_btdm->b_ignore_wlan_act);
		rtl8821ae_dm_bt_set_fw_3a(hw, 0x0, 0x0, 0x0, 0x8, 0x0); 	// Antenna control by PTA, 0x870 = 0x300.

		// turn on 2AntHid
		rtl8821ae_dm_bt_set_fw_bt_hid_info(hw, true);
		rtl8821ae_dm_bt_set_fw_2_ant_hid(hw, true, true);
	} else if(p_btdm->b_tdma_on) {
		// turn off 2AntHid
		rtl8821ae_dm_bt_set_fw_bt_hid_info(hw, false);
		rtl8821ae_dm_bt_set_fw_2_ant_hid(hw, false, false);

		// turn off pstdma
		rtl8821ae_dm_bt_set_fw_ignore_wlan_act(hw, p_btdm->b_ignore_wlan_act);
		rtl8821ae_dm_bt_set_fw_3a(hw, 0x0, 0x0, 0x0, 0x8, 0x0); 	// Antenna control by PTA, 0x870 = 0x300.

		// turn on tdma
		rtl8821ae_dm_bt_set_fw_tra_tdma_ctrl(hw, p_btdm->b_tra_tdma_on, p_btdm->tra_tdma_ant, p_btdm->tra_tdma_nav);
		rtl8821ae_dm_bt_set_fw_tdma_ctrl(hw, true, p_btdm->tdma_ant, p_btdm->tdma_nav, p_btdm->tdma_dac_swing);
	} else if(p_btdm->b_ps_tdma_on) {
		// turn off 2AntHid
		rtl8821ae_dm_bt_set_fw_bt_hid_info(hw, false);
		rtl8821ae_dm_bt_set_fw_2_ant_hid(hw, false, false);

		// turn off tdma
		rtl8821ae_dm_bt_set_fw_tra_tdma_ctrl(hw, p_btdm->b_tra_tdma_on, p_btdm->tra_tdma_ant, p_btdm->tra_tdma_nav);
		rtl8821ae_dm_bt_set_fw_tdma_ctrl(hw, false, p_btdm->tdma_ant, p_btdm->tdma_nav, p_btdm->tdma_dac_swing);

		// turn on pstdma
		rtl8821ae_dm_bt_set_fw_ignore_wlan_act(hw, p_btdm->b_ignore_wlan_act);
		rtl8821ae_dm_bt_set_fw_3a(hw,
			p_btdm->ps_tdma_byte[0],
			p_btdm->ps_tdma_byte[1],
			p_btdm->ps_tdma_byte[2],
			p_btdm->ps_tdma_byte[3],
			p_btdm->ps_tdma_byte[4]);
	} else {
		// turn off 2AntHid
		rtl8821ae_dm_bt_set_fw_bt_hid_info(hw, false);
		rtl8821ae_dm_bt_set_fw_2_ant_hid(hw, false, false);

		// turn off tdma
		rtl8821ae_dm_bt_set_fw_tra_tdma_ctrl(hw, p_btdm->b_tra_tdma_on, p_btdm->tra_tdma_ant, p_btdm->tra_tdma_nav);
		rtl8821ae_dm_bt_set_fw_tdma_ctrl(hw, false, p_btdm->tdma_ant, p_btdm->tdma_nav, p_btdm->tdma_dac_swing);

		// turn off pstdma
		rtl8821ae_dm_bt_set_fw_ignore_wlan_act(hw, p_btdm->b_ignore_wlan_act);
		rtl8821ae_dm_bt_set_fw_3a(hw, 0x0, 0x0, 0x0, 0x8, 0x0); 	// Antenna control by PTA, 0x870 = 0x300.
	}
#else
	if (p_btdm->b_tdma_on) {
		if(p_btdm->b_ps_tdma_on) {
		} else {
			rtl8821ae_dm_bt_set_fw_3a(hw, 0x0, 0x0, 0x0, 0x8, 0x0);
		}
		/* Turn off 2AntHID first then turn tdma ON */
		rtl8821ae_dm_bt_set_fw_bt_hid_info(hw, false);
		rtl8821ae_dm_bt_set_fw_2_ant_hid(hw, false, false);
		rtl8821ae_dm_bt_set_fw_tra_tdma_ctrl(hw, p_btdm->b_tra_tdma_on, p_btdm->tra_tdma_ant, p_btdm->tra_tdma_nav);
		rtl8821ae_dm_bt_set_fw_tdma_ctrl(hw, true,
			p_btdm->tdma_ant, p_btdm->tdma_nav, p_btdm->tdma_dac_swing);
	} else {
		/* Turn off tdma first then turn 2AntHID ON if need */
		rtl8821ae_dm_bt_set_fw_tra_tdma_ctrl(hw, p_btdm->b_tra_tdma_on, p_btdm->tra_tdma_ant, p_btdm->tra_tdma_nav);
		rtl8821ae_dm_bt_set_fw_tdma_ctrl(hw, false, p_btdm->tdma_ant,
			p_btdm->tdma_nav, p_btdm->tdma_dac_swing);
		if (p_btdm->b2_ant_hid_en) {
			rtl8821ae_dm_bt_set_fw_bt_hid_info(hw, true);
			rtl8821ae_dm_bt_set_fw_2_ant_hid(hw, true, true);
		} else {
			rtl8821ae_dm_bt_set_fw_bt_hid_info(hw, false);
			rtl8821ae_dm_bt_set_fw_2_ant_hid(hw, false, false);
		}
		if(p_btdm->b_ps_tdma_on) {
			rtl8821ae_dm_bt_set_fw_3a(hw, p_btdm->ps_tdma_byte[0], p_btdm->ps_tdma_byte[1],
				p_btdm->ps_tdma_byte[2], p_btdm->ps_tdma_byte[3], p_btdm->ps_tdma_byte[4]);
		} else {
			rtl8821ae_dm_bt_set_fw_3a(hw, 0x0, 0x0, 0x0, 0x8, 0x0);
		}
	}
#endif

	/*
	 * Note:
	 * We should add delay for making sure sw DacSwing can be set successfully.
	 * because of that rtl8821ae_dm_bt_set_fw_2_ant_hid() and rtl8821ae_dm_bt_set_fw_tdma_ctrl()
	 * will overwrite the reg 0x880.
	*/
	mdelay(30);
	rtl8821ae_dm_bt_set_sw_full_time_dac_swing(hw,
		p_btdm->b_sw_dac_swing_on, p_btdm->sw_dac_swing_lvl);
	rtl8821ae_dm_bt_set_fw_dec_bt_pwr(hw, p_btdm->b_dec_bt_pwr);
}

void rtl8821ae_dm_bt_bt_state_update_2_ant_hid(struct ieee80211_hw *hw)
{
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("[BTCoex], HID busy!!\n"));
	rtlpcipriv->btcoexist.b_bt_busy = true;
	rtlpcipriv->btcoexist.current_state &= ~BT_COEX_STATE_BT_IDLE;
}

void rtl8821ae_dm_bt_bt_state_update_2_ant_pan(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
	bool b_idle = false;

	if (hal_coex_8821ae.low_priority_tx >=
		hal_coex_8821ae.low_priority_rx) {
		if((hal_coex_8821ae.low_priority_tx/
			hal_coex_8821ae.low_priority_rx) > 10) {
			b_idle = true;
		}
	} else {
		if((hal_coex_8821ae.low_priority_rx/
			hal_coex_8821ae.low_priority_tx) > 10) {
			b_idle = true;
		}
	}

	if(!b_idle) {
		RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("[BTCoex], PAN busy!!\n"));
		rtlpcipriv->btcoexist.b_bt_busy = true;
		rtlpcipriv->btcoexist.current_state &= ~BT_COEX_STATE_BT_IDLE;
	} else {
		RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("[BTCoex], PAN idle!!\n"));
	}
}

void rtl8821ae_dm_bt_2_ant_sco_action(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct btdm_8821ae btdm8821ae;
	u8 bt_rssi_state;

	rtl8821ae_dm_bt_btdm_structure_reload(hw, &btdm8821ae);
	btdm8821ae.b_rf_rx_lpf_shrink = true;
	btdm8821ae.b_low_penalty_rate_adaptive = true;
	btdm8821ae.b_reject_aggre_pkt = false;

	if (rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_20_40) {
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("HT40\n"));
		/*  coex table */
		btdm8821ae.val_0x6c0 = 0x5a5aaaaa;
		btdm8821ae.val_0x6c8 = 0xcc;
		btdm8821ae.val_0x6cc = 0x3;
		/* sw mechanism */
		btdm8821ae.b_agc_table_en = false;
		btdm8821ae.b_adc_back_off_on = true;
		btdm8821ae.b_sw_dac_swing_on = false;
		/* fw mechanism */
		btdm8821ae.b_tdma_on = false;
		btdm8821ae.tdma_dac_swing = TDMA_DAC_SWING_OFF;
	} else {
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("HT20 or Legacy\n"));
		bt_rssi_state
			= rtl8821ae_dm_bt_check_coex_rssi_state(hw, 2, BT_FW_COEX_THRESH_47, 0);

		/* coex table */
		btdm8821ae.val_0x6c0 = 0x5a5aaaaa;
		btdm8821ae.val_0x6c8 = 0xcc;
		btdm8821ae.val_0x6cc = 0x3;
		/* sw mechanism */
		if ((bt_rssi_state == BT_RSSI_STATE_HIGH) ||
			(bt_rssi_state == BT_RSSI_STATE_STAY_HIGH) ) {
			btdm8821ae.b_agc_table_en = true;
			btdm8821ae.b_adc_back_off_on = true;
			btdm8821ae.b_sw_dac_swing_on = false;
		} else {
			btdm8821ae.b_agc_table_en = false;
			btdm8821ae.b_adc_back_off_on = false;
			btdm8821ae.b_sw_dac_swing_on = false;
		}
		/* fw mechanism */
		btdm8821ae.b_tdma_on = false;
		btdm8821ae.tdma_dac_swing = TDMA_DAC_SWING_OFF;
	}

	if (rtl8821ae_dm_bt_need_to_dec_bt_pwr(hw)) {
		btdm8821ae.b_dec_bt_pwr = true;
	}

	if(rtl8821ae_dm_bt_is_coexist_state_changed(hw))
		rtl8821ae_dm_bt_set_bt_dm(hw, &btdm8821ae);
}

void rtl8821ae_dm_bt_2_ant_hid_action(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct btdm_8821ae btdm8821ae;
	u8 bt_rssi_state;

	rtl8821ae_dm_bt_btdm_structure_reload(hw, &btdm8821ae);

	btdm8821ae.b_rf_rx_lpf_shrink = true;
	btdm8821ae.b_low_penalty_rate_adaptive = true;
	btdm8821ae.b_reject_aggre_pkt = false;

	// coex table
	btdm8821ae.val_0x6c0 = 0x55555555;
	btdm8821ae.val_0x6c8 = 0xffff;
	btdm8821ae.val_0x6cc = 0x3;
	btdm8821ae.b_ignore_wlan_act = true;

	if(rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_20_40) {
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("HT40\n"));
		// sw mechanism
		btdm8821ae.b_agc_table_en = false;
		btdm8821ae.b_adc_back_off_on = false;
		btdm8821ae.b_sw_dac_swing_on = false;

		// fw mechanism
		btdm8821ae.b_ps_tdma_on = true;
		btdm8821ae.ps_tdma_byte[0] = 0xa3;
		btdm8821ae.ps_tdma_byte[1] = 0xf;
		btdm8821ae.ps_tdma_byte[2] = 0xf;
		btdm8821ae.ps_tdma_byte[3] = 0x0;
		btdm8821ae.ps_tdma_byte[4] = 0x80;

		btdm8821ae.b_tra_tdma_on = false;
		btdm8821ae.b_tdma_on = false;
		btdm8821ae.tdma_dac_swing = TDMA_DAC_SWING_OFF;
		btdm8821ae.b2_ant_hid_en = false;
	} else {
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("HT20 or Legacy\n"));
		bt_rssi_state =
			rtl8821ae_dm_bt_check_coex_rssi_state(hw, 2, 47, 0);

		if( (bt_rssi_state == BT_RSSI_STATE_HIGH) ||
			(bt_rssi_state == BT_RSSI_STATE_STAY_HIGH) ) {
			RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("Wifi rssi high \n"));
			// sw mechanism
			btdm8821ae.b_agc_table_en = false;
			btdm8821ae.b_adc_back_off_on = false;
			btdm8821ae.b_sw_dac_swing_on = true;
			btdm8821ae.sw_dac_swing_lvl = 0x20;

			// fw mechanism
			btdm8821ae.b_ps_tdma_on = false;
			btdm8821ae.b_tdma_on = false;
			btdm8821ae.tdma_dac_swing = TDMA_DAC_SWING_OFF;
			btdm8821ae.b2_ant_hid_en = false;
		} else {
			RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("Wifi rssi low \n"));
			// sw mechanism
			btdm8821ae.b_agc_table_en = false;
			btdm8821ae.b_adc_back_off_on = false;
			btdm8821ae.b_sw_dac_swing_on = false;

			// fw mechanism
			btdm8821ae.b_ps_tdma_on = false;
			btdm8821ae.b_tdma_on = false;
			btdm8821ae.tdma_dac_swing = TDMA_DAC_SWING_OFF;
			btdm8821ae.b2_ant_hid_en = true;
			btdm8821ae.fw_dac_swing_lvl = 0x20;
		}
	}

	if (rtl8821ae_dm_bt_need_to_dec_bt_pwr(hw)) {
		btdm8821ae.b_dec_bt_pwr = true;
	}

	if (rtl8821ae_dm_bt_is_coexist_state_changed(hw)) {
		rtl8821ae_dm_bt_set_bt_dm(hw, &btdm8821ae);
	}
}


void rtl8821ae_dm_bt_2_ant_2_dp_action_no_profile(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct btdm_8821ae btdm8821ae;
	u8 bt_rssi_state;

	rtl8821ae_dm_bt_btdm_structure_reload(hw, &btdm8821ae);

	btdm8821ae.b_rf_rx_lpf_shrink = true;
	btdm8821ae.b_low_penalty_rate_adaptive = true;
	btdm8821ae.b_reject_aggre_pkt = false;

	if (rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_20_40) {
		RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("HT40\n"));
		if (rtl8821ae_dm_bt_is_wifi_up_link(hw)) {
			RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("Wifi Uplink\n"));
			/* coex table */
			btdm8821ae.val_0x6c0 = 0x5a5a5a5a;
			btdm8821ae.val_0x6c8 = 0xcccc;
			btdm8821ae.val_0x6cc = 0x3;
			// sw mechanism
			btdm8821ae.b_agc_table_en = false;
			btdm8821ae.b_adc_back_off_on = true;
			btdm8821ae.b_sw_dac_swing_on = false;
			// fw mechanism
			btdm8821ae.b_tra_tdma_on = true;
			btdm8821ae.b_tdma_on = true;
			btdm8821ae.tdma_dac_swing = TDMA_DAC_SWING_ON;
			btdm8821ae.b2_ant_hid_en = false;
			//btSpec = BTHCI_GetBTCoreSpecByProf(Adapter, BT_PROFILE_A2DP);
			//if(btSpec >= BT_SPEC_2_1_EDR)
			{
				btdm8821ae.wlan_act_hi = 0x10;
				btdm8821ae.wlan_act_lo = 0x10;
			}
			//else
			//{
				//btdm8821ae.wlanActHi = 0x20;
				//btdm8821ae.wlanActLo = 0x20;
			//}
			btdm8821ae.bt_retry_index = 2;
			btdm8821ae.fw_dac_swing_lvl = 0x18;
		} else {
			RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("Wifi Downlink\n"));
			// coex table
			btdm8821ae.val_0x6c0 = 0x5a5a5a5a;
			btdm8821ae.val_0x6c8 = 0xcc;
			btdm8821ae.val_0x6cc = 0x3;
			// sw mechanism
			btdm8821ae.b_agc_table_en = false;
			btdm8821ae.b_adc_back_off_on = true;
			btdm8821ae.b_sw_dac_swing_on = false;
			// fw mechanism
			btdm8821ae.b_tra_tdma_on = true;
			btdm8821ae.b_tdma_on = true;
			btdm8821ae.tdma_dac_swing = TDMA_DAC_SWING_ON;
			btdm8821ae.b2_ant_hid_en = false;
			//btSpec = BTHCI_GetBTCoreSpecByProf(Adapter, BT_PROFILE_A2DP);
			//if(btSpec >= BT_SPEC_2_1_EDR)
			{
				btdm8821ae.wlan_act_hi = 0x10;
				btdm8821ae.wlan_act_lo = 0x10;
			}
			//else
			//{
			//	btdm8821ae.wlanActHi = 0x20;
			//	btdm8821ae.wlanActLo = 0x20;
			//}
			btdm8821ae.bt_retry_index = 2;
			btdm8821ae.fw_dac_swing_lvl = 0x40;
		}
	} else {
		RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("HT20 or Legacy\n"));
		bt_rssi_state = rtl8821ae_dm_bt_check_coex_rssi_state(hw, 2, BT_FW_COEX_THRESH_47, 0);

		if(rtl8821ae_dm_bt_is_wifi_up_link(hw))
		{
			RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("Wifi Uplink\n"));
			// coex table
			btdm8821ae.val_0x6c0 = 0x5a5a5a5a;
			btdm8821ae.val_0x6c8 = 0xcccc;
			btdm8821ae.val_0x6cc = 0x3;
			// sw mechanism
			if( (bt_rssi_state == BT_RSSI_STATE_HIGH) ||
				(bt_rssi_state == BT_RSSI_STATE_STAY_HIGH) )
			{
				RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("Wifi rssi high \n"));
				btdm8821ae.b_agc_table_en = true;
				btdm8821ae.b_adc_back_off_on = true;
				btdm8821ae.b_sw_dac_swing_on = false;
			} else {
				RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("Wifi rssi low \n"));
				btdm8821ae.b_agc_table_en = false;
				btdm8821ae.b_adc_back_off_on = false;
				btdm8821ae.b_sw_dac_swing_on = false;
			}
			// fw mechanism
			btdm8821ae.b_tra_tdma_on = true;
			btdm8821ae.b_tdma_on = true;
			btdm8821ae.tdma_dac_swing = TDMA_DAC_SWING_ON;
			btdm8821ae.b2_ant_hid_en = false;
			//btSpec = BTHCI_GetBTCoreSpecByProf(Adapter, BT_PROFILE_A2DP);
			//if(btSpec >= BT_SPEC_2_1_EDR)
			{
				btdm8821ae.wlan_act_hi = 0x10;
				btdm8821ae.wlan_act_lo = 0x10;
			}
			//else
			//{
				//btdm8821ae.wlanActHi = 0x20;
				//btdm8821ae.wlanActLo = 0x20;
			//}
			btdm8821ae.bt_retry_index = 2;
			btdm8821ae.fw_dac_swing_lvl = 0x18;
		}
		else
		{
			RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("Wifi Downlink\n"));
			// coex table
			btdm8821ae.val_0x6c0 = 0x5a5a5a5a;
			btdm8821ae.val_0x6c8 = 0xcc;
			btdm8821ae.val_0x6cc = 0x3;
			// sw mechanism
			if( (bt_rssi_state == BT_RSSI_STATE_HIGH) ||
				(bt_rssi_state == BT_RSSI_STATE_STAY_HIGH) )
			{
				RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("Wifi rssi high \n"));
				btdm8821ae.b_agc_table_en = true;
				btdm8821ae.b_adc_back_off_on = true;
				btdm8821ae.b_sw_dac_swing_on = false;
			}
			else
			{
				RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("Wifi rssi low \n"));
				btdm8821ae.b_agc_table_en = false;
				btdm8821ae.b_adc_back_off_on = false;
				btdm8821ae.b_sw_dac_swing_on = false;
			}
			// fw mechanism
			btdm8821ae.b_tra_tdma_on = true;
			btdm8821ae.b_tdma_on = true;
			btdm8821ae.tdma_dac_swing = TDMA_DAC_SWING_ON;
			btdm8821ae.b2_ant_hid_en = false;
			//btSpec = BTHCI_GetBTCoreSpecByProf(Adapter, BT_PROFILE_A2DP);
			//if(btSpec >= BT_SPEC_2_1_EDR)
			{
				btdm8821ae.wlan_act_hi = 0x10;
				btdm8821ae.wlan_act_lo = 0x10;
			}
			//else
			//{
				//btdm8821ae.wlanActHi = 0x20;
				//btdm8821ae.wlanActLo = 0x20;
			//}
			btdm8821ae.bt_retry_index = 2;
			btdm8821ae.fw_dac_swing_lvl = 0x40;
		}
	}

	if (rtl8821ae_dm_bt_need_to_dec_bt_pwr(hw)) {
		btdm8821ae.b_dec_bt_pwr = true;
	}

	if (rtl8821ae_dm_bt_is_coexist_state_changed(hw)) {
		rtl8821ae_dm_bt_set_bt_dm(hw, &btdm8821ae);
	}
}


//============================================================
// extern function start with BTDM_
//============================================================
u32 rtl8821ae_dm_bt_tx_rx_couter_h(struct ieee80211_hw *hw)
{
	u32	counters=0;

	counters = hal_coex_8821ae.high_priority_tx + hal_coex_8821ae.high_priority_rx ;
	return counters;
}

u32 rtl8821ae_dm_bt_tx_rx_couter_l(struct ieee80211_hw *hw)
{
	u32 counters=0;

	counters = hal_coex_8821ae.low_priority_tx + hal_coex_8821ae.low_priority_rx ;
	return counters;
}

u8 rtl8821ae_dm_bt_bt_tx_rx_counter_level(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
	u32	bt_tx_rx_cnt = 0;
	u8	bt_tx_rx_cnt_lvl = 0;

	bt_tx_rx_cnt = rtl8821ae_dm_bt_tx_rx_couter_h(hw)
				+ rtl8821ae_dm_bt_tx_rx_couter_l(hw);
	RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
		("[BTCoex], BT TxRx Counters = %d\n", bt_tx_rx_cnt));

	rtlpcipriv->btcoexist.current_state_h &= ~\
		 (BT_COEX_STATE_BT_CNT_LEVEL_0 | BT_COEX_STATE_BT_CNT_LEVEL_1|
		  BT_COEX_STATE_BT_CNT_LEVEL_2);

	if (bt_tx_rx_cnt >= BT_TXRX_CNT_THRES_3) {
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("[BTCoex], BT TxRx Counters at level 3\n"));
		bt_tx_rx_cnt_lvl = BT_TXRX_CNT_LEVEL_3;
		rtlpcipriv->btcoexist.current_state_h |= BT_COEX_STATE_BT_CNT_LEVEL_3;
	} else if(bt_tx_rx_cnt >= BT_TXRX_CNT_THRES_2) {
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("[BTCoex], BT TxRx Counters at level 2\n"));
		bt_tx_rx_cnt_lvl = BT_TXRX_CNT_LEVEL_2;
		rtlpcipriv->btcoexist.current_state_h |= BT_COEX_STATE_BT_CNT_LEVEL_2;
	} else if(bt_tx_rx_cnt >= BT_TXRX_CNT_THRES_1) {
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("[BTCoex], BT TxRx Counters at level 1\n"));
		bt_tx_rx_cnt_lvl = BT_TXRX_CNT_LEVEL_1;
		rtlpcipriv->btcoexist.current_state_h  |= BT_COEX_STATE_BT_CNT_LEVEL_1;
	} else {
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("[BTCoex], BT TxRx Counters at level 0\n"));
		bt_tx_rx_cnt_lvl = BT_TXRX_CNT_LEVEL_0;
		rtlpcipriv->btcoexist.current_state_h |= BT_COEX_STATE_BT_CNT_LEVEL_0;
	}
	return bt_tx_rx_cnt_lvl;
}


void rtl8821ae_dm_bt_2_ant_hid_sco_esco(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct btdm_8821ae btdm8821ae;

	u8 bt_rssi_state, bt_rssi_state1;
	u8	bt_tx_rx_cnt_lvl = 0;

	rtl8821ae_dm_bt_btdm_structure_reload(hw, &btdm8821ae);


	btdm8821ae.b_rf_rx_lpf_shrink = true;
	btdm8821ae.b_low_penalty_rate_adaptive = true;
	btdm8821ae.b_reject_aggre_pkt = false;

	bt_tx_rx_cnt_lvl = rtl8821ae_dm_bt_bt_tx_rx_counter_level(hw);
	RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("[BTCoex], BT TxRx Counters = %d\n", bt_tx_rx_cnt_lvl));

	if(rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_20_40)
	{
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("HT40\n"));
		// coex table
		btdm8821ae.val_0x6c0 = 0x55555555;
		btdm8821ae.val_0x6c8 = 0xffff;
		btdm8821ae.val_0x6cc = 0x3;

		// sw mechanism
		btdm8821ae.b_agc_table_en = false;
		btdm8821ae.b_adc_back_off_on = false;
		btdm8821ae.b_sw_dac_swing_on = false;

		// fw mechanism
		btdm8821ae.b_ps_tdma_on = true;
		if (bt_tx_rx_cnt_lvl == BT_TXRX_CNT_LEVEL_2) {
			RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("[BTCoex], BT TxRx Counters >= 1400\n"));
			btdm8821ae.ps_tdma_byte[0] = 0xa3;
			btdm8821ae.ps_tdma_byte[1] = 0x5;
			btdm8821ae.ps_tdma_byte[2] = 0x5;
			btdm8821ae.ps_tdma_byte[3] = 0x2;
			btdm8821ae.ps_tdma_byte[4] = 0x80;
		} else if(bt_tx_rx_cnt_lvl == BT_TXRX_CNT_LEVEL_1) {
			RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("[BTCoex], BT TxRx Counters >= 1200 && < 1400\n"));
			btdm8821ae.ps_tdma_byte[0] = 0xa3;
			btdm8821ae.ps_tdma_byte[1] = 0xa;
			btdm8821ae.ps_tdma_byte[2] = 0xa;
			btdm8821ae.ps_tdma_byte[3] = 0x2;
			btdm8821ae.ps_tdma_byte[4] = 0x80;
		} else {
			RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("[BTCoex], BT TxRx Counters < 1200\n"));
			btdm8821ae.ps_tdma_byte[0] = 0xa3;
			btdm8821ae.ps_tdma_byte[1] = 0xf;
			btdm8821ae.ps_tdma_byte[2] = 0xf;
			btdm8821ae.ps_tdma_byte[3] = 0x2;
			btdm8821ae.ps_tdma_byte[4] = 0x80;
		}
	} else {
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("HT20 or Legacy\n"));
		bt_rssi_state = rtl8821ae_dm_bt_check_coex_rssi_state(hw, 2, 47, 0);
		bt_rssi_state1 = rtl8821ae_dm_bt_check_coex_rssi_state1(hw, 2, 27, 0);

		// coex table
		btdm8821ae.val_0x6c0 = 0x55555555;
		btdm8821ae.val_0x6c8 = 0xffff;
		btdm8821ae.val_0x6cc = 0x3;

		// sw mechanism
		if( (bt_rssi_state == BT_RSSI_STATE_HIGH) ||
			(bt_rssi_state == BT_RSSI_STATE_STAY_HIGH) ) {
			RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("Wifi rssi high \n"));
			btdm8821ae.b_agc_table_en = true;
			btdm8821ae.b_adc_back_off_on = true;
			btdm8821ae.b_sw_dac_swing_on = false;
		} else {
			RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("Wifi rssi low \n"));
			btdm8821ae.b_agc_table_en = false;
			btdm8821ae.b_adc_back_off_on = false;
			btdm8821ae.b_sw_dac_swing_on = false;
		}

		// fw mechanism
		btdm8821ae.b_ps_tdma_on = true;
		if( (bt_rssi_state1 == BT_RSSI_STATE_HIGH) ||
			(bt_rssi_state1 == BT_RSSI_STATE_STAY_HIGH) ) {
			RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,("Wifi rssi-1 high \n"));
			// only rssi high we need to do this,
			// when rssi low, the value will modified by fw
			rtl_write_byte(rtlpriv, 0x883, 0x40);
			if(bt_tx_rx_cnt_lvl == BT_TXRX_CNT_LEVEL_2) {
				RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("[BTCoex], BT TxRx Counters >= 1400\n"));
				btdm8821ae.ps_tdma_byte[0] = 0xa3;
				btdm8821ae.ps_tdma_byte[1] = 0x5;
				btdm8821ae.ps_tdma_byte[2] = 0x5;
				btdm8821ae.ps_tdma_byte[3] = 0x83;
				btdm8821ae.ps_tdma_byte[4] = 0x80;
			} else if(bt_tx_rx_cnt_lvl == BT_TXRX_CNT_LEVEL_1) {
				RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("[BTCoex], BT TxRx Counters >= 1200 && < 1400\n"));
				btdm8821ae.ps_tdma_byte[0] = 0xa3;
				btdm8821ae.ps_tdma_byte[1] = 0xa;
				btdm8821ae.ps_tdma_byte[2] = 0xa;
				btdm8821ae.ps_tdma_byte[3] = 0x83;
				btdm8821ae.ps_tdma_byte[4] = 0x80;
			} else {
				RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("[BTCoex], BT TxRx Counters < 1200\n"));
				btdm8821ae.ps_tdma_byte[0] = 0xa3;
				btdm8821ae.ps_tdma_byte[1] = 0xf;
				btdm8821ae.ps_tdma_byte[2] = 0xf;
				btdm8821ae.ps_tdma_byte[3] = 0x83;
				btdm8821ae.ps_tdma_byte[4] = 0x80;
			}
		} else {
			RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("Wifi rssi-1 low \n"));
			if(bt_tx_rx_cnt_lvl == BT_TXRX_CNT_LEVEL_2)
			{
				RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("[BTCoex], BT TxRx Counters >= 1400\n"));
				btdm8821ae.ps_tdma_byte[0] = 0xa3;
				btdm8821ae.ps_tdma_byte[1] = 0x5;
				btdm8821ae.ps_tdma_byte[2] = 0x5;
				btdm8821ae.ps_tdma_byte[3] = 0x2;
				btdm8821ae.ps_tdma_byte[4] = 0x80;
			} else if(bt_tx_rx_cnt_lvl == BT_TXRX_CNT_LEVEL_1) {
				RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("[BTCoex], BT TxRx Counters >= 1200 && < 1400\n"));
				btdm8821ae.ps_tdma_byte[0] = 0xa3;
				btdm8821ae.ps_tdma_byte[1] = 0xa;
				btdm8821ae.ps_tdma_byte[2] = 0xa;
				btdm8821ae.ps_tdma_byte[3] = 0x2;
				btdm8821ae.ps_tdma_byte[4] = 0x80;
			} else {
				RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("[BTCoex], BT TxRx Counters < 1200\n"));
				btdm8821ae.ps_tdma_byte[0] = 0xa3;
				btdm8821ae.ps_tdma_byte[1] = 0xf;
				btdm8821ae.ps_tdma_byte[2] = 0xf;
				btdm8821ae.ps_tdma_byte[3] = 0x2;
				btdm8821ae.ps_tdma_byte[4] = 0x80;
			}
		}
	}

	if (rtl8821ae_dm_bt_need_to_dec_bt_pwr(hw)) {
		btdm8821ae.b_dec_bt_pwr = true;
	}

	// Always ignore WlanAct if bHid|bSCOBusy|bSCOeSCO

	RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
		("[BTCoex], BT btInqPageStartTime = 0x%x, btTxRxCntLvl = %d\n",
		hal_coex_8821ae.bt_inq_page_start_time, bt_tx_rx_cnt_lvl));
	if( (hal_coex_8821ae.bt_inq_page_start_time) ||
		(BT_TXRX_CNT_LEVEL_3 == bt_tx_rx_cnt_lvl) ) {
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("[BTCoex], Set BT inquiry / page scan 0x3a setting\n"));
		btdm8821ae.b_ps_tdma_on = true;
		btdm8821ae.ps_tdma_byte[0] = 0xa3;
		btdm8821ae.ps_tdma_byte[1] = 0x5;
		btdm8821ae.ps_tdma_byte[2] = 0x5;
		btdm8821ae.ps_tdma_byte[3] = 0x2;
		btdm8821ae.ps_tdma_byte[4] = 0x80;
	}

	if(rtl8821ae_dm_bt_is_coexist_state_changed(hw)) {
		rtl8821ae_dm_bt_set_bt_dm(hw, &btdm8821ae);
	}
}

void rtl8821ae_dm_bt_2_ant_ftp_a2dp(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct btdm_8821ae btdm8821ae;

	u8 bt_rssi_state, bt_rssi_state1;
	u32 bt_tx_rx_cnt_lvl = 0;

	rtl8821ae_dm_bt_btdm_structure_reload(hw, &btdm8821ae);

	btdm8821ae.b_rf_rx_lpf_shrink = true;
	btdm8821ae.b_low_penalty_rate_adaptive = true;
	btdm8821ae.b_reject_aggre_pkt = false;

	bt_tx_rx_cnt_lvl = rtl8821ae_dm_bt_bt_tx_rx_counter_level(hw);

	RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("[BTCoex], BT TxRx Counters = %d\n", bt_tx_rx_cnt_lvl));

	if(rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_20_40)
	{
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("HT40\n"));
		bt_rssi_state = rtl8821ae_dm_bt_check_coex_rssi_state(hw, 2, 37, 0);

		// coex table
		btdm8821ae.val_0x6c0 = 0x55555555;
		btdm8821ae.val_0x6c8 = 0xffff;
		btdm8821ae.val_0x6cc = 0x3;

		// sw mechanism
		btdm8821ae.b_agc_table_en = false;
		btdm8821ae.b_adc_back_off_on = true;
		btdm8821ae.b_sw_dac_swing_on = false;

		// fw mechanism
		btdm8821ae.b_ps_tdma_on = true;
		if ((bt_rssi_state == BT_RSSI_STATE_HIGH) ||
			(bt_rssi_state == BT_RSSI_STATE_STAY_HIGH) ) {
			RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("Wifi rssi high \n"));
			if (bt_tx_rx_cnt_lvl == BT_TXRX_CNT_LEVEL_2) {
				RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("[BTCoex], BT TxRx Counters >= 1400\n"));
				btdm8821ae.ps_tdma_byte[0] = 0xa3;
				btdm8821ae.ps_tdma_byte[1] = 0x5;
				btdm8821ae.ps_tdma_byte[2] = 0x5;
				btdm8821ae.ps_tdma_byte[3] = 0x81;
				btdm8821ae.ps_tdma_byte[4] = 0x80;
			} else if(bt_tx_rx_cnt_lvl == BT_TXRX_CNT_LEVEL_1) {
				RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("[BTCoex], BT TxRx Counters >= 1200 && < 1400\n"));
				btdm8821ae.ps_tdma_byte[0] = 0xa3;
				btdm8821ae.ps_tdma_byte[1] = 0xa;
				btdm8821ae.ps_tdma_byte[2] = 0xa;
				btdm8821ae.ps_tdma_byte[3] = 0x81;
				btdm8821ae.ps_tdma_byte[4] = 0x80;
			} else {
				RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("[BTCoex], BT TxRx Counters < 1200\n"));
				btdm8821ae.ps_tdma_byte[0] = 0xa3;
				btdm8821ae.ps_tdma_byte[1] = 0xf;
				btdm8821ae.ps_tdma_byte[2] = 0xf;
				btdm8821ae.ps_tdma_byte[3] = 0x81;
				btdm8821ae.ps_tdma_byte[4] = 0x80;
			}
		} else {
			RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("Wifi rssi low \n"));
			if(bt_tx_rx_cnt_lvl == BT_TXRX_CNT_LEVEL_2) {
				RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("[BTCoex], BT TxRx Counters >= 1400\n"));
				btdm8821ae.ps_tdma_byte[0] = 0xa3;
				btdm8821ae.ps_tdma_byte[1] = 0x5;
				btdm8821ae.ps_tdma_byte[2] = 0x5;
				btdm8821ae.ps_tdma_byte[3] = 0x0;
				btdm8821ae.ps_tdma_byte[4] = 0x80;
			} else if(bt_tx_rx_cnt_lvl == BT_TXRX_CNT_LEVEL_1) {
				RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("[BTCoex], BT TxRx Counters >= 1200 && < 1400\n"));
				btdm8821ae.ps_tdma_byte[0] = 0xa3;
				btdm8821ae.ps_tdma_byte[1] = 0xa;
				btdm8821ae.ps_tdma_byte[2] = 0xa;
				btdm8821ae.ps_tdma_byte[3] = 0x0;
				btdm8821ae.ps_tdma_byte[4] = 0x80;
			} else {
				RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("[BTCoex], BT TxRx Counters < 1200\n"));
				btdm8821ae.ps_tdma_byte[0] = 0xa3;
				btdm8821ae.ps_tdma_byte[1] = 0xf;
				btdm8821ae.ps_tdma_byte[2] = 0xf;
				btdm8821ae.ps_tdma_byte[3] = 0x0;
				btdm8821ae.ps_tdma_byte[4] = 0x80;
			}
		}
	} else {
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("HT20 or Legacy\n"));
		bt_rssi_state = rtl8821ae_dm_bt_check_coex_rssi_state(hw, 2, 47, 0);
		bt_rssi_state1 = rtl8821ae_dm_bt_check_coex_rssi_state1(hw, 2, 27, 0);

		// coex table
		btdm8821ae.val_0x6c0 = 0x55555555;
		btdm8821ae.val_0x6c8 = 0xffff;
		btdm8821ae.val_0x6cc = 0x3;

		// sw mechanism
		if( (bt_rssi_state == BT_RSSI_STATE_HIGH) ||
			(bt_rssi_state == BT_RSSI_STATE_STAY_HIGH) ) {
			RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("Wifi rssi high \n"));
			btdm8821ae.b_agc_table_en = true;
			btdm8821ae.b_adc_back_off_on = true;
			btdm8821ae.b_sw_dac_swing_on = false;
		} else {
			RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("Wifi rssi low \n"));
			btdm8821ae.b_agc_table_en = false;
			btdm8821ae.b_adc_back_off_on = false;
			btdm8821ae.b_sw_dac_swing_on = false;
		}

		// fw mechanism
		btdm8821ae.b_ps_tdma_on = true;
		if( (bt_rssi_state1 == BT_RSSI_STATE_HIGH) ||
			(bt_rssi_state1 == BT_RSSI_STATE_STAY_HIGH) ) {
			RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("Wifi rssi-1 high \n"));
			// only rssi high we need to do this,
			// when rssi low, the value will modified by fw
			rtl_write_byte(rtlpriv, 0x883, 0x40);
			if (bt_tx_rx_cnt_lvl == BT_TXRX_CNT_LEVEL_2) {
				RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("[BTCoex], BT TxRx Counters >= 1400\n"));
				btdm8821ae.ps_tdma_byte[0] = 0xa3;
				btdm8821ae.ps_tdma_byte[1] = 0x5;
				btdm8821ae.ps_tdma_byte[2] = 0x5;
				btdm8821ae.ps_tdma_byte[3] = 0x81;
				btdm8821ae.ps_tdma_byte[4] = 0x80;
			} else if(bt_tx_rx_cnt_lvl == BT_TXRX_CNT_LEVEL_1) {
				RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("[BTCoex], BT TxRx Counters >= 1200 && < 1400\n"));
				btdm8821ae.ps_tdma_byte[0] = 0xa3;
				btdm8821ae.ps_tdma_byte[1] = 0xa;
				btdm8821ae.ps_tdma_byte[2] = 0xa;
				btdm8821ae.ps_tdma_byte[3] = 0x81;
				btdm8821ae.ps_tdma_byte[4] = 0x80;
			} else {
				RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("[BTCoex], BT TxRx Counters < 1200\n"));
				btdm8821ae.ps_tdma_byte[0] = 0xa3;
				btdm8821ae.ps_tdma_byte[1] = 0xf;
				btdm8821ae.ps_tdma_byte[2] = 0xf;
				btdm8821ae.ps_tdma_byte[3] = 0x81;
				btdm8821ae.ps_tdma_byte[4] = 0x80;
			}
		} else {
			RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("Wifi rssi-1 low \n"));
			if(bt_tx_rx_cnt_lvl == BT_TXRX_CNT_LEVEL_2) {
				RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("[BTCoex], BT TxRx Counters >= 1400\n"));
				btdm8821ae.ps_tdma_byte[0] = 0xa3;
				btdm8821ae.ps_tdma_byte[1] = 0x5;
				btdm8821ae.ps_tdma_byte[2] = 0x5;
				btdm8821ae.ps_tdma_byte[3] = 0x0;
				btdm8821ae.ps_tdma_byte[4] = 0x80;
			} else if(bt_tx_rx_cnt_lvl == BT_TXRX_CNT_LEVEL_1) {
				RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("[BTCoex], BT TxRx Counters >= 1200 && < 1400\n"));
				btdm8821ae.ps_tdma_byte[0] = 0xa3;
				btdm8821ae.ps_tdma_byte[1] = 0xa;
				btdm8821ae.ps_tdma_byte[2] = 0xa;
				btdm8821ae.ps_tdma_byte[3] = 0x0;
				btdm8821ae.ps_tdma_byte[4] = 0x80;
			} else {
				RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("[BTCoex], BT TxRx Counters < 1200\n"));
				btdm8821ae.ps_tdma_byte[0] = 0xa3;
				btdm8821ae.ps_tdma_byte[1] = 0xf;
				btdm8821ae.ps_tdma_byte[2] = 0xf;
				btdm8821ae.ps_tdma_byte[3] = 0x0;
				btdm8821ae.ps_tdma_byte[4] = 0x80;
			}
		}
	}

	if(rtl8821ae_dm_bt_need_to_dec_bt_pwr(hw)) {
		btdm8821ae.b_dec_bt_pwr = true;
	}

	RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
	       ("[BTCoex], BT btInqPageStartTime = 0x%x, btTxRxCntLvl = %d\n",
	        hal_coex_8821ae.bt_inq_page_start_time, bt_tx_rx_cnt_lvl));

	if( (hal_coex_8821ae.bt_inq_page_start_time) ||
		(BT_TXRX_CNT_LEVEL_3 == bt_tx_rx_cnt_lvl) )
	{
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
				("[BTCoex], Set BT inquiry / page scan 0x3a setting\n"));
		btdm8821ae.b_ps_tdma_on = true;
		btdm8821ae.ps_tdma_byte[0] = 0xa3;
		btdm8821ae.ps_tdma_byte[1] = 0x5;
		btdm8821ae.ps_tdma_byte[2] = 0x5;
		btdm8821ae.ps_tdma_byte[3] = 0x83;
		btdm8821ae.ps_tdma_byte[4] = 0x80;
	}

	if(rtl8821ae_dm_bt_is_coexist_state_changed(hw)){
		rtl8821ae_dm_bt_set_bt_dm(hw, &btdm8821ae);
	}
}

void rtl8821ae_dm_bt_inq_page_monitor(struct ieee80211_hw *hw)
{
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 cur_time;
	cur_time = jiffies;
	if (hal_coex_8821ae.b_c2h_bt_inquiry_page) {
		//pHalData->btcoexist.halCoex8821ae.btInquiryPageCnt++;
		// bt inquiry or page is started.
		if(hal_coex_8821ae.bt_inq_page_start_time == 0){
			rtlpcipriv->btcoexist.current_state  |= BT_COEX_STATE_BT_INQ_PAGE;
			hal_coex_8821ae.bt_inq_page_start_time = cur_time;
			RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
				("[BTCoex], BT Inquiry/page is started at time : 0x%x \n",
				hal_coex_8821ae.bt_inq_page_start_time));
		}
	}
	RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
		("[BTCoex], BT Inquiry/page started time : 0x%x, cur_time : 0x%x \n",
		hal_coex_8821ae.bt_inq_page_start_time, cur_time));

	if (hal_coex_8821ae.bt_inq_page_start_time) {
		if ((((long)cur_time - (long)hal_coex_8821ae.bt_inq_page_start_time) / HZ) >= 10) {
			RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,  ("[BTCoex], BT Inquiry/page >= 10sec!!!"));
			hal_coex_8821ae.bt_inq_page_start_time = 0;
			rtlpcipriv->btcoexist.current_state &=~ BT_COEX_STATE_BT_INQ_PAGE;
		}
	}

#if 0
	if (hal_coex_8821ae.b_c2h_bt_inquiry_page) {
		hal_coex_8821ae.b_c2h_bt_inquiry_page++;
		// bt inquiry or page is started.
	} if(hal_coex_8821ae.b_c2h_bt_inquiry_page) {
		rtlpcipriv->btcoexist.current_state |= BT_COEX_STATE_BT_INQ_PAGE;
		if(hal_coex_8821ae.bt_inquiry_page_cnt >= 4)
			hal_coex_8821ae.bt_inquiry_page_cnt = 0;
		hal_coex_8821ae.bt_inquiry_page_cnt++;
	} else {
		rtlpcipriv->btcoexist.current_state &=~ BT_COEX_STATE_BT_INQ_PAGE;
	}
#endif
}

void rtl8821ae_dm_bt_reset_action_profile_state(struct ieee80211_hw *hw)
{
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);

	rtlpcipriv->btcoexist.current_state &= ~\
		(BT_COEX_STATE_PROFILE_HID | BT_COEX_STATE_PROFILE_A2DP|
		BT_COEX_STATE_PROFILE_PAN | BT_COEX_STATE_PROFILE_SCO);

	rtlpcipriv->btcoexist.current_state &= ~\
		(BT_COEX_STATE_BTINFO_COMMON | BT_COEX_STATE_BTINFO_B_HID_SCOESCO|
		BT_COEX_STATE_BTINFO_B_FTP_A2DP);
}

void _rtl8821ae_dm_bt_coexist_2_ant(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
	u8 bt_retry_cnt;
	u8 bt_info_original;
	RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("[BTCoex] Get bt info by fw!!\n"));

	_rtl8821ae_dm_bt_check_wifi_state(hw);

	if (hal_coex_8821ae.b_c2h_bt_info_req_sent) {
			RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
				("[BTCoex] c2h for bt_info not rcvd yet!!\n"));
	}

	bt_retry_cnt = hal_coex_8821ae.bt_retry_cnt;
	bt_info_original = hal_coex_8821ae.c2h_bt_info_original;

	// when bt inquiry or page scan, we have to set h2c 0x25
	// ignore wlanact for continuous 4x2secs
	rtl8821ae_dm_bt_inq_page_monitor(hw);
	rtl8821ae_dm_bt_reset_action_profile_state(hw);

	if(rtl8821ae_dm_bt_is_2_ant_common_action(hw)) {
		rtlpcipriv->btcoexist.bt_profile_case = BT_COEX_MECH_COMMON;
		rtlpcipriv->btcoexist.bt_profile_action= BT_COEX_MECH_COMMON;
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("Action 2-Ant common.\n"));
	} else {
		if( (bt_info_original & BTINFO_B_HID) ||
			(bt_info_original & BTINFO_B_SCO_BUSY) ||
			(bt_info_original & BTINFO_B_SCO_ESCO) ) {
				rtlpcipriv->btcoexist.current_state |= BT_COEX_STATE_BTINFO_B_HID_SCOESCO;
				rtlpcipriv->btcoexist.bt_profile_case = BT_COEX_MECH_HID_SCO_ESCO;
				rtlpcipriv->btcoexist.bt_profile_action = BT_COEX_MECH_HID_SCO_ESCO;
				RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("[BTCoex], BTInfo: bHid|bSCOBusy|bSCOeSCO\n"));
				rtl8821ae_dm_bt_2_ant_hid_sco_esco(hw);
		} else if( (bt_info_original & BTINFO_B_FTP) ||
				(bt_info_original & BTINFO_B_A2DP) ) {
				rtlpcipriv->btcoexist.current_state |= BT_COEX_STATE_BTINFO_B_FTP_A2DP;
				rtlpcipriv->btcoexist.bt_profile_case = BT_COEX_MECH_FTP_A2DP;
				rtlpcipriv->btcoexist.bt_profile_action = BT_COEX_MECH_FTP_A2DP;
				RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("BTInfo: bFTP|bA2DP\n"));
				rtl8821ae_dm_bt_2_ant_ftp_a2dp(hw);
		} else {
				rtlpcipriv->btcoexist.current_state |= BT_COEX_STATE_BTINFO_B_HID_SCOESCO;
				rtlpcipriv->btcoexist.bt_profile_case = BT_COEX_MECH_NONE;
				rtlpcipriv->btcoexist.bt_profile_action= BT_COEX_MECH_NONE;
				RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("[BTCoex], BTInfo: undefined case!!!!\n"));
				rtl8821ae_dm_bt_2_ant_hid_sco_esco(hw);
		}
	}
}

void _rtl8821ae_dm_bt_coexist_1_ant(struct ieee80211_hw *hw)
{
	return;
}

void rtl8821ae_dm_bt_hw_coex_all_off_8723a(struct ieee80211_hw *hw)
{
	rtl8821ae_dm_bt_set_coex_table(hw, 0x5a5aaaaa, 0xcc, 0x3);
	rtl8821ae_dm_bt_set_hw_pta_mode(hw, true);
}

void rtl8821ae_dm_bt_fw_coex_all_off_8723a(struct ieee80211_hw *hw)
{
	rtl8821ae_dm_bt_set_fw_ignore_wlan_act(hw, false);
	rtl8821ae_dm_bt_set_fw_3a(hw, 0x0, 0x0, 0x0, 0x8, 0x0);
	rtl8821ae_dm_bt_set_fw_2_ant_hid(hw, false, false);
	rtl8821ae_dm_bt_set_fw_tra_tdma_ctrl(hw, false, TDMA_2ANT, TDMA_NAV_OFF);
	rtl8821ae_dm_bt_set_fw_tdma_ctrl(hw, false, TDMA_2ANT,
				TDMA_NAV_OFF, TDMA_DAC_SWING_OFF);
	rtl8821ae_dm_bt_set_fw_dac_swing_level(hw, 0);
	rtl8821ae_dm_bt_set_fw_bt_hid_info(hw, false);
	rtl8821ae_dm_bt_set_fw_bt_retry_index(hw, 2);
	rtl8821ae_dm_bt_set_fw_wlan_act(hw, 0x10, 0x10);
	rtl8821ae_dm_bt_set_fw_dec_bt_pwr(hw, false);
}

void rtl8821ae_dm_bt_sw_coex_all_off_8723a(struct ieee80211_hw *hw)
{
	rtl8821ae_dm_bt_agc_table(hw, BT_AGCTABLE_OFF);
	rtl8821ae_dm_bt_bb_back_off_level(hw, BT_BB_BACKOFF_OFF);
	rtl8821ae_dm_bt_reject_ap_aggregated_packet(hw, false);

	rtl8821ae_dm_bt_set_sw_penalty_tx_rate_adaptive(hw,
							BT_TX_RATE_ADAPTIVE_NORMAL);
	rtl8821ae_dm_bt_set_sw_rf_rx_lpf_corner(hw, BT_RF_RX_LPF_CORNER_RESUME);
	rtl8821ae_dm_bt_set_sw_full_time_dac_swing(hw, false, 0xc0);
}

void rtl8821ae_dm_bt_query_bt_information(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 h2c_parameter[1] = {0};

	hal_coex_8821ae.b_c2h_bt_info_req_sent = true;

	h2c_parameter[0] |=  BIT(0);

	RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
		("Query Bt information, write 0x38=0x%x\n", h2c_parameter[0]));

	rtl8821ae_fill_h2c_cmd(hw, 0x38, 1, h2c_parameter);
}

void rtl8821ae_dm_bt_bt_hw_counters_monitor(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
	u32 reg_hp_tx_rx, reg_lp_tx_rx, u32_tmp;
	u32 reg_hp_tx=0, reg_hp_rx=0, reg_lp_tx=0, reg_lp_rx=0;

	reg_hp_tx_rx = REG_HIGH_PRIORITY_TXRX;
	reg_lp_tx_rx = REG_LOW_PRIORITY_TXRX;

	u32_tmp = rtl_read_dword(rtlpriv, reg_hp_tx_rx);
	reg_hp_tx = u32_tmp & MASKLWORD;
	reg_hp_rx = (u32_tmp & MASKHWORD)>>16;

	u32_tmp = rtl_read_dword(rtlpriv, reg_lp_tx_rx);
	reg_lp_tx = u32_tmp & MASKLWORD;
	reg_lp_rx = (u32_tmp & MASKHWORD)>>16;

	if(rtlpcipriv->btcoexist.lps_counter > 1) {
		reg_hp_tx %= rtlpcipriv->btcoexist.lps_counter;
		reg_hp_rx %= rtlpcipriv->btcoexist.lps_counter;
		reg_lp_tx %= rtlpcipriv->btcoexist.lps_counter;
		reg_lp_rx %= rtlpcipriv->btcoexist.lps_counter;
	}

	hal_coex_8821ae.high_priority_tx = reg_hp_tx;
	hal_coex_8821ae.high_priority_rx = reg_hp_rx;
	hal_coex_8821ae.low_priority_tx = reg_lp_tx;
	hal_coex_8821ae.low_priority_rx = reg_lp_rx;

	RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
		("High Priority Tx/Rx (reg 0x%x)=%x(%d)/%x(%d)\n",
		reg_hp_tx_rx, reg_hp_tx, reg_hp_tx, reg_hp_rx, reg_hp_rx));
	RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
		("Low Priority Tx/Rx (reg 0x%x)=%x(%d)/%x(%d)\n",
		reg_lp_tx_rx, reg_lp_tx, reg_lp_tx, reg_lp_rx, reg_lp_rx));
	rtlpcipriv->btcoexist.lps_counter = 0;
	//rtl_write_byte(rtlpriv, 0x76e, 0xc);
}

void rtl8821ae_dm_bt_bt_enable_disable_check(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
	bool bt_alife = true;

	if (hal_coex_8821ae.high_priority_tx == 0 &&
		hal_coex_8821ae.high_priority_rx == 0 &&
		hal_coex_8821ae.low_priority_tx == 0 &&
		hal_coex_8821ae.low_priority_rx == 0) {
		bt_alife = false;
	}
	if (hal_coex_8821ae.high_priority_tx == 0xeaea &&
		hal_coex_8821ae.high_priority_rx == 0xeaea &&
		hal_coex_8821ae.low_priority_tx == 0xeaea &&
		hal_coex_8821ae.low_priority_rx == 0xeaea) {
		bt_alife = false;
	}
	if (hal_coex_8821ae.high_priority_tx == 0xffff &&
		hal_coex_8821ae.high_priority_rx == 0xffff &&
		hal_coex_8821ae.low_priority_tx == 0xffff &&
		hal_coex_8821ae.low_priority_rx == 0xffff) {
		bt_alife = false;
	}
	if (bt_alife) {
		rtlpcipriv->btcoexist.bt_active_zero_cnt = 0;
		rtlpcipriv->btcoexist.b_cur_bt_disabled = false;
		RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("8821AE BT is enabled !!\n"));
	} else {
		rtlpcipriv->btcoexist.bt_active_zero_cnt++;
		RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
			("8821AE bt all counters=0, %d times!!\n",
			rtlpcipriv->btcoexist.bt_active_zero_cnt));
		if (rtlpcipriv->btcoexist.bt_active_zero_cnt >= 2) {
			rtlpcipriv->btcoexist.b_cur_bt_disabled = true;
			RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("8821AE BT is disabled !!\n"));
		}
	}
	if (rtlpcipriv->btcoexist.b_pre_bt_disabled !=
		rtlpcipriv->btcoexist.b_cur_bt_disabled) {
		RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("8821AE BT is from %s to %s!!\n",
			(rtlpcipriv->btcoexist.b_pre_bt_disabled ? "disabled":"enabled"),
			(rtlpcipriv->btcoexist.b_cur_bt_disabled ? "disabled":"enabled")));
		rtlpcipriv->btcoexist.b_pre_bt_disabled
			= rtlpcipriv->btcoexist.b_cur_bt_disabled;
	}
}


void rtl8821ae_dm_bt_coexist(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);

	rtl8821ae_dm_bt_query_bt_information(hw);
	rtl8821ae_dm_bt_bt_hw_counters_monitor(hw);
	rtl8821ae_dm_bt_bt_enable_disable_check(hw);

	if (rtlpcipriv->btcoexist.bt_ant_num == ANT_X2) {
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("[BTCoex], 2 Ant mechanism\n"));
		_rtl8821ae_dm_bt_coexist_2_ant(hw);
	} else {
		RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("[BTCoex], 1 Ant mechanism\n"));
		_rtl8821ae_dm_bt_coexist_1_ant(hw);
	}

	if (!rtl8821ae_dm_bt_is_same_coexist_state(hw)) {
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
			("[BTCoex], Coexist State[bitMap] change from 0x%x%8x to 0x%x%8x\n",
			rtlpcipriv->btcoexist.previous_state_h,
			rtlpcipriv->btcoexist.previous_state,
			rtlpcipriv->btcoexist.current_state_h,
			rtlpcipriv->btcoexist.current_state));
		rtlpcipriv->btcoexist.previous_state
			= rtlpcipriv->btcoexist.current_state;
		rtlpcipriv->btcoexist.previous_state_h
			= rtlpcipriv->btcoexist.current_state_h;
	}
}

void rtl8821ae_dm_bt_parse_bt_info(struct ieee80211_hw *hw, u8 * tmp_buf, u8 len)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
	u8 bt_info;
	u8 i;

	hal_coex_8821ae.b_c2h_bt_info_req_sent = false;
	hal_coex_8821ae.bt_retry_cnt = 0;
	for (i = 0; i < len; i++) {
		if (i == 0) {
			hal_coex_8821ae.c2h_bt_info_original = tmp_buf[i];
		} else if (i == 1) {
			hal_coex_8821ae.bt_retry_cnt = tmp_buf[i];
		}
		if(i == len-1) {
			RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("0x%2x]", tmp_buf[i]));
		} else {
			RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("0x%2x, ", tmp_buf[i]));
		}
	}
	RT_TRACE(COMP_BT_COEXIST, DBG_DMESG,
		("BT info bt_info (Data)= 0x%x\n",hal_coex_8821ae.c2h_bt_info_original));
	bt_info = hal_coex_8821ae.c2h_bt_info_original;

	if(bt_info & BIT(2)){
		hal_coex_8821ae.b_c2h_bt_inquiry_page = true;
	} else {
		hal_coex_8821ae.b_c2h_bt_inquiry_page = false;
	}

	if (bt_info & BTINFO_B_CONNECTION) {
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("[BTC2H], BTInfo: bConnect=true\n"));
		rtlpcipriv->btcoexist.b_bt_busy = true;
		rtlpcipriv->btcoexist.current_state &= ~BT_COEX_STATE_BT_IDLE;
	} else {
		RT_TRACE(COMP_BT_COEXIST, DBG_DMESG, ("[BTC2H], BTInfo: bConnect=false\n"));
		rtlpcipriv->btcoexist.b_bt_busy = false;
		rtlpcipriv->btcoexist.current_state |= BT_COEX_STATE_BT_IDLE;
	}
}
void rtl_8821ae_c2h_command_handle(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct c2h_evt_hdr c2h_event;
	u8 * ptmp_buf = NULL;
	u8 index = 0;
	u8 u1b_tmp = 0;
	memset(&c2h_event, 0, sizeof(c2h_event));
	u1b_tmp = rtl_read_byte(rtlpriv, REG_C2HEVT_MSG_NORMAL);
	RT_TRACE(COMP_FW, DBG_DMESG,
		("&&&&&&: REG_C2HEVT_MSG_NORMAL is 0x%x\n", u1b_tmp));
	c2h_event.cmd_id = u1b_tmp & 0xF;
	c2h_event.cmd_len = (u1b_tmp & 0xF0) >> 4;
	c2h_event.cmd_seq = rtl_read_byte(rtlpriv, REG_C2HEVT_MSG_NORMAL + 1);
	RT_TRACE(COMP_FW, DBG_DMESG, ("cmd_id: %d, cmd_len: %d, cmd_seq: %d\n",
		c2h_event.cmd_id , c2h_event.cmd_len, c2h_event.cmd_seq));
	u1b_tmp = rtl_read_byte(rtlpriv, 0x01AF);
	if (u1b_tmp == C2H_EVT_HOST_CLOSE) {
		return;
	} else if (u1b_tmp != C2H_EVT_FW_CLOSE) {
		rtl_write_byte(rtlpriv, 0x1AF, 0x00);
		return;
	}
	ptmp_buf = kmalloc(c2h_event.cmd_len, GFP_KERNEL);
	if(ptmp_buf == NULL) {
		RT_TRACE(COMP_FW, DBG_TRACE, ("malloc cmd buf failed\n"));
		return;
	}

	/* Read the content */
	for (index = 0; index < c2h_event.cmd_len; index ++) {
		ptmp_buf[index] = rtl_read_byte(rtlpriv, REG_C2HEVT_MSG_NORMAL + 2+ index);
	}

	switch(c2h_event.cmd_id) {
		case C2H_BT_RSSI:
			break;

	case C2H_BT_OP_MODE:
			break;

	case BT_INFO:
		RT_TRACE(COMP_FW, DBG_TRACE,
			("BT info Byte[0] (ID) is 0x%x\n", c2h_event.cmd_id));
		RT_TRACE(COMP_FW, DBG_TRACE,
			("BT info Byte[1] (Seq) is 0x%x\n", c2h_event.cmd_seq));
		RT_TRACE(COMP_FW, DBG_TRACE,
			("BT info Byte[2] (Data)= 0x%x\n", ptmp_buf[0]));

		if (rtlpriv->cfg->ops->get_btc_status()){
			rtlpriv->btcoexist.btc_ops->btc_btinfo_notify(rtlpriv, ptmp_buf, c2h_event.cmd_len);
		}
		break;
	default:
		break;
	}

	if(ptmp_buf)
		kfree(ptmp_buf);

	rtl_write_byte(rtlpriv, 0x01AF, C2H_EVT_HOST_CLOSE);
}



