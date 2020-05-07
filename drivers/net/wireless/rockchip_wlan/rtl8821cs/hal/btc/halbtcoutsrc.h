/******************************************************************************
 *
 * Copyright(c) 2016 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#ifndef	__HALBTC_OUT_SRC_H__
#define __HALBTC_OUT_SRC_H__

enum {
	BTC_CCK_1,
	BTC_CCK_2,
	BTC_CCK_5_5,
	BTC_CCK_11,
	BTC_OFDM_6,
	BTC_OFDM_9,
	BTC_OFDM_12,
	BTC_OFDM_18,
	BTC_OFDM_24,
	BTC_OFDM_36,
	BTC_OFDM_48,
	BTC_OFDM_54,
	BTC_MCS_0,
	BTC_MCS_1,
	BTC_MCS_2,
	BTC_MCS_3,
	BTC_MCS_4,
	BTC_MCS_5,
	BTC_MCS_6,
	BTC_MCS_7,
	BTC_MCS_8,
	BTC_MCS_9,
	BTC_MCS_10,
	BTC_MCS_11,
	BTC_MCS_12,
	BTC_MCS_13,
	BTC_MCS_14,
	BTC_MCS_15,
	BTC_MCS_16,
	BTC_MCS_17,
	BTC_MCS_18,
	BTC_MCS_19,
	BTC_MCS_20,
	BTC_MCS_21,
	BTC_MCS_22,
	BTC_MCS_23,
	BTC_MCS_24,
	BTC_MCS_25,
	BTC_MCS_26,
	BTC_MCS_27,
	BTC_MCS_28,
	BTC_MCS_29,
	BTC_MCS_30,
	BTC_MCS_31,
	BTC_VHT_1SS_MCS_0,
	BTC_VHT_1SS_MCS_1,
	BTC_VHT_1SS_MCS_2,
	BTC_VHT_1SS_MCS_3,
	BTC_VHT_1SS_MCS_4,
	BTC_VHT_1SS_MCS_5,
	BTC_VHT_1SS_MCS_6,
	BTC_VHT_1SS_MCS_7,
	BTC_VHT_1SS_MCS_8,
	BTC_VHT_1SS_MCS_9,
	BTC_VHT_2SS_MCS_0,
	BTC_VHT_2SS_MCS_1,
	BTC_VHT_2SS_MCS_2,
	BTC_VHT_2SS_MCS_3,
	BTC_VHT_2SS_MCS_4,
	BTC_VHT_2SS_MCS_5,
	BTC_VHT_2SS_MCS_6,
	BTC_VHT_2SS_MCS_7,
	BTC_VHT_2SS_MCS_8,
	BTC_VHT_2SS_MCS_9,
	BTC_VHT_3SS_MCS_0,
	BTC_VHT_3SS_MCS_1,
	BTC_VHT_3SS_MCS_2,
	BTC_VHT_3SS_MCS_3,
	BTC_VHT_3SS_MCS_4,
	BTC_VHT_3SS_MCS_5,
	BTC_VHT_3SS_MCS_6,
	BTC_VHT_3SS_MCS_7,
	BTC_VHT_3SS_MCS_8,
	BTC_VHT_3SS_MCS_9,
	BTC_VHT_4SS_MCS_0,
	BTC_VHT_4SS_MCS_1,
	BTC_VHT_4SS_MCS_2,
	BTC_VHT_4SS_MCS_3,
	BTC_VHT_4SS_MCS_4,
	BTC_VHT_4SS_MCS_5,
	BTC_VHT_4SS_MCS_6,
	BTC_VHT_4SS_MCS_7,
	BTC_VHT_4SS_MCS_8,
	BTC_VHT_4SS_MCS_9,
	BTC_MCS_32,
	BTC_UNKNOWN,
	BTC_PKT_MGNT,
	BTC_PKT_CTRL,
	BTC_PKT_UNKNOWN,
	BTC_PKT_NOT_FOR_ME,
	BTC_RATE_MAX
};

enum {
	BTC_MULTIPORT_SCC,
	BTC_MULTIPORT_MCC_DUAL_CHANNEL,
	BTC_MULTIPORT_MCC_DUAL_BAND,
	BTC_MULTIPORT_MAX
};

#define		BTC_COEX_8822B_COMMON_CODE	0
#define		BTC_COEX_OFFLOAD			0
#define		BTC_TMP_BUF_SHORT		20

extern u1Byte	gl_btc_trace_buf[];
#define		BTC_SPRINTF			rsprintf
#define		BTC_TRACE(_MSG_)\
do {\
	if (GLBtcDbgType[COMP_COEX] & BIT(DBG_LOUD)) {\
		RTW_INFO("%s", _MSG_);\
	} \
} while (0)
#define		BT_PrintData(adapter, _MSG_, len, data)	RTW_DBG_DUMP((_MSG_), data, len)


#define		NORMAL_EXEC					FALSE
#define		FORCE_EXEC						TRUE

#define		NM_EXCU						FALSE
#define		FC_EXCU						TRUE

#define		BTC_RF_OFF					0x0
#define		BTC_RF_ON					0x1

#define		BTC_RF_A					0x0
#define		BTC_RF_B					0x1
#define		BTC_RF_C					0x2
#define		BTC_RF_D					0x3

#define		BTC_SMSP				SINGLEMAC_SINGLEPHY
#define		BTC_DMDP				DUALMAC_DUALPHY
#define		BTC_DMSP				DUALMAC_SINGLEPHY
#define		BTC_MP_UNKNOWN		0xff

#define		BT_COEX_ANT_TYPE_PG			0
#define		BT_COEX_ANT_TYPE_ANTDIV		1
#define		BT_COEX_ANT_TYPE_DETECTED	2

#define		BTC_MIMO_PS_STATIC			0	/* 1ss */
#define		BTC_MIMO_PS_DYNAMIC			1	/* 2ss */

#define		BTC_RATE_DISABLE			0
#define		BTC_RATE_ENABLE				1

/* single Antenna definition */
#define		BTC_ANT_PATH_WIFI			0
#define		BTC_ANT_PATH_BT				1
#define		BTC_ANT_PATH_PTA			2
#define		BTC_ANT_PATH_WIFI5G			3
#define		BTC_ANT_PATH_AUTO			4
/* dual Antenna definition */
#define		BTC_ANT_WIFI_AT_MAIN		0
#define		BTC_ANT_WIFI_AT_AUX			1
#define		BTC_ANT_WIFI_AT_DIVERSITY	2
/* coupler Antenna definition */
#define		BTC_ANT_WIFI_AT_CPL_MAIN	0
#define		BTC_ANT_WIFI_AT_CPL_AUX		1

typedef enum _BTC_POWERSAVE_TYPE {
	BTC_PS_WIFI_NATIVE			= 0,	/* wifi original power save behavior */
	BTC_PS_LPS_ON				= 1,
	BTC_PS_LPS_OFF				= 2,
	BTC_PS_MAX
} BTC_POWERSAVE_TYPE, *PBTC_POWERSAVE_TYPE;

typedef enum _BTC_BT_REG_TYPE {
	BTC_BT_REG_RF						= 0,
	BTC_BT_REG_MODEM					= 1,
	BTC_BT_REG_BLUEWIZE					= 2,
	BTC_BT_REG_VENDOR					= 3,
	BTC_BT_REG_LE						= 4,
	BTC_BT_REG_MAX
} BTC_BT_REG_TYPE, *PBTC_BT_REG_TYPE;

typedef enum _BTC_CHIP_INTERFACE {
	BTC_INTF_UNKNOWN	= 0,
	BTC_INTF_PCI			= 1,
	BTC_INTF_USB			= 2,
	BTC_INTF_SDIO		= 3,
	BTC_INTF_MAX
} BTC_CHIP_INTERFACE, *PBTC_CHIP_INTERFACE;

typedef enum _BTC_CHIP_TYPE {
	BTC_CHIP_UNDEF		= 0,
	BTC_CHIP_CSR_BC4		= 1,
	BTC_CHIP_CSR_BC8		= 2,
	BTC_CHIP_RTL8723A		= 3,
	BTC_CHIP_RTL8821		= 4,
	BTC_CHIP_RTL8723B		= 5,
	BTC_CHIP_RTL8822B 		= 6,
	BTC_CHIP_RTL8822C 		= 7,
	BTC_CHIP_RTL8821C 		= 8,
	BTC_CHIP_RTL8821A 		= 9,
	BTC_CHIP_RTL8723D 		= 10,
	BTC_CHIP_RTL8703B 		= 11,
	BTC_CHIP_RTL8725A 		= 12,
	BTC_CHIP_MAX
} BTC_CHIP_TYPE, *PBTC_CHIP_TYPE;

/* following is for wifi link status */
#define		WIFI_STA_CONNECTED				BIT0
#define		WIFI_AP_CONNECTED				BIT1
#define		WIFI_HS_CONNECTED				BIT2
#define		WIFI_P2P_GO_CONNECTED			BIT3
#define		WIFI_P2P_GC_CONNECTED			BIT4

/* following is for command line utility */
#define	CL_SPRINTF	rsprintf
#define	CL_PRINTF	DCMD_Printf
#define CL_STRNCAT(dst, dst_size, src, src_size) rstrncat(dst, src, src_size)

static const char *const glbt_info_src[] = {
	"BT Info[wifi fw]",
	"BT Info[bt rsp]",
	"BT Info[bt auto report]",
};

#define BTC_INFO_FTP		BIT(7)
#define BTC_INFO_A2DP		BIT(6)
#define BTC_INFO_HID		BIT(5)
#define BTC_INFO_SCO_BUSY		BIT(4)
#define BTC_INFO_ACL_BUSY		BIT(3)
#define BTC_INFO_INQ_PAGE		BIT(2)
#define BTC_INFO_SCO_ESCO		BIT(1)
#define BTC_INFO_CONNECTION	BIT(0)

#define BTC_BTINFO_LENGTH_MAX 10

enum btc_gnt_setup_state {
	BTC_GNT_SET_SW_LOW	= 0x0,
	BTC_GNT_SET_SW_HIGH	= 0x1,
	BTC_GNT_SET_HW_PTA	= 0x2,
	BTC_GNT_SET_MAX
};

enum btc_gnt_setup_state_2 {
	BTC_GNT_SW_LOW		= 0x0,
	BTC_GNT_SW_HIGH		= 0x1,
	BTC_GNT_HW_PTA		= 0x2,
	BTC_GNT_MAX
};

enum btc_path_ctrl_owner {
	BTC_OWNER_BT		= 0x0,
	BTC_OWNER_WL		= 0x1,
	BTC_OWNER_MAX
};

