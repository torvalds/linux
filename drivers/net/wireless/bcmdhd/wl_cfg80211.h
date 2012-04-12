/*
 * Linux cfg80211 driver
 *
 * Copyright (C) 1999-2012, Broadcom Corporation
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
 * $Id: wl_cfg80211.h 316895 2012-02-24 00:05:41Z $
 */

#ifndef _wl_cfg80211_h_
#define _wl_cfg80211_h_

#include <linux/wireless.h>
#include <typedefs.h>
#include <proto/ethernet.h>
#include <wlioctl.h>
#include <linux/wireless.h>
#include <net/cfg80211.h>
#include <linux/rfkill.h>

#include <wl_cfgp2p.h>

struct wl_conf;
struct wl_iface;
struct wl_priv;
struct wl_security;
struct wl_ibss;


#define htod32(i) i
#define htod16(i) i
#define dtoh32(i) i
#define dtoh16(i) i
#define htodchanspec(i) i
#define dtohchanspec(i) i

#define WL_DBG_NONE	0
#define WL_DBG_TRACE	(1 << 4)
#define WL_DBG_SCAN 	(1 << 3)
#define WL_DBG_DBG 	(1 << 2)
#define WL_DBG_INFO	(1 << 1)
#define WL_DBG_ERR	(1 << 0)

/* 0 invalidates all debug messages.  default is 1 */
#define WL_DBG_LEVEL 0xFF

#define	WL_ERR(args)									\
do {										\
	if (wl_dbg_level & WL_DBG_ERR) {				\
			printk(KERN_ERR "CFG80211-ERROR) %s : ", __func__);	\
			printk args;						\
		} 								\
} while (0)
#ifdef WL_INFO
#undef WL_INFO
#endif
#define	WL_INFO(args)									\
do {										\
	if (wl_dbg_level & WL_DBG_INFO) {				\
			printk(KERN_ERR "CFG80211-INFO) %s : ", __func__);	\
			printk args;						\
		}								\
} while (0)
#ifdef WL_SCAN
#undef WL_SCAN
#endif
#define	WL_SCAN(args)								\
do {									\
	if (wl_dbg_level & WL_DBG_SCAN) {			\
		printk(KERN_ERR "CFG80211-SCAN) %s :", __func__);	\
		printk args;							\
	}									\
} while (0)
#ifdef WL_TRACE
#undef WL_TRACE
#endif
#define	WL_TRACE(args)								\
do {									\
	if (wl_dbg_level & WL_DBG_TRACE) {			\
		printk(KERN_ERR "CFG80211-TRACE) %s :", __func__);	\
		printk args;							\
	}									\
} while (0)
#if (WL_DBG_LEVEL > 0)
#define	WL_DBG(args)								\
do {									\
	if (wl_dbg_level & WL_DBG_DBG) {			\
		printk(KERN_ERR "CFG80211-DEBUG) %s :", __func__);	\
		printk args;							\
	}									\
} while (0)
#else				/* !(WL_DBG_LEVEL > 0) */
#define	WL_DBG(args)
#endif				/* (WL_DBG_LEVEL > 0) */


#define WL_SCAN_RETRY_MAX	3
#define WL_NUM_PMKIDS_MAX	MAXPMKID
#define WL_SCAN_BUF_MAX 	(1024 * 8)
#define WL_TLV_INFO_MAX 	1024
#define WL_SCAN_IE_LEN_MAX      2048
#define WL_BSS_INFO_MAX		2048
#define WL_ASSOC_INFO_MAX	512
#define WL_IOCTL_LEN_MAX	1024
#define WL_EXTRA_BUF_MAX	2048
#define WL_ISCAN_BUF_MAX	2048
#define WL_ISCAN_TIMER_INTERVAL_MS	3000
#define WL_SCAN_ERSULTS_LAST 	(WL_SCAN_RESULTS_NO_MEM+1)
#define WL_AP_MAX		256
#define WL_FILE_NAME_MAX	256
#define WL_DWELL_TIME 		200
#define WL_MED_DWELL_TIME       400
#define WL_LONG_DWELL_TIME 	1000
#define IFACE_MAX_CNT 		2

