// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#include "coex.h"
#include "debug.h"
#include "fw.h"
#include "mac.h"
#include "ps.h"
#include "reg.h"

#define FCXDEF_STEP 50 /* MUST <= FCXMAX_STEP and match with wl fw*/

enum btc_fbtc_tdma_template {
	CXTD_OFF = 0x0,
	CXTD_OFF_B2,
	CXTD_OFF_EXT,
	CXTD_FIX,
	CXTD_PFIX,
	CXTD_AUTO,
	CXTD_PAUTO,
	CXTD_AUTO2,
	CXTD_PAUTO2,
	CXTD_MAX,
};

enum btc_fbtc_tdma_type {
	CXTDMA_OFF = 0x0,
	CXTDMA_FIX = 0x1,
	CXTDMA_AUTO = 0x2,
	CXTDMA_AUTO2 = 0x3,
	CXTDMA_MAX
};

enum btc_fbtc_tdma_rx_flow_ctrl {
	CXFLC_OFF = 0x0,
	CXFLC_NULLP = 0x1,
	CXFLC_QOSNULL = 0x2,
	CXFLC_CTS = 0x3,
	CXFLC_MAX
};

enum btc_fbtc_tdma_wlan_tx_pause {
	CXTPS_OFF = 0x0,  /* no wl tx pause*/
	CXTPS_ON = 0x1,
	CXTPS_MAX
};

enum btc_mlme_state {
	MLME_NO_LINK,
	MLME_LINKING,
	MLME_LINKED,
};

#define FCXONESLOT_VER 1
struct btc_fbtc_1slot {
	u8 fver;
	u8 sid; /* slot id */
	struct rtw89_btc_fbtc_slot slot;
} __packed;

static const struct rtw89_btc_fbtc_tdma t_def[] = {
	[CXTD_OFF]	= { CXTDMA_OFF,    CXFLC_OFF, CXTPS_OFF, 0, 0, 0, 0, 0},
	[CXTD_OFF_B2]	= { CXTDMA_OFF,    CXFLC_OFF, CXTPS_OFF, 0, 0, 1, 0, 0},
	[CXTD_OFF_EXT]	= { CXTDMA_OFF,    CXFLC_OFF, CXTPS_OFF, 0, 0, 3, 0, 0},
	[CXTD_FIX]	= { CXTDMA_FIX,    CXFLC_OFF, CXTPS_OFF, 0, 0, 0, 0, 0},
	[CXTD_PFIX]	= { CXTDMA_FIX,  CXFLC_NULLP,  CXTPS_ON, 0, 5, 0, 0, 0},
	[CXTD_AUTO]	= { CXTDMA_AUTO,   CXFLC_OFF, CXTPS_OFF, 0, 0, 0, 0, 0},
	[CXTD_PAUTO]	= { CXTDMA_AUTO, CXFLC_NULLP,  CXTPS_ON, 0, 5, 0, 0, 0},
	[CXTD_AUTO2]	= {CXTDMA_AUTO2,   CXFLC_OFF, CXTPS_OFF, 0, 0, 0, 0, 0},
	[CXTD_PAUTO2]	= {CXTDMA_AUTO2, CXFLC_NULLP,  CXTPS_ON, 0, 5, 0, 0, 0}
};

#define __DEF_FBTC_SLOT(__dur, __cxtbl, __cxtype) \
	{ .dur = cpu_to_le16(__dur), .cxtbl = cpu_to_le32(__cxtbl), \
	  .cxtype = cpu_to_le16(__cxtype),}

static const struct rtw89_btc_fbtc_slot s_def[] = {
	[CXST_OFF]	= __DEF_FBTC_SLOT(100, 0x55555555, SLOT_MIX),
	[CXST_B2W]	= __DEF_FBTC_SLOT(5,   0x5a5a5a5a, SLOT_ISO),
	[CXST_W1]	= __DEF_FBTC_SLOT(70,  0x5a5a5a5a, SLOT_ISO),
	[CXST_W2]	= __DEF_FBTC_SLOT(70,  0x5a5a5aaa, SLOT_ISO),
	[CXST_W2B]	= __DEF_FBTC_SLOT(15,  0x5a5a5a5a, SLOT_ISO),
	[CXST_B1]	= __DEF_FBTC_SLOT(100, 0x55555555, SLOT_MIX),
	[CXST_B2]	= __DEF_FBTC_SLOT(7,   0x6a5a5a5a, SLOT_MIX),
	[CXST_B3]	= __DEF_FBTC_SLOT(5,   0x55555555, SLOT_MIX),
	[CXST_B4]	= __DEF_FBTC_SLOT(50,  0x55555555, SLOT_MIX),
	[CXST_LK]	= __DEF_FBTC_SLOT(20,  0x5a5a5a5a, SLOT_ISO),
	[CXST_BLK]	= __DEF_FBTC_SLOT(250, 0x55555555, SLOT_MIX),
	[CXST_E2G]	= __DEF_FBTC_SLOT(20,  0x6a5a5a5a, SLOT_MIX),
	[CXST_E5G]	= __DEF_FBTC_SLOT(20,  0xffffffff, SLOT_MIX),
	[CXST_EBT]	= __DEF_FBTC_SLOT(20,  0x55555555, SLOT_MIX),
	[CXST_ENULL]	= __DEF_FBTC_SLOT(7,   0xaaaaaaaa, SLOT_ISO),
	[CXST_WLK]	= __DEF_FBTC_SLOT(250, 0x6a5a6a5a, SLOT_MIX),
	[CXST_W1FDD]	= __DEF_FBTC_SLOT(35,  0xfafafafa, SLOT_ISO),
	[CXST_B1FDD]	= __DEF_FBTC_SLOT(100, 0xffffffff, SLOT_MIX),
};

static const u32 cxtbl[] = {
	0xffffffff, /* 0 */
	0xaaaaaaaa, /* 1 */
	0x55555555, /* 2 */
	0x66555555, /* 3 */
	0x66556655, /* 4 */
	0x5a5a5a5a, /* 5 */
	0x5a5a5aaa, /* 6 */
	0xaa5a5a5a, /* 7 */
	0x6a5a5a5a, /* 8 */
	0x6a5a5aaa, /* 9 */
	0x6a5a6a5a, /* 10 */
	0x6a5a6aaa, /* 11 */
	0x6afa5afa, /* 12 */
	0xaaaa5aaa, /* 13 */
	0xaaffffaa, /* 14 */
	0xaa5555aa, /* 15 */
	0xfafafafa, /* 16 */
	0xffffddff, /* 17 */
	0xdaffdaff, /* 18 */
	0xfafadafa  /* 19 */
};

struct rtw89_btc_btf_tlv {
	u8 type;
	u8 len;
	u8 val[1];
} __packed;

enum btc_btf_set_report_en {
	RPT_EN_TDMA = BIT(0),
	RPT_EN_CYCLE = BIT(1),
	RPT_EN_MREG = BIT(2),
	RPT_EN_BT_VER_INFO = BIT(3),
	RPT_EN_BT_SCAN_INFO = BIT(4),
	RPT_EN_BT_AFH_MAP = BIT(5),
	RPT_EN_BT_DEVICE_INFO = BIT(6),
	RPT_EN_WL_ALL = GENMASK(2, 0),
	RPT_EN_BT_ALL = GENMASK(6, 3),
	RPT_EN_ALL = GENMASK(6, 0),
};

#define BTF_SET_REPORT_VER 1
struct rtw89_btc_btf_set_report {
	u8 fver;
	__le32 enable;
	__le32 para;
} __packed;

#define BTF_SET_SLOT_TABLE_VER 1
struct rtw89_btc_btf_set_slot_table {
	u8 fver;
	u8 tbl_num;
	u8 buf[];
} __packed;

#define BTF_SET_MON_REG_VER 1
struct rtw89_btc_btf_set_mon_reg {
	u8 fver;
	u8 reg_num;
	u8 buf[];
} __packed;

enum btc_btf_set_cx_policy {
	CXPOLICY_TDMA = 0x0,
	CXPOLICY_SLOT = 0x1,
	CXPOLICY_TYPE = 0x2,
	CXPOLICY_MAX,
};

enum btc_b2w_scoreboard {
	BTC_BSCB_ACT = BIT(0),
	BTC_BSCB_ON = BIT(1),
	BTC_BSCB_WHQL = BIT(2),
	BTC_BSCB_BT_S1 = BIT(3),
	BTC_BSCB_A2DP_ACT = BIT(4),
	BTC_BSCB_RFK_RUN = BIT(5),
	BTC_BSCB_RFK_REQ = BIT(6),
	BTC_BSCB_LPS = BIT(7),
	BTC_BSCB_WLRFK = BIT(11),
	BTC_BSCB_BT_HILNA = BIT(13),
	BTC_BSCB_BT_CONNECT = BIT(16),
	BTC_BSCB_PATCH_CODE = BIT(30),
	BTC_BSCB_ALL = GENMASK(30, 0),
};

enum btc_phymap {
	BTC_PHY_0 = BIT(0),
	BTC_PHY_1 = BIT(1),
	BTC_PHY_ALL = BIT(0) | BIT(1),
};

enum btc_cx_state_map {
	BTC_WIDLE = 0,
	BTC_WBUSY_BNOSCAN,
	BTC_WBUSY_BSCAN,
	BTC_WSCAN_BNOSCAN,
	BTC_WSCAN_BSCAN,
	BTC_WLINKING
};

enum btc_ant_phase {
	BTC_ANT_WPOWERON = 0,
	BTC_ANT_WINIT,
	BTC_ANT_WONLY,
	BTC_ANT_WOFF,
	BTC_ANT_W2G,
	BTC_ANT_W5G,
	BTC_ANT_W25G,
	BTC_ANT_FREERUN,
	BTC_ANT_WRFK,
	BTC_ANT_BRFK,
	BTC_ANT_MAX
};

enum btc_plt {
	BTC_PLT_NONE = 0,
	BTC_PLT_LTE_RX = BIT(0),
	BTC_PLT_GNT_BT_TX = BIT(1),
	BTC_PLT_GNT_BT_RX = BIT(2),
	BTC_PLT_GNT_WL = BIT(3),
	BTC_PLT_BT = BIT(1) | BIT(2),
	BTC_PLT_ALL = 0xf
};

enum btc_cx_poicy_main_type {
	BTC_CXP_OFF = 0,
	BTC_CXP_OFFB,
	BTC_CXP_OFFE,
	BTC_CXP_FIX,
	BTC_CXP_PFIX,
	BTC_CXP_AUTO,
	BTC_CXP_PAUTO,
	BTC_CXP_AUTO2,
	BTC_CXP_PAUTO2,
	BTC_CXP_MANUAL,
	BTC_CXP_USERDEF0,
	BTC_CXP_MAIN_MAX
};

enum btc_cx_poicy_type {
	/* TDMA off + pri: BT > WL */
	BTC_CXP_OFF_BT = (BTC_CXP_OFF << 8) | 0,

	/* TDMA off + pri: WL > BT */
	BTC_CXP_OFF_WL = (BTC_CXP_OFF << 8) | 1,

	/* TDMA off + pri: BT = WL */
	BTC_CXP_OFF_EQ0 = (BTC_CXP_OFF << 8) | 2,

	/* TDMA off + pri: BT = WL > BT_Lo */
	BTC_CXP_OFF_EQ1 = (BTC_CXP_OFF << 8) | 3,

	/* TDMA off + pri: WL = BT, BT_Rx > WL_Lo_Tx */
	BTC_CXP_OFF_EQ2 = (BTC_CXP_OFF << 8) | 4,

	/* TDMA off + pri: WL_Rx = BT, BT_HI > WL_Tx > BT_Lo */
	BTC_CXP_OFF_EQ3 = (BTC_CXP_OFF << 8) | 5,

	/* TDMA off + pri: BT_Hi > WL > BT_Lo */
	BTC_CXP_OFF_BWB0 = (BTC_CXP_OFF << 8) | 6,

	/* TDMA off + pri: WL_Hi-Tx > BT_Hi_Rx, BT_Hi > WL > BT_Lo */
	BTC_CXP_OFF_BWB1 = (BTC_CXP_OFF << 8) | 7,

	/* TDMA off+Bcn-Protect + pri: WL_Hi-Tx > BT_Hi_Rx, BT_Hi > WL > BT_Lo*/
	BTC_CXP_OFFB_BWB0 = (BTC_CXP_OFFB << 8) | 0,

	/* TDMA off + Ext-Ctrl + pri: default */
	BTC_CXP_OFFE_DEF = (BTC_CXP_OFFE << 8) | 0,

	/* TDMA off + Ext-Ctrl + pri: E2G-slot block all BT */
	BTC_CXP_OFFE_DEF2 = (BTC_CXP_OFFE << 8) | 1,

	/* TDMA Fix slot-0: W1:B1 = 30:30 */
	BTC_CXP_FIX_TD3030 = (BTC_CXP_FIX << 8) | 0,

	/* TDMA Fix slot-1: W1:B1 = 50:50 */
	BTC_CXP_FIX_TD5050 = (BTC_CXP_FIX << 8) | 1,

	/* TDMA Fix slot-2: W1:B1 = 20:30 */
	BTC_CXP_FIX_TD2030 = (BTC_CXP_FIX << 8) | 2,

	/* TDMA Fix slot-3: W1:B1 = 40:10 */
	BTC_CXP_FIX_TD4010 = (BTC_CXP_FIX << 8) | 3,

	/* TDMA Fix slot-4: W1:B1 = 70:10 */
	BTC_CXP_FIX_TD7010 = (BTC_CXP_FIX << 8) | 4,

	/* TDMA Fix slot-5: W1:B1 = 20:60 */
	BTC_CXP_FIX_TD2060 = (BTC_CXP_FIX << 8) | 5,

	/* TDMA Fix slot-6: W1:B1 = 30:60 */
	BTC_CXP_FIX_TD3060 = (BTC_CXP_FIX << 8) | 6,

	/* TDMA Fix slot-7: W1:B1 = 20:80 */
	BTC_CXP_FIX_TD2080 = (BTC_CXP_FIX << 8) | 7,

	/* TDMA Fix slot-8: W1:B1 = user-define */
	BTC_CXP_FIX_TDW1B1 = (BTC_CXP_FIX << 8) | 8,

	/* TDMA Fix slot-9: W1:B1 = 40:20 */
	BTC_CXP_FIX_TD4020 = (BTC_CXP_FIX << 8) | 9,

	/* PS-TDMA Fix slot-0: W1:B1 = 30:30 */
	BTC_CXP_PFIX_TD3030 = (BTC_CXP_PFIX << 8) | 0,

	/* PS-TDMA Fix slot-1: W1:B1 = 50:50 */
	BTC_CXP_PFIX_TD5050 = (BTC_CXP_PFIX << 8) | 1,

	/* PS-TDMA Fix slot-2: W1:B1 = 20:30 */
	BTC_CXP_PFIX_TD2030 = (BTC_CXP_PFIX << 8) | 2,

	/* PS-TDMA Fix slot-3: W1:B1 = 20:60 */
	BTC_CXP_PFIX_TD2060 = (BTC_CXP_PFIX << 8) | 3,

	/* PS-TDMA Fix slot-4: W1:B1 = 30:70 */
	BTC_CXP_PFIX_TD3070 = (BTC_CXP_PFIX << 8) | 4,

	/* PS-TDMA Fix slot-5: W1:B1 = 20:80 */
	BTC_CXP_PFIX_TD2080 = (BTC_CXP_PFIX << 8) | 5,

	/* PS-TDMA Fix slot-6: W1:B1 = user-define */
	BTC_CXP_PFIX_TDW1B1 = (BTC_CXP_PFIX << 8) | 6,

	/* TDMA Auto slot-0: W1:B1 = 50:200 */
	BTC_CXP_AUTO_TD50200 = (BTC_CXP_AUTO << 8) | 0,

	/* TDMA Auto slot-1: W1:B1 = 60:200 */
	BTC_CXP_AUTO_TD60200 = (BTC_CXP_AUTO << 8) | 1,

	/* TDMA Auto slot-2: W1:B1 = 20:200 */
	BTC_CXP_AUTO_TD20200 = (BTC_CXP_AUTO << 8) | 2,

	/* TDMA Auto slot-3: W1:B1 = user-define */
	BTC_CXP_AUTO_TDW1B1 = (BTC_CXP_AUTO << 8) | 3,

	/* PS-TDMA Auto slot-0: W1:B1 = 50:200 */
	BTC_CXP_PAUTO_TD50200 = (BTC_CXP_PAUTO << 8) | 0,

	/* PS-TDMA Auto slot-1: W1:B1 = 60:200 */
	BTC_CXP_PAUTO_TD60200 = (BTC_CXP_PAUTO << 8) | 1,

	/* PS-TDMA Auto slot-2: W1:B1 = 20:200 */
	BTC_CXP_PAUTO_TD20200 = (BTC_CXP_PAUTO << 8) | 2,

	/* PS-TDMA Auto slot-3: W1:B1 = user-define */
	BTC_CXP_PAUTO_TDW1B1 = (BTC_CXP_PAUTO << 8) | 3,

	/* TDMA Auto slot2-0: W1:B4 = 30:50 */
	BTC_CXP_AUTO2_TD3050 = (BTC_CXP_AUTO2 << 8) | 0,

	/* TDMA Auto slot2-1: W1:B4 = 30:70 */
	BTC_CXP_AUTO2_TD3070 = (BTC_CXP_AUTO2 << 8) | 1,

	/* TDMA Auto slot2-2: W1:B4 = 50:50 */
	BTC_CXP_AUTO2_TD5050 = (BTC_CXP_AUTO2 << 8) | 2,

	/* TDMA Auto slot2-3: W1:B4 = 60:60 */
	BTC_CXP_AUTO2_TD6060 = (BTC_CXP_AUTO2 << 8) | 3,

	/* TDMA Auto slot2-4: W1:B4 = 20:80 */
	BTC_CXP_AUTO2_TD2080 = (BTC_CXP_AUTO2 << 8) | 4,

	/* TDMA Auto slot2-5: W1:B4 = user-define */
	BTC_CXP_AUTO2_TDW1B4 = (BTC_CXP_AUTO2 << 8) | 5,

	/* PS-TDMA Auto slot2-0: W1:B4 = 30:50 */
	BTC_CXP_PAUTO2_TD3050 = (BTC_CXP_PAUTO2 << 8) | 0,

	/* PS-TDMA Auto slot2-1: W1:B4 = 30:70 */
	BTC_CXP_PAUTO2_TD3070 = (BTC_CXP_PAUTO2 << 8) | 1,

	/* PS-TDMA Auto slot2-2: W1:B4 = 50:50 */
	BTC_CXP_PAUTO2_TD5050 = (BTC_CXP_PAUTO2 << 8) | 2,

	/* PS-TDMA Auto slot2-3: W1:B4 = 60:60 */
	BTC_CXP_PAUTO2_TD6060 = (BTC_CXP_PAUTO2 << 8) | 3,

	/* PS-TDMA Auto slot2-4: W1:B4 = 20:80 */
	BTC_CXP_PAUTO2_TD2080 = (BTC_CXP_PAUTO2 << 8) | 4,

	/* PS-TDMA Auto slot2-5: W1:B4 = user-define */
	BTC_CXP_PAUTO2_TDW1B4 = (BTC_CXP_PAUTO2 << 8) | 5,

	BTC_CXP_MAX = 0xffff
};

enum btc_wl_rfk_result {
	BTC_WRFK_REJECT = 0,
	BTC_WRFK_ALLOW = 1,
};

enum btc_coex_info_map_en {
	BTC_COEX_INFO_CX = BIT(0),
	BTC_COEX_INFO_WL = BIT(1),
	BTC_COEX_INFO_BT = BIT(2),
	BTC_COEX_INFO_DM = BIT(3),
	BTC_COEX_INFO_MREG = BIT(4),
	BTC_COEX_INFO_SUMMARY = BIT(5),
	BTC_COEX_INFO_ALL = GENMASK(7, 0),
};

#define BTC_CXP_MASK GENMASK(15, 8)

enum btc_w2b_scoreboard {
	BTC_WSCB_ACTIVE = BIT(0),
	BTC_WSCB_ON = BIT(1),
	BTC_WSCB_SCAN = BIT(2),
	BTC_WSCB_UNDERTEST = BIT(3),
	BTC_WSCB_RXGAIN = BIT(4),
	BTC_WSCB_WLBUSY = BIT(7),
	BTC_WSCB_EXTFEM = BIT(8),
	BTC_WSCB_TDMA = BIT(9),
	BTC_WSCB_FIX2M = BIT(10),
	BTC_WSCB_WLRFK = BIT(11),
	BTC_WSCB_BTRFK_GNT = BIT(12), /* not used, use mailbox to inform BT */
	BTC_WSCB_BT_HILNA = BIT(13),
	BTC_WSCB_BTLOG = BIT(14),
	BTC_WSCB_ALL = GENMASK(23, 0),
};

enum btc_wl_link_mode {
	BTC_WLINK_NOLINK = 0x0,
	BTC_WLINK_2G_STA,
	BTC_WLINK_2G_AP,
	BTC_WLINK_2G_GO,
	BTC_WLINK_2G_GC,
	BTC_WLINK_2G_SCC,
	BTC_WLINK_2G_MCC,
	BTC_WLINK_25G_MCC,
	BTC_WLINK_25G_DBCC,
	BTC_WLINK_5G,
	BTC_WLINK_2G_NAN,
	BTC_WLINK_OTHER,
	BTC_WLINK_MAX
};

enum btc_bt_hid_type {
	BTC_HID_218 = BIT(0),
	BTC_HID_418 = BIT(1),
	BTC_HID_BLE = BIT(2),
	BTC_HID_RCU = BIT(3),
	BTC_HID_RCU_VOICE = BIT(4),
	BTC_HID_OTHER_LEGACY = BIT(5)
};

enum btc_reset_module {
	BTC_RESET_CX = BIT(0),
	BTC_RESET_DM = BIT(1),
	BTC_RESET_CTRL = BIT(2),
	BTC_RESET_CXDM = BIT(0) | BIT(1),
	BTC_RESET_BTINFO = BIT(3),
	BTC_RESET_MDINFO = BIT(4),
	BTC_RESET_ALL =  GENMASK(7, 0),
};

enum btc_gnt_state {
	BTC_GNT_HW	= 0,
	BTC_GNT_SW_LO,
	BTC_GNT_SW_HI,
	BTC_GNT_MAX
};

enum btc_wl_max_tx_time {
	BTC_MAX_TX_TIME_L1 = 500,
	BTC_MAX_TX_TIME_L2 = 1000,
	BTC_MAX_TX_TIME_L3 = 2000,
	BTC_MAX_TX_TIME_DEF = 5280
};

enum btc_wl_max_tx_retry {
	BTC_MAX_TX_RETRY_L1 = 7,
	BTC_MAX_TX_RETRY_L2 = 15,
	BTC_MAX_TX_RETRY_DEF = 31,
};

enum btc_reason_and_action {
	BTC_RSN_NONE,
	BTC_RSN_NTFY_INIT,
	BTC_RSN_NTFY_SWBAND,
	BTC_RSN_NTFY_WL_STA,
	BTC_RSN_NTFY_RADIO_STATE,
	BTC_RSN_UPDATE_BT_SCBD,
	BTC_RSN_NTFY_WL_RFK,
	BTC_RSN_UPDATE_BT_INFO,
	BTC_RSN_NTFY_SCAN_START,
	BTC_RSN_NTFY_SCAN_FINISH,
	BTC_RSN_NTFY_SPECIFIC_PACKET,
	BTC_RSN_NTFY_POWEROFF,
	BTC_RSN_NTFY_ROLE_INFO,
	BTC_RSN_CMD_SET_COEX,
	BTC_RSN_ACT1_WORK,
	BTC_RSN_BT_DEVINFO_WORK,
	BTC_RSN_RFK_CHK_WORK,
	BTC_RSN_NUM,
	BTC_ACT_NONE = 100,
	BTC_ACT_WL_ONLY,
	BTC_ACT_WL_5G,
	BTC_ACT_WL_OTHER,
	BTC_ACT_WL_IDLE,
	BTC_ACT_WL_NC,
	BTC_ACT_WL_RFK,
	BTC_ACT_WL_INIT,
	BTC_ACT_WL_OFF,
	BTC_ACT_FREERUN,
	BTC_ACT_BT_WHQL,
	BTC_ACT_BT_RFK,
	BTC_ACT_BT_OFF,
	BTC_ACT_BT_IDLE,
	BTC_ACT_BT_HFP,
	BTC_ACT_BT_HID,
	BTC_ACT_BT_A2DP,
	BTC_ACT_BT_A2DPSINK,
	BTC_ACT_BT_PAN,
	BTC_ACT_BT_A2DP_HID,
	BTC_ACT_BT_A2DP_PAN,
	BTC_ACT_BT_PAN_HID,
	BTC_ACT_BT_A2DP_PAN_HID,
	BTC_ACT_WL_25G_MCC,
	BTC_ACT_WL_2G_MCC,
	BTC_ACT_WL_2G_SCC,
	BTC_ACT_WL_2G_AP,
	BTC_ACT_WL_2G_GO,
	BTC_ACT_WL_2G_GC,
	BTC_ACT_WL_2G_NAN,
	BTC_ACT_LAST,
	BTC_ACT_NUM = BTC_ACT_LAST - BTC_ACT_NONE,
	BTC_ACT_EXT_BIT = BIT(14),
	BTC_POLICY_EXT_BIT = BIT(15),
};

#define BTC_FREERUN_ANTISO_MIN 30
#define BTC_TDMA_BTHID_MAX 2
#define BTC_BLINK_NOCONNECT 0

static void _run_coex(struct rtw89_dev *rtwdev,
		      enum btc_reason_and_action reason);
static void _write_scbd(struct rtw89_dev *rtwdev, u32 val, bool state);
static void _update_bt_scbd(struct rtw89_dev *rtwdev, bool only_update);

static void _send_fw_cmd(struct rtw89_dev *rtwdev, u8 h2c_class, u8 h2c_func,
			 void *param, u16 len)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_btf_fwinfo *pfwinfo = &btc->fwinfo;
	struct rtw89_btc_cx *cx = &btc->cx;
	struct rtw89_btc_wl_info *wl = &cx->wl;
	int ret;

	if (!wl->status.map.init_ok) {
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s(): return by btc not init!!\n", __func__);
		pfwinfo->cnt_h2c_fail++;
		return;
	} else if ((wl->status.map.rf_off_pre == 1 && wl->status.map.rf_off == 1) ||
		   (wl->status.map.lps_pre == 1 && wl->status.map.lps == 1)) {
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s(): return by wl off!!\n", __func__);
		pfwinfo->cnt_h2c_fail++;
		return;
	}

	pfwinfo->cnt_h2c++;

	ret = rtw89_fw_h2c_raw_with_hdr(rtwdev, h2c_class, h2c_func, param, len,
					false, true);
	if (ret != 0)
		pfwinfo->cnt_h2c_fail++;
}

static void _reset_btc_var(struct rtw89_dev *rtwdev, u8 type)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_cx *cx = &btc->cx;
	struct rtw89_btc_wl_info *wl = &btc->cx.wl;
	struct rtw89_btc_bt_info *bt = &btc->cx.bt;
	struct rtw89_btc_bt_link_info *bt_linfo = &bt->link_info;
	struct rtw89_btc_wl_link_info *wl_linfo = wl->link_info;
	u8 i;

	rtw89_debug(rtwdev, RTW89_DBG_BTC, "[BTC], %s\n", __func__);

	if (type & BTC_RESET_CX)
		memset(cx, 0, sizeof(*cx));
	else if (type & BTC_RESET_BTINFO) /* only for BT enable */
		memset(bt, 0, sizeof(*bt));

	if (type & BTC_RESET_CTRL) {
		memset(&btc->ctrl, 0, sizeof(btc->ctrl));
		btc->ctrl.trace_step = FCXDEF_STEP;
	}

	/* Init Coex variables that are not zero */
	if (type & BTC_RESET_DM) {
		memset(&btc->dm, 0, sizeof(btc->dm));
		memset(bt_linfo->rssi_state, 0, sizeof(bt_linfo->rssi_state));

		for (i = 0; i < RTW89_MAX_HW_PORT_NUM; i++)
			memset(wl_linfo[i].rssi_state, 0,
			       sizeof(wl_linfo[i].rssi_state));

		/* set the slot_now table to original */
		btc->dm.tdma_now = t_def[CXTD_OFF];
		btc->dm.tdma = t_def[CXTD_OFF];
		memcpy(&btc->dm.slot_now, s_def, sizeof(btc->dm.slot_now));
		memcpy(&btc->dm.slot, s_def, sizeof(btc->dm.slot));

		btc->policy_len = 0;
		btc->bt_req_len = 0;

		btc->dm.coex_info_map = BTC_COEX_INFO_ALL;
		btc->dm.wl_tx_limit.tx_time = BTC_MAX_TX_TIME_DEF;
		btc->dm.wl_tx_limit.tx_retry = BTC_MAX_TX_RETRY_DEF;
	}

	if (type & BTC_RESET_MDINFO)
		memset(&btc->mdinfo, 0, sizeof(btc->mdinfo));
}

#define BTC_FWINFO_BUF 1024

#define BTC_RPT_HDR_SIZE 3
#define BTC_CHK_WLSLOT_DRIFT_MAX 15
#define BTC_CHK_HANG_MAX 3

