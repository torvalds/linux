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
#ifndef __RTW_MLME_EXT_H_
#define __RTW_MLME_EXT_H_


/*	Commented by Albert 20101105
 *	Increase the SURVEY_TO value from 100 to 150  ( 100ms to 150ms )
 *	The Realtek 8188CE SoftAP will spend around 100ms to send the probe response after receiving the probe request.
 *	So, this driver tried to extend the dwell time for each scanning channel.
 *	This will increase the chance to receive the probe response from SoftAP. */

#define SURVEY_TO		(100)
#define REAUTH_TO		(300) /* (50) */
#define REASSOC_TO		(300) /* (50) */
/* #define DISCONNECT_TO	(3000) */
#define ADDBA_TO			(2000)

#define LINKED_TO (1) /* unit:2 sec, 1x2 = 2 sec */

#define REAUTH_LIMIT	(4)
#define REASSOC_LIMIT	(4)
#define READDBA_LIMIT	(2)

#ifdef CONFIG_GSPI_HCI
	#define ROAMING_LIMIT	5
#else
	#define ROAMING_LIMIT	8
#endif
/* #define	IOCMD_REG0		0x10250370 */
/* #define	IOCMD_REG1		0x10250374 */
/* #define	IOCMD_REG2		0x10250378 */

/* #define	FW_DYNAMIC_FUN_SWITCH	0x10250364 */

/* #define	WRITE_BB_CMD		0xF0000001 */
/* #define	SET_CHANNEL_CMD	0xF3000000 */
/* #define	UPDATE_RA_CMD	0xFD0000A2 */

#define _HW_STATE_NOLINK_		0x00
#define _HW_STATE_ADHOC_		0x01
#define _HW_STATE_STATION_	0x02
#define _HW_STATE_AP_			0x03
#define _HW_STATE_MONITOR_ 0x04


#define		_1M_RATE_	0
#define		_2M_RATE_	1
#define		_5M_RATE_	2
#define		_11M_RATE_	3
#define		_6M_RATE_	4
#define		_9M_RATE_	5
#define		_12M_RATE_	6
#define		_18M_RATE_	7
#define		_24M_RATE_	8
#define		_36M_RATE_	9
#define		_48M_RATE_	10
#define		_54M_RATE_	11

/********************************************************
MCS rate definitions
*********************************************************/
#define MCS_RATE_1R	(0x000000ff)
#define MCS_RATE_2R	(0x0000ffff)
#define MCS_RATE_3R	(0x00ffffff)
#define MCS_RATE_4R	(0xffffffff)
#define MCS_RATE_2R_13TO15_OFF	(0x00001fff)


extern unsigned char RTW_WPA_OUI[];
extern unsigned char WMM_OUI[];
extern unsigned char WPS_OUI[];
extern unsigned char WFD_OUI[];
extern unsigned char P2P_OUI[];

extern unsigned char WMM_INFO_OUI[];
extern unsigned char WMM_PARA_OUI[];

typedef enum _RT_CHANNEL_DOMAIN {
	/* ===== 0x00 ~ 0x1F, legacy channel plan ===== */
	RTW_CHPLAN_FCC = 0x00,
	RTW_CHPLAN_IC = 0x01,
	RTW_CHPLAN_ETSI = 0x02,
	RTW_CHPLAN_SPAIN = 0x03,
	RTW_CHPLAN_FRANCE = 0x04,
	RTW_CHPLAN_MKK = 0x05,
	RTW_CHPLAN_MKK1 = 0x06,
	RTW_CHPLAN_ISRAEL = 0x07,
	RTW_CHPLAN_TELEC = 0x08,
	RTW_CHPLAN_GLOBAL_DOAMIN = 0x09,
	RTW_CHPLAN_WORLD_WIDE_13 = 0x0A,
	RTW_CHPLAN_TAIWAN = 0x0B,
	RTW_CHPLAN_CHINA = 0x0C,
	RTW_CHPLAN_SINGAPORE_INDIA_MEXICO = 0x0D,
	RTW_CHPLAN_KOREA = 0x0E,
	RTW_CHPLAN_TURKEY = 0x0F,
	RTW_CHPLAN_JAPAN = 0x10,
	RTW_CHPLAN_FCC_NO_DFS = 0x11,
	RTW_CHPLAN_JAPAN_NO_DFS = 0x12,
	RTW_CHPLAN_WORLD_WIDE_5G = 0x13,
	RTW_CHPLAN_TAIWAN_NO_DFS = 0x14,

	/* ===== 0x20 ~ 0x7F, new channel plan ===== */
	RTW_CHPLAN_WORLD_NULL = 0x20,
	RTW_CHPLAN_ETSI1_NULL = 0x21,
	RTW_CHPLAN_FCC1_NULL = 0x22,
	RTW_CHPLAN_MKK1_NULL = 0x23,
	RTW_CHPLAN_ETSI2_NULL = 0x24,
	RTW_CHPLAN_FCC1_FCC1 = 0x25,
	RTW_CHPLAN_WORLD_ETSI1 = 0x26,
	RTW_CHPLAN_MKK1_MKK1 = 0x27,
	RTW_CHPLAN_WORLD_KCC1 = 0x28,
	RTW_CHPLAN_WORLD_FCC2 = 0x29,
	RTW_CHPLAN_FCC2_NULL = 0x2A,
	RTW_CHPLAN_WORLD_FCC3 = 0x30,
	RTW_CHPLAN_WORLD_FCC4 = 0x31,
	RTW_CHPLAN_WORLD_FCC5 = 0x32,
	RTW_CHPLAN_WORLD_FCC6 = 0x33,
	RTW_CHPLAN_FCC1_FCC7 = 0x34,
	RTW_CHPLAN_WORLD_ETSI2 = 0x35,
	RTW_CHPLAN_WORLD_ETSI3 = 0x36,
	RTW_CHPLAN_MKK1_MKK2 = 0x37,
	RTW_CHPLAN_MKK1_MKK3 = 0x38,
	RTW_CHPLAN_FCC1_NCC1 = 0x39,
	RTW_CHPLAN_FCC1_NCC2 = 0x40,
	RTW_CHPLAN_GLOBAL_NULL = 0x41,
	RTW_CHPLAN_ETSI1_ETSI4 = 0x42,
	RTW_CHPLAN_FCC1_FCC2 = 0x43,
	RTW_CHPLAN_FCC1_NCC3 = 0x44,
	RTW_CHPLAN_WORLD_ETSI5 = 0x45,
	RTW_CHPLAN_FCC1_FCC8 = 0x46,
	RTW_CHPLAN_WORLD_ETSI6 = 0x47,
	RTW_CHPLAN_WORLD_ETSI7 = 0x48,
	RTW_CHPLAN_WORLD_ETSI8 = 0x49,
	RTW_CHPLAN_WORLD_ETSI9 = 0x50,
	RTW_CHPLAN_WORLD_ETSI10 = 0x51,
	RTW_CHPLAN_WORLD_ETSI11 = 0x52,
	RTW_CHPLAN_FCC1_NCC4 = 0x53,
	RTW_CHPLAN_WORLD_ETSI12 = 0x54,
	RTW_CHPLAN_FCC1_FCC9 = 0x55,
	RTW_CHPLAN_WORLD_ETSI13 = 0x56,
	RTW_CHPLAN_FCC1_FCC10 = 0x57,
	RTW_CHPLAN_MKK2_MKK4 = 0x58,
	RTW_CHPLAN_WORLD_ETSI14 = 0x59,
	RTW_CHPLAN_FCC1_FCC5 = 0x60,
	RTW_CHPLAN_FCC2_FCC7 = 0x61,
	RTW_CHPLAN_FCC2_FCC1 = 0x62,

	RTW_CHPLAN_MAX,
	RTW_CHPLAN_REALTEK_DEFINE = 0x7F,
} RT_CHANNEL_DOMAIN, *PRT_CHANNEL_DOMAIN;

typedef enum _RT_CHANNEL_DOMAIN_2G {
	RTW_RD_2G_NULL = 0,
	RTW_RD_2G_WORLD = 1,	/* Worldwird 13 */
	RTW_RD_2G_ETSI1 = 2,	/* Europe */
	RTW_RD_2G_FCC1 = 3,		/* US */
	RTW_RD_2G_MKK1 = 4,		/* Japan */
	RTW_RD_2G_ETSI2 = 5,	/* France */
	RTW_RD_2G_GLOBAL = 6,	/* Global domain */
	RTW_RD_2G_MKK2 = 7,		/* Japan */
	RTW_RD_2G_FCC2 = 8,		/* US */

	RTW_RD_2G_MAX,
} RT_CHANNEL_DOMAIN_2G, *PRT_CHANNEL_DOMAIN_2G;

