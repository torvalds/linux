/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2014  Realtek Corporation.*/

#ifndef __RTL8723BE__FW__H__
#define __RTL8723BE__FW__H__

#define FW_8192C_SIZE				0x8000
#define FW_8192C_START_ADDRESS			0x1000
#define FW_8192C_END_ADDRESS			0x5FFF
#define FW_8192C_PAGE_SIZE			4096
#define FW_8192C_POLLING_DELAY			5

#define USE_OLD_WOWLAN_DEBUG_FW			0

#define H2C_PWEMODE_LENGTH			7

/* Fw PS state for RPWM.
*BIT[2:0] = HW state
*BIT[3] = Protocol PS state, 1: register active state , 0: register sleep state
*BIT[4] = sub-state
*/
#define	FW_PS_RF_ON		BIT(2)
#define	FW_PS_REGISTER_ACTIVE	BIT(3)

#define	FW_PS_ACK		BIT(6)
#define	FW_PS_TOGGLE		BIT(7)

 /* 8723BE RPWM value*/
 /* BIT[0] = 1: 32k, 0: 40M*/
#define	FW_PS_CLOCK_OFF		BIT(0)		/* 32k*/
#define	FW_PS_CLOCK_ON		0		/*40M*/

#define	FW_PS_STATE_MASK	(0x0F)
#define	FW_PS_STATE_HW_MASK	(0x07)
/*ISR_ENABLE, IMR_ENABLE, and PS mode should be inherited.*/
#define	FW_PS_STATE_INT_MASK	(0x3F)

#define	FW_PS_STATE(x)		(FW_PS_STATE_MASK & (x))

/* ((FW_PS_RF_ON) | (FW_PS_REGISTER_ACTIVE))*/
#define	FW_PS_STATE_ALL_ON	(FW_PS_CLOCK_ON)
/* (FW_PS_RF_ON)*/
#define	FW_PS_STATE_RF_ON	(FW_PS_CLOCK_ON)
/* 0x0*/
#define	FW_PS_STATE_RF_OFF	(FW_PS_CLOCK_ON)
/* (FW_PS_STATE_RF_OFF)*/
#define	FW_PS_STATE_RF_OFF_LOW_PWR	(FW_PS_CLOCK_OFF)


/* For 8723BE H2C PwrMode Cmd ID 5.*/
#define	FW_PWR_STATE_ACTIVE	((FW_PS_RF_ON) | (FW_PS_REGISTER_ACTIVE))
#define	FW_PWR_STATE_RF_OFF	0

#define	FW_PS_IS_ACK(x)		((x) & FW_PS_ACK)

#define	IS_IN_LOW_POWER_STATE(__fwpsstate)	\
	(FW_PS_STATE(__fwpsstate) == FW_PS_CLOCK_OFF)

#define	FW_PWR_STATE_ACTIVE	((FW_PS_RF_ON) | (FW_PS_REGISTER_ACTIVE))
#define	FW_PWR_STATE_RF_OFF	0

enum rtl8723b_h2c_cmd {
	H2C_8723B_RSVDPAGE = 0,
	H2C_8723B_MSRRPT = 1,
	H2C_8723B_SCAN = 2,
	H2C_8723B_KEEP_ALIVE_CTRL = 3,
	H2C_8723B_DISCONNECT_DECISION = 4,
	H2C_8723B_BCN_RSVDPAGE = 9,
	H2C_8723B_PROBERSP_RSVDPAGE = 10,

	H2C_8723B_SETPWRMODE = 0x20,
	H2C_8723B_PS_LPS_PARA = 0x23,
	H2C_8723B_P2P_PS_OFFLOAD = 0x24,

	H2C_8723B_RA_MASK = 0x40,
	H2C_RSSIBE_REPORT = 0x42,
	/*Not defined CTW CMD for P2P yet*/
	H2C_8723B_P2P_PS_CTW_CMD,
	MAX_8723B_H2CCMD
};

#define pagenum_128(_len) (u32)(((_len)>>7) + ((_len)&0x7F ? 1 : 0))