static void _chk_btc_err(struct rtw89_dev *rtwdev, u8 type, u32 cnt)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_cx *cx = &btc->cx;
	struct rtw89_btc_dm *dm = &btc->dm;
	struct rtw89_btc_bt_info *bt = &cx->bt;

	rtw89_debug(rtwdev, RTW89_DBG_BTC,
		    "[BTC], %s(): type:%d cnt:%d\n",
		    __func__, type, cnt);

	switch (type) {
	case BTC_DCNT_RPT_FREEZE:
		if (dm->cnt_dm[BTC_DCNT_RPT] == cnt && btc->fwinfo.rpt_en_map)
			dm->cnt_dm[BTC_DCNT_RPT_FREEZE]++;
		else
			dm->cnt_dm[BTC_DCNT_RPT_FREEZE] = 0;

		if (dm->cnt_dm[BTC_DCNT_RPT_FREEZE] >= BTC_CHK_HANG_MAX)
			dm->error.map.wl_fw_hang = true;
		else
			dm->error.map.wl_fw_hang = false;

		dm->cnt_dm[BTC_DCNT_RPT] = cnt;
		break;
	case BTC_DCNT_CYCLE_FREEZE:
		if (dm->cnt_dm[BTC_DCNT_CYCLE] == cnt &&
		    (dm->tdma_now.type != CXTDMA_OFF ||
		     dm->tdma_now.ext_ctrl == CXECTL_EXT))
			dm->cnt_dm[BTC_DCNT_CYCLE_FREEZE]++;
		else
			dm->cnt_dm[BTC_DCNT_CYCLE_FREEZE] = 0;

		if (dm->cnt_dm[BTC_DCNT_CYCLE_FREEZE] >= BTC_CHK_HANG_MAX)
			dm->error.map.cycle_hang = true;
		else
			dm->error.map.cycle_hang = false;

		dm->cnt_dm[BTC_DCNT_CYCLE] = cnt;
		break;
	case BTC_DCNT_W1_FREEZE:
		if (dm->cnt_dm[BTC_DCNT_W1] == cnt &&
		    dm->tdma_now.type != CXTDMA_OFF)
			dm->cnt_dm[BTC_DCNT_W1_FREEZE]++;
		else
			dm->cnt_dm[BTC_DCNT_W1_FREEZE] = 0;

		if (dm->cnt_dm[BTC_DCNT_W1_FREEZE] >= BTC_CHK_HANG_MAX)
			dm->error.map.w1_hang = true;
		else
			dm->error.map.w1_hang = false;

		dm->cnt_dm[BTC_DCNT_W1] = cnt;
		break;
	case BTC_DCNT_B1_FREEZE:
		if (dm->cnt_dm[BTC_DCNT_B1] == cnt &&
		    dm->tdma_now.type != CXTDMA_OFF)
			dm->cnt_dm[BTC_DCNT_B1_FREEZE]++;
		else
			dm->cnt_dm[BTC_DCNT_B1_FREEZE] = 0;

		if (dm->cnt_dm[BTC_DCNT_B1_FREEZE] >= BTC_CHK_HANG_MAX)
			dm->error.map.b1_hang = true;
		else
			dm->error.map.b1_hang = false;

		dm->cnt_dm[BTC_DCNT_B1] = cnt;
		break;
	case BTC_DCNT_TDMA_NONSYNC:
		if (cnt != 0) /* if tdma not sync between drv/fw  */
			dm->cnt_dm[BTC_DCNT_TDMA_NONSYNC]++;
		else
			dm->cnt_dm[BTC_DCNT_TDMA_NONSYNC] = 0;

		if (dm->cnt_dm[BTC_DCNT_TDMA_NONSYNC] >= BTC_CHK_HANG_MAX)
			dm->error.map.tdma_no_sync = true;
		else
			dm->error.map.tdma_no_sync = false;
		break;
	case BTC_DCNT_SLOT_NONSYNC:
		if (cnt != 0) /* if slot not sync between drv/fw  */
			dm->cnt_dm[BTC_DCNT_SLOT_NONSYNC]++;
		else
			dm->cnt_dm[BTC_DCNT_SLOT_NONSYNC] = 0;

		if (dm->cnt_dm[BTC_DCNT_SLOT_NONSYNC] >= BTC_CHK_HANG_MAX)
			dm->error.map.tdma_no_sync = true;
		else
			dm->error.map.tdma_no_sync = false;
		break;
	case BTC_DCNT_BTCNT_FREEZE:
		cnt = cx->cnt_bt[BTC_BCNT_HIPRI_RX] +
		      cx->cnt_bt[BTC_BCNT_HIPRI_TX] +
		      cx->cnt_bt[BTC_BCNT_LOPRI_RX] +
		      cx->cnt_bt[BTC_BCNT_LOPRI_TX];

		if (cnt == 0)
			dm->cnt_dm[BTC_DCNT_BTCNT_FREEZE]++;
		else
			dm->cnt_dm[BTC_DCNT_BTCNT_FREEZE] = 0;

		if ((dm->cnt_dm[BTC_DCNT_BTCNT_FREEZE] >= BTC_CHK_HANG_MAX &&
		     bt->enable.now) || (!dm->cnt_dm[BTC_DCNT_BTCNT_FREEZE] &&
		     !bt->enable.now))
			_update_bt_scbd(rtwdev, false);
		break;
	case BTC_DCNT_WL_SLOT_DRIFT:
		if (cnt >= BTC_CHK_WLSLOT_DRIFT_MAX)
			dm->cnt_dm[BTC_DCNT_WL_SLOT_DRIFT]++;
		else
			dm->cnt_dm[BTC_DCNT_WL_SLOT_DRIFT] = 0;

		if (dm->cnt_dm[BTC_DCNT_WL_SLOT_DRIFT] >= BTC_CHK_HANG_MAX)
			dm->error.map.wl_slot_drift = true;
		else
			dm->error.map.wl_slot_drift = false;
		break;
	}
}

static void _update_bt_report(struct rtw89_dev *rtwdev, u8 rpt_type, u8 *pfinfo)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_bt_info *bt = &btc->cx.bt;
	struct rtw89_btc_bt_link_info *bt_linfo = &bt->link_info;
	struct rtw89_btc_bt_a2dp_desc *a2dp = &bt_linfo->a2dp_desc;
	struct rtw89_btc_fbtc_btver *pver = NULL;
	struct rtw89_btc_fbtc_btscan *pscan = NULL;
	struct rtw89_btc_fbtc_btafh *pafh = NULL;
	struct rtw89_btc_fbtc_btdevinfo *pdev = NULL;

	pver = (struct rtw89_btc_fbtc_btver *)pfinfo;
	pscan = (struct rtw89_btc_fbtc_btscan *)pfinfo;
	pafh = (struct rtw89_btc_fbtc_btafh *)pfinfo;
	pdev = (struct rtw89_btc_fbtc_btdevinfo *)pfinfo;

	rtw89_debug(rtwdev, RTW89_DBG_BTC,
		    "[BTC], %s(): rpt_type:%d\n",
		    __func__, rpt_type);

	switch (rpt_type) {
	case BTC_RPT_TYPE_BT_VER:
		bt->ver_info.fw = le32_to_cpu(pver->fw_ver);
		bt->ver_info.fw_coex = le32_get_bits(pver->coex_ver, GENMASK(7, 0));
		bt->feature = le32_to_cpu(pver->feature);
		break;
	case BTC_RPT_TYPE_BT_SCAN:
		memcpy(bt->scan_info, pscan->scan, BTC_SCAN_MAX1);
		break;
	case BTC_RPT_TYPE_BT_AFH:
		memcpy(&bt_linfo->afh_map[0], pafh->afh_l, 4);
		memcpy(&bt_linfo->afh_map[4], pafh->afh_m, 4);
		memcpy(&bt_linfo->afh_map[8], pafh->afh_h, 2);
		break;
	case BTC_RPT_TYPE_BT_DEVICE:
		a2dp->device_name = le32_to_cpu(pdev->dev_name);
		a2dp->vendor_id = le16_to_cpu(pdev->vendor_id);
		a2dp->flush_time = le32_to_cpu(pdev->flush_time);
		break;
	default:
		break;
	}
}

struct rtw89_btc_fbtc_cysta_cpu {
	u8 fver;
	u8 rsvd;
	u16 cycles;
	u16 cycles_a2dp[CXT_FLCTRL_MAX];
	u16 a2dpept;
	u16 a2dpeptto;
	u16 tavg_cycle[CXT_MAX];
	u16 tmax_cycle[CXT_MAX];
	u16 tmaxdiff_cycle[CXT_MAX];
	u16 tavg_a2dp[CXT_FLCTRL_MAX];
	u16 tmax_a2dp[CXT_FLCTRL_MAX];
	u16 tavg_a2dpept;
	u16 tmax_a2dpept;
	u16 tavg_lk;
	u16 tmax_lk;
	u32 slot_cnt[CXST_MAX];
	u32 bcn_cnt[CXBCN_MAX];
	u32 leakrx_cnt;
	u32 collision_cnt;
	u32 skip_cnt;
	u32 exception;
	u32 except_cnt;
	u16 tslot_cycle[BTC_CYCLE_SLOT_MAX];
};

static void rtw89_btc_fbtc_cysta_to_cpu(const struct rtw89_btc_fbtc_cysta *src,
					struct rtw89_btc_fbtc_cysta_cpu *dst)
{
	static_assert(sizeof(*src) == sizeof(*dst));

#define __CPY_U8(_x)	({dst->_x = src->_x; })
#define __CPY_LE16(_x)	({dst->_x = le16_to_cpu(src->_x); })
#define __CPY_LE16S(_x)	({int _i; for (_i = 0; _i < ARRAY_SIZE(dst->_x); _i++) \
				   dst->_x[_i] = le16_to_cpu(src->_x[_i]); })
#define __CPY_LE32(_x)	({dst->_x = le32_to_cpu(src->_x); })
#define __CPY_LE32S(_x)	({int _i; for (_i = 0; _i < ARRAY_SIZE(dst->_x); _i++) \
				   dst->_x[_i] = le32_to_cpu(src->_x[_i]); })

	__CPY_U8(fver);
	__CPY_U8(rsvd);
	__CPY_LE16(cycles);
	__CPY_LE16S(cycles_a2dp);
	__CPY_LE16(a2dpept);
	__CPY_LE16(a2dpeptto);
	__CPY_LE16S(tavg_cycle);
	__CPY_LE16S(tmax_cycle);
	__CPY_LE16S(tmaxdiff_cycle);
	__CPY_LE16S(tavg_a2dp);
	__CPY_LE16S(tmax_a2dp);
	__CPY_LE16(tavg_a2dpept);
	__CPY_LE16(tmax_a2dpept);
	__CPY_LE16(tavg_lk);
	__CPY_LE16(tmax_lk);
	__CPY_LE32S(slot_cnt);
	__CPY_LE32S(bcn_cnt);
	__CPY_LE32(leakrx_cnt);
	__CPY_LE32(collision_cnt);
	__CPY_LE32(skip_cnt);
	__CPY_LE32(exception);
	__CPY_LE32(except_cnt);
	__CPY_LE16S(tslot_cycle);

#undef __CPY_U8
#undef __CPY_LE16
#undef __CPY_LE16S
#undef __CPY_LE32
#undef __CPY_LE32S
}

#define BTC_LEAK_AP_TH 10
#define BTC_CYSTA_CHK_PERIOD 100

struct rtw89_btc_prpt {
	u8 type;
	__le16 len;
	u8 content[];
} __packed;

static u32 _chk_btc_report(struct rtw89_dev *rtwdev,
			   struct rtw89_btc_btf_fwinfo *pfwinfo,
			   u8 *prptbuf, u32 index)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_dm *dm = &btc->dm;
	struct rtw89_btc_rpt_cmn_info *pcinfo = NULL;
	struct rtw89_btc_wl_info *wl = &btc->cx.wl;
	struct rtw89_btc_fbtc_rpt_ctrl *prpt = NULL;
	struct rtw89_btc_fbtc_cysta *pcysta_le32 = NULL;
	struct rtw89_btc_fbtc_cysta_cpu pcysta[1];
	struct rtw89_btc_prpt *btc_prpt = NULL;
	struct rtw89_btc_fbtc_slot *rtp_slot = NULL;
	u8 rpt_type = 0, *rpt_content = NULL, *pfinfo = NULL;
	u16 wl_slot_set = 0;
	u32 trace_step = btc->ctrl.trace_step, rpt_len = 0, diff_t;
	u8 i;

	rtw89_debug(rtwdev, RTW89_DBG_BTC,
		    "[BTC], %s(): index:%d\n",
		    __func__, index);

	if (!prptbuf) {
		pfwinfo->err[BTFRE_INVALID_INPUT]++;
		return 0;
	}

	btc_prpt = (struct rtw89_btc_prpt *)&prptbuf[index];
	rpt_type = btc_prpt->type;
	rpt_len = le16_to_cpu(btc_prpt->len);
	rpt_content = btc_prpt->content;

	rtw89_debug(rtwdev, RTW89_DBG_BTC,
		    "[BTC], %s(): rpt_type:%d\n",
		    __func__, rpt_type);

	switch (rpt_type) {
	case BTC_RPT_TYPE_CTRL:
		pcinfo = &pfwinfo->rpt_ctrl.cinfo;
		pfinfo = (u8 *)(&pfwinfo->rpt_ctrl.finfo);
		pcinfo->req_len = sizeof(pfwinfo->rpt_ctrl.finfo);
		pcinfo->req_fver = BTCRPT_VER;
		pcinfo->rx_len = rpt_len;
		pcinfo->rx_cnt++;
		break;
	case BTC_RPT_TYPE_TDMA:
		pcinfo = &pfwinfo->rpt_fbtc_tdma.cinfo;
		pfinfo = (u8 *)(&pfwinfo->rpt_fbtc_tdma.finfo);
		pcinfo->req_len = sizeof(pfwinfo->rpt_fbtc_tdma.finfo);
		pcinfo->req_fver = FCXTDMA_VER;
		pcinfo->rx_len = rpt_len;
		pcinfo->rx_cnt++;
		break;
	case BTC_RPT_TYPE_SLOT:
		pcinfo = &pfwinfo->rpt_fbtc_slots.cinfo;
		pfinfo = (u8 *)(&pfwinfo->rpt_fbtc_slots.finfo);
		pcinfo->req_len = sizeof(pfwinfo->rpt_fbtc_slots.finfo);
		pcinfo->req_fver = FCXSLOTS_VER;
		pcinfo->rx_len = rpt_len;
		pcinfo->rx_cnt++;
		break;
	case BTC_RPT_TYPE_CYSTA:
		pcinfo = &pfwinfo->rpt_fbtc_cysta.cinfo;
		pfinfo = (u8 *)(&pfwinfo->rpt_fbtc_cysta.finfo);
		pcysta_le32 = &pfwinfo->rpt_fbtc_cysta.finfo;
		rtw89_btc_fbtc_cysta_to_cpu(pcysta_le32, pcysta);
		pcinfo->req_len = sizeof(pfwinfo->rpt_fbtc_cysta.finfo);
		pcinfo->req_fver = FCXCYSTA_VER;
		pcinfo->rx_len = rpt_len;
		pcinfo->rx_cnt++;
		break;
	case BTC_RPT_TYPE_STEP:
		pcinfo = &pfwinfo->rpt_fbtc_step.cinfo;
		pfinfo = (u8 *)(&pfwinfo->rpt_fbtc_step.finfo);
		pcinfo->req_len = sizeof(pfwinfo->rpt_fbtc_step.finfo.step[0]) *
				  trace_step + 8;
		pcinfo->req_fver = FCXSTEP_VER;
		pcinfo->rx_len = rpt_len;
		pcinfo->rx_cnt++;
		break;
	case BTC_RPT_TYPE_NULLSTA:
		pcinfo = &pfwinfo->rpt_fbtc_nullsta.cinfo;
		pfinfo = (u8 *)(&pfwinfo->rpt_fbtc_nullsta.finfo);
		pcinfo->req_len = sizeof(pfwinfo->rpt_fbtc_nullsta.finfo);
		pcinfo->req_fver = FCXNULLSTA_VER;
		pcinfo->rx_len = rpt_len;
		pcinfo->rx_cnt++;
		break;
	case BTC_RPT_TYPE_MREG:
		pcinfo = &pfwinfo->rpt_fbtc_mregval.cinfo;
		pfinfo = (u8 *)(&pfwinfo->rpt_fbtc_mregval.finfo);
		pcinfo->req_len = sizeof(pfwinfo->rpt_fbtc_mregval.finfo);
		pcinfo->req_fver = FCXMREG_VER;
		pcinfo->rx_len = rpt_len;
		pcinfo->rx_cnt++;
		break;
	case BTC_RPT_TYPE_GPIO_DBG:
		pcinfo = &pfwinfo->rpt_fbtc_gpio_dbg.cinfo;
		pfinfo = (u8 *)(&pfwinfo->rpt_fbtc_gpio_dbg.finfo);
		pcinfo->req_len = sizeof(pfwinfo->rpt_fbtc_gpio_dbg.finfo);
		pcinfo->req_fver = FCXGPIODBG_VER;
		pcinfo->rx_len = rpt_len;
		pcinfo->rx_cnt++;
		break;
	case BTC_RPT_TYPE_BT_VER:
		pcinfo = &pfwinfo->rpt_fbtc_btver.cinfo;
		pfinfo = (u8 *)(&pfwinfo->rpt_fbtc_btver.finfo);
		pcinfo->req_len = sizeof(pfwinfo->rpt_fbtc_btver.finfo);
		pcinfo->req_fver = FCX_BTVER_VER;
		pcinfo->rx_len = rpt_len;
		pcinfo->rx_cnt++;
		break;
	case BTC_RPT_TYPE_BT_SCAN:
		pcinfo = &pfwinfo->rpt_fbtc_btscan.cinfo;
		pfinfo = (u8 *)(&pfwinfo->rpt_fbtc_btscan.finfo);
		pcinfo->req_len = sizeof(pfwinfo->rpt_fbtc_btscan.finfo);
		pcinfo->req_fver = FCX_BTSCAN_VER;
		pcinfo->rx_len = rpt_len;
		pcinfo->rx_cnt++;
		break;
	case BTC_RPT_TYPE_BT_AFH:
		pcinfo = &pfwinfo->rpt_fbtc_btafh.cinfo;
		pfinfo = (u8 *)(&pfwinfo->rpt_fbtc_btafh.finfo);
		pcinfo->req_len = sizeof(pfwinfo->rpt_fbtc_btafh.finfo);
		pcinfo->req_fver = FCX_BTAFH_VER;
		pcinfo->rx_len = rpt_len;
		pcinfo->rx_cnt++;
		break;
	case BTC_RPT_TYPE_BT_DEVICE:
		pcinfo = &pfwinfo->rpt_fbtc_btdev.cinfo;
		pfinfo = (u8 *)(&pfwinfo->rpt_fbtc_btdev.finfo);
		pcinfo->req_len = sizeof(pfwinfo->rpt_fbtc_btdev.finfo);
		pcinfo->req_fver = FCX_BTDEVINFO_VER;
		pcinfo->rx_len = rpt_len;
		pcinfo->rx_cnt++;
		break;
	default:
		pfwinfo->err[BTFRE_UNDEF_TYPE]++;
		return 0;
	}

	if (rpt_len != pcinfo->req_len) {
		if (rpt_type < BTC_RPT_TYPE_MAX)
			pfwinfo->len_mismch |= (0x1 << rpt_type);
		else
			pfwinfo->len_mismch |= BIT(31);
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s(): %d rpt_len:%d!=req_len:%d\n",
			    __func__, rpt_type, rpt_len, pcinfo->req_len);

		pcinfo->valid = 0;
		return 0;
	} else if (!pfinfo || !rpt_content || !pcinfo->req_len) {
		pfwinfo->err[BTFRE_EXCEPTION]++;
		pcinfo->valid = 0;
		return 0;
	}

	memcpy(pfinfo, rpt_content, pcinfo->req_len);
	pcinfo->valid = 1;

	if (rpt_type == BTC_RPT_TYPE_TDMA) {
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s(): check %d %zu\n", __func__,
			    BTC_DCNT_TDMA_NONSYNC, sizeof(dm->tdma_now));

		if (memcmp(&dm->tdma_now, &pfwinfo->rpt_fbtc_tdma.finfo,
			   sizeof(dm->tdma_now)) != 0) {
			rtw89_debug(rtwdev, RTW89_DBG_BTC,
				    "[BTC], %s(): %d tdma_now %x %x %x %x %x %x %x %x\n",
				    __func__, BTC_DCNT_TDMA_NONSYNC,
				    dm->tdma_now.type, dm->tdma_now.rxflctrl,
				    dm->tdma_now.txpause, dm->tdma_now.wtgle_n,
				    dm->tdma_now.leak_n, dm->tdma_now.ext_ctrl,
				    dm->tdma_now.rsvd0, dm->tdma_now.rsvd1);

			rtw89_debug(rtwdev, RTW89_DBG_BTC,
				    "[BTC], %s(): %d rpt_fbtc_tdma %x %x %x %x %x %x %x %x\n",
				    __func__, BTC_DCNT_TDMA_NONSYNC,
				    pfwinfo->rpt_fbtc_tdma.finfo.type,
				    pfwinfo->rpt_fbtc_tdma.finfo.rxflctrl,
				    pfwinfo->rpt_fbtc_tdma.finfo.txpause,
				    pfwinfo->rpt_fbtc_tdma.finfo.wtgle_n,
				    pfwinfo->rpt_fbtc_tdma.finfo.leak_n,
				    pfwinfo->rpt_fbtc_tdma.finfo.ext_ctrl,
				    pfwinfo->rpt_fbtc_tdma.finfo.rsvd0,
				    pfwinfo->rpt_fbtc_tdma.finfo.rsvd1);
		}

		_chk_btc_err(rtwdev, BTC_DCNT_TDMA_NONSYNC,
			     memcmp(&dm->tdma_now,
				    &pfwinfo->rpt_fbtc_tdma.finfo,
				    sizeof(dm->tdma_now)));
	}

	if (rpt_type == BTC_RPT_TYPE_SLOT) {
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s(): check %d %zu\n",
			    __func__, BTC_DCNT_SLOT_NONSYNC,
			    sizeof(dm->slot_now));

		if (memcmp(dm->slot_now, pfwinfo->rpt_fbtc_slots.finfo.slot,
			   sizeof(dm->slot_now)) != 0) {
			for (i = 0; i < CXST_MAX; i++) {
				rtp_slot =
				&pfwinfo->rpt_fbtc_slots.finfo.slot[i];
				if (memcmp(&dm->slot_now[i], rtp_slot,
					   sizeof(dm->slot_now[i])) != 0) {
					rtw89_debug(rtwdev, RTW89_DBG_BTC,
						    "[BTC], %s(): %d slot_now[%d] dur=0x%04x tbl=%08x type=0x%04x\n",
						    __func__,
						    BTC_DCNT_SLOT_NONSYNC, i,
						    dm->slot_now[i].dur,
						    dm->slot_now[i].cxtbl,
						    dm->slot_now[i].cxtype);

					rtw89_debug(rtwdev, RTW89_DBG_BTC,
						    "[BTC], %s(): %d rpt_fbtc_slots[%d] dur=0x%04x tbl=%08x type=0x%04x\n",
						    __func__,
						    BTC_DCNT_SLOT_NONSYNC, i,
						    rtp_slot->dur,
						    rtp_slot->cxtbl,
						    rtp_slot->cxtype);
				}
			}
		}
		_chk_btc_err(rtwdev, BTC_DCNT_SLOT_NONSYNC,
			     memcmp(dm->slot_now,
				    pfwinfo->rpt_fbtc_slots.finfo.slot,
				    sizeof(dm->slot_now)));
	}

	if (rpt_type == BTC_RPT_TYPE_CYSTA &&
	    pcysta->cycles >= BTC_CYSTA_CHK_PERIOD) {
		/* Check Leak-AP */
		if (pcysta->slot_cnt[CXST_LK] != 0 &&
		    pcysta->leakrx_cnt != 0 && dm->tdma_now.rxflctrl) {
			if (pcysta->slot_cnt[CXST_LK] <
			    BTC_LEAK_AP_TH * pcysta->leakrx_cnt)
				dm->leak_ap = 1;
		}

		/* Check diff time between WL slot and W1/E2G slot */
		if (dm->tdma_now.type == CXTDMA_OFF &&
		    dm->tdma_now.ext_ctrl == CXECTL_EXT)
			wl_slot_set = le16_to_cpu(dm->slot_now[CXST_E2G].dur);
		else
			wl_slot_set = le16_to_cpu(dm->slot_now[CXST_W1].dur);

		if (pcysta->tavg_cycle[CXT_WL] > wl_slot_set) {
			diff_t = pcysta->tavg_cycle[CXT_WL] - wl_slot_set;
			_chk_btc_err(rtwdev, BTC_DCNT_WL_SLOT_DRIFT, diff_t);
		}

		_chk_btc_err(rtwdev, BTC_DCNT_W1_FREEZE, pcysta->slot_cnt[CXST_W1]);
		_chk_btc_err(rtwdev, BTC_DCNT_W1_FREEZE, pcysta->slot_cnt[CXST_W1]);
		_chk_btc_err(rtwdev, BTC_DCNT_CYCLE_FREEZE, (u32)pcysta->cycles);
	}

	if (rpt_type == BTC_RPT_TYPE_CTRL) {
		prpt = &pfwinfo->rpt_ctrl.finfo;
		btc->fwinfo.rpt_en_map = prpt->rpt_enable;
		wl->ver_info.fw_coex = prpt->wl_fw_coex_ver;
		wl->ver_info.fw = prpt->wl_fw_ver;
		dm->wl_fw_cx_offload = !!(prpt->wl_fw_cx_offload);

		_chk_btc_err(rtwdev, BTC_DCNT_RPT_FREEZE,
			     pfwinfo->event[BTF_EVNT_RPT]);

		/* To avoid I/O if WL LPS or power-off */
		if (wl->status.map.lps != BTC_LPS_RF_OFF && !wl->status.map.rf_off) {
			rtwdev->chip->ops->btc_update_bt_cnt(rtwdev);
			_chk_btc_err(rtwdev, BTC_DCNT_BTCNT_FREEZE, 0);

			btc->cx.cnt_bt[BTC_BCNT_POLUT] =
				rtw89_mac_get_plt_cnt(rtwdev, RTW89_MAC_0);
		}
	}

	if (rpt_type >= BTC_RPT_TYPE_BT_VER &&
	    rpt_type <= BTC_RPT_TYPE_BT_DEVICE)
		_update_bt_report(rtwdev, rpt_type, pfinfo);

	return (rpt_len + BTC_RPT_HDR_SIZE);
}

static void _parse_btc_report(struct rtw89_dev *rtwdev,
			      struct rtw89_btc_btf_fwinfo *pfwinfo,
			      u8 *pbuf, u32 buf_len)
{
	struct rtw89_btc_prpt *btc_prpt = NULL;
	u32 index = 0, rpt_len = 0;

	rtw89_debug(rtwdev, RTW89_DBG_BTC,
		    "[BTC], %s(): buf_len:%d\n",
		    __func__, buf_len);

	while (pbuf) {
		btc_prpt = (struct rtw89_btc_prpt *)&pbuf[index];
		if (index + 2 >= BTC_FWINFO_BUF)
			break;
		/* At least 3 bytes: type(1) & len(2) */
		rpt_len = le16_to_cpu(btc_prpt->len);
		if ((index + rpt_len + BTC_RPT_HDR_SIZE) > buf_len)
			break;

		rpt_len = _chk_btc_report(rtwdev, pfwinfo, pbuf, index);
		if (!rpt_len)
			break;
		index += rpt_len;
	}
}

#define BTC_TLV_HDR_LEN 2

static void _append_tdma(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_dm *dm = &btc->dm;
	struct rtw89_btc_btf_tlv *tlv = NULL;
	struct rtw89_btc_fbtc_tdma *v = NULL;
	u16 len = btc->policy_len;

	if (!btc->update_policy_force &&
	    !memcmp(&dm->tdma, &dm->tdma_now, sizeof(dm->tdma))) {
		rtw89_debug(rtwdev,
			    RTW89_DBG_BTC, "[BTC], %s(): tdma no change!\n",
			    __func__);
		return;
	}

	tlv = (struct rtw89_btc_btf_tlv *)&btc->policy[len];
	v = (struct rtw89_btc_fbtc_tdma *)&tlv->val[0];
	tlv->type = CXPOLICY_TDMA;
	tlv->len = sizeof(*v);

	memcpy(v, &dm->tdma, sizeof(*v));
	btc->policy_len += BTC_TLV_HDR_LEN  + sizeof(*v);

	rtw89_debug(rtwdev, RTW89_DBG_BTC,
		    "[BTC], %s(): type:%d, rxflctrl=%d, txpause=%d, wtgle_n=%d, leak_n=%d, ext_ctrl=%d\n",
		    __func__, dm->tdma.type, dm->tdma.rxflctrl,
		    dm->tdma.txpause, dm->tdma.wtgle_n, dm->tdma.leak_n,
		    dm->tdma.ext_ctrl);
}

static void _append_slot(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_dm *dm = &btc->dm;
	struct rtw89_btc_btf_tlv *tlv = NULL;
	struct btc_fbtc_1slot *v = NULL;
	u16 len = 0;
	u8 i, cnt = 0;

	rtw89_debug(rtwdev, RTW89_DBG_BTC,
		    "[BTC], %s(): A:btc->policy_len = %d\n",
		    __func__, btc->policy_len);

	for (i = 0; i < CXST_MAX; i++) {
		if (!btc->update_policy_force &&
		    !memcmp(&dm->slot[i], &dm->slot_now[i],
			    sizeof(dm->slot[i])))
			continue;

		len = btc->policy_len;

		tlv = (struct rtw89_btc_btf_tlv *)&btc->policy[len];
		v = (struct btc_fbtc_1slot *)&tlv->val[0];
		tlv->type = CXPOLICY_SLOT;
		tlv->len = sizeof(*v);

		v->fver = FCXONESLOT_VER;
		v->sid = i;
		v->slot = dm->slot[i];

		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s(): slot-%d: dur=%d, table=0x%08x, type=%d\n",
			    __func__, i, dm->slot[i].dur, dm->slot[i].cxtbl,
			    dm->slot[i].cxtype);
		cnt++;

		btc->policy_len += BTC_TLV_HDR_LEN  + sizeof(*v);
	}

	if (cnt > 0)
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s(): slot update (cnt=%d)!!\n",
			    __func__, cnt);
}

static void rtw89_btc_fw_en_rpt(struct rtw89_dev *rtwdev,
				u32 rpt_map, bool rpt_state)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_btf_fwinfo *fwinfo = &btc->fwinfo;
	struct rtw89_btc_btf_set_report r = {0};
	u32 val = 0;

	rtw89_debug(rtwdev, RTW89_DBG_BTC,
		    "[BTC], %s(): rpt_map=%x, rpt_state=%x\n",
		    __func__, rpt_map, rpt_state);

	if (rpt_state)
		val = fwinfo->rpt_en_map | rpt_map;
	else
		val = fwinfo->rpt_en_map & ~rpt_map;

	if (val == fwinfo->rpt_en_map)
		return;

	fwinfo->rpt_en_map = val;

	r.fver = BTF_SET_REPORT_VER;
	r.enable = cpu_to_le32(val);
	r.para = cpu_to_le32(rpt_state);

	_send_fw_cmd(rtwdev, BTFC_SET, SET_REPORT_EN, &r, sizeof(r));
}

static void rtw89_btc_fw_set_slots(struct rtw89_dev *rtwdev, u8 num,
				   struct rtw89_btc_fbtc_slot *s)
{
	struct rtw89_btc_btf_set_slot_table *tbl = NULL;
	u8 *ptr = NULL;
	u16 n = 0;

	n = sizeof(*s) * num + sizeof(*tbl);
	tbl = kmalloc(n, GFP_KERNEL);
	if (!tbl)
		return;

	tbl->fver = BTF_SET_SLOT_TABLE_VER;
	tbl->tbl_num = num;
	ptr = &tbl->buf[0];
	memcpy(ptr, s, num * sizeof(*s));

	_send_fw_cmd(rtwdev, BTFC_SET, SET_SLOT_TABLE, tbl, n);

	kfree(tbl);
}

static void btc_fw_set_monreg(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_btc_btf_set_mon_reg *monreg = NULL;
	u8 n, *ptr = NULL, ulen;
	u16 sz = 0;

	n = chip->mon_reg_num;

	rtw89_debug(rtwdev, RTW89_DBG_BTC,
		    "[BTC], %s(): mon_reg_num=%d\n", __func__, n);
	if (n > CXMREG_MAX) {
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s(): mon reg count %d > %d\n",
			    __func__, n, CXMREG_MAX);
		return;
	}

	ulen = sizeof(struct rtw89_btc_fbtc_mreg);
	sz = (ulen * n) + sizeof(*monreg);
	monreg = kmalloc(sz, GFP_KERNEL);
	if (!monreg)
		return;

	monreg->fver = BTF_SET_MON_REG_VER;
	monreg->reg_num = n;
	ptr = &monreg->buf[0];
	memcpy(ptr, chip->mon_reg, n * ulen);
	rtw89_debug(rtwdev, RTW89_DBG_BTC,
		    "[BTC], %s(): sz=%d ulen=%d n=%d\n",
		    __func__, sz, ulen, n);

	_send_fw_cmd(rtwdev, BTFC_SET, SET_MREG_TABLE, (u8 *)monreg, sz);
	kfree(monreg);
	rtw89_btc_fw_en_rpt(rtwdev, RPT_EN_MREG, 1);
}

static void _update_dm_step(struct rtw89_dev *rtwdev,
			    enum btc_reason_and_action reason_or_action)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_dm *dm = &btc->dm;

	/* use ring-structure to store dm step */
	dm->dm_step.step[dm->dm_step.step_pos] = reason_or_action;
	dm->dm_step.step_pos++;

	if (dm->dm_step.step_pos >= ARRAY_SIZE(dm->dm_step.step)) {
		dm->dm_step.step_pos = 0;
		dm->dm_step.step_ov = true;
	}
}

