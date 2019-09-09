// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#include "hal_btc.h"
#include "../pci.h"
#include "phy.h"
#include "fw.h"
#include "reg.h"
#include "def.h"
#include "../rtl8723com/phy_common.h"

static struct bt_coexist_8723 hal_coex_8723;

void rtl8723e_dm_bt_turn_off_bt_coexist_before_enter_lps(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));

	if (!rtlpriv->btcoexist.bt_coexistence)
		return;

	if (ppsc->inactiveps) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			"[BT][DM], Before enter IPS, turn off all Coexist DM\n");
		rtlpriv->btcoexist.cstate = 0;
		rtlpriv->btcoexist.previous_state = 0;
		rtlpriv->btcoexist.cstate_h = 0;
		rtlpriv->btcoexist.previous_state_h = 0;
		rtl8723e_btdm_coex_all_off(hw);
	}
}

static enum rt_media_status mgnt_link_status_query(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	enum rt_media_status    m_status = RT_MEDIA_DISCONNECT;
	u8 bibss = (mac->opmode == NL80211_IFTYPE_ADHOC) ? 1 : 0;
	if (bibss || rtlpriv->mac80211.link_state >= MAC80211_LINKED)
		m_status = RT_MEDIA_CONNECT;

	return m_status;
}

void rtl_8723e_bt_wifi_media_status_notify(struct ieee80211_hw *hw,
						bool mstatus)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u8 h2c_parameter[3] = {0};
	u8 chnl;

	if (!rtlpriv->btcoexist.bt_coexistence)
		return;

	if (RT_MEDIA_CONNECT == mstatus)
		h2c_parameter[0] = 0x1; /* 0: disconnected, 1:connected */
	else
		h2c_parameter[0] = 0x0;

	if (mgnt_link_status_query(hw))	{
		chnl = rtlphy->current_channel;
		h2c_parameter[1] = chnl;
	}

	if (rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_20_40)
		h2c_parameter[2] = 0x30;
	else
		h2c_parameter[2] = 0x20;

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
		 "[BTCoex], FW write 0x19=0x%x\n",
		 h2c_parameter[0]<<16|h2c_parameter[1]<<8|h2c_parameter[2]);

	rtl8723e_fill_h2c_cmd(hw, 0x19, 3, h2c_parameter);
}

static bool rtl8723e_dm_bt_is_wifi_busy(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	if (rtlpriv->link_info.busytraffic ||
		rtlpriv->link_info.rx_busy_traffic ||
		rtlpriv->link_info.tx_busy_traffic)
		return true;
	else
		return false;
}

static void rtl8723e_dm_bt_set_fw_3a(struct ieee80211_hw *hw,
				     u8 byte1, u8 byte2, u8 byte3, u8 byte4,
				     u8 byte5)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 h2c_parameter[5];

	h2c_parameter[0] = byte1;
	h2c_parameter[1] = byte2;
	h2c_parameter[2] = byte3;
	h2c_parameter[3] = byte4;
	h2c_parameter[4] = byte5;
	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
		"[BTCoex], FW write 0x3a(4bytes)=0x%x%8x\n",
		h2c_parameter[0], h2c_parameter[1]<<24 |
		h2c_parameter[2]<<16 | h2c_parameter[3]<<8 |
		h2c_parameter[4]);
	rtl8723e_fill_h2c_cmd(hw, 0x3a, 5, h2c_parameter);
}

static bool rtl8723e_dm_bt_need_to_dec_bt_pwr(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (mgnt_link_status_query(hw) == RT_MEDIA_CONNECT) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			"Need to decrease bt power\n");
		rtlpriv->btcoexist.cstate |=
		BT_COEX_STATE_DEC_BT_POWER;
		return true;
	}

	rtlpriv->btcoexist.cstate &= ~BT_COEX_STATE_DEC_BT_POWER;
	return false;
}

static bool rtl8723e_dm_bt_is_same_coexist_state(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if ((rtlpriv->btcoexist.previous_state ==
	     rtlpriv->btcoexist.cstate) &&
	    (rtlpriv->btcoexist.previous_state_h ==
	     rtlpriv->btcoexist.cstate_h)) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			 "[DM][BT], Coexist state do not chang!!\n");
		return true;
	} else {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			 "[DM][BT], Coexist state changed!!\n");
		return false;
	}
}

static void rtl8723e_dm_bt_set_coex_table(struct ieee80211_hw *hw,
					  u32 val_0x6c0, u32 val_0x6c8,
					  u32 val_0x6cc)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
		 "set coex table, set 0x6c0=0x%x\n", val_0x6c0);
	rtl_write_dword(rtlpriv, 0x6c0, val_0x6c0);

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
		 "set coex table, set 0x6c8=0x%x\n", val_0x6c8);
	rtl_write_dword(rtlpriv, 0x6c8, val_0x6c8);

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
		 "set coex table, set 0x6cc=0x%x\n", val_0x6cc);
	rtl_write_byte(rtlpriv, 0x6cc, val_0x6cc);
}

static void rtl8723e_dm_bt_set_hw_pta_mode(struct ieee80211_hw *hw, bool b_mode)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (BT_PTA_MODE_ON == b_mode) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE, "PTA mode on\n");
		/*  Enable GPIO 0/1/2/3/8 pins for bt */
		rtl_write_byte(rtlpriv, 0x40, 0x20);
		rtlpriv->btcoexist.hw_coexist_all_off = false;
	} else {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE, "PTA mode off\n");
		rtl_write_byte(rtlpriv, 0x40, 0x0);
	}
}

static void rtl8723e_dm_bt_set_sw_rf_rx_lpf_corner(struct ieee80211_hw *hw,
						   u8 type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (BT_RF_RX_LPF_CORNER_SHRINK == type) {
		/* Shrink RF Rx LPF corner, 0x1e[7:4]=1111 ==> [11:4] */
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
			 "Shrink RF Rx LPF corner!!\n");
		rtl8723e_phy_set_rf_reg(hw, RF90_PATH_A, 0x1e,
					0xfffff, 0xf0ff7);
		rtlpriv->btcoexist.sw_coexist_all_off = false;
	} else if (BT_RF_RX_LPF_CORNER_RESUME == type) {
		/*Resume RF Rx LPF corner*/
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
			 "Resume RF Rx LPF corner!!\n");
		rtl8723e_phy_set_rf_reg(hw, RF90_PATH_A, 0x1e, 0xfffff,
					rtlpriv->btcoexist.bt_rfreg_origin_1e);
	}
}

static void dm_bt_set_sw_penalty_tx_rate_adapt(struct ieee80211_hw *hw,
					       u8 ra_type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 tmp_u1;

	tmp_u1 = rtl_read_byte(rtlpriv, 0x4fd);
	tmp_u1 |= BIT(0);
	if (BT_TX_RATE_ADAPTIVE_LOW_PENALTY == ra_type) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
			"Tx rate adaptive, set low penalty!!\n");
		tmp_u1 &= ~BIT(2);
		rtlpriv->btcoexist.sw_coexist_all_off = false;
	} else if (BT_TX_RATE_ADAPTIVE_NORMAL == ra_type) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
			"Tx rate adaptive, set normal!!\n");
		tmp_u1 |= BIT(2);
	}

	rtl_write_byte(rtlpriv, 0x4fd, tmp_u1);
}

static void rtl8723e_dm_bt_btdm_structure_reload(struct ieee80211_hw *hw,
						 struct btdm_8723 *btdm)
{
	btdm->all_off = false;
	btdm->agc_table_en = false;
	btdm->adc_back_off_on = false;
	btdm->b2_ant_hid_en = false;
	btdm->low_penalty_rate_adaptive = false;
	btdm->rf_rx_lpf_shrink = false;
	btdm->reject_aggre_pkt = false;

	btdm->tdma_on = false;
	btdm->tdma_ant = TDMA_2ANT;
	btdm->tdma_nav = TDMA_NAV_OFF;
	btdm->tdma_dac_swing = TDMA_DAC_SWING_OFF;
	btdm->fw_dac_swing_lvl = 0x20;

	btdm->tra_tdma_on = false;
	btdm->tra_tdma_ant = TDMA_2ANT;
	btdm->tra_tdma_nav = TDMA_NAV_OFF;
	btdm->ignore_wlan_act = false;

	btdm->ps_tdma_on = false;
	btdm->ps_tdma_byte[0] = 0x0;
	btdm->ps_tdma_byte[1] = 0x0;
	btdm->ps_tdma_byte[2] = 0x0;
	btdm->ps_tdma_byte[3] = 0x8;
	btdm->ps_tdma_byte[4] = 0x0;

	btdm->pta_on = true;
	btdm->val_0x6c0 = 0x5a5aaaaa;
	btdm->val_0x6c8 = 0xcc;
	btdm->val_0x6cc = 0x3;

	btdm->sw_dac_swing_on = false;
	btdm->sw_dac_swing_lvl = 0xc0;
	btdm->wlan_act_hi = 0x20;
	btdm->wlan_act_lo = 0x10;
	btdm->bt_retry_index = 2;

	btdm->dec_bt_pwr = false;
}

static void rtl8723e_dm_bt_btdm_structure_reload_all_off(struct ieee80211_hw *hw,
							 struct btdm_8723 *btdm)
{
	rtl8723e_dm_bt_btdm_structure_reload(hw, btdm);
	btdm->all_off = true;
	btdm->pta_on = false;
	btdm->wlan_act_hi = 0x10;
}

