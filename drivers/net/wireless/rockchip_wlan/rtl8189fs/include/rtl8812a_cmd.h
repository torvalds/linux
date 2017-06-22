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
	H2C_8812_TxBF = 0x41,
	H2C_8812_RSSI_REPORT = 0x42,
	H2C_8812_IQ_CALIBRATION = 0x45,
	H2C_8812_RA_PARA_ADJUST = 0x46,

	H2C_8812_BT_FW_PATCH = 0x6a,

	H2C_8812_WO_WLAN = 0x80,
	H2C_8812_REMOTE_WAKE_CTRL = 0x81,
	H2C_8812_AOAC_GLOBAL_INFO = 0x82,
	H2C_8812_AOAC_RSVDPAGE = 0x83,
	H2C_8812_FW_SWCHANNL = 0x87,

	H2C_8812_TSF_RESET = 0xC0,

	MAX_8812_H2CCMD
}RTL8812_H2C_CMD;

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
#define SET_8812_H2CCMD_RSVDPAGE_LOC_PROBE_RSP(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE(__pH2CCmd, 0, 8, __Value)
#define SET_8812_H2CCMD_RSVDPAGE_LOC_PSPOLL(__pH2CCmd, __Value)				SET_BITS_TO_LE_1BYTE((__pH2CCmd)+1, 0, 8, __Value)
#define SET_8812_H2CCMD_RSVDPAGE_LOC_NULL_DATA(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE((__pH2CCmd)+2, 0, 8, __Value)
#define SET_8812_H2CCMD_RSVDPAGE_LOC_QOS_NULL_DATA(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE((__pH2CCmd)+3, 0, 8, __Value)
#define SET_8812_H2CCMD_RSVDPAGE_LOC_BT_QOS_NULL_DATA(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE((__pH2CCmd)+4, 0, 8, __Value)

//_SETPWRMODE_PARM
#define SET_8812_H2CCMD_PWRMODE_PARM_MODE(__pH2CCmd, __Value)				SET_BITS_TO_LE_1BYTE(__pH2CCmd, 0, 8, __Value)
#define SET_8812_H2CCMD_PWRMODE_PARM_RLBM(__pH2CCmd, __Value)				SET_BITS_TO_LE_1BYTE((__pH2CCmd)+1, 0, 4, __Value)
#define SET_8812_H2CCMD_PWRMODE_PARM_SMART_PS(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE((__pH2CCmd)+1, 4, 4, __Value)
#define SET_8812_H2CCMD_PWRMODE_PARM_BCN_PASS_TIME(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE((__pH2CCmd)+2, 0, 8, __Value)
#define SET_8812_H2CCMD_PWRMODE_PARM_ALL_QUEUE_UAPSD(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE((__pH2CCmd)+3, 0, 8, __Value)
#define SET_8812_H2CCMD_PWRMODE_PARM_PWR_STATE(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE((__pH2CCmd)+4, 0, 8, __Value)
#define SET_8812_H2CCMD_PWRMODE_PARM_BYTE5(__pH2CCmd, __Value)				SET_BITS_TO_LE_1BYTE((__pH2CCmd)+5, 0, 8, __Value)

#define GET_8812_H2CCMD_PWRMODE_PARM_MODE(__pH2CCmd)							LE_BITS_TO_1BYTE(__pH2CCmd, 0, 8)

//_P2P_PS_OFFLOAD
#define SET_8812_H2CCMD_P2P_PS_OFFLOAD_ENABLE(__pH2CCmd, __Value)				SET_BITS_TO_LE_1BYTE(__pH2CCmd, 0, 1, __Value)
#define SET_8812_H2CCMD_P2P_PS_OFFLOAD_ROLE(__pH2CCmd, __Value)				SET_BITS_TO_LE_1BYTE(__pH2CCmd, 1, 1, __Value)
#define SET_8812_H2CCMD_P2P_PS_OFFLOAD_CTWINDOW_EN(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd, 2, 1, __Value)
#define SET_8812_H2CCMD_P2P_PS_OFFLOAD_NOA0_EN(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE(__pH2CCmd, 3, 1, __Value)
#define SET_8812_H2CCMD_P2P_PS_OFFLOAD_NOA1_EN(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE(__pH2CCmd, 4, 1, __Value)
#define SET_8812_H2CCMD_P2P_PS_OFFLOAD_ALLSTASLEEP(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd, 5, 1, __Value)
#define SET_8812_H2CCMD_P2P_PS_OFFLOAD_DISCOVERY(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE(__pH2CCmd, 6, 1, __Value)


void	Set_RA_LDPC_8812(struct sta_info	*psta, BOOLEAN bLDPC);

