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

struct brcmf_cfg80211_conf;
struct brcmf_cfg80211_iface;
struct brcmf_cfg80211_priv;
struct brcmf_cfg80211_security;
struct brcmf_cfg80211_ibss;

#define WL_DBG_NONE		0
#define WL_DBG_CONN		(1 << 5)
#define WL_DBG_SCAN		(1 << 4)
#define WL_DBG_TRACE		(1 << 3)
#define WL_DBG_INFO		(1 << 1)
#define WL_DBG_ERR		(1 << 0)
#define WL_DBG_MASK		((WL_DBG_INFO | WL_DBG_ERR | WL_DBG_TRACE) | \
				(WL_DBG_SCAN) | (WL_DBG_CONN))

#define	WL_ERR(fmt, args...)					\
do {								\
	if (brcmf_dbg_level & WL_DBG_ERR) {			\
		if (net_ratelimit()) {				\
			printk(KERN_ERR "ERROR @%s : " fmt,	\
				__func__, ##args);		\
		}						\
	}							\
} while (0)

#if (defined BCMDBG)
#define	WL_INFO(fmt, args...)					\
do {								\
	if (brcmf_dbg_level & WL_DBG_INFO) {			\
		if (net_ratelimit()) {				\
			printk(KERN_ERR "INFO @%s : " fmt,	\
				__func__, ##args);		\
		}						\
	}							\
} while (0)

#define	WL_TRACE(fmt, args...)					\
do {								\
	if (brcmf_dbg_level & WL_DBG_TRACE) {			\
		if (net_ratelimit()) {				\
			printk(KERN_ERR "TRACE @%s : " fmt,	\
				__func__, ##args);		\
		}						\
	}							\
} while (0)

#define	WL_SCAN(fmt, args...)					\
do {								\
	if (brcmf_dbg_level & WL_DBG_SCAN) {			\
		if (net_ratelimit()) {				\
			printk(KERN_ERR "SCAN @%s : " fmt,	\
				__func__, ##args);		\
		}						\
	}							\
} while (0)

#define	WL_CONN(fmt, args...)					\
do {								\
	if (brcmf_dbg_level & WL_DBG_CONN) {			\
		if (net_ratelimit()) {				\
			printk(KERN_ERR "CONN @%s : " fmt,	\
				__func__, ##args);		\
		}						\
	}							\
} while (0)

#else /* (defined BCMDBG) */
#define	WL_INFO(fmt, args...)
#define	WL_TRACE(fmt, args...)
#define	WL_SCAN(fmt, args...)
#define	WL_CONN(fmt, args...)
#endif /* (defined BCMDBG) */

#define WL_NUM_SCAN_MAX		1
#define WL_NUM_PMKIDS_MAX	MAXPMKID	/* will be used
						 * for 2.6.33 kernel
						 * or later
						 */
#define WL_SCAN_BUF_MAX			(1024 * 8)
#define WL_TLV_INFO_MAX			1024
#define WL_BSS_INFO_MAX			2048
#define WL_ASSOC_INFO_MAX	512	/*
				 * needs to grab assoc info from dongle to
				 * report it to cfg80211 through "connect"
				 * event
				 */
#define WL_IOCTL_LEN_MAX	1024
#define WL_EXTRA_BUF_MAX	2048
#define WL_ISCAN_BUF_MAX	2048	/*
				 * the buf length can be BRCMF_C_IOCTL_MAXLEN
				 * to reduce iteration
				 */
#define WL_ISCAN_TIMER_INTERVAL_MS	3000
#define WL_SCAN_ERSULTS_LAST	(BRCMF_SCAN_RESULTS_NO_MEM+1)
#define WL_AP_MAX	256	/* virtually unlimitted as long
				 * as kernel memory allows
				 */

#define WL_ROAM_TRIGGER_LEVEL		-75
#define WL_ROAM_DELTA			20
#define WL_BEACON_TIMEOUT		3

#define WL_SCAN_CHANNEL_TIME		40
#define WL_SCAN_UNASSOC_TIME		40
#define WL_SCAN_PASSIVE_TIME		120

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

/* dongle configuration */
struct brcmf_cfg80211_conf {
	u32 mode;		/* adhoc , infrastructure or ap */
	u32 frag_threshold;
	u32 rts_threshold;
	u32 retry_short;
	u32 retry_long;
	s32 tx_power;
	struct ieee80211_channel channel;
};

/* cfg80211 main event loop */
struct brcmf_cfg80211_event_loop {
	s32(*handler[BRCMF_E_LAST]) (struct brcmf_cfg80211_priv *cfg_priv,
				     struct net_device *ndev,
				     const struct brcmf_event_msg *e,
				     void *data);
};

/* representing interface of cfg80211 plane */
struct brcmf_cfg80211_iface {
	struct brcmf_cfg80211_priv *cfg_priv;
};

struct brcmf_cfg80211_dev {
	void *driver_data;	/* to store cfg80211 object information */
};

/* basic structure of scan request */
struct brcmf_cfg80211_scan_req {
	struct brcmf_ssid ssid;
};

/* basic structure of information element */
struct brcmf_cfg80211_ie {
	u16 offset;
	u8 buf[WL_TLV_INFO_MAX];
};

/* event queue for cfg80211 main event */
struct brcmf_cfg80211_event_q {
	struct list_head eq_list;
	u32 etype;
	struct brcmf_event_msg emsg;
	s8 edata[1];
};

/* security information with currently associated ap */
struct brcmf_cfg80211_security {
	u32 wpa_versions;
	u32 auth_type;
	u32 cipher_pairwise;
	u32 cipher_group;
	u32 wpa_auth;
};

/* ibss information for currently joined ibss network */
struct brcmf_cfg80211_ibss {
	u8 beacon_interval;	/* in millisecond */
	u8 atim;		/* in millisecond */
	s8 join_only;
	u8 band;
	u8 channel;
};

/* dongle profile */
struct brcmf_cfg80211_profile {
	u32 mode;
	struct brcmf_ssid ssid;
	u8 bssid[ETH_ALEN];
	u16 beacon_interval;
	u8 dtim_period;
	struct brcmf_cfg80211_security sec;
	struct brcmf_cfg80211_ibss ibss;
	s32 band;
};

/* dongle iscan event loop */
struct brcmf_cfg80211_iscan_eloop {
	s32 (*handler[WL_SCAN_ERSULTS_LAST])
		(struct brcmf_cfg80211_priv *cfg_priv);
};

/* dongle iscan controller */
struct brcmf_cfg80211_iscan_ctrl {
	struct net_device *dev;
	struct timer_list timer;
	u32 timer_ms;
	u32 timer_on;
	s32 state;
	struct task_struct *tsk;
	struct semaphore sync;
	struct brcmf_cfg80211_iscan_eloop el;
	void *data;
	s8 ioctl_buf[BRCMF_C_IOCTL_SMLEN];
	s8 scan_buf[WL_ISCAN_BUF_MAX];
};

/* association inform */
struct brcmf_cfg80211_connect_info {
	u8 *req_ie;
	s32 req_ie_len;
	u8 *resp_ie;
	s32 resp_ie_len;
};

/* assoc ie length */
struct brcmf_cfg80211_assoc_ielen {
	u32 req_len;
	u32 resp_len;
};

/* wpa2 pmk list */
struct brcmf_cfg80211_pmk_list {
	pmkid_list_t pmkids;
	pmkid_t foo[MAXPMKID - 1];
};

/* dongle private data of cfg80211 interface */
struct brcmf_cfg80211_priv {
	struct wireless_dev *wdev;	/* representing wl cfg80211 device */
	struct brcmf_cfg80211_conf *conf;	/* dongle configuration */
	struct cfg80211_scan_request *scan_request;	/* scan request
							 object */
	struct brcmf_cfg80211_event_loop el;	/* main event loop */
	struct list_head eq_list;	/* used for event queue */
	spinlock_t eq_lock;	/* for event queue synchronization */
	struct mutex usr_sync;	/* maily for dongle up/down synchronization */
	struct brcmf_scan_results *bss_list;	/* bss_list holding scanned
						 ap information */
	struct brcmf_scan_results *scan_results;
	struct brcmf_cfg80211_scan_req *scan_req_int;	/* scan request object
						 for internal purpose */
	struct wl_cfg80211_bss_info *bss_info;	/* bss information for
						 cfg80211 layer */
	struct brcmf_cfg80211_ie ie;	/* information element object for
					 internal purpose */
	struct semaphore event_sync;	/* for synchronization of main event
					 thread */
	struct brcmf_cfg80211_profile *profile;	/* holding dongle profile */
	struct brcmf_cfg80211_iscan_ctrl *iscan;	/* iscan controller */
	struct brcmf_cfg80211_connect_info conn_info; /* association info */
	struct brcmf_cfg80211_pmk_list *pmk_list;	/* wpa2 pmk list */
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

#define cfg_to_wiphy(w) (w->wdev->wiphy)
#define wiphy_to_cfg(w) ((struct brcmf_cfg80211_priv *)(wiphy_priv(w)))
#define cfg_to_wdev(w) (w->wdev)
#define wdev_to_cfg(w) ((struct brcmf_cfg80211_priv *)(wdev_priv(w)))
#define cfg_to_ndev(w) (w->wdev->netdev)
#define ndev_to_cfg(n) (wdev_to_cfg(n->ieee80211_ptr))
#define iscan_to_cfg(i) ((struct brcmf_cfg80211_priv *)(i->data))
#define cfg_to_iscan(w) (w->iscan)
#define cfg_to_conn(w) (&w->conn_info)

static inline struct brcmf_bss_info *next_bss(struct brcmf_scan_results *list,
					   struct brcmf_bss_info *bss)
{
	return bss = bss ?
		(struct brcmf_bss_info *)((unsigned long)bss +
				       le32_to_cpu(bss->length)) :
		list->bss_info;
}

#define for_each_bss(list, bss, __i)	\
	for (__i = 0; __i < list->count && __i < WL_AP_MAX; __i++, bss = next_bss(list, bss))

extern s32 brcmf_cfg80211_attach(struct net_device *ndev, void *data);
extern void brcmf_cfg80211_detach(void);
/* event handler from dongle */
extern void brcmf_cfg80211_event(struct net_device *ndev,
				 const struct brcmf_event_msg *e, void *data);
extern void brcmf_cfg80211_sdio_func(void *func); /* set sdio function info */
extern struct sdio_func *brcmf_cfg80211_get_sdio_func(void);
extern s32 brcmf_cfg80211_up(void);	/* dongle up */
extern s32 brcmf_cfg80211_down(void);	/* dongle down */

#endif				/* _wl_cfg80211_h_ */