static bool rtl8723e_dm_bt_is_2_ant_common_action(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct btdm_8723 btdm8723;
	bool b_common = false;

	rtl8723e_dm_bt_btdm_structure_reload(hw, &btdm8723);

	if (!rtl8723e_dm_bt_is_wifi_busy(hw) &&
	    !rtlpriv->btcoexist.bt_busy) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			 "Wifi idle + Bt idle, bt coex mechanism always off!!\n");
		rtl8723e_dm_bt_btdm_structure_reload_all_off(hw, &btdm8723);
		b_common = true;
	} else if (rtl8723e_dm_bt_is_wifi_busy(hw) &&
		   !rtlpriv->btcoexist.bt_busy) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			 "Wifi non-idle + Bt disabled/idle!!\n");
		btdm8723.low_penalty_rate_adaptive = true;
		btdm8723.rf_rx_lpf_shrink = false;
		btdm8723.reject_aggre_pkt = false;

		/* sw mechanism */
		btdm8723.agc_table_en = false;
		btdm8723.adc_back_off_on = false;
		btdm8723.sw_dac_swing_on = false;

		btdm8723.pta_on = true;
		btdm8723.val_0x6c0 = 0x5a5aaaaa;
		btdm8723.val_0x6c8 = 0xcccc;
		btdm8723.val_0x6cc = 0x3;

		btdm8723.tdma_on = false;
		btdm8723.tdma_dac_swing = TDMA_DAC_SWING_OFF;
		btdm8723.b2_ant_hid_en = false;

		b_common = true;
	} else if (rtlpriv->btcoexist.bt_busy) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			"Bt non-idle!\n");
		if (mgnt_link_status_query(hw) == RT_MEDIA_CONNECT) {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
				"Wifi connection exist\n");
			b_common = false;
		} else {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
				"No Wifi connection!\n");
			btdm8723.rf_rx_lpf_shrink = true;
			btdm8723.low_penalty_rate_adaptive = false;
			btdm8723.reject_aggre_pkt = false;

			/* sw mechanism */
			btdm8723.agc_table_en = false;
			btdm8723.adc_back_off_on = false;
			btdm8723.sw_dac_swing_on = false;

			btdm8723.pta_on = true;
			btdm8723.val_0x6c0 = 0x55555555;
			btdm8723.val_0x6c8 = 0x0000ffff;
			btdm8723.val_0x6cc = 0x3;

			btdm8723.tdma_on = false;
			btdm8723.tdma_dac_swing = TDMA_DAC_SWING_OFF;
			btdm8723.b2_ant_hid_en = false;

			b_common = true;
		}
	}

	if (rtl8723e_dm_bt_need_to_dec_bt_pwr(hw))
		btdm8723.dec_bt_pwr = true;

	if (b_common)
		rtlpriv->btcoexist.cstate |=
			BT_COEX_STATE_BTINFO_COMMON;

	if (b_common && rtl8723e_dm_bt_is_coexist_state_changed(hw))
		rtl8723e_dm_bt_set_bt_dm(hw, &btdm8723);

	return b_common;
}

static void rtl8723e_dm_bt_set_sw_full_time_dac_swing(
		struct ieee80211_hw *hw,
		bool sw_dac_swing_on,
		u32 sw_dac_swing_lvl)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (sw_dac_swing_on) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
			 "[BTCoex], SwDacSwing = 0x%x\n", sw_dac_swing_lvl);
		rtl8723_phy_set_bb_reg(hw, 0x880, 0xff000000,
				       sw_dac_swing_lvl);
		rtlpriv->btcoexist.sw_coexist_all_off = false;
	} else {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
			 "[BTCoex], SwDacSwing Off!\n");
		rtl8723_phy_set_bb_reg(hw, 0x880, 0xff000000, 0xc0);
	}
}

static void rtl8723e_dm_bt_set_fw_dec_bt_pwr(
		struct ieee80211_hw *hw, bool dec_bt_pwr)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 h2c_parameter[1] = {0};

	h2c_parameter[0] = 0;

	if (dec_bt_pwr) {
		h2c_parameter[0] |= BIT(1);
		rtlpriv->btcoexist.fw_coexist_all_off = false;
	}

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
		 "[BTCoex], decrease Bt Power : %s, write 0x21=0x%x\n",
		 (dec_bt_pwr ? "Yes!!" : "No!!"), h2c_parameter[0]);

	rtl8723e_fill_h2c_cmd(hw, 0x21, 1, h2c_parameter);
}

static void rtl8723e_dm_bt_set_fw_2_ant_hid(struct ieee80211_hw *hw,
					    bool b_enable, bool b_dac_swing_on)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 h2c_parameter[1] = {0};

	if (b_enable) {
		h2c_parameter[0] |= BIT(0);
		rtlpriv->btcoexist.fw_coexist_all_off = false;
	}
	if (b_dac_swing_on)
		h2c_parameter[0] |= BIT(1); /* Dac Swing default enable */

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
		 "[BTCoex], turn 2-Ant+HID mode %s, DACSwing:%s, write 0x15=0x%x\n",
		 (b_enable ? "ON!!" : "OFF!!"), (b_dac_swing_on ? "ON" : "OFF"),
		 h2c_parameter[0]);

	rtl8723e_fill_h2c_cmd(hw, 0x15, 1, h2c_parameter);
}

static void rtl8723e_dm_bt_set_fw_tdma_ctrl(struct ieee80211_hw *hw,
					    bool b_enable, u8 ant_num,
					    u8 nav_en, u8 dac_swing_en)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 h2c_parameter[1] = {0};
	u8 h2c_parameter1[1] = {0};

	h2c_parameter[0] = 0;
	h2c_parameter1[0] = 0;

	if (b_enable) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
			 "[BTCoex], set BT PTA update manager to trigger update!!\n");
		h2c_parameter1[0] |= BIT(0);

		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
			"[BTCoex], turn TDMA mode ON!!\n");
		h2c_parameter[0] |= BIT(0);		/* function enable */
		if (TDMA_1ANT == ant_num) {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
			"[BTCoex], TDMA_1ANT\n");
			h2c_parameter[0] |= BIT(1);
		} else if (TDMA_2ANT == ant_num) {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
			"[BTCoex], TDMA_2ANT\n");
		} else {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
			"[BTCoex], Unknown Ant\n");
		}

		if (TDMA_NAV_OFF == nav_en) {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
			"[BTCoex], TDMA_NAV_OFF\n");
		} else if (TDMA_NAV_ON == nav_en) {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
			"[BTCoex], TDMA_NAV_ON\n");
			h2c_parameter[0] |= BIT(2);
		}

		if (TDMA_DAC_SWING_OFF == dac_swing_en) {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
				"[BTCoex], TDMA_DAC_SWING_OFF\n");
		} else if (TDMA_DAC_SWING_ON == dac_swing_en) {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
				"[BTCoex], TDMA_DAC_SWING_ON\n");
			h2c_parameter[0] |= BIT(4);
		}
		rtlpriv->btcoexist.fw_coexist_all_off = false;
	} else {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
			 "[BTCoex], set BT PTA update manager to no update!!\n");
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
			 "[BTCoex], turn TDMA mode OFF!!\n");
	}

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
		 "[BTCoex], FW2AntTDMA, write 0x26=0x%x\n",
		 h2c_parameter1[0]);
	rtl8723e_fill_h2c_cmd(hw, 0x26, 1, h2c_parameter1);

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
		"[BTCoex], FW2AntTDMA, write 0x14=0x%x\n",
		h2c_parameter[0]);
	rtl8723e_fill_h2c_cmd(hw, 0x14, 1, h2c_parameter);
}

static void rtl8723e_dm_bt_set_fw_ignore_wlan_act(struct ieee80211_hw *hw,
						  bool b_enable)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 h2c_parameter[1] = {0};

	if (b_enable) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
			"[BTCoex], BT Ignore Wlan_Act !!\n");
		h2c_parameter[0] |= BIT(0);		/* function enable */
		rtlpriv->btcoexist.fw_coexist_all_off = false;
	} else {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
			"[BTCoex], BT don't ignore Wlan_Act !!\n");
	}

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
		 "[BTCoex], set FW for BT Ignore Wlan_Act, write 0x25=0x%x\n",
		 h2c_parameter[0]);

	rtl8723e_fill_h2c_cmd(hw, 0x25, 1, h2c_parameter);
}

static void rtl8723e_dm_bt_set_fw_tra_tdma_ctrl(struct ieee80211_hw *hw,
						bool b_enable, u8 ant_num,
						u8 nav_en)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	u8 h2c_parameter[2] = {0};

	/* Only 8723 B cut should do this */
	if (IS_VENDOR_8723_A_CUT(rtlhal->version)) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
			 "[BTCoex], not 8723B cut, don't set Traditional TDMA!!\n");
		return;
	}

	if (b_enable) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
			 "[BTCoex], turn TTDMA mode ON!!\n");
		h2c_parameter[0] |= BIT(0);	/* function enable */
		if (TDMA_1ANT == ant_num) {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
				 "[BTCoex], TTDMA_1ANT\n");
			h2c_parameter[0] |= BIT(1);
		} else if (TDMA_2ANT == ant_num) {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
			"[BTCoex], TTDMA_2ANT\n");
		} else {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
			"[BTCoex], Unknown Ant\n");
		}

		if (TDMA_NAV_OFF == nav_en) {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
			"[BTCoex], TTDMA_NAV_OFF\n");
		} else if (TDMA_NAV_ON == nav_en) {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
			"[BTCoex], TTDMA_NAV_ON\n");
			h2c_parameter[1] |= BIT(0);
		}

		rtlpriv->btcoexist.fw_coexist_all_off = false;
	} else {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
			"[BTCoex], turn TTDMA mode OFF!!\n");
	}

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
		"[BTCoex], FW Traditional TDMA, write 0x33=0x%x\n",
		h2c_parameter[0] << 8 | h2c_parameter[1]);

	rtl8723e_fill_h2c_cmd(hw, 0x33, 2, h2c_parameter);
}

