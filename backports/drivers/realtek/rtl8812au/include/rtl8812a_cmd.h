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
#ifndef __RTL8812A_CMD_H__
#define __RTL8812A_CMD_H__

typedef enum _RTL8812_H2C_CMD 
{
	H2C_8812_RSVDPAGE = 0,
	H2C_8812_MSRRPT = 1,
	H2C_8812_SCAN = 2,
	H2C_8812_KEEP_ALIVE_CTRL = 3,
	H2C_8812_DISCONNECT_DECISION = 4,

	H2C_8812_INIT_OFFLOAD = 6,		
	H2C_8812_AP_OFFLOAD = 8,
	H2C_8812_BCN_RSVDPAGE = 9,
	H2C_8812_PROBERSP_RSVDPAGE = 10,
	
	H2C_8812_SETPWRMODE = 0x20,		
	H2C_8812_PS_TUNING_PARA = 0x21,
	H2C_8812_PS_TUNING_PARA2 = 0x22,
	H2C_8812_PS_LPS_PARA = 0x23,
	H2C_8812_P2P_PS_OFFLOAD = 0x24,
	H2C_8812_RA_MASK = 0x40,
	H2C_8812_RSSI_REPORT = 0x42,

	H2C_8812_WO_WLAN = 0x80,
	H2C_8812_REMOTE_WAKE_CTRL = 0x81,
	H2C_8812_AOAC_GLOBAL_INFO = 0x82,
	H2C_8812_AOAC_RSVDPAGE = 0x83,

	H2C_8812_TSF_RESET = 0xC0,

	MAX_8812_H2CCMD
}RTL8812_H2C_CMD;


typedef enum _RTL8812_C2H_EVT
{
	C2H_8812_DBG = 0,
	C2H_8812_LB = 1,
	C2H_8812_TXBF = 2,
	C2H_8812_TX_REPORT = 3,
	C2H_8812_BT_INFO = 9,
	C2H_8812_BT_MP = 11,
	C2H_8812_RA_RPT=12,

	C2H_8812_FW_SWCHNL = 0x10,
	C2H_8812_IQK_FINISH = 0x11,
	MAX_8812_C2HEVENT
}RTL8812_C2H_EVT;


struct cmd_msg_parm {
	u8 eid; //element id
	u8 sz; // sz
	u8 buf[6];
};

enum{
	PWRS
};

struct H2C_SS_RFOFF_PARAM{
	u8 ROFOn; // 1: on, 0:off
	u16 gpio_period; // unit: 1024 us
}__attribute__ ((packed));



//_RSVDPAGE_LOC_CMD0
#define SET_8812_H2CCMD_RSVDPAGE_LOC_PROBE_RSP(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd, 0, 8, __Value)
#define SET_8812_H2CCMD_RSVDPAGE_LOC_PSPOLL(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE((__pH2CCmd)+1, 0, 8, __Value)
#define SET_8812_H2CCMD_RSVDPAGE_LOC_NULL_DATA(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE((__pH2CCmd)+2, 0, 8, __Value)
#define SET_8812_H2CCMD_RSVDPAGE_LOC_QOS_NULL_DATA(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE((__pH2CCmd)+3, 0, 8, __Value)
#define SET_8812_H2CCMD_RSVDPAGE_LOC_BT_QOS_NULL_DATA(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE((__pH2CCmd)+4, 0, 8, __Value)

//_MEDIA_STATUS_RPT_PARM_CMD1
#define SET_8812_H2CCMD_MSRRPT_PARM_OPMODE(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd, 0, 1, __Value)
#define SET_8812_H2CCMD_MSRRPT_PARM_MACID_IND(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd, 1, 1, __Value)
#define SET_8812_H2CCMD_MSRRPT_PARM_MACID(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE(__pH2CCmd+1, 0, 8, __Value)
#define SET_8812_H2CCMD_MSRRPT_PARM_MACID_END(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE(__pH2CCmd+2, 0, 8, __Value)

//_SETPWRMODE_PARM
#define SET_8812_H2CCMD_PWRMODE_PARM_MODE(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE(__pH2CCmd, 0, 8, __Value)
#define SET_8812_H2CCMD_PWRMODE_PARM_RLBM(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE((__pH2CCmd)+1, 0, 4, __Value)
#define SET_8812_H2CCMD_PWRMODE_PARM_SMART_PS(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE((__pH2CCmd)+1, 4, 4, __Value)
#define SET_8812_H2CCMD_PWRMODE_PARM_BCN_PASS_TIME(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE((__pH2CCmd)+2, 0, 8, __Value)
#define SET_8812_H2CCMD_PWRMODE_PARM_ALL_QUEUE_UAPSD(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE((__pH2CCmd)+3, 0, 8, __Value)
#define SET_8812_H2CCMD_PWRMODE_PARM_PWR_STATE(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE((__pH2CCmd)+4, 0, 8, __Value)

