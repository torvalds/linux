/******************************************************************************
 *
 * Copyright(c) 2007 - 2013 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/

#include "halbt_precomp.h"

/*#if(BT_30_SUPPORT == 1)*/
#if 1
/***********************************************
 *		Global variables
 ***********************************************/
const char *const bt_profile_string[]={
	"NONE",
	"A2DP",
	"PAN",
	"HID",
	"SCO",
};

const char *const bt_spec_string[]={
	"1.0b",
	"1.1",
	"1.2",
	"2.0+EDR",
	"2.1+EDR",
	"3.0+HS",
	"4.0",
};

const char *const bt_link_role_string[]={
	"Master",
	"Slave",
};

const char *const h2c_state_string[]={
	"successful",
	"h2c busy",
	"rf off",
	"fw not read",
};

const char *const io_state_string[]={
	"IO_STATUS_SUCCESS",
	"IO_STATUS_FAIL_CANNOT_IO",
	"IO_STATUS_FAIL_RF_OFF",
	"IO_STATUS_FAIL_FW_READ_CLEAR_TIMEOUT",
	"IO_STATUS_FAIL_WAIT_IO_EVENT_TIMEOUT",
	"IO_STATUS_INVALID_LEN",
	"IO_STATUS_IO_IDLE_QUEUE_EMPTY",
	"IO_STATUS_IO_INSERT_WAIT_QUEUE_FAIL",
	"IO_STATUS_UNKNOWN_FAIL",
	"IO_STATUS_WRONG_LEVEL",
	"IO_STATUS_H2C_STOPPED",
};

struct btc_coexist gl_bt_coexist;

u32 btc_dbg_type[BTC_MSG_MAX];
u8 btc_dbg_buf[100];

/***************************************************
 *		Debug related function
 ***************************************************/
bool halbtc_is_bt_coexist_available(struct btc_coexist *btcoexist)
{
	if (!btcoexist->binded || NULL == btcoexist->adapter)
		return false;
	
	return true;
}

bool halbtc_is_wifi_busy(struct rtl_priv *rtlpriv)
{

	if (rtlpriv->link_info.b_busytraffic)
		return true;
	else
		return false;
}


void halbtc_dbg_init(void)
{
	u8 i;

	for (i = 0; i < BTC_MSG_MAX; i++)
		btc_dbg_type[i] = 0;

	btc_dbg_type[BTC_MSG_INTERFACE]			= 	\
//			INTF_INIT				|
//			INTF_NOTIFY				|
			0;
	
	btc_dbg_type[BTC_MSG_ALGORITHM]			= 	\
//			ALGO_BT_RSSI_STATE			|
//			ALGO_WIFI_RSSI_STATE			|
//			ALGO_BT_MONITOR				|
//			ALGO_TRACE				|
//			ALGO_TRACE_FW				|
//			ALGO_TRACE_FW_DETAIL			|
//			ALGO_TRACE_FW_EXEC			|
//			ALGO_TRACE_SW				|
//			ALGO_TRACE_SW_DETAIL			|
//			ALGO_TRACE_SW_EXEC			|
			0;
}

bool halbtc_is_hw_mailbox_exist(struct btc_coexist *btcoexist)
{
	return true;
}

bool halbtc_is_bt40(struct rtl_priv *adapter)
{
	struct rtl_priv *rtlpriv = adapter;
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	bool is_ht40 = true;
	enum ht_channel_width bw = rtlphy->current_chan_bw;
	
	
	if (bw == HT_CHANNEL_WIDTH_20)
		is_ht40 = false;
	else if (bw == HT_CHANNEL_WIDTH_20_40)
		is_ht40 = true;

	return is_ht40;
}

bool halbtc_legacy(struct rtl_priv *adapter)
{
	struct rtl_priv *rtlpriv = adapter;
	struct rtl_mac *mac = rtl_mac(rtlpriv);
	
	bool is_legacy = false;

	if ((mac->mode == WIRELESS_MODE_B) || (mac->mode == WIRELESS_MODE_B))
		is_legacy = true;

	return is_legacy;
}

bool halbtc_is_wifi_uplink(struct rtl_priv *adapter)
{
	struct rtl_priv *rtlpriv = adapter;
	
	if (rtlpriv->link_info.b_tx_busy_traffic)
		return true;
	else
		return false;
}