static void _fw_set_policy(struct rtw89_dev *rtwdev, u16 policy_type,
			   enum btc_reason_and_action action)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_dm *dm = &btc->dm;

	dm->run_action = action;

	_update_dm_step(rtwdev, action | BTC_ACT_EXT_BIT);
	_update_dm_step(rtwdev, policy_type | BTC_POLICY_EXT_BIT);

	btc->policy_len = 0;
	btc->policy_type = policy_type;

	_append_tdma(rtwdev);
	_append_slot(rtwdev);

	if (btc->policy_len == 0 || btc->policy_len > RTW89_BTC_POLICY_MAXLEN)
		return;

	rtw89_debug(rtwdev, RTW89_DBG_BTC,
		    "[BTC], %s(): action = %d -> policy type/len: 0x%04x/%d\n",
		    __func__, action, policy_type, btc->policy_len);

	if (dm->tdma.rxflctrl == CXFLC_NULLP ||
	    dm->tdma.rxflctrl == CXFLC_QOSNULL)
		btc->lps = 1;
	else
		btc->lps = 0;

	if (btc->lps == 1)
		rtw89_set_coex_ctrl_lps(rtwdev, btc->lps);

	_send_fw_cmd(rtwdev, BTFC_SET, SET_CX_POLICY,
		     btc->policy, btc->policy_len);

	memcpy(&dm->tdma_now, &dm->tdma, sizeof(dm->tdma_now));
	memcpy(&dm->slot_now, &dm->slot, sizeof(dm->slot_now));

	if (btc->update_policy_force)
		btc->update_policy_force = false;

	if (btc->lps == 0)
		rtw89_set_coex_ctrl_lps(rtwdev, btc->lps);
}

static void _fw_set_drv_info(struct rtw89_dev *rtwdev, u8 type)
{
	switch (type) {
	case CXDRVINFO_INIT:
		rtw89_fw_h2c_cxdrv_init(rtwdev);
		break;
	case CXDRVINFO_ROLE:
		rtw89_fw_h2c_cxdrv_role(rtwdev);
		break;
	case CXDRVINFO_CTRL:
		rtw89_fw_h2c_cxdrv_ctrl(rtwdev);
		break;
	case CXDRVINFO_RFK:
		rtw89_fw_h2c_cxdrv_rfk(rtwdev);
		break;
	default:
		break;
	}
}

static
void btc_fw_event(struct rtw89_dev *rtwdev, u8 evt_id, void *data, u32 len)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_btf_fwinfo *pfwinfo = &btc->fwinfo;

	rtw89_debug(rtwdev, RTW89_DBG_BTC,
		    "[BTC], %s(): evt_id:%d len:%d\n",
		    __func__, evt_id, len);

	if (!len || !data)
		return;

	switch (evt_id) {
	case BTF_EVNT_RPT:
		_parse_btc_report(rtwdev, pfwinfo, data, len);
		break;
	default:
		break;
	}
}

static void _set_gnt_wl(struct rtw89_dev *rtwdev, u8 phy_map, u8 state)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_dm *dm = &btc->dm;
	struct rtw89_mac_ax_gnt *g = dm->gnt.band;
	u8 i;

	if (phy_map > BTC_PHY_ALL)
		return;

	for (i = 0; i < RTW89_PHY_MAX; i++) {
		if (!(phy_map & BIT(i)))
			continue;

		switch (state) {
		case BTC_GNT_HW:
			g[i].gnt_wl_sw_en = 0;
			g[i].gnt_wl = 0;
			break;
		case BTC_GNT_SW_LO:
			g[i].gnt_wl_sw_en = 1;
			g[i].gnt_wl = 0;
			break;
		case BTC_GNT_SW_HI:
			g[i].gnt_wl_sw_en = 1;
			g[i].gnt_wl = 1;
			break;
		}
	}

	rtw89_mac_cfg_gnt(rtwdev, &dm->gnt);
}

#define BTC_TDMA_WLROLE_MAX 2

static void _set_bt_ignore_wlan_act(struct rtw89_dev *rtwdev, u8 enable)
{
	rtw89_debug(rtwdev, RTW89_DBG_BTC,
		    "[BTC], %s(): set bt %s wlan_act\n", __func__,
		    enable ? "ignore" : "do not ignore");

	_send_fw_cmd(rtwdev, BTFC_SET, SET_BT_IGNORE_WLAN_ACT, &enable, 1);
}

#define WL_TX_POWER_NO_BTC_CTRL	GENMASK(31, 0)
#define WL_TX_POWER_ALL_TIME GENMASK(15, 0)
#define WL_TX_POWER_WITH_BT GENMASK(31, 16)
#define WL_TX_POWER_INT_PART GENMASK(8, 2)
#define WL_TX_POWER_FRA_PART GENMASK(1, 0)
#define B_BTC_WL_TX_POWER_SIGN BIT(7)
#define B_TSSI_WL_TX_POWER_SIGN BIT(8)

static void _set_wl_tx_power(struct rtw89_dev *rtwdev, u32 level)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_wl_info *wl = &btc->cx.wl;
	u32 pwr_val;

	if (wl->rf_para.tx_pwr_freerun == level)
		return;

	wl->rf_para.tx_pwr_freerun = level;
	btc->dm.rf_trx_para.wl_tx_power = level;

	rtw89_debug(rtwdev, RTW89_DBG_BTC,
		    "[BTC], %s(): level = %d\n",
		    __func__, level);

	if (level == RTW89_BTC_WL_DEF_TX_PWR) {
		pwr_val = WL_TX_POWER_NO_BTC_CTRL;
	} else { /* only apply "force tx power" */
		pwr_val = FIELD_PREP(WL_TX_POWER_INT_PART, level);
		if (pwr_val > RTW89_BTC_WL_DEF_TX_PWR)
			pwr_val = RTW89_BTC_WL_DEF_TX_PWR;

		if (level & B_BTC_WL_TX_POWER_SIGN)
			pwr_val |= B_TSSI_WL_TX_POWER_SIGN;
		pwr_val |= WL_TX_POWER_WITH_BT;
	}

	chip->ops->btc_set_wl_txpwr_ctrl(rtwdev, pwr_val);
}

static void _set_wl_rx_gain(struct rtw89_dev *rtwdev, u32 level)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_wl_info *wl = &btc->cx.wl;

	if (wl->rf_para.rx_gain_freerun == level)
		return;

	wl->rf_para.rx_gain_freerun = level;
	btc->dm.rf_trx_para.wl_rx_gain = level;

	rtw89_debug(rtwdev, RTW89_DBG_BTC,
		    "[BTC], %s(): level = %d\n",
		    __func__, level);
}

static void _set_bt_tx_power(struct rtw89_dev *rtwdev, u8 level)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_bt_info *bt = &btc->cx.bt;
	u8 buf;

	if (bt->rf_para.tx_pwr_freerun == level)
		return;

	bt->rf_para.tx_pwr_freerun = level;
	btc->dm.rf_trx_para.bt_tx_power = level;

	rtw89_debug(rtwdev, RTW89_DBG_BTC,
		    "[BTC], %s(): level = %d\n",
		    __func__, level);

	buf = (s8)(-level);
	_send_fw_cmd(rtwdev, BTFC_SET, SET_BT_TX_PWR, &buf, 1);
}

#define BTC_BT_RX_NORMAL_LVL 7

static void _set_bt_rx_gain(struct rtw89_dev *rtwdev, u8 level)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_bt_info *bt = &btc->cx.bt;

	if (bt->rf_para.rx_gain_freerun == level ||
	    level > BTC_BT_RX_NORMAL_LVL)
		return;

	bt->rf_para.rx_gain_freerun = level;
	btc->dm.rf_trx_para.bt_rx_gain = level;

	rtw89_debug(rtwdev, RTW89_DBG_BTC,
		    "[BTC], %s(): level = %d\n",
		    __func__, level);

	if (level == BTC_BT_RX_NORMAL_LVL)
		_write_scbd(rtwdev, BTC_WSCB_RXGAIN, false);
	else
		_write_scbd(rtwdev, BTC_WSCB_RXGAIN, true);

	_send_fw_cmd(rtwdev, BTFC_SET, SET_BT_LNA_CONSTRAIN, &level, 1);
}

static void _set_rf_trx_para(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_dm *dm = &btc->dm;
	struct rtw89_btc_wl_info *wl = &btc->cx.wl;
	struct rtw89_btc_bt_info *bt = &btc->cx.bt;
	struct rtw89_btc_rf_trx_para para;
	u32 wl_stb_chg = 0;
	u8 level_id = 0;

	if (!dm->freerun) {
		dm->trx_para_level = 0;
		chip->ops->btc_bt_aci_imp(rtwdev);
	}

	level_id = (u8)dm->trx_para_level;

	if (level_id >= chip->rf_para_dlink_num ||
	    level_id >= chip->rf_para_ulink_num) {
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s(): invalid level_id: %d\n",
			    __func__, level_id);
		return;
	}

	if (wl->status.map.traffic_dir & BIT(RTW89_TFC_UL))
		para = chip->rf_para_ulink[level_id];
	else
		para = chip->rf_para_dlink[level_id];

	if (para.wl_tx_power != RTW89_BTC_WL_DEF_TX_PWR)
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s(): wl_tx_power=%d\n",
			    __func__, para.wl_tx_power);
	_set_wl_tx_power(rtwdev, para.wl_tx_power);
	_set_wl_rx_gain(rtwdev, para.wl_rx_gain);
	_set_bt_tx_power(rtwdev, para.bt_tx_power);
	_set_bt_rx_gain(rtwdev, para.bt_rx_gain);

	if (bt->enable.now == 0 || wl->status.map.rf_off == 1 ||
	    wl->status.map.lps == BTC_LPS_RF_OFF)
		wl_stb_chg = 0;
	else
		wl_stb_chg = 1;

	if (wl_stb_chg != dm->wl_stb_chg) {
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s(): wl_stb_chg=%d\n",
			    __func__, wl_stb_chg);
		dm->wl_stb_chg = wl_stb_chg;
		chip->ops->btc_wl_s1_standby(rtwdev, dm->wl_stb_chg);
	}
}

static void _update_btc_state_map(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_cx *cx = &btc->cx;
	struct rtw89_btc_wl_info *wl = &cx->wl;
	struct rtw89_btc_bt_info *bt = &cx->bt;
	struct rtw89_btc_bt_link_info *bt_linfo = &bt->link_info;

	if (wl->status.map.connecting || wl->status.map._4way ||
	    wl->status.map.roaming) {
		cx->state_map = BTC_WLINKING;
	} else if (wl->status.map.scan) { /* wl scan */
		if (bt_linfo->status.map.inq_pag)
			cx->state_map = BTC_WSCAN_BSCAN;
		else
			cx->state_map = BTC_WSCAN_BNOSCAN;
	} else if (wl->status.map.busy) { /* only busy */
		if (bt_linfo->status.map.inq_pag)
			cx->state_map = BTC_WBUSY_BSCAN;
		else
			cx->state_map = BTC_WBUSY_BNOSCAN;
	} else { /* wl idle */
		cx->state_map = BTC_WIDLE;
	}
}

static void _set_bt_afh_info(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_wl_info *wl = &btc->cx.wl;
	struct rtw89_btc_bt_info *bt = &btc->cx.bt;
	struct rtw89_btc_bt_link_info *b = &bt->link_info;
	struct rtw89_btc_wl_role_info *wl_rinfo = &wl->role_info;
	u8 en = 0, i, ch = 0, bw = 0;

	if (btc->ctrl.manual || wl->status.map.scan)
		return;

	/* TODO if include module->ant.type == BTC_ANT_SHARED */
	if (wl->status.map.rf_off || bt->whql_test ||
	    wl_rinfo->link_mode == BTC_WLINK_NOLINK ||
	    wl_rinfo->link_mode == BTC_WLINK_5G ||
	    wl_rinfo->connect_cnt > BTC_TDMA_WLROLE_MAX) {
		en = false;
	} else if (wl_rinfo->link_mode == BTC_WLINK_2G_MCC ||
		   wl_rinfo->link_mode == BTC_WLINK_2G_SCC) {
		en = true;
		/* get p2p channel */
		for (i = 0; i < RTW89_MAX_HW_PORT_NUM; i++) {
			if (wl_rinfo->active_role[i].role ==
			    RTW89_WIFI_ROLE_P2P_GO ||
			    wl_rinfo->active_role[i].role ==
			    RTW89_WIFI_ROLE_P2P_CLIENT) {
				ch = wl_rinfo->active_role[i].ch;
				bw = wl_rinfo->active_role[i].bw;
				break;
			}
		}
	} else {
		en = true;
		/* get 2g channel  */
		for (i = 0; i < RTW89_MAX_HW_PORT_NUM; i++) {
			if (wl_rinfo->active_role[i].connected &&
			    wl_rinfo->active_role[i].band == RTW89_BAND_2G) {
				ch = wl_rinfo->active_role[i].ch;
				bw = wl_rinfo->active_role[i].bw;
				break;
			}
		}
	}

	switch (bw) {
	case RTW89_CHANNEL_WIDTH_20:
		bw = 20 + chip->afh_guard_ch * 2;
		break;
	case RTW89_CHANNEL_WIDTH_40:
		bw = 40 + chip->afh_guard_ch * 2;
		break;
	case RTW89_CHANNEL_WIDTH_5:
		bw = 5 + chip->afh_guard_ch * 2;
		break;
	case RTW89_CHANNEL_WIDTH_10:
		bw = 10 + chip->afh_guard_ch * 2;
		break;
	default:
		bw = 0;
		en = false; /* turn off AFH info if BW > 40 */
		break;
	}

	if (wl->afh_info.en == en &&
	    wl->afh_info.ch == ch &&
	    wl->afh_info.bw == bw &&
	    b->profile_cnt.last == b->profile_cnt.now) {
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s(): return because no change!\n",
			    __func__);
		return;
	}

	wl->afh_info.en = en;
	wl->afh_info.ch = ch;
	wl->afh_info.bw = bw;

	_send_fw_cmd(rtwdev, BTFC_SET, SET_BT_WL_CH_INFO, &wl->afh_info, 3);

	rtw89_debug(rtwdev, RTW89_DBG_BTC,
		    "[BTC], %s(): en=%d, ch=%d, bw=%d\n",
		    __func__, en, ch, bw);
	btc->cx.cnt_wl[BTC_WCNT_CH_UPDATE]++;
}

static bool _check_freerun(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_wl_info *wl = &btc->cx.wl;
	struct rtw89_btc_bt_info *bt = &btc->cx.bt;
	struct rtw89_btc_wl_role_info *wl_rinfo = &wl->role_info;
	struct rtw89_btc_bt_link_info *bt_linfo = &bt->link_info;
	struct rtw89_btc_bt_hid_desc *hid = &bt_linfo->hid_desc;

	if (btc->mdinfo.ant.type == BTC_ANT_SHARED) {
		btc->dm.trx_para_level = 0;
		return false;
	}

	/* The below is dedicated antenna case */
	if (wl_rinfo->connect_cnt > BTC_TDMA_WLROLE_MAX) {
		btc->dm.trx_para_level = 5;
		return true;
	}

	if (bt_linfo->profile_cnt.now == 0) {
		btc->dm.trx_para_level = 5;
		return true;
	}

	if (hid->pair_cnt > BTC_TDMA_BTHID_MAX) {
		btc->dm.trx_para_level = 5;
		return true;
	}

	/* TODO get isolation by BT psd */
	if (btc->mdinfo.ant.isolation >= BTC_FREERUN_ANTISO_MIN) {
		btc->dm.trx_para_level = 5;
		return true;
	}

	if (!wl->status.map.busy) {/* wl idle -> freerun */
		btc->dm.trx_para_level = 5;
		return true;
	} else if (wl->rssi_level > 1) {/* WL rssi < 50% (-60dBm) */
		btc->dm.trx_para_level = 0;
		return false;
	} else if (wl->status.map.traffic_dir & BIT(RTW89_TFC_UL)) {
		if (wl->rssi_level == 0 && bt_linfo->rssi > 31) {
			btc->dm.trx_para_level = 6;
			return true;
		} else if (wl->rssi_level == 1 && bt_linfo->rssi > 36) {
			btc->dm.trx_para_level = 7;
			return true;
		}
		btc->dm.trx_para_level = 0;
		return false;
	} else if (wl->status.map.traffic_dir & BIT(RTW89_TFC_DL)) {
		if (bt_linfo->rssi > 28) {
			btc->dm.trx_para_level = 6;
			return true;
		}
	}

	btc->dm.trx_para_level = 0;
	return false;
}

#define _tdma_set_flctrl(btc, flc) ({(btc)->dm.tdma.rxflctrl = flc; })
#define _tdma_set_tog(btc, wtg) ({(btc)->dm.tdma.wtgle_n = wtg; })
#define _tdma_set_lek(btc, lek) ({(btc)->dm.tdma.leak_n = lek; })

#define _slot_set(btc, sid, dura, tbl, type) \
	do { \
		typeof(sid) _sid = (sid); \
		typeof(btc) _btc = (btc); \
		_btc->dm.slot[_sid].dur = cpu_to_le16(dura);\
		_btc->dm.slot[_sid].cxtbl = cpu_to_le32(tbl); \
		_btc->dm.slot[_sid].cxtype = cpu_to_le16(type); \
	} while (0)

#define _slot_set_dur(btc, sid, dura) (btc)->dm.slot[sid].dur = cpu_to_le16(dura)
#define _slot_set_tbl(btc, sid, tbl) (btc)->dm.slot[sid].cxtbl = cpu_to_le32(tbl)
#define _slot_set_type(btc, sid, type) (btc)->dm.slot[sid].cxtype = cpu_to_le16(type)

struct btc_btinfo_lb2 {
	u8 connect: 1;
	u8 sco_busy: 1;
	u8 inq_pag: 1;
	u8 acl_busy: 1;
	u8 hfp: 1;
	u8 hid: 1;
	u8 a2dp: 1;
	u8 pan: 1;
};

struct btc_btinfo_lb3 {
	u8 retry: 4;
	u8 cqddr: 1;
	u8 inq: 1;
	u8 mesh_busy: 1;
	u8 pag: 1;
};

struct btc_btinfo_hb0 {
	s8 rssi;
};

struct btc_btinfo_hb1 {
	u8 ble_connect: 1;
	u8 reinit: 1;
	u8 relink: 1;
	u8 igno_wl: 1;
	u8 voice: 1;
	u8 ble_scan: 1;
	u8 role_sw: 1;
	u8 multi_link: 1;
};

struct btc_btinfo_hb2 {
	u8 pan_active: 1;
	u8 afh_update: 1;
	u8 a2dp_active: 1;
	u8 slave: 1;
	u8 hid_slot: 2;
	u8 hid_cnt: 2;
};

struct btc_btinfo_hb3 {
	u8 a2dp_bitpool: 6;
	u8 tx_3m: 1;
	u8 a2dp_sink: 1;
};

union btc_btinfo {
	u8 val;
	struct btc_btinfo_lb2 lb2;
	struct btc_btinfo_lb3 lb3;
	struct btc_btinfo_hb0 hb0;
	struct btc_btinfo_hb1 hb1;
	struct btc_btinfo_hb2 hb2;
	struct btc_btinfo_hb3 hb3;
};

static void _set_policy(struct rtw89_dev *rtwdev, u16 policy_type,
			enum btc_reason_and_action action)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_dm *dm = &btc->dm;
	struct rtw89_btc_fbtc_tdma *t = &dm->tdma;
	struct rtw89_btc_fbtc_slot *s = dm->slot;
	u8 type;
	u32 tbl_w1, tbl_b1, tbl_b4;

	if (btc->mdinfo.ant.type == BTC_ANT_SHARED) {
		if (btc->cx.wl.status.map._4way)
			tbl_w1 = cxtbl[1];
		else
			tbl_w1 = cxtbl[8];
		tbl_b1 = cxtbl[3];
		tbl_b4 = cxtbl[3];
	} else {
		tbl_w1 = cxtbl[16];
		tbl_b1 = cxtbl[17];
		tbl_b4 = cxtbl[17];
	}

	type = (u8)((policy_type & BTC_CXP_MASK) >> 8);
	btc->bt_req_en = false;

	switch (type) {
	case BTC_CXP_USERDEF0:
		*t = t_def[CXTD_OFF];
		s[CXST_OFF] = s_def[CXST_OFF];
		_slot_set_tbl(btc, CXST_OFF, cxtbl[2]);
		btc->update_policy_force = true;
		break;
	case BTC_CXP_OFF: /* TDMA off */
		_write_scbd(rtwdev, BTC_WSCB_TDMA, false);
		*t = t_def[CXTD_OFF];
		s[CXST_OFF] = s_def[CXST_OFF];

		switch (policy_type) {
		case BTC_CXP_OFF_BT:
			_slot_set_tbl(btc, CXST_OFF, cxtbl[2]);
			break;
		case BTC_CXP_OFF_WL:
			_slot_set_tbl(btc, CXST_OFF, cxtbl[1]);
			break;
		case BTC_CXP_OFF_EQ0:
			_slot_set_tbl(btc, CXST_OFF, cxtbl[0]);
			break;
		case BTC_CXP_OFF_EQ1:
			_slot_set_tbl(btc, CXST_OFF, cxtbl[16]);
			break;
		case BTC_CXP_OFF_EQ2:
			_slot_set_tbl(btc, CXST_OFF, cxtbl[17]);
			break;
		case BTC_CXP_OFF_EQ3:
			_slot_set_tbl(btc, CXST_OFF, cxtbl[18]);
			break;
		case BTC_CXP_OFF_BWB0:
			_slot_set_tbl(btc, CXST_OFF, cxtbl[5]);
			break;
		case BTC_CXP_OFF_BWB1:
			_slot_set_tbl(btc, CXST_OFF, cxtbl[8]);
			break;
		}
		break;
	case BTC_CXP_OFFB: /* TDMA off + beacon protect */
		_write_scbd(rtwdev, BTC_WSCB_TDMA, false);
		*t = t_def[CXTD_OFF_B2];
		s[CXST_OFF] = s_def[CXST_OFF];
		switch (policy_type) {
		case BTC_CXP_OFFB_BWB0:
			_slot_set_tbl(btc, CXST_OFF, cxtbl[8]);
			break;
		}
		break;
	case BTC_CXP_OFFE: /* TDMA off + beacon protect + Ext_control */
		btc->bt_req_en = true;
		_write_scbd(rtwdev, BTC_WSCB_TDMA, true);
		*t = t_def[CXTD_OFF_EXT];
		switch (policy_type) {
		case BTC_CXP_OFFE_DEF:
			s[CXST_E2G] = s_def[CXST_E2G];
			s[CXST_E5G] = s_def[CXST_E5G];
			s[CXST_EBT] = s_def[CXST_EBT];
			s[CXST_ENULL] = s_def[CXST_ENULL];
			break;
		case BTC_CXP_OFFE_DEF2:
			_slot_set(btc, CXST_E2G, 20, cxtbl[1], SLOT_ISO);
			s[CXST_E5G] = s_def[CXST_E5G];
			s[CXST_EBT] = s_def[CXST_EBT];
			s[CXST_ENULL] = s_def[CXST_ENULL];
			break;
		}
		break;
	case BTC_CXP_FIX: /* TDMA Fix-Slot */
		_write_scbd(rtwdev, BTC_WSCB_TDMA, true);
		*t = t_def[CXTD_FIX];
		switch (policy_type) {
		case BTC_CXP_FIX_TD3030:
			_slot_set(btc, CXST_W1, 30, tbl_w1, SLOT_ISO);
			_slot_set(btc, CXST_B1, 30, tbl_b1, SLOT_MIX);
			break;
		case BTC_CXP_FIX_TD5050:
			_slot_set(btc, CXST_W1, 50, tbl_w1, SLOT_ISO);
			_slot_set(btc, CXST_B1, 50, tbl_b1, SLOT_MIX);
			break;
		case BTC_CXP_FIX_TD2030:
			_slot_set(btc, CXST_W1, 20, tbl_w1, SLOT_ISO);
			_slot_set(btc, CXST_B1, 30, tbl_b1, SLOT_MIX);
			break;
		case BTC_CXP_FIX_TD4010:
			_slot_set(btc, CXST_W1, 40, tbl_w1, SLOT_ISO);
			_slot_set(btc, CXST_B1, 10, tbl_b1, SLOT_MIX);
			break;
		case BTC_CXP_FIX_TD4020:
			_slot_set(btc, CXST_W1, 40, cxtbl[1], SLOT_MIX);
			_slot_set(btc, CXST_B1, 20, tbl_b1, SLOT_MIX);
			break;
		case BTC_CXP_FIX_TD7010:
			_slot_set(btc, CXST_W1, 70, tbl_w1, SLOT_ISO);
			_slot_set(btc, CXST_B1, 10, tbl_b1, SLOT_MIX);
			break;
		case BTC_CXP_FIX_TD2060:
			_slot_set(btc, CXST_W1, 20, tbl_w1, SLOT_ISO);
			_slot_set(btc, CXST_B1, 60, tbl_b1, SLOT_MIX);
			break;
		case BTC_CXP_FIX_TD3060:
			_slot_set(btc, CXST_W1, 30, tbl_w1, SLOT_ISO);
			_slot_set(btc, CXST_B1, 60, tbl_b1, SLOT_MIX);
			break;
		case BTC_CXP_FIX_TD2080:
			_slot_set(btc, CXST_W1, 20, tbl_w1, SLOT_ISO);
			_slot_set(btc, CXST_B1, 80, tbl_b1, SLOT_MIX);
			break;
		case BTC_CXP_FIX_TDW1B1: /* W1:B1 = user-define */
			_slot_set(btc, CXST_W1, dm->slot_dur[CXST_W1],
				  tbl_w1, SLOT_ISO);
			_slot_set(btc, CXST_B1, dm->slot_dur[CXST_B1],
				  tbl_b1, SLOT_MIX);
			break;
		}
		break;
	case BTC_CXP_PFIX: /* PS-TDMA Fix-Slot */
		_write_scbd(rtwdev, BTC_WSCB_TDMA, true);
		*t = t_def[CXTD_PFIX];
		if (btc->cx.wl.role_info.role_map.role.ap)
			_tdma_set_flctrl(btc, CXFLC_QOSNULL);

		switch (policy_type) {
		case BTC_CXP_PFIX_TD3030:
			_slot_set(btc, CXST_W1, 30, tbl_w1, SLOT_ISO);
			_slot_set(btc, CXST_B1, 30, tbl_b1, SLOT_MIX);
			break;
		case BTC_CXP_PFIX_TD5050:
			_slot_set(btc, CXST_W1, 50, tbl_w1, SLOT_ISO);
			_slot_set(btc, CXST_B1, 50, tbl_b1, SLOT_MIX);
			break;
		case BTC_CXP_PFIX_TD2030:
			_slot_set(btc, CXST_W1, 20, tbl_w1, SLOT_ISO);
			_slot_set(btc, CXST_B1, 30, tbl_b1, SLOT_MIX);
			break;
		case BTC_CXP_PFIX_TD2060:
			_slot_set(btc, CXST_W1, 20, tbl_w1, SLOT_ISO);
			_slot_set(btc, CXST_B1, 60, tbl_b1, SLOT_MIX);
			break;
		case BTC_CXP_PFIX_TD3070:
			_slot_set(btc, CXST_W1, 30, tbl_w1, SLOT_ISO);
			_slot_set(btc, CXST_B1, 60, tbl_b1, SLOT_MIX);
			break;
		case BTC_CXP_PFIX_TD2080:
			_slot_set(btc, CXST_W1, 20, tbl_w1, SLOT_ISO);
			_slot_set(btc, CXST_B1, 80, tbl_b1, SLOT_MIX);
			break;
		}
		break;
	case BTC_CXP_AUTO: /* TDMA Auto-Slot */
		_write_scbd(rtwdev, BTC_WSCB_TDMA, true);
		*t = t_def[CXTD_AUTO];
		switch (policy_type) {
		case BTC_CXP_AUTO_TD50200:
			_slot_set(btc, CXST_W1,  50, tbl_w1, SLOT_ISO);
			_slot_set(btc, CXST_B1, 200, tbl_b1, SLOT_MIX);
			break;
		case BTC_CXP_AUTO_TD60200:
			_slot_set(btc, CXST_W1,  60, tbl_w1, SLOT_ISO);
			_slot_set(btc, CXST_B1, 200, tbl_b1, SLOT_MIX);
			break;
		case BTC_CXP_AUTO_TD20200:
			_slot_set(btc, CXST_W1,  20, tbl_w1, SLOT_ISO);
			_slot_set(btc, CXST_B1, 200, tbl_b1, SLOT_MIX);
			break;
		case BTC_CXP_AUTO_TDW1B1: /* W1:B1 = user-define */
			_slot_set(btc, CXST_W1, dm->slot_dur[CXST_W1],
				  tbl_w1, SLOT_ISO);
			_slot_set(btc, CXST_B1, dm->slot_dur[CXST_B1],
				  tbl_b1, SLOT_MIX);
			break;
		}
		break;
	case BTC_CXP_PAUTO: /* PS-TDMA Auto-Slot */
		_write_scbd(rtwdev, BTC_WSCB_TDMA, true);
		*t = t_def[CXTD_PAUTO];
		switch (policy_type) {
		case BTC_CXP_PAUTO_TD50200:
			_slot_set(btc, CXST_W1,  50, tbl_w1, SLOT_ISO);
			_slot_set(btc, CXST_B1, 200, tbl_b1, SLOT_MIX);
			break;
		case BTC_CXP_PAUTO_TD60200:
			_slot_set(btc, CXST_W1,  60, tbl_w1, SLOT_ISO);
			_slot_set(btc, CXST_B1, 200, tbl_b1, SLOT_MIX);
			break;
		case BTC_CXP_PAUTO_TD20200:
			_slot_set(btc, CXST_W1,  20, tbl_w1, SLOT_ISO);
			_slot_set(btc, CXST_B1, 200, tbl_b1, SLOT_MIX);
			break;
		case BTC_CXP_PAUTO_TDW1B1:
			_slot_set(btc, CXST_W1, dm->slot_dur[CXST_W1],
				  tbl_w1, SLOT_ISO);
			_slot_set(btc, CXST_B1, dm->slot_dur[CXST_B1],
				  tbl_b1, SLOT_MIX);
			break;
		}
		break;
	case BTC_CXP_AUTO2: /* TDMA Auto-Slot2 */
		_write_scbd(rtwdev, BTC_WSCB_TDMA, true);
		*t = t_def[CXTD_AUTO2];
		switch (policy_type) {
		case BTC_CXP_AUTO2_TD3050:
			_slot_set(btc, CXST_W1,  30, tbl_w1, SLOT_ISO);
			_slot_set(btc, CXST_B1, 200, tbl_b1, SLOT_MIX);
			_slot_set(btc, CXST_B4,  50, tbl_b4, SLOT_MIX);
			break;
		case BTC_CXP_AUTO2_TD3070:
			_slot_set(btc, CXST_W1,  30, tbl_w1, SLOT_ISO);
			_slot_set(btc, CXST_B1, 200, tbl_b1, SLOT_MIX);
			_slot_set(btc, CXST_B4,  70, tbl_b4, SLOT_MIX);
			break;
		case BTC_CXP_AUTO2_TD5050:
			_slot_set(btc, CXST_W1,  50, tbl_w1, SLOT_ISO);
			_slot_set(btc, CXST_B1, 200, tbl_b1, SLOT_MIX);
			_slot_set(btc, CXST_B4,  50, tbl_b4, SLOT_MIX);
			break;
		case BTC_CXP_AUTO2_TD6060:
			_slot_set(btc, CXST_W1,  60, tbl_w1, SLOT_ISO);
			_slot_set(btc, CXST_B1, 200, tbl_b1, SLOT_MIX);
			_slot_set(btc, CXST_B4,  60, tbl_b4, SLOT_MIX);
			break;
		case BTC_CXP_AUTO2_TD2080:
			_slot_set(btc, CXST_W1,  20, tbl_w1, SLOT_ISO);
			_slot_set(btc, CXST_B1, 200, tbl_b1, SLOT_MIX);
			_slot_set(btc, CXST_B4,  80, tbl_b4, SLOT_MIX);
			break;
		case BTC_CXP_AUTO2_TDW1B4: /* W1:B1 = user-define */
			_slot_set(btc, CXST_W1, dm->slot_dur[CXST_W1],
				  tbl_w1, SLOT_ISO);
			_slot_set(btc, CXST_B4, dm->slot_dur[CXST_B4],
				  tbl_b4, SLOT_MIX);
			break;
		}
		break;
	case BTC_CXP_PAUTO2: /* PS-TDMA Auto-Slot2 */
		_write_scbd(rtwdev, BTC_WSCB_TDMA, true);
		*t = t_def[CXTD_PAUTO2];
		switch (policy_type) {
		case BTC_CXP_PAUTO2_TD3050:
			_slot_set(btc, CXST_W1,  30, tbl_w1, SLOT_ISO);
			_slot_set(btc, CXST_B1, 200, tbl_b1, SLOT_MIX);
			_slot_set(btc, CXST_B4,  50, tbl_b4, SLOT_MIX);
			break;
		case BTC_CXP_PAUTO2_TD3070:
			_slot_set(btc, CXST_W1,  30, tbl_w1, SLOT_ISO);
			_slot_set(btc, CXST_B1, 200, tbl_b1, SLOT_MIX);
			_slot_set(btc, CXST_B4,  70, tbl_b4, SLOT_MIX);
			break;
		case BTC_CXP_PAUTO2_TD5050:
			_slot_set(btc, CXST_W1,  50, tbl_w1, SLOT_ISO);
			_slot_set(btc, CXST_B1, 200, tbl_b1, SLOT_MIX);
			_slot_set(btc, CXST_B4,  50, tbl_b4, SLOT_MIX);
			break;
		case BTC_CXP_PAUTO2_TD6060:
			_slot_set(btc, CXST_W1,  60, tbl_w1, SLOT_ISO);
			_slot_set(btc, CXST_B1, 200, tbl_b1, SLOT_MIX);
			_slot_set(btc, CXST_B4,  60, tbl_b4, SLOT_MIX);
			break;
		case BTC_CXP_PAUTO2_TD2080:
			_slot_set(btc, CXST_W1,  20, tbl_w1, SLOT_ISO);
			_slot_set(btc, CXST_B1, 200, tbl_b1, SLOT_MIX);
			_slot_set(btc, CXST_B4,  80, tbl_b4, SLOT_MIX);
			break;
		case BTC_CXP_PAUTO2_TDW1B4: /* W1:B1 = user-define */
			_slot_set(btc, CXST_W1, dm->slot_dur[CXST_W1],
				  tbl_w1, SLOT_ISO);
			_slot_set(btc, CXST_B4, dm->slot_dur[CXST_B4],
				  tbl_b4, SLOT_MIX);
			break;
		}
		break;
	}

	_fw_set_policy(rtwdev, policy_type, action);
}