typedef enum _RT_CHANNEL_DOMAIN_5G {
	RTW_RD_5G_NULL = 0,		/*	*/
	RTW_RD_5G_ETSI1 = 1,	/* Europe */
	RTW_RD_5G_ETSI2 = 2,	/* Australia, New Zealand */
	RTW_RD_5G_ETSI3 = 3,	/* Russia */
	RTW_RD_5G_FCC1 = 4,		/* US */
	RTW_RD_5G_FCC2 = 5,		/* FCC w/o DFS Channels */
	RTW_RD_5G_FCC3 = 6,		/* Bolivia, Chile, El Salvador, Venezuela */
	RTW_RD_5G_FCC4 = 7,		/* Venezuela */
	RTW_RD_5G_FCC5 = 8,		/* China */
	RTW_RD_5G_FCC6 = 9,		/*	*/
	RTW_RD_5G_FCC7 = 10,	/* US Canada(w/o Weather radar) */
	RTW_RD_5G_KCC1 = 11,	/* Korea */
	RTW_RD_5G_MKK1 = 12,	/* Japan */
	RTW_RD_5G_MKK2 = 13,	/* Japan (W52, W53) */
	RTW_RD_5G_MKK3 = 14,	/* Japan (W56) */
	RTW_RD_5G_NCC1 = 15,	/* Taiwan, (w/o Weather radar) */
	RTW_RD_5G_NCC2 = 16,	/* Taiwan, Band2, Band4 */
	RTW_RD_5G_NCC3 = 17,	/* Taiwan w/o DFS, Band4 only */
	RTW_RD_5G_ETSI4 = 18,	/* Europe w/o DFS, Band1 only */
	RTW_RD_5G_ETSI5 = 19,	/* Australia, New Zealand(w/o Weather radar) */
	RTW_RD_5G_FCC8 = 20,	/* Latin America */
	RTW_RD_5G_ETSI6 = 21,	/* Israel, Bahrain, Egypt, India, China, Malaysia */
	RTW_RD_5G_ETSI7 = 22,	/* China */
	RTW_RD_5G_ETSI8 = 23,	/* Jordan */
	RTW_RD_5G_ETSI9 = 24,	/* Lebanon */
	RTW_RD_5G_ETSI10 = 25,	/* Qatar */
	RTW_RD_5G_ETSI11 = 26,	/* Russia */
	RTW_RD_5G_NCC4 = 27,	/* Taiwan, (w/o Weather radar) */
	RTW_RD_5G_ETSI12 = 28,	/* Indonesia */
	RTW_RD_5G_FCC9 = 29,	/* (w/o Weather radar) */
	RTW_RD_5G_ETSI13 = 30,	/* (w/o Weather radar) */
	RTW_RD_5G_FCC10 = 31,	/* Argentina(w/o Weather radar) */
	RTW_RD_5G_MKK4 = 32,	/* Japan (W52) */
	RTW_RD_5G_ETSI14 = 33,	/* Russia */
	RTW_RD_5G_FCC11 = 34,	/* US(include CH144) */

	/* === Below are driver defined for legacy channel plan compatible, DON'T assign index ==== */
	RTW_RD_5G_OLD_FCC1,
	RTW_RD_5G_OLD_NCC1,
	RTW_RD_5G_OLD_KCC1,

	RTW_RD_5G_MAX,
} RT_CHANNEL_DOMAIN_5G, *PRT_CHANNEL_DOMAIN_5G;

bool rtw_chplan_is_empty(u8 id);
#define rtw_is_channel_plan_valid(chplan) (((chplan) < RTW_CHPLAN_MAX || (chplan) == RTW_CHPLAN_REALTEK_DEFINE) && !rtw_chplan_is_empty(chplan))
#define rtw_is_legacy_channel_plan(chplan) ((chplan) < 0x20)

typedef struct _RT_CHANNEL_PLAN {
	unsigned char	Channel[MAX_CHANNEL_NUM];
	unsigned char	Len;
} RT_CHANNEL_PLAN, *PRT_CHANNEL_PLAN;

struct ch_list_t {
	u8 *len_ch;
};

