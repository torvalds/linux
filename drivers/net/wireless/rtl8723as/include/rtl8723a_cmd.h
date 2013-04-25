/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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
#ifndef __RTL8723A_CMD_H__
#define __RTL8723A_CMD_H__


enum cmd_msg_element_id
{
	NONE_CMDMSG_EID,
	AP_OFFLOAD_EID = 0,
	SET_PWRMODE_EID = 1,
	JOINBSS_RPT_EID = 2,
	RSVD_PAGE_EID = 3,
	RSSI_4_EID = 4,
	RSSI_SETTING_EID = 5,
	MACID_CONFIG_EID = 6,
	MACID_PS_MODE_EID = 7,
	P2P_PS_OFFLOAD_EID = 8,
	SELECTIVE_SUSPEND_ROF_CMD = 9,
	BT_QUEUE_PKT_EID = 17,
	BT_ANT_TDMA_EID = 20,
	BT_2ANT_HID_EID = 21,
	P2P_PS_CTW_CMD_EID = 32,
	FORCE_BT_TX_PWR_EID = 33,
	SET_TDMA_WLAN_ACT_TIME_EID = 34,
	SET_BT_TX_RETRY_INDEX_EID = 35,
	HID_PROFILE_ENABLE_EID = 36,
	BT_IGNORE_WLAN_ACT_EID = 37,
	BT_PTA_MANAGER_UPDATE_ENABLE_EID = 38,
	DAC_SWING_VALUE_EID = 41,
	TRADITIONAL_TDMA_EN_EID = 51,
	H2C_RESET_TSF = 75,
	MAX_CMDMSG_EID	 
};

struct cmd_msg_parm {
	u8 eid; //element id
	u8 sz; // sz
	u8 buf[6];
};

typedef struct _SETPWRMODE_PARM
{
	u8 Mode;
	u8 SmartPS;
	u8 AwakeInterval;	// unit: beacon interval
	u8 bAllQueueUAPSD;

#if 0
	u8 LowRxBCN:1;
	u8 AutoAntSwitch:1;
	u8 PSAllowBTHighPriority:1;
	u8 rsvd43:5;
#else
#define SETPM_LOWRXBCN			BIT(0)
#define SETPM_AUTOANTSWITCH		BIT(1)
#define SETPM_PSALLOWBTHIGHPRI	BIT(2)
	u8 BcnAntMode;
#endif
}__attribute__((__packed__)) SETPWRMODE_PARM, *PSETPWRMODE_PARM;

struct H2C_SS_RFOFF_PARAM{
	u8 ROFOn; // 1: on, 0:off
	u16 gpio_period; // unit: 1024 us
}__attribute__ ((packed));


typedef struct JOINBSSRPT_PARM{
	u8 OpMode;	// RT_MEDIA_STATUS
}JOINBSSRPT_PARM, *PJOINBSSRPT_PARM;

typedef struct _RSVDPAGE_LOC {
	u8 LocProbeRsp;
	u8 LocPsPoll;
	u8 LocNullData;
	u8 LocQosNull;
	u8 LocBTQosNull;
} RSVDPAGE_LOC, *PRSVDPAGE_LOC;

struct P2P_PS_Offload_t {
	u8 Offload_En:1;
	u8 role:1; // 1: Owner, 0: Client
	u8 CTWindow_En:1;
	u8 NoA0_En:1;
	u8 NoA1_En:1;
	u8 AllStaSleep:1; // Only valid in Owner
	u8 discovery:1;
	u8 rsvd:1;
};

struct P2P_PS_CTWPeriod_t {
	u8 CTWPeriod;	//TU
};


// host message to firmware cmd
void rtl8723a_set_FwPwrMode_cmd(PADAPTER padapter, u8 Mode);
void rtl8723a_set_FwJoinBssReport_cmd(PADAPTER padapter, u8 mstatus);
#ifdef CONFIG_BT_COEXIST
void rtl8723a_set_BTCoex_AP_mode_FwRsvdPkt_cmd(PADAPTER padapter);
#endif
u8 rtl8192c_set_rssi_cmd(PADAPTER padapter, u8 *param);
//u8 rtl8723a_set_rssi_cmd(PADAPTER padapter, u8 *param);
u8 rtl8192c_set_raid_cmd(PADAPTER padapter, u32 mask, u8 arg);
//u8 rtl8723a_set_raid_cmd(PADAPTER padapter, u32 mask, u8 arg);
void rtl8192c_Add_RateATid(PADAPTER padapter, u32 bitmap, u8 arg);
//void rtl8723a_Add_RateATid(PADAPTER padapter, u32 bitmap, u8 arg);
u8 rtl8192c_set_FwSelectSuspend_cmd(PADAPTER padapter, u8 bfwpoll, u16 period);
//u8 rtl8723a_set_FwSelectSuspend_cmd(PADAPTER padapter, u8 bfwpoll, u16 period);

#ifdef CONFIG_P2P
void rtl8192c_set_p2p_ps_offload_cmd(PADAPTER padapter, u8 p2p_ps_state);
//void rtl8723a_set_p2p_ps_offload_cmd(PADAPTER padapter, u8 p2p_ps_state);
#endif //CONFIG_P2P

void CheckFwRsvdPageContent(PADAPTER padapter);

#endif

#ifdef CONFIG_TSF_RESET_OFFLOAD
u8 rtl8723c_reset_tsf(_adapter *padapter, u8 reset_port);
#endif	// CONFIG_TSF_RESET_OFFLOAD

