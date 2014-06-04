/******************************************************************************
 *
 * Copyright(c) 2009-2014  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#ifndef __RTL8723BE__FW__H__
#define __RTL8723BE__FW__H__

#define FW_8192C_SIZE				0x8000
#define FW_8192C_START_ADDRESS			0x1000
#define FW_8192C_END_ADDRESS			0x5FFF
#define FW_8192C_PAGE_SIZE			4096
#define FW_8192C_POLLING_DELAY			5
#define FW_8192C_POLLING_TIMEOUT_COUNT		6000

#define IS_FW_HEADER_EXIST(_pfwhdr)	\
	((_pfwhdr->signature&0xFFF0) == 0x5300)
#define USE_OLD_WOWLAN_DEBUG_FW			0

#define H2C_8723BE_RSVDPAGE_LOC_LEN		5
#define H2C_8723BE_PWEMODE_LENGTH		5
#define H2C_8723BE_JOINBSSRPT_LENGTH		1
#define H2C_8723BE_AP_OFFLOAD_LENGTH		3
#define H2C_8723BE_WOWLAN_LENGTH		3
#define H2C_8723BE_KEEP_ALIVE_CTRL_LENGTH	3
#if (USE_OLD_WOWLAN_DEBUG_FW == 0)
#define H2C_8723BE_REMOTE_WAKE_CTRL_LEN		1
#else
#define H2C_8723BE_REMOTE_WAKE_CTRL_LEN		3
#endif
#define H2C_8723BE_AOAC_GLOBAL_INFO_LEN		2
#define H2C_8723BE_AOAC_RSVDPAGE_LOC_LEN	7


/* Fw PS state for RPWM.
*BIT[2:0] = HW state
*BIT[3] = Protocol PS state, 1: register active state , 0: register sleep state
*BIT[4] = sub-state
*/
#define	FW_PS_GO_ON		BIT(0)
#define	FW_PS_TX_NULL		BIT(1)
#define	FW_PS_RF_ON		BIT(2)
#define	FW_PS_REGISTER_ACTIVE	BIT(3)

#define	FW_PS_DPS		BIT(0)
#define	FW_PS_LCLK		(FW_PS_DPS)
#define	FW_PS_RF_OFF		BIT(1)
#define	FW_PS_ALL_ON		BIT(2)
#define	FW_PS_ST_ACTIVE	BIT(3)
#define	FW_PS_ISR_ENABLE	BIT(4)
#define	FW_PS_IMR_ENABLE	BIT(5)


#define	FW_PS_ACK		BIT(6)
#define	FW_PS_TOGGLE		BIT(7)

 /* 88E RPWM value*/
 /* BIT[0] = 1: 32k, 0: 40M*/
#define	FW_PS_CLOCK_OFF		BIT(0)		/* 32k*/
#define	FW_PS_CLOCK_ON		0		/*40M*/

#define	FW_PS_STATE_MASK	(0x0F)
#define	FW_PS_STATE_HW_MASK	(0x07)
/*ISR_ENABLE, IMR_ENABLE, and PS mode should be inherited.*/
#define	FW_PS_STATE_INT_MASK	(0x3F)

#define	FW_PS_STATE(x)	(FW_PS_STATE_MASK & (x))
#define	FW_PS_STATE_HW(x)	(FW_PS_STATE_HW_MASK & (x))
#define	FW_PS_STATE_INT(x)	(FW_PS_STATE_INT_MASK & (x))
#define	FW_PS_ISR_VAL(x)	((x) & 0x70)
#define	FW_PS_IMR_MASK(x)	((x) & 0xDF)
#define	FW_PS_KEEP_IMR(x)	((x) & 0x20)


#define	FW_PS_STATE_S0		(FW_PS_DPS)
#define	FW_PS_STATE_S1		(FW_PS_LCLK)
#define	FW_PS_STATE_S2		(FW_PS_RF_OFF)
#define	FW_PS_STATE_S3		(FW_PS_ALL_ON)
#define	FW_PS_STATE_S4		((FW_PS_ST_ACTIVE) | (FW_PS_ALL_ON))

