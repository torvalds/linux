//============================================================
// Description:
//
// This file is for 8812a1ant Co-exist mechanism
//
// History
// 2012/11/15 Cosa first check in.
//
//============================================================

//============================================================
// include files
//============================================================
#include "halbt_precomp.h"
#if 1
//============================================================
// Global variables, these are static variables
//============================================================
static COEX_DM_8812A_1ANT		GLCoexDm8812a1Ant;
static PCOEX_DM_8812A_1ANT 	coex_dm=&GLCoexDm8812a1Ant;
static COEX_STA_8812A_1ANT		GLCoexSta8812a1Ant;
static PCOEX_STA_8812A_1ANT	coex_sta=&GLCoexSta8812a1Ant;

const char *const GLBtInfoSrc8812a1Ant[]={
	"BT Info[wifi fw]",
	"BT Info[bt rsp]",
	"BT Info[bt auto report]",
};

//============================================================
// local function proto type if needed
//============================================================
//============================================================
// local function start with halbtc8812a1ant_
//============================================================
#if 0
void
halbtc8812a1ant_Reg0x550Bit3(
	 	PBTC_COEXIST		btcoexist,
	 	BOOLEAN			bSet
	)
{
	u1Byte	u1tmp=0;
	
	u1tmp = btcoexist->btc_read_1byte(btcoexist, 0x550);
	if(bSet)
	{
		u1tmp |= BIT3;
	}
	else
	{
		u1tmp &= ~BIT3;
	}
	btcoexist->btc_write_1byte(btcoexist, 0x550, u1tmp);
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], set 0x550[3]=%d\n", (bSet? 1:0)));
}
#endif
u1Byte
halbtc8812a1ant_BtRssiState(
	u1Byte			level_num,
	u1Byte			rssi_thresh,
	u1Byte			rssi_thresh1
	)
{
	s4Byte			bt_rssi=0;
	u1Byte			bt_rssi_state;

	bt_rssi = coex_sta->bt_rssi;

	if(level_num == 2)
	{			
		if( (coex_sta->pre_bt_rssi_state == BTC_RSSI_STATE_LOW) ||
			(coex_sta->pre_bt_rssi_state == BTC_RSSI_STATE_STAY_LOW))
		{
			if(bt_rssi >= (rssi_thresh+BTC_RSSI_COEX_THRESH_TOL_8812A_1ANT))
			{
				bt_rssi_state = BTC_RSSI_STATE_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE, ("[BTCoex], BT Rssi state switch to High\n"));
			}
			else
			{
				bt_rssi_state = BTC_RSSI_STATE_STAY_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE, ("[BTCoex], BT Rssi state stay at Low\n"));
			}
		}
		else
		{
			if(bt_rssi < rssi_thresh)
			{
				bt_rssi_state = BTC_RSSI_STATE_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE, ("[BTCoex], BT Rssi state switch to Low\n"));
			}
			else
			{
				bt_rssi_state = BTC_RSSI_STATE_STAY_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE, ("[BTCoex], BT Rssi state stay at High\n"));
			}
		}
	}
	else if(level_num == 3)
	{
		if(rssi_thresh > rssi_thresh1)
		{
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE, ("[BTCoex], BT Rssi thresh error!!\n"));
			return coex_sta->pre_bt_rssi_state;
		}
		
		if( (coex_sta->pre_bt_rssi_state == BTC_RSSI_STATE_LOW) ||
			(coex_sta->pre_bt_rssi_state == BTC_RSSI_STATE_STAY_LOW))
		{
			if(bt_rssi >= (rssi_thresh+BTC_RSSI_COEX_THRESH_TOL_8812A_1ANT))
			{
				bt_rssi_state = BTC_RSSI_STATE_MEDIUM;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE, ("[BTCoex], BT Rssi state switch to Medium\n"));
			}
			else
			{
				bt_rssi_state = BTC_RSSI_STATE_STAY_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE, ("[BTCoex], BT Rssi state stay at Low\n"));
			}
		}
		else if( (coex_sta->pre_bt_rssi_state == BTC_RSSI_STATE_MEDIUM) ||
			(coex_sta->pre_bt_rssi_state == BTC_RSSI_STATE_STAY_MEDIUM))
		{
			if(bt_rssi >= (rssi_thresh1+BTC_RSSI_COEX_THRESH_TOL_8812A_1ANT))
			{
				bt_rssi_state = BTC_RSSI_STATE_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE, ("[BTCoex], BT Rssi state switch to High\n"));
			}
			else if(bt_rssi < rssi_thresh)
			{
				bt_rssi_state = BTC_RSSI_STATE_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE, ("[BTCoex], BT Rssi state switch to Low\n"));
			}
			else
			{
				bt_rssi_state = BTC_RSSI_STATE_STAY_MEDIUM;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE, ("[BTCoex], BT Rssi state stay at Medium\n"));
			}
		}
		else
		{
			if(bt_rssi < rssi_thresh1)
			{
				bt_rssi_state = BTC_RSSI_STATE_MEDIUM;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE, ("[BTCoex], BT Rssi state switch to Medium\n"));
			}
			else
			{
				bt_rssi_state = BTC_RSSI_STATE_STAY_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE, ("[BTCoex], BT Rssi state stay at High\n"));
			}
		}
	}
		
	coex_sta->pre_bt_rssi_state = bt_rssi_state;

	return bt_rssi_state;
}

u1Byte
halbtc8812a1ant_WifiRssiState(
	 	PBTC_COEXIST		btcoexist,
	 	u1Byte			index,
	 	u1Byte			level_num,
	 	u1Byte			rssi_thresh,
	 	u1Byte			rssi_thresh1
	)
{
	s4Byte			wifi_rssi=0;
	u1Byte			wifi_rssi_state;

	btcoexist->btc_get(btcoexist, BTC_GET_S4_WIFI_RSSI, &wifi_rssi);
	
	if(level_num == 2)
	{
		if( (coex_sta->pre_wifi_rssi_state[index] == BTC_RSSI_STATE_LOW) ||
			(coex_sta->pre_wifi_rssi_state[index] == BTC_RSSI_STATE_STAY_LOW))
		{
			if(wifi_rssi >= (rssi_thresh+BTC_RSSI_COEX_THRESH_TOL_8812A_1ANT))
			{
				wifi_rssi_state = BTC_RSSI_STATE_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE, ("[BTCoex], wifi RSSI state switch to High\n"));
			}
			else
			{
				wifi_rssi_state = BTC_RSSI_STATE_STAY_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE, ("[BTCoex], wifi RSSI state stay at Low\n"));
			}
		}
		else
		{
			if(wifi_rssi < rssi_thresh)
			{
				wifi_rssi_state = BTC_RSSI_STATE_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE, ("[BTCoex], wifi RSSI state switch to Low\n"));
			}
			else
			{
				wifi_rssi_state = BTC_RSSI_STATE_STAY_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE, ("[BTCoex], wifi RSSI state stay at High\n"));
			}
		}
	}
	else if(level_num == 3)
	{
		if(rssi_thresh > rssi_thresh1)
		{
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE, ("[BTCoex], wifi RSSI thresh error!!\n"));
			return coex_sta->pre_wifi_rssi_state[index];
		}
		
		if( (coex_sta->pre_wifi_rssi_state[index] == BTC_RSSI_STATE_LOW) ||
			(coex_sta->pre_wifi_rssi_state[index] == BTC_RSSI_STATE_STAY_LOW))
		{
			if(wifi_rssi >= (rssi_thresh+BTC_RSSI_COEX_THRESH_TOL_8812A_1ANT))
			{
				wifi_rssi_state = BTC_RSSI_STATE_MEDIUM;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE, ("[BTCoex], wifi RSSI state switch to Medium\n"));
			}
			else
			{
				wifi_rssi_state = BTC_RSSI_STATE_STAY_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE, ("[BTCoex], wifi RSSI state stay at Low\n"));
			}
		}
		else if( (coex_sta->pre_wifi_rssi_state[index] == BTC_RSSI_STATE_MEDIUM) ||
			(coex_sta->pre_wifi_rssi_state[index] == BTC_RSSI_STATE_STAY_MEDIUM))
		{
			if(wifi_rssi >= (rssi_thresh1+BTC_RSSI_COEX_THRESH_TOL_8812A_1ANT))
			{
				wifi_rssi_state = BTC_RSSI_STATE_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE, ("[BTCoex], wifi RSSI state switch to High\n"));
			}
			else if(wifi_rssi < rssi_thresh)
			{
				wifi_rssi_state = BTC_RSSI_STATE_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE, ("[BTCoex], wifi RSSI state switch to Low\n"));
			}
			else
			{
				wifi_rssi_state = BTC_RSSI_STATE_STAY_MEDIUM;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE, ("[BTCoex], wifi RSSI state stay at Medium\n"));
			}
		}
		else
		{
			if(wifi_rssi < rssi_thresh1)
			{
				wifi_rssi_state = BTC_RSSI_STATE_MEDIUM;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE, ("[BTCoex], wifi RSSI state switch to Medium\n"));
			}
			else
			{
				wifi_rssi_state = BTC_RSSI_STATE_STAY_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE, ("[BTCoex], wifi RSSI state stay at High\n"));
			}
		}
	}
		
	coex_sta->pre_wifi_rssi_state[index] = wifi_rssi_state;

	return wifi_rssi_state;
}

void
halbtc8812a1ant_MonitorBtEnableDisable(
	  	PBTC_COEXIST		btcoexist
	)
{
	static BOOLEAN	pre_bt_disabled=false;
	static u4Byte		bt_disable_cnt=0;
	BOOLEAN			bt_active=true, bt_disable_by68=false, bt_disabled=false;
	u4Byte			u4_tmp=0;

	// This function check if bt is disabled

	if(	coex_sta->high_priority_tx == 0 &&
		coex_sta->high_priority_rx == 0 &&
		coex_sta->low_priority_tx == 0 &&
		coex_sta->low_priority_rx == 0)
	{
		bt_active = false;
	}
	if(	coex_sta->high_priority_tx == 0xffff &&
		coex_sta->high_priority_rx == 0xffff &&
		coex_sta->low_priority_tx == 0xffff &&
		coex_sta->low_priority_rx == 0xffff)
	{
		bt_active = false;
	}
	if(bt_active)
	{
		bt_disable_cnt = 0;
		bt_disabled = false;
		btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_DISABLE, &bt_disabled);
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_MONITOR, ("[BTCoex], BT is enabled !!\n"));
	}
	else
	{
		bt_disable_cnt++;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_MONITOR, ("[BTCoex], bt all counters=0, %d times!!\n", 
				bt_disable_cnt));
		if(bt_disable_cnt >= 2 ||bt_disable_by68)
		{
			bt_disabled = true;
			btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_DISABLE, &bt_disabled);
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_MONITOR, ("[BTCoex], BT is disabled !!\n"));
		}
	}
	if(pre_bt_disabled != bt_disabled)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_MONITOR, ("[BTCoex], BT is from %s to %s!!\n", 
			(pre_bt_disabled ? "disabled":"enabled"), 
			(bt_disabled ? "disabled":"enabled")));
		pre_bt_disabled = bt_disabled;
		if(!bt_disabled)
		{
		}
		else
		{
		}
	}
}

void
halbtc8812a1ant_MonitorBtCtr(
	 	PBTC_COEXIST		btcoexist
	)
{
	u4Byte 			reg_hp_tx_rx, reg_lp_tx_rx, u4_tmp;
	u4Byte			reg_hp_tx=0, reg_hp_rx=0, reg_lp_tx=0, reg_lp_rx=0;
	u1Byte			u1_tmp;
	
	reg_hp_tx_rx = 0x770;
	reg_lp_tx_rx = 0x774;

	u4_tmp = btcoexist->btc_read_4byte(btcoexist, reg_hp_tx_rx);
	reg_hp_tx = u4_tmp & bMaskLWord;
	reg_hp_rx = (u4_tmp & bMaskHWord)>>16;

	u4_tmp = btcoexist->btc_read_4byte(btcoexist, reg_lp_tx_rx);
	reg_lp_tx = u4_tmp & bMaskLWord;
	reg_lp_rx = (u4_tmp & bMaskHWord)>>16;
		
	coex_sta->high_priority_tx = reg_hp_tx;
	coex_sta->high_priority_rx = reg_hp_rx;
	coex_sta->low_priority_tx = reg_lp_tx;
	coex_sta->low_priority_rx = reg_lp_rx;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_MONITOR, ("[BTCoex], High Priority Tx/Rx (reg 0x%x)=%x(%d)/%x(%d)\n", 
		reg_hp_tx_rx, reg_hp_tx, reg_hp_tx, reg_hp_rx, reg_hp_rx));
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_MONITOR, ("[BTCoex], Low Priority Tx/Rx (reg 0x%x)=%x(%d)/%x(%d)\n", 
		reg_lp_tx_rx, reg_lp_tx, reg_lp_tx, reg_lp_rx, reg_lp_rx));

	// reset counter
	btcoexist->btc_write_1byte(btcoexist, 0x76e, 0xc);
}

void
halbtc8812a1ant_QueryBtInfo(
	 	PBTC_COEXIST		btcoexist
	)
{	
	u1Byte	dataLen=3;
	u1Byte	buf[5] = {0};
	static	u4Byte	btInfoCnt=0;

	if(!btInfoCnt ||
		(coex_sta->bt_info_c2h_cnt[BT_INFO_SRC_8812A_1ANT_BT_RSP]-btInfoCnt)>2)
	{
		buf[0] = dataLen;
		buf[1] = 0x1;	// polling enable, 1=enable, 0=disable
		buf[2] = 0x2;	// polling time in seconds
		buf[3] = 0x1;	// auto report enable, 1=enable, 0=disable
			
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_CTRL_BT_INFO, (PVOID)&buf[0]);
	}
	btInfoCnt = coex_sta->bt_info_c2h_cnt[BT_INFO_SRC_8812A_1ANT_BT_RSP];
}
u1Byte
halbtc8812a1ant_ActionAlgorithm(
	 	PBTC_COEXIST		btcoexist
	)
{
	PBTC_STACK_INFO		stack_info=&btcoexist->stack_info;
	BOOLEAN				bt_hs_on=false;
	u1Byte				algorithm=BT_8812A_1ANT_COEX_ALGO_UNDEFINED;
	u1Byte				num_of_diff_profile=0;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);
	
	if(!stack_info->bt_link_exist)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], No profile exists!!!\n"));
		return algorithm;
	}

	if(stack_info->sco_exist)
		num_of_diff_profile++;
	if(stack_info->hid_exist)
		num_of_diff_profile++;
	if(stack_info->pan_exist)
		num_of_diff_profile++;
	if(stack_info->a2dp_exist)
		num_of_diff_profile++;
	
	if(num_of_diff_profile == 1)
	{
		if(stack_info->sco_exist)
		{
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], SCO only\n"));
			algorithm = BT_8812A_1ANT_COEX_ALGO_SCO;
		}
		else
		{
			if(stack_info->hid_exist)
			{
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], HID only\n"));
				algorithm = BT_8812A_1ANT_COEX_ALGO_HID;
			}
			else if(stack_info->a2dp_exist)
			{
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], A2DP only\n"));
				algorithm = BT_8812A_1ANT_COEX_ALGO_A2DP;
			}
			else if(stack_info->pan_exist)
			{
				if(bt_hs_on)
				{
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], PAN(HS) only\n"));
					algorithm = BT_8812A_1ANT_COEX_ALGO_PANHS;
				}
				else
				{
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], PAN(EDR) only\n"));
					algorithm = BT_8812A_1ANT_COEX_ALGO_PANEDR;
				}
			}
		}
	}
	else if(num_of_diff_profile == 2)
	{
		if(stack_info->sco_exist)
		{
			if(stack_info->hid_exist)
			{
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], SCO + HID\n"));
				algorithm = BT_8812A_1ANT_COEX_ALGO_HID;
			}
			else if(stack_info->a2dp_exist)
			{
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], SCO + A2DP ==> SCO\n"));
				algorithm = BT_8812A_1ANT_COEX_ALGO_SCO;
			}
			else if(stack_info->pan_exist)
			{
				if(bt_hs_on)
				{
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], SCO + PAN(HS)\n"));
					algorithm = BT_8812A_1ANT_COEX_ALGO_SCO;
				}
				else
				{
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], SCO + PAN(EDR)\n"));
					algorithm = BT_8812A_1ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		}
		else
		{
			if( stack_info->hid_exist &&
				stack_info->a2dp_exist )
			{
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], HID + A2DP\n"));
				algorithm = BT_8812A_1ANT_COEX_ALGO_HID_A2DP;
			}
			else if( stack_info->hid_exist &&
				stack_info->pan_exist )
			{
				if(bt_hs_on)
				{
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], HID + PAN(HS)\n"));
					algorithm = BT_8812A_1ANT_COEX_ALGO_HID_A2DP;
				}
				else
				{
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], HID + PAN(EDR)\n"));
					algorithm = BT_8812A_1ANT_COEX_ALGO_PANEDR_HID;
				}
			}
			else if( stack_info->pan_exist &&
				stack_info->a2dp_exist )
			{
				if(bt_hs_on)
				{
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], A2DP + PAN(HS)\n"));
					algorithm = BT_8812A_1ANT_COEX_ALGO_A2DP_PANHS;
				}
				else
				{
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], A2DP + PAN(EDR)\n"));
					algorithm = BT_8812A_1ANT_COEX_ALGO_PANEDR_A2DP;
				}
			}
		}
	}
	else if(num_of_diff_profile == 3)
	{
		if(stack_info->sco_exist)
		{
			if( stack_info->hid_exist &&
				stack_info->a2dp_exist )
			{
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], SCO + HID + A2DP ==> HID\n"));
				algorithm = BT_8812A_1ANT_COEX_ALGO_HID;
			}
			else if( stack_info->hid_exist &&
				stack_info->pan_exist )
			{
				if(bt_hs_on)
				{
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], SCO + HID + PAN(HS)\n"));
					algorithm = BT_8812A_1ANT_COEX_ALGO_HID_A2DP;
				}
				else
				{
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], SCO + HID + PAN(EDR)\n"));
					algorithm = BT_8812A_1ANT_COEX_ALGO_PANEDR_HID;
				}
			}
			else if( stack_info->pan_exist &&
				stack_info->a2dp_exist )
			{
				if(bt_hs_on)
				{
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], SCO + A2DP + PAN(HS)\n"));
					algorithm = BT_8812A_1ANT_COEX_ALGO_SCO;
				}
				else
				{
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], SCO + A2DP + PAN(EDR) ==> HID\n"));
					algorithm = BT_8812A_1ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		}
		else
		{
			if( stack_info->hid_exist &&
				stack_info->pan_exist &&
				stack_info->a2dp_exist )
			{
				if(bt_hs_on)
				{
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], HID + A2DP + PAN(HS)\n"));
					algorithm = BT_8812A_1ANT_COEX_ALGO_HID_A2DP;
				}
				else
				{
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], HID + A2DP + PAN(EDR)\n"));
					algorithm = BT_8812A_1ANT_COEX_ALGO_HID_A2DP_PANEDR;
				}
			}
		}
	}
	else if(num_of_diff_profile >= 3)
	{
		if(stack_info->sco_exist)
		{
			if( stack_info->hid_exist &&
				stack_info->pan_exist &&
				stack_info->a2dp_exist )
			{
				if(bt_hs_on)
				{
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Error!!! SCO + HID + A2DP + PAN(HS)\n"));

				}
				else
				{
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], SCO + HID + A2DP + PAN(EDR)==>PAN(EDR)+HID\n"));
					algorithm = BT_8812A_1ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		}
	}

	return algorithm;
}