static void rtl8723e_dm_bt_set_fw_dac_swing_level(struct ieee80211_hw *hw,
						  u8 dac_swing_lvl)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 h2c_parameter[1] = {0};
	h2c_parameter[0] = dac_swing_lvl;

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
		"[BTCoex], Set Dac Swing Level=0x%x\n", dac_swing_lvl);
	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
		"[BTCoex], write 0x29=0x%x\n", h2c_parameter[0]);

	rtl8723e_fill_h2c_cmd(hw, 0x29, 1, h2c_parameter);
}

static void rtl8723e_dm_bt_set_fw_bt_hid_info(struct ieee80211_hw *hw,
					      bool b_enable)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 h2c_parameter[1] = {0};
	h2c_parameter[0] = 0;

	if (b_enable) {
		h2c_parameter[0] |= BIT(0);
		rtlpriv->btcoexist.fw_coexist_all_off = false;
	}
	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
		"[BTCoex], Set BT HID information=0x%x\n", b_enable);
	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
		"[BTCoex], write 0x24=0x%x\n", h2c_parameter[0]);

	rtl8723e_fill_h2c_cmd(hw, 0x24, 1, h2c_parameter);
}

static void rtl8723e_dm_bt_set_fw_bt_retry_index(struct ieee80211_hw *hw,
						 u8 retry_index)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 h2c_parameter[1] = {0};
	h2c_parameter[0] = retry_index;

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
		"[BTCoex], Set BT Retry Index=%d\n", retry_index);
	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
		"[BTCoex], write 0x23=0x%x\n", h2c_parameter[0]);

	rtl8723e_fill_h2c_cmd(hw, 0x23, 1, h2c_parameter);
}

static void rtl8723e_dm_bt_set_fw_wlan_act(struct ieee80211_hw *hw,
					   u8 wlan_act_hi, u8 wlan_act_lo)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 h2c_parameter_hi[1] = {0};
	u8 h2c_parameter_lo[1] = {0};
	h2c_parameter_hi[0] = wlan_act_hi;
	h2c_parameter_lo[0] = wlan_act_lo;

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
		"[BTCoex], Set WLAN_ACT Hi:Lo=0x%x/0x%x\n",
		wlan_act_hi, wlan_act_lo);
	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
		"[BTCoex], write 0x22=0x%x\n", h2c_parameter_hi[0]);
	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
		"[BTCoex], write 0x11=0x%x\n", h2c_parameter_lo[0]);

	/* WLAN_ACT = High duration, unit:ms */
	rtl8723e_fill_h2c_cmd(hw, 0x22, 1, h2c_parameter_hi);
	/*  WLAN_ACT = Low duration, unit:3*625us */
	rtl8723e_fill_h2c_cmd(hw, 0x11, 1, h2c_parameter_lo);
}

void rtl8723e_dm_bt_set_bt_dm(struct ieee80211_hw *hw,
			      struct btdm_8723 *btdm)
{
	struct rtl_priv	*rtlpriv = rtl_priv(hw);
	struct btdm_8723 *btdm_8723 = &hal_coex_8723.btdm;
	u8 i;

	bool fw_current_inpsmode = false;
	bool fw_ps_awake = true;

	rtlpriv->cfg->ops->get_hw_reg(hw, HW_VAR_FW_PSMODE_STATUS,
					      (u8 *)(&fw_current_inpsmode));
	rtlpriv->cfg->ops->get_hw_reg(hw, HW_VAR_FWLPS_RF_ON,
					      (u8 *)(&fw_ps_awake));

