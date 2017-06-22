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
#ifndef __RTL8814A_CMD_H__
#define __RTL8814A_CMD_H__
#include "hal_com_h2c.h"

//_RSVDPAGE_LOC_CMD0
#define SET_8814A_H2CCMD_RSVDPAGE_LOC_PROBE_RSP(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd, 0, 8, __Value)
#define SET_8814A_H2CCMD_RSVDPAGE_LOC_PSPOLL(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE((__pH2CCmd)+1, 0, 8, __Value)
#define SET_8814A_H2CCMD_RSVDPAGE_LOC_NULL_DATA(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE((__pH2CCmd)+2, 0, 8, __Value)
#define SET_8814A_H2CCMD_RSVDPAGE_LOC_QOS_NULL_DATA(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE((__pH2CCmd)+3, 0, 8, __Value)
#define SET_8814A_H2CCMD_RSVDPAGE_LOC_BT_QOS_NULL_DATA(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE((__pH2CCmd)+4, 0, 8, __Value)

//_SETPWRMODE_PARM
#define SET_8814A_H2CCMD_PWRMODE_PARM_MODE(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE(__pH2CCmd, 0, 8, __Value)
#define SET_8814A_H2CCMD_PWRMODE_PARM_RLBM(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE((__pH2CCmd)+1, 0, 4, __Value)
#define SET_8814A_H2CCMD_PWRMODE_PARM_SMART_PS(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE((__pH2CCmd)+1, 4, 4, __Value)
#define SET_8814A_H2CCMD_PWRMODE_PARM_BCN_PASS_TIME(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE((__pH2CCmd)+2, 0, 8, __Value)
#define SET_8814A_H2CCMD_PWRMODE_PARM_ALL_QUEUE_UAPSD(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE((__pH2CCmd)+3, 0, 8, __Value)
#define SET_8814A_H2CCMD_PWRMODE_PARM_PWR_STATE(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE((__pH2CCmd)+4, 0, 8, __Value)
#define SET_8814A_H2CCMD_PWRMODE_PARM_BYTE5(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE((__pH2CCmd)+5, 0, 8, __Value)

#define GET_8814A_H2CCMD_PWRMODE_PARM_MODE(__pH2CCmd)					LE_BITS_TO_1BYTE(__pH2CCmd, 0, 8)


// _WoWLAN PARAM_CMD5
#define SET_8814A_H2CCMD_WOWLAN_FUNC_ENABLE(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd, 0, 1, __Value)
#define SET_8814A_H2CCMD_WOWLAN_PATTERN_MATCH_ENABLE(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd, 1, 1, __Value)
#define SET_8814A_H2CCMD_WOWLAN_MAGIC_PKT_ENABLE(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd, 2, 1, __Value)
#define SET_8814A_H2CCMD_WOWLAN_UNICAST_PKT_ENABLE(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd, 3, 1, __Value)
#define SET_8814A_H2CCMD_WOWLAN_ALL_PKT_DROP(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd, 4, 1, __Value)
#define SET_8814A_H2CCMD_WOWLAN_GPIO_ACTIVE(__pH2CCmd, __Value)				SET_BITS_TO_LE_1BYTE(__pH2CCmd, 5, 1, __Value)
#define SET_8814A_H2CCMD_WOWLAN_REKEY_WAKE_UP(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd, 6, 1, __Value)
#define SET_8814A_H2CCMD_WOWLAN_DISCONNECT_WAKE_UP(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd, 7, 1, __Value)
#define SET_8814A_H2CCMD_WOWLAN_GPIONUM(__pH2CCmd, __Value)					SET_BITS_TO_LE_1BYTE((__pH2CCmd)+1, 0, 8, __Value)
#define SET_8814A_H2CCMD_WOWLAN_GPIO_DURATION(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE((__pH2CCmd)+2, 0, 8, __Value)


//WLANINFO_PARM
#define SET_8814A_H2CCMD_WLANINFO_PARM_OPMODE(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd, 0, 8, __Value)
#define SET_8814A_H2CCMD_WLANINFO_PARM_CHANNEL(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE((__pH2CCmd)+1, 0, 8, __Value)
#define SET_8814A_H2CCMD_WLANINFO_PARM_BW40MHZ(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE((__pH2CCmd)+2, 0, 8, __Value)