BOOLEAN
halbtc8812a1ant_NeedToDecBtPwr(
	 	PBTC_COEXIST		btcoexist
	)
{
	BOOLEAN		ret=false;
	BOOLEAN		bt_hs_on=false, wifi_connected=false;
	s4Byte		bt_hs_rssi=0;

	if(!btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on))
		return false;
	if(!btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED, &wifi_connected))
		return false;
	if(!btcoexist->btc_get(btcoexist, BTC_GET_S4_HS_RSSI, &bt_hs_rssi))
		return false;

	if(wifi_connected)
	{
		if(bt_hs_on)
		{
			if(bt_hs_rssi > 37)
			{
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW, ("[BTCoex], Need to decrease bt power for HS mode!!\n"));
				ret = true;
			}
		}
		else
		{
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW, ("[BTCoex], Need to decrease bt power for Wifi is connected!!\n"));
			ret = true;
		}
	}
	
	return ret;
}

void
halbtc8812a1ant_SetFwDacSwingLevel(
	 	PBTC_COEXIST		btcoexist,
	 	u1Byte			dac_swing_lvl
	)
{
	u1Byte			h2c_parameter[1] ={0};

	// There are several type of dacswing
	// 0x18/ 0x10/ 0xc/ 0x8/ 0x4/ 0x6
	h2c_parameter[0] = dac_swing_lvl;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC, ("[BTCoex], Set Dac Swing Level=0x%x\n", dac_swing_lvl));
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC, ("[BTCoex], FW write 0x64=0x%x\n", h2c_parameter[0]));

	btcoexist->btc_fill_h2c(btcoexist, 0x64, 1, h2c_parameter);
}

void
halbtc8812a1ant_SetFwDecBtPwr(
	 	PBTC_COEXIST		btcoexist,
	 	BOOLEAN			dec_bt_pwr
	)
{
	u1Byte	dataLen=3;
	u1Byte	buf[5] = {0};

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC, ("[BTCoex], decrease Bt Power : %s\n", 
			(dec_bt_pwr? "Yes!!":"No!!")));

	buf[0] = dataLen;
	buf[1] = 0x3;		// OP_Code
	buf[2] = 0x1;		// OP_Code_Length
	if(dec_bt_pwr)
		buf[3] = 0x1;	// OP_Code_Content
	else
		buf[3] = 0x0;
		
	btcoexist->btc_set(btcoexist, BTC_SET_ACT_CTRL_BT_COEX, (PVOID)&buf[0]);	
}

void
halbtc8812a1ant_DecBtPwr(
	 	PBTC_COEXIST		btcoexist,
	 	BOOLEAN			force_exec,
	 	BOOLEAN			dec_bt_pwr
	)
{
	return;
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW, ("[BTCoex], %s Dec BT power = %s\n",  
		(force_exec? "force to":""), ((dec_bt_pwr)? "ON":"OFF")));
	coex_dm->cur_dec_bt_pwr = dec_bt_pwr;

	if(!force_exec)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], pre_dec_bt_pwr=%d, cur_dec_bt_pwr=%d\n", 
			coex_dm->pre_dec_bt_pwr, coex_dm->cur_dec_bt_pwr));

		if(coex_dm->pre_dec_bt_pwr == coex_dm->cur_dec_bt_pwr) 
			return;
	}
	halbtc8812a1ant_SetFwDecBtPwr(btcoexist, coex_dm->cur_dec_bt_pwr);

	coex_dm->pre_dec_bt_pwr = coex_dm->cur_dec_bt_pwr;
}

void
halbtc8812a1ant_SetFwBtLnaConstrain(
	 	PBTC_COEXIST		btcoexist,
	 	BOOLEAN			bt_lna_cons_on
	)
{
	u1Byte	dataLen=3;
	u1Byte	buf[5] = {0};

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC, ("[BTCoex], set BT LNA Constrain: %s\n", 
		(bt_lna_cons_on? "ON!!":"OFF!!")));

	buf[0] = dataLen;
	buf[1] = 0x2;		// OP_Code
	buf[2] = 0x1;		// OP_Code_Length
	if(bt_lna_cons_on)
		buf[3] = 0x1;	// OP_Code_Content
	else
		buf[3] = 0x0;
		
	btcoexist->btc_set(btcoexist, BTC_SET_ACT_CTRL_BT_COEX, (PVOID)&buf[0]);	
}

void
halbtc8812a1ant_SetBtLnaConstrain(
	 	PBTC_COEXIST		btcoexist,
	 	BOOLEAN			force_exec,
	 	BOOLEAN			bt_lna_cons_on
	)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW, ("[BTCoex], %s BT Constrain = %s\n",  
		(force_exec? "force":""), ((bt_lna_cons_on)? "ON":"OFF")));
	coex_dm->bCurBtLnaConstrain = bt_lna_cons_on;

	if(!force_exec)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], bPreBtLnaConstrain=%d, bCurBtLnaConstrain=%d\n", 
			coex_dm->bPreBtLnaConstrain, coex_dm->bCurBtLnaConstrain));

		if(coex_dm->bPreBtLnaConstrain == coex_dm->bCurBtLnaConstrain) 
			return;
	}
	halbtc8812a1ant_SetFwBtLnaConstrain(btcoexist, coex_dm->bCurBtLnaConstrain);

	coex_dm->bPreBtLnaConstrain = coex_dm->bCurBtLnaConstrain;
}

void
halbtc8812a1ant_SetFwBtPsdMode(
	 	PBTC_COEXIST		btcoexist,
	 	u1Byte			bt_psd_mode
	)
{
	u1Byte	dataLen=3;
	u1Byte	buf[5] = {0};

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC, ("[BTCoex], set BT PSD mode=0x%x\n", 
		bt_psd_mode));

	buf[0] = dataLen;
	buf[1] = 0x4;			// OP_Code
	buf[2] = 0x1;			// OP_Code_Length
	buf[3] = bt_psd_mode;	// OP_Code_Content
		
	btcoexist->btc_set(btcoexist, BTC_SET_ACT_CTRL_BT_COEX, (PVOID)&buf[0]);	
}


void
halbtc8812a1ant_SetBtPsdMode(
	 	PBTC_COEXIST		btcoexist,
	 	BOOLEAN			force_exec,
	 	u1Byte			bt_psd_mode
	)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW, ("[BTCoex], %s BT PSD mode = 0x%x\n",  
		(force_exec? "force":""), bt_psd_mode));
	coex_dm->bCurBtPsdMode = bt_psd_mode;

	if(!force_exec)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], bPreBtPsdMode=0x%x, bCurBtPsdMode=0x%x\n", 
			coex_dm->bPreBtPsdMode, coex_dm->bCurBtPsdMode));

		if(coex_dm->bPreBtPsdMode == coex_dm->bCurBtPsdMode) 
			return;
	}
	halbtc8812a1ant_SetFwBtPsdMode(btcoexist, coex_dm->bCurBtPsdMode);

	coex_dm->bPreBtPsdMode = coex_dm->bCurBtPsdMode;
}


void
halbtc8812a1ant_SetBtAutoReport(
	 	PBTC_COEXIST		btcoexist,
	 	BOOLEAN			enable_auto_report
	)
{
#if 0
	u1Byte			h2c_parameter[1] ={0};
	
	h2c_parameter[0] = 0;

	if(enable_auto_report)
	{
		h2c_parameter[0] |= BIT0;
	}

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC, ("[BTCoex], BT FW auto report : %s, FW write 0x68=0x%x\n", 
		(enable_auto_report? "Enabled!!":"Disabled!!"), h2c_parameter[0]));

	btcoexist->btc_fill_h2c(btcoexist, 0x68, 1, h2c_parameter);	
#else

#endif
}

void
halbtc8812a1ant_BtAutoReport(
	 	PBTC_COEXIST		btcoexist,
	 	BOOLEAN			force_exec,
	 	BOOLEAN			enable_auto_report
	)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW, ("[BTCoex], %s BT Auto report = %s\n",  
		(force_exec? "force to":""), ((enable_auto_report)? "Enabled":"Disabled")));
	coex_dm->cur_bt_auto_report = enable_auto_report;

	if(!force_exec)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], pre_bt_auto_report=%d, cur_bt_auto_report=%d\n", 
			coex_dm->pre_bt_auto_report, coex_dm->cur_bt_auto_report));

		if(coex_dm->pre_bt_auto_report == coex_dm->cur_bt_auto_report) 
			return;
	}
	halbtc8812a1ant_SetBtAutoReport(btcoexist, coex_dm->cur_bt_auto_report);

	coex_dm->pre_bt_auto_report = coex_dm->cur_bt_auto_report;
}

void
halbtc8812a1ant_FwDacSwingLvl(
	 	PBTC_COEXIST		btcoexist,
	 	BOOLEAN			force_exec,
	 	u1Byte			fw_dac_swing_lvl
	)
{
	return;
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW, ("[BTCoex], %s set FW Dac Swing level = %d\n",  
		(force_exec? "force to":""), fw_dac_swing_lvl));
	coex_dm->cur_fw_dac_swing_lvl = fw_dac_swing_lvl;

	if(!force_exec)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], pre_fw_dac_swing_lvl=%d, cur_fw_dac_swing_lvl=%d\n", 
			coex_dm->pre_fw_dac_swing_lvl, coex_dm->cur_fw_dac_swing_lvl));

		if(coex_dm->pre_fw_dac_swing_lvl == coex_dm->cur_fw_dac_swing_lvl) 
			return;
	}

	halbtc8812a1ant_SetFwDacSwingLevel(btcoexist, coex_dm->cur_fw_dac_swing_lvl);

	coex_dm->pre_fw_dac_swing_lvl = coex_dm->cur_fw_dac_swing_lvl;
}

void
halbtc8812a1ant_SetSwRfRxLpfCorner(
	 	PBTC_COEXIST		btcoexist,
	 	BOOLEAN			rx_rf_shrink_on
	)
{
	if(rx_rf_shrink_on)
	{
		//Shrink RF Rx LPF corner
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], Shrink RF Rx LPF corner!!\n"));
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1e, 0xfffff, 0xf0ff7);
	}
	else
	{
		//Resume RF Rx LPF corner
		// After initialized, we can use coex_dm->bt_rf0x1e_backup
		if(btcoexist->bInitilized)
		{
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], Resume RF Rx LPF corner!!\n"));
			btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1e, 0xfffff, coex_dm->bt_rf0x1e_backup);
		}
	}
}

void
halbtc8812a1ant_RfShrink(
	 	PBTC_COEXIST		btcoexist,
	 	BOOLEAN			force_exec,
	 	BOOLEAN			rx_rf_shrink_on
	)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW, ("[BTCoex], %s turn Rx RF Shrink = %s\n",  
		(force_exec? "force to":""), ((rx_rf_shrink_on)? "ON":"OFF")));
	coex_dm->cur_rf_rx_lpf_shrink = rx_rf_shrink_on;

	if(!force_exec)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_DETAIL, ("[BTCoex], pre_rf_rx_lpf_shrink=%d, cur_rf_rx_lpf_shrink=%d\n", 
			coex_dm->pre_rf_rx_lpf_shrink, coex_dm->cur_rf_rx_lpf_shrink));

		if(coex_dm->pre_rf_rx_lpf_shrink == coex_dm->cur_rf_rx_lpf_shrink) 
			return;
	}
	halbtc8812a1ant_SetSwRfRxLpfCorner(btcoexist, coex_dm->cur_rf_rx_lpf_shrink);

	coex_dm->pre_rf_rx_lpf_shrink = coex_dm->cur_rf_rx_lpf_shrink;
}

void
halbtc8812a1ant_SetSwPenaltyTxRateAdaptive(
	 	PBTC_COEXIST		btcoexist,
	 	BOOLEAN			low_penalty_ra
	)
{
	u1Byte	u1_tmp;

	u1_tmp = btcoexist->btc_read_1byte(btcoexist, 0x4fd);
	u1_tmp |= BIT0;
	if(low_penalty_ra)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], Tx rate adaptive, set low penalty!!\n"));
		u1_tmp &= ~BIT2;
	}
	else
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], Tx rate adaptive, set normal!!\n"));
		u1_tmp |= BIT2;
	}

	btcoexist->btc_write_1byte(btcoexist, 0x4fd, u1_tmp);
}

void
halbtc8812a1ant_LowPenaltyRa(
	 	PBTC_COEXIST		btcoexist,
	 	BOOLEAN			force_exec,
	 	BOOLEAN			low_penalty_ra
	)
{
	return;
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW, ("[BTCoex], %s turn LowPenaltyRA = %s\n",  
		(force_exec? "force to":""), ((low_penalty_ra)? "ON":"OFF")));
	coex_dm->cur_low_penalty_ra = low_penalty_ra;

	if(!force_exec)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_DETAIL, ("[BTCoex], pre_low_penalty_ra=%d, cur_low_penalty_ra=%d\n", 
			coex_dm->pre_low_penalty_ra, coex_dm->cur_low_penalty_ra));

		if(coex_dm->pre_low_penalty_ra == coex_dm->cur_low_penalty_ra) 
			return;
	}
	halbtc8812a1ant_SetSwPenaltyTxRateAdaptive(btcoexist, coex_dm->cur_low_penalty_ra);

	coex_dm->pre_low_penalty_ra = coex_dm->cur_low_penalty_ra;
}

void
halbtc8812a1ant_SetDacSwingReg(
	 	PBTC_COEXIST		btcoexist,
	 	u4Byte			level
	)
{
	u1Byte	val=(u1Byte)level;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], Write SwDacSwing = 0x%x\n", level));
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0xc5b, 0x3e, val);
}

void
halbtc8812a1ant_SetSwFullTimeDacSwing(
	 	PBTC_COEXIST		btcoexist,
	 	BOOLEAN			sw_dac_swing_on,
	 	u4Byte			sw_dac_swing_lvl
	)
{
	if(sw_dac_swing_on)
	{
		halbtc8812a1ant_SetDacSwingReg(btcoexist, sw_dac_swing_lvl);
	}
	else
	{
		halbtc8812a1ant_SetDacSwingReg(btcoexist, 0x18);
	}
}