	/* check new setting is different with the old one, */
	/* if all the same, don't do the setting again. */
	if (memcmp(btdm_8723, btdm, sizeof(struct btdm_8723)) == 0) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			"[BTCoex], the same coexist setting, return!!\n");
		return;
	} else {	/* save the new coexist setting */
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			"[BTCoex], UPDATE TO NEW COEX SETTING!!\n");
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			"[BTCoex], original/new bAllOff=0x%x/ 0x%x\n",
			btdm_8723->all_off, btdm->all_off);
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			"[BTCoex], original/new agc_table_en=0x%x/ 0x%x\n",
			btdm_8723->agc_table_en, btdm->agc_table_en);
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			 "[BTCoex], original/new adc_back_off_on=0x%x/ 0x%x\n",
			 btdm_8723->adc_back_off_on,
			 btdm->adc_back_off_on);
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			 "[BTCoex], original/new b2_ant_hid_en=0x%x/ 0x%x\n",
			 btdm_8723->b2_ant_hid_en, btdm->b2_ant_hid_en);
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			 "[BTCoex], original/new bLowPenaltyRateAdaptive=0x%x/ 0x%x\n",
			 btdm_8723->low_penalty_rate_adaptive,
			 btdm->low_penalty_rate_adaptive);
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			 "[BTCoex], original/new bRfRxLpfShrink=0x%x/ 0x%x\n",
			 btdm_8723->rf_rx_lpf_shrink,
			 btdm->rf_rx_lpf_shrink);
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			 "[BTCoex], original/new bRejectAggrePkt=0x%x/ 0x%x\n",
			 btdm_8723->reject_aggre_pkt,
			 btdm->reject_aggre_pkt);
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			 "[BTCoex], original/new tdma_on=0x%x/ 0x%x\n",
			 btdm_8723->tdma_on, btdm->tdma_on);
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			 "[BTCoex], original/new tdmaAnt=0x%x/ 0x%x\n",
			 btdm_8723->tdma_ant, btdm->tdma_ant);
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			 "[BTCoex], original/new tdmaNav=0x%x/ 0x%x\n",
			 btdm_8723->tdma_nav, btdm->tdma_nav);
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			 "[BTCoex], original/new tdma_dac_swing=0x%x/ 0x%x\n",
			 btdm_8723->tdma_dac_swing, btdm->tdma_dac_swing);
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			 "[BTCoex], original/new fw_dac_swing_lvl=0x%x/ 0x%x\n",
			 btdm_8723->fw_dac_swing_lvl,
			 btdm->fw_dac_swing_lvl);

		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			 "[BTCoex], original/new bTraTdmaOn=0x%x/ 0x%x\n",
			 btdm_8723->tra_tdma_on, btdm->tra_tdma_on);
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			 "[BTCoex], original/new traTdmaAnt=0x%x/ 0x%x\n",
			 btdm_8723->tra_tdma_ant, btdm->tra_tdma_ant);
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			 "[BTCoex], original/new traTdmaNav=0x%x/ 0x%x\n",
			 btdm_8723->tra_tdma_nav, btdm->tra_tdma_nav);
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			 "[BTCoex], original/new bPsTdmaOn=0x%x/ 0x%x\n",
			 btdm_8723->ps_tdma_on, btdm->ps_tdma_on);
		for (i = 0; i < 5; i++) {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
				 "[BTCoex], original/new psTdmaByte[i]=0x%x/ 0x%x\n",
				 btdm_8723->ps_tdma_byte[i],
				 btdm->ps_tdma_byte[i]);
		}
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			"[BTCoex], original/new bIgnoreWlanAct=0x%x/ 0x%x\n",
			btdm_8723->ignore_wlan_act,
			btdm->ignore_wlan_act);


		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			"[BTCoex], original/new bPtaOn=0x%x/ 0x%x\n",
			btdm_8723->pta_on, btdm->pta_on);
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			"[BTCoex], original/new val_0x6c0=0x%x/ 0x%x\n",
			btdm_8723->val_0x6c0, btdm->val_0x6c0);
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			"[BTCoex], original/new val_0x6c8=0x%x/ 0x%x\n",
			btdm_8723->val_0x6c8, btdm->val_0x6c8);
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			"[BTCoex], original/new val_0x6cc=0x%x/ 0x%x\n",
			btdm_8723->val_0x6cc, btdm->val_0x6cc);
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			 "[BTCoex], original/new sw_dac_swing_on=0x%x/ 0x%x\n",
			 btdm_8723->sw_dac_swing_on,
			 btdm->sw_dac_swing_on);
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			 "[BTCoex], original/new sw_dac_swing_lvl=0x%x/ 0x%x\n",
			 btdm_8723->sw_dac_swing_lvl,
			 btdm->sw_dac_swing_lvl);
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			 "[BTCoex], original/new wlanActHi=0x%x/ 0x%x\n",
			 btdm_8723->wlan_act_hi, btdm->wlan_act_hi);
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			 "[BTCoex], original/new wlanActLo=0x%x/ 0x%x\n",
			 btdm_8723->wlan_act_lo, btdm->wlan_act_lo);
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			 "[BTCoex], original/new btRetryIndex=0x%x/ 0x%x\n",
			 btdm_8723->bt_retry_index, btdm->bt_retry_index);

		memcpy(btdm_8723, btdm, sizeof(struct btdm_8723));
	}
	/* Here we only consider when Bt Operation
	 * inquiry/paging/pairing is ON
	 * we only need to turn off TDMA
	 */

	if (rtlpriv->btcoexist.hold_for_bt_operation) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
			"[BTCoex], set to ignore wlanAct for BT OP!!\n");
		rtl8723e_dm_bt_set_fw_ignore_wlan_act(hw, true);
		return;
	}

	if (btdm->all_off) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
			"[BTCoex], disable all coexist mechanism !!\n");
		rtl8723e_btdm_coex_all_off(hw);
		return;
	}

	rtl8723e_dm_bt_reject_ap_aggregated_packet(hw, btdm->reject_aggre_pkt);

	if (btdm->low_penalty_rate_adaptive)
		dm_bt_set_sw_penalty_tx_rate_adapt(hw, BT_TX_RATE_ADAPTIVE_LOW_PENALTY);
	else
		dm_bt_set_sw_penalty_tx_rate_adapt(hw,
						   BT_TX_RATE_ADAPTIVE_NORMAL);

	if (btdm->rf_rx_lpf_shrink)
		rtl8723e_dm_bt_set_sw_rf_rx_lpf_corner(hw,
				BT_RF_RX_LPF_CORNER_SHRINK);
	else
		rtl8723e_dm_bt_set_sw_rf_rx_lpf_corner(hw,
				BT_RF_RX_LPF_CORNER_RESUME);

	if (btdm->agc_table_en)
		rtl8723e_dm_bt_agc_table(hw, BT_AGCTABLE_ON);
	else
		rtl8723e_dm_bt_agc_table(hw, BT_AGCTABLE_OFF);

	if (btdm->adc_back_off_on)
		rtl8723e_dm_bt_bb_back_off_level(hw, BT_BB_BACKOFF_ON);
	else
		rtl8723e_dm_bt_bb_back_off_level(hw, BT_BB_BACKOFF_OFF);

	rtl8723e_dm_bt_set_fw_bt_retry_index(hw, btdm->bt_retry_index);

	rtl8723e_dm_bt_set_fw_dac_swing_level(hw, btdm->fw_dac_swing_lvl);
	rtl8723e_dm_bt_set_fw_wlan_act(hw, btdm->wlan_act_hi,
				       btdm->wlan_act_lo);

	rtl8723e_dm_bt_set_coex_table(hw, btdm->val_0x6c0,
				      btdm->val_0x6c8, btdm->val_0x6cc);
	rtl8723e_dm_bt_set_hw_pta_mode(hw, btdm->pta_on);

	/* Note: There is a constraint between TDMA and 2AntHID
	 * Only one of 2AntHid and tdma can be turn on
	 * We should turn off those mechanisms should be turned off first
	 * and then turn on those mechanisms should be turned on.
	*/
	if (btdm->b2_ant_hid_en) {
		/* turn off tdma */
		rtl8723e_dm_bt_set_fw_tra_tdma_ctrl(hw, btdm->tra_tdma_on,
						    btdm->tra_tdma_ant,
						    btdm->tra_tdma_nav);
		rtl8723e_dm_bt_set_fw_tdma_ctrl(hw, false, btdm->tdma_ant,
						btdm->tdma_nav,
						btdm->tdma_dac_swing);

		/* turn off Pstdma */
		rtl8723e_dm_bt_set_fw_ignore_wlan_act(hw,
						      btdm->ignore_wlan_act);
		/* Antenna control by PTA, 0x870 = 0x300. */
		rtl8723e_dm_bt_set_fw_3a(hw, 0x0, 0x0, 0x0, 0x8, 0x0);

		/* turn on 2AntHid */
		rtl8723e_dm_bt_set_fw_bt_hid_info(hw, true);
		rtl8723e_dm_bt_set_fw_2_ant_hid(hw, true, true);
	} else if (btdm->tdma_on) {
		/* turn off 2AntHid */
		rtl8723e_dm_bt_set_fw_bt_hid_info(hw, false);
		rtl8723e_dm_bt_set_fw_2_ant_hid(hw, false, false);

		/* turn off pstdma */
		rtl8723e_dm_bt_set_fw_ignore_wlan_act(hw,
						      btdm->ignore_wlan_act);
		/* Antenna control by PTA, 0x870 = 0x300. */
		rtl8723e_dm_bt_set_fw_3a(hw, 0x0, 0x0, 0x0, 0x8, 0x0);

		/* turn on tdma */
		rtl8723e_dm_bt_set_fw_tra_tdma_ctrl(hw, btdm->tra_tdma_on,
						    btdm->tra_tdma_ant,
						    btdm->tra_tdma_nav);
		rtl8723e_dm_bt_set_fw_tdma_ctrl(hw, true, btdm->tdma_ant,
						btdm->tdma_nav,
						btdm->tdma_dac_swing);
	} else if (btdm->ps_tdma_on) {
		/* turn off 2AntHid */
		rtl8723e_dm_bt_set_fw_bt_hid_info(hw, false);
		rtl8723e_dm_bt_set_fw_2_ant_hid(hw, false, false);

		/* turn off tdma */
		rtl8723e_dm_bt_set_fw_tra_tdma_ctrl(hw, btdm->tra_tdma_on,
						    btdm->tra_tdma_ant,
						    btdm->tra_tdma_nav);
		rtl8723e_dm_bt_set_fw_tdma_ctrl(hw, false, btdm->tdma_ant,
						btdm->tdma_nav,
						btdm->tdma_dac_swing);

		/* turn on pstdma */
		rtl8723e_dm_bt_set_fw_ignore_wlan_act(hw,
						      btdm->ignore_wlan_act);
		rtl8723e_dm_bt_set_fw_3a(hw, btdm->ps_tdma_byte[0],
					 btdm->ps_tdma_byte[1],
					 btdm->ps_tdma_byte[2],
					 btdm->ps_tdma_byte[3],
					 btdm->ps_tdma_byte[4]);
	} else {
		/* turn off 2AntHid */
		rtl8723e_dm_bt_set_fw_bt_hid_info(hw, false);
		rtl8723e_dm_bt_set_fw_2_ant_hid(hw, false, false);

		/* turn off tdma */
		rtl8723e_dm_bt_set_fw_tra_tdma_ctrl(hw, btdm->tra_tdma_on,
						    btdm->tra_tdma_ant,
						    btdm->tra_tdma_nav);
		rtl8723e_dm_bt_set_fw_tdma_ctrl(hw, false, btdm->tdma_ant,
						btdm->tdma_nav,
						btdm->tdma_dac_swing);

		/* turn off pstdma */
		rtl8723e_dm_bt_set_fw_ignore_wlan_act(hw,
						btdm->ignore_wlan_act);
		/* Antenna control by PTA, 0x870 = 0x300. */
		rtl8723e_dm_bt_set_fw_3a(hw, 0x0, 0x0, 0x0, 0x8, 0x0);
	}

	/* Note:
	 * We should add delay for making sure
	 *	sw DacSwing can be set sucessfully.
	 * because of that rtl8723e_dm_bt_set_fw_2_ant_hid()
	 *	and rtl8723e_dm_bt_set_fw_tdma_ctrl()
	 * will overwrite the reg 0x880.
	*/
	mdelay(30);
	rtl8723e_dm_bt_set_sw_full_time_dac_swing(hw, btdm->sw_dac_swing_on,
						  btdm->sw_dac_swing_lvl);
	rtl8723e_dm_bt_set_fw_dec_bt_pwr(hw, btdm->dec_bt_pwr);
}

/* ============================================================ */
/* extern function start with BTDM_ */
/* ============================================================i
 */
static u32 rtl8723e_dm_bt_tx_rx_couter_h(struct ieee80211_hw *hw)
{
	u32	counters = 0;

	counters = hal_coex_8723.high_priority_tx +
			hal_coex_8723.high_priority_rx;
	return counters;
}

static u32 rtl8723e_dm_bt_tx_rx_couter_l(struct ieee80211_hw *hw)
{
	u32 counters = 0;

	counters = hal_coex_8723.low_priority_tx +
			hal_coex_8723.low_priority_rx;
	return counters;
}

static u8 rtl8723e_dm_bt_bt_tx_rx_counter_level(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32	bt_tx_rx_cnt = 0;
	u8	bt_tx_rx_cnt_lvl = 0;

	bt_tx_rx_cnt = rtl8723e_dm_bt_tx_rx_couter_h(hw)
				+ rtl8723e_dm_bt_tx_rx_couter_l(hw);
	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
		 "[BTCoex], BT TxRx Counters = %d\n", bt_tx_rx_cnt);

	rtlpriv->btcoexist.cstate_h &= ~
		 (BT_COEX_STATE_BT_CNT_LEVEL_0 | BT_COEX_STATE_BT_CNT_LEVEL_1|
		  BT_COEX_STATE_BT_CNT_LEVEL_2);

	if (bt_tx_rx_cnt >= BT_TXRX_CNT_THRES_3) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			 "[BTCoex], BT TxRx Counters at level 3\n");
		bt_tx_rx_cnt_lvl = BT_TXRX_CNT_LEVEL_3;
		rtlpriv->btcoexist.cstate_h |=
			BT_COEX_STATE_BT_CNT_LEVEL_3;
	} else if (bt_tx_rx_cnt >= BT_TXRX_CNT_THRES_2) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			 "[BTCoex], BT TxRx Counters at level 2\n");
		bt_tx_rx_cnt_lvl = BT_TXRX_CNT_LEVEL_2;
		rtlpriv->btcoexist.cstate_h |=
			BT_COEX_STATE_BT_CNT_LEVEL_2;
	} else if (bt_tx_rx_cnt >= BT_TXRX_CNT_THRES_1) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			 "[BTCoex], BT TxRx Counters at level 1\n");
		bt_tx_rx_cnt_lvl = BT_TXRX_CNT_LEVEL_1;
		rtlpriv->btcoexist.cstate_h  |=
			BT_COEX_STATE_BT_CNT_LEVEL_1;
	} else {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			 "[BTCoex], BT TxRx Counters at level 0\n");
		bt_tx_rx_cnt_lvl = BT_TXRX_CNT_LEVEL_0;
		rtlpriv->btcoexist.cstate_h |=
			BT_COEX_STATE_BT_CNT_LEVEL_0;
	}
	return bt_tx_rx_cnt_lvl;
}

