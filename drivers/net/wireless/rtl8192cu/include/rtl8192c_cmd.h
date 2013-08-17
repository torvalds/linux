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
#ifndef __RTL8192C_CMD_H_
#define __RTL8192C_CMD_H_


enum cmd_msg_element_id
{	
	NONE_CMDMSG_EID,
	AP_OFFLOAD_EID=0,
	SET_PWRMODE_EID=1,
	JOINBSS_RPT_EID=2,
	RSVD_PAGE_EID=3,
	RSSI_4_EID = 4,
	RSSI_SETTING_EID=5,
	MACID_CONFIG_EID=6,
	MACID_PS_MODE_EID=7,
	P2P_PS_OFFLOAD_EID=8,
	SELECTIVE_SUSPEND_ROF_CMD=9,
	H2C_WO_WLAN_CMD = 26,	// Wake on Wlan.
	EXT_MACID_PERIOD_EID = 27,	// support macid to 64
	MACID64_CONFIG_EID = 28,	// support macid to 64
	P2P_PS_CTW_CMD_EID=32,
	H2C_92C_IO_OFFLOAD=44,
	KEEP_ALIVE_CONTROL_CMD=48,
	DISCONNECT_DECISION_CTRL_CMD=49,
	REMOTE_WAKE_CTRL_CMD=60,
	H2C_92C_CMD_MAX};

struct cmd_msg_parm {
	u8 eid; //element id
	u8 sz; // sz
	u8 buf[6];
};

enum evt_msg_element_id
{	
	EVT_DBG_EID=0,
	EVT_TSF_EID=1,
	EVT_AP_RPT_RSP_EID=2,
	EVT_CCX_TXRPT_EID=3,
	EVT_BT_RSSI_EID=4,
	EVT_BT_OPMODE_EID=5,
	EVT_EXT_RA_RPT_EID=6,
	EVT_BT_TYPE_RPT_EID=7,
	EVT_INIT_OFFLOAD_EID=8,
	EVT_PSD_CONTROL_EID=9,
	EVT_HW_INFO_EXCHGNGE_EID=10,
	EVT_C2H_H2C_TEST_EID=11,
	EVT_BT_INTO_EID=12,
	EVT_BT_RPT_EID=13,
	H2C_92C_EVT_MAX};


typedef struct _SETPWRMODE_PARM{
	u8 	Mode;
	u8 	SmartPS;
	u8	BcnPassTime;	// unit: 100ms
}SETPWRMODE_PARM, *PSETPWRMODE_PARM;

typedef struct _SETWOWLAN_PARM{
	u8 	mode;
	u8 	gpio_index;
	u8	gpio_duration;	
	u8  second_mode;
	u8  reserve;
}SETWOWLAN_PARM, *PSETWOWLAN_PARM;

#define FW_WOWLAN_FUN_EN			BIT(0)
#define FW_WOWLAN_PATTERN_MATCH		BIT(1)
#define FW_WOWLAN_MAGIC_PKT			BIT(2)
#define FW_WOWLAN_UNICAST			BIT(3)
#define FW_WOWLAN_ALL_PKT_DROP		BIT(4)
#define FW_WOWLAN_GPIO_ACTIVE		BIT(5)
#define FW_WOWLAN_REKEY_WAKEUP		BIT(6)
#define FW_WOWLAN_DEAUTH_WAKEUP		BIT(7)

#define FW_WOWLAN_GPIO_WAKEUP_EN	BIT(0)
#define FW_FW_PARSE_MAGIC_PKT		BIT(1)

struct H2C_SS_RFOFF_PARAM{
	u8 	ROFOn; // 1: on, 0:off
	u16	gpio_period; // unit: 1024 us
}__attribute__ ((packed));


typedef struct JOINBSSRPT_PARM{
	u8	OpMode;	// RT_MEDIA_STATUS
}JOINBSSRPT_PARM, *PJOINBSSRPT_PARM;

typedef struct _RSVDPAGE_LOC{
	u8 	LocProbeRsp;
	u8 	LocPsPoll;
	u8	LocNullData;
}RSVDPAGE_LOC, *PRSVDPAGE_LOC;

struct P2P_PS_Offload_t {
 unsigned char Offload_En:1;
 unsigned char role:1; // 1: Owner, 0: Client
 unsigned char CTWindow_En:1;
 unsigned char NoA0_En:1;
 unsigned char NoA1_En:1;
 unsigned char AllStaSleep:1; // Only valid in Owner
 unsigned char discovery:1;
 unsigned char rsvd:1;
};

struct P2P_PS_CTWPeriod_t {
    unsigned char CTWPeriod;	//TU
};

// host message to firmware cmd
void	rtl8192c_set_FwPwrMode_cmd(_adapter*padapter, u8 Mode);
void	rtl8192c_set_FwJoinBssReport_cmd(_adapter* padapter, u8 mstatus);
u8	rtl8192c_set_rssi_cmd(_adapter*padapter, u8 *param);
u8	rtl8192c_set_raid_cmd(_adapter*padapter, u32 mask, u8 arg);
u8	rtl8192c_set_raid64_cmd(_adapter*padapter, u32 mask, u8 arg);
void	rtl8192c_Add_RateATid(PADAPTER pAdapter, u32 bitmap, u8 arg, u8 mac_id);
u8	rtl8192c_set_FwSelectSuspend_cmd(_adapter*padapter,u8 bfwpoll, u16 period);
#ifdef CONFIG_P2P
void	rtl8192c_set_p2p_ps_offload_cmd(_adapter* padapter, u8 p2p_ps_state);
#endif //CONFIG_P2P

#ifdef CONFIG_IOL
typedef struct _IO_OFFLOAD_LOC{
	u8 	LocCmd;
}IO_OFFLOAD_LOC, *PIO_OFFLOAD_LOC;
int rtl8192c_IOL_exec_cmds_sync(ADAPTER *adapter, struct xmit_frame *xmit_frame, u32 max_wating_ms);
#endif //CONFIG_IOL

#endif
#ifdef CONFIG_WOWLAN
void rtl8192c_set_wowlan_cmd(_adapter* padapter);
void SetFwRelatedForWoWLAN8192CU(_adapter* 	padapter,u8 bHostIsGoingtoSleep);
#endif // CONFIG_WOWLAN