void
halbtc8812a1ant_DacSwing(
	 	PBTC_COEXIST		btcoexist,
	 	BOOLEAN			force_exec,
	 	BOOLEAN			dac_swing_on,
	 	u4Byte			dac_swing_lvl
	)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW, ("[BTCoex], %s turn DacSwing=%s, dac_swing_lvl=0x%x\n",  
		(force_exec? "force to":""), ((dac_swing_on)? "ON":"OFF"), dac_swing_lvl));
	coex_dm->cur_dac_swing_on = dac_swing_on;
	coex_dm->cur_dac_swing_lvl = dac_swing_lvl;

	if(!force_exec)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_DETAIL, ("[BTCoex], pre_dac_swing_on=%d, pre_dac_swing_lvl=0x%x, cur_dac_swing_on=%d, cur_dac_swing_lvl=0x%x\n", 
			coex_dm->pre_dac_swing_on, coex_dm->pre_dac_swing_lvl,
			coex_dm->cur_dac_swing_on, coex_dm->cur_dac_swing_lvl));

		if( (coex_dm->pre_dac_swing_on == coex_dm->cur_dac_swing_on) &&
			(coex_dm->pre_dac_swing_lvl == coex_dm->cur_dac_swing_lvl) )
			return;
	}
	delay_ms(30);
	halbtc8812a1ant_SetSwFullTimeDacSwing(btcoexist, dac_swing_on, dac_swing_lvl);

	coex_dm->pre_dac_swing_on = coex_dm->cur_dac_swing_on;
	coex_dm->pre_dac_swing_lvl = coex_dm->cur_dac_swing_lvl;
}

void
halbtc8812a1ant_SetAdcBackOff(
	 	PBTC_COEXIST		btcoexist,
	 	BOOLEAN			adc_back_off
	)
{
	if(adc_back_off)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], BB BackOff Level On!\n"));
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x8db, 0x60, 0x3);
	}
	else
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], BB BackOff Level Off!\n"));
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x8db, 0x60, 0x1);
	}
}

void
halbtc8812a1ant_AdcBackOff(
	 	PBTC_COEXIST		btcoexist,
	 	BOOLEAN			force_exec,
	 	BOOLEAN			adc_back_off
	)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW, ("[BTCoex], %s turn AdcBackOff = %s\n",  
		(force_exec? "force to":""), ((adc_back_off)? "ON":"OFF")));
	coex_dm->cur_adc_back_off = adc_back_off;

	if(!force_exec)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_DETAIL, ("[BTCoex], pre_adc_back_off=%d, cur_adc_back_off=%d\n", 
			coex_dm->pre_adc_back_off, coex_dm->cur_adc_back_off));

		if(coex_dm->pre_adc_back_off == coex_dm->cur_adc_back_off) 
			return;
	}
	halbtc8812a1ant_SetAdcBackOff(btcoexist, coex_dm->cur_adc_back_off);

	coex_dm->pre_adc_back_off = coex_dm->cur_adc_back_off;
}

void
halbtc8812a1ant_SetAgcTable(
	 	PBTC_COEXIST		btcoexist,
	 	BOOLEAN			agc_table_en
	)
{
	u1Byte		rssi_adjust_val=0;
	
	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0xef, 0xfffff, 0x02000);
	if(agc_table_en)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], Agc Table On!\n"));
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x3b, 0xfffff, 0x3fa58);
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x3b, 0xfffff, 0x37a58);
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x3b, 0xfffff, 0x2fa58);
		rssi_adjust_val = 8;
	}
	else
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], Agc Table Off!\n"));
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x3b, 0xfffff, 0x39258);
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x3b, 0xfffff, 0x31258);
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x3b, 0xfffff, 0x29258);
	}
	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0xef, 0xfffff, 0x0);

	// set rssi_adjust_val for wifi module.
	btcoexist->btc_set(btcoexist, BTC_SET_U1_RSSI_ADJ_VAL_FOR_AGC_TABLE_ON, &rssi_adjust_val);
}


void
halbtc8812a1ant_AgcTable(
	 	PBTC_COEXIST		btcoexist,
	 	BOOLEAN			force_exec,
	 	BOOLEAN			agc_table_en
	)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW, ("[BTCoex], %s %s Agc Table\n",  
		(force_exec? "force to":""), ((agc_table_en)? "Enable":"Disable")));
	coex_dm->cur_agc_table_en = agc_table_en;

	if(!force_exec)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_DETAIL, ("[BTCoex], pre_agc_table_en=%d, cur_agc_table_en=%d\n", 
			coex_dm->pre_agc_table_en, coex_dm->cur_agc_table_en));

		if(coex_dm->pre_agc_table_en == coex_dm->cur_agc_table_en) 
			return;
	}
	halbtc8812a1ant_SetAgcTable(btcoexist, agc_table_en);

	coex_dm->pre_agc_table_en = coex_dm->cur_agc_table_en;
}

void
halbtc8812a1ant_SetCoexTable(
	 	PBTC_COEXIST	btcoexist,
	 	u4Byte		val0x6c0,
	 	u4Byte		val0x6c4,
	 	u4Byte		val0x6c8,
	 	u1Byte		val0x6cc
	)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], set coex table, set 0x6c0=0x%x\n", val0x6c0));
	btcoexist->btc_write_4byte(btcoexist, 0x6c0, val0x6c0);

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], set coex table, set 0x6c4=0x%x\n", val0x6c4));
	btcoexist->btc_write_4byte(btcoexist, 0x6c4, val0x6c4);

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], set coex table, set 0x6c8=0x%x\n", val0x6c8));
	btcoexist->btc_write_4byte(btcoexist, 0x6c8, val0x6c8);

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], set coex table, set 0x6cc=0x%x\n", val0x6cc));
	btcoexist->btc_write_1byte(btcoexist, 0x6cc, val0x6cc);
}

void
halbtc8812a1ant_CoexTable(
	 	PBTC_COEXIST		btcoexist,
	 	BOOLEAN			force_exec,
	 	u4Byte			val0x6c0,
	 	u4Byte			val0x6c4,
	 	u4Byte			val0x6c8,
	 	u1Byte			val0x6cc
	)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW, ("[BTCoex], %s write Coex Table 0x6c0=0x%x, 0x6c4=0x%x, 0x6c8=0x%x, 0x6cc=0x%x\n", 
		(force_exec? "force to":""), val0x6c0, val0x6c4, val0x6c8, val0x6cc));
	coex_dm->cur_val0x6c0 = val0x6c0;
	coex_dm->cur_val0x6c4 = val0x6c4;
	coex_dm->cur_val0x6c8 = val0x6c8;
	coex_dm->cur_val0x6cc = val0x6cc;

	if(!force_exec)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_DETAIL, ("[BTCoex], pre_val0x6c0=0x%x, pre_val0x6c4=0x%x, pre_val0x6c8=0x%x, pre_val0x6cc=0x%x !!\n", 
			coex_dm->pre_val0x6c0, coex_dm->pre_val0x6c4, coex_dm->pre_val0x6c8, coex_dm->pre_val0x6cc));
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_DETAIL, ("[BTCoex], cur_val0x6c0=0x%x, cur_val0x6c4=0x%x, cur_val0x6c8=0x%x, cur_val0x6cc=0x%x !!\n", 
			coex_dm->cur_val0x6c0, coex_dm->cur_val0x6c4, coex_dm->cur_val0x6c8, coex_dm->cur_val0x6cc));
	
		if( (coex_dm->pre_val0x6c0 == coex_dm->cur_val0x6c0) &&
			(coex_dm->pre_val0x6c4 == coex_dm->cur_val0x6c4) &&
			(coex_dm->pre_val0x6c8 == coex_dm->cur_val0x6c8) &&
			(coex_dm->pre_val0x6cc == coex_dm->cur_val0x6cc) )
			return;
	}
	halbtc8812a1ant_SetCoexTable(btcoexist, val0x6c0, val0x6c4, val0x6c8, val0x6cc);

	coex_dm->pre_val0x6c0 = coex_dm->cur_val0x6c0;
	coex_dm->pre_val0x6c4 = coex_dm->cur_val0x6c4;
	coex_dm->pre_val0x6c8 = coex_dm->cur_val0x6c8;
	coex_dm->pre_val0x6cc = coex_dm->cur_val0x6cc;
}

void
halbtc8812a1ant_SetFwIgnoreWlanAct(
	 	PBTC_COEXIST		btcoexist,
	 	BOOLEAN			enable
	)
{
	u1Byte	dataLen=3;
	u1Byte	buf[5] = {0};

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC, ("[BTCoex], %s BT Ignore Wlan_Act\n",
		(enable? "Enable":"Disable")));

	buf[0] = dataLen;
	buf[1] = 0x1;			// OP_Code
	buf[2] = 0x1;			// OP_Code_Length
	if(enable)
		buf[3] = 0x1; 		// OP_Code_Content
	else
		buf[3] = 0x0;
		
	btcoexist->btc_set(btcoexist, BTC_SET_ACT_CTRL_BT_COEX, (PVOID)&buf[0]);	
}

void
halbtc8812a1ant_IgnoreWlanAct(
	 	PBTC_COEXIST		btcoexist,
	 	BOOLEAN			force_exec,
	 	BOOLEAN			enable
	)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW, ("[BTCoex], %s turn Ignore WlanAct %s\n", 
		(force_exec? "force to":""), (enable? "ON":"OFF")));
	coex_dm->cur_ignore_wlan_act = enable;

	if(!force_exec)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], pre_ignore_wlan_act = %d, cur_ignore_wlan_act = %d!!\n", 
			coex_dm->pre_ignore_wlan_act, coex_dm->cur_ignore_wlan_act));

		if(coex_dm->pre_ignore_wlan_act == coex_dm->cur_ignore_wlan_act)
			return;
	}
	halbtc8812a1ant_SetFwIgnoreWlanAct(btcoexist, enable);

	coex_dm->pre_ignore_wlan_act = coex_dm->cur_ignore_wlan_act;
}

void
halbtc8812a1ant_SetFwPstdma(
	 	PBTC_COEXIST		btcoexist,
	 	u1Byte			byte1,
	 	u1Byte			byte2,
	 	u1Byte			byte3,
	 	u1Byte			byte4,
	 	u1Byte			byte5
	)
{
	u1Byte			h2c_parameter[5] ={0};

	h2c_parameter[0] = byte1;	
	h2c_parameter[1] = byte2;	
	h2c_parameter[2] = byte3;
	h2c_parameter[3] = byte4;
	h2c_parameter[4] = byte5;

	coex_dm->ps_tdma_para[0] = byte1;
	coex_dm->ps_tdma_para[1] = byte2;
	coex_dm->ps_tdma_para[2] = byte3;
	coex_dm->ps_tdma_para[3] = byte4;
	coex_dm->ps_tdma_para[4] = byte5;
	
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC, ("[BTCoex], FW write 0x60(5bytes)=0x%x%08x\n", 
		h2c_parameter[0], 
		h2c_parameter[1]<<24|h2c_parameter[2]<<16|h2c_parameter[3]<<8|h2c_parameter[4]));

	btcoexist->btc_fill_h2c(btcoexist, 0x60, 5, h2c_parameter);
}

void
halbtc8812a1ant_SetLpsRpwm(
	 	PBTC_COEXIST		btcoexist,
	 	u1Byte			lps_val,
	 	u1Byte			rpwm_val
	)
{
	u1Byte	lps=lps_val;
	u1Byte	rpwm=rpwm_val;
	
	btcoexist->btc_set(btcoexist, BTC_SET_U1_1ANT_LPS, &lps);
	btcoexist->btc_set(btcoexist, BTC_SET_U1_1ANT_RPWM, &rpwm);
	
	btcoexist->btc_set(btcoexist, BTC_SET_ACT_INC_FORCE_EXEC_PWR_CMD_CNT, NULL);
}

void
halbtc8812a1ant_LpsRpwm(
	 	PBTC_COEXIST		btcoexist,
	 	BOOLEAN			force_exec,
	 	u1Byte			lps_val,
	 	u1Byte			rpwm_val
	)
{
	BOOLEAN	bForceExecPwrCmd=false;
	
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW, ("[BTCoex], %s set lps/rpwm=0x%x/0x%x \n", 
		(force_exec? "force to":""), lps_val, rpwm_val));
	coex_dm->cur_lps = lps_val;
	coex_dm->cur_rpwm = rpwm_val;

	if(!force_exec)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], pre_lps/cur_lps=0x%x/0x%x, pre_rpwm/cur_rpwm=0x%x/0x%x!!\n", 
			coex_dm->pre_lps, coex_dm->cur_lps, coex_dm->pre_rpwm, coex_dm->cur_rpwm));

		if( (coex_dm->pre_lps == coex_dm->cur_lps) &&
			(coex_dm->pre_rpwm == coex_dm->cur_rpwm) )
		{
			return;
		}
	}
	halbtc8812a1ant_SetLpsRpwm(btcoexist, lps_val, rpwm_val);

	coex_dm->pre_lps = coex_dm->cur_lps;
	coex_dm->pre_rpwm = coex_dm->cur_rpwm;
}

void
halbtc8812a1ant_SwMechanism1(
	 	PBTC_COEXIST	btcoexist,	
	 	BOOLEAN		shrink_rx_lpf,
	 	BOOLEAN 	low_penalty_ra,
	 	BOOLEAN		limited_dig, 
	 	BOOLEAN		bt_lna_constrain
	) 
{
	//halbtc8812a1ant_RfShrink(btcoexist, NORMAL_EXEC, shrink_rx_lpf);
	//halbtc8812a1ant_LowPenaltyRa(btcoexist, NORMAL_EXEC, low_penalty_ra);

	//no limited DIG
	//halbtc8812a1ant_SetBtLnaConstrain(btcoexist, NORMAL_EXEC, bt_lna_constrain);
}

void
halbtc8812a1ant_SwMechanism2(
	 	PBTC_COEXIST	btcoexist,	
	 	BOOLEAN		agc_table_shift,
	 	BOOLEAN 	adc_back_off,
	 	BOOLEAN		sw_dac_swing,
	 	u4Byte		dac_swing_lvl
	)
{
	//halbtc8812a1ant_AgcTable(btcoexist, NORMAL_EXEC, agc_table_shift);
	//halbtc8812a1ant_AdcBackOff(btcoexist, NORMAL_EXEC, adc_back_off);
	//halbtc8812a1ant_DacSwing(btcoexist, NORMAL_EXEC, sw_dac_swing, dac_swing_lvl);
}