static void rtl8723e_dm_bt_2_ant_hid_sco_esco(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct btdm_8723 btdm8723;
	u8 bt_rssi_state, bt_rssi_state1;
	u8	bt_tx_rx_cnt_lvl = 0;

	rtl8723e_dm_bt_btdm_structure_reload(hw, &btdm8723);

	btdm8723.rf_rx_lpf_shrink = true;
	btdm8723.low_penalty_rate_adaptive = true;
	btdm8723.reject_aggre_pkt = false;

	bt_tx_rx_cnt_lvl = rtl8723e_dm_bt_bt_tx_rx_counter_level(hw);
	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
		 "[BTCoex], BT TxRx Counters = %d\n", bt_tx_rx_cnt_lvl);

	if (rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_20_40) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG, "HT40\n");
		/* coex table */
		btdm8723.val_0x6c0 = 0x55555555;
		btdm8723.val_0x6c8 = 0xffff;
		btdm8723.val_0x6cc = 0x3;

		/* sw mechanism */
		btdm8723.agc_table_en = false;
		btdm8723.adc_back_off_on = false;
		btdm8723.sw_dac_swing_on = false;

		/* fw mechanism */
		btdm8723.ps_tdma_on = true;
		if (bt_tx_rx_cnt_lvl == BT_TXRX_CNT_LEVEL_2) {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
				 "[BTCoex], BT TxRx Counters >= 1400\n");
			btdm8723.ps_tdma_byte[0] = 0xa3;
			btdm8723.ps_tdma_byte[1] = 0x5;
			btdm8723.ps_tdma_byte[2] = 0x5;
			btdm8723.ps_tdma_byte[3] = 0x2;
			btdm8723.ps_tdma_byte[4] = 0x80;
		} else if (bt_tx_rx_cnt_lvl == BT_TXRX_CNT_LEVEL_1) {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
				 "[BTCoex], BT TxRx Counters >= 1200 && < 1400\n");
			btdm8723.ps_tdma_byte[0] = 0xa3;
			btdm8723.ps_tdma_byte[1] = 0xa;
			btdm8723.ps_tdma_byte[2] = 0xa;
			btdm8723.ps_tdma_byte[3] = 0x2;
			btdm8723.ps_tdma_byte[4] = 0x80;
		} else {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
				 "[BTCoex], BT TxRx Counters < 1200\n");
			btdm8723.ps_tdma_byte[0] = 0xa3;
			btdm8723.ps_tdma_byte[1] = 0xf;
			btdm8723.ps_tdma_byte[2] = 0xf;
			btdm8723.ps_tdma_byte[3] = 0x2;
			btdm8723.ps_tdma_byte[4] = 0x80;
		}
	} else {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			 "HT20 or Legacy\n");
		bt_rssi_state =
		  rtl8723e_dm_bt_check_coex_rssi_state(hw, 2, 47, 0);
		bt_rssi_state1 =
		  rtl8723e_dm_bt_check_coex_rssi_state1(hw, 2, 27, 0);

		/* coex table */
		btdm8723.val_0x6c0 = 0x55555555;
		btdm8723.val_0x6c8 = 0xffff;
		btdm8723.val_0x6cc = 0x3;

		/* sw mechanism */
		if ((bt_rssi_state == BT_RSSI_STATE_HIGH) ||
			(bt_rssi_state == BT_RSSI_STATE_STAY_HIGH)) {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
					"Wifi rssi high\n");
			btdm8723.agc_table_en = true;
			btdm8723.adc_back_off_on = true;
			btdm8723.sw_dac_swing_on = false;
		} else {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
					"Wifi rssi low\n");
			btdm8723.agc_table_en = false;
			btdm8723.adc_back_off_on = false;
			btdm8723.sw_dac_swing_on = false;
		}

		/* fw mechanism */
		btdm8723.ps_tdma_on = true;
		if ((bt_rssi_state1 == BT_RSSI_STATE_HIGH) ||
			(bt_rssi_state1 == BT_RSSI_STATE_STAY_HIGH)) {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
				 "Wifi rssi-1 high\n");
			/* only rssi high we need to do this, */
			/* when rssi low, the value will modified by fw */
			rtl_write_byte(rtlpriv, 0x883, 0x40);
			if (bt_tx_rx_cnt_lvl == BT_TXRX_CNT_LEVEL_2) {
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
				"[BTCoex], BT TxRx Counters >= 1400\n");
				btdm8723.ps_tdma_byte[0] = 0xa3;
				btdm8723.ps_tdma_byte[1] = 0x5;
				btdm8723.ps_tdma_byte[2] = 0x5;
				btdm8723.ps_tdma_byte[3] = 0x83;
				btdm8723.ps_tdma_byte[4] = 0x80;
			} else if (bt_tx_rx_cnt_lvl == BT_TXRX_CNT_LEVEL_1) {
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
					 "[BTCoex], BT TxRx Counters>= 1200 && < 1400\n");
				btdm8723.ps_tdma_byte[0] = 0xa3;
				btdm8723.ps_tdma_byte[1] = 0xa;
				btdm8723.ps_tdma_byte[2] = 0xa;
				btdm8723.ps_tdma_byte[3] = 0x83;
				btdm8723.ps_tdma_byte[4] = 0x80;
			} else {
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
					 "[BTCoex], BT TxRx Counters < 1200\n");
				btdm8723.ps_tdma_byte[0] = 0xa3;
				btdm8723.ps_tdma_byte[1] = 0xf;
				btdm8723.ps_tdma_byte[2] = 0xf;
				btdm8723.ps_tdma_byte[3] = 0x83;
				btdm8723.ps_tdma_byte[4] = 0x80;
			}
		} else {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
					"Wifi rssi-1 low\n");
			if (bt_tx_rx_cnt_lvl == BT_TXRX_CNT_LEVEL_2) {
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
					 "[BTCoex], BT TxRx Counters >= 1400\n");
				btdm8723.ps_tdma_byte[0] = 0xa3;
				btdm8723.ps_tdma_byte[1] = 0x5;
				btdm8723.ps_tdma_byte[2] = 0x5;
				btdm8723.ps_tdma_byte[3] = 0x2;
				btdm8723.ps_tdma_byte[4] = 0x80;
			} else if (bt_tx_rx_cnt_lvl == BT_TXRX_CNT_LEVEL_1) {
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
					 "[BTCoex], BT TxRx Counters >= 1200 && < 1400\n");
				btdm8723.ps_tdma_byte[0] = 0xa3;
				btdm8723.ps_tdma_byte[1] = 0xa;
				btdm8723.ps_tdma_byte[2] = 0xa;
				btdm8723.ps_tdma_byte[3] = 0x2;
				btdm8723.ps_tdma_byte[4] = 0x80;
			} else {
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
					 "[BTCoex], BT TxRx Counters < 1200\n");
				btdm8723.ps_tdma_byte[0] = 0xa3;
				btdm8723.ps_tdma_byte[1] = 0xf;
				btdm8723.ps_tdma_byte[2] = 0xf;
				btdm8723.ps_tdma_byte[3] = 0x2;
				btdm8723.ps_tdma_byte[4] = 0x80;
			}
		}
	}

	if (rtl8723e_dm_bt_need_to_dec_bt_pwr(hw))
		btdm8723.dec_bt_pwr = true;

	/* Always ignore WlanAct if bHid|bSCOBusy|bSCOeSCO */

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
		 "[BTCoex], BT btInqPageStartTime = 0x%x, btTxRxCntLvl = %d\n",
		 hal_coex_8723.bt_inq_page_start_time, bt_tx_rx_cnt_lvl);
	if ((hal_coex_8723.bt_inq_page_start_time) ||
	    (BT_TXRX_CNT_LEVEL_3 == bt_tx_rx_cnt_lvl)) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			 "[BTCoex], Set BT inquiry / page scan 0x3a setting\n");
		btdm8723.ps_tdma_on = true;
		btdm8723.ps_tdma_byte[0] = 0xa3;
		btdm8723.ps_tdma_byte[1] = 0x5;
		btdm8723.ps_tdma_byte[2] = 0x5;
		btdm8723.ps_tdma_byte[3] = 0x2;
		btdm8723.ps_tdma_byte[4] = 0x80;
	}

	if (rtl8723e_dm_bt_is_coexist_state_changed(hw))
		rtl8723e_dm_bt_set_bt_dm(hw, &btdm8723);

}