#define WL_SCAN_TIMER_INTERVAL_MS	8000 /* Scan timeout */
#define WL_CHANNEL_SYNC_RETRY 	5
#define WL_INVALID 		-1

/* driver status */
enum wl_status {
	WL_STATUS_READY = 0,
	WL_STATUS_SCANNING,
	WL_STATUS_SCAN_ABORTING,
	WL_STATUS_CONNECTING,
	WL_STATUS_CONNECTED,
	WL_STATUS_DISCONNECTING,
	WL_STATUS_AP_CREATING,
	WL_STATUS_AP_CREATED,
	WL_STATUS_SENDING_ACT_FRM
};

/* wi-fi mode */
enum wl_mode {
	WL_MODE_BSS,
	WL_MODE_IBSS,
	WL_MODE_AP
};

/* driver profile list */
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

/* driver iscan state */
enum wl_iscan_state {
	WL_ISCAN_STATE_IDLE,
	WL_ISCAN_STATE_SCANING
};

/* donlge escan state */
enum wl_escan_state {
    WL_ESCAN_STATE_IDLE,
    WL_ESCAN_STATE_SCANING
};
/* fw downloading status */
enum wl_fw_status {
	WL_FW_LOADING_DONE,
	WL_NVRAM_LOADING_DONE
};

enum wl_management_type {
	WL_BEACON = 0x1,
	WL_PROBE_RESP = 0x2,
	WL_ASSOC_RESP = 0x4
};
/* beacon / probe_response */
struct beacon_proberesp {
	__le64 timestamp;
	__le16 beacon_int;
	__le16 capab_info;
	u8 variable[0];
} __attribute__ ((packed));

/* driver configuration */
struct wl_conf {
	u32 frag_threshold;
	u32 rts_threshold;
	u32 retry_short;
	u32 retry_long;
	s32 tx_power;
	struct ieee80211_channel channel;
};

typedef s32(*EVENT_HANDLER) (struct wl_priv *wl,
                            struct net_device *ndev, const wl_event_msg_t *e, void *data);

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

/* wl driver profile */
struct wl_profile {
	u32 mode;
	s32 band;
	struct wlc_ssid ssid;
	struct wl_security sec;
	struct wl_ibss ibss;
	u8 bssid[ETHER_ADDR_LEN];
	u16 beacon_interval;
	u8 dtim_period;
	bool active;
};

struct net_info {
	struct net_device *ndev;
	struct wireless_dev *wdev;
	struct wl_profile profile;
	s32 mode;
	unsigned long sme_state;
	struct list_head list; /* list of all net_info structure */
};
typedef s32(*ISCAN_HANDLER) (struct wl_priv *wl);

/* iscan controller */
struct wl_iscan_ctrl {
	struct net_device *dev;
	struct timer_list timer;
	u32 timer_ms;
	u32 timer_on;
	s32 state;
	struct task_struct *tsk;
	struct semaphore sync;
	ISCAN_HANDLER iscan_handler[WL_SCAN_ERSULTS_LAST];
	void *data;
	s8 ioctl_buf[WLC_IOCTL_SMLEN];
	s8 scan_buf[WL_ISCAN_BUF_MAX];
};