#define SET_H2CCMD_PWRMODE_PARM_MODE(__ph2ccmd, __val)			\
	SET_BITS_TO_LE_1BYTE(__ph2ccmd, 0, 8, __val)
#define SET_H2CCMD_PWRMODE_PARM_RLBM(__ph2ccmd, __val)			\
	SET_BITS_TO_LE_1BYTE((__ph2ccmd)+1, 0, 4, __val)
#define SET_H2CCMD_PWRMODE_PARM_SMART_PS(__ph2ccmd, __val)		\
	SET_BITS_TO_LE_1BYTE((__ph2ccmd)+1, 4, 4, __val)
#define SET_H2CCMD_PWRMODE_PARM_AWAKE_INTERVAL(__ph2ccmd, __val)	\
	SET_BITS_TO_LE_1BYTE((__ph2ccmd)+2, 0, 8, __val)
#define SET_H2CCMD_PWRMODE_PARM_ALL_QUEUE_UAPSD(__ph2ccmd, __val)	\
	SET_BITS_TO_LE_1BYTE((__ph2ccmd)+3, 0, 8, __val)
#define SET_H2CCMD_PWRMODE_PARM_PWR_STATE(__ph2ccmd, __val)		\
	SET_BITS_TO_LE_1BYTE((__ph2ccmd)+4, 0, 8, __val)
#define SET_H2CCMD_PWRMODE_PARM_BYTE5(__ph2ccmd, __val)			\
	SET_BITS_TO_LE_1BYTE((__ph2ccmd) + 5, 0, 8, __val)
#define GET_88E_H2CCMD_PWRMODE_PARM_MODE(__ph2ccmd)			\
	LE_BITS_TO_1BYTE(__ph2ccmd, 0, 8)

#define SET_H2CCMD_MSRRPT_PARM_OPMODE(__ph2ccmd, __val)		\
	SET_BITS_TO_LE_1BYTE(__ph2ccmd, 0, 1, __val)
#define SET_H2CCMD_MSRRPT_PARM_MACID_IND(__ph2ccmd, __val)	\
	SET_BITS_TO_LE_1BYTE(__ph2ccmd, 1, 1, __val)

#define SET_H2CCMD_RSVDPAGE_LOC_PROBE_RSP(__ph2ccmd, __val)		\
	SET_BITS_TO_LE_1BYTE(__ph2ccmd, 0, 8, __val)
#define SET_H2CCMD_RSVDPAGE_LOC_PSPOLL(__ph2ccmd, __val)		\
	SET_BITS_TO_LE_1BYTE((__ph2ccmd)+1, 0, 8, __val)
#define SET_H2CCMD_RSVDPAGE_LOC_NULL_DATA(__ph2ccmd, __val)		\
	SET_BITS_TO_LE_1BYTE((__ph2ccmd)+2, 0, 8, __val)
#define SET_H2CCMD_RSVDPAGE_LOC_QOS_NULL_DATA(__ph2ccmd, __val)	\
	SET_BITS_TO_LE_1BYTE((__ph2ccmd) + 3, 0, 8, __val)
#define SET_H2CCMD_RSVDPAGE_LOC_BT_QOS_NULL_DATA(__ph2ccmd, __val)	\
	SET_BITS_TO_LE_1BYTE((__ph2ccmd) + 4, 0, 8, __val)


void rtl8723be_fill_h2c_cmd(struct ieee80211_hw *hw, u8 element_id,
			    u32 cmd_len, u8 *p_cmdbuffer);
void rtl8723be_set_fw_pwrmode_cmd(struct ieee80211_hw *hw, u8 mode);
void rtl8723be_set_fw_media_status_rpt_cmd(struct ieee80211_hw *hw, u8 mstatus);
void rtl8723be_set_fw_rsvdpagepkt(struct ieee80211_hw *hw, bool b_dl_finished);
void rtl8723be_set_p2p_ps_offload_cmd(struct ieee80211_hw *hw, u8 p2p_ps_state);
#endif
