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
#ifndef __RTL8188E_CMD_H__
#define __RTL8188E_CMD_H__

#if 0
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
	P2P_PS_CTW_CMD_EID = 32,
	MAX_CMDMSG_EID	 
};
#else
typedef enum _RTL8188E_H2C_CMD_ID
{
	//Class Common
	H2C_COM_RSVD_PAGE			=0x00,
	H2C_COM_MEDIA_STATUS_RPT	=0x01,
	H2C_COM_SCAN					=0x02,	
	H2C_COM_KEEP_ALIVE			=0x03,	
	H2C_COM_DISCNT_DECISION		=0x04,
#ifndef CONFIG_WOWLAN
	H2C_COM_WWLAN				=0x05,
#endif
	H2C_COM_INIT_OFFLOAD			=0x06,
	H2C_COM_REMOTE_WAKE_CTL	=0x07,
	H2C_COM_AP_OFFLOAD			=0x08,
	H2C_COM_BCN_RSVD_PAGE		=0x09,
	H2C_COM_PROB_RSP_RSVD_PAGE	=0x0A,

	//Class PS
	H2C_PS_PWR_MODE				=0x20,
	H2C_PS_TUNE_PARA				=0x21,
	H2C_PS_TUNE_PARA_2			=0x22,
	H2C_PS_LPS_PARA				=0x23,
	H2C_PS_P2P_OFFLOAD			=0x24,

	//Class DM
	H2C_DM_MACID_CFG				=0x40,
	H2C_DM_TXBF					=0x41,
	H2C_RSSI_REPORT 				=0x42,
	//Class BT
	H2C_BT_COEX_MASK				=0x60,
	H2C_BT_COEX_GPIO_MODE		=0x61,
	H2C_BT_DAC_SWING_VAL			=0x62,
	H2C_BT_PSD_RST				=0x63,
	
	//Class Remote WakeUp
#ifdef CONFIG_WOWLAN
	H2C_COM_WWLAN				=0x80,
	H2C_COM_REMOTE_WAKE_CTRL	=0x81,
	H2C_COM_AOAC_GLOBAL_INFO	=0x82,
	H2C_COM_AOAC_RSVD_PAGE		=0x83,
#endif

	//Class 
	 //H2C_RESET_TSF				=0xc0,
}RTL8188E_H2C_CMD_ID;
	
#endif


struct cmd_msg_parm {
	u8 eid; //element id
	u8 sz; // sz
	u8 buf[6];
};

enum{
	PWRS
};

typedef struct _SETPWRMODE_PARM {
	u8 Mode;//0:Active,1:LPS,2:WMMPS
	//u8 RLBM:4;//0:Min,1:Max,2: User define
	u8 SmartPS_RLBM;//LPS=0:PS_Poll,1:PS_Poll,2:NullData,WMM=0:PS_Poll,1:NullData
	u8 AwakeInterval;	// unit: beacon interval
	u8 bAllQueueUAPSD;
	u8 PwrState;//AllON(0x0c),RFON(0x04),RFOFF(0x00)
} SETPWRMODE_PARM, *PSETPWRMODE_PARM;

struct H2C_SS_RFOFF_PARAM{
	u8 ROFOn; // 1: on, 0:off
	u16 gpio_period; // unit: 1024 us
}__attribute__ ((packed));


typedef struct JOINBSSRPT_PARM_88E{
	u8 OpMode;	// RT_MEDIA_STATUS
#ifdef CONFIG_WOWLAN
	u8 MacID;       // MACID
#endif //CONFIG_WOWLAN
}JOINBSSRPT_PARM_88E, *PJOINBSSRPT_PARM_88E;