/* association inform */
#define MAX_REQ_LINE 1024
struct wl_connect_info {
	u8 req_ie[MAX_REQ_LINE];
	s32 req_ie_len;
	u8 resp_ie[MAX_REQ_LINE];
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


#define ESCAN_BUF_SIZE (64 * 1024)

struct escan_info {
	u32 escan_state;
	u8 escan_buf[ESCAN_BUF_SIZE];
	struct wiphy *wiphy;
	struct net_device *ndev;
};

struct ap_info {
/* Structure to hold WPS, WPA IEs for a AP */
	u8   probe_res_ie[IE_MAX_LEN];
	u8   beacon_ie[IE_MAX_LEN];
	u32 probe_res_ie_len;
	u32 beacon_ie_len;
	u8 *wpa_ie;
	u8 *rsn_ie;
	u8 *wps_ie;
	bool security_mode;
};
struct btcoex_info {
	struct timer_list timer;
	u32 timer_ms;
	u32 timer_on;
	u32 ts_dhcp_start;	/* ms ts ecord time stats */
	u32 ts_dhcp_ok;		/* ms ts ecord time stats */
	bool dhcp_done;	/* flag, indicates that host done with
					 * dhcp before t1/t2 expiration
					 */
	s32 bt_state;
	struct work_struct work;
	struct net_device *dev;
};

struct sta_info {
	/* Structure to hold WPS IE for a STA */
	u8  probe_req_ie[IE_MAX_LEN];
	u8  assoc_req_ie[IE_MAX_LEN];
	u32 probe_req_ie_len;
	u32 assoc_req_ie_len;
};

struct afx_hdl {
	wl_af_params_t *pending_tx_act_frm;
	struct ether_addr	pending_tx_dst_addr;
	struct net_device *dev;
	struct work_struct work;
	u32 bssidx;
	u32 retry;
	s32 peer_chan;
	bool ack_recv;
};

/* private data of cfg80211 interface */
struct wl_priv {
	struct wireless_dev *wdev;	/* representing wl cfg80211 device */

	struct wireless_dev *p2p_wdev;	/* representing wl cfg80211 device for P2P */
	struct net_device *p2p_net;    /* reference to p2p0 interface */

	struct wl_conf *conf;
	struct cfg80211_scan_request *scan_request;	/* scan request object */
	EVENT_HANDLER evt_handler[WLC_E_LAST];
	struct list_head eq_list;	/* used for event queue */
	struct list_head net_list;     /* used for struct net_info */
	spinlock_t eq_lock;	/* for event queue synchronization */
	spinlock_t cfgdrv_lock;	/* to protect scan status (and others if needed) */
	struct completion act_frm_scan;
	struct mutex usr_sync;	/* maily for up/down synchronization */
	struct wl_scan_results *bss_list;
	struct wl_scan_results *scan_results;

	/* scan request object for internal purpose */
	struct wl_scan_req *scan_req_int;
	/* information element object for internal purpose */
	struct wl_ie ie;
	struct wl_iscan_ctrl *iscan;	/* iscan controller */

	/* association information container */
	struct wl_connect_info conn_info;

	struct wl_pmk_list *pmk_list;	/* wpa2 pmk list */
	tsk_ctl_t event_tsk;  		/* task of main event handler thread */
	void *pub;
	u32 iface_cnt;
	u32 channel;		/* current channel */
	bool iscan_on;		/* iscan on/off switch */
	bool iscan_kickstart;	/* indicate iscan already started */
	bool escan_on;      /* escan on/off switch */
	struct escan_info escan_info;   /* escan information */
	bool active_scan;	/* current scan mode */
	bool ibss_starter;	/* indicates this sta is ibss starter */
	bool link_up;		/* link/connection up flag */