static void _set_gnt_bt(struct rtw89_dev *rtwdev, u8 phy_map, u8 state)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_dm *dm = &btc->dm;
	struct rtw89_mac_ax_gnt *g = dm->gnt.band;
	u8 i;

	if (phy_map > BTC_PHY_ALL)
		return;

	for (i = 0; i < RTW89_PHY_MAX; i++) {
		if (!(phy_map & BIT(i)))
			continue;

		switch (state) {
		case BTC_GNT_HW:
			g[i].gnt_bt_sw_en = 0;
			g[i].gnt_bt = 0;
			break;
		case BTC_GNT_SW_LO:
			g[i].gnt_bt_sw_en = 1;
			g[i].gnt_bt = 0;
			break;
		case BTC_GNT_SW_HI:
			g[i].gnt_bt_sw_en = 1;
			g[i].gnt_bt = 1;
			break;
		}
	}

	rtw89_mac_cfg_gnt(rtwdev, &dm->gnt);
}

static void _set_bt_plut(struct rtw89_dev *rtwdev, u8 phy_map,
			 u8 tx_val, u8 rx_val)
{
	struct rtw89_mac_ax_plt plt;

	plt.band = RTW89_MAC_0;
	plt.tx = tx_val;
	plt.rx = rx_val;

	if (phy_map & BTC_PHY_0)
		rtw89_mac_cfg_plt(rtwdev, &plt);

	if (!rtwdev->dbcc_en)
		return;

	plt.band = RTW89_MAC_1;
	if (phy_map & BTC_PHY_1)
		rtw89_mac_cfg_plt(rtwdev, &plt);
}

static void _set_ant(struct rtw89_dev *rtwdev, bool force_exec,
		     u8 phy_map, u8 type)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_dm *dm = &btc->dm;
	struct rtw89_btc_cx *cx = &btc->cx;
	struct rtw89_btc_wl_info *wl = &btc->cx.wl;
	struct rtw89_btc_bt_info *bt = &cx->bt;
	struct rtw89_btc_wl_dbcc_info *wl_dinfo = &wl->dbcc_info;
	u8 gnt_wl_ctrl, gnt_bt_ctrl, plt_ctrl, i, b2g = 0;
	u32 ant_path_type;

	ant_path_type = ((phy_map << 8) + type);

	if (btc->dm.run_reason == BTC_RSN_NTFY_POWEROFF ||
	    btc->dm.run_reason == BTC_RSN_NTFY_RADIO_STATE ||
	    btc->dm.run_reason == BTC_RSN_CMD_SET_COEX)
		force_exec = FC_EXEC;

	if (!force_exec && ant_path_type == dm->set_ant_path) {
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s(): return by no change!!\n",
			     __func__);
		return;
	} else if (bt->rfk_info.map.run) {
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s(): return by bt rfk!!\n", __func__);
		return;
	} else if (btc->dm.run_reason != BTC_RSN_NTFY_WL_RFK &&
		   wl->rfk_info.state != BTC_WRFK_STOP) {
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s(): return by wl rfk!!\n", __func__);
		return;
	}

	dm->set_ant_path = ant_path_type;

	rtw89_debug(rtwdev,
		    RTW89_DBG_BTC,
		    "[BTC], %s(): path=0x%x, set_type=0x%x\n",
		    __func__, phy_map, dm->set_ant_path & 0xff);

	switch (type) {
	case BTC_ANT_WPOWERON:
		rtw89_mac_cfg_ctrl_path(rtwdev, false);
		break;
	case BTC_ANT_WINIT:
		if (bt->enable.now) {
			_set_gnt_wl(rtwdev, phy_map, BTC_GNT_SW_LO);
			_set_gnt_bt(rtwdev, phy_map, BTC_GNT_SW_HI);
		} else {
			_set_gnt_wl(rtwdev, phy_map, BTC_GNT_SW_HI);
			_set_gnt_bt(rtwdev, phy_map, BTC_GNT_SW_LO);
		}
		rtw89_mac_cfg_ctrl_path(rtwdev, true);
		_set_bt_plut(rtwdev, BTC_PHY_ALL, BTC_PLT_BT, BTC_PLT_BT);
		break;
	case BTC_ANT_WONLY:
		_set_gnt_wl(rtwdev, phy_map, BTC_GNT_SW_HI);
		_set_gnt_bt(rtwdev, phy_map, BTC_GNT_SW_LO);
		rtw89_mac_cfg_ctrl_path(rtwdev, true);
		_set_bt_plut(rtwdev, BTC_PHY_ALL, BTC_PLT_NONE, BTC_PLT_NONE);
		break;
	case BTC_ANT_WOFF:
		rtw89_mac_cfg_ctrl_path(rtwdev, false);
		_set_bt_plut(rtwdev, BTC_PHY_ALL, BTC_PLT_NONE, BTC_PLT_NONE);
		break;
	case BTC_ANT_W2G:
		rtw89_mac_cfg_ctrl_path(rtwdev, true);
		if (rtwdev->dbcc_en) {
			for (i = 0; i < RTW89_PHY_MAX; i++) {
				b2g = (wl_dinfo->real_band[i] == RTW89_BAND_2G);

				gnt_wl_ctrl = b2g ? BTC_GNT_HW : BTC_GNT_SW_HI;
				_set_gnt_wl(rtwdev, BIT(i), gnt_wl_ctrl);

				gnt_bt_ctrl = b2g ? BTC_GNT_HW : BTC_GNT_SW_HI;
				/* BT should control by GNT_BT if WL_2G at S0 */
				if (i == 1 &&
				    wl_dinfo->real_band[0] == RTW89_BAND_2G &&
				    wl_dinfo->real_band[1] == RTW89_BAND_5G)
					gnt_bt_ctrl = BTC_GNT_HW;
				_set_gnt_bt(rtwdev, BIT(i), gnt_bt_ctrl);

				plt_ctrl = b2g ? BTC_PLT_BT : BTC_PLT_NONE;
				_set_bt_plut(rtwdev, BIT(i),
					     plt_ctrl, plt_ctrl);
			}
		} else {
			_set_gnt_wl(rtwdev, phy_map, BTC_GNT_HW);
			_set_gnt_bt(rtwdev, phy_map, BTC_GNT_HW);
			_set_bt_plut(rtwdev, BTC_PHY_ALL,
				     BTC_PLT_BT, BTC_PLT_BT);
		}
		break;
	case BTC_ANT_W5G:
		rtw89_mac_cfg_ctrl_path(rtwdev, true);
		_set_gnt_wl(rtwdev, phy_map, BTC_GNT_SW_HI);
		_set_gnt_bt(rtwdev, phy_map, BTC_GNT_HW);
		_set_bt_plut(rtwdev, BTC_PHY_ALL, BTC_PLT_NONE, BTC_PLT_NONE);
		break;
	case BTC_ANT_W25G:
		rtw89_mac_cfg_ctrl_path(rtwdev, true);
		_set_gnt_wl(rtwdev, phy_map, BTC_GNT_HW);
		_set_gnt_bt(rtwdev, phy_map, BTC_GNT_HW);
		_set_bt_plut(rtwdev, BTC_PHY_ALL,
			     BTC_PLT_GNT_WL, BTC_PLT_GNT_WL);
		break;
	case BTC_ANT_FREERUN:
		rtw89_mac_cfg_ctrl_path(rtwdev, true);
		_set_gnt_wl(rtwdev, phy_map, BTC_GNT_SW_HI);
		_set_gnt_bt(rtwdev, phy_map, BTC_GNT_SW_HI);
		_set_bt_plut(rtwdev, BTC_PHY_ALL, BTC_PLT_NONE, BTC_PLT_NONE);
		break;
	case BTC_ANT_WRFK:
		rtw89_mac_cfg_ctrl_path(rtwdev, true);
		_set_gnt_wl(rtwdev, phy_map, BTC_GNT_SW_HI);
		_set_gnt_bt(rtwdev, phy_map, BTC_GNT_SW_LO);
		_set_bt_plut(rtwdev, phy_map, BTC_PLT_NONE, BTC_PLT_NONE);
		break;
	case BTC_ANT_BRFK:
		rtw89_mac_cfg_ctrl_path(rtwdev, false);
		_set_gnt_wl(rtwdev, phy_map, BTC_GNT_SW_LO);
		_set_gnt_bt(rtwdev, phy_map, BTC_GNT_SW_HI);
		_set_bt_plut(rtwdev, phy_map, BTC_PLT_NONE, BTC_PLT_NONE);
		break;
	default:
		break;
	}
}

static void _action_wl_only(struct rtw89_dev *rtwdev)
{
	_set_ant(rtwdev, FC_EXEC, BTC_PHY_ALL, BTC_ANT_WONLY);
	_set_policy(rtwdev, BTC_CXP_OFF_BT, BTC_ACT_WL_ONLY);
}

static void _action_wl_init(struct rtw89_dev *rtwdev)
{
	rtw89_debug(rtwdev, RTW89_DBG_BTC, "[BTC], %s(): !!\n", __func__);

	_set_ant(rtwdev, FC_EXEC, BTC_PHY_ALL, BTC_ANT_WINIT);
	_set_policy(rtwdev, BTC_CXP_OFF_BT, BTC_ACT_WL_INIT);
}

static void _action_wl_off(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_wl_info *wl = &btc->cx.wl;

	rtw89_debug(rtwdev, RTW89_DBG_BTC, "[BTC], %s(): !!\n", __func__);

	if (wl->status.map.rf_off || btc->dm.bt_only)
		_set_ant(rtwdev, NM_EXEC, BTC_PHY_ALL, BTC_ANT_WOFF);

	_set_policy(rtwdev, BTC_CXP_OFF_BT, BTC_ACT_WL_OFF);
}

static void _action_freerun(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;

	rtw89_debug(rtwdev, RTW89_DBG_BTC, "[BTC], %s(): !!\n", __func__);

	_set_ant(rtwdev, FC_EXEC, BTC_PHY_ALL, BTC_ANT_FREERUN);
	_set_policy(rtwdev, BTC_CXP_OFF_BT, BTC_ACT_FREERUN);

	btc->dm.freerun = true;
}

static void _action_bt_whql(struct rtw89_dev *rtwdev)
{
	rtw89_debug(rtwdev, RTW89_DBG_BTC, "[BTC], %s(): !!\n", __func__);

	_set_ant(rtwdev, FC_EXEC, BTC_PHY_ALL, BTC_ANT_W2G);
	_set_policy(rtwdev, BTC_CXP_OFF_BT, BTC_ACT_BT_WHQL);
}

static void _action_bt_off(struct rtw89_dev *rtwdev)
{
	rtw89_debug(rtwdev, RTW89_DBG_BTC, "[BTC], %s(): !!\n", __func__);

	_set_ant(rtwdev, FC_EXEC, BTC_PHY_ALL, BTC_ANT_WONLY);
	_set_policy(rtwdev, BTC_CXP_OFF_BT, BTC_ACT_BT_OFF);
}

static void _action_bt_idle(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_bt_link_info *b = &btc->cx.bt.link_info;

	_set_ant(rtwdev, NM_EXEC, BTC_PHY_ALL, BTC_ANT_W2G);

	if (btc->mdinfo.ant.type == BTC_ANT_SHARED) { /* shared-antenna */
		switch (btc->cx.state_map) {
		case BTC_WBUSY_BNOSCAN: /*wl-busy + bt idle*/
			if (b->profile_cnt.now > 0)
				_set_policy(rtwdev, BTC_CXP_FIX_TD4010,
					    BTC_ACT_BT_IDLE);
			else
				_set_policy(rtwdev, BTC_CXP_FIX_TD4020,
					    BTC_ACT_BT_IDLE);
			break;
		case BTC_WBUSY_BSCAN: /*wl-busy + bt-inq */
			_set_policy(rtwdev, BTC_CXP_PFIX_TD5050,
				    BTC_ACT_BT_IDLE);
			break;
		case BTC_WSCAN_BNOSCAN: /* wl-scan + bt-idle */
			if (b->profile_cnt.now > 0)
				_set_policy(rtwdev, BTC_CXP_FIX_TD4010,
					    BTC_ACT_BT_IDLE);
			else
				_set_policy(rtwdev, BTC_CXP_FIX_TD4020,
					    BTC_ACT_BT_IDLE);
			break;
		case BTC_WSCAN_BSCAN: /* wl-scan + bt-inq */
			_set_policy(rtwdev, BTC_CXP_FIX_TD5050,
				    BTC_ACT_BT_IDLE);
			break;
		case BTC_WLINKING: /* wl-connecting + bt-inq or bt-idle */
			_set_policy(rtwdev, BTC_CXP_FIX_TD7010,
				    BTC_ACT_BT_IDLE);
			break;
		case BTC_WIDLE:  /* wl-idle + bt-idle */
			_set_policy(rtwdev, BTC_CXP_OFF_BWB1, BTC_ACT_BT_IDLE);
			break;
		}
	} else { /* dedicated-antenna */
		_set_policy(rtwdev, BTC_CXP_OFF_EQ0, BTC_ACT_BT_IDLE);
	}
}

static void _action_bt_hfp(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;

	_set_ant(rtwdev, NM_EXEC, BTC_PHY_ALL, BTC_ANT_W2G);

	if (btc->mdinfo.ant.type == BTC_ANT_SHARED) {
		if (btc->cx.wl.status.map._4way)
			_set_policy(rtwdev, BTC_CXP_OFF_WL, BTC_ACT_BT_HFP);
		else
			_set_policy(rtwdev, BTC_CXP_OFF_BWB0, BTC_ACT_BT_HFP);
	} else {
		_set_policy(rtwdev, BTC_CXP_OFF_EQ2, BTC_ACT_BT_HFP);
	}
}

static void _action_bt_hid(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;

	_set_ant(rtwdev, NM_EXEC, BTC_PHY_ALL, BTC_ANT_W2G);

	if (btc->mdinfo.ant.type == BTC_ANT_SHARED) /* shared-antenna */
		if (btc->cx.wl.status.map._4way)
			_set_policy(rtwdev, BTC_CXP_OFF_WL, BTC_ACT_BT_HID);
		else
			_set_policy(rtwdev, BTC_CXP_OFF_BWB0, BTC_ACT_BT_HID);
	else /* dedicated-antenna */
		_set_policy(rtwdev, BTC_CXP_OFF_EQ3, BTC_ACT_BT_HID);
}

static void _action_bt_a2dp(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_bt_link_info *bt_linfo = &btc->cx.bt.link_info;
	struct rtw89_btc_bt_a2dp_desc a2dp = bt_linfo->a2dp_desc;
	struct rtw89_btc_dm *dm = &btc->dm;

	_set_ant(rtwdev, NM_EXEC, BTC_PHY_ALL, BTC_ANT_W2G);

	switch (btc->cx.state_map) {
	case BTC_WBUSY_BNOSCAN: /* wl-busy + bt-A2DP */
		if (a2dp.vendor_id == 0x4c || dm->leak_ap) {
			dm->slot_dur[CXST_W1] = 40;
			dm->slot_dur[CXST_B1] = 200;
			_set_policy(rtwdev,
				    BTC_CXP_PAUTO_TDW1B1, BTC_ACT_BT_A2DP);
		} else {
			_set_policy(rtwdev,
				    BTC_CXP_PAUTO_TD50200, BTC_ACT_BT_A2DP);
		}
		break;
	case BTC_WBUSY_BSCAN: /* wl-busy + bt-inq + bt-A2DP */
		_set_policy(rtwdev, BTC_CXP_PAUTO2_TD3050, BTC_ACT_BT_A2DP);
		break;
	case BTC_WSCAN_BSCAN: /* wl-scan + bt-inq + bt-A2DP */
		_set_policy(rtwdev, BTC_CXP_AUTO2_TD3050, BTC_ACT_BT_A2DP);
		break;
	case BTC_WSCAN_BNOSCAN: /* wl-scan + bt-A2DP */
	case BTC_WLINKING: /* wl-connecting + bt-A2DP */
		if (a2dp.vendor_id == 0x4c || dm->leak_ap) {
			dm->slot_dur[CXST_W1] = 40;
			dm->slot_dur[CXST_B1] = 200;
			_set_policy(rtwdev, BTC_CXP_AUTO_TDW1B1,
				    BTC_ACT_BT_A2DP);
		} else {
			_set_policy(rtwdev, BTC_CXP_AUTO_TD50200,
				    BTC_ACT_BT_A2DP);
		}
		break;
	case BTC_WIDLE:  /* wl-idle + bt-A2DP */
		_set_policy(rtwdev, BTC_CXP_AUTO_TD20200, BTC_ACT_BT_A2DP);
		break;
	}
}

static void _action_bt_a2dpsink(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;

	_set_ant(rtwdev, NM_EXEC, BTC_PHY_ALL, BTC_ANT_W2G);

	switch (btc->cx.state_map) {
	case BTC_WBUSY_BNOSCAN: /* wl-busy + bt-A2dp_Sink */
		_set_policy(rtwdev, BTC_CXP_PFIX_TD2030, BTC_ACT_BT_A2DPSINK);
		break;
	case BTC_WBUSY_BSCAN: /* wl-busy + bt-inq + bt-A2dp_Sink */
		_set_policy(rtwdev, BTC_CXP_PFIX_TD2060, BTC_ACT_BT_A2DPSINK);
		break;
	case BTC_WSCAN_BNOSCAN: /* wl-scan + bt-A2dp_Sink */
		_set_policy(rtwdev, BTC_CXP_FIX_TD2030, BTC_ACT_BT_A2DPSINK);
		break;
	case BTC_WSCAN_BSCAN: /* wl-scan + bt-inq + bt-A2dp_Sink */
		_set_policy(rtwdev, BTC_CXP_FIX_TD2060, BTC_ACT_BT_A2DPSINK);
		break;
	case BTC_WLINKING: /* wl-connecting + bt-A2dp_Sink */
		_set_policy(rtwdev, BTC_CXP_FIX_TD3030, BTC_ACT_BT_A2DPSINK);
		break;
	case BTC_WIDLE: /* wl-idle + bt-A2dp_Sink */
		_set_policy(rtwdev, BTC_CXP_FIX_TD2080, BTC_ACT_BT_A2DPSINK);
		break;
	}
}

static void _action_bt_pan(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;

	_set_ant(rtwdev, NM_EXEC, BTC_PHY_ALL, BTC_ANT_W2G);

	switch (btc->cx.state_map) {
	case BTC_WBUSY_BNOSCAN: /* wl-busy + bt-PAN */
		_set_policy(rtwdev, BTC_CXP_PFIX_TD5050, BTC_ACT_BT_PAN);
		break;
	case BTC_WBUSY_BSCAN: /* wl-busy + bt-inq + bt-PAN */
		_set_policy(rtwdev, BTC_CXP_PFIX_TD3070, BTC_ACT_BT_PAN);
		break;
	case BTC_WSCAN_BNOSCAN: /* wl-scan + bt-PAN */
		_set_policy(rtwdev, BTC_CXP_FIX_TD3030, BTC_ACT_BT_PAN);
		break;
	case BTC_WSCAN_BSCAN: /* wl-scan + bt-inq + bt-PAN */
		_set_policy(rtwdev, BTC_CXP_FIX_TD3060, BTC_ACT_BT_PAN);
		break;
	case BTC_WLINKING: /* wl-connecting + bt-PAN */
		_set_policy(rtwdev, BTC_CXP_FIX_TD4020, BTC_ACT_BT_PAN);
		break;
	case BTC_WIDLE: /* wl-idle + bt-pan */
		_set_policy(rtwdev, BTC_CXP_PFIX_TD2080, BTC_ACT_BT_PAN);
		break;
	}
}

static void _action_bt_a2dp_hid(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_bt_link_info *bt_linfo = &btc->cx.bt.link_info;
	struct rtw89_btc_bt_a2dp_desc a2dp = bt_linfo->a2dp_desc;
	struct rtw89_btc_dm *dm = &btc->dm;

	_set_ant(rtwdev, NM_EXEC, BTC_PHY_ALL, BTC_ANT_W2G);

	switch (btc->cx.state_map) {
	case BTC_WBUSY_BNOSCAN: /* wl-busy + bt-A2DP+HID */
	case BTC_WIDLE:  /* wl-idle + bt-A2DP */
		if (a2dp.vendor_id == 0x4c || dm->leak_ap) {
			dm->slot_dur[CXST_W1] = 40;
			dm->slot_dur[CXST_B1] = 200;
			_set_policy(rtwdev,
				    BTC_CXP_PAUTO_TDW1B1, BTC_ACT_BT_A2DP_HID);
		} else {
			_set_policy(rtwdev,
				    BTC_CXP_PAUTO_TD50200, BTC_ACT_BT_A2DP_HID);
		}
		break;
	case BTC_WBUSY_BSCAN: /* wl-busy + bt-inq + bt-A2DP+HID */
		_set_policy(rtwdev, BTC_CXP_PAUTO2_TD3050, BTC_ACT_BT_A2DP_HID);
		break;

	case BTC_WSCAN_BSCAN: /* wl-scan + bt-inq + bt-A2DP+HID */
		_set_policy(rtwdev, BTC_CXP_AUTO2_TD3050, BTC_ACT_BT_A2DP_HID);
		break;
	case BTC_WSCAN_BNOSCAN: /* wl-scan + bt-A2DP+HID */
	case BTC_WLINKING: /* wl-connecting + bt-A2DP+HID */
		if (a2dp.vendor_id == 0x4c || dm->leak_ap) {
			dm->slot_dur[CXST_W1] = 40;
			dm->slot_dur[CXST_B1] = 200;
			_set_policy(rtwdev, BTC_CXP_AUTO_TDW1B1,
				    BTC_ACT_BT_A2DP_HID);
		} else {
			_set_policy(rtwdev, BTC_CXP_AUTO_TD50200,
				    BTC_ACT_BT_A2DP_HID);
		}
		break;
	}
}

static void _action_bt_a2dp_pan(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;

	_set_ant(rtwdev, NM_EXEC, BTC_PHY_ALL, BTC_ANT_W2G);

	switch (btc->cx.state_map) {
	case BTC_WBUSY_BNOSCAN: /* wl-busy + bt-A2DP+PAN */
		_set_policy(rtwdev, BTC_CXP_PAUTO2_TD3070, BTC_ACT_BT_A2DP_PAN);
		break;
	case BTC_WBUSY_BSCAN: /* wl-busy + bt-inq + bt-A2DP+PAN */
		_set_policy(rtwdev, BTC_CXP_PAUTO2_TD3070, BTC_ACT_BT_A2DP_PAN);
		break;
	case BTC_WSCAN_BNOSCAN: /* wl-scan + bt-A2DP+PAN */
		_set_policy(rtwdev, BTC_CXP_AUTO2_TD5050, BTC_ACT_BT_A2DP_PAN);
		break;
	case BTC_WSCAN_BSCAN: /* wl-scan + bt-inq + bt-A2DP+PAN */
		_set_policy(rtwdev, BTC_CXP_AUTO2_TD3070, BTC_ACT_BT_A2DP_PAN);
		break;
	case BTC_WLINKING: /* wl-connecting + bt-A2DP+PAN */
		_set_policy(rtwdev, BTC_CXP_AUTO2_TD3050, BTC_ACT_BT_A2DP_PAN);
		break;
	case BTC_WIDLE:  /* wl-idle + bt-A2DP+PAN */
		_set_policy(rtwdev, BTC_CXP_PAUTO2_TD2080, BTC_ACT_BT_A2DP_PAN);
		break;
	}
}

static void _action_bt_pan_hid(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;

	_set_ant(rtwdev, NM_EXEC, BTC_PHY_ALL, BTC_ANT_W2G);

	switch (btc->cx.state_map) {
	case BTC_WBUSY_BNOSCAN: /* wl-busy + bt-PAN+HID */
		_set_policy(rtwdev, BTC_CXP_PFIX_TD3030, BTC_ACT_BT_PAN_HID);
		break;
	case BTC_WBUSY_BSCAN: /* wl-busy + bt-inq + bt-PAN+HID */
		_set_policy(rtwdev, BTC_CXP_PFIX_TD3070, BTC_ACT_BT_PAN_HID);
		break;
	case BTC_WSCAN_BNOSCAN: /* wl-scan + bt-PAN+HID */
		_set_policy(rtwdev, BTC_CXP_FIX_TD3030, BTC_ACT_BT_PAN_HID);
		break;
	case BTC_WSCAN_BSCAN: /* wl-scan + bt-inq + bt-PAN+HID */
		_set_policy(rtwdev, BTC_CXP_FIX_TD3060, BTC_ACT_BT_PAN_HID);
		break;
	case BTC_WLINKING: /* wl-connecting + bt-PAN+HID */
		_set_policy(rtwdev, BTC_CXP_FIX_TD4010, BTC_ACT_BT_PAN_HID);
		break;
	case BTC_WIDLE: /* wl-idle + bt-PAN+HID */
		_set_policy(rtwdev, BTC_CXP_PFIX_TD2080, BTC_ACT_BT_PAN_HID);
		break;
	}
}

static void _action_bt_a2dp_pan_hid(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;

	_set_ant(rtwdev, NM_EXEC, BTC_PHY_ALL, BTC_ANT_W2G);

	switch (btc->cx.state_map) {
	case BTC_WBUSY_BNOSCAN: /* wl-busy + bt-A2DP+PAN+HID */
		_set_policy(rtwdev, BTC_CXP_PAUTO2_TD3070,
			    BTC_ACT_BT_A2DP_PAN_HID);
		break;
	case BTC_WBUSY_BSCAN: /* wl-busy + bt-inq + bt-A2DP+PAN+HID */
		_set_policy(rtwdev, BTC_CXP_PAUTO2_TD3070,
			    BTC_ACT_BT_A2DP_PAN_HID);
		break;
	case BTC_WSCAN_BSCAN: /* wl-scan + bt-inq + bt-A2DP+PAN+HID */
		_set_policy(rtwdev, BTC_CXP_AUTO2_TD3070,
			    BTC_ACT_BT_A2DP_PAN_HID);
		break;
	case BTC_WSCAN_BNOSCAN: /* wl-scan + bt-A2DP+PAN+HID */
	case BTC_WLINKING: /* wl-connecting + bt-A2DP+PAN+HID */
		_set_policy(rtwdev, BTC_CXP_AUTO2_TD3050,
			    BTC_ACT_BT_A2DP_PAN_HID);
		break;
	case BTC_WIDLE:  /* wl-idle + bt-A2DP+PAN+HID */
		_set_policy(rtwdev, BTC_CXP_PAUTO2_TD2080,
			    BTC_ACT_BT_A2DP_PAN_HID);
		break;
	}
}

static void _action_wl_5g(struct rtw89_dev *rtwdev)
{
	_set_ant(rtwdev, NM_EXEC, BTC_PHY_ALL, BTC_ANT_W5G);
	_set_policy(rtwdev, BTC_CXP_OFF_EQ0, BTC_ACT_WL_5G);
}

static void _action_wl_other(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;

	_set_ant(rtwdev, NM_EXEC, BTC_PHY_ALL, BTC_ANT_W2G);

	if (btc->mdinfo.ant.type == BTC_ANT_SHARED)
		_set_policy(rtwdev, BTC_CXP_OFFB_BWB0, BTC_ACT_WL_OTHER);
	else
		_set_policy(rtwdev, BTC_CXP_OFF_EQ0, BTC_ACT_WL_OTHER);
}

static void _action_wl_nc(struct rtw89_dev *rtwdev)
{
	_set_ant(rtwdev, NM_EXEC, BTC_PHY_ALL, BTC_ANT_W2G);
	_set_policy(rtwdev, BTC_CXP_OFF_BT, BTC_ACT_WL_NC);
}

static void _action_wl_rfk(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_wl_rfk_info rfk = btc->cx.wl.rfk_info;

	if (rfk.state != BTC_WRFK_START)
		return;

	rtw89_debug(rtwdev, RTW89_DBG_BTC, "[BTC], %s(): band = %d\n",
		    __func__, rfk.band);

	_set_ant(rtwdev, FC_EXEC, BTC_PHY_ALL, BTC_ANT_WRFK);
	_set_policy(rtwdev, BTC_CXP_OFF_WL, BTC_ACT_WL_RFK);
}

static void _set_btg_ctrl(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_wl_info *wl = &btc->cx.wl;
	struct rtw89_btc_wl_role_info *wl_rinfo = &wl->role_info;
	struct rtw89_btc_wl_dbcc_info *wl_dinfo = &wl->dbcc_info;
	bool is_btg = false;

	if (btc->ctrl.manual)
		return;

	/* notify halbb ignore GNT_BT or not for WL BB Rx-AGC control */
	if (wl_rinfo->link_mode == BTC_WLINK_5G) /* always 0 if 5G */
		is_btg = false;
	else if (wl_rinfo->link_mode == BTC_WLINK_25G_DBCC &&
		 wl_dinfo->real_band[RTW89_PHY_1] != RTW89_BAND_2G)
		is_btg = false;
	else
		is_btg = true;

	if (btc->dm.run_reason != BTC_RSN_NTFY_INIT &&
	    is_btg == btc->dm.wl_btg_rx)
		return;

	btc->dm.wl_btg_rx = is_btg;

	if (wl_rinfo->link_mode == BTC_WLINK_25G_MCC)
		return;

	rtw89_ctrl_btg(rtwdev, is_btg);
}

