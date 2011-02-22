/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _wl_cfg80211_h_
#define _wl_cfg80211_h_

#include <linux/wireless.h>
#include <linux/wireless.h>
#include <net/cfg80211.h>
#include <proto/ethernet.h>
#include <wlioctl.h>

struct wl_conf;
struct wl_iface;
struct wl_priv;
struct wl_security;
struct wl_ibss;

#if defined(IL_BIGENDIAN)
#include <bcmendian.h>
#define htod32(i) (bcmswap32(i))
#define htod16(i) (bcmswap16(i))
#define dtoh32(i) (bcmswap32(i))
#define dtoh16(i) (bcmswap16(i))
#define htodchanspec(i) htod16(i)
#define dtohchanspec(i) dtoh16(i)
#else
#define htod32(i) i
#define htod16(i) i
#define dtoh32(i) i
#define dtoh16(i) i
#define htodchanspec(i) i
#define dtohchanspec(i) i
#endif

#define WL_DBG_NONE	0
#define WL_DBG_DBG 	(1 << 2)
#define WL_DBG_INFO	(1 << 1)
#define WL_DBG_ERR	(1 << 0)
#define WL_DBG_MASK ((WL_DBG_DBG | WL_DBG_INFO | WL_DBG_ERR) << 1)

#define WL_DBG_LEVEL 1		/* 0 invalidates all debug messages.
				 default is 1 */