	/* indicate whether chip to support power save mode */
	bool pwr_save;
	bool roam_on;		/* on/off switch for self-roaming */
	bool scan_tried;	/* indicates if first scan attempted */
	u8 *ioctl_buf;		/* ioctl buffer */
	struct mutex ioctl_buf_sync;
	u8 *escan_ioctl_buf;
	u8 *extra_buf;	/* maily to grab assoc information */
	struct dentry *debugfsdir;
	struct rfkill *rfkill;
	bool rf_blocked;
	struct ieee80211_channel remain_on_chan;
	enum nl80211_channel_type remain_on_chan_type;
	u64 send_action_id;
	u64 last_roc_id;
	wait_queue_head_t netif_change_event;
	struct afx_hdl *afx_hdl;
	struct ap_info *ap_info;
	struct sta_info *sta_info;
	struct p2p_info *p2p;
	bool p2p_supported;
	struct btcoex_info *btcoex_info;
	struct timer_list scan_timeout;   /* Timer for catch scan event timeout */
};


static inline struct wl_bss_info *next_bss(struct wl_scan_results *list, struct wl_bss_info *bss)
{
	return bss = bss ?
		(struct wl_bss_info *)((uintptr) bss + dtoh32(bss->length)) : list->bss_info;
}
static inline s32
wl_alloc_netinfo(struct wl_priv *wl, struct net_device *ndev,
	struct wireless_dev * wdev, s32 mode)
{
	struct net_info *_net_info;
	s32 err = 0;
	if (wl->iface_cnt == IFACE_MAX_CNT)
		return -ENOMEM;
	_net_info = kzalloc(sizeof(struct net_info), GFP_KERNEL);
	if (!_net_info)
		err = -ENOMEM;
	else {
		_net_info->mode = mode;
		_net_info->ndev = ndev;
		_net_info->wdev = wdev;
		wl->iface_cnt++;
		list_add(&_net_info->list, &wl->net_list);
	}
	return err;
}
static inline void
wl_dealloc_netinfo(struct wl_priv *wl, struct net_device *ndev)
{
	struct net_info *_net_info, *next;

	list_for_each_entry_safe(_net_info, next, &wl->net_list, list) {
		if (ndev && (_net_info->ndev == ndev)) {
			list_del(&_net_info->list);
			wl->iface_cnt--;
			if (_net_info->wdev) {
				kfree(_net_info->wdev);
				ndev->ieee80211_ptr = NULL;
			}
			kfree(_net_info);
		}
	}

}
static inline void
wl_delete_all_netinfo(struct wl_priv *wl)
{
	struct net_info *_net_info, *next;

	list_for_each_entry_safe(_net_info, next, &wl->net_list, list) {
		list_del(&_net_info->list);
			if (_net_info->wdev)
				kfree(_net_info->wdev);
			kfree(_net_info);
	}
	wl->iface_cnt = 0;
}
static inline bool
wl_get_status_all(struct wl_priv *wl, s32 status)

{
	struct net_info *_net_info, *next;
	u32 cnt = 0;
	list_for_each_entry_safe(_net_info, next, &wl->net_list, list) {
		if (_net_info->ndev &&
			test_bit(status, &_net_info->sme_state))
			cnt++;
	}
	return cnt? true: false;
}
static inline void
wl_set_status_by_netdev(struct wl_priv *wl, s32 status,
	struct net_device *ndev, u32 op)
{

	struct net_info *_net_info, *next;

	list_for_each_entry_safe(_net_info, next, &wl->net_list, list) {
		if (ndev && (_net_info->ndev == ndev)) {
			switch (op) {
				case 1:
					set_bit(status, &_net_info->sme_state);
					break;
				case 2:
					clear_bit(status, &_net_info->sme_state);
					break;
				case 4:
					change_bit(status, &_net_info->sme_state);
					break;
			}
		}

	}

}

static inline u32
wl_get_status_by_netdev(struct wl_priv *wl, s32 status,
	struct net_device *ndev)
{
	struct net_info *_net_info, *next;