/* move to hal_com_h2c.h
typedef struct _RSVDPAGE_LOC_88E {
	u8 LocProbeRsp;
	u8 LocPsPoll;
	u8 LocNullData;
	u8 LocQosNull;
	u8 LocBTQosNull;
#ifdef CONFIG_WOWLAN
	u8 LocRemoteCtrlInfo;
	u8 LocArpRsp;
	u8 LocNbrAdv;
	u8 LocGTKRsp;
	u8 LocGTKInfo;
	u8 LocProbeReq;
	u8 LocNetList;
#endif //CONFIG_WOWLAN	
} RSVDPAGE_LOC_88E, *PRSVDPAGE_LOC_88E;
*/

// host message to firmware cmd
void rtl8188e_set_FwPwrMode_cmd(PADAPTER padapter, u8 Mode);
void rtl8188e_set_FwJoinBssReport_cmd(PADAPTER padapter, u8 mstatus);
u8 rtl8188e_set_rssi_cmd(PADAPTER padapter, u8 *param);
u8 rtl8188e_set_raid_cmd(_adapter *padapter, u32 bitmap, u8 *arg);
void rtl8188e_Add_RateATid(PADAPTER padapter, u64 rate_bitmap, u8 *arg, u8 rssi_level);
s32 FillH2CCmd_88E(PADAPTER padapter, u8 ElementID, u32 CmdLen, u8 *pCmdBuffer);
//u8 rtl8192c_set_FwSelectSuspend_cmd(PADAPTER padapter, u8 bfwpoll, u16 period);
u8 GetTxBufferRsvdPageNum8188E(_adapter *padapter, bool wowlan);


#ifdef CONFIG_P2P
void rtl8188e_set_p2p_ps_offload_cmd(PADAPTER padapter, u8 p2p_ps_state);
#endif //CONFIG_P2P

void CheckFwRsvdPageContent(PADAPTER padapter);

#ifdef CONFIG_TSF_RESET_OFFLOAD
//u8 rtl8188e_reset_tsf(_adapter *padapter, u8 reset_port);
int reset_tsf(PADAPTER Adapter, u8 reset_port );
#endif	// CONFIG_TSF_RESET_OFFLOAD

//#define H2C_8188E_RSVDPAGE_LOC_LEN      5
//#define H2C_8188E_AOAC_RSVDPAGE_LOC_LEN 7

//---------------------------------------------------------------------------------------------------------//
//----------------------------------    H2C CMD CONTENT    --------------------------------------------------//
//---------------------------------------------------------------------------------------------------------//
//
/* move to hal_com_h2c.h
//_RSVDPAGE_LOC_CMD_0x00
#define SET_8188E_H2CCMD_RSVDPAGE_LOC_PROBE_RSP(__pH2CCmd, __Value)     SET_BITS_TO_LE_1BYTE(__pH2CCmd, 0, 8, __Value)
#define SET_8188E_H2CCMD_RSVDPAGE_LOC_PSPOLL(__pH2CCmd, __Value)            SET_BITS_TO_LE_1BYTE((__pH2CCmd)+1, 0, 8, __Value)
#define SET_8188E_H2CCMD_RSVDPAGE_LOC_NULL_DATA(__pH2CCmd, __Value)     SET_BITS_TO_LE_1BYTE((__pH2CCmd)+2, 0, 8, __Value)
#define SET_8188E_H2CCMD_RSVDPAGE_LOC_QOS_NULL_DATA(__pH2CCmd, __Value)     SET_BITS_TO_LE_1BYTE((__pH2CCmd)+3, 0, 8, __Value)
// AOAC_RSVDPAGE_LOC_0x83
#define SET_8188E_H2CCMD_AOAC_RSVDPAGE_LOC_REMOTE_WAKE_CTRL_INFO(__pH2CCmd, __Value)        SET_BITS_TO_LE_1BYTE((__pH2CCmd), 0, 8, __Value)
#define SET_8188E_H2CCMD_AOAC_RSVDPAGE_LOC_ARP_RSP(__pH2CCmd, __Value)                  SET_BITS_TO_LE_1BYTE((__pH2CCmd)+1, 0, 8, __Value)
*/
#endif//__RTL8188E_CMD_H__