void
halbtc8812a1ant_PsTdma(
	 	PBTC_COEXIST		btcoexist,
	 	BOOLEAN			force_exec,
	 	BOOLEAN			turn_on,
	 	u1Byte			type
	)
{
	BOOLEAN			bTurnOnByCnt=false;
	u1Byte			psTdmaTypeByCnt=0, rssi_adjust_val=0;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW, ("[BTCoex], %s turn %s PS TDMA, type=%d\n", 
		(force_exec? "force to":""), (turn_on? "ON":"OFF"), type));
	coex_dm->cur_ps_tdma_on = turn_on;
	coex_dm->cur_ps_tdma = type;

	if(!force_exec)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], pre_ps_tdma_on = %d, cur_ps_tdma_on = %d!!\n", 
			coex_dm->pre_ps_tdma_on, coex_dm->cur_ps_tdma_on));
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], pre_ps_tdma = %d, cur_ps_tdma = %d!!\n", 
			coex_dm->pre_ps_tdma, coex_dm->cur_ps_tdma));

		if( (coex_dm->pre_ps_tdma_on == coex_dm->cur_ps_tdma_on) &&
			(coex_dm->pre_ps_tdma == coex_dm->cur_ps_tdma) )
			return;
	}
	if(turn_on)
	{
		switch(type)
		{
			default:
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0xd3, 0x1a, 0x1a, 0x0, 0x58);
				break;
			case 1:
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0xd3, 0x1a, 0x1a, 0x0, 0x48);
				rssi_adjust_val = 11;
				break;
			case 2:
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0xd3, 0x12, 0x12, 0x0, 0x48);
				rssi_adjust_val = 14;
				break;
			case 3:
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0x93, 0x25, 0x3, 0x10, 0x40);
				break;
			case 4:
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0x93, 0x15, 0x3, 0x14, 0x0);
				rssi_adjust_val = 17;
				break;
			case 5:
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0x61, 0x15, 0x3, 0x31, 0x0);
				break;
			case 6:
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0x13, 0xa, 0x3, 0x0, 0x0);
				break;
			case 7:
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0x13, 0xc, 0x5, 0x0, 0x0);
				break;
			case 8:	
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0x93, 0x25, 0x3, 0x10, 0x0);
				break;
			case 9:	
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0xd3, 0xa, 0xa, 0x0, 0x48);
				rssi_adjust_val = 18;
				break;
			case 10:	
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0x13, 0xa, 0xa, 0x0, 0x40);
				break;
			case 11:	
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0xd3, 0x5, 0x5, 0x0, 0x48);
				rssi_adjust_val = 20;
				break;
			case 12:
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0xeb, 0xa, 0x3, 0x31, 0x18);
				break;

			case 15:
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0x13, 0xa, 0x3, 0x8, 0x0);
				break;
			case 16:
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0x93, 0x15, 0x3, 0x10, 0x0);
				rssi_adjust_val = 18;
				break;

			case 18:
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0x93, 0x25, 0x3, 0x10, 0x0);
				rssi_adjust_val = 14;
				break;			
				
			case 20:
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0x13, 0x25, 0x25, 0x0, 0x0);
				break;
			case 21:
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0x93, 0x20, 0x3, 0x10, 0x40);
				break;
			case 22:
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0x13, 0x8, 0x8, 0x0, 0x40);
				break;
			case 23:
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0xe3, 0x25, 0x3, 0x31, 0x18);
				rssi_adjust_val = 22;
				break;
			case 24:
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0xe3, 0x15, 0x3, 0x31, 0x18);
				rssi_adjust_val = 22;
				break;
			case 25:
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0xe3, 0xa, 0x3, 0x31, 0x18);
				rssi_adjust_val = 22;
				break;
			case 26:
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0xe3, 0xa, 0x3, 0x31, 0x18);
				rssi_adjust_val = 22;
				break;
			case 27:
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0xe3, 0x25, 0x3, 0x31, 0x98);
				rssi_adjust_val = 22;
				break;
			case 28:
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0x69, 0x25, 0x3, 0x31, 0x0);
				break;
			case 29:
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0xab, 0x1a, 0x1a, 0x1, 0x8);
				break;
			case 30:
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0x93, 0x15, 0x3, 0x14, 0x0);
				break;
			case 31:
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0xd3, 0x1a, 0x1a, 0, 0x58);
				break;
			case 32:
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0xab, 0xa, 0x3, 0x31, 0x88);
				break;
			case 33:
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0xa3, 0x25, 0x3, 0x30, 0x88);
				break;
			case 34:
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0xd3, 0x1a, 0x1a, 0x0, 0x8);
				break;
			case 35:
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0xe3, 0x1a, 0x1a, 0x0, 0x8);
				break;
			case 36:
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0xd3, 0x12, 0x3, 0x14, 0x58);
				break;
		}
	}
	else
	{
		// disable PS tdma
		switch(type)
		{
			case 8:
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0x8, 0x0, 0x0, 0x0, 0x0);
				btcoexist->btc_write_1byte(btcoexist, 0x92c, 0x4);
				break;
			case 0:
			default:
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0x0, 0x0, 0x0, 0x0, 0x0);
				delay_ms(5);
				btcoexist->btc_write_1byte(btcoexist, 0x92c, 0x20);
				break;
			case 9:
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0x0, 0x0, 0x0, 0x0, 0x0);
				btcoexist->btc_write_1byte(btcoexist, 0x92c, 0x4);
				break;
			case 10:
				halbtc8812a1ant_SetFwPstdma(btcoexist, 0x0, 0x0, 0x0, 0x8, 0x0);
				delay_ms(5);
				btcoexist->btc_write_1byte(btcoexist, 0x92c, 0x20);
				break;
		}
	}
	rssi_adjust_val =0;
	btcoexist->btc_set(btcoexist, BTC_SET_U1_RSSI_ADJ_VAL_FOR_1ANT_COEX_TYPE, &rssi_adjust_val);

	// update pre state
	coex_dm->pre_ps_tdma_on = coex_dm->cur_ps_tdma_on;
	coex_dm->pre_ps_tdma = coex_dm->cur_ps_tdma;
}

void
halbtc8812a1ant_CoexAllOff(
	 	PBTC_COEXIST		btcoexist
	)
{
	// fw all off
	halbtc8812a1ant_FwDacSwingLvl(btcoexist, NORMAL_EXEC, 6);
	halbtc8812a1ant_DecBtPwr(btcoexist, NORMAL_EXEC, false);

	// sw all off
	halbtc8812a1ant_SwMechanism1(btcoexist,false,false,false,false);
	halbtc8812a1ant_SwMechanism2(btcoexist,false,false,false,0x18);


	// hw all off
	halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x55555555, 0xffff, 0x3);
}

void
halbtc8812a1ant_WifiParaAdjust(
	 	PBTC_COEXIST		btcoexist,
	 	BOOLEAN			enable
	)
{
	if(enable)
	{
		halbtc8812a1ant_LowPenaltyRa(btcoexist, NORMAL_EXEC, true);
	}
	else
	{
		halbtc8812a1ant_LowPenaltyRa(btcoexist, NORMAL_EXEC, false);
	}
}

BOOLEAN
halbtc8812a1ant_IsCommonAction(
	 	PBTC_COEXIST		btcoexist
	)
{
	BOOLEAN			common=false, wifi_connected=false, wifi_busy=false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED, &wifi_connected);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	//halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x55555555, 0xffff, 0x3);

	if(!wifi_connected && 
		BT_8812A_1ANT_BT_STATUS_NON_CONNECTED_IDLE == coex_dm->bt_status)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Wifi non connected-idle + BT non connected-idle!!\n"));
		halbtc8812a1ant_FwDacSwingLvl(btcoexist, NORMAL_EXEC, 6);
		halbtc8812a1ant_DecBtPwr(btcoexist, NORMAL_EXEC, false);
		
 		halbtc8812a1ant_SwMechanism1(btcoexist,false,false,false,false);
		halbtc8812a1ant_SwMechanism2(btcoexist,false,false,false,0x18);

		common = true;
	}
	else if(wifi_connected && 
		(BT_8812A_1ANT_BT_STATUS_NON_CONNECTED_IDLE == coex_dm->bt_status) )
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Wifi connected + BT non connected-idle!!\n"));
		halbtc8812a1ant_FwDacSwingLvl(btcoexist, NORMAL_EXEC, 6);
		halbtc8812a1ant_DecBtPwr(btcoexist, NORMAL_EXEC, true);

      		halbtc8812a1ant_SwMechanism1(btcoexist,false,false,false,false);
		halbtc8812a1ant_SwMechanism2(btcoexist,false,false,false,0x18);

		common = true;
	}
	else if(!wifi_connected && 
		(BT_8812A_1ANT_BT_STATUS_CONNECTED_IDLE == coex_dm->bt_status) )
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Wifi non connected-idle + BT connected-idle!!\n"));
		halbtc8812a1ant_FwDacSwingLvl(btcoexist, NORMAL_EXEC, 6);
		halbtc8812a1ant_DecBtPwr(btcoexist, NORMAL_EXEC, false);

		halbtc8812a1ant_SwMechanism1(btcoexist,false,false,false,false);
		halbtc8812a1ant_SwMechanism2(btcoexist,false,false,false,0x18);

		common = true;
	}
	else if(wifi_connected && 
		(BT_8812A_1ANT_BT_STATUS_CONNECTED_IDLE == coex_dm->bt_status) )
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Wifi connected + BT connected-idle!!\n"));
		halbtc8812a1ant_FwDacSwingLvl(btcoexist, NORMAL_EXEC, 6);
		halbtc8812a1ant_DecBtPwr(btcoexist, NORMAL_EXEC, false);

		halbtc8812a1ant_SwMechanism1(btcoexist,true,true,true,true);
		halbtc8812a1ant_SwMechanism2(btcoexist,false,false,false,0x18);

		common = true;
	}
	else if(!wifi_connected && 
		(BT_8812A_1ANT_BT_STATUS_CONNECTED_IDLE != coex_dm->bt_status) )
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Wifi non connected-idle + BT Busy!!\n"));
		halbtc8812a1ant_FwDacSwingLvl(btcoexist, NORMAL_EXEC, 6);
		halbtc8812a1ant_DecBtPwr(btcoexist, NORMAL_EXEC, false);

		halbtc8812a1ant_SwMechanism1(btcoexist,false,false,false,false);
		halbtc8812a1ant_SwMechanism2(btcoexist,false,false,false,0x18);
		
		common = true;
	}
	else
	{
		halbtc8812a1ant_SwMechanism1(btcoexist,true,true,true,true);
		
		common = false;
	}
	
	return common;
}


void
halbtc8812a1ant_TdmaDurationAdjustForAcl(
	 	PBTC_COEXIST		btcoexist
	)
{
	static s4Byte		up,dn,m,n,wait_count;
	s4Byte			result;   //0: no change, +1: increase WiFi duration, -1: decrease WiFi duration
	u1Byte			retry_count=0, bt_info_ext;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW, ("[BTCoex], halbtc8812a1ant_TdmaDurationAdjustForAcl()\n"));
	if(coex_dm->reset_tdma_adjust)
	{
		coex_dm->reset_tdma_adjust = false;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], first run TdmaDurationAdjust()!!\n"));

		halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 2);
		coex_dm->ps_tdma_du_adj_type = 2;
		//============
		up = 0;
		dn = 0;
		m = 1;
		n= 3;
		result = 0;
		wait_count = 0;
	}
	else
	{
		//acquire the BT TRx retry count from BT_Info byte2
		retry_count = coex_sta->bt_retry_cnt;
		bt_info_ext = coex_sta->bt_info_ext;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], retry_count = %d\n", retry_count));
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], up=%d, dn=%d, m=%d, n=%d, wait_count=%d\n", 
			up, dn, m, n, wait_count));
		result = 0;
		wait_count++; 
		  
		if(retry_count == 0)  // no retry in the last 2-second duration
		{
			up++;
			dn--;

			if (dn <= 0)
				dn = 0;				 

			if(up >= n)	// Google translated: if consecutive n-2 seconds retry count is 0, width-modulated WiFi duration
			{
				wait_count = 0; 
				n = 3;
				up = 0;
				dn = 0;
				result = 1; 
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], Increase wifi duration!!\n"));
			}
		}
		else if (retry_count <= 3)	// <=3 retry in the last 2-second duration
		{
			up--; 
			dn++;

			if (up <= 0)
				up = 0;

			if (dn == 2)	// Google translated: if 2 consecutive two seconds retry count <3, then tune narrow WiFi duration
			{
				if (wait_count <= 2)
					m++; // Google translated: Avoid been back and forth in the two level
				else
					m = 1;

				if ( m >= 20) // Google translated: m max = 20 'Max 120 seconds recheck whether to adjust WiFi duration.
					m = 20;

				n = 3*m;
				up = 0;
				dn = 0;
				wait_count = 0;
				result = -1; 
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], Decrease wifi duration for retryCounter<3!!\n"));
			}
		}
		else  // Google translated: retry count> 3, as long as a second retry count> 3, then tune narrow WiFi duration
		{
			if (wait_count == 1)
				m++; // Google translated: Avoid been back and forth in the two level 
			else
				m = 1;

			if ( m >= 20) // Google translated: m max = 20 'Max 120 seconds recheck whether to adjust WiFi duration.
				m = 20;

			n = 3*m;
			up = 0;
			dn = 0;
			wait_count = 0; 
			result = -1;
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], Decrease wifi duration for retryCounter>3!!\n"));
		}

		if(result == -1)
		{
			if( (BT_INFO_8812A_1ANT_A2DP_BASIC_RATE(bt_info_ext)) &&
				((coex_dm->cur_ps_tdma == 1) ||(coex_dm->cur_ps_tdma == 2)) )
			{
				halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 9);
				coex_dm->ps_tdma_du_adj_type = 9;
			}
			else if(coex_dm->cur_ps_tdma == 1)
			{
				halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 2);
				coex_dm->ps_tdma_du_adj_type = 2;
			}
			else if(coex_dm->cur_ps_tdma == 2)
			{
				halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 9);
				coex_dm->ps_tdma_du_adj_type = 9;
			}
			else if(coex_dm->cur_ps_tdma == 9)
			{
				halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 11);
				coex_dm->ps_tdma_du_adj_type = 11;
			}
		}
		else if(result == 1)
		{
			if( (BT_INFO_8812A_1ANT_A2DP_BASIC_RATE(bt_info_ext)) &&
				((coex_dm->cur_ps_tdma == 1) ||(coex_dm->cur_ps_tdma == 2)) )
			{
				halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 9);
				coex_dm->ps_tdma_du_adj_type = 9;
			}
			else if(coex_dm->cur_ps_tdma == 11)
			{
				halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 9);
				coex_dm->ps_tdma_du_adj_type = 9;
			}
			else if(coex_dm->cur_ps_tdma == 9)
			{
				halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 2);
				coex_dm->ps_tdma_du_adj_type = 2;
			}
			else if(coex_dm->cur_ps_tdma == 2)
			{
				halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 1);
				coex_dm->ps_tdma_du_adj_type = 1;
			}
		}

		if( coex_dm->cur_ps_tdma != 1 &&
			coex_dm->cur_ps_tdma != 2 &&
			coex_dm->cur_ps_tdma != 9 &&
			coex_dm->cur_ps_tdma != 11 )
		{
			// recover to previous adjust type
			halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, coex_dm->ps_tdma_du_adj_type);
		}
	}
}

u1Byte
halbtc8812a1ant_PsTdmaTypeByWifiRssi(
	 	s4Byte	wifi_rssi,
	 	s4Byte	pre_wifi_rssi,
	 	u1Byte	wifi_rssi_thresh
	)
{
	u1Byte	ps_tdma_type=0;
	
	if(wifi_rssi > pre_wifi_rssi)
	{
		if(wifi_rssi > (wifi_rssi_thresh+5))
		{
			ps_tdma_type = 26;
		}
		else
		{
			ps_tdma_type = 25;
		}
	}
	else
	{
		if(wifi_rssi > wifi_rssi_thresh)
		{
			ps_tdma_type = 26;
		}
		else
		{
			ps_tdma_type = 25;
		}
	}

	return ps_tdma_type;
}

void
halbtc8812a1ant_PsTdmaCheckForPowerSaveState(
	 	PBTC_COEXIST		btcoexist,
	 	BOOLEAN			new_ps_state
	)
{
	u1Byte	lps_mode=0x0;

	btcoexist->btc_get(btcoexist, BTC_GET_U1_LPS_MODE, &lps_mode);
	
	if(lps_mode)	// already under LPS state
	{
		if(new_ps_state)		
		{
			// keep state under LPS, do nothing.
		}
		else
		{
			// will leave LPS state, turn off psTdma first
			halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, false, 0);
		}
	}
	else						// NO PS state
	{
		if(new_ps_state)
		{
			// will enter LPS state, turn off psTdma first
			halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, false, 0);
		}
		else
		{
			// keep state under NO PS state, do nothing.
		}
	}
}

// SCO only or SCO+PAN(HS)
void
halbtc8812a1ant_ActionSco(
	 	PBTC_COEXIST		btcoexist
	)
{
	u1Byte	wifi_rssi_state;
	u4Byte	wifi_bw;

	wifi_rssi_state = halbtc8812a1ant_WifiRssiState(btcoexist, 0, 2, 25, 0);

	halbtc8812a1ant_FwDacSwingLvl(btcoexist, NORMAL_EXEC, 4);

	if(halbtc8812a1ant_NeedToDecBtPwr(btcoexist))
		halbtc8812a1ant_DecBtPwr(btcoexist, NORMAL_EXEC, true);
	else	
		halbtc8812a1ant_DecBtPwr(btcoexist, NORMAL_EXEC, false);
	
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if(BTC_WIFI_BW_HT40 == wifi_bw)
	{
		// sw mechanism
		if( (wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
			(wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH) )
		{
			  halbtc8812a1ant_SwMechanism2(btcoexist,true,true,false,0x18);
		}
		else
		{
			  halbtc8812a1ant_SwMechanism2(btcoexist,false,true,false,0x18);
		}
	}
	else
	{
		// sw mechanism
		if( (wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
			(wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,true,true,false,0x18);
		}
		else
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,false,false,false,0x18);
		}		
	}
}


void
halbtc8812a1ant_ActionHid(
	 	PBTC_COEXIST		btcoexist
	)
{
	u1Byte	wifi_rssi_state, bt_rssi_state;	
	u4Byte	wifi_bw;

	wifi_rssi_state = halbtc8812a1ant_WifiRssiState(btcoexist, 0, 2, 25, 0);
	bt_rssi_state = halbtc8812a1ant_BtRssiState(2, 50, 0);

	halbtc8812a1ant_FwDacSwingLvl(btcoexist, NORMAL_EXEC, 6);

	if(halbtc8812a1ant_NeedToDecBtPwr(btcoexist))
		halbtc8812a1ant_DecBtPwr(btcoexist, NORMAL_EXEC, true);
	else	
		halbtc8812a1ant_DecBtPwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if(BTC_WIFI_BW_HT40 == wifi_bw)
	{
		// sw mechanism
		if( (wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
			(wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,true,false,false,0x18);
		}
		else
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,false,false,false,0x18);
		}
	}
	else
	{
		// sw mechanism
		if( (wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
			(wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,true,true,false,0x18);
		}
		else
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,false,false,false,0x18);
		}		
	}
}

