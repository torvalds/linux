/* SPDX-License-Identifier: GPL-2.0 */
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
 * $Id: wl_android.h 607319 2015-12-18 14:16:55Z $
 */

#ifndef _wl_android_
#define _wl_android_

#include <linux/module.h>
#include <linux/netdevice.h>
#include <wldev_common.h>
#include <dngl_stats.h>
#include <dhd.h>

/* If any feature uses the Generic Netlink Interface, put it here to enable WL_GENL
 * automatically
 */
#if defined(BT_WIFI_HANDOVER)
#define WL_GENL
#endif



typedef struct _android_wifi_priv_cmd {
    char *buf;
    int used_len;
    int total_len;
} android_wifi_priv_cmd;

#ifdef CONFIG_COMPAT
typedef struct _compat_android_wifi_priv_cmd {
    compat_caddr_t buf;
    int used_len;
    int total_len;
} compat_android_wifi_priv_cmd;
#endif /* CONFIG_COMPAT */

/**
 * Android platform dependent functions, feel free to add Android specific functions here
 * (save the macros in dhd). Please do NOT declare functions that are NOT exposed to dhd
 * or cfg, define them as static in wl_android.c
 */

/* message levels */
#define ANDROID_ERROR_LEVEL	(1 << 0)
#define ANDROID_TRACE_LEVEL	(1 << 1)
#define ANDROID_INFO_LEVEL	(1 << 2)
#define ANDROID_SCAN_LEVEL	(1 << 3)
#define ANDROID_DBG_LEVEL	(1 << 4)
#define ANDROID_MSG_LEVEL	(1 << 0)