u32 halbtc_get_wifi_bw(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = 
		(struct rtl_priv *)btcoexist->adapter;
	u32 wifi_bw = BTC_WIFI_BW_HT20;
	
	if (halbtc_is_bt40(rtlpriv)){
		wifi_bw = BTC_WIFI_BW_HT40;	
	} else {
		if(halbtc_legacy(rtlpriv))
			wifi_bw = BTC_WIFI_BW_LEGACY;
		else
			wifi_bw = BTC_WIFI_BW_HT20;
	}
	return wifi_bw;
}

u8 halbtc_get_wifi_central_chnl(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	struct rtl_phy 	*rtlphy = &(rtlpriv->phy);
	u8 chnl = 1;
	

	if (rtlphy->current_channel != 0)
		chnl = rtlphy->current_channel;
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, 
		  "halbtc_get_wifi_central_chnl:%d\n",chnl);
	return chnl;
}

void halbtc_leave_lps(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv;
	struct rtl_ps_ctl *ppsc;
	bool ap_enable = false;
	
	rtlpriv = btcoexist->adapter;
	ppsc = rtl_psc(rtlpriv);
	
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_AP_MODE_ENABLE,
			   &ap_enable);
	
	if (ap_enable) {
		printk("halbtc_leave_lps()<--dont leave lps under AP mode\n");
		return;
	}

	btcoexist->bt_info.bt_ctrl_lps = true;
	btcoexist->bt_info.bt_lps_on = false;
}

void halbtc_enter_lps(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv;
	struct rtl_ps_ctl *ppsc;
	bool ap_enable = false;
	
	rtlpriv = btcoexist->adapter;
	ppsc = rtl_psc(rtlpriv);
	
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_AP_MODE_ENABLE, 
			   &ap_enable);
	
	if (ap_enable) {
		printk("halbtc_enter_lps()<--dont enter lps under AP mode\n");
		return;
	}
	
	btcoexist->bt_info.bt_ctrl_lps = true;
	btcoexist->bt_info.bt_lps_on = false;
}

void halbtc_normal_lps(struct btc_coexist *btcoexist)
{
	if (btcoexist->bt_info.bt_ctrl_lps) {
		btcoexist->bt_info.bt_lps_on = false;
		btcoexist->bt_info.bt_ctrl_lps = false;
	}
		
}

void halbtc_leave_low_power(void)
{
}

void halbtc_nomal_low_power(void)
{
}

void halbtc_disable_low_power(void)
{
}

void halbtc_aggregation_check(void)
{
}


u32 halbtc_get_bt_patch_version(struct btc_coexist *btcoexist)
{
	return 0;
}

s32 halbtc_get_wifi_rssi(struct rtl_priv *adapter)
{
	struct rtl_priv *rtlpriv = adapter;
	s32	undecorated_smoothed_pwdb = 0;
	
	if (rtlpriv->mac80211.link_state >= MAC80211_LINKED)
		undecorated_smoothed_pwdb = 
			rtlpriv->dm.undecorated_smoothed_pwdb;
	else /* associated entry pwdb */
		undecorated_smoothed_pwdb =
			rtlpriv->dm.undecorated_smoothed_pwdb;
	return undecorated_smoothed_pwdb;
}

