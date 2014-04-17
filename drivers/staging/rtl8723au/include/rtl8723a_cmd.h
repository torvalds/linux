/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
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
 ******************************************************************************/
#ifndef __RTL8723A_CMD_H__
#define __RTL8723A_CMD_H__


#define H2C_BT_FW_PATCH_LEN		3
#define H2C_BT_PWR_FORCE_LEN		3

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
	H2C_BT_FW_PATCH = 54,
	B_TYPE_TDMA_EID = 58,
	SCAN_EN_EID = 59,
	LOWPWR_LPS_EID = 71,
	H2C_RESET_TSF = 75,
	MAX_CMDMSG_EID
};

struct cmd_msg_parm {
	u8 eid; /* element id */
	u8 sz; /*  sz */
	u8 buf[6];
};

struct setpwrmode_parm {
	u8 Mode;
	u8 SmartPS;
	u8 AwakeInterval;	/*  unit: beacon interval */
	u8 bAllQueueUAPSD;

#define SETPM_LOWRXBCN			BIT(0)
#define SETPM_AUTOANTSWITCH		BIT(1)
#define SETPM_PSALLOWBTHIGHPRI	BIT(2)
	u8 BcnAntMode;
} __packed;

struct H2C_SS_RFOFF_PARAM{
	u8 ROFOn; /*  1: on, 0:off */
	u16 gpio_period; /*  unit: 1024 us */
}__attribute__ ((packed));


struct joinbssrpt_parm {
	u8 OpMode;	/*  enum rt_media_status */
};

struct rsvdpage_loc {
	u8 LocProbeRsp;
	u8 LocPsPoll;
	u8 LocNullData;
	u8 LocQosNull;
	u8 LocBTQosNull;
};

struct P2P_PS_Offload_t {
	u8 Offload_En:1;
	u8 role:1; /*  1: Owner, 0: Client */
	u8 CTWindow_En:1;
	u8 NoA0_En:1;
	u8 NoA1_En:1;
	u8 AllStaSleep:1; /*  Only valid in Owner */
	u8 discovery:1;
	u8 rsvd:1;
};

struct P2P_PS_CTWPeriod_t {
	u8 CTWPeriod;	/* TU */
};

#define B_TDMA_EN			BIT(0)
#define B_TDMA_FIXANTINBT		BIT(1)
#define B_TDMA_TXPSPOLL			BIT(2)
#define B_TDMA_VAL870			BIT(3)
#define B_TDMA_AUTOWAKEUP		BIT(4)
#define B_TDMA_NOPS			BIT(5)
#define B_TDMA_WLANHIGHPRI		BIT(6)

struct b_type_tdma_parm {
	u8 option;

	u8 TBTTOnPeriod;
	u8 MedPeriod;
	u8 rsvd30;
} __packed;

struct scan_en_parm {
	u8 En;
} __packed;

/*  BT_PWR */
#define SET_H2CCMD_BT_PWR_IDX(__pH2CCmd, __Value)							SET_BITS_TO_LE_1BYTE_8BIT(__pH2CCmd, 0, 8, __Value)

/*  BT_FW_PATCH */
#define SET_H2CCMD_BT_FW_PATCH_ENABLE(__pH2CCmd, __Value)					SET_BITS_TO_LE_4BYTE(__pH2CCmd, 0, 8, __Value) /*	SET_BITS_TO_LE_1BYTE(__pH2CCmd, 0, 8, __Value) */
#define SET_H2CCMD_BT_FW_PATCH_SIZE(__pH2CCmd, __Value)						SET_BITS_TO_LE_4BYTE(__pH2CCmd, 8, 16, __Value) /*	SET_BITS_TO_LE_2BYTE((__pH2CCmd)+1, 0, 16, __Value) */

struct lowpwr_lps_parm{
	u8 bcn_count:4;
	u8 tb_bcn_threshold:3;
	u8 enable:1;
	u8 bcn_interval;
	u8 drop_threshold;
	u8 max_early_period;
	u8 max_bcn_timeout_period;
} __packed;


/*  host message to firmware cmd */
void rtl8723a_set_FwPwrMode_cmd(struct rtw_adapter * padapter, u8 Mode);
void rtl8723a_set_FwJoinBssReport_cmd(struct rtw_adapter * padapter, u8 mstatus);
#ifdef CONFIG_8723AU_BT_COEXIST
void rtl8723a_set_BTCoex_AP_mode_FwRsvdPkt_cmd(struct rtw_adapter * padapter);
#endif
u8 rtl8723a_set_rssi_cmd(struct rtw_adapter * padapter, u8 *param);
u8 rtl8723a_set_raid_cmd(struct rtw_adapter * padapter, u32 mask, u8 arg);
void rtl8723a_add_rateatid(struct rtw_adapter * padapter, u32 bitmap, u8 arg, u8 rssi_level);

#ifdef CONFIG_8723AU_P2P
void rtl8723a_set_p2p_ps_offload_cmd(struct rtw_adapter * padapter, u8 p2p_ps_state);
#endif /* CONFIG_8723AU_P2P */

void CheckFwRsvdPageContent23a(struct rtw_adapter *padapter);

#endif