enum btc_gnt_ctrl_type {
	BTC_GNT_CTRL_BY_PTA	= 0x0,
	BTC_GNT_CTRL_BY_SW	= 0x1,
	BTC_GNT_CTRL_MAX
};

enum btc_gnt_ctrl_block {
	BTC_GNT_BLOCK_RFC_BB	= 0x0,
	BTC_GNT_BLOCK_RFC	= 0x1,
	BTC_GNT_BLOCK_BB	= 0x2,
	BTC_GNT_BLOCK_MAX
};

enum btc_lte_coex_table_type {
	BTC_CTT_WL_VS_LTE	= 0x0,
	BTC_CTT_BT_VS_LTE	= 0x1,
	BTC_CTT_MAX
};

enum btc_lte_break_table_type {
	BTC_LBTT_WL_BREAK_LTE	= 0x0,
	BTC_LBTT_BT_BREAK_LTE	= 0x1,
	BTC_LBTT_LTE_BREAK_WL	= 0x2,
	BTC_LBTT_LTE_BREAK_BT	= 0x3,
	BTC_LBTT_MAX
};

enum btc_btinfo_src {
	BTC_BTINFO_SRC_WL_FW	= 0x0,
	BTC_BTINFO_SRC_BT_RSP	= 0x1,
	BTC_BTINFO_SRC_BT_ACT	= 0x2,
	BTC_BTINFO_SRC_BT_IQK	= 0x3,
	BTC_BTINFO_SRC_BT_SCBD	= 0x4,
	BTC_BTINFO_SRC_H2C60	= 0x5,
	BTC_BTINFO_SRC_MAX
};

enum btc_bt_profile {
	BTC_BTPROFILE_NONE		= 0,
	BTC_BTPROFILE_HFP		= BIT(0),
	BTC_BTPROFILE_HID		= BIT(1),
	BTC_BTPROFILE_A2DP		= BIT(2),
	BTC_BTPROFILE_PAN		= BIT(3),
	BTC_BTPROFILE_MAX		= 0xf
};

static const char *const bt_profile_string[] = {
	"None",
	"HFP",
	"HID",
	"HID + HFP",
	"A2DP",
	"A2DP + HFP",
	"A2DP + HID",
	"PAN + HID + HFP",
	"PAN",
	"PAN + HFP",
	"PAN + HID",
	"PAN + HID + HFP",
	"PAN + A2DP",
	"PAN + A2DP + HFP",
	"PAN + A2DP + HID",
	"PAN + A2DP + HID + HFP"
};

enum btc_bt_status {
	BTC_BTSTATUS_NCON_IDLE		= 0x0,
	BTC_BTSTATUS_CON_IDLE		= 0x1,
	BTC_BTSTATUS_INQ_PAGE		= 0x2,
	BTC_BTSTATUS_ACL_BUSY		= 0x3,
	BTC_BTSTATUS_SCO_BUSY		= 0x4,
	BTC_BTSTATUS_ACL_SCO_BUSY	= 0x5,
	BTC_BTSTATUS_MAX
};

static const char *const bt_status_string[] = {
	"BT Non-Connected-idle",
	"BT Connected-idle",
	"BT Inq-page",
	"BT ACL-busy",
	"BT SCO-busy",
	"BT ACL-SCO-busy",
	"BT Non-Defined-state"
};

enum btc_coex_algo {
	BTC_COEX_NOPROFILE		= 0x0,
	BTC_COEX_HFP			= 0x1,
	BTC_COEX_HID			= 0x2,
	BTC_COEX_A2DP			= 0x3,
	BTC_COEX_PAN			= 0x4,
	BTC_COEX_A2DP_HID		= 0x5,
	BTC_COEX_A2DP_PAN		= 0x6,
	BTC_COEX_PAN_HID		= 0x7,
	BTC_COEX_A2DP_PAN_HID		= 0x8,
	BTC_COEX_MAX
};

static const char *const coex_algo_string[] = {
	"No Profile",
	"HFP",
	"HID",
	"A2DP",
	"PAN",
	"A2DP + HID",
	"A2DP + PAN",
	"PAN + HID",
	"A2DP + PAN + HID"
};

enum btc_ext_ant_switch_type {
	BTC_SWITCH_NONE	= 0x0,
	BTC_SWITCH_SPDT	= 0x1,
	BTC_SWITCH_SP3T	= 0x2,
	BTC_SWITCH_ANTMAX
};

enum btc_ext_ant_switch_ctrl_type {
	BTC_SWITCH_CTRL_BY_BBSW		= 0x0,
	BTC_SWITCH_CTRL_BY_PTA		= 0x1,
	BTC_SWITCH_CTRL_BY_ANTDIV	= 0x2,
	BTC_SWITCH_CTRL_BY_MAC		= 0x3,
	BTC_SWITCH_CTRL_BY_BT		= 0x4,
	BTC_SWITCH_CTRL_BY_FW		= 0x5,
	BTC_SWITCH_CTRL_MAX
};

enum btc_ext_ant_switch_pos_type {
	BTC_SWITCH_TO_BT		= 0x0,
	BTC_SWITCH_TO_WLG		= 0x1,
	BTC_SWITCH_TO_WLA		= 0x2,
	BTC_SWITCH_TO_NOCARE		= 0x3,
	BTC_SWITCH_TO_WLG_BT		= 0x4,
	BTC_SWITCH_TO_MAX
};

enum btx_set_ant_phase {
	BTC_ANT_INIT			= 0x0,
	BTC_ANT_WONLY			= 0x1,
	BTC_ANT_WOFF			= 0x2,
	BTC_ANT_2G			= 0x3,
	BTC_ANT_5G			= 0x4,
	BTC_ANT_BTMP			= 0x5,
	BTC_ANT_POWERON			= 0x6,
	BTC_ANT_2G_WL			= 0x7,
	BTC_ANT_2G_BT			= 0x8,
	BTC_ANT_MCC			= 0x9,
	BTC_ANT_2G_WLBT			= 0xa,
	BTC_ANT_2G_FREERUN		= 0xb,
	BTC_ANT_MAX
};

/*ADD SCOREBOARD TO FIX BT LPS 32K ISSUE WHILE WL BUSY*/
enum btc_wl2bt_scoreboard {
	BTC_SCBD_ACTIVE		= BIT(0),
	BTC_SCBD_ON			= BIT(1),
	BTC_SCBD_SCAN		= BIT(2),
	BTC_SCBD_UNDERTEST	= BIT(3),
	BTC_SCBD_RXGAIN		= BIT(4),
	BTC_SCBD_WLBUSY		= BIT(7),
	BTC_SCBD_EXTFEM		= BIT(8),
	BTC_SCBD_TDMA		= BIT(9),
	BTC_SCBD_FIX2M		= BIT(10),
	BTC_SCBD_ALL		= 0xffff
};

enum btc_bt2wl_scoreboard {
	BTC_SCBD_BT_ONOFF	= BIT(1),
	BTC_SCBD_BT_LPS		= BIT(7)
};

enum btc_runreason {
	BTC_RSN_2GSCANSTART	= 0x0,
	BTC_RSN_5GSCANSTART	= 0x1,
	BTC_RSN_SCANFINISH	= 0x2,
	BTC_RSN_2GSWITCHBAND	= 0x3,
	BTC_RSN_5GSWITCHBAND	= 0x4,
	BTC_RSN_2GCONSTART	= 0x5,
	BTC_RSN_5GCONSTART	= 0x6,
	BTC_RSN_2GCONFINISH	= 0x7,
	BTC_RSN_5GCONFINISH	= 0x8,
	BTC_RSN_2GMEDIA		= 0x9,
	BTC_RSN_5GMEDIA		= 0xa,
	BTC_RSN_MEDIADISCON	= 0xb,
	BTC_RSN_2GSPECIALPKT	= 0xc,
	BTC_RSN_5GSPECIALPKT	= 0xd,
	BTC_RSN_BTINFO		= 0xe,
	BTC_RSN_PERIODICAL	= 0xf,
	BTC_RSN_PNP		= 0x10,
	BTC_RSN_LPS		= 0x11,
	BTC_RSN_TIMERUP		= 0x12,
	BTC_RSN_WLSTATUS	= 0x13,
	BTC_RSN_MAX
};

static const char *const run_reason_string[] = {
	"2G_SCAN_START",
	"5G_SCAN_START",
	"SCAN_FINISH",
	"2G_SWITCH_BAND",
	"5G_SWITCH_BAND",
	"2G_CONNECT_START",
	"5G_CONNECT_START",
	"2G_CONNECT_FINISH",
	"5G_CONNECT_FINISH",
	"2G_MEDIA_STATUS",
	"5G_MEDIA_STATUS",
	"MEDIA_DISCONNECT",
	"2G_SPECIALPKT",
	"5G_SPECIALPKT",
	"BTINFO",
	"PERIODICAL",
	"PNPNotify",
	"LPSNotify",
	"TimerUp",
	"WL_STATUS_CHANGE",
};

enum btc_wl_link_mode {
	BTC_WLINK_2G1PORT	= 0x0,
	BTC_WLINK_2GMPORT	= 0x1,
	BTC_WLINK_25GMPORT	= 0x2,
	BTC_WLINK_5G		= 0x3,
	BTC_WLINK_2GGO		= 0x4,
	BTC_WLINK_2GGC		= 0x5,
	BTC_WLINK_BTMR		= 0x6,
	BTC_WLINK_MAX
};

static const char *const coex_mode_string[] = {
	"2G-SP",
	"2G-MP",
	"25G-MP",
	"5G",
	"2G-P2P-GO",
	"2G-P2P-GC",
	"BT-MR"
};

enum btc_bt_state_cnt {
	BTC_CNT_BT_RETRY	= 0x0,
	BTC_CNT_BT_REINIT	= 0x1,
	BTC_CNT_BT_POPEVENT	= 0x2,
	BTC_CNT_BT_SETUPLINK	= 0x3,
	BTC_CNT_BT_IGNWLANACT	= 0x4,
	BTC_CNT_BT_INQ		= 0x5,
	BTC_CNT_BT_PAGE		= 0x6,
	BTC_CNT_BT_ROLESWITCH	= 0x7,
	BTC_CNT_BT_AFHUPDATE	= 0x8,
	BTC_CNT_BT_DISABLE	= 0x9,
	BTC_CNT_BT_INFOUPDATE	= 0xa,
	BTC_CNT_BT_IQK		= 0xb,
	BTC_CNT_BT_IQKFAIL	= 0xc,
	BTC_CNT_BT_MAX
};