struct rtw89_txtime_data {
	struct rtw89_dev *rtwdev;
	int type;
	u32 tx_time;
	u8 tx_retry;
	u16 enable;
	bool reenable;
};

static void rtw89_tx_time_iter(void *data, struct ieee80211_sta *sta)
{
	struct rtw89_sta *rtwsta = (struct rtw89_sta *)sta->drv_priv;
	struct rtw89_txtime_data *iter_data =
				(struct rtw89_txtime_data *)data;
	struct rtw89_dev *rtwdev = iter_data->rtwdev;
	struct rtw89_vif *rtwvif = rtwsta->rtwvif;
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_cx *cx = &btc->cx;
	struct rtw89_btc_wl_info *wl = &cx->wl;
	struct rtw89_btc_wl_link_info *plink = NULL;
	u8 port = rtwvif->port;
	u32 tx_time = iter_data->tx_time;
	u8 tx_retry = iter_data->tx_retry;
	u16 enable = iter_data->enable;
	bool reenable = iter_data->reenable;

	plink = &wl->link_info[port];

	rtw89_debug(rtwdev, RTW89_DBG_BTC,
		    "[BTC], %s(): port = %d\n", __func__, port);

	if (!plink->connected) {
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s(): connected = %d\n",
			    __func__, plink->connected);
		return;
	}

	/* backup the original tx time before tx-limit on */
	if (reenable) {
		rtw89_mac_get_tx_time(rtwdev, rtwsta, &plink->tx_time);
		rtw89_mac_get_tx_retry_limit(rtwdev, rtwsta, &plink->tx_retry);
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s(): reenable, tx_time=%d tx_retry= %d\n",
			    __func__, plink->tx_time, plink->tx_retry);
	}

	/* restore the original tx time if no tx-limit */
	if (!enable) {
		rtw89_mac_set_tx_time(rtwdev, rtwsta, true, plink->tx_time);
		rtw89_mac_set_tx_retry_limit(rtwdev, rtwsta, true,
					     plink->tx_retry);
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s(): restore, tx_time=%d tx_retry= %d\n",
			    __func__, plink->tx_time, plink->tx_retry);

	} else {
		rtw89_mac_set_tx_time(rtwdev, rtwsta, false, tx_time);
		rtw89_mac_set_tx_retry_limit(rtwdev, rtwsta, false, tx_retry);
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s(): set, tx_time=%d tx_retry= %d\n",
			    __func__, tx_time, tx_retry);
	}
}

static void _set_wl_tx_limit(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_cx *cx = &btc->cx;
	struct rtw89_btc_dm *dm = &btc->dm;
	struct rtw89_btc_wl_info *wl = &cx->wl;
	struct rtw89_btc_bt_info *bt = &cx->bt;
	struct rtw89_btc_bt_link_info *b = &bt->link_info;
	struct rtw89_btc_bt_hfp_desc *hfp = &b->hfp_desc;
	struct rtw89_btc_bt_hid_desc *hid = &b->hid_desc;
	struct rtw89_btc_wl_role_info *wl_rinfo = &wl->role_info;
	struct rtw89_txtime_data data = {.rtwdev = rtwdev};
	u8 mode = wl_rinfo->link_mode;
	u8 tx_retry = 0;
	u32 tx_time = 0;
	u16 enable = 0;
	bool reenable = false;

	if (btc->ctrl.manual)
		return;

	if (btc->dm.freerun || btc->ctrl.igno_bt || b->profile_cnt.now == 0 ||
	    mode == BTC_WLINK_5G || mode == BTC_WLINK_NOLINK) {
		enable = 0;
		tx_time = BTC_MAX_TX_TIME_DEF;
		tx_retry = BTC_MAX_TX_RETRY_DEF;
	} else if ((hfp->exist && hid->exist) || hid->pair_cnt > 1) {
		enable = 1;
		tx_time = BTC_MAX_TX_TIME_L2;
		tx_retry = BTC_MAX_TX_RETRY_L1;
	} else if (hfp->exist || hid->exist) {
		enable = 1;
		tx_time = BTC_MAX_TX_TIME_L3;
		tx_retry = BTC_MAX_TX_RETRY_L1;
	} else {
		enable = 0;
		tx_time = BTC_MAX_TX_TIME_DEF;
		tx_retry = BTC_MAX_TX_RETRY_DEF;
	}

	if (dm->wl_tx_limit.enable == enable &&
	    dm->wl_tx_limit.tx_time == tx_time &&
	    dm->wl_tx_limit.tx_retry == tx_retry)
		return;

	if (!dm->wl_tx_limit.enable && enable)
		reenable = true;

	dm->wl_tx_limit.enable = enable;
	dm->wl_tx_limit.tx_time = tx_time;
	dm->wl_tx_limit.tx_retry = tx_retry;

	data.enable = enable;
	data.tx_time = tx_time;
	data.tx_retry = tx_retry;
	data.reenable = reenable;

	ieee80211_iterate_stations_atomic(rtwdev->hw,
					  rtw89_tx_time_iter,
					  &data);
}

static void _set_bt_rx_agc(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_wl_info *wl = &btc->cx.wl;
	struct rtw89_btc_wl_role_info *wl_rinfo = &wl->role_info;
	struct rtw89_btc_bt_info *bt = &btc->cx.bt;
	bool bt_hi_lna_rx = false;

	if (wl_rinfo->link_mode != BTC_WLINK_NOLINK && btc->dm.wl_btg_rx)
		bt_hi_lna_rx = true;

	if (bt_hi_lna_rx == bt->hi_lna_rx)
		return;

	_write_scbd(rtwdev, BTC_WSCB_BT_HILNA, bt_hi_lna_rx);
}

/* TODO add these functions */
static void _action_common(struct rtw89_dev *rtwdev)
{
	_set_btg_ctrl(rtwdev);
	_set_wl_tx_limit(rtwdev);
	_set_bt_afh_info(rtwdev);
	_set_bt_rx_agc(rtwdev);
	_set_rf_trx_para(rtwdev);
}

static void _action_by_bt(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_bt_info *bt = &btc->cx.bt;
	struct rtw89_btc_bt_link_info *bt_linfo = &bt->link_info;
	struct rtw89_btc_bt_hid_desc hid = bt_linfo->hid_desc;
	struct rtw89_btc_bt_a2dp_desc a2dp = bt_linfo->a2dp_desc;
	struct rtw89_btc_bt_pan_desc pan = bt_linfo->pan_desc;
	u8 profile_map = 0;

	if (bt_linfo->hfp_desc.exist)
		profile_map |= BTC_BT_HFP;

	if (bt_linfo->hid_desc.exist)
		profile_map |= BTC_BT_HID;

	if (bt_linfo->a2dp_desc.exist)
		profile_map |= BTC_BT_A2DP;

	if (bt_linfo->pan_desc.exist)
		profile_map |= BTC_BT_PAN;

	switch (profile_map) {
	case BTC_BT_NOPROFILE:
		if (_check_freerun(rtwdev))
			_action_freerun(rtwdev);
		else if (a2dp.active || pan.active)
			_action_bt_pan(rtwdev);
		else
			_action_bt_idle(rtwdev);
		break;
	case BTC_BT_HFP:
		if (_check_freerun(rtwdev))
			_action_freerun(rtwdev);
		else
			_action_bt_hfp(rtwdev);
		break;
	case BTC_BT_HFP | BTC_BT_HID:
	case BTC_BT_HID:
		if (_check_freerun(rtwdev))
			_action_freerun(rtwdev);
		else
			_action_bt_hid(rtwdev);
		break;
	case BTC_BT_A2DP:
		if (_check_freerun(rtwdev))
			_action_freerun(rtwdev);
		else if (a2dp.sink)
			_action_bt_a2dpsink(rtwdev);
		else if (bt_linfo->multi_link.now && !hid.pair_cnt)
			_action_bt_a2dp_pan(rtwdev);
		else
			_action_bt_a2dp(rtwdev);
		break;
	case BTC_BT_PAN:
		_action_bt_pan(rtwdev);
		break;
	case BTC_BT_A2DP | BTC_BT_HFP:
	case BTC_BT_A2DP | BTC_BT_HID:
	case BTC_BT_A2DP | BTC_BT_HFP | BTC_BT_HID:
		if (_check_freerun(rtwdev))
			_action_freerun(rtwdev);
		else
			_action_bt_a2dp_hid(rtwdev);
		break;
	case BTC_BT_A2DP | BTC_BT_PAN:
		_action_bt_a2dp_pan(rtwdev);
		break;
	case BTC_BT_PAN | BTC_BT_HFP:
	case BTC_BT_PAN | BTC_BT_HID:
	case BTC_BT_PAN | BTC_BT_HFP | BTC_BT_HID:
		_action_bt_pan_hid(rtwdev);
		break;
	case BTC_BT_A2DP | BTC_BT_PAN | BTC_BT_HID:
	case BTC_BT_A2DP | BTC_BT_PAN | BTC_BT_HFP:
	default:
		_action_bt_a2dp_pan_hid(rtwdev);
		break;
	}
}

static void _action_wl_2g_sta(struct rtw89_dev *rtwdev)
{
	_action_by_bt(rtwdev);
}

static void _action_wl_scan(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_wl_info *wl = &btc->cx.wl;
	struct rtw89_btc_wl_dbcc_info *wl_dinfo = &wl->dbcc_info;

	if (rtwdev->dbcc_en) {
		if (wl_dinfo->real_band[RTW89_PHY_0] != RTW89_BAND_2G &&
		    wl_dinfo->real_band[RTW89_PHY_1] != RTW89_BAND_2G)
			_action_wl_5g(rtwdev);
		else
			_action_by_bt(rtwdev);
	} else {
		if (wl->scan_info.band[RTW89_PHY_0] != RTW89_BAND_2G)
			_action_wl_5g(rtwdev);
		else
			_action_by_bt(rtwdev);
	}
}

static void _action_wl_25g_mcc(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;

	_set_ant(rtwdev, NM_EXEC, BTC_PHY_ALL, BTC_ANT_W25G);

	if (btc->mdinfo.ant.type == BTC_ANT_SHARED) {
		if (btc->cx.bt.link_info.profile_cnt.now == 0)
			_set_policy(rtwdev, BTC_CXP_OFFE_DEF2,
				    BTC_ACT_WL_25G_MCC);
		else
			_set_policy(rtwdev, BTC_CXP_OFFE_DEF,
				    BTC_ACT_WL_25G_MCC);
	} else { /* dedicated-antenna */
		_set_policy(rtwdev, BTC_CXP_OFF_EQ0, BTC_ACT_WL_25G_MCC);
	}
}

static void _action_wl_2g_mcc(struct rtw89_dev *rtwdev)
{	struct rtw89_btc *btc = &rtwdev->btc;

	_set_ant(rtwdev, NM_EXEC, BTC_PHY_ALL, BTC_ANT_W2G);

	if (btc->mdinfo.ant.type == BTC_ANT_SHARED) { /* shared-antenna */
		if (btc->cx.bt.link_info.profile_cnt.now == 0)
			_set_policy(rtwdev, BTC_CXP_OFFE_DEF2,
				    BTC_ACT_WL_2G_MCC);
		else
			_set_policy(rtwdev, BTC_CXP_OFFE_DEF,
				    BTC_ACT_WL_2G_MCC);
	} else { /* dedicated-antenna */
		_set_policy(rtwdev, BTC_CXP_OFF_EQ0, BTC_ACT_WL_2G_MCC);
	}
}

static void _action_wl_2g_scc(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;

	_set_ant(rtwdev, NM_EXEC, BTC_PHY_ALL, BTC_ANT_W2G);

	if (btc->mdinfo.ant.type == BTC_ANT_SHARED) { /* shared-antenna */
		if (btc->cx.bt.link_info.profile_cnt.now == 0)
			_set_policy(rtwdev,
				    BTC_CXP_OFFE_DEF2, BTC_ACT_WL_2G_SCC);
		else
			_set_policy(rtwdev,
				    BTC_CXP_OFFE_DEF, BTC_ACT_WL_2G_SCC);
	} else { /* dedicated-antenna */
		_set_policy(rtwdev, BTC_CXP_OFF_EQ0, BTC_ACT_WL_2G_SCC);
	}
}

static void _action_wl_2g_ap(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;

	_set_ant(rtwdev, NM_EXEC, BTC_PHY_ALL, BTC_ANT_W2G);

	if (btc->mdinfo.ant.type == BTC_ANT_SHARED) {
		if (btc->cx.bt.link_info.profile_cnt.now == 0)
			_set_policy(rtwdev, BTC_CXP_OFFE_DEF2,
				    BTC_ACT_WL_2G_AP);
		else
			_set_policy(rtwdev, BTC_CXP_OFFE_DEF, BTC_ACT_WL_2G_AP);
	} else {/* dedicated-antenna */
		_set_policy(rtwdev, BTC_CXP_OFF_EQ0, BTC_ACT_WL_2G_AP);
	}
}

static void _action_wl_2g_go(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;

	_set_ant(rtwdev, NM_EXEC, BTC_PHY_ALL, BTC_ANT_W2G);

	if (btc->mdinfo.ant.type == BTC_ANT_SHARED) { /* shared-antenna */
		if (btc->cx.bt.link_info.profile_cnt.now == 0)
			_set_policy(rtwdev,
				    BTC_CXP_OFFE_DEF2, BTC_ACT_WL_2G_GO);
		else
			_set_policy(rtwdev,
				    BTC_CXP_OFFE_DEF, BTC_ACT_WL_2G_GO);
	} else { /* dedicated-antenna */
		_set_policy(rtwdev, BTC_CXP_OFF_EQ0, BTC_ACT_WL_2G_GO);
	}
}

static void _action_wl_2g_gc(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;

	_set_ant(rtwdev, NM_EXEC, BTC_PHY_ALL, BTC_ANT_W2G);

	if (btc->mdinfo.ant.type == BTC_ANT_SHARED) { /* shared-antenna */
		_action_by_bt(rtwdev);
	} else {/* dedicated-antenna */
		_set_policy(rtwdev, BTC_CXP_OFF_EQ0, BTC_ACT_WL_2G_GC);
	}
}

static void _action_wl_2g_nan(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;

	_set_ant(rtwdev, NM_EXEC, BTC_PHY_ALL, BTC_ANT_W2G);

	if (btc->mdinfo.ant.type == BTC_ANT_SHARED) { /* shared-antenna */
		if (btc->cx.bt.link_info.profile_cnt.now == 0)
			_set_policy(rtwdev,
				    BTC_CXP_OFFE_DEF2, BTC_ACT_WL_2G_NAN);
		else
			_set_policy(rtwdev,
				    BTC_CXP_OFFE_DEF, BTC_ACT_WL_2G_NAN);
	} else { /* dedicated-antenna */
		_set_policy(rtwdev, BTC_CXP_OFF_EQ0, BTC_ACT_WL_2G_NAN);
	}
}

static u32 _read_scbd(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_btc *btc = &rtwdev->btc;
	u32 scbd_val = 0;

	if (!chip->scbd)
		return 0;

	scbd_val = rtw89_mac_get_sb(rtwdev);
	rtw89_debug(rtwdev, RTW89_DBG_BTC, "[BTC], read scbd: 0x%08x\n",
		    scbd_val);

	btc->cx.cnt_bt[BTC_BCNT_SCBDREAD]++;
	return scbd_val;
}

static void _write_scbd(struct rtw89_dev *rtwdev, u32 val, bool state)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_wl_info *wl = &btc->cx.wl;
	u32 scbd_val = 0;

	if (!chip->scbd)
		return;

	scbd_val = state ? wl->scbd | val : wl->scbd & ~val;

	if (scbd_val == wl->scbd)
		return;
	rtw89_mac_cfg_sb(rtwdev, scbd_val);
	rtw89_debug(rtwdev, RTW89_DBG_BTC, "[BTC], write scbd: 0x%08x\n",
		    scbd_val);
	wl->scbd = scbd_val;

	btc->cx.cnt_wl[BTC_WCNT_SCBDUPDATE]++;
}

static u8
_update_rssi_state(struct rtw89_dev *rtwdev, u8 pre_state, u8 rssi, u8 thresh)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	u8 next_state, tol = chip->rssi_tol;

	if (pre_state == BTC_RSSI_ST_LOW ||
	    pre_state == BTC_RSSI_ST_STAY_LOW) {
		if (rssi >= (thresh + tol))
			next_state = BTC_RSSI_ST_HIGH;
		else
			next_state = BTC_RSSI_ST_STAY_LOW;
	} else {
		if (rssi < thresh)
			next_state = BTC_RSSI_ST_LOW;
		else
			next_state = BTC_RSSI_ST_STAY_HIGH;
	}

	return next_state;
}

static
void _update_dbcc_band(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx)
{
	struct rtw89_btc *btc = &rtwdev->btc;

	btc->cx.wl.dbcc_info.real_band[phy_idx] =
		btc->cx.wl.scan_info.phy_map & BIT(phy_idx) ?
		btc->cx.wl.dbcc_info.scan_band[phy_idx] :
		btc->cx.wl.dbcc_info.op_band[phy_idx];
}

static void _update_wl_info(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_wl_info *wl = &btc->cx.wl;
	struct rtw89_btc_wl_link_info *wl_linfo = wl->link_info;
	struct rtw89_btc_wl_role_info *wl_rinfo = &wl->role_info;
	struct rtw89_btc_wl_dbcc_info *wl_dinfo = &wl->dbcc_info;
	u8 i, cnt_connect = 0, cnt_connecting = 0, cnt_active = 0;
	u8 cnt_2g = 0, cnt_5g = 0, phy;
	u32 wl_2g_ch[2] = {0}, wl_5g_ch[2] = {0};
	bool b2g = false, b5g = false, client_joined = false;

	memset(wl_rinfo, 0, sizeof(*wl_rinfo));

	for (i = 0; i < RTW89_MAX_HW_PORT_NUM; i++) {
		/* check if role active? */
		if (!wl_linfo[i].active)
			continue;

		cnt_active++;
		wl_rinfo->active_role[cnt_active - 1].role = wl_linfo[i].role;
		wl_rinfo->active_role[cnt_active - 1].pid = wl_linfo[i].pid;
		wl_rinfo->active_role[cnt_active - 1].phy = wl_linfo[i].phy;
		wl_rinfo->active_role[cnt_active - 1].band = wl_linfo[i].band;
		wl_rinfo->active_role[cnt_active - 1].noa = (u8)wl_linfo[i].noa;
		wl_rinfo->active_role[cnt_active - 1].connected = 0;

		wl->port_id[wl_linfo[i].role] = wl_linfo[i].pid;

		phy = wl_linfo[i].phy;

		/* check dbcc role */
		if (rtwdev->dbcc_en && phy < RTW89_PHY_MAX) {
			wl_dinfo->role[phy] = wl_linfo[i].role;
			wl_dinfo->op_band[phy] = wl_linfo[i].band;
			_update_dbcc_band(rtwdev, phy);
			_fw_set_drv_info(rtwdev, CXDRVINFO_DBCC);
		}

		if (wl_linfo[i].connected == MLME_NO_LINK) {
			continue;
		} else if (wl_linfo[i].connected == MLME_LINKING) {
			cnt_connecting++;
		} else {
			cnt_connect++;
			if ((wl_linfo[i].role == RTW89_WIFI_ROLE_P2P_GO ||
			     wl_linfo[i].role == RTW89_WIFI_ROLE_AP) &&
			     wl_linfo[i].client_cnt > 1)
				client_joined = true;
		}

		wl_rinfo->role_map.val |= BIT(wl_linfo[i].role);
		wl_rinfo->active_role[cnt_active - 1].ch = wl_linfo[i].ch;
		wl_rinfo->active_role[cnt_active - 1].bw = wl_linfo[i].bw;
		wl_rinfo->active_role[cnt_active - 1].connected = 1;

		/* only care 2 roles + BT coex */
		if (wl_linfo[i].band != RTW89_BAND_2G) {
			if (cnt_5g <= ARRAY_SIZE(wl_5g_ch) - 1)
				wl_5g_ch[cnt_5g] = wl_linfo[i].ch;
			cnt_5g++;
			b5g = true;
		} else {
			if (cnt_2g <= ARRAY_SIZE(wl_2g_ch) - 1)
				wl_2g_ch[cnt_2g] = wl_linfo[i].ch;
			cnt_2g++;
			b2g = true;
		}
	}

	wl_rinfo->connect_cnt = cnt_connect;

	/* Be careful to change the following sequence!! */
	if (cnt_connect == 0) {
		wl_rinfo->link_mode = BTC_WLINK_NOLINK;
		wl_rinfo->role_map.role.none = 1;
	} else if (!b2g && b5g) {
		wl_rinfo->link_mode = BTC_WLINK_5G;
	} else if (wl_rinfo->role_map.role.nan) {
		wl_rinfo->link_mode = BTC_WLINK_2G_NAN;
	} else if (cnt_connect > BTC_TDMA_WLROLE_MAX) {
		wl_rinfo->link_mode = BTC_WLINK_OTHER;
	} else  if (b2g && b5g && cnt_connect == 2) {
		if (rtwdev->dbcc_en) {
			switch (wl_dinfo->role[RTW89_PHY_0]) {
			case RTW89_WIFI_ROLE_STATION:
				wl_rinfo->link_mode = BTC_WLINK_2G_STA;
				break;
			case RTW89_WIFI_ROLE_P2P_GO:
				wl_rinfo->link_mode = BTC_WLINK_2G_GO;
				break;
			case RTW89_WIFI_ROLE_P2P_CLIENT:
				wl_rinfo->link_mode = BTC_WLINK_2G_GC;
				break;
			case RTW89_WIFI_ROLE_AP:
				wl_rinfo->link_mode = BTC_WLINK_2G_AP;
				break;
			default:
				wl_rinfo->link_mode = BTC_WLINK_OTHER;
				break;
			}
		} else {
			wl_rinfo->link_mode = BTC_WLINK_25G_MCC;
		}
	} else if (!b5g && cnt_connect == 2) {
		if (wl_rinfo->role_map.role.station &&
		    (wl_rinfo->role_map.role.p2p_go ||
		    wl_rinfo->role_map.role.p2p_gc ||
		    wl_rinfo->role_map.role.ap)) {
			if (wl_2g_ch[0] == wl_2g_ch[1])
				wl_rinfo->link_mode = BTC_WLINK_2G_SCC;
			else
				wl_rinfo->link_mode = BTC_WLINK_2G_MCC;
		} else {
			wl_rinfo->link_mode = BTC_WLINK_2G_MCC;
		}
	} else if (!b5g && cnt_connect == 1) {
		if (wl_rinfo->role_map.role.station)
			wl_rinfo->link_mode = BTC_WLINK_2G_STA;
		else if (wl_rinfo->role_map.role.ap)
			wl_rinfo->link_mode = BTC_WLINK_2G_AP;
		else if (wl_rinfo->role_map.role.p2p_go)
			wl_rinfo->link_mode = BTC_WLINK_2G_GO;
		else if (wl_rinfo->role_map.role.p2p_gc)
			wl_rinfo->link_mode = BTC_WLINK_2G_GC;
		else
			wl_rinfo->link_mode = BTC_WLINK_OTHER;
	}

	/* if no client_joined, don't care P2P-GO/AP role */
	if (wl_rinfo->role_map.role.p2p_go || wl_rinfo->role_map.role.ap) {
		if (!client_joined) {
			if (wl_rinfo->link_mode == BTC_WLINK_2G_SCC ||
			    wl_rinfo->link_mode == BTC_WLINK_2G_MCC) {
				wl_rinfo->link_mode = BTC_WLINK_2G_STA;
				wl_rinfo->connect_cnt = 1;
			} else if (wl_rinfo->link_mode == BTC_WLINK_2G_GO ||
				 wl_rinfo->link_mode == BTC_WLINK_2G_AP) {
				wl_rinfo->link_mode = BTC_WLINK_NOLINK;
				wl_rinfo->connect_cnt = 0;
			}
		}
	}

	rtw89_debug(rtwdev, RTW89_DBG_BTC,
		    "[BTC], cnt_connect = %d, link_mode = %d\n",
		    cnt_connect, wl_rinfo->link_mode);

	_fw_set_drv_info(rtwdev, CXDRVINFO_ROLE);
}

#define BTC_CHK_HANG_MAX 3
#define BTC_SCB_INV_VALUE GENMASK(31, 0)

void rtw89_coex_act1_work(struct work_struct *work)
{
	struct rtw89_dev *rtwdev = container_of(work, struct rtw89_dev,
						coex_act1_work.work);
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_dm *dm = &rtwdev->btc.dm;
	struct rtw89_btc_cx *cx = &btc->cx;
	struct rtw89_btc_wl_info *wl = &cx->wl;

	mutex_lock(&rtwdev->mutex);
	rtw89_debug(rtwdev, RTW89_DBG_BTC, "[BTC], %s(): enter\n", __func__);
	dm->cnt_notify[BTC_NCNT_TIMER]++;
	if (wl->status.map._4way)
		wl->status.map._4way = false;
	if (wl->status.map.connecting)
		wl->status.map.connecting = false;

	_run_coex(rtwdev, BTC_RSN_ACT1_WORK);
	mutex_unlock(&rtwdev->mutex);
}

void rtw89_coex_bt_devinfo_work(struct work_struct *work)
{
	struct rtw89_dev *rtwdev = container_of(work, struct rtw89_dev,
						coex_bt_devinfo_work.work);
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_dm *dm = &rtwdev->btc.dm;
	struct rtw89_btc_bt_a2dp_desc *a2dp = &btc->cx.bt.link_info.a2dp_desc;

	mutex_lock(&rtwdev->mutex);
	rtw89_debug(rtwdev, RTW89_DBG_BTC, "[BTC], %s(): enter\n", __func__);
	dm->cnt_notify[BTC_NCNT_TIMER]++;
	a2dp->play_latency = 0;
	_run_coex(rtwdev, BTC_RSN_BT_DEVINFO_WORK);
	mutex_unlock(&rtwdev->mutex);
}

void rtw89_coex_rfk_chk_work(struct work_struct *work)
{
	struct rtw89_dev *rtwdev = container_of(work, struct rtw89_dev,
						coex_rfk_chk_work.work);
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_dm *dm = &rtwdev->btc.dm;
	struct rtw89_btc_cx *cx = &btc->cx;
	struct rtw89_btc_wl_info *wl = &cx->wl;

	mutex_lock(&rtwdev->mutex);
	rtw89_debug(rtwdev, RTW89_DBG_BTC, "[BTC], %s(): enter\n", __func__);
	dm->cnt_notify[BTC_NCNT_TIMER]++;
	if (wl->rfk_info.state != BTC_WRFK_STOP) {
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s(): RFK timeout\n", __func__);
		cx->cnt_wl[BTC_WCNT_RFK_TIMEOUT]++;
		dm->error.map.wl_rfk_timeout = true;
		wl->rfk_info.state = BTC_WRFK_STOP;
		_write_scbd(rtwdev, BTC_WSCB_WLRFK, false);
		_run_coex(rtwdev, BTC_RSN_RFK_CHK_WORK);
	}
	mutex_unlock(&rtwdev->mutex);
}

static void _update_bt_scbd(struct rtw89_dev *rtwdev, bool only_update)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_cx *cx = &btc->cx;
	struct rtw89_btc_bt_info *bt = &btc->cx.bt;
	u32 val;
	bool status_change = false;

	if (!chip->scbd)
		return;

	rtw89_debug(rtwdev, RTW89_DBG_BTC, "[BTC], %s\n", __func__);

	val = _read_scbd(rtwdev);
	if (val == BTC_SCB_INV_VALUE) {
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s(): return by invalid scbd value\n",
			    __func__);
		return;
	}

	if (!(val & BTC_BSCB_ON) ||
	    btc->dm.cnt_dm[BTC_DCNT_BTCNT_FREEZE] >= BTC_CHK_HANG_MAX)
		bt->enable.now = 0;
	else
		bt->enable.now = 1;

	if (bt->enable.now != bt->enable.last)
		status_change = true;

	/* reset bt info if bt re-enable */
	if (bt->enable.now && !bt->enable.last) {
		_reset_btc_var(rtwdev, BTC_RESET_BTINFO);
		cx->cnt_bt[BTC_BCNT_REENABLE]++;
		bt->enable.now = 1;
	}

	bt->enable.last = bt->enable.now;
	bt->scbd = val;
	bt->mbx_avl = !!(val & BTC_BSCB_ACT);

	if (bt->whql_test != !!(val & BTC_BSCB_WHQL))
		status_change = true;

	bt->whql_test = !!(val & BTC_BSCB_WHQL);
	bt->btg_type = val & BTC_BSCB_BT_S1 ? BTC_BT_BTG : BTC_BT_ALONE;
	bt->link_info.a2dp_desc.active = !!(val & BTC_BSCB_A2DP_ACT);

	/* if rfk run 1->0 */
	if (bt->rfk_info.map.run && !(val & BTC_BSCB_RFK_RUN))
		status_change = true;

	bt->rfk_info.map.run  = !!(val & BTC_BSCB_RFK_RUN);
	bt->rfk_info.map.req = !!(val & BTC_BSCB_RFK_REQ);
	bt->hi_lna_rx = !!(val & BTC_BSCB_BT_HILNA);
	bt->link_info.status.map.connect = !!(val & BTC_BSCB_BT_CONNECT);
	bt->run_patch_code = !!(val & BTC_BSCB_PATCH_CODE);

	if (!only_update && status_change)
		_run_coex(rtwdev, BTC_RSN_UPDATE_BT_SCBD);
}

static bool _chk_wl_rfk_request(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_cx *cx = &btc->cx;
	struct rtw89_btc_bt_info *bt = &cx->bt;

	_update_bt_scbd(rtwdev, true);

	cx->cnt_wl[BTC_WCNT_RFK_REQ]++;

	if ((bt->rfk_info.map.run || bt->rfk_info.map.req) &&
	    !bt->rfk_info.map.timeout) {
		cx->cnt_wl[BTC_WCNT_RFK_REJECT]++;
	} else {
		cx->cnt_wl[BTC_WCNT_RFK_GO]++;
		return true;
	}
	return false;
}