static void rtl8723e_dm_bt_2_ant_ftp_a2dp(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct btdm_8723 btdm8723;

	u8 bt_rssi_state, bt_rssi_state1;
	u32 bt_tx_rx_cnt_lvl = 0;

	rtl8723e_dm_bt_btdm_structure_reload(hw, &btdm8723);

	btdm8723.rf_rx_lpf_shrink = true;
	btdm8723.low_penalty_rate_adaptive = true;
	btdm8723.reject_aggre_pkt = false;

	bt_tx_rx_cnt_lvl = rtl8723e_dm_bt_bt_tx_rx_counter_level(hw);

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
	"[BTCoex], BT TxRx Counters = %d\n", bt_tx_rx_cnt_lvl);

	if (rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_20_40) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG, "HT40\n");
		bt_rssi_state =
		  rtl8723e_dm_bt_check_coex_rssi_state(hw, 2, 37, 0);

		/* coex table */
		btdm8723.val_0x6c0 = 0x55555555;
		btdm8723.val_0x6c8 = 0xffff;
		btdm8723.val_0x6cc = 0x3;

		/* sw mechanism */
		btdm8723.agc_table_en = false;
		btdm8723.adc_back_off_on = true;
		btdm8723.sw_dac_swing_on = false;

		/* fw mechanism */
		btdm8723.ps_tdma_on = true;
		if ((bt_rssi_state == BT_RSSI_STATE_HIGH) ||
			(bt_rssi_state == BT_RSSI_STATE_STAY_HIGH)) {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
						"Wifi rssi high\n");
			if (bt_tx_rx_cnt_lvl == BT_TXRX_CNT_LEVEL_2) {
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
				"[BTCoex], BT TxRx Counters >= 1400\n");
				btdm8723.ps_tdma_byte[0] = 0xa3;
				btdm8723.ps_tdma_byte[1] = 0x5;
				btdm8723.ps_tdma_byte[2] = 0x5;
				btdm8723.ps_tdma_byte[3] = 0x81;
				btdm8723.ps_tdma_byte[4] = 0x80;
			} else if (bt_tx_rx_cnt_lvl == BT_TXRX_CNT_LEVEL_1) {
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
					 "[BTCoex], BT TxRx Counters >= 1200 && < 1400\n");
				btdm8723.ps_tdma_byte[0] = 0xa3;
				btdm8723.ps_tdma_byte[1] = 0xa;
				btdm8723.ps_tdma_byte[2] = 0xa;
				btdm8723.ps_tdma_byte[3] = 0x81;
				btdm8723.ps_tdma_byte[4] = 0x80;
			} else {
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
					 "[BTCoex], BT TxRx Counters < 1200\n");
				btdm8723.ps_tdma_byte[0] = 0xa3;
				btdm8723.ps_tdma_byte[1] = 0xf;
				btdm8723.ps_tdma_byte[2] = 0xf;
				btdm8723.ps_tdma_byte[3] = 0x81;
				btdm8723.ps_tdma_byte[4] = 0x80;
			}
		} else {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
				 "Wifi rssi low\n");
			if (bt_tx_rx_cnt_lvl == BT_TXRX_CNT_LEVEL_2) {
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
					 "[BTCoex], BT TxRx Counters >= 1400\n");
				btdm8723.ps_tdma_byte[0] = 0xa3;
				btdm8723.ps_tdma_byte[1] = 0x5;
				btdm8723.ps_tdma_byte[2] = 0x5;
				btdm8723.ps_tdma_byte[3] = 0x0;
				btdm8723.ps_tdma_byte[4] = 0x80;
			} else if (bt_tx_rx_cnt_lvl ==
				BT_TXRX_CNT_LEVEL_1) {
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
					 "[BTCoex], BT TxRx Counters >= 1200 && < 1400\n");
				btdm8723.ps_tdma_byte[0] = 0xa3;
				btdm8723.ps_tdma_byte[1] = 0xa;
				btdm8723.ps_tdma_byte[2] = 0xa;
				btdm8723.ps_tdma_byte[3] = 0x0;
				btdm8723.ps_tdma_byte[4] = 0x80;
			} else {
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
					 "[BTCoex], BT TxRx Counters < 1200\n");
				btdm8723.ps_tdma_byte[0] = 0xa3;
				btdm8723.ps_tdma_byte[1] = 0xf;
				btdm8723.ps_tdma_byte[2] = 0xf;
				btdm8723.ps_tdma_byte[3] = 0x0;
				btdm8723.ps_tdma_byte[4] = 0x80;
			}
		}
	} else {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			 "HT20 or Legacy\n");
		bt_rssi_state =
		  rtl8723e_dm_bt_check_coex_rssi_state(hw, 2, 47, 0);
		bt_rssi_state1 =
		  rtl8723e_dm_bt_check_coex_rssi_state1(hw, 2, 27, 0);

		/* coex table */
		btdm8723.val_0x6c0 = 0x55555555;
		btdm8723.val_0x6c8 = 0xffff;
		btdm8723.val_0x6cc = 0x3;

		/* sw mechanism */
		if ((bt_rssi_state == BT_RSSI_STATE_HIGH) ||
			(bt_rssi_state == BT_RSSI_STATE_STAY_HIGH)) {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
				 "Wifi rssi high\n");
			btdm8723.agc_table_en = true;
			btdm8723.adc_back_off_on = true;
			btdm8723.sw_dac_swing_on = false;
		} else {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
				 "Wifi rssi low\n");
			btdm8723.agc_table_en = false;
			btdm8723.adc_back_off_on = false;
			btdm8723.sw_dac_swing_on = false;
		}

		/* fw mechanism */
		btdm8723.ps_tdma_on = true;
		if ((bt_rssi_state1 == BT_RSSI_STATE_HIGH) ||
			(bt_rssi_state1 == BT_RSSI_STATE_STAY_HIGH)) {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
				 "Wifi rssi-1 high\n");
			/* only rssi high we need to do this, */
			/* when rssi low, the value will modified by fw */
			rtl_write_byte(rtlpriv, 0x883, 0x40);
			if (bt_tx_rx_cnt_lvl == BT_TXRX_CNT_LEVEL_2) {
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
					 "[BTCoex], BT TxRx Counters >= 1400\n");
				btdm8723.ps_tdma_byte[0] = 0xa3;
				btdm8723.ps_tdma_byte[1] = 0x5;
				btdm8723.ps_tdma_byte[2] = 0x5;
				btdm8723.ps_tdma_byte[3] = 0x81;
				btdm8723.ps_tdma_byte[4] = 0x80;
			} else if (bt_tx_rx_cnt_lvl == BT_TXRX_CNT_LEVEL_1) {
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
					 "[BTCoex], BT TxRx Counters >= 1200 && < 1400\n");
				btdm8723.ps_tdma_byte[0] = 0xa3;
				btdm8723.ps_tdma_byte[1] = 0xa;
				btdm8723.ps_tdma_byte[2] = 0xa;
				btdm8723.ps_tdma_byte[3] = 0x81;
				btdm8723.ps_tdma_byte[4] = 0x80;
			} else {
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
					 "[BTCoex], BT TxRx Counters < 1200\n");
				btdm8723.ps_tdma_byte[0] = 0xa3;
				btdm8723.ps_tdma_byte[1] = 0xf;
				btdm8723.ps_tdma_byte[2] = 0xf;
				btdm8723.ps_tdma_byte[3] = 0x81;
				btdm8723.ps_tdma_byte[4] = 0x80;
			}
		} else {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
				 "Wifi rssi-1 low\n");
			if (bt_tx_rx_cnt_lvl == BT_TXRX_CNT_LEVEL_2) {
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
					 "[BTCoex], BT TxRx Counters >= 1400\n");
				btdm8723.ps_tdma_byte[0] = 0xa3;
				btdm8723.ps_tdma_byte[1] = 0x5;
				btdm8723.ps_tdma_byte[2] = 0x5;
				btdm8723.ps_tdma_byte[3] = 0x0;
				btdm8723.ps_tdma_byte[4] = 0x80;
			} else if (bt_tx_rx_cnt_lvl == BT_TXRX_CNT_LEVEL_1) {
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
					 "[BTCoex], BT TxRx Counters >= 1200 && < 1400\n");
				btdm8723.ps_tdma_byte[0] = 0xa3;
				btdm8723.ps_tdma_byte[1] = 0xa;
				btdm8723.ps_tdma_byte[2] = 0xa;
				btdm8723.ps_tdma_byte[3] = 0x0;
				btdm8723.ps_tdma_byte[4] = 0x80;
			} else {
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
					 "[BTCoex], BT TxRx Counters < 1200\n");
				btdm8723.ps_tdma_byte[0] = 0xa3;
				btdm8723.ps_tdma_byte[1] = 0xf;
				btdm8723.ps_tdma_byte[2] = 0xf;
				btdm8723.ps_tdma_byte[3] = 0x0;
				btdm8723.ps_tdma_byte[4] = 0x80;
			}
		}
	}

	if (rtl8723e_dm_bt_need_to_dec_bt_pwr(hw))
		btdm8723.dec_bt_pwr = true;

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
		 "[BTCoex], BT btInqPageStartTime = 0x%x, btTxRxCntLvl = %d\n",
		 hal_coex_8723.bt_inq_page_start_time, bt_tx_rx_cnt_lvl);

	if ((hal_coex_8723.bt_inq_page_start_time) ||
	    (BT_TXRX_CNT_LEVEL_3 == bt_tx_rx_cnt_lvl)) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			 "[BTCoex], Set BT inquiry / page scan 0x3a setting\n");
		btdm8723.ps_tdma_on = true;
		btdm8723.ps_tdma_byte[0] = 0xa3;
		btdm8723.ps_tdma_byte[1] = 0x5;
		btdm8723.ps_tdma_byte[2] = 0x5;
		btdm8723.ps_tdma_byte[3] = 0x83;
		btdm8723.ps_tdma_byte[4] = 0x80;
	}

	if (rtl8723e_dm_bt_is_coexist_state_changed(hw))
		rtl8723e_dm_bt_set_bt_dm(hw, &btdm8723);

}

