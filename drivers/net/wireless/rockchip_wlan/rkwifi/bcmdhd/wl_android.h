/*
 * Linux cfg80211 driver - Android related functions
 *
 * $Copyright Open Broadcom Corporation$
 *
 * $Id: wl_android.h 487838 2014-06-27 05:51:44Z $
 */

#ifndef _wl_android_
#define _wl_android_

#include <linux/module.h>
#include <linux/netdevice.h>
#include <wldev_common.h>

/* If any feature uses the Generic Netlink Interface, put it here to enable WL_GENL
 * automatically
 */
#if defined(WL_SDO) || defined(BT_WIFI_HANDOVER) || defined(WL_NAN)
#define WL_GENL
#endif


#ifdef WL_GENL
#include <net/genetlink.h>
#endif

/**
 * Android platform dependent functions, feel free to add Android specific functions here
 * (save the macros in dhd). Please do NOT declare functions that are NOT exposed to dhd
 * or cfg, define them as static in wl_android.c
 */

/**
 * wl_android_init will be called from module init function (dhd_module_init now), similarly
 * wl_android_exit will be called from module exit function (dhd_module_cleanup now)
 */
int wl_android_init(void);
int wl_android_exit(void);
void wl_android_post_init(void);
int wl_android_wifi_on(struct net_device *dev);
int wl_android_wifi_off(struct net_device *dev);
int wl_android_priv_cmd(struct net_device *net, struct ifreq *ifr, int cmd);

#ifdef WL_GENL
typedef struct bcm_event_hdr {
	u16 event_type;
	u16 len;
} bcm_event_hdr_t;

/* attributes (variables): the index in this enum is used as a reference for the type,
 *             userspace application has to indicate the corresponding type
 *             the policy is used for security considerations
 */
enum {
	BCM_GENL_ATTR_UNSPEC,
	BCM_GENL_ATTR_STRING,
	BCM_GENL_ATTR_MSG,
	__BCM_GENL_ATTR_MAX
};
#define BCM_GENL_ATTR_MAX (__BCM_GENL_ATTR_MAX - 1)

/* commands: enumeration of all commands (functions),
 * used by userspace application to identify command to be ececuted
 */
enum {
	BCM_GENL_CMD_UNSPEC,
	BCM_GENL_CMD_MSG,
	__BCM_GENL_CMD_MAX
};
#define BCM_GENL_CMD_MAX (__BCM_GENL_CMD_MAX - 1)

/* Enum values used by the BCM supplicant to identify the events */
enum {
	BCM_E_UNSPEC,
	BCM_E_SVC_FOUND,
	BCM_E_DEV_FOUND,
	BCM_E_DEV_LOST,
	BCM_E_DEV_BT_WIFI_HO_REQ,
	BCM_E_MAX
};

s32 wl_genl_send_msg(struct net_device *ndev, u32 event_type,
	u8 *string, u16 len, u8 *hdr, u16 hdrlen);
#endif /* WL_GENL */
s32 wl_netlink_send_msg(int pid, int type, int seq, void *data, size_t size);

/* hostap mac mode */
#define MACLIST_MODE_DISABLED   0
#define MACLIST_MODE_DENY       1
#define MACLIST_MODE_ALLOW      2

/* max number of assoc list */
#define MAX_NUM_OF_ASSOCLIST    64

/* max number of mac filter list
 * restrict max number to 10 as maximum cmd string size is 255
 */
#define MAX_NUM_MAC_FILT        10

int wl_android_set_ap_mac_list(struct net_device *dev, int macmode, struct maclist *maclist);

/* terence:
 * BSSCACHE: Cache bss list
 * RSSAVG: Average RSSI of BSS list
 * RSSIOFFSET: RSSI offset
 */
//#define BSSCACHE
//#define RSSIAVG
//#define RSSIOFFSET
//#define RSSIOFFSET_NEW

#define RSSI_MAXVAL -2
#define RSSI_MINVAL -200

#if defined(ESCAN_RESULT_PATCH)
#define REPEATED_SCAN_RESULT_CNT	2
#else
#define REPEATED_SCAN_RESULT_CNT	1
#endif

#if defined(RSSIAVG)
#define RSSIAVG_LEN (4*REPEATED_SCAN_RESULT_CNT)
#define RSSICACHE_TIMEOUT 15

typedef struct wl_rssi_cache {
	struct wl_rssi_cache *next;
	int dirty;
	struct timeval tv;
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
	struct timeval tv;
	wl_scan_results_t results;
} wl_bss_cache_t;

typedef struct wl_bss_cache_ctrl {
	wl_bss_cache_t *m_cache_head;
} wl_bss_cache_ctrl_t;

void wl_free_bss_cache(wl_bss_cache_ctrl_t *bss_cache_ctrl);
void wl_delete_dirty_bss_cache(wl_bss_cache_ctrl_t *bss_cache_ctrl);
void wl_delete_disconnected_bss_cache(wl_bss_cache_ctrl_t *bss_cache_ctrl, u8 *bssid);
void wl_reset_bss_cache(wl_bss_cache_ctrl_t *bss_cache_ctrl);
void wl_update_bss_cache(wl_bss_cache_ctrl_t *bss_cache_ctrl, wl_scan_results_t *ss_list);
void wl_release_bss_cache_ctrl(wl_bss_cache_ctrl_t *bss_cache_ctrl);
#endif
#endif /* _wl_android_ */