/* ((FW_PS_RF_ON) | (FW_PS_REGISTER_ACTIVE))*/
#define	FW_PS_STATE_ALL_ON_88E	(FW_PS_CLOCK_ON)
/* (FW_PS_RF_ON)*/
#define	FW_PS_STATE_RF_ON_88E	(FW_PS_CLOCK_ON)
/* 0x0*/
#define	FW_PS_STATE_RF_OFF_88E	(FW_PS_CLOCK_ON)
/* (FW_PS_STATE_RF_OFF)*/
#define	FW_PS_STATE_RF_OFF_LOW_PWR_88E	(FW_PS_CLOCK_OFF)

#define	FW_PS_STATE_ALL_ON_92C	(FW_PS_STATE_S4)
#define	FW_PS_STATE_RF_ON_92C		(FW_PS_STATE_S3)
#define	FW_PS_STATE_RF_OFF_92C	(FW_PS_STATE_S2)
#define	FW_PS_STATE_RF_OFF_LOW_PWR_92C	(FW_PS_STATE_S1)


/* For 88E H2C PwrMode Cmd ID 5.*/
#define	FW_PWR_STATE_ACTIVE	((FW_PS_RF_ON) | (FW_PS_REGISTER_ACTIVE))
#define	FW_PWR_STATE_RF_OFF	0

#define	FW_PS_IS_ACK(x)	((x) & FW_PS_ACK)
#define	FW_PS_IS_CLK_ON(x)	((x) & (FW_PS_RF_OFF | FW_PS_ALL_ON))
#define	FW_PS_IS_RF_ON(x)	((x) & (FW_PS_ALL_ON))
#define	FW_PS_IS_ACTIVE(x)	((x) & (FW_PS_ST_ACTIVE))
#define	FW_PS_IS_CPWM_INT(x)	((x) & 0x40)

#define	FW_CLR_PS_STATE(x)	((x) = ((x) & (0xF0)))

#define	IS_IN_LOW_POWER_STATE_88E(fwpsstate)		\
			(FW_PS_STATE(fwpsstate) == FW_PS_CLOCK_OFF)

#define	FW_PWR_STATE_ACTIVE	((FW_PS_RF_ON) | (FW_PS_REGISTER_ACTIVE))
#define	FW_PWR_STATE_RF_OFF	0

#define pagenum_128(_len)	(u32)(((_len)>>7) + ((_len)&0x7F ? 1 : 0))

#define SET_88E_H2CCMD_WOWLAN_FUNC_ENABLE(__ph2ccmd, __val)		\
	SET_BITS_TO_LE_1BYTE(__ph2ccmd, 0, 1, __val)
#define SET_88E_H2CCMD_WOWLAN_PATTERN_MATCH_ENABLE(__ph2ccmd, __val)	\
	SET_BITS_TO_LE_1BYTE(__ph2ccmd, 1, 1, __val)
#define SET_88E_H2CCMD_WOWLAN_MAGIC_PKT_ENABLE(__ph2ccmd, __val)	\
	SET_BITS_TO_LE_1BYTE(__ph2ccmd, 2, 1, __val)
#define SET_88E_H2CCMD_WOWLAN_UNICAST_PKT_ENABLE(__ph2ccmd, __val)	\
	SET_BITS_TO_LE_1BYTE(__ph2ccmd, 3, 1, __val)
#define SET_88E_H2CCMD_WOWLAN_ALL_PKT_DROP(__ph2ccmd, __val)		\
	SET_BITS_TO_LE_1BYTE(__ph2ccmd, 4, 1, __val)
#define SET_88E_H2CCMD_WOWLAN_GPIO_ACTIVE(__ph2ccmd, __val)		\
	SET_BITS_TO_LE_1BYTE(__ph2ccmd, 5, 1, __val)