static
void _run_coex(struct rtw89_dev *rtwdev, enum btc_reason_and_action reason)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_dm *dm = &rtwdev->btc.dm;
	struct rtw89_btc_cx *cx = &btc->cx;
	struct rtw89_btc_wl_info *wl = &btc->cx.wl;
	struct rtw89_btc_wl_role_info *wl_rinfo = &wl->role_info;
	u8 mode = wl_rinfo->link_mode;

	lockdep_assert_held(&rtwdev->mutex);
	rtw89_debug(rtwdev, RTW89_DBG_BTC, "[BTC], %s(): reason=%d, mode=%d\n",
		    __func__, reason, mode);
	rtw89_debug(rtwdev, RTW89_DBG_BTC, "[BTC], %s(): wl_only=%d, bt_only=%d\n",
		    __func__, dm->wl_only, dm->bt_only);

	dm->run_reason = reason;
	_update_dm_step(rtwdev, reason);
	_update_btc_state_map(rtwdev);

	/* Be careful to change the following function sequence!! */
	if (btc->ctrl.manual) {
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s(): return for Manual CTRL!!\n",
			    __func__);
		return;
	}

	if (btc->ctrl.igno_bt &&
	    (reason == BTC_RSN_UPDATE_BT_INFO ||
	     reason == BTC_RSN_UPDATE_BT_SCBD)) {
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s(): return for Stop Coex DM!!\n",
			    __func__);
		return;
	}

	if (!wl->status.map.init_ok) {
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s(): return for WL init fail!!\n",
			    __func__);
		return;
	}

	if (wl->status.map.rf_off_pre == wl->status.map.rf_off &&
	    wl->status.map.lps_pre == wl->status.map.lps &&
	    (reason == BTC_RSN_NTFY_POWEROFF ||
	    reason == BTC_RSN_NTFY_RADIO_STATE)) {
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s(): return for WL rf off state no change!!\n",
			    __func__);
		return;
	}

	dm->cnt_dm[BTC_DCNT_RUN]++;

	if (btc->ctrl.always_freerun) {
		_action_freerun(rtwdev);
		btc->ctrl.igno_bt = true;
		goto exit;
	}

	if (dm->wl_only) {
		_action_wl_only(rtwdev);
		btc->ctrl.igno_bt = true;
		goto exit;
	}

	if (wl->status.map.rf_off || wl->status.map.lps || dm->bt_only) {
		_action_wl_off(rtwdev);
		btc->ctrl.igno_bt = true;
		goto exit;
	}

	btc->ctrl.igno_bt = false;
	dm->freerun = false;

	if (reason == BTC_RSN_NTFY_INIT) {
		_action_wl_init(rtwdev);
		goto exit;
	}

	if (!cx->bt.enable.now && !cx->other.type) {
		_action_bt_off(rtwdev);
		goto exit;
	}

	if (cx->bt.whql_test) {
		_action_bt_whql(rtwdev);
		goto exit;
	}

	if (wl->rfk_info.state != BTC_WRFK_STOP) {
		_action_wl_rfk(rtwdev);
		goto exit;
	}

	if (cx->state_map == BTC_WLINKING) {
		if (mode == BTC_WLINK_NOLINK || mode == BTC_WLINK_2G_STA ||
		    mode == BTC_WLINK_5G) {
			_action_wl_scan(rtwdev);
			goto exit;
		}
	}

	if (wl->status.map.scan) {
		_action_wl_scan(rtwdev);
		goto exit;
	}

	switch (mode) {
	case BTC_WLINK_NOLINK:
		_action_wl_nc(rtwdev);
		break;
	case BTC_WLINK_2G_STA:
		_action_wl_2g_sta(rtwdev);
		break;
	case BTC_WLINK_2G_AP:
		_action_wl_2g_ap(rtwdev);
		break;
	case BTC_WLINK_2G_GO:
		_action_wl_2g_go(rtwdev);
		break;
	case BTC_WLINK_2G_GC:
		_action_wl_2g_gc(rtwdev);
		break;
	case BTC_WLINK_2G_SCC:
		_action_wl_2g_scc(rtwdev);
		break;
	case BTC_WLINK_2G_MCC:
		_action_wl_2g_mcc(rtwdev);
		break;
	case BTC_WLINK_25G_MCC:
		_action_wl_25g_mcc(rtwdev);
		break;
	case BTC_WLINK_5G:
		_action_wl_5g(rtwdev);
		break;
	case BTC_WLINK_2G_NAN:
		_action_wl_2g_nan(rtwdev);
		break;
	default:
		_action_wl_other(rtwdev);
		break;
	}

exit:
	rtw89_debug(rtwdev, RTW89_DBG_BTC, "[BTC], %s(): exit\n", __func__);
	_action_common(rtwdev);
}

void rtw89_btc_ntfy_poweron(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;

	rtw89_debug(rtwdev, RTW89_DBG_BTC, "[BTC], %s(): !!\n", __func__);
	btc->dm.cnt_notify[BTC_NCNT_POWER_ON]++;
}

void rtw89_btc_ntfy_poweroff(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;

	rtw89_debug(rtwdev, RTW89_DBG_BTC, "[BTC], %s(): !!\n", __func__);
	btc->dm.cnt_notify[BTC_NCNT_POWER_OFF]++;

	btc->cx.wl.status.map.rf_off = 1;

	_write_scbd(rtwdev, BTC_WSCB_ALL, false);
	_run_coex(rtwdev, BTC_RSN_NTFY_POWEROFF);

	rtw89_btc_fw_en_rpt(rtwdev, RPT_EN_ALL, 0);

	btc->cx.wl.status.map.rf_off_pre = btc->cx.wl.status.map.rf_off;
}

static void _set_init_info(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_dm *dm = &btc->dm;
	struct rtw89_btc_wl_info *wl = &btc->cx.wl;

	dm->init_info.wl_only = (u8)dm->wl_only;
	dm->init_info.bt_only = (u8)dm->bt_only;
	dm->init_info.wl_init_ok = (u8)wl->status.map.init_ok;
	dm->init_info.dbcc_en = rtwdev->dbcc_en;
	dm->init_info.cx_other = btc->cx.other.type;
	dm->init_info.wl_guard_ch = chip->afh_guard_ch;
	dm->init_info.module = btc->mdinfo;
}

void rtw89_btc_ntfy_init(struct rtw89_dev *rtwdev, u8 mode)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_dm *dm = &rtwdev->btc.dm;
	struct rtw89_btc_wl_info *wl = &btc->cx.wl;
	const struct rtw89_chip_info *chip = rtwdev->chip;

	_reset_btc_var(rtwdev, BTC_RESET_ALL);
	btc->dm.run_reason = BTC_RSN_NONE;
	btc->dm.run_action = BTC_ACT_NONE;
	btc->ctrl.igno_bt = true;

	rtw89_debug(rtwdev, RTW89_DBG_BTC,
		    "[BTC], %s(): mode=%d\n", __func__, mode);

	dm->cnt_notify[BTC_NCNT_INIT_COEX]++;
	dm->wl_only = mode == BTC_MODE_WL ? 1 : 0;
	dm->bt_only = mode == BTC_MODE_BT ? 1 : 0;
	wl->status.map.rf_off = mode == BTC_MODE_WLOFF ? 1 : 0;

	chip->ops->btc_set_rfe(rtwdev);
	chip->ops->btc_init_cfg(rtwdev);

	if (!wl->status.map.init_ok) {
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s(): return for WL init fail!!\n",
			    __func__);
		dm->error.map.init = true;
		return;
	}

	_write_scbd(rtwdev,
		    BTC_WSCB_ACTIVE | BTC_WSCB_ON | BTC_WSCB_BTLOG, true);
	_update_bt_scbd(rtwdev, true);
	if (rtw89_mac_get_ctrl_path(rtwdev)) {
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s(): PTA owner warning!!\n",
			    __func__);
		dm->error.map.pta_owner = true;
	}

	_set_init_info(rtwdev);
	_set_wl_tx_power(rtwdev, RTW89_BTC_WL_DEF_TX_PWR);
	rtw89_btc_fw_set_slots(rtwdev, CXST_MAX, dm->slot);
	btc_fw_set_monreg(rtwdev);
	_fw_set_drv_info(rtwdev, CXDRVINFO_INIT);
	_fw_set_drv_info(rtwdev, CXDRVINFO_CTRL);

	_run_coex(rtwdev, BTC_RSN_NTFY_INIT);
}

void rtw89_btc_ntfy_scan_start(struct rtw89_dev *rtwdev, u8 phy_idx, u8 band)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_wl_info *wl = &btc->cx.wl;

	rtw89_debug(rtwdev, RTW89_DBG_BTC,
		    "[BTC], %s(): phy_idx=%d, band=%d\n",
		    __func__, phy_idx, band);
	btc->dm.cnt_notify[BTC_NCNT_SCAN_START]++;
	wl->status.map.scan = true;
	wl->scan_info.band[phy_idx] = band;
	wl->scan_info.phy_map |= BIT(phy_idx);
	_fw_set_drv_info(rtwdev, CXDRVINFO_SCAN);

	if (rtwdev->dbcc_en) {
		wl->dbcc_info.scan_band[phy_idx] = band;
		_update_dbcc_band(rtwdev, phy_idx);
		_fw_set_drv_info(rtwdev, CXDRVINFO_DBCC);
	}

	_run_coex(rtwdev, BTC_RSN_NTFY_SCAN_START);
}

void rtw89_btc_ntfy_scan_finish(struct rtw89_dev *rtwdev, u8 phy_idx)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_wl_info *wl = &btc->cx.wl;

	rtw89_debug(rtwdev, RTW89_DBG_BTC,
		    "[BTC], %s(): phy_idx=%d\n", __func__, phy_idx);
	btc->dm.cnt_notify[BTC_NCNT_SCAN_FINISH]++;

	wl->status.map.scan = false;
	wl->scan_info.phy_map &= ~BIT(phy_idx);
	_fw_set_drv_info(rtwdev, CXDRVINFO_SCAN);

	if (rtwdev->dbcc_en) {
		_update_dbcc_band(rtwdev, phy_idx);
		_fw_set_drv_info(rtwdev, CXDRVINFO_DBCC);
	}

	_run_coex(rtwdev, BTC_RSN_NTFY_SCAN_FINISH);
}

void rtw89_btc_ntfy_switch_band(struct rtw89_dev *rtwdev, u8 phy_idx, u8 band)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_wl_info *wl = &btc->cx.wl;

	rtw89_debug(rtwdev, RTW89_DBG_BTC,
		    "[BTC], %s(): phy_idx=%d, band=%d\n",
		    __func__, phy_idx, band);
	btc->dm.cnt_notify[BTC_NCNT_SWITCH_BAND]++;

	wl->scan_info.band[phy_idx] = band;
	wl->scan_info.phy_map |= BIT(phy_idx);
	_fw_set_drv_info(rtwdev, CXDRVINFO_SCAN);

	if (rtwdev->dbcc_en) {
		wl->dbcc_info.scan_band[phy_idx] = band;
		_update_dbcc_band(rtwdev, phy_idx);
		_fw_set_drv_info(rtwdev, CXDRVINFO_DBCC);
	}
	_run_coex(rtwdev, BTC_RSN_NTFY_SWBAND);
}

void rtw89_btc_ntfy_specific_packet(struct rtw89_dev *rtwdev,
				    enum btc_pkt_type pkt_type)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_cx *cx = &btc->cx;
	struct rtw89_btc_wl_info *wl = &cx->wl;
	struct rtw89_btc_bt_link_info *b = &cx->bt.link_info;
	struct rtw89_btc_bt_hfp_desc *hfp = &b->hfp_desc;
	struct rtw89_btc_bt_hid_desc *hid = &b->hid_desc;
	u32 cnt;
	u32 delay = RTW89_COEX_ACT1_WORK_PERIOD;
	bool delay_work = false;

	switch (pkt_type) {
	case PACKET_DHCP:
		cnt = ++cx->cnt_wl[BTC_WCNT_DHCP];
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s(): DHCP cnt=%d\n", __func__, cnt);
		wl->status.map.connecting = true;
		delay_work = true;
		break;
	case PACKET_EAPOL:
		cnt = ++cx->cnt_wl[BTC_WCNT_EAPOL];
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s(): EAPOL cnt=%d\n", __func__, cnt);
		wl->status.map._4way = true;
		delay_work = true;
		if (hfp->exist || hid->exist)
			delay /= 2;
		break;
	case PACKET_EAPOL_END:
		cnt = ++cx->cnt_wl[BTC_WCNT_EAPOL];
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s(): EAPOL_End cnt=%d\n",
			    __func__, cnt);
		wl->status.map._4way = false;
		cancel_delayed_work(&rtwdev->coex_act1_work);
		break;
	case PACKET_ARP:
		cnt = ++cx->cnt_wl[BTC_WCNT_ARP];
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s(): ARP cnt=%d\n", __func__, cnt);
		return;
	case PACKET_ICMP:
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s(): ICMP pkt\n", __func__);
		return;
	default:
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s(): unknown packet type %d\n",
			    __func__, pkt_type);
		return;
	}

	if (delay_work) {
		cancel_delayed_work(&rtwdev->coex_act1_work);
		ieee80211_queue_delayed_work(rtwdev->hw,
					     &rtwdev->coex_act1_work, delay);
	}

	btc->dm.cnt_notify[BTC_NCNT_SPECIAL_PACKET]++;
	_run_coex(rtwdev, BTC_RSN_NTFY_SPECIFIC_PACKET);
}

void rtw89_btc_ntfy_eapol_packet_work(struct work_struct *work)
{
	struct rtw89_dev *rtwdev = container_of(work, struct rtw89_dev,
						btc.eapol_notify_work);

	mutex_lock(&rtwdev->mutex);
	rtw89_leave_ps_mode(rtwdev);
	rtw89_btc_ntfy_specific_packet(rtwdev, PACKET_EAPOL);
	mutex_unlock(&rtwdev->mutex);
}

void rtw89_btc_ntfy_arp_packet_work(struct work_struct *work)
{
	struct rtw89_dev *rtwdev = container_of(work, struct rtw89_dev,
						btc.arp_notify_work);

	mutex_lock(&rtwdev->mutex);
	rtw89_btc_ntfy_specific_packet(rtwdev, PACKET_ARP);
	mutex_unlock(&rtwdev->mutex);
}

void rtw89_btc_ntfy_dhcp_packet_work(struct work_struct *work)
{
	struct rtw89_dev *rtwdev = container_of(work, struct rtw89_dev,
						btc.dhcp_notify_work);

	mutex_lock(&rtwdev->mutex);
	rtw89_leave_ps_mode(rtwdev);
	rtw89_btc_ntfy_specific_packet(rtwdev, PACKET_DHCP);
	mutex_unlock(&rtwdev->mutex);
}

void rtw89_btc_ntfy_icmp_packet_work(struct work_struct *work)
{
	struct rtw89_dev *rtwdev = container_of(work, struct rtw89_dev,
						btc.icmp_notify_work);

	mutex_lock(&rtwdev->mutex);
	rtw89_leave_ps_mode(rtwdev);
	rtw89_btc_ntfy_specific_packet(rtwdev, PACKET_ICMP);
	mutex_unlock(&rtwdev->mutex);
}

static void _update_bt_info(struct rtw89_dev *rtwdev, u8 *buf, u32 len)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_cx *cx = &btc->cx;
	struct rtw89_btc_bt_info *bt = &cx->bt;
	struct rtw89_btc_bt_link_info *b = &bt->link_info;
	struct rtw89_btc_bt_hfp_desc *hfp = &b->hfp_desc;
	struct rtw89_btc_bt_hid_desc *hid = &b->hid_desc;
	struct rtw89_btc_bt_a2dp_desc *a2dp = &b->a2dp_desc;
	struct rtw89_btc_bt_pan_desc *pan = &b->pan_desc;
	union btc_btinfo btinfo;

	if (buf[BTC_BTINFO_L1] != 6)
		return;

	if (!memcmp(bt->raw_info, buf, BTC_BTINFO_MAX)) {
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s(): return by bt-info duplicate!!\n",
			    __func__);
		cx->cnt_bt[BTC_BCNT_INFOSAME]++;
		return;
	}

	memcpy(bt->raw_info, buf, BTC_BTINFO_MAX);

	rtw89_debug(rtwdev, RTW89_DBG_BTC,
		    "[BTC], %s(): bt_info[2]=0x%02x\n",
		    __func__, bt->raw_info[2]);

	/* reset to mo-connect before update */
	b->status.val = BTC_BLINK_NOCONNECT;
	b->profile_cnt.last = b->profile_cnt.now;
	b->relink.last = b->relink.now;
	a2dp->exist_last = a2dp->exist;
	b->multi_link.last = b->multi_link.now;
	bt->inq_pag.last = bt->inq_pag.now;
	b->profile_cnt.now = 0;
	hid->type = 0;

	/* parse raw info low-Byte2 */
	btinfo.val = bt->raw_info[BTC_BTINFO_L2];
	b->status.map.connect = btinfo.lb2.connect;
	b->status.map.sco_busy = btinfo.lb2.sco_busy;
	b->status.map.acl_busy = btinfo.lb2.acl_busy;
	b->status.map.inq_pag = btinfo.lb2.inq_pag;
	bt->inq_pag.now = btinfo.lb2.inq_pag;
	cx->cnt_bt[BTC_BCNT_INQPAG] += !!(bt->inq_pag.now && !bt->inq_pag.last);

	hfp->exist = btinfo.lb2.hfp;
	b->profile_cnt.now += (u8)hfp->exist;
	hid->exist = btinfo.lb2.hid;
	b->profile_cnt.now += (u8)hid->exist;
	a2dp->exist = btinfo.lb2.a2dp;
	b->profile_cnt.now += (u8)a2dp->exist;
	pan->active = btinfo.lb2.pan;

	/* parse raw info low-Byte3 */
	btinfo.val = bt->raw_info[BTC_BTINFO_L3];
	if (btinfo.lb3.retry != 0)
		cx->cnt_bt[BTC_BCNT_RETRY]++;
	b->cqddr = btinfo.lb3.cqddr;
	cx->cnt_bt[BTC_BCNT_INQ] += !!(btinfo.lb3.inq && !bt->inq);
	bt->inq = btinfo.lb3.inq;
	cx->cnt_bt[BTC_BCNT_PAGE] += !!(btinfo.lb3.pag && !bt->pag);
	bt->pag = btinfo.lb3.pag;

	b->status.map.mesh_busy = btinfo.lb3.mesh_busy;
	/* parse raw info high-Byte0 */
	btinfo.val = bt->raw_info[BTC_BTINFO_H0];
	/* raw val is dBm unit, translate from -100~ 0dBm to 0~100%*/
	b->rssi = chip->ops->btc_get_bt_rssi(rtwdev, btinfo.hb0.rssi);

	/* parse raw info high-Byte1 */
	btinfo.val = bt->raw_info[BTC_BTINFO_H1];
	b->status.map.ble_connect = btinfo.hb1.ble_connect;
	if (btinfo.hb1.ble_connect)
		hid->type |= (hid->exist ? BTC_HID_BLE : BTC_HID_RCU);

	cx->cnt_bt[BTC_BCNT_REINIT] += !!(btinfo.hb1.reinit && !bt->reinit);
	bt->reinit = btinfo.hb1.reinit;
	cx->cnt_bt[BTC_BCNT_RELINK] += !!(btinfo.hb1.relink && !b->relink.now);
	b->relink.now = btinfo.hb1.relink;
	cx->cnt_bt[BTC_BCNT_IGNOWL] += !!(btinfo.hb1.igno_wl && !bt->igno_wl);
	bt->igno_wl = btinfo.hb1.igno_wl;

	if (bt->igno_wl && !cx->wl.status.map.rf_off)
		_set_bt_ignore_wlan_act(rtwdev, false);

	hid->type |= (btinfo.hb1.voice ? BTC_HID_RCU_VOICE : 0);
	bt->ble_scan_en = btinfo.hb1.ble_scan;

	cx->cnt_bt[BTC_BCNT_ROLESW] += !!(btinfo.hb1.role_sw && !b->role_sw);
	b->role_sw = btinfo.hb1.role_sw;

	b->multi_link.now = btinfo.hb1.multi_link;

	/* parse raw info high-Byte2 */
	btinfo.val = bt->raw_info[BTC_BTINFO_H2];
	pan->exist = btinfo.hb2.pan_active;
	b->profile_cnt.now += (u8)pan->exist;

	cx->cnt_bt[BTC_BCNT_AFH] += !!(btinfo.hb2.afh_update && !b->afh_update);
	b->afh_update = btinfo.hb2.afh_update;
	a2dp->active = btinfo.hb2.a2dp_active;
	b->slave_role = btinfo.hb2.slave;
	hid->slot_info = btinfo.hb2.hid_slot;
	hid->pair_cnt = btinfo.hb2.hid_cnt;
	hid->type |= (hid->slot_info == BTC_HID_218 ?
		      BTC_HID_218 : BTC_HID_418);
	/* parse raw info high-Byte3 */
	btinfo.val = bt->raw_info[BTC_BTINFO_H3];
	a2dp->bitpool = btinfo.hb3.a2dp_bitpool;

	if (b->tx_3m != (u32)btinfo.hb3.tx_3m)
		cx->cnt_bt[BTC_BCNT_RATECHG]++;
	b->tx_3m = (u32)btinfo.hb3.tx_3m;

	a2dp->sink = btinfo.hb3.a2dp_sink;

	if (b->profile_cnt.now || b->status.map.ble_connect)
		rtw89_btc_fw_en_rpt(rtwdev, RPT_EN_BT_AFH_MAP, 1);
	else
		rtw89_btc_fw_en_rpt(rtwdev, RPT_EN_BT_AFH_MAP, 0);

	if (!a2dp->exist_last && a2dp->exist) {
		a2dp->vendor_id = 0;
		a2dp->flush_time = 0;
		a2dp->play_latency = 1;
		ieee80211_queue_delayed_work(rtwdev->hw,
					     &rtwdev->coex_bt_devinfo_work,
					     RTW89_COEX_BT_DEVINFO_WORK_PERIOD);
	}

	if (a2dp->exist && (a2dp->flush_time == 0 || a2dp->vendor_id == 0 ||
			    a2dp->play_latency == 1))
		rtw89_btc_fw_en_rpt(rtwdev, RPT_EN_BT_DEVICE_INFO, 1);
	else
		rtw89_btc_fw_en_rpt(rtwdev, RPT_EN_BT_DEVICE_INFO, 0);

	_run_coex(rtwdev, BTC_RSN_UPDATE_BT_INFO);
}

enum btc_wl_mode {
	BTC_WL_MODE_HT = 0,
	BTC_WL_MODE_VHT = 1,
	BTC_WL_MODE_HE = 2,
	BTC_WL_MODE_NUM,
};

void rtw89_btc_ntfy_role_info(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif,
			      struct rtw89_sta *rtwsta, enum btc_role_state state)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	struct ieee80211_vif *vif = rtwvif_to_vif(rtwvif);
	struct ieee80211_sta *sta = rtwsta_to_sta(rtwsta);
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_wl_info *wl = &btc->cx.wl;
	struct rtw89_btc_wl_link_info r = {0};
	struct rtw89_btc_wl_link_info *wlinfo = NULL;
	u8 mode = 0;

	rtw89_debug(rtwdev, RTW89_DBG_BTC, "[BTC], state=%d\n", state);
	rtw89_debug(rtwdev, RTW89_DBG_BTC,
		    "[BTC], role is STA=%d\n",
		    vif->type == NL80211_IFTYPE_STATION);
	rtw89_debug(rtwdev, RTW89_DBG_BTC, "[BTC], port=%d\n", rtwvif->port);
	rtw89_debug(rtwdev, RTW89_DBG_BTC, "[BTC], band=%d ch=%d bw=%d\n",
		    hal->current_band_type, hal->current_channel,
		    hal->current_band_width);
	rtw89_debug(rtwdev, RTW89_DBG_BTC, "[BTC], associated=%d\n",
		    state == BTC_ROLE_MSTS_STA_CONN_END);
	rtw89_debug(rtwdev, RTW89_DBG_BTC,
		    "[BTC], bcn_period=%d dtim_period=%d\n",
		    vif->bss_conf.beacon_int, vif->bss_conf.dtim_period);

	if (rtwsta) {
		rtw89_debug(rtwdev, RTW89_DBG_BTC, "[BTC], STA mac_id=%d\n",
			    rtwsta->mac_id);

		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], STA support HE=%d VHT=%d HT=%d\n",
			    sta->he_cap.has_he,
			    sta->vht_cap.vht_supported,
			    sta->ht_cap.ht_supported);
		if (sta->he_cap.has_he)
			mode |= BIT(BTC_WL_MODE_HE);
		if (sta->vht_cap.vht_supported)
			mode |= BIT(BTC_WL_MODE_VHT);
		if (sta->ht_cap.ht_supported)
			mode |= BIT(BTC_WL_MODE_HT);

		r.mode = mode;
	}

	if (rtwvif->wifi_role >= RTW89_WIFI_ROLE_MLME_MAX)
		return;

	rtw89_debug(rtwdev, RTW89_DBG_BTC,
		    "[BTC], wifi_role=%d\n", rtwvif->wifi_role);

	r.role = rtwvif->wifi_role;
	r.phy = rtwvif->phy_idx;
	r.pid = rtwvif->port;
	r.active = true;
	r.connected = MLME_LINKED;
	r.bcn_period = vif->bss_conf.beacon_int;
	r.dtim_period = vif->bss_conf.dtim_period;
	r.band = hal->current_band_type;
	r.ch = hal->current_channel;
	r.bw = hal->current_band_width;
	ether_addr_copy(r.mac_addr, rtwvif->mac_addr);

	if (rtwsta && vif->type == NL80211_IFTYPE_STATION)
		r.mac_id = rtwsta->mac_id;

	btc->dm.cnt_notify[BTC_NCNT_ROLE_INFO]++;

	wlinfo = &wl->link_info[r.pid];

	memcpy(wlinfo, &r, sizeof(*wlinfo));
	_update_wl_info(rtwdev);

	if (wlinfo->role == RTW89_WIFI_ROLE_STATION &&
	    wlinfo->connected == MLME_NO_LINK)
		btc->dm.leak_ap = 0;

	if (state == BTC_ROLE_MSTS_STA_CONN_START)
		wl->status.map.connecting = 1;
	else
		wl->status.map.connecting = 0;

	if (state == BTC_ROLE_MSTS_STA_DIS_CONN)
		wl->status.map._4way = false;

	_run_coex(rtwdev, BTC_RSN_NTFY_ROLE_INFO);
}

void rtw89_btc_ntfy_radio_state(struct rtw89_dev *rtwdev, enum btc_rfctrl rf_state)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_wl_info *wl = &btc->cx.wl;

	rtw89_debug(rtwdev, RTW89_DBG_BTC, "[BTC], %s(): rf_state = %d\n",
		    __func__, rf_state);
	btc->dm.cnt_notify[BTC_NCNT_RADIO_STATE]++;

	switch (rf_state) {
	case BTC_RFCTRL_WL_OFF:
		wl->status.map.rf_off = 1;
		wl->status.map.lps = BTC_LPS_OFF;
		break;
	case BTC_RFCTRL_FW_CTRL:
		wl->status.map.rf_off = 0;
		wl->status.map.lps = BTC_LPS_RF_OFF;
		break;
	case BTC_RFCTRL_WL_ON:
	default:
		wl->status.map.rf_off = 0;
		wl->status.map.lps = BTC_LPS_OFF;
		break;
	}

	if (rf_state == BTC_RFCTRL_WL_ON) {
		rtw89_btc_fw_en_rpt(rtwdev,
				    RPT_EN_MREG | RPT_EN_BT_VER_INFO, true);
		_write_scbd(rtwdev, BTC_WSCB_ACTIVE, true);
		_update_bt_scbd(rtwdev, true);
		chip->ops->btc_init_cfg(rtwdev);
	} else {
		rtw89_btc_fw_en_rpt(rtwdev, RPT_EN_ALL, false);
		_write_scbd(rtwdev, BTC_WSCB_ACTIVE | BTC_WSCB_WLBUSY, false);
	}

	_run_coex(rtwdev, BTC_RSN_NTFY_RADIO_STATE);

	wl->status.map.rf_off_pre = wl->status.map.rf_off;
	wl->status.map.lps_pre = wl->status.map.lps;
}

static bool _ntfy_wl_rfk(struct rtw89_dev *rtwdev, u8 phy_path,
			 enum btc_wl_rfk_type type,
			 enum btc_wl_rfk_state state)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_cx *cx = &btc->cx;
	struct rtw89_btc_wl_info *wl = &cx->wl;
	bool result = BTC_WRFK_REJECT;

	wl->rfk_info.type = type;
	wl->rfk_info.path_map = FIELD_GET(BTC_RFK_PATH_MAP, phy_path);
	wl->rfk_info.phy_map = FIELD_GET(BTC_RFK_PHY_MAP, phy_path);
	wl->rfk_info.band = FIELD_GET(BTC_RFK_BAND_MAP, phy_path);

	rtw89_debug(rtwdev, RTW89_DBG_BTC,
		    "[BTC], %s()_start: phy=0x%x, path=0x%x, type=%d, state=%d\n",
		    __func__, wl->rfk_info.phy_map, wl->rfk_info.path_map,
		    type, state);

	switch (state) {
	case BTC_WRFK_START:
		result = _chk_wl_rfk_request(rtwdev);
		wl->rfk_info.state = result ? BTC_WRFK_START : BTC_WRFK_STOP;

		_write_scbd(rtwdev, BTC_WSCB_WLRFK, result);

		btc->dm.cnt_notify[BTC_NCNT_WL_RFK]++;
		break;
	case BTC_WRFK_ONESHOT_START:
	case BTC_WRFK_ONESHOT_STOP:
		if (wl->rfk_info.state == BTC_WRFK_STOP) {
			result = BTC_WRFK_REJECT;
		} else {
			result = BTC_WRFK_ALLOW;
			wl->rfk_info.state = state;
		}
		break;
	case BTC_WRFK_STOP:
		result = BTC_WRFK_ALLOW;
		wl->rfk_info.state = BTC_WRFK_STOP;

		_write_scbd(rtwdev, BTC_WSCB_WLRFK, false);
		cancel_delayed_work(&rtwdev->coex_rfk_chk_work);
		break;
	default:
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s() warning state=%d\n", __func__, state);
		break;
	}

	if (result == BTC_WRFK_ALLOW) {
		if (wl->rfk_info.state == BTC_WRFK_START ||
		    wl->rfk_info.state == BTC_WRFK_STOP)
			_run_coex(rtwdev, BTC_RSN_NTFY_WL_RFK);

		if (wl->rfk_info.state == BTC_WRFK_START)
			ieee80211_queue_delayed_work(rtwdev->hw,
						     &rtwdev->coex_rfk_chk_work,
						     RTW89_COEX_RFK_CHK_WORK_PERIOD);
	}

	rtw89_debug(rtwdev, RTW89_DBG_BTC,
		    "[BTC], %s()_finish: rfk_cnt=%d, result=%d\n",
		    __func__, btc->dm.cnt_notify[BTC_NCNT_WL_RFK], result);

	return result == BTC_WRFK_ALLOW;
}

void rtw89_btc_ntfy_wl_rfk(struct rtw89_dev *rtwdev, u8 phy_map,
			   enum btc_wl_rfk_type type,
			   enum btc_wl_rfk_state state)
{
	u8 band;
	bool allow;
	int ret;

	band = FIELD_GET(BTC_RFK_BAND_MAP, phy_map);

	rtw89_debug(rtwdev, RTW89_DBG_RFK,
		    "[RFK] RFK notify (%s / PHY%u / K_type = %u / path_idx = %lu / process = %s)\n",
		    band == RTW89_BAND_2G ? "2G" :
		    band == RTW89_BAND_5G ? "5G" : "6G",
		    !!(FIELD_GET(BTC_RFK_PHY_MAP, phy_map) & BIT(RTW89_PHY_1)),
		    type,
		    FIELD_GET(BTC_RFK_PATH_MAP, phy_map),
		    state == BTC_WRFK_STOP ? "RFK_STOP" :
		    state == BTC_WRFK_START ? "RFK_START" :
		    state == BTC_WRFK_ONESHOT_START ? "ONE-SHOT_START" :
		    "ONE-SHOT_STOP");

	if (state != BTC_WRFK_START || rtwdev->is_bt_iqk_timeout) {
		_ntfy_wl_rfk(rtwdev, phy_map, type, state);
		return;
	}

	ret = read_poll_timeout(_ntfy_wl_rfk, allow, allow, 40, 100000, false,
				rtwdev, phy_map, type, state);
	if (ret) {
		rtw89_warn(rtwdev, "RFK notify timeout\n");
		rtwdev->is_bt_iqk_timeout = true;
	}
}

struct rtw89_btc_wl_sta_iter_data {
	struct rtw89_dev *rtwdev;
	u8 busy_all;
	u8 dir_all;
	u8 rssi_map_all;
	bool is_sta_change;
	bool is_traffic_change;
};