// _REMOTE_WAKEUP_CMD7
#define SET_8814A_H2CCMD_REMOTE_WAKECTRL_ENABLE(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd, 0, 1, __Value)
#define SET_8814A_H2CCMD_REMOTE_WAKE_CTRL_ARP_OFFLOAD_EN(__pH2CCmd, __Value)				SET_BITS_TO_LE_1BYTE(__pH2CCmd, 1, 1, __Value)
#define SET_8814A_H2CCMD_REMOTE_WAKE_CTRL_NDP_OFFLOAD_EN(__pH2CCmd, __Value)				SET_BITS_TO_LE_1BYTE(__pH2CCmd, 2, 1, __Value)
#define SET_8814A_H2CCMD_REMOTE_WAKE_CTRL_GTK_OFFLOAD_EN(__pH2CCmd, __Value)				SET_BITS_TO_LE_1BYTE(__pH2CCmd, 3, 1, __Value)


// _AP_OFFLOAD_CMD8
#define SET_8814A_H2CCMD_AP_OFFLOAD_ON(__pH2CCmd, __Value)				SET_BITS_TO_LE_1BYTE(__pH2CCmd, 0, 8, __Value)
#define SET_8814A_H2CCMD_AP_OFFLOAD_HIDDEN(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE((__pH2CCmd)+1, 0, 8, __Value)
#define SET_8814A_H2CCMD_AP_OFFLOAD_DENYANY(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE((__pH2CCmd)+2, 0, 8, __Value)
#define SET_8814A_H2CCMD_AP_OFFLOAD_WAKEUP_EVT_RPT(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE((__pH2CCmd)+3, 0, 8, __Value)

// _PWR_MOD_CMD20
#define SET_88E_H2CCMD_PWRMODE_PARM_MODE(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE(__pH2CCmd, 0, 8, __Value)
#define SET_88E_H2CCMD_PWRMODE_PARM_RLBM(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE((__pH2CCmd)+1, 0, 4, __Value)
#define SET_88E_H2CCMD_PWRMODE_PARM_SMART_PS(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE((__pH2CCmd)+1, 4, 4, __Value)
#define SET_88E_H2CCMD_PWRMODE_PARM_BCN_PASS_TIME(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE((__pH2CCmd)+2, 0, 8, __Value)
#define SET_88E_H2CCMD_PWRMODE_PARM_ALL_QUEUE_UAPSD(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE((__pH2CCmd)+3, 0, 8, __Value)
#define SET_88E_H2CCMD_PWRMODE_PARM_PWR_STATE(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE((__pH2CCmd)+4, 0, 8, __Value)

/*BCNHWSEQ*/
#define SET_8814A_H2CCMD_BCNHWSEQ_EN(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE((__pH2CCmd), 0, 1, __Value)
#define SET_8814A_H2CCMD_BCNHWSEQ_BCN_NUMBER(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE((__pH2CCmd), 1, 3, __Value)
#define SET_8814A_H2CCMD_BCNHWSEQ_HWSEQ(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE((__pH2CCmd), 6, 1, __Value)
#define SET_8814A_H2CCMD_BCNHWSEQ_EXHWSEQ(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE((__pH2CCmd), 7, 1, __Value)
#define SET_8814A_H2CCMD_BCNHWSEQ_PAGE(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE((__pH2CCmd)+1, 0, 8, __Value)
void rtl8814_fw_update_beacon_cmd(_adapter *padapter);

// TX Beamforming
#define GET_8814A_C2H_TXBF_ORIGINATE(_Header)			LE_BITS_TO_1BYTE(_Header, 0, 8)
#define GET_8814A_C2H_TXBF_MACID(_Header)				LE_BITS_TO_1BYTE((_Header + 1), 0, 8)



/// TX Feedback Content
#define 	USEC_UNIT_FOR_8814A_C2H_TX_RPT_QUEUE_TIME			256