static void rtl8723e_dm_bt_inq_page_monitor(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 cur_time;

	cur_time = jiffies;
	if (hal_coex_8723.c2h_bt_inquiry_page) {
		/* bt inquiry or page is started. */
		if (hal_coex_8723.bt_inq_page_start_time == 0) {
			rtlpriv->btcoexist.cstate  |=
			BT_COEX_STATE_BT_INQ_PAGE;
			hal_coex_8723.bt_inq_page_start_time = cur_time;
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
				 "[BTCoex], BT Inquiry/page is started at time : 0x%x\n",
				 hal_coex_8723.bt_inq_page_start_time);
		}
	}
	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
		 "[BTCoex], BT Inquiry/page started time : 0x%x, cur_time : 0x%x\n",
		 hal_coex_8723.bt_inq_page_start_time, cur_time);

	if (hal_coex_8723.bt_inq_page_start_time) {
		if ((((long)cur_time -
			(long)hal_coex_8723.bt_inq_page_start_time) / HZ)
			>= 10) {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
				"[BTCoex], BT Inquiry/page >= 10sec!!!\n");
			hal_coex_8723.bt_inq_page_start_time = 0;
			rtlpriv->btcoexist.cstate &=
				~BT_COEX_STATE_BT_INQ_PAGE;
		}
	}
}

static void rtl8723e_dm_bt_reset_action_profile_state(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpriv->btcoexist.cstate &= ~
		(BT_COEX_STATE_PROFILE_HID | BT_COEX_STATE_PROFILE_A2DP|
		BT_COEX_STATE_PROFILE_PAN | BT_COEX_STATE_PROFILE_SCO);

	rtlpriv->btcoexist.cstate &= ~
		(BT_COEX_STATE_BTINFO_COMMON |
		BT_COEX_STATE_BTINFO_B_HID_SCOESCO|
		BT_COEX_STATE_BTINFO_B_FTP_A2DP);
}

static void _rtl8723e_dm_bt_coexist_2_ant(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 bt_info_original;
	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
		"[BTCoex] Get bt info by fw!!\n");

	_rtl8723_dm_bt_check_wifi_state(hw);

	if (hal_coex_8723.c2h_bt_info_req_sent) {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
				"[BTCoex] c2h for bt_info not rcvd yet!!\n");
	}

	bt_info_original = hal_coex_8723.c2h_bt_info_original;

	/* when bt inquiry or page scan, we have to set h2c 0x25 */
	/* ignore wlanact for continuous 4x2secs */
	rtl8723e_dm_bt_inq_page_monitor(hw);
	rtl8723e_dm_bt_reset_action_profile_state(hw);

	if (rtl8723e_dm_bt_is_2_ant_common_action(hw)) {
		rtlpriv->btcoexist.bt_profile_case = BT_COEX_MECH_COMMON;
		rtlpriv->btcoexist.bt_profile_action = BT_COEX_MECH_COMMON;
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
		"Action 2-Ant common.\n");
	} else {
		if ((bt_info_original & BTINFO_B_HID) ||
			(bt_info_original & BTINFO_B_SCO_BUSY) ||
			(bt_info_original & BTINFO_B_SCO_ESCO)) {
				rtlpriv->btcoexist.cstate |=
					BT_COEX_STATE_BTINFO_B_HID_SCOESCO;
				rtlpriv->btcoexist.bt_profile_case =
					BT_COEX_MECH_HID_SCO_ESCO;
				rtlpriv->btcoexist.bt_profile_action =
					BT_COEX_MECH_HID_SCO_ESCO;
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
					 "[BTCoex], BTInfo: bHid|bSCOBusy|bSCOeSCO\n");
				rtl8723e_dm_bt_2_ant_hid_sco_esco(hw);
		} else if ((bt_info_original & BTINFO_B_FTP) ||
				(bt_info_original & BTINFO_B_A2DP)) {
				rtlpriv->btcoexist.cstate |=
					BT_COEX_STATE_BTINFO_B_FTP_A2DP;
				rtlpriv->btcoexist.bt_profile_case =
					BT_COEX_MECH_FTP_A2DP;
				rtlpriv->btcoexist.bt_profile_action =
					BT_COEX_MECH_FTP_A2DP;
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
					 "BTInfo: bFTP|bA2DP\n");
				rtl8723e_dm_bt_2_ant_ftp_a2dp(hw);
		} else {
				rtlpriv->btcoexist.cstate |=
					BT_COEX_STATE_BTINFO_B_HID_SCOESCO;
				rtlpriv->btcoexist.bt_profile_case =
					BT_COEX_MECH_NONE;
				rtlpriv->btcoexist.bt_profile_action =
					BT_COEX_MECH_NONE;
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
					 "[BTCoex], BTInfo: undefined case!!!!\n");
				rtl8723e_dm_bt_2_ant_hid_sco_esco(hw);
		}
	}
}

static void _rtl8723e_dm_bt_coexist_1_ant(struct ieee80211_hw *hw)
{
	return;
}

void rtl8723e_dm_bt_hw_coex_all_off_8723a(struct ieee80211_hw *hw)
{
	rtl8723e_dm_bt_set_coex_table(hw, 0x5a5aaaaa, 0xcc, 0x3);
	rtl8723e_dm_bt_set_hw_pta_mode(hw, true);
}

void rtl8723e_dm_bt_fw_coex_all_off_8723a(struct ieee80211_hw *hw)
{
	rtl8723e_dm_bt_set_fw_ignore_wlan_act(hw, false);
	rtl8723e_dm_bt_set_fw_3a(hw, 0x0, 0x0, 0x0, 0x8, 0x0);
	rtl8723e_dm_bt_set_fw_2_ant_hid(hw, false, false);
	rtl8723e_dm_bt_set_fw_tra_tdma_ctrl(hw, false, TDMA_2ANT,
					    TDMA_NAV_OFF);
	rtl8723e_dm_bt_set_fw_tdma_ctrl(hw, false, TDMA_2ANT, TDMA_NAV_OFF,
					TDMA_DAC_SWING_OFF);
	rtl8723e_dm_bt_set_fw_dac_swing_level(hw, 0);
	rtl8723e_dm_bt_set_fw_bt_hid_info(hw, false);
	rtl8723e_dm_bt_set_fw_bt_retry_index(hw, 2);
	rtl8723e_dm_bt_set_fw_wlan_act(hw, 0x10, 0x10);
	rtl8723e_dm_bt_set_fw_dec_bt_pwr(hw, false);
}

void rtl8723e_dm_bt_sw_coex_all_off_8723a(struct ieee80211_hw *hw)
{
	rtl8723e_dm_bt_agc_table(hw, BT_AGCTABLE_OFF);
	rtl8723e_dm_bt_bb_back_off_level(hw, BT_BB_BACKOFF_OFF);
	rtl8723e_dm_bt_reject_ap_aggregated_packet(hw, false);

	dm_bt_set_sw_penalty_tx_rate_adapt(hw, BT_TX_RATE_ADAPTIVE_NORMAL);
	rtl8723e_dm_bt_set_sw_rf_rx_lpf_corner(hw, BT_RF_RX_LPF_CORNER_RESUME);
	rtl8723e_dm_bt_set_sw_full_time_dac_swing(hw, false, 0xc0);
}

static void rtl8723e_dm_bt_query_bt_information(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 h2c_parameter[1] = {0};

	hal_coex_8723.c2h_bt_info_req_sent = true;

	h2c_parameter[0] |=  BIT(0);

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
		"Query Bt information, write 0x38=0x%x\n", h2c_parameter[0]);

	rtl8723e_fill_h2c_cmd(hw, 0x38, 1, h2c_parameter);
}