static void rtw89_btc_ntfy_wl_sta_iter(void *data, struct ieee80211_sta *sta)
{
	struct rtw89_btc_wl_sta_iter_data *iter_data =
				(struct rtw89_btc_wl_sta_iter_data *)data;
	struct rtw89_dev *rtwdev = iter_data->rtwdev;
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_wl_info *wl = &btc->cx.wl;
	struct rtw89_btc_wl_link_info *link_info = NULL;
	struct rtw89_sta *rtwsta = (struct rtw89_sta *)sta->drv_priv;
	struct rtw89_traffic_stats *link_info_t = NULL;
	struct rtw89_vif *rtwvif = rtwsta->rtwvif;
	struct rtw89_traffic_stats *stats = &rtwvif->stats;
	const struct rtw89_chip_info *chip = rtwdev->chip;
	u32 last_tx_rate, last_rx_rate;
	u16 last_tx_lvl, last_rx_lvl;
	u8 port = rtwvif->port;
	u8 rssi;
	u8 busy = 0;
	u8 dir = 0;
	u8 rssi_map = 0;
	u8 i = 0;
	bool is_sta_change = false, is_traffic_change = false;

	rssi = ewma_rssi_read(&rtwsta->avg_rssi) >> RSSI_FACTOR;
	rtw89_debug(rtwdev, RTW89_DBG_BTC, "[BTC], rssi=%d\n", rssi);

	link_info = &wl->link_info[port];
	link_info->stat.traffic = rtwvif->stats;
	link_info_t = &link_info->stat.traffic;

	if (link_info->connected == MLME_NO_LINK) {
		link_info->rx_rate_drop_cnt = 0;
		return;
	}

	link_info->stat.rssi = rssi;
	for (i = 0; i < BTC_WL_RSSI_THMAX; i++) {
		link_info->rssi_state[i] =
			_update_rssi_state(rtwdev,
					   link_info->rssi_state[i],
					   link_info->stat.rssi,
					   chip->wl_rssi_thres[i]);
		if (BTC_RSSI_LOW(link_info->rssi_state[i]))
			rssi_map |= BIT(i);

		if (btc->mdinfo.ant.type == BTC_ANT_DEDICATED &&
		    BTC_RSSI_CHANGE(link_info->rssi_state[i]))
			is_sta_change = true;
	}
	iter_data->rssi_map_all |= rssi_map;

	last_tx_rate = link_info_t->tx_rate;
	last_rx_rate = link_info_t->rx_rate;
	last_tx_lvl = (u16)link_info_t->tx_tfc_lv;
	last_rx_lvl = (u16)link_info_t->rx_tfc_lv;

	if (stats->tx_tfc_lv != RTW89_TFC_IDLE ||
	    stats->rx_tfc_lv != RTW89_TFC_IDLE)
		busy = 1;

	if (stats->tx_tfc_lv > stats->rx_tfc_lv)
		dir = RTW89_TFC_UL;
	else
		dir = RTW89_TFC_DL;

	link_info = &wl->link_info[port];
	if (link_info->busy != busy || link_info->dir != dir) {
		is_sta_change = true;
		link_info->busy = busy;
		link_info->dir = dir;
	}

	iter_data->busy_all |= busy;
	iter_data->dir_all |= BIT(dir);

	if (rtwsta->rx_hw_rate <= RTW89_HW_RATE_CCK2 &&
	    last_rx_rate > RTW89_HW_RATE_CCK2 &&
	    link_info_t->rx_tfc_lv > RTW89_TFC_IDLE)
		link_info->rx_rate_drop_cnt++;

	if (last_tx_rate != rtwsta->ra_report.hw_rate ||
	    last_rx_rate != rtwsta->rx_hw_rate ||
	    last_tx_lvl != link_info_t->tx_tfc_lv ||
	    last_rx_lvl != link_info_t->rx_tfc_lv)
		is_traffic_change = true;

	link_info_t->tx_rate = rtwsta->ra_report.hw_rate;
	link_info_t->rx_rate = rtwsta->rx_hw_rate;

	wl->role_info.active_role[port].tx_lvl = (u16)stats->tx_tfc_lv;
	wl->role_info.active_role[port].rx_lvl = (u16)stats->rx_tfc_lv;
	wl->role_info.active_role[port].tx_rate = rtwsta->ra_report.hw_rate;
	wl->role_info.active_role[port].rx_rate = rtwsta->rx_hw_rate;

	if (is_sta_change)
		iter_data->is_sta_change = true;

	if (is_traffic_change)
		iter_data->is_traffic_change = true;
}

#define BTC_NHM_CHK_INTVL 20

void rtw89_btc_ntfy_wl_sta(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_wl_info *wl = &btc->cx.wl;
	struct rtw89_btc_wl_sta_iter_data data = {.rtwdev = rtwdev};
	u8 i;

	ieee80211_iterate_stations_atomic(rtwdev->hw,
					  rtw89_btc_ntfy_wl_sta_iter,
					  &data);

	wl->rssi_level = 0;
	btc->dm.cnt_notify[BTC_NCNT_WL_STA]++;
	for (i = BTC_WL_RSSI_THMAX; i > 0; i--) {
		/* set RSSI level 4 ~ 0 if rssi bit map match */
		if (data.rssi_map_all & BIT(i - 1)) {
			wl->rssi_level = i;
			break;
		}
	}

	rtw89_debug(rtwdev, RTW89_DBG_BTC, "[BTC], %s(): busy=%d\n",
		    __func__, !!wl->status.map.busy);

	_write_scbd(rtwdev, BTC_WSCB_WLBUSY, (!!wl->status.map.busy));

	if (data.is_traffic_change)
		_fw_set_drv_info(rtwdev, CXDRVINFO_ROLE);
	if (data.is_sta_change) {
		wl->status.map.busy = data.busy_all;
		wl->status.map.traffic_dir = data.dir_all;
		_run_coex(rtwdev, BTC_RSN_NTFY_WL_STA);
	} else if (btc->dm.cnt_notify[BTC_NCNT_WL_STA] >=
		   btc->dm.cnt_dm[BTC_DCNT_WL_STA_LAST] + BTC_NHM_CHK_INTVL) {
		btc->dm.cnt_dm[BTC_DCNT_WL_STA_LAST] =
			btc->dm.cnt_notify[BTC_NCNT_WL_STA];
	} else if (btc->dm.cnt_notify[BTC_NCNT_WL_STA] <
		   btc->dm.cnt_dm[BTC_DCNT_WL_STA_LAST]) {
		btc->dm.cnt_dm[BTC_DCNT_WL_STA_LAST] =
		btc->dm.cnt_notify[BTC_NCNT_WL_STA];
	}
}

void rtw89_btc_c2h_handle(struct rtw89_dev *rtwdev, struct sk_buff *skb,
			  u32 len, u8 class, u8 func)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_btf_fwinfo *pfwinfo = &btc->fwinfo;
	u8 *buf = &skb->data[RTW89_C2H_HEADER_LEN];

	len -= RTW89_C2H_HEADER_LEN;

	rtw89_debug(rtwdev, RTW89_DBG_BTC,
		    "[BTC], %s(): C2H BT len:%d class:%d fun:%d\n",
		    __func__, len, class, func);

	if (class != BTFC_FW_EVENT)
		return;

	switch (func) {
	case BTF_EVNT_RPT:
	case BTF_EVNT_BUF_OVERFLOW:
		pfwinfo->event[func]++;
		/* Don't need rtw89_leave_ps_mode() */
		btc_fw_event(rtwdev, func, buf, len);
		break;
	case BTF_EVNT_BT_INFO:
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], handle C2H BT INFO with data %8ph\n", buf);
		btc->cx.cnt_bt[BTC_BCNT_INFOUPDATE]++;
		_update_bt_info(rtwdev, buf, len);
		break;
	case BTF_EVNT_BT_SCBD:
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], handle C2H BT SCBD with data %8ph\n", buf);
		btc->cx.cnt_bt[BTC_BCNT_SCBDUPDATE]++;
		_update_bt_scbd(rtwdev, false);
		break;
	case BTF_EVNT_BT_PSD:
		break;
	case BTF_EVNT_BT_REG:
		btc->dbg.rb_done = true;
		btc->dbg.rb_val = le32_to_cpu(*((__le32 *)buf));

		break;
	case BTF_EVNT_C2H_LOOPBACK:
		btc->dbg.rb_done = true;
		btc->dbg.rb_val = buf[0];
		break;
	case BTF_EVNT_CX_RUNINFO:
		btc->dm.cnt_dm[BTC_DCNT_CX_RUNINFO]++;
		break;
	}
}

#define BTC_CX_FW_OFFLOAD 0

static void _show_cx_info(struct rtw89_dev *rtwdev, struct seq_file *m)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_hal *hal = &rtwdev->hal;
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_dm *dm = &btc->dm;
	struct rtw89_btc_bt_info *bt = &btc->cx.bt;
	struct rtw89_btc_wl_info *wl = &btc->cx.wl;
	u32 ver_main = 0, ver_sub = 0, ver_hotfix = 0, id_branch = 0;

	if (!(dm->coex_info_map & BTC_COEX_INFO_CX))
		return;

	dm->cnt_notify[BTC_NCNT_SHOW_COEX_INFO]++;

	seq_printf(m, "========== [BTC COEX INFO (%d)] ==========\n",
		   chip->chip_id);

	ver_main = FIELD_GET(GENMASK(31, 24), chip->para_ver);
	ver_sub = FIELD_GET(GENMASK(23, 16), chip->para_ver);
	ver_hotfix = FIELD_GET(GENMASK(15, 8), chip->para_ver);
	id_branch = FIELD_GET(GENMASK(7, 0), chip->para_ver);
	seq_printf(m, " %-15s : Coex:%d.%d.%d(branch:%d), ",
		   "[coex_version]", ver_main, ver_sub, ver_hotfix, id_branch);

	if (dm->wl_fw_cx_offload != BTC_CX_FW_OFFLOAD)
		dm->error.map.offload_mismatch = true;
	else
		dm->error.map.offload_mismatch = false;

	ver_main = FIELD_GET(GENMASK(31, 24), wl->ver_info.fw_coex);
	ver_sub = FIELD_GET(GENMASK(23, 16), wl->ver_info.fw_coex);
	ver_hotfix = FIELD_GET(GENMASK(15, 8), wl->ver_info.fw_coex);
	id_branch = FIELD_GET(GENMASK(7, 0), wl->ver_info.fw_coex);
	seq_printf(m, "WL_FW_coex:%d.%d.%d(branch:%d)",
		   ver_main, ver_sub, ver_hotfix, id_branch);

	ver_main = FIELD_GET(GENMASK(31, 24), chip->wlcx_desired);
	ver_sub = FIELD_GET(GENMASK(23, 16), chip->wlcx_desired);
	ver_hotfix = FIELD_GET(GENMASK(15, 8), chip->wlcx_desired);
	seq_printf(m, "(%s, desired:%d.%d.%d), ",
		   (wl->ver_info.fw_coex >= chip->wlcx_desired ?
		   "Match" : "Mis-Match"), ver_main, ver_sub, ver_hotfix);

	seq_printf(m, "BT_FW_coex:%d(%s, desired:%d)\n",
		   bt->ver_info.fw_coex,
		   (bt->ver_info.fw_coex >= chip->btcx_desired ?
		   "Match" : "Mis-Match"), chip->btcx_desired);

	if (bt->enable.now && bt->ver_info.fw == 0)
		rtw89_btc_fw_en_rpt(rtwdev, RPT_EN_BT_VER_INFO, true);
	else
		rtw89_btc_fw_en_rpt(rtwdev, RPT_EN_BT_VER_INFO, false);

	ver_main = FIELD_GET(GENMASK(31, 24), wl->ver_info.fw);
	ver_sub = FIELD_GET(GENMASK(23, 16), wl->ver_info.fw);
	ver_hotfix = FIELD_GET(GENMASK(15, 8), wl->ver_info.fw);
	id_branch = FIELD_GET(GENMASK(7, 0), wl->ver_info.fw);
	seq_printf(m, " %-15s : WL_FW:%d.%d.%d.%d, BT_FW:0x%x(%s)\n",
		   "[sub_module]",
		   ver_main, ver_sub, ver_hotfix, id_branch,
		   bt->ver_info.fw, bt->run_patch_code ? "patch" : "ROM");

	seq_printf(m, " %-15s : cv:%x, rfe_type:0x%x, ant_iso:%d, ant_pg:%d, %s",
		   "[hw_info]", btc->mdinfo.cv, btc->mdinfo.rfe_type,
		   btc->mdinfo.ant.isolation, btc->mdinfo.ant.num,
		   (btc->mdinfo.ant.num > 1 ? "" : (btc->mdinfo.ant.single_pos ?
		   "1Ant_Pos:S1, " : "1Ant_Pos:S0, ")));

	seq_printf(m, "3rd_coex:%d, dbcc:%d, tx_num:%d, rx_num:%d\n",
		   btc->cx.other.type, rtwdev->dbcc_en, hal->tx_nss,
		   hal->rx_nss);
}

static void _show_wl_role_info(struct rtw89_dev *rtwdev, struct seq_file *m)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_wl_link_info *plink = NULL;
	struct rtw89_btc_wl_info *wl = &btc->cx.wl;
	struct rtw89_btc_wl_dbcc_info *wl_dinfo = &wl->dbcc_info;
	struct rtw89_traffic_stats *t;
	u8 i;

	if (rtwdev->dbcc_en) {
		seq_printf(m,
			   " %-15s : PHY0_band(op:%d/scan:%d/real:%d), ",
			   "[dbcc_info]", wl_dinfo->op_band[RTW89_PHY_0],
			   wl_dinfo->scan_band[RTW89_PHY_0],
			   wl_dinfo->real_band[RTW89_PHY_0]);
		seq_printf(m,
			   "PHY1_band(op:%d/scan:%d/real:%d)\n",
			   wl_dinfo->op_band[RTW89_PHY_1],
			   wl_dinfo->scan_band[RTW89_PHY_1],
			   wl_dinfo->real_band[RTW89_PHY_1]);
	}

	for (i = 0; i < RTW89_MAX_HW_PORT_NUM; i++) {
		plink = &btc->cx.wl.link_info[i];

		if (!plink->active)
			continue;

		seq_printf(m,
			   " [port_%d]        : role=%d(phy-%d), connect=%d(client_cnt=%d), mode=%d, center_ch=%d, bw=%d",
			   plink->pid, (u32)plink->role, plink->phy,
			   (u32)plink->connected, plink->client_cnt - 1,
			   (u32)plink->mode, plink->ch, (u32)plink->bw);

		if (plink->connected == MLME_NO_LINK)
			continue;

		seq_printf(m,
			   ", mac_id=%d, max_tx_time=%dus, max_tx_retry=%d\n",
			   plink->mac_id, plink->tx_time, plink->tx_retry);

		seq_printf(m,
			   " [port_%d]        : rssi=-%ddBm(%d), busy=%d, dir=%s, ",
			   plink->pid, 110 - plink->stat.rssi,
			   plink->stat.rssi, plink->busy,
			   plink->dir == RTW89_TFC_UL ? "UL" : "DL");

		t = &plink->stat.traffic;

		seq_printf(m,
			   "tx[rate:%d/busy_level:%d], ",
			   (u32)t->tx_rate, t->tx_tfc_lv);

		seq_printf(m, "rx[rate:%d/busy_level:%d/drop:%d]\n",
			   (u32)t->rx_rate,
			   t->rx_tfc_lv, plink->rx_rate_drop_cnt);
	}
}

static void _show_wl_info(struct rtw89_dev *rtwdev, struct seq_file *m)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_cx *cx = &btc->cx;
	struct rtw89_btc_wl_info *wl = &cx->wl;
	struct rtw89_btc_wl_role_info *wl_rinfo = &wl->role_info;

	if (!(btc->dm.coex_info_map & BTC_COEX_INFO_WL))
		return;

	seq_puts(m, "========== [WL Status] ==========\n");

	seq_printf(m, " %-15s : link_mode:%d, ",
		   "[status]", (u32)wl_rinfo->link_mode);

	seq_printf(m,
		   "rf_off:%s, power_save:%s, scan:%s(band:%d/phy_map:0x%x), ",
		   wl->status.map.rf_off ? "Y" : "N",
		   wl->status.map.lps ? "Y" : "N",
		   wl->status.map.scan ? "Y" : "N",
		   wl->scan_info.band[RTW89_PHY_0], wl->scan_info.phy_map);

	seq_printf(m,
		   "connecting:%s, roam:%s, 4way:%s, init_ok:%s\n",
		   wl->status.map.connecting ? "Y" : "N",
		   wl->status.map.roaming ?  "Y" : "N",
		   wl->status.map._4way ? "Y" : "N",
		   wl->status.map.init_ok ? "Y" : "N");

	_show_wl_role_info(rtwdev, m);
}

enum btc_bt_a2dp_type {
	BTC_A2DP_LEGACY = 0,
	BTC_A2DP_TWS_SNIFF = 1,
	BTC_A2DP_TWS_RELAY = 2,
};

static void _show_bt_profile_info(struct rtw89_dev *rtwdev, struct seq_file *m)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_bt_link_info *bt_linfo = &btc->cx.bt.link_info;
	struct rtw89_btc_bt_hfp_desc hfp = bt_linfo->hfp_desc;
	struct rtw89_btc_bt_hid_desc hid = bt_linfo->hid_desc;
	struct rtw89_btc_bt_a2dp_desc a2dp = bt_linfo->a2dp_desc;
	struct rtw89_btc_bt_pan_desc pan = bt_linfo->pan_desc;

	if (hfp.exist) {
		seq_printf(m, " %-15s : type:%s, sut_pwr:%d, golden-rx:%d",
			   "[HFP]", (hfp.type == 0 ? "SCO" : "eSCO"),
			   bt_linfo->sut_pwr_level[0],
			   bt_linfo->golden_rx_shift[0]);
	}

	if (hid.exist) {
		seq_printf(m,
			   "\n\r %-15s : type:%s%s%s%s%s pair-cnt:%d, sut_pwr:%d, golden-rx:%d\n",
			   "[HID]",
			   hid.type & BTC_HID_218 ? "2/18," : "",
			   hid.type & BTC_HID_418 ? "4/18," : "",
			   hid.type & BTC_HID_BLE ? "BLE," : "",
			   hid.type & BTC_HID_RCU ? "RCU," : "",
			   hid.type & BTC_HID_RCU_VOICE ? "RCU-Voice," : "",
			   hid.pair_cnt, bt_linfo->sut_pwr_level[1],
			   bt_linfo->golden_rx_shift[1]);
	}

	if (a2dp.exist) {
		seq_printf(m,
			   " %-15s : type:%s, bit-pool:%d, flush-time:%d, ",
			   "[A2DP]",
			   a2dp.type == BTC_A2DP_LEGACY ? "Legacy" : "TWS",
			    a2dp.bitpool, a2dp.flush_time);

		seq_printf(m,
			   "vid:0x%x, Dev-name:0x%x, sut_pwr:%d, golden-rx:%d\n",
			   a2dp.vendor_id, a2dp.device_name,
			   bt_linfo->sut_pwr_level[2],
			   bt_linfo->golden_rx_shift[2]);
	}

	if (pan.exist) {
		seq_printf(m, " %-15s : sut_pwr:%d, golden-rx:%d\n",
			   "[PAN]",
			   bt_linfo->sut_pwr_level[3],
			   bt_linfo->golden_rx_shift[3]);
	}
}

static void _show_bt_info(struct rtw89_dev *rtwdev, struct seq_file *m)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_cx *cx = &btc->cx;
	struct rtw89_btc_bt_info *bt = &cx->bt;
	struct rtw89_btc_wl_info *wl = &cx->wl;
	struct rtw89_btc_module *module = &btc->mdinfo;
	struct rtw89_btc_bt_link_info *bt_linfo = &bt->link_info;
	u8 *afh = bt_linfo->afh_map;

	if (!(btc->dm.coex_info_map & BTC_COEX_INFO_BT))
		return;

	seq_puts(m, "========== [BT Status] ==========\n");

	seq_printf(m, " %-15s : enable:%s, btg:%s%s, connect:%s, ",
		   "[status]", bt->enable.now ? "Y" : "N",
		   bt->btg_type ? "Y" : "N",
		   (bt->enable.now && (bt->btg_type != module->bt_pos) ?
		   "(efuse-mismatch!!)" : ""),
		   (bt_linfo->status.map.connect ? "Y" : "N"));

	seq_printf(m, "igno_wl:%s, mailbox_avl:%s, rfk_state:0x%x\n",
		   bt->igno_wl ? "Y" : "N",
		   bt->mbx_avl ? "Y" : "N", bt->rfk_info.val);

	seq_printf(m, " %-15s : profile:%s%s%s%s%s ",
		   "[profile]",
		   (bt_linfo->profile_cnt.now == 0) ? "None," : "",
		   bt_linfo->hfp_desc.exist ? "HFP," : "",
		   bt_linfo->hid_desc.exist ? "HID," : "",
		   bt_linfo->a2dp_desc.exist ?
		   (bt_linfo->a2dp_desc.sink ? "A2DP_sink," : "A2DP,") : "",
		   bt_linfo->pan_desc.exist ? "PAN," : "");

	seq_printf(m,
		   "multi-link:%s, role:%s, ble-connect:%s, CQDDR:%s, A2DP_active:%s, PAN_active:%s\n",
		   bt_linfo->multi_link.now ? "Y" : "N",
		   bt_linfo->slave_role ? "Slave" : "Master",
		   bt_linfo->status.map.ble_connect ? "Y" : "N",
		   bt_linfo->cqddr ? "Y" : "N",
		   bt_linfo->a2dp_desc.active ? "Y" : "N",
		   bt_linfo->pan_desc.active ? "Y" : "N");

	seq_printf(m,
		   " %-15s : rssi:%ddBm, tx_rate:%dM, %s%s%s",
		   "[link]", bt_linfo->rssi - 100,
		   bt_linfo->tx_3m ? 3 : 2,
		   bt_linfo->status.map.inq_pag ? " inq-page!!" : "",
		   bt_linfo->status.map.acl_busy ? " acl_busy!!" : "",
		   bt_linfo->status.map.mesh_busy ? " mesh_busy!!" : "");

	seq_printf(m,
		   "%s afh_map[%02x%02x_%02x%02x_%02x%02x_%02x%02x_%02x%02x], ",
		   bt_linfo->relink.now ? " ReLink!!" : "",
		   afh[0], afh[1], afh[2], afh[3], afh[4],
		   afh[5], afh[6], afh[7], afh[8], afh[9]);

	seq_printf(m, "wl_ch_map[en:%d/ch:%d/bw:%d]\n",
		   wl->afh_info.en, wl->afh_info.ch, wl->afh_info.bw);

	seq_printf(m,
		   " %-15s : retry:%d, relink:%d, rate_chg:%d, reinit:%d, reenable:%d, ",
		   "[stat_cnt]", cx->cnt_bt[BTC_BCNT_RETRY],
		   cx->cnt_bt[BTC_BCNT_RELINK], cx->cnt_bt[BTC_BCNT_RATECHG],
		   cx->cnt_bt[BTC_BCNT_REINIT], cx->cnt_bt[BTC_BCNT_REENABLE]);

	seq_printf(m,
		   "role-switch:%d, afh:%d, inq_page:%d(inq:%d/page:%d), igno_wl:%d\n",
		   cx->cnt_bt[BTC_BCNT_ROLESW], cx->cnt_bt[BTC_BCNT_AFH],
		   cx->cnt_bt[BTC_BCNT_INQPAG], cx->cnt_bt[BTC_BCNT_INQ],
		   cx->cnt_bt[BTC_BCNT_PAGE], cx->cnt_bt[BTC_BCNT_IGNOWL]);

	_show_bt_profile_info(rtwdev, m);

	seq_printf(m,
		   " %-15s : raw_data[%02x %02x %02x %02x %02x %02x] (type:%s/cnt:%d/same:%d)\n",
		   "[bt_info]", bt->raw_info[2], bt->raw_info[3],
		   bt->raw_info[4], bt->raw_info[5], bt->raw_info[6],
		   bt->raw_info[7],
		   bt->raw_info[0] == BTC_BTINFO_AUTO ? "auto" : "reply",
		   cx->cnt_bt[BTC_BCNT_INFOUPDATE],
		   cx->cnt_bt[BTC_BCNT_INFOSAME]);

	seq_printf(m,
		   " %-15s : Hi-rx = %d, Hi-tx = %d, Lo-rx = %d, Lo-tx = %d (bt_polut_wl_tx = %d)\n",
		   "[trx_req_cnt]", cx->cnt_bt[BTC_BCNT_HIPRI_RX],
		   cx->cnt_bt[BTC_BCNT_HIPRI_TX], cx->cnt_bt[BTC_BCNT_LOPRI_RX],
		   cx->cnt_bt[BTC_BCNT_LOPRI_TX], cx->cnt_bt[BTC_BCNT_POLUT]);
}

#define CASE_BTC_RSN_STR(e) case BTC_RSN_ ## e: return #e
#define CASE_BTC_ACT_STR(e) case BTC_ACT_ ## e | BTC_ACT_EXT_BIT: return #e
#define CASE_BTC_POLICY_STR(e) \
	case BTC_CXP_ ## e | BTC_POLICY_EXT_BIT: return #e

static const char *steps_to_str(u16 step)
{
	switch (step) {
	CASE_BTC_RSN_STR(NONE);
	CASE_BTC_RSN_STR(NTFY_INIT);
	CASE_BTC_RSN_STR(NTFY_SWBAND);
	CASE_BTC_RSN_STR(NTFY_WL_STA);
	CASE_BTC_RSN_STR(NTFY_RADIO_STATE);
	CASE_BTC_RSN_STR(UPDATE_BT_SCBD);
	CASE_BTC_RSN_STR(NTFY_WL_RFK);
	CASE_BTC_RSN_STR(UPDATE_BT_INFO);
	CASE_BTC_RSN_STR(NTFY_SCAN_START);
	CASE_BTC_RSN_STR(NTFY_SCAN_FINISH);
	CASE_BTC_RSN_STR(NTFY_SPECIFIC_PACKET);
	CASE_BTC_RSN_STR(NTFY_POWEROFF);
	CASE_BTC_RSN_STR(NTFY_ROLE_INFO);
	CASE_BTC_RSN_STR(CMD_SET_COEX);
	CASE_BTC_RSN_STR(ACT1_WORK);
	CASE_BTC_RSN_STR(BT_DEVINFO_WORK);
	CASE_BTC_RSN_STR(RFK_CHK_WORK);

	CASE_BTC_ACT_STR(NONE);
	CASE_BTC_ACT_STR(WL_ONLY);
	CASE_BTC_ACT_STR(WL_5G);
	CASE_BTC_ACT_STR(WL_OTHER);
	CASE_BTC_ACT_STR(WL_IDLE);
	CASE_BTC_ACT_STR(WL_NC);
	CASE_BTC_ACT_STR(WL_RFK);
	CASE_BTC_ACT_STR(WL_INIT);
	CASE_BTC_ACT_STR(WL_OFF);
	CASE_BTC_ACT_STR(FREERUN);
	CASE_BTC_ACT_STR(BT_WHQL);
	CASE_BTC_ACT_STR(BT_RFK);
	CASE_BTC_ACT_STR(BT_OFF);
	CASE_BTC_ACT_STR(BT_IDLE);
	CASE_BTC_ACT_STR(BT_HFP);
	CASE_BTC_ACT_STR(BT_HID);
	CASE_BTC_ACT_STR(BT_A2DP);
	CASE_BTC_ACT_STR(BT_A2DPSINK);
	CASE_BTC_ACT_STR(BT_PAN);
	CASE_BTC_ACT_STR(BT_A2DP_HID);
	CASE_BTC_ACT_STR(BT_A2DP_PAN);
	CASE_BTC_ACT_STR(BT_PAN_HID);
	CASE_BTC_ACT_STR(BT_A2DP_PAN_HID);
	CASE_BTC_ACT_STR(WL_25G_MCC);
	CASE_BTC_ACT_STR(WL_2G_MCC);
	CASE_BTC_ACT_STR(WL_2G_SCC);
	CASE_BTC_ACT_STR(WL_2G_AP);
	CASE_BTC_ACT_STR(WL_2G_GO);
	CASE_BTC_ACT_STR(WL_2G_GC);
	CASE_BTC_ACT_STR(WL_2G_NAN);

	CASE_BTC_POLICY_STR(OFF_BT);
	CASE_BTC_POLICY_STR(OFF_WL);
	CASE_BTC_POLICY_STR(OFF_EQ0);
	CASE_BTC_POLICY_STR(OFF_EQ1);
	CASE_BTC_POLICY_STR(OFF_EQ2);
	CASE_BTC_POLICY_STR(OFF_EQ3);
	CASE_BTC_POLICY_STR(OFF_BWB0);
	CASE_BTC_POLICY_STR(OFF_BWB1);
	CASE_BTC_POLICY_STR(OFFB_BWB0);
	CASE_BTC_POLICY_STR(OFFE_DEF);
	CASE_BTC_POLICY_STR(OFFE_DEF2);
	CASE_BTC_POLICY_STR(FIX_TD3030);
	CASE_BTC_POLICY_STR(FIX_TD5050);
	CASE_BTC_POLICY_STR(FIX_TD2030);
	CASE_BTC_POLICY_STR(FIX_TD4010);
	CASE_BTC_POLICY_STR(FIX_TD7010);
	CASE_BTC_POLICY_STR(FIX_TD2060);
	CASE_BTC_POLICY_STR(FIX_TD3060);
	CASE_BTC_POLICY_STR(FIX_TD2080);
	CASE_BTC_POLICY_STR(FIX_TDW1B1);
	CASE_BTC_POLICY_STR(FIX_TD4020);
	CASE_BTC_POLICY_STR(PFIX_TD3030);
	CASE_BTC_POLICY_STR(PFIX_TD5050);
	CASE_BTC_POLICY_STR(PFIX_TD2030);
	CASE_BTC_POLICY_STR(PFIX_TD2060);
	CASE_BTC_POLICY_STR(PFIX_TD3070);
	CASE_BTC_POLICY_STR(PFIX_TD2080);
	CASE_BTC_POLICY_STR(PFIX_TDW1B1);
	CASE_BTC_POLICY_STR(AUTO_TD50200);
	CASE_BTC_POLICY_STR(AUTO_TD60200);
	CASE_BTC_POLICY_STR(AUTO_TD20200);
	CASE_BTC_POLICY_STR(AUTO_TDW1B1);
	CASE_BTC_POLICY_STR(PAUTO_TD50200);
	CASE_BTC_POLICY_STR(PAUTO_TD60200);
	CASE_BTC_POLICY_STR(PAUTO_TD20200);
	CASE_BTC_POLICY_STR(PAUTO_TDW1B1);
	CASE_BTC_POLICY_STR(AUTO2_TD3050);
	CASE_BTC_POLICY_STR(AUTO2_TD3070);
	CASE_BTC_POLICY_STR(AUTO2_TD5050);
	CASE_BTC_POLICY_STR(AUTO2_TD6060);
	CASE_BTC_POLICY_STR(AUTO2_TD2080);
	CASE_BTC_POLICY_STR(AUTO2_TDW1B4);
	CASE_BTC_POLICY_STR(PAUTO2_TD3050);
	CASE_BTC_POLICY_STR(PAUTO2_TD3070);
	CASE_BTC_POLICY_STR(PAUTO2_TD5050);
	CASE_BTC_POLICY_STR(PAUTO2_TD6060);
	CASE_BTC_POLICY_STR(PAUTO2_TD2080);
	CASE_BTC_POLICY_STR(PAUTO2_TDW1B4);
	default:
		return "unknown step";
	}
}