bool halbtc_get(void *void_btcoexist, u8 get_type, void *out_buf)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)void_btcoexist;
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_mac *mac = rtl_mac(rtlpriv);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	bool *bool_tmp = (bool*)out_buf;
	int *s32_tmp = (int*)out_buf;
	u32 *u32_tmp = (u32*)out_buf;
	u8 *u8_tmp = (u8*)out_buf;
	bool tmp = false;


	if (!halbtc_is_bt_coexist_available(btcoexist))
		return false;
	

	switch (get_type){
	case BTC_GET_BL_HS_OPERATION:
		*bool_tmp = false;
		break;
	case BTC_GET_BL_HS_CONNECTING:
		*bool_tmp = false;
		break;
	case BTC_GET_BL_WIFI_CONNECTED:
		if (rtlpriv->mac80211.link_state >= MAC80211_LINKED)
			tmp = true;
				
		*bool_tmp = tmp;
		break;
	case BTC_GET_BL_WIFI_BUSY:
		if(halbtc_is_wifi_busy(rtlpriv))
			*bool_tmp = true;
		else
			*bool_tmp = false;
		break;
	case BTC_GET_BL_WIFI_SCAN:
		if (mac->act_scanning == true)
			*bool_tmp = true;
		else
			*bool_tmp = false;
		break;
	case BTC_GET_BL_WIFI_LINK:
		if (mac->link_state == MAC80211_LINKING)
			*bool_tmp = true;
		else
			*bool_tmp = false;
		break;
	case BTC_GET_BL_WIFI_ROAM:	/*TODO*/
		if (mac->link_state == MAC80211_LINKING)
			*bool_tmp = true;
		else
			*bool_tmp = false;
		break;
	case BTC_GET_BL_WIFI_4_WAY_PROGRESS:	/*TODO*/
			*bool_tmp = false;

		break;
	case BTC_GET_BL_WIFI_UNDER_5G:
		*bool_tmp = false; /*TODO*/
		
	case BTC_GET_BL_WIFI_DHCP:	/*TODO*/
		break;
	case BTC_GET_BL_WIFI_SOFTAP_IDLE:
		*bool_tmp = true;
		break;
	case BTC_GET_BL_WIFI_SOFTAP_LINKING:
		*bool_tmp = false;
		break;
	case BTC_GET_BL_WIFI_IN_EARLY_SUSPEND:
		*bool_tmp = false;
		break;
	case BTC_GET_BL_WIFI_AP_MODE_ENABLE:
		*bool_tmp = false;
		break;
	case BTC_GET_BL_WIFI_ENABLE_ENCRYPTION:
		if (NO_ENCRYPTION == rtlpriv->sec.pairwise_enc_algorithm)
			*bool_tmp = false;
		else
			*bool_tmp = true;
		break;
	case BTC_GET_BL_WIFI_UNDER_B_MODE:
		*bool_tmp = false; /*TODO*/
		break;
	case BTC_GET_BL_EXT_SWITCH:
		*bool_tmp = false;
		break;
	case BTC_GET_S4_WIFI_RSSI:
		*s32_tmp = halbtc_get_wifi_rssi(rtlpriv);
		break;
	case BTC_GET_S4_HS_RSSI:	/*TODO*/
		*s32_tmp = halbtc_get_wifi_rssi(rtlpriv);
		break;
	case BTC_GET_U4_WIFI_BW:
		*u32_tmp = halbtc_get_wifi_bw(btcoexist);
		break;
	case BTC_GET_U4_WIFI_TRAFFIC_DIRECTION:
		if (halbtc_is_wifi_uplink(rtlpriv))
			*u32_tmp = BTC_WIFI_TRAFFIC_TX;
		else
			*u32_tmp = BTC_WIFI_TRAFFIC_RX;
		break;
	case BTC_GET_U4_WIFI_FW_VER:
		*u32_tmp = rtlhal->fw_version;
		break;
	case BTC_GET_U4_BT_PATCH_VER:
		*u32_tmp = halbtc_get_bt_patch_version(btcoexist);
		break;
	case BTC_GET_U1_WIFI_DOT11_CHNL:
		*u8_tmp = rtlphy->current_channel;
		break;
	case BTC_GET_U1_WIFI_CENTRAL_CHNL:
		*u8_tmp = halbtc_get_wifi_central_chnl(btcoexist);
		break;
	case BTC_GET_U1_WIFI_HS_CHNL:
		*u8_tmp = 1;/* BT_OperateChnl(rtlpriv); */
		break;
	case BTC_GET_U1_MAC_PHY_MODE:
		*u8_tmp = BTC_MP_UNKNOWN;
		break;
	case BTC_GET_U1_AP_NUM:
		/* driver don't know AP num in Linux, 
		 * So, the return value here is not right */
		*u8_tmp = 1;/* pDefMgntInfo->NumBssDesc4Query; */
		break;

	/************* 1Ant **************/
	case BTC_GET_U1_LPS_MODE:
		*u8_tmp = btcoexist->pwr_mode_val[0];
		break;
					
	default:
		break;
	}
	
	return true;
}

