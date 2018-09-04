/*
 * Linux cfg80211 driver - Android related functions
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
 * $Id: wl_android.c 710862 2017-07-14 07:43:59Z $
 */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <net/netlink.h>
#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif

#include <wl_android.h>
#include <wldev_common.h>
#include <wlc_types.h>
#include <wlioctl.h>
#include <wlioctl_utils.h>
#include <bcmutils.h>
#include <linux_osl.h>
#include <dhd_dbg.h>
#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_config.h>
#include <bcmip.h>
#ifdef PNO_SUPPORT
#include <dhd_pno.h>
#endif
#ifdef BCMSDIO
#include <bcmsdbus.h>
#endif
#ifdef WL_CFG80211
#include <wl_cfg80211.h>
#endif
#ifdef DHDTCPACK_SUPPRESS
#include <dhd_ip.h>
#endif /* DHDTCPACK_SUPPRESS */
#include <dhd_linux.h>
#ifdef DHD_PKT_LOGGING
#include <dhd_pktlog.h>
#endif /* DHD_PKT_LOGGING */

#if defined(STAT_REPORT)
#include <wl_statreport.h>
#endif /* STAT_REPORT */

#ifndef WL_CFG80211
#define htod32(i) i
#define htod16(i) i
#define dtoh32(i) i
#define dtoh16(i) i
#define htodchanspec(i) i
#define dtohchanspec(i) i
#endif

uint android_msg_level = ANDROID_ERROR_LEVEL;

/*
 * Android private command strings, PLEASE define new private commands here
 * so they can be updated easily in the future (if needed)
 */

#define CMD_START		"START"
#define CMD_STOP		"STOP"
#define	CMD_SCAN_ACTIVE		"SCAN-ACTIVE"
#define	CMD_SCAN_PASSIVE	"SCAN-PASSIVE"
#define CMD_RSSI		"RSSI"
#define CMD_LINKSPEED		"LINKSPEED"
#define CMD_RXFILTER_START	"RXFILTER-START"
#define CMD_RXFILTER_STOP	"RXFILTER-STOP"
#define CMD_RXFILTER_ADD	"RXFILTER-ADD"
#define CMD_RXFILTER_REMOVE	"RXFILTER-REMOVE"
#define CMD_BTCOEXSCAN_START	"BTCOEXSCAN-START"
#define CMD_BTCOEXSCAN_STOP	"BTCOEXSCAN-STOP"
#define CMD_BTCOEXMODE		"BTCOEXMODE"
#define CMD_SETSUSPENDOPT	"SETSUSPENDOPT"
#define CMD_SETSUSPENDMODE      "SETSUSPENDMODE"
#define CMD_MAXDTIM_IN_SUSPEND  "MAX_DTIM_IN_SUSPEND"
#define CMD_P2P_DEV_ADDR	"P2P_DEV_ADDR"
#define CMD_SETFWPATH		"SETFWPATH"
#define CMD_SETBAND		"SETBAND"
#define CMD_GETBAND		"GETBAND"
#define CMD_COUNTRY		"COUNTRY"
#ifdef WLMESH
#define CMD_SAE_SET_PASSWORD "SAE_SET_PASSWORD"
#define CMD_SET_RSDB_MODE "RSDB_MODE"
#endif
#define CMD_P2P_SET_NOA		"P2P_SET_NOA"
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
#define CMD_COUNTRY_DELIMITER "/"
#ifdef WL11ULB
#define CMD_ULB_MODE "ULB_MODE"
#define CMD_ULB_BW "ULB_BW"
#endif /* WL11ULB */

#if defined(WL_SUPPORT_AUTO_CHANNEL)
#define CMD_GET_BEST_CHANNELS	"GET_BEST_CHANNELS"
#endif /* WL_SUPPORT_AUTO_CHANNEL */

#define CMD_80211_MODE    "MODE"  /* 802.11 mode a/b/g/n/ac */
#define CMD_CHANSPEC      "CHANSPEC"
#define CMD_DATARATE      "DATARATE"
#define CMD_ASSOC_CLIENTS "ASSOCLIST"
#define CMD_SET_CSA       "SETCSA"
#ifdef WL_SUPPORT_AUTO_CHANNEL
#define CMD_SET_HAPD_AUTO_CHANNEL	"HAPD_AUTO_CHANNEL"
#endif /* WL_SUPPORT_AUTO_CHANNEL */
#ifdef CUSTOMER_HW4_PRIVATE_CMD
#ifdef SUPPORT_SET_LPC
#define CMD_HAPD_LPC_ENABLED		"HAPD_LPC_ENABLED"
#endif /* SUPPORT_SET_LPC */
#ifdef SUPPORT_TRIGGER_HANG_EVENT
#define CMD_TEST_FORCE_HANG		"TEST_FORCE_HANG"
#endif /* SUPPORT_TRIGGER_HANG_EVENT */
#ifdef TEST_TX_POWER_CONTROL
#define CMD_TEST_SET_TX_POWER		"TEST_SET_TX_POWER"
#define CMD_TEST_GET_TX_POWER		"TEST_GET_TX_POWER"
#endif /* TEST_TX_POWER_CONTROL */
#define CMD_SARLIMIT_TX_CONTROL		"SET_TX_POWER_CALLING"
#endif /* CUSTOMER_HW4_PRIVATE_CMD */
#define CMD_KEEP_ALIVE		"KEEPALIVE"


#ifdef PNO_SUPPORT
#define CMD_PNOSSIDCLR_SET	"PNOSSIDCLR"
#define CMD_PNOSETUP_SET	"PNOSETUP "
#define CMD_PNOENABLE_SET	"PNOFORCE"
#define CMD_PNODEBUG_SET	"PNODEBUG"
#define CMD_WLS_BATCHING	"WLS_BATCHING"
#endif /* PNO_SUPPORT */

#define	CMD_HAPD_MAC_FILTER	"HAPD_MAC_FILTER"

#ifdef CUSTOMER_HW4_PRIVATE_CMD


#if defined(SUPPORT_RANDOM_MAC_SCAN)
#define ENABLE_RANDOM_MAC "ENABLE_RANDOM_MAC"
#define DISABLE_RANDOM_MAC "DISABLE_RANDOM_MAC"
#endif /* SUPPORT_RANDOM_MAC_SCAN */


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

#ifdef FCC_PWR_LIMIT_2G
#define CMD_GET_FCC_PWR_LIMIT_2G "GET_FCC_CHANNEL"
#define CMD_SET_FCC_PWR_LIMIT_2G "SET_FCC_CHANNEL"
/* CUSTOMER_HW4's value differs from BRCM FW value for enable/disable */
#define CUSTOMER_HW4_ENABLE		0
#define CUSTOMER_HW4_DISABLE	-1
#define CUSTOMER_HW4_EN_CONVERT(i)	(i += 1)
#endif /* FCC_PWR_LIMIT_2G */

#endif /* CUSTOMER_HW4_PRIVATE_CMD */



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

#ifdef SUPPORT_AP_HIGHER_BEACONRATE
#define CMD_SET_AP_BEACONRATE				"SET_AP_BEACONRATE"
#define CMD_GET_AP_BASICRATE				"GET_AP_BASICRATE"
#endif /* SUPPORT_AP_HIGHER_BEACONRATE */

#ifdef SUPPORT_AP_RADIO_PWRSAVE
#define CMD_SET_AP_RPS						"SET_AP_RPS"
#define CMD_GET_AP_RPS						"GET_AP_RPS"
#define CMD_SET_AP_RPS_PARAMS				"SET_AP_RPS_PARAMS"
#endif /* SUPPORT_AP_RADIO_PWRSAVE */

#ifdef SUPPORT_RSSI_LOGGING
#define CMD_SET_RSSI_LOGGING				"SET_RSSI_LOGGING"
#define CMD_GET_RSSI_LOGGING				"GET_RSSI_LOGGING"
#define CMD_GET_RSSI_PER_ANT				"GET_RSSI_PER_ANT"
#endif /* SUPPORT_RSSI_LOGGING */

#define CMD_GET_SNR							"GET_SNR"

/* miracast related definition */
#define MIRACAST_MODE_OFF	0
#define MIRACAST_MODE_SOURCE	1
#define MIRACAST_MODE_SINK	2

#ifndef MIRACAST_AMPDU_SIZE
#define MIRACAST_AMPDU_SIZE	8
#endif

#ifndef MIRACAST_MCHAN_ALGO
#define MIRACAST_MCHAN_ALGO     1
#endif

#ifndef MIRACAST_MCHAN_BW
#define MIRACAST_MCHAN_BW       25
#endif

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

#ifdef SUPPORT_LQCM
#define CMD_SET_LQCM_ENABLE			"SET_LQCM_ENABLE"
#define CMD_GET_LQCM_REPORT			"GET_LQCM_REPORT"
#endif

static LIST_HEAD(miracast_resume_list);
#ifdef WL_CFG80211
static u8 miracast_cur_mode;
#endif

#ifdef DHD_LOG_DUMP
#define CMD_NEW_DEBUG_PRINT_DUMP			"DEBUG_DUMP"
extern void dhd_schedule_log_dump(dhd_pub_t *dhdp);
extern int dhd_bus_mem_dump(dhd_pub_t *dhd);
#endif /* DHD_LOG_DUMP */

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
#define CMD_PKTLOG_FILTER_INFO	"PKTLOG_FILTER_INFO"
#define CMD_PKTLOG_START	"PKTLOG_START"
#define CMD_PKTLOG_STOP		"PKTLOG_STOP"
#define CMD_PKTLOG_FILTER_EXIST "PKTLOG_FILTER_EXIST"
#endif /* DHD_PKT_LOGGING */

#if defined(STAT_REPORT)
#define CMD_STAT_REPORT_GET_START	"STAT_REPORT_GET_START"
#define CMD_STAT_REPORT_GET_NEXT	"STAT_REPORT_GET_NEXT"
#endif /* STAT_REPORT */


#ifdef SUPPORT_LQCM
#define LQCM_ENAB_MASK			0x000000FF	/* LQCM enable flag mask */
#define LQCM_TX_INDEX_MASK		0x0000FF00	/* LQCM tx index mask */
#define LQCM_RX_INDEX_MASK		0x00FF0000	/* LQCM rx index mask */

#define LQCM_TX_INDEX_SHIFT		8	/* LQCM tx index shift */
#define LQCM_RX_INDEX_SHIFT		16	/* LQCM rx index shift */
#endif /* SUPPORT_LQCM */

/**
 * Extern function declarations (TODO: move them to dhd_linux.h)
 */
int dhd_net_bus_devreset(struct net_device *dev, uint8 flag);
int dhd_dev_init_ioctl(struct net_device *dev);
#ifdef WL_CFG80211
int wl_cfg80211_get_p2p_dev_addr(struct net_device *net, struct ether_addr *p2pdev_addr);
int wl_cfg80211_set_btcoex_dhcp(struct net_device *dev, dhd_pub_t *dhd, char *command);
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
#endif /* WL_CFG80211 */
#ifdef WBTEXT
static int wl_android_wbtext(struct net_device *dev, char *command, int total_len);
static int wl_cfg80211_wbtext_btm_timer_threshold(struct net_device *dev,
	char *command, int total_len);
static int wl_cfg80211_wbtext_btm_delta(struct net_device *dev,
	char *command, int total_len);
#endif /* WBTEXT */

#ifdef ENABLE_4335BT_WAR
extern int bcm_bt_lock(int cookie);
extern void bcm_bt_unlock(int cookie);
static int lock_cookie_wifi = 'W' | 'i'<<8 | 'F'<<16 | 'i'<<24;	/* cookie is "WiFi" */
#endif /* ENABLE_4335BT_WAR */

extern bool ap_fw_loaded;
extern char iface_name[IFNAMSIZ];

/**
 * Local (static) functions and variables
 */

/* Initialize g_wifi_on to 1 so dhd_bus_start will be called for the first
 * time (only) in dhd_open, subsequential wifi on will be handled by
 * wl_android_wifi_on
 */
int g_wifi_on = TRUE;

/**
 * Local (static) function definitions
 */

#ifdef WLWFDS
static int wl_android_set_wfds_hash(
	struct net_device *dev, char *command, int total_len, bool enable)
{
	int error = 0;
	wl_p2p_wfds_hash_t *wfds_hash = NULL;
	char *smbuf = NULL;
	smbuf = kmalloc(WLC_IOCTL_MAXLEN, GFP_KERNEL);

	if (smbuf == NULL) {
		ANDROID_ERROR(("%s: failed to allocated memory %d bytes\n",
			__FUNCTION__, WLC_IOCTL_MAXLEN));
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
		ANDROID_ERROR(("%s: failed to %s, error=%d\n", __FUNCTION__, command, error));
	}

	if (smbuf)
		kfree(smbuf);
	return error;
}
#endif /* WLWFDS */

static int wl_android_get_link_speed(struct net_device *net, char *command, int total_len)
{
	int link_speed;
	int bytes_written;
	int error;

	error = wldev_get_link_speed(net, &link_speed);
	if (error) {
		ANDROID_ERROR(("Get linkspeed failed \n"));
		return -1;
	}

	/* Convert Kbps to Android Mbps */
	link_speed = link_speed / 1000;
	bytes_written = snprintf(command, total_len, "LinkSpeed %d", link_speed);
	ANDROID_INFO(("%s: command result is %s\n", __FUNCTION__, command));
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
		ANDROID_TRACE(("%s: cmd:%s\n", __FUNCTION__, delim));
		/* skip space from delim after finding char */
		delim++;
		if (!(bcm_ether_atoe((delim), &scbval.ea)))
		{
			ANDROID_ERROR(("%s:address err\n", __FUNCTION__));
			return -1;
		}
	        scbval.val = htod32(0);
		ANDROID_TRACE(("%s: address:"MACDBG, __FUNCTION__, MAC2STRDBG(scbval.ea.octet)));
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
		memset(&scbval, 0, sizeof(scb_val_t));
	}

	error = wldev_get_rssi(target_ndev, &scbval);
	if (error)
		return -1;
#if defined(RSSIOFFSET)
	scbval.val = wl_update_rssi_offset(net, scbval.val);
#endif

	error = wldev_get_ssid(target_ndev, &ssid);
	if (error)
		return -1;
	if ((ssid.SSID_len == 0) || (ssid.SSID_len > DOT11_MAX_SSID_LEN)) {
		ANDROID_ERROR(("%s: wldev_get_ssid failed\n", __FUNCTION__));
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

	ANDROID_TRACE(("%s: command result is %s (%d)\n", __FUNCTION__, command, bytes_written));
	return bytes_written;
}

static int wl_android_set_suspendopt(struct net_device *dev, char *command, int total_len)
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
			ANDROID_INFO(("%s: Suspend Flag %d -> %d\n",
				__FUNCTION__, ret_now, suspend_flag));
		} else {
			ANDROID_ERROR(("%s: failed %d\n", __FUNCTION__, ret));
		}
	}

	return ret;
}

static int wl_android_set_suspendmode(struct net_device *dev, char *command, int total_len)
{
	int ret = 0;

#if !defined(CONFIG_HAS_EARLYSUSPEND) || !defined(DHD_USE_EARLYSUSPEND)
	int suspend_flag;

	suspend_flag = *(command + strlen(CMD_SETSUSPENDMODE) + 1) - '0';
	if (suspend_flag != 0)
		suspend_flag = 1;

	if (!(ret = net_os_set_suspend(dev, suspend_flag, 0)))
		ANDROID_INFO(("%s: Suspend Mode %d\n", __FUNCTION__, suspend_flag));
	else
		ANDROID_ERROR(("%s: failed %d\n", __FUNCTION__, ret));
#endif

	return ret;
}

