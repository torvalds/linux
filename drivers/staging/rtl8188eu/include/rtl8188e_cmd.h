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

enum RTL8188E_H2C_CMD_ID {
	/* Class Common */
	H2C_COM_RSVD_PAGE		= 0x00,
	H2C_COM_MEDIA_STATUS_RPT	= 0x01,
	H2C_COM_SCAN			= 0x02,
	H2C_COM_KEEP_ALIVE		= 0x03,
	H2C_COM_DISCNT_DECISION		= 0x04,
	H2C_COM_INIT_OFFLOAD		= 0x06,
	H2C_COM_REMOTE_WAKE_CTL		= 0x07,
	H2C_COM_AP_OFFLOAD		= 0x08,
	H2C_COM_BCN_RSVD_PAGE		= 0x09,
	H2C_COM_PROB_RSP_RSVD_PAGE	= 0x0A,

	/* Class PS */
	H2C_PS_PWR_MODE			= 0x20,
	H2C_PS_TUNE_PARA		= 0x21,
	H2C_PS_TUNE_PARA_2		= 0x22,
	H2C_PS_LPS_PARA			= 0x23,
	H2C_PS_P2P_OFFLOAD		= 0x24,

	/* Class DM */
	H2C_DM_MACID_CFG		= 0x40,
	H2C_DM_TXBF			= 0x41,

	/* Class BT */
	H2C_BT_COEX_MASK		= 0x60,
	H2C_BT_COEX_GPIO_MODE		= 0x61,
	H2C_BT_DAC_SWING_VAL		= 0x62,
	H2C_BT_PSD_RST			= 0x63,

	/* Class */
	 H2C_RESET_TSF			= 0xc0,
};

struct cmd_msg_parm {
	u8 eid; /* element id */
	u8 sz; /*  sz */
	u8 buf[6];
};

enum {
	PWRS
};

struct setpwrmode_parm {
	u8 Mode;/* 0:Active,1:LPS,2:WMMPS */
	u8 SmartPS_RLBM;/* LPS= 0:PS_Poll,1:PS_Poll,2:NullData,WMM= 0:PS_Poll,1:NullData */
	u8 AwakeInterval;	/*  unit: beacon interval */
	u8 bAllQueueUAPSD;
	u8 PwrState;/* AllON(0x0c),RFON(0x04),RFOFF(0x00) */
};

struct H2C_SS_RFOFF_PARAM {
	u8 ROFOn; /*  1: on, 0:off */
	u16 gpio_period; /*  unit: 1024 us */
} __packed;

struct joinbssrpt_parm {
	u8 OpMode;	/*  RT_MEDIA_STATUS */
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

/*  host message to firmware cmd */
void rtl8188e_set_FwPwrMode_cmd(struct adapter *padapter, u8 Mode);
void rtl8188e_set_FwJoinBssReport_cmd(struct adapter *padapter, u8 mstatus);
u8 rtl8188e_set_raid_cmd(struct adapter *padapter, u32 mask);
void rtl8188e_Add_RateATid(struct adapter *padapter, u32 bitmap, u8 arg,
			   u8 rssi_level);

void rtl8188e_set_FwMediaStatus_cmd(struct adapter *adapt, __le16 mstatus_rpt);

#endif/* __RTL8188E_CMD_H__ */