#define SET_88E_H2CCMD_WOWLAN_REKEY_WAKE_UP(__ph2ccmd, __val)		\
	SET_BITS_TO_LE_1BYTE(__ph2ccmd, 6, 1, __val)
#define SET_88E_H2CCMD_WOWLAN_DISCONNECT_WAKE_UP(__ph2ccmd, __val)	\
	SET_BITS_TO_LE_1BYTE(__ph2ccmd, 7, 1, __val)
#define SET_88E_H2CCMD_WOWLAN_GPIONUM(__ph2ccmd, __val)			\
	SET_BITS_TO_LE_1BYTE((__ph2ccmd)+1, 0, 8, __val)
#define SET_88E_H2CCMD_WOWLAN_GPIO_DURATION(__ph2ccmd, __val)		\
	SET_BITS_TO_LE_1BYTE((__ph2ccmd)+2, 0, 8, __val)


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
#define GET_88E_H2CCMD_PWRMODE_PARM_MODE(__ph2ccmd)			\
	LE_BITS_TO_1BYTE(__ph2ccmd, 0, 8)

#define SET_H2CCMD_JOINBSSRPT_PARM_OPMODE(__ph2ccmd, __val)		\
	SET_BITS_TO_LE_1BYTE(__ph2ccmd, 0, 8, __val)
#define SET_H2CCMD_RSVDPAGE_LOC_PROBE_RSP(__ph2ccmd, __val)		\
	SET_BITS_TO_LE_1BYTE(__ph2ccmd, 0, 8, __val)
#define SET_H2CCMD_RSVDPAGE_LOC_PSPOLL(__ph2ccmd, __val)		\
	SET_BITS_TO_LE_1BYTE((__ph2ccmd)+1, 0, 8, __val)
#define SET_H2CCMD_RSVDPAGE_LOC_NULL_DATA(__ph2ccmd, __val)		\
	SET_BITS_TO_LE_1BYTE((__ph2ccmd)+2, 0, 8, __val)

/* AP_OFFLOAD */
#define SET_H2CCMD_AP_OFFLOAD_ON(__ph2ccmd, __val)			\
	SET_BITS_TO_LE_1BYTE(__ph2ccmd, 0, 8, __val)
#define SET_H2CCMD_AP_OFFLOAD_HIDDEN(__ph2ccmd, __val)			\
	SET_BITS_TO_LE_1BYTE((__ph2ccmd)+1, 0, 8, __val)
#define SET_H2CCMD_AP_OFFLOAD_DENYANY(__ph2ccmd, __val)			\
	SET_BITS_TO_LE_1BYTE((__ph2ccmd)+2, 0, 8, __val)
#define SET_H2CCMD_AP_OFFLOAD_WAKEUP_EVT_RPT(__ph2ccmd, __val)		\
	SET_BITS_TO_LE_1BYTE((__ph2ccmd)+3, 0, 8, __val)

/* Keep Alive Control*/
#define SET_88E_H2CCMD_KEEP_ALIVE_ENABLE(__ph2ccmd, __val)		\
	SET_BITS_TO_LE_1BYTE(__ph2ccmd, 0, 1, __val)
#define SET_88E_H2CCMD_KEEP_ALIVE_ACCPEPT_USER_DEFINED(__ph2ccmd, __val)\
	SET_BITS_TO_LE_1BYTE(__ph2ccmd, 1, 1, __val)
#define SET_88E_H2CCMD_KEEP_ALIVE_PERIOD(__ph2ccmd, __val)		\
	SET_BITS_TO_LE_1BYTE((__ph2ccmd)+1, 0, 8, __val)

/*REMOTE_WAKE_CTRL */
#define SET_88E_H2CCMD_REMOTE_WAKE_CTRL_EN(__ph2ccmd, __val)		\
	SET_BITS_TO_LE_1BYTE(__ph2ccmd, 0, 1, __val)
#if (USE_OLD_WOWLAN_DEBUG_FW == 0)
#define SET_88E_H2CCMD_REMOTE_WAKE_CTRL_ARP_OFFLOAD_EN(__ph2ccmd, __val)\
	SET_BITS_TO_LE_1BYTE(__ph2ccmd, 1, 1, __val)