#ifdef WL_CFG80211
int wl_android_get_80211_mode(struct net_device *dev, char *command, int total_len)
{
	uint8 mode[5];
	int  error = 0;
	int bytes_written = 0;

	error = wldev_get_mode(dev, mode, sizeof(mode));
	if (error)
		return -1;

	ANDROID_INFO(("%s: mode:%s\n", __FUNCTION__, mode));
	bytes_written = snprintf(command, total_len, "%s %s", CMD_80211_MODE, mode);
	ANDROID_INFO(("%s: command:%s EXIT\n", __FUNCTION__, command));
	return bytes_written;

}

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
	ANDROID_INFO(("%s:return value of chanspec:%x\n", __FUNCTION__, chanspec));

	channel = chanspec & WL_CHANSPEC_CHAN_MASK;
	band = chanspec & WL_CHANSPEC_BAND_MASK;
	bw = chanspec & WL_CHANSPEC_BW_MASK;

	ANDROID_INFO(("%s:channel:%d band:%d bandwidth:%d\n", __FUNCTION__, channel, band, bw));

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

	ANDROID_INFO(("%s: command:%s EXIT\n", __FUNCTION__, command));
	return bytes_written;

}
#endif

/* returns current datarate datarate returned from firmware are in 500kbps */
int wl_android_get_datarate(struct net_device *dev, char *command, int total_len)
{
	int  error = 0;
	int datarate = 0;
	int bytes_written = 0;

	error = wldev_get_datarate(dev, &datarate);
	if (error)
		return -1;

	ANDROID_INFO(("%s:datarate:%d\n", __FUNCTION__, datarate));

	bytes_written = snprintf(command, total_len, "%s %d", CMD_DATARATE, (datarate/2));
	return bytes_written;
}
int wl_android_get_assoclist(struct net_device *dev, char *command, int total_len)
{
	int  error = 0;
	int bytes_written = 0;
	uint i;
	char mac_buf[MAX_NUM_OF_ASSOCLIST *
		sizeof(struct ether_addr) + sizeof(uint)] = {0};
	struct maclist *assoc_maclist = (struct maclist *)mac_buf;

	ANDROID_TRACE(("%s: ENTER\n", __FUNCTION__));

	assoc_maclist->count = htod32(MAX_NUM_OF_ASSOCLIST);

	error = wldev_ioctl_get(dev, WLC_GET_ASSOCLIST, assoc_maclist, sizeof(mac_buf));
	if (error)
		return -1;

	assoc_maclist->count = dtoh32(assoc_maclist->count);
	bytes_written = snprintf(command, total_len, "%s listcount: %d Stations:",
		CMD_ASSOC_CLIENTS, assoc_maclist->count);

	for (i = 0; i < assoc_maclist->count; i++) {
		bytes_written += snprintf(command + bytes_written, total_len, " " MACDBG,
			MAC2STRDBG(assoc_maclist->ea[i].octet));
	}
	return bytes_written;

}

#ifdef WL_CFG80211
extern chanspec_t
wl_chspec_host_to_driver(chanspec_t chanspec);
static int wl_android_set_csa(struct net_device *dev, char *command, int total_len)
{
	int error = 0;
	char smbuf[WLC_IOCTL_SMLEN];
	wl_chan_switch_t csa_arg;
	u32 chnsp = 0;
	int err = 0;

	ANDROID_INFO(("%s: command:%s\n", __FUNCTION__, command));

	command = (command + strlen(CMD_SET_CSA));
	/* Order is mode, count channel */
	if (!*++command) {
		ANDROID_ERROR(("%s:error missing arguments\n", __FUNCTION__));
		return -1;
	}
	csa_arg.mode = bcm_atoi(command);

	if (csa_arg.mode != 0 && csa_arg.mode != 1) {
		ANDROID_ERROR(("Invalid mode\n"));
		return -1;
	}

	if (!*++command) {
		ANDROID_ERROR(("%s:error missing count\n", __FUNCTION__));
		return -1;
	}
	command++;
	csa_arg.count = bcm_atoi(command);

	csa_arg.reg = 0;
	csa_arg.chspec = 0;
	command += 2;
	if (!*command) {
		ANDROID_ERROR(("%s:error missing channel\n", __FUNCTION__));
		return -1;
	}

	chnsp = wf_chspec_aton(command);
	if (chnsp == 0)	{
		ANDROID_ERROR(("%s:chsp is not correct\n", __FUNCTION__));
		return -1;
	}
	chnsp = wl_chspec_host_to_driver(chnsp);
	csa_arg.chspec = chnsp;

	if (chnsp & WL_CHANSPEC_BAND_5G) {
		u32 chanspec = chnsp;
		err = wldev_iovar_getint(dev, "per_chan_info", &chanspec);
		if (!err) {
			if ((chanspec & WL_CHAN_RADAR) || (chanspec & WL_CHAN_PASSIVE)) {
				ANDROID_ERROR(("Channel is radar sensitive\n"));
				return -1;
			}
			if (chanspec == 0) {
				ANDROID_ERROR(("Invalid hw channel\n"));
				return -1;
			}
		} else  {
			ANDROID_ERROR(("does not support per_chan_info\n"));
			return -1;
		}
		ANDROID_INFO(("non radar sensitivity\n"));
	}
	error = wldev_iovar_setbuf(dev, "csa", &csa_arg, sizeof(csa_arg),
		smbuf, sizeof(smbuf), NULL);
	if (error) {
		ANDROID_ERROR(("%s:set csa failed:%d\n", __FUNCTION__, error));
		return -1;
	}
	return 0;
}
#endif

static int
wl_android_set_max_dtim(struct net_device *dev, char *command, int total_len)
{
	int ret = 0;
	int dtim_flag;

	dtim_flag = *(command + strlen(CMD_MAXDTIM_IN_SUSPEND) + 1) - '0';

	if (!(ret = net_os_set_max_dtim_enable(dev, dtim_flag))) {
		ANDROID_TRACE(("%s: use Max bcn_li_dtim in suspend %s\n",
			__FUNCTION__, (dtim_flag ? "Enable" : "Disable")));
	} else {
		ANDROID_ERROR(("%s: failed %d\n", __FUNCTION__, ret));
	}

	return ret;
}

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

#ifdef CUSTOMER_HW4_PRIVATE_CMD

#ifdef WLTDLS
int wl_android_tdls_reset(struct net_device *dev)
{
	int ret = 0;
	ret = dhd_tdls_enable(dev, false, false, NULL);
	if (ret < 0) {
		ANDROID_ERROR(("Disable tdls failed. %d\n", ret));
		return ret;
	}
	ret = dhd_tdls_enable(dev, true, true, NULL);
	if (ret < 0) {
		ANDROID_ERROR(("enable tdls failed. %d\n", ret));
		return ret;
	}
	return 0;
}
#endif /* WLTDLS */
#ifdef FCC_PWR_LIMIT_2G
int
wl_android_set_fcc_pwr_limit_2g(struct net_device *dev, char *command, int total_len)
{
	int error = 0;
	int enable = 0;

	sscanf(command+sizeof("SET_FCC_CHANNEL"), "%d", &enable);

	if ((enable != CUSTOMER_HW4_ENABLE) && (enable != CUSTOMER_HW4_DISABLE)) {
		ANDROID_ERROR(("%s: Invalid data\n", __FUNCTION__));
		return BCME_ERROR;
	}

	CUSTOMER_HW4_EN_CONVERT(enable);

	ANDROID_ERROR(("%s: fccpwrlimit2g set (%d)\n", __FUNCTION__, enable));
	error = wldev_iovar_setint(dev, "fccpwrlimit2g", enable);
	if (error) {
		ANDROID_ERROR(("%s: fccpwrlimit2g set returned (%d)\n", __FUNCTION__, error));
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
		ANDROID_ERROR(("%s: fccpwrlimit2g get error (%d)\n", __FUNCTION__, error));
		return BCME_ERROR;
	}
	ANDROID_ERROR(("%s: fccpwrlimit2g get (%d)\n", __FUNCTION__, enable));

	bytes_written = snprintf(command, total_len, "%s %d", CMD_GET_FCC_PWR_LIMIT_2G, enable);

	return bytes_written;
}
#endif /* FCC_PWR_LIMIT_2G */

