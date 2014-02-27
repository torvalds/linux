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
#ifndef __RTL8192D_CMD_H_
#define __RTL8192D_CMD_H_


//--------------------------------------------
//3				Host Message Box 
//--------------------------------------------

// User Define Message [31:8]

//_SETPWRMODE_PARM
#define SET_H2CCMD_PWRMODE_PARM_MODE(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE(__pH2CCmd, 0, 8, __Value)
#define SET_H2CCMD_PWRMODE_PARM_SMART_PS(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE((__pH2CCmd)+1, 0, 8, __Value)
#define SET_H2CCMD_PWRMODE_PARM_BCN_PASS_TIME(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE((__pH2CCmd)+2, 0, 8, __Value)

//JOINBSSRPT_PARM
#define SET_H2CCMD_JOINBSSRPT_PARM_OPMODE(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd, 0, 8, __Value)

//_RSVDPAGE_LOC
#define SET_H2CCMD_RSVDPAGE_LOC_PROBE_RSP(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd, 0, 8, __Value)
#define SET_H2CCMD_RSVDPAGE_LOC_PSPOLL(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE((__pH2CCmd)+1, 0, 8, __Value)
#define SET_H2CCMD_RSVDPAGE_LOC_NULL_DATA(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE((__pH2CCmd)+2, 0, 8, __Value)

//P2P_PS_OFFLOAD

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

#define SET_H2CCMD_P2P_PS_OFFLOAD_EN(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd, 0, 1, __Value)
#define SET_H2CCMD_P2P_PS_OFFLOAD_ROLE(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd, 1, 1, __Value)
#define SET_H2CCMD_P2P_PS_OFFLOAD_CTW(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd, 2, 1, __Value)
#define SET_H2CCMD_P2P_PS_OFFLOAD_NOA0(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd, 3, 1, __Value)
#define SET_H2CCMD_P2P_PS_OFFLOAD_NOA1(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd, 4, 1, __Value)
#define SET_H2CCMD_P2P_PS_OFFLOAD_ALLSTASLEEP(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd, 5, 1, __Value)
#define SET_H2CCMD_P2P_PS_OFFLOAD_DISCOVERY(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd, 6, 1, __Value)

// Description: Determine the types of H2C commands that are the same in driver and Fw.
// Fisrt constructed by tynli. 2009.10.09.
typedef enum _RTL8192D_H2C_CMD 
{
	H2C_AP_OFFLOAD = 0,		/*0*/
	H2C_SETPWRMODE = 1,		/*1*/
	H2C_JOINBSSRPT = 2,		/*2*/
	H2C_RSVDPAGE = 3,
	H2C_RSSI_REPORT = 5,
	H2C_RA_MASK = 6,
	H2C_P2P_PS_OFFLOAD = 8,
	H2C_MAC_MODE_SEL = 9,
	H2C_PWRM=15,
#ifdef CONFIG_WOWLAN
	H2C_WO_WLAN_CMD = 20,	// Wake on Wlan.
#endif // CONFIG_WOWLAN
	H2C_P2P_PS_CTW_CMD = 24,
	H2C_PathDiv = 26,                  //PathDiv--NeilChen--2011.07.15
#ifdef CONFIG_WOWLAN
	KEEP_ALIVE_CONTROL_CMD=31, //keep alive for wake on wlan
	DISCONNECT_DECISION_CTRL_CMD=32,
	REMOTE_WAKE_CTRL_CMD=34,
#endif // CONFIG_WOWLAN
	H2C_92D_TSF_SYNC=36,
	H2C_92D_RESET_TSF = 43,
	H2C_CMD_MAX
}RTL8192D_H2C_CMD;

struct cmd_msg_parm {
	u8 eid; //element id
	u8 sz; // sz
	u8 buf[6];
};


void	FillH2CCmd92D(_adapter* padapter, u8 ElementID, u32 CmdLen, u8* pCmdBuffer);

// host message to firmware cmd
void	rtl8192d_set_FwPwrMode_cmd(_adapter*padapter, u8 Mode);
void	rtl8192d_set_FwJoinBssReport_cmd(_adapter* padapter, u8 mstatus);
u8	rtl8192d_set_rssi_cmd(_adapter*padapter, u8 *param);
u8	rtl8192d_set_raid_cmd(_adapter*padapter, u32 mask, u8 arg);
void	rtl8192d_Add_RateATid(PADAPTER pAdapter, u32 bitmap, u8 arg);
#ifdef CONFIG_P2P
void	rtl8192d_set_p2p_ps_offload_cmd(_adapter* padapter, u8 p2p_ps_state);
#endif //CONFIG_P2P

#ifdef CONFIG_TSF_RESET_OFFLOAD
int reset_tsf(PADAPTER Adapter, u8 reset_port );
#endif	// CONFIG_TSF_RESET_OFFLOAD

#ifdef CONFIG_WOWLAN
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

void rtl8192d_set_wowlan_cmd(_adapter* padapter);
void SetFwRelatedForWoWLAN8192DU(_adapter* 	padapter,u8 bHostIsGoingtoSleep);
#endif // CONFIG_WOWLAN

#endif	// __RTL8192D_CMD_H_


