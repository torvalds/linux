/*
 * Linux cfg80211 driver - Android related functions
 *
 * Portions of this code are copyright (c) 2022 Cypress Semiconductor Corporation
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: wl_android.c 814826 2019-04-15 05:25:59Z $
 */

#include <linux/string.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <net/netlink.h>

#include <wl_android.h>
#include <wldev_common.h>
#include <wlioctl.h>
#include <wlioctl_utils.h>
#include <bcmutils.h>
#include <bcmstdlib_s.h>
#include <linux_osl.h>
#include <dhd_dbg.h>
#include <dngl_stats.h>
#include <dhd.h>
#include <bcmip.h>
#ifdef PNO_SUPPORT
#include <dhd_pno.h>
#endif // endif
#ifdef BCMSDIO
#include <bcmsdbus.h>
#endif // endif
#ifdef WL_CFG80211
#include <wl_cfg80211.h>
#include <wl_cfgscan.h>
#endif // endif
#ifdef WL_NAN
#include <wl_cfgnan.h>
#endif /* WL_NAN */
#ifdef DHDTCPACK_SUPPRESS
#include <dhd_ip.h>
#endif /* DHDTCPACK_SUPPRESS */
#include <bcmwifi_rspec.h>
#include <bcmwifi_channels.h>
#include <dhd_linux.h>
#include <bcmiov.h>
#ifdef DHD_PKT_LOGGING
#include <dhd_pktlog.h>
#endif /* DHD_PKT_LOGGING */
#ifdef WL_BCNRECV
#include <wl_cfgvendor.h>
#include <brcm_nl80211.h>
#endif /* WL_BCNRECV */
#ifdef WL_MBO
#include <mbo.h>
#endif /* WL_MBO */

#ifdef DHD_BANDSTEER
#include <dhd_bandsteer.h>
#endif /* DHD_BANDSTEER */

#ifdef WL_STATIC_IF
#define WL_BSSIDX_MAX	16
#endif /* WL_STATIC_IF */
/*
 * Android private command strings, PLEASE define new private commands here
 * so they can be updated easily in the future (if needed)
 */

#define CMD_START		"START"
#define CMD_STOP		"STOP"

#ifdef AUTOMOTIVE_FEATURE
#define CMD_SCAN_ACTIVE         "SCAN-ACTIVE"
#define CMD_SCAN_PASSIVE        "SCAN-PASSIVE"
#define CMD_RSSI                "RSSI"
#define CMD_LINKSPEED		"LINKSPEED"
#endif /* AUTOMOTIVE_FEATURE */

#define CMD_RXFILTER_START	"RXFILTER-START"
#define CMD_RXFILTER_STOP	"RXFILTER-STOP"
#define CMD_RXFILTER_ADD	"RXFILTER-ADD"
#define CMD_RXFILTER_REMOVE	"RXFILTER-REMOVE"
#define CMD_BTCOEXSCAN_START	"BTCOEXSCAN-START"
#define CMD_BTCOEXSCAN_STOP	"BTCOEXSCAN-STOP"
#define CMD_BTCOEXMODE		"BTCOEXMODE"
#define CMD_SETSUSPENDOPT	"SETSUSPENDOPT"
#define CMD_SETSUSPENDMODE      "SETSUSPENDMODE"
#define CMD_SETDTIM_IN_SUSPEND  "SET_DTIM_IN_SUSPEND"
#define CMD_MAXDTIM_IN_SUSPEND  "MAX_DTIM_IN_SUSPEND"
#define CMD_DISDTIM_IN_SUSPEND  "DISABLE_DTIM_IN_SUSPEND"
#define CMD_P2P_DEV_ADDR	"P2P_DEV_ADDR"
#define CMD_SETFWPATH		"SETFWPATH"
#define CMD_SETBAND		"SETBAND"
#define CMD_GETBAND		"GETBAND"
#define CMD_COUNTRY		"COUNTRY"
#define CMD_CHANNELS_IN_CC      "CHANNELS_IN_CC"
#define CMD_P2P_SET_NOA		"P2P_SET_NOA"
#define CMD_BLOCKASSOC         "BLOCKASSOC"
#if !defined WL_ENABLE_P2P_IF
#define CMD_P2P_GET_NOA			"P2P_GET_NOA"
#endif /* WL_ENABLE_P2P_IF */
#define CMD_P2P_SD_OFFLOAD		"P2P_SD_"
#define CMD_P2P_LISTEN_OFFLOAD		"P2P_LO_"
#define CMD_P2P_SET_PS		"P2P_SET_PS"
#define CMD_P2P_ECSA		"P2P_ECSA"
#define CMD_P2P_INC_BW		"P2P_INCREASE_BW"
#define CMD_SET_AP_WPS_P2P_IE 		"SET_AP_WPS_P2P_IE"
#define CMD_SETROAMMODE 	"SETROAMMODE"
#define CMD_SETIBSSBEACONOUIDATA	"SETIBSSBEACONOUIDATA"
#define CMD_MIRACAST		"MIRACAST"
#ifdef WL_NAN
#define CMD_NAN         "NAN_"
#endif /* WL_NAN */
#define CMD_COUNTRY_DELIMITER "/"

#if defined(WL_SUPPORT_AUTO_CHANNEL)
#define CMD_GET_BEST_CHANNELS	"GET_BEST_CHANNELS"
#endif /* WL_SUPPORT_AUTO_CHANNEL */

#define CMD_CHANSPEC      "CHANSPEC"
#ifdef AUTOMOTIVE_FEATURE
#define CMD_DATARATE      "DATARATE"
#define CMD_80211_MODE    "MODE"  /* 802.11 mode a/b/g/n/ac */
#define CMD_ASSOC_CLIENTS "ASSOCLIST"
#endif /* AUTOMOTIVE_FEATURE */
#define CMD_SET_CSA       "SETCSA"
#define CMD_RSDB_MODE	"RSDB_MODE"
#ifdef WL_SUPPORT_AUTO_CHANNEL
#define CMD_SET_HAPD_AUTO_CHANNEL	"HAPD_AUTO_CHANNEL"
#endif /* WL_SUPPORT_AUTO_CHANNEL */
#ifdef CUSTOMER_HW4_PRIVATE_CMD
/* Hostapd private command */
#ifdef SUPPORT_SOFTAP_SINGL_DISASSOC
#define CMD_HAPD_STA_DISASSOC		"HAPD_STA_DISASSOC"
#endif /* SUPPORT_SOFTAP_SINGL_DISASSOC */
#ifdef SUPPORT_SET_LPC
#define CMD_HAPD_LPC_ENABLED		"HAPD_LPC_ENABLED"
#endif /* SUPPORT_SET_LPC */
#ifdef SUPPORT_TRIGGER_HANG_EVENT
#define CMD_TEST_FORCE_HANG		"TEST_FORCE_HANG"
#endif /* SUPPORT_TRIGGER_HANG_EVENT */
#ifdef SUPPORT_LTECX
#define CMD_LTECX_SET		"LTECOEX"
#endif /* SUPPORT_LTECX */
#ifdef TEST_TX_POWER_CONTROL
#define CMD_TEST_SET_TX_POWER		"TEST_SET_TX_POWER"
#define CMD_TEST_GET_TX_POWER		"TEST_GET_TX_POWER"
#endif /* TEST_TX_POWER_CONTROL */
#define CMD_SARLIMIT_TX_CONTROL		"SET_TX_POWER_CALLING"
#ifdef SUPPORT_SET_TID
#define CMD_SET_TID		"SET_TID"
#define CMD_GET_TID		"GET_TID"
#endif /* SUPPORT_SET_TID */
#endif /* CUSTOMER_HW4_PRIVATE_CMD */
#define CMD_KEEP_ALIVE		"KEEPALIVE"
#ifdef SUPPORT_HIDDEN_AP
#define CMD_SET_HAPD_MAX_NUM_STA	"MAX_NUM_STA"
#define CMD_SET_HAPD_SSID		"HAPD_SSID"
#define CMD_SET_HAPD_HIDE_SSID		"HIDE_SSID"
#endif /* SUPPORT_HIDDEN_AP */
#ifdef PNO_SUPPORT
#define CMD_PNOSSIDCLR_SET	"PNOSSIDCLR"
#define CMD_PNOSETUP_SET	"PNOSETUP "
#define CMD_PNOENABLE_SET	"PNOFORCE"
#define CMD_PNODEBUG_SET	"PNODEBUG"
#define CMD_WLS_BATCHING	"WLS_BATCHING"
#endif /* PNO_SUPPORT */

#ifdef AUTOMOTIVE_FEATURE
#define	CMD_HAPD_MAC_FILTER	"HAPD_MAC_FILTER"
#endif /* AUTOMOTIVE_FEATURE */
#define CMD_ADDIE		"ADD_IE"
#define CMD_DELIE		"DEL_IE"

#if defined(CUSTOMER_HW4_PRIVATE_CMD) || defined(IGUANA_LEGACY_CHIPS)

#ifdef ROAM_API
#define CMD_ROAMTRIGGER_SET "SETROAMTRIGGER"
#define CMD_ROAMTRIGGER_GET "GETROAMTRIGGER"
#define CMD_ROAMDELTA_SET "SETROAMDELTA"
#define CMD_ROAMDELTA_GET "GETROAMDELTA"
#define CMD_ROAMSCANPERIOD_SET "SETROAMSCANPERIOD"
#define CMD_ROAMSCANPERIOD_GET "GETROAMSCANPERIOD"
#define CMD_FULLROAMSCANPERIOD_SET "SETFULLROAMSCANPERIOD"
#define CMD_FULLROAMSCANPERIOD_GET "GETFULLROAMSCANPERIOD"
#ifdef AUTOMOTIVE_FEATURE
#define CMD_COUNTRYREV_SET "SETCOUNTRYREV"
#define CMD_COUNTRYREV_GET "GETCOUNTRYREV"
#endif /* AUTOMOTIVE_FEATURE */
#endif /* ROAM_API */

#if defined(SUPPORT_RANDOM_MAC_SCAN)
#define ENABLE_RANDOM_MAC "ENABLE_RANDOM_MAC"
#define DISABLE_RANDOM_MAC "DISABLE_RANDOM_MAC"
#endif /* SUPPORT_RANDOM_MAC_SCAN */

#ifdef WES_SUPPORT
#define CMD_GETROAMSCANCONTROL "GETROAMSCANCONTROL"
#define CMD_SETROAMSCANCONTROL "SETROAMSCANCONTROL"
#define CMD_GETROAMSCANCHANNELS "GETROAMSCANCHANNELS"
#define CMD_SETROAMSCANCHANNELS "SETROAMSCANCHANNELS"

#define CMD_GETSCANCHANNELTIME "GETSCANCHANNELTIME"
#define CMD_SETSCANCHANNELTIME "SETSCANCHANNELTIME"
#define CMD_GETSCANUNASSOCTIME "GETSCANUNASSOCTIME"
#define CMD_SETSCANUNASSOCTIME "SETSCANUNASSOCTIME"
#define CMD_GETSCANPASSIVETIME "GETSCANPASSIVETIME"
#define CMD_SETSCANPASSIVETIME "SETSCANPASSIVETIME"
#define CMD_GETSCANHOMETIME "GETSCANHOMETIME"
#define CMD_SETSCANHOMETIME "SETSCANHOMETIME"
#define CMD_GETSCANHOMEAWAYTIME "GETSCANHOMEAWAYTIME"
#define CMD_SETSCANHOMEAWAYTIME "SETSCANHOMEAWAYTIME"
#define CMD_GETSCANNPROBES "GETSCANNPROBES"
#define CMD_SETSCANNPROBES "SETSCANNPROBES"
#define CMD_GETDFSSCANMODE "GETDFSSCANMODE"
#define CMD_SETDFSSCANMODE "SETDFSSCANMODE"
#define CMD_SETJOINPREFER "SETJOINPREFER"

#define CMD_SENDACTIONFRAME "SENDACTIONFRAME"
#define CMD_REASSOC "REASSOC"

#define CMD_GETWESMODE "GETWESMODE"
#define CMD_SETWESMODE "SETWESMODE"

#define CMD_GETOKCMODE "GETOKCMODE"
#define CMD_SETOKCMODE "SETOKCMODE"

#define CMD_OKC_SET_PMK         "SET_PMK"
#define CMD_OKC_ENABLE          "OKC_ENABLE"

typedef struct android_wifi_reassoc_params {
	unsigned char bssid[18];
	int channel;
} android_wifi_reassoc_params_t;

#define ANDROID_WIFI_REASSOC_PARAMS_SIZE sizeof(struct android_wifi_reassoc_params)

#define ANDROID_WIFI_ACTION_FRAME_SIZE 1040

typedef struct android_wifi_af_params {
	unsigned char bssid[18];
	int channel;
	int dwell_time;
	int len;
	unsigned char data[ANDROID_WIFI_ACTION_FRAME_SIZE];
} android_wifi_af_params_t;

#define ANDROID_WIFI_AF_PARAMS_SIZE sizeof(struct android_wifi_af_params)
#endif /* WES_SUPPORT */
#ifdef SUPPORT_AMPDU_MPDU_CMD
#define CMD_AMPDU_MPDU		"AMPDU_MPDU"
#endif /* SUPPORT_AMPDU_MPDU_CMD */

#define CMD_CHANGE_RL 	"CHANGE_RL"
#define CMD_RESTORE_RL  "RESTORE_RL"

#define CMD_SET_RMC_ENABLE			"SETRMCENABLE"
#define CMD_SET_RMC_TXRATE			"SETRMCTXRATE"
#define CMD_SET_RMC_ACTPERIOD		"SETRMCACTIONPERIOD"
#define CMD_SET_RMC_IDLEPERIOD		"SETRMCIDLEPERIOD"
#define CMD_SET_RMC_LEADER			"SETRMCLEADER"
#define CMD_SET_RMC_EVENT			"SETRMCEVENT"

#define CMD_SET_SCSCAN		"SETSINGLEANT"
#define CMD_GET_SCSCAN		"GETSINGLEANT"
#ifdef WLTDLS
#define CMD_TDLS_RESET "TDLS_RESET"
#endif /* WLTDLS */

#ifdef CONFIG_SILENT_ROAM
#define CMD_SROAM_TURN_ON	"SROAMTURNON"
#define CMD_SROAM_SET_INFO	"SROAMSETINFO"
#define CMD_SROAM_GET_INFO	"SROAMGETINFO"
#endif /* CONFIG_SILENT_ROAM */

#define CMD_SET_DISCONNECT_IES  "SET_DISCONNECT_IES"

#ifdef FCC_PWR_LIMIT_2G
#define CMD_GET_FCC_PWR_LIMIT_2G "GET_FCC_CHANNEL"
#define CMD_SET_FCC_PWR_LIMIT_2G "SET_FCC_CHANNEL"
/* CUSTOMER_HW4's value differs from BRCM FW value for enable/disable */
#define CUSTOMER_HW4_ENABLE		0
#define CUSTOMER_HW4_DISABLE	-1
#define CUSTOMER_HW4_EN_CONVERT(i)	(i += 1)
#endif /* FCC_PWR_LIMIT_2G */

#endif /* CUSTOMER_HW4_PRIVATE_CMD */

#ifdef WLFBT
#define CMD_GET_FTKEY      "GET_FTKEY"
#endif // endif

#ifdef WLAIBSS
#define CMD_SETIBSSTXFAILEVENT		"SETIBSSTXFAILEVENT"
#define CMD_GET_IBSS_PEER_INFO		"GETIBSSPEERINFO"
#define CMD_GET_IBSS_PEER_INFO_ALL	"GETIBSSPEERINFOALL"
#define CMD_SETIBSSROUTETABLE		"SETIBSSROUTETABLE"
#define CMD_SETIBSSAMPDU			"SETIBSSAMPDU"
#define CMD_SETIBSSANTENNAMODE		"SETIBSSANTENNAMODE"
#endif /* WLAIBSS */

#define CMD_ROAM_OFFLOAD			"SETROAMOFFLOAD"
#define CMD_INTERFACE_CREATE			"INTERFACE_CREATE"
#define CMD_INTERFACE_DELETE			"INTERFACE_DELETE"
#define CMD_GET_LINK_STATUS			"GETLINKSTATUS"

#if defined(DHD_ENABLE_BIGDATA_LOGGING)
#define CMD_GET_BSS_INFO            "GETBSSINFO"
#define CMD_GET_ASSOC_REJECT_INFO   "GETASSOCREJECTINFO"
#endif /* DHD_ENABLE_BIGDATA_LOGGING */
#define CMD_GET_STA_INFO   "GETSTAINFO"

/* related with CMD_GET_LINK_STATUS */
#define WL_ANDROID_LINK_VHT					0x01
#define WL_ANDROID_LINK_MIMO					0x02
#define WL_ANDROID_LINK_AP_VHT_SUPPORT		0x04
#define WL_ANDROID_LINK_AP_MIMO_SUPPORT	0x08

#ifdef P2PRESP_WFDIE_SRC
#define CMD_P2P_SET_WFDIE_RESP      "P2P_SET_WFDIE_RESP"
#define CMD_P2P_GET_WFDIE_RESP      "P2P_GET_WFDIE_RESP"
#endif /* P2PRESP_WFDIE_SRC */

#define CMD_DFS_AP_MOVE			"DFS_AP_MOVE"
#define CMD_WBTEXT_ENABLE		"WBTEXT_ENABLE"
#define CMD_WBTEXT_PROFILE_CONFIG	"WBTEXT_PROFILE_CONFIG"
#define CMD_WBTEXT_WEIGHT_CONFIG	"WBTEXT_WEIGHT_CONFIG"
#define CMD_WBTEXT_TABLE_CONFIG		"WBTEXT_TABLE_CONFIG"
#define CMD_WBTEXT_DELTA_CONFIG		"WBTEXT_DELTA_CONFIG"
#define CMD_WBTEXT_BTM_TIMER_THRESHOLD	"WBTEXT_BTM_TIMER_THRESHOLD"
#define CMD_WBTEXT_BTM_DELTA		"WBTEXT_BTM_DELTA"
#define CMD_WBTEXT_ESTM_ENABLE	"WBTEXT_ESTM_ENABLE"

#ifdef WBTEXT
#define CMD_WBTEXT_PROFILE_CONFIG	"WBTEXT_PROFILE_CONFIG"
#define CMD_WBTEXT_WEIGHT_CONFIG	"WBTEXT_WEIGHT_CONFIG"
#define CMD_WBTEXT_TABLE_CONFIG		"WBTEXT_TABLE_CONFIG"
#define CMD_WBTEXT_DELTA_CONFIG		"WBTEXT_DELTA_CONFIG"
#define DEFAULT_WBTEXT_PROFILE_A_V2		"a -70 -75 70 10 -75 -128 0 10"
#define DEFAULT_WBTEXT_PROFILE_B_V2		"b -60 -75 70 10 -75 -128 0 10"
#define DEFAULT_WBTEXT_PROFILE_A_V3		"a -70 -75 70 10 -75 -128 0 10"
#define DEFAULT_WBTEXT_PROFILE_B_V3		"b -60 -75 70 10 -75 -128 0 10"
#define DEFAULT_WBTEXT_WEIGHT_RSSI_A	"RSSI a 65"
#define DEFAULT_WBTEXT_WEIGHT_RSSI_B	"RSSI b 65"
#define DEFAULT_WBTEXT_WEIGHT_CU_A	"CU a 35"
#define DEFAULT_WBTEXT_WEIGHT_CU_B	"CU b 35"
#define DEFAULT_WBTEXT_WEIGHT_ESTM_DL_A	"ESTM_DL a 70"
#define DEFAULT_WBTEXT_WEIGHT_ESTM_DL_B	"ESTM_DL b 70"
#ifdef WBTEXT_SCORE_V2
#define DEFAULT_WBTEXT_TABLE_RSSI_A	"RSSI a 0 55 100 55 60 90 \
60 70 60 70 80 20 80 128 20"
#define DEFAULT_WBTEXT_TABLE_RSSI_B	"RSSI b 0 55 100 55 60 90 \
60 70 60 70 80 20 80 128 20"
#define DEFAULT_WBTEXT_TABLE_CU_A	"CU a 0 30 100 30 80 20 \
80 100 20"
#define DEFAULT_WBTEXT_TABLE_CU_B	"CU b 0 10 100 10 70 20 \
70 100 20"
#else
#define DEFAULT_WBTEXT_TABLE_RSSI_A	"RSSI a 0 55 100 55 60 90 \
60 65 70 65 70 50 70 128 20"
#define DEFAULT_WBTEXT_TABLE_RSSI_B	"RSSI b 0 55 100 55 60 90 \
60 65 70 65 70 50 70 128 20"
#define DEFAULT_WBTEXT_TABLE_CU_A	"CU a 0 30 100 30 50 90 \
50 60 70 60 80 50 80 100 20"
#define DEFAULT_WBTEXT_TABLE_CU_B	"CU b 0 10 100 10 25 90 \
25 40 70 40 70 50 70 100 20"
#endif /* WBTEXT_SCORE_V2 */
#endif /* WBTEXT */

#define BUFSZ 8
#define BUFSZN	BUFSZ + 1

#define _S(x) #x
#define S(x) _S(x)

#define  MAXBANDS    2  /**< Maximum #of bands */
#define BAND_2G_INDEX      0
#define BAND_5G_INDEX      0

typedef union {
	wl_roam_prof_band_v1_t v1;
	wl_roam_prof_band_v2_t v2;
	wl_roam_prof_band_v3_t v3;
} wl_roamprof_band_t;

#ifdef WLWFDS
#define CMD_ADD_WFDS_HASH	"ADD_WFDS_HASH"
#define CMD_DEL_WFDS_HASH	"DEL_WFDS_HASH"
#endif /* WLWFDS */

#ifdef SET_RPS_CPUS
#define CMD_RPSMODE  "RPSMODE"
#endif /* SET_RPS_CPUS */

#ifdef BT_WIFI_HANDOVER
#define CMD_TBOW_TEARDOWN "TBOW_TEARDOWN"
#endif /* BT_WIFI_HANDOVER */

#define CMD_MURX_BFE_CAP "MURX_BFE_CAP"

#ifdef SUPPORT_RSSI_SUM_REPORT
#define CMD_SET_RSSI_LOGGING				"SET_RSSI_LOGGING"
#define CMD_GET_RSSI_LOGGING				"GET_RSSI_LOGGING"
#define CMD_GET_RSSI_PER_ANT				"GET_RSSI_PER_ANT"
#endif /* SUPPORT_RSSI_SUM_REPORT */

#define CMD_GET_SNR							"GET_SNR"

#ifdef SUPPORT_AP_HIGHER_BEACONRATE
#define CMD_SET_AP_BEACONRATE				"SET_AP_BEACONRATE"
#define CMD_GET_AP_BASICRATE				"GET_AP_BASICRATE"
#endif /* SUPPORT_AP_HIGHER_BEACONRATE */

#ifdef SUPPORT_AP_RADIO_PWRSAVE
#define CMD_SET_AP_RPS						"SET_AP_RPS"
#define CMD_GET_AP_RPS						"GET_AP_RPS"
#define CMD_SET_AP_RPS_PARAMS				"SET_AP_RPS_PARAMS"
#endif /* SUPPORT_AP_RADIO_PWRSAVE */

#ifdef DHD_BANDSTEER
#define CMD_BANDSTEER   "BANDSTEER"
#define CMD_BANDSTEER_TRIGGER  "TRIGGER_BANDSTEER"
#endif /* DHD_BANDSTEER */
/* miracast related definition */
#define MIRACAST_MODE_OFF	0
#define MIRACAST_MODE_SOURCE	1
#define MIRACAST_MODE_SINK	2

#define CMD_CHANNEL_WIDTH "CHANNEL_WIDTH"
#define CMD_TRANSITION_DISABLE "TRANSITION_DISABLE"
#define CMD_SAE_PWE "SAE_PWE"
#define CMD_MAXASSOC "MAXASSOC"

#ifdef ENABLE_HOGSQS
#define CMD_AP_HOGSQS  "HOGSQS"
#endif /* ENABLE_HOGSQS */

#ifdef CONNECTION_STATISTICS
#define CMD_GET_CONNECTION_STATS	"GET_CONNECTION_STATS"

struct connection_stats {
	u32 txframe;
	u32 txbyte;
	u32 txerror;
	u32 rxframe;
	u32 rxbyte;
	u32 txfail;
	u32 txretry;
	u32 txretrie;
	u32 txrts;
	u32 txnocts;
	u32 txexptime;
	u32 txrate;
	u8	chan_idle;
};
#endif /* CONNECTION_STATISTICS */

#define CMD_SCAN_PROTECT_BSS "SCAN_PROTECT_BSS"

#ifdef SUPPORT_LQCM
#define CMD_SET_LQCM_ENABLE			"SET_LQCM_ENABLE"
#define CMD_GET_LQCM_REPORT			"GET_LQCM_REPORT"
#endif // endif

static LIST_HEAD(miracast_resume_list);
static u8 miracast_cur_mode;

#ifdef DHD_LOG_DUMP
#define CMD_NEW_DEBUG_PRINT_DUMP	"DEBUG_DUMP"
#define SUBCMD_UNWANTED			"UNWANTED"
#define SUBCMD_DISCONNECTED		"DISCONNECTED"
void dhd_log_dump_trigger(dhd_pub_t *dhdp, int subcmd);
#endif /* DHD_LOG_DUMP */

#ifdef DHD_STATUS_LOGGING
#define CMD_DUMP_STATUS_LOG		"DUMP_STAT_LOG"
#define CMD_QUERY_STATUS_LOG		"QUERY_STAT_LOG"
#endif /* DHD_STATUS_LOGGING */

#ifdef DHD_HANG_SEND_UP_TEST
#define CMD_MAKE_HANG  "MAKE_HANG"
#endif /* CMD_DHD_HANG_SEND_UP_TEST */
#ifdef DHD_DEBUG_UART
extern bool dhd_debug_uart_is_running(struct net_device *dev);
#endif	/* DHD_DEBUG_UART */

struct io_cfg {
	s8 *iovar;
	s32 param;
	u32 ioctl;
	void *arg;
	u32 len;
	struct list_head list;
};

typedef enum {
	HEAD_SAR_BACKOFF_DISABLE = -1,
	HEAD_SAR_BACKOFF_ENABLE = 0,
	GRIP_SAR_BACKOFF_DISABLE,
	GRIP_SAR_BACKOFF_ENABLE,
	NR_mmWave_SAR_BACKOFF_DISABLE,
	NR_mmWave_SAR_BACKOFF_ENABLE,
	NR_Sub6_SAR_BACKOFF_DISABLE,
	NR_Sub6_SAR_BACKOFF_ENABLE,
	SAR_BACKOFF_DISABLE_ALL
} sar_modes;

#if defined(BCMFW_ROAM_ENABLE)
#define CMD_SET_ROAMPREF	"SET_ROAMPREF"

#define MAX_NUM_SUITES		10
#define WIDTH_AKM_SUITE		8
#define JOIN_PREF_RSSI_LEN		0x02
#define JOIN_PREF_RSSI_SIZE		4	/* RSSI pref header size in bytes */
#define JOIN_PREF_WPA_HDR_SIZE		4 /* WPA pref header size in bytes */
#define JOIN_PREF_WPA_TUPLE_SIZE	12	/* Tuple size in bytes */
#define JOIN_PREF_MAX_WPA_TUPLES	16
#define MAX_BUF_SIZE		(JOIN_PREF_RSSI_SIZE + JOIN_PREF_WPA_HDR_SIZE +	\
				           (JOIN_PREF_WPA_TUPLE_SIZE * JOIN_PREF_MAX_WPA_TUPLES))
#endif /* BCMFW_ROAM_ENABLE */

#define CMD_DEBUG_VERBOSE          "DEBUG_VERBOSE"
#ifdef WL_NATOE

#define CMD_NATOE		"NATOE"

#define NATOE_MAX_PORT_NUM	65535

/* natoe command info structure */
typedef struct wl_natoe_cmd_info {
	uint8  *command;        /* pointer to the actual command */
	uint16 tot_len;        /* total length of the command */
	uint16 bytes_written;  /* Bytes written for get response */
} wl_natoe_cmd_info_t;

typedef struct wl_natoe_sub_cmd wl_natoe_sub_cmd_t;
typedef int (natoe_cmd_handler_t)(struct net_device *dev,
		const wl_natoe_sub_cmd_t *cmd, char *command, wl_natoe_cmd_info_t *cmd_info);

struct wl_natoe_sub_cmd {
	char *name;
	uint8  version;              /* cmd  version */
	uint16 id;                   /* id for the dongle f/w switch/case */
	uint16 type;                 /* base type of argument */
	natoe_cmd_handler_t *handler; /* cmd handler  */
};

#define WL_ANDROID_NATOE_FUNC(suffix) wl_android_natoe_subcmd_ ##suffix
static int wl_android_process_natoe_cmd(struct net_device *dev,
		char *command, int total_len);
static int wl_android_natoe_subcmd_enable(struct net_device *dev,
		const wl_natoe_sub_cmd_t *cmd, char *command, wl_natoe_cmd_info_t *cmd_info);
static int wl_android_natoe_subcmd_config_ips(struct net_device *dev,
		const wl_natoe_sub_cmd_t *cmd, char *command, wl_natoe_cmd_info_t *cmd_info);
static int wl_android_natoe_subcmd_config_ports(struct net_device *dev,
		const wl_natoe_sub_cmd_t *cmd, char *command, wl_natoe_cmd_info_t *cmd_info);
static int wl_android_natoe_subcmd_dbg_stats(struct net_device *dev,
		const wl_natoe_sub_cmd_t *cmd, char *command, wl_natoe_cmd_info_t *cmd_info);
static int wl_android_natoe_subcmd_tbl_cnt(struct net_device *dev,
		const wl_natoe_sub_cmd_t *cmd, char *command, wl_natoe_cmd_info_t *cmd_info);

static const wl_natoe_sub_cmd_t natoe_cmd_list[] = {
	/* wl natoe enable [0/1] or new: "wl natoe [0/1]" */
	{"enable", 0x01, WL_NATOE_CMD_ENABLE,
	IOVT_BUFFER, WL_ANDROID_NATOE_FUNC(enable)
	},
	{"config_ips", 0x01, WL_NATOE_CMD_CONFIG_IPS,
	IOVT_BUFFER, WL_ANDROID_NATOE_FUNC(config_ips)
	},
	{"config_ports", 0x01, WL_NATOE_CMD_CONFIG_PORTS,
	IOVT_BUFFER, WL_ANDROID_NATOE_FUNC(config_ports)
	},
	{"stats", 0x01, WL_NATOE_CMD_DBG_STATS,
	IOVT_BUFFER, WL_ANDROID_NATOE_FUNC(dbg_stats)
	},
	{"tbl_cnt", 0x01, WL_NATOE_CMD_TBL_CNT,
	IOVT_BUFFER, WL_ANDROID_NATOE_FUNC(tbl_cnt)
	},
	{NULL, 0, 0, 0, NULL}
};

#endif /* WL_NATOE */

static int
wl_android_get_channel_list(struct net_device *dev, char *command, int total_len);

#ifdef SET_PCIE_IRQ_CPU_CORE
#define CMD_PCIE_IRQ_CORE	"PCIE_IRQ_CORE"
#endif /* SET_PCIE_IRQ_CPU_CORE */

#ifdef WLADPS_PRIVATE_CMD
#define CMD_SET_ADPS	"SET_ADPS"
#define CMD_GET_ADPS	"GET_ADPS"
#endif /* WLADPS_PRIVATE_CMD */

#ifdef DHD_PKT_LOGGING
#define CMD_PKTLOG_FILTER_ENABLE	"PKTLOG_FILTER_ENABLE"
#define CMD_PKTLOG_FILTER_DISABLE	"PKTLOG_FILTER_DISABLE"
#define CMD_PKTLOG_FILTER_PATTERN_ENABLE	"PKTLOG_FILTER_PATTERN_ENABLE"
#define CMD_PKTLOG_FILTER_PATTERN_DISABLE	"PKTLOG_FILTER_PATTERN_DISABLE"
#define CMD_PKTLOG_FILTER_ADD	"PKTLOG_FILTER_ADD"
#define CMD_PKTLOG_FILTER_DEL	"PKTLOG_FILTER_DEL"
#define CMD_PKTLOG_FILTER_INFO	"PKTLOG_FILTER_INFO"
#define CMD_PKTLOG_START	"PKTLOG_START"
#define CMD_PKTLOG_STOP		"PKTLOG_STOP"
#define CMD_PKTLOG_FILTER_EXIST "PKTLOG_FILTER_EXIST"
#define CMD_PKTLOG_MINMIZE_ENABLE	"PKTLOG_MINMIZE_ENABLE"
#define CMD_PKTLOG_MINMIZE_DISABLE	"PKTLOG_MINMIZE_DISABLE"
#define CMD_PKTLOG_CHANGE_SIZE	"PKTLOG_CHANGE_SIZE"
#endif /* DHD_PKT_LOGGING */

#ifdef DHD_EVENT_LOG_FILTER
#define CMD_EWP_FILTER		"EWP_FILTER"
#endif /* DHD_EVENT_LOG_FILTER */

#ifdef WL_BCNRECV
#define CMD_BEACON_RECV "BEACON_RECV"
#endif /* WL_BCNRECV */
#ifdef WL_CAC_TS
#define CMD_CAC_TSPEC "CAC_TSPEC"
#endif /* WL_CAC_TS */
#ifdef WL_CHAN_UTIL
#define CMD_GET_CHAN_UTIL "GET_CU"
#endif /* WL_CHAN_UTIL */

/* drv command info structure */
typedef struct wl_drv_cmd_info {
	uint8  *command;        /* pointer to the actual command */
	uint16 tot_len;         /* total length of the command */
	uint16 bytes_written;   /* Bytes written for get response */
} wl_drv_cmd_info_t;

typedef struct wl_drv_sub_cmd wl_drv_sub_cmd_t;
typedef int (drv_cmd_handler_t)(struct net_device *dev,
		const wl_drv_sub_cmd_t *cmd, char *command, wl_drv_cmd_info_t *cmd_info);

struct wl_drv_sub_cmd {
	char *name;
	uint8  version;              /* cmd  version */
	uint16 id;                   /* id for the dongle f/w switch/case */
	uint16 type;                 /* base type of argument */
	drv_cmd_handler_t *handler;  /* cmd handler  */
};

#ifdef WL_MBO

#define CMD_MBO		"MBO"
enum {
	WL_MBO_CMD_NON_CHAN_PREF = 1,
	WL_MBO_CMD_CELL_DATA_CAP = 2
};
#define WL_ANDROID_MBO_FUNC(suffix) wl_android_mbo_subcmd_ ##suffix

static int wl_android_process_mbo_cmd(struct net_device *dev,
		char *command, int total_len);
static int wl_android_mbo_subcmd_cell_data_cap(struct net_device *dev,
		const wl_drv_sub_cmd_t *cmd, char *command, wl_drv_cmd_info_t *cmd_info);
static int wl_android_mbo_subcmd_non_pref_chan(struct net_device *dev,
		const wl_drv_sub_cmd_t *cmd, char *command, wl_drv_cmd_info_t *cmd_info);

static const wl_drv_sub_cmd_t mbo_cmd_list[] = {
	{"non_pref_chan", 0x01, WL_MBO_CMD_NON_CHAN_PREF,
	IOVT_BUFFER, WL_ANDROID_MBO_FUNC(non_pref_chan)
	},
	{"cell_data_cap", 0x01, WL_MBO_CMD_CELL_DATA_CAP,
	IOVT_BUFFER, WL_ANDROID_MBO_FUNC(cell_data_cap)
	},
	{NULL, 0, 0, 0, NULL}
};

#endif /* WL_MBO */

#ifdef WL_GENL
static s32 wl_genl_handle_msg(struct sk_buff *skb, struct genl_info *info);
static int wl_genl_init(void);
static int wl_genl_deinit(void);

extern struct net init_net;
/* attribute policy: defines which attribute has which type (e.g int, char * etc)
 * possible values defined in net/netlink.h
 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 2, 0))
static struct nla_policy wl_genl_policy[BCM_GENL_ATTR_MAX + 1] = {
	[BCM_GENL_ATTR_STRING] = { .type = NLA_NUL_STRING },
	[BCM_GENL_ATTR_MSG] = { .type = NLA_BINARY },
};
#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(5, 2, 0)) */

#define WL_GENL_VER 1
/* family definition */
static struct genl_family wl_genl_family = {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0))
	.id = GENL_ID_GENERATE,    /* Genetlink would generate the ID */
#endif // endif
	.hdrsize = 0,
	.name = "bcm-genl",        /* Netlink I/F for Android */
	.version = WL_GENL_VER,     /* Version Number */
	.maxattr = BCM_GENL_ATTR_MAX,
};

/* commands: mapping between the command enumeration and the actual function */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0))
struct genl_ops wl_genl_ops[] = {
	{
	.cmd = BCM_GENL_CMD_MSG,
	.flags = 0,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 2, 0))
	.policy = wl_genl_policy,
#else
	.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(5, 2, 0)) */
	.doit = wl_genl_handle_msg,
	.dumpit = NULL,
	},
};
#else
struct genl_ops wl_genl_ops = {
	.cmd = BCM_GENL_CMD_MSG,
	.flags = 0,
	.policy = wl_genl_policy,
	.doit = wl_genl_handle_msg,
	.dumpit = NULL,

};
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0) */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0))
static struct genl_multicast_group wl_genl_mcast[] = {
	 { .name = "bcm-genl-mcast", },
};
#else
static struct genl_multicast_group wl_genl_mcast = {
	.id = GENL_ID_GENERATE,    /* Genetlink would generate the ID */
	.name = "bcm-genl-mcast",
};
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0) */
#endif /* WL_GENL */

#ifdef SUPPORT_LQCM
#define LQCM_ENAB_MASK			0x000000FF	/* LQCM enable flag mask */
#define LQCM_TX_INDEX_MASK		0x0000FF00	/* LQCM tx index mask */
#define LQCM_RX_INDEX_MASK		0x00FF0000	/* LQCM rx index mask */

#define LQCM_TX_INDEX_SHIFT		8	/* LQCM tx index shift */
#define LQCM_RX_INDEX_SHIFT		16	/* LQCM rx index shift */
#endif /* SUPPORT_LQCM */

#ifdef DHD_SEND_HANG_PRIVCMD_ERRORS
#define NUMBER_SEQUENTIAL_PRIVCMD_ERRORS	7
static int priv_cmd_errors = 0;
#endif /* DHD_SEND_HANG_PRIVCMD_ERRORS */

/**
 * Extern function declarations (TODO: move them to dhd_linux.h)
 */
int dhd_net_bus_devreset(struct net_device *dev, uint8 flag);
int dhd_dev_init_ioctl(struct net_device *dev);
#ifdef WL_CFG80211
int wl_cfg80211_get_p2p_dev_addr(struct net_device *net, struct ether_addr *p2pdev_addr);
int wl_cfg80211_set_btcoex_dhcp(struct net_device *dev, dhd_pub_t *dhd, char *command);
#ifdef WES_SUPPORT
int wl_cfg80211_set_wes_mode(int mode);
int wl_cfg80211_get_wes_mode(void);
#endif /* WES_SUPPORT */
#else
int wl_cfg80211_get_p2p_dev_addr(struct net_device *net, struct ether_addr *p2pdev_addr)
{ return 0; }
int wl_cfg80211_set_p2p_noa(struct net_device *net, char* buf, int len)
{ return 0; }
int wl_cfg80211_get_p2p_noa(struct net_device *net, char* buf, int len)
{ return 0; }
int wl_cfg80211_set_p2p_ps(struct net_device *net, char* buf, int len)
{ return 0; }
int wl_cfg80211_set_p2p_ecsa(struct net_device *net, char* buf, int len)
{ return 0; }
int wl_cfg80211_increase_p2p_bw(struct net_device *net, char* buf, int len)
{ return 0; }
#endif /* WK_CFG80211 */
#ifdef WBTEXT
static int wl_android_wbtext(struct net_device *dev, char *command, int total_len);
static int wl_cfg80211_wbtext_btm_timer_threshold(struct net_device *dev,
	char *command, int total_len);
static int wl_cfg80211_wbtext_btm_delta(struct net_device *dev,
	char *command, int total_len);
static int wl_cfg80211_wbtext_estm_enable(struct net_device *dev,
	char *command, int total_len);
static int wlc_wbtext_get_roam_prof(struct net_device *ndev, wl_roamprof_band_t *rp,
	uint8 band, uint8 *roam_prof_ver, uint8 *roam_prof_size);
#endif /* WBTEXT */
#ifdef WES_SUPPORT
/* wl_roam.c */
extern int get_roamscan_mode(struct net_device *dev, int *mode);
extern int set_roamscan_mode(struct net_device *dev, int mode);
extern int get_roamscan_channel_list(struct net_device *dev,
	unsigned char channels[], int n_channels);
extern int set_roamscan_channel_list(struct net_device *dev, unsigned char n,
	unsigned char channels[], int ioctl_ver);
#endif /* WES_SUPPORT */
#ifdef ROAM_CHANNEL_CACHE
extern void wl_update_roamscan_cache_by_band(struct net_device *dev, int band);
#endif /* ROAM_CHANNEL_CACHE */

#ifdef ENABLE_4335BT_WAR
extern int bcm_bt_lock(int cookie);
extern void bcm_bt_unlock(int cookie);
static int lock_cookie_wifi = 'W' | 'i'<<8 | 'F'<<16 | 'i'<<24;	/* cookie is "WiFi" */
#endif /* ENABLE_4335BT_WAR */

extern bool ap_fw_loaded;
extern char iface_name[IFNAMSIZ];
#ifdef DHD_PM_CONTROL_FROM_FILE
extern bool g_pm_control;
#endif	/* DHD_PM_CONTROL_FROM_FILE */

/* private command support for restoring roam/scan parameters */
#ifdef SUPPORT_RESTORE_SCAN_PARAMS
#define CMD_RESTORE_SCAN_PARAMS "RESTORE_SCAN_PARAMS"

typedef int (*PRIV_CMD_HANDLER) (struct net_device *dev, char *command);
typedef int (*PRIV_CMD_HANDLER_WITH_LEN) (struct net_device *dev, char *command, int total_len);

enum {
	RESTORE_TYPE_UNSPECIFIED = 0,
	RESTORE_TYPE_PRIV_CMD = 1,
	RESTORE_TYPE_PRIV_CMD_WITH_LEN = 2
};

typedef struct android_restore_scan_params {
	char command[64];
	int parameter;
	int cmd_type;
	union {
		PRIV_CMD_HANDLER cmd_handler;
		PRIV_CMD_HANDLER_WITH_LEN cmd_handler_w_len;
	};
} android_restore_scan_params_t;

/* function prototypes of private command handler */
static int wl_android_set_roam_trigger(struct net_device *dev, char* command);
int wl_android_set_roam_delta(struct net_device *dev, char* command);
int wl_android_set_roam_scan_period(struct net_device *dev, char* command);
int wl_android_set_full_roam_scan_period(struct net_device *dev, char* command, int total_len);
int wl_android_set_roam_scan_control(struct net_device *dev, char *command);
int wl_android_set_scan_channel_time(struct net_device *dev, char *command);
int wl_android_set_scan_home_time(struct net_device *dev, char *command);
int wl_android_set_scan_home_away_time(struct net_device *dev, char *command);
int wl_android_set_scan_nprobes(struct net_device *dev, char *command);
static int wl_android_set_band(struct net_device *dev, char *command);
int wl_android_set_scan_dfs_channel_mode(struct net_device *dev, char *command);
int wl_android_set_wes_mode(struct net_device *dev, char *command);
int wl_android_set_okc_mode(struct net_device *dev, char *command);

/* default values */
#ifdef ROAM_API
#define DEFAULT_ROAM_TIRGGER	-75
#define DEFAULT_ROAM_DELTA	10
#define DEFAULT_ROAMSCANPERIOD	10
#define DEFAULT_FULLROAMSCANPERIOD_SET	120
#endif /* ROAM_API */
#ifdef WES_SUPPORT
#define DEFAULT_ROAMSCANCONTROL	0
#define DEFAULT_SCANCHANNELTIME	40
#ifdef BCM4361_CHIP
#define DEFAULT_SCANHOMETIME	60
#else
#define DEFAULT_SCANHOMETIME	45
#endif /* BCM4361_CHIP */
#define DEFAULT_SCANHOMEAWAYTIME	100
#define DEFAULT_SCANPROBES	2
#define DEFAULT_DFSSCANMODE	1
#define DEFAULT_WESMODE		0
#define DEFAULT_OKCMODE		1
#endif /* WES_SUPPORT */
#define DEFAULT_BAND		0
#ifdef WBTEXT
#define DEFAULT_WBTEXT_ENABLE	1
#endif /* WBTEXT */

/* restoring parameter list, please don't change order */
static android_restore_scan_params_t restore_params[] =
{
/* wbtext need to be disabled while updating roam/scan parameters */
#ifdef WBTEXT
	{ CMD_WBTEXT_ENABLE, 0, RESTORE_TYPE_PRIV_CMD_WITH_LEN,
		.cmd_handler_w_len = wl_android_wbtext},
#endif /* WBTEXT */
#ifdef ROAM_API
	{ CMD_ROAMTRIGGER_SET, DEFAULT_ROAM_TIRGGER,
		RESTORE_TYPE_PRIV_CMD, .cmd_handler = wl_android_set_roam_trigger},
	{ CMD_ROAMDELTA_SET, DEFAULT_ROAM_DELTA,
		RESTORE_TYPE_PRIV_CMD, .cmd_handler = wl_android_set_roam_delta},
	{ CMD_ROAMSCANPERIOD_SET, DEFAULT_ROAMSCANPERIOD,
		RESTORE_TYPE_PRIV_CMD, .cmd_handler = wl_android_set_roam_scan_period},
	{ CMD_FULLROAMSCANPERIOD_SET, DEFAULT_FULLROAMSCANPERIOD_SET,
		RESTORE_TYPE_PRIV_CMD_WITH_LEN,
		.cmd_handler_w_len = wl_android_set_full_roam_scan_period},
#endif /* ROAM_API */
#ifdef WES_SUPPORT
	{ CMD_SETROAMSCANCONTROL, DEFAULT_ROAMSCANCONTROL,
		RESTORE_TYPE_PRIV_CMD, .cmd_handler = wl_android_set_roam_scan_control},
	{ CMD_SETSCANCHANNELTIME, DEFAULT_SCANCHANNELTIME,
		RESTORE_TYPE_PRIV_CMD, .cmd_handler = wl_android_set_scan_channel_time},
	{ CMD_SETSCANHOMETIME, DEFAULT_SCANHOMETIME,
		RESTORE_TYPE_PRIV_CMD, .cmd_handler = wl_android_set_scan_home_time},
	{ CMD_GETSCANHOMEAWAYTIME, DEFAULT_SCANHOMEAWAYTIME,
		RESTORE_TYPE_PRIV_CMD, .cmd_handler = wl_android_set_scan_home_away_time},
	{ CMD_SETSCANNPROBES, DEFAULT_SCANPROBES,
		RESTORE_TYPE_PRIV_CMD, .cmd_handler = wl_android_set_scan_nprobes},
	{ CMD_SETDFSSCANMODE, DEFAULT_DFSSCANMODE,
		RESTORE_TYPE_PRIV_CMD, .cmd_handler = wl_android_set_scan_dfs_channel_mode},
	{ CMD_SETWESMODE, DEFAULT_WESMODE,
		RESTORE_TYPE_PRIV_CMD, .cmd_handler = wl_android_set_wes_mode},
	{ CMD_SETOKCMODE, DEFAULT_OKCMODE,
		RESTORE_TYPE_PRIV_CMD, .cmd_handler = wl_android_set_okc_mode},
#endif /* WES_SUPPORT */
	{ CMD_SETBAND, DEFAULT_BAND,
		RESTORE_TYPE_PRIV_CMD, .cmd_handler = wl_android_set_band},
#ifdef WBTEXT
	{ CMD_WBTEXT_ENABLE, DEFAULT_WBTEXT_ENABLE,
		RESTORE_TYPE_PRIV_CMD_WITH_LEN, .cmd_handler_w_len = wl_android_wbtext},
#endif /* WBTEXT */
	{ "\0", 0, RESTORE_TYPE_UNSPECIFIED, .cmd_handler = NULL}
};
#endif /* SUPPORT_RESTORE_SCAN_PARAMS */

/**
 * Local (static) functions and variables
 */

/* Initialize g_wifi_on to 1 so dhd_bus_start will be called for the first
 * time (only) in dhd_open, subsequential wifi on will be handled by
 * wl_android_wifi_on
 */
static int g_wifi_on = TRUE;

/**
 * Local (static) function definitions
 */

static int
wl_android_set_channel_width(struct net_device *dev, char *command, int total_len)
{
	u32 channel_width = 0;
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct bcm_cfg80211 *cfg = (struct bcm_cfg80211 *)wiphy_priv(wdev->wiphy);
	command = (command + strlen(CMD_CHANNEL_WIDTH));
	command++;
	channel_width = bcm_atoi(command);
	if (channel_width == 80)
		wl_set_chanwidth_by_netdev(cfg, dev, WL_CHANSPEC_BW_80);
	else if (channel_width == 40)
		wl_set_chanwidth_by_netdev(cfg, dev, WL_CHANSPEC_BW_40);
	else if (channel_width == 20)
		wl_set_chanwidth_by_netdev(cfg, dev, WL_CHANSPEC_BW_20);
	else
		return 0;
	DHD_INFO(("%s : channel width = %d\n", __FUNCTION__, channel_width));
	return 0;
}

#ifdef ENABLE_HOGSQS
#define M_HOGSQS_DURATION (M_HOGSQS_CFG + 0x2)
#define M_HOGSQS_DUR_THR (M_HOGSQS_CFG + 0x4)
#define M_HOGSQS_STAT (M_HOGSQS_CFG + 0x6)
#define M_HOGSQS_TXCFE_DET_CNT (M_HOGSQS_CFG + 0xe)
static int
wl_android_hogsqs(struct net_device *dev, char *command, int total_len)
{
	int ret = 0, bytes_written = 0;
	s32 value = 0;
	uint32 reg = 0;
	uint32 set_val = 0;
	uint32 set_val2 = 0;
	char *pos = command;
	char *pos2 = NULL;

	if (*(command + strlen(CMD_AP_HOGSQS)) == '\0') {
		DHD_ERROR(("%s: Error argument is required on %s \n", __FUNCTION__, CMD_AP_HOGSQS));
		return -EINVAL;
	} else {
		pos = pos + strlen(CMD_AP_HOGSQS) + 1;
		if (!strncmp(pos, "cfg", strlen("cfg"))) {
			reg = M_HOGSQS_CFG;
			pos2 = pos + strlen("cfg");
		} else if (!strncmp(pos, "duration", strlen("duration"))) {
			reg = M_HOGSQS_DURATION;
			pos2 = pos + strlen("duration");
		} else if (!strncmp(pos, "durth", strlen("durth"))) {
			reg = M_HOGSQS_DUR_THR;
			pos2 = pos + strlen("durth");
		} else if (!strncmp(pos, "count", strlen("count"))) {
			reg = M_HOGSQS_TXCFE_DET_CNT;
			pos2 = pos + strlen("count");
		} else {
			DHD_ERROR(("%s: Error wrong argument is on %s \n", __FUNCTION__,
			CMD_AP_HOGSQS));
			return -EINVAL;
		}
		value = reg;

		if (*pos2 == '\0') {
		/* Get operation */
			ret = wldev_iovar_getint(dev, "hogsqs", &value);
			if (ret) {
				DHD_ERROR(("%s: Failed to get hogsqs\n", __FUNCTION__));
				return -EINVAL;
			}

			if (reg == M_HOGSQS_TXCFE_DET_CNT)
				bytes_written = snprintf(command, total_len, " %s 0x%x/0x%x",
					CMD_AP_HOGSQS, (value&0x00FF), ((value&0xFF00)>> 8));
			else
				bytes_written = snprintf(command, total_len, " %s 0x%x",
						CMD_AP_HOGSQS, value);

			return bytes_written;
		} else {
		/* Set operation */
			pos2 = pos2 + 1;
			set_val = (uint32)simple_strtol(pos2, NULL, 0);

			set_val2 = (reg & 0xFFFF) << 16;
			set_val2 |= set_val;

			ret = wldev_iovar_setint(dev, "hogsqs", set_val2);
			if (ret != BCME_OK) {
				DHD_ERROR(("%s: hogsqs set returned (%d)\n", __FUNCTION__, ret));
				return BCME_ERROR;
			}
		}
	}
	return 0;
}
#endif /* ENABLE_HOGSQS */

/* The wl_android_scan_protect_bss function does both SET/GET based on parameters passed */
static int wl_android_scan_protect_bss(struct net_device * dev, char * command, int total_len)
{
       int ret = 0, result = 0, bytes_written = 0;

       if (*(command + strlen(CMD_SCAN_PROTECT_BSS)) == '\0') {
               ret = wldev_iovar_getint(dev, "scan_protect_bss", &result);
               if (ret) {
                       DHD_ERROR(("%s: Failed to get scan_protect_bss\n", __FUNCTION__));
                       return ret;
               }
               bytes_written = snprintf(command, total_len, "%s %d", CMD_SCAN_PROTECT_BSS, result);
               return bytes_written;
       }
       command = (command + strlen(CMD_SCAN_PROTECT_BSS));
       command++;
       result = bcm_atoi(command);

       DHD_INFO(("%s : scan_protect_bss = %d\n", __FUNCTION__, result));
       ret = wldev_iovar_setint(dev, "scan_protect_bss", result);
       if (ret) {
               DHD_ERROR(("%s: Failed to set result to %d\n", __FUNCTION__, result));
               return ret;
       }
       return 0;
}

#ifdef DHD_BANDSTEER
static int
wl_android_set_bandsteer(struct net_device *dev, char *command, int total_len)
{
	char *iftype;
	char *token1, *context1 = NULL;
	int val;
	int ret = 0;

	struct wireless_dev *__wdev = (struct wireless_dev *)(dev)->ieee80211_ptr;
	struct bcm_cfg80211 *cfg = (struct bcm_cfg80211 *)wiphy_priv(__wdev->wiphy);

	command = (command + strlen(CMD_BANDSTEER));
	command++;
	token1 = command;

	iftype = bcmstrtok(&token1, " ", context1);
	val = bcm_atoi(token1);

	if (val < 0 || val > 1) {
		DHD_ERROR(("%s : invalid val\n", __FUNCTION__));
		return -1;
	}

	if (!strncmp(iftype, "p2p", 3)) {
		cfg->ap_bs = 0;
		cfg->p2p_bs = 1;

		if (val) {
			ret = dhd_bandsteer_module_init(dev, cfg->ap_bs, cfg->p2p_bs);
			if (ret == BCME_ERROR) {
				DHD_ERROR(("%s: Failed to enable %s bandsteer\n", __FUNCTION__,
				cfg->ap_bs ? "ap":"p2p"));
				return ret;
			} else {
				DHD_ERROR(("%s: Successfully enabled %s bandsteer\n", __FUNCTION__,
				cfg->ap_bs ? "ap":"p2p"));
			}
		} else {
			ret = dhd_bandsteer_module_deinit(dev, cfg->ap_bs, cfg->p2p_bs);
			if (ret == BCME_ERROR) {
				DHD_ERROR(("%s: Failed to disable %s bandsteer\n", __FUNCTION__,
				cfg->ap_bs ? "ap":"p2p"));
				return ret;
			} else {
				DHD_ERROR(("%s: Successfully disabled %s bandsteer\n", __FUNCTION__,
				cfg->ap_bs ? "ap":"p2p"));
			}
		}
	} else if (!strncmp(iftype, "ap", 2)) {
		cfg->ap_bs = 1;
		cfg->p2p_bs = 0;

		if (val) {
			ret = dhd_bandsteer_module_init(dev, cfg->ap_bs, cfg->p2p_bs);
			if (ret == BCME_ERROR) {
				DHD_ERROR(("%s: Failed to enable %s bandsteer\n", __FUNCTION__,
				cfg->ap_bs ? "ap":"p2p"));
				return ret;
			} else {
				DHD_ERROR(("%s: Successfully enabled %s bandsteer\n", __FUNCTION__,
				cfg->ap_bs ? "ap":"p2p"));
			}
		} else {
			ret = dhd_bandsteer_module_deinit(dev, cfg->ap_bs, cfg->p2p_bs);
			if (ret == BCME_ERROR) {
				DHD_ERROR(("%s: Failed to disable %s bandsteer\n", __FUNCTION__,
				cfg->ap_bs ? "ap":"p2p"));
				return ret;
			} else {
				DHD_ERROR(("%s: Successfully disabled %s bandsteer\n", __FUNCTION__,
				cfg->ap_bs ? "ap":"p2p"));
			}
		}
	} else if (!strncmp(iftype, "1", 1)) {
		cfg->ap_bs = 1;
		cfg->p2p_bs = 1;
		ret = dhd_bandsteer_module_init(dev, cfg->ap_bs, cfg->p2p_bs);
		if (ret == BCME_ERROR) {
			DHD_ERROR(("%s: Failed to enable bandsteer\n", __FUNCTION__));
			return ret;
		} else {
			DHD_ERROR(("%s: Successfully enabled bandsteer\n", __FUNCTION__));
		}
	} else if (!strncmp(iftype, "0", 1)) {
		cfg->ap_bs = 1;
		cfg->p2p_bs = 1;
		ret = dhd_bandsteer_module_deinit(dev, cfg->ap_bs, cfg->p2p_bs);
		if (ret == BCME_ERROR) {
			DHD_ERROR(("%s: Failed to diable bandsteer\n", __FUNCTION__));
			return ret;
		} else {
			DHD_ERROR(("%s: Successfully disabled  bandsteer\n", __FUNCTION__));
		}
	} else {
		DHD_ERROR(("%s: Invalid bandsteer iftype\n", __FUNCTION__));
		return -1;
	}
	return ret;
}
#endif /* DHD_BANDSTEER */

static int
wl_android_set_maxassoc_limit(struct net_device *dev, char *command, int total_len)
{
	int ret = 0, max_assoc = 0, bytes_written = 0;

	if (*(command + strlen(CMD_MAXASSOC)) == '\0') {
		ret = wldev_iovar_getint(dev, "maxassoc", &max_assoc);
		if (ret) {
			DHD_ERROR(("%s: Failed to get maxassoc limit\n", __FUNCTION__));
			return ret;
		}
		bytes_written = snprintf(command, total_len, "%s %d", CMD_MAXASSOC, max_assoc);
		return bytes_written;
	}
	command = (command + strlen(CMD_MAXASSOC));
	command++;
	max_assoc = bcm_atoi(command);

	DHD_INFO(("%s : maxassoc limit = %d\n", __FUNCTION__, max_assoc));
	ret = wldev_iovar_setint(dev, "maxassoc", max_assoc);
	if (ret) {
		DHD_ERROR(("%s: Failed to set maxassoc limit to %d\n", __FUNCTION__, max_assoc));
		return ret;
	}
	return 0;
}

#ifdef WLWFDS
static int wl_android_set_wfds_hash(
	struct net_device *dev, char *command, bool enable)
{
	int error = 0;
	wl_p2p_wfds_hash_t *wfds_hash = NULL;
	char *smbuf = NULL;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	smbuf = (char *)MALLOC(cfg->osh, WLC_IOCTL_MAXLEN);
	if (smbuf == NULL) {
		DHD_ERROR(("wl_android_set_wfds_hash: failed to allocated memory %d bytes\n",
			WLC_IOCTL_MAXLEN));
		return -ENOMEM;
	}

	if (enable) {
		wfds_hash = (wl_p2p_wfds_hash_t *)(command + strlen(CMD_ADD_WFDS_HASH) + 1);
		error = wldev_iovar_setbuf(dev, "p2p_add_wfds_hash", wfds_hash,
			sizeof(wl_p2p_wfds_hash_t), smbuf, WLC_IOCTL_MAXLEN, NULL);
	}
	else {
		wfds_hash = (wl_p2p_wfds_hash_t *)(command + strlen(CMD_DEL_WFDS_HASH) + 1);
		error = wldev_iovar_setbuf(dev, "p2p_del_wfds_hash", wfds_hash,
			sizeof(wl_p2p_wfds_hash_t), smbuf, WLC_IOCTL_MAXLEN, NULL);
	}

	if (error) {
		DHD_ERROR(("wl_android_set_wfds_hash: failed to %s, error=%d\n", command, error));
	}

	if (smbuf) {
		MFREE(cfg->osh, smbuf, WLC_IOCTL_MAXLEN);
	}
	return error;
}
#endif /* WLWFDS */

#ifdef AUTOMOTIVE_FEATURE
static int wl_android_get_link_speed(struct net_device *net, char *command, int total_len)
{
	int link_speed;
	int bytes_written;
	int error;

	error = wldev_get_link_speed(net, &link_speed);
	if (error) {
		DHD_ERROR(("Get linkspeed failed \n"));
		return -1;
	}

	/* Convert Kbps to Android Mbps */
	link_speed = link_speed / 1000;
	bytes_written = snprintf(command, total_len, "LinkSpeed %d", link_speed);
	DHD_INFO(("wl_android_get_link_speed: command result is %s\n", command));
	return bytes_written;
}

static int wl_android_get_rssi(struct net_device *net, char *command, int total_len)
{
	wlc_ssid_t ssid = {0, {0}};
	int bytes_written = 0;
	int error = 0;
	scb_val_t scbval;
	char *delim = NULL;
	struct net_device *target_ndev = net;
#ifdef WL_VIRTUAL_APSTA
	char *pos = NULL;
	struct bcm_cfg80211 *cfg;
#endif /* WL_VIRTUAL_APSTA */

	delim = strchr(command, ' ');
	/* For Ap mode rssi command would be
	 * driver rssi <sta_mac_addr>
	 * for STA/GC mode
	 * driver rssi
	*/
	if (delim) {
		/* Ap/GO mode
		* driver rssi <sta_mac_addr>
		*/
		DHD_TRACE(("wl_android_get_rssi: cmd:%s\n", delim));
		/* skip space from delim after finding char */
		delim++;
		if (!(bcm_ether_atoe((delim), &scbval.ea))) {
			DHD_ERROR(("wl_android_get_rssi: address err\n"));
			return -1;
		}
		scbval.val = htod32(0);
		DHD_TRACE(("wl_android_get_rssi: address:"MACDBG, MAC2STRDBG(scbval.ea.octet)));
#ifdef WL_VIRTUAL_APSTA
		/* RSDB AP may have another virtual interface
		 * In this case, format of private command is as following,
		 * DRIVER rssi <sta_mac_addr> <AP interface name>
		 */

		/* Current position is start of MAC address string */
		pos = delim;
		delim = strchr(pos, ' ');
		if (delim) {
			/* skip space from delim after finding char */
			delim++;
			if (strnlen(delim, IFNAMSIZ)) {
				cfg = wl_get_cfg(net);
				target_ndev = wl_get_ap_netdev(cfg, delim);
				if (target_ndev == NULL)
					target_ndev = net;
			}
		}
#endif /* WL_VIRTUAL_APSTA */
	}
	else {
		/* STA/GC mode */
		bzero(&scbval, sizeof(scb_val_t));
	}

	error = wldev_get_rssi(target_ndev, &scbval);
	if (error)
		return -1;

	error = wldev_get_ssid(target_ndev, &ssid);
	if (error)
		return -1;
	if ((ssid.SSID_len == 0) || (ssid.SSID_len > DOT11_MAX_SSID_LEN)) {
		DHD_ERROR(("wl_android_get_rssi: wldev_get_ssid failed\n"));
	} else if (total_len <= ssid.SSID_len) {
		return -ENOMEM;
	} else {
		memcpy(command, ssid.SSID, ssid.SSID_len);
		bytes_written = ssid.SSID_len;
	}
	if ((total_len - bytes_written) < (strlen(" rssi -XXX") + 1))
		return -ENOMEM;

	bytes_written += scnprintf(&command[bytes_written], total_len - bytes_written,
		" rssi %d", scbval.val);
	command[bytes_written] = '\0';

	DHD_TRACE(("wl_android_get_rssi: command result is %s (%d)\n", command, bytes_written));
	return bytes_written;
}
#endif /* AUTOMOTIVE_FEATURE */

static int wl_android_set_suspendopt(struct net_device *dev, char *command)
{
	int suspend_flag;
	int ret_now;
	int ret = 0;

	suspend_flag = *(command + strlen(CMD_SETSUSPENDOPT) + 1) - '0';

	if (suspend_flag != 0) {
		suspend_flag = 1;
	}
	ret_now = net_os_set_suspend_disable(dev, suspend_flag);

	if (ret_now != suspend_flag) {
		if (!(ret = net_os_set_suspend(dev, ret_now, 1))) {
			DHD_INFO(("wl_android_set_suspendopt: Suspend Flag %d -> %d\n",
				ret_now, suspend_flag));
		} else {
			DHD_ERROR(("wl_android_set_suspendopt: failed %d\n", ret));
		}
	}

	return ret;
}

static int wl_android_set_suspendmode(struct net_device *dev, char *command)
{
	int ret = 0;

#if !defined(CONFIG_HAS_EARLYSUSPEND) || !defined(DHD_USE_EARLYSUSPEND)
	int suspend_flag;

	suspend_flag = *(command + strlen(CMD_SETSUSPENDMODE) + 1) - '0';
	if (suspend_flag != 0)
		suspend_flag = 1;

	if (!(ret = net_os_set_suspend(dev, suspend_flag, 0)))
		DHD_INFO(("wl_android_set_suspendmode: Suspend Mode %d\n", suspend_flag));
	else
		DHD_ERROR(("wl_android_set_suspendmode: failed %d\n", ret));
#endif // endif

	return ret;
}

#ifdef AUTOMOTIVE_FEATURE
int wl_android_get_80211_mode(struct net_device *dev, char *command, int total_len)
{
	uint8 mode[5];
	int  error = 0;
	int bytes_written = 0;

	error = wldev_get_mode(dev, mode, sizeof(mode));
	if (error)
		return -1;

	DHD_INFO(("wl_android_get_80211_mode: mode:%s\n", mode));
	bytes_written = snprintf(command, total_len, "%s %s", CMD_80211_MODE, mode);
	DHD_INFO(("wl_android_get_80211_mode: command:%s EXIT\n", command));
	return bytes_written;

}
#endif /* AUTOMOTIVE_FEATURE */

extern chanspec_t
wl_chspec_driver_to_host(chanspec_t chanspec);
int wl_android_get_chanspec(struct net_device *dev, char *command, int total_len)
{
	int error = 0;
	int bytes_written = 0;
	int chsp = {0};
	uint16 band = 0;
	uint16 bw = 0;
	uint16 channel = 0;
	u32 sb = 0;
	chanspec_t chanspec;

	/* command is
	 * driver chanspec
	 */
	error = wldev_iovar_getint(dev, "chanspec", &chsp);
	if (error)
		return -1;

	chanspec = wl_chspec_driver_to_host(chsp);
	DHD_INFO(("wl_android_get_80211_mode: return value of chanspec:%x\n", chanspec));

	channel = chanspec & WL_CHANSPEC_CHAN_MASK;
	band = chanspec & WL_CHANSPEC_BAND_MASK;
	bw = chanspec & WL_CHANSPEC_BW_MASK;

	DHD_INFO(("wl_android_get_80211_mode: channel:%d band:%d bandwidth:%d\n",
		channel, band, bw));

	if (bw == WL_CHANSPEC_BW_80)
		bw = WL_CH_BANDWIDTH_80MHZ;
	else if (bw == WL_CHANSPEC_BW_40)
		bw = WL_CH_BANDWIDTH_40MHZ;
	else if	(bw == WL_CHANSPEC_BW_20)
		bw = WL_CH_BANDWIDTH_20MHZ;
	else
		bw = WL_CH_BANDWIDTH_20MHZ;

	if (bw == WL_CH_BANDWIDTH_40MHZ) {
		if (CHSPEC_SB_UPPER(chanspec)) {
			channel += CH_10MHZ_APART;
		} else {
			channel -= CH_10MHZ_APART;
		}
	}
	else if (bw == WL_CH_BANDWIDTH_80MHZ) {
		sb = chanspec & WL_CHANSPEC_CTL_SB_MASK;
		if (sb == WL_CHANSPEC_CTL_SB_LL) {
			channel -= (CH_10MHZ_APART + CH_20MHZ_APART);
		} else if (sb == WL_CHANSPEC_CTL_SB_LU) {
			channel -= CH_10MHZ_APART;
		} else if (sb == WL_CHANSPEC_CTL_SB_UL) {
			channel += CH_10MHZ_APART;
		} else {
			/* WL_CHANSPEC_CTL_SB_UU */
			channel += (CH_10MHZ_APART + CH_20MHZ_APART);
		}
	}
	bytes_written = snprintf(command, total_len, "%s channel %d band %s bw %d", CMD_CHANSPEC,
		channel, band == WL_CHANSPEC_BAND_5G ? "5G":"2G", bw);

	DHD_INFO(("wl_android_get_chanspec: command:%s EXIT\n", command));
	return bytes_written;

}

/* returns whether rsdb supported or not */
int wl_android_get_rsdb_mode(struct net_device *dev, char *command, int total_len)
{
	int bytes_written = 0;
	dhd_pub_t *dhd = wl_cfg80211_get_dhdp(dev);
	int rsdb_mode = 0;

	if (FW_SUPPORTED(dhd, rsdb)) {
		rsdb_mode = 1;
	}
	DHD_INFO(("wl_android_get_rsdb_mode: rsdb_mode:%d\n", rsdb_mode));

	bytes_written = snprintf(command, total_len, "%d", rsdb_mode);
	return bytes_written;
}

/* returns current datarate datarate returned from firmware are in 500kbps */
#ifdef AUTOMOTIVE_FEATURE
int wl_android_get_datarate(struct net_device *dev, char *command, int total_len)
{
	int  error = 0;
	int datarate = 0;
	int bytes_written = 0;

	error = wldev_get_datarate(dev, &datarate);
	if (error)
		return -1;

	DHD_INFO(("wl_android_get_datarate: datarate:%d\n", datarate));

	bytes_written = snprintf(command, total_len, "%s %d", CMD_DATARATE, (datarate/2));
	return bytes_written;
}

int wl_android_get_assoclist(struct net_device *dev, char *command, int total_len)
{
	int  error = 0;
	int bytes_written = 0;
	uint i;
	int len = 0;
	char mac_buf[MAX_NUM_OF_ASSOCLIST *
		sizeof(struct ether_addr) + sizeof(uint)] = {0};
	struct maclist *assoc_maclist = (struct maclist *)mac_buf;

	DHD_TRACE(("wl_android_get_assoclist: ENTER\n"));

	assoc_maclist->count = htod32(MAX_NUM_OF_ASSOCLIST);

	error = wldev_ioctl_get(dev, WLC_GET_ASSOCLIST, assoc_maclist, sizeof(mac_buf));
	if (error)
		return -1;

	assoc_maclist->count = dtoh32(assoc_maclist->count);
	bytes_written = snprintf(command, total_len, "%s listcount: %d Stations:",
		CMD_ASSOC_CLIENTS, assoc_maclist->count);

	for (i = 0; i < assoc_maclist->count; i++) {
		len = snprintf(command + bytes_written, total_len - bytes_written, " " MACDBG,
			MAC2STRDBG(assoc_maclist->ea[i].octet));
		/* A return value of '(total_len - bytes_written)' or more means that the
		 * output was truncated
		 */
		if ((len > 0) && (len < (total_len - bytes_written))) {
			bytes_written += len;
		} else {
			DHD_ERROR(("wl_android_get_assoclist: Insufficient buffer %d,"
				" bytes_written %d\n",
				total_len, bytes_written));
			bytes_written = -1;
			break;
		}
	}
	return bytes_written;
}
#endif /* AUTOMOTIVE_FEATURE */
static int wl_android_get_channel_list(struct net_device *dev, char *command, int total_len)
{

	int error = 0, len = 0, i;
	char smbuf[WLC_IOCTL_SMLEN] = {0};
	wl_channels_in_country_t *cic;
	char band[2];
	char *pos = command;

	cic = (wl_channels_in_country_t *)smbuf;

	pos = pos + strlen(CMD_CHANNELS_IN_CC) + 1;

	sscanf(pos, "%s %s", cic->country_abbrev, band);
	DHD_INFO(("%s:country  %s  and mode %s \n", __FUNCTION__, cic->country_abbrev, band));
	len = strlen(cic->country_abbrev);
	if ((len > 3) || (len < 2)) {
		DHD_ERROR(("%s :invalid country abbrev\n", __FUNCTION__));
		return -1;
	}

	if (!strcmp(band, "a") || !strcmp(band, "A"))
		cic->band = WLC_BAND_5G;
	else if (!strcmp(band, "b") || !strcmp(band, "B"))
		cic->band = WLC_BAND_2G;
	else {
		DHD_ERROR(("%s: unsupported band: \n", __FUNCTION__));
		return -1;
	}

	cic->count = 0;
	cic->buflen = WL_EXTRA_BUF_MAX;

	error = wldev_ioctl_get(dev, WLC_GET_CHANNELS_IN_COUNTRY, cic, sizeof(smbuf));
	if (error) {
		DHD_ERROR(("%s :Failed to get channels \n", __FUNCTION__));
		return -1;
	}

	if (cic->count == 0)
		return -1;

	for (i = 0; i < (cic->count); i++) {
		pos += snprintf(pos, total_len, " %d", (cic->channel[i]));
	}
	return (pos - command);
}

int wl_android_block_associations(struct net_device *dev, char *command, int total_len)
{

	int enable_blockassoc = 0, bytes_written = 0, ret = 0;

	if (*(command + strlen(CMD_BLOCKASSOC)) == '\0') {
		ret = wldev_iovar_getint(dev, "block_assoc", &enable_blockassoc);
		if (ret != BCME_OK) {
			DHD_ERROR(("%s: Failed to get status of block_assoc error(%d)\n", __FUNCTION__, ret));
			return ret;
		}
                bytes_written = snprintf(command, total_len, "%s %d", CMD_BLOCKASSOC, enable_blockassoc);
                return bytes_written;
	}
        command = (command + strlen(CMD_BLOCKASSOC));
        command++;
        enable_blockassoc = bcm_atoi(command);
        DHD_INFO(("%s: Block associations in AP mode = %d\n", __FUNCTION__, enable_blockassoc));
        ret = wldev_iovar_setint(dev, "block_assoc", enable_blockassoc);
	if (ret != BCME_OK){
		DHD_ERROR(("%s: Failed to set block_assoc in AP mode %d\n", __FUNCTION__, ret));
		return ret;
	}

        return 0;
}

extern chanspec_t
wl_chspec_host_to_driver(chanspec_t chanspec);
static int wl_android_set_csa(struct net_device *dev, char *command)
{
	int error = 0;
	char smbuf[WLC_IOCTL_SMLEN];
	wl_chan_switch_t csa_arg;
	u32 chnsp = 0;
	int err = 0;
	char *str, str_chan[8];
	uint default_bw = WL_CHANSPEC_BW_20;
#if !defined(DISALBE_11H) && defined(DHD_NOSCAN_DURING_CSA)
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct bcm_cfg80211 *cfg = (struct bcm_cfg80211 *)wiphy_priv(wdev->wiphy);
#endif // endif

	DHD_INFO(("wl_android_set_csa: command:%s\n", command));

#if !defined(DISALBE_11H) && defined(DHD_NOSCAN_DURING_CSA)
	if (!(wdev->iftype == NL80211_IFTYPE_AP || wdev->iftype == NL80211_IFTYPE_P2P_GO)) {
		DHD_ERROR(("%s:error csa is for only AP/AGO mode(%d)\n", __FUNCTION__,
					wdev->iftype));
		return -1;
	}
#endif // endif

	/*
	 * SETCSA driver command provides support for AP/AGO to switch its channel
	 * as well as connected STAs channel. This command will send CSA frame and
	 * based on this connected STA will switch to channel which we will pass in
	 * CSA frame.
	 * Usage:
	 * > IFNAME=<group_iface_name> DRIVER SETCSA mode count channel frame_type
	 * > IFNAME=<group_iface_name> DRIVER SETCSA 0 10 1 u
	 * If no frame type is specified, frame_type=0 (Broadcast frame type)
	 */

	command = (command + strlen(CMD_SET_CSA));
	/* Order is mode, count channel */
	if (!*++command) {
		DHD_ERROR(("wl_android_set_csa:error missing arguments\n"));
		return -1;
	}
	csa_arg.mode = bcm_atoi(command);

	if (csa_arg.mode != 0 && csa_arg.mode != 1) {
		DHD_ERROR(("Invalid mode\n"));
		return -1;
	}

	if (!*++command) {
		DHD_ERROR(("wl_android_set_csa: error missing count\n"));
		return -1;
	}
	command++;
	csa_arg.count = bcm_atoi(command);

	csa_arg.reg = 0;
	csa_arg.chspec = 0;

	str = strchr(command, ' ');
	if (str == NULL) {
		DHD_ERROR(("wl_android_set_csa: error missing channel\n"));
		return -1;
	}
	command = ++str;

	str = strchr(command, ' ');
	if (str != NULL){
		strncpy(str_chan, command, (str-command));
	}else {
		strncpy(str_chan, command, strlen(command));
	}

	/* Get current chanspec to retrieve the current bandwidth */
	error = wldev_iovar_getint(dev, "chanspec", &chnsp);
	if (error == BCME_OK) {
		chnsp = wl_chspec_driver_to_host(chnsp);
		/* Use current bandwidth as default if it is not specified in cmd string */
		default_bw = chnsp & WL_CHANSPEC_BW_MASK;
	}

	chnsp = wf_chspec_aton_ex(str_chan, default_bw);

	if (chnsp == 0) {
		DHD_ERROR(("wl_android_set_csa:chsp is not correct\n"));
		return -1;
	}
	chnsp = wl_chspec_host_to_driver(chnsp);
	csa_arg.chspec = chnsp;

	/* csa action frame type */
	if (str != NULL){
		if (strcmp(++str, "u") == 0) {
			csa_arg.frame_type = CSA_UNICAST_ACTION_FRAME;
		} else {
			DHD_ERROR(("%s:error: invalid frame type: %s\n",
						__FUNCTION__, command));
			return -1;
		}
	} else {
		csa_arg.frame_type = CSA_BROADCAST_ACTION_FRAME;
	}

	if (chnsp & WL_CHANSPEC_BAND_5G) {
		u32 chanspec = wf_chspec_ctlchan(chnsp);
		err = wldev_iovar_getint(dev, "per_chan_info", &chanspec);
		if (!err) {
			if ((chanspec & WL_CHAN_RADAR) || (chanspec & WL_CHAN_PASSIVE)) {
				DHD_ERROR(("Channel is radar sensitive\n"));
				return -1;
			}
			if (chanspec == 0) {
				DHD_ERROR(("Invalid hw channel\n"));
				return -1;
			}
		} else  {
			DHD_ERROR(("does not support per_chan_info\n"));
			return -1;
		}
		DHD_INFO(("non radar sensitivity\n"));
	}
	error = wldev_iovar_setbuf(dev, "csa", &csa_arg, sizeof(csa_arg),
			smbuf, sizeof(smbuf), NULL);
	if (error) {
		DHD_ERROR(("wl_android_set_csa:set csa failed:%d\n", error));
		return -1;
	}

#if !defined(DISALBE_11H) && defined(DHD_NOSCAN_DURING_CSA)
	cfg->in_csa = TRUE;
	mod_timer(&cfg->csa_timeout, jiffies + msecs_to_jiffies(100 * (csa_arg.count+2)));
#endif // endif
	return 0;
}

static int
wl_android_set_bcn_li_dtim(struct net_device *dev, char *command)
{
	int ret = 0;
	int dtim;

	dtim = *(command + strlen(CMD_SETDTIM_IN_SUSPEND) + 1) - '0';

	if (dtim > (MAX_DTIM_ALLOWED_INTERVAL / MAX_DTIM_SKIP_BEACON_INTERVAL)) {
		DHD_ERROR(("%s: failed, invalid dtim %d\n",
			__FUNCTION__, dtim));
		return BCME_ERROR;
	}

	if (!(ret = net_os_set_suspend_bcn_li_dtim(dev, dtim))) {
		DHD_TRACE(("%s: SET bcn_li_dtim in suspend %d\n",
			__FUNCTION__, dtim));
	} else {
		DHD_ERROR(("%s: failed %d\n", __FUNCTION__, ret));
	}

	return ret;
}

static int
wl_android_set_max_dtim(struct net_device *dev, char *command)
{
	int ret = 0;
	int dtim_flag;

	dtim_flag = *(command + strlen(CMD_MAXDTIM_IN_SUSPEND) + 1) - '0';

	if (!(ret = net_os_set_max_dtim_enable(dev, dtim_flag))) {
		DHD_TRACE(("wl_android_set_max_dtim: use Max bcn_li_dtim in suspend %s\n",
			(dtim_flag ? "Enable" : "Disable")));
	} else {
		DHD_ERROR(("wl_android_set_max_dtim: failed %d\n", ret));
	}

	return ret;
}

#ifdef DISABLE_DTIM_IN_SUSPEND
static int
wl_android_set_disable_dtim_in_suspend(struct net_device *dev, char *command)
{
	int ret = 0;
	int dtim_flag;

	dtim_flag = *(command + strlen(CMD_DISDTIM_IN_SUSPEND) + 1) - '0';

	if (!(ret = net_os_set_disable_dtim_in_suspend(dev, dtim_flag))) {
		DHD_TRACE(("wl_android_set_disable_dtim_in_suspend: "
			"use Disable bcn_li_dtim in suspend %s\n",
			(dtim_flag ? "Enable" : "Disable")));
	} else {
		DHD_ERROR(("wl_android_set_disable_dtim_in_suspend: failed %d\n", ret));
	}

	return ret;
}
#endif /* DISABLE_DTIM_IN_SUSPEND */

static int wl_android_get_band(struct net_device *dev, char *command, int total_len)
{
	uint band;
	int bytes_written;
	int error;

	error = wldev_get_band(dev, &band);
	if (error)
		return -1;
	bytes_written = snprintf(command, total_len, "Band %d", band);
	return bytes_written;
}

static int
wl_android_set_band(struct net_device *dev, char *command)
{
	int error = 0;
	uint band = *(command + strlen(CMD_SETBAND) + 1) - '0';
#ifdef WL_HOST_BAND_MGMT
	int ret = 0;
	if ((ret = wl_cfg80211_set_band(dev, band)) < 0) {
		if (ret == BCME_UNSUPPORTED) {
			/* If roam_var is unsupported, fallback to the original method */
			WL_ERR(("WL_HOST_BAND_MGMT defined, "
				"but roam_band iovar unsupported in the firmware\n"));
		} else {
			error = -1;
		}
	}
	if (((ret == 0) && (band == WLC_BAND_AUTO)) || (ret == BCME_UNSUPPORTED)) {
		/* Apply if roam_band iovar is not supported or band setting is AUTO */
		error = wldev_set_band(dev, band);
	}
#else
	error = wl_cfg80211_set_if_band(dev, band);
#endif /* WL_HOST_BAND_MGMT */
#ifdef ROAM_CHANNEL_CACHE
	wl_update_roamscan_cache_by_band(dev, band);
#endif /* ROAM_CHANNEL_CACHE */
	return error;
}

static int wl_android_add_vendor_ie(struct net_device *dev, char *command, int total_len)
{
	char ie_buf[VNDR_IE_MAX_LEN];
	char *ioctl_buf = NULL;
	char hex[] = "XX";
	char *pcmd = NULL;
	int ielen = 0, datalen = 0, idx = 0, tot_len = 0;
	vndr_ie_setbuf_t *vndr_ie = NULL;
	s32 iecount;
	uint32 pktflag;
	gfp_t kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;
	s32 err = BCME_OK;

	/*
	 * ADD_IE driver command provides support for addition of vendor elements
	 * to different management frames via wpa_cli
	 * Usage:
	 * Create softap/AGO
	 * wpa_cli> IFNAME=<group_iface_name> DRIVER ADD_IE <flag> <OUI> <DATA>
	 * Here Flag is 802.11 Mgmt packet flags values
	 * Beacon: 0
	 * Probe Rsp: 1
	 * Assoc Rsp: 2
	 * Auth Rsp: 4
	 * Probe Req: 8
	 * Assoc Req: 16
	 * E.g
	 * wpa_cli> IFNAME=bcm0 DRIVER ADD_IE 1 998877 1122334455667788
	 */
	pcmd = command + strlen(CMD_ADDIE) + 1;
	pktflag = simple_strtoul(pcmd, &pcmd, 16);
	pcmd = pcmd + 1;

	for (idx = 0; idx < DOT11_OUI_LEN; idx++) {
		hex[0] = *pcmd++;
		hex[1] = *pcmd++;
		ie_buf[idx] =  (uint8)simple_strtoul(hex, NULL, 16);
	}
	pcmd++;
	while ((*pcmd != '\0') && (idx < VNDR_IE_MAX_LEN)) {
		hex[0] = *pcmd++;
		hex[1] = *pcmd++;
		ie_buf[idx++] =  (uint8)simple_strtoul(hex, NULL, 16);
		datalen++;
	}

	tot_len = sizeof(vndr_ie_setbuf_t) + (datalen - 1);

	if (tot_len > VNDR_IE_MAX_LEN) {
		WL_ERR(("Invalid IE total length %d\n", tot_len));
		return -ENOMEM;
	}

	vndr_ie = (vndr_ie_setbuf_t *) kzalloc(tot_len, kflags);
	if (!vndr_ie) {
		WL_ERR(("IE memory alloc failed\n"));
		return -ENOMEM;
	}
	/* Copy the vndr_ie SET command ("add"/"del") to the buffer */
	strncpy(vndr_ie->cmd, "add", VNDR_IE_CMD_LEN - 1);
	vndr_ie->cmd[VNDR_IE_CMD_LEN - 1] = '\0';

	/* Set the IE count - the buffer contains only 1 IE */
	iecount = htod32(1);
	memcpy((void *)&vndr_ie->vndr_ie_buffer.iecount, &iecount, sizeof(s32));

	/* Set packet flag to indicate the appropriate frame will contain this IE */
	pktflag = htod32(1<<pktflag);
	memcpy((void *)&vndr_ie->vndr_ie_buffer.vndr_ie_list[0].pktflag, &pktflag,
		sizeof(u32));

	/* Set the IE ID */
	vndr_ie->vndr_ie_buffer.vndr_ie_list[0].vndr_ie_data.id = (uchar) DOT11_MNG_PROPR_ID;

	/* Set the OUI */
	memcpy(&vndr_ie->vndr_ie_buffer.vndr_ie_list[0].vndr_ie_data.oui, &ie_buf,
		DOT11_OUI_LEN);
	/* Set the Data */
	memcpy(&vndr_ie->vndr_ie_buffer.vndr_ie_list[0].vndr_ie_data.data,
		&ie_buf[DOT11_OUI_LEN], datalen);

	ielen = DOT11_OUI_LEN + datalen;
	vndr_ie->vndr_ie_buffer.vndr_ie_list[0].vndr_ie_data.len = (uchar) ielen;

	ioctl_buf = kmalloc(WLC_IOCTL_MEDLEN, GFP_KERNEL);
	if (!ioctl_buf) {
		WL_ERR(("ioctl memory alloc failed\n"));
		if (vndr_ie) {
			kfree(vndr_ie);
		}
		return -ENOMEM;
	}
	memset(ioctl_buf, 0, WLC_IOCTL_MEDLEN); /* init the buffer */
	err = wldev_iovar_setbuf(dev, "vndr_ie", vndr_ie, tot_len, ioctl_buf,
		WLC_IOCTL_MEDLEN, NULL);

	if (err != BCME_OK) {
		err = -EINVAL;
	}

	if (vndr_ie) {
		kfree(vndr_ie);
	}

	if (ioctl_buf) {
		kfree(ioctl_buf);
	}

	return err;
}

static int wl_android_del_vendor_ie(struct net_device *dev, char *command, int total_len)
{
	char ie_buf[VNDR_IE_MAX_LEN];
	char *ioctl_buf = NULL;
	char hex[] = "XX";
	char *pcmd = NULL;
	int ielen = 0, datalen = 0, idx = 0, tot_len = 0;
	vndr_ie_setbuf_t *vndr_ie = NULL;
	s32 iecount;
	uint32 pktflag;
	gfp_t kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;
	s32 err = BCME_OK;

	/*
	 * DEL_IE driver command provides support for deletoon of vendor elements
	 * from different management frames via wpa_cli
	 * Usage:
	 * Create softap/AGO
	 * wpa_cli> IFNAME=<group_iface_name> DRIVER DEL_IE <flag> <OUI> <DATA>
	 * Here Flag is 802.11 Mgmt packet flags values
	 * Beacon: 1
	 * Probe Rsp: 2
	 * Assoc Rsp: 4
	 * Auth Rsp: 8
	 * Probe Req: 16
	 * Assoc Req: 32
	 * E.g
	 * wpa_cli> IFNAME=bcm0 DRIVER DEL_IE 1 998877 1122334455667788
	 */
	pcmd = command + strlen(CMD_DELIE) + 1;

	pktflag = simple_strtoul(pcmd, &pcmd, 16);
	pcmd = pcmd + 1;

	for (idx = 0; idx < DOT11_OUI_LEN; idx++) {
		hex[0] = *pcmd++;
		hex[1] = *pcmd++;
		ie_buf[idx] =  (uint8)simple_strtoul(hex, NULL, 16);
	}
	pcmd++;
	while ((*pcmd != '\0') && (idx < VNDR_IE_MAX_LEN)) {
		hex[0] = *pcmd++;
		hex[1] = *pcmd++;
		ie_buf[idx++] =  (uint8)simple_strtoul(hex, NULL, 16);
		datalen++;
	}

	tot_len = sizeof(vndr_ie_setbuf_t) + (datalen - 1);
	if (tot_len > VNDR_IE_MAX_LEN) {
		WL_ERR(("Invalid IE total length %d\n", tot_len));
		return -ENOMEM;
	}
	vndr_ie = (vndr_ie_setbuf_t *) kzalloc(tot_len, kflags);
	if (!vndr_ie) {
		WL_ERR(("IE memory alloc failed\n"));
		return -ENOMEM;
	}
	/* Copy the vndr_ie SET command ("add"/"del") to the buffer */
	strncpy(vndr_ie->cmd, "del", VNDR_IE_CMD_LEN - 1);
	vndr_ie->cmd[VNDR_IE_CMD_LEN - 1] = '\0';

	/* Set the IE count - the buffer contains only 1 IE */
	iecount = htod32(1);
	memcpy((void *)&vndr_ie->vndr_ie_buffer.iecount, &iecount, sizeof(s32));

	/* Set packet flag to indicate the appropriate frame will contain this IE */
	pktflag = htod32(1<<(pktflag-1));
	memcpy((void *)&vndr_ie->vndr_ie_buffer.vndr_ie_list[0].pktflag, &pktflag,
		sizeof(u32));

	/* Set the IE ID */
	vndr_ie->vndr_ie_buffer.vndr_ie_list[0].vndr_ie_data.id = (uchar) DOT11_MNG_PROPR_ID;

	/* Set the OUI */
	memcpy(&vndr_ie->vndr_ie_buffer.vndr_ie_list[0].vndr_ie_data.oui, &ie_buf,
		DOT11_OUI_LEN);

	/* Set the Data */
	memcpy(&vndr_ie->vndr_ie_buffer.vndr_ie_list[0].vndr_ie_data.data,
		&ie_buf[DOT11_OUI_LEN], datalen);

	ielen = DOT11_OUI_LEN + datalen;
	vndr_ie->vndr_ie_buffer.vndr_ie_list[0].vndr_ie_data.len = (uchar) ielen;

	ioctl_buf = kmalloc(WLC_IOCTL_MEDLEN, GFP_KERNEL);
	if (!ioctl_buf) {
		WL_ERR(("ioctl memory alloc failed\n"));
		if (vndr_ie) {
			kfree(vndr_ie);
		}
		return -ENOMEM;
	}
	memset(ioctl_buf, 0, WLC_IOCTL_MEDLEN); /* init the buffer */
	err = wldev_iovar_setbuf(dev, "vndr_ie", vndr_ie, tot_len, ioctl_buf,
		WLC_IOCTL_MEDLEN, NULL);

	if (err != BCME_OK) {
		err = -EINVAL;
	}

	if (vndr_ie) {
		kfree(vndr_ie);
	}

	if (ioctl_buf) {
		kfree(ioctl_buf);
	}
	return err;
}

#if defined(CUSTOMER_HW4_PRIVATE_CMD) || defined(IGUANA_LEGACY_CHIPS)
#ifdef ROAM_API
static bool wl_android_check_wbtext(struct net_device *dev)
{
	dhd_pub_t *dhdp = wl_cfg80211_get_dhdp(dev);
	return dhdp->wbtext_support;
}

static int wl_android_set_roam_trigger(
	struct net_device *dev, char* command)
{
	int roam_trigger[2] = {0, 0};
	int error;

#ifdef WBTEXT
	if (wl_android_check_wbtext(dev)) {
		WL_ERR(("blocked to set roam trigger. try with setting roam profile\n"));
		return BCME_ERROR;
	}
#endif /* WBTEXT */

	sscanf(command, "%*s %10d", &roam_trigger[0]);
	if (roam_trigger[0] >= 0) {
		WL_ERR(("wrong roam trigger value (%d)\n", roam_trigger[0]));
		return BCME_ERROR;
	}

	roam_trigger[1] = WLC_BAND_ALL;
	error = wldev_ioctl_set(dev, WLC_SET_ROAM_TRIGGER, roam_trigger,
		sizeof(roam_trigger));
	if (error != BCME_OK) {
		WL_ERR(("failed to set roam trigger (%d)\n", error));
		return BCME_ERROR;
	}

	return BCME_OK;
}

static int wl_android_get_roam_trigger(
	struct net_device *dev, char *command, int total_len)
{
	int bytes_written, error;
	int roam_trigger[2] = {0, 0};
	uint16 band = 0;
	int chsp = {0};
	chanspec_t chanspec;
#ifdef WBTEXT
	int i;
	wl_roamprof_band_t rp;
	uint8 roam_prof_ver = 0, roam_prof_size = 0;
#endif /* WBTEXT */

	error = wldev_iovar_getint(dev, "chanspec", &chsp);
	if (error != BCME_OK) {
		WL_ERR(("failed to get chanspec (%d)\n", error));
		return BCME_ERROR;
	}

	chanspec = wl_chspec_driver_to_host(chsp);
	band = chanspec & WL_CHANSPEC_BAND_MASK;
	if (band == WL_CHANSPEC_BAND_5G)
		band = WLC_BAND_5G;
	else
		band = WLC_BAND_2G;

	if (wl_android_check_wbtext(dev)) {
#ifdef WBTEXT
		memset_s(&rp, sizeof(rp), 0, sizeof(rp));
		if ((error = wlc_wbtext_get_roam_prof(dev, &rp, band, &roam_prof_ver,
			&roam_prof_size))) {
			WL_ERR(("Getting roam_profile failed with err=%d \n", error));
			return -EINVAL;
		}
		switch (roam_prof_ver) {
			case WL_ROAM_PROF_VER_1:
			{
				for (i = 0; i < WL_MAX_ROAM_PROF_BRACKETS; i++) {
					if (rp.v2.roam_prof[i].channel_usage == 0) {
						roam_trigger[0] = rp.v2.roam_prof[i].roam_trigger;
						break;
					}
				}
			}
			break;
			case WL_ROAM_PROF_VER_2:
			{
				for (i = 0; i < WL_MAX_ROAM_PROF_BRACKETS; i++) {
					if (rp.v3.roam_prof[i].channel_usage == 0) {
						roam_trigger[0] = rp.v3.roam_prof[i].roam_trigger;
						break;
					}
				}
			}
			break;
			default:
				WL_ERR(("bad version = %d \n", roam_prof_ver));
				return BCME_VERSION;
		}
#endif /* WBTEXT */
		if (roam_trigger[0] == 0) {
			WL_ERR(("roam trigger was not set properly\n"));
			return BCME_ERROR;
		}
	} else {
		roam_trigger[1] = band;
		error = wldev_ioctl_get(dev, WLC_GET_ROAM_TRIGGER, roam_trigger,
			sizeof(roam_trigger));
		if (error != BCME_OK) {
			WL_ERR(("failed to get roam trigger (%d)\n", error));
			return BCME_ERROR;
		}
	}

	bytes_written = snprintf(command, total_len, "%s %d",
		CMD_ROAMTRIGGER_GET, roam_trigger[0]);

	return bytes_written;
}

int wl_android_set_roam_delta(
	struct net_device *dev, char* command)
{
	int roam_delta[2];

	sscanf(command, "%*s %10d", &roam_delta[0]);
	roam_delta[1] = WLC_BAND_ALL;

	return wldev_ioctl_set(dev, WLC_SET_ROAM_DELTA, roam_delta,
		sizeof(roam_delta));
}

static int wl_android_get_roam_delta(
	struct net_device *dev, char *command, int total_len)
{
	int bytes_written;
	int roam_delta[2] = {0, 0};

	roam_delta[1] = WLC_BAND_2G;
	if (wldev_ioctl_get(dev, WLC_GET_ROAM_DELTA, roam_delta,
		sizeof(roam_delta))) {
		roam_delta[1] = WLC_BAND_5G;
		if (wldev_ioctl_get(dev, WLC_GET_ROAM_DELTA, roam_delta,
		                    sizeof(roam_delta)))
			return -1;
	}

	bytes_written = snprintf(command, total_len, "%s %d",
		CMD_ROAMDELTA_GET, roam_delta[0]);

	return bytes_written;
}

int wl_android_set_roam_scan_period(
	struct net_device *dev, char* command)
{
	int roam_scan_period = 0;

	sscanf(command, "%*s %10d", &roam_scan_period);
	return wldev_ioctl_set(dev, WLC_SET_ROAM_SCAN_PERIOD, &roam_scan_period,
		sizeof(roam_scan_period));
}

static int wl_android_get_roam_scan_period(
	struct net_device *dev, char *command, int total_len)
{
	int bytes_written;
	int roam_scan_period = 0;

	if (wldev_ioctl_get(dev, WLC_GET_ROAM_SCAN_PERIOD, &roam_scan_period,
		sizeof(roam_scan_period)))
		return -1;

	bytes_written = snprintf(command, total_len, "%s %d",
		CMD_ROAMSCANPERIOD_GET, roam_scan_period);

	return bytes_written;
}

int wl_android_set_full_roam_scan_period(
	struct net_device *dev, char* command, int total_len)
{
	int error = 0;
	int full_roam_scan_period = 0;
	char smbuf[WLC_IOCTL_SMLEN];

	sscanf(command+sizeof("SETFULLROAMSCANPERIOD"), "%d", &full_roam_scan_period);
	WL_TRACE(("fullroamperiod = %d\n", full_roam_scan_period));

	error = wldev_iovar_setbuf(dev, "fullroamperiod", &full_roam_scan_period,
		sizeof(full_roam_scan_period), smbuf, sizeof(smbuf), NULL);
	if (error) {
		DHD_ERROR(("Failed to set full roam scan period, error = %d\n", error));
	}

	return error;
}

static int wl_android_get_full_roam_scan_period(
	struct net_device *dev, char *command, int total_len)
{
	int error;
	int bytes_written;
	int full_roam_scan_period = 0;

	error = wldev_iovar_getint(dev, "fullroamperiod", &full_roam_scan_period);

	if (error) {
		DHD_ERROR(("%s: get full roam scan period failed code %d\n",
			__func__, error));
		return -1;
	} else {
		DHD_INFO(("%s: get full roam scan period %d\n", __func__, full_roam_scan_period));
	}

	bytes_written = snprintf(command, total_len, "%s %d",
		CMD_FULLROAMSCANPERIOD_GET, full_roam_scan_period);

	return bytes_written;
}

#ifdef AUTOMOTIVE_FEATURE
int wl_android_set_country_rev(
	struct net_device *dev, char* command)
{
	int error = 0;
	wl_country_t cspec = {{0}, 0, {0} };
	char country_code[WLC_CNTRY_BUF_SZ];
	char smbuf[WLC_IOCTL_SMLEN];
	int rev = 0;

	/*
	 * SETCOUNTRYREV driver command provides support setting the country.
	 * e.g US, DE, JP etc via supplicant. Once set, band and channels
	 * too automatically gets updated based on the country.
	 * Usage:
	 * > IFNAME=wlan0 DRIVER SETCOUNTRYREV JP
	 * OK
	 */

	bzero(country_code, sizeof(country_code));
	sscanf(command+sizeof("SETCOUNTRYREV"), "%3s %10d", country_code, &rev);
	WL_TRACE(("country_code = %s, rev = %d\n", country_code, rev));

	memcpy(cspec.country_abbrev, country_code, sizeof(country_code));
	memcpy(cspec.ccode, country_code, sizeof(country_code));
	cspec.rev = rev;

	error = wldev_iovar_setbuf(dev, "country", (char *)&cspec,
		sizeof(cspec), smbuf, sizeof(smbuf), NULL);

	if (error) {
		DHD_ERROR(("wl_android_set_country_rev: set country '%s/%d' failed code %d\n",
			cspec.ccode, cspec.rev, error));
	} else {
		dhd_bus_country_set(dev, &cspec, true);
		DHD_INFO(("wl_android_set_country_rev: set country '%s/%d'\n",
			cspec.ccode, cspec.rev));
	}

	return error;
}

static int wl_android_get_country_rev(
	struct net_device *dev, char *command, int total_len)
{
	int error;
	int bytes_written;
	char smbuf[WLC_IOCTL_SMLEN];
	wl_country_t cspec;

	/*
	 * GETCOUNTRYREV driver command provides support getting the country.
	 * e.g US, DE, JP etc via supplicant.
	 * Usage:
	 * > IFNAME=wlan0 DRIVER GETCOUNTRYREV
	 * GETCOUNTRYREV JP 0
	 */

	error = wldev_iovar_getbuf(dev, "country", NULL, 0, smbuf,
		sizeof(smbuf), NULL);

	if (error) {
		DHD_ERROR(("wl_android_get_country_rev: get country failed code %d\n",
			error));
		return -1;
	} else {
		memcpy(&cspec, smbuf, sizeof(cspec));
		DHD_INFO(("wl_android_get_country_rev: get country '%c%c %d'\n",
			cspec.ccode[0], cspec.ccode[1], cspec.rev));
	}

	bytes_written = snprintf(command, total_len, "%s %c%c %d",
		CMD_COUNTRYREV_GET, cspec.ccode[0], cspec.ccode[1], cspec.rev);

	return bytes_written;
}
#endif /* AUTOMOTIVE_FEATURE */
#endif /* ROAM_API */

#ifdef WES_SUPPORT
int wl_android_get_roam_scan_control(struct net_device *dev, char *command, int total_len)
{
	int error = 0;
	int bytes_written = 0;
	int mode = 0;

	error = get_roamscan_mode(dev, &mode);
	if (error) {
		DHD_ERROR(("wl_android_get_roam_scan_control: Failed to get Scan Control,"
			" error = %d\n", error));
		return -1;
	}

	bytes_written = snprintf(command, total_len, "%s %d", CMD_GETROAMSCANCONTROL, mode);

	return bytes_written;
}

int wl_android_set_roam_scan_control(struct net_device *dev, char *command)
{
	int error = 0;
	int mode = 0;

	if (sscanf(command, "%*s %d", &mode) != 1) {
		DHD_ERROR(("wl_android_set_roam_scan_control: Failed to get Parameter\n"));
		return -1;
	}

	error = set_roamscan_mode(dev, mode);
	if (error) {
		DHD_ERROR(("wl_android_set_roam_scan_control: Failed to set Scan Control %d,"
		" error = %d\n",
		 mode, error));
		return -1;
	}

	return 0;
}

int wl_android_get_roam_scan_channels(struct net_device *dev, char *command, int total_len)
{
	int bytes_written = 0;
	unsigned char channels[MAX_ROAM_CHANNEL] = {0};
	int channel_cnt = 0;
	int i = 0;
	int buf_avail, len;

	channel_cnt = get_roamscan_channel_list(dev, channels, MAX_ROAM_CHANNEL);
	bytes_written = snprintf(command, total_len, "%s %d",
		CMD_GETROAMSCANCHANNELS, channel_cnt);
	buf_avail = total_len - bytes_written;
	for (i = 0; i < channel_cnt; i++) {
		/* A return value of 'buf_avail' or more means that the output was truncated */
		len = snprintf(command + bytes_written, buf_avail, " %d", channels[i]);
		if (len >= buf_avail) {
			WL_ERR(("wl_android_get_roam_scan_channels: Insufficient memory,"
				" %d bytes\n",
				total_len));
			bytes_written = -1;
			break;
		}
		/* 'buf_avail' decremented by number of bytes written */
		buf_avail -= len;
		bytes_written += len;
	}
	WL_INFORM(("wl_android_get_roam_scan_channels: %s\n", command));
	return bytes_written;
}

int wl_android_set_roam_scan_channels(struct net_device *dev, char *command)
{
	int error = 0;
	unsigned char *p = (unsigned char *)(command + strlen(CMD_SETROAMSCANCHANNELS) + 1);
	int get_ioctl_version = wl_cfg80211_get_ioctl_version();
	error = set_roamscan_channel_list(dev, p[0], &p[1], get_ioctl_version);
	if (error) {
		DHD_ERROR(("wl_android_set_roam_scan_channels: Failed to set Scan Channels %d,"
		" error = %d\n",
		 p[0], error));
		return -1;
	}

	return 0;
}

int wl_android_get_scan_channel_time(struct net_device *dev, char *command, int total_len)
{
	int error = 0;
	int bytes_written = 0;
	int time = 0;

	error = wldev_ioctl_get(dev, WLC_GET_SCAN_CHANNEL_TIME, &time, sizeof(time));
	if (error) {
		DHD_ERROR(("wl_android_get_scan_channel_time: Failed to get Scan Channel Time,"
		" error = %d\n",
		error));
		return -1;
	}

	bytes_written = snprintf(command, total_len, "%s %d", CMD_GETSCANCHANNELTIME, time);

	return bytes_written;
}

int wl_android_set_scan_channel_time(struct net_device *dev, char *command)
{
	int error = 0;
	int time = 0;

	if (sscanf(command, "%*s %d", &time) != 1) {
		DHD_ERROR(("wl_android_set_scan_channel_time: Failed to get Parameter\n"));
		return -1;
	}
#ifdef CUSTOMER_SCAN_TIMEOUT_SETTING
	wl_cfg80211_custom_scan_time(dev, WL_CUSTOM_SCAN_CHANNEL_TIME, time);
	error = wldev_ioctl_set(dev, WLC_SET_SCAN_CHANNEL_TIME, &time, sizeof(time));
#endif /* CUSTOMER_SCAN_TIMEOUT_SETTING */
	if (error) {
		DHD_ERROR(("wl_android_set_scan_channel_time: Failed to set Scan Channel Time %d,"
		" error = %d\n",
		time, error));
		return -1;
	}

	return 0;
}

int
wl_android_get_scan_unassoc_time(struct net_device *dev, char *command, int total_len)
{
	int error = 0;
	int bytes_written = 0;
	int time = 0;

	error = wldev_ioctl_get(dev, WLC_GET_SCAN_UNASSOC_TIME, &time, sizeof(time));
	if (error) {
		DHD_ERROR(("wl_android_get_scan_unassoc_time: Failed to get Scan Unassoc"
		" Time, error = %d\n",
			error));
		return -1;
	}

	bytes_written = snprintf(command, total_len, "%s %d", CMD_GETSCANUNASSOCTIME, time);

	return bytes_written;
}

int
wl_android_set_scan_unassoc_time(struct net_device *dev, char *command)
{
	int error = 0;
	int time = 0;

	if (sscanf(command, "%*s %d", &time) != 1) {
		DHD_ERROR(("wl_android_set_scan_unassoc_time: Failed to get Parameter\n"));
		return -1;
	}
#ifdef CUSTOMER_SCAN_TIMEOUT_SETTING
	wl_cfg80211_custom_scan_time(dev, WL_CUSTOM_SCAN_UNASSOC_TIME, time);
	error = wldev_ioctl_set(dev, WLC_SET_SCAN_UNASSOC_TIME, &time, sizeof(time));
#endif /* CUSTOMER_SCAN_TIMEOUT_SETTING */
	if (error) {
		DHD_ERROR(("wl_android_set_scan_unassoc_time: Failed to set Scan Unassoc Time %d,"
			" error = %d\n",
			time, error));
		return -1;
	}

	return 0;
}

int
wl_android_get_scan_passive_time(struct net_device *dev, char *command, int total_len)
{
	int error = 0;
	int bytes_written = 0;
	int time = 0;

	error = wldev_ioctl_get(dev, WLC_GET_SCAN_PASSIVE_TIME, &time, sizeof(time));
	if (error) {
		DHD_ERROR(("wl_android_get_scan_passive_time: Failed to get Scan Passive Time,"
			" error = %d\n",
			error));
		return -1;
	}

	bytes_written = snprintf(command, total_len, "%s %d", CMD_GETSCANPASSIVETIME, time);

	return bytes_written;
}

int
wl_android_set_scan_passive_time(struct net_device *dev, char *command)
{
	int error = 0;
	int time = 0;

	if (sscanf(command, "%*s %d", &time) != 1) {
		DHD_ERROR(("wl_android_set_scan_passive_time: Failed to get Parameter\n"));
		return -1;
	}
#ifdef CUSTOMER_SCAN_TIMEOUT_SETTING
	wl_cfg80211_custom_scan_time(dev, WL_CUSTOM_SCAN_PASSIVE_TIME, time);
	error = wldev_ioctl_set(dev, WLC_SET_SCAN_PASSIVE_TIME, &time, sizeof(time));
#endif /* CUSTOMER_SCAN_TIMEOUT_SETTING */
	if (error) {
		DHD_ERROR(("wl_android_set_scan_passive_time: Failed to set Scan Passive Time %d,"
			" error = %d\n",
			time, error));
		return -1;
	}

	return 0;
}

int wl_android_get_scan_home_time(struct net_device *dev, char *command, int total_len)
{
	int error = 0;
	int bytes_written = 0;
	int time = 0;

	error = wldev_ioctl_get(dev, WLC_GET_SCAN_HOME_TIME, &time, sizeof(time));
	if (error) {
		DHD_ERROR(("Failed to get Scan Home Time, error = %d\n", error));
		return -1;
	}

	bytes_written = snprintf(command, total_len, "%s %d", CMD_GETSCANHOMETIME, time);

	return bytes_written;
}

int wl_android_set_scan_home_time(struct net_device *dev, char *command)
{
	int error = 0;
	int time = 0;

	if (sscanf(command, "%*s %d", &time) != 1) {
		DHD_ERROR(("wl_android_set_scan_home_time: Failed to get Parameter\n"));
		return -1;
	}
#ifdef CUSTOMER_SCAN_TIMEOUT_SETTING
	wl_cfg80211_custom_scan_time(dev, WL_CUSTOM_SCAN_HOME_TIME, time);
	error = wldev_ioctl_set(dev, WLC_SET_SCAN_HOME_TIME, &time, sizeof(time));
#endif /* CUSTOMER_SCAN_TIMEOUT_SETTING */
	if (error) {
		DHD_ERROR(("wl_android_set_scan_home_time: Failed to set Scan Home Time %d,"
		" error = %d\n",
		time, error));
		return -1;
	}

	return 0;
}

int wl_android_get_scan_home_away_time(struct net_device *dev, char *command, int total_len)
{
	int error = 0;
	int bytes_written = 0;
	int time = 0;

	error = wldev_iovar_getint(dev, "scan_home_away_time", &time);
	if (error) {
		DHD_ERROR(("wl_android_get_scan_home_away_time: Failed to get Scan Home Away Time,"
		" error = %d\n",
		error));
		return -1;
	}

	bytes_written = snprintf(command, total_len, "%s %d", CMD_GETSCANHOMEAWAYTIME, time);

	return bytes_written;
}

int wl_android_set_scan_home_away_time(struct net_device *dev, char *command)
{
	int error = 0;
	int time = 0;

	if (sscanf(command, "%*s %d", &time) != 1) {
		DHD_ERROR(("wl_android_set_scan_home_away_time: Failed to get Parameter\n"));
		return -1;
	}
#ifdef CUSTOMER_SCAN_TIMEOUT_SETTING
	wl_cfg80211_custom_scan_time(dev, WL_CUSTOM_SCAN_HOME_AWAY_TIME, time);
	error = wldev_iovar_setint(dev, "scan_home_away_time", time);
#endif /* CUSTOMER_SCAN_TIMEOUT_SETTING */
	if (error) {
		DHD_ERROR(("wl_android_set_scan_home_away_time: Failed to set Scan Home Away"
		" Time %d, error = %d\n",
		 time, error));
		return -1;
	}

	return 0;
}

int wl_android_get_scan_nprobes(struct net_device *dev, char *command, int total_len)
{
	int error = 0;
	int bytes_written = 0;
	int num = 0;

	error = wldev_ioctl_get(dev, WLC_GET_SCAN_NPROBES, &num, sizeof(num));
	if (error) {
		DHD_ERROR(("wl_android_get_scan_nprobes: Failed to get Scan NProbes,"
		" error = %d\n", error));
		return -1;
	}

	bytes_written = snprintf(command, total_len, "%s %d", CMD_GETSCANNPROBES, num);

	return bytes_written;
}

int wl_android_set_scan_nprobes(struct net_device *dev, char *command)
{
	int error = 0;
	int num = 0;

	if (sscanf(command, "%*s %d", &num) != 1) {
		DHD_ERROR(("wl_android_set_scan_nprobes: Failed to get Parameter\n"));
		return -1;
	}

	error = wldev_ioctl_set(dev, WLC_SET_SCAN_NPROBES, &num, sizeof(num));
	if (error) {
		DHD_ERROR(("wl_android_set_scan_nprobes: Failed to set Scan NProbes %d,"
		" error = %d\n",
		num, error));
		return -1;
	}

	return 0;
}

int wl_android_get_scan_dfs_channel_mode(struct net_device *dev, char *command, int total_len)
{
	int error = 0;
	int bytes_written = 0;
	int mode = 0;
	int scan_passive_time = 0;

	error = wldev_iovar_getint(dev, "scan_passive_time", &scan_passive_time);
	if (error) {
		DHD_ERROR(("wl_android_get_scan_dfs_channel_mode: Failed to get Passive Time,"
		" error = %d\n", error));
		return -1;
	}

	if (scan_passive_time == 0) {
		mode = 0;
	} else {
		mode = 1;
	}

	bytes_written = snprintf(command, total_len, "%s %d", CMD_GETDFSSCANMODE, mode);

	return bytes_written;
}

int wl_android_set_scan_dfs_channel_mode(struct net_device *dev, char *command)
{
	int error = 0;
	int mode = 0;
	int scan_passive_time = 0;

	if (sscanf(command, "%*s %d", &mode) != 1) {
		DHD_ERROR(("wl_android_set_scan_dfs_channel_mode: Failed to get Parameter\n"));
		return -1;
	}

	if (mode == 1) {
		scan_passive_time = DHD_SCAN_PASSIVE_TIME;
	} else if (mode == 0) {
		scan_passive_time = 0;
	} else {
		DHD_ERROR(("wl_android_set_scan_dfs_channel_mode: Failed to set Scan DFS"
		" channel mode %d, error = %d\n",
		 mode, error));
		return -1;
	}
	error = wldev_iovar_setint(dev, "scan_passive_time", scan_passive_time);
	if (error) {
		DHD_ERROR(("wl_android_set_scan_dfs_channel_mode: Failed to set Scan"
		" Passive Time %d, error = %d\n",
		scan_passive_time, error));
		return -1;
	}

	return 0;
}

#define JOINPREFFER_BUF_SIZE 12

static int
wl_android_set_join_prefer(struct net_device *dev, char *command)
{
	int error = BCME_OK;
	char smbuf[WLC_IOCTL_SMLEN];
	uint8 buf[JOINPREFFER_BUF_SIZE];
	char *pcmd;
	int total_len_left;
	int i;
	char hex[] = "XX";
#ifdef WBTEXT
	char commandp[WLC_IOCTL_SMLEN];
	char clear[] = { 0x01, 0x02, 0x00, 0x00, 0x03, 0x02, 0x00, 0x00, 0x04, 0x02, 0x00, 0x00 };
#endif /* WBTEXT */

	pcmd = command + strlen(CMD_SETJOINPREFER) + 1;
	total_len_left = strlen(pcmd);

	bzero(buf, sizeof(buf));

	if (total_len_left != JOINPREFFER_BUF_SIZE << 1) {
		DHD_ERROR(("wl_android_set_join_prefer: Failed to get Parameter\n"));
		return BCME_ERROR;
	}

	/* Store the MSB first, as required by join_pref */
	for (i = 0; i < JOINPREFFER_BUF_SIZE; i++) {
		hex[0] = *pcmd++;
		hex[1] = *pcmd++;
		buf[i] = (uint8)simple_strtoul(hex, NULL, 16);
	}

#ifdef WBTEXT
	/* No coexistance between 11kv and join pref */
	if (wl_android_check_wbtext(dev)) {
		bzero(commandp, sizeof(commandp));
		if (memcmp(buf, clear, sizeof(buf)) == 0) {
			snprintf(commandp, WLC_IOCTL_SMLEN, "WBTEXT_ENABLE 1");
		} else {
			snprintf(commandp, WLC_IOCTL_SMLEN, "WBTEXT_ENABLE 0");
		}
		if ((error = wl_android_wbtext(dev, commandp, WLC_IOCTL_SMLEN)) != BCME_OK) {
			DHD_ERROR(("Failed to set WBTEXT = %d\n", error));
			return error;
		}
	}
#endif /* WBTEXT */

	prhex("join pref", (uint8 *)buf, JOINPREFFER_BUF_SIZE);
	error = wldev_iovar_setbuf(dev, "join_pref", buf, JOINPREFFER_BUF_SIZE,
		smbuf, sizeof(smbuf), NULL);
	if (error) {
		DHD_ERROR(("Failed to set join_pref, error = %d\n", error));
	}

	return error;
}

int wl_android_send_action_frame(struct net_device *dev, char *command, int total_len)
{
	int error = -1;
	android_wifi_af_params_t *params = NULL;
	wl_action_frame_t *action_frame = NULL;
	wl_af_params_t *af_params = NULL;
	char *smbuf = NULL;
	struct ether_addr tmp_bssid;
	int tmp_channel = 0;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	if (total_len <
			(strlen(CMD_SENDACTIONFRAME) + 1 + sizeof(android_wifi_af_params_t))) {
		DHD_ERROR(("wl_android_send_action_frame: Invalid parameters \n"));
		goto send_action_frame_out;
	}

	params = (android_wifi_af_params_t *)(command + strlen(CMD_SENDACTIONFRAME) + 1);

	if ((uint16)params->len > ANDROID_WIFI_ACTION_FRAME_SIZE) {
		DHD_ERROR(("wl_android_send_action_frame: Requested action frame len"
			" was out of range(%d)\n",
			params->len));
		goto send_action_frame_out;
	}

	smbuf = (char *)MALLOC(cfg->osh, WLC_IOCTL_MAXLEN);
	if (smbuf == NULL) {
		DHD_ERROR(("wl_android_send_action_frame: failed to allocated memory %d bytes\n",
		WLC_IOCTL_MAXLEN));
		goto send_action_frame_out;
	}

	af_params = (wl_af_params_t *)MALLOCZ(cfg->osh, WL_WIFI_AF_PARAMS_SIZE);
	if (af_params == NULL) {
		DHD_ERROR(("wl_android_send_action_frame: unable to allocate frame\n"));
		goto send_action_frame_out;
	}

	bzero(&tmp_bssid, ETHER_ADDR_LEN);
	if (bcm_ether_atoe((const char *)params->bssid, (struct ether_addr *)&tmp_bssid) == 0) {
		bzero(&tmp_bssid, ETHER_ADDR_LEN);

		error = wldev_ioctl_get(dev, WLC_GET_BSSID, &tmp_bssid, ETHER_ADDR_LEN);
		if (error) {
			bzero(&tmp_bssid, ETHER_ADDR_LEN);
			DHD_ERROR(("wl_android_send_action_frame: failed to get bssid,"
				" error=%d\n", error));
			goto send_action_frame_out;
		}
	}

	if (params->channel < 0) {
		struct channel_info ci;
		bzero(&ci, sizeof(ci));
		error = wldev_ioctl_get(dev, WLC_GET_CHANNEL, &ci, sizeof(ci));
		if (error) {
			DHD_ERROR(("wl_android_send_action_frame: failed to get channel,"
				" error=%d\n", error));
			goto send_action_frame_out;
		}

		tmp_channel = ci.hw_channel;
	}
	else {
		tmp_channel = params->channel;
	}

	af_params->channel = tmp_channel;
	af_params->dwell_time = params->dwell_time;
	memcpy(&af_params->BSSID, &tmp_bssid, ETHER_ADDR_LEN);
	action_frame = &af_params->action_frame;

	action_frame->packetId = 0;
	memcpy(&action_frame->da, &tmp_bssid, ETHER_ADDR_LEN);
	action_frame->len = (uint16)params->len;
	memcpy(action_frame->data, params->data, action_frame->len);

	error = wldev_iovar_setbuf(dev, "actframe", af_params,
		sizeof(wl_af_params_t), smbuf, WLC_IOCTL_MAXLEN, NULL);
	if (error) {
		DHD_ERROR(("wl_android_send_action_frame: failed to set action frame,"
			" error=%d\n", error));
	}

send_action_frame_out:
	if (af_params) {
		MFREE(cfg->osh, af_params, WL_WIFI_AF_PARAMS_SIZE);
	}

	if (smbuf) {
		MFREE(cfg->osh, smbuf, WLC_IOCTL_MAXLEN);
	}

	if (error)
		return -1;
	else
		return 0;
}

int wl_android_reassoc(struct net_device *dev, char *command, int total_len)
{
	int error = 0;
	android_wifi_reassoc_params_t *params = NULL;
	uint band;
	chanspec_t channel;
	u32 params_size;
	wl_reassoc_params_t reassoc_params;

	if (total_len <
			(strlen(CMD_REASSOC) + 1 + sizeof(android_wifi_reassoc_params_t))) {
		DHD_ERROR(("wl_android_reassoc: Invalid parameters \n"));
		return -1;
	}
	params = (android_wifi_reassoc_params_t *)(command + strlen(CMD_REASSOC) + 1);

	bzero(&reassoc_params, WL_REASSOC_PARAMS_FIXED_SIZE);

	if (bcm_ether_atoe((const char *)params->bssid,
	(struct ether_addr *)&reassoc_params.bssid) == 0) {
		DHD_ERROR(("wl_android_reassoc: Invalid bssid \n"));
		return -1;
	}

	if (params->channel < 0) {
		DHD_ERROR(("wl_android_reassoc: Invalid Channel \n"));
		return -1;
	}

	reassoc_params.chanspec_num = 1;

	channel = params->channel;
#ifdef D11AC_IOTYPES
	if (wl_cfg80211_get_ioctl_version() == 1) {
		band = ((channel <= CH_MAX_2G_CHANNEL) ?
		WL_LCHANSPEC_BAND_2G : WL_LCHANSPEC_BAND_5G);
		reassoc_params.chanspec_list[0] = channel |
		band | WL_LCHANSPEC_BW_20 | WL_LCHANSPEC_CTL_SB_NONE;
	}
	else {
		band = ((channel <= CH_MAX_2G_CHANNEL) ? WL_CHANSPEC_BAND_2G : WL_CHANSPEC_BAND_5G);
		reassoc_params.chanspec_list[0] = channel | band | WL_CHANSPEC_BW_20;
	}
#else
	band = ((channel <= CH_MAX_2G_CHANNEL) ? WL_CHANSPEC_BAND_2G : WL_CHANSPEC_BAND_5G);
	reassoc_params.chanspec_list[0] = channel |
	band | WL_CHANSPEC_BW_20 | WL_CHANSPEC_CTL_SB_NONE;
#endif /* D11AC_IOTYPES */
	params_size = WL_REASSOC_PARAMS_FIXED_SIZE + sizeof(chanspec_t);

	error = wldev_ioctl_set(dev, WLC_REASSOC, &reassoc_params, params_size);
	if (error) {
		DHD_ERROR(("wl_android_reassoc: failed to reassoc, error=%d\n", error));
		return -1;
	}
	return 0;
}

int wl_android_get_wes_mode(struct net_device *dev, char *command, int total_len)
{
	int bytes_written = 0;
	int mode = 0;

	mode = wl_cfg80211_get_wes_mode();

	bytes_written = snprintf(command, total_len, "%s %d", CMD_GETWESMODE, mode);

	return bytes_written;
}

int wl_android_set_wes_mode(struct net_device *dev, char *command)
{
	int error = 0;
	int mode = 0;
#ifdef WBTEXT
	char commandp[WLC_IOCTL_SMLEN];
#endif /* WBTEXT */

	if (sscanf(command, "%*s %d", &mode) != 1) {
		DHD_ERROR(("wl_android_set_wes_mode: Failed to get Parameter\n"));
		return -1;
	}

	error = wl_cfg80211_set_wes_mode(mode);
	if (error) {
		DHD_ERROR(("wl_android_set_wes_mode: Failed to set WES Mode %d, error = %d\n",
		mode, error));
		return -1;
	}

#ifdef WBTEXT
	/* No coexistance between 11kv and FMC */
	if (wl_android_check_wbtext(dev)) {
		bzero(commandp, sizeof(commandp));
		if (!mode) {
			snprintf(commandp, WLC_IOCTL_SMLEN, "WBTEXT_ENABLE 1");
		} else {
			snprintf(commandp, WLC_IOCTL_SMLEN, "WBTEXT_ENABLE 0");
		}
		if ((error = wl_android_wbtext(dev, commandp, WLC_IOCTL_SMLEN)) != BCME_OK) {
			DHD_ERROR(("Failed to set WBTEXT = %d\n", error));
			return error;
		}
	}
#endif /* WBTEXT */

	return 0;
}

int wl_android_get_okc_mode(struct net_device *dev, char *command, int total_len)
{
	int error = 0;
	int bytes_written = 0;
	int mode = 0;

	error = wldev_iovar_getint(dev, "okc_enable", &mode);
	if (error) {
		DHD_ERROR(("wl_android_get_okc_mode: Failed to get OKC Mode, error = %d\n", error));
		return -1;
	}

	bytes_written = snprintf(command, total_len, "%s %d", CMD_GETOKCMODE, mode);

	return bytes_written;
}

int wl_android_set_okc_mode(struct net_device *dev, char *command)
{
	int error = 0;
	int mode = 0;

	if (sscanf(command, "%*s %d", &mode) != 1) {
		DHD_ERROR(("wl_android_set_okc_mode: Failed to get Parameter\n"));
		return -1;
	}

	error = wldev_iovar_setint(dev, "okc_enable", mode);
	if (error) {
		DHD_ERROR(("wl_android_set_okc_mode: Failed to set OKC Mode %d, error = %d\n",
		mode, error));
		return -1;
	}

	return error;
}
static int
wl_android_set_pmk(struct net_device *dev, char *command, int total_len)
{
	uchar pmk[33];
	int error = 0;
	char smbuf[WLC_IOCTL_SMLEN];
	dhd_pub_t *dhdp;
#ifdef OKC_DEBUG
	int i = 0;
#endif // endif

	if (total_len < (strlen("SET_PMK ") + 32)) {
		DHD_ERROR(("wl_android_set_pmk: Invalid argument\n"));
		return -1;
	}

	dhdp = wl_cfg80211_get_dhdp(dev);
	if (!dhdp) {
		DHD_ERROR(("%s: dhdp is NULL\n", __FUNCTION__));
		return -1;
	}

	bzero(pmk, sizeof(pmk));
	DHD_STATLOG_CTRL(dhdp, ST(INSTALL_OKC_PMK), dhd_net2idx(dhdp->info, dev), 0);
	memcpy((char *)pmk, command + strlen("SET_PMK "), 32);
	error = wldev_iovar_setbuf(dev, "okc_info_pmk", pmk, 32, smbuf, sizeof(smbuf), NULL);
	if (error) {
		DHD_ERROR(("Failed to set PMK for OKC, error = %d\n", error));
	}
#ifdef OKC_DEBUG
	DHD_ERROR(("PMK is "));
	for (i = 0; i < 32; i++)
		DHD_ERROR(("%02X ", pmk[i]));

	DHD_ERROR(("\n"));
#endif // endif
	return error;
}

static int
wl_android_okc_enable(struct net_device *dev, char *command)
{
	int error = 0;
	char okc_enable = 0;

	okc_enable = command[strlen(CMD_OKC_ENABLE) + 1] - '0';
	error = wldev_iovar_setint(dev, "okc_enable", okc_enable);
	if (error) {
		DHD_ERROR(("Failed to %s OKC, error = %d\n",
			okc_enable ? "enable" : "disable", error));
	}

	return error;
}
#endif /* WES_SUPPORT */

#ifdef SUPPORT_RESTORE_SCAN_PARAMS
	static int
wl_android_restore_scan_params(struct net_device *dev, char *command, int total_len)
{
	int error = 0;
	uint error_cnt = 0;
	int cnt = 0;
	char restore_command[WLC_IOCTL_SMLEN];

	while (strlen(restore_params[cnt].command) > 0 && restore_params[cnt].cmd_handler) {
		sprintf(restore_command, "%s %d", restore_params[cnt].command,
			restore_params[cnt].parameter);
		if (restore_params[cnt].cmd_type == RESTORE_TYPE_PRIV_CMD) {
			error = restore_params[cnt].cmd_handler(dev, restore_command);
		}  else if (restore_params[cnt].cmd_type == RESTORE_TYPE_PRIV_CMD_WITH_LEN) {
			error = restore_params[cnt].cmd_handler_w_len(dev,
				restore_command, total_len);
		} else {
			DHD_ERROR(("Unknown restore command handler\n"));
			error = -1;
		}
		if (error) {
			DHD_ERROR(("Failed to restore scan parameters %s, error : %d\n",
				restore_command, error));
			error_cnt++;
		}
		cnt++;
	}
	if (error_cnt > 0) {
		DHD_ERROR(("Got %d error(s) while restoring scan parameters\n",
			error_cnt));
		error = -1;
	}
	return error;
}
#endif /* SUPPORT_RESTORE_SCAN_PARAMS */

#ifdef WLTDLS
int wl_android_tdls_reset(struct net_device *dev)
{
	int ret = 0;
	ret = dhd_tdls_enable(dev, false, false, NULL);
	if (ret < 0) {
		DHD_ERROR(("Disable tdls failed. %d\n", ret));
		return ret;
	}
	ret = dhd_tdls_enable(dev, true, true, NULL);
	if (ret < 0) {
		DHD_ERROR(("enable tdls failed. %d\n", ret));
		return ret;
	}
	return 0;
}
#endif /* WLTDLS */

#ifdef CONFIG_SILENT_ROAM
int
wl_android_sroam_turn_on(struct net_device *dev, const char* turn)
{
	int ret = BCME_OK, sroam_mode;
	dhd_pub_t *dhdp = wl_cfg80211_get_dhdp(dev);

	sroam_mode = bcm_atoi(turn);
	dhdp->sroam_turn_on = sroam_mode;
	DHD_INFO(("%s Silent mode %s\n", __FUNCTION__,
		sroam_mode ? "enable" : "disable"));

	if (!sroam_mode) {
		ret = dhd_sroam_set_mon(dhdp, FALSE);
		if (ret) {
			DHD_ERROR(("%s Failed to Set sroam %d\n",
				__FUNCTION__, ret));
		}
	}

	return ret;
}

int
wl_android_sroam_set_info(struct net_device *dev, char *data,
	char *command, int total_len)
{
	int ret = BCME_OK;
	dhd_pub_t *dhdp = wl_cfg80211_get_dhdp(dev);
	size_t slen = strlen(data);
	u8 ioctl_buf[WLC_IOCTL_SMLEN];
	wlc_sroam_t *psroam;
	wlc_sroam_info_t *sroam;
	uint sroamlen = sizeof(*sroam) + SROAM_HDRLEN;

	data[slen] = '\0';
	psroam = (wlc_sroam_t *)MALLOCZ(dhdp->osh, sroamlen);
	if (!psroam) {
		WL_ERR(("%s Fail to malloc buffer\n", __FUNCTION__));
		ret = BCME_NOMEM;
		goto done;
	}

	psroam->ver = WLC_SILENT_ROAM_CUR_VER;
	psroam->len = sizeof(*sroam);
	sroam = (wlc_sroam_info_t *)psroam->data;

	sroam->sroam_on = FALSE;
	if (*data && *data != '\0') {
		sroam->sroam_min_rssi = simple_strtol(data, &data, 10);
		WL_DBG(("1.Minimum RSSI %d\n", sroam->sroam_min_rssi));
		data++;
	}
	if (*data && *data != '\0') {
		sroam->sroam_rssi_range = simple_strtol(data, &data, 10);
		WL_DBG(("2.RSSI Range %d\n", sroam->sroam_rssi_range));
		data++;
	}
	if (*data && *data != '\0') {
		sroam->sroam_score_delta = simple_strtol(data, &data, 10);
		WL_DBG(("3.Score Delta %d\n", sroam->sroam_score_delta));
		data++;
	}
	if (*data && *data != '\0') {
		sroam->sroam_period_time = simple_strtol(data, &data, 10);
		WL_DBG(("4.Sroam period %d\n", sroam->sroam_period_time));
		data++;
	}
	if (*data && *data != '\0') {
		sroam->sroam_band = simple_strtol(data, &data, 10);
		WL_DBG(("5.Sroam Band %d\n", sroam->sroam_band));
		data++;
	}
	if (*data && *data != '\0') {
		sroam->sroam_inact_cnt = simple_strtol(data, &data, 10);
		WL_DBG(("6.Inactivity Count %d\n", sroam->sroam_inact_cnt));
		data++;
	}

	if (*data != '\0') {
		ret = BCME_BADARG;
		goto done;
	}

	ret = wldev_iovar_setbuf(dev, "sroam", psroam, sroamlen, ioctl_buf,
		sizeof(ioctl_buf), NULL);
	if (ret) {
		WL_ERR(("Failed to set silent roam info(%d)\n", ret));
		goto done;
	}
done:
	if (psroam) {
		MFREE(dhdp->osh, psroam, sroamlen);
	}

	return ret;
}

int
wl_android_sroam_get_info(struct net_device *dev, char *command, int total_len)
{
	int ret = BCME_OK;
	int bytes_written = 0;
	dhd_pub_t *dhdp = wl_cfg80211_get_dhdp(dev);
	wlc_sroam_t *psroam;
	wlc_sroam_info_t *sroam;
	uint sroamlen = sizeof(*sroam) + SROAM_HDRLEN;

	psroam = (wlc_sroam_t *)MALLOCZ(dhdp->osh, sroamlen);
	if (!psroam) {
		WL_ERR(("%s Fail to malloc buffer\n", __FUNCTION__));
		ret = BCME_NOMEM;
		goto done;
	}

	ret = wldev_iovar_getbuf(dev, "sroam", NULL, 0, psroam, sroamlen, NULL);
	if (ret) {
		WL_ERR(("Failed to get silent roam info(%d)\n", ret));
		goto done;
	}

	if (psroam->ver != WLC_SILENT_ROAM_CUR_VER) {
		ret = BCME_VERSION;
		WL_ERR(("Ver(%d:%d). mismatch silent roam info(%d)\n",
			psroam->ver, WLC_SILENT_ROAM_CUR_VER, ret));
		goto done;
	}

	sroam = (wlc_sroam_info_t *)psroam->data;
	bytes_written = snprintf(command, total_len,
		"%s %d %d %d %d %d %d %d\n",
		CMD_SROAM_GET_INFO, sroam->sroam_on, sroam->sroam_min_rssi, sroam->sroam_rssi_range,
		sroam->sroam_score_delta, sroam->sroam_period_time, sroam->sroam_band,
		sroam->sroam_inact_cnt);
	ret = bytes_written;

	WL_DBG(("%s", command));
done:
	if (psroam) {
		MFREE(dhdp->osh, psroam, sroamlen);
	}

	return ret;
}
#endif /* CONFIG_SILENT_ROAM */

static int
get_int_bytes(uchar *oui_str, uchar *oui, int len)
{
	int idx;
	uchar val;
	uchar *src, *dest;
	char hexstr[3];

	if ((oui_str == NULL) || (oui == NULL) || (len == 0)) {
		return BCME_BADARG;
	}
	src = oui_str;
	dest = oui;

	for (idx = 0; idx < len; idx++) {
		if (*src == '\0') {
			*dest = '\0';
			break;
		}
		hexstr[0] = src[0];
		hexstr[1] = src[1];
		hexstr[2] = '\0';

		val = (uchar)bcm_strtoul(hexstr, NULL, 16);
		if (val == (uchar)-1) {
			return BCME_ERROR;
		}
		*dest++ = val;
		src += 2;
	}
	return BCME_OK;
}

#define TAG_BYTE 0
static int
wl_android_set_disconnect_ies(struct net_device *dev, char *command)
{
	int cmd_prefix_len = 0;
	char ie_len = 0;
	int hex_ie_len = 0;
	int total_len = 0;
	int max_len = 0;
	int cmd_len = 0;
	uchar disassoc_ie[VNDR_IE_MAX_LEN] = {0};
	s32 bssidx = 0;
	struct bcm_cfg80211 *cfg = NULL;
	s32 ret = 0;
	cfg = wl_get_cfg(dev);

	cmd_prefix_len = strlen("SET_DISCONNECT_IES ");
	cmd_len = strlen(command);
	/*
	 * <CMD> + <IES in HEX format>
	 * IES in hex format has to be in following format
	 * First byte = Tag, Second Byte = len and rest of
	 * bytes will be value. For ex: SET_DISCONNECT_IES dd0411223344
	 * tag = dd, len =04. Total IEs len = len + 2
	 */
	WL_DBG(("cmd recv = %s\n", command));
	max_len = MIN(cmd_len, VNDR_IE_MAX_LEN);
	/* Validate IEs len */
	get_int_bytes(&command[cmd_prefix_len + 2], &ie_len, 1);
	WL_INFORM_MEM(("ie_len = %d \n", ie_len));
	if (ie_len <= 0 || ie_len > max_len) {
		ret = BCME_BADLEN;
		return ret;
	}

	/* Total len in hex is sum of double binary len, tag and len byte */
	hex_ie_len = (ie_len * 2) + 4;
	total_len = cmd_prefix_len + hex_ie_len;
	if (command[total_len] != '\0' || (cmd_len != total_len)) {
		WL_ERR(("command recv not matching with len, command = %s"
			"total_len = %d, cmd_len = %d\n", command, total_len, cmd_len));
		ret = BCME_BADARG;
		return ret;
	}

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find index failed\n"));
		ret = -EINVAL;
		return ret;
	}

	/* Tag and len bytes are also part of total len of ies in binary */
	ie_len = ie_len + 2;
	/* Convert IEs in binary */
	get_int_bytes(&command[cmd_prefix_len], disassoc_ie, ie_len);
	if (disassoc_ie[TAG_BYTE] != 0xdd) {
		WL_ERR(("Wrong tag recv, tag = 0x%02x\n", disassoc_ie[TAG_BYTE]));
		ret = BCME_UNSUPPORTED;
		return ret;
	}

	ret = wl_cfg80211_set_mgmt_vndr_ies(cfg,
		ndev_to_cfgdev(dev), bssidx, VNDR_IE_DISASSOC_FLAG, disassoc_ie, ie_len);

	return ret;
}

#ifdef FCC_PWR_LIMIT_2G
int
wl_android_set_fcc_pwr_limit_2g(struct net_device *dev, char *command)
{
	int error = 0;
	int enable = 0;

	sscanf(command+sizeof("SET_FCC_CHANNEL"), "%d", &enable);

	if ((enable != CUSTOMER_HW4_ENABLE) && (enable != CUSTOMER_HW4_DISABLE)) {
		DHD_ERROR(("wl_android_set_fcc_pwr_limit_2g: Invalid data\n"));
		return BCME_ERROR;
	}

	CUSTOMER_HW4_EN_CONVERT(enable);

	DHD_ERROR(("wl_android_set_fcc_pwr_limit_2g: fccpwrlimit2g set (%d)\n", enable));
	error = wldev_iovar_setint(dev, "fccpwrlimit2g", enable);
	if (error) {
		DHD_ERROR(("wl_android_set_fcc_pwr_limit_2g: fccpwrlimit2g"
			" set returned (%d)\n", error));
		return BCME_ERROR;
	}

	return error;
}

int
wl_android_get_fcc_pwr_limit_2g(struct net_device *dev, char *command, int total_len)
{
	int error = 0;
	int enable = 0;
	int bytes_written = 0;

	error = wldev_iovar_getint(dev, "fccpwrlimit2g", &enable);
	if (error) {
		DHD_ERROR(("wl_android_get_fcc_pwr_limit_2g: fccpwrlimit2g get"
			" error (%d)\n", error));
		return BCME_ERROR;
	}
	DHD_ERROR(("wl_android_get_fcc_pwr_limit_2g: fccpwrlimit2g get (%d)\n", enable));

	bytes_written = snprintf(command, total_len, "%s %d", CMD_GET_FCC_PWR_LIMIT_2G, enable);

	return bytes_written;
}
#endif /* FCC_PWR_LIMIT_2G */

s32
wl_cfg80211_get_sta_info(struct net_device *dev, char* command, int total_len)
{
	int bytes_written = -1, ret = 0;
	char *pcmd = command;
	char *str;
	sta_info_v4_t *sta = NULL;
	const wl_cnt_wlc_t* wlc_cnt = NULL;
	struct ether_addr mac;
	char *iovar_buf;
	/* Client information */
	uint16 cap = 0;
	uint32 rxrtry = 0;
	uint32 rxmulti = 0;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

#ifdef BIGDATA_SOFTAP
	void *data = NULL;
	int get_bigdata_softap = FALSE;
	wl_ap_sta_data_t *sta_data = NULL;
	struct bcm_cfg80211 *bcm_cfg = wl_get_cfg(dev);
#endif /* BIGDATA_SOFTAP */

	WL_DBG(("%s\n", command));

	iovar_buf = (char *)MALLOCZ(cfg->osh, WLC_IOCTL_MAXLEN);
	if (iovar_buf == NULL) {
		DHD_ERROR(("wl_cfg80211_get_sta_info: failed to allocated memory %d bytes\n",
		WLC_IOCTL_MAXLEN));
		goto error;
	}

	str = bcmstrtok(&pcmd, " ", NULL);
	if (str) {
		str = bcmstrtok(&pcmd, " ", NULL);
		/* If GETSTAINFO subcmd name is not provided, return error */
		if (str == NULL) {
			WL_ERR(("GETSTAINFO subcmd not provided wl_cfg80211_get_sta_info\n"));
			goto error;
		}

		bzero(&mac, ETHER_ADDR_LEN);
		if ((bcm_ether_atoe((str), &mac))) {
			/* get the sta info */
			ret = wldev_iovar_getbuf(dev, "sta_info",
				(struct ether_addr *)mac.octet,
				ETHER_ADDR_LEN, iovar_buf, WLC_IOCTL_SMLEN, NULL);
#ifdef BIGDATA_SOFTAP
			get_bigdata_softap = TRUE;
#endif /* BIGDATA_SOFTAP */
			if (ret < 0) {
				WL_ERR(("Get sta_info ERR %d\n", ret));
#ifndef BIGDATA_SOFTAP
				goto error;
#else
				goto get_bigdata;
#endif /* BIGDATA_SOFTAP */
			}

			sta = (sta_info_v4_t *)iovar_buf;
			if (dtoh16(sta->ver) != WL_STA_VER_4) {
				WL_ERR(("sta_info struct version mismatch, "
					"host ver : %d, fw ver : %d\n", WL_STA_VER_4,
					dtoh16(sta->ver)));
				goto error;
			}
			cap = dtoh16(sta->cap);
			rxrtry = dtoh32(sta->rx_pkts_retried);
			rxmulti = dtoh32(sta->rx_mcast_pkts);
		} else if ((!strncmp(str, "all", 3)) || (!strncmp(str, "ALL", 3))) {
			/* get counters info */
			ret = wldev_iovar_getbuf(dev, "counters", NULL, 0,
				iovar_buf, WLC_IOCTL_MAXLEN, NULL);
			if (unlikely(ret)) {
				WL_ERR(("counters error (%d) - size = %zu\n",
					ret, sizeof(wl_cnt_wlc_t)));
				goto error;
			}
			ret = wl_cntbuf_to_xtlv_format(NULL, iovar_buf, WL_CNTBUF_MAX_SIZE, 0);
			if (ret != BCME_OK) {
				WL_ERR(("wl_cntbuf_to_xtlv_format ERR %d\n", ret));
				goto error;
			}
			if (!(wlc_cnt = GET_WLCCNT_FROM_CNTBUF(iovar_buf))) {
				WL_ERR(("wlc_cnt NULL!\n"));
				goto error;
			}

			rxrtry = dtoh32(wlc_cnt->rxrtry);
			rxmulti = dtoh32(wlc_cnt->rxmulti);
		} else {
			WL_ERR(("Get address fail\n"));
			goto error;
		}
	} else {
		WL_ERR(("Command ERR\n"));
		goto error;
	}

#ifdef BIGDATA_SOFTAP
get_bigdata:
	if (get_bigdata_softap) {
		WL_ERR(("mac " MACDBG" \n", MAC2STRDBG((char*)&mac)));
		if (wl_get_ap_stadata(bcm_cfg, &mac, &data) == BCME_OK) {
			sta_data = (wl_ap_sta_data_t *)data;
			bytes_written = snprintf(command, total_len,
					"%s %s Rx_Retry_Pkts=%d Rx_BcMc_Pkts=%d "
					"CAP=%04x "MACOUI" %d %s %d %d %d %d %d %d\n",
					CMD_GET_STA_INFO, str, rxrtry, rxmulti, cap,
					MACOUI2STR((char*)&sta_data->mac),
					sta_data->channel,
					wf_chspec_to_bw_str(sta_data->chanspec),
					sta_data->rssi, sta_data->rate,
					sta_data->mode_80211, sta_data->nss, sta_data->mimo,
					sta_data->reason_code);
			WL_ERR_KERN(("command %s\n", command));
			goto error;
		}
	}
#endif /* BIGDATA_SOFTAP */
	bytes_written = snprintf(command, total_len,
		"%s %s Rx_Retry_Pkts=%d Rx_BcMc_Pkts=%d CAP=%04x\n",
		CMD_GET_STA_INFO, str, rxrtry, rxmulti, cap);

	WL_DBG(("%s", command));

error:
	if (iovar_buf) {
		MFREE(cfg->osh, iovar_buf, WLC_IOCTL_MAXLEN);

	}
	return bytes_written;
}
#endif /* CUSTOMER_HW4_PRIVATE_CMD */

#ifdef WBTEXT
static int wl_android_wbtext(struct net_device *dev, char *command, int total_len)
{
	int error = BCME_OK, argc = 0;
	int data, bytes_written;
	int roam_trigger[2];
	dhd_pub_t *dhdp = wl_cfg80211_get_dhdp(dev);

	argc = sscanf(command+sizeof(CMD_WBTEXT_ENABLE), "%d", &data);
	if (!argc) {
		error = wldev_iovar_getint(dev, "wnm_bsstrans_resp", &data);
		if (error) {
			DHD_ERROR(("wl_android_wbtext: Failed to set wbtext error = %d\n",
				error));
			return error;
		}
		bytes_written = snprintf(command, total_len, "WBTEXT %s\n",
				(data == WL_BSSTRANS_POLICY_PRODUCT_WBTEXT)?
				"ENABLED" : "DISABLED");
		return bytes_written;
	} else {
		if (data) {
			data = WL_BSSTRANS_POLICY_PRODUCT_WBTEXT;
		}

		if ((error = wldev_iovar_setint(dev, "wnm_bsstrans_resp", data)) != BCME_OK) {
			DHD_ERROR(("wl_android_wbtext: Failed to set wbtext error = %d\n",
				error));
			return error;
		}

		if (data) {
			/* reset roam_prof when wbtext is on */
			if ((error = wl_cfg80211_wbtext_set_default(dev)) != BCME_OK) {
				return error;
			}
			dhdp->wbtext_support = TRUE;
		} else {
			/* reset legacy roam trigger when wbtext is off */
			roam_trigger[0] = DEFAULT_ROAM_TRIGGER_VALUE;
			roam_trigger[1] = WLC_BAND_ALL;
			if ((error = wldev_ioctl_set(dev, WLC_SET_ROAM_TRIGGER, roam_trigger,
					sizeof(roam_trigger))) != BCME_OK) {
				DHD_ERROR(("wl_android_wbtext: Failed to reset roam trigger = %d\n",
					error));
				return error;
			}
			dhdp->wbtext_support = FALSE;
		}
	}
	return error;
}

static int wl_cfg80211_wbtext_btm_timer_threshold(struct net_device *dev,
	char *command, int total_len)
{
	int error = BCME_OK, argc = 0;
	int data, bytes_written;

	argc = sscanf(command, CMD_WBTEXT_BTM_TIMER_THRESHOLD " %d\n", &data);
	if (!argc) {
		error = wldev_iovar_getint(dev, "wnm_bsstrans_timer_threshold", &data);
		if (error) {
			WL_ERR(("Failed to get wnm_bsstrans_timer_threshold (%d)\n", error));
			return error;
		}
		bytes_written = snprintf(command, total_len, "%d\n", data);
		return bytes_written;
	} else {
		if ((error = wldev_iovar_setint(dev, "wnm_bsstrans_timer_threshold",
				data)) != BCME_OK) {
			WL_ERR(("Failed to set wnm_bsstrans_timer_threshold (%d)\n", error));
			return error;
		}
	}
	return error;
}

static int wl_cfg80211_wbtext_btm_delta(struct net_device *dev,
	char *command, int total_len)
{
	int error = BCME_OK, argc = 0;
	int data = 0, bytes_written;

	argc = sscanf(command, CMD_WBTEXT_BTM_DELTA " %d\n", &data);
	if (!argc) {
		error = wldev_iovar_getint(dev, "wnm_btmdelta", &data);
		if (error) {
			WL_ERR(("Failed to get wnm_btmdelta (%d)\n", error));
			return error;
		}
		bytes_written = snprintf(command, total_len, "%d\n", data);
		return bytes_written;
	} else {
		if ((error = wldev_iovar_setint(dev, "wnm_btmdelta",
				data)) != BCME_OK) {
			WL_ERR(("Failed to set wnm_btmdelta (%d)\n", error));
			return error;
		}
	}
	return error;
}

static int wl_cfg80211_wbtext_estm_enable(struct net_device *dev,
	char *command, int total_len)
{
	int error = BCME_OK;
	int data = 0, bytes_written = 0;
	int wnmmask = 0;
	char *pcmd = command;

	bcmstrtok(&pcmd, " ", NULL);

	error = wldev_iovar_getint(dev, "wnm", &wnmmask);
	if (error) {
		WL_ERR(("Failed to get wnm_btmdelta (%d)\n", error));
		return error;
	}
	WL_DBG(("wnmmask %x\n", wnmmask));
	if (*pcmd == WL_IOCTL_ACTION_GET) {
		bytes_written = snprintf(command, total_len, "wbtext_estm_enable %d\n",
			(wnmmask & WL_WNM_ESTM) ? 1:0);
		return bytes_written;
	} else {
		data = bcm_atoi(pcmd);
		if (data == 0) {
			wnmmask &= ~WL_WNM_ESTM;
		} else {
			wnmmask |= WL_WNM_ESTM;
		}
		WL_DBG(("wnmmask %x\n", wnmmask));
		if ((error = wldev_iovar_setint(dev, "wnm", wnmmask)) != BCME_OK) {
			WL_ERR(("Failed to set wnm mask (%d)\n", error));
			return error;
		}
	}
	return error;
}
#endif /* WBTEXT */

#ifdef PNO_SUPPORT
#define PNO_PARAM_SIZE 50
#define VALUE_SIZE 50
#define LIMIT_STR_FMT  ("%50s %50s")
static int
wls_parse_batching_cmd(struct net_device *dev, char *command, int total_len)
{
	int err = BCME_OK;
	uint i, tokens, len_remain;
	char *pos, *pos2, *token, *token2, *delim;
	char param[PNO_PARAM_SIZE+1], value[VALUE_SIZE+1];
	struct dhd_pno_batch_params batch_params;

	DHD_PNO(("wls_parse_batching_cmd: command=%s, len=%d\n", command, total_len));
	len_remain = total_len;
	if (len_remain > (strlen(CMD_WLS_BATCHING) + 1)) {
		pos = command + strlen(CMD_WLS_BATCHING) + 1;
		len_remain -= strlen(CMD_WLS_BATCHING) + 1;
	} else {
		WL_ERR(("wls_parse_batching_cmd: No arguments, total_len %d\n", total_len));
		err = BCME_ERROR;
		goto exit;
	}
	bzero(&batch_params, sizeof(struct dhd_pno_batch_params));
	if (!strncmp(pos, PNO_BATCHING_SET, strlen(PNO_BATCHING_SET))) {
		if (len_remain > (strlen(PNO_BATCHING_SET) + 1)) {
			pos += strlen(PNO_BATCHING_SET) + 1;
		} else {
			WL_ERR(("wls_parse_batching_cmd: %s missing arguments, total_len %d\n",
				PNO_BATCHING_SET, total_len));
			err = BCME_ERROR;
			goto exit;
		}
		while ((token = strsep(&pos, PNO_PARAMS_DELIMETER)) != NULL) {
			bzero(param, sizeof(param));
			bzero(value, sizeof(value));
			if (token == NULL || !*token)
				break;
			if (*token == '\0')
				continue;
			delim = strchr(token, PNO_PARAM_VALUE_DELLIMETER);
			if (delim != NULL)
				*delim = ' ';

			tokens = sscanf(token, LIMIT_STR_FMT, param, value);
			if (!strncmp(param, PNO_PARAM_SCANFREQ, strlen(PNO_PARAM_SCANFREQ))) {
				batch_params.scan_fr = simple_strtol(value, NULL, 0);
				DHD_PNO(("scan_freq : %d\n", batch_params.scan_fr));
			} else if (!strncmp(param, PNO_PARAM_BESTN, strlen(PNO_PARAM_BESTN))) {
				batch_params.bestn = simple_strtol(value, NULL, 0);
				DHD_PNO(("bestn : %d\n", batch_params.bestn));
			} else if (!strncmp(param, PNO_PARAM_MSCAN, strlen(PNO_PARAM_MSCAN))) {
				batch_params.mscan = simple_strtol(value, NULL, 0);
				DHD_PNO(("mscan : %d\n", batch_params.mscan));
			} else if (!strncmp(param, PNO_PARAM_CHANNEL, strlen(PNO_PARAM_CHANNEL))) {
				i = 0;
				pos2 = value;
				tokens = sscanf(value, "<%s>", value);
				if (tokens != 1) {
					err = BCME_ERROR;
					DHD_ERROR(("wls_parse_batching_cmd: invalid format"
					" for channel"
					" <> params\n"));
					goto exit;
				}
				while ((token2 = strsep(&pos2,
						PNO_PARAM_CHANNEL_DELIMETER)) != NULL) {
					if (token2 == NULL || !*token2)
						break;
					if (*token2 == '\0')
						continue;
					if (*token2 == 'A' || *token2 == 'B') {
						batch_params.band = (*token2 == 'A')?
							WLC_BAND_5G : WLC_BAND_2G;
						DHD_PNO(("band : %s\n",
							(*token2 == 'A')? "A" : "B"));
					} else {
						if ((batch_params.nchan >= WL_NUMCHANNELS) ||
							(i >= WL_NUMCHANNELS)) {
							DHD_ERROR(("Too many nchan %d\n",
								batch_params.nchan));
							err = BCME_BUFTOOSHORT;
							goto exit;
						}
						batch_params.chan_list[i++] =
							simple_strtol(token2, NULL, 0);
						batch_params.nchan++;
						DHD_PNO(("channel :%d\n",
							batch_params.chan_list[i-1]));
					}
				 }
			} else if (!strncmp(param, PNO_PARAM_RTT, strlen(PNO_PARAM_RTT))) {
				batch_params.rtt = simple_strtol(value, NULL, 0);
				DHD_PNO(("rtt : %d\n", batch_params.rtt));
			} else {
				DHD_ERROR(("wls_parse_batching_cmd : unknown param: %s\n", param));
				err = BCME_ERROR;
				goto exit;
			}
		}
		err = dhd_dev_pno_set_for_batch(dev, &batch_params);
		if (err < 0) {
			DHD_ERROR(("failed to configure batch scan\n"));
		} else {
			bzero(command, total_len);
			err = snprintf(command, total_len, "%d", err);
		}
	} else if (!strncmp(pos, PNO_BATCHING_GET, strlen(PNO_BATCHING_GET))) {
		err = dhd_dev_pno_get_for_batch(dev, command, total_len);
		if (err < 0) {
			DHD_ERROR(("failed to getting batching results\n"));
		} else {
			err = strlen(command);
		}
	} else if (!strncmp(pos, PNO_BATCHING_STOP, strlen(PNO_BATCHING_STOP))) {
		err = dhd_dev_pno_stop_for_batch(dev);
		if (err < 0) {
			DHD_ERROR(("failed to stop batching scan\n"));
		} else {
			bzero(command, total_len);
			err = snprintf(command, total_len, "OK");
		}
	} else {
		DHD_ERROR(("wls_parse_batching_cmd : unknown command\n"));
		err = BCME_ERROR;
		goto exit;
	}
exit:
	return err;
}
#ifndef WL_SCHED_SCAN
static int wl_android_set_pno_setup(struct net_device *dev, char *command, int total_len)
{
	wlc_ssid_ext_t ssids_local[MAX_PFN_LIST_COUNT];
	int res = -1;
	int nssid = 0;
	cmd_tlv_t *cmd_tlv_temp;
	char *str_ptr;
	int tlv_size_left;
	int pno_time = 0;
	int pno_repeat = 0;
	int pno_freq_expo_max = 0;

#ifdef PNO_SET_DEBUG
	int i;
	char pno_in_example[] = {
		'P', 'N', 'O', 'S', 'E', 'T', 'U', 'P', ' ',
		'S', '1', '2', '0',
		'S',
		0x05,
		'd', 'l', 'i', 'n', 'k',
		'S',
		0x04,
		'G', 'O', 'O', 'G',
		'T',
		'0', 'B',
		'R',
		'2',
		'M',
		'2',
		0x00
		};
#endif /* PNO_SET_DEBUG */
	DHD_PNO(("wl_android_set_pno_setup: command=%s, len=%d\n", command, total_len));

	if (total_len < (strlen(CMD_PNOSETUP_SET) + sizeof(cmd_tlv_t))) {
		DHD_ERROR(("wl_android_set_pno_setup: argument=%d less min size\n", total_len));
		goto exit_proc;
	}
#ifdef PNO_SET_DEBUG
	memcpy(command, pno_in_example, sizeof(pno_in_example));
	total_len = sizeof(pno_in_example);
#endif // endif
	str_ptr = command + strlen(CMD_PNOSETUP_SET);
	tlv_size_left = total_len - strlen(CMD_PNOSETUP_SET);

	cmd_tlv_temp = (cmd_tlv_t *)str_ptr;
	bzero(ssids_local, sizeof(ssids_local));

	if ((cmd_tlv_temp->prefix == PNO_TLV_PREFIX) &&
		(cmd_tlv_temp->version == PNO_TLV_VERSION) &&
		(cmd_tlv_temp->subtype == PNO_TLV_SUBTYPE_LEGACY_PNO)) {

		str_ptr += sizeof(cmd_tlv_t);
		tlv_size_left -= sizeof(cmd_tlv_t);

		if ((nssid = wl_parse_ssid_list_tlv(&str_ptr, ssids_local,
			MAX_PFN_LIST_COUNT, &tlv_size_left)) <= 0) {
			DHD_ERROR(("SSID is not presented or corrupted ret=%d\n", nssid));
			goto exit_proc;
		} else {
			if ((str_ptr[0] != PNO_TLV_TYPE_TIME) || (tlv_size_left <= 1)) {
				DHD_ERROR(("wl_android_set_pno_setup: scan duration corrupted"
					" field size %d\n",
					tlv_size_left));
				goto exit_proc;
			}
			str_ptr++;
			pno_time = simple_strtoul(str_ptr, &str_ptr, 16);
			DHD_PNO(("wl_android_set_pno_setup: pno_time=%d\n", pno_time));

			if (str_ptr[0] != 0) {
				if ((str_ptr[0] != PNO_TLV_FREQ_REPEAT)) {
					DHD_ERROR(("wl_android_set_pno_setup: pno repeat:"
						" corrupted field\n"));
					goto exit_proc;
				}
				str_ptr++;
				pno_repeat = simple_strtoul(str_ptr, &str_ptr, 16);
				DHD_PNO(("wl_android_set_pno_setup: got pno_repeat=%d\n",
					pno_repeat));
				if (str_ptr[0] != PNO_TLV_FREQ_EXPO_MAX) {
					DHD_ERROR(("wl_android_set_pno_setup: FREQ_EXPO_MAX"
						" corrupted field size\n"));
					goto exit_proc;
				}
				str_ptr++;
				pno_freq_expo_max = simple_strtoul(str_ptr, &str_ptr, 16);
				DHD_PNO(("wl_android_set_pno_setup: pno_freq_expo_max=%d\n",
					pno_freq_expo_max));
			}
		}
	} else {
		DHD_ERROR(("wl_android_set_pno_setup: get wrong TLV command\n"));
		goto exit_proc;
	}

	res = dhd_dev_pno_set_for_ssid(dev, ssids_local, nssid, pno_time, pno_repeat,
		pno_freq_expo_max, NULL, 0);
exit_proc:
	return res;
}
#endif /* !WL_SCHED_SCAN */
#endif /* PNO_SUPPORT  */

static int wl_android_get_p2p_dev_addr(struct net_device *ndev, char *command, int total_len)
{
	int ret;
	struct ether_addr p2pdev_addr;

#define MAC_ADDR_STR_LEN 18
	if (total_len < MAC_ADDR_STR_LEN) {
		DHD_ERROR(("wl_android_get_p2p_dev_addr: buflen %d is less than p2p dev addr\n",
			total_len));
		return -1;
	}

	ret = wl_cfg80211_get_p2p_dev_addr(ndev, &p2pdev_addr);
	if (ret) {
		DHD_ERROR(("wl_android_get_p2p_dev_addr: Failed to get p2p dev addr\n"));
		return -1;
	}
	return (snprintf(command, total_len, MACF, ETHERP_TO_MACF(&p2pdev_addr)));
}

int
wl_android_set_ap_mac_list(struct net_device *dev, int macmode, struct maclist *maclist)
{
	int i, j, match;
	int ret	= 0;
	char mac_buf[MAX_NUM_OF_ASSOCLIST *
		sizeof(struct ether_addr) + sizeof(uint)] = {0};
	struct maclist *assoc_maclist = (struct maclist *)mac_buf;

	/* set filtering mode */
	if ((ret = wldev_ioctl_set(dev, WLC_SET_MACMODE, &macmode, sizeof(macmode)) != 0)) {
		DHD_ERROR(("wl_android_set_ap_mac_list : WLC_SET_MACMODE error=%d\n", ret));
		return ret;
	}
	if (macmode != MACLIST_MODE_DISABLED) {
		/* set the MAC filter list */
		if ((ret = wldev_ioctl_set(dev, WLC_SET_MACLIST, maclist,
			sizeof(int) + sizeof(struct ether_addr) * maclist->count)) != 0) {
			DHD_ERROR(("wl_android_set_ap_mac_list : WLC_SET_MACLIST error=%d\n", ret));
			return ret;
		}
		/* get the current list of associated STAs */
		assoc_maclist->count = MAX_NUM_OF_ASSOCLIST;
		if ((ret = wldev_ioctl_get(dev, WLC_GET_ASSOCLIST, assoc_maclist,
			sizeof(mac_buf))) != 0) {
			DHD_ERROR(("wl_android_set_ap_mac_list: WLC_GET_ASSOCLIST error=%d\n",
				ret));
			return ret;
		}
		/* do we have any STA associated?  */
		if (assoc_maclist->count) {
			/* iterate each associated STA */
			for (i = 0; i < assoc_maclist->count; i++) {
				match = 0;
				/* compare with each entry */
				for (j = 0; j < maclist->count; j++) {
					DHD_INFO(("wl_android_set_ap_mac_list: associated="MACDBG
					"list = "MACDBG "\n",
					MAC2STRDBG(assoc_maclist->ea[i].octet),
					MAC2STRDBG(maclist->ea[j].octet)));
					if (memcmp(assoc_maclist->ea[i].octet,
						maclist->ea[j].octet, ETHER_ADDR_LEN) == 0) {
						match = 1;
						break;
					}
				}
				/* do conditional deauth */
				/*   "if not in the allow list" or "if in the deny list" */
				if ((macmode == MACLIST_MODE_ALLOW && !match) ||
					(macmode == MACLIST_MODE_DENY && match)) {
					scb_val_t scbval;

					scbval.val = htod32(1);
					memcpy(&scbval.ea, &assoc_maclist->ea[i],
						ETHER_ADDR_LEN);
					if ((ret = wldev_ioctl_set(dev,
						WLC_SCB_DEAUTHENTICATE_FOR_REASON,
						&scbval, sizeof(scb_val_t))) != 0)
						DHD_ERROR(("wl_android_set_ap_mac_list:"
							" WLC_SCB_DEAUTHENTICATE"
							" error=%d\n",
							ret));
				}
			}
		}
	}
	return ret;
}

/*
 * HAPD_MAC_FILTER mac_mode mac_cnt mac_addr1 mac_addr2
 *
 */
#ifdef AUTOMOTIVE_FEATURE
static int
wl_android_set_mac_address_filter(struct net_device *dev, char* str)
{
	int i;
	int ret = 0;
	int macnum = 0;
	int macmode = MACLIST_MODE_DISABLED;
	struct maclist *list;
	char eabuf[ETHER_ADDR_STR_LEN];
	const char *token;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	/* string should look like below (macmode/macnum/maclist) */
	/*   1 2 00:11:22:33:44:55 00:11:22:33:44:ff  */

	/* get the MAC filter mode */
	token = strsep((char**)&str, " ");
	if (!token) {
		return -1;
	}
	macmode = bcm_atoi(token);

	if (macmode < MACLIST_MODE_DISABLED || macmode > MACLIST_MODE_ALLOW) {
		DHD_ERROR(("wl_android_set_mac_address_filter: invalid macmode %d\n", macmode));
		return -1;
	}

	token = strsep((char**)&str, " ");
	if (!token) {
		return -1;
	}
	macnum = bcm_atoi(token);
	if (macnum < 0 || macnum > MAX_NUM_MAC_FILT) {
		DHD_ERROR(("wl_android_set_mac_address_filter: invalid number of MAC"
			" address entries %d\n",
			macnum));
		return -1;
	}
	/* allocate memory for the MAC list */
	list = (struct maclist*) MALLOCZ(cfg->osh, sizeof(int) +
		sizeof(struct ether_addr) * macnum);
	if (!list) {
		DHD_ERROR(("wl_android_set_mac_address_filter : failed to allocate memory\n"));
		return -1;
	}
	/* prepare the MAC list */
	list->count = htod32(macnum);
	bzero((char *)eabuf, ETHER_ADDR_STR_LEN);
	for (i = 0; i < list->count; i++) {
		token = strsep((char**)&str, " ");
		if (token == NULL) {
			DHD_ERROR(("wl_android_set_mac_address_filter : No mac address present\n"));
			ret = -EINVAL;
			goto exit;
		}
		strlcpy(eabuf, token, sizeof(eabuf));
		if (!(ret = bcm_ether_atoe(eabuf, &list->ea[i]))) {
			DHD_ERROR(("wl_android_set_mac_address_filter : mac parsing err index=%d,"
				" addr=%s\n",
				i, eabuf));
			list->count = i;
			break;
		}
		DHD_INFO(("wl_android_set_mac_address_filter : %d/%d MACADDR=%s",
			i, list->count, eabuf));
	}
	if (i == 0)
		goto exit;

	/* set the list */
	if ((ret = wl_android_set_ap_mac_list(dev, macmode, list)) != 0)
		DHD_ERROR(("wl_android_set_mac_address_filter: Setting MAC list failed error=%d\n",
			ret));

exit:
	MFREE(cfg->osh, list, sizeof(int) + sizeof(struct ether_addr) * macnum);

	return ret;
}
#endif /* AUTOMOTIVE_FEATURE */
/**
 * Global function definitions (declared in wl_android.h)
 */

int wl_android_wifi_on(struct net_device *dev)
{
	int ret = 0;
	int retry = POWERUP_MAX_RETRY;

	DHD_ERROR(("wl_android_wifi_on in\n"));
	if (!dev) {
		DHD_ERROR(("wl_android_wifi_on: dev is null\n"));
		return -EINVAL;
	}

	dhd_net_if_lock(dev);
	if (!g_wifi_on) {
		do {
			dhd_net_wifi_platform_set_power(dev, TRUE, WIFI_TURNON_DELAY);
#ifdef BCMSDIO
			ret = dhd_net_bus_resume(dev, 0);
#endif /* BCMSDIO */
#ifdef BCMPCIE
			ret = dhd_net_bus_devreset(dev, FALSE);
#endif /* BCMPCIE */
			if (ret == 0) {
				break;
			}
			DHD_ERROR(("\nfailed to power up wifi chip, retry again (%d left) **\n\n",
				retry));
#ifdef BCMPCIE
			dhd_net_bus_devreset(dev, TRUE);
#endif /* BCMPCIE */
			dhd_net_wifi_platform_set_power(dev, FALSE, WIFI_TURNOFF_DELAY);
		} while (retry-- > 0);
		if (ret != 0) {
			DHD_ERROR(("\nfailed to power up wifi chip, max retry reached **\n\n"));
#ifdef BCM_DETECT_TURN_ON_FAILURE
			BUG_ON(1);
#endif /* BCM_DETECT_TURN_ON_FAILURE */
			goto exit;
		}
#ifdef BCMSDIO
		ret = dhd_net_bus_devreset(dev, FALSE);
		dhd_net_bus_resume(dev, 1);
#endif /* BCMSDIO */

#ifndef BCMPCIE
		if (!ret) {
			if (dhd_dev_init_ioctl(dev) < 0) {
				ret = -EFAULT;
			}
		}
#endif /* !BCMPCIE */
		g_wifi_on = TRUE;
	}

exit:
	dhd_net_if_unlock(dev);

	return ret;
}

int wl_android_wifi_off(struct net_device *dev, bool on_failure)
{
	int ret = 0;

	DHD_ERROR(("wl_android_wifi_off in\n"));
	if (!dev) {
		DHD_TRACE(("wl_android_wifi_off: dev is null\n"));
		return -EINVAL;
	}

#if defined(BCMPCIE) && defined(DHD_DEBUG_UART)
	ret = dhd_debug_uart_is_running(dev);
	if (ret) {
		DHD_ERROR(("wl_android_wifi_off: - Debug UART App is running\n"));
		return -EBUSY;
	}
#endif	/* BCMPCIE && DHD_DEBUG_UART */
	dhd_net_if_lock(dev);
	if (g_wifi_on || on_failure) {
#if defined(BCMSDIO) || defined(BCMPCIE)
		ret = dhd_net_bus_devreset(dev, TRUE);
#ifdef BCMSDIO
		dhd_net_bus_suspend(dev);
#endif /* BCMSDIO */
#endif /* BCMSDIO || BCMPCIE */
		dhd_net_wifi_platform_set_power(dev, FALSE, WIFI_TURNOFF_DELAY);
		g_wifi_on = FALSE;
	}
	dhd_net_if_unlock(dev);

	return ret;
}

static int wl_android_set_fwpath(struct net_device *net, char *command, int total_len)
{
	if ((strlen(command) - strlen(CMD_SETFWPATH)) > MOD_PARAM_PATHLEN)
		return -1;
	return dhd_net_set_fw_path(net, command + strlen(CMD_SETFWPATH) + 1);
}

#ifdef CONNECTION_STATISTICS
static int
wl_chanim_stats(struct net_device *dev, u8 *chan_idle)
{
	int err;
	wl_chanim_stats_t *list;
	/* Parameter _and_ returned buffer of chanim_stats. */
	wl_chanim_stats_t param;
	u8 result[WLC_IOCTL_SMLEN];
	chanim_stats_t *stats;

	bzero(&param, sizeof(param));

	param.buflen = htod32(sizeof(wl_chanim_stats_t));
	param.count = htod32(WL_CHANIM_COUNT_ONE);

	if ((err = wldev_iovar_getbuf(dev, "chanim_stats", (char*)&param, sizeof(wl_chanim_stats_t),
		(char*)result, sizeof(result), 0)) < 0) {
		WL_ERR(("Failed to get chanim results %d \n", err));
		return err;
	}

	list = (wl_chanim_stats_t*)result;

	list->buflen = dtoh32(list->buflen);
	list->version = dtoh32(list->version);
	list->count = dtoh32(list->count);

	if (list->buflen == 0) {
		list->version = 0;
		list->count = 0;
	} else if (list->version != WL_CHANIM_STATS_VERSION) {
		WL_ERR(("Sorry, firmware has wl_chanim_stats version %d "
			"but driver supports only version %d.\n",
				list->version, WL_CHANIM_STATS_VERSION));
		list->buflen = 0;
		list->count = 0;
	}

	stats = list->stats;
	stats->glitchcnt = dtoh32(stats->glitchcnt);
	stats->badplcp = dtoh32(stats->badplcp);
	stats->chanspec = dtoh16(stats->chanspec);
	stats->timestamp = dtoh32(stats->timestamp);
	stats->chan_idle = dtoh32(stats->chan_idle);

	WL_INFORM(("chanspec: 0x%4x glitch: %d badplcp: %d idle: %d timestamp: %d\n",
		stats->chanspec, stats->glitchcnt, stats->badplcp, stats->chan_idle,
		stats->timestamp));

	*chan_idle = stats->chan_idle;

	return (err);
}

static int
wl_android_get_connection_stats(struct net_device *dev, char *command, int total_len)
{
	static char iovar_buf[WLC_IOCTL_MAXLEN];
	const wl_cnt_wlc_t* wlc_cnt = NULL;
#ifndef DISABLE_IF_COUNTERS
	wl_if_stats_t* if_stats = NULL;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	dhd_pub_t *dhdp = wl_cfg80211_get_dhdp(dev);
#endif /* DISABLE_IF_COUNTERS */

	int link_speed = 0;
	struct connection_stats *output;
	unsigned int bufsize = 0;
	int bytes_written = -1;
	int ret = 0;

	WL_INFORM(("wl_android_get_connection_stats: enter Get Connection Stats\n"));

	if (total_len <= 0) {
		WL_ERR(("wl_android_get_connection_stats: invalid buffer size %d\n", total_len));
		goto error;
	}

	bufsize = total_len;
	if (bufsize < sizeof(struct connection_stats)) {
		WL_ERR(("wl_android_get_connection_stats: not enough buffer size, provided=%u,"
			" requires=%zu\n",
			bufsize,
			sizeof(struct connection_stats)));
		goto error;
	}

	output = (struct connection_stats *)command;

#ifndef DISABLE_IF_COUNTERS
	if_stats = (wl_if_stats_t *)MALLOCZ(cfg->osh, sizeof(*if_stats));
	if (if_stats == NULL) {
		WL_ERR(("wl_android_get_connection_stats: MALLOCZ failed\n"));
		goto error;
	}
	bzero(if_stats, sizeof(*if_stats));

	if (FW_SUPPORTED(dhdp, ifst)) {
		ret = wl_cfg80211_ifstats_counters(dev, if_stats);
	} else
	{
		ret = wldev_iovar_getbuf(dev, "if_counters", NULL, 0,
			(char *)if_stats, sizeof(*if_stats), NULL);
	}

	ret = wldev_iovar_getbuf(dev, "if_counters", NULL, 0,
		(char *)if_stats, sizeof(*if_stats), NULL);
	if (ret) {
		WL_ERR(("wl_android_get_connection_stats: if_counters not supported ret=%d\n",
			ret));

		/* In case if_stats IOVAR is not supported, get information from counters. */
#endif /* DISABLE_IF_COUNTERS */
		ret = wldev_iovar_getbuf(dev, "counters", NULL, 0,
			iovar_buf, WLC_IOCTL_MAXLEN, NULL);
		if (unlikely(ret)) {
			WL_ERR(("counters error (%d) - size = %zu\n", ret, sizeof(wl_cnt_wlc_t)));
			goto error;
		}
		ret = wl_cntbuf_to_xtlv_format(NULL, iovar_buf, WL_CNTBUF_MAX_SIZE, 0);
		if (ret != BCME_OK) {
			WL_ERR(("wl_android_get_connection_stats:"
			" wl_cntbuf_to_xtlv_format ERR %d\n",
			ret));
			goto error;
		}

		if (!(wlc_cnt = GET_WLCCNT_FROM_CNTBUF(iovar_buf))) {
			WL_ERR(("wl_android_get_connection_stats: wlc_cnt NULL!\n"));
			goto error;
		}

		output->txframe   = dtoh32(wlc_cnt->txframe);
		output->txbyte    = dtoh32(wlc_cnt->txbyte);
		output->txerror   = dtoh32(wlc_cnt->txerror);
		output->rxframe   = dtoh32(wlc_cnt->rxframe);
		output->rxbyte    = dtoh32(wlc_cnt->rxbyte);
		output->txfail    = dtoh32(wlc_cnt->txfail);
		output->txretry   = dtoh32(wlc_cnt->txretry);
		output->txretrie  = dtoh32(wlc_cnt->txretrie);
		output->txrts     = dtoh32(wlc_cnt->txrts);
		output->txnocts   = dtoh32(wlc_cnt->txnocts);
		output->txexptime = dtoh32(wlc_cnt->txexptime);
#ifndef DISABLE_IF_COUNTERS
	} else {
		/* Populate from if_stats. */
		if (dtoh16(if_stats->version) > WL_IF_STATS_T_VERSION) {
			WL_ERR(("wl_android_get_connection_stats: incorrect version of"
				" wl_if_stats_t,"
				" expected=%u got=%u\n",
				WL_IF_STATS_T_VERSION, if_stats->version));
			goto error;
		}

		output->txframe   = (uint32)dtoh64(if_stats->txframe);
		output->txbyte    = (uint32)dtoh64(if_stats->txbyte);
		output->txerror   = (uint32)dtoh64(if_stats->txerror);
		output->rxframe   = (uint32)dtoh64(if_stats->rxframe);
		output->rxbyte    = (uint32)dtoh64(if_stats->rxbyte);
		output->txfail    = (uint32)dtoh64(if_stats->txfail);
		output->txretry   = (uint32)dtoh64(if_stats->txretry);
		output->txretrie  = (uint32)dtoh64(if_stats->txretrie);
		if (dtoh16(if_stats->length) > OFFSETOF(wl_if_stats_t, txexptime)) {
			output->txexptime = (uint32)dtoh64(if_stats->txexptime);
			output->txrts     = (uint32)dtoh64(if_stats->txrts);
			output->txnocts   = (uint32)dtoh64(if_stats->txnocts);
		} else {
			output->txexptime = 0;
			output->txrts     = 0;
			output->txnocts   = 0;
		}
	}
#endif /* DISABLE_IF_COUNTERS */

	/* link_speed is in kbps */
	ret = wldev_get_link_speed(dev, &link_speed);
	if (ret || link_speed < 0) {
		WL_ERR(("wl_android_get_connection_stats: wldev_get_link_speed()"
			" failed, ret=%d, speed=%d\n",
			ret, link_speed));
		goto error;
	}

	output->txrate    = link_speed;

	/* Channel idle ratio. */
	if (wl_chanim_stats(dev, &(output->chan_idle)) < 0) {
		output->chan_idle = 0;
	};

	bytes_written = sizeof(struct connection_stats);

error:
#ifndef DISABLE_IF_COUNTERS
	if (if_stats) {
		MFREE(cfg->osh, if_stats, sizeof(*if_stats));
	}
#endif /* DISABLE_IF_COUNTERS */

	return bytes_written;
}
#endif /* CONNECTION_STATISTICS */

#ifdef WL_NATOE
static int
wl_android_process_natoe_cmd(struct net_device *dev, char *command, int total_len)
{
	int ret = BCME_ERROR;
	char *pcmd = command;
	char *str = NULL;
	wl_natoe_cmd_info_t cmd_info;
	const wl_natoe_sub_cmd_t *natoe_cmd = &natoe_cmd_list[0];

	/* skip to cmd name after "natoe" */
	str = bcmstrtok(&pcmd, " ", NULL);

	/* If natoe subcmd name is not provided, return error */
	if (*pcmd == '\0') {
		WL_ERR(("natoe subcmd not provided wl_android_process_natoe_cmd\n"));
		ret = -EINVAL;
		return ret;
	}

	/* get the natoe command name to str */
	str = bcmstrtok(&pcmd, " ", NULL);

	while (natoe_cmd->name != NULL) {
		if (strcmp(natoe_cmd->name, str) == 0)  {
			/* dispacth cmd to appropriate handler */
			if (natoe_cmd->handler) {
				cmd_info.command = command;
				cmd_info.tot_len = total_len;
				ret = natoe_cmd->handler(dev, natoe_cmd, pcmd, &cmd_info);
			}
			return ret;
		}
		natoe_cmd++;
	}
	return ret;
}

static int
wlu_natoe_set_vars_cbfn(void *ctx, uint8 *data, uint16 type, uint16 len)
{
	int res = BCME_OK;
	wl_natoe_cmd_info_t *cmd_info = (wl_natoe_cmd_info_t *)ctx;
	uint8 *command = cmd_info->command;
	uint16 total_len = cmd_info->tot_len;
	uint16 bytes_written = 0;

	UNUSED_PARAMETER(len);

	switch (type) {

	case WL_NATOE_XTLV_ENABLE:
	{
		bytes_written = snprintf(command, total_len, "natoe: %s\n",
				*data?"enabled":"disabled");
		cmd_info->bytes_written = bytes_written;
		break;
	}

	case WL_NATOE_XTLV_CONFIG_IPS:
	{
		wl_natoe_config_ips_t *config_ips;
		uint8 buf[16];

		config_ips = (wl_natoe_config_ips_t *)data;
		bcm_ip_ntoa((struct ipv4_addr *)&config_ips->sta_ip, buf);
		bytes_written = snprintf(command, total_len, "sta ip: %s\n", buf);
		bcm_ip_ntoa((struct ipv4_addr *)&config_ips->sta_netmask, buf);
		bytes_written += snprintf(command + bytes_written, total_len,
				"sta netmask: %s\n", buf);
		bcm_ip_ntoa((struct ipv4_addr *)&config_ips->sta_router_ip, buf);
		bytes_written += snprintf(command + bytes_written, total_len,
				"sta router ip: %s\n", buf);
		bcm_ip_ntoa((struct ipv4_addr *)&config_ips->sta_dnsip, buf);
		bytes_written += snprintf(command + bytes_written, total_len,
				"sta dns ip: %s\n", buf);
		bcm_ip_ntoa((struct ipv4_addr *)&config_ips->ap_ip, buf);
		bytes_written += snprintf(command + bytes_written, total_len,
				"ap ip: %s\n", buf);
		bcm_ip_ntoa((struct ipv4_addr *)&config_ips->ap_netmask, buf);
		bytes_written += snprintf(command + bytes_written, total_len,
				"ap netmask: %s\n", buf);
		cmd_info->bytes_written = bytes_written;
		break;
	}

	case WL_NATOE_XTLV_CONFIG_PORTS:
	{
		wl_natoe_ports_config_t *ports_config;

		ports_config = (wl_natoe_ports_config_t *)data;
		bytes_written = snprintf(command, total_len, "starting port num: %d\n",
				dtoh16(ports_config->start_port_num));
		bytes_written += snprintf(command + bytes_written, total_len,
				"number of ports: %d\n", dtoh16(ports_config->no_of_ports));
		cmd_info->bytes_written = bytes_written;
		break;
	}

	case WL_NATOE_XTLV_DBG_STATS:
	{
		char *stats_dump = (char *)data;

		bytes_written = snprintf(command, total_len, "%s\n", stats_dump);
		cmd_info->bytes_written = bytes_written;
		break;
	}

	case WL_NATOE_XTLV_TBL_CNT:
	{
		bytes_written = snprintf(command, total_len, "natoe max tbl entries: %d\n",
				dtoh32(*(uint32 *)data));
		cmd_info->bytes_written = bytes_written;
		break;
	}

	default:
		/* ignore */
		break;
	}

	return res;
}

/*
 *   --- common for all natoe get commands ----
 */
static int
wl_natoe_get_ioctl(struct net_device *dev, wl_natoe_ioc_t *natoe_ioc,
		uint16 iocsz, uint8 *buf, uint16 buflen, wl_natoe_cmd_info_t *cmd_info)
{
	/* for gets we only need to pass ioc header */
	wl_natoe_ioc_t *iocresp = (wl_natoe_ioc_t *)buf;
	int res;

	/*  send getbuf natoe iovar */
	res = wldev_iovar_getbuf(dev, "natoe", natoe_ioc, iocsz, buf,
			buflen, NULL);

	/*  check the response buff  */
	if ((res == BCME_OK)) {
		/* scans ioctl tlvbuf f& invokes the cbfn for processing  */
		res = bcm_unpack_xtlv_buf(cmd_info, iocresp->data, iocresp->len,
				BCM_XTLV_OPTION_ALIGN32, wlu_natoe_set_vars_cbfn);

		if (res == BCME_OK) {
			res = cmd_info->bytes_written;
		}
	}
	else
	{
		DHD_ERROR(("wl_natoe_get_ioctl: get command failed code %d\n", res));
		res = BCME_ERROR;
	}

	return res;
}

static int
wl_android_natoe_subcmd_enable(struct net_device *dev, const wl_natoe_sub_cmd_t *cmd,
		char *command, wl_natoe_cmd_info_t *cmd_info)
{
	int ret = BCME_OK;
	wl_natoe_ioc_t *natoe_ioc;
	char *pcmd = command;
	uint16 iocsz = sizeof(*natoe_ioc) + WL_NATOE_IOC_BUFSZ;
	uint16 buflen = WL_NATOE_IOC_BUFSZ;
	bcm_xtlv_t *pxtlv = NULL;
	char *ioctl_buf = NULL;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	ioctl_buf = (char *)MALLOCZ(cfg->osh, WLC_IOCTL_MEDLEN);
	if (!ioctl_buf) {
		WL_ERR(("ioctl memory alloc failed\n"));
		return -ENOMEM;
	}

	/* alloc mem for ioctl headr + tlv data */
	natoe_ioc = (wl_natoe_ioc_t *)MALLOCZ(cfg->osh, iocsz);
	if (!natoe_ioc) {
		WL_ERR(("ioctl header memory alloc failed\n"));
		MFREE(cfg->osh, ioctl_buf, WLC_IOCTL_MEDLEN);
		return -ENOMEM;
	}

	/* make up natoe cmd ioctl header */
	natoe_ioc->version = htod16(WL_NATOE_IOCTL_VERSION);
	natoe_ioc->id = htod16(cmd->id);
	natoe_ioc->len = htod16(WL_NATOE_IOC_BUFSZ);
	pxtlv = (bcm_xtlv_t *)natoe_ioc->data;

	if(*pcmd == WL_IOCTL_ACTION_GET) { /* get */
		iocsz = sizeof(*natoe_ioc) + sizeof(*pxtlv);
		ret = wl_natoe_get_ioctl(dev, natoe_ioc, iocsz, ioctl_buf,
				WLC_IOCTL_MEDLEN, cmd_info);
		if (ret != BCME_OK) {
			WL_ERR(("Fail to get iovar wl_android_natoe_subcmd_enable\n"));
			ret = -EINVAL;
		}
	} else {	/* set */
		uint8 val = bcm_atoi(pcmd);

		/* buflen is max tlv data we can write, it will be decremented as we pack */
		/* save buflen at start */
		uint16 buflen_at_start = buflen;

		/* we'll adjust final ioc size at the end */
		ret = bcm_pack_xtlv_entry((uint8**)&pxtlv, &buflen, WL_NATOE_XTLV_ENABLE,
			sizeof(uint8), &val, BCM_XTLV_OPTION_ALIGN32);

		if (ret != BCME_OK) {
			ret = -EINVAL;
			goto exit;
		}

		/* adjust iocsz to the end of last data record */
		natoe_ioc->len = (buflen_at_start - buflen);
		iocsz = sizeof(*natoe_ioc) + natoe_ioc->len;

		ret = wldev_iovar_setbuf(dev, "natoe",
				natoe_ioc, iocsz, ioctl_buf, WLC_IOCTL_MEDLEN, NULL);
		if (ret != BCME_OK) {
			WL_ERR(("Fail to set iovar %d\n", ret));
			ret = -EINVAL;
		}
	}

exit:
	MFREE(cfg->osh, ioctl_buf, WLC_IOCTL_MEDLEN);
	MFREE(cfg->osh, natoe_ioc, iocsz);

	return ret;
}

static int
wl_android_natoe_subcmd_config_ips(struct net_device *dev,
		const wl_natoe_sub_cmd_t *cmd, char *command, wl_natoe_cmd_info_t *cmd_info)
{
	int ret = BCME_OK;
	wl_natoe_config_ips_t config_ips;
	wl_natoe_ioc_t *natoe_ioc;
	char *pcmd = command;
	char *str;
	uint16 iocsz = sizeof(*natoe_ioc) + WL_NATOE_IOC_BUFSZ;
	uint16 buflen = WL_NATOE_IOC_BUFSZ;
	bcm_xtlv_t *pxtlv = NULL;
	char *ioctl_buf = NULL;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	ioctl_buf = (char *)MALLOCZ(cfg->osh, WLC_IOCTL_MEDLEN);
	if (!ioctl_buf) {
		WL_ERR(("ioctl memory alloc failed\n"));
		return -ENOMEM;
	}

	/* alloc mem for ioctl headr + tlv data */
	natoe_ioc = (wl_natoe_ioc_t *)MALLOCZ(cfg->osh, iocsz);
	if (!natoe_ioc) {
		WL_ERR(("ioctl header memory alloc failed\n"));
		MFREE(cfg->osh, ioctl_buf, WLC_IOCTL_MEDLEN);
		return -ENOMEM;
	}

	/* make up natoe cmd ioctl header */
	natoe_ioc->version = htod16(WL_NATOE_IOCTL_VERSION);
	natoe_ioc->id = htod16(cmd->id);
	natoe_ioc->len = htod16(WL_NATOE_IOC_BUFSZ);
	pxtlv = (bcm_xtlv_t *)natoe_ioc->data;

	if(*pcmd == WL_IOCTL_ACTION_GET) { /* get */
		iocsz = sizeof(*natoe_ioc) + sizeof(*pxtlv);
		ret = wl_natoe_get_ioctl(dev, natoe_ioc, iocsz, ioctl_buf,
				WLC_IOCTL_MEDLEN, cmd_info);
		if (ret != BCME_OK) {
			WL_ERR(("Fail to get iovar wl_android_natoe_subcmd_config_ips\n"));
			ret = -EINVAL;
		}
	} else {	/* set */
		/* buflen is max tlv data we can write, it will be decremented as we pack */
		/* save buflen at start */
		uint16 buflen_at_start = buflen;

		bzero(&config_ips, sizeof(config_ips));

		str = bcmstrtok(&pcmd, " ", NULL);
		if (!str || !bcm_atoipv4(str, (struct ipv4_addr *)&config_ips.sta_ip)) {
			WL_ERR(("Invalid STA IP addr %s\n", str));
			ret = -EINVAL;
			goto exit;
		}

		str = bcmstrtok(&pcmd, " ", NULL);
		if (!str || !bcm_atoipv4(str, (struct ipv4_addr *)&config_ips.sta_netmask)) {
			WL_ERR(("Invalid STA netmask %s\n", str));
			ret = -EINVAL;
			goto exit;
		}

		str = bcmstrtok(&pcmd, " ", NULL);
		if (!str || !bcm_atoipv4(str, (struct ipv4_addr *)&config_ips.sta_router_ip)) {
			WL_ERR(("Invalid STA router IP addr %s\n", str));
			ret = -EINVAL;
			goto exit;
		}

		str = bcmstrtok(&pcmd, " ", NULL);
		if (!str || !bcm_atoipv4(str, (struct ipv4_addr *)&config_ips.sta_dnsip)) {
			WL_ERR(("Invalid STA DNS IP addr %s\n", str));
			ret = -EINVAL;
			goto exit;
		}

		str = bcmstrtok(&pcmd, " ", NULL);
		if (!str || !bcm_atoipv4(str, (struct ipv4_addr *)&config_ips.ap_ip)) {
			WL_ERR(("Invalid AP IP addr %s\n", str));
			ret = -EINVAL;
			goto exit;
		}

		str = bcmstrtok(&pcmd, " ", NULL);
		if (!str || !bcm_atoipv4(str, (struct ipv4_addr *)&config_ips.ap_netmask)) {
			WL_ERR(("Invalid AP netmask %s\n", str));
			ret = -EINVAL;
			goto exit;
		}

		ret = bcm_pack_xtlv_entry((uint8**)&pxtlv,
				&buflen, WL_NATOE_XTLV_CONFIG_IPS, sizeof(config_ips),
				&config_ips, BCM_XTLV_OPTION_ALIGN32);

		if (ret != BCME_OK) {
			ret = -EINVAL;
			goto exit;
		}

		/* adjust iocsz to the end of last data record */
		natoe_ioc->len = (buflen_at_start - buflen);
		iocsz = sizeof(*natoe_ioc) + natoe_ioc->len;

		ret = wldev_iovar_setbuf(dev, "natoe",
				natoe_ioc, iocsz, ioctl_buf, WLC_IOCTL_MEDLEN, NULL);
		if (ret != BCME_OK) {
			WL_ERR(("Fail to set iovar %d\n", ret));
			ret = -EINVAL;
		}
	}

exit:
	MFREE(cfg->osh, ioctl_buf, WLC_IOCTL_MEDLEN);
	MFREE(cfg->osh, natoe_ioc, sizeof(*natoe_ioc) + WL_NATOE_IOC_BUFSZ);

	return ret;
}

static int
wl_android_natoe_subcmd_config_ports(struct net_device *dev,
		const wl_natoe_sub_cmd_t *cmd, char *command, wl_natoe_cmd_info_t *cmd_info)
{
	int ret = BCME_OK;
	wl_natoe_ports_config_t ports_config;
	wl_natoe_ioc_t *natoe_ioc;
	char *pcmd = command;
	char *str;
	uint16 iocsz = sizeof(*natoe_ioc) + WL_NATOE_IOC_BUFSZ;
	uint16 buflen = WL_NATOE_IOC_BUFSZ;
	bcm_xtlv_t *pxtlv = NULL;
	char *ioctl_buf = NULL;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	ioctl_buf = (char *)MALLOCZ(cfg->osh, WLC_IOCTL_MEDLEN);
	if (!ioctl_buf) {
		WL_ERR(("ioctl memory alloc failed\n"));
		return -ENOMEM;
	}

	/* alloc mem for ioctl headr + tlv data */
	natoe_ioc = (wl_natoe_ioc_t *)MALLOCZ(cfg->osh, iocsz);
	if (!natoe_ioc) {
		WL_ERR(("ioctl header memory alloc failed\n"));
		MFREE(cfg->osh, ioctl_buf, WLC_IOCTL_MEDLEN);
		return -ENOMEM;
	}

	/* make up natoe cmd ioctl header */
	natoe_ioc->version = htod16(WL_NATOE_IOCTL_VERSION);
	natoe_ioc->id = htod16(cmd->id);
	natoe_ioc->len = htod16(WL_NATOE_IOC_BUFSZ);
	pxtlv = (bcm_xtlv_t *)natoe_ioc->data;

	if(*pcmd == WL_IOCTL_ACTION_GET) { /* get */
		iocsz = sizeof(*natoe_ioc) + sizeof(*pxtlv);
		ret = wl_natoe_get_ioctl(dev, natoe_ioc, iocsz, ioctl_buf,
				WLC_IOCTL_MEDLEN, cmd_info);
		if (ret != BCME_OK) {
			WL_ERR(("Fail to get iovar wl_android_natoe_subcmd_config_ports\n"));
			ret = -EINVAL;
		}
	} else {	/* set */
		/* buflen is max tlv data we can write, it will be decremented as we pack */
		/* save buflen at start */
		uint16 buflen_at_start = buflen;

		bzero(&ports_config, sizeof(ports_config));

		str = bcmstrtok(&pcmd, " ", NULL);
		if (!str) {
			WL_ERR(("Invalid port string %s\n", str));
			ret = -EINVAL;
			goto exit;
		}
		ports_config.start_port_num = htod16(bcm_atoi(str));

		str = bcmstrtok(&pcmd, " ", NULL);
		if (!str) {
			WL_ERR(("Invalid port string %s\n", str));
			ret = -EINVAL;
			goto exit;
		}
		ports_config.no_of_ports = htod16(bcm_atoi(str));

		if ((uint32)(ports_config.start_port_num + ports_config.no_of_ports) >
				NATOE_MAX_PORT_NUM) {
			WL_ERR(("Invalid port configuration\n"));
			ret = -EINVAL;
			goto exit;
		}
		ret = bcm_pack_xtlv_entry((uint8**)&pxtlv,
				&buflen, WL_NATOE_XTLV_CONFIG_PORTS, sizeof(ports_config),
				&ports_config, BCM_XTLV_OPTION_ALIGN32);

		if (ret != BCME_OK) {
			ret = -EINVAL;
			goto exit;
		}

		/* adjust iocsz to the end of last data record */
		natoe_ioc->len = (buflen_at_start - buflen);
		iocsz = sizeof(*natoe_ioc) + natoe_ioc->len;

		ret = wldev_iovar_setbuf(dev, "natoe",
				natoe_ioc, iocsz, ioctl_buf, WLC_IOCTL_MEDLEN, NULL);
		if (ret != BCME_OK) {
			WL_ERR(("Fail to set iovar %d\n", ret));
			ret = -EINVAL;
		}
	}

exit:
	MFREE(cfg->osh, ioctl_buf, WLC_IOCTL_MEDLEN);
	MFREE(cfg->osh, natoe_ioc, sizeof(*natoe_ioc) + WL_NATOE_IOC_BUFSZ);

	return ret;
}

static int
wl_android_natoe_subcmd_dbg_stats(struct net_device *dev, const wl_natoe_sub_cmd_t *cmd,
		char *command, wl_natoe_cmd_info_t *cmd_info)
{
	int ret = BCME_OK;
	wl_natoe_ioc_t *natoe_ioc;
	char *pcmd = command;
	gfp_t kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;
	uint16 iocsz = sizeof(*natoe_ioc) + WL_NATOE_DBG_STATS_BUFSZ;
	uint16 buflen = WL_NATOE_DBG_STATS_BUFSZ;
	bcm_xtlv_t *pxtlv = NULL;
	char *ioctl_buf = NULL;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	ioctl_buf = (char *)MALLOCZ(cfg->osh, WLC_IOCTL_MAXLEN);
	if (!ioctl_buf) {
		WL_ERR(("ioctl memory alloc failed\n"));
		return -ENOMEM;
	}

	/* alloc mem for ioctl headr + tlv data */
	natoe_ioc = (wl_natoe_ioc_t *)MALLOCZ(cfg->osh, iocsz);
	if (!natoe_ioc) {
		WL_ERR(("ioctl header memory alloc failed\n"));
		MFREE(cfg->osh, ioctl_buf, WLC_IOCTL_MAXLEN);
		return -ENOMEM;
	}

	/* make up natoe cmd ioctl header */
	natoe_ioc->version = htod16(WL_NATOE_IOCTL_VERSION);
	natoe_ioc->id = htod16(cmd->id);
	natoe_ioc->len = htod16(WL_NATOE_DBG_STATS_BUFSZ);
	pxtlv = (bcm_xtlv_t *)natoe_ioc->data;

	if(*pcmd == WL_IOCTL_ACTION_GET) { /* get */
		iocsz = sizeof(*natoe_ioc) + sizeof(*pxtlv);
		ret = wl_natoe_get_ioctl(dev, natoe_ioc, iocsz, ioctl_buf,
				WLC_IOCTL_MAXLEN, cmd_info);
		if (ret != BCME_OK) {
			WL_ERR(("Fail to get iovar wl_android_natoe_subcmd_dbg_stats\n"));
			ret = -EINVAL;
		}
	} else {	/* set */
		uint8 val = bcm_atoi(pcmd);

		/* buflen is max tlv data we can write, it will be decremented as we pack */
		/* save buflen at start */
		uint16 buflen_at_start = buflen;

		/* we'll adjust final ioc size at the end */
		ret = bcm_pack_xtlv_entry((uint8**)&pxtlv, &buflen, WL_NATOE_XTLV_ENABLE,
			sizeof(uint8), &val, BCM_XTLV_OPTION_ALIGN32);

		if (ret != BCME_OK) {
			ret = -EINVAL;
			goto exit;
		}

		/* adjust iocsz to the end of last data record */
		natoe_ioc->len = (buflen_at_start - buflen);
		iocsz = sizeof(*natoe_ioc) + natoe_ioc->len;

		ret = wldev_iovar_setbuf(dev, "natoe",
				natoe_ioc, iocsz, ioctl_buf, WLC_IOCTL_MAXLEN, NULL);
		if (ret != BCME_OK) {
			WL_ERR(("Fail to set iovar %d\n", ret));
			ret = -EINVAL;
		}
	}

exit:
	MFREE(cfg->osh, ioctl_buf, WLC_IOCTL_MAXLEN);
	MFREE(cfg->osh, natoe_ioc, sizeof(*natoe_ioc) + WL_NATOE_DBG_STATS_BUFSZ);

	return ret;
}

static int
wl_android_natoe_subcmd_tbl_cnt(struct net_device *dev, const wl_natoe_sub_cmd_t *cmd,
		char *command, wl_natoe_cmd_info_t *cmd_info)
{
	int ret = BCME_OK;
	wl_natoe_ioc_t *natoe_ioc;
	char *pcmd = command;
	gfp_t kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;
	uint16 iocsz = sizeof(*natoe_ioc) + WL_NATOE_IOC_BUFSZ;
	uint16 buflen = WL_NATOE_IOC_BUFSZ;
	bcm_xtlv_t *pxtlv = NULL;
	char *ioctl_buf = NULL;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	ioctl_buf = (char *)MALLOCZ(cfg->osh, WLC_IOCTL_MEDLEN);
	if (!ioctl_buf) {
		WL_ERR(("ioctl memory alloc failed\n"));
		return -ENOMEM;
	}

	/* alloc mem for ioctl headr + tlv data */
	natoe_ioc = (wl_natoe_ioc_t *)MALLOCZ(cfg->osh, iocsz);
	if (!natoe_ioc) {
		WL_ERR(("ioctl header memory alloc failed\n"));
		MFREE(cfg->osh, ioctl_buf, WLC_IOCTL_MEDLEN);
		return -ENOMEM;
	}

	/* make up natoe cmd ioctl header */
	natoe_ioc->version = htod16(WL_NATOE_IOCTL_VERSION);
	natoe_ioc->id = htod16(cmd->id);
	natoe_ioc->len = htod16(WL_NATOE_IOC_BUFSZ);
	pxtlv = (bcm_xtlv_t *)natoe_ioc->data;

	if(*pcmd == WL_IOCTL_ACTION_GET) { /* get */
		iocsz = sizeof(*natoe_ioc) + sizeof(*pxtlv);
		ret = wl_natoe_get_ioctl(dev, natoe_ioc, iocsz, ioctl_buf,
				WLC_IOCTL_MEDLEN, cmd_info);
		if (ret != BCME_OK) {
			WL_ERR(("Fail to get iovar wl_android_natoe_subcmd_tbl_cnt\n"));
			ret = -EINVAL;
		}
	} else {	/* set */
		uint32 val = bcm_atoi(pcmd);

		/* buflen is max tlv data we can write, it will be decremented as we pack */
		/* save buflen at start */
		uint16 buflen_at_start = buflen;

		/* we'll adjust final ioc size at the end */
		ret = bcm_pack_xtlv_entry((uint8**)&pxtlv, &buflen, WL_NATOE_XTLV_TBL_CNT,
			sizeof(uint32), &val, BCM_XTLV_OPTION_ALIGN32);

		if (ret != BCME_OK) {
			ret = -EINVAL;
			goto exit;
		}

		/* adjust iocsz to the end of last data record */
		natoe_ioc->len = (buflen_at_start - buflen);
		iocsz = sizeof(*natoe_ioc) + natoe_ioc->len;

		ret = wldev_iovar_setbuf(dev, "natoe",
				natoe_ioc, iocsz, ioctl_buf, WLC_IOCTL_MEDLEN, NULL);
		if (ret != BCME_OK) {
			WL_ERR(("Fail to set iovar %d\n", ret));
			ret = -EINVAL;
		}
	}

exit:
	MFREE(cfg->osh, ioctl_buf, WLC_IOCTL_MEDLEN);
	MFREE(cfg->osh, natoe_ioc, sizeof(*natoe_ioc) + WL_NATOE_IOC_BUFSZ);

	return ret;
}

#endif /* WL_NATOE */

#ifdef WL_MBO
static int
wl_android_process_mbo_cmd(struct net_device *dev, char *command, int total_len)
{
	int ret = BCME_ERROR;
	char *pcmd = command;
	char *str = NULL;
	wl_drv_cmd_info_t cmd_info;
	const wl_drv_sub_cmd_t *mbo_cmd = &mbo_cmd_list[0];

	/* skip to cmd name after "mbo" */
	str = bcmstrtok(&pcmd, " ", NULL);

	/* If mbo subcmd name is not provided, return error */
	if (*pcmd == '\0') {
		WL_ERR(("mbo subcmd not provided %s\n", __FUNCTION__));
		ret = -EINVAL;
		return ret;
	}

	/* get the mbo command name to str */
	str = bcmstrtok(&pcmd, " ", NULL);

	while (mbo_cmd->name != NULL) {
		if (strnicmp(mbo_cmd->name, str, strlen(mbo_cmd->name)) == 0) {
			/* dispatch cmd to appropriate handler */
			if (mbo_cmd->handler) {
				cmd_info.command = command;
				cmd_info.tot_len = total_len;
				ret = mbo_cmd->handler(dev, mbo_cmd, pcmd, &cmd_info);
			}
			return ret;
		}
		mbo_cmd++;
	}
	return ret;
}

static int
wl_android_send_wnm_notif(struct net_device *dev, bcm_iov_buf_t *iov_buf,
	uint16 iov_buf_len, uint8 *iov_resp, uint16 iov_resp_len, uint8 sub_elem_type)
{
	int ret = BCME_OK;
	uint8 *pxtlv = NULL;
	uint16 iovlen = 0;
	uint16 buflen = 0, buflen_start = 0;

	memset_s(iov_buf, iov_buf_len, 0, iov_buf_len);
	iov_buf->version = WL_MBO_IOV_VERSION;
	iov_buf->id = WL_MBO_CMD_SEND_NOTIF;
	buflen = buflen_start = iov_buf_len - sizeof(bcm_iov_buf_t);
	pxtlv = (uint8 *)&iov_buf->data[0];
	ret = bcm_pack_xtlv_entry(&pxtlv, &buflen, WL_MBO_XTLV_SUB_ELEM_TYPE,
		sizeof(sub_elem_type), &sub_elem_type, BCM_XTLV_OPTION_ALIGN32);
	if (ret != BCME_OK) {
		return ret;
	}
	iov_buf->len = buflen_start - buflen;
	iovlen = sizeof(bcm_iov_buf_t) + iov_buf->len;
	ret = wldev_iovar_setbuf(dev, "mbo",
			iov_buf, iovlen, iov_resp, WLC_IOCTL_MAXLEN, NULL);
	if (ret != BCME_OK) {
		WL_ERR(("Fail to sent wnm notif %d\n", ret));
	}
	return ret;
}

static int
wl_android_mbo_resp_parse_cbfn(void *ctx, const uint8 *data, uint16 type, uint16 len)
{
	wl_drv_cmd_info_t *cmd_info = (wl_drv_cmd_info_t *)ctx;
	uint8 *command = cmd_info->command;
	uint16 total_len = cmd_info->tot_len;
	uint16 bytes_written = 0;

	UNUSED_PARAMETER(len);
	/* TODO: validate data value */
	if (data == NULL) {
		WL_ERR(("%s: Bad argument !!\n", __FUNCTION__));
		return -EINVAL;
	}
	switch (type) {
		case WL_MBO_XTLV_CELL_DATA_CAP:
		{
			bytes_written = snprintf(command, total_len, "cell_data_cap: %u\n", *data);
			cmd_info->bytes_written = bytes_written;
		}
		break;
		default:
			WL_ERR(("%s: Unknown tlv %u\n", __FUNCTION__, type));
	}
	return BCME_OK;
}

static int
wl_android_mbo_subcmd_cell_data_cap(struct net_device *dev, const wl_drv_sub_cmd_t *cmd,
		char *command, wl_drv_cmd_info_t *cmd_info)
{
	int ret = BCME_OK;
	uint8 *pxtlv = NULL;
	uint16 buflen = 0, buflen_start = 0;
	uint16 iovlen = 0;
	char *pcmd = command;
	bcm_iov_buf_t *iov_buf = NULL;
	bcm_iov_buf_t *p_resp = NULL;
	uint8 *iov_resp = NULL;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	uint16 version;

	/* first get the configured value */
	iov_buf = (bcm_iov_buf_t *)MALLOCZ(cfg->osh, WLC_IOCTL_MEDLEN);
	if (iov_buf == NULL) {
		ret = -ENOMEM;
		WL_ERR(("iov buf memory alloc exited\n"));
		goto exit;
	}
	iov_resp = (uint8 *)MALLOCZ(cfg->osh, WLC_IOCTL_MAXLEN);
	if (iov_resp == NULL) {
		ret = -ENOMEM;
		WL_ERR(("iov resp memory alloc exited\n"));
		goto exit;
	}

	/* fill header */
	iov_buf->version = WL_MBO_IOV_VERSION;
	iov_buf->id = WL_MBO_CMD_CELLULAR_DATA_CAP;

	ret = wldev_iovar_getbuf(dev, "mbo", iov_buf, WLC_IOCTL_MEDLEN, iov_resp,
		WLC_IOCTL_MAXLEN,
		NULL);
	if (ret != BCME_OK) {
		goto exit;
	}
	p_resp = (bcm_iov_buf_t *)iov_resp;

	/* get */
	if (*pcmd == WL_IOCTL_ACTION_GET) {
		/* Check for version */
		version = dtoh16(*(uint16 *)iov_resp);
		if (version != WL_MBO_IOV_VERSION) {
			ret = -EINVAL;
		}
		if (p_resp->id == WL_MBO_CMD_CELLULAR_DATA_CAP) {
			ret = bcm_unpack_xtlv_buf((void *)cmd_info, (uint8 *)p_resp->data,
				p_resp->len, BCM_XTLV_OPTION_ALIGN32,
				wl_android_mbo_resp_parse_cbfn);
			if (ret == BCME_OK) {
				ret = cmd_info->bytes_written;
			}
		} else {
			ret = -EINVAL;
			WL_ERR(("Mismatch: resp id %d req id %d\n", p_resp->id, cmd->id));
			goto exit;
		}
	} else {
		uint8 cell_cap = bcm_atoi(pcmd);
		const uint8* old_cell_cap = NULL;
		uint16 len = 0;

		old_cell_cap = bcm_get_data_from_xtlv_buf((uint8 *)p_resp->data, p_resp->len,
			WL_MBO_XTLV_CELL_DATA_CAP, &len, BCM_XTLV_OPTION_ALIGN32);
		if (old_cell_cap && *old_cell_cap == cell_cap) {
			WL_ERR(("No change is cellular data capability\n"));
			/* No change in value */
			goto exit;
		}

		buflen = buflen_start = WLC_IOCTL_MEDLEN - sizeof(bcm_iov_buf_t);

		if (cell_cap < MBO_CELL_DATA_CONN_AVAILABLE ||
			cell_cap > MBO_CELL_DATA_CONN_NOT_CAPABLE) {
			WL_ERR(("wrong value %u\n", cell_cap));
			ret = -EINVAL;
			goto exit;
		}
		pxtlv = (uint8 *)&iov_buf->data[0];
		ret = bcm_pack_xtlv_entry(&pxtlv, &buflen, WL_MBO_XTLV_CELL_DATA_CAP,
			sizeof(cell_cap), &cell_cap, BCM_XTLV_OPTION_ALIGN32);
		if (ret != BCME_OK) {
			goto exit;
		}
		iov_buf->len = buflen_start - buflen;
		iovlen = sizeof(bcm_iov_buf_t) + iov_buf->len;
		ret = wldev_iovar_setbuf(dev, "mbo",
				iov_buf, iovlen, iov_resp, WLC_IOCTL_MAXLEN, NULL);
		if (ret != BCME_OK) {
			WL_ERR(("Fail to set iovar %d\n", ret));
			ret = -EINVAL;
			goto exit;
		}
		/* Skip for CUSTOMER_HW4 - WNM notification
		 * for cellular data capability is handled by host
		 */
		/* send a WNM notification request to associated AP */
		if (wl_get_drv_status(cfg, CONNECTED, dev)) {
			WL_INFORM(("Sending WNM Notif\n"));
			ret = wl_android_send_wnm_notif(dev, iov_buf, WLC_IOCTL_MEDLEN,
				iov_resp, WLC_IOCTL_MAXLEN, MBO_ATTR_CELL_DATA_CAP);
			if (ret != BCME_OK) {
				WL_ERR(("Fail to send WNM notification %d\n", ret));
				ret = -EINVAL;
			}
		}
	}
exit:
	if (iov_buf) {
		MFREE(cfg->osh, iov_buf, WLC_IOCTL_MEDLEN);
	}
	if (iov_resp) {
		MFREE(cfg->osh, iov_resp, WLC_IOCTL_MAXLEN);
	}
	return ret;
}

static int
wl_android_mbo_non_pref_chan_parse_cbfn(void *ctx, const uint8 *data, uint16 type, uint16 len)
{
	wl_drv_cmd_info_t *cmd_info = (wl_drv_cmd_info_t *)ctx;
	uint8 *command = cmd_info->command + cmd_info->bytes_written;
	uint16 total_len = cmd_info->tot_len;
	uint16 bytes_written = 0;

	WL_DBG(("Total bytes written at begining %u\n", cmd_info->bytes_written));
	UNUSED_PARAMETER(len);
	if (data == NULL) {
		WL_ERR(("%s: Bad argument !!\n", __FUNCTION__));
		return -EINVAL;
	}
	switch (type) {
		case WL_MBO_XTLV_OPCLASS:
		{
			bytes_written = snprintf(command, total_len, "%u:", *data);
			WL_ERR(("wr %u %u\n", bytes_written, *data));
			command += bytes_written;
			cmd_info->bytes_written += bytes_written;
		}
		break;
		case WL_MBO_XTLV_CHAN:
		{
			bytes_written = snprintf(command, total_len, "%u:", *data);
			WL_ERR(("wr %u\n", bytes_written));
			command += bytes_written;
			cmd_info->bytes_written += bytes_written;
		}
		break;
		case WL_MBO_XTLV_PREFERENCE:
		{
			bytes_written = snprintf(command, total_len, "%u:", *data);
			WL_ERR(("wr %u\n", bytes_written));
			command += bytes_written;
			cmd_info->bytes_written += bytes_written;
		}
		break;
		case WL_MBO_XTLV_REASON_CODE:
		{
			bytes_written = snprintf(command, total_len, "%u ", *data);
			WL_ERR(("wr %u\n", bytes_written));
			command += bytes_written;
			cmd_info->bytes_written += bytes_written;
		}
		break;
		default:
			WL_ERR(("%s: Unknown tlv %u\n", __FUNCTION__, type));
	}
	WL_DBG(("Total bytes written %u\n", cmd_info->bytes_written));
	return BCME_OK;
}

static int
wl_android_mbo_subcmd_non_pref_chan(struct net_device *dev,
		const wl_drv_sub_cmd_t *cmd, char *command,
		wl_drv_cmd_info_t *cmd_info)
{
	int ret = BCME_OK;
	uint8 *pxtlv = NULL;
	uint16 buflen = 0, buflen_start = 0;
	uint16 iovlen = 0;
	char *pcmd = command;
	bcm_iov_buf_t *iov_buf = NULL;
	bcm_iov_buf_t *p_resp = NULL;
	uint8 *iov_resp = NULL;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	uint16 version;

	WL_ERR(("%s:%d\n", __FUNCTION__, __LINE__));
	iov_buf = (bcm_iov_buf_t *)MALLOCZ(cfg->osh, WLC_IOCTL_MEDLEN);
	if (iov_buf == NULL) {
		ret = -ENOMEM;
		WL_ERR(("iov buf memory alloc exited\n"));
		goto exit;
	}
	iov_resp = (uint8 *)MALLOCZ(cfg->osh, WLC_IOCTL_MAXLEN);
	if (iov_resp == NULL) {
		ret = -ENOMEM;
		WL_ERR(("iov resp memory alloc exited\n"));
		goto exit;
	}
	/* get */
	if (*pcmd == WL_IOCTL_ACTION_GET) {
		/* fill header */
		iov_buf->version = WL_MBO_IOV_VERSION;
		iov_buf->id = WL_MBO_CMD_LIST_CHAN_PREF;

		ret = wldev_iovar_getbuf(dev, "mbo", iov_buf, WLC_IOCTL_MEDLEN, iov_resp,
				WLC_IOCTL_MAXLEN, NULL);
		if (ret != BCME_OK) {
			goto exit;
		}
		p_resp = (bcm_iov_buf_t *)iov_resp;
		/* Check for version */
		version = dtoh16(*(uint16 *)iov_resp);
		if (version != WL_MBO_IOV_VERSION) {
			WL_ERR(("Version mismatch. returned ver %u expected %u\n",
				version, WL_MBO_IOV_VERSION));
			ret = -EINVAL;
		}
		if (p_resp->id == WL_MBO_CMD_LIST_CHAN_PREF) {
			ret = bcm_unpack_xtlv_buf((void *)cmd_info, (uint8 *)p_resp->data,
				p_resp->len, BCM_XTLV_OPTION_ALIGN32,
				wl_android_mbo_non_pref_chan_parse_cbfn);
			if (ret == BCME_OK) {
				ret = cmd_info->bytes_written;
			}
		} else {
			ret = -EINVAL;
			WL_ERR(("Mismatch: resp id %d req id %d\n", p_resp->id, cmd->id));
			goto exit;
		}
	} else {
		char *str = pcmd;
		uint opcl = 0, ch = 0, pref = 0, rc = 0;

		str = bcmstrtok(&pcmd, " ", NULL);
		if (!(strnicmp(str, "set", 3)) || (!strnicmp(str, "clear", 5))) {
			/* delete all configurations */
			iov_buf->version = WL_MBO_IOV_VERSION;
			iov_buf->id = WL_MBO_CMD_DEL_CHAN_PREF;
			iov_buf->len = 0;
			iovlen = sizeof(bcm_iov_buf_t) + iov_buf->len;
			ret = wldev_iovar_setbuf(dev, "mbo",
				iov_buf, iovlen, iov_resp, WLC_IOCTL_MAXLEN, NULL);
			if (ret != BCME_OK) {
				WL_ERR(("Fail to set iovar %d\n", ret));
				ret = -EINVAL;
				goto exit;
			}
		} else {
			WL_ERR(("Unknown command %s\n", str));
			goto exit;
		}
		/* parse non pref channel list */
		if (strnicmp(str, "set", 3) == 0) {
			uint8 cnt = 0;
			str = bcmstrtok(&pcmd, " ", NULL);
			while (str != NULL) {
				ret = sscanf(str, "%u:%u:%u:%u", &opcl, &ch, &pref, &rc);
				WL_ERR(("buflen %u op %u, ch %u, pref %u rc %u\n",
					buflen, opcl, ch, pref, rc));
				if (ret != 4) {
					WL_ERR(("Not all parameter presents\n"));
					ret = -EINVAL;
				}
				/* TODO: add a validation check here */
				memset_s(iov_buf, WLC_IOCTL_MEDLEN, 0, WLC_IOCTL_MEDLEN);
				buflen = buflen_start = WLC_IOCTL_MEDLEN;
				pxtlv = (uint8 *)&iov_buf->data[0];
				/* opclass */
				ret = bcm_pack_xtlv_entry(&pxtlv, &buflen, WL_MBO_XTLV_OPCLASS,
					sizeof(uint8), (uint8 *)&opcl, BCM_XTLV_OPTION_ALIGN32);
				if (ret != BCME_OK) {
					goto exit;
				}
				/* channel */
				ret = bcm_pack_xtlv_entry(&pxtlv, &buflen, WL_MBO_XTLV_CHAN,
					sizeof(uint8), (uint8 *)&ch, BCM_XTLV_OPTION_ALIGN32);
				if (ret != BCME_OK) {
					goto exit;
				}
				/* preference */
				ret = bcm_pack_xtlv_entry(&pxtlv, &buflen, WL_MBO_XTLV_PREFERENCE,
					sizeof(uint8), (uint8 *)&pref, BCM_XTLV_OPTION_ALIGN32);
				if (ret != BCME_OK) {
					goto exit;
				}
				/* reason */
				ret = bcm_pack_xtlv_entry(&pxtlv, &buflen, WL_MBO_XTLV_REASON_CODE,
					sizeof(uint8), (uint8 *)&rc, BCM_XTLV_OPTION_ALIGN32);
				if (ret != BCME_OK) {
					goto exit;
				}
				WL_ERR(("len %u\n", (buflen_start - buflen)));
				/* Now set the new non pref channels */
				iov_buf->version = WL_MBO_IOV_VERSION;
				iov_buf->id = WL_MBO_CMD_ADD_CHAN_PREF;
				iov_buf->len = buflen_start - buflen;
				iovlen = sizeof(bcm_iov_buf_t) + iov_buf->len;
				ret = wldev_iovar_setbuf(dev, "mbo",
					iov_buf, iovlen, iov_resp, WLC_IOCTL_MEDLEN, NULL);
				if (ret != BCME_OK) {
					WL_ERR(("Fail to set iovar %d\n", ret));
					ret = -EINVAL;
					goto exit;
				}
				cnt++;
				if (cnt >= MBO_MAX_CHAN_PREF_ENTRIES) {
					break;
				}
				WL_ERR(("%d cnt %u\n", __LINE__, cnt));
				str = bcmstrtok(&pcmd, " ", NULL);
			}
		}
		/* send a WNM notification request to associated AP */
		if (wl_get_drv_status(cfg, CONNECTED, dev)) {
			WL_INFORM(("Sending WNM Notif\n"));
			ret = wl_android_send_wnm_notif(dev, iov_buf, WLC_IOCTL_MEDLEN,
				iov_resp, WLC_IOCTL_MAXLEN, MBO_ATTR_NON_PREF_CHAN_REPORT);
			if (ret != BCME_OK) {
				WL_ERR(("Fail to send WNM notification %d\n", ret));
				ret = -EINVAL;
			}
		}
	}
exit:
	if (iov_buf) {
		MFREE(cfg->osh, iov_buf, WLC_IOCTL_MEDLEN);
	}
	if (iov_resp) {
		MFREE(cfg->osh, iov_resp, WLC_IOCTL_MAXLEN);
	}
	return ret;
}
#endif /* WL_MBO */

#ifdef CUSTOMER_HW4_PRIVATE_CMD
#ifdef SUPPORT_AMPDU_MPDU_CMD
/* CMD_AMPDU_MPDU */
static int
wl_android_set_ampdu_mpdu(struct net_device *dev, const char* string_num)
{
	int err = 0;
	int ampdu_mpdu;

	ampdu_mpdu = bcm_atoi(string_num);

	if (ampdu_mpdu > 32) {
		DHD_ERROR(("wl_android_set_ampdu_mpdu : ampdu_mpdu MAX value is 32.\n"));
		return -1;
	}

	DHD_ERROR(("wl_android_set_ampdu_mpdu : ampdu_mpdu = %d\n", ampdu_mpdu));
	err = wldev_iovar_setint(dev, "ampdu_mpdu", ampdu_mpdu);
	if (err < 0) {
		DHD_ERROR(("wl_android_set_ampdu_mpdu : ampdu_mpdu set error. %d\n", err));
		return -1;
	}

	return 0;
}
#endif /* SUPPORT_AMPDU_MPDU_CMD */
#endif /* CUSTOMER_HW4_PRIVATE_CMD */

#if defined(CONFIG_WLAN_BEYONDX) || defined(CONFIG_SEC_5GMODEL)
extern int wl_cfg80211_send_msg_to_ril(void);
extern void wl_cfg80211_register_dev_ril_bridge_event_notifier(void);
extern void wl_cfg80211_unregister_dev_ril_bridge_event_notifier(void);
extern int g_mhs_chan_for_cpcoex;
#endif /* CONFIG_WLAN_BEYONDX || defined(CONFIG_SEC_5GMODEL) */

#if defined(WL_SUPPORT_AUTO_CHANNEL)
/* SoftAP feature */
#define APCS_BAND_2G_LEGACY1	20
#define APCS_BAND_2G_LEGACY2	0
#define APCS_BAND_AUTO		"band=auto"
#define APCS_BAND_2G		"band=2g"
#define APCS_BAND_5G		"band=5g"
#define APCS_MAX_2G_CHANNELS	11
#define APCS_MAX_RETRY		10
#define APCS_DEFAULT_2G_CH	1
#define APCS_DEFAULT_5G_CH	149
static int
wl_android_set_auto_channel(struct net_device *dev, const char* cmd_str,
	char* command, int total_len)
{
	int channel = 0;
	int chosen = 0;
	int retry = 0;
	int ret = 0;
	int spect = 0;
	u8 *reqbuf = NULL;
	uint32 band = WLC_BAND_2G;
	uint32 buf_size;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	if (cmd_str) {
		WL_INFORM(("Command: %s len:%d \n", cmd_str, (int)strlen(cmd_str)));
		if (strncmp(cmd_str, APCS_BAND_AUTO, strlen(APCS_BAND_AUTO)) == 0) {
			band = WLC_BAND_AUTO;
		} else if (strnicmp(cmd_str, APCS_BAND_5G, strlen(APCS_BAND_5G)) == 0) {
			band = WLC_BAND_5G;
		} else if (strnicmp(cmd_str, APCS_BAND_2G, strlen(APCS_BAND_2G)) == 0) {
			band = WLC_BAND_2G;
		} else {
			/*
			 * For backward compatibility: Some platforms used to issue argument 20 or 0
			 * to enforce the 2G channel selection
			 */
			channel = bcm_atoi(cmd_str);
			if ((channel == APCS_BAND_2G_LEGACY1) ||
				(channel == APCS_BAND_2G_LEGACY2)) {
				band = WLC_BAND_2G;
			} else {
				WL_ERR(("Invalid argument\n"));
				return -EINVAL;
			}
		}
	} else {
		/* If no argument is provided, default to 2G */
		WL_ERR(("No argument given default to 2.4G scan\n"));
		band = WLC_BAND_2G;
	}
	WL_INFORM(("HAPD_AUTO_CHANNEL = %d, band=%d \n", channel, band));

#if defined(CONFIG_WLAN_BEYONDX) || defined(CONFIG_SEC_5GMODEL)
	wl_cfg80211_register_dev_ril_bridge_event_notifier();
	if (band == WLC_BAND_2G) {
		wl_cfg80211_send_msg_to_ril();

		if (g_mhs_chan_for_cpcoex) {
			channel = g_mhs_chan_for_cpcoex;
			g_mhs_chan_for_cpcoex = 0;
			goto done2;
		}
	}
	wl_cfg80211_unregister_dev_ril_bridge_event_notifier();
#endif /* CONFIG_WLAN_BEYONDX || defined(CONFIG_SEC_5GMODEL) */

	/* If STA is connected, return is STA channel, else ACS can be issued,
	 * set spect to 0 and proceed with ACS
	 */
	channel = wl_cfg80211_get_sta_channel(cfg);
	if (channel) {
		channel = (channel <= CH_MAX_2G_CHANNEL) ?
			channel : APCS_DEFAULT_2G_CH;
		goto done2;
	}

	ret = wldev_ioctl_get(dev, WLC_GET_SPECT_MANAGMENT, &spect, sizeof(spect));
	if (ret) {
		WL_ERR(("ACS: error getting the spect, ret=%d\n", ret));
		goto done;
	}

	if (spect > 0) {
		ret = wl_cfg80211_set_spect(dev, 0);
		if (ret < 0) {
			WL_ERR(("ACS: error while setting spect, ret=%d\n", ret));
			goto done;
		}
	}

	reqbuf = (u8 *)MALLOCZ(cfg->osh, CHANSPEC_BUF_SIZE);
	if (reqbuf == NULL) {
		WL_ERR(("failed to allocate chanspec buffer\n"));
		return -ENOMEM;
	}

	if (band == WLC_BAND_AUTO) {
		WL_DBG(("ACS full channel scan \n"));
		reqbuf[0] = htod32(0);
	} else if (band == WLC_BAND_5G) {
		WL_DBG(("ACS 5G band scan \n"));
		if ((ret = wl_cfg80211_get_chanspecs_5g(dev, reqbuf, CHANSPEC_BUF_SIZE)) < 0) {
			WL_ERR(("ACS 5g chanspec retreival failed! \n"));
			goto done;
		}
	} else if (band == WLC_BAND_2G) {
		/*
		 * If channel argument is not provided/ argument 20 is provided,
		 * Restrict channel to 2GHz, 20MHz BW, No SB
		 */
		WL_DBG(("ACS 2G band scan \n"));
		if ((ret = wl_cfg80211_get_chanspecs_2g(dev, reqbuf, CHANSPEC_BUF_SIZE)) < 0) {
			WL_ERR(("ACS 2g chanspec retreival failed! \n"));
			goto done;
		}
	} else {
		WL_ERR(("ACS: No band chosen\n"));
		goto done2;
	}

	buf_size = (band == WLC_BAND_AUTO) ? sizeof(int) : CHANSPEC_BUF_SIZE;
	ret = wldev_ioctl_set(dev, WLC_START_CHANNEL_SEL, (void *)reqbuf,
		buf_size);
	if (ret < 0) {
		WL_ERR(("can't start auto channel scan, err = %d\n", ret));
		channel = 0;
		goto done;
	}

	/* Wait for auto channel selection, max 3000 ms */
	if ((band == WLC_BAND_2G) || (band == WLC_BAND_5G)) {
		OSL_SLEEP(500);
	} else {
		/*
		 * Full channel scan at the minimum takes 1.2secs
		 * even with parallel scan. max wait time: 3500ms
		 */
		OSL_SLEEP(1000);
	}

	retry = APCS_MAX_RETRY;
	while (retry--) {
		ret = wldev_ioctl_get(dev, WLC_GET_CHANNEL_SEL, &chosen,
			sizeof(chosen));
		if (ret < 0) {
			chosen = 0;
		} else {
			chosen = dtoh32(chosen);
		}

		if (chosen) {
			int chosen_band;
			int apcs_band;
#ifdef D11AC_IOTYPES
			if (wl_cfg80211_get_ioctl_version() == 1) {
				channel = LCHSPEC_CHANNEL((chanspec_t)chosen);
			} else {
				channel = CHSPEC_CHANNEL((chanspec_t)chosen);
			}
#else
			channel = CHSPEC_CHANNEL((chanspec_t)chosen);
#endif /* D11AC_IOTYPES */
			apcs_band = (band == WLC_BAND_AUTO) ? WLC_BAND_2G : band;
			chosen_band = (channel <= CH_MAX_2G_CHANNEL) ? WLC_BAND_2G : WLC_BAND_5G;
			if (apcs_band == chosen_band) {
				WL_ERR(("selected channel = %d\n", channel));
				break;
			}
		}
		WL_DBG(("%d tried, ret = %d, chosen = 0x%x\n",
			(APCS_MAX_RETRY - retry), ret, chosen));
		OSL_SLEEP(250);
	}

done:
	if ((retry == 0) || (ret < 0)) {
		/* On failure, fallback to a default channel */
		if (band == WLC_BAND_5G) {
			channel = APCS_DEFAULT_5G_CH;
		} else {
			channel = APCS_DEFAULT_2G_CH;
		}
		WL_ERR(("ACS failed. Fall back to default channel (%d) \n", channel));
	}
done2:
	if (spect > 0) {
		if ((ret = wl_cfg80211_set_spect(dev, spect) < 0)) {
			WL_ERR(("ACS: error while setting spect\n"));
		}
	}

	if (reqbuf) {
		MFREE(cfg->osh, reqbuf, CHANSPEC_BUF_SIZE);
	}

	if (channel) {
		ret = snprintf(command, total_len, "%d", channel);
		WL_INFORM(("command result is %s \n", command));
	}

	return ret;
}
#endif /* WL_SUPPORT_AUTO_CHANNEL */

#ifdef SUPPORT_HIDDEN_AP
static int
wl_android_set_max_num_sta(struct net_device *dev, const char* string_num)
{
	int err = BCME_ERROR;
	int max_assoc;

	max_assoc = bcm_atoi(string_num);
	DHD_INFO(("wl_android_set_max_num_sta : HAPD_MAX_NUM_STA = %d\n", max_assoc));

	err = wldev_iovar_setint(dev, "maxassoc", max_assoc);
	if (err < 0) {
		WL_ERR(("failed to set maxassoc, error:%d\n", err));
	}

	return err;
}

static int
wl_android_set_ssid(struct net_device *dev, const char* hapd_ssid)
{
	wlc_ssid_t ssid;
	s32 ret;

	ssid.SSID_len = strlen(hapd_ssid);
	if (ssid.SSID_len == 0) {
		WL_ERR(("wl_android_set_ssids : No SSID\n"));
		return -1;
	}
	if (ssid.SSID_len > DOT11_MAX_SSID_LEN) {
		ssid.SSID_len = DOT11_MAX_SSID_LEN;
		WL_ERR(("wl_android_set_ssid : Too long SSID Length %zu\n", strlen(hapd_ssid)));
	}
	bcm_strncpy_s(ssid.SSID, sizeof(ssid.SSID), hapd_ssid, ssid.SSID_len);
	DHD_INFO(("wl_android_set_ssid: HAPD_SSID = %s\n", ssid.SSID));
	ret = wldev_ioctl_set(dev, WLC_SET_SSID, &ssid, sizeof(wlc_ssid_t));
	if (ret < 0) {
		WL_ERR(("wl_android_set_ssid : WLC_SET_SSID Error:%d\n", ret));
	}
	return 1;

}

static int
wl_android_set_hide_ssid(struct net_device *dev, const char* string_num)
{
	int hide_ssid;
	int enable = 0;
	int err = BCME_ERROR;

	hide_ssid = bcm_atoi(string_num);
	DHD_INFO(("wl_android_set_hide_ssid: HIDE_SSID = %d\n", hide_ssid));
	if (hide_ssid) {
		enable = 1;
	}

	err = wldev_iovar_setint(dev, "closednet", enable);
	if (err < 0) {
		WL_ERR(("failed to set closednet, error:%d\n", err));
	}

	return err;
}
#endif /* SUPPORT_HIDDEN_AP */

#ifdef CUSTOMER_HW4_PRIVATE_CMD
#ifdef SUPPORT_SOFTAP_SINGL_DISASSOC
static int
wl_android_sta_diassoc(struct net_device *dev, const char* straddr)
{
	scb_val_t scbval;
	int error  = 0;

	DHD_INFO(("wl_android_sta_diassoc: deauth STA %s\n", straddr));

	/* Unspecified reason */
	scbval.val = htod32(1);

	if (bcm_ether_atoe(straddr, &scbval.ea) == 0) {
		DHD_ERROR(("wl_android_sta_diassoc: Invalid station MAC Address!!!\n"));
		return -1;
	}

	DHD_ERROR(("wl_android_sta_diassoc: deauth STA: "MACDBG " scb_val.val %d\n",
		MAC2STRDBG(scbval.ea.octet), scbval.val));

	error = wldev_ioctl_set(dev, WLC_SCB_DEAUTHENTICATE_FOR_REASON, &scbval,
		sizeof(scb_val_t));
	if (error) {
		DHD_ERROR(("Fail to DEAUTH station, error = %d\n", error));
	}

	return 1;
}
#endif /* SUPPORT_SOFTAP_SINGL_DISASSOC */

#ifdef SUPPORT_SET_LPC
static int
wl_android_set_lpc(struct net_device *dev, const char* string_num)
{
	int lpc_enabled, ret;
	s32 val = 1;

	lpc_enabled = bcm_atoi(string_num);
	DHD_INFO(("wl_android_set_lpc: HAPD_LPC_ENABLED = %d\n", lpc_enabled));

	ret = wldev_ioctl_set(dev, WLC_DOWN, &val, sizeof(s32));
	if (ret < 0)
		DHD_ERROR(("WLC_DOWN error %d\n", ret));

	wldev_iovar_setint(dev, "lpc", lpc_enabled);

	ret = wldev_ioctl_set(dev, WLC_UP, &val, sizeof(s32));
	if (ret < 0)
		DHD_ERROR(("WLC_UP error %d\n", ret));

	return 1;
}
#endif /* SUPPORT_SET_LPC */

static int
wl_android_ch_res_rl(struct net_device *dev, bool change)
{
	int error = 0;
	s32 srl = 7;
	s32 lrl = 4;
	printk("wl_android_ch_res_rl: enter\n");
	if (change) {
		srl = 4;
		lrl = 2;
	}

	BCM_REFERENCE(lrl);

	error = wldev_ioctl_set(dev, WLC_SET_SRL, &srl, sizeof(s32));
	if (error) {
		DHD_ERROR(("Failed to set SRL, error = %d\n", error));
	}
#ifndef CUSTOM_LONG_RETRY_LIMIT
	error = wldev_ioctl_set(dev, WLC_SET_LRL, &lrl, sizeof(s32));
	if (error) {
		DHD_ERROR(("Failed to set LRL, error = %d\n", error));
	}
#endif /* CUSTOM_LONG_RETRY_LIMIT */
	return error;
}

#ifdef SUPPORT_LTECX
#define DEFAULT_WLANRX_PROT	1
#define DEFAULT_LTERX_PROT	0
#define DEFAULT_LTETX_ADV	1200

static int
wl_android_set_ltecx(struct net_device *dev, const char* string_num)
{
	uint16 chan_bitmap;
	int ret;

	chan_bitmap = bcm_strtoul(string_num, NULL, 16);

	DHD_INFO(("wl_android_set_ltecx: LTECOEX 0x%x\n", chan_bitmap));

	if (chan_bitmap) {
		ret = wldev_iovar_setint(dev, "mws_coex_bitmap", chan_bitmap);
		if (ret < 0) {
			DHD_ERROR(("mws_coex_bitmap error %d\n", ret));
		}

		ret = wldev_iovar_setint(dev, "mws_wlanrx_prot", DEFAULT_WLANRX_PROT);
		if (ret < 0) {
			DHD_ERROR(("mws_wlanrx_prot error %d\n", ret));
		}

		ret = wldev_iovar_setint(dev, "mws_lterx_prot", DEFAULT_LTERX_PROT);
		if (ret < 0) {
			DHD_ERROR(("mws_lterx_prot error %d\n", ret));
		}

		ret = wldev_iovar_setint(dev, "mws_ltetx_adv", DEFAULT_LTETX_ADV);
		if (ret < 0) {
			DHD_ERROR(("mws_ltetx_adv error %d\n", ret));
		}
	} else {
		ret = wldev_iovar_setint(dev, "mws_coex_bitmap", chan_bitmap);
		if (ret < 0) {
			if (ret == BCME_UNSUPPORTED) {
				DHD_ERROR(("LTECX_CHAN_BITMAP is UNSUPPORTED\n"));
			} else {
				DHD_ERROR(("LTECX_CHAN_BITMAP error %d\n", ret));
			}
		}
	}
	return 1;
}
#endif /* SUPPORT_LTECX */

#ifdef WL_RELMCAST
static int
wl_android_rmc_enable(struct net_device *net, int rmc_enable)
{
	int err;

	err = wldev_iovar_setint(net, "rmc_ackreq", rmc_enable);
	if (err != BCME_OK) {
		DHD_ERROR(("wl_android_rmc_enable: rmc_ackreq, error = %d\n", err));
	}
	return err;
}

static int
wl_android_rmc_set_leader(struct net_device *dev, const char* straddr)
{
	int error  = BCME_OK;
	char smbuf[WLC_IOCTL_SMLEN];
	wl_rmc_entry_t rmc_entry;
	DHD_INFO(("wl_android_rmc_set_leader: Set new RMC leader %s\n", straddr));

	bzero(&rmc_entry, sizeof(wl_rmc_entry_t));
	if (!bcm_ether_atoe(straddr, &rmc_entry.addr)) {
		if (strlen(straddr) == 1 && bcm_atoi(straddr) == 0) {
			DHD_INFO(("wl_android_rmc_set_leader: Set auto leader selection mode\n"));
			bzero(&rmc_entry, sizeof(wl_rmc_entry_t));
		} else {
			DHD_ERROR(("wl_android_rmc_set_leader: No valid mac address provided\n"));
			return BCME_ERROR;
		}
	}

	error = wldev_iovar_setbuf(dev, "rmc_ar", &rmc_entry, sizeof(wl_rmc_entry_t),
		smbuf, sizeof(smbuf), NULL);

	if (error != BCME_OK) {
		DHD_ERROR(("wl_android_rmc_set_leader: Unable to set RMC leader, error = %d\n",
			error));
	}

	return error;
}

static int wl_android_set_rmc_event(struct net_device *dev, char *command)
{
	int err = 0;
	int pid = 0;

	if (sscanf(command, CMD_SET_RMC_EVENT " %d", &pid) <= 0) {
		WL_ERR(("Failed to get Parameter from : %s\n", command));
		return -1;
	}

	/* set pid, and if the event was happened, let's send a notification through netlink */
	wl_cfg80211_set_rmc_pid(dev, pid);

	WL_DBG(("RMC pid=%d\n", pid));

	return err;
}
#endif /* WL_RELMCAST */

int wl_android_get_singlecore_scan(struct net_device *dev, char *command, int total_len)
{
	int error = 0;
	int bytes_written = 0;
	int mode = 0;

	error = wldev_iovar_getint(dev, "scan_ps", &mode);
	if (error) {
		DHD_ERROR(("wl_android_get_singlecore_scan: Failed to get single core scan Mode,"
			" error = %d\n",
			error));
		return -1;
	}

	bytes_written = snprintf(command, total_len, "%s %d", CMD_GET_SCSCAN, mode);

	return bytes_written;
}

int wl_android_set_singlecore_scan(struct net_device *dev, char *command)
{
	int error = 0;
	int mode = 0;

	if (sscanf(command, "%*s %d", &mode) != 1) {
		DHD_ERROR(("wl_android_set_singlecore_scan: Failed to get Parameter\n"));
		return -1;
	}

	error = wldev_iovar_setint(dev, "scan_ps", mode);
	if (error) {
		DHD_ERROR(("wl_android_set_singlecore_scan[1]: Failed to set Mode %d, error = %d\n",
		mode, error));
		return -1;
	}

	return error;
}
#ifdef TEST_TX_POWER_CONTROL
static int
wl_android_set_tx_power(struct net_device *dev, const char* string_num)
{
	int err = 0;
	s32 dbm;
	enum nl80211_tx_power_setting type;

	dbm = bcm_atoi(string_num);

	if (dbm < -1) {
		DHD_ERROR(("wl_android_set_tx_power: dbm is negative...\n"));
		return -EINVAL;
	}

	if (dbm == -1)
		type = NL80211_TX_POWER_AUTOMATIC;
	else
		type = NL80211_TX_POWER_FIXED;

	err = wl_set_tx_power(dev, type, dbm);
	if (unlikely(err)) {
		DHD_ERROR(("wl_android_set_tx_power: error (%d)\n", err));
		return err;
	}

	return 1;
}

static int
wl_android_get_tx_power(struct net_device *dev, char *command, int total_len)
{
	int err;
	int bytes_written;
	s32 dbm = 0;

	err = wl_get_tx_power(dev, &dbm);
	if (unlikely(err)) {
		DHD_ERROR(("wl_android_get_tx_power: error (%d)\n", err));
		return err;
	}

	bytes_written = snprintf(command, total_len, "%s %d",
		CMD_TEST_GET_TX_POWER, dbm);

	DHD_ERROR(("wl_android_get_tx_power: GET_TX_POWER: dBm=%d\n", dbm));

	return bytes_written;
}
#endif /* TEST_TX_POWER_CONTROL */

static int
wl_android_set_sarlimit_txctrl(struct net_device *dev, const char* string_num)
{
	int err = BCME_ERROR;
	int setval = 0;
	s32 mode = bcm_atoi(string_num);
	s32 mode_bit = 0;
	int enab = 0;

	/* As Samsung specific and their requirement,
	 * the mode set as the following form.
	 * -1 : HEAD SAR disabled
	 *  0 : HEAD SAR enabled
	 *  1 : GRIP SAR disabled
	 *  2 : GRIP SAR enabled
	 *  3 : NR mmWave SAR disabled
	 *  4 : NR mmWave SAR enabled
	 *  5 : NR Sub6 SAR disabled
	 *  6 : NR Sub6 SAR enabled
	 *  7 : SAR BACKOFF disabled all
	 * The 'SAR BACKOFF disabled all' index should be the end of the mode.
	 */
	if ((mode < HEAD_SAR_BACKOFF_DISABLE) || (mode > SAR_BACKOFF_DISABLE_ALL)) {
		DHD_ERROR(("%s: Request for Unsupported:%d\n", __FUNCTION__, bcm_atoi(string_num)));
		err = BCME_RANGE;
		goto error;
	}

	mode_bit = mode + 1;
	enab = mode_bit % 2;
	mode_bit = mode_bit / 2;

	err = wldev_iovar_getint(dev, "sar_enable", &setval);
	if (unlikely(err)) {
		DHD_ERROR(("%s: Failed to get sar_enable - error (%d)\n", __FUNCTION__, err));
		goto error;
	}

	if (mode == SAR_BACKOFF_DISABLE_ALL) {
		DHD_ERROR(("%s: SAR limit control all mode disable!\n", __FUNCTION__));
		setval = 0;
	} else {
		DHD_ERROR(("%s: SAR limit control mode %d enab %d\n",
			__FUNCTION__, mode_bit, enab));
		if (enab) {
			setval |= (1 << mode_bit);
		} else {
			setval &= ~(1 << mode_bit);
		}
	}

	err = wldev_iovar_setint(dev, "sar_enable", setval);
	if (unlikely(err)) {
		DHD_ERROR(("%s: Failed to set sar_enable - error (%d)\n", __FUNCTION__, err));
		goto error;
	}
error:
	return err;
}

#ifdef SUPPORT_SET_TID
static int
wl_android_set_tid(struct net_device *dev, char* command)
{
	int err = BCME_ERROR;
	char *pos = command;
	char *token = NULL;
	uint8 mode = 0;
	uint32 uid = 0;
	uint8 prio = 0;
	dhd_pub_t *dhdp = wl_cfg80211_get_dhdp(dev);

	if (!dhdp) {
		WL_ERR(("dhd is NULL\n"));
		return err;
	}

	WL_DBG(("%s: command[%s]\n", __FUNCTION__, command));

	/* drop command */
	token = bcmstrtok(&pos, " ", NULL);

	token = bcmstrtok(&pos, " ", NULL);
	if (!token) {
		WL_ERR(("Invalid arguments\n"));
		return err;
	}

	mode = bcm_atoi(token);

	if (mode < SET_TID_OFF || mode > SET_TID_BASED_ON_UID) {
		WL_ERR(("Invalid arguments, mode %d\n", mode));
			return err;
	}

	if (mode) {
		token = bcmstrtok(&pos, " ", NULL);
		if (!token) {
			WL_ERR(("Invalid arguments for target uid\n"));
			return err;
		}

		uid = bcm_atoi(token);

		token = bcmstrtok(&pos, " ", NULL);
		if (!token) {
			WL_ERR(("Invalid arguments for target tid\n"));
			return err;
		}

		prio = bcm_atoi(token);
		if (prio >= 0 && prio <= MAXPRIO) {
			dhdp->tid_mode = mode;
			dhdp->target_uid = uid;
			dhdp->target_tid = prio;
		} else {
			WL_ERR(("Invalid arguments, prio %d\n", prio));
			return err;
		}
	} else {
		dhdp->tid_mode = SET_TID_OFF;
		dhdp->target_uid = 0;
		dhdp->target_tid = 0;
	}

	WL_DBG(("%s mode [%d], uid [%d], tid [%d]\n", __FUNCTION__,
		dhdp->tid_mode, dhdp->target_uid, dhdp->target_tid));

	err = BCME_OK;
	return err;
}

static int
wl_android_get_tid(struct net_device *dev, char* command, int total_len)
{
	int bytes_written = BCME_ERROR;
	dhd_pub_t *dhdp = wl_cfg80211_get_dhdp(dev);

	if (!dhdp) {
		WL_ERR(("dhd is NULL\n"));
		return bytes_written;
	}

	bytes_written = snprintf(command, total_len, "mode %d uid %d tid %d",
		dhdp->tid_mode, dhdp->target_uid, dhdp->target_tid);

	WL_DBG(("%s: command results %s\n", __FUNCTION__, command));

	return bytes_written;
}
#endif /* SUPPORT_SET_TID */
#endif /* CUSTOMER_HW4_PRIVATE_CMD */

int wl_android_set_roam_mode(struct net_device *dev, char *command)
{
	int error = 0;
	int mode = 0;

	if (sscanf(command, "%*s %d", &mode) != 1) {
		DHD_ERROR(("wl_android_set_roam_mode: Failed to get Parameter\n"));
		return -1;
	}

	error = wldev_iovar_setint(dev, "roam_off", mode);
	if (error) {
		DHD_ERROR(("wl_android_set_roam_mode: Failed to set roaming Mode %d, error = %d\n",
		mode, error));
		return -1;
	}
	else
		DHD_ERROR(("wl_android_set_roam_mode: succeeded to set roaming Mode %d,"
		" error = %d\n",
		mode, error));
	return 0;
}

int wl_android_set_ibss_beacon_ouidata(struct net_device *dev, char *command, int total_len)
{
	char ie_buf[VNDR_IE_MAX_LEN];
	char *ioctl_buf = NULL;
	char hex[] = "XX";
	char *pcmd = NULL;
	int ielen = 0, datalen = 0, idx = 0, tot_len = 0;
	vndr_ie_setbuf_t *vndr_ie = NULL;
	s32 iecount;
	uint32 pktflag;
	s32 err = BCME_OK, bssidx;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	/* Check the VSIE (Vendor Specific IE) which was added.
	 *  If exist then send IOVAR to delete it
	 */
	if (wl_cfg80211_ibss_vsie_delete(dev) != BCME_OK) {
		return -EINVAL;
	}

	if (total_len < (strlen(CMD_SETIBSSBEACONOUIDATA) + 1)) {
		WL_ERR(("error. total_len:%d\n", total_len));
		return -EINVAL;
	}

	pcmd = command + strlen(CMD_SETIBSSBEACONOUIDATA) + 1;
	for (idx = 0; idx < DOT11_OUI_LEN; idx++) {
		if (*pcmd == '\0') {
			WL_ERR(("error while parsing OUI.\n"));
			return -EINVAL;
		}
		hex[0] = *pcmd++;
		hex[1] = *pcmd++;
		ie_buf[idx] =  (uint8)simple_strtoul(hex, NULL, 16);
	}
	pcmd++;
	while ((*pcmd != '\0') && (idx < VNDR_IE_MAX_LEN)) {
		hex[0] = *pcmd++;
		hex[1] = *pcmd++;
		ie_buf[idx++] =  (uint8)simple_strtoul(hex, NULL, 16);
		datalen++;
	}

	if (datalen <= 0) {
		WL_ERR(("error. vndr ie len:%d\n", datalen));
		return -EINVAL;
	}

	tot_len = (int)(sizeof(vndr_ie_setbuf_t) + (datalen - 1));
	vndr_ie = (vndr_ie_setbuf_t *)MALLOCZ(cfg->osh, tot_len);
	if (!vndr_ie) {
		WL_ERR(("IE memory alloc failed\n"));
		return -ENOMEM;
	}
	/* Copy the vndr_ie SET command ("add"/"del") to the buffer */
	strlcpy(vndr_ie->cmd, "add", sizeof(vndr_ie->cmd));

	/* Set the IE count - the buffer contains only 1 IE */
	iecount = htod32(1);
	memcpy((void *)&vndr_ie->vndr_ie_buffer.iecount, &iecount, sizeof(s32));

	/* Set packet flag to indicate that BEACON's will contain this IE */
	pktflag = htod32(VNDR_IE_BEACON_FLAG | VNDR_IE_PRBRSP_FLAG);
	memcpy((void *)&vndr_ie->vndr_ie_buffer.vndr_ie_list[0].pktflag, &pktflag,
		sizeof(u32));
	/* Set the IE ID */
	vndr_ie->vndr_ie_buffer.vndr_ie_list[0].vndr_ie_data.id = (uchar) DOT11_MNG_PROPR_ID;

	memcpy(&vndr_ie->vndr_ie_buffer.vndr_ie_list[0].vndr_ie_data.oui, &ie_buf,
		DOT11_OUI_LEN);
	memcpy(&vndr_ie->vndr_ie_buffer.vndr_ie_list[0].vndr_ie_data.data,
		&ie_buf[DOT11_OUI_LEN], datalen);

	ielen = DOT11_OUI_LEN + datalen;
	vndr_ie->vndr_ie_buffer.vndr_ie_list[0].vndr_ie_data.len = (uchar) ielen;

	ioctl_buf = (char *)MALLOC(cfg->osh, WLC_IOCTL_MEDLEN);
	if (!ioctl_buf) {
		WL_ERR(("ioctl memory alloc failed\n"));
		if (vndr_ie) {
			MFREE(cfg->osh, vndr_ie, tot_len);
		}
		return -ENOMEM;
	}
	bzero(ioctl_buf, WLC_IOCTL_MEDLEN);	/* init the buffer */
	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find index failed\n"));
		err = BCME_ERROR;
		goto end;
	}
	err = wldev_iovar_setbuf_bsscfg(dev, "vndr_ie", vndr_ie, tot_len, ioctl_buf,
			WLC_IOCTL_MEDLEN, bssidx, &cfg->ioctl_buf_sync);
end:
	if (err != BCME_OK) {
		err = -EINVAL;
		if (vndr_ie) {
			MFREE(cfg->osh, vndr_ie, tot_len);
		}
	}
	else {
		/* do NOT free 'vndr_ie' for the next process */
		wl_cfg80211_ibss_vsie_set_buffer(dev, vndr_ie, tot_len);
	}

	if (ioctl_buf) {
		MFREE(cfg->osh, ioctl_buf, WLC_IOCTL_MEDLEN);
	}

	return err;
}

#if defined(BCMFW_ROAM_ENABLE)
static int
wl_android_set_roampref(struct net_device *dev, char *command, int total_len)
{
	int error = 0;
	char smbuf[WLC_IOCTL_SMLEN];
	uint8 buf[MAX_BUF_SIZE];
	uint8 *pref = buf;
	char *pcmd;
	int num_ucipher_suites = 0;
	int num_akm_suites = 0;
	wpa_suite_t ucipher_suites[MAX_NUM_SUITES];
	wpa_suite_t akm_suites[MAX_NUM_SUITES];
	int num_tuples = 0;
	int total_bytes = 0;
	int total_len_left;
	int i, j;
	char hex[] = "XX";

	pcmd = command + strlen(CMD_SET_ROAMPREF) + 1;
	total_len_left = total_len - strlen(CMD_SET_ROAMPREF) + 1;

	num_akm_suites = simple_strtoul(pcmd, NULL, 16);
	if (num_akm_suites > MAX_NUM_SUITES) {
		DHD_ERROR(("too many AKM suites = %d\n", num_akm_suites));
		return -1;
	}

	/* Increment for number of AKM suites field + space */
	pcmd += 3;
	total_len_left -= 3;

	/* check to make sure pcmd does not overrun */
	if (total_len_left < (num_akm_suites * WIDTH_AKM_SUITE))
		return -1;

	bzero(buf, sizeof(buf));
	bzero(akm_suites, sizeof(akm_suites));
	bzero(ucipher_suites, sizeof(ucipher_suites));

	/* Save the AKM suites passed in the command */
	for (i = 0; i < num_akm_suites; i++) {
		/* Store the MSB first, as required by join_pref */
		for (j = 0; j < 4; j++) {
			hex[0] = *pcmd++;
			hex[1] = *pcmd++;
			buf[j] = (uint8)simple_strtoul(hex, NULL, 16);
		}
		memcpy((uint8 *)&akm_suites[i], buf, sizeof(uint32));
	}

	total_len_left -= (num_akm_suites * WIDTH_AKM_SUITE);
	num_ucipher_suites = simple_strtoul(pcmd, NULL, 16);
	/* Increment for number of cipher suites field + space */
	pcmd += 3;
	total_len_left -= 3;

	if (total_len_left < (num_ucipher_suites * WIDTH_AKM_SUITE))
		return -1;

	/* Save the cipher suites passed in the command */
	for (i = 0; i < num_ucipher_suites; i++) {
		/* Store the MSB first, as required by join_pref */
		for (j = 0; j < 4; j++) {
			hex[0] = *pcmd++;
			hex[1] = *pcmd++;
			buf[j] = (uint8)simple_strtoul(hex, NULL, 16);
		}
		memcpy((uint8 *)&ucipher_suites[i], buf, sizeof(uint32));
	}

	/* Join preference for RSSI
	 * Type	  : 1 byte (0x01)
	 * Length : 1 byte (0x02)
	 * Value  : 2 bytes	(reserved)
	 */
	*pref++ = WL_JOIN_PREF_RSSI;
	*pref++ = JOIN_PREF_RSSI_LEN;
	*pref++ = 0;
	*pref++ = 0;

	/* Join preference for WPA
	 * Type	  : 1 byte (0x02)
	 * Length : 1 byte (not used)
	 * Value  : (variable length)
	 *		reserved: 1 byte
	 *      count	: 1 byte (no of tuples)
	 *		Tuple1	: 12 bytes
	 *			akm[4]
	 *			ucipher[4]
	 *			mcipher[4]
	 *		Tuple2	: 12 bytes
	 *		Tuplen	: 12 bytes
	 */
	num_tuples = num_akm_suites * num_ucipher_suites;
	if (num_tuples != 0) {
		if (num_tuples <= JOIN_PREF_MAX_WPA_TUPLES) {
			*pref++ = WL_JOIN_PREF_WPA;
			*pref++ = 0;
			*pref++ = 0;
			*pref++ = (uint8)num_tuples;
			total_bytes = JOIN_PREF_RSSI_SIZE + JOIN_PREF_WPA_HDR_SIZE +
				(JOIN_PREF_WPA_TUPLE_SIZE * num_tuples);
		} else {
			DHD_ERROR(("wl_android_set_roampref: Too many wpa configs"
				" for join_pref \n"));
			return -1;
		}
	} else {
		/* No WPA config, configure only RSSI preference */
		total_bytes = JOIN_PREF_RSSI_SIZE;
	}

	/* akm-ucipher-mcipher tuples in the format required for join_pref */
	for (i = 0; i < num_ucipher_suites; i++) {
		for (j = 0; j < num_akm_suites; j++) {
			memcpy(pref, (uint8 *)&akm_suites[j], WPA_SUITE_LEN);
			pref += WPA_SUITE_LEN;
			memcpy(pref, (uint8 *)&ucipher_suites[i], WPA_SUITE_LEN);
			pref += WPA_SUITE_LEN;
			/* Set to 0 to match any available multicast cipher */
			bzero(pref, WPA_SUITE_LEN);
			pref += WPA_SUITE_LEN;
		}
	}

	prhex("join pref", (uint8 *)buf, total_bytes);
	error = wldev_iovar_setbuf(dev, "join_pref", buf, total_bytes, smbuf, sizeof(smbuf), NULL);
	if (error) {
		DHD_ERROR(("Failed to set join_pref, error = %d\n", error));
	}
	return error;
}
#endif /* defined(BCMFW_ROAM_ENABLE */

static int
wl_android_iolist_add(struct net_device *dev, struct list_head *head, struct io_cfg *config)
{
	struct io_cfg *resume_cfg;
	s32 ret;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	resume_cfg = (struct io_cfg *)MALLOCZ(cfg->osh, sizeof(struct io_cfg));
	if (!resume_cfg)
		return -ENOMEM;

	if (config->iovar) {
		ret = wldev_iovar_getint(dev, config->iovar, &resume_cfg->param);
		if (ret) {
			DHD_ERROR(("wl_android_iolist_add: Failed to get current %s value\n",
				config->iovar));
			goto error;
		}

		ret = wldev_iovar_setint(dev, config->iovar, config->param);
		if (ret) {
			DHD_ERROR(("wl_android_iolist_add: Failed to set %s to %d\n",
				config->iovar, config->param));
			goto error;
		}

		resume_cfg->iovar = config->iovar;
	} else {
		resume_cfg->arg = MALLOCZ(cfg->osh, config->len);
		if (!resume_cfg->arg) {
			ret = -ENOMEM;
			goto error;
		}
		ret = wldev_ioctl_get(dev, config->ioctl, resume_cfg->arg, config->len);
		if (ret) {
			DHD_ERROR(("wl_android_iolist_add: Failed to get ioctl %d\n",
				config->ioctl));
			goto error;
		}
		ret = wldev_ioctl_set(dev, config->ioctl + 1, config->arg, config->len);
		if (ret) {
			DHD_ERROR(("wl_android_iolist_add: Failed to set %s to %d\n",
				config->iovar, config->param));
			goto error;
		}
		if (config->ioctl + 1 == WLC_SET_PM)
			wl_cfg80211_update_power_mode(dev);
		resume_cfg->ioctl = config->ioctl;
		resume_cfg->len = config->len;
	}

	list_add(&resume_cfg->list, head);

	return 0;
error:
	MFREE(cfg->osh, resume_cfg->arg, config->len);
	MFREE(cfg->osh, resume_cfg, sizeof(struct io_cfg));
	return ret;
}

static void
wl_android_iolist_resume(struct net_device *dev, struct list_head *head)
{
	struct io_cfg *config;
	struct list_head *cur, *q;
	s32 ret = 0;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	list_for_each_safe(cur, q, head) {
		config = list_entry(cur, struct io_cfg, list);
		GCC_DIAGNOSTIC_POP();
		if (config->iovar) {
			if (!ret)
				ret = wldev_iovar_setint(dev, config->iovar,
					config->param);
		} else {
			if (!ret)
				ret = wldev_ioctl_set(dev, config->ioctl + 1,
					config->arg, config->len);
			if (config->ioctl + 1 == WLC_SET_PM)
				wl_cfg80211_update_power_mode(dev);
			MFREE(cfg->osh, config->arg, config->len);
		}
		list_del(cur);
		MFREE(cfg->osh, config, sizeof(struct io_cfg));
	}
}
static int
wl_android_set_miracast(struct net_device *dev, char *command)
{
	int mode, val = 0;
	int ret = 0;
	struct io_cfg config;

	if (sscanf(command, "%*s %d", &mode) != 1) {
		DHD_ERROR(("wl_android_set_miracasts: Failed to get Parameter\n"));
		return -1;
	}

	DHD_INFO(("wl_android_set_miracast: enter miracast mode %d\n", mode));

	if (miracast_cur_mode == mode) {
		return 0;
	}

	wl_android_iolist_resume(dev, &miracast_resume_list);
	miracast_cur_mode = MIRACAST_MODE_OFF;

	bzero((void *)&config, sizeof(config));
	switch (mode) {
	case MIRACAST_MODE_SOURCE:
#ifdef MIRACAST_MCHAN_ALGO
		/* setting mchan_algo to platform specific value */
		config.iovar = "mchan_algo";

		ret = wldev_ioctl_get(dev, WLC_GET_BCNPRD, &val, sizeof(int));
		if (!ret && val > 100) {
			config.param = 0;
			DHD_ERROR(("wl_android_set_miracast: Connected station's beacon interval: "
				"%d and set mchan_algo to %d \n",
				val, config.param));
		} else {
			config.param = MIRACAST_MCHAN_ALGO;
		}
		ret = wl_android_iolist_add(dev, &miracast_resume_list, &config);
		if (ret) {
			goto resume;
		}
#endif /* MIRACAST_MCHAN_ALGO */

#ifdef MIRACAST_MCHAN_BW
		/* setting mchan_bw to platform specific value */
		config.iovar = "mchan_bw";
		config.param = MIRACAST_MCHAN_BW;
		ret = wl_android_iolist_add(dev, &miracast_resume_list, &config);
		if (ret) {
			goto resume;
		}
#endif /* MIRACAST_MCHAN_BW */

#ifdef MIRACAST_AMPDU_SIZE
		/* setting apmdu to platform specific value */
		config.iovar = "ampdu_mpdu";
		config.param = MIRACAST_AMPDU_SIZE;
		ret = wl_android_iolist_add(dev, &miracast_resume_list, &config);
		if (ret) {
			goto resume;
		}
#endif /* MIRACAST_AMPDU_SIZE */

		/* Source mode shares most configurations with sink mode.
		 * Fall through here to avoid code duplication
		 */
		/* fall through */
	case MIRACAST_MODE_SINK:
		/* disable internal roaming */
		config.iovar = "roam_off";
		config.param = 1;
		config.arg = NULL;
		config.len = 0;
		ret = wl_android_iolist_add(dev, &miracast_resume_list, &config);
		if (ret) {
			goto resume;
		}

		/* tunr off pm */
		ret = wldev_ioctl_get(dev, WLC_GET_PM, &val, sizeof(val));
		if (ret) {
			goto resume;
		}

		if (val != PM_OFF) {
			val = PM_OFF;
			config.iovar = NULL;
			config.ioctl = WLC_GET_PM;
			config.arg = &val;
			config.len = sizeof(int);
			ret = wl_android_iolist_add(dev, &miracast_resume_list, &config);
			if (ret) {
				goto resume;
			}
		}
		break;
	case MIRACAST_MODE_OFF:
	default:
		break;
	}
	miracast_cur_mode = mode;

	return 0;

resume:
	DHD_ERROR(("wl_android_set_miracast: turnoff miracast mode because of err%d\n", ret));
	wl_android_iolist_resume(dev, &miracast_resume_list);
	return ret;
}

#define NETLINK_OXYGEN     30
#define AIBSS_BEACON_TIMEOUT	10

static struct sock *nl_sk = NULL;

static void wl_netlink_recv(struct sk_buff *skb)
{
	WL_ERR(("netlink_recv called\n"));
}

static int wl_netlink_init(void)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
	struct netlink_kernel_cfg cfg = {
		.input	= wl_netlink_recv,
	};
#endif // endif

	if (nl_sk != NULL) {
		WL_ERR(("nl_sk already exist\n"));
		return BCME_ERROR;
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0))
	nl_sk = netlink_kernel_create(&init_net, NETLINK_OXYGEN,
		0, wl_netlink_recv, NULL, THIS_MODULE);
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(3, 7, 0))
	nl_sk = netlink_kernel_create(&init_net, NETLINK_OXYGEN, THIS_MODULE, &cfg);
#else
	nl_sk = netlink_kernel_create(&init_net, NETLINK_OXYGEN, &cfg);
#endif // endif

	if (nl_sk == NULL) {
		WL_ERR(("nl_sk is not ready\n"));
		return BCME_ERROR;
	}

	return BCME_OK;
}

static void wl_netlink_deinit(void)
{
	if (nl_sk) {
		netlink_kernel_release(nl_sk);
		nl_sk = NULL;
	}
}

s32
wl_netlink_send_msg(int pid, int type, int seq, const void *data, size_t size)
{
	struct sk_buff *skb = NULL;
	struct nlmsghdr *nlh = NULL;
	int ret = -1;

	if (nl_sk == NULL) {
		WL_ERR(("nl_sk was not initialized\n"));
		goto nlmsg_failure;
	}

	skb = alloc_skb(NLMSG_SPACE(size), GFP_ATOMIC);
	if (skb == NULL) {
		WL_ERR(("failed to allocate memory\n"));
		goto nlmsg_failure;
	}

	nlh = nlmsg_put(skb, 0, 0, 0, size, 0);
	if (nlh == NULL) {
		WL_ERR(("failed to build nlmsg, skb_tailroom:%d, nlmsg_total_size:%d\n",
			skb_tailroom(skb), nlmsg_total_size(size)));
		dev_kfree_skb(skb);
		goto nlmsg_failure;
	}

	memcpy(nlmsg_data(nlh), data, size);
	nlh->nlmsg_seq = seq;
	nlh->nlmsg_type = type;

	/* netlink_unicast() takes ownership of the skb and frees it itself. */
	ret = netlink_unicast(nl_sk, skb, pid, 0);
	WL_DBG(("netlink_unicast() pid=%d, ret=%d\n", pid, ret));

nlmsg_failure:
	return ret;
}

#ifdef WLAIBSS
static int wl_android_set_ibss_txfail_event(struct net_device *dev, char *command, int total_len)
{
	int err = 0;
	int retry = 0;
	int pid = 0;
	aibss_txfail_config_t txfail_config = {0, 0, 0, 0, 0};
	char smbuf[WLC_IOCTL_SMLEN];

	if (sscanf(command, CMD_SETIBSSTXFAILEVENT " %d %d", &retry, &pid) <= 0) {
		WL_ERR(("Failed to get Parameter from : %s\n", command));
		return -1;
	}

	/* set pid, and if the event was happened, let's send a notification through netlink */
	wl_cfg80211_set_txfail_pid(dev, pid);

#ifdef WL_RELMCAST
	/* using same pid for RMC, AIBSS shares same pid with RMC and it is set once */
	wl_cfg80211_set_rmc_pid(dev, pid);
#endif /* WL_RELMCAST */

	/* If retry value is 0, it disables the functionality for TX Fail. */
	if (retry > 0) {
		txfail_config.max_tx_retry = retry;
		txfail_config.bcn_timeout = 0;	/* 0 : disable tx fail from beacon */
	}
	txfail_config.version = AIBSS_TXFAIL_CONFIG_VER_0;
	txfail_config.len = sizeof(txfail_config);

	err = wldev_iovar_setbuf(dev, "aibss_txfail_config", (void *) &txfail_config,
		sizeof(aibss_txfail_config_t), smbuf, WLC_IOCTL_SMLEN, NULL);
	WL_DBG(("retry=%d, pid=%d, err=%d\n", retry, pid, err));

	return ((err == 0)?total_len:err);
}

static int wl_android_get_ibss_peer_info(struct net_device *dev, char *command,
	int total_len, bool bAll)
{
	int error;
	int bytes_written = 0;
	void *buf = NULL;
	bss_peer_list_info_t peer_list_info;
	bss_peer_info_t *peer_info;
	int i;
	bool found = false;
	struct ether_addr mac_ea;
	char *str = command;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	WL_DBG(("get ibss peer info(%s)\n", bAll?"true":"false"));

	if (!bAll) {
		if (bcmstrtok(&str, " ", NULL) == NULL) {
			WL_ERR(("invalid command\n"));
			return -1;
		}

		if (!str || !bcm_ether_atoe(str, &mac_ea)) {
			WL_ERR(("invalid MAC address\n"));
			return -1;
		}
	}

	if ((buf = MALLOC(cfg->osh, WLC_IOCTL_MAXLEN)) == NULL) {
		WL_ERR(("kmalloc failed\n"));
		return -1;
	}

	error = wldev_iovar_getbuf(dev, "bss_peer_info", NULL, 0, buf, WLC_IOCTL_MAXLEN, NULL);
	if (unlikely(error)) {
		WL_ERR(("could not get ibss peer info (%d)\n", error));
		MFREE(cfg->osh, buf, WLC_IOCTL_MAXLEN);
		return -1;
	}

	memcpy(&peer_list_info, buf, sizeof(peer_list_info));
	peer_list_info.version = htod16(peer_list_info.version);
	peer_list_info.bss_peer_info_len = htod16(peer_list_info.bss_peer_info_len);
	peer_list_info.count = htod32(peer_list_info.count);

	WL_DBG(("ver:%d, len:%d, count:%d\n", peer_list_info.version,
		peer_list_info.bss_peer_info_len, peer_list_info.count));

	if (peer_list_info.count > 0) {
		if (bAll)
			bytes_written += snprintf(&command[bytes_written], total_len, "%u ",
				peer_list_info.count);

		peer_info = (bss_peer_info_t *) ((char *)buf + BSS_PEER_LIST_INFO_FIXED_LEN);

		for (i = 0; i < peer_list_info.count; i++) {

			WL_DBG(("index:%d rssi:%d, tx:%u, rx:%u\n", i, peer_info->rssi,
				peer_info->tx_rate, peer_info->rx_rate));

			if (!bAll &&
				memcmp(&mac_ea, &peer_info->ea, sizeof(struct ether_addr)) == 0) {
				found = true;
			}

			if (bAll || found) {
				bytes_written += snprintf(&command[bytes_written],
					total_len - bytes_written,
					MACF" %u %d ", ETHER_TO_MACF(peer_info->ea),
					peer_info->tx_rate/1000, peer_info->rssi);
				if (bytes_written >= total_len) {
					WL_ERR(("wl_android_get_ibss_peer_info: Insufficient"
						" memory, %d bytes\n",
						total_len));
					bytes_written = -1;
					break;
				}
			}

			if (found)
				break;

			peer_info = (bss_peer_info_t *)((char *)peer_info+sizeof(bss_peer_info_t));
		}
	}
	else {
		WL_ERR(("could not get ibss peer info : no item\n"));
	}
	WL_DBG(("command(%u):%s\n", total_len, command));
	WL_DBG(("bytes_written:%d\n", bytes_written));

	MFREE(cfg->osh, buf, WLC_IOCTL_MAXLEN);
	return bytes_written;
}

int wl_android_set_ibss_routetable(struct net_device *dev, char *command)
{

	char *pcmd = command;
	char *str = NULL;
	ibss_route_tbl_t *route_tbl = NULL;
	char *ioctl_buf = NULL;
	s32 err = BCME_OK;
	uint32 route_tbl_len;
	uint32 entries;
	char *endptr;
	uint32 i = 0;
	struct ipv4_addr  dipaddr;
	struct ether_addr ea;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	route_tbl_len = sizeof(ibss_route_tbl_t) +
		(MAX_IBSS_ROUTE_TBL_ENTRY - 1) * sizeof(ibss_route_entry_t);
	route_tbl = (ibss_route_tbl_t *)MALLOCZ(cfg->osh, route_tbl_len);
	if (!route_tbl) {
		WL_ERR(("Route TBL alloc failed\n"));
		return -ENOMEM;
	}
	ioctl_buf = (char *)MALLOCZ(cfg->osh, WLC_IOCTL_MEDLEN);
	if (!ioctl_buf) {
		WL_ERR(("ioctl memory alloc failed\n"));
		if (route_tbl) {
			MFREE(cfg->osh, route_tbl, route_tbl_len);
		}
		return -ENOMEM;
	}
	bzero(ioctl_buf, WLC_IOCTL_MEDLEN);

	/* drop command */
	str = bcmstrtok(&pcmd, " ", NULL);

	/* get count */
	str = bcmstrtok(&pcmd, " ",  NULL);
	if (!str) {
		WL_ERR(("Invalid number parameter %s\n", str));
		err = -EINVAL;
		goto exit;
	}
	entries = bcm_strtoul(str, &endptr, 0);
	if (*endptr != '\0') {
		WL_ERR(("Invalid number parameter %s\n", str));
		err = -EINVAL;
		goto exit;
	}
	if (entries > MAX_IBSS_ROUTE_TBL_ENTRY) {
		WL_ERR(("Invalid entries number %u\n", entries));
		err = -EINVAL;
		goto exit;
	}

	WL_INFORM(("Routing table count:%u\n", entries));
	route_tbl->num_entry = entries;

	for (i = 0; i < entries; i++) {
		str = bcmstrtok(&pcmd, " ", NULL);
		if (!str || !bcm_atoipv4(str, &dipaddr)) {
			WL_ERR(("Invalid ip string %s\n", str));
			err = -EINVAL;
			goto exit;
		}

		str = bcmstrtok(&pcmd, " ", NULL);
		if (!str || !bcm_ether_atoe(str, &ea)) {
			WL_ERR(("Invalid ethernet string %s\n", str));
			err = -EINVAL;
			goto exit;
		}
		bcopy(&dipaddr, &route_tbl->route_entry[i].ipv4_addr, IPV4_ADDR_LEN);
		bcopy(&ea, &route_tbl->route_entry[i].nexthop, ETHER_ADDR_LEN);
	}

	route_tbl_len = sizeof(ibss_route_tbl_t) +
		((!entries?0:(entries - 1)) * sizeof(ibss_route_entry_t));
	err = wldev_iovar_setbuf(dev, "ibss_route_tbl",
		route_tbl, route_tbl_len, ioctl_buf, WLC_IOCTL_MEDLEN, NULL);
	if (err != BCME_OK) {
		WL_ERR(("Fail to set iovar %d\n", err));
		err = -EINVAL;
	}

exit:
	if (route_tbl) {
		MFREE(cfg->osh, route_tbl, sizeof(ibss_route_tbl_t) +
			(MAX_IBSS_ROUTE_TBL_ENTRY - 1) * sizeof(ibss_route_entry_t));
	}
	if (ioctl_buf) {
		MFREE(cfg->osh, ioctl_buf, WLC_IOCTL_MEDLEN);
	}
	return err;

}

int
wl_android_set_ibss_ampdu(struct net_device *dev, char *command, int total_len)
{
	char *pcmd = command;
	char *str = NULL, *endptr = NULL;
	struct ampdu_aggr aggr;
	char smbuf[WLC_IOCTL_SMLEN];
	int idx;
	int err = 0;
	int wme_AC2PRIO[AC_COUNT][2] = {
		{PRIO_8021D_VO, PRIO_8021D_NC},		/* AC_VO - 3 */
		{PRIO_8021D_CL, PRIO_8021D_VI},		/* AC_VI - 2 */
		{PRIO_8021D_BK, PRIO_8021D_NONE},	/* AC_BK - 1 */
		{PRIO_8021D_BE, PRIO_8021D_EE}};	/* AC_BE - 0 */

	WL_DBG(("set ibss ampdu:%s\n", command));

	bzero(&aggr, sizeof(aggr));
	/* Cofigure all priorities */
	aggr.conf_TID_bmap = NBITMASK(NUMPRIO);

	/* acquire parameters */
	/* drop command */
	str = bcmstrtok(&pcmd, " ", NULL);

	for (idx = 0; idx < AC_COUNT; idx++) {
		bool on;
		str = bcmstrtok(&pcmd, " ", NULL);
		if (!str) {
			WL_ERR(("Invalid parameter : %s\n", pcmd));
			return -EINVAL;
		}
		on = bcm_strtoul(str, &endptr, 0) ? TRUE : FALSE;
		if (*endptr != '\0') {
			WL_ERR(("Invalid number format %s\n", str));
			return -EINVAL;
		}
		if (on) {
			setbit(&aggr.enab_TID_bmap, wme_AC2PRIO[idx][0]);
			setbit(&aggr.enab_TID_bmap, wme_AC2PRIO[idx][1]);
		}
	}

	err = wldev_iovar_setbuf(dev, "ampdu_txaggr", (void *)&aggr,
	sizeof(aggr), smbuf, WLC_IOCTL_SMLEN, NULL);

	return ((err == 0) ? total_len : err);
}

int wl_android_set_ibss_antenna(struct net_device *dev, char *command, int total_len)
{
	char *pcmd = command;
	char *str = NULL;
	int txchain, rxchain;
	int err = 0;

	WL_DBG(("set ibss antenna:%s\n", command));

	/* acquire parameters */
	/* drop command */
	str = bcmstrtok(&pcmd, " ", NULL);

	/* TX chain */
	str = bcmstrtok(&pcmd, " ", NULL);
	if (!str) {
		WL_ERR(("Invalid parameter : %s\n", pcmd));
		return -EINVAL;
	}
	txchain = bcm_atoi(str);

	/* RX chain */
	str = bcmstrtok(&pcmd, " ", NULL);
	if (!str) {
		WL_ERR(("Invalid parameter : %s\n", pcmd));
		return -EINVAL;
	}
	rxchain = bcm_atoi(str);

	err = wldev_iovar_setint(dev, "txchain", txchain);
	if (err != 0)
		return err;
	err = wldev_iovar_setint(dev, "rxchain", rxchain);
	return ((err == 0)?total_len:err);
}
#endif /* WLAIBSS */

int wl_keep_alive_set(struct net_device *dev, char* extra)
{
	wl_mkeep_alive_pkt_t	mkeep_alive_pkt;
	int ret;
	uint period_msec = 0;
	char *buf;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	if (extra == NULL) {
		 DHD_ERROR(("wl_keep_alive_set: extra is NULL\n"));
		 return -1;
	}
	if (sscanf(extra, "%d", &period_msec) != 1) {
		 DHD_ERROR(("wl_keep_alive_set: sscanf error. check period_msec value\n"));
		 return -EINVAL;
	}
	DHD_ERROR(("wl_keep_alive_set: period_msec is %d\n", period_msec));

	bzero(&mkeep_alive_pkt, sizeof(wl_mkeep_alive_pkt_t));

	mkeep_alive_pkt.period_msec = period_msec;
	mkeep_alive_pkt.version = htod16(WL_MKEEP_ALIVE_VERSION);
	mkeep_alive_pkt.length = htod16(WL_MKEEP_ALIVE_FIXED_LEN);

	/* Setup keep alive zero for null packet generation */
	mkeep_alive_pkt.keep_alive_id = 0;
	mkeep_alive_pkt.len_bytes = 0;

	buf = (char *)MALLOC(cfg->osh, WLC_IOCTL_SMLEN);
	if (!buf) {
		DHD_ERROR(("wl_keep_alive_set: buffer alloc failed\n"));
		return BCME_NOMEM;
	}
	ret = wldev_iovar_setbuf(dev, "mkeep_alive", (char *)&mkeep_alive_pkt,
			WL_MKEEP_ALIVE_FIXED_LEN, buf, WLC_IOCTL_SMLEN, NULL);
	if (ret < 0)
		DHD_ERROR(("wl_keep_alive_set:keep_alive set failed:%d\n", ret));
	else
		DHD_TRACE(("wl_keep_alive_set: keep_alive set ok\n"));
	MFREE(cfg->osh, buf, WLC_IOCTL_SMLEN);
	return ret;
}
#ifdef P2PRESP_WFDIE_SRC
static int wl_android_get_wfdie_resp(struct net_device *dev, char *command, int total_len)
{
	int error = 0;
	int bytes_written = 0;
	int only_resp_wfdsrc = 0;

	error = wldev_iovar_getint(dev, "p2p_only_resp_wfdsrc", &only_resp_wfdsrc);
	if (error) {
		DHD_ERROR(("wl_android_get_wfdie_resp: Failed to get the mode"
			" for only_resp_wfdsrc, error = %d\n",
			error));
		return -1;
	}

	bytes_written = snprintf(command, total_len, "%s %d",
		CMD_P2P_GET_WFDIE_RESP, only_resp_wfdsrc);

	return bytes_written;
}

static int wl_android_set_wfdie_resp(struct net_device *dev, int only_resp_wfdsrc)
{
	int error = 0;

	error = wldev_iovar_setint(dev, "p2p_only_resp_wfdsrc", only_resp_wfdsrc);
	if (error) {
		DHD_ERROR(("wl_android_set_wfdie_resp: Failed to set only_resp_wfdsrc %d,"
			" error = %d\n",
			only_resp_wfdsrc, error));
		return -1;
	}

	return 0;
}
#endif /* P2PRESP_WFDIE_SRC */

#ifdef BT_WIFI_HANDOVER
static int
wl_tbow_teardown(struct net_device *dev)
{
	int err = BCME_OK;
	char buf[WLC_IOCTL_SMLEN];
	tbow_setup_netinfo_t netinfo;
	bzero(&netinfo, sizeof(netinfo));
	netinfo.opmode = TBOW_HO_MODE_TEARDOWN;

	err = wldev_iovar_setbuf_bsscfg(dev, "tbow_doho", &netinfo,
			sizeof(tbow_setup_netinfo_t), buf, WLC_IOCTL_SMLEN, 0, NULL);
	if (err < 0) {
		WL_ERR(("tbow_doho iovar error %d\n", err));
		return err;
	}
	return err;
}
#endif /* BT_WIFI_HANOVER */

#ifdef SET_RPS_CPUS
static int
wl_android_set_rps_cpus(struct net_device *dev, char *command)
{
	int error, enable;

	enable = command[strlen(CMD_RPSMODE) + 1] - '0';
	error = dhd_rps_cpus_enable(dev, enable);

#if defined(DHDTCPACK_SUPPRESS) && defined(BCMPCIE) && defined(WL_CFG80211)
	if (!error) {
		void *dhdp = wl_cfg80211_get_dhdp(net);
		if (enable) {
			DHD_TRACE(("wl_android_set_rps_cpus: set ack suppress."
			" TCPACK_SUP_HOLD.\n"));
			dhd_tcpack_suppress_set(dhdp, TCPACK_SUP_HOLD);
		} else {
			DHD_TRACE(("wl_android_set_rps_cpus: clear ack suppress.\n"));
			dhd_tcpack_suppress_set(dhdp, TCPACK_SUP_OFF);
		}
	}
#endif /* DHDTCPACK_SUPPRESS && BCMPCIE && WL_CFG80211 */

	return error;
}
#endif /* SET_RPS_CPUS */

static int wl_android_get_link_status(struct net_device *dev, char *command,
	int total_len)
{
	int bytes_written, error, result = 0, single_stream, stf = -1, i, nss = 0, mcs_map;
	uint32 rspec;
	uint encode, txexp;
	wl_bss_info_t *bi;
	int datalen;
	char buf[sizeof(uint32) + sizeof(wl_bss_info_t)];

	datalen = sizeof(uint32) + sizeof(wl_bss_info_t);

	bzero(buf, datalen);
	/* get BSS information */
	*(u32 *) buf = htod32(datalen);
	error = wldev_ioctl_get(dev, WLC_GET_BSS_INFO, (void *)buf, datalen);
	if (unlikely(error)) {
		WL_ERR(("Could not get bss info %d\n", error));
		return -1;
	}

	bi = (wl_bss_info_t*) (buf + sizeof(uint32));

	for (i = 0; i < ETHER_ADDR_LEN; i++) {
		if (bi->BSSID.octet[i] > 0) {
			break;
		}
	}

	if (i == ETHER_ADDR_LEN) {
		WL_DBG(("No BSSID\n"));
		return -1;
	}

	/* check VHT capability at beacon */
	if (bi->vht_cap) {
		if (CHSPEC_IS5G(bi->chanspec)) {
			result |= WL_ANDROID_LINK_AP_VHT_SUPPORT;
		}
	}

	/* get a rspec (radio spectrum) rate */
	error = wldev_iovar_getint(dev, "nrate", &rspec);
	if (unlikely(error) || rspec == 0) {
		WL_ERR(("get link status error (%d)\n", error));
		return -1;
	}

	encode = (rspec & WL_RSPEC_ENCODING_MASK);
	txexp = (rspec & WL_RSPEC_TXEXP_MASK) >> WL_RSPEC_TXEXP_SHIFT;

	switch (encode) {
	case WL_RSPEC_ENCODE_HT:
		/* check Rx MCS Map for HT */
		for (i = 0; i < MAX_STREAMS_SUPPORTED; i++) {
			int8 bitmap = 0xFF;
			if (i == MAX_STREAMS_SUPPORTED-1) {
				bitmap = 0x7F;
			}
			if (bi->basic_mcs[i] & bitmap) {
				nss++;
			}
		}
		break;
	case WL_RSPEC_ENCODE_VHT:
		/* check Rx MCS Map for VHT */
		for (i = 1; i <= VHT_CAP_MCS_MAP_NSS_MAX; i++) {
			mcs_map = VHT_MCS_MAP_GET_MCS_PER_SS(i, dtoh16(bi->vht_rxmcsmap));
			if (mcs_map != VHT_CAP_MCS_MAP_NONE) {
				nss++;
			}
		}
		break;
	}

	/* check MIMO capability with nss in beacon */
	if (nss > 1) {
		result |= WL_ANDROID_LINK_AP_MIMO_SUPPORT;
	}

	single_stream = (encode == WL_RSPEC_ENCODE_RATE) ||
		((encode == WL_RSPEC_ENCODE_HT) && (rspec & WL_RSPEC_HT_MCS_MASK) < 8) ||
		((encode == WL_RSPEC_ENCODE_VHT) &&
		((rspec & WL_RSPEC_VHT_NSS_MASK) >> WL_RSPEC_VHT_NSS_SHIFT) == 1);

	if (txexp == 0) {
		if ((rspec & WL_RSPEC_STBC) && single_stream) {
			stf = OLD_NRATE_STF_STBC;
		} else {
			stf = (single_stream) ? OLD_NRATE_STF_SISO : OLD_NRATE_STF_SDM;
		}
	} else if (txexp == 1 && single_stream) {
		stf = OLD_NRATE_STF_CDD;
	}

	/* check 11ac (VHT) */
	if (encode == WL_RSPEC_ENCODE_VHT) {
		if (CHSPEC_IS5G(bi->chanspec)) {
			result |= WL_ANDROID_LINK_VHT;
		}
	}

	/* check MIMO */
	if (result & WL_ANDROID_LINK_AP_MIMO_SUPPORT) {
		switch (stf) {
		case OLD_NRATE_STF_SISO:
			break;
		case OLD_NRATE_STF_CDD:
		case OLD_NRATE_STF_STBC:
			result |= WL_ANDROID_LINK_MIMO;
			break;
		case OLD_NRATE_STF_SDM:
			if (!single_stream) {
				result |= WL_ANDROID_LINK_MIMO;
			}
			break;
		}
	}

	WL_DBG(("%s:result=%d, stf=%d, single_stream=%d, mcs map=%d\n",
		__FUNCTION__, result, stf, single_stream, nss));

	bytes_written = snprintf(command, total_len, "%s %d", CMD_GET_LINK_STATUS, result);

	return bytes_written;
}

#ifdef P2P_LISTEN_OFFLOADING

s32
wl_cfg80211_p2plo_deinit(struct bcm_cfg80211 *cfg)
{
	s32 bssidx;
	int ret = 0;
	int p2plo_pause = 0;
	dhd_pub_t *dhd = NULL;
	if (!cfg || !cfg->p2p) {
		WL_ERR(("Wl %p or cfg->p2p %p is null\n",
			cfg, cfg ? cfg->p2p : 0));
		return 0;
	}

	dhd =  (dhd_pub_t *)(cfg->pub);
	if (!dhd->up) {
		WL_ERR(("bus is already down\n"));
		return ret;
	}

	bssidx = wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE);
	ret = wldev_iovar_setbuf_bsscfg(bcmcfg_to_prmry_ndev(cfg),
			"p2po_stop", (void*)&p2plo_pause, sizeof(p2plo_pause),
			cfg->ioctl_buf, WLC_IOCTL_SMLEN, bssidx, &cfg->ioctl_buf_sync);
	if (ret < 0) {
		WL_ERR(("p2po_stop Failed :%d\n", ret));
	}

	return  ret;
}
s32
wl_cfg80211_p2plo_listen_start(struct net_device *dev, u8 *buf, int len)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	s32 bssidx = wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE);
	wl_p2plo_listen_t p2plo_listen;
	int ret = -EAGAIN;
	int channel = 0;
	int period = 0;
	int interval = 0;
	int count = 0;
	if (WL_DRV_STATUS_SENDING_AF_FRM_EXT(cfg)) {
		WL_ERR(("Sending Action Frames. Try it again.\n"));
		goto exit;
	}

	if (wl_get_drv_status_all(cfg, SCANNING)) {
		WL_ERR(("Scanning already\n"));
		goto exit;
	}

	if (wl_get_drv_status(cfg, SCAN_ABORTING, dev)) {
		WL_ERR(("Scanning being aborted\n"));
		goto exit;
	}

	if (wl_get_p2p_status(cfg, DISC_IN_PROGRESS)) {
		WL_ERR(("p2p listen offloading already running\n"));
		goto exit;
	}

	/* Just in case if it is not enabled */
	if ((ret = wl_cfgp2p_enable_discovery(cfg, dev, NULL, 0)) < 0) {
		WL_ERR(("cfgp2p_enable discovery failed"));
		goto exit;
	}

	bzero(&p2plo_listen, sizeof(wl_p2plo_listen_t));

	if (len) {
		sscanf(buf, " %10d %10d %10d %10d", &channel, &period, &interval, &count);
		if ((channel == 0) || (period == 0) ||
				(interval == 0) || (count == 0)) {
			WL_ERR(("Wrong argument %d/%d/%d/%d \n",
				channel, period, interval, count));
			ret = -EAGAIN;
			goto exit;
		}
		p2plo_listen.period = period;
		p2plo_listen.interval = interval;
		p2plo_listen.count = count;

		WL_ERR(("channel:%d period:%d, interval:%d count:%d\n",
			channel, period, interval, count));
	} else {
		WL_ERR(("Argument len is wrong.\n"));
		ret = -EAGAIN;
		goto exit;
	}

	if ((ret = wldev_iovar_setbuf_bsscfg(dev, "p2po_listen_channel", (void*)&channel,
			sizeof(channel), cfg->ioctl_buf, WLC_IOCTL_SMLEN,
			bssidx, &cfg->ioctl_buf_sync)) < 0) {
		WL_ERR(("p2po_listen_channel Failed :%d\n", ret));
		goto exit;
	}

	if ((ret = wldev_iovar_setbuf_bsscfg(dev, "p2po_listen", (void*)&p2plo_listen,
			sizeof(wl_p2plo_listen_t), cfg->ioctl_buf, WLC_IOCTL_SMLEN,
			bssidx, &cfg->ioctl_buf_sync)) < 0) {
		WL_ERR(("p2po_listen Failed :%d\n", ret));
		goto exit;
	}

	wl_set_p2p_status(cfg, DISC_IN_PROGRESS);
exit :
	return ret;
}
s32
wl_cfg80211_p2plo_listen_stop(struct net_device *dev)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	s32 bssidx = wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE);
	int ret = -EAGAIN;

	if ((ret = wldev_iovar_setbuf_bsscfg(dev, "p2po_stop", NULL,
			0, cfg->ioctl_buf, WLC_IOCTL_SMLEN,
			bssidx, &cfg->ioctl_buf_sync)) < 0) {
		WL_ERR(("p2po_stop Failed :%d\n", ret));
		goto exit;
	}

exit:
	return ret;
}

s32
wl_cfg80211_p2plo_offload(struct net_device *dev, char *cmd, char* buf, int len)
{
	int ret = 0;

	WL_ERR(("Entry cmd:%s arg_len:%d \n", cmd, len));

	if (strncmp(cmd, "P2P_LO_START", strlen("P2P_LO_START")) == 0) {
		ret = wl_cfg80211_p2plo_listen_start(dev, buf, len);
	} else if (strncmp(cmd, "P2P_LO_STOP", strlen("P2P_LO_STOP")) == 0) {
		ret = wl_cfg80211_p2plo_listen_stop(dev);
	} else {
		WL_ERR(("Request for Unsupported CMD:%s \n", buf));
		ret = -EINVAL;
	}
	return ret;
}
void
wl_cfg80211_cancel_p2plo(struct bcm_cfg80211 *cfg)
{
	struct wireless_dev *wdev;
	if (!cfg) {
		return;
	}

	wdev = bcmcfg_to_p2p_wdev(cfg);

	if (wl_get_p2p_status(cfg, DISC_IN_PROGRESS)) {
		WL_INFORM_MEM(("P2P_FIND: Discovery offload is already in progress."
					"it aborted\n"));
		wl_clr_p2p_status(cfg, DISC_IN_PROGRESS);
		if (wdev != NULL) {
#if defined(WL_CFG80211_P2P_DEV_IF)
			cfg80211_remain_on_channel_expired(wdev,
					cfg->last_roc_id,
					&cfg->remain_on_chan, GFP_KERNEL);
#else
			cfg80211_remain_on_channel_expired(wdev,
					cfg->last_roc_id,
					&cfg->remain_on_chan,
					cfg->remain_on_chan_type, GFP_KERNEL);
#endif /* WL_CFG80211_P2P_DEV_IF */
		}
		wl_cfg80211_p2plo_deinit(cfg);
	}
}
#endif /* P2P_LISTEN_OFFLOADING */

#ifdef WL_MURX
int
wl_android_murx_bfe_cap(struct net_device *dev, int val)
{
	int err = BCME_OK;
	int iface_count = wl_cfg80211_iface_count(dev);
	struct ether_addr bssid;
	wl_reassoc_params_t params;

	if (iface_count > 1) {
		WL_ERR(("murx_bfe_cap change is not allowed when "
				"there are multiple interfaces\n"));
		return -EINVAL;
	}
	/* Now there is only single interface */
	err = wldev_iovar_setint(dev, "murx_bfe_cap", val);
	if (unlikely(err)) {
		WL_ERR(("Failed to set murx_bfe_cap IOVAR to %d,"
				"error %d\n", val, err));
		return err;
	}

	/* If successful intiate a reassoc */
	bzero(&bssid, ETHER_ADDR_LEN);
	if ((err = wldev_ioctl_get(dev, WLC_GET_BSSID, &bssid, ETHER_ADDR_LEN)) < 0) {
		WL_ERR(("Failed to get bssid, error=%d\n", err));
		return err;
	}

	bzero(&params, sizeof(wl_reassoc_params_t));
	memcpy(&params.bssid, &bssid, ETHER_ADDR_LEN);

	if ((err = wldev_ioctl_set(dev, WLC_REASSOC, &params,
		sizeof(wl_reassoc_params_t))) < 0) {
		WL_ERR(("reassoc failed err:%d \n", err));
	} else {
		WL_DBG(("reassoc issued successfully\n"));
	}

	return err;
}
#endif /* WL_MURX */

#ifdef SUPPORT_RSSI_SUM_REPORT
int
wl_android_get_rssi_per_ant(struct net_device *dev, char *command, int total_len)
{
	wl_rssi_ant_mimo_t rssi_ant_mimo;
	char *ifname = NULL;
	char *peer_mac = NULL;
	char *mimo_cmd = "mimo";
	char *pos, *token;
	int err = BCME_OK;
	int bytes_written = 0;
	bool mimo_rssi = FALSE;

	bzero(&rssi_ant_mimo, sizeof(wl_rssi_ant_mimo_t));
	/*
	 * STA I/F: DRIVER GET_RSSI_PER_ANT <ifname> <mimo>
	 * AP/GO I/F: DRIVER GET_RSSI_PER_ANT <ifname> <Peer MAC addr> <mimo>
	 */
	pos = command;

	/* drop command */
	token = bcmstrtok(&pos, " ", NULL);

	/* get the interface name */
	token = bcmstrtok(&pos, " ", NULL);
	if (!token) {
		WL_ERR(("Invalid arguments\n"));
		return -EINVAL;
	}
	ifname = token;

	/* Optional: Check the MIMO RSSI mode or peer MAC address */
	token = bcmstrtok(&pos, " ", NULL);
	if (token) {
		/* Check the MIMO RSSI mode */
		if (strncmp(token, mimo_cmd, strlen(mimo_cmd)) == 0) {
			mimo_rssi = TRUE;
		} else {
			peer_mac = token;
		}
	}

	/* Optional: Check the MIMO RSSI mode - RSSI sum across antennas */
	token = bcmstrtok(&pos, " ", NULL);
	if (token && strncmp(token, mimo_cmd, strlen(mimo_cmd)) == 0) {
		mimo_rssi = TRUE;
	}

	err = wl_get_rssi_per_ant(dev, ifname, peer_mac, &rssi_ant_mimo);
	if (unlikely(err)) {
		WL_ERR(("Failed to get RSSI info, err=%d\n", err));
		return err;
	}

	/* Parse the results */
	WL_DBG(("ifname %s, version %d, count %d, mimo rssi %d\n",
		ifname, rssi_ant_mimo.version, rssi_ant_mimo.count, mimo_rssi));
	if (mimo_rssi) {
		WL_DBG(("MIMO RSSI: %d\n", rssi_ant_mimo.rssi_sum));
		bytes_written = snprintf(command, total_len, "%s MIMO %d",
			CMD_GET_RSSI_PER_ANT, rssi_ant_mimo.rssi_sum);
	} else {
		int cnt;
		bytes_written = snprintf(command, total_len, "%s PER_ANT ", CMD_GET_RSSI_PER_ANT);
		for (cnt = 0; cnt < rssi_ant_mimo.count; cnt++) {
			WL_DBG(("RSSI[%d]: %d\n", cnt, rssi_ant_mimo.rssi_ant[cnt]));
			bytes_written = snprintf(command, total_len, "%d ",
				rssi_ant_mimo.rssi_ant[cnt]);
		}
	}

	return bytes_written;
}

int
wl_android_set_rssi_logging(struct net_device *dev, char *command, int total_len)
{
	rssilog_set_param_t set_param;
	char *pos, *token;
	int err = BCME_OK;

	bzero(&set_param, sizeof(rssilog_set_param_t));
	/*
	 * DRIVER SET_RSSI_LOGGING <enable/disable> <RSSI Threshold> <Time Threshold>
	 */
	pos = command;

	/* drop command */
	token = bcmstrtok(&pos, " ", NULL);

	/* enable/disable */
	token = bcmstrtok(&pos, " ", NULL);
	if (!token) {
		WL_ERR(("Invalid arguments\n"));
		return -EINVAL;
	}
	set_param.enable = bcm_atoi(token);

	/* RSSI Threshold */
	token = bcmstrtok(&pos, " ", NULL);
	if (!token) {
		WL_ERR(("Invalid arguments\n"));
		return -EINVAL;
	}
	set_param.rssi_threshold = bcm_atoi(token);

	/* Time Threshold */
	token = bcmstrtok(&pos, " ", NULL);
	if (!token) {
		WL_ERR(("Invalid arguments\n"));
		return -EINVAL;
	}
	set_param.time_threshold = bcm_atoi(token);

	WL_DBG(("enable %d, RSSI threshold %d, Time threshold %d\n", set_param.enable,
		set_param.rssi_threshold, set_param.time_threshold));

	err = wl_set_rssi_logging(dev, (void *)&set_param);
	if (unlikely(err)) {
		WL_ERR(("Failed to configure RSSI logging: enable %d, RSSI Threshold %d,"
			" Time Threshold %d\n", set_param.enable, set_param.rssi_threshold,
			set_param.time_threshold));
	}

	return err;
}

int
wl_android_get_rssi_logging(struct net_device *dev, char *command, int total_len)
{
	rssilog_get_param_t get_param;
	int err = BCME_OK;
	int bytes_written = 0;

	err = wl_get_rssi_logging(dev, (void *)&get_param);
	if (unlikely(err)) {
		WL_ERR(("Failed to get RSSI logging info\n"));
		return BCME_ERROR;
	}

	WL_DBG(("report_count %d, enable %d, rssi_threshold %d, time_threshold %d\n",
		get_param.report_count, get_param.enable, get_param.rssi_threshold,
		get_param.time_threshold));

	/* Parse the parameter */
	if (!get_param.enable) {
		WL_DBG(("RSSI LOGGING: Feature is disables\n"));
		bytes_written = snprintf(command, total_len,
			"%s FEATURE DISABLED\n", CMD_GET_RSSI_LOGGING);
	} else if (get_param.enable &
		(RSSILOG_FLAG_FEATURE_SW | RSSILOG_FLAG_REPORT_READY)) {
		if (!get_param.report_count) {
			WL_DBG(("[PASS] RSSI difference across antennas is within"
				" threshold limits\n"));
			bytes_written = snprintf(command, total_len, "%s PASS\n",
				CMD_GET_RSSI_LOGGING);
		} else {
			WL_DBG(("[FAIL] RSSI difference across antennas found "
				"to be greater than %3d dB\n", get_param.rssi_threshold));
			WL_DBG(("[FAIL] RSSI difference check have failed for "
				"%d out of %d times\n", get_param.report_count,
				get_param.time_threshold));
			WL_DBG(("[FAIL] RSSI difference is being monitored once "
				"per second, for a %d secs window\n", get_param.time_threshold));
			bytes_written = snprintf(command, total_len, "%s FAIL - RSSI Threshold "
				"%d dBm for %d out of %d times\n", CMD_GET_RSSI_LOGGING,
				get_param.rssi_threshold, get_param.report_count,
				get_param.time_threshold);
		}
	} else {
		WL_DBG(("[BUSY] Reprot is not ready\n"));
		bytes_written = snprintf(command, total_len, "%s BUSY - NOT READY\n",
			CMD_GET_RSSI_LOGGING);
	}

	return bytes_written;
}
#endif /* SUPPORT_RSSI_SUM_REPORT */

#ifdef SET_PCIE_IRQ_CPU_CORE
void
wl_android_set_irq_cpucore(struct net_device *net, int affinity_cmd)
{
	dhd_pub_t *dhdp = wl_cfg80211_get_dhdp(net);
	if (!dhdp) {
		WL_ERR(("dhd is NULL\n"));
		return;
	}

	if (affinity_cmd < PCIE_IRQ_AFFINITY_OFF || affinity_cmd > PCIE_IRQ_AFFINITY_LAST) {
		WL_ERR(("Wrong Affinity cmds:%d, %s\n", affinity_cmd, __FUNCTION__));
		return;
	}

	dhd_set_irq_cpucore(dhdp, affinity_cmd);
}
#endif /* SET_PCIE_IRQ_CPU_CORE */

#ifdef SUPPORT_LQCM
static int
wl_android_lqcm_enable(struct net_device *net, int lqcm_enable)
{
	int err = 0;

	err = wldev_iovar_setint(net, "lqcm", lqcm_enable);
	if (err != BCME_OK) {
		WL_ERR(("failed to set lqcm enable %d, error = %d\n", lqcm_enable, err));
		return -EIO;
	}
	return err;
}

static int
wl_android_get_lqcm_report(struct net_device *dev, char *command, int total_len)
{
	int bytes_written, err = 0;
	uint32 lqcm_report = 0;
	uint32 lqcm_enable, tx_lqcm_idx, rx_lqcm_idx;

	err = wldev_iovar_getint(dev, "lqcm", &lqcm_report);
	if (err != BCME_OK) {
		WL_ERR(("failed to get lqcm report, error = %d\n", err));
		return -EIO;
	}
	lqcm_enable = lqcm_report & LQCM_ENAB_MASK;
	tx_lqcm_idx = (lqcm_report & LQCM_TX_INDEX_MASK) >> LQCM_TX_INDEX_SHIFT;
	rx_lqcm_idx = (lqcm_report & LQCM_RX_INDEX_MASK) >> LQCM_RX_INDEX_SHIFT;

	WL_DBG(("lqcm report EN:%d, TX:%d, RX:%d\n", lqcm_enable, tx_lqcm_idx, rx_lqcm_idx));

	bytes_written = snprintf(command, total_len, "%s %d",
			CMD_GET_LQCM_REPORT, lqcm_report);

	return bytes_written;
}
#endif /* SUPPORT_LQCM */

int
wl_android_get_snr(struct net_device *dev, char *command, int total_len)
{
	int bytes_written, error = 0;
	s32 snr = 0;

	error = wldev_iovar_getint(dev, "snr", &snr);
	if (error) {
		DHD_ERROR(("wl_android_get_snr: Failed to get SNR %d, error = %d\n",
			snr, error));
		return -EIO;
	}

	bytes_written = snprintf(command, total_len, "snr %d", snr);
	DHD_INFO(("wl_android_get_snr: command result is %s\n", command));
	return bytes_written;
}

#ifdef SUPPORT_AP_HIGHER_BEACONRATE
int
wl_android_set_ap_beaconrate(struct net_device *dev, char *command)
{
	int rate = 0;
	char *pos, *token;
	char *ifname = NULL;
	int err = BCME_OK;

	/*
	 * DRIVER SET_AP_BEACONRATE <rate> <ifname>
	 */
	pos = command;

	/* drop command */
	token = bcmstrtok(&pos, " ", NULL);

	/* Rate */
	token = bcmstrtok(&pos, " ", NULL);
	if (!token)
		return -EINVAL;
	rate = bcm_atoi(token);

	/* get the interface name */
	token = bcmstrtok(&pos, " ", NULL);
	if (!token)
		return -EINVAL;
	ifname = token;

	WL_DBG(("rate %d, ifacename %s\n", rate, ifname));

	err = wl_set_ap_beacon_rate(dev, rate, ifname);
	if (unlikely(err)) {
		WL_ERR(("Failed to set ap beacon rate to %d, error = %d\n", rate, err));
	}

	return err;
}

int wl_android_get_ap_basicrate(struct net_device *dev, char *command, int total_len)
{
	char *pos, *token;
	char *ifname = NULL;
	int bytes_written = 0;
	/*
	 * DRIVER GET_AP_BASICRATE <ifname>
	 */
	pos = command;

	/* drop command */
	token = bcmstrtok(&pos, " ", NULL);

	/* get the interface name */
	token = bcmstrtok(&pos, " ", NULL);
	if (!token)
		return -EINVAL;
	ifname = token;

	WL_DBG(("ifacename %s\n", ifname));

	bytes_written = wl_get_ap_basic_rate(dev, command, ifname, total_len);
	if (bytes_written < 1) {
		WL_ERR(("Failed to get ap basic rate, error = %d\n", bytes_written));
		return -EPROTO;
	}

	return bytes_written;
}
#endif /* SUPPORT_AP_HIGHER_BEACONRATE */

#ifdef SUPPORT_AP_RADIO_PWRSAVE
int
wl_android_get_ap_rps(struct net_device *dev, char *command, int total_len)
{
	char *pos, *token;
	char *ifname = NULL;
	int bytes_written = 0;
	char name[IFNAMSIZ];
	/*
	 * DRIVER GET_AP_RPS <ifname>
	 */
	pos = command;

	/* drop command */
	token = bcmstrtok(&pos, " ", NULL);

	/* get the interface name */
	token = bcmstrtok(&pos, " ", NULL);
	if (!token)
		return -EINVAL;
	ifname = token;

	strlcpy(name, ifname, sizeof(name));
	WL_DBG(("ifacename %s\n", name));

	bytes_written = wl_get_ap_rps(dev, command, name, total_len);
	if (bytes_written < 1) {
		WL_ERR(("Failed to get rps, error = %d\n", bytes_written));
		return -EPROTO;
	}

	return bytes_written;

}

int
wl_android_set_ap_rps(struct net_device *dev, char *command, int total_len)
{
	int enable = 0;
	char *pos, *token;
	char *ifname = NULL;
	int err = BCME_OK;
	char name[IFNAMSIZ];

	/*
	 * DRIVER SET_AP_RPS <0/1> <ifname>
	 */
	pos = command;

	/* drop command */
	token = bcmstrtok(&pos, " ", NULL);

	/* Enable */
	token = bcmstrtok(&pos, " ", NULL);
	if (!token)
		return -EINVAL;
	enable = bcm_atoi(token);

	/* get the interface name */
	token = bcmstrtok(&pos, " ", NULL);
	if (!token)
		return -EINVAL;
	ifname = token;

	strlcpy(name, ifname, sizeof(name));
	WL_DBG(("enable %d, ifacename %s\n", enable, name));

	err = wl_set_ap_rps(dev, enable? TRUE: FALSE, name);
	if (unlikely(err)) {
		WL_ERR(("Failed to set rps, enable %d, error = %d\n", enable, err));
	}

	return err;
}

int
wl_android_set_ap_rps_params(struct net_device *dev, char *command, int total_len)
{
	ap_rps_info_t rps;
	char *pos, *token;
	char *ifname = NULL;
	int err = BCME_OK;
	char name[IFNAMSIZ];

	bzero(&rps, sizeof(rps));
	/*
	 * DRIVER SET_AP_RPS_PARAMS <pps> <level> <quiettime> <assoccheck> <ifname>
	 */
	pos = command;

	/* drop command */
	token = bcmstrtok(&pos, " ", NULL);

	/* pps */
	token = bcmstrtok(&pos, " ", NULL);
	if (!token)
		return -EINVAL;
	rps.pps = bcm_atoi(token);

	/* level */
	token = bcmstrtok(&pos, " ", NULL);
	if (!token)
		return -EINVAL;
	rps.level = bcm_atoi(token);

	/* quiettime */
	token = bcmstrtok(&pos, " ", NULL);
	if (!token)
		return -EINVAL;
	rps.quiet_time = bcm_atoi(token);

	/* sta assoc check */
	token = bcmstrtok(&pos, " ", NULL);
	if (!token)
		return -EINVAL;
	rps.sta_assoc_check = bcm_atoi(token);

	/* get the interface name */
	token = bcmstrtok(&pos, " ", NULL);
	if (!token)
		return -EINVAL;
	ifname = token;
	strlcpy(name, ifname, sizeof(name));

	WL_DBG(("pps %d, level %d, quiettime %d, sta_assoc_check %d, "
		"ifacename %s\n", rps.pps, rps.level, rps.quiet_time,
		rps.sta_assoc_check, name));

	err = wl_update_ap_rps_params(dev, &rps, name);
	if (unlikely(err)) {
		WL_ERR(("Failed to update rps, pps %d, level %d, quiettime %d, "
			"sta_assoc_check %d, err = %d\n", rps.pps, rps.level, rps.quiet_time,
			rps.sta_assoc_check, err));
	}

	return err;
}
#endif /* SUPPORT_AP_RADIO_PWRSAVE */

#if defined(DHD_HANG_SEND_UP_TEST)
void
wl_android_make_hang_with_reason(struct net_device *dev, const char *string_num)
{
	dhd_make_hang_with_reason(dev, string_num);
}
#endif /* DHD_HANG_SEND_UP_TEST */

#ifdef DHD_SEND_HANG_PRIVCMD_ERRORS
static void
wl_android_check_priv_cmd_errors(struct net_device *dev)
{
	dhd_pub_t *dhdp;
	int memdump_mode;

	if (!dev) {
		WL_ERR(("dev is NULL\n"));
		return;
	}

	dhdp = wl_cfg80211_get_dhdp(dev);
	if (!dhdp) {
		WL_ERR(("dhdp is NULL\n"));
		return;
	}

#ifdef DHD_FW_COREDUMP
	memdump_mode = dhdp->memdump_enabled;
#else
	/* Default enable if DHD doesn't support SOCRAM dump */
	memdump_mode = 1;
#endif /* DHD_FW_COREDUMP */

	if (report_hang_privcmd_err) {
		priv_cmd_errors++;
	} else {
		priv_cmd_errors = 0;
	}

	/* Trigger HANG event only if memdump mode is enabled
	 * due to customer's request
	 */
	if (memdump_mode == DUMP_MEMFILE_BUGON &&
		(priv_cmd_errors > NUMBER_SEQUENTIAL_PRIVCMD_ERRORS)) {
		WL_ERR(("Send HANG event due to sequential private cmd errors\n"));
		priv_cmd_errors = 0;
#ifdef DHD_FW_COREDUMP
		/* Take a SOCRAM dump */
		dhdp->memdump_type = DUMP_TYPE_SEQUENTIAL_PRIVCMD_ERROR;
		dhd_common_socram_dump(dhdp);
#endif /* DHD_FW_COREDUMP */
		/* Send the HANG event to upper layer */
		dhdp->hang_reason = HANG_REASON_SEQUENTIAL_PRIVCMD_ERROR;
		dhd_os_check_hang(dhdp, 0, -EREMOTEIO);
	}
}
#endif /* DHD_SEND_HANG_PRIVCMD_ERRORS */

#ifdef DHD_PKT_LOGGING
static int
wl_android_pktlog_filter_enable(struct net_device *dev, char *command, int total_len)
{
	int bytes_written = 0;
	dhd_pub_t *dhdp = wl_cfg80211_get_dhdp(dev);
	dhd_pktlog_filter_t *filter;
	int err = BCME_OK;

	if (!dhdp || !dhdp->pktlog) {
		DHD_PKT_LOG(("%s(): dhdp=%p pktlog=%p\n",
			__FUNCTION__, dhdp, (dhdp ? dhdp->pktlog : NULL)));
		return -EINVAL;
	}

	filter = dhdp->pktlog->pktlog_filter;

	err = dhd_pktlog_filter_enable(filter, PKTLOG_TXPKT_CASE, TRUE);
	err = dhd_pktlog_filter_enable(filter, PKTLOG_TXSTATUS_CASE, TRUE);
	err = dhd_pktlog_filter_enable(filter, PKTLOG_RXPKT_CASE, TRUE);

	if (err == BCME_OK) {
		bytes_written = snprintf(command, total_len, "OK");
		DHD_ERROR(("%s: pktlog filter enable success\n", __FUNCTION__));
	} else {
		DHD_ERROR(("%s: pktlog filter enable fail\n", __FUNCTION__));
		return BCME_ERROR;
	}

	return bytes_written;
}

static int
wl_android_pktlog_filter_disable(struct net_device *dev, char *command, int total_len)
{
	int bytes_written = 0;
	dhd_pub_t *dhdp = wl_cfg80211_get_dhdp(dev);
	dhd_pktlog_filter_t *filter;
	int err = BCME_OK;

	if (!dhdp || !dhdp->pktlog) {
		DHD_PKT_LOG(("%s(): dhdp=%p pktlog=%p\n",
			__FUNCTION__, dhdp, (dhdp ? dhdp->pktlog : NULL)));
		return -EINVAL;
	}

	filter = dhdp->pktlog->pktlog_filter;

	err = dhd_pktlog_filter_enable(filter, PKTLOG_TXPKT_CASE, FALSE);
	err = dhd_pktlog_filter_enable(filter, PKTLOG_TXSTATUS_CASE, FALSE);
	err = dhd_pktlog_filter_enable(filter, PKTLOG_RXPKT_CASE, FALSE);

	if (err == BCME_OK) {
		bytes_written = snprintf(command, total_len, "OK");
		DHD_ERROR(("%s: pktlog filter disable success\n", __FUNCTION__));
	} else {
		DHD_ERROR(("%s: pktlog filter disable fail\n", __FUNCTION__));
		return BCME_ERROR;
	}

	return bytes_written;
}

static int
wl_android_pktlog_filter_pattern_enable(struct net_device *dev, char *command, int total_len)
{
	int bytes_written = 0;
	dhd_pub_t *dhdp = wl_cfg80211_get_dhdp(dev);
	dhd_pktlog_filter_t *filter;
	int err = BCME_OK;

	if (!dhdp || !dhdp->pktlog) {
		DHD_PKT_LOG(("%s(): dhdp=%p pktlog=%p\n",
			__FUNCTION__, dhdp, (dhdp ? dhdp->pktlog : NULL)));
		return -EINVAL;
	}

	filter = dhdp->pktlog->pktlog_filter;

	if (strlen(CMD_PKTLOG_FILTER_PATTERN_ENABLE) + 1 > total_len) {
		return BCME_ERROR;
	}

	err = dhd_pktlog_filter_pattern_enable(filter,
			command + strlen(CMD_PKTLOG_FILTER_PATTERN_ENABLE) + 1, TRUE);

	if (err == BCME_OK) {
		bytes_written = snprintf(command, total_len, "OK");
		DHD_ERROR(("%s: pktlog filter pattern enable success\n", __FUNCTION__));
	} else {
		DHD_ERROR(("%s: pktlog filter pattern enable fail\n", __FUNCTION__));
		return BCME_ERROR;
	}

	return bytes_written;
}

static int
wl_android_pktlog_filter_pattern_disable(struct net_device *dev, char *command, int total_len)
{
	int bytes_written = 0;
	dhd_pub_t *dhdp = wl_cfg80211_get_dhdp(dev);
	dhd_pktlog_filter_t *filter;
	int err = BCME_OK;

	if (!dhdp || !dhdp->pktlog) {
		DHD_PKT_LOG(("%s(): dhdp=%p pktlog=%p\n",
			__FUNCTION__, dhdp, (dhdp ? dhdp->pktlog : NULL)));
		return -EINVAL;
	}

	filter = dhdp->pktlog->pktlog_filter;

	if (strlen(CMD_PKTLOG_FILTER_PATTERN_DISABLE) + 1 > total_len) {
		return BCME_ERROR;
	}

	err = dhd_pktlog_filter_pattern_enable(filter,
			command + strlen(CMD_PKTLOG_FILTER_PATTERN_DISABLE) + 1, FALSE);

	if (err == BCME_OK) {
		bytes_written = snprintf(command, total_len, "OK");
		DHD_ERROR(("%s: pktlog filter pattern disable success\n", __FUNCTION__));
	} else {
		DHD_ERROR(("%s: pktlog filter pattern disable fail\n", __FUNCTION__));
		return BCME_ERROR;
	}

	return bytes_written;
}

static int
wl_android_pktlog_filter_add(struct net_device *dev, char *command, int total_len)
{
	int bytes_written = 0;
	dhd_pub_t *dhdp = wl_cfg80211_get_dhdp(dev);
	dhd_pktlog_filter_t *filter;
	int err = BCME_OK;

	if (!dhdp || !dhdp->pktlog) {
		DHD_PKT_LOG(("%s(): dhdp=%p pktlog=%p\n",
			__FUNCTION__, dhdp, (dhdp ? dhdp->pktlog : NULL)));
		return -EINVAL;
	}

	filter = dhdp->pktlog->pktlog_filter;

	if (strlen(CMD_PKTLOG_FILTER_ADD) + 1 > total_len) {
		return BCME_ERROR;
	}

	err = dhd_pktlog_filter_add(filter, command + strlen(CMD_PKTLOG_FILTER_ADD) + 1);

	if (err == BCME_OK) {
		bytes_written = snprintf(command, total_len, "OK");
		DHD_ERROR(("%s: pktlog filter add success\n", __FUNCTION__));
	} else {
		DHD_ERROR(("%s: pktlog filter add fail\n", __FUNCTION__));
		return BCME_ERROR;
	}

	return bytes_written;
}

static int
wl_android_pktlog_filter_del(struct net_device *dev, char *command, int total_len)
{
	int bytes_written = 0;
	dhd_pub_t *dhdp = wl_cfg80211_get_dhdp(dev);
	dhd_pktlog_filter_t *filter;
	int err = BCME_OK;

	if (!dhdp || !dhdp->pktlog) {
		DHD_ERROR(("%s(): dhdp=%p pktlog=%p\n",
			__FUNCTION__, dhdp, (dhdp ? dhdp->pktlog : NULL)));
		return -EINVAL;
	}

	filter = dhdp->pktlog->pktlog_filter;

	if (strlen(CMD_PKTLOG_FILTER_DEL) + 1 > total_len) {
		DHD_PKT_LOG(("%s(): wrong cmd length %d found\n",
			__FUNCTION__, (int)strlen(CMD_PKTLOG_FILTER_DEL)));
		return BCME_ERROR;
	}

	err = dhd_pktlog_filter_del(filter, command + strlen(CMD_PKTLOG_FILTER_DEL) + 1);
	if (err == BCME_OK) {
		bytes_written = snprintf(command, total_len, "OK");
		DHD_ERROR(("%s: pktlog filter del success\n", __FUNCTION__));
	} else {
		DHD_ERROR(("%s: pktlog filter del fail\n", __FUNCTION__));
		return BCME_ERROR;
	}

	return bytes_written;
}

static int
wl_android_pktlog_filter_info(struct net_device *dev, char *command, int total_len)
{
	int bytes_written = 0;
	dhd_pub_t *dhdp = wl_cfg80211_get_dhdp(dev);
	dhd_pktlog_filter_t *filter;
	int err = BCME_OK;

	if (!dhdp || !dhdp->pktlog) {
		DHD_PKT_LOG(("%s(): dhdp=%p pktlog=%p\n",
			__FUNCTION__, dhdp, (dhdp ? dhdp->pktlog : NULL)));
		return -EINVAL;
	}

	filter = dhdp->pktlog->pktlog_filter;

	err = dhd_pktlog_filter_info(filter);

	if (err == BCME_OK) {
		bytes_written = snprintf(command, total_len, "OK");
		DHD_ERROR(("%s: pktlog filter info success\n", __FUNCTION__));
	} else {
		DHD_ERROR(("%s: pktlog filter info fail\n", __FUNCTION__));
		return BCME_ERROR;
	}

	return bytes_written;
}

static int
wl_android_pktlog_start(struct net_device *dev, char *command, int total_len)
{
	int bytes_written = 0;
	dhd_pub_t *dhdp = wl_cfg80211_get_dhdp(dev);

	if (!dhdp || !dhdp->pktlog) {
		DHD_PKT_LOG(("%s(): dhdp=%p pktlog=%p\n",
			__FUNCTION__, dhdp, (dhdp ? dhdp->pktlog : NULL)));
		return -EINVAL;
	}

	if (!dhdp->pktlog->pktlog_ring) {
		DHD_PKT_LOG(("%s(): pktlog_ring=%p\n",
			__FUNCTION__, dhdp->pktlog->pktlog_ring));
		return -EINVAL;
	}

	atomic_set(&dhdp->pktlog->pktlog_ring->start, TRUE);

	bytes_written = snprintf(command, total_len, "OK");

	DHD_ERROR(("%s: pktlog start success\n", __FUNCTION__));

	return bytes_written;
}

static int
wl_android_pktlog_stop(struct net_device *dev, char *command, int total_len)
{
	int bytes_written = 0;
	dhd_pub_t *dhdp = wl_cfg80211_get_dhdp(dev);

	if (!dhdp || !dhdp->pktlog) {
		DHD_PKT_LOG(("%s(): dhdp=%p pktlog=%p\n",
			__FUNCTION__, dhdp, (dhdp ? dhdp->pktlog : NULL)));
		return -EINVAL;
	}

	if (!dhdp->pktlog->pktlog_ring) {
		DHD_PKT_LOG(("%s(): _pktlog_ring=%p\n",
			__FUNCTION__, dhdp->pktlog->pktlog_ring));
		return -EINVAL;
	}

	atomic_set(&dhdp->pktlog->pktlog_ring->start, FALSE);

	bytes_written = snprintf(command, total_len, "OK");

	DHD_ERROR(("%s: pktlog stop success\n", __FUNCTION__));

	return bytes_written;
}

static int
wl_android_pktlog_filter_exist(struct net_device *dev, char *command, int total_len)
{
	int bytes_written = 0;
	dhd_pub_t *dhdp = wl_cfg80211_get_dhdp(dev);
	dhd_pktlog_filter_t *filter;
	uint32 id;
	bool exist = FALSE;

	if (!dhdp || !dhdp->pktlog) {
		DHD_PKT_LOG(("%s(): dhdp=%p pktlog=%p\n",
			__FUNCTION__, dhdp, (dhdp ? dhdp->pktlog : NULL)));
		return -EINVAL;
	}

	filter = dhdp->pktlog->pktlog_filter;

	if (strlen(CMD_PKTLOG_FILTER_EXIST) + 1 > total_len) {
		return BCME_ERROR;
	}

	exist = dhd_pktlog_filter_existed(filter, command + strlen(CMD_PKTLOG_FILTER_EXIST) + 1,
			&id);

	if (exist) {
		bytes_written = snprintf(command, total_len, "TRUE");
		DHD_ERROR(("%s: pktlog filter pattern id: %d is existed\n", __FUNCTION__, id));
	} else {
		bytes_written = snprintf(command, total_len, "FALSE");
		DHD_ERROR(("%s: pktlog filter pattern id: %d is not existed\n", __FUNCTION__, id));
	}

	return bytes_written;
}

static int
wl_android_pktlog_minmize_enable(struct net_device *dev, char *command, int total_len)
{
	int bytes_written = 0;
	dhd_pub_t *dhdp = wl_cfg80211_get_dhdp(dev);

	if (!dhdp || !dhdp->pktlog) {
		DHD_PKT_LOG(("%s(): dhdp=%p pktlog=%p\n",
			__FUNCTION__, dhdp, (dhdp ? dhdp->pktlog : NULL)));
		return -EINVAL;
	}

	if (!dhdp->pktlog->pktlog_ring) {
		DHD_PKT_LOG(("%s(): pktlog_ring=%p\n",
			__FUNCTION__, dhdp->pktlog->pktlog_ring));
		return -EINVAL;
	}

	dhdp->pktlog->pktlog_ring->pktlog_minmize = TRUE;

	bytes_written = snprintf(command, total_len, "OK");

	DHD_ERROR(("%s: pktlog pktlog_minmize enable\n", __FUNCTION__));

	return bytes_written;
}

static int
wl_android_pktlog_minmize_disable(struct net_device *dev, char *command, int total_len)
{
	int bytes_written = 0;
	dhd_pub_t *dhdp = wl_cfg80211_get_dhdp(dev);

	if (!dhdp || !dhdp->pktlog) {
		DHD_PKT_LOG(("%s(): dhdp=%p pktlog=%p\n",
			__FUNCTION__, dhdp, (dhdp ? dhdp->pktlog : NULL)));
		return -EINVAL;
	}

	if (!dhdp->pktlog->pktlog_ring) {
		DHD_PKT_LOG(("%s(): pktlog_ring=%p\n",
			__FUNCTION__, dhdp->pktlog->pktlog_ring));
		return -EINVAL;
	}

	dhdp->pktlog->pktlog_ring->pktlog_minmize = FALSE;

	bytes_written = snprintf(command, total_len, "OK");

	DHD_ERROR(("%s: pktlog pktlog_minmize disable\n", __FUNCTION__));

	return bytes_written;
}

static int
wl_android_pktlog_change_size(struct net_device *dev, char *command, int total_len)
{
	int bytes_written = 0;
	dhd_pub_t *dhdp = wl_cfg80211_get_dhdp(dev);
	int err = BCME_OK;
	int size;

	if (!dhdp || !dhdp->pktlog) {
		DHD_PKT_LOG(("%s(): dhdp=%p pktlog=%p\n",
			__FUNCTION__, dhdp, (dhdp ? dhdp->pktlog : NULL)));
		return -EINVAL;
	}

	if (strlen(CMD_PKTLOG_CHANGE_SIZE) + 1 > total_len) {
		return BCME_ERROR;
	}

	size = bcm_strtoul(command + strlen(CMD_PKTLOG_CHANGE_SIZE) + 1, NULL, 0);

	dhdp->pktlog->pktlog_ring =
		dhd_pktlog_ring_change_size(dhdp->pktlog->pktlog_ring, size);
	if (!dhdp->pktlog->pktlog_ring) {
		err = BCME_ERROR;
	}

	if (err == BCME_OK) {
		bytes_written = snprintf(command, total_len, "OK");
		DHD_ERROR(("%s: pktlog change size success\n", __FUNCTION__));
	} else {
		DHD_ERROR(("%s: pktlog change size fail\n", __FUNCTION__));
		return BCME_ERROR;
	}

	return bytes_written;
}
#endif /* DHD_PKT_LOGGING */

#ifdef DHD_EVENT_LOG_FILTER
uint32 dhd_event_log_filter_serialize(dhd_pub_t *dhdp, char *buf, uint32 tot_len, int type);

#ifdef DHD_EWPR_VER2
uint32 dhd_event_log_filter_serialize_bit(dhd_pub_t *dhdp, char *buf, uint32 tot_len,
	int index1, int index2, int index3);
#endif // endif

static int
wl_android_ewp_filter(struct net_device *dev, char *command, uint32 tot_len)
{
	uint32 bytes_written = 0;
	dhd_pub_t *dhdp = wl_cfg80211_get_dhdp(dev);
#ifdef DHD_EWPR_VER2
	int index1 = 0, index2 = 0, index3 = 0;
	unsigned char *index_str = (unsigned char *)(command +
		strlen(CMD_EWP_FILTER) + 1);
#else
	int type = 0;
#endif // endif

	if (!dhdp || !command) {
		DHD_ERROR(("%s(): dhdp=%p \n", __FUNCTION__, dhdp));
		return -EINVAL;
	}

#ifdef DHD_EWPR_VER2
	if (strlen(command) > strlen(CMD_EWP_FILTER) + 1) {
		sscanf(index_str, "%10d %10d %10d", &index1, &index2, &index3);
		DHD_TRACE(("%s(): get index request: %d %d %d\n", __FUNCTION__,
			index1, index2, index3));
	}
	bytes_written += dhd_event_log_filter_serialize_bit(dhdp,
		&command[bytes_written], tot_len - bytes_written, index1, index2, index3);
#else
	/* NEED TO GET TYPE if EXIST */
	type = 0;

	bytes_written += dhd_event_log_filter_serialize(dhdp,
		&command[bytes_written], tot_len - bytes_written, type);
#endif // endif

	return (int)bytes_written;
}
#endif /* DHD_EVENT_LOG_FILTER */

int wl_android_priv_cmd(struct net_device *net, struct ifreq *ifr)
{
#define PRIVATE_COMMAND_MAX_LEN	8192
#define PRIVATE_COMMAND_DEF_LEN	4096
	int ret = 0;
	char *command = NULL;
	int bytes_written = 0;
	android_wifi_priv_cmd priv_cmd;
	int buf_size = 0;
	struct bcm_cfg80211 *cfg = wl_get_cfg(net);

	net_os_wake_lock(net);

	if (!capable(CAP_NET_ADMIN)) {
		ret = -EPERM;
		goto exit;
	}

	if (!ifr->ifr_data) {
		ret = -EINVAL;
		goto exit;
	}

	{
		if (copy_from_user(&priv_cmd, ifr->ifr_data, sizeof(android_wifi_priv_cmd))) {
			ret = -EFAULT;
			goto exit;
		}
	}
	if ((priv_cmd.total_len > PRIVATE_COMMAND_MAX_LEN) || (priv_cmd.total_len < 0)) {
		DHD_ERROR(("wl_android_priv_cmd: buf length invalid:%d\n",
			priv_cmd.total_len));
		ret = -EINVAL;
		goto exit;
	}

	buf_size = max(priv_cmd.total_len, PRIVATE_COMMAND_DEF_LEN);
	command = (char *)MALLOC(cfg->osh, (buf_size + 1));
	if (!command) {
		DHD_ERROR(("wl_android_priv_cmd: failed to allocate memory\n"));
		ret = -ENOMEM;
		goto exit;
	}
	if (copy_from_user(command, priv_cmd.buf, priv_cmd.total_len)) {
		ret = -EFAULT;
		goto exit;
	}
	command[priv_cmd.total_len] = '\0';

	DHD_ERROR(("wl_android_priv_cmd: Android private cmd \"%s\" on %s\n",
		command, ifr->ifr_name));

	bytes_written = wl_handle_private_cmd(net, command, priv_cmd.total_len);
	if (bytes_written >= 0) {
		if ((bytes_written == 0) && (priv_cmd.total_len > 0)) {
			command[0] = '\0';
		}
		if (bytes_written >= priv_cmd.total_len) {
			DHD_ERROR(("wl_android_priv_cmd: err. bytes_written:%d >= total_len:%d,"
				" buf_size:%d \n", bytes_written, priv_cmd.total_len, buf_size));
			ret = BCME_BUFTOOSHORT;
			goto exit;
		}
		bytes_written++;
		priv_cmd.used_len = bytes_written;
		if (copy_to_user(priv_cmd.buf, command, bytes_written)) {
			DHD_ERROR(("wl_android_priv_cmd: failed to copy data to user buffer\n"));
			ret = -EFAULT;
		}
	}
	else {
		/* Propagate the error */
		ret = bytes_written;
	}

exit:
#ifdef DHD_SEND_HANG_PRIVCMD_ERRORS
	if (ret) {
		/* Avoid incrementing priv_cmd_errors in case of unsupported feature */
		if (ret != BCME_UNSUPPORTED) {
			wl_android_check_priv_cmd_errors(net);
		}
	} else {
		priv_cmd_errors = 0;
	}
#endif /* DHD_SEND_HANG_PRIVCMD_ERRORS */
	net_os_wake_unlock(net);
	MFREE(cfg->osh, command, (buf_size + 1));
	return ret;
}
#ifdef WLADPS_PRIVATE_CMD
static int
wl_android_set_adps_mode(struct net_device *dev, const char* string_num)
{
	int err = 0, adps_mode;
	dhd_pub_t *dhdp = wl_cfg80211_get_dhdp(dev);
#ifdef DHD_PM_CONTROL_FROM_FILE
	if (g_pm_control) {
		return -EPERM;
	}
#endif	/* DHD_PM_CONTROL_FROM_FILE */

	adps_mode = bcm_atoi(string_num);
	WL_ERR(("%s: SET_ADPS %d\n", __FUNCTION__, adps_mode));

	if ((adps_mode < 0) && (1 < adps_mode)) {
		WL_ERR(("wl_android_set_adps_mode: Invalid value %d.\n", adps_mode));
		return -EINVAL;
	}

	err = dhd_enable_adps(dhdp, adps_mode);
	if (err != BCME_OK) {
		WL_ERR(("failed to set adps mode %d, error = %d\n", adps_mode, err));
		return -EIO;
	}
	return err;
}
static int
wl_android_get_adps_mode(
	struct net_device *dev, char *command, int total_len)
{
	int bytes_written, err = 0;
	uint len;
	char buf[WLC_IOCTL_SMLEN];

	bcm_iov_buf_t iov_buf;
	bcm_iov_buf_t *ptr = NULL;
	wl_adps_params_v1_t *data = NULL;

	uint8 *pdata = NULL;
	uint8 band, mode = 0;

	bzero(&iov_buf, sizeof(iov_buf));

	len = OFFSETOF(bcm_iov_buf_t, data) + sizeof(band);

	iov_buf.version = WL_ADPS_IOV_VER;
	iov_buf.len = sizeof(band);
	iov_buf.id = WL_ADPS_IOV_MODE;

	pdata = (uint8 *)&iov_buf.data;

	for (band = 1; band <= MAX_BANDS; band++) {
		pdata[0] = band;
		err = wldev_iovar_getbuf(dev, "adps", &iov_buf, len,
			buf, WLC_IOCTL_SMLEN, NULL);
		if (err != BCME_OK) {
			WL_ERR(("wl_android_get_adps_mode fail to get adps band %d(%d).\n",
					band, err));
			return -EIO;
		}
		ptr = (bcm_iov_buf_t *) buf;
		data = (wl_adps_params_v1_t *) ptr->data;
		mode = data->mode;
		if (mode != OFF) {
			break;
		}
	}

	bytes_written = snprintf(command, total_len, "%s %d",
		CMD_GET_ADPS, mode);
	return bytes_written;
}
#endif /* WLADPS_PRIVATE_CMD */

#ifdef WL_BCNRECV
#define BCNRECV_ATTR_HDR_LEN 30
int
wl_android_bcnrecv_event(struct net_device *ndev, uint attr_type,
		uint status, uint reason, uint8 *data, uint data_len)
{
	s32 err = BCME_OK;
	struct sk_buff *skb;
	gfp_t kflags;
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	struct wiphy *wiphy = bcmcfg_to_wiphy(cfg);
	uint len;

	len = BCNRECV_ATTR_HDR_LEN + data_len;

	kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;
	skb = CFG80211_VENDOR_EVENT_ALLOC(wiphy, ndev_to_wdev(ndev), len,
		BRCM_VENDOR_EVENT_BEACON_RECV, kflags);
	if (!skb) {
		WL_ERR(("skb alloc failed"));
		return -ENOMEM;
	}
	if ((attr_type == BCNRECV_ATTR_BCNINFO) && (data)) {
		/* send bcn info to upper layer */
		nla_put(skb, BCNRECV_ATTR_BCNINFO, data_len, data);
	} else if (attr_type == BCNRECV_ATTR_STATUS) {
		nla_put_u32(skb, BCNRECV_ATTR_STATUS, status);
		if (reason) {
			nla_put_u32(skb, BCNRECV_ATTR_REASON, reason);
		}
	} else {
		WL_ERR(("UNKNOWN ATTR_TYPE. attr_type:%d\n", attr_type));
		kfree_skb(skb);
		return -EINVAL;
	}
	cfg80211_vendor_event(skb, kflags);
	return err;
}

static int
_wl_android_bcnrecv_start(struct bcm_cfg80211 *cfg, struct net_device *ndev, bool user_trigger)
{
	s32 err = BCME_OK;

	/* check any scan is in progress before beacon recv scan trigger IOVAR */
	if (wl_get_drv_status_all(cfg, SCANNING)) {
		err = BCME_UNSUPPORTED;
		WL_ERR(("Scan in progress, Aborting beacon recv start, "
			"error:%d\n", err));
		goto exit;
	}

	if (wl_get_p2p_status(cfg, SCANNING)) {
		err = BCME_UNSUPPORTED;
		WL_ERR(("P2P Scan in progress, Aborting beacon recv start, "
			"error:%d\n", err));
		goto exit;
	}

	if (wl_get_drv_status(cfg, REMAINING_ON_CHANNEL, ndev)) {
		err = BCME_UNSUPPORTED;
		WL_ERR(("P2P remain on channel, Aborting beacon recv start, "
			"error:%d\n", err));
		goto exit;
	}

	/* check STA is in connected state, Beacon recv required connected state
	 * else exit from beacon recv scan
	 */
	if (!wl_get_drv_status(cfg, CONNECTED, ndev)) {
		err = BCME_UNSUPPORTED;
		WL_ERR(("STA is in not associated state error:%d\n", err));
		goto exit;
	}

#ifdef WL_NAN
	/* Check NAN is enabled, if enabled exit else continue */
	if (wl_cfgnan_check_state(cfg)) {
		err = BCME_UNSUPPORTED;
		WL_ERR(("Nan is enabled, NAN+STA+FAKEAP concurrency is not supported\n"));
		goto exit;
	}
#endif /* WL_NAN */

	/* Triggering an sendup_bcn iovar */
	err = wldev_iovar_setint(ndev, "sendup_bcn", 1);
	if (unlikely(err)) {
		WL_ERR(("sendup_bcn failed to set, error:%d\n", err));
	} else {
		cfg->bcnrecv_info.bcnrecv_state = BEACON_RECV_STARTED;
		WL_INFORM_MEM(("bcnrecv started. user_trigger:%d\n", user_trigger));
		if (user_trigger) {
			if ((err = wl_android_bcnrecv_event(ndev, BCNRECV_ATTR_STATUS,
					WL_BCNRECV_STARTED, 0, NULL, 0)) != BCME_OK) {
				WL_ERR(("failed to send bcnrecv event, error:%d\n", err));
			}
		}
	}
exit:
	/*
	 * BCNRECV start request can be rejected from dongle
	 * in various conditions.
	 * Error code need to be overridden to BCME_UNSUPPORTED
	 * to avoid hang event from continous private
	 * command error
	 */
	if (err) {
		err = BCME_UNSUPPORTED;
	}
	return err;
}

int
_wl_android_bcnrecv_stop(struct bcm_cfg80211 *cfg, struct net_device *ndev, uint reason)
{
	s32 err = BCME_OK;
	u32 status;

	/* Send sendup_bcn iovar for all cases except W_BCNRECV_ROAMABORT reason -
	 * fw generates roam abort event after aborting the bcnrecv.
	 */
	if (reason != WL_BCNRECV_ROAMABORT) {
		/* Triggering an sendup_bcn iovar */
		err = wldev_iovar_setint(ndev, "sendup_bcn", 0);
		if (unlikely(err)) {
			WL_ERR(("sendup_bcn failed to set error:%d\n", err));
			goto exit;
		}
	}

	/* Send notification for all cases */
	if (reason == WL_BCNRECV_SUSPEND) {
		cfg->bcnrecv_info.bcnrecv_state = BEACON_RECV_SUSPENDED;
		status = WL_BCNRECV_SUSPENDED;
	} else {
		cfg->bcnrecv_info.bcnrecv_state = BEACON_RECV_STOPPED;
		WL_INFORM_MEM(("bcnrecv stopped\n"));
		if (reason == WL_BCNRECV_USER_TRIGGER) {
			status = WL_BCNRECV_STOPPED;
		} else {
			status = WL_BCNRECV_ABORTED;
		}
	}
	if ((err = wl_android_bcnrecv_event(ndev, BCNRECV_ATTR_STATUS, status,
			reason, NULL, 0)) != BCME_OK) {
		WL_ERR(("failed to send bcnrecv event, error:%d\n", err));
	}
exit:
	return err;
}

static int
wl_android_bcnrecv_start(struct bcm_cfg80211 *cfg, struct net_device *ndev)
{
	s32 err = BCME_OK;

	/* Adding scan_sync mutex to avoid race condition in b/w scan_req and bcn recv */
	mutex_lock(&cfg->scan_sync);
	mutex_lock(&cfg->bcn_sync);
	err = _wl_android_bcnrecv_start(cfg, ndev, true);
	mutex_unlock(&cfg->bcn_sync);
	mutex_unlock(&cfg->scan_sync);
	return err;
}

int
wl_android_bcnrecv_stop(struct net_device *ndev, uint reason)
{
	s32 err = BCME_OK;
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);

	mutex_lock(&cfg->bcn_sync);
	if ((cfg->bcnrecv_info.bcnrecv_state == BEACON_RECV_STARTED) ||
	   (cfg->bcnrecv_info.bcnrecv_state == BEACON_RECV_SUSPENDED)) {
		err = _wl_android_bcnrecv_stop(cfg, ndev, reason);
	}
	mutex_unlock(&cfg->bcn_sync);
	return err;
}

int
wl_android_bcnrecv_suspend(struct net_device *ndev)
{
	s32 ret = BCME_OK;
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);

	mutex_lock(&cfg->bcn_sync);
	if (cfg->bcnrecv_info.bcnrecv_state == BEACON_RECV_STARTED) {
		WL_INFORM_MEM(("bcnrecv suspend\n"));
		ret = _wl_android_bcnrecv_stop(cfg, ndev, WL_BCNRECV_SUSPEND);
	}
	mutex_unlock(&cfg->bcn_sync);
	return ret;
}

int
wl_android_bcnrecv_resume(struct net_device *ndev)
{
	s32 ret = BCME_OK;
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);

	/* Adding scan_sync mutex to avoid race condition in b/w scan_req and bcn recv */
	mutex_lock(&cfg->scan_sync);
	mutex_lock(&cfg->bcn_sync);
	if (cfg->bcnrecv_info.bcnrecv_state == BEACON_RECV_SUSPENDED) {
		WL_INFORM_MEM(("bcnrecv resume\n"));
		ret = _wl_android_bcnrecv_start(cfg, ndev, false);
	}
	mutex_unlock(&cfg->bcn_sync);
	mutex_unlock(&cfg->scan_sync);
	return ret;
}

/* Beacon recv functionality code implementation */
int
wl_android_bcnrecv_config(struct net_device *ndev, char *cmd_argv, int total_len)
{
	struct bcm_cfg80211 *cfg = NULL;
	uint err = BCME_OK;

	if (!ndev) {
		WL_ERR(("ndev is NULL\n"));
		return -EINVAL;
	}

	cfg = wl_get_cfg(ndev);
	if (!cfg) {
		WL_ERR(("cfg is NULL\n"));
		return -EINVAL;
	}

	/* sync commands from user space */
	mutex_lock(&cfg->usr_sync);
	if (strncmp(cmd_argv, "start", strlen("start")) == 0) {
		WL_INFORM(("BCNRECV start\n"));
		err = wl_android_bcnrecv_start(cfg, ndev);
		if (err != BCME_OK) {
			WL_ERR(("Failed to process the start command, error:%d\n", err));
			goto exit;
		}
	} else if (strncmp(cmd_argv, "stop", strlen("stop")) == 0) {
		WL_INFORM(("BCNRECV stop\n"));
		err = wl_android_bcnrecv_stop(ndev, WL_BCNRECV_USER_TRIGGER);
		if (err != BCME_OK) {
			WL_ERR(("Failed to stop the bcn recv, error:%d\n", err));
			goto exit;
		}
	} else {
		err = BCME_ERROR;
	}
exit:
	mutex_unlock(&cfg->usr_sync);
	return err;
}
#endif /* WL_BCNRECV */

#ifdef WL_CAC_TS
/* CAC TSPEC functionality code implementation */
static void
wl_android_update_tsinfo(uint8 access_category, tspec_arg_t *tspec_arg)
{
	uint8 tspec_id;
	/* Using direction as bidirectional by default */
	uint8 direction = TSPEC_BI_DIRECTION;
	/* Using U-APSD as the default power save mode */
	uint8 user_psb = TSPEC_UAPSD_PSB;
	uint8 ADDTS_AC2PRIO[4] = {PRIO_8021D_BE, PRIO_8021D_BK, PRIO_8021D_VI, PRIO_8021D_VO};

	/* Map tspec_id from access category */
	tspec_id = ADDTS_AC2PRIO[access_category];

	/* Update the tsinfo */
	tspec_arg->tsinfo.octets[0] = (uint8)(TSPEC_EDCA_ACCESS | direction |
		(tspec_id << TSPEC_TSINFO_TID_SHIFT));
	tspec_arg->tsinfo.octets[1] = (uint8)((tspec_id << TSPEC_TSINFO_PRIO_SHIFT) |
		user_psb);
	tspec_arg->tsinfo.octets[2] = 0x00;
}

static s32
wl_android_handle_cac_action(struct bcm_cfg80211 * cfg, struct net_device * ndev, char * argv)
{
	tspec_arg_t tspec_arg;
	s32 err = BCME_ERROR;
	u8 ts_cmd[12] = "cac_addts";
	uint8 access_category;
	s32 bssidx;

	/* Following handling is done only for the primary interface */
	memset_s(&tspec_arg, sizeof(tspec_arg), 0, sizeof(tspec_arg));
	if (strncmp(argv, "addts", strlen("addts")) == 0) {
		tspec_arg.version = TSPEC_ARG_VERSION;
		tspec_arg.length = sizeof(tspec_arg_t) - (2 * sizeof(uint16));
		/* Read the params passed */
		sscanf(argv, "%*s %hhu %hu %hu", &access_category,
			&tspec_arg.nom_msdu_size, &tspec_arg.surplus_bw);
		if ((access_category > TSPEC_MAX_ACCESS_CATEGORY) ||
			((tspec_arg.surplus_bw < TSPEC_MIN_SURPLUS_BW) ||
			(tspec_arg.surplus_bw > TSPEC_MAX_SURPLUS_BW)) ||
			(tspec_arg.nom_msdu_size > TSPEC_MAX_MSDU_SIZE)) {
			WL_ERR(("Invalid params access_category %hhu nom_msdu_size %hu"
				" surplus BW %hu\n", access_category, tspec_arg.nom_msdu_size,
				tspec_arg.surplus_bw));
			return BCME_USAGE_ERROR;
		}

		/* Update tsinfo */
		wl_android_update_tsinfo(access_category, &tspec_arg);
		/* Update other tspec parameters */
		tspec_arg.dialog_token = TSPEC_DEF_DIALOG_TOKEN;
		tspec_arg.mean_data_rate = TSPEC_DEF_MEAN_DATA_RATE;
		tspec_arg.min_phy_rate = TSPEC_DEF_MIN_PHY_RATE;
	} else if (strncmp(argv, "delts", strlen("delts")) == 0) {
		snprintf(ts_cmd, sizeof(ts_cmd), "cac_delts");
		tspec_arg.length = sizeof(tspec_arg_t) - (2 * sizeof(uint16));
		tspec_arg.version = TSPEC_ARG_VERSION;
		/* Read the params passed */
		sscanf(argv, "%*s %hhu", &access_category);

		if (access_category > TSPEC_MAX_ACCESS_CATEGORY) {
			WL_INFORM_MEM(("Invalide param, access_category %hhu\n", access_category));
			return BCME_USAGE_ERROR;
		}
		/* Update tsinfo */
		wl_android_update_tsinfo(access_category, &tspec_arg);
	}

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, ndev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find index failed\n"));
		err = BCME_ERROR;
		return err;
	}
	err = wldev_iovar_setbuf_bsscfg(ndev, ts_cmd, &tspec_arg, sizeof(tspec_arg),
			cfg->ioctl_buf, WLC_IOCTL_MAXLEN, bssidx, &cfg->ioctl_buf_sync);
	if (unlikely(err)) {
		WL_ERR(("%s error (%d)\n", ts_cmd, err));
	}

	return err;
}

static s32
wl_android_cac_ts_config(struct net_device *ndev, char *cmd_argv, int total_len)
{
	struct bcm_cfg80211 *cfg = NULL;
	s32 err = BCME_OK;

	if (!ndev) {
		WL_ERR(("ndev is NULL\n"));
		return -EINVAL;
	}

	cfg = wl_get_cfg(ndev);
	if (!cfg) {
		WL_ERR(("cfg is NULL\n"));
		return -EINVAL;
	}

	/* Request supported only for primary interface */
	if (ndev != bcmcfg_to_prmry_ndev(cfg)) {
		WL_ERR(("Request on non-primary interface\n"));
		return -1;
	}

	/* sync commands from user space */
	mutex_lock(&cfg->usr_sync);
	err = wl_android_handle_cac_action(cfg, ndev, cmd_argv);
	mutex_unlock(&cfg->usr_sync);

	return err;
}
#endif /* WL_CAC_TS */

#ifdef WL_GET_CU
/* Implementation to get channel usage from framework */
static s32
wl_android_get_channel_util(struct net_device *ndev, char *command, int total_len)
{
	s32 bytes_written, err = 0;
	wl_bssload_t bssload;
	u8 smbuf[WLC_IOCTL_SMLEN];
	u8 chan_use_percentage = 0;

	if ((err = wldev_iovar_getbuf(ndev, "bssload_report", NULL,
		0, smbuf, WLC_IOCTL_SMLEN, NULL))) {
		WL_ERR(("Getting bssload report failed with err=%d \n", err));
		return err;
	}

	(void)memcpy_s(&bssload, sizeof(wl_bssload_t), smbuf, sizeof(wl_bssload_t));
	/* Convert channel usage to percentage value */
	chan_use_percentage = (bssload.chan_util * 100) / 255;

	bytes_written = snprintf(command, total_len, "CU %hhu",
		chan_use_percentage);
	WL_DBG(("Channel Utilization %u %u\n", bssload.chan_util, chan_use_percentage));

	return bytes_written;
}
#endif /* WL_GET_CU */

int
wl_handle_private_cmd(struct net_device *net, char *command, u32 cmd_len)
{
	int bytes_written = 0;
	android_wifi_priv_cmd priv_cmd;

	bzero(&priv_cmd, sizeof(android_wifi_priv_cmd));
	priv_cmd.total_len = cmd_len;

	if (strnicmp(command, CMD_START, strlen(CMD_START)) == 0) {
		DHD_INFO(("wl_handle_private_cmd, Received regular START command\n"));
#ifdef SUPPORT_DEEP_SLEEP
		trigger_deep_sleep = 1;
#else
#ifdef  BT_OVER_SDIO
		bytes_written = dhd_net_bus_get(net);
#else
		bytes_written = wl_android_wifi_on(net);
#endif /* BT_OVER_SDIO */
#endif /* SUPPORT_DEEP_SLEEP */
	}
	else if (strnicmp(command, CMD_SETFWPATH, strlen(CMD_SETFWPATH)) == 0) {
		bytes_written = wl_android_set_fwpath(net, command, priv_cmd.total_len);
	}

	if (!g_wifi_on) {
		DHD_ERROR(("wl_handle_private_cmd: Ignore private cmd \"%s\" - iface is down\n",
			command));
		return 0;
	}

	if (strnicmp(command, CMD_STOP, strlen(CMD_STOP)) == 0) {
#ifdef SUPPORT_DEEP_SLEEP
		trigger_deep_sleep = 1;
#else
#ifdef  BT_OVER_SDIO
		bytes_written = dhd_net_bus_put(net);
#else
		bytes_written = wl_android_wifi_off(net, FALSE);
#endif /* BT_OVER_SDIO */
#endif /* SUPPORT_DEEP_SLEEP */
	}
#ifdef AUTOMOTIVE_FEATURE
	else if (strnicmp(command, CMD_SCAN_ACTIVE, strlen(CMD_SCAN_ACTIVE)) == 0) {
		wl_cfg80211_set_passive_scan(net, command);
	}
	else if (strnicmp(command, CMD_SCAN_PASSIVE, strlen(CMD_SCAN_PASSIVE)) == 0) {
		wl_cfg80211_set_passive_scan(net, command);
	}
	else if (strnicmp(command, CMD_RSSI, strlen(CMD_RSSI)) == 0) {
		bytes_written = wl_android_get_rssi(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_LINKSPEED, strlen(CMD_LINKSPEED)) == 0) {
		bytes_written = wl_android_get_link_speed(net, command, priv_cmd.total_len);
	}
#endif /* AUTOMOTIVE_FEATURE */
#ifdef PKT_FILTER_SUPPORT
	else if (strnicmp(command, CMD_RXFILTER_START, strlen(CMD_RXFILTER_START)) == 0) {
		bytes_written = net_os_enable_packet_filter(net, 1);
	}
	else if (strnicmp(command, CMD_RXFILTER_STOP, strlen(CMD_RXFILTER_STOP)) == 0) {
		bytes_written = net_os_enable_packet_filter(net, 0);
	}
	else if (strnicmp(command, CMD_RXFILTER_ADD, strlen(CMD_RXFILTER_ADD)) == 0) {
		int filter_num = *(command + strlen(CMD_RXFILTER_ADD) + 1) - '0';
		bytes_written = net_os_rxfilter_add_remove(net, TRUE, filter_num);
	}
	else if (strnicmp(command, CMD_RXFILTER_REMOVE, strlen(CMD_RXFILTER_REMOVE)) == 0) {
		int filter_num = *(command + strlen(CMD_RXFILTER_REMOVE) + 1) - '0';
		bytes_written = net_os_rxfilter_add_remove(net, FALSE, filter_num);
	}
#endif /* PKT_FILTER_SUPPORT */
	else if (strnicmp(command, CMD_BTCOEXSCAN_START, strlen(CMD_BTCOEXSCAN_START)) == 0) {
		/* TBD: BTCOEXSCAN-START */
	}
	else if (strnicmp(command, CMD_BTCOEXSCAN_STOP, strlen(CMD_BTCOEXSCAN_STOP)) == 0) {
		/* TBD: BTCOEXSCAN-STOP */
	}
	else if (strnicmp(command, CMD_BTCOEXMODE, strlen(CMD_BTCOEXMODE)) == 0) {
#ifdef WL_CFG80211
		void *dhdp = wl_cfg80211_get_dhdp(net);
		bytes_written = wl_cfg80211_set_btcoex_dhcp(net, dhdp, command);
#else
#ifdef PKT_FILTER_SUPPORT
		uint mode = *(command + strlen(CMD_BTCOEXMODE) + 1) - '0';

		if (mode == 1)
			net_os_enable_packet_filter(net, 0); /* DHCP starts */
		else
			net_os_enable_packet_filter(net, 1); /* DHCP ends */
#endif /* PKT_FILTER_SUPPORT */
#endif /* WL_CFG80211 */
	}
	else if (strnicmp(command, CMD_SETSUSPENDOPT, strlen(CMD_SETSUSPENDOPT)) == 0) {
		bytes_written = wl_android_set_suspendopt(net, command);
	}
	else if (strnicmp(command, CMD_SETSUSPENDMODE, strlen(CMD_SETSUSPENDMODE)) == 0) {
		bytes_written = wl_android_set_suspendmode(net, command);
	}
	else if (strnicmp(command, CMD_SETDTIM_IN_SUSPEND, strlen(CMD_SETDTIM_IN_SUSPEND)) == 0) {
		bytes_written = wl_android_set_bcn_li_dtim(net, command);
	}
	else if (strnicmp(command, CMD_MAXDTIM_IN_SUSPEND, strlen(CMD_MAXDTIM_IN_SUSPEND)) == 0) {
		bytes_written = wl_android_set_max_dtim(net, command);
	}
#ifdef DISABLE_DTIM_IN_SUSPEND
	else if (strnicmp(command, CMD_DISDTIM_IN_SUSPEND, strlen(CMD_DISDTIM_IN_SUSPEND)) == 0) {
		bytes_written = wl_android_set_disable_dtim_in_suspend(net, command);
	}
#endif /* DISABLE_DTIM_IN_SUSPEND */
	else if (strnicmp(command, CMD_SETBAND, strlen(CMD_SETBAND)) == 0) {
		bytes_written = wl_android_set_band(net, command);
	}
	else if (strnicmp(command, CMD_GETBAND, strlen(CMD_GETBAND)) == 0) {
		bytes_written = wl_android_get_band(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_ADDIE, strlen(CMD_ADDIE)) == 0) {
		bytes_written = wl_android_add_vendor_ie(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_DELIE, strlen(CMD_DELIE)) == 0) {
		bytes_written = wl_android_del_vendor_ie(net, command, priv_cmd.total_len);
	}
#ifdef WL_CFG80211
#ifndef CUSTOMER_SET_COUNTRY
	/* CUSTOMER_SET_COUNTRY feature is define for only GGSM model */
	else if (strnicmp(command, CMD_COUNTRY, strlen(CMD_COUNTRY)) == 0) {
		/*
		 * Usage examples:
		 * DRIVER COUNTRY US
		 * DRIVER COUNTRY US/7
		 */
		char *country_code = command + strlen(CMD_COUNTRY) + 1;
		char *rev_info_delim = country_code + 2; /* 2 bytes of country code */
		int revinfo = -1;
#if defined(DHD_BLOB_EXISTENCE_CHECK)
		dhd_pub_t *dhdp = wl_cfg80211_get_dhdp(net);

		if (dhdp->is_blob) {
			revinfo = 0;
		} else
#endif /* DHD_BLOB_EXISTENCE_CHECK */
		if ((rev_info_delim) &&
			(strnicmp(rev_info_delim, CMD_COUNTRY_DELIMITER,
			strlen(CMD_COUNTRY_DELIMITER)) == 0) &&
			(rev_info_delim + 1)) {
			revinfo  = bcm_atoi(rev_info_delim + 1);
		}
#ifdef SAVE_CONNECTION_WHEN_CC_UPDATE
		wl_check_valid_channel_in_country(net, country_code, true);
		bytes_written = wl_cfg80211_set_country_code(net, country_code,
				true, false, revinfo);

		wl_update_ap_chandef(net);
#else
		bytes_written = wl_cfg80211_set_country_code(net, country_code,
				true, true, revinfo);
#endif // endif
#ifdef CUSTOMER_HW4_PRIVATE_CMD
#ifdef FCC_PWR_LIMIT_2G
		if (wldev_iovar_setint(net, "fccpwrlimit2g", FALSE)) {
			DHD_ERROR(("%s: fccpwrlimit2g deactivation is failed\n", __FUNCTION__));
		} else {
			DHD_ERROR(("%s: fccpwrlimit2g is deactivated\n", __FUNCTION__));
		}
#endif /* FCC_PWR_LIMIT_2G */
#endif /* CUSTOMER_HW4_PRIVATE_CMD */
	}
#endif /* CUSTOMER_SET_COUNTRY */
#endif /* WL_CFG80211 */
	else if (strnicmp(command, CMD_CHANNELS_IN_CC, strlen(CMD_CHANNELS_IN_CC)) == 0) {
		bytes_written = wl_android_get_channel_list(net, command, priv_cmd.total_len);
	} else if (strnicmp(command, CMD_SET_CSA, strlen(CMD_SET_CSA)) == 0) {
		bytes_written = wl_android_set_csa(net, command);
	} else if (strnicmp(command, CMD_CHANSPEC, strlen(CMD_CHANSPEC)) == 0) {
		bytes_written = wl_android_get_chanspec(net, command, priv_cmd.total_len);
	} else if (strnicmp(command, CMD_BLOCKASSOC, strlen(CMD_BLOCKASSOC)) == 0) {
               bytes_written = wl_android_block_associations(net, command, priv_cmd.total_len);
	}
#ifdef AUTOMOTIVE_FEATURE
	 else if (strnicmp(command, CMD_DATARATE, strlen(CMD_DATARATE)) == 0) {
		bytes_written = wl_android_get_datarate(net, command, priv_cmd.total_len);
	} else if (strnicmp(command, CMD_80211_MODE, strlen(CMD_80211_MODE)) == 0) {
		bytes_written = wl_android_get_80211_mode(net, command, priv_cmd.total_len);
	} else if (strnicmp(command, CMD_ASSOC_CLIENTS, strlen(CMD_ASSOC_CLIENTS)) == 0) {
		bytes_written = wl_android_get_assoclist(net, command, priv_cmd.total_len);
	}
#endif /* AUTOMOTIVE_FEATURE */
	 else if (strnicmp(command, CMD_RSDB_MODE, strlen(CMD_RSDB_MODE)) == 0) {
		bytes_written = wl_android_get_rsdb_mode(net, command, priv_cmd.total_len);
	}

#if defined(CUSTOMER_HW4_PRIVATE_CMD) || defined(IGUANA_LEGACY_CHIPS)
#ifdef ROAM_API
	else if (strnicmp(command, CMD_ROAMTRIGGER_SET,
		strlen(CMD_ROAMTRIGGER_SET)) == 0) {
		bytes_written = wl_android_set_roam_trigger(net, command);
	} else if (strnicmp(command, CMD_ROAMTRIGGER_GET,
		strlen(CMD_ROAMTRIGGER_GET)) == 0) {
		bytes_written = wl_android_get_roam_trigger(net, command,
		priv_cmd.total_len);
	} else if (strnicmp(command, CMD_ROAMDELTA_SET,
		strlen(CMD_ROAMDELTA_SET)) == 0) {
		bytes_written = wl_android_set_roam_delta(net, command);
	} else if (strnicmp(command, CMD_ROAMDELTA_GET,
		strlen(CMD_ROAMDELTA_GET)) == 0) {
		bytes_written = wl_android_get_roam_delta(net, command,
		priv_cmd.total_len);
	} else if (strnicmp(command, CMD_ROAMSCANPERIOD_SET,
		strlen(CMD_ROAMSCANPERIOD_SET)) == 0) {
		bytes_written = wl_android_set_roam_scan_period(net, command);
	} else if (strnicmp(command, CMD_ROAMSCANPERIOD_GET,
		strlen(CMD_ROAMSCANPERIOD_GET)) == 0) {
		bytes_written = wl_android_get_roam_scan_period(net, command,
		priv_cmd.total_len);
	} else if (strnicmp(command, CMD_FULLROAMSCANPERIOD_SET,
		strlen(CMD_FULLROAMSCANPERIOD_SET)) == 0) {
		bytes_written = wl_android_set_full_roam_scan_period(net, command,
		priv_cmd.total_len);
	} else if (strnicmp(command, CMD_FULLROAMSCANPERIOD_GET,
		strlen(CMD_FULLROAMSCANPERIOD_GET)) == 0) {
		bytes_written = wl_android_get_full_roam_scan_period(net, command,
		priv_cmd.total_len);
	}
#ifdef AUTOMOTIVE_FEATURE
	 else if (strnicmp(command, CMD_COUNTRYREV_SET,
		strlen(CMD_COUNTRYREV_SET)) == 0) {
		bytes_written = wl_android_set_country_rev(net, command);
#ifdef FCC_PWR_LIMIT_2G
		if (wldev_iovar_setint(net, "fccpwrlimit2g", FALSE)) {
			DHD_ERROR(("wl_handle_private_cmd: fccpwrlimit2g"
				" deactivation is failed\n"));
		} else {
			DHD_ERROR(("wl_handle_private_cmd: fccpwrlimit2g is deactivated\n"));
		}
#endif /* FCC_PWR_LIMIT_2G */
	} else if (strnicmp(command, CMD_COUNTRYREV_GET,
		strlen(CMD_COUNTRYREV_GET)) == 0) {
		bytes_written = wl_android_get_country_rev(net, command,
		priv_cmd.total_len);
	}
#endif /* AUTOMOTIVE_FEATURE */
#endif /* ROAM_API */
#ifdef WES_SUPPORT
	else if (strnicmp(command, CMD_GETROAMSCANCONTROL, strlen(CMD_GETROAMSCANCONTROL)) == 0) {
		bytes_written = wl_android_get_roam_scan_control(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_SETROAMSCANCONTROL, strlen(CMD_SETROAMSCANCONTROL)) == 0) {
		bytes_written = wl_android_set_roam_scan_control(net, command);
	}
	else if (strnicmp(command, CMD_GETROAMSCANCHANNELS, strlen(CMD_GETROAMSCANCHANNELS)) == 0) {
		bytes_written = wl_android_get_roam_scan_channels(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_SETROAMSCANCHANNELS, strlen(CMD_SETROAMSCANCHANNELS)) == 0) {
		bytes_written = wl_android_set_roam_scan_channels(net, command);
	}
	else if (strnicmp(command, CMD_SENDACTIONFRAME, strlen(CMD_SENDACTIONFRAME)) == 0) {
		bytes_written = wl_android_send_action_frame(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_REASSOC, strlen(CMD_REASSOC)) == 0) {
		bytes_written = wl_android_reassoc(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_GETSCANCHANNELTIME, strlen(CMD_GETSCANCHANNELTIME)) == 0) {
		bytes_written = wl_android_get_scan_channel_time(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_SETSCANCHANNELTIME, strlen(CMD_SETSCANCHANNELTIME)) == 0) {
		bytes_written = wl_android_set_scan_channel_time(net, command);
	}
	else if (strnicmp(command, CMD_GETSCANUNASSOCTIME, strlen(CMD_GETSCANUNASSOCTIME)) == 0) {
		bytes_written = wl_android_get_scan_unassoc_time(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_SETSCANUNASSOCTIME, strlen(CMD_SETSCANUNASSOCTIME)) == 0) {
		bytes_written = wl_android_set_scan_unassoc_time(net, command);
	}
	else if (strnicmp(command, CMD_GETSCANPASSIVETIME, strlen(CMD_GETSCANPASSIVETIME)) == 0) {
		bytes_written = wl_android_get_scan_passive_time(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_SETSCANPASSIVETIME, strlen(CMD_SETSCANPASSIVETIME)) == 0) {
		bytes_written = wl_android_set_scan_passive_time(net, command);
	}
	else if (strnicmp(command, CMD_GETSCANHOMETIME, strlen(CMD_GETSCANHOMETIME)) == 0) {
		bytes_written = wl_android_get_scan_home_time(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_SETSCANHOMETIME, strlen(CMD_SETSCANHOMETIME)) == 0) {
		bytes_written = wl_android_set_scan_home_time(net, command);
	}
	else if (strnicmp(command, CMD_GETSCANHOMEAWAYTIME, strlen(CMD_GETSCANHOMEAWAYTIME)) == 0) {
		bytes_written = wl_android_get_scan_home_away_time(net, command,
			priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_SETSCANHOMEAWAYTIME, strlen(CMD_SETSCANHOMEAWAYTIME)) == 0) {
		bytes_written = wl_android_set_scan_home_away_time(net, command);
	}
	else if (strnicmp(command, CMD_GETSCANNPROBES, strlen(CMD_GETSCANNPROBES)) == 0) {
		bytes_written = wl_android_get_scan_nprobes(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_SETSCANNPROBES, strlen(CMD_SETSCANNPROBES)) == 0) {
		bytes_written = wl_android_set_scan_nprobes(net, command);
	}
	else if (strnicmp(command, CMD_GETDFSSCANMODE, strlen(CMD_GETDFSSCANMODE)) == 0) {
		bytes_written = wl_android_get_scan_dfs_channel_mode(net, command,
			priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_SETDFSSCANMODE, strlen(CMD_SETDFSSCANMODE)) == 0) {
		bytes_written = wl_android_set_scan_dfs_channel_mode(net, command);
	}
	else if (strnicmp(command, CMD_SETJOINPREFER, strlen(CMD_SETJOINPREFER)) == 0) {
		bytes_written = wl_android_set_join_prefer(net, command);
	}
	else if (strnicmp(command, CMD_GETWESMODE, strlen(CMD_GETWESMODE)) == 0) {
		bytes_written = wl_android_get_wes_mode(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_SETWESMODE, strlen(CMD_SETWESMODE)) == 0) {
		bytes_written = wl_android_set_wes_mode(net, command);
	}
	else if (strnicmp(command, CMD_GETOKCMODE, strlen(CMD_GETOKCMODE)) == 0) {
		bytes_written = wl_android_get_okc_mode(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_SETOKCMODE, strlen(CMD_SETOKCMODE)) == 0) {
		bytes_written = wl_android_set_okc_mode(net, command);
	}
	else if (strnicmp(command, CMD_OKC_SET_PMK, strlen(CMD_OKC_SET_PMK)) == 0) {
		bytes_written = wl_android_set_pmk(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_OKC_ENABLE, strlen(CMD_OKC_ENABLE)) == 0) {
		bytes_written = wl_android_okc_enable(net, command);
	}
#endif /* WES_SUPPORT */
#ifdef SUPPORT_RESTORE_SCAN_PARAMS
	else if (strnicmp(command, CMD_RESTORE_SCAN_PARAMS, strlen(CMD_RESTORE_SCAN_PARAMS)) == 0) {
		bytes_written = wl_android_restore_scan_params(net, command, priv_cmd.total_len);
	}
#endif /* SUPPORT_RESTORE_SCAN_PARAMS */
#ifdef WLTDLS
	else if (strnicmp(command, CMD_TDLS_RESET, strlen(CMD_TDLS_RESET)) == 0) {
		bytes_written = wl_android_tdls_reset(net);
	}
#endif /* WLTDLS */
#ifdef CONFIG_SILENT_ROAM
	else if (strnicmp(command, CMD_SROAM_TURN_ON, strlen(CMD_SROAM_TURN_ON)) == 0) {
		int skip = strlen(CMD_SROAM_TURN_ON) + 1;
		bytes_written = wl_android_sroam_turn_on(net, (const char*)command+skip);
	}
	else if (strnicmp(command, CMD_SROAM_SET_INFO, strlen(CMD_SROAM_SET_INFO)) == 0) {
		char *data = (command + strlen(CMD_SROAM_SET_INFO) + 1);
		bytes_written = wl_android_sroam_set_info(net, data, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_SROAM_GET_INFO, strlen(CMD_SROAM_GET_INFO)) == 0) {
		bytes_written = wl_android_sroam_get_info(net, command, priv_cmd.total_len);
	}
#endif /* CONFIG_SILENT_ROAM */
	else if (strnicmp(command, CMD_SET_DISCONNECT_IES, strlen(CMD_SET_DISCONNECT_IES)) == 0) {
		bytes_written = wl_android_set_disconnect_ies(net, command);
	}
#endif /* CUSTOMER_HW4_PRIVATE_CMD */

#ifdef PNO_SUPPORT
	else if (strnicmp(command, CMD_PNOSSIDCLR_SET, strlen(CMD_PNOSSIDCLR_SET)) == 0) {
		bytes_written = dhd_dev_pno_stop_for_ssid(net);
	}
#ifndef WL_SCHED_SCAN
	else if (strnicmp(command, CMD_PNOSETUP_SET, strlen(CMD_PNOSETUP_SET)) == 0) {
		bytes_written = wl_android_set_pno_setup(net, command, priv_cmd.total_len);
	}
#endif /* !WL_SCHED_SCAN */
	else if (strnicmp(command, CMD_PNOENABLE_SET, strlen(CMD_PNOENABLE_SET)) == 0) {
		int enable = *(command + strlen(CMD_PNOENABLE_SET) + 1) - '0';
		bytes_written = (enable)? 0 : dhd_dev_pno_stop_for_ssid(net);
	}
	else if (strnicmp(command, CMD_WLS_BATCHING, strlen(CMD_WLS_BATCHING)) == 0) {
		bytes_written = wls_parse_batching_cmd(net, command, priv_cmd.total_len);
	}
#endif /* PNO_SUPPORT */
	else if (strnicmp(command, CMD_P2P_DEV_ADDR, strlen(CMD_P2P_DEV_ADDR)) == 0) {
		bytes_written = wl_android_get_p2p_dev_addr(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_P2P_SET_NOA, strlen(CMD_P2P_SET_NOA)) == 0) {
		int skip = strlen(CMD_P2P_SET_NOA) + 1;
		bytes_written = wl_cfg80211_set_p2p_noa(net, command + skip,
			priv_cmd.total_len - skip);
	}
#ifdef P2P_LISTEN_OFFLOADING
	else if (strnicmp(command, CMD_P2P_LISTEN_OFFLOAD, strlen(CMD_P2P_LISTEN_OFFLOAD)) == 0) {
		u8 *sub_command = strchr(command, ' ');
		bytes_written = wl_cfg80211_p2plo_offload(net, command, sub_command,
				sub_command ? strlen(sub_command) : 0);
	}
#endif /* P2P_LISTEN_OFFLOADING */
#if !defined WL_ENABLE_P2P_IF
	else if (strnicmp(command, CMD_P2P_GET_NOA, strlen(CMD_P2P_GET_NOA)) == 0) {
		bytes_written = wl_cfg80211_get_p2p_noa(net, command, priv_cmd.total_len);
	}
#endif /* WL_ENABLE_P2P_IF */
	else if (strnicmp(command, CMD_P2P_SET_PS, strlen(CMD_P2P_SET_PS)) == 0) {
		int skip = strlen(CMD_P2P_SET_PS) + 1;
		bytes_written = wl_cfg80211_set_p2p_ps(net, command + skip,
			priv_cmd.total_len - skip);
	}
	else if (strnicmp(command, CMD_P2P_ECSA, strlen(CMD_P2P_ECSA)) == 0) {
		int skip = strlen(CMD_P2P_ECSA) + 1;
		bytes_written = wl_cfg80211_set_p2p_ecsa(net, command + skip,
			priv_cmd.total_len - skip);
	}
	else if (strnicmp(command, CMD_P2P_INC_BW, strlen(CMD_P2P_INC_BW)) == 0) {
		int skip = strlen(CMD_P2P_INC_BW) + 1;
		bytes_written = wl_cfg80211_increase_p2p_bw(net,
				command + skip, priv_cmd.total_len - skip);
	}
#ifdef WL_CFG80211
	else if (strnicmp(command, CMD_SET_AP_WPS_P2P_IE,
		strlen(CMD_SET_AP_WPS_P2P_IE)) == 0) {
		int skip = strlen(CMD_SET_AP_WPS_P2P_IE) + 3;
		bytes_written = wl_cfg80211_set_wps_p2p_ie(net, command + skip,
			priv_cmd.total_len - skip, *(command + skip - 2) - '0');
	}
#ifdef WLFBT
	else if (strnicmp(command, CMD_GET_FTKEY, strlen(CMD_GET_FTKEY)) == 0) {
		bytes_written = wl_cfg80211_get_fbt_key(net, command, priv_cmd.total_len);
	}
#endif /* WLFBT */
#endif /* WL_CFG80211 */
#if defined(WL_SUPPORT_AUTO_CHANNEL)
	else if (strnicmp(command, CMD_GET_BEST_CHANNELS,
		strlen(CMD_GET_BEST_CHANNELS)) == 0) {
		bytes_written = wl_cfg80211_get_best_channels(net, command,
			priv_cmd.total_len);
	}
#endif /* WL_SUPPORT_AUTO_CHANNEL */
#if defined(WL_SUPPORT_AUTO_CHANNEL)
	else if (strnicmp(command, CMD_SET_HAPD_AUTO_CHANNEL,
		strlen(CMD_SET_HAPD_AUTO_CHANNEL)) == 0) {
		int skip = strlen(CMD_SET_HAPD_AUTO_CHANNEL) + 1;
		bytes_written = wl_android_set_auto_channel(net, (const char*)command+skip, command,
			priv_cmd.total_len);
	}
#endif /* WL_SUPPORT_AUTO_CHANNEL */
#ifdef CUSTOMER_HW4_PRIVATE_CMD
#ifdef SUPPORT_AMPDU_MPDU_CMD
	/* CMD_AMPDU_MPDU */
	else if (strnicmp(command, CMD_AMPDU_MPDU, strlen(CMD_AMPDU_MPDU)) == 0) {
		int skip = strlen(CMD_AMPDU_MPDU) + 1;
		bytes_written = wl_android_set_ampdu_mpdu(net, (const char*)command+skip);
	}
#endif /* SUPPORT_AMPDU_MPDU_CMD */
#ifdef SUPPORT_SOFTAP_SINGL_DISASSOC
	else if (strnicmp(command, CMD_HAPD_STA_DISASSOC,
		strlen(CMD_HAPD_STA_DISASSOC)) == 0) {
		int skip = strlen(CMD_HAPD_STA_DISASSOC) + 1;
		wl_android_sta_diassoc(net, (const char*)command+skip);
	}
#endif /* SUPPORT_SOFTAP_SINGL_DISASSOC */
#ifdef SUPPORT_SET_LPC
	else if (strnicmp(command, CMD_HAPD_LPC_ENABLED,
		strlen(CMD_HAPD_LPC_ENABLED)) == 0) {
		int skip = strlen(CMD_HAPD_LPC_ENABLED) + 3;
		wl_android_set_lpc(net, (const char*)command+skip);
	}
#endif /* SUPPORT_SET_LPC */
#ifdef SUPPORT_TRIGGER_HANG_EVENT
	else if (strnicmp(command, CMD_TEST_FORCE_HANG,
		strlen(CMD_TEST_FORCE_HANG)) == 0) {
		int skip = strlen(CMD_TEST_FORCE_HANG) + 1;
		net_os_send_hang_message_reason(net, (const char*)command+skip);
	}
#endif /* SUPPORT_TRIGGER_HANG_EVENT */
	else if (strnicmp(command, CMD_CHANGE_RL, strlen(CMD_CHANGE_RL)) == 0)
		bytes_written = wl_android_ch_res_rl(net, true);
	else if (strnicmp(command, CMD_RESTORE_RL, strlen(CMD_RESTORE_RL)) == 0)
		bytes_written = wl_android_ch_res_rl(net, false);
#ifdef SUPPORT_LTECX
	else if (strnicmp(command, CMD_LTECX_SET, strlen(CMD_LTECX_SET)) == 0) {
		int skip = strlen(CMD_LTECX_SET) + 1;
		bytes_written = wl_android_set_ltecx(net, (const char*)command+skip);
	}
#endif /* SUPPORT_LTECX */
#ifdef WL_RELMCAST
	else if (strnicmp(command, CMD_SET_RMC_ENABLE, strlen(CMD_SET_RMC_ENABLE)) == 0) {
		int rmc_enable = *(command + strlen(CMD_SET_RMC_ENABLE) + 1) - '0';
		bytes_written = wl_android_rmc_enable(net, rmc_enable);
	}
	else if (strnicmp(command, CMD_SET_RMC_TXRATE, strlen(CMD_SET_RMC_TXRATE)) == 0) {
		int rmc_txrate;
		sscanf(command, "%*s %10d", &rmc_txrate);
		bytes_written = wldev_iovar_setint(net, "rmc_txrate", rmc_txrate * 2);
	}
	else if (strnicmp(command, CMD_SET_RMC_ACTPERIOD, strlen(CMD_SET_RMC_ACTPERIOD)) == 0) {
		int actperiod;
		sscanf(command, "%*s %10d", &actperiod);
		bytes_written = wldev_iovar_setint(net, "rmc_actf_time", actperiod);
	}
	else if (strnicmp(command, CMD_SET_RMC_IDLEPERIOD, strlen(CMD_SET_RMC_IDLEPERIOD)) == 0) {
		int acktimeout;
		sscanf(command, "%*s %10d", &acktimeout);
		acktimeout *= 1000;
		bytes_written = wldev_iovar_setint(net, "rmc_acktmo", acktimeout);
	}
	else if (strnicmp(command, CMD_SET_RMC_LEADER, strlen(CMD_SET_RMC_LEADER)) == 0) {
		int skip = strlen(CMD_SET_RMC_LEADER) + 1;
		bytes_written = wl_android_rmc_set_leader(net, (const char*)command+skip);
	}
	else if (strnicmp(command, CMD_SET_RMC_EVENT,
		strlen(CMD_SET_RMC_EVENT)) == 0) {
		bytes_written = wl_android_set_rmc_event(net, command);
	}
#endif /* WL_RELMCAST */
	else if (strnicmp(command, CMD_GET_SCSCAN, strlen(CMD_GET_SCSCAN)) == 0) {
		bytes_written = wl_android_get_singlecore_scan(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_SET_SCSCAN, strlen(CMD_SET_SCSCAN)) == 0) {
		bytes_written = wl_android_set_singlecore_scan(net, command);
	}
#ifdef TEST_TX_POWER_CONTROL
	else if (strnicmp(command, CMD_TEST_SET_TX_POWER,
		strlen(CMD_TEST_SET_TX_POWER)) == 0) {
		int skip = strlen(CMD_TEST_SET_TX_POWER) + 1;
		wl_android_set_tx_power(net, (const char*)command+skip);
	}
	else if (strnicmp(command, CMD_TEST_GET_TX_POWER,
		strlen(CMD_TEST_GET_TX_POWER)) == 0) {
		wl_android_get_tx_power(net, command, priv_cmd.total_len);
	}
#endif /* TEST_TX_POWER_CONTROL */
	else if (strnicmp(command, CMD_SARLIMIT_TX_CONTROL,
		strlen(CMD_SARLIMIT_TX_CONTROL)) == 0) {
		int skip = strlen(CMD_SARLIMIT_TX_CONTROL) + 1;
		bytes_written = wl_android_set_sarlimit_txctrl(net, (const char*)command+skip);
	}
#ifdef SUPPORT_SET_TID
	else if (strnicmp(command, CMD_SET_TID, strlen(CMD_SET_TID)) == 0) {
		bytes_written = wl_android_set_tid(net, command);
	}
	else if (strnicmp(command, CMD_GET_TID, strlen(CMD_GET_TID)) == 0) {
		bytes_written = wl_android_get_tid(net, command, priv_cmd.total_len);
	}
#endif /* SUPPORT_SET_TID */
#endif /* CUSTOMER_HW4_PRIVATE_CMD */
#if defined(SUPPORT_HIDDEN_AP)
	else if (strnicmp(command, CMD_SET_HAPD_MAX_NUM_STA,
		strlen(CMD_SET_HAPD_MAX_NUM_STA)) == 0) {
		int skip = strlen(CMD_SET_HAPD_MAX_NUM_STA) + 3;
		wl_android_set_max_num_sta(net, (const char*)command+skip);
	}
	else if (strnicmp(command, CMD_SET_HAPD_SSID,
		strlen(CMD_SET_HAPD_SSID)) == 0) {
		int skip = strlen(CMD_SET_HAPD_SSID) + 3;
		wl_android_set_ssid(net, (const char*)command+skip);
	}
	else if (strnicmp(command, CMD_SET_HAPD_HIDE_SSID,
		strlen(CMD_SET_HAPD_HIDE_SSID)) == 0) {
		int skip = strlen(CMD_SET_HAPD_HIDE_SSID) + 1;
		wl_android_set_hide_ssid(net, (const char*)(command+skip));
	}
#endif /* SUPPORT_HIDDEN_AP */
#ifdef AUTOMOTIVE_FEATURE
	else if (strnicmp(command, CMD_HAPD_MAC_FILTER, strlen(CMD_HAPD_MAC_FILTER)) == 0) {
		int skip = strlen(CMD_HAPD_MAC_FILTER) + 1;
		wl_android_set_mac_address_filter(net, command+skip);
	}
#endif /* AUTOMOTIVE_FEATURE */
	else if (strnicmp(command, CMD_SETROAMMODE, strlen(CMD_SETROAMMODE)) == 0)
		bytes_written = wl_android_set_roam_mode(net, command);
#if defined(BCMFW_ROAM_ENABLE)
	else if (strnicmp(command, CMD_SET_ROAMPREF, strlen(CMD_SET_ROAMPREF)) == 0) {
		bytes_written = wl_android_set_roampref(net, command, priv_cmd.total_len);
	}
#endif /* BCMFW_ROAM_ENABLE */
	else if (strnicmp(command, CMD_MIRACAST, strlen(CMD_MIRACAST)) == 0)
		bytes_written = wl_android_set_miracast(net, command);
	else if (strnicmp(command, CMD_SETIBSSBEACONOUIDATA, strlen(CMD_SETIBSSBEACONOUIDATA)) == 0)
		bytes_written = wl_android_set_ibss_beacon_ouidata(net,
		command, priv_cmd.total_len);
#ifdef WLAIBSS
	else if (strnicmp(command, CMD_SETIBSSTXFAILEVENT,
		strlen(CMD_SETIBSSTXFAILEVENT)) == 0)
		bytes_written = wl_android_set_ibss_txfail_event(net, command, priv_cmd.total_len);
	else if (strnicmp(command, CMD_GET_IBSS_PEER_INFO_ALL,
		strlen(CMD_GET_IBSS_PEER_INFO_ALL)) == 0)
		bytes_written = wl_android_get_ibss_peer_info(net, command, priv_cmd.total_len,
			TRUE);
	else if (strnicmp(command, CMD_GET_IBSS_PEER_INFO,
		strlen(CMD_GET_IBSS_PEER_INFO)) == 0)
		bytes_written = wl_android_get_ibss_peer_info(net, command, priv_cmd.total_len,
			FALSE);
	else if (strnicmp(command, CMD_SETIBSSROUTETABLE,
		strlen(CMD_SETIBSSROUTETABLE)) == 0)
		bytes_written = wl_android_set_ibss_routetable(net, command);
	else if (strnicmp(command, CMD_SETIBSSAMPDU, strlen(CMD_SETIBSSAMPDU)) == 0)
		bytes_written = wl_android_set_ibss_ampdu(net, command, priv_cmd.total_len);
	else if (strnicmp(command, CMD_SETIBSSANTENNAMODE, strlen(CMD_SETIBSSANTENNAMODE)) == 0)
		bytes_written = wl_android_set_ibss_antenna(net, command, priv_cmd.total_len);
#endif /* WLAIBSS */
	else if (strnicmp(command, CMD_KEEP_ALIVE, strlen(CMD_KEEP_ALIVE)) == 0) {
		int skip = strlen(CMD_KEEP_ALIVE) + 1;
		bytes_written = wl_keep_alive_set(net, command + skip);
	}
	else if (strnicmp(command, CMD_ROAM_OFFLOAD, strlen(CMD_ROAM_OFFLOAD)) == 0) {
		int enable = *(command + strlen(CMD_ROAM_OFFLOAD) + 1) - '0';
		bytes_written = wl_cfg80211_enable_roam_offload(net, enable);
	}
	else if (strnicmp(command, CMD_INTERFACE_CREATE, strlen(CMD_INTERFACE_CREATE)) == 0) {
		char *name = (command + strlen(CMD_INTERFACE_CREATE) +1);
		WL_INFORM(("Creating %s interface\n", name));
		if (wl_cfg80211_add_if(wl_get_cfg(net), net, WL_IF_TYPE_STA,
				name, NULL) == NULL) {
			bytes_written = -ENODEV;
		} else {
			/* Return success */
			bytes_written = 0;
		}
	}
	else if (strnicmp(command, CMD_INTERFACE_DELETE, strlen(CMD_INTERFACE_DELETE)) == 0) {
		char *name = (command + strlen(CMD_INTERFACE_DELETE) +1);
		WL_INFORM(("Deleteing %s interface\n", name));
		bytes_written = wl_cfg80211_del_if(wl_get_cfg(net), net, NULL, name);
	}
	else if (strnicmp(command, CMD_GET_LINK_STATUS, strlen(CMD_GET_LINK_STATUS)) == 0) {
		bytes_written = wl_android_get_link_status(net, command, priv_cmd.total_len);
	}
#ifdef P2PRESP_WFDIE_SRC
	else if (strnicmp(command, CMD_P2P_SET_WFDIE_RESP,
		strlen(CMD_P2P_SET_WFDIE_RESP)) == 0) {
		int mode = *(command + strlen(CMD_P2P_SET_WFDIE_RESP) + 1) - '0';
		bytes_written = wl_android_set_wfdie_resp(net, mode);
	} else if (strnicmp(command, CMD_P2P_GET_WFDIE_RESP,
		strlen(CMD_P2P_GET_WFDIE_RESP)) == 0) {
		bytes_written = wl_android_get_wfdie_resp(net, command, priv_cmd.total_len);
	}
#endif /* P2PRESP_WFDIE_SRC */
	else if (strnicmp(command, CMD_DFS_AP_MOVE, strlen(CMD_DFS_AP_MOVE)) == 0) {
		char *data = (command + strlen(CMD_DFS_AP_MOVE) +1);
		bytes_written = wl_cfg80211_dfs_ap_move(net, data, command, priv_cmd.total_len);
	}
#ifdef WBTEXT
	else if (strnicmp(command, CMD_WBTEXT_ENABLE, strlen(CMD_WBTEXT_ENABLE)) == 0) {
		bytes_written = wl_android_wbtext(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_WBTEXT_PROFILE_CONFIG,
			strlen(CMD_WBTEXT_PROFILE_CONFIG)) == 0) {
		char *data = (command + strlen(CMD_WBTEXT_PROFILE_CONFIG) + 1);
		bytes_written = wl_cfg80211_wbtext_config(net, data, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_WBTEXT_WEIGHT_CONFIG,
			strlen(CMD_WBTEXT_WEIGHT_CONFIG)) == 0) {
		char *data = (command + strlen(CMD_WBTEXT_WEIGHT_CONFIG) + 1);
		bytes_written = wl_cfg80211_wbtext_weight_config(net, data,
				command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_WBTEXT_TABLE_CONFIG,
			strlen(CMD_WBTEXT_TABLE_CONFIG)) == 0) {
		char *data = (command + strlen(CMD_WBTEXT_TABLE_CONFIG) + 1);
		bytes_written = wl_cfg80211_wbtext_table_config(net, data,
				command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_WBTEXT_DELTA_CONFIG,
			strlen(CMD_WBTEXT_DELTA_CONFIG)) == 0) {
		char *data = (command + strlen(CMD_WBTEXT_DELTA_CONFIG) + 1);
		bytes_written = wl_cfg80211_wbtext_delta_config(net, data,
				command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_WBTEXT_BTM_TIMER_THRESHOLD,
			strlen(CMD_WBTEXT_BTM_TIMER_THRESHOLD)) == 0) {
		bytes_written = wl_cfg80211_wbtext_btm_timer_threshold(net, command,
			priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_WBTEXT_BTM_DELTA,
			strlen(CMD_WBTEXT_BTM_DELTA)) == 0) {
		bytes_written = wl_cfg80211_wbtext_btm_delta(net, command,
			priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_WBTEXT_ESTM_ENABLE,
			strlen(CMD_WBTEXT_ESTM_ENABLE)) == 0) {
		bytes_written = wl_cfg80211_wbtext_estm_enable(net, command,
			priv_cmd.total_len);
	}
#endif /* WBTEXT */
#ifdef SET_RPS_CPUS
	else if (strnicmp(command, CMD_RPSMODE, strlen(CMD_RPSMODE)) == 0) {
		bytes_written = wl_android_set_rps_cpus(net, command);
	}
#endif /* SET_RPS_CPUS */
#ifdef WLWFDS
	else if (strnicmp(command, CMD_ADD_WFDS_HASH, strlen(CMD_ADD_WFDS_HASH)) == 0) {
		bytes_written = wl_android_set_wfds_hash(net, command, 1);
	}
	else if (strnicmp(command, CMD_DEL_WFDS_HASH, strlen(CMD_DEL_WFDS_HASH)) == 0) {
		bytes_written = wl_android_set_wfds_hash(net, command, 0);
	}
#endif /* WLWFDS */
#ifdef BT_WIFI_HANDOVER
	else if (strnicmp(command, CMD_TBOW_TEARDOWN, strlen(CMD_TBOW_TEARDOWN)) == 0) {
	    bytes_written = wl_tbow_teardown(net);
	}
#endif /* BT_WIFI_HANDOVER */
#ifdef CUSTOMER_HW4_PRIVATE_CMD
#ifdef FCC_PWR_LIMIT_2G
	else if (strnicmp(command, CMD_GET_FCC_PWR_LIMIT_2G,
		strlen(CMD_GET_FCC_PWR_LIMIT_2G)) == 0) {
		bytes_written = wl_android_get_fcc_pwr_limit_2g(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_SET_FCC_PWR_LIMIT_2G,
		strlen(CMD_SET_FCC_PWR_LIMIT_2G)) == 0) {
		bytes_written = wl_android_set_fcc_pwr_limit_2g(net, command);
	}
#endif /* FCC_PWR_LIMIT_2G */
	else if (strnicmp(command, CMD_GET_STA_INFO, strlen(CMD_GET_STA_INFO)) == 0) {
		bytes_written = wl_cfg80211_get_sta_info(net, command, priv_cmd.total_len);
	}
#endif /* CUSTOMER_HW4_PRIVATE_CMD */
	else if (strnicmp(command, CMD_MURX_BFE_CAP,
			strlen(CMD_MURX_BFE_CAP)) == 0) {
#ifdef WL_MURX
		uint val = *(command + strlen(CMD_MURX_BFE_CAP) + 1) - '0';
		bytes_written = wl_android_murx_bfe_cap(net, val);
#else
		return BCME_UNSUPPORTED;
#endif /* WL_MURX */
	}
#ifdef SUPPORT_AP_HIGHER_BEACONRATE
	else if (strnicmp(command, CMD_GET_AP_BASICRATE, strlen(CMD_GET_AP_BASICRATE)) == 0) {
		bytes_written = wl_android_get_ap_basicrate(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_SET_AP_BEACONRATE, strlen(CMD_SET_AP_BEACONRATE)) == 0) {
		bytes_written = wl_android_set_ap_beaconrate(net, command);
	}
#endif /* SUPPORT_AP_HIGHER_BEACONRATE */
#ifdef SUPPORT_AP_RADIO_PWRSAVE
	else if (strnicmp(command, CMD_SET_AP_RPS_PARAMS, strlen(CMD_SET_AP_RPS_PARAMS)) == 0) {
		bytes_written = wl_android_set_ap_rps_params(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_SET_AP_RPS, strlen(CMD_SET_AP_RPS)) == 0) {
		bytes_written = wl_android_set_ap_rps(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_GET_AP_RPS, strlen(CMD_GET_AP_RPS)) == 0) {
		bytes_written = wl_android_get_ap_rps(net, command, priv_cmd.total_len);
	}
#endif /* SUPPORT_AP_RADIO_PWRSAVE */
#ifdef SUPPORT_RSSI_SUM_REPORT
	else if (strnicmp(command, CMD_SET_RSSI_LOGGING, strlen(CMD_SET_RSSI_LOGGING)) == 0) {
		bytes_written = wl_android_set_rssi_logging(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_GET_RSSI_LOGGING, strlen(CMD_GET_RSSI_LOGGING)) == 0) {
		bytes_written = wl_android_get_rssi_logging(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_GET_RSSI_PER_ANT, strlen(CMD_GET_RSSI_PER_ANT)) == 0) {
		bytes_written = wl_android_get_rssi_per_ant(net, command, priv_cmd.total_len);
	}
#endif /* SUPPORT_RSSI_SUM_REPORT */
#if defined(DHD_ENABLE_BIGDATA_LOGGING)
	else if (strnicmp(command, CMD_GET_BSS_INFO, strlen(CMD_GET_BSS_INFO)) == 0) {
		bytes_written = wl_cfg80211_get_bss_info(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_GET_ASSOC_REJECT_INFO, strlen(CMD_GET_ASSOC_REJECT_INFO))
			== 0) {
		bytes_written = wl_cfg80211_get_connect_failed_status(net, command,
				priv_cmd.total_len);
	}
#endif /* DHD_ENABLE_BIGDATA_LOGGING */
#if defined(SUPPORT_RANDOM_MAC_SCAN)
	else if (strnicmp(command, ENABLE_RANDOM_MAC, strlen(ENABLE_RANDOM_MAC)) == 0) {
		bytes_written = wl_cfg80211_set_random_mac(net, TRUE);
	} else if (strnicmp(command, DISABLE_RANDOM_MAC, strlen(DISABLE_RANDOM_MAC)) == 0) {
		bytes_written = wl_cfg80211_set_random_mac(net, FALSE);
	}
#endif /* SUPPORT_RANDOM_MAC_SCAN */
#ifdef DHD_BANDSTEER
	else if (strnicmp(command, CMD_BANDSTEER, strlen(CMD_BANDSTEER)) == 0) {
		bytes_written = wl_android_set_bandsteer(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_BANDSTEER_TRIGGER, strlen(CMD_BANDSTEER_TRIGGER)) == 0) {
		uint8 *p = command + strlen(CMD_BANDSTEER_TRIGGER)+1;
		struct ether_addr ea;
		char eabuf[ETHER_ADDR_STR_LEN];
		bytes_written = 0;

		bzero((char *)eabuf, ETHER_ADDR_STR_LEN);
		strncpy(eabuf, p, ETHER_ADDR_STR_LEN - 1);

		if (!bcm_ether_atoe(eabuf, &ea)) {
			DHD_ERROR(("BANDSTEER: ERROR while parsing macaddr cmd %s - ignored\n",
					command));
			return BCME_BADARG;
		}
		bytes_written = dhd_bandsteer_trigger_bandsteer(net, ea.octet);
	}
#endif /* DHD_BANDSTEER */
#ifdef ENABLE_HOGSQS
	else if (strnicmp(command, CMD_AP_HOGSQS, strlen(CMD_AP_HOGSQS)) == 0) {
		bytes_written = wl_android_hogsqs(net, command, priv_cmd.total_len);
	}
#endif /* ENABLE_HOGSQS */
#ifdef WL_NATOE
	else if (strnicmp(command, CMD_NATOE, strlen(CMD_NATOE)) == 0) {
		bytes_written = wl_android_process_natoe_cmd(net, command,
				priv_cmd.total_len);
	}
#endif /* WL_NATOE */
#ifdef CONNECTION_STATISTICS
	else if (strnicmp(command, CMD_GET_CONNECTION_STATS,
		strlen(CMD_GET_CONNECTION_STATS)) == 0) {
		bytes_written = wl_android_get_connection_stats(net, command,
			priv_cmd.total_len);
	}
#endif // endif
#ifdef DHD_LOG_DUMP
	else if (strnicmp(command, CMD_NEW_DEBUG_PRINT_DUMP,
		strlen(CMD_NEW_DEBUG_PRINT_DUMP)) == 0) {
		dhd_pub_t *dhdp = wl_cfg80211_get_dhdp(net);
		/* check whether it has more command */
		if (strnicmp(command + strlen(CMD_NEW_DEBUG_PRINT_DUMP), " ", 1) == 0) {
			/* compare unwanted/disconnected command */
			if (strnicmp(command + strlen(CMD_NEW_DEBUG_PRINT_DUMP) + 1,
				SUBCMD_UNWANTED, strlen(SUBCMD_UNWANTED)) == 0) {
				dhd_log_dump_trigger(dhdp, CMD_UNWANTED);
			} else if (strnicmp(command + strlen(CMD_NEW_DEBUG_PRINT_DUMP) + 1,
				SUBCMD_DISCONNECTED, strlen(SUBCMD_DISCONNECTED)) == 0) {
				dhd_log_dump_trigger(dhdp, CMD_DISCONNECTED);
			} else {
				dhd_log_dump_trigger(dhdp, CMD_DEFAULT);
			}
		} else {
			dhd_log_dump_trigger(dhdp, CMD_DEFAULT);
		}
	}
#endif /* DHD_LOG_DUMP */
#ifdef DHD_STATUS_LOGGING
	else if (strnicmp(command, CMD_DUMP_STATUS_LOG, strlen(CMD_DUMP_STATUS_LOG)) == 0) {
		dhd_statlog_dump_scr(wl_cfg80211_get_dhdp(net));
	}
	else if (strnicmp(command, CMD_QUERY_STATUS_LOG, strlen(CMD_QUERY_STATUS_LOG)) == 0) {
		dhd_pub_t *dhdp = wl_cfg80211_get_dhdp(net);
		bytes_written = dhd_statlog_query(dhdp, command, priv_cmd.total_len);
	}
#endif /* DHD_STATUS_LOGGING */
#ifdef SET_PCIE_IRQ_CPU_CORE
	else if (strnicmp(command, CMD_PCIE_IRQ_CORE, strlen(CMD_PCIE_IRQ_CORE)) == 0) {
		int affinity_cmd = *(command + strlen(CMD_PCIE_IRQ_CORE) + 1) - '0';
		wl_android_set_irq_cpucore(net, affinity_cmd);
	}
#endif /* SET_PCIE_IRQ_CPU_CORE */
#if defined(DHD_HANG_SEND_UP_TEST)
	else if (strnicmp(command, CMD_MAKE_HANG, strlen(CMD_MAKE_HANG)) == 0) {
		int skip = strlen(CMD_MAKE_HANG) + 1;
		wl_android_make_hang_with_reason(net, (const char*)command+skip);
	}
#endif /* DHD_HANG_SEND_UP_TEST */
#ifdef SUPPORT_LQCM
	else if (strnicmp(command, CMD_SET_LQCM_ENABLE, strlen(CMD_SET_LQCM_ENABLE)) == 0) {
		int lqcm_enable = *(command + strlen(CMD_SET_LQCM_ENABLE) + 1) - '0';
		bytes_written = wl_android_lqcm_enable(net, lqcm_enable);
	}
	else if (strnicmp(command, CMD_GET_LQCM_REPORT,
			strlen(CMD_GET_LQCM_REPORT)) == 0) {
		bytes_written = wl_android_get_lqcm_report(net, command,
			priv_cmd.total_len);
	}
#endif // endif
	else if (strnicmp(command, CMD_GET_SNR, strlen(CMD_GET_SNR)) == 0) {
		bytes_written = wl_android_get_snr(net, command, priv_cmd.total_len);
	}
#ifdef WLADPS_PRIVATE_CMD
	else if (strnicmp(command, CMD_SET_ADPS, strlen(CMD_SET_ADPS)) == 0) {
		int skip = strlen(CMD_SET_ADPS) + 1;
		bytes_written = wl_android_set_adps_mode(net, (const char*)command+skip);
	}
	else if (strnicmp(command, CMD_GET_ADPS, strlen(CMD_GET_ADPS)) == 0) {
		bytes_written = wl_android_get_adps_mode(net, command, priv_cmd.total_len);
	}
#endif /* WLADPS_PRIVATE_CMD */
#ifdef DHD_PKT_LOGGING
	else if (strnicmp(command, CMD_PKTLOG_FILTER_ENABLE,
		strlen(CMD_PKTLOG_FILTER_ENABLE)) == 0) {
		bytes_written = wl_android_pktlog_filter_enable(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_PKTLOG_FILTER_DISABLE,
		strlen(CMD_PKTLOG_FILTER_DISABLE)) == 0) {
		bytes_written = wl_android_pktlog_filter_disable(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_PKTLOG_FILTER_PATTERN_ENABLE,
		strlen(CMD_PKTLOG_FILTER_PATTERN_ENABLE)) == 0) {
		bytes_written =
			wl_android_pktlog_filter_pattern_enable(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_PKTLOG_FILTER_PATTERN_DISABLE,
		strlen(CMD_PKTLOG_FILTER_PATTERN_DISABLE)) == 0) {
		bytes_written =
			wl_android_pktlog_filter_pattern_disable(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_PKTLOG_FILTER_ADD, strlen(CMD_PKTLOG_FILTER_ADD)) == 0) {
		bytes_written = wl_android_pktlog_filter_add(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_PKTLOG_FILTER_DEL, strlen(CMD_PKTLOG_FILTER_DEL)) == 0) {
		bytes_written = wl_android_pktlog_filter_del(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_PKTLOG_FILTER_INFO, strlen(CMD_PKTLOG_FILTER_INFO)) == 0) {
		bytes_written = wl_android_pktlog_filter_info(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_PKTLOG_START, strlen(CMD_PKTLOG_START)) == 0) {
		bytes_written = wl_android_pktlog_start(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_PKTLOG_STOP, strlen(CMD_PKTLOG_STOP)) == 0) {
		bytes_written = wl_android_pktlog_stop(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_PKTLOG_FILTER_EXIST, strlen(CMD_PKTLOG_FILTER_EXIST)) == 0) {
		bytes_written = wl_android_pktlog_filter_exist(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_PKTLOG_MINMIZE_ENABLE,
		strlen(CMD_PKTLOG_MINMIZE_ENABLE)) == 0) {
		bytes_written = wl_android_pktlog_minmize_enable(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_PKTLOG_MINMIZE_DISABLE,
		strlen(CMD_PKTLOG_MINMIZE_DISABLE)) == 0) {
		bytes_written = wl_android_pktlog_minmize_disable(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_PKTLOG_CHANGE_SIZE,
		strlen(CMD_PKTLOG_CHANGE_SIZE)) == 0) {
		bytes_written = wl_android_pktlog_change_size(net, command, priv_cmd.total_len);
	}
#endif /* DHD_PKT_LOGGING */
	else if (strnicmp(command, CMD_DEBUG_VERBOSE, strlen(CMD_DEBUG_VERBOSE)) == 0) {
		int verbose_level = *(command + strlen(CMD_DEBUG_VERBOSE) + 1) - '0';
		bytes_written = wl_cfg80211_set_dbg_verbose(net, verbose_level);
	}
#ifdef DHD_EVENT_LOG_FILTER
	else if (strnicmp(command, CMD_EWP_FILTER,
		strlen(CMD_EWP_FILTER)) == 0) {
		bytes_written = wl_android_ewp_filter(net, command, priv_cmd.total_len);
	}
#endif /* DHD_EVENT_LOG_FILTER */
#ifdef WL_BCNRECV
	else if (strnicmp(command, CMD_BEACON_RECV,
		strlen(CMD_BEACON_RECV)) == 0) {
		char *data = (command + strlen(CMD_BEACON_RECV) + 1);
		bytes_written = wl_android_bcnrecv_config(net,
				data, priv_cmd.total_len);
	}
#endif /* WL_BCNRECV */
#ifdef WL_MBO
	else if (strnicmp(command, CMD_MBO, strlen(CMD_MBO)) == 0) {
		bytes_written = wl_android_process_mbo_cmd(net, command,
			priv_cmd.total_len);
	}
#endif /* WL_MBO */
#ifdef WL_CAC_TS
	else if (strnicmp(command, CMD_CAC_TSPEC,
		strlen(CMD_CAC_TSPEC)) == 0) {
		char *data = (command + strlen(CMD_CAC_TSPEC) + 1);
		bytes_written = wl_android_cac_ts_config(net,
				data, priv_cmd.total_len);
	}
#endif /* WL_CAC_TS */
#ifdef WL_GET_CU
	else if (strnicmp(command, CMD_GET_CHAN_UTIL,
		strlen(CMD_GET_CHAN_UTIL)) == 0) {
		bytes_written = wl_android_get_channel_util(net,
			command, priv_cmd.total_len);
	}
#endif /* WL_GET_CU */
	else if (strnicmp(command, CMD_CHANNEL_WIDTH, strlen(CMD_CHANNEL_WIDTH)) == 0) {
		bytes_written = wl_android_set_channel_width(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_TRANSITION_DISABLE, strlen(CMD_TRANSITION_DISABLE)) == 0) {
		int transition_disabled = *(command + strlen(CMD_TRANSITION_DISABLE) + 1) - '0';
		bytes_written = wl_cfg80211_set_transition_mode(net, transition_disabled);
	}
	else if (strnicmp(command, CMD_SAE_PWE, strlen(CMD_SAE_PWE)) == 0) {
		u8 sae_pwe = *(command + strlen(CMD_SAE_PWE) + 1) - '0';
		bytes_written = wl_cfg80211_set_sae_pwe(net, sae_pwe);
	}
	else if (strnicmp(command, CMD_MAXASSOC, strlen(CMD_MAXASSOC)) == 0) {
		bytes_written = wl_android_set_maxassoc_limit(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_SCAN_PROTECT_BSS, strlen(CMD_SCAN_PROTECT_BSS)) == 0) {
		bytes_written = wl_android_scan_protect_bss(net, command, priv_cmd.total_len);
	}
	else {
		DHD_ERROR(("Unknown PRIVATE command %s - ignored\n", command));
		bytes_written = scnprintf(command, sizeof("FAIL"), "FAIL");
	}

	return bytes_written;
}

int wl_android_init(void)
{
	int ret = 0;

#ifdef ENABLE_INSMOD_NO_FW_LOAD
	dhd_download_fw_on_driverload = FALSE;
#endif /* ENABLE_INSMOD_NO_FW_LOAD */
	if (!iface_name[0]) {
		bzero(iface_name, IFNAMSIZ);
		bcm_strncpy_s(iface_name, IFNAMSIZ, "wlan", IFNAMSIZ);
	}

#ifdef CUSTOMER_HW4_DEBUG
	g_assert_type = 1;
#endif /* CUSTOMER_HW4_DEBUG */

#ifdef WL_GENL
	wl_genl_init();
#endif // endif
	wl_netlink_init();

	return ret;
}

int wl_android_exit(void)
{
	int ret = 0;
	struct io_cfg *cur, *q;

#ifdef WL_GENL
	wl_genl_deinit();
#endif /* WL_GENL */
	wl_netlink_deinit();

	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	list_for_each_entry_safe(cur, q, &miracast_resume_list, list) {
		GCC_DIAGNOSTIC_POP();
		list_del(&cur->list);
		kfree(cur);
	}

	return ret;
}

void wl_android_post_init(void)
{

#ifdef ENABLE_4335BT_WAR
	bcm_bt_unlock(lock_cookie_wifi);
	printk("wl_android_post_init: btlock released\n");
#endif /* ENABLE_4335BT_WAR */

	if (!dhd_download_fw_on_driverload)
		g_wifi_on = FALSE;
}

#ifdef WL_GENL
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0))
static int
wl_genl_register_family_with_ops_groups(struct genl_family *family,
	const struct genl_ops *ops, size_t n_ops,
	const struct genl_multicast_group *mcgrps,
	size_t n_mcgrps)
{
	family->module = THIS_MODULE;
	family->ops = ops;
	family->n_ops = n_ops;
	family->mcgrps = mcgrps;
	family->n_mcgrps = n_mcgrps;
	return genl_register_family(family);
}
#endif // endif

/* Generic Netlink Initializaiton */
static int wl_genl_init(void)
{
	int ret;

	WL_DBG(("GEN Netlink Init\n\n"));

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0))
	/* register new family */
	ret = genl_register_family(&wl_genl_family);
	if (ret != 0)
		goto failure;

	/* register functions (commands) of the new family */
	ret = genl_register_ops(&wl_genl_family, &wl_genl_ops);
	if (ret != 0) {
		WL_ERR(("register ops failed: %i\n", ret));
		genl_unregister_family(&wl_genl_family);
		goto failure;
	}

	ret = genl_register_mc_group(&wl_genl_family, &wl_genl_mcast);
#elif ((LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)) && \
	(LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)))
	ret = genl_register_family_with_ops_groups(&wl_genl_family, wl_genl_ops, wl_genl_mcast);
#else
	ret = wl_genl_register_family_with_ops_groups(&wl_genl_family, wl_genl_ops,
		ARRAY_SIZE(wl_genl_ops), wl_genl_mcast, ARRAY_SIZE(wl_genl_mcast));
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0) */
	if (ret != 0) {
		WL_ERR(("register mc_group failed: %i\n", ret));
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0))
		genl_unregister_ops(&wl_genl_family, &wl_genl_ops);
#endif // endif
		genl_unregister_family(&wl_genl_family);
		goto failure;
	}

	return 0;

failure:
	WL_ERR(("Registering Netlink failed!!\n"));
	return -1;
}

/* Generic netlink deinit */
static int wl_genl_deinit(void)
{

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0))
	if (genl_unregister_ops(&wl_genl_family, &wl_genl_ops) < 0)
		WL_ERR(("Unregister wl_genl_ops failed\n"));
#endif // endif
	if (genl_unregister_family(&wl_genl_family) < 0)
		WL_ERR(("Unregister wl_genl_ops failed\n"));

	return 0;
}

s32 wl_event_to_bcm_event(u16 event_type)
{
	u16 event = -1;

	switch (event_type) {
		case WLC_E_SERVICE_FOUND:
			event = BCM_E_SVC_FOUND;
			break;
		case WLC_E_P2PO_ADD_DEVICE:
			event = BCM_E_DEV_FOUND;
			break;
		case WLC_E_P2PO_DEL_DEVICE:
			event = BCM_E_DEV_LOST;
			break;
	/* Above events are supported from BCM Supp ver 47 Onwards */
#ifdef BT_WIFI_HANDOVER
		case WLC_E_BT_WIFI_HANDOVER_REQ:
			event = BCM_E_DEV_BT_WIFI_HO_REQ;
			break;
#endif /* BT_WIFI_HANDOVER */

		default:
			WL_ERR(("Event not supported\n"));
	}

	return event;
}

s32
wl_genl_send_msg(
	struct net_device *ndev,
	u32 event_type,
	const u8 *buf,
	u16 len,
	u8 *subhdr,
	u16 subhdr_len)
{
	int ret = 0;
	struct sk_buff *skb;
	void *msg;
	u32 attr_type = 0;
	bcm_event_hdr_t *hdr = NULL;
	int mcast = 1; /* By default sent as mutlicast type */
	int pid = 0;
	u8 *ptr = NULL, *p = NULL;
	u32 tot_len = sizeof(bcm_event_hdr_t) + subhdr_len + len;
	gfp_t kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);

	WL_DBG(("Enter \n"));

	/* Decide between STRING event and Data event */
	if (event_type == 0)
		attr_type = BCM_GENL_ATTR_STRING;
	else
		attr_type = BCM_GENL_ATTR_MSG;

	skb = genlmsg_new(NLMSG_GOODSIZE, kflags);
	if (skb == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	msg = genlmsg_put(skb, 0, 0, &wl_genl_family, 0, BCM_GENL_CMD_MSG);
	if (msg == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	if (attr_type == BCM_GENL_ATTR_STRING) {
		/* Add a BCM_GENL_MSG attribute. Since it is specified as a string.
		 * make sure it is null terminated
		 */
		if (subhdr || subhdr_len) {
			WL_ERR(("No sub hdr support for the ATTR STRING type \n"));
			ret =  -EINVAL;
			goto out;
		}

		ret = nla_put_string(skb, BCM_GENL_ATTR_STRING, buf);
		if (ret != 0) {
			WL_ERR(("nla_put_string failed\n"));
			goto out;
		}
	} else {
		/* ATTR_MSG */

		/* Create a single buffer for all */
		p = ptr = (u8 *)MALLOCZ(cfg->osh, tot_len);
		if (!ptr) {
			ret = -ENOMEM;
			WL_ERR(("ENOMEM!!\n"));
			goto out;
		}

		/* Include the bcm event header */
		hdr = (bcm_event_hdr_t *)ptr;
		hdr->event_type = wl_event_to_bcm_event(event_type);
		hdr->len = len + subhdr_len;
		ptr += sizeof(bcm_event_hdr_t);

		/* Copy subhdr (if any) */
		if (subhdr && subhdr_len) {
			memcpy(ptr, subhdr, subhdr_len);
			ptr += subhdr_len;
		}

		/* Copy the data */
		if (buf && len) {
			memcpy(ptr, buf, len);
		}

		ret = nla_put(skb, BCM_GENL_ATTR_MSG, tot_len, p);
		if (ret != 0) {
			WL_ERR(("nla_put_string failed\n"));
			goto out;
		}
	}

	if (mcast) {
		int err = 0;
		/* finalize the message */
		genlmsg_end(skb, msg);
		/* NETLINK_CB(skb).dst_group = 1; */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0)
		if ((err = genlmsg_multicast(skb, 0, wl_genl_mcast.id, GFP_ATOMIC)) < 0)
#else
		if ((err = genlmsg_multicast(&wl_genl_family, skb, 0, 0, GFP_ATOMIC)) < 0)
#endif // endif
			WL_ERR(("genlmsg_multicast for attr(%d) failed. Error:%d \n",
				attr_type, err));
		else
			WL_DBG(("Multicast msg sent successfully. attr_type:%d len:%d \n",
				attr_type, tot_len));
	} else {
		NETLINK_CB(skb).dst_group = 0; /* Not in multicast group */

		/* finalize the message */
		genlmsg_end(skb, msg);

		/* send the message back */
		if (genlmsg_unicast(&init_net, skb, pid) < 0)
			WL_ERR(("genlmsg_unicast failed\n"));
	}

out:
	if (p) {
		MFREE(cfg->osh, p, tot_len);
	}
	if (ret)
		nlmsg_free(skb);

	return ret;
}

static s32
wl_genl_handle_msg(
	struct sk_buff *skb,
	struct genl_info *info)
{
	struct nlattr *na;
	u8 *data = NULL;

	WL_DBG(("Enter \n"));

	if (info == NULL) {
		return -EINVAL;
	}

	na = info->attrs[BCM_GENL_ATTR_MSG];
	if (!na) {
		WL_ERR(("nlattribute NULL\n"));
		return -EINVAL;
	}

	data = (char *)nla_data(na);
	if (!data) {
		WL_ERR(("Invalid data\n"));
		return -EINVAL;
	} else {
		/* Handle the data */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 7, 0)) || defined(WL_COMPAT_WIRELESS)
		WL_DBG(("%s: Data received from pid (%d) \n", __func__,
			info->snd_pid));
#else
		WL_DBG(("%s: Data received from pid (%d) \n", __func__,
			info->snd_portid));
#endif /* (LINUX_VERSION < VERSION(3, 7, 0) || WL_COMPAT_WIRELESS */
	}

	return 0;
}
#endif /* WL_GENL */

int wl_fatal_error(void * wl, int rc)
{
	return FALSE;
}

#if defined(BT_OVER_SDIO)
void
wl_android_set_wifi_on_flag(bool enable)
{
	g_wifi_on = enable;
}
#endif /* BT_OVER_SDIO */

#ifdef WL_STATIC_IF
struct net_device *
wl_cfg80211_register_static_if(struct bcm_cfg80211 *cfg, u16 iftype, char *ifname, int ifidx)
{
	struct net_device *ndev;
	struct wireless_dev *wdev = NULL;
	u8 mac_addr[ETH_ALEN];
	struct net_device *primary_ndev;

	WL_INFORM_MEM(("[STATIC_IF] Enter (%s) iftype:%d\n", ifname, iftype));

	if (!cfg) {
		WL_ERR(("cfg null\n"));
		return NULL;
	}

	/* Use primary mac with locally admin bit set */
	primary_ndev = bcmcfg_to_prmry_ndev(cfg);
	(void)memcpy_s(mac_addr, ETH_ALEN, primary_ndev->dev_addr, ETH_ALEN);
	mac_addr[0] |= 0x02;

	ndev = wl_cfg80211_allocate_if(cfg, ifidx, ifname, mac_addr,
		WL_BSSIDX_MAX, NULL);
	if (unlikely(!ndev)) {
		WL_ERR(("Failed to allocate static_if\n"));
		goto fail;
	}
	wdev = (struct wireless_dev *)MALLOCZ(cfg->osh, sizeof(*wdev));
	if (unlikely(!wdev)) {
		WL_ERR(("Failed to allocate wdev for static_if\n"));
		goto fail;
	}

	wdev->wiphy = cfg->wdev->wiphy;
	wdev->iftype = iftype;

	ndev->ieee80211_ptr = wdev;
	SET_NETDEV_DEV(ndev, wiphy_dev(wdev->wiphy));
	wdev->netdev = ndev;

	if (wl_cfg80211_register_if(cfg, ifidx,
		ndev, TRUE) != BCME_OK) {
		WL_ERR(("ndev registration failed!\n"));
		goto fail;
	}

	cfg->static_ndev[ifidx - DHD_MAX_IFS] = ndev;
	cfg->static_ndev_state[ifidx - DHD_MAX_IFS] = NDEV_STATE_OS_IF_CREATED;
	wl_cfg80211_update_iflist_info(cfg, ndev, ifidx, NULL, WL_BSSIDX_MAX,
		ifname, NDEV_STATE_OS_IF_CREATED);
	WL_INFORM_MEM(("Static I/F (%s) Registered\n", ndev->name));
	return ndev;

fail:
	wl_cfg80211_remove_if(cfg, ifidx, ndev, false);
	return NULL;
}

void
wl_cfg80211_unregister_static_if(struct bcm_cfg80211 *cfg)
{
	int i = 0;
	WL_INFORM_MEM(("[STATIC_IF] Enter\n"));
	for (i = 0; i < DHD_NUM_STATIC_IFACES; i++) {
		if (!cfg || !cfg->static_ndev[i]) {
			WL_ERR(("invalid input\n"));
			continue;
		}

		/* wdev free will happen from notifier context */
		/* free_netdev(cfg->static_ndev);
		*/
		unregister_netdev(cfg->static_ndev[i]);
	}
}

s32
wl_cfg80211_static_if_open(struct net_device *net)
{
	struct wireless_dev *wdev = NULL;
	struct bcm_cfg80211 *cfg = wl_get_cfg(net);
	struct net_device *primary_ndev = bcmcfg_to_prmry_ndev(cfg);
	u16 iftype = net->ieee80211_ptr ? net->ieee80211_ptr->iftype : 0;
	u16 wl_iftype, wl_mode;

	WL_INFORM_MEM(("[STATIC_IF] dev_open ndev %p and wdev %p\n", net, net->ieee80211_ptr));
	ASSERT(is_static_iface(cfg, net));

	if (cfg80211_to_wl_iftype(iftype, &wl_iftype, &wl_mode) <  0) {
		return BCME_ERROR;
	}
	if (static_if_ndev_get_state(cfg, net) != NDEV_STATE_FW_IF_CREATED) {
		wdev = wl_cfg80211_add_if(cfg, primary_ndev, wl_iftype, net->name, NULL);
		if (!wdev) {
			WL_ERR(("[STATIC_IF] wdev is NULL, can't proceed"));
			return BCME_ERROR;
		}
	} else {
		WL_INFORM_MEM(("Fw IF for static netdev already created\n"));
	}

	return BCME_OK;
}

s32
wl_cfg80211_static_if_close(struct net_device *net)
{
	int ret = BCME_OK;
	struct bcm_cfg80211 *cfg = wl_get_cfg(net);
	struct net_device *primary_ndev = bcmcfg_to_prmry_ndev(cfg);

	if (static_if_ndev_get_state(cfg, net) == NDEV_STATE_FW_IF_CREATED) {
		if (mutex_is_locked(&cfg->if_sync) == TRUE) {
			ret = _wl_cfg80211_del_if(cfg, primary_ndev, net->ieee80211_ptr, net->name);
		} else {
			ret = wl_cfg80211_del_if(cfg, primary_ndev, net->ieee80211_ptr, net->name);
		}

		if (unlikely(ret)) {
			WL_ERR(("Del iface failed for static_if %d\n", ret));
		}
	}

	return ret;
}
struct net_device *
wl_cfg80211_post_static_ifcreate(struct bcm_cfg80211 *cfg,
	wl_if_event_info *event, u8 *addr, s32 iface_type, const char *iface_name)
{
	struct net_device *new_ndev = NULL;
	struct wireless_dev *wdev = NULL;

	int iface_num = 0;
	/* Checks if iface number returned is valid or not */
	if ((iface_num = get_iface_num(iface_name, cfg)) < 0) {
		return NULL;
	}

	WL_INFORM_MEM(("Updating static iface after Fw IF create \n"));

	new_ndev = cfg->static_ndev[iface_num];
	if (new_ndev) {
		wdev = new_ndev->ieee80211_ptr;
		ASSERT(wdev);
		wdev->iftype = iface_type;
		(void)memcpy_s(new_ndev->dev_addr, ETH_ALEN, addr, ETH_ALEN);
	}

	cfg->static_ndev_state[iface_num] = NDEV_STATE_FW_IF_CREATED;
	wl_cfg80211_update_iflist_info(cfg, new_ndev, event->ifidx, addr, event->bssidx,
		event->name, NDEV_STATE_FW_IF_CREATED);
	return new_ndev;
}
s32
wl_cfg80211_post_static_ifdel(struct bcm_cfg80211 *cfg, struct net_device *ndev)
{
	int iface_num = 0;
	if ((iface_num = get_iface_num(ndev->name, cfg)) < 0) {
		return BCME_ERROR;
	}

	cfg->static_ndev_state[iface_num] = NDEV_STATE_FW_IF_DELETED;
	wl_cfg80211_update_iflist_info(cfg, ndev, (DHD_MAX_IFS + iface_num), NULL,
	WL_BSSIDX_MAX, NULL, NDEV_STATE_FW_IF_DELETED);
	wl_cfg80211_clear_per_bss_ies(cfg, ndev->ieee80211_ptr);
	wl_dealloc_netinfo_by_wdev(cfg, ndev->ieee80211_ptr);
	return BCME_OK;
}
#endif /* WL_STATIC_IF */

#ifdef WBTEXT
static int
wlc_wbtext_get_roam_prof(struct net_device *ndev, wl_roamprof_band_t *rp,
	uint8 band, uint8 *roam_prof_ver, uint8 *roam_prof_size)
{
	int err = BCME_OK;
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	u8 *ioctl_buf = NULL;

	ioctl_buf = (u8 *)MALLOCZ(cfg->osh, WLC_IOCTL_MEDLEN);
	if (unlikely(!ioctl_buf)) {
		WL_ERR(("%s: failed to allocate memory\n", __func__));
		err =  -ENOMEM;
		goto exit;
	}
	rp->v1.band = band;
	rp->v1.len = 0;
	/* Getting roam profile from fw */
	if ((err = wldev_iovar_getbuf(ndev, "roam_prof", rp, sizeof(*rp),
		ioctl_buf, WLC_IOCTL_MEDLEN, NULL))) {
		WL_ERR(("Getting roam_profile failed with err=%d \n", err));
		goto exit;
	}
	memcpy_s(rp, sizeof(*rp), ioctl_buf, sizeof(*rp));
	/* roam_prof version get */
	if (rp->v1.ver > WL_ROAM_PROF_VER_2) {
		WL_ERR(("bad version (=%d) in return data\n", rp->v1.ver));
		err = BCME_VERSION;
		goto exit;
	}
	switch (rp->v1.ver) {
		case WL_ROAM_PROF_VER_0:
		{
			*roam_prof_size = sizeof(wl_roam_prof_v1_t);
			*roam_prof_ver = WL_ROAM_PROF_VER_0;
		}
		break;
		case WL_ROAM_PROF_VER_1:
		{
			*roam_prof_size = sizeof(wl_roam_prof_v2_t);
			*roam_prof_ver = WL_ROAM_PROF_VER_1;
		}
		break;
		case WL_ROAM_PROF_VER_2:
		{
			*roam_prof_size = sizeof(wl_roam_prof_v3_t);
			*roam_prof_ver = WL_ROAM_PROF_VER_2;
		}
		break;
		default:
			WL_ERR(("bad version = %d \n", rp->v1.ver));
			err = BCME_VERSION;
			goto exit;
	}
	WL_DBG(("roam prof ver %u size %u\n", *roam_prof_ver, *roam_prof_size));
	if ((rp->v1.len % *roam_prof_size) != 0) {
		WL_ERR(("bad length (=%d) in return data\n", rp->v1.len));
		err = BCME_BADLEN;
	}
exit:
	if (ioctl_buf) {
		MFREE(cfg->osh, ioctl_buf, WLC_IOCTL_MEDLEN);
	}
	return err;
}

s32
wl_cfg80211_wbtext_set_default(struct net_device *ndev)
{
	char *commandp = NULL;
	s32 ret = BCME_OK;
	char *data;
	u8 *ioctl_buf = NULL;
	wl_roamprof_band_t rp;
	uint8 bandidx = 0;
	int wnmmask = 0;
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);

	WL_DBG(("set wbtext to default\n"));

	commandp = (char *)MALLOCZ(cfg->osh, WLC_IOCTL_SMLEN);
	if (unlikely(!commandp)) {
		WL_ERR(("%s: failed to allocate memory\n", __func__));
		ret =  -ENOMEM;
		goto exit;
	}
	ioctl_buf = (char *)MALLOCZ(cfg->osh, WLC_IOCTL_SMLEN);
	if (unlikely(!ioctl_buf)) {
		WL_ERR(("%s: failed to allocate memory\n", __func__));
		ret =  -ENOMEM;
		goto exit;
	}

	rp.v1.band = WLC_BAND_2G;
	rp.v1.len = 0;
	/* Getting roam profile from fw */
	if ((ret = wldev_iovar_getbuf(ndev, "roam_prof", &rp, sizeof(rp),
		ioctl_buf, WLC_IOCTL_SMLEN, NULL))) {
		WL_ERR(("Getting roam_profile failed with err=%d \n", ret));
		goto exit;
	}
	memcpy_s(&rp, sizeof(rp), ioctl_buf, sizeof(rp));
	for (bandidx = 0; bandidx < MAXBANDS; bandidx++) {
		switch (rp.v1.ver) {
			case WL_ROAM_PROF_VER_1:
			{
				memset_s(commandp, WLC_IOCTL_SMLEN, 0, WLC_IOCTL_SMLEN);
				if (bandidx == BAND_5G_INDEX) {
					snprintf(commandp, WLC_IOCTL_SMLEN, "%s %s",
						CMD_WBTEXT_PROFILE_CONFIG,
						DEFAULT_WBTEXT_PROFILE_A_V2);
					data = (commandp + strlen(CMD_WBTEXT_PROFILE_CONFIG) + 1);
				} else {
					snprintf(commandp, WLC_IOCTL_SMLEN, "%s %s",
						CMD_WBTEXT_PROFILE_CONFIG,
						DEFAULT_WBTEXT_PROFILE_B_V2);
					data = (commandp + strlen(CMD_WBTEXT_PROFILE_CONFIG) + 1);
				}
			}
			break;
			case WL_ROAM_PROF_VER_2:
			{
				memset_s(commandp, WLC_IOCTL_SMLEN, 0, WLC_IOCTL_SMLEN);
				if (bandidx == BAND_5G_INDEX) {
					snprintf(commandp, WLC_IOCTL_SMLEN, "%s %s",
						CMD_WBTEXT_PROFILE_CONFIG,
						DEFAULT_WBTEXT_PROFILE_A_V3);
					data = (commandp + strlen(CMD_WBTEXT_PROFILE_CONFIG) + 1);
				} else {
					snprintf(commandp, WLC_IOCTL_SMLEN, "%s %s",
						CMD_WBTEXT_PROFILE_CONFIG,
						DEFAULT_WBTEXT_PROFILE_B_V3);
					data = (commandp + strlen(CMD_WBTEXT_PROFILE_CONFIG) + 1);
				}
			}
			break;
			default:
				WL_ERR(("No Support for roam prof ver = %d \n", rp.v1.ver));
				ret = -EINVAL;
				goto exit;
		}
		/* set roam profile */
		ret = wl_cfg80211_wbtext_config(ndev, data, commandp, WLC_IOCTL_SMLEN);
		if (ret != BCME_OK) {
			WL_ERR(("%s: Failed to set roam_prof %s error = %d\n",
				__FUNCTION__, data, ret));
			goto exit;
		}
	}

	/* set RSSI weight */
	memset_s(commandp, WLC_IOCTL_SMLEN, 0, WLC_IOCTL_SMLEN);
	snprintf(commandp, WLC_IOCTL_SMLEN, "%s %s",
		CMD_WBTEXT_WEIGHT_CONFIG, DEFAULT_WBTEXT_WEIGHT_RSSI_A);
	data = (commandp + strlen(CMD_WBTEXT_WEIGHT_CONFIG) + 1);
	ret = wl_cfg80211_wbtext_weight_config(ndev, data, commandp, WLC_IOCTL_SMLEN);
	if (ret != BCME_OK) {
		WL_ERR(("%s: Failed to set weight config %s error = %d\n",
			__FUNCTION__, data, ret));
		goto exit;
	}

	memset_s(commandp, WLC_IOCTL_SMLEN, 0, WLC_IOCTL_SMLEN);
	snprintf(commandp, WLC_IOCTL_SMLEN, "%s %s",
		CMD_WBTEXT_WEIGHT_CONFIG, DEFAULT_WBTEXT_WEIGHT_RSSI_B);
	data = (commandp + strlen(CMD_WBTEXT_WEIGHT_CONFIG) + 1);
	ret = wl_cfg80211_wbtext_weight_config(ndev, data, commandp, WLC_IOCTL_SMLEN);
	if (ret != BCME_OK) {
		WL_ERR(("%s: Failed to set weight config %s error = %d\n",
			__FUNCTION__, data, ret));
		goto exit;
	}

	/* set CU weight */
	memset_s(commandp, WLC_IOCTL_SMLEN, 0, WLC_IOCTL_SMLEN);
	snprintf(commandp, WLC_IOCTL_SMLEN, "%s %s",
		CMD_WBTEXT_WEIGHT_CONFIG, DEFAULT_WBTEXT_WEIGHT_CU_A);
	data = (commandp + strlen(CMD_WBTEXT_WEIGHT_CONFIG) + 1);
	ret = wl_cfg80211_wbtext_weight_config(ndev, data, commandp, WLC_IOCTL_SMLEN);
	if (ret != BCME_OK) {
		WL_ERR(("%s: Failed to set weight config %s error = %d\n",
			__FUNCTION__, data, ret));
		goto exit;
	}

	memset_s(commandp, WLC_IOCTL_SMLEN, 0, WLC_IOCTL_SMLEN);
	snprintf(commandp, WLC_IOCTL_SMLEN, "%s %s",
		CMD_WBTEXT_WEIGHT_CONFIG, DEFAULT_WBTEXT_WEIGHT_CU_B);
	data = (commandp + strlen(CMD_WBTEXT_WEIGHT_CONFIG) + 1);
	ret = wl_cfg80211_wbtext_weight_config(ndev, data, commandp, WLC_IOCTL_SMLEN);
	if (ret != BCME_OK) {
		WL_ERR(("%s: Failed to set weight config %s error = %d\n",
			__FUNCTION__, data, ret));
		goto exit;
	}

	ret = wldev_iovar_getint(ndev, "wnm", &wnmmask);
	if (ret != BCME_OK) {
		WL_ERR(("%s: Failed to get wnmmask error = %d\n", __func__, ret));
		goto exit;
	}
	/* set ESTM DL weight. */
	if (wnmmask & WL_WNM_ESTM) {
		WL_ERR(("Setting ESTM wt\n"));
		memset_s(commandp, WLC_IOCTL_SMLEN, 0, WLC_IOCTL_SMLEN);
		snprintf(commandp, WLC_IOCTL_SMLEN, "%s %s",
			CMD_WBTEXT_WEIGHT_CONFIG, DEFAULT_WBTEXT_WEIGHT_ESTM_DL_A);
		data = (commandp + strlen(CMD_WBTEXT_WEIGHT_CONFIG) + 1);
		ret = wl_cfg80211_wbtext_weight_config(ndev, data, commandp, WLC_IOCTL_SMLEN);
		if (ret != BCME_OK) {
			WL_ERR(("%s: Failed to set weight config %s error = %d\n",
				__FUNCTION__, data, ret));
			goto exit;
		}

		memset_s(commandp, WLC_IOCTL_SMLEN, 0, WLC_IOCTL_SMLEN);
		snprintf(commandp, WLC_IOCTL_SMLEN, "%s %s",
			CMD_WBTEXT_WEIGHT_CONFIG, DEFAULT_WBTEXT_WEIGHT_ESTM_DL_B);
		data = (commandp + strlen(CMD_WBTEXT_WEIGHT_CONFIG) + 1);
		ret = wl_cfg80211_wbtext_weight_config(ndev, data, commandp, WLC_IOCTL_SMLEN);
		if (ret != BCME_OK) {
			WL_ERR(("%s: Failed to set weight config %s error = %d\n",
				__FUNCTION__, data, ret));
			goto exit;
		}
	}

	/* set RSSI table */
	memset_s(commandp, WLC_IOCTL_SMLEN, 0, WLC_IOCTL_SMLEN);
	snprintf(commandp, WLC_IOCTL_SMLEN, "%s %s",
		CMD_WBTEXT_TABLE_CONFIG, DEFAULT_WBTEXT_TABLE_RSSI_A);
	data = (commandp + strlen(CMD_WBTEXT_TABLE_CONFIG) + 1);
	ret = wl_cfg80211_wbtext_table_config(ndev, data, commandp, WLC_IOCTL_SMLEN);
	if (ret != BCME_OK) {
		WL_ERR(("%s: Failed to set RSSI table %s error = %d\n",
			__FUNCTION__, data, ret));
		goto exit;
	}

	memset_s(commandp, WLC_IOCTL_SMLEN, 0, WLC_IOCTL_SMLEN);
	snprintf(commandp, WLC_IOCTL_SMLEN, "%s %s",
		CMD_WBTEXT_TABLE_CONFIG, DEFAULT_WBTEXT_TABLE_RSSI_B);
	data = (commandp + strlen(CMD_WBTEXT_TABLE_CONFIG) + 1);
	ret = wl_cfg80211_wbtext_table_config(ndev, data, commandp, WLC_IOCTL_SMLEN);
	if (ret != BCME_OK) {
		WL_ERR(("%s: Failed to set RSSI table %s error = %d\n",
			__FUNCTION__, data, ret));
		goto exit;
	}

	/* set CU table */
	memset_s(commandp, WLC_IOCTL_SMLEN, 0, WLC_IOCTL_SMLEN);
	snprintf(commandp, WLC_IOCTL_SMLEN, "%s %s",
		CMD_WBTEXT_TABLE_CONFIG, DEFAULT_WBTEXT_TABLE_CU_A);
	data = (commandp + strlen(CMD_WBTEXT_TABLE_CONFIG) + 1);
	ret = wl_cfg80211_wbtext_table_config(ndev, data, commandp, WLC_IOCTL_SMLEN);
	if (ret != BCME_OK) {
		WL_ERR(("%s: Failed to set CU table %s error = %d\n",
			__FUNCTION__, data, ret));
		goto exit;
	}

	memset_s(commandp, WLC_IOCTL_SMLEN, 0, WLC_IOCTL_SMLEN);
	snprintf(commandp, WLC_IOCTL_SMLEN, "%s %s",
		CMD_WBTEXT_TABLE_CONFIG, DEFAULT_WBTEXT_TABLE_CU_B);
	data = (commandp + strlen(CMD_WBTEXT_TABLE_CONFIG) + 1);
	ret = wl_cfg80211_wbtext_table_config(ndev, data, commandp, WLC_IOCTL_SMLEN);
	if (ret != BCME_OK) {
		WL_ERR(("%s: Failed to set CU table %s error = %d\n",
			__FUNCTION__, data, ret));
		goto exit;
	}

exit:
	if (commandp) {
		MFREE(cfg->osh, commandp, WLC_IOCTL_SMLEN);
	}
	if (ioctl_buf) {
		MFREE(cfg->osh, ioctl_buf, WLC_IOCTL_SMLEN);
	}
	return ret;
}

s32
wl_cfg80211_wbtext_config(struct net_device *ndev, char *data, char *command, int total_len)
{
	uint i = 0;
	long int rssi_lower, roam_trigger;
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	wl_roamprof_band_t *rp = NULL;
	int err = -EINVAL, bytes_written = 0;
	size_t len = strlen(data);
	int rp_len = 0;
	u8 *ioctl_buf = NULL;
	uint8 roam_prof_size = 0, roam_prof_ver = 0, fs_per = 0, prof_cnt = 0;

	data[len] = '\0';
	ioctl_buf = (u8 *)MALLOCZ(cfg->osh, WLC_IOCTL_MEDLEN);
	if (unlikely(!ioctl_buf)) {
		WL_ERR(("%s: failed to allocate memory\n", __func__));
		err =  -ENOMEM;
		goto exit;
	}
	rp = (wl_roamprof_band_t *)MALLOCZ(cfg->osh, sizeof(*rp));
	if (unlikely(!rp)) {
		WL_ERR(("%s: failed to allocate memory\n", __func__));
		err =  -ENOMEM;
		goto exit;
	}
	if (*data && (!strncmp(data, "b", 1))) {
		rp->v1.band = WLC_BAND_2G;
	} else if (*data && (!strncmp(data, "a", 1))) {
		rp->v1.band = WLC_BAND_5G;
	} else {
		err = snprintf(command, total_len, "Missing band\n");
		goto exit;
	}
	data++;
	rp->v1.len = 0;
	/* Getting roam profile from fw */
	if ((err = wldev_iovar_getbuf(ndev, "roam_prof", rp, sizeof(*rp),
		ioctl_buf, WLC_IOCTL_MEDLEN, NULL))) {
		WL_ERR(("Getting roam_profile failed with err=%d \n", err));
		goto exit;
	}
	memcpy_s(rp, sizeof(*rp), ioctl_buf, sizeof(*rp));
	/* roam_prof version get */
	if (rp->v1.ver > WL_ROAM_PROF_VER_2) {
		WL_ERR(("bad version (=%d) in return data\n", rp->v1.ver));
		err = -EINVAL;
		goto exit;
	}
	switch (rp->v1.ver) {
		case WL_ROAM_PROF_VER_0:
		{
			roam_prof_size = sizeof(wl_roam_prof_v1_t);
			roam_prof_ver = WL_ROAM_PROF_VER_0;
		}
		break;
		case WL_ROAM_PROF_VER_1:
		{
			roam_prof_size = sizeof(wl_roam_prof_v2_t);
			roam_prof_ver = WL_ROAM_PROF_VER_1;
		}
		break;
		case WL_ROAM_PROF_VER_2:
		{
			roam_prof_size = sizeof(wl_roam_prof_v3_t);
			roam_prof_ver = WL_ROAM_PROF_VER_2;
		}
		break;
		default:
			WL_ERR(("bad version = %d \n", rp->v1.ver));
			goto exit;
	}
	WL_DBG(("roam prof ver %u size %u\n", roam_prof_ver, roam_prof_size));
	if ((rp->v1.len % roam_prof_size) != 0) {
		WL_ERR(("bad length (=%d) in return data\n", rp->v1.len));
		err = -EINVAL;
		goto exit;
	}
	for (i = 0; i < WL_MAX_ROAM_PROF_BRACKETS; i++) {
		/* printing contents of roam profile data from fw and exits
		 * if code hits any of one of the below condtion. If remaining
		 * length of buffer is less than roam profile size or
		 * if there is no valid entry.
		 */
		if (((i * roam_prof_size) > rp->v1.len)) {
			break;
		}
		if (roam_prof_ver == WL_ROAM_PROF_VER_0) {
			fs_per = rp->v1.roam_prof[i].fullscan_period;
		} else if (roam_prof_ver == WL_ROAM_PROF_VER_1) {
			fs_per = rp->v2.roam_prof[i].fullscan_period;
		} else if (roam_prof_ver == WL_ROAM_PROF_VER_2) {
			fs_per = rp->v3.roam_prof[i].fullscan_period;
		}
		if (fs_per == 0) {
			break;
		}
		prof_cnt++;
	}

	if (!*data) {
		for (i = 0; (i < prof_cnt) && (i < WL_MAX_ROAM_PROF_BRACKETS); i++) {
			if (roam_prof_ver == WL_ROAM_PROF_VER_1) {
				bytes_written += snprintf(command+bytes_written,
					total_len - bytes_written,
					"RSSI[%d,%d] CU(trigger:%d%%: duration:%ds)\n",
					rp->v2.roam_prof[i].roam_trigger,
					rp->v2.roam_prof[i].rssi_lower,
					rp->v2.roam_prof[i].channel_usage,
					rp->v2.roam_prof[i].cu_avg_calc_dur);
			} else if (roam_prof_ver == WL_ROAM_PROF_VER_2) {
				bytes_written += snprintf(command+bytes_written,
					total_len - bytes_written,
					"RSSI[%d,%d] CU(trigger:%d%%: duration:%ds)\n",
					rp->v3.roam_prof[i].roam_trigger,
					rp->v3.roam_prof[i].rssi_lower,
					rp->v3.roam_prof[i].channel_usage,
					rp->v3.roam_prof[i].cu_avg_calc_dur);
			}
		}
		err = bytes_written;
		goto exit;
	} else {
		/* Do not set roam_prof from upper layer if fw doesn't have 2 rows */
		if (prof_cnt != 2) {
			WL_ERR(("FW must have 2 rows to fill roam_prof\n"));
			err = -EINVAL;
			goto exit;
		}
		/* setting roam profile to fw */
		data++;
		for (i = 0; i < WL_MAX_ROAM_PROF_BRACKETS; i++) {
			roam_trigger = simple_strtol(data, &data, 10);
			if (roam_trigger >= 0) {
				WL_ERR(("roam trigger[%d] value must be negative\n", i));
				err = -EINVAL;
				goto exit;
			}
			data++;
			rssi_lower = simple_strtol(data, &data, 10);
			if (rssi_lower >= 0) {
				WL_ERR(("rssi lower[%d] value must be negative\n", i));
				err = -EINVAL;
				goto exit;
			}
			if (roam_prof_ver == WL_ROAM_PROF_VER_1) {
				rp->v2.roam_prof[i].roam_trigger = roam_trigger;
				rp->v2.roam_prof[i].rssi_lower = rssi_lower;
				data++;
				rp->v2.roam_prof[i].channel_usage = simple_strtol(data, &data, 10);
				data++;
				rp->v2.roam_prof[i].cu_avg_calc_dur =
					simple_strtol(data, &data, 10);
			}
			if (roam_prof_ver == WL_ROAM_PROF_VER_2) {
				rp->v3.roam_prof[i].roam_trigger = roam_trigger;
				rp->v3.roam_prof[i].rssi_lower = rssi_lower;
				data++;
				rp->v3.roam_prof[i].channel_usage = simple_strtol(data, &data, 10);
				data++;
				rp->v3.roam_prof[i].cu_avg_calc_dur =
					simple_strtol(data, &data, 10);
			}

			rp_len += roam_prof_size;

			if (*data == '\0') {
				break;
			}
			data++;
		}
		if (i != 1) {
			WL_ERR(("Only two roam_prof rows supported.\n"));
			err = -EINVAL;
			goto exit;
		}
		rp->v1.len = rp_len;
		if ((err = wldev_iovar_setbuf(ndev, "roam_prof", rp,
				sizeof(*rp), cfg->ioctl_buf, WLC_IOCTL_MEDLEN,
				&cfg->ioctl_buf_sync)) < 0) {
			WL_ERR(("seting roam_profile failed with err %d\n", err));
		}
	}
exit:
	if (rp) {
		MFREE(cfg->osh, rp, sizeof(*rp));
	}
	if (ioctl_buf) {
		MFREE(cfg->osh, ioctl_buf, WLC_IOCTL_MEDLEN);
	}
	return err;
}

int wl_cfg80211_wbtext_weight_config(struct net_device *ndev, char *data,
		char *command, int total_len)
{
	int bytes_written = 0, err = -EINVAL, argc = 0;
	char rssi[BUFSZN], band[BUFSZN], weight[BUFSZN];
	char *endptr = NULL;
	wnm_bss_select_weight_cfg_t *bwcfg;
	u8 ioctl_buf[WLC_IOCTL_SMLEN];
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);

	bwcfg = (wnm_bss_select_weight_cfg_t *)MALLOCZ(cfg->osh, sizeof(*bwcfg));
	if (unlikely(!bwcfg)) {
		WL_ERR(("%s: failed to allocate memory\n", __func__));
		err = -ENOMEM;
		goto exit;
	}
	bwcfg->version =  WNM_BSSLOAD_MONITOR_VERSION;
	bwcfg->type = 0;
	bwcfg->weight = 0;

	argc = sscanf(data, "%"S(BUFSZ)"s %"S(BUFSZ)"s %"S(BUFSZ)"s", rssi, band, weight);

	if (!strcasecmp(rssi, "rssi"))
		bwcfg->type = WNM_BSS_SELECT_TYPE_RSSI;
	else if (!strcasecmp(rssi, "cu"))
		bwcfg->type = WNM_BSS_SELECT_TYPE_CU;
	else if (!strcasecmp(rssi, "estm_dl"))
		bwcfg->type = WNM_BSS_SELECT_TYPE_ESTM_DL;
	else {
		/* Usage DRIVER WBTEXT_WEIGHT_CONFIG <rssi/cu/estm_dl> <band> <weight> */
		WL_ERR(("%s: Command usage error\n", __func__));
		goto exit;
	}

	if (!strcasecmp(band, "a"))
		bwcfg->band = WLC_BAND_5G;
	else if (!strcasecmp(band, "b"))
		bwcfg->band = WLC_BAND_2G;
	else if (!strcasecmp(band, "all"))
		bwcfg->band = WLC_BAND_ALL;
	else {
		WL_ERR(("%s: Command usage error\n", __func__));
		goto exit;
	}

	if (argc == 2) {
		/* If there is no data after band, getting wnm_bss_select_weight from fw */
		if (bwcfg->band == WLC_BAND_ALL) {
			WL_ERR(("band option \"all\" is for set only, not get\n"));
			goto exit;
		}
		if ((err = wldev_iovar_getbuf(ndev, "wnm_bss_select_weight", bwcfg,
				sizeof(*bwcfg),
				ioctl_buf, sizeof(ioctl_buf), NULL))) {
			WL_ERR(("Getting wnm_bss_select_weight failed with err=%d \n", err));
			goto exit;
		}
		memcpy(bwcfg, ioctl_buf, sizeof(*bwcfg));
		bytes_written = snprintf(command, total_len, "%s %s weight = %d\n",
			(bwcfg->type == WNM_BSS_SELECT_TYPE_RSSI) ? "RSSI" :
			(bwcfg->type == WNM_BSS_SELECT_TYPE_CU) ? "CU": "ESTM_DL",
			(bwcfg->band == WLC_BAND_2G) ? "2G" : "5G", bwcfg->weight);
		err = bytes_written;
		goto exit;
	} else {
		/* if weight is non integer returns command usage error */
		bwcfg->weight = simple_strtol(weight, &endptr, 0);
		if (*endptr != '\0') {
			WL_ERR(("%s: Command usage error", __func__));
			goto exit;
		}
		/* setting weight for iovar wnm_bss_select_weight to fw */
		if ((err = wldev_iovar_setbuf(ndev, "wnm_bss_select_weight", bwcfg,
				sizeof(*bwcfg),
				ioctl_buf, sizeof(ioctl_buf), NULL))) {
			WL_ERR(("setting wnm_bss_select_weight failed with err=%d\n", err));
		}
	}
exit:
	if (bwcfg) {
		MFREE(cfg->osh, bwcfg, sizeof(*bwcfg));
	}
	return err;
}

/* WBTEXT_TUPLE_MIN_LEN_CHECK :strlen(low)+" "+strlen(high)+" "+strlen(factor) */
#define WBTEXT_TUPLE_MIN_LEN_CHECK 5

int wl_cfg80211_wbtext_table_config(struct net_device *ndev, char *data,
	char *command, int total_len)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	int bytes_written = 0, err = -EINVAL;
	char rssi[BUFSZN], band[BUFSZN];
	int btcfg_len = 0, i = 0, parsed_len = 0;
	wnm_bss_select_factor_cfg_t *btcfg;
	size_t slen = strlen(data);
	char *start_addr = NULL;
	u8 ioctl_buf[WLC_IOCTL_SMLEN];

	data[slen] = '\0';
	btcfg = (wnm_bss_select_factor_cfg_t *)MALLOCZ(cfg->osh,
		(sizeof(*btcfg) + sizeof(*btcfg) * WL_FACTOR_TABLE_MAX_LIMIT));
	if (unlikely(!btcfg)) {
		WL_ERR(("%s: failed to allocate memory\n", __func__));
		err = -ENOMEM;
		goto exit;
	}

	btcfg->version = WNM_BSS_SELECT_FACTOR_VERSION;
	btcfg->band = WLC_BAND_AUTO;
	btcfg->type = 0;
	btcfg->count = 0;

	sscanf(data, "%"S(BUFSZ)"s %"S(BUFSZ)"s", rssi, band);

	if (!strcasecmp(rssi, "rssi")) {
		btcfg->type = WNM_BSS_SELECT_TYPE_RSSI;
	}
	else if (!strcasecmp(rssi, "cu")) {
		btcfg->type = WNM_BSS_SELECT_TYPE_CU;
	}
	else {
		WL_ERR(("%s: Command usage error\n", __func__));
		goto exit;
	}

	if (!strcasecmp(band, "a")) {
		btcfg->band = WLC_BAND_5G;
	}
	else if (!strcasecmp(band, "b")) {
		btcfg->band = WLC_BAND_2G;
	}
	else if (!strcasecmp(band, "all")) {
		btcfg->band = WLC_BAND_ALL;
	}
	else {
		WL_ERR(("%s: Command usage, Wrong band\n", __func__));
		goto exit;
	}

	if ((slen - 1) == (strlen(rssi) + strlen(band))) {
		/* Getting factor table using iovar 'wnm_bss_select_table' from fw */
		if ((err = wldev_iovar_getbuf(ndev, "wnm_bss_select_table", btcfg,
				sizeof(*btcfg),
				ioctl_buf, sizeof(ioctl_buf), NULL))) {
			WL_ERR(("Getting wnm_bss_select_table failed with err=%d \n", err));
			goto exit;
		}
		memcpy(btcfg, ioctl_buf, sizeof(*btcfg));
		memcpy(btcfg, ioctl_buf, (btcfg->count+1) * sizeof(*btcfg));

		bytes_written += snprintf(command + bytes_written, total_len - bytes_written,
					"No of entries in table: %d\n", btcfg->count);
		bytes_written += snprintf(command + bytes_written, total_len - bytes_written,
				"%s factor table\n",
				(btcfg->type == WNM_BSS_SELECT_TYPE_RSSI) ? "RSSI" : "CU");
		bytes_written += snprintf(command + bytes_written, total_len - bytes_written,
					"low\thigh\tfactor\n");
		for (i = 0; i <= btcfg->count-1; i++) {
			bytes_written += snprintf(command + bytes_written,
				total_len - bytes_written, "%d\t%d\t%d\n", btcfg->params[i].low,
				btcfg->params[i].high, btcfg->params[i].factor);
		}
		err = bytes_written;
		goto exit;
	} else {
		uint16 len = (sizeof(wnm_bss_select_factor_params_t) * WL_FACTOR_TABLE_MAX_LIMIT);
		memset_s(btcfg->params, len, 0, len);
		data += (strlen(rssi) + strlen(band) + 2);
		start_addr = data;
		slen = slen - (strlen(rssi) + strlen(band) + 2);
		for (i = 0; i < WL_FACTOR_TABLE_MAX_LIMIT; i++) {
			if (parsed_len + WBTEXT_TUPLE_MIN_LEN_CHECK <= slen) {
				btcfg->params[i].low = simple_strtol(data, &data, 10);
				data++;
				btcfg->params[i].high = simple_strtol(data, &data, 10);
				data++;
				btcfg->params[i].factor = simple_strtol(data, &data, 10);
				btcfg->count++;
				if (*data == '\0') {
					break;
				}
				data++;
				parsed_len = data - start_addr;
			} else {
				WL_ERR(("%s:Command usage:less no of args\n", __func__));
				goto exit;
			}
		}
		btcfg_len = sizeof(*btcfg) + ((btcfg->count) * sizeof(*btcfg));
		if ((err = wldev_iovar_setbuf(ndev, "wnm_bss_select_table", btcfg, btcfg_len,
				cfg->ioctl_buf, WLC_IOCTL_MEDLEN, &cfg->ioctl_buf_sync)) < 0) {
			WL_ERR(("seting wnm_bss_select_table failed with err %d\n", err));
			goto exit;
		}
	}
exit:
	if (btcfg) {
		MFREE(cfg->osh, btcfg,
			(sizeof(*btcfg) + sizeof(*btcfg) * WL_FACTOR_TABLE_MAX_LIMIT));
	}
	return err;
}

s32
wl_cfg80211_wbtext_delta_config(struct net_device *ndev, char *data, char *command, int total_len)
{
	uint i = 0;
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	int err = -EINVAL, bytes_written = 0, argc = 0, val, len = 0;
	char delta[BUFSZN], band[BUFSZN], *endptr = NULL;
	wl_roamprof_band_t *rp = NULL;
	uint8 band_val = 0, roam_prof_size = 0, roam_prof_ver = 0;

	rp = (wl_roamprof_band_t *)MALLOCZ(cfg->osh, sizeof(*rp));
	if (unlikely(!rp)) {
		WL_ERR(("%s: failed to allocate memory\n", __func__));
		err = -ENOMEM;
		goto exit;
	}

	argc = sscanf(data, "%"S(BUFSZ)"s %"S(BUFSZ)"s", band, delta);
	if (!strcasecmp(band, "a"))
		band_val = WLC_BAND_5G;
	else if (!strcasecmp(band, "b"))
		band_val = WLC_BAND_2G;
	else {
		WL_ERR(("%s: Missing band\n", __func__));
		goto exit;
	}
	if ((err = wlc_wbtext_get_roam_prof(ndev, rp, band_val, &roam_prof_ver,
		&roam_prof_size))) {
		WL_ERR(("Getting roam_profile failed with err=%d \n", err));
		err = -EINVAL;
		goto exit;
	}
	if (argc == 2) {
		/* if delta is non integer returns command usage error */
		val = simple_strtol(delta, &endptr, 0);
		if (*endptr != '\0') {
			WL_ERR(("%s: Command usage error", __func__));
			goto exit;
		}
		for (i = 0; i < WL_MAX_ROAM_PROF_BRACKETS; i++) {
		/*
		 * Checking contents of roam profile data from fw and exits
		 * if code hits below condtion. If remaining length of buffer is
		 * less than roam profile size or if there is no valid entry.
		 */
			if (((i * roam_prof_size) > rp->v1.len) ||
				(rp->v1.roam_prof[i].fullscan_period == 0)) {
				break;
			}
			if (roam_prof_ver == WL_ROAM_PROF_VER_1) {
				if (rp->v2.roam_prof[i].channel_usage != 0) {
					rp->v2.roam_prof[i].roam_delta = val;
				}
			} else if (roam_prof_ver == WL_ROAM_PROF_VER_2) {
				if (rp->v3.roam_prof[i].channel_usage != 0) {
					rp->v3.roam_prof[i].roam_delta = val;
				}
			}
			len += roam_prof_size;
		}
	}
	else {
		if (rp->v2.roam_prof[0].channel_usage != 0) {
			bytes_written = snprintf(command, total_len,
				"%s Delta %d\n", (rp->v1.band == WLC_BAND_2G) ? "2G" : "5G",
				rp->v2.roam_prof[0].roam_delta);
		}
		err = bytes_written;
		goto exit;
	}
	rp->v1.len = len;
	if ((err = wldev_iovar_setbuf(ndev, "roam_prof", rp,
			sizeof(*rp), cfg->ioctl_buf, WLC_IOCTL_MEDLEN,
			&cfg->ioctl_buf_sync)) < 0) {
		WL_ERR(("seting roam_profile failed with err %d\n", err));
	}
exit :
	if (rp) {
		MFREE(cfg->osh, rp, sizeof(*rp));
	}
	return err;
}
#endif /* WBTEXT */