s32
wl_cfg80211_get_sta_info(struct net_device *dev, char* command, int total_len)
{
	static char iovar_buf[WLC_IOCTL_MAXLEN];
	int bytes_written = -1, ret = 0;
	char *pcmd = command;
	char *str;
	sta_info_t *sta = NULL;
	wl_cnt_wlc_t* wlc_cnt = NULL;
	struct ether_addr mac;

	/* Client information */
	uint16 cap = 0;
	uint32 rxrtry = 0;
	uint32 rxmulti = 0;

	ANDROID_TRACE(("%s\n", command));
	str = bcmstrtok(&pcmd, " ", NULL);
	if (str) {
		str = bcmstrtok(&pcmd, " ", NULL);
		/* If GETSTAINFO subcmd name is not provided, return error */
		if (str == NULL) {
			ANDROID_ERROR(("GETSTAINFO subcmd not provided %s\n", __FUNCTION__));
			goto error;
		}

		memset(&mac, 0, ETHER_ADDR_LEN);
		if ((bcm_ether_atoe((str), &mac))) {
			/* get the sta info */
			ret = wldev_iovar_getbuf(dev, "sta_info",
				(struct ether_addr *)mac.octet,
				ETHER_ADDR_LEN, iovar_buf, WLC_IOCTL_SMLEN, NULL);
			if (ret < 0) {
				ANDROID_ERROR(("Get sta_info ERR %d\n", ret));
				goto error;
			}

			sta = (sta_info_t *)iovar_buf;
			cap = dtoh16(sta->cap);
			rxrtry = dtoh32(sta->rx_pkts_retried);
			rxmulti = dtoh32(sta->rx_mcast_pkts);
		} else if ((!strncmp(str, "all", 3)) || (!strncmp(str, "ALL", 3))) {
			/* get counters info */
			ret = wldev_iovar_getbuf(dev, "counters", NULL, 0,
				iovar_buf, WLC_IOCTL_MAXLEN, NULL);
			if (unlikely(ret)) {
				ANDROID_ERROR(("counters error (%d) - size = %zu\n",
					ret, sizeof(wl_cnt_wlc_t)));
				goto error;
			}
			ret = wl_cntbuf_to_xtlv_format(NULL, iovar_buf, WL_CNTBUF_MAX_SIZE, 0);
			if (ret != BCME_OK) {
				ANDROID_ERROR(("wl_cntbuf_to_xtlv_format ERR %d\n", ret));
				goto error;
			}
			if (!(wlc_cnt = GET_WLCCNT_FROM_CNTBUF(iovar_buf))) {
				ANDROID_ERROR(("wlc_cnt NULL!\n"));
				goto error;
			}

			rxrtry = dtoh32(wlc_cnt->rxrtry);
			rxmulti = dtoh32(wlc_cnt->rxmulti);
		} else {
			ANDROID_ERROR(("Get address fail\n"));
			goto error;
		}
	} else {
		ANDROID_ERROR(("Command ERR\n"));
		goto error;
	}

	bytes_written = snprintf(command, total_len,
		"%s %s Rx_Retry_Pkts=%d Rx_BcMc_Pkts=%d CAP=%04x\n",
		CMD_GET_STA_INFO, str, rxrtry, rxmulti, cap);

	ANDROID_TRACE(("%s", command));

error:
	return bytes_written;
}
#endif

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
			ANDROID_ERROR(("%s: Failed to set wbtext error = %d\n",
				__FUNCTION__, error));
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
			ANDROID_ERROR(("%s: Failed to set wbtext error = %d\n",
				__FUNCTION__, error));
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
				ANDROID_ERROR(("%s: Failed to reset roam trigger = %d\n",
					__FUNCTION__, error));
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
			ANDROID_ERROR(("Failed to get wnm_bsstrans_timer_threshold (%d)\n", error));
			return error;
		}
		bytes_written = snprintf(command, total_len, "%d\n", data);
		return bytes_written;
	} else {
		if ((error = wldev_iovar_setint(dev, "wnm_bsstrans_timer_threshold",
				data)) != BCME_OK) {
			ANDROID_ERROR(("Failed to set wnm_bsstrans_timer_threshold (%d)\n", error));
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
			ANDROID_ERROR(("Failed to get wnm_btmdelta (%d)\n", error));
			return error;
		}
		bytes_written = snprintf(command, total_len, "%d\n", data);
		return bytes_written;
	} else {
		if ((error = wldev_iovar_setint(dev, "wnm_btmdelta",
				data)) != BCME_OK) {
			ANDROID_ERROR(("Failed to set wnm_btmdelta (%d)\n", error));
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
	uint i, tokens;
	char *pos, *pos2, *token, *token2, *delim;
	char param[PNO_PARAM_SIZE+1], value[VALUE_SIZE+1];
	struct dhd_pno_batch_params batch_params;

	ANDROID_INFO(("%s: command=%s, len=%d\n", __FUNCTION__, command, total_len));
	if (total_len < strlen(CMD_WLS_BATCHING)) {
		ANDROID_ERROR(("%s argument=%d less min size\n", __FUNCTION__, total_len));
		err = BCME_ERROR;
		goto exit;
	}
	pos = command + strlen(CMD_WLS_BATCHING) + 1;
	memset(&batch_params, 0, sizeof(struct dhd_pno_batch_params));

	if (!strncmp(pos, PNO_BATCHING_SET, strlen(PNO_BATCHING_SET))) {
		pos += strlen(PNO_BATCHING_SET) + 1;
		while ((token = strsep(&pos, PNO_PARAMS_DELIMETER)) != NULL) {
			memset(param, 0, sizeof(param));
			memset(value, 0, sizeof(value));
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
				ANDROID_INFO(("scan_freq : %d\n", batch_params.scan_fr));
			} else if (!strncmp(param, PNO_PARAM_BESTN, strlen(PNO_PARAM_BESTN))) {
				batch_params.bestn = simple_strtol(value, NULL, 0);
				ANDROID_INFO(("bestn : %d\n", batch_params.bestn));
			} else if (!strncmp(param, PNO_PARAM_MSCAN, strlen(PNO_PARAM_MSCAN))) {
				batch_params.mscan = simple_strtol(value, NULL, 0);
				ANDROID_INFO(("mscan : %d\n", batch_params.mscan));
			} else if (!strncmp(param, PNO_PARAM_CHANNEL, strlen(PNO_PARAM_CHANNEL))) {
				i = 0;
				pos2 = value;
				tokens = sscanf(value, "<%s>", value);
				if (tokens != 1) {
					err = BCME_ERROR;
					ANDROID_ERROR(("%s : invalid format for channel"
					" <> params\n", __FUNCTION__));
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
						ANDROID_INFO(("band : %s\n",
							(*token2 == 'A')? "A" : "B"));
					} else {
						if ((batch_params.nchan >= WL_NUMCHANNELS) ||
							(i >= WL_NUMCHANNELS)) {
							ANDROID_ERROR(("Too many nchan %d\n",
								batch_params.nchan));
							err = BCME_BUFTOOSHORT;
							goto exit;
						}
						batch_params.chan_list[i++] =
							simple_strtol(token2, NULL, 0);
						batch_params.nchan++;
						ANDROID_INFO(("channel :%d\n",
							batch_params.chan_list[i-1]));
					}
				 }
			} else if (!strncmp(param, PNO_PARAM_RTT, strlen(PNO_PARAM_RTT))) {
				batch_params.rtt = simple_strtol(value, NULL, 0);
				ANDROID_INFO(("rtt : %d\n", batch_params.rtt));
			} else {
				ANDROID_ERROR(("%s : unknown param: %s\n", __FUNCTION__, param));
				err = BCME_ERROR;
				goto exit;
			}
		}
		err = dhd_dev_pno_set_for_batch(dev, &batch_params);
		if (err < 0) {
			ANDROID_ERROR(("failed to configure batch scan\n"));
		} else {
			memset(command, 0, total_len);
			err = snprintf(command, total_len, "%d", err);
		}
	} else if (!strncmp(pos, PNO_BATCHING_GET, strlen(PNO_BATCHING_GET))) {
		err = dhd_dev_pno_get_for_batch(dev, command, total_len);
		if (err < 0) {
			ANDROID_ERROR(("failed to getting batching results\n"));
		} else {
			err = strlen(command);
		}
	} else if (!strncmp(pos, PNO_BATCHING_STOP, strlen(PNO_BATCHING_STOP))) {
		err = dhd_dev_pno_stop_for_batch(dev);
		if (err < 0) {
			ANDROID_ERROR(("failed to stop batching scan\n"));
		} else {
			memset(command, 0, total_len);
			err = snprintf(command, total_len, "OK");
		}
	} else {
		ANDROID_ERROR(("%s : unknown command\n", __FUNCTION__));
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
	ANDROID_INFO(("%s: command=%s, len=%d\n", __FUNCTION__, command, total_len));

	if (total_len < (strlen(CMD_PNOSETUP_SET) + sizeof(cmd_tlv_t))) {
		ANDROID_ERROR(("%s argument=%d less min size\n", __FUNCTION__, total_len));
		goto exit_proc;
	}
#ifdef PNO_SET_DEBUG
	memcpy(command, pno_in_example, sizeof(pno_in_example));
	total_len = sizeof(pno_in_example);
#endif
	str_ptr = command + strlen(CMD_PNOSETUP_SET);
	tlv_size_left = total_len - strlen(CMD_PNOSETUP_SET);

	cmd_tlv_temp = (cmd_tlv_t *)str_ptr;
	memset(ssids_local, 0, sizeof(ssids_local));

	if ((cmd_tlv_temp->prefix == PNO_TLV_PREFIX) &&
		(cmd_tlv_temp->version == PNO_TLV_VERSION) &&
		(cmd_tlv_temp->subtype == PNO_TLV_SUBTYPE_LEGACY_PNO)) {

		str_ptr += sizeof(cmd_tlv_t);
		tlv_size_left -= sizeof(cmd_tlv_t);

		if ((nssid = wl_parse_ssid_list_tlv(&str_ptr, ssids_local,
			MAX_PFN_LIST_COUNT, &tlv_size_left)) <= 0) {
			ANDROID_ERROR(("SSID is not presented or corrupted ret=%d\n", nssid));
			goto exit_proc;
		} else {
			if ((str_ptr[0] != PNO_TLV_TYPE_TIME) || (tlv_size_left <= 1)) {
				ANDROID_ERROR(("%s scan duration corrupted field size %d\n",
					__FUNCTION__, tlv_size_left));
				goto exit_proc;
			}
			str_ptr++;
			pno_time = simple_strtoul(str_ptr, &str_ptr, 16);
			ANDROID_INFO(("%s: pno_time=%d\n", __FUNCTION__, pno_time));

			if (str_ptr[0] != 0) {
				if ((str_ptr[0] != PNO_TLV_FREQ_REPEAT)) {
					ANDROID_ERROR(("%s pno repeat : corrupted field\n",
						__FUNCTION__));
					goto exit_proc;
				}
				str_ptr++;
				pno_repeat = simple_strtoul(str_ptr, &str_ptr, 16);
				ANDROID_INFO(("%s :got pno_repeat=%d\n", __FUNCTION__, pno_repeat));
				if (str_ptr[0] != PNO_TLV_FREQ_EXPO_MAX) {
					ANDROID_ERROR(("%s FREQ_EXPO_MAX corrupted field size\n",
						__FUNCTION__));
					goto exit_proc;
				}
				str_ptr++;
				pno_freq_expo_max = simple_strtoul(str_ptr, &str_ptr, 16);
				ANDROID_INFO(("%s: pno_freq_expo_max=%d\n",
					__FUNCTION__, pno_freq_expo_max));
			}
		}
	} else {
		ANDROID_ERROR(("%s get wrong TLV command\n", __FUNCTION__));
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
	int bytes_written = 0;

	ret = wl_cfg80211_get_p2p_dev_addr(ndev, (struct ether_addr*)command);
	if (ret)
		return 0;
	bytes_written = sizeof(struct ether_addr);
	return bytes_written;
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
		ANDROID_ERROR(("%s : WLC_SET_MACMODE error=%d\n", __FUNCTION__, ret));
		return ret;
	}
	if (macmode != MACLIST_MODE_DISABLED) {
		/* set the MAC filter list */
		if ((ret = wldev_ioctl_set(dev, WLC_SET_MACLIST, maclist,
			sizeof(int) + sizeof(struct ether_addr) * maclist->count)) != 0) {
			ANDROID_ERROR(("%s : WLC_SET_MACLIST error=%d\n", __FUNCTION__, ret));
			return ret;
		}
		/* get the current list of associated STAs */
		assoc_maclist->count = MAX_NUM_OF_ASSOCLIST;
		if ((ret = wldev_ioctl_get(dev, WLC_GET_ASSOCLIST, assoc_maclist,
			sizeof(mac_buf))) != 0) {
			ANDROID_ERROR(("%s : WLC_GET_ASSOCLIST error=%d\n", __FUNCTION__, ret));
			return ret;
		}
		/* do we have any STA associated?  */
		if (assoc_maclist->count) {
			/* iterate each associated STA */
			for (i = 0; i < assoc_maclist->count; i++) {
				match = 0;
				/* compare with each entry */
				for (j = 0; j < maclist->count; j++) {
					ANDROID_INFO(("%s : associated="MACDBG " list="MACDBG "\n",
					__FUNCTION__, MAC2STRDBG(assoc_maclist->ea[i].octet),
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
						ANDROID_ERROR(("%s WLC_SCB_DEAUTHENTICATE error=%d\n",
							__FUNCTION__, ret));
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

	/* string should look like below (macmode/macnum/maclist) */
	/*   1 2 00:11:22:33:44:55 00:11:22:33:44:ff  */

	/* get the MAC filter mode */
	token = strsep((char**)&str, " ");
	if (!token) {
		return -1;
	}
	macmode = bcm_atoi(token);

	if (macmode < MACLIST_MODE_DISABLED || macmode > MACLIST_MODE_ALLOW) {
		ANDROID_ERROR(("%s : invalid macmode %d\n", __FUNCTION__, macmode));
		return -1;
	}

	token = strsep((char**)&str, " ");
	if (!token) {
		return -1;
	}
	macnum = bcm_atoi(token);
	if (macnum < 0 || macnum > MAX_NUM_MAC_FILT) {
		ANDROID_ERROR(("%s : invalid number of MAC address entries %d\n",
			__FUNCTION__, macnum));
		return -1;
	}
	/* allocate memory for the MAC list */
	list = (struct maclist*)kmalloc(sizeof(int) +
		sizeof(struct ether_addr) * macnum, GFP_KERNEL);
	if (!list) {
		ANDROID_ERROR(("%s : failed to allocate memory\n", __FUNCTION__));
		return -1;
	}
	/* prepare the MAC list */
	list->count = htod32(macnum);
	bzero((char *)eabuf, ETHER_ADDR_STR_LEN);
	for (i = 0; i < list->count; i++) {
		strncpy(eabuf, strsep((char**)&str, " "), ETHER_ADDR_STR_LEN - 1);
		if (!(ret = bcm_ether_atoe(eabuf, &list->ea[i]))) {
			ANDROID_ERROR(("%s : mac parsing err index=%d, addr=%s\n",
				__FUNCTION__, i, eabuf));
			list->count--;
			break;
		}
		ANDROID_INFO(("%s : %d/%d MACADDR=%s", __FUNCTION__, i, list->count, eabuf));
	}
	/* set the list */
	if ((ret = wl_android_set_ap_mac_list(dev, macmode, list)) != 0)
		ANDROID_ERROR(("%s : Setting MAC list failed error=%d\n", __FUNCTION__, ret));

	kfree(list);

	return 0;
}

/**
 * Global function definitions (declared in wl_android.h)
 */

int wl_android_wifi_on(struct net_device *dev)
{
	int ret = 0;
	int retry = POWERUP_MAX_RETRY;

	if (!dev) {
		ANDROID_ERROR(("%s: dev is null\n", __FUNCTION__));
		return -EINVAL;
	}

	printf("%s in 1\n", __FUNCTION__);
	dhd_net_if_lock(dev);
	printf("%s in 2: g_wifi_on=%d\n", __FUNCTION__, g_wifi_on);
	if (!g_wifi_on) {
		do {
			if (!dhd_net_wifi_platform_set_power(dev, TRUE, WIFI_TURNON_DELAY)) {
#ifdef BCMSDIO
			ret = dhd_net_bus_resume(dev, 0);
#endif /* BCMSDIO */
			}
#ifdef BCMPCIE
			ret = dhd_net_bus_devreset(dev, FALSE);
#endif /* BCMPCIE */
			if (ret == 0) {
				break;
			}
			ANDROID_ERROR(("\nfailed to power up wifi chip, retry again (%d left) **\n\n",
				retry));
#ifdef BCMPCIE
			dhd_net_bus_devreset(dev, TRUE);
#endif /* BCMPCIE */
			dhd_net_wifi_platform_set_power(dev, FALSE, WIFI_TURNOFF_DELAY);
		} while (retry-- > 0);
		if (ret != 0) {
			ANDROID_ERROR(("\nfailed to power up wifi chip, max retry reached **\n\n"));
			goto exit;
		}
#if defined(BCMSDIO) || defined(BCMDBUS)
		ret = dhd_net_bus_devreset(dev, FALSE);
		if (ret)
			goto err;
#ifdef BCMSDIO
		dhd_net_bus_resume(dev, 1);
#endif /* BCMSDIO */
#endif /* BCMSDIO || BCMDBUS */
#if defined(BCMSDIO) || defined(BCMDBUS)
		if (!ret) {
			if (dhd_dev_init_ioctl(dev) < 0) {
				ret = -EFAULT;
				goto err;
			}
		}
#endif /* BCMSDIO || BCMDBUS */
		g_wifi_on = TRUE;
	}

exit:
	printf("%s: Success\n", __FUNCTION__);
	dhd_net_if_unlock(dev);
	return ret;

#if defined(BCMSDIO) || defined(BCMDBUS)
err:
	dhd_net_bus_devreset(dev, TRUE);
#ifdef BCMSDIO
	dhd_net_bus_suspend(dev);
#endif /* BCMSDIO */
	dhd_net_wifi_platform_set_power(dev, FALSE, WIFI_TURNOFF_DELAY);
	printf("%s: Failed\n", __FUNCTION__);
	dhd_net_if_unlock(dev);
	return ret;
#endif /* BCMSDIO || BCMDBUS */
}

int wl_android_wifi_off(struct net_device *dev, bool on_failure)
{
	int ret = 0;

	if (!dev) {
		ANDROID_ERROR(("%s: dev is null\n", __FUNCTION__));
		return -EINVAL;
	}

	printf("%s in 1\n", __FUNCTION__);
#if defined(BCMPCIE) && defined(DHD_DEBUG_UART)
	ret = dhd_debug_uart_is_running(dev);
	if (ret) {
		ANDROID_ERROR(("%s - Debug UART App is running\n", __FUNCTION__));
		return -EBUSY;
	}
#endif	/* BCMPCIE && DHD_DEBUG_UART */
	dhd_net_if_lock(dev);
	printf("%s in 2: g_wifi_on=%d, on_failure=%d\n", __FUNCTION__, g_wifi_on, on_failure);
	if (g_wifi_on || on_failure) {
#if defined(BCMSDIO) || defined(BCMPCIE) || defined(BCMDBUS)
		ret = dhd_net_bus_devreset(dev, TRUE);
#if  defined(BCMSDIO)
		dhd_net_bus_suspend(dev);
#endif /* BCMSDIO */
#endif /* BCMSDIO || BCMPCIE || BCMDBUS */
		dhd_net_wifi_platform_set_power(dev, FALSE, WIFI_TURNOFF_DELAY);
		g_wifi_on = FALSE;
	}
	printf("%s out\n", __FUNCTION__);
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

	memset(&param, 0, sizeof(param));

	param.buflen = htod32(sizeof(wl_chanim_stats_t));
	param.count = htod32(WL_CHANIM_COUNT_ONE);

	if ((err = wldev_iovar_getbuf(dev, "chanim_stats", (char*)&param, sizeof(wl_chanim_stats_t),
		(char*)result, sizeof(result), 0)) < 0) {
		ANDROID_ERROR(("Failed to get chanim results %d \n", err));
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
		ANDROID_ERROR(("Sorry, firmware has wl_chanim_stats version %d "
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

	ANDROID_INFO(("chanspec: 0x%4x glitch: %d badplcp: %d idle: %d timestamp: %d\n",
		stats->chanspec, stats->glitchcnt, stats->badplcp, stats->chan_idle,
		stats->timestamp));

	*chan_idle = stats->chan_idle;

	return (err);
}

static int
wl_android_get_connection_stats(struct net_device *dev, char *command, int total_len)
{
	static char iovar_buf[WLC_IOCTL_MAXLEN];
	wl_cnt_wlc_t* wlc_cnt = NULL;
#ifndef DISABLE_IF_COUNTERS
	wl_if_stats_t* if_stats = NULL;
#endif /* DISABLE_IF_COUNTERS */

	int link_speed = 0;
	struct connection_stats *output;
	unsigned int bufsize = 0;
	int bytes_written = -1;
	int ret = 0;

	ANDROID_INFO(("%s: enter Get Connection Stats\n", __FUNCTION__));

	if (total_len <= 0) {
		ANDROID_ERROR(("%s: invalid buffer size %d\n", __FUNCTION__, total_len));
		goto error;
	}

	bufsize = total_len;
	if (bufsize < sizeof(struct connection_stats)) {
		ANDROID_ERROR(("%s: not enough buffer size, provided=%u, requires=%zu\n",
			__FUNCTION__, bufsize,
			sizeof(struct connection_stats)));
		goto error;
	}

	output = (struct connection_stats *)command;

#ifndef DISABLE_IF_COUNTERS
	if ((if_stats = kmalloc(sizeof(*if_stats), GFP_KERNEL)) == NULL) {
		ANDROID_ERROR(("%s(%d): kmalloc failed\n", __FUNCTION__, __LINE__));
		goto error;
	}
	memset(if_stats, 0, sizeof(*if_stats));

	ret = wldev_iovar_getbuf(dev, "if_counters", NULL, 0,
		(char *)if_stats, sizeof(*if_stats), NULL);
	if (ret) {
		ANDROID_ERROR(("%s: if_counters not supported ret=%d\n",
			__FUNCTION__, ret));

		/* In case if_stats IOVAR is not supported, get information from counters. */
#endif /* DISABLE_IF_COUNTERS */
		ret = wldev_iovar_getbuf(dev, "counters", NULL, 0,
			iovar_buf, WLC_IOCTL_MAXLEN, NULL);
		if (unlikely(ret)) {
			ANDROID_ERROR(("counters error (%d) - size = %zu\n", ret, sizeof(wl_cnt_wlc_t)));
			goto error;
		}
		ret = wl_cntbuf_to_xtlv_format(NULL, iovar_buf, WL_CNTBUF_MAX_SIZE, 0);
		if (ret != BCME_OK) {
			ANDROID_ERROR(("%s wl_cntbuf_to_xtlv_format ERR %d\n",
			__FUNCTION__, ret));
			goto error;
		}

		if (!(wlc_cnt = GET_WLCCNT_FROM_CNTBUF(iovar_buf))) {
			ANDROID_ERROR(("%s wlc_cnt NULL!\n", __FUNCTION__));
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
			ANDROID_ERROR(("%s: incorrect version of wl_if_stats_t, expected=%u got=%u\n",
				__FUNCTION__,  WL_IF_STATS_T_VERSION, if_stats->version));
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
		ANDROID_ERROR(("%s: wldev_get_link_speed() failed, ret=%d, speed=%d\n",
			__FUNCTION__, ret, link_speed));
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
		kfree(if_stats);
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
		ANDROID_ERROR(("natoe subcmd not provided %s\n", __FUNCTION__));
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
		ANDROID_ERROR(("%s: get command failed code %d\n", __FUNCTION__, res));
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
	uint16 kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;
	uint16 iocsz = sizeof(*natoe_ioc) + WL_NATOE_IOC_BUFSZ;
	uint16 buflen = WL_NATOE_IOC_BUFSZ;
	bcm_xtlv_t *pxtlv = NULL;
	char *ioctl_buf = NULL;

	ioctl_buf = kzalloc(WLC_IOCTL_MEDLEN, kflags);
	if (!ioctl_buf) {
		ANDROID_ERROR(("ioctl memory alloc failed\n"));
		return -ENOMEM;
	}

	/* alloc mem for ioctl headr + tlv data */
	natoe_ioc = kzalloc(iocsz, kflags);
	if (!natoe_ioc) {
		ANDROID_ERROR(("ioctl header memory alloc failed\n"));
		kfree(ioctl_buf);
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
			ANDROID_ERROR(("Fail to get iovar %s\n", __FUNCTION__));
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
			ANDROID_ERROR(("Fail to set iovar %d\n", ret));
			ret = -EINVAL;
		}
	}

exit:
	kfree(ioctl_buf);
	kfree(natoe_ioc);

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
	uint16 kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;
	uint16 iocsz = sizeof(*natoe_ioc) + WL_NATOE_IOC_BUFSZ;
	uint16 buflen = WL_NATOE_IOC_BUFSZ;
	bcm_xtlv_t *pxtlv = NULL;
	char *ioctl_buf = NULL;

	ioctl_buf = kzalloc(WLC_IOCTL_MEDLEN, kflags);
	if (!ioctl_buf) {
		ANDROID_ERROR(("ioctl memory alloc failed\n"));
		return -ENOMEM;
	}

	/* alloc mem for ioctl headr + tlv data */
	natoe_ioc = kzalloc(iocsz, kflags);
	if (!natoe_ioc) {
		ANDROID_ERROR(("ioctl header memory alloc failed\n"));
		kfree(ioctl_buf);
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
			ANDROID_ERROR(("Fail to get iovar %s\n", __FUNCTION__));
			ret = -EINVAL;
		}
	} else {	/* set */
		/* buflen is max tlv data we can write, it will be decremented as we pack */
		/* save buflen at start */
		uint16 buflen_at_start = buflen;

		memset(&config_ips, 0, sizeof(config_ips));

		str = bcmstrtok(&pcmd, " ", NULL);
		if (!str || !bcm_atoipv4(str, (struct ipv4_addr *)&config_ips.sta_ip)) {
			ANDROID_ERROR(("Invalid STA IP addr %s\n", str));
			ret = -EINVAL;
			goto exit;
		}

		str = bcmstrtok(&pcmd, " ", NULL);
		if (!str || !bcm_atoipv4(str, (struct ipv4_addr *)&config_ips.sta_netmask)) {
			ANDROID_ERROR(("Invalid STA netmask %s\n", str));
			ret = -EINVAL;
			goto exit;
		}

		str = bcmstrtok(&pcmd, " ", NULL);
		if (!str || !bcm_atoipv4(str, (struct ipv4_addr *)&config_ips.sta_router_ip)) {
			ANDROID_ERROR(("Invalid STA router IP addr %s\n", str));
			ret = -EINVAL;
			goto exit;
		}

		str = bcmstrtok(&pcmd, " ", NULL);
		if (!str || !bcm_atoipv4(str, (struct ipv4_addr *)&config_ips.sta_dnsip)) {
			ANDROID_ERROR(("Invalid STA DNS IP addr %s\n", str));
			ret = -EINVAL;
			goto exit;
		}

		str = bcmstrtok(&pcmd, " ", NULL);
		if (!str || !bcm_atoipv4(str, (struct ipv4_addr *)&config_ips.ap_ip)) {
			ANDROID_ERROR(("Invalid AP IP addr %s\n", str));
			ret = -EINVAL;
			goto exit;
		}

		str = bcmstrtok(&pcmd, " ", NULL);
		if (!str || !bcm_atoipv4(str, (struct ipv4_addr *)&config_ips.ap_netmask)) {
			ANDROID_ERROR(("Invalid AP netmask %s\n", str));
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
			ANDROID_ERROR(("Fail to set iovar %d\n", ret));
			ret = -EINVAL;
		}
	}

exit:
	kfree(ioctl_buf);
	kfree(natoe_ioc);

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
	uint16 kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;
	uint16 iocsz = sizeof(*natoe_ioc) + WL_NATOE_IOC_BUFSZ;
	uint16 buflen = WL_NATOE_IOC_BUFSZ;
	bcm_xtlv_t *pxtlv = NULL;
	char *ioctl_buf = NULL;

	ioctl_buf = kzalloc(WLC_IOCTL_MEDLEN, kflags);
	if (!ioctl_buf) {
		ANDROID_ERROR(("ioctl memory alloc failed\n"));
		return -ENOMEM;
	}

	/* alloc mem for ioctl headr + tlv data */
	natoe_ioc = kzalloc(iocsz, kflags);
	if (!natoe_ioc) {
		ANDROID_ERROR(("ioctl header memory alloc failed\n"));
		kfree(ioctl_buf);
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
			ANDROID_ERROR(("Fail to get iovar %s\n", __FUNCTION__));
			ret = -EINVAL;
		}
	} else {	/* set */
		/* buflen is max tlv data we can write, it will be decremented as we pack */
		/* save buflen at start */
		uint16 buflen_at_start = buflen;

		memset(&ports_config, 0, sizeof(ports_config));

		str = bcmstrtok(&pcmd, " ", NULL);
		if (!str) {
			ANDROID_ERROR(("Invalid port string %s\n", str));
			ret = -EINVAL;
			goto exit;
		}
		ports_config.start_port_num = htod16(bcm_atoi(str));

		str = bcmstrtok(&pcmd, " ", NULL);
		if (!str) {
			ANDROID_ERROR(("Invalid port string %s\n", str));
			ret = -EINVAL;
			goto exit;
		}
		ports_config.no_of_ports = htod16(bcm_atoi(str));

		if ((uint32)(ports_config.start_port_num + ports_config.no_of_ports) >
				NATOE_MAX_PORT_NUM) {
			ANDROID_ERROR(("Invalid port configuration\n"));
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
			ANDROID_ERROR(("Fail to set iovar %d\n", ret));
			ret = -EINVAL;
		}
	}

exit:
	kfree(ioctl_buf);
	kfree(natoe_ioc);

	return ret;
}

static int
wl_android_natoe_subcmd_dbg_stats(struct net_device *dev, const wl_natoe_sub_cmd_t *cmd,
		char *command, wl_natoe_cmd_info_t *cmd_info)
{
	int ret = BCME_OK;
	wl_natoe_ioc_t *natoe_ioc;
	char *pcmd = command;
	uint16 kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;
	uint16 iocsz = sizeof(*natoe_ioc) + WL_NATOE_DBG_STATS_BUFSZ;
	uint16 buflen = WL_NATOE_DBG_STATS_BUFSZ;
	bcm_xtlv_t *pxtlv = NULL;
	char *ioctl_buf = NULL;

	ioctl_buf = kzalloc(WLC_IOCTL_MAXLEN, kflags);
	if (!ioctl_buf) {
		ANDROID_ERROR(("ioctl memory alloc failed\n"));
		return -ENOMEM;
	}

	/* alloc mem for ioctl headr + tlv data */
	natoe_ioc = kzalloc(iocsz, kflags);
	if (!natoe_ioc) {
		ANDROID_ERROR(("ioctl header memory alloc failed\n"));
		kfree(ioctl_buf);
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
			ANDROID_ERROR(("Fail to get iovar %s\n", __FUNCTION__));
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
			ANDROID_ERROR(("Fail to set iovar %d\n", ret));
			ret = -EINVAL;
		}
	}

exit:
	kfree(ioctl_buf);
	kfree(natoe_ioc);

	return ret;
}

static int
wl_android_natoe_subcmd_tbl_cnt(struct net_device *dev, const wl_natoe_sub_cmd_t *cmd,
		char *command, wl_natoe_cmd_info_t *cmd_info)
{
	int ret = BCME_OK;
	wl_natoe_ioc_t *natoe_ioc;
	char *pcmd = command;
	uint16 kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;
	uint16 iocsz = sizeof(*natoe_ioc) + WL_NATOE_IOC_BUFSZ;
	uint16 buflen = WL_NATOE_IOC_BUFSZ;
	bcm_xtlv_t *pxtlv = NULL;
	char *ioctl_buf = NULL;

	ioctl_buf = kzalloc(WLC_IOCTL_MEDLEN, kflags);
	if (!ioctl_buf) {
		ANDROID_ERROR(("ioctl memory alloc failed\n"));
		return -ENOMEM;
	}

	/* alloc mem for ioctl headr + tlv data */
	natoe_ioc = kzalloc(iocsz, kflags);
	if (!natoe_ioc) {
		ANDROID_ERROR(("ioctl header memory alloc failed\n"));
		kfree(ioctl_buf);
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
			ANDROID_ERROR(("Fail to get iovar %s\n", __FUNCTION__));
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
			ANDROID_ERROR(("Fail to set iovar %d\n", ret));
			ret = -EINVAL;
		}
	}

exit:
	kfree(ioctl_buf);
	kfree(natoe_ioc);

	return ret;
}

#endif /* WL_NATOE */

#ifdef CUSTOMER_HW4_PRIVATE_CMD
#endif /* CUSTOMER_HW4_PRIVATE_CMD */

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
	char *pos = command;
	int band_new, band_cur;

	if (cmd_str) {
		ANDROID_INFO(("Command: %s len:%d \n", cmd_str, (int)strlen(cmd_str)));
		if (strncmp(cmd_str, APCS_BAND_AUTO, strlen(APCS_BAND_AUTO)) == 0) {
			band = WLC_BAND_AUTO;
		} else if (strncmp(cmd_str, APCS_BAND_5G, strlen(APCS_BAND_5G)) == 0) {
			band = WLC_BAND_5G;
		} else if (strncmp(cmd_str, APCS_BAND_2G, strlen(APCS_BAND_2G)) == 0) {
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
				ANDROID_ERROR(("%s: Invalid argument\n", __FUNCTION__));
				return -EINVAL;
			}
		}
	} else {
		/* If no argument is provided, default to 2G */
		ANDROID_ERROR(("%s: No argument given default to 2.4G scan\n", __FUNCTION__));
		band = WLC_BAND_2G;
	}
	ANDROID_INFO(("%s : HAPD_AUTO_CHANNEL = %d, band=%d \n", __FUNCTION__, channel, band));

	ret = wldev_ioctl_set(dev, WLC_GET_BAND, &band_cur, sizeof(band_cur));

	if ((ret =
	     wldev_ioctl_get(dev, WLC_GET_SPECT_MANAGMENT, &spect, sizeof(spect))) < 0) {
		ANDROID_ERROR(("%s: ACS: error getting the spect\n", __FUNCTION__));
		goto done;
	}

	if (spect > 0) {
		/* If STA is connected, return is STA channel, else ACS can be issued,
		 * set spect to 0 and proceed with ACS
		 */
		channel = wl_cfg80211_get_sta_channel(dev);
		if (channel) {
			channel = (channel <= CH_MAX_2G_CHANNEL) ? channel : APCS_DEFAULT_2G_CH;
			goto done2;
		}

		if ((ret = wl_cfg80211_set_spect(dev, 0) < 0)) {
			ANDROID_ERROR(("ACS: error while setting spect\n"));
			goto done;
		}
	}

	reqbuf = kzalloc(CHANSPEC_BUF_SIZE, GFP_KERNEL);
	if (reqbuf == NULL) {
		ANDROID_ERROR(("%s: failed to allocate chanspec buffer\n", __FUNCTION__));
		return -ENOMEM;
	}

	if (band == WLC_BAND_AUTO) {
		ANDROID_INFO(("%s: ACS full channel scan \n", __func__));
		reqbuf[0] = htod32(0);
	} else if (band == WLC_BAND_5G) {
		band_new = band_cur==WLC_BAND_2G ? band_cur : WLC_BAND_5G;
		ret = wldev_ioctl_set(dev, WLC_SET_BAND, &band_new, sizeof(band_new));
		if (ret < 0)
			WL_ERR(("WLC_SET_BAND error %d\n", ret));
		ANDROID_INFO(("%s: ACS 5G band scan \n", __func__));
		if ((ret = wl_cfg80211_get_chanspecs_5g(dev, reqbuf, CHANSPEC_BUF_SIZE)) < 0) {
			ANDROID_ERROR(("ACS 5g chanspec retreival failed! \n"));
			goto done;
		}
	} else if (band == WLC_BAND_2G) {
		/*
		 * If channel argument is not provided/ argument 20 is provided,
		 * Restrict channel to 2GHz, 20MHz BW, No SB
		 */
		ANDROID_INFO(("%s: ACS 2G band scan \n", __func__));
		if ((ret = wl_cfg80211_get_chanspecs_2g(dev, reqbuf, CHANSPEC_BUF_SIZE)) < 0) {
			ANDROID_ERROR(("ACS 2g chanspec retreival failed! \n"));
			goto done;
		}
	} else {
		ANDROID_ERROR(("ACS: No band chosen\n"));
		goto done2;
	}

	buf_size = CHANSPEC_BUF_SIZE;
	ret = wldev_ioctl_set(dev, WLC_START_CHANNEL_SEL, (void *)reqbuf,
		buf_size);
	if (ret < 0) {
		ANDROID_ERROR(("%s: can't start auto channel scan, err = %d\n",
			__FUNCTION__, ret));
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

		if ((ret == 0) && (dtoh32(chosen) != 0)) {
			uint chip;
			chip = dhd_conf_get_chip(dhd_get_pub(dev));
			if (chip != BCM43143_CHIP_ID) {
				u32 chanspec = 0;
				chanspec = wl_chspec_driver_to_host(chosen);
				ANDROID_INFO(("%s: selected chanspec = 0x%x\n", __FUNCTION__, chanspec));
				chosen = wf_chspec_ctlchan(chanspec);
				ANDROID_INFO(("%s: selected chosen = 0x%x\n", __FUNCTION__, chosen));
			}
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
			if (band == WLC_BAND_AUTO) {
				printf("%s: selected channel = %d\n", __FUNCTION__, channel);
				break;
			} else if (apcs_band == chosen_band) {
				printf("%s: selected channel = %d\n", __FUNCTION__, channel);
				break;
			}
		}
		ANDROID_INFO(("%s: %d tried, ret = %d, chosen = 0x%x\n", __FUNCTION__,
			(APCS_MAX_RETRY - retry), ret, chosen));
		OSL_SLEEP(250);
	}

done:
	if ((retry == 0) || (ret < 0)) {
		/* On failure, fallback to a default channel */
		if ((band == WLC_BAND_5G)) {
			channel = APCS_DEFAULT_5G_CH;
		} else {
			channel = APCS_DEFAULT_2G_CH;
		}
		ANDROID_ERROR(("%s: ACS failed."
			" Fall back to default channel (%d) \n", __FUNCTION__, channel));
	}
done2:
	ret = wldev_ioctl_set(dev, WLC_SET_BAND, &band_cur, sizeof(band_cur));
	if (ret < 0)
		WL_ERR(("WLC_SET_BAND error %d\n", ret));
	if (spect > 0) {
		if ((ret = wl_cfg80211_set_spect(dev, spect) < 0)) {
			ANDROID_ERROR(("%s: ACS: error while setting spect\n", __FUNCTION__));
		}
	}

	if (reqbuf) {
		kfree(reqbuf);
	}

	if (channel) {
		if (channel < 15)
			pos += snprintf(pos, total_len, "2g=");
		else
			pos += snprintf(pos, total_len, "5g=");
		pos += snprintf(pos, total_len, "%d", channel);
		ANDROID_INFO(("%s: command result is %s \n", __FUNCTION__, command));
		return strlen(command);
	} else {
		return ret;
	}
}
#endif /* WL_SUPPORT_AUTO_CHANNEL */

#ifdef CUSTOMER_HW4_PRIVATE_CMD


#ifdef SUPPORT_SET_LPC
static int
wl_android_set_lpc(struct net_device *dev, const char* string_num)
{
	int lpc_enabled, ret;
	s32 val = 1;

	lpc_enabled = bcm_atoi(string_num);
	ANDROID_INFO(("%s : HAPD_LPC_ENABLED = %d\n", __FUNCTION__, lpc_enabled));

	ret = wldev_ioctl_set(dev, WLC_DOWN, &val, sizeof(s32));
	if (ret < 0)
		ANDROID_ERROR(("WLC_DOWN error %d\n", ret));

	wldev_iovar_setint(dev, "lpc", lpc_enabled);

	ret = wldev_ioctl_set(dev, WLC_UP, &val, sizeof(s32));
	if (ret < 0)
		ANDROID_ERROR(("WLC_UP error %d\n", ret));

	return 1;
}
#endif /* SUPPORT_SET_LPC */

static int
wl_android_ch_res_rl(struct net_device *dev, bool change)
{
	int error = 0;
	s32 srl = 7;
	s32 lrl = 4;
	printk("%s enter\n", __FUNCTION__);
	if (change) {
		srl = 4;
		lrl = 2;
	}
	error = wldev_ioctl_set(dev, WLC_SET_SRL, &srl, sizeof(s32));
	if (error) {
		ANDROID_ERROR(("Failed to set SRL, error = %d\n", error));
	}
	error = wldev_ioctl_set(dev, WLC_SET_LRL, &lrl, sizeof(s32));
	if (error) {
		ANDROID_ERROR(("Failed to set LRL, error = %d\n", error));
	}
	return error;
}


#ifdef WL_RELMCAST
static int
wl_android_rmc_enable(struct net_device *net, int rmc_enable)
{
	int err;

	err = wldev_iovar_setint(net, "rmc_ackreq", rmc_enable);
	return err;
}

static int
wl_android_rmc_set_leader(struct net_device *dev, const char* straddr)
{
	int error  = BCME_OK;
	char smbuf[WLC_IOCTL_SMLEN];
	wl_rmc_entry_t rmc_entry;
	ANDROID_INFO(("%s: Set new RMC leader %s\n", __FUNCTION__, straddr));

	memset(&rmc_entry, 0, sizeof(wl_rmc_entry_t));
	if (!bcm_ether_atoe(straddr, &rmc_entry.addr)) {
		if (strlen(straddr) == 1 && bcm_atoi(straddr) == 0) {
			ANDROID_INFO(("%s: Set auto leader selection mode\n", __FUNCTION__));
			memset(&rmc_entry, 0, sizeof(wl_rmc_entry_t));
		} else {
			ANDROID_ERROR(("%s: No valid mac address provided\n",
				__FUNCTION__));
			return BCME_ERROR;
		}
	}

	error = wldev_iovar_setbuf(dev, "rmc_ar", &rmc_entry, sizeof(wl_rmc_entry_t),
		smbuf, sizeof(smbuf), NULL);

	if (error != BCME_OK) {
		ANDROID_ERROR(("%s: Unable to set RMC leader, error = %d\n",
			__FUNCTION__, error));
	}

	return error;
}

static int wl_android_set_rmc_event(struct net_device *dev, char *command, int total_len)
{
	int err = 0;
	int pid = 0;

	if (sscanf(command, CMD_SET_RMC_EVENT " %d", &pid) <= 0) {
		ANDROID_ERROR(("Failed to get Parameter from : %s\n", command));
		return -1;
	}

	/* set pid, and if the event was happened, let's send a notification through netlink */
	wl_cfg80211_set_rmc_pid(dev, pid);

	ANDROID_TRACE(("RMC pid=%d\n", pid));

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
		ANDROID_ERROR(("%s: Failed to get single core scan Mode, error = %d\n",
			__FUNCTION__, error));
		return -1;
	}

	bytes_written = snprintf(command, total_len, "%s %d", CMD_GET_SCSCAN, mode);

	return bytes_written;
}

int wl_android_set_singlecore_scan(struct net_device *dev, char *command, int total_len)
{
	int error = 0;
	int mode = 0;

	if (sscanf(command, "%*s %d", &mode) != 1) {
		ANDROID_ERROR(("%s: Failed to get Parameter\n", __FUNCTION__));
		return -1;
	}

	error = wldev_iovar_setint(dev, "scan_ps", mode);
	if (error) {
		ANDROID_ERROR(("%s[1]: Failed to set Mode %d, error = %d\n",
		__FUNCTION__, mode, error));
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
		ANDROID_ERROR(("%s: dbm is negative...\n", __FUNCTION__));
		return -EINVAL;
	}

	if (dbm == -1)
		type = NL80211_TX_POWER_AUTOMATIC;
	else
		type = NL80211_TX_POWER_FIXED;

	err = wl_set_tx_power(dev, type, dbm);
	if (unlikely(err)) {
		ANDROID_ERROR(("%s: error (%d)\n", __FUNCTION__, err));
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
		ANDROID_ERROR(("%s: error (%d)\n", __FUNCTION__, err));
		return err;
	}

	bytes_written = snprintf(command, total_len, "%s %d",
		CMD_TEST_GET_TX_POWER, dbm);

	ANDROID_ERROR(("%s: GET_TX_POWER: dBm=%d\n", __FUNCTION__, dbm));

	return bytes_written;
}
#endif /* TEST_TX_POWER_CONTROL */

static int
wl_android_set_sarlimit_txctrl(struct net_device *dev, const char* string_num)
{
	int err = 0;
	int setval = 0;
	s32 mode = bcm_atoi(string_num);

	/* '0' means activate sarlimit
	 * and '-1' means back to normal state (deactivate sarlimit)
	 */
	if (mode == 0) {
		ANDROID_INFO(("%s: SAR limit control activated\n", __FUNCTION__));
		setval = 1;
	} else if (mode == -1) {
		ANDROID_INFO(("%s: SAR limit control deactivated\n", __FUNCTION__));
		setval = 0;
	} else {
		return -EINVAL;
	}

	err = wldev_iovar_setint(dev, "sar_enable", setval);
	if (unlikely(err)) {
		ANDROID_ERROR(("%s: error (%d)\n", __FUNCTION__, err));
		return err;
	}
	return 1;
}
#endif /* CUSTOMER_HW4_PRIVATE_CMD */

int wl_android_set_roam_mode(struct net_device *dev, char *command, int total_len)
{
	int error = 0;
	int mode = 0;

	if (sscanf(command, "%*s %d", &mode) != 1) {
		ANDROID_ERROR(("%s: Failed to get Parameter\n", __FUNCTION__));
		return -1;
	}

	error = wldev_iovar_setint(dev, "roam_off", mode);
	if (error) {
		ANDROID_ERROR(("%s: Failed to set roaming Mode %d, error = %d\n",
		__FUNCTION__, mode, error));
		return -1;
	}
	else
		ANDROID_ERROR(("%s: succeeded to set roaming Mode %d, error = %d\n",
		__FUNCTION__, mode, error));
	return 0;
}

#ifdef WL_CFG80211
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
	u16 kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;
	s32 err = BCME_OK, bssidx;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	/* Check the VSIE (Vendor Specific IE) which was added.
	 *  If exist then send IOVAR to delete it
	 */
	if (wl_cfg80211_ibss_vsie_delete(dev) != BCME_OK) {
		return -EINVAL;
	}

	if (total_len < (strlen(CMD_SETIBSSBEACONOUIDATA) + 1)) {
		ANDROID_ERROR(("error. total_len:%d\n", total_len));
		return -EINVAL;
	}

	pcmd = command + strlen(CMD_SETIBSSBEACONOUIDATA) + 1;
	for (idx = 0; idx < DOT11_OUI_LEN; idx++) {
		if (*pcmd == '\0') {
			ANDROID_ERROR(("error while parsing OUI.\n"));
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
		ANDROID_ERROR(("error. vndr ie len:%d\n", datalen));
		return -EINVAL;
	}

	tot_len = (int)(sizeof(vndr_ie_setbuf_t) + (datalen - 1));
	vndr_ie = (vndr_ie_setbuf_t *) kzalloc(tot_len, kflags);
	if (!vndr_ie) {
		ANDROID_ERROR(("IE memory alloc failed\n"));
		return -ENOMEM;
	}
	/* Copy the vndr_ie SET command ("add"/"del") to the buffer */
	strncpy(vndr_ie->cmd, "add", VNDR_IE_CMD_LEN - 1);
	vndr_ie->cmd[VNDR_IE_CMD_LEN - 1] = '\0';

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

	ioctl_buf = kmalloc(WLC_IOCTL_MEDLEN, GFP_KERNEL);
	if (!ioctl_buf) {
		ANDROID_ERROR(("ioctl memory alloc failed\n"));
		if (vndr_ie) {
			kfree(vndr_ie);
		}
		return -ENOMEM;
	}
	memset(ioctl_buf, 0, WLC_IOCTL_MEDLEN);	/* init the buffer */
	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		ANDROID_ERROR(("Find index failed\n"));
		err = BCME_ERROR;
		goto end;
	}
	err = wldev_iovar_setbuf_bsscfg(dev, "vndr_ie", vndr_ie, tot_len, ioctl_buf,
		WLC_IOCTL_MEDLEN, bssidx, &cfg->ioctl_buf_sync);

end:
	if (err != BCME_OK) {
		err = -EINVAL;
		if (vndr_ie) {
			kfree(vndr_ie);
		}
	}
	else {
		/* do NOT free 'vndr_ie' for the next process */
		wl_cfg80211_ibss_vsie_set_buffer(dev, vndr_ie, tot_len);
	}

	if (ioctl_buf) {
		kfree(ioctl_buf);
	}

	return err;
}
#endif

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
		ANDROID_ERROR(("too many AKM suites = %d\n", num_akm_suites));
		return -1;
	}

	/* Increment for number of AKM suites field + space */
	pcmd += 3;
	total_len_left -= 3;

	/* check to make sure pcmd does not overrun */
	if (total_len_left < (num_akm_suites * WIDTH_AKM_SUITE))
		return -1;

	memset(buf, 0, sizeof(buf));
	memset(akm_suites, 0, sizeof(akm_suites));
	memset(ucipher_suites, 0, sizeof(ucipher_suites));

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
			ANDROID_ERROR(("%s: Too many wpa configs for join_pref \n", __FUNCTION__));
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
			memset(pref, 0, WPA_SUITE_LEN);
			pref += WPA_SUITE_LEN;
		}
	}

	prhex("join pref", (uint8 *)buf, total_bytes);
	error = wldev_iovar_setbuf(dev, "join_pref", buf, total_bytes, smbuf, sizeof(smbuf), NULL);
	if (error) {
		ANDROID_ERROR(("Failed to set join_pref, error = %d\n", error));
	}
	return error;
}
#endif /* defined(BCMFW_ROAM_ENABLE */

