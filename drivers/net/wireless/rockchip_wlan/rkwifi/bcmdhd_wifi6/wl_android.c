/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Linux cfg80211 driver - Android related functions
 *
 * Copyright (C) 1999-2019, Broadcom.
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
 * $Id: wl_android.c 825470 2019-06-14 09:08:11Z $
 */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <net/netlink.h>
#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif // endif

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
#include <dhd_config.h>
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
#include <dhd_linux.h>
#include <bcmiov.h>
#ifdef WL_BCNRECV
#include <wl_cfgvendor.h>
#include <brcm_nl80211.h>
#endif /* WL_BCNRECV */
#ifdef WL_MBO
#include <mbo.h>
#endif /* WL_MBO */
#ifdef RTT_SUPPORT
#include <dhd_rtt.h>
#endif /* RTT_SUPPORT */
#ifdef WL_ESCAN
#include <wl_escan.h>
#endif

#ifdef WL_STATIC_IF
#define WL_BSSIDX_MAX	16
#endif /* WL_STATIC_IF */

uint android_msg_level = ANDROID_ERROR_LEVEL | ANDROID_MSG_LEVEL;

#define ANDROID_ERROR_MSG(x, args...) \
	do { \
		if (android_msg_level & ANDROID_ERROR_LEVEL) { \
			printk(KERN_ERR "[dhd] ANDROID-ERROR) " x, ## args); \
		} \
	} while (0)
#define ANDROID_TRACE_MSG(x, args...) \
	do { \
		if (android_msg_level & ANDROID_TRACE_LEVEL) { \
			printk(KERN_INFO "[dhd] ANDROID-TRACE) " x, ## args); \
		} \
	} while (0)
#define ANDROID_INFO_MSG(x, args...) \
	do { \
		if (android_msg_level & ANDROID_INFO_LEVEL) { \
			printk(KERN_INFO "[dhd] ANDROID-INFO) " x, ## args); \
		} \
	} while (0)
#define ANDROID_ERROR(x) ANDROID_ERROR_MSG x
#define ANDROID_TRACE(x) ANDROID_TRACE_MSG x
#define ANDROID_INFO(x) ANDROID_INFO_MSG x

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
#define CMD_SETDTIM_IN_SUSPEND  "SET_DTIM_IN_SUSPEND"
#define CMD_MAXDTIM_IN_SUSPEND  "MAX_DTIM_IN_SUSPEND"
#define CMD_DISDTIM_IN_SUSPEND  "DISABLE_DTIM_IN_SUSPEND"
#define CMD_P2P_DEV_ADDR	"P2P_DEV_ADDR"
#define CMD_SETFWPATH		"SETFWPATH"
#define CMD_SETBAND		"SETBAND"
#define CMD_GETBAND		"GETBAND"
#define CMD_COUNTRY		"COUNTRY"
#define CMD_P2P_SET_NOA		"P2P_SET_NOA"
#define CMD_P2P_GET_NOA			"P2P_GET_NOA"
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

#define CMD_80211_MODE    "MODE"  /* 802.11 mode a/b/g/n/ac */
#define CMD_CHANSPEC      "CHANSPEC"
#define CMD_DATARATE      "DATARATE"
#define CMD_ASSOC_CLIENTS "ASSOCLIST"
#define CMD_SET_CSA       "SETCSA"
#ifdef WL_SUPPORT_AUTO_CHANNEL
#define CMD_SET_HAPD_AUTO_CHANNEL	"HAPD_AUTO_CHANNEL"
#endif /* WL_SUPPORT_AUTO_CHANNEL */
#define CMD_KEEP_ALIVE		"KEEPALIVE"

#ifdef PNO_SUPPORT
#define CMD_PNOSSIDCLR_SET	"PNOSSIDCLR"
#define CMD_PNOSETUP_SET	"PNOSETUP "
#define CMD_PNOENABLE_SET	"PNOFORCE"
#define CMD_PNODEBUG_SET	"PNODEBUG"
#define CMD_WLS_BATCHING	"WLS_BATCHING"
#endif /* PNO_SUPPORT */

#define	CMD_HAPD_MAC_FILTER	"HAPD_MAC_FILTER"

#ifdef WLFBT
#define CMD_GET_FTKEY      "GET_FTKEY"
#endif // endif

#define CMD_ROAM_OFFLOAD			"SETROAMOFFLOAD"
#define CMD_INTERFACE_CREATE			"INTERFACE_CREATE"
#define CMD_INTERFACE_DELETE			"INTERFACE_DELETE"
#define CMD_GET_LINK_STATUS			"GETLINKSTATUS"

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

#ifdef SUPPORT_AP_SUSPEND
#define CMD_SET_AP_SUSPEND			"SET_AP_SUSPEND"
#endif /* SUPPORT_AP_SUSPEND */

#ifdef SUPPORT_AP_BWCTRL
#define CMD_SET_AP_BW			"SET_AP_BW"
#define CMD_GET_AP_BW			"GET_AP_BW"
#endif /* SUPPORT_AP_BWCTRL */

/* miracast related definition */
#define MIRACAST_MODE_OFF	0
#define MIRACAST_MODE_SOURCE	1
#define MIRACAST_MODE_SINK	2

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
#endif // endif

static LIST_HEAD(miracast_resume_list);
#ifdef WL_CFG80211
static u8 miracast_cur_mode;
#endif /* WL_CFG80211 */

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

#ifdef DHD_DEBUG_UART
extern bool dhd_debug_uart_is_running(struct net_device *dev);
#endif	/* DHD_DEBUG_UART */

#ifdef RTT_GEOFENCE_INTERVAL
#if defined(RTT_SUPPORT) && defined(WL_NAN)
#define CMD_GEOFENCE_INTERVAL	"GEOFENCE_INT"
#endif /* RTT_SUPPORT && WL_NAN */
#endif /* RTT_GEOFENCE_INTERVAL */

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

#ifdef SET_PCIE_IRQ_CPU_CORE
#define CMD_PCIE_IRQ_CORE	"PCIE_IRQ_CORE"
#endif /* SET_PCIE_IRQ_CPU_CORE */

#ifdef WL_BCNRECV
#define CMD_BEACON_RECV "BEACON_RECV"
#endif /* WL_BCNRECV */
#ifdef WL_CAC_TS
#define CMD_CAC_TSPEC "CAC_TSPEC"
#endif /* WL_CAC_TS */
#ifdef WL_CHAN_UTIL
#define CMD_GET_CHAN_UTIL "GET_CU"
#endif /* WL_CHAN_UTIL */

#ifdef SUPPORT_SOFTAP_ELNA_BYPASS
#define CMD_SET_SOFTAP_ELNA_BYPASS				"SET_SOFTAP_ELNA_BYPASS"
#define CMD_GET_SOFTAP_ELNA_BYPASS				"GET_SOFTAP_ELNA_BYPASS"
#endif /* SUPPORT_SOFTAP_ELNA_BYPASS */

#ifdef WL_NAN
#define CMD_GET_NAN_STATUS	"GET_NAN_STATUS"
#endif /* WL_NAN */

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
static struct nla_policy wl_genl_policy[BCM_GENL_ATTR_MAX + 1] = {
	[BCM_GENL_ATTR_STRING] = { .type = NLA_NUL_STRING },
	[BCM_GENL_ATTR_MSG] = { .type = NLA_BINARY },
};

#define WL_GENL_VER 1
/* family definition */
static struct genl_family wl_genl_family = {
	.id = GENL_ID_GENERATE,    /* Genetlink would generate the ID */
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
	.policy = wl_genl_policy,
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
#define DEFAULT_BAND		0

/* restoring parameter list, please don't change order */
static android_restore_scan_params_t restore_params[] =
{
/* wbtext need to be disabled while updating roam/scan parameters */
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
	{ CMD_SETBAND, DEFAULT_BAND,
		RESTORE_TYPE_PRIV_CMD, .cmd_handler = wl_android_set_band},
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
int g_wifi_on = TRUE;

/**
 * Local (static) function definitions
 */

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
		ANDROID_ERROR(("wl_android_set_wfds_hash: failed to allocated memory %d bytes\n",
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
		ANDROID_ERROR(("wl_android_set_wfds_hash: failed to %s, error=%d\n", command, error));
	}

	if (smbuf) {
		MFREE(cfg->osh, smbuf, WLC_IOCTL_MAXLEN);
	}
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
	ANDROID_INFO(("wl_android_get_link_speed: command result is %s\n", command));
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
		ANDROID_TRACE(("wl_android_get_rssi: cmd:%s\n", delim));
		/* skip space from delim after finding char */
		delim++;
		if (!(bcm_ether_atoe((delim), &scbval.ea))) {
			ANDROID_ERROR(("wl_android_get_rssi: address err\n"));
			return -1;
		}
		scbval.val = htod32(0);
		ANDROID_TRACE(("wl_android_get_rssi: address:"MACDBG, MAC2STRDBG(scbval.ea.octet)));
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
#if defined(RSSIOFFSET)
	scbval.val = wl_update_rssi_offset(net, scbval.val);
#endif

	error = wldev_get_ssid(target_ndev, &ssid);
	if (error)
		return -1;
	if ((ssid.SSID_len == 0) || (ssid.SSID_len > DOT11_MAX_SSID_LEN)) {
		ANDROID_ERROR(("wl_android_get_rssi: wldev_get_ssid failed\n"));
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

	ANDROID_TRACE(("wl_android_get_rssi: command result is %s (%d)\n", command, bytes_written));
	return bytes_written;
}

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
			ANDROID_INFO(("wl_android_set_suspendopt: Suspend Flag %d -> %d\n",
				ret_now, suspend_flag));
		} else {
			ANDROID_ERROR(("wl_android_set_suspendopt: failed %d\n", ret));
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
		ANDROID_INFO(("wl_android_set_suspendmode: Suspend Mode %d\n", suspend_flag));
	else
		ANDROID_ERROR(("wl_android_set_suspendmode: failed %d\n", ret));
#endif // endif

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

	ANDROID_INFO(("wl_android_get_80211_mode: mode:%s\n", mode));
	bytes_written = snprintf(command, total_len, "%s %s", CMD_80211_MODE, mode);
	ANDROID_INFO(("wl_android_get_80211_mode: command:%s EXIT\n", command));
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
	ANDROID_INFO(("wl_android_get_80211_mode: return value of chanspec:%x\n", chanspec));

	channel = chanspec & WL_CHANSPEC_CHAN_MASK;
	band = chanspec & WL_CHANSPEC_BAND_MASK;
	bw = chanspec & WL_CHANSPEC_BW_MASK;

	ANDROID_INFO(("wl_android_get_80211_mode: channel:%d band:%d bandwidth:%d\n",
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

	ANDROID_INFO(("wl_android_get_chanspec: command:%s EXIT\n", command));
	return bytes_written;

}
#endif /* WL_CFG80211 */

/* returns current datarate datarate returned from firmware are in 500kbps */
int wl_android_get_datarate(struct net_device *dev, char *command, int total_len)
{
	int  error = 0;
	int datarate = 0;
	int bytes_written = 0;

	error = wldev_get_datarate(dev, &datarate);
	if (error)
		return -1;

	ANDROID_INFO(("wl_android_get_datarate: datarate:%d\n", datarate));

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

	ANDROID_TRACE(("wl_android_get_assoclist: ENTER\n"));

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
			ANDROID_ERROR(("wl_android_get_assoclist: Insufficient buffer %d,"
				" bytes_written %d\n",
				total_len, bytes_written));
			bytes_written = -1;
			break;
		}
	}
	return bytes_written;
}

#ifdef WL_CFG80211
extern chanspec_t
wl_chspec_host_to_driver(chanspec_t chanspec);
static int wl_android_set_csa(struct net_device *dev, char *command)
{
	int error = 0;
	char smbuf[WLC_IOCTL_SMLEN];
	wl_chan_switch_t csa_arg;
	u32 chnsp = 0;
	int err = 0;

	ANDROID_INFO(("wl_android_set_csa: command:%s\n", command));

	command = (command + strlen(CMD_SET_CSA));
	/* Order is mode, count channel */
	if (!*++command) {
		ANDROID_ERROR(("wl_android_set_csa:error missing arguments\n"));
		return -1;
	}
	csa_arg.mode = bcm_atoi(command);

	if (csa_arg.mode != 0 && csa_arg.mode != 1) {
		ANDROID_ERROR(("Invalid mode\n"));
		return -1;
	}

	if (!*++command) {
		ANDROID_ERROR(("wl_android_set_csa: error missing count\n"));
		return -1;
	}
	command++;
	csa_arg.count = bcm_atoi(command);

	csa_arg.reg = 0;
	csa_arg.chspec = 0;
	command += 2;
	if (!*command) {
		ANDROID_ERROR(("wl_android_set_csa: error missing channel\n"));
		return -1;
	}

	chnsp = wf_chspec_aton(command);
	if (chnsp == 0)	{
		ANDROID_ERROR(("wl_android_set_csa:chsp is not correct\n"));
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
		ANDROID_ERROR(("wl_android_set_csa:set csa failed:%d\n", error));
		return -1;
	}
	return 0;
}
#endif /* WL_CFG80211 */

static int
wl_android_set_bcn_li_dtim(struct net_device *dev, char *command)
{
	int ret = 0;
	int dtim;

	dtim = *(command + strlen(CMD_SETDTIM_IN_SUSPEND) + 1) - '0';

	if (dtim > (MAX_DTIM_ALLOWED_INTERVAL / MAX_DTIM_SKIP_BEACON_INTERVAL)) {
		ANDROID_ERROR(("%s: failed, invalid dtim %d\n",
			__FUNCTION__, dtim));
		return BCME_ERROR;
	}

	if (!(ret = net_os_set_suspend_bcn_li_dtim(dev, dtim))) {
		ANDROID_TRACE(("%s: SET bcn_li_dtim in suspend %d\n",
			__FUNCTION__, dtim));
	} else {
		ANDROID_ERROR(("%s: failed %d\n", __FUNCTION__, ret));
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
		ANDROID_TRACE(("wl_android_set_max_dtim: use Max bcn_li_dtim in suspend %s\n",
			(dtim_flag ? "Enable" : "Disable")));
	} else {
		ANDROID_ERROR(("wl_android_set_max_dtim: failed %d\n", ret));
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
		ANDROID_TRACE(("wl_android_set_disable_dtim_in_suspend: "
			"use Disable bcn_li_dtim in suspend %s\n",
			(dtim_flag ? "Enable" : "Disable")));
	} else {
		ANDROID_ERROR(("wl_android_set_disable_dtim_in_suspend: failed %d\n", ret));
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

#ifdef WL_CFG80211
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
			ANDROID_ERROR(("WL_HOST_BAND_MGMT defined, "
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
#endif /* WL_CFG80211 */

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

	ANDROID_INFO(("wls_parse_batching_cmd: command=%s, len=%d\n", command, total_len));
	len_remain = total_len;
	if (len_remain > (strlen(CMD_WLS_BATCHING) + 1)) {
		pos = command + strlen(CMD_WLS_BATCHING) + 1;
		len_remain -= strlen(CMD_WLS_BATCHING) + 1;
	} else {
		ANDROID_ERROR(("wls_parse_batching_cmd: No arguments, total_len %d\n", total_len));
		err = BCME_ERROR;
		goto exit;
	}
	bzero(&batch_params, sizeof(struct dhd_pno_batch_params));
	if (!strncmp(pos, PNO_BATCHING_SET, strlen(PNO_BATCHING_SET))) {
		if (len_remain > (strlen(PNO_BATCHING_SET) + 1)) {
			pos += strlen(PNO_BATCHING_SET) + 1;
		} else {
			ANDROID_ERROR(("wls_parse_batching_cmd: %s missing arguments, total_len %d\n",
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
					ANDROID_ERROR(("wls_parse_batching_cmd: invalid format"
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
				ANDROID_ERROR(("wls_parse_batching_cmd : unknown param: %s\n", param));
				err = BCME_ERROR;
				goto exit;
			}
		}
		err = dhd_dev_pno_set_for_batch(dev, &batch_params);
		if (err < 0) {
			ANDROID_ERROR(("failed to configure batch scan\n"));
		} else {
			bzero(command, total_len);
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
			bzero(command, total_len);
			err = snprintf(command, total_len, "OK");
		}
	} else {
		ANDROID_ERROR(("wls_parse_batching_cmd : unknown command\n"));
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
	ANDROID_INFO(("wl_android_set_pno_setup: command=%s, len=%d\n", command, total_len));

	if (total_len < (strlen(CMD_PNOSETUP_SET) + sizeof(cmd_tlv_t))) {
		ANDROID_ERROR(("wl_android_set_pno_setup: argument=%d less min size\n", total_len));
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
			ANDROID_ERROR(("SSID is not presented or corrupted ret=%d\n", nssid));
			goto exit_proc;
		} else {
			if ((str_ptr[0] != PNO_TLV_TYPE_TIME) || (tlv_size_left <= 1)) {
				ANDROID_ERROR(("wl_android_set_pno_setup: scan duration corrupted"
					" field size %d\n",
					tlv_size_left));
				goto exit_proc;
			}
			str_ptr++;
			pno_time = simple_strtoul(str_ptr, &str_ptr, 16);
			ANDROID_INFO(("wl_android_set_pno_setup: pno_time=%d\n", pno_time));

			if (str_ptr[0] != 0) {
				if ((str_ptr[0] != PNO_TLV_FREQ_REPEAT)) {
					ANDROID_ERROR(("wl_android_set_pno_setup: pno repeat:"
						" corrupted field\n"));
					goto exit_proc;
				}
				str_ptr++;
				pno_repeat = simple_strtoul(str_ptr, &str_ptr, 16);
				ANDROID_INFO(("wl_android_set_pno_setup: got pno_repeat=%d\n",
					pno_repeat));
				if (str_ptr[0] != PNO_TLV_FREQ_EXPO_MAX) {
					ANDROID_ERROR(("wl_android_set_pno_setup: FREQ_EXPO_MAX"
						" corrupted field size\n"));
					goto exit_proc;
				}
				str_ptr++;
				pno_freq_expo_max = simple_strtoul(str_ptr, &str_ptr, 16);
				ANDROID_INFO(("wl_android_set_pno_setup: pno_freq_expo_max=%d\n",
					pno_freq_expo_max));
			}
		}
	} else {
		ANDROID_ERROR(("wl_android_set_pno_setup: get wrong TLV command\n"));
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
		ANDROID_ERROR(("wl_android_get_p2p_dev_addr: buflen %d is less than p2p dev addr\n",
			total_len));
		return -1;
	}

	ret = wl_cfg80211_get_p2p_dev_addr(ndev, &p2pdev_addr);
	if (ret) {
		ANDROID_ERROR(("wl_android_get_p2p_dev_addr: Failed to get p2p dev addr\n"));
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
		ANDROID_ERROR(("wl_android_set_ap_mac_list : WLC_SET_MACMODE error=%d\n", ret));
		return ret;
	}
	if (macmode != MACLIST_MODE_DISABLED) {
		/* set the MAC filter list */
		if ((ret = wldev_ioctl_set(dev, WLC_SET_MACLIST, maclist,
			sizeof(int) + sizeof(struct ether_addr) * maclist->count)) != 0) {
			ANDROID_ERROR(("wl_android_set_ap_mac_list : WLC_SET_MACLIST error=%d\n", ret));
			return ret;
		}
		/* get the current list of associated STAs */
		assoc_maclist->count = MAX_NUM_OF_ASSOCLIST;
		if ((ret = wldev_ioctl_get(dev, WLC_GET_ASSOCLIST, assoc_maclist,
			sizeof(mac_buf))) != 0) {
			ANDROID_ERROR(("wl_android_set_ap_mac_list: WLC_GET_ASSOCLIST error=%d\n",
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
					ANDROID_INFO(("wl_android_set_ap_mac_list: associated="MACDBG
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
						ANDROID_ERROR(("wl_android_set_ap_mac_list:"
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
	dhd_pub_t *dhd = dhd_get_pub(dev);

	/* string should look like below (macmode/macnum/maclist) */
	/*   1 2 00:11:22:33:44:55 00:11:22:33:44:ff  */

	/* get the MAC filter mode */
	token = strsep((char**)&str, " ");
	if (!token) {
		return -1;
	}
	macmode = bcm_atoi(token);

	if (macmode < MACLIST_MODE_DISABLED || macmode > MACLIST_MODE_ALLOW) {
		ANDROID_ERROR(("wl_android_set_mac_address_filter: invalid macmode %d\n", macmode));
		return -1;
	}

	token = strsep((char**)&str, " ");
	if (!token) {
		return -1;
	}
	macnum = bcm_atoi(token);
	if (macnum < 0 || macnum > MAX_NUM_MAC_FILT) {
		ANDROID_ERROR(("wl_android_set_mac_address_filter: invalid number of MAC"
			" address entries %d\n",
			macnum));
		return -1;
	}
	/* allocate memory for the MAC list */
	list = (struct maclist*) MALLOCZ(dhd->osh, sizeof(int) +
		sizeof(struct ether_addr) * macnum);
	if (!list) {
		ANDROID_ERROR(("wl_android_set_mac_address_filter : failed to allocate memory\n"));
		return -1;
	}
	/* prepare the MAC list */
	list->count = htod32(macnum);
	bzero((char *)eabuf, ETHER_ADDR_STR_LEN);
	for (i = 0; i < list->count; i++) {
		token = strsep((char**)&str, " ");
		if (token == NULL) {
			ANDROID_ERROR(("wl_android_set_mac_address_filter : No mac address present\n"));
			ret = -EINVAL;
			goto exit;
		}
		strlcpy(eabuf, token, sizeof(eabuf));
		if (!(ret = bcm_ether_atoe(eabuf, &list->ea[i]))) {
			ANDROID_ERROR(("wl_android_set_mac_address_filter : mac parsing err index=%d,"
				" addr=%s\n",
				i, eabuf));
			list->count = i;
			break;
		}
		ANDROID_INFO(("wl_android_set_mac_address_filter : %d/%d MACADDR=%s",
			i, list->count, eabuf));
	}
	if (i == 0)
		goto exit;

	/* set the list */
	if ((ret = wl_android_set_ap_mac_list(dev, macmode, list)) != 0)
		ANDROID_ERROR(("wl_android_set_mac_address_filter: Setting MAC list failed error=%d\n",
			ret));

exit:
	MFREE(dhd->osh, list, sizeof(int) + sizeof(struct ether_addr) * macnum);

	return ret;
}

/**
 * Global function definitions (declared in wl_android.h)
 */

int wl_android_wifi_on(struct net_device *dev)
{
	int ret = 0;
	int retry = POWERUP_MAX_RETRY;

	if (!dev) {
		ANDROID_ERROR(("wl_android_wifi_on: dev is null\n"));
		return -EINVAL;
	}

	dhd_net_if_lock(dev);
	WL_MSG(dev->name, "in g_wifi_on=%d\n", g_wifi_on);
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
			ANDROID_ERROR(("\nfailed to power up wifi chip, retry again (%d left) **\n\n",
				retry));
#ifdef BCMPCIE
			dhd_net_bus_devreset(dev, TRUE);
#endif /* BCMPCIE */
			dhd_net_wifi_platform_set_power(dev, FALSE, WIFI_TURNOFF_DELAY);
		} while (retry-- > 0);
		if (ret != 0) {
			ANDROID_ERROR(("\nfailed to power up wifi chip, max retry reached **\n\n"));
#ifdef BCM_DETECT_TURN_ON_FAILURE
			BUG_ON(1);
#endif /* BCM_DETECT_TURN_ON_FAILURE */
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
	WL_MSG(dev->name, "Success\n");
	dhd_net_if_unlock(dev);
	return ret;

#if defined(BCMSDIO) || defined(BCMDBUS)
err:
	dhd_net_bus_devreset(dev, TRUE);
#ifdef BCMSDIO
	dhd_net_bus_suspend(dev);
#endif /* BCMSDIO */
	dhd_net_wifi_platform_set_power(dev, FALSE, WIFI_TURNOFF_DELAY);
	WL_MSG(dev->name, "Failed\n");
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

#if defined(BCMPCIE) && defined(DHD_DEBUG_UART)
	ret = dhd_debug_uart_is_running(dev);
	if (ret) {
		ANDROID_ERROR(("wl_android_wifi_off: - Debug UART App is running\n"));
		return -EBUSY;
	}
#endif	/* BCMPCIE && DHD_DEBUG_UART */
	dhd_net_if_lock(dev);
	WL_MSG(dev->name, "in g_wifi_on=%d, on_failure=%d\n", g_wifi_on, on_failure);
	if (g_wifi_on || on_failure) {
#if defined(BCMSDIO) || defined(BCMPCIE) || defined(BCMDBUS)
		ret = dhd_net_bus_devreset(dev, TRUE);
#ifdef BCMSDIO
		dhd_net_bus_suspend(dev);
#endif /* BCMSDIO */
#endif /* BCMSDIO || BCMPCIE || BCMDBUS */
		dhd_net_wifi_platform_set_power(dev, FALSE, WIFI_TURNOFF_DELAY);
		g_wifi_on = FALSE;
	}
	WL_MSG(dev->name, "out\n");
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

	ANDROID_INFO(("wl_android_get_connection_stats: enter Get Connection Stats\n"));

	if (total_len <= 0) {
		ANDROID_ERROR(("wl_android_get_connection_stats: invalid buffer size %d\n", total_len));
		goto error;
	}

	bufsize = total_len;
	if (bufsize < sizeof(struct connection_stats)) {
		ANDROID_ERROR(("wl_android_get_connection_stats: not enough buffer size, provided=%u,"
			" requires=%zu\n",
			bufsize,
			sizeof(struct connection_stats)));
		goto error;
	}

	output = (struct connection_stats *)command;

#ifndef DISABLE_IF_COUNTERS
	if_stats = (wl_if_stats_t *)MALLOCZ(cfg->osh, sizeof(*if_stats));
	if (if_stats == NULL) {
		ANDROID_ERROR(("wl_android_get_connection_stats: MALLOCZ failed\n"));
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
		ANDROID_ERROR(("wl_android_get_connection_stats: if_counters not supported ret=%d\n",
			ret));

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
			ANDROID_ERROR(("wl_android_get_connection_stats:"
			" wl_cntbuf_to_xtlv_format ERR %d\n",
			ret));
			goto error;
		}

		if (!(wlc_cnt = GET_WLCCNT_FROM_CNTBUF(iovar_buf))) {
			ANDROID_ERROR(("wl_android_get_connection_stats: wlc_cnt NULL!\n"));
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
			ANDROID_ERROR(("wl_android_get_connection_stats: incorrect version of"
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
		ANDROID_ERROR(("wl_android_get_connection_stats: wldev_get_link_speed()"
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
		ANDROID_ERROR(("natoe subcmd not provided wl_android_process_natoe_cmd\n"));
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
		ANDROID_ERROR(("wl_natoe_get_ioctl: get command failed code %d\n", res));
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
		ANDROID_ERROR(("ioctl memory alloc failed\n"));
		return -ENOMEM;
	}

	/* alloc mem for ioctl headr + tlv data */
	natoe_ioc = (wl_natoe_ioc_t *)MALLOCZ(cfg->osh, iocsz);
	if (!natoe_ioc) {
		ANDROID_ERROR(("ioctl header memory alloc failed\n"));
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
			ANDROID_ERROR(("Fail to get iovar wl_android_natoe_subcmd_enable\n"));
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
		ANDROID_ERROR(("ioctl memory alloc failed\n"));
		return -ENOMEM;
	}

	/* alloc mem for ioctl headr + tlv data */
	natoe_ioc = (wl_natoe_ioc_t *)MALLOCZ(cfg->osh, iocsz);
	if (!natoe_ioc) {
		ANDROID_ERROR(("ioctl header memory alloc failed\n"));
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
			ANDROID_ERROR(("Fail to get iovar wl_android_natoe_subcmd_config_ips\n"));
			ret = -EINVAL;
		}
	} else {	/* set */
		/* buflen is max tlv data we can write, it will be decremented as we pack */
		/* save buflen at start */
		uint16 buflen_at_start = buflen;

		bzero(&config_ips, sizeof(config_ips));

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
		ANDROID_ERROR(("ioctl memory alloc failed\n"));
		return -ENOMEM;
	}

	/* alloc mem for ioctl headr + tlv data */
	natoe_ioc = (wl_natoe_ioc_t *)MALLOCZ(cfg->osh, iocsz);
	if (!natoe_ioc) {
		ANDROID_ERROR(("ioctl header memory alloc failed\n"));
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
			ANDROID_ERROR(("Fail to get iovar wl_android_natoe_subcmd_config_ports\n"));
			ret = -EINVAL;
		}
	} else {	/* set */
		/* buflen is max tlv data we can write, it will be decremented as we pack */
		/* save buflen at start */
		uint16 buflen_at_start = buflen;

		bzero(&ports_config, sizeof(ports_config));

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
	uint16 kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;
	uint16 iocsz = sizeof(*natoe_ioc) + WL_NATOE_DBG_STATS_BUFSZ;
	uint16 buflen = WL_NATOE_DBG_STATS_BUFSZ;
	bcm_xtlv_t *pxtlv = NULL;
	char *ioctl_buf = NULL;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	ioctl_buf = (char *)MALLOCZ(cfg->osh, WLC_IOCTL_MAXLEN);
	if (!ioctl_buf) {
		ANDROID_ERROR(("ioctl memory alloc failed\n"));
		return -ENOMEM;
	}

	/* alloc mem for ioctl headr + tlv data */
	natoe_ioc = (wl_natoe_ioc_t *)MALLOCZ(cfg->osh, iocsz);
	if (!natoe_ioc) {
		ANDROID_ERROR(("ioctl header memory alloc failed\n"));
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
			ANDROID_ERROR(("Fail to get iovar wl_android_natoe_subcmd_dbg_stats\n"));
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
	uint16 kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;
	uint16 iocsz = sizeof(*natoe_ioc) + WL_NATOE_IOC_BUFSZ;
	uint16 buflen = WL_NATOE_IOC_BUFSZ;
	bcm_xtlv_t *pxtlv = NULL;
	char *ioctl_buf = NULL;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	ioctl_buf = (char *)MALLOCZ(cfg->osh, WLC_IOCTL_MEDLEN);
	if (!ioctl_buf) {
		ANDROID_ERROR(("ioctl memory alloc failed\n"));
		return -ENOMEM;
	}

	/* alloc mem for ioctl headr + tlv data */
	natoe_ioc = (wl_natoe_ioc_t *)MALLOCZ(cfg->osh, iocsz);
	if (!natoe_ioc) {
		ANDROID_ERROR(("ioctl header memory alloc failed\n"));
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
			ANDROID_ERROR(("Fail to get iovar wl_android_natoe_subcmd_tbl_cnt\n"));
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
		ANDROID_ERROR(("mbo subcmd not provided %s\n", __FUNCTION__));
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
		ANDROID_ERROR(("Fail to sent wnm notif %d\n", ret));
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
		ANDROID_ERROR(("%s: Bad argument !!\n", __FUNCTION__));
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
			ANDROID_ERROR(("%s: Unknown tlv %u\n", __FUNCTION__, type));
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
		ANDROID_ERROR(("iov buf memory alloc exited\n"));
		goto exit;
	}
	iov_resp = (uint8 *)MALLOCZ(cfg->osh, WLC_IOCTL_MAXLEN);
	if (iov_resp == NULL) {
		ret = -ENOMEM;
		ANDROID_ERROR(("iov resp memory alloc exited\n"));
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
			ANDROID_ERROR(("Mismatch: resp id %d req id %d\n", p_resp->id, cmd->id));
			goto exit;
		}
	} else {
		uint8 cell_cap = bcm_atoi(pcmd);
		const uint8* old_cell_cap = NULL;
		uint16 len = 0;

		old_cell_cap = bcm_get_data_from_xtlv_buf((uint8 *)p_resp->data, p_resp->len,
			WL_MBO_XTLV_CELL_DATA_CAP, &len, BCM_XTLV_OPTION_ALIGN32);
		if (old_cell_cap && *old_cell_cap == cell_cap) {
			ANDROID_ERROR(("No change is cellular data capability\n"));
			/* No change in value */
			goto exit;
		}

		buflen = buflen_start = WLC_IOCTL_MEDLEN - sizeof(bcm_iov_buf_t);

		if (cell_cap < MBO_CELL_DATA_CONN_AVAILABLE ||
			cell_cap > MBO_CELL_DATA_CONN_NOT_CAPABLE) {
			ANDROID_ERROR(("wrong value %u\n", cell_cap));
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
			ANDROID_ERROR(("Fail to set iovar %d\n", ret));
			ret = -EINVAL;
			goto exit;
		}
		/* Skip for CUSTOMER_HW4 - WNM notification
		 * for cellular data capability is handled by host
		 */
		/* send a WNM notification request to associated AP */
		if (wl_get_drv_status(cfg, CONNECTED, dev)) {
			ANDROID_INFO(("Sending WNM Notif\n"));
			ret = wl_android_send_wnm_notif(dev, iov_buf, WLC_IOCTL_MEDLEN,
				iov_resp, WLC_IOCTL_MAXLEN, MBO_ATTR_CELL_DATA_CAP);
			if (ret != BCME_OK) {
				ANDROID_ERROR(("Fail to send WNM notification %d\n", ret));
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

	ANDROID_INFO(("Total bytes written at begining %u\n", cmd_info->bytes_written));
	UNUSED_PARAMETER(len);
	if (data == NULL) {
		ANDROID_ERROR(("%s: Bad argument !!\n", __FUNCTION__));
		return -EINVAL;
	}
	switch (type) {
		case WL_MBO_XTLV_OPCLASS:
		{
			bytes_written = snprintf(command, total_len, "%u:", *data);
			ANDROID_ERROR(("wr %u %u\n", bytes_written, *data));
			command += bytes_written;
			cmd_info->bytes_written += bytes_written;
		}
		break;
		case WL_MBO_XTLV_CHAN:
		{
			bytes_written = snprintf(command, total_len, "%u:", *data);
			ANDROID_ERROR(("wr %u\n", bytes_written));
			command += bytes_written;
			cmd_info->bytes_written += bytes_written;
		}
		break;
		case WL_MBO_XTLV_PREFERENCE:
		{
			bytes_written = snprintf(command, total_len, "%u:", *data);
			ANDROID_ERROR(("wr %u\n", bytes_written));
			command += bytes_written;
			cmd_info->bytes_written += bytes_written;
		}
		break;
		case WL_MBO_XTLV_REASON_CODE:
		{
			bytes_written = snprintf(command, total_len, "%u ", *data);
			ANDROID_ERROR(("wr %u\n", bytes_written));
			command += bytes_written;
			cmd_info->bytes_written += bytes_written;
		}
		break;
		default:
			ANDROID_ERROR(("%s: Unknown tlv %u\n", __FUNCTION__, type));
	}
	ANDROID_INFO(("Total bytes written %u\n", cmd_info->bytes_written));
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

	ANDROID_ERROR(("%s:%d\n", __FUNCTION__, __LINE__));
	iov_buf = (bcm_iov_buf_t *)MALLOCZ(cfg->osh, WLC_IOCTL_MEDLEN);
	if (iov_buf == NULL) {
		ret = -ENOMEM;
		ANDROID_ERROR(("iov buf memory alloc exited\n"));
		goto exit;
	}
	iov_resp = (uint8 *)MALLOCZ(cfg->osh, WLC_IOCTL_MAXLEN);
	if (iov_resp == NULL) {
		ret = -ENOMEM;
		ANDROID_ERROR(("iov resp memory alloc exited\n"));
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
			ANDROID_ERROR(("Version mismatch. returned ver %u expected %u\n",
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
			ANDROID_ERROR(("Mismatch: resp id %d req id %d\n", p_resp->id, cmd->id));
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
				ANDROID_ERROR(("Fail to set iovar %d\n", ret));
				ret = -EINVAL;
				goto exit;
			}
		} else {
			ANDROID_ERROR(("Unknown command %s\n", str));
			goto exit;
		}
		/* parse non pref channel list */
		if (strnicmp(str, "set", 3) == 0) {
			uint8 cnt = 0;
			str = bcmstrtok(&pcmd, " ", NULL);
			while (str != NULL) {
				ret = sscanf(str, "%u:%u:%u:%u", &opcl, &ch, &pref, &rc);
				ANDROID_ERROR(("buflen %u op %u, ch %u, pref %u rc %u\n",
					buflen, opcl, ch, pref, rc));
				if (ret != 4) {
					ANDROID_ERROR(("Not all parameter presents\n"));
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
				ANDROID_ERROR(("len %u\n", (buflen_start - buflen)));
				/* Now set the new non pref channels */
				iov_buf->version = WL_MBO_IOV_VERSION;
				iov_buf->id = WL_MBO_CMD_ADD_CHAN_PREF;
				iov_buf->len = buflen_start - buflen;
				iovlen = sizeof(bcm_iov_buf_t) + iov_buf->len;
				ret = wldev_iovar_setbuf(dev, "mbo",
					iov_buf, iovlen, iov_resp, WLC_IOCTL_MEDLEN, NULL);
				if (ret != BCME_OK) {
					ANDROID_ERROR(("Fail to set iovar %d\n", ret));
					ret = -EINVAL;
					goto exit;
				}
				cnt++;
				if (cnt >= MBO_MAX_CHAN_PREF_ENTRIES) {
					break;
				}
				ANDROID_ERROR(("%d cnt %u\n", __LINE__, cnt));
				str = bcmstrtok(&pcmd, " ", NULL);
			}
		}
		/* send a WNM notification request to associated AP */
		if (wl_get_drv_status(cfg, CONNECTED, dev)) {
			ANDROID_INFO(("Sending WNM Notif\n"));
			ret = wl_android_send_wnm_notif(dev, iov_buf, WLC_IOCTL_MEDLEN,
				iov_resp, WLC_IOCTL_MAXLEN, MBO_ATTR_NON_PREF_CHAN_REPORT);
			if (ret != BCME_OK) {
				ANDROID_ERROR(("Fail to send WNM notification %d\n", ret));
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
	uint32 band = WLC_BAND_2G, sta_band = WLC_BAND_2G;
	uint32 buf_size;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	if (cmd_str) {
		ANDROID_INFO(("Command: %s len:%d \n", cmd_str, (int)strlen(cmd_str)));
		if (strnicmp(cmd_str, APCS_BAND_AUTO, strlen(APCS_BAND_AUTO)) == 0) {
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
				ANDROID_ERROR(("Invalid argument\n"));
				return -EINVAL;
			}
		}
	} else {
		/* If no argument is provided, default to 2G */
		ANDROID_ERROR(("No argument given default to 2.4G scan\n"));
		band = WLC_BAND_2G;
	}
	ANDROID_INFO(("HAPD_AUTO_CHANNEL = %d, band=%d \n", channel, band));

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
		sta_band = WL_GET_BAND(channel);
		switch (sta_band) {
			case (WL_CHANSPEC_BAND_5G): {
				if (band == WLC_BAND_2G || band == WLC_BAND_AUTO) {
					channel = APCS_DEFAULT_2G_CH;
				}
				break;
			}
			case (WL_CHANSPEC_BAND_2G): {
				if (band == WLC_BAND_5G) {
					channel = APCS_DEFAULT_5G_CH;
				}
				break;
			}
			default:
				/* Intentional fall through to use same sta channel for softap */
				break;
		}
		WL_MSG(dev->name, "band=%d, sta_band=%d, channel=%d\n", band, sta_band, channel);
		goto done2;
	}

	channel = wl_ext_autochannel(dev, ACS_FW_BIT|ACS_DRV_BIT, band);
	if (channel)
		goto done2;
	else
		goto done;

	ret = wldev_ioctl_get(dev, WLC_GET_SPECT_MANAGMENT, &spect, sizeof(spect));
	if (ret) {
		ANDROID_ERROR(("ACS: error getting the spect, ret=%d\n", ret));
		goto done;
	}

	if (spect > 0) {
		ret = wl_cfg80211_set_spect(dev, 0);
		if (ret < 0) {
			ANDROID_ERROR(("ACS: error while setting spect, ret=%d\n", ret));
			goto done;
		}
	}

	reqbuf = (u8 *)MALLOCZ(cfg->osh, CHANSPEC_BUF_SIZE);
	if (reqbuf == NULL) {
		ANDROID_ERROR(("failed to allocate chanspec buffer\n"));
		return -ENOMEM;
	}

	if (band == WLC_BAND_AUTO) {
		ANDROID_INFO(("ACS full channel scan \n"));
		reqbuf[0] = htod32(0);
	} else if (band == WLC_BAND_5G) {
		ANDROID_INFO(("ACS 5G band scan \n"));
		if ((ret = wl_cfg80211_get_chanspecs_5g(dev, reqbuf, CHANSPEC_BUF_SIZE)) < 0) {
			ANDROID_ERROR(("ACS 5g chanspec retreival failed! \n"));
			goto done;
		}
	} else if (band == WLC_BAND_2G) {
		/*
		 * If channel argument is not provided/ argument 20 is provided,
		 * Restrict channel to 2GHz, 20MHz BW, No SB
		 */
		ANDROID_INFO(("ACS 2G band scan \n"));
		if ((ret = wl_cfg80211_get_chanspecs_2g(dev, reqbuf, CHANSPEC_BUF_SIZE)) < 0) {
			ANDROID_ERROR(("ACS 2g chanspec retreival failed! \n"));
			goto done;
		}
	} else {
		ANDROID_ERROR(("ACS: No band chosen\n"));
		goto done2;
	}

	buf_size = (band == WLC_BAND_AUTO) ? sizeof(int) : CHANSPEC_BUF_SIZE;
	ret = wldev_ioctl_set(dev, WLC_START_CHANNEL_SEL, (void *)reqbuf,
		buf_size);
	if (ret < 0) {
		ANDROID_ERROR(("can't start auto channel scan, err = %d\n", ret));
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
				WL_MSG(dev->name, "selected channel = %d\n", channel);
				break;
			}
		}
		ANDROID_INFO(("%d tried, ret = %d, chosen = 0x%x\n",
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
		ANDROID_ERROR(("ACS failed. Fall back to default channel (%d) \n", channel));
	}
done2:
	if (spect > 0) {
		if ((ret = wl_cfg80211_set_spect(dev, spect) < 0)) {
			ANDROID_ERROR(("ACS: error while setting spect\n"));
		}
	}

	if (reqbuf) {
		MFREE(cfg->osh, reqbuf, CHANSPEC_BUF_SIZE);
	}

	if (channel) {
		ret = snprintf(command, total_len, "%d", channel);
		ANDROID_INFO(("command result is %s \n", command));
	}

	return ret;
}
#endif /* WL_SUPPORT_AUTO_CHANNEL */

int wl_android_set_roam_mode(struct net_device *dev, char *command)
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
	vndr_ie = (vndr_ie_setbuf_t *)MALLOCZ(cfg->osh, tot_len);
	if (!vndr_ie) {
		ANDROID_ERROR(("IE memory alloc failed\n"));
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
		ANDROID_ERROR(("ioctl memory alloc failed\n"));
		if (vndr_ie) {
			MFREE(cfg->osh, vndr_ie, tot_len);
		}
		return -ENOMEM;
	}
	bzero(ioctl_buf, WLC_IOCTL_MEDLEN);	/* init the buffer */
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
#endif /* WL_CFG80211 */

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
			bzero(pref, WPA_SUITE_LEN);
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
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	resume_cfg = (struct io_cfg *)MALLOCZ(cfg->osh, sizeof(struct io_cfg));
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
		resume_cfg->arg = MALLOCZ(cfg->osh, config->len);
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
		ANDROID_ERROR(("%s: Failed to get Parameter\n", __FUNCTION__));
		return -1;
	}

	ANDROID_INFO(("%s: enter miracast mode %d\n", __FUNCTION__, mode));

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
		/* FALLTROUGH */
		/* Source mode shares most configurations with sink mode.
		 * Fall through here to avoid code duplication
		 */
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
	ANDROID_ERROR(("%s: turnoff miracast mode because of err%d\n", __FUNCTION__, ret));
	wl_android_iolist_resume(dev, &miracast_resume_list);
	return ret;
}
#endif /* WL_CFG80211 */

#ifdef WL_RELMCAST
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
#endif // endif

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
#endif // endif

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
	ANDROID_INFO(("netlink_unicast() pid=%d, ret=%d\n", pid, ret));

nlmsg_failure:
	return ret;
}
#endif /* WL_RELMCAST */

int wl_keep_alive_set(struct net_device *dev, char* extra)
{
	wl_mkeep_alive_pkt_t	mkeep_alive_pkt;
	int ret;
	uint period_msec = 0;
	char *buf;
	dhd_pub_t *dhd = dhd_get_pub(dev);

	if (extra == NULL) {
		 ANDROID_ERROR(("%s: extra is NULL\n", __FUNCTION__));
		 return -1;
	}
	if (sscanf(extra, "%d", &period_msec) != 1) {
		 ANDROID_ERROR(("%s: sscanf error. check period_msec value\n", __FUNCTION__));
		 return -EINVAL;
	}
	ANDROID_ERROR(("%s: period_msec is %d\n", __FUNCTION__, period_msec));

	bzero(&mkeep_alive_pkt, sizeof(wl_mkeep_alive_pkt_t));

	mkeep_alive_pkt.period_msec = period_msec;
	mkeep_alive_pkt.version = htod16(WL_MKEEP_ALIVE_VERSION);
	mkeep_alive_pkt.length = htod16(WL_MKEEP_ALIVE_FIXED_LEN);

	/* Setup keep alive zero for null packet generation */
	mkeep_alive_pkt.keep_alive_id = 0;
	mkeep_alive_pkt.len_bytes = 0;

	buf = (char *)MALLOC(dhd->osh, WLC_IOCTL_SMLEN);
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
	MFREE(dhd->osh, buf, WLC_IOCTL_SMLEN);
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
		ANDROID_ERROR(("tbow_doho iovar error %d\n", err));
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
	uint encode, txexp;
	wl_bss_info_t *bi;
	int datalen = sizeof(uint32) + sizeof(wl_bss_info_t);
	char buf[WLC_IOCTL_SMLEN];

	if (datalen > WLC_IOCTL_SMLEN) {
		ANDROID_ERROR(("data too big\n"));
		return -1;
	}

	bzero(buf, datalen);
	/* get BSS information */
	*(u32 *) buf = htod32(datalen);
	error = wldev_ioctl_get(dev, WLC_GET_BSS_INFO, (void *)buf, datalen);
	if (unlikely(error)) {
		ANDROID_ERROR(("Could not get bss info %d\n", error));
		return -1;
	}

	bi = (wl_bss_info_t*) (buf + sizeof(uint32));

	for (i = 0; i < ETHER_ADDR_LEN; i++) {
		if (bi->BSSID.octet[i] > 0) {
			break;
		}
	}

	if (i == ETHER_ADDR_LEN) {
		ANDROID_INFO(("No BSSID\n"));
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

	ANDROID_INFO(("%s:result=%d, stf=%d, single_stream=%d, mcs map=%d\n",
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
		ANDROID_ERROR(("Wl %p or cfg->p2p %p is null\n",
			cfg, cfg ? cfg->p2p : 0));
		return 0;
	}

	dhd =  (dhd_pub_t *)(cfg->pub);
	if (!dhd->up) {
		ANDROID_ERROR(("bus is already down\n"));
		return ret;
	}

	bssidx = wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE);
	ret = wldev_iovar_setbuf_bsscfg(bcmcfg_to_prmry_ndev(cfg),
			"p2po_stop", (void*)&p2plo_pause, sizeof(p2plo_pause),
			cfg->ioctl_buf, WLC_IOCTL_SMLEN, bssidx, &cfg->ioctl_buf_sync);
	if (ret < 0) {
		ANDROID_ERROR(("p2po_stop Failed :%d\n", ret));
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
		ANDROID_ERROR(("Sending Action Frames. Try it again.\n"));
		goto exit;
	}

	if (wl_get_drv_status_all(cfg, SCANNING)) {
		ANDROID_ERROR(("Scanning already\n"));
		goto exit;
	}

	if (wl_get_drv_status(cfg, SCAN_ABORTING, dev)) {
		ANDROID_ERROR(("Scanning being aborted\n"));
		goto exit;
	}

	if (wl_get_p2p_status(cfg, DISC_IN_PROGRESS)) {
		ANDROID_ERROR(("p2p listen offloading already running\n"));
		goto exit;
	}

	/* Just in case if it is not enabled */
	if ((ret = wl_cfgp2p_enable_discovery(cfg, dev, NULL, 0)) < 0) {
		ANDROID_ERROR(("cfgp2p_enable discovery failed"));
		goto exit;
	}

	bzero(&p2plo_listen, sizeof(wl_p2plo_listen_t));

	if (len) {
		sscanf(buf, " %10d %10d %10d %10d", &channel, &period, &interval, &count);
		if ((channel == 0) || (period == 0) ||
				(interval == 0) || (count == 0)) {
			ANDROID_ERROR(("Wrong argument %d/%d/%d/%d \n",
				channel, period, interval, count));
			ret = -EAGAIN;
			goto exit;
		}
		p2plo_listen.period = period;
		p2plo_listen.interval = interval;
		p2plo_listen.count = count;

		ANDROID_ERROR(("channel:%d period:%d, interval:%d count:%d\n",
			channel, period, interval, count));
	} else {
		ANDROID_ERROR(("Argument len is wrong.\n"));
		ret = -EAGAIN;
		goto exit;
	}

	if ((ret = wldev_iovar_setbuf_bsscfg(dev, "p2po_listen_channel", (void*)&channel,
			sizeof(channel), cfg->ioctl_buf, WLC_IOCTL_SMLEN,
			bssidx, &cfg->ioctl_buf_sync)) < 0) {
		ANDROID_ERROR(("p2po_listen_channel Failed :%d\n", ret));
		goto exit;
	}

	if ((ret = wldev_iovar_setbuf_bsscfg(dev, "p2po_listen", (void*)&p2plo_listen,
			sizeof(wl_p2plo_listen_t), cfg->ioctl_buf, WLC_IOCTL_SMLEN,
			bssidx, &cfg->ioctl_buf_sync)) < 0) {
		ANDROID_ERROR(("p2po_listen Failed :%d\n", ret));
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
		ANDROID_ERROR(("p2po_stop Failed :%d\n", ret));
		goto exit;
	}

exit:
	return ret;
}

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
	bzero(&bssid, ETHER_ADDR_LEN);
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
		ANDROID_INFO(("reassoc issued successfully\n"));
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
	ANDROID_INFO(("ifname %s, version %d, count %d, mimo rssi %d\n",
		ifname, rssi_ant_mimo.version, rssi_ant_mimo.count, mimo_rssi));
	if (mimo_rssi) {
		ANDROID_INFO(("MIMO RSSI: %d\n", rssi_ant_mimo.rssi_sum));
		bytes_written = snprintf(command, total_len, "%s MIMO %d",
			CMD_GET_RSSI_PER_ANT, rssi_ant_mimo.rssi_sum);
	} else {
		int cnt;
		bytes_written = snprintf(command, total_len, "%s PER_ANT ", CMD_GET_RSSI_PER_ANT);
		for (cnt = 0; cnt < rssi_ant_mimo.count; cnt++) {
			ANDROID_INFO(("RSSI[%d]: %d\n", cnt, rssi_ant_mimo.rssi_ant[cnt]));
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

	ANDROID_INFO(("enable %d, RSSI threshold %d, Time threshold %d\n", set_param.enable,
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

	ANDROID_INFO(("report_count %d, enable %d, rssi_threshold %d, time_threshold %d\n",
		get_param.report_count, get_param.enable, get_param.rssi_threshold,
		get_param.time_threshold));

	/* Parse the parameter */
	if (!get_param.enable) {
		ANDROID_INFO(("RSSI LOGGING: Feature is disables\n"));
		bytes_written = snprintf(command, total_len,
			"%s FEATURE DISABLED\n", CMD_GET_RSSI_LOGGING);
	} else if (get_param.enable &
		(RSSILOG_FLAG_FEATURE_SW | RSSILOG_FLAG_REPORT_READY)) {
		if (!get_param.report_count) {
			ANDROID_INFO(("[PASS] RSSI difference across antennas is within"
				" threshold limits\n"));
			bytes_written = snprintf(command, total_len, "%s PASS\n",
				CMD_GET_RSSI_LOGGING);
		} else {
			ANDROID_INFO(("[FAIL] RSSI difference across antennas found "
				"to be greater than %3d dB\n", get_param.rssi_threshold));
			ANDROID_INFO(("[FAIL] RSSI difference check have failed for "
				"%d out of %d times\n", get_param.report_count,
				get_param.time_threshold));
			ANDROID_INFO(("[FAIL] RSSI difference is being monitored once "
				"per second, for a %d secs window\n", get_param.time_threshold));
			bytes_written = snprintf(command, total_len, "%s FAIL - RSSI Threshold "
				"%d dBm for %d out of %d times\n", CMD_GET_RSSI_LOGGING,
				get_param.rssi_threshold, get_param.report_count,
				get_param.time_threshold);
		}
	} else {
		ANDROID_INFO(("[BUSY] Reprot is not ready\n"));
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
		ANDROID_ERROR(("dhd is NULL\n"));
		return;
	}

	if (affinity_cmd < PCIE_IRQ_AFFINITY_OFF || affinity_cmd > PCIE_IRQ_AFFINITY_LAST) {
		ANDROID_ERROR(("Wrong Affinity cmds:%d, %s\n", affinity_cmd, __FUNCTION__));
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
		ANDROID_ERROR(("failed to set lqcm enable %d, error = %d\n", lqcm_enable, err));
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
		ANDROID_ERROR(("failed to get lqcm report, error = %d\n", err));
		return -EIO;
	}
	lqcm_enable = lqcm_report & LQCM_ENAB_MASK;
	tx_lqcm_idx = (lqcm_report & LQCM_TX_INDEX_MASK) >> LQCM_TX_INDEX_SHIFT;
	rx_lqcm_idx = (lqcm_report & LQCM_RX_INDEX_MASK) >> LQCM_RX_INDEX_SHIFT;

	ANDROID_INFO(("lqcm report EN:%d, TX:%d, RX:%d\n", lqcm_enable, tx_lqcm_idx, rx_lqcm_idx));

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

	ANDROID_INFO(("rate %d, ifacename %s\n", rate, ifname));

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

	ANDROID_INFO(("ifacename %s\n", ifname));

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
	ANDROID_INFO(("ifacename %s\n", name));

	bytes_written = wl_get_ap_rps(dev, command, name, total_len);
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
	ANDROID_INFO(("enable %d, ifacename %s\n", enable, name));

	err = wl_set_ap_rps(dev, enable? TRUE: FALSE, name);
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

	ANDROID_INFO(("pps %d, level %d, quiettime %d, sta_assoc_check %d, "
		"ifacename %s\n", rps.pps, rps.level, rps.quiet_time,
		rps.sta_assoc_check, name));

	err = wl_update_ap_rps_params(dev, &rps, name);
	if (unlikely(err)) {
		ANDROID_ERROR(("Failed to update rps, pps %d, level %d, quiettime %d, "
			"sta_assoc_check %d, err = %d\n", rps.pps, rps.level, rps.quiet_time,
			rps.sta_assoc_check, err));
	}

	return err;
}
#endif /* SUPPORT_AP_RADIO_PWRSAVE */

#ifdef DHD_SEND_HANG_PRIVCMD_ERRORS
static void
wl_android_check_priv_cmd_errors(struct net_device *dev)
{
	dhd_pub_t *dhdp;
	int memdump_mode;

	if (!dev) {
		ANDROID_ERROR(("dev is NULL\n"));
		return;
	}

	dhdp = wl_cfg80211_get_dhdp(dev);
	if (!dhdp) {
		ANDROID_ERROR(("dhdp is NULL\n"));
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
		ANDROID_ERROR(("Send HANG event due to sequential private cmd errors\n"));
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

#ifdef SUPPORT_AP_SUSPEND
int
wl_android_set_ap_suspend(struct net_device *dev, char *command, int total_len)
{
	int suspend = 0;
	char *pos, *token;
	char *ifname = NULL;
	int err = BCME_OK;
	char name[IFNAMSIZ];

	/*
	 * DRIVER SET_AP_SUSPEND <0/1> <ifname>
	 */
	pos = command;

	/* drop command */
	token = bcmstrtok(&pos, " ", NULL);

	/* Enable */
	token = bcmstrtok(&pos, " ", NULL);
	if (!token) {
		return -EINVAL;
	}
	suspend = bcm_atoi(token);

	/* get the interface name */
	token = bcmstrtok(&pos, " ", NULL);
	if (!token) {
		return -EINVAL;
	}
	ifname = token;

	strlcpy(name, ifname, sizeof(name));
	ANDROID_INFO(("suspend %d, ifacename %s\n", suspend, name));

	err = wl_set_ap_suspend(dev, suspend? TRUE: FALSE, name);
	if (unlikely(err)) {
		ANDROID_ERROR(("Failed to set suspend, suspend %d, error = %d\n", suspend, err));
	}

	return err;
}
#endif /* SUPPORT_AP_SUSPEND */

#ifdef SUPPORT_AP_BWCTRL
int
wl_android_set_ap_bw(struct net_device *dev, char *command, int total_len)
{
	int bw = DOT11_OPER_MODE_20MHZ;
	char *pos, *token;
	char *ifname = NULL;
	int err = BCME_OK;
	char name[IFNAMSIZ];

	/*
	 * DRIVER SET_AP_BW <0/1/2> <ifname>
	 * 0 : 20MHz, 1 : 40MHz, 2 : 80MHz 3: 80+80 or 160MHz
	 * This is from operating mode field
	 * in 8.4.1.50 of 802.11ac-2013
	 */
	pos = command;

	/* drop command */
	token = bcmstrtok(&pos, " ", NULL);

	/* BW */
	token = bcmstrtok(&pos, " ", NULL);
	if (!token) {
		return -EINVAL;
	}
	bw = bcm_atoi(token);

	/* get the interface name */
	token = bcmstrtok(&pos, " ", NULL);
	if (!token) {
		return -EINVAL;
	}
	ifname = token;

	strlcpy(name, ifname, sizeof(name));
	ANDROID_INFO(("bw %d, ifacename %s\n", bw, name));

	err = wl_set_ap_bw(dev, bw, name);
	if (unlikely(err)) {
		ANDROID_ERROR(("Failed to set bw, bw %d, error = %d\n", bw, err));
	}

	return err;
}

int
wl_android_get_ap_bw(struct net_device *dev, char *command, int total_len)
{
	char *pos, *token;
	char *ifname = NULL;
	int bytes_written = 0;
	char name[IFNAMSIZ];

	/*
	 * DRIVER GET_AP_BW <ifname>
	 * returns 0 : 20MHz, 1 : 40MHz, 2 : 80MHz 3: 80+80 or 160MHz
	 * This is from operating mode field
	 * in 8.4.1.50 of 802.11ac-2013
	 */
	pos = command;

	/* drop command */
	token = bcmstrtok(&pos, " ", NULL);

	/* get the interface name */
	token = bcmstrtok(&pos, " ", NULL);
	if (!token) {
		return -EINVAL;
	}
	ifname = token;

	strlcpy(name, ifname, sizeof(name));
	ANDROID_INFO(("ifacename %s\n", name));

	bytes_written = wl_get_ap_bw(dev, command, name, total_len);
	if (bytes_written < 1) {
		ANDROID_ERROR(("Failed to get bw, error = %d\n", bytes_written));
		return -EPROTO;
	}

	return bytes_written;

}
#endif /* SUPPORT_AP_BWCTRL */

int wl_android_priv_cmd(struct net_device *net, struct ifreq *ifr)
{
#define PRIVATE_COMMAND_MAX_LEN	8192
#define PRIVATE_COMMAND_DEF_LEN	4096
	int ret = 0;
	char *command = NULL;
	int bytes_written = 0;
	android_wifi_priv_cmd priv_cmd;
	int buf_size = 0;
	dhd_pub_t *dhd = dhd_get_pub(net);

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
	command = (char *)MALLOC(dhd->osh, (buf_size + 1));
	if (!command) {
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
			ANDROID_ERROR(("%s: err. bytes_written:%d >= total_len:%d, buf_size:%d\n",
				__FUNCTION__, bytes_written, priv_cmd.total_len, buf_size));

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
	MFREE(dhd->osh, command, (buf_size + 1));
	return ret;
}

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
		ANDROID_ERROR(("skb alloc failed"));
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
		ANDROID_ERROR(("UNKNOWN ATTR_TYPE. attr_type:%d\n", attr_type));
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
		ANDROID_ERROR(("Scan in progress, Aborting beacon recv start, "
			"error:%d\n", err));
		goto exit;
	}

	if (wl_get_p2p_status(cfg, SCANNING)) {
		err = BCME_UNSUPPORTED;
		ANDROID_ERROR(("P2P Scan in progress, Aborting beacon recv start, "
			"error:%d\n", err));
		goto exit;
	}

	if (wl_get_drv_status(cfg, REMAINING_ON_CHANNEL, ndev)) {
		err = BCME_UNSUPPORTED;
		ANDROID_ERROR(("P2P remain on channel, Aborting beacon recv start, "
			"error:%d\n", err));
		goto exit;
	}

	/* check STA is in connected state, Beacon recv required connected state
	 * else exit from beacon recv scan
	 */
	if (!wl_get_drv_status(cfg, CONNECTED, ndev)) {
		err = BCME_UNSUPPORTED;
		ANDROID_ERROR(("STA is in not associated state error:%d\n", err));
		goto exit;
	}

#ifdef WL_NAN
	/* Check NAN is enabled, if enabled exit else continue */
	if (wl_cfgnan_check_state(cfg)) {
		err = BCME_UNSUPPORTED;
		ANDROID_ERROR(("Nan is enabled, NAN+STA+FAKEAP concurrency is not supported\n"));
		goto exit;
	}
#endif /* WL_NAN */

	/* Triggering an sendup_bcn iovar */
	err = wldev_iovar_setint(ndev, "sendup_bcn", 1);
	if (unlikely(err)) {
		ANDROID_ERROR(("sendup_bcn failed to set, error:%d\n", err));
	} else {
		cfg->bcnrecv_info.bcnrecv_state = BEACON_RECV_STARTED;
		WL_INFORM_MEM(("bcnrecv started. user_trigger:%d\n", user_trigger));
		if (user_trigger) {
			if ((err = wl_android_bcnrecv_event(ndev, BCNRECV_ATTR_STATUS,
					WL_BCNRECV_STARTED, 0, NULL, 0)) != BCME_OK) {
				ANDROID_ERROR(("failed to send bcnrecv event, error:%d\n", err));
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
			ANDROID_ERROR(("sendup_bcn failed to set error:%d\n", err));
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
		ANDROID_ERROR(("failed to send bcnrecv event, error:%d\n", err));
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
		ANDROID_ERROR(("ndev is NULL\n"));
		return -EINVAL;
	}

	cfg = wl_get_cfg(ndev);
	if (!cfg) {
		ANDROID_ERROR(("cfg is NULL\n"));
		return -EINVAL;
	}

	/* sync commands from user space */
	mutex_lock(&cfg->usr_sync);
	if (strncmp(cmd_argv, "start", strlen("start")) == 0) {
		ANDROID_INFO(("BCNRECV start\n"));
		err = wl_android_bcnrecv_start(cfg, ndev);
		if (err != BCME_OK) {
			ANDROID_ERROR(("Failed to process the start command, error:%d\n", err));
			goto exit;
		}
	} else if (strncmp(cmd_argv, "stop", strlen("stop")) == 0) {
		ANDROID_INFO(("BCNRECV stop\n"));
		err = wl_android_bcnrecv_stop(ndev, WL_BCNRECV_USER_TRIGGER);
		if (err != BCME_OK) {
			ANDROID_ERROR(("Failed to stop the bcn recv, error:%d\n", err));
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
			ANDROID_ERROR(("Invalid params access_category %hhu nom_msdu_size %hu"
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
		ANDROID_ERROR(("Find index failed\n"));
		err = BCME_ERROR;
		return err;
	}
	err = wldev_iovar_setbuf_bsscfg(ndev, ts_cmd, &tspec_arg, sizeof(tspec_arg),
			cfg->ioctl_buf, WLC_IOCTL_MAXLEN, bssidx, &cfg->ioctl_buf_sync);
	if (unlikely(err)) {
		ANDROID_ERROR(("%s error (%d)\n", ts_cmd, err));
	}

	return err;
}

static s32
wl_android_cac_ts_config(struct net_device *ndev, char *cmd_argv, int total_len)
{
	struct bcm_cfg80211 *cfg = NULL;
	s32 err = BCME_OK;

	if (!ndev) {
		ANDROID_ERROR(("ndev is NULL\n"));
		return -EINVAL;
	}

	cfg = wl_get_cfg(ndev);
	if (!cfg) {
		ANDROID_ERROR(("cfg is NULL\n"));
		return -EINVAL;
	}

	/* Request supported only for primary interface */
	if (ndev != bcmcfg_to_prmry_ndev(cfg)) {
		ANDROID_ERROR(("Request on non-primary interface\n"));
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
		ANDROID_ERROR(("Getting bssload report failed with err=%d \n", err));
		return err;
	}

	(void)memcpy_s(&bssload, sizeof(wl_bssload_t), smbuf, sizeof(wl_bssload_t));
	/* Convert channel usage to percentage value */
	chan_use_percentage = (bssload.chan_util * 100) / 255;

	bytes_written = snprintf(command, total_len, "CU %hhu",
		chan_use_percentage);
	ANDROID_INFO(("Channel Utilization %u %u\n", bssload.chan_util, chan_use_percentage));

	return bytes_written;
}
#endif /* WL_GET_CU */

#ifdef RTT_GEOFENCE_INTERVAL
#if defined(RTT_SUPPORT) && defined(WL_NAN)
static void
wl_android_set_rtt_geofence_interval(struct net_device *ndev, char *command)
{
	int rtt_interval = 0;
	dhd_pub_t *dhdp = wl_cfg80211_get_dhdp(ndev);
	char *rtt_intp = command + strlen(CMD_GEOFENCE_INTERVAL) + 1;

	rtt_interval = bcm_atoi(rtt_intp);
	dhd_rtt_set_geofence_rtt_interval(dhdp, rtt_interval);
}
#endif /* RTT_SUPPORT && WL_NAN */
#endif /* RTT_GEOFENCE_INTERVAL */

#ifdef SUPPORT_SOFTAP_ELNA_BYPASS
int
wl_android_set_softap_elna_bypass(struct net_device *dev, char *command, int total_len)
{
	char *ifname = NULL;
	char *pos, *token;
	int err = BCME_OK;
	int enable = FALSE;

	/*
	 * STA/AP/GO I/F: DRIVER SET_SOFTAP_ELNA_BYPASS <ifname> <enable/disable>
	 * the enable/disable format follows Samsung specific rules as following
	 * Enable : 0
	 * Disable :-1
	 */
	pos = command;

	/* drop command */
	token = bcmstrtok(&pos, " ", NULL);

	/* get the interface name */
	token = bcmstrtok(&pos, " ", NULL);
	if (!token) {
		ANDROID_ERROR(("%s: Invalid arguments about interface name\n", __FUNCTION__));
		return -EINVAL;
	}
	ifname = token;

	/* get enable/disable flag */
	token = bcmstrtok(&pos, " ", NULL);
	if (!token) {
		ANDROID_ERROR(("%s: Invalid arguments about Enable/Disable\n", __FUNCTION__));
		return -EINVAL;
	}
	enable = bcm_atoi(token);

	CUSTOMER_HW4_EN_CONVERT(enable);
	err = wl_set_softap_elna_bypass(dev, ifname, enable);
	if (unlikely(err)) {
		ANDROID_ERROR(("%s: Failed to set ELNA Bypass of SoftAP mode, err=%d\n",
			__FUNCTION__, err));
		return err;
	}

	return err;
}

int
wl_android_get_softap_elna_bypass(struct net_device *dev, char *command, int total_len)
{
	char *ifname = NULL;
	char *pos, *token;
	int err = BCME_OK;
	int bytes_written = 0;
	int softap_elnabypass = 0;

	/*
	 * STA/AP/GO I/F: DRIVER GET_SOFTAP_ELNA_BYPASS <ifname>
	 */
	pos = command;

	/* drop command */
	token = bcmstrtok(&pos, " ", NULL);

	/* get the interface name */
	token = bcmstrtok(&pos, " ", NULL);
	if (!token) {
		ANDROID_ERROR(("%s: Invalid arguments about interface name\n", __FUNCTION__));
		return -EINVAL;
	}
	ifname = token;

	err = wl_get_softap_elna_bypass(dev, ifname, &softap_elnabypass);
	if (unlikely(err)) {
		ANDROID_ERROR(("%s: Failed to get ELNA Bypass of SoftAP mode, err=%d\n",
			__FUNCTION__, err));
		return err;
	} else {
		softap_elnabypass--; //Convert format to Customer HW4
		ANDROID_INFO(("%s: eLNA Bypass feature enable status is %d\n",
			__FUNCTION__, softap_elnabypass));
		bytes_written = snprintf(command, total_len, "%s %d",
			CMD_GET_SOFTAP_ELNA_BYPASS, softap_elnabypass);
	}

	return bytes_written;
}
#endif /* SUPPORT_SOFTAP_ELNA_BYPASS */

#ifdef WL_NAN
int
wl_android_get_nan_status(struct net_device *dev, char *command, int total_len)
{
	int bytes_written = 0;
	int error = BCME_OK;
	wl_nan_conf_status_t nstatus;

	error = wl_cfgnan_get_status(dev, &nstatus);
	if (error) {
		ANDROID_ERROR(("Failed to get nan status (%d)\n", error));
		return error;
	}

	bytes_written = snprintf(command, total_len,
			"EN:%d Role:%d EM:%d CID:"MACF" NMI:"MACF" SC(2G):%d SC(5G):%d "
			"MR:"NMRSTR" AMR:"NMRSTR" IMR:"NMRSTR
			"HC:%d AMBTT:%04x TSF[%04x:%04x]\n",
			nstatus.enabled,
			nstatus.role,
			nstatus.election_mode,
			ETHERP_TO_MACF(&(nstatus.cid)),
			ETHERP_TO_MACF(&(nstatus.nmi)),
			nstatus.social_chans[0],
			nstatus.social_chans[1],
			NMR2STR(nstatus.mr),
			NMR2STR(nstatus.amr),
			NMR2STR(nstatus.imr),
			nstatus.hop_count,
			nstatus.ambtt,
			nstatus.cluster_tsf_h,
			nstatus.cluster_tsf_l);
	return bytes_written;
}
#endif /* WL_NAN */

#ifdef SUPPORT_NAN_RANGING_TEST_BW
enum {
	NAN_RANGING_5G_BW20 = 1,
	NAN_RANGING_5G_BW40,
	NAN_RANGING_5G_BW80
};

int
wl_nan_ranging_bw(struct net_device *net, int bw, char *command)
{
	int bytes_written, err = BCME_OK;
	u8 ioctl_buf[WLC_IOCTL_SMLEN];
	s32 val = 1;
	struct {
		u32 band;
		u32 bw_cap;
	} param = {0, 0};

	if (bw < NAN_RANGING_5G_BW20 || bw > NAN_RANGING_5G_BW80) {
		ANDROID_ERROR(("Wrong BW cmd:%d, %s\n", bw, __FUNCTION__));
		bytes_written = scnprintf(command, sizeof("FAIL"), "FAIL");
		return bytes_written;
	}

	switch (bw) {
		case NAN_RANGING_5G_BW20:
			ANDROID_ERROR(("NAN_RANGING 5G/BW20\n"));
			param.band = WLC_BAND_5G;
			param.bw_cap = 0x1;
			break;
		case NAN_RANGING_5G_BW40:
			ANDROID_ERROR(("NAN_RANGING 5G/BW40\n"));
			param.band = WLC_BAND_5G;
			param.bw_cap = 0x3;
			break;
		case NAN_RANGING_5G_BW80:
			ANDROID_ERROR(("NAN_RANGING 5G/BW80\n"));
			param.band = WLC_BAND_5G;
			param.bw_cap = 0x7;
			break;
	}

	err = wldev_ioctl_set(net, WLC_DOWN, &val, sizeof(s32));
	if (err) {
		ANDROID_ERROR(("WLC_DOWN error %d\n", err));
		bytes_written = scnprintf(command, sizeof("FAIL"), "FAIL");
	} else {
		err = wldev_iovar_setbuf(net, "bw_cap", &param, sizeof(param),
			ioctl_buf, sizeof(ioctl_buf), NULL);

		if (err) {
			ANDROID_ERROR(("BW set failed\n"));
			bytes_written = scnprintf(command, sizeof("FAIL"), "FAIL");
		} else {
			ANDROID_ERROR(("BW set done\n"));
			bytes_written = scnprintf(command, sizeof("OK"), "OK");
		}

		err = wldev_ioctl_set(net, WLC_UP, &val, sizeof(s32));
		if (err < 0) {
			ANDROID_ERROR(("WLC_UP error %d\n", err));
			bytes_written = scnprintf(command, sizeof("FAIL"), "FAIL");
		}
	}
	return bytes_written;
}
#endif /* SUPPORT_NAN_RANGING_TEST_BW */

int
wl_handle_private_cmd(struct net_device *net, char *command, u32 cmd_len)
{
	int bytes_written = 0;
	android_wifi_priv_cmd priv_cmd;

	bzero(&priv_cmd, sizeof(android_wifi_priv_cmd));
	priv_cmd.total_len = cmd_len;

	if (strnicmp(command, CMD_START, strlen(CMD_START)) == 0) {
		ANDROID_INFO(("%s, Received regular START command\n", __FUNCTION__));
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
		ANDROID_ERROR(("%s: Ignore private cmd \"%s\" - iface is down\n",
			__FUNCTION__, command));
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
#ifdef WL_CFG80211
	else if (strnicmp(command, CMD_SCAN_ACTIVE, strlen(CMD_SCAN_ACTIVE)) == 0) {
		wl_cfg80211_set_passive_scan(net, command);
	}
	else if (strnicmp(command, CMD_SCAN_PASSIVE, strlen(CMD_SCAN_PASSIVE)) == 0) {
		wl_cfg80211_set_passive_scan(net, command);
	}
#endif /* WL_CFG80211 */
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
#ifdef WL_CFG80211
	else if (strnicmp(command, CMD_SETBAND, strlen(CMD_SETBAND)) == 0) {
		bytes_written = wl_android_set_band(net, command);
	}
#endif /* WL_CFG80211 */
	else if (strnicmp(command, CMD_GETBAND, strlen(CMD_GETBAND)) == 0) {
		bytes_written = wl_android_get_band(net, command, priv_cmd.total_len);
	}
#ifdef WL_CFG80211
	else if (strnicmp(command, CMD_SET_CSA, strlen(CMD_SET_CSA)) == 0) {
		bytes_written = wl_android_set_csa(net, command);
	} else if (strnicmp(command, CMD_80211_MODE, strlen(CMD_80211_MODE)) == 0) {
		bytes_written = wl_android_get_80211_mode(net, command, priv_cmd.total_len);
	} else if (strnicmp(command, CMD_CHANSPEC, strlen(CMD_CHANSPEC)) == 0) {
		bytes_written = wl_android_get_chanspec(net, command, priv_cmd.total_len);
	}
#endif /* WL_CFG80211 */
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
#ifdef WL_CFG80211
		bytes_written = wl_cfg80211_set_country_code(net, country_code,
				true, true, revinfo);
#else
		bytes_written = wldev_set_country(net, country_code, true, true, revinfo);
#endif /* WL_CFG80211 */
	}
#endif /* CUSTOMER_SET_COUNTRY */
	else if (strnicmp(command, CMD_DATARATE, strlen(CMD_DATARATE)) == 0) {
		bytes_written = wl_android_get_datarate(net, command, priv_cmd.total_len);
	} else if (strnicmp(command, CMD_ASSOC_CLIENTS,	strlen(CMD_ASSOC_CLIENTS)) == 0) {
		bytes_written = wl_android_get_assoclist(net, command, priv_cmd.total_len);
	}

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
	else if (strnicmp(command, CMD_HAPD_MAC_FILTER, strlen(CMD_HAPD_MAC_FILTER)) == 0) {
		int skip = strlen(CMD_HAPD_MAC_FILTER) + 1;
		wl_android_set_mac_address_filter(net, command+skip);
	}
	else if (strnicmp(command, CMD_SETROAMMODE, strlen(CMD_SETROAMMODE)) == 0)
		bytes_written = wl_android_set_roam_mode(net, command);
#if defined(BCMFW_ROAM_ENABLE)
	else if (strnicmp(command, CMD_SET_ROAMPREF, strlen(CMD_SET_ROAMPREF)) == 0) {
		bytes_written = wl_android_set_roampref(net, command, priv_cmd.total_len);
	}
#endif /* BCMFW_ROAM_ENABLE */
#ifdef WL_CFG80211
	else if (strnicmp(command, CMD_MIRACAST, strlen(CMD_MIRACAST)) == 0)
		bytes_written = wl_android_set_miracast(net, command);
	else if (strnicmp(command, CMD_SETIBSSBEACONOUIDATA, strlen(CMD_SETIBSSBEACONOUIDATA)) == 0)
		bytes_written = wl_android_set_ibss_beacon_ouidata(net,
		command, priv_cmd.total_len);
#endif /* WL_CFG80211 */
	else if (strnicmp(command, CMD_KEEP_ALIVE, strlen(CMD_KEEP_ALIVE)) == 0) {
		int skip = strlen(CMD_KEEP_ALIVE) + 1;
		bytes_written = wl_keep_alive_set(net, command + skip);
	}
#ifdef WL_CFG80211
	else if (strnicmp(command, CMD_ROAM_OFFLOAD, strlen(CMD_ROAM_OFFLOAD)) == 0) {
		int enable = *(command + strlen(CMD_ROAM_OFFLOAD) + 1) - '0';
		bytes_written = wl_cfg80211_enable_roam_offload(net, enable);
	}
	else if (strnicmp(command, CMD_INTERFACE_CREATE, strlen(CMD_INTERFACE_CREATE)) == 0) {
		char *name = (command + strlen(CMD_INTERFACE_CREATE) +1);
		ANDROID_INFO(("Creating %s interface\n", name));
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
		ANDROID_INFO(("Deleteing %s interface\n", name));
		bytes_written = wl_cfg80211_del_if(wl_get_cfg(net), net, NULL, name);
	}
#endif /* WL_CFG80211 */
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
#endif /* WL_CFG80211 */
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
	else if (strnicmp(command, CMD_MURX_BFE_CAP,
			strlen(CMD_MURX_BFE_CAP)) == 0) {
#if defined(WL_MURX) && defined(WL_CFG80211)
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
#ifdef SUPPORT_AP_SUSPEND
	else if (strnicmp(command, CMD_SET_AP_SUSPEND, strlen(CMD_SET_AP_SUSPEND)) == 0) {
		bytes_written = wl_android_set_ap_suspend(net, command, priv_cmd.total_len);
	}
#endif /* SUPPORT_AP_SUSPEND */
#ifdef SUPPORT_AP_BWCTRL
	else if (strnicmp(command, CMD_SET_AP_BW, strlen(CMD_SET_AP_BW)) == 0) {
		bytes_written = wl_android_set_ap_bw(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_GET_AP_BW, strlen(CMD_GET_AP_BW)) == 0) {
		bytes_written = wl_android_get_ap_bw(net, command, priv_cmd.total_len);
	}
#endif /* SUPPORT_AP_BWCTRL */
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
#ifdef WL_CFG80211
	else if (strnicmp(command, CMD_DEBUG_VERBOSE, strlen(CMD_DEBUG_VERBOSE)) == 0) {
		int verbose_level = *(command + strlen(CMD_DEBUG_VERBOSE) + 1) - '0';
		bytes_written = wl_cfg80211_set_dbg_verbose(net, verbose_level);
	}
#endif /* WL_CFG80211 */
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
#ifdef RTT_GEOFENCE_INTERVAL
#if defined(RTT_SUPPORT) && defined(WL_NAN)
	else if (strnicmp(command, CMD_GEOFENCE_INTERVAL,
			strlen(CMD_GEOFENCE_INTERVAL)) == 0) {
		(void)wl_android_set_rtt_geofence_interval(net, command);
	}
#endif /* RTT_SUPPORT && WL_NAN */
#endif /* RTT_GEOFENCE_INTERVAL */
#ifdef SUPPORT_SOFTAP_ELNA_BYPASS
	else if (strnicmp(command, CMD_SET_SOFTAP_ELNA_BYPASS,
			strlen(CMD_SET_SOFTAP_ELNA_BYPASS)) == 0) {
		bytes_written =
			wl_android_set_softap_elna_bypass(net, command, priv_cmd.total_len);
	}
	else if (strnicmp(command, CMD_GET_SOFTAP_ELNA_BYPASS,
			strlen(CMD_GET_SOFTAP_ELNA_BYPASS)) == 0) {
		bytes_written =
			wl_android_get_softap_elna_bypass(net, command, priv_cmd.total_len);
	}
#endif /* SUPPORT_SOFTAP_ELNA_BYPASS */
#ifdef WL_NAN
	else if (strnicmp(command, CMD_GET_NAN_STATUS,
			strlen(CMD_GET_NAN_STATUS)) == 0) {
		bytes_written =
			wl_android_get_nan_status(net, command, priv_cmd.total_len);
	}
#endif /* WL_NAN */
#if defined(SUPPORT_NAN_RANGING_TEST_BW)
	else if (strnicmp(command, CMD_NAN_RANGING_SET_BW, strlen(CMD_NAN_RANGING_SET_BW)) == 0) {
		int bw_cmd = *(command + strlen(CMD_NAN_RANGING_SET_BW) + 1) - '0';
		bytes_written = wl_nan_ranging_bw(net, bw_cmd, command);
	}
#endif /* SUPPORT_NAN_RANGING_TEST_BW */
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

#if defined(ENABLE_INSMOD_NO_FW_LOAD) || defined(BUS_POWER_RESTORE)
	dhd_download_fw_on_driverload = FALSE;
#endif /* ENABLE_INSMOD_NO_FW_LOAD */
	if (!iface_name[0]) {
		bzero(iface_name, IFNAMSIZ);
		bcm_strncpy_s(iface_name, IFNAMSIZ, "wlan", IFNAMSIZ);
	}

#ifdef WL_GENL
	wl_genl_init();
#endif // endif
#ifdef WL_RELMCAST
	wl_netlink_init();
#endif /* WL_RELMCAST */

	return ret;
}

int wl_android_exit(void)
{
	int ret = 0;
	struct io_cfg *cur, *q;

#ifdef WL_GENL
	wl_genl_deinit();
#endif /* WL_GENL */
#ifdef WL_RELMCAST
	wl_netlink_deinit();
#endif /* WL_RELMCAST */

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
	printk("%s: btlock released\n", __FUNCTION__);
#endif /* ENABLE_4335BT_WAR */

	if (!dhd_download_fw_on_driverload)
		g_wifi_on = FALSE;
}

#ifdef WL_GENL
/* Generic Netlink Initializaiton */
static int wl_genl_init(void)
{
	int ret;

	ANDROID_INFO(("GEN Netlink Init\n\n"));

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0))
	/* register new family */
	ret = genl_register_family(&wl_genl_family);
	if (ret != 0)
		goto failure;

	/* register functions (commands) of the new family */
	ret = genl_register_ops(&wl_genl_family, &wl_genl_ops);
	if (ret != 0) {
		ANDROID_ERROR(("register ops failed: %i\n", ret));
		genl_unregister_family(&wl_genl_family);
		goto failure;
	}

	ret = genl_register_mc_group(&wl_genl_family, &wl_genl_mcast);
#else
	ret = genl_register_family_with_ops_groups(&wl_genl_family, wl_genl_ops, wl_genl_mcast);
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0) */
	if (ret != 0) {
		ANDROID_ERROR(("register mc_group failed: %i\n", ret));
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0))
		genl_unregister_ops(&wl_genl_family, &wl_genl_ops);
#endif // endif
		genl_unregister_family(&wl_genl_family);
		goto failure;
	}

	return 0;

failure:
	ANDROID_ERROR(("Registering Netlink failed!!\n"));
	return -1;
}

/* Generic netlink deinit */
static int wl_genl_deinit(void)
{

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0))
	if (genl_unregister_ops(&wl_genl_family, &wl_genl_ops) < 0)
		ANDROID_ERROR(("Unregister wl_genl_ops failed\n"));
#endif // endif
	if (genl_unregister_family(&wl_genl_family) < 0)
		ANDROID_ERROR(("Unregister wl_genl_ops failed\n"));

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
			ANDROID_ERROR(("Event not supported\n"));
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
	u16 kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);

	ANDROID_INFO(("Enter \n"));

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
			ANDROID_ERROR(("No sub hdr support for the ATTR STRING type \n"));
			ret =  -EINVAL;
			goto out;
		}

		ret = nla_put_string(skb, BCM_GENL_ATTR_STRING, buf);
		if (ret != 0) {
			ANDROID_ERROR(("nla_put_string failed\n"));
			goto out;
		}
	} else {
		/* ATTR_MSG */

		/* Create a single buffer for all */
		p = ptr = (u8 *)MALLOCZ(cfg->osh, tot_len);
		if (!ptr) {
			ret = -ENOMEM;
			ANDROID_ERROR(("ENOMEM!!\n"));
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
			ANDROID_ERROR(("nla_put_string failed\n"));
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
			ANDROID_ERROR(("genlmsg_multicast for attr(%d) failed. Error:%d \n",
				attr_type, err));
		else
			ANDROID_INFO(("Multicast msg sent successfully. attr_type:%d len:%d \n",
				attr_type, tot_len));
	} else {
		NETLINK_CB(skb).dst_group = 0; /* Not in multicast group */

		/* finalize the message */
		genlmsg_end(skb, msg);

		/* send the message back */
		if (genlmsg_unicast(&init_net, skb, pid) < 0)
			ANDROID_ERROR(("genlmsg_unicast failed\n"));
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

	ANDROID_INFO(("Enter \n"));

	if (info == NULL) {
		return -EINVAL;
	}

	na = info->attrs[BCM_GENL_ATTR_MSG];
	if (!na) {
		ANDROID_ERROR(("nlattribute NULL\n"));
		return -EINVAL;
	}

	data = (char *)nla_data(na);
	if (!data) {
		ANDROID_ERROR(("Invalid data\n"));
		return -EINVAL;
	} else {
		/* Handle the data */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 7, 0)) || defined(WL_COMPAT_WIRELESS)
		ANDROID_INFO(("%s: Data received from pid (%d) \n", __func__,
			info->snd_pid));
#else
		ANDROID_INFO(("%s: Data received from pid (%d) \n", __func__,
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
#include <dhd_linux_priv.h>
struct net_device *
wl_cfg80211_register_static_if(struct bcm_cfg80211 *cfg, u16 iftype, char *ifname)
{
	struct net_device *ndev;
	struct wireless_dev *wdev = NULL;
	int ifidx = WL_STATIC_IFIDX; /* Register ndev with a reserved ifidx */
	u8 mac_addr[ETH_ALEN];
	struct net_device *primary_ndev;
#ifdef DHD_USE_RANDMAC
	struct ether_addr ea_addr;
#endif /* DHD_USE_RANDMAC */
#ifdef CUSTOM_MULTI_MAC
	char hw_ether[62];
	dhd_pub_t *dhd = cfg->pub;
#endif

	WL_INFORM_MEM(("[STATIC_IF] Enter (%s) iftype:%d\n", ifname, iftype));

	if (!cfg) {
		ANDROID_ERROR(("cfg null\n"));
		return NULL;
	}
	primary_ndev = bcmcfg_to_prmry_ndev(cfg);

#ifdef DHD_USE_RANDMAC
	dhd_generate_mac_addr(&ea_addr);
	(void)memcpy_s(mac_addr, ETH_ALEN, ea_addr.octet, ETH_ALEN);
#else
#if defined(CUSTOM_MULTI_MAC)
	if (wifi_platform_get_mac_addr(dhd->info->adapter, hw_ether, "wlan1")) {
#endif
	/* Use primary mac with locally admin bit set */
	(void)memcpy_s(mac_addr, ETH_ALEN, primary_ndev->dev_addr, ETH_ALEN);
	mac_addr[0] |= 0x02;
#if defined(CUSTOM_MULTI_MAC)
	} else {
		(void)memcpy_s(mac_addr, ETH_ALEN, hw_ether, ETH_ALEN);
	}
#endif
#endif /* DHD_USE_RANDMAC */

	ndev = wl_cfg80211_allocate_if(cfg, ifidx, ifname, mac_addr,
		WL_BSSIDX_MAX, NULL);
	if (unlikely(!ndev)) {
		ANDROID_ERROR(("Failed to allocate static_if\n"));
		goto fail;
	}
	wdev = (struct wireless_dev *)MALLOCZ(cfg->osh, sizeof(*wdev));
	if (unlikely(!wdev)) {
		ANDROID_ERROR(("Failed to allocate wdev for static_if\n"));
		goto fail;
	}

	wdev->wiphy = cfg->wdev->wiphy;
	wdev->iftype = iftype;

	ndev->ieee80211_ptr = wdev;
	SET_NETDEV_DEV(ndev, wiphy_dev(wdev->wiphy));
	wdev->netdev = ndev;

	if (wl_cfg80211_register_if(cfg, ifidx,
		ndev, TRUE) != BCME_OK) {
		ANDROID_ERROR(("ndev registration failed!\n"));
		goto fail;
	}

	cfg->static_ndev = ndev;
	cfg->static_ndev_state = NDEV_STATE_OS_IF_CREATED;
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
	WL_INFORM_MEM(("[STATIC_IF] Enter\n"));
	if (!cfg || !cfg->static_ndev) {
		ANDROID_ERROR(("invalid input\n"));
		return;
	}

	/* wdev free will happen from notifier context */
	/* free_netdev(cfg->static_ndev);
	*/
	unregister_netdev(cfg->static_ndev);
}

s32
wl_cfg80211_static_if_open(struct net_device *net)
{
	struct wireless_dev *wdev = NULL;
	struct bcm_cfg80211 *cfg = wl_get_cfg(net);
	struct net_device *primary_ndev = bcmcfg_to_prmry_ndev(cfg);
	u16 iftype = net->ieee80211_ptr ? net->ieee80211_ptr->iftype : 0;
	u16 wl_iftype, wl_mode;
#ifdef CUSTOM_MULTI_MAC
	char hw_ether[62];
	dhd_pub_t *dhd = dhd_get_pub(net);
#endif

	WL_INFORM_MEM(("[STATIC_IF] dev_open ndev %p and wdev %p\n", net, net->ieee80211_ptr));
	ASSERT(cfg->static_ndev == net);

	if (cfg80211_to_wl_iftype(iftype, &wl_iftype, &wl_mode) <  0) {
		return BCME_ERROR;
	}
	if (cfg->static_ndev_state != NDEV_STATE_FW_IF_CREATED) {
#ifdef DHD_USE_RANDMAC
		wdev = wl_cfg80211_add_if(cfg, primary_ndev, wl_iftype, net->name, net->dev_addr);
#else
#if defined(CUSTOM_MULTI_MAC)
		if (wifi_platform_get_mac_addr(dhd->info->adapter, hw_ether, net->name)) {
#endif
		wdev = wl_cfg80211_add_if(cfg, primary_ndev, wl_iftype, net->name, NULL);
#if defined(CUSTOM_MULTI_MAC)
		} else {
			wdev = wl_cfg80211_add_if(cfg, primary_ndev, wl_iftype, net->name, hw_ether);
		}
#endif
#endif // endif
		if (!wdev) {
			ANDROID_ERROR(("[STATIC_IF] wdev is NULL, can't proceed"));
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

	if (cfg->static_ndev_state == NDEV_STATE_FW_IF_CREATED) {
		if (mutex_is_locked(&cfg->if_sync) == TRUE) {
			ret = _wl_cfg80211_del_if(cfg, primary_ndev, net->ieee80211_ptr, net->name);
		} else {
			ret = wl_cfg80211_del_if(cfg, primary_ndev, net->ieee80211_ptr, net->name);
		}

		if (unlikely(ret)) {
			ANDROID_ERROR(("Del iface failed for static_if %d\n", ret));
		}
	}

	return ret;
}
struct net_device *
wl_cfg80211_post_static_ifcreate(struct bcm_cfg80211 *cfg,
	wl_if_event_info *event, u8 *addr, s32 iface_type)
{
	struct net_device *new_ndev = NULL;
	struct wireless_dev *wdev = NULL;

	WL_INFORM_MEM(("Updating static iface after Fw IF create \n"));
	new_ndev = cfg->static_ndev;

	if (new_ndev) {
		wdev = new_ndev->ieee80211_ptr;
		ASSERT(wdev);
		wdev->iftype = iface_type;
		(void)memcpy_s(new_ndev->dev_addr, ETH_ALEN, addr, ETH_ALEN);
	}

	cfg->static_ndev_state = NDEV_STATE_FW_IF_CREATED;
	wl_cfg80211_update_iflist_info(cfg, new_ndev, event->ifidx, addr, event->bssidx,
		event->name, NDEV_STATE_FW_IF_CREATED);
	return new_ndev;
}
s32
wl_cfg80211_post_static_ifdel(struct bcm_cfg80211 *cfg, struct net_device *ndev)
{
	cfg->static_ndev_state = NDEV_STATE_FW_IF_DELETED;
	wl_cfg80211_update_iflist_info(cfg, ndev, WL_STATIC_IFIDX, NULL,
		WL_BSSIDX_MAX, NULL, NDEV_STATE_FW_IF_DELETED);
	wl_cfg80211_clear_per_bss_ies(cfg, ndev->ieee80211_ptr);
	wl_dealloc_netinfo_by_wdev(cfg, ndev->ieee80211_ptr);
	return BCME_OK;
}
#endif /* WL_STATIC_IF */