enum btc_wl_state_cnt {
	BTC_CNT_WL_SCANAP		= 0x0,
	BTC_CNT_WL_ARP			= 0x1,
	BTC_CNT_WL_GNTERR		= 0x2,
	BTC_CNT_WL_PSFAIL		= 0x3,
	BTC_CNT_WL_COEXRUN		= 0x4,
	BTC_CNT_WL_COEXINFO1		= 0x5,
	BTC_CNT_WL_COEXINFO2		= 0x6,
	BTC_CNT_WL_AUTOSLOT_HANG	= 0x7,
	BTC_CNT_WL_NOISY0		= 0x8,
	BTC_CNT_WL_NOISY1		= 0x9,
	BTC_CNT_WL_NOISY2		= 0xa,
	BTC_CNT_WL_ACTIVEPORT		= 0xb,
	BTC_CNT_WL_5MS_NOEXTEND		= 0xc,
	BTC_CNT_WL_FW_NOTIFY		= 0xd,
	BTC_CNT_WL_MAX
};

enum btc_wl_crc_cnt {
	BTC_WLCRC_11BOK		= 0x0,
	BTC_WLCRC_11GOK		= 0x1,
	BTC_WLCRC_11NOK		= 0x2,
	BTC_WLCRC_11VHTOK	= 0x3,
	BTC_WLCRC_11BERR	= 0x4,
	BTC_WLCRC_11GERR	= 0x5,
	BTC_WLCRC_11NERR	= 0x6,
	BTC_WLCRC_11VHTERR	= 0x7,
	BTC_WLCRC_MAX
};

enum btc_timer_cnt {
	BTC_TIMER_WL_STAYBUSY	= 0x0,
	BTC_TIMER_WL_COEXFREEZE	= 0x1,
	BTC_TIMER_WL_SPECPKT	= 0x2,
	BTC_TIMER_WL_CONNPKT	= 0x3,
	BTC_TIMER_WL_PNPWAKEUP	= 0x4,
	BTC_TIMER_WL_CCKLOCK	= 0x5,
	BTC_TIMER_WL_FWDBG	= 0x6,
	BTC_TIMER_BT_RELINK	= 0x7,
	BTC_TIMER_BT_REENABLE	= 0x8,
	BTC_TIMER_MAX
};

enum btc_wl_status_change {
	BTC_WLSTATUS_CHANGE_TOIDLE	= 0x0,
	BTC_WLSTATUS_CHANGE_TOBUSY	= 0x1,
	BTC_WLSTATUS_CHANGE_RSSI	= 0x2,
	BTC_WLSTATUS_CHANGE_LINKINFO	= 0x3,
	BTC_WLSTATUS_CHANGE_DIR	= 0x4,
	BTC_WLSTATUS_CHANGE_NOISY	= 0x5,
	BTC_WLSTATUS_CHANGE_MAX
};

enum btc_commom_chip_setup {
	BTC_CSETUP_INIT_HW		= 0x0,
	BTC_CSETUP_ANT_SWITCH	= 0x1,
	BTC_CSETUP_GNT_FIX		= 0x2,
	BTC_CSETUP_GNT_DEBUG	= 0x3,
	BTC_CSETUP_RFE_TYPE		= 0x4,
	BTC_CSETUP_COEXINFO_HW	= 0x5,
	BTC_CSETUP_WL_TX_POWER	= 0x6,
	BTC_CSETUP_WL_RX_GAIN	= 0x7,
	BTC_CSETUP_WLAN_ACT_IPS = 0x8,
	BTC_CSETUP_MAX
};

enum btc_indirect_reg_type {
	BTC_INDIRECT_1700	= 0x0,
	BTC_INDIRECT_7C0	= 0x1,
	BTC_INDIRECT_MAX
};

enum btc_pstdma_type {
	BTC_PSTDMA_FORCE_LPSOFF	= 0x0,
	BTC_PSTDMA_FORCE_LPSON	= 0x1,
	BTC_PSTDMA_MAX
};

enum btc_btrssi_type {
	BTC_BTRSSI_RATIO	= 0x0,
	BTC_BTRSSI_DBM		= 0x1,
	BTC_BTRSSI_MAX
};

enum btc_wl_priority_mask {
	BTC_WLPRI_RX_RSP	= 2,
	BTC_WLPRI_TX_RSP	= 3,
	BTC_WLPRI_TX_BEACON	= 4,
	BTC_WLPRI_TX_OFDM	= 11,
	BTC_WLPRI_TX_CCK	= 12,
	BTC_WLPRI_TX_BEACONQ	= 27,
	BTC_WLPRI_RX_CCK	= 28,
	BTC_WLPRI_RX_OFDM	= 29,
	BTC_WLPRI_MAX
};

struct btc_board_info {
	/* The following is some board information */
	u8				bt_chip_type;
	u8				pg_ant_num;	/* pg ant number */
	u8				btdm_ant_num;	/* ant number for btdm */
	u8				btdm_ant_num_by_ant_det;	/* ant number for btdm after antenna detection */
	u8				btdm_ant_pos;		/* Bryant Add to indicate Antenna Position for (pg_ant_num = 2) && (btdm_ant_num =1)  (DPDT+1Ant case) */
	u8				single_ant_path;	/* current used for 8723b only, 1=>s0,  0=>s1 */
	boolean			tfbga_package;    /* for Antenna detect threshold */
	boolean			btdm_ant_det_finish;
	boolean			btdm_ant_det_already_init_phydm;
	u8				ant_type;
	u8				rfe_type;
	u8				ant_div_cfg;
	boolean			btdm_ant_det_complete_fail;
	u8				ant_det_result;
	boolean			ant_det_result_five_complete;
	u32				antdetval;
	u8				customerID;
	u8				customer_id;
	u8				ant_distance;	/* WL-BT antenna space for non-shared antenna  */
};

struct btc_coex_dm {
	boolean cur_ignore_wlan_act;
	boolean cur_ps_tdma_on;
	boolean cur_low_penalty_ra;
	boolean cur_wl_rx_low_gain_en;

	u8	bt_rssi_state[4];
	u8	wl_rssi_state[4];
	u8	cur_ps_tdma;
	u8	ps_tdma_para[5];
	u8	fw_tdma_para[5];
	u8	cur_lps;
	u8	cur_rpwm;
	u8	cur_bt_pwr_lvl;
	u8	cur_bt_lna_lvl;
	u8	cur_wl_pwr_lvl;
	u8	cur_algorithm;
	u8	bt_status;
	u8	wl_chnl_info[3];
	u8	cur_toggle_para[6];
	u8	cur_val0x6cc;
	u32	cur_val0x6c0;
	u32	cur_val0x6c4;
	u32	cur_val0x6c8;
	u32	cur_ant_pos_type;
	u32	cur_switch_status;
	u32	setting_tdma;
};

struct btc_coex_sta {
	boolean coex_freeze;
	boolean coex_freerun;
	boolean tdma_bt_autoslot;
	boolean rf4ce_en;
	boolean is_no_wl_5ms_extend;

	boolean bt_disabled;
	boolean bt_disabled_pre;
	boolean bt_link_exist;
	boolean bt_whck_test;
	boolean bt_inq_page;
	boolean bt_inq;
	boolean bt_page;
	boolean bt_ble_voice;
	boolean bt_ble_exist;
	boolean bt_hfp_exist;
	boolean bt_a2dp_exist;
	boolean bt_hid_exist;
	boolean bt_pan_exist; // PAN or OPP
	boolean bt_opp_exist; //OPP only
	boolean bt_msft_mr_exist;
	boolean bt_acl_busy;
	boolean bt_fix_2M;
	boolean bt_setup_link;
	boolean bt_multi_link;
	boolean bt_a2dp_sink;
	boolean bt_reenable;
	boolean bt_ble_scan_en;
	boolean bt_slave;
	boolean bt_a2dp_active;
	boolean bt_slave_latency;
	boolean bt_init_scan;
	boolean bt_418_hid_exist;
	boolean bt_mesh;

	boolean wl_under_lps;
	boolean wl_under_ips;
	boolean wl_under_4way;
	boolean	wl_hi_pri_task1;
	boolean	wl_hi_pri_task2;
	boolean wl_cck_lock;
	boolean wl_cck_lock_pre;
	boolean wl_cck_lock_ever;
	boolean wl_force_lps_ctrl;
	boolean wl_busy_pre;
	boolean wl_gl_busy;
	boolean wl_gl_busy_pre;
	boolean wl_linkscan_proc;
	boolean wl_mimo_ps;
	boolean wl_ps_state_fail;
	boolean wl_cck_dead_lock_ap;
	boolean wl_tx_limit_en;
	boolean wl_ampdu_limit_en;
	boolean wl_rxagg_limit_en;
	boolean wl_connecting;
	boolean wl_pnp_wakeup;
	boolean wl_slot_toggle;
	boolean wl_slot_toggle_change; /* if toggle to no-toggle */

	u8	coex_table_type;
	u8 	coex_run_reason;
	u8	tdma_byte4_modify_pre;
	u8	kt_ver;
	u8	gnt_workaround_state;
	u8	tdma_timer_base;
	u8	bt_rssi;
	u8	bt_profile_num;
	u8	bt_profile_num_pre;
	u8	bt_info_c2h[BTC_BTINFO_SRC_MAX][BTC_BTINFO_LENGTH_MAX];
	u8	bt_info_lb2;
	u8	bt_info_lb3;
	u8	bt_info_hb0;
	u8	bt_info_hb1;
	u8	bt_info_hb2;
	u8	bt_info_hb3;
	u8	bt_ble_scan_type;
	u8	bt_afh_map[10];
	u8	bt_a2dp_vendor_id;
	u8	bt_hid_pair_num;
	u8	bt_hid_slot;
	u8	bt_a2dp_bitpool;
	u8	bt_iqk_state;
	u8	bt_sut_pwr_lvl[4];
	u8	bt_golden_rx_shift[4];

	u8	wl_pnp_state_pre;
	u8	wl_noisy_level;
	u8	wl_fw_dbg_info[10];
	u8	wl_fw_dbg_info_pre[10];
	u8	wl_rx_rate;
	u8	wl_tx_rate;
	u8	wl_rts_rx_rate;
	u8	wl_center_ch;
	u8	wl_tx_macid;
	u8	wl_tx_retry_ratio;
	u8	wl_coex_mode;
	u8	wl_iot_peer;
	u8	wl_ra_thres;
	u8	wl_ampdulen_backup;
	u8	wl_rxagg_size;
	u8	wl_toggle_para[6];

