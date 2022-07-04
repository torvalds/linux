/*
 * Linux cfg80211 driver
 *
 * Copyright (C) 2020, Broadcom.
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
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 */
/* */
#include <typedefs.h>
#include <linuxver.h>
#include <linux/kernel.h>

#include <bcmutils.h>
#include <bcmstdlib_s.h>
#include <bcmwifi_channels.h>
#include <bcmendian.h>
#include <ethernet.h>
#ifdef WL_WPS_SYNC
#include <eapol.h>
#endif /* WL_WPS_SYNC */
#include <802.11.h>
#include <bcmiov.h>
#include <linux/if_arp.h>
#include <asm/uaccess.h>

#include <ethernet.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/netdevice.h>
#include <linux/sched.h>
#include <linux/etherdevice.h>
#include <linux/wireless.h>
#include <linux/ieee80211.h>
#include <linux/wait.h>
#if defined(CONFIG_TIZEN)
#include <linux/net_stat_tizen.h>
#endif /* CONFIG_TIZEN */
#include <net/cfg80211.h>
#include <net/rtnetlink.h>

#include <wlioctl.h>
#include <bcmevent.h>
#include <wldev_common.h>
#include <wl_cfg80211.h>
#include <wl_cfgp2p.h>
#include <wl_cfgscan.h>
#include <wl_cfgvif.h>
#include <bcmdevs.h>
#include <bcmdevs_legacy.h>
#ifdef WL_FILS
#include <fils.h>
#include <frag.h>
#endif /* WL_FILS */

#ifdef OEM_ANDROID
#include <wl_android.h>
#endif

#if defined(BCMDONGLEHOST)
#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_linux.h>
#include <dhd_linux_pktdump.h>
#include <dhd_debug.h>
#include <dhdioctl.h>
#include <wlioctl.h>
#include <dhd_cfg80211.h>
#include <dhd_bus.h>
#ifdef PNO_SUPPORT
#include <dhd_pno.h>
#endif /* PNO_SUPPORT */
#include <wl_cfgvendor.h>
#endif /* defined(BCMDONGLEHOST) */

#ifdef CONFIG_SLEEP_MONITOR
#include <linux/power/sleep_monitor.h>
#endif

#if !defined(WL_VENDOR_EXT_SUPPORT)
#undef GSCAN_SUPPORT
#endif
#include <dhd_config.h>

#ifdef WL_NAN
#include <wl_cfgnan.h>
#endif /* WL_NAN */

#ifdef PROP_TXSTATUS
#include <dhd_wlfc.h>
#endif

#ifdef BCMPCIE
#include <dhd_flowring.h>
#endif
#ifdef RTT_SUPPORT
#include <dhd_rtt.h>
#endif /* RTT_SUPPORT */

#if defined(BIGDATA_SOFTAP) || defined(DHD_ENABLE_BIGDATA_LOGGING)
#include <wl_bigdata.h>
#endif /* BIGDATA_SOFTAP || DHD_ENABLE_BIGDATA_LOGGING */

#ifdef DHD_EVENT_LOG_FILTER
#include <dhd_event_log_filter.h>
#endif /* DHD_EVENT_LOG_FILTER */
#define BRCM_SAE_VENDOR_EVENT_BUF_LEN 500

#ifdef DNGL_AXI_ERROR_LOGGING
#include <bcmtlv.h>
#endif /* DNGL_AXI_ERROR_LOGGING */

#if defined(CONFIG_WLAN_BEYONDX) || defined(CONFIG_SEC_5GMODEL)
#include <linux/dev_ril_bridge.h>
#include <linux/notifier.h>
#endif /* CONFIG_WLAN_BEYONDX || defined(CONFIG_SEC_5GMODEL) */

#if (defined(WL_FW_OCE_AP_SELECT) || defined(BCMFW_ROAM_ENABLE)) && \
	((LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)) || defined(WL_COMPAT_WIRELESS))
uint fw_ap_select = true;
#else
uint fw_ap_select = false;
#endif /* WL_FW_OCE_AP_SELECT && (ROAM_ENABLE || BCMFW_ROAM_ENABLE) */
module_param(fw_ap_select, uint, 0660);

#if defined(WL_REASSOC)
uint wl_reassoc_support = true;
#else
uint wl_reassoc_support = false;
#endif /* WL_REASSOC */
module_param(wl_reassoc_support, uint, 0660);

static struct device *cfg80211_parent_dev = NULL;
static struct bcm_cfg80211 *g_bcmcfg = NULL;
u32 wl_dbg_level = WL_DBG_ERR;

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

#if !defined(BCMDONGLEHOST)
#ifdef ntoh32
#undef ntoh32
#endif
#ifdef ntoh16
#undef ntoh16
#endif
#ifdef htod32
#undef htod32
#endif
#ifdef htod16
#undef htod16
#endif
#define ntoh32(i) (i)
#define ntoh16(i) (i)
#define htod32(i) (i)
#define htod16(i) (i)
#define DNGL_FUNC(func, parameters)
#else
#define DNGL_FUNC(func, parameters) func parameters
#define COEX_DHCP

#endif /* defined(BCMDONGLEHOST) */

#define WLAN_EID_SSID	0
#define CH_MIN_5G_CHANNEL 34
#ifdef WLAIBSS
enum abiss_event_type {
	AIBSS_EVENT_TXFAIL
};
#endif

#ifdef WL_RELMCAST
enum rmc_event_type {
	RMC_EVENT_NONE,
	RMC_EVENT_LEADER_CHECK_FAIL
};
#endif /* WL_RELMCAST */

/* This is to override regulatory domains defined in cfg80211 module (reg.c)
 * By default world regulatory domain defined in reg.c puts the flags NL80211_RRF_PASSIVE_SCAN
 * and NL80211_RRF_NO_IBSS for 5GHz channels (for 36..48 and 149..165).
 * With respect to these flags, wpa_supplicant doesn't start p2p operations on 5GHz channels.
 * All the chnages in world regulatory domain are to be done here.
 *
 * this definition reuires disabling missing-field-initializer warning
 * as the ieee80211_regdomain definition differs in plain linux and in Android
 */
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && \
(__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wmissing-field-initializers\"")
#endif
static const struct ieee80211_regdomain brcm_regdom = {
#ifdef WL_6G_BAND
	.n_reg_rules = 8,
#else
	.n_reg_rules = 4,
#endif
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
		REG_RULE(5470-10, 5850+10, 40, 6, 20, 0),
#ifdef WL_6G_BAND
		REG_RULE(6025-80, 6985+80, 160, 6, 20, 0),
		REG_RULE(5935-10, 7115+10, 20, 6, 20, 0),
		REG_RULE(5965-20, 7085+20, 40, 6, 20, 0),
		REG_RULE(5985-40, 7025+40, 80, 6, 20, 0),
#endif /* WL_6G_BAND */
		}
};
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && \
(__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic pop")
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)) && \
	(defined(WL_IFACE_COMB_NUM_CHANNELS) || \
	defined(WL_CFG80211_P2P_DEV_IF))
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
	 * linux-3.8 and above - max:4
	 * sta + NAN NMI + NAN DPI open + NAN DPI sec (since there is no iface type
	 * for NAN defined, registering it as STA type)
	 */
#ifdef WL_ENABLE_P2P_IF
	.max = 5,
#else
	.max = 4,
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

#define NUM_DIFF_CHANNELS 2

static const struct ieee80211_iface_combination
common_iface_combinations[] = {
	{
	.num_different_channels = NUM_DIFF_CHANNELS,
	/*
	 * At Max 5 network interfaces can be registered concurrently
	 */
	.max_interfaces = IFACE_MAX_CNT,
	.limits = common_if_limits,
	.n_limits = ARRAY_SIZE(common_if_limits),
	},
};
#endif /* LINUX_VER >= 3.0 && (WL_IFACE_COMB_NUM_CHANNELS || WL_CFG80211_P2P_DEV_IF) */

static const char *wl_if_state_strs[WL_IF_STATE_MAX + 1] = {
	"WL_IF_CREATE_REQ",
	"WL_IF_CREATE_DONE",
	"WL_IF_DELETE_REQ",
	"WL_IF_DELETE_DONE",
	"WL_IF_CHANGE_REQ",
	"WL_IF_CHANGE_DONE",
	"WL_IF_STATE_MAX"
};

#ifdef WBTEXT
typedef struct wl_wbtext_bssid {
	struct ether_addr ea;
	struct list_head list;
} wl_wbtext_bssid_t;

static void wl_cfg80211_wbtext_reset_conf(struct bcm_cfg80211 *cfg, struct net_device *ndev);
static void wl_cfg80211_wbtext_update_rcc(struct bcm_cfg80211 *cfg, struct net_device *dev);
static bool wl_cfg80211_wbtext_check_bssid_list(struct bcm_cfg80211 *cfg, struct ether_addr *ea);
static bool wl_cfg80211_wbtext_add_bssid_list(struct bcm_cfg80211 *cfg, struct ether_addr *ea);
static void wl_cfg80211_wbtext_clear_bssid_list(struct bcm_cfg80211 *cfg);
static bool wl_cfg80211_wbtext_send_nbr_req(struct bcm_cfg80211 *cfg, struct net_device *dev,
	struct wl_profile *profile);
static bool wl_cfg80211_wbtext_send_btm_query(struct bcm_cfg80211 *cfg, struct net_device *dev,
	struct wl_profile *profile);
static void wl_cfg80211_wbtext_set_wnm_maxidle(struct bcm_cfg80211 *cfg, struct net_device *dev);
static int wl_cfg80211_recv_nbr_resp(struct net_device *dev, uint8 *body, uint body_len);
#endif /* WBTEXT */

#ifdef RTT_SUPPORT
static s32 wl_cfg80211_rtt_event_handler(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
		const wl_event_msg_t *e, void *data);
#endif /* RTT_SUPPORT */
#ifdef WL_CHAN_UTIL
static s32 wl_cfg80211_bssload_report_event_handler(struct bcm_cfg80211 *cfg,
	bcm_struct_cfgdev *cfgdev, const wl_event_msg_t *e, void *data);
static s32 wl_cfg80211_start_bssload_report(struct net_device *ndev);
#endif /* WL_CHAN_UTIL */

/* SoftAP related parameters */
#define DEFAULT_2G_SOFTAP_CHANNEL	1
#define DEFAULT_2G_SOFTAP_CHANSPEC	0x1006
#define DEFAULT_5G_SOFTAP_CHANNEL	149
#define WL_MAX_NUM_CSA_COUNTERS		255

#define MAX_VNDR_OUI_STR_LEN	256u
#define VNDR_OUI_STR_LEN	10u
#define DOT11_DISCONNECT_RC     2u
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

#ifdef WL_ANALYTICS
static const uchar disco_bcnloss_vsie[] = {
	0xdd, /* Vendor specific */
	0x09, /* Length */
	0x00, 0x00, 0xF0, /* OUI */
	0x22, /* VENDOR_ENTERPRISE_STA_OUI_TYPE */
	0x03, /* Sub type for additional rc */
	0x01, /* Version */
	0x02, /* Length */
	0x07, 0x00 /* Reason code for BCN loss */
};
#endif /* WL_ANALYTICS */

static int wl_vndr_ies_get_vendor_oui(struct bcm_cfg80211 *cfg,
		struct net_device *ndev, char *vndr_oui, u32 vndr_oui_len);
static void wl_vndr_ies_clear_vendor_oui_list(struct bcm_cfg80211 *cfg);
#ifdef WL_ANALYTICS
static bool wl_vndr_ies_find_vendor_oui(struct bcm_cfg80211 *cfg,
	struct net_device *ndev, const char *vndr_oui);
#endif
static s32 wl_cfg80211_parse_vndr_ies(const u8 *parse, u32 len,
		struct parsed_vndr_ies *vndr_ies);
static bool wl_cfg80211_filter_vndr_ext_id(const vndr_ie_t *vndrie);
#if defined(WL_FW_OCE_AP_SELECT)
static bool
wl_cfgoce_has_ie(const u8 *ie, const u8 **tlvs, u32 *tlvs_len, const u8 *oui, u32 oui_len, u8 type);

/* Check whether the given IE looks like WFA OCE IE. */
#define wl_cfgoce_is_oce_ie(ie, tlvs, len)      wl_cfgoce_has_ie(ie, tlvs, len, \
	(const uint8 *)WFA_OUI, WFA_OUI_LEN, WFA_OUI_TYPE_MBO_OCE)

/* Is any of the tlvs the expected entry? If
 * not update the tlvs buffer pointer/length.
 */
static bool
wl_cfgoce_has_ie(const u8 *ie, const u8 **tlvs, u32 *tlvs_len, const u8 *oui, u32 oui_len, u8 type)
{
	/* If the contents match the OUI and the type */
	if (ie[TLV_LEN_OFF] >= oui_len + 1 &&
			!bcmp(&ie[TLV_BODY_OFF], oui, oui_len) &&
			type == ie[TLV_BODY_OFF + oui_len]) {
		return TRUE;
	}

	return FALSE;
}
#endif /* WL_FW_OCE_AP_SELECT */

/*
 * cfg80211_ops api/callback list
 */
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
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0))
static int wl_cfg80211_update_connect_params(struct wiphy *wiphy, struct net_device *dev,
	struct cfg80211_connect_params *sme, u32 changed);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0) */
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
#if defined(WL_SUPPORT_BACKPORTED_KPATCHES) || (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0))
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
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
static s32 wl_cfg80211_change_station(struct wiphy *wiphy,
	struct net_device *dev, const u8 *mac, struct station_parameters *params);
#else
static s32 wl_cfg80211_change_station(struct wiphy *wiphy,
	struct net_device *dev, u8 *mac, struct station_parameters *params);
#endif
#endif /* WL_SUPPORT_BACKPORTED_KPATCHES || KERNEL_VER >= KERNEL_VERSION(3, 2, 0)) */
static s32 wl_cfg80211_set_pmksa(struct wiphy *wiphy, struct net_device *dev,
	struct cfg80211_pmksa *pmksa);
static s32 wl_cfg80211_del_pmksa(struct wiphy *wiphy, struct net_device *dev,
	struct cfg80211_pmksa *pmksa);
static s32 wl_cfg80211_flush_pmksa(struct wiphy *wiphy,
	struct net_device *dev);
static s32 wl_cfg80211_update_pmksa(struct wiphy *wiphy, struct net_device *dev,
	struct cfg80211_pmksa *pmksa, bool set);
static void wl_cfg80211_spmk_pmkdb_change_pmk_type(struct bcm_cfg80211 *cfg,
	pmkid_list_v3_t *pmk_list);
static void wl_cfg80211_spmk_pmkdb_del_spmk(struct bcm_cfg80211 *cfg,
	struct cfg80211_pmksa *pmksa);

struct wireless_dev *
wl_cfg80211_create_iface(struct wiphy *wiphy, wl_iftype_t
	iface_type, u8 *mac_addr, const char *name);
s32
wl_cfg80211_del_iface(struct wiphy *wiphy, struct wireless_dev *wdev);

s32 wl_cfg80211_interface_ops(struct bcm_cfg80211 *cfg,
	struct net_device *ndev, s32 bsscfg_idx,
	wl_iftype_t iftype, s32 del, u8 *addr);
s32 wl_cfg80211_add_del_bss(struct bcm_cfg80211 *cfg,
	struct net_device *ndev, s32 bsscfg_idx,
	wl_iftype_t brcm_iftype, s32 del, u8 *addr);
#ifdef GTK_OFFLOAD_SUPPORT
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0))
static s32 wl_cfg80211_set_rekey_data(struct wiphy *wiphy, struct net_device *dev,
	struct cfg80211_gtk_rekey_data *data);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0) */
#endif /* GTK_OFFLOAD_SUPPORT */
chanspec_t wl_chspec_driver_to_host(chanspec_t chanspec);
chanspec_t wl_chspec_host_to_driver(chanspec_t chanspec);
static void wl_cfg80211_wait_for_disconnection(struct bcm_cfg80211 *cfg, struct net_device *dev);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0))
#ifdef WLFBT
static int wl_cfg80211_update_ft_ies(struct wiphy *wiphy, struct net_device *dev,
	struct cfg80211_update_ft_ies_params *ftie);
#endif /* WLFBT */
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0) */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0))
static int wl_cfg80211_set_pmk(struct wiphy *wiphy, struct net_device *dev,
        const struct cfg80211_pmk_conf *conf);
static int wl_cfg80211_del_pmk(struct wiphy *wiphy, struct net_device *dev,
        const u8 *aa);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0) */

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
static void wl_put_event(struct bcm_cfg80211 *cfg, struct wl_event_q *e);
static s32 wl_notify_connect_status(struct bcm_cfg80211 *cfg,
	bcm_struct_cfgdev *cfgdev, const wl_event_msg_t *e, void *data);
static s32 wl_notify_roaming_status(struct bcm_cfg80211 *cfg,
	bcm_struct_cfgdev *cfgdev, const wl_event_msg_t *e, void *data);
static s32 wl_bss_connect_done(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *e, void *data, bool completed);
#ifdef DHD_LOSSLESS_ROAMING
static s32 wl_bss_roaming_done(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *e, void *data);
#endif /* DHD_LOSSLESS_ROAMING */
static s32 wl_notify_mic_status(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data);
#ifdef BT_WIFI_HANDOVER
static s32 wl_notify_bt_wifi_handover_req(struct bcm_cfg80211 *cfg,
	bcm_struct_cfgdev *cfgdev, const wl_event_msg_t *e, void *data);
#endif /* BT_WIFI_HANDOVER */
#ifdef GSCAN_SUPPORT
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
#if defined(DHD_LOSSLESS_ROAMING) || defined (DBG_PKT_MON)
static s32 wl_notify_roam_prep_status(struct bcm_cfg80211 *cfg,
	bcm_struct_cfgdev *cfgdev, const wl_event_msg_t *e, void *data);
#endif /* DHD_LOSSLESS_ROAMING || DBG_PKT_MON */
#ifdef DHD_LOSSLESS_ROAMING
static void wl_del_roam_timeout(struct bcm_cfg80211 *cfg);
#endif /* DHD_LOSSLESS_ROAMING */
#ifdef WL_SDO
static s32 wl_svc_resp_handler(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data);
static s32 wl_notify_device_discovery(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data);
#endif

#ifdef WL_MBO
static s32
wl_mbo_event_handler(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data);
#endif /* WL_MBO */

#ifdef WL_TWT
static s32
wl_notify_twt_event(struct bcm_cfg80211 *cfg,
		bcm_struct_cfgdev *cfgdev, const wl_event_msg_t *e, void *data);
#endif /* WL_TWT */

#ifdef WL_CLIENT_SAE
static bool wl_is_pmkid_available(struct net_device *dev, const u8 *bssid);
static s32 wl_notify_start_auth(struct bcm_cfg80211 *cfg,
	bcm_struct_cfgdev *cfgdev, const wl_event_msg_t *e, void *data);
static s32 wl_cfg80211_external_auth(struct wiphy *wiphy,
	struct net_device *dev, struct cfg80211_external_auth_params *ext_auth);
static s32
wl_cfg80211_mgmt_auth_tx(struct net_device *dev, bcm_struct_cfgdev *cfgdev,
	struct bcm_cfg80211 *cfg, const u8 *buf, size_t len, s32 bssidx, u64 *cookie);
#endif /* WL_CLIENT_SAE */

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
static s32 wl_set_set_sharedkey(struct net_device *dev,
	struct cfg80211_connect_params *sme);
#ifdef WL_FILS
static s32 wl_set_fils_params(struct net_device *dev,
	struct cfg80211_connect_params *sme);
#endif

#ifdef BCMWAPI_WPI
static s32 wl_set_set_wapi_ie(struct net_device *dev,
	struct cfg80211_connect_params *sme);
#endif

#ifdef WL_GCMP
static s32 wl_set_wsec_info_algos(struct net_device *dev, uint32 algos, uint32 mask);
#endif /* WL_GCMP */
static s32 wl_get_assoc_ies(struct bcm_cfg80211 *cfg, struct net_device *ndev);
void wl_cfg80211_clear_security(struct bcm_cfg80211 *cfg);

/*
 * information element utilities
 */
static __used s32 wl_add_ie(struct bcm_cfg80211 *cfg, u8 t, u8 l, u8 *v);

#ifdef MFP
static int wl_cfg80211_get_rsn_capa(const bcm_tlv_t *wpa2ie, const u8** rsn_cap);
#endif

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

static s32 wl_update_bss_info(struct bcm_cfg80211 *cfg, struct net_device *ndev, bool update_ssid);
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
static __used bool wl_is_ibssstarter(struct bcm_cfg80211 *cfg);

/*
 * link up/down , default configuration utilities
 */
static s32 __wl_cfg80211_up(struct bcm_cfg80211 *cfg);
static s32 __wl_cfg80211_down(struct bcm_cfg80211 *cfg);

static void wl_link_up(struct bcm_cfg80211 *cfg);
static s32 wl_handle_link_down(struct bcm_cfg80211 *cfg, wl_assoc_status_t *as);
static s32 wl_post_linkup_ops(struct bcm_cfg80211 *cfg, wl_assoc_status_t *as);
static void wl_link_down(struct bcm_cfg80211 *cfg);
static s32 wl_config_infra(struct bcm_cfg80211 *cfg, struct net_device *ndev, u16 iftype);
static void wl_init_conf(struct wl_conf *conf);
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

/*
 * Some external functions, TODO: move them to dhd_linux.h
 */
int dhd_add_monitor(const char *name, struct net_device **new_ndev);
int dhd_del_monitor(struct net_device *ndev);
int dhd_monitor_init(void *dhd_pub);
int dhd_monitor_uninit(void);
netdev_tx_t dhd_start_xmit(struct sk_buff *skb, struct net_device *net);

#ifdef ROAM_CHANNEL_CACHE
int init_roam_cache(struct bcm_cfg80211 *cfg, int ioctl_ver);
#endif /* ROAM_CHANNEL_CACHE */

#ifdef P2P_LISTEN_OFFLOADING
s32 wl_cfg80211_p2plo_deinit(struct bcm_cfg80211 *cfg);
#endif /* P2P_LISTEN_OFFLOADING */

#ifdef CUSTOMER_HW4_DEBUG
extern bool wl_scan_timeout_dbg_enabled;
#endif /* CUSTOMER_HW4_DEBUG */
#ifdef PKT_FILTER_SUPPORT
extern uint dhd_pkt_filter_enable;
extern void dhd_pktfilter_offload_enable(dhd_pub_t * dhd, char *arg, int enable, int master_mode);
#endif /* PKT_FILTER_SUPPORT */

static int wl_cfg80211_delayed_roam(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const struct ether_addr *bssid);
static s32 __wl_update_wiphybands(struct bcm_cfg80211 *cfg, bool notify);

#ifdef WL_SDO
s32 wl_cfg80211_sdo_init(struct bcm_cfg80211 *cfg);
s32 wl_cfg80211_sdo_deinit(struct bcm_cfg80211 *cfg);
#define MAX_SDO_PROTO 5
wl_sdo_proto_t wl_sdo_protos [] = {
	{ "all", SVC_RPOTYPE_ALL },
	{ "upnp", SVC_RPOTYPE_UPNP },
	{ "bonjour", SVC_RPOTYPE_BONJOUR },
	{ "wsd", SVC_RPOTYPE_WSD },
	{ "vendor", SVC_RPOTYPE_VENDOR },
};
#endif

#ifdef WL_WPS_SYNC
static void wl_init_wps_reauth_sm(struct bcm_cfg80211 *cfg);
static void wl_deinit_wps_reauth_sm(struct bcm_cfg80211 *cfg);
static void wl_wps_reauth_timeout(unsigned long data);
static s32 wl_get_free_wps_inst(struct bcm_cfg80211 *cfg);
static s32 wl_get_wps_inst_match(struct bcm_cfg80211 *cfg, struct net_device *ndev);
static s32 wl_wps_session_add(struct net_device *ndev, u16 mode, u8 *peer_mac);
static void wl_wps_session_del(struct net_device *ndev);
s32 wl_wps_session_update(struct net_device *ndev, u16 state, const u8 *peer_mac);
static void wl_wps_handle_ifdel(struct net_device *ndev);
#endif /* WL_WPS_SYNC */

#if defined(WL_FW_OCE_AP_SELECT)
bool static wl_cfg80211_is_oce_ap(struct wiphy *wiphy, const u8 *bssid_hint);
#endif /* WL_FW_OCE_AP_SELECT */

#ifdef WL_BCNRECV
static s32 wl_bcnrecv_aborted_event_handler(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
		const wl_event_msg_t *e, void *data);
#endif /* WL_BCNRECV */

#ifdef WL_CAC_TS
static s32 wl_cfg80211_cac_event_handler(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
		const wl_event_msg_t *e, void *data);
#endif /* WL_CAC_TS */

#if defined(WL_MBO) || defined(WL_OCE)
static s32 wl_bssid_prune_event_handler(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
		const wl_event_msg_t *e, void *data);
#endif /* WL_MBO || WL_OCE */
static void wl_cfg80211_handle_set_ssid_complete(struct bcm_cfg80211 *cfg, wl_assoc_status_t *as,
		const wl_event_msg_t *event, wl_assoc_state_t assoc_state);

#if !defined(BCMDONGLEHOST)
/* Wake lock are used in Android only, which is dongle based as of now */
#define DHD_OS_WAKE_LOCK(pub)
#define DHD_OS_WAKE_UNLOCK(pub)
#define DHD_EVENT_WAKE_LOCK(pub)
#define DHD_EVENT_WAKE_UNLOCK(pub)
#define DHD_OS_WAKE_LOCK_TIMEOUT(pub)
#endif /* defined(BCMDONGLEHOST) */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)) || \
	   (defined(CONFIG_ARCH_MSM) && defined(CFG80211_DISCONNECTED_V2))
#define CFG80211_GET_BSS(wiphy, channel, bssid, ssid, ssid_len) \
	cfg80211_get_bss(wiphy, channel, bssid, ssid, ssid_len,	\
			IEEE80211_BSS_TYPE_ANY, IEEE80211_PRIVACY_ANY);
#else
#define CFG80211_GET_BSS(wiphy, channel, bssid, ssid, ssid_len) \
	cfg80211_get_bss(wiphy, channel, bssid, ssid, ssid_len,	\
			WLAN_CAPABILITY_ESS, WLAN_CAPABILITY_ESS);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)) */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 7, 0))
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)) || \
	defined(CFG80211_CONNECT_TIMEOUT_REASON_CODE) || \
	defined(WL_FILS) || defined(CONFIG_CFG80211_FILS_BKPORT)
#define CFG80211_CONNECT_RESULT(dev, bssid, bss, req_ie, req_ie_len, resp_ie, \
		resp_ie_len, status, gfp) \
	cfg80211_connect_bss(dev, bssid, bss, req_ie, req_ie_len, resp_ie, \
		resp_ie_len, status, gfp, NL80211_TIMEOUT_UNSPECIFIED);
#else
#define CFG80211_CONNECT_RESULT(dev, bssid, bss, req_ie, req_ie_len, resp_ie, \
		resp_ie_len, status, gfp) \
	cfg80211_connect_bss(dev, bssid, bss, req_ie, req_ie_len, resp_ie, \
		resp_ie_len, status, gfp);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0) || \
	* (CFG80211_CONNECT_TIMEOUT_REASON_CODE) ||
	* WL_FILS || CONFIG_CFG80211_FILS_BKPORT
	*/
#elif defined(CFG80211_CONNECT_TIMEOUT_REASON_CODE)
/* There are customer kernels with backported changes for
 *  connect timeout. CFG80211_CONNECT_TIMEOUT_REASON_CODE define
 * is available for kernels < 4.7 in such cases.
 */
#define CFG80211_CONNECT_RESULT(dev, bssid, bss, req_ie, req_ie_len, resp_ie, \
		resp_ie_len, status, gfp) \
	cfg80211_connect_bss(dev, bssid, bss, req_ie, req_ie_len, resp_ie, \
		resp_ie_len, status, gfp, NL80211_TIMEOUT_UNSPECIFIED);
#else
/* Kernels < 4.7 doesn't support cfg80211_connect_bss */
#define CFG80211_CONNECT_RESULT(dev, bssid, bss, req_ie, req_ie_len, resp_ie, \
		resp_ie_len, status, gfp) \
	cfg80211_connect_result(dev, bssid, req_ie, req_ie_len, resp_ie, \
		resp_ie_len, status, gfp);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4, 7, 0) */

#define IS_WPA_AKM(akm) ((akm) == RSN_AKM_NONE ||			\
				 (akm) == RSN_AKM_UNSPECIFIED ||	\
				 (akm) == RSN_AKM_PSK)

#define WL_EIDX_INVALID	0xffff
#define WL_SET_EIDX_IN_PROGRESS(cfg, id, type)	\
	{ cfg->eidx.in_progress = id; \
	cfg->eidx.event_type = type; }
#define WL_CLR_EIDX_STATES(cfg)	\
	cfg->eidx.in_progress = WL_EIDX_INVALID;
extern int dhd_wait_pend8021x(struct net_device *dev);
#ifdef PROP_TXSTATUS_VSDB
extern int disable_proptx;
#endif /* PROP_TXSTATUS_VSDB */

/* WAR: disable pm_bcnrx , scan_ps for BCM4354 WISOL module.
* WISOL module have ANT_1 Rx sensitivity issue.
*/
#if defined(FORCE_DISABLE_SINGLECORE_SCAN)
extern void dhd_force_disable_singlcore_scan(dhd_pub_t *dhd);
#endif /* FORCE_DISABLE_SINGLECORE_SCAN */

#if ((LINUX_VERSION_CODE >= KERNEL_VERSION (3, 5, 0)) && (LINUX_VERSION_CODE <= (3, 7, 0)))
struct chan_info {
	int freq;
	int chan_type;
};
#endif

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
	CHAN5G(165, 0),

#if defined(WL_6G_BAND) && !defined(CFG80211_6G_SUPPORT)
	/* 6GHz frequency starting 5935 */
	CHAN6G_CHAN2(0), CHAN6G(1, 0),
	CHAN6G(5, 0), CHAN6G(9, 0),
	CHAN6G(13, 0), CHAN6G(17, 0),
	CHAN6G(21, 0), CHAN6G(25, 0),
	CHAN6G(29, 0), CHAN6G(33, 0),
	CHAN6G(37, 0), CHAN6G(41, 0),
	CHAN6G(45, 0), CHAN6G(49, 0),
	CHAN6G(53, 0), CHAN6G(57, 0),
	CHAN6G(61, 0), CHAN6G(65, 0),
	CHAN6G(69, 0), CHAN6G(73, 0),
	CHAN6G(77, 0), CHAN6G(81, 0),
	CHAN6G(85, 0), CHAN6G(89, 0),
	CHAN6G(93, 0), CHAN6G(97, 0),
	CHAN6G(101, 0), CHAN6G(105, 0),
	CHAN6G(109, 0), CHAN6G(113, 0),
	CHAN6G(117, 0), CHAN6G(121, 0),
	CHAN6G(125, 0), CHAN6G(129, 0),
	CHAN6G(133, 0), CHAN6G(137, 0),
	CHAN6G(141, 0), CHAN6G(145, 0),
	CHAN6G(149, 0), CHAN6G(153, 0),
	CHAN6G(157, 0), CHAN6G(161, 0),
	CHAN6G(165, 0), CHAN6G(169, 0),
	CHAN6G(173, 0), CHAN6G(177, 0),
	CHAN6G(181, 0), CHAN6G(185, 0),
	CHAN6G(189, 0), CHAN6G(193, 0),
	CHAN6G(197, 0), CHAN6G(201, 0),
	CHAN6G(205, 0), CHAN6G(209, 0),
	CHAN6G(213, 0), CHAN6G(217, 0),
	CHAN6G(221, 0), CHAN6G(225, 0),
	CHAN6G(229, 0), CHAN6G(233, 0),

	CHAN6G(3, 0), CHAN6G(11, 0),
	CHAN6G(19, 0), CHAN6G(27, 0),
	CHAN6G(35, 0), CHAN6G(43, 0),
	CHAN6G(51, 0), CHAN6G(59, 0),
	CHAN6G(67, 0), CHAN6G(75, 0),
	CHAN6G(83, 0), CHAN6G(91, 0),
	CHAN6G(99, 0), CHAN6G(107, 0),
	CHAN6G(115, 0), CHAN6G(123, 0),
	CHAN6G(131, 0), CHAN6G(139, 0),
	CHAN6G(147, 0), CHAN6G(155, 0),
	CHAN6G(163, 0), CHAN6G(171, 0),
	CHAN6G(179, 0), CHAN6G(187, 0),
	CHAN6G(195, 0), CHAN6G(203, 0),
	CHAN6G(211, 0), CHAN6G(219, 0), CHAN6G(227, 0),

	CHAN6G(7, 0), CHAN6G(23, 0),
	CHAN6G(39, 0), CHAN6G(55, 0),
	CHAN6G(71, 0), CHAN6G(87, 0),
	CHAN6G(103, 0), CHAN6G(119, 0),
	CHAN6G(135, 0), CHAN6G(151, 0),
	CHAN6G(167, 0), CHAN6G(183, 0),
	CHAN6G(199, 0), CHAN6G(215, 0),

	CHAN6G(15, 0), CHAN6G(47, 0),
	CHAN6G(79, 0), CHAN6G(111, 0),
	CHAN6G(143, 0), CHAN6G(175, 0), CHAN6G(207, 0),
#endif /* WL_6G_BAND && !CFG80211_6G_SUPPORT */
};

#ifdef CFG80211_6G_SUPPORT
static struct ieee80211_channel __wl_6ghz_channels[] = {
	CHAN6G_CHAN2(0), CHAN6G(1, 0),
	CHAN6G(5, 0), CHAN6G(9, 0),
	CHAN6G(13, 0), CHAN6G(17, 0),
	CHAN6G(21, 0), CHAN6G(25, 0),
	CHAN6G(29, 0), CHAN6G(33, 0),
	CHAN6G(37, 0), CHAN6G(41, 0),
	CHAN6G(45, 0), CHAN6G(49, 0),
	CHAN6G(53, 0), CHAN6G(57, 0),
	CHAN6G(61, 0), CHAN6G(65, 0),
	CHAN6G(69, 0), CHAN6G(73, 0),
	CHAN6G(77, 0), CHAN6G(81, 0),
	CHAN6G(85, 0), CHAN6G(89, 0),
	CHAN6G(93, 0), CHAN6G(97, 0),
	CHAN6G(101, 0), CHAN6G(105, 0),
	CHAN6G(109, 0), CHAN6G(113, 0),
	CHAN6G(117, 0), CHAN6G(121, 0),
	CHAN6G(125, 0), CHAN6G(129, 0),
	CHAN6G(133, 0), CHAN6G(137, 0),
	CHAN6G(141, 0), CHAN6G(145, 0),
	CHAN6G(149, 0), CHAN6G(153, 0),
	CHAN6G(157, 0), CHAN6G(161, 0),
	CHAN6G(165, 0), CHAN6G(169, 0),
	CHAN6G(173, 0), CHAN6G(177, 0),
	CHAN6G(181, 0), CHAN6G(185, 0),
	CHAN6G(189, 0), CHAN6G(193, 0),
	CHAN6G(197, 0), CHAN6G(201, 0),
	CHAN6G(205, 0), CHAN6G(209, 0),
	CHAN6G(213, 0), CHAN6G(217, 0),
	CHAN6G(221, 0), CHAN6G(225, 0),
	CHAN6G(229, 0), CHAN6G(233, 0),
};
#endif /* CFG80211_6G_SUPPORT */

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

#ifdef CFG80211_6G_SUPPORT
static struct ieee80211_supported_band __wl_band_6ghz = {
	.band = IEEE80211_BAND_6GHZ,
	.channels = __wl_6ghz_channels,
	.n_channels = ARRAY_SIZE(__wl_6ghz_channels),
	.bitrates = wl_a_rates,
	.n_bitrates = wl_a_rates_size
};
#endif /* CFG80211_6G_SUPPORT */

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
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0))
	WLAN_CIPHER_SUITE_BIP_GMAC_256,
	WLAN_CIPHER_SUITE_BIP_GMAC_128,
	WLAN_CIPHER_SUITE_BIP_CMAC_256,
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0) */
#endif /* MFP */

#ifdef BCMWAPI_WPI
	WLAN_CIPHER_SUITE_SMS4,
#endif

#if defined(WLAN_CIPHER_SUITE_PMK)
	WLAN_CIPHER_SUITE_PMK,
#endif /* WLAN_CIPHER_SUITE_PMK */
#ifdef WL_GCMP
	WLAN_CIPHER_SUITE_GCMP,
	WLAN_CIPHER_SUITE_GCMP_256,
	WLAN_CIPHER_SUITE_BIP_GMAC_128,
	WLAN_CIPHER_SUITE_BIP_GMAC_256,
#endif /* WL_GCMP */
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

#ifdef WL_CFG80211_GON_COLLISION
#define BLOCK_GON_REQ_MAX_NUM 5
#endif /* WL_CFG80211_GON_COLLISION */

#if defined(USE_DYNAMIC_MAXPKT_RXGLOM)
static int maxrxpktglom = 0;
#endif

/* IOCtl version read from targeted driver */
int ioctl_version;

typedef struct rsn_cipher_algo_entry {
	u32 cipher_suite;
	u32 wsec_algo;
	u32 wsec_key_algo;
} rsn_cipher_algo_entry_t;

static const rsn_cipher_algo_entry_t rsn_cipher_algo_lookup_tbl[] = {
	{WLAN_CIPHER_SUITE_WEP40, WEP_ENABLED, CRYPTO_ALGO_WEP1},
	{WLAN_CIPHER_SUITE_WEP104, WEP_ENABLED, CRYPTO_ALGO_WEP128},
	{WLAN_CIPHER_SUITE_TKIP, TKIP_ENABLED, CRYPTO_ALGO_TKIP},
	{WLAN_CIPHER_SUITE_CCMP, AES_ENABLED, CRYPTO_ALGO_AES_CCM},
	{WLAN_CIPHER_SUITE_AES_CMAC, AES_ENABLED, CRYPTO_ALGO_BIP},

#ifdef BCMWAPI_WPI
	{WLAN_CIPHER_SUITE_SMS4, SMS4_ENABLED, CRYPTO_ALGO_SMS4},
#endif /* BCMWAPI_WPI */

#ifdef WL_GCMP
	{WLAN_CIPHER_SUITE_GCMP, AES_ENABLED, CRYPTO_ALGO_AES_GCM},
	{WLAN_CIPHER_SUITE_GCMP_256, AES_ENABLED, CRYPTO_ALGO_AES_GCM256},
	{WLAN_CIPHER_SUITE_BIP_GMAC_128, AES_ENABLED, CRYPTO_ALGO_BIP_GMAC},
	{WLAN_CIPHER_SUITE_BIP_GMAC_256, AES_ENABLED, CRYPTO_ALGO_BIP_GMAC256},
#endif /* WL_GCMP */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0))
	{WLAN_CIPHER_SUITE_BIP_CMAC_256, AES_ENABLED, CRYPTO_ALGO_BIP_CMAC256},
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0) */
};

typedef struct rsn_akm_wpa_auth_entry {
	u32 akm_suite;
	u32 wpa_auth;
} rsn_akm_wpa_auth_entry_t;

static const rsn_akm_wpa_auth_entry_t rsn_akm_wpa_auth_lookup_tbl[] = {
#ifdef WL_OWE
	{WLAN_AKM_SUITE_OWE, WPA3_AUTH_OWE},
#endif /* WL_OWE */
	{WLAN_AKM_SUITE_8021X, WPA2_AUTH_UNSPECIFIED},
	{WL_AKM_SUITE_SHA256_1X, WPA2_AUTH_1X_SHA256},
	{WL_AKM_SUITE_SHA256_PSK, WPA2_AUTH_PSK_SHA256},
	{WLAN_AKM_SUITE_PSK, WPA2_AUTH_PSK},
	{WLAN_AKM_SUITE_FT_8021X, WPA2_AUTH_UNSPECIFIED | WPA2_AUTH_FT},
	{WLAN_AKM_SUITE_FT_PSK, WPA2_AUTH_PSK | WPA2_AUTH_FT},
	{WLAN_AKM_SUITE_FILS_SHA256, WPA2_AUTH_FILS_SHA256},
	{WLAN_AKM_SUITE_FILS_SHA384, WPA2_AUTH_FILS_SHA384},
	{WLAN_AKM_SUITE_8021X_SUITE_B, WPA3_AUTH_1X_SUITE_B_SHA256},
	{WLAN_AKM_SUITE_8021X_SUITE_B_192, WPA3_AUTH_1X_SUITE_B_SHA384},

#ifdef BCMWAPI_WPI
	{WLAN_AKM_SUITE_WAPI_CERT, WAPI_AUTH_UNSPECIFIED},
	{WLAN_AKM_SUITE_WAPI_PSK, WAPI_AUTH_PSK},
#endif /* BCMWAPI_WPI */

#if defined(WL_SAE) || defined(WL_CLIENT_SAE)
	{WLAN_AKM_SUITE_SAE, WPA3_AUTH_SAE_PSK},
#endif /* WL_SAE || WL_CLIENT_SAE */
#ifdef WL_SAE_FT
	{WLAN_AKM_SUITE_FT_OVER_SAE, WPA3_AUTH_SAE_PSK | WPA2_AUTH_FT},
#endif /* WL_SAE_FT */
	{WLAN_AKM_SUITE_DPP, WPA3_AUTH_DPP_AKM},
	{WLAN_AKM_SUITE_FT_8021X_SHA384, WPA3_AUTH_1X_SUITE_B_SHA384 | WPA2_AUTH_FT}
};

#define BUFSZ 8
#define BUFSZN	BUFSZ + 1

#define _S(x) #x
#define S(x) _S(x)

#define SOFT_AP_IF_NAME         "swlan0"

/* watchdog timer for disconnecting when fw is not associated for FW_ASSOC_WATCHDOG_TIME ms */
uint32 fw_assoc_watchdog_ms = 0;
bool fw_assoc_watchdog_started = 0;
#define FW_ASSOC_WATCHDOG_TIME 10 * 1000 /* msec */

int wl_channel_to_frequency(u32 chan, chanspec_band_t band)
{
	if (chan == 0) {
		return 0; /* not supported */
	}
	switch (band) {
	case WL_CHANSPEC_BAND_2G:
		if (chan == 14)
			return 2484;
		else if (chan < 14)
			return 2407 + chan * 5;
		break;
	case WL_CHANSPEC_BAND_5G:
		if (chan >= 182 && chan <= 196)
			return 4000 + chan * 5;
		else
			return 5000 + chan * 5;
		break;
#ifdef WL_6G_BAND
	case WL_CHANSPEC_BAND_6G:
		if (chan == 2) {
			/* Specific handling for channel 2 in 6G */
			return 5935;
		}
		return 5950 + chan * 5;
		break;
#endif /* WL_6G_BAND */
	default:
		WL_ERR(("Invalid Frequency Band\n"));
	}
	return 0; /* not supported */
}

static void wl_add_remove_pm_enable_work(struct bcm_cfg80211 *cfg,
	enum wl_pm_workq_act_type type)
{
	u16 wq_duration = 0;

#if defined(BCMDONGLEHOST) && defined(OEM_ANDROID)
	dhd_pub_t *dhd =  NULL;
#endif /* BCMDONGLEHOST && OEM_ANDROID */

	if (cfg == NULL)
		return;

#if defined(BCMDONGLEHOST) && defined(OEM_ANDROID)
	dhd = (dhd_pub_t *)(cfg->pub);
#endif /* BCMDONGLEHOST && OEM_ANDROID */

	mutex_lock(&cfg->pm_sync);
	/*
	 * Make cancel and schedule work part mutually exclusive
	 * so that while cancelling, we are sure that there is no
	 * work getting scheduled.
	 */
	if (delayed_work_pending(&cfg->pm_enable_work)) {
		cancel_delayed_work(&cfg->pm_enable_work);

#if defined(BCMDONGLEHOST) && defined(OEM_ANDROID)
		DHD_PM_WAKE_UNLOCK(cfg->pub);
#endif /* BCMDONGLEHOST && OEM_ANDROID */

	}

	if (type == WL_PM_WORKQ_SHORT) {
		wq_duration = WL_PM_ENABLE_TIMEOUT;
	} else if (type == WL_PM_WORKQ_LONG) {
		wq_duration = (WL_PM_ENABLE_TIMEOUT*2);
	}

	/* It should schedule work item only if driver is up */
	if (wq_duration) {
#if defined(BCMDONGLEHOST) && defined(OEM_ANDROID)
		if (dhd->up)
#endif /* defined(BCMDONGLEHOST) && defined(OEM_ANDROID) */
		{
			if (schedule_delayed_work(&cfg->pm_enable_work,
				msecs_to_jiffies((const unsigned int)wq_duration))) {

#if defined(BCMDONGLEHOST) && defined(OEM_ANDROID)
				DHD_PM_WAKE_LOCK_TIMEOUT(cfg->pub, wq_duration);
#endif /* BCMDONGLEHOST && OEM_ANDROID */
			} else {
				WL_ERR(("Can't schedule pm work handler\n"));
			}
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

bool wl_cfg80211_is_hal_started(struct bcm_cfg80211 *cfg)
{
	return cfg->hal_started;
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
wl_ch_host_to_driver(u16 channel)
{
	chanspec_t chanspec;
	chanspec_band_t band;

	band = WL_CHANNEL_BAND(channel);

	chanspec = wf_create_20MHz_chspec(channel, band);
	if (chanspec == INVCHANSPEC) {
		return chanspec;
	}

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

	bzero(n, ETHER_ADDR_LEN);
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
#ifdef WL_CLIENT_SAE
		| BIT(IEEE80211_STYPE_AUTH >> 4)
#endif /* WL_CLIENT_SAE */
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

#if defined(WL_FW_OCE_AP_SELECT)
bool static wl_cfg80211_is_oce_ap(struct wiphy *wiphy, const u8 *bssid_hint)
{
	const u8 *parse = NULL;
	bcm_tlv_t *ie;
	const struct cfg80211_bss_ies *ies;
	u32 len;
	struct cfg80211_bss *bss;

	bss = CFG80211_GET_BSS(wiphy, NULL, bssid_hint, 0, 0);
	if (!bss) {
		WL_ERR(("Unable to find AP in the cache"));
		return false;
	}

	if (rcu_access_pointer(bss->ies)) {
		ies = rcu_access_pointer(bss->ies);
		parse = ies->data;
		len = ies->len;
	} else {
		WL_ERR(("ies is NULL"));
		return false;
	}

	while ((ie = bcm_parse_tlvs(parse, len, DOT11_MNG_VS_ID))) {
		if (wl_cfgoce_is_oce_ie((const uint8*)ie, (u8 const **)&parse, &len) == TRUE) {
			return true;
		} else {
			ie = bcm_next_tlv((const bcm_tlv_t*) ie, &len);
			if (!ie) {
				return false;
			}
			parse = (uint8 *)ie;
			WL_DBG(("NON OCE IE. next ie ptr:%p", parse));
		}
	}
	WL_DBG(("OCE IE NOT found"));
	return false;
}
#endif /* WL_FW_OCE_AP_SELECT */

/* Dump the contents of the encoded wps ie buffer and get pbc value */
static void
wl_validate_wps_ie(const char *wps_ie, s32 wps_ie_len, bool *pbc)
{
	#define WPS_IE_FIXED_LEN 6
	s16 len;
	const u8 *subel = NULL;
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
			char devname[33];
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
#ifdef SUPPORT_WL_TXPOWER
	if (type == NL80211_TX_POWER_AUTOMATIC)
		txpwrqdbm = 127;
	else
		txpwrqdbm |= WL_TXPWR_OVERRIDE;
#endif /* SUPPORT_WL_TXPOWER */
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
	char ioctl_buf[WLC_IOCTL_SMLEN];

	err = wldev_iovar_getbuf_bsscfg(dev, "qtxpower",
		NULL, 0, ioctl_buf, WLC_IOCTL_SMLEN, 0, NULL);
	if (unlikely(err)) {
		WL_ERR(("error (%d)\n", err));
		return err;
	}

	memcpy(&txpwrdbm, ioctl_buf, sizeof(txpwrdbm));
	txpwrdbm = dtoh32(txpwrdbm);
	*dbm = (txpwrdbm & ~WL_TXPWR_OVERRIDE) / 4;

	WL_DBG(("dBm=%d, txpwrdbm=0x%x\n", *dbm, txpwrdbm));

	return err;
}

chanspec_t wl_cfg80211_get_shared_freq(struct wiphy *wiphy)
{
	chanspec_t chspec;
	int cur_band, err = 0;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct net_device *dev = bcmcfg_to_prmry_ndev(cfg);
	struct ether_addr bssid;
	wl_bss_info_t *bss = NULL;
	u16 channel = WL_P2P_TEMP_CHAN;
	char *buf;

	bzero(&bssid, sizeof(bssid));
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
		} else if (cur_band == WLC_BAND_5G || cur_band == WLC_BAND_6G) {
			channel = WL_P2P_TEMP_CHAN_5G;
		}
		return wl_ch_host_to_driver(channel);
	}

	buf = (char *)MALLOCZ(cfg->osh, WL_EXTRA_BUF_MAX);
	if (!buf) {
		WL_ERR(("buf alloc failed. use temp channel\n"));
		return wl_ch_host_to_driver(channel);
	}

	*(u32 *)buf = htod32(WL_EXTRA_BUF_MAX);
	if ((err = wldev_ioctl_get(dev, WLC_GET_BSS_INFO, buf,
		WL_EXTRA_BUF_MAX))) {
			WL_ERR(("Failed to get associated bss info, use temp channel \n"));
			chspec = wl_ch_host_to_driver(channel);
	}
	else {
			bss = (wl_bss_info_t *) (buf + 4);
			chspec =  bss->chanspec;
#ifdef WL_6G_BAND
			/* Avoid p2p bring up in 6G based on bssinfo */
			if (CHSPEC_IS6G(chspec)) {
				channel = WL_P2P_TEMP_CHAN_5G;
				chspec = wl_ch_host_to_driver(channel);
			}
#endif /* WL_6G_BAND */
			WL_DBG(("Valid BSS Found. chanspec:%d \n", chspec));
	}

	MFREE(cfg->osh, buf, WL_EXTRA_BUF_MAX);
	return chspec;
}

void
wl_wlfc_enable(struct bcm_cfg80211 *cfg, bool enable)
{
#ifdef PROP_TXSTATUS_VSDB
#if defined(BCMSDIO) || defined(BCMDBUS)
	bool wlfc_enabled = FALSE;
	s32 err, up =1;
	dhd_pub_t *dhd;
	struct net_device *primary_ndev = bcmcfg_to_prmry_ndev(cfg);

	dhd = (dhd_pub_t *)(cfg->pub);
	if (!dhd) {
		return;
	}

	if (enable) {
		if (!cfg->wlfc_on && !disable_proptx) {
			dhd_wlfc_get_enable(dhd, &wlfc_enabled);
			if (!wlfc_enabled && dhd->op_mode != DHD_FLAG_HOSTAP_MODE &&
				dhd->op_mode != DHD_FLAG_IBSS_MODE) {
				dhd_wlfc_init(dhd);
				err = wldev_ioctl_set(primary_ndev, WLC_UP, &up, sizeof(s32));
				if (err < 0)
					WL_ERR(("WLC_UP return err:%d\n", err));
			}
			cfg->wlfc_on = true;
			WL_DBG(("wlfc_on:%d \n", cfg->wlfc_on));
		}
	} else if (dhd->conf->disable_proptx != 0){
			dhd_wlfc_deinit(dhd);
			cfg->wlfc_on = false;
	}
#endif /* BCMSDIO || BCMDBUS */
#endif /* PROP_TXSTATUS_VSDB */
}

struct wireless_dev *
wl_cfg80211_p2p_if_add(struct bcm_cfg80211 *cfg,
	wl_iftype_t wl_iftype,
	char const *name, u8 *mac_addr, s32 *ret_err)
{
	u16 chspec;
	s16 cfg_type;
	long timeout;
	s32 err;
	u16 p2p_iftype;
	int dhd_mode;
	struct net_device *new_ndev = NULL;
	struct wiphy *wiphy = bcmcfg_to_wiphy(cfg);
	struct ether_addr *p2p_addr;

	*ret_err = BCME_OK;
	if (!cfg->p2p) {
		WL_ERR(("p2p not initialized\n"));
		return NULL;
	}

#if defined(WL_CFG80211_P2P_DEV_IF)
	if (wl_iftype == WL_IF_TYPE_P2P_DISC) {
		/* Handle Dedicated P2P discovery Interface */
		return wl_cfgp2p_add_p2p_disc_if(cfg);
	}
#endif /* WL_CFG80211_P2P_DEV_IF */

	if (wl_iftype == WL_IF_TYPE_P2P_GO) {
		p2p_iftype = WL_P2P_IF_GO;
	} else {
		p2p_iftype = WL_P2P_IF_CLIENT;
	}

	/* Dual p2p doesn't support multiple P2PGO interfaces,
	 * p2p_go_count is the counter for GO creation
	 * requests.
	 */
	if ((cfg->p2p->p2p_go_count > 0) && (wl_iftype == WL_IF_TYPE_P2P_GO)) {
		WL_ERR(("FW does not support multiple GO\n"));
		*ret_err = -ENOTSUPP;
		return NULL;
	}
	if (!cfg->p2p->on) {
		p2p_on(cfg) = true;
		wl_cfgp2p_set_firm_p2p(cfg);
		wl_cfgp2p_init_discovery(cfg);
	}

	strlcpy(cfg->p2p->vir_ifname, name, sizeof(cfg->p2p->vir_ifname));
	/* In concurrency case, STA may be already associated in a particular channel.
	 * so retrieve the current channel of primary interface and then start the virtual
	 * interface on that.
	 */
	 chspec = wl_cfg80211_get_shared_freq(wiphy);

	/* For P2P mode, use P2P-specific driver features to create the
	 * bss: "cfg p2p_ifadd"
	 */
	wl_set_p2p_status(cfg, IF_ADDING);
	bzero(&cfg->if_event_info, sizeof(cfg->if_event_info));
	cfg_type = wl_cfgp2p_get_conn_idx(cfg);
	if (cfg_type < BCME_OK) {
		wl_clr_p2p_status(cfg, IF_ADDING);
		WL_ERR(("Failed to get connection idx for p2p interface"
			", error code = %d\n", cfg_type));
		return NULL;
	}

	p2p_addr = wl_to_p2p_bss_macaddr(cfg, cfg_type);
	memcpy(p2p_addr->octet, mac_addr, ETH_ALEN);

	err = wl_cfgp2p_ifadd(cfg, p2p_addr,
		htod32(p2p_iftype), chspec);
	if (unlikely(err)) {
		wl_clr_p2p_status(cfg, IF_ADDING);
		WL_ERR((" virtual iface add failed (%d) \n", err));
		return NULL;
	}

	/* Wait for WLC_E_IF event with IF_ADD opcode */
	timeout = wait_event_interruptible_timeout(cfg->netif_change_event,
		((wl_get_p2p_status(cfg, IF_ADDING) == false) &&
		(cfg->if_event_info.valid)),
		msecs_to_jiffies(MAX_WAIT_TIME));
	if (timeout > 0 && !wl_get_p2p_status(cfg, IF_ADDING) && cfg->if_event_info.valid) {
		wl_if_event_info *event = &cfg->if_event_info;
		new_ndev = wl_cfg80211_post_ifcreate(bcmcfg_to_prmry_ndev(cfg), event,
			event->mac, cfg->p2p->vir_ifname, false);
		if (unlikely(!new_ndev)) {
			goto fail;
		}

		if (wl_iftype == WL_IF_TYPE_P2P_GO) {
			cfg->p2p->p2p_go_count++;
		}
		/* Fill p2p specific data */
		wl_to_p2p_bss_ndev(cfg, cfg_type) = new_ndev;
		wl_to_p2p_bss_bssidx(cfg, cfg_type) = event->bssidx;

		WL_ERR((" virtual interface(%s) is "
			"created net attach done\n", cfg->p2p->vir_ifname));
#if defined(BCMDONGLEHOST)
		dhd_mode = (wl_iftype == WL_IF_TYPE_P2P_GC) ?
			DHD_FLAG_P2P_GC_MODE : DHD_FLAG_P2P_GO_MODE;
		DNGL_FUNC(dhd_cfg80211_set_p2p_info, (cfg, dhd_mode));
#endif /* defined(BCMDONGLEHOST) */
			/* reinitialize completion to clear previous count */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0))
			INIT_COMPLETION(cfg->iface_disable);
#else
			init_completion(&cfg->iface_disable);
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0) */

			return new_ndev->ieee80211_ptr;
	}

fail:
	return NULL;
}

void
wl_cfg80211_iface_state_ops(struct wireless_dev *wdev,
	wl_interface_state_t state,
	wl_iftype_t wl_iftype, u16 wl_mode)
{
	struct net_device *ndev;
	struct bcm_cfg80211 *cfg;
	dhd_pub_t *dhd;
	s32 bssidx;

	WL_DBG(("state:%s wl_iftype:%d mode:%d\n",
		wl_if_state_strs[state], wl_iftype, wl_mode));
	if (!wdev) {
		WL_ERR(("wdev null\n"));
		return;
	}

	if ((wl_iftype == WL_IF_TYPE_P2P_DISC) || (wl_iftype == WL_IF_TYPE_NAN_NMI)) {
		/* P2P discovery is a netless device and uses a
		 * hidden bsscfg interface in fw. Don't apply the
		 * iface ops state changes for p2p discovery I/F.
		 * NAN NMI is netless device and uses a hidden bsscfg interface in fw.
		 * Don't apply iface ops state changes for NMI I/F.
		 */
		return;
	}

	cfg = wiphy_priv(wdev->wiphy);
	ndev = wdev->netdev;
	dhd = (dhd_pub_t *)(cfg->pub);

	bssidx = wl_get_bssidx_by_wdev(cfg, wdev);
	if (!ndev || (bssidx < 0)) {
		WL_ERR(("ndev null. skip iface state ops\n"));
		return;
	}

	switch (state) {
		case WL_IF_CREATE_REQ:
#ifdef WL_BCNRECV
			/* check fakeapscan in progress then abort */
			wl_android_bcnrecv_stop(ndev, WL_BCNRECV_CONCURRENCY);
#endif /* WL_BCNRECV */
			wl_cfgscan_cancel_scan(cfg);
			wl_wlfc_enable(cfg, true);
#ifdef WLTDLS
			/* disable TDLS if number of connected interfaces is >= 1 */
			wl_cfg80211_tdls_config(cfg, TDLS_STATE_IF_CREATE, false);
#endif /* WLTDLS */
			break;
		case WL_IF_DELETE_REQ:
#ifdef WL_WPS_SYNC
			wl_wps_handle_ifdel(ndev);
#endif /* WPS_SYNC */
			if (wl_get_drv_status(cfg, SCANNING, ndev)) {
				/* Send completion for any pending scans */
				wl_cfgscan_cancel_scan(cfg);
			}

#ifdef CUSTOM_SET_CPUCORE
			dhd->chan_isvht80 &= ~DHD_FLAG_P2P_MODE;
			if (!(dhd->chan_isvht80)) {
				dhd_set_cpucore(dhd, FALSE);
			}
#endif /* CUSTOM_SET_CPUCORE */
			 wl_add_remove_pm_enable_work(cfg, WL_PM_WORKQ_DEL);
			break;
		case WL_IF_CREATE_DONE:
			if (wl_mode == WL_MODE_BSS) {
				/* Common code for sta type interfaces - STA, GC */
				/* Enable firmware key buffering before sent 4-way M4 */
				wldev_iovar_setint(ndev, "buf_key_b4_m4", 1);
			}
			if (wl_iftype == WL_IF_TYPE_P2P_GC) {
				/* Disable firmware roaming for P2P interface  */
				wldev_iovar_setint(ndev, "roam_off", 1);
				wldev_iovar_setint(ndev, "bcn_timeout", dhd->conf->bcn_timeout);
				{
					int assoc_retry = 3;
#if defined(CUSTOM_ASSOC_RETRY_MAX)
					assoc_retry = CUSTOM_ASSOC_RETRY_MAX;
#endif /* CUSTOM_ASSOC_RETRY_MAX */
					/* set retry_max to CUSTOM_ASSOC_RETRY_MAX(3) */
					wldev_iovar_setint(ndev, "assoc_retry_max", assoc_retry);
				}
			}
			if (wl_mode == WL_MODE_AP) {
				/* Common code for AP/GO */
#if defined(SUPPORT_AP_POWERSAVE) && defined(BCMDONGLEHOST)
				dhd_set_ap_powersave(dhd, 0, TRUE);
#endif /* SUPPORT_AP_POWERSAVE && BCMDONGLEHOST */
			}
			break;
		case WL_IF_DELETE_DONE:
#ifdef WLTDLS
			/* Enable back TDLS if connected interface is <= 1 */
			wl_cfg80211_tdls_config(cfg, TDLS_STATE_IF_DELETE, false);
#endif /* WLTDLS */
			wl_wlfc_enable(cfg, false);
			break;
		case WL_IF_CHANGE_REQ:
			/* Flush existing IEs from firmware on role change */
			wl_cfg80211_clear_per_bss_ies(cfg, wdev);
			break;
		case WL_IF_CHANGE_DONE:
			if (wl_mode == WL_MODE_BSS) {
#ifdef SUPPORT_AP_POWERSAVE
				dhd_set_ap_powersave(dhd, 0, FALSE);
#endif /* SUPPORT_AP_POWERSAVE */
				/* Enable buffering of PTK key till EAPOL 4/4 is sent out */
				wldev_iovar_setint(ndev, "buf_key_b4_m4", 1);
			}
			break;

		default:
			WL_ERR(("Unsupported state: %d\n", state));
			return;
	}
}

static s32
wl_cfg80211_p2p_if_del(struct wiphy *wiphy, struct wireless_dev *wdev)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	s16 bssidx;
	s16 err;
	s32 cfg_type;
	struct net_device *ndev;
	long timeout;
	struct ether_addr p2p_dev_addr = {{0}};

	if (unlikely(!wl_get_drv_status(cfg, READY, bcmcfg_to_prmry_ndev(cfg)))) {
		WL_INFORM_MEM(("device is not ready\n"));
		return BCME_NOTFOUND;
	}

#ifdef WL_CFG80211_P2P_DEV_IF
	if (wdev->iftype == NL80211_IFTYPE_P2P_DEVICE) {
		/* Handle dedicated P2P discovery interface. */
		return wl_cfgp2p_del_p2p_disc_if(wdev, cfg);
	}
#endif /* WL_CFG80211_P2P_DEV_IF */

	/* Handle P2P Group Interface */
	bssidx = wl_get_bssidx_by_wdev(cfg, wdev);
	if (bssidx <= 0) {
		WL_ERR(("bssidx not found\n"));
		return BCME_NOTFOUND;
	}
	if (wl_cfgp2p_find_type(cfg, bssidx, &cfg_type) != BCME_OK) {
		/* Couldn't find matching iftype */
		WL_MEM(("non P2P interface\n"));
		return BCME_NOTFOUND;
	}

	ndev = wdev->netdev;
	(void)memcpy_s(p2p_dev_addr.octet, ETHER_ADDR_LEN,
		ndev->dev_addr, ETHER_ADDR_LEN);

	wl_clr_p2p_status(cfg, GO_NEG_PHASE);
	wl_clr_p2p_status(cfg, IF_ADDING);

	/* for GO */
	if (wl_get_mode_by_netdev(cfg, ndev) == WL_MODE_AP) {
		wl_add_remove_eventmsg(ndev, WLC_E_PROBREQ_MSG, false);
		cfg->p2p->p2p_go_count--;
		/* disable interface before bsscfg free */
		err = wl_cfgp2p_ifdisable(cfg, &p2p_dev_addr);
		/* if fw doesn't support "ifdis",
		   do not wait for link down of ap mode
		 */
		if (err == 0) {
			WL_ERR(("Wait for Link Down event for GO !!!\n"));
			wait_for_completion_timeout(&cfg->iface_disable,
				msecs_to_jiffies(500));
		} else if (err != BCME_UNSUPPORTED) {
			msleep(300);
		}
	} else {
		/* GC case */
		if (wl_get_drv_status(cfg, DISCONNECTING, ndev)) {
			WL_ERR(("Wait for Link Down event for GC !\n"));
			wait_for_completion_timeout
					(&cfg->iface_disable, msecs_to_jiffies(500));
		}

		/* Force P2P disconnect in iface down context */
		if (wl_get_drv_status(cfg, CONNECTED, ndev)) {
			WL_INFORM_MEM(("force send disconnect event\n"));
			CFG80211_DISCONNECTED(ndev, 0, NULL, 0, false, GFP_KERNEL);
			wl_clr_drv_status(cfg, AUTHORIZED, ndev);
		}
	}

	bzero(&cfg->if_event_info, sizeof(cfg->if_event_info));
	wl_set_p2p_status(cfg, IF_DELETING);
	DNGL_FUNC(dhd_cfg80211_clean_p2p_info, (cfg));

	err = wl_cfgp2p_ifdel(cfg, &p2p_dev_addr);
	if (unlikely(err)) {
		WL_ERR(("P2P IFDEL operation failed, error code = %d\n", err));
		err = BCME_ERROR;
		goto fail;
	} else {
		/* Wait for WLC_E_IF event */
		timeout = wait_event_interruptible_timeout(cfg->netif_change_event,
			((wl_get_p2p_status(cfg, IF_DELETING) == false) &&
			(cfg->if_event_info.valid)),
			msecs_to_jiffies(MAX_WAIT_TIME));
		if (timeout > 0 && !wl_get_p2p_status(cfg, IF_DELETING) &&
			cfg->if_event_info.valid) {
			WL_ERR(("P2P IFDEL operation done\n"));
			err = BCME_OK;
		} else {
			WL_ERR(("IFDEL didn't complete properly\n"));
			err = -EINVAL;
		}
	}

fail:
	/* Even in failure case, attempt to remove the host data structure.
	 * Firmware would be cleaned up via WiFi reset done by the
	 * user space from hang event context (for android only).
	 */
	bzero(cfg->p2p->vir_ifname, IFNAMSIZ);
	wl_to_p2p_bss_bssidx(cfg, cfg_type) = -1;
	wl_to_p2p_bss_ndev(cfg, cfg_type) = NULL;
	wl_clr_drv_status(cfg, CONNECTED, wl_to_p2p_bss_ndev(cfg, cfg_type));

	/* Clear our saved WPS and P2P IEs for the discovery BSS */
	wl_cfg80211_clear_p2p_disc_ies(cfg);
#ifdef BCMDONGLEHOST
	dhd_net_if_lock(ndev);
#endif /* BCMDONGLEHOST */
	if (cfg->if_event_info.ifidx) {
		/* Remove interface except for primary ifidx */
		wl_cfg80211_remove_if(cfg, cfg->if_event_info.ifidx, ndev, FALSE);
	}
#ifdef BCMDONGLEHOST
	dhd_net_if_unlock(ndev);
#endif /* BCMDONGLEHOST */
	return err;
}

static struct wireless_dev *
wl_cfg80211_add_monitor_if(struct wiphy *wiphy, const char *name)
{
#if defined(WL_ENABLE_P2P_IF) || defined(WL_CFG80211_P2P_DEV_IF)
	WL_ERR(("wl_cfg80211_add_monitor_if: No more support monitor interface\n"));
	return ERR_PTR(-EOPNOTSUPP);
#else
	struct wireless_dev *wdev;
	struct net_device* ndev = NULL;

	dhd_add_monitor(name, &ndev);

	wdev = kzalloc(sizeof(*wdev), GFP_KERNEL);
	if (!wdev) {
		WL_ERR(("wireless_dev alloc failed! \n"));
		goto fail;
	}

	wdev->wiphy = wiphy;
	wdev->iftype = NL80211_IFTYPE_MONITOR;
	ndev->ieee80211_ptr = wdev;
	SET_NETDEV_DEV(ndev, wiphy_dev(wiphy));

	WL_DBG(("wl_cfg80211_add_monitor_if net device returned: 0x%p\n", ndev));
	return ndev->ieee80211_ptr;
fail:
	return ERR_PTR(-EOPNOTSUPP);
#endif // endif
}

static struct wireless_dev *
wl_cfg80211_add_ibss(struct wiphy *wiphy, u16 wl_iftype, char const *name)
{
#ifdef WLAIBSS_MCHAN
	/* AIBSS */
	return bcm_cfg80211_add_ibss_if(wiphy, (char *)name);
#else
	/* Normal IBSS */
	WL_ERR(("IBSS not supported on Virtual iface\n"));
	return NULL;
#endif
}

#ifdef DNGL_AXI_ERROR_LOGGING
static s32
_wl_cfg80211_check_axi_error(struct bcm_cfg80211 *cfg)
{
	s32 ret = BCME_OK;
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);
	hnd_ext_trap_hdr_t *hdr;
	int axi_host_error_size;
	uint8 *new_dst;
	uint32 *ext_data = dhd->extended_trap_data;
	struct file *fp = NULL;
	char *filename = DHD_COMMON_DUMP_PATH
			 DHD_DUMP_AXI_ERROR_FILENAME
			 DHD_DUMP_HAL_FILENAME_SUFFIX;

	WL_ERR(("%s: starts to read %s. Axi error \n", __FUNCTION__, filename));

	fp = filp_open(filename, O_RDONLY, 0);

	if (IS_ERR(fp) || (fp == NULL)) {
		WL_ERR(("%s: Couldn't read the file, err %ld,File [%s]  No previous axi error \n",
			__FUNCTION__, PTR_ERR(fp), filename));
		return ret;
	}

	kernel_read_compat(fp, fp->f_pos, (char *)dhd->axi_err_dump, sizeof(dhd_axi_error_dump_t));
	filp_close(fp, NULL);

	/* Delete axi error info file */
	if (dhd_file_delete(filename) < 0) {
		WL_ERR(("%s(): Failed to delete file: %s\n", __FUNCTION__, filename));
		return ret;
	}
	WL_ERR(("%s(): Success to delete file: %s\n", __FUNCTION__, filename));

	if (dhd->axi_err_dump->etd_axi_error_v1.signature != HND_EXT_TRAP_AXIERROR_SIGNATURE) {
		WL_ERR(("%s: Invalid AXI signature: 0x%x\n",
		__FUNCTION__, dhd->axi_err_dump->etd_axi_error_v1.signature));
	}

	/* First word is original trap_data */
	ext_data++;

	/* Followed by the extended trap data header */
	hdr = (hnd_ext_trap_hdr_t *)ext_data;
	new_dst = hdr->data;

	axi_host_error_size =  sizeof(dhd->axi_err_dump->axid)
		+ sizeof(dhd->axi_err_dump->fault_address);

	/* TAG_TRAP_AXI_HOST_INFO tlv : host's axid, fault address */
	new_dst = bcm_write_tlv(TAG_TRAP_AXI_HOST_INFO,
			(const void *)dhd->axi_err_dump,
			axi_host_error_size, new_dst);

	/* TAG_TRAP_AXI_ERROR tlv */
	new_dst = bcm_write_tlv(TAG_TRAP_AXI_ERROR,
			(const void *)&dhd->axi_err_dump->etd_axi_error_v1,
			sizeof(dhd->axi_err_dump->etd_axi_error_v1), new_dst);
	hdr->len = new_dst - hdr->data;

	dhd->dongle_trap_occured = TRUE;
#ifdef WL_CFGVENDOR_SEND_HANG_EVENT
	copy_hang_info_trap(dhd);
#endif /* WL_CFGVENDOR_SEND_HANG_EVENT */
	memset(dhd->axi_err_dump, 0, sizeof(dhd_axi_error_dump_t));

	dhd->hang_reason = HANG_REASON_DONGLE_TRAP;
	net_os_send_hang_message(bcmcfg_to_prmry_ndev(cfg));
	ret = BCME_ERROR;
	return ret;
}
#endif /* DNGL_AXI_ERROR_LOGGING */

/* All Android/Linux private/Vendor Interface calls should make
 *  use of below API for interface creation.
 */
struct wireless_dev *
wl_cfg80211_add_if(struct bcm_cfg80211 *cfg,
	struct net_device *primary_ndev,
	wl_iftype_t wl_iftype, const char *name, u8 *mac)
{
	u8 mac_addr[ETH_ALEN];
	s32 err = -ENODEV;
	struct wireless_dev *wdev = NULL;
	struct wiphy *wiphy;
	s32 wl_mode;
#if defined(BCMDONGLEHOST)
	dhd_pub_t *dhd;
#endif /* BCMDONGLEHOST */
	wl_iftype_t macaddr_iftype = wl_iftype;

	WL_INFORM_MEM(("if name: %s, wl_iftype:%d \n",
		name ? name : "NULL", wl_iftype));
	if (!cfg || !primary_ndev || !name) {
		WL_ERR(("cfg/ndev/name ptr null\n"));
		return NULL;
	}
	if (wl_cfg80211_get_wdev_from_ifname(cfg, name)) {
		WL_ERR(("Interface name %s exists!\n", name));
		return NULL;
	}

	wiphy = bcmcfg_to_wiphy(cfg);
#if defined(BCMDONGLEHOST)
	dhd = (dhd_pub_t *)(cfg->pub);
	if (!dhd) {
		return NULL;
	}
#endif /* BCMDONGLEHOST */

	if (dhd->op_mode == DHD_FLAG_HOSTAP_MODE) {
		WL_ERR(("Please check op_mode %d, name %s\n", dhd->op_mode, name));
		return NULL;
	}

	if ((wl_mode = wl_iftype_to_mode(wl_iftype)) < 0) {
		return NULL;
	}
	mutex_lock(&cfg->if_sync);
#ifdef WL_NAN
	if (wl_iftype == WL_IF_TYPE_NAN) {
	/*
	* Bypass the role conflict check for NDI and handle it
	* from dp req and dp resp context
	* because in aware comms, ndi gets created soon after nan enable.
	*/
	} else
#endif /* WL_NAN */
#ifdef WL_IFACE_MGMT
	/* Allow wdev interface creation for p2p discovery to avoid failures
	 * in user supplicant initialization. The role conflict rules will be
	 * applied from discovery context if userspace tries to use discovery.
	 */
	if ((wl_iftype != WL_IF_TYPE_P2P_DISC) &&
		(err = wl_cfg80211_handle_if_role_conflict(cfg, wl_iftype)) < 0) {
		mutex_unlock(&cfg->if_sync);
		return NULL;
	}
#endif /* WL_IFACE_MGMT */
#ifdef DNGL_AXI_ERROR_LOGGING
	/* Check the previous smmu fault error */
	if ((err = _wl_cfg80211_check_axi_error(cfg)) < 0) {
		mutex_unlock(&cfg->if_sync);
		return NULL;
	}
#endif /* DNGL_AXI_ERROR_LOGGING */
	/* Protect the interace op context */
	/* Do pre-create ops */
	wl_cfg80211_iface_state_ops(primary_ndev->ieee80211_ptr, WL_IF_CREATE_REQ,
		wl_iftype, wl_mode);

	if (strnicmp(name, SOFT_AP_IF_NAME, strlen(SOFT_AP_IF_NAME)) == 0) {
		macaddr_iftype = WL_IF_TYPE_AP;
	}

	if (mac) {
		/* If mac address is provided, use that */
		memcpy(mac_addr, mac, ETH_ALEN);
	} else if ((wl_get_vif_macaddr(cfg, macaddr_iftype, mac_addr) != BCME_OK)) {
		/* Fetch the mac address to be used for virtual interface */
		err = -EINVAL;
		goto fail;
	}

	switch (wl_iftype) {
		case WL_IF_TYPE_IBSS:
			wdev = wl_cfg80211_add_ibss(wiphy, wl_iftype, name);
			break;
		case WL_IF_TYPE_MONITOR:
			wdev = wl_cfg80211_add_monitor_if(wiphy, name);
			break;
		case WL_IF_TYPE_STA:
		case WL_IF_TYPE_AP:
		case WL_IF_TYPE_NAN:
			if (cfg->iface_cnt >= (IFACE_MAX_CNT - 1)) {
				WL_ERR(("iface_cnt exceeds max cnt. created iface_cnt: %d\n",
					cfg->iface_cnt));
				err = -ENOTSUPP;
				goto fail;
			}
			wdev = wl_cfg80211_create_iface(cfg->wdev->wiphy,
				wl_iftype, mac_addr, name);
			break;
		case WL_IF_TYPE_P2P_DISC:
		case WL_IF_TYPE_P2P_GO:
			/* Intentional fall through */
		case WL_IF_TYPE_P2P_GC:
			if (cfg->p2p_supported) {
				wdev = wl_cfg80211_p2p_if_add(cfg, wl_iftype,
					name, mac_addr, &err);
				break;
			}
			/* Intentionally fall through for unsupported interface
			 * handling when firmware doesn't support p2p
			 */
		default:
			WL_ERR(("Unsupported interface type\n"));
			err = -ENOTSUPP;
			goto fail;
	}

	if (!wdev) {
		WL_ERR(("vif create failed. err:%d\n", err));
		if (err != -ENOTSUPP) {
			err = -ENODEV;
		}
		goto fail;
	}

	/* Ensure decrementing in case of failure */
	cfg->vif_count++;

	wl_cfg80211_iface_state_ops(wdev,
		WL_IF_CREATE_DONE, wl_iftype, wl_mode);

	WL_INFORM_MEM(("Vif created. dev->ifindex:%d"
		" cfg_iftype:%d, vif_count:%d\n",
		(wdev->netdev ? wdev->netdev->ifindex : 0xff),
		wdev->iftype, cfg->vif_count));
	mutex_unlock(&cfg->if_sync);
	return wdev;

fail:
	wl_cfg80211_iface_state_ops(primary_ndev->ieee80211_ptr,
		WL_IF_DELETE_REQ, wl_iftype, wl_mode);

	if (err != -ENOTSUPP) {
		/* For non-supported interfaces, just return error and
		 * skip below recovery steps.
		 */
#ifdef WL_CFGVENDOR_SEND_HANG_EVENT
		wl_copy_hang_info_if_falure(primary_ndev, HANG_REASON_IFACE_DEL_FAILURE, err);
#endif /* WL_CFGVENDOR_SEND_HANG_EVENT */
		SUPP_LOG(("IF_ADD fail. err:%d\n", err));
		wl_flush_fw_log_buffer(primary_ndev, FW_LOGSET_MASK_ALL);
#if defined(BCMDONGLEHOST)
		if (dhd_query_bus_erros(dhd)) {
			goto exit;
		}
		dhd->iface_op_failed = TRUE;
#endif /* BCMDONGLEHOST */
#if defined(DHD_DEBUG) && defined(BCMPCIE) && defined(DHD_FW_COREDUMP)
		if (dhd->memdump_enabled) {
			dhd->memdump_type = DUMP_TYPE_IFACE_OP_FAILURE;
			dhd_bus_mem_dump(dhd);
		}
#endif /* DHD_DEBUG && BCMPCIE && DHD_FW_COREDUMP */

#if defined(BCMDONGLEHOST) && defined(OEM_ANDROID)
		/* If reached here, something wrong with DHD or firmware.
		 * There could be a chance that firmware is in bad state.
		 * Request the upper layer to do a Wi-Fi reset.
		 */
		dhd->hang_reason = HANG_REASON_IFACE_ADD_FAILURE;
		net_os_send_hang_message(bcmcfg_to_prmry_ndev(cfg));
#endif /* BCMDONGLEHOST && OEM_ANDROID */

	}
exit:
	mutex_unlock(&cfg->if_sync);
	return NULL;
}

static s32
wl_cfg80211_del_ibss(struct wiphy *wiphy, struct wireless_dev *wdev)
{
	WL_INFORM_MEM(("del ibss wdev_ptr:%p\n", wdev));
#ifdef WLAIBSS_MCHAN
	/* AIBSS */
	return bcm_cfg80211_del_ibss_if(wiphy, wdev);
#else
	/* Normal IBSS */
	return wl_cfg80211_del_iface(wiphy, wdev);
#endif
}

s32
wl_cfg80211_del_if(struct bcm_cfg80211 *cfg, struct net_device *primary_ndev,
	struct wireless_dev *wdev, char *ifname)
{
	int ret = BCME_OK;
	mutex_lock(&cfg->if_sync);
	ret = _wl_cfg80211_del_if(cfg, primary_ndev, wdev, ifname);
	mutex_unlock(&cfg->if_sync);
	return ret;
}

s32
_wl_cfg80211_del_if(struct bcm_cfg80211 *cfg, struct net_device *primary_ndev,
	struct wireless_dev *wdev, char *ifname)
{
	int ret = BCME_OK;
	s32 bssidx;
	struct wiphy *wiphy;
	u16 wl_mode;
	u16 wl_iftype;
	struct net_info *netinfo;
#if defined(BCMDONGLEHOST)
	dhd_pub_t *dhd;
	BCM_REFERENCE(dhd);
#endif /* BCMDONGLEHOST */

	if (!cfg) {
		return -EINVAL;
	}

#if defined(BCMDONGLEHOST)
	dhd = (dhd_pub_t *)(cfg->pub);
#endif /* BCMDONGLEHOST */

	if (!wdev && ifname) {
		/* If only ifname is provided, fetch corresponding wdev ptr from our
		 * internal data structure
		 */
		wdev = wl_cfg80211_get_wdev_from_ifname(cfg, ifname);
	}

	/* Check whether we have a valid wdev ptr */
	if (unlikely(!wdev)) {
		WL_ERR(("wdev not found. '%s' does not exists\n", ifname));
		return -ENODEV;
	}

	WL_INFORM_MEM(("del vif. wdev cfg_iftype:%d\n", wdev->iftype));

	wiphy = wdev->wiphy;
#ifdef WL_CFG80211_P2P_DEV_IF
	if (wdev->iftype == NL80211_IFTYPE_P2P_DEVICE) {
		/* p2p discovery would be de-initialized in stop p2p
		 * device context/from other virtual i/f creation context
		 * so netinfo list may not have any node corresponding to
		 * discovery I/F. Handle it before bssidx check.
		 */
		ret = wl_cfg80211_p2p_if_del(wiphy, wdev);
		if (unlikely(ret)) {
			goto exit;
		} else {
			/* success case. return from here */
			if (cfg->vif_count) {
				cfg->vif_count--;
			}
			return BCME_OK;
		}
	}
#endif /* WL_CFG80211_P2P_DEV_IF */

	if ((netinfo = wl_get_netinfo_by_wdev(cfg, wdev)) == NULL) {
		WL_ERR(("Find netinfo from wdev %p failed\n", wdev));
		ret = -ENODEV;
		goto exit;
	}

	if (!wdev->netdev) {
		WL_ERR(("ndev null! \n"));
	} else {
		/* Disable tx before del */
		netif_tx_disable(wdev->netdev);
	}

	wl_iftype = netinfo->iftype;
	wl_mode = wl_iftype_to_mode(wl_iftype);
	bssidx = netinfo->bssidx;
	WL_DBG_MEM(("[IFDEL] cfg_iftype:%d wl_iftype:%d mode:%d bssidx:%d\n",
		wdev->iftype, wl_iftype, wl_mode, bssidx));

	/* Do pre-interface del ops */
	wl_cfg80211_iface_state_ops(wdev, WL_IF_DELETE_REQ, wl_iftype, wl_mode);

#ifdef PCIE_FULL_DONGLE
	/* clean up sta info & clean up flowrings correspondign to the iface */
	dhd_net_del_flowrings_sta(dhd, wdev->netdev);
#endif /* PCIE_FULL_DONGLE */

	switch (wl_iftype) {
		case WL_IF_TYPE_P2P_GO:
		case WL_IF_TYPE_P2P_GC:
		case WL_IF_TYPE_AP:
		case WL_IF_TYPE_STA:
		case WL_IF_TYPE_NAN:
			ret = wl_cfg80211_del_iface(wiphy, wdev);
			break;
		case WL_IF_TYPE_IBSS:
			ret = wl_cfg80211_del_ibss(wiphy, wdev);
			break;

		default:
			WL_ERR(("Unsupported interface type\n"));
			ret = BCME_ERROR;
	}

exit:
	if (ret == BCME_OK) {
		/* Successful case */
		if (cfg->vif_count) {
			cfg->vif_count--;
		}
		wl_cfg80211_iface_state_ops(primary_ndev->ieee80211_ptr,
				WL_IF_DELETE_DONE, wl_iftype, wl_mode);
#ifdef WL_NAN
		if (!((cfg->nancfg->mac_rand) && (wl_iftype == WL_IF_TYPE_NAN)))
#endif /* WL_NAN */
		{
			wl_release_vif_macaddr(cfg, wdev->netdev->dev_addr, wl_iftype);
		}
		WL_INFORM_MEM(("vif deleted. vif_count:%d\n", cfg->vif_count));
	} else {
		if (!wdev->netdev) {
			WL_ERR(("ndev null! \n"));
		} else {
			/* IF del failed. revert back tx queue status */
			netif_tx_start_all_queues(wdev->netdev);
		}

		/* Skip generating log files and sending HANG event
		 * if driver state is not READY
		 */
		if (wl_get_drv_status(cfg, READY, bcmcfg_to_prmry_ndev(cfg))) {
#ifdef WL_CFGVENDOR_SEND_HANG_EVENT
			wl_copy_hang_info_if_falure(primary_ndev,
				HANG_REASON_IFACE_DEL_FAILURE, ret);
#endif /* WL_CFGVENDOR_SEND_HANG_EVENT */
			SUPP_LOG(("IF_DEL fail. err:%d\n", ret));
			wl_flush_fw_log_buffer(primary_ndev, FW_LOGSET_MASK_ALL);
#if defined(BCMDONGLEHOST)
			/* IF dongle is down due to previous hang or other conditions, sending
			* one more hang notification is not needed.
			*/
			if (dhd_query_bus_erros(dhd) || (ret == BCME_DONGLE_DOWN)) {
				goto end;
			}
			dhd->iface_op_failed = TRUE;
#if defined(DHD_FW_COREDUMP)
			if (dhd->memdump_enabled && (ret != -EBADTYPE)) {
				dhd->memdump_type = DUMP_TYPE_IFACE_OP_FAILURE;
				dhd_bus_mem_dump(dhd);
			}
#endif /* DHD_FW_COREDUMP */
#endif /* BCMDONGLEHOST */

#if defined(BCMDONGLEHOST) && defined(OEM_ANDROID)
			WL_ERR(("Notify hang event to upper layer \n"));
			dhd->hang_reason = HANG_REASON_IFACE_DEL_FAILURE;
			net_os_send_hang_message(bcmcfg_to_prmry_ndev(cfg));
#endif /* BCMDONGLEHOST && OEM_ANDROID */

		}
	}
end:
	return ret;
}

s32
wl_cfg80211_notify_ifadd(struct net_device *dev,
	int ifidx, char *name, uint8 *mac, uint8 bssidx, uint8 role)
{
	bool ifadd_expected = FALSE;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	bool bss_pending_op = TRUE;

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
		bss_pending_op = FALSE;
	}

	if (ifadd_expected) {
		wl_if_event_info *if_event_info = &cfg->if_event_info;

		if_event_info->ifidx = ifidx;
		if_event_info->bssidx = bssidx;
		if_event_info->role = role;
		strlcpy(if_event_info->name, name, sizeof(if_event_info->name));
		if_event_info->name[IFNAMSIZ - 1] = '\0';
		if (mac)
			memcpy(if_event_info->mac, mac, ETHER_ADDR_LEN);

		/* Update bss pendig operation status */
		if (!bss_pending_op) {
			cfg->bss_pending_op = FALSE;
		}
		WL_INFORM_MEM(("IF_ADD ifidx:%d bssidx:%d role:%d\n",
			ifidx, bssidx, role));
		OSL_SMP_WMB();
		if_event_info->valid = TRUE;
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
	bool bss_pending_op = TRUE;

	if (wl_get_p2p_status(cfg, IF_DELETING)) {
		ifdel_expected = TRUE;
		wl_clr_p2p_status(cfg, IF_DELETING);
	} else if (cfg->bss_pending_op) {
		ifdel_expected = TRUE;
		bss_pending_op = FALSE;
	}

	if (ifdel_expected) {
		if_event_info->ifidx = ifidx;
		if_event_info->bssidx = bssidx;
		/* Update bss pendig operation status */
		if (!bss_pending_op) {
			cfg->bss_pending_op = FALSE;
		}
		WL_INFORM_MEM(("IF_DEL ifidx:%d bssidx:%d\n", ifidx, bssidx));
		OSL_SMP_WMB();
		if_event_info->valid = TRUE;
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
		OSL_SMP_WMB();
		wake_up_interruptible(&cfg->netif_change_event);
		return BCME_OK;
	}

	return BCME_ERROR;
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

#ifdef CUSTOM_LONG_RETRY_LIMIT
	if ((cmd == WLC_SET_LRL) &&
		(retry != CUSTOM_LONG_RETRY_LIMIT)) {
		WL_DBG(("CUSTOM_LONG_RETRY_LIMIT is used.Ignore configuration"));
		return err;
	}
#endif /* CUSTOM_LONG_RETRY_LIMIT */

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

void
wl_cfg80211_ibss_vsie_set_buffer(struct net_device *dev, vndr_ie_setbuf_t *ibss_vsie,
	int ibss_vsie_len)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	if (cfg != NULL && ibss_vsie != NULL) {
		if (cfg->ibss_vsie != NULL) {
			MFREE(cfg->osh, cfg->ibss_vsie, cfg->ibss_vsie_len);
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
		MFREE(cfg->osh, cfg->ibss_vsie, cfg->ibss_vsie_len);
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
		ioctl_buf = (char *)MALLOC(cfg->osh, WLC_IOCTL_MEDLEN);
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
		strlcpy(cfg->ibss_vsie->cmd, "del", sizeof(cfg->ibss_vsie->cmd));

		ret = wldev_iovar_setbuf_bsscfg(dev, "vndr_ie",
				cfg->ibss_vsie, cfg->ibss_vsie_len,
				ioctl_buf, WLC_IOCTL_MEDLEN, bssidx, NULL);
		WL_ERR(("ret=%d\n", ret));

		if (ret == BCME_OK) {
			/* Free & initialize VSIE */
			MFREE(cfg->osh, cfg->ibss_vsie, cfg->ibss_vsie_len);
			cfg->ibss_vsie_len = 0;
		}
end:
		if (ioctl_buf) {
			MFREE(cfg->osh, ioctl_buf, WLC_IOCTL_MEDLEN);
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
	long timeout;
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
	bzero(&aibss_if, sizeof(aibss_if));
	memcpy(&aibss_if.addr, &cfg->ibss_if_addr, sizeof(aibss_if.addr));
	aibss_if.chspec = 0;
	aibss_if.len = sizeof(aibss_if);

	cfg->bss_pending_op = TRUE;
	bzero(&cfg->if_event_info, sizeof(cfg->if_event_info));
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
	wdev = (struct wireless_dev *)MALLOCZ(cfg->osh, sizeof(*wdev));
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

	wl_alloc_netinfo(cfg, new_ndev, wdev, WL_IF_TYPE_IBSS,
		PM_ENABLE, event->bssidx, event->ifidx);
	cfg->ibss_cfgdev = ndev_to_cfgdev(new_ndev);
	WL_ERR(("IBSS interface %s created\n", new_ndev->name));
	return cfg->ibss_cfgdev;

fail:
	WL_ERR(("failed to create IBSS interface %s \n", name));
	cfg->bss_pending_op = FALSE;
	if (new_ndev)
		wl_cfg80211_remove_if(cfg, event->ifidx, new_ndev, FALSE);
	if (wdev) {
		MFREE(cfg->osh, wdev, sizeof(*wdev));
	}
	return NULL;
}

static s32
bcm_cfg80211_del_ibss_if(struct wiphy *wiphy, bcm_struct_cfgdev *cfgdev)
{
	int err = 0;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct net_device *ndev = NULL;
	struct net_device *primary_ndev = NULL;
	long timeout;

	if (!cfgdev || cfg->ibss_cfgdev != cfgdev || ETHER_ISNULLADDR(&cfg->ibss_if_addr.octet))
		return -EINVAL;
	ndev = (struct net_device *)cfgdev_to_ndev(cfg->ibss_cfgdev);
	primary_ndev = bcmcfg_to_prmry_ndev(cfg);

	cfg->bss_pending_op = TRUE;
	bzero(&cfg->if_event_info, sizeof(cfg->if_event_info));
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

s32
wl_cfg80211_to_fw_iftype(wl_iftype_t iftype)
{
	s32 ret = BCME_ERROR;

	switch (iftype) {
		case WL_IF_TYPE_AP:
			ret = WL_INTERFACE_TYPE_AP;
			break;
		case WL_IF_TYPE_STA:
			ret = WL_INTERFACE_TYPE_STA;
			break;
		case WL_IF_TYPE_NAN_NMI:
		case WL_IF_TYPE_NAN:
			ret = WL_INTERFACE_TYPE_NAN;
			break;
		case WL_IF_TYPE_P2P_DISC:
			ret = WL_INTERFACE_TYPE_P2P_DISC;
			break;
		case WL_IF_TYPE_P2P_GO:
			ret = WL_INTERFACE_TYPE_P2P_GO;
			break;
		case WL_IF_TYPE_P2P_GC:
			ret = WL_INTERFACE_TYPE_P2P_GC;
			break;

#ifdef WLAWDL
		case WL_IF_TYPE_AWDL:
			ret = WL_INTERFACE_TYPE_AWDL;
			break;
#endif /* WLAWDL */

		default:
			WL_ERR(("Unsupported type:%d \n", iftype));
			ret = -EINVAL;
			break;
	}
	return ret;
}

s32
wl_cfg80211_interface_ops(struct bcm_cfg80211 *cfg,
	struct net_device *ndev, s32 bsscfg_idx,
	wl_iftype_t cfg_iftype, s32 del, u8 *addr)
{
	s32 ret;
	struct wl_interface_create_v2 iface;
	wl_interface_create_v3_t iface_v3;
	wl_interface_create_v0_t iface_v0;
	struct wl_interface_info_v1 *info;
	wl_interface_info_v2_t *info_v2;
	wl_interface_info_v0_t *info_v0;
	uint32 ifflags = 0;
	bool use_iface_info_v2 = false;
	u8 ioctl_buf[WLC_IOCTL_SMLEN];
	s32 iftype;
#ifdef WLEASYMESH
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);
#endif /* WLEASYMESH */

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

	iftype = wl_cfg80211_to_fw_iftype(cfg_iftype);
	if (iftype < 0) {
		return -ENOTSUPP;
	}

	if (addr) {
		ifflags |= WL_INTERFACE_MAC_USE;
		if (wl_legacy_chip_check(ndev)) {
			iface.flags = ifflags;
			memcpy(&iface.mac_addr.octet, addr, ETH_ALEN);
		}
	}
#ifdef WLEASYMESH
	if (dhd->conf->fw_type == FW_TYPE_EZMESH && iftype == WL_INTERFACE_TYPE_AP) {
		// this can be removed for 4359
		ifflags |= WL_INTERFACE_TYPE_AP;
	}
#endif /* WLEASYMESH */

	/* Pass ver = 0 for fetching the interface_create iovar version */
	if (wl_legacy_chip_check(ndev)) {
		bzero(&iface_v0, sizeof(iface_v0));
		iface_v0.ver = WL_INTERFACE_CREATE_VER_0;
		iface_v0.flags = iftype | ifflags;
		if (addr) {
			memcpy(&iface_v0.mac_addr.octet, addr, ETH_ALEN);
		}
		ret = wldev_iovar_getbuf(ndev, "interface_create",
			&iface_v0, sizeof(struct wl_interface_create),
			ioctl_buf, sizeof(ioctl_buf), NULL);
		if (ret == 0) {
			info_v0 = (wl_interface_info_v0_t *)ioctl_buf;
			ret = info_v0->bsscfgidx;
			goto exit;
        }
	} else {
		ret = wldev_iovar_getbuf(ndev, "interface_create",
			&iface, sizeof(struct wl_interface_create_v2),
			ioctl_buf, sizeof(ioctl_buf), NULL);
	}
	if (ret == BCME_UNSUPPORTED) {
		WL_ERR(("interface_create iovar not supported\n"));
		return ret;
	} else if ((ret == 0) && *((uint32 *)ioctl_buf) == WL_INTERFACE_CREATE_VER_3) {
		WL_DBG(("interface_create version 3. flags:0x%x \n", ifflags));
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
	} else if ((ret == 0) &&
		((*((uint32 *)ioctl_buf) == WL_INTERFACE_CREATE_VER_2) ||
		(*((uint32 *)ioctl_buf) == WL_INTERFACE_CREATE_VER_1))) {
		/* Legacy firmware could invoke iovar and reply data
		 * directly. Do not try if return ver is not expected.
		 * On any other error, attempt with iovar version 2 */
		WL_DBG(("interface_create version 2. get_ver:%d ifflags:0x%x\n", ret, ifflags));
		iface.ver = WL_INTERFACE_CREATE_VER_2;
		iface.iftype = iftype;
		iface.flags = ifflags;
		if (addr) {
			memcpy(&iface.mac_addr.octet, addr, ETH_ALEN);
		}
		ret = wldev_iovar_getbuf(ndev, "interface_create",
			&iface, sizeof(struct wl_interface_create_v2),
			ioctl_buf, sizeof(ioctl_buf), NULL);
	} else {
		/* inteface_create version 0, need to add chipnum in bcmdevs_legacy.h
		 * and add new chip check at wl_legacy_chip_check function */
		WL_DBG(("interface_create version 0. get_ver:%d ifflags:0x%x\n", ret, ifflags));
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

exit:
#ifdef WLEASYMESH
	//Give fw more time to process interface_create
	if (dhd->conf->fw_type == FW_TYPE_EZMESH) {
		wl_delay(500);
	}
#endif /* WLEASYMESH */
	WL_DBG(("wl interface create success!! bssidx:%d \n", ret));
	return ret;
}

#ifdef CUSTOMER_HW6
#define BCM4355_REV_C1 0x0c
bool
wl_customer6_legacy_chip_check(struct bcm_cfg80211 *cfg,
	struct net_device *ndev)
{
	u32 chipnum;
	wlc_rev_info_t revinfo;
	int ret;

	/* Get the device rev info */
	bzero(&revinfo, sizeof(revinfo));
	ret = wldev_ioctl_get(ndev, WLC_GET_REVINFO, &revinfo, sizeof(revinfo));
	if (ret < 0) {
		WL_ERR(("wl_customer6_legacy_chip_check: GET revinfo FAILED. ret:%d\n", ret));
		ASSERT(0);
		return false;
	}

	chipnum = revinfo.chipnum;
	WL_DBG(("wl_customer6_legacy_chip_check: GET_REVINFO device 0x%x, vendor 0x%x,"
		" chipnum 0x%x\n",
		dtoh32(revinfo.deviceid), dtoh32(revinfo.vendorid), dtoh32(chipnum)));
	if (
#ifdef BCM4350_CHIP_ID
		(chipnum == BCM4350_CHIP_ID) ||
#endif /* BCM4350_CHIP_ID */
#ifdef BCM4355_CHIP_ID
		((chipnum == BCM4355_CHIP_ID) && (revinfo.chiprev < BCM4355_REV_C1)) ||
#endif /* BCM4355_CHIP_ID */
#ifdef BCM4345_CHIP_ID
		(chipnum == BCM4345_CHIP_ID) ||
#endif /* BCM4345_CHIP_ID */
		false) {
		/* WAR required */
		WL_DBG(("%s: Customer6 legacy chip identified\n", __FUNCTION__));
		return true;
	}

	return false;
}
#endif /* CUSTOMER_HW6 */

s32
wl_cfg80211_add_del_bss(struct bcm_cfg80211 *cfg,
	struct net_device *ndev, s32 bsscfg_idx,
	wl_iftype_t brcm_iftype, s32 del, u8 *addr)
{
	s32 ret = BCME_OK;
	s32 val = 0;

	struct {
		s32 cfg;
		s32 val;
		struct ether_addr ea;
	} bss_setbuf;

	WL_DBG(("wl_iftype:%d del:%d \n", brcm_iftype, del));

	bzero(&bss_setbuf, sizeof(bss_setbuf));

	/* AP=2, STA=3, up=1, down=0, val=-1 */
	if (del) {
		val = WLC_AP_IOV_OP_DELETE;
	} else if (brcm_iftype == WL_IF_TYPE_AP) {
		/* Add/role change to AP Interface */
		WL_DBG(("Adding AP Interface \n"));
		val = WLC_AP_IOV_OP_MANUAL_AP_BSSCFG_CREATE;
	} else if (brcm_iftype == WL_IF_TYPE_STA) {
		/* Add/role change to STA Interface */
		WL_DBG(("Adding STA Interface \n"));
		val = WLC_AP_IOV_OP_MANUAL_STA_BSSCFG_CREATE;
	} else {
		WL_ERR((" add_del_bss NOT supported for IFACE type:0x%x", brcm_iftype));
		return -EINVAL;
	}

	if (!del) {
		wl_ext_bss_iovar_war(ndev, &val);
	}

	bss_setbuf.cfg = htod32(bsscfg_idx);
	bss_setbuf.val = htod32(val);

	if (addr) {
		memcpy(&bss_setbuf.ea.octet, addr, ETH_ALEN);
	}

	WL_MSG(ndev->name, "wl bss %d bssidx:%d\n", val, bsscfg_idx);
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

	WL_INFORM_MEM(("wl bss -C %d %s\n", bsscfg_idx, bss_up ? "up" : "down"));
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

s32
wl_iftype_to_mode(wl_iftype_t iftype)
{
	s32 mode = BCME_ERROR;

	switch (iftype) {
		case WL_IF_TYPE_STA:
		case WL_IF_TYPE_P2P_GC:
		case WL_IF_TYPE_P2P_DISC:
			mode = WL_MODE_BSS;
			break;
		case WL_IF_TYPE_AP:
		case WL_IF_TYPE_P2P_GO:
			mode = WL_MODE_AP;
			break;
		case WL_IF_TYPE_NAN:
			mode = WL_MODE_NAN;
			break;

#ifdef WLAWDL
		case WL_IF_TYPE_AWDL:
			mode = WL_MODE_AWDL;
			break;
#endif /* WLAWDL */

		case WL_IF_TYPE_AIBSS:
			/* Intentional fall through */
		case WL_IF_TYPE_IBSS:
			mode = WL_MODE_IBSS;
			break;
#ifdef WLMESH_CFG80211
		case WL_IF_TYPE_MESH:
			mode = WL_MODE_MESH;
			break;
#endif /* WLMESH_CFG80211 */
		default:
			WL_ERR(("Unsupported type:%d\n", iftype));
			break;
	}
	return mode;
}

s32
cfg80211_to_wl_iftype(uint16 type, uint16 *role, uint16 *mode)
{
	switch (type) {
		case NL80211_IFTYPE_STATION:
			*role = WL_IF_TYPE_STA;
			*mode = WL_MODE_BSS;
			break;
		case NL80211_IFTYPE_AP:
			*role = WL_IF_TYPE_AP;
			*mode = WL_MODE_AP;
			break;
#ifdef WL_CFG80211_P2P_DEV_IF
		case NL80211_IFTYPE_P2P_DEVICE:
			*role = WL_IF_TYPE_P2P_DISC;
			*mode = WL_MODE_BSS;
			break;
#endif /* WL_CFG80211_P2P_DEV_IF */
		case NL80211_IFTYPE_P2P_GO:
			*role = WL_IF_TYPE_P2P_GO;
			*mode = WL_MODE_AP;
			break;
		case NL80211_IFTYPE_P2P_CLIENT:
			*role = WL_IF_TYPE_P2P_GC;
			*mode = WL_MODE_BSS;
			break;
		case NL80211_IFTYPE_MONITOR:
			WL_ERR(("Unsupported mode \n"));
			return BCME_UNSUPPORTED;
		case NL80211_IFTYPE_ADHOC:
			*role = WL_IF_TYPE_IBSS;
			*mode = WL_MODE_IBSS;
			break;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0))
		case NL80211_IFTYPE_NAN:
			*role = WL_IF_TYPE_NAN;
			*mode = WL_MODE_NAN;
			break;
#endif
#ifdef WLMESH_CFG80211
		case NL80211_IFTYPE_MESH_POINT:
			*role = WLC_E_IF_ROLE_AP;
			*mode = WL_MODE_MESH;
			break;
#endif /* WLMESH_CFG80211 */
		default:
			WL_ERR(("Unknown interface type:0x%x\n", type));
			return BCME_ERROR;
	}
	return BCME_OK;
}

static s32
wl_role_to_cfg80211_type(uint16 role, uint16 *wl_iftype, uint16 *mode)
{
	switch (role) {

#ifdef WLAWDL
	case WLC_E_IF_ROLE_AWDL:
		/* Intentional fall through - Since there is no
		 * corresponding iftype in cfg80211 stack, map
		 * iftype to station.
		 */
		*wl_iftype = WL_IF_TYPE_AWDL;
		*mode = WL_MODE_AWDL;
		return NL80211_IFTYPE_STATION;
#endif /* WLAWDL */

	case WLC_E_IF_ROLE_STA:
		*wl_iftype = WL_IF_TYPE_STA;
		*mode = WL_MODE_BSS;
		return NL80211_IFTYPE_STATION;
	case WLC_E_IF_ROLE_AP:
		*wl_iftype = WL_IF_TYPE_AP;
		*mode = WL_MODE_AP;
		return NL80211_IFTYPE_AP;
	case WLC_E_IF_ROLE_P2P_GO:
		*wl_iftype = WL_IF_TYPE_P2P_GO;
		*mode = WL_MODE_AP;
		return NL80211_IFTYPE_P2P_GO;
	case WLC_E_IF_ROLE_P2P_CLIENT:
		*wl_iftype = WL_IF_TYPE_P2P_GC;
		*mode = WL_MODE_BSS;
		return NL80211_IFTYPE_P2P_CLIENT;
	case WLC_E_IF_ROLE_IBSS:
		*wl_iftype = WL_IF_TYPE_IBSS;
		*mode = WL_MODE_IBSS;
		return NL80211_IFTYPE_ADHOC;
	case WLC_E_IF_ROLE_NAN:
		*wl_iftype = WL_IF_TYPE_NAN;
		*mode = WL_MODE_NAN;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0)) && defined(WL_CFG80211_NAN)
		/* NL80211_IFTYPE_NAN should only be used with CFG80211 NAN MGMT
		 * For Vendor HAL based NAN implementation, continue advertising
		 * as a STA interface
		 */
		return NL80211_IFTYPE_NAN;
#else
		return NL80211_IFTYPE_STATION;
#endif /* ((LINUX_VER >= KERNEL_VERSION(4, 9, 0))) && WL_CFG80211_NAN */
#ifdef WLDWDS
	case WLC_E_IF_ROLE_WDS:
		*wl_iftype = WL_IF_TYPE_AP;
		*mode = WL_MODE_AP;
		return NL80211_IFTYPE_AP;
#endif
#ifdef WLMESH_CFG80211
	case WLC_E_IF_ROLE_MESH:
		*wl_iftype = WL_IF_TYPE_MESH;
		*mode = WL_MODE_MESH;
		return NL80211_IFTYPE_MESH_POINT;
#endif /* WLMESH_CFG80211 */

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
	s32 ret = BCME_OK;
	u16 mode;
	u8 mac_addr[ETH_ALEN];
	u16 wl_iftype;
#ifdef WL_STATIC_IF
	int static_ifidx;
#endif

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

#if defined(WLMESH_CFG80211) && defined(WL_EXT_IAPSTA)
	if (wl_ext_iapsta_mesh_creating(ndev)) {
		event->role = WLC_E_IF_ROLE_MESH;
		WL_MSG(ndev->name, "change role to WLC_E_IF_ROLE_MESH\n");
	}
#endif /* WLMESH_CFG80211 && WL_EXT_IAPSTA */

	iface_type = wl_role_to_cfg80211_type(event->role, &wl_iftype, &mode);
	if (iface_type < 0) {
		/* Unknown iface type */
		WL_ERR(("Wrong iface type \n"));
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

	if (iface_type == NL80211_IFTYPE_P2P_CLIENT) {
		struct ether_addr *p2p_addr;
		s16 cfg_type = wl_cfgp2p_get_conn_idx(cfg);
		if (cfg_type < BCME_OK) {
			WL_ERR(("Failed to get connection idx for p2p interface"
				", error code = %d", cfg_type));
			goto fail;
		}
		p2p_addr = wl_to_p2p_bss_macaddr(cfg, cfg_type);

		/* check if pre-registered mac matches the mac from dongle via WLC_E_LINK */
		if (memcmp(p2p_addr->octet, addr, ETH_ALEN)) {
			WL_INFORM_MEM(("p2p pre-regsitered mac:" MACDBG
				" , mac from dongle:" MACDBG "\n",
				MAC2STRDBG(p2p_addr->octet), MAC2STRDBG(addr)));

			primary_ndev = bcmcfg_to_prmry_ndev(cfg);

			wl_cfg80211_handle_hang_event(primary_ndev,
				HANG_REASON_IFACE_ADD_FAILURE, DUMP_TYPE_IFACE_OP_FAILURE);
			goto fail;
		}
	}

#ifdef WL_STATIC_IF
	static_ifidx = wl_cfg80211_static_if_name(cfg, name);
	if (static_ifidx >= 0) {
		new_ndev = wl_cfg80211_post_static_ifcreate(cfg, event, addr, iface_type,
			static_ifidx);
		if (!new_ndev) {
			WL_ERR(("failed to get I/F pointer\n"));
			return NULL;
		}
		wdev = new_ndev->ieee80211_ptr;
	} else
#endif /* WL_STATIC_IF */
	{
		new_ndev = wl_cfg80211_allocate_if(cfg, event->ifidx,
			name, addr, event->bssidx, event->name);
		if (!new_ndev) {
			WL_ERR(("I/F allocation failed! \n"));
			return NULL;
		} else {
			WL_DBG(("I/F allocation succeeded! ifidx:0x%x bssidx:0x%x \n",
			 event->ifidx, event->bssidx));
		}

		wdev = (struct wireless_dev *)MALLOCZ(cfg->osh, sizeof(*wdev));
		if (!wdev) {
			WL_ERR(("wireless_dev alloc failed! \n"));
			wl_cfg80211_remove_if(cfg, event->ifidx, new_ndev, rtnl_lock_reqd);
			return NULL;
		}

		wdev->wiphy = bcmcfg_to_wiphy(cfg);
		wdev->iftype = iface_type;

		new_ndev->ieee80211_ptr = wdev;
#ifdef WLDWDS
		/* set wds0.x to 4addr interface here */
		if (event->role == WLC_E_IF_ROLE_WDS) {
			struct dhd_if *ifp = dhd_get_ifp(cfg->pub, event->ifidx);
			ifp->dwds = TRUE;
			WL_MSG(event->name, "set to 4addr\n");
			wdev->use_4addr = true;
		}
#endif /* WLDWDS */
		SET_NETDEV_DEV(new_ndev, wiphy_dev(wdev->wiphy));

		dev_addr_set(new_ndev, addr);
#ifdef WL_EXT_IAPSTA
		if (event->role != WLC_E_IF_ROLE_WDS) {
			wl_ext_iapsta_ifadding(new_ndev, event->ifidx);
		}
#endif /* WL_EXT_IAPSTA */
		if (wl_cfg80211_register_if(cfg, event->ifidx, new_ndev, rtnl_lock_reqd)
			!= BCME_OK) {
			WL_ERR(("IFACE register failed \n"));
			/* Post interface registration, wdev would be freed from the netdev
			 * destructor path. For other cases, handle it here.
			 */
			MFREE(cfg->osh, wdev, sizeof(*wdev));
			wl_cfg80211_remove_if(cfg, event->ifidx, new_ndev, rtnl_lock_reqd);
			return NULL;
		}
	}

	/* Initialize with the station mode params */
	ret = wl_alloc_netinfo(cfg, new_ndev, wdev, wl_iftype,
		PM_ENABLE, event->bssidx, event->ifidx);
	if (unlikely(ret)) {
		WL_ERR(("wl_alloc_netinfo Error (%d)\n", ret));
		goto fail;
	}

	/* Apply the mode & infra setting based on iftype */
	if ((ret = wl_config_infra(cfg, new_ndev, wl_iftype)) < 0) {
		WL_ERR(("config ifmode failure (%d)\n", ret));
		goto fail;
	}

	if (mode == WL_MODE_AP) {
		wl_set_drv_status(cfg, AP_CREATING, new_ndev);
	}
#ifdef WL_EXT_IAPSTA
	wl_ext_iapsta_update_iftype(new_ndev, wl_iftype);
#endif

	WL_INFORM_MEM(("Network Interface (%s) registered with host."
		" cfg_iftype:%d wl_role:%d " MACDBG "\n",
		new_ndev->name, iface_type, event->role, MAC2STRDBG(new_ndev->dev_addr)));

#ifdef SUPPORT_SET_CAC
	wl_cfg80211_set_cac(cfg, 0);
#endif /* SUPPORT_SET_CAC */

	return new_ndev;

fail:
#ifdef WL_STATIC_IF
	/* remove static if from iflist */
	static_ifidx = wl_cfg80211_static_if_name(cfg, name);
	if (static_ifidx >= 0) {
		cfg->static_ndev_state[static_ifidx] = NDEV_STATE_FW_IF_FAILED;
		wl_cfg80211_update_iflist_info(cfg, new_ndev, WL_STATIC_IFIDX+static_ifidx, addr,
			event->bssidx, event->name, NDEV_STATE_FW_IF_FAILED);
	}
#endif /* WL_STATIC_IF */
	if (new_ndev) {
		/* wdev would be freed from netdev destructor call back */
		wl_cfg80211_remove_if(cfg, event->ifidx, new_ndev, rtnl_lock_reqd);
	}

	return NULL;
}

s32
wl_cfg80211_delete_iface(struct bcm_cfg80211 *cfg,
	wl_iftype_t sec_data_if_type)
{
	struct net_info *iter, *next;
	struct net_device *primary_ndev;
	s32 ret = BCME_OK;
	uint8 i = 0;

	BCM_REFERENCE(i);
	BCM_REFERENCE(ret);

	/* Note: This function will clean up only the network interface and host
	 * data structures. The firmware interface clean up will happen in the
	 * during chip reset (ifconfig wlan0 down for built-in drivers/rmmod
	 * context for the module case).
	 */
	primary_ndev = bcmcfg_to_prmry_ndev(cfg);
	WL_DBG(("Enter, deleting iftype  %s\n",
		wl_iftype_to_str(sec_data_if_type)));
	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	for_each_ndev(cfg, iter, next) {
		GCC_DIAGNOSTIC_POP();
		if (iter->ndev && (iter->ndev != primary_ndev)) {
			if (iter->iftype != sec_data_if_type) {
				continue;
			}
			switch (sec_data_if_type) {
				case WL_IF_TYPE_P2P_GO:
				case WL_IF_TYPE_P2P_GC: {
					ret = _wl_cfg80211_del_if(cfg,
						iter->ndev, NULL, iter->ndev->name);
					break;
				}
#ifdef WL_NAN
				case WL_IF_TYPE_NAN: {
					if (wl_cfgnan_is_enabled(cfg) == false) {
						WL_INFORM_MEM(("Nan is not active,"
							" ignore NDI delete\n"));
					} else {
						ret = wl_cfgnan_delete_ndp(cfg, iter->ndev);
					}
					break;
				}
#endif /* WL_NAN */
				case WL_IF_TYPE_AP: {
					/* Cleanup AP */
#ifdef WL_STATIC_IF
						/* handle static ap */
					if (wl_cfg80211_static_if(cfg, iter->ndev)) {
						dev_close(iter->ndev);
					} else
#endif /* WL_STATIC_IF */
					{
						/* handle virtual created AP */
						ret = _wl_cfg80211_del_if(cfg, iter->ndev,
							NULL, iter->ndev->name);
					}
					break;
				}
				default: {
					WL_ERR(("Unsupported interface type\n"));
					ret = -ENOTSUPP;
					goto fail;
				}
			}
		}
	}
fail:
	return ret;
}

s32
wl_cfg80211_post_ifdel(struct net_device *ndev, bool rtnl_lock_reqd, s32 ifidx)
{
	s32 ret = BCME_OK;
	struct bcm_cfg80211 *cfg;
	struct net_info *netinfo = NULL;

	if (!ndev || !ndev->ieee80211_ptr) {
		/* No wireless dev done for this interface */
		ret = -EINVAL;
		goto exit;
	}

	cfg = wl_get_cfg(ndev);
	if (!cfg) {
		WL_ERR(("cfg null\n"));
		ret = BCME_ERROR;
		goto exit;
	}

	if (ifidx <= 0) {
		WL_ERR(("Invalid IF idx for iface:%s\n", ndev->name));
#if defined(BCMDONGLEHOST)
		ifidx = dhd_net2idx(((struct dhd_pub *)(cfg->pub))->info, ndev);
		BCM_REFERENCE(ifidx);
#endif
		if (ifidx <= 0) {
			ASSERT(0);
			ret = BCME_ERROR;
			goto exit;
		}
	}

	if ((netinfo = wl_get_netinfo_by_wdev(cfg, ndev_to_wdev(ndev))) == NULL) {
		WL_ERR(("Find netinfo from wdev %p failed\n", ndev_to_wdev(ndev)));
		ret = -ENODEV;
		goto exit;
	}

#ifdef WL_STATIC_IF
	if (wl_cfg80211_static_if(cfg, ndev)) {
		ret = wl_cfg80211_post_static_ifdel(cfg, ndev);
	} else
#endif /* WL_STATIC_IF */
	{
		WL_INFORM_MEM(("[%s] cfg80211_remove_if ifidx:%d, vif_count:%d\n",
			ndev->name, ifidx, cfg->vif_count));
		wl_cfg80211_remove_if(cfg, ifidx, ndev, rtnl_lock_reqd);
		cfg->bss_pending_op = FALSE;
	}

#ifdef SUPPORT_SET_CAC
	wl_cfg80211_set_cac(cfg, 1);
#endif /* SUPPORT_SET_CAC */
exit:
	return ret;
}

int
wl_cfg80211_deinit_p2p_discovery(struct bcm_cfg80211 *cfg)
{
	s32 ret = BCME_OK;
	bcm_struct_cfgdev *cfgdev;

	if (cfg->p2p) {
		/* De-initialize the p2p discovery interface, if operational */
		WL_ERR(("Disabling P2P Discovery Interface \n"));
#ifdef WL_CFG80211_P2P_DEV_IF
		cfgdev = bcmcfg_to_p2p_wdev(cfg);
#else
		cfgdev = cfg->p2p_net;
#endif
		if (cfgdev) {
			ret = wl_cfg80211_scan_stop(cfg, cfgdev);
			if (unlikely(ret < 0)) {
				CFGP2P_ERR(("P2P scan stop failed, ret=%d\n", ret));
			}
		}

		wl_cfgp2p_disable_discovery(cfg);
		wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE) = 0;
		p2p_on(cfg) = false;
	}
	return ret;
}

/* Create a Generic Network Interface and initialize it depending up on
 * the interface type
 */
struct wireless_dev *
wl_cfg80211_create_iface(struct wiphy *wiphy,
	wl_iftype_t wl_iftype,
	u8 *mac_addr, const char *name)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct net_device *new_ndev = NULL;
	struct net_device *primary_ndev = NULL;
	s32 ret = BCME_OK;
	s32 bsscfg_idx = 0;
	long timeout;
	wl_if_event_info *event = NULL;
	u8 addr[ETH_ALEN];
	struct net_info *iter, *next;

	WL_DBG(("Enter\n"));
	if (!name) {
		WL_ERR(("Interface name not provided\n"));
		return NULL;
	}
	else {
		GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
		for_each_ndev(cfg, iter, next) {
			GCC_DIAGNOSTIC_POP();
			if (iter->ndev) {
				if (strncmp(iter->ndev->name, name, strlen(name)) == 0) {
					WL_ERR(("Interface name,%s exists!\n", iter->ndev->name));
					return NULL;
				}
			}
		}
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

	cfg->bss_pending_op = TRUE;
	bzero(&cfg->if_event_info, sizeof(cfg->if_event_info));

	/*
	 * Intialize the firmware I/F.
	 */

#ifdef CUSTOMER_HW6
	if (wl_customer6_legacy_chip_check(cfg, primary_ndev)) {
		/* Use bss iovar instead of interface_create iovar */
		ret = BCME_UNSUPPORTED;
	} else
#endif /* CUSTOMER_HW6 */

	{
		ret = wl_cfg80211_interface_ops(cfg, primary_ndev, bsscfg_idx,
			wl_iftype, 0, addr);
	}
	if (ret == BCME_UNSUPPORTED) {
		/* Use bssidx 1 by default */
		bsscfg_idx = 1;
		if ((ret = wl_cfg80211_add_del_bss(cfg, primary_ndev,
			bsscfg_idx, wl_iftype, 0, addr)) < 0) {
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
		WL_ERR(("ADD_IF event, didn't come. Return. timeout:%lu bss_pending_op:%d\n",
			timeout, cfg->bss_pending_op));
		if (timeout == -ERESTARTSYS) {
			WL_ERR(("waitqueue was interrupted by a signal, returns -ERESTARTSYS\n"));
		}
		goto exit;
	}

	event = &cfg->if_event_info;
	/*
	 * Since FW operation is successful,we can go ahead with the
	 * the host interface creation.
	 */
	new_ndev = wl_cfg80211_post_ifcreate(primary_ndev,
		event, addr, name, false);

	if (new_ndev) {
		/* Iface post ops successful. Return ndev/wdev ptr */
		return new_ndev->ieee80211_ptr;
	}

exit:
	cfg->bss_pending_op = FALSE;
	return NULL;
}

s32
wl_cfg80211_del_iface(struct wiphy *wiphy, struct wireless_dev *wdev)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct net_device *ndev = NULL;
	s32 ret = BCME_OK;
	s32 bsscfg_idx = 1;
	long timeout;
	u16 wl_iftype;
	u16 wl_mode;

	WL_DBG(("Enter\n"));

	/* If any scan is going on, abort it */
	if (wl_get_drv_status_all(cfg, SCANNING)) {
		WL_DBG(("Scan in progress. Aborting the scan!\n"));
		wl_cfgscan_cancel_scan(cfg);
	}

	bsscfg_idx = wl_get_bssidx_by_wdev(cfg, wdev);
	if (bsscfg_idx <= 0) {
		/* validate bsscfgidx */
		WL_ERR(("Wrong bssidx! \n"));
		return -EINVAL;
	}

	/* Handle p2p iface */
	if ((ret = wl_cfg80211_p2p_if_del(wiphy, wdev)) != BCME_NOTFOUND) {
		WL_DBG(("P2P iface del handled \n"));
#ifdef SUPPORT_SET_CAC
		wl_cfg80211_set_cac(cfg, 1);
#endif /* SUPPORT_SET_CAC */
		return ret;
	}

	ndev = wdev->netdev;
	if (unlikely(!ndev)) {
		WL_ERR(("ndev null! \n"));
		return -EINVAL;
	}

	memset(&cfg->if_event_info, 0, sizeof(cfg->if_event_info));

	if (cfg80211_to_wl_iftype(ndev->ieee80211_ptr->iftype,
		&wl_iftype, &wl_mode) < 0) {
		return -EINVAL;
	}

	WL_DBG(("del interface. bssidx:%d cfg_iftype:%d wl_iftype:%d",
		bsscfg_idx, ndev->ieee80211_ptr->iftype, wl_iftype));
	/* Delete the firmware interface. "interface_remove" command
	 * should go on the interface to be deleted
	 */
	if (wl_cfg80211_get_bus_state(cfg)) {
		WL_ERR(("Bus state is down: %d\n", __LINE__));
		ret = BCME_DONGLE_DOWN;
		goto exit;
	}

	cfg->bss_pending_op = true;
	ret = wl_cfg80211_interface_ops(cfg, ndev, bsscfg_idx,
		wl_iftype, 1, NULL);
	if (ret == BCME_UNSUPPORTED) {
		if ((ret = wl_cfg80211_add_del_bss(cfg, ndev,
			bsscfg_idx, wl_iftype, true, NULL)) < 0) {
			WL_ERR(("DEL bss failed ret:%d \n", ret));
			goto exit;
		}
	} else if ((ret == BCME_NOTAP) || (ret == BCME_NOTSTA)) {
		/* De-init sequence involving role downgrade not happened.
		 * Do nothing and return error. The del command should be
		 * retried.
		 */
		WL_ERR(("ifdel role mismatch:%d\n", ret));
		ret = -EBADTYPE;
		goto exit;
	} else if (ret < 0) {
		WL_ERR(("Interface DEL failed ret:%d \n", ret));
		goto exit;
	}

	timeout = wait_event_interruptible_timeout(cfg->netif_change_event,
		!cfg->bss_pending_op, msecs_to_jiffies(MAX_WAIT_TIME));
	if (timeout <= 0 || cfg->bss_pending_op) {
		WL_ERR(("timeout in waiting IF_DEL event\n"));
		/* The interface unregister will happen from wifi reset context */
		ret = -ETIMEDOUT;
		/* fall through */
	}

exit:
	if (ret < 0) {
		WL_ERR(("iface del failed:%d\n", ret));
#ifdef WL_STATIC_IF
		if (wl_cfg80211_static_if(cfg, ndev)) {
			/*
			 * For static interface, clean up the host data,
			 * irrespective of fw status. For dynamic
			 * interfaces it gets cleaned from dhd_stop context
			 */
			wl_cfg80211_post_static_ifdel(cfg, ndev);
		}
#endif /* WL_STATIC_IF */
	} else {
		ret = wl_cfg80211_post_ifdel(ndev, false, cfg->if_event_info.ifidx);
		if (unlikely(ret)) {
			WL_ERR(("post_ifdel failed\n"));
		}
	}

	cfg->bss_pending_op = false;
	return ret;
}

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
	char sec[64];

	WL_TRACE(("In\n"));
	RETURN_EIO_IF_NOT_UP(cfg);
	WL_INFORM_MEM(("IBSS JOIN\n"));
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
	if (chan) {
#ifdef WL_EXT_IAPSTA
		chanspec = wl_freq_to_chanspec(chan->center_freq);
		wl_ext_iapsta_update_iftype(dev, WL_IF_TYPE_IBSS);
		chanspec = wl_ext_iapsta_update_channel(dev, chanspec);
#endif /* WL_EXT_IAPSTA */
		cfg->channel = chanspec;
	}
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
			int retry = 4;
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
//			schedule_timeout_interruptible(msecs_to_jiffies(4000));
			while (retry--) {
				if (!wl_get_drv_status_all(cfg, SCANNING)) {
					break;
				}
				wl_delay(150);
			}

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
		u32 bw_cap = 0;
		err = wl_get_bandwidth_cap(dev, CHSPEC_BAND(cfg->channel), &bw_cap);
		if (unlikely(err)) {
			WL_ERR(("Failed to get bandwidth capability (%d)\n", err));
			return err;
		}
		chanspec = wf_create_chspec_from_primary(wf_chspec_primary20_chan(cfg->channel),
			bw_cap, CHSPEC_BAND(cfg->channel));
	}

	/*
	 * Join with specific BSSID and cached SSID
	 * If SSID is zero join based on BSSID only
	 */
	bzero(&join_params, sizeof(join_params));
	memcpy((void *)join_params.ssid.SSID, (const void *)params->ssid,
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
		bzero(&join_params.params.bssid, ETHER_ADDR_LEN);

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

	wl_ext_get_sec(dev, 0, sec, sizeof(sec), TRUE);
	WL_MSG(dev->name, "Join IBSS %pM ssid \"%s\", len (%d), channel=%d, sec=%s\n",
		&join_params.params.bssid, join_params.ssid.SSID, join_params.ssid.SSID_len,
		wf_chspec_ctlchan(chanspec), sec);
	err = wldev_ioctl_set(dev, WLC_SET_SSID, &join_params,
		join_params_size);
	if (unlikely(err)) {
		WL_ERR(("IBSS set_ssid Error (%d)\n", err));
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
#ifdef WLAIBSS
	cfg->aibss_txfail_seq = 0;	/* initialize the sequence */
#endif /* WLAIBSS */
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

	WL_MSG(dev->name, "Leave IBSS\n");
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
static
int wl_cfg80211_get_rsn_capa(const bcm_tlv_t *wpa2ie,
	const u8** rsn_cap)
{
	u16 suite_count;
	const wpa_suite_mcast_t *mcast;
	const wpa_suite_ucast_t *ucast;
	int len;
	const wpa_suite_auth_key_mgmt_t *mgmt;

	if (!wpa2ie)
		return BCME_BADARG;

	len = wpa2ie->len;

	/* check for Multicast cipher suite */
	if ((len -= (WPA_SUITE_LEN + WPA2_VERSION_LEN)) <= 0) {
		return BCME_NOTFOUND;
	}

	mcast = (const wpa_suite_mcast_t *)&wpa2ie->data[WPA2_VERSION_LEN];

	/* Check for the unicast suite(s) */
	if (len < WPA_IE_SUITE_COUNT_LEN) {
		return BCME_NOTFOUND;
	}

	ucast = (const wpa_suite_ucast_t *)&mcast[1];
	suite_count = ltoh16_ua(&ucast->count);
	if ((suite_count > NL80211_MAX_NR_CIPHER_SUITES) ||
		(len -= (WPA_IE_SUITE_COUNT_LEN +
		(WPA_SUITE_LEN * suite_count))) <= 0)
		return BCME_BADLEN;

	/* Check for AUTH key management suite(s) */
	if (len < WPA_IE_SUITE_COUNT_LEN) {
		return BCME_NOTFOUND;
	}

	mgmt = (const wpa_suite_auth_key_mgmt_t *)&ucast->list[suite_count];
	suite_count = ltoh16_ua(&mgmt->count);

	if ((suite_count <= NL80211_MAX_NR_CIPHER_SUITES) &&
			(len -= (WPA_IE_SUITE_COUNT_LEN +
			(WPA_SUITE_LEN * suite_count))) >= RSN_CAP_LEN) {
		rsn_cap[0] = (const u8 *)&mgmt->list[suite_count];
	} else {
		return BCME_BADLEN;
	}

	return BCME_OK;
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

	WL_DBG_MEM(("[%s] wl wpa_auth 0x%0x\n", dev->name, val));
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

	WL_DBG((" wl_set_set_wapi_ie\n"));
	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find p2p index from wdev(%p) failed\n", dev->ieee80211_ptr));
		return BCME_ERROR;
	}

	err = wldev_iovar_setbuf_bsscfg(dev, "wapiie", (const void *)sme->ie, sme->ie_len,
			cfg->ioctl_buf, WLC_IOCTL_MAXLEN, bssidx, &cfg->ioctl_buf_sync);
	if (unlikely(err)) {
		WL_ERR(("set_wapi_ie Error (%d)\n", err));
		return err;
	}
	WL_DBG_MEM(("wapi_ie successfully (%s)\n", dev->name));
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
#ifdef WL_FILS
	case NL80211_AUTHTYPE_FILS_SK:
		WL_DBG(("fils shared key\n"));
		val = WL_AUTH_FILS_SHARED;
		break;
	case NL80211_AUTHTYPE_FILS_SK_PFS:
		val = WL_AUTH_FILS_SHARED_PFS;
		WL_DBG(("fils shared key with pfs\n"));
		break;
	case NL80211_AUTHTYPE_FILS_PK:
		WL_DBG(("fils public key\n"));
		val = WL_AUTH_FILS_PUBLIC;
		break;
#endif /* WL_FILS */
#if defined(WL_SAE) || defined(WL_CLIENT_SAE)
	case NL80211_AUTHTYPE_SAE:
#ifdef WL_CLIENT_SAE
		if (!wl_is_pmkid_available(dev, sme->bssid))
			val = DOT11_SAE;
		else
#endif /* WL_CLIENT_SAE */
		{
			/* Fw will choose right auth type
			 * dynamically based on PMKID availability
			 */
			val = WL_AUTH_OPEN_SHARED;
		}

		WL_DBG(("sae auth type\n"));
		break;
#endif /* WL_SAE || WL_CLIENT_SAE */
	default:
		val = 2;
		WL_ERR(("invalid auth type (%d)\n", sme->auth_type));
		break;
	}

	WL_DBG_MEM(("[%s] wl auth 0x%0x \n", dev->name, val));
	err = wldev_iovar_setint_bsscfg(dev, "auth", val, bssidx);
	if (unlikely(err)) {
		WL_ERR(("set auth failed (%d)\n", err));
		return err;
	}
	sec = wl_read_prof(cfg, dev, WL_PROF_SEC);
	sec->auth_type = sme->auth_type;
	sec->fw_auth = val;
	return err;
}

static u32
wl_rsn_cipher_wsec_algo_lookup(uint32 cipher)
{
	uint i;

	for (i = 0; i < ARRAYSIZE(rsn_cipher_algo_lookup_tbl); i++) {
		if (cipher == rsn_cipher_algo_lookup_tbl[i].cipher_suite) {
			return rsn_cipher_algo_lookup_tbl[i].wsec_algo;
		}
	}
	return WSEC_NONE;
}

static u32
wl_rsn_cipher_wsec_key_algo_lookup(uint32 cipher)
{
	uint i;

	for (i = 0; i < ARRAYSIZE(rsn_cipher_algo_lookup_tbl); i++) {
		if (cipher == rsn_cipher_algo_lookup_tbl[i].cipher_suite) {
			return rsn_cipher_algo_lookup_tbl[i].wsec_key_algo;
		}
	}
	return CRYPTO_ALGO_OFF;
}

static u32
wl_rsn_akm_wpa_auth_lookup(uint32 akm)
{
	uint i;

	for (i = 0; i < ARRAYSIZE(rsn_akm_wpa_auth_lookup_tbl); i++) {
		if (akm == rsn_akm_wpa_auth_lookup_tbl[i].akm_suite) {
			return rsn_akm_wpa_auth_lookup_tbl[i].wpa_auth;
		}
	}
	return WPA_AUTH_DISABLED;
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

#ifdef BCMWAPI_WPI
	s32 wapi_val = 0;
	s32 val = 0;
#endif

	s32 bssidx;
#ifdef WL_GCMP
	uint32 algos = 0, mask = 0;
#endif /* WL_GCMP */

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find p2p index from wdev(%p) failed\n", dev->ieee80211_ptr));
		return BCME_ERROR;
	}

	if (sme->crypto.n_ciphers_pairwise) {
		pval = wl_rsn_cipher_wsec_algo_lookup(sme->crypto.ciphers_pairwise[0]);
		if (pval == WSEC_NONE) {
			WL_ERR(("Invalid cipher (0x%x)\n",
				sme->crypto.ciphers_pairwise[0]));
			return BCME_BADARG;
		}
		switch (sme->crypto.ciphers_pairwise[0]) {

#ifdef BCMWAPI_WPI
		case WLAN_CIPHER_SUITE_SMS4:
#ifndef CUSTOMER_HW6
			if (!IS_WAPI_VER(sme->crypto.wpa_versions)) {
				WL_ERR(("Invalid WAPI version %d\n", sme->crypto.wpa_versions));
				return BCME_BADARG;
			}
#endif /* !CUSTOMER_HW6 */
			val = pval;
			err = wl_set_set_wapi_ie(dev, sme);
			if (unlikely(err)) {
				WL_DBG(("Set wapi ie failed  \n"));
				return err;
			} else {
				WL_DBG(("Set wapi ie succeded\n"));
			}
			wapi_val = WAPI_AUTH_PSK | WAPI_AUTH_UNSPECIFIED;
			WL_DBG_MEM(("[WAPI] wl wpa_auth to 0x%0x (%s)\n", val, dev->name));
			err = wldev_iovar_setint_bsscfg(dev, "wpa_auth", wapi_val, bssidx);
			if (unlikely(err)) {
				WL_ERR(("set wpa_auth failed (%d)\n", err));
				return err;
			}
			break;
#endif /* BCMWAPI_WPI */

#ifdef WL_GCMP
		case WLAN_CIPHER_SUITE_GCMP:
		case WLAN_CIPHER_SUITE_GCMP_256:
			algos = KEY_ALGO_MASK(wl_rsn_cipher_wsec_key_algo_lookup(
					sme->crypto.ciphers_pairwise[0]));
			mask = algos | KEY_ALGO_MASK(CRYPTO_ALGO_AES_CCM);
			break;
#endif /* WL_GCMP */
		default: /* No post processing required */
			break;
		}
	}
#if defined(BCMSUP_4WAY_HANDSHAKE)
	/* Ensure in-dongle supplicant is turned on when FBT wants to do the 4-way
	 * handshake.
	 * Note that the FW feature flag only exists on kernels that support the
	 * FT-EAP AKM suite.
	 */
	if (cfg->wdev->wiphy->features & NL80211_FEATURE_FW_4WAY_HANDSHAKE) {
		err = wldev_iovar_setint_bsscfg(dev, "sup_wpa", 1, bssidx);
		if (err) {
			WL_ERR(("FBT: Error setting sup_wpa (%d)\n", err));
			return err;
		} else {
			WL_INFORM_MEM(("idsup enabled.\n"));
		}
	}
#endif /* BCMSUP_4WAY_HANDSHAKE */
	if (sme->crypto.cipher_group) {
		gval = wl_rsn_cipher_wsec_algo_lookup(sme->crypto.cipher_group);
		if (gval == WSEC_NONE) {
			WL_ERR(("invalid cipher group (0x%x)\n", sme->crypto.cipher_group));
			return BCME_BADARG;
		}
		switch (sme->crypto.cipher_group) {

#ifdef BCMWAPI_WPI
		case WLAN_CIPHER_SUITE_SMS4:
			val = gval;
			break;
#endif

#ifdef WL_GCMP
		case WLAN_CIPHER_SUITE_GCMP:
		case WLAN_CIPHER_SUITE_GCMP_256:
			algos = KEY_ALGO_MASK(
				wl_rsn_cipher_wsec_key_algo_lookup(sme->crypto.cipher_group));
			mask = algos | KEY_ALGO_MASK(CRYPTO_ALGO_AES_CCM);
			break;
#endif /* WL_GCMP */
		default: /* No post processing required */
			break;
		}
	}

	WL_DBG(("pval (%d) gval (%d)\n", pval, gval));
#ifdef WL_GCMP
	WL_DBG(("algos:%x, mask:%x\n", algos, mask));
#endif /* WL_GCMP */

	if (is_wps_conn(sme)) {
		if (sme->privacy) {
			wsec_val = 4;
		} else {
			/* WPS-2.0 allows no security */
			wsec_val = 0;
		}
	} else {

#ifdef BCMWAPI_WPI
		if (sme->crypto.cipher_group == WLAN_CIPHER_SUITE_SMS4) {
			WL_DBG((" NO, is_wps_conn, WAPI set to SMS4_ENABLED\n"));
			wsec_val = val;
		} else
#endif

		{
			WL_DBG((" NO, is_wps_conn, Set pval | gval to WSEC\n"));
			wsec_val = pval | gval;
		}
	}

	WL_DBG_MEM(("[%s] wl wsec 0x%x\n", dev->name, wsec_val));
	err = wldev_iovar_setint_bsscfg(dev, "wsec", wsec_val, bssidx);
	if (unlikely(err)) {
		WL_ERR(("error (%d)\n", err));
		return err;
	}
#ifdef WL_GCMP
	if (wl_set_wsec_info_algos(dev, algos, mask)) {
		WL_ERR(("set wsec_info error (%d)\n", err));
	}
#endif /* WL_GCMP */
	sec = wl_read_prof(cfg, dev, WL_PROF_SEC);
	sec->cipher_pairwise = sme->crypto.ciphers_pairwise[0];
	sec->cipher_group = sme->crypto.cipher_group;
	sec->fw_wsec = wsec_val;
	return err;
}
#ifdef WL_GCMP
static s32
wl_set_wsec_info_algos(struct net_device *dev, uint32 algos, uint32 mask)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	s32 bssidx;
	s32 err = 0;
	wl_wsec_info_t *wsec_info;
	bcm_xtlv_t *wsec_info_tlv;
	uint16 tlv_data_len;
	uint32 tlv_data[2];
	uint32 param_len;
	uint8 * buf;

	WL_DBG(("enter.\n"));
	if (!cfg) {
		return BCME_ERROR;
	}
	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find index from wdev(%p) failed\n", dev->ieee80211_ptr));
		return BCME_ERROR;
	}

	buf = MALLOCZ(cfg->osh, sizeof(wl_wsec_info_t) + sizeof(tlv_data));
	if (!buf) {
		WL_ERR(("No memory"));
		return BCME_NOMEM;
	}
	wsec_info = (wl_wsec_info_t *)buf;
	wsec_info->version = WL_WSEC_INFO_VERSION;
	wsec_info_tlv = (bcm_xtlv_t *)(buf + OFFSETOF(wl_wsec_info_t, tlvs));

	wsec_info->num_tlvs++;
	tlv_data_len = sizeof(tlv_data);
	tlv_data[0] = algos;
	tlv_data[1] = mask;

	bcm_xtlv_pack_xtlv(wsec_info_tlv, WL_WSEC_INFO_BSS_ALGOS, tlv_data_len,
		(const uint8 *)tlv_data, 0);
	param_len = OFFSETOF(wl_wsec_info_t, tlvs) + WL_WSEC_INFO_TLV_HDR_LEN + tlv_data_len;

	err = wldev_iovar_setbuf_bsscfg(dev, "wsec_info", wsec_info, param_len,
		cfg->ioctl_buf, WLC_IOCTL_MAXLEN, bssidx, &cfg->ioctl_buf_sync);

	MFREE(cfg->osh, buf, sizeof(wl_wsec_info_t) + sizeof(tlv_data));
	return err;
}
#endif /* WL_GCMP */

#ifdef WL_SAE
s32
wl_cfg80211_set_wsec_info(struct net_device *dev, uint32 *data,
	uint16 data_len, int tag)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	s32 bssidx;
	s32 err = 0;
	wl_wsec_info_t *wsec_info;
	bcm_xtlv_t *bcm_info_tlv;
	uint32 param_len;
	uint8 *buf = NULL;

	if (!cfg) {
		return BCME_ERROR;
	}

	if (data_len > WLC_IOCTL_SMLEN) {
		err = BCME_BADLEN;
		goto exit;
	}

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find index from wdev(%p) failed\n", dev->ieee80211_ptr));
		err = BCME_ERROR;
		goto exit;
	}

	buf = MALLOCZ(cfg->osh, sizeof(wl_wsec_info_t) + data_len);
	if (!buf) {
		WL_ERR(("No memory"));
		err = BCME_NOMEM;
		goto exit;
	}

	wsec_info = (wl_wsec_info_t *)buf;
	bzero(wsec_info, sizeof(wl_wsec_info_t) + data_len);
	wsec_info->version = WL_WSEC_INFO_VERSION;
	bcm_info_tlv = (bcm_xtlv_t *)(buf + OFFSETOF(wl_wsec_info_t, tlvs));

	wsec_info->num_tlvs++;

	bcm_xtlv_pack_xtlv(bcm_info_tlv, tag, data_len, (const u8*)data, 0);
	param_len = OFFSETOF(wl_wsec_info_t, tlvs) + WL_WSEC_INFO_TLV_HDR_LEN + data_len;

	err = wldev_iovar_setbuf_bsscfg(dev, "wsec_info", wsec_info, param_len,
		cfg->ioctl_buf, WLC_IOCTL_MAXLEN, bssidx, &cfg->ioctl_buf_sync);
	if (unlikely(err) && (err != BCME_UNSUPPORTED)) {
		WL_ERR(("set wsec_info error (%d)\n", err));
	}

exit:
	if (buf)
		MFREE(cfg->osh, buf, sizeof(wl_wsec_info_t) + data_len);
	return err;
}
#endif /* SAE */

#ifdef MFP
static s32
wl_cfg80211_set_mfp(struct bcm_cfg80211 *cfg,
	struct net_device *dev,
	struct cfg80211_connect_params *sme)
{
	s32 mfp = WL_MFP_NONE;
	s32 current_mfp = WL_MFP_NONE;
	const bcm_tlv_t *wpa2_ie;
	const u8* rsn_cap = NULL;
	bool fw_support = false;
	int err, count = 0;
	const u8 *eptr = NULL, *ptr = NULL;
	const u8* group_mgmt_cs = NULL;
	const wpa_pmkid_list_t* pmkid = NULL;
	struct wl_security *sec = wl_read_prof(cfg, dev, WL_PROF_SEC);

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
	if (((wpa2_ie = bcm_parse_tlvs((const u8 *)sme->ie, sme->ie_len,
			DOT11_MNG_RSN_ID)) != NULL) &&
			(wl_cfg80211_get_rsn_capa(wpa2_ie, &rsn_cap) == 0) && rsn_cap) {
		WL_DBG(("rsn_cap 0x%x%x\n", rsn_cap[0], rsn_cap[1]));
		/* Check for MFP cap in the RSN capability field */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0))
		if (sme->mfp)
#endif
		{
			if (rsn_cap[0] & RSN_CAP_MFPR) {
				mfp = WL_MFP_REQUIRED;
			} else if (rsn_cap[0] & RSN_CAP_MFPC) {
				mfp = WL_MFP_CAPABLE;
			}
		}
		/*
		 * eptr --> end/last byte addr of wpa2_ie
		 * ptr --> to keep track of current/required byte addr
		 */
		eptr = (const u8*)wpa2_ie + (wpa2_ie->len + TLV_HDR_LEN);
		/* pointing ptr to the next byte after rns_cap */
		ptr = (const u8*)rsn_cap + RSN_CAP_LEN;
		if (mfp && (eptr - ptr) >= WPA2_PMKID_COUNT_LEN) {
			/* pmkid now to point to 1st byte addr of pmkid in wpa2_ie */
			pmkid = (const wpa_pmkid_list_t*)ptr;
			count = pmkid->count.low | (pmkid->count.high << 8);
			/* ptr now to point to last byte addr of pmkid */
			ptr = (const u8*)pmkid + (count * WPA2_PMKID_LEN
					+ WPA2_PMKID_COUNT_LEN);
			if ((eptr - ptr) >= WPA_SUITE_LEN) {
				/* group_mgmt_cs now to point to first byte addr of bip */
				group_mgmt_cs = ptr;
			}
		}
	}

	WL_DBG(("mfp:%d wpa2_ie ptr:%p mfp fw_support:%d\n",
		mfp, wpa2_ie, fw_support));

	if (fw_support == false) {
		if (mfp == WL_MFP_REQUIRED) {
			/* if mfp > 0, mfp capability set in wpa ie, but
			 * FW indicated error for mfp. Propagate the error up.
			 */
			WL_ERR(("mfp capability found in wpaie. But fw doesn't "
				"seem to support MFP\n"));
			err = -EINVAL;
			goto exit;
		} else {
			/* Firmware doesn't support mfp. But since connection request
			 * is for non-mfp case, don't bother.
			 */
			err = BCME_OK;
			goto exit;
		}
	} else if (mfp != current_mfp) {
		/* Some FW brances report error (-5) during MFP set if the BSS
		 * is up (roam case). Typically in roaming cases, the MFP
		 * configuration doesn't change. So in roam/reassoc cases, there is
		 * no need to update the fw state. If we still hit corner cases
		 * throwing (-5) error, we need to pull in RB:59117.
		 */
		err = wldev_iovar_setint(dev, "mfp", mfp);
		if (unlikely(err)) {
			WL_ERR(("mfp (%d) set failed ret:%d \n", mfp, err));
			goto exit;
		}
		WL_DBG_MEM(("[%s] wl mfp 0x%x\n", dev->name, mfp));
	}

	if (sec) {
		sec->fw_mfp = mfp;
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

#ifdef CUSTOMER_HW6
			if (wl_customer6_legacy_chip_check(cfg,
				bcmcfg_to_prmry_ndev(cfg))) {
				/* Ignore bip error: Some older firmwares doesn't
				 * support bip iovar/ return BCME_NOTUP while trying
				 * to set bip from connect context. These firmares
				 * include bip in RSNIE by default. So its okay to
				 * ignore the error.
				 */
					err = BCME_OK;
					goto exit;
				} else
#endif /* CUSTOMER_HW6 */

				{
					goto exit;
				}
		} else {
			WL_INFORM_MEM(("[%s] wl bip %02X:%02X:%02X\n",
				dev->name, group_mgmt_cs[0], group_mgmt_cs[1],
				group_mgmt_cs[2]));
		}
	}
exit:
	if (err) {
		wl_flush_fw_log_buffer(bcmcfg_to_prmry_ndev(cfg),
			FW_LOGSET_MASK_ALL);
	}

	return 0;
}
#endif /* MFP */

#ifdef WL_FILS
bool
wl_is_fils_supported(struct net_device *ndev)
{
	s32 err;
	u8 ioctl_buf[WLC_IOCTL_SMLEN] = {0};
	bcm_iov_buf_t *iov_buf = (bcm_iov_buf_t *)ioctl_buf;

	iov_buf->version = WL_FILS_IOV_VERSION;
	err = wldev_iovar_getbuf(ndev, "fils", (uint8*)iov_buf, sizeof(bcm_iov_buf_t),
		iov_buf, WLC_IOCTL_SMLEN, NULL);
	if (err == BCME_UNSUPPORTED) {
		WL_DBG(("FILS NOT supported\n"));
		return false;
	}

	WL_INFORM(("FILS supported\n"));
	return true;
}

#define WL_NUM_OF_TLV_IN_SET_FILS_PARAMS	4u
static s32
wl_set_fils_params(struct net_device *dev, struct cfg80211_connect_params *sme)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	bcm_iov_buf_t *iov_buf = NULL;
	bcm_xtlvbuf_t tbuf;
	s32 err = BCME_OK;
	uint32 buf_size;

	if ((sme->auth_type != NL80211_AUTHTYPE_FILS_SK) &&
		(sme->auth_type != NL80211_AUTHTYPE_FILS_SK_PFS) &&
		(sme->auth_type != NL80211_AUTHTYPE_FILS_PK)) {
		return BCME_OK;
	}
	if (sme->fils_erp_rrk_len > WL_MAX_FILS_KEY_LEN) {
		WL_ERR(("%s: FILS rRK exceed allowed size\n", __FUNCTION__));
		err = BCME_BADARG;
		goto exit;
	}
	/* Check incoming buffer length */
	buf_size = sme->fils_erp_username_len + sme->fils_erp_realm_len + sme->fils_erp_rrk_len +
		sizeof(sme->fils_erp_next_seq_num) +
		WL_NUM_OF_TLV_IN_SET_FILS_PARAMS * BCM_XTLV_HDR_SIZE_EX(BCM_XTLV_OPTION_ALIGN32) +
		sizeof(bcm_iov_buf_t) - 1u;

	if (buf_size > WLC_IOCTL_SMLEN) {
		WL_ERR(("%s: FILS connect params arguments exceed allowed size\n", __FUNCTION__));
		err = BCME_BADARG;
		goto exit;
	}
	iov_buf = MALLOCZ(cfg->osh, WLC_IOCTL_SMLEN);
	if (!iov_buf) {
		WL_ERR(("%s: iov_buf alloc failed! %d bytes\n", __FUNCTION__, WLC_IOCTL_SMLEN));
		err = BCME_NOMEM;
		goto exit;
	}
	iov_buf->version = WL_FILS_IOV_VERSION;
	iov_buf->id = WL_FILS_CMD_ADD_CONNECT_PARAMS;
	/* check if this should be len w/o headers */
	err = bcm_xtlv_buf_init(&tbuf, (uint8*)&iov_buf->data[0],
		WLC_IOCTL_SMLEN - sizeof(bcm_iov_buf_t) + sizeof(uint16),
		BCM_XTLV_OPTION_ALIGN32);
	if (err != BCME_OK) {
		WL_ERR(("%s: xtlv_context initialization failed\n", __FUNCTION__));
		goto exit;
	}
	if (sme->fils_erp_username_len && sme->fils_erp_username != NULL) {
		err = bcm_xtlv_put_data(&tbuf, WL_FILS_XTLV_ERP_USERNAME,
			sme->fils_erp_username, sme->fils_erp_username_len);
		if (err != BCME_OK) {
			WL_ERR(("%s: write xtlv failed\n", __FUNCTION__));
			goto exit;
		}
	}
	if (sme->fils_erp_realm_len && sme->fils_erp_realm != NULL) {
		err = bcm_xtlv_put_data(&tbuf, WL_FILS_XTLV_ERP_REALM,
			sme->fils_erp_realm, sme->fils_erp_realm_len);
		if (err != BCME_OK) {
			WL_ERR(("%s: write xtlv failed\n", __FUNCTION__));
			goto exit;
		}
	}
	if (sme->fils_erp_rrk_len && sme->fils_erp_rrk != NULL) {
		err = bcm_xtlv_put_data(&tbuf, WL_FILS_XTLV_ERP_RRK,
			sme->fils_erp_rrk, sme->fils_erp_rrk_len);
		if (err != BCME_OK) {
			WL_ERR(("%s: write xtlv failed\n", __FUNCTION__));
			goto exit;
		}
	}
	err = bcm_xtlv_put_data(&tbuf, WL_FILS_XTLV_ERP_NEXT_SEQ_NUM,
			(u8 *)&sme->fils_erp_next_seq_num, sizeof(sme->fils_erp_next_seq_num));
	if (err != BCME_OK) {
		WL_ERR(("%s: write xtlv failed\n", __FUNCTION__));
		goto exit;
	}
	iov_buf->len = bcm_xtlv_buf_len(&tbuf);
	err = wldev_iovar_setbuf(dev, "fils", iov_buf, iov_buf->len + sizeof(bcm_iov_buf_t) -
		sizeof(uint16), cfg->ioctl_buf, WLC_IOCTL_SMLEN, &cfg->ioctl_buf_sync);
	if (unlikely(err)) {
		 WL_ERR(("set fils params ioctl error (%d)\n", err));
		 goto exit;
	}

exit:
	if (err != BCME_OK) {
		WL_ERR(("set FILS params error %d\n", err));
	}
	else {
		WL_DBG_MEM(("FILS parameters succesfully applied\n"));
	}
	if (iov_buf) {
		MFREE(cfg->osh, iov_buf, WLC_IOCTL_SMLEN);
	}
	return err;
}

#if !defined(WL_FILS_ROAM_OFFLD) && defined(WL_FILS)
static s32
wl_get_bcn_timeout(struct net_device *dev, u32 *bcn_timeout)
{
	s32 err = 0;

	err = wldev_iovar_getint(dev, "bcn_timeout", bcn_timeout);
	if (unlikely(err)) {
		WL_ERR(("could not get bcn_timeout (%d)\n", err));
	}
	return err;
}

#define WL_ROAM_ENABLE	0
#define WL_ROAM_DISABLE 1
/* Beacon Timeout beacon loss in case FILS roaming offload is not supported by fw */
#define WL_BCN_TIMEOUT	3

static s32
wl_fils_toggle_roaming(struct net_device *dev, u32 auth_type)
{
	s32 err = 0;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	if (WPA2_AUTH_IS_FILS(auth_type) && !cfg->fils_info.fils_roam_disabled) {
		err = wl_get_bcn_timeout(dev, &cfg->fils_info.fils_bcn_timeout_cache);
		if (unlikely(err)) {
			return err;
		}
		wl_dongle_roam(dev, WL_ROAM_DISABLE, WL_BCN_TIMEOUT);
		cfg->fils_info.fils_roam_disabled = true;
		WL_DBG_MEM(("fw roam disabled for FILS akm\n"));
	} else if (cfg->fils_info.fils_roam_disabled) {
		/* Enable roaming back for other auth types */
		wl_dongle_roam(dev, WL_ROAM_ENABLE, cfg->fils_info.fils_bcn_timeout_cache);
		cfg->fils_info.fils_roam_disabled = false;
		WL_DBG_MEM(("fw roam enabled\n"));
	}
	return err;
}
#endif /* !WL_FILS_ROAM_OFFLD && WL_FILS */
#endif /* WL_FILS */

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
#endif /* CUSTOMER_HW6 */

#ifndef CUSTOMER_HW6
			case WL_AKM_SUITE_SHA256_1X:
				val = WPA2_AUTH_1X_SHA256;
				break;
			case WL_AKM_SUITE_SHA256_PSK:
				val = WPA2_AUTH_PSK_SHA256;
				break;
#endif /* CUSTOMER_HW6 */
#endif /* MFP */
			case WLAN_AKM_SUITE_8021X:
			case WLAN_AKM_SUITE_PSK:
#if defined(WLFBT) && defined(WLAN_AKM_SUITE_FT_8021X)
			case WLAN_AKM_SUITE_FT_8021X:
#endif
#if defined(WLFBT) && defined(WLAN_AKM_SUITE_FT_PSK)
			case WLAN_AKM_SUITE_FT_PSK:
#endif
			case WLAN_AKM_SUITE_FILS_SHA256:
			case WLAN_AKM_SUITE_FILS_SHA384:
			case WLAN_AKM_SUITE_8021X_SUITE_B:
			case WLAN_AKM_SUITE_8021X_SUITE_B_192:
#ifdef WL_OWE
			case WLAN_AKM_SUITE_OWE:
#endif /* WL_OWE */
#if defined(WL_SAE) || defined(WL_CLIENT_SAE)
			case WLAN_AKM_SUITE_SAE:
#endif /* WL_SAE || WL_CLIENT_SAE */
#ifdef WL_SAE_FT
			case WLAN_AKM_SUITE_FT_OVER_SAE:
#endif /* WL_SAE_FT */
			case WLAN_AKM_SUITE_DPP:
			case WLAN_AKM_SUITE_FT_8021X_SHA384:
				val = wl_rsn_akm_wpa_auth_lookup(sme->crypto.akm_suites[0]);
				break;
			case WLAN_AKM_SUITE_FT_FILS_SHA256:
				val = WPA2_AUTH_FILS_SHA256 | WPA2_AUTH_FT;
				break;
			case WLAN_AKM_SUITE_FT_FILS_SHA384:
				val = WPA2_AUTH_FILS_SHA384 | WPA2_AUTH_FT;
				break;
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
				WL_ERR(("invalid akm suite (0x%x)\n",
					sme->crypto.akm_suites[0]));
				return -EINVAL;
			}
		}
#endif

#ifdef WL_FILS
#if !defined(WL_FILS_ROAM_OFFLD)
	err = wl_fils_toggle_roaming(dev, val);
	if (unlikely(err)) {
		return err;
	}
#endif /* !WL_FILS_ROAM_OFFLD */
#endif /* !WL_FILS */

#ifdef MFP
		if ((err = wl_cfg80211_set_mfp(cfg, dev, sme)) < 0) {
			WL_ERR(("MFP set failed err:%d\n", err));
			return -EINVAL;
		}
#endif /* MFP */

		WL_DBG_MEM(("[%s] wl wpa_auth to 0x%x\n", dev->name, val));
		err = wldev_iovar_setint_bsscfg(dev, "wpa_auth", val, bssidx);
		if (unlikely(err)) {
			WL_ERR(("could not set wpa_auth (0x%x)\n", err));
			return err;
		}
	}
	sec = wl_read_prof(cfg, dev, WL_PROF_SEC);
	sec->wpa_auth = sme->crypto.akm_suites[0];
	sec->fw_wpa_auth = val;

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
			NL80211_WPA_VERSION_2)) &&

#ifdef BCMWAPI_WPI
			!is_wapi(sec->cipher_pairwise) &&
#endif

			(sec->cipher_pairwise & (WLAN_CIPHER_SUITE_WEP40 |
			WLAN_CIPHER_SUITE_WEP104)))
		{
			bzero(&key, sizeof(key));
			key.len = (u32) sme->key_len;
			key.index = (u32) sme->key_idx;
			if (unlikely(key.len > sizeof(key.data))) {
				WL_ERR(("Too long key length (%u)\n", key.len));
				return -EINVAL;
			}
			memcpy(key.data, sme->key, key.len);
			key.flags = WL_PRIMARY_KEY;
			if ((sec->cipher_pairwise == WLAN_CIPHER_SUITE_WEP40) ||
			    (sec->cipher_pairwise == WLAN_CIPHER_SUITE_WEP104)) {
				key.algo = wl_rsn_cipher_wsec_key_algo_lookup(sec->cipher_pairwise);
			} else {
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
			WL_INFORM_MEM(("key applied to fw\n"));
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

#if defined (CUSTOM_SET_CPUCORE) || defined(CONFIG_TCPACK_FASTTX)
static bool wl_get_chan_isvht80(struct net_device *net, dhd_pub_t *dhd)
{
	u32 chanspec = 0;
	bool isvht80 = 0;

	if (wldev_iovar_getint(net, "chanspec", (s32 *)&chanspec) == BCME_OK)
		chanspec = wl_chspec_driver_to_host(chanspec);

	isvht80 = chanspec & WL_CHANSPEC_BW_80;
	WL_DBG(("wl_get_chan_isvht80: chanspec(%x:%d)\n", chanspec, isvht80));

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
#ifdef BCMDONGLEHOST
		dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
		BCM_REFERENCE(dhdp);
		DHD_STATLOG_CTRL(dhdp, ST(DISASSOC_INT_START),
			dhd_net2idx(dhdp->info, dev), DOT11_RC_DISASSOC_LEAVING);
#endif /* BCMDONGLEHOST */
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
		wl_clr_drv_status(cfg, CONNECTED, dev);
		return BCME_NOTREADY;
	}
	return BCME_OK;
}

#ifdef WL_FILS
static int
wl_fils_add_hlp_container(struct bcm_cfg80211 *cfg, struct net_device *dev,
	const uint8* ie_buf, uint16 ie_len)
{
	const bcm_tlv_ext_t *hlp_ie;

	if ((hlp_ie = (const bcm_tlv_ext_t*)bcm_parse_tlvs_dot11((const uint8 *)ie_buf, ie_len,
		FILS_HLP_CONTAINER_EXT_ID, TRUE))) {
		u16 hlp_len = hlp_ie->len;
		u16 left_len = (ie_len - ((const uint8*)hlp_ie - ie_buf));
		bcm_iov_buf_t *iov_buf = 0;
		uint8* pxtlv;
		int err;
		size_t iov_buf_len;
		bcm_tlv_dot11_frag_tot_len(ie_buf, ie_len, FILS_HLP_CONTAINER_EXT_ID,
			TRUE, (uint*)&hlp_len);

		hlp_len += BCM_TLV_EXT_HDR_SIZE;

		if ((hlp_len > DOT11_MAX_MPDU_BODY_LEN) || (hlp_len > left_len)) {
			WL_ERR(("bad HLP length %d\n", hlp_len));
			return EFAULT;
		}
		iov_buf_len = sizeof(bcm_iov_buf_t) + sizeof(bcm_xtlv_t) - 1 + hlp_len;
		iov_buf = MALLOCZ(cfg->osh, iov_buf_len);
		if (iov_buf == NULL) {
			WL_ERR(("failed to allocated iov_buf\n"));
			return ENOMEM;
		}

		prhex("HLP, HLP", (const uchar *)hlp_ie, hlp_len);

		pxtlv = (uint8 *)&iov_buf->data[0];
		((bcm_xtlv_t*)pxtlv)->id = WL_FILS_XTLV_HLP_IE;
		((bcm_xtlv_t*)pxtlv)->len = hlp_len;

		memcpy(((bcm_xtlv_t*)pxtlv)->data, hlp_ie, ((bcm_xtlv_t*)pxtlv)->len);

		iov_buf->version = WL_FILS_IOV_VERSION;
		iov_buf->id = WL_FILS_CMD_ADD_HLP_IE;
		iov_buf->len = ((sizeof(bcm_xtlv_t)-1) + ((bcm_xtlv_t*)pxtlv)->len);

		err = wldev_iovar_setbuf(dev, "fils", iov_buf,
				sizeof(bcm_iov_buf_t) + iov_buf->len,
				cfg->ioctl_buf, WLC_IOCTL_MAXLEN, &cfg->ioctl_buf_sync);
		if (unlikely(err)) {
			WL_ERR(("fils wldev_iovar_setbuf error (%d)\n", err));
		}
		else {
			WL_DBG_MEM(("FILS HLP Packet succesfully updated\n"));
		}
		MFREE(cfg->osh, iov_buf, iov_buf_len);
	}
	return BCME_OK;
}
#endif /* WL_FILS */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0))
#define UPDATE_ASSOC_IES	BIT(0)
#ifndef UPDATE_FILS_ERP_INFO
#define UPDATE_FILS_ERP_INFO	BIT(1)
#define UPDATE_AUTH_TYPE	BIT(2)
#endif

static int
wl_cfg80211_update_connect_params(struct wiphy *wiphy, struct net_device *dev,
	struct cfg80211_connect_params *sme, u32 changed)
{
	s32 err = BCME_OK;
#if defined(WL_FILS)
	if (changed & UPDATE_FILS_ERP_INFO) {
		err = wl_set_fils_params(dev, sme);
		if (unlikely(err)) {
			WL_ERR(("Invalid FILS params\n"));
			goto exit;
		}
		if (!(changed & UPDATE_AUTH_TYPE)) {
			WL_DBG(("Warning: FILS ERP params are set,"
				"but authentication type - not\n"));
		}
	}
	if (changed & UPDATE_AUTH_TYPE) {
		err = wl_set_auth_type(dev, sme);
		if (unlikely(err)) {
			WL_ERR(("Invalid auth type\n"));
			goto exit;
		}
	}
#endif /* WL_FILS */
	if (changed & UPDATE_ASSOC_IES) {
		WL_DBG(("update assoc ies\n"));
		err = wl_cfg80211_set_mgmt_vndr_ies(wl_get_cfg(dev), ndev_to_cfgdev(dev),
			wl_get_bssidx_by_wdev(wl_get_cfg(dev), dev->ieee80211_ptr),
			VNDR_IE_ASSOCREQ_FLAG, sme->ie, sme->ie_len);
		if (err) {
			WL_ERR(("error updating vndr ies\n"));
			goto exit;
		}
	}
exit:
	return err;
}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0) */

#if defined(ROAM_ENABLE) && defined(ROAM_AP_ENV_DETECTION)
static s32
wl_config_roam_env_detection(struct bcm_cfg80211 *cfg, struct net_device *dev)
{
#if defined(BCMDONGLEHOST)
	dhd_pub_t *dhdp =  (dhd_pub_t *)(cfg->pub);
#endif /* BCMDONGLEHOST */
	s32 roam_trigger[2] = {0, 0};
	s32 err = BCME_OK;

	if (dhdp->roam_env_detection && (dev == bcmcfg_to_prmry_ndev(cfg))) {
		bool is_roamtrig_reset = TRUE;
		bool is_roam_env_ok = (wldev_iovar_setint(dev, "roam_env_detection",
				AP_ENV_DETECT_NOT_USED) == BCME_OK);
#ifdef SKIP_ROAM_TRIGGER_RESET
		roam_trigger[1] = WLC_BAND_2G;
		is_roamtrig_reset =
			(wldev_ioctl_get(dev, WLC_GET_ROAM_TRIGGER, roam_trigger,
				sizeof(roam_trigger)) == BCME_OK) &&
			(roam_trigger[0] == WL_AUTO_ROAM_TRIGGER-10);
#endif /* SKIP_ROAM_TRIGGER_RESET */
		if (is_roamtrig_reset && is_roam_env_ok) {
			roam_trigger[0] = WL_AUTO_ROAM_TRIGGER;
			roam_trigger[1] = WLC_BAND_ALL;
			err = wldev_ioctl_set(dev, WLC_SET_ROAM_TRIGGER, roam_trigger,
				sizeof(roam_trigger));
			if (unlikely(err)) {
				WL_ERR((" failed to restore roam_trigger for auto env"
						" detection. err:%d\n", err));
			}
		}
	}
	return err;
}
#endif /* ROAM_ENABLE && ROAMENV_DETECTION */

s32
wl_do_preassoc_ops(struct bcm_cfg80211 *cfg,
		struct net_device *dev, struct cfg80211_connect_params *sme)
{
#if defined(BCMDONGLEHOST)
	dhd_pub_t *dhdp =  (dhd_pub_t *)(cfg->pub);
#endif /* BCMDONGLEHOST */

#ifdef BCMDONGLEHOST
	BCM_REFERENCE(dhdp);
	DHD_STATLOG_CTRL(dhdp, ST(ASSOC_START), dhd_net2idx(dhdp->info, dev), 0);
#endif /* BCMDONGLEHOST */

#ifdef DHDTCPSYNC_FLOOD_BLK
	dhd_reset_tcpsync_info_by_dev(dev);
#endif /* DHDTCPSYNC_FLOOD_BLK */

	if (wl_get_drv_status(cfg, SCANNING, dev)) {
		wl_cfgscan_cancel_scan(cfg);
	}

#ifdef WL_SCHED_SCAN
	/* Locks are taken in wl_cfg80211_sched_scan_stop()
	 * A start scan occuring during connect is unlikely
	 */
	if (cfg->sched_scan_req) {
		struct wireless_dev *wdev = dev->ieee80211_ptr;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0))
		wl_cfg80211_sched_scan_stop(wdev->wiphy, bcmcfg_to_prmry_ndev(cfg),
				cfg->sched_scan_req->reqid);
#else
		wl_cfg80211_sched_scan_stop(wdev->wiphy, bcmcfg_to_prmry_ndev(cfg));
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)) */
	}
#endif /* WL_SCHED_SCAN */
#ifdef WL_CFG80211_GON_COLLISION
	/* init block gon req count  */
	cfg->block_gon_req_tx_count = 0;
	cfg->block_gon_req_rx_count = 0;
#endif /* WL_CFG80211_GON_COLLISION */

#if defined(USE_DYNAMIC_MAXPKT_RXGLOM)
	maxrxpktglom = 0;
#endif

#if defined (ROAM_ENABLE) && defined (ROAM_AP_ENV_DETECTION)
	if (wl_config_roam_env_detection(cfg, dev) != BCME_OK) {
		return BCME_ERROR;
	}
#endif /* ROAM_ENABLE && ROAM_AP_ENV_DETECTION */

#ifdef WLTDLS
	/* disable TDLS if number of connected interfaces is >= 1 */
	wl_cfg80211_tdls_config(cfg, TDLS_STATE_CONNECT, false);
#endif /* WLTDLS */

#ifdef SUPPORT_AP_BWCTRL
	if (dhdp->op_mode & DHD_FLAG_HOSTAP_MODE) {
		wl_restore_ap_bw(cfg);
	}
#endif /* SUPPORT_AP_BWCTRL */
#if defined(ROAMEXP_SUPPORT)
	/* Clear Blacklist bssid and Whitelist ssid list before join issue
	 * This is temporary fix since currently firmware roaming is not
	 * disabled by android framework before SSID join from framework
	*/
	/* Flush blacklist bssid content */
	dhd_dev_set_blacklist_bssid(dev, NULL, 0, true);
	/* Flush whitelist ssid content */
	dhd_dev_set_whitelist_ssid(dev, NULL, 0, true);
#endif /* ROAMEXP_SUPPORT */

	WL_DBG(("SME IE : len=%zu\n", sme->ie_len));
	if (sme->ie != NULL && sme->ie_len > 0 && (wl_dbg_level & WL_DBG_DBG)) {
		prhex(NULL, sme->ie, sme->ie_len);
	}
	/* Connection attempted via linux-wireless */
	wl_set_drv_status(cfg, CFG80211_CONNECT, dev);
	return BCME_OK;
}

static s32
wl_config_assoc_security(struct bcm_cfg80211 *cfg,
	struct net_device *dev, struct cfg80211_connect_params *sme)
{
	s32 err = BCME_OK;

	err = wl_set_wpa_version(dev, sme);
	if (unlikely(err)) {
		WL_ERR(("Invalid wpa_version\n"));
		goto exit;
	}

	err = wl_set_auth_type(dev, sme);
	if (unlikely(err)) {
		WL_ERR(("Invalid auth type\n"));
		goto exit;
	}

#ifdef WL_FILS
	if (sme->ie && sme->ie_len) {
		err = wl_fils_add_hlp_container(cfg, dev, sme->ie, sme->ie_len);
		if (unlikely(err)) {
			WL_ERR(("FILS sending HLP failed\n"));
			goto exit;
		}
	}
#endif /* WL_FILS */

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

#ifdef WL_FILS
	err = wl_set_fils_params(dev, sme);
	if (unlikely(err)) {
		WL_ERR(("Invalid FILS params\n"));
		goto exit;
	}
#endif /* WL_FILS */

exit:
	return err;
}

static s32
wl_config_assoc_ies(struct bcm_cfg80211 *cfg, struct net_device *dev,
	struct cfg80211_connect_params *sme, wlcfg_assoc_info_t *info)
{
	const wpa_ie_fixed_t *wpa_ie;
	const bcm_tlv_t *wpa2_ie;
	const u8* wpaie = 0;
	u32 wpaie_len;
	s32 err;
	s32 bssidx = info->bssidx;

	/* configure all vendor and extended vendor IEs */
	wl_cfg80211_set_mgmt_vndr_ies(cfg, ndev_to_cfgdev(dev), bssidx,
		VNDR_IE_ASSOCREQ_FLAG, sme->ie, sme->ie_len);

	/* Find the RSNXE_IE and plumb */
	if ((err = wl_cfg80211_config_rsnxe_ie(cfg, dev,
			(const u8*)sme->ie, sme->ie_len)) < 0) {
		WL_ERR(("Failed to configure rsnxe ie: %d\n", err));
		return err;
	}

	/* find the RSN_IE */
	if ((wpa2_ie = bcm_parse_tlvs((const u8 *)sme->ie, sme->ie_len,
		DOT11_MNG_RSN_ID)) != NULL) {
		WL_DBG((" RSN IE is found\n"));
	}

	/* find the WPA_IE */
	if ((wpa_ie = wl_cfgp2p_find_wpaie(sme->ie,
		sme->ie_len)) != NULL) {
		WL_DBG((" WPA IE is found\n"));
	}

	if (wpa_ie != NULL || wpa2_ie != NULL) {
		wpaie = (wpa_ie != NULL) ? (const u8 *)wpa_ie : (const u8 *)wpa2_ie;
		wpaie_len = (wpa_ie != NULL) ? wpa_ie->length : wpa2_ie->len;
		wpaie_len += WPA_RSN_IE_TAG_FIXED_LEN;
	} else {
		wpaie = NULL;
		wpaie_len = 0;
	}

	err = wldev_iovar_setbuf(dev, "wpaie", wpaie, wpaie_len,
		cfg->ioctl_buf, WLC_IOCTL_MAXLEN, &cfg->ioctl_buf_sync);
	if (unlikely(err)) {
		WL_ERR(("wpaie set error (%d)\n", err));
	}

	return err;
}

s32
wl_cfg80211_config_rsnxe_ie(struct bcm_cfg80211 *cfg, struct net_device *dev,
		const u8 *parse, u32 len)
{
	bcm_tlv_t *ie = NULL;
	s32 err = 0;
	u8 ie_len = 0;
	char smbuf[WLC_IOCTL_SMLEN];

	while ((ie = bcm_parse_tlvs(parse, len, DOT11_MNG_RSNXE_ID))) {
		WL_DBG(("Found RSNXE ie\n"));
		break;
	}

	ie_len = (ie != NULL) ? (ie->len + BCM_TLV_HDR_SIZE): 0;

	err = wldev_iovar_setbuf(dev, "rsnxe", ie, ie_len,
		smbuf, sizeof(smbuf), NULL);
	if (!err) {
		WL_DBG(("Configured RSNXE IE\n"));
	} else if (err == BCME_UNSUPPORTED) {
		WL_DBG(("FW does not support rsnxe iovar\n"));
		err = BCME_OK;
	} else {
		WL_ERR(("rsnxe set error (%d)\n", err));
	}
	return err;
}

static s32
wl_fillup_assoc_params_v1(struct bcm_cfg80211 *cfg, struct net_device *dev,
	void *params, u32 buf_len, wlcfg_assoc_info_t *info)
{
	chanspec_t *chanspecs = info->chanspecs;
	u32 chan_cnt = info->chan_cnt;
	u32 join_scan_active_time = 0;
	wl_extjoin_params_v1_t *ext_join_params = (wl_extjoin_params_v1_t *)params;

	if (buf_len < (sizeof(ext_join_params->ssid.SSID) +
		(sizeof(chanspec_t) * chan_cnt))) {
		WL_ERR(("buf too short\n"));
		return -EINVAL;
	}

	if (info->bssid_hint) {
		/* Set bssid hint flag */
		WL_DBG_MEM(("ASSOC_HINT_BSSID_PRESENT. channels:%d\n", chan_cnt));
		ext_join_params->assoc.flags |= ASSOC_HINT_BSSID_PRESENT;
	}

	/* ssid length check is already done above */
	if (memcpy_s(ext_join_params->ssid.SSID, sizeof(ext_join_params->ssid.SSID),
			info->ssid, info->ssid_len) != BCME_OK) {
		WL_ERR(("ssid cpy failed info_len:%d\n", info->ssid_len));
		return -EINVAL;
	}

	ext_join_params->ssid.SSID_len = info->ssid_len;
	wl_update_prof(cfg, dev, NULL, &ext_join_params->ssid, WL_PROF_SSID);
	if (ext_join_params->ssid.SSID_len < IEEE80211_MAX_SSID_LEN) {
		WL_DBG(("ssid \"%s\", len (%d)\n", ext_join_params->ssid.SSID,
			ext_join_params->ssid.SSID_len));
	}
	ext_join_params->ssid.SSID_len = htod32(info->ssid_len);

	/* Use increased dwell for targeted join case to take care of noisy env */
	join_scan_active_time = info->targeted_join ? WL_SCAN_JOIN_ACTIVE_DWELL_TIME_MS :
		WL_BCAST_SCAN_JOIN_ACTIVE_DWELL_TIME_MS;
	ext_join_params->scan.active_time = chan_cnt ? join_scan_active_time : -1;
	ext_join_params->scan.passive_time = chan_cnt ? WL_SCAN_JOIN_PASSIVE_DWELL_TIME_MS : -1;
	/* Set up join scan parameters */
	ext_join_params->scan.scan_type = -1;
	/* WAR to sync with presence period of VSDB GO.
	 * send probe request more frequently
	 * probe request will be stopped when it gets probe response from target AP/GO.
	 */
	ext_join_params->scan.nprobes = chan_cnt ?
		(ext_join_params->scan.active_time/WL_SCAN_JOIN_PROBE_INTERVAL_MS) : -1;
	ext_join_params->scan.home_time = -1;

	(void)memcpy_s(&ext_join_params->assoc.bssid, ETH_ALEN, info->bssid, ETH_ALEN);

	ext_join_params->assoc.chanspec_num = chan_cnt;
	/* source and target lens are same */
	(void)memcpy_s(ext_join_params->assoc.chanspec_list, (sizeof(chanspec_t) * chan_cnt),
		chanspecs, sizeof(chanspec_t) * chan_cnt);

	ext_join_params->assoc.chanspec_num = htod32(chan_cnt);
	return BCME_OK;
}

static s32
wl_fillup_assoc_params_v0(struct bcm_cfg80211 *cfg, struct net_device *dev,
	void *params, u32 buf_len, wlcfg_assoc_info_t *info)
{
	chanspec_t *chanspecs = info->chanspecs;
	u32 chan_cnt = info->chan_cnt;
	u32 join_scan_active_time = 0;
	wl_extjoin_params_t *ext_join_params = (wl_extjoin_params_t *)params;

	if (buf_len < (sizeof(ext_join_params->ssid.SSID) +
		(sizeof(chanspec_t) * chan_cnt))) {
		WL_ERR(("buf too short\n"));
		return -EINVAL;
	}

	/* ssid length check is already done above */
	if (memcpy_s(ext_join_params->ssid.SSID, sizeof(ext_join_params->ssid.SSID),
			info->ssid, info->ssid_len) != BCME_OK) {
		WL_ERR(("ssid cpy failed info_len:%d\n", info->ssid_len));
		return -EINVAL;
	}

	ext_join_params->ssid.SSID_len = info->ssid_len;
	wl_update_prof(cfg, dev, NULL, &ext_join_params->ssid, WL_PROF_SSID);
	if (ext_join_params->ssid.SSID_len < IEEE80211_MAX_SSID_LEN) {
		WL_DBG(("ssid \"%s\", len (%d)\n", ext_join_params->ssid.SSID,
			ext_join_params->ssid.SSID_len));
	}
	ext_join_params->ssid.SSID_len = htod32(info->ssid_len);

	/* Use increased dwell for targeted join case to take care of noisy env */
	join_scan_active_time = info->targeted_join ? WL_SCAN_JOIN_ACTIVE_DWELL_TIME_MS :
		WL_BCAST_SCAN_JOIN_ACTIVE_DWELL_TIME_MS;
	ext_join_params->scan.active_time = chan_cnt ? join_scan_active_time : -1;
	ext_join_params->scan.passive_time = chan_cnt ? WL_SCAN_JOIN_PASSIVE_DWELL_TIME_MS : -1;
	/* Set up join scan parameters */
	ext_join_params->scan.scan_type = -1;
	/* WAR to sync with presence period of VSDB GO.
	 * send probe request more frequently
	 * probe request will be stopped when it gets probe response from target AP/GO.
	 */
	ext_join_params->scan.nprobes = chan_cnt ?
		(ext_join_params->scan.active_time/WL_SCAN_JOIN_PROBE_INTERVAL_MS) : -1;
	ext_join_params->scan.home_time = -1;

	(void)memcpy_s(&ext_join_params->assoc.bssid, ETH_ALEN, info->bssid, ETH_ALEN);

	ext_join_params->assoc.chanspec_num = chan_cnt;
	/* source and target lens are same */
	(void)memcpy_s(ext_join_params->assoc.chanspec_list, (sizeof(chanspec_t) * chan_cnt),
		chanspecs, sizeof(chanspec_t) * chan_cnt);

	ext_join_params->assoc.chanspec_num = htod32(chan_cnt);
	return BCME_OK;
}

static s32
wl_config_assoc_params(struct bcm_cfg80211 *cfg, struct net_device *dev,
	void *params, u32 buf_len, wlcfg_assoc_info_t *info)
{
	s32 ret;

	if (!cfg->join_iovar_ver) {
		ret = wl_fillup_assoc_params_v0(cfg, dev, params, buf_len, info);
	} else {
		ret = wl_fillup_assoc_params_v1(cfg, dev, params, buf_len, info);
	}
	return ret;
}

static s32
wl_handle_assoc_hints(struct bcm_cfg80211 *cfg, struct net_device *dev,
	struct cfg80211_connect_params *sme, wlcfg_assoc_info_t *info)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0))
	bool skip_hints = false;
#endif /* KERNEL >= 3.15 */
	chanspec_t chspec;

	if (!sme || !info) {
		WL_ERR(("wrong args\n"));
		return -EINVAL;
	}

	if (unlikely(!sme->ssid) || (sme->ssid_len > DOT11_MAX_SSID_LEN)) {
		WL_ERR(("Invalid ssid %p. len:%zu\n", sme->ssid, sme->ssid_len));
		return -EINVAL;
	}

	/* Copy SSID detail */
	info->ssid_len = sme->ssid_len;
	if (memcpy_s(info->ssid, sizeof(info->ssid),
			sme->ssid, info->ssid_len) != BCME_OK) {
		WL_ERR(("ssid cpy failed\n"));
		return -EINVAL;
	}

	/* Handle incoming BSSID and Channel info */
	if (sme->bssid && !ETHER_ISBCAST(sme->bssid)) {
		/* Use user space requested BSSID and channel */
		info->targeted_join = true;
		(void)memcpy_s(info->bssid, ETH_ALEN, sme->bssid, ETH_ALEN);
		if (sme->channel && ((chspec =
			wl_freq_to_chanspec(sme->channel->center_freq)) != INVCHANSPEC)) {
			info->chan_cnt = 1;
			info->chanspecs[0] = chspec;
			/* Skip p2p connection on 6G */
#ifdef WL_P2P_6G
			if (!(cfg->p2p_6g_enabled)) {
#endif /* WL_P2P_6G */
				if (IS_P2P_GC(dev->ieee80211_ptr) && (CHSPEC_IS6G(chspec))) {
					WL_ERR(("P2P connection not allowed on 6G\n"));
					return -ENOTSUPP;
				}
#ifdef WL_P2P_6G
			}
#endif /* WL_P2P_6G */
		}
	}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0))
	else {
#ifdef WL_SKIP_CONNECT_HINTS
		skip_hints = true;
		WL_DBG(("force skip connect hints\n"));
#else /* WL_SKIP_CONNECT_HINTS */
		/* override bssid_hint if overridden via module param */
		skip_hints = fw_ap_select;
#if defined(WL_FW_OCE_AP_SELECT)
		/* If fw select needs to be specifically done for OCE */
		skip_hints = fw_ap_select &&
			wl_cfg80211_is_oce_ap(wiphy, sme->bssid_hint);
#endif /* WL_FW_OCE_AP_SELECT */
		WL_DBG(("fw_ap_select:%d skip_hints:%d\n", fw_ap_select, skip_hints));
#endif /* WL_SKIP_CONNECT_HINTS */

		/* Use bssid_hint if hints are allowed and if its unicast addr */
		if (!skip_hints && sme->bssid_hint && !ETHER_ISBCAST(sme->bssid_hint)) {
			WL_INFORM_MEM(("bssid_hint "MACDBG" \n", MAC2STRDBG(sme->bssid_hint)));
			info->targeted_join = true;
			if (cfg->join_iovar_ver) {
				/* Firmware supports bssid_hint feature */
				info->bssid_hint = true;
			}
			(void)memcpy_s(info->bssid, ETH_ALEN, sme->bssid_hint, ETH_ALEN);
#ifndef WL_FORCE_RCC_LIST
			/* Use channel hint only for target bssid join case. In other
			 * cases, use RCC or full scan to find better APs.
			 */
			if (sme->channel_hint && ((chspec = wl_freq_to_chanspec(
				sme->channel_hint->center_freq)) != INVCHANSPEC)) {
				info->chan_cnt = 1;
				info->chanspecs[0] = chspec;
				WL_INFORM_MEM(("channel_hint: chspec(%x)\n", chspec));
			}
#endif /* !WL_FORCE_RCC_LIST */
		}
	}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0) */

	if (info->targeted_join != true) {
		/* For non targeted join, use bcast address */
		(void)memcpy_s(&info->bssid, ETH_ALEN, &ether_bcast, ETH_ALEN);
	}
	WL_DBG(("targeted_join:%d chan_cnt:%d\n",
			info->targeted_join, info->chan_cnt));
	return 0;
}

static s32
wl_sync_fw_assoc_states(struct bcm_cfg80211 *cfg,
		struct net_device *dev, wlcfg_assoc_info_t *info)
{
	s32 err = BCME_OK;
	u8 bssid[ETH_ALEN];

	if (wl_get_drv_status(cfg, CONNECTED, dev) && wl_reassoc_support) {
		/* ROAM case */
		info->reassoc = true;
	} else {
		/* store the bssid for the connect req */
		wl_update_prof(cfg, dev, NULL, info->bssid, WL_PROF_LATEST_BSSID);

		/* following scenarios are possible
		* In case of wrong request/abnormal status,
		* trigger DISASSOC to clean up status.
		* 1. DHD prev status is CONNECTING
		*      => 1.1 Wrong request
		* 2. DHD previous status is CONNECTED
		*      -  FW connected
		*              => Wrong request
		*      - FW not connected
		*              => Abnormal status
		* 3. DHD previous status is DISCONNECTING
		*      => Waiting for disconnecting
		* 4. DHD previous status is not connected
		*      - FW not connected
		*              => Normal status
		*      - FW connected
		*              => Abnormal status
		*/
		if (wl_get_drv_status(cfg, CONNECTING, dev) ||
			wl_get_drv_status(cfg, CONNECTED, dev)) {
			/* set nested connect bit to identify the context */
			wl_set_drv_status(cfg, NESTED_CONNECT, dev);
			/* DHD prev status is CONNECTING/CONNECTED */
			wl_cfg80211_cleanup_mismatch_status(dev, cfg, TRUE);
		} else if (wl_get_drv_status(cfg, DISCONNECTING, dev)) {
			/* DHD prev status is DISCONNECTING */
			wl_cfg80211_cleanup_mismatch_status(dev, cfg, false);
		} else if (!wl_get_drv_status(cfg, CONNECTED, dev)) {
			/* DHD previous status is not connected and FW connected */
			if (wldev_ioctl_get(dev, WLC_GET_BSSID, &bssid, ETHER_ADDR_LEN) == 0) {
				/* set nested connect bit to identify the context */
				wl_set_drv_status(cfg, NESTED_CONNECT, dev);
				wl_cfg80211_cleanup_mismatch_status(dev, cfg, true);
			}
		}
#ifdef WL_EXT_IAPSTA
		wl_ext_in4way_sync(dev, STA_WAIT_DISCONNECTED, WL_EXT_STATUS_DISCONNECTING, NULL);
#endif
	}

	/* Clear BSSID if disconnecting state is not in progress */
	bzero(&bssid, sizeof(bssid));
	if (!info->reassoc && !wl_get_drv_status(cfg, DISCONNECTING, dev)) {
		wl_update_prof(cfg, dev, NULL, (void *)&bssid, WL_PROF_BSSID);
	}

	LOG_TS(cfg, conn_start);
	CLR_TS(cfg, authorize_start);
	/* clear nested connect bit on proceeding for connection */
	wl_clr_drv_status(cfg, NESTED_CONNECT, dev);

	if (!info->reassoc) {
		/* 'connect' request received */
		wl_set_drv_status(cfg, CONNECTING, dev);
	}

	return err;
}

#if defined(DBG_PKT_MON) && defined(BCMDONGLEHOST)
void
wl_pkt_mon_start(struct bcm_cfg80211 *cfg, struct net_device *dev)
{
	if (dev == bcmcfg_to_prmry_ndev(cfg)) {
		dhd_pub_t *dhdp =  (dhd_pub_t *)(cfg->pub);
		DHD_DBG_PKT_MON_START(dhdp);
	}
}
#endif /* DBG_PKT_MON && BCMDONGLEHOST */

static s32
wl_ext_get_rssi(struct bcm_cfg80211 *cfg, u8 *bssid)
{
	wl_scan_results_t *bss_list;
	wl_bss_info_t *bi = NULL;
	s32 i, rssi = 0;

	mutex_lock(&cfg->scan_sync);
	bss_list = cfg->bss_list;
	bi = next_bss(bss_list, bi);
	for_each_bss(bss_list, bi, i) {
		if (!memcmp(&bi->BSSID, bssid, ETHER_ADDR_LEN))
			rssi = dtoh32(bi->RSSI);
	}
	mutex_unlock(&cfg->scan_sync);

	return rssi;
}

void
wl_conn_debug_info(struct bcm_cfg80211 *cfg, struct net_device *dev, wlcfg_assoc_info_t *info)
{
	struct wl_security *sec = wl_read_prof(cfg, dev, WL_PROF_SEC);
	char sec_info[64];
	u32 chanspec = info->chanspecs[0];
	u32 chan_cnt = info->chan_cnt;
	s32 cur_rssi = 0, target_rssi = 0;
	scb_val_t scb_val;
	s32 err = BCME_OK;

	if (!sec) {
		WL_ERR(("no sec?\n"));
		return;
	}

	target_rssi = wl_ext_get_rssi(cfg, info->bssid);
	wl_ext_get_sec(dev, 0, sec_info, sizeof(sec_info), TRUE);
	if (info->reassoc) {
		memset(&scb_val, 0, sizeof(scb_val_t));
		err = wldev_ioctl_get(dev, WLC_GET_RSSI, &scb_val, sizeof(scb_val_t));
		if (!err)
			cur_rssi = dtoh32(scb_val.val);
		WL_MSG(dev->name, "Reconnecting with " MACDBG " ssid \"%s\", len (%d), "
			"channel=%s-%d(chan_cnt=%d), sec=%s, rssi=%d => %d\n",
			MAC2STRDBG((u8*)(&info->bssid)), info->ssid, info->ssid_len,
			CHSPEC2BANDSTR(chanspec), wf_chspec_ctlchan(chanspec), chan_cnt,
			sec_info, cur_rssi, target_rssi);
	} else {
		WL_MSG(dev->name, "Connecting with " MACDBG " ssid \"%s\", len (%d), "
			"channel=%s-%d(chan_cnt=%d), sec=%s, rssi=%d\n",
			MAC2STRDBG((u8*)(&info->bssid)), info->ssid, info->ssid_len,
			CHSPEC2BANDSTR(chanspec), wf_chspec_ctlchan(chanspec), chan_cnt,
			sec_info, target_rssi);
	}
	if (wl_dbg_level & WL_DBG_DBG) {
		WL_MSG(dev->name, "akm:0x%x auth:0x%x wpaver:0x%x pwise:0x%x gwise:0x%x\n",
				sec->wpa_auth, sec->auth_type, sec->wpa_versions,
				sec->cipher_pairwise, sec->cipher_group);
		WL_MSG(dev->name, "wpa_auth:0x%x auth:0x%x wsec:0x%x mfp:0x%x\n",
				sec->fw_wpa_auth, sec->fw_auth, sec->fw_wsec, sec->fw_mfp);
		/* print channels for assoc */
		prhex("chanspecs", (const u8 *)info->chanspecs,
				(info->chan_cnt * sizeof(chanspec_t)));
	}
	SUPP_LOG(("[%s] Connecting with " MACDBG " ssid \"%s\",chan_cnt:%d\n",
		dev->name, MAC2STRDBG((u8*)(&info->bssid)),
		info->ssid, info->chan_cnt));
}

s32
wl_handle_join(struct bcm_cfg80211 *cfg,
	struct net_device *dev, wlcfg_assoc_info_t *assoc_info)
{
	s32 err = 0;
	size_t join_params_size;
	void *join_params = NULL;

	if (!cfg->join_iovar_ver) {
		join_params_size = WL_EXTJOIN_PARAMS_FIXED_SIZE +
			assoc_info->chan_cnt * sizeof(chanspec_t);
	} else if (cfg->join_iovar_ver == WL_EXTJOIN_VERSION_V1) {
		/* Use version join struct */
		join_params_size = WL_EXTJOIN_PARAMS_FIXED_SIZE_V1 +
			assoc_info->chan_cnt * sizeof(chanspec_t);
	} else {
		WL_ERR(("Unsupported join iovar version\n"));
		return -EINVAL;
	}

	join_params = MALLOCZ(cfg->osh, join_params_size);
	if (join_params == NULL) {
		err = -ENOMEM;
		WL_ERR(("Mem alloc for join_params failed\n"));
		goto fail;
	}

	/* Fill up the join params */
	err = wl_config_assoc_params(cfg, dev, join_params, join_params_size,
			assoc_info);
	if (unlikely(err)) {
		WL_ERR(("config assoc ies failed\n"));
		goto fail;
	}

#ifdef WL_EXT_IAPSTA
	wl_ext_iapsta_update_channel(dev, assoc_info->chanspecs[0]);
#endif
	/* print relevant info for debug purpose */
	wl_conn_debug_info(cfg, dev, assoc_info);
	err = wldev_iovar_setbuf_bsscfg(dev, "join", join_params, join_params_size,
		cfg->ioctl_buf, WLC_IOCTL_MAXLEN, assoc_info->bssidx, &cfg->ioctl_buf_sync);
	if (err) {
		WL_ERR(("join iovar failed. err:%d\n", err));
	}

fail:
	if (join_params) {
		MFREE(cfg->osh, join_params, join_params_size);
	}
	return err;
}

s32
wl_handle_reassoc(struct bcm_cfg80211 *cfg, struct net_device *dev,
		wlcfg_assoc_info_t *info)
{
	wl_reassoc_params_t *reassoc_params = NULL;
	s32 err = BCME_OK;
	size_t reassoc_params_size;
	u32 chan_cnt = info->chan_cnt;
	chanspec_t *chanspecs = info->chanspecs;
	u32 chanspec = info->chanspecs[0];

	if (!cfg->join_iovar_ver) {
		reassoc_params_size = WL_ASSOC_PARAMS_FIXED_SIZE +
			chan_cnt * sizeof(chanspec_t);
	} else if (cfg->join_iovar_ver == WL_EXTJOIN_VERSION_V1) {
		reassoc_params_size = WL_ASSOC_PARAMS_FIXED_SIZE_V1 +
			chan_cnt * sizeof(chanspec_t);
	} else {
		WL_ERR(("Unsupported join iovar version\n"));
		err = -EINVAL;
		goto fail;
	}

	reassoc_params = MALLOCZ(cfg->osh, reassoc_params_size);
	if (reassoc_params == NULL) {
		err = -ENOMEM;
		WL_ERR(("Mem alloc for reassoc_params failed\n"));
		goto fail;
	}

	(void)memcpy_s(&reassoc_params->bssid.octet, ETH_ALEN, info->bssid, ETH_ALEN);
	(void)memcpy_s(reassoc_params->chanspec_list, (sizeof(chanspec_t) * chan_cnt),
		chanspecs, sizeof(chanspec_t) * chan_cnt);
	reassoc_params->chanspec_num = htod32(chan_cnt);

#ifdef WL_EXT_IAPSTA
	wl_ext_iapsta_update_channel(dev, chanspec);
#endif
	/* print relevant info for debug purpose */
	wl_conn_debug_info(cfg, dev, info);
	err = wldev_ioctl_set(dev, WLC_REASSOC, reassoc_params, sizeof(wl_reassoc_params_t));
	if (unlikely(err)) {
		WL_ERR(("reassoc failed, error=%d\n", err));
		goto fail;
	}

fail:
	if (reassoc_params) {
		MFREE(cfg->osh, reassoc_params, reassoc_params_size);
	}
	return err;
}

static s32
wl_cfg80211_connect(struct wiphy *wiphy, struct net_device *dev,
	struct cfg80211_connect_params *sme)
{
	s32 err = BCME_OK;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	wlcfg_assoc_info_t assoc_info;

	WL_DBG(("Enter len=%zu\n", sme->ie_len));
	RETURN_EIO_IF_NOT_UP(cfg);

	/* syncronize the connect states */
	mutex_lock(&cfg->connect_sync);

	bzero(&assoc_info, sizeof(wlcfg_assoc_info_t));
	if ((assoc_info.bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find wlan index from wdev(%p) failed\n", dev->ieee80211_ptr));
		goto fail;
	}

	err = wl_do_preassoc_ops(cfg, dev, sme);
	if (unlikely(err)) {
		WL_ERR(("config assoc channel failed\n"));
		goto fail;
	}

	err = wl_handle_assoc_hints(cfg, dev, sme, &assoc_info);
	if (unlikely(err)) {
		WL_ERR(("assoc hint processing failed\n"));
		goto fail;
	}

	if (wl_sync_fw_assoc_states(cfg, dev, &assoc_info) != BCME_OK) {
		/* attempt best effort */
		WL_ERR(("fw assoc sync failed\n"));
	}

	if (assoc_info.reassoc) {
		/* Handle roam to same ESS */
		if ((err = wl_handle_reassoc(cfg, dev, &assoc_info)) != BCME_OK) {
			goto fail;
		}
	} else {
		err = wl_config_assoc_security(cfg, dev, sme);
		if (unlikely(err)) {
			WL_ERR(("config assoc security failed\n"));
			goto fail;
		}

		err = wl_get_assoc_channels(cfg, dev, &assoc_info);
		if (unlikely(err)) {
			WL_ERR(("get assoc channels failed\n"));
			goto fail;
		}

		err = wl_config_assoc_ies(cfg, dev, sme, &assoc_info);
		if (unlikely(err)) {
			WL_ERR(("config assoc ies failed\n"));
			goto fail;
		}

		if ((err = wl_handle_join(cfg, dev, &assoc_info)) != BCME_OK) {
			goto fail;
		}
	}
	/* Store the minium idx expected */
	cfg->eidx.min_connect_idx = cfg->eidx.enqd;

fail:
	if (unlikely(err)) {
		WL_ERR(("connect error (%d)\n", err));
		wl_clr_drv_status(cfg, CONNECTING, dev);
		CLR_TS(cfg, conn_start);
		/* Flush fw logs */
		wl_flush_fw_log_buffer(dev, FW_LOGSET_MASK_ALL);
#ifdef WLTDLS
		/* If connect fails, check whether we can enable back TDLS */
		wl_cfg80211_tdls_config(cfg, TDLS_STATE_DISCONNECT, false);
#endif /* WLTDLS */
	} else {
#ifdef DBG_PKT_MON
		/* start packet log in adv to ensure that EAPOL msgs aren't missed */
		wl_pkt_mon_start(cfg, dev);
#endif /* DBG_PKT_MON */
	}
#ifdef WL_EXT_IAPSTA
	if (!err)
		wl_ext_in4way_sync(dev, STA_NO_BTC_IN4WAY|STA_REASSOC_RETRY,
			WL_EXT_STATUS_CONNECTING, &assoc_info);
#endif

	mutex_unlock(&cfg->connect_sync);
	return err;
}

static void wl_cfg80211_wait_for_disconnection(struct bcm_cfg80211 *cfg, struct net_device *dev)
{
	uint8 wait_cnt;
	u32 status = 0;

	wait_cnt = WAIT_FOR_DISCONNECT_MAX;
	while ((status = wl_get_drv_status(cfg, DISCONNECTING, dev)) && wait_cnt) {
		WL_DBG(("Waiting for disconnection, wait_cnt: %d\n", wait_cnt));
		wait_cnt--;
		OSL_SLEEP(50);
	}

	WL_INFORM_MEM(("Wait for disconnection done. status:%d wait_cnt:%d\n", status, wait_cnt));
	if (!wait_cnt && wl_get_drv_status(cfg, DISCONNECTING, dev)) {
		/* No response from firmware. Indicate connect result
		 * to clear cfg80211 state machine
		 */
		if (wl_get_drv_status(cfg, CONNECTING, dev)) {
			WL_INFORM_MEM(("force send connect result\n"));
			CFG80211_CONNECT_RESULT(dev, NULL, NULL, NULL, 0, NULL, 0,
				WLAN_STATUS_UNSPECIFIED_FAILURE,
				GFP_KERNEL);
		} else {
			WL_INFORM_MEM(("force send disconnect event\n"));
			CFG80211_DISCONNECTED(dev, WLAN_REASON_DEAUTH_LEAVING,
				NULL, 0, false, GFP_KERNEL);
		}
		CLR_TS(cfg, conn_start);
		CLR_TS(cfg, authorize_start);
		wl_clr_drv_status(cfg, DISCONNECTING, dev);
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
	u8 *curbssid = NULL;
	u8 null_bssid[ETHER_ADDR_LEN];
	s32 bssidx = 0;
	bool connected;
	bool conn_in_progress;
	struct wireless_dev *wdev = dev->ieee80211_ptr;
#ifdef BCMDONGLEHOST
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
#endif /* BCMDONGLEHOST */

	RETURN_EIO_IF_NOT_UP(cfg);

#ifdef BCMDONGLEHOST
	BCM_REFERENCE(dhdp);
	DHD_STATLOG_CTRL(dhdp, ST(DISASSOC_START),
		dhd_net2idx(dhdp->info, dev), reason_code);
#ifdef DHD_4WAYM4_FAIL_DISCONNECT
	dhd_cleanup_m4_state_work(dhdp, dhd_net2idx(dhdp->info, dev));
#endif /* DHD_4WAYM4_FAIL_DISCONNECT */
#endif /* BCMDONGLEHOST */

	connected = wl_get_drv_status(cfg, CONNECTED, dev);
	conn_in_progress = wl_get_drv_status(cfg, CONNECTING, dev);
	curbssid = wl_read_prof(cfg, dev, WL_PROF_BSSID);
	act = *(bool *) wl_read_prof(cfg, dev, WL_PROF_ACT);
	WL_INFORM_MEM(("disconnect in connect state [%d:%d:%d]. reason:%d\n",
		connected, conn_in_progress, act, reason_code));
	if (connected || conn_in_progress) {
		if (curbssid) {
			WL_DBG_MEM(("curbssid:" MACDBG "\n", MAC2STRDBG(curbssid)));
		}
		act = true;
	}

	if (!curbssid) {
		WL_ERR(("Disconnecting while CONNECTING status %d\n", (int)sizeof(null_bssid)));
		bzero(null_bssid, sizeof(null_bssid));
		curbssid = null_bssid;
	}
	WL_MSG(dev->name, "Reason %d, act %d, bssid %pM\n", reason_code, act, curbssid);

	if (act) {
#ifdef DBG_PKT_MON
		/* Stop packet monitor */
		if (dev == bcmcfg_to_prmry_ndev(cfg)) {
			DHD_DBG_PKT_MON_STOP(dhdp);
		}
#endif /* DBG_PKT_MON */
		/*
		* Cancel ongoing scan to sync up with sme state machine of cfg80211.
		*/
		/* Let scan aborted by F/W */
		if (cfg->scan_request) {
			WL_TRACE_HW4(("Aborting the scan! \n"));
			wl_cfgscan_cancel_scan(cfg);
		}
		if (conn_in_progress || connected || wdev->ssid_len) {
				scbval.val = reason_code;
				memcpy(&scbval.ea, curbssid, ETHER_ADDR_LEN);
				scbval.val = htod32(scbval.val);
				WL_INFORM_MEM(("[%s] wl disassoc\n", dev->name));
				/* Set DISCONNECTING state. We are clearing this state
				 in all exit paths
				 */
				wl_set_drv_status(cfg, DISCONNECTING, dev);
				err = wldev_ioctl_set(dev, WLC_DISASSOC, &scbval,
						sizeof(scb_val_t));
				if (unlikely(err)) {
					wl_clr_drv_status(cfg, DISCONNECTING, dev);
					WL_ERR(("error (%d)\n", err));
					goto exit;
				}
#ifdef WL_EXT_IAPSTA
				wl_ext_in4way_sync(dev, STA_NO_BTC_IN4WAY|STA_WAIT_DISCONNECTED,
					WL_EXT_STATUS_DISCONNECTING, NULL);
#endif
		}
#ifdef WL_WPS_SYNC
		/* If are in WPS reauth state, then we would be
		 * dropping the link down events. Ensure that
		 * Event is sent up for the disconnect Req
		 */
		if (wl_wps_session_update(dev,
			WPS_STATE_DISCONNECT, curbssid) == BCME_OK) {
			WL_INFORM_MEM(("[WPS] Disconnect done.\n"));
			wl_clr_drv_status(cfg, DISCONNECTING, dev);
			if (connected) {
				/* Avoid further wl disassoc iovars */
				wl_clr_drv_status(cfg, CONNECTED, dev);
			}
			goto exit;

		}
#endif /* WPS_SYNC */
		wl_cfg80211_wait_for_disconnection(cfg, dev);
	} else {
		/* Not in connected or connection in progres states. Still receiving
		 * disassoc indicates state mismatch with upper layer. Check for state
		 * and issue disconnect indication if required.
		 */

		if (wdev->current_bss || wdev->ssid_len) {
			WL_INFORM_MEM(("report disconnect event\n"));
			CFG80211_DISCONNECTED(dev, 0, NULL, 0, false, GFP_KERNEL);
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

	cfg->rssi = 0;	/* reset backup of rssi */

exit:
	CLR_TS(cfg, conn_start);
	CLR_TS(cfg, authorize_start);

	/* Clear IEs for disaasoc */
	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) >= 0) {
		WL_INFORM_MEM(("Clearing disconnect IEs \n"));
		err = wl_cfg80211_set_mgmt_vndr_ies(cfg,
			ndev_to_cfgdev(dev), bssidx, VNDR_IE_DISASSOC_FLAG, NULL, 0);
	} else {
		WL_ERR(("Find index failed\n"));
		err = -EINVAL;
	}

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
	/* Fix IOT issue with Apple Airport */
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

	WL_MSG(dev->name, "key index (%d)\n", key_idx);
	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find p2p index from wdev(%p) failed\n", dev->ieee80211_ptr));
		return BCME_ERROR;
	}
	bzero(&key, sizeof(key));
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
			const u8 *ivptr;
			ivptr = (const u8 *) params->seq;
			key.rxiv.hi = (ivptr[5] << 24) | (ivptr[4] << 16) |
				(ivptr[3] << 8) | ivptr[2];
			key.rxiv.lo = (ivptr[1] << 8) | ivptr[0];
			key.iv_initialized = true;
		}
		key.algo = wl_rsn_cipher_wsec_key_algo_lookup(params->cipher);
		if (key.algo == CRYPTO_ALGO_OFF) { //not found.
			WL_ERR(("Invalid cipher (0x%x)\n", params->cipher));
			return -EINVAL;
		}
		swap_key_from_BE(&key);
#if defined(BCMDONGLEHOST) && !defined(CUSTOMER_HW4)
		/* need to guarantee EAPOL 4/4 send out before set key */
		dhd_wait_pend8021x(dev);
#endif /* BCMDONGLEHOST && !CUSTOMER_HW4 */
		err = wldev_iovar_setbuf_bsscfg(dev, "wsec_key", &key, sizeof(key),
			cfg->ioctl_buf, WLC_IOCTL_MAXLEN, bssidx, &cfg->ioctl_buf_sync);
		if (unlikely(err)) {
			WL_ERR(("WLC_SET_KEY error (%d)\n", err));
			return err;
		}
		WL_INFORM_MEM(("[%s] wsec key set\n", dev->name));
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

	WL_INFORM_MEM(("[%s] wl roam_offload %d\n", dev->name, enable));
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

struct wireless_dev *
wl_cfg80211_get_wdev_from_ifname(struct bcm_cfg80211 *cfg, const char *name)
{
	struct net_info *iter, *next;

	if (name == NULL) {
		WL_ERR(("Iface name is not provided\n"));
		return NULL;
	}

	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	for_each_ndev(cfg, iter, next) {
		GCC_DIAGNOSTIC_POP();
		if (iter->ndev) {
			if (strcmp(iter->ndev->name, name) == 0) {
				return iter->ndev->ieee80211_ptr;
			}
		}
	}

	WL_DBG(("Iface %s not found\n", name));
	return NULL;
}

#if defined(PKT_FILTER_SUPPORT) && defined(APSTA_BLOCK_ARP_DURING_DHCP)
void
wl_cfg80211_block_arp(struct net_device *dev, int enable)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);

	WL_INFORM_MEM(("[%s] Enter. enable:%d\n", dev->name, enable));
	if (!dhd_pkt_filter_enable) {
		WL_DBG(("Packet filter isn't enabled\n"));
		return;
	}

	/* Block/Unblock ARP frames only if STA is connected to
	 * the upstream AP in case of STA+SoftAP Concurrenct mode
	 */
	if (!wl_get_drv_status(cfg, CONNECTED, dev)) {
		WL_DBG(("STA not connected to upstream AP\n"));
		return;
	}

	if (enable) {
		WL_DBG(("Enable ARP Filter\n"));
		/* Add ARP filter */
		dhd_packet_filter_add_remove(dhdp, TRUE, DHD_BROADCAST_ARP_FILTER_NUM);

		/* Enable ARP packet filter - blacklist */
		dhd_pktfilter_offload_enable(dhdp, dhdp->pktfilter[DHD_BROADCAST_ARP_FILTER_NUM],
			TRUE, FALSE);
	} else {
		WL_DBG(("Disable ARP Filter\n"));
		/* Disable ARP packet filter */
		dhd_pktfilter_offload_enable(dhdp, dhdp->pktfilter[DHD_BROADCAST_ARP_FILTER_NUM],
			FALSE, TRUE);

		/* Delete ARP filter */
		dhd_packet_filter_add_remove(dhdp, FALSE, DHD_BROADCAST_ARP_FILTER_NUM);
	}
}
#endif /* PKT_FILTER_SUPPORT && APSTA_BLOCK_ARP_DURING_DHCP */

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
#ifdef WL_GCMP
	uint32 algos = 0, mask = 0;
#endif /* WL_GCMP */
#if defined(WLAN_CIPHER_SUITE_PMK)
	wsec_pmk_t pmk;
	struct wl_security *sec;
#endif /* defined(WLAN_CIPHER_SUITE_PMK) */
#ifdef BCMDONGLEHOST
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
#endif /* BCMDONGLEHOST */

#if defined (BCMDONGLEHOST)
	if (dhd_query_bus_erros(dhdp)) {
		/* If we are hit with bus error, return success so that
		 * don't repeatedly call del station till we recover.
		 */
		return 0;
	}
#endif /* BCMDONGLEHOST */

	WL_INFORM_MEM(("key index (%d) (0x%x)\n", key_idx, params->cipher));
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

#ifdef BCMDONGLEHOST
	BCM_REFERENCE(dhdp);
	DHD_STATLOG_CTRL(dhdp, ST(INSTALL_KEY), dhd_net2idx(dhdp->info, dev), 0);
#endif /* BCMDONGLEHOST */

	bzero(&key, sizeof(key));
	/* Clear any buffered wep key */
	bzero(&cfg->wep_key, sizeof(struct wl_wsec_key));

	key.len = (u32) params->key_len;
	key.index = (u32) key_idx;

	if (unlikely(key.len > sizeof(key.data))) {
		WL_ERR(("Too long key length (%u)\n", key.len));
		return -EINVAL;
	}
	memcpy(key.data, params->key, key.len);

	key.flags = WL_PRIMARY_KEY;

	key.algo = wl_rsn_cipher_wsec_key_algo_lookup(params->cipher);
	val = wl_rsn_cipher_wsec_algo_lookup(params->cipher);
	if (val == WSEC_NONE) {
		WL_ERR(("Invalid cipher (0x%x), key.len = %d\n", params->cipher, key.len));
#if defined(WLAN_CIPHER_SUITE_PMK)
	/* WLAN_CIPHER_SUITE_PMK is not NL80211 standard ,but BRCM proprietary cipher suite.
	 * so it doesn't have right algo type. Just for now, bypass this check for
	 * backward compatibility.
	 * TODO: deprecate this proprietary way and replace to nl80211 set_pmk API.
	 */
		if (params->cipher != WLAN_CIPHER_SUITE_PMK)
#endif /* defined(WLAN_CIPHER_SUITE_PMK) */
		return -EINVAL;
	}
	switch (params->cipher) {
	case WLAN_CIPHER_SUITE_TKIP:
		if (params->key_len != TKIP_KEY_SIZE) {
			WL_ERR(("wrong TKIP Key length:%d", params->key_len));
			return -EINVAL;
		}
		/* wpa_supplicant switches the third and fourth quarters of the TKIP key */
		if (mode == WL_MODE_BSS) {
			bcopy(&key.data[24], keybuf, sizeof(keybuf));
			bcopy(&key.data[16], &key.data[24], sizeof(keybuf));
			bcopy(keybuf, &key.data[16], sizeof(keybuf));
		}
		WL_DBG(("WLAN_CIPHER_SUITE_TKIP\n"));
		break;
#if defined(WLAN_CIPHER_SUITE_PMK)
	case WLAN_CIPHER_SUITE_PMK:
		sec = wl_read_prof(cfg, dev, WL_PROF_SEC);

		WL_MEM(("set_pmk: wpa_auth:%x akm:%x\n", sec->wpa_auth, params->cipher));
		/* Avoid pmk set for SAE and OWE for external supplicant case. */
		if (IS_AKM_SAE(sec->wpa_auth) || IS_AKM_OWE(sec->wpa_auth)) {
			WL_INFORM_MEM(("skip pmk set for akm:%x\n", sec->wpa_auth));
			break;
		}

		if (params->key_len > sizeof(pmk.key)) {
			WL_ERR(("Worng PMK key length:%d", params->key_len));
			return -EINVAL;
		}
		bzero(&pmk, sizeof(pmk));
		bcopy(params->key, &pmk.key, params->key_len);
		pmk.key_len = params->key_len;
		pmk.flags = 0; /* 0:PMK, WSEC_PASSPHRASE:PSK, WSEC_SAE_PASSPHRASE:SAE_PSK */

		if ((sec->wpa_auth == WLAN_AKM_SUITE_8021X) ||
			(sec->wpa_auth == WL_AKM_SUITE_SHA256_1X)) {
			err = wldev_iovar_setbuf_bsscfg(dev, "okc_info_pmk", pmk.key, pmk.key_len,
				cfg->ioctl_buf, WLC_IOCTL_SMLEN, bssidx, &cfg->ioctl_buf_sync);
			if (err) {
				/* could fail in case that 'okc' is not supported */
				WL_INFORM_MEM(("okc_info_pmk failed, err=%d (ignore)\n", err));
			}
		}

		err = wldev_ioctl_set(dev, WLC_SET_WSEC_PMK, &pmk, sizeof(pmk));
		if (err) {
			WL_ERR(("pmk failed, err=%d (ignore)\n", err));
			return err;
		} else {
			WL_DBG(("pmk set. flags:0x%x\n", pmk.flags));
		}
		/* Clear key length to delete key */
		key.len = 0;
		break;
#endif /* WLAN_CIPHER_SUITE_PMK */
#ifdef WL_GCMP
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
		algos = KEY_ALGO_MASK(key.algo);
		mask = algos | KEY_ALGO_MASK(CRYPTO_ALGO_AES_CCM);
		break;
#endif /* WL_GCMP */
	default: /* No post processing required */
		WL_DBG(("no post processing required (0x%x)\n", params->cipher));
		break;
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
#ifdef WL_GCMP
	wl_set_wsec_info_algos(dev, algos, mask);
#endif /* WL_GCMP */
#ifdef WL_EXT_IAPSTA
	wl_ext_in4way_sync(dev, STA_NO_BTC_IN4WAY, WL_EXT_STATUS_ADD_KEY, NULL);
#endif
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
#ifdef BCMDONGLEHOST
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
#endif /* BCMDONGLEHOST */

#if defined (BCMDONGLEHOST)
	if (dhd_query_bus_erros(dhdp)) {
		/* If we are hit with bus error, return success so that
		 * don't repeatedly call del station till we recover.
		 */
		return 0;
	}
#endif /* BCMDONGLEHOST */

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
#ifdef BCMDONGLEHOST
	BCM_REFERENCE(dhdp);
	DHD_STATLOG_CTRL(dhdp, ST(DELETE_KEY), dhd_net2idx(dhdp->info, dev), 0);
#endif /* BCMDONGLEHOST */
	bzero(&key, sizeof(key));

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

/* NOTE : this function cannot work as is and is never called */
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
	bzero(&key, sizeof(key));
	key.index = key_idx;
	swap_key_to_BE(&key);
	bzero(&params, sizeof(params));
	params.key_len = (u8) min_t(u8, DOT11_MAX_KEY_SIZE, key.len);
	params.key = key.data;

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

#ifdef BCMWAPI_WPI
		case SMS4_ENABLED:
			params.cipher = WLAN_CIPHER_SUITE_SMS4;
			WL_DBG(("WLAN_CIPHER_SUITE_SMS4\n"));
			break;
#endif

#if defined(SUPPORT_SOFTAP_WPAWPA2_MIXED)
		/* to connect to mixed mode AP */
		case (AES_ENABLED | TKIP_ENABLED): /* TKIP CCMP */
			params.cipher = WLAN_CIPHER_SUITE_AES_CMAC;
			WL_DBG(("WLAN_CIPHER_SUITE_TKIP\n"));
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
	/* Firmware seems to use hard coded index for Group Mgmt Key.
	 * TODO/Need to check whether something else needs to be
	 * taken here
	 */
	return 0;
#else
	WL_INFORM_MEM(("Not supported\n"));
	return -EOPNOTSUPP;
#endif /* MFP */
}

static bool
wl_check_assoc_state(struct bcm_cfg80211 *cfg, struct net_device *dev)
{
	wl_assoc_info_t asinfo;
	uint32 state = 0;
	int err;

	err = wldev_iovar_getbuf_bsscfg(dev, "assoc_info",
		NULL, 0, cfg->ioctl_buf, WLC_IOCTL_MEDLEN, 0, &cfg->ioctl_buf_sync);
	if (unlikely(err)) {
		WL_ERR(("failed to get assoc_info : err=%d\n", err));
		return FALSE;
	} else {
		memcpy(&asinfo, cfg->ioctl_buf, sizeof(wl_assoc_info_t));
		state = dtoh32(asinfo.state);
		WL_DBG(("assoc state=%d\n", state));
	}

	return (state > 0)? TRUE:FALSE;
}

static s32
wl_cfg80211_get_rssi(struct net_device *dev, struct bcm_cfg80211 *cfg, s32 *rssi)
{
	s32 err = BCME_OK;
	scb_val_t scb_val;
#ifdef SUPPORT_RSSI_SUM_REPORT
	wl_rssi_ant_mimo_t rssi_ant_mimo;
#endif /* SUPPORT_RSSI_SUM_REPORT */

	if (dev == NULL || cfg == NULL) {
		return BCME_ERROR;
	}

	/* initialize rssi */
	*rssi = 0;

#ifdef SUPPORT_RSSI_SUM_REPORT
	/* Query RSSI sum across antennas */
	bzero(&rssi_ant_mimo, sizeof(rssi_ant_mimo));
	err = wl_get_rssi_per_ant(dev, dev->name, NULL, &rssi_ant_mimo);
	if (err) {
		WL_ERR(("Could not get rssi sum (%d)\n", err));
		/* set rssi to zero and do not return error,
		* because iovar phy_rssi_ant could return BCME_UNSUPPORTED
		* when bssid was null during roaming
		*/
		err = BCME_OK;
	} else {
		cfg->rssi_sum_report = TRUE;
		if ((*rssi = rssi_ant_mimo.rssi_sum) >= 0) {
			*rssi = 0;
		}
	}
#endif /* SUPPORT_RSSI_SUM_REPORT */

	/* if SUPPORT_RSSI_SUM_REPORT works once, do not use legacy method anymore */
	if (cfg->rssi_sum_report == FALSE) {
		bzero(&scb_val, sizeof(scb_val));
		scb_val.val = 0;
		err = wldev_ioctl_get(dev, WLC_GET_RSSI, &scb_val,
			sizeof(scb_val_t));
		if (err) {
			WL_ERR(("Could not get rssi (%d)\n", err));
			return err;
		}
#if defined(RSSIOFFSET)
		*rssi = wl_update_rssi_offset(dev, dtoh32(scb_val.val));
#else
		*rssi = dtoh32(scb_val.val);
#endif
	}

	if (*rssi >= 0) {
		/* check assoc status including roaming */
		DHD_OS_WAKE_LOCK((dhd_pub_t *)(cfg->pub));
		if (wl_get_drv_status(cfg, CONNECTED, dev) && wl_check_assoc_state(cfg, dev)) {
			*rssi = cfg->rssi;	   /* use previous RSSI */
			WL_DBG(("use previous RSSI %d dBm\n", cfg->rssi));
		} else {
			*rssi = 0;
		}
		DHD_OS_WAKE_UNLOCK((dhd_pub_t *)(cfg->pub));
	} else {
		/* backup the current rssi */
		cfg->rssi = *rssi;
	}

	return err;
}

static int
wl_cfg80211_ifstats_counters_cb(void *ctx, const uint8 *data, uint16 type, uint16 len)
{
	switch (type) {
	case WL_IFSTATS_XTLV_IF_INDEX:
		WL_DBG(("Stats received on interface index: %d\n", *data));
		break;
	case WL_IFSTATS_XTLV_GENERIC: {
		if (len > sizeof(wl_if_stats_t)) {
			WL_INFORM(("type 0x%x: cntbuf length too long! %d > %d\n",
				type, len, (int)sizeof(wl_if_stats_t)));
		}
		memcpy(ctx, data, sizeof(wl_if_stats_t));
		break;
	}
	default:
		WL_DBG(("Unsupported counter type 0x%x\n", type));
		break;
	}

	return BCME_OK;
}

/* Parameters to if_counters iovar need to be converted to XTLV format
 * before sending to FW. The length of the top level XTLV container
 * containing parameters should not exceed 228 bytes
 */
#define IF_COUNTERS_PARAM_CONTAINER_LEN_MAX	228

int
wl_cfg80211_ifstats_counters(struct net_device *dev, wl_if_stats_t *if_stats)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
	uint8 *pbuf = NULL;
	bcm_xtlvbuf_t xtlvbuf, local_xtlvbuf;
	bcm_xtlv_t *xtlv;
	uint16 expected_resp_len;
	wl_stats_report_t *request = NULL, *response = NULL;
	int bsscfg_idx;
	int ret = BCME_OK;

	pbuf = (uint8 *)MALLOCZ(dhdp->osh, WLC_IOCTL_MEDLEN);
	if (!pbuf) {
		WL_ERR(("Failed to allocate local pbuf\n"));
		return BCME_NOMEM;
	}

	/* top level container length cannot exceed 228 bytes.
	 * This is because the output buffer is 1535 bytes long.
	 * Allow 1300 bytes for reporting stats coming in XTLV format
	 */
	request = (wl_stats_report_t *)
		MALLOCZ(dhdp->osh, IF_COUNTERS_PARAM_CONTAINER_LEN_MAX);
	if (!request) {
		WL_ERR(("Failed to allocate wl_stats_report_t with length (%d)\n",
			IF_COUNTERS_PARAM_CONTAINER_LEN_MAX));
		ret = BCME_NOMEM;
		goto fail;
	}

	request->version = WL_STATS_REPORT_REQUEST_VERSION_V2;

	/* Top level container... we will create it ourselves */
	/* Leave space for report version, length, and top level XTLV
	 * WL_IFSTATS_XTLV_IF.
	 */
	ret = bcm_xtlv_buf_init(&local_xtlvbuf,
		(uint8*)(request->data) + BCM_XTLV_HDR_SIZE,
		IF_COUNTERS_PARAM_CONTAINER_LEN_MAX -
		offsetof(wl_stats_report_t, data) - BCM_XTLV_HDR_SIZE,
		BCM_XTLV_OPTION_ALIGN32);

	if (ret) {
		goto fail;
	}

	/* Populate requests using this the local_xtlvbuf context. The xtlvbuf
	 * is used to fill the container containing the XTLVs populated using
	 * local_xtlvbuf.
	 */
	ret = bcm_xtlv_buf_init(&xtlvbuf,
		(uint8*)(request->data),
		IF_COUNTERS_PARAM_CONTAINER_LEN_MAX -
		offsetof(wl_stats_report_t, data),
		BCM_XTLV_OPTION_ALIGN32);

	if (ret) {
		goto fail;
	}

	/* Request generic stats */
	ret = bcm_xtlv_put_data(&local_xtlvbuf,
		WL_IFSTATS_XTLV_GENERIC, NULL, 0);
	if (ret) {
		goto fail;
	}

	/* Complete the outer container with type and length
	 * only.
	 */
	ret = bcm_xtlv_put_data(&xtlvbuf,
		WL_IFSTATS_XTLV_IF,
		NULL, bcm_xtlv_buf_len(&local_xtlvbuf));

	if (ret) {
		goto fail;
	}

	request->length = bcm_xtlv_buf_len(&xtlvbuf) +
		offsetof(wl_stats_report_t, data);
	bsscfg_idx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr);

	/* send the command over to the device and get teh output */
	ret = wldev_iovar_getbuf_bsscfg(dev, "if_counters", (void *)request,
		request->length, pbuf, WLC_IOCTL_MEDLEN, bsscfg_idx,
		&cfg->ioctl_buf_sync);
	if (ret < 0) {
		WL_ERR(("if_counters not supported ret=%d\n", ret));
		goto fail;
	}

	/* Reuse request to process response */
	response = (wl_stats_report_t *)pbuf;

	/* version check */
	if (response->version != WL_STATS_REPORT_REQUEST_VERSION_V2) {
		ret = BCME_VERSION;
		goto fail;
	}

	xtlv = (bcm_xtlv_t *)(response->data);

	expected_resp_len =
		(BCM_XTLV_LEN(xtlv) + OFFSETOF(wl_stats_report_t, data));

	/* Check if the received length is as expected */
	if ((response->length > WLC_IOCTL_MEDLEN) ||
		(response->length < expected_resp_len)) {
		ret = BCME_ERROR;
		WL_ERR(("Illegal response length received. Got: %d"
			" Expected: %d. Expected len must be <= %u\n",
			response->length, expected_resp_len, WLC_IOCTL_MEDLEN));
		goto fail;
	}

	/* check the type. The return data will be in
	 * WL_IFSTATS_XTLV_IF container. So check if that container is
	 * present
	 */
	if (BCM_XTLV_ID(xtlv) != WL_IFSTATS_XTLV_IF) {
		ret = BCME_ERROR;
		WL_ERR(("unexpected type received: %d Expected: %d\n",
			BCM_XTLV_ID(xtlv), WL_IFSTATS_XTLV_IF));
		goto fail;
	}

	/* Process XTLVs within WL_IFSTATS_XTLV_IF container */
	ret = bcm_unpack_xtlv_buf(if_stats,
		(uint8*)response->data + BCM_XTLV_HDR_SIZE,
		BCM_XTLV_LEN(xtlv), /* total length of all TLVs in container */
		BCM_XTLV_OPTION_ALIGN32, wl_cfg80211_ifstats_counters_cb);
	if (ret) {
		WL_ERR(("Error unpacking XTLVs in wl_ifstats_counters: %d\n", ret));
	}

fail:
	if (pbuf) {
		MFREE(dhdp->osh, pbuf, WLC_IOCTL_MEDLEN);
	}

	if (request) {
		MFREE(dhdp->osh, request, IF_COUNTERS_PARAM_CONTAINER_LEN_MAX);
	}
	return ret;
}
#undef IF_COUNTERS_PARAM_CONTAINER_LEN_MAX

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
	s32 rssi = 0;
#if defined(SUPPORT_RSSI_SUM_REPORT) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0))
	wl_rssi_ant_mimo_t rssi_ant_mimo;
	int cnt, chains;
#endif /* SUPPORT_RSSI_SUM_REPORT && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)) */
	s32 rate = 0;
	s32 err = 0;
	u16 wl_iftype = 0;
	u16 wl_mode = 0;
	get_pktcnt_t pktcnt;
	wl_if_stats_t *if_stats = NULL;
	sta_info_v4_t *sta = NULL;
#ifdef WL_RATE_INFO
	wl_rate_info_t *rates = NULL;
#endif /* WL_RATE_INFO */
	u8 *curmacp = NULL;
	s8 eabuf[ETHER_ADDR_STR_LEN];
#if defined(BCMDONGLEHOST)
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);
	bool fw_assoc_state = FALSE;
	u32 dhd_assoc_state = 0;
#endif
	void *buf;

	RETURN_EIO_IF_NOT_UP(cfg);

	if (cfg80211_to_wl_iftype(dev->ieee80211_ptr->iftype, &wl_iftype, &wl_mode) < 0) {
		return -EINVAL;
	}

	buf = MALLOC(cfg->osh, WLC_IOCTL_MEDLEN);
	if (buf == NULL) {
		WL_ERR(("wl_cfg80211_get_station: MALLOC failed\n"));
		goto error;
	}

	switch (wl_iftype) {
		case WL_IF_TYPE_STA:
		case WL_IF_TYPE_IBSS:
			if (cfg->roam_offload) {
				struct ether_addr bssid;
				bzero(&bssid, sizeof(bssid));
				err = wldev_ioctl_get(dev, WLC_GET_BSSID, &bssid, ETHER_ADDR_LEN);
				if (err) {
					WL_ERR(("Failed to get current BSSID\n"));
				} else {
					if (memcmp(mac, &bssid.octet, ETHER_ADDR_LEN) != 0) {
						/* roaming is detected */
						err = wl_cfg80211_delayed_roam(cfg, dev, &bssid);
						if (err)
							WL_ERR(("Failed to handle the delayed"
								" roam, err=%d", err));
						mac = (u8 *)bssid.octet;
					}
				}
			}
#if defined(BCMDONGLEHOST)
			dhd_assoc_state = wl_get_drv_status(cfg, CONNECTED, dev);
			DHD_OS_WAKE_LOCK(dhd);
			fw_assoc_state = dhd_is_associated(dhd, 0, &err);
			if (dhd_assoc_state && !fw_assoc_state) {
				/* check roam (join) status */
				if (wl_check_assoc_state(cfg, dev)) {
					fw_assoc_state = TRUE;
					WL_DBG(("roam status\n"));
				}
			}
			DHD_OS_WAKE_UNLOCK(dhd);
#endif
#if defined(BCMDONGLEHOST)
			if (!dhd_assoc_state || !fw_assoc_state)
#else
			if (!wl_get_drv_status(cfg, CONNECTED, dev))
#endif /* defined(BCMDONGLEHOST) */
			{
				WL_ERR(("NOT assoc\n"));
				if (err == -ENODATA)
					goto error;
#if defined(BCMDONGLEHOST)
				if (!dhd_assoc_state) {
					WL_TRACE_HW4(("drv state is not connected \n"));
				}
				if (!fw_assoc_state) {
					WL_TRACE_HW4(("fw state is not associated \n"));
				}
				/* Disconnect due to fw is not associated for
				 * FW_ASSOC_WATCHDOG_TIME ms.
				 * 'err == 0' of dhd_is_associated() and '!fw_assoc_state'
				 * means that BSSID is null.
				 */
				if (dhd_assoc_state && !fw_assoc_state && !err) {
					if (!fw_assoc_watchdog_started) {
						fw_assoc_watchdog_ms = OSL_SYSUPTIME();
						fw_assoc_watchdog_started = TRUE;
						WL_TRACE_HW4(("fw_assoc_watchdog_started \n"));
					} else if (OSL_SYSUPTIME() - fw_assoc_watchdog_ms >
							FW_ASSOC_WATCHDOG_TIME) {
						fw_assoc_watchdog_started = FALSE;
						err = -ENODEV;
						WL_TRACE_HW4(("fw is not associated for %d ms \n",
							(OSL_SYSUPTIME() - fw_assoc_watchdog_ms)));
						goto get_station_err;
					}
				}
#endif /* defined(BCMDONGLEHOST) */
				err = -ENODEV;
				goto error;
			}
#if defined(BCMDONGLEHOST)
			if (dhd_is_associated(dhd, 0, NULL)) {
				fw_assoc_watchdog_started = FALSE;
			}
#endif /* defined(BCMDONGLEHOST) */
			curmacp = wl_read_prof(cfg, dev, WL_PROF_BSSID);
			if (memcmp(mac, curmacp, ETHER_ADDR_LEN)) {
				WL_ERR(("Wrong Mac address: "MACDBG" != "MACDBG"\n",
					MAC2STRDBG(mac), MAC2STRDBG(curmacp)));
			}
			sta = (sta_info_v4_t *)buf;
			bzero(sta, WLC_IOCTL_MEDLEN);
			err = wldev_iovar_getbuf(dev, "sta_info", (const void*)curmacp,
					ETHER_ADDR_LEN, buf, WLC_IOCTL_MEDLEN, NULL);
			if (err < 0) {
				WL_ERR(("GET STA INFO failed, %d\n", err));
				goto error;
			}
			if (sta->ver != WL_STA_VER_4 && sta->ver != WL_STA_VER_5) {
				WL_ERR(("GET STA INFO version mismatch, %d\n", err));
				return BCME_VERSION;
			}
			sta->rx_rate = dtoh32(sta->rx_rate);
			if (sta->rx_rate != 0) {
				sinfo->filled |= STA_INFO_BIT(INFO_RX_BITRATE);
				sinfo->rxrate.legacy = sta->rx_rate / 100;
				WL_DBG(("RX Rate %d Mbps\n", (sta->rx_rate / 1000)));
			}
			/* go through to get another information */
		case WL_IF_TYPE_P2P_GC:
		case WL_IF_TYPE_P2P_DISC:
			if ((err = wl_cfg80211_get_rssi(dev, cfg, &rssi)) != BCME_OK) {
				goto get_station_err;
			}
#if defined(RSSIAVG)
			err = wl_update_connected_rssi_cache(dev, &cfg->g_connected_rssi_cache_ctrl, &rssi);
			if (err) {
				WL_ERR(("Could not get rssi (%d)\n", err));
				goto get_station_err;
			}
			wl_delete_dirty_rssi_cache(&cfg->g_connected_rssi_cache_ctrl);
			wl_reset_rssi_cache(&cfg->g_connected_rssi_cache_ctrl);
#if defined(RSSIOFFSET)
			rssi = wl_update_rssi_offset(dev, rssi);
#endif
#endif
#if !defined(RSSIAVG) && !defined(RSSIOFFSET)
			// terence 20150419: limit the max. rssi to -2 or the bss will be filtered out in android OS
			rssi = MIN(rssi, RSSI_MAXVAL);
#endif
			sinfo->filled |= STA_INFO_BIT(INFO_SIGNAL);
			sinfo->signal = rssi;
			WL_DBG(("RSSI %d dBm\n", rssi));
#if defined(SUPPORT_RSSI_SUM_REPORT) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0))
			/* Query RSSI sum across antennas */
			(void)memset_s(&rssi_ant_mimo,
				sizeof(rssi_ant_mimo), 0, sizeof(rssi_ant_mimo));
			err = wl_get_rssi_per_ant(dev, dev->name, NULL, &rssi_ant_mimo);
			if (err) {
				WL_ERR(("Could not get rssi sum (%d)\n", err));
			} else {
				chains = 0;
				for (cnt = 0; cnt < rssi_ant_mimo.count; cnt++) {
					sinfo->chain_signal[cnt] = rssi_ant_mimo.rssi_ant[cnt];
					chains |= (1 << cnt);
					WL_DBG(("RSSI[%d]: %d dBm\n",
						cnt, rssi_ant_mimo.rssi_ant[cnt]));
				}
				sinfo->chains = chains;
				sinfo->filled |= STA_INFO_BIT(INFO_CHAIN_SIGNAL);
			}
#endif /* SUPPORT_RSSI_SUM_REPORT && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)) */
			/* go through to get another information */
		case WL_IF_TYPE_P2P_GO:
#ifdef WL_RATE_INFO
			/* Get the current tx/rx rate */
			err = wldev_iovar_getbuf(dev, "rate_info", NULL, 0,
				buf, WLC_IOCTL_SMLEN, NULL);
#else
			/* Get the current tx rate */
			err = wldev_ioctl_get(dev, WLC_GET_RATE, &rate, sizeof(rate));
#endif /* WL_RATE_INFO */
			if (err) {
				WL_ERR(("Could not get rate (%d)\n", err));
				goto error;
			} else {
#if defined(USE_DYNAMIC_MAXPKT_RXGLOM)
				int rxpktglom;
#endif
#ifdef WL_RATE_INFO
				rates = (wl_rate_info_t *)buf;
				rates->version = dtoh32(rates->version);
				rates->length = dtoh32(rates->length);
				if (rates->version != WL_RATE_INFO_VERSION) {
					WL_ERR(("RATE_INFO version mismatch\n"));
					err = BCME_VERSION;
					goto error;
				}
				if (rates->length != (uint16)sizeof(wl_rate_info_t)) {
					WL_ERR(("RATE_INFO length mismatch\n"));
					err = BCME_BADLEN;
					goto error;
				}
				/* Report the current tx rate */
				rate = dtoh32(rates->mode_tx_rate);
#else
				/* Report the current tx rate */
				rate = dtoh32(rate);
#endif /* WL_RATE_INFO */
				sinfo->filled |= STA_INFO_BIT(INFO_TX_BITRATE);
				sinfo->txrate.legacy = rate * 5;
				WL_DBG(("Tx rate %d Mbps\n", (rate / 2)));
#if defined(USE_DYNAMIC_MAXPKT_RXGLOM)
				rxpktglom = ((rate/2) > 150) ? 20 : 10;

				if (maxrxpktglom != rxpktglom) {
					maxrxpktglom = rxpktglom;
					WL_DBG(("Rate %d Mbps, update bus:"
						"maxtxpktglom=%d\n", (rate/2), maxrxpktglom));
					err = wldev_iovar_setbuf(dev, "bus:maxtxpktglom",
							(char*)&maxrxpktglom, 4, cfg->ioctl_buf,
							WLC_IOCTL_MAXLEN, &cfg->ioctl_buf_sync);
					if (err < 0) {
						WL_ERR(("set bus:maxtxpktglom failed, %d\n", err));
					}
				}
#endif
#ifdef WL_RATE_INFO
				/* Report the current rx rate */
				rate = dtoh32(rates->mode_rx_rate);
				sinfo->filled |= STA_INFO_BIT(INFO_RX_BITRATE);
				sinfo->rxrate.legacy = rate * 5;
				WL_DBG(("Rx rate %d Mbps\n", (rate / 2)));
#endif /* WL_RATE_INFO */
			}
			if_stats = (wl_if_stats_t *)buf;
			bzero(if_stats, WLC_IOCTL_MEDLEN);
#ifdef BCMDONGLEHOST
			if (FW_SUPPORTED(dhd, ifst)) {
				err = wl_cfg80211_ifstats_counters(dev, if_stats);
			} else
#endif /* BCMDONGLEHOST */
			{
				err = wldev_iovar_getbuf(dev, "if_counters", NULL, 0,
						(char *)if_stats, WLC_IOCTL_MEDLEN, NULL);
			}

			if (err) {
//				WL_ERR(("if_counters not supported ret=%d\n", err));
				bzero(&pktcnt, sizeof(pktcnt));
				err = wldev_ioctl_get(dev, WLC_GET_PKTCNTS, &pktcnt,
						sizeof(pktcnt));
				if (!err) {
					sinfo->rx_packets = pktcnt.rx_good_pkt;
					sinfo->rx_dropped_misc = pktcnt.rx_bad_pkt;
					sinfo->tx_packets = pktcnt.tx_good_pkt;
					sinfo->tx_failed  = pktcnt.tx_bad_pkt;
				}
			} else {
				sinfo->rx_packets = (uint32)dtoh64(if_stats->rxframe);
				/* In this case, if_stats->rxerror is invalid.
				 * So, force to assign '0'.
				 */
				sinfo->rx_dropped_misc = 0;
				sinfo->tx_packets = (uint32)dtoh64(if_stats->txfrmsnt);
				sinfo->tx_failed = (uint32)dtoh64(if_stats->txnobuf) +
					(uint32)dtoh64(if_stats->txrunt) +
					(uint32)dtoh64(if_stats->txfail);
				sinfo->rx_bytes = dtoh64(if_stats->rxbyte);
				sinfo->tx_bytes = dtoh64(if_stats->txbyte);
				sinfo->tx_retries = (uint32)dtoh64(if_stats->txretry);
				sinfo->filled |= (STA_INFO_BIT(INFO_RX_BYTES) |
					STA_INFO_BIT(INFO_TX_BYTES) |
					STA_INFO_BIT(INFO_TX_RETRIES));
			}

			sinfo->filled |= (STA_INFO_BIT(INFO_RX_PACKETS) |
					STA_INFO_BIT(INFO_RX_DROP_MISC) |
					STA_INFO_BIT(INFO_TX_PACKETS) |
					STA_INFO_BIT(INFO_TX_FAILED));
get_station_err:
#if 0
			if (err && (err != -ENODATA)) {
				/* Disconnect due to zero BSSID or error to get RSSI */
				scb_val_t scbval;
#ifdef BCMDONGLEHOST
				DHD_STATLOG_CTRL(dhd, ST(DISASSOC_INT_START),
					dhd_net2idx(dhd->info, dev), DOT11_RC_DISASSOC_LEAVING);
#endif /* BCMDONGLEHOST */
				scbval.val = htod32(DOT11_RC_DISASSOC_LEAVING);
				err = wldev_ioctl_set(dev, WLC_DISASSOC, &scbval,
						sizeof(scb_val_t));
				if (unlikely(err)) {
					WL_ERR(("disassoc error (%d)\n", err));
				}

				WL_ERR(("force cfg80211_disconnected: %d\n", err));
				wl_clr_drv_status(cfg, CONNECTED, dev);
#ifdef BCMDONGLEHOST
				DHD_STATLOG_CTRL(dhd, ST(DISASSOC_DONE),
					dhd_net2idx(dhd->info, dev), DOT11_RC_DISASSOC_LEAVING);
#endif /* BCMDONGLEHOST */
				CFG80211_DISCONNECTED(dev, 0, NULL, 0, false, GFP_KERNEL);
				wl_link_down(cfg);
			}
#endif
			break;
		case WL_IF_TYPE_AP:
			sta = (sta_info_v4_t *)buf;
			bzero(sta, WLC_IOCTL_MEDLEN);
			err = wldev_iovar_getbuf(dev, "sta_info", (const   void*)mac,
					ETHER_ADDR_LEN, buf, WLC_IOCTL_MEDLEN, NULL);
			if (err < 0) {
				WL_ERR(("GET STA INFO failed, %d\n", err));
				goto error;
			}
			if (sta->ver != WL_STA_VER_4 && sta->ver != WL_STA_VER_5) {
				WL_ERR(("GET STA INFO version mismatch, %d\n", err));
				return BCME_VERSION;
			}
			sta->len = dtoh16(sta->len);
			sta->cap = dtoh16(sta->cap);
			sta->flags = dtoh32(sta->flags);
			sta->idle = dtoh32(sta->idle);
			sta->in = dtoh32(sta->in);
			sinfo->filled |= STA_INFO_BIT(INFO_INACTIVE_TIME);
			sinfo->inactive_time = sta->idle * 1000;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)) || \
			defined(WL_COMPAT_WIRELESS)
			if (sta->flags & WL_STA_ASSOC) {
				sinfo->filled |= STA_INFO_BIT(INFO_CONNECTED_TIME);
				sinfo->connected_time = sta->in;
			}
#endif
			WL_INFORM_MEM(("STA %s, flags 0x%x, idle time %ds, connected time %ds\n",
				bcm_ether_ntoa((const struct ether_addr *)mac, eabuf),
				sta->flags, sta->idle, sta->in));
			break;
		default :
			WL_ERR(("Invalid device mode %d\n", wl_get_mode_by_netdev(cfg, dev)));
	}
error:
	if (buf) {
		MFREE(cfg->osh, buf, WLC_IOCTL_MEDLEN);
	}

	return err;
}

static int
wl_cfg80211_dump_station(struct wiphy *wiphy, struct net_device *ndev,
	int idx, u8 *mac, struct station_info *sinfo)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct maclist *assoc_maclist = (struct maclist *)&(cfg->assoclist);
	int err;

	WL_DBG(("%s: enter, idx=%d\n", __FUNCTION__, idx));

	if (idx == 0) {
		assoc_maclist->count = MAX_NUM_OF_ASSOCIATED_DEV;
		err = wldev_ioctl_get(ndev, WLC_GET_ASSOCLIST,
			assoc_maclist, sizeof(cfg->assoclist));
		if (err < 0) {
			WL_ERR(("WLC_GET_ASSOCLIST error %d\n", err));
			cfg->assoclist.count = 0;
			return -EOPNOTSUPP;
		}
	}

	if (idx < le32_to_cpu(cfg->assoclist.count)) {
		(void)memcpy_s(mac, ETH_ALEN, cfg->assoclist.mac[idx], ETH_ALEN);
		return wl_cfg80211_get_station(wiphy, ndev, mac, sinfo);
	}

	return -ENOENT;
}

static s32
wl_cfg80211_set_power_mgmt(struct wiphy *wiphy, struct net_device *dev,
	bool enabled, s32 timeout)
{
	s32 pm;
	s32 err = 0;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct net_info *_net_info = wl_get_netinfo_by_netdev(cfg, dev);
	s32 mode;
#ifdef RTT_SUPPORT
	rtt_status_info_t *rtt_status;
#endif /* RTT_SUPPORT */
	dhd_pub_t *dhd = cfg->pub;
	RETURN_EIO_IF_NOT_UP(cfg);

	WL_DBG(("Enter\n"));
	mode = wl_get_mode_by_netdev(cfg, dev);
	if (cfg->p2p_net == dev || _net_info == NULL ||
			!wl_get_drv_status(cfg, CONNECTED, dev) ||
			((mode != WL_MODE_BSS) &&
			(mode != WL_MODE_IBSS))) {
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

/* force update cfg80211 to keep power save mode in sync. BUT this is NOT
 * a good solution since there is no protection while changing wdev->os. Best
 * way of changing power saving mode is doing it through
 * NL80211_CMD_SET_POWER_SAVE
 */
void wl_cfg80211_update_power_mode(struct net_device *dev)
{
	int err, pm = -1;

	err = wldev_ioctl_get(dev, WLC_GET_PM, &pm, sizeof(pm));
	if (err)
		WL_ERR(("error (%d)\n", err));
	else if (pm != -1 && dev->ieee80211_ptr)
		dev->ieee80211_ptr->ps = (pm == PM_OFF) ? false : true;
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

#ifndef OEM_ANDROID
/*
 * API invoked from driver .resume context
 */
s32
wl_cfg80211_resume(struct bcm_cfg80211 *cfg)
{
	struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);
	s32 err = BCME_OK;
	int pkt_filter_id = WL_WOWLAN_PKT_FILTER_ID_FIRST;

	if (unlikely(!wl_get_drv_status(cfg, READY, ndev))) {
		WL_INFORM_MEM(("device is not ready\n"));
		return err;
	}

	while (pkt_filter_id <= WL_WOWLAN_PKT_FILTER_ID_LAST) {
		/* delete wowlan pkt filter if any */
		err = wldev_iovar_setbuf(ndev, "pkt_filter_delete", &pkt_filter_id,
			sizeof(pkt_filter_id), cfg->ioctl_buf, WLC_IOCTL_SMLEN,
			&cfg->ioctl_buf_sync);
		/* pkt_filter_delete would return BCME_BADARG when pkt filter id
		 * does not exist in filter list of firmware, ignore it.
		 */
		if (BCME_BADARG == err)
			err = BCME_OK;

		if (BCME_OK != err) {
			WL_ERR(("pkt_filter_delete failed, id=%d, err=%d\n",
				pkt_filter_id, err));
		}
		pkt_filter_id++;
	}

	return err;
}
#endif /* !OEM_ANDROID */

#if !defined(OEM_ANDROID)
#ifdef CONFIG_PM
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0))
static s32 wl_wowlan_config(struct wiphy *wiphy, struct cfg80211_wowlan *wow)
{
	s32 err = BCME_OK;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);

	u32 i = 0, j = 0;
	u32 buf_len = 0, pattern_size = 0;
	wl_pkt_filter_t	*pkt_filterp = NULL;
	wl_pkt_filter_enable_t	pkt_filter_enable;
	u8 mask_bytes_len = 0, mask_byte_idx = 0, mask_bit_idx = 0;
	const u32 max_buf_size = WL_PKT_FILTER_FIXED_LEN +
		WL_PKT_FILTER_PATTERN_FIXED_LEN + (2 * WL_WOWLAN_MAX_PATTERN_LEN);

	WL_DBG(("Enter\n"));

	if (wow == NULL) {
		WL_DBG(("wow config is null\n"));
		return err;
	}

	/* configure wowlan pattern filters */
	if (0 < wow->n_patterns) {
		pkt_filterp = (wl_pkt_filter_t *)MALLOCZ(cfg->osh, max_buf_size);
		if (pkt_filterp == NULL) {
			WL_ERR(("Error allocating buffer for pkt filters\n"));
			return -ENOMEM;
		}

		WL_DBG(("Pattern count=%d\n", wow->n_patterns));
		while (i < wow->n_patterns) {

			/* reset buffers */
			buf_len = 0;
			bzero(pkt_filterp, max_buf_size);

			/* copy filter id */
			store32_ua(&pkt_filterp->id, (WL_WOWLAN_PKT_FILTER_ID_FIRST + i));

			/* copy filter type */
			store32_ua(&pkt_filterp->type, WL_PKT_FILTER_TYPE_PATTERN_MATCH);

			/* copy size */
			pattern_size = htod32(wow->patterns[i].pattern_len);
			store32_ua(&pkt_filterp->u.pattern.size_bytes, pattern_size);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0))
			/* copy offset */
			store32_ua(&pkt_filterp->u.pattern.offset, wow->patterns[i].pkt_offset);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0) */

			/* convert mask from bit to byte format */
			j = 0;
			mask_bit_idx = 0;
			mask_byte_idx = 0;
			mask_bytes_len = DIV_ROUND_UP(pattern_size, 8);
			while ((mask_byte_idx < mask_bytes_len) &&
					(mask_bit_idx < pattern_size)) {

				if (isbitset(wow->patterns[i].mask[mask_byte_idx], mask_bit_idx++))
					pkt_filterp->u.pattern.mask_and_pattern[j] = 0xFF;
				j++;
				if (mask_bit_idx >= 8) {
					/* move to next mask byte */
					mask_bit_idx = 0;
					mask_byte_idx++;
				}
			}

			/* copy pattern to be matched */
			memcpy(&pkt_filterp->u.pattern.mask_and_pattern[pattern_size],
				wow->patterns[i].pattern, pattern_size);

			/* calculate filter buffer len */
			buf_len += WL_PKT_FILTER_FIXED_LEN;
			buf_len += (WL_PKT_FILTER_PATTERN_FIXED_LEN + (2 * pattern_size));

			/* add pkt filter */
			err = wldev_iovar_setbuf(ndev, "pkt_filter_add", pkt_filterp, buf_len,
				cfg->ioctl_buf, WLC_IOCTL_MEDLEN, &cfg->ioctl_buf_sync);
			if (BCME_OK != err) {
				WL_ERR(("pkt_filter_add failed, id=%d, err=%d\n",
					pkt_filterp->id, err));
				goto exit;
			}

			/* enable pkt filter id */
			pkt_filter_enable.id = pkt_filterp->id;
			pkt_filter_enable.enable = TRUE;
			err = wldev_iovar_setbuf(ndev, "pkt_filter_enable", &pkt_filter_enable,
				sizeof(pkt_filter_enable),
				cfg->ioctl_buf, WLC_IOCTL_SMLEN, &cfg->ioctl_buf_sync);
			if (BCME_OK != err) {
				WL_ERR(("pkt_filter_enable failed, id=%d, err=%d\n",
					pkt_filterp->id, err));
				goto exit;
			}
			i++; /* move to next pattern */
		}
	} else
		WL_DBG(("wowlan filters not found\n"));

exit:
	if (pkt_filterp) {
		MFREE(cfg->osh, pkt_filterp, max_buf_size);
	}

	return err;
}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0) */
#endif /* CONFIG_PM */
#endif /* !OEM_ANDROID */

#ifndef OEM_ANDROID
/*
 * API invoked from driver .suspend context
 */
s32
wl_cfg80211_suspend(struct bcm_cfg80211 *cfg)
{
	s32 err = BCME_OK;
#ifdef CONFIG_PM
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0))
	struct wiphy *wiphy = bcmcfg_to_wiphy(cfg);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0) */
#endif /* CONFIG_PM */
#ifdef DHD_CLEAR_ON_SUSPEND
	struct net_info *iter, *next;
	struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);
	unsigned long flags;

	if (unlikely(!wl_get_drv_status(cfg, READY, ndev))) {
		WL_INFORM_MEM(("device is not ready : status (%d)\n",
			(int)cfg->status));
		return err;
	}
	for_each_ndev(cfg, iter, next) {
		/* p2p discovery iface doesn't have a ndev associated with it (for kernel > 3.8) */
		if (iter->ndev)
			wl_set_drv_status(cfg, SCAN_ABORTING, iter->ndev);
		}
	WL_CFG_DRV_LOCK(&cfg->cfgdrv_lock, flags);
	if (cfg->scan_request) {
		cfg80211_scan_done(cfg->scan_request, true);
		cfg->scan_request = NULL;
	}
	for_each_ndev(cfg, iter, next) {
		if (iter->ndev) {
			wl_clr_drv_status(cfg, SCANNING, iter->ndev);
			wl_clr_drv_status(cfg, SCAN_ABORTING, iter->ndev);
		}
	}
	WL_CFG_DRV_UNLOCK(&cfg->cfgdrv_lock, flags);
	for_each_ndev(cfg, iter, next) {
		if (iter->ndev) {
			if (wl_get_drv_status(cfg, CONNECTING, iter->ndev)) {
				wl_bss_connect_done(cfg, iter->ndev, NULL, NULL, false);
			}
		}
	}
#endif /* DHD_CLEAR_ON_SUSPEND */
#ifdef CONFIG_PM
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0))
	err = wl_wowlan_config(wiphy, wiphy->wowlan_config);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0) */
#endif /* CONFIG_PM */

	return err;
}
#endif /* !OEM_ANDROID */

static s32
wl_update_pmklist(struct net_device *dev, struct wl_pmk_list *pmk_list,
	s32 err)
{
	int i, j;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	struct net_device *primary_dev = bcmcfg_to_prmry_ndev(cfg);
	int npmkids = cfg->pmk_list->pmkids.count;

	ASSERT(cfg->pmk_list->pmkids.length >= (sizeof(u16)*2));
	if (!pmk_list) {
		WL_ERR(("pmk_list is NULL\n"));
		return -EINVAL;
	}
	/* pmk list is supported only for STA interface i.e. primary interface
	 * Refer code wlc_bsscfg.c->wlc_bsscfg_sta_init
	 */
	if (primary_dev != dev) {
		WL_INFORM_MEM(("Not supporting Flushing pmklist on virtual"
			" interfaces than primary interface\n"));
		return err;
	}

	WL_DBG(("No of elements %d\n", npmkids));
	for (i = 0; i < npmkids; i++) {
		WL_DBG(("PMKID[%d]: %pM =\n", i,
			&pmk_list->pmkids.pmkid[i].bssid));
		for (j = 0; j < WPA2_PMKID_LEN; j++) {
			WL_DBG(("%02x\n", pmk_list->pmkids.pmkid[i].pmkid[j]));
		}
	}
	if (cfg->wlc_ver.wlc_ver_major >= MIN_PMKID_LIST_V3_FW_MAJOR) {
			pmk_list->pmkids.version = PMKID_LIST_VER_3;
			err = wldev_iovar_setbuf(dev, "pmkid_info", (char *)pmk_list,
				sizeof(*pmk_list), cfg->ioctl_buf,
				WLC_IOCTL_MAXLEN, &cfg->ioctl_buf_sync);
	}
	else if (cfg->wlc_ver.wlc_ver_major == MIN_PMKID_LIST_V2_FW_MAJOR) {
		u32 v2_list_size = (u32)(sizeof(pmkid_list_v2_t) + npmkids*sizeof(pmkid_v2_t));
		pmkid_list_v2_t *pmkid_v2_list = (pmkid_list_v2_t *)MALLOCZ(cfg->osh, v2_list_size);
		pmkid_list_v3_t *spmk_list = &cfg->spmk_info_list->pmkids;

		if (pmkid_v2_list == NULL) {
			WL_ERR(("failed to allocate pmkid list\n"));
			return BCME_NOMEM;
		}

		pmkid_v2_list->version = PMKID_LIST_VER_2;
		/* Account for version, length and pmkid_v2_t fields */
		pmkid_v2_list->length = (npmkids * sizeof(pmkid_v2_t)) + (2 * sizeof(u16));

		for (i = 0; i < npmkids; i++) {
			/* memcpy_s return checks not needed as buffers are of same size */
			(void)memcpy_s(&pmkid_v2_list->pmkid[i].BSSID,
					ETHER_ADDR_LEN, &pmk_list->pmkids.pmkid[i].bssid,
					ETHER_ADDR_LEN);

			/* copy pmkid if available */
			if (pmk_list->pmkids.pmkid[i].pmkid_len) {
				(void)memcpy_s(pmkid_v2_list->pmkid[i].PMKID,
						WPA2_PMKID_LEN,
						pmk_list->pmkids.pmkid[i].pmkid,
						pmk_list->pmkids.pmkid[i].pmkid_len);
			}

			if (pmk_list->pmkids.pmkid[i].pmk_len) {
				(void)memcpy_s(pmkid_v2_list->pmkid[i].pmk,
						pmk_list->pmkids.pmkid[i].pmk_len,
						pmk_list->pmkids.pmkid[i].pmk,
						pmk_list->pmkids.pmkid[i].pmk_len);
				pmkid_v2_list->pmkid[i].pmk_len = pmk_list->pmkids.pmkid[i].pmk_len;
			}

			if (pmk_list->pmkids.pmkid[i].ssid_len) {
				(void)memcpy_s(pmkid_v2_list->pmkid[i].ssid.ssid,
						pmk_list->pmkids.pmkid[i].ssid_len,
						pmk_list->pmkids.pmkid[i].ssid,
						pmk_list->pmkids.pmkid[i].ssid_len);
				pmkid_v2_list->pmkid[i].ssid.ssid_len
					= pmk_list->pmkids.pmkid[i].ssid_len;
			}

			(void)memcpy_s(pmkid_v2_list->pmkid[i].fils_cache_id,
					FILS_CACHE_ID_LEN, &pmk_list->pmkids.pmkid[i].fils_cache_id,
					FILS_CACHE_ID_LEN);
			for (j = 0; j < spmk_list->count; j++) {
				if (memcmp(&pmkid_v2_list->pmkid[i].BSSID,
					&spmk_list->pmkid[j].bssid, ETHER_ADDR_LEN)) {
					continue; /* different MAC */
				}
				WL_DBG(("SPMK replace idx:%d bssid: "MACF " to SSID: %d\n", i,
					ETHER_TO_MACF(pmkid_v2_list->pmkid[i].BSSID),
					spmk_list->pmkid[j].ssid_len));
				bzero(&pmkid_v2_list->pmkid[i].BSSID, ETHER_ADDR_LEN);
				pmkid_v2_list->pmkid[i].ssid.ssid_len =
					spmk_list->pmkid[j].ssid_len;
				(void)memcpy_s(pmkid_v2_list->pmkid[i].ssid.ssid,
					spmk_list->pmkid[j].ssid_len,
					spmk_list->pmkid[j].ssid,
					spmk_list->pmkid[j].ssid_len);
			}
			pmkid_v2_list->pmkid[i].length = PMKID_ELEM_V2_LENGTH;
		}

		err = wldev_iovar_setbuf(dev, "pmkid_info", (char *)pmkid_v2_list,
				v2_list_size, cfg->ioctl_buf,
				WLC_IOCTL_MAXLEN, &cfg->ioctl_buf_sync);
		if (unlikely(err)) {
			WL_ERR(("pmkid_info failed (%d)\n", err));
		}

		MFREE(cfg->osh, pmkid_v2_list, v2_list_size);
	}
	else {
		u32 v1_list_size = (u32)(sizeof(pmkid_list_v1_t) + npmkids*sizeof(pmkid_v1_t));
		pmkid_list_v1_t *pmkid_v1_list = (pmkid_list_v1_t *)MALLOCZ(cfg->osh, v1_list_size);
		if (pmkid_v1_list == NULL) {
			WL_ERR(("failed to allocate pmkid list\n"));
			return BCME_NOMEM;
		}
		for (i = 0; i < npmkids; i++) {
			/* memcpy_s return checks not needed as buffers are of same size */
			(void)memcpy_s(&pmkid_v1_list->pmkid[i].BSSID,
					ETHER_ADDR_LEN, &pmk_list->pmkids.pmkid[i].bssid,
					ETHER_ADDR_LEN);
			(void)memcpy_s(pmkid_v1_list->pmkid[i].PMKID,
					WPA2_PMKID_LEN, pmk_list->pmkids.pmkid[i].pmkid,
					WPA2_PMKID_LEN);
			pmkid_v1_list->npmkid++;
		}
		err = wldev_iovar_setbuf(dev, "pmkid_info", (char *)pmkid_v1_list,
				v1_list_size, cfg->ioctl_buf,
				WLC_IOCTL_MAXLEN, &cfg->ioctl_buf_sync);
		if (unlikely(err)) {
			WL_ERR(("pmkid_info failed (%d)\n", err));
		}

		MFREE(cfg->osh, pmkid_v1_list, v1_list_size);
	}
	return err;
}

/* TODO: remove temporal cfg->pmk_list list, and call wl_cfg80211_update_pmksa for single
 * entry operation.
 */
static s32
wl_cfg80211_set_pmksa(struct wiphy *wiphy, struct net_device *dev,
	struct cfg80211_pmksa *pmksa)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	s32 err = 0;
	int i;
	int npmkids = cfg->pmk_list->pmkids.count;
#ifdef BCMDONGLEHOST
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
#endif /* BCMDONGLEHOST */

	if (cfg->wlc_ver.wlc_ver_major >= PMKDB_WLC_VER) {
		err = wl_cfg80211_update_pmksa(wiphy, dev, pmksa, TRUE);
		if (err != BCME_OK) {
			WL_ERR(("wl_cfg80211_set_pmksa err:%d\n", err));
		}
		return err;
	}

	RETURN_EIO_IF_NOT_UP(cfg);
#ifdef BCMDONGLEHOST
	BCM_REFERENCE(dhdp);
	DHD_STATLOG_CTRL(dhdp, ST(INSTALL_PMKSA), dhd_net2idx(dhdp->info, dev), 0);
#endif /* BCMDONGLEHOST */

	for (i = 0; i < npmkids; i++) {
		if (pmksa->bssid != NULL) {
			if (!memcmp(pmksa->bssid, &cfg->pmk_list->pmkids.pmkid[i].bssid,
				ETHER_ADDR_LEN))
				break;
		}
#ifdef WL_FILS
		else if (pmksa->ssid != NULL) {
			if (!memcmp(pmksa->ssid, &cfg->pmk_list->pmkids.pmkid[i].ssid,
				pmksa->ssid_len))
				break;
		}
#endif /* WL_FILS */
	}
	if (i < WL_NUM_PMKIDS_MAX) {
		if (pmksa->bssid != NULL) {
			memcpy(&cfg->pmk_list->pmkids.pmkid[i].bssid, pmksa->bssid,
				ETHER_ADDR_LEN);
		}
#ifdef WL_FILS
		else if (pmksa->ssid != NULL) {
			cfg->pmk_list->pmkids.pmkid[i].ssid_len = pmksa->ssid_len;
			memcpy(&cfg->pmk_list->pmkids.pmkid[i].ssid, pmksa->ssid,
				pmksa->ssid_len);
			memcpy(&cfg->pmk_list->pmkids.pmkid[i].fils_cache_id, pmksa->cache_id,
				FILS_CACHE_ID_LEN);
		}
#endif /* WL_FILS */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0) || \
	defined(WL_FILS))
		if (pmksa->pmk_len) {
			if (memcpy_s(&cfg->pmk_list->pmkids.pmkid[i].pmk, PMK_LEN_MAX, pmksa->pmk,
				pmksa->pmk_len)) {
				WL_ERR(("invalid pmk len = %zu", pmksa->pmk_len));
			} else {
				cfg->pmk_list->pmkids.pmkid[i].pmk_len = pmksa->pmk_len;
			}
		}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0) || defined(WL_FILS) */
		/* return check not required as buffer lengths are same */
		(void)memcpy_s(cfg->pmk_list->pmkids.pmkid[i].pmkid, WPA2_PMKID_LEN, pmksa->pmkid,
			WPA2_PMKID_LEN);
		cfg->pmk_list->pmkids.pmkid[i].pmkid_len = WPA2_PMKID_LEN;

		/* set lifetime not to expire in firmware by default.
		 * Currently, wpa_supplicant control PMKID lifetime on his end. e.g) set 12 hours
		 * when it expired, wpa_supplicant should call set_pmksa/del_pmksa to update
		 * corresponding entry.
		 */
		cfg->pmk_list->pmkids.pmkid[i].time_left = KEY_PERM_PMK;
		if (i == npmkids) {
			cfg->pmk_list->pmkids.length += sizeof(pmkid_v3_t);
			cfg->pmk_list->pmkids.count++;
		}
	} else {
		err = -EINVAL;
	}

#if (WL_DBG_LEVEL > 0)
	if (pmksa->bssid != NULL) {
		WL_DBG(("set_pmksa,IW_PMKSA_ADD - PMKID: %pM =\n",
			&cfg->pmk_list->pmkids.pmkid[npmkids - 1].bssid));
	}
	for (i = 0; i < WPA2_PMKID_LEN; i++) {
		WL_DBG(("%02x\n",
			cfg->pmk_list->pmkids.pmkid[npmkids - 1].
			pmkid[i]));
	}
#endif /* (WL_DBG_LEVEL > 0) */

	err = wl_update_pmklist(dev, cfg->pmk_list, err);

	return err;
}

/* sending pmkid_info IOVAR to manipulate PMKID(PMKSA) list in firmware.
 * input @pmksa: host given single pmksa info.
 * if it's NULL, assume whole list manipulated. e.g) flush all PMKIDs in firmware.
 * input @set: TRUE means adding PMKSA operation. FALSE means deleting.
 * return: log internal BCME_XXX error, and convert it to -EINVAL to linux generic error code.
 */
static s32 wl_cfg80211_update_pmksa(struct wiphy *wiphy, struct net_device *dev,
	struct cfg80211_pmksa *pmksa, bool set) {

	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	s32 err = 0;
	pmkid_list_v3_t *pmk_list;
	uint32 alloc_len;

	RETURN_EIO_IF_NOT_UP(cfg);

	if (cfg->wlc_ver.wlc_ver_major < MIN_PMKID_LIST_V3_FW_MAJOR) {
		WL_DBG(("wlc_ver_major not supported:%d\n", cfg->wlc_ver.wlc_ver_major));
		return BCME_VERSION;
	}

	alloc_len = (uint32)(OFFSETOF(pmkid_list_v3_t, pmkid) + ((pmksa) ? sizeof(pmkid_v3_t) : 0));
	pmk_list = (pmkid_list_v3_t *)MALLOCZ(cfg->osh, alloc_len);

	if (pmk_list == NULL) {
		return BCME_NOMEM;
	}

	pmk_list->version = PMKID_LIST_VER_3;
	pmk_list->length = alloc_len;
	pmk_list->count = (pmksa) ? 1 : 0; // 1 means single entry operation, 0 means whole list.
	pmk_list->flag = (set) ? PMKDB_SET_IOVAR : PMKDB_CLEAR_IOVAR;

	if (pmksa) {
		/* controll set/del action by lifetime parameter accordingly.
		 * if set == TRUE, it's set PMKID action with lifetime permanent.
		 * if set == FALSE, it's del PMKID action with lifetime zero.
		 */
		pmk_list->pmkid->time_left = (set) ? KEY_PERM_PMK : 0;
		if (pmksa->bssid) {
			eacopy(pmksa->bssid, &pmk_list->pmkid->bssid);
		}
		if (pmksa->pmkid) {
			err = memcpy_s(&pmk_list->pmkid->pmkid, sizeof(pmk_list->pmkid->pmkid),
				pmksa->pmkid, WPA2_PMKID_LEN);
			if (err) {
				goto exit;
			}
			pmk_list->pmkid->pmkid_len = WPA2_PMKID_LEN;
		}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0))
		if (pmksa->pmk) {
			err = memcpy_s(&pmk_list->pmkid->pmk, sizeof(pmk_list->pmkid->pmk),
				pmksa->pmk, pmksa->pmk_len);
			if (err) {
				goto exit;
			}
			pmk_list->pmkid->pmk_len = pmksa->pmk_len;
		}
		if (pmksa->ssid) {
			err = memcpy_s(&pmk_list->pmkid->ssid, sizeof(pmk_list->pmkid->ssid),
				pmksa->ssid, pmksa->ssid_len);
			if (err) {
				goto exit;
			}
			pmk_list->pmkid->ssid_len = pmksa->ssid_len;
		}
		if (pmksa->cache_id) {
			/* supplicant passes data received on air as is(network order).
			 * convert it before using.
			 */
			pmk_list->pmkid->fils_cache_id = ntoh16(*(const uint16 *)pmksa->cache_id);
		}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0) */
		wl_cfg80211_spmk_pmkdb_change_pmk_type(cfg, pmk_list);
	}

	if (wl_dbg_level & WL_DBG_DBG) {
		prhex("pmklist_data", (char *)pmk_list, alloc_len);
	}

	err = wldev_iovar_setbuf(dev, "pmkdb", (char *)pmk_list,
		alloc_len, cfg->ioctl_buf,
		WLC_IOCTL_MAXLEN, &cfg->ioctl_buf_sync);
	if (unlikely(err)) {
		WL_ERR(("pmkdb set failed. err:%d\n", err));
	}
exit:
	if (pmk_list) {
		MFREE(cfg->osh, pmk_list, alloc_len);
	}
	return err;
}

/* TODO: remove temporal cfg->pmk_list list, and call wl_cfg80211_update_pmksa for single
 * entry operation.
 */
static s32
wl_cfg80211_del_pmksa(struct wiphy *wiphy, struct net_device *dev,
	struct cfg80211_pmksa *pmksa)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	s32 err = 0;
	int i;
	int npmkids = cfg->pmk_list->pmkids.count;
	RETURN_EIO_IF_NOT_UP(cfg);

	if (!pmksa) {
		WL_ERR(("pmksa is not initialized\n"));
		return BCME_ERROR;
	}
	if (cfg->wlc_ver.wlc_ver_major >= PMKDB_WLC_VER) {
		err = wl_cfg80211_update_pmksa(wiphy, dev, pmksa, FALSE);
		if (err != BCME_OK) {
			WL_ERR(("wl_cfg80211_del_pmksa err:%d\n", err));
		}
		wl_cfg80211_spmk_pmkdb_del_spmk(cfg, pmksa);
		return err;
	}

	if (!npmkids) {
		/* nmpkids = 0, nothing to delete */
		WL_DBG(("npmkids=0. Skip del\n"));
		return BCME_OK;
	}

#if (WL_DBG_LEVEL > 0)
	if (pmksa->bssid) {
		WL_DBG(("del_pmksa,IW_PMKSA_REMOVE - PMKID: %pM =\n",
			pmksa->bssid));
	}
#ifdef WL_FILS
	else if (pmksa->ssid) {
		WL_DBG(("FILS: del_pmksa for ssid: "));
		for (i = 0; i < pmksa->ssid_len; i++) {
			WL_DBG(("%c", pmksa->ssid[i]));
		}
		WL_DBG(("\n"));
	}
#endif /* WL_FILS */
	if (pmksa->pmkid) {
		for (i = 0; i < WPA2_PMKID_LEN; i++) {
			WL_DBG(("%02x\n", pmksa->pmkid[i]));
		}
	}
#endif /* (WL_DBG_LEVEL > 0) */

	for (i = 0; i < npmkids; i++) {
		if (pmksa->bssid) {
			if (!memcmp
			    (pmksa->bssid, &cfg->pmk_list->pmkids.pmkid[i].bssid,
			     ETHER_ADDR_LEN)) {
					break;
			}
		}
#ifdef WL_FILS
		else if (pmksa->ssid) {
			if (!memcmp
			    (pmksa->ssid, &cfg->pmk_list->pmkids.pmkid[i].ssid,
			     pmksa->ssid_len)) {
					break;
			}
		}
#endif /* WL_FILS */
	}
	if ((npmkids > 0) && (i < npmkids)) {
		bzero(&cfg->pmk_list->pmkids.pmkid[i], sizeof(pmkid_v3_t));
		for (; i < (npmkids - 1); i++) {
			(void)memcpy_s(&cfg->pmk_list->pmkids.pmkid[i],
				sizeof(pmkid_v3_t),
				&cfg->pmk_list->pmkids.pmkid[i + 1],
				sizeof(pmkid_v3_t));
		}
		npmkids--;
		cfg->pmk_list->pmkids.length -= sizeof(pmkid_v3_t);
		cfg->pmk_list->pmkids.count--;

	} else {
		err = -EINVAL;
	}

	/* current wl_update_pmklist() doesn't delete corresponding PMKID entry.
	 * inside firmware. So we need to issue delete action explicitely through
	 * this function.
	 */
	err = wl_cfg80211_update_pmksa(wiphy, dev, pmksa, FALSE);
	/* intentional fall through even on error.
	 * it should work above MIN_PMKID_LIST_V3_FW_MAJOR, otherwise let ignore it.
	 */

	err = wl_update_pmklist(dev, cfg->pmk_list, err);

	return err;

}

/* TODO: remove temporal cfg->pmk_list list, and call wl_cfg80211_update_pmksa for single
 * entry operation.
 */
static s32
wl_cfg80211_flush_pmksa(struct wiphy *wiphy, struct net_device *dev)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	s32 err = 0;
	RETURN_EIO_IF_NOT_UP(cfg);
	if (cfg->wlc_ver.wlc_ver_major >= PMKDB_WLC_VER) {
		/* NULL pmksa means delete whole PMKSA list */
		err = wl_cfg80211_update_pmksa(wiphy, dev, NULL, FALSE);
		if (err != BCME_OK) {
			WL_ERR(("wl_cfg80211_flush_pmksa err:%d\n", err));
		}
		return err;
	}
	bzero(cfg->pmk_list, sizeof(*cfg->pmk_list));
	cfg->pmk_list->pmkids.length = OFFSETOF(pmkid_list_v3_t, pmkid);
	cfg->pmk_list->pmkids.count = 0;
	cfg->pmk_list->pmkids.version = PMKID_LIST_VER_3;
	err = wl_update_pmklist(dev, cfg->pmk_list, err);
	return err;
}

static void
wl_cfg80211_afx_handler(struct work_struct *work)
{
	struct afx_hdl *afx_instance;
	struct bcm_cfg80211 *cfg;
	s32 ret = BCME_OK;

	BCM_SET_CONTAINER_OF(afx_instance, work, struct afx_hdl, work);
	if (afx_instance) {
		cfg = wl_get_cfg(afx_instance->dev);
		if (cfg != NULL && cfg->afx_hdl->is_active) {
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
#ifdef WL_CFG80211_GON_COLLISION
	/* drop tx go nego request if go nego collision occurs */
	bool drop_tx_req;
#endif
#ifdef WL_CFG80211_SYNC_GON
	/* WAR: dongle does not keep the dwell time of 'actframe' sometime.
	 * if extra_listen is set, keep the dwell time to get af response frame
	 */
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
#ifdef WL_CFG80211_GON_COLLISION
	config_af_params->drop_tx_req = false;
#endif
#ifdef WL_CFG80211_SYNC_GON
	config_af_params->extra_listen = true;
#endif
	config_af_params->search_channel = false;
	config_af_params->max_tx_retry = WL_AF_TX_MAX_RETRY;
	cfg->next_af_subtype = WL_PUB_AF_STYPE_INVALID;

	switch (act_frm->subtype) {
	case P2P_PAF_GON_REQ: {
		/* Disable he if peer does not support before starting GONEG */
		WL_DBG(("P2P: GO_NEG_PHASE status set \n"));
		wl_set_p2p_status(cfg, GO_NEG_PHASE);

		config_af_params->search_channel = true;
		cfg->next_af_subtype = act_frm->subtype + 1;

		/* increase dwell time to wait for RESP frame */
		af_params->dwell_time = WL_MED_DWELL_TIME;

#ifdef WL_CFG80211_GON_COLLISION
		config_af_params->drop_tx_req = true;
#endif /* WL_CFG80211_GON_COLLISION */
		break;
	}
	case P2P_PAF_GON_RSP: {
		cfg->next_af_subtype = act_frm->subtype + 1;
		/* increase dwell time to wait for CONF frame */
		/* WAR : 100ms is added because kernel spent more time in some case.
		 *              Kernel should be fixed.
		 */
		af_params->dwell_time = WL_MED_DWELL_TIME + 100;
		break;
	}
	case P2P_PAF_GON_CONF: {
		/* If we reached till GO Neg confirmation reset the filter */
		WL_DBG(("P2P: GO_NEG_PHASE status cleared \n"));
		wl_clr_p2p_status(cfg, GO_NEG_PHASE);

		/* minimize dwell time */
		af_params->dwell_time = WL_MIN_DWELL_TIME;

#ifdef WL_CFG80211_GON_COLLISION
		/* if go nego formation done, clear it */
		cfg->block_gon_req_tx_count = 0;
		cfg->block_gon_req_rx_count = 0;
#endif /* WL_CFG80211_GON_COLLISION */
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
		/* wpa_supplicant send go nego req right after prov disc */
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

#if defined(WL11U) && defined(WL_HOST_AF_DFS_CHECK)
static bool
wl_cfg80211_check_DFS_channel(struct bcm_cfg80211 *cfg, wl_af_params_t *af_params,
	void *frame, u16 frame_len)
{
	wl_scan_results *bss_list;
	wl_bss_info_t *bi = NULL;
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
#endif /* WL11U && WL_HOST_AF_DFS_CHECK */
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
#ifdef BCMDONGLEHOST
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);
#endif /* BCMDONGLEHOST */

	int32 requested_dwell = af_params->dwell_time;

	/* Add the default dwell time
	 * Dwell time to stay off-channel to wait for a response action frame
	 * after transmitting an GO Negotiation action frame
	 */
	af_params->dwell_time = WL_DEFAULT_DWELL_TIME;

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
	cfg->next_af_subtype = WL_PUB_AF_STYPE_INVALID;
	config_af_params.max_tx_retry = WL_AF_TX_MAX_RETRY;
	config_af_params.search_channel = false;
#ifdef WL_CFG80211_GON_COLLISION
	config_af_params.drop_tx_req = false;
#endif
#ifdef WL_CFG80211_SYNC_GON
	config_af_params.extra_listen = false;
#endif

	/* config parameters */
	/* Public Action Frame Process - DOT11_ACTION_CAT_PUBLIC */
	if (category == DOT11_ACTION_CAT_PUBLIC) {
		if (wl_cfg80211_is_dpp_frame((void *)action_frame->data, action_frame->len)) {
			wl_dpp_pa_frame_t *pa = (wl_dpp_pa_frame_t *)action_frame->data;
			config_af_params.max_tx_retry = WL_AF_TX_MAX_RETRY;
			af_params->dwell_time = WL_MED_DWELL_TIME;
			cfg->need_wait_afrx = true;
			/* once matching frame is found in rx, abort dwell (upper layer
			 * doesn't do that).
			 */
			if (pa->ftype == DPP_AUTH_REQ) {
				cfg->next_af_subtype = DPP_AUTH_RESP;
			} else if (pa->ftype == DPP_AUTH_RESP) {
				cfg->next_af_subtype = DPP_AUTH_CONF;
			} else {
				cfg->next_af_subtype = WL_PUB_AF_STYPE_INVALID;
				cfg->need_wait_afrx = false;
			}
		} else if (wl_cfg80211_is_dpp_gas_action(
				(void *)action_frame->data, action_frame->len)) {
			config_af_params.max_tx_retry = WL_AF_TX_MAX_RETRY;
			af_params->dwell_time = WL_MED_DWELL_TIME;
			cfg->need_wait_afrx = true;
			config_af_params.search_channel = false;

			if (requested_dwell == 0) {
				/* Use minimal dwell to take care of Ack */
				af_params->dwell_time = WL_MIN_DWELL_TIME;
			}
		} else if ((action == P2P_PUB_AF_ACTION) &&
			(action_frame_len >= sizeof(wifi_p2p_pub_act_frame_t))) {
			/* p2p public action frame process */
			if (BCME_OK != wl_cfg80211_config_p2p_pub_af_tx(wiphy,
				action_frame, af_params, &config_af_params)) {
				/* just send unknown subtype frame with default parameters. */
				WL_DBG(("Unknown subtype.\n"));
			}

#ifdef WL_CFG80211_GON_COLLISION
			if (config_af_params.drop_tx_req) {
				if (cfg->block_gon_req_tx_count) {
					/* drop gon req tx action frame */
					WL_DBG(("Drop gon req tx action frame: count %d\n",
						cfg->block_gon_req_tx_count));
					goto exit;
				}
			}
#endif /* WL_CFG80211_GON_COLLISION */
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
#ifdef BCMDONGLEHOST
		if (dhd->op_mode & DHD_FLAG_HOSTAP_MODE) {
			wl_clr_drv_status(cfg, SENDING_ACT_FRM, dev);
			return false;
		}
#endif /* BCMDONGLEHOST */
	}

	netinfo = wl_get_netinfo_by_wdev(cfg, cfgdev_to_wdev(cfgdev));
	/* validate channel and p2p ies */
	if (config_af_params.search_channel &&
		IS_P2P_SOCIAL(CHSPEC_CHANNEL(af_params->channel)) &&
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
		wl_cfgscan_cancel_scan(cfg);
	}

	/* Abort P2P listen */
	if (discover_cfgdev(cfgdev, cfg)) {
		if (cfg->p2p_supported && cfg->p2p) {
			wl_cfgp2p_set_p2p_mode(cfg, WL_P2P_DISC_ST_SCAN, 0, 0,
				wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE));
		}
	}

#if defined(WL11U) && defined(WL_HOST_AF_DFS_CHECK)
	/* handling DFS channel exceptions */
	if (!wl_cfg80211_check_DFS_channel(cfg, af_params, action_frame->data, action_frame->len)) {
		return false;	/* the action frame was blocked */
	}
#endif /* WL11U && WL_HOST_AF_DFS_CHECK */

	/* set status and destination address before sending af */
	if (cfg->next_af_subtype != WL_PUB_AF_STYPE_INVALID) {
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
		wl_cfgscan_cancel_scan(cfg);
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
			WL_ERR(("Attempt tx on peer listen channel:%d\n",
				cfg->afx_hdl->peer_chan));
		} else {
			WL_ERR(("Attempt tx with the channel provided by userspace."
			"Channel: %d\n", CHSPEC_CHANNEL(af_params->channel)));
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

	/* if failed, retry it. tx_retry_max value is configure by .... */
	while ((ack == false) && (tx_retry++ < config_af_params.max_tx_retry) &&
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
#ifdef WL_CFG80211_GON_COLLISION
		!cfg->block_gon_req_tx_count &&
#endif /* WL_CFG80211_GON_COLLISION */
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

	if (ack) {
		WL_DBG(("-- Action Frame Tx succeeded, listen chan: %d\n",
			cfg->afx_hdl->my_listen_chan));
	} else {
		WL_ERR(("-- Action Frame Tx failed, listen chan: %d\n",
			cfg->afx_hdl->my_listen_chan));
	}

#ifdef WL_CFG80211_GON_COLLISION
	if (cfg->block_gon_req_tx_count) {
		cfg->block_gon_req_tx_count--;
		/* if ack is ture, supplicant will wait more time(100ms).
		 * so we will return it as a success to get more time .
		 */
		ack = true;
	}
#endif /* WL_CFG80211_GON_COLLISION */
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
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)) || \
	defined(WL_COMPAT_WIRELESS)
	bool no_cck,
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0)) || \
	defined(WL_COMPAT_WIRELESS)
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
			WL_ERR(("Failed to find bssidx\n"));
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
			wl_cfg80211_set_mgmt_vndr_ies(cfg, cfgdev, bssidx,
				VNDR_IE_PRBRSP_FLAG, (const u8 *)(buf + ie_offset), ie_len);
			cfg80211_mgmt_tx_status(cfgdev, *cookie, buf, len, true, GFP_KERNEL);
#if defined(P2P_IE_MISSING_FIX)
			if (!cfg->p2p_prb_noti) {
				cfg->p2p_prb_noti = true;
				WL_DBG(("wl_cfg80211_mgmt_tx: TX 802_1X Probe"
					" Response first time.\n"));
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
			WL_ERR(("Disconnect STA : " MACDBG " scb_val.val %d\n",
				MAC2STRDBG(bcm_ether_ntoa((const struct ether_addr *)mgmt->da,
				eabuf)), scb_val.val));

			/* WAR Wait for the deauth event to come,
			 * supplicant will do the delete iface immediately
			 * and we will have problem in sending
			 * deauth frame if we delete the bss in firmware.
			 * But we do not need additional delays for this WAR
			 * during P2P connection.
			 *
			 * Supplicant call this function with BCAST after
			 * delete all GC stations with each addr.
			 * So, 400 ms delay can be called only once when GO disconnect all GC
			*/
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
			wl_cfgscan_cancel_scan(cfg);
#endif /* not WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST */
		}
#ifdef WL_CLIENT_SAE
		else if (ieee80211_is_auth(mgmt->frame_control)) {
			err = wl_cfg80211_mgmt_auth_tx(dev, cfgdev, cfg, buf, len,
				bssidx, cookie);
			goto exit;
		}
#endif /* WL_CLIENT_SAE */

	} else {
		WL_ERR(("Driver only allows MGMT packet type\n"));
		goto exit;
	}

	af_params = (wl_af_params_t *)MALLOCZ(cfg->osh, WL_WIFI_AF_PARAMS_SIZE);

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

	if (channel) {
		/* Add the channel */
		af_params->channel =
			wl_freq_to_chanspec(channel->center_freq);
	} else {
		af_params->channel = 0;
	}

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
	WL_DBG(("txstatus notified for cookie:%llu. ack:%d\n", *cookie, ack));

	MFREE(cfg->osh, af_params, WL_WIFI_AF_PARAMS_SIZE);
exit:
	return err;
}

static void
wl_cfg80211_mgmt_frame_register(struct wiphy *wiphy, bcm_struct_cfgdev *cfgdev,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 8, 0))
	u16 frame, bool reg)
#else
	struct mgmt_frame_regs *upd)
#endif
{

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 8, 0))
	WL_DBG(("frame_type: %x, reg: %d\n", frame, reg));

	if (frame != (IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_PROBE_REQ))
		return;
#endif

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
#if defined(SUPPORT_HOSTAPD_BGN_MODE)
	s32 gmode = -1, nmode = -1;
	s32 gmode_prev = -1, nmode_prev = -1;
#endif /* SUPPORT_HOSTAPD_BGN_MODE */
#if defined(PCIE_FULL_DONGLE) || defined(SUPPORT_HOSTAPD_BGN_MODE)
	dhd_pub_t *dhd;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	dhd = (dhd_pub_t *)(cfg->pub);
#if defined(WL_ENABLE_P2P_IF)
	if (cfg->p2p_net == dev)
		dev = bcmcfg_to_prmry_ndev(cfg);
#endif
#endif /* PCIE_FULL_DONGLE || SUPPORT_HOSTAPD_BGN_MODE */

	if (params->use_cts_prot >= 0) {
	}

	if (params->use_short_preamble >= 0) {
	}

	if (params->use_short_slot_time >= 0) {
	}

	if (params->basic_rates) {
#if defined(SUPPORT_HOSTAPD_BGN_MODE)
		switch ((int)(params->basic_rates[params->basic_rates_len -1])) {
			case 22: /* B only , rate 11 */
				gmode = 0;
				nmode = 0;
				break;
			case 108: /* G only , rate 54 */
				gmode = 2;
				nmode = 0;
				break;
			default:
				gmode = -1;
				nmode = -1;
				break;
		}
#endif /* SUPPORT_HOSTAPD_BGN_MODE */
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
#if defined(SUPPORT_HOSTAPD_BGN_MODE)
		nmode = 1;
		gmode = 1;
	} else {
		nmode = 0;
#endif /* SUPPORT_HOSTAPD_BGN_MODE */
	}

#if defined(SUPPORT_HOSTAPD_BGN_MODE)
	err = wldev_iovar_getint(dev, "nmode", &nmode_prev);
	if (unlikely(err)) {
		WL_ERR(("error reading nmode (%d)\n", err));
	}
	if (nmode == nmode_prev) {
		nmode = -1;
	}
	err = wldev_ioctl_get(dev, WLC_GET_GMODE, &gmode_prev, sizeof(gmode_prev));
	if (unlikely(err)) {
		WL_ERR(("error reading gmode (%d)\n", err));
	}
	if (gmode == gmode_prev) {
		gmode = -1;
	}

	if (((dhd->op_mode & DHD_FLAG_HOSTAP_MODE) == DHD_FLAG_HOSTAP_MODE) &&
		((gmode > -1) || (nmode > -1))) {
		s32 val = 0;

		err = wldev_ioctl_set(dev, WLC_DOWN, &val, sizeof(s32));
		if (unlikely(err))
			WL_ERR(("WLC_DOWN command failed:[%d]\n", err));

		if (nmode > -1) {
			err = wldev_iovar_setint(dev, "nmode", nmode);
			if (unlikely(err))
				WL_ERR(("nmode command failed:mode[%d]:err[%d]\n", nmode, err));
		}

		if (gmode > -1) {
			err = wldev_ioctl_set(dev, WLC_SET_GMODE, &gmode, sizeof(s32));
			if (unlikely(err))
				WL_ERR(("WLC_SET_GMODE command failed:mode[%d]:err[%d]\n",
					gmode, err));
		}

		val = 0;
		err = wldev_ioctl_set(dev, WLC_UP, &val, sizeof(s32));
		if (unlikely(err))
			WL_ERR(("WLC_UP command failed:err[%d]\n", err));

	}
#endif /* SUPPORT_HOSTAPD_BGN_MODE */

	return err;
}

#ifdef WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST
struct net_device *
wl_cfg80211_get_remain_on_channel_ndev(struct bcm_cfg80211 *cfg)
{
	struct net_info *_net_info, *next;
	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	list_for_each_entry_safe(_net_info, next, &cfg->net_list, list) {
		GCC_DIAGNOSTIC_POP();
		if (_net_info->ndev &&
			test_bit(WL_STATUS_REMAINING_ON_CHANNEL, &_net_info->sme_state))
			return _net_info->ndev;
	}

	return NULL;
}
#endif /* WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST */

bool
wl_cfg80211_macaddr_sync_reqd(struct net_device *dev)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	WL_DBG(("enter \n"));
	if (!wdev) {
		WL_ERR(("no wdev present\n"));
		return false;
	}

	BCM_REFERENCE(cfg);

#if defined(WL_STATIC_IF)
	/* In soft case too role upgrade happens
	 * from STA to AP in some cases.These
	 * cases will have iftype as STATION.
	 */
	if (wl_cfg80211_static_if(cfg, dev)) {
		WL_INFORM_MEM(("STATIC interface\n"));
		return true;
	}
#endif /* WL_STATIC_IF && WL_SOFTAP_RAND */

	switch (wdev->iftype) {
#ifdef WL_P2P_RAND
		case NL80211_IFTYPE_P2P_GO:
		case NL80211_IFTYPE_P2P_CLIENT:
			WL_INFORM_MEM(("P2P GO/GC interface\n"));
			return true;
#endif /* WL_P2P_RAND */
#if defined(WL_STA_ASSOC_RAND)
		case NL80211_IFTYPE_STATION:
			WL_INFORM_MEM(("STA interface\n"));
			return true;
#endif /* WL_STA_ASSOC_RAND */
#ifdef WL_SOFTAP_RAND
		case NL80211_IFTYPE_AP:
			WL_INFORM_MEM(("SOFTAP interface\n"));
			return true;
#endif /* WL_SOFTAP_RAND */
		default:
			WL_ERR(("no macthing if type\n"));
	}
	return false;
}

#if defined(WL_SUPPORT_BACKPORTED_KPATCHES) || (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0))
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
#ifdef WL_EXT_IAPSTA
	err = wl_ext_in4way_sync(ndev, AP_WAIT_STA_RECONNECT,
		WL_EXT_STATUS_DELETE_STA, (void *)mac_addr);
	if (err) {
		return 0;
	}
#endif

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

#ifdef WL_WPS_SYNC
		if (wl_wps_session_update(ndev,
			WPS_STATE_DISCONNECT_CLIENT, mac_addr) == BCME_UNSUPPORTED) {
			/* Ignore disconnect command from upper layer */
			WL_INFORM_MEM(("[WPS] Ignore client disconnect.\n"));
		} else
#endif /* WL_WPS_SYNC */
		{
			scb_val.val = DOT11_RC_DEAUTH_LEAVING;
			WL_MSG(dev->name, "Disconnect STA : %pM scb_val.val %d\n",
				mac_addr, scb_val.val);
#if defined(BCMDONGLEHOST)
			/* need to guarantee EAP-Failure send out before deauth */
			dhd_wait_pend8021x(dev);
#endif
			err = wldev_ioctl_set(dev, WLC_SCB_DEAUTHENTICATE_FOR_REASON, &scb_val,
				sizeof(scb_val_t));
			if (err < 0) {
				WL_ERR(("WLC_SCB_DEAUTHENTICATE_FOR_REASON err %d\n", err));
			}
		}
#ifdef CUSTOM_BLOCK_DEAUTH_AT_EAP_FAILURE
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0))
	}
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)) */
#endif /* CUSTOM_BLOCK_DEAUTH_AT_EAP_FAILURE */

	/* WAR Wait for the deauth event to come, supplicant will do the
	 * delete iface immediately and we will have problem in sending
	 * deauth frame if we delete the bss in firmware
	 * But we do not need additional delays for this WAR
	 * during P2P connection.
	 *
	 * Supplicant call this function with BCAST after doing
	 * wl_cfg80211_del_station() all GC stations with each addr.
	 * So, 400 ms delay can be called only once when GO disconnect all GC
	*/
	if (num_associated > 0 && ETHER_ISBCAST(mac_addr))
		wl_delay(400);

	return 0;
}

/* Implementation for post SCB authorize */
static void
wl_cfg80211_post_scb_auth(struct bcm_cfg80211 *cfg, struct net_device *dev)
{
#ifdef WBTEXT
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
#endif /* WBTEXT */

	LOG_TS(cfg, authorize_cmplt);
	CLR_TS(cfg, authorize_start);
	wl_set_drv_status(cfg, AUTHORIZED, dev);
#ifdef DHD_LOSSLESS_ROAMING
	wl_del_roam_timeout(cfg);
#endif
#ifdef WBTEXT
	/* send nbr request or BTM query to update RCC
	 * after 4-way handshake is completed
	 */
	if (dev->ieee80211_ptr->iftype == NL80211_IFTYPE_STATION &&
		dhdp->wbtext_support) {
		wl_cfg80211_wbtext_update_rcc(cfg, dev);
	}
#endif /* WBTEXT */
}

/* Currently adding support only for authorize/de-authorize flag
 * Need to be extended in future
 */
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
	int err = BCME_OK;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
#ifdef BCMSUP_4WAY_HANDSHAKE
	struct wl_security *sec;
#endif /* BCMSUP_4WAY_HANDSHAKE */
	struct net_device *ndev = ndev_to_wlc_ndev(dev, cfg);

	WL_DBG(("SCB_AUTHORIZE mac_addr:"MACDBG" sta_flags_mask:0x%x "
				"sta_flags_set:0x%x iface:%s \n", MAC2STRDBG(mac),
				params->sta_flags_mask, params->sta_flags_set, ndev->name));

	if ((wl_get_mode_by_netdev(cfg, dev) == WL_MODE_BSS) &&
		!(wl_get_drv_status(cfg, CONNECTED, dev))) {
		/* Return error indicating not in connected state */
		WL_ERR(("Ignore SCB_AUTHORIZE/DEAUTHORIZE in non connected state\n"));
		return -ENOTSUPP;
	}

	/* Processing only authorize/de-authorize flag for now */
	if (!(params->sta_flags_mask & BIT(NL80211_STA_FLAG_AUTHORIZED))) {
		WL_ERR(("WLC_SCB_AUTHORIZE sta_flags_mask not set \n"));
		return -ENOTSUPP;
	}

	if (!(params->sta_flags_set & BIT(NL80211_STA_FLAG_AUTHORIZED))) {
		err = wldev_ioctl_set(ndev, WLC_SCB_DEAUTHORIZE, mac, ETH_ALEN);
		if (unlikely(err)) {
			WL_ERR(("WLC_SCB_DEAUTHORIZE error (%d)\n", err));
		} else {
			WL_INFORM_MEM(("[%s] WLC_SCB_DEAUTHORIZE " MACDBG "\n",
				ndev->name, MAC2STRDBG(mac)));
		}
		wl_clr_drv_status(cfg, AUTHORIZED, dev);
		CLR_TS(cfg, authorize_start);
		CLR_TS(cfg, conn_start);
		return err;
	}
	/* In case of 4way HS offloaded to FW and key_mgmt being 8021x, even the SCB
	* authorization is also offloaded to FW. So on reception of SCB authorize in the above
	* cases we avoid explicit call to ioctl WLC_SCB_AUTHORIZE. The post SCB authorize
	* actions are done from context of WLC_E_PSK_SUP event handler
	*/
#ifdef BCMSUP_4WAY_HANDSHAKE
	sec = wl_read_prof(cfg, ndev, WL_PROF_SEC);
	if (
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0))
		(wiphy_ext_feature_isset(wiphy, NL80211_EXT_FEATURE_4WAY_HANDSHAKE_STA_1X)) &&
#else
		(cfg->wdev->wiphy->features & NL80211_FEATURE_FW_4WAY_HANDSHAKE) &&
#endif
		((sec->wpa_auth == WLAN_AKM_SUITE_8021X) ||
		(sec->wpa_auth == WL_AKM_SUITE_SHA256_1X))) {
		return BCME_OK;
	}
#endif /* BCMSUP_4WAY_HANDSHAKE */
	err = wldev_ioctl_set(ndev, WLC_SCB_AUTHORIZE, mac, ETH_ALEN);
	if (unlikely(err)) {
		WL_ERR(("WLC_SCB_AUTHORIZE error (%d)\n", err));
	} else {
		WL_INFORM_MEM(("[%s] WLC_SCB_AUTHORIZE " MACDBG "\n",
			ndev->name, MAC2STRDBG(mac)));
#ifdef WL_WPS_SYNC
		wl_wps_session_update(ndev, WPS_STATE_AUTHORIZE, mac);
#endif /* WL_WPS_SYNC */
	}

	/* Post SCB authorize actions */
	wl_cfg80211_post_scb_auth(cfg, ndev);

	return err;
}
#endif /* WL_SUPPORT_BACKPORTED_KPATCHES || KERNEL_VER >= KERNEL_VERSION(3, 2, 0)) */

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
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);

	buf = (char *)MALLOCZ(cfg->osh, sizeof(char) * WLC_IOCTL_MAXLEN);
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
	MFREE(cfg->osh, buf, sizeof(char) * WLC_IOCTL_MAXLEN);

	return 0;
exit:
	MFREE(cfg->osh, buf, sizeof(char) * WLC_IOCTL_MAXLEN);
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

#ifdef BCMDONGLEHOST
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);
	if (!(dhd->op_mode & DHD_FLAG_HOSTAP_MODE)) {
		return -ENOENT;
	}
#endif
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
		/*
		 * FIXME:
		 *
		 *	Mostly set channel should not fail. because we are
		 *	traversing through Valid channel list. In case it fails,
		 *	right now we are passing the stats for previous channel.
		 */
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

	survey = (struct wl_dump_survey *)MALLOCZ(cfg->osh,
		sizeof(struct wl_dump_survey));
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
	MFREE(cfg->osh, survey, sizeof(struct wl_dump_survey));

	return 0;
exit:
	MFREE(cfg->osh, survey, sizeof(struct wl_dump_survey));
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
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0))
	.abort_scan = wl_cfg80211_abort_scan,
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)) */
	.set_wiphy_params = wl_cfg80211_set_wiphy_params,
	.join_ibss = wl_cfg80211_join_ibss,
	.leave_ibss = wl_cfg80211_leave_ibss,
	.get_station = wl_cfg80211_get_station,
	.dump_station = wl_cfg80211_dump_station,
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
	.set_pmksa = wl_cfg80211_set_pmksa,
	.del_pmksa = wl_cfg80211_del_pmksa,
	.flush_pmksa = wl_cfg80211_flush_pmksa,
	.remain_on_channel = wl_cfgscan_remain_on_channel,
	.cancel_remain_on_channel = wl_cfgscan_cancel_remain_on_channel,
	.mgmt_tx = wl_cfg80211_mgmt_tx,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 8, 0))
	.mgmt_frame_register = wl_cfg80211_mgmt_frame_register,
#else
	.update_mgmt_frame_registrations = wl_cfg80211_mgmt_frame_register,
#endif
	.change_bss = wl_cfg80211_change_bss,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)) || \
	defined(WL_COMPAT_WIRELESS)
	.set_channel = wl_cfg80211_set_channel,
#endif /* ((LINUX_VERSION < VERSION(3, 6, 0)) || WL_COMPAT_WIRELESS */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 0)) && \
	!defined(WL_COMPAT_WIRELESS)
	.set_beacon = wl_cfg80211_add_set_beacon,
	.add_beacon = wl_cfg80211_add_set_beacon,
	.del_beacon = wl_cfg80211_del_beacon,
#else
	.change_beacon = wl_cfg80211_change_beacon,
	.start_ap = wl_cfg80211_start_ap,
	.stop_ap = wl_cfg80211_stop_ap,
#endif /* LINUX_VERSION < KERNEL_VERSION(3,4,0) && !WL_COMPAT_WIRELESS */
#ifdef WL_SCHED_SCAN
	.sched_scan_start = wl_cfg80211_sched_scan_start,
	.sched_scan_stop = wl_cfg80211_sched_scan_stop,
#endif /* WL_SCHED_SCAN */
#if defined(WL_SUPPORT_BACKPORTED_KPATCHES) || (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0))
	.del_station = wl_cfg80211_del_station,
	.change_station = wl_cfg80211_change_station,
	.mgmt_tx_cancel_wait = wl_cfg80211_mgmt_tx_cancel_wait,
#endif /* WL_SUPPORT_BACKPORTED_KPATCHES || KERNEL_VERSION >= (3,2,0) */
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 2, 0)) || \
	defined(WL_COMPAT_WIRELESS)
	.tdls_mgmt = wl_cfg80211_tdls_mgmt,
	.tdls_oper = wl_cfg80211_tdls_oper,
#endif /* LINUX_VERSION > VERSION(3, 2, 0) || WL_COMPAT_WIRELESS */
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
#ifdef WL_CLIENT_SAE
	.external_auth = wl_cfg80211_external_auth,
#endif /* WL_CLIENT_SAE */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0))
	/* This should be enabled from kernel version which supports this */
	.update_connect_params = wl_cfg80211_update_connect_params,
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0) */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0))
	.set_pmk = wl_cfg80211_set_pmk,
	.del_pmk = wl_cfg80211_del_pmk,
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0) */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0))
	.channel_switch = wl_cfg80211_channel_switch,
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0) */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0))
#ifdef WLFBT
	.update_ft_ies = wl_cfg80211_update_ft_ies,
#endif /* WLFBT */
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0) */
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
#endif /* WLMESH_CFG80211 */
	default:
		return NL80211_IFTYPE_UNSPECIFIED;
	}

	return err;
}

static bool
wl_is_ccode_change_allowed(struct net_device *net)
{
	struct wireless_dev *wdev = ndev_to_wdev(net);
	struct wiphy *wiphy = wdev->wiphy;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct net_info *iter, *next;

	/* Country code isn't allowed change on AP/GO, NDP established  */
	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	for_each_ndev(cfg, iter, next) {
		GCC_DIAGNOSTIC_POP();
		if (iter->ndev) {
			if (wl_get_drv_status(cfg, AP_CREATED, iter->ndev)) {
				WL_ERR(("AP active. skip coutry ccode change"));
				return false;
			}
		}
	}

#ifdef WL_NAN
	if (wl_cfgnan_is_enabled(cfg) && wl_cfgnan_is_dp_active(net)) {
		WL_ERR(("NDP established. skip coutry ccode change"));
		return false;
	}
#endif /* WL_NAN */
	return true;
}

static bool
wl_is_ccode_change_required(struct net_device *net,
	char *country_code, int revinfo)
{
	s32 ret = BCME_OK;
	wl_country_t cspec = {{0}, 0, {0}};
	wl_country_t cur_cspec = {{0}, 0, {0}};

	ret = wldev_iovar_getbuf(net, "country", NULL, 0, &cur_cspec,
		sizeof(cur_cspec), NULL);
	if (ret < 0) {
		WL_ERR(("get country code failed = %d\n", ret));
		return true;
	}
	/* If translation table is available, update cspec */
	cspec.rev = revinfo;
	strlcpy(cspec.country_abbrev, country_code, WL_CCODE_LEN + 1);
	strlcpy(cspec.ccode, country_code, WL_CCODE_LEN + 1);
	dhd_get_customized_country_code(net, cspec.country_abbrev, &cspec);
	if ((cur_cspec.rev == cspec.rev) &&
		(strncmp(cur_cspec.ccode, cspec.ccode, WL_CCODE_LEN) == 0) &&
		(strncmp(cur_cspec.country_abbrev, cspec.country_abbrev, WL_CCODE_LEN) == 0)) {
			WL_INFORM_MEM(("country code = %s/%d is already configured\n",
				country_code, revinfo));
			return false;
	}
	return true;
}

void
wl_cfg80211_cleanup_connection(struct net_device *net, bool user_enforced)
{
	s32 ret = BCME_OK;
	struct wireless_dev *wdev = ndev_to_wdev(net);
	struct wiphy *wiphy = wdev->wiphy;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct net_info *iter, *next;
	scb_val_t scbval;

	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	for_each_ndev(cfg, iter, next) {
		GCC_DIAGNOSTIC_POP();
		if (iter->ndev) {
			if (wl_get_drv_status(cfg, AP_CREATED, iter->ndev)) {
				memset(scbval.ea.octet, 0xff, ETHER_ADDR_LEN);
				scbval.val = DOT11_RC_DEAUTH_LEAVING;
				if ((ret = wldev_ioctl_set(iter->ndev,
						WLC_SCB_DEAUTHENTICATE_FOR_REASON,
						&scbval, sizeof(scb_val_t))) != 0) {
					WL_ERR(("Failed to disconnect STAs %d\n", ret));
				}

			} else if (wl_get_drv_status(cfg, CONNECTED, iter->ndev)) {
				if ((iter->ndev == net) && !user_enforced)
					continue;
				wl_cfg80211_disassoc(iter->ndev, WLAN_REASON_DEAUTH_LEAVING);
			} else {
				WL_INFORM(("Disconnected state. Interface clean "
					"up skipped for ifname:%s\n", iter->ndev->name));
			}
		}
	}

	wl_cfgscan_cancel_scan(cfg);

	/* Clean up NAN connection */
#ifdef WL_NAN
	if (wl_cfgnan_is_enabled(cfg)) {
		mutex_lock(&cfg->if_sync);
		ret = wl_cfgnan_check_nan_disable_pending(cfg, true, true);
		mutex_unlock(&cfg->if_sync);
		if (ret != BCME_OK) {
			WL_ERR(("failed to disable nan, error[%d]\n", ret));
		}
	}
#endif /* WL_NAN */
}

static int wl_copy_regd(const struct ieee80211_regdomain *regd_orig,
	struct ieee80211_regdomain *regd_copy)
{
	int i;

	if (memcpy_s(regd_copy, sizeof(*regd_copy), regd_orig, sizeof(*regd_orig))) {
		return BCME_ERROR;
	}
	for (i = 0; i < regd_orig->n_reg_rules; i++) {
		if (memcpy_s(&regd_copy->reg_rules[i], sizeof(regd_copy->reg_rules[i]),
			&regd_orig->reg_rules[i], sizeof(regd_orig->reg_rules[i]))) {
			return BCME_ERROR;
		}
	}
	return BCME_OK;
}

static void wl_notify_regd(struct wiphy *wiphy, char *country_code)
{
	struct ieee80211_regdomain *regd_copy = NULL;
	int regd_len;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);

	regd_len = sizeof(brcm_regdom) + (brcm_regdom.n_reg_rules *
			sizeof(struct ieee80211_reg_rule));

	regd_copy = (struct ieee80211_regdomain *)MALLOCZ(cfg->osh, regd_len);
	if (!regd_copy) {
		WL_ERR(("failed to alloc regd_copy\n"));
		return;
	}

	/* the upper layer function below requires non-const type */
	if (wl_copy_regd(&brcm_regdom, regd_copy)) {
		WL_ERR(("failed to copy new regd\n"));
		goto exit;
	}

	if (country_code) {
		if (memcpy_s(regd_copy->alpha2, sizeof(regd_copy->alpha2),
			country_code, WL_CCODE_LEN)) {
			WL_ERR(("failed to copy new ccode:%s\n", country_code));
			goto exit;
		}
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0))
	if (rtnl_is_locked()) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0))
		wiphy_lock(wiphy);
		regulatory_set_wiphy_regd_sync(wiphy, regd_copy);
		wiphy_unlock(wiphy);
#else
		regulatory_set_wiphy_regd_sync_rtnl(wiphy, regd_copy);
#endif /* LINUX_VERSION > 5.12.0 */
	} else {
		regulatory_set_wiphy_regd(wiphy, regd_copy);
	}
#else
	wiphy_apply_custom_regulatory(wiphy, regd_copy);
#endif /* LINUX_VERSION > 4.0.0 */

exit:
	MFREE(cfg->osh, regd_copy, regd_len);
	return;
}

s32
wl_cfg80211_set_country_code(struct net_device *net, char *country_code,
	bool notify, bool user_enforced, int revinfo)
{
	s32 ret = BCME_OK;
	struct wireless_dev *wdev = ndev_to_wdev(net);
	struct wiphy *wiphy = wdev->wiphy;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	BCM_REFERENCE(cfg);

	if (revinfo < 0) {
		WL_ERR(("country revinfo wrong : %d\n", revinfo));
		ret = BCME_BADARG;
		goto exit;
	}

	if ((wl_is_ccode_change_required(net, country_code, revinfo) == false) &&
		!dhd_force_country_change(net)) {
		goto exit;
	}

	if (wl_is_ccode_change_allowed(net) == false) {
		WL_ERR(("country code change isn't allowed during AP role/NAN connected\n"));
		ret = BCME_EPERM;
		goto exit;
	}

	wl_cfg80211_cleanup_connection(net, user_enforced);

	/* Store before applying - so that if event comes earlier that is handled properly */
	if (strlcpy(cfg->country, country_code, WL_CCODE_LEN) >= WLC_CNTRY_BUF_SZ) {
		WL_ERR(("country code copy failed :%d\n", ret));
		goto exit;
	}

	ret = wldev_set_country(net, country_code,
		notify, revinfo);
	if (ret < 0) {
		WL_ERR(("set country Failed :%d\n", ret));
		bzero(cfg->country, sizeof(cfg->country));
		goto exit;
	}

	/* send up the hint so that upper layer apps
	 * can refresh the channel
	 * list
	 */
	if (!IS_REGDOM_SELF_MANAGED(wiphy)) {
		regulatory_hint(wiphy, country_code);
	} else {
		wl_notify_regd(wiphy, country_code);
	}

exit:
	return OSL_ERROR(ret);
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
	ret = wl_cfg80211_set_country_code(bcmcfg_to_prmry_ndev(cfg), request->alpha2, false,
			(request->initiator == NL80211_REGDOM_SET_BY_USER ? true : false),
			revinfo);
	if (ret < 0) {
		WL_ERR(("Set country failed ret:%d\n", ret));
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

int wl_features_set(u8 *array, uint8 len, u32 ftidx)
{
	u8* ft_byte;

	if ((ftidx / 8u) >= len)
		return BCME_BADARG;

	ft_byte = &array[ftidx / 8u];
	*ft_byte |= BIT(ftidx % 8u);
	return BCME_OK;
}

static
void wl_config_custom_regulatory(struct wiphy *wiphy)
{

#if defined(WL_SELF_MANAGED_REGDOM) && (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0))
	/* Use self managed regulatory domain */
	wiphy->regulatory_flags |= REGULATORY_WIPHY_SELF_MANAGED |
			REGULATORY_IGNORE_STALE_KICKOFF;
	WL_DBG(("Self managed regdom\n"));
	return;
#else /* WL_SELF_MANAGED_REGDOM && KERNEL >= 4.0 */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
	wiphy->regulatory_flags |=
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0))
		REGULATORY_IGNORE_STALE_KICKOFF |
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0) */
		REGULATORY_CUSTOM_REG;
#else /* KERNEL VER >= 3.14 */
	wiphy->flags |= WIPHY_FLAG_CUSTOM_REGULATORY;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0) */
	wl_notify_regd(wiphy, NULL);
	WL_DBG(("apply custom regulatory\n"));
#endif /* WL_SELF_MANAGED_REGDOM && KERNEL >= 4.0 */
}

static s32 wl_setup_wiphy(struct wireless_dev *wdev, struct device *sdiofunc_dev, dhd_pub_t *context)
{
	s32 err = 0;
#ifdef CONFIG_PM
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0))
	struct cfg80211_wowlan *brcm_wowlan_config = NULL;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0) */
#endif /* CONFIG_PM */

//#if defined(BCMDONGLEHOST) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0) || defined(WL_COMPAT_WIRELESS))
	dhd_pub_t *dhd = (dhd_pub_t *)context;
	BCM_REFERENCE(dhd);

	if (!dhd) {
		WL_ERR(("DHD is NULL!!"));
		err = -ENODEV;
		return err;
	}
//#endif /* defined(BCMDONGLEHOST) && KERNEL >= 3, 4, 0 || defined(WL_COMPAT_WIRELESS)) */

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
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0))
	wdev->wiphy->flags |= WIPHY_FLAG_SUPPORTS_SCHED_SCAN;
#else
	wdev->wiphy->max_sched_scan_reqs = 1;
#endif /* LINUX_VER < 4.12 */
#endif /* WL_SCHED_SCAN */
#ifdef WLMESH_CFG80211
	wdev->wiphy->flags |= WIPHY_FLAG_MESH_AUTH;
#endif /* WLMESH_CFG80211 */
	wdev->wiphy->interface_modes =
		BIT(NL80211_IFTYPE_STATION)
		| BIT(NL80211_IFTYPE_ADHOC)
#if !defined(WL_ENABLE_P2P_IF) && !defined(WL_CFG80211_P2P_DEV_IF)
	/*
	 * This monitor mode support creates an issue in registering
	 * Action frame for P2P-GO, this was leading an error in receiving
	 * action frames to GO interface.Keeping the code here because
	 * monitor mode code has kept as it is in other modules,
	 * though we are not supporting this mode.
	 */
		| BIT(NL80211_IFTYPE_MONITOR)
#endif /* !WL_ENABLE_P2P_IF && !WL_CFG80211_P2P_DEV_IF */
#if defined(WL_IFACE_COMB_NUM_CHANNELS) || \
	defined(WL_CFG80211_P2P_DEV_IF)
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
	(defined(WL_IFACE_COMB_NUM_CHANNELS) || \
	defined(WL_CFG80211_P2P_DEV_IF))
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
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 39)) && \
	!defined(WL_COMPAT_WIRELESS)
		WIPHY_FLAG_SUPPORTS_SEPARATE_DEFAULT_KEYS |
#endif
		WIPHY_FLAG_4ADDR_STATION;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0))
	/*
	 * If FW ROAM flag is advertised, upper layer doesn't provide the
	 * bssid & freq in the connect command. However, kernel ver >= 3.15,
	 * provides bssid_hint & freq_hint which can be used by the firmware.
	 * fw_ap_select variable determines whether FW selects the AP or the
	 * user space selects the target AP within the given ESS.
	 */
	wdev->wiphy->flags |= WIPHY_FLAG_SUPPORTS_FW_ROAM;
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0) */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0)) || \
	defined(WL_COMPAT_WIRELESS)
	/* this flag should be added to support wpa_supplicant 1.0+ */
	wdev->wiphy->flags |= WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL |
		WIPHY_FLAG_OFFCHAN_TX;
#endif
#if defined(WL_SUPPORT_BACKPORTED_KPATCHES) || (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0))
	/* From 3.4 kernel ownards AP_SME flag can be advertised
	 * to remove the patch from supplicant
	 */
	wdev->wiphy->flags |= WIPHY_FLAG_HAVE_AP_SME;

#ifdef WL_CFG80211_ACL
	/* Configure ACL capabilities. */
	wdev->wiphy->max_acl_mac_addrs = MAX_NUM_MAC_FILT;
#endif

#if defined(BCMDONGLEHOST) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0) || \
	defined(WL_COMPAT_WIRELESS))
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
#endif /* defined(BCMDONGLEHOST) && KERNEL >= 3, 4, 0 || defined(WL_COMPAT_WIRELESS)) */
#endif /* WL_SUPPORT_BACKPORTED_KPATCHES) || (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)) */

#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 2, 0)) || \
	defined(WL_COMPAT_WIRELESS)
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
	* Note: wiphy->wowlan_config is freed by cfg80211 layer.
	* so use malloc instead of MALLOC(osh) to avoid false alarm.
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
	wl_config_custom_regulatory(wdev->wiphy);

#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 14, 0)) || defined(WL_VENDOR_EXT_SUPPORT)
	WL_INFORM_MEM(("Registering Vendor80211\n"));
	err = wl_cfgvendor_attach(wdev->wiphy, dhd);
	if (unlikely(err < 0)) {
		WL_ERR(("Couldn not attach vendor commands (%d)\n", err));
	}
#endif /* (LINUX_VERSION_CODE > KERNEL_VERSION(3, 14, 0)) || defined(WL_VENDOR_EXT_SUPPORT) */
#ifdef WL_FILS
	wiphy_ext_feature_set(wdev->wiphy, NL80211_EXT_FEATURE_FILS_SK_OFFLOAD);
#endif /* WL_FILS */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0))
	wdev->wiphy->flags |= WIPHY_FLAG_HAS_CHANNEL_SWITCH;
	wdev->wiphy->max_num_csa_counters = WL_MAX_NUM_CSA_COUNTERS;
#endif /* LINUX_VERSION_CODE > KERNEL_VERSION(3, 12, 0) */

#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)) && \
	(LINUX_VERSION_CODE <= KERNEL_VERSION(3, 3, 0))) && defined(WL_IFACE_COMB_NUM_CHANNELS)
	/* Workaround for a cfg80211 bug */
	wdev->wiphy->flags &= ~WIPHY_FLAG_ENFORCE_COMBINATIONS;
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)) && defined(SUPPORT_RANDOM_MAC_SCAN)
		wdev->wiphy->features |= (NL80211_FEATURE_SCHED_SCAN_RANDOM_MAC_ADDR |
			NL80211_FEATURE_SCAN_RANDOM_MAC_ADDR);
		wdev->wiphy->max_sched_scan_plans = 1; /* multiple plans not supported */
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)) && defined(SUPPORT_RANDOM_MAC_SCAN) */

#if defined(WL_SAE) || defined(WL_CLIENT_SAE)
	if (wl_extsae_chip(dhd))
		wdev->wiphy->features |= NL80211_FEATURE_SAE;
#endif /* WL_SAE || WL_CLIENT_SAE */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0)) && defined(BCMSUP_4WAY_HANDSHAKE)
	if (FW_SUPPORTED(dhd, idsup)) {
		wiphy_ext_feature_set(wdev->wiphy, NL80211_EXT_FEATURE_4WAY_HANDSHAKE_STA_PSK);
		wiphy_ext_feature_set(wdev->wiphy, NL80211_EXT_FEATURE_4WAY_HANDSHAKE_STA_1X);
	}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0) && defined(BCMSUP_4WAY_HANDSHAKE) */
#ifdef WL_SCAN_TYPE
	/* These scan types will be mapped to default scan on non-supported chipset */
	/* Advertise scan type capability. */
	wiphy_ext_feature_set(wdev->wiphy, NL80211_EXT_FEATURE_LOW_SPAN_SCAN);
	wiphy_ext_feature_set(wdev->wiphy, NL80211_EXT_FEATURE_LOW_POWER_SCAN);
	wiphy_ext_feature_set(wdev->wiphy, NL80211_EXT_FEATURE_HIGH_ACCURACY_SCAN);
	wdev->wiphy->features |= NL80211_FEATURE_LOW_PRIORITY_SCAN;
#endif /* WL_SCAN_TYPE */

	/* Now we can register wiphy with cfg80211 module */
	err = wiphy_register(wdev->wiphy);
	if (unlikely(err < 0)) {
		WL_ERR(("Couldn not register wiphy device (%d)\n", err));
		wiphy_free(wdev->wiphy);
		return err;
	}

	/* set wiphy->regd through reg_process_self_managed_hints
	 * need to call it after wiphy_register
	 * since wiphy_register adds rdev to cfg80211_rdev_list
	 */
	if (IS_REGDOM_SELF_MANAGED(wdev->wiphy)) {
		rtnl_lock();
		wl_notify_regd(wdev->wiphy, NULL);
		rtnl_unlock();
	}
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
		/* Reset wowlan & wowlan_config before Unregister to avoid Kernel Panic */
		WL_DBG(("clear wowlan\n"));
		wdev->wiphy->wowlan = NULL;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0) */
#endif /* CONFIG_PM && WL_CFG80211_P2P_DEV_IF */
#if defined(WL_SELF_MANAGED_REGDOM) && (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0))
		/* Making regd ptr NULL, to avoid reference/freeing by regulatory unregister */
		wiphy->regd = NULL;
#endif /* WL_SELF_MANAGED_REGDOM && KERNEL >= 4.0 */
		wiphy_unregister(wdev->wiphy);
		wdev->wiphy->dev.parent = NULL;
		wdev->wiphy = NULL;
	}

	wl_delete_all_netinfo(cfg);
	if (wiphy) {
		if (wdev->netdev)
			wdev->netdev->ieee80211_ptr = NULL;
		wdev->netdev = NULL;
		MFREE(cfg->osh, wdev, sizeof(*wdev));
		cfg->wdev = NULL;
		wiphy_free(wiphy);
	}

	/* PLEASE do NOT call any function after wiphy_free, the driver's private structure "cfg",
	 * which is the private part of wiphy, has been freed in wiphy_free !!!!!!!!!!!
	 */
}

static s32
wl_post_linkup_ops(struct bcm_cfg80211 *cfg, wl_assoc_status_t *as)
{
	s32 ret = BCME_OK;
	int vndr_oui_num = 0;
	struct net_device *ndev = as->ndev;
	char vndr_oui[MAX_VNDR_OUI_STR_LEN] = {0, };
#ifdef BCMDONGLEHOST
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
#endif /* BCMDONGLEHOST */

#ifdef WL_WPS_SYNC
	wl_wps_session_update(ndev, WPS_STATE_LINKUP, as->addr);
#endif /** WL_WPS_SYNC */

	if (IS_PRIMARY_NDEV(cfg, ndev)) {
		vndr_oui_num = wl_vndr_ies_get_vendor_oui(cfg,
			ndev, vndr_oui, ARRAY_SIZE(vndr_oui));
		if (vndr_oui_num > 0) {
			WL_INFORM_MEM(("[%s] vendor oui: %s\n",
				ndev->name, vndr_oui));
		}
	}

#ifdef ESCAN_CHANNEL_CACHE
	/* Update RCC list. FW clears RCC from join iovar context */
	update_roam_cache(cfg, ioctl_version);
#endif /* ESCAN_CHANNEL_CACHE */

#ifdef BCMDONGLEHOST
	if (as->event_type == WLC_E_LINK) {
		/* Arm pkt logging timer */
		dhd_dump_mod_pkt_timer(dhdp, PKT_CNT_RSN_CONNECT);
	}
#endif /* BCMDONGLEHOST */
#ifdef WBTEXT
	if ((ndev->ieee80211_ptr->iftype == NL80211_IFTYPE_STATION) &&
		dhdp->wbtext_support &&	(as->event_type == WLC_E_SET_SSID)) {
		/* set wnm_keepalives_max_idle after association */
		wl_cfg80211_wbtext_set_wnm_maxidle(cfg, ndev);
	}
#endif /* WBTEXT */

#ifdef DHD_EVENT_LOG_FILTER
	dhd_event_log_filter_notify_connect_done(dhdp,
			as->addr, false);
#endif /* DHD_EVENT_LOG_FILTER */

#ifdef CUSTOM_SET_OCLOFF
	if (dhdp->ocl_off) {
		int err = 0;
		int ocl_enable = 0;
		err = wldev_iovar_setint(ndev, "ocl_enable", ocl_enable);
		if (err != 0) {
			WL_ERR(("[WIFI_SEC] Set ocl_enable %d"
				" failed %d\n",
				ocl_enable, err));
		} else {
			WL_ERR(("[WIFI_SEC] Set ocl_enable %d"
				" succeeded %d\n",
				ocl_enable, err));
		}
	}
#endif /* CUSTOM_SET_OCLOFF */
#ifdef CUSTOM_SET_ANTNPM
	if (dhdp->mimo_ant_set) {
		int err = 0;

		WL_ERR(("[WIFI_SEC] mimo_ant_set = %d\n", dhdp->mimo_ant_set));
		err = wldev_iovar_setint(ndev, "txchain", dhdp->mimo_ant_set);
		if (err != 0) {
			WL_ERR(("[WIFI_SEC] Fail set txchain. err:%d\n", err));
		}
		err = wldev_iovar_setint(ndev, "rxchain", dhdp->mimo_ant_set);
		if (err != 0) {
			WL_ERR(("[WIFI_SEC] Fail set rxchain. err:%d\n", err));
		}
	}
#endif /* CUSTOM_SET_ANTNPM */

#if defined (ROAM_ENABLE) && defined (ROAM_AP_ENV_DETECTION)
	if (dhdp->roam_env_detection) {
		wldev_iovar_setint(ndev, "roam_env_detection",
			AP_ENV_INDETERMINATE);
	}
#endif /* ROAM_AP_ENV_DETECTION */

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

#ifdef CUSTOM_LONG_RETRY_LIMIT
	if (wl_set_retry(ndev, CUSTOM_LONG_RETRY_LIMIT, 1) < 0) {
		WL_ERR(("CUSTOM_LONG_RETRY_LIMIT set fail!\n"));
	}
#endif /* CUSTOM_LONG_RETRY_LIMIT */

#if defined(CONFIG_TIZEN)
	net_stat_tizen_update_wifi(ndev, WIFISTAT_CONNECTION);
#endif /* CONFIG_TIZEN */

#ifdef WL_BAM
	{
		struct ether_addr eth = {{0}};
		(void)memcpy_s(&eth.octet, ETH_ALEN, as->addr, ETH_ALEN);
		if (wl_adps_bad_ap_check(cfg, &eth)) {
			if (wl_adps_enabled(cfg, ndev)) {
				wl_adps_set_suspend(cfg, ndev, ADPS_SUSPEND);
			}
		}
	}
#endif	/* WL_BAM */

#ifdef CONFIG_TCPACK_FASTTX
	if (wl_get_chan_isvht80(ndev, dhdp))
		wldev_iovar_setint(ndev, "tcpack_fast_tx", 0);
	else
		wldev_iovar_setint(ndev, "tcpack_fast_tx", 1);
#endif /* CONFIG_TCPACK_FASTTX */
	return ret;
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
	struct cfg80211_pmksa pmksa;

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

	WL_INFORM_MEM(("Received Sae Key event for "MACDBG" key length %x %x",
		MAC2STRDBG(sae_key->peer_mac), sae_key->pmk_len, sae_key->pmkid_len));
	nla_put(skb, BRCM_SAE_KEY_ATTR_PEER_MAC, ETHER_ADDR_LEN, sae_key->peer_mac);
	nla_put(skb, BRCM_SAE_KEY_ATTR_PMK, sae_key->pmk_len, sae_key->pmk);
	nla_put(skb, BRCM_SAE_KEY_ATTR_PMKID, sae_key->pmkid_len, sae_key->pmkid);
	cfg80211_vendor_event(skb, kflags);
	/* wpa_supplicant will manage the PMK and PMKID from here on..
	 * Delete the PMK cache in firmware, if wlc_ver equals to MIN_PMKID_LIST_V3_FW_MAJOR
	 * else ignore.
	 * MIN_PMKID_LIST_V3_FW_MAJOR has two IOVAR's(pmklist_info and PMKDB).
	 */
	if (cfg->wlc_ver.wlc_ver_major == MIN_PMKID_LIST_V3_FW_MAJOR) {
		WL_INFORM_MEM(("Deleting the SAE PMK cache Info from firmware \n"));
		memset_s(&pmksa, sizeof(pmksa), 0, sizeof(pmksa));
		pmksa.bssid = sae_key->peer_mac;
		pmksa.pmkid = sae_key->pmkid;
		err = wl_cfg80211_update_pmksa(wiphy, ndev, &pmksa, FALSE);
		if (err != BCME_OK) {
			WL_ERR(("Failed to delete the SAE PMK cache Info from firmware %d\n", err));
		}
	}
done:
	return err;
}

static s32
wl_bss_handle_sae_auth_v1(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *event, void *data)
{
	int err = BCME_OK;
	wl_auth_event_t *auth_data;
	wl_sae_key_info_t sae_key;
	uint16 tlv_buf_len;
	auth_data = (wl_auth_event_t *)data;

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
	memcpy_s(sae_key.peer_mac, ETHER_ADDR_LEN, event->addr.octet, ETHER_ADDR_LEN);
	err = wl_cfg80211_event_sae_key(cfg, ndev, &sae_key);
	if (err) {
		WL_ERR(("Failed to event sae key info\n"));
	}
done:
	return err;
}

static s32
wl_bss_handle_sae_auth_v2(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *event, void *data)
{
	int err = BCME_OK;
	wl_auth_event_t *auth_data;
	wl_sae_key_info_t sae_key;
	uint16 tlv_buf_len;
	uint8 ssid[DOT11_MAX_SSID_LEN];
	const uint8 *tmp_buf;
	uint16 ssid_len;
	uint16 type_len;
	uint32 type;
	pmkid_v3_t *t_pmkid = NULL;

	auth_data = (wl_auth_event_t *)data;

	tlv_buf_len = auth_data->length - WL_AUTH_EVENT_FIXED_LEN_V2;

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
	(void)memcpy_s(sae_key.peer_mac, ETHER_ADDR_LEN, event->addr.octet, ETHER_ADDR_LEN);

	tmp_buf = bcm_get_data_from_xtlv_buf(auth_data->xtlvs, tlv_buf_len,
		WL_AUTH_PMKID_TYPE_TLV_ID, &type_len, BCM_XTLV_OPTION_ALIGN32);

	memcpy(&type, tmp_buf, MIN(type_len, sizeof(type)));
	if (type == WL_AUTH_PMKID_TYPE_SSID) {
		int idx;
		int idx2;
		pmkid_list_v3_t *spmk_list = &cfg->spmk_info_list->pmkids;

		tmp_buf = bcm_get_data_from_xtlv_buf(auth_data->xtlvs, tlv_buf_len,
			WL_AUTH_SSID_TLV_ID, &ssid_len, BCM_XTLV_OPTION_ALIGN32);
		if (tmp_buf == NULL) {
			return BCME_ERROR;
		}
		bzero(ssid, sizeof(ssid));
		(void)memcpy_s(ssid, sizeof(ssid), tmp_buf, MIN(sizeof(ssid), ssid_len));
		for (idx = 0; idx < spmk_list->count; idx++) {
			t_pmkid = &spmk_list->pmkid[idx];
			if (ssid_len == t_pmkid->ssid_len &&
				!memcmp(ssid, t_pmkid->ssid, MIN(sizeof(ssid), ssid_len))) {
				break;
			}
		}
		if (idx >= spmk_list->count) {
			if (spmk_list->count == MAXPMKID) {
				/* remove oldest PMK info */
				for (idx2 = 0; idx2 < spmk_list->count - 1; idx2++) {
					(void)memcpy_s(&spmk_list->pmkid[idx2], sizeof(pmkid_v3_t),
						&spmk_list->pmkid[idx2 + 1], sizeof(pmkid_v3_t));
				}
				t_pmkid = &spmk_list->pmkid[spmk_list->count - 1];
			} else {
				t_pmkid = &spmk_list->pmkid[spmk_list->count++];
			}
		}
		if (!t_pmkid) {
			WL_ERR(("SPMK TPMKID is null\n"));
			return BCME_NOTFOUND;
		}
		bzero(t_pmkid, sizeof(pmkid_v3_t));
		memcpy(&t_pmkid->bssid, event->addr.octet, 6);
		t_pmkid->ssid_len = ssid_len;
		err = memcpy_s(t_pmkid->ssid, sizeof(t_pmkid->ssid), ssid, ssid_len);
		if (err != BCME_OK) {
			goto done;
		}
		/* COPY but not used */
		t_pmkid->pmkid_len = sae_key.pmkid_len;
		memcpy(t_pmkid->pmkid, sae_key.pmkid, sae_key.pmkid_len);
		t_pmkid->pmk_len = sae_key.pmk_len;
		memcpy(t_pmkid->pmk, sae_key.pmk, sae_key.pmk_len);
	}

	err = wl_cfg80211_event_sae_key(cfg, ndev, &sae_key);
	if (err) {
		WL_ERR(("Failed to event sae key info\n"));
	}
done:
	return err;
}

s32
wl_bss_handle_sae_auth(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *event, void *data)
{
	int err = BCME_OK;
	uint status = ntoh32(event->status);
	wl_auth_event_t *auth_data;

	if (status == WLC_E_STATUS_SUCCESS) {
		auth_data = (wl_auth_event_t *)data;
		if (auth_data->version == WL_AUTH_EVENT_DATA_V1) {
			err = wl_bss_handle_sae_auth_v1(cfg, ndev, event, data);
		} else if (auth_data->version == WL_AUTH_EVENT_DATA_V2) {
			err = wl_bss_handle_sae_auth_v2(cfg, ndev, event, data);
		} else {
			WL_ERR(("unknown auth event data version %x\n",
				auth_data->version));
			err = BCME_VERSION;
		}
	}
	WL_INFORM_MEM(("SAE AUTH status:%d ret: %d\n", status, err));
	return err;
}
#endif /* WL_SAE */

#if defined(DHD_ENABLE_BIGDATA_LOGGING)
enum {
	BIGDATA_ASSOC_REJECT_NO_ACK = 1,
	BIGDATA_ASSOC_REJECT_FAIL = 2,
	BIGDATA_ASSOC_REJECT_UNSOLICITED = 3,
	BIGDATA_ASSOC_REJECT_TIMEOUT = 4,
	BIGDATA_ASSOC_REJECT_ABORT = 5,
	BIGDATA_ASSOC_REJECT_NO_NETWWORKS = 6,
	BIGDATA_ASSOC_REJECT_MAX = 50
};

int wl_get_connect_failed_status(struct bcm_cfg80211 *cfg, const wl_event_msg_t *e)
{
	u32 status = ntoh32(e->status);

	cfg->assoc_reject_status = 0;

	if (status != WLC_E_STATUS_SUCCESS) {
		WL_INFORM(("auth assoc status event=%d e->status %d e->reason %d \n",
			ntoh32(cfg->event_auth_assoc.event_type),
			(int)ntoh32(cfg->event_auth_assoc.status),
			(int)ntoh32(cfg->event_auth_assoc.reason)));

		/* Populate status based on cached auth/assoc status value */
		switch ((int)ntoh32(cfg->event_auth_assoc.status)) {
			case WLC_E_STATUS_NO_ACK:
				cfg->assoc_reject_status = BIGDATA_ASSOC_REJECT_NO_ACK;
				break;
			case WLC_E_STATUS_FAIL:
				cfg->assoc_reject_status = BIGDATA_ASSOC_REJECT_FAIL;
				break;
			case WLC_E_STATUS_UNSOLICITED:
				cfg->assoc_reject_status = BIGDATA_ASSOC_REJECT_UNSOLICITED;
				break;
			case WLC_E_STATUS_TIMEOUT:
				cfg->assoc_reject_status = BIGDATA_ASSOC_REJECT_TIMEOUT;
				break;
			case WLC_E_STATUS_ABORT:
				cfg->assoc_reject_status = BIGDATA_ASSOC_REJECT_ABORT;
				break;
			case WLC_E_STATUS_SUCCESS:
				if (status == WLC_E_STATUS_NO_NETWORKS) {
					cfg->assoc_reject_status =
						BIGDATA_ASSOC_REJECT_NO_NETWWORKS;
					break;
				}
			default:
				cfg->assoc_reject_status = BIGDATA_ASSOC_REJECT_MAX;
				break;
		}
		if (cfg->assoc_reject_status) {
			if (ntoh32(cfg->event_auth_assoc.event_type) == WLC_E_ASSOC) {
				cfg->assoc_reject_status += BIGDATA_ASSOC_REJECT_MAX;
			}
		}
	}

	WL_INFORM_MEM(("assoc_reject_status %d \n", cfg->assoc_reject_status));

	return 0;
}

s32 wl_cfg80211_get_connect_failed_status(struct net_device *dev, char* cmd, int total_len)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	int bytes_written = 0;

	if (cfg == NULL) {
		return -1;
	}
	bytes_written = snprintf(cmd, total_len, "assoc_reject.status %d",
			cfg->assoc_reject_status);
	WL_ERR(("cmd: %s \n", cmd));
	return bytes_written;
}
#endif /* DHD_ENABLE_BIGDATA_LOGGING */

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
	chanspec_t chanspec;
	u32 freq;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0) */

	if (event == WLC_E_JOIN) {
		WL_INFORM_MEM(("[%s] joined in IBSS network\n", ndev->name));
	}
	if (event == WLC_E_START) {
		WL_INFORM_MEM(("[%s] started IBSS network\n", ndev->name));
	}
	if (event == WLC_E_JOIN || event == WLC_E_START ||
		(event == WLC_E_LINK && (flags == WLC_EVENT_MSG_LINK))) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
		err = wldev_iovar_getint(ndev, "chanspec", (s32 *)&chanspec);
		if (unlikely(err)) {
			WL_ERR(("Could not get chanspec %d\n", err));
			return err;
		}
		chanspec = wl_chspec_driver_to_host(chanspec);
		freq = wl_channel_to_frequency(wf_chspec_ctlchan(chanspec), CHSPEC_BAND(chanspec));
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
			WL_MSG(ndev->name, "IBSS BSSID is changed from " MACDBG " to " MACDBG "\n",
				MAC2STRDBG(cur_bssid), MAC2STRDBG((const u8 *)&e->addr));
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
			WL_MSG(ndev->name, "IBSS connected to " MACDBG "\n",
				MAC2STRDBG((const u8 *)&e->addr));
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
		WL_INFORM_MEM(("no action - join fail (IBSS mode)\n"));
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

/* 11kv feature flag for big data */
#define WL_BIGDATA_11KV_QBSSLOAD		0x00000001
#define WL_BIGDATA_11KV_PROXYARP		0x00000002
#define WL_BIGDATA_11KV_TFS			0x00000004
#define WL_BIGDATA_11KV_SLEEP			0x00000008
#define WL_BIGDATA_11KV_TIMBC			0x00000010
#define WL_BIGDATA_11KV_BSSTRANS		0x00000020
#define WL_BIGDATA_11KV_DMS			0x00000040
#define WL_BIGDATA_11KV_LINK_MEA		0x00000080
#define WL_BIGDATA_11KV_NBRREP			0x00000100
#define WL_BIGDATA_11KV_BCNPASSIVE		0x00000200
#define WL_BIGDATA_11KV_BCNACTIVE		0x00000400
#define WL_BIGDATA_11KV_BCNTABLE		0x00000800
#define WL_BIGDATA_11KV_BSSAAD			0x00001000
#define WL_BIGDATA_11KV_MAX			0x00002000

#define WL_BIGDATA_SUPPORT_11K		0x00000001
#define WL_BIGDATA_SUPPORT_11V		0x00000002

typedef struct {
	uint8	bitmap;
	uint8	octet_len;
	uint32	flag;
} bigdata_11kv_t;

bigdata_11kv_t bigdata_11k_info[] = {
	{DOT11_RRM_CAP_LINK, DOT11_RRM_CAP_LEN, WL_BIGDATA_11KV_LINK_MEA},
	{DOT11_RRM_CAP_NEIGHBOR_REPORT, DOT11_RRM_CAP_LEN, WL_BIGDATA_11KV_NBRREP},
	{DOT11_RRM_CAP_BCN_PASSIVE, DOT11_RRM_CAP_LEN, WL_BIGDATA_11KV_BCNPASSIVE},
	{DOT11_RRM_CAP_BCN_ACTIVE, DOT11_RRM_CAP_LEN, WL_BIGDATA_11KV_BCNACTIVE},
	{DOT11_RRM_CAP_BCN_TABLE, DOT11_RRM_CAP_LEN, WL_BIGDATA_11KV_BCNTABLE},
	{DOT11_RRM_CAP_BSSAAD, DOT11_RRM_CAP_LEN, WL_BIGDATA_11KV_BSSAAD},
};

bigdata_11kv_t bigdata_11v_info[] = {
	{DOT11_EXT_CAP_PROXY_ARP, DOT11_EXTCAP_LEN_PROXY_ARP, WL_BIGDATA_11KV_PROXYARP},
	{DOT11_EXT_CAP_TFS, DOT11_EXTCAP_LEN_TFS, WL_BIGDATA_11KV_TFS},
	{DOT11_EXT_CAP_WNM_SLEEP, DOT11_EXTCAP_LEN_WNM_SLEEP, WL_BIGDATA_11KV_SLEEP},
	{DOT11_EXT_CAP_TIMBC, DOT11_EXTCAP_LEN_TIMBC, WL_BIGDATA_11KV_TIMBC},
	{DOT11_EXT_CAP_BSSTRANS_MGMT, DOT11_EXTCAP_LEN_BSSTRANS, WL_BIGDATA_11KV_BSSTRANS},
	{DOT11_EXT_CAP_DMS, DOT11_EXTCAP_LEN_DMS, WL_BIGDATA_11KV_DMS}
};

static void
wl_get_11kv_info(u8 *ie, u32 ie_len, uint8 *support_11kv, uint32 *flag_11kv)
{
	bcm_tlv_t *ie_11kv = NULL;
	uint32 flag_11k = 0, flag_11v = 0;
	int i;

	/* parsing QBSS load ie */
	if ((bcm_parse_tlvs(ie, (u32)ie_len,
			DOT11_MNG_QBSS_LOAD_ID)) != NULL) {
		flag_11k |= WL_BIGDATA_11KV_QBSSLOAD;
	}

	/* parsing RM IE for 11k */
	if ((ie_11kv = bcm_parse_tlvs(ie, (u32)ie_len,
			DOT11_MNG_RRM_CAP_ID)) != NULL) {
		for (i = 0; i < ARRAYSIZE(bigdata_11k_info); i++) {
			if ((ie_11kv->len >= bigdata_11k_info[i].octet_len) &&
					isset(ie_11kv->data, bigdata_11k_info[i].bitmap)) {
				flag_11k |= bigdata_11k_info[i].flag;
			}
		}
	}

	/* parsing extended cap. IE for 11v */
	if ((ie_11kv = bcm_parse_tlvs(ie, (u32)ie_len,
			DOT11_MNG_EXT_CAP_ID)) != NULL) {
		for (i = 0; i < ARRAYSIZE(bigdata_11v_info); i++) {
			if ((ie_11kv->len >= bigdata_11v_info[i].octet_len) &&
					isset(ie_11kv->data, bigdata_11v_info[i].bitmap)) {
				flag_11v |= bigdata_11v_info[i].flag;
			}
		}
	}

	if (flag_11k > 0) {
		*support_11kv |= WL_BIGDATA_SUPPORT_11K;
	}

	if (flag_11v > 0) {
		*support_11kv |= WL_BIGDATA_SUPPORT_11V;
	}

	*flag_11kv = flag_11k | flag_11v;
}

int wl_get_bss_info(struct bcm_cfg80211 *cfg, struct net_device *dev, const u8 *mac)
{
	s32 err = 0;
	wl_bss_info_v109_1_t *bi;
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
	uint8 support_11kv = 0;
	uint32 flag_11kv = 0;	/* bit flags of 11kv big data */
	int cfg_bss_info_len = 0;

	/* get BSS information */

	strlcpy(cfg->bss_info, "x x x x x x x x x x x x x x x x x", sizeof(cfg->bss_info));

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

	bi = (wl_bss_info_v109_1_t *)(cfg->extra_buf + 4);
	channel = wf_chspec_ctlchan(bi->chanspec);
	freq = wl_channel_to_frequency(channel, CHSPEC_BAND(bi->chanspec));
	rate = 0;
	err = wldev_ioctl_get(dev, WLC_GET_RATE, &rate, sizeof(rate));
	if (err) {
		WL_ERR(("Could not get rate (%d)\n", err));
		snprintf(rate_str, sizeof(rate_str), "x"); /* Unknown */

	} else {
		rate = dtoh32(rate);
		snprintf(rate_str, sizeof(rate_str), "%d", (rate/2));
	}

	/* supported maximum rate */
	supported_rate = (bi->rateset.rates[bi->rateset.count - 1] & 0x7f) / 2;

	if (supported_rate < 12) {
		mode_80211 = BIGDATA_DOT11_11B_MODE; /* 11b maximum rate is 11Mbps. 11b mode */
	} else {
		/* It's not HT Capable case. */
		if (channel > 14) {
			mode_80211 = BIGDATA_DOT11_11A_MODE; /* 11a mode */
		} else {
			mode_80211 = BIGDATA_DOT11_11G_MODE; /* 11g mode */
		}
	}

	if (bi->n_cap) {
		/* check Rx MCS Map for HT */
		nss = 0;
		mode_80211 = BIGDATA_DOT11_11N_MODE;
		for (i = 0; i < MAX_STREAMS_SUPPORTED; i++) {
			int8 bitmap = DOT11_HT_MCS_RATE_MASK;
			if (i == MAX_STREAMS_SUPPORTED-1) {
				bitmap = DOT11_RATE_MASK;
			}
			if (bi->basic_mcs[i] & bitmap) {
				nss++;
			}
		}
	}

	if (bi->vht_cap) {
		nss = 0;
		mode_80211 = BIGDATA_DOT11_11AC_MODE;
		for (i = 1; i <= VHT_CAP_MCS_MAP_NSS_MAX; i++) {
			mcs_map = VHT_MCS_MAP_GET_MCS_PER_SS(i, dtoh16(bi->vht_rxmcsmap));
			if (mcs_map != VHT_CAP_MCS_MAP_NONE) {
				nss++;
			}
		}
	}

#if defined(WL11AX)
	if (bi->he_cap) {
		nss = 0;
		mode_80211 = BIGDATA_DOT11_11AX_MODE;
		for (i = 1; i <= HE_MCS_MAP_NSS_MAX; i++) {
			mcs_map = HE_MCS_NSS_GET_MCS(i, dtoh32(bi->he_rxmcsmap));
			if (mcs_map != HE_MCS_CODE_NONE) {
				nss++;
			}
		}
	}
#endif /* WL11AX */

	if (nss) {
		nss = nss - 1;
	}

	wiphy = bcmcfg_to_wiphy(cfg);
	bss = CFG80211_GET_BSS(wiphy, NULL, eabuf, bi->SSID, bi->SSID_len);
	if (!bss) {
		WL_ERR(("Could not find the AP\n"));
	} else {
		GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
#if defined(WL_CFG80211_P2P_DEV_IF)
		ie = (u8 *)bss->ies->data;
		ie_len = bss->ies->len;
#else
		ie = bss->information_elements;
		ie_len = bss->len_information_elements;
#endif /* WL_CFG80211_P2P_DEV_IF */
		GCC_DIAGNOSTIC_POP();
	}

	if (ie) {
		ie_mu_mimo_cap = 0;
		ie_11u_rel_num = 0;

		if (bi->vht_cap) {
			if ((vht_ie = bcm_parse_tlvs(ie, ie_len,
					DOT11_MNG_VHT_CAP_ID)) != NULL) {
					if (vht_ie->len >= VHT_CAP_IE_LEN) {
						ie_mu_mimo_cap = (vht_ie->data[2] & 0x08) >> 3;
					}
			}
		}

		if ((interworking_ie = bcm_parse_tlvs(ie, ie_len,
				DOT11_MNG_INTERWORKING_ID)) != NULL) {
			if ((tlv_ie = bcm_parse_tlvs(ie, ie_len, DOT11_MNG_VS_ID)) != NULL) {
				remained_len = ie_len;

				while (tlv_ie) {
					if (count > MAX_VNDR_IE_NUMBER)
						break;

					if (tlv_ie->id == DOT11_MNG_VS_ID) {
						vndrie = (vndr_ie_t *) tlv_ie;

						if (vndrie->len < (VNDR_IE_MIN_LEN + 1)) {
							WL_ERR(("wl_get_bss_info: invalid vndr ie."
								"length is too small %d\n",
								vndrie->len));
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

		/* get 11kv information from ie of current bss */
		wl_get_11kv_info(ie, ie_len, &support_11kv, &flag_11kv);
	}

	for (i = 0; i < bi->SSID_len; i++) {
		if (bi->SSID[i] == ' ') {
			bi->SSID[i] = '_';
		}
	}

	/* 0 : None, 1 : OKC, 2 : FT, 3 : CCKM */
	err = wldev_iovar_getint(dev, "wpa_auth", &val);
	if (unlikely(err)) {
		WL_ERR(("could not get wpa_auth (%d)\n", err));
		snprintf(akm_str, sizeof(akm_str), "x"); /* Unknown */
	} else {
		WL_ERR(("wpa_auth val %d \n", val));
			if (val & WPA2_AUTH_FT) {
				snprintf(akm_str, sizeof(akm_str), "2");
			} else if (val & (WPA_AUTH_UNSPECIFIED | WPA2_AUTH_UNSPECIFIED)) {
				snprintf(akm_str, sizeof(akm_str), "1");
			} else {
				snprintf(akm_str, sizeof(akm_str), "0");
			}
	}

	if (cfg->roam_offload) {
		snprintf(roam_count_str, sizeof(roam_count_str), "x"); /* Unknown */
	} else {
		snprintf(roam_count_str, sizeof(roam_count_str), "%d", cfg->roam_count);
	}
	cfg->roam_count = 0;

	WL_ERR(("BSSID:" MACDBG " SSID %s \n", MAC2STRDBG(eabuf), "*****"));
	WL_ERR(("freq:%d, BW:%s, RSSI:%d dBm, Rate:%d Mbps, 11mode:%d, stream:%d,"
				"MU-MIMO:%d, Passpoint:%d, SNR:%d, Noise:%d, \n"
				"akm:%s, roam:%s, 11kv:%d/%d \n",
				freq, wf_chspec_to_bw_str(bi->chanspec),
				dtoh32(bi->RSSI), (rate / 2), mode_80211, nss,
				ie_mu_mimo_cap, ie_11u_rel_num, bi->SNR, bi->phy_noise,
				akm_str, roam_count_str, support_11kv, flag_11kv));

	if (ie) {
		snprintf(cfg->bss_info, GET_BSS_INFO_LEN,
				MACOUI" %d %s %d %s %d %d %d %d %d %d %s %s %d %d",
				MACOUI2STR(eabuf), freq, wf_chspec_to_bw_str(bi->chanspec),
				dtoh32(bi->RSSI), rate_str, mode_80211, nss, ie_mu_mimo_cap,
				ie_11u_rel_num, bi->SNR, bi->phy_noise, akm_str, roam_count_str,
				support_11kv, flag_11kv);
	} else {
		/* ie_mu_mimo_cap and ie_11u_rel_num is unknow. */
		snprintf(cfg->bss_info, GET_BSS_INFO_LEN,
				MACOUI" %d %s %d %s %d %d x x %d %d %s %s x x",
				MACOUI2STR(eabuf), freq, wf_chspec_to_bw_str(bi->chanspec),
				dtoh32(bi->RSSI), rate_str, mode_80211, nss, bi->SNR,
				bi->phy_noise, akm_str, roam_count_str);
	}

	cfg_bss_info_len = strlen(cfg->bss_info);
	if (GET_BSS_INFO_LEN > cfg_bss_info_len) {
		uint16 full_cnt = 0, partial_cnt = 0;
		bool cnt_valid = FALSE;

#if defined(DHD_PUB_ROAM_EVT)
		wl_roam_stats_v1_t *roam_elem =
			(wl_roam_stats_v1_t *)dhd_get_roam_evt((dhd_pub_t *)cfg->pub);

		if (roam_elem && roam_elem->version == WL_ROAM_STATS_VER_1) {
			wl_roam_stats_v1_t *roam_elem_v1;
			roam_elem_v1 = (wl_roam_stats_v1_t *)(uintptr_t)roam_elem;

			cnt_valid = TRUE;
			full_cnt = roam_elem_v1->full_roam_scan_cnt;
			partial_cnt = roam_elem_v1->partial_roam_scan_cnt;
		}
#endif /* DHD_PUB_ROAM_EVT */
		if (cnt_valid) {
			WL_ERR(("GET_BSS: full roam scan count:%d partial roam scan count:%d\n",
				full_cnt, partial_cnt));
			snprintf(&cfg->bss_info[cfg_bss_info_len],
				GET_BSS_INFO_LEN - cfg_bss_info_len, " %d %d",
				full_cnt, partial_cnt);
		} else {
			WL_ERR(("GET_BSS: roam scan count invalid\n"));
			snprintf(&cfg->bss_info[cfg_bss_info_len],
				GET_BSS_INFO_LEN - cfg_bss_info_len, " x x");
		}
	} else {
		WL_ERR(("Buffer to short to save roam info\n"));
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

	if (total_len < GET_BSS_INFO_LEN) {
		WL_ERR(("wl_cfg80211_get_bss_info: Buffer insuffient %d\n", total_len));
		return -1;
	}

	bzero(cmd, total_len);
	memcpy(cmd, cfg->bss_info, GET_BSS_INFO_LEN);

	WL_ERR_KERN(("cmd: %s \n", cmd));

	return GET_BSS_INFO_LEN;
}
#endif /* DHD_ENABLE_BIGDATA_LOGGING */

void wl_cfg80211_disassoc(struct net_device *ndev, uint32 reason)
{
	scb_val_t scbval;
	s32 err;
#ifdef BCMDONGLEHOST
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);

	BCM_REFERENCE(cfg);
	BCM_REFERENCE(dhdp);
	DHD_STATLOG_CTRL(dhdp, ST(DISASSOC_INT_START),
		dhd_net2idx(dhdp->info, ndev), WLAN_REASON_DEAUTH_LEAVING);
#endif /* BCMDONGLEHOST */

	memset_s(&scbval, sizeof(scb_val_t), 0x0, sizeof(scb_val_t));
	scbval.val = htod32(reason);
	err = wldev_ioctl_set(ndev, WLC_DISASSOC, &scbval, sizeof(scb_val_t));
	if (err < 0) {
		WL_ERR(("WLC_DISASSOC error %d\n", err));
	} else {
		WL_INFORM_MEM(("wl disassoc. reason:%d\n", reason));
	}
}
void wl_cfg80211_del_all_sta(struct net_device *ndev, uint32 reason)
{
	struct net_device *dev;
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	scb_val_t scb_val;
	int err;
	char mac_buf[MAX_NUM_OF_ASSOCIATED_DEV *
		sizeof(struct ether_addr) + sizeof(uint)] = {0};
	struct maclist *assoc_maclist = (struct maclist *)mac_buf;
	int num_associated = 0;

	dev = ndev_to_wlc_ndev(ndev, cfg);

	if (p2p_is_on(cfg)) {
		/* Suspend P2P discovery search-listen to prevent it from changing the
		 * channel.
		 */
		if ((wl_cfgp2p_discover_enable_search(cfg, false)) < 0) {
			WL_ERR(("Can not disable discovery mode\n"));
			return;
		}
	}

	assoc_maclist->count = MAX_NUM_OF_ASSOCIATED_DEV;
	err = wldev_ioctl_get(ndev, WLC_GET_ASSOCLIST,
		assoc_maclist, sizeof(mac_buf));
	if (err < 0)
		WL_ERR(("WLC_GET_ASSOCLIST error %d\n", err));
	else
		num_associated = assoc_maclist->count;

	memset(scb_val.ea.octet, 0xff, ETHER_ADDR_LEN);
	scb_val.val = DOT11_RC_DEAUTH_LEAVING;
	scb_val.val = htod32(reason);
	err = wldev_ioctl_set(dev, WLC_SCB_DEAUTHENTICATE_FOR_REASON, &scb_val,
			sizeof(scb_val_t));
	if (err < 0) {
		WL_ERR(("WLC_SCB_DEAUTHENTICATE_FOR_REASON err %d\n", err));
	}

	/* WAR Wait for the deauth event to come, supplicant will do the
	 * delete iface immediately and we will have problem in sending
	 * deauth frame if we delete the bss in firmware
	 * But we do not need additional delays for this WAR
	 * during P2P connection.
	 *
	 * Supplicant call this function with BCAST after doing
	 * wl_cfg80211_del_station() all GC stations with each addr.
	 * So, 400 ms delay can be called only once when GO disconnect all GC
	*/
	if (num_associated > 0)
		wl_delay(400);

	return;
}
/* API to handle the Deauth from the AP.
* For now we are deleting the PMKID cache in DHD/FW
* in case of current connection is using SAE authnetication
*/
static s32
wl_cfg80211_handle_deauth_ind(struct bcm_cfg80211 *cfg, wl_assoc_status_t *as)
{
	int err = BCME_OK;
#ifdef WL_SAE
	struct net_device *ndev = as->ndev;
	const wl_event_msg_t *e = as->event_msg;
	uint8 bssid[ETHER_ADDR_LEN];
	struct cfg80211_pmksa pmksa;
	s32 val = 0;
	struct wlc_ssid *curssid;
	pmkid_list_v3_t *spmk_list = &cfg->spmk_info_list->pmkids;
	pmkid_v3_t *t_pmkid = NULL;
	int idx;
	bool bFound = FALSE;
#endif /* WL_SAE */

	if (as->reason > WLC_E_DEAUTH_MAX_REASON) {
		/* Specific AP send deauth by invalid reason.
		 * If reason over 0x8XXX, framework trigger recovery.
		 * Framework check HANG_REASON_MASK(0x8000) with reason.
		 */
		WL_ERR(("Event %d original reason is %d, "
			"changed 0xFF\n", as->event_type, as->reason));
		as->reason = WLC_E_DEAUTH_MAX_REASON;
	}
#ifdef WL_SAE
	err = wldev_iovar_getint(ndev, "wpa_auth", &val);
	if (unlikely(err)) {
		WL_ERR(("could not get wpa_auth (%d)\n", err));
		goto done;
	}
	if (val == WPA3_AUTH_SAE_PSK) {
		(void)memcpy_s(bssid, ETHER_ADDR_LEN,
		(const uint8*)&e->addr, ETHER_ADDR_LEN);
		memset_s(&pmksa, sizeof(pmksa), 0, sizeof(pmksa));
		pmksa.bssid = bssid;
		WL_INFORM_MEM(("Deleting the PMKSA for SAE AP "MACDBG,
			MAC2STRDBG(e->addr.octet)));
		wl_cfg80211_del_pmksa(cfg->wdev->wiphy, ndev, &pmksa);
		curssid = wl_read_prof(cfg, ndev, WL_PROF_SSID);
		for (idx = 0; idx < spmk_list->count; idx++) {
			t_pmkid = &spmk_list->pmkid[idx];
			if (curssid->SSID_len == t_pmkid->ssid_len &&
				!memcmp(curssid->SSID, t_pmkid->ssid, curssid->SSID_len)) {
				bFound = TRUE;
				break;
			}
		}
		if (!bFound) {
			goto done;
		}
		for (; idx < spmk_list->count - 1; idx++) {
			memcpy_s(&spmk_list->pmkid[idx], sizeof(pmkid_v3_t),
				&spmk_list->pmkid[idx + 1], sizeof(pmkid_v3_t));
		}
		spmk_list->count--;
	}
done:
#endif /* WL_SAE */
	return err;
}

static void
wl_cache_assoc_resp_ies(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *e, void *data)
{
	struct wl_connect_info *conn_info = wl_to_conn(cfg);
	u32 datalen = ntoh32(e->datalen);
	u32 event_type = ntoh32(e->event_type);

	if (data && datalen <= sizeof(conn_info->resp_ie)) {
		conn_info->resp_ie_len = datalen;
		WL_DBG((" assoc resp IES len = %d\n", conn_info->resp_ie_len));
		bzero(conn_info->resp_ie, sizeof(conn_info->resp_ie));
		(void)memcpy_s(conn_info->resp_ie, sizeof(conn_info->resp_ie),
			data, datalen);

		WL_INFORM_MEM(("[%s] cached assoc resp ies"
			"event %d reason=%d ie_len=%d from " MACDBG "\n",
			ndev->name,	event_type, ntoh32(e->reason), datalen,
			MAC2STRDBG((const u8*)(&e->addr))));
	}
}

char *
wl_get_link_action_str(u16 link_action)
{
	switch (link_action) {
	case WL_LINK_NONE:
		return "LINK_NONE";
	case WL_LINK_ASSOC_FAIL:
		return "ASSOC_FAIL";
	case WL_LINK_ASSOC_DONE:
		return "ASSOC_DONE";
	case WL_LINK_DOWN:
		return "LINK_DOWN";
	case WL_LINK_ROAM_DONE:
		return "ROAM_DONE";
	case WL_LINK_FORCE_DEAUTH:
		return "SEND_DEAUTH";
	default:
		return "UNKOWN_STATE";
	}
}

char *
wl_get_assoc_state_str(u16 assoc_state)
{
	switch (assoc_state) {
	case WL_STATE_ASSOC_IDLE:
		return "ASSOC_IDLE";
	case WL_STATE_ASSOCIATING:
		return "ASSOCIATING";
	case WL_STATE_ASSOCIATED:
		return "ASSOCIATED";
	default:
		return "UNKOWN_STATE";
	}
}

static u32
wl_set_link_action(wl_assoc_state_t assoc_state, bool link_up)
{
	wl_link_action_t action = WL_LINK_NONE;

	switch (assoc_state) {
		case WL_STATE_ASSOCIATING:
			if (link_up) {
				action = WL_LINK_ASSOC_DONE;
			} else {
				action = WL_LINK_ASSOC_FAIL;
			}
			break;
		case WL_STATE_ASSOCIATED:
			if (link_up) {
				action = WL_LINK_ROAM_DONE;
			} else {
				action = WL_LINK_DOWN;
			}
			break;
		case WL_STATE_ASSOC_IDLE:
			if (link_up) {
				/* link up while cfg80211 state is not in
				 * 'ASSOCIATING/ASSOCIATED. Sync up the fw
				 *  by disconnecting.
				 */
				WL_ERR(("Unexpected link up\n"));
				action = WL_LINK_FORCE_DEAUTH;
			}
			break;
		default:
			WL_ERR(("unknown state:%d\n", assoc_state));
			action = WL_LINK_NONE;
	}

	return action;
}

static void
wl_cfg8021_unlink_bss(struct bcm_cfg80211 *cfg, struct net_device *ndev, u8 *bssid)
{
	struct cfg80211_bss *bss;
	wlc_ssid_t *ssid;
	struct wireless_dev *wdev = ndev->ieee80211_ptr;

	ssid = (struct wlc_ssid *)wl_read_prof(cfg, ndev, WL_PROF_SSID);
	if (ssid && bssid) {
		bss = CFG80211_GET_BSS(wdev->wiphy, NULL, bssid, ssid->SSID, ssid->SSID_len);
		if (bss) {
			cfg80211_unlink_bss(wdev->wiphy, bss);
			CFG80211_PUT_BSS(wdev->wiphy, bss);
			WL_INFORM_MEM(("bss unlinked"));
		}
	}
}

static s32
wl_post_linkdown_ops(struct bcm_cfg80211 *cfg,
	wl_assoc_status_t *as, struct net_device *ndev)
{
	s32 ret = BCME_OK;
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);

	/* Common Code for connect failure & link down */
	BCM_REFERENCE(dhdp);

	WL_INFORM_MEM(("link down. connection state bit status: [%u:%u:%u:%u]\n",
		wl_get_drv_status(cfg, CONNECTING, ndev),
		wl_get_drv_status(cfg, CONNECTED, ndev),
		wl_get_drv_status(cfg, DISCONNECTING, ndev),
		wl_get_drv_status(cfg, NESTED_CONNECT, ndev)));

	/* clear timestamps on disconnect */
	CLR_TS(cfg, conn_start);
	CLR_TS(cfg, conn_cmplt);
	CLR_TS(cfg, authorize_start);
	CLR_TS(cfg, authorize_cmplt);

	wl_link_down(cfg);
	wl_clr_drv_status(cfg, AUTHORIZED, ndev);
	wl_clr_drv_status(cfg, CONNECTED, ndev);
	wl_clr_drv_status(cfg, DISCONNECTING, ndev);

#ifdef DBG_PKT_MON
	if (ndev == bcmcfg_to_prmry_ndev(cfg)) {
		/* Stop packet monitor */
		DHD_DBG_PKT_MON_STOP(dhdp);
	}
#endif /* DHD_PKT_MON */

	/* Flush preserve logs */
	wl_flush_fw_log_buffer(bcmcfg_to_prmry_ndev(cfg),
			FW_LOGSET_MASK_ALL);

#ifdef WL_GET_RCC
	wl_android_get_roam_scan_chanlist(cfg);
#endif /* WL_GET_RCC */

#ifdef WES_SUPPORT
	if (cfg->ncho_mode) {
		/* Turn off NCHO mode */
		wl_android_set_ncho_mode(ndev, FALSE);
	}
#endif /* WES_SUPPORT */

#ifdef WLTDLS
	wl_cfg80211_tdls_config(cfg, TDLS_STATE_DISCONNECT, false);
#endif /* WLTDLS */

	/* clear RSSI monitor, framework will set new cfg */
#ifdef RSSI_MONITOR_SUPPORT
	dhd_dev_set_rssi_monitor_cfg(bcmcfg_to_prmry_ndev(cfg),
		FALSE, 0, 0);
#endif /* RSSI_MONITOR_SUPPORT */

#ifdef WBTEXT
	wl_cfg80211_wbtext_reset_conf(cfg, as->ndev);
#endif /* WBTEXT */

#ifdef P2PLISTEN_AP_SAMECHN
	if (as->ndev == bcmcfg_to_prmry_ndev(cfg)) {
		wl_cfg80211_set_p2p_resp_ap_chn(as->ndev, 0);
		cfg->p2p_resp_apchn_status = false;
		WL_DBG(("p2p_resp_apchn_status Turn OFF \n"));
	}
#endif /* P2PLISTEN_AP_SAMECHN */

#ifdef WL_NAN
	if (wl_cfgnan_is_enabled(cfg)) {
		wl_cfgnan_get_stats(cfg);
	}
#endif /* WL_NAN */

	return ret;
}

static s32
wl_handle_assoc_fail(struct bcm_cfg80211 *cfg, wl_assoc_status_t *as, bool completed)
{
	s32 ret = BCME_OK;
	struct net_device *ndev = as->ndev;
	u8 *connect_req_bssid = wl_read_prof(cfg, ndev, WL_PROF_LATEST_BSSID);
#ifdef BCMDONGLEHOST
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
#endif /* BCMDONGLEHOST */

#ifdef BCMDONGLEHOST
	BCM_REFERENCE(dhdp);
#endif /* BCMDONGLEHOST */
	WL_MSG(ndev->name, "assoc fail Reason: %s from %pM\n",
		bcmevent_get_name(as->event_type), as->addr);

	if (connect_req_bssid && !ETHER_ISNULLADDR(as->addr) &&
		memcmp(&as->addr, connect_req_bssid, ETH_ALEN) != 0) {
		WL_ERR(("Event:%d Wrong bssid:" MACDBG "\n", as->event_type, MAC2STRDBG(as->addr)));
		return BCME_OK;
	}
#ifdef WL_EXT_IAPSTA
	{
		wl_event_msg_t emsg;
		memcpy(&emsg, as->event_msg, sizeof(wl_event_msg_t));
		ret = wl_ext_in4way_sync(ndev, STA_REASSOC_RETRY,
			WL_EXT_STATUS_RECONNECT, &emsg);
		if (ret)
			return 0;
	}
	wl_ext_iapsta_enable_master_if(ndev, FALSE);
#endif

	/* A connect request in Connected/Connecting will have the
	 * NESTED_CONNECT state set.
	 */
	if (wl_get_drv_status(cfg, NESTED_CONNECT, ndev) &&
	   wl_get_drv_status(cfg, DISCONNECTING, ndev)) {
		wl_clr_drv_status(cfg, DISCONNECTING, ndev);
		wl_clr_drv_status(cfg, NESTED_CONNECT, ndev);
		WL_INFORM_MEM(("Disconnect from nested connect context\n"));
#if defined(BSSCACHE)
		wl_delete_disconnected_bss_cache(&cfg->g_bss_cache_ctrl,
			(u8*)(&as->event_msg->addr));
#endif
#ifdef WL_EXT_IAPSTA
		wl_ext_in4way_sync(ndev, STA_NO_BTC_IN4WAY, WL_EXT_STATUS_DISCONNECTED, NULL);
#endif
		return BCME_OK;
	}

#ifdef WL_WPS_SYNC
	if (wl_wps_session_update(ndev,
			WPS_STATE_CONNECT_FAIL, as->addr) == BCME_UNSUPPORTED) {
		/* Skip the event handling */
		return BCME_OK;
	}
#endif /* WL_WPS_SYNC */

#ifdef BCMDONGLEHOST
	DHD_STATLOG_CTRL(dhdp, ST(DISASSOC_INT_START),
		dhd_net2idx(dhdp->info, ndev), 0);
#endif /* BCMDONGLEHOST */

#if defined(CONFIG_TIZEN)
	net_stat_tizen_update_wifi(ndev, WIFISTAT_CONNECTION_FAIL);
#endif /* CONFIG_TIZEN */

	/* if link down, bsscfg is disabled */
	if (ndev != bcmcfg_to_prmry_ndev(cfg)) {
		complete(&cfg->iface_disable);
	}

	/* Report connect result to upper layer */
	ret = wl_bss_connect_done(cfg, ndev, as->event_msg, as->data, false);
	if (unlikely(ret)) {
		WL_ERR(("connect result reporting failed.\n"));
	}

	/* Issue WLC_DISASSOC to prevent FW roam attempts. Do not issue
	 * WLC_DISASSOC again if the linkdown  is generated due to local
	 * disassoc, to avoid connect-disconnect loop.
	 */
	if (!((as->event_type == WLC_E_LINK) && (as->reason == WLC_E_LINK_DISASSOC))) {
		wl_cfg80211_disassoc(ndev, WLAN_REASON_DEAUTH_LEAVING);
	}

	/* Common handler for assoc fail/link down */
	wl_post_linkdown_ops(cfg, as, as->ndev);

	return ret;
}

s32
wl_get_connected_bssid(struct bcm_cfg80211 *cfg, struct net_device *ndev, u8 *mac_addr)
{
	u8 bssid_dongle[ETH_ALEN] = {0};
	u8 *curbssid = wl_read_prof(cfg, ndev, WL_PROF_BSSID);

	if (!mac_addr) {
		return -EINVAL;
	}

	/* roam offload does not sync BSSID always, get it from dongle */
	if (cfg->roam_offload) {
		if (wldev_ioctl_get(ndev, WLC_GET_BSSID, bssid_dongle,
				sizeof(bssid_dongle)) == BCME_OK) {
			/* if not roam case, it would return null bssid */
			if (!ETHER_ISNULLADDR(bssid_dongle)) {
				curbssid = (u8 *)&bssid_dongle;
			}
		}
	}

	if (curbssid) {
		(void)memcpy_s(mac_addr, ETH_ALEN, curbssid, ETH_ALEN);
	}
	return BCME_OK;
}

#ifdef WBTEXT
static void
wl_cfg80211_wbtext_reset_conf(struct bcm_cfg80211 *cfg, struct net_device *ndev)
{
#ifdef BCMDONGLEHOST
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
	s32 err;

	/* when STA was disconnected, clear join pref and set wbtext */
	if (ndev->ieee80211_ptr->iftype == NL80211_IFTYPE_STATION &&
		dhdp->wbtext_policy
		== WL_BSSTRANS_POLICY_PRODUCT_WBTEXT) {
		char smbuf[WLC_IOCTL_SMLEN];
		if ((err = wldev_iovar_setbuf(ndev, "join_pref",
			NULL, 0, smbuf, sizeof(smbuf), NULL)) == BCME_OK) {
			if ((err = wldev_iovar_setint(ndev, "wnm_bsstrans_resp",
				dhdp->wbtext_policy)) == BCME_OK) {
				wl_cfg80211_wbtext_set_default(ndev);
			} else {
				WL_ERR(("Failed to set wbtext = %d\n", err));
			}
		} else {
			WL_ERR(("Failed to clear join pref = %d\n", err));
		}
		wl_cfg80211_wbtext_clear_bssid_list(cfg);
	} else {
		WL_ERR(("wbtext not applicable\n"));
	}
#endif /* BCMDONGLEHOST */
}
#endif /* WBTEXT */

static s32
wl_handle_link_down(struct bcm_cfg80211 *cfg, wl_assoc_status_t *as)
{
	s32 ret = BCME_OK;
#if defined(BCMDONGLEHOST)
	dhd_pub_t *dhdp = (dhd_pub_t *)cfg->pub;
#endif /* BCMDONGLEHOST */
	struct net_device *ndev = as->ndev;
	u32 datalen = as->data_len;
	u32 event = as->event_type;
	u8 *data = as->data;
	u8 *ie_ptr = NULL;
	u16 ie_len = 0;
	bool loc_gen = 0;
	u16 reason = as->reason;

#ifdef BCMDONGLEHOST
	BCM_REFERENCE(dhdp);
#endif /* BCMDONGLEHOST */
	WL_MSG(ndev->name, "Link down: %s(%d), reason %d from %pM\n",
		bcmevent_get_name(as->event_type), event, reason, as->addr);
	if ((BCME_OK != wl_get_connected_bssid(cfg, ndev, as->curbssid))) {
		WL_ERR(("bssid not found\n"));
		return -1;
	}
#ifdef WL_EXT_IAPSTA
	{
		wl_event_msg_t emsg;
		memcpy(&emsg, as->event_msg, sizeof(wl_event_msg_t));
		ret = wl_ext_in4way_sync(ndev, STA_REASSOC_RETRY,
			WL_EXT_STATUS_RECONNECT, &emsg);
		if (ret)
			return 0;
	}
#endif

	if (memcmp(as->curbssid, as->addr, ETHER_ADDR_LEN) != 0) {
		WL_ERR(("BSSID of event is not the connected BSSID"
			"(ignore it) cur: " MACDBG
			" event: " MACDBG"\n",
			MAC2STRDBG(as->curbssid),
			MAC2STRDBG((const u8*)(&as->addr))));
		return 0;
	}

	/* A connect request in Connected/Connecting will have the
	 * NESTED_CONNECT state set.
	 */
	if (wl_get_drv_status(cfg, NESTED_CONNECT, ndev) &&
	   wl_get_drv_status(cfg, DISCONNECTING, ndev)) {
		wl_clr_drv_status(cfg, DISCONNECTING, ndev);
		wl_clr_drv_status(cfg, NESTED_CONNECT, ndev);
		WL_INFORM_MEM(("Disconnect from nested connect context\n"));
#if defined(BSSCACHE)
		wl_delete_disconnected_bss_cache(&cfg->g_bss_cache_ctrl,
			(u8*)(&as->event_msg->addr));
#endif
#ifdef WL_EXT_IAPSTA
		wl_ext_in4way_sync(ndev, STA_NO_BTC_IN4WAY, WL_EXT_STATUS_DISCONNECTED, NULL);
		wl_ext_iapsta_restart_master(ndev);
#endif
		return 0;
	}

#ifdef WL_WPS_SYNC
	if (wl_wps_session_update(ndev,
		WPS_STATE_LINKDOWN, as->addr) == BCME_UNSUPPORTED) {
		/* Skip event handling */
		return 0;
	}
#endif /* WL_WPS_SYNC */

	if (!wl_get_drv_status(cfg, DISCONNECTING, ndev)) {
#ifdef BCMDONGLEHOST
		DHD_STATLOG_CTRL(dhdp, ST(DISASSOC_INT_START),
			dhd_net2idx(dhdp->info, ndev),
			WLAN_REASON_DEAUTH_LEAVING);
#endif /* BCMDONGLEHOST */
		wl_cfg80211_disassoc(ndev, WLAN_REASON_DEAUTH_LEAVING);
	}

#ifdef BCMDONGLEHOST
	DHD_STATLOG_CTRL(dhdp, ST(DISASSOC_DONE),
		dhd_net2idx(dhdp->info, ndev), as->reason);
#endif /* BCMDONGLEHOST */

#if defined(CONFIG_TIZEN)
	net_stat_tizen_update_wifi(ndev, WIFISTAT_CONNECTION_FAIL);
#endif /* CONFIG_TIZEN */

#ifdef DHD_LOSSLESS_ROAMING
	wl_del_roam_timeout(cfg);
#endif /* DHD_LOSSLESS_ROAMING */
	/*
	* FW sends body and body len as a part of deauth
	* and disassoc events (WLC_E_DISASSOC_IND, WLC_E_DEAUTH_IND)
	* The VIEs sits after reason code in the body. Reason code is
	* 2 bytes long.
	*/
	WL_DBG(("recv disconnect ies ie_len = %d\n", ie_len));
	if (event == WLC_E_DISASSOC_IND || event == WLC_E_DEAUTH_IND) {
		if ((datalen > DOT11_DISCONNECT_RC) &&
			datalen < (VNDR_IE_MAX_LEN + DOT11_DISCONNECT_RC) &&
			data) {
			ie_ptr = (uchar*)data + DOT11_DISCONNECT_RC;
			ie_len = datalen - DOT11_DISCONNECT_RC;
		}
	}
#ifdef WL_ANALYTICS
	else if ((event == WLC_E_LINK) &&
			(reason == WLC_E_LINK_BCN_LOSS)) {
		if (ndev == bcmcfg_to_prmry_ndev(cfg)) {
			if (wl_vndr_ies_find_vendor_oui(cfg, ndev,
				CISCO_AIRONET_OUI)) {
				WL_INFORM_MEM(("Analytics Beacon loss\n"));
				ie_ptr = (uchar*)disco_bcnloss_vsie;
				ie_len = sizeof(disco_bcnloss_vsie);
			}
		}
	}
#endif /* WL_ANALYTICS */

#ifdef BCMDONGLEHOST
#if defined(DHDTCPSYNC_FLOOD_BLK) && defined(CUSTOMER_TCPSYNC_FLOOD_DIS_RC)
	{
		u32 ifidx = ntoh32(as->event_msg->ifidx);
		struct dhd_if *ifp = dhd_get_ifp(dhdp, ifidx);
		if (ifp && ifp->disconnect_tsync_flood) {
			reason = CUSTOMER_TCPSYNC_FLOOD_DIS_RC;
		}
	}
#endif /* DHDTCPSYNC_FLOOD_BLK && CUSTOMER_TCPSYNC_FLOOD_DIS_RC */
#endif /* BCMDONGLEHOST */

#ifdef DHD_ENABLE_BIGDATA_LOGGING
	wl_get_bss_info(cfg, ndev, as->addr);
#endif /* DHD_ENABLE_BIGDATA_LOGGING */

	/* unlink from bss list - to force fresh add from next scan/connect */
	wl_cfg8021_unlink_bss(cfg, ndev, as->addr);

	/* clear profile before reporting link down */
	wl_init_prof(cfg, ndev);
#if defined(BSSCACHE)
	wl_delete_disconnected_bss_cache(&cfg->g_bss_cache_ctrl,
		(u8*)(&as->event_msg->addr));
#endif
#ifdef WL_EXT_IAPSTA
	{
		wl_event_msg_t emsg;
		memcpy(&emsg, as->event_msg, sizeof(wl_event_msg_t));
		wl_ext_in4way_sync(ndev, STA_NO_BTC_IN4WAY, WL_EXT_STATUS_DISCONNECTED, &emsg);
		wl_ext_iapsta_restart_master(ndev);
	}
#endif

	CFG80211_DISCONNECTED(ndev, reason, ie_ptr, ie_len,
		loc_gen, GFP_KERNEL);
	WL_MSG(ndev->name, "Disconnect event sent to upper layer"
		"event:%d e->reason=%d reason=%d ie_len=%d "
		"from " MACDBG "\n",
		event, ntoh32(as->reason), reason, ie_len,
		MAC2STRDBG((const u8*)(&as->addr)));

	/* clear connected state */
	wl_clr_drv_status(cfg, CONNECTED, ndev);

	/* Common handler for assoc fail/link down */
	wl_post_linkdown_ops(cfg, as, as->ndev);

	return ret;
}

static s32
wl_handle_assoc_done(struct bcm_cfg80211 *cfg, wl_assoc_status_t *as)
{
	s32 ret = BCME_OK;
	bool act = true;
	struct net_device *ndev = as->ndev;

	wl_update_prof(cfg, ndev, as->event_msg, &act, WL_PROF_ACT);

	if (wl_get_drv_status(cfg, CONNECTED, ndev)) {
		u8 *curbssid = wl_read_prof(cfg, ndev, WL_PROF_BSSID);
		u8 *conn_req_bssid = wl_read_prof(cfg, ndev, WL_PROF_LATEST_BSSID);
		if (memcmp(curbssid, conn_req_bssid, ETHER_ADDR_LEN) == 0) {
			/* connected bssid and outstanding connect req bssid are same */
			WL_INFORM_MEM((" Connected event of connected device "
				"e=%d s=%d, ignore it\n",
				as->event_type, as->status));
			return ret;
		}
	}

	/* Report connect result to cfg80211 layer */
	ret = wl_bss_connect_done(cfg, ndev, as->event_msg, as->data, true);
	if (unlikely(ret)) {
		WL_ERR(("Connect report failed!\n"));
		/* Sync with fw */
		wl_cfg80211_disassoc(ndev, WLAN_REASON_DEAUTH_LEAVING);
		ret = BCME_ERROR;
		goto exit;
	}

	WL_DBG(("joined in BSS network \"%s\"\n",
		((struct wlc_ssid *)wl_read_prof(cfg, ndev, WL_PROF_SSID))->SSID));
	wl_update_prof(cfg, ndev, NULL, (const void *)&as->addr, WL_PROF_BSSID);

	wl_link_up(cfg);

	/* Handle feature specific handling on linkup event */
	ret = wl_post_linkup_ops(cfg, as);

exit:
	return ret;
}

static s32
wl_handle_roam_done(struct bcm_cfg80211 *cfg, wl_assoc_status_t *as)
{
	s32 ret = BCME_OK;
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);

	if (cfg->roam_offload) {
		/* roam offload enabled, avoid roam events to wake up host */
		WL_ERR(("roamoffload enabled. Ignore event\n"));
		return ret;
	}

#ifdef DHD_EVENT_LOG_FILTER
	dhd_event_log_filter_notify_connect_done(dhdp,
			as->addr, true);
#endif /* DHD_EVENT_LOG_FILTER */

#ifdef DHD_LOSSLESS_ROAMING
	{
		struct wl_security *sec = wl_read_prof(cfg,
				as->ndev, WL_PROF_SEC);
		if (!IS_AKM_SUITE_FT(sec)) {
			wl_bss_roaming_done(cfg, as->ndev, as->event_msg, as->data);
		}
	}
#endif /* DHD_LOSSLESS_ROAMING */

	/* Arm pkt logging timer */
	dhd_dump_mod_pkt_timer(dhdp, PKT_CNT_RSN_ROAM);

	return ret;
}

static s32
wl_handle_sta_link_action(struct bcm_cfg80211 *cfg, wl_assoc_status_t *as)
{
	s32 ret = BCME_OK;

	WL_INFORM_MEM(("assoc_state:%s link action:%s\n",
		wl_get_assoc_state_str(as->assoc_state),
		wl_get_link_action_str(as->link_action)));

	switch (as->link_action) {
		case WL_LINK_ASSOC_DONE:
			ret = wl_handle_assoc_done(cfg, as);
			break;
		case WL_LINK_ASSOC_FAIL:
			ret = wl_handle_assoc_fail(cfg, as, FALSE);
			break;
		case WL_LINK_DOWN:
			ret = wl_handle_link_down(cfg, as);
			break;
		case WL_LINK_ROAM_DONE:
			ret = wl_handle_roam_done(cfg, as);
			break;
		case WL_LINK_FORCE_DEAUTH:
			wl_cfg80211_disassoc(as->ndev, WLAN_REASON_DEAUTH_LEAVING);
			break;
		default:
			WL_ERR(("Unsupported link state:%d\n", as->link_action));
			ret = -ENOTSUPP;
	}

	if (unlikely(ret)) {
		WL_ERR(("link_action:%d handling failed\n", as->link_action));
	}

	return ret;
}

static s32
wl_handle_assoc_events(struct bcm_cfg80211 *cfg,
	struct wireless_dev *wdev, const wl_event_msg_t *e,
	void *data, wl_assoc_state_t assoc_state)
{
	s32 err = BCME_OK;
	wl_assoc_status_t as;

	if (!wdev || !e) {
		WL_ERR(("wrong input\n"));
		return -EINVAL;
	}

	bzero(&as, sizeof(wl_assoc_status_t));
	as.event_type = ntoh32(e->event_type);
	as.status = ntoh32(e->status);
	as.reason = ntoh32(e->reason);
	as.flags = ntoh16(e->flags);
	as.ndev = wdev->netdev;
	as.data = data;
	as.data_len = ntoh32(e->datalen);
	as.event_msg = e;
	(void)memcpy_s(as.addr, ETH_ALEN, e->addr.octet, ETH_ALEN);

	WL_INFORM_MEM(("[%s] Mode BSS. assoc_state:%d event:%d "
		"status:%d reason:%d e_idx:%d " MACDBG "\n",
		as.ndev->name, as.assoc_state, as.event_type, as.status, as.reason,
		cfg->eidx.in_progress, MAC2STRDBG((const u8*)(&e->addr))));

	/* Handle FW events */
	switch (as.event_type) {
		case WLC_E_AUTH:
			if (ntoh32(e->auth_type) == DOT11_SAE) {
#ifdef WL_SAE
				wl_bss_handle_sae_auth(cfg, as.ndev, e, data);
#endif /* WL_SAE */

#ifdef WL_CLIENT_SAE
				wl_handle_auth_event(cfg, as.ndev, e, data);
#endif /* WL_CLIENT_SAE */
			}

			/* Intentional fall through */
		case WLC_E_ASSOC:
			wl_get_auth_assoc_status(cfg, as.ndev, e, data);
			break;
		case WLC_E_ASSOC_RESP_IE:
			if (as.status != WLC_E_STATUS_SUCCESS) {
				wl_cache_assoc_resp_ies(cfg, as.ndev, e, data);
			}
			break;
		case WLC_E_SET_SSID:
			wl_cfg80211_handle_set_ssid_complete(cfg, &as, e, assoc_state);
			break;
		case WLC_E_DEAUTH_IND:
		case WLC_E_DISASSOC_IND:
			wl_cfg80211_handle_deauth_ind(cfg, &as);
			/* intentional fall through */
		case WLC_E_DISASSOC:
		case WLC_E_DEAUTH:
			as.link_action = wl_set_link_action(assoc_state, false);
			break;
		case WLC_E_LINK:
			if (as.flags & WLC_EVENT_MSG_LINK) {
				as.link_action = wl_set_link_action(assoc_state, true);
			} else {
				as.link_action = wl_set_link_action(assoc_state, false);
			}
			break;
		default:
			WL_DBG(("Ignore event:%d\n", as.event_type));
			as.link_action = 0;
	}

	if (as.link_action) {
		/* Handle change in link state (if any) */
		err = wl_handle_sta_link_action(cfg, &as);
	}

	return err;
}

#define IS_OBSOLETE_EVENT(cur_idx, marker_idx) ((s32)(cur_idx - marker_idx) < 0)
static s32
wl_notify_connect_status_sta(struct bcm_cfg80211 *cfg,
	struct wireless_dev *wdev, const wl_event_msg_t *e, void *data)
{
	u32 event_type;
	wl_assoc_state_t assoc_state;
	struct net_device *ndev;
	s32 ret = BCME_OK;
	wl_event_idx_t *idx = &cfg->eidx;

	if (!wdev || !e) {
		WL_ERR(("wrong input\n"));
		return -EINVAL;
	}

	event_type = ntoh32(e->event_type);
	if (IS_OBSOLETE_EVENT(idx->in_progress, idx->min_connect_idx)) {
		/* If this event is enqd before the connect req, discard */
		WL_ERR(("discard obsolete event:%d. cur_idx:%d min_idx:%d\n",
			event_type, idx->in_progress, idx->min_connect_idx));
		return -EINVAL;
	}

	ndev = wdev->netdev;
	if (!wl_get_drv_status(cfg, CFG80211_CONNECT, ndev)) {
		/* Join attempt via non-cfg80211 interface.
		 * Don't send events to cfg80211 layer
		 */
		WL_INFORM_MEM(("Event received in non-cfg80211"
				" connect state. Ignore\n"));
		goto exit;
	}

	if (wl_get_drv_status(cfg, CONNECTING, ndev)) {
		assoc_state = WL_STATE_ASSOCIATING;
	} else if (wl_get_drv_status(cfg, CONNECTED, ndev)) {
		assoc_state = WL_STATE_ASSOCIATED;
	} else {
		WL_ERR(("Unexpected event:%d in assoc idle state\n", event_type));
		assoc_state = WL_STATE_ASSOC_IDLE;
	}

	ret = wl_handle_assoc_events(cfg, wdev, e, data, assoc_state);
exit:
	return ret;
}

static s32
wl_notify_connect_status(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	struct net_device *ndev = NULL;
	s32 err = 0;
	u32 mode;

#if defined(BCMDONGLEHOST) && defined(OEM_ANDROID)
	DHD_DISABLE_RUNTIME_PM((dhd_pub_t *)cfg->pub);
#endif /* BCMDONGLEHOST && OEM_ANDROID */

	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);
	mode = wl_get_mode_by_netdev(cfg, ndev);

	/* Push link events to upper layer log */
	SUPP_LOG(("[%s] Mode:%d event:%d status:0x%x reason:%d\n",
		ndev->name, mode, ntoh32(e->event_type),
		ntoh32(e->status),  ntoh32(e->reason)));

	if (mode == WL_MODE_AP) {
		/* AP/P2O GO cases */
		err = wl_notify_connect_status_ap(cfg, ndev, e, data);
	} else if (mode == WL_MODE_IBSS) {
		err = wl_notify_connect_status_ibss(cfg, ndev, e, data);
	} else if (mode == WL_MODE_BSS) {
		/* STA/GC cases */
		err = wl_notify_connect_status_sta(cfg, ndev->ieee80211_ptr, e, data);
	} else {
		WL_ERR(("Unexpected event:%d for mode:%d\n", e->event_type, mode));
	}

#if defined(BCMDONGLEHOST) && defined(OEM_ANDROID)
	DHD_ENABLE_RUNTIME_PM((dhd_pub_t *)cfg->pub);
#endif /* BCMDONGLEHOST && OEM_ANDROID */
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

#ifdef WLAIBSS
void wl_cfg80211_set_txfail_pid(struct net_device *dev, int pid)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	if (pid > 0)
		cfg->aibss_txfail_pid = pid;
	WL_DBG(("set pid for aibss fail event : pid=%d\n", pid));
}

static s32
wl_notify_aibss_txfail(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	u32 evt = ntoh32(e->event_type);
	int ret = -1;
#ifdef PCIE_FULL_DONGLE
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);
	u32 reason = ntoh32(e->reason);
#endif
	if (cfg->aibss_txfail_pid != 0) {
#ifdef PCIE_FULL_DONGLE
		if (reason == AIBSS_PEER_FREE) {
			uint8 ifindex;
			wl_event_msg_t event;

			bzero(&event, sizeof(wl_event_msg_t));
			memcpy(&event, e, sizeof(wl_event_msg_t));

			ifindex = (uint8)dhd_ifname2idx(dhd->info, event.ifname);
			WL_INFORM_MEM(("Peer freed. Flow rings delete for peer.\n"));
			dhd_flow_rings_delete_for_peer(dhd, ifindex,
				(void *)&event.addr.octet[0]);
			return 0;
		}
#endif
		ret = wl_netlink_send_msg(cfg->aibss_txfail_pid, AIBSS_EVENT_TXFAIL,
			cfg->aibss_txfail_seq++, &e->addr, ETHER_ADDR_LEN);
	}

	WL_DBG(("txfail : evt=%d, pid=%d, ret=%d, mac=" MACF "\n",
		evt, cfg->aibss_txfail_pid, ret, CONST_ETHERP_TO_MACF(&e->addr)));
	return ret;
}
#endif /* WLAIBSS */
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
#ifdef BCMDONGLEHOST
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
#endif /* BCMDONGLEHOST */
	WL_DBG(("Enter \n"));

#ifdef BCMDONGLEHOST
	BCM_REFERENCE(dhdp);
#endif /* BCMDONGLEHOST */
	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);

	if ((!cfg->disable_roam_event) && (event == WLC_E_BSSID)) {

#ifdef OEM_ANDROID
		wl_add_remove_eventmsg(ndev, WLC_E_ROAM, false);
#endif /* OEM_ANDROID */

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
#ifdef BCMDONGLEHOST
				/* Arm pkt logging timer */
				dhd_dump_mod_pkt_timer(dhdp, PKT_CNT_RSN_ROAM);
#endif /* BCMDONGLEHOST */
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
#if !defined(DHD_NONFT_ROAMING)
			wl_bss_roaming_done(cfg, ndev, e, data);
#endif /* !DHD_NONFT_ROAMING */
#endif /* DHD_LOSSLESS_ROAMING */
#ifdef WBTEXT
			if (dhdp->wbtext_support) {
				/* set wnm_keepalives_max_idle after association */
				wl_cfg80211_wbtext_set_wnm_maxidle(cfg, ndev);

				/* Mostly nbr request of BTM query will be handled
				 * from wl_cfg80211_change_station
				 * after key negotiation is finished.
				 * This part is only for some specific security
				 * types (FT, CCKM) that don't call
				 * wl_cfg80211_change_station after roaming
				 */
				if (IS_AKM_SUITE_FT(sec) || IS_AKM_SUITE_CCKM(sec)) {
					/* send nbr request or BTM query to update RCC
					 * after roaming completed
					 */
					wl_cfg80211_wbtext_update_rcc(cfg, ndev);
				}
			}
#endif /* WBTEXT */
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
#define DPM_UPD_LMT_TIME	((CUSTOM_EVENT_PM_WAKE + (5)) * (1000) * (4))	/* ms */
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
#ifdef SUPPORT_RSSI_SUM_REPORT
	wl_rssi_ant_mimo_t rssi_ant_mimo;
#endif /* SUPPORT_RSSI_SUM_REPORT */
	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);

	pbuf = (u8 *)MALLOCZ(cfg->osh, WLC_IOCTL_MEDLEN);
	if (pbuf == NULL) {
		WL_ERR(("failed to allocate local pbuf\n"));
		return -ENOMEM;
	}

	err = wldev_iovar_getbuf_bsscfg(ndev, "dump",
		"pm", strlen("pm"), pbuf, WLC_IOCTL_MEDLEN,
		0, &cfg->ioctl_buf_sync);

	if (err) {
		WL_ERR(("dump ioctl err = %d", err));
	} else {
		WL_ERR(("PM status : %s\n", pbuf));
	}

	if (pbuf) {
		MFREE(cfg->osh, pbuf, WLC_IOCTL_MEDLEN);
	}

	if (dhd->early_suspended) {
		/* LCD off */
#ifdef SUPPORT_RSSI_SUM_REPORT
		/* Query RSSI sum across antennas */
		memset(&rssi_ant_mimo, 0, sizeof(rssi_ant_mimo));
		err = wl_get_rssi_per_ant(ndev, ndev->name, NULL, &rssi_ant_mimo);
		if (err) {
			WL_ERR(("Could not get rssi sum (%d)\n", err));
		}
		rssi = rssi_ant_mimo.rssi_sum;
		if (rssi == 0)
#endif /* SUPPORT_RSSI_SUM_REPORT */
		{
			scb_val_t scb_val;
			memset(&scb_val, 0, sizeof(scb_val_t));
			scb_val.val = 0;
			err = wldev_ioctl_get(ndev, WLC_GET_RSSI, &scb_val, sizeof(scb_val_t));
			if (err) {
				WL_ERR(("Could not get rssi (%d)\n", err));
			}
			rssi = wl_rssi_offset(dtoh32(scb_val.val));
		}
		WL_ERR(("RSSI %d dBm\n", rssi));
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
			DHD_STATLOG_CTRL(dhd, ST(DISASSOC_INT_START),
				dhd_net2idx(dhd->info, ndev), 0);
			bzero(&scbval, sizeof(scb_val_t));

			err = wldev_ioctl_set(ndev, WLC_DISASSOC,
				&scbval, sizeof(scb_val_t));
			if (err < 0) {
				WL_ERR(("Disassoc error %d\n", err));
				return err;
			}
			WL_ERR(("Force Disassoc due to updated DPM event.\n"));

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

#if defined(DHD_LOSSLESS_ROAMING) || defined (DBG_PKT_MON)
/*
 * start packet logging in advance to make sure that EAPOL
 * messages are not missed during roaming
 */
static s32
wl_notify_roam_prep_status(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	struct wl_security *sec;
	struct net_device *ndev;
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
	u32 status = ntoh32(e->status);
	u32 reason = ntoh32(e->reason);

	BCM_REFERENCE(sec);

	if (status == WLC_E_STATUS_SUCCESS && reason != WLC_E_REASON_INITIAL_ASSOC) {
		WL_ERR(("Attempting roam with reason code : %d\n", reason));
	}

#ifdef CONFIG_SILENT_ROAM
	if (dhdp->in_suspend && reason == WLC_E_REASON_SILENT_ROAM) {
		dhdp->sroamed = TRUE;
	}
#endif /* CONFIG_SILENT_ROAM */

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
	if (IS_AKM_SUITE_FT(sec) || IS_AKM_OWE(sec->wpa_auth)) {
		return BCME_OK;
	}

	dhdp->dequeue_prec_map = 1 << dhdp->flow_prio_map[PRIO_8021D_NC];
	/* Restore flow control  */
	dhd_txflowcontrol(dhdp, ALL_INTERFACES, OFF);

	mod_timer(&cfg->roam_timeout, jiffies + msecs_to_jiffies(WL_ROAM_TIMEOUT_MS));
#endif /* DHD_LOSSLESS_ROAMING */

	return BCME_OK;
}
#endif /* DHD_LOSSLESS_ROAMING || DBG_PKT_MON */

static s32
wl_notify_roam_start_status(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 13, 0)) || defined(WL_VENDOR_EXT_SUPPORT)
	struct net_device *ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);
	struct wiphy *wiphy = bcmcfg_to_wiphy(cfg);
	int event_type;

	event_type = WIFI_EVENT_ROAM_SCAN_STARTED;
	wl_cfgvendor_send_async_event(wiphy, ndev, GOOGLE_ROAM_EVENT_START,
		&event_type, sizeof(int));
#endif /* (LINUX_VERSION_CODE > KERNEL_VERSION(3, 13, 0)) || (WL_VENDOR_EXT_SUPPORT) */

	return BCME_OK;
}

static s32 wl_get_assoc_ies(struct bcm_cfg80211 *cfg, struct net_device *ndev)
{
	wl_assoc_info_t assoc_info;
	struct wl_connect_info *conn_info = wl_to_conn(cfg);
	s32 err = 0;
#ifdef QOS_MAP_SET
	bcm_tlv_t * qos_map_ie = NULL;
#endif /* QOS_MAP_SET */

	WL_DBG(("Enter \n"));

	bzero(&assoc_info, sizeof(wl_assoc_info_t));
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
			assoc_info.req_len, NULL);
		if (unlikely(err)) {
			WL_ERR(("could not get assoc req (%d)\n", err));
			return err;
		}
		if (assoc_info.req_len < sizeof(struct dot11_assoc_req)) {
			WL_ERR(("req_len %d lessthan %d \n", assoc_info.req_len,
				(int)sizeof(struct dot11_assoc_req)));
			return BCME_BADLEN;
		}
		conn_info->req_ie_len = (uint32)(assoc_info.req_len
						- sizeof(struct dot11_assoc_req));
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
			assoc_info.resp_len, NULL);
		if (unlikely(err)) {
			WL_ERR(("could not get assoc resp (%d)\n", err));
			return err;
		}
		if (assoc_info.resp_len < sizeof(struct dot11_assoc_resp)) {
			WL_ERR(("resp_len %d is lessthan %d \n", assoc_info.resp_len,
				(int)sizeof(struct dot11_assoc_resp)));
			return BCME_BADLEN;
		}
		conn_info->resp_ie_len = assoc_info.resp_len -
				(uint32)sizeof(struct dot11_assoc_resp);
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
				cfg->up_table = (uint8 *)MALLOC(cfg->osh, UP_TABLE_MAX);
			}
			wl_set_up_table(cfg->up_table, qos_map_ie);
		} else {
			MFREE(cfg->osh, cfg->up_table, UP_TABLE_MAX);
		}
#endif /* QOS_MAP_SET */
	} else {
		conn_info->resp_ie_len = 0;
	}
	WL_DBG(("req len (%d) resp len (%d)\n", conn_info->req_ie_len,
		conn_info->resp_ie_len));

	return err;
}

static s32 wl_update_bss_info(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	bool update_ssid)
{
	struct cfg80211_bss *bss;
	wl_bss_info_t *bi;
	struct wlc_ssid *ssid;
	const struct bcm_tlv *tim;
	s32 beacon_interval;
	s32 dtim_period;
	size_t ie_len;
	const u8 *ie;
	u8 *curbssid;
	s32 err = 0;
	struct wiphy *wiphy;
	char *buf;
	u32 freq;
	chanspec_t chspec = INVCHANSPEC;

	wiphy = bcmcfg_to_wiphy(cfg);

	ssid = (struct wlc_ssid *)wl_read_prof(cfg, ndev, WL_PROF_SSID);
	curbssid = wl_read_prof(cfg, ndev, WL_PROF_BSSID);
	bss = CFG80211_GET_BSS(wiphy, NULL, curbssid,
		ssid->SSID, ssid->SSID_len);
	buf = (char *)MALLOCZ(cfg->osh, WL_EXTRA_BUF_MAX);
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
	bi = (wl_bss_info_t *)(buf + 4);
	chspec = wl_chspec_driver_to_host(bi->chanspec);
	wl_update_prof(cfg, ndev, NULL, &chspec, WL_PROF_CHAN);

	if (!bss) {
		if (memcmp(bi->BSSID.octet, curbssid, ETHER_ADDR_LEN)) {
			WL_ERR(("Bssid doesn't match\n"));
			err = -EIO;
			goto update_bss_info_out;
		}
		err = wl_inform_single_bss(cfg, bi, update_ssid);
		if (unlikely(err)) {
			WL_ERR(("Could not update the AP detail in cache\n"));
			goto update_bss_info_out;
		}

		WL_INFORM_MEM(("Updated the AP %pM detail in cache\n", curbssid));
		ie = ((u8 *)bi) + bi->ie_offset;
		ie_len = bi->ie_length;
		beacon_interval = cpu_to_le16(bi->beacon_period);
	} else {
		u16 channel;
		WL_INFORM_MEM(("Found AP in the cache - BSSID %pM\n", bss->bssid));
		channel = wf_chspec_ctlchan(wl_chspec_driver_to_host(bi->chanspec));
		freq = wl_channel_to_frequency(channel, CHSPEC_BAND(bi->chanspec));
		bss->channel = ieee80211_get_channel(wiphy, freq);
#if defined(WL_CFG80211_P2P_DEV_IF)
		ie = (const u8 *)bss->ies->data;
		ie_len = bss->ies->len;
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

	MFREE(cfg->osh, buf, WL_EXTRA_BUF_MAX);
	mutex_unlock(&cfg->usr_sync);
	return err;
}

#ifdef DHD_LOSSLESS_ROAMING
static s32
wl_bss_roaming_done(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *e, void *data)
{
	struct wl_connect_info *conn_info = wl_to_conn(cfg);
	s32 err = 0;
	u8 *curbssid;
	chanspec_t *chanspec;
	scb_val_t scbval;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39)) || \
	defined(WL_COMPAT_WIRELESS)
	struct wiphy *wiphy = bcmcfg_to_wiphy(cfg);
	struct ieee80211_channel *notify_channel = NULL;
	u32 freq;
	u32 cur_channel, cur_chanspec, orig_channel;
#endif /* LINUX_VERSION > 2.6.39 || WL_COMPAT_WIRELESS */
#if (defined(CONFIG_ARCH_MSM) && defined(CFG80211_ROAMED_API_UNIFIED)) || \
	(LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)) || \
	defined(WL_FILS_ROAM_OFFLD) || defined(CFG80211_ROAM_API_GE_4_12)
	struct cfg80211_roam_info roam_info;
#endif /* (CONFIG_ARCH_MSM && CFG80211_ROAMED_API_UNIFIED) || LINUX_VERSION >= 4.12.0 */
#if defined(WL_FILS_ROAM_OFFLD)
	struct wl_fils_info *fils_info = wl_to_fils_info(cfg);
	struct wl_security *sec = wl_read_prof(cfg, ndev, WL_PROF_SEC);
#endif
#ifdef BCMDONGLEHOST
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
#endif /* BCMDONGLEHOST */
#ifdef DHD_POST_EAPOL_M1_AFTER_ROAM_EVT
	dhd_if_t *ifp = NULL;
#endif /* DHD_POST_EAPOL_M1_AFTER_ROAM_EVT */
	s32 rssi;
#ifdef WLFBT
	uint32 data_len = 0;
	if (data)
		data_len = ntoh32(e->datalen);
#endif /* WLFBT */

	BCM_REFERENCE(dhdp);
	curbssid = wl_read_prof(cfg, ndev, WL_PROF_BSSID);
	chanspec = (chanspec_t *)wl_read_prof(cfg, ndev, WL_PROF_CHAN);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39)) || \
	defined(WL_COMPAT_WIRELESS)
	/* Skip calling cfg80211_roamed If the channels are same and
	 * the current bssid & the new bssid are same
	 * Also clear timer roam_timeout.
	 * Only used on BCM4359 devices.
	 */
	err = wldev_iovar_getint(ndev, "chanspec", (s32 *)&cur_chanspec);
	if (unlikely(err)) {
		return  err;
	}
	cur_channel = wf_chspec_ctlchan(wl_chspec_driver_to_host(cur_chanspec));
	orig_channel = wf_chspec_ctlchan(wl_chspec_driver_to_host(*chanspec));
	if (dhdp->conf->chip != BCM43569_CHIP_ID) {
	if ((orig_channel == cur_channel) &&
			((memcmp(curbssid, &e->addr, ETHER_ADDR_LEN) == 0) ||
			(memcmp(&cfg->last_roamed_addr, &e->addr, ETHER_ADDR_LEN) == 0))) {
		WL_DBG(("BSS already present, Skipping roamed event to upper layer\n"));
		goto fail;
	}
	}
#endif /* LINUX_VERSION > 2.6.39 || WL_COMPAT_WIRELESS */

	if ((err = wl_get_assoc_ies(cfg, ndev)) != BCME_OK) {
#ifdef BCMDONGLEHOST
		DHD_STATLOG_CTRL(dhdp, ST(DISASSOC_INT_START),
			dhd_net2idx(dhdp->info, ndev), WLAN_REASON_DEAUTH_LEAVING);
#endif /* BCMDONGLEHOST */
		WL_ERR(("Fetching Assoc IEs failed, Skipping roamed event to"
			" upper layer\n"));
		/* To make sure disconnect, and fw sync, explictly send dissassoc
		 * for BSSID 00:00:00:00:00:00 issue
		 */
		bzero(&scbval, sizeof(scb_val_t));
		scbval.val = WLAN_REASON_DEAUTH_LEAVING;
		memcpy(&scbval.ea, curbssid, ETHER_ADDR_LEN);
		scbval.val = htod32(scbval.val);
		if (wldev_ioctl_set(ndev, WLC_DISASSOC, &scbval,
				sizeof(scb_val_t)) < 0) {
			WL_ERR(("WLC_DISASSOC error\n"));
		}
		goto fail;
	}

	memset(&scbval, 0, sizeof(scb_val_t));
	err = wldev_ioctl_get(ndev, WLC_GET_RSSI, &scbval, sizeof(scb_val_t));
	if (err) {
		WL_ERR(("Could not get rssi (%d)\n", err));
	}
	rssi = dtoh32(scbval.val);

	WL_MSG(ndev->name, "%pM(ch:%3d/%sMHz) => %pM(ch:%3d/%sMHz, rssi: %3d)\n",
		curbssid, orig_channel,
		CHSPEC_IS20(*chanspec)?"20":
		CHSPEC_IS40(*chanspec)?"40":
		CHSPEC_IS80(*chanspec)?"80":
		CHSPEC_IS160(*chanspec)?"160":"??",
		(const u8*)(&e->addr), cur_channel,
		CHSPEC_IS20(cur_chanspec)?"20":
		CHSPEC_IS40(cur_chanspec)?"40":
		CHSPEC_IS80(cur_chanspec)?"80":
		CHSPEC_IS160(cur_chanspec)?"160":"??",
		rssi);

	wl_update_prof(cfg, ndev, NULL, (const void *)(e->addr.octet), WL_PROF_BSSID);
	curbssid = wl_read_prof(cfg, ndev, WL_PROF_BSSID);
	if ((err = wl_update_bss_info(cfg, ndev, true)) != BCME_OK) {
		WL_ERR(("failed to update bss info, err=%d\n", err));
		goto fail;
	}
	if (cfg->wlc_ver.wlc_ver_major < PMKDB_WLC_VER) {
		wl_update_pmklist(ndev, cfg->pmk_list, err);
	}

	chanspec = (chanspec_t *)wl_read_prof(cfg, ndev, WL_PROF_CHAN);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39)) || \
	defined(WL_COMPAT_WIRELESS)
	/* channel info for cfg80211_roamed introduced in 2.6.39-rc1 */
	freq = wl_channel_to_frequency(wf_chspec_ctlchan(*chanspec), CHSPEC_BAND(*chanspec));
	notify_channel = ieee80211_get_channel(wiphy, freq);
#endif /* LINUX_VERSION > 2.6.39  || WL_COMPAT_WIRELESS */
#ifdef WLFBT
	/* back up the given FBT key for the further supplicant request,
	 * currently not checking the FBT is enabled for current BSS in DHD,
	 * because the supplicant decides to take it or not.
	 */
	if (data && (data_len == FBT_KEYLEN)) {
		memcpy(cfg->fbt_key, data, FBT_KEYLEN);
	}
#endif /* WLFBT */
#ifdef CUSTOM_LONG_RETRY_LIMIT
	if (wl_set_retry(ndev, CUSTOM_LONG_RETRY_LIMIT, 1) < 0) {
		WL_ERR(("CUSTOM_LONG_RETRY_LIMIT set fail!\n"));
	}
#endif /* CUSTOM_LONG_RETRY_LIMIT */
	DHD_STATLOG_CTRL(dhdp, ST(REASSOC_INFORM),
		dhd_net2idx(dhdp->info, ndev), 0);
#ifdef WL_EXT_IAPSTA
	wl_ext_in4way_sync(ndev, 0, WL_EXT_STATUS_CONNECTED, NULL);
#endif

#if (defined(CONFIG_ARCH_MSM) && defined(CFG80211_ROAMED_API_UNIFIED)) || \
	(LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)) || \
	defined(WL_FILS_ROAM_OFFLD) || defined(CFG80211_ROAM_API_GE_4_12)
	memset(&roam_info, 0, sizeof(struct cfg80211_roam_info));
	roam_info.channel = notify_channel;
	roam_info.bssid = curbssid;
	roam_info.req_ie = conn_info->req_ie;
	roam_info.req_ie_len = conn_info->req_ie_len;
	roam_info.resp_ie = conn_info->resp_ie;
	roam_info.resp_ie_len = conn_info->resp_ie_len;
#if defined(WL_FILS_ROAM_OFFLD)
	if ((sec->auth_type == NL80211_AUTHTYPE_FILS_SK_PFS) ||
			(sec->auth_type == NL80211_AUTHTYPE_FILS_SK)) {
		roam_info.fils.kek = fils_info->fils_kek;
		roam_info.fils.kek_len = fils_info->fils_kek_len;
		roam_info.fils.update_erp_next_seq_num = true;
		roam_info.fils.erp_next_seq_num = fils_info->fils_erp_next_seq_num;
		roam_info.fils.pmk = fils_info->fils_pmk;
		roam_info.fils.pmk_len = fils_info->fils_kek_len;
		roam_info.fils.pmkid = fils_info->fils_pmkid;
	}
#endif
	cfg80211_roamed(ndev, &roam_info, GFP_KERNEL);
#else
	cfg80211_roamed(ndev,
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39)) || \
	defined(WL_COMPAT_WIRELESS)
		notify_channel,
#endif
		curbssid,
		conn_info->req_ie, conn_info->req_ie_len,
		conn_info->resp_ie, conn_info->resp_ie_len, GFP_KERNEL);
#endif /* (CONFIG_ARCH_MSM && CFG80211_ROAMED_API_UNIFIED) || LINUX_VERSION >= 4.12.0 */

	memcpy(&cfg->last_roamed_addr, &e->addr, ETHER_ADDR_LEN);
	wl_set_drv_status(cfg, CONNECTED, ndev);

#if defined(DHD_ENABLE_BIGDATA_LOGGING)
	cfg->roam_count++;
#endif /* DHD_ENABLE_BIGDATA_LOGGING */
#ifdef WL_BAM
	if (wl_adps_bad_ap_check(cfg, &e->addr)) {
		if (wl_adps_enabled(cfg, ndev)) {
			wl_adps_set_suspend(cfg, ndev, ADPS_SUSPEND);
		}
	}
#endif	/* WL_BAM */

#ifdef DHD_POST_EAPOL_M1_AFTER_ROAM_EVT
	ifp = dhd_get_ifp(dhdp, e->ifidx);
	if (ifp) {
		ifp->post_roam_evt = TRUE;
	}
#endif /* DHD_POST_EAPOL_M1_AFTER_ROAM_EVT */

	return err;

fail:
#ifdef DHD_LOSSLESS_ROAMING
	wl_del_roam_timeout(cfg);
#endif  /* DHD_LOSSLESS_ROAMING */
	return err;
}
#endif /* DHD_LOSSLESS_ROAMING */

static bool
wl_cfg80211_verify_bss(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	struct cfg80211_bss **bss)
{
	struct wiphy *wiphy;
	struct wlc_ssid *ssid;
	uint8 *curbssid;
	bool ret = false;

	wiphy = bcmcfg_to_wiphy(cfg);
	ssid = (struct wlc_ssid *)wl_read_prof(cfg, ndev, WL_PROF_SSID);
	curbssid = wl_read_prof(cfg, ndev, WL_PROF_BSSID);
	if (!ssid || !curbssid) {
		WL_ERR(("No SSID/bssid found in the saved profile \n"));
		return false;
	}

	*bss = CFG80211_GET_BSS(wiphy, NULL, curbssid,
		ssid->SSID, ssid->SSID_len);
	if (*bss) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 7, 0))
		/* Update the reference count after use. In case of kernel version >= 4.7
		* the cfg802_put_bss is called in cfg80211_connect_bss context
		*/
		CFG80211_PUT_BSS(wiphy, *bss);
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(4, 7, 0) */
		ret = true;
	} else {
		WL_ERR(("No bss entry for bssid:"MACDBG" ssid_len:%d\n",
			MAC2STRDBG(curbssid), ssid->SSID_len));
	}

	return ret;
}

#ifdef WL_FILS
static s32
wl_get_fils_connect_params(struct bcm_cfg80211 *cfg, struct net_device *ndev)
{
	const bcm_xtlv_t* pxtlv_out;
	struct wl_fils_info *fils_info = wl_to_fils_info(cfg);
	int err = BCME_OK;
	bcm_iov_buf_t *iov_buf_in = NULL;
	bcm_iov_buf_t iov_buf_out = {0};
	u16 len;
	u16 type;
	const u8 *data;
	iov_buf_in = MALLOCZ(cfg->osh, WLC_IOCTL_SMLEN);
	if (!iov_buf_in) {
		WL_ERR(("buf memory alloc failed\n"));
		err = BCME_NOMEM;
		goto exit;
	}
	iov_buf_out.version = WL_FILS_IOV_VERSION;
	iov_buf_out.id = WL_FILS_CMD_GET_CONNECT_PARAMS;
	err = wldev_iovar_getbuf(ndev, "fils", (uint8*)&iov_buf_out, sizeof(bcm_iov_buf_t),
		iov_buf_in, WLC_IOCTL_SMLEN, &cfg->ioctl_buf_sync);
	if (unlikely(err)) {
		WL_ERR(("Get FILS Params Error (%d)\n", err));
		goto exit;
	}
	pxtlv_out = (bcm_xtlv_t*)((bcm_iov_buf_t*)iov_buf_in)->data;
	len = iov_buf_in->len;
	do {
		if (!bcm_valid_xtlv(pxtlv_out, iov_buf_in->len, BCM_XTLV_OPTION_ALIGN32)) {
			WL_ERR(("%s: XTLV is not valid\n", __func__));
			err = BCME_BADARG;
			goto exit;
		}
		bcm_xtlv_unpack_xtlv(pxtlv_out, &type, &len, &data, BCM_XTLV_OPTION_ALIGN32);
		switch (type) {
			case WL_FILS_XTLV_ERP_NEXT_SEQ_NUM:
				fils_info->fils_erp_next_seq_num = *(const u16 *)data;
				break;
			case WL_FILS_XTLV_KEK:
				if (memcpy_s(fils_info->fils_kek,
						WL_MAX_FILS_KEY_LEN, data, len) < 0) {
					err = BCME_BADARG;
					goto exit;
				}
				fils_info->fils_kek_len = len;
				break;
			case WL_FILS_XTLV_PMK:
				if (memcpy_s(fils_info->fils_pmk,
						WL_MAX_FILS_KEY_LEN, data, len) < 0) {
					err = BCME_BADARG;
					goto exit;
				}
				fils_info->fils_pmk_len = len;
				break;
			case WL_FILS_XTLV_PMKID:
				if (memcpy_s(fils_info->fils_pmkid,
						WL_MAX_FILS_KEY_LEN, data, len) < 0) {
					err = BCME_BADARG;
					goto exit;
				}
				break;
			default:
				WL_ERR(("%s: wrong XTLV code\n", __func__));
				break;

		}
	} while ((pxtlv_out = bcm_next_xtlv(pxtlv_out, (int *)&iov_buf_in->len,
		BCM_XTLV_OPTION_ALIGN32)) && iov_buf_in->len);
exit:
	if (iov_buf_in) {
		MFREE(cfg->osh, iov_buf_in, WLC_IOCTL_SMLEN);
	}
	return err;
}
#endif /* WL_FILS */

#ifdef WL_FILS
static s32
wl_fillup_resp_params(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	u8 *curbssid, void *params, u32 status)
{
	struct wl_connect_info *conn_info = wl_to_conn(cfg);
	struct cfg80211_connect_resp_params *resp_params;
	struct wl_fils_info *fils_info = NULL;
	struct wlc_ssid *ssid = NULL;
	struct wiphy *wiphy = NULL;
	s32 ret = BCME_OK;

	fils_info = wl_to_fils_info(cfg);
	ssid = (struct wlc_ssid *)wl_read_prof(cfg, ndev, WL_PROF_SSID);
	wiphy = bcmcfg_to_wiphy(cfg);
	if (!ssid) {
		WL_ERR(("ssid profile not found\n"));
		return BCME_ERROR;
	}

	resp_params = (struct cfg80211_connect_resp_params *)params;
	resp_params->status = status;
	resp_params->bssid = curbssid;
	resp_params->bss = CFG80211_GET_BSS(wiphy, NULL, curbssid,
		ssid->SSID, ssid->SSID_len);
	if (!resp_params->bss) {
		WL_ERR(("null bss\n"));
		return BCME_ERROR;
	}

	resp_params->req_ie = conn_info->req_ie;
	resp_params->req_ie_len = conn_info->req_ie_len;
	resp_params->resp_ie = conn_info->resp_ie;
	resp_params->resp_ie_len = conn_info->resp_ie_len;
#ifdef WL_FILS_ROAM_OFFLD
	/* Kernel >= 4.17 has introduced FILS data struct inside resp params */
	resp_params->fils.kek = fils_info->fils_kek;
	resp_params->fils.kek_len = fils_info->fils_kek_len;
	resp_params->fils.update_erp_next_seq_num = true;
	resp_params->fils.erp_next_seq_num = fils_info->fils_erp_next_seq_num;
	resp_params->fils.pmk = fils_info->fils_pmk;
	resp_params->fils.pmk_len = fils_info->fils_kek_len;
	resp_params->fils.pmkid = fils_info->fils_pmkid;
#else
	resp_params->fils_kek = fils_info->fils_kek;
	resp_params->fils_kek_len = fils_info->fils_kek_len;
	resp_params->update_erp_next_seq_num = true;
	resp_params->fils_erp_next_seq_num = fils_info->fils_erp_next_seq_num;
	resp_params->pmk = fils_info->fils_pmk;
	resp_params->pmk_len = fils_info->fils_kek_len;
	resp_params->pmkid = fils_info->fils_pmkid;
#endif /* WL_FILS_ROAM_OFFLD */

	return ret;
}
#endif /* WL_FILS */

static s32
wl_bss_connect_done(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *e, void *data, bool completed)
{
	struct wl_connect_info *conn_info = wl_to_conn(cfg);
	struct wl_security *sec = wl_read_prof(cfg, ndev, WL_PROF_SEC);
	s32 err = 0;
	u8 *conn_req_bssid = wl_read_prof(cfg, ndev, WL_PROF_LATEST_BSSID);
	u32 status;
#ifdef WL_FILS
	struct cfg80211_connect_resp_params resp_params = {0};
#endif /* WL_FILS */

	u8 *curbssid = wl_read_prof(cfg, ndev, WL_PROF_BSSID);
	u32 event_type = ntoh32(e->event_type);
	struct cfg80211_bss *bss = NULL;
#ifdef BCMDONGLEHOST
	dhd_pub_t *dhdp;
	dhdp = (dhd_pub_t *)(cfg->pub);
	BCM_REFERENCE(dhdp);
#endif /* BCMDONGLEHOST */

	WL_DBG((" enter\n"));
	if (!sec) {
		WL_ERR(("sec is NULL\n"));
		err = -ENODEV;
		goto exit;
	}

	if (!wl_get_drv_status(cfg, CONNECTING, ndev)) {
		WL_INFORM_MEM(("[%s] Ignore event:%d. drv status"
				" connecting:%x. connected:%d\n",
				ndev->name, event_type, wl_get_drv_status(cfg, CONNECTING, ndev),
				wl_get_drv_status(cfg, CONNECTED, ndev)));
		err = BCME_OK;
		goto exit;
	}

	if (!conn_req_bssid) {
		WL_ERR(("conn_req bssid is null\n"));
		err = -EINVAL;
		goto exit;
	}

	if (ETHER_ISNULLADDR(curbssid) &&
		!ETHER_ISNULLADDR(conn_req_bssid)) {
		WL_DBG(("copy bssid\n"));
		memcpy(curbssid, conn_req_bssid, ETHER_ADDR_LEN);
	}

	wl_cfgscan_cancel_scan(cfg);
	bzero(&cfg->last_roamed_addr, ETHER_ADDR_LEN);

	if (completed) {
		wl_get_assoc_ies(cfg, ndev);
		wl_update_prof(cfg, ndev, NULL, (const void *)(e->addr.octet),
			WL_PROF_BSSID);
		curbssid = wl_read_prof(cfg, ndev, WL_PROF_BSSID);
		/*
			* CFG layer relies on cached IEs (from probe/beacon) to fetch matching bss.
			* For cases, there is no match available,
			* need to update the cache based on bss info from fw.
			*/
		wl_update_bss_info(cfg, ndev, true);
		if (cfg->wlc_ver.wlc_ver_major < PMKDB_WLC_VER) {
			wl_update_pmklist(ndev, cfg->pmk_list, err);
		}
		wl_set_drv_status(cfg, CONNECTED, ndev);

		if (wl_cfg80211_verify_bss(cfg, ndev, &bss) != true) {
			/* If bss entry is not available in the cfg80211 bss cache
				* the wireless stack will complain and won't populate
				* wdev->current_bss ptr
				*/
			WL_ERR(("BSS entry not found. Indicate assoc event failure\n"));
			completed = false;
			sec->auth_assoc_res_status = WLAN_STATUS_UNSPECIFIED_FAILURE;
		}
		if (!ndev->ieee80211_ptr->ssid_len) {
			/* In certain cases, the delayed cfg80211 work from
				* disconnect context will induce race conditions in
				* which the ssid_len will be cleared, but dhd is in
				* connecting state. Return connect failure to avoid
				* getting locked in connected state.
				*/
			WL_ERR(("ssid_len=0. Indicate assoc event failure\n"));
			completed = false;
			sec->auth_assoc_res_status = WLAN_STATUS_UNSPECIFIED_FAILURE;
			wl_clr_drv_status(cfg, CONNECTED, ndev);
		}
	}

	/* Clear status after updating CONNECTED status */
	wl_clr_drv_status(cfg, CONNECTING, ndev);

	/* update status field */
	if (completed) {
		status = WLAN_STATUS_SUCCESS;
	} else if (sec->auth_assoc_res_status) {
			status = sec->auth_assoc_res_status;
	} else {
		status = WLAN_STATUS_UNSPECIFIED_FAILURE;
	}

	if (completed) {
		WL_MSG(ndev->name, "Report connect result - connection succeeded\n");
#ifdef WL_EXT_IAPSTA
		wl_ext_in4way_sync(ndev, 0, WL_EXT_STATUS_CONNECTED, NULL);
		wl_ext_iapsta_enable_master_if(ndev, TRUE);
#endif
	} else {
		WL_MSG(ndev->name, "Report connect result - connection failed\n");
#ifdef WL_EXT_IAPSTA
		wl_ext_in4way_sync(ndev, STA_NO_BTC_IN4WAY, WL_EXT_STATUS_DISCONNECTED, NULL);
#endif
	}

#ifdef WL_FILS
	if ((sec->auth_type == NL80211_AUTHTYPE_FILS_SK_PFS) ||
			(sec->auth_type == NL80211_AUTHTYPE_FILS_SK)) {
		if ((err = wl_get_fils_connect_params(cfg, ndev)) != BCME_OK) {
			WL_ERR(("FILS params fetch failed.\n"));
			goto exit;
		}

		if (wl_fillup_resp_params(cfg, ndev, curbssid, &resp_params, status) != BCME_OK) {
			WL_ERR(("connect resp_params failure\n"));
			err = BCME_ERROR;
			goto exit;
		}
		cfg80211_connect_done(ndev, &resp_params, GFP_KERNEL);
	}
	else
#endif /* WL_FILS */
	{
		CFG80211_CONNECT_RESULT(ndev, curbssid, bss,
			conn_info->req_ie, conn_info->req_ie_len,
			conn_info->resp_ie, conn_info->resp_ie_len,
			status, GFP_KERNEL);
	}

	if (completed) {
		LOG_TS(cfg, conn_cmplt);
		LOG_TS(cfg, authorize_start);
		WL_INFORM_MEM(("[%s] Report connect result - "
			"connection succeeded\n", ndev->name));

#ifdef BCMWAPI_WPI
		if (sec->cipher_group == WLAN_CIPHER_SUITE_SMS4) {
			/* In WAPI case, there is no seperate authorized call
				* from upper layer. so set the state from connect done.
				*/
			wl_set_drv_status(cfg, AUTHORIZED, ndev);
			CLR_TS(cfg, authorize_start);
			LOG_TS(cfg, authorize_cmplt);
		}
#endif /* WAPI */
	}

exit:
	CLR_TS(cfg, conn_start);
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

	WL_INFORM_MEM(("[%s] mic fail event - " MACDBG " \n",
		ndev->name, MAC2STRDBG(e->addr.octet)));
	mutex_lock(&cfg->usr_sync);
	if (flags & WLC_EVENT_MSG_GROUP)
		key_type = NL80211_KEYTYPE_GROUP;
	else
		key_type = NL80211_KEYTYPE_PAIRWISE;

	wl_flush_fw_log_buffer(ndev, FW_LOGSET_MASK_ALL);
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

#ifdef WL_CFG80211_GON_COLLISION
static void
wl_gon_req_collision(struct bcm_cfg80211 *cfg, wl_action_frame_t *tx_act_frm,
	wifi_p2p_pub_act_frame_t *rx_act_frm, struct net_device *ndev,
	struct ether_addr sa, struct ether_addr da)
{
	if (cfg->afx_hdl->pending_tx_act_frm == NULL)
		return;

	if (tx_act_frm &&
		wl_cfgp2p_is_pub_action(tx_act_frm->data, tx_act_frm->len)) {
		wifi_p2p_pub_act_frame_t *pact_frm;

		pact_frm = (wifi_p2p_pub_act_frame_t *)tx_act_frm->data;

		if (!(pact_frm->subtype == P2P_PAF_GON_REQ &&
			rx_act_frm->subtype == P2P_PAF_GON_REQ)) {
			return;
		}
	}

	WL_ERR((" GO NEGO Request COLLISION !!! \n"));

	/* if sa(peer) addr is less than da(my) addr,
	 * my device will process peer's gon request and block to send my gon req.
	 *
	 * if not (sa addr > da addr),
	 * my device will process gon request and drop gon req of peer.
	 */
	if (memcmp(sa.octet, da.octet, ETHER_ADDR_LEN) < 0) {
		/* block to send tx gon request */
		cfg->block_gon_req_tx_count = BLOCK_GON_REQ_MAX_NUM;
		WL_ERR((" block to send gon req tx !!!\n"));

		/* if we are finding a common channel for sending af,
		 * do not scan more to block to send current gon req
		 */
		if (wl_get_drv_status_all(cfg, FINDING_COMMON_CHANNEL)) {
			wl_clr_drv_status(cfg, FINDING_COMMON_CHANNEL, ndev);
			complete(&cfg->act_frm_scan);
		}
	} else {
		/* drop gon request of peer to process gon request by my device. */
		WL_ERR((" drop to receive gon req rx !!! \n"));
		cfg->block_gon_req_rx_count = BLOCK_GON_REQ_MAX_NUM;
	}

	return;
}
#endif /* WL_CFG80211_GON_COLLISION */

void
wl_stop_wait_next_action_frame(struct bcm_cfg80211 *cfg, struct net_device *ndev, u8 bsscfgidx)
{
	s32 err = 0;

	if (wl_get_drv_status_all(cfg, FINDING_COMMON_CHANNEL)) {
		del_timer_sync(&cfg->p2p->listen_timer);
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
		/* Scan engine is not used for sending action frames in the latest driver
		 * branches. actframe_abort is used in the latest driver branches
		 * instead of scan abort.
		 *  If actframe_abort iovar succeeds, don't execute scan abort.
		 *  If actframe_abort fails with unsupported error,
		 *  execute scan abort (for backward copmatibility).
		 */
		if (cfg->af_sent_channel) {
			err = wldev_iovar_setint_bsscfg(ndev, "actframe_abort", 1, bsscfgidx);
			if (err < 0) {
				if (err == BCME_UNSUPPORTED) {
					mutex_lock(&cfg->scan_sync);
					wl_cfgscan_scan_abort(cfg);
					mutex_unlock(&cfg->scan_sync);
				} else {
					WL_ERR(("actframe_abort failed. ret:%d\n", err));
				}
			}
		}
	}
#ifdef WL_CFG80211_SYNC_GON
	else if (wl_get_drv_status_all(cfg, WAITING_NEXT_ACT_FRM_LISTEN)) {
		WL_DBG(("*** Wake UP ** abort listen for next af frame\n"));
		/* So abort scan to cancel listen */
		wl_cfgscan_cancel_scan(cfg);
	}
#endif /* WL_CFG80211_SYNC_GON */
}

#if defined(WES_SUPPORT)
int wl_cfg80211_set_wes_mode(struct net_device *dev, int mode)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	cfg->wes_mode = mode;
	return 0;
}

int wl_cfg80211_get_wes_mode(struct net_device *dev)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	return cfg->wes_mode;
}

bool wl_cfg80211_is_wes(void *frame, u32 frame_len)
{
	unsigned char *data;

	if (frame == NULL) {
		WL_ERR(("Invalid frame \n"));
		return false;
	}

	if (frame_len < 4) {
		WL_ERR(("Invalid frame length [%d] \n", frame_len));
		return false;
	}

	data = frame;

	if (memcmp(data, "\x7f\x00\x00\xf0", 4) == 0) {
		WL_DBG(("Receive WES VS Action Frame \n"));
		return true;
	}

	return false;
}

int
wl_cfg80211_set_ncho_mode(struct net_device *dev, int mode)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	cfg->ncho_mode = mode;
	return BCME_OK;
}

int
wl_cfg80211_get_ncho_mode(struct net_device *dev)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	return cfg->ncho_mode;
}
#endif /* WES_SUPPORT */

int wl_cfg80211_get_ioctl_version(void)
{
	return ioctl_version;
}

static s32
wl_notify_rx_mgmt_frame(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	struct ether_addr da;
	struct ether_addr bssid;
	bool isfree = false;
	s32 err = 0;
	s32 freq;
	struct net_device *ndev = NULL;
	wifi_p2p_pub_act_frame_t *act_frm = NULL;
	wifi_p2p_action_frame_t *p2p_act_frm = NULL;
	wifi_p2psd_gas_pub_act_frame_t *sd_act_frm = NULL;
	wl_event_rx_frame_data_t *rxframe;
	u32 event;
	u8 *mgmt_frame;
	u8 bsscfgidx;
	u32 mgmt_frame_len;
	chanspec_t chspec;
#if defined(BCMDONGLEHOST) && defined(TDLS_MSG_ONLY_WFD) && defined(WLTDLS)
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
#endif /* BCMDONGLEHOST && TDLS_MSG_ONLY_WFD && WLTDLS */
	if (ntoh32(e->datalen) < sizeof(wl_event_rx_frame_data_t)) {
		WL_ERR(("wrong datalen:%d\n", ntoh32(e->datalen)));
		return -EINVAL;
	}
	mgmt_frame_len = ntoh32(e->datalen) - (uint32)sizeof(wl_event_rx_frame_data_t);
	event = ntoh32(e->event_type);
	bsscfgidx = e->bsscfgidx;
	rxframe = (wl_event_rx_frame_data_t *)data;
	if (!rxframe) {
		WL_ERR(("rxframe: NULL\n"));
		return -EINVAL;
	}
	chspec = ntoh16(rxframe->channel);
	bzero(&bssid, ETHER_ADDR_LEN);
	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);
	if ((ndev->ieee80211_ptr->iftype != NL80211_IFTYPE_AP) &&
		(event == WLC_E_PROBREQ_MSG)) {
		/* Probe req event comes on wlan0 interface even though
		 * the frame have been received on correct interface(AP)
		 * in firmware. Find the right interface to pass it up.
		 * Required for WPS-AP certification 4.2.13.
		 *  TODO/Need a better fix. Current fix doesn't take
		 * care of dual AP/GO scenarios.
		 */
		struct net_info *iter, *next;
		GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
		for_each_ndev(cfg, iter, next) {
			GCC_DIAGNOSTIC_POP();
			if (iter->ndev && iter->wdev &&
					iter->wdev->iftype == NL80211_IFTYPE_AP) {
					ndev = iter->ndev;
					cfgdev =  ndev_to_cfgdev(ndev);
					break;
			}
		}
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 39) && !defined(WL_COMPAT_WIRELESS)
	freq = ieee80211_channel_to_frequency(CHSPEC_CHANNEL(chspec));
#else
	freq = wl_channel_to_frequency(wf_chspec_ctlchan(chspec), CHSPEC_BAND(chspec));
#endif
	if (event == WLC_E_ACTION_FRAME_RX) {
		u8 ioctl_buf[WLC_IOCTL_SMLEN];
		if ((err = wldev_iovar_getbuf_bsscfg(ndev, "cur_etheraddr",
				NULL, 0, ioctl_buf, sizeof(ioctl_buf), bsscfgidx,
				NULL)) != BCME_OK) {
			WL_ERR(("WLC_GET_CUR_ETHERADDR failed, error %d\n", err));
			goto exit;
		}

		err = wldev_ioctl_get(ndev, WLC_GET_BSSID, &bssid, ETHER_ADDR_LEN);
		if (err < 0)
			 WL_ERR(("WLC_GET_BSSID error %d\n", err));
		memcpy(da.octet, ioctl_buf, ETHER_ADDR_LEN);
		err = wl_frame_get_mgmt(cfg, FC_ACTION, &da, &e->addr, &bssid,
			&mgmt_frame, &mgmt_frame_len,
			(u8 *)((wl_event_rx_frame_data_t *)rxframe + 1));
		if (err < 0) {
			WL_ERR(("Error in receiving action frame len %d channel %d freq %d\n",
				mgmt_frame_len, CHSPEC_CHANNEL(chspec), freq));
			goto exit;
		}
		isfree = true;

		wl_cfgp2p_print_actframe(false, &mgmt_frame[DOT11_MGMT_HDR_LEN],
			mgmt_frame_len - DOT11_MGMT_HDR_LEN, CHSPEC_CHANNEL(chspec));

		if (wl_cfgp2p_is_pub_action(&mgmt_frame[DOT11_MGMT_HDR_LEN],
			mgmt_frame_len - DOT11_MGMT_HDR_LEN)) {
			act_frm = (wifi_p2p_pub_act_frame_t *)
					(&mgmt_frame[DOT11_MGMT_HDR_LEN]);
		} else if (wl_cfgp2p_is_p2p_action(&mgmt_frame[DOT11_MGMT_HDR_LEN],
			mgmt_frame_len - DOT11_MGMT_HDR_LEN)) {
			p2p_act_frm = (wifi_p2p_action_frame_t *)
					(&mgmt_frame[DOT11_MGMT_HDR_LEN]);
			(void) p2p_act_frm;
		} else if (wl_cfg80211_is_dpp_gas_action(&mgmt_frame[DOT11_MGMT_HDR_LEN],
			mgmt_frame_len - DOT11_MGMT_HDR_LEN)) {
			wl_clr_drv_status(cfg, WAITING_NEXT_ACT_FRM, ndev);

			/* Stop waiting for next AF. */
			wl_stop_wait_next_action_frame(cfg, ndev, bsscfgidx);
		} else if (wl_cfgp2p_is_gas_action(&mgmt_frame[DOT11_MGMT_HDR_LEN],
			mgmt_frame_len - DOT11_MGMT_HDR_LEN)) {
#ifdef WL_SDO
			if (wl_get_p2p_status(cfg, DISC_IN_PROGRESS)) {
				WL_ERR(("SD offload is in progress. Don't report the"
					"frame via rx_mgmt path\n"));
				goto exit;
			}
#endif
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
#ifdef BCMDONGLEHOST
			if (!dhdp->tdls_mode) {
				WL_DBG((" TDLS Frame filtered \n"));
				goto exit;
			}
#endif /* BCMDONGLEHOST */
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
			uint8 offset = DOT11_MGMT_HDR_LEN + DOT11_ACTION_FRMHDR_LEN;
			if ((qos_map_ie = bcm_parse_tlvs(&mgmt_frame[offset],
					mgmt_frame_len - offset, DOT11_MNG_QOS_MAP_ID)) != NULL) {
				WL_DBG((" QoS map set IE found in QoS action frame\n"));
				if (!cfg->up_table) {
					cfg->up_table = (uint8 *)MALLOC(cfg->osh, UP_TABLE_MAX);
				}
				wl_set_up_table(cfg->up_table, qos_map_ie);
			} else {
				MFREE(cfg->osh, cfg->up_table, UP_TABLE_MAX);
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
			/* WAR : There is no way to identify DA of action frame as of now.
			 *  We have to modify firmware code to include DA and SA of Act frame
			 *  as event data
			 */
			/*
			 *  if we got normal action frame and ndev is p2p0,
			 *  we have to change ndev from p2p0 to wlan0
			 */
#if defined(WES_SUPPORT)
			if (wl_cfg80211_is_wes(&mgmt_frame[DOT11_MGMT_HDR_LEN],
			mgmt_frame_len - DOT11_MGMT_HDR_LEN) && cfg->wes_mode == 0) {
			/* Ignore WES VS Action frame */
			goto exit;
			}
#endif /* WES_SUPPORT */

			/* We need to check proper action frame is received */
			if (cfg->next_af_subtype != WL_PUB_AF_STYPE_INVALID) {
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
#ifdef WL_CFG80211_GON_COLLISION
			if (act_frm->subtype == P2P_PAF_GON_REQ) {
				wl_gon_req_collision(cfg,
					&cfg->afx_hdl->pending_tx_act_frm->action_frame,
					act_frm, ndev, e->addr, da);

				if (cfg->block_gon_req_rx_count) {
					WL_ERR(("drop frame GON Req Rx : count (%d)\n",
						cfg->block_gon_req_rx_count));
					cfg->block_gon_req_rx_count--;
					goto exit;
				}
			} else if (act_frm->subtype == P2P_PAF_GON_CONF) {
				/* if go formation done, clear it */
				cfg->block_gon_req_tx_count = 0;
				cfg->block_gon_req_rx_count = 0;
			}
#endif /* WL_CFG80211_GON_COLLISION */

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
				} else if ((cfg->next_af_subtype == P2P_PAF_GON_RSP) &&
						(act_frm->subtype == P2P_PAF_GON_REQ)) {
					/* If current received frame is GO NEG REQ and next
					 * expected frame is GO NEG RESP, do not send it up.
					 */
					WL_ERR(("GO Neg req received while waiting for RESP."
						"Discard incoming frame\n"));
					goto exit;
				}
			}
		}

		if (wl_cfg80211_is_dpp_frame(
			(void *)&mgmt_frame[DOT11_MGMT_HDR_LEN],
			mgmt_frame_len - DOT11_MGMT_HDR_LEN)) {
			wl_dpp_pa_frame_t *pa =
				(wl_dpp_pa_frame_t *)&mgmt_frame[DOT11_MGMT_HDR_LEN];
			if (cfg->next_af_subtype == pa->ftype) {
				WL_DBG(("matching dpp frm (%d) found. abort dwell\n", pa->ftype));
				wl_clr_drv_status(cfg, WAITING_NEXT_ACT_FRM, ndev);

				/* Stop waiting for next AF. */
				wl_stop_wait_next_action_frame(cfg, ndev, bsscfgidx);
			}
		}
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
			wl_validate_wps_ie(
				(const char *)prbreq_ies.wps_ie, prbreq_ies.wps_ie_len, &pbc);
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

#ifdef WL_CFG80211_USE_PRB_REQ_FOR_AF_TX
			if (WL_DRV_STATUS_SENDING_AF_FRM_EXT(cfg) &&
				!memcmp(cfg->afx_hdl->tx_dst_addr.octet, e->addr.octet,
				ETHER_ADDR_LEN)) {
				if (cfg->afx_hdl->pending_tx_act_frm &&
					wl_get_drv_status_all(cfg, FINDING_COMMON_CHANNEL)) {
					chanspec_t channel = hton16(rxframe->channel);
					WL_DBG(("PROBE REQUEST : Peer found, channel : %d\n",
						channel));
					cfg->afx_hdl->peer_chan = channel;
					complete(&cfg->act_frm_scan);
				}
			}
#endif /* WL_CFG80211_USE_PRB_REQ_FOR_AF_TX */

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
		mgmt_frame_len, ntoh32(e->datalen), CHSPEC_CHANNEL(chspec), freq));
exit:
	if (isfree) {
		MFREE(cfg->osh, mgmt_frame, mgmt_frame_len);
	}
	return err;
}

static void
wl_map_brcm_specifc_country_code(char *country_code)
{
	/* If country code is default locale, change the domain to world domain
	 * Default locale formats: AA, ZZ, XA-XZ, QM-QZ
	 */
	if (!strcmp("AA", country_code) || !strcmp("ZZ", country_code) ||
			((country_code[0] == 'X') && (country_code[1] >= 'A') &&
			(country_code[1] <= 'Z')) || ((country_code[0] == 'Q') &&
			(country_code[1] >= 'M') && (country_code[1] <= 'Z'))) {
		WL_DBG(("locale mapped to world domain\n"));
		country_code[0] = '0';
		country_code[1] = '0';
	}
}

static s32
wl_cfg80211_ccode_evt_handler(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *event, void *data)
{
	s32 err = 0;
	struct wiphy *wiphy = bcmcfg_to_wiphy(cfg);
	char country_str[WLC_CNTRY_BUF_SZ] = { 0 };
	struct net_device *dev = bcmcfg_to_prmry_ndev(cfg);
	wl_country_t cspec = {{0}, 0, {0}};

	if (strlcpy(country_str, data, WL_CCODE_LEN + 1) >= WLC_CNTRY_BUF_SZ) {
		return -EINVAL;
	}

	if (strncmp(cfg->country, country_str, WL_CCODE_LEN) == 0) {
		/* If country code is updated from command context, skip wiphy update */
		WL_MSG(dev->name, "No change in country (%s)\n", country_str);
		return BCME_OK;
	}

	WL_MSG(dev->name, "Updating new country %s\n", country_str);

	strlcpy(cspec.country_abbrev, country_str, WL_CCODE_LEN + 1);
	strlcpy(cspec.ccode, country_str, WL_CCODE_LEN + 1);
	err = dhd_conf_map_country_list(dhd_get_pub(dev), &cspec);
	if (err)
		dhd_get_customized_country_code(dev, (char *)&cspec.country_abbrev, &cspec);
	err = dhd_conf_set_country(dhd_get_pub(dev), &cspec);
	if (err < 0) {
		WL_ERR(("set country for %s as %s rev %d failed\n",
			country_str, cspec.ccode, cspec.rev));
	}
	dhd_conf_fix_country(dhd_get_pub(dev));

	/* Indicate to upper layer for regdom change */
	err = wl_update_wiphybands(cfg, true);
	if (err != BCME_OK) {
		WL_ERR(("update wiphy bands failed\n"));
		return err;
	}

	wl_map_brcm_specifc_country_code(country_str);

	if (!IS_REGDOM_SELF_MANAGED(wiphy)) {
		err = regulatory_hint(wiphy, country_str);
		if (err) {
			WL_ERR(("update country change failed\n"));
			return err;
		}
		WL_DBG_MEM(("regulatory hint notified for ccode change\n"));
	}

	return err;
}

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

	if (!profile) {
		WL_ERR(("profile null\n"));
		return;
	}

	WL_CFG_DRV_LOCK(&cfg->cfgdrv_lock, flags);
	bzero(profile, sizeof(struct wl_profile));
	WL_CFG_DRV_UNLOCK(&cfg->cfgdrv_lock, flags);
}

static void wl_init_event_handler(struct bcm_cfg80211 *cfg)
{
	bzero(cfg->evt_handler, sizeof(cfg->evt_handler));

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
	cfg->evt_handler[WLC_E_AUTH_IND] = wl_notify_connect_status;
	cfg->evt_handler[WLC_E_ASSOC_RESP_IE] = wl_notify_connect_status;
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
#ifdef WL_SDO
	cfg->evt_handler[WLC_E_SERVICE_FOUND] = wl_svc_resp_handler;
	cfg->evt_handler[WLC_E_P2PO_ADD_DEVICE] = wl_notify_device_discovery;
	cfg->evt_handler[WLC_E_P2PO_DEL_DEVICE] = wl_notify_device_discovery;
#endif
#ifdef WLTDLS
	cfg->evt_handler[WLC_E_TDLS_PEER_EVENT] = wl_tdls_event_handler;
#endif /* WLTDLS */
	cfg->evt_handler[WLC_E_BSSID] = wl_notify_roaming_status;
#ifdef WLAIBSS
	cfg->evt_handler[WLC_E_AIBSS_TXFAIL] = wl_notify_aibss_txfail;
#endif /* WLAIBSS */
#ifdef	WL_RELMCAST
	cfg->evt_handler[WLC_E_RMC_EVENT] = wl_notify_rmc_status;
#endif /* WL_RELMCAST */
#ifdef BT_WIFI_HANDOVER
	cfg->evt_handler[WLC_E_BT_WIFI_HANDOVER_REQ] = wl_notify_bt_wifi_handover_req;
#endif
#ifdef WL_NAN
	cfg->evt_handler[WLC_E_NAN_CRITICAL] = wl_cfgnan_notify_nan_status;
	cfg->evt_handler[WLC_E_NAN_NON_CRITICAL] = wl_cfgnan_notify_nan_status;
#endif /* WL_NAN */
	cfg->evt_handler[WLC_E_CSA_COMPLETE_IND] = wl_csa_complete_ind;
	cfg->evt_handler[WLC_E_AP_STARTED] = wl_ap_start_ind;
#ifdef CUSTOM_EVENT_PM_WAKE
	cfg->evt_handler[WLC_E_EXCESS_PM_WAKE_EVENT] = wl_check_pmstatus;
#endif	/* CUSTOM_EVENT_PM_WAKE */
#if defined(DHD_LOSSLESS_ROAMING) || defined (DBG_PKT_MON)
	cfg->evt_handler[WLC_E_ROAM_PREP] = wl_notify_roam_prep_status;
#endif /* DHD_LOSSLESS_ROAMING || DBG_PKT_MON  */
	cfg->evt_handler[WLC_E_ROAM_START] = wl_notify_roam_start_status;
#ifdef WL_BAM
	cfg->evt_handler[WLC_E_ADPS] = wl_adps_event_handler;
#endif	/* WL_BAM */
	cfg->evt_handler[WLC_E_PSK_SUP] = wl_cfg80211_sup_event_handler;
	cfg->evt_handler[WLC_E_COUNTRY_CODE_CHANGED] = wl_cfg80211_ccode_evt_handler;
#ifdef WL_BCNRECV
	cfg->evt_handler[WLC_E_BCNRECV_ABORTED] = wl_bcnrecv_aborted_event_handler;
#endif /* WL_BCNRECV */
#ifdef WL_MBO
	cfg->evt_handler[WLC_E_MBO] = wl_mbo_event_handler;
#endif  /* WL_MBO */
#ifdef WL_CAC_TS
	cfg->evt_handler[WLC_E_ADDTS_IND] = wl_cfg80211_cac_event_handler;
	cfg->evt_handler[WLC_E_DELTS_IND] = wl_cfg80211_cac_event_handler;
#endif /* WL_CAC_TS */
#if defined(WL_MBO) || defined(WL_OCE)
	cfg->evt_handler[WLC_E_PRUNE] = wl_bssid_prune_event_handler;
#endif /* WL_MBO || WL_OCE */
#ifdef RTT_SUPPORT
	cfg->evt_handler[WLC_E_PROXD] = wl_cfg80211_rtt_event_handler;
#endif
#ifdef WL_CHAN_UTIL
	cfg->evt_handler[WLC_E_BSS_LOAD] = wl_cfg80211_bssload_report_event_handler;
#endif /* WL_CHAN_UTIL */
#ifdef WL_TWT
	cfg->evt_handler[WLC_E_TWT_SETUP] = wl_notify_twt_event;
	cfg->evt_handler[WLC_E_TWT_TEARDOWN] = wl_notify_twt_event;
	cfg->evt_handler[WLC_E_TWT_INFO_FRM] = wl_notify_twt_event;
#endif /* WL_TWT */
#ifdef WL_CLIENT_SAE
	cfg->evt_handler[WLC_E_JOIN_START] = wl_notify_start_auth;
#endif /* WL_CLIENT_SAE */
}

#if defined(STATIC_WL_PRIV_STRUCT)
static int
wl_init_escan_result_buf(struct bcm_cfg80211 *cfg)
{
#ifdef DUAL_ESCAN_RESULT_BUFFER
	cfg->escan_info.escan_buf[0] = DHD_OS_PREALLOC(cfg->pub,
		DHD_PREALLOC_WIPHY_ESCAN0, ESCAN_BUF_SIZE);
	if (cfg->escan_info.escan_buf[0] == NULL) {
		WL_ERR(("Failed to alloc ESCAN_BUF0\n"));
		return -ENOMEM;
	}

	cfg->escan_info.escan_buf[1] = DHD_OS_PREALLOC(cfg->pub,
		DHD_PREALLOC_WIPHY_ESCAN1, ESCAN_BUF_SIZE);
	if (cfg->escan_info.escan_buf[1] == NULL) {
		WL_ERR(("Failed to alloc ESCAN_BUF1\n"));
		return -ENOMEM;
	}

	bzero(cfg->escan_info.escan_buf[0], ESCAN_BUF_SIZE);
	bzero(cfg->escan_info.escan_buf[1], ESCAN_BUF_SIZE);
	cfg->escan_info.escan_type[0] = 0;
	cfg->escan_info.escan_type[1] = 0;
#else
	cfg->escan_info.escan_buf = DHD_OS_PREALLOC(cfg->pub,
		DHD_PREALLOC_WIPHY_ESCAN0, ESCAN_BUF_SIZE);
	if (cfg->escan_info.escan_buf == NULL) {
		WL_ERR(("Failed to alloc ESCAN_BUF\n"));
		return -ENOMEM;
	}
	bzero(cfg->escan_info.escan_buf, ESCAN_BUF_SIZE);
#endif /* DUAL_ESCAN_RESULT_BUFFER */

	return 0;
}

static void
wl_deinit_escan_result_buf(struct bcm_cfg80211 *cfg)
{
#ifdef DUAL_ESCAN_RESULT_BUFFER
	if (cfg->escan_info.escan_buf[0] != NULL) {
		cfg->escan_info.escan_buf[0] = NULL;
		cfg->escan_info.escan_type[0] = 0;
	}

	if (cfg->escan_info.escan_buf[1] != NULL) {
		cfg->escan_info.escan_buf[1] = NULL;
		cfg->escan_info.escan_type[1] = 0;
	}
#else
	if (cfg->escan_info.escan_buf != NULL) {
		cfg->escan_info.escan_buf = NULL;
	}
#endif /* DUAL_ESCAN_RESULT_BUFFER */
}
#endif /* STATIC_WL_PRIV_STRUCT */

static s32 wl_init_priv_mem(struct bcm_cfg80211 *cfg)
{
	WL_DBG(("Enter \n"));

	cfg->scan_results = (wl_scan_results_t *)MALLOCZ(cfg->osh,
		WL_SCAN_BUF_MAX);
	if (unlikely(!cfg->scan_results)) {
		WL_ERR(("Scan results alloc failed\n"));
		goto init_priv_mem_out;
	}
	cfg->conf = (struct wl_conf *)MALLOCZ(cfg->osh, sizeof(*cfg->conf));
	if (unlikely(!cfg->conf)) {
		WL_ERR(("wl_conf alloc failed\n"));
		goto init_priv_mem_out;
	}
	cfg->scan_req_int = (void *)MALLOCZ(cfg->osh,
		sizeof(*cfg->scan_req_int));
	if (unlikely(!cfg->scan_req_int)) {
		WL_ERR(("Scan req alloc failed\n"));
		goto init_priv_mem_out;
	}
	cfg->ioctl_buf = (u8 *)MALLOCZ(cfg->osh, WLC_IOCTL_MAXLEN);
	if (unlikely(!cfg->ioctl_buf)) {
		WL_ERR(("Ioctl buf alloc failed\n"));
		goto init_priv_mem_out;
	}
	cfg->escan_ioctl_buf = (void *)MALLOCZ(cfg->osh, WLC_IOCTL_MAXLEN);
	if (unlikely(!cfg->escan_ioctl_buf)) {
		WL_ERR(("Ioctl buf alloc failed\n"));
		goto init_priv_mem_out;
	}
	cfg->extra_buf = (void *)MALLOCZ(cfg->osh, WL_EXTRA_BUF_MAX);
	if (unlikely(!cfg->extra_buf)) {
		WL_ERR(("Extra buf alloc failed\n"));
		goto init_priv_mem_out;
	}
	cfg->pmk_list = (void *)MALLOCZ(cfg->osh, sizeof(*cfg->pmk_list));
	if (unlikely(!cfg->pmk_list)) {
		WL_ERR(("pmk list alloc failed\n"));
		goto init_priv_mem_out;
	}
#if defined(STATIC_WL_PRIV_STRUCT)
	cfg->conn_info = (void *)MALLOCZ(cfg->osh, sizeof(*cfg->conn_info));
	if (unlikely(!cfg->conn_info)) {
		WL_ERR(("cfg->conn_info alloc failed\n"));
		goto init_priv_mem_out;
	}
	cfg->ie = (void *)MALLOC(cfg->osh, sizeof(*cfg->ie));
	if (unlikely(!cfg->ie)) {
		WL_ERR(("cfg->ie alloc failed\n"));
		goto init_priv_mem_out;
	}
	if (unlikely(wl_init_escan_result_buf(cfg))) {
		WL_ERR(("Failed to init escan resul buf\n"));
		goto init_priv_mem_out;
	}
#endif /* STATIC_WL_PRIV_STRUCT */
	cfg->afx_hdl = (void *)MALLOCZ(cfg->osh, sizeof(*cfg->afx_hdl));
	if (unlikely(!cfg->afx_hdl)) {
		WL_ERR(("afx hdl alloc failed\n"));
		goto init_priv_mem_out;
	} else {
		init_completion(&cfg->act_frm_scan);
		init_completion(&cfg->wait_next_af);

		INIT_WORK(&cfg->afx_hdl->work, wl_cfg80211_afx_handler);
	}
#ifdef WLTDLS
	if (cfg->tdls_mgmt_frame) {
		MFREE(cfg->osh, cfg->tdls_mgmt_frame, cfg->tdls_mgmt_frame_len);
		cfg->tdls_mgmt_frame = NULL;
		cfg->tdls_mgmt_frame_len = 0;
	}
#endif /* WLTDLS */
	cfg->spmk_info_list = (void *)MALLOCZ(cfg->osh, sizeof(*cfg->spmk_info_list));
	if (unlikely(!cfg->spmk_info_list)) {
		WL_ERR(("Single PMK info list allocation falure\n"));
		goto init_priv_mem_out;
	}

	return 0;

init_priv_mem_out:
	wl_deinit_priv_mem(cfg);

	return -ENOMEM;
}

static void wl_deinit_priv_mem(struct bcm_cfg80211 *cfg)
{
	MFREE(cfg->osh, cfg->scan_results, WL_SCAN_BUF_MAX);
	MFREE(cfg->osh, cfg->conf, sizeof(*cfg->conf));
	MFREE(cfg->osh, cfg->scan_req_int, sizeof(*cfg->scan_req_int));
	MFREE(cfg->osh, cfg->ioctl_buf, WLC_IOCTL_MAXLEN);
	MFREE(cfg->osh, cfg->escan_ioctl_buf, WLC_IOCTL_MAXLEN);
	MFREE(cfg->osh, cfg->extra_buf, WL_EXTRA_BUF_MAX);
	MFREE(cfg->osh, cfg->pmk_list, sizeof(*cfg->pmk_list));
#if defined(STATIC_WL_PRIV_STRUCT)
	MFREE(cfg->osh, cfg->conn_info, sizeof(*cfg->conn_info));
	MFREE(cfg->osh, cfg->ie, sizeof(*cfg->ie));
	wl_deinit_escan_result_buf(cfg);
#endif /* STATIC_WL_PRIV_STRUCT */
	if (cfg->afx_hdl) {
#if defined(BCMDONGLEHOST)
		cancel_work_sync(&cfg->afx_hdl->work);
#endif
		MFREE(cfg->osh, cfg->afx_hdl, sizeof(*cfg->afx_hdl));
	}
	MFREE(cfg->osh, cfg->spmk_info_list, sizeof(*cfg->spmk_info_list));

}

static s32 wl_create_event_handler(struct bcm_cfg80211 *cfg)
{
	int ret = 0;
	WL_DBG(("Enter \n"));

	/* making separate work queue needs GPL license,
	 * but some drivers are not in GPL license, so, making seperate queue Android only
	 */

#ifdef OEM_ANDROID
	/* Allocate workqueue for event */
	if (!cfg->event_workq) {
		cfg->event_workq = alloc_workqueue("dhd_eventd",
			WQ_MEM_RECLAIM | WQ_HIGHPRI | WQ_UNBOUND, 1);
	}

	if (!cfg->event_workq) {
		WL_ERR(("event_workq alloc_workqueue failed\n"));
		ret = -ENOMEM;
	} else {
		INIT_WORK(&cfg->event_work, wl_event_handler);
	}
#endif /* OEM_ANDROID */

#ifndef OEM_ANDROID
		INIT_WORK(&cfg->event_work, wl_event_handler);
		cfg->event_workq_init = true;
#endif /* OEM_ANDROID */
	return ret;
}

static void wl_destroy_event_handler(struct bcm_cfg80211 *cfg)
{

#ifdef OEM_ANDROID
	if (cfg && cfg->event_workq) {
		cancel_work_sync(&cfg->event_work);
		destroy_workqueue(cfg->event_workq);
		cfg->event_workq = NULL;
	}
#endif /* OEM_ANDROID */

#ifndef OEM_ANDROID
	if (cfg && cfg->event_workq_init) {
#ifdef BCMDONGLEHOST
		cancel_work_sync(&cfg->event_work);
#endif /* BCMDONGLEHOST */
		cfg->event_workq_init = false;
	}
#endif /* OEM_ANDROID */
}

void wl_terminate_event_handler(struct net_device *dev)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	if (cfg) {
		wl_destroy_event_handler(cfg);
		wl_flush_eq(cfg);
	}
}

#ifdef DHD_LOSSLESS_ROAMING
static void wl_del_roam_timeout(struct bcm_cfg80211 *cfg)
{
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);

	/* restore prec_map to ALLPRIO */
	dhdp->dequeue_prec_map = ALLPRIO;
	del_timer_sync(&cfg->roam_timeout);
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

#if defined(CONFIG_WLAN_BEYONDX) || defined(CONFIG_SEC_5GMODEL)
#define CP_CHAN_INFO_RAT_MODE_LTE	3
#define CP_CHAN_INFO_RAT_MODE_NR5G	7
int g_mhs_chan_for_cpcoex = 0;

struct __packed cam_cp_noti_info {
	u8 rat;
	u32 band;
	u32 channel;
};

int
wl_cfg80211_send_msg_to_ril()
{
	int id, buf = 1;

	id = IPC_SYSTEM_CP_CHANNEL_INFO;
	dev_ril_bridge_send_msg(id, sizeof(int), &buf);
	WL_ERR(("[BeyondX] send message to ril.\n"));

	OSL_SLEEP(500);
	return 0;
}

int
wl_cfg80211_ril_bridge_notifier_call(struct notifier_block *nb,
	unsigned long size, void *buf)
{
	struct dev_ril_bridge_msg *msg;
	struct cam_cp_noti_info *cp_noti_info;
	static int mhs_channel_for_4g, mhs_channel_for_5g;
	static int recv_msg_4g, recv_msg_5g;

	WL_ERR(("[BeyondX] receive message from ril.\n"));
	msg = (struct dev_ril_bridge_msg *)buf;

	if (msg->dev_id == IPC_SYSTEM_CP_CHANNEL_INFO &&
		msg->data_len <= sizeof(struct cam_cp_noti_info)) {
		u8 rat;
		u32 band;
		u32 channel;

		cp_noti_info = (struct cam_cp_noti_info *)msg->data;
		rat = cp_noti_info->rat;
		band = cp_noti_info->band;
		channel = cp_noti_info->channel;

		/* LTE/5G Band/Freq information => Mobile Hotspot channel mapping.
		 * LTE/B40: 38650~39649 => Ch.11
		 * LTE/B41: 39650~41589 => Ch.1
		 * 5G/N41: 499200~537999 => Ch.1
		 */
		if (rat == CP_CHAN_INFO_RAT_MODE_LTE) {
			recv_msg_4g = 1;
			if (channel >= 38650 && channel <= 39649) {
				mhs_channel_for_4g = 11;
			} else if (channel >= 39650 && channel <= 41589) {
				mhs_channel_for_4g = 1;
			}
		}
		if (rat == CP_CHAN_INFO_RAT_MODE_NR5G) {
			recv_msg_5g = 1;
			if (channel >= 499200 && channel <= 537999) {
				mhs_channel_for_5g = 1;
			}
		}

		WL_DBG(("[BeyondX] rat: %u, band: %u, channel: %u, mhs_channel_for_4g: %u, "
			"mhs_channel_for_5g: %u\n", rat, band, channel,
			mhs_channel_for_4g, mhs_channel_for_5g));

		if (recv_msg_4g && recv_msg_5g) {
			if (mhs_channel_for_4g && mhs_channel_for_5g) {
				/* if 4G/B40 + 5G/N41, select channel 6 for MHS */
				if (mhs_channel_for_4g == 11 && mhs_channel_for_5g == 1) {
					g_mhs_chan_for_cpcoex = 6;
				/* if 4G(except for B40) + 5G/N41, select channel 1 for MHS */
				} else {
					g_mhs_chan_for_cpcoex = 1;
				}
			} else {
				g_mhs_chan_for_cpcoex = mhs_channel_for_4g ? mhs_channel_for_4g :
					mhs_channel_for_5g ? mhs_channel_for_5g : 0;
			}
			mhs_channel_for_4g = mhs_channel_for_5g = 0;
			recv_msg_4g = recv_msg_5g = 0;
		}
	}

	return 0;
}

static struct notifier_block wl_cfg80211_ril_bridge_notifier = {
	.notifier_call = wl_cfg80211_ril_bridge_notifier_call,
};

static bool wl_cfg80211_ril_bridge_notifier_registered = FALSE;
#endif /* CONFIG_WLAN_BEYONDX || defined(CONFIG_SEC_5GMODEL) */

static s32
wl_cfg80211_netdev_notifier_call(struct notifier_block * nb,
	unsigned long state, void *ptr)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 11, 0))
	struct net_device *dev = ptr;
#else
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
#endif /* LINUX_VERSION < VERSION(3, 11, 0) */
	struct wireless_dev *wdev = NULL;
	struct bcm_cfg80211 *cfg = NULL;

	WL_DBG(("Enter state:%lu  ndev%p \n", state, dev));
	if (!dev) {
		WL_ERR(("dev null\n"));
		return NOTIFY_DONE;
	}

	wdev = ndev_to_wdev(dev);
	if (!wdev) {
		WL_ERR(("wdev null. Do nothing\n"));
		return NOTIFY_DONE;
	}

	cfg = (struct bcm_cfg80211 *)wiphy_priv(wdev->wiphy);
	if (!cfg || (cfg != wl_cfg80211_get_bcmcfg())) {
		/* If cfg80211 priv is null or doesn't match return */
		WL_ERR(("wrong cfg ptr (%p)\n", cfg));
		return NOTIFY_DONE;
	}

	if (dev == bcmcfg_to_prmry_ndev(cfg)) {
		/* Nothing to be done for primary I/F */
		return NOTIFY_DONE;
	}

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
			wl_cfg80211_clear_per_bss_ies(cfg, wdev);
			/* after calling list_del_rcu(&wdev->list) */
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
				wl_cfgscan_cancel_scan(cfg);
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

void wl_cfg80211_concurrent_roam(struct bcm_cfg80211 *cfg, int enable)
{
	u32 connected_cnt  = wl_get_drv_status_all(cfg, CONNECTED);
	bool p2p_connected  = wl_cfgp2p_vif_created(cfg);
#ifdef WL_NAN
	bool nan_connected  = wl_cfgnan_is_dp_active(bcmcfg_to_prmry_ndev(cfg));
#endif /* WL_NAN */
	struct net_info *iter, *next;

	if (!(cfg->roam_flags & WL_ROAM_OFF_ON_CONCURRENT))
		return;

	WL_DBG(("roam off:%d p2p_connected:%d connected_cnt:%d \n",
		enable, p2p_connected, connected_cnt));
	/* Disable FW roam when we have a concurrent P2P connection */
	if (enable &&
		((p2p_connected && connected_cnt > 1) ||
#ifdef WL_NAN
		nan_connected ||
#endif /* WL_NAN */
		FALSE)) {

		/* Mark it as to be reverted */
		cfg->roam_flags |= WL_ROAM_REVERT_STATUS;
		GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
		for_each_ndev(cfg, iter, next) {
			GCC_DIAGNOSTIC_POP();
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
	}
	else if (!enable && (cfg->roam_flags & WL_ROAM_REVERT_STATUS)) {
		cfg->roam_flags &= ~WL_ROAM_REVERT_STATUS;
		GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
		for_each_ndev(cfg, iter, next) {
			GCC_DIAGNOSTIC_POP();
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
	}

	return;
}

static void wl_cfg80211_determine_vsdb_mode(struct bcm_cfg80211 *cfg)
{
	struct net_info *iter, *next;
#ifdef WLEASYMESH
	struct net_device *primary_dev;
	dhd_pub_t *dhd = cfg->pub;
#endif /* WLEASYMESH */
	u32 ctl_chan = 0;
	u32 chanspec = 0;
	u32 pre_ctl_chan = 0;
	u32 band = 0;
	u32 pre_band = 0;
	u32 connected_cnt  = wl_get_drv_status_all(cfg, CONNECTED);
	cfg->vsdb_mode = false;

	if (connected_cnt <= 1)  {
		return;
	}
	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	for_each_ndev(cfg, iter, next) {
		GCC_DIAGNOSTIC_POP();
		/* p2p discovery iface ndev could be null */
		if (iter->ndev) {
			chanspec = 0;
			ctl_chan = 0;
			if (wl_get_drv_status(cfg, CONNECTED, iter->ndev)) {
				if (wldev_iovar_getint(iter->ndev, "chanspec",
					(s32 *)&chanspec) == BCME_OK) {
					chanspec = wl_chspec_driver_to_host(chanspec);
					ctl_chan = wf_chspec_ctlchan(chanspec);
					band = CHSPEC_BAND(chanspec);
					wl_update_prof(cfg, iter->ndev, NULL,
						&chanspec, WL_PROF_CHAN);
				}
				if (!cfg->vsdb_mode) {
					if (!pre_ctl_chan && ctl_chan) {
						pre_ctl_chan = ctl_chan;
						pre_band = band;
					} else if (pre_ctl_chan && (pre_ctl_chan != ctl_chan) &&
						(band == pre_band)) {
						cfg->vsdb_mode = true;
					}
				}
			}
		}
	}
#ifdef WLEASYMESH
    if(dhd->conf->fw_type == FW_TYPE_EZMESH && cfg->vsdb_mode) {
		primary_dev = bcmcfg_to_prmry_ndev(cfg);
		WL_MSG("wlan", "check primary chanspec\n");
		if (wldev_iovar_getint(primary_dev, "chanspec", (s32 *)&chanspec) == BCME_OK) {
			//chanspec = wl_chspec_driver_to_host(chanspec);
			WL_MSG("wlan", "set primary chanspec to 0x%d\n", chanspec);
			wldev_iovar_setint(primary_dev, "chanspec", chanspec);
		}
		cfg->vsdb_mode = false;
    }
#endif /* WLEASYMESH*/
	WL_MSG("wlan", "%s concurrency is enabled\n", cfg->vsdb_mode ? "Multi Channel" : "Same Channel");
	return;
}

int
wl_cfg80211_determine_p2p_rsdb_scc_mode(struct bcm_cfg80211 *cfg)
{
	struct net_info *iter, *next;
	u32 chanspec = 0;
	u32 pre_chanspec = 0;
	u32 band = 0;
	u32 pre_band = INVCHANSPEC;
	bool is_rsdb_supported = FALSE;
	bool rsdb_or_scc_mode = FALSE;

#ifdef BCMDONGLEHOST
	is_rsdb_supported = DHD_OPMODE_SUPPORTED(cfg->pub, DHD_FLAG_RSDB_MODE);
#endif /* BCMDONGLEHOST */

	if (!is_rsdb_supported) {
		return 0;
	}

	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	for_each_ndev(cfg, iter, next) {
		GCC_DIAGNOSTIC_POP();
		/* p2p discovery iface ndev could be null */
		if (iter->ndev) {
			chanspec = 0;
			band = 0;
			if (wl_get_drv_status(cfg, CONNECTED, iter->ndev)) {
				if (wldev_iovar_getint(iter->ndev, "chanspec",
					(s32 *)&chanspec) == BCME_OK) {
					chanspec = wl_chspec_driver_to_host(chanspec);
					band = CHSPEC_BAND(chanspec);
				}

				if (pre_band == INVCHANSPEC && chanspec) {
					pre_band = band;
					pre_chanspec = chanspec;
				} else {
					if ((pre_band == band) && (pre_chanspec != chanspec)) {
						/* VSDB case */
						rsdb_or_scc_mode = FALSE;
					} else {
						/* RSDB/SCC case */
						rsdb_or_scc_mode = TRUE;
					}
				}
			}
		}
	}
	WL_DBG(("RSDB or SCC mode is %s\n", rsdb_or_scc_mode ? "enabled" : "disabled"));

	return rsdb_or_scc_mode;
}

static s32 wl_notifier_change_state(struct bcm_cfg80211 *cfg, struct net_info *_net_info,
	enum wl_status state, bool set)
{
	s32 pm = PM_FAST;
	s32 err = BCME_OK;
	u32 mode;
	chanspec_t chspec = 0;
	struct net_device *primary_dev = bcmcfg_to_prmry_ndev(cfg);
#ifdef BCMDONGLEHOST
	dhd_pub_t *dhd = cfg->pub;
#endif /* BCMDONGLEHOST */
#ifdef RTT_SUPPORT
	rtt_status_info_t *rtt_status;
#endif /* RTT_SUPPORT */
#ifdef DISABLE_FRAMEBURST_VSDB
	bool rsdb_scc_flag = FALSE;
#endif /* DISABLE_FRAMEBURST_VSDB */
#ifdef BCMDONGLEHOST
	if (dhd->busstate == DHD_BUS_DOWN) {
		WL_ERR(("busstate is DHD_BUS_DOWN!\n"));
		return 0;
	}
#endif /* BCMDONGLEHOST */
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
#if defined(WLTDLS)
		if (wl_cfg80211_is_concurrent_mode(primary_dev)) {
			err = wldev_iovar_setint(primary_dev, "tdls_enable", 0);
		}
#endif /* defined(WLTDLS) */

#ifdef BCMDONGLEHOST
#ifdef DISABLE_FRAMEBURST_VSDB
		if (!DHD_OPMODE_SUPPORTED(cfg->pub, DHD_FLAG_HOSTAP_MODE) &&
			wl_cfg80211_is_concurrent_mode(primary_dev)) {
			rsdb_scc_flag = wl_cfg80211_determine_p2p_rsdb_scc_mode(cfg);
			wl_cfg80211_set_frameburst(cfg, rsdb_scc_flag);
		}
#endif /* DISABLE_FRAMEBURST_VSDB */
#ifdef DISABLE_WL_FRAMEBURST_SOFTAP
		if (DHD_OPMODE_STA_SOFTAP_CONCURR(dhd) &&
			wl_get_drv_status(cfg, CONNECTED, bcmcfg_to_prmry_ndev(cfg))) {
			/* Enable frameburst for
			 * STA/SoftAP concurrent mode
			 */
			wl_cfg80211_set_frameburst(cfg, TRUE);
		}
#endif /* DISABLE_WL_FRAMEBURST_SOFTAP */
#endif /* BCMDONGLEHOST */
	} else { /* clear */
		chspec = INVCHANSPEC;
		/* clear chan information when the net device is disconnected */
		wl_update_prof(cfg, _net_info->ndev, NULL, &chspec, WL_PROF_CHAN);
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
#if defined(WLTDLS)
		if (!wl_cfg80211_is_concurrent_mode(primary_dev)) {
			err = wldev_iovar_setint(primary_dev, "tdls_enable", 1);
		}
#endif /* defined(WLTDLS) */

#ifdef BCMDONGLEHOST
#if defined(DISABLE_FRAMEBURST_VSDB)
		if (!DHD_OPMODE_SUPPORTED(cfg->pub, DHD_FLAG_HOSTAP_MODE)) {
			wl_cfg80211_set_frameburst(cfg, TRUE);
		}
#endif /* DISABLE_FRAMEBURST_VSDB */
#ifdef DISABLE_WL_FRAMEBURST_SOFTAP
		if (DHD_OPMODE_STA_SOFTAP_CONCURR(dhd) &&
			CHSPEC_IS2G(cfg->ap_oper_channel)) {
			/* Disable frameburst for stand-alone 2GHz SoftAP */
			wl_cfg80211_set_frameburst(cfg, FALSE);
		}
#endif /* DISABLE_WL_FRAMEBURST_SOFTAP */
#endif /* BCMDONGLEHOST */
	}
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

#ifdef CONFIG_SLEEP_MONITOR
extern long long temp_raw;

int wlan_get_sleep_monitor64_cb(void *priv, long long *raw_val,
	int check_level, int caller_type)
{
	struct bcm_cfg80211 *cfg = priv;
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
	int state = DEVICE_UNKNOWN;

	if (!dhdp->up)
		state = DEVICE_POWER_OFF;
	else {
		state = DEVICE_ON_ACTIVE1;
		if (wl_get_drv_status_all(cfg, CONNECTED))
			state = DEVICE_ON_ACTIVE2;

		if (caller_type == SLEEP_MONITOR_CALL_SUSPEND) {
			*raw_val = temp_raw;
			temp_raw = 0;
		}
	}

	return state;
}

static struct sleep_monitor_ops wlan_sleep_monitor_ops = {
	.read64_cb_func = wlan_get_sleep_monitor64_cb,
};
#endif /* CONFIG_SLEEP_MONITOR */

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
	init_completion(&cfg->send_af_done);
	init_completion(&cfg->iface_disable);
	mutex_init(&cfg->usr_sync);
	mutex_init(&cfg->event_sync);
	mutex_init(&cfg->if_sync);
	mutex_init(&cfg->scan_sync);
	mutex_init(&cfg->connect_sync);
	mutex_init(&cfg->pm_sync);
#ifdef WLTDLS
	mutex_init(&cfg->tdls_sync);
#endif	/* WLTDLS */
#ifdef WL_BCNRECV
	mutex_init(&cfg->bcn_sync);
#endif /* WL_BCNRECV */
#ifdef WL_WPS_SYNC
	wl_init_wps_reauth_sm(cfg);
#endif /* WL_WPS_SYNC */
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
	cfg->pmk_list->pmkids.length = OFFSETOF(pmkid_list_v3_t, pmkid);
	cfg->pmk_list->pmkids.count = 0;
	cfg->pmk_list->pmkids.version = PMKID_LIST_VER_3;

#ifdef CONFIG_SLEEP_MONITOR
	sleep_monitor_register_ops(cfg, &wlan_sleep_monitor_ops,
		SLEEP_MONITOR_WIFI);
#endif /* CONFIG_SLEEP_MONITOR */
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

#ifdef CONFIG_SLEEP_MONITOR
	sleep_monitor_unregister_ops(SLEEP_MONITOR_WIFI);
#endif /* CONFIG_SLEEP_MONITOR */
}

#if defined(WL_ENABLE_P2P_IF) || defined (WL_NEWCFG_PRIVCMD_SUPPORT)
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
#ifndef WL_NEWCFG_PRIVCMD_SUPPORT
	struct wireless_dev *wdev;
#endif /* WL_NEWCFG_PRIVCMD_SUPPORT */

	WL_DBG(("Enter \n"));
	if (!cfg) {
		WL_ERR(("Invalid Ptr\n"));
		return -EINVAL;
	}
#ifndef WL_NEWCFG_PRIVCMD_SUPPORT
	else {
		wdev = cfg->p2p_wdev;
		if (!wdev) {
			WL_ERR(("Invalid Ptr\n"));
			return -EINVAL;
		}
	}
#endif /* WL_NEWCFG_PRIVCMD_SUPPORT */

	wl_cfgp2p_unregister_ndev(cfg);

	cfg->p2p_wdev = NULL;
	cfg->p2p_net = NULL;
#ifndef WL_NEWCFG_PRIVCMD_SUPPORT
	WL_DBG(("Freeing 0x%p \n", wdev));
	MFREE(cfg->osh, wdev, sizeof(*wdev));
#endif /* WL_NEWCFG_PRIVCMD_SUPPORT */

	return 0;
}
#endif /* WL_ENABLE_P2P_IF || WL_NEWCFG_PRIVCMD_SUPPORT */

#if defined(BCMDONGLEHOST)
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
#endif /* BCMDONGLEHOST */

struct bcm_cfg80211 *wl_get_cfg(struct net_device *ndev)
{
	struct wireless_dev *wdev = ndev->ieee80211_ptr;

	if (!wdev || !wdev->wiphy)
		return NULL;

	return wiphy_priv(wdev->wiphy);
}

s32
wl_cfg80211_net_attach(struct net_device *primary_ndev)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(primary_ndev);
#ifdef WL_STATIC_IF
	enum nl80211_iftype ntype;
	int i;
#endif

	if (!cfg) {
		WL_ERR(("cfg null\n"));
		return BCME_ERROR;
	}
#ifdef WL_STATIC_IF
	/* Register dummy n/w iface. FW init will happen only from dev_open */
#ifdef WLDWDS
	ntype = NL80211_IFTYPE_AP;
#else
	ntype = NL80211_IFTYPE_STATION;
#endif
	for (i=0; i<DHD_MAX_STATIC_IFS; i++) {
		if (wl_cfg80211_register_static_if(cfg, ntype,
				WL_STATIC_IFNAME_PREFIX, i) == NULL) {
			WL_ERR(("static i/f registration failed!\n"));
			wl_cfg80211_unregister_static_if(cfg);
			return BCME_ERROR;
		}
	}
#endif /* WL_STATIC_IF */
	return BCME_OK;
}

s32 wl_cfg80211_attach(struct net_device *ndev, void *context)
{
	struct wireless_dev *wdev;
	struct bcm_cfg80211 *cfg;
	s32 err = 0;
	struct device *dev;
	u16 bssidx = 0;
	u16 ifidx = 0;
	dhd_pub_t *dhd = (struct dhd_pub *)(context);

	WL_TRACE(("In\n"));
	if (!ndev) {
		WL_ERR(("ndev is invaild\n"));
		return -ENODEV;
	}
	WL_DBG(("func %p\n", wl_cfg80211_get_parent_dev()));
#if !defined(BCMDONGLEHOST)
	wl_cfg80211_set_parent_dev(context);
#endif
	dev = wl_cfg80211_get_parent_dev();

	wdev = (struct wireless_dev *)MALLOCZ(dhd->osh, sizeof(*wdev));
	if (unlikely(!wdev)) {
		WL_ERR(("Could not allocate wireless device\n"));
		return -ENOMEM;
	}
	err = wl_setup_wiphy(wdev, dev, context);
	if (unlikely(err)) {
		MFREE(dhd->osh, wdev, sizeof(*wdev));
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
	cfg->osh = dhd->osh;
	INIT_LIST_HEAD(&cfg->net_list);
#ifdef WBTEXT
	INIT_LIST_HEAD(&cfg->wbtext_bssid_list);
#endif /* WBTEXT */
	INIT_LIST_HEAD(&cfg->vndr_oui_list);
	spin_lock_init(&cfg->vndr_oui_sync);
	spin_lock_init(&cfg->net_list_sync);
	ndev->ieee80211_ptr = wdev;
	SET_NETDEV_DEV(ndev, wiphy_dev(wdev->wiphy));
	wdev->netdev = ndev;
	cfg->state_notifier = wl_notifier_change_state;
	err = wl_alloc_netinfo(cfg, ndev, wdev, WL_IF_TYPE_STA, PM_ENABLE, bssidx, ifidx);
	if (err) {
		WL_ERR(("Failed to alloc net_info (%d)\n", err));
		goto cfg80211_attach_out;
	}
	err = wl_init_priv(cfg);
	if (err) {
		WL_ERR(("Failed to init iwm_priv (%d)\n", err));
		goto cfg80211_attach_out;
	}

	err = wl_setup_rfkill(cfg, TRUE);
	if (err) {
		WL_ERR(("Failed to setup rfkill %d\n", err));
		goto cfg80211_attach_out;
	}

	if (!wl_cfg80211_netdev_notifier_registered) {
		wl_cfg80211_netdev_notifier_registered = TRUE;
		err = register_netdevice_notifier(&wl_cfg80211_netdev_notifier);
		if (err) {
			wl_cfg80211_netdev_notifier_registered = FALSE;
			WL_ERR(("Failed to register notifierl %d\n", err));
			goto cfg80211_attach_out;
		}
	}

#if defined(OEM_ANDROID) && defined(COEX_DHCP)
	cfg->btcoex_info = wl_cfg80211_btcoex_init(cfg->wdev->netdev);
	if (!cfg->btcoex_info)
		goto cfg80211_attach_out;
#endif /* defined(OEM_ANDROID) && defined(COEX_DHCP) */

#if defined(SUPPORT_RANDOM_MAC_SCAN)
	cfg->random_mac_enabled = FALSE;
#endif /* SUPPORT_RANDOM_MAC_SCAN */

#ifdef CONFIG_CFG80211_INTERNAL_REGDB
	wdev->wiphy->reg_notifier = wl_cfg80211_reg_notifier;
#endif /* CONFIG_CFG80211_INTERNAL_REGDB */

#if defined(WL_ENABLE_P2P_IF) || defined (WL_NEWCFG_PRIVCMD_SUPPORT)
	err = wl_cfg80211_attach_p2p(cfg);
	if (err)
		goto cfg80211_attach_out;
#endif /* WL_ENABLE_P2P_IF || WL_NEWCFG_PRIVCMD_SUPPORT */

#if defined(OEM_ANDROID) && defined(DHCP_SCAN_SUPPRESS)
	/* wlan scan_supp timer and work thread info */
	init_timer_compat(&cfg->scan_supp_timer, wl_cfg80211_scan_supp_timerfunc, cfg);
	INIT_WORK(&cfg->wlan_work, wl_cfg80211_work_handler);
#endif /* DHCP_SCAN_SUPPRESS */

	INIT_DELAYED_WORK(&cfg->pm_enable_work, wl_cfg80211_work_handler);
	INIT_DELAYED_WORK(&cfg->loc.work, wl_cfgscan_listen_complete_work);
	INIT_DELAYED_WORK(&cfg->ap_work, wl_cfg80211_ap_timeout_work);
	mutex_init(&cfg->pm_sync);
#ifdef WL_NAN
	err = wl_cfgnan_attach(cfg);
	if (err) {
		WL_ERR(("Failed to attach nan module %d\n", err));
		goto cfg80211_attach_out;
	}
#endif /* WL_NAN */
	cfg->rssi_sum_report = FALSE;
#ifdef WL_BAM
	wl_bad_ap_mngr_init(cfg);
#endif	/* WL_BAM */

#ifdef BIGDATA_SOFTAP
	wl_attach_ap_stainfo(cfg);
#endif /* BIGDATA_SOFTAP */

	return err;

cfg80211_attach_out:
	wl_cfg80211_detach(cfg);
	return err;
}

void wl_cfg80211_detach(struct bcm_cfg80211 *cfg)
{
	WL_DBG(("Enter\n"));
	if (!cfg) {
		return;
	}
/* clean up pm_enable work item. Remove this once deinit is properly
 * clean up and wl_cfg8021_down is called while removing the module
 */
	wl_add_remove_pm_enable_work(cfg, WL_PM_WORKQ_DEL);

#if defined(OEM_ANDROID) && defined(COEX_DHCP)
	wl_cfg80211_btcoex_deinit();
	cfg->btcoex_info = NULL;
#endif /* defined(OEM_ANDROID) && defined(COEX_DHCP) */

	wl_setup_rfkill(cfg, FALSE);

#ifdef WL_WPS_SYNC
	wl_deinit_wps_reauth_sm(cfg);
#endif /* WL_WPS_SYNC */

	del_timer_sync(&cfg->scan_timeout);
#ifdef DHD_LOSSLESS_ROAMING
	del_timer_sync(&cfg->roam_timeout);
#endif /* DHD_LOSSLESS_ROAMING */

#ifdef WL_STATIC_IF
	wl_cfg80211_unregister_static_if(cfg);
#endif /* WL_STATIC_IF */
#if defined(WL_ENABLE_P2P_IF) || defined (WL_NEWCFG_PRIVCMD_SUPPORT)
	wl_cfg80211_detach_p2p(cfg);
#endif /* WL_ENABLE_P2P_IF || WL_NEWCFG_PRIVCMD_SUPPORT */
#ifdef WL_BAM
	wl_bad_ap_mngr_deinit(cfg);
#endif	/* WL_BAM */

#ifdef BIGDATA_SOFTAP
	wl_detach_ap_stainfo(cfg);
#endif /* BIGDATA_SOFTAP */

#ifdef WL_NAN
	wl_cfgnan_detach(cfg);
#endif /* WL_NAN */
	wl_cfg80211_ibss_vsie_free(cfg);
	wl_dealloc_netinfo_by_wdev(cfg, cfg->wdev);
	wl_cfg80211_set_bcmcfg(NULL);
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
	WL_DBG(("Exit\n"));
}

#if defined(CONFIG_WLAN_BEYONDX) || defined(CONFIG_SEC_5GMODEL)
void wl_cfg80211_register_dev_ril_bridge_event_notifier()
{
	WL_DBG(("Enter\n"));
	if (!wl_cfg80211_ril_bridge_notifier_registered) {
		s32 err = 0;
		wl_cfg80211_ril_bridge_notifier_registered = TRUE;
		err = register_dev_ril_bridge_event_notifier(&wl_cfg80211_ril_bridge_notifier);
		if (err) {
			wl_cfg80211_ril_bridge_notifier_registered = FALSE;
			WL_ERR(("Failed to register ril_notifier! %d\n", err));
		}
	}
}

void wl_cfg80211_unregister_dev_ril_bridge_event_notifier()
{
	WL_DBG(("Enter\n"));
	if (wl_cfg80211_ril_bridge_notifier_registered) {
		wl_cfg80211_ril_bridge_notifier_registered = FALSE;
		unregister_dev_ril_bridge_event_notifier(&wl_cfg80211_ril_bridge_notifier);
	}
}
#endif /* CONFIG_WLAN_BEYONDX || defined(CONFIG_SEC_5GMODEL) */

static void wl_print_event_data(struct bcm_cfg80211 *cfg,
	uint32 event_type, const wl_event_msg_t *e)
{
	s32 status = ntoh32(e->status);
	s32 reason = ntoh32(e->reason);
	s32 ifidx = ntoh32(e->ifidx);
	s32 bssidx = ntoh32(e->bsscfgidx);

	switch (event_type) {
		case WLC_E_ESCAN_RESULT:
			if ((status == WLC_E_STATUS_SUCCESS) ||
				(status == WLC_E_STATUS_ABORT)) {
				WL_INFORM_MEM(("event_type (%d), ifidx: %d"
					" bssidx: %d scan_type:%d\n",
					event_type, ifidx, bssidx, status));
			}
			break;
		case WLC_E_LINK:
		case WLC_E_DISASSOC:
		case WLC_E_DISASSOC_IND:
		case WLC_E_DEAUTH:
		case WLC_E_DEAUTH_IND:
			WL_INFORM_MEM(("event_type (%d), ifidx: %d bssidx: %d"
				" status:%d reason:%d\n",
				event_type, ifidx, bssidx, status, reason));
				break;

		default:
			/* Print only when DBG verbose is enabled */
			WL_DBG(("event_type (%d), ifidx: %d bssidx: %d status:%d reason: %d\n",
				event_type, ifidx, bssidx, status, reason));
	}
}

static void wl_event_handler(struct work_struct *work_data)
{
	struct bcm_cfg80211 *cfg = NULL;
	struct wl_event_q *e;
	struct wireless_dev *wdev = NULL;

	WL_DBG(("Enter \n"));
	BCM_SET_CONTAINER_OF(cfg, work_data, struct bcm_cfg80211, event_work);
	LOG_TS(cfg, wl_evt_hdlr_entry);
	DHD_EVENT_WAKE_LOCK(cfg->pub);
	while ((e = wl_deq_event(cfg))) {
		s32 status = ntoh32(e->emsg.status);
		u32 event_type = ntoh32(e->emsg.event_type);
		bool scan_cmplt_evt = (event_type == WLC_E_ESCAN_RESULT) &&
			((status == WLC_E_STATUS_SUCCESS) || (status == WLC_E_STATUS_ABORT));

		LOG_TS(cfg, wl_evt_deq);
		if (scan_cmplt_evt) {
			LOG_TS(cfg, scan_deq);
		}
		/* Print only critical events to avoid too many prints */
		wl_print_event_data(cfg, e->etype, &e->emsg);

		if (e->emsg.ifidx > WL_MAX_IFS) {
			WL_ERR((" Event ifidx not in range. val:%d \n", e->emsg.ifidx));
			goto fail;
		}

		/* Make sure iface operations, don't creat race conditions */
		mutex_lock(&cfg->if_sync);
		if (!(wdev = wl_get_wdev_by_fw_idx(cfg,
			e->emsg.bsscfgidx, e->emsg.ifidx))) {
			/* For WLC_E_IF would be handled by wl_host_event */
			if (e->etype != WLC_E_IF)
				WL_ERR(("No wdev corresponding to bssidx: 0x%x found!"
					" Ignoring event.\n", e->emsg.bsscfgidx));
		} else if (e->etype < WLC_E_LAST && cfg->evt_handler[e->etype]) {
#if defined(BCMDONGLEHOST)
			dhd_pub_t *dhd = (struct dhd_pub *)(cfg->pub);
			if (dhd->busstate == DHD_BUS_DOWN) {
				WL_ERR((": BUS is DOWN.\n"));
			} else
#endif /* defined(BCMDONGLEHOST) */
			{
				WL_DBG(("event_type %d event_sub %d\n",
					ntoh32(e->emsg.event_type),
					ntoh32(e->emsg.reason)));
				WL_SET_EIDX_IN_PROGRESS(cfg, e->id, e->etype);
				cfg->evt_handler[e->etype](cfg, wdev_to_cfgdev(wdev),
					&e->emsg, e->edata);
				WL_CLR_EIDX_STATES(cfg);
				if (scan_cmplt_evt) {
					LOG_TS(cfg, scan_hdlr_cmplt);
				}
			}
		} else {
			WL_DBG(("Unknown Event (%d): ignoring\n", e->etype));
		}
		mutex_unlock(&cfg->if_sync);
fail:
		wl_put_event(cfg, e);
		if (scan_cmplt_evt) {
			LOG_TS(cfg, scan_cmplt);
		}
		LOG_TS(cfg, wl_evt_hdlr_exit);
	}
	DHD_EVENT_WAKE_UNLOCK(cfg->pub);
}

/*
* Generic API to handle critical events which doesnt need
* cfg enquening and sleepable API calls.
*/
s32
wl_cfg80211_handle_critical_events(struct bcm_cfg80211 *cfg,
	struct wireless_dev *wdev, const wl_event_msg_t * e)
{
	s32 ret = BCME_ERROR;
	u32 event_type = ntoh32(e->event_type);

	if (event_type >= WLC_E_LAST) {
		return BCME_ERROR;
	}

	switch (event_type) {
		case WLC_E_NAN_CRITICAL: {
#ifdef WL_NAN
		if (ntoh32(e->reason) == WL_NAN_EVENT_STOP) {
			WL_DBG(("Received WL_NAN_EVENT_STOP\n"));
		}
#endif /* WL_NAN */
			break;
		}
		default:
			ret = BCME_ERROR;
	}
	return ret;
}

void
wl_cfg80211_event(struct net_device *ndev, const wl_event_msg_t * e, void *data)
{
	s32 status = ntoh32(e->status);
	u32 event_type = ntoh32(e->event_type);
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	struct net_info *netinfo;

	WL_DBG(("event_type (%d): reason (%d): %s\n", event_type, ntoh32(e->reason),
		bcmevent_get_name(event_type)));
	if ((cfg == NULL) || (cfg->p2p_supported && cfg->p2p == NULL)) {
		WL_ERR(("Stale event ignored\n"));
		return;
	}

#ifdef OEM_ANDROID
	if (cfg->event_workq == NULL) {
		WL_ERR(("Event handler is not created\n"));
		return;
	}
#endif /* OEM_ANDROID */

#ifndef OEM_ANDROID
	if (!cfg->event_workq_init) {
		WL_ERR(("Event handler is not created\n"));
		return;
	}
#endif /* OEM_ANDROID */

	if (event_type == WLC_E_IF) {
		/* Don't process WLC_E_IF events in wl_cfg80211 layer */
		return;
	}

	netinfo = wl_get_netinfo_by_fw_idx(cfg, e->bsscfgidx, e->ifidx);
	if (!netinfo) {
		/* Since the netinfo entry is not there, the netdev entry is not
		 * created via cfg80211 interface. so the event is not of interest
		 * to the cfg80211 layer.
		 */
		WL_TRACE(("ignore event %d, not interested\n", event_type));
		return;
	}

	/* Handle wl_cfg80211_critical_events */
	if (wl_cfg80211_handle_critical_events(cfg, netinfo->wdev, e) == BCME_OK) {
		return;
	}

	if (event_type == WLC_E_PFN_NET_FOUND) {
		WL_DBG((" PNOEVENT: PNO_NET_FOUND\n"));
	}
	else if (event_type == WLC_E_PFN_NET_LOST) {
		WL_DBG((" PNOEVENT: PNO_NET_LOST\n"));
	}

	if (likely(!wl_enq_event(cfg, ndev, event_type, e, data))) {

#ifdef OEM_ANDROID
		queue_work(cfg->event_workq, &cfg->event_work);
#endif /* OEM_ANDROID */

#ifndef OEM_ANDROID
		schedule_work(&cfg->event_work);
#endif /* OEM_ANDROID */
	}
	/* Mark timeout value for thread sched */
	if ((event_type == WLC_E_ESCAN_RESULT) &&
		((status == WLC_E_STATUS_SUCCESS) ||
		(status == WLC_E_STATUS_ABORT)))  {
		LOG_TS(cfg, scan_enq);
		WL_INFORM_MEM(("Enqueing escan completion (%d). WQ state:0x%x \n",
			status, work_busy(&cfg->event_work)));
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
		MFREE(cfg->osh, e, e->datalen + sizeof(struct wl_event_q));
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

	data_len = 0;
	if (data)
		data_len = ntoh32(msg->datalen);
	evtq_size = (uint32)(sizeof(struct wl_event_q) + data_len);
	e = (struct wl_event_q *)MALLOCZ(cfg->osh, evtq_size);
	if (unlikely(!e)) {
		WL_ERR(("event alloc failed\n"));
		return -ENOMEM;
	}
	e->etype = event;
	memcpy(&e->emsg, msg, sizeof(wl_event_msg_t));
	if (data)
		memcpy(e->edata, data, data_len);
	e->datalen = data_len;
	e->id = cfg->eidx.enqd++;
	flags = wl_lock_eq(cfg);
	list_add_tail(&e->eq_list, &cfg->eq_list);
	wl_unlock_eq(cfg, flags);

	return err;
}

static void wl_put_event(struct bcm_cfg80211 *cfg, struct wl_event_q *e)
{
	MFREE(cfg->osh, e, e->datalen + sizeof(struct wl_event_q));
}

static s32 wl_config_infra(struct bcm_cfg80211 *cfg, struct net_device *ndev, u16 iftype)
{
	s32 infra = 0;
	s32 err = 0;
	bool skip_infra = false;

	switch (iftype) {
		case WL_IF_TYPE_IBSS:
		case WL_IF_TYPE_AIBSS:
			infra = 0;
			break;
		case WL_IF_TYPE_AP:
		case WL_IF_TYPE_STA:
		case WL_IF_TYPE_P2P_GO:
		case WL_IF_TYPE_P2P_GC:
			/* Intentional fall through */
			infra = 1;
			break;
#ifdef WLMESH_CFG80211
	case NL80211_IFTYPE_MESH_POINT:
		infra = WL_BSSTYPE_MESH;
		break;
#endif /* WLMESH_CFG80211 */
		case WL_IF_TYPE_MONITOR:

#ifdef WLAWDL
		case WL_IF_TYPE_AWDL:
#endif /* WLAWDL */

		case WL_IF_TYPE_NAN:
			/* Intentionall fall through */
		default:
			skip_infra = true;
			WL_ERR(("Skipping infra setting for type:%d\n", iftype));
			break;
	}

	/* /TODO Infra iovar is stored in default bss first and
	 * then applied to the next upcoming bss. so if there is
	 * some other concurrent bss coming up in parallel, it
	 * can cause problem. Ideally this iovar should get directly
	 * applied on the target bsscfg.
	 */
	if (!skip_infra) {
		infra = htod32(infra);
		err = wldev_ioctl_set(ndev, WLC_SET_INFRA, &infra, sizeof(infra));
		if (unlikely(err)) {
			WL_ERR(("WLC_SET_INFRA error (%d)\n", err));
			return err;
		}
	}
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
	int i, ret = BCME_OK;
	s8 event_buf[WL_EVENTING_MASK_EXT_LEN + EVENTMSGS_EXT_STRUCT_SIZE] = {0};
	/*  Room for "event_msgs_ext" + '\0' + bitvec  */
	char iovbuf[WL_EVENTING_MASK_EXT_LEN + EVENTMSGS_EXT_STRUCT_SIZE + 16];
	eventmsgs_ext_t *eventmask_msg;
	s32 msglen = WL_EVENTING_MASK_EXT_LEN + EVENTMSGS_EXT_STRUCT_SIZE;

	if (!ev || (!ev->num)) {
		return -EINVAL;
	}

	mutex_lock(&cfg->event_sync);

	eventmask_msg = (eventmsgs_ext_t *)event_buf;
	eventmask_msg->ver = EVENTMSGS_VER;
	eventmask_msg->command = EVENTMSGS_NONE;
	eventmask_msg->len = WL_EVENTING_MASK_EXT_LEN;
	eventmask_msg->maxgetsize = WL_EVENTING_MASK_EXT_LEN;

	/* Read event_msgs mask */
	ret = wldev_iovar_getbuf(ndev, "event_msgs_ext",
		eventmask_msg, EVENTMSGS_EXT_STRUCT_SIZE,
		iovbuf,
		sizeof(iovbuf),
		NULL);

	if (unlikely(ret)) {
		WL_ERR(("Get event_msgs error (%d)\n", ret));
		goto exit;
	}

	bcopy(iovbuf, eventmask_msg, msglen);

	/* apply the set bits */
	for (i = 0; i < ev->num; i++) {
		if (ev->event[i].set)
			setbit(eventmask_msg->mask, ev->event[i].type);
		else
			clrbit(eventmask_msg->mask, ev->event[i].type);
	}

	/* Write updated Event mask */
	eventmask_msg->ver = EVENTMSGS_VER;
	eventmask_msg->command = EVENTMSGS_SET_MASK;
	eventmask_msg->len = WL_EVENTING_MASK_EXT_LEN;

	/* Write updated Event mask */
	ret = wldev_iovar_setbuf(ndev, "event_msgs_ext", eventmask_msg,
		WL_EVENTING_MASK_EXT_LEN + EVENTMSGS_EXT_STRUCT_SIZE,
		iovbuf, sizeof(iovbuf), NULL);

	if (unlikely(ret)) {
		WL_ERR(("Set event_msgs error (%d)\n", ret));
	}

exit:
	mutex_unlock(&cfg->event_sync);
	return ret;
}

s32 wl_add_remove_eventmsg(struct net_device *ndev, u16 event, bool add)
{
	s32 err = 0;
	s8 event_buf[WL_EVENTING_MASK_EXT_LEN + EVENTMSGS_EXT_STRUCT_SIZE] = {0};
	eventmsgs_ext_t *eventmask_msg = NULL;
	struct bcm_cfg80211 *cfg;
	/*  Room for "event_msgs_ext" + '\0' + bitvec  */
	char iovbuf[WL_EVENTING_MASK_EXT_LEN + EVENTMSGS_EXT_STRUCT_SIZE + 16];
	s32 msglen = WL_EVENTING_MASK_EXT_LEN + EVENTMSGS_EXT_STRUCT_SIZE;

	if (!ndev)
		return -ENODEV;

	cfg = wl_get_cfg(ndev);
	if (!cfg)
		return -ENODEV;

	mutex_lock(&cfg->event_sync);

	eventmask_msg = (eventmsgs_ext_t *)event_buf;
	eventmask_msg->ver = EVENTMSGS_VER;
	eventmask_msg->command = EVENTMSGS_NONE;
	eventmask_msg->len = WL_EVENTING_MASK_EXT_LEN;
	eventmask_msg->maxgetsize = WL_EVENTING_MASK_EXT_LEN;

	/* Read event_msgs mask */
	err = wldev_iovar_getbuf(ndev, "event_msgs_ext",
		eventmask_msg, EVENTMSGS_EXT_STRUCT_SIZE,
		iovbuf,
		sizeof(iovbuf),
		NULL);

	if (unlikely(err)) {
		WL_ERR(("Get event_msgs error (%d)\n", err));
		goto eventmsg_out;
	}

	bcopy(iovbuf, eventmask_msg, msglen);

	if (add) {
		setbit(eventmask_msg->mask, event);
	} else {
		clrbit(eventmask_msg->mask, event);
	}

	/* Write updated Event mask */
	eventmask_msg->ver = EVENTMSGS_VER;
	eventmask_msg->command = EVENTMSGS_SET_MASK;
	eventmask_msg->len = WL_EVENTING_MASK_EXT_LEN;

	err = wldev_iovar_setbuf(ndev, "event_msgs_ext", eventmask_msg,
		WL_EVENTING_MASK_EXT_LEN + EVENTMSGS_EXT_STRUCT_SIZE,
		iovbuf, sizeof(iovbuf), NULL);

	if (unlikely(err)) {
		WL_ERR(("Set event_msgs error (%d)\n", err));
		goto eventmsg_out;
	}

eventmsg_out:
	mutex_unlock(&cfg->event_sync);
	return err;
}

void
wl_cfg80211_generate_mac_addr(struct ether_addr *ea_addr)
{
	RANDOM_BYTES(ea_addr->octet, ETHER_ADDR_LEN);
	/* restore mcast and local admin bits to 0 and 1 */
	ETHER_SET_UNICAST(ea_addr->octet);
	ETHER_SET_LOCALADDR(ea_addr->octet);
	WL_ERR(("%s:generated new MAC="MACDBG" \n",
		__FUNCTION__, MAC2STRDBG(ea_addr->octet)));
	return;
}

static s32 wl_update_chan_param(struct net_device *dev, u32 cur_chan,
	struct ieee80211_channel *band_chan, bool *dfs_radar_disabled, bool legacy_chan_info)
{
	s32 err = BCME_OK;
	u32 channel = cur_chan;

	if (!(*dfs_radar_disabled)) {
		if (legacy_chan_info) {
			channel |= WL_CHANSPEC_BW_20;
			channel = wl_chspec_host_to_driver(channel);
			err = wldev_iovar_getint(dev, "per_chan_info", &channel);
		}
		if (!err) {
			if (channel & WL_CHAN_RADAR) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0))
				band_chan->flags |= (IEEE80211_CHAN_RADAR |
					IEEE80211_CHAN_NO_IBSS);
#else
				band_chan->flags |= IEEE80211_CHAN_RADAR;
#endif
			}
			if (channel & WL_CHAN_PASSIVE) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0))
				band_chan->flags |= IEEE80211_CHAN_PASSIVE_SCAN;
#else
				band_chan->flags |= IEEE80211_CHAN_NO_IR;
#endif
			}
		} else if (err == BCME_UNSUPPORTED) {
			*dfs_radar_disabled = TRUE;
			WL_ERR(("does not support per_chan_info\n"));
		}
	}

	return err;
}

static int wl_construct_reginfo(struct bcm_cfg80211 *cfg, s32 bw_cap)
{
	struct net_device *dev = bcmcfg_to_prmry_ndev(cfg);
	struct ieee80211_channel *band_chan_arr = NULL;
	void *list;
	u32 i, j, index, channel, array_size = 0;
	chanspec_t chspec = 0;
	s32 err = BCME_OK;
	bool ht40_allowed;
	bool dfs_radar_disabled = FALSE;
	bool legacy_chan_info = FALSE;
	u16 list_count;

#define LOCAL_BUF_LEN 4096
	list = MALLOCZ(cfg->osh, LOCAL_BUF_LEN);
	if (list == NULL) {
		WL_ERR(("failed to allocate local buf\n"));
		return -ENOMEM;
	}

	err = wldev_iovar_getbuf_bsscfg(dev, "chan_info_list", NULL,
		0, list, LOCAL_BUF_LEN, 0, &cfg->ioctl_buf_sync);
	if (err == BCME_UNSUPPORTED) {
		WL_INFORM(("get chan_info_list, UNSUPPORTED\n"));
		err = wldev_iovar_getbuf_bsscfg(dev, "chanspecs", NULL,
			0, list, LOCAL_BUF_LEN, 0, &cfg->ioctl_buf_sync);
		if (err != BCME_OK) {
			WL_ERR(("get chanspecs err(%d)\n", err));
			MFREE(cfg->osh, list, LOCAL_BUF_LEN);
			return err;
		}
		/* Update indicating legacy chan info usage */
		legacy_chan_info = TRUE;
	} else if (err != BCME_OK) {
		WL_ERR(("get chan_info_list err(%d)\n", err));
		MFREE(cfg->osh, list, LOCAL_BUF_LEN);
		return err;
	}

	WL_CHANNEL_ARRAY_INIT(__wl_2ghz_channels);
	WL_CHANNEL_ARRAY_INIT(__wl_5ghz_a_channels);
#ifdef CFG80211_6G_SUPPORT
	WL_CHANNEL_ARRAY_INIT(__wl_6ghz_channels);
#endif /* CFG80211_6G_SUPPORT */

	list_count = legacy_chan_info ? ((wl_uint32_list_t *)list)->count :
		((wl_chanspec_list_v1_t *)list)->count;
	for (i = 0; i < dtoh32(list_count); i++) {
		index = 0;
		ht40_allowed = false;
		if (legacy_chan_info) {
			chspec = (chanspec_t)dtoh32(((wl_uint32_list_t *)list)->element[i]);
		} else {
			chspec = (chanspec_t)dtoh32
			(((wl_chanspec_list_v1_t *)list)->chspecs[i].chanspec);
		}
		chspec = wl_chspec_driver_to_host(chspec);
		channel = wf_chspec_ctlchan(chspec);

		if (!CHSPEC_IS40(chspec) &&
			!CHSPEC_IS20(chspec)) {
			WL_DBG(("HT80/160/80p80 center channel : %d\n", channel));
			continue;
		}
		if (CHSPEC_IS2G(chspec) && (channel >= CH_MIN_2G_CHANNEL) &&
			(channel <= CH_MAX_2G_CHANNEL)) {
			band_chan_arr = __wl_2ghz_channels;
			array_size = ARRAYSIZE(__wl_2ghz_channels);
			ht40_allowed = (bw_cap  == WLC_N_BW_40ALL)? true : false;
		}
#ifdef CFG80211_6G_SUPPORT
		else if (CHSPEC_IS6G(chspec) && (channel >= CH_MIN_6G_CHANNEL) &&
			(channel <= CH_MAX_6G_CHANNEL)) {
			band_chan_arr = __wl_6ghz_channels;
			array_size = ARRAYSIZE(__wl_6ghz_channels);
			ht40_allowed = (bw_cap  == WLC_N_BW_20ALL)? false : true;
		}
#endif /* CFG80211_6G_SUPPORT */
		else if (
#ifdef WL_6G_BAND
			/* Currently due to lack of kernel support both 6GHz and 5GHz
			* channels are published under 5GHz band
			*/
			(CHSPEC_IS6G(chspec) && (channel >= CH_MIN_6G_CHANNEL) &&
			(channel <= CH_MAX_6G_CHANNEL)) ||
#endif /* WL_6G_BAND */
			(CHSPEC_IS5G(chspec) && channel >= CH_MIN_5G_CHANNEL)) {
				band_chan_arr = __wl_5ghz_a_channels;
				array_size = ARRAYSIZE(__wl_5ghz_a_channels);
			ht40_allowed = (bw_cap  == WLC_N_BW_20ALL)? false : true;
		} else {
			WL_ERR(("Invalid channel Sepc. 0x%x.\n", chspec));
			continue;
		}
		if (!ht40_allowed && CHSPEC_IS40(chspec))
			continue;
		for (j = 0; j < array_size; j++) {
			if (band_chan_arr[j].hw_value == chspec) {
				break;
			}
		}
		index = j;
		if (!dhd_conf_match_channel(cfg->pub, channel))
			continue;
		if (index <  array_size) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 39) && !defined(WL_COMPAT_WIRELESS)
			band_chan_arr[index].center_freq =
				ieee80211_channel_to_frequency(channel);
#else
			band_chan_arr[index].center_freq =
				wl_channel_to_frequency(channel, CHSPEC_BAND(chspec));
#endif
			band_chan_arr[index].hw_value = chspec;
			band_chan_arr[index].beacon_found = false;
			band_chan_arr[index].flags &= ~IEEE80211_CHAN_DISABLED;

			if (CHSPEC_IS40(chspec) && ht40_allowed) {
				/* assuming the order is HT20, HT40 Upper,
				 *  HT40 lower from chanspecs
				 */
				u32 ht40_flag = band_chan_arr[index].flags & IEEE80211_CHAN_NO_HT40;
				if (CHSPEC_SB_UPPER(chspec)) {
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
				if (!legacy_chan_info) {
					channel = dtoh32
					(((wl_chanspec_list_v1_t *)list)->chspecs[i].chaninfo);
				} else {
					channel |= CHSPEC_BAND(chspec);
				}
				/* Update channel for radar/passive support */
				err = wl_update_chan_param(dev, channel,
				&band_chan_arr[index], &dfs_radar_disabled, legacy_chan_info);
			}
		}

	}

	__wl_band_2ghz.n_channels = ARRAYSIZE(__wl_2ghz_channels);
	__wl_band_5ghz_a.n_channels = ARRAYSIZE(__wl_5ghz_a_channels);
#ifdef CFG80211_6G_SUPPORT
	__wl_band_6ghz.n_channels = ARRAYSIZE(__wl_6ghz_channels);
#endif /* CFG80211_6G_SUPPORT */

	MFREE(cfg->osh, list, LOCAL_BUF_LEN);
#undef LOCAL_BUF_LEN
	return err;
}

#ifdef WL_6G_BAND
static void wl_is_6g_supported(struct bcm_cfg80211 *cfg, u32 *bandlist, u8 nbands)
{
	u32 i = 0;

	if (nbands > WL_MAX_BAND_SUPPORT) {
		return;
	}
	/* Check for 6GHz band support */
	for (i = 1; i <= nbands; i++) {
		if (bandlist[i] == WLC_BAND_6G) {
			cfg->band_6g_supported = true;
		}
	}
}
#endif /* WL_6G_BAND */

static s32 __wl_update_wiphybands(struct bcm_cfg80211 *cfg, bool notify)
{
	struct wiphy *wiphy;
	struct net_device *dev = bcmcfg_to_prmry_ndev(cfg);
	u32 bandlist[WL_MAX_BAND_SUPPORT+1];
	u32 nband = 0;
	u32 i = 0;
	s32 err = 0;
	s32 index = 0;
	s32 nmode = 0;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)) || defined(CUSTOMER_HW5)
	u32 j = 0;
	s32 vhtmode = 0;
	s32 txstreams = 0;
	s32 rxstreams = 0;
	s32 ldpc_cap = 0;
	s32 stbc_rx = 0;
	s32 stbc_tx = 0;
	s32 txbf_bfe_cap = 0;
	s32 txbf_bfr_cap = 0;
#endif /* KERNEL >= 3.6 || CUSTOMER_HW5 */
	s32 bw_cap = 0;
	s32 cur_band = -1;
	struct ieee80211_supported_band *bands[IEEE80211_NUM_BANDS] = {NULL, };

	bzero(bandlist, sizeof(bandlist));
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

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)) || defined(CUSTOMER_HW5)
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
#endif /* KERNEL >= 3.6 || CUSTOMER_HW5 */

	/* For nmode and vhtmode   check bw cap */
	if (nmode ||
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)) || defined(CUSTOMER_HW5)
		vhtmode ||
#endif /* KERNEL >= 3.6 || CUSTOMER_HW5 */
		0) {
		err = wldev_iovar_getint(dev, "mimo_bw_cap", &bw_cap);
		if (unlikely(err)) {
			WL_ERR(("error get mimo_bw_cap (%d)\n", err));
		}
	}

#ifdef WL_6G_BAND
	wl_is_6g_supported(cfg, bandlist, bandlist[0]);
#endif /* WL_6G_BAND */

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

		if (bandlist[i] == WLC_BAND_2G && __wl_band_2ghz.n_channels > 0) {
			bands[IEEE80211_BAND_2GHZ] =
				&__wl_band_2ghz;
			index = IEEE80211_BAND_2GHZ;
			if (bw_cap == WLC_N_BW_40ALL)
				bands[index]->ht_cap.cap |= IEEE80211_HT_CAP_SGI_40;
		} else {
			if (bandlist[i] == WLC_BAND_6G) {
#ifdef CFG80211_6G_SUPPORT
				if (__wl_band_6ghz.n_channels > 0) {
					bands[IEEE80211_BAND_6GHZ] = &__wl_band_6ghz;
					index = IEEE80211_BAND_6GHZ;
				} else {
					WL_ERR(("6GHz channels not listed\n"));
					continue;
				}
#else /* CFG80211_6G_SUPPORT */
				/* Both 6G/5G channels will be under 5G band list */
				if (__wl_band_5ghz_a.n_channels > 0)
				{
					bands[IEEE80211_BAND_5GHZ] = &__wl_band_5ghz_a;
					index = IEEE80211_BAND_5GHZ;
				} else {
					WL_ERR(("5GHz channels not listed\n"));
					continue;
				}
#endif /* CFG80211_6G_SUPPORT */
			} else if ((bandlist[i] == WLC_BAND_5G) &&
				(__wl_band_5ghz_a.n_channels > 0)) {
				bands[IEEE80211_BAND_5GHZ] = &__wl_band_5ghz_a;
				index = IEEE80211_BAND_5GHZ;
			} else {
				WL_ERR(("Invalid band\n"));
				continue;
			}

			if (nmode && (bw_cap == WLC_N_BW_40ALL || bw_cap == WLC_N_BW_20IN2G_40IN5G))
				bands[index]->ht_cap.cap |= IEEE80211_HT_CAP_SGI_40;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)) || defined(CUSTOMER_HW5)
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
				WL_DBG(("__wl_update_wiphybands band[%d] vht_enab=%d vht_cap=%08x "
					"vht_rx_mcs_map=%04x vht_tx_mcs_map=%04x\n",
					index,
					bands[index]->vht_cap.vht_supported,
					bands[index]->vht_cap.cap,
					bands[index]->vht_cap.vht_mcs.rx_mcs_map,
					bands[index]->vht_cap.vht_mcs.tx_mcs_map));
			}
#endif /* KERNEL >= 3.6 || CUSTOMER_HW5 */
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
#ifdef CFG80211_6G_SUPPORT
	wiphy->bands[IEEE80211_BAND_6GHZ] = bands[IEEE80211_BAND_6GHZ];
#endif /* CFG80211_6G_SUPPORT */

	/* check if any bands populated otherwise makes 2Ghz as default */
	if (wiphy->bands[IEEE80211_BAND_2GHZ] == NULL &&
#ifdef CFG80211_6G_SUPPORT
		wiphy->bands[IEEE80211_BAND_6GHZ] == NULL &&
#endif /* CFG80211_6G_SUPPORT */
		wiphy->bands[IEEE80211_BAND_5GHZ] == NULL) {
		/* Setup 2Ghz band as default */
		wiphy->bands[IEEE80211_BAND_2GHZ] = &__wl_band_2ghz;
	}

	if (notify) {
		if (!IS_REGDOM_SELF_MANAGED(wiphy)) {
			WL_UPDATE_CUSTOM_REGULATORY(wiphy);
			wl_notify_regd(wiphy, NULL);
		}
	}

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
	s32 ret = 0;

	struct net_info *netinfo = NULL;
	struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);
	struct wireless_dev *wdev = ndev->ieee80211_ptr;
	dhd_pub_t *dhd =  (dhd_pub_t *)(cfg->pub);
#ifdef WLTDLS
	u32 tdls;
#endif /* WLTDLS */
	u16 wl_iftype = 0;
	u16 wl_mode = 0;
	u8 ioctl_buf[WLC_IOCTL_SMLEN];

	WL_DBG(("In\n"));

#if defined(__linux__)
	if (!dhd_download_fw_on_driverload) {
#endif
		err = wl_create_event_handler(cfg);
		if (err) {
			WL_ERR(("wl_create_event_handler failed\n"));
			return err;
		}
		wl_init_event_handler(cfg);
#if defined(__linux__)
	}
#endif
	/* Reserve 0x8000 toggle bit for P2P GO/GC */
	cfg->vif_macaddr_mask = 0x8000;

#if defined(BCMDONGLEHOST)
	err = dhd_config_dongle(cfg);
	if (unlikely(err))
		return err;
#endif /* defined(BCMDONGLEHOST) */

#ifdef SHOW_LOGTRACE
	/* Start the event logging */
	wl_add_remove_eventmsg(ndev, WLC_E_TRACE, TRUE);
#endif /* SHOW_LOGTRACE */

	(void)memcpy_s(wdev->wiphy->perm_addr, ETHER_ADDR_LEN,
		bcmcfg_to_prmry_ndev(cfg)->perm_addr, ETHER_ADDR_LEN);
	/* Always bring up interface in STA mode.
	* Did observe , if previous SofAP Bringup/cleanup
	* is not done properly, iftype is stuck with AP mode.
	* So during next wlan0 up, forcing the type to STA
	*/
	netinfo = wl_get_netinfo_by_wdev(cfg, wdev);
	if (!netinfo) {
		WL_ERR(("there is no netinfo\n"));
		return -ENODEV;
	}

	if (ndev->ieee80211_ptr->iftype == NL80211_IFTYPE_AP) {
		/* AP on primary interface case: Supplicant will
		 * set mode first and then do dev_open. so in this
		 * case, the type will already be set.
		 */
		netinfo->iftype = WL_IF_TYPE_AP;
	} else {
		ndev->ieee80211_ptr->iftype = NL80211_IFTYPE_STATION;
		netinfo->iftype = WL_IF_TYPE_STA;
	}

	if (cfg80211_to_wl_iftype(wdev->iftype, &wl_iftype, &wl_mode) < 0) {
		return -EINVAL;
	}
	if (!dhd->fw_preinit) {
		err = wl_config_infra(cfg, ndev, wl_iftype);
		if (unlikely(err && err != -EINPROGRESS)) {
			WL_ERR(("wl_config_infra failed\n"));
			if (err == -1) {
				WL_ERR(("return error %d\n", err));
				return err;
			}
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

	/* Update wlc version in cfg struct already queried as part of DHD  initialization */
	cfg->wlc_ver.wlc_ver_major = dhd->wlc_ver_major;
	cfg->wlc_ver.wlc_ver_minor = dhd->wlc_ver_minor;

	if ((ret = wldev_iovar_getbuf(ndev, "scan_ver", NULL, 0,
		ioctl_buf, sizeof(ioctl_buf), NULL)) == BCME_OK) {
		WL_INFORM_MEM(("scan_params v2\n"));
		/* use scan_params ver2 */
		cfg->scan_params_v2 = true;
	} else {
		if (ret == BCME_UNSUPPORTED) {
			WL_INFORM(("scan_ver, UNSUPPORTED\n"));
			ret = BCME_OK;
		} else {
			WL_INFORM(("get scan_ver err(%d)\n", ret));
		}
	}

	if (((cfg->wlc_ver.wlc_ver_major == MIN_JOINEXT_V1_BR1_FW_MAJOR) &&
			(cfg->wlc_ver.wlc_ver_minor == MIN_JOINEXT_V1_BR1_FW_MINOR)) ||
			((cfg->wlc_ver.wlc_ver_major == MIN_JOINEXT_V1_BR2_FW_MAJOR) &&
			(cfg->wlc_ver.wlc_ver_minor >= MIN_JOINEXT_V1_BR2_FW_MINOR)) ||
			(cfg->wlc_ver.wlc_ver_major >= MIN_JOINEXT_V1_FW_MAJOR)) {
		cfg->join_iovar_ver = WL_EXTJOIN_VERSION_V1;
		WL_INFORM_MEM(("join_ver:%d\n", cfg->join_iovar_ver));
	}

#ifdef DHD_LOSSLESS_ROAMING
	del_timer_sync(&cfg->roam_timeout);
#endif /* DHD_LOSSLESS_ROAMING */

	err = dhd_monitor_init(cfg->pub);

#ifdef WL_HOST_BAND_MGMT
	/* By default the curr_band is initialized to BAND_AUTO */
	if ((ret = wl_cfg80211_set_band(ndev, WLC_BAND_AUTO)) < 0) {
		if (ret == BCME_UNSUPPORTED) {
			/* Don't fail the initialization, lets just
			 * fall back to the original method
			 */
			WL_ERR(("WL_HOST_BAND_MGMT defined, "
				"but roam_band iovar not supported \n"));
		} else {
			WL_ERR(("roam_band failed. ret=%d", ret));
			err = -1;
		}
	}
#endif /* WL_HOST_BAND_MGMT */
	/* Reset WES mode to 0 */
	cfg->wes_mode = OFF;
	cfg->ncho_mode = OFF;
	cfg->ncho_band = WLC_BAND_AUTO;
#ifdef WBTEXT
	/* when wifi up, set roam_prof to default value */
	if (dhd->wbtext_support) {
		if (dhd->op_mode & DHD_FLAG_STA_MODE) {
			if (!dhd->fw_preinit) {
				wl_cfg80211_wbtext_set_default(ndev);
			}
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
#ifdef WL_IFACE_MGMT
#ifdef CUSTOM_IF_MGMT_POLICY
	cfg->iface_data.policy = CUSTOM_IF_MGMT_POLICY;
#else
	cfg->iface_data.policy = WL_IF_POLICY_DEFAULT;
#endif /*  CUSTOM_IF_MGMT_POLICY */
#endif /* WL_IFACE_MGMT */
#ifdef WL_NAN
#ifdef WL_NANP2P
	if (FW_SUPPORTED(dhd, nanp2p)) {
		/* Enable NANP2P concurrent support */
		cfg->conc_disc = WL_NANP2P_CONC_SUPPORT;
		WL_INFORM_MEM(("nan + p2p conc discovery is supported\n"));
		cfg->nan_p2p_supported = true;
	}
#endif /* WL_NANP2P */
#endif /* WL_NAN  */

#ifdef WL_SAR_TX_POWER
	cfg->wifi_tx_power_mode = WIFI_POWER_SCENARIO_INVALID;
#endif /* WL_SAR_TX_POWER */

#if defined(OEM_ANDROID) && defined(DHCP_SCAN_SUPPRESS)
	/* wlan scan_supp timer and work thread info */
	init_timer_compat(&cfg->scan_supp_timer, wl_cfg80211_scan_supp_timerfunc, cfg);
	INIT_WORK(&cfg->wlan_work, wl_cfg80211_work_handler);
#endif /* DHCP_SCAN_SUPPRESS */

	INIT_DELAYED_WORK(&cfg->pm_enable_work, wl_cfg80211_work_handler);
	wl_set_drv_status(cfg, READY, ndev);

	return err;
}

static s32 __wl_cfg80211_down(struct bcm_cfg80211 *cfg)
{
	s32 err = 0;
	struct net_info *iter, *next;
	struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);
#if defined(WL_CFG80211) && \
	(defined(WL_ENABLE_P2P_IF) || defined(WL_NEWCFG_PRIVCMD_SUPPORT)) && \
	!defined(PLATFORM_SLP)
	struct net_device *p2p_net = cfg->p2p_net;
#endif /* WL_CFG80211 && (WL_ENABLE_P2P_IF || WL_NEWCFG_PRIVCMD_SUPPORT) && !PLATFORM_SLP */
#ifdef BCMDONGLEHOST
	dhd_pub_t *dhd =  (dhd_pub_t *)(cfg->pub);
#endif /* BCMDONGLEHOST */
	WL_INFORM_MEM(("cfg80211 down\n"));

	/* Check if cfg80211 interface is already down */
	if (!wl_get_drv_status(cfg, READY, ndev)) {
		WL_DBG(("cfg80211 interface is already down\n"));
		return err;	/* it is even not ready */
	}

#ifdef SHOW_LOGTRACE
	/* Stop the event logging */
	wl_add_remove_eventmsg(ndev, WLC_E_TRACE, FALSE);
#endif /* SHOW_LOGTRACE */

	/* clear vendor OUI list */
	wl_vndr_ies_clear_vendor_oui_list(cfg);

	/* clear timestamps */
	CLR_TS(cfg, scan_start);
	CLR_TS(cfg, scan_cmplt);
	CLR_TS(cfg, conn_start);
	CLR_TS(cfg, conn_cmplt);
	CLR_TS(cfg, authorize_start);
	CLR_TS(cfg, authorize_cmplt);

	/* Delete pm_enable_work */
	wl_add_remove_pm_enable_work(cfg, WL_PM_WORKQ_DEL);

	if (cfg->loc.in_progress) {
		/* Listen in progress */
		if (delayed_work_pending(&cfg->loc.work)) {
			cancel_delayed_work_sync(&cfg->loc.work);
		}
		wl_cfgscan_notify_listen_complete(cfg);
	}

	if (delayed_work_pending(&cfg->ap_work)) {
		cancel_delayed_work_sync(&cfg->ap_work);
	}

	if (cfg->p2p_supported) {
		wl_clr_p2p_status(cfg, GO_NEG_PHASE);
#ifdef PROP_TXSTATUS_VSDB
#if defined(BCMSDIO) || defined(BCMDBUS)
		if (wl_cfgp2p_vif_created(cfg)) {
			bool enabled = false;
			dhd_wlfc_get_enable(dhd, &enabled);
			/* WLFC should be turned off
			*	while unloading dhd driver in IBSS or SoftAP mode
			*/
			if (enabled && cfg->wlfc_on && dhd->op_mode != DHD_FLAG_HOSTAP_MODE &&
				dhd->op_mode != DHD_FLAG_IBSS_MODE) {
				dhd_wlfc_deinit(dhd);
				cfg->wlfc_on = false;
			}
		}
#endif /* BCMSDIO || BCMDBUS */
#endif /* PROP_TXSTATUS_VSDB */
	}

#if defined(OEM_ANDROID) && defined(DHCP_SCAN_SUPPRESS)
	/* Force clear of scan_suppress */
	if (cfg->scan_suppressed)
		wl_cfg80211_scan_suppress(ndev, 0);
	del_timer_sync(&cfg->scan_supp_timer);
	cancel_work_sync(&cfg->wlan_work);
#endif /* DHCP_SCAN_SUPPRESS */

#ifdef WL_SAR_TX_POWER
	cfg->wifi_tx_power_mode = WIFI_POWER_SCENARIO_INVALID;
#endif /* WL_SAR_TX_POWER */
	if (!dhd_download_fw_on_driverload) {
		/* For built-in drivers/other drivers that do reset on
		 * "ifconfig <primary_iface> down", cleanup any left
		 * over interfaces
		 */
		wl_cfg80211_cleanup_virtual_ifaces(cfg, false);
	}
	/* Clear used mac addr mask */
	cfg->vif_macaddr_mask = 0;

#ifdef BCMDONGLEHOST
	if (dhd->up)
#endif /* BCMDONGLEHOST */
	{
		/* If primary BSS is operational (for e.g SoftAP), bring it down */
		if (wl_cfg80211_bss_isup(ndev, 0)) {
			if (wl_cfg80211_bss_up(cfg, ndev, 0, 0) < 0)
				WL_ERR(("BSS down failed \n"));
		}

		/* clear all the security setting on primary Interface */
		wl_cfg80211_clear_security(cfg);
	}

	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	for_each_ndev(cfg, iter, next) {
		GCC_DIAGNOSTIC_POP();
		if (iter->ndev) /* p2p discovery iface is null */
			wl_set_drv_status(cfg, SCAN_ABORTING, iter->ndev);
	}
#ifdef WL_SDO
	wl_cfg80211_sdo_deinit(cfg);
#endif

#ifdef P2P_LISTEN_OFFLOADING
	wl_cfg80211_p2plo_deinit(cfg);
#endif /* P2P_LISTEN_OFFLOADING */

	/* cancel and notify scan complete, if scan request is pending */
	wl_cfgscan_cancel_scan(cfg);
	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	for_each_ndev(cfg, iter, next) {
		GCC_DIAGNOSTIC_POP();
		/* p2p discovery iface ndev ptr could be null */
		if (iter->ndev == NULL)
			continue;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
		WL_INFORM_MEM(("wl_cfg80211_down. connection state bit status: [%u:%u:%u:%u]\n",
			wl_get_drv_status(cfg, CONNECTING, ndev),
			wl_get_drv_status(cfg, CONNECTED, ndev),
			wl_get_drv_status(cfg, DISCONNECTING, ndev),
			wl_get_drv_status(cfg, NESTED_CONNECT, ndev)));

		if (wl_get_drv_status(cfg, CONNECTED, iter->ndev)) {
			CFG80211_DISCONNECTED(iter->ndev, 0, NULL, 0, false, GFP_KERNEL);
			wl_clr_drv_status(cfg, AUTHORIZED, iter->ndev);
		}

		if ((iter->ndev->ieee80211_ptr->iftype == NL80211_IFTYPE_STATION) &&
			wl_get_drv_status(cfg, CONNECTING, iter->ndev)) {

			u8 *latest_bssid = wl_read_prof(cfg, ndev, WL_PROF_LATEST_BSSID);
			struct wiphy *wiphy = bcmcfg_to_wiphy(cfg);
			struct wireless_dev *wdev = ndev->ieee80211_ptr;
			struct cfg80211_bss *bss = CFG80211_GET_BSS(wiphy, NULL, latest_bssid,
				wdev->ssid, wdev->ssid_len);

			BCM_REFERENCE(bss);

			CFG80211_CONNECT_RESULT(ndev,
				latest_bssid, bss, NULL, 0, NULL, 0,
				WLAN_STATUS_UNSPECIFIED_FAILURE,
				GFP_KERNEL);
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
		wl_clr_drv_status(cfg, NESTED_CONNECT, iter->ndev);
		wl_clr_drv_status(cfg, CFG80211_CONNECT, iter->ndev);
	}
	bcmcfg_to_prmry_ndev(cfg)->ieee80211_ptr->iftype =
		NL80211_IFTYPE_STATION;
#if defined(WL_CFG80211) && \
	(defined(WL_ENABLE_P2P_IF) || defined(WL_NEWCFG_PRIVCMD_SUPPORT)) && \
	!defined(PLATFORM_SLP)
#ifdef SUPPORT_DEEP_SLEEP
	if (!trigger_deep_sleep)
#endif /* SUPPORT_DEEP_SLEEP */
		if (p2p_net)
			dev_close(p2p_net);
#endif /* WL_CFG80211 && (WL_ENABLE_P2P_IF || WL_NEWCFG_PRIVCMD_SUPPORT)&& !PLATFORM_SLP */

	/* Avoid deadlock from wl_cfg80211_down */
#if defined(BCMDONGLEHOST) && defined(__linux__)
	if (!dhd_download_fw_on_driverload) {
#endif
		mutex_unlock(&cfg->usr_sync);
		wl_destroy_event_handler(cfg);
		mutex_lock(&cfg->usr_sync);
#if defined(BCMDONGLEHOST) && defined(__linux__)
	}
#endif

	wl_flush_eq(cfg);
	wl_link_down(cfg);
	if (cfg->p2p_supported) {
		if (timer_pending(&cfg->p2p->listen_timer))
			del_timer_sync(&cfg->p2p->listen_timer);
		wl_cfgp2p_down(cfg);
	}

	del_timer_sync(&cfg->scan_timeout);

	wl_cfg80211_clear_mgmt_vndr_ies(cfg);

#if defined(BCMDONGLEHOST) && defined(OEM_ANDROID)
	DHD_OS_SCAN_WAKE_UNLOCK((dhd_pub_t *)(cfg->pub));
#endif

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
	cfg->scan_params_v2 = false;

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
#ifdef BCMDONGLEHOST
	dhd_pub_t *dhd;
#endif /* BCMDONGLEHOST */
#ifdef DISABLE_PM_BCNRX
	s32 interr = 0;
	uint param = 0;
	s8 iovbuf[WLC_IOCTL_SMLEN];
#endif /* DISABLE_PM_BCNRX */
#ifdef WL_USE_RANDOMIZED_SCAN
	uint8 random_addr[ETHER_ADDR_LEN] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
#endif /* WL_USE_RANDOMIZED_SCAN */
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
#ifdef WL_EXT_IAPSTA
	wl_ext_in4way_sync(net, STA_NO_BTC_IN4WAY, WL_EXT_STATUS_DISCONNECTED, NULL);
#endif

	mutex_lock(&cfg->usr_sync);
#if defined(BCMDONGLEHOST)
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
#if defined(BCMSUP_4WAY_HANDSHAKE)
	if (dhd->fw_4way_handshake) {
		/* This is a hacky method to indicate fw 4WHS support and
		 * is used only for kernels (kernels < 3.14). For newer
		 * kernels, we would be using vendor extn. path to advertise
		 * FW based 4-way handshake feature support.
		 */
		cfg->wdev->wiphy->features |= NL80211_FEATURE_FW_4WAY_HANDSHAKE;
	}
#endif /* BCMSUP_4WAY_HANDSHAKE */
#endif /* defined(BCMDONGLEHOST) */
	err = __wl_cfg80211_up(cfg);
	if (unlikely(err))
		WL_ERR(("__wl_cfg80211_up failed\n"));

#ifdef ROAM_CHANNEL_CACHE
	if (init_roam_cache(cfg, ioctl_version) == 0) {
		/* Enable support for Roam cache */
		cfg->rcc_enabled = true;
		WL_ERR(("Roam channel cache enabled\n"));
	} else {
		WL_ERR(("Failed to enable RCC.\n"));
	}
#endif /* ROAM_CHANNEL_CACHE */
#ifdef WL_USE_RANDOMIZED_SCAN
	/* Call scanmac only for valid configuration */
	if (wl_cfg80211_scan_mac_enable(net)) {
		WL_ERR(("%s : randmac enable failed\n", __FUNCTION__));
	} else {
		/* scanmac enabled. apply configuration */
		if (wl_cfg80211_scan_mac_config(net, random_addr, NULL)) {
			WL_ERR(("%s : failed to set randmac config for scan\n", __FUNCTION__));
			/* if config fails, disable scan mac */
			wl_cfg80211_scan_mac_disable(net);
		}
	}
#endif /* WL_USE_RANDOMIZED_SCAN */
/* WAR: disable pm_bcnrx , scan_ps for BCM4354 WISOL module.
 * WISOL module have ANT_1 Rx sensitivity issue.
 */
#if defined(FORCE_DISABLE_SINGLECORE_SCAN)
	dhd_force_disable_singlcore_scan(dhd);
#endif /* FORCE_DISABLE_SINGLECORE_SCAN */

	/* IOVAR configurations with 'up' condition */
#ifdef DISABLE_PM_BCNRX
	interr = wldev_iovar_setbuf(net, "pm_bcnrx", (char *)&param, sizeof(param), iovbuf,
			sizeof(iovbuf), &cfg->ioctl_buf_sync);

	if (unlikely(interr)) {
		WL_ERR(("Set pm_bcnrx returned (%d)\n", interr));
	}
#endif /* DISABLE_PM_BCNRX */
#ifdef WL_CHAN_UTIL
	interr = wl_cfg80211_start_bssload_report(net);
	if (unlikely(interr)) {
		WL_ERR(("%s: Failed to start bssload_report eventing, err=%d\n",
			__FUNCTION__, interr));
	}
#endif /* WL_CHAN_UTIL */

	mutex_unlock(&cfg->usr_sync);

#ifdef WLAIBSS_MCHAN
	bcm_cfg80211_add_ibss_if(cfg->wdev->wiphy, IBSS_IF_NAME);
#endif /* WLAIBSS_MCHAN */
	cfg->spmk_info_list->pmkids.count = 0;
	return err;
}

/* Private Event to Supplicant with indication that chip hangs */
int wl_cfg80211_hang(struct net_device *dev, u16 reason)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
#ifdef BCMDONGLEHOST
	dhd_pub_t *dhd;
#if defined(SOFTAP_SEND_HANGEVT)
	/* specifc mac address used for hang event */
	uint8 hang_mac[ETHER_ADDR_LEN] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11};
#endif /* SOFTAP_SEND_HANGEVT */
#endif /* BCMDONGLEHOST */
	if (!cfg) {
		return BCME_ERROR;
	}

	RETURN_EIO_IF_NOT_UP(cfg);

#ifdef BCMDONGLEHOST
	dhd = (dhd_pub_t *)(cfg->pub);
#if defined(DHD_HANG_SEND_UP_TEST)
	if (dhd->req_hang_type) {
		WL_ERR(("wl_cfg80211_hang, Clear HANG test request 0x%x\n",
			dhd->req_hang_type));
		dhd->req_hang_type = 0;
	}
#endif /* DHD_HANG_SEND_UP_TEST */
	if ((dhd->hang_reason <= HANG_REASON_MASK) || (dhd->hang_reason >= HANG_REASON_MAX)) {
		WL_ERR(("wl_cfg80211_hang, Invalid hang reason 0x%x\n",
			dhd->hang_reason));
		dhd->hang_reason = HANG_REASON_UNKNOWN;
	}
#ifdef DHD_USE_EXTENDED_HANG_REASON
	/* The proper dhd->hang_reason handling codes should be implemented
	 * in the WPA Supplicant/Hostapd or Android framework.
	 * If not, HANG event may not be sent to Android framework and
	 * driver cannot be reloaded.
	 * Please do not enable DHD_USE_EXTENDED_HANG_REASON if your Android platform
	 * cannot handle the dhd->hang_reason value.
	 */
	if (dhd->hang_reason != 0) {
		reason = dhd->hang_reason;
	}
#endif /* DHD_USE_EXTENDED_HANG_REASON */
	WL_ERR(("In : chip crash eventing, reason=0x%x\n", (uint32)(dhd->hang_reason)));
#else
	WL_ERR(("In : chip crash eventing\n"));
#endif /* BCMDONGLEHOST */

	wl_add_remove_pm_enable_work(cfg, WL_PM_WORKQ_DEL);
#ifdef BCMDONGLEHOST
#ifdef SOFTAP_SEND_HANGEVT
	if (dhd->op_mode & DHD_FLAG_HOSTAP_MODE) {
		cfg80211_del_sta(dev, hang_mac, GFP_ATOMIC);
	} else
#endif /* SOFTAP_SEND_HANGEVT */
#endif /* BCMDONGLEHOST */
	{
		if (dhd->up == TRUE) {
#ifdef WL_CFGVENDOR_SEND_HANG_EVENT
#ifdef CUSOMER_HW4
			wl_cfgvendor_send_hang_event(dev, reason,
					dhd->hang_info, dhd->hang_info_cnt);
#else
			wl_cfgvendor_simple_hang_event(dev, reason);
#endif
#else
			CFG80211_DISCONNECTED(dev, reason, NULL, 0, false, GFP_KERNEL);
#endif /* WL_CFGVENDOR_SEND_HANG_EVENT */
		}
	}
#if defined(RSSIAVG)
	wl_free_rssi_cache(&cfg->g_rssi_cache_ctrl);
#endif
#if defined(BSSCACHE)
	wl_free_bss_cache(&cfg->g_bss_cache_ctrl);
#endif
	if (cfg != NULL) {
		/* Do we need to call wl_cfg80211_down here ? */
		wl_link_down(cfg);
	}
	return 0;
}

s32 wl_cfg80211_down(struct net_device *dev)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
#ifdef RTT_SUPPORT
	dhd_pub_t *dhdp;
#endif /* RTT_SUPPORT */

	s32 err = BCME_ERROR;

	WL_DBG(("In\n"));

	if (cfg && (cfg == wl_cfg80211_get_bcmcfg())) {
#ifdef WL_NAN
		mutex_lock(&cfg->if_sync);
		wl_cfgnan_check_nan_disable_pending(cfg, true, false);
		mutex_unlock(&cfg->if_sync);
#endif /* WL_NAN */

#ifdef RTT_SUPPORT
		dhdp = (dhd_pub_t *)(cfg->pub);
		if (dhdp->rtt_state) {
			dhd_rtt_deinit(dhdp);
		}
#endif /* RTT_SUPPORT */
		mutex_lock(&cfg->usr_sync);
#if defined(RSSIAVG)
		wl_free_rssi_cache(&cfg->g_rssi_cache_ctrl);
#endif
#if defined(BSSCACHE)
		wl_free_bss_cache(&cfg->g_bss_cache_ctrl);
#endif
		err = __wl_cfg80211_down(cfg);
		mutex_unlock(&cfg->usr_sync);
	}

	return err;
}

void
wl_cfg80211_sta_ifdown(struct net_device *dev)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	WL_DBG(("In\n"));

	if (cfg) {
		/* cancel scan if anything pending */
		wl_cfgscan_cancel_scan(cfg);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
		if ((dev->ieee80211_ptr->iftype == NL80211_IFTYPE_STATION) &&
			wl_get_drv_status(cfg, CONNECTED, dev)) {
			CFG80211_DISCONNECTED(dev, 0, NULL, 0, false, GFP_KERNEL);
		}
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)) */
	}
}

void *wl_read_prof(struct bcm_cfg80211 *cfg, struct net_device *ndev, s32 item)
{
	unsigned long flags;
	void *rptr = NULL;
	struct wl_profile *profile = wl_get_profile_by_netdev(cfg, ndev);

	if (!profile)
		return NULL;
	WL_CFG_DRV_LOCK(&cfg->cfgdrv_lock, flags);
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
	case WL_PROF_LATEST_BSSID:
		rptr = profile->latest_bssid;
		break;
	}
	WL_CFG_DRV_UNLOCK(&cfg->cfgdrv_lock, flags);
	if (!rptr)
		WL_ERR(("invalid item (%d)\n", item));
	return rptr;
}

s32
wl_update_prof(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *e, const void *data, s32 item)
{
	s32 err = 0;
	const struct wlc_ssid *ssid;
	unsigned long flags;
	struct wl_profile *profile = wl_get_profile_by_netdev(cfg, ndev);

	if (!profile)
		return WL_INVALID;
	WL_CFG_DRV_LOCK(&cfg->cfgdrv_lock, flags);
	switch (item) {
	case WL_PROF_SSID:
		ssid = (const wlc_ssid_t *) data;
		bzero(profile->ssid.SSID,
			sizeof(profile->ssid.SSID));
		profile->ssid.SSID_len = MIN(ssid->SSID_len, DOT11_MAX_SSID_LEN);
		memcpy(profile->ssid.SSID, ssid->SSID, profile->ssid.SSID_len);
		break;
	case WL_PROF_BSSID:
		if (data)
			memcpy(profile->bssid, data, ETHER_ADDR_LEN);
		else
			bzero(profile->bssid, ETHER_ADDR_LEN);
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
		profile->channel = *(const chanspec_t *)data;
		break;
	case WL_PROF_LATEST_BSSID:
		if (data) {
			memcpy_s(profile->latest_bssid, sizeof(profile->latest_bssid),
					data, ETHER_ADDR_LEN);
		} else {
			memset_s(profile->latest_bssid, sizeof(profile->latest_bssid),
					0, ETHER_ADDR_LEN);
		}
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}
	WL_CFG_DRV_UNLOCK(&cfg->cfgdrv_lock, flags);

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

static __used bool wl_is_ibssstarter(struct bcm_cfg80211 *cfg)
{
	return cfg->ibss_starter;
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

	WL_CFG_EQ_LOCK(&cfg->eq_lock, flags);
	return flags;
}

static void wl_unlock_eq(struct bcm_cfg80211 *cfg, unsigned long flags)
{
	WL_CFG_EQ_UNLOCK(&cfg->eq_lock, flags);
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
		memcpy((void *)&p2pdev_addr, (void *)&primary_mac, ETHER_ADDR_LEN);
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

#ifdef WL_SDO
#define MAX_QR_LEN NLMSG_GOODSIZE

typedef struct wl_cfg80211_dev_info {
	u16 band;
	u16 freq;
	s16 rssi;
	u16 ie_len;
	u8 bssid[ETH_ALEN];
} wl_cfg80211_dev_info_t;

static s32
wl_notify_device_discovery(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	int err = 0;
	u32 event = ntoh32(e->event_type);
	wl_cfg80211_dev_info_t info;
	wl_bss_info_t *bi = NULL;
	struct net_device *ndev = NULL;
	u8 *buf = NULL;
	u32 buflen = 0;
	u16 channel = 0;
	wl_escan_result_t *escan_result;
	chanspec_t chspec = INVCHANSPEC;

	WL_SD(("Enter. type:%d \n", event));

	if ((event != WLC_E_P2PO_ADD_DEVICE) && (event != WLC_E_P2PO_DEL_DEVICE)) {
		WL_ERR(("Unknown Event\n"));
		return -EINVAL;
	}

	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);

	mutex_lock(&cfg->usr_sync);
	if (event == WLC_E_P2PO_DEL_DEVICE) {
		WL_SD(("DEV_LOST MAC:"MACDBG" \n", MAC2STRDBG(e->addr.octet)));
		err = wl_genl_send_msg(ndev, event, (const u8 *)e->addr.octet, ETH_ALEN, 0, 0);
	} else {

		escan_result = (wl_escan_result_t *) data;

		if (dtoh16(escan_result->bss_count) != 1) {
			WL_ERR(("Invalid bss_count %d: ignoring\n", escan_result->bss_count));
			err = -EINVAL;
			goto exit;
		}

		bi = escan_result->bss_info;
		buflen = dtoh32(bi->length);
		if (unlikely(buflen > WL_BSS_INFO_MAX)) {
			WL_DBG(("Beacon is larger than buffer. Discarding\n"));
			err = -EINVAL;
			goto exit;
		}

		/* Update sub-header */
		bzero(&info, sizeof(wl_cfg80211_dev_info_t));
		chspec = wl_chspec_driver_to_host(bi->chanspec);
		channel = wf_chspec_ctlchan(chspec);
		info.freq = wl_channel_to_frequency(channel, CHSPEC_BAND(chspec));
		info.rssi = wl_rssi_offset(dtoh16(bi->RSSI));
		memcpy(info.bssid, &bi->BSSID, ETH_ALEN);
		info.ie_len = buflen;

		WL_SD(("DEV_FOUND band:%x Freq:%d rssi:%x "MACDBG" \n",
			info.band, info.freq, info.rssi, MAC2STRDBG(info.bssid)));

		buf =  ((u8 *) bi) + bi->ie_offset;
		err = wl_genl_send_msg(ndev, event, buf,
			buflen, (u8 *)&info, sizeof(wl_cfg80211_dev_info_t));
	}
exit:
	mutex_unlock(&cfg->usr_sync);
	return err;
}

s32
wl_cfg80211_sdo_init(struct bcm_cfg80211 *cfg)
{
	if (cfg->sdo) {
		WL_SD(("SDO already initialized\n"));
		return 0;
	}

	cfg->sdo = (sd_offload_t *)MALLOCZ(cfg->osh, sizeof(sd_offload_t));
	if (!cfg->sdo) {
		WL_ERR(("MALLOCZ failed for SDO \n"));
		return -ENOMEM;
	}

	return  0;
}

s32
wl_cfg80211_sdo_deinit(struct bcm_cfg80211 *cfg)
{
	s32 bssidx;
	int ret = 0;
	int sdo_pause = 0;
	if (!cfg || !cfg->p2p) {
		WL_ERR(("Wl %p or cfg->p2p %p is null\n",
			cfg, cfg ? cfg->p2p : 0));
		return 0;
	}

	bssidx = wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE);
	if (!cfg->sdo) {
		WL_DBG(("SDO Not Initialized. Do nothing. \n"));
		return 0;
	}
	if (cfg->sdo->dd_state &&
		(ret = wldev_iovar_setbuf_bsscfg(bcmcfg_to_prmry_ndev(cfg),
		"p2po_stop", (void*)&sdo_pause, sizeof(sdo_pause),
		cfg->ioctl_buf, WLC_IOCTL_SMLEN, bssidx, &cfg->ioctl_buf_sync)) < 0) {
		WL_ERR(("p2po_stop Failed :%d\n", ret));
	}
	MFREE(cfg->osh, cfg->sdo, sizeof(sd_offload_t));

	WL_SD(("SDO Deinit Done \n"));

	return  0;
}

s32
wl_cfg80211_resume_sdo(struct net_device *dev, struct bcm_cfg80211 *cfg)
{
	wl_sd_listen_t sd_listen;
	int ret = 0;
	s32 bssidx =  wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE);

	WL_DBG(("Enter\n"));

	if (!cfg->sdo) {
		return -EINVAL;
	}

	if (dev == NULL)
		dev = bcmcfg_to_prmry_ndev(cfg);

	/* Disable back the ESCAN events for the offload */
	wl_add_remove_eventmsg(dev, WLC_E_ESCAN_RESULT, false);

	/* Resume according to the saved state */
	if (cfg->sdo->dd_state == WL_DD_STATE_SEARCH) {
		if ((ret = wldev_iovar_setbuf_bsscfg(dev, "p2po_find", NULL, 0,
			cfg->ioctl_buf, WLC_IOCTL_SMLEN, bssidx, &cfg->ioctl_buf_sync)) < 0) {
			WL_ERR(("p2po_find Failed :%d\n", ret));
		}
	} else if (cfg->sdo->dd_state == WL_DD_STATE_LISTEN) {
		/* Need to save the listen params in the set context
		 * so that those values can be restored in the resume context
		 */
		sd_listen.interval = cfg->sdo->sd_listen.interval;
		sd_listen.period = cfg->sdo->sd_listen.period;

		if ((ret = wldev_iovar_setbuf_bsscfg(dev, "p2po_listen", (void*)&sd_listen,
			sizeof(wl_sd_listen_t), cfg->ioctl_buf, WLC_IOCTL_SMLEN,
			bssidx, &cfg->ioctl_buf_sync)) < 0) {
			WL_ERR(("p2po_listen Failed :%d\n", ret));
		}

	}

	 /* p2po_stop clears of the eventmask for GAS. Set it back */
	 wl_add_remove_eventmsg(dev, WLC_E_SERVICE_FOUND, true);
	 wl_add_remove_eventmsg(dev, WLC_E_GAS_FRAGMENT_RX, true);
	 wl_add_remove_eventmsg(dev, WLC_E_GAS_COMPLETE, true);

	WL_SD(("SDO Resumed \n"));

	return ret;
}

s32 wl_cfg80211_pause_sdo(struct net_device *dev, struct bcm_cfg80211 *cfg)
{

	int ret = 0;
	s32 bssidx =  wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE);
	int sdo_pause = 1;

	WL_DBG(("Enter \n"));

	if (!cfg->sdo) {
		WL_ERR(("SDO not initialized \n"));
		return -EINVAL;
	}

	if (dev == NULL)
		dev = bcmcfg_to_prmry_ndev(cfg);

	if ((ret = wldev_iovar_setbuf_bsscfg(dev, "p2po_stop",
		(void*)&sdo_pause, sizeof(sdo_pause),
		cfg->ioctl_buf, WLC_IOCTL_SMLEN, bssidx, &cfg->ioctl_buf_sync)) < 0) {
		WL_ERR(("p2po_stop Failed :%d\n", ret));
	}

	/* Enable back the ESCAN events for the SCAN */
	wl_add_remove_eventmsg(dev, WLC_E_ESCAN_RESULT, true);

	WL_SD(("SDO Paused \n"));

	return ret;
}

static s32
wl_svc_resp_handler(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	u32 event = ntoh32(e->event_type);
	struct net_device *ndev = NULL;
	const u8 *dst_mac = (const u8 *)e->addr.octet;
	int ret = 0;
	wl_event_sd_t *gas = NULL;
	int status = ntoh32(e->status);
	sdo_event_t sdo_hdr;
	u32 data_len = ntoh32(e->datalen);
	u8 *data_ptr = NULL;
	u32 tot_len = 0;

	WL_SD(("Enter event_type:%d status:%d\n", event, status));

	if (!cfg->sdo) {
		WL_ERR(("SDO Not initialized \n"));
		return -EINVAL;
	}

	if (!(cfg->sdo->sd_state & WL_SD_SEARCH_SVC)) {
		/* We are not searching for any service. Drop
		 * any bogus Event
		 */
		WL_ERR(("Bogus SDO Event. Do nothing.. \n"));
		return -1;
	}

	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);

	mutex_lock(&cfg->usr_sync);
	if (event == WLC_E_SERVICE_FOUND) {

		if ((status != WLC_E_STATUS_SUCCESS) && (status != WLC_E_STATUS_PARTIAL)) {
			WL_ERR(("WLC_E_SERVICE_FOUND: unknown status \n"));
			goto exit;
		}

		gas = (wl_event_sd_t *)data;
		if (!gas) {
			ret = -EINVAL;
			goto exit;
		}

		bzero(&sdo_hdr, sizeof(sdo_event_t));
		sdo_hdr.freq = wl_channel_to_frequency(gas->channel, WL_CHANSPEC_BAND_2G);
		sdo_hdr.count = gas->count;
		memcpy(sdo_hdr.addr, dst_mac, ETH_ALEN);
		data_ptr = (char *)gas->tlv;
		tot_len = data_len - (sizeof(wl_event_sd_t) - sizeof(wl_sd_tlv_t));

		WL_SD(("WLC_E_SERVICE_FOUND "MACDBG" data_len:%d tlv_count:%d \n",
			MAC2STRDBG(dst_mac), data_len, sdo_hdr.count));

		if (tot_len > NLMSG_DEFAULT_SIZE) {
			WL_ERR(("size(%u)  > %lu not supported \n", tot_len, NLMSG_DEFAULT_SIZE));
			ret = -ENOMEM;
			goto exit;
		}

		if (wl_genl_send_msg(ndev, event, data_ptr,
			tot_len, (u8 *)&sdo_hdr, sizeof(sdo_event_t)) < 0)
			WL_ERR(("Couldn't send up the NETLINK Event \n"));
		else
			WL_SD(("GAS event sent up \n"));
	} else {
		WL_ERR(("Unsupported Event: %d \n", event));
	}

exit:
	mutex_unlock(&cfg->usr_sync);
	return ret;
}

s32 wl_cfg80211_DsdOffloadParseProto(char* proto_str, u8* proto)
{
	s32 len = -1;
	int i = 0;

	for (i = 0; i < MAX_SDO_PROTO; i++) {
		if (strncmp(proto_str, wl_sdo_protos[i].str, strlen(wl_sdo_protos[i].str)) == 0) {
			WL_SD(("Matching proto (%d) found \n", wl_sdo_protos[i].val));
			*proto = wl_sdo_protos[i].val;
			len = strlen(wl_sdo_protos[i].str);
			break;
		}
	}
	return len;
}

/*
 * register to search for a UPnP service
 * ./DRIVER P2P_SD_REQ upnp 0x10urn:schemas-upnporg:device:InternetGatewayDevice:1
 *
 * Enable discovery
 * ./cfg p2po_find
*/
#define UPNP_QUERY_VER_OFFSET 3
s32 wl_sd_handle_sd_req(
	struct net_device *dev,
	u8 * buf,
	int len)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	s32 bssidx = 0;
	wl_sd_qr_t *sdreq;
	u8 proto = 0;
	s32 ret = 0;
	u32 tot_len = len + sizeof(wl_sd_qr_t);
	u16 version = 0;

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("find_idx failed\n"));
		return -EINVAL;
	}
	/* Check for the least arg length expected */
	if (!buf || (len < strlen("all"))) {
		WL_ERR(("Wrong Arg\n"));
		return -EINVAL;
	}

	if (tot_len > WLC_IOCTL_MAXLEN) {
		WL_ERR(("Length > %lu not supported \n", MAX_QR_LEN));
		return -EINVAL;
	}

	sdreq = (wl_sd_qr_t *)MALLOCZ(cfg->osh, tot_len);
	if (!sdreq) {
		WL_ERR(("MALLOCZ failed\n"));
		return -ENOMEM;
	}

	WL_SD(("%s Len: %d\n", buf, len));
	if ((ret = wl_cfg80211_DsdOffloadParseProto(buf, &proto)) < 0) {
		WL_ERR(("Unknown proto \n"));
		goto exit;
	}

	sdreq->protocol = proto;
	buf += ret;
	buf++; /* skip the space */
	sdreq->transaction_id = simple_strtoul(buf, NULL, 16);
	WL_SD(("transaction_id:%d\n", sdreq->transaction_id));
	buf += sizeof(sdreq->transaction_id);

	if (*buf == '\0') {
		WL_SD(("No Query present. Proto:%d \n", proto));
		sdreq->query_len = 0;
	} else {
		buf++; /* skip the space */
		/* UPNP version needs to put as binary val */
		if (sdreq->protocol == SVC_RPOTYPE_UPNP) {
			/* Extract UPNP version */
			version = simple_strtoul(buf, NULL, 16);
			buf = buf + UPNP_QUERY_VER_OFFSET;
			buf[0] = version;
			WL_SD(("Upnp version: 0x%x \n", version));
		}

		len = strlen(buf);
		WL_SD(("Len after stripping proto: %d Query: %s\n", len, buf));
		/* copy the query part */
		memcpy(sdreq->qrbuf, buf, len);
		sdreq->query_len = len;
	}

	/* Enable discovery */
	if ((ret = wl_cfgp2p_enable_discovery(cfg, dev, NULL, 0)) < 0) {
		WL_ERR(("cfgp2p_enable discovery failed"));
		goto exit;
	}

	if ((ret = wldev_iovar_setbuf_bsscfg(dev, "p2po_sd_req_resp", (void*)sdreq,
		tot_len, cfg->ioctl_buf, WLC_IOCTL_MAXLEN,
		bssidx, &cfg->ioctl_buf_sync)) < 0) {
		WL_ERR(("Find SVC Failed \n"));
		goto exit;
	}

	cfg->sdo->sd_state |= WL_SD_SEARCH_SVC;

exit:
	MFREE(cfg->osh, sdreq, tot_len);
	return ret;
}

s32 wl_sd_handle_sd_cancel_req(
	struct net_device *dev,
	u8 *buf)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	s32 bssidx =  wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE);

	if (wldev_iovar_setbuf_bsscfg(dev, "p2po_sd_cancel", NULL,
		0, cfg->ioctl_buf, WLC_IOCTL_SMLEN,
		bssidx, &cfg->ioctl_buf_sync) < 0) {
		WL_ERR(("Cancel SD Failed \n"));
		return -EINVAL;
	}

	cfg->sdo->sd_state &= ~WL_SD_SEARCH_SVC;

	return 0;
}

/*
 * register a UPnP service to be discovered
 * ./cfg P2P_SD_SVC_ADD upnp 0x10urn:schemas-upnporg:device:InternetGatewayDevice:1 0x10uu
 * id:6859dede-8574-59ab-9332-123456789012::urn:schemas-upnporg:device:InternetGate
 * wayDevice:1
*/
s32 wl_sd_handle_sd_add_svc(
	struct net_device *dev,
	u8 * buf,
	int len)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	s32 bssidx = 0;
	wl_sd_qr_t *sdreq;
	u8 proto = 0;
	u16 version = 0;
	s32 ret = 0;
	u8 *resp = NULL;
	u8 *query = NULL;
	u32 tot_len = len + sizeof(wl_sd_qr_t);

	if (!buf || !len)
		return -EINVAL;

	WL_SD(("%s Len: %d\n", buf, len));
	if (tot_len > WLC_IOCTL_MAXLEN) {
		WL_ERR(("Query-Resp length > %d not supported \n", WLC_IOCTL_MAXLEN));
		return -ENOMEM;
	}

	sdreq = (wl_sd_qr_t *)MALLOCZ(cfg->osh, tot_len);
	if (!sdreq) {
		WL_ERR(("malloc failed\n"));
		return -ENOMEM;
	}

	if ((ret = wl_cfg80211_DsdOffloadParseProto(buf, &proto)) < 0) {
		WL_ERR(("Unknown Proto \n"));
		goto exit;
	}

	sdreq->protocol = proto;
	buf += ret;

	if (*buf == '\0') {
		WL_ERR(("No Query Resp pair present \n"));
		ret = -EINVAL;
		goto exit;
	}

	buf++; /* Skip the space */
	len = strlen(buf);
	query = strsep((char **)&buf, " ");
	if (!query || !buf) {
		WL_ERR(("No Query RESP Present\n"));
		ret = -EINVAL;
		goto exit;
	}
	resp = buf;

	if (sdreq->protocol == SVC_RPOTYPE_UPNP) {
		/* Extract UPNP version */
		version = simple_strtoul(query, NULL, 16);
		query = query + UPNP_QUERY_VER_OFFSET;
		resp = resp + UPNP_QUERY_VER_OFFSET;
		query[0] = version;
		resp[0] = version;
		WL_SD(("Upnp version: 0x%x \n", version));
	}

	sdreq->query_len = strlen(query);
	sdreq->response_len = strlen(buf);
	WL_SD(("query:%s len:%u \n", query, sdreq->query_len));
	WL_SD(("resp:%s len:%u \n", buf, sdreq->response_len));

	memcpy(sdreq->qrbuf, query, sdreq->query_len);
	memcpy((sdreq->qrbuf + sdreq->query_len), resp, sdreq->response_len);

	/* Enable discovery */
	if ((ret = wl_cfgp2p_enable_discovery(cfg, dev, NULL, 0)) < 0) {
		WL_ERR(("cfgp2p_enable discovery failed"));
		goto exit;
	}

	if ((ret = wldev_iovar_setbuf_bsscfg(dev, "p2po_addsvc", (void*)sdreq,
		tot_len, cfg->ioctl_buf, WLC_IOCTL_MAXLEN,
		bssidx, &cfg->ioctl_buf_sync)) < 0) {
		WL_ERR(("FW Failed in doing p2po_addsvc. RET:%d \n", ret));
		goto exit;
	}

	cfg->sdo->sd_state |= WL_SD_ADV_SVC;

exit:
	MFREE(cfg->osh, sdreq, tot_len);
	return ret;
}

s32 wl_sd_handle_sd_del_svc(
	struct net_device *dev,
	u8 * buf,
	int len)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	s32 bssidx = 0;
	wl_sd_qr_t *sdreq;
	u8 proto = 0;
	s32 ret = 0;
	u32 tot_len = len + sizeof(wl_sd_qr_t);
	u16 version = 0;

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("find_idx failed\n"));
		return -EINVAL;
	}

	sdreq = (wl_sd_qr_t *)MALLOCZ(cfg->osh, tot_len);
	if (!sdreq) {
		WL_ERR(("malloc failed\n"));
		ret = -ENOMEM;
		goto exit;
	}

	/* Check for the least arg length expected */
	if (buf && len >= strlen("all")) {
		WL_DBG(("%s Len: %d\n", buf, len));
		if ((ret = wl_cfg80211_DsdOffloadParseProto(buf, &proto)) < 0) {
			WL_ERR(("Unknown Proto \n"));
			goto exit;
		}
		sdreq->protocol = proto;
		buf += ret;

		if (*buf == ' ') {
			/* Query present */
			buf++; /* Skip the space */
			/* UPNP version needs to put as binary val */
			if (sdreq->protocol == SVC_RPOTYPE_UPNP) {
				/* Extract UPNP version */
				version = simple_strtoul(buf, NULL, 16);
				buf = buf + UPNP_QUERY_VER_OFFSET;
				buf[0] = version;
				WL_SD(("Upnp version: 0x%x \n", version));
			}
			memcpy(sdreq->qrbuf, buf, strlen(buf));
			sdreq->query_len = strlen(buf);
			WL_SD(("Query to be deleted:%s len:%d\n", buf, sdreq->query_len));
		}
	} else {
		/* ALL */
		proto = 0;
	}

	sdreq->protocol = proto;
	WL_SD(("Proto: %d \n", proto));

	if ((ret = wldev_iovar_setbuf_bsscfg(dev, "p2po_delsvc", (void*)sdreq,
		tot_len, cfg->ioctl_buf, WLC_IOCTL_MAXLEN,
		bssidx, &cfg->ioctl_buf_sync)) < 0) {
		WL_ERR(("FW Failed in doing sd_delsvc. ret=%d \n", ret));
		goto exit;
	}

	cfg->sdo->sd_state &= ~WL_SD_ADV_SVC;

exit:
	if (sdreq) {
		MFREE(cfg->osh, sdreq, tot_len);
	}

	return ret;
}

s32 wl_sd_handle_sd_stop_discovery(
	struct net_device *dev,
	u8 * buf,
	int len)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	s32 bssidx = wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE);
	int ret = 0;
	int sdo_pause = 0;

	if ((ret = wldev_iovar_setbuf_bsscfg(dev, "p2po_stop", (void*)&sdo_pause,
		sizeof(sdo_pause), cfg->ioctl_buf, WLC_IOCTL_SMLEN,
		bssidx, &cfg->ioctl_buf_sync)) < 0) {
		WL_ERR(("p2po_stop Failed :%d\n", ret));
		return -1;
	}

	/* clear the states */
	cfg->sdo->dd_state = WL_DD_STATE_IDLE;
	wl_clr_p2p_status(cfg, DISC_IN_PROGRESS);

	bzero(&cfg->sdo->sd_listen, sizeof(wl_sd_listen_t));

	/* Remove ESCAN from waking up the host if ofind/olisten is enabled */
	wl_add_remove_eventmsg(dev, WLC_E_ESCAN_RESULT, true);

	return ret;
}

s32 wl_sd_handle_sd_find(
	struct net_device *dev,
	u8 * buf,
	int len)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	s32 bssidx = wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE);
	int ret = 0;
	s32 disc_bssidx = wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE);
	vndr_ie_setbuf_t *ie_setbuf;
	vndr_ie_t *vndrie;
	vndr_ie_buf_t *vndriebuf;
	int tot_len = 0;
	uint channel = 0;

	u8 p2pie_buf[] = {
				0x09, 0x02, 0x02, 0x00, 0x27, 0x0c, 0x06, 0x05, 0x00,
				0x55, 0x53, 0x04, 0x51, 0x0b, 0x11, 0x05, 0x00, 0x55,
				0x53, 0x04, 0x51, 0x0b
			  };

	/* Enable discovery */
	if ((ret = wl_cfgp2p_enable_discovery(cfg, dev, NULL, 0)) < 0) {
		WL_ERR(("cfgp2p_enable discovery failed"));
		return -1;
	}

	if (buf && strncmp(buf, "chan=", strlen("chan=")) == 0) {
		buf += strlen("chan=");
		channel = simple_strtol(buf, NULL, 10);
		WL_SD(("listen_chan to be set:%d\n", channel));
		if ((ret = wldev_iovar_setbuf_bsscfg(dev, "p2po_listen_channel", (void*)&channel,
			sizeof(channel), cfg->ioctl_buf, WLC_IOCTL_SMLEN,
			bssidx, &cfg->ioctl_buf_sync)) < 0) {
				WL_ERR(("p2po_listen_channel Failed :%d\n", ret));
				return -1;
		}
	}

	tot_len = sizeof(vndr_ie_setbuf_t) + sizeof(p2pie_buf);
	ie_setbuf = (vndr_ie_setbuf_t *)MALLOCZ(cfg->osh, tot_len);
	if (!ie_setbuf) {
		WL_ERR(("IE memory alloc failed\n"));
		return -ENOMEM;
	}

	/* Apply the p2p_ie for p2po_find */
	strlcpy(ie_setbuf->cmd, "add", sizeof(ie_setbuf->cmd));

	vndriebuf = &ie_setbuf->vndr_ie_buffer;
	vndriebuf->iecount = htod32(1);
	vndriebuf->vndr_ie_list[0].pktflag =  htod32(16);

	vndrie =  &vndriebuf->vndr_ie_list[0].vndr_ie_data;

	vndrie->id = (uchar) DOT11_MNG_PROPR_ID;
	vndrie->len = sizeof(p2pie_buf);
	memcpy(vndrie->oui, WFA_OUI, WFA_OUI_LEN);
	memcpy(vndrie->data, p2pie_buf, sizeof(p2pie_buf));

	/* Remove ESCAN from waking up the host if SDO is enabled */
	wl_add_remove_eventmsg(dev, WLC_E_ESCAN_RESULT, false);

	if (wldev_iovar_setbuf_bsscfg(dev, "ie", (void*)ie_setbuf,
		tot_len, cfg->ioctl_buf, WLC_IOCTL_SMLEN,
		disc_bssidx, &cfg->ioctl_buf_sync) < 0) {
		WL_ERR(("p2p add_ie failed \n"));
		ret = -EINVAL;
		goto exit;
	} else
		WL_SD(("p2p add_ie applied successfully len:%d \n", tot_len));

	if ((ret = wldev_iovar_setbuf_bsscfg(dev, "p2po_find", NULL, 0,
		cfg->ioctl_buf, WLC_IOCTL_SMLEN, bssidx, &cfg->ioctl_buf_sync)) < 0) {
		WL_ERR(("p2po_find Failed :%d\n", ret));
		ret = -1;
		goto exit;
	}

	/* set the states */
	cfg->sdo->dd_state = WL_DD_STATE_SEARCH;
	wl_set_p2p_status(cfg, DISC_IN_PROGRESS);

exit:
	if (ie_setbuf) {
		MFREE(cfg->osh, ie_setbuf, tot_len);
	}

	/* Incase of failure enable back the ESCAN event */
	if (ret)
		wl_add_remove_eventmsg(dev, WLC_E_ESCAN_RESULT, true);

	return ret;
}

s32 wl_sd_handle_sd_listen(
	struct net_device *dev,
	u8 *buf,
	int len)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	s32 bssidx = wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE);
	wl_sd_listen_t sd_listen;
	int ret = 0;
	u8 * ptr = NULL;
	uint channel = 0;

	/* Just in case if it is not enabled */
	if ((ret = wl_cfgp2p_enable_discovery(cfg, dev, NULL, 0)) < 0) {
		WL_ERR(("cfgp2p_enable discovery failed"));
		return -1;
	}
	bzero(&sd_listen, sizeof(wl_sd_listen_t));
	if (len) {
		ptr = strsep((char **)&buf, " ");
		if (ptr == NULL) {
			/* period and duration given wrongly */
			WL_ERR(("Arguments in wrong format \n"));
			return -EINVAL;
		}
		else if (strncmp(ptr, "chan=", strlen("chan=")) == 0) {
			sd_listen.interval = 65535;
			sd_listen.period = 65535;
			ptr += strlen("chan=");
			channel = simple_strtol(ptr, NULL, 10);
		}
		else {
			sd_listen.period = simple_strtol(ptr, NULL, 10);
			ptr = strsep((char **)&buf, " ");
			if (ptr == NULL) {
				WL_ERR(("Arguments in wrong format \n"));
				return -EINVAL;
			}
			sd_listen.interval = simple_strtol(ptr, NULL, 10);
			if (buf && strncmp(buf, "chan=", strlen("chan=")) == 0) {
				buf += strlen("chan=");
				channel = simple_strtol(buf, NULL, 10);
			}
		}
		WL_SD(("listen_period:%d, listen_interval:%d and listen_channel:%d\n",
			sd_listen.period, sd_listen.interval, channel));
	}
	if ((ret = wldev_iovar_setbuf_bsscfg(dev, "p2po_listen_channel", (void*)&channel,
		sizeof(channel), cfg->ioctl_buf, WLC_IOCTL_SMLEN,
		bssidx, &cfg->ioctl_buf_sync)) < 0) {
			WL_ERR(("p2po_listen_channel Failed :%d\n", ret));
			return -1;
	}

	WL_SD(("p2po_listen period:%d  interval:%d \n",
		sd_listen.period, sd_listen.interval));
	if ((ret = wldev_iovar_setbuf_bsscfg(dev, "p2po_listen", (void*)&sd_listen,
		sizeof(wl_sd_listen_t), cfg->ioctl_buf, WLC_IOCTL_SMLEN,
		bssidx, &cfg->ioctl_buf_sync)) < 0) {
		WL_ERR(("p2po_listen Failed :%d\n", ret));
		return -1;
	}

	/* Remove ESCAN from waking up the host if ofind/olisten is enabled */
	wl_add_remove_eventmsg(dev, WLC_E_ESCAN_RESULT, false);

	/* Store the extended listen values for use in sdo_resume */
	cfg->sdo->sd_listen.interval = sd_listen.interval;
	cfg->sdo->sd_listen.period = sd_listen.period;

	/* set the states */
	cfg->sdo->dd_state = WL_DD_STATE_LISTEN;
	wl_set_p2p_status(cfg, DISC_IN_PROGRESS);

	return 0;
}

s32 wl_cfg80211_sd_offload(struct net_device *dev, char *cmd, char* buf, int len)
{
	int ret = 0;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	WL_SD(("Entry cmd:%s arg_len:%d \n", cmd, len));

	if (!cfg->sdo) {
		WL_SD(("Initializing SDO \n"));
		if ((ret = wl_cfg80211_sdo_init(cfg)) < 0)
			goto exit;
	}

	if (strncmp(cmd, "P2P_SD_REQ", strlen("P2P_SD_REQ")) == 0) {
		ret = wl_sd_handle_sd_req(dev, buf, len);
	} else if (strncmp(cmd, "P2P_SD_CANCEL_REQ", strlen("P2P_SD_CANCEL_REQ")) == 0) {
		ret = wl_sd_handle_sd_cancel_req(dev, buf);
	} else if (strncmp(cmd, "P2P_SD_SVC_ADD", strlen("P2P_SD_SVC_ADD")) == 0) {
		ret = wl_sd_handle_sd_add_svc(dev, buf, len);
	} else if (strncmp(cmd, "P2P_SD_SVC_DEL", strlen("P2P_SD_SVC_DEL")) == 0) {
		ret = wl_sd_handle_sd_del_svc(dev, buf, len);
	} else if (strncmp(cmd, "P2P_SD_FIND", strlen("P2P_SD_FIND")) == 0) {
		ret = wl_sd_handle_sd_find(dev, buf, len);
	} else if (strncmp(cmd, "P2P_SD_LISTEN", strlen("P2P_SD_LISTEN")) == 0) {
		ret = wl_sd_handle_sd_listen(dev, buf, len);
	} else if (strncmp(cmd, "P2P_SD_STOP", strlen("P2P_STOP")) == 0) {
		ret = wl_sd_handle_sd_stop_discovery(dev, buf, len);
	} else {
		WL_ERR(("Request for Unsupported CMD:%s \n", buf));
		ret = -EINVAL;
	}

exit:
	return ret;
}
#endif /* WL_SDO */

s32 wl_cfg80211_set_wps_p2p_ie(struct net_device *ndev, char *buf, int len,
	enum wl_management_type type)
{
	struct bcm_cfg80211 *cfg;
	s32 ret = 0;
	s32 bssidx = 0;
	s32 pktflag = 0;
	struct wireless_dev *wdev = ndev->ieee80211_ptr;

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
				p2p_on(cfg) = true;
				ret = wl_cfgp2p_enable_discovery(cfg, ndev, NULL, 0);
				if (unlikely(ret)) {
					WL_ERR(("Enable discovery failed \n"));
					goto exit;
				}
			}
			ndev = wl_to_p2p_bss_ndev(cfg, P2PAPI_BSSCFG_PRIMARY);
			if (!cfg->p2p_wdev) {
				WL_ERR(("p2p_wdev not present\n"));
				goto exit;
			}
			wdev = cfg->p2p_wdev;
			WL_DBG(("Apply IEs for P2P Discovery Iface wdev:%p\n", wdev));
			bssidx = wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE);
		}
	} else {
		/* Virtual AP/ P2P Group Interface */
		WL_DBG(("Apply IEs for iface:%s\n", ndev->name));
		bssidx = wl_get_bssidx_by_wdev(cfg, ndev->ieee80211_ptr);
	}

	if (wdev != NULL) {
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
				wdev_to_cfgdev(wdev), bssidx, pktflag, buf, len);
		}
	}
exit:
	return ret;
}

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

struct bcm_cfg80211 *wl_cfg80211_get_bcmcfg(void)
{
	return g_bcmcfg;
}

void wl_cfg80211_set_bcmcfg(struct bcm_cfg80211 *cfg)
{
	g_bcmcfg = cfg;
}

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
	u8 ioctl_buf[WLC_IOCTL_SMLEN];

	if (wldev_iovar_getbuf_bsscfg(bcmcfg_to_prmry_ndev(cfg),
			"cur_etheraddr", NULL, 0, ioctl_buf, sizeof(ioctl_buf),
			0, NULL) == BCME_OK) {
		memcpy(mac->octet, ioctl_buf, ETHER_ADDR_LEN);
	} else {
		bzero(mac->octet, ETHER_ADDR_LEN);
	}
}

int wl_cfg80211_do_driver_init(struct net_device *net)
{
	struct bcm_cfg80211 *cfg = *(struct bcm_cfg80211 **)netdev_priv(net);

	if (!cfg || !cfg->wdev)
		return -EINVAL;

#if defined(BCMDONGLEHOST)
	if (dhd_do_driver_init(cfg->wdev->netdev) < 0)
		return -1;
#endif /* BCMDONGLEHOST */

	return 0;
}

void wl_cfg80211_enable_trace(u32 level)
{
	wl_dbg_level = level;
	WL_MSG("wlan", "wl_dbg_level = 0x%x\n", wl_dbg_level);
}

#if defined(WL_SUPPORT_BACKPORTED_KPATCHES) || (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0))
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

#ifdef WL_HOST_BAND_MGMT
s32
wl_cfg80211_set_band(struct net_device *ndev, int band)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	int ret = 0;
	char ioctl_buf[50];

	if ((band < WLC_BAND_AUTO) || (band > WLC_BAND_2G)) {
		WL_ERR(("Invalid band\n"));
		return -EINVAL;
	}

	if ((ret = wldev_iovar_setbuf(ndev, "roam_band", &band,
		sizeof(int), ioctl_buf, sizeof(ioctl_buf), NULL)) < 0) {
		WL_ERR(("seting roam_band failed code=%d\n", ret));
		return ret;
	}

	WL_DBG(("Setting band to %d\n", band));
	cfg->curr_band = band;

	return 0;
}
#endif /* WL_HOST_BAND_MGMT */

s32
wl_cfg80211_set_if_band(struct net_device *ndev, int band)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	int ret = BCME_OK, wait_cnt;
	char ioctl_buf[32];

	if ((band < WLC_BAND_AUTO) || (band > WLC_BAND_2G)) {
		WL_ERR(("Invalid band\n"));
		return -EINVAL;
	}

	if (cfg->ncho_band == band) {
		WL_ERR(("Same to Current band %d\n", cfg->ncho_band));
		return ret;
	}

	if (wl_get_drv_status(cfg, CONNECTED, ndev)) {
#ifdef BCMDONGLEHOST
		dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
		BCM_REFERENCE(dhdp);
		DHD_STATLOG_CTRL(dhdp, ST(DISASSOC_INT_START),
			dhd_net2idx(dhdp->info, ndev), 0);
#endif /* BCMDONGLEHOST */
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
			while (wl_get_drv_status(cfg, CONNECTED, ndev) && wait_cnt) {
				WL_DBG(("Wait until disconnected. wait_cnt: %d\n", wait_cnt));
				wait_cnt--;
				OSL_SLEEP(50);
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

	if (ret == BCME_OK) {
		cfg->ncho_band = band;
	}
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

/*
 * This is to support existing btcoex implementation
 * btcoex clean up may help removing this function
 */
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
#ifdef BCMDONGLEHOST
	u8 *curbssid = NULL;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);

	if (!cfg) {
		/* When interface is created using wl
		 * ndev->ieee80211_ptr will be NULL.
		 */
		return NULL;
	}
	curbssid = wl_read_prof(cfg, dev, WL_PROF_BSSID);

	if (memcmp(curbssid, &e->addr, ETHER_ADDR_LEN) == 0) {
		return true;
	}
#endif /* BCMDONGLEHOST */
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
	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	for_each_ndev(cfg, iter, next) {
		GCC_DIAGNOSTIC_POP();
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

#if defined(BCMDONGLEHOST) && defined(OEM_ANDROID)
	DHD_PM_WAKE_UNLOCK(cfg->pub);
#endif /* BCMDONGLEHOST && OEM_ANDROID */

#if defined(OEM_ANDROID) && defined(DHCP_SCAN_SUPPRESS)
	if (cfg->scan_suppressed) {
		/* There is pending scan_suppress. Clean it */
		WL_ERR(("Clean up from timer after %d msec\n", WL_SCAN_SUPPRESS_TIMEOUT));
		wl_cfg80211_scan_suppress(bcmcfg_to_prmry_ndev(cfg), 0);
	}
#endif /* DHCP_SCAN_SUPPRESS */

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
	WL_DBG(("Action Category: %d\n", category));
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
	WL_DBG(("Public Action : %d\n", *ret_action));
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

static bool
wl_cfg80211_filter_vndr_ext_id(const vndr_ie_t *vndrie)
{
	if (vndrie->oui[0] == FILS_EXTID_MNG_HLP_CONTAINER_ID) {
		/* Skip adding fils HLP IE, its already done using
		 * "WL_FILS_CMD_ADD_HLP_IE" subcmd.
		 */
		WL_DBG(("%s:SKIP ADDING FILS HLP EXTN ID\n", __func__));
		return true;
	}
	return false;
}

static s32
wl_cfg80211_parse_vndr_ies(const u8 *parse, u32 len,
    struct parsed_vndr_ies *vndr_ies)
{
	s32 err = BCME_OK;
	const vndr_ie_t *vndrie;
	const bcm_tlv_t *ie;
	struct parsed_vndr_ie_info *parsed_info;
	u32 count = 0;
	u32 remained_len;

	remained_len = len;
	bzero(vndr_ies, sizeof(*vndr_ies));

	ie = (const bcm_tlv_t *) parse;
	if (!bcm_valid_tlv(ie, remained_len))
		ie = NULL;
	while (ie) {
		if (count >= MAX_VNDR_IE_NUMBER)
			break;
		if (ie->id == DOT11_MNG_VS_ID || (ie->id == DOT11_MNG_ID_EXT_ID)) {
			vndrie = (const vndr_ie_t *) ie;
			if (ie->id == DOT11_MNG_ID_EXT_ID) {
				/* len should be bigger than sizeof ID extn field at least */
				if (vndrie->len < MIN_VENDOR_EXTN_IE_LEN) {
					WL_ERR(("%s: invalid vndr extn ie."
						" length %d\n",
						__FUNCTION__, vndrie->len));
					goto end;
				}
				if (wl_cfg80211_filter_vndr_ext_id(vndrie)) {
					goto end;
				}
			} else {
				/* len should be bigger than OUI length +
				 * one data length at least
				 */
				if (vndrie->len < (VNDR_IE_MIN_LEN + 1)) {
					WL_ERR(("wl_cfg80211_parse_vndr_ies:"
						" invalid vndr ie. length is too small %d\n",
						vndrie->len));
					goto end;
				}

				/* if wpa or wme ie, do not add ie */
				if (!bcmp(vndrie->oui, (u8*)WPA_OUI, WPA_OUI_LEN) &&
						((vndrie->data[0] == WPA_OUI_TYPE) ||
						(vndrie->data[0] == WME_OUI_TYPE))) {
					CFGP2P_DBG(("SKIP WPA/WME oui \n"));
					goto end;
				}
#if defined(WL_MBO) || defined(WL_OCE)
				if ((!memcmp(vndrie->oui, (u8 *)WFA_OUI, WFA_OUI_LEN)) &&
					(vndrie->data[0] == WFA_OUI_TYPE_MBO_OCE)) {
					WL_DBG(("SKIP ID : %d Len: %d OUI:"MACOUIDBG
						" TYPE:%0x\n", vndrie->id, vndrie->len,
						MACOUI2STRDBG(vndrie->oui), vndrie->data[0]));
					goto end;
				}
#endif /* WL_MBO || WL_OCE */
			}

			parsed_info = &vndr_ies->ie_info[count++];

			/* save vndr ie information */
			parsed_info->ie_ptr = (const char *)vndrie;
			parsed_info->ie_len = (vndrie->len + TLV_HDR_LEN);
			memcpy(&parsed_info->vndrie, vndrie, sizeof(vndr_ie_t));
			vndr_ies->count = count;
			if (ie->id == DOT11_MNG_ID_EXT_ID) {
				WL_DBG(("** Vendor Extension ie id: 0x%02x, len:%d\n",
					ie->id, vndrie->len));
			} else {
				WL_DBG(("** OUI "MACOUIDBG", type 0x%02x len:%d\n",
					MACOUI2STRDBG(parsed_info->vndrie.oui),
					parsed_info->vndrie.data[0], vndrie->len));
			}
		}
end:
		ie = bcm_next_tlv(ie, &remained_len);
	}
	return err;
}

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
	unsigned long flags;

	WL_CFG_VNDR_OUI_SYNC_LOCK(&cfg->vndr_oui_sync, flags);
	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	list_for_each_entry(oui_entry, &cfg->vndr_oui_list, list) {
		GCC_DIAGNOSTIC_POP();
		if (!memcmp(oui_entry->oui, vndr_info->vndrie.oui, DOT11_OUI_LEN)) {
			WL_CFG_VNDR_OUI_SYNC_UNLOCK(&cfg->vndr_oui_sync, flags);
			return TRUE;
		}
	}
	WL_CFG_VNDR_OUI_SYNC_UNLOCK(&cfg->vndr_oui_sync, flags);
	return FALSE;
}

static bool
wl_vndr_ies_add_vendor_oui_list(struct bcm_cfg80211 *cfg,
	struct parsed_vndr_ie_info *vndr_info)
{
	wl_vndr_oui_entry_t *oui_entry = NULL;
	unsigned long flags;

	oui_entry = kmalloc(sizeof(*oui_entry), GFP_KERNEL);
	if (oui_entry == NULL) {
		WL_ERR(("alloc failed\n"));
		return FALSE;
	}

	memcpy(oui_entry->oui, vndr_info->vndrie.oui, DOT11_OUI_LEN);

	INIT_LIST_HEAD(&oui_entry->list);
	WL_CFG_VNDR_OUI_SYNC_LOCK(&cfg->vndr_oui_sync, flags);
	list_add_tail(&oui_entry->list, &cfg->vndr_oui_list);
	WL_CFG_VNDR_OUI_SYNC_UNLOCK(&cfg->vndr_oui_sync, flags);

	return TRUE;
}

static void
wl_vndr_ies_clear_vendor_oui_list(struct bcm_cfg80211 *cfg)
{
	wl_vndr_oui_entry_t *oui_entry = NULL;
	unsigned long flags;

	WL_CFG_VNDR_OUI_SYNC_LOCK(&cfg->vndr_oui_sync, flags);
	while (!list_empty(&cfg->vndr_oui_list)) {
		GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
		oui_entry = list_entry(cfg->vndr_oui_list.next, wl_vndr_oui_entry_t, list);
		GCC_DIAGNOSTIC_POP();
		if (oui_entry) {
			list_del(&oui_entry->list);
			kfree(oui_entry);
		}
	}
	WL_CFG_VNDR_OUI_SYNC_UNLOCK(&cfg->vndr_oui_sync, flags);
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
	unsigned long flags;

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
		WL_CFG_VNDR_OUI_SYNC_LOCK(&cfg->vndr_oui_sync, flags);
		GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
		list_for_each_entry(oui_entry, &cfg->vndr_oui_list, list) {
			GCC_DIAGNOSTIC_POP();
			if (remained_buf_len < VNDR_OUI_STR_LEN) {
				WL_CFG_VNDR_OUI_SYNC_UNLOCK(&cfg->vndr_oui_sync, flags);
				return BCME_ERROR;
			}
			pos += snprintf(pos, VNDR_OUI_STR_LEN, "%02X-%02X-%02X ",
				oui_entry->oui[0], oui_entry->oui[1], oui_entry->oui[2]);
			remained_buf_len -= VNDR_OUI_STR_LEN;
		}
		WL_CFG_VNDR_OUI_SYNC_UNLOCK(&cfg->vndr_oui_sync, flags);
	}

	return vndr_oui_num;
}

#ifdef WL_ANALYTICS
static bool
wl_vndr_ies_find_vendor_oui(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const char *vndr_oui)
{
	int i;
	int vndr_oui_num = 0;

	struct wl_connect_info *conn_info = wl_to_conn(cfg);
	wl_vndr_oui_entry_t *oui_entry = NULL;
	struct parsed_vndr_ie_info *vndr_info;
	struct parsed_vndr_ies vndr_ies;

	unsigned long flags;
	bool found = FALSE;

	if (!conn_info->resp_ie_len) {
		return FALSE;
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

	if (vndr_oui && vndr_oui_num) {
		WL_CFG_VNDR_OUI_SYNC_LOCK(&cfg->vndr_oui_sync, flags);
		GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
		list_for_each_entry(oui_entry, &cfg->vndr_oui_list, list) {
			GCC_DIAGNOSTIC_POP();
			if (!memcmp(vndr_oui, oui_entry->oui, DOT11_OUI_LEN)) {
				found = TRUE;
				break;
			}
		}
		WL_CFG_VNDR_OUI_SYNC_UNLOCK(&cfg->vndr_oui_sync, flags);
	}

	return found;
}
#endif /* WL_ANALYTICS */

void
wl_cfg80211_clear_p2p_disc_ies(struct bcm_cfg80211 *cfg)
{
#ifdef WL_CFG80211_P2P_DEV_IF
	if (cfg->p2p_wdev) {
		/* clear IEs for dedicated p2p interface */
		WL_DBG_MEM(("Clear IEs for P2P Discovery Iface\n"));
		wl_cfg80211_clear_per_bss_ies(cfg, cfg->p2p_wdev);
	}
#else
	/* Legacy P2P used to store it in primary dev cache */
	s32 index;
	struct net_device *ndev;
	s32 bssidx;
	s32 ret;
	s32 vndrie_flag[] = {VNDR_IE_BEACON_FLAG, VNDR_IE_PRBRSP_FLAG,
		VNDR_IE_ASSOCRSP_FLAG, VNDR_IE_PRBREQ_FLAG, VNDR_IE_ASSOCREQ_FLAG};

	WL_DBG(("Clear IEs for P2P Discovery Iface \n"));
	/* certain vendors uses p2p0 interface in addition to
	 * the dedicated p2p interface supported by the linux
	 * kernel.
	 */
	ndev = wl_to_p2p_bss_ndev(cfg, P2PAPI_BSSCFG_PRIMARY);
	bssidx = wl_to_p2p_bss_bssidx(cfg, P2PAPI_BSSCFG_DEVICE);
	if (bssidx == WL_INVALID) {
		WL_DBG(("No discovery I/F available. Do nothing.\n"));
		return;
	}

	for (index = 0; index < ARRAYSIZE(vndrie_flag); index++) {
		if ((ret = wl_cfg80211_set_mgmt_vndr_ies(cfg, ndev_to_cfgdev(ndev),
			bssidx, vndrie_flag[index], NULL, 0)) < 0) {
			if (ret != BCME_NOTFOUND) {
				WL_ERR(("vndr_ies clear failed (%d). Ignoring.. \n", ret));
			}
		}
	}
#endif /* WL_CFG80211_P2P_DEV_IF */
}

s32
wl_cfg80211_clear_per_bss_ies(struct bcm_cfg80211 *cfg, struct wireless_dev *wdev)
{
	s32 index;
	s32 ret;
	struct net_info *netinfo;
	s32 vndrie_flag[] = {VNDR_IE_BEACON_FLAG, VNDR_IE_PRBRSP_FLAG,
		VNDR_IE_ASSOCRSP_FLAG, VNDR_IE_PRBREQ_FLAG, VNDR_IE_ASSOCREQ_FLAG};

	netinfo = wl_get_netinfo_by_wdev(cfg, wdev);
	if (!netinfo || !netinfo->wdev) {
		WL_ERR(("netinfo or netinfo->wdev is NULL\n"));
		return -1;
	}

	WL_DBG(("clear management vendor IEs for bssidx:%d \n", netinfo->bssidx));
	/* Clear the IEs set in the firmware so that host is in sync with firmware */
	for (index = 0; index < ARRAYSIZE(vndrie_flag); index++) {
		if ((ret = wl_cfg80211_set_mgmt_vndr_ies(cfg, wdev_to_cfgdev(netinfo->wdev),
			netinfo->bssidx, vndrie_flag[index], NULL, 0)) < 0)
			if (ret != BCME_NOTFOUND) {
				WL_ERR(("vndr_ies clear failed. Ignoring.. \n"));
			}
	}

	return 0;
}

s32
wl_cfg80211_clear_mgmt_vndr_ies(struct bcm_cfg80211 *cfg)
{
	struct net_info *iter, *next;

	WL_DBG(("clear management vendor IEs \n"));
	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	for_each_ndev(cfg, iter, next) {
		GCC_DIAGNOSTIC_POP();
		wl_cfg80211_clear_per_bss_ies(cfg, iter->wdev);
	}
	return 0;
}

static void
wl_print_fw_ie_data(struct bcm_cfg80211 *cfg, struct net_device *ndev, s32 bssidx)
{
	vndr_ie_buf_t *ies;
	s32 ret;

	ret  = wldev_iovar_getbuf_bsscfg(ndev, "vndr_ie", NULL,
		0, cfg->ioctl_buf, WLC_IOCTL_MAXLEN,
		bssidx, &cfg->ioctl_buf_sync);
	if (ret == BCME_OK) {
		ies = (vndr_ie_buf_t *)cfg->ioctl_buf;
		WL_INFORM_MEM(("FW IE count:%d ", ies->iecount));
#ifdef GET_FW_IE_DATA
		if (wl_dbg_level & WL_DBG_DBG) {
			int i = 0;
			/* If debug enabled, print each IE */
			for (i = 0; i < ies->iecount; i++) {
				vndr_ie_info_t *info = &ies->vndr_ie_list[i];
				WL_DBG_MEM(("pktflag:0x%x\n", info->pktflag));
					prhex("IE:", (u8 *)&info->vndr_ie_data,
						info->vndr_ie_data.len + TLV_HDR_LEN);
			}
		}
#endif /* GET_FW_IE_DATA */
	} else {
		WL_ERR(("IE retrieval failed! ret:%d\n", ret));
	}
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
	struct wireless_dev *wdev;

	if (!cfgdev) {
		WL_ERR(("cfgdev is NULL\n"));
		return -EINVAL;
	}

	ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);
	wdev = cfgdev_to_wdev(cfgdev);

	if (bssidx > WL_MAX_IFS) {
		WL_ERR(("bssidx > supported concurrent Ifaces \n"));
		return -EINVAL;
	}

	netinfo = wl_get_netinfo_by_wdev(cfg, wdev);
	if (!netinfo) {
		WL_ERR(("net_info ptr is NULL \n"));
		return -EINVAL;
	}

	/* Clear the global buffer */
	bzero(g_mgmt_ie_buf, sizeof(g_mgmt_ie_buf));
	curr_ie_buf = g_mgmt_ie_buf;
	ies = &netinfo->bss.ies;

	WL_DBG_MEM(("Enter. pktflag:0x%x bssidx:%x vnd_ie_len:%d\n",
		pktflag, bssidx, vndr_ie_len));

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
		case VNDR_IE_DISASSOC_FLAG :
			mgmt_ie_buf = ies->disassoc_ie;
			mgmt_ie_len = &ies->disassoc_ie_len;
			mgmt_ie_buf_len = sizeof(ies->disassoc_ie);
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

			WL_DBG(("Incoming IEs len:%d\n", vndr_ie_len));
			if ((ret = wl_cfg80211_parse_vndr_ies((const u8 *)vndr_ie,
			                                      vndr_ie_len, &new_vndr_ies)) < 0) {
				WL_ERR(("parse vndr ie failed \n"));
				goto exit;
			}

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
				WL_DBG_MEM(("No change in cached IEs for pkt:%d\n", pktflag));
				goto exit;
			}

			/* parse old vndr_ie */
			WL_DBG(("Cached IEs len:%d\n", *mgmt_ie_len));
			if ((ret = wl_cfg80211_parse_vndr_ies(mgmt_ie_buf, *mgmt_ie_len,
				&old_vndr_ies)) < 0) {
				WL_ERR(("parse vndr ie failed \n"));
				goto exit;
			}
			/* make a command to delete old ie */
			for (i = 0; i < old_vndr_ies.count; i++) {
				struct parsed_vndr_ie_info *vndrie_info =
				&old_vndr_ies.ie_info[i];

				if (vndrie_info->vndrie.id == DOT11_MNG_ID_EXT_ID) {
					WL_DBG_MEM(("DEL VENDOR EXTN ID :%d TYPE:%d Len:%d\n",
						vndrie_info->vndrie.id, vndrie_info->vndrie.oui[0],
						vndrie_info->vndrie.len));
				} else {
					WL_DBG_MEM(("DEL ID :%d Len:%d OUI:"MACOUIDBG" TYPE:%d\n",
						vndrie_info->vndrie.id, vndrie_info->vndrie.len,
						MACOUI2STRDBG(vndrie_info->vndrie.oui),
						vndrie_info->vndrie.data[0]));
				}

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
				if (vndrie_info->vndrie.id == DOT11_MNG_ID_EXT_ID) {
					WL_DBG_MEM(("ADD VENDOR EXTN ID :%d TYPE:%d Len:%d\n",
						vndrie_info->vndrie.id, vndrie_info->vndrie.oui[0],
						vndrie_info->vndrie.len));
				} else {
					WL_DBG_MEM(("ADD ID :%d Len:%d OUI:"MACOUIDBG" TYPE:%d\n",
						vndrie_info->vndrie.id, vndrie_info->vndrie.len,
						MACOUI2STRDBG(vndrie_info->vndrie.oui),
						vndrie_info->vndrie.data[0]));
				}

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
			if (ret) {
				WL_ERR(("vndr_ie set error :%d\n", ret));
				if (ret == BCME_NOTFOUND) {
					/* retrieve and print IE data for debug */
					wl_print_fw_ie_data(cfg, ndev, bssidx);
				}
			}
		}
	}
exit:

return ret;
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

	if (wdev) {
		wl_cfgp2p_del_p2p_disc_if(wdev, cfg);
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
	dhd_pub_t *dhd = cfg->pub;

	WL_DBG(("Enter\n"));
	if (data == NULL || cfg->p2p_net == dev) {
		WL_ERR(("data is NULL or wrong net device\n"));
		return -EINVAL;
	}

	if (!dhd->conf->rekey_offload) {
		WL_TRACE(("rekey_offload disabled\n"));
		return BCME_UNSUPPORTED;
	}

	memset(&bcol_keyinfo, 0, sizeof(bcol_keyinfo));
	bcol_keyinfo.enable = 1;
	bcol_keyinfo.ptk_len = 64;
	memcpy(&bcol_keyinfo.ptk[0], data->kck, RSN_KCK_LENGTH);
	memcpy(&bcol_keyinfo.ptk[RSN_KCK_LENGTH], data->kek, RSN_KEK_LENGTH);
	err = wldev_iovar_setbuf(dev, "bcol_gtk_rekey_ptk", &bcol_keyinfo,
		sizeof(bcol_keyinfo), cfg->ioctl_buf, WLC_IOCTL_SMLEN, &cfg->ioctl_buf_sync);
	if (!err) {
		goto exit;
	}

	bcopy(data->kck, keyinfo.KCK, RSN_KCK_LENGTH);
	bcopy(data->kek, keyinfo.KEK, RSN_KEK_LENGTH);
	bcopy(data->replay_ctr, keyinfo.ReplayCounter, RSN_REPLAY_LEN);
	if ((err = wldev_iovar_setbuf(dev, "gtk_key_info", &keyinfo, sizeof(keyinfo),
		cfg->ioctl_buf, WLC_IOCTL_SMLEN, &cfg->ioctl_buf_sync)) < 0) {
		return err;
	}

exit:
	prhex("kck", (const u8 *) (data->kck), RSN_KCK_LENGTH);
	prhex("kek", (const u8 *) (data->kek), RSN_KEK_LENGTH);
	prhex("replay_ctr", (const u8 *) (data->replay_ctr), RSN_REPLAY_LEN);
	WL_DBG(("Exit\n"));
	return err;
}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0) */
#endif /* GTK_OFFLOAD_SUPPORT */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0))
static int wl_cfg80211_set_pmk(struct wiphy *wiphy, struct net_device *dev,
	const struct cfg80211_pmk_conf *conf)
{
	int ret = 0;
	wsec_pmk_t pmk;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	struct wl_security *sec;
	s32 bssidx;

	pmk.key_len = conf->pmk_len;
	if (pmk.key_len > sizeof(pmk.key)) {
		ret = -EINVAL;
		return ret;
	}
	pmk.flags = 0;
	ret = memcpy_s(&pmk.key, sizeof(pmk.key), conf->pmk, conf->pmk_len);
	if (ret) {
		ret = -EINVAL;
		return ret;
	}

	if ((bssidx = wl_get_bssidx_by_wdev(cfg, dev->ieee80211_ptr)) < 0) {
		WL_ERR(("Find index failed\n"));
		ret = -EINVAL;
		return ret;
	}

	sec = wl_read_prof(cfg, dev, WL_PROF_SEC);
	if ((sec->wpa_auth == WLAN_AKM_SUITE_8021X) ||
		(sec->wpa_auth == WL_AKM_SUITE_SHA256_1X)) {
		ret = wldev_iovar_setbuf_bsscfg(dev, "okc_info_pmk", pmk.key, pmk.key_len,
			cfg->ioctl_buf, WLC_IOCTL_SMLEN, bssidx, &cfg->ioctl_buf_sync);
		if (ret) {
			/* could fail in case that 'okc' is not supported */
			WL_INFORM_MEM(("okc_info_pmk failed, err=%d (ignore)\n", ret));
		}
	}

	ret = wldev_ioctl_set(dev, WLC_SET_WSEC_PMK, &pmk, sizeof(pmk));
	if (ret) {
		WL_ERR(("wl_cfg80211_set_pmk error:%d", ret));
		ret = -EINVAL;
		return ret;
	} else {
		WL_DBG(("pmk added for mac:"MACDBG"\n", MAC2STRDBG(conf->aa)));
	}
	return 0;
}

static int wl_cfg80211_del_pmk(struct wiphy *wiphy, struct net_device *dev,
	const u8 *aa)
{
	int err = BCME_OK;
	struct cfg80211_pmksa pmksa;

	/* build up cfg80211_pmksa structure to use existing wl_cfg80211_update_pmksa API */
	bzero(&pmksa, sizeof(pmksa));
	pmksa.bssid = aa;

	err = wl_cfg80211_update_pmksa(wiphy, dev, &pmksa, FALSE);
	if (unlikely(err)) {
		WL_ERR(("wl_cfg80211_update_pmksa err:%d\n", err));
		err = -EINVAL;
	} else {
		WL_DBG(("pmk deleted for bssid:"MACDBG"\n", MAC2STRDBG(aa)));
	}

	return err;
}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0) */

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

struct net_device*
wl_get_netdev_by_name(struct bcm_cfg80211 *cfg, char *ifname)
{
	struct net_info *iter, *next;
	struct net_device *ndev = NULL;

	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	for_each_ndev(cfg, iter, next) {
		GCC_DIAGNOSTIC_POP();
		if (iter->ndev) {
			if (strncmp(iter->ndev->name, ifname, IFNAMSIZ) == 0) {
				ndev = iter->ndev;
				break;
			}
		}
	}

	return ndev;
}

#ifdef WBTEXT
static bool wl_cfg80211_wbtext_check_bssid_list(struct bcm_cfg80211 *cfg, struct ether_addr *ea)
{
	wl_wbtext_bssid_t *bssid = NULL;
	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	/* check duplicate */
	list_for_each_entry(bssid, &cfg->wbtext_bssid_list, list) {
		GCC_DIAGNOSTIC_POP();
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

	bssid = (wl_wbtext_bssid_t *)MALLOC(cfg->osh, sizeof(wl_wbtext_bssid_t));
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
		GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
		bssid = list_entry(cfg->wbtext_bssid_list.next, wl_wbtext_bssid_t, list);
		GCC_DIAGNOSTIC_POP();
		if (bssid) {
			WL_DBG(("clear wbtext bssid : %s\n", bcm_ether_ntoa(&bssid->ea, eabuf)));
			list_del(&bssid->list);
			MFREE(cfg->osh, bssid, sizeof(wl_wbtext_bssid_t));
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
			WL_DBG(("sending neighbor report\n"));
			req_sent = wl_cfg80211_wbtext_send_nbr_req(cfg, dev, profile);
		}
	}

	/* if RRM nbr was not supported, check BTM bit in extend cap. IE */
	if (!req_sent) {
		if ((cap_ie = bcm_parse_tlvs(conn_info->resp_ie, conn_info->resp_ie_len,
				DOT11_MNG_EXT_CAP_ID)) != NULL) {
			if (cap_ie->len >= DOT11_EXTCAP_LEN_BSSTRANS &&
					isset(cap_ie->data, DOT11_EXT_CAP_BSSTRANS_MGMT)) {
				WL_DBG(("sending btm query\n"));
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

	smbuf = (char *)MALLOCZ(cfg->osh, WLC_IOCTL_MAXLEN);
	if (smbuf == NULL) {
		WL_ERR(("failed to allocated memory\n"));
		goto nbr_req_out;
	}

	ssid = (wlc_ssid_t *)MALLOCZ(cfg->osh, sizeof(wlc_ssid_t));
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
		MFREE(cfg->osh, ssid, sizeof(wlc_ssid_t));
	}

	if (smbuf) {
		MFREE(cfg->osh, smbuf, WLC_IOCTL_MAXLEN);
	}
	return ret;
}

static bool wl_cfg80211_wbtext_send_btm_query(struct bcm_cfg80211 *cfg, struct net_device *dev,
	struct wl_profile *profile)

{
	int error = -1;
	bool ret = FALSE;
	wl_bsstrans_query_t btq;

	WL_DBG(("Enter\n"));

	bzero(&btq, sizeof(wl_bsstrans_query_t));

	btq.version = WL_BSSTRANS_QUERY_VERSION_1;
	error = wldev_iovar_setbuf(dev, "wnm_bsstrans_query", &btq,
		sizeof(btq), cfg->ioctl_buf, WLC_IOCTL_SMLEN, &cfg->ioctl_buf_sync);
	if (error == BCME_OK) {
		ret = wl_cfg80211_wbtext_add_bssid_list(cfg,
			(struct ether_addr *)&profile->bssid);
	} else {
		WL_ERR(("wl_cfg80211_wbtext_send_btm_query: failed to set BTM query,"
			" error=%d\n", error));
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
			if (error == BCME_BADARG) {
				WL_ERR(("set wnm_keepalive with invalid arguments\n"));
			} else {
				WL_ERR(("set wnm_keepalives_max_idle failed : %d\n", error));
			}
		}
	}
}

static int
wl_cfg80211_recv_nbr_resp(struct net_device *dev, uint8 *body, uint body_len)
{
	dot11_rm_action_t *rm_rep;
	bcm_tlv_t *tlvs;
	uint tlv_len;
	int i, error;
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
		WL_DBG(("ch:%d, bssid:"MACDBG"\n",
			ch, MAC2STRDBG(nbr_rep_ie->bssid.octet)));

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
void
wl_cfg80211_set_cac(struct bcm_cfg80211 *cfg, int enable)
{
	int ret = 0;
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);

	WL_DBG(("cac enable %d\n", enable));
	if (!dhd) {
		WL_ERR(("dhd is NULL\n"));
		return;
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

#ifdef SUPPORT_RSSI_SUM_REPORT
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

	bzero(iobuf, WLC_IOCTL_SMLEN);

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
			set_param = (rssi_ant_param_t *)MALLOCZ(cfg->osh, sizeof(rssi_ant_param_t));
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
		MFREE(cfg->osh, set_param, sizeof(rssi_ant_param_t));
	}

	return err;
}

int
wl_get_rssi_logging(struct net_device *dev, void *param)
{
	rssilog_get_param_t *get_param = (rssilog_get_param_t *)param;
	char iobuf[WLC_IOCTL_SMLEN];
	int err = BCME_OK;

	bzero(iobuf, WLC_IOCTL_SMLEN);
	bzero(get_param, sizeof(*get_param));
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
#endif /* SUPPORT_RSSI_SUM_REPORT */
/* Function to flush the FW preserve buffer content
* The buffer content is sent to host in form of events.
*/
void
wl_flush_fw_log_buffer(struct net_device *dev, uint32 logset_mask)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);
	int i;
	int err = 0;
	u8 buf[WLC_IOCTL_SMLEN] = {0};
	wl_el_set_params_t set_param;

	/* Set the size of data to retrieve */
	memset(&set_param, 0, sizeof(set_param));
	set_param.size = WLC_IOCTL_SMLEN;

	for (i = 0; i < dhd->event_log_max_sets; i++)
	{
		if ((0x01u << i) & logset_mask) {
			set_param.set = i;
			err = wldev_iovar_setbuf(dev, "event_log_get", &set_param,
				sizeof(struct wl_el_set_params_s), buf, WLC_IOCTL_SMLEN,
				NULL);
			if (err) {
				WL_DBG(("Failed to get fw preserve logs, err=%d\n", err));
			}
		}
	}
}
#ifdef USE_WFA_CERT_CONF
extern int g_frameburst;
#endif /* USE_WFA_CERT_CONF */

int
wl_cfg80211_set_frameburst(struct bcm_cfg80211 *cfg, bool enable)
{
	int ret = BCME_OK;
	int val = enable ? 1 : 0;

#ifdef USE_WFA_CERT_CONF
	if (!g_frameburst) {
		WL_DBG(("Skip setting frameburst\n"));
		return 0;
	}
#endif /* USE_WFA_CERT_CONF */

	WL_DBG(("Set frameburst %d\n", val));
	ret = wldev_ioctl_set(bcmcfg_to_prmry_ndev(cfg), WLC_SET_FAKEFRAG, &val, sizeof(val));
	if (ret < 0) {
		WL_ERR(("Failed set frameburst, ret=%d\n", ret));
	} else {
		WL_INFORM_MEM(("frameburst is %s\n", enable ? "enabled" : "disabled"));
	}

	return ret;
}

s32
wl_cfg80211_set_dbg_verbose(struct net_device *ndev, u32 level)
{
	/* configure verbose level for debugging */
	if (level) {
		/* Enable increased verbose */
		wl_dbg_level |= WL_DBG_DBG;
	} else {
		/* Disable */
		wl_dbg_level &= ~WL_DBG_DBG;
	}
	WL_INFORM(("debug verbose set to %d\n", level));

	return BCME_OK;
}

const u8 *
wl_find_attribute(const u8 *buf, u16 len, u16 element_id)
{
	const u8 *attrib;
	u16 attrib_id;
	u16 attrib_len;

	if (!buf) {
		WL_ERR(("buf null\n"));
		return NULL;
	}

	attrib = buf;
	while (len >= 4) {
		/* attribute id */
		attrib_id = *attrib++ << 8;
		attrib_id |= *attrib++;
		len -= 2;

		/* 2-byte little endian */
		attrib_len = *attrib++ << 8;
		attrib_len |= *attrib++;

		len -= 2;
		if (attrib_id == element_id) {
			/* This will point to start of subelement attrib after
			 * attribute id & len
			 */
			return attrib;
		}
		if (len > attrib_len) {
			len -= attrib_len;	/* for the remaining subelt fields */
			WL_DBG(("Attribue:%4x attrib_len:%d rem_len:%d\n",
				attrib_id, attrib_len, len));

			/* Go to next subelement */
			attrib += attrib_len;
		} else {
			WL_ERR(("Incorrect Attribue:%4x attrib_len:%d\n",
				attrib_id, attrib_len));
			return NULL;
		}
	}
	return NULL;
}

uint8 wl_cfg80211_get_bus_state(struct bcm_cfg80211 *cfg)
{
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);
	WL_INFORM(("dhd->hang_was_sent = %d and busstate = %d\n",
			dhd->hang_was_sent, dhd->busstate));
	return ((dhd->busstate == DHD_BUS_DOWN) || dhd->hang_was_sent);
}

#ifdef WL_WPS_SYNC
static void wl_wps_reauth_timeout(unsigned long data)
{
	struct net_device *ndev = (struct net_device *)data;
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	s32 inst;
	unsigned long flags;

	WL_CFG_WPS_SYNC_LOCK(&cfg->wps_sync, flags);
	inst = wl_get_wps_inst_match(cfg, ndev);
	if (inst >= 0) {
		WL_ERR(("[%s][WPS] Reauth Timeout Inst:%d! state:%d\n",
				ndev->name, inst, cfg->wps_session[inst].state));
		if (cfg->wps_session[inst].state == WPS_STATE_REAUTH_WAIT) {
			/* Session should get deleted from success (linkup) or
			 * deauth case. Just in case, link reassoc failed, clear
			 * state here.
			 */
			WL_ERR(("[%s][WPS] Reauth Timeout Inst:%d!\n",
				ndev->name, inst));
			cfg->wps_session[inst].state = WPS_STATE_IDLE;
			cfg->wps_session[inst].in_use = false;
		}
	}
	WL_CFG_WPS_SYNC_UNLOCK(&cfg->wps_sync, flags);
}

static void wl_init_wps_reauth_sm(struct bcm_cfg80211 *cfg)
{
	/* Only two instances are supported as of now. one for
	 * infra STA and other for infra STA/GC.
	 */
	int i = 0;
	struct net_device *pdev = bcmcfg_to_prmry_ndev(cfg);

	spin_lock_init(&cfg->wps_sync);
	for (i = 0; i < WPS_MAX_SESSIONS; i++) {
		/* Init scan_timeout timer */
		init_timer_compat(&cfg->wps_session[i].timer, wl_wps_reauth_timeout, pdev);
		cfg->wps_session[i].in_use = false;
		cfg->wps_session[i].state = WPS_STATE_IDLE;
	}
}

static void wl_deinit_wps_reauth_sm(struct bcm_cfg80211 *cfg)
{
	int i = 0;

	for (i = 0; i < WPS_MAX_SESSIONS; i++) {
		cfg->wps_session[i].in_use = false;
		cfg->wps_session[i].state = WPS_STATE_IDLE;
		del_timer_sync(&cfg->wps_session[i].timer);
	}

}

static s32
wl_get_free_wps_inst(struct bcm_cfg80211 *cfg)
{
	int i;

	for (i = 0; i < WPS_MAX_SESSIONS; i++) {
		if (!cfg->wps_session[i].in_use) {
			return i;
		}
	}
	return BCME_ERROR;
}

static s32
wl_get_wps_inst_match(struct bcm_cfg80211 *cfg, struct net_device *ndev)
{
	int i;

	for (i = 0; i < WPS_MAX_SESSIONS; i++) {
		if ((cfg->wps_session[i].in_use) &&
			(ndev == cfg->wps_session[i].ndev)) {
			return i;
		}
	}

	return BCME_ERROR;
}

static s32
wl_wps_session_add(struct net_device *ndev, u16 mode, u8 *mac_addr)
{
	s32 inst;
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	unsigned long flags;

	WL_CFG_WPS_SYNC_LOCK(&cfg->wps_sync, flags);
	/* Fetch and initialize a wps instance */
	inst = wl_get_free_wps_inst(cfg);
	if (inst == BCME_ERROR) {
		WL_ERR(("[WPS] No free insance\n"));
		WL_CFG_WPS_SYNC_UNLOCK(&cfg->wps_sync, flags);
		return BCME_ERROR;
	}
	cfg->wps_session[inst].in_use = true;
	cfg->wps_session[inst].state = WPS_STATE_STARTED;
	cfg->wps_session[inst].ndev = ndev;
	cfg->wps_session[inst].mode = mode;
	/* return check not required since both buffer lens are same */
	(void)memcpy_s(cfg->wps_session[inst].peer_mac, ETH_ALEN, mac_addr, ETH_ALEN);
	WL_CFG_WPS_SYNC_UNLOCK(&cfg->wps_sync, flags);

	WL_INFORM_MEM(("[%s][WPS] session created.  Peer: " MACDBG "\n",
		ndev->name, MAC2STRDBG(mac_addr)));
	return BCME_OK;
}

static void
wl_wps_session_del(struct net_device *ndev)
{
	s32 inst;
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	unsigned long flags;
	u16 cur_state;

	WL_CFG_WPS_SYNC_LOCK(&cfg->wps_sync, flags);

	/* Get current instance for the given ndev */
	inst = wl_get_wps_inst_match(cfg, ndev);
	if (inst == BCME_ERROR) {
		WL_DBG(("[WPS] instance match NOT found\n"));
		WL_CFG_WPS_SYNC_UNLOCK(&cfg->wps_sync, flags);
		return;
	}

	cur_state = cfg->wps_session[inst].state;
	if (cur_state != WPS_STATE_DONE) {
		WL_DBG(("[WPS] wrong state:%d\n", cur_state));
		WL_CFG_WPS_SYNC_UNLOCK(&cfg->wps_sync, flags);
		return;
	}

	/* Mark this as unused */
	cfg->wps_session[inst].in_use = false;
	cfg->wps_session[inst].state = WPS_STATE_IDLE;
	WL_CFG_WPS_SYNC_UNLOCK(&cfg->wps_sync, flags);

	/* Ensure this API is called from sleepable context. */
	del_timer_sync(&cfg->wps_session[inst].timer);

	WL_INFORM_MEM(("[%s][WPS] session deleted\n", ndev->name));
}

static void
wl_wps_handle_ifdel(struct net_device *ndev)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	unsigned long flags;
	u16 cur_state;
	s32 inst;

	WL_CFG_WPS_SYNC_LOCK(&cfg->wps_sync, flags);
	inst = wl_get_wps_inst_match(cfg, ndev);
	if (inst == BCME_ERROR) {
		WL_DBG(("[WPS] instance match NOT found\n"));
		WL_CFG_WPS_SYNC_UNLOCK(&cfg->wps_sync, flags);
		return;
	}
	cur_state = cfg->wps_session[inst].state;
	cfg->wps_session[inst].state = WPS_STATE_DONE;
	WL_CFG_WPS_SYNC_UNLOCK(&cfg->wps_sync, flags);

	WL_INFORM_MEM(("[%s][WPS] state:%x\n", ndev->name, cur_state));
	if (cur_state > WPS_STATE_IDLE) {
		wl_wps_session_del(ndev);
	}
}

static s32
wl_wps_handle_sta_linkdown(struct net_device *ndev, u16 inst)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	unsigned long flags;
	u16 cur_state;
	bool wps_done = false;

	WL_CFG_WPS_SYNC_LOCK(&cfg->wps_sync, flags);
	cur_state = cfg->wps_session[inst].state;
	if (cur_state == WPS_STATE_REAUTH_WAIT) {
		WL_CFG_WPS_SYNC_UNLOCK(&cfg->wps_sync, flags);
		wl_clr_drv_status(cfg, CONNECTED, ndev);
		wl_clr_drv_status(cfg, DISCONNECTING, ndev);
		WL_INFORM_MEM(("[%s][WPS] REAUTH link down\n", ndev->name));
		/* Drop the link down event while we are waiting for reauth */
		return BCME_UNSUPPORTED;
	} else if (cur_state == WPS_STATE_STARTED) {
		/* Link down before reaching EAP-FAIL. End WPS session */
		cfg->wps_session[inst].state = WPS_STATE_DONE;
		wps_done = true;
		WL_INFORM_MEM(("[%s][WPS] link down after wps start\n", ndev->name));
	} else {
		WL_DBG(("[%s][WPS] link down in state:%d\n",
			ndev->name, cur_state));
	}

	WL_CFG_WPS_SYNC_UNLOCK(&cfg->wps_sync, flags);

	if (wps_done) {
		wl_wps_session_del(ndev);
	}
	return BCME_OK;
}

static s32
wl_wps_handle_peersta_linkdown(struct net_device *ndev, u16 inst, const u8 *peer_mac)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	unsigned long flags;
	u16 cur_state;
	s32 ret = BCME_OK;
	bool wps_done = false;

	WL_CFG_WPS_SYNC_LOCK(&cfg->wps_sync, flags);
	cur_state = cfg->wps_session[inst].state;

	if (!peer_mac) {
		WL_ERR(("Invalid arg\n"));
		ret = BCME_ERROR;
		goto exit;
	}

	/* AP/GO can have multiple clients. so validate peer_mac addr
	 * and ensure states are updated only for right peer.
	 */
	if (memcmp(cfg->wps_session[inst].peer_mac, peer_mac, ETH_ALEN)) {
		/* Mac addr not matching. Ignore. */
		WL_DBG(("[%s][WPS] No active WPS session"
			"for the peer:" MACDBG "\n", ndev->name, MAC2STRDBG(peer_mac)));
		ret = BCME_OK;
		goto exit;
	}
	if (cur_state == WPS_STATE_REAUTH_WAIT) {
		WL_INFORM_MEM(("[%s][WPS] REAUTH link down."
			" Peer: " MACDBG "\n",
			ndev->name, MAC2STRDBG(peer_mac)));
#ifdef NOT_YET
		/* Link down during REAUTH state is expected. However,
		 * if this is send up, hostapd statemachine issues a
		 * deauth down and that may pre-empt WPS reauth state
		 * at GC.
		 */
		WL_INFORM_MEM(("[%s][WPS] REAUTH link down. Ignore."
			" for client:" MACDBG "\n",
			ndev->name, MAC2STRDBG(peer_mac)));
		ret = BCME_UNSUPPORTED;
#endif
	} else if (cur_state == WPS_STATE_STARTED) {
		/* Link down before reaching REAUTH_WAIT state. WPS
		 * session ended.
		 */
		cfg->wps_session[inst].state = WPS_STATE_DONE;
		WL_INFORM_MEM(("[%s][WPS] link down after wps start"
			" client:" MACDBG "\n",
			ndev->name, MAC2STRDBG(peer_mac)));
		wps_done = true;
		/* since we have freed lock above, return from here */
		ret = BCME_OK;
	} else {
		WL_ERR(("[%s][WPS] Unsupported state:%d",
			ndev->name, cur_state));
		ret = BCME_ERROR;
	}
exit:
	WL_CFG_WPS_SYNC_UNLOCK(&cfg->wps_sync, flags);
	if (wps_done) {
		wl_wps_session_del(ndev);
	}
	return ret;
}

static s32
wl_wps_handle_sta_linkup(struct net_device *ndev, u16 inst)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	unsigned long flags;
	u16 cur_state;
	s32 ret = BCME_OK;
	bool wps_done = false;

	WL_CFG_WPS_SYNC_LOCK(&cfg->wps_sync, flags);
	cur_state = cfg->wps_session[inst].state;
	if (cur_state == WPS_STATE_REAUTH_WAIT) {
		/* WPS session succeeded. del session. */
		cfg->wps_session[inst].state = WPS_STATE_DONE;
		wps_done = true;
		WL_INFORM_MEM(("[%s][WPS] WPS_REAUTH link up (WPS DONE)\n", ndev->name));
		ret = BCME_OK;
	} else {
		WL_ERR(("[%s][WPS] unexpected link up in state:%d \n",
			ndev->name, cur_state));
		ret = BCME_ERROR;
	}
	WL_CFG_WPS_SYNC_UNLOCK(&cfg->wps_sync, flags);
	if (wps_done) {
		wl_wps_session_del(ndev);
	}
	return ret;
}

static s32
wl_wps_handle_peersta_linkup(struct net_device *ndev, u16 inst, const u8 *peer_mac)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	unsigned long flags;
	u16 cur_state;
	s32 ret = BCME_OK;

	WL_CFG_WPS_SYNC_LOCK(&cfg->wps_sync, flags);
	cur_state = cfg->wps_session[inst].state;

	/* For AP case, check whether call came for right peer */
	if (!peer_mac ||
		memcmp(cfg->wps_session[inst].peer_mac, peer_mac, ETH_ALEN)) {
		WL_ERR(("[WPS] macaddr mismatch\n"));
		WL_CFG_WPS_SYNC_UNLOCK(&cfg->wps_sync, flags);
		/* Mac addr not matching. Ignore. */
		return BCME_ERROR;
	}

	if (cur_state == WPS_STATE_REAUTH_WAIT) {
		WL_INFORM_MEM(("[%s][WPS] REAUTH link up\n", ndev->name));
		ret = BCME_OK;
	} else {
		WL_INFORM_MEM(("[%s][WPS] unexpected link up in state:%d \n",
			ndev->name, cur_state));
		ret = BCME_ERROR;
	}

	WL_CFG_WPS_SYNC_UNLOCK(&cfg->wps_sync, flags);

	return ret;
}

static s32
wl_wps_handle_authorize(struct net_device *ndev, u16 inst, const u8 *peer_mac)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	unsigned long flags;
	u16 cur_state;
	bool wps_done = false;
	s32 ret = BCME_OK;

	WL_CFG_WPS_SYNC_LOCK(&cfg->wps_sync, flags);
	cur_state = cfg->wps_session[inst].state;

	/* For AP case, check whether call came for right peer */
	if (!peer_mac ||
		memcmp(cfg->wps_session[inst].peer_mac, peer_mac, ETH_ALEN)) {
		WL_ERR(("[WPS] macaddr mismatch\n"));
		WL_CFG_WPS_SYNC_UNLOCK(&cfg->wps_sync, flags);
		/* Mac addr not matching. Ignore. */
		return BCME_ERROR;
	}

	if (cur_state == WPS_STATE_REAUTH_WAIT) {
		/* WPS session succeeded. del session. */
		cfg->wps_session[inst].state = WPS_STATE_DONE;
		wps_done = true;
		WL_INFORM_MEM(("[%s][WPS] Authorize done (WPS DONE)\n", ndev->name));
		ret = BCME_OK;
	} else {
		WL_INFORM_MEM(("[%s][WPS] unexpected Authorize in state:%d \n",
			ndev->name, cur_state));
		ret = BCME_ERROR;
	}

	WL_CFG_WPS_SYNC_UNLOCK(&cfg->wps_sync, flags);
	if (wps_done) {
		wl_wps_session_del(ndev);
	}
	return ret;
}

static s32
wl_wps_handle_reauth(struct net_device *ndev, u16 inst, const u8 *peer_mac)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	unsigned long flags;
	u16 cur_state;
	u16 mode;
	s32 ret = BCME_OK;

	WL_CFG_WPS_SYNC_LOCK(&cfg->wps_sync, flags);
	cur_state = cfg->wps_session[inst].state;
	mode = cfg->wps_session[inst].mode;

	if (((mode == WL_MODE_BSS) && (cur_state == WPS_STATE_STARTED)) ||
		((mode == WL_MODE_AP) && (cur_state == WPS_STATE_M8_SENT))) {
		/* Move to reauth wait */
		cfg->wps_session[inst].state = WPS_STATE_REAUTH_WAIT;
		/* Use ndev to find the wps instance which fired the timer */
		timer_set_private(&cfg->wps_session[inst].timer, ndev);
		WL_CFG_WPS_SYNC_UNLOCK(&cfg->wps_sync, flags);
		mod_timer(&cfg->wps_session[inst].timer,
			jiffies + msecs_to_jiffies(WL_WPS_REAUTH_TIMEOUT));
		WL_INFORM_MEM(("[%s][WPS] STATE_REAUTH_WAIT mode:%d Peer: " MACDBG "\n",
			ndev->name, mode, MAC2STRDBG(peer_mac)));
		return BCME_OK;
	} else {
		/* 802.1x cases */
		WL_DBG(("[%s][WPS] EAP-FAIL\n", ndev->name));
	}
	WL_CFG_WPS_SYNC_UNLOCK(&cfg->wps_sync, flags);
	return ret;
}

static s32
wl_wps_handle_disconnect(struct net_device *ndev, u16 inst, const u8 *peer_mac)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	unsigned long flags;
	u16 cur_state;
	s32 ret = BCME_OK;

	WL_CFG_WPS_SYNC_LOCK(&cfg->wps_sync, flags);
	cur_state = cfg->wps_session[inst].state;
	/* If Disconnect command comes from  user space for STA/GC,
	 * respond with event without waiting for event from fw as
	 * it would be dropped by the WPS_SYNC code.
	 */
	if (cur_state == WPS_STATE_REAUTH_WAIT) {
		if (ETHER_ISBCAST(peer_mac)) {
			WL_INFORM_MEM(("[WPS] Bcast peer. Do nothing.\n"));
		} else {
			/* Notify link down */
			CFG80211_DISCONNECTED(ndev,
				WLAN_REASON_DEAUTH_LEAVING, NULL, 0,
				true, GFP_ATOMIC);
			WL_INFORM_MEM(("[WPS] Disconnect event notified\n"));
		}
	} else {
		WL_DBG(("[%s][WPS] Not valid state to report disconnected:%d",
			ndev->name, cur_state));
		ret = BCME_UNSUPPORTED;
	}
	WL_CFG_WPS_SYNC_UNLOCK(&cfg->wps_sync, flags);
	return ret;
}

static s32
wl_wps_handle_disconnect_client(struct net_device *ndev, u16 inst, const u8 *peer_mac)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	unsigned long flags;
	u16 cur_state;
	s32 ret = BCME_OK;
	bool wps_done = false;

	WL_CFG_WPS_SYNC_LOCK(&cfg->wps_sync, flags);
	cur_state = cfg->wps_session[inst].state;
	/* For GO/AP, ignore disconnect client during reauth state */
	if (cur_state == WPS_STATE_REAUTH_WAIT) {
		if (ETHER_ISBCAST(peer_mac)) {
			/* If there is broadcast deauth, then mark wps session as ended */
			cfg->wps_session[inst].state = WPS_STATE_DONE;
			wps_done = true;
			WL_INFORM_MEM(("[%s][WPS] BCAST deauth. WPS stopped.\n", ndev->name));
			ret = BCME_OK;
			goto exit;
		} else if (!(memcmp(cfg->wps_session[inst].peer_mac,
			peer_mac, ETH_ALEN))) {
			WL_ERR(("[%s][WPS] Drop disconnect client\n", ndev->name));
			ret = BCME_UNSUPPORTED;
		}
	}

exit:
	WL_CFG_WPS_SYNC_UNLOCK(&cfg->wps_sync, flags);
	if (wps_done) {
		wl_wps_session_del(ndev);
	}
	return ret;
}

static s32
wl_wps_handle_connect_fail(struct net_device *ndev, u16 inst)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	unsigned long flags;
	u16 cur_state;
	bool wps_done = false;

	WL_CFG_WPS_SYNC_LOCK(&cfg->wps_sync, flags);
	cur_state = cfg->wps_session[inst].state;
	if (cur_state == WPS_STATE_REAUTH_WAIT) {
		cfg->wps_session[inst].state = WPS_STATE_DONE;
		wps_done = true;
		WL_INFORM_MEM(("[%s][WPS] Connect fail. WPS stopped.\n",
			ndev->name));
	} else {
		WL_ERR(("[%s][WPS] Connect fail. state:%d\n",
			ndev->name, cur_state));
	}
	WL_CFG_WPS_SYNC_UNLOCK(&cfg->wps_sync, flags);
	if (wps_done) {
		wl_wps_session_del(ndev);
	}
	return BCME_OK;
}

static s32
wl_wps_handle_m8_sent(struct net_device *ndev, u16 inst, const u8 *peer_mac)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	unsigned long flags;
	u16 cur_state;
	s32 ret = BCME_OK;

	WL_CFG_WPS_SYNC_LOCK(&cfg->wps_sync, flags);
	cur_state = cfg->wps_session[inst].state;

	if (cur_state == WPS_STATE_STARTED) {
		/* Move to M8 sent state */
		cfg->wps_session[inst].state = WPS_STATE_M8_SENT;
		WL_CFG_WPS_SYNC_UNLOCK(&cfg->wps_sync, flags);
		return BCME_OK;
	} else {
		/* 802.1x cases */
		WL_DBG(("[%s][WPS] Not valid state to send M8\n", ndev->name));
	}
	WL_CFG_WPS_SYNC_UNLOCK(&cfg->wps_sync, flags);
	return ret;
}

s32
wl_wps_session_update(struct net_device *ndev, u16 state, const u8 *peer_mac)
{
	s32 inst;
	u16 mode;
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	s32 ret = BCME_ERROR;
	unsigned long flags;

	WL_CFG_WPS_SYNC_LOCK(&cfg->wps_sync, flags);
	/* Get current instance for the given ndev */
	inst = wl_get_wps_inst_match(cfg, ndev);
	if (inst == BCME_ERROR) {
		/* No active WPS session. Do Nothing. */
		WL_DBG(("[%s][WPS] No matching instance.\n", ndev->name));
		WL_CFG_WPS_SYNC_UNLOCK(&cfg->wps_sync, flags);
		return BCME_NOTFOUND;
	}
	mode = cfg->wps_session[inst].mode;
	WL_CFG_WPS_SYNC_UNLOCK(&cfg->wps_sync, flags);

	WL_DBG(("[%s][WPS] state:%d mode:%d Peer: " MACDBG "\n",
		ndev->name, state, mode, MAC2STRDBG(peer_mac)));

	switch (state) {
		case WPS_STATE_M8_RECVD:
		{
			/* Occasionally, due to race condition between ctrl
			 * and data path, deauth ind is recvd before EAP-FAIL.
			 * Ignore deauth ind before EAP-FAIL
			 * So move to REAUTH WAIT on receiving M8 on GC and
			 * ignore deauth ind before EAP-FAIL till 'x' timeout.
			 * Kickoff a timer to monitor reauth status.
			 */
			if (mode == WL_MODE_BSS) {
				ret = wl_wps_handle_reauth(ndev, inst, peer_mac);
			} else {
				/* Nothing to be done for AP/GO mode */
				ret = BCME_OK;
			}
			break;
		}
		case WPS_STATE_M8_SENT:
		{
			/* Mantain the M8 sent state to verify
			 * EAP-FAIL sent is valid
			 */
			if (mode == WL_MODE_AP) {
				ret = wl_wps_handle_m8_sent(ndev, inst, peer_mac);
			} else {
				/* Nothing to be done for STA/GC mode */
				ret = BCME_OK;
			}
			break;
		}
		case WPS_STATE_EAP_FAIL:
		{
			/* Move to REAUTH WAIT following EAP-FAIL TX on GO/AP.
			 * Kickoff a timer to monitor reauth status
			 */
			if (mode == WL_MODE_AP) {
				ret = wl_wps_handle_reauth(ndev, inst, peer_mac);
			} else {
				/* Nothing to be done for STA/GC mode */
				ret = BCME_OK;
			}
			break;
		}
		case WPS_STATE_LINKDOWN:
		{
			if (mode == WL_MODE_BSS) {
				ret = wl_wps_handle_sta_linkdown(ndev, inst);
			} else if (mode == WL_MODE_AP) {
				/* Take action only for matching peer mac */
				if (!memcmp(cfg->wps_session[inst].peer_mac, peer_mac, ETH_ALEN)) {
					ret = wl_wps_handle_peersta_linkdown(ndev, inst, peer_mac);
				}
			}
			break;
		}
		case WPS_STATE_LINKUP:
		{
			if (mode == WL_MODE_BSS) {
				wl_wps_handle_sta_linkup(ndev, inst);
			} else if (mode == WL_MODE_AP) {
				/* Take action only for matching peer mac */
				if (!memcmp(cfg->wps_session[inst].peer_mac, peer_mac, ETH_ALEN)) {
					wl_wps_handle_peersta_linkup(ndev, inst, peer_mac);
				}
			}
			break;
		}
		case WPS_STATE_DISCONNECT_CLIENT:
		{
			/* Disconnect STA/GC command from user space */
			if (mode == WL_MODE_AP) {
				ret = wl_wps_handle_disconnect_client(ndev, inst, peer_mac);
			} else {
				WL_ERR(("[WPS] Unsupported mode %d\n", mode));
			}
			break;
		}
		case WPS_STATE_DISCONNECT:
		{
			/* Disconnect command on STA/GC interface */
			if (mode == WL_MODE_BSS) {
				ret = wl_wps_handle_disconnect(ndev, inst, peer_mac);
			}
			break;
		}
		case WPS_STATE_CONNECT_FAIL:
		{
			if (mode == WL_MODE_BSS) {
				ret = wl_wps_handle_connect_fail(ndev, inst);
			} else {
				WL_ERR(("[WPS] Unsupported mode %d\n", mode));
			}
			break;
		}
		case WPS_STATE_AUTHORIZE:
		{
			if (mode == WL_MODE_AP) {
				/* Take action only for matching peer mac */
				if (!memcmp(cfg->wps_session[inst].peer_mac, peer_mac, ETH_ALEN)) {
					wl_wps_handle_authorize(ndev, inst, peer_mac);
				} else {
					WL_INFORM_MEM(("[WPS] Authorize Request for wrong peer\n"));
				}
			}
			break;
		}

	default:
		WL_ERR(("[WPS] Unsupported state:%d mode:%d\n", state, mode));
		ret = BCME_ERROR;
	}

	return ret;
}

#define EAP_EXP_ATTRIB_DATA_OFFSET 14
void
wl_handle_wps_states(struct net_device *ndev, u8 *pkt, u16 len, bool direction)
{
	eapol_header_t *eapol_hdr;
	bool tx_packet = direction;
	u16 eapol_type;
	u16 mode;
	u8 *peer_mac;

	if (!ndev || !pkt) {
		WL_ERR(("[WPS] Invalid arg\n"));
		return;
	}

	if (len < (ETHER_HDR_LEN + EAPOL_HDR_LEN)) {
		WL_ERR(("[WPS] Invalid len\n"));
		return;
	}

	eapol_hdr = (eapol_header_t *)pkt;
	eapol_type = eapol_hdr->type;

	peer_mac = tx_packet ? eapol_hdr->eth.ether_dhost :
			eapol_hdr->eth.ether_shost;
	/*
	 * The implementation assumes only one WPS session would be active
	 * per interface at a time. Even for hostap, the wps_pin session
	 * is limited to one enrollee/client at a time. A session is marked
	 * started on WSC_START and gets cleared from below contexts
	 * a) Deauth/link down before reaching EAP-FAIL state. (Fail case)
	 * b) Link up following EAP-FAIL. (success case)
	 * c) Link up timeout after EAP-FAIL. (Fail case)
	 */

	if (eapol_type == EAP_PACKET) {
		wl_eap_header_t *eap;

		if (len > sizeof(*eap)) {
			eap = (wl_eap_header_t *)(pkt + ETHER_HDR_LEN + EAPOL_HDR_LEN);
			if (eap->type == EAP_EXPANDED_TYPE) {
				wl_eap_exp_t *exp = (wl_eap_exp_t *)eap->data;
				if (eap->length > EAP_EXP_HDR_MIN_LENGTH) {
					/* opcode is at fixed offset */
					u8 opcode = exp->opcode;
					u16 eap_len = ntoh16(eap->length);

					WL_DBG(("[%s][WPS] EAP EXPANDED packet. opcode:%x len:%d\n",
						ndev->name, opcode, eap_len));
					if (opcode == EAP_WSC_MSG) {
						const u8 *msg;
						const u8* parse_buf = exp->data;
						/* Check if recvd pkt is fragmented */
						if ((!tx_packet) &&
							(exp->flags &
							EAP_EXP_FLAGS_FRAGMENTED_DATA)) {
							if ((eap_len - EAP_EXP_ATTRIB_DATA_OFFSET)
							> 2) {
								parse_buf +=
								EAP_EXP_FRAGMENT_LEN_OFFSET;
								eap_len -=
								EAP_EXP_FRAGMENT_LEN_OFFSET;
								WL_DBG(("Rcvd EAP"
								" fragmented pkt\n"));
							} else {
								/* If recvd pkt is fragmented
								* and does not have
								* length field drop the packet.
								*/
								return;
							}
						}

						msg = wl_find_attribute(parse_buf,
							(eap_len - EAP_EXP_ATTRIB_DATA_OFFSET),
							EAP_ATTRIB_MSGTYPE);
						if (unlikely(!msg)) {
							WL_ERR(("[WPS] ATTRIB MSG not found!\n"));
						} else if ((*msg == EAP_WSC_MSG_M8) &&
								!tx_packet) {
							/* In certain cases M2 can also carry
							 * credential. So add check for
							 * cred in M8/M2 and start reauth timer.
							 */
							WL_INFORM_MEM(("[%s][WPS] M8\n",
								ndev->name));
							wl_wps_session_update(ndev,
								WPS_STATE_M8_RECVD, peer_mac);
						} else if ((*msg == EAP_WSC_MSG_M8) &&
								tx_packet) {
							WL_INFORM_MEM(("[%s][WPS] M8 Sent\n",
								ndev->name));
							wl_wps_session_update(ndev,
								WPS_STATE_M8_SENT, peer_mac);
						} else {
							WL_DBG(("[%s][WPS] EAP WSC MSG: 0x%X\n",
								ndev->name, *msg));
						}
					} else if (opcode == EAP_WSC_START) {
						/* WSC session started. WSC_START - Tx from GO/AP.
						 * Session will be deleted on successful link up or
						 * on failure (deauth context)
						 */
						mode = tx_packet ? WL_MODE_AP : WL_MODE_BSS;
						wl_wps_session_add(ndev, mode, peer_mac);
						WL_INFORM_MEM(("[%s][WPS] WSC_START Mode:%d\n",
							ndev->name, mode));
					} else if (opcode == EAP_WSC_DONE) {
						/* WSC session done. TX on STA/GC. RX on GO/AP
						 * On devices where config file save fails, it may
						 * return WPS_NAK with config_error:0. But the
						 * connection would still proceed. Hence don't let
						 * state machine depend on WSC DONE.
						 */
						WL_INFORM_MEM(("[%s][WPS] WSC_DONE\n", ndev->name));
					}
				}
			}

			if (eap->code == EAP_CODE_FAILURE) {
				/* EAP_FAIL */
				WL_INFORM_MEM(("[%s][WPS] EAP_FAIL\n", ndev->name));
				wl_wps_session_update(ndev,
					WPS_STATE_EAP_FAIL, peer_mac);
			}
		}
	}
}
#endif /* WL_WPS_SYNC */

s32
wl_cfg80211_sup_event_handler(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *event, void *data)
{
	int err = BCME_OK;
	u32 status = ntoh32(event->status);
	struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);
	u32 reason = ntoh32(event->reason);

	if (!wl_get_drv_status(cfg, CFG80211_CONNECT, ndev)) {
		/* Join attempt via non-cfg80211 interface.
		 * Don't send resultant events to cfg80211
		 * layer
		 */
		WL_INFORM_MEM(("Event received in non-cfg80211"
			" connect state. Ignore\n"));
		return BCME_OK;
	}

	if ((status == WLC_SUP_KEYED || status == WLC_SUP_KEYXCHANGE_WAIT_G1) &&
	    reason == WLC_E_SUP_OTHER) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0))
		/* NL80211_CMD_PORT_AUTHORIZED supported above >= 4.15 */
		cfg80211_port_authorized(ndev, (u8 *)wl_read_prof(cfg, ndev, WL_PROF_BSSID),
			GFP_KERNEL);
		WL_INFORM_MEM(("4way HS finished. port authorized event sent\n"));
#elif ((LINUX_VERSION_CODE > KERNEL_VERSION(3, 14, 0)) || defined(WL_VENDOR_EXT_SUPPORT))
		err = wl_cfgvendor_send_async_event(bcmcfg_to_wiphy(cfg), ndev,
			BRCM_VENDOR_EVENT_PORT_AUTHORIZED, NULL, 0);
		WL_INFORM_MEM(("4way HS finished. port authorized event sent\n"));
#else
		/* not supported in kernel <= 3,14,0 */
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0) */
		/* Post SCB authorize actions */
		wl_cfg80211_post_scb_auth(cfg, ndev);
	} else if (status < WLC_SUP_KEYXCHANGE_WAIT_G1 && (reason != WLC_E_SUP_OTHER &&
		reason != WLC_E_SUP_PTK_UPDATE)) {
		/* if any failure seen while 4way HS, should send NL80211_CMD_DISCONNECT */
		WL_ERR(("4way HS error. status:%d, reason:%d\n", status, reason));
		CFG80211_DISCONNECTED(ndev, 0, NULL, 0, false, GFP_KERNEL);
	}

	return err;
}

#ifdef WL_BCNRECV
static s32
wl_bcnrecv_aborted_event_handler(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
		const wl_event_msg_t *e, void *data)
{
	s32 status = ntoh32(e->status);
	struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);
	/* Abort fakeapscan, when Roam is in progress */
	if (status == WLC_E_STATUS_RXBCN_ABORT) {
		wl_android_bcnrecv_stop(ndev, WL_BCNRECV_ROAMABORT);
	} else {
		WL_ERR(("UNKNOWN STATUS. status:%d\n", status));
	}
	return BCME_OK;
}
#endif /* WL_BCNRECV */

#ifdef WL_MBO
static s32
wl_mbo_event_handler(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	s32 err = 0;
	wl_event_mbo_t *mbo_evt = (wl_event_mbo_t *)data;
	wl_event_mbo_cell_nw_switch_t *cell_sw_evt = NULL;
	wl_btm_event_type_data_t *evt_data = NULL;

	WL_INFORM(("MBO: Evt %u\n", mbo_evt->type));

	if (mbo_evt->type == WL_MBO_E_CELLULAR_NW_SWITCH) {
		cell_sw_evt = (wl_event_mbo_cell_nw_switch_t *)mbo_evt->data;
		BCM_REFERENCE(cell_sw_evt);
		SUPP_EVENT(("CTRL-EVENT-CELLULAR-SWITCH", "reason %d cur_assoc_time_left %u "
			"reassoc_delay %u\n", cell_sw_evt->reason,
			cell_sw_evt->assoc_time_remain, cell_sw_evt->reassoc_delay));
	} else if (mbo_evt->type == WL_MBO_E_BTM_RCVD) {
		evt_data = (wl_btm_event_type_data_t *)mbo_evt->data;
		if (evt_data->version != WL_BTM_EVENT_DATA_VER_1) {
			WL_ERR(("version mismatch. rcvd %u expected %u\n",
				evt_data->version, WL_BTM_EVENT_DATA_VER_1));
				return -1;
		}
		SUPP_EVENT(("CTRL-EVENT-BRCM-BTM-REQ-RCVD", "reason=%u\n",
			evt_data->transition_reason));
	} else {
		WL_INFORM(("UNKNOWN EVENT. type:%u\n", mbo_evt->type));
	}
	return err;
}
#endif /* WL_MBO */

#ifdef WL_CAC_TS
static s32
wl_cfg80211_cac_event_handler(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
		const wl_event_msg_t *e, void *data)
{
	u32 event = ntoh32(e->event_type);
	s32 status = ntoh32(e->status);
	s32 reason = ntoh32(e->reason);

	BCM_REFERENCE(reason);

	if (event == WLC_E_ADDTS_IND) {
		/* The supp log format of adding ts_delay in success case needs to be maintained */
		if (status == WLC_E_STATUS_SUCCESS) {
			uint *ts_delay = (uint *)data;
			BCM_REFERENCE(ts_delay);
			SUPP_EVENT(("CTRL-EVENT-CAC-ADDTS", "status=%d reason=%d ts_delay=%u\n",
				status, reason, *ts_delay));
		} else {
			SUPP_EVENT(("CTRL-EVENT-CAC-ADDTS", "status=%d reason=%d\n",
				status, reason));
		}
	} else if (event == WLC_E_DELTS_IND) {
		SUPP_EVENT(("CTRL-EVENT-CAC-DELTS", "status=%d reason=%d\n", status, reason));
	}

	return BCME_OK;
}
#endif /* WL_CAC_TS */

#if defined(WL_MBO) || defined(WL_OCE)
static s32
wl_bssid_prune_event_handler(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
	const wl_event_msg_t *e, void *data)
{
	s32 err = 0;
	uint reason = 0;
	wl_bssid_pruned_evt_info_t *evt_info = (wl_bssid_pruned_evt_info_t *)data;

	if (evt_info->version == WL_BSSID_PRUNE_EVT_VER_1) {
		if (evt_info->reason == WLC_E_PRUNE_ASSOC_RETRY_DELAY) {
			/* MBO assoc retry delay */
			reason = WIFI_PRUNE_ASSOC_RETRY_DELAY;
			SUPP_EVENT(("CTRL-EVENT-BRCM-BSSID-PRUNED", "ssid=%s bssid=" MACF
				" reason=%u timeout_val=%u(ms)\n", evt_info->SSID,
				ETHER_TO_MACF(evt_info->BSSID),	reason, evt_info->time_remaining));
		} else if (evt_info->reason == WLC_E_PRUNE_RSSI_ASSOC_REJ) {
			/* OCE RSSI-based assoc rejection */
			reason = WIFI_PRUNE_RSSI_ASSOC_REJ;
			SUPP_EVENT(("CTRL-EVENT-BRCM-BSSID-PRUNED", "ssid=%s bssid=" MACF
				" reason=%u timeout_val=%u(ms) rssi_threshold=%d(dBm)\n",
				evt_info->SSID, ETHER_TO_MACF(evt_info->BSSID),
				reason, evt_info->time_remaining, evt_info->rssi_threshold));
		} else {
			/* Invalid other than the assoc retry delay/RSSI assoc rejection
			 * in the current handler
			 */
			BCM_REFERENCE(reason);
			WL_INFORM(("INVALID. reason:%u\n", evt_info->reason));
		}
	} else {
		WL_INFORM(("version mismatch. rcvd %u expected %u\n", evt_info->version,
			WL_BSSID_PRUNE_EVT_VER_1));
	}
	return err;
}
#endif /* WL_MBO || WL_OCE */
#ifdef RTT_SUPPORT
static s32
wl_cfg80211_rtt_event_handler(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
		const wl_event_msg_t *e, void *data)
{
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
	wl_event_msg_t event;

	(void)memcpy_s(&event, sizeof(wl_event_msg_t),
		e, sizeof(wl_event_msg_t));
	return dhd_rtt_event_handler(dhdp, &event, data);
}
#endif /* RTT_SUPPORT */

void
wl_print_verinfo(struct bcm_cfg80211 *cfg)
{
	char *ver_ptr;
	uint32 alloc_len = MOD_PARAM_INFOLEN;

	if (!cfg) {
		WL_ERR(("cfg is NULL\n"));
		return;
	}

	ver_ptr = (char *)MALLOCZ(cfg->osh, alloc_len);
	if (!ver_ptr) {
		WL_ERR(("Failed to alloc ver_ptr\n"));
		return;
	}

	if (!dhd_os_get_version(bcmcfg_to_prmry_ndev(cfg),
		TRUE, &ver_ptr, alloc_len)) {
		WL_ERR(("DHD Version: %s\n", ver_ptr));
	}

	if (!dhd_os_get_version(bcmcfg_to_prmry_ndev(cfg),
		FALSE, &ver_ptr, alloc_len)) {
		WL_ERR(("F/W Version: %s\n", ver_ptr));
	}

	MFREE(cfg->osh, ver_ptr, alloc_len);
}

/* Get the concurrency mode */
int wl_cfg80211_get_concurrency_mode(struct bcm_cfg80211 *cfg)
{
	struct net_info *iter, *next;
	uint cmode = CONCURRENCY_MODE_NONE;
	u32 connected_cnt = 0;
	u32 pre_channel = 0, channel = 0;
	u32 pre_band = 0;
	u32 chanspec = 0;
	u32 band = 0;

	connected_cnt = wl_get_drv_status_all(cfg, CONNECTED);
	if (connected_cnt <= 1) {
		return cmode;
	}
	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	for_each_ndev(cfg, iter, next) {
		if (iter->ndev) {
			if (wl_get_drv_status(cfg, CONNECTED, iter->ndev)) {
				if (wldev_iovar_getint(iter->ndev, "chanspec",
					(s32 *)&chanspec) == BCME_OK) {
					channel = wf_chspec_ctlchan(
						wl_chspec_driver_to_host(chanspec));
					band = (channel <= CH_MAX_2G_CHANNEL) ?
						IEEE80211_BAND_2GHZ : IEEE80211_BAND_5GHZ;
				}
				if ((!pre_channel && channel)) {
					pre_band = band;
					pre_channel = channel;
				} else if (pre_channel) {
					if ((pre_band == band) && (pre_channel == channel)) {
						cmode = CONCURRENCY_SCC_MODE;
						goto exit;
					} else if ((pre_band == band) && (pre_channel != channel)) {
						cmode = CONCURRENCY_VSDB_MODE;
						goto exit;
					} else if (pre_band != band) {
						cmode = CONCURRENCY_RSDB_MODE;
						goto exit;
					}
				}
			}
		}
	}
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && \
(__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic pop")
#endif
exit:
	return cmode;
}
#ifdef WL_CHAN_UTIL
static s32
wl_cfg80211_bssload_report_event_handler(struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
		const wl_event_msg_t *e, void *data)
{
	s32 err = BCME_OK;
	struct sk_buff *skb = NULL;
	s32 status = ntoh32(e->status);
	u8 chan_use_percentage = 0;
#if (defined(CONFIG_ARCH_MSM) && defined(SUPPORT_WDEV_CFG80211_VENDOR_EVENT_ALLOC)) || \
		LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)
	struct net_device *ndev = bcmcfg_to_prmry_ndev(cfg);
#endif /* (defined(CONFIG_ARCH_MSM) && defined(SUPPORT_WDEV_CFG80211_VENDOR_EVENT_ALLOC)) || */
			/* LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0) */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
	struct wiphy *wiphy = bcmcfg_to_wiphy(cfg);
	uint len;
	gfp_t kflags;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0) */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
	len = CU_ATTR_HDR_LEN + sizeof(u8);
	kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0) */

#if (defined(CONFIG_ARCH_MSM) && defined(SUPPORT_WDEV_CFG80211_VENDOR_EVENT_ALLOC)) || \
	LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)
	skb = cfg80211_vendor_event_alloc(wiphy, ndev_to_wdev(ndev), len,
		BRCM_VENDOR_EVENT_CU, kflags);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
	skb = cfg80211_vendor_event_alloc(wiphy, len, BRCM_VENDOR_EVENT_CU, kflags);
#else
	/* No support exist */
#endif /* (defined(CONFIG_ARCH_MSM) && defined(SUPPORT_WDEV_CFG80211_VENDOR_EVENT_ALLOC)) || */
		/* LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0) */
	if (!skb) {
		WL_ERR(("skb alloc failed"));
		return -ENOMEM;
	}

	if ((status == WLC_E_STATUS_SUCCESS) && data) {
		wl_bssload_t *bssload_report = (wl_bssload_t *)data;
		chan_use_percentage = (bssload_report->chan_util * 100) / 255;
		WL_DBG(("ChannelUtilization=%hhu\n", chan_use_percentage));
		err = nla_put_u8(skb, CU_ATTR_PERCENTAGE, chan_use_percentage);
		if (err < 0) {
			WL_ERR(("Failed to put CU_ATTR_PERCENTAGE, err:%d\n", err));
		}
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
	cfg80211_vendor_event(skb, kflags);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0) */

	return err;
}

#define WL_CHAN_UTIL_DEFAULT_INTERVAL	3000
#define WL_CHAN_UTIL_THRESH_MIN		15
#define WL_CHAN_UTIL_THRESH_INTERVAL	10
#ifndef CUSTOM_CU_INTERVAL
#define CUSTOM_CU_INTERVAL WL_CHAN_UTIL_DEFAULT_INTERVAL
#endif /* CUSTOM_CU_INTERVAL */

static s32
wl_cfg80211_start_bssload_report(struct net_device *ndev)
{
	s32 err = BCME_OK;
	wl_bssload_cfg_t blcfg;
	u8 i;
	struct bcm_cfg80211 *cfg;

	if (!ndev) {
		return -ENODEV;
	}

	cfg = wl_get_cfg(ndev);
	if (!cfg) {
		return -ENODEV;
	}

	/* Typecasting to void as the buffer size is same as the memset size */
	(void)memset_s(&blcfg, sizeof(wl_bssload_cfg_t), 0, sizeof(wl_bssload_cfg_t));
	/* Set default report interval 3 sec and 8 threshhold levels between 15 to 85% */
	blcfg.rate_limit_msec = CUSTOM_CU_INTERVAL;
	blcfg.num_util_levels = MAX_BSSLOAD_LEVELS;
	for (i = 0; i < MAX_BSSLOAD_LEVELS; i++) {
		blcfg.util_levels[i] = (((WL_CHAN_UTIL_THRESH_MIN +
			(i * WL_CHAN_UTIL_THRESH_INTERVAL)) * 255) / 100);
	}

	err = wldev_iovar_setbuf(ndev, "bssload_report_event", &blcfg,
		sizeof(wl_bssload_cfg_t), cfg->ioctl_buf, WLC_IOCTL_SMLEN, &cfg->ioctl_buf_sync);
	if (unlikely(err)) {
		WL_ERR(("Set event_msgs error (%d)\n", err));
	}

	return err;
}
#endif /* WL_CHAN_UTIL */

s32
wl_cfg80211_config_suspend_events(struct net_device *ndev, bool enable)
{
	s32 err = 0;
	struct bcm_cfg80211 *cfg;
	s8 event_buf[WL_EVENTING_MASK_EXT_LEN + EVENTMSGS_EXT_STRUCT_SIZE] = {0};
	eventmsgs_ext_t *eventmask_msg = NULL;
	/*  Room for "event_msgs_ext" + '\0' + bitvec  */
	char iovbuf[WL_EVENTING_MASK_EXT_LEN + EVENTMSGS_EXT_STRUCT_SIZE + 16];
	s32 msglen = WL_EVENTING_MASK_EXT_LEN + EVENTMSGS_EXT_STRUCT_SIZE;

	if (!ndev) {
		return -ENODEV;
	}

	cfg = wl_get_cfg(ndev);
	if (!cfg) {
		return -ENODEV;
	}

	mutex_lock(&cfg->event_sync);

	eventmask_msg = (eventmsgs_ext_t *)event_buf;
	eventmask_msg->ver = EVENTMSGS_VER;
	eventmask_msg->command = EVENTMSGS_NONE;
	eventmask_msg->len = WL_EVENTING_MASK_EXT_LEN;
	eventmask_msg->maxgetsize = WL_EVENTING_MASK_EXT_LEN;

	/* Read event_msgs mask */
	err = wldev_iovar_getbuf(ndev, "event_msgs_ext",
		eventmask_msg, EVENTMSGS_EXT_STRUCT_SIZE,
		iovbuf,
		sizeof(iovbuf),
		NULL);

	if (unlikely(err)) {
		WL_ERR(("Get event_msgs error (%d)\n", err));
		goto eventmsg_out;
	}

	bcopy(iovbuf, eventmask_msg, msglen);

	/* Add set/clear of event mask under feature specific flags */
	if (enable) {
		WL_DBG(("%s: Enabling events on resume\n", __FUNCTION__));
#ifdef WL_CHAN_UTIL
		setbit(eventmask_msg->mask, WLC_E_BSS_LOAD);
#endif /* WL_CHAN_UTIL */
	} else {
		WL_DBG(("%s: Disabling events before suspend\n", __FUNCTION__));
#ifdef WL_CHAN_UTIL
		clrbit(eventmask_msg->mask, WLC_E_BSS_LOAD);
#endif /* WL_CHAN_UTIL */
	}

	/* Write updated Event mask */
	eventmask_msg->ver = EVENTMSGS_VER;
	eventmask_msg->command = EVENTMSGS_SET_MASK;
	eventmask_msg->len = WL_EVENTING_MASK_EXT_LEN;

	err = wldev_iovar_setbuf(ndev, "event_msgs_ext", eventmask_msg,
		WL_EVENTING_MASK_EXT_LEN + EVENTMSGS_EXT_STRUCT_SIZE,
		iovbuf, sizeof(iovbuf), NULL);

	if (unlikely(err)) {
		WL_ERR(("Set event_msgs error (%d)\n", err));
		goto eventmsg_out;
	}

eventmsg_out:
	mutex_unlock(&cfg->event_sync);
	return err;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0))
#ifdef WLFBT
static int
wl_cfg80211_update_ft_ies(struct wiphy *wiphy, struct net_device *dev,
	struct cfg80211_update_ft_ies_params *ftie)
{
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);

	if (!FW_SUPPORTED(dhdp, fbtoverds) && !FW_SUPPORTED(dhdp, fbt_adpt)) {
		WL_INFORM(("FW does not support FT roaming\n"));
		return BCME_UNSUPPORTED;
	}
	return BCME_OK;
}
#endif /* WLFBT */
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0) */

#ifdef WL_WIPSEVT
int
wl_cfg80211_wips_event_ext(wl_wips_event_info_t *wips_event)
{
	s32 err = BCME_OK;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
	struct sk_buff *skb;
	gfp_t kflags;
	struct bcm_cfg80211 *cfg;
	struct net_device *ndev;
	struct wiphy *wiphy;

	cfg = wl_cfg80211_get_bcmcfg();
	if (!cfg || !cfg->wdev) {
		WL_ERR(("WIPS evt invalid arg\n"));
		return err;
	}

	ndev = bcmcfg_to_prmry_ndev(cfg);
	wiphy = bcmcfg_to_wiphy(cfg);

	kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;
	skb = CFG80211_VENDOR_EVENT_ALLOC(wiphy, ndev_to_wdev(ndev),
		BRCM_VENDOR_WIPS_EVENT_BUF_LEN, BRCM_VENDOR_EVENT_WIPS, kflags);

	if (!skb) {
		WL_ERR(("skb alloc failed"));
		return BCME_NOMEM;
	}

	err = nla_put_u16(skb, WIPS_ATTR_DEAUTH_CNT, wips_event->misdeauth);
	if (unlikely(err)) {
		WL_ERR(("nla_put_u16 WIPS_ATTR_DEAUTH_CNT failed\n"));
		goto fail;
	}
	err = nla_put(skb, WIPS_ATTR_DEAUTH_BSSID, ETHER_ADDR_LEN, &wips_event->bssid);
	if (unlikely(err)) {
		WL_ERR(("nla_put WIPS_ATTR_DEAUTH_BSSID failed\n"));
		goto fail;
	}
	err = nla_put_s16(skb, WIPS_ATTR_CURRENT_RSSI, wips_event->current_RSSI);
	if (unlikely(err)) {
		WL_ERR(("nla_put_u16 WIPS_ATTR_CURRENT_RSSI failed\n"));
		goto fail;
	}
	err = nla_put_s16(skb, WIPS_ATTR_DEAUTH_RSSI, wips_event->deauth_RSSI);
	if (unlikely(err)) {
		WL_ERR(("nla_put_u16 WIPS_ATTR_DEAUTH_RSSI failed\n"));
		goto fail;
	}
	cfg80211_vendor_event(skb, kflags);

	return err;

fail:
	if (skb) {
		nlmsg_free(skb);
	}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0) */
	return err;
}

int
wl_cfg80211_wips_event(uint16 misdeauth, char* bssid)
{
	s32 err = BCME_OK;
	wl_wips_event_info_t wips_event;

	wips_event.misdeauth = misdeauth;
	memcpy(&wips_event.bssid, bssid, ETHER_ADDR_LEN);
	wips_event.current_RSSI = 0;
	wips_event.deauth_RSSI = 0;

	err = wl_cfg80211_wips_event_ext(&wips_event);
	return err;
}
#endif /* WL_WIPSEVT */

#ifdef PCIE_INB_DW
#define WL_DS(x)
/*
 * This API checks whether its okay to enter DS.
 * If some transaction is in progress, return true
 * to skip DS.
 */
#ifndef USECS_PER_MSEC
#define USECS_PER_MSEC 1000UL
#endif /* USECS_PER_MSEC */
bool wl_cfg80211_check_in_progress(struct net_device *dev)
{
	struct bcm_cfg80211 *cfg;
	struct net_device *pri_dev;
	u8 reason = WL_STATE_IDLE;
	u64 timeout;
	u64 start_time = 0;

	cfg = wl_get_cfg(dev);
	pri_dev = bcmcfg_to_prmry_ndev(cfg);

	/* check states like scan in progress, four way handshake, etc
	 * before entering Deep Sleep.
	 */
	if (wl_get_drv_status_all(cfg, SCANNING)) {
		WL_DS(("scan in progress\n"));
		reason = WL_STATE_SCANNING;
		start_time = GET_TS(cfg, scan_start);
	} else if (wl_get_drv_status_all(cfg, CONNECTING)) {
		WL_DS(("connect in progress\n"));
		reason = WL_STATE_CONNECTING;
		start_time = GET_TS(cfg, conn_start);
	} else if ((IS_STA_IFACE(ndev_to_wdev(dev))) &&
		wl_get_drv_status(cfg, CONNECTED, pri_dev) &&
		!wl_get_drv_status(cfg, AUTHORIZED, pri_dev)) {
		WL_DS(("connect-authorization in progress\n"));
		reason = WL_STATE_AUTHORIZING;
		start_time = GET_TS(cfg, authorize_start);
	}

	if (reason) {
		u64 curtime = OSL_LOCALTIME_NS();
		if (unlikely(!start_time)) {
			WL_ERR_RLMT(("state got cleared for reason:%d\n", reason));
			return false;
		}
		/* check whether we are stuck in a state
		 * for too long.
		 */
		timeout = (start_time + (WL_DS_SKIP_THRESHOLD_USECS * USECS_PER_MSEC));
		if (time_after64(curtime, timeout)) {
			/* state hasn't changed for WL_DS_SKIP_THRESHOLD_USECS */
			WL_ERR(("DS skip threshold hit. reason:%d start_time:"
					SEC_USEC_FMT" cur_time:"SEC_USEC_FMT"\n",
					reason, GET_SEC_USEC(start_time), GET_SEC_USEC(curtime)));
			ASSERT((0));
		}
		/* return true to skip suspend */
		return true;
	}

	return false;
}
#endif

bool wl_cfg80211_is_dpp_frame(void *frame, u32 frame_len)
{
		/* check for DPP public action frames */
	wl_dpp_pa_frame_t *pact_frm;

	if (frame == NULL) {
		return false;
	}
	pact_frm = (wl_dpp_pa_frame_t *)frame;
	if (frame_len < sizeof(wl_dpp_pa_frame_t) -1) {
		return false;
	}

	if ((pact_frm->category == WL_PUB_AF_CATEGORY) &&
		(pact_frm->action == WL_PUB_AF_ACTION) &&
		(pact_frm->oui_type == WL_PUB_AF_WFA_STYPE_DPP) &&
		(memcmp(pact_frm->oui, WFA_OUI, sizeof(pact_frm->oui)) == 0)) {
		return true;
	}

	return false;
}

const char *
get_dpp_pa_ftype(enum wl_dpp_ftype ftype)
{
	switch (ftype) {
		case DPP_AUTH_REQ:
			return "DPP_AUTH_REQ";
		case DPP_AUTH_RESP:
			return "DPP_AUTH_RESP";
		case DPP_AUTH_CONF:
			return "DPP_AUTH_CONF";
		default:
			return "Unkown DPP frame";
	}
}

#define GAS_RESP_LEN        2
#define DOUBLE_TLV_BODY_OFF 4
bool wl_cfg80211_find_gas_subtype(u8 subtype, u16 adv_id, u8* data, u32 len)
{
	const bcm_tlv_t *ie = (bcm_tlv_t *)data;
	const u8 *frame = NULL;
	u16 id, flen;

	/* Skipped first ANQP Element, if frame has anqp elemnt */
	ie = bcm_parse_tlvs(ie, len, DOT11_MNG_ADVERTISEMENT_ID);

	if (ie == NULL)
		return false;

	frame = (const uint8 *)ie + ie->len + TLV_HDR_LEN + GAS_RESP_LEN;
	id = ((u16) (((frame)[1] << 8) | (frame)[0]));
	flen = ((u16) (((frame)[3] << 8) | (frame)[2]));

	/* If the contents match the OUI and the type */
	if ((flen >= WFA_OUI_LEN + 1) &&
		(id == adv_id) &&
		!bcmp(&frame[DOUBLE_TLV_BODY_OFF], (const uint8*)WFA_OUI, WFA_OUI_LEN) &&
		subtype == frame[DOUBLE_TLV_BODY_OFF+WFA_OUI_LEN]) {
		return true;
	}

	return false;
}

bool wl_cfg80211_is_dpp_gas_action(void *frame, u32 frame_len)
{
	wl_dpp_gas_af_t *act_frm = (wl_dpp_gas_af_t *)frame;
	u32 len;
	const bcm_tlv_t *ie = NULL;

	if ((frame_len < (sizeof(wl_dpp_gas_af_t) - 1)) ||
			act_frm->category != WL_PUB_AF_CATEGORY) {
		return false;
	}

	len = frame_len - (u32)(sizeof(wl_dpp_gas_af_t) - 1);
	if (act_frm->action == WL_PUB_AF_GAS_IREQ) {
		ie = (bcm_tlv_t *)act_frm->query_data;
		/* We are interested only in MNG ADV ID. Skip any other id. */
		ie = bcm_parse_tlvs(ie, len, DOT11_MNG_ADVERTISEMENT_ID);
	} else if (act_frm->action == WL_PUB_AF_GAS_IRESP) {
		ie = (bcm_tlv_t *)&act_frm->query_data[WL_GAS_RESP_OFFSET];
		/* We are interested only in MNG ADV ID. Skip any other id. */
		ie = bcm_parse_tlvs(ie, len, DOT11_MNG_ADVERTISEMENT_ID);
	} else {
		return false;
	}

	if (ie && (ie->len >= WL_GAS_MIN_LEN) &&
		(memcmp(&ie->data[WL_GAS_WFA_OFFSET], WFA_OUI, 3) == 0) &&
		(ie->data[WL_GAS_STYPE_OFFSET] == WL_GAS_WFA_STYPE_DPP)) {
		WL_DBG(("DPP GAS FRAME. type:%d\n", act_frm->action));
		return true;
	}

	/* Non DPP GAS frame */
	return false;
}

#ifdef KEEP_ALIVE
#define KA_TEMP_BUF_SIZE 512
#define KA_FRAME_SIZE 300
int
wl_cfg80211_start_mkeep_alive(struct bcm_cfg80211 *cfg, uint8 mkeep_alive_id,
	uint16 ether_type, uint8 *ip_pkt, uint16 ip_pkt_len,
	uint8* src_mac, uint8* dst_mac, uint32 period_msec)
{
	const int ETHERTYPE_LEN = 2;
	char *pbuf = NULL;
	const char *str;
	wl_mkeep_alive_pkt_t mkeep_alive_pkt;
	wl_mkeep_alive_pkt_t *mkeep_alive_pktp = NULL;
	uint16 buf_len = 0;
	u8 str_len = 0;
	int res = BCME_ERROR;
	uint16 len_bytes = 0;
	int i = 0;
	uint16 pmac_frame_len = KA_FRAME_SIZE;
	uint16 pbuf_len = KA_TEMP_BUF_SIZE;

	/* ether frame to have both max IP pkt (256 bytes) and ether header */
	char *pmac_frame = NULL;
	char *pmac_frame_begin = NULL;
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);
	struct net_device *primary_ndev = NULL;
	primary_ndev = bcmcfg_to_prmry_ndev(cfg);

	/*
	 * The mkeep_alive packet is for STA interface only; if the bss is configured as AP,
	 * dongle shall reject a mkeep_alive request.
	 */
	if (!(dhd->op_mode & DHD_FLAG_STA_MODE))
		return res;

	WL_TRACE(("%s execution\n", __FUNCTION__));

	if ((pbuf = MALLOCZ(cfg->osh, KA_TEMP_BUF_SIZE)) == NULL) {
		WL_ERR(("failed to allocate buf with size %d\n", KA_TEMP_BUF_SIZE));
		res = BCME_NOMEM;
		return res;
	}

	if ((pmac_frame = MALLOCZ(cfg->osh, pmac_frame_len)) == NULL) {
		WL_ERR(("failed to allocate mac_frame with size %d\n", pmac_frame_len));
		res = BCME_NOMEM;
		goto exit;
	}
	pmac_frame_begin = pmac_frame;

	/*
	 * Get current mkeep-alive status.
	 */
	res = wldev_iovar_getbuf(primary_ndev, "mkeep_alive", &mkeep_alive_id,
		sizeof(mkeep_alive_id), pbuf, KA_TEMP_BUF_SIZE, &cfg->ioctl_buf_sync);
	if (res < 0) {
		WL_ERR(("%s: Get mkeep_alive failed (error=%d)\n", __FUNCTION__, res));
		goto exit;
	} else {
		/* Check available ID whether it is occupied */
		mkeep_alive_pktp = (wl_mkeep_alive_pkt_t *) pbuf;
		if (dtoh32(mkeep_alive_pktp->period_msec != 0)) {
			WL_ERR(("%s: Get mkeep_alive failed, ID %u is in use.\n",
				__FUNCTION__, mkeep_alive_id));

			/* Current occupied ID info */
			WL_ERR(("%s: mkeep_alive\n", __FUNCTION__));
			WL_ERR(("   Id    : %d\n"
				"   Period: %d msec\n"
				"   Length: %d\n"
				"   Packet: 0x",
				mkeep_alive_pktp->keep_alive_id,
				dtoh32(mkeep_alive_pktp->period_msec),
				dtoh16(mkeep_alive_pktp->len_bytes)));

			for (i = 0; i < mkeep_alive_pktp->len_bytes; i++) {
				WL_ERR(("%02x", mkeep_alive_pktp->data[i]));
			}
			WL_ERR(("\n"));

			res = BCME_NOTFOUND;
			goto exit;
		}
	}

	/* Request the specified ID */
	bzero(&mkeep_alive_pkt, sizeof(wl_mkeep_alive_pkt_t));
	bzero(pbuf, KA_TEMP_BUF_SIZE);
	str = "mkeep_alive";
	str_len = strlen(str);
	strlcpy(pbuf, str, KA_TEMP_BUF_SIZE);
	buf_len = str_len + 1;
	pbuf_len -= buf_len;

	mkeep_alive_pktp = (wl_mkeep_alive_pkt_t *) (pbuf + buf_len);
	mkeep_alive_pkt.period_msec = htod32(period_msec);
	mkeep_alive_pkt.version = htod16(WL_MKEEP_ALIVE_VERSION);
	mkeep_alive_pkt.length = htod16(WL_MKEEP_ALIVE_FIXED_LEN);
	len_bytes = (ETHER_ADDR_LEN*2) + ETHERTYPE_LEN + ip_pkt_len;
	mkeep_alive_pkt.len_bytes = htod16(len_bytes);

	/* ID assigned */
	mkeep_alive_pkt.keep_alive_id = mkeep_alive_id;

	buf_len += WL_MKEEP_ALIVE_FIXED_LEN;

	/*
	 * Build up Ethernet Frame
	 */

	/* Mapping dest mac addr */
	res = memcpy_s(pmac_frame, pmac_frame_len, dst_mac, ETHER_ADDR_LEN);
	if (res) {
		goto exit;
	}
	pmac_frame += ETHER_ADDR_LEN;
	pmac_frame_len -= ETHER_ADDR_LEN;

	/* Mapping src mac addr */
	res = memcpy_s(pmac_frame, pmac_frame_len, src_mac, ETHER_ADDR_LEN);
	if (res) {
		goto exit;
	}
	pmac_frame += ETHER_ADDR_LEN;
	pmac_frame_len -= ETHER_ADDR_LEN;

	/* Mapping Ethernet type */
	ether_type = hton16(ether_type);
	res = memcpy_s(pmac_frame, pmac_frame_len, &ether_type, ETHERTYPE_LEN);
	if (res) {
		goto exit;
	}
	pmac_frame += ETHERTYPE_LEN;
	pmac_frame_len -= ETHERTYPE_LEN;

	/* Mapping IP pkt */
	res = memcpy_s(pmac_frame, pmac_frame_len, ip_pkt, ip_pkt_len);
	if (res) {
		goto exit;
	}
	pmac_frame += ip_pkt_len;
	pmac_frame_len -= ip_pkt_len;

	/*
	 * Keep-alive attributes are set in local variable (mkeep_alive_pkt), and
	 * then memcpy'ed into buffer (mkeep_alive_pktp) since there is no
	 * guarantee that the buffer is properly aligned.
	 */
	res = memcpy_s((char *)mkeep_alive_pktp, pbuf_len,
		&mkeep_alive_pkt, WL_MKEEP_ALIVE_FIXED_LEN);
	if (res) {
		goto exit;
	}
	pbuf_len -= WL_MKEEP_ALIVE_FIXED_LEN;

	/*
	 * Length of ether frame (assume to be all hexa bytes)
	 *     = src mac + dst mac + ether type + ip pkt len
	 */
	res = memcpy_s(mkeep_alive_pktp->data, pbuf_len,
		pmac_frame_begin, len_bytes);
	if (res) {
		goto exit;
	}
	buf_len += len_bytes;

	res = wldev_ioctl_set(primary_ndev, WLC_SET_VAR, pbuf, buf_len);
exit:
	if (pmac_frame_begin) {
		MFREE(cfg->osh, pmac_frame_begin, KA_FRAME_SIZE);
	}
	if (pbuf) {
		MFREE(cfg->osh, pbuf, KA_TEMP_BUF_SIZE);
	}
	return res;
}

int
wl_cfg80211_stop_mkeep_alive(struct bcm_cfg80211 *cfg, uint8 mkeep_alive_id)
{
	char *pbuf = NULL;
	wl_mkeep_alive_pkt_t mkeep_alive_pkt;
	wl_mkeep_alive_pkt_t *mkeep_alive_pktp = NULL;
	int	res = BCME_ERROR;
	int	i = 0;
	struct net_device *primary_ndev = NULL;
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);
	primary_ndev = bcmcfg_to_prmry_ndev(cfg);

	/*
	 * The mkeep_alive packet is for STA interface only; if the bss is configured as AP,
	 * dongle shall reject a mkeep_alive request.
	 */
	if (!(dhd->op_mode & DHD_FLAG_STA_MODE))
		return res;

	WL_TRACE(("%s execution\n", __FUNCTION__));

	/*
	 * Get current mkeep-alive status. Skip ID 0 which is being used for NULL pkt.
	 */
	if ((pbuf = MALLOCZ(cfg->osh, KA_TEMP_BUF_SIZE)) == NULL) {
		WL_ERR(("failed to allocate buf with size %d\n", KA_TEMP_BUF_SIZE));
		res = BCME_NOMEM;
		return res;
	}

	res = wldev_iovar_getbuf(primary_ndev, "mkeep_alive", &mkeep_alive_id,
			sizeof(mkeep_alive_id), pbuf, KA_TEMP_BUF_SIZE, &cfg->ioctl_buf_sync);
	if (res < 0) {
		WL_ERR(("%s: Get mkeep_alive failed (error=%d)\n", __FUNCTION__, res));
		goto exit;
	} else {
		/* Check occupied ID */
		mkeep_alive_pktp = (wl_mkeep_alive_pkt_t *) pbuf;
		WL_DBG(("%s: mkeep_alive\n", __FUNCTION__));
		WL_DBG(("   Id    : %d\n"
			"   Period: %d msec\n"
			"   Length: %d\n"
			"   Packet: 0x",
			mkeep_alive_pktp->keep_alive_id,
			dtoh32(mkeep_alive_pktp->period_msec),
			dtoh16(mkeep_alive_pktp->len_bytes)));

		for (i = 0; i < mkeep_alive_pktp->len_bytes; i++) {
			WL_DBG(("%02x", mkeep_alive_pktp->data[i]));
		}
		WL_DBG(("\n"));
	}

	/* Make it stop if available */
	if (dtoh32(mkeep_alive_pktp->period_msec != 0)) {
		WL_INFORM_MEM(("stop mkeep_alive on ID %d\n", mkeep_alive_id));
		bzero(&mkeep_alive_pkt, sizeof(wl_mkeep_alive_pkt_t));

		mkeep_alive_pkt.period_msec = 0;
		mkeep_alive_pkt.version = htod16(WL_MKEEP_ALIVE_VERSION);
		mkeep_alive_pkt.length = htod16(WL_MKEEP_ALIVE_FIXED_LEN);
		mkeep_alive_pkt.keep_alive_id = mkeep_alive_id;

		res = wldev_iovar_setbuf(primary_ndev, "mkeep_alive",
			(char *)&mkeep_alive_pkt, WL_MKEEP_ALIVE_FIXED_LEN,
			pbuf, KA_TEMP_BUF_SIZE, &cfg->ioctl_buf_sync);
	} else {
		WL_ERR(("%s: ID %u does not exist.\n", __FUNCTION__, mkeep_alive_id));
		res = BCME_NOTFOUND;
	}
exit:
	if (pbuf) {
		MFREE(cfg->osh, pbuf, KA_TEMP_BUF_SIZE);
	}
	return res;
}
#endif /* KEEP_ALIVE */

s32
wl_cfg80211_handle_macaddr_change(struct net_device *dev, u8 *macaddr)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	uint8 wait_cnt = WAIT_FOR_DISCONNECT_MAX;
	u32 status = TRUE;

	if (IS_STA_IFACE(dev->ieee80211_ptr) &&
		wl_get_drv_status(cfg, CONNECTED, dev)) {
		/* Macaddress change in connected state. The curent
		 * connection will become invalid. Issue disconnect
		 * to current AP to let the AP know about link down
		 */
		WL_INFORM_MEM(("macaddr change in connected state. Force disassoc.\n"));
		wl_cfg80211_disassoc(dev, WLAN_REASON_DEAUTH_LEAVING);

		while ((status = wl_get_drv_status(cfg, CONNECTED, dev)) && wait_cnt) {
			WL_DBG(("Waiting for disconnection, wait_cnt: %d\n", wait_cnt));
			wait_cnt--;
			OSL_SLEEP(50);
		}
	}
	return BCME_OK;
}

int
wl_cfg80211_handle_hang_event(struct net_device *ndev, uint16 hang_reason, uint32 memdump_type)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);
	dhd_pub_t *dhd = (dhd_pub_t *)(cfg->pub);

	WL_INFORM_MEM(("hang reason = %d, memdump_type =%d\n",
		hang_reason, memdump_type));

	/* check if pre-registered mac matches the mac from dongle via WLC_E_LINK */
	if (wl_get_drv_status(cfg, READY, ndev)) {
#ifdef WL_CFGVENDOR_SEND_HANG_EVENT
		wl_copy_hang_info_if_falure(ndev,
			hang_reason, BCME_NOTFOUND);
#endif /* WL_CFGVENDOR_SEND_HANG_EVENT */
		SUPP_LOG(("Err. hang reason:%d, dump_type:%d\n", hang_reason, memdump_type));
		wl_flush_fw_log_buffer(ndev, FW_LOGSET_MASK_ALL);
		/* IF dongle is down due to previous hang or other conditions,
		 * sending 0ne more hang notification is not needed.
		 */

		if (dhd_query_bus_erros(dhd)) {
			return BCME_ERROR;
		}
		dhd->iface_op_failed = TRUE;
#if defined(DHD_FW_COREDUMP)
		if (dhd->memdump_enabled) {
			dhd->memdump_type = memdump_type;
			dhd_bus_mem_dump(dhd);
		}
#endif /* DHD_FW_COREDUMP */
#if defined(BCMDONGLEHOST) && defined(OEM_ANDROID)
		WL_ERR(("Notify hang event to upper layer \n"));
		dhd->hang_reason = hang_reason;
		net_os_send_hang_message(ndev);
#endif /* BCMDONGLEHOST &&  OEM_ANDROID */
	}

	return BCME_OK;
}

static void
wl_cfg80211_spmk_pmkdb_change_pmk_type(struct bcm_cfg80211 *cfg, pmkid_list_v3_t *pmk_list)
{
	int i;
	pmkid_list_v3_t *spmk_list = NULL;

	if (!cfg || !cfg->spmk_info_list || !cfg->spmk_info_list->pmkids.count) {
		return;
	}

	spmk_list = &cfg->spmk_info_list->pmkids;
	for (i = 0; i < spmk_list->count; i++) {
		if (memcmp(&pmk_list->pmkid->bssid,
			&spmk_list->pmkid[i].bssid, ETHER_ADDR_LEN)) {
			continue; /* different MAC */
		}
		WL_INFORM_MEM(("SPMK replace idx:%d bssid: "MACF " to SSID: %d\n", i,
			ETHER_TO_MACF(pmk_list->pmkid->bssid), spmk_list->pmkid[i].ssid_len));
		bzero(&pmk_list->pmkid->bssid, ETHER_ADDR_LEN);
		pmk_list->pmkid->ssid_len = spmk_list->pmkid[i].ssid_len;
		(void)memcpy_s(pmk_list->pmkid->ssid, spmk_list->pmkid[i].ssid_len,
			spmk_list->pmkid[i].ssid, spmk_list->pmkid[i].ssid_len);
	}
}

static void
wl_cfg80211_spmk_pmkdb_del_spmk(struct bcm_cfg80211 *cfg, struct cfg80211_pmksa *pmksa)
{
	pmkid_list_v3_t *spmk_list = NULL;
	bool bFound = FALSE;
	int i;

	if (!cfg || !cfg->spmk_info_list || !cfg->spmk_info_list->pmkids.count) {
		return;
	}

	spmk_list = &cfg->spmk_info_list->pmkids;
	for (i = 0; i < spmk_list->count; i++) {
		if (eacmp(&pmksa->bssid, &spmk_list->pmkid[i].bssid)) {
			continue; /* different MAC */
		}
		bFound = TRUE;
		break;
	}
	WL_INFORM_MEM(("wl_cfg80211_del_pmksa "MACDBG "found:%d(idx:%d)",
		MAC2STRDBG(spmk_list->pmkid[i].bssid.octet), bFound, i));
	if (!bFound) {
		return;
	}

	for (; i < spmk_list->count - 1; i++) {
		memcpy_s(&spmk_list->pmkid[i], sizeof(pmkid_v3_t),
			&spmk_list->pmkid[i + 1], sizeof(pmkid_v3_t));
	}
	spmk_list->count--;
}

static void
wl_cfg80211_handle_set_ssid_complete(struct bcm_cfg80211 *cfg, wl_assoc_status_t *as,
		const wl_event_msg_t *event, wl_assoc_state_t assoc_state)
{
	if (as->status != WLC_E_STATUS_SUCCESS) {
#ifdef CUSTOMER_HW6
		/* Some older chips sends SET_SSID with fail status and it
		 * still proceeds with join. Excempt it for the sake of
		 * compatibility
		 */
		return;
#endif /* CUSTOMER_HW6 */
#ifdef DHD_ENABLE_BIGDATA_LOGGING
		wl_get_connect_failed_status(cfg, event);
#endif /* DHD_ENABLE_BIGDATA_LOGGING */

#ifdef SET_SSID_FAIL_CUSTOM_RC
		if (as->status == WLC_E_STATUS_TIMEOUT) {
			WL_INFORM_MEM(("overriding reason code %d to %d\n",
					as->reason, SET_SSID_FAIL_CUSTOM_RC));
			as->reason = SET_SSID_FAIL_CUSTOM_RC;
		}
#endif /* SET_SSID_FAIL_CUSTOM_RC */

		/* Report connect failure */
		as->link_action = wl_set_link_action(assoc_state, false);
	}
#ifdef WL_NAN
	else if ((as->status == WLC_E_STATUS_SUCCESS) &&
			wl_cfgnan_is_enabled(cfg) &&
			wl_get_drv_status(cfg, CONNECTED, as->ndev)) {
		u8 *curbssid = wl_read_prof(cfg, as->ndev, WL_PROF_BSSID);
		u8 *conn_req_bssid =
			wl_read_prof(cfg, as->ndev, WL_PROF_LATEST_BSSID);

		if (memcmp(curbssid, conn_req_bssid, ETHER_ADDR_LEN) == 0) {
			wl_cfgnan_get_stats(cfg);
		}
	}
#endif /* WL_NAN */

	return;
}

#ifdef  WL_TWT
static s32
wl_notify_twt_event(struct bcm_cfg80211 *cfg,
		bcm_struct_cfgdev *cfgdev, const wl_event_msg_t *e, void *data)
{
	uint32 type;
	type = ntoh32(e->event_type);
	WL_DBG(("TWT event type %d\n", type));
	return BCME_OK;
}
#endif /* WL_TWT */

#define CHECK_AND_INCR_LEN(ret, len, maxlen) \
	{ \
		if ((ret < 0) || ((ret + len) > maxlen)) \
			return len; \
		len += ret; \
	}
u32
wl_cfg80211_debug_data_dump(struct net_device *dev, u8 *buf, u32 buf_len)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	u32 len = 0;
	dhd_pub_t *dhdp = (dhd_pub_t *)(cfg->pub);
	s32 ret = 0;

	BCM_REFERENCE(dhdp);
	if ((!wl_get_drv_status(cfg, READY, bcmcfg_to_prmry_ndev(cfg)))) {
		WL_INFORM(("driver not up.\n"));
		return 0;
	}

	ret = snprintf(buf, buf_len, "\n[BCMLINUX]\nlock info:\n");
	CHECK_AND_INCR_LEN(ret, len, buf_len);

	ret = snprintf(buf+len, buf_len-len,
			"rtnl:%d\n"
			"scan_sync:%d\n"
			"usr_sync:%d\n"
			"if_sync:%d\n"
			"event_sync:%d\n"
			"ioctl_buf:%d\n",
			rtnl_is_locked(),  mutex_is_locked(&cfg->scan_sync),
			mutex_is_locked(&cfg->usr_sync), mutex_is_locked(&cfg->if_sync),
			mutex_is_locked(&cfg->event_sync), mutex_is_locked(&cfg->ioctl_buf_sync));
	CHECK_AND_INCR_LEN(ret, len, buf_len);

#ifdef WL_NAN
	ret = snprintf(buf+len, buf_len-len, "nan:%d\n", mutex_is_locked(&cfg->nancfg->nan_sync));
	CHECK_AND_INCR_LEN(ret, len, buf_len);
#endif /* WL_NAN */
#ifdef RTT_SUPPORT
	{
		rtt_status_info_t *rtt_status = GET_RTTSTATE(dhdp);
		ret = snprintf(buf+len, buf_len-len, "rtt:%d\n",
				mutex_is_locked(&rtt_status->rtt_mutex));
		CHECK_AND_INCR_LEN(ret, len, buf_len);
		ret = snprintf(buf+len, buf_len-len, "geofence:%d\n",
				mutex_is_locked(&(rtt_status)->geofence_mutex));
		CHECK_AND_INCR_LEN(ret, len, buf_len);
	}
#endif /* RTT_SUPPORT */
#ifdef WL_BCNRECV
	ret = snprintf(buf+len, buf_len-len, "bcn_sync:%d\n", mutex_is_locked(&cfg->bcn_sync));
	CHECK_AND_INCR_LEN(ret, len, buf_len);
#endif /* WL_BCNRECV */
#ifdef WLTDLS
	ret = snprintf(buf+len, buf_len-len, "tdls_sync:%d\n", mutex_is_locked(&cfg->tdls_sync));
	CHECK_AND_INCR_LEN(ret, len, buf_len);
#endif /* WLTDLS */
	ret = snprintf(buf+len, buf_len-len, "cfgdrv:%d\n", spin_is_locked(&cfg->cfgdrv_lock));
	CHECK_AND_INCR_LEN(ret, len, buf_len);

	ret = snprintf(buf+len, buf_len-len, "vndr_oui:%d\n", spin_is_locked(&cfg->vndr_oui_sync));
	CHECK_AND_INCR_LEN(ret, len, buf_len);

	ret = snprintf(buf+len, buf_len-len, "net_list:%d\n", spin_is_locked(&cfg->net_list_sync));
	CHECK_AND_INCR_LEN(ret, len, buf_len);

	ret = snprintf(buf+len, buf_len-len, "eq_lock:%d\n", spin_is_locked(&cfg->eq_lock));
	CHECK_AND_INCR_LEN(ret, len, buf_len);

#ifdef WL_WPS_SYNC
	ret = snprintf(buf+len, buf_len-len, "wps:%d\n", spin_is_locked(&cfg->wps_sync));
	CHECK_AND_INCR_LEN(ret, len, buf_len);
#endif /* WL_WPS_SYNC */
	ret = snprintf(buf+len, buf_len-len, "eidx.in_progress:0x%x eidx.event:0x%x",
			cfg->eidx.in_progress, cfg->eidx.event_type);
	CHECK_AND_INCR_LEN(ret, len, buf_len);
	return len;
}

#ifdef WL_CLIENT_SAE
static bool
wl_is_pmkid_available(struct net_device *dev, const u8 *bssid)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	int i;

	/* check the bssid is null or not */
	if (!bssid) return FALSE;

	for (i = 0; i < cfg->pmk_list->pmkids.count; i++) {
		if (!memcmp(bssid, &cfg->pmk_list->pmkids.pmkid[i].bssid,
			ETHER_ADDR_LEN)) {
			return TRUE;
		}
	}
	return FALSE;
}

#define WL_AUTH_START_EVT_V0 0
static s32
wl_notify_start_auth(struct bcm_cfg80211 *cfg,
	bcm_struct_cfgdev *cfgdev, const wl_event_msg_t *e, void *data)
{
	struct cfg80211_external_auth_params ext_auth_param;
	struct net_device *ndev = cfgdev_to_wlc_ndev(cfgdev, cfg);
	u32 datalen = be32_to_cpu(e->datalen);
#ifdef WL_AUTH_START_EVT_V0
	wl_ext_auth_evt_t *evt_v0_data = (wl_ext_auth_evt_t *)data;
#else
	wl_auth_start_evt_t *evt_v1_data = (wl_auth_start_evt_t *)data;
#endif
	wl_assoc_mgr_cmd_t cmd;
	struct wireless_dev *wdev = ndev->ieee80211_ptr;
	int err, retry = 3;

	WL_DBG(("Enter\n"));

	if (!datalen || !data) {
		WL_ERR(("Invalid data for auth start event\n"));
		return BCME_ERROR;
	}

	ext_auth_param.action = NL80211_EXTERNAL_AUTH_START;
	ext_auth_param.key_mgmt_suite = ntoh32(WLAN_AKM_SUITE_SAE_SHA256);
#ifdef WL_AUTH_START_EVT_V0
	(void)memcpy_s(&ext_auth_param.bssid, ETHER_ADDR_LEN, &evt_v0_data->bssid, ETHER_ADDR_LEN);
	ext_auth_param.ssid.ssid_len = MIN(evt_v0_data->ssid.SSID_len, DOT11_MAX_SSID_LEN);
	if (ext_auth_param.ssid.ssid_len) {
		(void)memcpy_s(&ext_auth_param.ssid.ssid, ext_auth_param.ssid.ssid_len,
			evt_v0_data->ssid.SSID, ext_auth_param.ssid.ssid_len);
	}
	WL_MSG(ndev->name, "BSSID "MACDBG"\n", MAC2STRDBG(&evt_v0_data->bssid));
#else
	(void)memcpy_s(&ext_auth_param.bssid, ETHER_ADDR_LEN, &evt_v1_data->bssid, ETHER_ADDR_LEN);
	ext_auth_param.ssid.ssid_len = MIN(evt_v1_data->ssid.SSID_len, DOT11_MAX_SSID_LEN);
	if (ext_auth_param.ssid.ssid_len) {
		(void)memcpy_s(&ext_auth_param.ssid.ssid, ext_auth_param.ssid.ssid_len,
			evt_v1_data->ssid.SSID, ext_auth_param.ssid.ssid_len);
	}
	WL_MSG(ndev->name, "BSSID "MACDBG", version=%d\n",
		MAC2STRDBG(&evt_v1_data->bssid), evt_v1_data->version);
#endif

	/* Wait for conn_owner_nlportid been assigned in nl80211_connect */
	for (retry = 3; retry > 0; retry--) {
		if (wdev->conn_owner_nlportid) {
			break;
		}
		wl_delay(10);
	}

	err = cfg80211_external_auth_request(ndev, &ext_auth_param, GFP_KERNEL);
	if (err) {
		WL_ERR(("Send external auth request failed, ret %d\n", err));
		err = wldev_ioctl_set(ndev, WLC_DISASSOC, NULL, 0);
		if (err < 0) {
			WL_ERR(("WLC_DISASSOC error %d\n", err));
		}
		return BCME_ERROR;
	}

	cmd.version = WL_ASSOC_MGR_CURRENT_VERSION;
	cmd.length = sizeof(cmd);
	cmd.cmd = WL_ASSOC_MGR_CMD_PAUSE_ON_EVT;
	cmd.params = WL_ASSOC_MGR_PARAMS_PAUSE_EVENT_AUTH_RESP;
	err = wldev_iovar_setbuf(ndev, "assoc_mgr_cmd", (void *)&cmd, sizeof(cmd), cfg->ioctl_buf,
		WLC_IOCTL_SMLEN, &cfg->ioctl_buf_sync);
	if (unlikely(err)) {
		WL_ERR(("Failed to pause assoc(%d)\n", err));
	}

	return BCME_OK;
}

s32
wl_handle_auth_event(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	const wl_event_msg_t *e, void *data)
{
	bcm_struct_cfgdev *cfgdev = ndev_to_cfgdev(ndev);
	u8 bsscfgidx = e->bsscfgidx;
	u8 *mgmt_frame = NULL;
	u8 *body = NULL;
	u32 body_len = 0;
	s32 chan;
	chanspec_t chanspec;
	s32 freq;
	struct ether_addr da;
	struct ether_addr bssid;
	u32 len = ntoh32(e->datalen);
	u32 status = ntoh32(e->status);
	int err = BCME_OK;

	if (wl_get_mode_by_netdev(cfg, ndev) == WL_INVALID) {
		return WL_INVALID;
	}

	if (!len) {
		WL_ERR(("WLC_E_AUTH has no payload. status %d reason %d\n",
			status, ntoh32(e->reason)));
#ifdef WL_EXT_IAPSTA
		if (status != WLC_E_STATUS_SUCCESS)
			wl_ext_in4way_sync(ndev, STA_NO_BTC_IN4WAY, WL_EXT_STATUS_DISCONNECTED, NULL);
#endif
		return WL_INVALID;
	}

	body = (u8 *)MALLOCZ(cfg->osh, len);
	if (body == NULL) {
		WL_ERR(("Failed to allocate body\n"));
		return WL_INVALID;
	}
	(void)memcpy_s(body, len, data, len);

	err = wldev_iovar_getbuf_bsscfg(ndev, "cur_etheraddr",
		NULL, 0, cfg->ioctl_buf, WLC_IOCTL_SMLEN, bsscfgidx, &cfg->ioctl_buf_sync);
	if (unlikely(err)) {
		MFREE(cfg->osh, body, len);
		WL_ERR(("Could not get cur_etheraddr %d\n", err));
		return err;
	}
	(void)memcpy_s(da.octet, ETHER_ADDR_LEN, cfg->ioctl_buf, ETHER_ADDR_LEN);

	bzero(&bssid, sizeof(bssid));
	err = wldev_ioctl_get(ndev, WLC_GET_BSSID, &bssid, ETHER_ADDR_LEN);
	/* Use e->addr as bssid for Sta case , before association completed */
	if (err == BCME_NOTASSOCIATED) {
		(void)memcpy_s(&bssid, ETHER_ADDR_LEN, &e->addr, ETHER_ADDR_LEN);
		err = BCME_OK;
	}
	if (unlikely(err)) {
		MFREE(cfg->osh, body, len);
		WL_ERR(("Could not get bssid %d\n", err));
		return err;
	}

	err = wldev_iovar_getint(ndev, "chanspec", &chan);
	if (unlikely(err)) {
		MFREE(cfg->osh, body, len);
		WL_ERR(("Could not get chanspec %d\n", err));
		return err;
	}

	chanspec = wl_chspec_driver_to_host(chan);
	freq = wl_channel_to_frequency(wf_chspec_ctlchan(chanspec), CHSPEC_BAND(chanspec));

	body_len = len;
	err = wl_frame_get_mgmt(cfg, FC_AUTH, &da, &e->addr, &bssid,
		&mgmt_frame, &len, body);
	if (!err) {
#ifdef WL_EXT_IAPSTA
		wl_ext_update_extsae_4way(ndev, (struct ieee80211_mgmt *)mgmt_frame, FALSE);
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0))
		cfg80211_rx_mgmt(cfgdev, freq, 0, mgmt_frame, len, 0);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0))
		cfg80211_rx_mgmt(cfgdev, freq, 0, mgmt_frame, len, 0, GFP_ATOMIC);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0))
		cfg80211_rx_mgmt(cfgdev, freq, 0, mgmt_frame, len, GFP_ATOMIC);
#else
		cfg80211_rx_mgmt(ndev, freq, mgmt_frame, len, GFP_ATOMIC);
#endif
		MFREE(cfg->osh, mgmt_frame, len);
	}

	if (body) {
		MFREE(cfg->osh, body, body_len);
	}

	return BCME_OK;
}

/** Called by the cfg80211 framework */
static s32
wl_cfg80211_external_auth(struct wiphy *wiphy,
	struct net_device *ndev, struct cfg80211_external_auth_params *ext_auth_param)
{
	int err = 0;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	wl_assoc_mgr_cmd_t cmd;

	WL_DBG(("Enter\n"));

	if (!ext_auth_param ||
		ETHER_ISNULLADDR(ext_auth_param->bssid)) {
		WL_ERR(("Invalid param\n"));
		return -EINVAL;
	}

	cmd.version = WL_ASSOC_MGR_CURRENT_VERSION;
	cmd.length = sizeof(cmd);
	cmd.cmd = WL_ASSOC_MGR_CMD_PAUSE_ON_EVT;
	cmd.params = WL_ASSOC_MGR_PARAMS_EVENT_NONE;
	err = wldev_iovar_setbuf(ndev, "assoc_mgr_cmd", (void *)&cmd, sizeof(cmd),
		cfg->ioctl_buf, WLC_IOCTL_SMLEN, &cfg->ioctl_buf_sync);
	if (unlikely(err)) {
		WL_ERR(("Failed to pause assoc(%d)\n", err));
	}

	return err;
}

static s32
wl_cfg80211_mgmt_auth_tx(struct net_device *dev, bcm_struct_cfgdev *cfgdev,
	struct bcm_cfg80211 *cfg, const u8 *buf, size_t len, s32 bssidx, u64 *cookie)
{
	int err = 0;
	wl_assoc_mgr_cmd_t *cmd;
	char *ambuf = NULL;
	int param_len;
	bool ack = true;

	param_len = sizeof(wl_assoc_mgr_cmd_t) + len;
	ambuf = MALLOCZ(cfg->osh, param_len);
	if (ambuf == NULL) {
		WL_ERR(("unable to allocate frame\n"));
		return -ENOMEM;
	}

	cmd = (wl_assoc_mgr_cmd_t*)ambuf;
	cmd->version = WL_ASSOC_MGR_CURRENT_VERSION;
	cmd->length = len;
	cmd->cmd = WL_ASSOC_MGR_CMD_SEND_AUTH;
	err = memcpy_s(&cmd->params, len, buf, len);
	if (err) {
		WL_ERR(("Failed to copy cmd params(%d)\n", err));
		ack = false;
	} else {
		err = wldev_iovar_setbuf(dev, "assoc_mgr_cmd", ambuf, param_len,
			cfg->ioctl_buf, WLC_IOCTL_MEDLEN, &cfg->ioctl_buf_sync);
		if (unlikely(err)) {
			WL_ERR(("Failed to send auth(%d)\n", err));
			ack = false;
		}
#ifdef WL_EXT_IAPSTA
		else {
			const struct ieee80211_mgmt *mgmt = (const struct ieee80211_mgmt *)buf;
			wl_ext_update_extsae_4way(dev, mgmt, TRUE);
		}
#endif
	}

	MFREE(cfg->osh, ambuf, param_len);

	cfg80211_mgmt_tx_status(cfgdev, *cookie, buf, len, ack, GFP_KERNEL);
	return BCME_OK;
}
#endif /* WL_CLIENT_SAE */

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
		WL_TRACE(("command result is %s\n", command));
		ret = bytes_written;
	}

	return ret;
}

#ifdef WL_STATIC_IF
bool
wl_cfg80211_static_if(struct bcm_cfg80211 *cfg, struct net_device *ndev)
{
	int i;

	if (!cfg)
		return FALSE;

	for (i=0; i<DHD_MAX_STATIC_IFS; i++) {
		if (cfg->static_ndev[i] == ndev)
			return TRUE;
	}

	return FALSE;
}

int
wl_cfg80211_static_ifidx(struct bcm_cfg80211 *cfg, struct net_device *ndev)
{
	int i;

	if (!cfg)
		return -1;

	for (i=0; i<DHD_MAX_STATIC_IFS; i++) {
		if (cfg->static_ndev[i] == ndev)
			return i;
	}

	return -1;
}

struct net_device *
wl_cfg80211_static_if_active(struct bcm_cfg80211 *cfg)
{
	int i;

	if (!cfg)
		return NULL;

	for (i=0; i<DHD_MAX_STATIC_IFS; i++) {
		if (cfg->static_ndev[i] && (cfg->static_ndev_state[i] & NDEV_STATE_FW_IF_CREATED))
			return cfg->static_ndev[i];
	}

	return NULL;
}

int
wl_cfg80211_static_if_name(struct bcm_cfg80211 *cfg, const char *name)
{
	int i;

	if (!cfg)
		return -1;

	for (i=0; i<DHD_MAX_STATIC_IFS; i++) {
		if (cfg->static_ndev[i] && (!strncmp(cfg->static_ndev[i]->name, name, strlen(name))))
			return i;
	}

	return -1;
}

void
wl_cfg80211_static_if_dev_close(struct net_device *dev)
{
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	int i;

	if (!cfg)
		return;

	for (i=0; i<DHD_MAX_STATIC_IFS; i++) {
		if (cfg->static_ndev[i] && (cfg->static_ndev[i]->flags & IFF_UP))
			dev_close(cfg->static_ndev[i]);
	}

	return;
}
#endif /* WL_STATIC_IF */