// host message to firmware cmd
s32 FillH2CCmd_8812(PADAPTER padapter, u8 ElementID, u32 CmdLen, u8 *pCmdBuffer);
void rtl8812_set_FwPwrMode_cmd(PADAPTER padapter, u8 PSMode);
void rtl8812_set_FwJoinBssReport_cmd(PADAPTER padapter, u8 mstatus);
u8 rtl8812_set_rssi_cmd(PADAPTER padapter, u8 *param);
void rtl8812_set_raid_cmd(PADAPTER padapter, u32 bitmap, u8* arg);
void rtl8812_Add_RateATid(PADAPTER padapter, u64 rate_bitmap, u8 *arg, u8 rssi_level);
void rtl8812_set_wowlan_cmd(_adapter* padapter, u8 enable);
s32 FillH2CCmd_8812(PADAPTER padapter, u8 ElementID, u32 CmdLen, u8 *pCmdBuffer);
u8 GetTxBufferRsvdPageNum8812(_adapter *padapter, bool wowlan);

#ifdef CONFIG_BT_COEXIST
void rtl8812a_download_BTCoex_AP_mode_rsvd_page(PADAPTER padapter);
#endif // CONFIG_BT_COEXIST
#ifdef CONFIG_P2P_PS
void rtl8812_set_p2p_ps_offload_cmd(PADAPTER padapter, u8 p2p_ps_state);
#endif //CONFIG_P2P

void CheckFwRsvdPageContent(PADAPTER padapter);

#ifdef CONFIG_TSF_RESET_OFFLOAD
int reset_tsf(PADAPTER Adapter, u8 reset_port );
#endif	// CONFIG_TSF_RESET_OFFLOAD

//------------------------------------
//     C2H format
//------------------------------------

// TX Beamforming
#define GET_8812_C2H_TXBF_ORIGINATE(_Header)			LE_BITS_TO_1BYTE(_Header, 0, 8)
#define GET_8812_C2H_TXBF_MACID(_Header)				LE_BITS_TO_1BYTE((_Header + 1), 0, 8)



/// TX Feedback Content
#define 	USEC_UNIT_FOR_8812_C2H_TX_RPT_QUEUE_TIME			256

#define	GET_8812_C2H_TX_RPT_QUEUE_SELECT(_Header)			LE_BITS_TO_1BYTE((_Header + 0), 0, 5)
#define	GET_8812_C2H_TX_RPT_PKT_BROCAST(_Header)			LE_BITS_TO_1BYTE((_Header + 0), 5, 1)
#define	GET_8812_C2H_TX_RPT_LIFE_TIME_OVER(_Header)			LE_BITS_TO_1BYTE((_Header + 0), 6, 1)
#define	GET_8812_C2H_TX_RPT_RETRY_OVER(_Header)				LE_BITS_TO_1BYTE((_Header + 0), 7, 1)
#define	GET_8812_C2H_TX_RPT_MAC_ID(_Header)					LE_BITS_TO_1BYTE((_Header + 1), 0, 8)
#define	GET_8812_C2H_TX_RPT_DATA_RETRY_CNT(_Header)		LE_BITS_TO_1BYTE((_Header + 2), 0, 6)
#define	GET_8812_C2H_TX_RPT_QUEUE_TIME(_Header)				LE_BITS_TO_2BYTE((_Header + 3), 0, 16)	// In unit of 256 microseconds.
#define	GET_8812_C2H_TX_RPT_FINAL_DATA_RATE(_Header)		LE_BITS_TO_1BYTE((_Header + 5), 0, 8)

// BT_FW_PATCH
#define SET_8812_H2CCMD_BT_FW_PATCH_SIZE(__pH2CCmd, __Value)					SET_BITS_TO_LE_2BYTE((pu1Byte)(__pH2CCmd), 0, 16, __Value)
#define SET_8812_H2CCMD_BT_FW_PATCH_ADDR0(__pH2CCmd, __Value)					SET_BITS_TO_LE_1BYTE((pu1Byte)(__pH2CCmd)+2, 0, 8, __Value)
#define SET_8812_H2CCMD_BT_FW_PATCH_ADDR1(__pH2CCmd, __Value)					SET_BITS_TO_LE_1BYTE((pu1Byte)(__pH2CCmd)+3, 0, 8, __Value)
#define SET_8812_H2CCMD_BT_FW_PATCH_ADDR2(__pH2CCmd, __Value)					SET_BITS_TO_LE_1BYTE((pu1Byte)(__pH2CCmd)+4, 0, 8, __Value)
#define SET_8812_H2CCMD_BT_FW_PATCH_ADDR3(__pH2CCmd, __Value)					SET_BITS_TO_LE_1BYTE((pu1Byte)(__pH2CCmd)+5, 0, 8, __Value)

int	rtl8812_iqk_wait(_adapter* padapter, u32 timeout_ms);
void	rtl8812_iqk_done(_adapter* padapter);

s32
_C2HContentParsing8812(
	IN	PADAPTER	Adapter,
	IN	u8			c2hCmdId, 
	IN	u8			c2hCmdLen,
	IN	u8 			*tmpBuf
);
void	C2HPacketHandler_8812(PADAPTER Adapter, u8 *Buffer, u8 Length);

#endif//__RTL8812A_CMD_H__