	u16	score_board_BW;
	u16	score_board_WB;
	u16	bt_reg_vendor_ac;
	u16	bt_reg_vendor_ae;
	u16	bt_reg_modem_a;
	u16	bt_reg_rf_2;
	u16	wl_txlimit_backup;

	u32	hi_pri_tx;
	u32	hi_pri_rx;
	u32	lo_pri_tx;
	u32	lo_pri_rx;
	u32	bt_supported_feature;
	u32	bt_supported_version;
	u32	bt_ble_scan_para[3];
	u32	bt_a2dp_device_name;
	u32	wl_arfb1_backup;
	u32	wl_arfb2_backup;
	u32	wl_traffic_dir;
	u32	wl_bw;
	u32	cnt_bt_info_c2h[BTC_BTINFO_SRC_MAX];
	u32	cnt_bt[BTC_CNT_BT_MAX];
	u32	cnt_wl[BTC_CNT_WL_MAX];
	u32	cnt_timer[BTC_TIMER_MAX];
};

struct btc_rfe_type {
	boolean ant_switch_exist;
	boolean ant_switch_diversity; /* If diversity on */
	boolean ant_switch_with_bt; /* If WL_2G/BT use ext-switch at shared-ant */
	u8	rfe_module_type;
	u8	ant_switch_type;
	u8	ant_switch_polarity;
	
	boolean band_switch_exist;
	u8	band_switch_type; /* 0:DPDT, 1:SPDT */
	u8	band_switch_polarity;

	/*  If TRUE:  WLG at BTG, If FALSE: WLG at WLAG */
	boolean wlg_at_btg;
};


struct btc_wifi_link_info_ext {
	boolean is_all_under_5g;
	boolean is_mcc_25g;
	boolean is_p2p_connected;
	boolean is_ap_mode;
	boolean is_scan;
	boolean is_link;
	boolean is_roam;
	boolean is_4way;
	boolean is_32k;
	boolean is_connected;
	u8	num_of_active_port;
	u32	port_connect_status;
	u32	traffic_dir;
	u32	wifi_bw;
};

struct btc_coex_table_para {
	u32 bt;	//0x6c0
	u32 wl;	//0x6c4
};

struct btc_tdma_para {
	u8 para[5];
};

struct btc_reg_byte_modify {
	u32 addr;
	u8 bitmask;
	u8 val;
};

struct btc_5g_afh_map {
	u32 wl_5g_ch;
	u8 bt_skip_ch;
	u8 bt_skip_span;
};

struct btc_rf_para {
	u8 wl_pwr_dec_lvl;
	u8 bt_pwr_dec_lvl;
	boolean wl_low_gain_en;
	u8 bt_lna_lvl;
};

typedef enum _BTC_DBG_OPCODE {
	BTC_DBG_SET_COEX_NORMAL				= 0x0,
	BTC_DBG_SET_COEX_WIFI_ONLY				= 0x1,
	BTC_DBG_SET_COEX_BT_ONLY				= 0x2,
	BTC_DBG_SET_COEX_DEC_BT_PWR				= 0x3,
	BTC_DBG_SET_COEX_BT_AFH_MAP				= 0x4,
	BTC_DBG_SET_COEX_BT_IGNORE_WLAN_ACT		= 0x5,
	BTC_DBG_SET_COEX_MANUAL_CTRL				= 0x6,
	BTC_DBG_MAX
} BTC_DBG_OPCODE, *PBTC_DBG_OPCODE;

typedef enum _BTC_RSSI_STATE {
	BTC_RSSI_STATE_HIGH						= 0x0,
	BTC_RSSI_STATE_MEDIUM					= 0x1,
	BTC_RSSI_STATE_LOW						= 0x2,
	BTC_RSSI_STATE_STAY_HIGH					= 0x3,
	BTC_RSSI_STATE_STAY_MEDIUM				= 0x4,
	BTC_RSSI_STATE_STAY_LOW					= 0x5,
	BTC_RSSI_MAX
} BTC_RSSI_STATE, *PBTC_RSSI_STATE;
#define	BTC_RSSI_HIGH(_rssi_)	((_rssi_ == BTC_RSSI_STATE_HIGH || _rssi_ == BTC_RSSI_STATE_STAY_HIGH) ? TRUE:FALSE)
#define	BTC_RSSI_MEDIUM(_rssi_)	((_rssi_ == BTC_RSSI_STATE_MEDIUM || _rssi_ == BTC_RSSI_STATE_STAY_MEDIUM) ? TRUE:FALSE)
#define	BTC_RSSI_LOW(_rssi_)	((_rssi_ == BTC_RSSI_STATE_LOW || _rssi_ == BTC_RSSI_STATE_STAY_LOW) ? TRUE:FALSE)

typedef enum _BTC_WIFI_ROLE {
	BTC_ROLE_STATION						= 0x0,
	BTC_ROLE_AP								= 0x1,
	BTC_ROLE_IBSS							= 0x2,
	BTC_ROLE_HS_MODE						= 0x3,
	BTC_ROLE_MAX
} BTC_WIFI_ROLE, *PBTC_WIFI_ROLE;

typedef enum _BTC_WIRELESS_FREQ {
	BTC_FREQ_2_4G					= 0x0,
	BTC_FREQ_5G						= 0x1,
	BTC_FREQ_25G					= 0x2,
	BTC_FREQ_MAX
} BTC_WIRELESS_FREQ, *PBTC_WIRELESS_FREQ;

typedef enum _BTC_WIFI_BW_MODE {
	BTC_WIFI_BW_LEGACY					= 0x0,
	BTC_WIFI_BW_HT20					= 0x1,
	BTC_WIFI_BW_HT40					= 0x2,
	BTC_WIFI_BW_HT80					= 0x3,
	BTC_WIFI_BW_HT160					= 0x4,
	BTC_WIFI_BW_MAX
} BTC_WIFI_BW_MODE, *PBTC_WIFI_BW_MODE;

typedef enum _BTC_WIFI_TRAFFIC_DIR {
	BTC_WIFI_TRAFFIC_TX					= 0x0,
	BTC_WIFI_TRAFFIC_RX					= 0x1,
	BTC_WIFI_TRAFFIC_MAX
} BTC_WIFI_TRAFFIC_DIR, *PBTC_WIFI_TRAFFIC_DIR;

typedef enum _BTC_WIFI_PNP {
	BTC_WIFI_PNP_WAKE_UP					= 0x0,
	BTC_WIFI_PNP_SLEEP						= 0x1,
	BTC_WIFI_PNP_SLEEP_KEEP_ANT				= 0x2,
	BTC_WIFI_PNP_WOWLAN					= 0x3,
	BTC_WIFI_PNP_MAX
} BTC_WIFI_PNP, *PBTC_WIFI_PNP;

typedef enum _BTC_IOT_PEER {
	BTC_IOT_PEER_UNKNOWN = 0,
	BTC_IOT_PEER_REALTEK = 1,
	BTC_IOT_PEER_REALTEK_92SE = 2,
	BTC_IOT_PEER_BROADCOM = 3,
	BTC_IOT_PEER_RALINK = 4,
	BTC_IOT_PEER_ATHEROS = 5,
	BTC_IOT_PEER_CISCO = 6,
	BTC_IOT_PEER_MERU = 7,
	BTC_IOT_PEER_MARVELL = 8,
	BTC_IOT_PEER_REALTEK_SOFTAP = 9, /* peer is RealTek SOFT_AP, by Bohn, 2009.12.17 */
	BTC_IOT_PEER_SELF_SOFTAP = 10, /* Self is SoftAP */
	BTC_IOT_PEER_AIRGO = 11,
	BTC_IOT_PEER_INTEL				= 12,
	BTC_IOT_PEER_RTK_APCLIENT		= 13,
	BTC_IOT_PEER_REALTEK_81XX		= 14,
	BTC_IOT_PEER_REALTEK_WOW		= 15,
	BTC_IOT_PEER_REALTEK_JAGUAR_BCUTAP = 16,
	BTC_IOT_PEER_REALTEK_JAGUAR_CCUTAP = 17,
	BTC_IOT_PEER_MAX,
} BTC_IOT_PEER, *PBTC_IOT_PEER;

/* for 8723b-d cut large current issue */
typedef enum _BTC_WIFI_COEX_STATE {
	BTC_WIFI_STAT_INIT,
	BTC_WIFI_STAT_IQK,
	BTC_WIFI_STAT_NORMAL_OFF,
	BTC_WIFI_STAT_MP_OFF,
	BTC_WIFI_STAT_NORMAL,
	BTC_WIFI_STAT_ANT_DIV,
	BTC_WIFI_STAT_MAX
} BTC_WIFI_COEX_STATE, *PBTC_WIFI_COEX_STATE;

typedef enum _BTC_ANT_TYPE {
	BTC_ANT_TYPE_0,
	BTC_ANT_TYPE_1,
	BTC_ANT_TYPE_2,
	BTC_ANT_TYPE_3,
	BTC_ANT_TYPE_4,
	BTC_ANT_TYPE_MAX
} BTC_ANT_TYPE, *PBTC_ANT_TYPE;

typedef enum _BTC_VENDOR {
	BTC_VENDOR_LENOVO,
	BTC_VENDOR_ASUS,
	BTC_VENDOR_OTHER
} BTC_VENDOR, *PBTC_VENDOR;