bool halbtc_set(void *void_btcoexist, u8 set_type, void *in_buf)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)void_btcoexist;
	bool *bool_tmp = (bool *)in_buf;
	u8 *u8_tmp = (u8 *)in_buf;
	u32 *u32_tmp = (u32 *)in_buf;
	
	
	if (!halbtc_is_bt_coexist_available(btcoexist))
		return false;

	switch (set_type) {
	/* set some bool type variables. */
	case BTC_SET_BL_BT_DISABLE:
		btcoexist->bt_info.bt_disabled = *bool_tmp;
		break;
	case BTC_SET_BL_BT_TRAFFIC_BUSY:
		btcoexist->bt_info.bt_busy = *bool_tmp;
		break;
	case BTC_SET_BL_BT_LIMITED_DIG:
		btcoexist->bt_info.limited_dig = *bool_tmp;
		break;
	case BTC_SET_BL_FORCE_TO_ROAM:
		btcoexist->bt_info.force_to_roam = *bool_tmp;
		break;
	case BTC_SET_BL_TO_REJ_AP_AGG_PKT:
		btcoexist->bt_info.reject_agg_pkt = *bool_tmp;
		break;
	case BTC_SET_BL_BT_CTRL_AGG_SIZE:
		btcoexist->bt_info.b_bt_ctrl_buf_size = *bool_tmp;
		break;
	case BTC_SET_BL_INC_SCAN_DEV_NUM:
		btcoexist->bt_info.increase_scan_dev_num = *bool_tmp;
		break;
		/* set some u1Byte type variables. */
	case BTC_SET_U1_RSSI_ADJ_VAL_FOR_AGC_TABLE_ON:
		btcoexist->bt_info.rssi_adjust_for_agc_table_on = *u8_tmp;
		break;
	case BTC_SET_U1_AGG_BUF_SIZE:
		btcoexist->bt_info.agg_buf_size = *u8_tmp;
		break;
		/* the following are some action which will be triggered */
	case BTC_SET_ACT_GET_BT_RSSI:
		/*BTHCI_SendGetBtRssiEvent(rtlpriv);*/
		break;
	case BTC_SET_ACT_AGGREGATE_CTRL:
		halbtc_aggregation_check();
		break;

		/* 1Ant */
	case BTC_SET_U1_RSSI_ADJ_VAL_FOR_1ANT_COEX_TYPE:
		btcoexist->bt_info.rssi_adjust_for_1ant_coex_type = *u8_tmp;
		break;
	case BTC_SET_UI_SCAN_SIG_COMPENSATION:
	/*	rtlpriv->mlmepriv.scan_compensation = *u8_tmp;  */
		break;
	case BTC_SET_U1_1ANT_LPS:
		btcoexist->bt_info.lps_1ant = *u8_tmp;
		break;
	case BTC_SET_U1_1ANT_RPWM:
		btcoexist->bt_info.rpwm_1ant = *u8_tmp;
		break;
	/* the following are some action which will be triggered  */
	case BTC_SET_ACT_LEAVE_LPS:
		halbtc_leave_lps(btcoexist);
		break;
	case BTC_SET_ACT_ENTER_LPS:
		halbtc_enter_lps(btcoexist);
		break;
	case BTC_SET_ACT_NORMAL_LPS:
		halbtc_normal_lps(btcoexist);
		break;
	case BTC_SET_ACT_DISABLE_LOW_POWER:
		halbtc_disable_low_power();
		break;
	case BTC_SET_ACT_UPDATE_ra_mask:
		btcoexist->bt_info.ra_mask = *u32_tmp;
		break;
	case BTC_SET_ACT_SEND_MIMO_PS:
		break;
	case BTC_SET_ACT_INC_FORCE_EXEC_PWR_CMD_CNT:
		btcoexist->bt_info.force_exec_pwr_cmd_cnt++;
		break;
	case BTC_SET_ACT_CTRL_BT_INFO: /*wait for 8812/8821*/
		break;
	case BTC_SET_ACT_CTRL_BT_COEX:
		break;
	default:
		break;
	}
	
	return true;
}

void halbtc_display_coex_statistics(struct btc_coexist *btcoexist)
{
}

void halbtc_display_bt_link_info(struct btc_coexist *btcoexist)
{
}

void halbtc_display_bt_fw_info(struct btc_coexist *btcoexist)
{
}

void halbtc_display_fw_pwr_mode_cmd(struct btc_coexist *btcoexist)
{
}

/************************************************************
 *		IO related function
 ************************************************************/
u8 halbtc_read_1byte(void *bt_context, u32 reg_addr)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)bt_context;
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	
	return	rtl_read_byte(rtlpriv, reg_addr);
}


u16 halbtc_read_2byte(void *bt_context, u32 reg_addr)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)bt_context;
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	return	rtl_read_word(rtlpriv, reg_addr);
}


u32 halbtc_read_4byte(void *bt_context, u32 reg_addr)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)bt_context;
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	
	return	rtl_read_dword(rtlpriv, reg_addr);
}