#define CH_LIST_ENT(_len, arg...) \
	{.len_ch = (u8[_len + 1]) {_len, ##arg}, }

#define CH_LIST_LEN(_ch_list) (_ch_list.len_ch[0])
#define CH_LIST_CH(_ch_list, _i) (_ch_list.len_ch[_i + 1])

typedef struct _RT_CHANNEL_PLAN_MAP {
	u8 Index2G;
#ifdef CONFIG_IEEE80211_BAND_5GHZ
	u8 Index5G;
#endif
	u8 regd; /* value of REGULATION_TXPWR_LMT */
} RT_CHANNEL_PLAN_MAP, *PRT_CHANNEL_PLAN_MAP;

#ifdef CONFIG_IEEE80211_BAND_5GHZ
#define CHPLAN_ENT(i2g, i5g, regd) {i2g, i5g, regd}
#else
#define CHPLAN_ENT(i2g, i5g, regd) {i2g, regd}
#endif

enum Associated_AP {
	atherosAP	= 0,
	broadcomAP	= 1,
	ciscoAP		= 2,
	marvellAP	= 3,
	ralinkAP	= 4,
	realtekAP	= 5,
	airgocapAP	= 6,
	unknownAP	= 7,
	maxAP,
};

typedef enum _HT_IOT_PEER {
	HT_IOT_PEER_UNKNOWN			= 0,
	HT_IOT_PEER_REALTEK			= 1,
	HT_IOT_PEER_REALTEK_92SE		= 2,
	HT_IOT_PEER_BROADCOM		= 3,
	HT_IOT_PEER_RALINK			= 4,
	HT_IOT_PEER_ATHEROS			= 5,
	HT_IOT_PEER_CISCO				= 6,
	HT_IOT_PEER_MERU				= 7,
	HT_IOT_PEER_MARVELL			= 8,
	HT_IOT_PEER_REALTEK_SOFTAP 	= 9,/* peer is RealTek SOFT_AP, by Bohn, 2009.12.17 */
	HT_IOT_PEER_SELF_SOFTAP 		= 10, /* Self is SoftAP */
	HT_IOT_PEER_AIRGO				= 11,
	HT_IOT_PEER_INTEL				= 12,
	HT_IOT_PEER_RTK_APCLIENT		= 13,
	HT_IOT_PEER_REALTEK_81XX		= 14,
	HT_IOT_PEER_REALTEK_WOW		= 15,
	HT_IOT_PEER_REALTEK_JAGUAR_BCUTAP = 16,
	HT_IOT_PEER_REALTEK_JAGUAR_CCUTAP = 17,
	HT_IOT_PEER_MAX				= 18
} HT_IOT_PEER_E, *PHTIOT_PEER_E;

struct mlme_handler {
	unsigned int   num;
	char *str;
	unsigned int (*func)(_adapter *padapter, union recv_frame *precv_frame);
};

struct action_handler {
	unsigned int   num;
	char *str;
	unsigned int (*func)(_adapter *padapter, union recv_frame *precv_frame);
};

enum SCAN_STATE {
	SCAN_DISABLE = 0,
	SCAN_START = 1,
	SCAN_PS_ANNC_WAIT = 2,
	SCAN_ENTER = 3,
	SCAN_PROCESS = 4,

	/* backop */
	SCAN_BACKING_OP = 5,
	SCAN_BACK_OP = 6,
	SCAN_LEAVING_OP = 7,
	SCAN_LEAVE_OP = 8,

	/* SW antenna diversity (before linked) */
	SCAN_SW_ANTDIV_BL = 9,

	/* legacy p2p */
	SCAN_TO_P2P_LISTEN = 10,
	SCAN_P2P_LISTEN = 11,

	SCAN_COMPLETE = 12,
	SCAN_STATE_MAX,
};

const char *scan_state_str(u8 state);

enum ss_backop_flag {
	SS_BACKOP_EN = BIT0, /* backop when linked */
	SS_BACKOP_EN_NL = BIT1, /* backop even when no linked */

	SS_BACKOP_PS_ANNC = BIT4,
	SS_BACKOP_TX_RESUME = BIT5,
};

struct ss_res {
	u8 state;
	u8 next_state; /* will set to state on next cmd hdl */
	int	bss_cnt;
	int	channel_idx;
	int	scan_mode;
	u16 scan_ch_ms;
	u8 rx_ampdu_accept;
	u8 rx_ampdu_size;
	u8 igi_scan;
	u8 igi_before_scan; /* used for restoring IGI value without enable DIG & FA_CNT */
#ifdef CONFIG_SCAN_BACKOP
	u8 backop_flags_sta; /* policy for station mode*/
	u8 backop_flags_ap; /* policy for ap mode */
	u8 backop_flags; /* per backop runtime decision */
	u8 scan_cnt;
	u8 scan_cnt_max;
	u32 backop_time; /* the start time of backop */
	u16 backop_ms;
#endif
#if defined(CONFIG_ANTENNA_DIVERSITY) || defined(DBG_SCAN_SW_ANTDIV_BL)
	u8 is_sw_antdiv_bl_scan;
#endif
	u8 ssid_num;
	u8 ch_num;
	NDIS_802_11_SSID ssid[RTW_SSID_SCAN_AMOUNT];
	struct rtw_ieee80211_channel ch[RTW_CHANNEL_SCAN_AMOUNT];
};

/* #define AP_MODE				0x0C */
/* #define STATION_MODE	0x08 */
/* #define AD_HOC_MODE		0x04 */
/* #define NO_LINK_MODE	0x00 */

#define	WIFI_FW_NULL_STATE			_HW_STATE_NOLINK_
#define	WIFI_FW_STATION_STATE		_HW_STATE_STATION_
#define	WIFI_FW_AP_STATE				_HW_STATE_AP_
#define	WIFI_FW_ADHOC_STATE			_HW_STATE_ADHOC_

#define WIFI_FW_PRE_LINK			0x00000800
#define	WIFI_FW_AUTH_NULL			0x00000100
#define	WIFI_FW_AUTH_STATE			0x00000200
#define	WIFI_FW_AUTH_SUCCESS			0x00000400

#define	WIFI_FW_ASSOC_STATE			0x00002000
#define	WIFI_FW_ASSOC_SUCCESS		0x00004000

#define	WIFI_FW_LINKING_STATE		(WIFI_FW_AUTH_NULL | WIFI_FW_AUTH_STATE | WIFI_FW_AUTH_SUCCESS | WIFI_FW_ASSOC_STATE)

#ifdef CONFIG_TDLS
enum TDLS_option {
	TDLS_ESTABLISHED = 1,
	TDLS_ISSUE_PTI,
	TDLS_CH_SW_RESP,
	TDLS_CH_SW_PREPARE,
	TDLS_CH_SW_START,
	TDLS_CH_SW_TO_OFF_CHNL,
	TDLS_CH_SW_TO_BASE_CHNL_UNSOLICITED,
	TDLS_CH_SW_TO_BASE_CHNL,
	TDLS_CH_SW_END_TO_BASE_CHNL,
	TDLS_CH_SW_END,
	TDLS_RS_RCR,
	TDLS_TEARDOWN_STA,
	TDLS_TEARDOWN_STA_LOCALLY,
	maxTDLS,
};

#endif /* CONFIG_TDLS */

/*
 * Usage:
 * When one iface acted as AP mode and the other iface is STA mode and scanning,
 * it should switch back to AP's operating channel periodically.
 * Parameters info:
 * When the driver scanned RTW_SCAN_NUM_OF_CH channels, it would switch back to AP's operating channel for
 * RTW_BACK_OP_CH_MS milliseconds.
 * Example:
 * For chip supports 2.4G + 5GHz and AP mode is operating in channel 1,
 * RTW_SCAN_NUM_OF_CH is 8, RTW_BACK_OP_CH_MS is 300
 * When it's STA mode gets set_scan command,
 * it would
 * 1. Doing the scan on channel 1.2.3.4.5.6.7.8
 * 2. Back to channel 1 for 300 milliseconds
 * 3. Go through doing site survey on channel 9.10.11.36.40.44.48.52
 * 4. Back to channel 1 for 300 milliseconds
 * 5. ... and so on, till survey done.
 */
#if defined(CONFIG_ATMEL_RC_PATCH)
	#define RTW_SCAN_NUM_OF_CH 2
	#define RTW_BACK_OP_CH_MS 200
#else
	#define RTW_SCAN_NUM_OF_CH 3
	#define RTW_BACK_OP_CH_MS 400
#endif

struct mlme_ext_info {
	u32	state;
#ifdef CONFIG_MI_WITH_MBSSID_CAM
	u8	hw_media_state;
#endif
	u32	reauth_count;
	u32	reassoc_count;
	u32	link_count;
	u32	auth_seq;
	u32	auth_algo;	/* 802.11 auth, could be open, shared, auto */
	u32	authModeToggle;
	u32	enc_algo;/* encrypt algorithm; */
	u32	key_index;	/* this is only valid for legendary wep, 0~3 for key id. */
	u32	iv;
	u8	chg_txt[128];
	u16	aid;
	u16	bcn_interval;
	u16	capability;
	u8	assoc_AP_vendor;
	u8	slotTime;
	u8	preamble_mode;
	u8	WMM_enable;
	u8	ERP_enable;
	u8	ERP_IE;
	u8	HT_enable;
	u8	HT_caps_enable;
	u8	HT_info_enable;
	u8	HT_protection;
	u8	turboMode_cts2self;
	u8	turboMode_rtsen;
	u8	SM_PS;
	u8	agg_enable_bitmap;
	u8	ADDBA_retry_count;
	u8	candidate_tid_bitmap;
	u8	dialogToken;
	/* Accept ADDBA Request */
	BOOLEAN bAcceptAddbaReq;
	u8	bwmode_updated;
	u8	hidden_ssid_mode;
	u8	VHT_enable;

	struct ADDBA_request		ADDBA_req;
	struct WMM_para_element	WMM_param;
	struct HT_caps_element	HT_caps;
	struct HT_info_element		HT_info;
	WLAN_BSSID_EX			network;/* join network or bss_network, if in ap mode, it is the same to cur_network.network */
};

/* The channel information about this channel including joining, scanning, and power constraints. */
typedef struct _RT_CHANNEL_INFO {
	u8				ChannelNum;		/* The channel number. */
	RT_SCAN_TYPE	ScanType;		/* Scan type such as passive or active scan. */
	/* u16				ScanPeriod;		 */ /* Listen time in millisecond in this channel. */
	/* s32				MaxTxPwrDbm;	 */ /* Max allowed tx power. */
	/* u32				ExInfo;			 */ /* Extended Information for this channel. */
#ifdef CONFIG_FIND_BEST_CHANNEL
	u32				rx_count;
#endif
#ifdef CONFIG_DFS_MASTER
	u32 non_ocp_end_time;
#endif
} RT_CHANNEL_INFO, *PRT_CHANNEL_INFO;

#define DFS_MASTER_TIMER_MS 100
#define CAC_TIME_MS (60*1000)
#define CAC_TIME_CE_MS (10*60*1000)
#define NON_OCP_TIME_MS (30*60*1000)

void rtw_rfctl_init(_adapter *adapter);

#ifdef CONFIG_DFS_MASTER
struct rf_ctl_t;
#define CH_IS_NON_OCP(rt_ch_info) (time_after((unsigned long)(rt_ch_info)->non_ocp_end_time, (unsigned long)rtw_get_current_time()))
bool rtw_is_cac_reset_needed(_adapter *adapter, u8 ch, u8 bw, u8 offset);
bool _rtw_rfctl_overlap_radar_detect_ch(struct rf_ctl_t *rfctl, u8 ch, u8 bw, u8 offset);
bool rtw_rfctl_overlap_radar_detect_ch(struct rf_ctl_t *rfctl);
bool rtw_rfctl_is_tx_blocked_by_ch_waiting(struct rf_ctl_t *rfctl);
bool rtw_chset_is_ch_non_ocp(RT_CHANNEL_INFO *ch_set, u8 ch, u8 bw, u8 offset);
void rtw_chset_update_non_ocp(RT_CHANNEL_INFO *ch_set, u8 ch, u8 bw, u8 offset);
void rtw_chset_update_non_ocp_ms(RT_CHANNEL_INFO *ch_set, u8 ch, u8 bw, u8 offset, int ms);
u32 rtw_get_ch_waiting_ms(_adapter *adapter, u8 ch, u8 bw, u8 offset, u32 *r_non_ocp_ms, u32 *r_cac_ms);
void rtw_reset_cac(_adapter *adapter, u8 ch, u8 bw, u8 offset);
#else
#define CH_IS_NON_OCP(rt_ch_info) 0
#define rtw_chset_is_ch_non_ocp(ch_set, ch, bw, offset) _FALSE
#define rtw_rfctl_is_tx_blocked_by_ch_waiting(rfctl) _FALSE
#endif

enum {
	RTW_CHF_2G = BIT0,
	RTW_CHF_5G = BIT1,
	RTW_CHF_DFS = BIT2,
	RTW_CHF_LONG_CAC = BIT3,
	RTW_CHF_NON_DFS = BIT4,
	RTW_CHF_NON_LONG_CAC = BIT5,
	RTW_CHF_NON_OCP = BIT6,
};

bool rtw_choose_shortest_waiting_ch(_adapter *adapter, u8 req_bw, u8 *dec_ch, u8 *dec_bw, u8 *dec_offset, u8 d_flags);

void dump_country_chplan(void *sel, const struct country_chplan *ent);
void dump_country_chplan_map(void *sel);
void dump_chplan_id_list(void *sel);
void dump_chplan_test(void *sel);
void dump_chset(void *sel, RT_CHANNEL_INFO *ch_set);
void dump_cur_chset(void *sel, _adapter *adapter);

int rtw_ch_set_search_ch(RT_CHANNEL_INFO *ch_set, const u32 ch);
u8 rtw_chset_is_chbw_valid(RT_CHANNEL_INFO *ch_set, u8 ch, u8 bw, u8 offset);

bool rtw_mlme_band_check(_adapter *adapter, const u32 ch);


enum {
	BAND_24G = BIT0,
	BAND_5G = BIT1,
};
void RTW_SET_SCAN_BAND_SKIP(_adapter *padapter, int skip_band);
void RTW_CLR_SCAN_BAND_SKIP(_adapter *padapter, int skip_band);
int RTW_GET_SCAN_BAND_SKIP(_adapter *padapter);

bool rtw_mlme_ignore_chan(_adapter *adapter, const u32 ch);

/* P2P_MAX_REG_CLASSES - Maximum number of regulatory classes */
#define P2P_MAX_REG_CLASSES 10

/* P2P_MAX_REG_CLASS_CHANNELS - Maximum number of channels per regulatory class */
#define P2P_MAX_REG_CLASS_CHANNELS 20

/* struct p2p_channels - List of supported channels */
struct p2p_channels {
	/* struct p2p_reg_class - Supported regulatory class */
	struct p2p_reg_class {
		/* reg_class - Regulatory class (IEEE 802.11-2007, Annex J) */
		u8 reg_class;

		/* channel - Supported channels */
		u8 channel[P2P_MAX_REG_CLASS_CHANNELS];

		/* channels - Number of channel entries in use */
		size_t channels;
	} reg_class[P2P_MAX_REG_CLASSES];

	/* reg_classes - Number of reg_class entries in use */
	size_t reg_classes;
};

struct p2p_oper_class_map {
	enum hw_mode {IEEE80211G, IEEE80211A} mode;
	u8 op_class;
	u8 min_chan;
	u8 max_chan;
	u8 inc;
	enum { BW20, BW40PLUS, BW40MINUS } bw;
};

struct mlme_ext_priv {
	_adapter	*padapter;
	u8	mlmeext_init;
	ATOMIC_T		event_seq;
	u16	mgnt_seq;
#ifdef CONFIG_IEEE80211W
	u16	sa_query_seq;
	u64 mgnt_80211w_IPN;
	u64 mgnt_80211w_IPN_rx;
#endif /* CONFIG_IEEE80211W */
	/* struct fw_priv 	fwpriv; */

	unsigned char	cur_channel;
	unsigned char	cur_bwmode;
	unsigned char	cur_ch_offset;/* PRIME_CHNL_OFFSET */
	unsigned char	cur_wireless_mode;	/* NETWORK_TYPE */

	unsigned char	max_chan_nums;
	RT_CHANNEL_INFO		channel_set[MAX_CHANNEL_NUM];
	struct p2p_channels channel_list;
	unsigned char	basicrate[NumRates];
	unsigned char	datarate[NumRates];
#ifdef CONFIG_80211N_HT
	unsigned char default_supported_mcs_set[16];
#endif

	struct ss_res		sitesurvey_res;
	struct mlme_ext_info	mlmext_info;/* for sta/adhoc mode, including current scanning/connecting/connected related info.
                                                      * for ap mode, network includes ap's cap_info */
	_timer		survey_timer;
	_timer		link_timer;
#ifdef CONFIG_RTW_80211R
	_timer		ft_link_timer;
	_timer		ft_roam_timer;
#endif

	/* _timer		ADDBA_timer; */
	u32 last_scan_time;
	u8	scan_abort;
	u8	tx_rate; /* TXRATE when USERATE is set. */

	u32	retry; /* retry for issue probereq */

	u64 TSFValue;

	/* for LPS-32K to adaptive bcn early and timeout */
	u8 adaptive_tsf_done;
	u32 bcn_delay_cnt[9];
	u32 bcn_delay_ratio[9];
	u32 bcn_cnt;
	u8 DrvBcnEarly;
	u8 DrvBcnTimeOut;

#ifdef CONFIG_AP_MODE
	unsigned char bstart_bss;
#endif

#ifdef CONFIG_80211D
	u8 update_channel_plan_by_ap_done;
#endif
	/* recv_decache check for Action_public frame */
	u8 action_public_dialog_token;
	u16	 action_public_rxseq;

	/* #ifdef CONFIG_ACTIVE_KEEP_ALIVE_CHECK */
	u8 active_keep_alive_check;
	/* #endif */
#ifdef DBG_FIXED_CHAN
	u8 fixed_chan;
#endif
	/* set hw sync bcn tsf register or not */
	u8 en_hw_update_tsf;
};

static inline u8 check_mlmeinfo_state(struct mlme_ext_priv *plmeext, sint state)
{
	if ((plmeext->mlmext_info.state & 0x03) == state)
		return _TRUE;

	return _FALSE;
}

#define mlmeext_msr(mlmeext) ((mlmeext)->mlmext_info.state & 0x03)
#define mlmeext_scan_state(mlmeext) ((mlmeext)->sitesurvey_res.state)
#define mlmeext_scan_state_str(mlmeext) scan_state_str((mlmeext)->sitesurvey_res.state)
#define mlmeext_chk_scan_state(mlmeext, _state) ((mlmeext)->sitesurvey_res.state == (_state))
#define mlmeext_set_scan_state(mlmeext, _state) \
	do { \
		((mlmeext)->sitesurvey_res.state = (_state)); \
		((mlmeext)->sitesurvey_res.next_state = (_state)); \
		/* RTW_INFO("set_scan_state:%s\n", scan_state_str(_state)); */ \
	} while (0)

#define mlmeext_scan_next_state(mlmeext) ((mlmeext)->sitesurvey_res.next_state)
#define mlmeext_set_scan_next_state(mlmeext, _state) \
	do { \
		((mlmeext)->sitesurvey_res.next_state = (_state)); \
		/* RTW_INFO("set_scan_next_state:%s\n", scan_state_str(_state)); */ \
	} while (0)

#ifdef CONFIG_SCAN_BACKOP
#define mlmeext_scan_backop_flags(mlmeext) ((mlmeext)->sitesurvey_res.backop_flags)
#define mlmeext_chk_scan_backop_flags(mlmeext, flags) ((mlmeext)->sitesurvey_res.backop_flags & (flags))
#define mlmeext_assign_scan_backop_flags(mlmeext, flags) \
	do { \
		((mlmeext)->sitesurvey_res.backop_flags = (flags)); \
		RTW_INFO("assign_scan_backop_flags:0x%02x\n", (mlmeext)->sitesurvey_res.backop_flags); \
	} while (0)

#define mlmeext_scan_backop_flags_sta(mlmeext) ((mlmeext)->sitesurvey_res.backop_flags_sta)
#define mlmeext_chk_scan_backop_flags_sta(mlmeext, flags) ((mlmeext)->sitesurvey_res.backop_flags_sta & (flags))
#define mlmeext_assign_scan_backop_flags_sta(mlmeext, flags) \
	do { \
		((mlmeext)->sitesurvey_res.backop_flags_sta = (flags)); \
	} while (0)

#define mlmeext_scan_backop_flags_ap(mlmeext) ((mlmeext)->sitesurvey_res.backop_flags_ap)
#define mlmeext_chk_scan_backop_flags_ap(mlmeext, flags) ((mlmeext)->sitesurvey_res.backop_flags_ap & (flags))
#define mlmeext_assign_scan_backop_flags_ap(mlmeext, flags) \
	do { \
		((mlmeext)->sitesurvey_res.backop_flags_ap = (flags)); \
	} while (0)
#else
#define mlmeext_scan_backop_flags(mlmeext) (0)
#define mlmeext_chk_scan_backop_flags(mlmeext, flags) (0)
#define mlmeext_assign_scan_backop_flags(mlmeext, flags) do {} while (0)

#define mlmeext_scan_backop_flags_sta(mlmeext) (0)
#define mlmeext_chk_scan_backop_flags_sta(mlmeext, flags) (0)
#define mlmeext_assign_scan_backop_flags_sta(mlmeext, flags) do {} while (0)

#define mlmeext_scan_backop_flags_ap(mlmeext) (0)
#define mlmeext_chk_scan_backop_flags_ap(mlmeext, flags) (0)
#define mlmeext_assign_scan_backop_flags_ap(mlmeext, flags) do {} while (0)
#endif

void init_mlme_default_rate_set(_adapter *padapter);
int init_mlme_ext_priv(_adapter *padapter);
int init_hw_mlme_ext(_adapter *padapter);
void free_mlme_ext_priv(struct mlme_ext_priv *pmlmeext);
extern void init_mlme_ext_timer(_adapter *padapter);
extern void init_addba_retry_timer(_adapter *padapter, struct sta_info *psta);
extern struct xmit_frame *alloc_mgtxmitframe(struct xmit_priv *pxmitpriv);
struct xmit_frame *alloc_mgtxmitframe_once(struct xmit_priv *pxmitpriv);

/* void fill_fwpriv(_adapter * padapter, struct fw_priv *pfwpriv); */
#ifdef CONFIG_GET_RAID_BY_DRV
unsigned char networktype_to_raid(_adapter *adapter, struct sta_info *psta);
unsigned char networktype_to_raid_ex(_adapter *adapter, struct sta_info *psta);
#endif
u8 judge_network_type(_adapter *padapter, unsigned char *rate, int ratelen);
void get_rate_set(_adapter *padapter, unsigned char *pbssrate, int *bssrate_len);
void set_mcs_rate_by_mask(u8 *mcs_set, u32 mask);
void UpdateBrateTbl(_adapter *padapter, u8 *mBratesOS);
void UpdateBrateTblForSoftAP(u8 *bssrateset, u32 bssratelen);
void change_band_update_ie(_adapter *padapter, WLAN_BSSID_EX *pnetwork, u8 ch);

void Set_MSR(_adapter *padapter, u8 type);

u8 rtw_get_oper_ch(_adapter *adapter);
void rtw_set_oper_ch(_adapter *adapter, u8 ch);
u8 rtw_get_oper_bw(_adapter *adapter);
void rtw_set_oper_bw(_adapter *adapter, u8 bw);
u8 rtw_get_oper_choffset(_adapter *adapter);
void rtw_set_oper_choffset(_adapter *adapter, u8 offset);
u8	rtw_get_center_ch(u8 channel, u8 chnl_bw, u8 chnl_offset);
u32 rtw_get_on_oper_ch_time(_adapter *adapter);
u32 rtw_get_on_cur_ch_time(_adapter *adapter);

u8 rtw_get_offset_by_chbw(u8 ch, u8 bw, u8 *r_offset);
u8 rtw_get_offset_by_ch(u8 channel);

void set_channel_bwmode(_adapter *padapter, unsigned char channel, unsigned char channel_offset, unsigned short bwmode);

unsigned int decide_wait_for_beacon_timeout(unsigned int bcn_interval);

void _clear_cam_entry(_adapter *padapter, u8 entry);
void write_cam_from_cache(_adapter *adapter, u8 id);
void rtw_sec_cam_swap(_adapter *adapter, u8 cam_id_a, u8 cam_id_b);
void rtw_clean_dk_section(_adapter *adapter);
void rtw_clean_hw_dk_cam(_adapter *adapter);

/* modify both HW and cache */
void write_cam(_adapter *padapter, u8 id, u16 ctrl, u8 *mac, u8 *key);
void clear_cam_entry(_adapter *padapter, u8 id);

/* modify cache only */
void write_cam_cache(_adapter *adapter, u8 id, u16 ctrl, u8 *mac, u8 *key);
void clear_cam_cache(_adapter *adapter, u8 id);

void invalidate_cam_all(_adapter *padapter);
void CAM_empty_entry(PADAPTER Adapter, u8 ucIndex);

void flush_all_cam_entry(_adapter *padapter);

BOOLEAN IsLegal5GChannel(PADAPTER Adapter, u8 channel);

void site_survey(_adapter *padapter, u8 survey_channel, RT_SCAN_TYPE ScanType);
u8 collect_bss_info(_adapter *padapter, union recv_frame *precv_frame, WLAN_BSSID_EX *bssid);
void update_network(WLAN_BSSID_EX *dst, WLAN_BSSID_EX *src, _adapter *padapter, bool update_ie);

int get_bsstype(unsigned short capability);
u8 *get_my_bssid(WLAN_BSSID_EX *pnetwork);
u16 get_beacon_interval(WLAN_BSSID_EX *bss);

int is_client_associated_to_ap(_adapter *padapter);
int is_client_associated_to_ibss(_adapter *padapter);
int is_IBSS_empty(_adapter *padapter);

unsigned char check_assoc_AP(u8 *pframe, uint len);

int WMM_param_handler(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs	pIE);
#ifdef CONFIG_WFD
void rtw_process_wfd_ie(_adapter *adapter, u8 *ie, u8 ie_len, const char *tag);
void rtw_process_wfd_ies(_adapter *adapter, u8 *ies, u8 ies_len, const char *tag);
#endif
void WMMOnAssocRsp(_adapter *padapter);

void HT_caps_handler(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs pIE);
void HT_info_handler(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs pIE);
void HTOnAssocRsp(_adapter *padapter);

void ERP_IE_handler(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs pIE);
void VCS_update(_adapter *padapter, struct sta_info *psta);
void	update_ldpc_stbc_cap(struct sta_info *psta);

int rtw_get_bcn_keys(ADAPTER *Adapter, u8 *pframe, u32 packet_len,
		struct beacon_keys *recv_beacon);
int validate_beacon_len(u8 *pframe, uint len);
void rtw_dump_bcn_keys(struct beacon_keys *recv_beacon);
int rtw_check_bcn_info(ADAPTER *Adapter, u8 *pframe, u32 packet_len);
void update_beacon_info(_adapter *padapter, u8 *pframe, uint len, struct sta_info *psta);
#ifdef CONFIG_DFS
void process_csa_ie(_adapter *padapter, u8 *pframe, uint len);
#endif /* CONFIG_DFS */
void update_capinfo(PADAPTER Adapter, u16 updateCap);
void update_wireless_mode(_adapter *padapter);
void update_tx_basic_rate(_adapter *padapter, u8 modulation);
void update_sta_basic_rate(struct sta_info *psta, u8 wireless_mode);
int rtw_ies_get_supported_rate(u8 *ies, uint ies_len, u8 *rate_set, u8 *rate_num);

/* for sta/adhoc mode */
void update_sta_info(_adapter *padapter, struct sta_info *psta);
unsigned int update_basic_rate(unsigned char *ptn, unsigned int ptn_sz);
unsigned int update_supported_rate(unsigned char *ptn, unsigned int ptn_sz);
void Update_RA_Entry(_adapter *padapter, struct sta_info *psta);
void set_sta_rate(_adapter *padapter, struct sta_info *psta);

unsigned int receive_disconnect(_adapter *padapter, unsigned char *MacAddr, unsigned short reason, u8 locally_generated);

unsigned char get_highest_rate_idx(u32 mask);
int support_short_GI(_adapter *padapter, struct HT_caps_element *pHT_caps, u8 bwmode);
unsigned int is_ap_in_tkip(_adapter *padapter);
unsigned int is_ap_in_wep(_adapter *padapter);
unsigned int should_forbid_n_rate(_adapter *padapter);

bool _rtw_camctl_chk_cap(_adapter *adapter, u8 cap);
void _rtw_camctl_set_flags(_adapter *adapter, u32 flags);
void rtw_camctl_set_flags(_adapter *adapter, u32 flags);
void _rtw_camctl_clr_flags(_adapter *adapter, u32 flags);
void rtw_camctl_clr_flags(_adapter *adapter, u32 flags);
bool _rtw_camctl_chk_flags(_adapter *adapter, u32 flags);

struct sec_cam_bmp;
void dump_sec_cam_map(void *sel, struct sec_cam_bmp *map, u8 max_num);
void rtw_sec_cam_map_clr_all(struct sec_cam_bmp *map);

bool _rtw_camid_is_gk(_adapter *adapter, u8 cam_id);
bool rtw_camid_is_gk(_adapter *adapter, u8 cam_id);
s16 rtw_camid_search(_adapter *adapter, u8 *addr, s16 kid, s8 gk);
s16 rtw_camid_alloc(_adapter *adapter, struct sta_info *sta, u8 kid, bool *used);
void rtw_camid_free(_adapter *adapter, u8 cam_id);
u8 rtw_get_sec_camid(_adapter *adapter, u8 max_bk_key_num, u8 *sec_key_id);

struct macid_bmp;
struct macid_ctl_t;
void dump_macid_map(void *sel, struct macid_bmp *map, u8 max_num);
bool rtw_macid_is_set(struct macid_bmp *map, u8 id);
bool rtw_macid_is_used(struct macid_ctl_t *macid_ctl, u8 id);
bool rtw_macid_is_bmc(struct macid_ctl_t *macid_ctl, u8 id);
s8 rtw_macid_get_if_g(struct macid_ctl_t *macid_ctl, u8 id);
s8 rtw_macid_get_ch_g(struct macid_ctl_t *macid_ctl, u8 id);
void rtw_alloc_macid(_adapter *padapter, struct sta_info *psta);
void rtw_release_macid(_adapter *padapter, struct sta_info *psta);
u8 rtw_search_max_mac_id(_adapter *padapter);
void rtw_macid_ctl_set_h2c_msr(struct macid_ctl_t *macid_ctl, u8 id, u8 h2c_msr);
void rtw_macid_ctl_set_bw(struct macid_ctl_t *macid_ctl, u8 id, u8 bw);
void rtw_macid_ctl_set_vht_en(struct macid_ctl_t *macid_ctl, u8 id, u8 en);
void rtw_macid_ctl_set_rate_bmp0(struct macid_ctl_t *macid_ctl, u8 id, u32 bmp);
void rtw_macid_ctl_set_rate_bmp1(struct macid_ctl_t *macid_ctl, u8 id, u32 bmp);
void rtw_macid_ctl_init(struct macid_ctl_t *macid_ctl);
void rtw_macid_ctl_deinit(struct macid_ctl_t *macid_ctl);
u8 rtw_iface_bcmc_id_get(_adapter *padapter);

u32 report_join_res(_adapter *padapter, int res);
void report_survey_event(_adapter *padapter, union recv_frame *precv_frame);
void report_surveydone_event(_adapter *padapter);
u32 report_del_sta_event(_adapter *padapter, unsigned char *MacAddr, unsigned short reason, bool enqueue, u8 locally_generated);
void report_add_sta_event(_adapter *padapter, unsigned char *MacAddr);
bool rtw_port_switch_chk(_adapter *adapter);
void report_wmm_edca_update(_adapter *padapter);

void beacon_timing_control(_adapter *padapter);
u8 chk_bmc_sleepq_cmd(_adapter *padapter);
extern u8 set_tx_beacon_cmd(_adapter *padapter);
unsigned int setup_beacon_frame(_adapter *padapter, unsigned char *beacon_frame);
void update_mgnt_tx_rate(_adapter *padapter, u8 rate);
void update_monitor_frame_attrib(_adapter *padapter, struct pkt_attrib *pattrib);
void update_mgntframe_attrib(_adapter *padapter, struct pkt_attrib *pattrib);
void update_mgntframe_attrib_addr(_adapter *padapter, struct xmit_frame *pmgntframe);
void dump_mgntframe(_adapter *padapter, struct xmit_frame *pmgntframe);
s32 dump_mgntframe_and_wait(_adapter *padapter, struct xmit_frame *pmgntframe, int timeout_ms);
s32 dump_mgntframe_and_wait_ack(_adapter *padapter, struct xmit_frame *pmgntframe);
s32 dump_mgntframe_and_wait_ack_timeout(_adapter *padapter, struct xmit_frame *pmgntframe, int timeout_ms);

#ifdef CONFIG_P2P
void issue_probersp_p2p(_adapter *padapter, unsigned char *da);
void issue_p2p_provision_request(_adapter *padapter, u8 *pssid, u8 ussidlen, u8 *pdev_raddr);
void issue_p2p_GO_request(_adapter *padapter, u8 *raddr);
void issue_probereq_p2p(_adapter *padapter, u8 *da);
int issue_probereq_p2p_ex(_adapter *adapter, u8 *da, int try_cnt, int wait_ms);
void issue_p2p_invitation_response(_adapter *padapter, u8 *raddr, u8 dialogToken, u8 success);
void issue_p2p_invitation_request(_adapter *padapter, u8 *raddr);
#endif /* CONFIG_P2P */
void issue_beacon(_adapter *padapter, int timeout_ms);
void issue_probersp(_adapter *padapter, unsigned char *da, u8 is_valid_p2p_probereq);
void _issue_assocreq(_adapter *padapter, u8 is_assoc);
void issue_assocreq(_adapter *padapter);
void issue_reassocreq(_adapter *padapter);
void issue_asocrsp(_adapter *padapter, unsigned short status, struct sta_info *pstat, int pkt_type);
void issue_auth(_adapter *padapter, struct sta_info *psta, unsigned short status);
void issue_probereq(_adapter *padapter, NDIS_802_11_SSID *pssid, u8 *da);
s32 issue_probereq_ex(_adapter *padapter, NDIS_802_11_SSID *pssid, u8 *da, u8 ch, bool append_wps, int try_cnt, int wait_ms);
int issue_nulldata(_adapter *padapter, unsigned char *da, unsigned int power_mode, int try_cnt, int wait_ms);
s32 issue_nulldata_in_interrupt(PADAPTER padapter, u8 *da, unsigned int power_mode);
int issue_qos_nulldata(_adapter *padapter, unsigned char *da, u16 tid, int try_cnt, int wait_ms);
int issue_deauth(_adapter *padapter, unsigned char *da, unsigned short reason);
int issue_deauth_ex(_adapter *padapter, u8 *da, unsigned short reason, int try_cnt, int wait_ms);
void issue_action_spct_ch_switch(_adapter *padapter, u8 *ra, u8 new_ch, u8 ch_offset);
void issue_addba_req(_adapter *adapter, unsigned char *ra, u8 tid);
void issue_addba_rsp(_adapter *adapter, unsigned char *ra, u8 tid, u16 status, u8 size);
u8 issue_addba_rsp_wait_ack(_adapter *adapter, unsigned char *ra, u8 tid, u16 status, u8 size, int try_cnt, int wait_ms);
void issue_del_ba(_adapter *adapter, unsigned char *ra, u8 tid, u16 reason, u8 initiator);
int issue_del_ba_ex(_adapter *adapter, unsigned char *ra, u8 tid, u16 reason, u8 initiator, int try_cnt, int wait_ms);

#ifdef CONFIG_IEEE80211W
void issue_action_SA_Query(_adapter *padapter, unsigned char *raddr, unsigned char action, unsigned short tid, u8 key_type);
int issue_deauth_11w(_adapter *padapter, unsigned char *da, unsigned short reason, u8 key_type);
extern void init_dot11w_expire_timer(_adapter *padapter, struct sta_info *psta);
#endif /* CONFIG_IEEE80211W */
int issue_action_SM_PS(_adapter *padapter ,  unsigned char *raddr , u8 NewMimoPsMode);
int issue_action_SM_PS_wait_ack(_adapter *padapter, unsigned char *raddr, u8 NewMimoPsMode, int try_cnt, int wait_ms);

unsigned int send_delba_sta_tid(_adapter *adapter, u8 initiator, struct sta_info *sta, u8 tid, u8 force);
unsigned int send_delba_sta_tid_wait_ack(_adapter *adapter, u8 initiator, struct sta_info *sta, u8 tid, u8 force);

unsigned int send_delba(_adapter *padapter, u8 initiator, u8 *addr);
unsigned int send_beacon(_adapter *padapter);

void start_clnt_assoc(_adapter *padapter);
void start_clnt_auth(_adapter *padapter);
void start_clnt_join(_adapter *padapter);
void start_create_ibss(_adapter *padapter);

unsigned int OnAssocReq(_adapter *padapter, union recv_frame *precv_frame);
unsigned int OnAssocRsp(_adapter *padapter, union recv_frame *precv_frame);
unsigned int OnProbeReq(_adapter *padapter, union recv_frame *precv_frame);
unsigned int OnProbeRsp(_adapter *padapter, union recv_frame *precv_frame);
unsigned int DoReserved(_adapter *padapter, union recv_frame *precv_frame);
unsigned int OnBeacon(_adapter *padapter, union recv_frame *precv_frame);
unsigned int OnAtim(_adapter *padapter, union recv_frame *precv_frame);
unsigned int OnDisassoc(_adapter *padapter, union recv_frame *precv_frame);
unsigned int OnAuth(_adapter *padapter, union recv_frame *precv_frame);
unsigned int OnAuthClient(_adapter *padapter, union recv_frame *precv_frame);
unsigned int OnDeAuth(_adapter *padapter, union recv_frame *precv_frame);
unsigned int OnAction(_adapter *padapter, union recv_frame *precv_frame);

unsigned int on_action_spct(_adapter *padapter, union recv_frame *precv_frame);
unsigned int OnAction_qos(_adapter *padapter, union recv_frame *precv_frame);
unsigned int OnAction_dls(_adapter *padapter, union recv_frame *precv_frame);
#ifdef CONFIG_RTW_WNM
unsigned int on_action_wnm(_adapter *adapter, union recv_frame *rframe);
#endif

#define RX_AMPDU_ACCEPT_INVALID 0xFF
#define RX_AMPDU_SIZE_INVALID 0xFF

enum rx_ampdu_reason {
	RX_AMPDU_DRV_FIXED = 1,
	RX_AMPDU_BTCOEX = 2, /* not used, because BTCOEX has its own variable management */
	RX_AMPDU_DRV_SCAN = 3,
};
u8 rtw_rx_ampdu_size(_adapter *adapter);
bool rtw_rx_ampdu_is_accept(_adapter *adapter);
bool rtw_rx_ampdu_set_size(_adapter *adapter, u8 size, u8 reason);
bool rtw_rx_ampdu_set_accept(_adapter *adapter, u8 accept, u8 reason);
u8 rx_ampdu_apply_sta_tid(_adapter *adapter, struct sta_info *sta, u8 tid, u8 accept, u8 size);
u8 rx_ampdu_size_sta_limit(_adapter *adapter, struct sta_info *sta);
u8 rx_ampdu_apply_sta(_adapter *adapter, struct sta_info *sta, u8 accept, u8 size);
u16 rtw_rx_ampdu_apply(_adapter *adapter);

unsigned int OnAction_back(_adapter *padapter, union recv_frame *precv_frame);
unsigned int on_action_public(_adapter *padapter, union recv_frame *precv_frame);
unsigned int OnAction_ft(_adapter *padapter, union recv_frame *precv_frame);
unsigned int OnAction_ht(_adapter *padapter, union recv_frame *precv_frame);
#ifdef CONFIG_IEEE80211W
unsigned int OnAction_sa_query(_adapter *padapter, union recv_frame *precv_frame);
#endif /* CONFIG_IEEE80211W */
unsigned int OnAction_wmm(_adapter *padapter, union recv_frame *precv_frame);
unsigned int OnAction_vht(_adapter *padapter, union recv_frame *precv_frame);
unsigned int OnAction_p2p(_adapter *padapter, union recv_frame *precv_frame);

#ifdef CONFIG_RTW_80211R
void start_clnt_ft_action(_adapter *padapter, u8 *pTargetAddr);
void issue_action_ft_request(_adapter *padapter, u8 *pTargetAddr);
void report_ft_event(_adapter *padapter);
void report_ft_reassoc_event(_adapter *padapter, u8 *pMacAddr);
void ft_link_timer_hdl(_adapter *padapter);
void ft_roam_timer_hdl(_adapter *padapter);
#endif
void mlmeext_joinbss_event_callback(_adapter *padapter, int join_res);
void mlmeext_sta_del_event_callback(_adapter *padapter);
void mlmeext_sta_add_event_callback(_adapter *padapter, struct sta_info *psta);

void linked_status_chk(_adapter *padapter, u8 from_timer);

void _linked_info_dump(_adapter *padapter);

void survey_timer_hdl(_adapter *padapter);
void link_timer_hdl(_adapter *padapter);
void addba_timer_hdl(struct sta_info *psta);
#ifdef CONFIG_IEEE80211W
void sa_query_timer_hdl(struct sta_info *psta);
#endif /* CONFIG_IEEE80211W */
#if 0
void reauth_timer_hdl(_adapter *padapter);
void reassoc_timer_hdl(_adapter *padapter);
#endif

#define set_survey_timer(mlmeext, ms) \
	do { \
		/*RTW_INFO("%s set_survey_timer(%p, %d)\n", __FUNCTION__, (mlmeext), (ms));*/ \
		_set_timer(&(mlmeext)->survey_timer, (ms)); \
	} while (0)

#define set_link_timer(mlmeext, ms) \
	do { \
		/*RTW_INFO("%s set_link_timer(%p, %d)\n", __FUNCTION__, (mlmeext), (ms));*/ \
		_set_timer(&(mlmeext)->link_timer, (ms)); \
	} while (0)

extern int cckrates_included(unsigned char *rate, int ratelen);
extern int cckratesonly_included(unsigned char *rate, int ratelen);

extern void process_addba_req(_adapter *padapter, u8 *paddba_req, u8 *addr);

extern void update_TSF(struct mlme_ext_priv *pmlmeext, u8 *pframe, uint len);
extern void correct_TSF(_adapter *padapter, struct mlme_ext_priv *pmlmeext);
extern void adaptive_early_32k(struct mlme_ext_priv *pmlmeext, u8 *pframe, uint len);
extern u8 traffic_status_watchdog(_adapter *padapter, u8 from_timer);


void rtw_join_done_chk_ch(_adapter *padapter, int join_res);

int rtw_chk_start_clnt_join(_adapter *padapter, u8 *ch, u8 *bw, u8 *offset);

#ifdef CONFIG_PLATFORM_ARM_SUN8I
	#define BUSY_TRAFFIC_SCAN_DENY_PERIOD	8000
#else
	#define BUSY_TRAFFIC_SCAN_DENY_PERIOD	12000
#endif

struct cmd_hdl {
	uint	parmsize;
	u8(*h2cfuns)(struct _ADAPTER *padapter, u8 *pbuf);
};


u8 read_macreg_hdl(_adapter *padapter, u8 *pbuf);
u8 write_macreg_hdl(_adapter *padapter, u8 *pbuf);
u8 read_bbreg_hdl(_adapter *padapter, u8 *pbuf);
u8 write_bbreg_hdl(_adapter *padapter, u8 *pbuf);
u8 read_rfreg_hdl(_adapter *padapter, u8 *pbuf);
u8 write_rfreg_hdl(_adapter *padapter, u8 *pbuf);


u8 NULL_hdl(_adapter *padapter, u8 *pbuf);
u8 join_cmd_hdl(_adapter *padapter, u8 *pbuf);
u8 disconnect_hdl(_adapter *padapter, u8 *pbuf);
u8 createbss_hdl(_adapter *padapter, u8 *pbuf);
u8 setopmode_hdl(_adapter *padapter, u8 *pbuf);
u8 sitesurvey_cmd_hdl(_adapter *padapter, u8 *pbuf);
u8 setauth_hdl(_adapter *padapter, u8 *pbuf);
u8 setkey_hdl(_adapter *padapter, u8 *pbuf);
u8 set_stakey_hdl(_adapter *padapter, u8 *pbuf);
u8 set_assocsta_hdl(_adapter *padapter, u8 *pbuf);
u8 del_assocsta_hdl(_adapter *padapter, u8 *pbuf);
u8 add_ba_hdl(_adapter *padapter, unsigned char *pbuf);
u8 add_ba_rsp_hdl(_adapter *padapter, unsigned char *pbuf);

void rtw_ap_wep_pk_setting(_adapter *adapter, struct sta_info *psta);

u8 mlme_evt_hdl(_adapter *padapter, unsigned char *pbuf);
u8 h2c_msg_hdl(_adapter *padapter, unsigned char *pbuf);
u8 chk_bmc_sleepq_hdl(_adapter *padapter, unsigned char *pbuf);
u8 tx_beacon_hdl(_adapter *padapter, unsigned char *pbuf);
u8 set_ch_hdl(_adapter *padapter, u8 *pbuf);
u8 set_chplan_hdl(_adapter *padapter, unsigned char *pbuf);
u8 led_blink_hdl(_adapter *padapter, unsigned char *pbuf);
u8 set_csa_hdl(_adapter *padapter, unsigned char *pbuf);	/* Kurt: Handling DFS channel switch announcement ie. */
u8 tdls_hdl(_adapter *padapter, unsigned char *pbuf);
u8 run_in_thread_hdl(_adapter *padapter, u8 *pbuf);
u8 rtw_getmacreg_hdl(_adapter *padapter, u8 *pbuf);

#define GEN_DRV_CMD_HANDLER(size, cmd)	{size, &cmd ## _hdl},
#define GEN_MLME_EXT_HANDLER(size, cmd)	{size, cmd},

#ifdef _RTW_CMD_C_

struct cmd_hdl wlancmds[] = {
	GEN_DRV_CMD_HANDLER(sizeof(struct readMAC_parm), rtw_getmacreg) /*0*/
	GEN_DRV_CMD_HANDLER(0, NULL)
	GEN_DRV_CMD_HANDLER(0, NULL)
	GEN_DRV_CMD_HANDLER(0, NULL)
	GEN_DRV_CMD_HANDLER(0, NULL)
	GEN_DRV_CMD_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL) /*10*/
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(sizeof(struct joinbss_parm), join_cmd_hdl)  /*14*/
	GEN_MLME_EXT_HANDLER(sizeof(struct disconnect_parm), disconnect_hdl)
	GEN_MLME_EXT_HANDLER(sizeof(struct createbss_parm), createbss_hdl)
	GEN_MLME_EXT_HANDLER(sizeof(struct setopmode_parm), setopmode_hdl)
	GEN_MLME_EXT_HANDLER(sizeof(struct sitesurvey_parm), sitesurvey_cmd_hdl)  /*18*/
	GEN_MLME_EXT_HANDLER(sizeof(struct setauth_parm), setauth_hdl)
	GEN_MLME_EXT_HANDLER(sizeof(struct setkey_parm), setkey_hdl)  /*20*/
	GEN_MLME_EXT_HANDLER(sizeof(struct set_stakey_parm), set_stakey_hdl)
	GEN_MLME_EXT_HANDLER(sizeof(struct set_assocsta_parm), NULL)
	GEN_MLME_EXT_HANDLER(sizeof(struct del_assocsta_parm), NULL)
	GEN_MLME_EXT_HANDLER(sizeof(struct setstapwrstate_parm), NULL)
	GEN_MLME_EXT_HANDLER(sizeof(struct setbasicrate_parm), NULL)
	GEN_MLME_EXT_HANDLER(sizeof(struct getbasicrate_parm), NULL)
	GEN_MLME_EXT_HANDLER(sizeof(struct setdatarate_parm), NULL)
	GEN_MLME_EXT_HANDLER(sizeof(struct getdatarate_parm), NULL)
	GEN_MLME_EXT_HANDLER(sizeof(struct setphyinfo_parm), NULL)
	GEN_MLME_EXT_HANDLER(sizeof(struct getphyinfo_parm), NULL)   /*30*/
	GEN_MLME_EXT_HANDLER(sizeof(struct setphy_parm), NULL)
	GEN_MLME_EXT_HANDLER(sizeof(struct getphy_parm), NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)	/*40*/
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(sizeof(struct addBaReq_parm), add_ba_hdl)
	GEN_MLME_EXT_HANDLER(sizeof(struct set_ch_parm), set_ch_hdl) /* 46 */
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL) /*50*/
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(0, NULL)
	GEN_MLME_EXT_HANDLER(sizeof(struct Tx_Beacon_param), tx_beacon_hdl) /*55*/

	GEN_MLME_EXT_HANDLER(0, mlme_evt_hdl) /*56*/
	GEN_MLME_EXT_HANDLER(0, rtw_drvextra_cmd_hdl) /*57*/

	GEN_MLME_EXT_HANDLER(0, h2c_msg_hdl) /*58*/
	GEN_MLME_EXT_HANDLER(sizeof(struct SetChannelPlan_param), set_chplan_hdl) /*59*/
	GEN_MLME_EXT_HANDLER(sizeof(struct LedBlink_param), led_blink_hdl) /*60*/

	GEN_MLME_EXT_HANDLER(sizeof(struct SetChannelSwitch_param), set_csa_hdl) /*61*/
	GEN_MLME_EXT_HANDLER(sizeof(struct TDLSoption_param), tdls_hdl) /*62*/
	GEN_MLME_EXT_HANDLER(0, chk_bmc_sleepq_hdl) /*63*/
	GEN_MLME_EXT_HANDLER(sizeof(struct RunInThread_param), run_in_thread_hdl) /*64*/
	GEN_MLME_EXT_HANDLER(sizeof(struct addBaRsp_parm), add_ba_rsp_hdl) /* 65 */
};