#define SET_88E_H2CCMD_REMOTE_WAKE_CTRL_NDP_OFFLOAD_EN(__ph2ccmd, __val)\
	SET_BITS_TO_LE_1BYTE(__ph2ccmd, 2, 1, __val)
#define SET_88E_H2CCMD_REMOTE_WAKE_CTRL_GTK_OFFLOAD_EN(__ph2ccmd, __val)\
	SET_BITS_TO_LE_1BYTE(__ph2ccmd, 3, 1, __val)
#else
#define SET_88E_H2CCMD_REMOTE_WAKE_CTRL_PAIRWISE_ENC_ALG(__ph2ccmd, __val)\
	SET_BITS_TO_LE_1BYTE((__ph2ccmd)+1, 0, 8, __val)
#define SET_88E_H2CCMD_REMOTE_WAKE_CTRL_GROUP_ENC_ALG(__ph2ccmd, __val)	\
	SET_BITS_TO_LE_1BYTE((__ph2ccmd)+2, 0, 8, __val)
#endif

/* GTK_OFFLOAD */
#define SET_88E_H2CCMD_AOAC_GLOBAL_INFO_PAIRWISE_ENC_ALG(__ph2ccmd, __val)\
	SET_BITS_TO_LE_1BYTE(__ph2ccmd, 0, 8, __val)
#define SET_88E_H2CCMD_AOAC_GLOBAL_INFO_GROUP_ENC_ALG(__ph2ccmd, __val)	\
	SET_BITS_TO_LE_1BYTE((__ph2ccmd)+1, 0, 8, __val)

/* AOAC_RSVDPAGE_LOC */
#define SET_88E_H2CCMD_AOAC_RSVDPAGE_LOC_REM_WAKE_CTRL_INFO(__ph2ccmd, __val)\
	SET_BITS_TO_LE_1BYTE((__ph2ccmd), 0, 8, __val)
#define SET_88E_H2CCMD_AOAC_RSVDPAGE_LOC_ARP_RSP(__ph2ccmd, __val)	\
	SET_BITS_TO_LE_1BYTE((__ph2ccmd)+1, 0, 8, __val)
#define SET_88E_H2CCMD_AOAC_RSVDPAGE_LOC_NEIGHBOR_ADV(__ph2ccmd, __val)	\
	SET_BITS_TO_LE_1BYTE((__ph2ccmd)+2, 0, 8, __val)
#define SET_88E_H2CCMD_AOAC_RSVDPAGE_LOC_GTK_RSP(__ph2ccmd, __val)	\
	SET_BITS_TO_LE_1BYTE((__ph2ccmd)+3, 0, 8, __val)
#define SET_88E_H2CCMD_AOAC_RSVDPAGE_LOC_GTK_INFO(__ph2ccmd, __val)	\
	SET_BITS_TO_LE_1BYTE((__ph2ccmd)+4, 0, 8, __val)

void rtl8723be_set_fw_pwrmode_cmd(struct ieee80211_hw *hw, u8 mode);
void rtl8723be_set_fw_ap_off_load_cmd(struct ieee80211_hw *hw,
				      u8 ap_offload_enable);
void rtl8723be_fill_h2c_cmd(struct ieee80211_hw *hw, u8 element_id,
			    u32 cmd_len, u8 *p_cmdbuffer);
void rtl8723be_firmware_selfreset(struct ieee80211_hw *hw);
void rtl8723be_set_fw_rsvdpagepkt(struct ieee80211_hw *hw,
				  bool dl_finished);
void rtl8723be_set_fw_joinbss_report_cmd(struct ieee80211_hw *hw, u8 mstatus);
int rtl8723be_download_fw(struct ieee80211_hw *hw,
			  bool buse_wake_on_wlan_fw);
void rtl8723be_set_p2p_ps_offload_cmd(struct ieee80211_hw *hw,
				      u8 p2p_ps_state);

#endif