void halbtc_write_1byte(void *bt_context, u32 reg_addr, u8 data)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)bt_context;
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	rtl_write_byte(rtlpriv, reg_addr, data);
}

void halbtc_bitmask_write_1byte(void *bt_context, u32 reg_addr, 
				u8 bit_mask, u8 data)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)bt_context;
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	u8 original_value, bit_shift = 0;
	u8 i;

	if (bit_mask != MASKDWORD) {/*if not "double word" write*/
		original_value = rtl_read_byte(rtlpriv, reg_addr);	
		for (i=0; i<=7; i++) {
			if((bit_mask>>i)&0x1)
				break;
		}
		bit_shift = i;
		data = (original_value & (~bit_mask)) | 
			((data << bit_shift) & bit_mask);
	}
	rtl_write_byte(rtlpriv, reg_addr, data);
}


void halbtc_write_2byte(void *bt_context, u32 reg_addr, u16 data)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)bt_context;
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	rtl_write_word(rtlpriv, reg_addr, data);
}


void halbtc_write_4byte(void *bt_context, u32 reg_addr, u32 data)
{
	struct btc_coexist *btcoexist = 
		(struct btc_coexist *)bt_context;
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	rtl_write_dword(rtlpriv, reg_addr, data);
}


void halbtc_set_macreg(void *bt_context, u32 reg_addr, u32 bit_mask, u32 data)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)bt_context;
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	rtl_set_bbreg(rtlpriv->mac80211.hw, reg_addr, bit_mask, data);
}


u32 halbtc_get_macreg(void *bt_context, u32 reg_addr, u32 bit_mask)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)bt_context;
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	return rtl_get_bbreg(rtlpriv->mac80211.hw, reg_addr, bit_mask);
}


void halbtc_set_bbreg(void *bt_context, u32 reg_addr, u32 bit_mask, u32 data)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)bt_context;
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	rtl_set_bbreg(rtlpriv->mac80211.hw, reg_addr, bit_mask, data);
}


u32 halbtc_get_bbreg(void *bt_context, u32 reg_addr, u32 bit_mask)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)bt_context;
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	return rtl_get_bbreg(rtlpriv->mac80211.hw,reg_addr, bit_mask);
}


void halbtc_set_rfreg(void *bt_context, u8 rf_path, u32 reg_addr, 
		      u32 bit_mask, u32 data)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)bt_context;
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	rtl_set_rfreg(rtlpriv->mac80211.hw, rf_path, reg_addr, bit_mask, data);
}


u32 halbtc_get_rfreg(void *bt_context, u8 rf_path, u32 reg_addr, u32 bit_mask)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)bt_context;
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	return rtl_get_rfreg(rtlpriv->mac80211.hw, rf_path, reg_addr, bit_mask);
}


void halbtc_fill_h2c_cmd(void *bt_context, u8 element_id, 
			 u32 cmd_len, u8 *cmd_buf)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)bt_context;
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	
	rtlpriv->cfg->ops->fill_h2c_cmd(rtlpriv->mac80211.hw, element_id, 
					cmd_len, cmd_buf);
}

void halbtc_display_dbg_msg(void *bt_context, u8 disp_type)
{
	struct btc_coexist *btcoexist =	(struct btc_coexist *)bt_context;
	switch (disp_type) {
	case BTC_DBG_DISP_COEX_STATISTICS:
		halbtc_display_coex_statistics(btcoexist);
		break;
	case BTC_DBG_DISP_BT_LINK_INFO:
		halbtc_display_bt_link_info(btcoexist);
		break;
	case BTC_DBG_DISP_BT_FW_VER:
		halbtc_display_bt_fw_info(btcoexist);
		break;
	case BTC_DBG_DISP_FW_PWR_MODE_CMD:
		halbtc_display_fw_pwr_mode_cmd(btcoexist);
		break;
	default:
		break;
	}
}

bool halbtc_under_ips(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	struct rtl_ps_ctl *ppsc = rtl_psc(rtlpriv);
	enum rf_pwrstate rtstate;
	
	if (ppsc->b_inactiveps) {
		rtstate = ppsc->rfpwr_state;

		if (rtstate != ERFON &&
		    ppsc->rfoff_reason == RF_CHANGE_BY_IPS) {

			return true;
		}
	}

	return false;
}

/*****************************************************************
 *         Extern functions called by other module
 *****************************************************************/