#endif

struct C2HEvent_Header {

#ifdef CONFIG_LITTLE_ENDIAN

	unsigned int len:16;
	unsigned int ID:8;
	unsigned int seq:8;

#elif defined(CONFIG_BIG_ENDIAN)

	unsigned int seq:8;
	unsigned int ID:8;
	unsigned int len:16;

#else

#  error "Must be LITTLE or BIG Endian"

#endif

	unsigned int rsvd;

};

void rtw_dummy_event_callback(_adapter *adapter , u8 *pbuf);
void rtw_fwdbg_event_callback(_adapter *adapter , u8 *pbuf);

enum rtw_c2h_event {
	GEN_EVT_CODE(_Read_MACREG) = 0, /*0*/
	GEN_EVT_CODE(_Read_BBREG),
	GEN_EVT_CODE(_Read_RFREG),
	GEN_EVT_CODE(_Read_EEPROM),
	GEN_EVT_CODE(_Read_EFUSE),
	GEN_EVT_CODE(_Read_CAM),			/*5*/
	GEN_EVT_CODE(_Get_BasicRate),
	GEN_EVT_CODE(_Get_DataRate),
	GEN_EVT_CODE(_Survey),	 /*8*/
	GEN_EVT_CODE(_SurveyDone),	 /*9*/