//A2DP only / PAN(EDR) only/ A2DP+PAN(HS)
void
halbtc8812a1ant_ActionA2dp(
	 	PBTC_COEXIST		btcoexist
	)
{
	u1Byte		wifi_rssi_state, bt_rssi_state;
	u4Byte		wifi_bw;

	wifi_rssi_state = halbtc8812a1ant_WifiRssiState(btcoexist, 0, 2, 25, 0);
	bt_rssi_state = halbtc8812a1ant_BtRssiState(2, 50, 0);

	halbtc8812a1ant_FwDacSwingLvl(btcoexist, NORMAL_EXEC, 6);

	if(halbtc8812a1ant_NeedToDecBtPwr(btcoexist))
		halbtc8812a1ant_DecBtPwr(btcoexist, NORMAL_EXEC, true);
	else	
		halbtc8812a1ant_DecBtPwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if(BTC_WIFI_BW_HT40 == wifi_bw)
	{
		// sw mechanism
		if( (wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
			(wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,true,true,false,0x18);
		}
		else
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,false,true,false,0x18);
		}
	}
	else
	{
		// sw mechanism
		if( (wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
			(wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,true,true,false,0x18);
		}
		else
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,false,false,false,0x18);
		}		
	}
}

void
halbtc8812a1ant_ActionA2dpPanHs(
	 	PBTC_COEXIST		btcoexist
	)
{
	u1Byte		wifi_rssi_state, bt_rssi_state, bt_info_ext;
	u4Byte		wifi_bw;

	bt_info_ext = coex_sta->bt_info_ext;
	wifi_rssi_state = halbtc8812a1ant_WifiRssiState(btcoexist, 0, 2, 25, 0);
	bt_rssi_state = halbtc8812a1ant_BtRssiState(2, 50, 0);

	halbtc8812a1ant_FwDacSwingLvl(btcoexist, NORMAL_EXEC, 6);

	if(halbtc8812a1ant_NeedToDecBtPwr(btcoexist))
		halbtc8812a1ant_DecBtPwr(btcoexist, NORMAL_EXEC, true);
	else	
		halbtc8812a1ant_DecBtPwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if(BTC_WIFI_BW_HT40 == wifi_bw)
	{
		// sw mechanism
		if( (wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
			(wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,true,true,false,0x18);
		}
		else
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,false,true,false,0x18);
		}
	}
	else
	{
		// sw mechanism
		if( (wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
			(wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,true,true,false,0x18);
		}
		else
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,false,false,false,0x18);
		}		
	}
}

void
halbtc8812a1ant_ActionPanEdr(
	 	PBTC_COEXIST		btcoexist
	)
{
	u1Byte		wifi_rssi_state, bt_rssi_state;
	u4Byte		wifi_bw;

	wifi_rssi_state = halbtc8812a1ant_WifiRssiState(btcoexist, 0, 2, 25, 0);
	bt_rssi_state = halbtc8812a1ant_BtRssiState(2, 50, 0);

	halbtc8812a1ant_FwDacSwingLvl(btcoexist, NORMAL_EXEC, 6);

	if(halbtc8812a1ant_NeedToDecBtPwr(btcoexist))
		halbtc8812a1ant_DecBtPwr(btcoexist, NORMAL_EXEC, true);
	else	
		halbtc8812a1ant_DecBtPwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if(BTC_WIFI_BW_HT40 == wifi_bw)
	{
		// sw mechanism
		if( (wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
			(wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,true,true,false,0x18);
		}
		else
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,false,true,false,0x18);
		}
	}
	else
	{
		// sw mechanism
		if( (wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
			(wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,true,true,false,0x18);
		}
		else
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,false,false,false,0x18);
		}
	}
}


//PAN(HS) only
void
halbtc8812a1ant_ActionPanHs(
	 	PBTC_COEXIST		btcoexist
	)
{
	u1Byte		wifi_rssi_state, bt_rssi_state;
	u4Byte		wifi_bw;

	wifi_rssi_state = halbtc8812a1ant_WifiRssiState(btcoexist, 0, 2, 25, 0);
	bt_rssi_state = halbtc8812a1ant_BtRssiState(2, 50, 0);

	halbtc8812a1ant_FwDacSwingLvl(btcoexist, NORMAL_EXEC, 6);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if(BTC_WIFI_BW_HT40 == wifi_bw)
	{
		// fw mechanism
		if( (wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
			(wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8812a1ant_DecBtPwr(btcoexist, NORMAL_EXEC, true);
		}
		else
		{
			halbtc8812a1ant_DecBtPwr(btcoexist, NORMAL_EXEC, false);
		}

		// sw mechanism
		if( (wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
			(wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,true,true,false,0x18);
		}
		else
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,false,true,false,0x18);
		}
	}
	else
	{
		// fw mechanism
		if( (wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
			(wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8812a1ant_DecBtPwr(btcoexist, NORMAL_EXEC, true);
		}
		else
		{
			halbtc8812a1ant_DecBtPwr(btcoexist, NORMAL_EXEC, false);
		}

		// sw mechanism
		if( (wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
			(wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,true,true,false,0x18);
		}
		else
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,false,false,false,0x18);
		}
	}
}

//PAN(EDR)+A2DP
void
halbtc8812a1ant_ActionPanEdrA2dp(
	 	PBTC_COEXIST		btcoexist
	)
{
	u1Byte		wifi_rssi_state, bt_rssi_state, bt_info_ext;
	u4Byte		wifi_bw;

	bt_info_ext = coex_sta->bt_info_ext;
	wifi_rssi_state = halbtc8812a1ant_WifiRssiState(btcoexist, 0, 2, 25, 0);
	bt_rssi_state = halbtc8812a1ant_BtRssiState(2, 50, 0);

	halbtc8812a1ant_FwDacSwingLvl(btcoexist, NORMAL_EXEC, 6);

	if(halbtc8812a1ant_NeedToDecBtPwr(btcoexist))
		halbtc8812a1ant_DecBtPwr(btcoexist, NORMAL_EXEC, true);
	else	
		halbtc8812a1ant_DecBtPwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if(BTC_WIFI_BW_HT40 == wifi_bw)
	{
		// sw mechanism
		if( (wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
			(wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,true,true,false,0x18);
		}
		else
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,false,true,false,0x18);
		}
	}
	else
	{
		// sw mechanism
		if( (wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
			(wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,true,true,false,0x18);
		}
		else
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,false,false,false,0x18);
		}
	}
}

void
halbtc8812a1ant_ActionPanEdrHid(
	 	PBTC_COEXIST		btcoexist
	)
{
	u1Byte		wifi_rssi_state, bt_rssi_state;
	u4Byte		wifi_bw;

	wifi_rssi_state = halbtc8812a1ant_WifiRssiState(btcoexist, 0, 2, 25, 0);
	bt_rssi_state = halbtc8812a1ant_BtRssiState(2, 50, 0);

	halbtc8812a1ant_FwDacSwingLvl(btcoexist, NORMAL_EXEC, 6);

	if(halbtc8812a1ant_NeedToDecBtPwr(btcoexist))
		halbtc8812a1ant_DecBtPwr(btcoexist, NORMAL_EXEC, true);
	else	
		halbtc8812a1ant_DecBtPwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if(BTC_WIFI_BW_HT40 == wifi_bw)
	{
		// sw mechanism
		if( (wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
			(wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,true,true,false,0x18);
		}
		else
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,false,true,false,0x18);
		}
	}
	else
	{		
		// sw mechanism
		if( (wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
			(wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,true,true,false,0x18);
		}
		else
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,false,false,false,0x18);
		}
	}
}

// HID+A2DP+PAN(EDR)
void
halbtc8812a1ant_ActionHidA2dpPanEdr(
	 	PBTC_COEXIST		btcoexist
	)
{
	u1Byte		wifi_rssi_state, bt_rssi_state, bt_info_ext;
	u4Byte		wifi_bw;

	bt_info_ext = coex_sta->bt_info_ext;
	wifi_rssi_state = halbtc8812a1ant_WifiRssiState(btcoexist, 0, 2, 25, 0);
	bt_rssi_state = halbtc8812a1ant_BtRssiState(2, 50, 0);

	halbtc8812a1ant_FwDacSwingLvl(btcoexist, NORMAL_EXEC, 6);

	if(halbtc8812a1ant_NeedToDecBtPwr(btcoexist))
		halbtc8812a1ant_DecBtPwr(btcoexist, NORMAL_EXEC, true);
	else	
		halbtc8812a1ant_DecBtPwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if(BTC_WIFI_BW_HT40 == wifi_bw)
	{	
		// sw mechanism
		if( (wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
			(wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,true,true,false,0x18);
		}
		else
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,false,true,false,0x18);
		}
	}
	else
	{
		// sw mechanism
		if( (wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
			(wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,true,true,false,0x18);
		}
		else
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,false,false,false,0x18);
		}
	}
}

void
halbtc8812a1ant_ActionHidA2dp(
	 	PBTC_COEXIST		btcoexist
	)
{
	u1Byte		wifi_rssi_state, bt_rssi_state, bt_info_ext;
	u4Byte		wifi_bw;

	bt_info_ext = coex_sta->bt_info_ext;
	wifi_rssi_state = halbtc8812a1ant_WifiRssiState(btcoexist, 0, 2, 25, 0);
	bt_rssi_state = halbtc8812a1ant_BtRssiState(2, 50, 0);

	if(halbtc8812a1ant_NeedToDecBtPwr(btcoexist))
		halbtc8812a1ant_DecBtPwr(btcoexist, NORMAL_EXEC, true);
	else	
		halbtc8812a1ant_DecBtPwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if(BTC_WIFI_BW_HT40 == wifi_bw)
	{		
		// sw mechanism
		if( (wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
			(wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,true,true,false,0x18);
		}
		else
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,false,true,false,0x18);
		}
	}
	else
	{
		// sw mechanism
		if( (wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
			(wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,true,true,false,0x18);
		}
		else
		{
			halbtc8812a1ant_SwMechanism2(btcoexist,false,false,false,0x18);
		}
	}
}

void
halbtc8812a1ant_ActionHs(
	 	PBTC_COEXIST		btcoexist,
	 	BOOLEAN			hs_connecting
	)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action for HS, hs_connecting=%d!!!\n", hs_connecting));
	halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, false, 8);

	if(hs_connecting)
	{
		halbtc8812a1ant_CoexTable(btcoexist, FORCE_EXEC, 0xaaaaaaaa, 0xaaaaaaaa, 0xffff, 0x3);
	}
	else
	{
		if((coex_sta->high_priority_tx+coex_sta->high_priority_rx+
			coex_sta->low_priority_tx+coex_sta->low_priority_rx)<=1200)
			halbtc8812a1ant_CoexTable(btcoexist, FORCE_EXEC, 0xaaaaaaaa, 0xaaaaaaaa, 0xffff, 0x3);
		else
			halbtc8812a1ant_CoexTable(btcoexist, FORCE_EXEC, 0xffffffff, 0xffffffff, 0xffff, 0x3);	
	}
}


void
halbtc8812a1ant_ActionWifiNotConnected(
	 	PBTC_COEXIST		btcoexist
	)
{
	BOOLEAN		hs_connecting=false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_CONNECTING, &hs_connecting);
	
	halbtc8812a1ant_PsTdmaCheckForPowerSaveState(btcoexist, false);
	btcoexist->btc_set(btcoexist, BTC_SET_ACT_LEAVE_LPS, NULL);

	if(hs_connecting)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], HS is connecting!!!\n"));
		halbtc8812a1ant_ActionHs(btcoexist, hs_connecting);
	}
	else
	{
		halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, false, 8);
		halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x55555555, 0xffff, 0x3);
	}
}

void
halbtc8812a1ant_ActionWifiNotConnectedAssoAuthScan(
	 	PBTC_COEXIST		btcoexist
	)
{
	PBTC_STACK_INFO	stack_info=&btcoexist->stack_info;
	BOOLEAN			hs_connecting=false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_CONNECTING, &hs_connecting);
	halbtc8812a1ant_PsTdmaCheckForPowerSaveState(btcoexist, false);
	btcoexist->btc_set(btcoexist, BTC_SET_ACT_LEAVE_LPS, NULL);

	if(hs_connecting)
	{
		halbtc8812a1ant_ActionHs(btcoexist, hs_connecting);
	}
	else if(btcoexist->bt_info.bt_disabled)
	{
		halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, false, 9);
		halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x55555555, 0xffff, 0x3);
	}
	else if(BT_8812A_1ANT_BT_STATUS_INQ_PAGE == coex_dm->bt_status)
{
		halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 30);
		halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x55555555, 0xffff, 0x3);
	}
	else if( (BT_8812A_1ANT_BT_STATUS_NON_CONNECTED_IDLE == coex_dm->bt_status) ||
		(BT_8812A_1ANT_BT_STATUS_CONNECTED_IDLE == coex_dm->bt_status) )
	{
		halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 28);
		halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x55555555, 0xffff, 0x3);
	}
	else if(BT_8812A_1ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status)
	{
		if(stack_info->hid_exist)
			halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 35);
		else
			halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 29);
		halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x5afa5afa, 0xffff, 0x3);
	}
	else if( (BT_8812A_1ANT_BT_STATUS_SCO_BUSY == coex_dm->bt_status) ||
		(BT_8812A_1ANT_BT_STATUS_ACL_SCO_BUSY == coex_dm->bt_status) )
		{
		halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, false, 8);
		halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x5aea5aea, 0x5aea5aea, 0xffff, 0x3);
	}
	else 
	{
		//error condition, should not reach here, record error number for debugging.
		coex_dm->error_condition = 1;
	}
}

void
halbtc8812a1ant_ActionWifiConnectedScan(
	 	PBTC_COEXIST		btcoexist
	)
{
	PBTC_STACK_INFO	stack_info=&btcoexist->stack_info;
	
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], ActionConnectedScan()===>\n"));

	if(btcoexist->bt_info.bt_disabled)
	{
		halbtc8812a1ant_PsTdmaCheckForPowerSaveState(btcoexist, false);
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_LEAVE_LPS, NULL);
		halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, false, 9);
		halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x55555555, 0xffff, 0x3);
	}
	else
	{
		halbtc8812a1ant_PsTdmaCheckForPowerSaveState(btcoexist, true);
		halbtc8812a1ant_LpsRpwm(btcoexist, NORMAL_EXEC, 0x0, 0x4);
		// power save must executed before psTdma.
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_ENTER_LPS, NULL);

		// psTdma
		if(BT_8812A_1ANT_BT_STATUS_INQ_PAGE == coex_dm->bt_status)
		{
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], ActionConnectedScan(), bt is under inquiry/page scan\n"));
			if(stack_info->sco_exist)
			{
				halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 32);
				halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x5afa5afa, 0xffff, 0x3);
			}
			else
			{
				halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 30);
				halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x55555555, 0xffff, 0x3);
			}
		}
		else if( (BT_8812A_1ANT_BT_STATUS_NON_CONNECTED_IDLE == coex_dm->bt_status) ||
			(BT_8812A_1ANT_BT_STATUS_CONNECTED_IDLE == coex_dm->bt_status) )
		{
			halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 5);
			halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x55555555, 0xffff, 0x3);
		}
		else if(BT_8812A_1ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status)
		{
			if(stack_info->hid_exist)
		{
				halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 34);
				halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x5afa5afa, 0xffff, 0x3);
			}
			else
			{
				halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 4);
				halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x55555555, 0xffff, 0x3);
			}
		}
		else if( (BT_8812A_1ANT_BT_STATUS_SCO_BUSY == coex_dm->bt_status) ||
			(BT_8812A_1ANT_BT_STATUS_ACL_SCO_BUSY == coex_dm->bt_status) )
		{
			halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 33);
			halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x5afa5afa, 0xffff, 0x3);
		}
		else 
		{
			//error condition, should not reach here
			coex_dm->error_condition = 2;
		}
	}
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], ActionConnectedScan()<===\n"));
}

void
halbtc8812a1ant_ActionWifiConnectedSpecialPacket(
	 	PBTC_COEXIST		btcoexist
	)
{
	PBTC_STACK_INFO	stack_info=&btcoexist->stack_info;

	halbtc8812a1ant_PsTdmaCheckForPowerSaveState(btcoexist, false);
	btcoexist->btc_set(btcoexist, BTC_SET_ACT_LEAVE_LPS, NULL);

	if(btcoexist->bt_info.bt_disabled)
	{	
		halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, false, 9);
		halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x55555555, 0xffff, 0x3);
	}
	else
	{
		if(BT_8812A_1ANT_BT_STATUS_INQ_PAGE == coex_dm->bt_status)
		{
			if(stack_info->sco_exist)
		{
				halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 32);
				halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x5afa5afa, 0xffff, 0x3);
			}
			else
			{
				halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 30);
				halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x55555555, 0xffff, 0x3);
			}
		}
		else if( (BT_8812A_1ANT_BT_STATUS_NON_CONNECTED_IDLE == coex_dm->bt_status) ||
			(BT_8812A_1ANT_BT_STATUS_CONNECTED_IDLE == coex_dm->bt_status) )
		{
			halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 28);
			halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x55555555, 0xffff, 0x3);
		}
		else if(BT_8812A_1ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status)
		{
			if(stack_info->hid_exist)
				halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 35);
			else
				halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 29);
			halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x5afa5afa, 0xffff, 0x3);
		}
		else if( (BT_8812A_1ANT_BT_STATUS_SCO_BUSY == coex_dm->bt_status) ||
			(BT_8812A_1ANT_BT_STATUS_ACL_SCO_BUSY == coex_dm->bt_status) )
		{
			halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, false, 8);
			halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x5aea5aea, 0x5aea5aea, 0xffff, 0x3);
		}
		else 
		{
			//error condition, should not reach here
			coex_dm->error_condition = 3;
		}
	}
}