#define	GET_8814A_C2H_TX_RPT_QUEUE_SELECT(_Header)			LE_BITS_TO_1BYTE((_Header + 0), 0, 5)
#define	GET_8814A_C2H_TX_RPT_PKT_BROCAST(_Header)			LE_BITS_TO_1BYTE((_Header + 0), 5, 1)
#define	GET_8814A_C2H_TX_RPT_LIFE_TIME_OVER(_Header)			LE_BITS_TO_1BYTE((_Header + 0), 6, 1)
#define	GET_8814A_C2H_TX_RPT_RETRY_OVER(_Header)				LE_BITS_TO_1BYTE((_Header + 0), 7, 1)
#define	GET_8814A_C2H_TX_RPT_MAC_ID(_Header)					LE_BITS_TO_1BYTE((_Header + 1), 0, 8)
#define	GET_8814A_C2H_TX_RPT_DATA_RETRY_CNT(_Header)		LE_BITS_TO_1BYTE((_Header + 2), 0, 6)
#define	GET_8814A_C2H_TX_RPT_QUEUE_TIME(_Header)				LE_BITS_TO_2BYTE((_Header + 3), 0, 16)	// In unit of 256 microseconds.
#define	GET_8814A_C2H_TX_RPT_FINAL_DATA_RATE(_Header)		LE_BITS_TO_1BYTE((_Header + 5), 0, 8)


//_P2P_PS_OFFLOAD
#define SET_8814A_H2CCMD_P2P_PS_OFFLOAD_ENABLE(__pH2CCmd, __Value)				SET_BITS_TO_LE_1BYTE(__pH2CCmd, 0, 1, __Value)
#define SET_8814A_H2CCMD_P2P_PS_OFFLOAD_ROLE(__pH2CCmd, __Value)				SET_BITS_TO_LE_1BYTE(__pH2CCmd, 1, 1, __Value)
#define SET_8814A_H2CCMD_P2P_PS_OFFLOAD_CTWINDOW_EN(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd, 2, 1, __Value)
#define SET_8814A_H2CCMD_P2P_PS_OFFLOAD_NOA0_EN(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE(__pH2CCmd, 3, 1, __Value)
#define SET_8814A_H2CCMD_P2P_PS_OFFLOAD_NOA1_EN(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE(__pH2CCmd, 4, 1, __Value)
#define SET_8814A_H2CCMD_P2P_PS_OFFLOAD_ALLSTASLEEP(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd, 5, 1, __Value)
#define SET_8814A_H2CCMD_P2P_PS_OFFLOAD_DISCOVERY(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE(__pH2CCmd, 6, 1, __Value)

s32 FillH2CCmd_8814(PADAPTER padapter, u8 ElementID, u32 CmdLen, u8 *pCmdBuffer);
void rtl8814_set_raid_cmd(PADAPTER padapter, u64 bitmap, u8* arg);
void rtl8814_Add_RateATid(PADAPTER padapter, u64 rate_bitmap, u8 *arg, u8 rssi_level);
void rtl8814_set_wowlan_cmd(_adapter* padapter, u8 enable);
void rtl8814_set_FwJoinBssReport_cmd(PADAPTER padapter, u8 mstatus);
void rtl8814_set_FwPwrMode_cmd(PADAPTER padapter, u8 PSMode);
u8 GetTxBufferRsvdPageNum8814(_adapter *padapter, bool wowlan);
u8 rtl8814_set_rssi_cmd(_adapter*padapter, u8 *param);

void
Set_RA_LDPC_8814(
	struct sta_info	*psta,
	BOOLEAN			bLDPC
	);
int rtl8814_iqk_wait(_adapter* padapter, u32 timeout_ms);
void rtl8814_iqk_done(_adapter* padapter);
VOID
C2HPacketHandler_8814(
	IN	PADAPTER	Adapter,
	IN	u8			*Buffer,
	IN	u8			Length
	);
#ifdef CONFIG_P2P_PS
void rtl8814_set_p2p_ps_offload_cmd(PADAPTER padapter, u8 p2p_ps_state);
#endif //CONFIG_P2P

s32
_C2HContentParsing8814(
	IN	PADAPTER	Adapter,
	IN	u8			c2hCmdId, 
	IN	u8			c2hCmdLen,
	IN	u8 			*tmpBuf
);

#endif/* __RTL8814A_CMD_H__ */