#ifdef WL_CFG80211
static int
wl_android_iolist_add(struct net_device *dev, struct list_head *head, struct io_cfg *config)
{
	struct io_cfg *resume_cfg;
	s32 ret;

	resume_cfg = kzalloc(sizeof(struct io_cfg), GFP_KERNEL);
	if (!resume_cfg)
		return -ENOMEM;

	if (config->iovar) {
		ret = wldev_iovar_getint(dev, config->iovar, &resume_cfg->param);
		if (ret) {
			ANDROID_ERROR(("%s: Failed to get current %s value\n",
				__FUNCTION__, config->iovar));
			goto error;
		}

		ret = wldev_iovar_setint(dev, config->iovar, config->param);
		if (ret) {
			ANDROID_ERROR(("%s: Failed to set %s to %d\n", __FUNCTION__,
				config->iovar, config->param));
			goto error;
		}

		resume_cfg->iovar = config->iovar;
	} else {
		resume_cfg->arg = kzalloc(config->len, GFP_KERNEL);
		if (!resume_cfg->arg) {
			ret = -ENOMEM;
			goto error;
		}
		ret = wldev_ioctl_get(dev, config->ioctl, resume_cfg->arg, config->len);
		if (ret) {
			ANDROID_ERROR(("%s: Failed to get ioctl %d\n", __FUNCTION__,
				config->ioctl));
			goto error;
		}
		ret = wldev_ioctl_set(dev, config->ioctl + 1, config->arg, config->len);
		if (ret) {
			ANDROID_ERROR(("%s: Failed to set %s to %d\n", __FUNCTION__,
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
	kfree(resume_cfg->arg);
	kfree(resume_cfg);
	return ret;
}

static void
wl_android_iolist_resume(struct net_device *dev, struct list_head *head)
{
	struct io_cfg *config;
	struct list_head *cur, *q;
	s32 ret = 0;

#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	list_for_each_safe(cur, q, head) {
		config = list_entry(cur, struct io_cfg, list);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
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
			kfree(config->arg);
		}
		list_del(cur);
		kfree(config);
	}
}
#ifdef WL11ULB
static int
wl_android_set_ulb_mode(struct net_device *dev, char *command, int total_len)
{
	int mode = 0;

	ANDROID_INFO(("set ulb mode (%s) \n", command));
	if (sscanf(command, "%*s %d", &mode) != 1) {
		ANDROID_ERROR(("%s: Failed to get Parameter\n", __FUNCTION__));
		return -1;
	}
	return wl_cfg80211_set_ulb_mode(dev, mode);
}
static int
wl_android_set_ulb_bw(struct net_device *dev, char *command, int total_len)
{
	int bw = 0;
	u8 *pos;
	char *ifname = NULL;
	ANDROID_INFO(("set ulb bw (%s) \n", command));

	/*
	 * For sta/ap: IFNAME=<ifname> DRIVER ULB_BW <bw> ifname
	 * For p2p:    IFNAME=wlan0 DRIVER ULB_BW <bw> p2p-dev-wlan0
	 */
	if (total_len < strlen(CMD_ULB_BW) + 2)
		return -EINVAL;

	pos = command + strlen(CMD_ULB_BW) + 1;
	bw = bcm_atoi(pos);

	if ((strlen(pos) >= 5)) {
		ifname = pos + 2;
	}

	ANDROID_INFO(("[ULB] ifname:%s ulb_bw:%d \n", ifname, bw));
	return wl_cfg80211_set_ulb_bw(dev, bw, ifname);
}
#endif /* WL11ULB */
static int
wl_android_set_miracast(struct net_device *dev, char *command, int total_len)
{
	int mode, val;
	int ret = 0;
	struct io_cfg config;

	if (sscanf(command, "%*s %d", &mode) != 1) {
		ANDROID_ERROR(("%s: Failed to get Parameter\n", __FUNCTION__));
		return -1;
	}

	ANDROID_INFO(("%s: enter miracast mode %d\n", __FUNCTION__, mode));

	if (miracast_cur_mode == mode) {
		return 0;
	}

	wl_android_iolist_resume(dev, &miracast_resume_list);
	miracast_cur_mode = MIRACAST_MODE_OFF;

	switch (mode) {
	case MIRACAST_MODE_SOURCE:
		/* setting mchan_algo to platform specific value */
		config.iovar = "mchan_algo";

		ret = wldev_ioctl_get(dev, WLC_GET_BCNPRD, &val, sizeof(int));
		if (!ret && val > 100) {
			config.param = 0;
			ANDROID_ERROR(("%s: Connected station's beacon interval: "
				"%d and set mchan_algo to %d \n",
				__FUNCTION__, val, config.param));
		} else {
			config.param = MIRACAST_MCHAN_ALGO;
		}
		ret = wl_android_iolist_add(dev, &miracast_resume_list, &config);
		if (ret) {
			goto resume;
		}

		/* setting mchan_bw to platform specific value */
		config.iovar = "mchan_bw";
		config.param = MIRACAST_MCHAN_BW;
		ret = wl_android_iolist_add(dev, &miracast_resume_list, &config);
		if (ret) {
			goto resume;
		}

		/* setting apmdu to platform specific value */
		config.iovar = "ampdu_mpdu";
		config.param = MIRACAST_AMPDU_SIZE;
		ret = wl_android_iolist_add(dev, &miracast_resume_list, &config);
		if (ret) {
			goto resume;
		}
		/* FALLTROUGH */
		/* Source mode shares most configurations with sink mode.
		 * Fall through here to avoid code duplication
		 */
	case MIRACAST_MODE_SINK:
		/* disable internal roaming */
		config.iovar = "roam_off";
		config.param = 1;
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
	ANDROID_ERROR(("%s: turnoff miracast mode because of err%d\n", __FUNCTION__, ret));
	wl_android_iolist_resume(dev, &miracast_resume_list);
	return ret;
}
#endif

#define NETLINK_OXYGEN     30
#define AIBSS_BEACON_TIMEOUT	10

static struct sock *nl_sk = NULL;

static void wl_netlink_recv(struct sk_buff *skb)
{
	ANDROID_ERROR(("netlink_recv called\n"));
}

static int wl_netlink_init(void)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
	struct netlink_kernel_cfg cfg = {
		.input	= wl_netlink_recv,
	};
#endif

	if (nl_sk != NULL) {
		ANDROID_ERROR(("nl_sk already exist\n"));
		return BCME_ERROR;
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0))
	nl_sk = netlink_kernel_create(&init_net, NETLINK_OXYGEN,
		0, wl_netlink_recv, NULL, THIS_MODULE);
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(3, 7, 0))
	nl_sk = netlink_kernel_create(&init_net, NETLINK_OXYGEN, THIS_MODULE, &cfg);
#else
	nl_sk = netlink_kernel_create(&init_net, NETLINK_OXYGEN, &cfg);
#endif

	if (nl_sk == NULL) {
		ANDROID_ERROR(("nl_sk is not ready\n"));
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
		ANDROID_ERROR(("nl_sk was not initialized\n"));
		goto nlmsg_failure;
	}

	skb = alloc_skb(NLMSG_SPACE(size), GFP_ATOMIC);
	if (skb == NULL) {
		ANDROID_ERROR(("failed to allocate memory\n"));
		goto nlmsg_failure;
	}

	nlh = nlmsg_put(skb, 0, 0, 0, size, 0);
	if (nlh == NULL) {
		ANDROID_ERROR(("failed to build nlmsg, skb_tailroom:%d, nlmsg_total_size:%d\n",
			skb_tailroom(skb), nlmsg_total_size(size)));
		dev_kfree_skb(skb);
		goto nlmsg_failure;
	}

	memcpy(nlmsg_data(nlh), data, size);
	nlh->nlmsg_seq = seq;
	nlh->nlmsg_type = type;

	/* netlink_unicast() takes ownership of the skb and frees it itself. */
	ret = netlink_unicast(nl_sk, skb, pid, 0);
	ANDROID_TRACE(("netlink_unicast() pid=%d, ret=%d\n", pid, ret));

nlmsg_failure:
	return ret;
}