void
halbtc8812a1ant_ActionWifiConnected(
	 	PBTC_COEXIST		btcoexist
	)
{
	PBTC_STACK_INFO	stack_info=&btcoexist->stack_info;
	BOOLEAN 	wifi_connected=false, wifi_busy=false, bt_hs_on=false;
	BOOLEAN		scan=false, link=false, roam=false;
	BOOLEAN		hs_connecting=false, under4way=false;
	u4Byte		wifi_bw;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], CoexForWifiConnect()===>\n"));

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED, &wifi_connected);
	if(!wifi_connected)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], CoexForWifiConnect(), return for wifi not connected<===\n"));
		return;
	}

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_4_WAY_PROGRESS, &under4way);
	if(under4way)
	{
		halbtc8812a1ant_ActionWifiConnectedSpecialPacket(btcoexist);
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], CoexForWifiConnect(), return for wifi is under 4way<===\n"));
		return;
	}
	
	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_CONNECTING, &hs_connecting);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &scan);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_LINK, &link);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &roam);
	if(scan || link || roam)
	{
		halbtc8812a1ant_ActionWifiConnectedScan(btcoexist);
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], CoexForWifiConnect(), return for wifi is under scan<===\n"));
		return;
	}
	
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);	
	if(!wifi_busy)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], wifi associated-idle!!!\n"));
		if(btcoexist->bt_info.bt_disabled)
		{
			halbtc8812a1ant_PsTdmaCheckForPowerSaveState(btcoexist, true);
			halbtc8812a1ant_LpsRpwm(btcoexist, NORMAL_EXEC, 0x0, 0x4);
			btcoexist->btc_set(btcoexist, BTC_SET_ACT_ENTER_LPS, NULL);
			halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, false, 9);
			halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x55555555, 0xffff, 0x3);
		}
		else
		{
			if(BT_8812A_1ANT_BT_STATUS_INQ_PAGE == coex_dm->bt_status)
			{
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], bt is under inquiry/page scan!!!\n"));
				if(stack_info->sco_exist)
				{
					halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 32);
					halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x5afa5afa, 0xffff, 0x3);
				}
				else
				{
					halbtc8812a1ant_PsTdmaCheckForPowerSaveState(btcoexist, true);
					halbtc8812a1ant_LpsRpwm(btcoexist, NORMAL_EXEC, 0x0, 0x4);
					// power save must executed before psTdma.
					btcoexist->btc_set(btcoexist, BTC_SET_ACT_ENTER_LPS, NULL);
					halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 30);
					halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x55555555, 0xffff, 0x3);
				}
			}
			else if(BT_8812A_1ANT_BT_STATUS_NON_CONNECTED_IDLE == coex_dm->bt_status)
			{
				halbtc8812a1ant_PsTdmaCheckForPowerSaveState(btcoexist, true);
				halbtc8812a1ant_LpsRpwm(btcoexist, NORMAL_EXEC, 0x26, 0x0);
				// power save must executed before psTdma.
				btcoexist->btc_set(btcoexist, BTC_SET_ACT_ENTER_LPS, NULL);
				halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, false, 9);
				halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x55555555, 0xffff, 0x3);
			}
			else if(BT_8812A_1ANT_BT_STATUS_CONNECTED_IDLE == coex_dm->bt_status)
			{
				halbtc8812a1ant_PsTdmaCheckForPowerSaveState(btcoexist, true);
				halbtc8812a1ant_LpsRpwm(btcoexist, NORMAL_EXEC, 0x26, 0x0);
				// power save must executed before psTdma.
				btcoexist->btc_set(btcoexist, BTC_SET_ACT_ENTER_LPS, NULL);
				halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, false, 0);
				halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x55555555, 0xffff, 0x3);
			}
			else if(BT_8812A_1ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status)
			{
				if(stack_info->hid_exist && stack_info->numOfLink==1)
				{
					// hid only
					halbtc8812a1ant_PsTdmaCheckForPowerSaveState(btcoexist, false);
					// power save must executed before psTdma.
					btcoexist->btc_set(btcoexist, BTC_SET_ACT_LEAVE_LPS, NULL);
					
					halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, false, 8);
					halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x5fff5fff, 0x5fff5fff, 0xffff, 0x3);
					coex_dm->reset_tdma_adjust = true;
				}
				else
				{				
					halbtc8812a1ant_PsTdmaCheckForPowerSaveState(btcoexist, true);
					halbtc8812a1ant_LpsRpwm(btcoexist, NORMAL_EXEC, 0x0, 0x4);
					// power save must executed before psTdma.
					btcoexist->btc_set(btcoexist, BTC_SET_ACT_ENTER_LPS, NULL);

					if(stack_info->hid_exist)
					{
						if(stack_info->a2dp_exist)
						{
							// hid+a2dp
							halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 2);
							halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x5afa5afa, 0xffff, 0x3);
						}
						else if(stack_info->pan_exist)
						{
							if(bt_hs_on)
							{
								// hid+hs
								halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 2);
							}
							else
							{
								// hid+pan
								halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 2);
							}
							halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x5afa5afa, 0xffff, 0x3);
						}
						else
						{
							coex_dm->error_condition = 4;
						}
						coex_dm->reset_tdma_adjust = true;
					}
					else if(stack_info->a2dp_exist)
					{
						if(stack_info->pan_exist)
						{
							if(bt_hs_on)
							{
								// a2dp+hs
								halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 2);
							}
							else
							{
								// a2dp+pan
								halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 36);
							}
							halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x5afa5afa, 0xffff, 0x3);
							coex_dm->reset_tdma_adjust = true;
						}
						else
						{
							// a2dp only
							halbtc8812a1ant_TdmaDurationAdjustForAcl(btcoexist);
							halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x5afa5afa, 0xffff, 0x3);
						}
					}
					else if(stack_info->pan_exist)
					{
						// pan only
						if(bt_hs_on)
						{
							coex_dm->error_condition = 5;
						}
						else
						{
							halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 2);
							halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x5afa5afa, 0xffff, 0x3);
						}
						coex_dm->reset_tdma_adjust = true;
					}
					else
					{
						// temp state, do nothing!!!
						//DbgPrint("error 6, coex_dm->bt_status=%d\n", coex_dm->bt_status);
						//DbgPrint("error 6, stack_info->numOfLink=%d, stack_info->hid_exist=%d, stack_info->a2dp_exist=%d, stack_info->pan_exist=%d, stack_info->sco_exist=%d\n", 
							//stack_info->numOfLink, stack_info->hid_exist, stack_info->a2dp_exist, stack_info->pan_exist, stack_info->sco_exist);
						//coex_dm->error_condition = 6;
					}
				}
			}
			else if( (BT_8812A_1ANT_BT_STATUS_SCO_BUSY == coex_dm->bt_status) ||
				(BT_8812A_1ANT_BT_STATUS_ACL_SCO_BUSY == coex_dm->bt_status) )
			{
				halbtc8812a1ant_PsTdmaCheckForPowerSaveState(btcoexist, false);
				// power save must executed before psTdma.
				btcoexist->btc_set(btcoexist, BTC_SET_ACT_LEAVE_LPS, NULL);

				halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, false, 8);
				halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x5aea5aea, 0x5aea5aea, 0xffff, 0x3);
			}
			else 
			{
				//error condition, should not reach here
				coex_dm->error_condition = 7;
			}
		}
	}
	else
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], wifi busy!!!\n"));
		if(btcoexist->bt_info.bt_disabled)
		{
			halbtc8812a1ant_PsTdmaCheckForPowerSaveState(btcoexist, false);
			btcoexist->btc_set(btcoexist, BTC_SET_ACT_LEAVE_LPS, NULL);
			halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, false, 9);
			halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x55555555, 0xffff, 0x3);
		}
		else
		{
			if(bt_hs_on)
			{
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], HS is under progress!!!\n"));
				//DbgPrint("coex_dm->bt_status = 0x%x\n", coex_dm->bt_status);
				halbtc8812a1ant_ActionHs(btcoexist, hs_connecting);
			}
			else if(BT_8812A_1ANT_BT_STATUS_INQ_PAGE == coex_dm->bt_status)
			{
				if(stack_info->sco_exist)
				{
					halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 32);
					halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x5afa5afa, 0xffff, 0x3);
				}
				else
				{
					halbtc8812a1ant_PsTdmaCheckForPowerSaveState(btcoexist, true);
					halbtc8812a1ant_LpsRpwm(btcoexist, NORMAL_EXEC, 0x0, 0x4);
					// power save must executed before psTdma.
					btcoexist->btc_set(btcoexist, BTC_SET_ACT_ENTER_LPS, NULL);
					halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 30);
					halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x55555555, 0xffff, 0x3);
				}
			}
			else if(BT_8812A_1ANT_BT_STATUS_NON_CONNECTED_IDLE == coex_dm->bt_status)
			{
				halbtc8812a1ant_PsTdmaCheckForPowerSaveState(btcoexist, false);
				// power save must executed before psTdma.
				btcoexist->btc_set(btcoexist, BTC_SET_ACT_LEAVE_LPS, NULL);
				halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 5);
				halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x5a5a5a5a, 0x5a5a5a5a, 0xffff, 0x3);
			}
			else if(BT_8812A_1ANT_BT_STATUS_CONNECTED_IDLE == coex_dm->bt_status)
			{
				halbtc8812a1ant_PsTdmaCheckForPowerSaveState(btcoexist, false);
				// power save must executed before psTdma.
				btcoexist->btc_set(btcoexist, BTC_SET_ACT_LEAVE_LPS, NULL);
				if(bt_hs_on)
				{
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], HS is under progress!!!\n"));
					halbtc8812a1ant_ActionHs(btcoexist, hs_connecting);
				}
				else
				{
					halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 5);
					halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x5a5a5a5a, 0x5a5a5a5a, 0xffff, 0x3);
				}
			}
			else if(BT_8812A_1ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status)
			{
				if(stack_info->hid_exist && stack_info->numOfLink==1)
				{
					// hid only
					halbtc8812a1ant_PsTdmaCheckForPowerSaveState(btcoexist, false);
					// power save must executed before psTdma.
					btcoexist->btc_set(btcoexist, BTC_SET_ACT_LEAVE_LPS, NULL);
					
					halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, false, 8);
					halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x5fff5fff, 0x5fff5fff, 0xffff, 0x3);
					coex_dm->reset_tdma_adjust = true;
				}
				else
				{
					halbtc8812a1ant_PsTdmaCheckForPowerSaveState(btcoexist, true);
					halbtc8812a1ant_LpsRpwm(btcoexist, NORMAL_EXEC, 0x0, 0x4);
					// power save must executed before psTdma.
					btcoexist->btc_set(btcoexist, BTC_SET_ACT_ENTER_LPS, NULL);

					if(stack_info->hid_exist)
					{
						if(stack_info->a2dp_exist)
						{
							// hid+a2dp
							halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 2);
							halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x5afa5afa, 0xffff, 0x3);
						}
						else if(stack_info->pan_exist)
						{
							if(bt_hs_on)
							{
								// hid+hs
								halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 2);
							}
							else
							{
								// hid+pan
								halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 2);
							}
							halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x5afa5afa, 0xffff, 0x3);
						}
						else
						{
							coex_dm->error_condition = 8;
						}
						coex_dm->reset_tdma_adjust = true;
					}
					else if(stack_info->a2dp_exist)
					{
						if(stack_info->pan_exist)
						{
							if(bt_hs_on)
							{
								// a2dp+hs
								halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 2);
							}
							else
							{
								// a2dp+pan
								halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 36);
							}
							halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x5afa5afa, 0xffff, 0x3);
							coex_dm->reset_tdma_adjust = true;
						}
						else
						{
							// a2dp only
							halbtc8812a1ant_TdmaDurationAdjustForAcl(btcoexist);
							halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x5afa5afa, 0xffff, 0x3);
						}
					}
					else if(stack_info->pan_exist)
					{
						// pan only
						if(bt_hs_on)
						{
							coex_dm->error_condition = 9;
						}
						else
						{
							halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, true, 2);
							halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x5afa5afa, 0xffff, 0x3);
						}
						coex_dm->reset_tdma_adjust = true;
					}
					else
					{
						//DbgPrint("error 10, stack_info->numOfLink=%d, stack_info->hid_exist=%d, stack_info->a2dp_exist=%d, stack_info->pan_exist=%d, stack_info->sco_exist=%d\n", 
							//stack_info->numOfLink, stack_info->hid_exist, stack_info->a2dp_exist, stack_info->pan_exist, stack_info->sco_exist);
						coex_dm->error_condition = 10;
					}
				}
			}
			else if( (BT_8812A_1ANT_BT_STATUS_SCO_BUSY == coex_dm->bt_status) ||
				(BT_8812A_1ANT_BT_STATUS_ACL_SCO_BUSY == coex_dm->bt_status) )
			{
				halbtc8812a1ant_PsTdmaCheckForPowerSaveState(btcoexist, false);
				// power save must executed before psTdma.
				btcoexist->btc_set(btcoexist, BTC_SET_ACT_LEAVE_LPS, NULL);

				halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, false, 8);
				halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x5aea5aea, 0x5aea5aea, 0xffff, 0x3);
			}
			else 
			{
				//DbgPrint("error 11, coex_dm->bt_status=%d\n", coex_dm->bt_status);
				//DbgPrint("error 11, stack_info->numOfLink=%d, stack_info->hid_exist=%d, stack_info->a2dp_exist=%d, stack_info->pan_exist=%d, stack_info->sco_exist=%d\n", 
					//stack_info->numOfLink, stack_info->hid_exist, stack_info->a2dp_exist, stack_info->pan_exist, stack_info->sco_exist);
				//error condition, should not reach here
				coex_dm->error_condition = 11;
			}
		}
	}
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], CoexForWifiConnect()<===\n"));
}

void
halbtc8812a1ant_RunSwCoexistMechanism(
	 	PBTC_COEXIST		btcoexist
	)
{
	PBTC_STACK_INFO		stack_info=&btcoexist->stack_info;
	BOOLEAN				wifi_under5g=false, wifi_busy=false, wifi_connected=false;
	u1Byte				bt_info_original=0, bt_retry_cnt=0;
	u1Byte				algorithm=0;

	return;
	if(stack_info->bProfileNotified)
	{
		algorithm = halbtc8812a1ant_ActionAlgorithm(btcoexist);
		coex_dm->cur_algorithm = algorithm;		
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Algorithm = %d \n", coex_dm->cur_algorithm));

		if(halbtc8812a1ant_IsCommonAction(btcoexist))
		{
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action common.\n"));
		}
		else
		{
			switch(coex_dm->cur_algorithm)
			{
				case BT_8812A_1ANT_COEX_ALGO_SCO:
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = SCO.\n"));
					halbtc8812a1ant_ActionSco(btcoexist);
					break;
				case BT_8812A_1ANT_COEX_ALGO_HID:
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = HID.\n"));
					halbtc8812a1ant_ActionHid(btcoexist);
					break;
				case BT_8812A_1ANT_COEX_ALGO_A2DP:
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = A2DP.\n"));
					halbtc8812a1ant_ActionA2dp(btcoexist);
					break;
				case BT_8812A_1ANT_COEX_ALGO_A2DP_PANHS:
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = A2DP+PAN(HS).\n"));
					halbtc8812a1ant_ActionA2dpPanHs(btcoexist);
					break;
				case BT_8812A_1ANT_COEX_ALGO_PANEDR:
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = PAN(EDR).\n"));
					halbtc8812a1ant_ActionPanEdr(btcoexist);
					break;
				case BT_8812A_1ANT_COEX_ALGO_PANHS:
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = HS mode.\n"));
					halbtc8812a1ant_ActionPanHs(btcoexist);
					break;
				case BT_8812A_1ANT_COEX_ALGO_PANEDR_A2DP:
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = PAN+A2DP.\n"));
					halbtc8812a1ant_ActionPanEdrA2dp(btcoexist);
					break;
				case BT_8812A_1ANT_COEX_ALGO_PANEDR_HID:
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = PAN(EDR)+HID.\n"));
					halbtc8812a1ant_ActionPanEdrHid(btcoexist);
					break;
				case BT_8812A_1ANT_COEX_ALGO_HID_A2DP_PANEDR:
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = HID+A2DP+PAN.\n"));
					halbtc8812a1ant_ActionHidA2dpPanEdr(btcoexist);
					break;
				case BT_8812A_1ANT_COEX_ALGO_HID_A2DP:
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = HID+A2DP.\n"));
					halbtc8812a1ant_ActionHidA2dp(btcoexist);
					break;
				default:
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = coexist All Off!!\n"));
					halbtc8812a1ant_CoexAllOff(btcoexist);
					break;
			}
			coex_dm->pre_algorithm = coex_dm->cur_algorithm;
		}
	}
}