bool exhalbtc_initlize_variables(struct rtl_priv *adapter)
{
	struct btc_coexist *btcoexist = &gl_bt_coexist;

	btcoexist->statistics.cnt_bind++;
	
	halbtc_dbg_init();

	if (btcoexist->binded)
		return false;
	else
		btcoexist->binded = true;

#if ( defined(CONFIG_PCI_HCI))
	btcoexist->chip_interface = BTC_INTF_PCI;
#elif ( defined(CONFIG_USB_HCI))
	btcoexist->chip_interface = BTC_INTF_USB;
#elif ( defined(CONFIG_SDIO_HCI))
	btcoexist->chip_interface = BTC_INTF_SDIO;
#elif ( defined(CONFIG_GSPI_HCI))
	btcoexist->chip_interface = BTC_INTF_GSPI;
#else
	btcoexist->chip_interface = BTC_INTF_UNKNOWN;
#endif

	if (NULL == btcoexist->adapter)
		btcoexist->adapter = adapter;

	btcoexist->stack_info.profile_notified = false;

	btcoexist->btc_read_1byte = halbtc_read_1byte;
	btcoexist->btc_write_1byte = halbtc_write_1byte;
	btcoexist->btc_write_1byte_bitmask = halbtc_bitmask_write_1byte;
	btcoexist->btc_read_2byte = halbtc_read_2byte;
	btcoexist->btc_write_2byte = halbtc_write_2byte;
	btcoexist->btc_read_4byte = halbtc_read_4byte;
	btcoexist->btc_write_4byte = halbtc_write_4byte;

	btcoexist->btc_set_bb_reg = halbtc_set_bbreg;
	btcoexist->btc_get_bb_reg = halbtc_get_bbreg;

	btcoexist->btc_set_rf_reg = halbtc_set_rfreg;
	btcoexist->btc_get_rf_reg = halbtc_get_rfreg;

	btcoexist->btc_fill_h2c = halbtc_fill_h2c_cmd;
	btcoexist->btc_disp_dbg_msg = halbtc_display_dbg_msg;
	
	btcoexist->btc_get = halbtc_get;
	btcoexist->btc_set = halbtc_set;

	btcoexist->cli_buf = &btc_dbg_buf[0];

	btcoexist->bt_info.b_bt_ctrl_buf_size = false;
	btcoexist->bt_info.agg_buf_size = 5;

	btcoexist->bt_info.increase_scan_dev_num = false;
	return true;
}

void exhalbtc_init_hw_config(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	
	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;

	btcoexist->statistics.cnt_init_hw_config++;

	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8723BE) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723b2ant_init_hwconfig(btcoexist);
		else if(btcoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723b1ant_init_hwconfig(btcoexist);
	} else if (rtlhal->hw_type == HARDWARE_TYPE_RTL8192EE) {
		ex_halbtc8192e2ant_init_hwconfig(btcoexist);
	}
	
}

void exhalbtc_init_coex_dm(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	
	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;

	btcoexist->statistics.cnt_init_coex_dm++;

	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8723BE) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723b2ant_init_coex_dm(btcoexist);		
		else if(btcoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723b1ant_init_coex_dm(btcoexist);
	} else if (rtlhal->hw_type == HARDWARE_TYPE_RTL8192EE) {
		ex_halbtc8192e2ant_init_coex_dm(btcoexist);
	}
	
	btcoexist->initilized = true;
}

void exhalbtc_ips_notify(struct btc_coexist *btcoexist, u8 type)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	u8 ips_type;

	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;
	btcoexist->statistics.cnt_ips_notify++;
	if (btcoexist->manual_control)
		return;

	if (ERFOFF == type)
		ips_type = BTC_IPS_ENTER;
	else
		ips_type = BTC_IPS_LEAVE;

	halbtc_leave_low_power();
	
	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8723BE) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723b2ant_ips_notify(btcoexist, ips_type);
		else if(btcoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723b1ant_ips_notify(btcoexist, ips_type);
	} else if (rtlhal->hw_type == HARDWARE_TYPE_RTL8192EE) {
		ex_halbtc8192e2ant_ips_notify(btcoexist, ips_type);
	}

	halbtc_nomal_low_power();
}