	GEN_EVT_CODE(_JoinBss) , /*10*/
	GEN_EVT_CODE(_AddSTA),
	GEN_EVT_CODE(_DelSTA),
	GEN_EVT_CODE(_AtimDone) ,
	GEN_EVT_CODE(_TX_Report),
	GEN_EVT_CODE(_CCX_Report),			/*15*/
	GEN_EVT_CODE(_DTM_Report),
	GEN_EVT_CODE(_TX_Rate_Statistics),
	GEN_EVT_CODE(_C2HLBK),
	GEN_EVT_CODE(_FWDBG),
	GEN_EVT_CODE(_C2HFEEDBACK),               /*20*/
	GEN_EVT_CODE(_ADDBA),
	GEN_EVT_CODE(_C2HBCN),
	GEN_EVT_CODE(_ReportPwrState),		/* filen: only for PCIE, USB	 */
	GEN_EVT_CODE(_CloseRF),				/* filen: only for PCIE, work around ASPM */
	GEN_EVT_CODE(_WMM),					/*25*/
#ifdef CONFIG_IEEE80211W
	GEN_EVT_CODE(_TimeoutSTA),
#endif /* CONFIG_IEEE80211W */
#ifdef CONFIG_RTW_80211R
	GEN_EVT_CODE(_FT_REASSOC),
#endif
	MAX_C2HEVT
};


