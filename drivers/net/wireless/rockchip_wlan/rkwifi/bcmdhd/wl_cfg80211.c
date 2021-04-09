/*
 * Linux cfg80211 driver
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
 * $Id: wl_cfg80211.c 711110 2017-07-17 04:38:25Z $
 */
/* */
#include <typedefs.h>
#include <linuxver.h>
#include <osl.h>
#include <linux/kernel.h>

#include <wlc_types.h>
#include <bcmutils.h>
#include <bcmwifi_channels.h>
#include <bcmendian.h>
#include <ethernet.h>
#include <802.11.h>
#include <linux/if_arp.h>
#include <linux/uaccess.h>

#include <ethernet.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/netdevice.h>
#include <linux/sched.h>
#include <linux/etherdevice.h>
#include <linux/wireless.h>
#include <linux/ieee80211.h>
#include <linux/wait.h>
#include <net/cfg80211.h>
#include <net/rtnetlink.h>

#include <wlioctl.h>
#include <wldev_common.h>
#include <wl_cfg80211.h>
#include <wl_cfgp2p.h>
#include <bcmdevs.h>
#include <wl_android.h>
#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_linux.h>
#include <dhd_debug.h>
#include <dhdioctl.h>
#include <wlioctl.h>
#include <dhd_cfg80211.h>
#include <dhd_bus.h>
#ifdef PNO_SUPPORT
#include <dhd_pno.h>
#endif /* PNO_SUPPORT */
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 14, 0)) || defined(WL_VENDOR_EXT_SUPPORT)
#include <wl_cfgvendor.h>
#endif /* (LINUX_VERSION_CODE > KERNEL_VERSION(3, 14, 0)) || defined(WL_VENDOR_EXT_SUPPORT) */

#if !defined(WL_VENDOR_EXT_SUPPORT)
#undef GSCAN_SUPPORT
#endif

#if defined(STAT_REPORT)
#include <wl_statreport.h>
#endif /* STAT_REPORT */
#include <dhd_config.h>


#ifdef PROP_TXSTATUS
#include <dhd_wlfc.h>
#endif

#ifdef BCMPCIE
#include <dhd_flowring.h>
#endif
#ifdef RTT_SUPPORT
#include <dhd_rtt.h>
#endif /* RTT_SUPPORT */

#define BRCM_SAE_VENDOR_EVENT_BUF_LEN 500

#ifdef WL11U
#endif /* WL11U */

#ifndef DHD_UNSUPPORT_IF_CNTS
#define DHD_SUPPORT_IF_CNTS
#endif /* !DHD_UN_SUPPORT_IF_CNTS */

#ifdef BCMWAPI_WPI
/* these items should evetually go into wireless.h of the linux system headfile dir */
#ifndef IW_ENCODE_ALG_SM4
#define IW_ENCODE_ALG_SM4 0x20
#endif

#ifndef IW_AUTH_WAPI_ENABLED
#define IW_AUTH_WAPI_ENABLED 0x20
#endif

#ifndef IW_AUTH_WAPI_VERSION_1
#define IW_AUTH_WAPI_VERSION_1  0x00000008
#endif

#ifndef IW_AUTH_CIPHER_SMS4
#define IW_AUTH_CIPHER_SMS4     0x00000020
#endif

#ifndef IW_AUTH_KEY_MGMT_WAPI_PSK
#define IW_AUTH_KEY_MGMT_WAPI_PSK 4
#endif

#ifndef IW_AUTH_KEY_MGMT_WAPI_CERT
#define IW_AUTH_KEY_MGMT_WAPI_CERT 8
#endif
#endif /* BCMWAPI_WPI */

#ifdef BCMWAPI_WPI
#define IW_WSEC_ENABLED(wsec)   ((wsec) & (WEP_ENABLED | TKIP_ENABLED | AES_ENABLED | SMS4_ENABLED))
#else /* BCMWAPI_WPI */
#define IW_WSEC_ENABLED(wsec)   ((wsec) & (WEP_ENABLED | TKIP_ENABLED | AES_ENABLED))
#endif /* BCMWAPI_WPI */

static struct device *cfg80211_parent_dev = NULL;
#ifdef CUSTOMER_HW4_DEBUG
u32 wl_dbg_level = WL_DBG_ERR | WL_DBG_P2P_ACTION;
#else
u32 wl_dbg_level = WL_DBG_ERR;
#endif /* CUSTOMER_HW4_DEBUG */

#define MAX_WAIT_TIME 1500
#ifdef WLAIBSS_MCHAN
#define IBSS_IF_NAME "ibss%d"
#endif /* WLAIBSS_MCHAN */

#ifdef VSDB
/* sleep time to keep STA's connecting or connection for continuous af tx or finding a peer */
#define DEFAULT_SLEEP_TIME_VSDB		120
#define OFF_CHAN_TIME_THRESHOLD_MS	200
#define AF_RETRY_DELAY_TIME			40

/* if sta is connected or connecting, sleep for a while before retry af tx or finding a peer */
#define WL_AF_TX_KEEP_PRI_CONNECTION_VSDB(cfg)	\
	do {	\
		if (wl_get_drv_status(cfg, CONNECTED, bcmcfg_to_prmry_ndev(cfg)) ||	\
			wl_get_drv_status(cfg, CONNECTING, bcmcfg_to_prmry_ndev(cfg))) {	\
			OSL_SLEEP(DEFAULT_SLEEP_TIME_VSDB);			\
		}	\
	} while (0)
#else /* VSDB */
/* if not VSDB, do nothing */
#define WL_AF_TX_KEEP_PRI_CONNECTION_VSDB(cfg)
#endif /* VSDB */

#ifdef WL_CFG80211_SYNC_GON
#define WL_DRV_STATUS_SENDING_AF_FRM_EXT(cfg) \
	(wl_get_drv_status_all(cfg, SENDING_ACT_FRM) || \
		wl_get_drv_status_all(cfg, WAITING_NEXT_ACT_FRM_LISTEN))
#else
#define WL_DRV_STATUS_SENDING_AF_FRM_EXT(cfg) wl_get_drv_status_all(cfg, SENDING_ACT_FRM)
#endif /* WL_CFG80211_SYNC_GON */

#define DNGL_FUNC(func, parameters) func parameters
#define COEX_DHCP

#define WLAN_EID_SSID	0
#define CH_MIN_5G_CHANNEL 34
#define CH_MIN_2G_CHANNEL 1
#define ACTIVE_SCAN 1
#define PASSIVE_SCAN 0

#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
#define BCM_SET_LIST_FIRST_ENTRY(entry, ptr, type, member) \
_Pragma("GCC diagnostic push") \
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"") \
(entry) = list_first_entry((ptr), type, member); \
_Pragma("GCC diagnostic pop") \

#define BCM_SET_CONTAINER_OF(entry, ptr, type, member) \
_Pragma("GCC diagnostic push") \
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"") \
entry = container_of((ptr), type, member); \
_Pragma("GCC diagnostic pop") \

#else
#define BCM_SET_LIST_FIRST_ENTRY(entry, ptr, type, member) \
(entry) = list_first_entry((ptr), type, member); \

#define BCM_SET_CONTAINER_OF(entry, ptr, type, member) \
entry = container_of((ptr), type, member); \

#endif /* STRICT_GCC_WARNINGS */

#ifdef WL_RELMCAST
enum rmc_event_type {
	RMC_EVENT_NONE,
	RMC_EVENT_LEADER_CHECK_FAIL
};
#endif /* WL_RELMCAST */

#ifdef WL_LASTEVT
typedef struct wl_last_event {
	uint32 current_time;		/* current tyime */
	uint32 timestamp;		/* event timestamp */
	wl_event_msg_t event;	/* Encapsulated event */
} wl_last_event_t;
#endif /* WL_LASTEVT */

/* This is to override regulatory domains defined in cfg80211 module (reg.c)
 * By default world regulatory domain defined in reg.c puts the flags NL80211_RRF_PASSIVE_SCAN
 * and NL80211_RRF_NO_IBSS for 5GHz channels (for 36..48 and 149..165).
 * With respect to these flags, wpa_supplicant doesn't start p2p operations on 5GHz channels.
 * All the chnages in world regulatory domain are to be done here.
 *
 * this definition reuires disabling missing-field-initializer warning
 * as the ieee80211_regdomain definition differs in plain linux and in Android
 */
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wmissing-field-initializers\"")
#endif
static const struct ieee80211_regdomain brcm_regdom = {
	.n_reg_rules = 4,
	.alpha2 =  "99",
	.reg_rules = {
		/* IEEE 802.11b/g, channels 1..11 */
		REG_RULE(2412-10, 2472+10, 40, 6, 20, 0),
		/* If any */
		/* IEEE 802.11 channel 14 - Only JP enables
		 * this and for 802.11b only
		 */
		REG_RULE(2484-10, 2484+10, 20, 6, 20, 0),
		/* IEEE 802.11a, channel 36..64 */
		REG_RULE(5150-10, 5350+10, 40, 6, 20, 0),
		/* IEEE 802.11a, channel 100..165 */
		REG_RULE(5470-10, 5850+10, 40, 6, 20, 0), }
};
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic pop")
#endif


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)) && \
	(defined(WL_IFACE_COMB_NUM_CHANNELS) || defined(WL_CFG80211_P2P_DEV_IF))
static const struct ieee80211_iface_limit common_if_limits[] = {
	{
	/*
	 * Driver can support up to 2 AP's
	 */
	.max = 2,
	.types = BIT(NL80211_IFTYPE_AP),
	},
	{
	/*
	 * During P2P-GO removal, P2P-GO is first changed to STA and later only
	 * removed. So setting maximum possible number of STA interfaces according
	 * to kernel version.
	 *
	 * less than linux-3.8 - max:3 (wlan0 + p2p0 + group removal of p2p-p2p0-x)
	 * linux-3.8 and above - max:2 (wlan0 + group removal of p2p-wlan0-x)
	 */
#ifdef WL_ENABLE_P2P_IF
	.max = 3,
#else
	.max = 2,
#endif /* WL_ENABLE_P2P_IF */
	.types = BIT(NL80211_IFTYPE_STATION),
	},
	{
	.max = 2,
	.types = BIT(NL80211_IFTYPE_P2P_GO) | BIT(NL80211_IFTYPE_P2P_CLIENT),
	},
#if defined(WL_CFG80211_P2P_DEV_IF)
	{
	.max = 1,
	.types = BIT(NL80211_IFTYPE_P2P_DEVICE),
	},
#endif /* WL_CFG80211_P2P_DEV_IF */
	{
	.max = 1,
	.types = BIT(NL80211_IFTYPE_ADHOC),
	},
};
#ifdef BCM4330_CHIP
#define NUM_DIFF_CHANNELS 1
#else
#define NUM_DIFF_CHANNELS 2
#endif
static const struct ieee80211_iface_combination
common_iface_combinations[] = {
	{
	.num_different_channels = NUM_DIFF_CHANNELS,
	/*
	 * max_interfaces = 4
	 * The max no of interfaces will be used in dual p2p case.
	 * {STA, P2P Device, P2P Group 1, P2P Group 2}. Though we
	 * will not be using the STA functionality in this case, it
	 * will remain registered as it is the primary interface.
	 */
	.max_interfaces = 4,
	.limits = common_if_limits,
	.n_limits = ARRAY_SIZE(common_if_limits),
	},
};
#endif /* LINUX_VER >= 3.0 && (WL_IFACE_COMB_NUM_CHANNELS || WL_CFG80211_P2P_DEV_IF) */

/* Data Element Definitions */
#define WPS_ID_CONFIG_METHODS     0x1008
#define WPS_ID_REQ_TYPE           0x103A
#define WPS_ID_DEVICE_NAME        0x1011
#define WPS_ID_VERSION            0x104A
#define WPS_ID_DEVICE_PWD_ID      0x1012
#define WPS_ID_REQ_DEV_TYPE       0x106A
#define WPS_ID_SELECTED_REGISTRAR_CONFIG_METHODS 0x1053
#define WPS_ID_PRIM_DEV_TYPE      0x1054

/* Device Password ID */
#define DEV_PW_DEFAULT 0x0000
#define DEV_PW_USER_SPECIFIED 0x0001,
#define DEV_PW_MACHINE_SPECIFIED 0x0002
#define DEV_PW_REKEY 0x0003
#define DEV_PW_PUSHBUTTON 0x0004
#define DEV_PW_REGISTRAR_SPECIFIED 0x0005

/* Config Methods */
#define WPS_CONFIG_USBA 0x0001
#define WPS_CONFIG_ETHERNET 0x0002
#define WPS_CONFIG_LABEL 0x0004
#define WPS_CONFIG_DISPLAY 0x0008
#define WPS_CONFIG_EXT_NFC_TOKEN 0x0010
#define WPS_CONFIG_INT_NFC_TOKEN 0x0020
#define WPS_CONFIG_NFC_INTERFACE 0x0040
#define WPS_CONFIG_PUSHBUTTON 0x0080
#define WPS_CONFIG_KEYPAD 0x0100
#define WPS_CONFIG_VIRT_PUSHBUTTON 0x0280
#define WPS_CONFIG_PHY_PUSHBUTTON 0x0480
#define WPS_CONFIG_VIRT_DISPLAY 0x2008
#define WPS_CONFIG_PHY_DISPLAY 0x4008

#define PM_BLOCK 1
#define PM_ENABLE 0


#define WL_AKM_SUITE_SHA256_1X  0x000FAC05
#define WL_AKM_SUITE_SHA256_PSK 0x000FAC06

#ifndef IBSS_COALESCE_ALLOWED
#define IBSS_COALESCE_ALLOWED IBSS_COALESCE_DEFAULT
#endif

#ifndef IBSS_INITIAL_SCAN_ALLOWED
#define IBSS_INITIAL_SCAN_ALLOWED IBSS_INITIAL_SCAN_ALLOWED_DEFAULT
#endif

#define CUSTOM_RETRY_MASK 0xff000000 /* Mask for retry counter of custom dwell time */
#define LONG_LISTEN_TIME 2000

#ifdef WBTEXT
#define CMD_WBTEXT_PROFILE_CONFIG	"WBTEXT_PROFILE_CONFIG"
#define CMD_WBTEXT_WEIGHT_CONFIG	"WBTEXT_WEIGHT_CONFIG"
#define CMD_WBTEXT_TABLE_CONFIG		"WBTEXT_TABLE_CONFIG"
#define CMD_WBTEXT_DELTA_CONFIG		"WBTEXT_DELTA_CONFIG"
#define DEFAULT_WBTEXT_PROFILE_A		"a -70 -75 70 10 -75 -128 0 10"
#define DEFAULT_WBTEXT_PROFILE_B		"b -60 -75 70 10 -75 -128 0 10"
#define DEFAULT_WBTEXT_WEIGHT_RSSI_A	"RSSI a 65"
#define DEFAULT_WBTEXT_WEIGHT_RSSI_B	"RSSI b 65"
#define DEFAULT_WBTEXT_WEIGHT_CU_A	"CU a 35"
#define DEFAULT_WBTEXT_WEIGHT_CU_B	"CU b 35"
#define DEFAULT_WBTEXT_TABLE_RSSI_A	"RSSI a 0 55 100 55 60 90 \
60 65 70 65 70 50 70 128 20"
#define DEFAULT_WBTEXT_TABLE_RSSI_B	"RSSI b 0 55 100 55 60 90 \
60 65 70 65 70 50 70 128 20"
#define DEFAULT_WBTEXT_TABLE_CU_A	"CU a 0 30 100 30 50 90 \
50 60 70 60 80 50 80 100 20"
#define DEFAULT_WBTEXT_TABLE_CU_B	"CU b 0 10 100 10 25 90 \
25 40 70 40 70 50 70 100 20"

typedef struct wl_wbtext_bssid {
	struct ether_addr ea;
	struct list_head list;
} wl_wbtext_bssid_t;

static void wl_cfg80211_wbtext_update_rcc(struct bcm_cfg80211 *cfg, struct net_device *dev);
static bool wl_cfg80211_wbtext_check_bssid_list(struct bcm_cfg80211 *cfg, struct ether_addr *ea);
static bool wl_cfg80211_wbtext_add_bssid_list(struct bcm_cfg80211 *cfg, struct ether_addr *ea);
static void wl_cfg80211_wbtext_clear_bssid_list(struct bcm_cfg80211 *cfg);
static bool wl_cfg80211_wbtext_send_nbr_req(struct bcm_cfg80211 *cfg, struct net_device *dev,
	struct wl_profile *profile);
static bool wl_cfg80211_wbtext_send_btm_query(struct bcm_cfg80211 *cfg, struct net_device *dev,
	struct wl_profile *profile);
static void wl_cfg80211_wbtext_set_wnm_maxidle(struct bcm_cfg80211 *cfg, struct net_device *dev);
static int wl_cfg80211_recv_nbr_resp(struct net_device *dev, uint8 *body, int body_len);
#endif /* WBTEXT */

#ifdef SUPPORT_AP_RADIO_PWRSAVE
#define RADIO_PWRSAVE_PPS					10
#define RADIO_PWRSAVE_QUIET_TIME			10
#define RADIO_PWRSAVE_LEVEL				3
#define RADIO_PWRSAVE_STAS_ASSOC_CHECK	0

#define RADIO_PWRSAVE_LEVEL_MIN				1
#define RADIO_PWRSAVE_LEVEL_MAX				5
#define RADIO_PWRSAVE_PPS_MIN					1
#define RADIO_PWRSAVE_QUIETTIME_MIN			1
#define RADIO_PWRSAVE_ASSOCCHECK_MIN		0
#define RADIO_PWRSAVE_ASSOCCHECK_MAX		1

#define RADIO_PWRSAVE_MAJOR_VER 1
#define RADIO_PWRSAVE_MINOR_VER 0
#define RADIO_PWRSAVE_MAJOR_VER_SHIFT 8
#define RADIO_PWRSAVE_VERSION \
	((RADIO_PWRSAVE_MAJOR_VER << RADIO_PWRSAVE_MAJOR_VER_SHIFT)| RADIO_PWRSAVE_MINOR_VER)
#endif /* SUPPORT_AP_RADIO_PWRSAVE */

#ifdef WLADPS_SEAK_AP_WAR
#define ATHEROS_OUI "\x00\x03\x7F"
#define CAMEO_MAC_PREFIX "\x00\x18\xE7"
#define MAC_PREFIX_LEN 3
static bool
wl_find_vndr_ies_specific_vender(struct bcm_cfg80211 *cfg,
	struct net_device *ndev, const u8 *vndr_oui);
static s32 wl_set_adps_mode(struct bcm_cfg80211 *cfg, struct net_device *ndev, uint8 enable_mode);
#endif /* WLADPS_SEAK_AP_WAR */

#define MAX_VNDR_OUI_STR_LEN	256
#define VNDR_OUI_STR_LEN	10
static const uchar *exclude_vndr_oui_list[] = {
	"\x00\x50\xf2",			/* Microsoft */
	"\x00\x00\xf0",			/* Samsung Elec */
	WFA_OUI,			/* WFA */
	NULL
};

typedef struct wl_vndr_oui_entry {
	uchar oui[DOT11_OUI_LEN];
	struct list_head list;
} wl_vndr_oui_entry_t;

static int wl_vndr_ies_get_vendor_oui(struct bcm_cfg80211 *cfg,
		struct net_device *ndev, char *vndr_oui, u32 vndr_oui_len);
static void wl_vndr_ies_clear_vendor_oui_list(struct bcm_cfg80211 *cfg);

/*
 * cfg80211_ops api/callback list
 */
static s32 wl_frame_get_mgmt(u16 fc, const struct ether_addr *da,
	const struct ether_addr *sa, const struct ether_addr *bssid,
	u8 **pheader, u32 *body_len, u8 *pbody);
static s32 __wl_cfg80211_scan(struct wiphy *wiphy, struct net_device *ndev,
	struct cfg80211_scan_request *request,
	struct cfg80211_ssid *this_ssid);
#if defined(WL_CFG80211_P2P_DEV_IF)
static s32
wl_cfg80211_scan(struct wiphy *wiphy, struct cfg80211_scan_request *request);
#else
static s32
wl_cfg80211_scan(struct wiphy *wiphy, struct net_device *ndev,
	struct cfg80211_scan_request *request);
#endif /* WL_CFG80211_P2P_DEV_IF */
static s32 wl_cfg80211_set_wiphy_params(struct wiphy *wiphy, u32 changed);
#ifdef WLAIBSS_MCHAN
static bcm_struct_cfgdev* bcm_cfg80211_add_ibss_if(struct wiphy *wiphy, char *name);
static s32 bcm_cfg80211_del_ibss_if(struct wiphy *wiphy, bcm_struct_cfgdev *cfgdev);
#endif /* WLAIBSS_MCHAN */
static s32 wl_cfg80211_join_ibss(struct wiphy *wiphy, struct net_device *dev,
	struct cfg80211_ibss_params *params);
static s32 wl_cfg80211_leave_ibss(struct wiphy *wiphy,
	struct net_device *dev);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
static s32 wl_cfg80211_get_station(struct wiphy *wiphy,
	struct net_device *dev, const u8 *mac,
	struct station_info *sinfo);
#else
static s32 wl_cfg80211_get_station(struct wiphy *wiphy,
	struct net_device *dev, u8 *mac,
	struct station_info *sinfo);
#endif
static s32 wl_cfg80211_set_power_mgmt(struct wiphy *wiphy,
	struct net_device *dev, bool enabled,
	s32 timeout);
static int wl_cfg80211_connect(struct wiphy *wiphy, struct net_device *dev,
	struct cfg80211_connect_params *sme);
static s32 wl_cfg80211_disconnect(struct wiphy *wiphy, struct net_device *dev,
	u16 reason_code);
#if defined(WL_CFG80211_P2P_DEV_IF)
static s32
wl_cfg80211_set_tx_power(struct wiphy *wiphy, struct wireless_dev *wdev,
	enum nl80211_tx_power_setting type, s32 mbm);
#else
static s32
wl_cfg80211_set_tx_power(struct wiphy *wiphy,
	enum nl80211_tx_power_setting type, s32 dbm);
#endif /* WL_CFG80211_P2P_DEV_IF */
#if defined(WL_CFG80211_P2P_DEV_IF)
static s32 wl_cfg80211_get_tx_power(struct wiphy *wiphy,
	struct wireless_dev *wdev, s32 *dbm);
#else
static s32 wl_cfg80211_get_tx_power(struct wiphy *wiphy, s32 *dbm);
#endif /* WL_CFG80211_P2P_DEV_IF */
static s32 wl_cfg80211_config_default_key(struct wiphy *wiphy,
	struct net_device *dev,
	u8 key_idx, bool unicast, bool multicast);
static s32 wl_cfg80211_add_key(struct wiphy *wiphy, struct net_device *dev,
	u8 key_idx, bool pairwise, const u8 *mac_addr,
	struct key_params *params);
static s32 wl_cfg80211_del_key(struct wiphy *wiphy, struct net_device *dev,
	u8 key_idx, bool pairwise, const u8 *mac_addr);
static s32 wl_cfg80211_get_key(struct wiphy *wiphy, struct net_device *dev,
	u8 key_idx, bool pairwise, const u8 *mac_addr,
	void *cookie, void (*callback) (void *cookie,
	struct key_params *params));
static s32 wl_cfg80211_config_default_mgmt_key(struct wiphy *wiphy,
	struct net_device *dev,	u8 key_idx);
static s32 wl_cfg80211_resume(struct wiphy *wiphy);
#if defined(WL_SUPPORT_BACKPORTED_KPATCHES) || (LINUX_VERSION_CODE >= KERNEL_VERSION(3, \
	2, 0))
static s32 wl_cfg80211_mgmt_tx_cancel_wait(struct wiphy *wiphy,
	bcm_struct_cfgdev *cfgdev, u64 cookie);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0))
static s32 wl_cfg80211_del_station(
		struct wiphy *wiphy, struct net_device *ndev,
		struct station_del_parameters *params);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
static s32 wl_cfg80211_del_station(struct wiphy *wiphy,
	struct net_device *ndev, const u8* mac_addr);
#else
static s32 wl_cfg80211_del_station(struct wiphy *wiphy,
	struct net_device *ndev, u8* mac_addr);
#endif
#ifdef WLMESH_CFG80211
static s32 wl_cfg80211_join_mesh(
	struct wiphy *wiphy, struct net_device *dev,
	const struct mesh_config *conf,
	const struct mesh_setup *setup);
static s32 wl_cfg80211_leave_mesh(struct wiphy *wiphy,
	struct net_device *dev);
#endif /* WLMESH_CFG80211 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
static s32 wl_cfg80211_change_station(struct wiphy *wiphy,
	struct net_device *dev, const u8 *mac, struct station_parameters *params);
#else
static s32 wl_cfg80211_change_station(struct wiphy *wiphy,
	struct net_device *dev, u8 *mac, struct station_parameters *params);
#endif
#endif /* WL_SUPPORT_BACKPORTED_KPATCHES || KERNEL_VER >= KERNEL_VERSION(3, 2, 0)) */
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39))
static s32 wl_cfg80211_suspend(struct wiphy *wiphy, struct cfg80211_wowlan *wow);
#else
static s32 wl_cfg80211_suspend(struct wiphy *wiphy);
#endif
static s32 wl_cfg80211_set_pmksa(struct wiphy *wiphy, struct net_device *dev,
	struct cfg80211_pmksa *pmksa);
static s32 wl_cfg80211_del_pmksa(struct wiphy *wiphy, struct net_device *dev,
	struct cfg80211_pmksa *pmksa);
static s32 wl_cfg80211_flush_pmksa(struct wiphy *wiphy,
	struct net_device *dev);
void wl_cfg80211_scan_abort(struct bcm_cfg80211 *cfg);
static void wl_cfg80211_cancel_scan(struct bcm_cfg80211 *cfg);
static s32 wl_notify_escan_complete(struct bcm_cfg80211 *cfg,
	struct net_device *ndev, bool aborted, bool fw_abort);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 2, 0))
#if (defined(CONFIG_ARCH_MSM) && defined(TDLS_MGMT_VERSION2)) || (LINUX_VERSION_CODE < \
	KERNEL_VERSION(3, 16, 0) && LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0))
static s32 wl_cfg80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev,
	u8 *peer, u8 action_code, u8 dialog_token, u16 status_code,
	u32 peer_capability, const u8 *buf, size_t len);
#elif ((LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)) && \
		(LINUX_VERSION_CODE < KERNEL_VERSION(3, 18, 0)))
static s32 wl_cfg80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev,
	const u8 *peer, u8 action_code, u8 dialog_token, u16 status_code,
	u32 peer_capability, const u8 *buf, size_t len);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0))
static s32 wl_cfg80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev,
       const u8 *peer, u8 action_code, u8 dialog_token, u16 status_code,
       u32 peer_capability, bool initiator, const u8 *buf, size_t len);
#else /* CONFIG_ARCH_MSM && TDLS_MGMT_VERSION2 */
static s32 wl_cfg80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev,
	u8 *peer, u8 action_code, u8 dialog_token, u16 status_code,
	const u8 *buf, size_t len);
#endif /* CONFIG_ARCH_MSM && TDLS_MGMT_VERSION2 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
static s32 wl_cfg80211_tdls_oper(struct wiphy *wiphy, struct net_device *dev,
	const u8 *peer, enum nl80211_tdls_operation oper);
#else
static s32 wl_cfg80211_tdls_oper(struct wiphy *wiphy, struct net_device *dev,
	u8 *peer, enum nl80211_tdls_operation oper);
#endif
#endif 
#ifdef WL_SCHED_SCAN
static int wl_cfg80211_sched_scan_stop(struct wiphy *wiphy, struct net_device *dev
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0))
	, u64 reqid
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)) */
);
#endif
static s32 wl_cfg80211_set_ap_role(struct bcm_cfg80211 *cfg, struct net_device *dev);
#if defined(WL_VIRTUAL_APSTA) || defined(DUAL_STA_STATIC_IF)
bcm_struct_cfgdev*
wl_cfg80211_create_iface(struct wiphy *wiphy, enum nl80211_iftype
		 iface_type, u8 *mac_addr, const char *name);
s32
wl_cfg80211_del_iface(struct wiphy *wiphy, bcm_struct_cfgdev *cfgdev);
#endif /* defined(WL_VIRTUAL_APSTA) || defined(DUAL_STA_STATIC_IF) */

s32 wl_cfg80211_interface_ops(struct bcm_cfg80211 *cfg,
	struct net_device *ndev, s32 bsscfg_idx,
	enum nl80211_iftype iface_type, s32 del, u8 *addr);
s32 wl_cfg80211_add_del_bss(struct bcm_cfg80211 *cfg,
	struct net_device *ndev, s32 bsscfg_idx,
	enum nl80211_iftype iface_type, s32 del, u8 *addr);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0))
static s32 wl_cfg80211_stop_ap(struct wiphy *wiphy, struct net_device *dev);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0) */
#ifdef GTK_OFFLOAD_SUPPORT
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0))
static s32 wl_cfg80211_set_rekey_data(struct wiphy *wiphy, struct net_device *dev,
	struct cfg80211_gtk_rekey_data *data);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0) */
#endif /* GTK_OFFLOAD_SUPPORT */
chanspec_t wl_chspec_driver_to_host(chanspec_t chanspec);
chanspec_t wl_chspec_host_to_driver(chanspec_t chanspec);
#ifdef WL11ULB
static s32 wl_cfg80211_get_ulb_bw(struct bcm_cfg80211 *cfg, struct wireless_dev *wdev);
static chanspec_t wl_cfg80211_ulb_get_min_bw_chspec(struct bcm_cfg80211 *cfg,
	struct wireless_dev *wdev, s32 bssidx);
static s32 wl_cfg80211_ulbbw_to_ulbchspec(u32 ulb_bw);
#else
static inline chanspec_t wl_cfg80211_ulb_get_min_bw_chspec(
		struct bcm_cfg80211 *cfg, struct wireless_dev *wdev, s32 bssidx)
{
	return WL_CHANSPEC_BW_20;
}
#endif /* WL11ULB */
static void wl_cfg80211_wait_for_disconnection(struct bcm_cfg80211 *cfg, struct net_device *dev);

/*
 * event & event Q handlers for cfg80211 interfaces
 */
static s32 wl_create_event_handler(struct bcm_cfg80211 *cfg);
static void wl_destroy_event_handler(struct bcm_cfg80211 *cfg);
static void wl_event_handler(struct work_struct *work_data);
static void wl_init_eq(struct bcm_cfg80211 *cfg);
static void wl_flush_eq(struct bcm_cfg80211 *cfg);
static unsigned long wl_lock_eq(struct bcm_cfg80211 *cfg);
static void wl_unlock_eq(struct bcm_cfg80211 *cfg, unsigned long flags);
static void wl_init_eq_lock(struct bcm_cfg80211 *cfg);
static void wl_init_event_handler(struct bcm_cfg80211 *cfg);
static struct wl_event_q *wl_deq_event(struct bcm_cfg80211 *cfg);
static s32 wl_enq_event(struct bcm_cfg80211 *cfg, struct net_device *ndev, u32 type,
	const wl_event_msg_t *msg, void *data);
static void wl_put_event(struct wl_event_q *e);
static s32 wl_notify_connect_status_ap(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *e, void *data);
static s32 wl_notify_connect_status(struct bcm_cfg80211 *cfg,
	bcm_struct_cfgdev *cfgdev, const wl_event_msg_t *e, void *data);
static s32 wl_notify_roaming_status(struct bcm_cfg80211 *cfg,
	bcm_struct_cfgdev *cfgdev, const wl_event_msg_t *e, void *data);
static s32 wl_notify_scan_status(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data);
static s32 wl_bss_connect_done(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *e, void *data, bool completed);
static s32 wl_bss_roaming_done(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *e, void *data);
static s32 wl_notify_mic_status(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data);
#ifdef BT_WIFI_HANDOVER
static s32 wl_notify_bt_wifi_handover_req(struct bcm_cfg80211 *cfg,
	bcm_struct_cfgdev *cfgdev, const wl_event_msg_t *e, void *data);
#endif /* BT_WIFI_HANDOVER */
#ifdef WL_SCHED_SCAN
static s32
wl_notify_sched_scan_results(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *e, void *data);
#endif /* WL_SCHED_SCAN */
#ifdef PNO_SUPPORT
static s32 wl_notify_pfn_status(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data);
#endif /* PNO_SUPPORT */
#ifdef GSCAN_SUPPORT
static s32 wl_notify_gscan_event(struct bcm_cfg80211 *wl, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data);
static s32 wl_handle_roam_exp_event(struct bcm_cfg80211 *wl, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data);
#endif /* GSCAN_SUPPORT */
#ifdef RSSI_MONITOR_SUPPORT
static s32 wl_handle_rssi_monitor_event(struct bcm_cfg80211 *wl, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data);
#endif /* RSSI_MONITOR_SUPPORT */
static s32 wl_notifier_change_state(struct bcm_cfg80211 *cfg, struct net_info *_net_info,
	enum wl_status state, bool set);
#ifdef CUSTOM_EVENT_PM_WAKE
static s32 wl_check_pmstatus(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data);
#endif	/* CUSTOM_EVENT_PM_WAKE */
#ifdef ENABLE_TEMP_THROTTLING
static s32 wl_check_rx_throttle_status(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data);
#endif /* ENABLE_TEMP_THROTTLING */
#if defined(DHD_LOSSLESS_ROAMING) || defined(DBG_PKT_MON)
static s32 wl_notify_roam_prep_status(struct bcm_cfg80211 *cfg,
	bcm_struct_cfgdev *cfgdev, const wl_event_msg_t *e, void *data);
#endif /* DHD_LOSSLESS_ROAMING || DBG_PKT_MON */
#ifdef DHD_LOSSLESS_ROAMING
static void wl_del_roam_timeout(struct bcm_cfg80211 *cfg);
#endif /* DHD_LOSSLESS_ROAMING */

#ifdef WLTDLS
static s32 wl_cfg80211_tdls_config(struct bcm_cfg80211 *cfg,
	enum wl_tdls_config state, bool tdls_mode);
static s32 wl_tdls_event_handler(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data);
#endif /* WLTDLS */
/*
 * register/deregister parent device
 */
static void wl_cfg80211_clear_parent_dev(void);
/*
 * ioctl utilites
 */

/*
 * cfg80211 set_wiphy_params utilities
 */
static s32 wl_set_frag(struct net_device *dev, u32 frag_threshold);
static s32 wl_set_rts(struct net_device *dev, u32 frag_threshold);
static s32 wl_set_retry(struct net_device *dev, u32 retry, bool l);

/*
 * cfg profile utilities
 */
static s32 wl_update_prof(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *e, const void *data, s32 item);
static void *wl_read_prof(struct bcm_cfg80211 *cfg, struct net_device *ndev, s32 item);
static void wl_init_prof(struct bcm_cfg80211 *cfg, struct net_device *ndev);

/*
 * cfg80211 connect utilites
 */
static s32 wl_set_wpa_version(struct net_device *dev,
	struct cfg80211_connect_params *sme);
static s32 wl_set_auth_type(struct net_device *dev,
	struct cfg80211_connect_params *sme);
static s32 wl_set_set_cipher(struct net_device *dev,
	struct cfg80211_connect_params *sme);
static s32 wl_set_key_mgmt(struct net_device *dev,
	struct cfg80211_connect_params *sme);
#ifdef BCMWAPI_WPI
static s32 wl_set_set_wapi_ie(struct net_device *dev,
        struct cfg80211_connect_params *sme);
#endif
static s32 wl_set_set_sharedkey(struct net_device *dev,
	struct cfg80211_connect_params *sme);
static s32 wl_get_assoc_ies(struct bcm_cfg80211 *cfg, struct net_device *ndev);
static s32 wl_ch_to_chanspec(struct net_device *dev, int ch,
	struct wl_join_params *join_params, size_t *join_params_size);
void wl_cfg80211_clear_security(struct bcm_cfg80211 *cfg);

/*
 * information element utilities
 */
static void wl_rst_ie(struct bcm_cfg80211 *cfg);
static __used s32 wl_add_ie(struct bcm_cfg80211 *cfg, u8 t, u8 l, u8 *v);
static void wl_update_hidden_ap_ie(struct wl_bss_info *bi, const u8 *ie_stream, u32 *ie_size,
	bool roam);
static s32 wl_mrg_ie(struct bcm_cfg80211 *cfg, u8 *ie_stream, u16 ie_size);
static s32 wl_cp_ie(struct bcm_cfg80211 *cfg, u8 *dst, u16 dst_size);
static u32 wl_get_ielen(struct bcm_cfg80211 *cfg);
#ifdef MFP
static int wl_cfg80211_get_rsn_capa(bcm_tlv_t *wpa2ie, u8** rsn_cap);
#endif

#ifdef WL11U
static bcm_tlv_t *
wl_cfg80211_find_interworking_ie(const u8 *parse, u32 len);
static s32
wl_cfg80211_clear_iw_ie(struct bcm_cfg80211 *cfg, struct net_device *ndev, s32 bssidx);
static s32
wl_cfg80211_add_iw_ie(struct bcm_cfg80211 *cfg, struct net_device *ndev, s32 bssidx, s32 pktflag,
            uint8 ie_id, uint8 *data, uint8 data_len);
#endif /* WL11U */

static s32 wl_setup_wiphy(struct wireless_dev *wdev, struct device *dev, dhd_pub_t *data);
static void wl_free_wdev(struct bcm_cfg80211 *cfg);
#ifdef CONFIG_CFG80211_INTERNAL_REGDB
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 10, 11))
static int
#else
static void
#endif /* kernel version < 3.10.11 */
wl_cfg80211_reg_notifier(struct wiphy *wiphy, struct regulatory_request *request);
#endif /* CONFIG_CFG80211_INTERNAL_REGDB */

static s32 wl_inform_bss(struct bcm_cfg80211 *cfg);
static s32 wl_inform_single_bss(struct bcm_cfg80211 *cfg, struct wl_bss_info *bi, bool roam);
static s32 wl_update_bss_info(struct bcm_cfg80211 *cfg, struct net_device *ndev, bool roam);
static chanspec_t wl_cfg80211_get_shared_freq(struct wiphy *wiphy);
s32 wl_cfg80211_channel_to_freq(u32 channel);


static void wl_cfg80211_work_handler(struct work_struct *work);
static s32 wl_add_keyext(struct wiphy *wiphy, struct net_device *dev,
	u8 key_idx, const u8 *mac_addr,
	struct key_params *params);
/*
 * key indianess swap utilities
 */
static void swap_key_from_BE(struct wl_wsec_key *key);
static void swap_key_to_BE(struct wl_wsec_key *key);

/*
 * bcm_cfg80211 memory init/deinit utilities
 */
static s32 wl_init_priv_mem(struct bcm_cfg80211 *cfg);
static void wl_deinit_priv_mem(struct bcm_cfg80211 *cfg);

static void wl_delay(u32 ms);

/*
 * ibss mode utilities
 */
static bool wl_is_ibssmode(struct bcm_cfg80211 *cfg, struct net_device *ndev);
static __used bool wl_is_ibssstarter(struct bcm_cfg80211 *cfg);

/*
 * link up/down , default configuration utilities
 */
static s32 __wl_cfg80211_up(struct bcm_cfg80211 *cfg);
static s32 __wl_cfg80211_down(struct bcm_cfg80211 *cfg);

#ifdef WL_LASTEVT
static bool wl_is_linkdown(struct bcm_cfg80211 *cfg, const wl_event_msg_t *e, void *data);
#define WL_IS_LINKDOWN(cfg, e, data) wl_is_linkdown(cfg, e, data)
#else
static bool wl_is_linkdown(struct bcm_cfg80211 *cfg, const wl_event_msg_t *e);
#define WL_IS_LINKDOWN(cfg, e, data) wl_is_linkdown(cfg, e)
#endif /* WL_LASTEVT */

static bool wl_is_linkup(struct bcm_cfg80211 *cfg, const wl_event_msg_t *e,
	struct net_device *ndev);
static bool wl_is_nonetwork(struct bcm_cfg80211 *cfg, const wl_event_msg_t *e);
static void wl_link_up(struct bcm_cfg80211 *cfg);
static void wl_link_down(struct bcm_cfg80211 *cfg);
static s32 wl_config_ifmode(struct bcm_cfg80211 *cfg, struct net_device *ndev, s32 iftype);
static void wl_init_conf(struct wl_conf *conf);
static s32 wl_cfg80211_handle_ifdel(struct bcm_cfg80211 *cfg, wl_if_event_info *if_event_info,
	struct net_device* ndev);
int wl_cfg80211_get_ioctl_version(void);

/*
 * find most significant bit set
 */
static __used u32 wl_find_msb(u16 bit16);

/*
 * rfkill support
 */
static int wl_setup_rfkill(struct bcm_cfg80211 *cfg, bool setup);
static int wl_rfkill_set(void *data, bool blocked);
#ifdef DEBUGFS_CFG80211
static s32 wl_setup_debugfs(struct bcm_cfg80211 *cfg);
static s32 wl_free_debugfs(struct bcm_cfg80211 *cfg);
#endif
static wl_scan_params_t *wl_cfg80211_scan_alloc_params(struct bcm_cfg80211 *cfg,
	int channel, int nprobes, int *out_params_size);
static bool check_dev_role_integrity(struct bcm_cfg80211 *cfg, u32 dev_role);

#ifdef WL_CFG80211_ACL
/* ACL */
static int wl_cfg80211_set_mac_acl(struct wiphy *wiphy, struct net_device *cfgdev,
	const struct cfg80211_acl_data *acl);
#endif /* WL_CFG80211_ACL */

/*
 * Some external functions, TODO: move them to dhd_linux.h
 */
int dhd_add_monitor(const char *name, struct net_device **new_ndev);
int dhd_del_monitor(struct net_device *ndev);
int dhd_monitor_init(void *dhd_pub);
int dhd_monitor_uninit(void);
int dhd_start_xmit(struct sk_buff *skb, struct net_device *net);


#ifdef P2P_LISTEN_OFFLOADING
s32 wl_cfg80211_p2plo_deinit(struct bcm_cfg80211 *cfg);
#endif /* P2P_LISTEN_OFFLOADING */

#ifdef PKT_FILTER_SUPPORT
extern uint dhd_pkt_filter_enable;
extern uint dhd_master_mode;
extern void dhd_pktfilter_offload_enable(dhd_pub_t * dhd, char *arg, int enable, int master_mode);
#endif /* PKT_FILTER_SUPPORT */

#ifdef SUPPORT_SET_CAC
static void wl_cfg80211_set_cac(struct bcm_cfg80211 *cfg, int enable);
#endif /* SUPPORT_SET_CAC */

static int wl_cfg80211_delayed_roam(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const struct ether_addr *bssid);
static s32 __wl_update_wiphybands(struct bcm_cfg80211 *cfg, bool notify);

static int bw2cap[] = { 0, 0, WLC_BW_CAP_20MHZ, WLC_BW_CAP_40MHZ, WLC_BW_CAP_80MHZ,
	WLC_BW_CAP_160MHZ, WLC_BW_CAP_160MHZ };

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 2, 0)) || (defined(CONFIG_ARCH_MSM) && \
	defined(CFG80211_DISCONNECTED_V2))
#define CFG80211_DISCONNECTED(dev, reason, ie, len, loc_gen, gfp) \
	cfg80211_disconnected(dev, reason, ie, len, loc_gen, gfp);
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(4, 2, 0))
#define CFG80211_DISCONNECTED(dev, reason, ie, len, loc_gen, gfp) \
	cfg80211_disconnected(dev, reason, ie, len, gfp);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 2, 0)) */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)) || (defined(CONFIG_ARCH_MSM) && \
	defined(CFG80211_DISCONNECTED_V2))
#define CFG80211_GET_BSS(wiphy, channel, bssid, ssid, ssid_len) \
	cfg80211_get_bss(wiphy, channel, bssid, ssid, ssid_len,	\
			IEEE80211_BSS_TYPE_ESS, IEEE80211_PRIVACY_ANY);
#else
#define CFG80211_GET_BSS(wiphy, channel, bssid, ssid, ssid_len) \
	cfg80211_get_bss(wiphy, channel, bssid, ssid, ssid_len,	\
			WLAN_CAPABILITY_ESS, WLAN_CAPABILITY_ESS);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)) */

#define IS_WPA_AKM(akm) ((akm) == RSN_AKM_NONE || 			\
				 (akm) == RSN_AKM_UNSPECIFIED || 	\
				 (akm) == RSN_AKM_PSK)


extern int dhd_wait_pend8021x(struct net_device *dev);
#ifdef PROP_TXSTATUS_VSDB
extern int disable_proptx;
#endif /* PROP_TXSTATUS_VSDB */
static int wl_cfg80211_check_in4way(struct bcm_cfg80211 *cfg,
	struct net_device *dev, uint action, enum wl_ext_status status, void *context);


extern int passive_channel_skip;

static s32
wl_ap_start_ind(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data);
static s32
wl_csa_complete_ind(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data);
#if ((LINUX_VERSION_CODE >= KERNEL_VERSION (3, 5, 0)) && (LINUX_VERSION_CODE <= (3, 7, \
	0)))
struct chan_info {
	int freq;
	int chan_type;
};
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0))
#define CFG80211_PUT_BSS(wiphy, bss) cfg80211_put_bss(wiphy, bss);
#else
#define CFG80211_PUT_BSS(wiphy, bss) cfg80211_put_bss(bss);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0) */

#define CHAN2G(_channel, _freq, _flags) {			\
	.band			= IEEE80211_BAND_2GHZ,		\
	.center_freq		= (_freq),			\
	.hw_value		= (_channel),			\
	.flags			= (_flags),			\
	.max_antenna_gain	= 0,				\
	.max_power		= 30,				\
}

#define CHAN5G(_channel, _flags) {				\
	.band			= IEEE80211_BAND_5GHZ,		\
	.center_freq		= 5000 + (5 * (_channel)),	\
	.hw_value		= (_channel),			\
	.flags			= (_flags),			\
	.max_antenna_gain	= 0,				\
	.max_power		= 30,				\
}

#define RATE_TO_BASE100KBPS(rate)   (((rate) * 10) / 2)
#define RATETAB_ENT(_rateid, _flags) \
	{								\
		.bitrate	= RATE_TO_BASE100KBPS(_rateid),     \
		.hw_value	= (_rateid),			    \
		.flags	  = (_flags),			     \
	}

static struct ieee80211_rate __wl_rates[] = {
	RATETAB_ENT(DOT11_RATE_1M, 0),
	RATETAB_ENT(DOT11_RATE_2M, IEEE80211_RATE_SHORT_PREAMBLE),
	RATETAB_ENT(DOT11_RATE_5M5, IEEE80211_RATE_SHORT_PREAMBLE),
	RATETAB_ENT(DOT11_RATE_11M, IEEE80211_RATE_SHORT_PREAMBLE),
	RATETAB_ENT(DOT11_RATE_6M, 0),
	RATETAB_ENT(DOT11_RATE_9M, 0),
	RATETAB_ENT(DOT11_RATE_12M, 0),
	RATETAB_ENT(DOT11_RATE_18M, 0),
	RATETAB_ENT(DOT11_RATE_24M, 0),
	RATETAB_ENT(DOT11_RATE_36M, 0),
	RATETAB_ENT(DOT11_RATE_48M, 0),
	RATETAB_ENT(DOT11_RATE_54M, 0)
};

#define wl_a_rates		(__wl_rates + 4)
#define wl_a_rates_size	8
#define wl_g_rates		(__wl_rates + 0)
#define wl_g_rates_size	12

static struct ieee80211_channel __wl_2ghz_channels[] = {
	CHAN2G(1, 2412, 0),
	CHAN2G(2, 2417, 0),
	CHAN2G(3, 2422, 0),
	CHAN2G(4, 2427, 0),
	CHAN2G(5, 2432, 0),
	CHAN2G(6, 2437, 0),
	CHAN2G(7, 2442, 0),
	CHAN2G(8, 2447, 0),
	CHAN2G(9, 2452, 0),
	CHAN2G(10, 2457, 0),
	CHAN2G(11, 2462, 0),
	CHAN2G(12, 2467, 0),
	CHAN2G(13, 2472, 0),
	CHAN2G(14, 2484, 0)
};

static struct ieee80211_channel __wl_5ghz_a_channels[] = {
	CHAN5G(34, 0), CHAN5G(36, 0),
	CHAN5G(38, 0), CHAN5G(40, 0),
	CHAN5G(42, 0), CHAN5G(44, 0),
	CHAN5G(46, 0), CHAN5G(48, 0),
	CHAN5G(52, 0), CHAN5G(56, 0),
	CHAN5G(60, 0), CHAN5G(64, 0),
	CHAN5G(100, 0), CHAN5G(104, 0),
	CHAN5G(108, 0), CHAN5G(112, 0),
	CHAN5G(116, 0), CHAN5G(120, 0),
	CHAN5G(124, 0), CHAN5G(128, 0),
	CHAN5G(132, 0), CHAN5G(136, 0),
	CHAN5G(140, 0), CHAN5G(144, 0),
	CHAN5G(149, 0), CHAN5G(153, 0),
	CHAN5G(157, 0), CHAN5G(161, 0),
	CHAN5G(165, 0)
};

static struct ieee80211_supported_band __wl_band_2ghz = {
	.band = IEEE80211_BAND_2GHZ,
	.channels = __wl_2ghz_channels,
	.n_channels = ARRAY_SIZE(__wl_2ghz_channels),
	.bitrates = wl_g_rates,
	.n_bitrates = wl_g_rates_size
};

static struct ieee80211_supported_band __wl_band_5ghz_a = {
	.band = IEEE80211_BAND_5GHZ,
	.channels = __wl_5ghz_a_channels,
	.n_channels = ARRAY_SIZE(__wl_5ghz_a_channels),
	.bitrates = wl_a_rates,
	.n_bitrates = wl_a_rates_size
};

static const u32 __wl_cipher_suites[] = {
	WLAN_CIPHER_SUITE_WEP40,
	WLAN_CIPHER_SUITE_WEP104,
	WLAN_CIPHER_SUITE_TKIP,
	WLAN_CIPHER_SUITE_CCMP,
#ifdef MFP
	/*
	 * Advertising AES_CMAC cipher suite to userspace would imply that we
	 * are supporting MFP. So advertise only when MFP support is enabled.
	 */
	WLAN_CIPHER_SUITE_AES_CMAC,
#endif /* MFP */
#ifdef BCMWAPI_WPI
	WLAN_CIPHER_SUITE_SMS4,
#endif
#if defined(WLAN_CIPHER_SUITE_PMK)
	WLAN_CIPHER_SUITE_PMK,
#endif
};

#ifdef WL_SUPPORT_ACS
/*
 * The firmware code required for this feature to work is currently under
 * BCMINTERNAL flag. In future if this is to enabled we need to bring the
 * required firmware code out of the BCMINTERNAL flag.
 */
struct wl_dump_survey {
	u32 obss;
	u32 ibss;
	u32 no_ctg;
	u32 no_pckt;
	u32 tx;
	u32 idle;
};
#endif /* WL_SUPPORT_ACS */


#if defined(USE_DYNAMIC_MAXPKT_RXGLOM)
static int maxrxpktglom = 0;
#endif

/* IOCtl version read from targeted driver */
int ioctl_version;
#ifdef DEBUGFS_CFG80211
#define S_SUBLOGLEVEL 20
static const struct {
	u32 log_level;
	char *sublogname;
} sublogname_map[] = {
	{WL_DBG_ERR, "ERR"},
	{WL_DBG_INFO, "INFO"},
	{WL_DBG_DBG, "DBG"},
	{WL_DBG_SCAN, "SCAN"},
	{WL_DBG_TRACE, "TRACE"},
	{WL_DBG_P2P_ACTION, "P2PACTION"}
};
#endif

#ifdef CUSTOMER_HW4_DEBUG
uint prev_dhd_console_ms = 0;
u32 prev_wl_dbg_level = 0;
bool wl_scan_timeout_dbg_enabled = 0;
static void wl_scan_timeout_dbg_set(void);
static void wl_scan_timeout_dbg_clear(void);

static void wl_scan_timeout_dbg_set(void)
{
	WL_ERR(("Enter \n"));
	prev_dhd_console_ms = dhd_console_ms;
	prev_wl_dbg_level = wl_dbg_level;

	dhd_console_ms = 1;
	wl_dbg_level |= (WL_DBG_ERR | WL_DBG_P2P_ACTION | WL_DBG_SCAN);

	wl_scan_timeout_dbg_enabled = 1;
}
static void wl_scan_timeout_dbg_clear(void)
{
	WL_ERR(("Enter \n"));
	dhd_console_ms = prev_dhd_console_ms;
	wl_dbg_level = prev_wl_dbg_level;

	wl_scan_timeout_dbg_enabled = 0;
}
#endif /* CUSTOMER_HW4_DEBUG */

/* watchdog timer for disconnecting when fw is not associated for FW_ASSOC_WATCHDOG_TIME ms */
uint32 fw_assoc_watchdog_ms = 0;
bool fw_assoc_watchdog_started = 0;
#define FW_ASSOC_WATCHDOG_TIME 10 * 1000 /* msec */

static void wl_add_remove_pm_enable_work(struct bcm_cfg80211 *cfg,
	enum wl_pm_workq_act_type type)
{
	u16 wq_duration = 0;
	dhd_pub_t *dhd =  NULL;

	if (cfg == NULL)
		return;

	dhd = (dhd_pub_t *)(cfg->pub);

	mutex_lock(&cfg->pm_sync);
	/*
	 * Make cancel and schedule work part mutually exclusive
	 * so that while cancelling, we are sure that there is no
	 * work getting scheduled.
	 */
	if (delayed_work_pending(&cfg->pm_enable_work)) {
		cancel_delayed_work_sync(&cfg->pm_enable_work);
		DHD_PM_WAKE_UNLOCK(cfg->pub);
	}

	if (type == WL_PM_WORKQ_SHORT) {
		wq_duration = WL_PM_ENABLE_TIMEOUT;
	} else if (type == WL_PM_WORKQ_LONG) {
		wq_duration = (WL_PM_ENABLE_TIMEOUT*2);
	}

	/* It should schedule work item only if driver is up */
	if (wq_duration && dhd->up) {
		if (schedule_delayed_work(&cfg->pm_enable_work,
				msecs_to_jiffies((const unsigned int)wq_duration))) {
			DHD_PM_WAKE_LOCK_TIMEOUT(cfg->pub, wq_duration);
		} else {
			WL_ERR(("Can't schedule pm work handler\n"));
		}
	}
	mutex_unlock(&cfg->pm_sync);
}

/* Return a new chanspec given a legacy chanspec
 * Returns INVCHANSPEC on error
 */
chanspec_t
wl_chspec_from_legacy(chanspec_t legacy_chspec)
{
	chanspec_t chspec;

	/* get the channel number */
	chspec = LCHSPEC_CHANNEL(legacy_chspec);

	/* convert the band */
	if (LCHSPEC_IS2G(legacy_chspec)) {
		chspec |= WL_CHANSPEC_BAND_2G;
	} else {
		chspec |= WL_CHANSPEC_BAND_5G;
	}

	/* convert the bw and sideband */
	if (LCHSPEC_IS20(legacy_chspec)) {
		chspec |= WL_CHANSPEC_BW_20;
	} else {
		chspec |= WL_CHANSPEC_BW_40;
		if (LCHSPEC_CTL_SB(legacy_chspec) == WL_LCHANSPEC_CTL_SB_LOWER) {
			chspec |= WL_CHANSPEC_CTL_SB_L;
		} else {
			chspec |= WL_CHANSPEC_CTL_SB_U;
		}
	}

	if (wf_chspec_malformed(chspec)) {
		WL_ERR(("wl_chspec_from_legacy: output chanspec (0x%04X) malformed\n",
		        chspec));
		return INVCHANSPEC;
	}

	return chspec;
}

/* Return a legacy chanspec given a new chanspec
 * Returns INVCHANSPEC on error
 */
static chanspec_t
wl_chspec_to_legacy(chanspec_t chspec)
{
	chanspec_t lchspec;

	if (wf_chspec_malformed(chspec)) {
		WL_ERR(("wl_chspec_to_legacy: input chanspec (0x%04X) malformed\n",
		        chspec));
		return INVCHANSPEC;
	}

	/* get the channel number */
	lchspec = CHSPEC_CHANNEL(chspec);

	/* convert the band */
	if (CHSPEC_IS2G(chspec)) {
		lchspec |= WL_LCHANSPEC_BAND_2G;
	} else {
		lchspec |= WL_LCHANSPEC_BAND_5G;
	}

	/* convert the bw and sideband */
	if (CHSPEC_IS20(chspec)) {
		lchspec |= WL_LCHANSPEC_BW_20;
		lchspec |= WL_LCHANSPEC_CTL_SB_NONE;
	} else if (CHSPEC_IS40(chspec)) {
		lchspec |= WL_LCHANSPEC_BW_40;
		if (CHSPEC_CTL_SB(chspec) == WL_CHANSPEC_CTL_SB_L) {
			lchspec |= WL_LCHANSPEC_CTL_SB_LOWER;
		} else {
			lchspec |= WL_LCHANSPEC_CTL_SB_UPPER;
		}
	} else {
		/* cannot express the bandwidth */
		char chanbuf[CHANSPEC_STR_LEN];
		WL_ERR((
		        "wl_chspec_to_legacy: unable to convert chanspec %s (0x%04X) "
		        "to pre-11ac format\n",
		        wf_chspec_ntoa(chspec, chanbuf), chspec));
		return INVCHANSPEC;
	}

	return lchspec;
}

/* given a chanspec value, do the endian and chanspec version conversion to
 * a chanspec_t value
 * Returns INVCHANSPEC on error
 */
chanspec_t
wl_chspec_host_to_driver(chanspec_t chanspec)
{
	if (ioctl_version == 1) {
		chanspec = wl_chspec_to_legacy(chanspec);
		if (chanspec == INVCHANSPEC) {
			return chanspec;
		}
	}
	chanspec = htodchanspec(chanspec);

	return chanspec;
}

/* given a channel value, do the endian and chanspec version conversion to
 * a chanspec_t value
 * Returns INVCHANSPEC on error
 */
chanspec_t
wl_ch_host_to_driver(struct bcm_cfg80211 *cfg, s32 bssidx, u16 channel)
{
	chanspec_t chanspec;

	chanspec = channel & WL_CHANSPEC_CHAN_MASK;

	if (channel <= CH_MAX_2G_CHANNEL)
		chanspec |= WL_CHANSPEC_BAND_2G;
	else
		chanspec |= WL_CHANSPEC_BAND_5G;

	chanspec |= wl_cfg80211_ulb_get_min_bw_chspec(cfg, NULL, bssidx);

	chanspec |= WL_CHANSPEC_CTL_SB_NONE;

	return wl_chspec_host_to_driver(chanspec);
}

/* given a chanspec value from the driver, do the endian and chanspec version conversion to
 * a chanspec_t value
 * Returns INVCHANSPEC on error
 */
chanspec_t
wl_chspec_driver_to_host(chanspec_t chanspec)
{
	chanspec = dtohchanspec(chanspec);
	if (ioctl_version == 1) {
		chanspec = wl_chspec_from_legacy(chanspec);
	}

	return chanspec;
}

/*
 * convert ASCII string to MAC address (colon-delimited format)
 * eg: 00:11:22:33:44:55
 */
int
wl_cfg80211_ether_atoe(const char *a, struct ether_addr *n)
{
	char *c = NULL;
	int count = 0;

	memset(n, 0, ETHER_ADDR_LEN);
	for (;;) {
		n->octet[count++] = (uint8)simple_strtoul(a, &c, 16);
		if (!*c++ || count == ETHER_ADDR_LEN)
			break;
		a = c;
	}
	return (count == ETHER_ADDR_LEN);
}

/* There isn't a lot of sense in it, but you can transmit anything you like */
static const struct ieee80211_txrx_stypes
wl_cfg80211_default_mgmt_stypes[NUM_NL80211_IFTYPES] = {
#ifdef WLMESH_CFG80211
	[NL80211_IFTYPE_MESH_POINT] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
		BIT(IEEE80211_STYPE_AUTH >> 4)
	},
#endif /* WLMESH_CFG80211 */
	[NL80211_IFTYPE_ADHOC] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4)
	},
	[NL80211_IFTYPE_STATION] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
		BIT(IEEE80211_STYPE_PROBE_REQ >> 4)
	},
	[NL80211_IFTYPE_AP] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
		BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
		BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
		BIT(IEEE80211_STYPE_DISASSOC >> 4) |
		BIT(IEEE80211_STYPE_AUTH >> 4) |
		BIT(IEEE80211_STYPE_DEAUTH >> 4) |
		BIT(IEEE80211_STYPE_ACTION >> 4)
	},
	[NL80211_IFTYPE_AP_VLAN] = {
		/* copy AP */
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
		BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
		BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
		BIT(IEEE80211_STYPE_DISASSOC >> 4) |
		BIT(IEEE80211_STYPE_AUTH >> 4) |
		BIT(IEEE80211_STYPE_DEAUTH >> 4) |
		BIT(IEEE80211_STYPE_ACTION >> 4)
	},
	[NL80211_IFTYPE_P2P_CLIENT] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
		BIT(IEEE80211_STYPE_PROBE_REQ >> 4)
	},
	[NL80211_IFTYPE_P2P_GO] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
		BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
		BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
		BIT(IEEE80211_STYPE_DISASSOC >> 4) |
		BIT(IEEE80211_STYPE_AUTH >> 4) |
		BIT(IEEE80211_STYPE_DEAUTH >> 4) |
		BIT(IEEE80211_STYPE_ACTION >> 4)
	},
#if defined(WL_CFG80211_P2P_DEV_IF)
	[NL80211_IFTYPE_P2P_DEVICE] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
		BIT(IEEE80211_STYPE_PROBE_REQ >> 4)
	},
#endif /* WL_CFG80211_P2P_DEV_IF */
};

static void swap_key_from_BE(struct wl_wsec_key *key)
{
	key->index = htod32(key->index);
	key->len = htod32(key->len);
	key->algo = htod32(key->algo);
	key->flags = htod32(key->flags);
	key->rxiv.hi = htod32(key->rxiv.hi);
	key->rxiv.lo = htod16(key->rxiv.lo);
	key->iv_initialized = htod32(key->iv_initialized);
}

static void swap_key_to_BE(struct wl_wsec_key *key)
{
	key->index = dtoh32(key->index);
	key->len = dtoh32(key->len);
	key->algo = dtoh32(key->algo);
	key->flags = dtoh32(key->flags);
	key->rxiv.hi = dtoh32(key->rxiv.hi);
	key->rxiv.lo = dtoh16(key->rxiv.lo);
	key->iv_initialized = dtoh32(key->iv_initialized);
}

/* Dump the contents of the encoded wps ie buffer and get pbc value */
static void
wl_validate_wps_ie(char *wps_ie, s32 wps_ie_len, bool *pbc)
{
	#define WPS_IE_FIXED_LEN 6
	s16 len;
	u8 *subel = NULL;
	u16 subelt_id;
	u16 subelt_len;
	u16 val;
	u8 *valptr = (uint8*) &val;
	if (wps_ie == NULL || wps_ie_len < WPS_IE_FIXED_LEN) {
		WL_ERR(("invalid argument : NULL\n"));
		return;
	}
	len = (s16)wps_ie[TLV_LEN_OFF];

	if (len > wps_ie_len) {
		WL_ERR(("invalid length len %d, wps ie len %d\n", len, wps_ie_len));
		return;
	}
	WL_DBG(("wps_ie len=%d\n", len));
	len -= 4;	/* for the WPS IE's OUI, oui_type fields */
	subel = wps_ie + WPS_IE_FIXED_LEN;
	while (len >= 4) {		/* must have attr id, attr len fields */
		valptr[0] = *subel++;
		valptr[1] = *subel++;
		subelt_id = HTON16(val);

		valptr[0] = *subel++;
		valptr[1] = *subel++;
		subelt_len = HTON16(val);

		len -= 4;			/* for the attr id, attr len fields */
		len -= (s16)subelt_len;	/* for the remaining fields in this attribute */
		if (len < 0) {
			break;
		}
		WL_DBG((" subel=%p, subelt_id=0x%x subelt_len=%u\n",
			subel, subelt_id, subelt_len));

		if (subelt_id == WPS_ID_VERSION) {
			WL_DBG(("  attr WPS_ID_VERSION: %u\n", *subel));
		} else if (subelt_id == WPS_ID_REQ_TYPE) {
			WL_DBG(("  attr WPS_ID_REQ_TYPE: %u\n", *subel));
		} else if (subelt_id == WPS_ID_CONFIG_METHODS) {
			valptr[0] = *subel;
			valptr[1] = *(subel + 1);
			WL_DBG(("  attr WPS_ID_CONFIG_METHODS: %x\n", HTON16(val)));
		} else if (subelt_id == WPS_ID_DEVICE_NAME) {
			char devname[100];
			int namelen = MIN(subelt_len, (sizeof(devname) - 1));

			if (namelen) {
				memcpy(devname, subel, namelen);
				devname[namelen] = '\0';
				/* Printing len as rx'ed in the IE */
				WL_DBG(("  attr WPS_ID_DEVICE_NAME: %s (len %u)\n",
					devname, subelt_len));
			}
		} else if (subelt_id == WPS_ID_DEVICE_PWD_ID) {
			valptr[0] = *subel;
			valptr[1] = *(subel + 1);
			WL_DBG(("  attr WPS_ID_DEVICE_PWD_ID: %u\n", HTON16(val)));
			*pbc = (HTON16(val) == DEV_PW_PUSHBUTTON) ? true : false;
		} else if (subelt_id == WPS_ID_PRIM_DEV_TYPE) {
			valptr[0] = *subel;
			valptr[1] = *(subel + 1);
			WL_DBG(("  attr WPS_ID_PRIM_DEV_TYPE: cat=%u \n", HTON16(val)));
			valptr[0] = *(subel + 6);
			valptr[1] = *(subel + 7);
			WL_DBG(("  attr WPS_ID_PRIM_DEV_TYPE: subcat=%u\n", HTON16(val)));
		} else if (subelt_id == WPS_ID_REQ_DEV_TYPE) {
			valptr[0] = *subel;
			valptr[1] = *(subel + 1);
			WL_DBG(("  attr WPS_ID_REQ_DEV_TYPE: cat=%u\n", HTON16(val)));
			valptr[0] = *(subel + 6);
			valptr[1] = *(subel + 7);
			WL_DBG(("  attr WPS_ID_REQ_DEV_TYPE: subcat=%u\n", HTON16(val)));
		} else if (subelt_id == WPS_ID_SELECTED_REGISTRAR_CONFIG_METHODS) {
			valptr[0] = *subel;
			valptr[1] = *(subel + 1);
			WL_DBG(("  attr WPS_ID_SELECTED_REGISTRAR_CONFIG_METHODS"
				": cat=%u\n", HTON16(val)));
		} else {
			WL_DBG(("  unknown attr 0x%x\n", subelt_id));
		}

		subel += subelt_len;
	}
}

s32 wl_set_tx_power(struct net_device *dev,
	enum nl80211_tx_power_setting type, s32 dbm)
{
	s32 err = 0;
	s32 disable = 0;
	s32 txpwrqdbm;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	/* Make sure radio is off or on as far as software is concerned */
	disable = WL_RADIO_SW_DISABLE << 16;
	disable = htod32(disable);
	err = wldev_ioctl_set(dev, WLC_SET_RADIO, &disable, sizeof(disable));
	if (unlikely(err)) {
		WL_ERR(("WLC_SET_RADIO error (%d)\n", err));
		return err;
	}

	if (dbm > 0xffff)
		dbm = 0xffff;
	txpwrqdbm = dbm * 4;
	err = wldev_iovar_setbuf_bsscfg(dev, "qtxpower", (void *)&txpwrqdbm,
		sizeof(txpwrqdbm), cfg->ioctl_buf, WLC_IOCTL_SMLEN, 0,
		&cfg->ioctl_buf_sync);
	if (unlikely(err))
		WL_ERR(("qtxpower error (%d)\n", err));
	else
		WL_ERR(("dBm=%d, txpwrqdbm=0x%x\n", dbm, txpwrqdbm));

	return err;
}

s32 wl_get_tx_power(struct net_device *dev, s32 *dbm)
{
	s32 err = 0;
	s32 txpwrdbm;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	err = wldev_iovar_getbuf_bsscfg(dev, "qtxpower",
		NULL, 0, cfg->ioctl_buf, WLC_IOCTL_SMLEN, 0, &cfg->ioctl_buf_sync);
	if (unlikely(err)) {
		WL_ERR(("error (%d)\n", err));
		return err;
	}

	memcpy(&txpwrdbm, cfg->ioctl_buf, sizeof(txpwrdbm));
	txpwrdbm = dtoh32(txpwrdbm);
	*dbm = (txpwrdbm & ~WL_TXPWR_OVERRIDE) / 4;

	WL_INFORM(("dBm=%d, txpwrdbm=0x%x\n", *dbm, txpwrdbm));

	return err;
}

static chanspec_t wl_cfg80211_get_shared_freq(struct wiphy *wiphy)
{
	chanspec_t chspec;
	int cur_band, err = 0;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct net_device *dev = bcmcfg_to_prmry_ndev(cfg);
	struct ether_addr bssid;
	struct wl_bss_info *bss = NULL;
	s32 bssidx = 0; /* Explicitly set to primary bssidx */
	char *buf;

	memset(&bssid, 0, sizeof(bssid));
	if ((err = wldev_ioctl_get(dev, WLC_GET_BSSID, &bssid, sizeof(bssid)))) {
		/* STA interface is not associated. So start the new interface on a temp
		 * channel . Later proper channel will be applied by the above framework
		 * via set_channel (cfg80211 API).
		 */
		WL_DBG(("Not associated. Return a temp channel. \n"));
		cur_band = 0;
		err = wldev_ioctl_get(dev, WLC_GET_BAND, &cur_band, sizeof(int));
		if (unlikely(err)) {
			WL_ERR(("Get band failed\n"));
			return wl_ch_host_to_driver(cfg, bssidx, WL_P2P_TEMP_CHAN);
		}
		if (cur_band == WLC_BAND_5G)
			return wl_ch_host_to_driver(cfg, bssidx, WL_P2P_TEMP_CHAN_5G);
		else
			return wl_ch_host_to_driver(cfg, bssidx, WL_P2P_TEMP_CHAN);
	}

	buf = kzalloc(WL_EXTRA_BUF_MAX, GFP_KERNEL);
	if (!buf) {
		WL_ERR(("buf alloc failed. use temp channel\n"));
		return wl_ch_host_to_driver(cfg, bssidx, WL_P2P_TEMP_CHAN);
	}

	*(u32 *)buf = htod32(WL_EXTRA_BUF_MAX);
	if ((err = wldev_ioctl_get(dev, WLC_GET_BSS_INFO, buf,
		WL_EXTRA_BUF_MAX))) {
			WL_ERR(("Failed to get associated bss info, use temp channel \n"));
			chspec = wl_ch_host_to_driver(cfg, bssidx, WL_P2P_TEMP_CHAN);
	}
	else {
			bss = (struct wl_bss_info *) (buf + 4);
			chspec =  bss->chanspec;

			WL_DBG(("Valid BSS Found. chanspec:%d \n", chspec));
	}

	kfree(buf);
	return chspec;
}

static bcm_struct_cfgdev *
wl_cfg80211_add_monitor_if(const char *name)
{
#if defined(WL_ENABLE_P2P_IF) || defined(WL_CFG80211_P2P_DEV_IF)
	WL_INFORM(("wl_cfg80211_add_monitor_if: No more support monitor interface\n"));
	return ERR_PTR(-EOPNOTSUPP);
#else
	struct net_device* ndev = NULL;

	dhd_add_monitor(name, &ndev);
	WL_INFORM(("wl_cfg80211_add_monitor_if net device returned: 0x%p\n", ndev));
	return ndev_to_cfgdev(ndev);
#endif /* WL_ENABLE_P2P_IF || WL_CFG80211_P2P_DEV_IF */
}

static bcm_struct_cfgdev *
wl_cfg80211_add_virtual_iface(struct wiphy *wiphy,
#if defined(WL_CFG80211_P2P_DEV_IF)
	const char *name,
#else
	char *name,
#endif /* WL_CFG80211_P2P_DEV_IF */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
	unsigned char name_assign_type,
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)) */
	enum nl80211_iftype type,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0))
	u32 *flags,
#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0)) */
	struct vif_params *params)
{
	s32 err = -ENODEV;
	s32 timeout = -1;
	s32 wlif_type = -1;
	s32 mode = 0;
	s32 val = 0;
	s32 cfg_type;
	s32 dhd_mode = 0;
	chanspec_t chspec;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct net_device *primary_ndev;
	struct net_device *new_ndev;
	struct ether_addr primary_mac;
	bcm_struct_cfgdev *new_cfgdev;
#ifdef PROP_TXSTATUS_VSDB
#if defined(BCMSDIO) || defined(BCMDBUS)
	s32 up = 1;
	bool enabled;
#endif /* BCMSDIO || BCMDBUS */
#endif /* PROP_TXSTATUS_VSDB */
	dhd_pub_t *dhd;
	bool hang_required = false;

	if (!cfg)
		return ERR_PTR(-EINVAL);

	dhd = (dhd_pub_t *)(cfg->pub);

	/* Use primary I/F for sending cmds down to firmware */
	primary_ndev = bcmcfg_to_prmry_ndev(cfg);

	if (unlikely(!wl_get_drv_status(cfg, READY, primary_ndev))) {
		WL_ERR(("device is not ready\n"));
		return ERR_PTR(-ENODEV);
	}

	if (!name) {
		WL_ERR(("Interface name not provided \n"));
		return ERR_PTR(-EINVAL);
	}

#ifdef WLTDLS
	/* disable TDLS if number of connected interfaces is >= 1 */
	wl_cfg80211_tdls_config(cfg, TDLS_STATE_IF_CREATE, false);
#endif /* WLTDLS */

	mutex_lock(&cfg->if_sync);
	WL_DBG(("if name: %s, type: %d\n", name, type));
	switch (type) {
	case NL80211_IFTYPE_ADHOC:
#ifdef WLAIBSS_MCHAN
		new_cfgdev = bcm_cfg80211_add_ibss_if(wiphy, (char *)name);
		mutex_unlock(&cfg->if_sync);
		return new_cfgdev;
#endif /* WLAIBSS_MCHAN */
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_WDS:
	case NL80211_IFTYPE_MESH_POINT:
		WL_ERR(("Unsupported interface type\n"));
		mode = WL_MODE_IBSS;
		err = -EINVAL;
		goto fail;
	case NL80211_IFTYPE_MONITOR:
		new_cfgdev = wl_cfg80211_add_monitor_if(name);
		mutex_unlock(&cfg->if_sync);
		return new_cfgdev;
#if defined(WL_CFG80211_P2P_DEV_IF)
	case NL80211_IFTYPE_P2P_DEVICE:
		cfg->down_disc_if = FALSE;
		new_cfgdev = wl_cfgp2p_add_p2p_disc_if(cfg);
		mutex_unlock(&cfg->if_sync);
		return new_cfgdev;
#endif /* WL_CFG80211_P2P_DEV_IF */
	case NL80211_IFTYPE_STATION:
#ifdef WL_VIRTUAL_APSTA
#ifdef WLAIBSS_MCHAN
		if (cfg->ibss_cfgdev) {
			WL_ERR(("AIBSS is already operational. "
					" AIBSS & DUALSTA can't be used together \n"));
			err = -ENOMEM;
			goto fail;
		}
#endif /* WLAIBSS_MCHAN */

		if (wl_cfgp2p_vif_created(cfg)) {
			WL_ERR(("Could not create new iface."
				"Already one p2p interface is running"));
			err = -ENOMEM;
			goto fail;
		}
		new_cfgdev = wl_cfg80211_create_iface(cfg->wdev->wiphy,
			NL80211_IFTYPE_STATION, NULL, name);
		if (!new_cfgdev) {
			err = -ENOMEM;
			goto fail;
		 } else {
			mutex_unlock(&cfg->if_sync);
			return new_cfgdev;
		}
#endif /* WL_VIRTUAL_APSTA */
	case NL80211_IFTYPE_P2P_CLIENT:
		wlif_type = WL_P2P_IF_CLIENT;
		mode = WL_MODE_BSS;
		break;
	case NL80211_IFTYPE_P2P_GO:
		wlif_type = WL_P2P_IF_GO;
		mode = WL_MODE_AP;
		break;
#ifdef WL_VIRTUAL_APSTA
	case NL80211_IFTYPE_AP:
		dhd->op_mode = DHD_FLAG_HOSTAP_MODE;
		new_cfgdev = wl_cfg80211_create_iface(cfg->wdev->wiphy,
			NL80211_IFTYPE_AP, NULL, name);
		if (!new_cfgdev) {
			err = -ENOMEM;
			goto fail;
		} else {
			mutex_unlock(&cfg->if_sync);
			return new_cfgdev;
		}
#endif /* WL_VIRTUAL_APSTA */
	default:
		WL_ERR(("Unsupported interface type\n"));
		err = -EINVAL;
		goto fail;
	}

	if (cfg->p2p_supported && (wlif_type != -1)) {
		ASSERT(cfg->p2p); /* ensure expectation of p2p initialization */

#ifdef PROP_TXSTATUS_VSDB
#if defined(BCMSDIO)
		if (!dhd) {
			err = -EINVAL;
			goto fail;
		}
#endif 
#endif /* PROP_TXSTATUS_VSDB */

		if (!cfg->p2p) {
			WL_ERR(("Failed to start p2p"));
			err = -ENODEV;
			goto fail;
		}

		if (cfg->p2p && !cfg->p2p->on && strstr(name, WL_P2P_INTERFACE_PREFIX)) {
			p2p_on(cfg) = true;
			wl_cfgp2p_set_firm_p2p(cfg);
			wl_cfgp2p_init_discovery(cfg);
			get_primary_mac(cfg, &primary_mac);
			wl_cfgp2p_generate_bss_mac(cfg, &primary_mac);
		}

		strncpy(cfg->p2p->vir_ifname, name, IFNAMSIZ - 1);
		cfg->p2p->vir_ifname[IFNAMSIZ - 1] = '\0';

		wl_cfg80211_scan_abort(cfg);
#ifdef PROP_TXSTATUS_VSDB
#if defined(BCMSDIO) || defined(BCMDBUS)
		if (!cfg->wlfc_on && !disable_proptx) {
			dhd_wlfc_get_enable(dhd, &enabled);
			if (!enabled && dhd->op_mode != DHD_FLAG_HOSTAP_MODE &&
				dhd->op_mode != DHD_FLAG_IBSS_MODE) {
				dhd_wlfc_init(dhd);
				err = wldev_ioctl_set(primary_ndev, WLC_UP, &up, sizeof(s32));
				if (err < 0)
					WL_ERR(("WLC_UP return err:%d\n", err));
			}
			cfg->wlfc_on = true;
		}
#endif /* BCMSDIO || BCMDBUS */
#endif /* PROP_TXSTATUS_VSDB */

		/* Dual p2p doesn't support multiple P2PGO interfaces,
		 * p2p_go_count is the counter for GO creation
		 * requests.
		 */
		if ((cfg->p2p->p2p_go_count > 0) && (type == NL80211_IFTYPE_P2P_GO)) {
			WL_ERR(("Fw does not support multiple Go\n"));
			err = -ENOTSUPP;
			goto fail;
		}
		/* In concurrency case, STA may be already associated in a particular channel.
		 * so retrieve the current channel of primary interface and then start the virtual
		 * interface on that.
		 */
		 chspec = wl_cfg80211_get_shared_freq(wiphy);

		/* For P2P mode, use P2P-specific driver features to create the
		 * bss: "cfg p2p_ifadd"
		 */
		wl_set_p2p_status(cfg, IF_ADDING);
		memset(&cfg->if_event_info, 0, sizeof(cfg->if_event_info));
		cfg_type = wl_cfgp2p_get_conn_idx(cfg);
		if (cfg_type == BCME_ERROR) {
			wl_clr_p2p_status(cfg, IF_ADDING);
			WL_ERR(("Failed to get connection idx for p2p interface\n"));
			err = -ENOTSUPP;
			goto fail;
		}
		err = wl_cfgp2p_ifadd(cfg, wl_to_p2p_bss_macaddr(cfg, cfg_type),
			htod32(wlif_type), chspec);
		if (unlikely(err)) {
			wl_clr_p2p_status(cfg, IF_ADDING);
			WL_ERR((" virtual iface add failed (%d) \n", err));
			err = -ENOMEM;
			goto fail;
		}

		timeout = wait_event_interruptible_timeout(cfg->netif_change_event,
			((wl_get_p2p_status(cfg, IF_ADDING) == false) &&
			(cfg->if_event_info.valid)),
			msecs_to_jiffies(MAX_WAIT_TIME));

		if (timeout > 0 && !wl_get_p2p_status(cfg, IF_ADDING) && cfg->if_event_info.valid) {
			struct wireless_dev *vwdev;
			int pm_mode = PM_ENABLE;
			wl_if_event_info *event = &cfg->if_event_info;
			/* IF_ADD event has come back, we can proceed to to register
			 * the new interface now, use the interface name provided by caller (thus
			 * ignore the one from wlc)
			 */
			new_ndev = wl_cfg80211_allocate_if(cfg, event->ifidx, cfg->p2p->vir_ifname,
				event->mac, event->bssidx, event->name);
			if (new_ndev == NULL) {
				err = -ENODEV;
				goto fail;
			}

			wl_to_p2p_bss_ndev(cfg, cfg_type) = new_ndev;
			wl_to_p2p_bss_bssidx(cfg, cfg_type) = event->bssidx;
			vwdev = kzalloc(sizeof(*vwdev), GFP_KERNEL);
			if (unlikely(!vwdev)) {
				WL_ERR(("Could not allocate wireless device\n"));
				err = -ENOMEM;
				goto fail;
			}
			vwdev->wiphy = cfg->wdev->wiphy;
			WL_INFORM(("virtual interface(%s) is created\n", cfg->p2p->vir_ifname));
			if (type == NL80211_IFTYPE_P2P_GO) {
				cfg->p2p->p2p_go_count++;
			}
			vwdev->iftype = type;
			vwdev->netdev = new_ndev;
			new_ndev->ieee80211_ptr = vwdev;
			SET_NETDEV_DEV(new_ndev, wiphy_dev(vwdev->wiphy));
			wl_set_drv_status(cfg, READY, new_ndev);
			if (wl_config_ifmode(cfg, new_ndev, type) < 0) {
				WL_ERR(("conf ifmode failed\n"));
				kfree(vwdev);
				err = -ENOTSUPP;
				goto fail;
			}

			if (wl_cfg80211_register_if(cfg,
				event->ifidx, new_ndev, FALSE) != BCME_OK) {
				wl_cfg80211_remove_if(cfg, event->ifidx, new_ndev, FALSE);
				err = -ENODEV;
				goto fail;
			}
			err = wl_alloc_netinfo(cfg, new_ndev, vwdev, mode, pm_mode, event->bssidx);
			if (unlikely(err != 0)) {
				wl_cfg80211_remove_if(cfg, event->ifidx, new_ndev, FALSE);
				WL_ERR(("Allocation of netinfo failed (%d) \n", err));
				goto fail;
			}
			val = 1;
			/* Disable firmware roaming for P2P interface  */
			wldev_iovar_setint(new_ndev, "roam_off", val);
			wldev_iovar_setint(new_ndev, "bcn_timeout", dhd->conf->bcn_timeout);
#ifdef WL11ULB
			if (cfg->p2p_wdev && is_p2p_group_iface(new_ndev->ieee80211_ptr)) {
				u32 ulb_bw = wl_cfg80211_get_ulb_bw(cfg, cfg->p2p_wdev);
				if (ulb_bw) {
					/* Apply ULB BW settings on the newly spawned interface */
					WL_DBG(("[ULB] Applying ULB BW for the newly"
						"created P2P interface \n"));
					if (wl_cfg80211_set_ulb_bw(new_ndev,
						ulb_bw, new_ndev->name) < 0) {
						/*
						 * If ulb_bw set failed, fail the iface creation.
						 * wl_dealloc_netinfo_by_wdev will be called by the
						 * unregister notifier.
						 */
						wl_cfg80211_remove_if(cfg,
							event->ifidx, new_ndev, FALSE);
						err = -EINVAL;
						goto fail;
					}
				}
			}
#endif /* WL11ULB */

			if (mode != WL_MODE_AP)
				wldev_iovar_setint(new_ndev, "buf_key_b4_m4", 1);

			WL_ERR((" virtual interface(%s) is "
				"created net attach done\n", cfg->p2p->vir_ifname));
			if (mode == WL_MODE_AP)
				wl_set_drv_status(cfg, CONNECTED, new_ndev);
#ifdef SUPPORT_AP_POWERSAVE
			if (mode == WL_MODE_AP) {
				dhd_set_ap_powersave(dhd, 0, TRUE);
			}
#endif /* SUPPORT_AP_POWERSAVE */
			if (type == NL80211_IFTYPE_P2P_CLIENT)
				dhd_mode = DHD_FLAG_P2P_GC_MODE;
			else if (type == NL80211_IFTYPE_P2P_GO)
				dhd_mode = DHD_FLAG_P2P_GO_MODE;
			DNGL_FUNC(dhd_cfg80211_set_p2p_info, (cfg, dhd_mode));
			/* reinitialize completion to clear previous count */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0))
			INIT_COMPLETION(cfg->iface_disable);
#else
			init_completion(&cfg->iface_disable);
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0) */
#ifdef SUPPORT_SET_CAC
			wl_cfg80211_set_cac(cfg, 0);
#endif /* SUPPORT_SET_CAC */
			mutex_unlock(&cfg->if_sync);
			return ndev_to_cfgdev(new_ndev);
		} else {
			wl_clr_p2p_status(cfg, IF_ADDING);
			WL_ERR((" virtual interface(%s) is not created \n", cfg->p2p->vir_ifname));

			WL_ERR(("left timeout : %d\n", timeout));
			WL_ERR(("IF_ADDING status : %d\n", wl_get_p2p_status(cfg, IF_ADDING)));
			WL_ERR(("event valid : %d\n", cfg->if_event_info.valid));

			wl_clr_p2p_status(cfg, GO_NEG_PHASE);
			wl_set_p2p_status(cfg, IF_DELETING);

			hang_required = true;
			if ((err = wl_cfgp2p_ifdel(cfg,
					wl_to_p2p_bss_macaddr(cfg,
					cfg_type))) == BCME_OK) {
				timeout = wait_event_interruptible_timeout(cfg->netif_change_event,
					((wl_get_p2p_status(cfg, IF_DELETING) == false) &&
					(cfg->if_event_info.valid)),
					msecs_to_jiffies(MAX_WAIT_TIME));
				if (timeout > 0 && !wl_get_p2p_status(cfg, IF_DELETING) &&
					cfg->if_event_info.valid) {
					hang_required = false;
					WL_ERR(("IFDEL operation done\n"));
				} else {
					WL_ERR(("IFDEL didn't complete properly\n"));
				}
				err = -ENODEV;
#ifdef SUPPORT_SET_CAC
				wl_cfg80211_set_cac(cfg, 1);
#endif /* SUPPORT_SET_CAC */
			} else {
				WL_ERR(("IFDEL operation failed, error code = %d\n", err));
			}

			memset(cfg->p2p->vir_ifname, '\0', IFNAMSIZ);
			wl_to_p2p_bss_bssidx(cfg, cfg_type) = -1;
#ifdef PROP_TXSTATUS_VSDB
#if defined(BCMSDIO) || defined(BCMDBUS)
			dhd_wlfc_get_enable(dhd, &enabled);
			if (enabled && cfg->wlfc_on && dhd->op_mode != DHD_FLAG_HOSTAP_MODE &&
				dhd->op_mode != DHD_FLAG_IBSS_MODE && dhd->conf->disable_proptx!=0) {
				dhd_wlfc_deinit(dhd);
				cfg->wlfc_on = false;
			}
#endif /* BCMSDIO || BCMDBUS */
#endif /* PROP_TXSTATUS_VSDB */
		}
	}

fail:
	mutex_unlock(&cfg->if_sync);
	if (err) {
#ifdef WLTDLS
		/* Enable back TDLS on failure */
		wl_cfg80211_tdls_config(cfg, TDLS_STATE_IF_DELETE, false);
#endif /* WLTDLS */
		if (err != -ENOTSUPP) {
#if defined(BCMPCIE) && defined(DHD_FW_COREDUMP)
			if (dhd->memdump_enabled) {
				/* Load the dongle side dump to host
				 * memory and then BUG_ON()
				 */
				dhd->memdump_type = DUMP_TYPE_HANG_ON_IFACE_OP_FAIL;
				dhd_bus_mem_dump(dhd);
			}
#endif /* BCMPCIE && DHD_FW_COREDUMP */
			if (hang_required) {
				/* Notify the user space to recover */
				struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);
				WL_ERR(("if add failed, error %d, sent HANG event to %s\n",
					err, ndev->name));
				dhd->hang_reason = HANG_REASON_IFACE_OP_FAILURE;
				net_os_send_hang_message(ndev);
			}
		}
	}
	return ERR_PTR(err);
}

static s32
wl_cfg80211_del_virtual_iface(struct wiphy *wiphy, bcm_struct_cfgdev *cfgdev)
{
	struct net_device *dev = NULL;
	struct ether_addr p2p_mac;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	s32 timeout = -1;
	s32 ret = 0;
	s32 index = -1;
	s32 type = -1;
#if defined(CUSTOM_SET_CPUCORE) || defined(DHD_HANG_SEND_UP_TEST)
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);
#endif /* CUSTOM_SET_CPUCORE || DHD_HANG_SEND_UP_TEST */
	WL_DBG(("Enter\n"));

	memset(&p2p_mac, 0, sizeof(struct ether_addr));
#ifdef CUSTOM_SET_CPUCORE
	dhd->chan_isvht80 &= ~DHD_FLAG_P2P_MODE;
	if (!(dhd->chan_isvht80))
		dhd_set_cpucore(dhd, FALSE);
#endif /* CUSTOM_SET_CPUCORE */
	mutex_lock(&cfg->if_sync);
#ifdef WL_CFG80211_P2P_DEV_IF
	if (cfgdev->iftype == NL80211_IFTYPE_P2P_DEVICE) {
		if (dhd_download_fw_on_driverload) {
			ret = wl_cfgp2p_del_p2p_disc_if(cfgdev, cfg);
		} else {
			cfg->down_disc_if = TRUE;
			ret = 0;
		}
		mutex_unlock(&cfg->if_sync);
		return ret;
	}
#endif /* WL_CFG80211_P2P_DEV_IF */
	dev = cfgdev_to_wlc_ndev(cfgdev, cfg);

#ifdef WLAIBSS_MCHAN
	if (cfgdev == cfg->ibss_cfgdev) {
		ret = bcm_cfg80211_del_ibss_if(wiphy, cfgdev);
		goto done;
	}
#endif /* WLAIBSS_MCHAN */

	if ((index = wl_get_bssidx_by_wdev(cfg, cfgdev_to_wdev(cfgdev))) < 0) {
		WL_ERR(("Find p2p index from wdev failed\n"));
		ret = -ENODEV;
		goto done;
	}
	if ((cfg->p2p_supported) && index && (wl_cfgp2p_find_type(cfg, index, &type) == BCME_OK)) {
		/* Handle P2P Interace del */
		memcpy(p2p_mac.octet, wl_to_p2p_bss_macaddr(cfg, type).octet, ETHER_ADDR_LEN);

		/* Clear GO_NEG_PHASE bit to take care of GO-NEG-FAIL cases
		 */
		WL_DBG(("P2P: GO_NEG_PHASE status cleared "));
		wl_clr_p2p_status(cfg, GO_NEG_PHASE);
		if (wl_cfgp2p_vif_created(cfg)) {
			if (wl_get_drv_status(cfg, SCANNING, dev)) {
				wl_notify_escan_complete(cfg, dev, true, true);
			}
			/* Delete pm_enable_work */
			wl_add_remove_pm_enable_work(cfg, WL_PM_WORKQ_DEL);

			/* for GC */
			if (wl_get_drv_status(cfg, DISCONNECTING, dev) &&
				(wl_get_mode_by_netdev(cfg, dev) != WL_MODE_AP)) {
				WL_ERR(("Wait for Link Down event for GC !\n"));
				wait_for_completion_timeout
					(&cfg->iface_disable, msecs_to_jiffies(500));
			}

			memset(&cfg->if_event_info, 0, sizeof(cfg->if_event_info));
			wl_set_p2p_status(cfg, IF_DELETING);
			DNGL_FUNC(dhd_cfg80211_clean_p2p_info, (cfg));

			/* for GO */
			if (wl_get_mode_by_netdev(cfg, dev) == WL_MODE_AP) {
				wl_add_remove_eventmsg(dev, WLC_E_PROBREQ_MSG, false);
				cfg->p2p->p2p_go_count--;
				/* disable interface before bsscfg free */
				ret = wl_cfgp2p_ifdisable(cfg, &p2p_mac);
				/* if fw doesn't support "ifdis",
				   do not wait for link down of ap mode
				 */
				if (ret == 0) {
					WL_ERR(("Wait for Link Down event for GO !!!\n"));
					wait_for_completion_timeout(&cfg->iface_disable,
						msecs_to_jiffies(500));
				} else if (ret != BCME_UNSUPPORTED) {
					msleep(300);
				}
			}
			wl_cfg80211_clear_per_bss_ies(cfg, index);

			if (wl_get_mode_by_netdev(cfg, dev) != WL_MODE_AP)
				wldev_iovar_setint(dev, "buf_key_b4_m4", 0);
			memcpy(p2p_mac.octet, wl_to_p2p_bss_macaddr(cfg, type).octet,
			ETHER_ADDR_LEN);
			CFGP2P_INFO(("primary idx %d : cfg p2p_ifdis "MACDBG"\n",
			       dev->ifindex, MAC2STRDBG(p2p_mac.octet)));

			/* delete interface after link down */
			ret = wl_cfgp2p_ifdel(cfg, &p2p_mac);
#if defined(DHD_HANG_SEND_UP_TEST)
			if (ret != BCME_OK ||
				dhd->req_hang_type == HANG_REASON_IFACE_OP_FAILURE)
#else /* DHD_HANG_SEND_UP_TEST */
			if (ret != BCME_OK)
#endif /* DHD_HANG_SEND_UP_TEST */
			{
				struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);
				dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);

				WL_ERR(("p2p_ifdel failed, error %d, sent HANG event to %s\n",
					ret, ndev->name));
#if defined(BCMPCIE) && defined(DHD_FW_COREDUMP)
				if (dhd->memdump_enabled) {
					/* Load the dongle side dump to host
					 * memory and then BUG_ON()
					 */
					dhd->memdump_type = DUMP_TYPE_HANG_ON_IFACE_OP_FAIL;
					dhd_bus_mem_dump(dhd);
				}
#endif /* BCMPCIE && DHD_FW_COREDUMP */
				dhd->hang_reason = HANG_REASON_IFACE_OP_FAILURE;
				net_os_send_hang_message(ndev);
			} else {
				/* Wait for IF_DEL operation to be finished */
				timeout = wait_event_interruptible_timeout(cfg->netif_change_event,
					((wl_get_p2p_status(cfg, IF_DELETING) == false) &&
					(cfg->if_event_info.valid)),
					msecs_to_jiffies(MAX_WAIT_TIME));
				if (timeout > 0 && !wl_get_p2p_status(cfg, IF_DELETING) &&
					cfg->if_event_info.valid) {

					WL_DBG(("IFDEL operation done\n"));
					wl_cfg80211_handle_ifdel(cfg, &cfg->if_event_info, dev);
				} else {
					WL_ERR(("IFDEL didn't complete properly\n"));
				}
#ifdef SUPPORT_SET_CAC
				wl_cfg80211_set_cac(cfg, 1);
#endif /* SUPPORT_SET_CAC */
			}

			ret = dhd_del_monitor(dev);
			if (wl_get_mode_by_netdev(cfg, dev) == WL_MODE_AP) {
				DHD_OS_WAKE_LOCK_CTRL_TIMEOUT_CANCEL((dhd_pub_t *)(cfg->pub));
			}
		}
	} else if ((dev->ieee80211_ptr->iftype == NL80211_IFTYPE_AP) ||
		(dev->ieee80211_ptr->iftype == NL80211_IFTYPE_STATION)) {
#ifdef WL_VIRTUAL_APSTA
		ret = wl_cfg80211_del_iface(wiphy, cfgdev);
#else
		WL_ERR(("Virtual APSTA not supported!\n"));
#endif /* WL_VIRTUAL_APSTA */
	}

done:
	mutex_unlock(&cfg->if_sync);
#ifdef WLTDLS
	if (ret == BCME_OK) {
		/* If interface del is success, try enabling back TDLS */
		wl_cfg80211_tdls_config(cfg, TDLS_STATE_IF_DELETE, false);
	}
#endif /* WLTDLS */
	return ret;
}

static s32
wl_cfg80211_change_virtual_iface(struct wiphy *wiphy, struct net_device *ndev,
	enum nl80211_iftype type,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0))
	u32 *flags,
#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0)) */
	struct vif_params *params)
{
	s32 ap = 0;
	s32 infra_ibss = 1;
	s32 wlif_type;
	s32 mode = 0;
	s32 err = BCME_OK;
	s32 index;
	s32 conn_idx = -1;
	chanspec_t chspec;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);

	mutex_lock(&cfg->if_sync);
	WL_DBG(("Enter type %d\n", type));
	switch (type) {
	case NL80211_IFTYPE_MONITOR:
	case NL80211_IFTYPE_WDS:
#ifndef WLMESH_CFG80211
	case NL80211_IFTYPE_MESH_POINT:
#endif /* WLMESH_CFG80211 */
		ap = 1;
		WL_ERR(("type (%d) : currently we do not support this type\n",
			type));
		break;
#ifdef WLMESH_CFG80211
	case NL80211_IFTYPE_MESH_POINT:
		infra_ibss = WL_BSSTYPE_MESH;
		break;
#endif /* WLMESH_CFG80211 */
	case NL80211_IFTYPE_ADHOC:
		mode = WL_MODE_IBSS;
		infra_ibss = 0;
		break;
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
		if (ndev->ieee80211_ptr->iftype == NL80211_IFTYPE_AP) {
			s32 bssidx = wl_get_bssidx_by_wdev(cfg, ndev->ieee80211_ptr);
			if (bssidx < 0) {
				/* validate bssidx */
				WL_ERR(("Wrong bssidx! \n"));
				err = -EINVAL;
				goto error;
			}
			WL_DBG(("del interface. bssidx:%d", bssidx));
			/* Downgrade role from AP to STA */
			if ((err = wl_cfg80211_add_del_bss(cfg, ndev,
					bssidx, NL80211_IFTYPE_STATION, 0, NULL)) < 0) {
				WL_ERR(("AP-STA Downgrade failed \n"));
				err = -EINVAL;
				goto error;
			}
		}
		mode = WL_MODE_BSS;
		break;
	case NL80211_IFTYPE_AP:
		dhd->op_mode |= DHD_FLAG_HOSTAP_MODE;
		/* intentional fall through */
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_P2P_GO:
		mode = WL_MODE_AP;
		ap = 1;
		break;
	default:
		err = -EINVAL;
		goto error;
	}

	if (!dhd) {
		err = -EINVAL;
		goto error;
	}
	if (ap) {
		wl_set_mode_by_netdev(cfg, ndev, mode);
		if (is_p2p_group_iface(ndev->ieee80211_ptr) &&
			cfg->p2p && wl_cfgp2p_vif_created(cfg)) {
			WL_DBG(("p2p_vif_created p2p_on (%d)\n", p2p_on(cfg)));
			wl_notify_escan_complete(cfg, ndev, true, true);

			/* Dual p2p doesn't support multiple P2PGO interfaces,
			 * p2p_go_count is the counter for GO creation
			 * requests.
			 */
			if ((cfg->p2p->p2p_go_count > 0) && (type == NL80211_IFTYPE_P2P_GO)) {
				wl_set_mode_by_netdev(cfg, ndev, WL_MODE_BSS);
				WL_ERR(("Fw does not support multiple GO\n"));
				err = BCME_ERROR;
				goto error;
			}
			/* In concurrency case, STA may be already associated in a particular
			 * channel. so retrieve the current channel of primary interface and
			 * then start the virtual interface on that.
			 */
			chspec = wl_cfg80211_get_shared_freq(wiphy);
			index = wl_get_bssidx_by_wdev(cfg, ndev->ieee80211_ptr);
			if (index < 0) {
				WL_ERR(("Find p2p index from ndev(%p) failed\n", ndev));
				err = BCME_ERROR;
				goto error;
			}
			if (wl_cfgp2p_find_type(cfg, index, &conn_idx) != BCME_OK) {
				err = BCME_ERROR;
				goto error;
			}

			wlif_type = WL_P2P_IF_GO;
			WL_MSG(ndev->name, "ap (%d), infra_ibss (%d), iftype (%d) conn_idx (%d)\n",
				ap, infra_ibss, type, conn_idx);
			wl_set_p2p_status(cfg, IF_CHANGING);
			wl_clr_p2p_status(cfg, IF_CHANGED);
			wl_cfgp2p_ifchange(cfg, wl_to_p2p_bss_macaddr(cfg, conn_idx),
				htod32(wlif_type), chspec, conn_idx);
			wait_event_interruptible_timeout(cfg->netif_change_event,
				(wl_get_p2p_status(cfg, IF_CHANGED) == true),
				msecs_to_jiffies(MAX_WAIT_TIME));
			wl_set_mode_by_netdev(cfg, ndev, mode);
			dhd->op_mode &= ~DHD_FLAG_P2P_GC_MODE;
			dhd->op_mode |= DHD_FLAG_P2P_GO_MODE;
			wl_clr_p2p_status(cfg, IF_CHANGING);
			wl_clr_p2p_status(cfg, IF_CHANGED);
			if (mode == WL_MODE_AP)
				wl_set_drv_status(cfg, CONNECTED, ndev);
#ifdef SUPPORT_AP_POWERSAVE
			dhd_set_ap_powersave(dhd, 0, TRUE);
#endif /* SUPPORT_AP_POWERSAVE */
		} else if ((type == NL80211_IFTYPE_AP) &&
			!wl_get_drv_status(cfg, AP_CREATED, ndev)) {
#if 0
			err = wl_cfg80211_set_ap_role(cfg, ndev);
			if (unlikely(err)) {
				WL_ERR(("set ap role failed!\n"));
				goto error;
			}
#else
			wl_set_drv_status(cfg, AP_CREATING, ndev);
#endif
		} else {
			WL_ERR(("Cannot change the interface for GO or SOFTAP\n"));
			err = -EINVAL;
			goto error;
		}
	} else {
		/* P2P GO interface deletion is handled on the basis of role type (AP).
		 * So avoid changing role for p2p type.
		 */
		if (ndev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_GO)
			wl_set_mode_by_netdev(cfg, ndev, mode);
		WL_DBG(("Change_virtual_iface for transition from GO/AP to client/STA\n"));
#ifdef SUPPORT_AP_POWERSAVE
		dhd_set_ap_powersave(dhd, 0, FALSE);
#endif /* SUPPORT_AP_POWERSAVE */
	}

	if (!infra_ibss) {
		err = wldev_ioctl_set(ndev, WLC_SET_INFRA, &infra_ibss, sizeof(s32));
		if (err < 0) {
			WL_ERR(("SET INFRA/IBSS  error %d\n", err));
			err = -EINVAL;
			goto error;
		}
	}

	WL_DBG(("Setting iftype to %d \n", type));
	ndev->ieee80211_ptr->iftype = type;
error:
	mutex_unlock(&cfg->if_sync);
	return err;
}

s32
wl_cfg80211_notify_ifadd(struct net_device *dev, int ifidx, char *name, uint8 *mac, uint8 bssidx)
{
	bool ifadd_expected = FALSE;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	/* P2P may send WLC_E_IF_ADD and/or WLC_E_IF_CHANGE during IF updating ("p2p_ifupd")
	 * redirect the IF_ADD event to ifchange as it is not a real "new" interface
	 */
	if (wl_get_p2p_status(cfg, IF_CHANGING))
		return wl_cfg80211_notify_ifchange(dev, ifidx, name, mac, bssidx);

	/* Okay, we are expecting IF_ADD (as IF_ADDING is true) */
	if (wl_get_p2p_status(cfg, IF_ADDING)) {
		ifadd_expected = TRUE;
		wl_clr_p2p_status(cfg, IF_ADDING);
	} else if (cfg->bss_pending_op) {
		ifadd_expected = TRUE;
		cfg->bss_pending_op = FALSE;
	}

	if (ifadd_expected) {
		wl_if_event_info *if_event_info = &cfg->if_event_info;

		if_event_info->valid = TRUE;
		if_event_info->ifidx = ifidx;
		if_event_info->bssidx = bssidx;
		strncpy(if_event_info->name, name, IFNAMSIZ);
		if_event_info->name[IFNAMSIZ] = '\0';
		if (mac)
			memcpy(if_event_info->mac, mac, ETHER_ADDR_LEN);
		wake_up_interruptible(&cfg->netif_change_event);
		return BCME_OK;
	}

	return BCME_ERROR;
}

s32
wl_cfg80211_notify_ifdel(struct net_device *dev, int ifidx, char *name, uint8 *mac, uint8 bssidx)
{
	bool ifdel_expected = FALSE;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	wl_if_event_info *if_event_info = &cfg->if_event_info;

	if (wl_get_p2p_status(cfg, IF_DELETING)) {
		ifdel_expected = TRUE;
		wl_clr_p2p_status(cfg, IF_DELETING);
	} else if (cfg->bss_pending_op) {
		ifdel_expected = TRUE;
		cfg->bss_pending_op = FALSE;
	}

	if (ifdel_expected) {
		if_event_info->valid = TRUE;
		if_event_info->ifidx = ifidx;
		if_event_info->bssidx = bssidx;
		wake_up_interruptible(&cfg->netif_change_event);
		return BCME_OK;
	}

	return BCME_ERROR;
}

s32
wl_cfg80211_notify_ifchange(struct net_device * dev, int ifidx, char *name, uint8 *mac,
	uint8 bssidx)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	if (wl_get_p2p_status(cfg, IF_CHANGING)) {
		wl_set_p2p_status(cfg, IF_CHANGED);
		wake_up_interruptible(&cfg->netif_change_event);
		return BCME_OK;
	}

	return BCME_ERROR;
}

static s32 wl_cfg80211_handle_ifdel(struct bcm_cfg80211 *cfg, wl_if_event_info *if_event_info,
	struct net_device* ndev)
{
	s32 type = -1;
	s32 bssidx = -1;
#ifdef PROP_TXSTATUS_VSDB
#if defined(BCMSDIO) || defined(BCMDBUS)
	dhd_pub_t *dhd =  (dhd_pub_t *)(cfg->pub);
	bool enabled;
#endif /* BCMSDIO || BCMDBUS */
#endif /* PROP_TXSTATUS_VSDB */

	bssidx = if_event_info->bssidx;
	if (bssidx != wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_CONNECTION1) &&
		bssidx != wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_CONNECTION2)) {
		WL_ERR(("got IF_DEL for if %d, not owned by cfg driver\n", bssidx));
		return BCME_ERROR;
	}

	if (p2p_is_on(cfg) && wl_cfgp2p_vif_created(cfg)) {
		if (cfg->scan_request && (cfg->escan_info.ndev == ndev)) {
			/* Abort any pending scan requests */
			cfg->escan_info.escan_state = WL_ESCAN_STATE_IDLE;
			WL_DBG(("ESCAN COMPLETED\n"));
			wl_notify_escan_complete(cfg, cfg->escan_info.ndev, true, false);
		}

		memset(cfg->p2p->vir_ifname, '\0', IFNAMSIZ);
		if (wl_cfgp2p_find_type(cfg, bssidx, &type) == BCME_OK) {
			/* Update P2P data */
			wl_clr_drv_status(cfg, CONNECTED, wl_to_p2p_bss_ndev(cfg, type));
			wl_to_p2p_bss_ndev(cfg, type) = NULL;
			wl_to_p2p_bss_bssidx(cfg, type) = -1;
		} else if (wl_get_bssidx_by_wdev(cfg, ndev->ieee80211_ptr) < 0) {
			WL_ERR(("bssidx not known for the given ndev as per net_info data \n"));
			return BCME_ERROR;
		}

#ifdef PROP_TXSTATUS_VSDB
#if defined(BCMSDIO) || defined(BCMDBUS)
		dhd_wlfc_get_enable(dhd, &enabled);
		if (enabled && cfg->wlfc_on && dhd->op_mode != DHD_FLAG_HOSTAP_MODE &&
			dhd->op_mode != DHD_FLAG_IBSS_MODE && dhd->conf->disable_proptx!=0) {
			dhd_wlfc_deinit(dhd);
			cfg->wlfc_on = false;
		}
#endif /* BCMSDIO || BCMDBUS */
#endif /* PROP_TXSTATUS_VSDB */
	}

	dhd_net_if_lock(ndev);
	wl_cfg80211_remove_if(cfg, if_event_info->ifidx, ndev, FALSE);
	dhd_net_if_unlock(ndev);

	return BCME_OK;
}

/* Find listen channel */
static s32 wl_find_listen_channel(struct bcm_cfg80211 *cfg,
	const u8 *ie, u32 ie_len)
{
	wifi_p2p_ie_t *p2p_ie;
	u8 *end, *pos;
	s32 listen_channel;

/* unfortunately const cast required here - function is
 * a callback so its signature must not be changed
 * and cascade of changing wl_cfgp2p_find_p2pie
 * causes need for const cast in other places
 */
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"")
#endif
	pos = (u8 *)ie;
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic pop")
#endif
	p2p_ie = wl_cfgp2p_find_p2pie(pos, ie_len);

	if (p2p_ie == NULL)
		return 0;

	pos = p2p_ie->subelts;
	end = p2p_ie->subelts + (p2p_ie->len - 4);

	CFGP2P_DBG((" found p2p ie ! lenth %d \n",
		p2p_ie->len));

	while (pos < end) {
		uint16 attr_len;
		if (pos + 2 >= end) {
			CFGP2P_DBG((" -- Invalid P2P attribute"));
			return 0;
		}
		attr_len = ((uint16) (((pos + 1)[1] << 8) | (pos + 1)[0]));

		if (pos + 3 + attr_len > end) {
			CFGP2P_DBG(("P2P: Attribute underflow "
				   "(len=%u left=%d)",
				   attr_len, (int) (end - pos - 3)));
			return 0;
		}

		/* if Listen Channel att id is 6 and the vailue is valid,
		 * return the listen channel
		 */
		if (pos[0] == 6) {
			/* listen channel subel length format
			 * 1(id) + 2(len) + 3(country) + 1(op. class) + 1(chan num)
			 */
			listen_channel = pos[1 + 2 + 3 + 1];

			if (listen_channel == SOCIAL_CHAN_1 ||
				listen_channel == SOCIAL_CHAN_2 ||
				listen_channel == SOCIAL_CHAN_3) {
				CFGP2P_DBG((" Found my Listen Channel %d \n", listen_channel));
				return listen_channel;
			}
		}
		pos += 3 + attr_len;
	}
	return 0;
}

static void wl_scan_prep(struct bcm_cfg80211 *cfg, struct wl_scan_params *params,
	struct cfg80211_scan_request *request)
{
	u32 n_ssids;
	u32 n_channels;
	u16 channel;
	chanspec_t chanspec;
	s32 i = 0, j = 0, offset;
	char *ptr;
	wlc_ssid_t ssid;
	struct wireless_dev *wdev;

	memcpy(&params->bssid, &ether_bcast, ETHER_ADDR_LEN);
	params->bss_type = DOT11_BSSTYPE_ANY;
	params->scan_type = 0;
	params->nprobes = -1;
	params->active_time = -1;
	params->passive_time = -1;
	params->home_time = -1;
	params->channel_num = 0;
	memset(&params->ssid, 0, sizeof(wlc_ssid_t));

	WL_SCAN(("Preparing Scan request\n"));
	WL_SCAN(("nprobes=%d\n", params->nprobes));
	WL_SCAN(("active_time=%d\n", params->active_time));
	WL_SCAN(("passive_time=%d\n", params->passive_time));
	WL_SCAN(("home_time=%d\n", params->home_time));
	WL_SCAN(("scan_type=%d\n", params->scan_type));

	params->nprobes = htod32(params->nprobes);
	params->active_time = htod32(params->active_time);
	params->passive_time = htod32(params->passive_time);
	params->home_time = htod32(params->home_time);

	/* if request is null just exit so it will be all channel broadcast scan */
	if (!request)
		return;

	n_ssids = request->n_ssids;
	n_channels = request->n_channels;

	/* Copy channel array if applicable */
	WL_SCAN(("### List of channelspecs to scan ###\n"));
	if (n_channels > 0) {
		for (i = 0; i < n_channels; i++) {
			channel = ieee80211_frequency_to_channel(request->channels[i]->center_freq);
			/* SKIP DFS channels for Secondary interface */
			if ((cfg->escan_info.ndev != bcmcfg_to_prmry_ndev(cfg)) &&
				(request->channels[i]->flags &
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0))
				(IEEE80211_CHAN_RADAR | IEEE80211_CHAN_PASSIVE_SCAN)))
#else
				(IEEE80211_CHAN_RADAR | IEEE80211_CHAN_NO_IR)))
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0) */
				continue;
			if (!dhd_conf_match_channel(cfg->pub, channel))
				continue;

#if defined(WL_CFG80211_P2P_DEV_IF)
			wdev = request->wdev;
#else
			wdev = request->dev->ieee80211_ptr;
#endif /* WL_CFG80211_P2P_DEV_IF */
			chanspec = wl_cfg80211_ulb_get_min_bw_chspec(cfg, wdev, -1);
			if (chanspec == INVCHANSPEC) {
				WL_ERR(("Invalid chanspec! Skipping channel\n"));
				continue;
			}

			if (request->channels[i]->band == IEEE80211_BAND_2GHZ) {
				chanspec |= WL_CHANSPEC_BAND_2G;
			} else {
				chanspec |= WL_CHANSPEC_BAND_5G;
			}
			params->channel_list[j] = channel;
			params->channel_list[j] &= WL_CHANSPEC_CHAN_MASK;
			params->channel_list[j] |= chanspec;
			WL_SCAN(("Chan : %d, Channel spec: %x \n",
				channel, params->channel_list[j]));
			params->channel_list[j] = wl_chspec_host_to_driver(params->channel_list[j]);
			j++;
		}
	} else {
		WL_SCAN(("Scanning all channels\n"));
	}
	n_channels = j;
	/* Copy ssid array if applicable */
	WL_SCAN(("### List of SSIDs to scan ###\n"));
	if (n_ssids > 0) {
		offset = offsetof(wl_scan_params_t, channel_list) + n_channels * sizeof(u16);
		offset = roundup(offset, sizeof(u32));
		ptr = (char*)params + offset;
		for (i = 0; i < n_ssids; i++) {
			memset(&ssid, 0, sizeof(wlc_ssid_t));
			ssid.SSID_len = MIN(request->ssids[i].ssid_len, DOT11_MAX_SSID_LEN);
			memcpy(ssid.SSID, request->ssids[i].ssid, ssid.SSID_len);
			if (!ssid.SSID_len)
				WL_SCAN(("%d: Broadcast scan\n", i));
			else
				WL_SCAN(("%d: scan  for  %s size =%d\n", i,
				ssid.SSID, ssid.SSID_len));
			memcpy(ptr, &ssid, sizeof(wlc_ssid_t));
			ptr += sizeof(wlc_ssid_t);
		}
	} else {
		WL_SCAN(("Broadcast scan\n"));
	}
	/* Adding mask to channel numbers */
	params->channel_num =
	        htod32((n_ssids << WL_SCAN_PARAMS_NSSID_SHIFT) |
	               (n_channels & WL_SCAN_PARAMS_COUNT_MASK));

	if (n_channels == 1) {
		params->active_time = htod32(WL_SCAN_CONNECT_DWELL_TIME_MS);
		params->nprobes = htod32(params->active_time / WL_SCAN_JOIN_PROBE_INTERVAL_MS);
	}
}

static s32
wl_get_valid_channels(struct net_device *ndev, u8 *valid_chan_list, s32 size)
{
	wl_uint32_list_t *list;
	s32 err = BCME_OK;
	if (valid_chan_list == NULL || size <= 0)
		return -ENOMEM;

	memset(valid_chan_list, 0, size);
	list = (wl_uint32_list_t *)(void *) valid_chan_list;
	list->count = htod32(WL_NUMCHANNELS);
	err = wldev_ioctl_get(ndev, WLC_GET_VALID_CHANNELS, valid_chan_list, size);
	if (err != 0) {
		WL_ERR(("get channels failed with %d\n", err));
	}

	return err;
}

#if defined(USE_INITIAL_SHORT_DWELL_TIME)
#define FIRST_SCAN_ACTIVE_DWELL_TIME_MS 40
bool g_first_broadcast_scan = TRUE;
#endif 

static s32
wl_run_escan(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	struct cfg80211_scan_request *request, uint16 action)
{
	s32 err = BCME_OK;
	u32 n_channels;
	u32 n_ssids;
	s32 params_size = (WL_SCAN_PARAMS_FIXED_SIZE + OFFSETOF(wl_escan_params_t, params));
	wl_escan_params_t *params = NULL;
	u8 chan_buf[sizeof(u32)*(WL_NUMCHANNELS + 1)];
	u32 num_chans = 0;
	s32 channel;
	u32 n_valid_chan;
	s32 search_state = WL_P2P_DISC_ST_SCAN;
	u32 i, j, n_nodfs = 0;
	u16 *default_chan_list = NULL;
	wl_uint32_list_t *list;
	s32 bssidx = -1;
	struct net_device *dev = NULL;
#if defined(USE_INITIAL_SHORT_DWELL_TIME)
	bool is_first_init_2g_scan = false;
#endif 
	p2p_scan_purpose_t	p2p_scan_purpose = P2P_SCAN_PURPOSE_MIN;
	scb_val_t scbval;
	static int cnt = 0;

	WL_DBG(("Enter \n"));

	/* scan request can come with empty request : perform all default scan */
	if (!cfg) {
		err = -EINVAL;
		goto exit;
	}
	if (!cfg->p2p_supported || !p2p_scan(cfg)) {
		/* LEGACY SCAN TRIGGER */
		WL_SCAN((" LEGACY E-SCAN START\n"));

#if defined(USE_INITIAL_SHORT_DWELL_TIME)
		if (!request) {
			err = -EINVAL;
			goto exit;
		}
		if (ndev == bcmcfg_to_prmry_ndev(cfg) && g_first_broadcast_scan == true) {
			is_first_init_2g_scan = true;
			g_first_broadcast_scan = false;
		}
#endif 

		/* if scan request is not empty parse scan request paramters */
		if (request != NULL) {
			n_channels = request->n_channels;
			n_ssids = request->n_ssids;
			if (n_channels % 2)
				/* If n_channels is odd, add a padd of u16 */
				params_size += sizeof(u16) * (n_channels + 1);
			else
				params_size += sizeof(u16) * n_channels;

			/* Allocate space for populating ssids in wl_escan_params_t struct */
			params_size += sizeof(struct wlc_ssid) * n_ssids;
		}
		params = (wl_escan_params_t *) kzalloc(params_size, GFP_KERNEL);
		if (params == NULL) {
			err = -ENOMEM;
			goto exit;
		}
		wl_scan_prep(cfg, &params->params, request);

#if defined(USE_INITIAL_SHORT_DWELL_TIME)
		/* Override active_time to reduce scan time if it's first bradcast scan. */
		if (is_first_init_2g_scan)
			params->params.active_time = FIRST_SCAN_ACTIVE_DWELL_TIME_MS;
#endif 

		params->version = htod32(ESCAN_REQ_VERSION);
		params->action =  htod16(action);
		wl_escan_set_sync_id(params->sync_id, cfg);
		wl_escan_set_type(cfg, WL_SCANTYPE_LEGACY);
		if (params_size + sizeof("escan") >= WLC_IOCTL_MEDLEN) {
			WL_ERR(("ioctl buffer length not sufficient\n"));
			kfree(params);
			err = -ENOMEM;
			goto exit;
		}
		if (cfg->active_scan == PASSIVE_SCAN) {
			params->params.scan_type = DOT11_SCANTYPE_PASSIVE;
			WL_DBG(("Passive scan_type %d \n", params->params.scan_type));
		}

		bssidx = wl_get_bssidx_by_wdev(cfg, ndev->ieee80211_ptr);

		WL_MSG(ndev->name, "LEGACY_SCAN sync ID: %d, bssidx: %d\n", params->sync_id, bssidx);
		err = wldev_iovar_setbuf(ndev, "escan", params, params_size,
			cfg->escan_ioctl_buf, WLC_IOCTL_MEDLEN, NULL);
		if (unlikely(err)) {
			if (err == BCME_EPERM)
				/* Scan Not permitted at this point of time */
				WL_DBG((" Escan not permitted at this time (%d)\n", err));
			else
				WL_ERR((" Escan set error (%d)\n", err));
		} else {
			DBG_EVENT_LOG((dhd_pub_t *)cfg->pub, WIFI_EVENT_DRIVER_SCAN_REQUESTED);
		}
		kfree(params);
	}
	else if (p2p_is_on(cfg) && p2p_scan(cfg)) {
		/* P2P SCAN TRIGGER */
		s32 _freq = 0;
		n_nodfs = 0;
		if (request && request->n_channels) {
			num_chans = request->n_channels;
			WL_SCAN((" chann number : %d\n", num_chans));
			default_chan_list = kzalloc(num_chans * sizeof(*default_chan_list),
				GFP_KERNEL);
			if (default_chan_list == NULL) {
				WL_ERR(("channel list allocation failed \n"));
				err = -ENOMEM;
				goto exit;
			}
			if (!wl_get_valid_channels(ndev, chan_buf, sizeof(chan_buf))) {
#ifdef P2P_SKIP_DFS
				int is_printed = false;
#endif /* P2P_SKIP_DFS */
				list = (wl_uint32_list_t *) chan_buf;
				n_valid_chan = dtoh32(list->count);
				if (n_valid_chan > WL_NUMCHANNELS) {
					WL_ERR(("wrong n_valid_chan:%d\n", n_valid_chan));
					kfree(default_chan_list);
					err = -EINVAL;
					goto exit;
				}

				for (i = 0; i < num_chans; i++)
				{
					_freq = request->channels[i]->center_freq;
					channel = ieee80211_frequency_to_channel(_freq);

					/* ignore DFS channels */
					if (request->channels[i]->flags &
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
						(IEEE80211_CHAN_NO_IR
						| IEEE80211_CHAN_RADAR))
#else
						(IEEE80211_CHAN_RADAR
						| IEEE80211_CHAN_PASSIVE_SCAN))
#endif
						continue;
#ifdef P2P_SKIP_DFS
					if (channel >= 52 && channel <= 144) {
						if (is_printed == false) {
							WL_ERR(("SKIP DFS CHANs(52~144)\n"));
							is_printed = true;
						}
						continue;
					}
#endif /* P2P_SKIP_DFS */

					for (j = 0; j < n_valid_chan; j++) {
						/* allows only supported channel on
						*  current reguatory
						*/
						if (n_nodfs >= num_chans) {
							break;
						}
						if (channel == (dtoh32(list->element[j]))) {
							default_chan_list[n_nodfs++] =
								channel;
						}
					}

				}
			}
			if (num_chans == SOCIAL_CHAN_CNT && (
						(default_chan_list[0] == SOCIAL_CHAN_1) &&
						(default_chan_list[1] == SOCIAL_CHAN_2) &&
						(default_chan_list[2] == SOCIAL_CHAN_3))) {
				/* SOCIAL CHANNELS 1, 6, 11 */
				search_state = WL_P2P_DISC_ST_SEARCH;
				p2p_scan_purpose = P2P_SCAN_SOCIAL_CHANNEL;
				WL_INFORM(("P2P SEARCH PHASE START \n"));
			} else if (((dev = wl_to_p2p_bss_ndev(cfg, P2PAPI_BSSCFG_CONNECTION1)) &&
				(wl_get_mode_by_netdev(cfg, dev) == WL_MODE_AP)) ||
				((dev = wl_to_p2p_bss_ndev(cfg, P2PAPI_BSSCFG_CONNECTION2)) &&
				(wl_get_mode_by_netdev(cfg, dev) == WL_MODE_AP))) {
				/* If you are already a GO, then do SEARCH only */
				WL_INFORM(("Already a GO. Do SEARCH Only"));
				search_state = WL_P2P_DISC_ST_SEARCH;
				num_chans = n_nodfs;
				p2p_scan_purpose = P2P_SCAN_NORMAL;

			} else if (num_chans == 1) {
				p2p_scan_purpose = P2P_SCAN_CONNECT_TRY;
			} else if (num_chans == SOCIAL_CHAN_CNT + 1) {
			/* SOCIAL_CHAN_CNT + 1 takes care of the Progressive scan supported by
			 * the supplicant
			 */
				p2p_scan_purpose = P2P_SCAN_SOCIAL_CHANNEL;
			} else {
				WL_INFORM(("P2P SCAN STATE START \n"));
				num_chans = n_nodfs;
				p2p_scan_purpose = P2P_SCAN_NORMAL;
			}
		} else {
			err = -EINVAL;
			goto exit;
		}
		err = wl_cfgp2p_escan(cfg, ndev, ACTIVE_SCAN, num_chans, default_chan_list,
			search_state, action,
			wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE), NULL,
			p2p_scan_purpose);

		if (!err)
			cfg->p2p->search_state = search_state;

		kfree(default_chan_list);
	}
exit:
	if (unlikely(err)) {
		/* Don't print Error incase of Scan suppress */
		if ((err == BCME_EPERM) && cfg->scan_suppressed)
			WL_DBG(("Escan failed: Scan Suppressed \n"));
		else {
			cnt++;
			WL_ERR(("error (%d), cnt=%d\n", err, cnt));
			// terence 20140111: send disassoc to firmware
			if (cnt >= 4) {
				memset(&scbval, 0, sizeof(scb_val_t));
				wldev_ioctl(ndev, WLC_DISASSOC, &scbval, sizeof(scb_val_t), true);
				WL_ERR(("Send disassoc to break the busy dev=%p\n", dev));
				cnt = 0;
			}
		}
	} else {
		cnt = 0;
	}
	return err;
}


static s32
wl_do_escan(struct bcm_cfg80211 *cfg, struct wiphy *wiphy, struct net_device *ndev,
	struct cfg80211_scan_request *request)
{
	s32 err = BCME_OK;
	s32 passive_scan = 0;
	s32 passive_scan_time = 0;
	s32 passive_scan_time_org = 0;
	wl_scan_results_t *results;
	WL_SCAN(("Enter \n"));

	results = wl_escan_get_buf(cfg, FALSE);
	results->version = 0;
	results->count = 0;
	results->buflen = WL_SCAN_RESULTS_FIXED_SIZE;

	cfg->escan_info.ndev = ndev;
	cfg->escan_info.wiphy = wiphy;
	cfg->escan_info.escan_state = WL_ESCAN_STATE_SCANING;
	passive_scan = cfg->active_scan ? 0 : 1;
	err = wldev_ioctl_set(ndev, WLC_SET_PASSIVE_SCAN,
	                      &passive_scan, sizeof(passive_scan));
	if (unlikely(err)) {
		WL_ERR(("error (%d)\n", err));
		goto exit;
	}

	if (passive_channel_skip) {

		err = wldev_ioctl_get(ndev, WLC_GET_SCAN_PASSIVE_TIME,
			&passive_scan_time_org, sizeof(passive_scan_time_org));
		if (unlikely(err)) {
			WL_ERR(("== error (%d)\n", err));
			goto exit;
		}

		WL_SCAN(("PASSIVE SCAN time : %d \n", passive_scan_time_org));

		passive_scan_time = 0;
		err = wldev_ioctl_set(ndev, WLC_SET_SCAN_PASSIVE_TIME,
			&passive_scan_time, sizeof(passive_scan_time));
		if (unlikely(err)) {
			WL_ERR(("== error (%d)\n", err));
			goto exit;
		}

		WL_SCAN(("PASSIVE SCAN SKIPED!! (passive_channel_skip:%d) \n",
			passive_channel_skip));
	}

	err = wl_run_escan(cfg, ndev, request, WL_SCAN_ACTION_START);

	if (passive_channel_skip) {
		err = wldev_ioctl_set(ndev, WLC_SET_SCAN_PASSIVE_TIME,
			&passive_scan_time_org, sizeof(passive_scan_time_org));
		if (unlikely(err)) {
			WL_ERR(("== error (%d)\n", err));
			goto exit;
		}

		WL_SCAN(("PASSIVE SCAN RECOVERED!! (passive_scan_time_org:%d) \n",
			passive_scan_time_org));
	}

exit:
	return err;
}

static s32
__wl_cfg80211_scan(struct wiphy *wiphy, struct net_device *ndev,
	struct cfg80211_scan_request *request,
	struct cfg80211_ssid *this_ssid)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct cfg80211_ssid *ssids;
	struct ether_addr primary_mac;
	bool p2p_ssid;
#ifdef WL11U
	bcm_tlv_t *interworking_ie;
#endif
	s32 err = 0;
	s32 bssidx = -1;
	s32 i;

	unsigned long flags;
	static s32 busy_count = 0;
#ifdef WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST
	struct net_device *remain_on_channel_ndev = NULL;
#endif
	uint scan_timer_interval_ms = WL_SCAN_TIMER_INTERVAL_MS;

	/*
	 * Hostapd triggers scan before starting automatic channel selection
	 * to collect channel characteristics. However firmware scan engine
	 * doesn't support any channel characteristics collection along with
	 * scan. Hence return scan success.
	 */
	if (request && (scan_req_iftype(request) == NL80211_IFTYPE_AP)) {
		WL_INFORM(("Scan Command on SoftAP Interface. Ignoring...\n"));
// terence 20161023: let it scan in SoftAP mode
//		return 0;
	}

	ndev = ndev_to_wlc_ndev(ndev, cfg);

	if (WL_DRV_STATUS_SENDING_AF_FRM_EXT(cfg)) {
		WL_ERR(("Sending Action Frames. Try it again.\n"));
		return -EAGAIN;
	}

	WL_DBG(("Enter wiphy (%p)\n", wiphy));
	if (wl_get_drv_status_all(cfg, SCANNING)) {
		if (cfg->scan_request == NULL) {
			wl_clr_drv_status_all(cfg, SCANNING);
			WL_DBG(("<<<<<<<<<<<Force Clear Scanning Status>>>>>>>>>>>\n"));
		} else {
			WL_ERR(("Scanning already\n"));
			return -EAGAIN;
		}
	}
	if (wl_get_drv_status(cfg, SCAN_ABORTING, ndev)) {
		WL_ERR(("Scanning being aborted\n"));
		return -EAGAIN;
	}
	if (request && request->n_ssids > WL_SCAN_PARAMS_SSID_MAX) {
		WL_ERR(("request null or n_ssids > WL_SCAN_PARAMS_SSID_MAX\n"));
		return -EOPNOTSUPP;
	}

#ifdef P2P_LISTEN_OFFLOADING
	if (wl_get_p2p_status(cfg, DISC_IN_PROGRESS)) {
		WL_ERR(("P2P_FIND: Discovery offload is in progress\n"));
		return -EAGAIN;
	}
#endif /* P2P_LISTEN_OFFLOADING */

#ifdef WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST
	remain_on_channel_ndev = wl_cfg80211_get_remain_on_channel_ndev(cfg);
	if (remain_on_channel_ndev) {
		WL_DBG(("Remain_on_channel bit is set, somehow it didn't get cleared\n"));
		wl_notify_escan_complete(cfg, remain_on_channel_ndev, true, true);
	}
#endif /* WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST */


	/* Arm scan timeout timer */
	mod_timer(&cfg->scan_timeout, jiffies + msecs_to_jiffies(scan_timer_interval_ms));
	if (request) {		/* scan bss */
		ssids = request->ssids;
		p2p_ssid = false;
		for (i = 0; i < request->n_ssids; i++) {
			if (ssids[i].ssid_len &&
				IS_P2P_SSID(ssids[i].ssid, ssids[i].ssid_len)) {
				p2p_ssid = true;
				break;
			}
		}
		if (p2p_ssid) {
			if (cfg->p2p_supported) {
				/* p2p scan trigger */
				if (p2p_on(cfg) == false) {
					/* p2p on at the first time */
					p2p_on(cfg) = true;
					wl_cfgp2p_set_firm_p2p(cfg);
					get_primary_mac(cfg, &primary_mac);
					wl_cfgp2p_generate_bss_mac(cfg, &primary_mac);
#if defined(P2P_IE_MISSING_FIX)
					cfg->p2p_prb_noti = false;
#endif
				}
				wl_clr_p2p_status(cfg, GO_NEG_PHASE);
				WL_DBG(("P2P: GO_NEG_PHASE status cleared \n"));
				p2p_scan(cfg) = true;
			}
		} else {
			/* legacy scan trigger
			 * So, we have to disable p2p discovery if p2p discovery is on
			 */
			if (cfg->p2p_supported) {
				p2p_scan(cfg) = false;
				/* If Netdevice is not equals to primary and p2p is on
				*  , we will do p2p scan using P2PAPI_BSSCFG_DEVICE.
				*/

				if (p2p_scan(cfg) == false) {
					if (wl_get_p2p_status(cfg, DISCOVERY_ON)) {
						err = wl_cfgp2p_discover_enable_search(cfg,
						false);
						if (unlikely(err)) {
							goto scan_out;
						}

					}
				}
			}
			if (!cfg->p2p_supported || !p2p_scan(cfg)) {
				if ((bssidx = wl_get_bssidx_by_wdev(cfg,
					ndev->ieee80211_ptr)) < 0) {
					WL_ERR(("Find p2p index from ndev(%p) failed\n",
						ndev));
					err = BCME_ERROR;
					goto scan_out;
				}
#ifdef WL11U
				if (request && (interworking_ie =
						wl_cfg80211_find_interworking_ie(
						request->ie, request->ie_len)) != NULL) {
					if ((err = wl_cfg80211_add_iw_ie(cfg, ndev, bssidx,
							VNDR_IE_CUSTOM_FLAG, interworking_ie->id,
							interworking_ie->data,
							interworking_ie->len)) != BCME_OK) {
						WL_ERR(("Failed to add interworking IE"));
					}
				} else if (cfg->wl11u) {
					/* we have to clear IW IE and disable gratuitous APR */
					wl_cfg80211_clear_iw_ie(cfg, ndev, bssidx);
					err = wldev_iovar_setint_bsscfg(ndev, "grat_arp",
					                                0, bssidx);
					/* we don't care about error here
					 * because the only failure case is unsupported,
					 * which is fine
					 */
					if (unlikely(err)) {
						WL_ERR(("Set grat_arp failed:(%d) Ignore!\n", err));
					}
					cfg->wl11u = FALSE;
				}
#endif /* WL11U */
				if (request) {
					err = wl_cfg80211_set_mgmt_vndr_ies(cfg,
							ndev_to_cfgdev(ndev),
							bssidx, VNDR_IE_PRBREQ_FLAG, request->ie,
							request->ie_len);
				}

				if (unlikely(err)) {
// terence 20161023: let it scan in SoftAP mode
//					goto scan_out;
				}

			}
		}
	} else {		/* scan in ibss */
		ssids = this_ssid;
	}

	if (request && cfg->p2p_supported) {
		WL_TRACE_HW4(("START SCAN\n"));
		DHD_OS_SCAN_WAKE_LOCK_TIMEOUT((dhd_pub_t *)(cfg->pub),
			SCAN_WAKE_LOCK_TIMEOUT);
		DHD_DISABLE_RUNTIME_PM((dhd_pub_t *)(cfg->pub));
	}

	if (cfg->p2p_supported) {
		if (request && p2p_on(cfg) && p2p_scan(cfg)) {

			/* find my listen channel */
			cfg->afx_hdl->my_listen_chan =
				wl_find_listen_channel(cfg, request->ie,
				request->ie_len);
			err = wl_cfgp2p_enable_discovery(cfg, ndev,
			request->ie, request->ie_len);

			if (unlikely(err)) {
				goto scan_out;
			}
		}
	}
	err = wl_do_escan(cfg, wiphy, ndev, request);
	if (likely(!err))
		goto scan_success;
	else
		goto scan_out;

scan_success:
	busy_count = 0;
	cfg->scan_request = request;
	wl_set_drv_status(cfg, SCANNING, ndev);

	return 0;

scan_out:
	if (err == BCME_BUSY || err == BCME_NOTREADY) {
		WL_ERR(("Scan err = (%d), busy?%d", err, -EBUSY));
		err = -EBUSY;
	} else if ((err == BCME_EPERM) && cfg->scan_suppressed) {
		WL_ERR(("Scan not permitted due to scan suppress\n"));
		err = -EPERM;
	} else {
		/* For all other fw errors, use a generic error code as return
		 * value to cfg80211 stack
		 */
		err = -EAGAIN;
	}

#define SCAN_EBUSY_RETRY_LIMIT 20
	if (err == -EBUSY) {
		if (busy_count++ > SCAN_EBUSY_RETRY_LIMIT) {
			struct ether_addr bssid;
			s32 ret = 0;
#if defined(DHD_DEBUG) && defined(DHD_FW_COREDUMP)
			dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
#endif /* DHD_DEBUG && DHD_FW_COREDUMP */
			busy_count = 0;
			WL_ERR(("Unusual continuous EBUSY error, %d %d %d %d %d %d %d %d %d\n",
				wl_get_drv_status(cfg, SCANNING, ndev),
				wl_get_drv_status(cfg, SCAN_ABORTING, ndev),
				wl_get_drv_status(cfg, CONNECTING, ndev),
				wl_get_drv_status(cfg, CONNECTED, ndev),
				wl_get_drv_status(cfg, DISCONNECTING, ndev),
				wl_get_drv_status(cfg, AP_CREATING, ndev),
				wl_get_drv_status(cfg, AP_CREATED, ndev),
				wl_get_drv_status(cfg, SENDING_ACT_FRM, ndev),
				wl_get_drv_status(cfg, SENDING_ACT_FRM, ndev)));

#if defined(DHD_DEBUG) && defined(DHD_FW_COREDUMP)
			if (dhdp->memdump_enabled) {
				dhdp->memdump_type = DUMP_TYPE_SCAN_BUSY;
				dhd_bus_mem_dump(dhdp);
			}
#endif /* DHD_DEBUG && DHD_FW_COREDUMP */

			bzero(&bssid, sizeof(bssid));
			if ((ret = wldev_ioctl_get(ndev, WLC_GET_BSSID,
				&bssid, ETHER_ADDR_LEN)) == 0)
				WL_ERR(("FW is connected with " MACDBG "/n",
					MAC2STRDBG(bssid.octet)));
			else
				WL_ERR(("GET BSSID failed with %d\n", ret));

			wl_cfg80211_scan_abort(cfg);

		} else {
			/* Hold the context for 400msec, so that 10 subsequent scans
			* can give a buffer of 4sec which is enough to
			* cover any on-going scan in the firmware
			*/
			WL_DBG(("Enforcing delay for EBUSY case \n"));
			msleep(400);
		}
	} else {
		busy_count = 0;
	}

	wl_clr_drv_status(cfg, SCANNING, ndev);
	if (timer_pending(&cfg->scan_timeout))
		del_timer_sync(&cfg->scan_timeout);
	DHD_OS_SCAN_WAKE_UNLOCK((dhd_pub_t *)(cfg->pub));
	spin_lock_irqsave(&cfg->cfgdrv_lock, flags);
	cfg->scan_request = NULL;
	spin_unlock_irqrestore(&cfg->cfgdrv_lock, flags);

	return err;
}

static s32
#if defined(WL_CFG80211_P2P_DEV_IF)
wl_cfg80211_scan(struct wiphy *wiphy, struct cfg80211_scan_request *request)
#else
wl_cfg80211_scan(struct wiphy *wiphy, struct net_device *ndev,
	struct cfg80211_scan_request *request)
#endif /* WL_CFG80211_P2P_DEV_IF */
{
	s32 err = 0;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
#if defined(WL_CFG80211_P2P_DEV_IF)
	struct net_device *ndev = wdev_to_wlc_ndev(request->wdev, cfg);
#endif /* WL_CFG80211_P2P_DEV_IF */

	WL_DBG(("Enter\n"));
	RETURN_EIO_IF_NOT_UP(cfg);

#ifdef DHD_IFDEBUG
#ifdef WL_CFG80211_P2P_DEV_IF
	PRINT_WDEV_INFO(request->wdev);
#else
	PRINT_WDEV_INFO(ndev);
#endif /* WL_CFG80211_P2P_DEV_IF */
#endif /* DHD_IFDEBUG */

	if (ndev == bcmcfg_to_prmry_ndev(cfg)) {
		if (wl_cfg_multip2p_operational(cfg)) {
			WL_ERR(("wlan0 scan failed, p2p devices are operational"));
			 return -ENODEV;
		}
	}
	err = wl_cfg80211_check_in4way(cfg, ndev, NO_SCAN_IN4WAY,
		WL_EXT_STATUS_SCAN, NULL);
	if (err)
		return err;

	mutex_lock(&cfg->usr_sync);
	err = __wl_cfg80211_scan(wiphy, ndev, request, NULL);
	if (unlikely(err)) {
		WL_ERR(("scan error (%d)\n", err));
	}
	mutex_unlock(&cfg->usr_sync);
#ifdef WL_DRV_AVOID_SCANCACHE
	/* Reset roam cache after successful scan request */
#endif /* WL_DRV_AVOID_SCANCACHE */
	return err;
}

static s32 wl_set_rts(struct net_device *dev, u32 rts_threshold)
{
	s32 err = 0;

	err = wldev_iovar_setint(dev, "rtsthresh", rts_threshold);
	if (unlikely(err)) {
		WL_ERR(("Error (%d)\n", err));
		return err;
	}
	return err;
}

static s32 wl_set_frag(struct net_device *dev, u32 frag_threshold)
{
	s32 err = 0;

	err = wldev_iovar_setint_bsscfg(dev, "fragthresh", frag_threshold, 0);
	if (unlikely(err)) {
		WL_ERR(("Error (%d)\n", err));
		return err;
	}
	return err;
}

static s32 wl_set_retry(struct net_device *dev, u32 retry, bool l)
{
	s32 err = 0;
	u32 cmd = (l ? WLC_SET_LRL : WLC_SET_SRL);

	retry = htod32(retry);
	err = wldev_ioctl_set(dev, cmd, &retry, sizeof(retry));
	if (unlikely(err)) {
		WL_ERR(("cmd (%d) , error (%d)\n", cmd, err));
		return err;
	}
	return err;
}

static s32 wl_cfg80211_set_wiphy_params(struct wiphy *wiphy, u32 changed)
{
	struct bcm_cfg80211 *cfg = (struct bcm_cfg80211 *)wiphy_priv(wiphy);
	struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);
	s32 err = 0;

	RETURN_EIO_IF_NOT_UP(cfg);
	WL_DBG(("Enter\n"));
	if (changed & WIPHY_PARAM_RTS_THRESHOLD &&
		(cfg->conf->rts_threshold != wiphy->rts_threshold)) {
		cfg->conf->rts_threshold = wiphy->rts_threshold;
		err = wl_set_rts(ndev, cfg->conf->rts_threshold);
		if (err != BCME_OK)
			return err;
	}
	if (changed & WIPHY_PARAM_FRAG_THRESHOLD &&
		(cfg->conf->frag_threshold != wiphy->frag_threshold)) {
		cfg->conf->frag_threshold = wiphy->frag_threshold;
		err = wl_set_frag(ndev, cfg->conf->frag_threshold);
		if (err != BCME_OK)
			return err;
	}
	if (changed & WIPHY_PARAM_RETRY_LONG &&
		(cfg->conf->retry_long != wiphy->retry_long)) {
		cfg->conf->retry_long = wiphy->retry_long;
		err = wl_set_retry(ndev, cfg->conf->retry_long, true);
		if (err != BCME_OK)
			return err;
	}
	if (changed & WIPHY_PARAM_RETRY_SHORT &&
		(cfg->conf->retry_short != wiphy->retry_short)) {
		cfg->conf->retry_short = wiphy->retry_short;
		err = wl_set_retry(ndev, cfg->conf->retry_short, false);
		if (err != BCME_OK) {
			return err;
		}
	}

	return err;
}
static chanspec_t
channel_to_chanspec(struct wiphy *wiphy, struct net_device *dev, u32 channel, u32 bw_cap)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	u8 *buf = NULL;
	wl_uint32_list_t *list;
	int err = BCME_OK;
	chanspec_t c = 0, ret_c = 0;
	int bw = 0, tmp_bw = 0;
	int i;
	u32 tmp_c;
	gfp_t kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;
#define LOCAL_BUF_SIZE	1024
	buf = (u8 *) kzalloc(LOCAL_BUF_SIZE, kflags);
	if (!buf) {
		WL_ERR(("buf memory alloc failed\n"));
		goto exit;
	}

	err = wldev_iovar_getbuf_bsscfg(dev, "chanspecs", NULL,
		0, buf, LOCAL_BUF_SIZE, 0, &cfg->ioctl_buf_sync);
	if (err != BCME_OK) {
		WL_ERR(("get chanspecs failed with %d\n", err));
		goto exit;
	}

	list = (wl_uint32_list_t *)(void *)buf;
	for (i = 0; i < dtoh32(list->count); i++) {
		c = dtoh32(list->element[i]);
		if (channel <= CH_MAX_2G_CHANNEL) {
			if (!CHSPEC_IS20(c))
				continue;
			if (channel == CHSPEC_CHANNEL(c)) {
				ret_c = c;
				bw = 20;
				goto exit;
			}
		}
		tmp_c = wf_chspec_ctlchan(c);
		tmp_bw = bw2cap[CHSPEC_BW(c) >> WL_CHANSPEC_BW_SHIFT];
		if (tmp_c != channel)
			continue;

		if ((tmp_bw > bw) && (tmp_bw <= bw_cap)) {
			bw = tmp_bw;
			ret_c = c;
			if (bw == bw_cap)
				goto exit;
		}
	}
exit:
	if (buf)
		kfree(buf);
#undef LOCAL_BUF_SIZE
	WL_INFORM(("return chanspec %x %d\n", ret_c, bw));
	return ret_c;
}

void
wl_cfg80211_ibss_vsie_set_buffer(struct net_device *dev, vndr_ie_setbuf_t *ibss_vsie,
	int ibss_vsie_len)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	if (cfg != NULL && ibss_vsie != NULL) {
		if (cfg->ibss_vsie != NULL) {
			kfree(cfg->ibss_vsie);
		}
		cfg->ibss_vsie = ibss_vsie;
		cfg->ibss_vsie_len = ibss_vsie_len;
	}
}

static void
wl_cfg80211_ibss_vsie_free(struct bcm_cfg80211 *cfg)
{
	/* free & initiralize VSIE (Vendor Specific IE) */
	if (cfg->ibss_vsie != NULL) {
		kfree(cfg->ibss_vsie);
		cfg->ibss_vsie = NULL;
		cfg->ibss_vsie_len = 0;
	}
}

s32
wl_cfg80211_ibss_vsie_delete(struct net_device *dev)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	char *ioctl_buf = NULL;
	s32 ret = BCME_OK, bssidx;

	if (cfg != NULL && cfg->ibss_vsie != NULL) {
		ioctl_buf = kmalloc(WLC_IOCTL_MEDLEN, GFP_KERNEL);
		if (!ioctl_buf) {
			WL_ERR(("ioctl memory alloc failed\n"));
			return -ENOMEM;
		}

		if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
			WL_ERR(("Find index failed\n"));
			ret = BCME_ERROR;
			goto end;
		}
		/* change the command from "add" to "del" */
		strncpy(cfg->ibss_vsie->cmd, "del", VNDR_IE_CMD_LEN - 1);
		cfg->ibss_vsie->cmd[VNDR_IE_CMD_LEN - 1] = '\0';

		ret = wldev_iovar_setbuf_bsscfg(dev, "vndr_ie",
			cfg->ibss_vsie, cfg->ibss_vsie_len,
			ioctl_buf, WLC_IOCTL_MEDLEN, bssidx, &cfg->ioctl_buf_sync);
		WL_ERR(("ret=%d\n", ret));

		if (ret == BCME_OK) {
			/* free & initiralize VSIE */
			kfree(cfg->ibss_vsie);
			cfg->ibss_vsie = NULL;
			cfg->ibss_vsie_len = 0;
		}
end:
		if (ioctl_buf) {
			kfree(ioctl_buf);
		}
	}

	return ret;
}

#ifdef WLAIBSS_MCHAN
static bcm_struct_cfgdev*
bcm_cfg80211_add_ibss_if(struct wiphy *wiphy, char *name)
{
	int err = 0;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct wireless_dev* wdev = NULL;
	struct net_device *new_ndev = NULL;
	struct net_device *primary_ndev = NULL;
	s32 timeout;
	wl_aibss_if_t aibss_if;
	wl_if_event_info *event = NULL;

	if (cfg->ibss_cfgdev != NULL) {
		WL_ERR(("IBSS interface %s already exists\n", name));
		return NULL;
	}

	WL_ERR(("Try to create IBSS interface %s\n", name));
	primary_ndev = bcmcfg_to_prmry_ndev(cfg);
	/* generate a new MAC address for the IBSS interface */
	get_primary_mac(cfg, &cfg->ibss_if_addr);
	cfg->ibss_if_addr.octet[4] ^= 0x40;
	memset(&aibss_if, sizeof(aibss_if), 0);
	memcpy(&aibss_if.addr, &cfg->ibss_if_addr, sizeof(aibss_if.addr));
	aibss_if.chspec = 0;
	aibss_if.len = sizeof(aibss_if);

	cfg->bss_pending_op = TRUE;
	memset(&cfg->if_event_info, 0, sizeof(cfg->if_event_info));
	err = wldev_iovar_setbuf(primary_ndev, "aibss_ifadd", &aibss_if,
		sizeof(aibss_if), cfg->ioctl_buf, WLC_IOCTL_MAXLEN, &cfg->ioctl_buf_sync);
	if (err) {
		WL_ERR(("IOVAR aibss_ifadd failed with error %d\n", err));
		goto fail;
	}
	timeout = wait_event_interruptible_timeout(cfg->netif_change_event,
		!cfg->bss_pending_op, msecs_to_jiffies(MAX_WAIT_TIME));
	if (timeout <= 0 || cfg->bss_pending_op)
		goto fail;

	event = &cfg->if_event_info;
	/* By calling wl_cfg80211_allocate_if (dhd_allocate_if eventually) we give the control
	 * over this net_device interface to dhd_linux, hence the interface is managed by dhd_liux
	 * and will be freed by dhd_detach unless it gets unregistered before that. The
	 * wireless_dev instance new_ndev->ieee80211_ptr associated with this net_device will
	 * be freed by wl_dealloc_netinfo
	 */
	new_ndev = wl_cfg80211_allocate_if(cfg, event->ifidx, event->name,
		event->mac, event->bssidx, event->name);
	if (new_ndev == NULL)
		goto fail;
	wdev = kzalloc(sizeof(*wdev), GFP_KERNEL);
	if (wdev == NULL)
		goto fail;
	wdev->wiphy = wiphy;
	wdev->iftype = NL80211_IFTYPE_ADHOC;
	wdev->netdev = new_ndev;
	new_ndev->ieee80211_ptr = wdev;
	SET_NETDEV_DEV(new_ndev, wiphy_dev(wdev->wiphy));

	/* rtnl lock must have been acquired, if this is not the case, wl_cfg80211_register_if
	* needs to be modified to take one parameter (bool need_rtnl_lock)
	 */
	ASSERT_RTNL();
	if (wl_cfg80211_register_if(cfg, event->ifidx, new_ndev, FALSE) != BCME_OK)
		goto fail;

	wl_alloc_netinfo(cfg, new_ndev, wdev, WL_MODE_IBSS, PM_ENABLE, event->bssidx);
	cfg->ibss_cfgdev = ndev_to_cfgdev(new_ndev);
	WL_ERR(("IBSS interface %s created\n", new_ndev->name));
	return cfg->ibss_cfgdev;

fail:
	WL_ERR(("failed to create IBSS interface %s \n", name));
	cfg->bss_pending_op = FALSE;
	if (new_ndev)
		wl_cfg80211_remove_if(cfg, event->ifidx, new_ndev, FALSE);
	if (wdev)
		kfree(wdev);
	return NULL;
}

static s32
bcm_cfg80211_del_ibss_if(struct wiphy *wiphy, bcm_struct_cfgdev *cfgdev)
{
	int err = 0;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct net_device *ndev = NULL;
	struct net_device *primary_ndev = NULL;
	s32 timeout;

	if (!cfgdev || cfg->ibss_cfgdev != cfgdev || ETHER_ISNULLADDR(&cfg->ibss_if_addr.octet))
		return -EINVAL;
	ndev = (struct net_device *)cfgdev_to_ndev(cfg->ibss_cfgdev);
	primary_ndev = bcmcfg_to_prmry_ndev(cfg);

	cfg->bss_pending_op = TRUE;
	memset(&cfg->if_event_info, 0, sizeof(cfg->if_event_info));
	err = wldev_iovar_setbuf(primary_ndev, "aibss_ifdel", &cfg->ibss_if_addr,
		sizeof(cfg->ibss_if_addr), cfg->ioctl_buf, WLC_IOCTL_MAXLEN, &cfg->ioctl_buf_sync);
	if (err) {
		WL_ERR(("IOVAR aibss_ifdel failed with error %d\n", err));
		goto fail;
	}
	timeout = wait_event_interruptible_timeout(cfg->netif_change_event,
		!cfg->bss_pending_op, msecs_to_jiffies(MAX_WAIT_TIME));
	if (timeout <= 0 || cfg->bss_pending_op) {
		WL_ERR(("timeout in waiting IF_DEL event\n"));
		goto fail;
	}

	wl_cfg80211_remove_if(cfg, cfg->if_event_info.ifidx, ndev, FALSE);
	cfg->ibss_cfgdev = NULL;
	return 0;

fail:
	cfg->bss_pending_op = FALSE;
	return -1;
}
#endif /* WLAIBSS_MCHAN */

#ifdef WLMESH_CFG80211
s32
wl_cfg80211_interface_ops(struct bcm_cfg80211 *cfg,
	struct net_device *ndev, s32 bsscfg_idx,
	enum nl80211_iftype iface_type, s32 del, u8 *addr)
{
	wl_interface_create_t iface;
	s32 ret;
	wl_interface_info_t *info;

	bzero(&iface, sizeof(wl_interface_create_t));

	iface.ver = WL_INTERFACE_CREATE_VER;

	if (iface_type == NL80211_IFTYPE_AP)
		iface.flags = WL_INTERFACE_CREATE_AP;
	else
		iface.flags = WL_INTERFACE_CREATE_STA;

	if (del) {
		ret = wldev_iovar_setbuf(ndev, "interface_remove",
			NULL, 0, cfg->ioctl_buf, WLC_IOCTL_MEDLEN, NULL);
	} else {
		if (addr) {
			memcpy(&iface.mac_addr.octet, addr, ETH_ALEN);
			iface.flags |= WL_INTERFACE_MAC_USE;
		}
		ret = wldev_iovar_getbuf(ndev, "interface_create",
			&iface, sizeof(wl_interface_create_t),
			cfg->ioctl_buf, WLC_IOCTL_MAXLEN, &cfg->ioctl_buf_sync);
		if (ret == 0) {
			/* success */
			info = (wl_interface_info_t *)cfg->ioctl_buf;
			WL_DBG(("wl interface create success!! bssidx:%d \n",
				info->bsscfgidx));
		}
	}

	if (ret < 0)
		WL_ERR(("Interface %s failed!! ret %d\n",
			del ? "remove" : "create", ret));

	return ret;
}
#else
s32
wl_cfg80211_interface_ops(struct bcm_cfg80211 *cfg,
	struct net_device *ndev, s32 bsscfg_idx,
	enum nl80211_iftype iface_type, s32 del, u8 *addr)
{
	s32 ret;
	struct wl_interface_create_v2 iface;
	wl_interface_create_v3_t iface_v3;
	struct wl_interface_info_v1 *info;
	wl_interface_info_v2_t *info_v2;
	enum wl_interface_type iftype;
	uint32 ifflags;
	bool use_iface_info_v2 = false;
	u8 ioctl_buf[WLC_IOCTL_SMLEN];

	if (del) {
		ret = wldev_iovar_setbuf(ndev, "interface_remove",
			NULL, 0, ioctl_buf, sizeof(ioctl_buf), NULL);
		if (unlikely(ret))
			WL_ERR(("Interface remove failed!! ret %d\n", ret));
		return ret;
	}

	/* Interface create */
	bzero(&iface, sizeof(iface));
	/*
	 * flags field is still used along with iftype inorder to support the old version of the
	 * FW work with the latest app changes.
	 */
	if (iface_type == NL80211_IFTYPE_AP) {
		iftype = WL_INTERFACE_TYPE_AP;
		ifflags = WL_INTERFACE_CREATE_AP;
	} else {
		iftype = WL_INTERFACE_TYPE_STA;
		ifflags = WL_INTERFACE_CREATE_STA;
	}
	if (addr) {
		ifflags |= WL_INTERFACE_MAC_USE;
	}

	/* Pass ver = 0 for fetching the interface_create iovar version */
	ret = wldev_iovar_getbuf(ndev, "interface_create",
		&iface, sizeof(struct wl_interface_create_v2),
		ioctl_buf, sizeof(ioctl_buf), NULL);
	if (ret == BCME_UNSUPPORTED) {
		WL_ERR(("interface_create iovar not supported\n"));
		return ret;
	} else if ((ret == 0) && *((uint32 *)ioctl_buf) == WL_INTERFACE_CREATE_VER_3) {
		WL_DBG(("interface_create version 3\n"));
		use_iface_info_v2 = true;
		bzero(&iface_v3, sizeof(wl_interface_create_v3_t));
		iface_v3.ver = WL_INTERFACE_CREATE_VER_3;
		iface_v3.iftype = iftype;
		iface_v3.flags = ifflags;
		if (addr) {
			memcpy(&iface_v3.mac_addr.octet, addr, ETH_ALEN);
		}
		ret = wldev_iovar_getbuf(ndev, "interface_create",
			&iface_v3, sizeof(wl_interface_create_v3_t),
			ioctl_buf, sizeof(ioctl_buf), NULL);
	} else {
#if 0
		/* On any other error, attempt with iovar version 2 */
		WL_DBG(("interface_create version 2. get_ver:%d\n", ret));
		iface.ver = WL_INTERFACE_CREATE_VER_2;
		iface.iftype = iftype;
		iface.flags = ifflags;
		if (addr) {
			memcpy(&iface.mac_addr.octet, addr, ETH_ALEN);
		}
		ret = wldev_iovar_getbuf(ndev, "interface_create",
			&iface, sizeof(struct wl_interface_create_v2),
			ioctl_buf, sizeof(ioctl_buf), NULL);
#endif
	}

	if (unlikely(ret)) {
		WL_ERR(("Interface create failed!! ret %d\n", ret));
		return ret;
	}

	/* success case */
	if (use_iface_info_v2 == true) {
		info_v2 = (wl_interface_info_v2_t *)ioctl_buf;
		ret = info_v2->bsscfgidx;
	} else {
		/* Use v1 struct */
		info = (struct wl_interface_info_v1 *)ioctl_buf;
		ret = info->bsscfgidx;
	}

	WL_DBG(("wl interface create success!! bssidx:%d \n", ret));
	return ret;
}
#endif

bool
wl_customer6_legacy_chip_check(struct bcm_cfg80211 *cfg,
	struct net_device *ndev)
{
	u32 chipnum;
	wlc_rev_info_t revinfo;
	int ret;

	/* Get the device rev info */
	memset(&revinfo, 0, sizeof(revinfo));
	ret = wldev_ioctl_get(ndev, WLC_GET_REVINFO, &revinfo, sizeof(revinfo));
	if (ret < 0) {
		WL_ERR(("%s: GET revinfo FAILED. ret:%d\n", __FUNCTION__, ret));
		ASSERT(0);
		return false;
	}

	WL_DBG(("%s: GET_REVINFO device 0x%x, vendor 0x%x, chipnum 0x%x\n", __FUNCTION__,
		dtoh32(revinfo.deviceid), dtoh32(revinfo.vendorid), dtoh32(revinfo.chipnum)));
	chipnum = revinfo.chipnum;
	if ((chipnum == BCM4350_CHIP_ID) || (chipnum == BCM4355_CHIP_ID) ||
		(chipnum == BCM4345_CHIP_ID) || (chipnum == BCM43430_CHIP_ID) ||
		(chipnum == BCM43362_CHIP_ID)) {
		/* WAR required */
		return true;
	}

	return false;
}

void
wl_bss_iovar_war(struct bcm_cfg80211 *cfg,
	struct net_device *ndev, s32 *val)
{
	u32 chipnum;
	wlc_rev_info_t revinfo;
	int ret;
	bool need_war = false;

	/* Get the device rev info */
	memset(&revinfo, 0, sizeof(revinfo));
	ret = wldev_ioctl_get(ndev, WLC_GET_REVINFO, &revinfo, sizeof(revinfo));
	if (ret < 0) {
		WL_ERR(("%s: GET revinfo FAILED. ret:%d\n", __FUNCTION__, ret));
	} else {
		WL_DBG(("%s: GET_REVINFO device 0x%x, vendor 0x%x, chipnum 0x%x\n", __FUNCTION__,
			dtoh32(revinfo.deviceid), dtoh32(revinfo.vendorid), dtoh32(revinfo.chipnum)));
		chipnum = revinfo.chipnum;
		if ((chipnum == BCM4359_CHIP_ID) || (chipnum == BCM43596_CHIP_ID)) { 
			/* WAR required */
			need_war = true;
		}
	}

	if (wl_customer6_legacy_chip_check(cfg, ndev) || need_war) {
		/* Few firmware branches have issues in bss iovar handling and
		 * that can't be changed since they are in production.
		 */
		if (*val == WLC_AP_IOV_OP_MANUAL_AP_BSSCFG_CREATE) {
			*val = WLC_AP_IOV_OP_MANUAL_STA_BSSCFG_CREATE;
		} else if (*val == WLC_AP_IOV_OP_MANUAL_STA_BSSCFG_CREATE) {
			*val = WLC_AP_IOV_OP_MANUAL_AP_BSSCFG_CREATE;
		} else {
			/* Ignore for other bss enums */
			return;
		}
		WL_ERR(("wl bss %d\n", *val));
	}
}

s32
wl_cfg80211_add_del_bss(struct bcm_cfg80211 *cfg,
	struct net_device *ndev, s32 bsscfg_idx,
	enum nl80211_iftype iface_type, s32 del, u8 *addr)
{
	s32 ret = BCME_OK;
	s32 val = 0;

	struct {
		s32 cfg;
		s32 val;
		struct ether_addr ea;
	} bss_setbuf;

	WL_INFORM(("iface_type:%d del:%d \n", iface_type, del));

	bzero(&bss_setbuf, sizeof(bss_setbuf));

	/* AP=2, STA=3, up=1, down=0, val=-1 */
	if (del) {
		val = WLC_AP_IOV_OP_DELETE;
	} else if (iface_type == NL80211_IFTYPE_AP) {
		/* Add/role change to AP Interface */
		WL_DBG(("Adding AP Interface \n"));
		val = WLC_AP_IOV_OP_MANUAL_AP_BSSCFG_CREATE;
	} else if (iface_type == NL80211_IFTYPE_STATION) {
		/* Add/role change to STA Interface */
		WL_DBG(("Adding STA Interface \n"));
		val = WLC_AP_IOV_OP_MANUAL_STA_BSSCFG_CREATE;
	} else {
		WL_ERR((" add_del_bss NOT supported for IFACE type:0x%x", iface_type));
		return -EINVAL;
	}

	if (!del) {
		wl_bss_iovar_war(cfg, ndev, &val);
	}

	bss_setbuf.cfg = htod32(bsscfg_idx);
	bss_setbuf.val = htod32(val);

	if (addr) {
		memcpy(&bss_setbuf.ea.octet, addr, ETH_ALEN);
	}

	WL_DBG(("wl bss %d bssidx:%d iface:%s \n", val, bsscfg_idx, ndev->name));
	ret = wldev_iovar_setbuf(ndev, "bss", &bss_setbuf, sizeof(bss_setbuf),
		cfg->ioctl_buf, WLC_IOCTL_MAXLEN, &cfg->ioctl_buf_sync);
	if (ret != 0)
		WL_ERR(("'bss %d' failed with %d\n", val, ret));

	return ret;
}

s32
wl_cfg80211_bss_up(struct bcm_cfg80211 *cfg, struct net_device *ndev, s32 bsscfg_idx, s32 bss_up)
{
	s32 ret = BCME_OK;
	s32 val = bss_up ? 1 : 0;

	struct {
		s32 cfg;
		s32 val;
	} bss_setbuf;

	bss_setbuf.cfg = htod32(bsscfg_idx);
	bss_setbuf.val = htod32(val);

	WL_DBG(("wl bss -C %d %s\n", bsscfg_idx, bss_up ? "up" : "down"));
	ret = wldev_iovar_setbuf(ndev, "bss", &bss_setbuf, sizeof(bss_setbuf),
		cfg->ioctl_buf, WLC_IOCTL_MAXLEN, &cfg->ioctl_buf_sync);

	if (ret != 0) {
		WL_ERR(("'bss %d' failed with %d\n", bss_up, ret));
	}

	return ret;
}

bool
wl_cfg80211_bss_isup(struct net_device *ndev, int bsscfg_idx)
{
	s32 result, val;
	bool isup = false;
	s8 getbuf[64];

	/* Check if the BSS is up */
	*(int*)getbuf = -1;
	result = wldev_iovar_getbuf_bsscfg(ndev, "bss", &bsscfg_idx,
		sizeof(bsscfg_idx), getbuf, sizeof(getbuf), 0, NULL);
	if (result != 0) {
		WL_ERR(("'cfg bss -C %d' failed: %d\n", bsscfg_idx, result));
		WL_ERR(("NOTE: this ioctl error is normal "
			"when the BSS has not been created yet.\n"));
	} else {
		val = *(int*)getbuf;
		val = dtoh32(val);
		WL_DBG(("wl bss -C %d = %d\n", bsscfg_idx, val));
		isup = (val ? TRUE : FALSE);
	}
	return isup;
}

static s32
cfg80211_to_wl_iftype(uint16 type, uint16 *role, uint16 *mode)
{
	switch (type) {
		case NL80211_IFTYPE_STATION:
			*role = WLC_E_IF_ROLE_STA;
			*mode = WL_MODE_BSS;
			break;
		case NL80211_IFTYPE_AP:
			*role = WLC_E_IF_ROLE_AP;
			*mode = WL_MODE_AP;
			break;
		case NL80211_IFTYPE_P2P_GO:
			*role = WLC_E_IF_ROLE_P2P_GO;
			*mode = WL_MODE_AP;
			break;
		case NL80211_IFTYPE_P2P_CLIENT:
			*role = WLC_E_IF_ROLE_P2P_CLIENT;
			*mode = WL_MODE_BSS;
			break;
		case NL80211_IFTYPE_MONITOR:
			WL_ERR(("Unsupported mode \n"));
			return BCME_UNSUPPORTED;
		case NL80211_IFTYPE_ADHOC:
			*role = WLC_E_IF_ROLE_IBSS;
			*mode = WL_MODE_IBSS;
			break;
#ifdef WLMESH_CFG80211
		case NL80211_IFTYPE_MESH_POINT:
			*role = WLC_E_IF_ROLE_AP;
			*mode = WL_MODE_MESH;
			break;
#endif
		default:
			WL_ERR(("Unknown interface type:0x%x\n", type));
			return BCME_ERROR;
	}
	return BCME_OK;
}

static s32
wl_if_to_cfg80211_type(uint16 role)
{
	switch (role) {
	case WLC_E_IF_ROLE_STA:
		return NL80211_IFTYPE_STATION;
	case WLC_E_IF_ROLE_AP:
		return NL80211_IFTYPE_AP;
	case WLC_E_IF_ROLE_P2P_GO:
		return NL80211_IFTYPE_P2P_GO;
	case WLC_E_IF_ROLE_P2P_CLIENT:
		return NL80211_IFTYPE_P2P_CLIENT;
	case WLC_E_IF_ROLE_IBSS:
		return NL80211_IFTYPE_ADHOC;
	default:
		WL_ERR(("Unknown interface role:0x%x. Forcing type station\n", role));
		return BCME_ERROR;
	}
}

struct net_device *
wl_cfg80211_post_ifcreate(struct net_device *ndev,
	wl_if_event_info *event, u8 *addr,
	const char *name, bool rtnl_lock_reqd)
{
	struct bcm_cfg80211 *cfg;
	struct net_device *primary_ndev;
	struct net_device *new_ndev = NULL;
	struct wireless_dev *wdev = NULL;
	s32 iface_type;
	s32 ret;
	u16 mode;
	u16 role;
	u8 mac_addr[ETH_ALEN];

	if (!ndev || !event) {
		WL_ERR(("Wrong arg\n"));
		return NULL;
	}

	cfg = wl_get_cfg(ndev);
	if (!cfg) {
		WL_ERR(("cfg null\n"));
		return NULL;
	}

	WL_DBG(("Enter. role:%d ifidx:%d bssidx:%d\n",
		event->role, event->ifidx, event->bssidx));
	if (!event->ifidx || !event->bssidx) {
		/* Fw returned primary idx (0) for virtual interface */
		WL_ERR(("Wrong index. ifidx:%d bssidx:%d \n",
			event->ifidx, event->bssidx));
		return NULL;
	}

	iface_type = wl_if_to_cfg80211_type(event->role);
	if (iface_type < 0) {
		/* Unknown iface type */
		WL_ERR(("Wrong iface type \n"));
		return NULL;
	}

#ifdef WL_EXT_IAPSTA
	if (wl_ext_check_mesh_creating(ndev)) {
		WL_MSG(ndev->name, "change iface_type to NL80211_IFTYPE_MESH_POINT\n");
		iface_type = NL80211_IFTYPE_MESH_POINT;
	}
#endif
	if (cfg80211_to_wl_iftype(iface_type, &role, &mode) < 0) {
		/* Unsupported operating mode */
		WL_ERR(("Unsupported operating mode \n"));
		return NULL;
	}

	WL_DBG(("mac_ptr:%p name:%s role:%d nl80211_iftype:%d " MACDBG "\n",
		addr, name, event->role, iface_type, MAC2STRDBG(event->mac)));
	if (!name) {
		/* If iface name is not provided, use dongle ifname */
		name = event->name;
	}

	if (!addr) {
		/* If mac address is not set, use primary mac with locally administered
		 * bit set.
		 */
		primary_ndev = bcmcfg_to_prmry_ndev(cfg);
		memcpy(mac_addr, primary_ndev->dev_addr, ETH_ALEN);
#ifndef CUSTOMER_HW6
		/* For customer6 builds, use primary mac address for virtual interface */
		mac_addr[0] |= 0x02;
#endif /* CUSTOMER_HW6 */
		addr = mac_addr;
	}

	new_ndev = wl_cfg80211_allocate_if(cfg, event->ifidx,
		name, addr, event->bssidx, event->name);
	if (!new_ndev) {
		WL_ERR(("I/F allocation failed! \n"));
		goto fail;
	} else {
		WL_DBG(("I/F allocation succeeded! ifidx:0x%x bssidx:0x%x \n",
		 event->ifidx, event->bssidx));
	}

	wdev = kzalloc(sizeof(*wdev), GFP_KERNEL);
	if (!wdev) {
		WL_ERR(("wireless_dev alloc failed! \n"));
		goto fail;
	}

	wdev->wiphy = bcmcfg_to_wiphy(cfg);
	wdev->iftype = iface_type;
	new_ndev->ieee80211_ptr = wdev;
	SET_NETDEV_DEV(new_ndev, wiphy_dev(wdev->wiphy));

	/* Check whether mac addr is in sync with fw. If not,
	 * apply it using cur_etheraddr.
	 */
	if (memcmp(addr, event->mac, ETH_ALEN) != 0) {
		ret = wldev_iovar_setbuf_bsscfg(new_ndev, "cur_etheraddr",
			addr, ETH_ALEN, cfg->ioctl_buf, WLC_IOCTL_MAXLEN,
			event->bssidx, &cfg->ioctl_buf_sync);
		if (unlikely(ret)) {
			WL_ERR(("set cur_etheraddr Error (%d)\n", ret));
			goto fail;
		}
		memcpy(new_ndev->dev_addr, addr, ETH_ALEN);
		WL_ERR(("Applying updated mac address to firmware\n"));
	}

	if (wl_cfg80211_register_if(cfg, event->ifidx, new_ndev, rtnl_lock_reqd) != BCME_OK) {
		WL_ERR(("IFACE register failed \n"));
		goto fail;
	}

	/* Initialize with the station mode params */
	ret = wl_alloc_netinfo(cfg, new_ndev, wdev, mode,
		PM_ENABLE, event->bssidx);
	if (unlikely(ret)) {
		WL_ERR(("wl_alloc_netinfo Error (%d)\n", ret));
		goto fail;
	}

	/* Apply the mode & infra setting based on iftype */
	if ((ret = wl_config_ifmode(cfg, new_ndev, iface_type)) < 0) {
		WL_ERR(("config ifmode failure (%d)\n", ret));
		goto fail;
	}

	if (mode == WL_MODE_AP) {
		wl_set_drv_status(cfg, AP_CREATING, new_ndev);
	}

	WL_INFORM(("Host Network Interface (%s) for Secondary I/F created."
		" cfg_iftype:%d wl_role:%d\n", new_ndev->name, iface_type, event->role));

	return new_ndev;

fail:
	if (wdev)
		kfree(wdev);
	if (new_ndev)
		wl_cfg80211_remove_if(cfg, event->ifidx, new_ndev, rtnl_lock_reqd);

	return NULL;
}

void
wl_cfg80211_cleanup_virtual_ifaces(struct net_device *dev, bool rtnl_lock_reqd)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	struct net_info *iter, *next;
	struct net_device *primary_ndev;

	/* Note: This function will clean up only the network interface and host
	 * data structures. The firmware interface clean up will happen in the
	 * during chip reset (ifconfig wlan0 down for built-in drivers/rmmod
	 * context for the module case).
	 */
	primary_ndev = bcmcfg_to_prmry_ndev(cfg);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	for_each_ndev(cfg, iter, next) {
		if (iter->ndev && (iter->ndev != primary_ndev)) {
			WL_DBG(("Cleaning up iface:%s \n", iter->ndev->name));
			wl_cfg80211_post_ifdel(iter->ndev, rtnl_lock_reqd);
		}
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
}

s32
wl_cfg80211_post_ifdel(struct net_device *ndev, bool rtnl_lock_reqd)
{
	int ifidx = -1;
	struct bcm_cfg80211 *cfg;

	if (!ndev || !ndev->ieee80211_ptr) {
		/* No wireless dev done for this interface */
		return -EINVAL;
	}

	cfg = wl_get_cfg(ndev);
	if (!cfg) {
		WL_ERR(("cfg null\n"));
		return BCME_ERROR;
	}
	ifidx = dhd_net2idx(((struct dhd_pub *)(cfg->pub))->info, ndev);
	BCM_REFERENCE(ifidx);
	if (ifidx <= 0) {
		WL_ERR(("Invalid IF idx for iface:%s\n", ndev->name));
		ASSERT(0);
		return BCME_ERROR;
	}
	WL_DBG(("cfg80211_remove for iface:%s \n", ndev->name));
	wl_cfg80211_remove_if(cfg, ifidx, ndev, rtnl_lock_reqd);
	cfg->bss_pending_op = FALSE;

	return BCME_OK;
}

#if defined(WL_VIRTUAL_APSTA) || defined(DUAL_STA_STATIC_IF)
/* Create a Generic Network Interface and initialize it depending up on
 * the interface type
 */
bcm_struct_cfgdev*
wl_cfg80211_create_iface(struct wiphy *wiphy,
	enum nl80211_iftype iface_type,
	u8 *mac_addr, const char *name)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct net_device *new_ndev = NULL;
	struct net_device *primary_ndev = NULL;
	s32 ret = BCME_OK;
	s32 bsscfg_idx = 0;
	u32 timeout;
	wl_if_event_info *event = NULL;
	u8 addr[ETH_ALEN];
	struct net_info *iter, *next;
#ifdef WLMESH_CFG80211
	u16 role = 0, mode = 0;
#endif

	WL_DBG(("Enter\n"));
	if (!name) {
		WL_ERR(("Interface name not provided\n"));
		return NULL;
	}
	else {
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
		for_each_ndev(cfg, iter, next) {
			if (iter->ndev) {
				if (strcmp(iter->ndev->name, name) == 0) {
					WL_ERR(("Interface name, %s exists !\n", iter->ndev->name));
					return NULL;
				}
			}
		}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
	}
	primary_ndev = bcmcfg_to_prmry_ndev(cfg);

	if (likely(!mac_addr)) {
		/* Use primary MAC with the locally administered bit for the
		 *  Secondary STA I/F
		 */
		memcpy(addr, primary_ndev->dev_addr, ETH_ALEN);
		addr[0] |= 0x02;
	} else {
		/* Use the application provided mac address (if any) */
		memcpy(addr, mac_addr, ETH_ALEN);
	}

	if ((iface_type != NL80211_IFTYPE_STATION) && (iface_type != NL80211_IFTYPE_AP)) {
		WL_ERR(("IFACE type:%d not supported. STA "
					"or AP IFACE is only supported\n", iface_type));
		return NULL;
	}

	cfg->bss_pending_op = TRUE;
	memset(&cfg->if_event_info, 0, sizeof(cfg->if_event_info));

	/* De-initialize the p2p discovery interface, if operational */
	if (p2p_is_on(cfg)) {
		WL_DBG(("Disabling P2P Discovery Interface \n"));
#ifdef WL_CFG80211_P2P_DEV_IF
		ret = wl_cfg80211_scan_stop(cfg, bcmcfg_to_p2p_wdev(cfg));
#else
		ret = wl_cfg80211_scan_stop(cfg, cfg->p2p_net);
#endif
		if (unlikely(ret < 0)) {
			CFGP2P_ERR(("P2P scan stop failed, ret=%d\n", ret));
		}

		wl_cfgp2p_disable_discovery(cfg);
		wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE) = 0;
		p2p_on(cfg) = false;
	}

	/*
	 * Intialize the firmware I/F.
	 */
	if (wl_customer6_legacy_chip_check(cfg, primary_ndev)) {
		/* Use bss iovar instead of interface_create iovar */
		ret = BCME_UNSUPPORTED;
	} else {
		ret = wl_cfg80211_interface_ops(cfg, primary_ndev, bsscfg_idx,
			iface_type, 0, addr);
	}
	if (ret == BCME_UNSUPPORTED) {
		/* Use bssidx 1 by default */
		bsscfg_idx = 1;
		if ((ret = wl_cfg80211_add_del_bss(cfg, primary_ndev,
			bsscfg_idx, iface_type, 0, addr)) < 0) {
			goto exit;
		}
	} else if (ret < 0) {
		WL_ERR(("Interface create failed!! ret:%d \n", ret));
		goto exit;
	} else {
		/* Success */
		bsscfg_idx = ret;
	}

	WL_DBG(("Interface created!! bssidx:%d \n", bsscfg_idx));

	/*
	 * Wait till the firmware send a confirmation event back.
	 */
	WL_DBG(("Wait for the FW I/F Event\n"));
	timeout = wait_event_interruptible_timeout(cfg->netif_change_event,
		!cfg->bss_pending_op, msecs_to_jiffies(MAX_WAIT_TIME));
	if (timeout <= 0 || cfg->bss_pending_op) {
		WL_ERR(("ADD_IF event, didn't come. Return \n"));
		goto exit;
	}

	event = &cfg->if_event_info;
#ifdef WLMESH_CFG80211
	cfg80211_to_wl_iftype(iface_type, &role, &mode);
	event->role = role;
#endif

	/*
	 * Since FW operation is successful,we can go ahead with the
	 * the host interface creation.
	 */
	if ((new_ndev = wl_cfg80211_post_ifcreate(primary_ndev,
		event, mac_addr, name, false))) {
		/* Iface post ops successful. Return ndev/wdev ptr */
		return  ndev_to_cfgdev(new_ndev);
	}

exit:
	cfg->bss_pending_op = FALSE;
	return NULL;
}

s32
wl_cfg80211_del_iface(struct wiphy *wiphy, bcm_struct_cfgdev *cfgdev)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct net_device *ndev = NULL;
	s32 ret = BCME_OK;
	s32 bsscfg_idx = 1;
	u32 timeout;
	enum nl80211_iftype iface_type = NL80211_IFTYPE_STATION;

	WL_DBG(("Enter\n"));

	/* If any scan is going on, abort it */
	if (wl_get_drv_status_all(cfg, SCANNING)) {
		WL_DBG(("Scan in progress. Aborting the scan!\n"));
		wl_notify_escan_complete(cfg, cfg->escan_info.ndev, true, true);
	}

	ndev = (struct net_device *)cfgdev_to_ndev(cfgdev);
	cfg->bss_pending_op = TRUE;
	memset(&cfg->if_event_info, 0, sizeof(cfg->if_event_info));

	/* Delete the firmware interface. "interface_remove" command
	 * should go on the interface to be deleted
	 */
	bsscfg_idx = wl_get_bssidx_by_wdev(cfg, ndev->ieee80211_ptr);
	if (bsscfg_idx <= 0) {
		/* validate bsscfgidx */
		WL_ERR(("Wrong bssidx! \n"));
		return -EINVAL;
	}
	WL_DBG(("del interface. bssidx:%d", bsscfg_idx));
	ret = wl_cfg80211_interface_ops(cfg, ndev, bsscfg_idx,
		NL80211_IFTYPE_STATION, 1, NULL);
	if (ret == BCME_UNSUPPORTED) {
		if ((ret = wl_cfg80211_add_del_bss(cfg, ndev,
			bsscfg_idx, iface_type, true, NULL)) < 0) {
			WL_ERR(("DEL bss failed ret:%d \n", ret));
			goto exit;
		}
	} else if (ret < 0) {
		WL_ERR(("Interface DEL failed ret:%d \n", ret));
		goto exit;
	}

	timeout = wait_event_interruptible_timeout(cfg->netif_change_event,
		!cfg->bss_pending_op, msecs_to_jiffies(MAX_WAIT_TIME));
	if (timeout <= 0 || cfg->bss_pending_op) {
		WL_ERR(("timeout in waiting IF_DEL event\n"));
	}

exit:
	ret = wl_cfg80211_post_ifdel(ndev, false);
	if (unlikely(ret)) {
		WL_ERR(("post_ifdel failed\n"));
	}

	return ret;
}
#endif /* defined(WL_VIRTUAL_APSTA) || defined(DUAL_STA_STATIC_IF) */

#ifdef WLMESH_CFG80211
s32 wl_cfg80211_set_sae_password(struct net_device *dev, char* buf, int len)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	sscanf(buf, "%s %d", cfg->sae_password, &cfg->sae_password_len);
	return 0;
}

static s32 wl_cfg80211_join_mesh(
	struct wiphy *wiphy, struct net_device *dev,
	const struct mesh_config *conf,
	const struct mesh_setup *setup)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
	struct ieee80211_channel *chan = setup->chandef.chan;
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION (3, 6, 0))
	struct ieee80211_channel *chan = setup->channel;
#endif
	u32 param[2] = {0, 0};
	s32 err = 0;
	u32 bw_cap = 0;
	u32 beacon_interval = setup->beacon_interval;
	u32 dtim_period = setup->dtim_period;
	size_t join_params_size;
	struct wl_join_params join_params;
	chanspec_t chanspec = 0;

	cfg->channel = ieee80211_frequency_to_channel(chan->center_freq);

	if (wl_get_drv_status(cfg, CONNECTED, dev)) {
		struct wlc_ssid *lssid = (struct wlc_ssid *)wl_read_prof(cfg, dev, WL_PROF_SSID);
		u8 *bssid = (u8 *)wl_read_prof(cfg, dev, WL_PROF_BSSID);
		u32 *channel = (u32 *)wl_read_prof(cfg, dev, WL_PROF_CHAN);
		if ((memcmp(setup->mesh_id, lssid->SSID, lssid->SSID_len) == 0) &&
			(*channel == cfg->channel)) {
			WL_ERR(("MESH connection already existed to " MACDBG "\n",
				MAC2STRDBG((u8 *)wl_read_prof(cfg, dev, WL_PROF_BSSID))));
			return -EISCONN;
		}
		WL_ERR(("Previous connecton existed, please disconnect mesh %s (" MACDBG ") first\n",
			lssid->SSID, MAC2STRDBG(bssid)));
		return -EISCONN;
	}

	if (chan) {
		if (chan->band == IEEE80211_BAND_5GHZ)
			param[0] = WLC_BAND_5G;
		else if (chan->band == IEEE80211_BAND_2GHZ)
			param[0] = WLC_BAND_2G;
		err = wldev_iovar_getint(dev, "bw_cap", param);
		if (unlikely(err)) {
			WL_ERR(("Get bw_cap Failed (%d)\n", err));
			return err;
		}
		bw_cap = param[0];
		chanspec = channel_to_chanspec(wiphy, dev, cfg->channel, bw_cap);
	}

	memset(&join_params, 0, sizeof(join_params));
	memcpy((void *)join_params.ssid.SSID, (void *)setup->mesh_id,
		setup->mesh_id_len);

	join_params.ssid.SSID_len = htod32(setup->mesh_id_len);
	join_params.params.chanspec_list[0] = chanspec;
	join_params.params.chanspec_num = 1;
	wldev_iovar_setint(dev, "chanspec", chanspec);
	join_params_size = sizeof(join_params);

	wldev_iovar_setint(dev, "wpa_auth", WPA_AUTH_DISABLED);
	wldev_iovar_setint(dev, "wsec", 0);

	if (cfg->sae_password_len > 0) {
		wldev_iovar_setint(dev, "mesh_auth_proto", 1);
		wldev_iovar_setint(dev, "wpa_auth", WPA2_AUTH_PSK);
		wldev_iovar_setint(dev, "wsec", AES_ENABLED);
		wldev_iovar_setint(dev, "mfp", WL_MFP_REQUIRED);
		WL_MSG(dev->name, "password=%s, len=%d\n",
			cfg->sae_password, cfg->sae_password_len);
		wldev_iovar_setbuf(dev, "sae_password", cfg->sae_password, cfg->sae_password_len,
			cfg->ioctl_buf, WLC_IOCTL_MAXLEN, NULL);
	} else {
		wldev_iovar_setint(dev, "mesh_auth_proto", 0);
		wldev_iovar_setint(dev, "mfp", WL_MFP_NONE);
	}

	if (beacon_interval) {
		if ((err = wldev_ioctl_set(dev, WLC_SET_BCNPRD,
			&beacon_interval, sizeof(s32))) < 0) {
			WL_ERR(("Beacon Interval Set Error, %d\n", err));
			return err;
		}
	}

	if (dtim_period) {
		if ((err = wldev_ioctl_set(dev, WLC_SET_DTIMPRD,
			&dtim_period, sizeof(s32))) < 0) {
			WL_ERR(("DTIM Interval Set Error, %d\n", err));
			return err;
		}
	}
	wldev_iovar_setint(dev, "mpc", 0);

	WL_ERR(("JOIN %s on channel %d with chanspec 0x%4x\n",
				join_params.ssid.SSID, cfg->channel, chanspec));

	err = wldev_ioctl_set(dev, WLC_SET_SSID, &join_params,
		join_params_size);

	if (unlikely(err)) {
		WL_ERR(("Error (%d)\n", err));
		return err;
	}

	wl_update_prof(cfg, dev, NULL, &join_params.ssid, WL_PROF_SSID);
	wl_update_prof(cfg, dev, NULL, &cfg->channel, WL_PROF_CHAN);
	return err;
}


static s32 wl_cfg80211_leave_mesh(
	struct wiphy *wiphy, struct net_device *dev)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	s32 err = 0;
	scb_val_t scbval;
	u8 *curbssid;

	RETURN_EIO_IF_NOT_UP(cfg);
	wl_link_down(cfg);

	WL_ERR(("Leave MESH\n"));
	curbssid = wl_read_prof(cfg, dev, WL_PROF_BSSID);
	wl_set_drv_status(cfg, DISCONNECTING, dev);
	scbval.val = 0;
	memcpy(&scbval.ea, curbssid, ETHER_ADDR_LEN);
	err = wldev_ioctl_set(dev, WLC_DISASSOC, &scbval,
		sizeof(scb_val_t));
	if (unlikely(err)) {
		wl_clr_drv_status(cfg, DISCONNECTING, dev);
		WL_ERR(("error(%d)\n", err));
		return err;
	}
	memset(cfg->sae_password, 0, SAE_MAX_PASSWD_LEN);
	cfg->sae_password_len = 0;

	return err;
}
#endif /* WLMESH_CFG80211 */

static s32
wl_cfg80211_join_ibss(struct wiphy *wiphy, struct net_device *dev,
	struct cfg80211_ibss_params *params)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct cfg80211_bss *bss;
	struct ieee80211_channel *chan;
	struct wl_join_params join_params;
	int scan_suppress;
	struct cfg80211_ssid ssid;
	s32 scan_retry = 0;
	s32 err = 0;
	size_t join_params_size;
	chanspec_t chanspec = 0;
	u32 param[2] = {0, 0};
	u32 bw_cap = 0;

	WL_TRACE(("In\n"));
	RETURN_EIO_IF_NOT_UP(cfg);
	WL_INFORM(("JOIN BSSID:" MACDBG "\n", MAC2STRDBG(params->bssid)));
	if (!params->ssid || params->ssid_len <= 0 ||
			params->ssid_len > DOT11_MAX_SSID_LEN) {
		WL_ERR(("Invalid parameter\n"));
		return -EINVAL;
	}
#if defined(WL_CFG80211_P2P_DEV_IF)
	chan = params->chandef.chan;
#else
	chan = params->channel;
#endif /* WL_CFG80211_P2P_DEV_IF */
	if (chan)
		cfg->channel = ieee80211_frequency_to_channel(chan->center_freq);
	if (wl_get_drv_status(cfg, CONNECTED, dev)) {
		struct wlc_ssid *lssid = (struct wlc_ssid *)wl_read_prof(cfg, dev, WL_PROF_SSID);
		u8 *bssid = (u8 *)wl_read_prof(cfg, dev, WL_PROF_BSSID);
		u32 *channel = (u32 *)wl_read_prof(cfg, dev, WL_PROF_CHAN);
		if (!params->bssid || ((memcmp(params->bssid, bssid, ETHER_ADDR_LEN) == 0) &&
			(memcmp(params->ssid, lssid->SSID, lssid->SSID_len) == 0) &&
			(*channel == cfg->channel))) {
			WL_ERR(("Connection already existed to " MACDBG "\n",
				MAC2STRDBG((u8 *)wl_read_prof(cfg, dev, WL_PROF_BSSID))));
			return -EISCONN;
		}
		WL_ERR(("Ignore Previous connecton to %s (" MACDBG ")\n",
			lssid->SSID, MAC2STRDBG(bssid)));
	}

	/* remove the VSIE */
	wl_cfg80211_ibss_vsie_delete(dev);

	bss = cfg80211_get_ibss(wiphy, NULL, params->ssid, params->ssid_len);
	if (!bss) {
		if (IBSS_INITIAL_SCAN_ALLOWED == TRUE) {
			memcpy(ssid.ssid, params->ssid, params->ssid_len);
			ssid.ssid_len = params->ssid_len;
			do {
				if (unlikely
					(__wl_cfg80211_scan(wiphy, dev, NULL, &ssid) ==
					 -EBUSY)) {
					wl_delay(150);
				} else {
					break;
				}
			} while (++scan_retry < WL_SCAN_RETRY_MAX);

			/* rtnl lock code is removed here. don't see why rtnl lock
			 * needs to be released.
			 */

			/* wait 4 secons till scan done.... */
			schedule_timeout_interruptible(msecs_to_jiffies(4000));

			bss = cfg80211_get_ibss(wiphy, NULL,
				params->ssid, params->ssid_len);
		}
	}
	if (bss && ((IBSS_COALESCE_ALLOWED == TRUE) ||
		((IBSS_COALESCE_ALLOWED == FALSE) && params->bssid &&
		!memcmp(bss->bssid, params->bssid, ETHER_ADDR_LEN)))) {
		cfg->ibss_starter = false;
		WL_DBG(("Found IBSS\n"));
	} else {
		cfg->ibss_starter = true;
	}

	if (bss) {
		CFG80211_PUT_BSS(wiphy, bss);
	}

	if (chan) {
		if (chan->band == IEEE80211_BAND_5GHZ)
			param[0] = WLC_BAND_5G;
		else if (chan->band == IEEE80211_BAND_2GHZ)
			param[0] = WLC_BAND_2G;
		err = wldev_iovar_getint(dev, "bw_cap", param);
		if (unlikely(err)) {
			WL_ERR(("Get bw_cap Failed (%d)\n", err));
			return err;
		}
		bw_cap = param[0];
		chanspec = channel_to_chanspec(wiphy, dev, cfg->channel, bw_cap);
	}
	/*
	 * Join with specific BSSID and cached SSID
	 * If SSID is zero join based on BSSID only
	 */
	memset(&join_params, 0, sizeof(join_params));
	memcpy((void *)join_params.ssid.SSID, (void *)params->ssid,
		params->ssid_len);
	join_params.ssid.SSID_len = htod32(params->ssid_len);
	if (params->bssid) {
		memcpy(&join_params.params.bssid, params->bssid, ETHER_ADDR_LEN);
		err = wldev_ioctl_set(dev, WLC_SET_DESIRED_BSSID, &join_params.params.bssid,
			ETHER_ADDR_LEN);
		if (unlikely(err)) {
			WL_ERR(("Error (%d)\n", err));
			return err;
		}
	} else
		memset(&join_params.params.bssid, 0, ETHER_ADDR_LEN);

	if (IBSS_INITIAL_SCAN_ALLOWED == FALSE) {
		scan_suppress = TRUE;
		/* Set the SCAN SUPPRESS Flag in the firmware to skip join scan */
		err = wldev_ioctl_set(dev, WLC_SET_SCANSUPPRESS,
			&scan_suppress, sizeof(int));
		if (unlikely(err)) {
			WL_ERR(("Scan Suppress Setting Failed (%d)\n", err));
			return err;
		}
	}

	join_params.params.chanspec_list[0] = chanspec;
	join_params.params.chanspec_num = 1;
	wldev_iovar_setint(dev, "chanspec", chanspec);
	join_params_size = sizeof(join_params);

	/* Disable Authentication, IBSS will add key if it required */
	wldev_iovar_setint(dev, "wpa_auth", WPA_AUTH_DISABLED);
	wldev_iovar_setint(dev, "wsec", 0);

	err = wldev_ioctl_set(dev, WLC_SET_SSID, &join_params,
		join_params_size);
	if (unlikely(err)) {
		WL_ERR(("Error (%d)\n", err));
		return err;
	}

	if (IBSS_INITIAL_SCAN_ALLOWED == FALSE) {
		scan_suppress = FALSE;
		/* Reset the SCAN SUPPRESS Flag */
		err = wldev_ioctl_set(dev, WLC_SET_SCANSUPPRESS,
			&scan_suppress, sizeof(int));
		if (unlikely(err)) {
			WL_ERR(("Reset Scan Suppress Flag Failed (%d)\n", err));
			return err;
		}
	}
	wl_update_prof(cfg, dev, NULL, &join_params.ssid, WL_PROF_SSID);
	wl_update_prof(cfg, dev, NULL, &cfg->channel, WL_PROF_CHAN);
#ifdef WL_RELMCAST
	cfg->rmc_event_seq = 0; /* initialize rmcfail sequence */
#endif /* WL_RELMCAST */
	return err;
}

static s32 wl_cfg80211_leave_ibss(struct wiphy *wiphy, struct net_device *dev)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	s32 err = 0;
	scb_val_t scbval;
	u8 *curbssid;

	RETURN_EIO_IF_NOT_UP(cfg);
	wl_link_down(cfg);

	WL_ERR(("Leave IBSS\n"));
	curbssid = wl_read_prof(cfg, dev, WL_PROF_BSSID);
	wl_set_drv_status(cfg, DISCONNECTING, dev);
	scbval.val = 0;
	memcpy(&scbval.ea, curbssid, ETHER_ADDR_LEN);
	err = wldev_ioctl_set(dev, WLC_DISASSOC, &scbval,
		sizeof(scb_val_t));
	if (unlikely(err)) {
		wl_clr_drv_status(cfg, DISCONNECTING, dev);
		WL_ERR(("error(%d)\n", err));
		return err;
	}

	/* remove the VSIE */
	wl_cfg80211_ibss_vsie_delete(dev);

	return err;
}

#ifdef MFP
static int wl_cfg80211_get_rsn_capa(bcm_tlv_t *wpa2ie, u8** rsn_cap)
{
	u16 suite_count;
	wpa_suite_mcast_t *mcast;
	wpa_suite_ucast_t *ucast;
	u16 len;
	wpa_suite_auth_key_mgmt_t *mgmt;

	if (!wpa2ie)
		return -1;

	len = wpa2ie->len;
	mcast = (wpa_suite_mcast_t *)&wpa2ie->data[WPA2_VERSION_LEN];
	if ((len -= WPA_SUITE_LEN) <= 0)
		return BCME_BADLEN;
	ucast = (wpa_suite_ucast_t *)&mcast[1];
	suite_count = ltoh16_ua(&ucast->count);
	if ((suite_count > NL80211_MAX_NR_CIPHER_SUITES) ||
		(len -= (WPA_IE_SUITE_COUNT_LEN +
		(WPA_SUITE_LEN * suite_count))) <= 0)
		return BCME_BADLEN;

	mgmt = (wpa_suite_auth_key_mgmt_t *)&ucast->list[suite_count];
	suite_count = ltoh16_ua(&mgmt->count);

	if ((suite_count > NL80211_MAX_NR_CIPHER_SUITES) ||
		(len -= (WPA_IE_SUITE_COUNT_LEN +
		(WPA_SUITE_LEN * suite_count))) >= RSN_CAP_LEN) {
		rsn_cap[0] = (u8 *)&mgmt->list[suite_count];
	} else
		return BCME_BADLEN;

	return 0;
}
#endif /* MFP */

static s32
wl_set_wpa_version(struct net_device *dev, struct cfg80211_connect_params *sme)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	struct wl_security *sec;
	s32 val = 0;
	s32 err = 0;
	s32 bssidx;

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find p2p index from wdev(%p) failed\n", dev->ieee80211_ptr));
		return BCME_ERROR;
	}

	if (sme->crypto.wpa_versions & NL80211_WPA_VERSION_1)
		val = WPA_AUTH_PSK |
			WPA_AUTH_UNSPECIFIED;
	else if (sme->crypto.wpa_versions & NL80211_WPA_VERSION_2)
		val = WPA2_AUTH_PSK|
			WPA2_AUTH_UNSPECIFIED;
	else
		val = WPA_AUTH_DISABLED;

	if (is_wps_conn(sme))
		val = WPA_AUTH_DISABLED;

#ifdef BCMWAPI_WPI
	if (sme->crypto.wpa_versions & NL80211_WAPI_VERSION_1) {
		WL_DBG((" * wl_set_wpa_version, set wpa_auth"
			" to WPA_AUTH_WAPI 0x400"));
		val = WAPI_AUTH_PSK | WAPI_AUTH_UNSPECIFIED;
	}
#endif
	WL_DBG(("setting wpa_auth to 0x%0x\n", val));
	err = wldev_iovar_setint_bsscfg(dev, "wpa_auth", val, bssidx);
	if (unlikely(err)) {
		WL_ERR(("set wpa_auth failed (%d)\n", err));
		return err;
	}
	sec = wl_read_prof(cfg, dev, WL_PROF_SEC);
	sec->wpa_versions = sme->crypto.wpa_versions;
	return err;
}
 
#ifdef BCMWAPI_WPI
static s32
wl_set_set_wapi_ie(struct net_device *dev, struct cfg80211_connect_params *sme)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	s32 err = 0;
	s32 bssidx;

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find p2p index from wdev(%p) failed\n", dev->ieee80211_ptr));
		return BCME_ERROR;
	}

	WL_DBG((" %s \n", __FUNCTION__));

	if (sme->crypto.wpa_versions & NL80211_WAPI_VERSION_1) {
		err = wldev_iovar_setbuf_bsscfg(dev, "wapiie", (void *)sme->ie, sme->ie_len,
			cfg->ioctl_buf, WLC_IOCTL_MAXLEN, bssidx, &cfg->ioctl_buf_sync);

		if (unlikely(err)) {
			WL_ERR(("===> set_wapi_ie Error (%d)\n", err));
			return err;
		}
	} else
		WL_DBG((" * skip \n"));
	return err;
}
#endif /* BCMWAPI_WPI */

static s32
wl_set_auth_type(struct net_device *dev, struct cfg80211_connect_params *sme)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	struct wl_security *sec;
	s32 val = 0;
	s32 err = 0;
	s32 bssidx;

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find p2p index from wdev(%p) failed\n", dev->ieee80211_ptr));
		return BCME_ERROR;
	}

	switch (sme->auth_type) {
	case NL80211_AUTHTYPE_OPEN_SYSTEM:
		val = WL_AUTH_OPEN_SYSTEM;
		WL_DBG(("open system\n"));
		break;
	case NL80211_AUTHTYPE_SHARED_KEY:
		val = WL_AUTH_SHARED_KEY;
		WL_DBG(("shared key\n"));
		break;
	case NL80211_AUTHTYPE_AUTOMATIC:
		val = WL_AUTH_OPEN_SHARED;
		WL_DBG(("automatic\n"));
		break;
#ifdef WL_SAE
	case NL80211_AUTHTYPE_SAE:
		val = WL_AUTH_OPEN_SHARED;
		WL_DBG(("sae\n"));
		break;
#endif /* WL_SAE */
	default:
		val = 2;
		WL_ERR(("invalid auth type (%d)\n", sme->auth_type));
		break;
	}

	err = wldev_iovar_setint_bsscfg(dev, "auth", val, bssidx);
	if (unlikely(err)) {
		WL_ERR(("set auth failed (%d)\n", err));
		return err;
	}
	sec = wl_read_prof(cfg, dev, WL_PROF_SEC);
	sec->auth_type = sme->auth_type;
	return err;
}

static s32
wl_set_set_cipher(struct net_device *dev, struct cfg80211_connect_params *sme)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	struct wl_security *sec;
	s32 pval = 0;
	s32 gval = 0;
	s32 err = 0;
	s32 wsec_val = 0;
	s32 bssidx;
#ifdef BCMWAPI_WPI
	s32 val = 0;
#endif

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find p2p index from wdev(%p) failed\n", dev->ieee80211_ptr));
		return BCME_ERROR;
	}

	if (sme->crypto.n_ciphers_pairwise) {
		switch (sme->crypto.ciphers_pairwise[0]) {
		case WLAN_CIPHER_SUITE_WEP40:
		case WLAN_CIPHER_SUITE_WEP104:
			pval = WEP_ENABLED;
			break;
		case WLAN_CIPHER_SUITE_TKIP:
			pval = TKIP_ENABLED;
			break;
		case WLAN_CIPHER_SUITE_CCMP:
		case WLAN_CIPHER_SUITE_AES_CMAC:
			pval = AES_ENABLED;
			break;
#ifdef BCMWAPI_WPI
		case WLAN_CIPHER_SUITE_SMS4:
			val = SMS4_ENABLED;
			pval = SMS4_ENABLED;
			break;
#endif
		default:
			WL_ERR(("invalid cipher pairwise (%d)\n",
				sme->crypto.ciphers_pairwise[0]));
			return -EINVAL;
		}
	}
	if (sme->crypto.cipher_group) {
		switch (sme->crypto.cipher_group) {
		case WLAN_CIPHER_SUITE_WEP40:
		case WLAN_CIPHER_SUITE_WEP104:
			gval = WEP_ENABLED;
			break;
		case WLAN_CIPHER_SUITE_TKIP:
			gval = TKIP_ENABLED;
			break;
		case WLAN_CIPHER_SUITE_CCMP:
			gval = AES_ENABLED;
			break;
		case WLAN_CIPHER_SUITE_AES_CMAC:
			gval = AES_ENABLED;
			break;
#ifdef BCMWAPI_WPI
		case WLAN_CIPHER_SUITE_SMS4:
			val = SMS4_ENABLED;
			gval = SMS4_ENABLED;
			break;
#endif
		default:
			WL_ERR(("invalid cipher group (%d)\n",
				sme->crypto.cipher_group));
			return -EINVAL;
		}
	}

	WL_DBG(("pval (%d) gval (%d)\n", pval, gval));

	if (is_wps_conn(sme)) {
		if (sme->privacy)
			err = wldev_iovar_setint_bsscfg(dev, "wsec", 4, bssidx);
		else
			/* WPS-2.0 allows no security */
			err = wldev_iovar_setint_bsscfg(dev, "wsec", 0, bssidx);
	} else {
#ifdef BCMWAPI_WPI
		if (sme->crypto.cipher_group == WLAN_CIPHER_SUITE_SMS4) {
			WL_DBG((" NO, is_wps_conn, WAPI set to SMS4_ENABLED"));
			err = wldev_iovar_setint_bsscfg(dev, "wsec", val, bssidx);
		} else {
#endif
			WL_DBG((" NO, is_wps_conn, Set pval | gval to WSEC"));
			wsec_val = pval | gval;

			WL_DBG((" Set WSEC to fW 0x%x \n", wsec_val));
			err = wldev_iovar_setint_bsscfg(dev, "wsec",
				wsec_val, bssidx);
#ifdef BCMWAPI_WPI
		}
#endif
	}
	if (unlikely(err)) {
		WL_ERR(("error (%d)\n", err));
		return err;
	}

	sec = wl_read_prof(cfg, dev, WL_PROF_SEC);
	sec->cipher_pairwise = sme->crypto.ciphers_pairwise[0];
	sec->cipher_group = sme->crypto.cipher_group;

	return err;
}

#ifdef MFP
static s32
wl_cfg80211_set_mfp(struct bcm_cfg80211 *cfg,
	struct net_device *dev,
	struct cfg80211_connect_params *sme)
{
	s32 mfp = WL_MFP_NONE;
	s32 current_mfp = WL_MFP_NONE;
	bcm_tlv_t *wpa2_ie;
	u8* rsn_cap = NULL;
	bool fw_support = false;
	int err, count = 0;
	u8 *eptr = NULL, *ptr = NULL;
	u8* group_mgmt_cs = NULL;
	wpa_pmkid_list_t* pmkid = NULL;

	if (!sme) {
		/* No connection params from userspace, Do nothing. */
		return 0;
	}

	/* Check fw support and retreive current mfp val */
	err = wldev_iovar_getint(dev, "mfp", &current_mfp);
	if (!err) {
		fw_support = true;
	}

	/* Parse the wpa2ie to decode the MFP capablity */
	if (((wpa2_ie = bcm_parse_tlvs((u8 *)sme->ie, sme->ie_len,
			DOT11_MNG_RSN_ID)) != NULL) &&
			(wl_cfg80211_get_rsn_capa(wpa2_ie, &rsn_cap) == 0)) {
		/* Check for MFP cap in the RSN capability field */
		if (rsn_cap[0] & RSN_CAP_MFPR) {
			mfp = WL_MFP_REQUIRED;
		} else if (rsn_cap[0] & RSN_CAP_MFPC) {
			mfp = WL_MFP_CAPABLE;
		}

		/*
		 * eptr --> end/last byte addr of wpa2_ie
		 * ptr --> to keep track of current/required byte addr
		 */
		eptr = (u8*)wpa2_ie + (wpa2_ie->len + TLV_HDR_LEN);
		/* pointing ptr to the next byte after rns_cap */
		ptr = (u8*)rsn_cap + RSN_CAP_LEN;
		if (mfp && (eptr - ptr) >= WPA2_PMKID_COUNT_LEN) {
			/* pmkid now to point to 1st byte addr of pmkid in wpa2_ie */
			pmkid = (wpa_pmkid_list_t*)ptr;
			count = pmkid->count.low | (pmkid->count.high << 8);
			/* ptr now to point to last byte addr of pmkid */
			ptr = (u8*)pmkid + (count * WPA2_PMKID_LEN
				+ WPA2_PMKID_COUNT_LEN);
			if ((eptr - ptr) >= WPA_SUITE_LEN) {
				/* group_mgmt_cs now to point to first byte addr of bip */
				group_mgmt_cs = ptr;
			}
		}
	}

	WL_DBG((" mfp:%d wpa2_ie ptr:%p rsn_cap 0x%x%x fw mfp support:%d\n",
		mfp, wpa2_ie, rsn_cap[0], rsn_cap[1], fw_support));

	if (fw_support == false) {
		if (mfp == WL_MFP_REQUIRED) {
			/* if mfp > 0, mfp capability set in wpa ie, but
			 * FW indicated error for mfp. Propagate the error up.
			 */
			WL_ERR(("mfp capability found in wpaie. But fw doesn't "
				"seem to support MFP\n"));
			return -EINVAL;
		} else {
			/* Firmware doesn't support mfp. But since connection request
			 * is for non-mfp case, don't bother.
			 */
			return 0;
		}
	} else if (mfp != current_mfp) {
		err = wldev_iovar_setint(dev, "mfp", mfp);
		if (unlikely(err)) {
			WL_ERR(("mfp (%d) set failed ret:%d \n", mfp, err));
			return err;
		}
		WL_DBG(("mfp set to 0x%x \n", mfp));
	}

	if (group_mgmt_cs && bcmp((const uint8 *)WPA2_OUI,
			group_mgmt_cs, (WPA_SUITE_LEN - 1)) == 0) {
		WL_DBG(("BIP is found\n"));
		err = wldev_iovar_setbuf(dev, "bip",
				group_mgmt_cs, WPA_SUITE_LEN, cfg->ioctl_buf,
				WLC_IOCTL_SMLEN, &cfg->ioctl_buf_sync);
		/*
		 * Dont return failure for unsupported cases
		 * of bip iovar for backward compatibility
		 */
		if (err != BCME_UNSUPPORTED && err < 0) {
			WL_ERR(("bip set error (%d)\n", err));
			return err;
		}
	}

	return 0;
}
#endif /* MFP */

static s32
wl_set_key_mgmt(struct net_device *dev, struct cfg80211_connect_params *sme)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	struct wl_security *sec;
	s32 val = 0;
	s32 err = 0;
	s32 bssidx;

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find p2p index from wdev(%p) failed\n", dev->ieee80211_ptr));
		return BCME_ERROR;
	}

	if (sme->crypto.n_akm_suites) {
		err = wldev_iovar_getint(dev, "wpa_auth", &val);
		if (unlikely(err)) {
			WL_ERR(("could not get wpa_auth (%d)\n", err));
			return err;
		}
		if (val & (WPA_AUTH_PSK |
			WPA_AUTH_UNSPECIFIED)) {
			switch (sme->crypto.akm_suites[0]) {
			case WLAN_AKM_SUITE_8021X:
				val = WPA_AUTH_UNSPECIFIED;
				break;
			case WLAN_AKM_SUITE_PSK:
				val = WPA_AUTH_PSK;
				break;
			default:
				WL_ERR(("invalid akm suite (0x%x)\n",
					sme->crypto.akm_suites[0]));
				return -EINVAL;
			}
		} else if (val & (WPA2_AUTH_PSK |
			WPA2_AUTH_UNSPECIFIED)) {
			switch (sme->crypto.akm_suites[0]) {
			case WLAN_AKM_SUITE_8021X:
				val = WPA2_AUTH_UNSPECIFIED;
				break;
#ifdef MFP
#ifdef CUSTOMER_HW6
			case WL_AKM_SUITE_SHA256_1X:
				if (wl_customer6_legacy_chip_check(cfg,	dev)) {
					val = WPA2_AUTH_UNSPECIFIED;
				} else {
					val = WPA2_AUTH_1X_SHA256;
				}
				break;
			case WL_AKM_SUITE_SHA256_PSK:
				if (wl_customer6_legacy_chip_check(cfg,	dev)) {
					val = WPA2_AUTH_PSK;
				} else {
					val = WPA2_AUTH_PSK_SHA256;
				}
				break;
#else
			case WL_AKM_SUITE_SHA256_1X:
				val = WPA2_AUTH_1X_SHA256;
				break;
			case WL_AKM_SUITE_SHA256_PSK:
				val = WPA2_AUTH_PSK_SHA256;
				break;
#endif /* CUSTOMER_HW6 */
#endif /* MFP */
#if defined(WLFBT) && defined(WLAN_AKM_SUITE_FT_8021X)
			case WLAN_AKM_SUITE_FT_8021X:
				val = WPA2_AUTH_UNSPECIFIED | WPA2_AUTH_FT;
				break;
#endif
#if defined(WLFBT) && defined(WLAN_AKM_SUITE_FT_PSK)
			case WLAN_AKM_SUITE_FT_PSK:
				val = WPA2_AUTH_PSK | WPA2_AUTH_FT;
				break;
#endif
			case WLAN_AKM_SUITE_PSK:
				val = WPA2_AUTH_PSK;
				break;
#ifdef WL_SAE
			case WLAN_AKM_SUITE_SAE:
				val = WPA3_AUTH_SAE_PSK;
				break;
#endif /* WL_SAE */
			default:
				WL_ERR(("invalid akm suite (0x%x)\n",
					sme->crypto.akm_suites[0]));
				return -EINVAL;
			}
		}
#ifdef BCMWAPI_WPI
		else if (val & (WAPI_AUTH_PSK | WAPI_AUTH_UNSPECIFIED)) {
			switch (sme->crypto.akm_suites[0]) {
			case WLAN_AKM_SUITE_WAPI_CERT:
				val = WAPI_AUTH_UNSPECIFIED;
				break;
			case WLAN_AKM_SUITE_WAPI_PSK:
				val = WAPI_AUTH_PSK;
				break;
			default:
				WL_ERR(("invalid cipher group (%d)\n",
					sme->crypto.cipher_group));
				return -EINVAL;
			}
		}
#endif

#ifdef MFP
		if ((err = wl_cfg80211_set_mfp(cfg, dev, sme)) < 0) {
			WL_ERR(("MFP set failed err:%d\n", err));
			return -EINVAL;
		}
#endif /* MFP */

		WL_DBG(("setting wpa_auth to 0x%x\n", val));
		err = wldev_iovar_setint_bsscfg(dev, "wpa_auth", val, bssidx);
		if (unlikely(err)) {
			WL_ERR(("could not set wpa_auth (0x%x)\n", err));
			return err;
		}
	}
	sec = wl_read_prof(cfg, dev, WL_PROF_SEC);
	sec->wpa_auth = sme->crypto.akm_suites[0];

	return err;
}

static s32
wl_set_set_sharedkey(struct net_device *dev,
	struct cfg80211_connect_params *sme)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	struct wl_security *sec;
	struct wl_wsec_key key;
	s32 val;
	s32 err = 0;
	s32 bssidx;

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find p2p index from wdev(%p) failed\n", dev->ieee80211_ptr));
		return BCME_ERROR;
	}

	WL_DBG(("key len (%d)\n", sme->key_len));
	if (sme->key_len) {
		sec = wl_read_prof(cfg, dev, WL_PROF_SEC);
		WL_DBG(("wpa_versions 0x%x cipher_pairwise 0x%x\n",
			sec->wpa_versions, sec->cipher_pairwise));
		if (!(sec->wpa_versions & (NL80211_WPA_VERSION_1 |
#ifdef BCMWAPI_WPI
			NL80211_WPA_VERSION_2 | NL80211_WAPI_VERSION_1)) &&
#else
			NL80211_WPA_VERSION_2)) &&
#endif
			(sec->cipher_pairwise & (WLAN_CIPHER_SUITE_WEP40 |
#ifdef BCMWAPI_WPI
		    WLAN_CIPHER_SUITE_WEP104 | WLAN_CIPHER_SUITE_SMS4)))
#else
		    WLAN_CIPHER_SUITE_WEP104)))
#endif
		{
			memset(&key, 0, sizeof(key));
			key.len = (u32) sme->key_len;
			key.index = (u32) sme->key_idx;
			if (unlikely(key.len > sizeof(key.data))) {
				WL_ERR(("Too long key length (%u)\n", key.len));
				return -EINVAL;
			}
			memcpy(key.data, sme->key, key.len);
			key.flags = WL_PRIMARY_KEY;
			switch (sec->cipher_pairwise) {
			case WLAN_CIPHER_SUITE_WEP40:
				key.algo = CRYPTO_ALGO_WEP1;
				break;
			case WLAN_CIPHER_SUITE_WEP104:
				key.algo = CRYPTO_ALGO_WEP128;
				break;
#ifdef BCMWAPI_WPI
			case WLAN_CIPHER_SUITE_SMS4:
				key.algo = CRYPTO_ALGO_SMS4;
				break;
#endif
			default:
				WL_ERR(("Invalid algorithm (%d)\n",
					sme->crypto.ciphers_pairwise[0]));
				return -EINVAL;
			}
			/* Set the new key/index */
			WL_DBG(("key length (%d) key index (%d) algo (%d)\n",
				key.len, key.index, key.algo));
			WL_DBG(("key \"%s\"\n", key.data));
			swap_key_from_BE(&key);
			err = wldev_iovar_setbuf_bsscfg(dev, "wsec_key", &key, sizeof(key),
				cfg->ioctl_buf, WLC_IOCTL_MAXLEN, bssidx, &cfg->ioctl_buf_sync);
			if (unlikely(err)) {
				WL_ERR(("WLC_SET_KEY error (%d)\n", err));
				return err;
			}
			if (sec->auth_type == NL80211_AUTHTYPE_SHARED_KEY) {
				WL_DBG(("set auth_type to shared key\n"));
				val = WL_AUTH_SHARED_KEY;	/* shared key */
				err = wldev_iovar_setint_bsscfg(dev, "auth", val, bssidx);
				if (unlikely(err)) {
					WL_ERR(("set auth failed (%d)\n", err));
					return err;
				}
			}
		}
	}
	return err;
}

#if defined(ESCAN_RESULT_PATCH)
static u8 connect_req_bssid[6];
static u8 broad_bssid[6];
#endif /* ESCAN_RESULT_PATCH */



#if defined(CUSTOM_SET_CPUCORE) || defined(CONFIG_TCPACK_FASTTX)
static bool wl_get_chan_isvht80(struct net_device *net, dhd_pub_t *dhd)
{
	u32 chanspec = 0;
	bool isvht80 = 0;

	if (wldev_iovar_getint(net, "chanspec", (s32 *)&chanspec) == BCME_OK)
		chanspec = wl_chspec_driver_to_host(chanspec);

	isvht80 = chanspec & WL_CHANSPEC_BW_80;
	WL_INFORM(("%s: chanspec(%x:%d)\n", __FUNCTION__, chanspec, isvht80));

	return isvht80;
}
#endif /* CUSTOM_SET_CPUCORE || CONFIG_TCPACK_FASTTX */

int wl_cfg80211_cleanup_mismatch_status(struct net_device *dev, struct bcm_cfg80211 *cfg,
	bool disassociate)
{
	scb_val_t scbval;
	int err = TRUE;
	int wait_cnt;

	if (disassociate) {
		WL_ERR(("Disassociate previous connection!\n"));
		wl_set_drv_status(cfg, DISCONNECTING, dev);
		scbval.val = DOT11_RC_DISASSOC_LEAVING;
		scbval.val = htod32(scbval.val);

		err = wldev_ioctl_set(dev, WLC_DISASSOC, &scbval,
				sizeof(scb_val_t));
		if (unlikely(err)) {
			wl_clr_drv_status(cfg, DISCONNECTING, dev);
			WL_ERR(("error (%d)\n", err));
			return err;
		}
		wait_cnt = 500/10;
	} else {
		wait_cnt = 200/10;
		WL_ERR(("Waiting for previous DISCONNECTING status!\n"));
		if (wl_get_drv_status(cfg, DISCONNECTING, dev)) {
			wl_clr_drv_status(cfg, DISCONNECTING, dev);
		}
	}

	while (wl_get_drv_status(cfg, DISCONNECTING, dev) && wait_cnt) {
		WL_DBG(("Waiting for disconnection terminated, wait_cnt: %d\n",
			wait_cnt));
		wait_cnt--;
		OSL_SLEEP(10);
	}

	if (wait_cnt == 0) {
		WL_ERR(("DISCONNECING clean up failed!\n"));
		/* Clear DISCONNECTING driver status as we have made sufficient attempts
		* for driver clean up.
		*/
		wl_clr_drv_status(cfg, DISCONNECTING, dev);
		return BCME_NOTREADY;
	}
	return BCME_OK;
}

#define MAX_SCAN_ABORT_WAIT_CNT 20
#define WAIT_SCAN_ABORT_OSL_SLEEP_TIME 10

static s32
wl_cfg80211_connect(struct wiphy *wiphy, struct net_device *dev,
	struct cfg80211_connect_params *sme)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct ieee80211_channel *chan = sme->channel;
	wl_extjoin_params_t *ext_join_params;
	struct wl_join_params join_params;
	size_t join_params_size;
	dhd_pub_t *dhdp =  (dhd_pub_t *)(cfg->pub);
	s32 err = 0;
	wpa_ie_fixed_t *wpa_ie;
	bcm_tlv_t *wpa2_ie;
	u8* wpaie  = 0;
	u32 wpaie_len = 0;
	u32 chan_cnt = 0;
	struct ether_addr bssid;
	s32 bssidx = -1;
#if (defined(BCM4359_CHIP) || !defined(ESCAN_RESULT_PATCH))
	int wait_cnt;
#endif
	char sec[32];

	WL_DBG(("In\n"));
	BCM_REFERENCE(dhdp);

#ifdef WLMESH
	wl_config_ifmode(cfg, dev, dev->ieee80211_ptr->iftype);
#endif
#if defined(SUPPORT_RANDOM_MAC_SCAN)
	wl_cfg80211_set_random_mac(dev, FALSE);
#endif /* SUPPORT_RANDOM_MAC_SCAN */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0))
	if (sme->channel_hint) {
		chan = sme->channel_hint;
		WL_DBG(("channel_hint (%d), channel_hint center_freq (%d)\n",
			ieee80211_frequency_to_channel(sme->channel_hint->center_freq),
			sme->channel_hint->center_freq));
	}
	if (sme->bssid_hint) {
		sme->bssid = sme->bssid_hint;
		WL_DBG(("bssid_hint "MACDBG" \n", MAC2STRDBG(sme->bssid_hint)));
	}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0) */

	if (unlikely(!sme->ssid)) {
		WL_ERR(("Invalid ssid\n"));
		return -EOPNOTSUPP;
	}

	if (unlikely(sme->ssid_len > DOT11_MAX_SSID_LEN)) {
		WL_ERR(("Invalid SSID info: SSID=%s, length=%zd\n",
			sme->ssid, sme->ssid_len));
		return -EINVAL;
	}

	WL_DBG(("SME IE : len=%zu\n", sme->ie_len));
	if (sme->ie != NULL && sme->ie_len > 0 && (wl_dbg_level & WL_DBG_DBG)) {
		prhex(NULL, (uchar *)sme->ie, sme->ie_len);
	}

	RETURN_EIO_IF_NOT_UP(cfg);

	/*
	 * Cancel ongoing scan to sync up with sme state machine of cfg80211.
	 */
#if (defined(BCM4359_CHIP) || !defined(ESCAN_RESULT_PATCH))
	if (cfg->scan_request) {
		WL_TRACE_HW4(("Aborting the scan! \n"));
		wl_cfg80211_scan_abort(cfg);
		wait_cnt = MAX_SCAN_ABORT_WAIT_CNT;
		while (wl_get_drv_status(cfg, SCANNING, dev) && wait_cnt) {
			WL_DBG(("Waiting for SCANNING terminated, wait_cnt: %d\n", wait_cnt));
			wait_cnt--;
			OSL_SLEEP(WAIT_SCAN_ABORT_OSL_SLEEP_TIME);
		}
		if (wl_get_drv_status(cfg, SCANNING, dev)) {
			wl_notify_escan_complete(cfg, dev, true, true);
		}
	}
#endif
#ifdef WL_SCHED_SCAN
	/* Locks are taken in wl_cfg80211_sched_scan_stop()
	 * A start scan occuring during connect is unlikely
	 */
	if (cfg->sched_scan_req) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0))
		wl_cfg80211_sched_scan_stop(wiphy, bcmcfg_to_prmry_ndev(cfg), 0);
#else
		wl_cfg80211_sched_scan_stop(wiphy, bcmcfg_to_prmry_ndev(cfg));
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)) */
	}
#endif
#if defined(ESCAN_RESULT_PATCH)
	if (sme->bssid)
		memcpy(connect_req_bssid, sme->bssid, ETHER_ADDR_LEN);
	else
		bzero(connect_req_bssid, ETHER_ADDR_LEN);
	bzero(broad_bssid, ETHER_ADDR_LEN);
#endif
#if defined(USE_DYNAMIC_MAXPKT_RXGLOM)
	maxrxpktglom = 0;
#endif
	if (wl_get_drv_status(cfg, CONNECTING, dev) || wl_get_drv_status(cfg, CONNECTED, dev)) {
		/* set nested connect bit to identify the context */
		wl_set_drv_status(cfg, NESTED_CONNECT, dev);
		/* DHD prev status is CONNECTING/CONNECTED */
		err = wl_cfg80211_cleanup_mismatch_status(dev, cfg, TRUE);
	} else if (wl_get_drv_status(cfg, DISCONNECTING, dev)) {
		/* DHD prev status is DISCONNECTING */
		err = wl_cfg80211_cleanup_mismatch_status(dev, cfg, FALSE);
	} else if (!wl_get_drv_status(cfg, CONNECTED, dev)) {
		/* DHD previous status is not connected and FW connected */
		if (wldev_ioctl_get(dev, WLC_GET_BSSID, &bssid, ETHER_ADDR_LEN) == 0) {
			/* set nested connect bit to identify the context */
			wl_set_drv_status(cfg, NESTED_CONNECT, dev);
			err = wl_cfg80211_cleanup_mismatch_status(dev, cfg, TRUE);
		}
	}
	wl_cfg80211_check_in4way(cfg, dev, WAIT_DISCONNECTED,
		WL_EXT_STATUS_CONNECTING, NULL);

	/* 'connect' request received */
	wl_set_drv_status(cfg, CONNECTING, dev);
	/* clear nested connect bit on proceeding for connection */
	wl_clr_drv_status(cfg, NESTED_CONNECT, dev);

	/* Clean BSSID */
	bzero(&bssid, sizeof(bssid));
	if (!wl_get_drv_status(cfg, DISCONNECTING, dev))
		wl_update_prof(cfg, dev, NULL, (void *)&bssid, WL_PROF_BSSID);

	if (p2p_is_on(cfg) && (dev != bcmcfg_to_prmry_ndev(cfg))) {
		/* we only allow to connect using virtual interface in case of P2P */
			if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
				WL_ERR(("Find p2p index from wdev(%p) failed\n",
					dev->ieee80211_ptr));
				err = BCME_ERROR;
				goto exit;
			}
			wl_cfg80211_set_mgmt_vndr_ies(cfg, ndev_to_cfgdev(dev), bssidx,
				VNDR_IE_ASSOCREQ_FLAG, sme->ie, sme->ie_len);
	} else if (dev == bcmcfg_to_prmry_ndev(cfg)) {
		if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
			WL_ERR(("Find wlan index from wdev(%p) failed\n", dev->ieee80211_ptr));
			err = BCME_ERROR;
			goto exit;
		}

		/* find the RSN_IE */
		if ((wpa2_ie = bcm_parse_tlvs((u8 *)sme->ie, sme->ie_len,
			DOT11_MNG_RSN_ID)) != NULL) {
			WL_DBG((" WPA2 IE is found\n"));
		}
		/* find the WPA_IE */
		if ((wpa_ie = wl_cfgp2p_find_wpaie((u8 *)sme->ie,
			sme->ie_len)) != NULL) {
			WL_DBG((" WPA IE is found\n"));
		}
		if (wpa_ie != NULL || wpa2_ie != NULL) {
			wpaie = (wpa_ie != NULL) ? (u8 *)wpa_ie : (u8 *)wpa2_ie;
			wpaie_len = (wpa_ie != NULL) ? wpa_ie->length : wpa2_ie->len;
			wpaie_len += WPA_RSN_IE_TAG_FIXED_LEN;
			err = wldev_iovar_setbuf(dev, "wpaie", wpaie, wpaie_len,
				cfg->ioctl_buf, WLC_IOCTL_MAXLEN, &cfg->ioctl_buf_sync);
			if (unlikely(err)) {
				WL_ERR(("wpaie set error (%d)\n", err));
				goto exit;
			}
		} else {
			err = wldev_iovar_setbuf(dev, "wpaie", NULL, 0,
				cfg->ioctl_buf, WLC_IOCTL_MAXLEN, &cfg->ioctl_buf_sync);
			if (unlikely(err)) {
				WL_ERR(("wpaie set error (%d)\n", err));
				goto exit;
			}
		}
		err = wl_cfg80211_set_mgmt_vndr_ies(cfg, ndev_to_cfgdev(dev), bssidx,
			VNDR_IE_ASSOCREQ_FLAG, (const u8 *)sme->ie, sme->ie_len);
		if (unlikely(err)) {
			goto exit;
		}
	}

	if (chan) {
		cfg->channel = ieee80211_frequency_to_channel(chan->center_freq);
		chan_cnt = 1;
		WL_DBG(("channel (%d), center_req (%d), %d channels\n", cfg->channel,
			chan->center_freq, chan_cnt));
	} else {
		WL_DBG(("No channel info from user space\n"));
		cfg->channel = 0;
	}
#ifdef BCMWAPI_WPI
	WL_DBG(("1. enable wapi auth\n"));
	if (sme->crypto.wpa_versions & NL80211_WAPI_VERSION_1) {
		WL_DBG(("2. set wapi ie  \n"));
		err = wl_set_set_wapi_ie(dev, sme);
		if (unlikely(err))
			goto exit;
	} else
		WL_DBG(("2. Not wapi ie  \n"));
#endif
	WL_DBG(("3. set wpa version \n"));

	err = wl_set_wpa_version(dev, sme);
	if (unlikely(err)) {
		WL_ERR(("Invalid wpa_version\n"));
		goto exit;
	}
#ifdef BCMWAPI_WPI
	if (sme->crypto.wpa_versions & NL80211_WAPI_VERSION_1)
		WL_DBG(("4. WAPI Dont Set wl_set_auth_type\n"));
	else {
		WL_DBG(("4. wl_set_auth_type\n"));
#endif
		err = wl_set_auth_type(dev, sme);
		if (unlikely(err)) {
			WL_ERR(("Invalid auth type\n"));
			goto exit;
		}
#ifdef BCMWAPI_WPI
	}
#endif

	err = wl_set_set_cipher(dev, sme);
	if (unlikely(err)) {
		WL_ERR(("Invalid ciper\n"));
		goto exit;
	}

	err = wl_set_key_mgmt(dev, sme);
	if (unlikely(err)) {
		WL_ERR(("Invalid key mgmt\n"));
		goto exit;
	}

	err = wl_set_set_sharedkey(dev, sme);
	if (unlikely(err)) {
		WL_ERR(("Invalid shared key\n"));
		goto exit;
	}

	/*
	 *  Join with specific BSSID and cached SSID
	 *  If SSID is zero join based on BSSID only
	 */
	join_params_size = WL_EXTJOIN_PARAMS_FIXED_SIZE +
		chan_cnt * sizeof(chanspec_t);
	ext_join_params =  (wl_extjoin_params_t*)kzalloc(join_params_size, GFP_KERNEL);
	if (ext_join_params == NULL) {
		err = -ENOMEM;
		wl_clr_drv_status(cfg, CONNECTING, dev);
		goto exit;
	}
	ext_join_params->ssid.SSID_len = min(sizeof(ext_join_params->ssid.SSID), sme->ssid_len);
	memcpy(&ext_join_params->ssid.SSID, sme->ssid, ext_join_params->ssid.SSID_len);
	wl_update_prof(cfg, dev, NULL, &ext_join_params->ssid, WL_PROF_SSID);
	ext_join_params->ssid.SSID_len = htod32(ext_join_params->ssid.SSID_len);
	/* increate dwell time to receive probe response or detect Beacon
	* from target AP at a noisy air only during connect command
	*/
	ext_join_params->scan.active_time = chan_cnt ? WL_SCAN_JOIN_ACTIVE_DWELL_TIME_MS : -1;
	ext_join_params->scan.passive_time = chan_cnt ? WL_SCAN_JOIN_PASSIVE_DWELL_TIME_MS : -1;
	/* Set up join scan parameters */
	ext_join_params->scan.scan_type = -1;
	ext_join_params->scan.nprobes = chan_cnt ?
		(ext_join_params->scan.active_time/WL_SCAN_JOIN_PROBE_INTERVAL_MS) : -1;
	ext_join_params->scan.home_time = -1;

	if (sme->bssid)
		memcpy(&ext_join_params->assoc.bssid, sme->bssid, ETH_ALEN);
	else
		memcpy(&ext_join_params->assoc.bssid, &ether_bcast, ETH_ALEN);
	ext_join_params->assoc.chanspec_num = chan_cnt;
	if (chan_cnt) {
		if (cfg->channel) {
			/*
			 * Use the channel provided by userspace
			 */
			u16 channel, band, bw, ctl_sb;
			chanspec_t chspec;
			channel = cfg->channel;
			band = (channel <= CH_MAX_2G_CHANNEL) ? WL_CHANSPEC_BAND_2G
				: WL_CHANSPEC_BAND_5G;

			/* Get min_bw set for the interface */
			bw = wl_cfg80211_ulb_get_min_bw_chspec(cfg, dev->ieee80211_ptr, bssidx);
			if (bw == INVCHANSPEC) {
				WL_ERR(("Invalid chanspec \n"));
				kfree(ext_join_params);
				err = BCME_ERROR;
				goto exit;
			}

			ctl_sb = WL_CHANSPEC_CTL_SB_NONE;
			chspec = (channel | band | bw | ctl_sb);
			ext_join_params->assoc.chanspec_list[0]  &= WL_CHANSPEC_CHAN_MASK;
			ext_join_params->assoc.chanspec_list[0] |= chspec;
			ext_join_params->assoc.chanspec_list[0] =
				wl_chspec_host_to_driver(ext_join_params->assoc.chanspec_list[0]);
		}
	}
	ext_join_params->assoc.chanspec_num = htod32(ext_join_params->assoc.chanspec_num);
	if (ext_join_params->ssid.SSID_len < IEEE80211_MAX_SSID_LEN) {
		WL_INFORM(("ssid \"%s\", len (%d)\n", ext_join_params->ssid.SSID,
			ext_join_params->ssid.SSID_len));
	}

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find p2p index from wdev(%p) failed\n", dev->ieee80211_ptr));
		kfree(ext_join_params);
		err = BCME_ERROR;
		goto exit;
	}
#if defined(CUSTOMER_HW2)
	DHD_DISABLE_RUNTIME_PM((dhd_pub_t *)cfg->pub);
#endif /* BCMDONGLEHOST && CUSTOMER_HW2 */
#ifdef WL_EXT_IAPSTA
	wl_ext_iapsta_update_channel(dhdp, dev, cfg->channel);
#endif

	wl_ext_get_sec(dev, 0, sec, sizeof(sec));
	if (cfg->rcc_enabled) {
		WL_MSG(dev->name, "Connecting with " MACDBG " ssid \"%s\", len (%d), "
			"sec=%s, with rcc channels\n\n",
			MAC2STRDBG((u8*)(&ext_join_params->assoc.bssid)),
			ext_join_params->ssid.SSID, ext_join_params->ssid.SSID_len, sec);
	} else {
		WL_MSG(dev->name, "Connecting with " MACDBG " ssid \"%s\", len (%d), "
			"sec=%s, channel=%d\n\n",
			MAC2STRDBG((u8*)(&ext_join_params->assoc.bssid)),
			ext_join_params->ssid.SSID, ext_join_params->ssid.SSID_len, sec,
			cfg->channel);
	}
	err = wldev_iovar_setbuf_bsscfg(dev, "join", ext_join_params, join_params_size,
		cfg->ioctl_buf, WLC_IOCTL_MAXLEN, bssidx, &cfg->ioctl_buf_sync);

	kfree(ext_join_params);
	if (err) {
		wl_clr_drv_status(cfg, CONNECTING, dev);
		if (err == BCME_UNSUPPORTED) {
			WL_DBG(("join iovar is not supported\n"));
			goto set_ssid;
		} else {
			WL_ERR(("error (%d)\n", err));
			goto exit;
		}
	} else
		goto exit;

set_ssid:
	memset(&join_params, 0, sizeof(join_params));
	join_params_size = sizeof(join_params.ssid);

	join_params.ssid.SSID_len = min(sizeof(join_params.ssid.SSID), sme->ssid_len);
	memcpy(&join_params.ssid.SSID, sme->ssid, join_params.ssid.SSID_len);
	join_params.ssid.SSID_len = htod32(join_params.ssid.SSID_len);
	wl_update_prof(cfg, dev, NULL, &join_params.ssid, WL_PROF_SSID);
	if (sme->bssid)
		memcpy(&join_params.params.bssid, sme->bssid, ETH_ALEN);
	else
		memcpy(&join_params.params.bssid, &ether_bcast, ETH_ALEN);

	if (wl_ch_to_chanspec(dev, cfg->channel, &join_params, &join_params_size) < 0) {
		WL_ERR(("Invalid chanspec\n"));
		return -EINVAL;
	}

	WL_DBG(("join_param_size %zu\n", join_params_size));

	if (join_params.ssid.SSID_len < IEEE80211_MAX_SSID_LEN) {
		WL_MSG(dev->name, "ssid \"%s\", len (%d)\n", join_params.ssid.SSID,
			join_params.ssid.SSID_len);
	}
	err = wldev_ioctl_set(dev, WLC_SET_SSID, &join_params, join_params_size);
exit:
	if (err) {
		WL_ERR(("error (%d)\n", err));
		wl_clr_drv_status(cfg, CONNECTING, dev);
	}
	if (!err)
		wl_cfg80211_check_in4way(cfg, dev, NO_SCAN_IN4WAY|NO_BTC_IN4WAY,
			WL_EXT_STATUS_CONNECTING, NULL);

#ifdef WLTDLS
	/* disable TDLS if number of connected interfaces is >= 1 */
	wl_cfg80211_tdls_config(cfg, TDLS_STATE_CONNECT, false);
#endif /* WLTDLS */

#ifdef DBG_PKT_MON
	if ((dev == bcmcfg_to_prmry_ndev(cfg)) && !err) {
		DHD_DBG_PKT_MON_START(dhdp);
	}
#endif /* DBG_PKT_MON */
	return err;
}

#define WAIT_FOR_DISCONNECT_MAX 10
static void wl_cfg80211_wait_for_disconnection(struct bcm_cfg80211 *cfg, struct net_device *dev)
{
	uint8 wait_cnt;

	wait_cnt = WAIT_FOR_DISCONNECT_MAX;
	while (wl_get_drv_status(cfg, DISCONNECTING, dev) && wait_cnt) {
		WL_DBG(("Waiting for disconnection, wait_cnt: %d\n", wait_cnt));
		wait_cnt--;
		OSL_SLEEP(50);
	}

	return;
}

static s32
wl_cfg80211_disconnect(struct wiphy *wiphy, struct net_device *dev,
	u16 reason_code)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	scb_val_t scbval;
	bool act = false;
	s32 err = 0;
	u8 *curbssid;
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);

	WL_MSG(dev->name, "Reason %d\n", reason_code);
	RETURN_EIO_IF_NOT_UP(cfg);
	act = *(bool *) wl_read_prof(cfg, dev, WL_PROF_ACT);
	curbssid = wl_read_prof(cfg, dev, WL_PROF_BSSID);

	BCM_REFERENCE(dhdp);

#ifdef ESCAN_RESULT_PATCH
	if (wl_get_drv_status(cfg, CONNECTING, dev) && curbssid &&
		(memcmp(curbssid, connect_req_bssid, ETHER_ADDR_LEN) == 0)) {
		WL_ERR(("Disconnecting from connecting device: " MACDBG "\n",
			MAC2STRDBG(curbssid)));
		act = true;
	}
#endif /* ESCAN_RESULT_PATCH */

	if (act) {
#ifdef DBG_PKT_MON
		if (dev == bcmcfg_to_prmry_ndev(cfg)) {
			DHD_DBG_PKT_MON_STOP(dhdp);
		}
#endif /* DBG_PKT_MON */
		/*
		* Cancel ongoing scan to sync up with sme state machine of cfg80211.
		*/
#if !defined(ESCAN_RESULT_PATCH)
		/* Let scan aborted by F/W */
		if (cfg->scan_request) {
			WL_TRACE_HW4(("Aborting the scan! \n"));
			wl_notify_escan_complete(cfg, dev, true, true);
		}
#endif /* ESCAN_RESULT_PATCH */
		if (wl_get_drv_status(cfg, CONNECTING, dev) ||
			wl_get_drv_status(cfg, CONNECTED, dev)) {
				wl_set_drv_status(cfg, DISCONNECTING, dev);
				scbval.val = reason_code;
				memcpy(&scbval.ea, curbssid, ETHER_ADDR_LEN);
				scbval.val = htod32(scbval.val);
				err = wldev_ioctl_set(dev, WLC_DISASSOC, &scbval,
						sizeof(scb_val_t));
				if (unlikely(err)) {
					wl_clr_drv_status(cfg, DISCONNECTING, dev);
					WL_ERR(("error (%d)\n", err));
					return err;
				}
				wl_cfg80211_check_in4way(cfg, dev, NO_SCAN_IN4WAY|NO_BTC_IN4WAY|WAIT_DISCONNECTED,
					WL_EXT_STATUS_DISCONNECTING, NULL);
				wl_cfg80211_wait_for_disconnection(cfg, dev);
		}
	}
#ifdef CUSTOM_SET_CPUCORE
	/* set default cpucore */
	if (dev == bcmcfg_to_prmry_ndev(cfg)) {
		dhdp->chan_isvht80 &= ~DHD_FLAG_STA_MODE;
		if (!(dhdp->chan_isvht80))
			dhd_set_cpucore(dhdp, FALSE);
	}
#endif /* CUSTOM_SET_CPUCORE */

	return err;
}

static s32
#if defined(WL_CFG80211_P2P_DEV_IF)
wl_cfg80211_set_tx_power(struct wiphy *wiphy, struct wireless_dev *wdev,
	enum nl80211_tx_power_setting type, s32 mbm)
#else
wl_cfg80211_set_tx_power(struct wiphy *wiphy,
	enum nl80211_tx_power_setting type, s32 dbm)
#endif /* WL_CFG80211_P2P_DEV_IF */
{

	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);
	s32 err = 0;
#if defined(WL_CFG80211_P2P_DEV_IF)
	s32 dbm = MBM_TO_DBM(mbm);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0)) || \
	defined(WL_COMPAT_WIRELESS) || defined(WL_SUPPORT_BACKPORTED_KPATCHES)
	dbm = MBM_TO_DBM(dbm);
#endif /* WL_CFG80211_P2P_DEV_IF */

	RETURN_EIO_IF_NOT_UP(cfg);
	switch (type) {
	case NL80211_TX_POWER_AUTOMATIC:
		break;
	case NL80211_TX_POWER_LIMITED:
		if (dbm < 0) {
			WL_ERR(("TX_POWER_LIMITTED - dbm is negative\n"));
			return -EINVAL;
		}
		break;
	case NL80211_TX_POWER_FIXED:
		if (dbm < 0) {
			WL_ERR(("TX_POWER_FIXED - dbm is negative..\n"));
			return -EINVAL;
		}
		break;
	}

	err = wl_set_tx_power(ndev, type, dbm);
	if (unlikely(err)) {
		WL_ERR(("error (%d)\n", err));
		return err;
	}

	cfg->conf->tx_power = dbm;

	return err;
}

static s32
#if defined(WL_CFG80211_P2P_DEV_IF)
wl_cfg80211_get_tx_power(struct wiphy *wiphy,
	struct wireless_dev *wdev, s32 *dbm)
#else
wl_cfg80211_get_tx_power(struct wiphy *wiphy, s32 *dbm)
#endif /* WL_CFG80211_P2P_DEV_IF */
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);
	s32 err = 0;

	RETURN_EIO_IF_NOT_UP(cfg);
	err = wl_get_tx_power(ndev, dbm);
	if (unlikely(err))
		WL_ERR(("error (%d)\n", err));

	return err;
}

static s32
wl_cfg80211_config_default_key(struct wiphy *wiphy, struct net_device *dev,
	u8 key_idx, bool unicast, bool multicast)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	u32 index;
	s32 wsec;
	s32 err = 0;
	s32 bssidx;

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find p2p index from dev(%p) failed\n", dev->ieee80211_ptr));
		return BCME_ERROR;
	}

	WL_DBG(("key index (%d)\n", key_idx));
	RETURN_EIO_IF_NOT_UP(cfg);
	err = wldev_iovar_getint_bsscfg(dev, "wsec", &wsec, bssidx);
	if (unlikely(err)) {
		WL_ERR(("WLC_GET_WSEC error (%d)\n", err));
		return err;
	}
	if (wsec == WEP_ENABLED) {
		/* Just select a new current key */
		index = (u32) key_idx;
		index = htod32(index);
		err = wldev_ioctl_set(dev, WLC_SET_KEY_PRIMARY, &index,
			sizeof(index));
		if (unlikely(err)) {
			WL_ERR(("error (%d)\n", err));
		}
	}
	return err;
}

static s32
wl_add_keyext(struct wiphy *wiphy, struct net_device *dev,
	u8 key_idx, const u8 *mac_addr, struct key_params *params)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct wl_wsec_key key;
	s32 err = 0;
	s32 bssidx;
	s32 mode = wl_get_mode_by_netdev(cfg, dev);

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find p2p index from wdev(%p) failed\n", dev->ieee80211_ptr));
		return BCME_ERROR;
	}
	memset(&key, 0, sizeof(key));
	key.index = (u32) key_idx;

	if (!ETHER_ISMULTI(mac_addr))
		memcpy((char *)&key.ea, (const void *)mac_addr, ETHER_ADDR_LEN);
	key.len = (u32) params->key_len;

	/* check for key index change */
	if (key.len == 0) {
		/* key delete */
		swap_key_from_BE(&key);
		err = wldev_iovar_setbuf_bsscfg(dev, "wsec_key", &key, sizeof(key),
			cfg->ioctl_buf, WLC_IOCTL_MAXLEN, bssidx, &cfg->ioctl_buf_sync);
		if (unlikely(err)) {
			WL_ERR(("key delete error (%d)\n", err));
			return err;
		}
	} else {
		if (key.len > sizeof(key.data)) {
			WL_ERR(("Invalid key length (%d)\n", key.len));
			return -EINVAL;
		}
		WL_DBG(("Setting the key index %d\n", key.index));
		memcpy(key.data, params->key, key.len);

		if ((mode == WL_MODE_BSS) &&
			(params->cipher == WLAN_CIPHER_SUITE_TKIP)) {
			u8 keybuf[8];
			memcpy(keybuf, &key.data[24], sizeof(keybuf));
			memcpy(&key.data[24], &key.data[16], sizeof(keybuf));
			memcpy(&key.data[16], keybuf, sizeof(keybuf));
		}

		/* if IW_ENCODE_EXT_RX_SEQ_VALID set */
		if (params->seq && params->seq_len == 6) {
			/* rx iv */
			u8 *ivptr;
			ivptr = (u8 *) params->seq;
			key.rxiv.hi = (ivptr[5] << 24) | (ivptr[4] << 16) |
				(ivptr[3] << 8) | ivptr[2];
			key.rxiv.lo = (ivptr[1] << 8) | ivptr[0];
			key.iv_initialized = true;
		}

		switch (params->cipher) {
		case WLAN_CIPHER_SUITE_WEP40:
			key.algo = CRYPTO_ALGO_WEP1;
			WL_DBG(("WLAN_CIPHER_SUITE_WEP40\n"));
			break;
		case WLAN_CIPHER_SUITE_WEP104:
			key.algo = CRYPTO_ALGO_WEP128;
			WL_DBG(("WLAN_CIPHER_SUITE_WEP104\n"));
			break;
		case WLAN_CIPHER_SUITE_TKIP:
			key.algo = CRYPTO_ALGO_TKIP;
			WL_DBG(("WLAN_CIPHER_SUITE_TKIP\n"));
			break;
		case WLAN_CIPHER_SUITE_AES_CMAC:
			key.algo = CRYPTO_ALGO_AES_CCM;
			WL_DBG(("WLAN_CIPHER_SUITE_AES_CMAC\n"));
			break;
		case WLAN_CIPHER_SUITE_CCMP:
			key.algo = CRYPTO_ALGO_AES_CCM;
			WL_DBG(("WLAN_CIPHER_SUITE_CCMP\n"));
			break;
#ifdef BCMWAPI_WPI
		case WLAN_CIPHER_SUITE_SMS4:
			key.algo = CRYPTO_ALGO_SMS4;
			WL_DBG(("WLAN_CIPHER_SUITE_SMS4\n"));
			break;
#endif
		default:
			WL_ERR(("Invalid cipher (0x%x)\n", params->cipher));
			return -EINVAL;
		}
		swap_key_from_BE(&key);
		/* need to guarantee EAPOL 4/4 send out before set key */
		dhd_wait_pend8021x(dev);
		err = wldev_iovar_setbuf_bsscfg(dev, "wsec_key", &key, sizeof(key),
			cfg->ioctl_buf, WLC_IOCTL_MAXLEN, bssidx, &cfg->ioctl_buf_sync);
		if (unlikely(err)) {
			WL_ERR(("WLC_SET_KEY error (%d)\n", err));
			return err;
		}
	}
	return err;
}

int
wl_cfg80211_enable_roam_offload(struct net_device *dev, int enable)
{
	int err;
	wl_eventmsg_buf_t ev_buf;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	if (dev != bcmcfg_to_prmry_ndev(cfg)) {
		/* roam offload is only for the primary device */
		return -1;
	}
	err = wldev_iovar_setint(dev, "roam_offload", enable);
	if (err)
		return err;

	bzero(&ev_buf, sizeof(wl_eventmsg_buf_t));
	wl_cfg80211_add_to_eventbuffer(&ev_buf, WLC_E_PSK_SUP, !enable);
	wl_cfg80211_add_to_eventbuffer(&ev_buf, WLC_E_ASSOC_REQ_IE, !enable);
	wl_cfg80211_add_to_eventbuffer(&ev_buf, WLC_E_ASSOC_RESP_IE, !enable);
	wl_cfg80211_add_to_eventbuffer(&ev_buf, WLC_E_REASSOC, !enable);
	wl_cfg80211_add_to_eventbuffer(&ev_buf, WLC_E_JOIN, !enable);
	wl_cfg80211_add_to_eventbuffer(&ev_buf, WLC_E_ROAM, !enable);
	err = wl_cfg80211_apply_eventbuffer(dev, cfg, &ev_buf);
	if (!err) {
		cfg->roam_offload = enable;
	}
	return err;
}

#if defined(WL_VIRTUAL_APSTA)
int
wl_cfg80211_interface_create(struct net_device *dev, char *name)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	bcm_struct_cfgdev *new_cfgdev;
	char ifname[IFNAMSIZ];
	char iftype[IFNAMSIZ];
	enum nl80211_iftype iface_type = NL80211_IFTYPE_STATION;

	sscanf(name, "%s %s", ifname, iftype);

	if (strnicmp(iftype, "AP", strlen("AP")) == 0) {
		iface_type = NL80211_IFTYPE_AP;
	}

	new_cfgdev = wl_cfg80211_create_iface(cfg->wdev->wiphy,
		iface_type, NULL, ifname);
	if (!new_cfgdev) {
		return BCME_ERROR;
	}
	else {
		WL_DBG(("Iface %s created successfuly\n", name));
		return BCME_OK;
	}
}

int
wl_cfg80211_interface_delete(struct net_device *dev, char *name)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	struct net_info *iter, *next;
	int err = BCME_ERROR;

	if (name == NULL) {
		return BCME_ERROR;
	}

#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	for_each_ndev(cfg, iter, next) {
		if (iter->ndev) {
			if (strcmp(iter->ndev->name, name) == 0) {
				err =  wl_cfg80211_del_iface(cfg->wdev->wiphy,
					ndev_to_cfgdev(iter->ndev));
				break;
			}
		}
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
	if (!err) {
		WL_DBG(("Iface %s deleted successfuly", name));
	}
	return err;
}

#if defined(PKT_FILTER_SUPPORT) && defined(APSTA_BLOCK_ARP_DURING_DHCP)
void
wl_cfg80211_block_arp(struct net_device *dev, int enable)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);

	if (!dhd_pkt_filter_enable) {
		WL_INFORM(("Packet filter isn't enabled\n"));
		return;
	}

	/* Block/Unblock ARP frames only if STA is connected to
	 * the upstream AP in case of STA+SoftAP Concurrenct mode
	 */
	if (!wl_get_drv_status(cfg, CONNECTED, dev)) {
		WL_INFORM(("STA doesn't connected to upstream AP\n"));
		return;
	}

	if (enable) {
		WL_DBG(("Enable ARP Filter\n"));
		/* Add ARP filter */
		dhd_packet_filter_add_remove(dhdp, TRUE, DHD_BROADCAST_ARP_FILTER_NUM);

		/* Enable ARP packet filter - blacklist */
		dhd_master_mode = FALSE;
		dhd_pktfilter_offload_enable(dhdp, dhdp->pktfilter[DHD_BROADCAST_ARP_FILTER_NUM],
			TRUE, dhd_master_mode);
	} else {
		WL_DBG(("Disable ARP Filter\n"));
		/* Disable ARP packet filter */
		dhd_master_mode = TRUE;
		dhd_pktfilter_offload_enable(dhdp, dhdp->pktfilter[DHD_BROADCAST_ARP_FILTER_NUM],
			FALSE, dhd_master_mode);

		/* Delete ARP filter */
		dhd_packet_filter_add_remove(dhdp, FALSE, DHD_BROADCAST_ARP_FILTER_NUM);
	}
}
#endif /* PKT_FILTER_SUPPORT && APSTA_BLOCK_ARP_DURING_DHCP */
#endif /* defined (WL_VIRTUAL_APSTA) */

static s32
wl_cfg80211_add_key(struct wiphy *wiphy, struct net_device *dev,
	u8 key_idx, bool pairwise, const u8 *mac_addr,
	struct key_params *params)
{
	struct wl_wsec_key key;
	s32 val = 0;
	s32 wsec = 0;
	s32 err = 0;
	u8 keybuf[8];
	s32 bssidx = 0;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	s32 mode = wl_get_mode_by_netdev(cfg, dev);

	WL_DBG(("key index (%d)\n", key_idx));
	RETURN_EIO_IF_NOT_UP(cfg);

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find p2p index from dev(%p) failed\n", dev->ieee80211_ptr));
		return BCME_ERROR;
	}

	if (mac_addr &&
		((params->cipher != WLAN_CIPHER_SUITE_WEP40) &&
		(params->cipher != WLAN_CIPHER_SUITE_WEP104))) {
			wl_add_keyext(wiphy, dev, key_idx, mac_addr, params);
			goto exit;
	}
	memset(&key, 0, sizeof(key));
	/* Clear any buffered wep key */
	memset(&cfg->wep_key, 0, sizeof(struct wl_wsec_key));

	key.len = (u32) params->key_len;
	key.index = (u32) key_idx;

	if (unlikely(key.len > sizeof(key.data))) {
		WL_ERR(("Too long key length (%u)\n", key.len));
		return -EINVAL;
	}
	memcpy(key.data, params->key, key.len);

	key.flags = WL_PRIMARY_KEY;
	switch (params->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		key.algo = CRYPTO_ALGO_WEP1;
		val = WEP_ENABLED;
		WL_DBG(("WLAN_CIPHER_SUITE_WEP40\n"));
		break;
	case WLAN_CIPHER_SUITE_WEP104:
		key.algo = CRYPTO_ALGO_WEP128;
		val = WEP_ENABLED;
		WL_DBG(("WLAN_CIPHER_SUITE_WEP104\n"));
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		key.algo = CRYPTO_ALGO_TKIP;
		val = TKIP_ENABLED;
		/* wpa_supplicant switches the third and fourth quarters of the TKIP key */
		if (mode == WL_MODE_BSS) {
			bcopy(&key.data[24], keybuf, sizeof(keybuf));
			bcopy(&key.data[16], &key.data[24], sizeof(keybuf));
			bcopy(keybuf, &key.data[16], sizeof(keybuf));
		}
		WL_DBG(("WLAN_CIPHER_SUITE_TKIP\n"));
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
		key.algo = CRYPTO_ALGO_AES_CCM;
		val = AES_ENABLED;
		WL_DBG(("WLAN_CIPHER_SUITE_AES_CMAC\n"));
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		key.algo = CRYPTO_ALGO_AES_CCM;
		val = AES_ENABLED;
		WL_DBG(("WLAN_CIPHER_SUITE_CCMP\n"));
		break;
#if defined(WLFBT) && defined(WLAN_CIPHER_SUITE_PMK)
	case WLAN_CIPHER_SUITE_PMK: {
		int j;
		wsec_pmk_t pmk;
		char keystring[WSEC_MAX_PSK_LEN + 1];
		char* charptr = keystring;
		uint len;
		struct wl_security *sec;
		sec = wl_read_prof(cfg, dev, WL_PROF_SEC);
		if (sec->wpa_auth == WLAN_AKM_SUITE_8021X) {
			err = wldev_iovar_setbuf(dev, "okc_info_pmk", (void *)params->key,
				WSEC_MAX_PSK_LEN / 2, keystring, sizeof(keystring), NULL);
			if (err) {
				/* could fail in case that 'okc' is not supported */
				WL_INFORM(("Setting 'okc_info_pmk' failed, err=%d\n", err));
			}
		}
		/* copy the raw hex key to the appropriate format */
		for (j = 0; j < (WSEC_MAX_PSK_LEN / 2); j++) {
			charptr += snprintf(charptr, sizeof(keystring), "%02x", params->key[j]);
		}
		len = strlen(keystring);
		pmk.key_len = htod16(len);
		bcopy(keystring, pmk.key, len);
		pmk.flags = htod16(WSEC_PASSPHRASE);
		err = wldev_ioctl(dev, WLC_SET_WSEC_PMK, &pmk, sizeof(pmk), true);
		if (err)
			return err;
	} break;
#endif /* WLFBT && WLAN_CIPHER_SUITE_PMK */
#ifdef BCMWAPI_WPI
	case WLAN_CIPHER_SUITE_SMS4:
		key.algo = CRYPTO_ALGO_SMS4;
		WL_DBG(("WLAN_CIPHER_SUITE_SMS4\n"));
		val = SMS4_ENABLED;
		break;
#endif /* BCMWAPI_WPI */
	default:
		WL_ERR(("Invalid cipher (0x%x)\n", params->cipher));
		return -EINVAL;
	}

	/* Set the new key/index */
	if ((mode == WL_MODE_IBSS) && (val & (TKIP_ENABLED | AES_ENABLED))) {
		WL_ERR(("IBSS KEY setted\n"));
		wldev_iovar_setint(dev, "wpa_auth", WPA_AUTH_NONE);
	}
	swap_key_from_BE(&key);
	if ((params->cipher == WLAN_CIPHER_SUITE_WEP40) ||
		(params->cipher == WLAN_CIPHER_SUITE_WEP104)) {
		/*
		 * For AP role, since we are doing a wl down before bringing up AP,
		 * the plumbed keys will be lost. So for AP once we bring up AP, we
		 * need to plumb keys again. So buffer the keys for future use. This
		 * is more like a WAR. If firmware later has the capability to do
		 * interface upgrade without doing a "wl down" and "wl apsta 0", then
		 * this will not be required.
		 */
		WL_DBG(("Buffering WEP Keys \n"));
		memcpy(&cfg->wep_key, &key, sizeof(struct wl_wsec_key));
	}
	err = wldev_iovar_setbuf_bsscfg(dev, "wsec_key", &key, sizeof(key), cfg->ioctl_buf,
		WLC_IOCTL_MAXLEN, bssidx, &cfg->ioctl_buf_sync);
	if (unlikely(err)) {
		WL_ERR(("WLC_SET_KEY error (%d)\n", err));
		return err;
	}

exit:
	err = wldev_iovar_getint_bsscfg(dev, "wsec", &wsec, bssidx);
	if (unlikely(err)) {
		WL_ERR(("get wsec error (%d)\n", err));
		return err;
	}

	wsec |= val;
	err = wldev_iovar_setint_bsscfg(dev, "wsec", wsec, bssidx);
	if (unlikely(err)) {
		WL_ERR(("set wsec error (%d)\n", err));
		return err;
	}
	wl_cfg80211_check_in4way(cfg, dev, NO_SCAN_IN4WAY|NO_BTC_IN4WAY,
		WL_EXT_STATUS_ADD_KEY, NULL);

	return err;
}

static s32
wl_cfg80211_del_key(struct wiphy *wiphy, struct net_device *dev,
	u8 key_idx, bool pairwise, const u8 *mac_addr)
{
	struct wl_wsec_key key;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	s32 err = 0;
	s32 bssidx;

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find p2p index from wdev(%p) failed\n", dev->ieee80211_ptr));
		return BCME_ERROR;
	}
	WL_DBG(("Enter\n"));

#ifndef MFP
	if ((key_idx >= DOT11_MAX_DEFAULT_KEYS) && (key_idx < DOT11_MAX_DEFAULT_KEYS+2))
		return -EINVAL;
#endif

	RETURN_EIO_IF_NOT_UP(cfg);
	memset(&key, 0, sizeof(key));

	key.flags = WL_PRIMARY_KEY;
	key.algo = CRYPTO_ALGO_OFF;
	key.index = (u32) key_idx;

	WL_DBG(("key index (%d)\n", key_idx));
	/* Set the new key/index */
	swap_key_from_BE(&key);
	err = wldev_iovar_setbuf_bsscfg(dev, "wsec_key", &key, sizeof(key), cfg->ioctl_buf,
		WLC_IOCTL_MAXLEN, bssidx, &cfg->ioctl_buf_sync);
	if (unlikely(err)) {
		if (err == -EINVAL) {
			if (key.index >= DOT11_MAX_DEFAULT_KEYS) {
				/* we ignore this key index in this case */
				WL_DBG(("invalid key index (%d)\n", key_idx));
			}
		} else {
			WL_ERR(("WLC_SET_KEY error (%d)\n", err));
		}
		return err;
	}
	return err;
}

static s32
wl_cfg80211_get_key(struct wiphy *wiphy, struct net_device *dev,
	u8 key_idx, bool pairwise, const u8 *mac_addr, void *cookie,
	void (*callback) (void *cookie, struct key_params * params))
{
	struct key_params params;
	struct wl_wsec_key key;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct wl_security *sec;
	s32 wsec;
	s32 err = 0;
	s32 bssidx;

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find p2p index from wdev(%p) failed\n", dev->ieee80211_ptr));
		return BCME_ERROR;
	}
	WL_DBG(("key index (%d)\n", key_idx));
	RETURN_EIO_IF_NOT_UP(cfg);
	memset(&key, 0, sizeof(key));
	key.index = key_idx;
	swap_key_to_BE(&key);
	memset(&params, 0, sizeof(params));
	params.key_len = (u8) min_t(u8, DOT11_MAX_KEY_SIZE, key.len);
	memcpy((void *)params.key, key.data, params.key_len);

	err = wldev_iovar_getint_bsscfg(dev, "wsec", &wsec, bssidx);
	if (unlikely(err)) {
		WL_ERR(("WLC_GET_WSEC error (%d)\n", err));
		return err;
	}
	switch (WSEC_ENABLED(wsec)) {
		case WEP_ENABLED:
			sec = wl_read_prof(cfg, dev, WL_PROF_SEC);
			if (sec->cipher_pairwise & WLAN_CIPHER_SUITE_WEP40) {
				params.cipher = WLAN_CIPHER_SUITE_WEP40;
				WL_DBG(("WLAN_CIPHER_SUITE_WEP40\n"));
			} else if (sec->cipher_pairwise & WLAN_CIPHER_SUITE_WEP104) {
				params.cipher = WLAN_CIPHER_SUITE_WEP104;
				WL_DBG(("WLAN_CIPHER_SUITE_WEP104\n"));
			}
			break;
		case TKIP_ENABLED:
			params.cipher = WLAN_CIPHER_SUITE_TKIP;
			WL_DBG(("WLAN_CIPHER_SUITE_TKIP\n"));
			break;
		case AES_ENABLED:
			params.cipher = WLAN_CIPHER_SUITE_AES_CMAC;
			WL_DBG(("WLAN_CIPHER_SUITE_AES_CMAC\n"));
			break;
#if defined(SUPPORT_SOFTAP_WPAWPA2_MIXED)
		/* to connect to mixed mode AP */
		case (AES_ENABLED | TKIP_ENABLED): /* TKIP CCMP */
			params.cipher = WLAN_CIPHER_SUITE_AES_CMAC;
			WL_DBG(("WLAN_CIPHER_SUITE_TKIP\n"));
			break;
#endif
#ifdef BCMWAPI_WPI
		case SMS4_ENABLED:
			params.cipher = WLAN_CIPHER_SUITE_SMS4;
			WL_DBG(("WLAN_CIPHER_SUITE_SMS4\n"));
			break;
#endif
		default:
			WL_ERR(("Invalid algo (0x%x)\n", wsec));
			return -EINVAL;
	}

	callback(cookie, &params);
	return err;
}

static s32
wl_cfg80211_config_default_mgmt_key(struct wiphy *wiphy,
	struct net_device *dev, u8 key_idx)
{
#ifdef MFP
	return 0;
#else
	WL_INFORM(("Not supported\n"));
	return -EOPNOTSUPP;
#endif /* MFP */
}

static s32
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
wl_cfg80211_get_station(struct wiphy *wiphy, struct net_device *dev,
	const u8 *mac, struct station_info *sinfo)
#else
wl_cfg80211_get_station(struct wiphy *wiphy, struct net_device *dev,
	u8 *mac, struct station_info *sinfo)
#endif
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	scb_val_t scb_val;
	s32 rssi;
	s32 rate;
	s32 err = 0;
	sta_info_t *sta;
	s8 eabuf[ETHER_ADDR_STR_LEN];
	dhd_pub_t *dhd =  (dhd_pub_t *)(cfg->pub);
	bool fw_assoc_state = FALSE;
	u32 dhd_assoc_state = 0;
	static int err_cnt = 0;

	RETURN_EIO_IF_NOT_UP(cfg);
	if (wl_get_mode_by_netdev(cfg, dev) == WL_MODE_AP) {
		err = wldev_iovar_getbuf(dev, "sta_info", (struct ether_addr *)mac,
			ETHER_ADDR_LEN, cfg->ioctl_buf, WLC_IOCTL_SMLEN, &cfg->ioctl_buf_sync);
		if (err < 0) {
			WL_ERR(("GET STA INFO failed, %d\n", err));
			return err;
		}
		sinfo->filled = STA_INFO_BIT(INFO_INACTIVE_TIME);
		sta = (sta_info_t *)cfg->ioctl_buf;
		sta->len = dtoh16(sta->len);
		sta->cap = dtoh16(sta->cap);
		sta->flags = dtoh32(sta->flags);
		sta->idle = dtoh32(sta->idle);
		sta->in = dtoh32(sta->in);
		sinfo->inactive_time = sta->idle * 1000;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
		if (sta->flags & WL_STA_ASSOC) {
			sinfo->filled |= STA_INFO_BIT(INFO_CONNECTED_TIME);
			sinfo->connected_time = sta->in;
		}
#endif
		WL_INFORM(("STA %s, flags 0x%x, idle time %ds, connected time %ds\n",
			bcm_ether_ntoa((const struct ether_addr *)mac, eabuf),
			sta->flags, sta->idle, sta->in));
	} else if (wl_get_mode_by_netdev(cfg, dev) == WL_MODE_BSS ||
		wl_get_mode_by_netdev(cfg, dev) == WL_MODE_IBSS) {
		get_pktcnt_t pktcnt;
#ifdef DHD_SUPPORT_IF_CNTS
		wl_if_stats_t *if_stats = NULL;
#endif /* DHD_SUPPORT_IF_CNTS */
		u8 *curmacp;

		if (cfg->roam_offload) {
			struct ether_addr bssid;
			memset(&bssid, 0, sizeof(bssid));
			err = wldev_ioctl_get(dev, WLC_GET_BSSID, &bssid, ETHER_ADDR_LEN);
			if (err) {
				WL_ERR(("Failed to get current BSSID\n"));
			} else {
				if (memcmp(mac, &bssid.octet, ETHER_ADDR_LEN) != 0) {
					/* roaming is detected */
					err = wl_cfg80211_delayed_roam(cfg, dev, &bssid);
					if (err)
						WL_ERR(("Failed to handle the delayed roam, "
							"err=%d", err));
					mac = (u8 *)bssid.octet;
				}
			}
		}
		dhd_assoc_state = wl_get_drv_status(cfg, CONNECTED, dev);
		DHD_OS_WAKE_LOCK(dhd);
		fw_assoc_state = dhd_is_associated(dhd, 0, &err);
		DHD_OS_WAKE_UNLOCK(dhd);
		if (!dhd_assoc_state || !fw_assoc_state) {
			WL_ERR(("NOT assoc\n"));
			if (err == -ENODATA)
				return err;
			if (!dhd_assoc_state) {
				WL_TRACE_HW4(("drv state is not connected \n"));
			}
			if (!fw_assoc_state) {
				WL_TRACE_HW4(("fw state is not associated \n"));
			}
			/* Disconnect due to fw is not associated for FW_ASSOC_WATCHDOG_TIME ms.
			* 'err == 0' of dhd_is_associated() and '!fw_assoc_state'
			* means that BSSID is null.
			*/
			if (dhd_assoc_state && !fw_assoc_state && !err) {
				if (!fw_assoc_watchdog_started) {
					fw_assoc_watchdog_ms = OSL_SYSUPTIME();
					fw_assoc_watchdog_started = TRUE;
					WL_TRACE_HW4(("fw_assoc_watchdog_started \n"));
				} else {
					if (OSL_SYSUPTIME() - fw_assoc_watchdog_ms >
						FW_ASSOC_WATCHDOG_TIME) {
						fw_assoc_watchdog_started = FALSE;
						err = -ENODEV;
						WL_TRACE_HW4(("fw is not associated for %d ms \n",
							(OSL_SYSUPTIME() - fw_assoc_watchdog_ms)));
						goto get_station_err;
					}
				}
			}
			err = -ENODEV;
			return err;
		}
		fw_assoc_watchdog_started = FALSE;
		curmacp = wl_read_prof(cfg, dev, WL_PROF_BSSID);
		if (memcmp(mac, curmacp, ETHER_ADDR_LEN)) {
			WL_ERR(("Wrong Mac address: "MACDBG" != "MACDBG"\n",
				MAC2STRDBG(mac), MAC2STRDBG(curmacp)));
		}

		/* Report the current tx rate */
		rate = 0;
		err = wldev_ioctl_get(dev, WLC_GET_RATE, &rate, sizeof(rate));
		if (err) {
			WL_ERR(("Could not get rate (%d)\n", err));
		} else {
#if defined(USE_DYNAMIC_MAXPKT_RXGLOM)
			int rxpktglom;
#endif
			rate = dtoh32(rate);
			sinfo->filled |= STA_INFO_BIT(INFO_TX_BITRATE);
			sinfo->txrate.legacy = rate * 5;
			WL_DBG(("Rate %d Mbps\n", (rate / 2)));
#if defined(USE_DYNAMIC_MAXPKT_RXGLOM)
			rxpktglom = ((rate/2) > 150) ? 20 : 10;

			if (maxrxpktglom != rxpktglom) {
				maxrxpktglom = rxpktglom;
				WL_DBG(("Rate %d Mbps, update bus:maxtxpktglom=%d\n", (rate/2),
					maxrxpktglom));
				err = wldev_iovar_setbuf(dev, "bus:maxtxpktglom",
					(char*)&maxrxpktglom, 4, cfg->ioctl_buf,
					WLC_IOCTL_MAXLEN, NULL);
				if (err < 0) {
					WL_ERR(("set bus:maxtxpktglom failed, %d\n", err));
				}
			}
#endif
		}

		memset(&scb_val, 0, sizeof(scb_val));
		scb_val.val = 0;
		err = wldev_ioctl_get(dev, WLC_GET_RSSI, &scb_val,
			sizeof(scb_val_t));
		if (err) {
			WL_ERR(("Could not get rssi (%d)\n", err));
			goto get_station_err;
		}
		rssi = dtoh32(scb_val.val);
#if defined(RSSIAVG)
		err = wl_update_connected_rssi_cache(dev, &cfg->g_connected_rssi_cache_ctrl, &rssi);
		if (err) {
			WL_ERR(("Could not get rssi (%d)\n", err));
			goto get_station_err;
		}
		wl_delete_dirty_rssi_cache(&cfg->g_connected_rssi_cache_ctrl);
		wl_reset_rssi_cache(&cfg->g_connected_rssi_cache_ctrl);
#endif
#if defined(RSSIOFFSET)
		rssi = wl_update_rssi_offset(dev, rssi);
#endif
#if !defined(RSSIAVG) && !defined(RSSIOFFSET)
		// terence 20150419: limit the max. rssi to -2 or the bss will be filtered out in android OS
		rssi = MIN(rssi, RSSI_MAXVAL);
#endif
		sinfo->filled |= STA_INFO_BIT(INFO_SIGNAL);
		sinfo->signal = rssi;
		WL_DBG(("RSSI %d dBm\n", rssi));

#ifdef DHD_SUPPORT_IF_CNTS
		if ((if_stats = kmalloc(sizeof(*if_stats), GFP_KERNEL)) == NULL) {
			WL_ERR(("%s(%d): kmalloc failed\n", __FUNCTION__, __LINE__));
			goto error;
		}
		memset(if_stats, 0, sizeof(*if_stats));

		err = wldev_iovar_getbuf(dev, "if_counters", NULL, 0,
			(char *)if_stats, sizeof(*if_stats), NULL);
		if (!err) {
			sinfo->rx_packets = (uint32)dtoh64(if_stats->rxframe);
			sinfo->rx_dropped_misc = 0;
			sinfo->tx_packets = (uint32)dtoh64(if_stats->txfrmsnt);
			sinfo->tx_failed = (uint32)dtoh64(if_stats->txnobuf) +
				(uint32)dtoh64(if_stats->txrunt) +
				(uint32)dtoh64(if_stats->txfail);
		} else {
//			WL_ERR(("%s: if_counters not supported ret=%d\n",
//				__FUNCTION__, err));
#endif /* DHD_SUPPORT_IF_CNTS */

			err = wldev_ioctl_get(dev, WLC_GET_PKTCNTS, &pktcnt,
				sizeof(pktcnt));
			if (err) {
				WL_ERR(("Could not get WLC_GET_PKTCNTS (%d)\n", err));
				goto get_station_err;
			}
			sinfo->rx_packets = pktcnt.rx_good_pkt;
			sinfo->rx_dropped_misc = pktcnt.rx_bad_pkt;
			sinfo->tx_packets = pktcnt.tx_good_pkt;
			sinfo->tx_failed  = pktcnt.tx_bad_pkt;
#ifdef DHD_SUPPORT_IF_CNTS
		}
#endif /* DHD_SUPPORT_IF_CNTS */

		sinfo->filled |= (STA_INFO_BIT(INFO_RX_PACKETS) |
			STA_INFO_BIT(INFO_RX_DROP_MISC) |
			STA_INFO_BIT(INFO_TX_PACKETS) |
			STA_INFO_BIT(INFO_TX_FAILED));

get_station_err:
		if (err)
			err_cnt++;
		else
			err_cnt = 0;
		if (err_cnt >= 3 && (err != -ENODATA)) {
			/* Disconnect due to zero BSSID or error to get RSSI */
			scb_val_t scbval;
			scbval.val = htod32(DOT11_RC_DISASSOC_LEAVING);
			err = wldev_ioctl_set(dev, WLC_DISASSOC, &scbval, sizeof(scb_val_t));
			if (unlikely(err)) {
				WL_ERR(("disassoc error (%d)\n", err));
			}

			WL_ERR(("force cfg80211_disconnected: %d\n", err));
			wl_clr_drv_status(cfg, CONNECTED, dev);
			CFG80211_DISCONNECTED(dev, 0, NULL, 0, false, GFP_KERNEL);
			wl_link_down(cfg);
		}
#ifdef DHD_SUPPORT_IF_CNTS
error:
		if (if_stats) {
			kfree(if_stats);
		}
#endif /* DHD_SUPPORT_IF_CNTS */
	}
	else {
		WL_ERR(("Invalid device mode %d\n", wl_get_mode_by_netdev(cfg, dev)));
	}

	return err;
}

static s32
wl_cfg80211_set_power_mgmt(struct wiphy *wiphy, struct net_device *dev,
	bool enabled, s32 timeout)
{
	s32 pm;
	s32 err = 0;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct net_info *_net_info = wl_get_netinfo_by_netdev(cfg, dev);
#ifdef RTT_SUPPORT
	rtt_status_info_t *rtt_status;
#endif /* RTT_SUPPORT */
	dhd_pub_t *dhd = cfg->pub;

	RETURN_EIO_IF_NOT_UP(cfg);
	WL_DBG(("Enter\n"));
	if (cfg->p2p_net == dev || _net_info == NULL ||
			!wl_get_drv_status(cfg, CONNECTED, dev) ||
			(wl_get_mode_by_netdev(cfg, dev) != WL_MODE_BSS &&
			wl_get_mode_by_netdev(cfg, dev) != WL_MODE_IBSS)) {
		return err;
	}

	/* Enlarge pm_enable_work */
	wl_add_remove_pm_enable_work(cfg, WL_PM_WORKQ_LONG);

	pm = enabled ? PM_FAST : PM_OFF;
	if (_net_info->pm_block) {
		WL_ERR(("%s:Do not enable the power save for pm_block %d\n",
			dev->name, _net_info->pm_block));
		pm = PM_OFF;
	}
	if (enabled && dhd_conf_get_pm(dhd) >= 0)
		pm = dhd_conf_get_pm(dhd);
	pm = htod32(pm);
	WL_DBG(("%s:power save %s\n", dev->name, (pm ? "enabled" : "disabled")));
#ifdef RTT_SUPPORT
	rtt_status = GET_RTTSTATE(dhd);
	if (rtt_status->status != RTT_ENABLED) {
#endif /* RTT_SUPPORT */
		err = wldev_ioctl_set(dev, WLC_SET_PM, &pm, sizeof(pm));
		if (unlikely(err)) {
			if (err == -ENODEV)
				WL_DBG(("net_device is not ready yet\n"));
			else
				WL_ERR(("error (%d)\n", err));
			return err;
		}
#ifdef RTT_SUPPORT
	}
#endif /* RTT_SUPPORT */
	wl_cfg80211_update_power_mode(dev);
	return err;
}

void wl_cfg80211_update_power_mode(struct net_device *dev)
{
	int err, pm = -1;

	err = wldev_ioctl_get(dev, WLC_GET_PM, &pm, sizeof(pm));
	if (err)
		WL_ERR(("%s:error (%d)\n", __FUNCTION__, err));
	else if (pm != -1 && dev->ieee80211_ptr)
		dev->ieee80211_ptr->ps = (pm == PM_OFF) ? false : true;
}

void wl_cfg80211_set_passive_scan(struct net_device *dev, char *command)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	if (strcmp(command, "SCAN-ACTIVE") == 0) {
		cfg->active_scan = 1;
	} else if (strcmp(command, "SCAN-PASSIVE") == 0) {
		cfg->active_scan = 0;
	} else
		WL_ERR(("Unknown command \n"));
}

static __used u32 wl_find_msb(u16 bit16)
{
	u32 ret = 0;

	if (bit16 & 0xff00) {
		ret += 8;
		bit16 >>= 8;
	}

	if (bit16 & 0xf0) {
		ret += 4;
		bit16 >>= 4;
	}

	if (bit16 & 0xc) {
		ret += 2;
		bit16 >>= 2;
	}

	if (bit16 & 2)
		ret += bit16 & 2;
	else if (bit16)
		ret += bit16;

	return ret;
}

static s32 wl_cfg80211_resume(struct wiphy *wiphy)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);
	s32 err = BCME_OK;

	if (unlikely(!wl_get_drv_status(cfg, READY, ndev))) {
		WL_INFORM(("device is not ready\n"));
		return err;
	}


	return err;
}


static s32
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39))
wl_cfg80211_suspend(struct wiphy *wiphy, struct cfg80211_wowlan *wow)
#else
wl_cfg80211_suspend(struct wiphy *wiphy)
#endif /* KERNEL_VERSION(2, 6, 39) || WL_COMPAT_WIRELES */
{
	s32 err = BCME_OK;
#ifdef DHD_CLEAR_ON_SUSPEND
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct net_info *iter, *next;
	struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);
	unsigned long flags;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0))
	struct cfg80211_scan_info info;
#endif

	if (unlikely(!wl_get_drv_status(cfg, READY, ndev))) {
		WL_INFORM(("device is not ready : status (%d)\n",
			(int)cfg->status));
		return err;
	}
	for_each_ndev(cfg, iter, next) {
		/* p2p discovery iface doesn't have a ndev associated with it (for kernel > 3.8) */
		if (iter->ndev)
			wl_set_drv_status(cfg, SCAN_ABORTING, iter->ndev);
		}
	spin_lock_irqsave(&cfg->cfgdrv_lock, flags);
	if (cfg->scan_request) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0))
		info.aborted = true;
		cfg80211_scan_done(cfg->scan_request, &info);
#else
		cfg80211_scan_done(cfg->scan_request, true);
#endif
		cfg->scan_request = NULL;
	}
	for_each_ndev(cfg, iter, next) {
		if (iter->ndev) {
			wl_clr_drv_status(cfg, SCANNING, iter->ndev);
			wl_clr_drv_status(cfg, SCAN_ABORTING, iter->ndev);
		}
	}
	spin_unlock_irqrestore(&cfg->cfgdrv_lock, flags);
	for_each_ndev(cfg, iter, next) {
		if (iter->ndev) {
			if (wl_get_drv_status(cfg, CONNECTING, iter->ndev)) {
				wl_bss_connect_done(cfg, iter->ndev, NULL, NULL, false);
			}
		}
	}
#endif /* DHD_CLEAR_ON_SUSPEND */


	return err;
}

static s32
wl_update_pmklist(struct net_device *dev, struct wl_pmk_list *pmk_list,
	s32 err)
{
	int i, j;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	struct net_device *primary_dev = bcmcfg_to_prmry_ndev(cfg);

	if (!pmk_list) {
		WL_MSG(dev->name, "pmk_list is NULL\n");
		return -EINVAL;
	}
	/* pmk list is supported only for STA interface i.e. primary interface
	 * Refer code wlc_bsscfg.c->wlc_bsscfg_sta_init
	 */
	if (primary_dev != dev) {
		WL_INFORM(("Not supporting Flushing pmklist on virtual"
			" interfaces than primary interface\n"));
		return err;
	}

	WL_DBG(("No of elements %d\n", pmk_list->pmkids.npmkid));
	for (i = 0; i < pmk_list->pmkids.npmkid; i++) {
		WL_DBG(("PMKID[%d]: %pM =\n", i,
			&pmk_list->pmkids.pmkid[i].BSSID));
		for (j = 0; j < WPA2_PMKID_LEN; j++) {
			WL_DBG(("%02x\n", pmk_list->pmkids.pmkid[i].PMKID[j]));
		}
	}
	if (likely(!err)) {
		err = wldev_iovar_setbuf(dev, "pmkid_info", (char *)pmk_list,
			sizeof(*pmk_list), cfg->ioctl_buf, WLC_IOCTL_MAXLEN, &cfg->ioctl_buf_sync);
	}

	return err;
}

static s32
wl_cfg80211_set_pmksa(struct wiphy *wiphy, struct net_device *dev,
	struct cfg80211_pmksa *pmksa)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	s32 err = 0;
	int i;

	RETURN_EIO_IF_NOT_UP(cfg);
	for (i = 0; i < cfg->pmk_list->pmkids.npmkid; i++)
		if (!memcmp(pmksa->bssid, &cfg->pmk_list->pmkids.pmkid[i].BSSID,
			ETHER_ADDR_LEN))
			break;
	if (i < WL_NUM_PMKIDS_MAX) {
		memcpy(&cfg->pmk_list->pmkids.pmkid[i].BSSID, pmksa->bssid,
			ETHER_ADDR_LEN);
		memcpy(&cfg->pmk_list->pmkids.pmkid[i].PMKID, pmksa->pmkid,
			WPA2_PMKID_LEN);
		if (i == cfg->pmk_list->pmkids.npmkid)
			cfg->pmk_list->pmkids.npmkid++;
	} else {
		err = -EINVAL;
	}
	WL_DBG(("set_pmksa,IW_PMKSA_ADD - PMKID: %pM =\n",
		&cfg->pmk_list->pmkids.pmkid[cfg->pmk_list->pmkids.npmkid - 1].BSSID));
	for (i = 0; i < WPA2_PMKID_LEN; i++) {
		WL_DBG(("%02x\n",
			cfg->pmk_list->pmkids.pmkid[cfg->pmk_list->pmkids.npmkid - 1].
			PMKID[i]));
	}

	err = wl_update_pmklist(dev, cfg->pmk_list, err);

	return err;
}

static s32
wl_cfg80211_del_pmksa(struct wiphy *wiphy, struct net_device *dev,
	struct cfg80211_pmksa *pmksa)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);

	struct _pmkid_list pmkid = {.npmkid = 0};
	s32 err = 0;
	int i;

	RETURN_EIO_IF_NOT_UP(cfg);
	memcpy(&pmkid.pmkid[0].BSSID, pmksa->bssid, ETHER_ADDR_LEN);
	memcpy(pmkid.pmkid[0].PMKID, pmksa->pmkid, WPA2_PMKID_LEN);

	WL_DBG(("del_pmksa,IW_PMKSA_REMOVE - PMKID: %pM =\n",
		&pmkid.pmkid[0].BSSID));
	for (i = 0; i < WPA2_PMKID_LEN; i++) {
		WL_DBG(("%02x\n", pmkid.pmkid[0].PMKID[i]));
	}

	for (i = 0; i < cfg->pmk_list->pmkids.npmkid; i++)
		if (!memcmp
		    (pmksa->bssid, &cfg->pmk_list->pmkids.pmkid[i].BSSID,
		     ETHER_ADDR_LEN))
			break;

	if ((cfg->pmk_list->pmkids.npmkid > 0) &&
		(i < cfg->pmk_list->pmkids.npmkid)) {
		memset(&cfg->pmk_list->pmkids.pmkid[i], 0, sizeof(pmkid_t));
		for (; i < (cfg->pmk_list->pmkids.npmkid - 1); i++) {
			memcpy(&cfg->pmk_list->pmkids.pmkid[i].BSSID,
				&cfg->pmk_list->pmkids.pmkid[i + 1].BSSID,
				ETHER_ADDR_LEN);
			memcpy(&cfg->pmk_list->pmkids.pmkid[i].PMKID,
				&cfg->pmk_list->pmkids.pmkid[i + 1].PMKID,
				WPA2_PMKID_LEN);
		}
		cfg->pmk_list->pmkids.npmkid--;
	} else {
		err = -EINVAL;
	}

	err = wl_update_pmklist(dev, cfg->pmk_list, err);

	return err;

}

static s32
wl_cfg80211_flush_pmksa(struct wiphy *wiphy, struct net_device *dev)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	s32 err = 0;
	RETURN_EIO_IF_NOT_UP(cfg);
	memset(cfg->pmk_list, 0, sizeof(*cfg->pmk_list));
	err = wl_update_pmklist(dev, cfg->pmk_list, err);
	return err;
}

static wl_scan_params_t *
wl_cfg80211_scan_alloc_params(struct bcm_cfg80211 *cfg, int channel, int nprobes,
	int *out_params_size)
{
	wl_scan_params_t *params;
	int params_size;
	int num_chans;
	int bssidx = 0;

	*out_params_size = 0;

	/* Our scan params only need space for 1 channel and 0 ssids */
	params_size = WL_SCAN_PARAMS_FIXED_SIZE + 1 * sizeof(uint16);
	params = (wl_scan_params_t*) kzalloc(params_size, GFP_KERNEL);
	if (params == NULL) {
		WL_ERR(("mem alloc failed (%d bytes)\n", params_size));
		return params;
	}
	memset(params, 0, params_size);
	params->nprobes = nprobes;

	num_chans = (channel == 0) ? 0 : 1;

	memcpy(&params->bssid, &ether_bcast, ETHER_ADDR_LEN);
	params->bss_type = DOT11_BSSTYPE_ANY;
	params->scan_type = DOT11_SCANTYPE_ACTIVE;
	params->nprobes = htod32(1);
	params->active_time = htod32(-1);
	params->passive_time = htod32(-1);
	params->home_time = htod32(10);
	if (channel == -1)
		params->channel_list[0] = htodchanspec(channel);
	else
		params->channel_list[0] = wl_ch_host_to_driver(cfg, bssidx, channel);

	/* Our scan params have 1 channel and 0 ssids */
	params->channel_num = htod32((0 << WL_SCAN_PARAMS_NSSID_SHIFT) |
		(num_chans & WL_SCAN_PARAMS_COUNT_MASK));

	*out_params_size = params_size;	/* rtn size to the caller */
	return params;
}

static s32
#if defined(WL_CFG80211_P2P_DEV_IF)
wl_cfg80211_remain_on_channel(struct wiphy *wiphy, bcm_struct_cfgdev *cfgdev,
	struct ieee80211_channel *channel, unsigned int duration, u64 *cookie)
#else
wl_cfg80211_remain_on_channel(struct wiphy *wiphy, bcm_struct_cfgdev *cfgdev,
	struct ieee80211_channel * channel,
	enum nl80211_channel_type channel_type,
	unsigned int duration, u64 *cookie)
#endif /* WL_CFG80211_P2P_DEV_IF */
{
	s32 target_channel;
	u32 id;
	s32 err = BCME_OK;
	struct ether_addr primary_mac;
	struct net_device *ndev = NULL;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);

#ifdef DHD_IFDEBUG
	PRINT_WDEV_INFO(cfgdev);
#endif /* DHD_IFDEBUG */

	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);

	WL_DBG(("Enter, channel: %d, duration ms (%d) SCANNING ?? %s \n",
		ieee80211_frequency_to_channel(channel->center_freq),
		duration, (wl_get_drv_status(cfg, SCANNING, ndev)) ? "YES":"NO"));

	if (!cfg->p2p) {
		WL_ERR(("cfg->p2p is not initialized\n"));
		err = BCME_ERROR;
		goto exit;
	}

#ifdef P2P_LISTEN_OFFLOADING
	if (wl_get_p2p_status(cfg, DISC_IN_PROGRESS)) {
		WL_ERR(("P2P_FIND: Discovery offload is in progress\n"));
		return -EAGAIN;
	}
#endif /* P2P_LISTEN_OFFLOADING */

#ifndef WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST
	if (wl_get_drv_status_all(cfg, SCANNING)) {
		wl_notify_escan_complete(cfg, cfg->escan_info.ndev, true, true);
	}
#endif /* not WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST */

	target_channel = ieee80211_frequency_to_channel(channel->center_freq);
	memcpy(&cfg->remain_on_chan, channel, sizeof(struct ieee80211_channel));
#if defined(WL_ENABLE_P2P_IF)
	cfg->remain_on_chan_type = channel_type;
#endif /* WL_ENABLE_P2P_IF */
	id = ++cfg->last_roc_id;
	if (id == 0)
		id = ++cfg->last_roc_id;
	*cookie = id;

#ifdef WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST
	if (wl_get_drv_status(cfg, SCANNING, ndev)) {
		timer_list_compat_t *_timer;
		WL_DBG(("scan is running. go to fake listen state\n"));

		if (duration > LONG_LISTEN_TIME) {
			wl_cfg80211_scan_abort(cfg);
		} else {
			wl_set_drv_status(cfg, FAKE_REMAINING_ON_CHANNEL, ndev);

			if (timer_pending(&cfg->p2p->listen_timer)) {
				WL_DBG(("cancel current listen timer \n"));
				del_timer_sync(&cfg->p2p->listen_timer);
			}

			_timer = &cfg->p2p->listen_timer;
			wl_clr_p2p_status(cfg, LISTEN_EXPIRED);

			INIT_TIMER(_timer, wl_cfgp2p_listen_expired, duration, 0);

			err = BCME_OK;
			goto exit;
		}
	}
#endif /* WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST */

#ifdef WL_CFG80211_SYNC_GON
	if (wl_get_drv_status_all(cfg, WAITING_NEXT_ACT_FRM_LISTEN)) {
		/* do not enter listen mode again if we are in listen mode already for next af.
		 * remain on channel completion will be returned by waiting next af completion.
		 */
#ifdef WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST
		wl_set_drv_status(cfg, FAKE_REMAINING_ON_CHANNEL, ndev);
#else
		wl_set_drv_status(cfg, REMAINING_ON_CHANNEL, ndev);
#endif /* WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST */
		goto exit;
	}
#endif /* WL_CFG80211_SYNC_GON */
	if (cfg->p2p && !cfg->p2p->on) {
		/* In case of p2p_listen command, supplicant send remain_on_channel
		 * without turning on P2P
		 */
		get_primary_mac(cfg, &primary_mac);
		wl_cfgp2p_generate_bss_mac(cfg, &primary_mac);
		p2p_on(cfg) = true;
	}

	if (p2p_is_on(cfg)) {
		err = wl_cfgp2p_enable_discovery(cfg, ndev, NULL, 0);
		if (unlikely(err)) {
			goto exit;
		}
#ifndef WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST
		wl_set_drv_status(cfg, REMAINING_ON_CHANNEL, ndev);
#endif /* not WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST */
		err = wl_cfgp2p_discover_listen(cfg, target_channel, duration);

#ifdef WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST
		if (err == BCME_OK) {
			wl_set_drv_status(cfg, REMAINING_ON_CHANNEL, ndev);
		} else {
			/* if failed, firmware may be internal scanning state.
			 * so other scan request shall not abort it
			 */
			wl_set_drv_status(cfg, FAKE_REMAINING_ON_CHANNEL, ndev);
		}
#endif /* WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST */
		/* WAR: set err = ok to prevent cookie mismatch in wpa_supplicant
		 * and expire timer will send a completion to the upper layer
		 */
		err = BCME_OK;
	}

exit:
	if (err == BCME_OK) {
		WL_INFORM(("Success\n"));
#if defined(WL_CFG80211_P2P_DEV_IF)
		cfg80211_ready_on_channel(cfgdev, *cookie, channel,
			duration, GFP_KERNEL);
#else
		cfg80211_ready_on_channel(cfgdev, *cookie, channel,
			channel_type, duration, GFP_KERNEL);
#endif /* WL_CFG80211_P2P_DEV_IF */
	} else {
		WL_ERR(("Fail to Set (err=%d cookie:%llu)\n", err, *cookie));
	}
	return err;
}

static s32
wl_cfg80211_cancel_remain_on_channel(struct wiphy *wiphy,
	bcm_struct_cfgdev *cfgdev, u64 cookie)
{
	s32 err = 0;

	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);

#ifdef P2PLISTEN_AP_SAMECHN
	struct net_device *dev;
#endif /* P2PLISTEN_AP_SAMECHN */

	RETURN_EIO_IF_NOT_UP(cfg);

#ifdef DHD_IFDEBUG
	PRINT_WDEV_INFO(cfgdev);
#endif /* DHD_IFDEBUG */

#if defined(WL_CFG80211_P2P_DEV_IF)
	if (cfgdev->iftype == NL80211_IFTYPE_P2P_DEVICE) {
		WL_DBG((" enter ) on P2P dedicated discover interface\n"));
	}
#else
	WL_DBG((" enter ) netdev_ifidx: %d \n", cfgdev->ifindex));
#endif /* WL_CFG80211_P2P_DEV_IF */

#ifdef P2PLISTEN_AP_SAMECHN
	if (cfg && cfg->p2p_resp_apchn_status) {
		dev = bcmcfg_to_prmry_ndev(cfg);
		wl_cfg80211_set_p2p_resp_ap_chn(dev, 0);
		cfg->p2p_resp_apchn_status = false;
		WL_DBG(("p2p_resp_apchn_status Turn OFF \n"));
	}
#endif /* P2PLISTEN_AP_SAMECHN */

	if (cfg->last_roc_id == cookie) {
		wl_cfgp2p_set_p2p_mode(cfg, WL_P2P_DISC_ST_SCAN, 0, 0,
			wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE));
	} else {
		WL_ERR(("%s : ignore, request cookie(%llu) is not matched. (cur : %llu)\n",
			__FUNCTION__, cookie, cfg->last_roc_id));
	}

	return err;
}

static void
wl_cfg80211_afx_handler(struct work_struct *work)
{
	struct afx_hdl *afx_instance;
	struct bcm_cfg80211 *cfg;
	s32 ret = BCME_OK;

	BCM_SET_CONTAINER_OF(afx_instance, work, struct afx_hdl, work);
	if (!afx_instance) {
		WL_ERR(("afx_instance is NULL\n"));
		return;
	}
	cfg = wl_get_cfg(afx_instance->dev);
	if (afx_instance) {
		if (cfg->afx_hdl->is_active) {
			if (cfg->afx_hdl->is_listen && cfg->afx_hdl->my_listen_chan) {
				ret = wl_cfgp2p_discover_listen(cfg, cfg->afx_hdl->my_listen_chan,
					(100 * (1 + (RANDOM32() % 3)))); /* 100ms ~ 300ms */
			} else {
				ret = wl_cfgp2p_act_frm_search(cfg, cfg->afx_hdl->dev,
					cfg->afx_hdl->bssidx, cfg->afx_hdl->peer_listen_chan,
					NULL);
			}
			if (unlikely(ret != BCME_OK)) {
				WL_ERR(("ERROR occurred! returned value is (%d)\n", ret));
				if (wl_get_drv_status_all(cfg, FINDING_COMMON_CHANNEL))
					complete(&cfg->act_frm_scan);
			}
		}
	}
}

static s32
wl_cfg80211_af_searching_channel(struct bcm_cfg80211 *cfg, struct net_device *dev)
{
	u32 max_retry = WL_CHANNEL_SYNC_RETRY;
	bool is_p2p_gas = false;

	if (dev == NULL)
		return -1;

	WL_DBG((" enter ) \n"));

	wl_set_drv_status(cfg, FINDING_COMMON_CHANNEL, dev);
	cfg->afx_hdl->is_active = TRUE;

	if (cfg->afx_hdl->pending_tx_act_frm) {
		wl_action_frame_t *action_frame;
		action_frame = &(cfg->afx_hdl->pending_tx_act_frm->action_frame);
		if (wl_cfgp2p_is_p2p_gas_action(action_frame->data, action_frame->len))
			is_p2p_gas = true;
	}

	/* Loop to wait until we find a peer's channel or the
	 * pending action frame tx is cancelled.
	 */
	while ((cfg->afx_hdl->retry < max_retry) &&
		(cfg->afx_hdl->peer_chan == WL_INVALID)) {
		cfg->afx_hdl->is_listen = FALSE;
		wl_set_drv_status(cfg, SCANNING, dev);
		WL_DBG(("Scheduling the action frame for sending.. retry %d\n",
			cfg->afx_hdl->retry));
		/* search peer on peer's listen channel */
		schedule_work(&cfg->afx_hdl->work);
		wait_for_completion_timeout(&cfg->act_frm_scan,
			msecs_to_jiffies(WL_AF_SEARCH_TIME_MAX));

		if ((cfg->afx_hdl->peer_chan != WL_INVALID) ||
			!(wl_get_drv_status(cfg, FINDING_COMMON_CHANNEL, dev)))
			break;

		if (is_p2p_gas)
			break;

		if (cfg->afx_hdl->my_listen_chan) {
			WL_DBG(("Scheduling Listen peer in my listen channel = %d\n",
				cfg->afx_hdl->my_listen_chan));
			/* listen on my listen channel */
			cfg->afx_hdl->is_listen = TRUE;
			schedule_work(&cfg->afx_hdl->work);
			wait_for_completion_timeout(&cfg->act_frm_scan,
				msecs_to_jiffies(WL_AF_SEARCH_TIME_MAX));
		}
		if ((cfg->afx_hdl->peer_chan != WL_INVALID) ||
			!(wl_get_drv_status(cfg, FINDING_COMMON_CHANNEL, dev)))
			break;

		cfg->afx_hdl->retry++;

		WL_AF_TX_KEEP_PRI_CONNECTION_VSDB(cfg);
	}

	cfg->afx_hdl->is_active = FALSE;

	wl_clr_drv_status(cfg, SCANNING, dev);
	wl_clr_drv_status(cfg, FINDING_COMMON_CHANNEL, dev);

	return (cfg->afx_hdl->peer_chan);
}

struct p2p_config_af_params {
	s32 max_tx_retry;	/* max tx retry count if tx no ack */
#ifdef WL_CFG80211_SYNC_GON
	bool extra_listen;
#endif
	bool search_channel;	/* 1: search peer's channel to send af */
};

static s32
wl_cfg80211_config_p2p_pub_af_tx(struct wiphy *wiphy,
	wl_action_frame_t *action_frame, wl_af_params_t *af_params,
	struct p2p_config_af_params *config_af_params)
{
	s32 err = BCME_OK;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	wifi_p2p_pub_act_frame_t *act_frm =
		(wifi_p2p_pub_act_frame_t *) (action_frame->data);

	/* initialize default value */
#ifdef WL_CFG80211_SYNC_GON
	config_af_params->extra_listen = true;
#endif
	config_af_params->search_channel = false;
	config_af_params->max_tx_retry = WL_AF_TX_MAX_RETRY;
	cfg->next_af_subtype = P2P_PAF_SUBTYPE_INVALID;

	switch (act_frm->subtype) {
	case P2P_PAF_GON_REQ: {
		WL_DBG(("P2P: GO_NEG_PHASE status set \n"));
		wl_set_p2p_status(cfg, GO_NEG_PHASE);

		config_af_params->search_channel = true;
		cfg->next_af_subtype = act_frm->subtype + 1;

		/* increase dwell time to wait for RESP frame */
		af_params->dwell_time = WL_MED_DWELL_TIME;

		break;
	}
	case P2P_PAF_GON_RSP: {
		cfg->next_af_subtype = act_frm->subtype + 1;
		/* increase dwell time to wait for CONF frame */
		af_params->dwell_time = WL_MED_DWELL_TIME + 100;
		break;
	}
	case P2P_PAF_GON_CONF: {
		/* If we reached till GO Neg confirmation reset the filter */
		WL_DBG(("P2P: GO_NEG_PHASE status cleared \n"));
		wl_clr_p2p_status(cfg, GO_NEG_PHASE);

		/* minimize dwell time */
		af_params->dwell_time = WL_MIN_DWELL_TIME;

#ifdef WL_CFG80211_SYNC_GON
		config_af_params->extra_listen = false;
#endif /* WL_CFG80211_SYNC_GON */
		break;
	}
	case P2P_PAF_INVITE_REQ: {
		config_af_params->search_channel = true;
		cfg->next_af_subtype = act_frm->subtype + 1;

		/* increase dwell time */
		af_params->dwell_time = WL_MED_DWELL_TIME;
		break;
	}
	case P2P_PAF_INVITE_RSP:
		/* minimize dwell time */
		af_params->dwell_time = WL_MIN_DWELL_TIME;
#ifdef WL_CFG80211_SYNC_GON
		config_af_params->extra_listen = false;
#endif /* WL_CFG80211_SYNC_GON */
		break;
	case P2P_PAF_DEVDIS_REQ: {
		if (IS_ACTPUB_WITHOUT_GROUP_ID(&act_frm->elts[0],
			action_frame->len)) {
			config_af_params->search_channel = true;
		}

		cfg->next_af_subtype = act_frm->subtype + 1;
		/* maximize dwell time to wait for RESP frame */
		af_params->dwell_time = WL_LONG_DWELL_TIME;
		break;
	}
	case P2P_PAF_DEVDIS_RSP:
		/* minimize dwell time */
		af_params->dwell_time = WL_MIN_DWELL_TIME;
#ifdef WL_CFG80211_SYNC_GON
		config_af_params->extra_listen = false;
#endif /* WL_CFG80211_SYNC_GON */
		break;
	case P2P_PAF_PROVDIS_REQ: {
		if (IS_ACTPUB_WITHOUT_GROUP_ID(&act_frm->elts[0],
			action_frame->len)) {
			config_af_params->search_channel = true;
		}

		cfg->next_af_subtype = act_frm->subtype + 1;
		/* increase dwell time to wait for RESP frame */
		af_params->dwell_time = WL_MED_DWELL_TIME;
		break;
	}
	case P2P_PAF_PROVDIS_RSP: {
		cfg->next_af_subtype = P2P_PAF_GON_REQ;
		af_params->dwell_time = WL_MED_DWELL_TIME;
#ifdef WL_CFG80211_SYNC_GON
		config_af_params->extra_listen = false;
#endif /* WL_CFG80211_SYNC_GON */
		break;
	}
	default:
		WL_DBG(("Unknown p2p pub act frame subtype: %d\n",
			act_frm->subtype));
		err = BCME_BADARG;
	}
	return err;
}

#ifdef WL11U
static bool
wl_cfg80211_check_DFS_channel(struct bcm_cfg80211 *cfg, wl_af_params_t *af_params,
	void *frame, u16 frame_len)
{
	struct wl_scan_results *bss_list;
	struct wl_bss_info *bi = NULL;
	bool result = false;
	s32 i;
	chanspec_t chanspec;

	/* If DFS channel is 52~148, check to block it or not */
	if (af_params &&
		(af_params->channel >= 52 && af_params->channel <= 148)) {
		if (!wl_cfgp2p_is_p2p_action(frame, frame_len)) {
			bss_list = cfg->bss_list;
			bi = next_bss(bss_list, bi);
			for_each_bss(bss_list, bi, i) {
				chanspec = wl_chspec_driver_to_host(bi->chanspec);
				if (CHSPEC_IS5G(chanspec) &&
					((bi->ctl_ch ? bi->ctl_ch : CHSPEC_CHANNEL(chanspec))
					== af_params->channel)) {
					result = true;	/* do not block the action frame */
					break;
				}
			}
		}
	}
	else {
		result = true;
	}

	WL_DBG(("result=%s", result?"true":"false"));
	return result;
}
#endif /* WL11U */
static bool
wl_cfg80211_check_dwell_overflow(int32 requested_dwell, ulong dwell_jiffies)
{
	if ((requested_dwell & CUSTOM_RETRY_MASK) &&
			(jiffies_to_msecs(jiffies - dwell_jiffies) >
			 (requested_dwell & ~CUSTOM_RETRY_MASK))) {
		WL_ERR(("Action frame TX retry time over dwell time!\n"));
		return true;
	}
	return false;
}

static bool
wl_cfg80211_send_action_frame(struct wiphy *wiphy, struct net_device *dev,
	bcm_struct_cfgdev *cfgdev, wl_af_params_t *af_params,
	wl_action_frame_t *action_frame, u16 action_frame_len, s32 bssidx)
{
#ifdef WL11U
	struct net_device *ndev = NULL;
#endif /* WL11U */
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	bool ack = false;
	u8 category, action;
	s32 tx_retry;
	struct p2p_config_af_params config_af_params;
	struct net_info *netinfo;
#ifdef VSDB
	ulong off_chan_started_jiffies = 0;
#endif
	ulong dwell_jiffies = 0;
	bool dwell_overflow = false;
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);
	bool miss_gon_cfm = false;

	int32 requested_dwell = af_params->dwell_time;

	/* Add the default dwell time
	 * Dwell time to stay off-channel to wait for a response action frame
	 * after transmitting an GO Negotiation action frame
	 */
	af_params->dwell_time = WL_DWELL_TIME;

#ifdef WL11U
#if defined(WL_CFG80211_P2P_DEV_IF)
	ndev = dev;
#else
	ndev = ndev_to_cfgdev(cfgdev);
#endif /* WL_CFG80211_P2P_DEV_IF */
#endif /* WL11U */

	category = action_frame->data[DOT11_ACTION_CAT_OFF];
	action = action_frame->data[DOT11_ACTION_ACT_OFF];

	/* initialize variables */
	tx_retry = 0;
	cfg->next_af_subtype = P2P_PAF_SUBTYPE_INVALID;
	config_af_params.max_tx_retry = WL_AF_TX_MAX_RETRY;
	config_af_params.search_channel = false;
#ifdef WL_CFG80211_SYNC_GON
	config_af_params.extra_listen = false;
#endif

	/* config parameters */
	/* Public Action Frame Process - DOT11_ACTION_CAT_PUBLIC */
	if (category == DOT11_ACTION_CAT_PUBLIC) {
		if ((action == P2P_PUB_AF_ACTION) &&
			(action_frame_len >= sizeof(wifi_p2p_pub_act_frame_t))) {
			/* p2p public action frame process */
			if (BCME_OK != wl_cfg80211_config_p2p_pub_af_tx(wiphy,
				action_frame, af_params, &config_af_params)) {
				WL_DBG(("Unknown subtype.\n"));
			}

		} else if (action_frame_len >= sizeof(wifi_p2psd_gas_pub_act_frame_t)) {
			/* service discovery process */
			if (action == P2PSD_ACTION_ID_GAS_IREQ ||
				action == P2PSD_ACTION_ID_GAS_CREQ) {
				/* configure service discovery query frame */

				config_af_params.search_channel = true;

				/* save next af suptype to cancel remained dwell time */
				cfg->next_af_subtype = action + 1;

				af_params->dwell_time = WL_MED_DWELL_TIME;
				if (requested_dwell & CUSTOM_RETRY_MASK) {
					config_af_params.max_tx_retry =
						(requested_dwell & CUSTOM_RETRY_MASK) >> 24;
					af_params->dwell_time =
						(requested_dwell & ~CUSTOM_RETRY_MASK);
					WL_DBG(("Custom retry(%d) and dwell time(%d) is set.\n",
						config_af_params.max_tx_retry,
						af_params->dwell_time));
				}
			} else if (action == P2PSD_ACTION_ID_GAS_IRESP ||
				action == P2PSD_ACTION_ID_GAS_CRESP) {
				/* configure service discovery response frame */
				af_params->dwell_time = WL_MIN_DWELL_TIME;
			} else {
				WL_DBG(("Unknown action type: %d\n", action));
			}
		} else {
			WL_DBG(("Unknown Frame: category 0x%x, action 0x%x, length %d\n",
				category, action, action_frame_len));
		}
	} else if (category == P2P_AF_CATEGORY) {
		/* do not configure anything. it will be sent with a default configuration */
	} else {
		WL_DBG(("Unknown Frame: category 0x%x, action 0x%x\n",
			category, action));
		if (dhd->op_mode & DHD_FLAG_HOSTAP_MODE) {
			wl_clr_drv_status(cfg, SENDING_ACT_FRM, dev);
			return false;
		}
	}
	netinfo = wl_get_netinfo_by_bssidx(cfg, bssidx);
	/* validate channel and p2p ies */
	if (config_af_params.search_channel && IS_P2P_SOCIAL(af_params->channel) &&
		netinfo && netinfo->bss.ies.probe_req_ie_len) {
		config_af_params.search_channel = true;
	} else {
		config_af_params.search_channel = false;
	}
#ifdef WL11U
	if (ndev == bcmcfg_to_prmry_ndev(cfg))
		config_af_params.search_channel = false;
#endif /* WL11U */

#ifdef VSDB
	/* if connecting on primary iface, sleep for a while before sending af tx for VSDB */
	if (wl_get_drv_status(cfg, CONNECTING, bcmcfg_to_prmry_ndev(cfg))) {
		OSL_SLEEP(50);
	}
#endif

	/* if scan is ongoing, abort current scan. */
	if (wl_get_drv_status_all(cfg, SCANNING)) {
		wl_notify_escan_complete(cfg, cfg->escan_info.ndev, true, true);
	}

	/* Abort P2P listen */
	if (discover_cfgdev(cfgdev, cfg)) {
		if (cfg->p2p_supported && cfg->p2p) {
			wl_cfgp2p_set_p2p_mode(cfg, WL_P2P_DISC_ST_SCAN, 0, 0,
				wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE));
		}
	}

#ifdef WL11U
	/* handling DFS channel exceptions */
	if (!wl_cfg80211_check_DFS_channel(cfg, af_params, action_frame->data, action_frame->len)) {
		return false;	/* the action frame was blocked */
	}
#endif /* WL11U */

	/* set status and destination address before sending af */
	if (cfg->next_af_subtype != P2P_PAF_SUBTYPE_INVALID) {
		/* set this status to cancel the remained dwell time in rx process */
		wl_set_drv_status(cfg, WAITING_NEXT_ACT_FRM, dev);
	}
	wl_set_drv_status(cfg, SENDING_ACT_FRM, dev);
	memcpy(cfg->afx_hdl->tx_dst_addr.octet,
		af_params->action_frame.da.octet,
		sizeof(cfg->afx_hdl->tx_dst_addr.octet));

	/* save af_params for rx process */
	cfg->afx_hdl->pending_tx_act_frm = af_params;

	if (wl_cfgp2p_is_p2p_gas_action(action_frame->data, action_frame->len)) {
		WL_DBG(("Set GAS action frame config.\n"));
		config_af_params.search_channel = false;
		config_af_params.max_tx_retry = 1;
	}

	/* search peer's channel */
	if (config_af_params.search_channel) {
		/* initialize afx_hdl */
		if ((cfg->afx_hdl->bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
			WL_ERR(("Find p2p index from wdev(%p) failed\n", dev->ieee80211_ptr));
			goto exit;
		}
		cfg->afx_hdl->dev = dev;
		cfg->afx_hdl->retry = 0;
		cfg->afx_hdl->peer_chan = WL_INVALID;

		if (wl_cfg80211_af_searching_channel(cfg, dev) == WL_INVALID) {
			WL_ERR(("couldn't find peer's channel.\n"));
			wl_cfgp2p_print_actframe(true, action_frame->data, action_frame->len,
				af_params->channel);
			/* Even if we couldn't find peer channel, try to send the frame
			 * out. P2P cert 5.1.14 testbed device (realtek) doesn't seem to
			 * respond to probe request (Ideally it has to be in listen and
			 * responsd to probe request). However if we send Go neg req, the
			 * peer is sending GO-neg resp. So instead of giving up here, just
			 * proceed and attempt sending out the action frame.
			 */
		}

		wl_clr_drv_status(cfg, SCANNING, cfg->afx_hdl->dev);
		/*
		 * Abort scan even for VSDB scenarios. Scan gets aborted in firmware
		 * but after the check of piggyback algorithm.
		 * To take care of current piggback algo, lets abort the scan here itself.
		 */
		wl_notify_escan_complete(cfg, dev, true, true);
		/* Suspend P2P discovery's search-listen to prevent it from
		 * starting a scan or changing the channel.
		 */
		if ((wl_cfgp2p_discover_enable_search(cfg, false)) < 0) {
			WL_ERR(("Can not disable discovery mode\n"));
			goto exit;
		}

		/* update channel */
		if (cfg->afx_hdl->peer_chan != WL_INVALID) {
			af_params->channel = cfg->afx_hdl->peer_chan;
			WL_ERR(("Attempt tx on peer listen channel:%d ",
				cfg->afx_hdl->peer_chan));
		} else {
			WL_ERR(("Attempt tx with the channel provided by userspace."
			"Channel: %d\n", af_params->channel));
		}
	}

#ifdef VSDB
	off_chan_started_jiffies = jiffies;
#endif /* VSDB */

	wl_cfgp2p_print_actframe(true, action_frame->data, action_frame->len, af_params->channel);

	wl_cfgp2p_need_wait_actfrmae(cfg, action_frame->data, action_frame->len, true);

	dwell_jiffies = jiffies;
	/* Now send a tx action frame */
	ack = wl_cfgp2p_tx_action_frame(cfg, dev, af_params, bssidx) ? false : true;
	dwell_overflow = wl_cfg80211_check_dwell_overflow(requested_dwell, dwell_jiffies);

	if (ack && (wl_get_drv_status_all(cfg, WAITING_NEXT_ACT_FRM))) {
		wifi_p2p_pub_act_frame_t *pact_frm;
		pact_frm = (wifi_p2p_pub_act_frame_t *)(action_frame->data);
		if (pact_frm->subtype == P2P_PAF_GON_RSP) {
			WL_ERR(("Miss GO Nego cfm after P2P_PAF_GON_RSP\n"));
			miss_gon_cfm = true;
		}
	}

	/* if failed, retry it. tx_retry_max value is configure by .... */
	while ((miss_gon_cfm || (ack == false)) && (tx_retry++ < config_af_params.max_tx_retry) &&
			!dwell_overflow) {
#ifdef VSDB
		if (af_params->channel) {
			if (jiffies_to_msecs(jiffies - off_chan_started_jiffies) >
				OFF_CHAN_TIME_THRESHOLD_MS) {
				WL_AF_TX_KEEP_PRI_CONNECTION_VSDB(cfg);
				off_chan_started_jiffies = jiffies;
			} else
				OSL_SLEEP(AF_RETRY_DELAY_TIME);
		}
#endif /* VSDB */
		ack = wl_cfgp2p_tx_action_frame(cfg, dev, af_params, bssidx) ?
			false : true;
		if (miss_gon_cfm && !wl_get_drv_status_all(cfg, WAITING_NEXT_ACT_FRM)) {
			WL_ERR(("Received GO Nego cfm after P2P_PAF_GON_RSP\n"));
			miss_gon_cfm = false;
		}
		dwell_overflow = wl_cfg80211_check_dwell_overflow(requested_dwell, dwell_jiffies);
	}

	if (ack == false) {
		WL_ERR(("Failed to send Action Frame(retry %d)\n", tx_retry));
	}
	WL_DBG(("Complete to send action frame\n"));
exit:
	/* Clear SENDING_ACT_FRM after all sending af is done */
	wl_clr_drv_status(cfg, SENDING_ACT_FRM, dev);

#ifdef WL_CFG80211_SYNC_GON
	/* WAR: sometimes dongle does not keep the dwell time of 'actframe'.
	 * if we coundn't get the next action response frame and dongle does not keep
	 * the dwell time, go to listen state again to get next action response frame.
	 */
	if (ack && config_af_params.extra_listen &&
		wl_get_drv_status_all(cfg, WAITING_NEXT_ACT_FRM) &&
		cfg->af_sent_channel == cfg->afx_hdl->my_listen_chan) {
		s32 extar_listen_time;

		extar_listen_time = af_params->dwell_time -
			jiffies_to_msecs(jiffies - cfg->af_tx_sent_jiffies);

		if (extar_listen_time > 50) {
			wl_set_drv_status(cfg, WAITING_NEXT_ACT_FRM_LISTEN, dev);
			WL_DBG(("Wait more time! actual af time:%d,"
				"calculated extar listen:%d\n",
				af_params->dwell_time, extar_listen_time));
			if (wl_cfgp2p_discover_listen(cfg, cfg->af_sent_channel,
				extar_listen_time + 100) == BCME_OK) {
				wait_for_completion_timeout(&cfg->wait_next_af,
					msecs_to_jiffies(extar_listen_time + 100 + 300));
			}
			wl_clr_drv_status(cfg, WAITING_NEXT_ACT_FRM_LISTEN, dev);
		}
	}
#endif /* WL_CFG80211_SYNC_GON */
	wl_clr_drv_status(cfg, WAITING_NEXT_ACT_FRM, dev);
	cfg->afx_hdl->pending_tx_act_frm = NULL;

	WL_INFORM(("-- sending Action Frame is %s, listen chan: %d\n",
		(ack) ? "Succeeded!!":"Failed!!", cfg->afx_hdl->my_listen_chan));

	return ack;
}

#define MAX_NUM_OF_ASSOCIATED_DEV       64
static s32
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
wl_cfg80211_mgmt_tx(struct wiphy *wiphy, bcm_struct_cfgdev *cfgdev,
	struct cfg80211_mgmt_tx_params *params, u64 *cookie)
#else
wl_cfg80211_mgmt_tx(struct wiphy *wiphy, bcm_struct_cfgdev *cfgdev,
	struct ieee80211_channel *channel, bool offchan,
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(3, 7, 0))
	enum nl80211_channel_type channel_type,
	bool channel_type_valid,
#endif /* LINUX_VERSION_CODE <= KERNEL_VERSION(3, 7, 0) */
	unsigned int wait, const u8* buf, size_t len,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0))
	bool no_cck,
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0))
	bool dont_wait_for_ack,
#endif
	u64 *cookie)
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0) */
{
	wl_action_frame_t *action_frame;
	wl_af_params_t *af_params;
	scb_val_t scb_val;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
	struct ieee80211_channel *channel = params->chan;
	const u8 *buf = params->buf;
	size_t len = params->len;
#endif
	const struct ieee80211_mgmt *mgmt;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct net_device *dev = NULL;
	s32 err = BCME_OK;
	s32 bssidx = 0;
	u32 id;
	bool ack = false;
	s8 eabuf[ETHER_ADDR_STR_LEN];

	WL_DBG(("Enter \n"));

	if (len > ACTION_FRAME_SIZE) {
		WL_ERR(("bad length:%zu\n", len));
		return BCME_BADLEN;
	}
#ifdef DHD_IFDEBUG
	PRINT_WDEV_INFO(cfgdev);
#endif /* DHD_IFDEBUG */

	dev = cfgdev_to_wlc_ndev(cfgdev, cfg);

	if (!dev) {
		WL_ERR(("dev is NULL\n"));
		return -EINVAL;
	}

	/* set bsscfg idx for iovar (wlan0: P2PAPI_BSSCFG_PRIMARY, p2p: P2PAPI_BSSCFG_DEVICE)	*/
	if (discover_cfgdev(cfgdev, cfg)) {
		if (!cfg->p2p_supported || !cfg->p2p) {
			WL_ERR(("P2P doesn't setup completed yet\n"));
			return -EINVAL;
		}
		bssidx = wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE);
	}
	else {
		if ((bssidx = wl_get_bssidx_by_wdev(cfg, cfgdev_to_wdev(cfgdev))) < 0) {
			WL_ERR(("Find p2p index failed\n"));
			return BCME_ERROR;
		}
	}

	WL_DBG(("TX target bssidx=%d\n", bssidx));

	if (p2p_is_on(cfg)) {
		/* Suspend P2P discovery search-listen to prevent it from changing the
		 * channel.
		 */
		if ((err = wl_cfgp2p_discover_enable_search(cfg, false)) < 0) {
			WL_ERR(("Can not disable discovery mode\n"));
			return -EFAULT;
		}
	}
	*cookie = 0;
	id = cfg->send_action_id++;
	if (id == 0)
		id = cfg->send_action_id++;
	*cookie = id;
	mgmt = (const struct ieee80211_mgmt *)buf;
	if (ieee80211_is_mgmt(mgmt->frame_control)) {
		if (ieee80211_is_probe_resp(mgmt->frame_control)) {
			s32 ie_offset =  DOT11_MGMT_HDR_LEN + DOT11_BCN_PRB_FIXED_LEN;
			s32 ie_len = len - ie_offset;
			if ((dev == bcmcfg_to_prmry_ndev(cfg)) && cfg->p2p) {
				bssidx = wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE);
			}
			wl_cfg80211_set_mgmt_vndr_ies(cfg, ndev_to_cfgdev(dev), bssidx,
				VNDR_IE_PRBRSP_FLAG, (const u8 *)(buf + ie_offset), ie_len);
			cfg80211_mgmt_tx_status(cfgdev, *cookie, buf, len, true, GFP_KERNEL);
#if defined(P2P_IE_MISSING_FIX)
			if (!cfg->p2p_prb_noti) {
				cfg->p2p_prb_noti = true;
				WL_DBG(("%s: TX 802_1X Probe Response first time.\n",
					__FUNCTION__));
			}
#endif
			goto exit;
		} else if (ieee80211_is_disassoc(mgmt->frame_control) ||
			ieee80211_is_deauth(mgmt->frame_control)) {
			char mac_buf[MAX_NUM_OF_ASSOCIATED_DEV *
				sizeof(struct ether_addr) + sizeof(uint)] = {0};
			int num_associated = 0;
			struct maclist *assoc_maclist = (struct maclist *)mac_buf;
			if (!bcmp((const uint8 *)BSSID_BROADCAST,
				(const struct ether_addr *)mgmt->da, ETHER_ADDR_LEN)) {
				assoc_maclist->count = MAX_NUM_OF_ASSOCIATED_DEV;
				err = wldev_ioctl_get(dev, WLC_GET_ASSOCLIST,
					assoc_maclist, sizeof(mac_buf));
				if (err < 0)
					WL_ERR(("WLC_GET_ASSOCLIST error %d\n", err));
				else
					num_associated = assoc_maclist->count;
			}
			memcpy(scb_val.ea.octet, mgmt->da, ETH_ALEN);
			scb_val.val = mgmt->u.disassoc.reason_code;
			err = wldev_ioctl_set(dev, WLC_SCB_DEAUTHENTICATE_FOR_REASON, &scb_val,
				sizeof(scb_val_t));
			if (err < 0)
				WL_ERR(("WLC_SCB_DEAUTHENTICATE_FOR_REASON error %d\n", err));
			WL_ERR(("Disconnect STA : %s scb_val.val %d\n",
				bcm_ether_ntoa((const struct ether_addr *)mgmt->da, eabuf),
				scb_val.val));

			if (num_associated > 0 && ETHER_ISBCAST(mgmt->da))
				wl_delay(400);

			cfg80211_mgmt_tx_status(cfgdev, *cookie, buf, len, true, GFP_KERNEL);
			goto exit;

		} else if (ieee80211_is_action(mgmt->frame_control)) {
			/* Abort the dwell time of any previous off-channel
			* action frame that may be still in effect.  Sending
			* off-channel action frames relies on the driver's
			* scan engine.  If a previous off-channel action frame
			* tx is still in progress (including the dwell time),
			* then this new action frame will not be sent out.
			*/
/* Do not abort scan for VSDB. Scan will be aborted in firmware if necessary.
 * And previous off-channel action frame must be ended before new af tx.
 */
#ifndef WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST
			wl_notify_escan_complete(cfg, dev, true, true);
#endif /* not WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST */
		}

	} else {
		WL_ERR(("Driver only allows MGMT packet type\n"));
		goto exit;
	}

	af_params = (wl_af_params_t *) kzalloc(WL_WIFI_AF_PARAMS_SIZE, GFP_KERNEL);

	if (af_params == NULL)
	{
		WL_ERR(("unable to allocate frame\n"));
		return -ENOMEM;
	}

	action_frame = &af_params->action_frame;

	/* Add the packet Id */
	action_frame->packetId = *cookie;
	WL_DBG(("action frame %d\n", action_frame->packetId));
	/* Add BSSID */
	memcpy(&action_frame->da, &mgmt->da[0], ETHER_ADDR_LEN);
	memcpy(&af_params->BSSID, &mgmt->bssid[0], ETHER_ADDR_LEN);

	/* Add the length exepted for 802.11 header  */
	action_frame->len = len - DOT11_MGMT_HDR_LEN;
	WL_DBG(("action_frame->len: %d\n", action_frame->len));

	/* Add the channel */
	af_params->channel =
		ieee80211_frequency_to_channel(channel->center_freq);
	/* Save listen_chan for searching common channel */
	cfg->afx_hdl->peer_listen_chan = af_params->channel;
	WL_DBG(("channel from upper layer %d\n", cfg->afx_hdl->peer_listen_chan));

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
	af_params->dwell_time = params->wait;
#else
	af_params->dwell_time = wait;
#endif

	memcpy(action_frame->data, &buf[DOT11_MGMT_HDR_LEN], action_frame->len);

	ack = wl_cfg80211_send_action_frame(wiphy, dev, cfgdev, af_params,
		action_frame, action_frame->len, bssidx);
	cfg80211_mgmt_tx_status(cfgdev, *cookie, buf, len, ack, GFP_KERNEL);

	kfree(af_params);
exit:
	return err;
}


static void
wl_cfg80211_mgmt_frame_register(struct wiphy *wiphy, bcm_struct_cfgdev *cfgdev,
	u16 frame, bool reg)
{

	WL_DBG(("frame_type: %x, reg: %d\n", frame, reg));

	if (frame != (IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_PROBE_REQ))
		return;

	return;
}


static s32
wl_cfg80211_change_bss(struct wiphy *wiphy,
	struct net_device *dev,
	struct bss_parameters *params)
{
	s32 err = 0;
	s32 ap_isolate = 0;
#ifdef PCIE_FULL_DONGLE
	s32 ifidx = DHD_BAD_IF;
#endif
#if defined(PCIE_FULL_DONGLE)
	dhd_pub_t *dhd;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	dhd = (dhd_pub_t *)(cfg->pub);
#if defined(WL_ENABLE_P2P_IF)
	if (cfg->p2p_net == dev)
		dev = bcmcfg_to_prmry_ndev(cfg);
#endif
#endif 

	if (params->use_cts_prot >= 0) {
	}

	if (params->use_short_preamble >= 0) {
	}

	if (params->use_short_slot_time >= 0) {
	}

	if (params->basic_rates) {
	}

	if (params->ap_isolate >= 0) {
		ap_isolate = params->ap_isolate;
#ifdef PCIE_FULL_DONGLE
		ifidx = dhd_net2idx(dhd->info, dev);

		if (ifidx != DHD_BAD_IF) {
			err = dhd_set_ap_isolate(dhd, ifidx, ap_isolate);
		} else {
			WL_ERR(("Failed to set ap_isolate\n"));
		}
#else
		err = wldev_iovar_setint(dev, "ap_isolate", ap_isolate);
		if (unlikely(err))
		{
			WL_ERR(("set ap_isolate Error (%d)\n", err));
		}
#endif /* PCIE_FULL_DONGLE */
	}

	if (params->ht_opmode >= 0) {
	}


	return err;
}

static s32
wl_cfg80211_set_channel(struct wiphy *wiphy, struct net_device *dev,
	struct ieee80211_channel *chan,
	enum nl80211_channel_type channel_type)
{
	s32 _chan;
	chanspec_t chspec = 0;
	chanspec_t fw_chspec = 0;
	u32 bw = WL_CHANSPEC_BW_20;
#ifdef WL11ULB
	u32 ulb_bw = wl_cfg80211_get_ulb_bw(wl_get_cfg(dev), dev->ieee80211_ptr);
#endif /* WL11ULB */

	s32 err = BCME_OK;
	s32 bw_cap = 0;
	struct {
		u32 band;
		u32 bw_cap;
	} param = {0, 0};
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
#if defined(CUSTOM_SET_CPUCORE) || (defined(WL_VIRTUAL_APSTA) && \
	defined(APSTA_RESTRICTED_CHANNEL)) || defined(WL_EXT_IAPSTA)
	dhd_pub_t *dhd =  (dhd_pub_t *)(cfg->pub);
#endif /* CUSTOM_SET_CPUCORE || (WL_VIRTUAL_APSTA && APSTA_RESTRICTED_CHANNEL) */

	dev = ndev_to_wlc_ndev(dev, cfg);
	_chan = ieee80211_frequency_to_channel(chan->center_freq);
#ifdef WL_EXT_IAPSTA
	_chan = wl_ext_iapsta_update_channel(dhd, dev, _chan);
#endif
	WL_MSG(dev->name, "netdev_ifidx(%d), chan_type(%d) target channel(%d) \n",
		dev->ifindex, channel_type, _chan);


#if defined(WL_VIRTUAL_APSTA) && defined(APSTA_RESTRICTED_CHANNEL)
#define DEFAULT_2G_SOFTAP_CHANNEL	1
#define DEFAULT_5G_SOFTAP_CHANNEL	149
	if (wl_get_mode_by_netdev(cfg, dev) == WL_MODE_AP &&
		(dhd->op_mode & DHD_FLAG_CONCURR_STA_HOSTAP_MODE) ==
		DHD_FLAG_CONCURR_STA_HOSTAP_MODE &&
		wl_get_drv_status(cfg, CONNECTED, bcmcfg_to_prmry_ndev(cfg))) {
		u32 *sta_chan = (u32 *)wl_read_prof(cfg,
			bcmcfg_to_prmry_ndev(cfg), WL_PROF_CHAN);
		u32 sta_band = (*sta_chan > CH_MAX_2G_CHANNEL) ?
			IEEE80211_BAND_5GHZ : IEEE80211_BAND_2GHZ;
		if (chan->band == sta_band) {
			/* Do not try SCC in 5GHz if channel is not CH149 */
			_chan = (sta_band == IEEE80211_BAND_5GHZ &&
				*sta_chan != DEFAULT_5G_SOFTAP_CHANNEL) ?
				DEFAULT_2G_SOFTAP_CHANNEL : *sta_chan;
			WL_ERR(("target channel will be changed to %d\n", _chan));
			if (_chan <= CH_MAX_2G_CHANNEL) {
				bw = WL_CHANSPEC_BW_20;
				goto set_channel;
			}
		}
	}
#undef DEFAULT_2G_SOFTAP_CHANNEL
#undef DEFAULT_5G_SOFTAP_CHANNEL
#endif /* WL_VIRTUAL_APSTA && APSTA_RESTRICTED_CHANNEL */

#ifdef WL11ULB
	if (ulb_bw) {
		WL_DBG(("[ULB] setting AP/GO BW to ulb_bw 0x%x \n", ulb_bw));
		bw = wl_cfg80211_ulbbw_to_ulbchspec(ulb_bw);
		goto set_channel;
	}
#endif /* WL11ULB */
	if (chan->band == IEEE80211_BAND_5GHZ) {
		param.band = WLC_BAND_5G;
		err = wldev_iovar_getbuf(dev, "bw_cap", &param, sizeof(param),
			cfg->ioctl_buf, WLC_IOCTL_SMLEN, &cfg->ioctl_buf_sync);
		if (err) {
			if (err != BCME_UNSUPPORTED) {
				WL_ERR(("bw_cap failed, %d\n", err));
				return err;
			} else {
				err = wldev_iovar_getint(dev, "mimo_bw_cap", &bw_cap);
				if (err) {
					WL_ERR(("error get mimo_bw_cap (%d)\n", err));
				}
				if (bw_cap != WLC_N_BW_20ALL)
					bw = WL_CHANSPEC_BW_40;
			}
		} else {
			if (WL_BW_CAP_80MHZ(cfg->ioctl_buf[0]))
				bw = WL_CHANSPEC_BW_80;
			else if (WL_BW_CAP_40MHZ(cfg->ioctl_buf[0]))
				bw = WL_CHANSPEC_BW_40;
			else
				bw = WL_CHANSPEC_BW_20;

		}

	} else if (chan->band == IEEE80211_BAND_2GHZ)
		bw = WL_CHANSPEC_BW_20;
set_channel:
	chspec = wf_channel2chspec(_chan, bw);
	if (wf_chspec_valid(chspec)) {
		fw_chspec = wl_chspec_host_to_driver(chspec);
		if (fw_chspec != INVCHANSPEC) {
			if ((err = wldev_iovar_setint(dev, "chanspec",
				fw_chspec)) == BCME_BADCHAN) {
				if (bw == WL_CHANSPEC_BW_80)
					goto change_bw;
				err = wldev_ioctl_set(dev, WLC_SET_CHANNEL,
					&_chan, sizeof(_chan));
				if (err < 0) {
					WL_ERR(("WLC_SET_CHANNEL error %d"
					"chip may not be supporting this channel\n", err));
				}
			} else if (err) {
				WL_ERR(("failed to set chanspec error %d\n", err));
			}
		} else {
			WL_ERR(("failed to convert host chanspec to fw chanspec\n"));
			err = BCME_ERROR;
		}
	} else {
change_bw:
		if (bw == WL_CHANSPEC_BW_80)
			bw = WL_CHANSPEC_BW_40;
		else if (bw == WL_CHANSPEC_BW_40)
			bw = WL_CHANSPEC_BW_20;
		else
			bw = 0;
		if (bw)
			goto set_channel;
		WL_ERR(("Invalid chanspec 0x%x\n", chspec));
		err = BCME_ERROR;
	}
#ifdef CUSTOM_SET_CPUCORE
	if (dhd->op_mode == DHD_FLAG_HOSTAP_MODE) {
		WL_DBG(("SoftAP mode do not need to set cpucore\n"));
	} else if (chspec & WL_CHANSPEC_BW_80) {
		/* SoftAp only mode do not need to set cpucore */
		if ((dev->ieee80211_ptr->iftype == NL80211_IFTYPE_AP) &&
			dev != bcmcfg_to_prmry_ndev(cfg)) {
			/* Soft AP on virtual Iface (AP+STA case) */
			dhd->chan_isvht80 |= DHD_FLAG_HOSTAP_MODE;
			dhd_set_cpucore(dhd, TRUE);
		} else if (is_p2p_group_iface(dev->ieee80211_ptr)) {
			/* If P2P IF is vht80 */
			dhd->chan_isvht80 |= DHD_FLAG_P2P_MODE;
			dhd_set_cpucore(dhd, TRUE);
		}
	}
#endif /* CUSTOM_SET_CPUCORE */
	if (!err && (wl_get_mode_by_netdev(cfg, dev) == WL_MODE_AP)) {
		/* Update AP/GO operating channel */
		cfg->ap_oper_channel = ieee80211_frequency_to_channel(chan->center_freq);
	}
	return err;
}

#ifdef WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST
struct net_device *
wl_cfg80211_get_remain_on_channel_ndev(struct bcm_cfg80211 *cfg)
{
	struct net_info *_net_info, *next;
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	list_for_each_entry_safe(_net_info, next, &cfg->net_list, list) {
		if (_net_info->ndev &&
			test_bit(WL_STATUS_REMAINING_ON_CHANNEL, &_net_info->sme_state))
			return _net_info->ndev;
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
	return NULL;
}
#endif /* WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST */

static s32
wl_validate_opensecurity(struct net_device *dev, s32 bssidx, bool privacy)
{
	s32 err = BCME_OK;
	u32 wpa_val;
	s32 wsec = 0;

	/* set auth */
	err = wldev_iovar_setint_bsscfg(dev, "auth", 0, bssidx);
	if (err < 0) {
		WL_ERR(("auth error %d\n", err));
		return BCME_ERROR;
	}

	if (privacy) {
		/* If privacy bit is set in open mode, then WEP would be enabled */
		wsec = WEP_ENABLED;
		WL_DBG(("Setting wsec to %d for WEP \n", wsec));
	}

	/* set wsec */
	err = wldev_iovar_setint_bsscfg(dev, "wsec", wsec, bssidx);
	if (err < 0) {
		WL_ERR(("wsec error %d\n", err));
		return BCME_ERROR;
	}

	/* set upper-layer auth */
	if (dev->ieee80211_ptr->iftype == NL80211_IFTYPE_ADHOC)
		wpa_val = WPA_AUTH_NONE;
	else
		wpa_val = WPA_AUTH_DISABLED;
	err = wldev_iovar_setint_bsscfg(dev, "wpa_auth", wpa_val, bssidx);
	if (err < 0) {
		WL_ERR(("wpa_auth error %d\n", err));
		return BCME_ERROR;
	}

	return 0;
}

static s32
wl_validate_wpa2ie(struct net_device *dev, bcm_tlv_t *wpa2ie, s32 bssidx)
{
	s32 len = 0;
	s32 err = BCME_OK;
	u16 auth = 0; /* d11 open authentication */
	u32 wsec;
	u32 pval = 0;
	u32 gval = 0;
	u32 wpa_auth = 0;
	wpa_suite_mcast_t *mcast;
	wpa_suite_ucast_t *ucast;
	wpa_suite_auth_key_mgmt_t *mgmt;
	wpa_pmkid_list_t *pmkid;
	int cnt = 0;
#ifdef MFP
	int mfp = 0;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
#endif /* MFP */

	u16 suite_count;
	u8 rsn_cap[2];
	u32 wme_bss_disable;

	if (wpa2ie == NULL)
		goto exit;

	WL_DBG(("Enter \n"));
	len =  wpa2ie->len - WPA2_VERSION_LEN;
	/* check the mcast cipher */
	mcast = (wpa_suite_mcast_t *)&wpa2ie->data[WPA2_VERSION_LEN];
	switch (mcast->type) {
		case WPA_CIPHER_NONE:
			gval = 0;
			break;
		case WPA_CIPHER_WEP_40:
		case WPA_CIPHER_WEP_104:
			gval = WEP_ENABLED;
			break;
		case WPA_CIPHER_TKIP:
			gval = TKIP_ENABLED;
			break;
		case WPA_CIPHER_AES_CCM:
			gval = AES_ENABLED;
			break;
#ifdef BCMWAPI_WPI
		case WAPI_CIPHER_SMS4:
			gval = SMS4_ENABLED;
			break;
#endif
		default:
			WL_ERR(("No Security Info\n"));
			break;
	}
	if ((len -= WPA_SUITE_LEN) <= 0)
		return BCME_BADLEN;

	/* check the unicast cipher */
	ucast = (wpa_suite_ucast_t *)&mcast[1];
	suite_count = ltoh16_ua(&ucast->count);
	switch (ucast->list[0].type) {
		case WPA_CIPHER_NONE:
			pval = 0;
			break;
		case WPA_CIPHER_WEP_40:
		case WPA_CIPHER_WEP_104:
			pval = WEP_ENABLED;
			break;
		case WPA_CIPHER_TKIP:
			pval = TKIP_ENABLED;
			break;
		case WPA_CIPHER_AES_CCM:
			pval = AES_ENABLED;
			break;
#ifdef BCMWAPI_WPI
		case WAPI_CIPHER_SMS4:
			pval = SMS4_ENABLED;
			break;
#endif
		default:
			WL_ERR(("No Security Info\n"));
	}
	if ((len -= (WPA_IE_SUITE_COUNT_LEN + (WPA_SUITE_LEN * suite_count))) <= 0)
		return BCME_BADLEN;

	/* FOR WPS , set SEC_OW_ENABLED */
	wsec = (pval | gval | SES_OW_ENABLED);
	/* check the AKM */
	mgmt = (wpa_suite_auth_key_mgmt_t *)&ucast->list[suite_count];
	suite_count = cnt = ltoh16_ua(&mgmt->count);
	while (cnt--) {
		switch (mgmt->list[cnt].type) {
		case RSN_AKM_NONE:
			wpa_auth |= WPA_AUTH_NONE;
			break;
		case RSN_AKM_UNSPECIFIED:
			wpa_auth |= WPA2_AUTH_UNSPECIFIED;
			break;
		case RSN_AKM_PSK:
			wpa_auth |= WPA2_AUTH_PSK;
			break;
#ifdef MFP
		case RSN_AKM_MFP_PSK:
			wpa_auth |= WPA2_AUTH_PSK_SHA256;
			break;
		case RSN_AKM_MFP_1X:
			wpa_auth |= WPA2_AUTH_1X_SHA256;
			break;
#endif /* MFP */
#ifdef WL_SAE
		case RSN_AKM_SAE_PSK:
		case RSN_AKM_SAE_FBT:
			wpa_auth |= WPA3_AUTH_SAE_PSK;
			break;
#endif /* WL_SAE */
		default:
			WL_ERR(("No Key Mgmt Info\n"));
		}
	}

	if ((len -= (WPA_IE_SUITE_COUNT_LEN + (WPA_SUITE_LEN * suite_count))) >= RSN_CAP_LEN) {
		rsn_cap[0] = *(u8 *)&mgmt->list[suite_count];
		rsn_cap[1] = *((u8 *)&mgmt->list[suite_count] + 1);

		if (rsn_cap[0] & (RSN_CAP_16_REPLAY_CNTRS << RSN_CAP_PTK_REPLAY_CNTR_SHIFT)) {
			wme_bss_disable = 0;
		} else {
			wme_bss_disable = 1;
		}

#ifdef MFP
	if (rsn_cap[0] & RSN_CAP_MFPR) {
		WL_DBG(("MFP Required \n"));
		mfp = WL_MFP_REQUIRED;
		/* Our firmware has requirement that WPA2_AUTH_PSK/WPA2_AUTH_UNSPECIFIED
		 * be set, if SHA256 OUI is to be included in the rsn ie.
		 */
		if (wpa_auth & WPA2_AUTH_PSK_SHA256) {
			wpa_auth |= WPA2_AUTH_PSK;
		} else if (wpa_auth & WPA2_AUTH_1X_SHA256) {
			wpa_auth |= WPA2_AUTH_UNSPECIFIED;
		}
	} else if (rsn_cap[0] & RSN_CAP_MFPC) {
		WL_DBG(("MFP Capable \n"));
		mfp = WL_MFP_CAPABLE;
	}
#endif /* MFP */

		/* set wme_bss_disable to sync RSN Capabilities */
		err = wldev_iovar_setint_bsscfg(dev, "wme_bss_disable", wme_bss_disable, bssidx);
		if (err < 0) {
			WL_ERR(("wme_bss_disable error %d\n", err));
			return BCME_ERROR;
		}
	} else {
		WL_DBG(("There is no RSN Capabilities. remained len %d\n", len));
	}

	len -= RSN_CAP_LEN;
	if (len >= WPA2_PMKID_COUNT_LEN) {
		pmkid = (wpa_pmkid_list_t *)((u8 *)&mgmt->list[suite_count] + RSN_CAP_LEN);
		cnt = ltoh16_ua(&pmkid->count);
		if (cnt != 0) {
			WL_ERR(("AP has non-zero PMKID count. Wrong!\n"));
			return BCME_ERROR;
		}
		/* since PMKID cnt is known to be 0 for AP, */
		/* so don't bother to send down this info to firmware */
	}

#ifdef MFP
	len -= WPA2_PMKID_COUNT_LEN;
	if (len >= WPA_SUITE_LEN) {
		cfg->bip_pos = (u8 *)&mgmt->list[suite_count] + RSN_CAP_LEN + WPA2_PMKID_COUNT_LEN;
	} else {
		cfg->bip_pos = NULL;
	}
#endif

	/* set auth */
	err = wldev_iovar_setint_bsscfg(dev, "auth", auth, bssidx);
	if (err < 0) {
		WL_ERR(("auth error %d\n", err));
		return BCME_ERROR;
	}

	/* set wsec */
	err = wldev_iovar_setint_bsscfg(dev, "wsec", wsec, bssidx);
	if (err < 0) {
		WL_ERR(("wsec error %d\n", err));
		return BCME_ERROR;
	}

#ifdef MFP
	cfg->mfp_mode = mfp;
#endif /* MFP */

	/* set upper-layer auth */
	err = wldev_iovar_setint_bsscfg(dev, "wpa_auth", wpa_auth, bssidx);
	if (err < 0) {
		WL_ERR(("wpa_auth error %d\n", err));
		return BCME_ERROR;
	}
exit:
	return 0;
}

static s32
wl_validate_wpaie(struct net_device *dev, wpa_ie_fixed_t *wpaie, s32 bssidx)
{
	wpa_suite_mcast_t *mcast;
	wpa_suite_ucast_t *ucast;
	wpa_suite_auth_key_mgmt_t *mgmt;
	u16 auth = 0; /* d11 open authentication */
	u16 count;
	s32 err = BCME_OK;
	s32 len = 0;
	u32 i;
	u32 wsec;
	u32 pval = 0;
	u32 gval = 0;
	u32 wpa_auth = 0;
	u32 tmp = 0;

	if (wpaie == NULL)
		goto exit;
	WL_DBG(("Enter \n"));
	len = wpaie->length;    /* value length */
	len -= WPA_IE_TAG_FIXED_LEN;
	/* check for multicast cipher suite */
	if (len < WPA_SUITE_LEN) {
		WL_INFORM(("no multicast cipher suite\n"));
		goto exit;
	}

	/* pick up multicast cipher */
	mcast = (wpa_suite_mcast_t *)&wpaie[1];
	len -= WPA_SUITE_LEN;
	if (!bcmp(mcast->oui, WPA_OUI, WPA_OUI_LEN)) {
		if (IS_WPA_CIPHER(mcast->type)) {
			tmp = 0;
			switch (mcast->type) {
				case WPA_CIPHER_NONE:
					tmp = 0;
					break;
				case WPA_CIPHER_WEP_40:
				case WPA_CIPHER_WEP_104:
					tmp = WEP_ENABLED;
					break;
				case WPA_CIPHER_TKIP:
					tmp = TKIP_ENABLED;
					break;
				case WPA_CIPHER_AES_CCM:
					tmp = AES_ENABLED;
					break;
				default:
					WL_ERR(("No Security Info\n"));
			}
			gval |= tmp;
		}
	}
	/* Check for unicast suite(s) */
	if (len < WPA_IE_SUITE_COUNT_LEN) {
		WL_INFORM(("no unicast suite\n"));
		goto exit;
	}
	/* walk thru unicast cipher list and pick up what we recognize */
	ucast = (wpa_suite_ucast_t *)&mcast[1];
	count = ltoh16_ua(&ucast->count);
	len -= WPA_IE_SUITE_COUNT_LEN;
	for (i = 0; i < count && len >= WPA_SUITE_LEN;
		i++, len -= WPA_SUITE_LEN) {
		if (!bcmp(ucast->list[i].oui, WPA_OUI, WPA_OUI_LEN)) {
			if (IS_WPA_CIPHER(ucast->list[i].type)) {
				tmp = 0;
				switch (ucast->list[i].type) {
					case WPA_CIPHER_NONE:
						tmp = 0;
						break;
					case WPA_CIPHER_WEP_40:
					case WPA_CIPHER_WEP_104:
						tmp = WEP_ENABLED;
						break;
					case WPA_CIPHER_TKIP:
						tmp = TKIP_ENABLED;
						break;
					case WPA_CIPHER_AES_CCM:
						tmp = AES_ENABLED;
						break;
					default:
						WL_ERR(("No Security Info\n"));
				}
				pval |= tmp;
			}
		}
	}
	len -= (count - i) * WPA_SUITE_LEN;
	/* Check for auth key management suite(s) */
	if (len < WPA_IE_SUITE_COUNT_LEN) {
		WL_INFORM((" no auth key mgmt suite\n"));
		goto exit;
	}
	/* walk thru auth management suite list and pick up what we recognize */
	mgmt = (wpa_suite_auth_key_mgmt_t *)&ucast->list[count];
	count = ltoh16_ua(&mgmt->count);
	len -= WPA_IE_SUITE_COUNT_LEN;
	for (i = 0; i < count && len >= WPA_SUITE_LEN;
		i++, len -= WPA_SUITE_LEN) {
		if (!bcmp(mgmt->list[i].oui, WPA_OUI, WPA_OUI_LEN)) {
			if (IS_WPA_AKM(mgmt->list[i].type)) {
				tmp = 0;
				switch (mgmt->list[i].type) {
					case RSN_AKM_NONE:
						tmp = WPA_AUTH_NONE;
						break;
					case RSN_AKM_UNSPECIFIED:
						tmp = WPA_AUTH_UNSPECIFIED;
						break;
					case RSN_AKM_PSK:
						tmp = WPA_AUTH_PSK;
						break;
					default:
						WL_ERR(("No Key Mgmt Info\n"));
				}
				wpa_auth |= tmp;
			}
		}

	}
	/* FOR WPS , set SEC_OW_ENABLED */
	wsec = (pval | gval | SES_OW_ENABLED);
	/* set auth */
	err = wldev_iovar_setint_bsscfg(dev, "auth", auth, bssidx);
	if (err < 0) {
		WL_ERR(("auth error %d\n", err));
		return BCME_ERROR;
	}
	/* set wsec */
	err = wldev_iovar_setint_bsscfg(dev, "wsec", wsec, bssidx);
	if (err < 0) {
		WL_ERR(("wsec error %d\n", err));
		return BCME_ERROR;
	}
	/* set upper-layer auth */
	err = wldev_iovar_setint_bsscfg(dev, "wpa_auth", wpa_auth, bssidx);
	if (err < 0) {
		WL_ERR(("wpa_auth error %d\n", err));
		return BCME_ERROR;
	}
exit:
	return 0;
}

#if defined(SUPPORT_SOFTAP_WPAWPA2_MIXED)
static u32 wl_get_cipher_type(uint8 type)
{
	u32 ret = 0;
	switch (type) {
		case WPA_CIPHER_NONE:
			ret = 0;
			break;
		case WPA_CIPHER_WEP_40:
		case WPA_CIPHER_WEP_104:
			ret = WEP_ENABLED;
			break;
		case WPA_CIPHER_TKIP:
			ret = TKIP_ENABLED;
			break;
		case WPA_CIPHER_AES_CCM:
			ret = AES_ENABLED;
			break;
#ifdef BCMWAPI_WPI
		case WAPI_CIPHER_SMS4:
			ret = SMS4_ENABLED;
			break;
#endif
		default:
			WL_ERR(("No Security Info\n"));
	}
	return ret;
}

static u32 wl_get_suite_auth_key_mgmt_type(uint8 type)
{
	u32 ret = 0;
	switch (type) {
		case RSN_AKM_NONE:
			ret = WPA_AUTH_NONE;
			break;
		case RSN_AKM_UNSPECIFIED:
			ret = WPA_AUTH_UNSPECIFIED;
			break;
		case RSN_AKM_PSK:
			ret = WPA_AUTH_PSK;
			break;
		default:
			WL_ERR(("No Key Mgmt Info\n"));
	}
	return ret;
}

static u32 wl_get_suite_auth2_key_mgmt_type(uint8 type)
{
	u32 ret = 0;
	switch (type) {
		case RSN_AKM_NONE:
			ret = WPA_AUTH_NONE;
			break;
		case RSN_AKM_UNSPECIFIED:
			ret = WPA2_AUTH_UNSPECIFIED;
			break;
		case RSN_AKM_PSK:
			ret = WPA2_AUTH_PSK;
			break;
		default:
			WL_ERR(("No Key Mgmt Info\n"));
	}
	return ret;
}

static s32
wl_validate_wpaie_wpa2ie(struct net_device *dev, wpa_ie_fixed_t *wpaie,
	bcm_tlv_t *wpa2ie, s32 bssidx)
{
	wpa_suite_mcast_t *mcast;
	wpa_suite_ucast_t *ucast;
	wpa_suite_auth_key_mgmt_t *mgmt;
	u16 auth = 0; /* d11 open authentication */
	u16 count;
	s32 err = BCME_OK;
	u32 wme_bss_disable;
	u16 suite_count;
	u8 rsn_cap[2];
	s32 len = 0;
	u32 i;
	u32 wsec1, wsec2, wsec;
	u32 pval = 0;
	u32 gval = 0;
	u32 wpa_auth = 0;
	u32 wpa_auth1 = 0;
	u32 wpa_auth2 = 0;
	u8* ptmp;

	if (wpaie == NULL || wpa2ie == NULL)
		goto exit;

	WL_DBG(("Enter \n"));
	len = wpaie->length;    /* value length */
	len -= WPA_IE_TAG_FIXED_LEN;
	/* check for multicast cipher suite */
	if (len < WPA_SUITE_LEN) {
		WL_INFORM(("no multicast cipher suite\n"));
		goto exit;
	}

	/* pick up multicast cipher */
	mcast = (wpa_suite_mcast_t *)&wpaie[1];
	len -= WPA_SUITE_LEN;
	if (!bcmp(mcast->oui, WPA_OUI, WPA_OUI_LEN)) {
		if (IS_WPA_CIPHER(mcast->type)) {
			gval |= wl_get_cipher_type(mcast->type);
		}
	}
	WL_ERR(("\nwpa ie validate\n"));
	WL_ERR(("wpa ie mcast cipher = 0x%X\n", gval));

	/* Check for unicast suite(s) */
	if (len < WPA_IE_SUITE_COUNT_LEN) {
		WL_INFORM(("no unicast suite\n"));
		goto exit;
	}

	/* walk thru unicast cipher list and pick up what we recognize */
	ucast = (wpa_suite_ucast_t *)&mcast[1];
	count = ltoh16_ua(&ucast->count);
	len -= WPA_IE_SUITE_COUNT_LEN;
	for (i = 0; i < count && len >= WPA_SUITE_LEN;
		i++, len -= WPA_SUITE_LEN) {
		if (!bcmp(ucast->list[i].oui, WPA_OUI, WPA_OUI_LEN)) {
			if (IS_WPA_CIPHER(ucast->list[i].type)) {
				pval |= wl_get_cipher_type(ucast->list[i].type);
			}
		}
	}
	WL_ERR(("wpa ie ucast count =%d, cipher = 0x%X\n", count, pval));

	/* FOR WPS , set SEC_OW_ENABLED */
	wsec1 = (pval | gval | SES_OW_ENABLED);
	WL_ERR(("wpa ie wsec = 0x%X\n", wsec1));

	len -= (count - i) * WPA_SUITE_LEN;
	/* Check for auth key management suite(s) */
	if (len < WPA_IE_SUITE_COUNT_LEN) {
		WL_INFORM((" no auth key mgmt suite\n"));
		goto exit;
	}
	/* walk thru auth management suite list and pick up what we recognize */
	mgmt = (wpa_suite_auth_key_mgmt_t *)&ucast->list[count];
	count = ltoh16_ua(&mgmt->count);
	len -= WPA_IE_SUITE_COUNT_LEN;
	for (i = 0; i < count && len >= WPA_SUITE_LEN;
		i++, len -= WPA_SUITE_LEN) {
		if (!bcmp(mgmt->list[i].oui, WPA_OUI, WPA_OUI_LEN)) {
			if (IS_WPA_AKM(mgmt->list[i].type)) {

				wpa_auth1 |= wl_get_suite_auth_key_mgmt_type(mgmt->list[i].type);
			}
		}

	}
	WL_ERR(("wpa ie wpa_suite_auth_key_mgmt count=%d, key_mgmt = 0x%X\n", count, wpa_auth1));
	WL_ERR(("\nwpa2 ie validate\n"));

	pval = 0;
	gval = 0;
	len =  wpa2ie->len;
	/* check the mcast cipher */
	mcast = (wpa_suite_mcast_t *)&wpa2ie->data[WPA2_VERSION_LEN];
	ptmp = mcast->oui;
	gval = wl_get_cipher_type(ptmp[DOT11_OUI_LEN]);

	WL_ERR(("wpa2 ie mcast cipher = 0x%X\n", gval));
	if ((len -= WPA_SUITE_LEN) <= 0)
	{
		WL_ERR(("P:wpa2 ie len[%d]", len));
		return BCME_BADLEN;
	}

	/* check the unicast cipher */
	ucast = (wpa_suite_ucast_t *)&mcast[1];
	suite_count = ltoh16_ua(&ucast->count);
	WL_ERR((" WPA2 ucast cipher count=%d\n", suite_count));
	pval |= wl_get_cipher_type(ucast->list[0].type);

	if ((len -= (WPA_IE_SUITE_COUNT_LEN + (WPA_SUITE_LEN * suite_count))) <= 0)
		return BCME_BADLEN;

	WL_ERR(("wpa2 ie ucast cipher = 0x%X\n", pval));

	/* FOR WPS , set SEC_OW_ENABLED */
	wsec2 = (pval | gval | SES_OW_ENABLED);
	WL_ERR(("wpa2 ie wsec = 0x%X\n", wsec2));

	/* check the AKM */
	mgmt = (wpa_suite_auth_key_mgmt_t *)&ucast->list[suite_count];
	suite_count = ltoh16_ua(&mgmt->count);
	ptmp = (u8 *)&mgmt->list[0];
	wpa_auth2 = wl_get_suite_auth2_key_mgmt_type(ptmp[DOT11_OUI_LEN]);
	WL_ERR(("wpa ie wpa_suite_auth_key_mgmt count=%d, key_mgmt = 0x%X\n", count, wpa_auth2));

	if ((len -= (WPA_IE_SUITE_COUNT_LEN + (WPA_SUITE_LEN * suite_count))) >= RSN_CAP_LEN) {
		rsn_cap[0] = *(u8 *)&mgmt->list[suite_count];
		rsn_cap[1] = *((u8 *)&mgmt->list[suite_count] + 1);
		if (rsn_cap[0] & (RSN_CAP_16_REPLAY_CNTRS << RSN_CAP_PTK_REPLAY_CNTR_SHIFT)) {
			wme_bss_disable = 0;
		} else {
			wme_bss_disable = 1;
		}
		WL_DBG(("P:rsn_cap[0]=[0x%X]:wme_bss_disabled[%d]\n", rsn_cap[0], wme_bss_disable));

		/* set wme_bss_disable to sync RSN Capabilities */
		err = wldev_iovar_setint_bsscfg(dev, "wme_bss_disable", wme_bss_disable, bssidx);
		if (err < 0) {
			WL_ERR(("wme_bss_disable error %d\n", err));
			return BCME_ERROR;
		}
	} else {
		WL_DBG(("There is no RSN Capabilities. remained len %d\n", len));
	}

	wsec = (wsec1 | wsec2);
	wpa_auth = (wpa_auth1 | wpa_auth2);
	WL_ERR(("wpa_wpa2 wsec=0x%X wpa_auth=0x%X\n", wsec, wpa_auth));

	/* set auth */
	err = wldev_iovar_setint_bsscfg(dev, "auth", auth, bssidx);
	if (err < 0) {
		WL_ERR(("auth error %d\n", err));
		return BCME_ERROR;
	}
	/* set wsec */
	err = wldev_iovar_setint_bsscfg(dev, "wsec", wsec, bssidx);
	if (err < 0) {
		WL_ERR(("wsec error %d\n", err));
		return BCME_ERROR;
	}
	/* set upper-layer auth */
	err = wldev_iovar_setint_bsscfg(dev, "wpa_auth", wpa_auth, bssidx);
	if (err < 0) {
		WL_ERR(("wpa_auth error %d\n", err));
		return BCME_ERROR;
	}
exit:
	return 0;
}
#endif /* SUPPORT_SOFTAP_WPAWPA2_MIXED */

static s32
wl_cfg80211_bcn_validate_sec(
	struct net_device *dev,
	struct parsed_ies *ies,
	u32 dev_role,
	s32 bssidx,
	bool privacy)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	wl_cfgbss_t *bss = wl_get_cfgbss_by_wdev(cfg, dev->ieee80211_ptr);

	if (!bss) {
		WL_ERR(("cfgbss is NULL \n"));
		return BCME_ERROR;
	}

	if (dev_role == NL80211_IFTYPE_P2P_GO && (ies->wpa2_ie)) {
		/* For P2P GO, the sec type is WPA2-PSK */
		WL_DBG(("P2P GO: validating wpa2_ie"));
		if (wl_validate_wpa2ie(dev, ies->wpa2_ie, bssidx)  < 0)
			return BCME_ERROR;

	} else if (dev_role == NL80211_IFTYPE_AP) {

		WL_DBG(("SoftAP: validating security"));
		/* If wpa2_ie or wpa_ie is present validate it */

#if defined(SUPPORT_SOFTAP_WPAWPA2_MIXED)
		if ((ies->wpa_ie != NULL && ies->wpa2_ie != NULL)) {
			if (wl_validate_wpaie_wpa2ie(dev, ies->wpa_ie, ies->wpa2_ie, bssidx)  < 0) {
				bss->security_mode = false;
				return BCME_ERROR;
			}
		}
		else {
#endif /* SUPPORT_SOFTAP_WPAWPA2_MIXED */
		if ((ies->wpa2_ie || ies->wpa_ie) &&
			((wl_validate_wpa2ie(dev, ies->wpa2_ie, bssidx)  < 0 ||
			wl_validate_wpaie(dev, ies->wpa_ie, bssidx) < 0))) {
			bss->security_mode = false;
			return BCME_ERROR;
		}

		bss->security_mode = true;
		if (bss->rsn_ie) {
			kfree(bss->rsn_ie);
			bss->rsn_ie = NULL;
		}
		if (bss->wpa_ie) {
			kfree(bss->wpa_ie);
			bss->wpa_ie = NULL;
		}
		if (bss->wps_ie) {
			kfree(bss->wps_ie);
			bss->wps_ie = NULL;
		}
		if (ies->wpa_ie != NULL) {
			/* WPAIE */
			bss->rsn_ie = NULL;
			bss->wpa_ie = kmemdup(ies->wpa_ie,
				ies->wpa_ie->length + WPA_RSN_IE_TAG_FIXED_LEN,
				GFP_KERNEL);
		} else if (ies->wpa2_ie != NULL) {
			/* RSNIE */
			bss->wpa_ie = NULL;
			bss->rsn_ie = kmemdup(ies->wpa2_ie,
				ies->wpa2_ie->len + WPA_RSN_IE_TAG_FIXED_LEN,
				GFP_KERNEL);
		}
#if defined(SUPPORT_SOFTAP_WPAWPA2_MIXED)
		}
#endif /* SUPPORT_SOFTAP_WPAWPA2_MIXED */
		if (!ies->wpa2_ie && !ies->wpa_ie) {
			wl_validate_opensecurity(dev, bssidx, privacy);
			bss->security_mode = false;
		}

		if (ies->wps_ie) {
			bss->wps_ie = kmemdup(ies->wps_ie, ies->wps_ie_len, GFP_KERNEL);
		}
	}

	return 0;

}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0))
static s32 wl_cfg80211_bcn_set_params(
	struct cfg80211_ap_settings *info,
	struct net_device *dev,
	u32 dev_role, s32 bssidx)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	s32 err = BCME_OK;

	WL_DBG(("interval (%d) \ndtim_period (%d) \n",
		info->beacon_interval, info->dtim_period));

	if (info->beacon_interval) {
		if ((err = wldev_ioctl_set(dev, WLC_SET_BCNPRD,
			&info->beacon_interval, sizeof(s32))) < 0) {
			WL_ERR(("Beacon Interval Set Error, %d\n", err));
			return err;
		}
	}

	if (info->dtim_period) {
		if ((err = wldev_ioctl_set(dev, WLC_SET_DTIMPRD,
			&info->dtim_period, sizeof(s32))) < 0) {
			WL_ERR(("DTIM Interval Set Error, %d\n", err));
			return err;
		}
	}

	if ((info->ssid) && (info->ssid_len > 0) &&
		(info->ssid_len <= DOT11_MAX_SSID_LEN)) {
		WL_DBG(("SSID (%s) len:%zd \n", info->ssid, info->ssid_len));
		if (dev_role == NL80211_IFTYPE_AP) {
			/* Store the hostapd SSID */
			memset(cfg->hostapd_ssid.SSID, 0x00, DOT11_MAX_SSID_LEN);
			memcpy(cfg->hostapd_ssid.SSID, info->ssid, info->ssid_len);
			cfg->hostapd_ssid.SSID_len = info->ssid_len;
		} else {
				/* P2P GO */
			memset(cfg->p2p->ssid.SSID, 0x00, DOT11_MAX_SSID_LEN);
			memcpy(cfg->p2p->ssid.SSID, info->ssid, info->ssid_len);
			cfg->p2p->ssid.SSID_len = info->ssid_len;
		}
	}

	if (info->hidden_ssid != NL80211_HIDDEN_SSID_NOT_IN_USE) {
		if ((err = wldev_iovar_setint(dev, "closednet", 1)) < 0)
			WL_ERR(("failed to set hidden : %d\n", err));
		WL_DBG(("hidden_ssid_enum_val: %d \n", info->hidden_ssid));
	}

	return err;
}
#endif 

static s32
wl_cfg80211_parse_ies(u8 *ptr, u32 len, struct parsed_ies *ies)
{
	s32 err = BCME_OK;

	memset(ies, 0, sizeof(struct parsed_ies));

	/* find the WPSIE */
	if ((ies->wps_ie = wl_cfgp2p_find_wpsie(ptr, len)) != NULL) {
		WL_DBG(("WPSIE in beacon \n"));
		ies->wps_ie_len = ies->wps_ie->length + WPA_RSN_IE_TAG_FIXED_LEN;
	} else {
		WL_DBG(("No WPSIE in beacon \n"));
	}

	/* find the RSN_IE */
	if ((ies->wpa2_ie = bcm_parse_tlvs(ptr, len,
		DOT11_MNG_RSN_ID)) != NULL) {
		WL_DBG((" WPA2 IE found\n"));
		ies->wpa2_ie_len = ies->wpa2_ie->len;
	}

	/* find the WPA_IE */
	if ((ies->wpa_ie = wl_cfgp2p_find_wpaie(ptr, len)) != NULL) {
		WL_DBG((" WPA found\n"));
		ies->wpa_ie_len = ies->wpa_ie->length;
	}

	return err;

}
static s32
wl_cfg80211_set_ap_role(
	struct bcm_cfg80211 *cfg,
	struct net_device *dev)
{
	s32 err = BCME_OK;
	s32 infra = 1;
	s32 ap = 1;
	s32 pm;
	s32 is_rsdb_supported = BCME_ERROR;
	s32 bssidx;
	s32 apsta = 0;

	is_rsdb_supported = DHD_OPMODE_SUPPORTED(cfg->pub, DHD_FLAG_RSDB_MODE);
	if (is_rsdb_supported < 0)
		return (-ENODEV);

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find p2p index from wdev(%p) failed\n", dev->ieee80211_ptr));
		return -EINVAL;
	}

	/* AP on primary Interface */
	if (bssidx == 0) {
		if (is_rsdb_supported) {
			if ((err = wl_cfg80211_add_del_bss(cfg, dev, bssidx,
				NL80211_IFTYPE_AP, 0, NULL)) < 0) {
				WL_ERR(("wl add_del_bss returned error:%d\n", err));
				return err;
			}
		} else if (is_rsdb_supported == 0) {
			/* AP mode switch not supported. Try setting up AP explicitly */
			err = wldev_iovar_getint(dev, "apsta", (s32 *)&apsta);
			if (unlikely(err)) {
				WL_ERR(("Could not get apsta %d\n", err));
			}
			if (1) { // terence: fix me
				/* If apsta is not set, set it */
				err = wldev_ioctl_set(dev, WLC_DOWN, &ap, sizeof(s32));
				if (err < 0) {
					WL_ERR(("WLC_DOWN error %d\n", err));
					return err;
				}
				err = wldev_iovar_setint(dev, "apsta", 0);
				if (err < 0) {
					WL_ERR(("wl apsta 0 error %d\n", err));
					return err;
				}
				if ((err = wldev_ioctl_set(dev,
					WLC_SET_AP, &ap, sizeof(s32))) < 0) {
					WL_ERR(("setting AP mode failed %d \n", err));
					return err;
				}
			}
		}

		pm = 0;
		if ((err = wldev_ioctl_set(dev, WLC_SET_PM, &pm, sizeof(pm))) != 0) {
			WL_ERR(("wl PM 0 returned error:%d\n", err));
			/* Ignore error, if any */
			err = BCME_OK;
		}
		err = wldev_ioctl_set(dev, WLC_SET_INFRA, &infra, sizeof(s32));
		if (err < 0) {
			WL_ERR(("SET INFRA error %d\n", err));
			return err;
		}
	} else {
		WL_DBG(("Bringup SoftAP on virtual Interface bssidx:%d \n", bssidx));
		if ((err = wl_cfg80211_add_del_bss(cfg, dev,
			bssidx, NL80211_IFTYPE_AP, 0, NULL)) < 0) {
			WL_ERR(("wl bss ap returned error:%d\n", err));
			return err;
		}
	}

	/* On success, mark AP creation in progress. */
	wl_set_drv_status(cfg, AP_CREATING, dev);
	return 0;
}


/* In RSDB downgrade cases, the link up event can get delayed upto 7-8 secs */
#define MAX_AP_LINK_WAIT_TIME   10000
static s32
wl_cfg80211_bcn_bringup_ap(
	struct net_device *dev,
	struct parsed_ies *ies,
	u32 dev_role, s32 bssidx)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	struct wl_join_params join_params;
	bool is_bssup = false;
	s32 infra = 1;
	s32 join_params_size = 0;
	s32 ap = 1;
	s32 wsec;
#ifdef WLMESH_CFG80211
	bool retried = false;
#endif
#ifdef SOFTAP_UAPSD_OFF
	uint32 wme_apsd = 0;
#endif /* SOFTAP_UAPSD_OFF */
	s32 err = BCME_OK;
	s32 is_rsdb_supported = BCME_ERROR;
	u32 timeout;
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
	char sec[32];

	is_rsdb_supported = DHD_OPMODE_SUPPORTED(cfg->pub, DHD_FLAG_RSDB_MODE);
	if (is_rsdb_supported < 0)
		return (-ENODEV);

	WL_DBG(("Enter dev_role:%d bssidx:%d ifname:%s\n", dev_role, bssidx, dev->name));

	/* Common code for SoftAP and P2P GO */
	wl_clr_drv_status(cfg, AP_CREATED, dev);

	/* Make sure INFRA is set for AP/GO */
	err = wldev_ioctl_set(dev, WLC_SET_INFRA, &infra, sizeof(s32));
	if (err < 0) {
		WL_ERR(("SET INFRA error %d\n", err));
		goto exit;
	}

	/* Do abort scan before creating GO */
	wl_cfg80211_scan_abort(cfg);

	wl_ext_get_sec(dev, 0, sec, sizeof(sec));
	WL_MSG(dev->name, "Creating AP/GO with sec=%s\n", sec);
	if (dev_role == NL80211_IFTYPE_P2P_GO) {
		is_bssup = wl_cfg80211_bss_isup(dev, bssidx);
		if (!is_bssup && (ies->wpa2_ie != NULL)) {

			err = wldev_iovar_setbuf_bsscfg(dev, "ssid", &cfg->p2p->ssid,
				sizeof(cfg->p2p->ssid), cfg->ioctl_buf, WLC_IOCTL_MAXLEN,
				bssidx, &cfg->ioctl_buf_sync);
			if (err < 0) {
				WL_ERR(("GO SSID setting error %d\n", err));
				goto exit;
			}

			if ((err = wl_cfg80211_bss_up(cfg, dev, bssidx, 1)) < 0) {
				WL_ERR(("GO Bring up error %d\n", err));
				goto exit;
			}
		} else
			WL_DBG(("Bss is already up\n"));
	} else if (dev_role == NL80211_IFTYPE_AP) {

//		if (!wl_get_drv_status(cfg, AP_CREATING, dev)) {
			/* Make sure fw is in proper state */
			err = wl_cfg80211_set_ap_role(cfg, dev);
			if (unlikely(err)) {
				WL_ERR(("set ap role failed!\n"));
				goto exit;
			}
//		}

		/* Device role SoftAP */
		WL_DBG(("Creating AP bssidx:%d dev_role:%d\n", bssidx, dev_role));
		/* Clear the status bit after use */
		wl_clr_drv_status(cfg, AP_CREATING, dev);


#ifdef SOFTAP_UAPSD_OFF
		err = wldev_iovar_setbuf_bsscfg(dev, "wme_apsd", &wme_apsd, sizeof(wme_apsd),
			cfg->ioctl_buf, WLC_IOCTL_SMLEN, bssidx, &cfg->ioctl_buf_sync);
		if (err < 0) {
			WL_ERR(("failed to disable uapsd, error=%d\n", err));
		}
#endif /* SOFTAP_UAPSD_OFF */

		err = wldev_ioctl_set(dev, WLC_UP, &ap, sizeof(s32));
		if (unlikely(err)) {
			WL_ERR(("WLC_UP error (%d)\n", err));
			goto exit;
		}

#ifdef MFP
		if (cfg->bip_pos) {
			err = wldev_iovar_setbuf_bsscfg(dev, "bip",
				(void *)(cfg->bip_pos), WPA_SUITE_LEN, cfg->ioctl_buf,
				WLC_IOCTL_SMLEN, bssidx, &cfg->ioctl_buf_sync);
			if (err < 0) {
				WL_ERR(("bip set error %d\n", err));
				if (wl_customer6_legacy_chip_check(cfg,
					bcmcfg_to_prmry_ndev(cfg))) {
					/* Ignore bip error: Some older firmwares doesn't
					 * support bip iovar/ return BCME_NOTUP while trying
					 * to set bip from AP bring up context. These firmares
					 * include bip in RSNIE by default. So its okay to ignore
					 * the error.
					 */
					err = BCME_OK;
				} else {
					goto exit;
				}
			}
		}
#endif /* MFP */

		err = wldev_iovar_getint(dev, "wsec", (s32 *)&wsec);
		if (unlikely(err)) {
			WL_ERR(("Could not get wsec %d\n", err));
			goto exit;
		}
		if (dhdp->conf->chip == BCM43430_CHIP_ID && bssidx > 0 && wsec >= 2) {
			wsec |= 0x8; // terence 20180628: fix me, this is a workaround
			err = wldev_iovar_setint_bsscfg(dev, "wsec", wsec, bssidx);
			if (err < 0) {
				WL_ERR(("wsec error %d\n", err));
				goto exit;
			}
		}
		if ((wsec == WEP_ENABLED) && cfg->wep_key.len) {
			WL_DBG(("Applying buffered WEP KEY \n"));
			err = wldev_iovar_setbuf_bsscfg(dev, "wsec_key", &cfg->wep_key,
				sizeof(struct wl_wsec_key), cfg->ioctl_buf,
				WLC_IOCTL_MAXLEN, bssidx, &cfg->ioctl_buf_sync);
			/* clear the key after use */
			memset(&cfg->wep_key, 0, sizeof(struct wl_wsec_key));
			if (unlikely(err)) {
				WL_ERR(("WLC_SET_KEY error (%d)\n", err));
				goto exit;
			}
		}

#ifdef MFP
		if (cfg->mfp_mode) {
			/* This needs to go after wsec otherwise the wsec command will
			 * overwrite the values set by MFP
			 */
			err = wldev_iovar_setint_bsscfg(dev, "mfp", cfg->mfp_mode, bssidx);
			if (err < 0) {
				WL_ERR(("MFP Setting failed. ret = %d \n", err));
				/* If fw doesn't support mfp, Ignore the error */
				if (err != BCME_UNSUPPORTED) {
					goto exit;
				}
			}
		}
#endif /* MFP */

#ifdef WLMESH_CFG80211
ssid_retry:
#endif
		memset(&join_params, 0, sizeof(join_params));
		/* join parameters starts with ssid */
		join_params_size = sizeof(join_params.ssid);
		join_params.ssid.SSID_len = MIN(cfg->hostapd_ssid.SSID_len,
			(uint32)DOT11_MAX_SSID_LEN);
		memcpy(join_params.ssid.SSID, cfg->hostapd_ssid.SSID,
			join_params.ssid.SSID_len);
		join_params.ssid.SSID_len = htod32(join_params.ssid.SSID_len);

		/* create softap */
		if ((err = wldev_ioctl_set(dev, WLC_SET_SSID, &join_params,
			join_params_size)) != 0) {
			WL_ERR(("SoftAP/GO set ssid failed! %d\n", err));
			goto exit;
		} else {
			WL_DBG((" SoftAP SSID \"%s\" \n", join_params.ssid.SSID));
		}

		if (bssidx != 0) {
			/* AP on Virtual Interface */
			if ((err = wl_cfg80211_bss_up(cfg, dev, bssidx, 1)) < 0) {
				WL_ERR(("AP Bring up error %d\n", err));
				goto exit;
			}
		}

	}

	/* Wait for Linkup event to mark successful AP/GO bring up */
	timeout = wait_event_interruptible_timeout(cfg->netif_change_event,
		wl_get_drv_status(cfg, AP_CREATED, dev), msecs_to_jiffies(MAX_AP_LINK_WAIT_TIME));
	if (timeout <= 0 || !wl_get_drv_status(cfg, AP_CREATED, dev)) {
#ifdef WLMESH_CFG80211
		if (!retried) {
			retried = true;
			WL_ERR(("Link up didn't come for AP interface. Try to set ssid again to recover it! \n"));
			goto ssid_retry;
		}
#endif
		WL_ERR(("Link up didn't come for AP interface. AP/GO creation failed! \n"));
		if (timeout == -ERESTARTSYS) {
			WL_ERR(("waitqueue was interrupted by a signal, returns -ERESTARTSYS\n"));
			err = -ERESTARTSYS;
			goto exit;
		}
#if defined(DHD_DEBUG) && defined(DHD_FW_COREDUMP)
		if (dhdp->memdump_enabled) {
			dhdp->memdump_type = DUMP_TYPE_AP_LINKUP_FAILURE;
			dhd_bus_mem_dump(dhdp);
		}
#endif /* DHD_DEBUG && DHD_FW_COREDUMP */
		err = -ENODEV;
		goto exit;
	}

exit:
	if (cfg->wep_key.len) {
		memset(&cfg->wep_key, 0, sizeof(struct wl_wsec_key));
	}

#ifdef MFP
	if (cfg->mfp_mode) {
		cfg->mfp_mode = 0;
	}

	if (cfg->bip_pos) {
		cfg->bip_pos = NULL;
	}
#endif /* MFP */

	return err;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0))
s32
wl_cfg80211_parse_ap_ies(
	struct net_device *dev,
	struct cfg80211_beacon_data *info,
	struct parsed_ies *ies)
{
	struct parsed_ies prb_ies;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);
	const u8 *vndr = NULL;
	u32 vndr_ie_len = 0;
	s32 err = BCME_OK;

	/* Parse Beacon IEs */
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	if (wl_cfg80211_parse_ies((u8 *)info->tail,
		info->tail_len, ies) < 0) {
		WL_ERR(("Beacon get IEs failed \n"));
		err = -EINVAL;
		goto fail;
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
	vndr = (const u8 *)info->proberesp_ies;
	vndr_ie_len = info->proberesp_ies_len;

	if (dhd->op_mode & DHD_FLAG_HOSTAP_MODE) {
		/* SoftAP mode */
		const struct ieee80211_mgmt *mgmt;
		mgmt = (const struct ieee80211_mgmt *)info->probe_resp;
		if (mgmt != NULL) {
			vndr = (const u8 *)&mgmt->u.probe_resp.variable;
			vndr_ie_len = info->probe_resp_len -
				offsetof(const struct ieee80211_mgmt, u.probe_resp.variable);
		}
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	/* Parse Probe Response IEs */
	if (wl_cfg80211_parse_ies((u8 *)vndr, vndr_ie_len, &prb_ies) < 0) {
		WL_ERR(("PROBE RESP get IEs failed \n"));
		err = -EINVAL;
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
fail:

	return err;
}

s32
wl_cfg80211_set_ies(
	struct net_device *dev,
	struct cfg80211_beacon_data *info,
	s32 bssidx)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);
	const u8 *vndr = NULL;
	u32 vndr_ie_len = 0;
	s32 err = BCME_OK;

	/* Set Beacon IEs to FW */
	if ((err = wl_cfg80211_set_mgmt_vndr_ies(cfg, ndev_to_cfgdev(dev), bssidx,
		VNDR_IE_BEACON_FLAG, (const u8 *)info->tail,
		info->tail_len)) < 0) {
		WL_ERR(("Set Beacon IE Failed \n"));
	} else {
		WL_DBG(("Applied Vndr IEs for Beacon \n"));
	}

	vndr = (const u8 *)info->proberesp_ies;
	vndr_ie_len = info->proberesp_ies_len;

	if (dhd->op_mode & DHD_FLAG_HOSTAP_MODE) {
		/* SoftAP mode */
		const struct ieee80211_mgmt *mgmt;
		mgmt = (const struct ieee80211_mgmt *)info->probe_resp;
		if (mgmt != NULL) {
			vndr = (const u8 *)&mgmt->u.probe_resp.variable;
			vndr_ie_len = info->probe_resp_len -
				offsetof(struct ieee80211_mgmt, u.probe_resp.variable);
		}
	}

	/* Set Probe Response IEs to FW */
	if ((err = wl_cfg80211_set_mgmt_vndr_ies(cfg, ndev_to_cfgdev(dev), bssidx,
		VNDR_IE_PRBRSP_FLAG, vndr, vndr_ie_len)) < 0) {
		WL_ERR(("Set Probe Resp IE Failed \n"));
	} else {
		WL_DBG(("Applied Vndr IEs for Probe Resp \n"));
	}

	return err;
}
#endif 

static s32 wl_cfg80211_hostapd_sec(
	struct net_device *dev,
	struct parsed_ies *ies,
	s32 bssidx)
{
	bool update_bss = 0;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	wl_cfgbss_t *bss = wl_get_cfgbss_by_wdev(cfg, dev->ieee80211_ptr);

	if (!bss) {
		WL_ERR(("cfgbss is NULL \n"));
		return -EINVAL;
	}

	if (ies->wps_ie) {
		if (bss->wps_ie &&
			memcmp(bss->wps_ie, ies->wps_ie, ies->wps_ie_len)) {
			WL_DBG((" WPS IE is changed\n"));
			kfree(bss->wps_ie);
			bss->wps_ie = kmemdup(ies->wps_ie, ies->wps_ie_len, GFP_KERNEL);
		} else if (bss->wps_ie == NULL) {
			WL_DBG((" WPS IE is added\n"));
			bss->wps_ie = kmemdup(ies->wps_ie, ies->wps_ie_len, GFP_KERNEL);
		}

		if ((ies->wpa_ie != NULL || ies->wpa2_ie != NULL)) {
			if (!bss->security_mode) {
				/* change from open mode to security mode */
				update_bss = true;
				if (ies->wpa_ie != NULL) {
					bss->wpa_ie = kmemdup(ies->wpa_ie,
					ies->wpa_ie->length + WPA_RSN_IE_TAG_FIXED_LEN,
					GFP_KERNEL);
				} else {
					bss->rsn_ie = kmemdup(ies->wpa2_ie,
					ies->wpa2_ie->len + WPA_RSN_IE_TAG_FIXED_LEN,
					GFP_KERNEL);
				}
			} else if (bss->wpa_ie) {
				/* change from WPA2 mode to WPA mode */
				if (ies->wpa_ie != NULL) {
					update_bss = true;
					kfree(bss->rsn_ie);
					bss->rsn_ie = NULL;
					bss->wpa_ie = kmemdup(ies->wpa_ie,
					ies->wpa_ie->length + WPA_RSN_IE_TAG_FIXED_LEN,
					GFP_KERNEL);
				} else if (memcmp(bss->rsn_ie,
					ies->wpa2_ie, ies->wpa2_ie->len
					+ WPA_RSN_IE_TAG_FIXED_LEN)) {
					update_bss = true;
					kfree(bss->rsn_ie);
					bss->rsn_ie = kmemdup(ies->wpa2_ie,
					ies->wpa2_ie->len + WPA_RSN_IE_TAG_FIXED_LEN,
					GFP_KERNEL);
					bss->wpa_ie = NULL;
				}
			}
			if (update_bss) {
				bss->security_mode = true;
				wl_cfg80211_bss_up(cfg, dev, bssidx, 0);
				if (wl_validate_wpa2ie(dev, ies->wpa2_ie, bssidx)  < 0 ||
					wl_validate_wpaie(dev, ies->wpa_ie, bssidx) < 0) {
					return BCME_ERROR;
				}
				wl_cfg80211_bss_up(cfg, dev, bssidx, 1);
			}
		}
	} else {
		WL_ERR(("No WPSIE in beacon \n"));
	}
	return 0;
}

#if defined(WL_SUPPORT_BACKPORTED_KPATCHES) || (LINUX_VERSION_CODE >= KERNEL_VERSION(3, \
	2, 0))
static s32
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0))
wl_cfg80211_del_station(
		struct wiphy *wiphy, struct net_device *ndev,
		struct station_del_parameters *params)
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
wl_cfg80211_del_station(
	struct wiphy *wiphy,
	struct net_device *ndev,
	const u8* mac_addr)
#else
wl_cfg80211_del_station(
	struct wiphy *wiphy,
	struct net_device *ndev,
	u8* mac_addr)
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)) */
{
	struct net_device *dev;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	scb_val_t scb_val;
	s8 eabuf[ETHER_ADDR_STR_LEN];
	int err;
	char mac_buf[MAX_NUM_OF_ASSOCIATED_DEV *
		sizeof(struct ether_addr) + sizeof(uint)] = {0};
	struct maclist *assoc_maclist = (struct maclist *)mac_buf;
	int num_associated = 0;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0))
	const u8 *mac_addr = params->mac;
#ifdef CUSTOM_BLOCK_DEAUTH_AT_EAP_FAILURE
	u16 rc = params->reason_code;
#endif /* CUSTOM_BLOCK_DEAUTH_AT_EAP_FAILURE */
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)) */

	WL_DBG(("Entry\n"));
	if (mac_addr == NULL) {
		WL_DBG(("mac_addr is NULL ignore it\n"));
		return 0;
	}

	dev = ndev_to_wlc_ndev(ndev, cfg);

	if (p2p_is_on(cfg)) {
		/* Suspend P2P discovery search-listen to prevent it from changing the
		 * channel.
		 */
		if ((wl_cfgp2p_discover_enable_search(cfg, false)) < 0) {
			WL_ERR(("Can not disable discovery mode\n"));
			return -EFAULT;
		}
	}
	err = wl_cfg80211_check_in4way(cfg, ndev, DONT_DELETE_GC_AFTER_WPS,
		WL_EXT_STATUS_DELETE_STA, (void *)mac_addr);
	if (err) {
		return 0;
	}

	assoc_maclist->count = MAX_NUM_OF_ASSOCIATED_DEV;
	err = wldev_ioctl_get(ndev, WLC_GET_ASSOCLIST,
		assoc_maclist, sizeof(mac_buf));
	if (err < 0)
		WL_ERR(("WLC_GET_ASSOCLIST error %d\n", err));
	else
		num_associated = assoc_maclist->count;

	memcpy(scb_val.ea.octet, mac_addr, ETHER_ADDR_LEN);
#ifdef CUSTOM_BLOCK_DEAUTH_AT_EAP_FAILURE
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0))
	if (rc == DOT11_RC_8021X_AUTH_FAIL) {
		WL_ERR(("deauth will be sent at F/W\n"));
		scb_val.val = DOT11_RC_8021X_AUTH_FAIL;
	} else {
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)) */
#endif /* CUSTOM_BLOCK_DEAUTH_AT_EAP_FAILURE */
	scb_val.val = DOT11_RC_DEAUTH_LEAVING;
	WL_MSG(dev->name, "Disconnect STA %s scb_val.val %d\n",
		bcm_ether_ntoa((const struct ether_addr *)mac_addr, eabuf),
		scb_val.val);
#ifndef BCMDBUS
	dhd_wait_pend8021x(dev);
#endif /* !BCMDBUS */
	err = wldev_ioctl_set(dev, WLC_SCB_DEAUTHENTICATE_FOR_REASON, &scb_val,
		sizeof(scb_val_t));
	if (err < 0)
		WL_ERR(("WLC_SCB_DEAUTHENTICATE_FOR_REASON err %d\n", err));
#ifdef CUSTOM_BLOCK_DEAUTH_AT_EAP_FAILURE
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0))
	}
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)) */
#endif /* CUSTOM_BLOCK_DEAUTH_AT_EAP_FAILURE */

	if (num_associated > 0 && ETHER_ISBCAST(mac_addr))
		wl_delay(400);

	return 0;
}

static s32
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
wl_cfg80211_change_station(
	struct wiphy *wiphy,
	struct net_device *dev,
	const u8 *mac,
	struct station_parameters *params)
#else
wl_cfg80211_change_station(
	struct wiphy *wiphy,
	struct net_device *dev,
	u8 *mac,
	struct station_parameters *params)
#endif
{
	int err;
#if defined(WL_ENABLE_P2P_IF)
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
#endif
	struct net_device *ndev = ndev_to_wlc_ndev(dev, cfg);

	WL_DBG(("SCB_AUTHORIZE mac_addr:"MACDBG" sta_flags_mask:0x%x "
				"sta_flags_set:0x%x iface:%s \n", MAC2STRDBG(mac),
				params->sta_flags_mask, params->sta_flags_set, ndev->name));

	/* Processing only authorize/de-authorize flag for now */
	if (!(params->sta_flags_mask & BIT(NL80211_STA_FLAG_AUTHORIZED))) {
		WL_ERR(("WLC_SCB_AUTHORIZE sta_flags_mask not set \n"));
		return -ENOTSUPP;
	}

	if (!(params->sta_flags_set & BIT(NL80211_STA_FLAG_AUTHORIZED))) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
		err = wldev_ioctl_set(ndev, WLC_SCB_DEAUTHORIZE, (u8 *)mac, ETH_ALEN);
#else
		err = wldev_ioctl_set(ndev, WLC_SCB_DEAUTHORIZE, mac, ETH_ALEN);
#endif
		if (err)
			WL_ERR(("WLC_SCB_DEAUTHORIZE error (%d)\n", err));
		return err;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
	err = wldev_ioctl_set(ndev, WLC_SCB_AUTHORIZE, (u8 *)mac, ETH_ALEN);
#else
	err = wldev_ioctl_set(ndev, WLC_SCB_AUTHORIZE, mac, ETH_ALEN);
#endif
	if (err)
		WL_ERR(("WLC_SCB_AUTHORIZE error (%d)\n", err));
#ifdef DHD_LOSSLESS_ROAMING
	wl_del_roam_timeout(cfg);
#endif
	return err;
}
#endif /* WL_SUPPORT_BACKPORTED_KPATCHES || KERNEL_VER >= KERNEL_VERSION(3, 2, 0)) */

static s32
wl_cfg80211_set_scb_timings(
	struct bcm_cfg80211 *cfg,
	struct net_device *dev)
{
	int err;
	u32 ps_pretend;
	wl_scb_probe_t scb_probe;

	bzero(&scb_probe, sizeof(wl_scb_probe_t));
	scb_probe.scb_timeout = WL_SCB_TIMEOUT;
	scb_probe.scb_activity_time = WL_SCB_ACTIVITY_TIME;
	scb_probe.scb_max_probe = WL_SCB_MAX_PROBE;
	err = wldev_iovar_setbuf(dev, "scb_probe", (void *)&scb_probe,
		sizeof(wl_scb_probe_t), cfg->ioctl_buf, WLC_IOCTL_SMLEN,
		&cfg->ioctl_buf_sync);
	if (unlikely(err)) {
		WL_ERR(("set 'scb_probe' failed, error = %d\n", err));
		return err;
	}

	ps_pretend = MAX(WL_SCB_MAX_PROBE / 2, WL_MIN_PSPRETEND_THRESHOLD);
	err = wldev_iovar_setint(dev, "pspretend_threshold", ps_pretend);
	if (unlikely(err)) {
		if (err == BCME_UNSUPPORTED) {
			/* Ignore error if fw doesn't support the iovar */
			WL_DBG(("wl pspretend_threshold %d set error %d\n",
				ps_pretend, err));
		} else {
			WL_ERR(("wl pspretend_threshold %d set error %d\n",
				ps_pretend, err));
			return err;
		}
	}

	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0))
static s32
wl_cfg80211_start_ap(
	struct wiphy *wiphy,
	struct net_device *dev,
	struct cfg80211_ap_settings *info)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	s32 err = BCME_OK;
	struct parsed_ies ies;
	s32 bssidx = 0;
	u32 dev_role = 0;
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);
#ifdef WLMESH_CFG80211
	struct wl_join_params join_params;
	s32 join_params_size = 0;
#endif

	WL_DBG(("Enter \n"));

#if defined(SUPPORT_RANDOM_MAC_SCAN)
	wl_cfg80211_set_random_mac(dev, FALSE);
#endif /* SUPPORT_RANDOM_MAC_SCAN */

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find p2p index from wdev(%p) failed\n", dev->ieee80211_ptr));
		return BCME_ERROR;
	}

	if (p2p_is_on(cfg) && (dev->ieee80211_ptr->iftype == NL80211_IFTYPE_P2P_GO)) {
		dev_role = NL80211_IFTYPE_P2P_GO;
	} else if (dev->ieee80211_ptr->iftype == NL80211_IFTYPE_AP) {
		dev_role = NL80211_IFTYPE_AP;
		dhd->op_mode |= DHD_FLAG_HOSTAP_MODE;
		err = dhd_ndo_enable(dhd, FALSE);
		WL_DBG(("%s: Disabling NDO on Hostapd mode %d\n", __FUNCTION__, err));
		if (err) {
			WL_ERR(("%s: Disabling NDO Failed %d\n", __FUNCTION__, err));
		}
#ifdef PKT_FILTER_SUPPORT
		/* Disable packet filter */
		if (dhd->early_suspended) {
			WL_ERR(("Disable pkt_filter\n"));
			dhd_enable_packet_filter(0, dhd);
		}
#endif /* PKT_FILTER_SUPPORT */
#ifdef ARP_OFFLOAD_SUPPORT
		/* IF SoftAP is enabled, disable arpoe */
		if (dhd->op_mode & DHD_FLAG_STA_MODE) {
			dhd_arp_offload_set(dhd, 0);
			dhd_arp_offload_enable(dhd, FALSE);
		}
#endif /* ARP_OFFLOAD_SUPPORT */
#ifdef SUPPORT_SET_CAC
		wl_cfg80211_set_cac(cfg, 0);
#endif /* SUPPORT_SET_CAC */
	} else {
		/* only AP or GO role need to be handled here. */
		err = -EINVAL;
		goto fail;
	}

	/* disable TDLS */
#ifdef WLTDLS
	if (bssidx == 0) {
		/* Disable TDLS for primary Iface. For virtual interface,
		 * tdls disable will happen from interface create context
		 */
		wl_cfg80211_tdls_config(cfg, TDLS_STATE_AP_CREATE, false);
	}
#endif /*  WLTDLS */

	if (!check_dev_role_integrity(cfg, dev_role)) {
		err = -EINVAL;
		goto fail;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
	if ((err = wl_cfg80211_set_channel(wiphy, dev,
		dev->ieee80211_ptr->preset_chandef.chan,
		NL80211_CHAN_HT20) < 0)) {
		WL_ERR(("Set channel failed \n"));
		goto fail;
	}
#endif 

	if ((err = wl_cfg80211_bcn_set_params(info, dev,
		dev_role, bssidx)) < 0) {
		WL_ERR(("Beacon params set failed \n"));
		goto fail;
	}

	/* Parse IEs */
	if ((err = wl_cfg80211_parse_ap_ies(dev, &info->beacon, &ies)) < 0) {
		WL_ERR(("Set IEs failed \n"));
		goto fail;
	}

	if ((err = wl_cfg80211_bcn_validate_sec(dev, &ies,
		dev_role, bssidx, info->privacy)) < 0)
	{
		WL_ERR(("Beacon set security failed \n"));
		goto fail;
	}

	if ((err = wl_cfg80211_bcn_bringup_ap(dev, &ies,
		dev_role, bssidx)) < 0) {
		WL_ERR(("Beacon bring up AP/GO failed \n"));
		goto fail;
	}

	/* Set GC/STA SCB expiry timings. */
	if ((err = wl_cfg80211_set_scb_timings(cfg, dev))) {
		WL_ERR(("scb setting failed \n"));
//		goto fail;
	}

#ifdef WLMESH_CFG80211
	OSL_SLEEP(1000);
	if ((dev_role == NL80211_IFTYPE_P2P_GO) || (dev_role == NL80211_IFTYPE_AP)) {
		memset(&join_params, 0, sizeof(join_params));
		/* join parameters starts with ssid */
		join_params_size = sizeof(join_params.ssid);
		if (dev_role == NL80211_IFTYPE_P2P_GO) {
			join_params.ssid.SSID_len = min(cfg->p2p->ssid.SSID_len,
				(uint32)DOT11_MAX_SSID_LEN);
			memcpy(join_params.ssid.SSID, cfg->p2p->ssid.SSID,
				join_params.ssid.SSID_len);
		} else if (dev_role == NL80211_IFTYPE_AP) {
			join_params.ssid.SSID_len = min(cfg->hostapd_ssid.SSID_len,
				(uint32)DOT11_MAX_SSID_LEN);
			memcpy(join_params.ssid.SSID, cfg->hostapd_ssid.SSID,
				join_params.ssid.SSID_len);
		}
		join_params.ssid.SSID_len = htod32(join_params.ssid.SSID_len);
		/* create softap */
		if ((err = wldev_ioctl_set(dev, WLC_SET_SSID, &join_params,
			join_params_size)) != 0) {
			WL_ERR(("SoftAP/GO set ssid failed! \n"));
			goto fail;
		} else {
			WL_DBG((" SoftAP SSID \"%s\" \n", join_params.ssid.SSID));
		}
	}
#endif

	WL_DBG(("** AP/GO Created **\n"));

#ifdef WL_CFG80211_ACL
	/* Enfoce Admission Control. */
	if ((err = wl_cfg80211_set_mac_acl(wiphy, dev, info->acl)) < 0) {
		WL_ERR(("Set ACL failed\n"));
	}
#endif /* WL_CFG80211_ACL */

	/* Set IEs to FW */
	if ((err = wl_cfg80211_set_ies(dev, &info->beacon, bssidx)) < 0)
		WL_ERR(("Set IEs failed \n"));

	/* Enable Probe Req filter, WPS-AP certification 4.2.13 */
	if ((dev_role == NL80211_IFTYPE_AP) && (ies.wps_ie != NULL)) {
		bool pbc = 0;
		wl_validate_wps_ie((char *) ies.wps_ie, ies.wps_ie_len, &pbc);
		if (pbc) {
			WL_DBG(("set WLC_E_PROBREQ_MSG\n"));
			wl_add_remove_eventmsg(dev, WLC_E_PROBREQ_MSG, true);
		}
	}

#ifdef SUPPORT_AP_RADIO_PWRSAVE
	if ((dev_role == NL80211_IFTYPE_AP)) {
		wl_set_ap_rps(dev, FALSE, dev->name);
		wl_cfg80211_init_ap_rps(cfg);
	}
#endif /* SUPPORT_AP_RADIO_PWRSAVE */
fail:
	if (err) {
		WL_ERR(("ADD/SET beacon failed\n"));
		wl_cfg80211_stop_ap(wiphy, dev);
		if (dev_role == NL80211_IFTYPE_AP) {
			dhd->op_mode &= ~DHD_FLAG_HOSTAP_MODE;
#ifdef PKT_FILTER_SUPPORT
			/* Enable packet filter */
			if (dhd->early_suspended) {
				WL_ERR(("Enable pkt_filter\n"));
				dhd_enable_packet_filter(1, dhd);
			}
#endif /* PKT_FILTER_SUPPORT */
#ifdef ARP_OFFLOAD_SUPPORT
			/* IF SoftAP is disabled, enable arpoe back for STA mode. */
			if (dhd->op_mode & DHD_FLAG_STA_MODE) {
				dhd_arp_offload_set(dhd, dhd_arp_mode);
				dhd_arp_offload_enable(dhd, TRUE);
			}
#endif /* ARP_OFFLOAD_SUPPORT */
		}
#ifdef WLTDLS
		if (bssidx == 0) {
			/* Since AP creation failed, re-enable TDLS */
			wl_cfg80211_tdls_config(cfg, TDLS_STATE_AP_DELETE, false);
		}
#endif /*  WLTDLS */

	}

	return err;
}

static s32
wl_cfg80211_stop_ap(
	struct wiphy *wiphy,
	struct net_device *dev)
{
	int err = 0;
	u32 dev_role = 0;
	int ap = 0;
	s32 bssidx = 0;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	s32 is_rsdb_supported = BCME_ERROR;
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);

	WL_DBG(("Enter \n"));

	is_rsdb_supported = DHD_OPMODE_SUPPORTED(cfg->pub, DHD_FLAG_RSDB_MODE);
	if (is_rsdb_supported < 0)
		return (-ENODEV);

	wl_clr_drv_status(cfg, AP_CREATING, dev);
	wl_clr_drv_status(cfg, AP_CREATED, dev);
	cfg->ap_oper_channel = 0;

	if (dev->ieee80211_ptr->iftype == NL80211_IFTYPE_AP) {
		dev_role = NL80211_IFTYPE_AP;
		WL_DBG(("stopping AP operation\n"));
	} else if (dev->ieee80211_ptr->iftype == NL80211_IFTYPE_P2P_GO) {
		dev_role = NL80211_IFTYPE_P2P_GO;
		WL_DBG(("stopping P2P GO operation\n"));
	} else {
		WL_ERR(("no AP/P2P GO interface is operational.\n"));
		return -EINVAL;
	}

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("find p2p index from wdev(%p) failed\n", dev->ieee80211_ptr));
		return BCME_ERROR;
	}

	if (!check_dev_role_integrity(cfg, dev_role)) {
		WL_ERR(("role integrity check failed \n"));
		err = -EINVAL;
		goto exit;
	}

	/* Clear AP/GO connected status */
	wl_clr_drv_status(cfg, CONNECTED, dev);
	if ((err = wl_cfg80211_bss_up(cfg, dev, bssidx, 0)) < 0) {
		WL_ERR(("bss down error %d\n", err));
	}

	if (dev_role == NL80211_IFTYPE_AP) {
#ifdef PKT_FILTER_SUPPORT
		/* Enable packet filter */
		if (dhd->early_suspended) {
			WL_ERR(("Enable pkt_filter\n"));
			dhd_enable_packet_filter(1, dhd);
		}
#endif /* PKT_FILTER_SUPPORT */
#ifdef ARP_OFFLOAD_SUPPORT
		/* IF SoftAP is disabled, enable arpoe back for STA mode. */
		if (dhd->op_mode & DHD_FLAG_STA_MODE) {
			dhd_arp_offload_set(dhd, dhd_arp_mode);
			dhd_arp_offload_enable(dhd, TRUE);
		}
#endif /* ARP_OFFLOAD_SUPPORT */

		if (is_rsdb_supported == 0) {
			if (dhd_download_fw_on_driverload && bssidx == 0) {
				wldev_ioctl_set(dev, WLC_DOWN, &ap, sizeof(s32));
				wldev_iovar_setint(dev, "apsta", 1);
			}
			/* For non-rsdb chips, we use stand alone AP. Do wl down on stop AP */
			err = wldev_ioctl_set(dev, WLC_UP, &ap, sizeof(s32));
			if (unlikely(err)) {
				WL_ERR(("WLC_UP error (%d)\n", err));
				err = -EINVAL;
				goto exit;
			}
		}

		 wl_cfg80211_clear_per_bss_ies(cfg, bssidx);
#ifdef SUPPORT_AP_RADIO_PWRSAVE
		wl_set_ap_rps(dev, FALSE, dev->name);
		wl_cfg80211_init_ap_rps(cfg);
#endif /* SUPPORT_AP_RADIO_PWRSAVE */
	} else {
		WL_DBG(("Stopping P2P GO \n"));
		DHD_OS_WAKE_LOCK_CTRL_TIMEOUT_ENABLE((dhd_pub_t *)(cfg->pub),
			DHD_EVENT_TIMEOUT_MS*3);
		DHD_OS_WAKE_LOCK_TIMEOUT((dhd_pub_t *)(cfg->pub));
	}
exit:
#ifdef WLTDLS
	if (bssidx == 0) {
		/* re-enable TDLS if the number of connected interfaces is less than 2 */
		wl_cfg80211_tdls_config(cfg, TDLS_STATE_AP_DELETE, false);
	}
#endif /* WLTDLS */

	if (dev_role == NL80211_IFTYPE_AP) {
		/* clear the AP mode */
		dhd->op_mode &= ~DHD_FLAG_HOSTAP_MODE;
	}
#ifdef SUPPORT_SET_CAC
	wl_cfg80211_set_cac(cfg, 1);
#endif /* SUPPORT_SET_CAC */
	return err;
}

static s32
wl_cfg80211_change_beacon(
	struct wiphy *wiphy,
	struct net_device *dev,
	struct cfg80211_beacon_data *info)
{
	s32 err = BCME_OK;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct parsed_ies ies;
	u32 dev_role = 0;
	s32 bssidx = 0;
	bool pbc = 0;

	WL_DBG(("Enter \n"));

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find p2p index from wdev(%p) failed\n", dev->ieee80211_ptr));
		return BCME_ERROR;
	}

	if (dev->ieee80211_ptr->iftype == NL80211_IFTYPE_P2P_GO) {
		dev_role = NL80211_IFTYPE_P2P_GO;
	} else if (dev->ieee80211_ptr->iftype == NL80211_IFTYPE_AP) {
		dev_role = NL80211_IFTYPE_AP;
	} else {
		err = -EINVAL;
		goto fail;
	}

	if (!check_dev_role_integrity(cfg, dev_role)) {
		err = -EINVAL;
		goto fail;
	}

	if ((dev_role == NL80211_IFTYPE_P2P_GO) && (cfg->p2p_wdev == NULL)) {
		WL_ERR(("P2P already down status!\n"));
		err = BCME_ERROR;
		goto fail;
	}

	/* Parse IEs */
	if ((err = wl_cfg80211_parse_ap_ies(dev, info, &ies)) < 0) {
		WL_ERR(("Parse IEs failed \n"));
		goto fail;
	}

	/* Set IEs to FW */
	if ((err = wl_cfg80211_set_ies(dev, info, bssidx)) < 0) {
		WL_ERR(("Set IEs failed \n"));
		goto fail;
	}

	if (dev_role == NL80211_IFTYPE_AP) {
		if (wl_cfg80211_hostapd_sec(dev, &ies, bssidx) < 0) {
			WL_ERR(("Hostapd update sec failed \n"));
			err = -EINVAL;
			goto fail;
		}
		/* Enable Probe Req filter, WPS-AP certification 4.2.13 */
		if ((dev_role == NL80211_IFTYPE_AP) && (ies.wps_ie != NULL)) {
			wl_validate_wps_ie((char *) ies.wps_ie, ies.wps_ie_len, &pbc);
			WL_DBG((" WPS AP, wps_ie is exists pbc=%d\n", pbc));
			if (pbc)
				wl_add_remove_eventmsg(dev, WLC_E_PROBREQ_MSG, true);
			else
				wl_add_remove_eventmsg(dev, WLC_E_PROBREQ_MSG, false);
		}
	}

fail:
	return err;
}
#else
static s32
wl_cfg80211_add_set_beacon(struct wiphy *wiphy, struct net_device *dev,
	struct beacon_parameters *info)
{
	s32 err = BCME_OK;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	s32 ie_offset = 0;
	s32 bssidx = 0;
	u32 dev_role = NL80211_IFTYPE_AP;
	struct parsed_ies ies;
	bcm_tlv_t *ssid_ie;
	bool pbc = 0;
	bool privacy;
	bool is_bss_up = 0;
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);

	WL_DBG(("interval (%d) dtim_period (%d) head_len (%d) tail_len (%d)\n",
		info->interval, info->dtim_period, info->head_len, info->tail_len));

	if (dev == bcmcfg_to_prmry_ndev(cfg)) {
		dev_role = NL80211_IFTYPE_AP;
	}
#if defined(WL_ENABLE_P2P_IF)
	else if (dev == cfg->p2p_net) {
		/* Group Add request on p2p0 */
		dev = bcmcfg_to_prmry_ndev(cfg);
		dev_role = NL80211_IFTYPE_P2P_GO;
	}
#endif /* WL_ENABLE_P2P_IF */

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find p2p index from wdev(%p) failed\n", dev->ieee80211_ptr));
		return BCME_ERROR;
	}

	if (dev->ieee80211_ptr->iftype == NL80211_IFTYPE_P2P_GO) {
		dev_role = NL80211_IFTYPE_P2P_GO;
	} else if (dev->ieee80211_ptr->iftype == NL80211_IFTYPE_AP) {
		dhd->op_mode |= DHD_FLAG_HOSTAP_MODE;
	}

	if (!check_dev_role_integrity(cfg, dev_role)) {
		err = -ENODEV;
		goto fail;
	}

	if ((dev_role == NL80211_IFTYPE_P2P_GO) && (cfg->p2p_wdev == NULL)) {
		WL_ERR(("P2P already down status!\n"));
		err = BCME_ERROR;
		goto fail;
	}

	ie_offset = DOT11_MGMT_HDR_LEN + DOT11_BCN_PRB_FIXED_LEN;
	/* find the SSID */
	if ((ssid_ie = bcm_parse_tlvs((u8 *)&info->head[ie_offset],
		info->head_len - ie_offset,
		DOT11_MNG_SSID_ID)) != NULL) {
		if (dev_role == NL80211_IFTYPE_AP) {
			/* Store the hostapd SSID */
			memset(&cfg->hostapd_ssid.SSID[0], 0x00, DOT11_MAX_SSID_LEN);
			cfg->hostapd_ssid.SSID_len = MIN(ssid_ie->len, DOT11_MAX_SSID_LEN);
			memcpy(&cfg->hostapd_ssid.SSID[0], ssid_ie->data,
				cfg->hostapd_ssid.SSID_len);
		} else {
			/* P2P GO */
			memset(&cfg->p2p->ssid.SSID[0], 0x00, DOT11_MAX_SSID_LEN);
			cfg->p2p->ssid.SSID_len = MIN(ssid_ie->len, DOT11_MAX_SSID_LEN);
			memcpy(cfg->p2p->ssid.SSID, ssid_ie->data,
				cfg->p2p->ssid.SSID_len);
		}
	}

	if (wl_cfg80211_parse_ies((u8 *)info->tail,
		info->tail_len, &ies) < 0) {
		WL_ERR(("Beacon get IEs failed \n"));
		err = -EINVAL;
		goto fail;
	}

	if ((err = wl_cfg80211_set_mgmt_vndr_ies(cfg, ndev_to_cfgdev(dev), bssidx,
		VNDR_IE_BEACON_FLAG, (u8 *)info->tail,
		info->tail_len)) < 0) {
		WL_ERR(("Beacon set IEs failed \n"));
		goto fail;
	} else {
		WL_DBG(("Applied Vndr IEs for Beacon \n"));
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0))
	if ((err = wl_cfg80211_set_mgmt_vndr_ies(cfg, ndev_to_cfgdev(dev), bssidx,
		VNDR_IE_PRBRSP_FLAG, (u8 *)info->proberesp_ies,
		info->proberesp_ies_len)) < 0) {
		WL_ERR(("ProbeRsp set IEs failed \n"));
		goto fail;
	} else {
		WL_DBG(("Applied Vndr IEs for ProbeRsp \n"));
	}
#endif

	is_bss_up = wl_cfg80211_bss_isup(dev, bssidx);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0))
	privacy = info->privacy;
#else
	privacy = 0;
#endif
	if (!is_bss_up &&
		(wl_cfg80211_bcn_validate_sec(dev, &ies, dev_role, bssidx, privacy) < 0))
	{
		WL_ERR(("Beacon set security failed \n"));
		err = -EINVAL;
		goto fail;
	}

	/* Set BI and DTIM period */
	if (info->interval) {
		if ((err = wldev_ioctl_set(dev, WLC_SET_BCNPRD,
			&info->interval, sizeof(s32))) < 0) {
			WL_ERR(("Beacon Interval Set Error, %d\n", err));
			return err;
		}
	}
	if (info->dtim_period) {
		if ((err = wldev_ioctl_set(dev, WLC_SET_DTIMPRD,
			&info->dtim_period, sizeof(s32))) < 0) {
			WL_ERR(("DTIM Interval Set Error, %d\n", err));
			return err;
		}
	}

	/* If bss is already up, skip bring up */
	if (!is_bss_up &&
		(err = wl_cfg80211_bcn_bringup_ap(dev, &ies, dev_role, bssidx)) < 0)
	{
		WL_ERR(("Beacon bring up AP/GO failed \n"));
		goto fail;
	}

	/* Set GC/STA SCB expiry timings. */
	if ((err = wl_cfg80211_set_scb_timings(cfg, dev))) {
		WL_ERR(("scb setting failed \n"));
		if (err == BCME_UNSUPPORTED)
			err = 0;
//		goto fail;
	}

	if (wl_get_drv_status(cfg, AP_CREATED, dev)) {
		/* Soft AP already running. Update changed params */
		if (wl_cfg80211_hostapd_sec(dev, &ies, bssidx) < 0) {
			WL_ERR(("Hostapd update sec failed \n"));
			err = -EINVAL;
			goto fail;
		}
	}

	/* Enable Probe Req filter */
	if (((dev_role == NL80211_IFTYPE_P2P_GO) ||
		(dev_role == NL80211_IFTYPE_AP)) && (ies.wps_ie != NULL)) {
		wl_validate_wps_ie((char *) ies.wps_ie, ies.wps_ie_len, &pbc);
		if (pbc)
			wl_add_remove_eventmsg(dev, WLC_E_PROBREQ_MSG, true);
	}

	WL_DBG(("** ADD/SET beacon done **\n"));

fail:
	if (err) {
		WL_ERR(("ADD/SET beacon failed\n"));
		if (dev_role == NL80211_IFTYPE_AP) {
			/* clear the AP mode */
			dhd->op_mode &= ~DHD_FLAG_HOSTAP_MODE;
		}
	}
	return err;

}

static s32
wl_cfg80211_del_beacon(struct wiphy *wiphy, struct net_device *dev)
{
	int err = 0;
	s32 bssidx = 0;
	int infra = 0;
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);

	WL_DBG(("Enter. \n"));

	if (!wdev) {
		WL_ERR(("wdev null \n"));
		return -EINVAL;
	}

	if ((wdev->iftype != NL80211_IFTYPE_P2P_GO) && (wdev->iftype != NL80211_IFTYPE_AP)) {
		WL_ERR(("Unspported iface type iftype:%d \n", wdev->iftype));
	}

	wl_clr_drv_status(cfg, AP_CREATING, dev);
	wl_clr_drv_status(cfg, AP_CREATED, dev);

	/* Clear AP/GO connected status */
	wl_clr_drv_status(cfg, CONNECTED, dev);

	cfg->ap_oper_channel = 0;


	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("find p2p index from wdev(%p) failed\n", dev->ieee80211_ptr));
		return BCME_ERROR;
	}

	/* Do bss down */
	if ((err = wl_cfg80211_bss_up(cfg, dev, bssidx, 0)) < 0) {
		WL_ERR(("bss down error %d\n", err));
	}

	/* fall through is intentional */
	err = wldev_ioctl_set(dev, WLC_SET_INFRA, &infra, sizeof(s32));
	if (err < 0) {
		WL_ERR(("SET INFRA error %d\n", err));
	}
	 wl_cfg80211_clear_per_bss_ies(cfg, bssidx);

	if (wdev->iftype == NL80211_IFTYPE_AP) {
		/* clear the AP mode */
		dhd->op_mode &= ~DHD_FLAG_HOSTAP_MODE;
	}

	return 0;
}
#endif 

#ifdef WL_SCHED_SCAN
#define PNO_TIME		30
#define PNO_REPEAT		4
#define PNO_FREQ_EXPO_MAX	2
static bool
is_ssid_in_list(struct cfg80211_ssid *ssid, struct cfg80211_ssid *ssid_list, int count)
{
	int i;

	if (!ssid || !ssid_list)
		return FALSE;

	for (i = 0; i < count; i++) {
		if (ssid->ssid_len == ssid_list[i].ssid_len) {
			if (strncmp(ssid->ssid, ssid_list[i].ssid, ssid->ssid_len) == 0)
				return TRUE;
		}
	}
	return FALSE;
}

static int
wl_cfg80211_sched_scan_start(struct wiphy *wiphy,
                             struct net_device *dev,
                             struct cfg80211_sched_scan_request *request)
{
	ushort pno_time = PNO_TIME;
	int pno_repeat = PNO_REPEAT;
	int pno_freq_expo_max = PNO_FREQ_EXPO_MAX;
	wlc_ssid_ext_t ssids_local[MAX_PFN_LIST_COUNT];
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
	struct cfg80211_ssid *ssid = NULL;
	struct cfg80211_ssid *hidden_ssid_list = NULL;
	log_conn_event_t *event_data = NULL;
	tlv_log *tlv_data = NULL;
	u32 alloc_len, tlv_len;
	u32 payload_len;
	int ssid_cnt = 0;
	int i;
	int ret = 0;
	unsigned long flags;

	if (!request) {
		WL_ERR(("Sched scan request was NULL\n"));
		return -EINVAL;
	}

	WL_DBG(("Enter \n"));
	WL_PNO((">>> SCHED SCAN START\n"));
	WL_PNO(("Enter n_match_sets:%d   n_ssids:%d \n",
		request->n_match_sets, request->n_ssids));
	WL_PNO(("ssids:%d pno_time:%d pno_repeat:%d pno_freq:%d \n",
		request->n_ssids, pno_time, pno_repeat, pno_freq_expo_max));


	if (!request->n_ssids || !request->n_match_sets) {
		WL_ERR(("Invalid sched scan req!! n_ssids:%d \n", request->n_ssids));
		return -EINVAL;
	}

	memset(&ssids_local, 0, sizeof(ssids_local));

	if (request->n_ssids > 0) {
		hidden_ssid_list = request->ssids;
	}

	if (DBG_RING_ACTIVE(dhdp, DHD_EVENT_RING_ID)) {
		alloc_len = sizeof(log_conn_event_t) + DOT11_MAX_SSID_LEN;
		event_data = MALLOC(dhdp->osh, alloc_len);
		if (!event_data) {
			WL_ERR(("%s: failed to allocate log_conn_event_t with "
						"length(%d)\n", __func__, alloc_len));
			return -ENOMEM;
		}
		memset(event_data, 0, alloc_len);
		event_data->tlvs = NULL;
		tlv_len = sizeof(tlv_log);
		event_data->tlvs = (tlv_log *)MALLOC(dhdp->osh, tlv_len);
		if (!event_data->tlvs) {
			WL_ERR(("%s: failed to allocate log_tlv with "
					"length(%d)\n", __func__, tlv_len));
			MFREE(dhdp->osh, event_data, alloc_len);
			return -ENOMEM;
		}
	}
	for (i = 0; i < request->n_match_sets && ssid_cnt < MAX_PFN_LIST_COUNT; i++) {
		ssid = &request->match_sets[i].ssid;
		/* No need to include null ssid */
		if (ssid->ssid_len) {
			ssids_local[ssid_cnt].SSID_len = MIN(ssid->ssid_len,
				(uint32)DOT11_MAX_SSID_LEN);
			memcpy(ssids_local[ssid_cnt].SSID, ssid->ssid,
				ssids_local[ssid_cnt].SSID_len);
			if (is_ssid_in_list(ssid, hidden_ssid_list, request->n_ssids)) {
				ssids_local[ssid_cnt].hidden = TRUE;
				WL_PNO((">>> PNO hidden SSID (%s) \n", ssid->ssid));
			} else {
				ssids_local[ssid_cnt].hidden = FALSE;
				WL_PNO((">>> PNO non-hidden SSID (%s) \n", ssid->ssid));
			}
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 15, 0))
			if (request->match_sets[i].rssi_thold != NL80211_SCAN_RSSI_THOLD_OFF) {
				ssids_local[ssid_cnt].rssi_thresh =
				      (int8)request->match_sets[i].rssi_thold;
			}
#endif /* (LINUX_VERSION_CODE > KERNEL_VERSION(3, 15, 0)) */
			ssid_cnt++;
		}
	}

	if (ssid_cnt) {
		if ((ret = dhd_dev_pno_set_for_ssid(dev, ssids_local, ssid_cnt,
			pno_time, pno_repeat, pno_freq_expo_max, NULL, 0)) < 0) {
			WL_ERR(("PNO setup failed!! ret=%d \n", ret));
			ret = -EINVAL;
			goto exit;
		}

		if (DBG_RING_ACTIVE(dhdp, DHD_EVENT_RING_ID)) {
			for (i = 0; i < ssid_cnt; i++) {
				payload_len = sizeof(log_conn_event_t);
				event_data->event = WIFI_EVENT_DRIVER_PNO_ADD;
				tlv_data = event_data->tlvs;
				/* ssid */
				tlv_data->tag = WIFI_TAG_SSID;
				tlv_data->len = ssids_local[i].SSID_len;
				memcpy(tlv_data->value, ssids_local[i].SSID,
					ssids_local[i].SSID_len);
				payload_len += TLV_LOG_SIZE(tlv_data);

				dhd_os_push_push_ring_data(dhdp, DHD_EVENT_RING_ID,
					event_data, payload_len);
			}
		}

		spin_lock_irqsave(&cfg->cfgdrv_lock, flags);
		cfg->sched_scan_req = request;
		spin_unlock_irqrestore(&cfg->cfgdrv_lock, flags);
	} else {
		ret = -EINVAL;
	}
exit:
	if (event_data) {
		MFREE(dhdp->osh, event_data->tlvs, tlv_len);
		MFREE(dhdp->osh, event_data, alloc_len);
	}
	return ret;
}

static int
wl_cfg80211_sched_scan_stop(struct wiphy *wiphy, struct net_device *dev
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0))
	, u64 reqid
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)) */
)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
	unsigned long flags;

	WL_DBG(("Enter \n"));
	WL_PNO((">>> SCHED SCAN STOP\n"));

	BCM_REFERENCE(dhdp);
	if (dhd_dev_pno_stop_for_ssid(dev) < 0) {
		WL_ERR(("PNO Stop for SSID failed"));
	} else {
		DBG_EVENT_LOG(dhdp, WIFI_EVENT_DRIVER_PNO_REMOVE);
	}

	if (cfg->scan_request && cfg->sched_scan_running) {
		WL_PNO((">>> Sched scan running. Aborting it..\n"));
		wl_notify_escan_complete(cfg, dev, true, true);
	}
	spin_lock_irqsave(&cfg->cfgdrv_lock, flags);
	cfg->sched_scan_req = NULL;
	cfg->sched_scan_running = FALSE;
	spin_unlock_irqrestore(&cfg->cfgdrv_lock, flags);
	return 0;
}
#endif /* WL_SCHED_SCAN */

#ifdef WL_SUPPORT_ACS
/*
 * Currently the dump_obss IOVAR is returning string as output so we need to
 * parse the output buffer in an unoptimized way. Going forward if we get the
 * IOVAR output in binary format this method can be optimized
 */
static int wl_parse_dump_obss(char *buf, struct wl_dump_survey *survey)
{
	int i;
	char *token;
	char delim[] = " \n";

	token = strsep(&buf, delim);
	while (token != NULL) {
		if (!strcmp(token, "OBSS")) {
			for (i = 0; i < OBSS_TOKEN_IDX; i++)
				token = strsep(&buf, delim);
			survey->obss = simple_strtoul(token, NULL, 10);
		}

		if (!strcmp(token, "IBSS")) {
			for (i = 0; i < IBSS_TOKEN_IDX; i++)
				token = strsep(&buf, delim);
			survey->ibss = simple_strtoul(token, NULL, 10);
		}

		if (!strcmp(token, "TXDur")) {
			for (i = 0; i < TX_TOKEN_IDX; i++)
				token = strsep(&buf, delim);
			survey->tx = simple_strtoul(token, NULL, 10);
		}

		if (!strcmp(token, "Category")) {
			for (i = 0; i < CTG_TOKEN_IDX; i++)
				token = strsep(&buf, delim);
			survey->no_ctg = simple_strtoul(token, NULL, 10);
		}

		if (!strcmp(token, "Packet")) {
			for (i = 0; i < PKT_TOKEN_IDX; i++)
				token = strsep(&buf, delim);
			survey->no_pckt = simple_strtoul(token, NULL, 10);
		}

		if (!strcmp(token, "Opp(time):")) {
			for (i = 0; i < IDLE_TOKEN_IDX; i++)
				token = strsep(&buf, delim);
			survey->idle = simple_strtoul(token, NULL, 10);
		}

		token = strsep(&buf, delim);
	}

	return 0;
}

static int wl_dump_obss(struct net_device *ndev, cca_msrmnt_query req,
	struct wl_dump_survey *survey)
{
	cca_stats_n_flags *results;
	char *buf;
	int retry, err;

	buf = kzalloc(sizeof(char) * WLC_IOCTL_MAXLEN, GFP_KERNEL);
	if (unlikely(!buf)) {
		WL_ERR(("%s: buf alloc failed\n", __func__));
		return -ENOMEM;
	}

	retry = IOCTL_RETRY_COUNT;
	while (retry--) {
		err = wldev_iovar_getbuf(ndev, "dump_obss", &req, sizeof(req),
			buf, WLC_IOCTL_MAXLEN, NULL);
		if (err >=  0) {
			break;
		}
		WL_DBG(("attempt = %d, err = %d, \n",
			(IOCTL_RETRY_COUNT - retry), err));
	}

	if (retry <= 0)	{
		WL_ERR(("failure, dump_obss IOVAR failed\n"));
		err = -EINVAL;
		goto exit;
	}

	results = (cca_stats_n_flags *)(buf);
	wl_parse_dump_obss(results->buf, survey);
	kfree(buf);

	return 0;
exit:
	kfree(buf);
	return err;
}

static int wl_cfg80211_dump_survey(struct wiphy *wiphy, struct net_device *ndev,
	int idx, struct survey_info *info)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct wl_dump_survey *survey;
	struct ieee80211_supported_band *band;
	struct ieee80211_channel*chan;
	cca_msrmnt_query req;
	int val, err, noise, retry;

	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);
	if (!(dhd->op_mode & DHD_FLAG_HOSTAP_MODE)) {
		return -ENOENT;
	}
	band = wiphy->bands[IEEE80211_BAND_2GHZ];
	if (band && idx >= band->n_channels) {
		idx -= band->n_channels;
		band = NULL;
	}

	if (!band || idx >= band->n_channels) {
		/* Move to 5G band */
		band = wiphy->bands[IEEE80211_BAND_5GHZ];
		if (idx >= band->n_channels) {
			return -ENOENT;
		}
	}

	chan = &band->channels[idx];
	/* Setting current channel to the requested channel */
	if ((err = wl_cfg80211_set_channel(wiphy, ndev, chan,
		NL80211_CHAN_HT20) < 0)) {
		WL_ERR(("Set channel failed \n"));
	}

	if (!idx) {
		/* Set interface up, explicitly. */
		val = 1;
		err = wldev_ioctl_set(ndev, WLC_UP, (void *)&val, sizeof(val));
		if (err < 0) {
			WL_ERR(("set interface up failed, error = %d\n", err));
		}
	}

	/* Get noise value */
	retry = IOCTL_RETRY_COUNT;
	while (retry--) {
		noise = 0;
		err = wldev_ioctl_get(ndev, WLC_GET_PHY_NOISE, &noise,
			sizeof(noise));
		if (err >=  0) {
			break;
		}
		WL_DBG(("attempt = %d, err = %d, \n",
			(IOCTL_RETRY_COUNT - retry), err));
	}

	if (retry <= 0)	{
		WL_ERR(("Get Phy Noise failed, error = %d\n", err));
		noise = CHAN_NOISE_DUMMY;
	}

	survey = (struct wl_dump_survey *) kzalloc(sizeof(struct wl_dump_survey),
		GFP_KERNEL);
	if (unlikely(!survey)) {
		WL_ERR(("%s: alloc failed\n", __func__));
		return -ENOMEM;
	}

	/* Start Measurement for obss stats on current channel */
	req.msrmnt_query = 0;
	req.time_req = ACS_MSRMNT_DELAY;
	if ((err = wl_dump_obss(ndev, req, survey)) < 0) {
		goto exit;
	}

	/*
	 * Wait for the meaurement to complete, adding a buffer value of 10 to take
	 * into consideration any delay in IOVAR completion
	 */
	msleep(ACS_MSRMNT_DELAY + 10);

	/* Issue IOVAR to collect measurement results */
	req.msrmnt_query = 1;
	if ((err = wl_dump_obss(ndev, req, survey)) < 0) {
		goto exit;
	}

	info->channel = chan;
	info->noise = noise;
	info->channel_time = ACS_MSRMNT_DELAY;
	info->channel_time_busy = ACS_MSRMNT_DELAY - survey->idle;
	info->channel_time_rx = survey->obss + survey->ibss + survey->no_ctg +
		survey->no_pckt;
	info->channel_time_tx = survey->tx;
	info->filled = SURVEY_INFO_NOISE_DBM |SURVEY_INFO_CHANNEL_TIME |
		SURVEY_INFO_CHANNEL_TIME_BUSY |	SURVEY_INFO_CHANNEL_TIME_RX |
		SURVEY_INFO_CHANNEL_TIME_TX;
	kfree(survey);

	return 0;
exit:
	kfree(survey);
	return err;
}
#endif /* WL_SUPPORT_ACS */

static struct cfg80211_ops wl_cfg80211_ops = {
	.add_virtual_intf = wl_cfg80211_add_virtual_iface,
	.del_virtual_intf = wl_cfg80211_del_virtual_iface,
	.change_virtual_intf = wl_cfg80211_change_virtual_iface,
#if defined(WL_CFG80211_P2P_DEV_IF)
	.start_p2p_device = wl_cfgp2p_start_p2p_device,
	.stop_p2p_device = wl_cfgp2p_stop_p2p_device,
#endif /* WL_CFG80211_P2P_DEV_IF */
	.scan = wl_cfg80211_scan,
	.set_wiphy_params = wl_cfg80211_set_wiphy_params,
	.join_ibss = wl_cfg80211_join_ibss,
	.leave_ibss = wl_cfg80211_leave_ibss,
	.get_station = wl_cfg80211_get_station,
	.set_tx_power = wl_cfg80211_set_tx_power,
	.get_tx_power = wl_cfg80211_get_tx_power,
	.add_key = wl_cfg80211_add_key,
	.del_key = wl_cfg80211_del_key,
	.get_key = wl_cfg80211_get_key,
	.set_default_key = wl_cfg80211_config_default_key,
	.set_default_mgmt_key = wl_cfg80211_config_default_mgmt_key,
	.set_power_mgmt = wl_cfg80211_set_power_mgmt,
	.connect = wl_cfg80211_connect,
	.disconnect = wl_cfg80211_disconnect,
	.suspend = wl_cfg80211_suspend,
	.resume = wl_cfg80211_resume,
	.set_pmksa = wl_cfg80211_set_pmksa,
	.del_pmksa = wl_cfg80211_del_pmksa,
	.flush_pmksa = wl_cfg80211_flush_pmksa,
	.remain_on_channel = wl_cfg80211_remain_on_channel,
	.cancel_remain_on_channel = wl_cfg80211_cancel_remain_on_channel,
	.mgmt_tx = wl_cfg80211_mgmt_tx,
	.mgmt_frame_register = wl_cfg80211_mgmt_frame_register,
	.change_bss = wl_cfg80211_change_bss,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0))
	.set_channel = wl_cfg80211_set_channel,
#endif 
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 0))
	.set_beacon = wl_cfg80211_add_set_beacon,
	.add_beacon = wl_cfg80211_add_set_beacon,
	.del_beacon = wl_cfg80211_del_beacon,
#else
	.change_beacon = wl_cfg80211_change_beacon,
	.start_ap = wl_cfg80211_start_ap,
	.stop_ap = wl_cfg80211_stop_ap,
#endif 
#ifdef WL_SCHED_SCAN
	.sched_scan_start = wl_cfg80211_sched_scan_start,
	.sched_scan_stop = wl_cfg80211_sched_scan_stop,
#endif /* WL_SCHED_SCAN */
#if defined(WL_SUPPORT_BACKPORTED_KPATCHES) || (LINUX_VERSION_CODE >= KERNEL_VERSION(3, \
	2, 0))
	.del_station = wl_cfg80211_del_station,
	.change_station = wl_cfg80211_change_station,
	.mgmt_tx_cancel_wait = wl_cfg80211_mgmt_tx_cancel_wait,
#endif /* WL_SUPPORT_BACKPORTED_KPATCHES || KERNEL_VERSION >= (3,2,0) */
#ifdef WLMESH_CFG80211
	.join_mesh = wl_cfg80211_join_mesh,
	.leave_mesh = wl_cfg80211_leave_mesh,
#endif /* WLMESH_CFG80211 */
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 2, 0))
	.tdls_mgmt = wl_cfg80211_tdls_mgmt,
	.tdls_oper = wl_cfg80211_tdls_oper,
#endif 
#ifdef WL_SUPPORT_ACS
	.dump_survey = wl_cfg80211_dump_survey,
#endif /* WL_SUPPORT_ACS */
#ifdef WL_CFG80211_ACL
	.set_mac_acl = wl_cfg80211_set_mac_acl,
#endif /* WL_CFG80211_ACL */
#ifdef GTK_OFFLOAD_SUPPORT
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0))
	.set_rekey_data = wl_cfg80211_set_rekey_data,
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0) */
#endif /* GTK_OFFLOAD_SUPPORT */
};

s32 wl_mode_to_nl80211_iftype(s32 mode)
{
	s32 err = 0;

	switch (mode) {
	case WL_MODE_BSS:
		return NL80211_IFTYPE_STATION;
	case WL_MODE_IBSS:
		return NL80211_IFTYPE_ADHOC;
	case WL_MODE_AP:
		return NL80211_IFTYPE_AP;
#ifdef WLMESH_CFG80211
	case WL_MODE_MESH:
		return NL80211_IFTYPE_MESH_POINT;
#endif
	default:
		return NL80211_IFTYPE_UNSPECIFIED;
	}

	return err;
}

#ifdef CONFIG_CFG80211_INTERNAL_REGDB
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 9, 0))
#define WL_CFG80211_REG_NOTIFIER() static int wl_cfg80211_reg_notifier(struct wiphy *wiphy, struct regulatory_request *request)
#else
#define WL_CFG80211_REG_NOTIFIER() static void wl_cfg80211_reg_notifier(struct wiphy *wiphy, struct regulatory_request *request)
#endif /* kernel version < 3.9.0 */
#endif

#ifdef CONFIG_CFG80211_INTERNAL_REGDB
WL_CFG80211_REG_NOTIFIER()
{
	struct bcm_cfg80211 *cfg = (struct bcm_cfg80211 *)wiphy_priv(wiphy);
	int ret = 0;
	int revinfo = -1;

	if (!request || !cfg) {
		WL_ERR(("Invalid arg\n"));
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 10, 11))
		return -EINVAL;
#else
		return;
#endif /* kernel version < 3.10.11 */
	}

	WL_DBG(("ccode: %c%c Initiator: %d\n",
		request->alpha2[0], request->alpha2[1], request->initiator));

	/* We support only REGDOM_SET_BY_USER as of now */
	if ((request->initiator != NL80211_REGDOM_SET_BY_USER) &&
		(request->initiator != NL80211_REGDOM_SET_BY_COUNTRY_IE)) {
		WL_ERR(("reg_notifier for intiator:%d not supported : set default\n",
			request->initiator));
		/* in case of no supported country by regdb
		     lets driver setup platform default Locale
		*/
	}

	WL_ERR(("Set country code %c%c from %s\n",
		request->alpha2[0], request->alpha2[1],
		((request->initiator == NL80211_REGDOM_SET_BY_COUNTRY_IE) ? " 11d AP" : "User")));

	if ((ret = wldev_set_country(bcmcfg_to_prmry_ndev(cfg), request->alpha2,
		false, (request->initiator == NL80211_REGDOM_SET_BY_USER ? true : false),
		revinfo)) < 0) {
		WL_ERR(("set country Failed :%d\n", ret));
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 10, 11))
	return ret;
#else
	return;
#endif /* kernel version < 3.10.11 */
}
#endif /* CONFIG_CFG80211_INTERNAL_REGDB */

#ifdef CONFIG_PM
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
static const struct wiphy_wowlan_support brcm_wowlan_support = {
	.flags = WIPHY_WOWLAN_ANY,
	.n_patterns = WL_WOWLAN_MAX_PATTERNS,
	.pattern_min_len = WL_WOWLAN_MIN_PATTERN_LEN,
	.pattern_max_len = WL_WOWLAN_MAX_PATTERN_LEN,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0))
	.max_pkt_offset = WL_WOWLAN_MAX_PATTERN_LEN,
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0) */
};
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0) */
#endif /* CONFIG_PM */

static s32 wl_setup_wiphy(struct wireless_dev *wdev, struct device *sdiofunc_dev, dhd_pub_t *context)
{
	s32 err = 0;
#ifdef CONFIG_PM
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0))
	struct cfg80211_wowlan *brcm_wowlan_config = NULL;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0) */
#endif /* CONFIG_PM */

//#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0))
	dhd_pub_t *dhd = (dhd_pub_t *)context;
	BCM_REFERENCE(dhd);

	if (!dhd) {
		WL_ERR(("DHD is NULL!!"));
		err = -ENODEV;
		return err;
	}
//#endif 

	wdev->wiphy =
	    wiphy_new(&wl_cfg80211_ops, sizeof(struct bcm_cfg80211));
	if (unlikely(!wdev->wiphy)) {
		WL_ERR(("Couldn not allocate wiphy device\n"));
		err = -ENOMEM;
		return err;
	}
	set_wiphy_dev(wdev->wiphy, sdiofunc_dev);
	wdev->wiphy->max_scan_ie_len = WL_SCAN_IE_LEN_MAX;
	/* Report  how many SSIDs Driver can support per Scan request */
	wdev->wiphy->max_scan_ssids = WL_SCAN_PARAMS_SSID_MAX;
	wdev->wiphy->max_num_pmkids = WL_NUM_PMKIDS_MAX;
#ifdef WL_SCHED_SCAN
	wdev->wiphy->max_sched_scan_ssids = MAX_PFN_LIST_COUNT;
	wdev->wiphy->max_match_sets = MAX_PFN_LIST_COUNT;
	wdev->wiphy->max_sched_scan_ie_len = WL_SCAN_IE_LEN_MAX;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0))
	wdev->wiphy->max_sched_scan_plan_interval = PNO_SCAN_MAX_FW_SEC;
#else
	wdev->wiphy->flags |= WIPHY_FLAG_SUPPORTS_SCHED_SCAN;
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)) */
#endif /* WL_SCHED_SCAN */
#ifdef WLMESH_CFG80211
	wdev->wiphy->flags |= WIPHY_FLAG_MESH_AUTH;
#endif
	wdev->wiphy->interface_modes =
		BIT(NL80211_IFTYPE_STATION)
		| BIT(NL80211_IFTYPE_ADHOC)
#if !defined(WL_ENABLE_P2P_IF) && !defined(WL_CFG80211_P2P_DEV_IF)
		| BIT(NL80211_IFTYPE_MONITOR)
#endif /* !WL_ENABLE_P2P_IF && !WL_CFG80211_P2P_DEV_IF */
#if defined(WL_IFACE_COMB_NUM_CHANNELS) || defined(WL_CFG80211_P2P_DEV_IF)
		| BIT(NL80211_IFTYPE_P2P_CLIENT)
		| BIT(NL80211_IFTYPE_P2P_GO)
#endif /* WL_IFACE_COMB_NUM_CHANNELS || WL_CFG80211_P2P_DEV_IF */
#if defined(WL_CFG80211_P2P_DEV_IF)
		| BIT(NL80211_IFTYPE_P2P_DEVICE)
#endif /* WL_CFG80211_P2P_DEV_IF */
#ifdef WLMESH_CFG80211
		| BIT(NL80211_IFTYPE_MESH_POINT)
#endif /* WLMESH_CFG80211 */
		| BIT(NL80211_IFTYPE_AP);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)) && \
	(defined(WL_IFACE_COMB_NUM_CHANNELS) || defined(WL_CFG80211_P2P_DEV_IF))
	WL_DBG(("Setting interface combinations for common mode\n"));
	wdev->wiphy->iface_combinations = common_iface_combinations;
	wdev->wiphy->n_iface_combinations =
		ARRAY_SIZE(common_iface_combinations);
#endif /* LINUX_VER >= 3.0 && (WL_IFACE_COMB_NUM_CHANNELS || WL_CFG80211_P2P_DEV_IF) */

	wdev->wiphy->bands[IEEE80211_BAND_2GHZ] = &__wl_band_2ghz;

	wdev->wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;
	wdev->wiphy->cipher_suites = __wl_cipher_suites;
	wdev->wiphy->n_cipher_suites = ARRAY_SIZE(__wl_cipher_suites);
	wdev->wiphy->max_remain_on_channel_duration = 5000;
	wdev->wiphy->mgmt_stypes = wl_cfg80211_default_mgmt_stypes;
#ifndef WL_POWERSAVE_DISABLED
	wdev->wiphy->flags |= WIPHY_FLAG_PS_ON_BY_DEFAULT;
#else
	wdev->wiphy->flags &= ~WIPHY_FLAG_PS_ON_BY_DEFAULT;
#endif				/* !WL_POWERSAVE_DISABLED */
	wdev->wiphy->flags |= WIPHY_FLAG_NETNS_OK |
		WIPHY_FLAG_4ADDR_AP |
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 39))
		WIPHY_FLAG_SUPPORTS_SEPARATE_DEFAULT_KEYS |
#endif
		WIPHY_FLAG_4ADDR_STATION;
#if ((defined(ROAM_ENABLE) || defined(BCMFW_ROAM_ENABLE)) && (LINUX_VERSION_CODE >= \
	KERNEL_VERSION(3, 2, 0)))
	/*
	 * If FW ROAM flag is advertised, upper layer wouldn't provide
	 * the bssid & freq in the connect command. This will result a
	 * delay in initial connection time due to firmware doing a full
	 * channel scan to figure out the channel & bssid. However kernel
	 * ver >= 3.15, provides bssid_hint & freq_hint and hence kernel
	 * ver >= 3.15 won't have any issue. So if this flags need to be
	 * advertised for kernel < 3.15, suggest to use RCC along with it
	 * to avoid the initial connection delay.
	 */
	wdev->wiphy->flags |= WIPHY_FLAG_SUPPORTS_FW_ROAM;
#endif 
#ifdef UNSET_FW_ROAM_WIPHY_FLAG
	wdev->wiphy->flags &= ~WIPHY_FLAG_SUPPORTS_FW_ROAM;
#endif /* UNSET_FW_ROAM_WIPHY_FLAG */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0))
	wdev->wiphy->flags |= WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL |
		WIPHY_FLAG_OFFCHAN_TX;
#endif
#if defined(WL_SUPPORT_BACKPORTED_KPATCHES) || (LINUX_VERSION_CODE >= KERNEL_VERSION(3, \
	4, 0))
	/* From 3.4 kernel ownards AP_SME flag can be advertised
	 * to remove the patch from supplicant
	 */
	wdev->wiphy->flags |= WIPHY_FLAG_HAVE_AP_SME;

#ifdef WL_CFG80211_ACL
	/* Configure ACL capabilities. */
	wdev->wiphy->max_acl_mac_addrs = MAX_NUM_MAC_FILT;
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0))
	/* Supplicant distinguish between the SoftAP mode and other
	 * modes (e.g. P2P, WPS, HS2.0) when it builds the probe
	 * response frame from Supplicant MR1 and Kernel 3.4.0 or
	 * later version. To add Vendor specific IE into the
	 * probe response frame in case of SoftAP mode,
	 * AP_PROBE_RESP_OFFLOAD flag is set to wiphy->flags variable.
	 */
	if (dhd_get_fw_mode(dhd->info) == DHD_FLAG_HOSTAP_MODE) {
		wdev->wiphy->flags |= WIPHY_FLAG_AP_PROBE_RESP_OFFLOAD;
		wdev->wiphy->probe_resp_offload = 0;
	}
#endif 
#endif /* WL_SUPPORT_BACKPORTED_KPATCHES) || (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)) */

#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 2, 0))
	wdev->wiphy->flags |= WIPHY_FLAG_SUPPORTS_TDLS;
#endif

#if defined(CONFIG_PM) && defined(WL_CFG80211_P2P_DEV_IF)
	/*
	 * From linux-3.10 kernel, wowlan packet filter is mandated to avoid the
	 * disconnection of connected network before suspend. So a dummy wowlan
	 * filter is configured for kernels linux-3.8 and above.
	 */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0))
	wdev->wiphy->wowlan = &brcm_wowlan_support;
	/* If this is not provided cfg stack will get disconnect
	* during suspend.
	*/
	brcm_wowlan_config = kmalloc(sizeof(struct cfg80211_wowlan), GFP_KERNEL);
	if (brcm_wowlan_config) {
		brcm_wowlan_config->disconnect = true;
		brcm_wowlan_config->gtk_rekey_failure = true;
		brcm_wowlan_config->eap_identity_req = true;
		brcm_wowlan_config->four_way_handshake = true;
		brcm_wowlan_config->patterns = NULL;
		brcm_wowlan_config->n_patterns = 0;
		brcm_wowlan_config->tcp = NULL;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0))
		brcm_wowlan_config->nd_config = NULL;
#endif
	} else {
		WL_ERR(("Can not allocate memory for brcm_wowlan_config,"
			" So wiphy->wowlan_config is set to NULL\n"));
	}
	wdev->wiphy->wowlan_config = brcm_wowlan_config;
#else
	wdev->wiphy->wowlan.flags = WIPHY_WOWLAN_ANY;
	wdev->wiphy->wowlan.n_patterns = WL_WOWLAN_MAX_PATTERNS;
	wdev->wiphy->wowlan.pattern_min_len = WL_WOWLAN_MIN_PATTERN_LEN;
	wdev->wiphy->wowlan.pattern_max_len = WL_WOWLAN_MAX_PATTERN_LEN;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0))
	wdev->wiphy->wowlan.max_pkt_offset = WL_WOWLAN_MAX_PATTERN_LEN;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0) */
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0) */
#endif /* CONFIG_PM && WL_CFG80211_P2P_DEV_IF */

	WL_DBG(("Registering custom regulatory)\n"));
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
	wdev->wiphy->regulatory_flags |= REGULATORY_CUSTOM_REG;
#else
	wdev->wiphy->flags |= WIPHY_FLAG_CUSTOM_REGULATORY;
#endif
	wiphy_apply_custom_regulatory(wdev->wiphy, &brcm_regdom);

#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 14, 0)) || defined(WL_VENDOR_EXT_SUPPORT)
	WL_ERR(("Registering Vendor80211\n"));
	err = wl_cfgvendor_attach(wdev->wiphy, dhd);
	if (unlikely(err < 0)) {
		WL_ERR(("Couldn not attach vendor commands (%d)\n", err));
	}
#endif /* (LINUX_VERSION_CODE > KERNEL_VERSION(3, 14, 0)) || defined(WL_VENDOR_EXT_SUPPORT) */

	/* Now we can register wiphy with cfg80211 module */
	err = wiphy_register(wdev->wiphy);
	if (unlikely(err < 0)) {
		WL_ERR(("Couldn not register wiphy device (%d)\n", err));
		wiphy_free(wdev->wiphy);
	}

#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)) && (LINUX_VERSION_CODE <= \
	KERNEL_VERSION(3, 3, 0))) && defined(WL_IFACE_COMB_NUM_CHANNELS)
	wdev->wiphy->flags &= ~WIPHY_FLAG_ENFORCE_COMBINATIONS;
#endif

#ifdef WL_SAE
	WL_ERR(("SAE support\n"));
	wdev->wiphy->features |= NL80211_FEATURE_SAE;
#endif /* WL_SAE */

	return err;
}

static void wl_free_wdev(struct bcm_cfg80211 *cfg)
{
	struct wireless_dev *wdev = cfg->wdev;
	struct wiphy *wiphy = NULL;
	if (!wdev) {
		WL_ERR(("wdev is invalid\n"));
		return;
	}
	if (wdev->wiphy) {
		wiphy = wdev->wiphy;

#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 14, 0)) || defined(WL_VENDOR_EXT_SUPPORT)
		wl_cfgvendor_detach(wdev->wiphy);
#endif /* (LINUX_VERSION_CODE > KERNEL_VERSION(3, 14, 0)) || defined(WL_VENDOR_EXT_SUPPORT) */
#if defined(CONFIG_PM) && defined(WL_CFG80211_P2P_DEV_IF)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0))
		/* Reset wowlan & wowlan_config before Unregister to avoid  Kernel Panic */
		WL_DBG(("wl_free_wdev Clearing wowlan Config \n"));
		wdev->wiphy->wowlan = NULL;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0) */
#endif /* CONFIG_PM && WL_CFG80211_P2P_DEV_IF */
		wiphy_unregister(wdev->wiphy);
		wdev->wiphy->dev.parent = NULL;
		wdev->wiphy = NULL;
	}

	wl_delete_all_netinfo(cfg);
	if (wiphy)
		wiphy_free(wiphy);

	/* PLEASE do NOT call any function after wiphy_free, the driver's private structure "cfg",
	 * which is the private part of wiphy, has been freed in wiphy_free !!!!!!!!!!!
	 */
}

static s32 wl_inform_bss(struct bcm_cfg80211 *cfg)
{
	struct wl_scan_results *bss_list;
	struct wl_bss_info *bi = NULL;	/* must be initialized */
	s32 err = 0;
	s32 i;
	struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);
#if defined(RSSIAVG)
	int rssi;
#endif
#if defined(BSSCACHE)
	wl_bss_cache_t *node;
#endif

	bss_list = cfg->bss_list;

	/* Free cache in p2p scanning*/
	if (p2p_is_on(cfg) && p2p_scan(cfg)) {
#if defined(RSSIAVG)
		wl_free_rssi_cache(&cfg->g_rssi_cache_ctrl);
#endif
#if defined(BSSCACHE)
		wl_free_bss_cache(&cfg->g_bss_cache_ctrl);
#endif
	}

	/* Delete disconnected cache */
#if defined(BSSCACHE)
	wl_delete_disconnected_bss_cache(&cfg->g_bss_cache_ctrl, (u8*)&cfg->disconnected_bssid);
#if defined(RSSIAVG)
	wl_delete_disconnected_rssi_cache(&cfg->g_rssi_cache_ctrl, (u8*)&cfg->disconnected_bssid);
#endif
	if (cfg->p2p_disconnected == 0)
		memset(&cfg->disconnected_bssid, 0, ETHER_ADDR_LEN);
#endif

	/* Update cache */
#if defined(RSSIAVG)
	wl_update_rssi_cache(&cfg->g_rssi_cache_ctrl, bss_list);
	if (!in_atomic())
		wl_update_connected_rssi_cache(ndev, &cfg->g_rssi_cache_ctrl, &rssi);
#endif
#if defined(BSSCACHE)
	wl_update_bss_cache(&cfg->g_bss_cache_ctrl,
#if defined(RSSIAVG)
		&cfg->g_rssi_cache_ctrl,
#endif
		bss_list);
#endif

	/* delete dirty cache */
#if defined(RSSIAVG)
	wl_delete_dirty_rssi_cache(&cfg->g_rssi_cache_ctrl);
	wl_reset_rssi_cache(&cfg->g_rssi_cache_ctrl);
#endif
#if defined(BSSCACHE)
	wl_delete_dirty_bss_cache(&cfg->g_bss_cache_ctrl);
	wl_reset_bss_cache(&cfg->g_bss_cache_ctrl);
#endif

#if defined(BSSCACHE)
	if (cfg->p2p_disconnected > 0) {
		// terence 20130703: Fix for wrong group_capab (timing issue)
		wl_delete_disconnected_bss_cache(&cfg->g_bss_cache_ctrl, (u8*)&cfg->disconnected_bssid);
#if defined(RSSIAVG)
		wl_delete_disconnected_rssi_cache(&cfg->g_rssi_cache_ctrl, (u8*)&cfg->disconnected_bssid);
#endif
	}
	WL_SCAN(("scanned AP count (%d)\n", bss_list->count));
	node = cfg->g_bss_cache_ctrl.m_cache_head;
	for (i=0; node && i<WL_AP_MAX; i++) {
		bi = node->results.bss_info;
		err = wl_inform_single_bss(cfg, bi, false);
		node = node->next;
	}
	if (cfg->autochannel)
		wl_ext_get_best_channel(ndev, &cfg->g_bss_cache_ctrl, ioctl_version,
			&cfg->best_2g_ch, &cfg->best_5g_ch);
#else
	WL_SCAN(("scanned AP count (%d)\n", bss_list->count));
	preempt_disable();
	bi = next_bss(bss_list, bi);
	for_each_bss(bss_list, bi, i) {
		if (cfg->p2p_disconnected > 0 && !memcmp(&bi->BSSID, &cfg->disconnected_bssid, ETHER_ADDR_LEN))
			continue;
		err = wl_inform_single_bss(cfg, bi, false);
	}
	preempt_enable();
	if (cfg->autochannel)
		wl_ext_get_best_channel(ndev, bss_list, ioctl_version,
			&cfg->best_2g_ch, &cfg->best_5g_ch);
#endif

	if (cfg->p2p_disconnected > 0) {
		// terence 20130703: Fix for wrong group_capab (timing issue)
		cfg->p2p_disconnected++;
		if (cfg->p2p_disconnected >= REPEATED_SCAN_RESULT_CNT+1) {
			cfg->p2p_disconnected = 0;
			memset(&cfg->disconnected_bssid, 0, ETHER_ADDR_LEN);
		}
	}

	return err;
}

static s32 wl_inform_single_bss(struct bcm_cfg80211 *cfg, struct wl_bss_info *bi, bool roam)
{
	struct wiphy *wiphy = bcmcfg_to_wiphy(cfg);
	struct ieee80211_mgmt *mgmt;
	struct ieee80211_channel *channel;
	struct ieee80211_supported_band *band;
	struct wl_cfg80211_bss_info *notif_bss_info;
	struct wl_scan_req *sr = wl_to_sr(cfg);
	struct beacon_proberesp *beacon_proberesp;
	struct cfg80211_bss *cbss = NULL;
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
	log_conn_event_t *event_data = NULL;
	tlv_log *tlv_data = NULL;
	u32 alloc_len, tlv_len;
	u32 payload_len;
	s32 mgmt_type;
	s32 signal;
	u32 freq;
	s32 err = 0;
	gfp_t aflags;
	chanspec_t chanspec;

	if (unlikely(dtoh32(bi->length) > WL_BSS_INFO_MAX)) {
		WL_DBG(("Beacon is larger than buffer. Discarding\n"));
		return err;
	}
	aflags = (in_atomic()) ? GFP_ATOMIC : GFP_KERNEL;
	notif_bss_info = kzalloc(sizeof(*notif_bss_info) + sizeof(*mgmt)
		- sizeof(u8) + WL_BSS_INFO_MAX, aflags);
	if (unlikely(!notif_bss_info)) {
		WL_ERR(("notif_bss_info alloc failed\n"));
		return -ENOMEM;
	}
	mgmt = (struct ieee80211_mgmt *)notif_bss_info->frame_buf;
	chanspec = wl_chspec_driver_to_host(bi->chanspec);
	notif_bss_info->channel = wf_chspec_ctlchan(chanspec);

	if (notif_bss_info->channel <= CH_MAX_2G_CHANNEL)
		band = wiphy->bands[IEEE80211_BAND_2GHZ];
	else
		band = wiphy->bands[IEEE80211_BAND_5GHZ];
	if (!band) {
		WL_ERR(("No valid band\n"));
		kfree(notif_bss_info);
		return -EINVAL;
	}
	notif_bss_info->rssi = dtoh16(bi->RSSI);
#if defined(RSSIAVG)
	notif_bss_info->rssi = wl_get_avg_rssi(&cfg->g_rssi_cache_ctrl, &bi->BSSID);
	if (notif_bss_info->rssi == RSSI_MINVAL)
		notif_bss_info->rssi = MIN(dtoh16(bi->RSSI), RSSI_MAXVAL);
#endif
#if defined(RSSIOFFSET)
	notif_bss_info->rssi = wl_update_rssi_offset(bcmcfg_to_prmry_ndev(cfg), notif_bss_info->rssi);
#endif
#if !defined(RSSIAVG) && !defined(RSSIOFFSET)
	// terence 20150419: limit the max. rssi to -2 or the bss will be filtered out in android OS
	notif_bss_info->rssi = MIN(notif_bss_info->rssi, RSSI_MAXVAL);
#endif
	memcpy(mgmt->bssid, &bi->BSSID, ETHER_ADDR_LEN);
	mgmt_type = cfg->active_scan ?
		IEEE80211_STYPE_PROBE_RESP : IEEE80211_STYPE_BEACON;
	if (!memcmp(bi->SSID, sr->ssid.SSID, bi->SSID_len)) {
	    mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT | mgmt_type);
	}
	beacon_proberesp = cfg->active_scan ?
		(struct beacon_proberesp *)&mgmt->u.probe_resp :
		(struct beacon_proberesp *)&mgmt->u.beacon;
	beacon_proberesp->timestamp = 0;
	beacon_proberesp->beacon_int = cpu_to_le16(bi->beacon_period);
	beacon_proberesp->capab_info = cpu_to_le16(bi->capability);
	wl_rst_ie(cfg);
	wl_update_hidden_ap_ie(bi, ((u8 *) bi) + bi->ie_offset, &bi->ie_length, roam);
	wl_mrg_ie(cfg, ((u8 *) bi) + bi->ie_offset, bi->ie_length);
	wl_cp_ie(cfg, beacon_proberesp->variable, WL_BSS_INFO_MAX -
		offsetof(struct wl_cfg80211_bss_info, frame_buf));
	notif_bss_info->frame_len = offsetof(struct ieee80211_mgmt,
		u.beacon.variable) + wl_get_ielen(cfg);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 39)
	freq = ieee80211_channel_to_frequency(notif_bss_info->channel);
	(void)band->band;
#else
	freq = ieee80211_channel_to_frequency(notif_bss_info->channel, band->band);
#endif
	if (freq == 0) {
		WL_ERR(("Invalid channel, fail to change channel to freq\n"));
		kfree(notif_bss_info);
		return -EINVAL;
	}
	channel = ieee80211_get_channel(wiphy, freq);
	WL_SCAN(("BSSID %pM, channel %3d(%3d %sMHz), rssi %3d, capa 0x04%x, mgmt_type %d, "
		"frame_len %d, SSID \"%s\"\n",
		&bi->BSSID, notif_bss_info->channel, CHSPEC_CHANNEL(chanspec),
		CHSPEC_IS20(chanspec)?"20":
		CHSPEC_IS40(chanspec)?"40":
		CHSPEC_IS80(chanspec)?"80":"160",
		notif_bss_info->rssi, mgmt->u.beacon.capab_info, mgmt_type,
		notif_bss_info->frame_len, bi->SSID));
	if (unlikely(!channel)) {
		WL_ERR(("ieee80211_get_channel error, freq=%d, channel=%d\n",
			freq, notif_bss_info->channel));
		kfree(notif_bss_info);
		return -EINVAL;
	}

	signal = notif_bss_info->rssi * 100;
	if (!mgmt->u.probe_resp.timestamp) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 39))
		struct osl_timespec ts;
		osl_get_monotonic_boottime(&ts);
		mgmt->u.probe_resp.timestamp = ((u64)ts.tv_sec*1000000)
				+ ts.tv_nsec / 1000;
#else
		struct timeval tv;
		do_gettimeofday(&tv);
		mgmt->u.probe_resp.timestamp = ((u64)tv.tv_sec*1000000)
				+ tv.tv_usec;
#endif
	}


	cbss = cfg80211_inform_bss_frame(wiphy, channel, mgmt,
		le16_to_cpu(notif_bss_info->frame_len), signal, aflags);
	if (unlikely(!cbss)) {
		WL_ERR(("cfg80211_inform_bss_frame error\n"));
		err = -EINVAL;
		goto out_err;
	}

	CFG80211_PUT_BSS(wiphy, cbss);

	if (DBG_RING_ACTIVE(dhdp, DHD_EVENT_RING_ID) &&
			(cfg->sched_scan_req && !cfg->scan_request)) {
		alloc_len = sizeof(log_conn_event_t) + IEEE80211_MAX_SSID_LEN + sizeof(uint16) +
			sizeof(int16);
		event_data = MALLOCZ(dhdp->osh, alloc_len);
		if (!event_data) {
			WL_ERR(("%s: failed to allocate the log_conn_event_t with "
				"length(%d)\n", __func__, alloc_len));
			goto out_err;
		}
		tlv_len = 3 * sizeof(tlv_log);
		event_data->tlvs = MALLOC(dhdp->osh, tlv_len);
		if (!event_data->tlvs) {
			WL_ERR(("%s: failed to allocate the log_conn_event_t with "
						"length(%d)\n", __func__, tlv_len));
			goto out_err;
		}

		payload_len = sizeof(log_conn_event_t);
		event_data->event = WIFI_EVENT_DRIVER_PNO_SCAN_RESULT_FOUND;
		tlv_data = event_data->tlvs;

		/* ssid */
		tlv_data->tag = WIFI_TAG_SSID;
		tlv_data->len = bi->SSID_len;
		memcpy(tlv_data->value, bi->SSID, bi->SSID_len);
		payload_len += TLV_LOG_SIZE(tlv_data);
		tlv_data = TLV_LOG_NEXT(tlv_data);

		/* channel */
		tlv_data->tag = WIFI_TAG_CHANNEL;
		tlv_data->len = sizeof(uint16);
		memcpy(tlv_data->value, &notif_bss_info->channel, sizeof(uint16));
		payload_len += TLV_LOG_SIZE(tlv_data);
		tlv_data = TLV_LOG_NEXT(tlv_data);

		/* rssi */
		tlv_data->tag = WIFI_TAG_RSSI;
		tlv_data->len = sizeof(int16);
		memcpy(tlv_data->value, &notif_bss_info->rssi, sizeof(int16));
		payload_len += TLV_LOG_SIZE(tlv_data);
		tlv_data = TLV_LOG_NEXT(tlv_data);

		dhd_os_push_push_ring_data(dhdp, DHD_EVENT_RING_ID,
				event_data, payload_len);
		MFREE(dhdp->osh, event_data->tlvs, tlv_len);
		MFREE(dhdp->osh, event_data, alloc_len);
	}

out_err:
	kfree(notif_bss_info);
	return err;
}

static bool wl_is_linkup(struct bcm_cfg80211 *cfg, const wl_event_msg_t *e, struct net_device *ndev)
{
	u32 event = ntoh32(e->event_type);
	u32 status =  ntoh32(e->status);
	u16 flags = ntoh16(e->flags);
#if defined(CUSTOM_SET_ANTNPM) || defined(CUSTOM_SET_OCLOFF)
	dhd_pub_t *dhd;
	dhd = (dhd_pub_t *)(cfg->pub);
#endif /* CUSTOM_SET_ANTNPM */

	WL_DBG(("event %d, status %d flags %x\n", event, status, flags));
	if (event == WLC_E_SET_SSID) {
		if (status == WLC_E_STATUS_SUCCESS) {
#ifdef CUSTOM_SET_ANTNPM
			if (dhd->mimo_ant_set) {
				int err = 0;

				WL_ERR(("[WIFI_SEC] mimo_ant_set = %d\n", dhd->mimo_ant_set));
				err = wldev_iovar_setint(ndev, "txchain", dhd->mimo_ant_set);
				if (err != 0) {
					WL_ERR(("[WIFI_SEC] Fail set txchain\n"));
				}
				err = wldev_iovar_setint(ndev, "rxchain", dhd->mimo_ant_set);
				if (err != 0) {
					WL_ERR(("[WIFI_SEC] Fail set rxchain\n"));
				}
			}
#endif /* CUSTOM_SET_ANTNPM */
#ifdef CUSTOM_SET_OCLOFF
			if (dhd->ocl_off) {
				int err = 0;
				int ocl_enable = 0;
				err = wldev_iovar_setint(ndev, "ocl_enable", ocl_enable);
				if (err != 0) {
					WL_ERR(("[WIFI_SEC] %s: Set ocl_enable %d failed %d\n",
						__FUNCTION__, ocl_enable, err));
				} else {
					WL_ERR(("[WIFI_SEC] %s: Set ocl_enable %d succeeded %d\n",
						__FUNCTION__, ocl_enable, err));
				}
			}
#endif /* CUSTOM_SET_OCLOFF */
			if (!wl_is_ibssmode(cfg, ndev))
				return true;
		}
	} else if (event == WLC_E_LINK) {
		if (flags & WLC_EVENT_MSG_LINK)
			return true;
	}

	WL_DBG(("wl_is_linkup false\n"));
	return false;
}

#ifdef WL_LASTEVT
static bool wl_is_linkdown(struct bcm_cfg80211 *cfg, const wl_event_msg_t *e, void *data)
{
	u32 event = ntoh32(e->event_type);
	u16 flags = ntoh16(e->flags);
	wl_last_event_t *last_event = (wl_last_event_t *)data;
	u32 len = ntoh32(e->datalen);

	if (event == WLC_E_DEAUTH_IND ||
	event == WLC_E_DISASSOC_IND ||
	event == WLC_E_DISASSOC ||
	event == WLC_E_DEAUTH) {
		WL_ERR(("Link down Reason : %s\n", bcmevent_get_name(event)));
		return true;
	} else if (event == WLC_E_LINK) {
		if (!(flags & WLC_EVENT_MSG_LINK)) {
			if (last_event && len > 0) {
				u32 current_time = last_event->current_time;
				u32 timestamp = last_event->timestamp;
				u32 event_type = last_event->event.event_type;
				u32 status = last_event->event.status;
				u32 reason = last_event->event.reason;

				WL_ERR(("Last roam event before disconnection : current_time %d,"
					" time %d, type %d, status %d, reason %d\n",
					current_time, timestamp, event_type, status, reason));
			}
			WL_ERR(("Link down Reason : %s\n", bcmevent_get_name(event)));
			return true;
		}
	}

	return false;
}
#else
static bool wl_is_linkdown(struct bcm_cfg80211 *cfg, const wl_event_msg_t *e)
{
	u32 event = ntoh32(e->event_type);
	u16 flags = ntoh16(e->flags);

	if (event == WLC_E_DEAUTH_IND ||
	event == WLC_E_DISASSOC_IND ||
	event == WLC_E_DISASSOC ||
	event == WLC_E_DEAUTH) {
		WL_ERR(("Link down Reason : %s\n", bcmevent_get_name(event)));
		return true;
	} else if (event == WLC_E_LINK) {
		if (!(flags & WLC_EVENT_MSG_LINK)) {
			WL_ERR(("Link down Reason : %s\n", bcmevent_get_name(event)));
			return true;
		}
	}

	return false;
}
#endif /* WL_LASTEVT */

static bool wl_is_nonetwork(struct bcm_cfg80211 *cfg, const wl_event_msg_t *e)
{
	u32 event = ntoh32(e->event_type);
	u32 status = ntoh32(e->status);

	if (event == WLC_E_LINK && status == WLC_E_STATUS_NO_NETWORKS)
		return true;
	if (event == WLC_E_SET_SSID && status != WLC_E_STATUS_SUCCESS)
		return true;

	return false;
}

#ifdef WL_SAE
static s32
wl_cfg80211_event_sae_key(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	wl_sae_key_info_t *sae_key)
{
	struct sk_buff *skb;
	gfp_t kflags;
	struct wiphy *wiphy = bcmcfg_to_wiphy(cfg);
	int err = BCME_OK;

	kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;
#if (defined(CONFIG_ARCH_MSM) && defined(SUPPORT_WDEV_CFG80211_VENDOR_EVENT_ALLOC)) || \
	LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)
	skb = cfg80211_vendor_event_alloc(wiphy, ndev_to_wdev(ndev), BRCM_SAE_VENDOR_EVENT_BUF_LEN,
		BRCM_VENDOR_EVENT_SAE_KEY, kflags);
#else
	skb = cfg80211_vendor_event_alloc(wiphy, BRCM_SAE_VENDOR_EVENT_BUF_LEN,
		BRCM_VENDOR_EVENT_SAE_KEY, kflags);
#endif /* (defined(CONFIG_ARCH_MSM) && defined(SUPPORT_WDEV_CFG80211_VENDOR_EVENT_ALLOC)) || */
		/* LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0) */
	if (!skb) {
		WL_ERR(("skb alloc failed"));
		err = BCME_NOMEM;
		goto done;
	}

	WL_INFORM(("Received Sae Key event for "MACDBG" key length %x %x",
		MAC2STRDBG(sae_key->peer_mac), sae_key->pmk_len, sae_key->pmkid_len));
	nla_put(skb, BRCM_SAE_KEY_ATTR_PEER_MAC, ETHER_ADDR_LEN, sae_key->peer_mac);
	nla_put(skb, BRCM_SAE_KEY_ATTR_PMK, sae_key->pmk_len, sae_key->pmk);
	nla_put(skb, BRCM_SAE_KEY_ATTR_PMKID, sae_key->pmkid_len, sae_key->pmkid);
	cfg80211_vendor_event(skb, kflags);

done:
	return err;
}

static s32
wl_bss_handle_sae_auth(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *event, void *data)
{
	int err = BCME_OK;
	uint status = ntoh32(event->status);
	wl_auth_event_t *auth_data;
	wl_sae_key_info_t sae_key;
	uint16 tlv_buf_len;

	if (status == WLC_E_STATUS_SUCCESS) {
		auth_data = (wl_auth_event_t *)data;
		if (auth_data->version != WL_AUTH_EVENT_DATA_V1) {
			WL_ERR(("unknown auth event data version %x\n",
				auth_data->version));
			err = BCME_VERSION;
			goto done;
		}

		tlv_buf_len = auth_data->length - WL_AUTH_EVENT_FIXED_LEN_V1;

		/* check if PMK info present */
		sae_key.pmk = bcm_get_data_from_xtlv_buf(auth_data->xtlvs, tlv_buf_len,
			WL_AUTH_PMK_TLV_ID, &(sae_key.pmk_len), BCM_XTLV_OPTION_ALIGN32);
		if (!sae_key.pmk || !sae_key.pmk_len) {
			WL_ERR(("Mandatory PMK info not present"));
			err = BCME_NOTFOUND;
			goto done;
		}
		/* check if PMKID info present */
		sae_key.pmkid = bcm_get_data_from_xtlv_buf(auth_data->xtlvs, tlv_buf_len,
			WL_AUTH_PMKID_TLV_ID, &(sae_key.pmkid_len), BCM_XTLV_OPTION_ALIGN32);
		if (!sae_key.pmkid || !sae_key.pmkid_len) {
			WL_ERR(("Mandatory PMKID info not present\n"));
			err = BCME_NOTFOUND;
			goto done;
		}
		memcpy(sae_key.peer_mac, event->addr.octet, ETHER_ADDR_LEN);
		err = wl_cfg80211_event_sae_key(cfg, ndev, &sae_key);
		if (err) {
			WL_ERR(("Failed to event sae key info\n"));
		}
	} else {
		WL_ERR(("sae auth status failure:%d\n", status));
	}
done:
	return err;
}
#endif /* WL_SAE */

/* The mainline kernel >= 3.2.0 has support for indicating new/del station
 * to AP/P2P GO via events. If this change is backported to kernel for which
 * this driver is being built, then define WL_CFG80211_STA_EVENT. You
 * should use this new/del sta event mechanism for BRCM supplicant >= 22.
 */
static s32
wl_notify_connect_status_ap(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *e, void *data)
{
	s32 err = 0;
	u32 event = ntoh32(e->event_type);
	u32 reason = ntoh32(e->reason);
	u32 len = ntoh32(e->datalen);
	u32 status = ntoh32(e->status);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 2, 0)) && !defined(WL_CFG80211_STA_EVENT)
	bool isfree = false;
	u8 *mgmt_frame;
	u8 bsscfgidx = e->bsscfgidx;
	s32 freq;
	s32 channel;
	u8 *body = NULL;
	u16 fc = 0;

	struct ieee80211_supported_band *band;
	struct ether_addr da;
	struct ether_addr bssid;
	struct wiphy *wiphy = bcmcfg_to_wiphy(cfg);
	channel_info_t ci;
#else
	struct station_info sinfo;
#endif 

	WL_DBG(("event %d status %d reason %d\n", event, ntoh32(e->status), reason));
	/* if link down, bsscfg is disabled. */
	if (event == WLC_E_LINK && reason == WLC_E_LINK_BSSCFG_DIS &&
		wl_get_p2p_status(cfg, IF_DELETING) && (ndev != bcmcfg_to_prmry_ndev(cfg))) {
		wl_add_remove_eventmsg(ndev, WLC_E_PROBREQ_MSG, false);
		WL_MSG(ndev->name, "AP mode link down !! \n");
		complete(&cfg->iface_disable);
		return 0;
	}

	if ((event == WLC_E_LINK) && (status == WLC_E_STATUS_SUCCESS) &&
		(reason == WLC_E_REASON_INITIAL_ASSOC) &&
		(wl_get_mode_by_netdev(cfg, ndev) == WL_MODE_AP)) {
		if (!wl_get_drv_status(cfg, AP_CREATED, ndev)) {
			/* AP/GO brought up successfull in firmware */
			WL_MSG(ndev->name, "AP/GO Link up\n");
			wl_set_drv_status(cfg, AP_CREATED, ndev);
			wake_up_interruptible(&cfg->netif_change_event);
			wl_cfg80211_check_in4way(cfg, ndev, 0, WL_EXT_STATUS_AP_ENABLED, NULL);
			return 0;
		}
	}

	if (event == WLC_E_DISASSOC_IND || event == WLC_E_DEAUTH_IND || event == WLC_E_DEAUTH) {
		WL_MSG(ndev->name, "event %s(%d) status %d reason %d\n",
		bcmevent_get_name(event), event, ntoh32(e->status), reason);
	}
#ifdef WL_SAE
	if (event == WLC_E_AUTH_IND)
		wl_bss_handle_sae_auth(cfg, ndev, e, data);
#endif /* WL_SAE */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 2, 0)) && !defined(WL_CFG80211_STA_EVENT)
	WL_DBG(("Enter \n"));
	if (!len && (event == WLC_E_DEAUTH)) {
		len = 2; /* reason code field */
		data = &reason;
	}
	if (len) {
		body = kzalloc(len, GFP_KERNEL);

		if (body == NULL) {
			WL_ERR(("Failed to allocate body\n"));
			return WL_INVALID;
		}
	}
	memset(&bssid, 0, ETHER_ADDR_LEN);
	WL_DBG(("Enter event %d ndev %p\n", event, ndev));
	if (wl_get_mode_by_netdev(cfg, ndev) == WL_INVALID) {
		kfree(body);
		return WL_INVALID;
	}
	if (len)
		memcpy(body, data, len);

	wldev_iovar_getbuf_bsscfg(ndev, "cur_etheraddr",
		NULL, 0, cfg->ioctl_buf, WLC_IOCTL_SMLEN, bsscfgidx, &cfg->ioctl_buf_sync);
	memcpy(da.octet, cfg->ioctl_buf, ETHER_ADDR_LEN);
	memset(&bssid, 0, sizeof(bssid));
	err = wldev_ioctl_get(ndev, WLC_GET_BSSID, &bssid, ETHER_ADDR_LEN);
	switch (event) {
		case WLC_E_ASSOC_IND:
			fc = FC_ASSOC_REQ;
			break;
		case WLC_E_REASSOC_IND:
			fc = FC_REASSOC_REQ;
			break;
		case WLC_E_DISASSOC_IND:
			fc = FC_DISASSOC;
			break;
		case WLC_E_DEAUTH_IND:
			fc = FC_DISASSOC;
			break;
		case WLC_E_DEAUTH:
			fc = FC_DISASSOC;
			break;
		default:
			fc = 0;
			goto exit;
	}
	memset(&ci, 0, sizeof(ci));
	if ((err = wldev_ioctl_get(ndev, WLC_GET_CHANNEL, &ci, sizeof(ci)))) {
		kfree(body);
		return err;
	}

	channel = dtoh32(ci.hw_channel);
	if (channel <= CH_MAX_2G_CHANNEL)
		band = wiphy->bands[IEEE80211_BAND_2GHZ];
	else
		band = wiphy->bands[IEEE80211_BAND_5GHZ];
	if (!band) {
		WL_ERR(("No valid band\n"));
		if (body)
			kfree(body);
		return -EINVAL;
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 39)
	freq = ieee80211_channel_to_frequency(channel);
	(void)band->band;
#else
	freq = ieee80211_channel_to_frequency(channel, band->band);
#endif

	err = wl_frame_get_mgmt(fc, &da, &e->addr, &bssid,
		&mgmt_frame, &len, body);
	if (err < 0)
		goto exit;
	isfree = true;

	if ((event == WLC_E_ASSOC_IND && reason == DOT11_SC_SUCCESS) ||
			(event == WLC_E_DISASSOC_IND) ||
			((event == WLC_E_DEAUTH_IND) || (event == WLC_E_DEAUTH))) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0))
		cfg80211_rx_mgmt(ndev, freq, 0, mgmt_frame, len, 0);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0))
		cfg80211_rx_mgmt(ndev, freq, 0, mgmt_frame, len, 0, GFP_ATOMIC);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)) || \
		defined(WL_COMPAT_WIRELESS)
		cfg80211_rx_mgmt(ndev, freq, 0, mgmt_frame, len, GFP_ATOMIC);
#else
		cfg80211_rx_mgmt(ndev, freq, mgmt_frame, len, GFP_ATOMIC);
#endif 
	}

exit:
	if (isfree)
		kfree(mgmt_frame);
	if (body)
		kfree(body);
#else /* LINUX_VERSION < VERSION(3,2,0) && !WL_CFG80211_STA_EVENT && !WL_COMPAT_WIRELESS */
	memset(&sinfo, 0, sizeof(struct station_info));
	sinfo.filled = 0;
	if (((event == WLC_E_ASSOC_IND) || (event == WLC_E_REASSOC_IND)) &&
		reason == DOT11_SC_SUCCESS) {
		/* Linux ver >= 4.0 assoc_req_ies_len is used instead of
		 * STATION_INFO_ASSOC_REQ_IES flag
		 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
		sinfo.filled = STA_INFO_BIT(INFO_ASSOC_REQ_IES);
#endif /*  (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)) */
		if (!data) {
			WL_ERR(("No IEs present in ASSOC/REASSOC_IND"));
			return -EINVAL;
		}
		sinfo.assoc_req_ies = data;
		sinfo.assoc_req_ies_len = len;
		WL_MSG(ndev->name, "connected device "MACDBG"\n", MAC2STRDBG(e->addr.octet));
		wl_cfg80211_check_in4way(cfg, ndev, DONT_DELETE_GC_AFTER_WPS,
			WL_EXT_STATUS_STA_CONNECTED, NULL);
		cfg80211_new_sta(ndev, e->addr.octet, &sinfo, GFP_ATOMIC);
	} else if (event == WLC_E_DISASSOC_IND) {
		WL_MSG(ndev->name, "disassociated device "MACDBG"\n", MAC2STRDBG(e->addr.octet));
		wl_cfg80211_check_in4way(cfg, ndev, DONT_DELETE_GC_AFTER_WPS,
			WL_EXT_STATUS_STA_DISCONNECTED, NULL);
		cfg80211_del_sta(ndev, e->addr.octet, GFP_ATOMIC);
	} else if ((event == WLC_E_DEAUTH_IND) ||
		((event == WLC_E_DEAUTH) && (reason != DOT11_RC_RESERVED))) {
		WL_MSG(ndev->name, "deauthenticated device "MACDBG"\n", MAC2STRDBG(e->addr.octet));
		wl_cfg80211_check_in4way(cfg, ndev, DONT_DELETE_GC_AFTER_WPS,
			WL_EXT_STATUS_STA_DISCONNECTED, NULL);
		cfg80211_del_sta(ndev, e->addr.octet, GFP_ATOMIC);
	}
#endif 
	return err;
}

#if defined(DHD_ENABLE_BIGDATA_LOGGING)
#define MAX_ASSOC_REJECT_ERR_STATUS 5
int wl_get_connect_failed_status(struct bcm_cfg80211 *cfg, const wl_event_msg_t *e)
{
	u32 status = ntoh32(e->status);

	cfg->assoc_reject_status = 0;

	if (status == WLC_E_STATUS_FAIL) {
		WL_ERR(("auth assoc status event=%d e->status %d e->reason %d \n",
			ntoh32(cfg->event_auth_assoc.event_type),
			(int)ntoh32(cfg->event_auth_assoc.status),
			(int)ntoh32(cfg->event_auth_assoc.reason)));

		switch ((int)ntoh32(cfg->event_auth_assoc.status)) {
			case WLC_E_STATUS_NO_ACK:
				cfg->assoc_reject_status = 1;
				break;
			case WLC_E_STATUS_FAIL:
				cfg->assoc_reject_status = 2;
				break;
			case WLC_E_STATUS_UNSOLICITED:
				cfg->assoc_reject_status = 3;
				break;
			case WLC_E_STATUS_TIMEOUT:
				cfg->assoc_reject_status = 4;
				break;
			case WLC_E_STATUS_ABORT:
				cfg->assoc_reject_status = 5;
				break;
			default:
				break;
		}
		if (cfg->assoc_reject_status) {
			if (ntoh32(cfg->event_auth_assoc.event_type) == WLC_E_ASSOC) {
				cfg->assoc_reject_status += MAX_ASSOC_REJECT_ERR_STATUS;
			}
		}
	}

	WL_ERR(("assoc_reject_status %d \n", cfg->assoc_reject_status));

	return 0;
}

s32 wl_cfg80211_get_connect_failed_status(struct net_device *dev, char* cmd, int total_len)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	int bytes_written = 0;

	if (cfg == NULL) {
		return -1;
	}

	memset(cmd, 0, total_len);
	bytes_written = snprintf(cmd, 30, "assoc_reject.status %d", cfg->assoc_reject_status);

	WL_ERR(("cmd: %s \n", cmd));

	return bytes_written;
}
#endif /* DHD_ENABLE_BIGDATA_LOGGING */

static s32
wl_get_auth_assoc_status(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *e, void *data)
{
	u32 reason = ntoh32(e->reason);
	u32 event = ntoh32(e->event_type);
	uint auth_type = ntoh32(e->auth_type);
	struct wl_security *sec = wl_read_prof(cfg, ndev, WL_PROF_SEC);

	WL_DBG(("event type : %d, reason : %d, auth_type : %d\n", event, reason, auth_type));

#if defined(DHD_ENABLE_BIGDATA_LOGGING)
	memcpy(&cfg->event_auth_assoc, e, sizeof(wl_event_msg_t));
	WL_ERR(("event=%d status %d reason %d \n",
		ntoh32(cfg->event_auth_assoc.event_type),
		ntoh32(cfg->event_auth_assoc.status),
		ntoh32(cfg->event_auth_assoc.reason)));
#endif /* DHD_ENABLE_BIGDATA_LOGGING */
	if (sec) {
		switch (event) {
		case WLC_E_ASSOC:
		case WLC_E_AUTH:
			sec->auth_assoc_res_status = reason;
#ifdef WL_SAE
			if (event == WLC_E_AUTH && auth_type == DOT11_SAE) {
				wl_bss_handle_sae_auth(cfg, ndev, e, data);
			}
#endif /* WL_SAE */
		default:
			break;
		}
	} else
		WL_ERR(("sec is NULL\n"));
	return 0;
}

static s32
wl_notify_connect_status_ibss(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *e, void *data)
{
	s32 err = 0;
	u32 event = ntoh32(e->event_type);
	u16 flags = ntoh16(e->flags);
	u32 status =  ntoh32(e->status);
	bool active;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
	struct ieee80211_channel *channel = NULL;
	struct wiphy *wiphy = bcmcfg_to_wiphy(cfg);
	u32 chanspec, chan;
	u32 freq, band;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0) */

	if (event == WLC_E_JOIN) {
		WL_DBG(("joined in IBSS network\n"));
	}
	if (event == WLC_E_START) {
		WL_DBG(("started IBSS network\n"));
	}
	if (event == WLC_E_JOIN || event == WLC_E_START ||
		(event == WLC_E_LINK && (flags == WLC_EVENT_MSG_LINK))) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
		err = wldev_iovar_getint(ndev, "chanspec", (s32 *)&chanspec);
		if (unlikely(err)) {
			WL_ERR(("Could not get chanspec %d\n", err));
			return err;
		}
		chan = wf_chspec_ctlchan(wl_chspec_driver_to_host(chanspec));
		band = (chan <= CH_MAX_2G_CHANNEL) ? IEEE80211_BAND_2GHZ : IEEE80211_BAND_5GHZ;
		freq = ieee80211_channel_to_frequency(chan, band);
		channel = ieee80211_get_channel(wiphy, freq);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0) */
		if (wl_get_drv_status(cfg, CONNECTED, ndev)) {
			/* ROAM or Redundant */
			u8 *cur_bssid = wl_read_prof(cfg, ndev, WL_PROF_BSSID);
			if (memcmp(cur_bssid, &e->addr, ETHER_ADDR_LEN) == 0) {
				WL_DBG(("IBSS connected event from same BSSID("
					MACDBG "), ignore it\n", MAC2STRDBG(cur_bssid)));
				return err;
			}
			WL_INFORM(("IBSS BSSID is changed from " MACDBG " to " MACDBG "\n",
				MAC2STRDBG(cur_bssid), MAC2STRDBG((const u8 *)&e->addr)));
			wl_get_assoc_ies(cfg, ndev);
			wl_update_prof(cfg, ndev, NULL, (const void *)&e->addr, WL_PROF_BSSID);
			wl_update_bss_info(cfg, ndev, false);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
			cfg80211_ibss_joined(ndev, (const s8 *)&e->addr, channel, GFP_KERNEL);
#else
			cfg80211_ibss_joined(ndev, (const s8 *)&e->addr, GFP_KERNEL);
#endif
		}
		else {
			/* New connection */
			WL_INFORM(("IBSS connected to " MACDBG "\n",
				MAC2STRDBG((const u8 *)&e->addr)));
			wl_link_up(cfg);
			wl_get_assoc_ies(cfg, ndev);
			wl_update_prof(cfg, ndev, NULL, (const void *)&e->addr, WL_PROF_BSSID);
			wl_update_bss_info(cfg, ndev, false);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
			cfg80211_ibss_joined(ndev, (const s8 *)&e->addr, channel, GFP_KERNEL);
#else
			cfg80211_ibss_joined(ndev, (const s8 *)&e->addr, GFP_KERNEL);
#endif
			wl_set_drv_status(cfg, CONNECTED, ndev);
			active = true;
			wl_update_prof(cfg, ndev, NULL, (const void *)&active, WL_PROF_ACT);
		}
	} else if ((event == WLC_E_LINK && !(flags & WLC_EVENT_MSG_LINK)) ||
		event == WLC_E_DEAUTH_IND || event == WLC_E_DISASSOC_IND) {
		wl_clr_drv_status(cfg, CONNECTED, ndev);
		wl_link_down(cfg);
		wl_init_prof(cfg, ndev);
	}
	else if (event == WLC_E_SET_SSID && status == WLC_E_STATUS_NO_NETWORKS) {
		WL_DBG(("no action - join fail (IBSS mode)\n"));
	}
	else {
		WL_DBG(("no action (IBSS mode)\n"));
}
	return err;
}

#if defined(DHD_ENABLE_BIGDATA_LOGGING)
#define WiFiALL_OUI         "\x50\x6F\x9A"  /* Wi-FiAll OUI */
#define WiFiALL_OUI_LEN     3
#define WiFiALL_OUI_TYPE    16

int wl_get_bss_info(struct bcm_cfg80211 *cfg, struct net_device *dev, uint8 *mac)
{
	s32 err = 0;
	struct wl_bss_info *bi;
	uint8 eabuf[ETHER_ADDR_LEN];
	u32 rate, channel, freq, supported_rate, nss = 0, mcs_map, mode_80211 = 0;
	char rate_str[4];
	u8 *ie = NULL;
	u32 ie_len;
	struct wiphy *wiphy;
	struct cfg80211_bss *bss;
	bcm_tlv_t *interworking_ie = NULL;
	bcm_tlv_t *tlv_ie = NULL;
	bcm_tlv_t *vht_ie = NULL;
	vndr_ie_t *vndrie;
	int16 ie_11u_rel_num = -1, ie_mu_mimo_cap = -1;
	u32 i, remained_len, count = 0;
	char roam_count_str[4], akm_str[4];
	s32 val = 0;

	/* get BSS information */

	strncpy(cfg->bss_info, "x x x x x x x x x x x x x", GET_BSS_INFO_LEN);

	memset(cfg->extra_buf, 0, WL_EXTRA_BUF_MAX);
	*(u32 *) cfg->extra_buf = htod32(WL_EXTRA_BUF_MAX);

	err = wldev_ioctl_get(dev, WLC_GET_BSS_INFO, cfg->extra_buf, WL_EXTRA_BUF_MAX);
	if (unlikely(err)) {
		WL_ERR(("Could not get bss info %d\n", err));
		cfg->roam_count = 0;
		return -1;
	}

	if (!mac) {
		WL_ERR(("mac is null \n"));
		cfg->roam_count = 0;
		return -1;
	}

	memcpy(eabuf, mac, ETHER_ADDR_LEN);

	bi = (struct wl_bss_info *)(cfg->extra_buf + 4);
	channel = wf_chspec_ctlchan(bi->chanspec);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 39)
	freq = ieee80211_channel_to_frequency(channel);
#else
	if (channel > 14) {
		freq = ieee80211_channel_to_frequency(channel, IEEE80211_BAND_5GHZ);
	} else {
		freq = ieee80211_channel_to_frequency(channel, IEEE80211_BAND_2GHZ);
	}
#endif
	rate = 0;
	err = wldev_ioctl_get(dev, WLC_GET_RATE, &rate, sizeof(rate));
	if (err) {
		WL_ERR(("Could not get rate (%d)\n", err));
		snprintf(rate_str, sizeof(rate_str), "x"); // Unknown

	} else {
		rate = dtoh32(rate);
		snprintf(rate_str, sizeof(rate_str), "%d", (rate/2));
	}

	//supported maximum rate
	supported_rate = (bi->rateset.rates[bi->rateset.count - 1] & 0x7f) / 2;

	if (supported_rate < 12) {
		mode_80211 = 0; //11b maximum rate is 11Mbps. 11b mode
	} else {
		//It's not HT Capable case.
		if (channel > 14) {
			mode_80211 = 3; // 11a mode
		} else {
			mode_80211 = 1; // 11g mode
		}
	}

	if (bi->n_cap) {
		/* check Rx MCS Map for HT */
		nss = 0;
		mode_80211 = 2;
		for (i = 0; i < MAX_STREAMS_SUPPORTED; i++) {
			int8 bitmap = 0xFF;
			if (i == MAX_STREAMS_SUPPORTED-1) {
				bitmap = 0x7F;
			}
			if (bi->basic_mcs[i] & bitmap) {
				nss++;
			}
		}
	}

	if (bi->vht_cap) {
		nss = 0;
		mode_80211 = 4;
		for (i = 1; i <= VHT_CAP_MCS_MAP_NSS_MAX; i++) {
			mcs_map = VHT_MCS_MAP_GET_MCS_PER_SS(i, dtoh16(bi->vht_rxmcsmap));
			if (mcs_map != VHT_CAP_MCS_MAP_NONE) {
				nss++;
			}
		}
	}

	if (nss) {
		nss = nss - 1;
	}

	wiphy = bcmcfg_to_wiphy(cfg);
	bss = CFG80211_GET_BSS(wiphy, NULL, eabuf, bi->SSID, bi->SSID_len);
	if (!bss) {
		WL_ERR(("Could not find the AP\n"));
	} else {
#if defined(WL_CFG80211_P2P_DEV_IF)
		ie = (u8 *)bss->ies->data;
		ie_len = bss->ies->len;
#else
		ie = bss->information_elements;
		ie_len = bss->len_information_elements;
#endif /* WL_CFG80211_P2P_DEV_IF */
	}

	if (ie) {
		ie_mu_mimo_cap = 0;
		ie_11u_rel_num = 0;

		if (bi->vht_cap) {
			if ((vht_ie = bcm_parse_tlvs(ie, (u32)ie_len,
					DOT11_MNG_VHT_CAP_ID)) != NULL) {
				ie_mu_mimo_cap = (vht_ie->data[2] & 0x08) >> 3;
			}
		}

		if ((interworking_ie = bcm_parse_tlvs(ie, (u32)ie_len,
				DOT11_MNG_INTERWORKING_ID)) != NULL) {
			if ((tlv_ie = bcm_parse_tlvs(ie, (u32)ie_len, DOT11_MNG_VS_ID)) != NULL) {
				remained_len = ie_len;

				while (tlv_ie) {
					if (count > MAX_VNDR_IE_NUMBER)
						break;

					if (tlv_ie->id == DOT11_MNG_VS_ID) {
						vndrie = (vndr_ie_t *) tlv_ie;

						if (vndrie->len < (VNDR_IE_MIN_LEN + 1)) {
							WL_ERR(("%s: invalid vndr ie."
								"length is too small %d\n",
								__FUNCTION__, vndrie->len));
							break;
						}

						if (!bcmp(vndrie->oui,
							(u8*)WiFiALL_OUI, WiFiALL_OUI_LEN) &&
							(vndrie->data[0] == WiFiALL_OUI_TYPE))
						{
							WL_ERR(("Found Wi-FiAll OUI oui.\n"));
							ie_11u_rel_num = vndrie->data[1];
							ie_11u_rel_num = (ie_11u_rel_num & 0xf0)>>4;
							ie_11u_rel_num += 1;

							break;
						}
					}
					count++;
					tlv_ie = bcm_next_tlv(tlv_ie, &remained_len);
				}
			}
		}
	}

	for (i = 0; i < bi->SSID_len; i++) {
		if (bi->SSID[i] == ' ') {
			bi->SSID[i] = '_';
		}
	}

	//0 : None, 1 : OKC, 2 : FT, 3 : CCKM
	err = wldev_iovar_getint(dev, "wpa_auth", &val);
	if (unlikely(err)) {
		WL_ERR(("could not get wpa_auth (%d)\n", err));
		snprintf(akm_str, sizeof(akm_str), "x"); // Unknown
	} else {
		WL_ERR(("wpa_auth val %d \n", val));
#if defined(BCMEXTCCX)
		if (val & (WPA_AUTH_CCKM | WPA2_AUTH_CCKM)) {
			snprintf(akm_str, sizeof(akm_str), "3");
		} else
#endif  
		if (val & WPA2_AUTH_FT) {
			snprintf(akm_str, sizeof(akm_str), "2");
		} else if (val & (WPA_AUTH_UNSPECIFIED | WPA2_AUTH_UNSPECIFIED)) {
			snprintf(akm_str, sizeof(akm_str), "1");
		} else {
			snprintf(akm_str, sizeof(akm_str), "0");
		}
	}

	if (cfg->roam_offload) {
		snprintf(roam_count_str, sizeof(roam_count_str), "x"); // Unknown
	} else {
		snprintf(roam_count_str, sizeof(roam_count_str), "%d", cfg->roam_count);
	}
	cfg->roam_count = 0;

	WL_ERR(("BSSID:" MACDBG " SSID %s \n", MAC2STRDBG(eabuf), bi->SSID));
	WL_ERR(("freq:%d, BW:%s, RSSI:%d dBm, Rate:%d Mbps, 11mode:%d, stream:%d,"
				"MU-MIMO:%d, Passpoint:%d, SNR:%d, Noise:%d, \n"
				"akm:%s roam:%s \n",
				freq, wf_chspec_to_bw_str(bi->chanspec),
				dtoh32(bi->RSSI), (rate / 2), mode_80211, nss,
				ie_mu_mimo_cap, ie_11u_rel_num, bi->SNR, bi->phy_noise,
				akm_str, roam_count_str));

	if (ie) {
		snprintf(cfg->bss_info, GET_BSS_INFO_LEN,
				"%02x:%02x:%02x %d %s %d %s %d %d %d %d %d %d %s %s",
				eabuf[0], eabuf[1], eabuf[2],
				freq, wf_chspec_to_bw_str(bi->chanspec),
				dtoh32(bi->RSSI), rate_str, mode_80211, nss,
				ie_mu_mimo_cap, ie_11u_rel_num,
				bi->SNR, bi->phy_noise, akm_str, roam_count_str);
	} else {
		//ie_mu_mimo_cap and ie_11u_rel_num is unknow.
		snprintf(cfg->bss_info, GET_BSS_INFO_LEN,
				"%02x:%02x:%02x %d %s %d %s %d %d x x %d %d %s %s",
				eabuf[0], eabuf[1], eabuf[2],
				freq, wf_chspec_to_bw_str(bi->chanspec),
				dtoh32(bi->RSSI), rate_str, mode_80211, nss,
				bi->SNR, bi->phy_noise, akm_str, roam_count_str);
	}

	CFG80211_PUT_BSS(wiphy, bss);

	return 0;
}

s32 wl_cfg80211_get_bss_info(struct net_device *dev, char* cmd, int total_len)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	if (cfg == NULL) {
		return -1;
	}

	memset(cmd, 0, total_len);
	memcpy(cmd, cfg->bss_info, GET_BSS_INFO_LEN);

	WL_ERR(("cmd: %s \n", cmd));

	return GET_BSS_INFO_LEN;
}

#endif /* DHD_ENABLE_BIGDATA_LOGGING */

#ifdef WLMESH_CFG80211
static s32
wl_notify_connect_status_mesh(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *e, void *data)
{
	s32 err = 0;
	u32 event = ntoh32(e->event_type);
	u32 reason = ntoh32(e->reason);
	u32 len = ntoh32(e->datalen);
	u32 status = ntoh32(e->status);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 2, 0)) && !defined(WL_CFG80211_STA_EVENT)
	bool isfree = false;
	u8 *mgmt_frame;
	u8 bsscfgidx = e->bsscfgidx;
	s32 freq;
	s32 channel;
	u8 *body = NULL;
	u16 fc = 0;

	struct ieee80211_supported_band *band;
	struct ether_addr da;
	struct ether_addr bssid;
	struct wiphy *wiphy = bcmcfg_to_wiphy(cfg);
	channel_info_t ci;
#else
	struct station_info sinfo;
#endif 

	WL_DBG(("event %d status %d reason %d\n", event, ntoh32(e->status), reason));
	/* if link down, bsscfg is disabled. */
	if (event == WLC_E_LINK && reason == WLC_E_LINK_BSSCFG_DIS &&
			(ndev != bcmcfg_to_prmry_ndev(cfg))) {
		WL_MSG(ndev->name, "Mesh mode link down !! \n");
		return 0;
	}

	if ((event == WLC_E_LINK) && (status == WLC_E_STATUS_SUCCESS) &&
			(reason == WLC_E_REASON_INITIAL_ASSOC)) {
		/* AP/GO brought up successfull in firmware */
		WL_MSG(ndev->name, "Mesh Link up\n");
		return 0;
	}

	if (event == WLC_E_DISASSOC_IND || event == WLC_E_DEAUTH_IND || event == WLC_E_DEAUTH) {
		WL_MSG(ndev->name, "event %s(%d) status %d reason %d\n",
		bcmevent_get_name(event), event, ntoh32(e->status), reason);
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 2, 0)) && !defined(WL_CFG80211_STA_EVENT)
	WL_DBG(("Enter \n"));
	if (!len && (event == WLC_E_DEAUTH)) {
		len = 2; /* reason code field */
		data = &reason;
	}
	if (len) {
		body = kzalloc(len, GFP_KERNEL);

		if (body == NULL) {
			WL_ERR(("Failed to allocate body\n"));
			return WL_INVALID;
		}
	}
	memset(&bssid, 0, ETHER_ADDR_LEN);
	WL_DBG(("Enter event %d ndev %p\n", event, ndev));
	if (wl_get_mode_by_netdev(cfg, ndev) == WL_INVALID) {
		kfree(body);
		return WL_INVALID;
	}
	if (len)
		memcpy(body, data, len);

	wldev_iovar_getbuf_bsscfg(ndev, "cur_etheraddr",
		NULL, 0, cfg->ioctl_buf, WLC_IOCTL_SMLEN, bsscfgidx, &cfg->ioctl_buf_sync);
	memcpy(da.octet, cfg->ioctl_buf, ETHER_ADDR_LEN);
	memset(&bssid, 0, sizeof(bssid));
	err = wldev_ioctl_get(ndev, WLC_GET_BSSID, &bssid, ETHER_ADDR_LEN);
	switch (event) {
		case WLC_E_ASSOC_IND:
			fc = FC_ASSOC_REQ;
			break;
		case WLC_E_REASSOC_IND:
			fc = FC_REASSOC_REQ;
			break;
		case WLC_E_DISASSOC_IND:
			fc = FC_DISASSOC;
			break;
		case WLC_E_DEAUTH_IND:
			fc = FC_DISASSOC;
			break;
		case WLC_E_DEAUTH:
			fc = FC_DISASSOC;
			break;
		default:
			fc = 0;
			goto exit;
	}
	memset(&ci, 0, sizeof(ci));
	if ((err = wldev_ioctl_get(ndev, WLC_GET_CHANNEL, &ci, sizeof(ci)))) {
		kfree(body);
		return err;
	}

	channel = dtoh32(ci.hw_channel);
	if (channel <= CH_MAX_2G_CHANNEL)
		band = wiphy->bands[IEEE80211_BAND_2GHZ];
	else
		band = wiphy->bands[IEEE80211_BAND_5GHZ];
	if (!band) {
		WL_ERR(("No valid band\n"));
		if (body)
			kfree(body);
		return -EINVAL;
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 39)
	freq = ieee80211_channel_to_frequency(channel);
	(void)band->band;
#else
	freq = ieee80211_channel_to_frequency(channel, band->band);
#endif

	err = wl_frame_get_mgmt(fc, &da, &e->addr, &bssid,
		&mgmt_frame, &len, body);
	if (err < 0)
		goto exit;
	isfree = true;

	if ((event == WLC_E_ASSOC_IND && reason == DOT11_SC_SUCCESS) ||
			(event == WLC_E_DISASSOC_IND) ||
			((event == WLC_E_DEAUTH_IND) || (event == WLC_E_DEAUTH))) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0))
		cfg80211_rx_mgmt(ndev, freq, 0, mgmt_frame, len, 0);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0))
		cfg80211_rx_mgmt(ndev, freq, 0, mgmt_frame, len, 0, GFP_ATOMIC);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)) || \
		defined(WL_COMPAT_WIRELESS)
		cfg80211_rx_mgmt(ndev, freq, 0, mgmt_frame, len, GFP_ATOMIC);
#else
		cfg80211_rx_mgmt(ndev, freq, mgmt_frame, len, GFP_ATOMIC);
#endif 
	}

exit:
	if (isfree)
		kfree(mgmt_frame);
	if (body)
		kfree(body);
#else /* LINUX_VERSION < VERSION(3,2,0) && !WL_CFG80211_STA_EVENT && !WL_COMPAT_WIRELESS */
	memset(&sinfo, 0, sizeof(struct station_info));
	sinfo.filled = 0;
	if (((event == WLC_E_ASSOC_IND) || (event == WLC_E_REASSOC_IND)) &&
		reason == DOT11_SC_SUCCESS) {
		/* Linux ver >= 4.0 assoc_req_ies_len is used instead of
		 * STATION_INFO_ASSOC_REQ_IES flag
		 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
		sinfo.filled = STA_INFO_BIT(INFO_ASSOC_REQ_IES);
#endif /*  (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)) */
		if (!data) {
			WL_ERR(("No IEs present in ASSOC/REASSOC_IND"));
			return -EINVAL;
		}
		sinfo.assoc_req_ies = data;
		sinfo.assoc_req_ies_len = len;
		WL_MSG(ndev->name, "connected device "MACDBG"\n", MAC2STRDBG(e->addr.octet));
		cfg80211_new_sta(ndev, e->addr.octet, &sinfo, GFP_ATOMIC);
	} else if (event == WLC_E_DISASSOC_IND) {
		WL_MSG(ndev->name, "disassociated device "MACDBG"\n", MAC2STRDBG(e->addr.octet));
		cfg80211_del_sta(ndev, e->addr.octet, GFP_ATOMIC);
	} else if ((event == WLC_E_DEAUTH_IND) ||
		((event == WLC_E_DEAUTH) && (reason != DOT11_RC_RESERVED))) {
		WL_MSG(ndev->name, "deauthenticated device "MACDBG"\n", MAC2STRDBG(e->addr.octet));
		cfg80211_del_sta(ndev, e->addr.octet, GFP_ATOMIC);
	}
#endif 
	return err;
}
#endif

static s32
wl_notify_connect_status(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	bool act;
	struct net_device *ndev = NULL;
	s32 err = 0;
	u32 event = ntoh32(e->event_type);
	struct wiphy *wiphy = NULL;
	struct cfg80211_bss *bss = NULL;
	struct wlc_ssid *ssid = NULL;
	u8 *bssid = 0;
	dhd_pub_t *dhdp;
	int vndr_oui_num = 0;
	char vndr_oui[MAX_VNDR_OUI_STR_LEN] = {0, };

	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);
	dhdp = (dhd_pub_t *)(cfg->pub);
	BCM_REFERENCE(dhdp);

	if (wl_get_mode_by_netdev(cfg, ndev) == WL_MODE_AP) {
		err = wl_notify_connect_status_ap(cfg, ndev, e, data);
#ifdef WLMESH_CFG80211
	} else if (wl_get_mode_by_netdev(cfg, ndev) == WL_MODE_MESH) {
		err = wl_notify_connect_status_mesh(cfg, ndev, e, data);
#endif
	} else if (wl_get_mode_by_netdev(cfg, ndev) == WL_MODE_IBSS) {
		err = wl_notify_connect_status_ibss(cfg, ndev, e, data);
	} else if (wl_get_mode_by_netdev(cfg, ndev) == WL_MODE_BSS) {
		WL_DBG(("event %d status : %d ndev %p\n",
			ntoh32(e->event_type), ntoh32(e->status), ndev));
		if (event == WLC_E_ASSOC || event == WLC_E_AUTH) {
			wl_get_auth_assoc_status(cfg, ndev, e, data);
			return 0;
		}
		DHD_DISABLE_RUNTIME_PM((dhd_pub_t *)cfg->pub);
		if (wl_is_linkup(cfg, e, ndev)) {
			wl_link_up(cfg);
			act = true;
			if (!wl_get_drv_status(cfg, DISCONNECTING, ndev)) {
				if (event == WLC_E_LINK &&
#ifdef DHD_LOSSLESS_ROAMING
					!cfg->roam_offload &&
#endif /* DHD_LOSSLESS_ROAMING */
					wl_get_drv_status(cfg, CONNECTED, ndev)) {
					wl_bss_roaming_done(cfg, ndev, e, data);
				}

				if (ndev == bcmcfg_to_prmry_ndev(cfg)) {
					vndr_oui_num = wl_vndr_ies_get_vendor_oui(cfg,
						ndev, vndr_oui, ARRAYSIZE(vndr_oui));
#if defined(STAT_REPORT)
					/* notify STA connection only */
					wl_stat_report_notify_connected(cfg);
#endif /* STAT_REPORT */
				}

				WL_MSG(ndev->name, "wl_bss_connect_done succeeded with "
					MACDBG " %s%s\n", MAC2STRDBG((const u8*)(&e->addr)),
					vndr_oui_num > 0 ? "vndr_oui: " : "",
					vndr_oui_num > 0 ? vndr_oui : "");

				wl_bss_connect_done(cfg, ndev, e, data, true);
				WL_DBG(("joined in BSS network \"%s\"\n",
					((struct wlc_ssid *)
					 wl_read_prof(cfg, ndev, WL_PROF_SSID))->SSID));

#ifdef WBTEXT
				if (ndev->ieee80211_ptr->iftype == NL80211_IFTYPE_STATION &&
					dhdp->wbtext_support &&
					event == WLC_E_SET_SSID) {
					/* set wnm_keepalives_max_idle after association */
					wl_cfg80211_wbtext_set_wnm_maxidle(cfg, ndev);
					/* send nbr request or BTM query to update RCC */
					wl_cfg80211_wbtext_update_rcc(cfg, ndev);
				}
#endif /* WBTEXT */
			}
			wl_update_prof(cfg, ndev, e, &act, WL_PROF_ACT);
			wl_update_prof(cfg, ndev, NULL, (const void *)&e->addr, WL_PROF_BSSID);
		} else if (WL_IS_LINKDOWN(cfg, e, data) ||
				((event == WLC_E_SET_SSID) &&
				(ntoh32(e->status) != WLC_E_STATUS_SUCCESS) &&
				(wl_get_drv_status(cfg, CONNECTED, ndev)))) {

			WL_INFORM(("connection state bit status: [%d:%d:%d:%d]\n",
				wl_get_drv_status(cfg, CONNECTING, ndev),
				wl_get_drv_status(cfg, CONNECTED, ndev),
				wl_get_drv_status(cfg, DISCONNECTING, ndev),
				wl_get_drv_status(cfg, NESTED_CONNECT, ndev)));

			if (wl_get_drv_status(cfg, DISCONNECTING, ndev) &&
				(wl_get_drv_status(cfg, NESTED_CONNECT, ndev) ||
				wl_get_drv_status(cfg, CONNECTING, ndev))) {
				/* wl_cfg80211_connect was called before 'DISCONNECTING' was
				 * cleared. Deauth/Link down event is caused by WLC_DISASSOC
				 * command issued from the wl_cfg80211_connect context. Ignore
				 * the event to avoid pre-empting the current connection
				 */
				WL_INFORM(("Nested connection case. Drop event. \n"));
				wl_cfg80211_check_in4way(cfg, ndev, NO_SCAN_IN4WAY|NO_BTC_IN4WAY|WAIT_DISCONNECTED,
					WL_EXT_STATUS_DISCONNECTED, NULL);
				wl_clr_drv_status(cfg, NESTED_CONNECT, ndev);
				wl_clr_drv_status(cfg, DISCONNECTING, ndev);
				/* Not in 'CONNECTED' state, clear it */
				wl_clr_drv_status(cfg, CONNECTED, ndev);
				return 0;
			}

#ifdef DHD_LOSSLESS_ROAMING
			wl_del_roam_timeout(cfg);
#endif
#ifdef P2PLISTEN_AP_SAMECHN
			if (ndev == bcmcfg_to_prmry_ndev(cfg)) {
				wl_cfg80211_set_p2p_resp_ap_chn(ndev, 0);
				cfg->p2p_resp_apchn_status = false;
				WL_DBG(("p2p_resp_apchn_status Turn OFF \n"));
			}
#endif /* P2PLISTEN_AP_SAMECHN */
			wl_cfg80211_cancel_scan(cfg);

#if defined(DHD_ENABLE_BIGDATA_LOGGING)
			if (wl_get_drv_status(cfg, CONNECTED, ndev)) {
				wl_get_bss_info(cfg, ndev, (u8*)(&e->addr));
			}
#endif /* DHD_ENABLE_BIGDATA_LOGGING */
			if (wl_get_drv_status(cfg, CONNECTED, ndev)) {
				u8 *curbssid = wl_read_prof(cfg, ndev, WL_PROF_BSSID);
				if (memcmp(curbssid, &e->addr, ETHER_ADDR_LEN) != 0) {
					bool fw_assoc_state = TRUE;
					dhd_pub_t *dhd = (dhd_pub_t *)cfg->pub;
					fw_assoc_state = dhd_is_associated(dhd, e->ifidx, &err);
					if (!fw_assoc_state) {
						WL_ERR(("Event sends up even different BSSID"
							" cur: " MACDBG " event: " MACDBG"\n",
							MAC2STRDBG(curbssid),
							MAC2STRDBG((const u8*)(&e->addr))));
					} else {
						WL_ERR(("BSSID of event is not the connected BSSID"
							"(ignore it) cur: " MACDBG
							" event: " MACDBG"\n",
							MAC2STRDBG(curbssid),
							MAC2STRDBG((const u8*)(&e->addr))));
						return 0;
					}
				}
			}
			/* Explicitly calling unlink to remove BSS in CFG */
			wiphy = bcmcfg_to_wiphy(cfg);
			ssid = (struct wlc_ssid *)wl_read_prof(cfg, ndev, WL_PROF_SSID);
			bssid = (u8 *)wl_read_prof(cfg, ndev, WL_PROF_BSSID);
			if (ssid && bssid) {
				bss = CFG80211_GET_BSS(wiphy, NULL, bssid,
					ssid->SSID, ssid->SSID_len);
				if (bss) {
					cfg80211_unlink_bss(wiphy, bss);
					CFG80211_PUT_BSS(wiphy, bss);
				}
			}

			if (wl_get_drv_status(cfg, CONNECTED, ndev)) {
				scb_val_t scbval;
				u8 *curbssid = wl_read_prof(cfg, ndev, WL_PROF_BSSID);
				s32 reason = 0;
				struct ether_addr bssid_dongle = {{0, 0, 0, 0, 0, 0}};
				struct ether_addr bssid_null = {{0, 0, 0, 0, 0, 0}};

				if (event == WLC_E_DEAUTH_IND || event == WLC_E_DISASSOC_IND)
					reason = ntoh32(e->reason);
				/* WLAN_REASON_UNSPECIFIED is used for hang up event in Android */
				reason = (reason == WLAN_REASON_UNSPECIFIED)? 0 : reason;

				WL_MSG(ndev->name, "link down if %s may call cfg80211_disconnected. "
					"event : %d, reason=%d from " MACDBG "\n",
					ndev->name, event, ntoh32(e->reason),
					MAC2STRDBG((const u8*)(&e->addr)));

				/* roam offload does not sync BSSID always, get it from dongle */
				if (cfg->roam_offload) {
					memset(&bssid_dongle, 0, sizeof(bssid_dongle));
					if (wldev_ioctl_get(ndev, WLC_GET_BSSID, &bssid_dongle,
							sizeof(bssid_dongle)) == BCME_OK) {
						/* if not roam case, it would return null bssid */
						if (memcmp(&bssid_dongle, &bssid_null,
								ETHER_ADDR_LEN) != 0) {
							curbssid = (u8 *)&bssid_dongle;
						}
					}
				}
				if (memcmp(curbssid, &e->addr, ETHER_ADDR_LEN) != 0) {
					bool fw_assoc_state = TRUE;
					dhd_pub_t *dhd = (dhd_pub_t *)cfg->pub;
					fw_assoc_state = dhd_is_associated(dhd, e->ifidx, &err);
					if (!fw_assoc_state) {
						WL_ERR(("Event sends up even different BSSID"
							" cur: " MACDBG " event: " MACDBG"\n",
							MAC2STRDBG(curbssid),
							MAC2STRDBG((const u8*)(&e->addr))));
					} else {
						WL_ERR(("BSSID of event is not the connected BSSID"
							"(ignore it) cur: " MACDBG
							" event: " MACDBG"\n",
							MAC2STRDBG(curbssid),
							MAC2STRDBG((const u8*)(&e->addr))));
						return 0;
					}
				}

#ifdef DBG_PKT_MON
				if (ndev == bcmcfg_to_prmry_ndev(cfg)) {
					DHD_DBG_PKT_MON_STOP(dhdp);
				}
#endif /* DBG_PKT_MON */

				/* clear RSSI monitor, framework will set new cfg */
#ifdef RSSI_MONITOR_SUPPORT
				dhd_dev_set_rssi_monitor_cfg(bcmcfg_to_prmry_ndev(cfg),
				    FALSE, 0, 0);
#endif /* RSSI_MONITOR_SUPPORT */
				if (!memcmp(ndev->name, WL_P2P_INTERFACE_PREFIX, strlen(WL_P2P_INTERFACE_PREFIX))) {
					// terence 20130703: Fix for wrong group_capab (timing issue)
					cfg->p2p_disconnected = 1;
				}
				memcpy(&cfg->disconnected_bssid, curbssid, ETHER_ADDR_LEN);
				wl_clr_drv_status(cfg, CONNECTED, ndev);

				if (!wl_get_drv_status(cfg, DISCONNECTING, ndev)) {
					/* To make sure disconnect, explictly send dissassoc
					*  for BSSID 00:00:00:00:00:00 issue
					*/
					scbval.val = WLAN_REASON_DEAUTH_LEAVING;

					memcpy(&scbval.ea, curbssid, ETHER_ADDR_LEN);
					scbval.val = htod32(scbval.val);
					err = wldev_ioctl_set(ndev, WLC_DISASSOC, &scbval,
						sizeof(scb_val_t));
					if (err < 0) {
						WL_ERR(("WLC_DISASSOC error %d\n", err));
						err = 0;
					}
				}

				/* Send up deauth and clear states */
				CFG80211_DISCONNECTED(ndev, reason, NULL, 0,
						false, GFP_KERNEL);
				wl_link_down(cfg);
				wl_init_prof(cfg, ndev);
#ifdef WBTEXT
				/* when STA was disconnected, clear join pref and set wbtext */
				if (ndev->ieee80211_ptr->iftype == NL80211_IFTYPE_STATION) {
					char smbuf[WLC_IOCTL_SMLEN];
					char clear[] = { 0x01, 0x02, 0x00, 0x00, 0x03,
						0x02, 0x00, 0x00, 0x04, 0x02, 0x00, 0x00 };
					if ((err = wldev_iovar_setbuf(ndev, "join_pref",
							clear, sizeof(clear), smbuf,
							sizeof(smbuf), NULL))
							== BCME_OK) {
						if ((err = wldev_iovar_setint(ndev,
								"wnm_bsstrans_resp",
								WL_BSSTRANS_POLICY_PRODUCT_WBTEXT))
								== BCME_OK) {
							wl_cfg80211_wbtext_set_default(ndev);
						} else {
							WL_ERR(("%s: Failed to set wbtext = %d\n",
								__FUNCTION__, err));
						}
					} else {
						WL_ERR(("%s: Failed to clear join pref = %d\n",
							__FUNCTION__, err));
					}
					wl_cfg80211_wbtext_clear_bssid_list(cfg);
				}
#endif /* WBTEXT */
				wl_vndr_ies_clear_vendor_oui_list(cfg);
			}
			else if (wl_get_drv_status(cfg, CONNECTING, ndev)) {
				WL_MSG(ndev->name, "link down, during connecting\n");
				/* Issue WLC_DISASSOC to prevent FW roam attempts */
				err = wldev_ioctl_set(ndev, WLC_DISASSOC, NULL, 0);
				if (err < 0) {
					WL_ERR(("CONNECTING state, WLC_DISASSOC error %d\n", err));
					err = 0;
				}
#ifdef ESCAN_RESULT_PATCH
				if ((memcmp(connect_req_bssid, broad_bssid, ETHER_ADDR_LEN) == 0) ||
					(memcmp(&e->addr, broad_bssid, ETHER_ADDR_LEN) == 0) ||
					(memcmp(&e->addr, connect_req_bssid, ETHER_ADDR_LEN) == 0))
					/* In case this event comes while associating another AP */
#endif /* ESCAN_RESULT_PATCH */
					wl_bss_connect_done(cfg, ndev, e, data, false);
				WL_INFORM(("Clear drv CONNECTING status\n"));
				wl_clr_drv_status(cfg, CONNECTING, ndev);
			}
			wl_clr_drv_status(cfg, DISCONNECTING, ndev);
			wl_cfg80211_check_in4way(cfg, ndev, NO_SCAN_IN4WAY|NO_BTC_IN4WAY|WAIT_DISCONNECTED,
				WL_EXT_STATUS_DISCONNECTED, NULL);

			/* if link down, bsscfg is diabled */
			if (ndev != bcmcfg_to_prmry_ndev(cfg))
				complete(&cfg->iface_disable);
#ifdef WLTDLS
			/* re-enable TDLS if the number of connected interfaces
			 * is less than 2.
			 */
			wl_cfg80211_tdls_config(cfg, TDLS_STATE_DISCONNECT, false);
#endif /* WLTDLS */
		} else if (wl_is_nonetwork(cfg, e)) {
			WL_MSG(ndev->name, "connect failed event=%d e->status %d e->reason %d \n",
				event, (int)ntoh32(e->status), (int)ntoh32(e->reason));
			wl_cfg80211_check_in4way(cfg, ndev, NO_SCAN_IN4WAY|NO_BTC_IN4WAY|WAIT_DISCONNECTED,
				WL_EXT_STATUS_DISCONNECTED, NULL);
#if defined(DHD_ENABLE_BIGDATA_LOGGING)
			if (event == WLC_E_SET_SSID) {
				wl_get_connect_failed_status(cfg, e);
			}
#endif /* DHD_ENABLE_BIGDATA_LOGGING */

			if (wl_get_drv_status(cfg, DISCONNECTING, ndev) &&
				wl_get_drv_status(cfg, CONNECTING, ndev)) {
				wl_clr_drv_status(cfg, DISCONNECTING, ndev);
				wl_clr_drv_status(cfg, CONNECTING, ndev);
				wl_cfg80211_scan_abort(cfg);
				DHD_ENABLE_RUNTIME_PM((dhd_pub_t *)cfg->pub);
				return err;
			}
			/* Clean up any pending scan request */
			wl_cfg80211_cancel_scan(cfg);

			if (wl_get_drv_status(cfg, CONNECTING, ndev))
				wl_bss_connect_done(cfg, ndev, e, data, false);
		} else {
			WL_DBG(("%s nothing\n", __FUNCTION__));
		}
		DHD_ENABLE_RUNTIME_PM((dhd_pub_t *)cfg->pub);
	}
	else {
		WL_MSG(ndev->name, "Invalid mode %d event %d status %d\n",
			wl_get_mode_by_netdev(cfg, ndev), ntoh32(e->event_type),
			ntoh32(e->status));
	}
	return err;
}

#ifdef WL_RELMCAST
void wl_cfg80211_set_rmc_pid(struct net_device *dev, int pid)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	if (pid > 0)
		cfg->rmc_event_pid = pid;
	WL_DBG(("set pid for rmc event : pid=%d\n", pid));
}
#endif /* WL_RELMCAST */

#ifdef WL_RELMCAST
static s32
wl_notify_rmc_status(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	u32 evt = ntoh32(e->event_type);
	u32 reason = ntoh32(e->reason);
	int ret = -1;

	switch (reason) {
		case WLC_E_REASON_RMC_AR_LOST:
		case WLC_E_REASON_RMC_AR_NO_ACK:
			if (cfg->rmc_event_pid != 0) {
				ret = wl_netlink_send_msg(cfg->rmc_event_pid,
					RMC_EVENT_LEADER_CHECK_FAIL,
					cfg->rmc_event_seq++, NULL, 0);
			}
			break;
		default:
			break;
	}
	WL_DBG(("rmcevent : evt=%d, pid=%d, ret=%d\n", evt, cfg->rmc_event_pid, ret));
	return ret;
}
#endif /* WL_RELMCAST */

#ifdef GSCAN_SUPPORT
static s32
wl_handle_roam_exp_event(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	struct net_device *ndev = NULL;
	u32 datalen = be32_to_cpu(e->datalen);

	if (datalen) {
		wl_roam_exp_event_t *evt_data = (wl_roam_exp_event_t *)data;
		if (evt_data->version == ROAM_EXP_EVENT_VERSION) {
			wlc_ssid_t *ssid = &evt_data->cur_ssid;
			struct wireless_dev *wdev;
			ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);
			if (ndev) {
				wdev = ndev->ieee80211_ptr;
				wdev->ssid_len = min(ssid->SSID_len, (uint32)DOT11_MAX_SSID_LEN);
				memcpy(wdev->ssid, ssid->SSID, wdev->ssid_len);
				WL_ERR(("SSID is %s\n", ssid->SSID));
				wl_update_prof(cfg, ndev, NULL, ssid, WL_PROF_SSID);
			} else {
				WL_ERR(("NULL ndev!\n"));
			}
		} else {
			WL_ERR(("Version mismatch %d, expected %d", evt_data->version,
			       ROAM_EXP_EVENT_VERSION));
		}
	}
	return BCME_OK;
}
#endif /* GSCAN_SUPPORT */

#ifdef RSSI_MONITOR_SUPPORT
static s32 wl_handle_rssi_monitor_event(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{

#if defined(WL_VENDOR_EXT_SUPPORT) || defined(CONFIG_BCMDHD_VENDOR_EXT)
	u32 datalen = be32_to_cpu(e->datalen);
	struct net_device *ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);
	struct wiphy *wiphy = bcmcfg_to_wiphy(cfg);

	if (datalen) {
		wl_rssi_monitor_evt_t *evt_data = (wl_rssi_monitor_evt_t *)data;
		if (evt_data->version == RSSI_MONITOR_VERSION) {
			dhd_rssi_monitor_evt_t monitor_data;
			monitor_data.version = DHD_RSSI_MONITOR_EVT_VERSION;
			monitor_data.cur_rssi = evt_data->cur_rssi;
			memcpy(&monitor_data.BSSID, &e->addr, ETHER_ADDR_LEN);
			wl_cfgvendor_send_async_event(wiphy, ndev,
				GOOGLE_RSSI_MONITOR_EVENT,
				&monitor_data, sizeof(monitor_data));
		} else {
			WL_ERR(("Version mismatch %d, expected %d", evt_data->version,
			       RSSI_MONITOR_VERSION));
		}
	}
#endif /* WL_VENDOR_EXT_SUPPORT || CONFIG_BCMDHD_VENDOR_EXT */
	return BCME_OK;
}
#endif /* RSSI_MONITOR_SUPPORT */

static s32
wl_notify_roaming_status(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	bool act;
	struct net_device *ndev = NULL;
	s32 err = 0;
	u32 event = be32_to_cpu(e->event_type);
	u32 status = be32_to_cpu(e->status);
#ifdef DHD_LOSSLESS_ROAMING
	struct wl_security *sec;
#endif
	WL_DBG(("Enter \n"));

	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);

	if ((!cfg->disable_roam_event) && (event == WLC_E_BSSID)) {
		wl_add_remove_eventmsg(ndev, WLC_E_ROAM, false);
		cfg->disable_roam_event = TRUE;
	}

	if ((cfg->disable_roam_event) && (event == WLC_E_ROAM))
		return err;

	if ((event == WLC_E_ROAM || event == WLC_E_BSSID) && status == WLC_E_STATUS_SUCCESS) {
		if (wl_get_drv_status(cfg, CONNECTED, ndev)) {
#ifdef DHD_LOSSLESS_ROAMING
			sec = wl_read_prof(cfg, ndev, WL_PROF_SEC);
			/* In order to reduce roaming delay, wl_bss_roaming_done is
			 * early called with WLC_E_LINK event. It is called from
			 * here only if WLC_E_LINK event is blocked for specific
			 * security type.
			 */
			if (IS_AKM_SUITE_FT(sec)) {
				wl_bss_roaming_done(cfg, ndev, e, data);
			}
			/* Roam timer is deleted mostly from wl_cfg80211_change_station
			 * after roaming is finished successfully. We need to delete
			 * the timer from here only for some security types that aren't
			 * using wl_cfg80211_change_station to authorize SCB
			 */
			if (IS_AKM_SUITE_FT(sec) || IS_AKM_SUITE_CCKM(sec)) {
				wl_del_roam_timeout(cfg);
			}
#else
			wl_bss_roaming_done(cfg, ndev, e, data);
#endif /* DHD_LOSSLESS_ROAMING */
		} else {
			wl_bss_connect_done(cfg, ndev, e, data, true);
		}
		act = true;
		wl_update_prof(cfg, ndev, e, &act, WL_PROF_ACT);
		wl_update_prof(cfg, ndev, NULL, (const void *)&e->addr, WL_PROF_BSSID);

		if (ndev == bcmcfg_to_prmry_ndev(cfg)) {
			wl_vndr_ies_get_vendor_oui(cfg, ndev, NULL, 0);
		}
	}
#ifdef DHD_LOSSLESS_ROAMING
	else if ((event == WLC_E_ROAM || event == WLC_E_BSSID) && status != WLC_E_STATUS_SUCCESS) {
		wl_del_roam_timeout(cfg);
	}
#endif
	return err;
}

#ifdef CUSTOM_EVENT_PM_WAKE
uint32 last_dpm_upd_time = 0;	/* ms */
#define DPM_UPD_LMT_TIME	25000	/* ms */
#define DPM_UPD_LMT_RSSI	-85	/* dbm */

static s32
wl_check_pmstatus(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	s32 err = BCME_OK;
	struct net_device *ndev = NULL;
	u8 *pbuf = NULL;
	uint32 cur_dpm_upd_time = 0;
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);
	s32 rssi;
	scb_val_t scb_val;

	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);

	pbuf = kzalloc(WLC_IOCTL_MEDLEN, GFP_KERNEL);
	if (pbuf == NULL) {
		WL_ERR(("failed to allocate local pbuf\n"));
		return -ENOMEM;
	}

	err = wldev_iovar_getbuf_bsscfg(ndev, "dump",
		"pm", strlen("pm"), pbuf, WLC_IOCTL_MEDLEN, 0, &cfg->ioctl_buf_sync);

	if (err) {
		WL_ERR(("dump ioctl err = %d", err));
	} else {
		WL_ERR(("PM status : %s\n", pbuf));
	}

	if (pbuf) {
		kfree(pbuf);
	}

	if (dhd->early_suspended) {
		/* LCD off */
		memset(&scb_val, 0, sizeof(scb_val_t));
		scb_val.val = 0;
		err = wldev_ioctl_get(ndev, WLC_GET_RSSI, &scb_val, sizeof(scb_val_t));
		if (err) {
			WL_ERR(("Could not get rssi (%d)\n", err));
		}
		rssi = wl_rssi_offset(dtoh32(scb_val.val));
		WL_ERR(("[%s] RSSI %d dBm\n", ndev->name, rssi));
		if (rssi > DPM_UPD_LMT_RSSI) {
			return err;
		}
	} else {
		/* LCD on */
		return err;
	}

	if (last_dpm_upd_time == 0) {
		last_dpm_upd_time = OSL_SYSUPTIME();
	} else {
		cur_dpm_upd_time = OSL_SYSUPTIME();
		if (cur_dpm_upd_time - last_dpm_upd_time < DPM_UPD_LMT_TIME) {
			scb_val_t scbval;
			bzero(&scbval, sizeof(scb_val_t));

			err = wldev_ioctl_set(ndev, WLC_DISASSOC, &scbval, sizeof(scb_val_t));
			if (err < 0) {
				WL_ERR(("%s: Disassoc error %d\n", __FUNCTION__, err));
				return err;
			}
			WL_ERR(("%s: Force Disassoc due to updated DPM event.\n", __FUNCTION__));

			last_dpm_upd_time = 0;
		} else {
			last_dpm_upd_time = cur_dpm_upd_time;
		}
	}

	return err;
}
#endif	/* CUSTOM_EVENT_PM_WAKE */

#ifdef QOS_MAP_SET
/* get user priority table */
uint8 *
wl_get_up_table(dhd_pub_t * dhdp, int idx)
{
	struct net_device *ndev;
	struct bcm_cfg80211 *cfg;

	ndev = dhd_idx2net(dhdp, idx);
	if (ndev) {
		cfg = wl_get_cfg(ndev);
		if (cfg)
			return (uint8 *)(cfg->up_table);
	}

	return NULL;
}
#endif /* QOS_MAP_SET */

#if defined(DHD_LOSSLESS_ROAMING) || defined(DBG_PKT_MON)
static s32
wl_notify_roam_prep_status(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	struct wl_security *sec;
	struct net_device *ndev;
#if defined(DHD_LOSSLESS_ROAMING) || defined(DBG_PKT_MON)
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
#endif /* DHD_LOSSLESS_ROAMING || DBG_PKT_MON */
	u32 status = ntoh32(e->status);
	u32 reason = ntoh32(e->reason);

	BCM_REFERENCE(sec);

	if (status == WLC_E_STATUS_SUCCESS && reason != WLC_E_REASON_INITIAL_ASSOC) {
		WL_ERR(("Attempting roam with reason code : %d\n", reason));
	}

	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);

#ifdef DBG_PKT_MON
	if (ndev == bcmcfg_to_prmry_ndev(cfg)) {
		DHD_DBG_PKT_MON_STOP(dhdp);
		DHD_DBG_PKT_MON_START(dhdp);
	}
#endif /* DBG_PKT_MON */

#ifdef DHD_LOSSLESS_ROAMING
sec = wl_read_prof(cfg, ndev, WL_PROF_SEC);
	/* Disable Lossless Roaming for specific AKM suite
	 * Any other AKM suite can be added below if transition time
	 * is delayed because of Lossless Roaming
	 * and it causes any certication failure
	 */
	if (IS_AKM_SUITE_FT(sec)) {
		return BCME_OK;
	}
	dhdp->dequeue_prec_map = 1 << PRIO_8021D_NC;
	/* Restore flow control  */
	dhd_txflowcontrol(dhdp, ALL_INTERFACES, OFF);

	mod_timer(&cfg->roam_timeout, jiffies + msecs_to_jiffies(WL_ROAM_TIMEOUT_MS));
#endif /* DHD_LOSSLESS_ROAMING */
	return BCME_OK;

}
#endif /* DHD_LOSSLESS_ROAMING || DBG_PKT_MON */
#ifdef ENABLE_TEMP_THROTTLING
static s32
wl_check_rx_throttle_status(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	s32 err = 0;
	u32 status = ntoh32(e->status);
	u32 reason = ntoh32(e->reason);

	WL_ERR_EX(("RX THROTTLE : status=%d, reason=0x%x\n", status, reason));

	return err;
}
#endif /* ENABLE_TEMP_THROTTLING */

static s32 wl_get_assoc_ies(struct bcm_cfg80211 *cfg, struct net_device *ndev)
{
	wl_assoc_info_t assoc_info;
	struct wl_connect_info *conn_info = wl_to_conn(cfg);
	s32 err = 0;
#ifdef QOS_MAP_SET
	bcm_tlv_t * qos_map_ie = NULL;
#endif /* QOS_MAP_SET */

	WL_DBG(("Enter \n"));
	err = wldev_iovar_getbuf(ndev, "assoc_info", NULL, 0, cfg->extra_buf,
		WL_ASSOC_INFO_MAX, NULL);
	if (unlikely(err)) {
		WL_ERR(("could not get assoc info (%d)\n", err));
		return err;
	}
	memcpy(&assoc_info, cfg->extra_buf, sizeof(wl_assoc_info_t));
	assoc_info.req_len = htod32(assoc_info.req_len);
	assoc_info.resp_len = htod32(assoc_info.resp_len);
	assoc_info.flags = htod32(assoc_info.flags);
	if (conn_info->req_ie_len) {
		conn_info->req_ie_len = 0;
		bzero(conn_info->req_ie, sizeof(conn_info->req_ie));
	}
	if (conn_info->resp_ie_len) {
		conn_info->resp_ie_len = 0;
		bzero(conn_info->resp_ie, sizeof(conn_info->resp_ie));
	}
	if (assoc_info.req_len) {
		err = wldev_iovar_getbuf(ndev, "assoc_req_ies", NULL, 0, cfg->extra_buf,
			WL_ASSOC_INFO_MAX, NULL);
		if (unlikely(err)) {
			WL_ERR(("could not get assoc req (%d)\n", err));
			return err;
		}
		conn_info->req_ie_len = assoc_info.req_len - sizeof(struct dot11_assoc_req);
		if (assoc_info.flags & WLC_ASSOC_REQ_IS_REASSOC) {
			conn_info->req_ie_len -= ETHER_ADDR_LEN;
		}
		if (conn_info->req_ie_len <= MAX_REQ_LINE)
			memcpy(conn_info->req_ie, cfg->extra_buf, conn_info->req_ie_len);
		else {
			WL_ERR(("IE size %d above max %d size \n",
				conn_info->req_ie_len, MAX_REQ_LINE));
			return err;
		}
	} else {
		conn_info->req_ie_len = 0;
	}
	if (assoc_info.resp_len) {
		err = wldev_iovar_getbuf(ndev, "assoc_resp_ies", NULL, 0, cfg->extra_buf,
			WL_ASSOC_INFO_MAX, NULL);
		if (unlikely(err)) {
			WL_ERR(("could not get assoc resp (%d)\n", err));
			return err;
		}
		conn_info->resp_ie_len = assoc_info.resp_len -sizeof(struct dot11_assoc_resp);
		if (conn_info->resp_ie_len <= MAX_REQ_LINE) {
			memcpy(conn_info->resp_ie, cfg->extra_buf, conn_info->resp_ie_len);
		} else {
			WL_ERR(("IE size %d above max %d size \n",
				conn_info->resp_ie_len, MAX_REQ_LINE));
			return err;
		}

#ifdef QOS_MAP_SET
		/* find qos map set ie */
		if ((qos_map_ie = bcm_parse_tlvs(conn_info->resp_ie, conn_info->resp_ie_len,
				DOT11_MNG_QOS_MAP_ID)) != NULL) {
			WL_DBG((" QoS map set IE found in assoc response\n"));
			if (!cfg->up_table) {
				cfg->up_table = kmalloc(UP_TABLE_MAX, GFP_KERNEL);
			}
			wl_set_up_table(cfg->up_table, qos_map_ie);
		} else {
			kfree(cfg->up_table);
			cfg->up_table = NULL;
		}
#endif /* QOS_MAP_SET */
	} else {
		conn_info->resp_ie_len = 0;
	}
	WL_DBG(("req len (%d) resp len (%d)\n", conn_info->req_ie_len,
		conn_info->resp_ie_len));

	return err;
}

static s32 wl_ch_to_chanspec(struct net_device *dev, int ch, struct wl_join_params *join_params,
        size_t *join_params_size)
{
	s32 bssidx = -1;
	chanspec_t chanspec = 0, chspec;

	if (ch != 0) {
		struct bcm_cfg80211 *cfg =
			(struct bcm_cfg80211 *)wiphy_priv(dev->ieee80211_ptr->wiphy);
			join_params->params.chanspec_num = 1;
			join_params->params.chanspec_list[0] = ch;

			if (join_params->params.chanspec_list[0] <= CH_MAX_2G_CHANNEL)
				chanspec |= WL_CHANSPEC_BAND_2G;
			else
				chanspec |= WL_CHANSPEC_BAND_5G;

			/* Get the min_bw set for the interface */
			chspec = wl_cfg80211_ulb_get_min_bw_chspec(cfg, dev->ieee80211_ptr, bssidx);
			if (chspec == INVCHANSPEC) {
				WL_ERR(("Invalid chanspec \n"));
				return -EINVAL;
			}
			chanspec |= chspec;
			chanspec |= WL_CHANSPEC_CTL_SB_NONE;

			*join_params_size += WL_ASSOC_PARAMS_FIXED_SIZE +
				join_params->params.chanspec_num * sizeof(chanspec_t);

			join_params->params.chanspec_list[0]  &= WL_CHANSPEC_CHAN_MASK;
			join_params->params.chanspec_list[0] |= chanspec;
			join_params->params.chanspec_list[0] =
				wl_chspec_host_to_driver(join_params->params.chanspec_list[0]);

			join_params->params.chanspec_num =
				htod32(join_params->params.chanspec_num);

		WL_DBG(("join_params->params.chanspec_list[0]= %X, %d channels\n",
			join_params->params.chanspec_list[0],
			join_params->params.chanspec_num));
	}
	return 0;
}

static s32 wl_update_bss_info(struct bcm_cfg80211 *cfg, struct net_device *ndev, bool roam)
{
	struct cfg80211_bss *bss;
	struct wl_bss_info *bi;
	struct wlc_ssid *ssid;
	struct bcm_tlv *tim;
	s32 beacon_interval;
	s32 dtim_period;
	size_t ie_len;
	u8 *ie;
	u8 *curbssid;
	s32 err = 0;
	struct wiphy *wiphy;
	u32 channel;
	char *buf;
	u32 freq, band;

	wiphy = bcmcfg_to_wiphy(cfg);

	ssid = (struct wlc_ssid *)wl_read_prof(cfg, ndev, WL_PROF_SSID);
	curbssid = wl_read_prof(cfg, ndev, WL_PROF_BSSID);
	bss = CFG80211_GET_BSS(wiphy, NULL, curbssid,
		ssid->SSID, ssid->SSID_len);
	buf = kzalloc(WL_EXTRA_BUF_MAX, GFP_ATOMIC);
	if (!buf) {
		WL_ERR(("buffer alloc failed.\n"));
		return BCME_NOMEM;
	}
	mutex_lock(&cfg->usr_sync);
	*(u32 *)buf = htod32(WL_EXTRA_BUF_MAX);
	err = wldev_ioctl_get(ndev, WLC_GET_BSS_INFO, buf, WL_EXTRA_BUF_MAX);
	if (unlikely(err)) {
		WL_ERR(("Could not get bss info %d\n", err));
		goto update_bss_info_out;
	}
	bi = (struct wl_bss_info *)(buf + 4);
	channel = wf_chspec_ctlchan(wl_chspec_driver_to_host(bi->chanspec));
	wl_update_prof(cfg, ndev, NULL, &channel, WL_PROF_CHAN);

	if (!bss) {
		WL_DBG(("Could not find the AP\n"));
		if (memcmp(bi->BSSID.octet, curbssid, ETHER_ADDR_LEN)) {
			WL_ERR(("Bssid doesn't match\n"));
			err = -EIO;
			goto update_bss_info_out;
		}
		err = wl_inform_single_bss(cfg, bi, roam);
		if (unlikely(err))
			goto update_bss_info_out;

		ie = ((u8 *)bi) + bi->ie_offset;
		ie_len = bi->ie_length;
		beacon_interval = cpu_to_le16(bi->beacon_period);
	} else {
		WL_DBG(("Found the AP in the list - BSSID %pM\n", bss->bssid));
#if LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 38)
		freq = ieee80211_channel_to_frequency(channel);
#else
		band = (channel <= CH_MAX_2G_CHANNEL) ? IEEE80211_BAND_2GHZ : IEEE80211_BAND_5GHZ;
		freq = ieee80211_channel_to_frequency(channel, band);
#endif
		bss->channel = ieee80211_get_channel(wiphy, freq);
#if defined(WL_CFG80211_P2P_DEV_IF)
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
		ie = (u8 *)bss->ies->data;
		ie_len = bss->ies->len;
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#else
		ie = bss->information_elements;
		ie_len = bss->len_information_elements;
#endif /* WL_CFG80211_P2P_DEV_IF */
		beacon_interval = bss->beacon_interval;

		CFG80211_PUT_BSS(wiphy, bss);
	}

	tim = bcm_parse_tlvs(ie, ie_len, WLAN_EID_TIM);
	if (tim) {
		dtim_period = tim->data[1];
	} else {
		/*
		* active scan was done so we could not get dtim
		* information out of probe response.
		* so we speficially query dtim information.
		*/
		dtim_period = 0;
		err = wldev_ioctl_get(ndev, WLC_GET_DTIMPRD,
			&dtim_period, sizeof(dtim_period));
		if (unlikely(err)) {
			WL_ERR(("WLC_GET_DTIMPRD error (%d)\n", err));
			goto update_bss_info_out;
		}
	}

	wl_update_prof(cfg, ndev, NULL, &beacon_interval, WL_PROF_BEACONINT);
	wl_update_prof(cfg, ndev, NULL, &dtim_period, WL_PROF_DTIMPERIOD);

update_bss_info_out:
	if (unlikely(err)) {
		WL_ERR(("Failed with error %d\n", err));
	}

	kfree(buf);
	mutex_unlock(&cfg->usr_sync);
	return err;
}

static s32
wl_bss_roaming_done(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *e, void *data)
{
	struct wl_connect_info *conn_info = wl_to_conn(cfg);
	s32 err = 0;
	u8 *curbssid;
	u32 *channel;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39))
	struct wiphy *wiphy = bcmcfg_to_wiphy(cfg);
	struct ieee80211_supported_band *band;
	struct ieee80211_channel *notify_channel = NULL;
	u32 freq;
	u32 cur_channel, chanspec;
#endif 
#if defined(WLADPS_SEAK_AP_WAR) || defined(WBTEXT)
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
#endif /* WLADPS_SEAK_AP_WAR || WBTEXT */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0))
	struct cfg80211_roam_info roam_info = {};
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)) */
#ifdef WLFBT
	uint32 data_len = 0;
	if (data)
		data_len = ntoh32(e->datalen);
#endif /* WLFBT */

#ifdef WLADPS_SEAK_AP_WAR
	BCM_REFERENCE(dhdp);
#endif /* WLADPS_SEAK_AP_WAR */

	curbssid = wl_read_prof(cfg, ndev, WL_PROF_BSSID);
	channel = (u32 *)wl_read_prof(cfg, ndev, WL_PROF_CHAN);

	/* Skip calling cfg80211_roamed If the channels are same and
	 * the current bssid/last_roamed_bssid & the new bssid are same
	 * Also clear timer roam_timeout.
	 * Only used on BCM4359 devices.
	 */
	err = wldev_iovar_getint(ndev, "chanspec", (s32 *)&chanspec);
	if (unlikely(err)) {
		return  err;
	}
	cur_channel = wf_chspec_ctlchan(wl_chspec_driver_to_host(chanspec));
	WL_DBG(("%d=>%d, %pM(%pM)=>%pM\n", *channel, cur_channel,
		curbssid, (const u8*)(&cfg->last_roamed_addr), (const u8*)(&e->addr)));
	if ((*channel == cur_channel) &&
			((memcmp(curbssid, &e->addr, ETHER_ADDR_LEN) == 0) ||
			(memcmp(&cfg->last_roamed_addr,	&e->addr, ETHER_ADDR_LEN) == 0))) {
		WL_DBG(("BSS already present, Skipping roamed event to"
		" upper layer\n"));
#ifdef DHD_LOSSLESS_ROAMING
		wl_del_roam_timeout(cfg);
#endif  /* DHD_LOSSLESS_ROAMING */
		return  err;
	}

	wl_get_assoc_ies(cfg, ndev);
	wl_update_prof(cfg, ndev, NULL, (const void *)(e->addr.octet), WL_PROF_BSSID);
	curbssid = wl_read_prof(cfg, ndev, WL_PROF_BSSID);
	wl_update_bss_info(cfg, ndev, true);
	wl_update_pmklist(ndev, cfg->pmk_list, err);

	channel = (u32 *)wl_read_prof(cfg, ndev, WL_PROF_CHAN);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39))
	/* channel info for cfg80211_roamed introduced in 2.6.39-rc1 */
	if (*channel <= CH_MAX_2G_CHANNEL)
		band = wiphy->bands[IEEE80211_BAND_2GHZ];
	else
		band = wiphy->bands[IEEE80211_BAND_5GHZ];
	freq = ieee80211_channel_to_frequency(*channel, band->band);
	notify_channel = ieee80211_get_channel(wiphy, freq);
#endif 
#ifdef WLFBT
	/* back up the given FBT key for the further supplicant request,
	 * currently not checking the FBT is enabled for current BSS in DHD,
	 * because the supplicant decides to take it or not.
	 */
	if (data && (data_len == FBT_KEYLEN)) {
		memcpy(cfg->fbt_key, data, FBT_KEYLEN);
	}
#endif /* WLFBT */
#ifdef WLADPS_SEAK_AP_WAR
	if ((dhdp->op_mode & DHD_FLAG_STA_MODE) &&
			(!dhdp->disabled_adps)) {
		bool find = FALSE;
		uint8 enable_mode;
		if (!memcmp(curbssid, (u8*)CAMEO_MAC_PREFIX, MAC_PREFIX_LEN)) {
			find = wl_find_vndr_ies_specific_vender(cfg, ndev, ATHEROS_OUI);
		}
		enable_mode = (find == TRUE) ? OFF : ON;
		wl_set_adps_mode(cfg, ndev, enable_mode);
	}
#endif /* WLADPS_SEAK_AP_WAR */
	WL_MSG(ndev->name, "succeeded to " MACDBG " (ch:%d)\n",
		MAC2STRDBG((const u8*)(&e->addr)), *channel);
	wl_cfg80211_check_in4way(cfg, ndev, 0, WL_EXT_STATUS_CONNECTED, NULL);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0))
	roam_info.channel = notify_channel;
	roam_info.bssid = curbssid;
	roam_info.req_ie = conn_info->req_ie;
	roam_info.req_ie_len = conn_info->req_ie_len;
	roam_info.resp_ie = conn_info->resp_ie;
	roam_info.resp_ie_len = conn_info->resp_ie_len;
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)) */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0))
	cfg80211_roamed(ndev, &roam_info, GFP_KERNEL);
#else
	cfg80211_roamed(ndev,
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39))
		notify_channel,
#endif
		curbssid,
		conn_info->req_ie, conn_info->req_ie_len,
		conn_info->resp_ie, conn_info->resp_ie_len, GFP_KERNEL);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)) */
	WL_DBG(("Report roaming result\n"));

	memcpy(&cfg->last_roamed_addr, &e->addr, ETHER_ADDR_LEN);
	wl_set_drv_status(cfg, CONNECTED, ndev);

#if defined(DHD_ENABLE_BIGDATA_LOGGING)
	cfg->roam_count++;
#endif /* DHD_ENABLE_BIGDATA_LOGGING */

#ifdef WBTEXT
	if (dhdp->wbtext_support) {
		/* set wnm_keepalives_max_idle after association */
		wl_cfg80211_wbtext_set_wnm_maxidle(cfg, ndev);
		/* send nbr request or BTM query to update RCC */
		wl_cfg80211_wbtext_update_rcc(cfg, ndev);
	}
#endif /* WBTEXT */

	return err;
}

static bool
wl_cfg80211_verify_bss(struct bcm_cfg80211 *cfg, struct net_device *ndev)
{
	struct cfg80211_bss *bss;
	struct wiphy *wiphy;
	struct wlc_ssid *ssid;
	uint8 *curbssid;
	int count = 0;
	int ret = false;

	wiphy = bcmcfg_to_wiphy(cfg);
	ssid = (struct wlc_ssid *)wl_read_prof(cfg, ndev, WL_PROF_SSID);
	curbssid = wl_read_prof(cfg, ndev, WL_PROF_BSSID);
	if (!ssid) {
		WL_ERR(("No SSID found in the saved profile \n"));
		return false;
	}

	do {
		bss = CFG80211_GET_BSS(wiphy, NULL, curbssid,
			ssid->SSID, ssid->SSID_len);
		if (bss || (count > 5)) {
			break;
		}

		count++;
		msleep(100);
	} while (bss == NULL);

	WL_DBG(("cfg80211 bss_ptr:%p loop_cnt:%d\n", bss, count));
	if (bss) {
		/* Update the reference count after use */
		CFG80211_PUT_BSS(wiphy, bss);
		ret = true;
	}

	return ret;
}

static s32
wl_bss_connect_done(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *e, void *data, bool completed)
{
	struct wl_connect_info *conn_info = wl_to_conn(cfg);
	struct wl_security *sec = wl_read_prof(cfg, ndev, WL_PROF_SEC);
	s32 err = 0;
	u8 *curbssid = wl_read_prof(cfg, ndev, WL_PROF_BSSID);
	dhd_pub_t *dhdp;

	dhdp = (dhd_pub_t *)(cfg->pub);
	BCM_REFERENCE(dhdp);

	if (!sec) {
		WL_ERR(("sec is NULL\n"));
		return -ENODEV;
	}
	WL_DBG((" enter\n"));
#ifdef ESCAN_RESULT_PATCH
	if (wl_get_drv_status(cfg, CONNECTED, ndev)) {
		if (memcmp(curbssid, connect_req_bssid, ETHER_ADDR_LEN) == 0) {
			WL_DBG((" Connected event of connected device e=%d s=%d, ignore it\n",
				ntoh32(e->event_type), ntoh32(e->status)));
			return err;
		}
	}
	if (memcmp(curbssid, broad_bssid, ETHER_ADDR_LEN) == 0 &&
		memcmp(broad_bssid, connect_req_bssid, ETHER_ADDR_LEN) != 0) {
		WL_DBG(("copy bssid\n"));
		memcpy(curbssid, connect_req_bssid, ETHER_ADDR_LEN);
	}

#else
	if (cfg->scan_request) {
		wl_notify_escan_complete(cfg, ndev, true, true);
	}
#endif /* ESCAN_RESULT_PATCH */
	if (wl_get_drv_status(cfg, CONNECTING, ndev)) {
		wl_cfg80211_scan_abort(cfg);
		wl_clr_drv_status(cfg, CONNECTING, ndev);
		if (completed) {
			wl_get_assoc_ies(cfg, ndev);
			wl_update_prof(cfg, ndev, NULL, (const void *)(e->addr.octet),
				WL_PROF_BSSID);
			curbssid = wl_read_prof(cfg, ndev, WL_PROF_BSSID);
			wl_update_bss_info(cfg, ndev, false);
			wl_update_pmklist(ndev, cfg->pmk_list, err);
			wl_set_drv_status(cfg, CONNECTED, ndev);
#ifdef WLADPS_SEAK_AP_WAR
			if ((dhdp->op_mode & DHD_FLAG_STA_MODE) &&
					(!dhdp->disabled_adps)) {
				bool find = FALSE;
				uint8 enable_mode;
				if (!memcmp(curbssid, (u8*)CAMEO_MAC_PREFIX, MAC_PREFIX_LEN)) {
					find = wl_find_vndr_ies_specific_vender(cfg,
						ndev, ATHEROS_OUI);
				}
				enable_mode = (find == TRUE) ? OFF : ON;
				wl_set_adps_mode(cfg, ndev, enable_mode);
			}
#endif /* WLADPS_SEAK_AP_WAR */
			if (ndev != bcmcfg_to_prmry_ndev(cfg)) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
				init_completion(&cfg->iface_disable);
#else
				/* reinitialize completion to clear previous count */
				INIT_COMPLETION(cfg->iface_disable);
#endif
			}
#ifdef CUSTOM_SET_CPUCORE
			if (wl_get_chan_isvht80(ndev, dhdp)) {
				if (ndev == bcmcfg_to_prmry_ndev(cfg))
					dhdp->chan_isvht80 |= DHD_FLAG_STA_MODE; /* STA mode */
				else if (is_p2p_group_iface(ndev->ieee80211_ptr))
					dhdp->chan_isvht80 |= DHD_FLAG_P2P_MODE; /* p2p mode */
				dhd_set_cpucore(dhdp, TRUE);
			}
#endif /* CUSTOM_SET_CPUCORE */
			memset(&cfg->last_roamed_addr, 0, ETHER_ADDR_LEN);
		}

		if (completed && (wl_cfg80211_verify_bss(cfg, ndev) != true)) {
			/* If bss entry is not available in the cfg80211 bss cache
			 * the wireless stack will complain and won't populate
			 * wdev->current_bss ptr
			 */
			WL_ERR(("BSS entry not found. Indicate assoc event failure\n"));
			completed = false;
			sec->auth_assoc_res_status = WLAN_STATUS_UNSPECIFIED_FAILURE;
		}
		if (completed) {
			WL_INFORM(("Report connect result - connection succeeded\n"));
			wl_cfg80211_check_in4way(cfg, ndev, 0, WL_EXT_STATUS_CONNECTED, NULL);
		} else {
			WL_MSG(ndev->name, "Report connect result - connection failed\n");
			wl_cfg80211_check_in4way(cfg, ndev, NO_SCAN_IN4WAY|NO_BTC_IN4WAY|WAIT_DISCONNECTED,
				WL_EXT_STATUS_DISCONNECTED, NULL);
		}
		cfg80211_connect_result(ndev,
			curbssid,
			conn_info->req_ie,
			conn_info->req_ie_len,
			conn_info->resp_ie,
			conn_info->resp_ie_len,
			completed ? WLAN_STATUS_SUCCESS :
			(sec->auth_assoc_res_status) ?
			sec->auth_assoc_res_status :
			WLAN_STATUS_UNSPECIFIED_FAILURE,
			GFP_KERNEL);
	}
#ifdef CONFIG_TCPACK_FASTTX
	if (wl_get_chan_isvht80(ndev, dhdp))
		wldev_iovar_setint(ndev, "tcpack_fast_tx", 0);
	else
		wldev_iovar_setint(ndev, "tcpack_fast_tx", 1);
#endif /* CONFIG_TCPACK_FASTTX */

	return err;
}

static s32
wl_notify_mic_status(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	struct net_device *ndev = NULL;
	u16 flags = ntoh16(e->flags);
	enum nl80211_key_type key_type;

	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);

	mutex_lock(&cfg->usr_sync);
	if (flags & WLC_EVENT_MSG_GROUP)
		key_type = NL80211_KEYTYPE_GROUP;
	else
		key_type = NL80211_KEYTYPE_PAIRWISE;

	cfg80211_michael_mic_failure(ndev, (const u8 *)&e->addr, key_type, -1,
		NULL, GFP_KERNEL);
	mutex_unlock(&cfg->usr_sync);

	return 0;
}

#ifdef BT_WIFI_HANDOVER
static s32
wl_notify_bt_wifi_handover_req(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	struct net_device *ndev = NULL;
	u32 event = ntoh32(e->event_type);
	u32 datalen = ntoh32(e->datalen);
	s32 err;

	WL_ERR(("wl_notify_bt_wifi_handover_req: event_type : %d, datalen : %d\n", event, datalen));
	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);
	err = wl_genl_send_msg(ndev, event, data, (u16)datalen, 0, 0);

	return err;
}
#endif /* BT_WIFI_HANDOVER */

#ifdef PNO_SUPPORT
static s32
wl_notify_pfn_status(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	struct net_device *ndev = NULL;
#ifdef GSCAN_SUPPORT
	void *ptr;
	int send_evt_bytes = 0;
	u32 event = be32_to_cpu(e->event_type);
	struct wiphy *wiphy = bcmcfg_to_wiphy(cfg);
#endif /* GSCAN_SUPPORT */

	WL_ERR((">>> PNO Event\n"));

	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);
#ifdef GSCAN_SUPPORT
	ptr = dhd_dev_process_epno_result(ndev, data, event, &send_evt_bytes);
	if (ptr) {
		wl_cfgvendor_send_async_event(wiphy, ndev,
			GOOGLE_SCAN_EPNO_EVENT, ptr, send_evt_bytes);
		kfree(ptr);
	}
	if (!dhd_dev_is_legacy_pno_enabled(ndev))
		return 0;
#endif /* GSCAN_SUPPORT */


#ifndef WL_SCHED_SCAN
	mutex_lock(&cfg->usr_sync);
	/* TODO: Use cfg80211_sched_scan_results(wiphy); */
	CFG80211_DISCONNECTED(ndev, 0, NULL, 0, false, GFP_KERNEL);
	mutex_unlock(&cfg->usr_sync);
#else
	/* If cfg80211 scheduled scan is supported, report the pno results via sched
	 * scan results
	 */
	wl_notify_sched_scan_results(cfg, ndev, e, data);
#endif /* WL_SCHED_SCAN */
	return 0;
}
#endif /* PNO_SUPPORT */

#ifdef GSCAN_SUPPORT
static s32
wl_notify_gscan_event(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	s32 err = 0;
	u32 event = be32_to_cpu(e->event_type);
	void *ptr;
	int send_evt_bytes = 0;
	int event_type;
	struct net_device *ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);
	struct wiphy *wiphy = bcmcfg_to_wiphy(cfg);
	u32 len = ntoh32(e->datalen);

	switch (event) {
		case WLC_E_PFN_BEST_BATCHING:
			err = dhd_dev_retrieve_batch_scan(ndev);
			if (err < 0) {
				WL_ERR(("Batch retrieval already in progress %d\n", err));
			} else {
				event_type = WIFI_SCAN_THRESHOLD_NUM_SCANS;
				if (data && len) {
					event_type = *((int *)data);
				}
				wl_cfgvendor_send_async_event(wiphy, ndev,
				    GOOGLE_GSCAN_BATCH_SCAN_EVENT,
				     &event_type, sizeof(int));
			}
			break;
		case WLC_E_PFN_SCAN_COMPLETE:
			event_type = WIFI_SCAN_COMPLETE;
			wl_cfgvendor_send_async_event(wiphy, ndev,
				GOOGLE_SCAN_COMPLETE_EVENT,
				&event_type, sizeof(int));
			break;
		case WLC_E_PFN_BSSID_NET_FOUND:
			ptr = dhd_dev_hotlist_scan_event(ndev, data, &send_evt_bytes,
			      HOTLIST_FOUND);
			if (ptr) {
				wl_cfgvendor_send_hotlist_event(wiphy, ndev,
				 ptr, send_evt_bytes, GOOGLE_GSCAN_GEOFENCE_FOUND_EVENT);
				dhd_dev_gscan_hotlist_cache_cleanup(ndev, HOTLIST_FOUND);
			} else {
				err = -ENOMEM;
			}
			break;
		case WLC_E_PFN_BSSID_NET_LOST:
			/* WLC_E_PFN_BSSID_NET_LOST is conflict shared with WLC_E_PFN_SCAN_ALLGONE
			 * We currently do not use WLC_E_PFN_SCAN_ALLGONE, so if we get it, ignore
			 */
			if (len) {
				ptr = dhd_dev_hotlist_scan_event(ndev, data, &send_evt_bytes,
				            HOTLIST_LOST);
				if (ptr) {
					wl_cfgvendor_send_hotlist_event(wiphy, ndev,
					 ptr, send_evt_bytes, GOOGLE_GSCAN_GEOFENCE_LOST_EVENT);
					dhd_dev_gscan_hotlist_cache_cleanup(ndev, HOTLIST_LOST);
				} else {
					err = -ENOMEM;
				}
			} else {
				err = -EINVAL;
			}
			break;
		case WLC_E_PFN_GSCAN_FULL_RESULT:
			ptr = dhd_dev_process_full_gscan_result(ndev, data, len, &send_evt_bytes);
			if (ptr) {
				wl_cfgvendor_send_async_event(wiphy, ndev,
				    GOOGLE_SCAN_FULL_RESULTS_EVENT, ptr, send_evt_bytes);
				kfree(ptr);
			} else {
				err = -ENOMEM;
			}
			break;
		case WLC_E_PFN_SSID_EXT:
			ptr = dhd_dev_process_epno_result(ndev, data, event, &send_evt_bytes);
			if (ptr) {
				wl_cfgvendor_send_async_event(wiphy, ndev,
				    GOOGLE_SCAN_EPNO_EVENT, ptr, send_evt_bytes);
				kfree(ptr);
			} else {
				err = -ENOMEM;
			}
			break;
		default:
			WL_ERR(("Unknown event %d\n", event));
			break;
	}
	return err;
}
#endif /* GSCAN_SUPPORT */

static s32
wl_notify_scan_status(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	struct channel_info channel_inform;
	struct wl_scan_results *bss_list;
	struct net_device *ndev = NULL;
	u32 len = WL_SCAN_BUF_MAX;
	s32 err = 0;
	unsigned long flags;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0))
	struct cfg80211_scan_info info;
#endif

	WL_DBG(("Enter \n"));
	if (!wl_get_drv_status(cfg, SCANNING, ndev)) {
		WL_ERR(("scan is not ready \n"));
		return err;
	}
	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);

	mutex_lock(&cfg->usr_sync);
	wl_clr_drv_status(cfg, SCANNING, ndev);
	memset(&channel_inform, 0, sizeof(channel_inform));
	err = wldev_ioctl_get(ndev, WLC_GET_CHANNEL, &channel_inform,
		sizeof(channel_inform));
	if (unlikely(err)) {
		WL_ERR(("scan busy (%d)\n", err));
		goto scan_done_out;
	}
	channel_inform.scan_channel = dtoh32(channel_inform.scan_channel);
	if (unlikely(channel_inform.scan_channel)) {

		WL_DBG(("channel_inform.scan_channel (%d)\n",
			channel_inform.scan_channel));
	}
	cfg->bss_list = cfg->scan_results;
	bss_list = cfg->bss_list;
	memset(bss_list, 0, len);
	bss_list->buflen = htod32(len);
	err = wldev_ioctl_get(ndev, WLC_SCAN_RESULTS, bss_list, len);
	if (unlikely(err) && unlikely(!cfg->scan_suppressed)) {
		WL_ERR(("%s Scan_results error (%d)\n", ndev->name, err));
		err = -EINVAL;
		goto scan_done_out;
	}
	bss_list->buflen = dtoh32(bss_list->buflen);
	bss_list->version = dtoh32(bss_list->version);
	bss_list->count = dtoh32(bss_list->count);

	err = wl_inform_bss(cfg);

scan_done_out:
	del_timer_sync(&cfg->scan_timeout);
	spin_lock_irqsave(&cfg->cfgdrv_lock, flags);
	if (cfg->scan_request) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0))
		info.aborted = false;
		cfg80211_scan_done(cfg->scan_request, &info);
#else
		cfg80211_scan_done(cfg->scan_request, false);
#endif
		cfg->scan_request = NULL;
	}
	spin_unlock_irqrestore(&cfg->cfgdrv_lock, flags);
	WL_DBG(("cfg80211_scan_done\n"));
	mutex_unlock(&cfg->usr_sync);
	return err;
}

static s32
wl_frame_get_mgmt(u16 fc, const struct ether_addr *da,
	const struct ether_addr *sa, const struct ether_addr *bssid,
	u8 **pheader, u32 *body_len, u8 *pbody)
{
	struct dot11_management_header *hdr;
	u32 totlen = 0;
	s32 err = 0;
	u8 *offset;
	u32 prebody_len = *body_len;
	switch (fc) {
		case FC_ASSOC_REQ:
			/* capability , listen interval */
			totlen = DOT11_ASSOC_REQ_FIXED_LEN;
			*body_len += DOT11_ASSOC_REQ_FIXED_LEN;
			break;

		case FC_REASSOC_REQ:
			/* capability, listen inteval, ap address */
			totlen = DOT11_REASSOC_REQ_FIXED_LEN;
			*body_len += DOT11_REASSOC_REQ_FIXED_LEN;
			break;
	}
	totlen += DOT11_MGMT_HDR_LEN + prebody_len;
	*pheader = kzalloc(totlen, GFP_KERNEL);
	if (*pheader == NULL) {
		WL_ERR(("memory alloc failed \n"));
		return -ENOMEM;
	}
	hdr = (struct dot11_management_header *) (*pheader);
	hdr->fc = htol16(fc);
	hdr->durid = 0;
	hdr->seq = 0;
	offset = (u8*)(hdr + 1) + (totlen - DOT11_MGMT_HDR_LEN - prebody_len);
	bcopy((const char*)da, (u8*)&hdr->da, ETHER_ADDR_LEN);
	bcopy((const char*)sa, (u8*)&hdr->sa, ETHER_ADDR_LEN);
	bcopy((const char*)bssid, (u8*)&hdr->bssid, ETHER_ADDR_LEN);
	if ((pbody != NULL) && prebody_len)
		bcopy((const char*)pbody, offset, prebody_len);
	*body_len = totlen;
	return err;
}


void
wl_stop_wait_next_action_frame(struct bcm_cfg80211 *cfg, struct net_device *ndev, u8 bsscfgidx)
{
	s32 err = 0;

	if (wl_get_drv_status_all(cfg, FINDING_COMMON_CHANNEL)) {
		if (timer_pending(&cfg->p2p->listen_timer)) {
			del_timer_sync(&cfg->p2p->listen_timer);
		}
		if (cfg->afx_hdl != NULL) {
			if (cfg->afx_hdl->dev != NULL) {
				wl_clr_drv_status(cfg, SCANNING, cfg->afx_hdl->dev);
				wl_clr_drv_status(cfg, FINDING_COMMON_CHANNEL, cfg->afx_hdl->dev);
			}
			cfg->afx_hdl->peer_chan = WL_INVALID;
		}
		complete(&cfg->act_frm_scan);
		WL_DBG(("*** Wake UP ** Working afx searching is cleared\n"));
	} else if (wl_get_drv_status_all(cfg, SENDING_ACT_FRM)) {
		if (!(wl_get_p2p_status(cfg, ACTION_TX_COMPLETED) ||
			wl_get_p2p_status(cfg, ACTION_TX_NOACK)))
			wl_set_p2p_status(cfg, ACTION_TX_COMPLETED);

		WL_DBG(("*** Wake UP ** abort actframe iovar on bsscfxidx %d\n", bsscfgidx));
		/* if channel is not zero, "actfame" uses off channel scan.
		 * So abort scan for off channel completion.
		 */
		if (cfg->af_sent_channel) {
			/* Scan engine is not used for sending action frames in the latest driver
			 * branches. So, actframe_abort is used in the latest driver branches
			 * instead of scan abort.
			 * New driver branches:
			 *     Issue actframe_abort and it succeeded. So, don't execute scan abort.
			 * Old driver branches:
			 *     Issue actframe_abort and it fails. So, execute scan abort.
			 */
			err = wldev_iovar_setint_bsscfg(ndev, "actframe_abort", 1, bsscfgidx);
			if (err < 0) {
				wl_cfg80211_scan_abort(cfg);
			}
		}
	}
#ifdef WL_CFG80211_SYNC_GON
	else if (wl_get_drv_status_all(cfg, WAITING_NEXT_ACT_FRM_LISTEN)) {
		WL_DBG(("*** Wake UP ** abort listen for next af frame\n"));
		/* So abort scan to cancel listen */
		wl_cfg80211_scan_abort(cfg);
	}
#endif /* WL_CFG80211_SYNC_GON */
}

#if defined(WLTDLS)
bool wl_cfg80211_is_tdls_tunneled_frame(void *frame, u32 frame_len)
{
	unsigned char *data;

	if (frame == NULL) {
		WL_ERR(("Invalid frame \n"));
		return false;
	}

	if (frame_len < 5) {
		WL_ERR(("Invalid frame length [%d] \n", frame_len));
		return false;
	}

	data = frame;

	if (!memcmp(data, TDLS_TUNNELED_PRB_REQ, 5) ||
		!memcmp(data, TDLS_TUNNELED_PRB_RESP, 5)) {
		WL_DBG(("TDLS Vendor Specific Received type\n"));
		return true;
	}

	return false;
}
#endif /* WLTDLS */


int wl_cfg80211_get_ioctl_version(void)
{
	return ioctl_version;
}

static s32
wl_notify_rx_mgmt_frame(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	struct ieee80211_supported_band *band;
	struct wiphy *wiphy = bcmcfg_to_wiphy(cfg);
	struct ether_addr da;
	struct ether_addr bssid;
	bool isfree = false;
	s32 err = 0;
	s32 freq;
#if defined(TDLS_MSG_ONLY_WFD)
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
#endif /* TDLS_MSG_ONLY_WFD && BCMDONGLEHOST */
	struct net_device *ndev = NULL;
	wifi_p2p_pub_act_frame_t *act_frm = NULL;
	wifi_p2p_action_frame_t *p2p_act_frm = NULL;
	wifi_p2psd_gas_pub_act_frame_t *sd_act_frm = NULL;
	wl_event_rx_frame_data_t *rxframe;
	u32 event;
	u8 *mgmt_frame;
	u8 bsscfgidx;
	u32 mgmt_frame_len;
	u16 channel;
	if (ntoh32(e->datalen) < sizeof(wl_event_rx_frame_data_t)) {
		WL_ERR(("wrong datalen:%d\n", ntoh32(e->datalen)));
		return -EINVAL;
	}
	mgmt_frame_len = ntoh32(e->datalen) - sizeof(wl_event_rx_frame_data_t);
	event = ntoh32(e->event_type);
	bsscfgidx = e->bsscfgidx;
	rxframe = (wl_event_rx_frame_data_t *)data;
	if (!rxframe) {
		WL_ERR(("rxframe: NULL\n"));
		return -EINVAL;
	}
	channel = (ntoh16(rxframe->channel) & WL_CHANSPEC_CHAN_MASK);
	memset(&bssid, 0, ETHER_ADDR_LEN);
	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);
	if ((ndev->ieee80211_ptr->iftype != NL80211_IFTYPE_AP) &&
		(event == WLC_E_PROBREQ_MSG)) {
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"")
#endif
		struct net_info *iter, *next;
		for_each_ndev(cfg, iter, next) {
			if (iter->ndev && iter->wdev &&
					iter->wdev->iftype == NL80211_IFTYPE_AP) {
					ndev = iter->ndev;
					cfgdev =  ndev_to_cfgdev(ndev);
					break;
			}
		}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic pop")
#endif
	}

	if (channel <= CH_MAX_2G_CHANNEL)
		band = wiphy->bands[IEEE80211_BAND_2GHZ];
	else
		band = wiphy->bands[IEEE80211_BAND_5GHZ];
	if (!band) {
		WL_ERR(("No valid band\n"));
		return -EINVAL;
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 39)
	freq = ieee80211_channel_to_frequency(channel);
	(void)band->band;
#else
	freq = ieee80211_channel_to_frequency(channel, band->band);
#endif
	if (event == WLC_E_ACTION_FRAME_RX) {
		if ((err = wldev_iovar_getbuf_bsscfg(ndev, "cur_etheraddr",
			NULL, 0, cfg->ioctl_buf, WLC_IOCTL_SMLEN, bsscfgidx,
			&cfg->ioctl_buf_sync)) != BCME_OK) {
			WL_ERR(("WLC_GET_CUR_ETHERADDR failed, error %d\n", err));
			goto exit;
		}

		err = wldev_ioctl_get(ndev, WLC_GET_BSSID, &bssid, ETHER_ADDR_LEN);
		if (err < 0)
			 WL_ERR(("WLC_GET_BSSID error %d\n", err));
		memcpy(da.octet, cfg->ioctl_buf, ETHER_ADDR_LEN);
		err = wl_frame_get_mgmt(FC_ACTION, &da, &e->addr, &bssid,
			&mgmt_frame, &mgmt_frame_len,
			(u8 *)((wl_event_rx_frame_data_t *)rxframe + 1));
		if (err < 0) {
			WL_ERR(("Error in receiving action frame len %d channel %d freq %d\n",
				mgmt_frame_len, channel, freq));
			goto exit;
		}
		isfree = true;
		if (wl_cfgp2p_is_pub_action(&mgmt_frame[DOT11_MGMT_HDR_LEN],
			mgmt_frame_len - DOT11_MGMT_HDR_LEN)) {
			act_frm = (wifi_p2p_pub_act_frame_t *)
					(&mgmt_frame[DOT11_MGMT_HDR_LEN]);
		} else if (wl_cfgp2p_is_p2p_action(&mgmt_frame[DOT11_MGMT_HDR_LEN],
			mgmt_frame_len - DOT11_MGMT_HDR_LEN)) {
			p2p_act_frm = (wifi_p2p_action_frame_t *)
					(&mgmt_frame[DOT11_MGMT_HDR_LEN]);
			(void) p2p_act_frm;
		} else if (wl_cfgp2p_is_gas_action(&mgmt_frame[DOT11_MGMT_HDR_LEN],
			mgmt_frame_len - DOT11_MGMT_HDR_LEN)) {

			sd_act_frm = (wifi_p2psd_gas_pub_act_frame_t *)
					(&mgmt_frame[DOT11_MGMT_HDR_LEN]);
			if (sd_act_frm && wl_get_drv_status_all(cfg, WAITING_NEXT_ACT_FRM)) {
				if (cfg->next_af_subtype == sd_act_frm->action) {
					WL_DBG(("We got a right next frame of SD!(%d)\n",
						sd_act_frm->action));
					wl_clr_drv_status(cfg, WAITING_NEXT_ACT_FRM, ndev);

					/* Stop waiting for next AF. */
					wl_stop_wait_next_action_frame(cfg, ndev, bsscfgidx);
				}
			}
			(void) sd_act_frm;
#ifdef WLTDLS
		} else if ((mgmt_frame[DOT11_MGMT_HDR_LEN] == TDLS_AF_CATEGORY) ||
				(wl_cfg80211_is_tdls_tunneled_frame(
				    &mgmt_frame[DOT11_MGMT_HDR_LEN],
				    mgmt_frame_len - DOT11_MGMT_HDR_LEN))) {
			if (mgmt_frame[DOT11_MGMT_HDR_LEN] == TDLS_AF_CATEGORY) {
				WL_ERR((" TDLS Action Frame Received type = %d \n",
					mgmt_frame[DOT11_MGMT_HDR_LEN + 1]));
			}
#ifdef TDLS_MSG_ONLY_WFD
			if (!dhdp->tdls_mode) {
				WL_DBG((" TDLS Frame filtered \n"));
				return 0;
			}
#else
			if (mgmt_frame[DOT11_MGMT_HDR_LEN + 1] == TDLS_ACTION_SETUP_RESP) {
				cfg->tdls_mgmt_frame = mgmt_frame;
				cfg->tdls_mgmt_frame_len = mgmt_frame_len;
				cfg->tdls_mgmt_freq = freq;
				return 0;
			}
#endif /* TDLS_MSG_ONLY_WFD */
#endif /* WLTDLS */
#ifdef QOS_MAP_SET
		} else if (mgmt_frame[DOT11_MGMT_HDR_LEN] == DOT11_ACTION_CAT_QOS) {
			/* update QoS map set table */
			bcm_tlv_t * qos_map_ie = NULL;
			if ((qos_map_ie = bcm_parse_tlvs(&mgmt_frame[DOT11_MGMT_HDR_LEN],
					mgmt_frame_len - DOT11_MGMT_HDR_LEN,
					DOT11_MNG_QOS_MAP_ID)) != NULL) {
				WL_DBG((" QoS map set IE found in QoS action frame\n"));
				if (!cfg->up_table) {
					cfg->up_table = kmalloc(UP_TABLE_MAX, GFP_KERNEL);
				}
				wl_set_up_table(cfg->up_table, qos_map_ie);
			} else {
				kfree(cfg->up_table);
				cfg->up_table = NULL;
			}
#endif /* QOS_MAP_SET */
#ifdef WBTEXT
		} else if (mgmt_frame[DOT11_MGMT_HDR_LEN] == DOT11_ACTION_CAT_RRM) {
			/* radio measurement category */
			switch (mgmt_frame[DOT11_MGMT_HDR_LEN+1]) {
				case DOT11_RM_ACTION_NR_REP:
					if (wl_cfg80211_recv_nbr_resp(ndev,
							&mgmt_frame[DOT11_MGMT_HDR_LEN],
							mgmt_frame_len - DOT11_MGMT_HDR_LEN)
							== BCME_OK) {
						WL_DBG(("RCC updated by nbr response\n"));
					}
					break;
				default:
					break;
			}
#endif /* WBTEXT */
		} else {
			/*
			 *  if we got normal action frame and ndev is p2p0,
			 *  we have to change ndev from p2p0 to wlan0
			 */


			if (cfg->next_af_subtype != P2P_PAF_SUBTYPE_INVALID) {
				u8 action = 0;
				if (wl_get_public_action(&mgmt_frame[DOT11_MGMT_HDR_LEN],
					mgmt_frame_len - DOT11_MGMT_HDR_LEN, &action) != BCME_OK) {
					WL_DBG(("Recived action is not public action frame\n"));
				} else if (cfg->next_af_subtype == action) {
					WL_DBG(("Recived action is the waiting action(%d)\n",
						action));
					wl_clr_drv_status(cfg, WAITING_NEXT_ACT_FRM, ndev);

					/* Stop waiting for next AF. */
					wl_stop_wait_next_action_frame(cfg, ndev, bsscfgidx);
				}
			}
		}

		if (act_frm) {

			if (wl_get_drv_status_all(cfg, WAITING_NEXT_ACT_FRM)) {
				if (cfg->next_af_subtype == act_frm->subtype) {
					WL_DBG(("We got a right next frame!(%d)\n",
						act_frm->subtype));
					wl_clr_drv_status(cfg, WAITING_NEXT_ACT_FRM, ndev);

					if (cfg->next_af_subtype == P2P_PAF_GON_CONF) {
						OSL_SLEEP(20);
					}

					/* Stop waiting for next AF. */
					wl_stop_wait_next_action_frame(cfg, ndev, bsscfgidx);
				}
			}
		}

		wl_cfgp2p_print_actframe(false, &mgmt_frame[DOT11_MGMT_HDR_LEN],
			mgmt_frame_len - DOT11_MGMT_HDR_LEN, channel);
		if (act_frm && (act_frm->subtype == P2P_PAF_GON_CONF)) {
			WL_DBG(("P2P: GO_NEG_PHASE status cleared \n"));
			wl_clr_p2p_status(cfg, GO_NEG_PHASE);
		}
	} else if (event == WLC_E_PROBREQ_MSG) {

		/* Handle probe reqs frame
		 * WPS-AP certification 4.2.13
		 */
		struct parsed_ies prbreq_ies;
		u32 prbreq_ie_len = 0;
		bool pbc = 0;

		WL_DBG((" Event WLC_E_PROBREQ_MSG received\n"));
		mgmt_frame = (u8 *)(data);
		mgmt_frame_len = ntoh32(e->datalen);
		if (mgmt_frame_len < DOT11_MGMT_HDR_LEN) {
			WL_ERR(("wrong datalen:%d\n", mgmt_frame_len));
			return -EINVAL;
		}
		prbreq_ie_len = mgmt_frame_len - DOT11_MGMT_HDR_LEN;

		/* Parse prob_req IEs */
		if (wl_cfg80211_parse_ies(&mgmt_frame[DOT11_MGMT_HDR_LEN],
			prbreq_ie_len, &prbreq_ies) < 0) {
			WL_ERR(("Prob req get IEs failed\n"));
			return 0;
		}
		if (prbreq_ies.wps_ie != NULL) {
			wl_validate_wps_ie((char *)prbreq_ies.wps_ie, prbreq_ies.wps_ie_len, &pbc);
			WL_DBG((" wps_ie exist pbc = %d\n", pbc));
			/* if pbc method, send prob_req mgmt frame to upper layer */
			if (!pbc)
				return 0;
		} else
			return 0;
	} else {
		mgmt_frame = (u8 *)((wl_event_rx_frame_data_t *)rxframe + 1);

		/* wpa supplicant use probe request event for restarting another GON Req.
		 * but it makes GON Req repetition.
		 * so if src addr of prb req is same as my target device,
		 * do not send probe request event during sending action frame.
		 */
		if (event == WLC_E_P2P_PROBREQ_MSG) {
			WL_DBG((" Event %s\n", (event == WLC_E_P2P_PROBREQ_MSG) ?
				"WLC_E_P2P_PROBREQ_MSG":"WLC_E_PROBREQ_MSG"));


			/* Filter any P2P probe reqs arriving during the
			 * GO-NEG Phase
			 */
			if (cfg->p2p &&
#if defined(P2P_IE_MISSING_FIX)
				cfg->p2p_prb_noti &&
#endif
				wl_get_p2p_status(cfg, GO_NEG_PHASE)) {
				WL_DBG(("Filtering P2P probe_req while "
					"being in GO-Neg state\n"));
				return 0;
			}
		}
	}

	if (discover_cfgdev(cfgdev, cfg))
		WL_DBG(("Rx Managment frame For P2P Discovery Interface \n"));
	else
		WL_DBG(("Rx Managment frame For Iface (%s) \n", ndev->name));
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0))
	 cfg80211_rx_mgmt(cfgdev, freq, 0,  mgmt_frame, mgmt_frame_len, 0);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0))
	cfg80211_rx_mgmt(cfgdev, freq, 0,  mgmt_frame, mgmt_frame_len, 0, GFP_ATOMIC);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)) || \
	defined(WL_COMPAT_WIRELESS)
	cfg80211_rx_mgmt(cfgdev, freq, 0, mgmt_frame, mgmt_frame_len, GFP_ATOMIC);
#else
	cfg80211_rx_mgmt(cfgdev, freq, mgmt_frame, mgmt_frame_len, GFP_ATOMIC);
#endif /* LINUX_VERSION >= VERSION(3, 18, 0) */

	WL_DBG(("mgmt_frame_len (%d) , e->datalen (%d), channel (%d), freq (%d)\n",
		mgmt_frame_len, ntoh32(e->datalen), channel, freq));
exit:
	if (isfree)
		kfree(mgmt_frame);
	return err;
}

#ifdef WL_SCHED_SCAN
/* If target scan is not reliable, set the below define to "1" to do a
 * full escan
 */
#define FULL_ESCAN_ON_PFN_NET_FOUND		0
static s32
wl_notify_sched_scan_results(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *e, void *data)
{
	wl_pfn_net_info_v2_t *netinfo, *pnetinfo;
	struct wiphy *wiphy = bcmcfg_to_wiphy(cfg);
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
	int err = 0;
	struct cfg80211_scan_request *request = NULL;
	struct cfg80211_ssid ssid[MAX_PFN_LIST_COUNT];
	struct ieee80211_channel *channel = NULL;
	int channel_req = 0;
	int band = 0;
	wl_pfn_scanresults_t *pfn_result = (wl_pfn_scanresults_t *)data;
	int n_pfn_results = pfn_result->count;
	log_conn_event_t *event_data = NULL;
	tlv_log *tlv_data = NULL;
	u32 alloc_len, tlv_len;
	u32 payload_len;

	WL_DBG(("Enter\n"));

	if (pfn_result->version != PFN_SCANRESULT_VERSION) {
		WL_ERR(("Incorrect version %d, expected %d\n", pfn_result->version,
		       PFN_SCANRESULT_VERSION));
		return BCME_VERSION;
	}

	if (e->event_type == WLC_E_PFN_NET_LOST) {
		WL_PNO(("PFN NET LOST event. Do Nothing\n"));
		return 0;
	}

	WL_PNO((">>> PFN NET FOUND event. count:%d \n", n_pfn_results));
	if (n_pfn_results > 0) {
		int i;

		if (n_pfn_results > MAX_PFN_LIST_COUNT)
			n_pfn_results = MAX_PFN_LIST_COUNT;
		pnetinfo = (wl_pfn_net_info_v2_t *)((char *)data + sizeof(wl_pfn_scanresults_v2_t)
				- sizeof(wl_pfn_net_info_v2_t));

		memset(&ssid, 0x00, sizeof(ssid));

		request = kzalloc(sizeof(*request)
			+ sizeof(*request->channels) * n_pfn_results,
			GFP_KERNEL);
		channel = (struct ieee80211_channel *)kzalloc(
			(sizeof(struct ieee80211_channel) * n_pfn_results),
			GFP_KERNEL);
		if (!request || !channel) {
			WL_ERR(("No memory"));
			err = -ENOMEM;
			goto out_err;
		}

		request->wiphy = wiphy;

		if (DBG_RING_ACTIVE(dhdp, DHD_EVENT_RING_ID)) {
			alloc_len = sizeof(log_conn_event_t) + DOT11_MAX_SSID_LEN + sizeof(uint16) +
				sizeof(int16);
			event_data = MALLOC(dhdp->osh, alloc_len);
			if (!event_data) {
				WL_ERR(("%s: failed to allocate the log_conn_event_t with "
					"length(%d)\n", __func__, alloc_len));
				goto out_err;
			}
			tlv_len = 3 * sizeof(tlv_log);
			event_data->tlvs = MALLOC(dhdp->osh, tlv_len);
			if (!event_data->tlvs) {
				WL_ERR(("%s: failed to allocate the tlv_log with "
					"length(%d)\n", __func__, tlv_len));
				goto out_err;
			}
		}

		for (i = 0; i < n_pfn_results; i++) {
			netinfo = &pnetinfo[i];
			if (!netinfo) {
				WL_ERR(("Invalid netinfo ptr. index:%d", i));
				err = -EINVAL;
				goto out_err;
			}
			WL_PNO((">>> SSID:%s Channel:%d \n",
				netinfo->pfnsubnet.u.SSID, netinfo->pfnsubnet.channel));
			/* PFN result doesn't have all the info which are required by the supplicant
			 * (For e.g IEs) Do a target Escan so that sched scan results are reported
			 * via wl_inform_single_bss in the required format. Escan does require the
			 * scan request in the form of cfg80211_scan_request. For timebeing, create
			 * cfg80211_scan_request one out of the received PNO event.
			 */
			ssid[i].ssid_len = MIN(DOT11_MAX_SSID_LEN, netinfo->pfnsubnet.SSID_len);
			memcpy(ssid[i].ssid, netinfo->pfnsubnet.u.SSID,
				ssid[i].ssid_len);
			request->n_ssids++;

			channel_req = netinfo->pfnsubnet.channel;
			band = (channel_req <= CH_MAX_2G_CHANNEL) ? NL80211_BAND_2GHZ
				: NL80211_BAND_5GHZ;
			channel[i].center_freq = ieee80211_channel_to_frequency(channel_req, band);
			channel[i].band = band;
			channel[i].flags |= IEEE80211_CHAN_NO_HT40;
			request->channels[i] = &channel[i];
			request->n_channels++;

			if (DBG_RING_ACTIVE(dhdp, DHD_EVENT_RING_ID)) {
				payload_len = sizeof(log_conn_event_t);
				event_data->event = WIFI_EVENT_DRIVER_PNO_NETWORK_FOUND;
				tlv_data = event_data->tlvs;

				/* ssid */
				tlv_data->tag = WIFI_TAG_SSID;
				tlv_data->len = netinfo->pfnsubnet.SSID_len;
				memcpy(tlv_data->value, ssid[i].ssid, ssid[i].ssid_len);
				payload_len += TLV_LOG_SIZE(tlv_data);
				tlv_data = TLV_LOG_NEXT(tlv_data);

				/* channel */
				tlv_data->tag = WIFI_TAG_CHANNEL;
				tlv_data->len = sizeof(uint16);
				memcpy(tlv_data->value, &channel_req, sizeof(uint16));
				payload_len += TLV_LOG_SIZE(tlv_data);
				tlv_data = TLV_LOG_NEXT(tlv_data);

				/* rssi */
				tlv_data->tag = WIFI_TAG_RSSI;
				tlv_data->len = sizeof(int16);
				memcpy(tlv_data->value, &netinfo->RSSI, sizeof(int16));
				payload_len += TLV_LOG_SIZE(tlv_data);
				tlv_data = TLV_LOG_NEXT(tlv_data);

				dhd_os_push_push_ring_data(dhdp, DHD_EVENT_RING_ID,
					&event_data->event, payload_len);
			}
		}

		/* assign parsed ssid array */
		if (request->n_ssids)
			request->ssids = &ssid[0];

		if (wl_get_drv_status_all(cfg, SCANNING)) {
			/* Abort any on-going scan */
			wl_notify_escan_complete(cfg, ndev, true, true);
		}

		if (wl_get_p2p_status(cfg, DISCOVERY_ON)) {
			WL_PNO((">>> P2P discovery was ON. Disabling it\n"));
			err = wl_cfgp2p_discover_enable_search(cfg, false);
			if (unlikely(err)) {
				wl_clr_drv_status(cfg, SCANNING, ndev);
				goto out_err;
			}
			p2p_scan(cfg) = false;
		}

		wl_set_drv_status(cfg, SCANNING, ndev);
#if FULL_ESCAN_ON_PFN_NET_FOUND
		WL_PNO((">>> Doing Full ESCAN on PNO event\n"));
		err = wl_do_escan(cfg, wiphy, ndev, NULL);
#else
		WL_PNO((">>> Doing targeted ESCAN on PNO event\n"));
		err = wl_do_escan(cfg, wiphy, ndev, request);
#endif
		if (err) {
			wl_clr_drv_status(cfg, SCANNING, ndev);
			goto out_err;
		}
		DBG_EVENT_LOG(dhdp, WIFI_EVENT_DRIVER_PNO_SCAN_REQUESTED);
		cfg->sched_scan_running = TRUE;
	}
	else {
		WL_ERR(("FALSE PNO Event. (pfn_count == 0) \n"));
	}
out_err:
	if (request)
		kfree(request);
	if (channel)
		kfree(channel);

	if (event_data) {
		MFREE(dhdp->osh, event_data->tlvs, tlv_len);
		MFREE(dhdp->osh, event_data, alloc_len);
	}
	return err;
}
#endif /* WL_SCHED_SCAN */

static void wl_init_conf(struct wl_conf *conf)
{
	WL_DBG(("Enter \n"));
	conf->frag_threshold = (u32)-1;
	conf->rts_threshold = (u32)-1;
	conf->retry_short = (u32)-1;
	conf->retry_long = (u32)-1;
	conf->tx_power = -1;
}

static void wl_init_prof(struct bcm_cfg80211 *cfg, struct net_device *ndev)
{
	unsigned long flags;
	struct wl_profile *profile = wl_get_profile_by_netdev(cfg, ndev);

	if (profile) {
		spin_lock_irqsave(&cfg->cfgdrv_lock, flags);
		memset(profile, 0, sizeof(struct wl_profile));
		spin_unlock_irqrestore(&cfg->cfgdrv_lock, flags);
	} else {
		WL_ERR(("%s: No profile\n", __FUNCTION__));
	}
	return;
}

static void wl_init_event_handler(struct bcm_cfg80211 *cfg)
{
	memset(cfg->evt_handler, 0, sizeof(cfg->evt_handler));

	cfg->evt_handler[WLC_E_SCAN_COMPLETE] = wl_notify_scan_status;
	cfg->evt_handler[WLC_E_AUTH] = wl_notify_connect_status;
	cfg->evt_handler[WLC_E_ASSOC] = wl_notify_connect_status;
	cfg->evt_handler[WLC_E_LINK] = wl_notify_connect_status;
	cfg->evt_handler[WLC_E_DEAUTH_IND] = wl_notify_connect_status;
	cfg->evt_handler[WLC_E_DEAUTH] = wl_notify_connect_status;
	cfg->evt_handler[WLC_E_DISASSOC_IND] = wl_notify_connect_status;
	cfg->evt_handler[WLC_E_ASSOC_IND] = wl_notify_connect_status;
	cfg->evt_handler[WLC_E_REASSOC_IND] = wl_notify_connect_status;
	cfg->evt_handler[WLC_E_ROAM] = wl_notify_roaming_status;
	cfg->evt_handler[WLC_E_MIC_ERROR] = wl_notify_mic_status;
	cfg->evt_handler[WLC_E_SET_SSID] = wl_notify_connect_status;
	cfg->evt_handler[WLC_E_ACTION_FRAME_RX] = wl_notify_rx_mgmt_frame;
	cfg->evt_handler[WLC_E_PROBREQ_MSG] = wl_notify_rx_mgmt_frame;
	cfg->evt_handler[WLC_E_P2P_PROBREQ_MSG] = wl_notify_rx_mgmt_frame;
	cfg->evt_handler[WLC_E_P2P_DISC_LISTEN_COMPLETE] = wl_cfgp2p_listen_complete;
	cfg->evt_handler[WLC_E_ACTION_FRAME_COMPLETE] = wl_cfgp2p_action_tx_complete;
	cfg->evt_handler[WLC_E_ACTION_FRAME_OFF_CHAN_COMPLETE] = wl_cfgp2p_action_tx_complete;
	cfg->evt_handler[WLC_E_JOIN] = wl_notify_connect_status;
	cfg->evt_handler[WLC_E_START] = wl_notify_connect_status;
#ifdef PNO_SUPPORT
	cfg->evt_handler[WLC_E_PFN_NET_FOUND] = wl_notify_pfn_status;
#endif /* PNO_SUPPORT */
#ifdef GSCAN_SUPPORT
	cfg->evt_handler[WLC_E_PFN_BEST_BATCHING] = wl_notify_gscan_event;
	cfg->evt_handler[WLC_E_PFN_SCAN_COMPLETE] = wl_notify_gscan_event;
	cfg->evt_handler[WLC_E_PFN_GSCAN_FULL_RESULT] = wl_notify_gscan_event;
	cfg->evt_handler[WLC_E_PFN_BSSID_NET_FOUND] = wl_notify_gscan_event;
	cfg->evt_handler[WLC_E_PFN_BSSID_NET_LOST] = wl_notify_gscan_event;
	cfg->evt_handler[WLC_E_PFN_SSID_EXT] = wl_notify_gscan_event;
	cfg->evt_handler[WLC_E_GAS_FRAGMENT_RX] = wl_notify_gscan_event;
	cfg->evt_handler[WLC_E_ROAM_EXP_EVENT] = wl_handle_roam_exp_event;
#endif /* GSCAN_SUPPORT */
#ifdef RSSI_MONITOR_SUPPORT
	cfg->evt_handler[WLC_E_RSSI_LQM] = wl_handle_rssi_monitor_event;
#endif /* RSSI_MONITOR_SUPPORT */
#ifdef WLTDLS
	cfg->evt_handler[WLC_E_TDLS_PEER_EVENT] = wl_tdls_event_handler;
#endif /* WLTDLS */
	cfg->evt_handler[WLC_E_BSSID] = wl_notify_roaming_status;
#ifdef	WL_RELMCAST
	cfg->evt_handler[WLC_E_RMC_EVENT] = wl_notify_rmc_status;
#endif /* WL_RELMCAST */
#ifdef BT_WIFI_HANDOVER
	cfg->evt_handler[WLC_E_BT_WIFI_HANDOVER_REQ] = wl_notify_bt_wifi_handover_req;
#endif
	cfg->evt_handler[WLC_E_CSA_COMPLETE_IND] = wl_csa_complete_ind;
	cfg->evt_handler[WLC_E_AP_STARTED] = wl_ap_start_ind;
#ifdef CUSTOM_EVENT_PM_WAKE
	cfg->evt_handler[WLC_E_EXCESS_PM_WAKE_EVENT] = wl_check_pmstatus;
#endif	/* CUSTOM_EVENT_PM_WAKE */
#if defined(DHD_LOSSLESS_ROAMING) || defined(DBG_PKT_MON)
	cfg->evt_handler[WLC_E_ROAM_PREP] = wl_notify_roam_prep_status;
#endif /* DHD_LOSSLESS_ROAMING || DBG_PKT_MON  */
#ifdef ENABLE_TEMP_THROTTLING
	cfg->evt_handler[WLC_E_TEMP_THROTTLE] = wl_check_rx_throttle_status;
#endif /* ENABLE_TEMP_THROTTLING */
#ifdef WL_SAE
	cfg->evt_handler[WLC_E_AUTH_IND] = wl_notify_connect_status;
#endif /* WL_SAE */
}

#if defined(STATIC_WL_PRIV_STRUCT)
static int
wl_init_escan_result_buf(struct bcm_cfg80211 *cfg)
{
	cfg->escan_info.escan_buf = DHD_OS_PREALLOC(cfg->pub,
		DHD_PREALLOC_WIPHY_ESCAN0, ESCAN_BUF_SIZE);
	if (cfg->escan_info.escan_buf == NULL) {
		WL_ERR(("Failed to alloc ESCAN_BUF\n"));
		return -ENOMEM;
	}
	bzero(cfg->escan_info.escan_buf, ESCAN_BUF_SIZE);

	return 0;
}

static void
wl_deinit_escan_result_buf(struct bcm_cfg80211 *cfg)
{
	if (cfg->escan_info.escan_buf != NULL) {
		cfg->escan_info.escan_buf = NULL;
	}
}
#endif /* STATIC_WL_PRIV_STRUCT */

static s32 wl_init_priv_mem(struct bcm_cfg80211 *cfg)
{
	WL_DBG(("Enter \n"));

	cfg->scan_results = (void *)kzalloc(WL_SCAN_BUF_MAX, GFP_KERNEL);
	if (unlikely(!cfg->scan_results)) {
		WL_ERR(("Scan results alloc failed\n"));
		goto init_priv_mem_out;
	}
	cfg->conf = (void *)kzalloc(sizeof(*cfg->conf), GFP_KERNEL);
	if (unlikely(!cfg->conf)) {
		WL_ERR(("wl_conf alloc failed\n"));
		goto init_priv_mem_out;
	}
	cfg->scan_req_int =
	    (void *)kzalloc(sizeof(*cfg->scan_req_int), GFP_KERNEL);
	if (unlikely(!cfg->scan_req_int)) {
		WL_ERR(("Scan req alloc failed\n"));
		goto init_priv_mem_out;
	}
	cfg->ioctl_buf = (void *)kzalloc(WLC_IOCTL_MAXLEN, GFP_KERNEL);
	if (unlikely(!cfg->ioctl_buf)) {
		WL_ERR(("Ioctl buf alloc failed\n"));
		goto init_priv_mem_out;
	}
	cfg->escan_ioctl_buf = (void *)kzalloc(WLC_IOCTL_MAXLEN, GFP_KERNEL);
	if (unlikely(!cfg->escan_ioctl_buf)) {
		WL_ERR(("Ioctl buf alloc failed\n"));
		goto init_priv_mem_out;
	}
	cfg->extra_buf = (void *)kzalloc(WL_EXTRA_BUF_MAX, GFP_KERNEL);
	if (unlikely(!cfg->extra_buf)) {
		WL_ERR(("Extra buf alloc failed\n"));
		goto init_priv_mem_out;
	}
	cfg->pmk_list = (void *)kzalloc(sizeof(*cfg->pmk_list), GFP_KERNEL);
	if (unlikely(!cfg->pmk_list)) {
		WL_ERR(("pmk list alloc failed\n"));
		goto init_priv_mem_out;
	}
#if defined(STATIC_WL_PRIV_STRUCT)
	cfg->conn_info = (void *)kzalloc(sizeof(*cfg->conn_info), GFP_KERNEL);
	if (unlikely(!cfg->conn_info)) {
		WL_ERR(("cfg->conn_info  alloc failed\n"));
		goto init_priv_mem_out;
	}
	cfg->ie = (void *)kzalloc(sizeof(*cfg->ie), GFP_KERNEL);
	if (unlikely(!cfg->ie)) {
		WL_ERR(("cfg->ie  alloc failed\n"));
		goto init_priv_mem_out;
	}
	if (unlikely(wl_init_escan_result_buf(cfg))) {
		WL_ERR(("Failed to init escan resul buf\n"));
		goto init_priv_mem_out;
	}
#endif /* STATIC_WL_PRIV_STRUCT */
	cfg->afx_hdl = (void *)kzalloc(sizeof(*cfg->afx_hdl), GFP_KERNEL);
	if (unlikely(!cfg->afx_hdl)) {
		WL_ERR(("afx hdl  alloc failed\n"));
		goto init_priv_mem_out;
	} else {
		init_completion(&cfg->act_frm_scan);
		init_completion(&cfg->wait_next_af);

		INIT_WORK(&cfg->afx_hdl->work, wl_cfg80211_afx_handler);
	}
#ifdef WLTDLS
	if (cfg->tdls_mgmt_frame) {
		kfree(cfg->tdls_mgmt_frame);
		cfg->tdls_mgmt_frame = NULL;
	}
#endif /* WLTDLS */
	return 0;

init_priv_mem_out:
	wl_deinit_priv_mem(cfg);

	return -ENOMEM;
}

static void wl_deinit_priv_mem(struct bcm_cfg80211 *cfg)
{
	kfree(cfg->scan_results);
	cfg->scan_results = NULL;
	kfree(cfg->conf);
	cfg->conf = NULL;
	kfree(cfg->scan_req_int);
	cfg->scan_req_int = NULL;
	kfree(cfg->ioctl_buf);
	cfg->ioctl_buf = NULL;
	kfree(cfg->escan_ioctl_buf);
	cfg->escan_ioctl_buf = NULL;
	kfree(cfg->extra_buf);
	cfg->extra_buf = NULL;
	kfree(cfg->pmk_list);
	cfg->pmk_list = NULL;
#if defined(STATIC_WL_PRIV_STRUCT)
	kfree(cfg->conn_info);
	cfg->conn_info = NULL;
	kfree(cfg->ie);
	cfg->ie = NULL;
	wl_deinit_escan_result_buf(cfg);
#endif /* STATIC_WL_PRIV_STRUCT */
	if (cfg->afx_hdl) {
		cancel_work_sync(&cfg->afx_hdl->work);
		kfree(cfg->afx_hdl);
		cfg->afx_hdl = NULL;
	}

}

static s32 wl_create_event_handler(struct bcm_cfg80211 *cfg)
{
	int ret = 0;
	WL_DBG(("Enter \n"));

	/* Allocate workqueue for event */
	if (!cfg->event_workq) {
		cfg->event_workq = alloc_workqueue("dhd_eventd", WQ_MEM_RECLAIM | WQ_HIGHPRI, 0);
	}

	if (!cfg->event_workq) {
		WL_ERR(("event_workq alloc_workqueue failed\n"));
		ret = -ENOMEM;
	} else {
		INIT_WORK(&cfg->event_work, wl_event_handler);
	}
	return ret;
}

static void wl_destroy_event_handler(struct bcm_cfg80211 *cfg)
{
	if (cfg && cfg->event_workq) {
		cancel_work_sync(&cfg->event_work);
		destroy_workqueue(cfg->event_workq);
		cfg->event_workq = NULL;
	}
}

void wl_terminate_event_handler(struct net_device *dev)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	if (cfg) {
		wl_destroy_event_handler(cfg);
		wl_flush_eq(cfg);
	}
}

static void wl_scan_timeout(unsigned long data)
{
	wl_event_msg_t msg;
	struct bcm_cfg80211 *cfg = (struct bcm_cfg80211 *)data;
	struct wireless_dev *wdev = NULL;
	struct net_device *ndev = NULL;
	struct wl_scan_results *bss_list;
	struct wl_bss_info *bi = NULL;
	s32 i;
	u32 channel;
#if 0
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
	uint32 prev_memdump_mode = dhdp->memdump_enabled;
#endif /* DHD_DEBUG && DHD_FW_COREDUMP */

	if (!(cfg->scan_request)) {
		WL_ERR(("timer expired but no scan request\n"));
		return;
	}

	bss_list = wl_escan_get_buf(cfg, FALSE);
	if (!bss_list) {
		WL_ERR(("bss_list is null. Didn't receive any partial scan results\n"));
	} else {
		WL_ERR(("scanned AP count (%d)\n", bss_list->count));

		bi = next_bss(bss_list, bi);
		for_each_bss(bss_list, bi, i) {
			channel = wf_chspec_ctlchan(wl_chspec_driver_to_host(bi->chanspec));
			WL_ERR(("SSID :%s  Channel :%d\n", bi->SSID, channel));
		}
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0))
	if (cfg->scan_request->dev)
		wdev = cfg->scan_request->dev->ieee80211_ptr;
#else
	wdev = cfg->scan_request->wdev;
#endif /* LINUX_VERSION < KERNEL_VERSION(3, 6, 0) */
	if (!wdev) {
		WL_ERR(("No wireless_dev present\n"));
		return;
	}
	ndev = wdev_to_wlc_ndev(wdev, cfg);

	bzero(&msg, sizeof(wl_event_msg_t));
	WL_ERR(("timer expired\n"));
#if 0
	if (dhdp->memdump_enabled) {
		dhdp->memdump_enabled = DUMP_MEMFILE;
		dhdp->memdump_type = DUMP_TYPE_SCAN_TIMEOUT;
		dhd_bus_mem_dump(dhdp);
		dhdp->memdump_enabled = prev_memdump_mode;
	}
#endif /* DHD_DEBUG && DHD_FW_COREDUMP */
	msg.event_type = hton32(WLC_E_ESCAN_RESULT);
	msg.status = hton32(WLC_E_STATUS_TIMEOUT);
	msg.reason = 0xFFFFFFFF;
	wl_cfg80211_event(ndev, &msg, NULL);
#ifdef CUSTOMER_HW4_DEBUG
	if (!wl_scan_timeout_dbg_enabled)
		wl_scan_timeout_dbg_set();
#endif /* CUSTOMER_HW4_DEBUG */

	// terence 20130729: workaround to fix out of memory in firmware
//	if (dhd_conf_get_chip(dhd_get_pub(ndev)) == BCM43362_CHIP_ID) {
//		WL_ERR(("Send hang event\n"));
//		net_os_send_hang_message(ndev);
//	}
}

#ifdef DHD_LOSSLESS_ROAMING
static void wl_del_roam_timeout(struct bcm_cfg80211 *cfg)
{
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);

	/* restore prec_map to ALLPRIO */
	dhdp->dequeue_prec_map = ALLPRIO;
	if (timer_pending(&cfg->roam_timeout)) {
		del_timer_sync(&cfg->roam_timeout);
	}

}

static void wl_roam_timeout(unsigned long data)
{
	struct bcm_cfg80211 *cfg = (struct bcm_cfg80211 *)data;
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);

	WL_ERR(("roam timer expired\n"));

	/* restore prec_map to ALLPRIO */
	dhdp->dequeue_prec_map = ALLPRIO;
}

#endif /* DHD_LOSSLESS_ROAMING */

static s32
wl_cfg80211_netdev_notifier_call(struct notifier_block * nb,
	unsigned long state, void *ptr)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 11, 0))
	struct net_device *dev = ptr;
#else
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
#endif /* LINUX_VERSION < VERSION(3, 11, 0) */
	struct wireless_dev *wdev = ndev_to_wdev(dev);
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	WL_DBG(("Enter \n"));

	if (!wdev || !cfg || dev == bcmcfg_to_prmry_ndev(cfg))
		return NOTIFY_DONE;

	switch (state) {
		case NETDEV_DOWN:
		{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 11, 0))
			int max_wait_timeout = 2;
			int max_wait_count = 100;
			int refcnt = 0;
			unsigned long limit = jiffies + max_wait_timeout * HZ;
			while (work_pending(&wdev->cleanup_work)) {
				if (refcnt%5 == 0) {
					WL_ERR(("[NETDEV_DOWN] wait for "
						"complete of cleanup_work"
						" (%d th)\n", refcnt));
				}
				if (!time_before(jiffies, limit)) {
					WL_ERR(("[NETDEV_DOWN] cleanup_work"
						" of CFG80211 is not"
						" completed in %d sec\n",
						max_wait_timeout));
					break;
				}
				if (refcnt >= max_wait_count) {
					WL_ERR(("[NETDEV_DOWN] cleanup_work"
						" of CFG80211 is not"
						" completed in %d loop\n",
						max_wait_count));
					break;
				}
				set_current_state(TASK_INTERRUPTIBLE);
				(void)schedule_timeout(100);
				set_current_state(TASK_RUNNING);
				refcnt++;
			}
#endif /* LINUX_VERSION < VERSION(3, 14, 0) */
			break;
		}
		case NETDEV_UNREGISTER:
			/* after calling list_del_rcu(&wdev->list) */
			wl_cfg80211_clear_per_bss_ies(cfg,
				wl_get_bssidx_by_wdev(cfg, wdev));
			wl_dealloc_netinfo_by_wdev(cfg, wdev);
			break;
		case NETDEV_GOING_DOWN:
			/*
			 * At NETDEV_DOWN state, wdev_cleanup_work work will be called.
			 * In front of door, the function checks whether current scan
			 * is working or not. If the scanning is still working,
			 * wdev_cleanup_work call WARN_ON and make the scan done forcibly.
			 */
			if (wl_get_drv_status(cfg, SCANNING, dev))
				wl_notify_escan_complete(cfg, dev, true, true);
			break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block wl_cfg80211_netdev_notifier = {
	.notifier_call = wl_cfg80211_netdev_notifier_call,
};

/*
 * to make sure we won't register the same notifier twice, otherwise a loop is likely to be
 * created in kernel notifier link list (with 'next' pointing to itself)
 */
static bool wl_cfg80211_netdev_notifier_registered = FALSE;

static void wl_cfg80211_cancel_scan(struct bcm_cfg80211 *cfg)
{
	struct wireless_dev *wdev = NULL;
	struct net_device *ndev = NULL;

	if (!cfg->scan_request)
		return;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0))
	if (cfg->scan_request->dev)
		wdev = cfg->scan_request->dev->ieee80211_ptr;
#else
	wdev = cfg->scan_request->wdev;
#endif /* LINUX_VERSION < KERNEL_VERSION(3, 6, 0) */

	if (!wdev) {
		WL_ERR(("No wireless_dev present\n"));
		return;
	}

	ndev = wdev_to_wlc_ndev(wdev, cfg);
	wl_notify_escan_complete(cfg, ndev, true, true);
	WL_ERR(("Scan aborted! \n"));
}

void wl_cfg80211_scan_abort(struct bcm_cfg80211 *cfg)
{
	wl_scan_params_t *params = NULL;
	s32 params_size = 0;
	s32 err = BCME_OK;
	struct net_device *dev = bcmcfg_to_prmry_ndev(cfg);
	if (!in_atomic()) {
		/* Our scan params only need space for 1 channel and 0 ssids */
		params = wl_cfg80211_scan_alloc_params(cfg, -1, 0, &params_size);
		if (params == NULL) {
			WL_ERR(("scan params allocation failed \n"));
			err = -ENOMEM;
		} else {
			/* Do a scan abort to stop the driver's scan engine */
			err = wldev_ioctl_set(dev, WLC_SCAN, params, params_size);
			if (err < 0) {
				WL_ERR(("scan abort  failed \n"));
			}
			kfree(params);
		}
	}
#ifdef WLTDLS
	if (cfg->tdls_mgmt_frame) {
		kfree(cfg->tdls_mgmt_frame);
		cfg->tdls_mgmt_frame = NULL;
	}
#endif /* WLTDLS */
}

static s32 wl_notify_escan_complete(struct bcm_cfg80211 *cfg,
	struct net_device *ndev,
	bool aborted, bool fw_abort)
{
	s32 err = BCME_OK;
	unsigned long flags;
	struct net_device *dev;
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0))
	struct cfg80211_scan_info info;
	info.aborted = aborted;
#endif

	WL_DBG(("Enter \n"));
	BCM_REFERENCE(dhdp);

	mutex_lock(&cfg->scan_complete);

	if (!ndev) {
		WL_ERR(("ndev is null\n"));
		err = BCME_ERROR;
		goto out;
	}

	if (cfg->escan_info.ndev != ndev) {
		WL_ERR(("ndev is different %p %p\n", cfg->escan_info.ndev, ndev));
		err = BCME_ERROR;
		goto out;
	}

	if (cfg->scan_request) {
		dev = bcmcfg_to_prmry_ndev(cfg);
#if defined(WL_ENABLE_P2P_IF)
		if (cfg->scan_request->dev != cfg->p2p_net)
			dev = cfg->scan_request->dev;
#elif defined(WL_CFG80211_P2P_DEV_IF)
		if (cfg->scan_request->wdev->iftype != NL80211_IFTYPE_P2P_DEVICE)
			dev = cfg->scan_request->wdev->netdev;
#endif /* WL_ENABLE_P2P_IF */
	}
	else {
		WL_DBG(("cfg->scan_request is NULL may be internal scan."
			"doing scan_abort for ndev %p primary %p",
				ndev, bcmcfg_to_prmry_ndev(cfg)));
		dev = ndev;
	}
	if (fw_abort && !in_atomic())
		wl_cfg80211_scan_abort(cfg);
	if (timer_pending(&cfg->scan_timeout))
		del_timer_sync(&cfg->scan_timeout);
#if defined(ESCAN_RESULT_PATCH)
	if (likely(cfg->scan_request)) {
		cfg->bss_list = wl_escan_get_buf(cfg, aborted);
		wl_inform_bss(cfg);
	}
#endif /* ESCAN_RESULT_PATCH */
	spin_lock_irqsave(&cfg->cfgdrv_lock, flags);
#ifdef WL_SCHED_SCAN
	if (cfg->sched_scan_req && !cfg->scan_request) {
		WL_PNO((">>> REPORTING SCHED SCAN RESULTS \n"));
		if (!aborted) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0))
			cfg80211_sched_scan_results(cfg->sched_scan_req->wiphy, 0);
#else
			cfg80211_sched_scan_results(cfg->sched_scan_req->wiphy);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)) */
		}

		DBG_EVENT_LOG(dhdp, WIFI_EVENT_DRIVER_PNO_SCAN_COMPLETE);
		cfg->sched_scan_running = FALSE;
	}
#endif /* WL_SCHED_SCAN */
	if (likely(cfg->scan_request)) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0))
		cfg80211_scan_done(cfg->scan_request, &info);
#else
		cfg80211_scan_done(cfg->scan_request, aborted);
#endif
		cfg->scan_request = NULL;
		DHD_OS_SCAN_WAKE_UNLOCK((dhd_pub_t *)(cfg->pub));
		DHD_ENABLE_RUNTIME_PM((dhd_pub_t *)(cfg->pub));
	}
	if (p2p_is_on(cfg))
		wl_clr_p2p_status(cfg, SCANNING);
	wl_clr_drv_status(cfg, SCANNING, dev);
	wake_up_interruptible(&dhdp->conf->event_complete);
	spin_unlock_irqrestore(&cfg->cfgdrv_lock, flags);

out:
	mutex_unlock(&cfg->scan_complete);
	return err;
}

#ifdef ESCAN_BUF_OVERFLOW_MGMT
#ifndef WL_DRV_AVOID_SCANCACHE
static void
wl_cfg80211_find_removal_candidate(wl_bss_info_t *bss, removal_element_t *candidate)
{
	int idx;
	for (idx = 0; idx < BUF_OVERFLOW_MGMT_COUNT; idx++) {
		int len = BUF_OVERFLOW_MGMT_COUNT - idx - 1;
		if (bss->RSSI < candidate[idx].RSSI) {
			if (len)
				memcpy(&candidate[idx + 1], &candidate[idx],
					sizeof(removal_element_t) * len);
			candidate[idx].RSSI = bss->RSSI;
			candidate[idx].length = bss->length;
			memcpy(&candidate[idx].BSSID, &bss->BSSID, ETHER_ADDR_LEN);
			return;
		}
	}
}

static void
wl_cfg80211_remove_lowRSSI_info(wl_scan_results_t *list, removal_element_t *candidate,
	wl_bss_info_t *bi)
{
	int idx1, idx2;
	int total_delete_len = 0;
	for (idx1 = 0; idx1 < BUF_OVERFLOW_MGMT_COUNT; idx1++) {
		int cur_len = WL_SCAN_RESULTS_FIXED_SIZE;
		wl_bss_info_t *bss = NULL;
		if (candidate[idx1].RSSI >= bi->RSSI)
			continue;
		for (idx2 = 0; idx2 < list->count; idx2++) {
			bss = bss ? (wl_bss_info_t *)((uintptr)bss + dtoh32(bss->length)) :
				list->bss_info;
			if (!bcmp(&candidate[idx1].BSSID, &bss->BSSID, ETHER_ADDR_LEN) &&
				candidate[idx1].RSSI == bss->RSSI &&
				candidate[idx1].length == dtoh32(bss->length)) {
				u32 delete_len = dtoh32(bss->length);
				WL_DBG(("delete scan info of " MACDBG " to add new AP\n",
					MAC2STRDBG(bss->BSSID.octet)));
				if (idx2 < list->count -1) {
					memmove((u8 *)bss, (u8 *)bss + delete_len,
						list->buflen - cur_len - delete_len);
				}
				list->buflen -= delete_len;
				list->count--;
				total_delete_len += delete_len;
				/* if delete_len is greater than or equal to result length */
				if (total_delete_len >= bi->length) {
					return;
				}
				break;
			}
			cur_len += dtoh32(bss->length);
		}
	}
}
#endif /* WL_DRV_AVOID_SCANCACHE */
#endif /* ESCAN_BUF_OVERFLOW_MGMT */

#ifdef WL_DRV_AVOID_SCANCACHE
static u32 wl_p2p_find_peer_channel(struct bcm_cfg80211 *cfg, s32 status, wl_bss_info_t *bi,
		u32 bi_length)
{
	u32 ret;
	u8 *p2p_dev_addr = NULL;

	ret = wl_get_drv_status_all(cfg, FINDING_COMMON_CHANNEL);
	if (!ret) {
		return ret;
	}
	if (status == WLC_E_STATUS_PARTIAL) {
		p2p_dev_addr = wl_cfgp2p_retreive_p2p_dev_addr(bi, bi_length);
		if (p2p_dev_addr && !memcmp(p2p_dev_addr,
			cfg->afx_hdl->tx_dst_addr.octet, ETHER_ADDR_LEN)) {
			s32 channel = wf_chspec_ctlchan(
				wl_chspec_driver_to_host(bi->chanspec));

			if ((channel > MAXCHANNEL) || (channel <= 0))
				channel = WL_INVALID;
			else
				WL_ERR(("ACTION FRAME SCAN : Peer " MACDBG " found,"
					" channel : %d\n",
					MAC2STRDBG(cfg->afx_hdl->tx_dst_addr.octet),
					channel));

			wl_clr_p2p_status(cfg, SCANNING);
			cfg->afx_hdl->peer_chan = channel;
			complete(&cfg->act_frm_scan);
		}
	} else {
		WL_INFORM(("ACTION FRAME SCAN DONE\n"));
		wl_clr_p2p_status(cfg, SCANNING);
		wl_clr_drv_status(cfg, SCANNING, cfg->afx_hdl->dev);
		if (cfg->afx_hdl->peer_chan == WL_INVALID)
			complete(&cfg->act_frm_scan);
	}

	return ret;
}

static s32 wl_escan_without_scan_cache(struct bcm_cfg80211 *cfg, wl_escan_result_t *escan_result,
	struct net_device *ndev, const wl_event_msg_t *e, s32 status)
{
	s32 err = BCME_OK;
	wl_bss_info_t *bi;
	u32 bi_length;
	bool aborted = false;
	bool fw_abort = false;
	bool notify_escan_complete = false;

	if (wl_escan_check_sync_id(status, escan_result->sync_id,
		cfg->escan_info.cur_sync_id) < 0) {
		goto exit;
	}

	wl_escan_print_sync_id(status, escan_result->sync_id,
			cfg->escan_info.cur_sync_id);

	if (!(status == WLC_E_STATUS_TIMEOUT) || !(status == WLC_E_STATUS_PARTIAL)) {
		cfg->escan_info.escan_state = WL_ESCAN_STATE_IDLE;
	}

	if ((likely(cfg->scan_request)) || (cfg->sched_scan_running)) {
		notify_escan_complete = true;
	}

	if (status == WLC_E_STATUS_PARTIAL) {
		WL_INFORM(("WLC_E_STATUS_PARTIAL \n"));
		DBG_EVENT_LOG((dhd_pub_t *)cfg->pub, WIFI_EVENT_DRIVER_SCAN_RESULT_FOUND);
		if ((!escan_result) || (dtoh16(escan_result->bss_count) != 1)) {
			WL_ERR(("Invalid escan result (NULL pointer) or invalid bss_count\n"));
			goto exit;
		}

		bi = escan_result->bss_info;
		bi_length = dtoh32(bi->length);
		if ((!bi) ||
		(bi_length != (dtoh32(escan_result->buflen) - WL_ESCAN_RESULTS_FIXED_SIZE))) {
			WL_ERR(("Invalid escan bss info (NULL pointer)"
				"or invalid bss_info length\n"));
			goto exit;
		}

		if (!(bcmcfg_to_wiphy(cfg)->interface_modes & BIT(NL80211_IFTYPE_ADHOC))) {
			if (dtoh16(bi->capability) & DOT11_CAP_IBSS) {
				WL_DBG(("Ignoring IBSS result\n"));
				goto exit;
			}
		}

		if (wl_p2p_find_peer_channel(cfg, status, bi, bi_length)) {
			goto exit;
		} else {
			if (scan_req_match(cfg)) {
				/* p2p scan && allow only probe response */
				if ((cfg->p2p->search_state != WL_P2P_DISC_ST_SCAN) &&
					(bi->flags & WL_BSS_FLAGS_FROM_BEACON))
					goto exit;
			}
			err = wl_inform_single_bss(cfg, bi, false);

			/*
			 * !Broadcast && number of ssid = 1 && number of channels =1
			 * means specific scan to association
			 */
			if (wl_cfgp2p_is_p2p_specific_scan(cfg->scan_request)) {
				WL_ERR(("P2P assoc scan fast aborted.\n"));
				aborted = false;
				fw_abort = true;
			}
			/* Directly exit from function here and
			* avoid sending notify completion to cfg80211
			*/
			goto exit;
		}
	} else if (status == WLC_E_STATUS_SUCCESS) {
		if (wl_p2p_find_peer_channel(cfg, status, NULL, 0)) {
			goto exit;
		}
		WL_INFORM(("ESCAN COMPLETED\n"));
		DBG_EVENT_LOG((dhd_pub_t *)cfg->pub, WIFI_EVENT_DRIVER_SCAN_COMPLETE);

		/* Update escan complete status */
		aborted = false;
		fw_abort = false;

#ifdef CUSTOMER_HW4_DEBUG
		if (wl_scan_timeout_dbg_enabled)
			wl_scan_timeout_dbg_clear();
#endif /* CUSTOMER_HW4_DEBUG */
	} else if ((status == WLC_E_STATUS_ABORT) || (status == WLC_E_STATUS_NEWSCAN) ||
		(status == WLC_E_STATUS_11HQUIET) || (status == WLC_E_STATUS_CS_ABORT) ||
		(status == WLC_E_STATUS_NEWASSOC)) {
		/* Handle all cases of scan abort */

		WL_DBG(("ESCAN ABORT reason: %d\n", status));
		if (wl_p2p_find_peer_channel(cfg, status, NULL, 0)) {
			goto exit;
		}
		WL_INFORM(("ESCAN ABORTED\n"));

		/* Update escan complete status */
		aborted = true;
		fw_abort = false;

	} else if (status == WLC_E_STATUS_TIMEOUT) {
		WL_ERR(("WLC_E_STATUS_TIMEOUT : scan_request[%p]\n", cfg->scan_request));
		WL_ERR(("reason[0x%x]\n", e->reason));
		if (e->reason == 0xFFFFFFFF) {
			/* Update escan complete status */
			aborted = true;
			fw_abort = true;
		}
	} else {
		WL_ERR(("unexpected Escan Event %d : abort\n", status));

		if (wl_p2p_find_peer_channel(cfg, status, NULL, 0)) {
			goto exit;
		}
		/* Update escan complete status */
		aborted = true;
		fw_abort = false;
	}

	/* Notify escan complete status */
	if (notify_escan_complete) {
		wl_notify_escan_complete(cfg, ndev, aborted, fw_abort);
	}

exit:
	return err;

}
#endif /* WL_DRV_AVOID_SCANCACHE */

static s32 wl_escan_handler(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	s32 err = BCME_OK;
	s32 status = ntoh32(e->status);
	wl_escan_result_t *escan_result;
	struct net_device *ndev = NULL;
#ifndef WL_DRV_AVOID_SCANCACHE
	wl_bss_info_t *bi;
	u32 bi_length;
	wifi_p2p_ie_t * p2p_ie;
	u8 *p2p_dev_addr = NULL;
	wl_scan_results_t *list;
	wl_bss_info_t *bss = NULL;
	u32 i;
#endif /* WL_DRV_AVOID_SCANCACHE */
	u16 channel;
	struct ieee80211_supported_band *band;

	WL_DBG((" enter event type : %d, status : %d \n",
		ntoh32(e->event_type), ntoh32(e->status)));

	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);

	mutex_lock(&cfg->usr_sync);
	/* P2P SCAN is coming from primary interface */
	if (wl_get_p2p_status(cfg, SCANNING)) {
		if (wl_get_drv_status_all(cfg, SENDING_ACT_FRM))
			ndev = cfg->afx_hdl->dev;
		else
			ndev = cfg->escan_info.ndev;

	}
	if (!ndev || (!wl_get_drv_status(cfg, SCANNING, ndev) && !cfg->sched_scan_running)) {
		WL_ERR_RLMT(("escan is not ready. drv_scan_status 0x%x"
			" e_type %d e_states %d\n",
			wl_get_drv_status(cfg, SCANNING, ndev),
			ntoh32(e->event_type), ntoh32(e->status)));
		goto exit;
	}
	escan_result = (wl_escan_result_t *)data;

#ifndef WL_DRV_AVOID_SCANCACHE
	if (status == WLC_E_STATUS_PARTIAL) {
		WL_INFORM(("WLC_E_STATUS_PARTIAL \n"));
		DBG_EVENT_LOG((dhd_pub_t *)cfg->pub, WIFI_EVENT_DRIVER_SCAN_RESULT_FOUND);
		if (!escan_result) {
			WL_ERR(("Invalid escan result (NULL pointer)\n"));
			goto exit;
		}
		if ((dtoh32(escan_result->buflen) > (int)ESCAN_BUF_SIZE) ||
		    (dtoh32(escan_result->buflen) < sizeof(wl_escan_result_t))) {
			WL_ERR(("Invalid escan buffer len:%d\n", dtoh32(escan_result->buflen)));
			goto exit;
		}
		if (dtoh16(escan_result->bss_count) != 1) {
			WL_ERR(("Invalid bss_count %d: ignoring\n", escan_result->bss_count));
			goto exit;
		}
		bi = escan_result->bss_info;
		if (!bi) {
			WL_ERR(("Invalid escan bss info (NULL pointer)\n"));
			goto exit;
		}
		bi_length = dtoh32(bi->length);
		if (bi_length != (dtoh32(escan_result->buflen) - WL_ESCAN_RESULTS_FIXED_SIZE)) {
			WL_ERR(("Invalid bss_info length %d: ignoring\n", bi_length));
			goto exit;
		}

		/* +++++ terence 20130524: skip invalid bss */
		channel =
			bi->ctl_ch ? bi->ctl_ch : CHSPEC_CHANNEL(wl_chspec_driver_to_host(bi->chanspec));
		if (channel <= CH_MAX_2G_CHANNEL)
			band = bcmcfg_to_wiphy(cfg)->bands[IEEE80211_BAND_2GHZ];
		else
			band = bcmcfg_to_wiphy(cfg)->bands[IEEE80211_BAND_5GHZ];
		if (!band) {
			WL_ERR(("No valid band\n"));
			goto exit;
		}
		if (!dhd_conf_match_channel(cfg->pub, channel))
			goto exit;
		/* ----- terence 20130524: skip invalid bss */

		if (wl_escan_check_sync_id(status, escan_result->sync_id,
				cfg->escan_info.cur_sync_id) < 0)
			goto exit;

		if (!(bcmcfg_to_wiphy(cfg)->interface_modes & BIT(NL80211_IFTYPE_ADHOC))) {
			if (dtoh16(bi->capability) & DOT11_CAP_IBSS) {
				WL_DBG(("Ignoring IBSS result\n"));
				goto exit;
			}
		}

		if (wl_get_drv_status_all(cfg, FINDING_COMMON_CHANNEL)) {
			p2p_dev_addr = wl_cfgp2p_retreive_p2p_dev_addr(bi, bi_length);
			if (p2p_dev_addr && !memcmp(p2p_dev_addr,
				cfg->afx_hdl->tx_dst_addr.octet, ETHER_ADDR_LEN)) {
				s32 channel = wf_chspec_ctlchan(
					wl_chspec_driver_to_host(bi->chanspec));

				if ((channel > MAXCHANNEL) || (channel <= 0))
					channel = WL_INVALID;
				else
					WL_ERR(("ACTION FRAME SCAN : Peer " MACDBG " found,"
						" channel : %d\n",
						MAC2STRDBG(cfg->afx_hdl->tx_dst_addr.octet),
						channel));

				wl_clr_p2p_status(cfg, SCANNING);
				cfg->afx_hdl->peer_chan = channel;
				complete(&cfg->act_frm_scan);
				goto exit;
			}

		} else {
			int cur_len = WL_SCAN_RESULTS_FIXED_SIZE;
#ifdef ESCAN_BUF_OVERFLOW_MGMT
			removal_element_t candidate[BUF_OVERFLOW_MGMT_COUNT];
			int remove_lower_rssi = FALSE;

			bzero(candidate, sizeof(removal_element_t)*BUF_OVERFLOW_MGMT_COUNT);
#endif /* ESCAN_BUF_OVERFLOW_MGMT */

			list = wl_escan_get_buf(cfg, FALSE);
			if (scan_req_match(cfg)) {
				/* p2p scan && allow only probe response */
				if ((cfg->p2p->search_state != WL_P2P_DISC_ST_SCAN) &&
					(bi->flags & WL_BSS_FLAGS_FROM_BEACON))
					goto exit;
				if ((p2p_ie = wl_cfgp2p_find_p2pie(((u8 *) bi) + bi->ie_offset,
					bi->ie_length)) == NULL) {
						WL_ERR(("Couldn't find P2PIE in probe"
							" response/beacon\n"));
						goto exit;
				}
			}
#ifdef ESCAN_BUF_OVERFLOW_MGMT
			if (bi_length > ESCAN_BUF_SIZE - list->buflen)
				remove_lower_rssi = TRUE;
#endif /* ESCAN_BUF_OVERFLOW_MGMT */

			WL_DBG(("%s("MACDBG") RSSI %d flags 0x%x length %d\n", bi->SSID,
				MAC2STRDBG(bi->BSSID.octet), bi->RSSI, bi->flags, bi->length));
			for (i = 0; i < list->count; i++) {
				bss = bss ? (wl_bss_info_t *)((uintptr)bss + dtoh32(bss->length))
					: list->bss_info;
				if (!bss) {
					WL_ERR(("bss is NULL\n"));
					goto exit;
				}
#ifdef ESCAN_BUF_OVERFLOW_MGMT
				WL_DBG(("%s("MACDBG"), i=%d bss: RSSI %d list->count %d\n",
					bss->SSID, MAC2STRDBG(bss->BSSID.octet),
					i, bss->RSSI, list->count));

				if (remove_lower_rssi)
					wl_cfg80211_find_removal_candidate(bss, candidate);
#endif /* ESCAN_BUF_OVERFLOW_MGMT */

				if (!bcmp(&bi->BSSID, &bss->BSSID, ETHER_ADDR_LEN) &&
					(CHSPEC_BAND(wl_chspec_driver_to_host(bi->chanspec))
					== CHSPEC_BAND(wl_chspec_driver_to_host(bss->chanspec))) &&
					bi->SSID_len == bss->SSID_len &&
					!bcmp(bi->SSID, bss->SSID, bi->SSID_len)) {

					/* do not allow beacon data to update
					*the data recd from a probe response
					*/
					if (!(bss->flags & WL_BSS_FLAGS_FROM_BEACON) &&
						(bi->flags & WL_BSS_FLAGS_FROM_BEACON))
						goto exit;

					WL_DBG(("%s("MACDBG"), i=%d prev: RSSI %d"
						" flags 0x%x, new: RSSI %d flags 0x%x\n",
						bss->SSID, MAC2STRDBG(bi->BSSID.octet), i,
						bss->RSSI, bss->flags, bi->RSSI, bi->flags));

					if ((bss->flags & WL_BSS_FLAGS_RSSI_ONCHANNEL) ==
						(bi->flags & WL_BSS_FLAGS_RSSI_ONCHANNEL)) {
						/* preserve max RSSI if the measurements are
						* both on-channel or both off-channel
						*/
						WL_DBG(("%s("MACDBG"), same onchan"
						", RSSI: prev %d new %d\n",
						bss->SSID, MAC2STRDBG(bi->BSSID.octet),
						bss->RSSI, bi->RSSI));
						bi->RSSI = MAX(bss->RSSI, bi->RSSI);
					} else if ((bss->flags & WL_BSS_FLAGS_RSSI_ONCHANNEL) &&
						(bi->flags & WL_BSS_FLAGS_RSSI_ONCHANNEL) == 0) {
						/* preserve the on-channel rssi measurement
						* if the new measurement is off channel
						*/
						WL_DBG(("%s("MACDBG"), prev onchan"
						", RSSI: prev %d new %d\n",
						bss->SSID, MAC2STRDBG(bi->BSSID.octet),
						bss->RSSI, bi->RSSI));
						bi->RSSI = bss->RSSI;
						bi->flags |= WL_BSS_FLAGS_RSSI_ONCHANNEL;
					}
					if (dtoh32(bss->length) != bi_length) {
						u32 prev_len = dtoh32(bss->length);

						WL_DBG(("bss info replacement"
							" is occured(bcast:%d->probresp%d)\n",
							bss->ie_length, bi->ie_length));
						WL_DBG(("%s("MACDBG"), replacement!(%d -> %d)\n",
						bss->SSID, MAC2STRDBG(bi->BSSID.octet),
						prev_len, bi_length));

						if (list->buflen - prev_len + bi_length
							> ESCAN_BUF_SIZE) {
							WL_ERR(("Buffer is too small: keep the"
								" previous result of this AP\n"));
							/* Only update RSSI */
							bss->RSSI = bi->RSSI;
							bss->flags |= (bi->flags
								& WL_BSS_FLAGS_RSSI_ONCHANNEL);
							goto exit;
						}

						if (i < list->count - 1) {
							/* memory copy required by this case only */
							memmove((u8 *)bss + bi_length,
								(u8 *)bss + prev_len,
								list->buflen - cur_len - prev_len);
						}
						list->buflen -= prev_len;
						list->buflen += bi_length;
					}
					list->version = dtoh32(bi->version);
					memcpy((u8 *)bss, (u8 *)bi, bi_length);
					goto exit;
				}
				cur_len += dtoh32(bss->length);
			}
			if (bi_length > ESCAN_BUF_SIZE - list->buflen) {
#ifdef ESCAN_BUF_OVERFLOW_MGMT
				wl_cfg80211_remove_lowRSSI_info(list, candidate, bi);
				if (bi_length > ESCAN_BUF_SIZE - list->buflen) {
					WL_DBG(("RSSI(" MACDBG ") is too low(%d) to add Buffer\n",
						MAC2STRDBG(bi->BSSID.octet), bi->RSSI));
					goto exit;
				}
#else
				WL_ERR(("Buffer is too small: ignoring\n"));
				goto exit;
#endif /* ESCAN_BUF_OVERFLOW_MGMT */
			}

			memcpy(&(((char *)list)[list->buflen]), bi, bi_length);
			list->version = dtoh32(bi->version);
			list->buflen += bi_length;
			list->count++;

			/*
			 * !Broadcast && number of ssid = 1 && number of channels =1
			 * means specific scan to association
			 */
			if (wl_cfgp2p_is_p2p_specific_scan(cfg->scan_request)) {
				WL_ERR(("P2P assoc scan fast aborted.\n"));
				wl_notify_escan_complete(cfg, cfg->escan_info.ndev, false, true);
				goto exit;
			}
		}
	}
	else if (status == WLC_E_STATUS_SUCCESS) {
		cfg->escan_info.escan_state = WL_ESCAN_STATE_IDLE;
		wl_escan_print_sync_id(status, cfg->escan_info.cur_sync_id,
			escan_result->sync_id);

		if (wl_get_drv_status_all(cfg, FINDING_COMMON_CHANNEL)) {
			WL_INFORM(("ACTION FRAME SCAN DONE\n"));
			wl_clr_p2p_status(cfg, SCANNING);
			wl_clr_drv_status(cfg, SCANNING, cfg->afx_hdl->dev);
			if (cfg->afx_hdl->peer_chan == WL_INVALID)
				complete(&cfg->act_frm_scan);
		} else if ((likely(cfg->scan_request)) || (cfg->sched_scan_running)) {
			WL_INFORM(("ESCAN COMPLETED\n"));
			DBG_EVENT_LOG((dhd_pub_t *)cfg->pub, WIFI_EVENT_DRIVER_SCAN_COMPLETE);
			cfg->bss_list = wl_escan_get_buf(cfg, FALSE);
			if (!scan_req_match(cfg)) {
				WL_DBG(("SCAN COMPLETED: scanned AP count=%d\n",
					cfg->bss_list->count));
			}
			wl_inform_bss(cfg);
			wl_notify_escan_complete(cfg, ndev, false, false);
		}
		wl_escan_increment_sync_id(cfg, SCAN_BUF_NEXT);
#ifdef CUSTOMER_HW4_DEBUG
		if (wl_scan_timeout_dbg_enabled)
			wl_scan_timeout_dbg_clear();
#endif /* CUSTOMER_HW4_DEBUG */
	} else if ((status == WLC_E_STATUS_ABORT) || (status == WLC_E_STATUS_NEWSCAN) ||
		(status == WLC_E_STATUS_11HQUIET) || (status == WLC_E_STATUS_CS_ABORT) ||
		(status == WLC_E_STATUS_NEWASSOC)) {
		/* Handle all cases of scan abort */
		cfg->escan_info.escan_state = WL_ESCAN_STATE_IDLE;
		wl_escan_print_sync_id(status, escan_result->sync_id,
			cfg->escan_info.cur_sync_id);
		WL_DBG(("ESCAN ABORT reason: %d\n", status));
		if (wl_get_drv_status_all(cfg, FINDING_COMMON_CHANNEL)) {
			WL_INFORM(("ACTION FRAME SCAN DONE\n"));
			wl_clr_drv_status(cfg, SCANNING, cfg->afx_hdl->dev);
			wl_clr_p2p_status(cfg, SCANNING);
			if (cfg->afx_hdl->peer_chan == WL_INVALID)
				complete(&cfg->act_frm_scan);
		} else if ((likely(cfg->scan_request)) || (cfg->sched_scan_running)) {
			WL_INFORM(("ESCAN ABORTED\n"));
			cfg->bss_list = wl_escan_get_buf(cfg, TRUE);
			if (!scan_req_match(cfg)) {
				WL_TRACE_HW4(("SCAN ABORTED: scanned AP count=%d\n",
					cfg->bss_list->count));
			}
			wl_inform_bss(cfg);
			wl_notify_escan_complete(cfg, ndev, true, false);
		} else {
			/* If there is no pending host initiated scan, do nothing */
			WL_DBG(("ESCAN ABORT: No pending scans. Ignoring event.\n"));
		}
		wl_escan_increment_sync_id(cfg, SCAN_BUF_CNT);
	} else if (status == WLC_E_STATUS_TIMEOUT) {
		WL_ERR(("WLC_E_STATUS_TIMEOUT : scan_request[%p]\n", cfg->scan_request));
		WL_ERR(("reason[0x%x]\n", e->reason));
		if (e->reason == 0xFFFFFFFF) {
			wl_notify_escan_complete(cfg, cfg->escan_info.ndev, true, true);
		}
	} else {
		WL_ERR(("unexpected Escan Event %d : abort\n", status));
		cfg->escan_info.escan_state = WL_ESCAN_STATE_IDLE;
		wl_escan_print_sync_id(status, escan_result->sync_id,
			cfg->escan_info.cur_sync_id);
		if (wl_get_drv_status_all(cfg, FINDING_COMMON_CHANNEL)) {
			WL_INFORM(("ACTION FRAME SCAN DONE\n"));
			wl_clr_p2p_status(cfg, SCANNING);
			wl_clr_drv_status(cfg, SCANNING, cfg->afx_hdl->dev);
			if (cfg->afx_hdl->peer_chan == WL_INVALID)
				complete(&cfg->act_frm_scan);
		} else if ((likely(cfg->scan_request)) || (cfg->sched_scan_running)) {
			cfg->bss_list = wl_escan_get_buf(cfg, TRUE);
			if (!scan_req_match(cfg)) {
				WL_TRACE_HW4(("SCAN ABORTED(UNEXPECTED): "
					"scanned AP count=%d\n",
					cfg->bss_list->count));
			}
			wl_inform_bss(cfg);
			wl_notify_escan_complete(cfg, ndev, true, false);
		}
		wl_escan_increment_sync_id(cfg, 2);
	}
#else /* WL_DRV_AVOID_SCANCACHE */
	err = wl_escan_without_scan_cache(cfg, escan_result, ndev, e, status);
#endif /* WL_DRV_AVOID_SCANCACHE */
exit:
	mutex_unlock(&cfg->usr_sync);
	return err;
}

static void wl_cfg80211_concurrent_roam(struct bcm_cfg80211 *cfg, int enable)
{
	u32 connected_cnt  = wl_get_drv_status_all(cfg, CONNECTED);
	bool p2p_connected  = wl_cfgp2p_vif_created(cfg);
	struct net_info *iter, *next;

	if (!(cfg->roam_flags & WL_ROAM_OFF_ON_CONCURRENT))
		return;

	WL_DBG(("roam off:%d p2p_connected:%d connected_cnt:%d \n",
		enable, p2p_connected, connected_cnt));
	/* Disable FW roam when we have a concurrent P2P connection */
	if (enable && p2p_connected && connected_cnt > 1) {

		/* Mark it as to be reverted */
		cfg->roam_flags |= WL_ROAM_REVERT_STATUS;
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"")
#endif
		for_each_ndev(cfg, iter, next) {
			if (iter->ndev && iter->wdev &&
					iter->wdev->iftype == NL80211_IFTYPE_STATION) {
				if (wldev_iovar_setint(iter->ndev, "roam_off", TRUE)
						== BCME_OK) {
					iter->roam_off = TRUE;
				}
				else {
					WL_ERR(("error to enable roam_off\n"));
				}
			}
		}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic pop")
#endif
	}
	else if (!enable && (cfg->roam_flags & WL_ROAM_REVERT_STATUS)) {
		cfg->roam_flags &= ~WL_ROAM_REVERT_STATUS;
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"")
#endif
		for_each_ndev(cfg, iter, next) {
			if (iter->ndev && iter->wdev &&
					iter->wdev->iftype == NL80211_IFTYPE_STATION) {
				if (iter->roam_off != WL_INVALID) {
					if (wldev_iovar_setint(iter->ndev, "roam_off", FALSE)
							== BCME_OK) {
						iter->roam_off = FALSE;
					}
					else {
						WL_ERR(("error to disable roam_off\n"));
					}
				}
			}
		}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic pop")
#endif
	}

	return;
}

static void wl_cfg80211_determine_vsdb_mode(struct bcm_cfg80211 *cfg)
{
	struct net_info *iter, *next;
	u32 ctl_chan = 0;
	u32 chanspec = 0;
	u32 pre_ctl_chan = 0;
	u32 connected_cnt  = wl_get_drv_status_all(cfg, CONNECTED);
	cfg->vsdb_mode = false;

	if (connected_cnt <= 1)  {
		return;
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"")
#endif
	for_each_ndev(cfg, iter, next) {
		/* p2p discovery iface ndev could be null */
		if (iter->ndev) {
			chanspec = 0;
			ctl_chan = 0;
			if (wl_get_drv_status(cfg, CONNECTED, iter->ndev)) {
				if (wldev_iovar_getint(iter->ndev, "chanspec",
					(s32 *)&chanspec) == BCME_OK) {
					chanspec = wl_chspec_driver_to_host(chanspec);
					ctl_chan = wf_chspec_ctlchan(chanspec);
					wl_update_prof(cfg, iter->ndev, NULL,
						&ctl_chan, WL_PROF_CHAN);
				}
				if (!cfg->vsdb_mode) {
					if (!pre_ctl_chan && ctl_chan)
						pre_ctl_chan = ctl_chan;
					else if (pre_ctl_chan && (pre_ctl_chan != ctl_chan)) {
						cfg->vsdb_mode = true;
					}
				}
			}
		}
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic pop")
#endif
	WL_MSG("wlan", "%s concurrency is enabled\n", cfg->vsdb_mode ? "Multi Channel" : "Same Channel");
	return;
}

#if defined(DISABLE_FRAMEBURST_VSDB) && defined(USE_WFA_CERT_CONF)
extern int g_frameburst;
#endif /* DISABLE_FRAMEBURST_VSDB && USE_WFA_CERT_CONF */

static s32 wl_notifier_change_state(struct bcm_cfg80211 *cfg, struct net_info *_net_info,
	enum wl_status state, bool set)
{
	s32 pm = PM_FAST;
	s32 err = BCME_OK;
	u32 mode;
	u32 chan = 0;
	struct net_device *primary_dev = bcmcfg_to_prmry_ndev(cfg);
	dhd_pub_t *dhd = cfg->pub;
#ifdef RTT_SUPPORT
	rtt_status_info_t *rtt_status;
#endif /* RTT_SUPPORT */
	if (dhd->busstate == DHD_BUS_DOWN) {
		WL_ERR(("%s : busstate is DHD_BUS_DOWN!\n", __FUNCTION__));
		return 0;
	}
	WL_DBG(("Enter state %d set %d _net_info->pm_restore %d iface %s\n",
		state, set, _net_info->pm_restore, _net_info->ndev->name));

	if (state != WL_STATUS_CONNECTED)
		return 0;
	mode = wl_get_mode_by_netdev(cfg, _net_info->ndev);
	if (set) {
		wl_cfg80211_concurrent_roam(cfg, 1);
		wl_cfg80211_determine_vsdb_mode(cfg);
		if (mode == WL_MODE_AP) {
			if (wl_add_remove_eventmsg(primary_dev, WLC_E_P2P_PROBREQ_MSG, false))
				WL_ERR((" failed to unset WLC_E_P2P_PROPREQ_MSG\n"));
		}
		pm = PM_OFF;
		if ((err = wldev_ioctl_set(_net_info->ndev, WLC_SET_PM, &pm,
				sizeof(pm))) != 0) {
			if (err == -ENODEV)
				WL_DBG(("%s:netdev not ready\n",
					_net_info->ndev->name));
			else
				WL_ERR(("%s:error (%d)\n",
					_net_info->ndev->name, err));

			wl_cfg80211_update_power_mode(_net_info->ndev);
		}
		wl_add_remove_pm_enable_work(cfg, WL_PM_WORKQ_SHORT);

	} else { /* clear */
		chan = 0;
		/* clear chan information when the net device is disconnected */
		wl_update_prof(cfg, _net_info->ndev, NULL, &chan, WL_PROF_CHAN);
		wl_cfg80211_determine_vsdb_mode(cfg);
		if (primary_dev == _net_info->ndev) {
			pm = PM_FAST;
#ifdef RTT_SUPPORT
			rtt_status = GET_RTTSTATE(dhd);
			if (rtt_status->status != RTT_ENABLED) {
#endif /* RTT_SUPPORT */
				if (dhd_conf_get_pm(dhd) >= 0)
					pm = dhd_conf_get_pm(dhd);
				if ((err = wldev_ioctl_set(_net_info->ndev, WLC_SET_PM, &pm,
						sizeof(pm))) != 0) {
					if (err == -ENODEV)
						WL_DBG(("%s:netdev not ready\n",
							_net_info->ndev->name));
					else
						WL_ERR(("%s:error (%d)\n",
							_net_info->ndev->name, err));

					wl_cfg80211_update_power_mode(_net_info->ndev);
				}
#ifdef RTT_SUPPORT
			}
#endif /* RTT_SUPPORT */
		}
		wl_cfg80211_concurrent_roam(cfg, 0);

	}
	return err;
}

static s32 wl_init_scan(struct bcm_cfg80211 *cfg)
{
	int err = 0;

	cfg->evt_handler[WLC_E_ESCAN_RESULT] = wl_escan_handler;
	cfg->escan_info.escan_state = WL_ESCAN_STATE_IDLE;
	wl_escan_init_sync_id(cfg);

	/* Init scan_timeout timer */
	init_timer_compat(&cfg->scan_timeout, wl_scan_timeout, cfg);

	return err;
}

#ifdef DHD_LOSSLESS_ROAMING
static s32 wl_init_roam_timeout(struct bcm_cfg80211 *cfg)
{
	int err = 0;

	/* Init roam timer */
	init_timer_compat(&cfg->roam_timeout, wl_roam_timeout, cfg);

	return err;
}
#endif /* DHD_LOSSLESS_ROAMING */

static s32 wl_init_priv(struct bcm_cfg80211 *cfg)
{
	struct wiphy *wiphy = bcmcfg_to_wiphy(cfg);
	struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);
	s32 err = 0;

	cfg->scan_request = NULL;
	cfg->pwr_save = !!(wiphy->flags & WIPHY_FLAG_PS_ON_BY_DEFAULT);
#ifdef DISABLE_BUILTIN_ROAM
	cfg->roam_on = false;
#else
	cfg->roam_on = true;
#endif /* DISABLE_BUILTIN_ROAM */
	cfg->active_scan = true;
	cfg->rf_blocked = false;
	cfg->vsdb_mode = false;
#if defined(BCMSDIO) || defined(BCMDBUS)
	cfg->wlfc_on = false;
#endif /* BCMSDIO || BCMDBUS */
	cfg->roam_flags |= WL_ROAM_OFF_ON_CONCURRENT;
	cfg->disable_roam_event = false;
	/* register interested state */
	set_bit(WL_STATUS_CONNECTED, &cfg->interrested_state);
	spin_lock_init(&cfg->cfgdrv_lock);
	mutex_init(&cfg->ioctl_buf_sync);
	init_waitqueue_head(&cfg->netif_change_event);
	init_waitqueue_head(&cfg->wps_done_event);
	init_completion(&cfg->send_af_done);
	init_completion(&cfg->iface_disable);
	mutex_init(&cfg->usr_sync);
	mutex_init(&cfg->event_sync);
	mutex_init(&cfg->scan_complete);
	mutex_init(&cfg->if_sync);
	mutex_init(&cfg->pm_sync);
	mutex_init(&cfg->in4way_sync);
#ifdef WLTDLS
	mutex_init(&cfg->tdls_sync);
#endif /* WLTDLS */
	wl_init_eq(cfg);
	err = wl_init_priv_mem(cfg);
	if (err)
		return err;
	if (wl_create_event_handler(cfg))
		return -ENOMEM;
	wl_init_event_handler(cfg);
	err = wl_init_scan(cfg);
	if (err)
		return err;
#ifdef DHD_LOSSLESS_ROAMING
	err = wl_init_roam_timeout(cfg);
	if (err) {
		return err;
	}
#endif /* DHD_LOSSLESS_ROAMING */
	wl_init_conf(cfg->conf);
	wl_init_prof(cfg, ndev);
	wl_link_down(cfg);
	DNGL_FUNC(dhd_cfg80211_init, (cfg));
#ifdef NAN_DP
	cfg->nan_dp_state = NAN_DP_STATE_DISABLED;
	init_waitqueue_head(&cfg->ndp_if_change_event);
#endif /* NAN_DP */
	return err;
}

static void wl_deinit_priv(struct bcm_cfg80211 *cfg)
{
	DNGL_FUNC(dhd_cfg80211_deinit, (cfg));
	wl_destroy_event_handler(cfg);
	wl_flush_eq(cfg);
	wl_link_down(cfg);
	del_timer_sync(&cfg->scan_timeout);
#ifdef DHD_LOSSLESS_ROAMING
	del_timer_sync(&cfg->roam_timeout);
#endif
	wl_deinit_priv_mem(cfg);
	if (wl_cfg80211_netdev_notifier_registered) {
		wl_cfg80211_netdev_notifier_registered = FALSE;
		unregister_netdevice_notifier(&wl_cfg80211_netdev_notifier);
	}
}

#if defined(WL_ENABLE_P2P_IF)
static s32 wl_cfg80211_attach_p2p(struct bcm_cfg80211 *cfg)
{
	WL_TRACE(("Enter \n"));

	if (wl_cfgp2p_register_ndev(cfg) < 0) {
		WL_ERR(("P2P attach failed. \n"));
		return -ENODEV;
	}

	return 0;
}

static s32  wl_cfg80211_detach_p2p(struct bcm_cfg80211 *cfg)
{
	struct wireless_dev *wdev;

	WL_DBG(("Enter \n"));
	if (!cfg) {
		WL_ERR(("Invalid Ptr\n"));
		return -EINVAL;
	}
	else {
		wdev = cfg->p2p_wdev;
		if (!wdev) {
			WL_ERR(("Invalid Ptr\n"));
			return -EINVAL;
		}
	}

	wl_cfgp2p_unregister_ndev(cfg);

	cfg->p2p_wdev = NULL;
	cfg->p2p_net = NULL;
	WL_DBG(("Freeing 0x%p \n", wdev));
	kfree(wdev);

	return 0;
}
#endif 

static s32 wl_cfg80211_attach_post(struct net_device *ndev)
{
	struct bcm_cfg80211 * cfg;
	s32 err = 0;
	s32 ret = 0;
	WL_TRACE(("In\n"));
	if (unlikely(!ndev)) {
		WL_ERR(("ndev is invaild\n"));
		return -ENODEV;
	}
	cfg = wl_get_cfg(ndev);
	if (unlikely(!cfg)) {
		WL_ERR(("cfg is invaild\n"));
		return -EINVAL;
	}
	if (!wl_get_drv_status(cfg, READY, ndev)) {
		if (cfg->wdev) {
			ret = wl_cfgp2p_supported(cfg, ndev);
			if (ret > 0) {
#if !defined(WL_ENABLE_P2P_IF)
				cfg->wdev->wiphy->interface_modes |=
					(BIT(NL80211_IFTYPE_P2P_CLIENT)|
					BIT(NL80211_IFTYPE_P2P_GO));
#endif /* !WL_ENABLE_P2P_IF */
				if ((err = wl_cfgp2p_init_priv(cfg)) != 0)
					goto fail;

#if defined(WL_ENABLE_P2P_IF)
				if (cfg->p2p_net) {
					/* Update MAC addr for p2p0 interface here. */
					memcpy(cfg->p2p_net->dev_addr, ndev->dev_addr, ETH_ALEN);
					cfg->p2p_net->dev_addr[0] |= 0x02;
					WL_MSG(cfg->p2p_net->name, "p2p_dev_addr="MACDBG "\n",
						MAC2STRDBG(cfg->p2p_net->dev_addr));
				} else {
					WL_ERR(("p2p_net not yet populated."
					" Couldn't update the MAC Address for p2p0 \n"));
					return -ENODEV;
				}
#endif /* WL_ENABLE_P2P_IF */
				cfg->p2p_supported = true;
			} else if (ret == 0) {
				if ((err = wl_cfgp2p_init_priv(cfg)) != 0)
					goto fail;
			} else {
				/* SDIO bus timeout */
				err = -ENODEV;
				goto fail;
			}
		}
	}
	wl_set_drv_status(cfg, READY, ndev);
fail:
	return err;
}

struct bcm_cfg80211 *wl_get_cfg(struct net_device *ndev)
{
	struct wireless_dev *wdev = ndev->ieee80211_ptr;

	if (!wdev || !wdev->wiphy)
		return NULL;

	return wiphy_priv(wdev->wiphy);
}

s32 wl_cfg80211_attach(struct net_device *ndev, void *context)
{
	struct wireless_dev *wdev;
	struct bcm_cfg80211 *cfg;
	s32 err = 0;
	struct device *dev;

	WL_TRACE(("In\n"));
	if (!ndev) {
		WL_ERR(("ndev is invaild\n"));
		return -ENODEV;
	}
	WL_DBG(("func %p\n", wl_cfg80211_get_parent_dev()));
	dev = wl_cfg80211_get_parent_dev();

	wdev = kzalloc(sizeof(*wdev), GFP_KERNEL);
	if (unlikely(!wdev)) {
		WL_ERR(("Could not allocate wireless device\n"));
		return -ENOMEM;
	}
	err = wl_setup_wiphy(wdev, dev, context);
	if (unlikely(err)) {
		kfree(wdev);
		return -ENOMEM;
	}
#ifdef WLMESH_CFG80211
	wdev->iftype = wl_mode_to_nl80211_iftype(WL_MODE_MESH);
#else
	wdev->iftype = wl_mode_to_nl80211_iftype(WL_MODE_BSS);
#endif
	cfg = wiphy_priv(wdev->wiphy);
	cfg->wdev = wdev;
	cfg->pub = context;
	INIT_LIST_HEAD(&cfg->net_list);
#ifdef WBTEXT
	INIT_LIST_HEAD(&cfg->wbtext_bssid_list);
#endif /* WBTEXT */
	INIT_LIST_HEAD(&cfg->vndr_oui_list);
	spin_lock_init(&cfg->net_list_sync);
	ndev->ieee80211_ptr = wdev;
	SET_NETDEV_DEV(ndev, wiphy_dev(wdev->wiphy));
	wdev->netdev = ndev;
	cfg->state_notifier = wl_notifier_change_state;
	err = wl_init_priv(cfg);
	if (err) {
		WL_ERR(("Failed to init iwm_priv (%d)\n", err));
		goto cfg80211_attach_out;
	}
	err = wl_alloc_netinfo(cfg, ndev, wdev, wdev->iftype, PM_ENABLE, 0);
	if (err) {
		WL_ERR(("Failed to alloc net_info (%d)\n", err));
		goto cfg80211_attach_out;
	}

	err = wl_setup_rfkill(cfg, TRUE);
	if (err) {
		WL_ERR(("Failed to setup rfkill %d\n", err));
		goto cfg80211_attach_out;
	}
#ifdef DEBUGFS_CFG80211
	err = wl_setup_debugfs(cfg);
	if (err) {
		WL_ERR(("Failed to setup debugfs %d\n", err));
		goto cfg80211_attach_out;
	}
#endif
	if (!wl_cfg80211_netdev_notifier_registered) {
		wl_cfg80211_netdev_notifier_registered = TRUE;
		err = register_netdevice_notifier(&wl_cfg80211_netdev_notifier);
		if (err) {
			wl_cfg80211_netdev_notifier_registered = FALSE;
			WL_ERR(("Failed to register notifierl %d\n", err));
			goto cfg80211_attach_out;
		}
	}
#if defined(COEX_DHCP)
	cfg->btcoex_info = wl_cfg80211_btcoex_init(cfg->wdev->netdev);
	if (!cfg->btcoex_info)
		goto cfg80211_attach_out;
#endif 
#if defined(SUPPORT_RANDOM_MAC_SCAN)
	cfg->random_mac_enabled = FALSE;
#endif /* SUPPORT_RANDOM_MAC_SCAN */

#ifdef CONFIG_CFG80211_INTERNAL_REGDB
	wdev->wiphy->reg_notifier = wl_cfg80211_reg_notifier;
#endif /* CONFIG_CFG80211_INTERNAL_REGDB */

#if defined(WL_ENABLE_P2P_IF)
	err = wl_cfg80211_attach_p2p(cfg);
	if (err)
		goto cfg80211_attach_out;
#endif 

	INIT_DELAYED_WORK(&cfg->pm_enable_work, wl_cfg80211_work_handler);

#if defined(STAT_REPORT)
	err = wl_attach_stat_report(cfg);
	if (err) {
		goto cfg80211_attach_out;
	}
#endif /* STAT_REPORT */
	return err;

cfg80211_attach_out:
	wl_cfg80211_detach(cfg);
	return err;
}

void wl_cfg80211_detach(struct bcm_cfg80211 *cfg)
{

	WL_TRACE(("In\n"));
	if (!cfg)
		return;

	wl_add_remove_pm_enable_work(cfg, WL_PM_WORKQ_DEL);

#if defined(COEX_DHCP)
	wl_cfg80211_btcoex_deinit();
	cfg->btcoex_info = NULL;
#endif 

	wl_setup_rfkill(cfg, FALSE);
#ifdef DEBUGFS_CFG80211
	wl_free_debugfs(cfg);
#endif
	if (cfg->p2p_supported) {
		if (timer_pending(&cfg->p2p->listen_timer))
			del_timer_sync(&cfg->p2p->listen_timer);
		wl_cfgp2p_deinit_priv(cfg);
	}

	if (timer_pending(&cfg->scan_timeout))
		del_timer_sync(&cfg->scan_timeout);
#ifdef DHD_LOSSLESS_ROAMING
	if (timer_pending(&cfg->roam_timeout)) {
		del_timer_sync(&cfg->roam_timeout);
	}
#endif /* DHD_LOSSLESS_ROAMING */

#if defined(WL_CFG80211_P2P_DEV_IF)
	if (cfg->p2p_wdev)
		wl_cfgp2p_del_p2p_disc_if(cfg->p2p_wdev, cfg);
#endif /* WL_CFG80211_P2P_DEV_IF  */
#if defined(WL_ENABLE_P2P_IF)
	wl_cfg80211_detach_p2p(cfg);
#endif 
#if defined(STAT_REPORT)
	wl_detach_stat_report(cfg);
#endif /* STAT_REPORT */

	wl_cfg80211_ibss_vsie_free(cfg);
	wl_cfg80211_clear_mgmt_vndr_ies(cfg);
	wl_deinit_priv(cfg);
	wl_cfg80211_clear_parent_dev();
#if defined(RSSIAVG)
	wl_free_rssi_cache(&cfg->g_rssi_cache_ctrl);
	wl_free_rssi_cache(&cfg->g_connected_rssi_cache_ctrl);
#endif
#if defined(BSSCACHE)
	wl_release_bss_cache_ctrl(&cfg->g_bss_cache_ctrl);
#endif
	wl_free_wdev(cfg);
	/* PLEASE do NOT call any function after wl_free_wdev, the driver's private
	 * structure "cfg", which is the private part of wiphy, has been freed in
	 * wl_free_wdev !!!!!!!!!!!
	 */
}

static void wl_event_handler(struct work_struct *work_data)
{
	struct bcm_cfg80211 *cfg = NULL;
	struct wl_event_q *e;
	struct wireless_dev *wdev = NULL;

	BCM_SET_CONTAINER_OF(cfg, work_data, struct bcm_cfg80211, event_work);
	DHD_EVENT_WAKE_LOCK(cfg->pub);
	while ((e = wl_deq_event(cfg))) {
		WL_DBG(("event type (%d), ifidx: %d bssidx: %d \n",
			e->etype, e->emsg.ifidx, e->emsg.bsscfgidx));

		if (e->emsg.ifidx > WL_MAX_IFS) {
			WL_ERR((" Event ifidx not in range. val:%d \n", e->emsg.ifidx));
			goto fail;
		}

		/* Make sure iface operations, don't creat race conditions */
		mutex_lock(&cfg->if_sync);
		if (!(wdev = wl_get_wdev_by_bssidx(cfg, e->emsg.bsscfgidx))) {
			/* For WLC_E_IF would be handled by wl_host_event */
			if (e->etype != WLC_E_IF)
				WL_ERR(("No wdev corresponding to bssidx: 0x%x found!"
					" Ignoring event.\n", e->emsg.bsscfgidx));
		} else if (e->etype < WLC_E_LAST && cfg->evt_handler[e->etype]) {
			dhd_pub_t *dhd = (struct dhd_pub *)(cfg->pub);
			if (dhd->busstate == DHD_BUS_DOWN) {
				WL_ERR((": BUS is DOWN.\n"));
			} else
				cfg->evt_handler[e->etype](cfg, wdev_to_cfgdev(wdev),
					&e->emsg, e->edata);
		} else {
			WL_DBG(("Unknown Event (%d): ignoring\n", e->etype));
		}
		mutex_unlock(&cfg->if_sync);
fail:
		wl_put_event(e);
	}
	DHD_EVENT_WAKE_UNLOCK(cfg->pub);
}

void
wl_cfg80211_event(struct net_device *ndev, const wl_event_msg_t * e, void *data)
{
	u32 event_type = ntoh32(e->event_type);
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	struct net_info *netinfo;

	WL_DBG(("event_type (%d): %s\n", event_type, bcmevent_get_name(event_type)));

	if ((cfg == NULL) || (cfg->p2p_supported && cfg->p2p == NULL)) {
		WL_ERR(("Stale event ignored\n"));
		return;
	}

	if (cfg->event_workq == NULL) {
		WL_ERR(("Event handler is not created\n"));
		return;
	}

	if (wl_get_p2p_status(cfg, IF_CHANGING) || wl_get_p2p_status(cfg, IF_ADDING)) {
		WL_ERR(("during IF change, ignore event %d\n", event_type));
		return;
	}

	netinfo = wl_get_netinfo_by_bssidx(cfg, e->bsscfgidx);
	if (!netinfo) {
		/* Since the netinfo entry is not there, the netdev entry is not
		 * created via cfg80211 interface. so the event is not of interest
		 * to the cfg80211 layer.
		 */
		WL_ERR(("ignore event %d, not interested\n", event_type));
		return;
	}

	if (event_type == WLC_E_PFN_NET_FOUND) {
		WL_DBG((" PNOEVENT: PNO_NET_FOUND\n"));
	}
	else if (event_type == WLC_E_PFN_NET_LOST) {
		WL_DBG((" PNOEVENT: PNO_NET_LOST\n"));
	}

	if (likely(!wl_enq_event(cfg, ndev, event_type, e, data))) {
		queue_work(cfg->event_workq, &cfg->event_work);
	}
}

static void wl_init_eq(struct bcm_cfg80211 *cfg)
{
	wl_init_eq_lock(cfg);
	INIT_LIST_HEAD(&cfg->eq_list);
}

static void wl_flush_eq(struct bcm_cfg80211 *cfg)
{
	struct wl_event_q *e;
	unsigned long flags;

	flags = wl_lock_eq(cfg);
	while (!list_empty_careful(&cfg->eq_list)) {
		BCM_SET_LIST_FIRST_ENTRY(e, &cfg->eq_list, struct wl_event_q, eq_list);
		list_del(&e->eq_list);
		kfree(e);
	}
	wl_unlock_eq(cfg, flags);
}

/*
* retrieve first queued event from head
*/

static struct wl_event_q *wl_deq_event(struct bcm_cfg80211 *cfg)
{
	struct wl_event_q *e = NULL;
	unsigned long flags;

	flags = wl_lock_eq(cfg);
	if (likely(!list_empty(&cfg->eq_list))) {
		BCM_SET_LIST_FIRST_ENTRY(e, &cfg->eq_list, struct wl_event_q, eq_list);
		list_del(&e->eq_list);
	}
	wl_unlock_eq(cfg, flags);

	return e;
}

/*
 * push event to tail of the queue
 */

static s32
wl_enq_event(struct bcm_cfg80211 *cfg, struct net_device *ndev, u32 event,
	const wl_event_msg_t *msg, void *data)
{
	struct wl_event_q *e;
	s32 err = 0;
	uint32 evtq_size;
	uint32 data_len;
	unsigned long flags;
	gfp_t aflags;

	data_len = 0;
	if (data)
		data_len = ntoh32(msg->datalen);
	evtq_size = sizeof(struct wl_event_q) + data_len;
	aflags = (in_atomic()) ? GFP_ATOMIC : GFP_KERNEL;
	e = kzalloc(evtq_size, aflags);
	if (unlikely(!e)) {
		WL_ERR(("event alloc failed\n"));
		return -ENOMEM;
	}
	e->etype = event;
	memcpy(&e->emsg, msg, sizeof(wl_event_msg_t));
	if (data)
		memcpy(e->edata, data, data_len);
	flags = wl_lock_eq(cfg);
	list_add_tail(&e->eq_list, &cfg->eq_list);
	wl_unlock_eq(cfg, flags);

	return err;
}

static void wl_put_event(struct wl_event_q *e)
{
	kfree(e);
}

static s32 wl_config_ifmode(struct bcm_cfg80211 *cfg, struct net_device *ndev, s32 iftype)
{
	s32 infra = 0;
	s32 err = 0;
	s32 mode = 0;
	switch (iftype) {
	case NL80211_IFTYPE_MONITOR:
	case NL80211_IFTYPE_WDS:
		WL_ERR(("type (%d) : currently we do not support this mode\n",
			iftype));
		err = -EINVAL;
		return err;
	case NL80211_IFTYPE_ADHOC:
		mode = WL_MODE_IBSS;
		break;
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
		mode = WL_MODE_BSS;
		infra = 1;
		break;
#ifdef WLMESH_CFG80211
	case NL80211_IFTYPE_MESH_POINT:
		mode = WL_MODE_MESH;
		infra = WL_BSSTYPE_MESH;
		break;
#endif /* WLMESH_CFG80211 */
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_P2P_GO:
		mode = WL_MODE_AP;
		infra = 1;
		break;
	default:
		err = -EINVAL;
		WL_ERR(("invalid type (%d)\n", iftype));
		return err;
	}
	infra = htod32(infra);
	err = wldev_ioctl_set(ndev, WLC_SET_INFRA, &infra, sizeof(infra));
	if (unlikely(err)) {
		WL_ERR(("WLC_SET_INFRA error (%d)\n", err));
		return err;
	}

	wl_set_mode_by_netdev(cfg, ndev, mode);

	return 0;
}

void wl_cfg80211_add_to_eventbuffer(struct wl_eventmsg_buf *ev, u16 event, bool set)
{
	if (!ev || (event > WLC_E_LAST))
		return;

	if (ev->num < MAX_EVENT_BUF_NUM) {
		ev->event[ev->num].type = event;
		ev->event[ev->num].set = set;
		ev->num++;
	} else {
		WL_ERR(("evenbuffer doesn't support > %u events. Update"
			" the define MAX_EVENT_BUF_NUM \n", MAX_EVENT_BUF_NUM));
		ASSERT(0);
	}
}

s32 wl_cfg80211_apply_eventbuffer(
	struct net_device *ndev,
	struct bcm_cfg80211 *cfg,
	wl_eventmsg_buf_t *ev)
{
	char eventmask[WL_EVENTING_MASK_LEN];
	int i, ret = 0;
	s8 iovbuf[WL_EVENTING_MASK_LEN + 12];

	if (!ev || (!ev->num))
		return -EINVAL;

	mutex_lock(&cfg->event_sync);

	/* Read event_msgs mask */
	ret = wldev_iovar_getbuf(ndev, "event_msgs", NULL, 0, iovbuf, sizeof(iovbuf), NULL);
	if (unlikely(ret)) {
		WL_ERR(("Get event_msgs error (%d)\n", ret));
		goto exit;
	}
	memcpy(eventmask, iovbuf, WL_EVENTING_MASK_LEN);

	/* apply the set bits */
	for (i = 0; i < ev->num; i++) {
		if (ev->event[i].set)
			setbit(eventmask, ev->event[i].type);
		else
			clrbit(eventmask, ev->event[i].type);
	}

	/* Write updated Event mask */
	ret = wldev_iovar_setbuf(ndev, "event_msgs", eventmask, sizeof(eventmask), iovbuf,
			sizeof(iovbuf), NULL);
	if (unlikely(ret)) {
		WL_ERR(("Set event_msgs error (%d)\n", ret));
	}

exit:
	mutex_unlock(&cfg->event_sync);
	return ret;
}

s32 wl_add_remove_eventmsg(struct net_device *ndev, u16 event, bool add)
{
	s8 iovbuf[WL_EVENTING_MASK_LEN + 12];
	s8 eventmask[WL_EVENTING_MASK_LEN];
	s32 err = 0;
	struct bcm_cfg80211 *cfg;

	if (!ndev)
		return -ENODEV;

	cfg = wl_get_cfg(ndev);
	if (!cfg)
		return -ENODEV;

	mutex_lock(&cfg->event_sync);

	/* Setup event_msgs */
	err = wldev_iovar_getbuf(ndev, "event_msgs", NULL, 0, iovbuf, sizeof(iovbuf), NULL);
	if (unlikely(err)) {
		WL_ERR(("Get event_msgs error (%d)\n", err));
		goto eventmsg_out;
	}
	memcpy(eventmask, iovbuf, WL_EVENTING_MASK_LEN);
	if (add) {
		setbit(eventmask, event);
	} else {
		clrbit(eventmask, event);
	}
	err = wldev_iovar_setbuf(ndev, "event_msgs", eventmask, WL_EVENTING_MASK_LEN, iovbuf,
			sizeof(iovbuf), NULL);
	if (unlikely(err)) {
		WL_ERR(("Set event_msgs error (%d)\n", err));
		goto eventmsg_out;
	}

eventmsg_out:
	mutex_unlock(&cfg->event_sync);
	return err;
}

static int wl_construct_reginfo(struct bcm_cfg80211 *cfg, s32 bw_cap)
{
	struct net_device *dev = bcmcfg_to_prmry_ndev(cfg);
	struct ieee80211_channel *band_chan_arr = NULL;
	wl_uint32_list_t *list;
	u32 i, j, index, n_2g, n_5g, band, channel, array_size;
	u32 *n_cnt = NULL;
	chanspec_t c = 0;
	s32 err = BCME_OK;
	bool update;
	bool ht40_allowed;
	u8 *pbuf = NULL;
	bool dfs_radar_disabled = FALSE;

#define LOCAL_BUF_LEN 1024
	pbuf = kzalloc(LOCAL_BUF_LEN, GFP_KERNEL);

	if (pbuf == NULL) {
		WL_ERR(("failed to allocate local buf\n"));
		return -ENOMEM;
	}

	err = wldev_iovar_getbuf_bsscfg(dev, "chanspecs", NULL,
		0, pbuf, LOCAL_BUF_LEN, 0, &cfg->ioctl_buf_sync);
	if (err != 0) {
		WL_ERR(("get chanspecs failed with %d\n", err));
		kfree(pbuf);
		return err;
	}
#undef LOCAL_BUF_LEN

	list = (wl_uint32_list_t *)(void *)pbuf;
	band = array_size = n_2g = n_5g = 0;
	for (i = 0; i < dtoh32(list->count); i++) {
		index = 0;
		update = false;
		ht40_allowed = false;
		c = (chanspec_t)dtoh32(list->element[i]);
		c = wl_chspec_driver_to_host(c);
		channel = wf_chspec_ctlchan(c);

		if (!CHSPEC_IS40(c) && ! CHSPEC_IS20(c)) {
			WL_DBG(("HT80/160/80p80 center channel : %d\n", channel));
			continue;
		}
		if (CHSPEC_IS2G(c) && (channel >= CH_MIN_2G_CHANNEL) &&
			(channel <= CH_MAX_2G_CHANNEL)) {
			band_chan_arr = __wl_2ghz_channels;
			array_size = ARRAYSIZE(__wl_2ghz_channels);
			n_cnt = &n_2g;
			band = IEEE80211_BAND_2GHZ;
			ht40_allowed = (bw_cap  == WLC_N_BW_40ALL)? true : false;
		} else if (CHSPEC_IS5G(c) && channel >= CH_MIN_5G_CHANNEL) {
			band_chan_arr = __wl_5ghz_a_channels;
			array_size = ARRAYSIZE(__wl_5ghz_a_channels);
			n_cnt = &n_5g;
			band = IEEE80211_BAND_5GHZ;
			ht40_allowed = (bw_cap  == WLC_N_BW_20ALL)? false : true;
		} else {
			WL_ERR(("Invalid channel Sepc. 0x%x.\n", c));
			continue;
		}
		if (!ht40_allowed && CHSPEC_IS40(c))
			continue;
		for (j = 0; (j < *n_cnt && (*n_cnt < array_size)); j++) {
			if (band_chan_arr[j].hw_value == channel) {
				update = true;
				break;
			}
		}
		if (update)
			index = j;
		else
			index = *n_cnt;
		if (!dhd_conf_match_channel(cfg->pub, channel))
			continue;
		if (index <  array_size) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 39)
			band_chan_arr[index].center_freq =
				ieee80211_channel_to_frequency(channel);
#else
			band_chan_arr[index].center_freq =
				ieee80211_channel_to_frequency(channel, band);
#endif
			band_chan_arr[index].hw_value = channel;
			band_chan_arr[index].beacon_found = false;

			if (CHSPEC_IS40(c) && ht40_allowed) {
				/* assuming the order is HT20, HT40 Upper,
				 *  HT40 lower from chanspecs
				 */
				u32 ht40_flag = band_chan_arr[index].flags & IEEE80211_CHAN_NO_HT40;
				if (CHSPEC_SB_UPPER(c)) {
					if (ht40_flag == IEEE80211_CHAN_NO_HT40)
						band_chan_arr[index].flags &=
							~IEEE80211_CHAN_NO_HT40;
					band_chan_arr[index].flags |= IEEE80211_CHAN_NO_HT40PLUS;
				} else {
					/* It should be one of
					 * IEEE80211_CHAN_NO_HT40 or IEEE80211_CHAN_NO_HT40PLUS
					 */
					band_chan_arr[index].flags &= ~IEEE80211_CHAN_NO_HT40;
					if (ht40_flag == IEEE80211_CHAN_NO_HT40)
						band_chan_arr[index].flags |=
							IEEE80211_CHAN_NO_HT40MINUS;
				}
			} else {
				band_chan_arr[index].flags = IEEE80211_CHAN_NO_HT40;
				if (!dfs_radar_disabled) {
					if (band == IEEE80211_BAND_2GHZ)
						channel |= WL_CHANSPEC_BAND_2G;
					else
						channel |= WL_CHANSPEC_BAND_5G;
					channel |= WL_CHANSPEC_BW_20;
					channel = wl_chspec_host_to_driver(channel);
					err = wldev_iovar_getint(dev, "per_chan_info", &channel);
					if (!err) {
						if (channel & WL_CHAN_RADAR) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0))
							band_chan_arr[index].flags |=
								(IEEE80211_CHAN_RADAR
								| IEEE80211_CHAN_NO_IBSS);
#else
							band_chan_arr[index].flags |=
								IEEE80211_CHAN_RADAR;
#endif
						}

						if (channel & WL_CHAN_PASSIVE)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0))
							band_chan_arr[index].flags |=
								IEEE80211_CHAN_PASSIVE_SCAN;
#else
							band_chan_arr[index].flags |=
								IEEE80211_CHAN_NO_IR;
#endif
					} else if (err == BCME_UNSUPPORTED) {
						dfs_radar_disabled = TRUE;
						WL_ERR(("does not support per_chan_info\n"));
					}
				}
			}
			if (!update)
				(*n_cnt)++;
		}

	}
	__wl_band_2ghz.n_channels = n_2g;
	__wl_band_5ghz_a.n_channels = n_5g;
	kfree(pbuf);
	return err;
}

static s32 __wl_update_wiphybands(struct bcm_cfg80211 *cfg, bool notify)
{
	struct wiphy *wiphy;
	struct net_device *dev = bcmcfg_to_prmry_ndev(cfg);
	u32 bandlist[3];
	u32 nband = 0;
	u32 i = 0;
	s32 err = 0;
	s32 index = 0;
	s32 nmode = 0;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
	u32 j = 0;
	s32 vhtmode = 0;
	s32 txstreams = 0;
	s32 rxstreams = 0;
	s32 ldpc_cap = 0;
	s32 stbc_rx = 0;
	s32 stbc_tx = 0;
	s32 txbf_bfe_cap = 0;
	s32 txbf_bfr_cap = 0;
#endif 
	s32 bw_cap = 0;
	s32 cur_band = -1;
	struct ieee80211_supported_band *bands[IEEE80211_NUM_BANDS] = {NULL, };

	memset(bandlist, 0, sizeof(bandlist));
	err = wldev_ioctl_get(dev, WLC_GET_BANDLIST, bandlist,
		sizeof(bandlist));
	if (unlikely(err)) {
		WL_ERR(("error read bandlist (%d)\n", err));
		return err;
	}
	err = wldev_ioctl_get(dev, WLC_GET_BAND, &cur_band,
		sizeof(s32));
	if (unlikely(err)) {
		WL_ERR(("error (%d)\n", err));
		return err;
	}

	err = wldev_iovar_getint(dev, "nmode", &nmode);
	if (unlikely(err)) {
		WL_ERR(("error reading nmode (%d)\n", err));
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
	err = wldev_iovar_getint(dev, "vhtmode", &vhtmode);
	if (unlikely(err)) {
		WL_ERR(("error reading vhtmode (%d)\n", err));
	}

	if (vhtmode) {
		err = wldev_iovar_getint(dev, "txstreams", &txstreams);
		if (unlikely(err)) {
			WL_ERR(("error reading txstreams (%d)\n", err));
		}

		err = wldev_iovar_getint(dev, "rxstreams", &rxstreams);
		if (unlikely(err)) {
			WL_ERR(("error reading rxstreams (%d)\n", err));
		}

		err = wldev_iovar_getint(dev, "ldpc_cap", &ldpc_cap);
		if (unlikely(err)) {
			WL_ERR(("error reading ldpc_cap (%d)\n", err));
		}

		err = wldev_iovar_getint(dev, "stbc_rx", &stbc_rx);
		if (unlikely(err)) {
			WL_ERR(("error reading stbc_rx (%d)\n", err));
		}

		err = wldev_iovar_getint(dev, "stbc_tx", &stbc_tx);
		if (unlikely(err)) {
			WL_ERR(("error reading stbc_tx (%d)\n", err));
		}

		err = wldev_iovar_getint(dev, "txbf_bfe_cap", &txbf_bfe_cap);
		if (unlikely(err)) {
			WL_ERR(("error reading txbf_bfe_cap (%d)\n", err));
		}

		err = wldev_iovar_getint(dev, "txbf_bfr_cap", &txbf_bfr_cap);
		if (unlikely(err)) {
			WL_ERR(("error reading txbf_bfr_cap (%d)\n", err));
		}
	}
#endif 

	/* For nmode and vhtmode   check bw cap */
	if (nmode ||
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
		vhtmode ||
#endif 
		0) {
		err = wldev_iovar_getint(dev, "mimo_bw_cap", &bw_cap);
		if (unlikely(err)) {
			WL_ERR(("error get mimo_bw_cap (%d)\n", err));
		}
	}

	err = wl_construct_reginfo(cfg, bw_cap);
	if (err) {
		WL_ERR(("wl_construct_reginfo() fails err=%d\n", err));
		if (err != BCME_UNSUPPORTED)
			return err;
	}

	wiphy = bcmcfg_to_wiphy(cfg);
	nband = bandlist[0];

	for (i = 1; i <= nband && i < ARRAYSIZE(bandlist); i++) {
		index = -1;
		if (bandlist[i] == WLC_BAND_5G && __wl_band_5ghz_a.n_channels > 0) {
			bands[IEEE80211_BAND_5GHZ] =
				&__wl_band_5ghz_a;
			index = IEEE80211_BAND_5GHZ;
			if (nmode && (bw_cap == WLC_N_BW_40ALL || bw_cap == WLC_N_BW_20IN2G_40IN5G))
				bands[index]->ht_cap.cap |= IEEE80211_HT_CAP_SGI_40;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
			/* VHT capabilities. */
			if (vhtmode) {
				/* Supported */
				bands[index]->vht_cap.vht_supported = TRUE;

				for (j = 1; j <= VHT_CAP_MCS_MAP_NSS_MAX; j++) {
					/* TX stream rates. */
					if (j <= txstreams) {
						VHT_MCS_MAP_SET_MCS_PER_SS(j, VHT_CAP_MCS_MAP_0_9,
							bands[index]->vht_cap.vht_mcs.tx_mcs_map);
					} else {
						VHT_MCS_MAP_SET_MCS_PER_SS(j, VHT_CAP_MCS_MAP_NONE,
							bands[index]->vht_cap.vht_mcs.tx_mcs_map);
					}

					/* RX stream rates. */
					if (j <= rxstreams) {
						VHT_MCS_MAP_SET_MCS_PER_SS(j, VHT_CAP_MCS_MAP_0_9,
							bands[index]->vht_cap.vht_mcs.rx_mcs_map);
					} else {
						VHT_MCS_MAP_SET_MCS_PER_SS(j, VHT_CAP_MCS_MAP_NONE,
							bands[index]->vht_cap.vht_mcs.rx_mcs_map);
					}
				}


				/* Capabilities */
				/* 80 MHz is mandatory */
				bands[index]->vht_cap.cap |=
					IEEE80211_VHT_CAP_SHORT_GI_80;

				if (WL_BW_CAP_160MHZ(bw_cap)) {
					bands[index]->vht_cap.cap |=
						IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ;
					bands[index]->vht_cap.cap |=
						IEEE80211_VHT_CAP_SHORT_GI_160;
				}

				bands[index]->vht_cap.cap |=
					IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454;

				if (ldpc_cap)
					bands[index]->vht_cap.cap |=
						IEEE80211_VHT_CAP_RXLDPC;

				if (stbc_tx)
					bands[index]->vht_cap.cap |=
						IEEE80211_VHT_CAP_TXSTBC;

				if (stbc_rx)
					bands[index]->vht_cap.cap |=
						(stbc_rx << VHT_CAP_INFO_RX_STBC_SHIFT);

				if (txbf_bfe_cap)
					bands[index]->vht_cap.cap |=
						IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE;

				if (txbf_bfr_cap) {
					bands[index]->vht_cap.cap |=
						IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE;
				}

				if (txbf_bfe_cap || txbf_bfr_cap) {
					bands[index]->vht_cap.cap |=
						(2 << VHT_CAP_INFO_NUM_BMFMR_ANT_SHIFT);
					bands[index]->vht_cap.cap |=
						((txstreams - 1) <<
							VHT_CAP_INFO_NUM_SOUNDING_DIM_SHIFT);
					bands[index]->vht_cap.cap |=
						IEEE80211_VHT_CAP_VHT_LINK_ADAPTATION_VHT_MRQ_MFB;
				}

				/* AMPDU length limit, support max 1MB (2 ^ (13 + 7)) */
				bands[index]->vht_cap.cap |=
					(7 << VHT_CAP_INFO_AMPDU_MAXLEN_EXP_SHIFT);
				WL_INFORM(("%s band[%d] vht_enab=%d vht_cap=%08x "
					"vht_rx_mcs_map=%04x vht_tx_mcs_map=%04x\n",
					__FUNCTION__, index,
					bands[index]->vht_cap.vht_supported,
					bands[index]->vht_cap.cap,
					bands[index]->vht_cap.vht_mcs.rx_mcs_map,
					bands[index]->vht_cap.vht_mcs.tx_mcs_map));
			}
#endif 
		}
		else if (bandlist[i] == WLC_BAND_2G && __wl_band_2ghz.n_channels > 0) {
			bands[IEEE80211_BAND_2GHZ] =
				&__wl_band_2ghz;
			index = IEEE80211_BAND_2GHZ;
			if (bw_cap == WLC_N_BW_40ALL)
				bands[index]->ht_cap.cap |= IEEE80211_HT_CAP_SGI_40;
		}

		if ((index >= 0) && nmode) {
			bands[index]->ht_cap.cap |=
				(IEEE80211_HT_CAP_SGI_20 | IEEE80211_HT_CAP_DSSSCCK40);
			bands[index]->ht_cap.ht_supported = TRUE;
			bands[index]->ht_cap.ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K;
			bands[index]->ht_cap.ampdu_density = IEEE80211_HT_MPDU_DENSITY_16;
			/* An HT shall support all EQM rates for one spatial stream */
			bands[index]->ht_cap.mcs.rx_mask[0] = 0xff;
		}

	}

	wiphy->bands[IEEE80211_BAND_2GHZ] = bands[IEEE80211_BAND_2GHZ];
	wiphy->bands[IEEE80211_BAND_5GHZ] = bands[IEEE80211_BAND_5GHZ];

	/* check if any bands populated otherwise makes 2Ghz as default */
	if (wiphy->bands[IEEE80211_BAND_2GHZ] == NULL &&
		wiphy->bands[IEEE80211_BAND_5GHZ] == NULL) {
		/* Setup 2Ghz band as default */
		wiphy->bands[IEEE80211_BAND_2GHZ] = &__wl_band_2ghz;
	}

	if (notify)
		wiphy_apply_custom_regulatory(wiphy, &brcm_regdom);

	return 0;
}

s32 wl_update_wiphybands(struct bcm_cfg80211 *cfg, bool notify)
{
	s32 err;

	mutex_lock(&cfg->usr_sync);
	err = __wl_update_wiphybands(cfg, notify);
	mutex_unlock(&cfg->usr_sync);

	return err;
}

static s32 __wl_cfg80211_up(struct bcm_cfg80211 *cfg)
{
	s32 err = 0;
	struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);
	struct wireless_dev *wdev = ndev->ieee80211_ptr;
#ifdef WBTEXT
	dhd_pub_t *dhd =  (dhd_pub_t *)(cfg->pub);
#endif /* WBTEXT */
#ifdef WLTDLS
	u32 tdls;
#endif /* WLTDLS */

	WL_DBG(("In\n"));

	if (!dhd_download_fw_on_driverload) {
		err = wl_create_event_handler(cfg);
		if (err) {
			WL_ERR(("wl_create_event_handler failed\n"));
			return err;
		}
		wl_init_event_handler(cfg);
	}

	err = dhd_config_dongle(cfg);
	if (unlikely(err))
		return err;

	err = wl_config_ifmode(cfg, ndev, wdev->iftype);
	if (unlikely(err && err != -EINPROGRESS)) {
		WL_ERR(("wl_config_ifmode failed\n"));
		if (err == -1) {
			WL_ERR(("return error %d\n", err));
			return err;
		}
	}

	err = wl_init_scan(cfg);
	if (err) {
		WL_ERR(("wl_init_scan failed\n"));
		return err;
	}
	err = __wl_update_wiphybands(cfg, true);
	if (unlikely(err)) {
		WL_ERR(("wl_update_wiphybands failed\n"));
		if (err == -1) {
			WL_ERR(("return error %d\n", err));
			return err;
		}
	}

#ifdef DHD_LOSSLESS_ROAMING
	if (timer_pending(&cfg->roam_timeout)) {
		del_timer_sync(&cfg->roam_timeout);
	}
#endif /* DHD_LOSSLESS_ROAMING */

	err = dhd_monitor_init(cfg->pub);

#ifdef WBTEXT
	/* when wifi up, set roam_prof to default value */
	if (dhd->wbtext_support) {
		if (dhd->op_mode & DHD_FLAG_STA_MODE) {
			wl_cfg80211_wbtext_set_default(ndev);
			wl_cfg80211_wbtext_clear_bssid_list(cfg);
		}
	}
#endif /* WBTEXT */
#ifdef WLTDLS
	if (wldev_iovar_getint(ndev, "tdls_enable", &tdls) == 0) {
		WL_DBG(("TDLS supported in fw\n"));
		cfg->tdls_supported = true;
	}
#endif /* WLTDLS */
	INIT_DELAYED_WORK(&cfg->pm_enable_work, wl_cfg80211_work_handler);
	wl_set_drv_status(cfg, READY, ndev);
	return err;
}

static s32 __wl_cfg80211_down(struct bcm_cfg80211 *cfg)
{
	s32 err = 0;
	unsigned long flags;
	struct net_info *iter, *next;
	struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);
#if defined(WL_CFG80211) && (defined(WL_ENABLE_P2P_IF) || \
	defined(WL_NEW_CFG_PRIVCMD_SUPPORT)) && !defined(PLATFORM_SLP)
	struct net_device *p2p_net = cfg->p2p_net;
#endif 
#ifdef PROP_TXSTATUS_VSDB
#if defined(BCMSDIO) || defined(BCMDBUS)
	dhd_pub_t *dhd =  (dhd_pub_t *)(cfg->pub);
#endif /* BCMSDIO || BCMDBUS */
#endif /* PROP_TXSTATUS_VSDB */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0))
	struct cfg80211_scan_info info;
#endif

	WL_DBG(("In\n"));

	/* Check if cfg80211 interface is already down */
	if (!wl_get_drv_status(cfg, READY, ndev)) {
		WL_DBG(("cfg80211 interface is already down\n"));
		return err;	/* it is even not ready */
	}

#ifdef WLTDLS
	cfg->tdls_supported = false;
#endif /* WLTDLS */

	/* Delete pm_enable_work */
	wl_add_remove_pm_enable_work(cfg, WL_PM_WORKQ_DEL);

	/* clear all the security setting on primary Interface */
	wl_cfg80211_clear_security(cfg);


	if (cfg->p2p_supported) {
		wl_clr_p2p_status(cfg, GO_NEG_PHASE);
#ifdef PROP_TXSTATUS_VSDB
#if defined(BCMSDIO) || defined(BCMDBUS)
		if (wl_cfgp2p_vif_created(cfg)) {
			bool enabled = false;
			dhd_wlfc_get_enable(dhd, &enabled);
			if (enabled && cfg->wlfc_on && dhd->op_mode != DHD_FLAG_HOSTAP_MODE &&
				dhd->op_mode != DHD_FLAG_IBSS_MODE) {
				dhd_wlfc_deinit(dhd);
				cfg->wlfc_on = false;
			}
		}
#endif /* BCMSDIO || BCMDBUS */
#endif /* PROP_TXSTATUS_VSDB */
	}


	/* clean up any left over interfaces */
	wl_cfg80211_cleanup_virtual_ifaces(ndev, false);

	/* If primary BSS is operational (for e.g SoftAP), bring it down */
	if (wl_cfg80211_bss_isup(ndev, 0)) {
		if (wl_cfg80211_bss_up(cfg, ndev, 0, 0) < 0)
			WL_ERR(("BSS down failed \n"));
	}

#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"")
#endif
	for_each_ndev(cfg, iter, next) {
		if (iter->ndev) /* p2p discovery iface is null */
			wl_set_drv_status(cfg, SCAN_ABORTING, iter->ndev);
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic pop")
#endif

#ifdef P2P_LISTEN_OFFLOADING
	wl_cfg80211_p2plo_deinit(cfg);
#endif /* P2P_LISTEN_OFFLOADING */

	spin_lock_irqsave(&cfg->cfgdrv_lock, flags);
	if (cfg->scan_request) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0))
		info.aborted = true;
		cfg80211_scan_done(cfg->scan_request, &info);
#else
		cfg80211_scan_done(cfg->scan_request, true);
#endif
		cfg->scan_request = NULL;
	}
	spin_unlock_irqrestore(&cfg->cfgdrv_lock, flags);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"")
#endif
	for_each_ndev(cfg, iter, next) {
		/* p2p discovery iface ndev ptr could be null */
		if (iter->ndev == NULL)
			continue;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
		if (wl_get_drv_status(cfg, CONNECTED, iter->ndev)) {
			CFG80211_DISCONNECTED(iter->ndev, 0, NULL, 0, false, GFP_KERNEL);
		}
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)) */
		wl_clr_drv_status(cfg, READY, iter->ndev);
		wl_clr_drv_status(cfg, SCANNING, iter->ndev);
		wl_clr_drv_status(cfg, SCAN_ABORTING, iter->ndev);
		wl_clr_drv_status(cfg, CONNECTING, iter->ndev);
		wl_clr_drv_status(cfg, CONNECTED, iter->ndev);
		wl_clr_drv_status(cfg, DISCONNECTING, iter->ndev);
		wl_clr_drv_status(cfg, AP_CREATED, iter->ndev);
		wl_clr_drv_status(cfg, AP_CREATING, iter->ndev);
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic pop")
#endif
	bcmcfg_to_prmry_ndev(cfg)->ieee80211_ptr->iftype =
		NL80211_IFTYPE_STATION;
#if defined(WL_CFG80211) && (defined(WL_ENABLE_P2P_IF) || \
	defined(WL_NEW_CFG_PRIVCMD_SUPPORT)) && !defined(PLATFORM_SLP)
		if (p2p_net)
			dev_close(p2p_net);
#endif 

	/* Avoid deadlock from wl_cfg80211_down */
	if (!dhd_download_fw_on_driverload) {
		mutex_unlock(&cfg->usr_sync);
		wl_destroy_event_handler(cfg);
		mutex_lock(&cfg->usr_sync);
	}

	wl_flush_eq(cfg);
	wl_link_down(cfg);
	if (cfg->p2p_supported) {
		if (timer_pending(&cfg->p2p->listen_timer))
			del_timer_sync(&cfg->p2p->listen_timer);
		wl_cfgp2p_down(cfg);
	}

	if (timer_pending(&cfg->scan_timeout)) {
		del_timer_sync(&cfg->scan_timeout);
	}

	DHD_OS_SCAN_WAKE_UNLOCK((dhd_pub_t *)(cfg->pub));

	dhd_monitor_uninit();
#ifdef WLAIBSS_MCHAN
	bcm_cfg80211_del_ibss_if(cfg->wdev->wiphy, cfg->ibss_cfgdev);
#endif /* WLAIBSS_MCHAN */


#ifdef WL11U
	/* Clear interworking element. */
	if (cfg->wl11u) {
		cfg->wl11u = FALSE;
	}
#endif /* WL11U */

#ifdef CUSTOMER_HW4_DEBUG
	if (wl_scan_timeout_dbg_enabled) {
		wl_scan_timeout_dbg_clear();
	}
#endif /* CUSTOMER_HW4_DEBUG */

	cfg->disable_roam_event = false;

	DNGL_FUNC(dhd_cfg80211_down, (cfg));

#ifdef DHD_IFDEBUG
	/* Printout all netinfo entries */
	wl_probe_wdev_all(cfg);
#endif /* DHD_IFDEBUG */

	return err;
}

s32 wl_cfg80211_up(struct net_device *net)
{
	struct bcm_cfg80211 *cfg;
	s32 err = 0;
	int val = 1;
	dhd_pub_t *dhd;
#ifdef DISABLE_PM_BCNRX
	s32 interr = 0;
	uint param = 0;
	s8 iovbuf[WLC_IOCTL_SMLEN];
#endif /* DISABLE_PM_BCNRX */

	WL_DBG(("In\n"));
	cfg = wl_get_cfg(net);

	if ((err = wldev_ioctl_get(bcmcfg_to_prmry_ndev(cfg), WLC_GET_VERSION, &val,
		sizeof(int)) < 0)) {
		WL_ERR(("WLC_GET_VERSION failed, err=%d\n", err));
		return err;
	}
	val = dtoh32(val);
	if (val != WLC_IOCTL_VERSION && val != 1) {
		WL_ERR(("Version mismatch, please upgrade. Got %d, expected %d or 1\n",
			val, WLC_IOCTL_VERSION));
		return BCME_VERSION;
	}
	ioctl_version = val;
	WL_TRACE(("WLC_GET_VERSION=%d\n", ioctl_version));
	wl_cfg80211_check_in4way(cfg, net, NO_SCAN_IN4WAY|NO_BTC_IN4WAY|WAIT_DISCONNECTED,
		WL_EXT_STATUS_DISCONNECTED, NULL);

	mutex_lock(&cfg->usr_sync);
	dhd = (dhd_pub_t *)(cfg->pub);
	if (!(dhd->op_mode & DHD_FLAG_HOSTAP_MODE)) {
		err = wl_cfg80211_attach_post(bcmcfg_to_prmry_ndev(cfg));
		if (unlikely(err)) {
			mutex_unlock(&cfg->usr_sync);
			return err;
		}
	}
#ifdef WLMESH_CFG80211
	cfg->wdev->wiphy->features |= NL80211_FEATURE_USERSPACE_MPM;
#endif /* WLMESH_CFG80211 */

	err = __wl_cfg80211_up(cfg);
	if (unlikely(err))
		WL_ERR(("__wl_cfg80211_up failed\n"));



	/* IOVAR configurations with 'up' condition */
#ifdef DISABLE_PM_BCNRX
	interr = wldev_iovar_setbuf(bcmcfg_to_prmry_ndev(cfg), "pm_bcnrx", (char *)&param,
			sizeof(param), iovbuf, sizeof(iovbuf), NULL);
	if (unlikely(interr)) {
		WL_ERR(("Set pm_bcnrx returned (%d)\n", interr));
	}
#endif /* DISABLE_PM_BCNRX */

	mutex_unlock(&cfg->usr_sync);

#ifdef WLAIBSS_MCHAN
	bcm_cfg80211_add_ibss_if(cfg->wdev->wiphy, IBSS_IF_NAME);
#endif /* WLAIBSS_MCHAN */

#ifdef DUAL_STA_STATIC_IF
#ifdef WL_VIRTUAL_APSTA
#error "Both DUAL STA and DUAL_STA_STATIC_IF can't be enabled together"
#endif
	/* Static Interface support is currently supported only for STA only builds (without P2P) */
	wl_cfg80211_create_iface(cfg->wdev->wiphy, NL80211_IFTYPE_STATION, NULL, "wlan%d");
#endif /* DUAL_STA_STATIC_IF */

	return err;
}

/* Private Event to Supplicant with indication that chip hangs */
int wl_cfg80211_hang(struct net_device *dev, u16 reason)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	dhd_pub_t *dhd;
#if defined(SOFTAP_SEND_HANGEVT)
	/* specifc mac address used for hang event */
	uint8 hang_mac[ETHER_ADDR_LEN] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11};
#endif /* SOFTAP_SEND_HANGEVT */
	if (!cfg) {
		return BCME_ERROR;
	}

	dhd = (dhd_pub_t *)(cfg->pub);
#if defined(DHD_HANG_SEND_UP_TEST)
	if (dhd->req_hang_type) {
		WL_ERR(("%s, Clear HANG test request 0x%x\n",
			__FUNCTION__, dhd->req_hang_type));
		dhd->req_hang_type = 0;
	}
#endif /* DHD_HANG_SEND_UP_TEST */
	if ((dhd->hang_reason <= HANG_REASON_MASK) || (dhd->hang_reason >= HANG_REASON_MAX)) {
		WL_ERR(("%s, Invalid hang reason 0x%x\n",
			__FUNCTION__, dhd->hang_reason));
		dhd->hang_reason = HANG_REASON_UNKNOWN;
	}
#ifdef DHD_USE_EXTENDED_HANG_REASON
	if (dhd->hang_reason != 0) {
		reason = dhd->hang_reason;
	}
#endif /* DHD_USE_EXTENDED_HANG_REASON */
	WL_ERR(("In : chip crash eventing, reason=0x%x\n", (uint32)(dhd->hang_reason)));

	wl_add_remove_pm_enable_work(cfg, WL_PM_WORKQ_DEL);
#ifdef SOFTAP_SEND_HANGEVT
	if (dhd->op_mode & DHD_FLAG_HOSTAP_MODE) {
		cfg80211_del_sta(dev, hang_mac, GFP_ATOMIC);
	} else
#endif /* SOFTAP_SEND_HANGEVT */
	{
		CFG80211_DISCONNECTED(dev, reason, NULL, 0, false, GFP_KERNEL);
	}
#if defined(RSSIAVG)
	wl_free_rssi_cache(&cfg->g_rssi_cache_ctrl);
#endif
#if defined(BSSCACHE)
	wl_free_bss_cache(&cfg->g_bss_cache_ctrl);
#endif
	if (cfg != NULL) {
		wl_link_down(cfg);
	}
	return 0;
}

s32 wl_cfg80211_down(struct net_device *dev)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	s32 err = 0;

	WL_DBG(("In\n"));
	if (cfg == NULL)
		return err;
	mutex_lock(&cfg->usr_sync);
#if defined(RSSIAVG)
	wl_free_rssi_cache(&cfg->g_rssi_cache_ctrl);
#endif
#if defined(BSSCACHE)
	wl_free_bss_cache(&cfg->g_bss_cache_ctrl);
#endif
	err = __wl_cfg80211_down(cfg);
	mutex_unlock(&cfg->usr_sync);

	return err;
}

static void *wl_read_prof(struct bcm_cfg80211 *cfg, struct net_device *ndev, s32 item)
{
	unsigned long flags;
	void *rptr = NULL;
	struct wl_profile *profile = wl_get_profile_by_netdev(cfg, ndev);

	if (!profile)
		return NULL;
	spin_lock_irqsave(&cfg->cfgdrv_lock, flags);
	switch (item) {
	case WL_PROF_SEC:
		rptr = &profile->sec;
		break;
	case WL_PROF_ACT:
		rptr = &profile->active;
		break;
	case WL_PROF_BSSID:
		rptr = profile->bssid;
		break;
	case WL_PROF_SSID:
		rptr = &profile->ssid;
		break;
	case WL_PROF_CHAN:
		rptr = &profile->channel;
		break;
	}
	spin_unlock_irqrestore(&cfg->cfgdrv_lock, flags);
	if (!rptr)
		WL_ERR(("invalid item (%d)\n", item));
	return rptr;
}

static s32
wl_update_prof(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *e, const void *data, s32 item)
{
	s32 err = 0;
	const struct wlc_ssid *ssid;
	unsigned long flags;
	struct wl_profile *profile = wl_get_profile_by_netdev(cfg, ndev);

	if (!profile)
		return WL_INVALID;
	spin_lock_irqsave(&cfg->cfgdrv_lock, flags);
	switch (item) {
	case WL_PROF_SSID:
		ssid = (const wlc_ssid_t *) data;
		memset(profile->ssid.SSID, 0,
			sizeof(profile->ssid.SSID));
		profile->ssid.SSID_len = MIN(ssid->SSID_len, DOT11_MAX_SSID_LEN);
		memcpy(profile->ssid.SSID, ssid->SSID, profile->ssid.SSID_len);
		break;
	case WL_PROF_BSSID:
		if (data)
			memcpy(profile->bssid, data, ETHER_ADDR_LEN);
		else
			memset(profile->bssid, 0, ETHER_ADDR_LEN);
		break;
	case WL_PROF_SEC:
		memcpy(&profile->sec, data, sizeof(profile->sec));
		break;
	case WL_PROF_ACT:
		profile->active = *(const bool *)data;
		break;
	case WL_PROF_BEACONINT:
		profile->beacon_interval = *(const u16 *)data;
		break;
	case WL_PROF_DTIMPERIOD:
		profile->dtim_period = *(const u8 *)data;
		break;
	case WL_PROF_CHAN:
		profile->channel = *(const u32*)data;
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}
	spin_unlock_irqrestore(&cfg->cfgdrv_lock, flags);

	if (err == -EOPNOTSUPP)
		WL_ERR(("unsupported item (%d)\n", item));

	return err;
}

void wl_cfg80211_dbg_level(u32 level)
{
	/*
	* prohibit to change debug level
	* by insmod parameter.
	* eventually debug level will be configured
	* in compile time by using CONFIG_XXX
	*/
	/* wl_dbg_level = level; */
}

static bool wl_is_ibssmode(struct bcm_cfg80211 *cfg, struct net_device *ndev)
{
	return wl_get_mode_by_netdev(cfg, ndev) == WL_MODE_IBSS;
}

static __used bool wl_is_ibssstarter(struct bcm_cfg80211 *cfg)
{
	return cfg->ibss_starter;
}

static void wl_rst_ie(struct bcm_cfg80211 *cfg)
{
	struct wl_ie *ie = wl_to_ie(cfg);

	ie->offset = 0;
}

static __used s32 wl_add_ie(struct bcm_cfg80211 *cfg, u8 t, u8 l, u8 *v)
{
	struct wl_ie *ie = wl_to_ie(cfg);
	s32 err = 0;

	if (unlikely(ie->offset + l + 2 > WL_TLV_INFO_MAX)) {
		WL_ERR(("ei crosses buffer boundary\n"));
		return -ENOSPC;
	}
	ie->buf[ie->offset] = t;
	ie->buf[ie->offset + 1] = l;
	memcpy(&ie->buf[ie->offset + 2], v, l);
	ie->offset += l + 2;

	return err;
}

static void wl_update_hidden_ap_ie(struct wl_bss_info *bi, const u8 *ie_stream, u32 *ie_size,
	bool roam)
{
	u8 *ssidie;
	int32 ssid_len = MIN(bi->SSID_len, DOT11_MAX_SSID_LEN);
	int32 remaining_ie_buf_len, available_buffer_len;
	/* cfg80211_find_ie defined in kernel returning const u8 */
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"")
#endif
	ssidie = (u8 *)cfg80211_find_ie(WLAN_EID_SSID, ie_stream, *ie_size);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic pop")
#endif
	/* ERROR out if
	 * 1. No ssid IE is FOUND or
	 * 2. New ssid length is > what was allocated for existing ssid (as
	 * we do not want to overwrite the rest of the IEs) or
	 * 3. If in case of erroneous buffer input where ssid length doesnt match the space
	 * allocated to it.
	 */
	if (!ssidie) {
		return;
	}
	available_buffer_len = ((int)(*ie_size)) - (ssidie + 2 - ie_stream);
	remaining_ie_buf_len = available_buffer_len - (int)ssidie[1];
	if ((ssid_len > ssidie[1]) ||
		(ssidie[1] > available_buffer_len)) {
		return;
	}


	if (ssidie[1] != ssid_len) {
		if (ssidie[1]) {
			WL_ERR(("%s: Wrong SSID len: %d != %d\n",
				__FUNCTION__, ssidie[1], bi->SSID_len));
		}
		if (roam) {
			WL_ERR(("Changing the SSID Info.\n"));
			memmove(ssidie + ssid_len + 2,
				(ssidie + 2) + ssidie[1],
				remaining_ie_buf_len);
			memcpy(ssidie + 2, bi->SSID, ssid_len);
			*ie_size = *ie_size + ssid_len - ssidie[1];
			ssidie[1] = ssid_len;
		}
		return;
	}
	if (*(ssidie + 2) == '\0')
		 memcpy(ssidie + 2, bi->SSID, ssid_len);
	return;
}

static s32 wl_mrg_ie(struct bcm_cfg80211 *cfg, u8 *ie_stream, u16 ie_size)
{
	struct wl_ie *ie = wl_to_ie(cfg);
	s32 err = 0;

	if (unlikely(ie->offset + ie_size > WL_TLV_INFO_MAX)) {
		WL_ERR(("ei_stream crosses buffer boundary\n"));
		return -ENOSPC;
	}
	memcpy(&ie->buf[ie->offset], ie_stream, ie_size);
	ie->offset += ie_size;

	return err;
}

static s32 wl_cp_ie(struct bcm_cfg80211 *cfg, u8 *dst, u16 dst_size)
{
	struct wl_ie *ie = wl_to_ie(cfg);
	s32 err = 0;

	if (unlikely(ie->offset > dst_size)) {
		WL_ERR(("dst_size is not enough\n"));
		return -ENOSPC;
	}
	memcpy(dst, &ie->buf[0], ie->offset);

	return err;
}

static u32 wl_get_ielen(struct bcm_cfg80211 *cfg)
{
	struct wl_ie *ie = wl_to_ie(cfg);

	return ie->offset;
}

static void wl_link_up(struct bcm_cfg80211 *cfg)
{
	cfg->link_up = true;
}

static void wl_link_down(struct bcm_cfg80211 *cfg)
{
	struct wl_connect_info *conn_info = wl_to_conn(cfg);

	WL_DBG(("In\n"));
	cfg->link_up = false;
	if (conn_info) {
		conn_info->req_ie_len = 0;
		conn_info->resp_ie_len = 0;
	}
}

static unsigned long wl_lock_eq(struct bcm_cfg80211 *cfg)
{
	unsigned long flags;

	spin_lock_irqsave(&cfg->eq_lock, flags);
	return flags;
}

static void wl_unlock_eq(struct bcm_cfg80211 *cfg, unsigned long flags)
{
	spin_unlock_irqrestore(&cfg->eq_lock, flags);
}

static void wl_init_eq_lock(struct bcm_cfg80211 *cfg)
{
	spin_lock_init(&cfg->eq_lock);
}

static void wl_delay(u32 ms)
{
	if (in_atomic() || (ms < jiffies_to_msecs(1))) {
		OSL_DELAY(ms*1000);
	} else {
		OSL_SLEEP(ms);
	}
}

s32 wl_cfg80211_get_p2p_dev_addr(struct net_device *net, struct ether_addr *p2pdev_addr)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(net);
	struct ether_addr primary_mac;
	if (!cfg->p2p)
		return -1;
	if (!p2p_is_on(cfg)) {
		get_primary_mac(cfg, &primary_mac);
		wl_cfgp2p_generate_bss_mac(cfg, &primary_mac);
	} else {
		memcpy(p2pdev_addr->octet, wl_to_p2p_bss_macaddr(cfg, P2PAPI_BSSCFG_DEVICE).octet,
			ETHER_ADDR_LEN);
	}

	return 0;
}
s32 wl_cfg80211_set_p2p_noa(struct net_device *net, char* buf, int len)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(net);

	return wl_cfgp2p_set_p2p_noa(cfg, net, buf, len);
}

s32 wl_cfg80211_get_p2p_noa(struct net_device *net, char* buf, int len)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(net);

	return wl_cfgp2p_get_p2p_noa(cfg, net, buf, len);
}

s32 wl_cfg80211_set_p2p_ps(struct net_device *net, char* buf, int len)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(net);

	return wl_cfgp2p_set_p2p_ps(cfg, net, buf, len);
}

s32 wl_cfg80211_set_p2p_ecsa(struct net_device *net, char* buf, int len)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(net);

	return wl_cfgp2p_set_p2p_ecsa(cfg, net, buf, len);
}

s32 wl_cfg80211_increase_p2p_bw(struct net_device *net, char* buf, int len)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(net);

	return wl_cfgp2p_increase_p2p_bw(cfg, net, buf, len);
}

#ifdef P2PLISTEN_AP_SAMECHN
s32 wl_cfg80211_set_p2p_resp_ap_chn(struct net_device *net, s32 enable)
{
	s32 ret = wldev_iovar_setint(net, "p2p_resp_ap_chn", enable);

	if ((ret == 0) && enable) {
		/* disable PM for p2p responding on infra AP channel */
		s32 pm = PM_OFF;

		ret = wldev_ioctl_set(net, WLC_SET_PM, &pm, sizeof(pm));
	}

	return ret;
}
#endif /* P2PLISTEN_AP_SAMECHN */

s32 wl_cfg80211_channel_to_freq(u32 channel)
{
	int freq = 0;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 39)
	freq = ieee80211_channel_to_frequency(channel);
#else
	{
		u16 band = 0;
		if (channel <= CH_MAX_2G_CHANNEL)
			band = IEEE80211_BAND_2GHZ;
		else
			band = IEEE80211_BAND_5GHZ;
		freq = ieee80211_channel_to_frequency(channel, band);
	}
#endif
	return freq;
}


#ifdef WLTDLS
static s32
wl_tdls_event_handler(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data) {

	struct net_device *ndev = NULL;
	u32 reason = ntoh32(e->reason);
	s8 *msg = NULL;

	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);

	switch (reason) {
	case WLC_E_TDLS_PEER_DISCOVERED :
		msg = " TDLS PEER DISCOVERD ";
		break;
	case WLC_E_TDLS_PEER_CONNECTED :
		if (cfg->tdls_mgmt_frame) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0))
			cfg80211_rx_mgmt(cfgdev, cfg->tdls_mgmt_freq, 0,
					cfg->tdls_mgmt_frame, cfg->tdls_mgmt_frame_len, 0);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0))
			cfg80211_rx_mgmt(cfgdev, cfg->tdls_mgmt_freq, 0,
					cfg->tdls_mgmt_frame, cfg->tdls_mgmt_frame_len,	0,
					GFP_ATOMIC);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)) || \
			defined(WL_COMPAT_WIRELESS)
			cfg80211_rx_mgmt(cfgdev, cfg->tdls_mgmt_freq, 0,
					cfg->tdls_mgmt_frame, cfg->tdls_mgmt_frame_len,
					GFP_ATOMIC);
#else
			cfg80211_rx_mgmt(cfgdev, cfg->tdls_mgmt_freq,
					cfg->tdls_mgmt_frame, cfg->tdls_mgmt_frame_len, GFP_ATOMIC);
#endif 
		}
		msg = " TDLS PEER CONNECTED ";
#ifdef SUPPORT_SET_CAC
		/* TDLS connect reset CAC */
		wl_cfg80211_set_cac(cfg, 0);
#endif /* SUPPORT_SET_CAC */
		break;
	case WLC_E_TDLS_PEER_DISCONNECTED :
		if (cfg->tdls_mgmt_frame) {
			kfree(cfg->tdls_mgmt_frame);
			cfg->tdls_mgmt_frame = NULL;
			cfg->tdls_mgmt_freq = 0;
		}
		msg = "TDLS PEER DISCONNECTED ";
#ifdef SUPPORT_SET_CAC
		/* TDLS disconnec, set CAC */
		wl_cfg80211_set_cac(cfg, 1);
#endif /* SUPPORT_SET_CAC */
		break;
	}
	if (msg) {
		WL_ERR(("%s: " MACDBG " on %s ndev\n", msg, MAC2STRDBG((const u8*)(&e->addr)),
			(bcmcfg_to_prmry_ndev(cfg) == ndev) ? "primary" : "secondary"));
	}
	return 0;

}
#endif /* WLTDLS */

#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 2, 0))
static s32
#if (defined(CONFIG_ARCH_MSM) && defined(TDLS_MGMT_VERSION2)) || (LINUX_VERSION_CODE < \
	KERNEL_VERSION(3, 16, 0) && LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0))
wl_cfg80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev,
	u8 *peer, u8 action_code, u8 dialog_token, u16 status_code,
	u32 peer_capability, const u8 *buf, size_t len)
#elif ((LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)) && \
		(LINUX_VERSION_CODE < KERNEL_VERSION(3, 18, 0)))
wl_cfg80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev,
	const u8 *peer, u8 action_code, u8 dialog_token, u16 status_code,
	u32 peer_capability, const u8 *buf, size_t len)
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0))
wl_cfg80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev,
       const u8 *peer, u8 action_code, u8 dialog_token, u16 status_code,
       u32 peer_capability, bool initiator, const u8 *buf, size_t len)
#else /* CONFIG_ARCH_MSM && TDLS_MGMT_VERSION2 */
wl_cfg80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev,
	u8 *peer, u8 action_code, u8 dialog_token, u16 status_code,
	const u8 *buf, size_t len)
#endif /* CONFIG_ARCH_MSM && TDLS_MGMT_VERSION2 */
{
	s32 ret = 0;
#ifdef WLTDLS
	struct bcm_cfg80211 *cfg;
	tdls_wfd_ie_iovar_t info;
	memset(&info, 0, sizeof(tdls_wfd_ie_iovar_t));
	cfg = wl_get_cfg(dev);

#if defined(CONFIG_ARCH_MSM) && defined(TDLS_MGMT_VERSION2)
	/* Some customer platform back ported this feature from kernel 3.15 to kernel 3.10
	 * and that cuases build error
	 */
	BCM_REFERENCE(peer_capability);
#endif  /* CONFIG_ARCH_MSM && TDLS_MGMT_VERSION2 */

	switch (action_code) {
		/* We need to set TDLS Wifi Display IE to firmware
		 * using tdls_wfd_ie iovar
		 */
		case WLAN_TDLS_SET_PROBE_WFD_IE:
			WL_ERR(("%s WLAN_TDLS_SET_PROBE_WFD_IE\n", __FUNCTION__));
			info.mode = TDLS_WFD_PROBE_IE_TX;
			memcpy(&info.data, buf, len);
			info.length = len;
			break;
		case WLAN_TDLS_SET_SETUP_WFD_IE:
			WL_ERR(("%s WLAN_TDLS_SET_SETUP_WFD_IE\n", __FUNCTION__));
			info.mode = TDLS_WFD_IE_TX;
			memcpy(&info.data, buf, len);
			info.length = len;
			break;
		case WLAN_TDLS_SET_WFD_ENABLED:
			WL_ERR(("%s WLAN_TDLS_SET_MODE_WFD_ENABLED\n", __FUNCTION__));
			dhd_tdls_set_mode((dhd_pub_t *)(cfg->pub), true);
			goto out;
		case WLAN_TDLS_SET_WFD_DISABLED:
			WL_ERR(("%s WLAN_TDLS_SET_MODE_WFD_DISABLED\n", __FUNCTION__));
			dhd_tdls_set_mode((dhd_pub_t *)(cfg->pub), false);
			goto out;
		default:
			WL_ERR(("Unsupported action code : %d\n", action_code));
			goto out;
	}
	ret = wldev_iovar_setbuf(dev, "tdls_wfd_ie", &info, sizeof(info),
			cfg->ioctl_buf, WLC_IOCTL_MAXLEN, &cfg->ioctl_buf_sync);

	if (ret) {
		WL_ERR(("tdls_wfd_ie error %d\n", ret));
	}

out:
#endif /* WLTDLS */
	return ret;
}

static s32
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
wl_cfg80211_tdls_oper(struct wiphy *wiphy, struct net_device *dev,
	const u8 *peer, enum nl80211_tdls_operation oper)
#else
wl_cfg80211_tdls_oper(struct wiphy *wiphy, struct net_device *dev,
	u8 *peer, enum nl80211_tdls_operation oper)
#endif
{
	s32 ret = 0;
#ifdef WLTDLS
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	tdls_iovar_t info;
	dhd_pub_t *dhdp;
	bool tdls_auto_mode = false;
	dhdp = (dhd_pub_t *)(cfg->pub);
	memset(&info, 0, sizeof(tdls_iovar_t));
	if (peer) {
		memcpy(&info.ea, peer, ETHER_ADDR_LEN);
	} else {
		return -1;
	}
	switch (oper) {
	case NL80211_TDLS_DISCOVERY_REQ:
		/* If the discovery request is broadcast then we need to set
		 * info.mode to Tunneled Probe Request
		 */
		if (memcmp(peer, (const uint8 *)BSSID_BROADCAST, ETHER_ADDR_LEN) == 0) {
			info.mode = TDLS_MANUAL_EP_WFD_TPQ;
			WL_ERR(("%s TDLS TUNNELED PRBOBE REQUEST\n", __FUNCTION__));
		} else {
			info.mode = TDLS_MANUAL_EP_DISCOVERY;
		}
		break;
	case NL80211_TDLS_SETUP:
		if (dhdp->tdls_mode == true) {
			info.mode = TDLS_MANUAL_EP_CREATE;
			tdls_auto_mode = false;
			/* Do tear down and create a fresh one */
			ret = wl_cfg80211_tdls_config(cfg, TDLS_STATE_TEARDOWN, tdls_auto_mode);
			if (ret < 0) {
				return ret;
			}
		} else {
			tdls_auto_mode = true;
		}
		break;
	case NL80211_TDLS_TEARDOWN:
		info.mode = TDLS_MANUAL_EP_DELETE;
		break;
	default:
		WL_ERR(("Unsupported operation : %d\n", oper));
		goto out;
	}
	/* turn on TDLS */
	wl_cfg80211_tdls_config(cfg, TDLS_STATE_SETUP, tdls_auto_mode);
	if (ret < 0) {
		return ret;
	}
	if (info.mode) {
		ret = wldev_iovar_setbuf(dev, "tdls_endpoint", &info, sizeof(info),
			cfg->ioctl_buf, WLC_IOCTL_MAXLEN, &cfg->ioctl_buf_sync);
		if (ret) {
			WL_ERR(("tdls_endpoint error %d\n", ret));
		}
	}
out:
	if (ret) {
		return -ENOTSUPP;
	}
#endif /* WLTDLS */
	return ret;
}
#endif 

/*
 * This function returns no of bytes written
 * In case of failure return zero, not bcme_error
 */
s32 wl_cfg80211_set_wps_p2p_ie(struct net_device *ndev, char *buf, int len,
	enum wl_management_type type)
{
	struct bcm_cfg80211 *cfg;
	s32 ret = 0;
	struct ether_addr primary_mac;
	s32 bssidx = 0;
	s32 pktflag = 0;
	cfg = wl_get_cfg(ndev);

	if (wl_get_drv_status(cfg, AP_CREATING, ndev)) {
		/* Vendor IEs should be set to FW
		 * after SoftAP interface is brought up
		 */
		WL_DBG(("Skipping set IE since AP is not up \n"));
		goto exit;
	} else  if (ndev == bcmcfg_to_prmry_ndev(cfg)) {
		/* Either stand alone AP case or P2P discovery */
		if (wl_get_drv_status(cfg, AP_CREATED, ndev)) {
			/* Stand alone AP case on primary interface */
			WL_DBG(("Apply IEs for Primary AP Interface \n"));
			bssidx = 0;
		} else {
			if (!cfg->p2p) {
				/* If p2p not initialized, return failure */
				WL_ERR(("P2P not initialized \n"));
				goto exit;
			}
			/* P2P Discovery case (p2p listen) */
			if (!cfg->p2p->on) {
				/* Turn on Discovery interface */
				get_primary_mac(cfg, &primary_mac);
				wl_cfgp2p_generate_bss_mac(cfg, &primary_mac);
				p2p_on(cfg) = true;
				ret = wl_cfgp2p_enable_discovery(cfg, ndev, NULL, 0);
				if (unlikely(ret)) {
					WL_ERR(("Enable discovery failed \n"));
					goto exit;
				}
			}
			WL_DBG(("Apply IEs for P2P Discovery Iface \n"));
			ndev = wl_to_p2p_bss_ndev(cfg, P2PAPI_BSSCFG_PRIMARY);
			bssidx = wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE);
		}
	} else {
		/* Virtual AP/ P2P Group Interface */
		WL_DBG(("Apply IEs for iface:%s\n", ndev->name));
		bssidx = wl_get_bssidx_by_wdev(cfg, ndev->ieee80211_ptr);
	}

	if (ndev != NULL) {
		switch (type) {
			case WL_BEACON:
				pktflag = VNDR_IE_BEACON_FLAG;
				break;
			case WL_PROBE_RESP:
				pktflag = VNDR_IE_PRBRSP_FLAG;
				break;
			case WL_ASSOC_RESP:
				pktflag = VNDR_IE_ASSOCRSP_FLAG;
				break;
		}
		if (pktflag) {
			ret = wl_cfg80211_set_mgmt_vndr_ies(cfg,
				ndev_to_cfgdev(ndev), bssidx, pktflag, buf, len);
		}
	}
exit:
	return ret;
}

#ifdef WL_SUPPORT_AUTO_CHANNEL
static s32
wl_cfg80211_set_auto_channel_scan_state(struct net_device *ndev)
{
	u32 val = 0;
	s32 ret = BCME_ERROR;
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	/* Set interface up, explicitly. */
	val = 1;

	ret = wldev_ioctl_set(ndev, WLC_UP, (void *)&val, sizeof(val));
	if (ret < 0) {
		WL_ERR(("set interface up failed, error = %d\n", ret));
		goto done;
	}

	/* Stop all scan explicitly, till auto channel selection complete. */
	wl_set_drv_status(cfg, SCANNING, ndev);
	if (cfg->escan_info.ndev == NULL) {
		ret = BCME_OK;
		goto done;
	}
	ret = wl_notify_escan_complete(cfg, ndev, true, true);
	if (ret < 0) {
		WL_ERR(("set scan abort failed, error = %d\n", ret));
		ret = BCME_OK; // terence 20140115: fix escan_complete error
		goto done;
	}

done:
	return ret;
}

static bool
wl_cfg80211_valid_channel_p2p(int channel)
{
	bool valid = false;

	/* channel 1 to 14 */
	if ((channel >= 1) && (channel <= 14)) {
		valid = true;
	}
	/* channel 36 to 48 */
	else if ((channel >= 36) && (channel <= 48)) {
		valid = true;
	}
	/* channel 149 to 161 */
	else if ((channel >= 149) && (channel <= 161)) {
		valid = true;
	}
	else {
		valid = false;
		WL_INFORM(("invalid P2P chanspec, channel = %d\n", channel));
	}

	return valid;
}

s32
wl_cfg80211_get_chanspecs_2g(struct net_device *ndev, void *buf, s32 buflen)
{
	s32 ret = BCME_ERROR;
	struct bcm_cfg80211 *cfg = NULL;
	chanspec_t chanspec = 0;

	cfg = wl_get_cfg(ndev);

	/* Restrict channels to 2.4GHz, 20MHz BW, no SB. */
	chanspec |= (WL_CHANSPEC_BAND_2G | WL_CHANSPEC_BW_20 |
		WL_CHANSPEC_CTL_SB_NONE);
	chanspec = wl_chspec_host_to_driver(chanspec);

	ret = wldev_iovar_getbuf_bsscfg(ndev, "chanspecs", (void *)&chanspec,
		sizeof(chanspec), buf, buflen, 0, &cfg->ioctl_buf_sync);
	if (ret < 0) {
		WL_ERR(("get 'chanspecs' failed, error = %d\n", ret));
	}

	return ret;
}

s32
wl_cfg80211_get_chanspecs_5g(struct net_device *ndev, void *buf, s32 buflen)
{
	u32 channel = 0;
	s32 ret = BCME_ERROR;
	s32 i = 0;
	s32 j = 0;
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	wl_uint32_list_t *list = NULL;
	chanspec_t chanspec = 0;

	/* Restrict channels to 5GHz, 20MHz BW, no SB. */
	chanspec |= (WL_CHANSPEC_BAND_5G | WL_CHANSPEC_BW_20 |
		WL_CHANSPEC_CTL_SB_NONE);
	chanspec = wl_chspec_host_to_driver(chanspec);

	ret = wldev_iovar_getbuf_bsscfg(ndev, "chanspecs", (void *)&chanspec,
		sizeof(chanspec), buf, buflen, 0, &cfg->ioctl_buf_sync);
	if (ret < 0) {
		WL_ERR(("get 'chanspecs' failed, error = %d\n", ret));
		goto done;
	}

	list = (wl_uint32_list_t *)buf;
	/* Skip DFS and inavlid P2P channel. */
	for (i = 0, j = 0; i < dtoh32(list->count); i++) {
		chanspec = (chanspec_t) dtoh32(list->element[i]);
		channel = CHSPEC_CHANNEL(chanspec);

		ret = wldev_iovar_getint(ndev, "per_chan_info", &channel);
		if (ret < 0) {
			WL_ERR(("get 'per_chan_info' failed, error = %d\n", ret));
			goto done;
		}

		if (CHANNEL_IS_RADAR(channel) ||
			!(wl_cfg80211_valid_channel_p2p(CHSPEC_CHANNEL(chanspec)))) {
			continue;
		} else {
			list->element[j] = list->element[i];
		}

		j++;
	}

	list->count = j;

done:
	return ret;
}

static s32
wl_cfg80211_get_best_channel(struct net_device *ndev, void *buf, int buflen,
	int *channel)
{
	s32 ret = BCME_ERROR;
	int chosen = 0;
	int retry = 0;
	uint chip;

	/* Start auto channel selection scan. */
	ret = wldev_ioctl_set(ndev, WLC_START_CHANNEL_SEL, buf, buflen);
	if (ret < 0) {
		WL_ERR(("can't start auto channel scan, error = %d\n", ret));
		*channel = 0;
		goto done;
	}

	/* Wait for auto channel selection, worst case possible delay is 5250ms. */
	retry = CHAN_SEL_RETRY_COUNT;

	while (retry--) {
		OSL_SLEEP(CHAN_SEL_IOCTL_DELAY);
		chosen = 0;
		ret = wldev_ioctl_get(ndev, WLC_GET_CHANNEL_SEL, &chosen, sizeof(chosen));
		if ((ret == 0) && (dtoh32(chosen) != 0)) {
			chip = dhd_conf_get_chip(dhd_get_pub(ndev));
			if (chip != BCM43362_CHIP_ID &&	chip != BCM4330_CHIP_ID &&
					chip != BCM43143_CHIP_ID) {
				u32 chanspec = 0;
				int ctl_chan;
				chanspec = wl_chspec_driver_to_host(chosen);
				WL_INFORM(("selected chanspec = 0x%x\n", chanspec));
				ctl_chan = wf_chspec_ctlchan(chanspec);
				WL_INFORM(("selected ctl_chan = %d\n", ctl_chan));
				*channel = (u16)(ctl_chan & 0x00FF);
			} else
				*channel = (u16)(chosen & 0x00FF);
			WL_INFORM(("selected channel = %d\n", *channel));
			break;
		}
		WL_INFORM(("attempt = %d, ret = %d, chosen = %d\n",
			(CHAN_SEL_RETRY_COUNT - retry), ret, dtoh32(chosen)));
	}

	if (retry <= 0)	{
		WL_ERR(("failure, auto channel selection timed out\n"));
		*channel = 0;
		ret = BCME_ERROR;
	}
	WL_INFORM(("selected channel = %d\n", *channel));

done:
	return ret;
}

static s32
wl_cfg80211_restore_auto_channel_scan_state(struct net_device *ndev)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	/* Clear scan stop driver status. */
	wl_clr_drv_status(cfg, SCANNING, ndev);

	return BCME_OK;
}

s32
wl_cfg80211_get_best_channels(struct net_device *dev, char* cmd, int total_len)
{
	int channel = 0, band, band_cur;
	s32 ret = BCME_ERROR;
	u8 *buf = NULL;
	char *pos = cmd;
	struct bcm_cfg80211 *cfg = NULL;
	struct net_device *ndev = NULL;

	memset(cmd, 0, total_len);

	buf = kmalloc(CHANSPEC_BUF_SIZE, GFP_KERNEL);
	if (buf == NULL) {
		WL_ERR(("failed to allocate chanspec buffer\n"));
		return -ENOMEM;
	}

	/*
	 * Always use primary interface, irrespective of interface on which
	 * command came.
	 */
	cfg = wl_get_cfg(dev);
	ndev = bcmcfg_to_prmry_ndev(cfg);

	/*
	 * Make sure that FW and driver are in right state to do auto channel
	 * selection scan.
	 */
	ret = wl_cfg80211_set_auto_channel_scan_state(ndev);
	if (ret < 0) {
		WL_ERR(("can't set auto channel scan state, error = %d\n", ret));
		goto done;
	}

	ret = wldev_ioctl(dev, WLC_GET_BAND, &band_cur, sizeof(band_cur), false);
	if (band_cur != WLC_BAND_5G) {
		/* Best channel selection in 2.4GHz band. */
		ret = wl_cfg80211_get_chanspecs_2g(ndev, (void *)buf, CHANSPEC_BUF_SIZE);
		if (ret < 0) {
			WL_ERR(("can't get chanspecs in 2.4GHz, error = %d\n", ret));
			goto done;
		}

		ret = wl_cfg80211_get_best_channel(ndev, (void *)buf, CHANSPEC_BUF_SIZE,
			&channel);
		if (ret < 0) {
			WL_ERR(("can't select best channel scan in 2.4GHz, error = %d\n", ret));
			goto done;
		}

		if (CHANNEL_IS_2G(channel)) {
#if 0
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 39) && !defined(WL_COMPAT_WIRELESS)
			channel = ieee80211_channel_to_frequency(channel);
#else
			channel = ieee80211_channel_to_frequency(channel, IEEE80211_BAND_2GHZ);
#endif
#endif
		} else {
			WL_ERR(("invalid 2.4GHz channel, channel = %d\n", channel));
			channel = 0;
		}
		pos += snprintf(pos, total_len, "2g=%d ", channel);
	}

	if (band_cur != WLC_BAND_2G) {
		// terence 20140120: fix for some chipsets only return 2.4GHz channel (4330b2/43341b0/4339a0)
		band = band_cur==WLC_BAND_2G ? band_cur : WLC_BAND_5G;
		ret = wldev_ioctl(dev, WLC_SET_BAND, &band, sizeof(band), true);
		if (ret < 0) {
			WL_ERR(("WLC_SET_BAND error %d\n", ret));
			goto done;
		}

		/* Best channel selection in 5GHz band. */
		ret = wl_cfg80211_get_chanspecs_5g(ndev, (void *)buf, CHANSPEC_BUF_SIZE);
		if (ret < 0) {
			WL_ERR(("can't get chanspecs in 5GHz, error = %d\n", ret));
			goto done;
		}

		ret = wl_cfg80211_get_best_channel(ndev, (void *)buf, CHANSPEC_BUF_SIZE,
			&channel);
		if (ret < 0) {
			WL_ERR(("can't select best channel scan in 5GHz, error = %d\n", ret));
			goto done;
		}

		if (CHANNEL_IS_5G(channel)) {
#if 0
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 39) && !defined(WL_COMPAT_WIRELESS)
			channel = ieee80211_channel_to_frequency(channel);
#else
			channel = ieee80211_channel_to_frequency(channel, IEEE80211_BAND_5GHZ);
#endif
#endif
		} else {
			WL_ERR(("invalid 5GHz channel, channel = %d\n", channel));
			channel = 0;
		}

		ret = wldev_ioctl(dev, WLC_SET_BAND, &band_cur, sizeof(band_cur), true);
		if (ret < 0)
			WL_ERR(("WLC_SET_BAND error %d\n", ret));
		pos += snprintf(pos, total_len, "5g=%d ", channel);
	}

done:
	if (NULL != buf) {
		kfree(buf);
	}

	/* Restore FW and driver back to normal state. */
	ret = wl_cfg80211_restore_auto_channel_scan_state(ndev);
	if (ret < 0) {
		WL_ERR(("can't restore auto channel scan state, error = %d\n", ret));
	}

	WL_MSG(ndev->name, "%s\n", cmd);

	return (pos - cmd);
}
#endif /* WL_SUPPORT_AUTO_CHANNEL */

static const struct rfkill_ops wl_rfkill_ops = {
	.set_block = wl_rfkill_set
};

static int wl_rfkill_set(void *data, bool blocked)
{
	struct bcm_cfg80211 *cfg = (struct bcm_cfg80211 *)data;

	WL_DBG(("Enter \n"));
	WL_DBG(("RF %s\n", blocked ? "blocked" : "unblocked"));

	if (!cfg)
		return -EINVAL;

	cfg->rf_blocked = blocked;

	return 0;
}

static int wl_setup_rfkill(struct bcm_cfg80211 *cfg, bool setup)
{
	s32 err = 0;

	WL_DBG(("Enter \n"));
	if (!cfg)
		return -EINVAL;
	if (setup) {
		cfg->rfkill = rfkill_alloc("brcmfmac-wifi",
			wl_cfg80211_get_parent_dev(),
			RFKILL_TYPE_WLAN, &wl_rfkill_ops, (void *)cfg);

		if (!cfg->rfkill) {
			err = -ENOMEM;
			goto err_out;
		}

		err = rfkill_register(cfg->rfkill);

		if (err)
			rfkill_destroy(cfg->rfkill);
	} else {
		if (!cfg->rfkill) {
			err = -ENOMEM;
			goto err_out;
		}

		rfkill_unregister(cfg->rfkill);
		rfkill_destroy(cfg->rfkill);
	}

err_out:
	return err;
}

#ifdef DEBUGFS_CFG80211
/**
* Format : echo "SCAN:1 DBG:1" > /sys/kernel/debug/dhd/debug_level
* to turn on SCAN and DBG log.
* To turn off SCAN partially, echo "SCAN:0" > /sys/kernel/debug/dhd/debug_level
* To see current setting of debug level,
* cat /sys/kernel/debug/dhd/debug_level
*/
static ssize_t
wl_debuglevel_write(struct file *file, const char __user *userbuf,
	size_t count, loff_t *ppos)
{
	char tbuf[S_SUBLOGLEVEL * ARRAYSIZE(sublogname_map)], sublog[S_SUBLOGLEVEL];
	char *params, *token, *colon;
	uint i, tokens, log_on = 0;
	size_t minsize = min_t(size_t, (sizeof(tbuf) - 1), count);

	memset(tbuf, 0, sizeof(tbuf));
	memset(sublog, 0, sizeof(sublog));
	if (copy_from_user(&tbuf, userbuf, minsize)) {
		return -EFAULT;
	}

	tbuf[minsize + 1] = '\0';
	params = &tbuf[0];
	colon = strchr(params, '\n');
	if (colon != NULL)
		*colon = '\0';
	while ((token = strsep(&params, " ")) != NULL) {
		memset(sublog, 0, sizeof(sublog));
		if (token == NULL || !*token)
			break;
		if (*token == '\0')
			continue;
		colon = strchr(token, ':');
		if (colon != NULL) {
			*colon = ' ';
		}
		tokens = sscanf(token, "%s %u", sublog, &log_on);
		if (colon != NULL)
			*colon = ':';

		if (tokens == 2) {
				for (i = 0; i < ARRAYSIZE(sublogname_map); i++) {
					if (!strncmp(sublog, sublogname_map[i].sublogname,
						strlen(sublogname_map[i].sublogname))) {
						if (log_on)
							wl_dbg_level |=
							(sublogname_map[i].log_level);
						else
							wl_dbg_level &=
							~(sublogname_map[i].log_level);
					}
				}
		} else
			WL_ERR(("%s: can't parse '%s' as a "
			       "SUBMODULE:LEVEL (%d tokens)\n",
			       tbuf, token, tokens));


	}
	return count;
}

static ssize_t
wl_debuglevel_read(struct file *file, char __user *user_buf,
	size_t count, loff_t *ppos)
{
	char *param;
	char tbuf[S_SUBLOGLEVEL * ARRAYSIZE(sublogname_map)];
	uint i;
	memset(tbuf, 0, sizeof(tbuf));
	param = &tbuf[0];
	for (i = 0; i < ARRAYSIZE(sublogname_map); i++) {
		param += snprintf(param, sizeof(tbuf) - 1, "%s:%d ",
			sublogname_map[i].sublogname,
			(wl_dbg_level & sublogname_map[i].log_level) ? 1 : 0);
	}
	*param = '\n';
	return simple_read_from_buffer(user_buf, count, ppos, tbuf, strlen(&tbuf[0]));

}
static const struct file_operations fops_debuglevel = {
	.open = NULL,
	.write = wl_debuglevel_write,
	.read = wl_debuglevel_read,
	.owner = THIS_MODULE,
	.llseek = NULL,
};

static s32 wl_setup_debugfs(struct bcm_cfg80211 *cfg)
{
	s32 err = 0;
	struct dentry *_dentry;
	if (!cfg)
		return -EINVAL;
	cfg->debugfs = debugfs_create_dir(KBUILD_MODNAME, NULL);
	if (!cfg->debugfs || IS_ERR(cfg->debugfs)) {
		if (cfg->debugfs == ERR_PTR(-ENODEV))
			WL_ERR(("Debugfs is not enabled on this kernel\n"));
		else
			WL_ERR(("Can not create debugfs directory\n"));
		cfg->debugfs = NULL;
		goto exit;

	}
	_dentry = debugfs_create_file("debug_level", S_IRUSR | S_IWUSR,
		cfg->debugfs, cfg, &fops_debuglevel);
	if (!_dentry || IS_ERR(_dentry)) {
		WL_ERR(("failed to create debug_level debug file\n"));
		wl_free_debugfs(cfg);
	}
exit:
	return err;
}
static s32 wl_free_debugfs(struct bcm_cfg80211 *cfg)
{
	if (!cfg)
		return -EINVAL;
	if (cfg->debugfs)
		debugfs_remove_recursive(cfg->debugfs);
	cfg->debugfs = NULL;
	return 0;
}
#endif /* DEBUGFS_CFG80211 */

struct device *wl_cfg80211_get_parent_dev(void)
{
	return cfg80211_parent_dev;
}

void wl_cfg80211_set_parent_dev(void *dev)
{
	cfg80211_parent_dev = dev;
}

static void wl_cfg80211_clear_parent_dev(void)
{
	cfg80211_parent_dev = NULL;
}

void get_primary_mac(struct bcm_cfg80211 *cfg, struct ether_addr *mac)
{
	if (wldev_iovar_getbuf_bsscfg(bcmcfg_to_prmry_ndev(cfg), "cur_etheraddr", NULL,
		0, cfg->ioctl_buf, WLC_IOCTL_SMLEN, 0, &cfg->ioctl_buf_sync) == BCME_OK) {
		memcpy(mac->octet, cfg->ioctl_buf, ETHER_ADDR_LEN);
	} else {
		memset(mac->octet, 0, ETHER_ADDR_LEN);
	}
}
static bool check_dev_role_integrity(struct bcm_cfg80211 *cfg, u32 dev_role)
{
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);
	if (((dev_role == NL80211_IFTYPE_AP) &&
		!(dhd->op_mode & DHD_FLAG_HOSTAP_MODE)) ||
		((dev_role == NL80211_IFTYPE_P2P_GO) &&
		!(dhd->op_mode & DHD_FLAG_P2P_GO_MODE)))
	{
		WL_ERR(("device role select failed role:%d op_mode:%d \n", dev_role, dhd->op_mode));
		return false;
	}
	return true;
}

int wl_cfg80211_do_driver_init(struct net_device *net)
{
	struct bcm_cfg80211 *cfg = *(struct bcm_cfg80211 **)netdev_priv(net);

	if (!cfg || !cfg->wdev)
		return -EINVAL;

	if (dhd_do_driver_init(cfg->wdev->netdev) < 0)
		return -1;

	return 0;
}

void wl_cfg80211_enable_trace(u32 level)
{
	wl_dbg_level = level;
	WL_MSG("wlan", "wl_dbg_level = 0x%x\n", wl_dbg_level);
}

#if defined(WL_SUPPORT_BACKPORTED_KPATCHES) || (LINUX_VERSION_CODE >= KERNEL_VERSION(3, \
	2, 0))
static s32
wl_cfg80211_mgmt_tx_cancel_wait(struct wiphy *wiphy,
	bcm_struct_cfgdev *cfgdev, u64 cookie)
{
	/* CFG80211 checks for tx_cancel_wait callback when ATTR_DURATION
	 * is passed with CMD_FRAME. This callback is supposed to cancel
	 * the OFFCHANNEL Wait. Since we are already taking care of that
	 *  with the tx_mgmt logic, do nothing here.
	 */

	return 0;
}
#endif /* WL_SUPPORT_BACKPORTED_PATCHES || KERNEL >= 3.2.0 */

#ifdef WL11U
static bcm_tlv_t *
wl_cfg80211_find_interworking_ie(const u8 *parse, u32 len)
{
	bcm_tlv_t *ie;

/* unfortunately it's too much work to dispose the const cast - bcm_parse_tlvs
 * is used everywhere and changing its prototype to take const qualifier needs
 * a massive change to all its callers...
 */
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"")
#endif
	if ((ie = bcm_parse_tlvs((void *)parse, (int)len, DOT11_MNG_INTERWORKING_ID))) {
		return ie;
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic pop")
#endif
	return NULL;
}

static s32
wl_cfg80211_clear_iw_ie(struct bcm_cfg80211 *cfg, struct net_device *ndev, s32 bssidx)
{
	ie_setbuf_t ie_setbuf;

	WL_DBG(("clear interworking IE\n"));

	memset(&ie_setbuf, 0, sizeof(ie_setbuf_t));

	ie_setbuf.ie_buffer.iecount = htod32(1);
	ie_setbuf.ie_buffer.ie_list[0].ie_data.id = DOT11_MNG_INTERWORKING_ID;
	ie_setbuf.ie_buffer.ie_list[0].ie_data.len = 0;

	return wldev_iovar_setbuf_bsscfg(ndev, "ie", &ie_setbuf, sizeof(ie_setbuf),
		cfg->ioctl_buf, WLC_IOCTL_MAXLEN, bssidx, &cfg->ioctl_buf_sync);
}

static s32
wl_cfg80211_add_iw_ie(struct bcm_cfg80211 *cfg, struct net_device *ndev, s32 bssidx, s32 pktflag,
            uint8 ie_id, uint8 *data, uint8 data_len)
{
	s32 err = BCME_OK;
	s32 buf_len;
	ie_setbuf_t *ie_setbuf;
	ie_getbuf_t ie_getbufp;
	char getbuf[WLC_IOCTL_SMLEN];

	if (ie_id != DOT11_MNG_INTERWORKING_ID) {
		WL_ERR(("unsupported (id=%d)\n", ie_id));
		return BCME_UNSUPPORTED;
	}

	/* access network options (1 octet)  is the mandatory field */
	if (!data || data_len == 0 || data_len > IW_IES_MAX_BUF_LEN) {
		WL_ERR(("wrong interworking IE (len=%d)\n", data_len));
		return BCME_BADARG;
	}

	/* Validate the pktflag parameter */
	if ((pktflag & ~(VNDR_IE_BEACON_FLAG | VNDR_IE_PRBRSP_FLAG |
			VNDR_IE_ASSOCRSP_FLAG | VNDR_IE_AUTHRSP_FLAG |
			VNDR_IE_PRBREQ_FLAG | VNDR_IE_ASSOCREQ_FLAG|
			VNDR_IE_CUSTOM_FLAG))) {
		WL_ERR(("invalid packet flag 0x%x\n", pktflag));
		return BCME_BADARG;
	}

	buf_len = sizeof(ie_setbuf_t) + data_len - 1;

	ie_getbufp.id = DOT11_MNG_INTERWORKING_ID;
	if (wldev_iovar_getbuf_bsscfg(ndev, "ie", (void *)&ie_getbufp,
			sizeof(ie_getbufp), getbuf, WLC_IOCTL_SMLEN, bssidx, &cfg->ioctl_buf_sync)
			== BCME_OK) {
		if (!memcmp(&getbuf[TLV_HDR_LEN], data, data_len)) {
			WL_DBG(("skip to set interworking IE\n"));
			return BCME_OK;
		}
	}

	/* if already set with previous values, delete it first */
	if (cfg->wl11u) {
		if ((err = wl_cfg80211_clear_iw_ie(cfg, ndev, bssidx)) != BCME_OK) {
			return err;
		}
	}

	ie_setbuf = (ie_setbuf_t *) kzalloc(buf_len, GFP_KERNEL);
	if (!ie_setbuf) {
		WL_ERR(("Error allocating buffer for IE\n"));
		return -ENOMEM;
	}
	strncpy(ie_setbuf->cmd, "add", sizeof(ie_setbuf->cmd));
	ie_setbuf->cmd[sizeof(ie_setbuf->cmd) - 1] = '\0';

	/* Buffer contains only 1 IE */
	ie_setbuf->ie_buffer.iecount = htod32(1);
	/* use VNDR_IE_CUSTOM_FLAG flags for none vendor IE . currently fixed value */
	ie_setbuf->ie_buffer.ie_list[0].pktflag = htod32(pktflag);

	/* Now, add the IE to the buffer */
	ie_setbuf->ie_buffer.ie_list[0].ie_data.id = DOT11_MNG_INTERWORKING_ID;
	ie_setbuf->ie_buffer.ie_list[0].ie_data.len = data_len;
	memcpy((uchar *)&ie_setbuf->ie_buffer.ie_list[0].ie_data.data[0], data, data_len);

	if ((err = wldev_iovar_setbuf_bsscfg(ndev, "ie", ie_setbuf, buf_len,
			cfg->ioctl_buf, WLC_IOCTL_MAXLEN, bssidx, &cfg->ioctl_buf_sync))
			== BCME_OK) {
		WL_DBG(("set interworking IE\n"));
		cfg->wl11u = TRUE;
		err = wldev_iovar_setint_bsscfg(ndev, "grat_arp", 1, bssidx);
	}

	kfree(ie_setbuf);
	return err;
}
#endif /* WL11U */


s32
wl_cfg80211_set_if_band(struct net_device *ndev, int band)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	int ret = 0, wait_cnt;
	char ioctl_buf[32];

	if ((band < WLC_BAND_AUTO) || (band > WLC_BAND_2G)) {
		WL_ERR(("Invalid band\n"));
		return -EINVAL;
	}
	if (wl_get_drv_status(cfg, CONNECTED, ndev) ||
			wl_get_drv_status(cfg, CONNECTING, ndev)) {
		/* if driver is connected or connecting status, try to disconnect first.
		* if dongle is associated, iovar 'if_band' would be rejected.
		*/
		wl_set_drv_status(cfg, DISCONNECTING, ndev);
		ret = wldev_ioctl_set(ndev, WLC_DISASSOC, NULL, 0);
		if (ret < 0) {
			WL_ERR(("WLC_DISASSOC error %d\n", ret));
			/* continue to set 'if_band' */
		}
		else {
			/* This is to ensure that 'if_band' iovar is issued only after
			* disconnection is completed
			*/
			wait_cnt = WAIT_FOR_DISCONNECT_MAX;
			while (wl_get_drv_status(cfg, DISCONNECTING, ndev) && wait_cnt) {
				WL_DBG(("Wait until disconnected. wait_cnt: %d\n", wait_cnt));
				wait_cnt--;
				OSL_SLEEP(10);
			}
		}
	}
	if ((ret = wldev_iovar_setbuf(ndev, "if_band", &band,
			sizeof(int), ioctl_buf, sizeof(ioctl_buf), NULL)) < 0) {
		WL_ERR(("seting if_band failed ret=%d\n", ret));
		/* issue 'WLC_SET_BAND' if if_band is not supported */
		if (ret == BCME_UNSUPPORTED) {
			ret = wldev_set_band(ndev, band);
			if (ret < 0) {
				WL_ERR(("seting band failed ret=%d\n", ret));
			}
		}
	}
	return ret;
}

s32
wl_cfg80211_dfs_ap_move(struct net_device *ndev, char *data, char *command, int total_len)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	char ioctl_buf[50];
	int err = 0;
	uint32 val = 0;
	chanspec_t chanspec = 0;
	int abort;
	int bytes_written = 0;
	wl_dfs_ap_move_status_t *status;
	char chanbuf[CHANSPEC_STR_LEN];
	const char *dfs_state_str[DFS_SCAN_S_MAX] = {
		"Radar Free On Channel",
		"Radar Found On Channel",
		"Radar Scan In Progress",
		"Radar Scan Aborted",
		"RSDB Mode switch in Progress For Scan"
	};
	if (ndev->ieee80211_ptr->iftype != NL80211_IFTYPE_AP) {
		bytes_written = snprintf(command, total_len, "AP is not up\n");
		return bytes_written;
	}
	if (!*data) {
		if ((err = wldev_iovar_getbuf(ndev, "dfs_ap_move", NULL, 0,
				cfg->ioctl_buf, WLC_IOCTL_MAXLEN,  &cfg->ioctl_buf_sync))) {
			WL_ERR(("setting dfs_ap_move failed with err=%d \n", err));
			return err;
		}
		status = (wl_dfs_ap_move_status_t *)cfg->ioctl_buf;

		if (status->version != WL_DFS_AP_MOVE_VERSION) {
			err = BCME_UNSUPPORTED;
			WL_ERR(("err=%d version=%d\n", err, status->version));
			return err;
		}

		if (status->move_status != (int8) DFS_SCAN_S_IDLE) {
			chanspec = wl_chspec_driver_to_host(status->chanspec);
			if (chanspec != 0 && chanspec != INVCHANSPEC) {
				wf_chspec_ntoa(chanspec, chanbuf);
				bytes_written = snprintf(command, total_len,
					"AP Target Chanspec %s (0x%x)\n", chanbuf, chanspec);

			}
			bytes_written += snprintf(command + bytes_written, total_len,
					 "%s\n", dfs_state_str[status->move_status]);
			return bytes_written;
		} else {
			bytes_written = snprintf(command, total_len, "dfs AP move in IDLE state\n");
			return bytes_written;
		}

	}

	abort = bcm_atoi(data);
	if (abort == -1) {
		if ((err = wldev_iovar_setbuf(ndev, "dfs_ap_move", &abort,
				sizeof(int), ioctl_buf, sizeof(ioctl_buf), NULL)) < 0) {
			WL_ERR(("seting dfs_ap_move failed with err %d\n", err));
			return err;
		}
	} else {
		chanspec = wf_chspec_aton(data);
		if (chanspec != 0) {
			val = wl_chspec_host_to_driver(chanspec);
			if (val != INVCHANSPEC) {
				if ((err = wldev_iovar_setbuf(ndev, "dfs_ap_move", &val,
					sizeof(int), ioctl_buf, sizeof(ioctl_buf), NULL)) < 0) {
					WL_ERR(("seting dfs_ap_move failed with err %d\n", err));
					return err;
				}
				WL_DBG((" set dfs_ap_move successfull"));
			} else {
				err = BCME_USAGE_ERROR;
			}
		}
	}
	return err;
}

#ifdef WBTEXT
s32
wl_cfg80211_wbtext_set_default(struct net_device *ndev)
{
	char commandp[WLC_IOCTL_SMLEN];
	s32 ret = BCME_OK;
	char *data;

	WL_DBG(("set wbtext to default\n"));

	/* set roam profile */
	memset(commandp, 0, sizeof(commandp));
	snprintf(commandp, WLC_IOCTL_SMLEN, "%s %s",
		CMD_WBTEXT_PROFILE_CONFIG, DEFAULT_WBTEXT_PROFILE_A);
	data = (commandp + strlen(CMD_WBTEXT_PROFILE_CONFIG) + 1);
	ret = wl_cfg80211_wbtext_config(ndev, data, commandp, WLC_IOCTL_SMLEN);
	if (ret != BCME_OK) {
		WL_ERR(("%s: Failed to set roam_prof %s error = %d\n",
			__FUNCTION__, data, ret));
		return ret;
	}

	memset(commandp, 0, sizeof(commandp));
	snprintf(commandp, WLC_IOCTL_SMLEN, "%s %s",
		CMD_WBTEXT_PROFILE_CONFIG, DEFAULT_WBTEXT_PROFILE_B);
	data = (commandp + strlen(CMD_WBTEXT_PROFILE_CONFIG) + 1);
	ret = wl_cfg80211_wbtext_config(ndev, data, commandp, WLC_IOCTL_SMLEN);
	if (ret != BCME_OK) {
		WL_ERR(("%s: Failed to set roam_prof %s error = %d\n",
			__FUNCTION__, data, ret));
		return ret;
	}

	/* set RSSI weight */
	memset(commandp, 0, sizeof(commandp));
	snprintf(commandp, WLC_IOCTL_SMLEN, "%s %s",
		CMD_WBTEXT_WEIGHT_CONFIG, DEFAULT_WBTEXT_WEIGHT_RSSI_A);
	data = (commandp + strlen(CMD_WBTEXT_WEIGHT_CONFIG) + 1);
	ret = wl_cfg80211_wbtext_weight_config(ndev, data, commandp, WLC_IOCTL_SMLEN);
	if (ret != BCME_OK) {
		WL_ERR(("%s: Failed to set weight config %s error = %d\n",
			__FUNCTION__, data, ret));
		return ret;
	}

	memset(commandp, 0, sizeof(commandp));
	snprintf(commandp, WLC_IOCTL_SMLEN, "%s %s",
		CMD_WBTEXT_WEIGHT_CONFIG, DEFAULT_WBTEXT_WEIGHT_RSSI_B);
	data = (commandp + strlen(CMD_WBTEXT_WEIGHT_CONFIG) + 1);
	ret = wl_cfg80211_wbtext_weight_config(ndev, data, commandp, WLC_IOCTL_SMLEN);
	if (ret != BCME_OK) {
		WL_ERR(("%s: Failed to set weight config %s error = %d\n",
			__FUNCTION__, data, ret));
		return ret;
	}

	/* set CU weight */
	memset(commandp, 0, sizeof(commandp));
	snprintf(commandp, WLC_IOCTL_SMLEN, "%s %s",
		CMD_WBTEXT_WEIGHT_CONFIG, DEFAULT_WBTEXT_WEIGHT_CU_A);
	data = (commandp + strlen(CMD_WBTEXT_WEIGHT_CONFIG) + 1);
	ret = wl_cfg80211_wbtext_weight_config(ndev, data, commandp, WLC_IOCTL_SMLEN);
	if (ret != BCME_OK) {
		WL_ERR(("%s: Failed to set weight config %s error = %d\n",
			__FUNCTION__, data, ret));
		return ret;
	}

	memset(commandp, 0, sizeof(commandp));
	snprintf(commandp, WLC_IOCTL_SMLEN, "%s %s",
		CMD_WBTEXT_WEIGHT_CONFIG, DEFAULT_WBTEXT_WEIGHT_CU_B);
	data = (commandp + strlen(CMD_WBTEXT_WEIGHT_CONFIG) + 1);
	ret = wl_cfg80211_wbtext_weight_config(ndev, data, commandp, WLC_IOCTL_SMLEN);
	if (ret != BCME_OK) {
		WL_ERR(("%s: Failed to set weight config %s error = %d\n",
			__FUNCTION__, data, ret));
		return ret;
	}

	/* set RSSI table */
	memset(commandp, 0, sizeof(commandp));
	snprintf(commandp, WLC_IOCTL_SMLEN, "%s %s",
		CMD_WBTEXT_TABLE_CONFIG, DEFAULT_WBTEXT_TABLE_RSSI_A);
	data = (commandp + strlen(CMD_WBTEXT_TABLE_CONFIG) + 1);
	ret = wl_cfg80211_wbtext_table_config(ndev, data, commandp, WLC_IOCTL_SMLEN);
	if (ret != BCME_OK) {
		WL_ERR(("%s: Failed to set RSSI table %s error = %d\n",
			__FUNCTION__, data, ret));
		return ret;
	}

	memset(commandp, 0, sizeof(commandp));
	snprintf(commandp, WLC_IOCTL_SMLEN, "%s %s",
		CMD_WBTEXT_TABLE_CONFIG, DEFAULT_WBTEXT_TABLE_RSSI_B);
	data = (commandp + strlen(CMD_WBTEXT_TABLE_CONFIG) + 1);
	ret = wl_cfg80211_wbtext_table_config(ndev, data, commandp, WLC_IOCTL_SMLEN);
	if (ret != BCME_OK) {
		WL_ERR(("%s: Failed to set RSSI table %s error = %d\n",
			__FUNCTION__, data, ret));
		return ret;
	}

	/* set CU table */
	memset(commandp, 0, sizeof(commandp));
	snprintf(commandp, WLC_IOCTL_SMLEN, "%s %s",
		CMD_WBTEXT_TABLE_CONFIG, DEFAULT_WBTEXT_TABLE_CU_A);
	data = (commandp + strlen(CMD_WBTEXT_TABLE_CONFIG) + 1);
	ret = wl_cfg80211_wbtext_table_config(ndev, data, commandp, WLC_IOCTL_SMLEN);
	if (ret != BCME_OK) {
		WL_ERR(("%s: Failed to set CU table %s error = %d\n",
			__FUNCTION__, data, ret));
		return ret;
	}

	memset(commandp, 0, sizeof(commandp));
	snprintf(commandp, WLC_IOCTL_SMLEN, "%s %s",
		CMD_WBTEXT_TABLE_CONFIG, DEFAULT_WBTEXT_TABLE_CU_B);
	data = (commandp + strlen(CMD_WBTEXT_TABLE_CONFIG) + 1);
	ret = wl_cfg80211_wbtext_table_config(ndev, data, commandp, WLC_IOCTL_SMLEN);
	if (ret != BCME_OK) {
		WL_ERR(("%s: Failed to set CU table %s error = %d\n",
			__FUNCTION__, data, ret));
		return ret;
	}

	return ret;
}

s32
wl_cfg80211_wbtext_config(struct net_device *ndev, char *data, char *command, int total_len)
{
	uint i = 0;
	long int rssi_lower, roam_trigger;
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	wl_roam_prof_band_t *rp;
	int err = -EINVAL, bytes_written = 0;
	size_t len = strlen(data);
	int rp_len = 0;
	data[len] = '\0';
	rp = (wl_roam_prof_band_t *) kzalloc(sizeof(*rp)
			* WL_MAX_ROAM_PROF_BRACKETS, GFP_KERNEL);
	if (unlikely(!rp)) {
		WL_ERR(("%s: failed to allocate memory\n", __func__));
		err =  -ENOMEM;
		goto exit;
	}
	rp->ver = WL_MAX_ROAM_PROF_VER;
	if (*data && (!strncmp(data, "b", 1))) {
		rp->band = WLC_BAND_2G;
	} else if (*data && (!strncmp(data, "a", 1))) {
		rp->band = WLC_BAND_5G;
	} else {
		err = snprintf(command, total_len, "Missing band\n");
		goto exit;
	}
	data++;
	rp->len = 0;
	/* Getting roam profile  from fw */
	if ((err = wldev_iovar_getbuf(ndev, "roam_prof", rp, sizeof(*rp),
		cfg->ioctl_buf, WLC_IOCTL_MAXLEN,  &cfg->ioctl_buf_sync))) {
		WL_ERR(("Getting roam_profile failed with err=%d \n", err));
		goto exit;
	}
	memcpy(rp, cfg->ioctl_buf, sizeof(*rp) * WL_MAX_ROAM_PROF_BRACKETS);
	/* roam_prof version get */
	if (rp->ver != WL_MAX_ROAM_PROF_VER) {
		WL_ERR(("bad version (=%d) in return data\n", rp->ver));
		err = -EINVAL;
		goto exit;
	}
	if ((rp->len % sizeof(wl_roam_prof_t)) != 0) {
		WL_ERR(("bad length (=%d) in return data\n", rp->len));
		err = -EINVAL;
		goto exit;
	}

	if (!*data) {
		for (i = 0; i < WL_MAX_ROAM_PROF_BRACKETS; i++) {
			/* printing contents of roam profile data from fw and exits
			 * if code hits any of one of the below condtion. If remaining
			 * length of buffer is less than roam profile size or
			 * if there is no valid entry.
			 */
			if (((i * sizeof(wl_roam_prof_t)) > rp->len) ||
				(rp->roam_prof[i].fullscan_period == 0)) {
				break;
			}
			bytes_written += snprintf(command+bytes_written,
					total_len, "RSSI[%d,%d] CU(trigger:%d%%: duration:%ds)\n",
					rp->roam_prof[i].roam_trigger, rp->roam_prof[i].rssi_lower,
					rp->roam_prof[i].channel_usage,
					rp->roam_prof[i].cu_avg_calc_dur);
		}
		err = bytes_written;
		goto exit;
	} else {
		for (i = 0; i < WL_MAX_ROAM_PROF_BRACKETS; i++) {
			/* reading contents of roam profile data from fw and exits
			 * if code hits any of one of the below condtion, If remaining
			 * length of buffer is less than roam profile size or if there
			 * is no valid entry.
			 */
			if (((i * sizeof(wl_roam_prof_t)) > rp->len) ||
				(rp->roam_prof[i].fullscan_period == 0)) {
				break;
			}
		}
		/* Do not set roam_prof from upper layer if fw doesn't have 2 rows */
		if (i != 2) {
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
			rp->roam_prof[i].roam_trigger = roam_trigger;
			data++;
			rssi_lower = simple_strtol(data, &data, 10);
			if (rssi_lower >= 0) {
				WL_ERR(("rssi lower[%d] value must be negative\n", i));
				err = -EINVAL;
				goto exit;
			}
			rp->roam_prof[i].rssi_lower = rssi_lower;
			data++;
			rp->roam_prof[i].channel_usage = simple_strtol(data, &data, 10);
			data++;
			rp->roam_prof[i].cu_avg_calc_dur = simple_strtol(data, &data, 10);

			rp_len += sizeof(wl_roam_prof_t);

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
		rp->len = rp_len;
		if ((err = wldev_iovar_setbuf(ndev, "roam_prof", rp,
				sizeof(*rp), cfg->ioctl_buf, WLC_IOCTL_MEDLEN,
				&cfg->ioctl_buf_sync)) < 0) {
			WL_ERR(("seting roam_profile failed with err %d\n", err));
		}
	}
exit:
	if (rp) {
		kfree(rp);
	}
	return err;
}

#define BUFSZ 5

#define _S(x) #x
#define S(x) _S(x)

int wl_cfg80211_wbtext_weight_config(struct net_device *ndev, char *data,
		char *command, int total_len)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	int bytes_written = 0, err = -EINVAL, argc = 0;
	char rssi[BUFSZ], band[BUFSZ], weight[BUFSZ];
	char *endptr = NULL;
	wnm_bss_select_weight_cfg_t *bwcfg;

	bwcfg = kzalloc(sizeof(*bwcfg), GFP_KERNEL);
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
	else {
		/* Usage DRIVER WBTEXT_WEIGHT_CONFIG <rssi/cu> <band> <weight> */
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
				cfg->ioctl_buf, WLC_IOCTL_MAXLEN,  &cfg->ioctl_buf_sync))) {
			WL_ERR(("Getting wnm_bss_select_weight failed with err=%d \n", err));
			goto exit;
		}
		memcpy(bwcfg, cfg->ioctl_buf, sizeof(*bwcfg));
		bytes_written = snprintf(command, total_len, "%s %s weight = %d\n",
			(bwcfg->type == WNM_BSS_SELECT_TYPE_RSSI) ? "RSSI" : "CU",
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
				cfg->ioctl_buf, WLC_IOCTL_MAXLEN,  &cfg->ioctl_buf_sync))) {
			WL_ERR(("Getting wnm_bss_select_weight failed with err=%d\n", err));
		}
	}
exit:
	if (bwcfg) {
		kfree(bwcfg);
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
	char rssi[BUFSZ], band[BUFSZ];
	int btcfg_len = 0, i = 0, parsed_len = 0;
	wnm_bss_select_factor_cfg_t *btcfg;
	size_t slen = strlen(data);
	char *start_addr = NULL;
	data[slen] = '\0';

	btcfg = kzalloc((sizeof(*btcfg) + sizeof(*btcfg) *
			WL_FACTOR_TABLE_MAX_LIMIT), GFP_KERNEL);
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
				cfg->ioctl_buf, WLC_IOCTL_MAXLEN,  &cfg->ioctl_buf_sync))) {
			WL_ERR(("Getting wnm_bss_select_table failed with err=%d \n", err));
			goto exit;
		}
		memcpy(btcfg, cfg->ioctl_buf, sizeof(*btcfg));
		memcpy(btcfg, cfg->ioctl_buf, (btcfg->count+1) * sizeof(*btcfg));

		bytes_written += snprintf(command + bytes_written, total_len,
					"No of entries in table: %d\n", btcfg->count);
		bytes_written += snprintf(command + bytes_written, total_len, "%s factor table\n",
				(btcfg->type == WNM_BSS_SELECT_TYPE_RSSI) ? "RSSI" : "CU");
		bytes_written += snprintf(command + bytes_written, total_len,
					"low\thigh\tfactor\n");
		for (i = 0; i <= btcfg->count-1; i++) {
			bytes_written += snprintf(command + bytes_written, total_len,
				"%d\t%d\t%d\n", btcfg->params[i].low, btcfg->params[i].high,
				btcfg->params[i].factor);
		}
		err = bytes_written;
		goto exit;
	} else {
		memset(btcfg->params, 0, sizeof(wnm_bss_select_factor_params_t)
				* WL_FACTOR_TABLE_MAX_LIMIT);
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
		kfree(btcfg);
	}
	return err;
}

s32
wl_cfg80211_wbtext_delta_config(struct net_device *ndev, char *data, char *command, int total_len)
{
	uint i = 0;
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	int err = -EINVAL, bytes_written = 0, argc = 0, val, len = 0;
	char delta[BUFSZ], band[BUFSZ], *endptr = NULL;
	wl_roam_prof_band_t *rp;

	rp = (wl_roam_prof_band_t *) kzalloc(sizeof(*rp)
			* WL_MAX_ROAM_PROF_BRACKETS, GFP_KERNEL);
	if (unlikely(!rp)) {
		WL_ERR(("%s: failed to allocate memory\n", __func__));
		err = -ENOMEM;
		goto exit;
	}

	argc = sscanf(data, "%"S(BUFSZ)"s %"S(BUFSZ)"s", band, delta);
	if (!strcasecmp(band, "a"))
		rp->band = WLC_BAND_5G;
	else if (!strcasecmp(band, "b"))
		rp->band = WLC_BAND_2G;
	else {
		WL_ERR(("%s: Missing band\n", __func__));
		goto exit;
	}
	/* Getting roam profile  from fw */
	if ((err = wldev_iovar_getbuf(ndev, "roam_prof", rp, sizeof(*rp),
		cfg->ioctl_buf, WLC_IOCTL_MAXLEN,  &cfg->ioctl_buf_sync))) {
		WL_ERR(("Getting roam_profile failed with err=%d \n", err));
		goto exit;
	}
	memcpy(rp, cfg->ioctl_buf, sizeof(wl_roam_prof_band_t));
	if (rp->ver != WL_MAX_ROAM_PROF_VER) {
		WL_ERR(("bad version (=%d) in return data\n", rp->ver));
		err = -EINVAL;
		goto exit;
	}
	if ((rp->len % sizeof(wl_roam_prof_t)) != 0) {
		WL_ERR(("bad length (=%d) in return data\n", rp->len));
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
			if (((i * sizeof(wl_roam_prof_t)) > rp->len) ||
				(rp->roam_prof[i].fullscan_period == 0)) {
				break;
			}
			if (rp->roam_prof[i].channel_usage != 0) {
				rp->roam_prof[i].roam_delta = val;
			}
			len += sizeof(wl_roam_prof_t);
		}
	}
	else {
		if (rp->roam_prof[i].channel_usage != 0) {
			bytes_written = snprintf(command, total_len,
				"%s Delta %d\n", (rp->band == WLC_BAND_2G) ? "2G" : "5G",
				rp->roam_prof[0].roam_delta);
		}
		err = bytes_written;
		goto exit;
	}
	rp->len = len;
	if ((err = wldev_iovar_setbuf(ndev, "roam_prof", rp,
			sizeof(*rp), cfg->ioctl_buf, WLC_IOCTL_MEDLEN, &cfg->ioctl_buf_sync)) < 0) {
		WL_ERR(("seting roam_profile failed with err %d\n", err));
	}
exit :
	if (rp) {
		kfree(rp);
	}
	return err;
}
#endif /* WBTEXT */


int wl_cfg80211_scan_stop(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev)
{
	struct net_device *ndev = NULL;
	unsigned long flags;
	int clear_flag = 0;
	int ret = 0;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0))
	struct cfg80211_scan_info info;
#endif

	WL_TRACE(("Enter\n"));

	if (!cfg)
		return -EINVAL;

	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);

	spin_lock_irqsave(&cfg->cfgdrv_lock, flags);
#ifdef WL_CFG80211_P2P_DEV_IF
	if (cfg->scan_request && cfg->scan_request->wdev == cfgdev)
#else
	if (cfg->scan_request && cfg->scan_request->dev == cfgdev)
#endif
	{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0))
		info.aborted = true;
		cfg80211_scan_done(cfg->scan_request, &info);
#else
		cfg80211_scan_done(cfg->scan_request, true);
#endif
		cfg->scan_request = NULL;
		clear_flag = 1;
	}
	spin_unlock_irqrestore(&cfg->cfgdrv_lock, flags);

	if (clear_flag)
		wl_clr_drv_status(cfg, SCANNING, ndev);

	return ret;
}

bool wl_cfg80211_is_concurrent_mode(struct net_device *dev)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	if ((cfg) && (wl_get_drv_status_all(cfg, CONNECTED) > 1)) {
		return true;
	} else {
		return false;
	}
}

void* wl_cfg80211_get_dhdp(struct net_device *dev)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	return cfg->pub;
}

bool wl_cfg80211_is_p2p_active(struct net_device *dev)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	return (cfg && cfg->p2p);
}

bool wl_cfg80211_is_roam_offload(struct net_device * dev)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	return (cfg && cfg->roam_offload);
}

bool wl_cfg80211_is_event_from_connected_bssid(struct net_device * dev, const wl_event_msg_t *e,
		int ifidx)
{
	u8 *curbssid = NULL;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	if (!cfg) {
		/* When interface is created using wl
		 * ndev->ieee80211_ptr will be NULL.
		 */
		return NULL;
	}
	curbssid = wl_read_prof(cfg, dev, WL_PROF_BSSID);

	if (!curbssid) {
		return NULL;
	}

	if (memcmp(curbssid, &e->addr, ETHER_ADDR_LEN) == 0) {
		return true;
	}
	return false;
}

static void wl_cfg80211_work_handler(struct work_struct * work)
{
	struct bcm_cfg80211 *cfg = NULL;
	struct net_info *iter, *next;
	s32 err = BCME_OK;
	s32 pm = PM_FAST;
	dhd_pub_t *dhd;
	BCM_SET_CONTAINER_OF(cfg, work, struct bcm_cfg80211, pm_enable_work.work);
	WL_DBG(("Enter \n"));
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"")
#endif
	for_each_ndev(cfg, iter, next) {
		/* p2p discovery iface ndev could be null */
		if (iter->ndev) {
			if (!wl_get_drv_status(cfg, CONNECTED, iter->ndev) ||
				(wl_get_mode_by_netdev(cfg, iter->ndev) != WL_MODE_BSS &&
				wl_get_mode_by_netdev(cfg, iter->ndev) != WL_MODE_IBSS))
				continue;
			if (iter->ndev) {
				dhd = (dhd_pub_t *)(cfg->pub);
				if (dhd_conf_get_pm(dhd) >= 0)
					pm = dhd_conf_get_pm(dhd);
				if ((err = wldev_ioctl_set(iter->ndev, WLC_SET_PM,
						&pm, sizeof(pm))) != 0) {
					if (err == -ENODEV)
						WL_DBG(("%s:netdev not ready\n",
							iter->ndev->name));
					else
						WL_ERR(("%s:error (%d)\n",
							iter->ndev->name, err));
				} else
					wl_cfg80211_update_power_mode(iter->ndev);
			}
		}
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic pop")
#endif
	DHD_PM_WAKE_UNLOCK(cfg->pub);
}

u8
wl_get_action_category(void *frame, u32 frame_len)
{
	u8 category;
	u8 *ptr = (u8 *)frame;
	if (frame == NULL)
		return DOT11_ACTION_CAT_ERR_MASK;
	if (frame_len < DOT11_ACTION_HDR_LEN)
		return DOT11_ACTION_CAT_ERR_MASK;
	category = ptr[DOT11_ACTION_CAT_OFF];
	WL_INFORM(("Action Category: %d\n", category));
	return category;
}

int
wl_get_public_action(void *frame, u32 frame_len, u8 *ret_action)
{
	u8 *ptr = (u8 *)frame;
	if (frame == NULL || ret_action == NULL)
		return BCME_ERROR;
	if (frame_len < DOT11_ACTION_HDR_LEN)
		return BCME_ERROR;
	if (DOT11_ACTION_CAT_PUBLIC != wl_get_action_category(frame, frame_len))
		return BCME_ERROR;
	*ret_action = ptr[DOT11_ACTION_ACT_OFF];
	WL_INFORM(("Public Action : %d\n", *ret_action));
	return BCME_OK;
}

#ifdef WLFBT
int
wl_cfg80211_get_fbt_key(struct net_device *dev, uint8 *key, int total_len)
{
	struct bcm_cfg80211 * cfg = wl_get_cfg(dev);
	int bytes_written = -1;

	if (total_len < FBT_KEYLEN) {
		WL_ERR(("wl_cfg80211_get_fbt_key: Insufficient buffer \n"));
		goto end;
	}
	if (cfg) {
		memcpy(key, cfg->fbt_key, FBT_KEYLEN);
		bytes_written = FBT_KEYLEN;
	} else {
		bzero(key, FBT_KEYLEN);
		WL_ERR(("wl_cfg80211_get_fbt_key: Failed to copy KCK and KEK \n"));
	}
	prhex("KCK, KEK", (uchar *)key, FBT_KEYLEN);
end:
	return bytes_written;
}
#endif /* WLFBT */

static int
wl_cfg80211_delayed_roam(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const struct ether_addr *bssid)
{
	s32 err;
	wl_event_msg_t e;

	bzero(&e, sizeof(e));
	e.event_type = cpu_to_be32(WLC_E_ROAM);
	memcpy(&e.addr, bssid, ETHER_ADDR_LEN);
	/* trigger the roam event handler */
	err = wl_notify_roaming_status(cfg, ndev_to_cfgdev(ndev), &e, NULL);

	return err;
}

static s32
wl_cfg80211_parse_vndr_ies(u8 *parse, u32 len,
    struct parsed_vndr_ies *vndr_ies)
{
	s32 err = BCME_OK;
	vndr_ie_t *vndrie;
	bcm_tlv_t *ie;
	struct parsed_vndr_ie_info *parsed_info;
	u32 count = 0;
	s32 remained_len;

	remained_len = (s32)len;
	memset(vndr_ies, 0, sizeof(*vndr_ies));

	WL_DBG(("---> len %d\n", len));
	ie = (bcm_tlv_t *) parse;
	if (!bcm_valid_tlv(ie, remained_len))
		ie = NULL;
	while (ie) {
		if (count >= MAX_VNDR_IE_NUMBER)
			break;
		if (ie->id == DOT11_MNG_VS_ID) {
			vndrie = (vndr_ie_t *) ie;
			/* len should be bigger than OUI length + one data length at least */
			if (vndrie->len < (VNDR_IE_MIN_LEN + 1)) {
				WL_ERR(("%s: invalid vndr ie. length is too small %d\n",
					__FUNCTION__, vndrie->len));
				goto end;
			}
			/* if wpa or wme ie, do not add ie */
			if (!bcmp(vndrie->oui, (u8*)WPA_OUI, WPA_OUI_LEN) &&
				((vndrie->data[0] == WPA_OUI_TYPE) ||
				(vndrie->data[0] == WME_OUI_TYPE))) {
				WL_DBG(("Found WPA/WME oui. Do not add it\n"));
				goto end;
			}

			parsed_info = &vndr_ies->ie_info[count++];

			/* save vndr ie information */
			parsed_info->ie_ptr = (char *)vndrie;
			parsed_info->ie_len = (vndrie->len + TLV_HDR_LEN);
			memcpy(&parsed_info->vndrie, vndrie, sizeof(vndr_ie_t));
			vndr_ies->count = count;

			WL_DBG(("\t ** OUI %02x %02x %02x, type 0x%02x len:%d\n",
			parsed_info->vndrie.oui[0], parsed_info->vndrie.oui[1],
			parsed_info->vndrie.oui[2], parsed_info->vndrie.data[0],
			parsed_info->ie_len));
		}
end:
		ie = bcm_next_tlv(ie, &remained_len);
	}
	return err;
}

#ifdef WLADPS_SEAK_AP_WAR
static bool
wl_find_vndr_ies_specific_vender(struct bcm_cfg80211 *cfg,
	struct net_device *ndev, const u8 *vndr_oui)
{
	struct wl_connect_info *conn_info = wl_to_conn(cfg);
	struct parsed_vndr_ie_info *vndr_info_list;
	struct parsed_vndr_ies vndr_ies;
	bool ret = FALSE;
	int i;

	if (conn_info->resp_ie_len) {
		if ((wl_cfg80211_parse_vndr_ies((u8 *)conn_info->resp_ie,
				conn_info->resp_ie_len, &vndr_ies)) == BCME_OK) {
			for (i = 0; i < vndr_ies.count; i++) {
				vndr_info_list = &vndr_ies.ie_info[i];
				if (!bcmp(vndr_info_list->vndrie.oui,
						(u8*)vndr_oui, DOT11_OUI_LEN)) {
					WL_ERR(("Find OUI %02x %02x %02x\n",
						vndr_info_list->vndrie.oui[0],
						vndr_info_list->vndrie.oui[1],
						vndr_info_list->vndrie.oui[2]));
					ret = TRUE;
					break;
				}
			}
		}
	}
	return ret;
}

static s32
wl_set_adps_mode(struct bcm_cfg80211 *cfg, struct net_device *ndev, uint8 enable_mode)
{
	int i;
	int len;
	int ret = BCME_OK;

	bcm_iov_buf_t *iov_buf = NULL;
	wl_adps_params_v1_t *data = NULL;

	len = OFFSETOF(bcm_iov_buf_t, data) + sizeof(*data);
	iov_buf = kmalloc(len, GFP_KERNEL);
	if (iov_buf == NULL) {
		WL_ERR(("%s - failed to allocate %d bytes for iov_buf\n", __FUNCTION__, len));
		ret = BCME_NOMEM;
		goto exit;
	}

	iov_buf->version = WL_ADPS_IOV_VER;
	iov_buf->len = sizeof(*data);
	iov_buf->id = WL_ADPS_IOV_MODE;

	data = (wl_adps_params_v1_t *)iov_buf->data;
	data->version = ADPS_SUB_IOV_VERSION_1;
	data->length = sizeof(*data);
	data->mode = enable_mode;

	for (i = 1; i <= MAX_BANDS; i++) {
		data->band = i;
		ret = wldev_iovar_setbuf(ndev, "adps", iov_buf, len,
				cfg->ioctl_buf, WLC_IOCTL_SMLEN, NULL);
	}

exit:
	if (iov_buf) {
		kfree(iov_buf);
	}
	return ret;

}
#endif /* WLADPS_SEAK_AP_WAR */

static bool
wl_vndr_ies_exclude_vndr_oui(struct parsed_vndr_ie_info *vndr_info)
{
	int i = 0;

	while (exclude_vndr_oui_list[i]) {
		if (!memcmp(vndr_info->vndrie.oui,
			exclude_vndr_oui_list[i],
			DOT11_OUI_LEN)) {
			return TRUE;
		}
		i++;
	}

	return FALSE;
}

static bool
wl_vndr_ies_check_duplicate_vndr_oui(struct bcm_cfg80211 *cfg,
	struct parsed_vndr_ie_info *vndr_info)
{
	wl_vndr_oui_entry_t *oui_entry = NULL;

#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	list_for_each_entry(oui_entry, &cfg->vndr_oui_list, list) {
		if (!memcmp(oui_entry->oui, vndr_info->vndrie.oui, DOT11_OUI_LEN)) {
			return TRUE;
		}
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

	return FALSE;
}

static bool
wl_vndr_ies_add_vendor_oui_list(struct bcm_cfg80211 *cfg,
	struct parsed_vndr_ie_info *vndr_info)
{
	wl_vndr_oui_entry_t *oui_entry = NULL;

	oui_entry = kmalloc(sizeof(*oui_entry), GFP_KERNEL);
	if (oui_entry == NULL) {
		WL_ERR(("alloc failed\n"));
		return FALSE;
	}

	memcpy(oui_entry->oui, vndr_info->vndrie.oui, DOT11_OUI_LEN);

	INIT_LIST_HEAD(&oui_entry->list);
	list_add_tail(&oui_entry->list, &cfg->vndr_oui_list);

	return TRUE;
}

static void
wl_vndr_ies_clear_vendor_oui_list(struct bcm_cfg80211 *cfg)
{
	wl_vndr_oui_entry_t *oui_entry = NULL;

#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	while (!list_empty(&cfg->vndr_oui_list)) {
		oui_entry = list_entry(cfg->vndr_oui_list.next, wl_vndr_oui_entry_t, list);
		if (oui_entry) {
			list_del(&oui_entry->list);
			kfree(oui_entry);
		}
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
}

static int
wl_vndr_ies_get_vendor_oui(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	char *vndr_oui, u32 vndr_oui_len)
{
	int i;
	int vndr_oui_num = 0;

	struct wl_connect_info *conn_info = wl_to_conn(cfg);
	wl_vndr_oui_entry_t *oui_entry = NULL;
	struct parsed_vndr_ie_info *vndr_info;
	struct parsed_vndr_ies vndr_ies;

	char *pos = vndr_oui;
	u32 remained_buf_len = vndr_oui_len;

	if (!conn_info->resp_ie_len) {
		return BCME_ERROR;
	}

	wl_vndr_ies_clear_vendor_oui_list(cfg);

	if ((wl_cfg80211_parse_vndr_ies((u8 *)conn_info->resp_ie,
		conn_info->resp_ie_len, &vndr_ies)) == BCME_OK) {
		for (i = 0; i < vndr_ies.count; i++) {
			vndr_info = &vndr_ies.ie_info[i];
			if (wl_vndr_ies_exclude_vndr_oui(vndr_info)) {
				continue;
			}

			if (wl_vndr_ies_check_duplicate_vndr_oui(cfg, vndr_info)) {
				continue;
			}

			wl_vndr_ies_add_vendor_oui_list(cfg, vndr_info);
			vndr_oui_num++;
		}
	}

	if (vndr_oui) {
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
		list_for_each_entry(oui_entry, &cfg->vndr_oui_list, list) {
			if (remained_buf_len < VNDR_OUI_STR_LEN) {
				return BCME_ERROR;
			}
			pos += snprintf(pos, VNDR_OUI_STR_LEN, "%02X-%02X-%02X ",
				oui_entry->oui[0], oui_entry->oui[1], oui_entry->oui[2]);
			remained_buf_len -= VNDR_OUI_STR_LEN;
		}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
	}

	return vndr_oui_num;
}

int
wl_cfg80211_get_vndr_ouilist(struct bcm_cfg80211 *cfg, uint8 *buf, int max_cnt)
{
	wl_vndr_oui_entry_t *oui_entry = NULL;
	int cnt = 0;

#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	list_for_each_entry(oui_entry, &cfg->vndr_oui_list, list) {
		memcpy(buf, oui_entry->oui, DOT11_OUI_LEN);
		cnt++;
		if (cnt >= max_cnt) {
			return cnt;
		}
		buf += DOT11_OUI_LEN;
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
	return cnt;
}

s32
wl_cfg80211_clear_per_bss_ies(struct bcm_cfg80211 *cfg, s32 bssidx)
{
	s32 index;
	struct net_info *netinfo;
	s32 vndrie_flag[] = {VNDR_IE_BEACON_FLAG, VNDR_IE_PRBRSP_FLAG,
		VNDR_IE_ASSOCRSP_FLAG, VNDR_IE_PRBREQ_FLAG, VNDR_IE_ASSOCREQ_FLAG};

	netinfo = wl_get_netinfo_by_bssidx(cfg, bssidx);
	if (!netinfo || !netinfo->wdev) {
		WL_ERR(("netinfo or netinfo->wdev is NULL\n"));
		return -1;
	}

	WL_DBG(("clear management vendor IEs for bssidx:%d \n", bssidx));
	/* Clear the IEs set in the firmware so that host is in sync with firmware */
	for (index = 0; index < ARRAYSIZE(vndrie_flag); index++) {
		if (wl_cfg80211_set_mgmt_vndr_ies(cfg, wdev_to_cfgdev(netinfo->wdev),
			bssidx, vndrie_flag[index], NULL, 0) < 0)
			WL_ERR(("vndr_ies clear failed. Ignoring.. \n"));
	}

	return 0;
}

s32
wl_cfg80211_clear_mgmt_vndr_ies(struct bcm_cfg80211 *cfg)
{
	struct net_info *iter, *next;

	WL_DBG(("clear management vendor IEs \n"));
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"")
#endif
	for_each_ndev(cfg, iter, next) {
		wl_cfg80211_clear_per_bss_ies(cfg, iter->bssidx);
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic pop")
#endif
	return 0;
}

#define WL_VNDR_IE_MAXLEN 2048
static s8 g_mgmt_ie_buf[WL_VNDR_IE_MAXLEN];
int
wl_cfg80211_set_mgmt_vndr_ies(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	s32 bssidx, s32 pktflag, const u8 *vndr_ie, u32 vndr_ie_len)
{
	struct net_device *ndev = NULL;
	s32 ret = BCME_OK;
	u8  *curr_ie_buf = NULL;
	u8  *mgmt_ie_buf = NULL;
	u32 mgmt_ie_buf_len = 0;
	u32 *mgmt_ie_len = 0;
	u32 del_add_ie_buf_len = 0;
	u32 total_ie_buf_len = 0;
	u32 parsed_ie_buf_len = 0;
	struct parsed_vndr_ies old_vndr_ies;
	struct parsed_vndr_ies new_vndr_ies;
	s32 i;
	u8 *ptr;
	s32 remained_buf_len;
	wl_bss_vndr_ies_t *ies = NULL;
	struct net_info *netinfo;

	WL_DBG(("Enter. pktflag:0x%x bssidx:%x vnd_ie_len:%d \n",
		pktflag, bssidx, vndr_ie_len));

	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);

	if (bssidx > WL_MAX_IFS) {
		WL_ERR(("bssidx > supported concurrent Ifaces \n"));
		return -EINVAL;
	}

	netinfo = wl_get_netinfo_by_bssidx(cfg, bssidx);
	if (!netinfo) {
		WL_ERR(("net_info ptr is NULL \n"));
		return -EINVAL;
	}

	/* Clear the global buffer */
	memset(g_mgmt_ie_buf, 0, sizeof(g_mgmt_ie_buf));
	curr_ie_buf = g_mgmt_ie_buf;
	ies = &netinfo->bss.ies;

	switch (pktflag) {
		case VNDR_IE_PRBRSP_FLAG :
			mgmt_ie_buf = ies->probe_res_ie;
			mgmt_ie_len = &ies->probe_res_ie_len;
			mgmt_ie_buf_len = sizeof(ies->probe_res_ie);
			break;
		case VNDR_IE_ASSOCRSP_FLAG :
			mgmt_ie_buf = ies->assoc_res_ie;
			mgmt_ie_len = &ies->assoc_res_ie_len;
			mgmt_ie_buf_len = sizeof(ies->assoc_res_ie);
			break;
		case VNDR_IE_BEACON_FLAG :
			mgmt_ie_buf = ies->beacon_ie;
			mgmt_ie_len = &ies->beacon_ie_len;
			mgmt_ie_buf_len = sizeof(ies->beacon_ie);
			break;
		case VNDR_IE_PRBREQ_FLAG :
			mgmt_ie_buf = ies->probe_req_ie;
			mgmt_ie_len = &ies->probe_req_ie_len;
			mgmt_ie_buf_len = sizeof(ies->probe_req_ie);
			break;
		case VNDR_IE_ASSOCREQ_FLAG :
			mgmt_ie_buf = ies->assoc_req_ie;
			mgmt_ie_len = &ies->assoc_req_ie_len;
			mgmt_ie_buf_len = sizeof(ies->assoc_req_ie);
			break;
		default:
			mgmt_ie_buf = NULL;
			mgmt_ie_len = NULL;
			WL_ERR(("not suitable packet type (%d)\n", pktflag));
			return BCME_ERROR;
	}

	if (vndr_ie_len > mgmt_ie_buf_len) {
		WL_ERR(("extra IE size too big\n"));
		ret = -ENOMEM;
	} else {
		/* parse and save new vndr_ie in curr_ie_buff before comparing it */
		if (vndr_ie && vndr_ie_len && curr_ie_buf) {
			ptr = curr_ie_buf;
/* must discard vndr_ie constness, attempt to change vndr_ie arg to non-const
 * causes cascade of errors in other places, fix involves const casts there
 */
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"")
#endif
			if ((ret = wl_cfg80211_parse_vndr_ies((u8 *)vndr_ie,
				vndr_ie_len, &new_vndr_ies)) < 0) {
				WL_ERR(("parse vndr ie failed \n"));
				goto exit;
			}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic pop")
#endif
			for (i = 0; i < new_vndr_ies.count; i++) {
				struct parsed_vndr_ie_info *vndrie_info =
					&new_vndr_ies.ie_info[i];

				if ((parsed_ie_buf_len + vndrie_info->ie_len) > WL_VNDR_IE_MAXLEN) {
					WL_ERR(("IE size is too big (%d > %d)\n",
						parsed_ie_buf_len, WL_VNDR_IE_MAXLEN));
					ret = -EINVAL;
					goto exit;
				}

				memcpy(ptr + parsed_ie_buf_len, vndrie_info->ie_ptr,
					vndrie_info->ie_len);
				parsed_ie_buf_len += vndrie_info->ie_len;
			}
		}

		if (mgmt_ie_buf != NULL) {
			if (parsed_ie_buf_len && (parsed_ie_buf_len == *mgmt_ie_len) &&
				(memcmp(mgmt_ie_buf, curr_ie_buf, parsed_ie_buf_len) == 0)) {
				WL_INFORM(("Previous mgmt IE is equals to current IE"));
				goto exit;
			}

			/* parse old vndr_ie */
			if ((ret = wl_cfg80211_parse_vndr_ies(mgmt_ie_buf, *mgmt_ie_len,
				&old_vndr_ies)) < 0) {
				WL_ERR(("parse vndr ie failed \n"));
				goto exit;
			}
			/* make a command to delete old ie */
			for (i = 0; i < old_vndr_ies.count; i++) {
				struct parsed_vndr_ie_info *vndrie_info =
				&old_vndr_ies.ie_info[i];

				WL_INFORM(("DELETED ID : %d, Len: %d , OUI:%02x:%02x:%02x\n",
					vndrie_info->vndrie.id, vndrie_info->vndrie.len,
					vndrie_info->vndrie.oui[0], vndrie_info->vndrie.oui[1],
					vndrie_info->vndrie.oui[2]));

				del_add_ie_buf_len = wl_cfgp2p_vndr_ie(cfg, curr_ie_buf,
					pktflag, vndrie_info->vndrie.oui,
					vndrie_info->vndrie.id,
					vndrie_info->ie_ptr + VNDR_IE_FIXED_LEN,
					vndrie_info->ie_len - VNDR_IE_FIXED_LEN,
					"del");

				curr_ie_buf += del_add_ie_buf_len;
				total_ie_buf_len += del_add_ie_buf_len;
			}
		}

		*mgmt_ie_len = 0;
		/* Add if there is any extra IE */
		if (mgmt_ie_buf && parsed_ie_buf_len) {
			ptr = mgmt_ie_buf;

			remained_buf_len = mgmt_ie_buf_len;

			/* make a command to add new ie */
			for (i = 0; i < new_vndr_ies.count; i++) {
				struct parsed_vndr_ie_info *vndrie_info =
					&new_vndr_ies.ie_info[i];

				WL_INFORM(("ADDED ID : %d, Len: %d(%d), OUI:%02x:%02x:%02x\n",
					vndrie_info->vndrie.id, vndrie_info->vndrie.len,
					vndrie_info->ie_len - 2,
					vndrie_info->vndrie.oui[0], vndrie_info->vndrie.oui[1],
					vndrie_info->vndrie.oui[2]));

				del_add_ie_buf_len = wl_cfgp2p_vndr_ie(cfg, curr_ie_buf,
					pktflag, vndrie_info->vndrie.oui,
					vndrie_info->vndrie.id,
					vndrie_info->ie_ptr + VNDR_IE_FIXED_LEN,
					vndrie_info->ie_len - VNDR_IE_FIXED_LEN,
					"add");

				/* verify remained buf size before copy data */
				if (remained_buf_len >= vndrie_info->ie_len) {
					remained_buf_len -= vndrie_info->ie_len;
				} else {
					WL_ERR(("no space in mgmt_ie_buf: pktflag = %d, "
					"found vndr ies # = %d(cur %d), remained len %d, "
					"cur mgmt_ie_len %d, new ie len = %d\n",
					pktflag, new_vndr_ies.count, i, remained_buf_len,
					*mgmt_ie_len, vndrie_info->ie_len));
					break;
				}

				/* save the parsed IE in cfg struct */
				memcpy(ptr + (*mgmt_ie_len), vndrie_info->ie_ptr,
					vndrie_info->ie_len);
				*mgmt_ie_len += vndrie_info->ie_len;
				curr_ie_buf += del_add_ie_buf_len;
				total_ie_buf_len += del_add_ie_buf_len;
			}
		}

		if (total_ie_buf_len && cfg->ioctl_buf != NULL) {
			ret  = wldev_iovar_setbuf_bsscfg(ndev, "vndr_ie", g_mgmt_ie_buf,
				total_ie_buf_len, cfg->ioctl_buf, WLC_IOCTL_MAXLEN,
				bssidx, &cfg->ioctl_buf_sync);
			if (ret)
				WL_ERR(("vndr ie set error : %d\n", ret));
		}
	}
exit:

return ret;
}

#ifdef WL_CFG80211_ACL
static int
wl_cfg80211_set_mac_acl(struct wiphy *wiphy, struct net_device *cfgdev,
	const struct cfg80211_acl_data *acl)
{
	int i;
	int ret = 0;
	int macnum = 0;
	int macmode = MACLIST_MODE_DISABLED;
	struct maclist *list;

	/* get the MAC filter mode */
	if (acl && acl->acl_policy == NL80211_ACL_POLICY_DENY_UNLESS_LISTED) {
		macmode = MACLIST_MODE_ALLOW;
	} else if (acl && acl->acl_policy == NL80211_ACL_POLICY_ACCEPT_UNLESS_LISTED &&
	acl->n_acl_entries) {
		macmode = MACLIST_MODE_DENY;
	}

	/* if acl == NULL, macmode is still disabled.. */
	if (macmode == MACLIST_MODE_DISABLED) {
		if ((ret = wl_android_set_ap_mac_list(cfgdev, macmode, NULL)) != 0)
			WL_ERR(("%s : Setting MAC list failed error=%d\n", __FUNCTION__, ret));

		return ret;
	}

	macnum = acl->n_acl_entries;
	if (macnum < 0 || macnum > MAX_NUM_MAC_FILT) {
		WL_ERR(("%s : invalid number of MAC address entries %d\n",
			__FUNCTION__, macnum));
		return -1;
	}

	/* allocate memory for the MAC list */
	list = (struct maclist*)kmalloc(sizeof(int) +
		sizeof(struct ether_addr) * macnum, GFP_KERNEL);
	if (!list) {
		WL_ERR(("%s : failed to allocate memory\n", __FUNCTION__));
		return -1;
	}

	/* prepare the MAC list */
	list->count = htod32(macnum);
	for (i = 0; i < macnum; i++) {
		memcpy(&list->ea[i], &acl->mac_addrs[i], ETHER_ADDR_LEN);
	}
	/* set the list */
	if ((ret = wl_android_set_ap_mac_list(cfgdev, macmode, list)) != 0)
		WL_ERR(("%s : Setting MAC list failed error=%d\n", __FUNCTION__, ret));

	kfree(list);

	return ret;
}
#endif /* WL_CFG80211_ACL */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0))
int wl_chspec_chandef(chanspec_t chanspec,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
	struct cfg80211_chan_def *chandef,
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION (3, 5, 0) && (LINUX_VERSION_CODE <= (3, 7, 0)))
	struct chan_info *chaninfo,
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0)) */
struct wiphy *wiphy)

{
	uint16 freq = 0;
	int chan_type = 0;
	int channel = 0;
	struct ieee80211_channel *chan;

	if (!chandef) {
		return -1;
	}
	channel = CHSPEC_CHANNEL(chanspec);

	switch (CHSPEC_BW(chanspec)) {
		case WL_CHANSPEC_BW_20:
			chan_type = NL80211_CHAN_HT20;
			break;
		case WL_CHANSPEC_BW_40:
		{
			if (CHSPEC_SB_UPPER(chanspec)) {
				channel += CH_10MHZ_APART;
			} else {
				channel -= CH_10MHZ_APART;
			}
		}
			chan_type = NL80211_CHAN_HT40PLUS;
			break;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION (3, 8, 0))
		case WL_CHANSPEC_BW_80:
		case WL_CHANSPEC_BW_8080:
		{
			uint16 sb = CHSPEC_CTL_SB(chanspec);

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

			if (sb == WL_CHANSPEC_CTL_SB_LL || sb == WL_CHANSPEC_CTL_SB_LU)
				chan_type = NL80211_CHAN_HT40MINUS;
			else if (sb == WL_CHANSPEC_CTL_SB_UL || sb == WL_CHANSPEC_CTL_SB_UU)
				chan_type = NL80211_CHAN_HT40PLUS;
		}
			break;
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION (3, 8, 0)) */
		default:
			chan_type = NL80211_CHAN_HT20;
			break;

	}

	if (CHSPEC_IS5G(chanspec))
		freq = ieee80211_channel_to_frequency(channel, NL80211_BAND_5GHZ);
	else
		freq = ieee80211_channel_to_frequency(channel, NL80211_BAND_2GHZ);

	chan = ieee80211_get_channel(wiphy, freq);
	WL_DBG(("channel:%d freq:%d chan_type: %d chan_ptr:%p \n",
		channel, freq, chan_type, chan));

	if (unlikely(!chan)) {
		/* fw and cfg80211 channel lists are not in sync */
		WL_ERR(("Couldn't find matching channel in wiphy channel list \n"));
		ASSERT(0);
		return -EINVAL;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION (3, 8, 0))
	cfg80211_chandef_create(chandef, chan, chan_type);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION (3, 5, 0) && (LINUX_VERSION_CODE <= (3, 7, 0)))
	chaninfo->freq = freq;
	chaninfo->chan_type = chan_type;
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION (3, 8, 0)) */
	return 0;
}

void
wl_cfg80211_ch_switch_notify(struct net_device *dev, uint16 chanspec, struct wiphy *wiphy)
{
	u32 freq;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION (3, 8, 0))
	struct cfg80211_chan_def chandef;
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION (3, 5, 0) && (LINUX_VERSION_CODE <= (3, 7, 0)))
	struct chan_info chaninfo;
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION (3, 8, 0)) */

	if (!wiphy) {
		WL_MSG(dev->name, "wiphy is null\n");
		return;
	}
#ifndef ALLOW_CHSW_EVT
	/* Channel switch support is only for AP/GO/ADHOC/MESH */
	if (dev->ieee80211_ptr->iftype == NL80211_IFTYPE_STATION ||
		dev->ieee80211_ptr->iftype == NL80211_IFTYPE_P2P_CLIENT) {
		WL_ERR(("No channel switch notify support for STA/GC\n"));
		return;
	}
#endif /* !ALLOW_CHSW_EVT */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION (3, 8, 0))
	if (wl_chspec_chandef(chanspec, &chandef, wiphy))
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION (3, 5, 0) && (LINUX_VERSION_CODE <= (3, 7, 0)))
	if (wl_chspec_chandef(chanspec, &chaninfo, wiphy))
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION (3, 8, 0)) */
	{
		WL_ERR(("chspec_chandef failed\n"));
		return;
	}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION (3, 8, 0))
	freq = chandef.chan ? chandef.chan->center_freq : chandef.center_freq1;
	cfg80211_ch_switch_notify(dev, &chandef);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION (3, 5, 0) && (LINUX_VERSION_CODE <= (3, 7, 0)))
	freq = chan_info.freq;
	cfg80211_ch_switch_notify(dev, freq, chan_info.chan_type);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION (3, 8, 0)) */

	WL_MSG(dev->name, "Channel switch notification for freq: %d chanspec: 0x%x\n",
		freq, chanspec);
	return;
}
#endif /* LINUX_VERSION_CODE >= (3, 5, 0) */

#ifdef WL11ULB
s32
wl_cfg80211_set_ulb_mode(struct net_device *dev, int mode)
{
	int ret;
	int cur_mode;

	ret = wldev_iovar_getint(dev, "ulb_mode", &cur_mode);
	if (unlikely(ret)) {
		WL_ERR(("[ULB] ulb_mode get failed. ret:%d \n", ret));
		return ret;
	}

	if (cur_mode == mode) {
		/* If request mode is same as that of the current mode, then
		 * do nothing (Avoid unnecessary wl down and up).
		 */
		WL_INFORM(("[ULB] No change in ulb_mode. Do nothing.\n"));
		return 0;
	}

	/* setting of ulb_mode requires wl to be down */
	ret = wldev_ioctl_set(dev, WLC_DOWN, NULL, 0);
	if (unlikely(ret)) {
		WL_ERR(("[ULB] WLC_DOWN command failed:[%d]\n", ret));
		return ret;
	}

	if (mode >= MAX_SUPP_ULB_MODES) {
		WL_ERR(("[ULB] unsupported ulb_mode :[%d]\n", mode));
		return -EINVAL;
	}

	ret = wldev_iovar_setint(dev, "ulb_mode", mode);
	if (unlikely(ret)) {
		WL_ERR(("[ULB] ulb_mode set failed. ret:%d \n", ret));
		return ret;
	}

	ret = wldev_ioctl_set(dev, WLC_UP, NULL, 0);
	if (unlikely(ret)) {
		WL_ERR(("[ULB] WLC_DOWN command failed:[%d]\n", ret));
		return ret;
	}

	WL_DBG(("[ULB] ulb_mode set to %d successfully \n", mode));

	return ret;
}

static s32
wl_cfg80211_ulbbw_to_ulbchspec(u32 bw)
{
	if (bw == ULB_BW_DISABLED) {
		return WL_CHANSPEC_BW_20;
	} else if (bw == ULB_BW_10MHZ) {
		return WL_CHANSPEC_BW_10;
	} else if (bw == ULB_BW_5MHZ) {
		return WL_CHANSPEC_BW_5;
	} else if (bw == ULB_BW_2P5MHZ) {
		return WL_CHANSPEC_BW_2P5;
	} else {
		WL_ERR(("[ULB] unsupported value for ulb_bw \n"));
		return -EINVAL;
	}
}

static chanspec_t
wl_cfg80211_ulb_get_min_bw_chspec(struct bcm_cfg80211 *cfg, struct wireless_dev *wdev, s32 bssidx)
{
	struct net_info *_netinfo;

	/*
	 *  Return the chspec value corresponding to the
	 *  BW setting for a particular interface
	 */
	if (wdev) {
		/* if wdev is provided, use it */
		_netinfo = wl_get_netinfo_by_wdev(cfg, wdev);
	} else if (bssidx >= 0) {
		/* if wdev is not provided, use it */
		_netinfo = wl_get_netinfo_by_bssidx(cfg, bssidx);
	} else {
		WL_ERR(("[ULB] wdev/bssidx not provided\n"));
		return INVCHANSPEC;
	}

	if (unlikely(!_netinfo)) {
		WL_ERR(("[ULB] net_info is null \n"));
		return INVCHANSPEC;
	}

	if (_netinfo->ulb_bw) {
		WL_DBG(("[ULB] wdev_ptr:%p ulb_bw:0x%x \n", _netinfo->wdev, _netinfo->ulb_bw));
		return wl_cfg80211_ulbbw_to_ulbchspec(_netinfo->ulb_bw);
	} else {
		return WL_CHANSPEC_BW_20;
	}
}

static s32
wl_cfg80211_get_ulb_bw(struct bcm_cfg80211 *cfg, struct wireless_dev *wdev)
{
	struct net_info *_netinfo = wl_get_netinfo_by_wdev(cfg, wdev);

	/*
	 *  Return the ulb_bw setting for a
	 *  particular interface
	 */
	if (unlikely(!_netinfo)) {
		WL_ERR(("[ULB] net_info is null \n"));
		return -1;
	}

	return _netinfo->ulb_bw;
}

s32
wl_cfg80211_set_ulb_bw(struct net_device *dev,
	u32 ulb_bw,  char *ifname)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	int ret;
	int mode;
	struct net_info *_netinfo = NULL, *iter, *next;
	u32 bssidx;

	if (!ifname)
		return -EINVAL;

	WL_DBG(("[ULB] Enter. bw_type:%d \n", ulb_bw));

	ret = wldev_iovar_getint(dev, "ulb_mode", &mode);
	if (unlikely(ret)) {
		WL_ERR(("[ULB] ulb_mode not supported \n"));
		return ret;
	}

	if (mode != ULB_MODE_STD_ALONE_MODE) {
		WL_ERR(("[ULB] ulb bw modification allowed only in stand-alone mode\n"));
		return -EINVAL;
	}

	if (ulb_bw >= MAX_SUPP_ULB_BW) {
		WL_ERR(("[ULB] unsupported value (%d) for ulb_bw \n", ulb_bw));
		return -EINVAL;
	}

#ifdef WL_CFG80211_P2P_DEV_IF
	if (strcmp(ifname, "p2p-dev-wlan0") == 0) {
		/* Use wdev corresponding to the dedicated p2p discovery interface */
		if (likely(cfg->p2p_wdev)) {
			_netinfo = wl_get_netinfo_by_wdev(cfg, cfg->p2p_wdev);
		} else {
			return -ENODEV;
		}
	}
#endif /* WL_CFG80211_P2P_DEV_IF */
	if (!_netinfo) {
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
		for_each_ndev(cfg, iter, next) {
			if (iter->ndev) {
				if (strncmp(iter->ndev->name, ifname, strlen(ifname)) == 0) {
					_netinfo = wl_get_netinfo_by_netdev(cfg, iter->ndev);
				}
			}
		}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
	}

	if (!_netinfo)
		return -ENODEV;
	bssidx = _netinfo->bssidx;
	_netinfo->ulb_bw = ulb_bw;


	WL_DBG(("[ULB] Applying ulb_bw:%d for bssidx:%d \n", ulb_bw, bssidx));
	ret = wldev_iovar_setbuf_bsscfg(dev, "ulb_bw", (void *)&ulb_bw, 4,
		cfg->ioctl_buf, WLC_IOCTL_SMLEN, bssidx,
		&cfg->ioctl_buf_sync);
	if (unlikely(ret)) {
		WL_ERR(("[ULB] ulb_bw set failed. ret:%d \n", ret));
		return ret;
	}

	return ret;
}
#endif /* WL11ULB */

static void
wl_ap_channel_ind(struct bcm_cfg80211 *cfg,
	struct net_device *ndev,
	chanspec_t chanspec)
{
	u32 channel = LCHSPEC_CHANNEL(chanspec);

	WL_DBG(("(%s) AP channel:%d chspec:0x%x \n",
		ndev->name, channel, chanspec));
	if (cfg->ap_oper_channel && (cfg->ap_oper_channel != channel)) {
		/*
		 * If cached channel is different from the channel indicated
		 * by the event, notify user space about the channel switch.
		 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0))
		wl_cfg80211_ch_switch_notify(ndev, chanspec, bcmcfg_to_wiphy(cfg));
#endif /* LINUX_VERSION_CODE >= (3, 5, 0) */
		cfg->ap_oper_channel = channel;
	}
}

static s32
wl_ap_start_ind(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
const wl_event_msg_t *e, void *data)
{
	struct net_device *ndev = NULL;
	chanspec_t chanspec;

	WL_DBG(("Enter\n"));
	if (unlikely(e->status)) {
		WL_ERR(("status:0x%x \n", e->status));
		return -1;
	}

	if (!data) {
		return -EINVAL;
	}

	if (likely(cfgdev)) {
		ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);
		chanspec = *((chanspec_t *)data);

		if (wl_get_mode_by_netdev(cfg, ndev) == WL_MODE_AP) {
			/* For AP/GO role */
			wl_ap_channel_ind(cfg, ndev, chanspec);
		}
	}

	return 0;
}

static s32
wl_csa_complete_ind(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
const wl_event_msg_t *e, void *data)
{
	int error = 0;
	u32 chanspec = 0;
	struct net_device *ndev = NULL;

	WL_DBG(("Enter\n"));
	if (unlikely(e->status)) {
		WL_ERR(("status:0x%x \n", e->status));
		return -1;
	}

	if (likely(cfgdev)) {
		ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);
		error = wldev_iovar_getint(ndev, "chanspec", &chanspec);
		if (unlikely(error)) {
			WL_ERR(("Get chanspec error: %d \n", error));
			return -1;
		}

		if (wl_get_mode_by_netdev(cfg, ndev) == WL_MODE_AP) {
			/* For AP/GO role */
			wl_ap_channel_ind(cfg, ndev, chanspec);
		} else {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0))
			wl_cfg80211_ch_switch_notify(ndev, chanspec, bcmcfg_to_wiphy(cfg));
#endif /* LINUX_VERSION_CODE >= (3, 5, 0) */
		}

	}

	return 0;
}


void wl_cfg80211_clear_security(struct bcm_cfg80211 *cfg)
{
	struct net_device *dev = bcmcfg_to_prmry_ndev(cfg);
	int err;

	/* Clear the security settings on the primary Interface */
	err = wldev_iovar_setint(dev, "wsec", 0);
	if (unlikely(err)) {
		WL_ERR(("wsec clear failed \n"));
	}
	err = wldev_iovar_setint(dev, "auth", 0);
	if (unlikely(err)) {
		WL_ERR(("auth clear failed \n"));
	}
	err = wldev_iovar_setint(dev, "wpa_auth", WPA_AUTH_DISABLED);
	if (unlikely(err)) {
		WL_ERR(("wpa_auth clear failed \n"));
	}
}

#ifdef WL_CFG80211_P2P_DEV_IF
void wl_cfg80211_del_p2p_wdev(struct net_device *dev)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	struct wireless_dev *wdev = NULL;

	WL_DBG(("Enter \n"));
	if (!cfg) {
		WL_ERR(("Invalid Ptr\n"));
		return;
	} else {
		wdev = cfg->p2p_wdev;
	}

	if (wdev && cfg->down_disc_if) {
		wl_cfgp2p_del_p2p_disc_if(wdev, cfg);
		cfg->down_disc_if = FALSE;
	}
}
#endif /* WL_CFG80211_P2P_DEV_IF */

#ifdef GTK_OFFLOAD_SUPPORT
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0))
static s32
wl_cfg80211_set_rekey_data(struct wiphy *wiphy, struct net_device *dev,
		struct cfg80211_gtk_rekey_data *data)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	s32 err = 0;
	gtk_keyinfo_t keyinfo;
	bcol_gtk_para_t bcol_keyinfo;

	WL_DBG(("Enter\n"));
	if (data == NULL || cfg->p2p_net == dev) {
		WL_ERR(("data is NULL or wrong net device\n"));
		return -EINVAL;
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)
	prhex("kck", (uchar *) (data->kck), RSN_KCK_LENGTH);
	prhex("kek", (uchar *) (data->kek), RSN_KEK_LENGTH);
	prhex("replay_ctr", (uchar *) (data->replay_ctr), RSN_REPLAY_LEN);
#else
	prhex("kck", (uchar *)data->kck, RSN_KCK_LENGTH);
	prhex("kek", (uchar *)data->kek, RSN_KEK_LENGTH);
	prhex("replay_ctr", (uchar *)data->replay_ctr, RSN_REPLAY_LEN);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0) */
	bcopy(data->kck, keyinfo.KCK, RSN_KCK_LENGTH);
	bcopy(data->kek, keyinfo.KEK, RSN_KEK_LENGTH);
	bcopy(data->replay_ctr, keyinfo.ReplayCounter, RSN_REPLAY_LEN);

	memset(&bcol_keyinfo, 0, sizeof(bcol_keyinfo));
	bcol_keyinfo.enable = 1;
	bcol_keyinfo.ptk_len = 64;
	memcpy(&bcol_keyinfo.ptk[0], data->kck, RSN_KCK_LENGTH);
	memcpy(&bcol_keyinfo.ptk[RSN_KCK_LENGTH], data->kek, RSN_KEK_LENGTH);
	err = wldev_iovar_setbuf(dev, "bcol_gtk_rekey_ptk", &bcol_keyinfo,
		sizeof(bcol_keyinfo), cfg->ioctl_buf, WLC_IOCTL_SMLEN, &cfg->ioctl_buf_sync);
	if (!err) {
		return err;
	}

	if ((err = wldev_iovar_setbuf(dev, "gtk_key_info", &keyinfo, sizeof(keyinfo),
		cfg->ioctl_buf, WLC_IOCTL_SMLEN, &cfg->ioctl_buf_sync)) < 0) {
		WL_ERR(("seting gtk_key_info failed code=%d\n", err));
		return err;
	}

	WL_DBG(("Exit\n"));
	return err;
}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0) */
#endif /* GTK_OFFLOAD_SUPPORT */

#if defined(WL_SUPPORT_AUTO_CHANNEL)
int
wl_cfg80211_set_spect(struct net_device *dev, int spect)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	int wlc_down = 1;
	int wlc_up = 1;
	int err = BCME_OK;

	if (!wl_get_drv_status_all(cfg, CONNECTED)) {
		err = wldev_ioctl_set(dev, WLC_DOWN, &wlc_down, sizeof(wlc_down));
		if (err) {
			WL_ERR(("%s: WLC_DOWN failed: code: %d\n", __func__, err));
			return err;
		}

		err = wldev_ioctl_set(dev, WLC_SET_SPECT_MANAGMENT, &spect, sizeof(spect));
		if (err) {
			WL_ERR(("%s: error setting spect: code: %d\n", __func__, err));
			return err;
		}

		err = wldev_ioctl_set(dev, WLC_UP, &wlc_up, sizeof(wlc_up));
		if (err) {
			WL_ERR(("%s: WLC_UP failed: code: %d\n", __func__, err));
			return err;
		}
	}
	return err;
}

int
wl_cfg80211_get_sta_channel(struct net_device *dev)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	int channel = 0;

	if (wl_get_drv_status(cfg, CONNECTED, dev)) {
		channel = cfg->channel;
	}
	return channel;
}
#endif /* WL_SUPPORT_AUTO_CHANNEL */
#ifdef P2P_LISTEN_OFFLOADING
s32
wl_cfg80211_p2plo_deinit(struct bcm_cfg80211 *cfg)
{
	s32 bssidx;
	int ret = 0;
	int p2plo_pause = 0;
	if (!cfg || !cfg->p2p) {
		WL_ERR(("Wl %p or cfg->p2p %p is null\n",
			cfg, cfg ? cfg->p2p : 0));
		return 0;
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
#endif /* P2P_LISTEN_OFFLOADING */

u64
wl_cfg80211_get_new_roc_id(struct bcm_cfg80211 *cfg)
{
	u64 id = 0;
	id = ++cfg->last_roc_id;
#ifdef  P2P_LISTEN_OFFLOADING
	if (id == P2PO_COOKIE) {
		id = ++cfg->last_roc_id;
	}
#endif /* P2P_LISTEN_OFFLOADING */
	if (id == 0)
		id = ++cfg->last_roc_id;
	return id;
}

#if defined(SUPPORT_RANDOM_MAC_SCAN)
int
wl_cfg80211_set_random_mac(struct net_device *dev, bool enable)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	int ret;

	if (cfg->random_mac_enabled == enable) {
		WL_ERR(("Random MAC already %s\n", enable ? "Enabled" : "Disabled"));
		return BCME_OK;
	}

	if (enable) {
		ret = wl_cfg80211_random_mac_enable(dev);
	} else {
		ret = wl_cfg80211_random_mac_disable(dev);
	}

	if (!ret) {
		cfg->random_mac_enabled = enable;
	}

	return ret;
}

int
wl_cfg80211_random_mac_enable(struct net_device *dev)
{
	u8 random_mac[ETH_ALEN] = {0, };
	u8 rand_bytes[3] = {0, };
	s32 err = BCME_ERROR;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	if (wl_get_drv_status_all(cfg, CONNECTED) || wl_get_drv_status_all(cfg, CONNECTING) ||
	    wl_get_drv_status_all(cfg, AP_CREATED) || wl_get_drv_status_all(cfg, AP_CREATING)) {
		WL_ERR(("fail to Set random mac, current state is wrong\n"));
		return err;
	}

	memcpy(random_mac, bcmcfg_to_prmry_ndev(cfg)->dev_addr, ETH_ALEN);
	get_random_bytes(&rand_bytes, sizeof(rand_bytes));

	if (rand_bytes[2] == 0x0 || rand_bytes[2] == 0xff) {
		rand_bytes[2] = 0xf0;
	}

	memcpy(&random_mac[3], rand_bytes, sizeof(rand_bytes));

	err = wldev_iovar_setbuf_bsscfg(bcmcfg_to_prmry_ndev(cfg), "cur_etheraddr",
		random_mac, ETH_ALEN, cfg->ioctl_buf, WLC_IOCTL_SMLEN, 0, &cfg->ioctl_buf_sync);

	if (err != BCME_OK) {
		WL_ERR(("failed to set random generate MAC address\n"));
	} else {
		WL_ERR(("set mac " MACDBG " to " MACDBG "\n",
			MAC2STRDBG((const u8 *)bcmcfg_to_prmry_ndev(cfg)->dev_addr),
			MAC2STRDBG((const u8 *)&random_mac)));
		WL_ERR(("random MAC enable done"));
	}

	return err;
}

int
wl_cfg80211_random_mac_disable(struct net_device *dev)
{
	s32 err = BCME_ERROR;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	WL_ERR(("set original mac " MACDBG "\n",
		MAC2STRDBG((const u8 *)bcmcfg_to_prmry_ndev(cfg)->dev_addr)));

	err = wldev_iovar_setbuf_bsscfg(bcmcfg_to_prmry_ndev(cfg), "cur_etheraddr",
		bcmcfg_to_prmry_ndev(cfg)->dev_addr, ETH_ALEN,
		cfg->ioctl_buf, WLC_IOCTL_SMLEN, 0, &cfg->ioctl_buf_sync);

	if (err != BCME_OK) {
		WL_ERR(("failed to set original MAC address\n"));
	} else {
		WL_ERR(("random MAC disable done\n"));
	}

	return err;
}
#endif /* SUPPORT_RANDOM_MAC_SCAN */

#ifdef WLTDLS
static s32
wl_cfg80211_tdls_config(struct bcm_cfg80211 *cfg, enum wl_tdls_config state, bool auto_mode)
{
	struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);
	int err = 0;
	struct net_info *iter, *next;
	int update_reqd = 0;
	int enable = 0;
	dhd_pub_t *dhdp;
	dhdp = (dhd_pub_t *)(cfg->pub);

	/*
	 * TDLS need to be enabled only if we have a single STA/GC
	 * connection.
	 */

	WL_DBG(("Enter state:%d\n", state));

	if (!cfg->tdls_supported) {
		/* FW doesn't support tdls. Do nothing */
		return -ENODEV;
	}

	/* Protect tdls config session */
	mutex_lock(&cfg->tdls_sync);

	if ((state == TDLS_STATE_TEARDOWN)) {
		/* Host initiated TDLS tear down */
		err = dhd_tdls_enable(ndev, false, auto_mode, NULL);
		goto exit;
	} else if (state == TDLS_STATE_AP_CREATE) {
		/* We don't support tdls while AP/GO is operational */
		update_reqd = true;
		enable = false;
	} else if ((state == TDLS_STATE_CONNECT) || (state == TDLS_STATE_IF_CREATE)) {
		if (wl_get_drv_status_all(cfg,
			CONNECTED) >= TDLS_MAX_IFACE_FOR_ENABLE) {
			/* For STA/GC connect command request, disable
			 * tdls if we have any concurrent interfaces
			 * operational.
			 */
			WL_DBG(("Interface limit restriction. disable tdls.\n"));
			update_reqd = true;
			enable = false;
		}
	} else if ((state == TDLS_STATE_DISCONNECT) ||
		(state == TDLS_STATE_AP_DELETE) ||
		(state == TDLS_STATE_SETUP) ||
		(state == TDLS_STATE_IF_DELETE)) {
		/* Enable back the tdls connection only if we have less than
		 * or equal to a single STA/GC connection.
		 */
		if (wl_get_drv_status_all(cfg,
			CONNECTED) == 0) {
			/* If there are no interfaces connected, enable tdls */
			update_reqd = true;
			enable = true;
		} else if (wl_get_drv_status_all(cfg,
			CONNECTED) == TDLS_MAX_IFACE_FOR_ENABLE) {
			/* We have one interface in CONNECTED state.
			 * Verify whether its a non-AP interface before
			 * we enable back tdls.
			 */
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
			for_each_ndev(cfg, iter, next) {
				if ((iter->ndev) && (wl_get_drv_status(cfg, CONNECTED, ndev)) &&
					wl_get_mode_by_netdev(cfg, ndev) == WL_MODE_AP) {
					WL_DBG(("AP/GO operational. Can't enable tdls. \n"));
					err = -ENOTSUPP;
					goto exit;
				}
			}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
			/* No AP/GO found. Enable back tdls */
			update_reqd = true;
			enable = true;
		} else {
			WL_DBG(("Concurrent connection mode. Can't enable tdls. \n"));
			err = -ENOTSUPP;
			goto exit;
		}
	} else {
		WL_ERR(("Unknown tdls state:%d \n", state));
		err = -EINVAL;
		goto exit;
	}

	if (update_reqd == true) {
		if (dhdp->tdls_enable == enable) {
			WL_ERR(("No change in tdls state. Do nothing."
				" tdls_enable:%d\n", enable));
			goto exit;
		}
		err = wldev_iovar_setint(ndev, "tdls_enable", enable);
		if (unlikely(err)) {
			WL_ERR(("tdls_enable setting failed. err:%d\n", err));
			goto exit;
		} else {
			WL_DBG(("set tdls_enable: %d done\n", enable));
			/* Update the dhd state variable to be in sync */
			dhdp->tdls_enable = enable;
			if (state == TDLS_STATE_SETUP) {
				/* For host initiated setup, apply TDLS params
				 * Don't propagate errors up for param config
				 * failures
				 */
				dhd_tdls_enable(ndev, true, auto_mode, NULL);

			}
		}
	} else {
		WL_DBG(("Skip tdls config. state:%d update_reqd:%d "
			"current_status:%d \n",
			state, update_reqd, dhdp->tdls_enable));
	}

exit:
	mutex_unlock(&cfg->tdls_sync);

	return err;
}
#endif /* WLTDLS */

struct net_device* wl_get_ap_netdev(struct bcm_cfg80211 *cfg, char *ifname)
{
	struct net_info *iter, *next;
	struct net_device *ndev = NULL;

#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	for_each_ndev(cfg, iter, next) {
		if (iter->ndev) {
			if (strncmp(iter->ndev->name, ifname, strlen(iter->ndev->name)) == 0) {
				if (iter->ndev->ieee80211_ptr->iftype == NL80211_IFTYPE_AP) {
					ndev = iter->ndev;
					break;
				}
			}
		}
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

	return ndev;
}

struct net_device*
wl_get_netdev_by_name(struct bcm_cfg80211 *cfg, char *ifname)
{
	struct net_info *iter, *next;
	struct net_device *ndev = NULL;

#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	for_each_ndev(cfg, iter, next) {
		if (iter->ndev) {
			if (strncmp(iter->ndev->name, ifname, IFNAMSIZ) == 0) {
				ndev = iter->ndev;
				break;
			}
		}
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

	return ndev;
}

#ifdef SUPPORT_AP_HIGHER_BEACONRATE
#define WLC_RATE_FLAG	0x80
#define RATE_MASK		0x7f

int wl_set_ap_beacon_rate(struct net_device *dev, int val, char *ifname)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	dhd_pub_t *dhdp;
	wl_rateset_args_t rs;
	int error = BCME_ERROR, i;
	struct net_device *ndev = NULL;

	dhdp = (dhd_pub_t *)(cfg->pub);

	if (dhdp && !(dhdp->op_mode & DHD_FLAG_HOSTAP_MODE)) {
		WL_ERR(("Not Hostapd mode\n"));
		return BCME_NOTAP;
	}

	ndev = wl_get_ap_netdev(cfg, ifname);

	if (ndev == NULL) {
		WL_ERR(("No softAP interface named %s\n", ifname));
		return BCME_NOTAP;
	}

	bzero(&rs, sizeof(wl_rateset_args_t));
	error = wldev_iovar_getbuf(ndev, "rateset", NULL, 0,
		&rs, sizeof(wl_rateset_args_t), NULL);
	if (error < 0) {
		WL_ERR(("get rateset failed = %d\n", error));
		return error;
	}

	if (rs.count < 1) {
		WL_ERR(("Failed to get rate count\n"));
		return BCME_ERROR;
	}

	/* Host delivers target rate in the unit of 500kbps */
	/* To make it to 1mbps unit, atof should be implemented for 5.5mbps basic rate */
	for (i = 0; i < rs.count && i < WL_NUMRATES; i++)
		if (rs.rates[i] & WLC_RATE_FLAG)
			if ((rs.rates[i] & RATE_MASK) == val)
				break;

	/* Valid rate has been delivered as an argument */
	if (i < rs.count && i < WL_NUMRATES) {
		error = wldev_iovar_setint(ndev, "force_bcn_rspec", val);
		if (error < 0) {
			WL_ERR(("set beacon rate failed = %d\n", error));
			return BCME_ERROR;
		}
	} else {
		WL_ERR(("Rate is invalid"));
		return BCME_BADARG;
	}

	return BCME_OK;
}

int
wl_get_ap_basic_rate(struct net_device *dev, char* command, char *ifname, int total_len)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	dhd_pub_t *dhdp;
	wl_rateset_args_t rs;
	int error = BCME_ERROR;
	int i, bytes_written = 0;
	struct net_device *ndev = NULL;

	dhdp = (dhd_pub_t *)(cfg->pub);

	if (!(dhdp->op_mode & DHD_FLAG_HOSTAP_MODE)) {
		WL_ERR(("Not Hostapd mode\n"));
		return BCME_NOTAP;
	}

	ndev = wl_get_ap_netdev(cfg, ifname);

	if (ndev == NULL) {
		WL_ERR(("No softAP interface named %s\n", ifname));
		return BCME_NOTAP;
	}

	bzero(&rs, sizeof(wl_rateset_args_t));
	error = wldev_iovar_getbuf(ndev, "rateset", NULL, 0,
		&rs, sizeof(wl_rateset_args_t), NULL);
	if (error < 0) {
		WL_ERR(("get rateset failed = %d\n", error));
		return error;
	}

	if (rs.count < 1) {
		WL_ERR(("Failed to get rate count\n"));
		return BCME_ERROR;
	}

	/* Delivers basic rate in the unit of 500kbps to host */
	for (i = 0; i < rs.count && i < WL_NUMRATES; i++)
		if (rs.rates[i] & WLC_RATE_FLAG)
			bytes_written += snprintf(command + bytes_written, total_len,
							"%d ", rs.rates[i] & RATE_MASK);

	/* Remove last space in the command buffer */
	if (bytes_written) {
		command[bytes_written - 1] = '\0';
		bytes_written--;
	}

	return bytes_written;

}
#endif /* SUPPORT_AP_HIGHER_BEACONRATE */

#ifdef SUPPORT_AP_RADIO_PWRSAVE
static int
_wl_update_ap_rps_params(struct net_device *dev)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	rpsnoa_iovar_params_t iovar;
	u8 smbuf[WLC_IOCTL_SMLEN];

	if (!dev)
		return BCME_BADARG;

	memset(&iovar, 0, sizeof(iovar));
	memset(smbuf, 0, sizeof(smbuf));

	iovar.hdr.ver = RADIO_PWRSAVE_VERSION;
	iovar.hdr.subcmd = WL_RPSNOA_CMD_PARAMS;
	iovar.hdr.len = sizeof(iovar);
	iovar.param->band = WLC_BAND_ALL;
	iovar.param->level = cfg->ap_rps_info.level;
	iovar.param->stas_assoc_check = cfg->ap_rps_info.sta_assoc_check;
	iovar.param->pps = cfg->ap_rps_info.pps;
	iovar.param->quiet_time = cfg->ap_rps_info.quiet_time;

	if (wldev_iovar_setbuf(dev, "rpsnoa", &iovar, sizeof(iovar),
		smbuf, sizeof(smbuf), NULL)) {
		WL_ERR(("Failed to set rpsnoa params"));
		return BCME_ERROR;
	}

	return BCME_OK;
}

int
wl_get_ap_rps(struct net_device *dev, char* command, char *ifname, int total_len)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	dhd_pub_t *dhdp;
	int error = BCME_ERROR;
	int bytes_written = 0;
	struct net_device *ndev = NULL;
	rpsnoa_iovar_t iovar;
	u8 smbuf[WLC_IOCTL_SMLEN];
	u32 chanspec = 0;
	u8 idx = 0;
	u8 val;

	dhdp = (dhd_pub_t *)(cfg->pub);

	if (!dhdp) {
		error = BCME_NOTUP;
		goto fail;
	}

	if (!(dhdp->op_mode & DHD_FLAG_HOSTAP_MODE)) {
		WL_ERR(("Not Hostapd mode\n"));
		error = BCME_NOTAP;
		goto fail;
	}

	ndev = wl_get_ap_netdev(cfg, ifname);

	if (ndev == NULL) {
		WL_ERR(("No softAP interface named %s\n", ifname));
		error = BCME_NOTAP;
		goto fail;
	}

	memset(&iovar, 0, sizeof(iovar));
	memset(smbuf, 0, sizeof(smbuf));

	iovar.hdr.ver = RADIO_PWRSAVE_VERSION;
	iovar.hdr.subcmd = WL_RPSNOA_CMD_STATUS;
	iovar.hdr.len = sizeof(iovar);
	iovar.data->band = WLC_BAND_ALL;

	error = wldev_iovar_getbuf(ndev, "rpsnoa", &iovar, sizeof(iovar),
		smbuf, sizeof(smbuf), NULL);
	if (error < 0) {
		WL_ERR(("get ap radio pwrsave failed = %d\n", error));
		goto fail;
	}

	/* RSDB event doesn't seem to be handled correctly.
	 * So check chanspec of AP directly from the firmware
	 */
	error = wldev_iovar_getint(ndev, "chanspec", (s32 *)&chanspec);
	if (error < 0) {
		WL_ERR(("get chanspec from AP failed = %d\n", error));
		goto fail;
	}

	chanspec = wl_chspec_driver_to_host(chanspec);
	if (CHSPEC_IS2G(chanspec))
		idx = 0;
	else if (CHSPEC_IS5G(chanspec))
		idx = 1;
	else {
		error = BCME_BADCHAN;
		goto fail;
	}

	val = ((rpsnoa_iovar_t *)smbuf)->data[idx].value;
	bytes_written += snprintf(command + bytes_written, total_len, "%d", val);
	error = bytes_written;

fail:
	return error;
}

int
wl_set_ap_rps(struct net_device *dev, bool enable, char *ifname)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	dhd_pub_t *dhdp;
	struct net_device *ndev = NULL;
	rpsnoa_iovar_t iovar;
	u8 smbuf[WLC_IOCTL_SMLEN];
	int ret = BCME_OK;

	dhdp = (dhd_pub_t *)(cfg->pub);

	if (!dhdp) {
		ret = BCME_NOTUP;
		goto exit;
	}

	if (!(dhdp->op_mode & DHD_FLAG_HOSTAP_MODE)) {
		WL_ERR(("Not Hostapd mode\n"));
		ret = BCME_NOTAP;
		goto exit;
	}

	ndev = wl_get_ap_netdev(cfg, ifname);

	if (ndev == NULL) {
		WL_ERR(("No softAP interface named %s\n", ifname));
		ret = BCME_NOTAP;
		goto exit;
	}

	if (cfg->ap_rps_info.enable != enable) {
		cfg->ap_rps_info.enable = enable;
		if (enable) {
			ret = _wl_update_ap_rps_params(ndev);
			if (ret) {
				WL_ERR(("Filed to update rpsnoa params\n"));
				goto exit;
			}
		}
		memset(&iovar, 0, sizeof(iovar));
		memset(smbuf, 0, sizeof(smbuf));

		iovar.hdr.ver = RADIO_PWRSAVE_VERSION;
		iovar.hdr.subcmd = WL_RPSNOA_CMD_ENABLE;
		iovar.hdr.len = sizeof(iovar);
		iovar.data->band = WLC_BAND_ALL;
		iovar.data->value = (int16)enable;

		ret = wldev_iovar_setbuf(ndev, "rpsnoa", &iovar, sizeof(iovar),
			smbuf, sizeof(smbuf), NULL);
		if (ret) {
			WL_ERR(("Failed to enable AP radio power save"));
			goto exit;
		}
		cfg->ap_rps_info.enable = enable;
	}
exit:
	return ret;
}

int
wl_update_ap_rps_params(struct net_device *dev, ap_rps_info_t* rps, char *ifname)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	dhd_pub_t *dhdp;
	struct net_device *ndev = NULL;

	dhdp = (dhd_pub_t *)(cfg->pub);

	if (!dhdp)
		return BCME_NOTUP;

	if (!(dhdp->op_mode & DHD_FLAG_HOSTAP_MODE)) {
		WL_ERR(("Not Hostapd mode\n"));
		return BCME_NOTAP;
	}

	ndev = wl_get_ap_netdev(cfg, ifname);

	if (ndev == NULL) {
		WL_ERR(("No softAP interface named %s\n", ifname));
		return BCME_NOTAP;
	}

	if (!rps)
		return BCME_BADARG;

	if (rps->pps < RADIO_PWRSAVE_PPS_MIN)
		return BCME_BADARG;

	if (rps->level < RADIO_PWRSAVE_LEVEL_MIN ||
		rps->level > RADIO_PWRSAVE_LEVEL_MAX)
		return BCME_BADARG;

	if (rps->quiet_time < RADIO_PWRSAVE_QUIETTIME_MIN)
		return BCME_BADARG;

	if (rps->sta_assoc_check > RADIO_PWRSAVE_ASSOCCHECK_MAX ||
		rps->sta_assoc_check < RADIO_PWRSAVE_ASSOCCHECK_MIN)
		return BCME_BADARG;

	cfg->ap_rps_info.pps = rps->pps;
	cfg->ap_rps_info.level = rps->level;
	cfg->ap_rps_info.quiet_time = rps->quiet_time;
	cfg->ap_rps_info.sta_assoc_check = rps->sta_assoc_check;

	if (cfg->ap_rps_info.enable) {
		if (_wl_update_ap_rps_params(ndev)) {
			WL_ERR(("Failed to update rpsnoa params"));
			return BCME_ERROR;
		}
	}

	return BCME_OK;
}

void
wl_cfg80211_init_ap_rps(struct bcm_cfg80211 *cfg)
{
	cfg->ap_rps_info.enable = FALSE;
	cfg->ap_rps_info.sta_assoc_check = RADIO_PWRSAVE_STAS_ASSOC_CHECK;
	cfg->ap_rps_info.pps = RADIO_PWRSAVE_PPS;
	cfg->ap_rps_info.quiet_time = RADIO_PWRSAVE_QUIET_TIME;
	cfg->ap_rps_info.level = RADIO_PWRSAVE_LEVEL;
}
#endif /* SUPPORT_AP_RADIO_PWRSAVE */

int
wl_cfg80211_iface_count(struct net_device *dev)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	struct net_info *iter, *next;
	int iface_count = 0;

	/* Return the count of network interfaces (skip netless p2p discovery
	 * interface)
	 */
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
	for_each_ndev(cfg, iter, next) {
		if (iter->ndev) {
			iface_count++;
		}
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
	return iface_count;
}

#ifdef WBTEXT
static bool wl_cfg80211_wbtext_check_bssid_list(struct bcm_cfg80211 *cfg, struct ether_addr *ea)
{
	wl_wbtext_bssid_t *bssid = NULL;
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif

	/* check duplicate */
	list_for_each_entry(bssid, &cfg->wbtext_bssid_list, list) {
		if (!memcmp(bssid->ea.octet, ea, ETHER_ADDR_LEN)) {
			return FALSE;
		}
	}

	return TRUE;
}

static bool wl_cfg80211_wbtext_add_bssid_list(struct bcm_cfg80211 *cfg, struct ether_addr *ea)
{
	wl_wbtext_bssid_t *bssid = NULL;
	char eabuf[ETHER_ADDR_STR_LEN];

	bssid = kmalloc(sizeof(wl_wbtext_bssid_t), GFP_KERNEL);
	if (bssid == NULL) {
		WL_ERR(("alloc failed\n"));
		return FALSE;
	}

	memcpy(bssid->ea.octet, ea, ETHER_ADDR_LEN);

	INIT_LIST_HEAD(&bssid->list);
	list_add_tail(&bssid->list, &cfg->wbtext_bssid_list);

	WL_DBG(("add wbtext bssid : %s\n", bcm_ether_ntoa(ea, eabuf)));

	return TRUE;
}

static void wl_cfg80211_wbtext_clear_bssid_list(struct bcm_cfg80211 *cfg)
{
	wl_wbtext_bssid_t *bssid = NULL;
	char eabuf[ETHER_ADDR_STR_LEN];

	while (!list_empty(&cfg->wbtext_bssid_list)) {
		bssid = list_entry(cfg->wbtext_bssid_list.next, wl_wbtext_bssid_t, list);
		if (bssid) {
			WL_DBG(("clear wbtext bssid : %s\n", bcm_ether_ntoa(&bssid->ea, eabuf)));
			list_del(&bssid->list);
			kfree(bssid);
		}
	}
}

static void wl_cfg80211_wbtext_update_rcc(struct bcm_cfg80211 *cfg, struct net_device *dev)
{
	struct wl_connect_info *conn_info = wl_to_conn(cfg);
	bcm_tlv_t * cap_ie = NULL;
	bool req_sent = FALSE;
	struct wl_profile *profile;

	WL_DBG(("Enter\n"));

	profile = wl_get_profile_by_netdev(cfg, dev);
	if (!profile) {
		WL_ERR(("no profile exists\n"));
		return;
	}

	if (wl_cfg80211_wbtext_check_bssid_list(cfg,
			(struct ether_addr *)&profile->bssid) == FALSE) {
		WL_DBG(("already updated\n"));
		return;
	}

	/* first, check NBR bit in RRM IE */
	if ((cap_ie = bcm_parse_tlvs(conn_info->resp_ie, conn_info->resp_ie_len,
			DOT11_MNG_RRM_CAP_ID)) != NULL) {
		if (isset(cap_ie->data, DOT11_RRM_CAP_NEIGHBOR_REPORT)) {
			req_sent = wl_cfg80211_wbtext_send_nbr_req(cfg, dev, profile);
		}
	}

	/* if RRM nbr was not supported, check BTM bit in extend cap. IE */
	if (!req_sent) {
		if ((cap_ie = bcm_parse_tlvs(conn_info->resp_ie, conn_info->resp_ie_len,
				DOT11_MNG_EXT_CAP_ID)) != NULL) {
			if (cap_ie->len >= DOT11_EXTCAP_LEN_BSSTRANS &&
					isset(cap_ie->data, DOT11_EXT_CAP_BSSTRANS_MGMT)) {
				wl_cfg80211_wbtext_send_btm_query(cfg, dev, profile);
			}
		}
	}
}

static bool wl_cfg80211_wbtext_send_nbr_req(struct bcm_cfg80211 *cfg, struct net_device *dev,
	struct wl_profile *profile)
{
	int error = -1;
	char *smbuf = NULL;
	struct wl_connect_info *conn_info = wl_to_conn(cfg);
	bcm_tlv_t * rrm_cap_ie = NULL;
	wlc_ssid_t *ssid = NULL;
	bool ret = FALSE;

	WL_DBG(("Enter\n"));

	/* check RRM nbr bit in extend cap. IE of assoc response */
	if ((rrm_cap_ie = bcm_parse_tlvs(conn_info->resp_ie, conn_info->resp_ie_len,
			DOT11_MNG_RRM_CAP_ID)) != NULL) {
		if (!isset(rrm_cap_ie->data, DOT11_RRM_CAP_NEIGHBOR_REPORT)) {
			WL_DBG(("AP doesn't support neighbor report\n"));
			return FALSE;
		}
	}

	smbuf = (char *) kmalloc(WLC_IOCTL_MAXLEN, GFP_KERNEL);
	if (smbuf == NULL) {
		WL_ERR(("failed to allocated memory\n"));
		goto nbr_req_out;
	}

	ssid = (wlc_ssid_t *) kzalloc(sizeof(wlc_ssid_t), GFP_KERNEL);
	if (ssid == NULL) {
		WL_ERR(("failed to allocated memory\n"));
		goto nbr_req_out;
	}

	ssid->SSID_len = MIN(profile->ssid.SSID_len, DOT11_MAX_SSID_LEN);
	memcpy(ssid->SSID, profile->ssid.SSID, ssid->SSID_len);

	error = wldev_iovar_setbuf(dev, "rrm_nbr_req", ssid,
		sizeof(wlc_ssid_t), smbuf, WLC_IOCTL_MAXLEN, NULL);
	if (error == BCME_OK) {
		ret = wl_cfg80211_wbtext_add_bssid_list(cfg,
			(struct ether_addr *)&profile->bssid);
	} else {
		WL_ERR(("failed to send neighbor report request, error=%d\n", error));
	}

nbr_req_out:
	if (ssid) {
		kfree(ssid);
	}

	if (smbuf) {
		kfree(smbuf);
	}
	return ret;
}

static bool wl_cfg80211_wbtext_send_btm_query(struct bcm_cfg80211 *cfg, struct net_device *dev,
	struct wl_profile *profile)

{
	int error = -1;
	bool ret = FALSE;

	WL_DBG(("Enter\n"));

	error = wldev_iovar_setbuf(dev, "wnm_bsstrans_query", NULL,
		0, cfg->ioctl_buf, WLC_IOCTL_SMLEN, &cfg->ioctl_buf_sync);
	if (error == BCME_OK) {
		ret = wl_cfg80211_wbtext_add_bssid_list(cfg,
			(struct ether_addr *)&profile->bssid);
	} else {
		WL_ERR(("%s: failed to set BTM query, error=%d\n", __FUNCTION__, error));
	}
	return ret;
}

static void wl_cfg80211_wbtext_set_wnm_maxidle(struct bcm_cfg80211 *cfg, struct net_device *dev)
{
	keepalives_max_idle_t keepalive = {0, 0, 0, 0};
	s32 bssidx, error;
	int wnm_maxidle = 0;
	struct wl_connect_info *conn_info = wl_to_conn(cfg);

	/* AP supports wnm max idle ? */
	if (bcm_parse_tlvs(conn_info->resp_ie, conn_info->resp_ie_len,
			DOT11_MNG_BSS_MAX_IDLE_PERIOD_ID) != NULL) {
		error = wldev_iovar_getint(dev, "wnm_maxidle", &wnm_maxidle);
		if (error < 0) {
			WL_ERR(("failed to get wnm max idle period : %d\n", error));
		}
	}

	WL_DBG(("wnm max idle period : %d\n", wnm_maxidle));

	/* if wnm maxidle has valid period, set it as keep alive */
	if (wnm_maxidle > 0) {
		keepalive.keepalive_count = 1;
	}

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) >= 0) {
		error = wldev_iovar_setbuf_bsscfg(dev, "wnm_keepalives_max_idle", &keepalive,
			sizeof(keepalives_max_idle_t), cfg->ioctl_buf, WLC_IOCTL_SMLEN,
			bssidx, &cfg->ioctl_buf_sync);
		if (error < 0) {
			WL_ERR(("set wnm_keepalives_max_idle failed : %d\n", error));
		}
	}
}

static int
wl_cfg80211_recv_nbr_resp(struct net_device *dev, uint8 *body, int body_len)
{
	dot11_rm_action_t *rm_rep;
	bcm_tlv_t *tlvs;
	int tlv_len, i, error;
	dot11_neighbor_rep_ie_t *nbr_rep_ie;
	chanspec_t ch;
	wl_roam_channel_list_t channel_list;
	char iobuf[WLC_IOCTL_SMLEN];

	if (body_len < DOT11_RM_ACTION_LEN) {
		WL_ERR(("Received Neighbor Report frame with incorrect length %d\n",
			body_len));
		return BCME_ERROR;
	}

	rm_rep = (dot11_rm_action_t *)body;
	WL_DBG(("received neighbor report (token = %d)\n", rm_rep->token));

	tlvs = (bcm_tlv_t *)&rm_rep->data[0];

	tlv_len = body_len - DOT11_RM_ACTION_LEN;

	while (tlvs && tlvs->id == DOT11_MNG_NEIGHBOR_REP_ID) {
		nbr_rep_ie = (dot11_neighbor_rep_ie_t *)tlvs;

		if (nbr_rep_ie->len < DOT11_NEIGHBOR_REP_IE_FIXED_LEN) {
			WL_ERR(("malformed Neighbor Report element with length %d\n",
				nbr_rep_ie->len));
			tlvs = bcm_next_tlv(tlvs, &tlv_len);
			continue;
		}

		ch = CH20MHZ_CHSPEC(nbr_rep_ie->channel);
		WL_DBG(("ch:%d, bssid:%02x:%02x:%02x:%02x:%02x:%02x\n",
			ch, nbr_rep_ie->bssid.octet[0], nbr_rep_ie->bssid.octet[1],
			nbr_rep_ie->bssid.octet[2],	nbr_rep_ie->bssid.octet[3],
			nbr_rep_ie->bssid.octet[4],	nbr_rep_ie->bssid.octet[5]));

		/* get RCC list */
		error = wldev_iovar_getbuf(dev, "roamscan_channels", 0, 0,
			(void *)&channel_list, sizeof(channel_list), NULL);
		if (error) {
			WL_ERR(("Failed to get roamscan channels, error = %d\n", error));
			return BCME_ERROR;
		}

		/* update RCC */
		if (channel_list.n < MAX_ROAM_CHANNEL) {
			for (i = 0; i < channel_list.n; i++) {
				if (channel_list.channels[i] == ch) {
					break;
				}
			}
			if (i == channel_list.n) {
				channel_list.channels[channel_list.n] = ch;
				channel_list.n++;
			}
		}

		/* set RCC list */
		error = wldev_iovar_setbuf(dev, "roamscan_channels", &channel_list,
			sizeof(channel_list), iobuf, sizeof(iobuf), NULL);
		if (error) {
			WL_DBG(("Failed to set roamscan channels, error = %d\n", error));
		}

		tlvs = bcm_next_tlv(tlvs, &tlv_len);
	}

	return BCME_OK;
}
#endif /* WBTEXT */

#ifdef SUPPORT_SET_CAC
static void
wl_cfg80211_set_cac(struct bcm_cfg80211 *cfg, int enable)
{
	int ret = 0;
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);

	WL_DBG(("cac enable %d, op_mode 0x%04x\n", enable, dhd->op_mode));
	if (!dhd) {
		WL_ERR(("dhd is NULL\n"));
		return;
	}
	if (enable && ((dhd->op_mode & DHD_FLAG_HOSTAP_MODE) ||
			(dhd->op_mode & DHD_FLAG_P2P_GC_MODE) ||
			(dhd->op_mode & DHD_FLAG_P2P_GO_MODE))) {
		WL_ERR(("op_mode 0x%04x\n", dhd->op_mode));
		enable = 0;
	}
	if ((ret = dhd_wl_ioctl_set_intiovar(dhd, "cac", enable,
			WLC_SET_VAR, TRUE, 0)) < 0) {
		WL_ERR(("Failed set CAC, ret=%d\n", ret));
	} else {
		WL_DBG(("CAC set successfully\n"));
	}
	return;
}
#endif /* SUPPORT_SET_CAC */

#ifdef SUPPORT_RSSI_LOGGING
int
wl_get_rssi_per_ant(struct net_device *dev, char *ifname, char *peer_mac, void *param)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	wl_rssi_ant_mimo_t *get_param = (wl_rssi_ant_mimo_t *)param;
	rssi_ant_param_t *set_param = NULL;
	struct net_device *ifdev = NULL;
	char iobuf[WLC_IOCTL_SMLEN];
	int err = BCME_OK;
	int iftype = 0;

	memset(iobuf, 0, WLC_IOCTL_SMLEN);

	/* Check the interface type */
	ifdev = wl_get_netdev_by_name(cfg, ifname);
	if (ifdev == NULL) {
		WL_ERR(("Could not find net_device for ifname:%s\n", ifname));
		err = BCME_BADARG;
		goto fail;
	}

	iftype = ifdev->ieee80211_ptr->iftype;
	if (iftype == NL80211_IFTYPE_AP || iftype == NL80211_IFTYPE_P2P_GO) {
		if (peer_mac) {
			set_param = (rssi_ant_param_t *)kzalloc(sizeof(rssi_ant_param_t),
				GFP_KERNEL);
			err = wl_cfg80211_ether_atoe(peer_mac, &set_param->ea);
			if (!err) {
				WL_ERR(("Invalid Peer MAC format\n"));
				err = BCME_BADARG;
				goto fail;
			}
		} else {
			WL_ERR(("Peer MAC is not provided for iftype %d\n", iftype));
			err = BCME_BADARG;
			goto fail;
		}
	}

	err = wldev_iovar_getbuf(ifdev, "phy_rssi_ant", peer_mac ?
		(void *)&(set_param->ea) : NULL, peer_mac ? ETHER_ADDR_LEN : 0,
		(void *)iobuf, sizeof(iobuf), NULL);
	if (unlikely(err)) {
		WL_ERR(("Failed to get rssi info, err=%d\n", err));
	} else {
		memcpy(get_param, iobuf, sizeof(wl_rssi_ant_mimo_t));
		if (get_param->count == 0) {
			WL_ERR(("Not supported on this chip\n"));
			err = BCME_UNSUPPORTED;
		}
	}

fail:
	if (set_param) {
		kfree(set_param);
	}

	return err;
}

int
wl_get_rssi_logging(struct net_device *dev, void *param)
{
	rssilog_get_param_t *get_param = (rssilog_get_param_t *)param;
	char iobuf[WLC_IOCTL_SMLEN];
	int err = BCME_OK;

	memset(iobuf, 0, WLC_IOCTL_SMLEN);
	memset(get_param, 0, sizeof(*get_param));
	err = wldev_iovar_getbuf(dev, "rssilog", NULL, 0, (void *)iobuf,
		sizeof(iobuf), NULL);
	if (err) {
		WL_ERR(("Failed to get rssi logging info, err=%d\n", err));
	} else {
		memcpy(get_param, iobuf, sizeof(*get_param));
	}

	return err;
}

int
wl_set_rssi_logging(struct net_device *dev, void *param)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	rssilog_set_param_t *set_param = (rssilog_set_param_t *)param;
	int err;

	err = wldev_iovar_setbuf(dev, "rssilog", set_param,
		sizeof(*set_param), cfg->ioctl_buf, WLC_IOCTL_SMLEN,
		&cfg->ioctl_buf_sync);
	if (err) {
		WL_ERR(("Failed to set rssi logging param, err=%d\n", err));
	}

	return err;
}
#endif /* SUPPORT_RSSI_LOGGING */

s32
wl_cfg80211_autochannel(struct net_device *dev, char* command, int total_len)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	int ret = 0;
	int bytes_written = -1;

	sscanf(command, "%*s %d", &cfg->autochannel);

	if (cfg->autochannel == 0) {
		cfg->best_2g_ch = 0;
		cfg->best_5g_ch = 0;
	} else if (cfg->autochannel == 2) {
		bytes_written = snprintf(command, total_len, "2g=%d 5g=%d",
			cfg->best_2g_ch, cfg->best_5g_ch);
		WL_TRACE(("%s: command result is %s\n", __FUNCTION__, command));
		ret = bytes_written;
	}

	return ret;
}

static int
wl_cfg80211_check_in4way(struct bcm_cfg80211 *cfg,
	struct net_device *dev, uint action, enum wl_ext_status status, void *context)
{
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
	struct wl_security *sec;
	s32 bssidx = -1;
	int ret = 0, cur_eapol_status, ifidx;
	int max_wait_time, max_wait_cnt;
	int suppressed = 0;

	mutex_lock(&cfg->in4way_sync);
	action = action & dhdp->conf->in4way;
	WL_DBG(("status=%d, action=0x%x, in4way=0x%x\n", status, action, dhdp->conf->in4way));

	cur_eapol_status = dhdp->conf->eapol_status;
	switch (status) {
		case WL_EXT_STATUS_SCAN:
			wldev_ioctl(dev, WLC_GET_SCANSUPPRESS, &suppressed, sizeof(int), false);
			if (suppressed) {
				WL_ERR(("scan suppressed\n"));
				ret = -EBUSY;
				break;
			}
			if (action & NO_SCAN_IN4WAY) {
				if (cfg->handshaking > 0 && cfg->handshaking <= 3) {
					WL_ERR(("return -EBUSY cnt %d\n", cfg->handshaking));
					cfg->handshaking++;
					ret = -EBUSY;
					break;
				}
			}
			break;
 		case WL_EXT_STATUS_DISCONNECTING:
			if (cur_eapol_status >= EAPOL_STATUS_4WAY_START &&
					cur_eapol_status < EAPOL_STATUS_4WAY_DONE) {
				WL_ERR(("WPA failed at %d\n", cur_eapol_status));
				dhdp->conf->eapol_status = EAPOL_STATUS_NONE;
			} else if (cur_eapol_status >= EAPOL_STATUS_WSC_START &&
					cur_eapol_status < EAPOL_STATUS_WSC_DONE) {
				WL_ERR(("WPS failed at %d\n", cur_eapol_status));
				dhdp->conf->eapol_status = EAPOL_STATUS_NONE;
			}
			if (action & (NO_SCAN_IN4WAY|NO_BTC_IN4WAY)) {
				if (cfg->handshaking) {
					if ((action & NO_BTC_IN4WAY) && cfg->btc_mode) {
						WL_TRACE(("status=%d, restore btc_mode %d\n",
							status, cfg->btc_mode));
						wldev_iovar_setint(dev, "btc_mode", cfg->btc_mode);
					}
					cfg->handshaking = 0;
				}
			}
			if (action & WAIT_DISCONNECTED) {
				max_wait_time = 200;
				max_wait_cnt = 20;
				cfg->disconnected_jiffies = jiffies;
				while (!time_after(jiffies,
						cfg->disconnected_jiffies + msecs_to_jiffies(max_wait_time)) &&
						max_wait_cnt) {
					WL_TRACE(("status=%d, max_wait_cnt=%d waiting...\n",
						status, max_wait_cnt));
					mutex_unlock(&cfg->in4way_sync);
					OSL_SLEEP(50);
					mutex_lock(&cfg->in4way_sync);
					max_wait_cnt--;
				}
				wake_up_interruptible(&dhdp->conf->event_complete);
			}
			break;
		case WL_EXT_STATUS_CONNECTING:
			if (action & (NO_SCAN_IN4WAY|NO_BTC_IN4WAY)) {
				bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr);
				sec = wl_read_prof(cfg, dev, WL_PROF_SEC);
				if ((sec->wpa_versions & (NL80211_WPA_VERSION_1 | NL80211_WPA_VERSION_2)) &&
						bssidx == 0) {
					dhdp->conf->eapol_status = EAPOL_STATUS_4WAY_START;
					cfg->handshaking = 1;
					if (action & NO_BTC_IN4WAY) {
						ret = wldev_iovar_getint(dev, "btc_mode", &cfg->btc_mode);
						if (!ret && cfg->btc_mode) {
							WL_TRACE(("status=%d, disable current btc_mode %d\n",
								status, cfg->btc_mode));
							wldev_iovar_setint(dev, "btc_mode", 0);
						}
					}
				}
			}
			if (action & WAIT_DISCONNECTED) {
				max_wait_time = 200;
				max_wait_cnt = 10;
				while (!time_after(jiffies,
						cfg->disconnected_jiffies + msecs_to_jiffies(max_wait_time)) &&
						max_wait_cnt) {
					WL_TRACE(("status=%d, max_wait_cnt=%d waiting...\n",
						status, max_wait_cnt));
					mutex_unlock(&cfg->in4way_sync);
					OSL_SLEEP(50);
					mutex_lock(&cfg->in4way_sync);
					max_wait_cnt--;
				}
				wake_up_interruptible(&dhdp->conf->event_complete);
			}
			break;
		case WL_EXT_STATUS_CONNECTED:
			ifidx = dhd_net2idx(dhdp->info, dev);
			if (dev->ieee80211_ptr->iftype == NL80211_IFTYPE_STATION && ifidx >= 0) {
				dhd_conf_set_wme(cfg->pub, ifidx, 0);
				wake_up_interruptible(&dhdp->conf->event_complete);
			}
			else if (dev->ieee80211_ptr->iftype == NL80211_IFTYPE_P2P_CLIENT) {
				dhd_conf_set_mchan_bw(cfg->pub, WL_P2P_IF_CLIENT, -1);
			}
			break;
		case WL_EXT_STATUS_DISCONNECTED:
			if (cur_eapol_status >= EAPOL_STATUS_4WAY_START &&
					cur_eapol_status < EAPOL_STATUS_4WAY_DONE) {
				WL_ERR(("WPA failed at %d\n", cur_eapol_status));
				dhdp->conf->eapol_status = EAPOL_STATUS_NONE;
			} else if (cur_eapol_status >= EAPOL_STATUS_WSC_START &&
					cur_eapol_status < EAPOL_STATUS_WSC_DONE) {
				WL_ERR(("WPS failed at %d\n", cur_eapol_status));
				dhdp->conf->eapol_status = EAPOL_STATUS_NONE;
			}
			if (action & (NO_SCAN_IN4WAY|NO_BTC_IN4WAY)) {
				if (cfg->handshaking) {
					if ((action & NO_BTC_IN4WAY) && cfg->btc_mode) {
						WL_TRACE(("status=%d, restore btc_mode %d\n",
							status, cfg->btc_mode));
						wldev_iovar_setint(dev, "btc_mode", cfg->btc_mode);
					}
					cfg->handshaking = 0;
				}
			}
			if (action & WAIT_DISCONNECTED) {
				cfg->disconnected_jiffies = jiffies;
			}
			wake_up_interruptible(&dhdp->conf->event_complete);
			break;
		case WL_EXT_STATUS_ADD_KEY:
			dhdp->conf->eapol_status = EAPOL_STATUS_4WAY_DONE;
			if (action & (NO_SCAN_IN4WAY|NO_BTC_IN4WAY)) {
				if (cfg->handshaking) {
					if ((action & NO_BTC_IN4WAY) && cfg->btc_mode) {
						WL_TRACE(("status=%d, restore btc_mode %d\n",
							status, cfg->btc_mode));
						wldev_iovar_setint(dev, "btc_mode", cfg->btc_mode);
					}
					cfg->handshaking = 0;
				}
			}
			wake_up_interruptible(&dhdp->conf->event_complete);
			break;
		case WL_EXT_STATUS_AP_ENABLED:
			ifidx = dhd_net2idx(dhdp->info, dev);
			if (dev->ieee80211_ptr->iftype == NL80211_IFTYPE_AP && ifidx >= 0) {
				dhd_conf_set_wme(cfg->pub, ifidx, 1);
			}
			else if (dev->ieee80211_ptr->iftype == NL80211_IFTYPE_P2P_GO) {
				dhd_conf_set_mchan_bw(cfg->pub, WL_P2P_IF_GO, -1);
			}
			break;
		case WL_EXT_STATUS_DELETE_STA:
			if ((action & DONT_DELETE_GC_AFTER_WPS) &&
					(dev->ieee80211_ptr->iftype == NL80211_IFTYPE_P2P_GO)) {
				u8* mac_addr = context;
				if (mac_addr && memcmp(&ether_bcast, mac_addr, ETHER_ADDR_LEN) &&
						cur_eapol_status == EAPOL_STATUS_WSC_DONE) {
					u32 timeout;
					max_wait_time = 300;
					WL_TRACE(("status=%d, wps_done=%d, waiting %dms ...\n",
						status, cfg->wps_done, max_wait_time));
					mutex_unlock(&cfg->in4way_sync);
					timeout = wait_event_interruptible_timeout(cfg->wps_done_event,
						cfg->wps_done, msecs_to_jiffies(max_wait_time));
					mutex_lock(&cfg->in4way_sync);
					WL_TRACE(("status=%d, wps_done=%d, timeout=%d\n",
						status, cfg->wps_done, timeout));
					if (timeout > 0) {
						ret = -1;
						break;
					}
				} else {
					WL_TRACE(("status=%d, wps_done=%d => 0\n", status, cfg->wps_done));
					cfg->wps_done = FALSE;
					dhdp->conf->eapol_status = EAPOL_STATUS_NONE;
				}
			}
			break;
		case WL_EXT_STATUS_STA_DISCONNECTED:
			if ((action & DONT_DELETE_GC_AFTER_WPS) &&
					(dev->ieee80211_ptr->iftype == NL80211_IFTYPE_P2P_GO) &&
					cur_eapol_status == EAPOL_STATUS_WSC_DONE) {
				WL_TRACE(("status=%d, wps_done=%d => 0\n", status, cfg->wps_done));
				cfg->wps_done = FALSE;
			}
			break;
		case WL_EXT_STATUS_STA_CONNECTED:
			if ((action & DONT_DELETE_GC_AFTER_WPS) &&
					(dev->ieee80211_ptr->iftype == NL80211_IFTYPE_P2P_GO) &&
					cur_eapol_status == EAPOL_STATUS_WSC_DONE) {
				WL_TRACE(("status=%d, wps_done=%d => 1\n", status, cfg->wps_done));
				cfg->wps_done = TRUE;
				wake_up_interruptible(&cfg->wps_done_event);
			}
			break;
		default:
			WL_ERR(("Unknown action=0x%x, status=%d\n", action, status));
	}

	mutex_unlock(&cfg->in4way_sync);

	return ret;
}