/* defined for BFP_BTC_GET */
typedef enum _BTC_GET_TYPE {
	/* type BOOLEAN */
	BTC_GET_BL_HS_OPERATION,
	BTC_GET_BL_HS_CONNECTING,
	BTC_GET_BL_WIFI_FW_READY,
	BTC_GET_BL_WIFI_CONNECTED,
	BTC_GET_BL_WIFI_DUAL_BAND_CONNECTED,
	BTC_GET_BL_WIFI_LINK_INFO,
	BTC_GET_BL_WIFI_BUSY,
	BTC_GET_BL_WIFI_SCAN,
	BTC_GET_BL_WIFI_LINK,
	BTC_GET_BL_WIFI_ROAM,
	BTC_GET_BL_WIFI_4_WAY_PROGRESS,
	BTC_GET_BL_WIFI_UNDER_5G,
	BTC_GET_BL_WIFI_AP_MODE_ENABLE,
	BTC_GET_BL_WIFI_ENABLE_ENCRYPTION,
	BTC_GET_BL_WIFI_UNDER_B_MODE,
	BTC_GET_BL_EXT_SWITCH,
	BTC_GET_BL_WIFI_IS_IN_MP_MODE,
	BTC_GET_BL_IS_ASUS_8723B,
	BTC_GET_BL_RF4CE_CONNECTED,
	BTC_GET_BL_WIFI_LW_PWR_STATE,

	/* type s4Byte */
	BTC_GET_S4_WIFI_RSSI,
	BTC_GET_S4_HS_RSSI,

	/* type u4Byte */
	BTC_GET_U4_WIFI_BW,
	BTC_GET_U4_WIFI_TRAFFIC_DIRECTION,
	BTC_GET_U4_WIFI_TRAFFIC_DIR,
	BTC_GET_U4_WIFI_FW_VER,
	BTC_GET_U4_WIFI_PHY_VER,
	BTC_GET_U4_WIFI_LINK_STATUS,
	BTC_GET_U4_BT_PATCH_VER,
	BTC_GET_U4_VENDOR,
	BTC_GET_U4_SUPPORTED_VERSION,
	BTC_GET_U4_SUPPORTED_FEATURE,
	BTC_GET_U4_BT_DEVICE_INFO,
	BTC_GET_U4_BT_FORBIDDEN_SLOT_VAL,
	BTC_GET_U4_WIFI_IQK_TOTAL,
	BTC_GET_U4_WIFI_IQK_OK,
	BTC_GET_U4_WIFI_IQK_FAIL,

	/* type u1Byte */
	BTC_GET_U1_WIFI_DOT11_CHNL,
	BTC_GET_U1_WIFI_CENTRAL_CHNL,
	BTC_GET_U1_WIFI_HS_CHNL,
	BTC_GET_U1_WIFI_P2P_CHNL,
	BTC_GET_U1_MAC_PHY_MODE,
	BTC_GET_U1_AP_NUM,
	BTC_GET_U1_ANT_TYPE,
	BTC_GET_U1_IOT_PEER,

	/* type u2Byte */
	BTC_GET_U2_BEACON_PERIOD,

	/*===== for 1Ant ======*/
	BTC_GET_U1_LPS_MODE,

	BTC_GET_MAX
} BTC_GET_TYPE, *PBTC_GET_TYPE;

/* defined for BFP_BTC_SET */
typedef enum _BTC_SET_TYPE {
	/* type BOOLEAN */
	BTC_SET_BL_BT_DISABLE,
	BTC_SET_BL_BT_ENABLE_DISABLE_CHANGE,
	BTC_SET_BL_BT_TRAFFIC_BUSY,
	BTC_SET_BL_BT_LIMITED_DIG,
	BTC_SET_BL_FORCE_TO_ROAM,
	BTC_SET_BL_TO_REJ_AP_AGG_PKT,
	BTC_SET_BL_BT_CTRL_AGG_SIZE,
	BTC_SET_BL_INC_SCAN_DEV_NUM,
	BTC_SET_BL_BT_TX_RX_MASK,
	BTC_SET_BL_MIRACAST_PLUS_BT,
	BTC_SET_BL_BT_LNA_CONSTRAIN_LEVEL,
	BTC_SET_BL_BT_GOLDEN_RX_RANGE,

	/* type u1Byte */
	BTC_SET_U1_RSSI_ADJ_VAL_FOR_AGC_TABLE_ON,
	BTC_SET_U1_AGG_BUF_SIZE,

	/* type trigger some action */
	BTC_SET_ACT_GET_BT_RSSI,
	BTC_SET_ACT_AGGREGATE_CTRL,
	BTC_SET_ACT_ANTPOSREGRISTRY_CTRL,

	// for mimo ps mode setting
	BTC_SET_MIMO_PS_MODE,
	/*===== for 1Ant ======*/
	/* type BOOLEAN */

	/* type u1Byte */
	BTC_SET_U1_RSSI_ADJ_VAL_FOR_1ANT_COEX_TYPE,
	BTC_SET_U1_LPS_VAL,
	BTC_SET_U1_RPWM_VAL,
	/* type trigger some action */
	BTC_SET_ACT_LEAVE_LPS,
	BTC_SET_ACT_ENTER_LPS,
	BTC_SET_ACT_NORMAL_LPS,
	BTC_SET_ACT_PRE_NORMAL_LPS,
	BTC_SET_ACT_POST_NORMAL_LPS,
	BTC_SET_ACT_DISABLE_LOW_POWER,
	BTC_SET_ACT_UPDATE_RAMASK,
	BTC_SET_ACT_SEND_MIMO_PS,
	/* BT Coex related */
	BTC_SET_ACT_CTRL_BT_INFO,
	BTC_SET_ACT_CTRL_BT_COEX,
	BTC_SET_ACT_CTRL_8723B_ANT,
	BTC_SET_RESET_COEX_VAR,
	/*=================*/
	BTC_SET_MAX
} BTC_SET_TYPE, *PBTC_SET_TYPE;

typedef enum _BTC_DBG_DISP_TYPE {
	BTC_DBG_DISP_COEX_STATISTICS				= 0x0,
	BTC_DBG_DISP_BT_LINK_INFO				= 0x1,
	BTC_DBG_DISP_WIFI_STATUS				= 0x2,
	BTC_DBG_DISP_MAX
} BTC_DBG_DISP_TYPE, *PBTC_DBG_DISP_TYPE;

typedef enum _BTC_NOTIFY_TYPE_IPS {
	BTC_IPS_LEAVE							= 0x0,
	BTC_IPS_ENTER							= 0x1,
	BTC_IPS_MAX
} BTC_NOTIFY_TYPE_IPS, *PBTC_NOTIFY_TYPE_IPS;
typedef enum _BTC_NOTIFY_TYPE_LPS {
	BTC_LPS_DISABLE							= 0x0,
	BTC_LPS_ENABLE							= 0x1,
	BTC_LPS_MAX
} BTC_NOTIFY_TYPE_LPS, *PBTC_NOTIFY_TYPE_LPS;
typedef enum _BTC_NOTIFY_TYPE_SCAN {
	BTC_SCAN_FINISH							= 0x0,
	BTC_SCAN_START							= 0x1,
	BTC_SCAN_START_2G						= 0x2,
	BTC_SCAN_START_5G						= 0x3,
	BTC_SCAN_MAX
} BTC_NOTIFY_TYPE_SCAN, *PBTC_NOTIFY_TYPE_SCAN;
typedef enum _BTC_NOTIFY_TYPE_SWITCHBAND {
	BTC_NOT_SWITCH							= 0x0,
	BTC_SWITCH_TO_24G						= 0x1,
	BTC_SWITCH_TO_5G						= 0x2,
	BTC_SWITCH_TO_24G_NOFORSCAN				= 0x3,
	BTC_SWITCH_MAX
} BTC_NOTIFY_TYPE_SWITCHBAND, *PBTC_NOTIFY_TYPE_SWITCHBAND;
typedef enum _BTC_NOTIFY_TYPE_ASSOCIATE {
	BTC_ASSOCIATE_FINISH						= 0x0,
	BTC_ASSOCIATE_START						= 0x1,
	BTC_ASSOCIATE_5G_FINISH						= 0x2,
	BTC_ASSOCIATE_5G_START						= 0x3,
	BTC_ASSOCIATE_MAX
} BTC_NOTIFY_TYPE_ASSOCIATE, *PBTC_NOTIFY_TYPE_ASSOCIATE;
typedef enum _BTC_NOTIFY_TYPE_MEDIA_STATUS {
	BTC_MEDIA_DISCONNECT					= 0x0,
	BTC_MEDIA_CONNECT						= 0x1,
	BTC_MEDIA_CONNECT_5G					= 0x02,
	BTC_MEDIA_MAX
} BTC_NOTIFY_TYPE_MEDIA_STATUS, *PBTC_NOTIFY_TYPE_MEDIA_STATUS;
typedef enum _BTC_NOTIFY_TYPE_SPECIFIC_PACKET {
	BTC_PACKET_UNKNOWN					= 0x0,
	BTC_PACKET_DHCP							= 0x1,
	BTC_PACKET_ARP							= 0x2,
	BTC_PACKET_EAPOL						= 0x3,
	BTC_PACKET_MAX
} BTC_NOTIFY_TYPE_SPECIFIC_PACKET, *PBTC_NOTIFY_TYPE_SPECIFIC_PACKET;
typedef enum _BTC_NOTIFY_TYPE_STACK_OPERATION {
	BTC_STACK_OP_NONE					= 0x0,
	BTC_STACK_OP_INQ_PAGE_PAIR_START		= 0x1,
	BTC_STACK_OP_INQ_PAGE_PAIR_FINISH	= 0x2,
	BTC_STACK_OP_MAX
} BTC_NOTIFY_TYPE_STACK_OPERATION, *PBTC_NOTIFY_TYPE_STACK_OPERATION;

/* Bryant Add */
typedef enum _BTC_ANTENNA_POS {
	BTC_ANTENNA_AT_MAIN_PORT				= 0x1,
	BTC_ANTENNA_AT_AUX_PORT				= 0x2,
} BTC_ANTENNA_POS, *PBTC_ANTENNA_POS;

/* Bryant Add */
typedef enum _BTC_BT_OFFON {
	BTC_BT_OFF				= 0x0,
	BTC_BT_ON				= 0x1,
} BTC_BTOFFON, *PBTC_BT_OFFON;

#define BTC_5G_BAND 0x80

/*==================================================
For following block is for coex offload
==================================================*/
typedef struct _COL_H2C {
	u1Byte	opcode;
	u1Byte	opcode_ver:4;
	u1Byte	req_num:4;
	u1Byte	buf[1];
} COL_H2C, *PCOL_H2C;

#define	COL_C2H_ACK_HDR_LEN	3
typedef struct _COL_C2H_ACK {
	u1Byte	status;
	u1Byte	opcode_ver:4;
	u1Byte	req_num:4;
	u1Byte	ret_len;
	u1Byte	buf[1];
} COL_C2H_ACK, *PCOL_C2H_ACK;

#define	COL_C2H_IND_HDR_LEN	3
typedef struct _COL_C2H_IND {
	u1Byte	type;
	u1Byte	version;
	u1Byte	length;
	u1Byte	data[1];
} COL_C2H_IND, *PCOL_C2H_IND;