static
void seq_print_segment(struct seq_file *m, const char *prefix, u16 *data,
		       u8 len, u8 seg_len, u8 start_idx, u8 ring_len)
{
	u8 i;
	u8 cur_index;

	for (i = 0; i < len ; i++) {
		if ((i % seg_len) == 0)
			seq_printf(m, " %-15s : ", prefix);
		cur_index = (start_idx + i) % ring_len;
		if (i % 3 == 0)
			seq_printf(m, "-> %-20s",
				   steps_to_str(*(data + cur_index)));
		else if (i % 3 == 1)
			seq_printf(m, "-> %-15s",
				   steps_to_str(*(data + cur_index)));
		else
			seq_printf(m, "-> %-13s",
				   steps_to_str(*(data + cur_index)));
		if (i == (len - 1) || (i % seg_len) == (seg_len - 1))
			seq_puts(m, "\n");
	}
}

static void _show_dm_step(struct rtw89_dev *rtwdev, struct seq_file *m)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_dm *dm = &btc->dm;
	u8 start_idx;
	u8 len;

	len = dm->dm_step.step_ov ? RTW89_BTC_DM_MAXSTEP : dm->dm_step.step_pos;
	start_idx = dm->dm_step.step_ov ? dm->dm_step.step_pos : 0;

	seq_print_segment(m, "[dm_steps]", dm->dm_step.step, len, 6, start_idx,
			  ARRAY_SIZE(dm->dm_step.step));
}

static void _show_dm_info(struct rtw89_dev *rtwdev, struct seq_file *m)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_module *module = &btc->mdinfo;
	struct rtw89_btc_dm *dm = &btc->dm;
	struct rtw89_btc_wl_info *wl = &btc->cx.wl;
	struct rtw89_btc_bt_info *bt = &btc->cx.bt;

	if (!(dm->coex_info_map & BTC_COEX_INFO_DM))
		return;

	seq_printf(m, "========== [Mechanism Status %s] ==========\n",
		   (btc->ctrl.manual ? "(Manual)" : "(Auto)"));

	seq_printf(m,
		   " %-15s : type:%s, reason:%s(), action:%s(), ant_path:%ld, run_cnt:%d\n",
		   "[status]",
		   module->ant.type == BTC_ANT_SHARED ? "shared" : "dedicated",
		   steps_to_str(dm->run_reason),
		   steps_to_str(dm->run_action | BTC_ACT_EXT_BIT),
		   FIELD_GET(GENMASK(7, 0), dm->set_ant_path),
		   dm->cnt_dm[BTC_DCNT_RUN]);

	_show_dm_step(rtwdev, m);

	seq_printf(m, " %-15s : wl_only:%d, bt_only:%d, igno_bt:%d, free_run:%d, wl_ps_ctrl:%d, wl_mimo_ps:%d, ",
		   "[dm_flag]", dm->wl_only, dm->bt_only, btc->ctrl.igno_bt,
		   dm->freerun, btc->lps, dm->wl_mimo_ps);

	seq_printf(m, "leak_ap:%d, fw_offload:%s%s\n", dm->leak_ap,
		   (BTC_CX_FW_OFFLOAD ? "Y" : "N"),
		   (dm->wl_fw_cx_offload == BTC_CX_FW_OFFLOAD ?
		    "" : "(Mis-Match!!)"));

	if (dm->rf_trx_para.wl_tx_power == 0xff)
		seq_printf(m,
			   " %-15s : wl_rssi_lvl:%d, para_lvl:%d, wl_tx_pwr:orig, ",
			   "[trx_ctrl]", wl->rssi_level, dm->trx_para_level);

	else
		seq_printf(m,
			   " %-15s : wl_rssi_lvl:%d, para_lvl:%d, wl_tx_pwr:%d, ",
			   "[trx_ctrl]", wl->rssi_level, dm->trx_para_level,
			   dm->rf_trx_para.wl_tx_power);

	seq_printf(m,
		   "wl_rx_lvl:%d, bt_tx_pwr_dec:%d, bt_rx_lna:%d(%s-tbl), wl_btg_rx:%d\n",
		   dm->rf_trx_para.wl_rx_gain, dm->rf_trx_para.bt_tx_power,
		   dm->rf_trx_para.bt_rx_gain,
		   (bt->hi_lna_rx ? "Hi" : "Ori"), dm->wl_btg_rx);

	seq_printf(m,
		   " %-15s : wl_tx_limit[en:%d/max_t:%dus/max_retry:%d], bt_slot_reg:%d-TU\n",
		   "[dm_ctrl]", dm->wl_tx_limit.enable, dm->wl_tx_limit.tx_time,
		   dm->wl_tx_limit.tx_retry, btc->bt_req_len);
}

static void _show_error(struct rtw89_dev *rtwdev, struct seq_file *m)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_btf_fwinfo *pfwinfo = &btc->fwinfo;
	struct rtw89_btc_fbtc_cysta *pcysta = NULL;

	pcysta = &pfwinfo->rpt_fbtc_cysta.finfo;

	if (pfwinfo->event[BTF_EVNT_BUF_OVERFLOW] == 0 &&
	    pcysta->except_cnt == 0 &&
	    !pfwinfo->len_mismch && !pfwinfo->fver_mismch)
		return;

	seq_printf(m, " %-15s : ", "[error]");

	if (pfwinfo->event[BTF_EVNT_BUF_OVERFLOW]) {
		seq_printf(m,
			   "overflow-cnt: %d, ",
			   pfwinfo->event[BTF_EVNT_BUF_OVERFLOW]);
	}

	if (pfwinfo->len_mismch) {
		seq_printf(m,
			   "len-mismatch: 0x%x, ",
			   pfwinfo->len_mismch);
	}

	if (pfwinfo->fver_mismch) {
		seq_printf(m,
			   "fver-mismatch: 0x%x, ",
			   pfwinfo->fver_mismch);
	}

	/* cycle statistics exceptions */
	if (pcysta->exception || pcysta->except_cnt) {
		seq_printf(m,
			   "exception-type: 0x%x, exception-cnt = %d",
			   pcysta->exception, pcysta->except_cnt);
	}
	seq_puts(m, "\n");
}

static void _show_fbtc_tdma(struct rtw89_dev *rtwdev, struct seq_file *m)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_btf_fwinfo *pfwinfo = &btc->fwinfo;
	struct rtw89_btc_rpt_cmn_info *pcinfo = NULL;
	struct rtw89_btc_fbtc_tdma *t = NULL;
	struct rtw89_btc_fbtc_slot *s = NULL;
	struct rtw89_btc_dm *dm = &btc->dm;
	u8 i, cnt = 0;

	pcinfo = &pfwinfo->rpt_fbtc_tdma.cinfo;
	if (!pcinfo->valid)
		return;

	t = &pfwinfo->rpt_fbtc_tdma.finfo;

	seq_printf(m,
		   " %-15s : ", "[tdma_policy]");
	seq_printf(m,
		   "type:%d, rx_flow_ctrl:%d, tx_pause:%d, ",
		   (u32)t->type,
		   t->rxflctrl, t->txpause);

	seq_printf(m,
		   "wl_toggle_n:%d, leak_n:%d, ext_ctrl:%d, ",
		   t->wtgle_n, t->leak_n, t->ext_ctrl);

	seq_printf(m,
		   "policy_type:%d",
		   (u32)btc->policy_type);

	s = pfwinfo->rpt_fbtc_slots.finfo.slot;

	for (i = 0; i < CXST_MAX; i++) {
		if (dm->update_slot_map == BIT(CXST_MAX) - 1)
			break;

		if (!(dm->update_slot_map & BIT(i)))
			continue;

		if (cnt % 6 == 0)
			seq_printf(m,
				   " %-15s : %d[%d/0x%x/%d]",
				   "[slot_policy]",
				   (u32)i,
				   s[i].dur, s[i].cxtbl, s[i].cxtype);
		else
			seq_printf(m,
				   ", %d[%d/0x%x/%d]",
				   (u32)i,
				   s[i].dur, s[i].cxtbl, s[i].cxtype);
		if (cnt % 6 == 5)
			seq_puts(m, "\n");
		cnt++;
	}
	seq_puts(m, "\n");
}

static void _show_fbtc_slots(struct rtw89_dev *rtwdev, struct seq_file *m)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_btf_fwinfo *pfwinfo = &btc->fwinfo;
	struct rtw89_btc_rpt_cmn_info *pcinfo = NULL;
	struct rtw89_btc_fbtc_slots *pslots = NULL;
	struct rtw89_btc_fbtc_slot s;
	u8 i = 0;

	pcinfo = &pfwinfo->rpt_fbtc_slots.cinfo;
	if (!pcinfo->valid)
		return;

	pslots = &pfwinfo->rpt_fbtc_slots.finfo;

	for (i = 0; i < CXST_MAX; i++) {
		s = pslots->slot[i];
		if (i % 6 == 0)
			seq_printf(m,
				   " %-15s : %02d[%03d/0x%x/%d]",
				   "[slot_list]",
				   (u32)i,
				   s.dur, s.cxtbl, s.cxtype);
		else
			seq_printf(m,
				   ", %02d[%03d/0x%x/%d]",
				   (u32)i,
				   s.dur, s.cxtbl, s.cxtype);
		if (i % 6 == 5)
			seq_puts(m, "\n");
	}
}

static void _show_fbtc_cysta(struct rtw89_dev *rtwdev, struct seq_file *m)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_btf_fwinfo *pfwinfo = &btc->fwinfo;
	struct rtw89_btc_dm *dm = &btc->dm;
	struct rtw89_btc_bt_a2dp_desc *a2dp = &btc->cx.bt.link_info.a2dp_desc;
	struct rtw89_btc_rpt_cmn_info *pcinfo = NULL;
	struct rtw89_btc_fbtc_cysta *pcysta_le32 = NULL;
	struct rtw89_btc_fbtc_cysta_cpu pcysta[1];
	union rtw89_btc_fbtc_rxflct r;
	u8 i, cnt = 0, slot_pair;
	u16 cycle, c_begin, c_end, store_index;

	pcinfo = &pfwinfo->rpt_fbtc_cysta.cinfo;
	if (!pcinfo->valid)
		return;

	pcysta_le32 = &pfwinfo->rpt_fbtc_cysta.finfo;
	rtw89_btc_fbtc_cysta_to_cpu(pcysta_le32, pcysta);
	seq_printf(m,
		   " %-15s : cycle:%d, bcn[all:%d/all_ok:%d/bt:%d/bt_ok:%d]",
		   "[cycle_cnt]", pcysta->cycles, pcysta->bcn_cnt[CXBCN_ALL],
		   pcysta->bcn_cnt[CXBCN_ALL_OK],
		   pcysta->bcn_cnt[CXBCN_BT_SLOT],
		   pcysta->bcn_cnt[CXBCN_BT_OK]);

	for (i = 0; i < CXST_MAX; i++) {
		if (!pcysta->slot_cnt[i])
			continue;
		seq_printf(m,
			   ", %d:%d", (u32)i, pcysta->slot_cnt[i]);
	}

	if (dm->tdma_now.rxflctrl) {
		seq_printf(m,
			   ", leak_rx:%d", pcysta->leakrx_cnt);
	}

	if (pcysta->collision_cnt) {
		seq_printf(m,
			   ", collision:%d", pcysta->collision_cnt);
	}

	if (pcysta->skip_cnt) {
		seq_printf(m,
			   ", skip:%d", pcysta->skip_cnt);
	}
	seq_puts(m, "\n");

	seq_printf(m, " %-15s : avg_t[wl:%d/bt:%d/lk:%d.%03d]",
		   "[cycle_time]",
		   pcysta->tavg_cycle[CXT_WL],
		   pcysta->tavg_cycle[CXT_BT],
		   pcysta->tavg_lk / 1000, pcysta->tavg_lk % 1000);
	seq_printf(m,
		   ", max_t[wl:%d/bt:%d/lk:%d.%03d]",
		   pcysta->tmax_cycle[CXT_WL],
		   pcysta->tmax_cycle[CXT_BT],
		   pcysta->tmax_lk / 1000, pcysta->tmax_lk % 1000);
	seq_printf(m,
		   ", maxdiff_t[wl:%d/bt:%d]\n",
		   pcysta->tmaxdiff_cycle[CXT_WL],
		   pcysta->tmaxdiff_cycle[CXT_BT]);

	if (pcysta->cycles == 0)
		return;

	/* 1 cycle record 1 wl-slot and 1 bt-slot */
	slot_pair = BTC_CYCLE_SLOT_MAX / 2;

	if (pcysta->cycles <= slot_pair)
		c_begin = 1;
	else
		c_begin = pcysta->cycles - slot_pair + 1;

	c_end = pcysta->cycles;

	for (cycle = c_begin; cycle <= c_end; cycle++) {
		cnt++;
		store_index = ((cycle - 1) % slot_pair) * 2;

		if (cnt % (BTC_CYCLE_SLOT_MAX / 4) == 1)
			seq_printf(m,
				   " %-15s : ->b%02d->w%02d", "[cycle_step]",
				   pcysta->tslot_cycle[store_index],
				   pcysta->tslot_cycle[store_index + 1]);
		else
			seq_printf(m,
				   "->b%02d->w%02d",
				   pcysta->tslot_cycle[store_index],
				   pcysta->tslot_cycle[store_index + 1]);
		if (cnt % (BTC_CYCLE_SLOT_MAX / 4) == 0 || cnt == c_end)
			seq_puts(m, "\n");
	}

	if (a2dp->exist) {
		seq_printf(m,
			   " %-15s : a2dp_ept:%d, a2dp_late:%d",
			   "[a2dp_t_sta]",
			   pcysta->a2dpept, pcysta->a2dpeptto);

		seq_printf(m,
			   ", avg_t:%d, max_t:%d",
			   pcysta->tavg_a2dpept, pcysta->tmax_a2dpept);
		r.val = dm->tdma_now.rxflctrl;

		if (r.type && r.tgln_n) {
			seq_printf(m,
				   ", cycle[PSTDMA:%d/TDMA:%d], ",
				   pcysta->cycles_a2dp[CXT_FLCTRL_ON],
				   pcysta->cycles_a2dp[CXT_FLCTRL_OFF]);

			seq_printf(m,
				   "avg_t[PSTDMA:%d/TDMA:%d], ",
				   pcysta->tavg_a2dp[CXT_FLCTRL_ON],
				   pcysta->tavg_a2dp[CXT_FLCTRL_OFF]);

			seq_printf(m,
				   "max_t[PSTDMA:%d/TDMA:%d]",
				   pcysta->tmax_a2dp[CXT_FLCTRL_ON],
				   pcysta->tmax_a2dp[CXT_FLCTRL_OFF]);
		}
		seq_puts(m, "\n");
	}
}

static void _show_fbtc_nullsta(struct rtw89_dev *rtwdev, struct seq_file *m)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_btf_fwinfo *pfwinfo = &btc->fwinfo;
	struct rtw89_btc_rpt_cmn_info *pcinfo = NULL;
	struct rtw89_btc_fbtc_cynullsta *ns = NULL;
	u8 i = 0;

	if (!btc->dm.tdma_now.rxflctrl)
		return;

	pcinfo = &pfwinfo->rpt_fbtc_nullsta.cinfo;
	if (!pcinfo->valid)
		return;

	ns = &pfwinfo->rpt_fbtc_nullsta.finfo;

	seq_printf(m, " %-15s : ", "[null_sta]");

	for (i = 0; i < 2; i++) {
		if (i != 0)
			seq_printf(m, ", null-%d", i);
		else
			seq_printf(m, "null-%d", i);
		seq_printf(m, "[ok:%d/", le32_to_cpu(ns->result[i][1]));
		seq_printf(m, "fail:%d/", le32_to_cpu(ns->result[i][0]));
		seq_printf(m, "on_time:%d/", le32_to_cpu(ns->result[i][2]));
		seq_printf(m, "retry:%d/", le32_to_cpu(ns->result[i][3]));
		seq_printf(m, "avg_t:%d.%03d/",
			   le32_to_cpu(ns->avg_t[i]) / 1000,
			   le32_to_cpu(ns->avg_t[i]) % 1000);
		seq_printf(m, "max_t:%d.%03d]",
			   le32_to_cpu(ns->max_t[i]) / 1000,
			   le32_to_cpu(ns->max_t[i]) % 1000);
	}
	seq_puts(m, "\n");
}

static void _show_fbtc_step(struct rtw89_dev *rtwdev, struct seq_file *m)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_btf_fwinfo *pfwinfo = &btc->fwinfo;
	struct rtw89_btc_rpt_cmn_info *pcinfo = NULL;
	struct rtw89_btc_fbtc_steps *pstep = NULL;
	u8 type, val, cnt = 0, state = 0;
	bool outloop = false;
	u16 i, diff_t, n_start = 0, n_stop = 0;
	u16 pos_old, pos_new;

	pcinfo = &pfwinfo->rpt_fbtc_step.cinfo;
	if (!pcinfo->valid)
		return;

	pstep = &pfwinfo->rpt_fbtc_step.finfo;
	pos_old = le16_to_cpu(pstep->pos_old);
	pos_new = le16_to_cpu(pstep->pos_new);

	if (pcinfo->req_fver != pstep->fver)
		return;

	/* store step info by using ring instead of FIFO*/
	do {
		switch (state) {
		case 0:
			n_start = pos_old;
			if (pos_new >=  pos_old)
				n_stop = pos_new;
			else
				n_stop = btc->ctrl.trace_step - 1;

			state = 1;
			break;
		case 1:
			for (i = n_start; i <= n_stop; i++) {
				type = pstep->step[i].type;
				val = pstep->step[i].val;
				diff_t = le16_to_cpu(pstep->step[i].difft);

				if (type == CXSTEP_NONE || type >= CXSTEP_MAX)
					continue;

				if (cnt % 10 == 0)
					seq_printf(m, " %-15s : ", "[steps]");

				seq_printf(m, "-> %s(%02d)(%02d)",
					   (type == CXSTEP_SLOT ? "SLT" :
					    "EVT"), (u32)val, diff_t);
				if (cnt % 10 == 9)
					seq_puts(m, "\n");
				cnt++;
			}

			state = 2;
			break;
		case 2:
			if (pos_new <  pos_old && n_start != 0) {
				n_start = 0;
				n_stop = pos_new;
				state = 1;
			} else {
				outloop = true;
			}
			break;
		}
	} while (!outloop);
}

static void _show_fw_dm_msg(struct rtw89_dev *rtwdev, struct seq_file *m)
{
	struct rtw89_btc *btc = &rtwdev->btc;

	if (!(btc->dm.coex_info_map & BTC_COEX_INFO_DM))
		return;

	_show_error(rtwdev, m);
	_show_fbtc_tdma(rtwdev, m);
	_show_fbtc_slots(rtwdev, m);
	_show_fbtc_cysta(rtwdev, m);
	_show_fbtc_nullsta(rtwdev, m);
	_show_fbtc_step(rtwdev, m);
}

static void _show_mreg(struct rtw89_dev *rtwdev, struct seq_file *m)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_btf_fwinfo *pfwinfo = &btc->fwinfo;
	struct rtw89_btc_rpt_cmn_info *pcinfo = NULL;
	struct rtw89_btc_fbtc_mreg_val *pmreg = NULL;
	struct rtw89_btc_fbtc_gpio_dbg *gdbg = NULL;
	struct rtw89_btc_cx *cx = &btc->cx;
	struct rtw89_btc_wl_info *wl = &btc->cx.wl;
	struct rtw89_btc_bt_info *bt = &btc->cx.bt;
	struct rtw89_mac_ax_gnt gnt[2] = {0};
	u8 i = 0, type = 0, cnt = 0;
	u32 val, offset;

	if (!(btc->dm.coex_info_map & BTC_COEX_INFO_MREG))
		return;

	seq_puts(m, "========== [HW Status] ==========\n");

	seq_printf(m,
		   " %-15s : WL->BT:0x%08x(cnt:%d), BT->WL:0x%08x(total:%d, bt_update:%d)\n",
		   "[scoreboard]", wl->scbd, cx->cnt_wl[BTC_WCNT_SCBDUPDATE],
		   bt->scbd, cx->cnt_bt[BTC_BCNT_SCBDREAD],
		   cx->cnt_bt[BTC_BCNT_SCBDUPDATE]);

	/* To avoid I/O if WL LPS or power-off  */
	if (!wl->status.map.lps && !wl->status.map.rf_off) {
		rtw89_mac_read_lte(rtwdev, R_AX_LTE_SW_CFG_1, &val);
		if (val & (B_AX_GNT_BT_RFC_S0_SW_VAL |
		    B_AX_GNT_BT_BB_S0_SW_VAL))
			gnt[0].gnt_bt = true;
		if (val & (B_AX_GNT_BT_RFC_S0_SW_CTRL |
		    B_AX_GNT_BT_BB_S0_SW_CTRL))
			gnt[0].gnt_bt_sw_en = true;
		if (val & (B_AX_GNT_WL_RFC_S0_SW_VAL |
		    B_AX_GNT_WL_BB_S0_SW_VAL))
			gnt[0].gnt_wl = true;
		if (val & (B_AX_GNT_WL_RFC_S0_SW_CTRL |
		    B_AX_GNT_WL_BB_S0_SW_CTRL))
			gnt[0].gnt_wl_sw_en = true;

		if (val & (B_AX_GNT_BT_RFC_S1_SW_VAL |
		    B_AX_GNT_BT_BB_S1_SW_VAL))
			gnt[1].gnt_bt = true;
		if (val & (B_AX_GNT_BT_RFC_S1_SW_CTRL |
		    B_AX_GNT_BT_BB_S1_SW_CTRL))
			gnt[1].gnt_bt_sw_en = true;
		if (val & (B_AX_GNT_WL_RFC_S1_SW_VAL |
		    B_AX_GNT_WL_BB_S1_SW_VAL))
			gnt[1].gnt_wl = true;
		if (val & (B_AX_GNT_WL_RFC_S1_SW_CTRL |
		    B_AX_GNT_WL_BB_S1_SW_CTRL))
			gnt[1].gnt_wl_sw_en = true;

		seq_printf(m,
			   " %-15s : pta_owner:%s, phy-0[gnt_wl:%s-%d/gnt_bt:%s-%d], ",
			   "[gnt_status]",
			   (rtw89_mac_get_ctrl_path(rtwdev) ? "WL" : "BT"),
			   (gnt[0].gnt_wl_sw_en ? "SW" : "HW"), gnt[0].gnt_wl,
			   (gnt[0].gnt_bt_sw_en ? "SW" : "HW"), gnt[0].gnt_bt);

		seq_printf(m, "phy-1[gnt_wl:%s-%d/gnt_bt:%s-%d]\n",
			   (gnt[1].gnt_wl_sw_en ? "SW" : "HW"), gnt[1].gnt_wl,
			   (gnt[1].gnt_bt_sw_en ? "SW" : "HW"), gnt[1].gnt_bt);
	}

	pcinfo = &pfwinfo->rpt_fbtc_mregval.cinfo;
	if (!pcinfo->valid) {
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s(): stop due rpt_fbtc_mregval.cinfo\n",
			    __func__);
		return;
	}

	pmreg = &pfwinfo->rpt_fbtc_mregval.finfo;
	rtw89_debug(rtwdev, RTW89_DBG_BTC,
		    "[BTC], %s(): rpt_fbtc_mregval reg_num = %d\n",
		    __func__, pmreg->reg_num);

	for (i = 0; i < pmreg->reg_num; i++) {
		type = (u8)le16_to_cpu(chip->mon_reg[i].type);
		offset = le32_to_cpu(chip->mon_reg[i].offset);
		val = le32_to_cpu(pmreg->mreg_val[i]);

		if (cnt % 6 == 0)
			seq_printf(m, " %-15s : %d_0x%04x=0x%08x",
				   "[reg]", (u32)type, offset, val);
		else
			seq_printf(m, ", %d_0x%04x=0x%08x", (u32)type,
				   offset, val);
		if (cnt % 6 == 5)
			seq_puts(m, "\n");
		cnt++;
	}

	pcinfo = &pfwinfo->rpt_fbtc_gpio_dbg.cinfo;
	if (!pcinfo->valid) {
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "[BTC], %s(): stop due rpt_fbtc_gpio_dbg.cinfo\n",
			    __func__);
		return;
	}

	gdbg = &pfwinfo->rpt_fbtc_gpio_dbg.finfo;
	if (!gdbg->en_map)
		return;

	seq_printf(m, " %-15s : enable_map:0x%08x",
		   "[gpio_dbg]", gdbg->en_map);

	for (i = 0; i < BTC_DBG_MAX1; i++) {
		if (!(gdbg->en_map & BIT(i)))
			continue;
		seq_printf(m, ", %d->GPIO%d", (u32)i, gdbg->gpio_map[i]);
	}
	seq_puts(m, "\n");
}

static void _show_summary(struct rtw89_dev *rtwdev, struct seq_file *m)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_btf_fwinfo *pfwinfo = &btc->fwinfo;
	struct rtw89_btc_rpt_cmn_info *pcinfo = NULL;
	struct rtw89_btc_fbtc_rpt_ctrl *prptctrl = NULL;
	struct rtw89_btc_cx *cx = &btc->cx;
	struct rtw89_btc_dm *dm = &btc->dm;
	struct rtw89_btc_wl_info *wl = &cx->wl;
	struct rtw89_btc_bt_info *bt = &cx->bt;
	u32 cnt_sum = 0, *cnt = btc->dm.cnt_notify;
	u8 i;

	if (!(dm->coex_info_map & BTC_COEX_INFO_SUMMARY))
		return;

	seq_puts(m, "========== [Statistics] ==========\n");

	pcinfo = &pfwinfo->rpt_ctrl.cinfo;
	if (pcinfo->valid && !wl->status.map.lps && !wl->status.map.rf_off) {
		prptctrl = &pfwinfo->rpt_ctrl.finfo;

		seq_printf(m,
			   " %-15s : h2c_cnt=%d(fail:%d, fw_recv:%d), c2h_cnt=%d(fw_send:%d), ",
			   "[summary]", pfwinfo->cnt_h2c,
			   pfwinfo->cnt_h2c_fail, prptctrl->h2c_cnt,
			   pfwinfo->cnt_c2h, prptctrl->c2h_cnt);

		seq_printf(m,
			   "rpt_cnt=%d(fw_send:%d), rpt_map=0x%x, dm_error_map:0x%x",
			   pfwinfo->event[BTF_EVNT_RPT], prptctrl->rpt_cnt,
			   prptctrl->rpt_enable, dm->error.val);

		if (dm->error.map.wl_fw_hang)
			seq_puts(m, " (WL FW Hang!!)");
		seq_puts(m, "\n");
		seq_printf(m,
			   " %-15s : send_ok:%d, send_fail:%d, recv:%d",
			   "[mailbox]", prptctrl->mb_send_ok_cnt,
			   prptctrl->mb_send_fail_cnt, prptctrl->mb_recv_cnt);

		seq_printf(m,
			   "(A2DP_empty:%d, A2DP_flowstop:%d, A2DP_full:%d)\n",
			   prptctrl->mb_a2dp_empty_cnt,
			   prptctrl->mb_a2dp_flct_cnt,
			   prptctrl->mb_a2dp_full_cnt);

		seq_printf(m,
			   " %-15s : wl_rfk[req:%d/go:%d/reject:%d/timeout:%d]",
			   "[RFK]", cx->cnt_wl[BTC_WCNT_RFK_REQ],
			   cx->cnt_wl[BTC_WCNT_RFK_GO],
			   cx->cnt_wl[BTC_WCNT_RFK_REJECT],
			   cx->cnt_wl[BTC_WCNT_RFK_TIMEOUT]);

		seq_printf(m,
			   ", bt_rfk[req:%d/go:%d/reject:%d/timeout:%d/fail:%d]\n",
			   prptctrl->bt_rfk_cnt[BTC_BCNT_RFK_REQ],
			   prptctrl->bt_rfk_cnt[BTC_BCNT_RFK_GO],
			   prptctrl->bt_rfk_cnt[BTC_BCNT_RFK_REJECT],
			   prptctrl->bt_rfk_cnt[BTC_BCNT_RFK_TIMEOUT],
			   prptctrl->bt_rfk_cnt[BTC_BCNT_RFK_FAIL]);

		if (prptctrl->bt_rfk_cnt[BTC_BCNT_RFK_TIMEOUT] > 0)
			bt->rfk_info.map.timeout = 1;
		else
			bt->rfk_info.map.timeout = 0;

		dm->error.map.wl_rfk_timeout = bt->rfk_info.map.timeout;
	} else {
		seq_printf(m,
			   " %-15s : h2c_cnt=%d(fail:%d), c2h_cnt=%d, rpt_cnt=%d, rpt_map=0x%x",
			   "[summary]", pfwinfo->cnt_h2c,
			   pfwinfo->cnt_h2c_fail, pfwinfo->cnt_c2h,
			   pfwinfo->event[BTF_EVNT_RPT],
			   btc->fwinfo.rpt_en_map);
		seq_puts(m, " (WL FW report invalid!!)\n");
	}

	for (i = 0; i < BTC_NCNT_NUM; i++)
		cnt_sum += dm->cnt_notify[i];

	seq_printf(m,
		   " %-15s : total=%d, show_coex_info=%d, power_on=%d, init_coex=%d, ",
		   "[notify_cnt]", cnt_sum, cnt[BTC_NCNT_SHOW_COEX_INFO],
		   cnt[BTC_NCNT_POWER_ON], cnt[BTC_NCNT_INIT_COEX]);

	seq_printf(m,
		   "power_off=%d, radio_state=%d, role_info=%d, wl_rfk=%d, wl_sta=%d\n",
		   cnt[BTC_NCNT_POWER_OFF], cnt[BTC_NCNT_RADIO_STATE],
		   cnt[BTC_NCNT_ROLE_INFO], cnt[BTC_NCNT_WL_RFK],
		   cnt[BTC_NCNT_WL_STA]);

	seq_printf(m,
		   " %-15s : scan_start=%d, scan_finish=%d, switch_band=%d, special_pkt=%d, ",
		   "[notify_cnt]", cnt[BTC_NCNT_SCAN_START],
		   cnt[BTC_NCNT_SCAN_FINISH], cnt[BTC_NCNT_SWITCH_BAND],
		   cnt[BTC_NCNT_SPECIAL_PACKET]);

	seq_printf(m,
		   "timer=%d, control=%d, customerize=%d\n",
		   cnt[BTC_NCNT_TIMER], cnt[BTC_NCNT_CONTROL],
		   cnt[BTC_NCNT_CUSTOMERIZE]);
}

void rtw89_btc_dump_info(struct rtw89_dev *rtwdev, struct seq_file *m)
{
	struct rtw89_fw_suit *fw_suit = &rtwdev->fw.normal;
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_cx *cx = &btc->cx;
	struct rtw89_btc_bt_info *bt = &cx->bt;

	seq_puts(m, "=========================================\n");
	seq_printf(m, "WL FW / BT FW		%d.%d.%d.%d / NA\n",
		   fw_suit->major_ver, fw_suit->minor_ver,
		   fw_suit->sub_ver, fw_suit->sub_idex);
	seq_printf(m, "manual			%d\n", btc->ctrl.manual);

	seq_puts(m, "=========================================\n");

	seq_printf(m, "\n\r %-15s : raw_data[%02x %02x %02x %02x %02x %02x] (type:%s/cnt:%d/same:%d)",
		   "[bt_info]",
		   bt->raw_info[2], bt->raw_info[3],
		   bt->raw_info[4], bt->raw_info[5],
		   bt->raw_info[6], bt->raw_info[7],
		   bt->raw_info[0] == BTC_BTINFO_AUTO ? "auto" : "reply",
		   cx->cnt_bt[BTC_BCNT_INFOUPDATE],
		   cx->cnt_bt[BTC_BCNT_INFOSAME]);

	seq_puts(m, "\n=========================================\n");

	_show_cx_info(rtwdev, m);
	_show_wl_info(rtwdev, m);
	_show_bt_info(rtwdev, m);
	_show_dm_info(rtwdev, m);
	_show_fw_dm_msg(rtwdev, m);
	_show_mreg(rtwdev, m);
	_show_summary(rtwdev, m);
}