#define WL_MSG(name, arg1, args...) \
	do { \
		if (android_msg_level & ANDROID_MSG_LEVEL) { \
			printk(KERN_ERR "[dhd-%s] %s : " arg1, name, __func__, ## args); \
		} \
	} while (0)

#define WL_MSG_PRINT_RATE_LIMIT_PERIOD 1000000000u /* 1s in units of ns */
#define WL_MSG_RLMT(name, cmp, size, arg1, args...) \
do {	\
	if (android_msg_level & ANDROID_MSG_LEVEL) {	\
		static uint64 __err_ts = 0; \
		static uint32 __err_cnt = 0; \
		uint64 __cur_ts = 0; \
		static uint8 static_tmp[size]; \
		__cur_ts = local_clock(); \
		if (__err_ts == 0 || (__cur_ts > __err_ts && \
		(__cur_ts - __err_ts > WL_MSG_PRINT_RATE_LIMIT_PERIOD)) || \
		memcmp(&static_tmp, cmp, size)) { \
			__err_ts = __cur_ts; \
			memcpy(static_tmp, cmp, size); \
			printk(KERN_ERR "[dhd-%s] %s : [%u times] " arg1, \
				name, __func__, __err_cnt, ## args); \
			__err_cnt = 0; \
		} else { \
			++__err_cnt; \
		} \
	}	\
} while (0)

/**
 * wl_android_init will be called from module init function (dhd_module_init now), similarly
 * wl_android_exit will be called from module exit function (dhd_module_cleanup now)
 */
int wl_android_init(void);
int wl_android_exit(void);
void wl_android_post_init(void);
int wl_android_wifi_on(struct net_device *dev);
int wl_android_wifi_off(struct net_device *dev, bool on_failure);
int wl_android_priv_cmd(struct net_device *net, struct ifreq *ifr, int cmd);
int wl_handle_private_cmd(struct net_device *net, char *command, u32 cmd_len);

s32 wl_netlink_send_msg(int pid, int type, int seq, const void *data, size_t size);
#ifdef WL_EXT_IAPSTA
int wl_ext_iapsta_attach_netdev(struct net_device *net, int ifidx, uint8 bssidx);
int wl_ext_iapsta_attach_name(struct net_device *net, int ifidx);
int wl_ext_iapsta_dettach_netdev(struct net_device *net, int ifidx);
int wl_ext_iapsta_update_net_device(struct net_device *net, int ifidx);
#ifdef PROPTX_MAXCOUNT
void wl_ext_update_wlfc_maxcount(struct dhd_pub *dhd);
int wl_ext_get_wlfc_maxcount(struct dhd_pub *dhd, int ifidx);
#endif /* PROPTX_MAXCOUNT */
int wl_ext_iapsta_alive_preinit(struct net_device *dev);
int wl_ext_iapsta_alive_postinit(struct net_device *dev);
int wl_ext_iapsta_attach(dhd_pub_t *pub);
void wl_ext_iapsta_dettach(dhd_pub_t *pub);
#ifdef WL_CFG80211
u32 wl_ext_iapsta_update_channel(dhd_pub_t *dhd, struct net_device *dev, u32 channel);
void wl_ext_iapsta_update_iftype(struct net_device *net, int ifidx, int wl_iftype);
bool wl_ext_iapsta_iftype_enabled(struct net_device *net, int wl_iftype);
void wl_ext_iapsta_ifadding(struct net_device *net, int ifidx);
bool wl_ext_iapsta_mesh_creating(struct net_device *net);
#endif
extern int op_mode;
#endif
typedef struct bcol_gtk_para {
	int enable;
	int ptk_len;
	char ptk[64];
	char replay[8];
} bcol_gtk_para_t;
#define ACS_FW_BIT		(1<<0)
#define ACS_DRV_BIT		(1<<1)
#if defined(WL_EXT_IAPSTA) || defined(USE_IW)
typedef enum WL_EVENT_PRIO {
	PRIO_EVENT_IAPSTA,
	PRIO_EVENT_ESCAN,
	PRIO_EVENT_WEXT
}wl_event_prio_t;
s32 wl_ext_event_attach(struct net_device *dev, dhd_pub_t *dhdp);
void wl_ext_event_dettach(dhd_pub_t *dhdp);
int wl_ext_event_attach_netdev(struct net_device *net, int ifidx, uint8 bssidx);
int wl_ext_event_dettach_netdev(struct net_device *net, int ifidx);
int wl_ext_event_register(struct net_device *dev, dhd_pub_t *dhd,
	uint32 event, void *cb_func, void *data, wl_event_prio_t prio);
void wl_ext_event_deregister(struct net_device *dev, dhd_pub_t *dhd,
	uint32 event, void *cb_func);
void wl_ext_event_send(void *params, const wl_event_msg_t * e, void *data);
#endif
int wl_ext_autochannel(struct net_device *dev, uint acs, uint32 band);
int wl_android_ext_priv_cmd(struct net_device *net, char *command, int total_len,
	int *bytes_written);
void wl_ext_get_sec(struct net_device *dev, int ifmode, char *sec, int total_len);
bool wl_ext_check_scan(struct net_device *dev, dhd_pub_t *dhdp);
#if defined(WL_CFG80211) || defined(WL_ESCAN)
void wl_ext_user_sync(struct dhd_pub *dhd, int ifidx, bool lock);
bool wl_ext_event_complete(struct dhd_pub *dhd, int ifidx);
#endif
#if defined(WL_CFG80211)
void wl_ext_bss_iovar_war(struct net_device *dev, s32 *val);
#endif
enum wl_ext_status {
	WL_EXT_STATUS_DISCONNECTING = 0,
	WL_EXT_STATUS_DISCONNECTED,
	WL_EXT_STATUS_SCAN,
	WL_EXT_STATUS_CONNECTING,
	WL_EXT_STATUS_CONNECTED,
	WL_EXT_STATUS_ADD_KEY,
	WL_EXT_STATUS_AP_ENABLED,
	WL_EXT_STATUS_DELETE_STA,
	WL_EXT_STATUS_STA_DISCONNECTED,
	WL_EXT_STATUS_STA_CONNECTED,
	WL_EXT_STATUS_AP_DISABLED
};
#if defined(WL_EXT_IAPSTA) && defined(WL_CFG80211)
int wl_ext_in4way_sync(struct net_device *dev, uint action,
	enum wl_ext_status status, void *context);
#endif /* WL_EXT_IAPSTA && WL_CFG80211 */
#if defined(WL_EXT_IAPSTA) && defined(WL_WIRELESS_EXT)
int wl_ext_in4way_sync_wext(struct net_device *dev, uint action,
	enum wl_ext_status status, void *context);
#endif /* WL_EXT_IAPSTA && WL_WIRELESS_EXT */
#if defined(WL_EXT_IAPSTA)
void wl_ext_update_eapol_status(dhd_pub_t *dhd, int ifidx,
	uint eapol_status);
#else
static INLINE void wl_ext_update_eapol_status(dhd_pub_t *dhd, int ifidx,
	uint eapol_status) { }
#endif /* WL_EXT_IAPSTA */

typedef struct wl_conn_info {
	uint8 bssidx;
	wlc_ssid_t ssid;
	struct ether_addr bssid;
	uint16 channel;
} wl_conn_info_t;
#if defined(WL_WIRELESS_EXT)
s32 wl_ext_connect(struct net_device *dev, wl_conn_info_t *conn_info);
#endif /* defined(WL_WIRELESS_EXT) */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0))
#define strnicmp(str1, str2, len) strncasecmp((str1), (str2), (len))
#endif

/* hostap mac mode */
#define MACLIST_MODE_DISABLED   0
#define MACLIST_MODE_DENY       1
#define MACLIST_MODE_ALLOW      2

/* max number of assoc list */
#define MAX_NUM_OF_ASSOCLIST    64

/* Bandwidth */
#define WL_CH_BANDWIDTH_20MHZ 20
#define WL_CH_BANDWIDTH_40MHZ 40
#define WL_CH_BANDWIDTH_80MHZ 80
/* max number of mac filter list
 * restrict max number to 10 as maximum cmd string size is 255
 */
#define MAX_NUM_MAC_FILT        10
#define	WL_GET_BAND(ch)	(((uint)(ch) <= CH_MAX_2G_CHANNEL) ?	\
	WLC_BAND_2G : WLC_BAND_5G)

int wl_android_set_ap_mac_list(struct net_device *dev, int macmode, struct maclist *maclist);

/* terence:
 * BSSCACHE: Cache bss list
 * RSSAVG: Average RSSI of BSS list
 * RSSIOFFSET: RSSI offset
 * SORT_BSS_BY_RSSI: Sort BSS by RSSI
 */
//#define BSSCACHE
//#define RSSIAVG
//#define RSSIOFFSET
//#define RSSIOFFSET_NEW
//#define SORT_BSS_BY_RSSI

#define RSSI_MAXVAL -2
#define RSSI_MINVAL -200

#if defined(ESCAN_RESULT_PATCH)
#define REPEATED_SCAN_RESULT_CNT	2
#else
#define REPEATED_SCAN_RESULT_CNT	1
#endif

#if defined(RSSIAVG) || defined(RSSIOFFSET)
extern int g_wifi_on;
#endif

#if defined(RSSIAVG)
#define RSSIAVG_LEN (4*REPEATED_SCAN_RESULT_CNT)
#define RSSICACHE_TIMEOUT 15

typedef struct wl_rssi_cache {
	struct wl_rssi_cache *next;
	int dirty;
	struct osl_timespec tv;
	struct ether_addr BSSID;
	int16 RSSI[RSSIAVG_LEN];
} wl_rssi_cache_t;

typedef struct wl_rssi_cache_ctrl {
	wl_rssi_cache_t *m_cache_head;
} wl_rssi_cache_ctrl_t;

void wl_free_rssi_cache(wl_rssi_cache_ctrl_t *rssi_cache_ctrl);
void wl_delete_dirty_rssi_cache(wl_rssi_cache_ctrl_t *rssi_cache_ctrl);
void wl_delete_disconnected_rssi_cache(wl_rssi_cache_ctrl_t *rssi_cache_ctrl, u8 *bssid);
void wl_reset_rssi_cache(wl_rssi_cache_ctrl_t *rssi_cache_ctrl);
void wl_update_rssi_cache(wl_rssi_cache_ctrl_t *rssi_cache_ctrl, wl_scan_results_t *ss_list);
int wl_update_connected_rssi_cache(struct net_device *net, wl_rssi_cache_ctrl_t *rssi_cache_ctrl, int *rssi_avg);
int16 wl_get_avg_rssi(wl_rssi_cache_ctrl_t *rssi_cache_ctrl, void *addr);
#endif

#if defined(RSSIOFFSET)
#define RSSI_OFFSET	5
#if defined(RSSIOFFSET_NEW)
#define RSSI_OFFSET_MAXVAL -80
#define RSSI_OFFSET_MINVAL -94
#define RSSI_OFFSET_INTVAL ((RSSI_OFFSET_MAXVAL-RSSI_OFFSET_MINVAL)/RSSI_OFFSET)
#endif
#define BCM4330_CHIP_ID		0x4330
#define BCM4330B2_CHIP_REV      4
int wl_update_rssi_offset(struct net_device *net, int rssi);
#endif

#if defined(BSSCACHE)
#define BSSCACHE_TIMEOUT	15

typedef struct wl_bss_cache {
	struct wl_bss_cache *next;
	int dirty;
	struct osl_timespec tv;
	wl_scan_results_t results;
} wl_bss_cache_t;

typedef struct wl_bss_cache_ctrl {
	wl_bss_cache_t *m_cache_head;
} wl_bss_cache_ctrl_t;

void wl_free_bss_cache(wl_bss_cache_ctrl_t *bss_cache_ctrl);
void wl_delete_dirty_bss_cache(wl_bss_cache_ctrl_t *bss_cache_ctrl);
void wl_delete_disconnected_bss_cache(wl_bss_cache_ctrl_t *bss_cache_ctrl, u8 *bssid);
void wl_reset_bss_cache(wl_bss_cache_ctrl_t *bss_cache_ctrl);
void wl_update_bss_cache(wl_bss_cache_ctrl_t *bss_cache_ctrl,
#if defined(RSSIAVG)
	wl_rssi_cache_ctrl_t *rssi_cache_ctrl,
#endif
	wl_scan_results_t *ss_list);
void wl_release_bss_cache_ctrl(wl_bss_cache_ctrl_t *bss_cache_ctrl);
#endif
int wl_ext_get_best_channel(struct net_device *net,
#if defined(BSSCACHE)
	wl_bss_cache_ctrl_t *bss_cache_ctrl,
#else
	struct wl_scan_results *bss_list,
#endif
	int ioctl_ver, int *best_2g_ch, int *best_5g_ch
);
#endif /* _wl_android_ */