void
halbtc8812a1ant_RunCoexistMechanism(
	 	PBTC_COEXIST		btcoexist
	)
{
	PBTC_STACK_INFO		stack_info=&btcoexist->stack_info;
	BOOLEAN				wifi_under5g=false, wifi_busy=false, wifi_connected=false;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], RunCoexistMechanism()===>\n"));

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under5g);

	if(wifi_under5g)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], RunCoexistMechanism(), return for 5G <===\n"));
		return;
	}

	if(btcoexist->manual_control)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], RunCoexistMechanism(), return for Manual CTRL <===\n"));
		return;
	}

	if(btcoexist->stop_coex_dm)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], RunCoexistMechanism(), return for Stop Coex DM <===\n"));
		return;
	}

	halbtc8812a1ant_RunSwCoexistMechanism(btcoexist);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED, &wifi_connected);
	if(btcoexist->bt_info.bt_disabled)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], bt is disabled!!!\n"));
		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);
		if(wifi_busy)
		{			
			halbtc8812a1ant_PsTdmaCheckForPowerSaveState(btcoexist, false);
			btcoexist->btc_set(btcoexist, BTC_SET_ACT_LEAVE_LPS, NULL);
			halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, false, 9);
		}
		else
		{
			halbtc8812a1ant_PsTdmaCheckForPowerSaveState(btcoexist, true);
			halbtc8812a1ant_LpsRpwm(btcoexist, NORMAL_EXEC, 0x0, 0x4);
			// power save must executed before psTdma.
			btcoexist->btc_set(btcoexist, BTC_SET_ACT_ENTER_LPS, NULL);
			halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, false, 9);
		}
		halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x55555555, 0xffff, 0x3);
	}
	else if(coex_sta->under_ips)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], wifi is under IPS !!!\n"));
		halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, false, 0);
		halbtc8812a1ant_CoexTable(btcoexist, NORMAL_EXEC, 0x55555555, 0x55555555, 0xffff, 0x3);
		halbtc8812a1ant_WifiParaAdjust(btcoexist, false);
	}
	else if(!wifi_connected)
	{
		BOOLEAN	scan=false, link=false, roam=false;
		
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], wifi is non connected-idle !!!\n"));

		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &scan);
		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_LINK, &link);
		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &roam);

		if(scan || link || roam)
			halbtc8812a1ant_ActionWifiNotConnectedAssoAuthScan(btcoexist);
		else
			halbtc8812a1ant_ActionWifiNotConnected(btcoexist);
	}
	else	// wifi LPS/Busy
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], wifi is NOT under IPS!!!\n"));
		halbtc8812a1ant_WifiParaAdjust(btcoexist, true);
		halbtc8812a1ant_ActionWifiConnected(btcoexist);
	}
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], RunCoexistMechanism()<===\n"));
}

void
halbtc8812a1ant_InitCoexDm(
	 	PBTC_COEXIST		btcoexist
	)
{	
	BOOLEAN		wifi_connected=false;
	// force to reset coex mechanism

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED, &wifi_connected);
	if(!wifi_connected) // non-connected scan
	{
		halbtc8812a1ant_ActionWifiNotConnected(btcoexist);
	}
	else	// wifi is connected
	{
		halbtc8812a1ant_ActionWifiConnected(btcoexist);
	}

	halbtc8812a1ant_FwDacSwingLvl(btcoexist, FORCE_EXEC, 6);
	halbtc8812a1ant_DecBtPwr(btcoexist, FORCE_EXEC, false);

	// sw all off
	halbtc8812a1ant_SwMechanism1(btcoexist,false,false,false,false);
	halbtc8812a1ant_SwMechanism2(btcoexist,false,false,false,0x18);

	halbtc8812a1ant_CoexTable(btcoexist, FORCE_EXEC, 0x55555555, 0x55555555, 0xffff, 0x3);
}

//============================================================
// work around function start with wa_halbtc8812a1ant_
//============================================================
//============================================================
// extern function start with EXhalbtc8812a1ant_
//============================================================
void
EXhalbtc8812a1ant_InitHwConfig(
	 	PBTC_COEXIST		btcoexist
	)
{
	u4Byte	u4_tmp=0;
	u2Byte	u2Tmp=0;
	u1Byte	u1_tmp=0;

	BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT, ("[BTCoex], 1Ant Init HW Config!!\n"));

	// backup rf 0x1e value
	coex_dm->bt_rf0x1e_backup = 
		btcoexist->btc_get_rf_reg(btcoexist, BTC_RF_A, 0x1e, 0xfffff);
	
	//ant sw control to BT
	 btcoexist->btc_write_4byte(btcoexist, 0x900, 0x00000400);
	 btcoexist->btc_write_1byte(btcoexist, 0x76d, 0x1);
	 btcoexist->btc_write_1byte(btcoexist, 0xcb3, 0x77);
	 btcoexist->btc_write_1byte(btcoexist, 0xcb7, 0x40);	

	// 0x790[5:0]=0x5
	u1_tmp = btcoexist->btc_read_1byte(btcoexist, 0x790);
	u1_tmp &= 0xc0;
	u1_tmp |= 0x5;
	btcoexist->btc_write_1byte(btcoexist, 0x790, u1_tmp);

	// PTA parameter
	btcoexist->btc_write_1byte(btcoexist, 0x6cc, 0x0);
	btcoexist->btc_write_4byte(btcoexist, 0x6c8, 0xffff);
	btcoexist->btc_write_4byte(btcoexist, 0x6c4, 0x55555555);
	btcoexist->btc_write_4byte(btcoexist, 0x6c0, 0x55555555);

	// coex parameters
	btcoexist->btc_write_1byte(btcoexist, 0x778, 0x1);

	// enable counter statistics
	btcoexist->btc_write_1byte(btcoexist, 0x76e, 0x4);

	// enable PTA
	btcoexist->btc_write_1byte(btcoexist, 0x40, 0x20);

	// bt clock related
	u1_tmp = btcoexist->btc_read_1byte(btcoexist, 0x4);
	u1_tmp |= BIT7;
	btcoexist->btc_write_1byte(btcoexist, 0x4, u1_tmp);

	// bt clock related
	u1_tmp = btcoexist->btc_read_1byte(btcoexist, 0x7);
	u1_tmp |= BIT1;
	btcoexist->btc_write_1byte(btcoexist, 0x7, u1_tmp);
}

void
EXhalbtc8812a1ant_InitCoexDm(
	 	PBTC_COEXIST		btcoexist
	)
{
	BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT, ("[BTCoex], Coex Mechanism Init!!\n"));

	btcoexist->stop_coex_dm = false;

	halbtc8812a1ant_InitCoexDm(btcoexist);
}

void
EXhalbtc8812a1ant_DisplayCoexInfo(
	 	PBTC_COEXIST		btcoexist
	)
{
	PBTC_BOARD_INFO		board_info=&btcoexist->boardInfo;
	PBTC_STACK_INFO		stack_info=&btcoexist->stack_info;
	pu1Byte				cli_buf=btcoexist->cli_buf;
	u1Byte				u1_tmp[4], i, bt_info_ext, psTdmaCase=0;
	u4Byte				u4_tmp[4];
	BOOLEAN				roam=false, scan=false, link=false, wifi_under5g=false;
	BOOLEAN				bt_hs_on=false, wifi_busy=false;
	s4Byte				wifi_rssi=0, bt_hs_rssi=0;
	u4Byte				wifi_bw, wifiTrafficDir;
	u1Byte				wifiDot11Chnl, wifiHsChnl;

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n ============[BT Coexist info]============");
	CL_PRINTF(cli_buf);

	if(btcoexist->manual_control)
	{
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n ============[Under Manual Control]============");
		CL_PRINTF(cli_buf);
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n ==========================================");
		CL_PRINTF(cli_buf);
	}
	if(btcoexist->stop_coex_dm)
	{
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n ============[Coex is STOPPED]============");
		CL_PRINTF(cli_buf);
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n ==========================================");
		CL_PRINTF(cli_buf);
	}

	if(!board_info->bBtExist)
	{
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n BT not exists !!!");
		CL_PRINTF(cli_buf);
		return;
	}

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d ", "Ant PG number/ Ant mechanism:", \
		board_info->pgAntNum, board_info->btdmAntNum);
	CL_PRINTF(cli_buf);	
	
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s / %d", "BT stack/ hci ext ver", \
		((stack_info->bProfileNotified)? "Yes":"No"), stack_info->hciVersion);
	CL_PRINTF(cli_buf);
	btcoexist->btc_disp_dbg_msg(btcoexist, BTC_DBG_DISP_BT_FW_VER);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);
	btcoexist->btc_get(btcoexist, BTC_GET_U1_WIFI_DOT11_CHNL, &wifiDot11Chnl);
	btcoexist->btc_get(btcoexist, BTC_GET_U1_WIFI_HS_CHNL, &wifiHsChnl);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d(%d)", "Dot11 channel / HsChnl(HsMode)", \
		wifiDot11Chnl, wifiHsChnl, bt_hs_on);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %02x %02x %02x ", "H2C Wifi inform bt chnl Info", \
		coex_dm->wifi_chnl_info[0], coex_dm->wifi_chnl_info[1],
		coex_dm->wifi_chnl_info[2]);
	CL_PRINTF(cli_buf);

	btcoexist->btc_get(btcoexist, BTC_GET_S4_WIFI_RSSI, &wifi_rssi);
	btcoexist->btc_get(btcoexist, BTC_GET_S4_HS_RSSI, &bt_hs_rssi);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d", "Wifi rssi/ HS rssi", \
		wifi_rssi, bt_hs_rssi);
	CL_PRINTF(cli_buf);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &scan);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_LINK, &link);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &roam);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d ", "Wifi link/ roam/ scan", \
		link, roam, scan);
	CL_PRINTF(cli_buf);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under5g);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_TRAFFIC_DIRECTION, &wifiTrafficDir);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s / %s/ %s ", "Wifi status", \
		(wifi_under5g? "5G":"2.4G"),
		((BTC_WIFI_BW_LEGACY==wifi_bw)? "Legacy": (((BTC_WIFI_BW_HT40==wifi_bw)? "HT40":"HT20"))),
		((!wifi_busy)? "idle": ((BTC_WIFI_TRAFFIC_TX==wifiTrafficDir)? "uplink":"downlink")));
	CL_PRINTF(cli_buf);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = [%s/ %d/ %d] ", "BT [status/ rssi/ retryCnt]", \
		((coex_sta->c2h_bt_inquiry_page)?("inquiry/page scan"):((BT_8812A_1ANT_BT_STATUS_NON_CONNECTED_IDLE == coex_dm->bt_status)? "non-connected idle":
		(  (BT_8812A_1ANT_BT_STATUS_CONNECTED_IDLE == coex_dm->bt_status)? "connected-idle":"busy"))),
		coex_sta->bt_rssi, coex_sta->bt_retry_cnt);
	CL_PRINTF(cli_buf);
	
	if(stack_info->bProfileNotified)
	{			
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d / %d / %d", "SCO/HID/PAN/A2DP", \
			stack_info->sco_exist, stack_info->hid_exist, stack_info->pan_exist, stack_info->a2dp_exist);
		CL_PRINTF(cli_buf);	

		btcoexist->btc_disp_dbg_msg(btcoexist, BTC_DBG_DISP_BT_LINK_INFO);
	}

	bt_info_ext = coex_sta->bt_info_ext;
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s", "BT Info A2DP rate", \
		(bt_info_ext&BIT0)? "Basic rate":"EDR rate");
	CL_PRINTF(cli_buf);	

	for(i=0; i<BT_INFO_SRC_8812A_1ANT_MAX; i++)
	{
		if(coex_sta->bt_info_c2h_cnt[i])
		{				
			CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %02x %02x %02x %02x %02x %02x %02x(%d)", GLBtInfoSrc8812a1Ant[i], \
				coex_sta->bt_info_c2h[i][0], coex_sta->bt_info_c2h[i][1],
				coex_sta->bt_info_c2h[i][2], coex_sta->bt_info_c2h[i][3],
				coex_sta->bt_info_c2h[i][4], coex_sta->bt_info_c2h[i][5],
				coex_sta->bt_info_c2h[i][6], coex_sta->bt_info_c2h_cnt[i]);
			CL_PRINTF(cli_buf);
		}
	}
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/%s, (0x%x/0x%x)", "PS state, IPS/LPS, (lps/rpwm)", \
		((coex_sta->under_ips? "IPS ON":"IPS OFF")),
		((coex_sta->under_lps? "LPS ON":"LPS OFF")), 
		btcoexist->bt_info.lps1Ant, 
		btcoexist->bt_info.rpwm1Ant);
	CL_PRINTF(cli_buf);
	btcoexist->btc_disp_dbg_msg(btcoexist, BTC_DBG_DISP_FW_PWR_MODE_CMD);

	if(!btcoexist->manual_control)
	{
		// Sw mechanism	
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Sw mechanism]============");
		CL_PRINTF(cli_buf);
	
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d/ %d ", "SM1[ShRf/ LpRA/ LimDig/ btLna]", \
			coex_dm->cur_rf_rx_lpf_shrink, coex_dm->cur_low_penalty_ra, coex_dm->limited_dig, coex_dm->bCurBtLnaConstrain);
		CL_PRINTF(cli_buf);
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d(0x%x) ", "SM2[AgcT/ AdcB/ SwDacSwing(lvl)]", \
			coex_dm->cur_agc_table_en, coex_dm->cur_adc_back_off, coex_dm->cur_dac_swing_on, coex_dm->cur_dac_swing_lvl);
		CL_PRINTF(cli_buf);
	
		// Fw mechanism		
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Fw mechanism]============");
		CL_PRINTF(cli_buf);	

		psTdmaCase = coex_dm->cur_ps_tdma;
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %02x %02x %02x %02x %02x case-%d", "PS TDMA", \
			coex_dm->ps_tdma_para[0], coex_dm->ps_tdma_para[1],
			coex_dm->ps_tdma_para[2], coex_dm->ps_tdma_para[3],
			coex_dm->ps_tdma_para[4], psTdmaCase);
		CL_PRINTF(cli_buf);

		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x ", "Latest error condition(should be 0)", \
			coex_dm->error_condition);
		CL_PRINTF(cli_buf);
		
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d ", "DecBtPwr/ IgnWlanAct", \
			coex_dm->cur_dec_bt_pwr, coex_dm->cur_ignore_wlan_act);
		CL_PRINTF(cli_buf);
	}

	// Hw setting		
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Hw setting]============");
	CL_PRINTF(cli_buf);	

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "RF-A, 0x1e initVal", \
		coex_dm->bt_rf0x1e_backup);
	CL_PRINTF(cli_buf);

	u1_tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x778);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x778", \
		u1_tmp[0]);
	CL_PRINTF(cli_buf);
	
	u1_tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x92c);
	u4_tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x930);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x", "0x92c/ 0x930", \
		(u1_tmp[0]), u4_tmp[0]);
	CL_PRINTF(cli_buf);

	u1_tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x40);
	u1_tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0x4f);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x", "0x40/ 0x4f", \
		u1_tmp[0], u1_tmp[1]);
	CL_PRINTF(cli_buf);

	u4_tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x550);
	u1_tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x522);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x", "0x550(bcn ctrl)/0x522", \
		u4_tmp[0], u1_tmp[0]);
	CL_PRINTF(cli_buf);

	u4_tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0xc50);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0xc50(dig)", \
		u4_tmp[0]);
	CL_PRINTF(cli_buf);

#if 0
	u4_tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0xf48);
	u4_tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0xf4c);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x", "0xf48/ 0xf4c (FA cnt)", \
		u4_tmp[0], u4_tmp[1]);
	CL_PRINTF(cli_buf);
#endif

	u4_tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x6c0);
	u4_tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0x6c4);
	u4_tmp[2] = btcoexist->btc_read_4byte(btcoexist, 0x6c8);
	u1_tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x6cc);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x/ 0x%x", "0x6c0/0x6c4/0x6c8/0x6cc(coexTable)", \
		u4_tmp[0], u4_tmp[1], u4_tmp[2], u1_tmp[0]);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d", "0x770(hp rx[31:16]/tx[15:0])", \
		coex_sta->high_priority_rx, coex_sta->high_priority_tx);
	CL_PRINTF(cli_buf);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d", "0x774(lp rx[31:16]/tx[15:0])", \
		coex_sta->low_priority_rx, coex_sta->low_priority_tx);
	CL_PRINTF(cli_buf);
	
	btcoexist->btc_disp_dbg_msg(btcoexist, BTC_DBG_DISP_COEX_STATISTICS);
}