	list_for_each_entry_safe(_net_info, next, &wl->net_list, list) {
				if (ndev && (_net_info->ndev == ndev))
					return test_bit(status, &_net_info->sme_state);
	}
	return 0;
}

static inline s32
wl_get_mode_by_netdev(struct wl_priv *wl, struct net_device *ndev)
{
	struct net_info *_net_info, *next;

	list_for_each_entry_safe(_net_info, next, &wl->net_list, list) {
				if (ndev && (_net_info->ndev == ndev))
					return _net_info->mode;
	}
	return -1;
}


static inline void
wl_set_mode_by_netdev(struct wl_priv *wl, struct net_device *ndev,
	s32 mode)
{
	struct net_info *_net_info, *next;

	list_for_each_entry_safe(_net_info, next, &wl->net_list, list) {
				if (ndev && (_net_info->ndev == ndev))
					_net_info->mode = mode;
	}
}
static inline struct wl_profile *
wl_get_profile_by_netdev(struct wl_priv *wl, struct net_device *ndev)
{
	struct net_info *_net_info, *next;

	list_for_each_entry_safe(_net_info, next, &wl->net_list, list) {
				if (ndev && (_net_info->ndev == ndev))
					return &_net_info->profile;
	}
	return NULL;
}
#define wl_to_wiphy(w) (w->wdev->wiphy)
#define wl_to_prmry_ndev(w) (w->wdev->netdev)
#define ndev_to_wl(n) (wdev_to_wl(n->ieee80211_ptr))
#define wl_to_sr(w) (w->scan_req_int)
#define wl_to_ie(w) (&w->ie)
#define iscan_to_wl(i) ((struct wl_priv *)(i->data))
#define wl_to_iscan(w) (w->iscan)
#define wl_to_conn(w) (&w->conn_info)
#define wiphy_from_scan(w) (w->escan_info.wiphy)
#define wl_get_drv_status_all(wl, stat) \
	(wl_get_status_all(wl, WL_STATUS_ ## stat))
#define wl_get_drv_status(wl, stat, ndev)  \
	(wl_get_status_by_netdev(wl, WL_STATUS_ ## stat, ndev))
#define wl_set_drv_status(wl, stat, ndev)  \
	(wl_set_status_by_netdev(wl, WL_STATUS_ ## stat, ndev, 1))
#define wl_clr_drv_status(wl, stat, ndev)  \
	(wl_set_status_by_netdev(wl, WL_STATUS_ ## stat, ndev, 2))
#define wl_chg_drv_status(wl, stat, ndev)  \
	(wl_set_status_by_netdev(wl, WL_STATUS_ ## stat, ndev, 4))

#define for_each_bss(list, bss, __i)	\
	for (__i = 0; __i < list->count && __i < WL_AP_MAX; __i++, bss = next_bss(list, bss))

#define for_each_ndev(wl, iter, next) \
	list_for_each_entry_safe(iter, next, &wl->net_list, list)


/* In case of WPS from wpa_supplicant, pairwise siute and group suite is 0.
 * In addtion to that, wpa_version is WPA_VERSION_1
 */
#define is_wps_conn(_sme) \
	((wl_cfgp2p_find_wpsie((u8 *)_sme->ie, _sme->ie_len) != NULL) && \
	 (!_sme->crypto.n_ciphers_pairwise) && \
	 (!_sme->crypto.cipher_group))
extern s32 wl_cfg80211_attach(struct net_device *ndev, void *data);
extern s32 wl_cfg80211_attach_post(struct net_device *ndev);
extern void wl_cfg80211_detach(void *para);

extern void wl_cfg80211_event(struct net_device *ndev, const wl_event_msg_t *e,
            void *data);
void wl_cfg80211_set_parent_dev(void *dev);
struct device *wl_cfg80211_get_parent_dev(void);

extern s32 wl_cfg80211_up(void *para);
extern s32 wl_cfg80211_down(void *para);
extern s32 wl_cfg80211_notify_ifadd(struct net_device *ndev, s32 idx, s32 bssidx,
	void* _net_attach);
extern s32 wl_cfg80211_ifdel_ops(struct net_device *net);
extern s32 wl_cfg80211_notify_ifdel(struct net_device *ndev);
extern s32 wl_cfg80211_is_progress_ifadd(void);
extern s32 wl_cfg80211_is_progress_ifchange(void);
extern s32 wl_cfg80211_is_progress_ifadd(void);
extern s32 wl_cfg80211_notify_ifchange(void);
extern void wl_cfg80211_dbg_level(u32 level);
extern s32 wl_cfg80211_get_p2p_dev_addr(struct net_device *net, struct ether_addr *p2pdev_addr);
extern s32 wl_cfg80211_set_p2p_noa(struct net_device *net, char* buf, int len);
extern s32 wl_cfg80211_get_p2p_noa(struct net_device *net, char* buf, int len);
extern s32 wl_cfg80211_set_wps_p2p_ie(struct net_device *net, char *buf, int len,
	enum wl_management_type type);
extern s32 wl_cfg80211_set_p2p_ps(struct net_device *net, char* buf, int len);
extern int wl_cfg80211_hang(struct net_device *dev, u16 reason);
extern s32 wl_mode_to_nl80211_iftype(s32 mode);
int wl_cfg80211_do_driver_init(struct net_device *net);
void wl_cfg80211_enable_trace(int level);
extern s32 wl_cfg80211_if_is_group_owner(void);
extern chanspec_t wl_ch_host_to_driver(u16 channel);

#endif				/* _wl_cfg80211_h_ */