/*============================================
NOTE: for debug message, the following define should match
the strings in coexH2cResultString.
============================================*/
typedef enum _COL_H2C_STATUS {
	/* c2h status */
	COL_STATUS_C2H_OK								= 0x00, /* Wifi received H2C request and check content ok. */
	COL_STATUS_C2H_UNKNOWN							= 0x01,	/* Not handled routine */
	COL_STATUS_C2H_UNKNOWN_OPCODE					= 0x02,	/* Invalid OP code, It means that wifi firmware received an undefiend OP code. */
	COL_STATUS_C2H_OPCODE_VER_MISMATCH			= 0x03, /* Wifi firmware and wifi driver mismatch, need to update wifi driver or wifi or. */
	COL_STATUS_C2H_PARAMETER_ERROR				= 0x04, /* Error paraneter.(ex: parameters = NULL but it should have values) */
	COL_STATUS_C2H_PARAMETER_OUT_OF_RANGE		= 0x05, /* Wifi firmware needs to check the parameters from H2C request and return the status.(ex: ch = 500, it's wrong) */
	/* other COL status start from here */
	COL_STATUS_C2H_REQ_NUM_MISMATCH			, /* c2h req_num mismatch, means this c2h is not we expected. */
	COL_STATUS_H2C_HALMAC_FAIL					, /* HALMAC return fail. */
	COL_STATUS_H2C_TIMTOUT						, /* not received the c2h response from fw */
	COL_STATUS_INVALID_C2H_LEN					, /* invalid coex offload c2h ack length, must >= 3 */
	COL_STATUS_COEX_DATA_OVERFLOW				, /* coex returned length over the c2h ack length. */
	COL_STATUS_MAX
} COL_H2C_STATUS, *PCOL_H2C_STATUS;

#define	COL_MAX_H2C_REQ_NUM		16

#define	COL_H2C_BUF_LEN			20
typedef enum _COL_OPCODE {
	COL_OP_WIFI_STATUS_NOTIFY					= 0x0,
	COL_OP_WIFI_PROGRESS_NOTIFY					= 0x1,
	COL_OP_WIFI_INFO_NOTIFY						= 0x2,
	COL_OP_WIFI_POWER_STATE_NOTIFY				= 0x3,
	COL_OP_SET_CONTROL							= 0x4,
	COL_OP_GET_CONTROL							= 0x5,
	COL_OP_WIFI_OPCODE_MAX
} COL_OPCODE, *PCOL_OPCODE;

typedef enum _COL_IND_TYPE {
	COL_IND_BT_INFO								= 0x0,
	COL_IND_PSTDMA								= 0x1,
	COL_IND_LIMITED_TX_RX						= 0x2,
	COL_IND_COEX_TABLE							= 0x3,
	COL_IND_REQ									= 0x4,
	COL_IND_MAX
} COL_IND_TYPE, *PCOL_IND_TYPE;

typedef struct _COL_SINGLE_H2C_RECORD {
	u1Byte					h2c_buf[COL_H2C_BUF_LEN];	/* the latest sent h2c buffer */
	u4Byte					h2c_len;
	u1Byte					c2h_ack_buf[COL_H2C_BUF_LEN];	/* the latest received c2h buffer */
	u4Byte					c2h_ack_len;
	u4Byte					count;									/* the total number of the sent h2c command */
	u4Byte					status[COL_STATUS_MAX];					/* the c2h status for the sent h2c command */
} COL_SINGLE_H2C_RECORD, *PCOL_SINGLE_H2C_RECORD;

typedef struct _COL_SINGLE_C2H_IND_RECORD {
	u1Byte					ind_buf[COL_H2C_BUF_LEN];	/* the latest received c2h indication buffer */
	u4Byte					ind_len;
	u4Byte					count;									/* the total number of the rcvd c2h indication */
	u4Byte					status[COL_STATUS_MAX];					/* the c2h indication verified status */
} COL_SINGLE_C2H_IND_RECORD, *PCOL_SINGLE_C2H_IND_RECORD;

typedef struct _BTC_OFFLOAD {
	/* H2C command related */
	u1Byte					h2c_req_num;
	u4Byte					cnt_h2c_sent;
	COL_SINGLE_H2C_RECORD	h2c_record[COL_OP_WIFI_OPCODE_MAX];

	/* C2H Ack related */
	u4Byte					cnt_c2h_ack;
	u4Byte					status[COL_STATUS_MAX];
	struct completion		c2h_event[COL_MAX_H2C_REQ_NUM];	/* for req_num = 1~COL_MAX_H2C_REQ_NUM */
	u1Byte					c2h_ack_buf[COL_MAX_H2C_REQ_NUM][COL_H2C_BUF_LEN];
	u1Byte					c2h_ack_len[COL_MAX_H2C_REQ_NUM];

	/* C2H Indication related */
	u4Byte						cnt_c2h_ind;
	COL_SINGLE_C2H_IND_RECORD	c2h_ind_record[COL_IND_MAX];
	u4Byte						c2h_ind_status[COL_STATUS_MAX];
	u1Byte						c2h_ind_buf[COL_H2C_BUF_LEN];
	u1Byte						c2h_ind_len;
} BTC_OFFLOAD, *PBTC_OFFLOAD;
extern BTC_OFFLOAD				gl_coex_offload;
/*==================================================*/

/* BTC_LINK_MODE same as WIFI_LINK_MODE */
typedef enum _BTC_LINK_MODE{
	BTC_LINK_NONE=0,
	BTC_LINK_ONLY_GO,
	BTC_LINK_ONLY_GC,
	BTC_LINK_ONLY_STA,
	BTC_LINK_ONLY_AP,
	BTC_LINK_2G_MCC_GO_STA,
	BTC_LINK_5G_MCC_GO_STA,
	BTC_LINK_25G_MCC_GO_STA,
	BTC_LINK_2G_MCC_GC_STA,
	BTC_LINK_5G_MCC_GC_STA,
	BTC_LINK_25G_MCC_GC_STA,
	BTC_LINK_2G_SCC_GO_STA,
	BTC_LINK_5G_SCC_GO_STA,
	BTC_LINK_2G_SCC_GC_STA,
	BTC_LINK_5G_SCC_GC_STA,
	BTC_LINK_MAX=30
}BTC_LINK_MODE, *PBTC_LINK_MODE;


struct btc_wifi_link_info {
	BTC_LINK_MODE link_mode; /* LinkMode */
	u1Byte sta_center_channel; /* StaCenterChannel */
	u1Byte p2p_center_channel; /* P2PCenterChannel	*/
	BOOLEAN bany_client_join_go;
	BOOLEAN benable_noa;
	BOOLEAN bhotspot;
};

#if 0
typedef enum _BTC_MULTI_PORT_TDMA_MODE {
	BTC_MULTI_PORT_TDMA_MODE_NONE=0,
	BTC_MULTI_PORT_TDMA_MODE_2G_SCC_GO,
	BTC_MULTI_PORT_TDMA_MODE_2G_P2P_GO,
	BTC_MULTI_PORT_TDMA_MODE_2G_HOTSPOT_GO
} BTC_MULTI_PORT_TDMA_MODE, *PBTC_MULTI_PORT_TDMA_MODE;

typedef struct btc_multi_port_tdma_info {
	BTC_MULTI_PORT_TDMA_MODE btc_multi_port_tdma_mode;
	u1Byte start_time_from_bcn;
	u1Byte bt_time;
} BTC_MULTI_PORT_TDMA_INFO, *PBTC_MULTI_PORT_TDMA_INFO;
#endif

typedef enum _btc_concurrent_mode {
	btc_concurrent_mode_none = 0,
	btc_concurrent_mode_2g_go_miracast,
	btc_concurrent_mode_2g_go_hotspot,
	btc_concurrent_mode_2g_scc_go_miracast_sta,
	btc_concurrent_mode_2g_scc_go_hotspot_sta,
	btc_concurrent_mode_2g_gc,
} btc_concurrent_mode, *pbtc_concurrent_mode;

struct btc_concurrent_setting {
	btc_concurrent_mode btc_concurrent_mode;
	u1Byte start_time_from_bcn;
	u1Byte bt_time;
};

typedef u1Byte
(*BFP_BTC_R1)(
	IN	PVOID			pBtcContext,
	IN	u4Byte			RegAddr
	);
typedef u2Byte
(*BFP_BTC_R2)(
	IN	PVOID			pBtcContext,
	IN	u4Byte			RegAddr
	);
typedef u4Byte
(*BFP_BTC_R4)(
	IN	PVOID			pBtcContext,
	IN	u4Byte			RegAddr
	);
typedef VOID
(*BFP_BTC_W1)(
	IN	PVOID			pBtcContext,
	IN	u4Byte			RegAddr,
	IN	u1Byte			Data
	);
typedef VOID
(*BFP_BTC_W1_BIT_MASK)(
	IN	PVOID			pBtcContext,
	IN	u4Byte			regAddr,
	IN	u1Byte			bitMask,
	IN	u1Byte			data1b
	);
typedef VOID
(*BFP_BTC_W2)(
	IN	PVOID			pBtcContext,
	IN	u4Byte			RegAddr,
	IN	u2Byte			Data
	);
typedef VOID
(*BFP_BTC_W4)(
	IN	PVOID			pBtcContext,
	IN	u4Byte			RegAddr,
	IN	u4Byte			Data
	);
typedef VOID
(*BFP_BTC_LOCAL_REG_W1)(
	IN	PVOID			pBtcContext,
	IN	u4Byte			RegAddr,
	IN	u1Byte			Data
	);
typedef u4Byte
(*BFP_BTC_R_LINDIRECT)(
	IN 	PVOID			pBtcContext,
	IN	u2Byte			reg_addr
	);
typedef VOID
(*BFP_BTC_R_SCBD)(
	IN 	PVOID			pBtcContext,
	IN	pu2Byte			score_board_val
	);
typedef VOID
(*BFP_BTC_W_SCBD)(
	IN 	PVOID			pBtcContext,
	IN	u2Byte			bitpos,
	IN	BOOLEAN			state
	);
typedef VOID
(*BFP_BTC_W_LINDIRECT)(
	IN 	PVOID			pBtcContext,
	IN	u2Byte			reg_addr,
	IN	u4Byte			bit_mask,
	IN	u4Byte 			reg_value
	);
typedef VOID
(*BFP_BTC_SET_BB_REG)(
	IN	PVOID			pBtcContext,
	IN	u4Byte			RegAddr,
	IN	u4Byte			BitMask,
	IN	u4Byte			Data
	);
typedef u4Byte
(*BFP_BTC_GET_BB_REG)(
	IN	PVOID			pBtcContext,
	IN	u4Byte			RegAddr,
	IN	u4Byte			BitMask
	);
typedef VOID
(*BFP_BTC_SET_RF_REG)(
	IN	PVOID			pBtcContext,
	IN	enum rf_path		eRFPath,
	IN	u4Byte			RegAddr,
	IN	u4Byte			BitMask,
	IN	u4Byte			Data
	);
typedef u4Byte
(*BFP_BTC_GET_RF_REG)(
	IN	PVOID			pBtcContext,
	IN	enum rf_path		eRFPath,
	IN	u4Byte			RegAddr,
	IN	u4Byte			BitMask
	);