void
EXhalbtc8812a1ant_IpsNotify(
	 	PBTC_COEXIST		btcoexist,
	 	u1Byte			type
	)
{
	u4Byte	u4_tmp=0;

	if(btcoexist->manual_control ||	btcoexist->stop_coex_dm)
		return;

	if(BTC_IPS_ENTER == type)
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], IPS ENTER notify\n"));
		coex_sta->under_ips = true;

		// 0x4c[23]=1
		u4_tmp = btcoexist->btc_read_4byte(btcoexist, 0x4c);
		u4_tmp |= BIT23;
		btcoexist->btc_write_4byte(btcoexist, 0x4c, u4_tmp);
		
		halbtc8812a1ant_CoexAllOff(btcoexist);
	}
	else if(BTC_IPS_LEAVE == type)
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], IPS LEAVE notify\n"));
		coex_sta->under_ips = false;
		//halbtc8812a1ant_InitCoexDm(btcoexist);
	}
}

void
EXhalbtc8812a1ant_LpsNotify(
	 	PBTC_COEXIST		btcoexist,
	 	u1Byte			type
	)
{
	if(btcoexist->manual_control || btcoexist->stop_coex_dm)
		return;

	if(BTC_LPS_ENABLE == type)
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], LPS ENABLE notify\n"));
		coex_sta->under_lps = true;
	}
	else if(BTC_IPS_LEAVE == type)
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], LPS DISABLE notify\n"));
		coex_sta->under_lps = false;
	}
}

void
EXhalbtc8812a1ant_ScanNotify(
	 	PBTC_COEXIST		btcoexist,
	 	u1Byte			type
	)
{
	PBTC_STACK_INFO 	stack_info=&btcoexist->stack_info;
	BOOLEAN 		wifi_connected=false;	

	if(btcoexist->manual_control ||btcoexist->stop_coex_dm)
		return;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED, &wifi_connected);
	if(BTC_SCAN_START == type)
	{	
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], SCAN START notify\n"));
		if(!wifi_connected)	// non-connected scan
		{
			//set 0x550[3]=1 before PsTdma
			//halbtc8812a1ant_Reg0x550Bit3(btcoexist, true);
			halbtc8812a1ant_ActionWifiNotConnectedAssoAuthScan(btcoexist);
		}
		else	// wifi is connected
		{
			halbtc8812a1ant_ActionWifiConnectedScan(btcoexist);
		}
	}
	else if(BTC_SCAN_FINISH == type)
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], SCAN FINISH notify\n"));
		if(!wifi_connected)	// non-connected scan
		{
			//halbtc8812a1ant_Reg0x550Bit3(btcoexist, false);
			halbtc8812a1ant_ActionWifiNotConnected(btcoexist);
		}
		else
		{
			halbtc8812a1ant_ActionWifiConnected(btcoexist);
		}
	}
}

void
EXhalbtc8812a1ant_ConnectNotify(
	 	PBTC_COEXIST		btcoexist,
	 	u1Byte			type
	)
{
	PBTC_STACK_INFO 	stack_info=&btcoexist->stack_info;
	BOOLEAN			wifi_connected=false;	

	if(btcoexist->manual_control ||btcoexist->stop_coex_dm)
		return;

	if(BTC_ASSOCIATE_START == type)
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], CONNECT START notify\n"));
		if(btcoexist->bt_info.bt_disabled)
		{			
			halbtc8812a1ant_PsTdmaCheckForPowerSaveState(btcoexist, false);
			btcoexist->btc_set(btcoexist, BTC_SET_ACT_LEAVE_LPS, NULL);
			halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, false, 9);
		}
		else
		{
			halbtc8812a1ant_ActionWifiNotConnectedAssoAuthScan(btcoexist);
		}
	}
	else if(BTC_ASSOCIATE_FINISH == type)
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], CONNECT FINISH notify\n"));
		
		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED, &wifi_connected);
		if(!wifi_connected) // non-connected scan
		{
			//halbtc8812a1ant_Reg0x550Bit3(btcoexist, false);
			halbtc8812a1ant_ActionWifiNotConnected(btcoexist);
		}
		else
		{
			halbtc8812a1ant_ActionWifiConnected(btcoexist);
		}
	}
}

void
EXhalbtc8812a1ant_MediaStatusNotify(
	 	PBTC_COEXIST			btcoexist,
	 	u1Byte				type
	)
{
	u1Byte			dataLen=5;
	u1Byte			buf[6] = {0};
	u1Byte			h2c_parameter[3] ={0};
	BOOLEAN			wifi_under5g=false;
	u4Byte			wifi_bw;
	u1Byte			wifi_central_chnl;

	if(btcoexist->manual_control ||btcoexist->stop_coex_dm)
		return;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under5g);

	// only 2.4G we need to inform bt the chnl mask
	if(!wifi_under5g)
	{
		if(BTC_MEDIA_CONNECT == type)
		{
			h2c_parameter[0] = 0x1;
		}
		btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
		btcoexist->btc_get(btcoexist, BTC_GET_U1_WIFI_CENTRAL_CHNL, &wifi_central_chnl);
		h2c_parameter[1] = wifi_central_chnl;
		if(BTC_WIFI_BW_HT40 == wifi_bw)
			h2c_parameter[2] = 0x30;
		else
			h2c_parameter[2] = 0x20;
	}
		
	coex_dm->wifi_chnl_info[0] = h2c_parameter[0];
	coex_dm->wifi_chnl_info[1] = h2c_parameter[1];
	coex_dm->wifi_chnl_info[2] = h2c_parameter[2];
	
	buf[0] = dataLen;
	buf[1] = 0x5;				// OP_Code
	buf[2] = 0x3;				// OP_Code_Length
	buf[3] = h2c_parameter[0]; 	// OP_Code_Content
	buf[4] = h2c_parameter[1];
	buf[5] = h2c_parameter[2];
		
	btcoexist->btc_set(btcoexist, BTC_SET_ACT_CTRL_BT_COEX, (PVOID)&buf[0]);		
}

void
EXhalbtc8812a1ant_SpecialPacketNotify(
	 	PBTC_COEXIST			btcoexist,
	 	u1Byte				type
	)
{
	BOOLEAN 	bSecurityLink=false;

	if(btcoexist->manual_control ||btcoexist->stop_coex_dm)
		return;

	//if(type == BTC_PACKET_DHCP)
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], special Packet(%d) notify\n", type));
		if(btcoexist->bt_info.bt_disabled)
		{
			halbtc8812a1ant_PsTdmaCheckForPowerSaveState(btcoexist, false);
			btcoexist->btc_set(btcoexist, BTC_SET_ACT_LEAVE_LPS, NULL);
			halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, false, 9);	
		}
		else
		{
			halbtc8812a1ant_ActionWifiConnectedSpecialPacket(btcoexist);
		}
	}
}

void
EXhalbtc8812a1ant_BtInfoNotify(
	 	PBTC_COEXIST		btcoexist,
	 	pu1Byte			tmp_buf,
	 	u1Byte			length
	)
{
	u1Byte			bt_info=0;
	u1Byte			i, rsp_source=0;
	static u4Byte		set_bt_lna_cnt=0, set_bt_psd_mode=0;
	BOOLEAN			bt_busy=false, limited_dig=false;
	BOOLEAN			wifi_connected=false;
	BOOLEAN			bRejApAggPkt=false;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BtInfoNotify()===>\n"));


	rsp_source = tmp_buf[0]&0xf;
	if(rsp_source >= BT_INFO_SRC_8812A_1ANT_MAX)
		rsp_source = BT_INFO_SRC_8812A_1ANT_WIFI_FW;
	coex_sta->bt_info_c2h_cnt[rsp_source]++;

	BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], Bt info[%d], length=%d, hex data=[", rsp_source, length));
	for(i=0; i<length; i++)
	{
		coex_sta->bt_info_c2h[rsp_source][i] = tmp_buf[i];
		if(i == 1)
			bt_info = tmp_buf[i];
		if(i == length-1)
		{
			BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("0x%2x]\n", tmp_buf[i]));
		}
		else
		{
			BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("0x%2x, ", tmp_buf[i]));
		}
	}

	if(btcoexist->manual_control)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BtInfoNotify(), return for Manual CTRL<===\n"));
		return;
	}
	if(btcoexist->stop_coex_dm)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BtInfoNotify(), return for Coex STOPPED!!<===\n"));
		return;
	}

	if(BT_INFO_SRC_8812A_1ANT_WIFI_FW != rsp_source)
	{
		coex_sta->bt_retry_cnt =
			coex_sta->bt_info_c2h[rsp_source][2];

		coex_sta->bt_rssi =
			coex_sta->bt_info_c2h[rsp_source][3]*2+10;

		coex_sta->bt_info_ext = 
			coex_sta->bt_info_c2h[rsp_source][4];

		// Here we need to resend some wifi info to BT
		// because bt is reset and loss of the info.
		if( (coex_sta->bt_info_ext & BIT1) )
		{			
			btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED, &wifi_connected);
			if(wifi_connected)
			{
				EXhalbtc8812a1ant_MediaStatusNotify(btcoexist, BTC_MEDIA_CONNECT);
			}
			else
			{
				EXhalbtc8812a1ant_MediaStatusNotify(btcoexist, BTC_MEDIA_DISCONNECT);
			}

			set_bt_psd_mode = 0;
		}

		// test-chip bt patch doesn't support, temporary remove.
		// need to add back when mp-chip. 12/20/2012
#if 0		
		if(set_bt_psd_mode <= 3)
		{
			halbtc8812a1ant_SetBtPsdMode(btcoexist, FORCE_EXEC, 0xd);
			set_bt_psd_mode++;
		}
		
		if(coex_dm->bCurBtLnaConstrain)
		{
			if( (coex_sta->bt_info_ext & BIT2) )
			{
			}
			else
			{
				if(set_bt_lna_cnt <= 3)
				{
					halbtc8812a1ant_SetBtLnaConstrain(btcoexist, FORCE_EXEC, true);
					set_bt_lna_cnt++;
				}
			}
		}
		else
		{
			set_bt_lna_cnt = 0;
		}
#endif
		// test-chip bt patch only rsp the status for BT_RSP, 
		// so temporary we consider the following only under BT_RSP
		if(BT_INFO_SRC_8812A_1ANT_BT_RSP == rsp_source)
		{
			if( (coex_sta->bt_info_ext & BIT3) )
			{
			#if 0// temp disable because bt patch report the wrong value.
				halbtc8812a1ant_IgnoreWlanAct(btcoexist, FORCE_EXEC, false);
			#endif
			}
			else
			{
				// BT already NOT ignore Wlan active, do nothing here.
			}

			if( (coex_sta->bt_info_ext & BIT4) )
			{
				// BT auto report already enabled, do nothing
			}
			else
			{
				halbtc8812a1ant_BtAutoReport(btcoexist, FORCE_EXEC, true);
			}
		}
	}
		
	// check BIT2 first ==> check if bt is under inquiry or page scan
	if(bt_info & BT_INFO_8812A_1ANT_B_INQ_PAGE)
	{
		coex_sta->c2h_bt_inquiry_page = true;
		coex_dm->bt_status = BT_8812A_1ANT_BT_STATUS_INQ_PAGE;
	}
	else
	{
		coex_sta->c2h_bt_inquiry_page = false;
		if(!(bt_info&BT_INFO_8812A_1ANT_B_CONNECTION))
		{
			coex_dm->bt_status = BT_8812A_1ANT_BT_STATUS_NON_CONNECTED_IDLE;
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BtInfoNotify(), bt non-connected idle!!!\n"));
		}
		else if(bt_info == BT_INFO_8812A_1ANT_B_CONNECTION)	// connection exists but no busy
		{
			coex_dm->bt_status = BT_8812A_1ANT_BT_STATUS_CONNECTED_IDLE;
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BtInfoNotify(), bt connected-idle!!!\n"));
		}		
		else if((bt_info&BT_INFO_8812A_1ANT_B_SCO_ESCO) ||
			(bt_info&BT_INFO_8812A_1ANT_B_SCO_BUSY))
		{
			coex_dm->bt_status = BT_8812A_1ANT_BT_STATUS_SCO_BUSY;
			bRejApAggPkt = true;
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BtInfoNotify(), bt sco busy!!!\n"));
		}
		else if(bt_info&BT_INFO_8812A_1ANT_B_ACL_BUSY)
		{
			if(BT_8812A_1ANT_BT_STATUS_ACL_BUSY != coex_dm->bt_status)
				coex_dm->reset_tdma_adjust = true;
			coex_dm->bt_status = BT_8812A_1ANT_BT_STATUS_ACL_BUSY;
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BtInfoNotify(), bt acl busy!!!\n"));
		}
#if 0
		else if(bt_info&BT_INFO_8812A_1ANT_B_SCO_ESCO)
		{
			coex_dm->bt_status = BT_8812A_1ANT_BT_STATUS_ACL_SCO_BUSY;
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BtInfoNotify(), bt acl/sco busy!!!\n"));
		}
#endif
		else
		{
			//DbgPrint("error, undefined bt_info=0x%x\n", bt_info);
			coex_dm->bt_status = BT_8812A_1ANT_BT_STATUS_MAX;
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BtInfoNotify(), bt non-defined state!!!\n"));
		}

		// send delete BA to disable aggregation
		//btcoexist->btc_set(btcoexist, BTC_SET_BL_TO_REJ_AP_AGG_PKT, &bRejApAggPkt);
	}

	if( (BT_8812A_1ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status) ||
		(BT_8812A_1ANT_BT_STATUS_SCO_BUSY == coex_dm->bt_status) ||
		(BT_8812A_1ANT_BT_STATUS_ACL_SCO_BUSY == coex_dm->bt_status) )
	{
		bt_busy = true;
	}
	else
	{
		bt_busy = false;
	}
	btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_TRAFFIC_BUSY, &bt_busy);

	if(bt_busy)
	{
		limited_dig = true;
	}
	else
	{
		limited_dig = false;
	}
	coex_dm->limited_dig = limited_dig;
	btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_LIMITED_DIG, &limited_dig);

	halbtc8812a1ant_RunCoexistMechanism(btcoexist);
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BtInfoNotify()<===\n"));
}

void
EXhalbtc8812a1ant_StackOperationNotify(
	 	PBTC_COEXIST			btcoexist,
	 	u1Byte				type
	)
{
	if(BTC_STACK_OP_INQ_PAGE_PAIR_START == type)
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], StackOP Inquiry/page/pair start notify\n"));
	}
	else if(BTC_STACK_OP_INQ_PAGE_PAIR_FINISH == type)
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], StackOP Inquiry/page/pair finish notify\n"));
	}
}

void
EXhalbtc8812a1ant_HaltNotify(
	 	PBTC_COEXIST			btcoexist
	)
{
	BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], Halt notify\n"));

	halbtc8812a1ant_IgnoreWlanAct(btcoexist, FORCE_EXEC, true);
	halbtc8812a1ant_PsTdma(btcoexist, FORCE_EXEC, false, 0);
	btcoexist->btc_write_1byte(btcoexist, 0x4f, 0xf);
	halbtc8812a1ant_WifiParaAdjust(btcoexist, false);
}

void
EXhalbtc8812a1ant_PnpNotify(
	 	PBTC_COEXIST			btcoexist,
	 	u1Byte				pnpState
	)
{
	BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], Pnp notify\n"));

	if(BTC_WIFI_PNP_SLEEP == pnpState)
	{
		btcoexist->stop_coex_dm = true;
		halbtc8812a1ant_IgnoreWlanAct(btcoexist, FORCE_EXEC, true);
		halbtc8812a1ant_PsTdmaCheckForPowerSaveState(btcoexist, false);
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_LEAVE_LPS, NULL);
		halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, false, 9);	
	}
	else if(BTC_WIFI_PNP_WAKE_UP == pnpState)
	{
		
	}
}

void
EXhalbtc8812a1ant_Periodical(
	 	PBTC_COEXIST			btcoexist
	)
{
	BOOLEAN			wifi_under5g=false;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Periodical()===>\n"));
	BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], 1Ant Periodical!!\n"));

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under5g);

	if(wifi_under5g)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Periodical(), return for 5G<===\n"));
		halbtc8812a1ant_CoexAllOff(btcoexist);
		return;
	}

	halbtc8812a1ant_QueryBtInfo(btcoexist);
	halbtc8812a1ant_MonitorBtCtr(btcoexist);
	halbtc8812a1ant_MonitorBtEnableDisable(btcoexist);
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Periodical()<===\n"));
}

void
EXhalbtc8812a1ant_DbgControl(
	 	PBTC_COEXIST			btcoexist,
	 	u1Byte				opCode,
	 	u1Byte				opLen,
	 	pu1Byte				pData
	)
{
	switch(opCode)
	{
		case BTC_DBG_SET_COEX_NORMAL:
			btcoexist->manual_control = false;
			halbtc8812a1ant_InitCoexDm(btcoexist);
			break;
		case BTC_DBG_SET_COEX_WIFI_ONLY:
			btcoexist->manual_control = true;
			halbtc8812a1ant_PsTdmaCheckForPowerSaveState(btcoexist, false);
			btcoexist->btc_set(btcoexist, BTC_SET_ACT_LEAVE_LPS, NULL);
			halbtc8812a1ant_PsTdma(btcoexist, NORMAL_EXEC, false, 9);	
			break;
		case BTC_DBG_SET_COEX_BT_ONLY:
			// todo
			break;
		default:
			break;
	}
}
#endif