static void rtl8723e_dm_bt_bt_hw_counters_monitor(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 reg_hp_tx_rx, reg_lp_tx_rx, u32_tmp;
	u32 reg_hp_tx = 0, reg_hp_rx = 0, reg_lp_tx = 0, reg_lp_rx = 0;

	reg_hp_tx_rx = REG_HIGH_PRIORITY_TXRX;
	reg_lp_tx_rx = REG_LOW_PRIORITY_TXRX;

	u32_tmp = rtl_read_dword(rtlpriv, reg_hp_tx_rx);
	reg_hp_tx = u32_tmp & MASKLWORD;
	reg_hp_rx = (u32_tmp & MASKHWORD)>>16;

	u32_tmp = rtl_read_dword(rtlpriv, reg_lp_tx_rx);
	reg_lp_tx = u32_tmp & MASKLWORD;
	reg_lp_rx = (u32_tmp & MASKHWORD)>>16;

	if (rtlpriv->btcoexist.lps_counter > 1) {
		reg_hp_tx %= rtlpriv->btcoexist.lps_counter;
		reg_hp_rx %= rtlpriv->btcoexist.lps_counter;
		reg_lp_tx %= rtlpriv->btcoexist.lps_counter;
		reg_lp_rx %= rtlpriv->btcoexist.lps_counter;
	}

	hal_coex_8723.high_priority_tx = reg_hp_tx;
	hal_coex_8723.high_priority_rx = reg_hp_rx;
	hal_coex_8723.low_priority_tx = reg_lp_tx;
	hal_coex_8723.low_priority_rx = reg_lp_rx;

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
		"High Priority Tx/Rx (reg 0x%x)=%x(%d)/%x(%d)\n",
		reg_hp_tx_rx, reg_hp_tx, reg_hp_tx, reg_hp_rx, reg_hp_rx);
	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
		"Low Priority Tx/Rx (reg 0x%x)=%x(%d)/%x(%d)\n",
		reg_lp_tx_rx, reg_lp_tx, reg_lp_tx, reg_lp_rx, reg_lp_rx);
	rtlpriv->btcoexist.lps_counter = 0;
	/* rtl_write_byte(rtlpriv, 0x76e, 0xc); */
}

static void rtl8723e_dm_bt_bt_enable_disable_check(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	bool bt_alife = true;

	if (hal_coex_8723.high_priority_tx == 0 &&
	    hal_coex_8723.high_priority_rx == 0 &&
	    hal_coex_8723.low_priority_tx == 0 &&
	    hal_coex_8723.low_priority_rx == 0) {
		bt_alife = false;
	}
	if (hal_coex_8723.high_priority_tx == 0xeaea &&
	    hal_coex_8723.high_priority_rx == 0xeaea &&
	    hal_coex_8723.low_priority_tx == 0xeaea &&
	    hal_coex_8723.low_priority_rx == 0xeaea) {
		bt_alife = false;
	}
	if (hal_coex_8723.high_priority_tx == 0xffff &&
	    hal_coex_8723.high_priority_rx == 0xffff &&
	    hal_coex_8723.low_priority_tx == 0xffff &&
	    hal_coex_8723.low_priority_rx == 0xffff) {
		bt_alife = false;
	}
	if (bt_alife) {
		rtlpriv->btcoexist.bt_active_zero_cnt = 0;
		rtlpriv->btcoexist.cur_bt_disabled = false;
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
			 "8723A BT is enabled !!\n");
	} else {
		rtlpriv->btcoexist.bt_active_zero_cnt++;
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
			 "8723A bt all counters=0, %d times!!\n",
			 rtlpriv->btcoexist.bt_active_zero_cnt);
		if (rtlpriv->btcoexist.bt_active_zero_cnt >= 2) {
			rtlpriv->btcoexist.cur_bt_disabled = true;
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
				 "8723A BT is disabled !!\n");
		}
	}
	if (rtlpriv->btcoexist.pre_bt_disabled !=
		rtlpriv->btcoexist.cur_bt_disabled) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST,
			 DBG_TRACE, "8723A BT is from %s to %s!!\n",
			 (rtlpriv->btcoexist.pre_bt_disabled ?
				"disabled" : "enabled"),
			 (rtlpriv->btcoexist.cur_bt_disabled ?
				"disabled" : "enabled"));
		rtlpriv->btcoexist.pre_bt_disabled
			= rtlpriv->btcoexist.cur_bt_disabled;
	}
}


void rtl8723e_dm_bt_coexist_8723(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl8723e_dm_bt_query_bt_information(hw);
	rtl8723e_dm_bt_bt_hw_counters_monitor(hw);
	rtl8723e_dm_bt_bt_enable_disable_check(hw);

	if (rtlpriv->btcoexist.bt_ant_num == ANT_X2) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			"[BTCoex], 2 Ant mechanism\n");
		_rtl8723e_dm_bt_coexist_2_ant(hw);
	} else {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
			"[BTCoex], 1 Ant mechanism\n");
		_rtl8723e_dm_bt_coexist_1_ant(hw);
	}

	if (!rtl8723e_dm_bt_is_same_coexist_state(hw)) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			 "[BTCoex], Coexist State[bitMap] change from 0x%x%8x to 0x%x%8x\n",
			 rtlpriv->btcoexist.previous_state_h,
			 rtlpriv->btcoexist.previous_state,
			 rtlpriv->btcoexist.cstate_h,
			 rtlpriv->btcoexist.cstate);
		rtlpriv->btcoexist.previous_state
			= rtlpriv->btcoexist.cstate;
		rtlpriv->btcoexist.previous_state_h
			= rtlpriv->btcoexist.cstate_h;
	}
}

static void rtl8723e_dm_bt_parse_bt_info(struct ieee80211_hw *hw,
					 u8 *tmp_buf, u8 len)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 bt_info;
	u8 i;

	hal_coex_8723.c2h_bt_info_req_sent = false;
	hal_coex_8723.bt_retry_cnt = 0;
	for (i = 0; i < len; i++) {
		if (i == 0)
			hal_coex_8723.c2h_bt_info_original = tmp_buf[i];
		else if (i == 1)
			hal_coex_8723.bt_retry_cnt = tmp_buf[i];
		if (i == len-1)
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
				 "0x%2x]", tmp_buf[i]);
		else
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_TRACE,
				 "0x%2x, ", tmp_buf[i]);

	}
	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
		"BT info bt_info (Data)= 0x%x\n",
			hal_coex_8723.c2h_bt_info_original);
	bt_info = hal_coex_8723.c2h_bt_info_original;

	if (bt_info & BIT(2))
		hal_coex_8723.c2h_bt_inquiry_page = true;
	else
		hal_coex_8723.c2h_bt_inquiry_page = false;


	if (bt_info & BTINFO_B_CONNECTION) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			"[BTC2H], BTInfo: bConnect=true\n");
		rtlpriv->btcoexist.bt_busy = true;
		rtlpriv->btcoexist.cstate &= ~BT_COEX_STATE_BT_IDLE;
	} else {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			"[BTC2H], BTInfo: bConnect=false\n");
		rtlpriv->btcoexist.bt_busy = false;
		rtlpriv->btcoexist.cstate |= BT_COEX_STATE_BT_IDLE;
	}
}
void rtl_8723e_c2h_command_handle(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct c2h_evt_hdr c2h_event;
	u8 *ptmp_buf = NULL;
	u8 index = 0;
	u8 u1b_tmp = 0;
	memset(&c2h_event, 0, sizeof(c2h_event));
	u1b_tmp = rtl_read_byte(rtlpriv, REG_C2HEVT_MSG_NORMAL);
	RT_TRACE(rtlpriv, COMP_FW, DBG_DMESG,
		"&&&&&&: REG_C2HEVT_MSG_NORMAL is 0x%x\n", u1b_tmp);
	c2h_event.cmd_id = u1b_tmp & 0xF;
	c2h_event.cmd_len = (u1b_tmp & 0xF0) >> 4;
	c2h_event.cmd_seq = rtl_read_byte(rtlpriv, REG_C2HEVT_MSG_NORMAL + 1);
	RT_TRACE(rtlpriv, COMP_FW, DBG_DMESG,
		 "cmd_id: %d, cmd_len: %d, cmd_seq: %d\n",
		 c2h_event.cmd_id , c2h_event.cmd_len, c2h_event.cmd_seq);
	u1b_tmp = rtl_read_byte(rtlpriv, 0x01AF);
	if (u1b_tmp == C2H_EVT_HOST_CLOSE) {
		return;
	} else if (u1b_tmp != C2H_EVT_FW_CLOSE) {
		rtl_write_byte(rtlpriv, 0x1AF, 0x00);
		return;
	}
	ptmp_buf = kzalloc(c2h_event.cmd_len, GFP_KERNEL);
	if (ptmp_buf == NULL) {
		RT_TRACE(rtlpriv, COMP_FW, DBG_TRACE,
			 "malloc cmd buf failed\n");
		return;
	}

	/* Read the content */
	for (index = 0; index < c2h_event.cmd_len; index++)
		ptmp_buf[index] = rtl_read_byte(rtlpriv,
					REG_C2HEVT_MSG_NORMAL + 2 + index);


	switch (c2h_event.cmd_id) {
	case C2H_V0_BT_RSSI:
			break;

	case C2H_V0_BT_OP_MODE:
			break;

	case C2H_V0_BT_INFO:
		RT_TRACE(rtlpriv, COMP_FW, DBG_TRACE,
			"BT info Byte[0] (ID) is 0x%x\n",
			c2h_event.cmd_id);
		RT_TRACE(rtlpriv, COMP_FW, DBG_TRACE,
			"BT info Byte[1] (Seq) is 0x%x\n",
			c2h_event.cmd_seq);
		RT_TRACE(rtlpriv, COMP_FW, DBG_TRACE,
			"BT info Byte[2] (Data)= 0x%x\n", ptmp_buf[0]);

		rtl8723e_dm_bt_parse_bt_info(hw, ptmp_buf, c2h_event.cmd_len);

		if (rtlpriv->cfg->ops->get_btc_status())
			rtlpriv->btcoexist.btc_ops->btc_periodical(rtlpriv);

		break;
	default:
		break;
	}
	kfree(ptmp_buf);

	rtl_write_byte(rtlpriv, 0x01AF, C2H_EVT_HOST_CLOSE);
}