typedef VOID
(*BFP_BTC_FILL_H2C)(
	IN	PVOID			pBtcContext,
	IN	u1Byte			elementId,
	IN	u4Byte			cmdLen,
	IN	pu1Byte			pCmdBuffer
	);

typedef	BOOLEAN
(*BFP_BTC_GET)(
	IN	PVOID			pBtCoexist,
	IN	u1Byte			getType,
	OUT	PVOID			pOutBuf
	);

typedef	BOOLEAN
(*BFP_BTC_SET)(
	IN	PVOID			pBtCoexist,
	IN	u1Byte			setType,
	OUT	PVOID			pInBuf
	);
typedef u2Byte
(*BFP_BTC_SET_BT_REG)(
	IN	PVOID			pBtcContext,
	IN	u1Byte			regType,
	IN	u4Byte			offset,
	IN	u4Byte			value
	);
typedef BOOLEAN
(*BFP_BTC_SET_BT_ANT_DETECTION)(
	IN	PVOID			pBtcContext,
	IN	u1Byte			txTime,
	IN	u1Byte			btChnl
	);

typedef BOOLEAN
(*BFP_BTC_SET_BT_TRX_MASK)(
	IN	PVOID			pBtcContext,
	IN	u1Byte			bt_trx_mask
	);

typedef u4Byte
(*BFP_BTC_GET_BT_REG)(
	IN	PVOID			pBtcContext,
	IN	u1Byte			regType,
	IN	u4Byte			offset
	);
typedef VOID
(*BFP_BTC_DISP_DBG_MSG)(
	IN	PVOID			pBtCoexist,
	IN	u1Byte			dispType
	);

typedef COL_H2C_STATUS
(*BFP_BTC_COEX_H2C_PROCESS)(
	IN	PVOID			pBtCoexist,
	IN	u1Byte			opcode,
	IN	u1Byte			opcode_ver,
	IN	pu1Byte			ph2c_par,
	IN	u1Byte			h2c_par_len
	);

typedef u4Byte
(*BFP_BTC_GET_BT_COEX_SUPPORTED_FEATURE)(
	IN	PVOID			pBtcContext
	);

typedef u4Byte
(*BFP_BTC_GET_BT_COEX_SUPPORTED_VERSION)(
	IN	PVOID			pBtcContext
	);

typedef u4Byte
(*BFP_BTC_GET_PHYDM_VERSION)(
	IN	PVOID			pBtcContext
	);

typedef u1Byte
(*BFP_BTC_SET_TIMER) 	(
	IN	PVOID			pBtcContext,
	IN	u4Byte 			type,
	IN	u4Byte			val
	);

typedef u4Byte
(*BFP_BTC_SET_ATOMIC) 	(
	IN	PVOID			pBtcContext,
	IN	pu4Byte 		target,
	IN	u4Byte			val
	);


typedef VOID
(*BTC_PHYDM_MODIFY_RA_PCR_THRESHLOD)(
	IN	PVOID		pDM_Odm,
	IN	u1Byte		RA_offset_direction,
	IN	u1Byte		RA_threshold_offset
	);

typedef u4Byte
(*BTC_PHYDM_CMNINFOQUERY)(
	IN		PVOID	pDM_Odm,
	IN		u1Byte	info_type
	);

typedef VOID
(*BTC_REDUCE_WL_TX_POWER)(
	IN		PVOID		pDM_Odm,
	IN		s1Byte		tx_power
	);

typedef VOID
(*BTC_PHYDM_MODIFY_ANTDIV_HWSW)(
	IN		PVOID	pDM_Odm,
	IN		u1Byte	type
	);

typedef u1Byte
(*BFP_BTC_GET_ANT_DET_VAL_FROM_BT)(

	IN	PVOID			pBtcContext
	);

typedef u1Byte
(*BFP_BTC_GET_BLE_SCAN_TYPE_FROM_BT)(
	IN	PVOID			pBtcContext
	);

typedef u4Byte
(*BFP_BTC_GET_BLE_SCAN_PARA_FROM_BT)(
	IN	PVOID			pBtcContext,
	IN  u1Byte			scanType
	);

typedef BOOLEAN
(*BFP_BTC_GET_BT_AFH_MAP_FROM_BT)(
	IN	PVOID			pBtcContext,
	IN	u1Byte			mapType,
	OUT	pu1Byte			afhMap
	);

struct  btc_bt_info {
	boolean					bt_disabled;
	boolean				bt_enable_disable_change;
	u8					rssi_adjust_for_agc_table_on;
	u8					rssi_adjust_for_1ant_coex_type;
	boolean					pre_bt_ctrl_agg_buf_size;
	boolean					bt_ctrl_agg_buf_size;
	boolean					pre_reject_agg_pkt;
	boolean					reject_agg_pkt;
	boolean					increase_scan_dev_num;
	boolean					bt_tx_rx_mask;
	u8					pre_agg_buf_size;
	u8					agg_buf_size;
	boolean					bt_busy;
	boolean					limited_dig;
	u16					bt_hci_ver;
	u32					bt_real_fw_ver;
	u32					get_bt_fw_ver_cnt;
	u32					bt_get_fw_ver;
	boolean					miracast_plus_bt;

	boolean					bt_disable_low_pwr;

	boolean					bt_ctrl_lps;
	boolean					bt_lps_on;
	boolean					force_to_roam;	/* for 1Ant solution */
	u8					lps_val;
	u8					rpwm_val;
	u32					ra_mask;
};

struct btc_stack_info {
	boolean					profile_notified;
	u16					hci_version;	/* stack hci version */
	u8					num_of_link;
	boolean					bt_link_exist;
	boolean					sco_exist;
	boolean					acl_exist;
	boolean					a2dp_exist;
	boolean					hid_exist;
	u8					num_of_hid;
	boolean					pan_exist;
	boolean					unknown_acl_exist;
	s8					min_bt_rssi;
};

struct btc_bt_link_info {
	boolean					bt_link_exist;
	boolean					bt_hi_pri_link_exist;
	boolean					sco_exist;
	boolean					sco_only;
	boolean					a2dp_exist;
	boolean					a2dp_only;
	boolean					hid_exist;
	boolean					hid_only;
	boolean					pan_exist;
	boolean					pan_only;
	boolean					slave_role;
	boolean					acl_busy;
};

#ifdef CONFIG_RF4CE_COEXIST
struct btc_rf4ce_info {
	u8					link_state;
};
#endif

struct btc_statistics {
	u32					cnt_bind;
	u32					cnt_power_on;
	u32					cnt_pre_load_firmware;
	u32					cnt_init_hw_config;
	u32					cnt_init_coex_dm;
	u32					cnt_ips_notify;
	u32					cnt_lps_notify;
	u32					cnt_scan_notify;
	u32					cnt_connect_notify;
	u32					cnt_media_status_notify;
	u32					cnt_specific_packet_notify;
	u32					cnt_bt_info_notify;
	u32					cnt_rf_status_notify;
	u32					cnt_periodical;
	u32					cnt_coex_dm_switch;
	u32					cnt_stack_operation_notify;
	u32					cnt_dbg_ctrl;
	u32					cnt_rate_id_notify;
	u32					cnt_halt_notify;
	u32					cnt_pnp_notify;
};

struct btc_coexist {
	BOOLEAN				bBinded;		/*make sure only one adapter can bind the data context*/
	PVOID				Adapter;		/*default adapter*/
	struct  btc_board_info		board_info;
	struct  btc_bt_info			bt_info;		/*some bt info referenced by non-bt module*/
	struct  btc_stack_info		stack_info;
	struct  btc_bt_link_info		bt_link_info;
	struct btc_wifi_link_info	wifi_link_info;
	struct btc_wifi_link_info_ext		wifi_link_info_ext;
	struct btc_coex_dm			coex_dm;
	struct btc_coex_sta			coex_sta;
	struct btc_rfe_type			rfe_type;
	const struct btc_chip_para		*chip_para;

#ifdef CONFIG_RF4CE_COEXIST
	struct  btc_rf4ce_info		rf4ce_info;
#endif
	BTC_CHIP_INTERFACE		chip_interface;
	PVOID					odm_priv;

	BOOLEAN					initilized;
	BOOLEAN					stop_coex_dm;
	BOOLEAN					manual_control;
	BOOLEAN					bdontenterLPS;
	pu1Byte					cli_buf;
	struct btc_statistics		statistics;
	u1Byte				pwrModeVal[10];
	BOOLEAN dbg_mode;
	BOOLEAN auto_report;
	u8	chip_type;
	BOOLEAN wl_rf_state_off;

	/* function pointers */
	/* io related */
	BFP_BTC_R1			btc_read_1byte;
	BFP_BTC_W1			btc_write_1byte;
	BFP_BTC_W1_BIT_MASK	btc_write_1byte_bitmask;
	BFP_BTC_R2			btc_read_2byte;
	BFP_BTC_W2			btc_write_2byte;
	BFP_BTC_R4			btc_read_4byte;
	BFP_BTC_W4			btc_write_4byte;
	BFP_BTC_LOCAL_REG_W1	btc_write_local_reg_1byte;
	BFP_BTC_R_LINDIRECT		btc_read_linderct;
	BFP_BTC_W_LINDIRECT		btc_write_linderct;
	BFP_BTC_R_SCBD			btc_read_scbd;
	BFP_BTC_W_SCBD			btc_write_scbd;
	/* read/write bb related */
	BFP_BTC_SET_BB_REG	btc_set_bb_reg;
	BFP_BTC_GET_BB_REG	btc_get_bb_reg;

	/* read/write rf related */
	BFP_BTC_SET_RF_REG	btc_set_rf_reg;
	BFP_BTC_GET_RF_REG	btc_get_rf_reg;

	/* fill h2c related */
	BFP_BTC_FILL_H2C		btc_fill_h2c;
	/* other */
	BFP_BTC_DISP_DBG_MSG	btc_disp_dbg_msg;
	/* normal get/set related */
	BFP_BTC_GET			btc_get;
	BFP_BTC_SET			btc_set;

	BFP_BTC_GET_BT_REG	btc_get_bt_reg;
	BFP_BTC_SET_BT_REG	btc_set_bt_reg;

	BFP_BTC_SET_BT_ANT_DETECTION	btc_set_bt_ant_detection;