#define	WL_ERR(fmt, args...)					\
do {								\
	if (wl_dbg_level & WL_DBG_ERR) {			\
		if (net_ratelimit()) {				\
			printk(KERN_ERR "ERROR @%s : " fmt,	\
			       __func__, ##args);		\
		}						\
	}							\
} while (0)

#define	WL_INFO(fmt, args...)					\
do {								\
	if (wl_dbg_level & WL_DBG_INFO) {			\
		if (net_ratelimit()) {				\
			printk(KERN_ERR "INFO @%s : " fmt,	\
			       __func__, ##args);		\
		}						\
	}							\
} while (0)

#if (WL_DBG_LEVEL > 0)
#define	WL_DBG(fmt, args...)					\
do {								\
	if (wl_dbg_level & WL_DBG_DBG) {			\
		printk(KERN_ERR "DEBUG @%s :" fmt,		\
		       __func__, ##args);			\
	}							\
} while (0)
#else				/* !(WL_DBG_LEVEL > 0) */
#define	WL_DBG(fmt, args...) noprintk(fmt, ##args)
#endif				/* (WL_DBG_LEVEL > 0) */

#define WL_SCAN_RETRY_MAX	3	/* used for ibss scan */
#define WL_NUM_SCAN_MAX		1
#define WL_NUM_PMKIDS_MAX	MAXPMKID	/* will be used
						 * for 2.6.33 kernel
						 * or later
						 */
#define WL_SCAN_BUF_MAX 		(1024 * 8)
#define WL_TLV_INFO_MAX 		1024
#define WL_BSS_INFO_MAX			2048
#define WL_ASSOC_INFO_MAX	512	/*
				 * needs to grab assoc info from dongle to
				 * report it to cfg80211 through "connect"
				 * event
				 */
#define WL_IOCTL_LEN_MAX	1024
#define WL_EXTRA_BUF_MAX	2048
#define WL_ISCAN_BUF_MAX	2048	/*
				 * the buf lengh can be WLC_IOCTL_MAXLEN (8K)
				 * to reduce iteration
				 */
#define WL_ISCAN_TIMER_INTERVAL_MS	3000
#define WL_SCAN_ERSULTS_LAST 	(WL_SCAN_RESULTS_NO_MEM+1)
#define WL_AP_MAX	256	/* virtually unlimitted as long
				 * as kernel memory allows
				 */
#define WL_FILE_NAME_MAX		256

/* dongle status */
enum wl_status {
	WL_STATUS_READY,
	WL_STATUS_SCANNING,
	WL_STATUS_SCAN_ABORTING,
	WL_STATUS_CONNECTING,
	WL_STATUS_CONNECTED
};

/* wi-fi mode */
enum wl_mode {
	WL_MODE_BSS,
	WL_MODE_IBSS,
	WL_MODE_AP
};

/* dongle profile list */
enum wl_prof_list {
	WL_PROF_MODE,
	WL_PROF_SSID,
	WL_PROF_SEC,
	WL_PROF_IBSS,
	WL_PROF_BAND,
	WL_PROF_BSSID,
	WL_PROF_ACT,
	WL_PROF_BEACONINT,
	WL_PROF_DTIMPERIOD
};

/* dongle iscan state */
enum wl_iscan_state {
	WL_ISCAN_STATE_IDLE,
	WL_ISCAN_STATE_SCANING
};

/* fw downloading status */
enum wl_fw_status {
	WL_FW_LOADING_DONE,
	WL_NVRAM_LOADING_DONE
};

/* beacon / probe_response */
struct beacon_proberesp {
	__le64 timestamp;
	__le16 beacon_int;
	__le16 capab_info;
	u8 variable[0];
} __attribute__ ((packed));

/* dongle configuration */
struct wl_conf {
	u32 mode;		/* adhoc , infrastructure or ap */
	u32 frag_threshold;
	u32 rts_threshold;
	u32 retry_short;
	u32 retry_long;
	s32 tx_power;
	struct ieee80211_channel channel;
};

/* cfg80211 main event loop */
struct wl_event_loop {
	s32(*handler[WLC_E_LAST]) (struct wl_priv *wl,
				     struct net_device *ndev,
				     const wl_event_msg_t *e, void *data);
};

/* representing interface of cfg80211 plane */
struct wl_iface {
	struct wl_priv *wl;
};

struct wl_dev {
	void *driver_data;	/* to store cfg80211 object information */
};

/* bss inform structure for cfg80211 interface */
struct wl_cfg80211_bss_info {
	u16 band;
	u16 channel;
	s16 rssi;
	u16 frame_len;
	u8 frame_buf[1];
};

/* basic structure of scan request */
struct wl_scan_req {
	struct wlc_ssid ssid;
};

/* basic structure of information element */
struct wl_ie {
	u16 offset;
	u8 buf[WL_TLV_INFO_MAX];
};

/* event queue for cfg80211 main event */
struct wl_event_q {
	struct list_head eq_list;
	u32 etype;
	wl_event_msg_t emsg;
	s8 edata[1];
};

/* security information with currently associated ap */
struct wl_security {
	u32 wpa_versions;
	u32 auth_type;
	u32 cipher_pairwise;
	u32 cipher_group;
	u32 wpa_auth;
};

/* ibss information for currently joined ibss network */
struct wl_ibss {
	u8 beacon_interval;	/* in millisecond */
	u8 atim;		/* in millisecond */
	s8 join_only;
	u8 band;
	u8 channel;
};

/* dongle profile */
struct wl_profile {
	u32 mode;
	struct wlc_ssid ssid;
	u8 bssid[ETH_ALEN];
	u16 beacon_interval;
	u8 dtim_period;
	struct wl_security sec;
	struct wl_ibss ibss;
	s32 band;
	bool active;
};

/* dongle iscan event loop */
struct wl_iscan_eloop {
	s32(*handler[WL_SCAN_ERSULTS_LAST]) (struct wl_priv *wl);
};

/* dongle iscan controller */
struct wl_iscan_ctrl {
	struct net_device *dev;
	struct timer_list timer;
	u32 timer_ms;
	u32 timer_on;
	s32 state;
	struct task_struct *tsk;
	struct semaphore sync;
	struct wl_iscan_eloop el;
	void *data;
	s8 ioctl_buf[WLC_IOCTL_SMLEN];
	s8 scan_buf[WL_ISCAN_BUF_MAX];
};

/* association inform */
struct wl_connect_info {
	u8 *req_ie;
	s32 req_ie_len;
	u8 *resp_ie;
	s32 resp_ie_len;
};

/* firmware /nvram downloading controller */
struct wl_fw_ctrl {
	const struct firmware *fw_entry;
	unsigned long status;
	u32 ptr;
	s8 fw_name[WL_FILE_NAME_MAX];
	s8 nvram_name[WL_FILE_NAME_MAX];
};

/* assoc ie length */
struct wl_assoc_ielen {
	u32 req_len;
	u32 resp_len;
};

/* wpa2 pmk list */
struct wl_pmk_list {
	pmkid_list_t pmkids;
	pmkid_t foo[MAXPMKID - 1];
};

/* dongle private data of cfg80211 interface */
struct wl_priv {
	struct wireless_dev *wdev;	/* representing wl cfg80211 device */
	struct wl_conf *conf;	/* dongle configuration */
	struct cfg80211_scan_request *scan_request;	/* scan request
							 object */
	struct wl_event_loop el;	/* main event loop */
	struct list_head eq_list;	/* used for event queue */
	spinlock_t eq_lock;	/* for event queue synchronization */
	struct mutex usr_sync;	/* maily for dongle up/down synchronization */
	struct wl_scan_results *bss_list;	/* bss_list holding scanned
						 ap information */
	struct wl_scan_results *scan_results;
	struct wl_scan_req *scan_req_int;	/* scan request object for
						 internal purpose */
	struct wl_cfg80211_bss_info *bss_info;	/* bss information for
						 cfg80211 layer */
	struct wl_ie ie;	/* information element object for
					 internal purpose */
	struct ether_addr bssid;	/* bssid of currently engaged network */
	struct semaphore event_sync;	/* for synchronization of main event
					 thread */
	struct wl_profile *profile;	/* holding dongle profile */
	struct wl_iscan_ctrl *iscan;	/* iscan controller */
	struct wl_connect_info conn_info;	/* association information
						 container */
	struct wl_fw_ctrl *fw;	/* control firwmare / nvram paramter
				 downloading */
	struct wl_pmk_list *pmk_list;	/* wpa2 pmk list */
	struct task_struct *event_tsk;	/* task of main event handler thread */
	unsigned long status;		/* current dongle status */
	void *pub;
	u32 channel;		/* current channel */
	bool iscan_on;		/* iscan on/off switch */
	bool iscan_kickstart;	/* indicate iscan already started */
	bool active_scan;	/* current scan mode */
	bool ibss_starter;	/* indicates this sta is ibss starter */
	bool link_up;		/* link/connection up flag */
	bool pwr_save;		/* indicate whether dongle to support
					 power save mode */
	bool dongle_up;		/* indicate whether dongle up or not */
	bool roam_on;		/* on/off switch for dongle self-roaming */
	bool scan_tried;	/* indicates if first scan attempted */
	u8 *ioctl_buf;	/* ioctl buffer */
	u8 *extra_buf;	/* maily to grab assoc information */
	struct dentry *debugfsdir;
	u8 ci[0] __attribute__ ((__aligned__(NETDEV_ALIGN)));
};

#define wl_to_dev(w) (wiphy_dev(wl->wdev->wiphy))
#define wl_to_wiphy(w) (w->wdev->wiphy)
#define wiphy_to_wl(w) ((struct wl_priv *)(wiphy_priv(w)))
#define wl_to_wdev(w) (w->wdev)
#define wdev_to_wl(w) ((struct wl_priv *)(wdev_priv(w)))
#define wl_to_ndev(w) (w->wdev->netdev)
#define ndev_to_wl(n) (wdev_to_wl(n->ieee80211_ptr))
#define ci_to_wl(c) (ci->wl)
#define wl_to_ci(w) (&w->ci)
#define wl_to_sr(w) (w->scan_req_int)
#define wl_to_ie(w) (&w->ie)
#define iscan_to_wl(i) ((struct wl_priv *)(i->data))
#define wl_to_iscan(w) (w->iscan)
#define wl_to_conn(w) (&w->conn_info)

static inline struct wl_bss_info *next_bss(struct wl_scan_results *list,
					   struct wl_bss_info *bss)
{
	return bss = bss ?
		(struct wl_bss_info *)((unsigned long)bss +
				       dtoh32(bss->length)) : list->bss_info;
}

#define for_each_bss(list, bss, __i)	\
	for (__i = 0; __i < list->count && __i < WL_AP_MAX; __i++, bss = next_bss(list, bss))

extern s32 wl_cfg80211_attach(struct net_device *ndev, void *data);
extern void wl_cfg80211_detach(void);
/* event handler from dongle */
extern void wl_cfg80211_event(struct net_device *ndev, const wl_event_msg_t *e,
			      void *data);
extern void wl_cfg80211_sdio_func(void *func);	/* set sdio function info */
extern struct sdio_func *wl_cfg80211_get_sdio_func(void);	/* set sdio function info */
extern s32 wl_cfg80211_up(void);	/* dongle up */
extern s32 wl_cfg80211_down(void);	/* dongle down */
extern void wl_cfg80211_dbg_level(u32 level);	/* set dongle
							 debugging level */
extern void *wl_cfg80211_request_fw(s8 *file_name);	/* request fw /nvram
							 downloading */
extern s32 wl_cfg80211_read_fw(s8 *buf, u32 size);	/* read fw
								 image */
extern void wl_cfg80211_release_fw(void);	/* release fw */
extern s8 *wl_cfg80211_get_fwname(void);	/* get firmware name for
						 the dongle */
extern s8 *wl_cfg80211_get_nvramname(void);	/* get nvram name for
						 the dongle */

#endif				/* _wl_cfg80211_h_ */