void exhalbtc_lps_notify(struct btc_coexist *btcoexist, u8 type)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	u8 lps_type;

	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;
	btcoexist->statistics.cnt_lps_notify++;
	if (btcoexist->manual_control)
		return;
	
	if (EACTIVE == type)
		lps_type = BTC_LPS_DISABLE;
	else
		lps_type = BTC_LPS_ENABLE;

	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8723BE) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723b2ant_lps_notify(btcoexist, lps_type);
		else if(btcoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723b1ant_lps_notify(btcoexist, lps_type);
	} else if (rtlhal->hw_type == HARDWARE_TYPE_RTL8192EE) {
		ex_halbtc8192e2ant_lps_notify(btcoexist, lps_type);
	}
}

void exhalbtc_scan_notify(struct btc_coexist *btcoexist, u8 type)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	u8 scan_type;

	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;
	btcoexist->statistics.cnt_scan_notify++;
	if (btcoexist->manual_control)
		return;

	if (type)
		scan_type = BTC_SCAN_START;
	else
		scan_type = BTC_SCAN_FINISH;

	halbtc_leave_low_power();
	
	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8723BE) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723b2ant_scan_notify(btcoexist, scan_type);
		else if(btcoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723b1ant_scan_notify(btcoexist, scan_type);
	} else if (rtlhal->hw_type == HARDWARE_TYPE_RTL8192EE) {
		ex_halbtc8192e2ant_scan_notify(btcoexist, scan_type);
	}

	halbtc_nomal_low_power();
}

void exhalbtc_connect_notify(struct btc_coexist *btcoexist, u8 action)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	u8 asso_type;

	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;
	btcoexist->statistics.cnt_connect_notify++;
	if (btcoexist->manual_control)
		return;

	if (action)
		asso_type = BTC_ASSOCIATE_START;
	else
		asso_type = BTC_ASSOCIATE_FINISH;

	halbtc_leave_low_power();
	
	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8723BE) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723b2ant_connect_notify(btcoexist, asso_type);
		else if(btcoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723b1ant_connect_notify(btcoexist, asso_type);
	} else if (rtlhal->hw_type == HARDWARE_TYPE_RTL8192EE) {
		ex_halbtc8192e2ant_connect_notify(btcoexist, asso_type);
	}

	halbtc_nomal_low_power();
}

void exhalbtc_mediastatus_notify(struct btc_coexist *btcoexist, 
				 enum rt_media_status media_status)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	u8 status;

	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;
	btcoexist->statistics.cnt_media_status_notify++;
	if (btcoexist->manual_control)
		return;

	if (RT_MEDIA_CONNECT == media_status)
		status = BTC_MEDIA_CONNECT;
	else
		status = BTC_MEDIA_DISCONNECT;

	halbtc_leave_low_power();
	
	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8723BE) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723b2ant_media_status_notify(btcoexist, status);
		else if(btcoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723b1ant_media_status_notify(btcoexist, status);
	} else if (rtlhal->hw_type == HARDWARE_TYPE_RTL8192EE) {
		ex_halbtc8192e2ant_media_status_notify(btcoexist, status);
	}

	halbtc_nomal_low_power();
}

void exhalbtc_special_packet_notify(struct btc_coexist *btcoexist, u8 pkt_type)
{
	u8 packet_type;

	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;
	btcoexist->statistics.cnt_special_packet_notify++;
	if (btcoexist->manual_control)
		return;

	/*if(PACKET_DHCP == pkt_type)*/
		packet_type = BTC_PACKET_DHCP;
	/*else if(PACKET_EAPOL == pkt_type)
		packet_type = BTC_PACKET_EAPOL;
	else
		packet_type = BTC_PACKET_UNKNOWN;*/

	halbtc_leave_low_power();

	if (btcoexist->board_info.btdm_ant_num == 2)
		ex_halbtc8723b2ant_special_packet_notify(btcoexist,
							 packet_type);
	else if (btcoexist->board_info.btdm_ant_num == 1)
		ex_halbtc8723b1ant_special_packet_notify(btcoexist,
							 packet_type);

	halbtc_nomal_low_power();
}

void exhalbtc_bt_info_notify(struct btc_coexist *btcoexist, 
			     u8 *tmp_buf, u8 length)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;
	btcoexist->statistics.cnt_bt_info_notify++;

	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8723BE) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723b2ant_bt_info_notify(btcoexist, tmp_buf, length);
		else if(btcoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723b1ant_bt_info_notify(btcoexist, tmp_buf, length);
	} else if (rtlhal->hw_type == HARDWARE_TYPE_RTL8192EE) {
	//	ex_halbtc8192e2ant_bt_info_notify(btcoexist, tmp_buf, length);
	}
}