	BFP_BTC_COEX_H2C_PROCESS	btc_coex_h2c_process;
	BFP_BTC_SET_BT_TRX_MASK		btc_set_bt_trx_mask;
	BFP_BTC_GET_BT_COEX_SUPPORTED_FEATURE btc_get_bt_coex_supported_feature;
	BFP_BTC_GET_BT_COEX_SUPPORTED_VERSION btc_get_bt_coex_supported_version;
	BFP_BTC_GET_PHYDM_VERSION		btc_get_bt_phydm_version;
	BFP_BTC_SET_TIMER				btc_set_timer;
	BFP_BTC_SET_ATOMIC			btc_set_atomic;
	BTC_PHYDM_MODIFY_RA_PCR_THRESHLOD	btc_phydm_modify_RA_PCR_threshold;
	BTC_PHYDM_CMNINFOQUERY				btc_phydm_query_PHY_counter;
	BTC_REDUCE_WL_TX_POWER				btc_reduce_wl_tx_power;
	BTC_PHYDM_MODIFY_ANTDIV_HWSW		btc_phydm_modify_antdiv_hwsw;
	BFP_BTC_GET_ANT_DET_VAL_FROM_BT		btc_get_ant_det_val_from_bt;
	BFP_BTC_GET_BLE_SCAN_TYPE_FROM_BT	btc_get_ble_scan_type_from_bt;
	BFP_BTC_GET_BLE_SCAN_PARA_FROM_BT	btc_get_ble_scan_para_from_bt;
	BFP_BTC_GET_BT_AFH_MAP_FROM_BT		btc_get_bt_afh_map_from_bt;

	union {
		#ifdef CONFIG_RTL8822B
		struct coex_dm_8822b_1ant	coex_dm_8822b_1ant;
		struct coex_dm_8822b_2ant	coex_dm_8822b_2ant;
		#endif /* 8822B */
		#ifdef CONFIG_RTL8821C
		struct coex_dm_8821c_1ant	coex_dm_8821c_1ant;
		struct coex_dm_8821c_2ant	coex_dm_8821c_2ant;
		#endif /* 8821C */
        #ifdef CONFIG_RTL8723D
        struct coex_dm_8723d_1ant   coex_dm_8723d_1ant;
        struct coex_dm_8723d_2ant   coex_dm_8723d_2ant;
        #endif /* 8723D */
	};

	union {
		#ifdef CONFIG_RTL8822B
		struct coex_sta_8822b_1ant	coex_sta_8822b_1ant;
		struct coex_sta_8822b_2ant	coex_sta_8822b_2ant;
		#endif /* 8822B */
		#ifdef CONFIG_RTL8821C
		struct coex_sta_8821c_1ant	coex_sta_8821c_1ant;
		struct coex_sta_8821c_2ant	coex_sta_8821c_2ant;
		#endif /* 8821C */
        #ifdef CONFIG_RTL8723D
        struct coex_sta_8723d_1ant  coex_sta_8723d_1ant;
        struct coex_sta_8723d_2ant  coex_sta_8723d_2ant;
        #endif /* 8723D */
	};

	union {
		#ifdef CONFIG_RTL8822B
		struct rfe_type_8822b_1ant	rfe_type_8822b_1ant;
		struct rfe_type_8822b_2ant	rfe_type_8822b_2ant;
		#endif /* 8822B */
		#ifdef CONFIG_RTL8821C
		struct rfe_type_8821c_1ant	rfe_type_8821c_1ant;
		struct rfe_type_8821c_2ant	rfe_type_8821c_2ant;
		#endif /* 8821C */
	};

	union {
		#ifdef CONFIG_RTL8822B
		struct wifi_link_info_8822b_1ant	wifi_link_info_8822b_1ant;
		struct wifi_link_info_8822b_2ant	wifi_link_info_8822b_2ant;
		#endif /* 8822B */
		#ifdef CONFIG_RTL8821C
		struct wifi_link_info_8821c_1ant	wifi_link_info_8821c_1ant;
		struct wifi_link_info_8821c_2ant	wifi_link_info_8821c_2ant;
		#endif /* 8821C */
	};

};
typedef struct btc_coexist *PBTC_COEXIST;

extern struct btc_coexist	GLBtCoexist;

typedef	void
(*BFP_BTC_CHIP_SETUP)(
	IN	PBTC_COEXIST	pBtCoexist,
	IN	u1Byte			setType
	);

struct btc_chip_para {
	const char				*chip_name;
	u32				para_ver_date;
	u32				para_ver;
	u32				bt_desired_ver;
	boolean			scbd_support;
	boolean			mailbox_support;
	boolean			lte_indirect_access;
	boolean			new_scbd10_def; /* TRUE: 1:fix 2M(8822c) */
	u8				indirect_type;	/* 0:17xx, 1:7cx */
	u8				pstdma_type; /* 0: LPSoff, 1:LPSon */
	u8				bt_rssi_type;
	u8				ant_isolation;
	u8				rssi_tolerance;
	u8				rx_path_num;
	u8				wl_rssi_step_num;
	const u8				*wl_rssi_step;
	u8				bt_rssi_step_num;
	const u8				*bt_rssi_step;
	u8				table_sant_num;
	const struct btc_coex_table_para 	*table_sant;
	u8				table_nsant_num;
	const struct btc_coex_table_para 	*table_nsant;
	u8				tdma_sant_num;
	const struct btc_tdma_para 	*tdma_sant;
	u8				tdma_nsant_num;
	const struct btc_tdma_para 	*tdma_nsant;
	u8				wl_rf_para_tx_num;
	const struct btc_rf_para		*wl_rf_para_tx;
	const struct btc_rf_para		*wl_rf_para_rx;
	u8				bt_afh_span_bw20;
	u8				bt_afh_span_bw40;
	u8				afh_5g_num;
	const struct btc_5g_afh_map	*afh_5g;
	BFP_BTC_CHIP_SETUP		chip_setup;
};

BOOLEAN
EXhalbtcoutsrc_InitlizeVariables(
	IN	PVOID		Adapter
	);
VOID
EXhalbtcoutsrc_PowerOnSetting(
	IN	PBTC_COEXIST		pBtCoexist
	);
VOID
EXhalbtcoutsrc_PreLoadFirmware(
	IN	PBTC_COEXIST		pBtCoexist
	);
VOID
EXhalbtcoutsrc_InitHwConfig(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN				bWifiOnly
	);
VOID
EXhalbtcoutsrc_InitCoexDm(
	IN	PBTC_COEXIST		pBtCoexist
	);
VOID
EXhalbtcoutsrc_IpsNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	);
VOID
EXhalbtcoutsrc_LpsNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	);
VOID
EXhalbtcoutsrc_ScanNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	);
VOID
EXhalbtcoutsrc_SetAntennaPathNotify(
	IN	PBTC_COEXIST	pBtCoexist,
	IN	u1Byte			type
	);
VOID
EXhalbtcoutsrc_ConnectNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			action
	);
VOID
EXhalbtcoutsrc_MediaStatusNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	RT_MEDIA_STATUS	mediaStatus
	);
VOID
EXhalbtcoutsrc_SpecificPacketNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			pktType
	);
VOID
EXhalbtcoutsrc_BtInfoNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	pu1Byte			tmpBuf,
	IN	u1Byte			length
	);
VOID
EXhalbtcoutsrc_RfStatusNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte				type
	);
u4Byte
EXhalbtcoutsrc_CoexTimerCheck(
	IN	PBTC_COEXIST		pBtCoexist
	);
u4Byte
EXhalbtcoutsrc_WLStatusCheck(
	IN	PBTC_COEXIST		pBtCoexist
	);
VOID
EXhalbtcoutsrc_WlFwDbgInfoNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	pu1Byte			tmpBuf,
	IN	u1Byte			length
	);
VOID
EXhalbtcoutsrc_rx_rate_change_notify(
	IN	PBTC_COEXIST	pBtCoexist,
	IN 	BOOLEAN			is_data_frame,
	IN	u1Byte			btc_rate_id
	);
VOID
EXhalbtcoutsrc_StackOperationNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	);
VOID
EXhalbtcoutsrc_HaltNotify(
	IN	PBTC_COEXIST		pBtCoexist
	);
VOID
EXhalbtcoutsrc_PnpNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			pnpState
	);
VOID
EXhalbtcoutsrc_TimerNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u4Byte timer_type
);
VOID
EXhalbtcoutsrc_WLStatusChangeNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u4Byte change_type
);
VOID
EXhalbtcoutsrc_CoexDmSwitch(
	IN	PBTC_COEXIST		pBtCoexist
	);
VOID
EXhalbtcoutsrc_Periodical(
	IN	PBTC_COEXIST		pBtCoexist
	);
VOID
EXhalbtcoutsrc_DbgControl(
	IN	PBTC_COEXIST			pBtCoexist,
	IN	u1Byte				opCode,
	IN	u1Byte				opLen,
	IN	pu1Byte				pData
	);
VOID
EXhalbtcoutsrc_AntennaDetection(
	IN	PBTC_COEXIST			pBtCoexist,
	IN	u4Byte					centFreq,
	IN	u4Byte					offset,
	IN	u4Byte					span,
	IN	u4Byte					seconds
	);
VOID
EXhalbtcoutsrc_StackUpdateProfileInfo(
	VOID
	);
VOID
EXhalbtcoutsrc_SetHciVersion(
	IN	u2Byte	hciVersion
	);
VOID
EXhalbtcoutsrc_SetBtPatchVersion(
	IN	u2Byte	btHciVersion,
	IN	u2Byte	btPatchVersion
	);
VOID
EXhalbtcoutsrc_UpdateMinBtRssi(
	IN	s1Byte	btRssi
	);
#if 0
VOID
EXhalbtcoutsrc_SetBtExist(
	IN	BOOLEAN		bBtExist
	);
#endif
VOID
EXhalbtcoutsrc_SetChipType(
	IN	u1Byte		chipType
	);
VOID
EXhalbtcoutsrc_SetAntNum(
	IN	u1Byte		type,
	IN	u1Byte		antNum
	);
VOID
EXhalbtcoutsrc_SetSingleAntPath(
	IN	u1Byte		singleAntPath
	);
VOID
EXhalbtcoutsrc_DisplayBtCoexInfo(
	IN	PBTC_COEXIST		pBtCoexist
	);
VOID
EXhalbtcoutsrc_DisplayAntDetection(
	IN	PBTC_COEXIST		pBtCoexist
	);

#define	MASKBYTE0		0xff
#define	MASKBYTE1		0xff00
#define	MASKBYTE2		0xff0000
#define	MASKBYTE3		0xff000000
#define	MASKHWORD	0xffff0000
#define	MASKLWORD		0x0000ffff
#define	MASKDWORD	0xffffffff
#define	MASK12BITS		0xfff
#define	MASKH4BITS		0xf0000000
#define	MASKOFDM_D	0xffc00000
#define	MASKCCK		0x3f3f3f3f

#endif