#define GET_8812_H2CCMD_PWRMODE_PARM_MODE(__pH2CCmd)					LE_BITS_TO_1BYTE(__pH2CCmd, 0, 8)

//_P2P_PS_OFFLOAD
#define SET_8812_H2CCMD_P2P_PS_OFFLOAD_ENABLE(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE(__pH2CCmd, 0, 1, __Value)
#define SET_8812_H2CCMD_P2P_PS_OFFLOAD_ROLE(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE(__pH2CCmd, 1, 1, __Value)
#define SET_8812_H2CCMD_P2P_PS_OFFLOAD_CTWINDOW_EN(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd, 2, 1, __Value)
#define SET_8812_H2CCMD_P2P_PS_OFFLOAD_NOA0_EN(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE(__pH2CCmd, 3, 1, __Value)
#define SET_8812_H2CCMD_P2P_PS_OFFLOAD_NOA1_EN(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE(__pH2CCmd, 4, 1, __Value)
#define SET_8812_H2CCMD_P2P_PS_OFFLOAD_ALLSTASLEEP(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE(__pH2CCmd, 5, 1, __Value)
#define SET_8812_H2CCMD_P2P_PS_OFFLOAD_DISCOVERY(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE(__pH2CCmd, 6, 1, __Value)


void	Set_RA_LDPC_8812(struct sta_info	*psta, BOOLEAN bLDPC);

// host message to firmware cmd
void rtl8812_set_FwPwrMode_cmd(PADAPTER padapter, u8 PSMode);
void rtl8812_set_FwJoinBssReport_cmd(PADAPTER padapter, u8 mstatus);
u8 rtl8812_set_rssi_cmd(PADAPTER padapter, u8 *param);
void rtl8812_set_raid_cmd(PADAPTER padapter, u32 bitmap, u8* arg);
void rtl8812_Add_RateATid(PADAPTER padapter, u32 bitmap, u8* arg, u8 rssi_level);


#ifdef CONFIG_P2P_PS
void rtl8812_set_p2p_ps_offload_cmd(PADAPTER padapter, u8 p2p_ps_state);
#endif //CONFIG_P2P

void CheckFwRsvdPageContent(PADAPTER padapter);
void rtl8812_set_FwMediaStatus_cmd(PADAPTER padapter, u16 mstatus_rpt );

#ifdef CONFIG_TSF_RESET_OFFLOAD
int reset_tsf(PADAPTER Adapter, u8 reset_port );
#endif	// CONFIG_TSF_RESET_OFFLOAD

#ifdef CONFIG_WOWLAN
typedef struct _SETWOWLAN_PARM{
	u8		mode;
	u8		gpio_index;
	u8		gpio_duration;
	u8		second_mode;
	u8		reserve;
}SETWOWLAN_PARM, *PSETWOWLAN_PARM;

#define FW_WOWLAN_FUN_EN				BIT(0)
#define FW_WOWLAN_PATTERN_MATCH			BIT(1)
#define FW_WOWLAN_MAGIC_PKT				BIT(2)
#define FW_WOWLAN_UNICAST				BIT(3)
#define FW_WOWLAN_ALL_PKT_DROP			BIT(4)
#define FW_WOWLAN_GPIO_ACTIVE			BIT(5)
#define FW_WOWLAN_REKEY_WAKEUP			BIT(6)
#define FW_WOWLAN_DEAUTH_WAKEUP			BIT(7)

#define FW_WOWLAN_GPIO_WAKEUP_EN		BIT(0)
#define FW_FW_PARSE_MAGIC_PKT			BIT(1)

#define FW_REMOTE_WAKE_CTRL_EN			BIT(0)
#define FW_REALWOWLAN_EN				BIT(5)
void rtl8812a_set_wowlan_cmd(_adapter* padapter, u8 enable);
void SetFwRelatedForWoWLAN8812(_adapter* padapter, u8 bHostIsGoingtoSleep);
#endif//CONFIG_WOWLAN
#endif//__RTL8188E_CMD_H__