int wl_keep_alive_set(struct net_device *dev, char* extra, int total_len)
{
	wl_mkeep_alive_pkt_t	mkeep_alive_pkt;
	int ret;
	uint period_msec = 0;
	char *buf;

	if (extra == NULL) {
		 ANDROID_ERROR(("%s: extra is NULL\n", __FUNCTION__));
		 return -1;
	}
	if (sscanf(extra, "%d", &period_msec) != 1) {
		 ANDROID_ERROR(("%s: sscanf error. check period_msec value\n", __FUNCTION__));
		 return -EINVAL;
	}
	ANDROID_ERROR(("%s: period_msec is %d\n", __FUNCTION__, period_msec));

	memset(&mkeep_alive_pkt, 0, sizeof(wl_mkeep_alive_pkt_t));

	mkeep_alive_pkt.period_msec = period_msec;
	mkeep_alive_pkt.version = htod16(WL_MKEEP_ALIVE_VERSION);
	mkeep_alive_pkt.length = htod16(WL_MKEEP_ALIVE_FIXED_LEN);

	/* Setup keep alive zero for null packet generation */
	mkeep_alive_pkt.keep_alive_id = 0;
	mkeep_alive_pkt.len_bytes = 0;

	buf = kmalloc(WLC_IOCTL_SMLEN, GFP_KERNEL);
	if (!buf) {
		ANDROID_ERROR(("%s: buffer alloc failed\n", __FUNCTION__));
		return BCME_NOMEM;
	}
	ret = wldev_iovar_setbuf(dev, "mkeep_alive", (char *)&mkeep_alive_pkt,
			WL_MKEEP_ALIVE_FIXED_LEN, buf, WLC_IOCTL_SMLEN, NULL);
	if (ret < 0)
		ANDROID_ERROR(("%s:keep_alive set failed:%d\n", __FUNCTION__, ret));
	else
		ANDROID_TRACE(("%s:keep_alive set ok\n", __FUNCTION__));
	kfree(buf);
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
		ANDROID_ERROR(("%s: Failed to get the mode for only_resp_wfdsrc, error = %d\n",
			__FUNCTION__, error));
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
		ANDROID_ERROR(("%s: Failed to set only_resp_wfdsrc %d, error = %d\n",
			__FUNCTION__, only_resp_wfdsrc, error));
		return -1;
	}

	return 0;
}
#endif /* P2PRESP_WFDIE_SRC */