#ifdef _RTW_MLME_EXT_C_

static struct fwevent wlanevents[] = {
	{0, rtw_dummy_event_callback},	/*0*/
	{0, NULL},
	{0, NULL},
	{0, NULL},
	{0, NULL},
	{0, NULL},
	{0, NULL},
	{0, NULL},
	{0, &rtw_survey_event_callback},		/*8*/
	{sizeof(struct surveydone_event), &rtw_surveydone_event_callback},	/*9*/

	{0, &rtw_joinbss_event_callback},		/*10*/
	{sizeof(struct stassoc_event), &rtw_stassoc_event_callback},
	{sizeof(struct stadel_event), &rtw_stadel_event_callback},
	{0, &rtw_atimdone_event_callback},
	{0, rtw_dummy_event_callback},
	{0, NULL},	/*15*/
	{0, NULL},
	{0, NULL},
	{0, NULL},
	{0, rtw_fwdbg_event_callback},
	{0, NULL},	 /*20*/
	{0, NULL},
	{0, NULL},
	{0, &rtw_cpwm_event_callback},
	{0, NULL},
	{0, &rtw_wmm_event_callback}, /*25*/
#ifdef CONFIG_IEEE80211W
	{sizeof(struct stadel_event), &rtw_sta_timeout_event_callback},
#endif /* CONFIG_IEEE80211W */
#ifdef CONFIG_RTW_80211R
	{sizeof(struct stassoc_event), &rtw_ft_reassoc_event_callback},
#endif
};

#endif/* _RTW_MLME_EXT_C_ */

#endif