void exhalbtc_stack_operation_notify(struct btc_coexist *btcoexist, u8 type)
{
	u8 stack_op_type;

	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;
	btcoexist->statistics.cnt_stack_operation_notify++;
	if (btcoexist->manual_control)
		return;

	stack_op_type = BTC_STACK_OP_NONE;
}

void exhalbtc_halt_notify(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;

	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8723BE) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723b2ant_halt_notify(btcoexist);
		else if(btcoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723b1ant_halt_notify(btcoexist);
	} else if (rtlhal->hw_type == HARDWARE_TYPE_RTL8192EE) {
		ex_halbtc8192e2ant_halt_notify(btcoexist);
	}
}

void exhalbtc_pnp_notify(struct btc_coexist *btcoexist, u8 pnp_state)
{
	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;
}

void exhalbtc_periodical(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;
	btcoexist->statistics.cnt_periodical++;

	halbtc_leave_low_power();

	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8723BE) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_halbtc8723b2ant_periodical(btcoexist);
		else if(btcoexist->board_info.btdm_ant_num == 1)
			ex_halbtc8723b1ant_periodical(btcoexist);
	} else if (rtlhal->hw_type == HARDWARE_TYPE_RTL8192EE) {
		ex_halbtc8192e2ant_periodical(btcoexist);
	}

	halbtc_nomal_low_power();
}

void exhalbtc_dbg_control(struct btc_coexist *btcoexist, 
			  u8 code, u8 len, u8 *data)
{
	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;
	btcoexist->statistics.cnt_dbg_ctrl++;
}

void exhalbtc_stack_update_profile_info()
{
}

void exhalbtc_update_min_bt_rssi(char bt_rssi)
{
	struct btc_coexist *btcoexist = &gl_bt_coexist;

	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;

	btcoexist->stack_info.min_bt_rssi = bt_rssi;
}


void exhalbtc_set_hci_version(u16 hci_version)
{
	struct btc_coexist *btcoexist = &gl_bt_coexist;

	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;

	btcoexist->stack_info.hci_version = hci_version;
}

void exhalbtc_set_bt_patch_version(u16 bt_hci_version, u16 bt_patch_version) 
{
	struct btc_coexist *btcoexist = &gl_bt_coexist;

	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;

	btcoexist->bt_info.bt_real_fw_ver = bt_patch_version;
	btcoexist->bt_info.bt_hci_ver = bt_hci_version;
}

void exhalbtc_set_bt_exist(bool bt_exist)
{
	gl_bt_coexist.board_info.bt_exist = bt_exist;
}

void exhalbtc_set_chip_type(u8 chip_type)
{
	switch (chip_type) {
	default:
	case BT_2WIRE:
	case BT_ISSC_3WIRE:
	case BT_ACCEL:
	case BT_RTL8756:
		gl_bt_coexist.board_info.bt_chip_type = BTC_CHIP_UNDEF;
		break;
	case BT_CSR_BC4:
		gl_bt_coexist.board_info.bt_chip_type = BTC_CHIP_CSR_BC4;
		break;
	case BT_CSR_BC8:
		gl_bt_coexist.board_info.bt_chip_type = BTC_CHIP_CSR_BC8;
		break;
	case BT_RTL8723A:
		gl_bt_coexist.board_info.bt_chip_type = BTC_CHIP_RTL8723A;
		break;
	case BT_RTL8821A:
		gl_bt_coexist.board_info.bt_chip_type = BTC_CHIP_RTL8821;
		break;
	case BT_RTL8723B:
		gl_bt_coexist.board_info.bt_chip_type = BTC_CHIP_RTL8723B;
		break;
	}
}

void exhalbtc_set_ant_num(u8 type, u8 ant_num)
{
	if (BT_COEX_ANT_TYPE_PG == type) {
		gl_bt_coexist.board_info.pg_ant_num = ant_num;
		gl_bt_coexist.board_info.btdm_ant_num = ant_num;
	} else if (BT_COEX_ANT_TYPE_ANTDIV == type) {
		gl_bt_coexist.board_info.btdm_ant_num = ant_num;
	}
}

void exhalbtc_display_bt_coex_info(struct btc_coexist *btcoexist)
{
	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;
	
	if (btcoexist->board_info.btdm_ant_num == 2)
		ex_halbtc8723b2ant_display_coex_info(btcoexist);
	else if (btcoexist->board_info.btdm_ant_num == 1)
		ex_halbtc8723b1ant_display_coex_info(btcoexist);
}

#endif