#ifdef BT_WIFI_HANDOVER
static int
wl_tbow_teardown(struct net_device *dev, char *command, int total_len)
{
	int err = BCME_OK;
	char buf[WLC_IOCTL_SMLEN];
	tbow_setup_netinfo_t netinfo;
	memset(&netinfo, 0, sizeof(netinfo));
	netinfo.opmode = TBOW_HO_MODE_TEARDOWN;

	err = wldev_iovar_setbuf_bsscfg(dev, "tbow_doho", &netinfo,
			sizeof(tbow_setup_netinfo_t), buf, WLC_IOCTL_SMLEN, 0, NULL);
	if (err < 0) {
		ANDROID_ERROR(("tbow_doho iovar error %d\n", err));
			return err;
	}
	return err;
}
#endif /* BT_WIFI_HANOVER */

#ifdef SET_RPS_CPUS
static int
wl_android_set_rps_cpus(struct net_device *dev, char *command, int total_len)
{
	int error, enable;

	enable = command[strlen(CMD_RPSMODE) + 1] - '0';
	error = dhd_rps_cpus_enable(dev, enable);

#if defined(DHDTCPACK_SUPPRESS) && defined(BCMPCIE) && defined(WL_CFG80211)
	if (!error) {
		void *dhdp = wl_cfg80211_get_dhdp(net);
		if (enable) {
			ANDROID_TRACE(("%s : set ack suppress. TCPACK_SUP_HOLD.\n", __FUNCTION__));
			dhd_tcpack_suppress_set(dhdp, TCPACK_SUP_HOLD);
		} else {
			ANDROID_TRACE(("%s : clear ack suppress.\n", __FUNCTION__));
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
	uint encode, rate, txexp;
	struct wl_bss_info *bi;
	int datalen = sizeof(uint32) + sizeof(wl_bss_info_t);
	char buf[datalen];

	/* get BSS information */
	*(u32 *) buf = htod32(datalen);
	error = wldev_ioctl_get(dev, WLC_GET_BSS_INFO, (void *)buf, datalen);
	if (unlikely(error)) {
		ANDROID_ERROR(("Could not get bss info %d\n", error));
		return -1;
	}

	bi = (struct wl_bss_info *) (buf + sizeof(uint32));

	for (i = 0; i < ETHER_ADDR_LEN; i++) {
		if (bi->BSSID.octet[i] > 0) {
			break;
		}
	}

	if (i == ETHER_ADDR_LEN) {
		ANDROID_TRACE(("No BSSID\n"));
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
		ANDROID_ERROR(("get link status error (%d)\n", error));
		return -1;
	}

	encode = (rspec & WL_RSPEC_ENCODING_MASK);
	rate = (rspec & WL_RSPEC_RATE_MASK);
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
		((encode == WL_RSPEC_ENCODE_HT) && rate < 8) ||
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

	ANDROID_TRACE(("%s:result=%d, stf=%d, single_stream=%d, mcs map=%d\n",
		__FUNCTION__, result, stf, single_stream, nss));

	bytes_written = sprintf(command, "%s %d", CMD_GET_LINK_STATUS, result);

	return bytes_written;
}

#ifdef P2P_LISTEN_OFFLOADING
s32
wl_cfg80211_p2plo_offload(struct net_device *dev, char *cmd, char* buf, int len)
{
	int ret = 0;

	ANDROID_ERROR(("Entry cmd:%s arg_len:%d \n", cmd, len));

	if (strncmp(cmd, "P2P_LO_START", strlen("P2P_LO_START")) == 0) {
		ret = wl_cfg80211_p2plo_listen_start(dev, buf, len);
	} else if (strncmp(cmd, "P2P_LO_STOP", strlen("P2P_LO_STOP")) == 0) {
		ret = wl_cfg80211_p2plo_listen_stop(dev);
	} else {
		ANDROID_ERROR(("Request for Unsupported CMD:%s \n", buf));
		ret = -EINVAL;
	}
	return ret;
}
#endif /* P2P_LISTEN_OFFLOADING */

#if defined(BCM4359_CHIP) && defined(WL_CFG80211)
int
wl_android_murx_bfe_cap(struct net_device *dev, int val)
{
	int err = BCME_OK;
	int iface_count = wl_cfg80211_iface_count(dev);
	struct ether_addr bssid;
	wl_reassoc_params_t params;

	if (iface_count > 1) {
		ANDROID_ERROR(("murx_bfe_cap change is not allowed when "
				"there are multiple interfaces\n"));
		return -EINVAL;
	}
	/* Now there is only single interface */
	err = wldev_iovar_setint(dev, "murx_bfe_cap", val);
	if (unlikely(err)) {
		ANDROID_ERROR(("Failed to set murx_bfe_cap IOVAR to %d,"
				"error %d\n", val, err));
		return err;
	}

	/* If successful intiate a reassoc */
	memset(&bssid, 0, ETHER_ADDR_LEN);
	if ((err = wldev_ioctl_get(dev, WLC_GET_BSSID, &bssid, ETHER_ADDR_LEN)) < 0) {
		ANDROID_ERROR(("Failed to get bssid, error=%d\n", err));
		return err;
	}

	bzero(&params, sizeof(wl_reassoc_params_t));
	memcpy(&params.bssid, &bssid, ETHER_ADDR_LEN);

	if ((err = wldev_ioctl_set(dev, WLC_REASSOC, &params,
		sizeof(wl_reassoc_params_t))) < 0) {
		ANDROID_ERROR(("reassoc failed err:%d \n", err));
	} else {
		ANDROID_TRACE(("reassoc issued successfully\n"));
	}

	return err;
}
#endif /* BCM4359_CHIP */

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

	ANDROID_TRACE(("rate %d, ifacename %s\n", rate, ifname));

	err = wl_set_ap_beacon_rate(dev, rate, ifname);
	if (unlikely(err)) {
		ANDROID_ERROR(("Failed to set ap beacon rate to %d, error = %d\n", rate, err));
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

	ANDROID_TRACE(("ifacename %s\n", ifname));

	bytes_written = wl_get_ap_basic_rate(dev, command, ifname, total_len);
	if (bytes_written < 1) {
		ANDROID_ERROR(("Failed to get ap basic rate, error = %d\n", bytes_written));
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

	ANDROID_TRACE(("ifacename %s\n", ifname));

	bytes_written = wl_get_ap_rps(dev, command, ifname, total_len);
	if (bytes_written < 1) {
		ANDROID_ERROR(("Failed to get rps, error = %d\n", bytes_written));
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

	ANDROID_TRACE(("enable %d, ifacename %s\n", enable, ifname));

	err = wl_set_ap_rps(dev, enable? TRUE: FALSE, ifname);
	if (unlikely(err)) {
		ANDROID_ERROR(("Failed to set rps, enable %d, error = %d\n", enable, err));
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

	memset(&rps, 0, sizeof(rps));
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

	ANDROID_TRACE(("pps %d, level %d, quiettime %d, sta_assoc_check %d, "
		"ifacename %s\n", rps.pps, rps.level, rps.quiet_time,
		rps.sta_assoc_check, ifname));

	err = wl_update_ap_rps_params(dev, &rps, ifname);
	if (unlikely(err)) {
		ANDROID_ERROR(("Failed to update rps, pps %d, level %d, quiettime %d, "
			"sta_assoc_check %d, err = %d\n", rps.pps, rps.level, rps.quiet_time,
			rps.sta_assoc_check, err));
	}

	return err;
}
#endif /* SUPPORT_AP_RADIO_PWRSAVE */

#ifdef SUPPORT_RSSI_LOGGING
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

	memset(&rssi_ant_mimo, 0, sizeof(wl_rssi_ant_mimo_t));
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
		ANDROID_ERROR(("Invalid arguments\n"));
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
		ANDROID_ERROR(("Failed to get RSSI info, err=%d\n", err));
		return err;
	}

	/* Parse the results */
	ANDROID_TRACE(("ifname %s, version %d, count %d, mimo rssi %d\n",
		ifname, rssi_ant_mimo.version, rssi_ant_mimo.count, mimo_rssi));
	if (mimo_rssi) {
		ANDROID_TRACE(("MIMO RSSI: %d\n", rssi_ant_mimo.rssi_sum));
		bytes_written = snprintf(command, total_len, "%s MIMO %d",
			CMD_GET_RSSI_PER_ANT, rssi_ant_mimo.rssi_sum);
	} else {
		int cnt;
		bytes_written = snprintf(command, total_len, "%s PER_ANT ", CMD_GET_RSSI_PER_ANT);
		for (cnt = 0; cnt < rssi_ant_mimo.count; cnt++) {
			ANDROID_TRACE(("RSSI[%d]: %d\n", cnt, rssi_ant_mimo.rssi_ant[cnt]));
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

	memset(&set_param, 0, sizeof(rssilog_set_param_t));
	/*
	 * DRIVER SET_RSSI_LOGGING <enable/disable> <RSSI Threshold> <Time Threshold>
	 */
	pos = command;

	/* drop command */
	token = bcmstrtok(&pos, " ", NULL);

	/* enable/disable */
	token = bcmstrtok(&pos, " ", NULL);
	if (!token) {
		ANDROID_ERROR(("Invalid arguments\n"));
		return -EINVAL;
	}
	set_param.enable = bcm_atoi(token);

	/* RSSI Threshold */
	token = bcmstrtok(&pos, " ", NULL);
	if (!token) {
		ANDROID_ERROR(("Invalid arguments\n"));
		return -EINVAL;
	}
	set_param.rssi_threshold = bcm_atoi(token);

	/* Time Threshold */
	token = bcmstrtok(&pos, " ", NULL);
	if (!token) {
		ANDROID_ERROR(("Invalid arguments\n"));
		return -EINVAL;
	}
	set_param.time_threshold = bcm_atoi(token);

	ANDROID_TRACE(("enable %d, RSSI threshold %d, Time threshold %d\n", set_param.enable,
		set_param.rssi_threshold, set_param.time_threshold));

	err = wl_set_rssi_logging(dev, (void *)&set_param);
	if (unlikely(err)) {
		ANDROID_ERROR(("Failed to configure RSSI logging: enable %d, RSSI Threshold %d,"
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
		ANDROID_ERROR(("Failed to get RSSI logging info\n"));
		return BCME_ERROR;
	}

	ANDROID_TRACE(("report_count %d, enable %d, rssi_threshold %d, time_threshold %d\n",
		get_param.report_count, get_param.enable, get_param.rssi_threshold,
		get_param.time_threshold));

	/* Parse the parameter */
	if (!get_param.enable) {
		ANDROID_TRACE(("RSSI LOGGING: Feature is disables\n"));
		bytes_written = snprintf(command, total_len,
			"%s FEATURE DISABLED\n", CMD_GET_RSSI_LOGGING);
	} else if (get_param.enable &
		(RSSILOG_FLAG_FEATURE_SW | RSSILOG_FLAG_REPORT_READY)) {
		if (!get_param.report_count) {
			ANDROID_TRACE(("[PASS] RSSI difference across antennas is within"
				" threshold limits\n"));
			bytes_written = snprintf(command, total_len, "%s PASS\n",
				CMD_GET_RSSI_LOGGING);
		} else {
			ANDROID_TRACE(("[FAIL] RSSI difference across antennas found "
				"to be greater than %3d dB\n", get_param.rssi_threshold));
			ANDROID_TRACE(("[FAIL] RSSI difference check have failed for "
				"%d out of %d times\n", get_param.report_count,
				get_param.time_threshold));
			ANDROID_TRACE(("[FAIL] RSSI difference is being monitored once "
				"per second, for a %d secs window\n", get_param.time_threshold));
			bytes_written = snprintf(command, total_len, "%s FAIL - RSSI Threshold "
				"%d dBm for %d out of %d times\n", CMD_GET_RSSI_LOGGING,
				get_param.rssi_threshold, get_param.report_count,
				get_param.time_threshold);
		}
	} else {
		ANDROID_TRACE(("[BUSY] Reprot is not ready\n"));
		bytes_written = snprintf(command, total_len, "%s BUSY - NOT READY\n",
			CMD_GET_RSSI_LOGGING);
	}

	return bytes_written;
}
#endif /* SUPPORT_RSSI_LOGGING */

#ifdef SET_PCIE_IRQ_CPU_CORE
void
wl_android_set_irq_cpucore(struct net_device *net, int set)
{
	dhd_pub_t *dhdp = wl_cfg80211_get_dhdp(net);
	if (!dhdp) {
		ANDROID_ERROR(("dhd is NULL\n"));
		return;
	}
	dhd_set_irq_cpucore(dhdp, set);
}
#endif /* SET_PCIE_IRQ_CPU_CORE */

#if defined(DHD_HANG_SEND_UP_TEST)
void
wl_android_make_hang_with_reason(struct net_device *dev, const char *string_num)
{
	dhd_make_hang_with_reason(dev, string_num);
}
#endif /* DHD_HANG_SEND_UP_TEST */

#ifdef WL_CFG80211
#ifdef WLMESH
static int
wl_android_set_rsdb_mode(struct net_device *dev, char *command, int total_len)
{
	int ret;
	wl_config_t rsdb_mode_cfg = {-1, 0};
	char smbuf[WLC_IOCTL_SMLEN];
	s32 val = 1;

	if (sscanf(command, "%*s %d", &rsdb_mode_cfg.config) != 1) {
		DHD_ERROR(("%s: Failed to get Parameter\n", __FUNCTION__));
		return -1;
	}
	DHD_INFO(("%s : RSDB_MODE = %d\n", __FUNCTION__, rsdb_mode_cfg.config));

	ret = wldev_ioctl_set(dev, WLC_DOWN, &val, sizeof(s32));
	if (ret < 0)
		DHD_ERROR(("WLC_DOWN error %d\n", ret));

	ret = wldev_iovar_setbuf(dev, "rsdb_mode", &rsdb_mode_cfg, sizeof(rsdb_mode_cfg),
		smbuf, sizeof(smbuf), NULL);
	if (ret < 0)
		DHD_ERROR(("%s : set rsdb_mode error=%d\n", __FUNCTION__, ret));

	ret = wldev_ioctl_set(dev, WLC_UP, &val, sizeof(s32));
	if (ret < 0)
		DHD_ERROR(("WLC_UP error %d\n", ret));

	return ret;
}
#endif /* WLMESH */
#endif /* WL_CFG80211 */

#ifdef SUPPORT_LQCM
static int
wl_android_lqcm_enable(struct net_device *net, int lqcm_enable)
{
	int err = 0;

	err = wldev_iovar_setint(net, "lqcm", lqcm_enable);
	if (err != BCME_OK) {
		ANDROID_ERROR(("failed to set lqcm enable %d, error = %d\n", lqcm_enable, err));
		return -EIO;
	}
	return err;
}

static int wl_android_get_lqcm_report(
	struct net_device *dev, char *command, int total_len)
{
	int bytes_written, err = 0;
	uint32 lqcm_report = 0;
	uint32 lqcm_enable, tx_lqcm_idx, rx_lqcm_idx;

	err = wldev_iovar_getint(dev, "lqcm", &lqcm_report);
	if (err != BCME_OK) {
		ANDROID_ERROR(("failed to get lqcm report, error = %d\n", err));
		return -EIO;
	}
	lqcm_enable = lqcm_report & LQCM_ENAB_MASK;
	tx_lqcm_idx = (lqcm_report & LQCM_TX_INDEX_MASK) >> LQCM_TX_INDEX_SHIFT;
	rx_lqcm_idx = (lqcm_report & LQCM_RX_INDEX_MASK) >> LQCM_RX_INDEX_SHIFT;

	ANDROID_ERROR(("lqcm report EN:%d, TX:%d, RX:%d\n", lqcm_enable, tx_lqcm_idx, rx_lqcm_idx));

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
		ANDROID_ERROR(("%s: Failed to get SNR %d, error = %d\n",
			__FUNCTION__, snr, error));
		return -EIO;
	}

	bytes_written = snprintf(command, total_len, "snr %d", snr);
	ANDROID_INFO(("%s: command result is %s\n", __FUNCTION__, command));
	return bytes_written;
}
#ifdef WLADPS_PRIVATE_CMD
static int
wl_android_set_adps_mode(struct net_device *dev, const char* string_num)
{
	int err = 0, adps_mode;
	dhd_pub_t *dhdp = wl_cfg80211_get_dhdp(dev);

	adps_mode = bcm_atoi(string_num);

	if ((adps_mode < 0) && (1 < adps_mode)) {
		ANDROID_ERROR(("%s: Invalid value %d.\n", __FUNCTION__, adps_mode));
		return -EINVAL;
	}

	err = dhd_enable_adps(dhdp, adps_mode);
	if (err != BCME_OK) {
		ANDROID_ERROR(("failed to set adps mode %d, error = %d\n", adps_mode, err));
		return -EIO;
	}
	return err;
}
static int
wl_android_get_adps_mode(
	struct net_device *dev, char *command, int total_len)
{
	int bytes_written, err = 0;
	int len;
	char buf[WLC_IOCTL_SMLEN];

	bcm_iov_buf_t iov_buf;
	bcm_iov_buf_t *ptr = NULL;
	wl_adps_params_v1_t *data = NULL;

	uint8 *pdata = NULL;
	uint8 band, mode = 0;

	memset(&iov_buf, 0, sizeof(iov_buf));

	len = OFFSETOF(bcm_iov_buf_t, data) + sizeof(*data);

	iov_buf.version = WL_ADPS_IOV_VER;
	iov_buf.len = sizeof(band);
	iov_buf.id = WL_ADPS_IOV_MODE;

	pdata = (uint8 *)&iov_buf.data;

	for (band = 1; band <= MAX_BANDS; band++) {
		pdata[0] = band;
		err = wldev_iovar_getbuf(dev, "adps", &iov_buf, len,
			buf, WLC_IOCTL_SMLEN, NULL);
		if (err != BCME_OK) {
			ANDROID_ERROR(("%s fail to get adps band %d(%d).\n",
					__FUNCTION__, band, err));
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
		ANDROID_ERROR(("%s: pktlog filter enable success\n", __FUNCTION__));
	} else {
		ANDROID_ERROR(("%s: pktlog filter enable fail\n", __FUNCTION__));
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
		ANDROID_ERROR(("%s: pktlog filter disable success\n", __FUNCTION__));
	} else {
		ANDROID_ERROR(("%s: pktlog filter disable fail\n", __FUNCTION__));
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
		ANDROID_ERROR(("%s: pktlog filter pattern enable success\n", __FUNCTION__));
	} else {
		ANDROID_ERROR(("%s: pktlog filter pattern enable fail\n", __FUNCTION__));
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
		ANDROID_ERROR(("%s: pktlog filter pattern disable success\n", __FUNCTION__));
	} else {
		ANDROID_ERROR(("%s: pktlog filter pattern disable fail\n", __FUNCTION__));
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
		ANDROID_ERROR(("%s: pktlog filter add success\n", __FUNCTION__));
	} else {
		ANDROID_ERROR(("%s: pktlog filter add fail\n", __FUNCTION__));
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
		ANDROID_ERROR(("%s: pktlog filter info success\n", __FUNCTION__));
	} else {
		ANDROID_ERROR(("%s: pktlog filter info fail\n", __FUNCTION__));
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

	if (!dhdp->pktlog->tx_pktlog_ring || !dhdp->pktlog->rx_pktlog_ring) {
		DHD_PKT_LOG(("%s(): tx_pktlog_ring=%p rx_pktlog_ring=%p\n",
			__FUNCTION__, dhdp->pktlog->tx_pktlog_ring, dhdp->pktlog->rx_pktlog_ring));
		return -EINVAL;
	}

	dhdp->pktlog->tx_pktlog_ring->start = TRUE;
	dhdp->pktlog->rx_pktlog_ring->start = TRUE;

	bytes_written = snprintf(command, total_len, "OK");

	ANDROID_ERROR(("%s: pktlog start success\n", __FUNCTION__));

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

	if (!dhdp->pktlog->tx_pktlog_ring || !dhdp->pktlog->rx_pktlog_ring) {
		DHD_PKT_LOG(("%s(): tx_pktlog_ring=%p rx_pktlog_ring=%p\n",
			__FUNCTION__, dhdp->pktlog->tx_pktlog_ring, dhdp->pktlog->rx_pktlog_ring));
		return -EINVAL;
	}

	dhdp->pktlog->tx_pktlog_ring->start = FALSE;
	dhdp->pktlog->rx_pktlog_ring->start = FALSE;

	bytes_written = snprintf(command, total_len, "OK");

	ANDROID_ERROR(("%s: pktlog stop success\n", __FUNCTION__));

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
		ANDROID_ERROR(("%s: pktlog filter pattern id: %d is existed\n", __FUNCTION__, id));
	} else {
		bytes_written = snprintf(command, total_len, "FALSE");
		ANDROID_ERROR(("%s: pktlog filter pattern id: %d is not existed\n", __FUNCTION__, id));
	}

	return bytes_written;
}
#endif /* DHD_PKT_LOGGING */

int wl_android_priv_cmd(struct net_device *net, struct ifreq *ifr, int cmd)
{
#define PRIVATE_COMMAND_MAX_LEN	8192
#define PRIVATE_COMMAND_DEF_LEN	4096
	int ret = 0;
	char *command = NULL;
	int bytes_written = 0;
	android_wifi_priv_cmd priv_cmd;
	int buf_size = 0;

	net_os_wake_lock(net);

	if (!capable(CAP_NET_ADMIN)) {
		ret = -EPERM;
		goto exit;
	}

	if (!ifr->ifr_data) {
		ret = -EINVAL;
		goto exit;
	}

#ifdef CONFIG_COMPAT
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0))
	if (in_compat_syscall())
#else
	if (is_compat_task())
#endif
	{
		compat_android_wifi_priv_cmd compat_priv_cmd;
		if (copy_from_user(&compat_priv_cmd, ifr->ifr_data,
			sizeof(compat_android_wifi_priv_cmd))) {
			ret = -EFAULT;
			goto exit;

		}
		priv_cmd.buf = compat_ptr(compat_priv_cmd.buf);
		priv_cmd.used_len = compat_priv_cmd.used_len;
		priv_cmd.total_len = compat_priv_cmd.total_len;
	} else
#endif /* CONFIG_COMPAT */
	{
		if (copy_from_user(&priv_cmd, ifr->ifr_data, sizeof(android_wifi_priv_cmd))) {
			ret = -EFAULT;
			goto exit;
		}
	}
	if ((priv_cmd.total_len > PRIVATE_COMMAND_MAX_LEN) || (priv_cmd.total_len < 0)) {
		ANDROID_ERROR(("%s: buf length invalid:%d\n", __FUNCTION__,
			priv_cmd.total_len));
		ret = -EINVAL;
		goto exit;
	}

	buf_size = max(priv_cmd.total_len, PRIVATE_COMMAND_DEF_LEN);
	command = kmalloc((buf_size + 1), GFP_KERNEL);

	if (!command)
	{
		ANDROID_ERROR(("%s: failed to allocate memory\n", __FUNCTION__));
		ret = -ENOMEM;
		goto exit;
	}
	if (copy_from_user(command, priv_cmd.buf, priv_cmd.total_len)) {
		ret = -EFAULT;
		goto exit;
	}
	command[priv_cmd.total_len] = '\0';

	ANDROID_INFO(("%s: Android private cmd \"%s\" on %s\n", __FUNCTION__, command, ifr->ifr_name));

	bytes_written = wl_handle_private_cmd(net, command, priv_cmd.total_len);
	if (bytes_written >= 0) {
		if ((bytes_written == 0) && (priv_cmd.total_len > 0)) {
			command[0] = '\0';
		}
		if (bytes_written >= priv_cmd.total_len) {
			ANDROID_ERROR(("%s: err. bytes_written:%d >= buf_size:%d \n",
				__FUNCTION__, bytes_written, buf_size));
			ret = BCME_BUFTOOSHORT;
			goto exit;
		}
		bytes_written++;
		priv_cmd.used_len = bytes_written;
		if (copy_to_user(priv_cmd.buf, command, bytes_written)) {
			ANDROID_ERROR(("%s: failed to copy data to user buffer\n", __FUNCTION__));
			ret = -EFAULT;
		}
	}
	else {
		/* Propagate the error */
		ret = bytes_written;
	}

exit:
	net_os_wake_unlock(net);
	kfree(command);
	return ret;
}

int
wl_handle_private_cmd(struct net_device *net, char *command, u32 cmd_len)
{
	int bytes_written = 0;
	android_wifi_priv_cmd priv_cmd;

	bzero(&priv_cmd, sizeof(android_wifi_priv_cmd));
	priv_cmd.total_len = cmd_len;

	if (strnicmp(command, CMD_START, strlen(CMD_START)) == 0) {
		ANDROID_INFO(("%s, Received regular START command\n", __FUNCTION__));
#ifdef  BT_OVER_SDIO
		bytes_written = dhd_net_bus_get(net);
#else
		bytes_written = wl_android_wifi_on(net);
#endif /* BT_OVER_SDIO */
	}
	else if (strnicmp(command, CMD_SETFWPATH, strlen(CMD_SETFWPATH)) == 0) {
		bytes_written = wl_android_set_fwpath(net, command, priv_cmd.total_len);
	}

	if (!g_wifi_on) {
		ANDROID_ERROR(("%s: Ignore private cmd \"%s\" - iface is down\n",
			__FUNCTION__, command));
		return 0;
	}

	if (strnicmp(command, CMD_STOP, strlen(CMD_STOP)) == 0) {
#ifdef  BT_OVER_SDIO
		bytes_written = dhd_net_bus_put(net);
#else
		bytes_written = wl_android_wifi_off(net, FALSE);
#endif /* BT_OVER_SDIO */
	}
#ifdef WL_CFG80211
	else if (strnicmp(command, CMD_SCAN_ACTIVE, strlen(CMD_SCAN_ACTIVE)) == 0) {
		wl_cfg80211_set_passive_scan(net, command);
	}
	else if (strnicmp(command, CMD_SCAN_PASSIVE, strlen(CMD_SCAN_PASSIVE)) == 0) {
		wl_cfg80211_set_passive_scan(net, command);
	}
#endif
	else if (strnicmp(command, CMD_RSSI, strlen(CMD_RSSI)) == 0) {
		bytes_written = wl_android_get_rssi(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_LINKSPEED, strlen(CMD_LINKSPEED)) == 0) {
		bytes_written = wl_android_get_link_speed(net, command, priv_cmd.total_len);
	}
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
		bytes_written = wl_android_set_suspendopt(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_SETSUSPENDMODE, strlen(CMD_SETSUSPENDMODE)) == 0) {
		bytes_written = wl_android_set_suspendmode(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_MAXDTIM_IN_SUSPEND, strlen(CMD_MAXDTIM_IN_SUSPEND)) == 0) {
		bytes_written = wl_android_set_max_dtim(net, command, priv_cmd.total_len);
	}
#ifdef WL_CFG80211
	else if (strnicmp(command, CMD_SETBAND, strlen(CMD_SETBAND)) == 0) {
#ifdef DISABLE_SETBAND
		bytes_written = BCME_DISABLED;
#else	/* DISABLE_SETBAND */
		uint band = *(command + strlen(CMD_SETBAND) + 1) - '0';
		if (dhd_conf_get_band(dhd_get_pub(net)) >= WLC_BAND_AUTO) {
			printf("%s: Band is fixed in config.txt\n", __FUNCTION__);
		} else
			bytes_written = wl_cfg80211_set_if_band(net, band);
#endif /* DISABLE_SETBAND */
	}
#endif
	else if (strnicmp(command, CMD_GETBAND, strlen(CMD_GETBAND)) == 0) {
		bytes_written = wl_android_get_band(net, command, priv_cmd.total_len);
	}
#ifdef WL_CFG80211
	else if (strnicmp(command, CMD_SET_CSA, strlen(CMD_SET_CSA)) == 0) {
		bytes_written = wl_android_set_csa(net, command, priv_cmd.total_len);
	} else if (strnicmp(command, CMD_80211_MODE, strlen(CMD_80211_MODE)) == 0) {
		bytes_written = wl_android_get_80211_mode(net, command, priv_cmd.total_len);
	} else if (strnicmp(command, CMD_CHANSPEC, strlen(CMD_CHANSPEC)) == 0) {
		bytes_written = wl_android_get_chanspec(net, command, priv_cmd.total_len);
	}
#endif /* WL_CFG80211 */
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
		bytes_written = wldev_set_country(net, country_code, true, true, revinfo);
#ifdef CUSTOMER_HW4_PRIVATE_CMD
#ifdef FCC_PWR_LIMIT_2G
		if (wldev_iovar_setint(net, "fccpwrlimit2g", FALSE)) {
			ANDROID_ERROR(("%s: fccpwrlimit2g deactivation is failed\n", __FUNCTION__));
		} else {
			ANDROID_ERROR(("%s: fccpwrlimit2g is deactivated\n", __FUNCTION__));
		}
#endif /* FCC_PWR_LIMIT_2G */
#endif /* CUSTOMER_HW4_PRIVATE_CMD */
	}
	else if (strnicmp(command, CMD_DATARATE, strlen(CMD_DATARATE)) == 0) {
		bytes_written = wl_android_get_datarate(net, command, priv_cmd.total_len);
	} else if (strnicmp(command, CMD_ASSOC_CLIENTS,	strlen(CMD_ASSOC_CLIENTS)) == 0) {
		bytes_written = wl_android_get_assoclist(net, command, priv_cmd.total_len);
	}

#ifdef CUSTOMER_HW4_PRIVATE_CMD
#ifdef WLTDLS
	else if (strnicmp(command, CMD_TDLS_RESET, strlen(CMD_TDLS_RESET)) == 0) {
		bytes_written = wl_android_tdls_reset(net);
	}
#endif /* WLTDLS */
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
#ifdef WL_CFG80211
#ifdef WLMESH
	else if (strnicmp(command, CMD_SAE_SET_PASSWORD, strlen(CMD_SAE_SET_PASSWORD)) == 0) {
		int skip = strlen(CMD_SAE_SET_PASSWORD) + 1;
		bytes_written = wl_cfg80211_set_sae_password(net, command + skip,
			priv_cmd.total_len - skip);
	}
	else if (strnicmp(command, CMD_SET_RSDB_MODE, strlen(CMD_SET_RSDB_MODE)) == 0) {
		bytes_written = wl_android_set_rsdb_mode(net, command, priv_cmd.total_len);
	}
#endif
#endif /* WL_CFG80211 */
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
		bytes_written = wl_android_set_rmc_event(net, command, priv_cmd.total_len);
	}
#endif /* WL_RELMCAST */
	else if (strnicmp(command, CMD_GET_SCSCAN, strlen(CMD_GET_SCSCAN)) == 0) {
		bytes_written = wl_android_get_singlecore_scan(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_SET_SCSCAN, strlen(CMD_SET_SCSCAN)) == 0) {
		bytes_written = wl_android_set_singlecore_scan(net, command, priv_cmd.total_len);
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
		wl_android_set_sarlimit_txctrl(net, (const char*)command+skip);
	}
#endif /* CUSTOMER_HW4_PRIVATE_CMD */
	else if (strnicmp(command, CMD_HAPD_MAC_FILTER, strlen(CMD_HAPD_MAC_FILTER)) == 0) {
		int skip = strlen(CMD_HAPD_MAC_FILTER) + 1;
		wl_android_set_mac_address_filter(net, command+skip);
	}
	else if (strnicmp(command, CMD_SETROAMMODE, strlen(CMD_SETROAMMODE)) == 0)
		bytes_written = wl_android_set_roam_mode(net, command, priv_cmd.total_len);
#if defined(BCMFW_ROAM_ENABLE)
	else if (strnicmp(command, CMD_SET_ROAMPREF, strlen(CMD_SET_ROAMPREF)) == 0) {
		bytes_written = wl_android_set_roampref(net, command, priv_cmd.total_len);
	}
#endif /* BCMFW_ROAM_ENABLE */
#ifdef WL_CFG80211
	else if (strnicmp(command, CMD_MIRACAST, strlen(CMD_MIRACAST)) == 0)
		bytes_written = wl_android_set_miracast(net, command, priv_cmd.total_len);
#ifdef WL11ULB
	else if (strnicmp(command, CMD_ULB_MODE, strlen(CMD_ULB_MODE)) == 0)
		bytes_written = wl_android_set_ulb_mode(net, command, priv_cmd.total_len);
	else if (strnicmp(command, CMD_ULB_BW, strlen(CMD_ULB_BW)) == 0)
		bytes_written = wl_android_set_ulb_bw(net, command, priv_cmd.total_len);
#endif /* WL11ULB */
	else if (strnicmp(command, CMD_SETIBSSBEACONOUIDATA, strlen(CMD_SETIBSSBEACONOUIDATA)) == 0)
		bytes_written = wl_android_set_ibss_beacon_ouidata(net,
		command, priv_cmd.total_len);
#endif
	else if (strnicmp(command, CMD_KEEP_ALIVE, strlen(CMD_KEEP_ALIVE)) == 0) {
		int skip = strlen(CMD_KEEP_ALIVE) + 1;
		bytes_written = wl_keep_alive_set(net, command + skip, priv_cmd.total_len - skip);
	}
#ifdef WL_CFG80211
	else if (strnicmp(command, CMD_ROAM_OFFLOAD, strlen(CMD_ROAM_OFFLOAD)) == 0) {
		int enable = *(command + strlen(CMD_ROAM_OFFLOAD) + 1) - '0';
		bytes_written = wl_cfg80211_enable_roam_offload(net, enable);
	}
#endif
#if defined(WL_VIRTUAL_APSTA)
	else if (strnicmp(command, CMD_INTERFACE_CREATE, strlen(CMD_INTERFACE_CREATE)) == 0) {
		char *name = (command + strlen(CMD_INTERFACE_CREATE) +1);
		ANDROID_INFO(("Creating %s interface\n", name));
		bytes_written = wl_cfg80211_interface_create(net, name);
	}
	else if (strnicmp(command, CMD_INTERFACE_DELETE, strlen(CMD_INTERFACE_DELETE)) == 0) {
		char *name = (command + strlen(CMD_INTERFACE_DELETE) +1);
		ANDROID_INFO(("Deleteing %s interface\n", name));
		bytes_written = wl_cfg80211_interface_delete(net, name);
	}
#endif /* defined (WL_VIRTUAL_APSTA) */
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
#ifdef WL_CFG80211
	else if (strnicmp(command, CMD_DFS_AP_MOVE, strlen(CMD_DFS_AP_MOVE)) == 0) {
		char *data = (command + strlen(CMD_DFS_AP_MOVE) +1);
		bytes_written = wl_cfg80211_dfs_ap_move(net, data, command, priv_cmd.total_len);
	}
#endif
#ifdef WBTEXT
	else if (strnicmp(command, CMD_WBTEXT_ENABLE, strlen(CMD_WBTEXT_ENABLE)) == 0) {
		bytes_written = wl_android_wbtext(net, command, priv_cmd.total_len);
	}
#ifdef WL_CFG80211
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
#endif
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
#endif /* WBTEXT */
#ifdef SET_RPS_CPUS
	else if (strnicmp(command, CMD_RPSMODE, strlen(CMD_RPSMODE)) == 0) {
		bytes_written = wl_android_set_rps_cpus(net, command, priv_cmd.total_len);
	}
#endif /* SET_RPS_CPUS */
#ifdef WLWFDS
	else if (strnicmp(command, CMD_ADD_WFDS_HASH, strlen(CMD_ADD_WFDS_HASH)) == 0) {
		bytes_written = wl_android_set_wfds_hash(net, command, priv_cmd.total_len, 1);
	}
	else if (strnicmp(command, CMD_DEL_WFDS_HASH, strlen(CMD_DEL_WFDS_HASH)) == 0) {
		bytes_written = wl_android_set_wfds_hash(net, command, priv_cmd.total_len, 0);
	}
#endif /* WLWFDS */
#ifdef BT_WIFI_HANDOVER
	else if (strnicmp(command, CMD_TBOW_TEARDOWN, strlen(CMD_TBOW_TEARDOWN)) == 0) {
	    bytes_written = wl_tbow_teardown(net, command, priv_cmd.total_len);
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
		bytes_written = wl_android_set_fcc_pwr_limit_2g(net, command, priv_cmd.total_len);
	}
#endif /* FCC_PWR_LIMIT_2G */
	else if (strnicmp(command, CMD_GET_STA_INFO, strlen(CMD_GET_STA_INFO)) == 0) {
		bytes_written = wl_cfg80211_get_sta_info(net, command, priv_cmd.total_len);
	}
#endif /* CUSTOMER_HW4_PRIVATE_CMD */
	else if (strnicmp(command, CMD_MURX_BFE_CAP,
			strlen(CMD_MURX_BFE_CAP)) == 0) {
#if defined(BCM4359_CHIP) && defined(WL_CFG80211)
		uint val = *(command + strlen(CMD_MURX_BFE_CAP) + 1) - '0';
		bytes_written = wl_android_murx_bfe_cap(net, val);
#else
		return BCME_UNSUPPORTED;
#endif /* BCM4359_CHIP */
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
#ifdef SUPPORT_RSSI_LOGGING
	else if (strnicmp(command, CMD_SET_RSSI_LOGGING, strlen(CMD_SET_RSSI_LOGGING)) == 0) {
		bytes_written = wl_android_set_rssi_logging(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_GET_RSSI_LOGGING, strlen(CMD_GET_RSSI_LOGGING)) == 0) {
		bytes_written = wl_android_get_rssi_logging(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_GET_RSSI_PER_ANT, strlen(CMD_GET_RSSI_PER_ANT)) == 0) {
		bytes_written = wl_android_get_rssi_per_ant(net, command, priv_cmd.total_len);
	}
#endif /* SUPPORT_RSSI_LOGGING */
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
#endif
#ifdef DHD_LOG_DUMP
	else if (strnicmp(command, CMD_NEW_DEBUG_PRINT_DUMP,
		strlen(CMD_NEW_DEBUG_PRINT_DUMP)) == 0) {
		dhd_pub_t *dhdp = wl_cfg80211_get_dhdp(net);
		dhd_schedule_log_dump(dhdp);
#if defined(DHD_DEBUG) && defined(BCMPCIE) && defined(DHD_FW_COREDUMP)
		dhdp->memdump_type = DUMP_TYPE_BY_SYSDUMP;
		dhd_bus_mem_dump(dhdp);
#endif /* DHD_DEBUG && BCMPCIE && DHD_FW_COREDUMP */
#ifdef DHD_PKT_LOGGING
		dhd_schedule_pktlog_dump(dhdp);
#endif /* DHD_PKT_LOGGING */
	}
#endif /* DHD_LOG_DUMP */
#ifdef SET_PCIE_IRQ_CPU_CORE
	else if (strnicmp(command, CMD_PCIE_IRQ_CORE, strlen(CMD_PCIE_IRQ_CORE)) == 0) {
		int set = *(command + strlen(CMD_PCIE_IRQ_CORE) + 1) - '0';
		wl_android_set_irq_cpucore(net, set);
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
#endif
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
#endif /* DHD_PKT_LOGGING */
#if defined(STAT_REPORT)
	else if (strnicmp(command, CMD_STAT_REPORT_GET_START,
		strlen(CMD_STAT_REPORT_GET_START)) == 0) {
		bytes_written = wl_android_stat_report_get_start(net, command, priv_cmd.total_len);
	} else if (strnicmp(command, CMD_STAT_REPORT_GET_NEXT,
		strlen(CMD_STAT_REPORT_GET_NEXT)) == 0) {
		bytes_written = wl_android_stat_report_get_next(net, command, priv_cmd.total_len);
	}
#endif /* STAT_REPORT */
	else if (wl_android_ext_priv_cmd(net, command, priv_cmd.total_len, &bytes_written) == 0) {
	}
	else {
		ANDROID_ERROR(("Unknown PRIVATE command %s - ignored\n", command));
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
		memset(iface_name, 0, IFNAMSIZ);
		bcm_strncpy_s(iface_name, IFNAMSIZ, "wlan", IFNAMSIZ);
	}

	wl_netlink_init();

	return ret;
}

int wl_android_exit(void)
{
	int ret = 0;
	struct io_cfg *cur, *q;

	wl_netlink_deinit();

#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	list_for_each_entry_safe(cur, q, &miracast_resume_list, list) {
		list_del(&cur->list);
		kfree(cur);
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

	return ret;
}

void wl_android_post_init(void)
{

#ifdef ENABLE_4335BT_WAR
	bcm_bt_unlock(lock_cookie_wifi);
	printk("%s: btlock released\n", __FUNCTION__);
#endif /* ENABLE_4335BT_WAR */

	if (!dhd_download_fw_on_driverload)
		g_wifi_on = FALSE;
}



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
