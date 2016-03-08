/*
 * Linux cfg80211 driver
 *
 * Copyright (C) 1999-2016, Broadcom Corporation
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
 * $Id: wl_cfg80211.h 608788 2015-12-29 10:59:33Z $
 */

/**
 * Older Linux versions support the 'iw' interface, more recent ones the 'cfg80211' interface.
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

#include <dngl_stats.h>
#include <dhd.h>
#include <wl_cfgp2p.h>
#include <linux/time.h>

struct wl_conf;
struct wl_iface;
struct bcm_cfg80211;
struct wl_security;
struct wl_ibss;


#define htod32(i) (i)
#define htod16(i) (i)
#define dtoh64(i) (i)
#define dtoh32(i) (i)
#define dtoh16(i) (i)
#define htodchanspec(i) (i)
#define dtohchanspec(i) (i)

#define WL_DBG_NONE	0
#define WL_DBG_P2P_ACTION (1 << 5)
#define WL_DBG_TRACE	(1 << 4)
#define WL_DBG_SCAN 	(1 << 3)
#define WL_DBG_DBG 	(1 << 2)
#define WL_DBG_INFO	(1 << 1)
#define WL_DBG_ERR	(1 << 0)

#ifdef DHD_LOG_DUMP
extern void dhd_log_dump_print(const char *fmt, ...);
extern char *dhd_log_dump_get_timestamp(void);
struct bcm_cfg80211 *wl_get_bcm_cfg80211_ptr(void);
#endif /* DHD_LOG_DUMP */

/* 0 invalidates all debug messages.  default is 1 */
#define WL_DBG_LEVEL 0xFF

#ifdef CUSTOMER_HW4_DEBUG
#define CFG80211_ERROR_TEXT		"CFG80211-INFO2) "
#else
#define CFG80211_ERROR_TEXT		"CFG80211-ERROR) "
#endif /* CUSTOMER_HW4_DEBUG */

#if defined(DHD_DEBUG)
#ifdef DHD_LOG_DUMP
#define	WL_ERR(args)	\
do {	\
	if (wl_dbg_level & WL_DBG_ERR) {	\
		printk(KERN_INFO CFG80211_ERROR_TEXT "%s : ", __func__);	\
		printk args;	\
		dhd_log_dump_print("[%s] %s: ", dhd_log_dump_get_timestamp(), __func__);	\
		dhd_log_dump_print args;	\
	}	\
} while (0)
#else
#define	WL_ERR(args)									\
do {										\
	if (wl_dbg_level & WL_DBG_ERR) {				\
			printk(KERN_INFO CFG80211_ERROR_TEXT "%s : ", __func__);	\
			printk args;						\
		}								\
} while (0)
#endif /* DHD_LOG_DUMP */
#else /* defined(DHD_DEBUG) */
#define	WL_ERR(args)									\
do {										\
	if ((wl_dbg_level & WL_DBG_ERR) && net_ratelimit()) {				\
			printk(KERN_INFO CFG80211_ERROR_TEXT "%s : ", __func__);	\
			printk args;						\
		}								\
} while (0)
#endif /* defined(DHD_DEBUG) */

#ifdef WL_INFORM
#undef WL_INFORM
#endif

#define	WL_INFORM(args)									\
do {										\
	if (wl_dbg_level & WL_DBG_INFO) {				\
			printk(KERN_INFO "CFG80211-INFO) %s : ", __func__);	\
			printk args;						\
		}								\
} while (0)


#ifdef WL_SCAN
#undef WL_SCAN
#endif
#define	WL_SCAN(args)								\
do {									\
	if (wl_dbg_level & WL_DBG_SCAN) {			\
		printk(KERN_INFO "CFG80211-SCAN) %s :", __func__);	\
		printk args;							\
	}									\
} while (0)
#ifdef WL_TRACE
#undef WL_TRACE
#endif
#define	WL_TRACE(args)								\
do {									\
	if (wl_dbg_level & WL_DBG_TRACE) {			\
		printk(KERN_INFO "CFG80211-TRACE) %s :", __func__);	\
		printk args;							\
	}									\
} while (0)
#ifdef WL_TRACE_HW4
#undef WL_TRACE_HW4
#endif
#ifdef CUSTOMER_HW4_DEBUG
#define	WL_TRACE_HW4(args)					\
do {										\
	if (wl_dbg_level & WL_DBG_ERR) {				\
			printk(KERN_INFO "CFG80211-TRACE) %s : ", __func__);	\
			printk args;						\
		} 								\
} while (0)
#else
#define	WL_TRACE_HW4			WL_TRACE
#endif /* CUSTOMER_HW4_DEBUG */
#if (WL_DBG_LEVEL > 0)
#define	WL_DBG(args)								\
do {									\
	if (wl_dbg_level & WL_DBG_DBG) {			\
		printk(KERN_INFO "CFG80211-DEBUG) %s :", __func__);	\
		printk args;							\
	}									\
} while (0)
#else				/* !(WL_DBG_LEVEL > 0) */
#define	WL_DBG(args)
#endif				/* (WL_DBG_LEVEL > 0) */
#define WL_PNO(x)
#define WL_SD(x)


#define WL_SCAN_RETRY_MAX	3
#define WL_NUM_PMKIDS_MAX	MAXPMKID
#define WL_SCAN_BUF_MAX 	(1024 * 8)
#define WL_TLV_INFO_MAX 	1500
#define WL_SCAN_IE_LEN_MAX      2048
#define WL_BSS_INFO_MAX		2048
#define WL_ASSOC_INFO_MAX	512
#define WL_IOCTL_LEN_MAX	2048
#define WL_EXTRA_BUF_MAX	2048
#define WL_SCAN_ERSULTS_LAST 	(WL_SCAN_RESULTS_NO_MEM+1)
#define WL_AP_MAX			256
#define WL_FILE_NAME_MAX	256
#define WL_DWELL_TIME		200
#define WL_MED_DWELL_TIME	400
#define WL_MIN_DWELL_TIME	100
#define WL_LONG_DWELL_TIME	1000
#define IFACE_MAX_CNT	4
#define WL_SCAN_CONNECT_DWELL_TIME_MS		200
#define WL_SCAN_JOIN_PROBE_INTERVAL_MS		20
#define WL_SCAN_JOIN_ACTIVE_DWELL_TIME_MS	320
#define WL_SCAN_JOIN_PASSIVE_DWELL_TIME_MS	400
#define WL_AF_TX_MAX_RETRY	5

#define WL_AF_SEARCH_TIME_MAX		450
#define WL_AF_TX_EXTRA_TIME_MAX		200

#define WL_SCAN_TIMER_INTERVAL_MS	10000 /* Scan timeout */
#define WL_CHANNEL_SYNC_RETRY 	5
#define WL_INVALID 		-1

#ifdef DHD_LOSSLESS_ROAMING
#define WL_ROAM_TIMEOUT_MS	1000 /* Roam timeout */
#endif
/* Bring down SCB Timeout to 20secs from 60secs default */
#ifndef WL_SCB_TIMEOUT
#define WL_SCB_TIMEOUT	20
#endif

#ifndef WL_SCB_ACTIVITY_TIME
#define WL_SCB_ACTIVITY_TIME	5
#endif

#ifndef WL_SCB_MAX_PROBE
#define WL_SCB_MAX_PROBE	3
#endif

#ifndef WL_MIN_PSPRETEND_THRESHOLD
#define WL_MIN_PSPRETEND_THRESHOLD	2
#endif

/* SCAN_SUPPRESS timer values in ms */
#define WL_SCAN_SUPPRESS_TIMEOUT 31000 /* default Framwork DHCP timeout is 30 sec */
#define WL_SCAN_SUPPRESS_RETRY 3000

#define WL_PM_ENABLE_TIMEOUT 10000

/* cfg80211 wowlan definitions */
#define WL_WOWLAN_MAX_PATTERNS			8
#define WL_WOWLAN_MIN_PATTERN_LEN		1
#define WL_WOWLAN_MAX_PATTERN_LEN		255
#define WL_WOWLAN_PKT_FILTER_ID_FIRST	201
#define WL_WOWLAN_PKT_FILTER_ID_LAST	(WL_WOWLAN_PKT_FILTER_ID_FIRST + \
									WL_WOWLAN_MAX_PATTERNS - 1)

#ifdef WLTDLS
#define TDLS_TUNNELED_PRB_REQ	"\x7f\x50\x6f\x9a\04"
#define TDLS_TUNNELED_PRB_RESP	"\x7f\x50\x6f\x9a\05"
#endif /* WLTDLS */


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
	/* whole sending action frame procedure:
	 * includes a) 'finding common channel' for public action request frame
	 * and b) 'sending af via 'actframe' iovar'
	 */
	WL_STATUS_SENDING_ACT_FRM,
	/* find a peer to go to a common channel before sending public action req frame */
	WL_STATUS_FINDING_COMMON_CHANNEL,
	/* waiting for next af to sync time of supplicant.
	 * it includes SENDING_ACT_FRM and WAITING_NEXT_ACT_FRM_LISTEN
	 */
	WL_STATUS_WAITING_NEXT_ACT_FRM,
#ifdef WL_CFG80211_SYNC_GON
	/* go to listen state to wait for next af after SENDING_ACT_FRM */
	WL_STATUS_WAITING_NEXT_ACT_FRM_LISTEN,
#endif /* WL_CFG80211_SYNC_GON */
	/* it will be set when upper layer requests listen and succeed in setting listen mode.
	 * if set, other scan request can abort current listen state
	 */
	WL_STATUS_REMAINING_ON_CHANNEL,
#ifdef WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST
	/* it's fake listen state to keep current scan state.
	 * it will be set when upper layer requests listen but scan is running. then just run
	 * a expire timer without actual listen state.
	 * if set, other scan request does not need to abort scan.
	 */
	WL_STATUS_FAKE_REMAINING_ON_CHANNEL
#endif /* WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST */
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
	WL_PROF_CHAN,
	WL_PROF_BSSID,
	WL_PROF_ACT,
	WL_PROF_BEACONINT,
	WL_PROF_DTIMPERIOD
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

enum wl_pm_workq_act_type {
	WL_PM_WORKQ_SHORT,
	WL_PM_WORKQ_LONG,
	WL_PM_WORKQ_DEL
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

typedef s32(*EVENT_HANDLER) (struct bcm_cfg80211 *cfg, bcm_struct_cfgdev *cfgdev,
                            const wl_event_msg_t *e, void *data);

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
	u32 auth_assoc_res_status;
};

/* ibss information for currently joined ibss network */
struct wl_ibss {
	u8 beacon_interval;	/* in millisecond */
	u8 atim;		/* in millisecond */
	s8 join_only;
	u8 band;
	u8 channel;
};

typedef struct wl_bss_vndr_ies {
	u8  probe_req_ie[VNDR_IES_BUF_LEN];
	u8  probe_res_ie[VNDR_IES_MAX_BUF_LEN];
	u8  assoc_req_ie[VNDR_IES_BUF_LEN];
	u8  assoc_res_ie[VNDR_IES_BUF_LEN];
	u8  beacon_ie[VNDR_IES_MAX_BUF_LEN];
	u32 probe_req_ie_len;
	u32 probe_res_ie_len;
	u32 assoc_req_ie_len;
	u32 assoc_res_ie_len;
	u32 beacon_ie_len;
} wl_bss_vndr_ies_t;

typedef struct wl_cfgbss {
	u8 *wpa_ie;
	u8 *rsn_ie;
	u8 *wps_ie;
	bool security_mode;
	struct wl_bss_vndr_ies ies;	/* Common for STA, P2P GC, GO, AP, P2P Disc Interface */
} wl_cfgbss_t;

/* cfg driver profile */
struct wl_profile {
	u32 mode;
	s32 band;
	u32 channel;
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
	s32 roam_off;
	unsigned long sme_state;
	bool pm_restore;
	bool pm_block;
	s32 pm;
	s32 bssidx;
	wl_cfgbss_t bss;
	u32 ulb_bw;
	struct list_head list; /* list of all net_info structure */
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

#ifdef DHD_MAX_IFS
#define WL_MAX_IFS DHD_MAX_IFS
#else
#define WL_MAX_IFS 16
#endif

#define ESCAN_BUF_SIZE (64 * 1024)

struct escan_info {
	u32 escan_state;
#if defined(STATIC_WL_PRIV_STRUCT)
#ifndef CONFIG_DHD_USE_STATIC_BUF
#error STATIC_WL_PRIV_STRUCT should be used with CONFIG_DHD_USE_STATIC_BUF
#endif /* CONFIG_DHD_USE_STATIC_BUF */
	u8 *escan_buf;
#else
	u8 escan_buf[ESCAN_BUF_SIZE];
#endif /* STATIC_WL_PRIV_STRUCT */
	struct wiphy *wiphy;
	struct net_device *ndev;
};

#ifdef ESCAN_BUF_OVERFLOW_MGMT
#define BUF_OVERFLOW_MGMT_COUNT 3
typedef struct {
	int RSSI;
	int length;
	struct ether_addr BSSID;
} removal_element_t;
#endif /* ESCAN_BUF_OVERFLOW_MGMT */

struct afx_hdl {
	wl_af_params_t *pending_tx_act_frm;
	struct ether_addr	tx_dst_addr;
	struct net_device *dev;
	struct work_struct work;
	s32 bssidx;
	u32 retry;
	s32 peer_chan;
	s32 peer_listen_chan; /* search channel: configured by upper layer */
	s32 my_listen_chan;	/* listen chanel: extract it from prb req or gon req */
	bool is_listen;
	bool ack_recv;
	bool is_active;
};

struct parsed_ies {
	wpa_ie_fixed_t *wps_ie;
	u32 wps_ie_len;
	wpa_ie_fixed_t *wpa_ie;
	u32 wpa_ie_len;
	bcm_tlv_t *wpa2_ie;
	u32 wpa2_ie_len;
};


#ifdef P2P_LISTEN_OFFLOADING
typedef struct {
	uint16	period;                 /* listen offload period */
	uint16	interval;               /* listen offload interval */
	uint16	count;			/* listen offload count */
	uint16	pad;                    /* pad for 32bit align */
} wl_p2plo_listen_t;
#endif /* P2P_LISTEN_OFFLOADING */

#ifdef WL11U
/* Max length of Interworking element */
#define IW_IES_MAX_BUF_LEN 		9
#endif
#define MAX_EVENT_BUF_NUM 16
typedef struct wl_eventmsg_buf {
	u16 num;
	struct {
		u16 type;
		bool set;
	} event [MAX_EVENT_BUF_NUM];
} wl_eventmsg_buf_t;

typedef struct wl_if_event_info {
	bool valid;
	int ifidx;
	int bssidx;
	uint8 mac[ETHER_ADDR_LEN];
	char name[IFNAMSIZ+1];
} wl_if_event_info;

#if defined(DHD_ENABLE_BIGDATA_LOGGING)
#define GET_BSS_INFO_LEN 90
#endif /* DHD_ENABLE_BIGDATA_LOGGING */

/* private data of cfg80211 interface */
struct bcm_cfg80211 {
	struct wireless_dev *wdev;	/* representing cfg cfg80211 device */

	struct wireless_dev *p2p_wdev;	/* representing cfg cfg80211 device for P2P */
	struct net_device *p2p_net;    /* reference to p2p0 interface */

	struct wl_conf *conf;
	struct cfg80211_scan_request *scan_request;	/* scan request object */
	EVENT_HANDLER evt_handler[WLC_E_LAST];
	struct list_head eq_list;	/* used for event queue */
	struct list_head net_list;     /* used for struct net_info */
	spinlock_t net_list_sync;	/* to protect scan status (and others if needed) */
	spinlock_t eq_lock;	/* for event queue synchronization */
	spinlock_t cfgdrv_lock;	/* to protect scan status (and others if needed) */
	struct completion act_frm_scan;
	struct completion iface_disable;
	struct completion wait_next_af;
	struct mutex usr_sync;	/* maily for up/down synchronization */
	struct mutex scan_complete;	/* serialize scan_complete call */
	struct wl_scan_results *bss_list;
	struct wl_scan_results *scan_results;

	/* scan request object for internal purpose */
	struct wl_scan_req *scan_req_int;
	/* information element object for internal purpose */
#if defined(STATIC_WL_PRIV_STRUCT)
	struct wl_ie *ie;
#else
	struct wl_ie ie;
#endif

	/* association information container */
#if defined(STATIC_WL_PRIV_STRUCT)
	struct wl_connect_info *conn_info;
#else
	struct wl_connect_info conn_info;
#endif
#ifdef DEBUGFS_CFG80211
	struct dentry		*debugfs;
#endif /* DEBUGFS_CFG80211 */
	struct wl_pmk_list *pmk_list;	/* wpa2 pmk list */
	tsk_ctl_t event_tsk;  		/* task of main event handler thread */
	dhd_pub_t *pub;
	u32 iface_cnt;
	u32 channel;		/* current channel */
	u32 af_sent_channel;	/* channel action frame is sent */
	/* next af subtype to cancel the remained dwell time in rx process */
	u8 next_af_subtype;
#ifdef WL_CFG80211_SYNC_GON
	ulong af_tx_sent_jiffies;
#endif /* WL_CFG80211_SYNC_GON */
	struct escan_info escan_info;   /* escan information */
	bool active_scan;	/* current scan mode */
	bool ibss_starter;	/* indicates this sta is ibss starter */
	bool link_up;		/* link/connection up flag */

	/* indicate whether chip to support power save mode */
	bool pwr_save;
	bool roam_on;		/* on/off switch for self-roaming */
	bool scan_tried;	/* indicates if first scan attempted */
#if defined(BCMSDIO) || defined(BCMPCIE)
	bool wlfc_on;
#endif 
	bool vsdb_mode;
	bool roamoff_on_concurrent;
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
	wl_if_event_info if_event_info;
	struct completion send_af_done;
	struct afx_hdl *afx_hdl;
	struct p2p_info *p2p;
	bool p2p_supported;
	void *btcoex_info;
	struct timer_list scan_timeout;   /* Timer for catch scan event timeout */
#if defined(P2P_IE_MISSING_FIX)
	bool p2p_prb_noti;
#endif
	s32(*state_notifier) (struct bcm_cfg80211 *cfg,
		struct net_info *_net_info, enum wl_status state, bool set);
	unsigned long interrested_state;
	wlc_ssid_t hostapd_ssid;
#ifdef WL11U
	bool wl11u;
	u8 iw_ie[IW_IES_MAX_BUF_LEN];
	u32 iw_ie_len;
#endif /* WL11U */
	bool sched_scan_running;	/* scheduled scan req status */
#ifdef WL_SCHED_SCAN
	struct cfg80211_sched_scan_request *sched_scan_req;	/* scheduled scan req */
#endif /* WL_SCHED_SCAN */
	bool scan_suppressed;
	struct timer_list scan_supp_timer;
	struct work_struct wlan_work;
	struct mutex event_sync;	/* maily for up/down synchronization */
	bool disable_roam_event;
	struct delayed_work pm_enable_work;
	struct mutex pm_sync;	/* mainly for pm work synchronization */

	vndr_ie_setbuf_t *ibss_vsie;	/* keep the VSIE for IBSS */
	int ibss_vsie_len;
	u32 rmc_event_pid;
	u32 rmc_event_seq;
#ifdef WLAIBSS_MCHAN
	struct ether_addr ibss_if_addr;
	bcm_struct_cfgdev *ibss_cfgdev; /* For AIBSS */
#endif /* WLAIBSS_MCHAN */
	bcm_struct_cfgdev *bss_cfgdev;  /* For DUAL STA/STA+AP */
	s32 cfgdev_bssidx;
	bool bss_pending_op;		/* indicate where there is a pending IF operation */
	int roam_offload;
#ifdef WL_NAN
	bool nan_enable;
	bool nan_running;
#endif /* WL_NAN */
#ifdef WL_CFG80211_P2P_DEV_IF
	bool down_disc_if;
#endif /* WL_CFG80211_P2P_DEV_IF */
#ifdef P2PLISTEN_AP_SAMECHN
	bool p2p_resp_apchn_status;
#endif /* P2PLISTEN_AP_SAMECHN */
	struct wl_wsec_key wep_key;
#ifdef WLTDLS
	u8 *tdls_mgmt_frame;
	u32 tdls_mgmt_frame_len;
	s32 tdls_mgmt_freq;
#endif /* WLTDLS */
	bool need_wait_afrx;
#ifdef QOS_MAP_SET
	uint8	 *up_table;	/* user priority table, size is UP_TABLE_MAX */
#endif /* QOS_MAP_SET */
	struct ether_addr last_roamed_addr;
#ifdef DHD_LOSSLESS_ROAMING
	struct timer_list roam_timeout;   /* Timer for catch roam timeout */
#endif
	bool rcc_enabled;	/* flag for Roam channel cache feature */
#if defined(DHD_ENABLE_BIGDATA_LOGGING)
	char bss_info[GET_BSS_INFO_LEN];
	wl_event_msg_t event_auth_assoc;
	u32 assoc_reject_status;
	u32 roam_count;
#endif /* DHD_ENABLE_BIGDATA_LOGGING */
	u16 ap_oper_channel;
	bool revert_ndo_disable;
#if defined(SUPPORT_RANDOM_MAC_SCAN)
	bool random_mac_enabled;
#endif /* SUPPORT_RANDOM_MAC_SCAN */
	int p2p_disconnected; // terence 20130703: Fix for wrong group_capab (timing issue)
	struct ether_addr disconnected_bssid;
};

#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))

#define BCM_LIST_FOR_EACH_ENTRY_SAFE(pos, next, head, member) \
_Pragma("GCC diagnostic push") \
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"") \
list_for_each_entry_safe((pos), (next), (head), member) \
_Pragma("GCC diagnostic pop") \

#else
#define BCM_LIST_FOR_EACH_ENTRY_SAFE(pos, next, head, member) \
list_for_each_entry_safe((pos), (next), (head), member) \

#endif /* STRICT_GCC_WARNINGS */

static inline struct wl_bss_info *next_bss(struct wl_scan_results *list, struct wl_bss_info *bss)
{
	return bss = bss ?
		(struct wl_bss_info *)((uintptr) bss + dtoh32(bss->length)) : list->bss_info;
}

static inline void
wl_probe_wdev_all(struct bcm_cfg80211 *cfg)
{
	struct net_info *_net_info, *next;
	unsigned long int flags;
	int idx = 0;
	spin_lock_irqsave(&cfg->net_list_sync, flags);
	BCM_LIST_FOR_EACH_ENTRY_SAFE(_net_info, next,
		&cfg->net_list, list) {
		WL_ERR(("%s: net_list[%d] bssidx: %d, "
			"ndev: %p, wdev: %p \n", __FUNCTION__,
			idx++, _net_info->bssidx,
			_net_info->ndev, _net_info->wdev));
	}
	spin_unlock_irqrestore(&cfg->net_list_sync, flags);
	return;
}

static inline struct net_info *
wl_get_netinfo_by_bssidx(struct bcm_cfg80211 *cfg, s32 bssidx)
{
	struct net_info *_net_info, *next, *info = NULL;
	unsigned long int flags;

	spin_lock_irqsave(&cfg->net_list_sync, flags);
	BCM_LIST_FOR_EACH_ENTRY_SAFE(_net_info, next, &cfg->net_list, list) {
		if ((bssidx >= 0) && (_net_info->bssidx == bssidx)) {
			info = _net_info;
			break;
		}
	}
	spin_unlock_irqrestore(&cfg->net_list_sync, flags);
	return info;
}

static inline void
wl_dealloc_netinfo_by_wdev(struct bcm_cfg80211 *cfg, struct wireless_dev *wdev)
{
	struct net_info *_net_info, *next;
	unsigned long int flags;

#ifdef DHD_IFDEBUG
	WL_ERR(("dealloc_netinfo enter wdev=%p \n", wdev));
#endif
	spin_lock_irqsave(&cfg->net_list_sync, flags);
	BCM_LIST_FOR_EACH_ENTRY_SAFE(_net_info, next, &cfg->net_list, list) {
		if (wdev && (_net_info->wdev == wdev)) {
			wl_cfgbss_t *bss = &_net_info->bss;

			kfree(bss->wpa_ie);
			bss->wpa_ie = NULL;
			kfree(bss->rsn_ie);
			bss->rsn_ie = NULL;
			kfree(bss->wps_ie);
			bss->wps_ie = NULL;
			list_del(&_net_info->list);
			cfg->iface_cnt--;
			kfree(_net_info);
		}
	}
	spin_unlock_irqrestore(&cfg->net_list_sync, flags);
#ifdef DHD_IFDEBUG
	WL_ERR(("dealloc_netinfo exit iface_cnt=%d \n", cfg->iface_cnt));
#endif
}

static inline s32
wl_alloc_netinfo(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	struct wireless_dev * wdev, s32 mode, bool pm_block, u8 bssidx)
{
	struct net_info *_net_info;
	s32 err = 0;
	unsigned long int flags;
#ifdef DHD_IFDEBUG
	WL_ERR(("alloc_netinfo enter bssidx=%d wdev=%p ndev=%p\n", bssidx, wdev, ndev));
#endif
	/* Check whether there is any duplicate entry for the
	 *  same bssidx *
	 */
	if ((_net_info = wl_get_netinfo_by_bssidx(cfg, bssidx))) {
		/* We have a duplicate entry for the same bssidx
		 * already present which shouldn't have been the case.
		 * Attempt recovery.
		 */
		WL_ERR(("Duplicate entry for bssidx=%d present\n", bssidx));
		wl_probe_wdev_all(cfg);
#ifdef DHD_DEBUG
		ASSERT(0);
#endif /* DHD_DEBUG */
		WL_ERR(("Removing the Dup entry for bssidx=%d \n", bssidx));
		wl_dealloc_netinfo_by_wdev(cfg, _net_info->wdev);
	}
	if (cfg->iface_cnt == IFACE_MAX_CNT)
		return -ENOMEM;
	_net_info = kzalloc(sizeof(struct net_info), GFP_KERNEL);
	if (!_net_info)
		err = -ENOMEM;
	else {
		_net_info->mode = mode;
		_net_info->ndev = ndev;
		_net_info->wdev = wdev;
		_net_info->pm_restore = 0;
		_net_info->pm = 0;
		_net_info->pm_block = pm_block;
		_net_info->roam_off = WL_INVALID;
		_net_info->bssidx = bssidx;
		spin_lock_irqsave(&cfg->net_list_sync, flags);
		cfg->iface_cnt++;
		list_add(&_net_info->list, &cfg->net_list);
		spin_unlock_irqrestore(&cfg->net_list_sync, flags);
	}
#ifdef DHD_IFDEBUG
	WL_ERR(("alloc_netinfo exit iface_cnt=%d \n", cfg->iface_cnt));
#endif
	return err;
}

static inline void
wl_delete_all_netinfo(struct bcm_cfg80211 *cfg)
{
	struct net_info *_net_info, *next;
	unsigned long int flags;

	spin_lock_irqsave(&cfg->net_list_sync, flags);
	BCM_LIST_FOR_EACH_ENTRY_SAFE(_net_info, next, &cfg->net_list, list) {
		wl_cfgbss_t *bss = &_net_info->bss;

		kfree(bss->wpa_ie);
		bss->wpa_ie = NULL;
		kfree(bss->rsn_ie);
		bss->rsn_ie = NULL;
		kfree(bss->wps_ie);
		bss->wps_ie = NULL;
		list_del(&_net_info->list);
		if (_net_info->wdev)
			kfree(_net_info->wdev);
		kfree(_net_info);
	}
	cfg->iface_cnt = 0;
	spin_unlock_irqrestore(&cfg->net_list_sync, flags);
}
static inline u32
wl_get_status_all(struct bcm_cfg80211 *cfg, s32 status)

{
	struct net_info *_net_info, *next;
	u32 cnt = 0;
	unsigned long int flags;

	spin_lock_irqsave(&cfg->net_list_sync, flags);
	BCM_LIST_FOR_EACH_ENTRY_SAFE(_net_info, next, &cfg->net_list, list) {
		if (_net_info->ndev &&
			test_bit(status, &_net_info->sme_state))
			cnt++;
	}
	spin_unlock_irqrestore(&cfg->net_list_sync, flags);
	return cnt;
}
static inline void
wl_set_status_all(struct bcm_cfg80211 *cfg, s32 status, u32 op)
{
	struct net_info *_net_info, *next;
	unsigned long int flags;

	spin_lock_irqsave(&cfg->net_list_sync, flags);
	BCM_LIST_FOR_EACH_ENTRY_SAFE(_net_info, next, &cfg->net_list, list) {
		switch (op) {
			case 1:
				break; /* set all status is not allowed */
			case 2:
				/*
				 * Release the spinlock before calling notifier. Else there
				 * will be nested calls
				 */
				spin_unlock_irqrestore(&cfg->net_list_sync, flags);
				clear_bit(status, &_net_info->sme_state);
				if (cfg->state_notifier &&
					test_bit(status, &(cfg->interrested_state)))
					cfg->state_notifier(cfg, _net_info, status, false);
				return;
			case 4:
				break; /* change all status is not allowed */
			default:
				break; /* unknown operation */
		}
	}
	spin_unlock_irqrestore(&cfg->net_list_sync, flags);
}
static inline void
wl_set_status_by_netdev(struct bcm_cfg80211 *cfg, s32 status,
	struct net_device *ndev, u32 op)
{

	struct net_info *_net_info, *next;
	unsigned long int flags;

	spin_lock_irqsave(&cfg->net_list_sync, flags);
	BCM_LIST_FOR_EACH_ENTRY_SAFE(_net_info, next, &cfg->net_list, list) {
		if (ndev && (_net_info->ndev == ndev)) {
			switch (op) {
				case 1:
					/*
					 * Release the spinlock before calling notifier. Else there
					 * will be nested calls
					 */
					spin_unlock_irqrestore(&cfg->net_list_sync, flags);
					set_bit(status, &_net_info->sme_state);
					if (cfg->state_notifier &&
						test_bit(status, &(cfg->interrested_state)))
						cfg->state_notifier(cfg, _net_info, status, true);
					return;
				case 2:
					/*
					 * Release the spinlock before calling notifier. Else there
					 * will be nested calls
					 */
					spin_unlock_irqrestore(&cfg->net_list_sync, flags);
					clear_bit(status, &_net_info->sme_state);
					if (cfg->state_notifier &&
						test_bit(status, &(cfg->interrested_state)))
						cfg->state_notifier(cfg, _net_info, status, false);
					return;
				case 4:
					change_bit(status, &_net_info->sme_state);
					break;
			}
		}

	}
	spin_unlock_irqrestore(&cfg->net_list_sync, flags);

}

static inline wl_cfgbss_t *
wl_get_cfgbss_by_wdev(struct bcm_cfg80211 *cfg,
	struct wireless_dev *wdev)
{
	struct net_info *_net_info, *next;
	wl_cfgbss_t *bss = NULL;
	unsigned long int flags;

	spin_lock_irqsave(&cfg->net_list_sync, flags);
	BCM_LIST_FOR_EACH_ENTRY_SAFE(_net_info, next, &cfg->net_list, list) {
		if (wdev && (_net_info->wdev == wdev)) {
			bss = &_net_info->bss;
			break;
		}
	}

	spin_unlock_irqrestore(&cfg->net_list_sync, flags);
	return bss;
}

static inline u32
wl_get_status_by_netdev(struct bcm_cfg80211 *cfg, s32 status,
	struct net_device *ndev)
{
	struct net_info *_net_info, *next;
	u32 stat = 0;
	unsigned long int flags;

	spin_lock_irqsave(&cfg->net_list_sync, flags);
	BCM_LIST_FOR_EACH_ENTRY_SAFE(_net_info, next, &cfg->net_list, list) {
		if (ndev && (_net_info->ndev == ndev)) {
			stat = test_bit(status, &_net_info->sme_state);
			break;
		}
	}
	spin_unlock_irqrestore(&cfg->net_list_sync, flags);
	return stat;
}

static inline s32
wl_get_mode_by_netdev(struct bcm_cfg80211 *cfg, struct net_device *ndev)
{
	struct net_info *_net_info, *next;
	s32 mode = -1;
	unsigned long int flags;

	spin_lock_irqsave(&cfg->net_list_sync, flags);
	BCM_LIST_FOR_EACH_ENTRY_SAFE(_net_info, next, &cfg->net_list, list) {
		if (ndev && (_net_info->ndev == ndev)) {
			mode = _net_info->mode;
			break;
		}
	}
	spin_unlock_irqrestore(&cfg->net_list_sync, flags);
	return mode;
}

static inline void
wl_set_mode_by_netdev(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	s32 mode)
{
	struct net_info *_net_info, *next;
	unsigned long int flags;

	spin_lock_irqsave(&cfg->net_list_sync, flags);
	BCM_LIST_FOR_EACH_ENTRY_SAFE(_net_info, next, &cfg->net_list, list) {
		if (ndev && (_net_info->ndev == ndev))
			_net_info->mode = mode;
	}
	spin_unlock_irqrestore(&cfg->net_list_sync, flags);
}

static inline s32
wl_get_bssidx_by_wdev(struct bcm_cfg80211 *cfg, struct wireless_dev *wdev)
{
	struct net_info *_net_info, *next;
	s32 bssidx = -1;
	unsigned long int flags;

	spin_lock_irqsave(&cfg->net_list_sync, flags);
	BCM_LIST_FOR_EACH_ENTRY_SAFE(_net_info, next, &cfg->net_list, list) {
		if (_net_info->wdev && (_net_info->wdev == wdev)) {
			bssidx = _net_info->bssidx;
			break;
		}
	}
	spin_unlock_irqrestore(&cfg->net_list_sync, flags);
	return bssidx;
}

static inline struct wireless_dev *
wl_get_wdev_by_bssidx(struct bcm_cfg80211 *cfg, s32 bssidx)
{
	struct net_info *_net_info, *next;
	struct wireless_dev *wdev = NULL;
	unsigned long int flags;

	if (bssidx < 0)
		return NULL;
	spin_lock_irqsave(&cfg->net_list_sync, flags);
	BCM_LIST_FOR_EACH_ENTRY_SAFE(_net_info, next, &cfg->net_list, list) {
		if (_net_info->bssidx == bssidx) {
				wdev = _net_info->wdev;
				break;
		}
	}
	spin_unlock_irqrestore(&cfg->net_list_sync, flags);
	return wdev;
}

static inline struct wl_profile *
wl_get_profile_by_netdev(struct bcm_cfg80211 *cfg, struct net_device *ndev)
{
	struct net_info *_net_info, *next;
	struct wl_profile *prof = NULL;
	unsigned long int flags;

	spin_lock_irqsave(&cfg->net_list_sync, flags);
	BCM_LIST_FOR_EACH_ENTRY_SAFE(_net_info, next, &cfg->net_list, list) {
		if (ndev && (_net_info->ndev == ndev)) {
			prof = &_net_info->profile;
			break;
		}
	}
	spin_unlock_irqrestore(&cfg->net_list_sync, flags);
	return prof;
}
static inline struct net_info *
wl_get_netinfo_by_netdev(struct bcm_cfg80211 *cfg, struct net_device *ndev)
{
	struct net_info *_net_info, *next, *info = NULL;
	unsigned long int flags;

	spin_lock_irqsave(&cfg->net_list_sync, flags);
	BCM_LIST_FOR_EACH_ENTRY_SAFE(_net_info, next, &cfg->net_list, list) {
		if (ndev && (_net_info->ndev == ndev)) {
			info = _net_info;
			break;
		}
	}
	spin_unlock_irqrestore(&cfg->net_list_sync, flags);
	return info;
}

static inline struct net_info *
wl_get_netinfo_by_wdev(struct bcm_cfg80211 *cfg, struct wireless_dev *wdev)
{
	struct net_info *_net_info, *next, *info = NULL;
	unsigned long int flags;

	spin_lock_irqsave(&cfg->net_list_sync, flags);
	BCM_LIST_FOR_EACH_ENTRY_SAFE(_net_info, next, &cfg->net_list, list) {
		if (wdev && (_net_info->wdev == wdev)) {
			info = _net_info;
			break;
		}
	}
	spin_unlock_irqrestore(&cfg->net_list_sync, flags);
	return info;
}

#define is_p2p_group_iface(wdev) (((wdev->iftype == NL80211_IFTYPE_P2P_GO) || \
		(wdev->iftype == NL80211_IFTYPE_P2P_CLIENT)) ? 1 : 0)
#define bcmcfg_to_wiphy(cfg) (cfg->wdev->wiphy)
#define bcmcfg_to_prmry_ndev(cfg) (cfg->wdev->netdev)
#define bcmcfg_to_prmry_wdev(cfg) (cfg->wdev)
#define bcmcfg_to_p2p_wdev(cfg) (cfg->p2p_wdev)
#define ndev_to_wl(n) (wdev_to_wl(n->ieee80211_ptr))
#define ndev_to_wdev(ndev) (ndev->ieee80211_ptr)
#define wdev_to_ndev(wdev) (wdev->netdev)

#if defined(WL_ENABLE_P2P_IF)
#define ndev_to_wlc_ndev(ndev, cfg)	((ndev == cfg->p2p_net) ? \
	bcmcfg_to_prmry_ndev(cfg) : ndev)
#else
#define ndev_to_wlc_ndev(ndev, cfg)	(ndev)
#endif /* WL_ENABLE_P2P_IF */

#define wdev_to_wlc_ndev(wdev, cfg)	\
	(wdev_to_ndev(wdev) ? \
	wdev_to_ndev(wdev) : bcmcfg_to_prmry_ndev(cfg))
#if defined(WL_CFG80211_P2P_DEV_IF)
#define cfgdev_to_wlc_ndev(cfgdev, cfg)	wdev_to_wlc_ndev(cfgdev, cfg)
#define bcmcfg_to_prmry_cfgdev(cfgdev, cfg) bcmcfg_to_prmry_wdev(cfg)
#elif defined(WL_ENABLE_P2P_IF)
#define cfgdev_to_wlc_ndev(cfgdev, cfg)	ndev_to_wlc_ndev(cfgdev, cfg)
#define bcmcfg_to_prmry_cfgdev(cfgdev, cfg) bcmcfg_to_prmry_ndev(cfg)
#else
#define cfgdev_to_wlc_ndev(cfgdev, cfg)	(cfgdev)
#define bcmcfg_to_prmry_cfgdev(cfgdev, cfg) (cfgdev)
#endif /* WL_CFG80211_P2P_DEV_IF */

#if defined(WL_CFG80211_P2P_DEV_IF)
#define cfgdev_to_wdev(cfgdev)	(cfgdev)
#define ndev_to_cfgdev(ndev)	ndev_to_wdev(ndev)
#define cfgdev_to_ndev(cfgdev)	(cfgdev ? (cfgdev->netdev) : NULL)
#define wdev_to_cfgdev(cfgdev)	(cfgdev)
#define discover_cfgdev(cfgdev, cfg) (cfgdev->iftype == NL80211_IFTYPE_P2P_DEVICE)
#else
#define cfgdev_to_wdev(cfgdev)	(cfgdev->ieee80211_ptr)
#define wdev_to_cfgdev(cfgdev)	cfgdev ? (cfgdev->netdev) : NULL
#define ndev_to_cfgdev(ndev)	(ndev)
#define cfgdev_to_ndev(cfgdev)	(cfgdev)
#define discover_cfgdev(cfgdev, cfg) (cfgdev == cfg->p2p_net)
#endif /* WL_CFG80211_P2P_DEV_IF */

#if defined(WL_CFG80211_P2P_DEV_IF)
#define scan_req_match(cfg)	(((cfg) && (cfg->scan_request) && \
	(cfg->scan_request->wdev == cfg->p2p_wdev)) ? true : false)
#elif defined(WL_ENABLE_P2P_IF)
#define scan_req_match(cfg)	(((cfg) && (cfg->scan_request) && \
	(cfg->scan_request->dev == cfg->p2p_net)) ? true : false)
#else
#define scan_req_match(cfg)	(((cfg) && p2p_is_on(cfg) && p2p_scan(cfg)) ? \
	true : false)
#endif /* WL_CFG80211_P2P_DEV_IF */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0))
#define scan_req_iftype(req) (req->dev->ieee80211_ptr->iftype)
#else
#define scan_req_iftype(req) (req->wdev->iftype)
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0) */

#define wl_to_sr(w) (w->scan_req_int)
#if defined(STATIC_WL_PRIV_STRUCT)
#define wl_to_ie(w) (w->ie)
#define wl_to_conn(w) (w->conn_info)
#else
#define wl_to_ie(w) (&w->ie)
#define wl_to_conn(w) (&w->conn_info)
#endif
#define wiphy_from_scan(w) (w->escan_info.wiphy)
#define wl_get_drv_status_all(cfg, stat) \
	(wl_get_status_all(cfg, WL_STATUS_ ## stat))
#define wl_get_drv_status(cfg, stat, ndev)  \
	(wl_get_status_by_netdev(cfg, WL_STATUS_ ## stat, ndev))
#define wl_set_drv_status(cfg, stat, ndev)  \
	(wl_set_status_by_netdev(cfg, WL_STATUS_ ## stat, ndev, 1))
#define wl_clr_drv_status(cfg, stat, ndev)  \
	(wl_set_status_by_netdev(cfg, WL_STATUS_ ## stat, ndev, 2))
#define wl_clr_drv_status_all(cfg, stat)  \
	(wl_set_status_all(cfg, WL_STATUS_ ## stat, 2))
#define wl_chg_drv_status(cfg, stat, ndev)  \
	(wl_set_status_by_netdev(cfg, WL_STATUS_ ## stat, ndev, 4))

#define for_each_bss(list, bss, __i)	\
	for (__i = 0; __i < list->count && __i < WL_AP_MAX; __i++, bss = next_bss(list, bss))

#define for_each_ndev(cfg, iter, next) \
	list_for_each_entry_safe(iter, next, &cfg->net_list, list)

/* In case of WPS from wpa_supplicant, pairwise siute and group suite is 0.
 * In addtion to that, wpa_version is WPA_VERSION_1
 */
#define is_wps_conn(_sme) \
	((wl_cfgp2p_find_wpsie((u8 *)_sme->ie, _sme->ie_len) != NULL) && \
	 (!_sme->crypto.n_ciphers_pairwise) && \
	 (!_sme->crypto.cipher_group))

#define IS_AKM_SUITE_FT(sec) false

#define IS_AKM_SUITE_CCKM(sec) false

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0))
#define STA_INFO_BIT(info) (1ul << NL80211_STA_ ## info)
#define strnicmp(str1, str2, len) strncasecmp((str1), (str2), (len))
#else
#define STA_INFO_BIT(info) (STATION_ ## info)
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)) */

extern s32 wl_cfg80211_attach(struct net_device *ndev, void *context);
extern s32 wl_cfg80211_attach_post(struct net_device *ndev);
extern void wl_cfg80211_detach(void *para);

extern void wl_cfg80211_event(struct net_device *ndev, const wl_event_msg_t *e,
            void *data);
void wl_cfg80211_set_parent_dev(void *dev);
struct device *wl_cfg80211_get_parent_dev(void);

/* clear IEs */
extern s32 wl_cfg80211_clear_mgmt_vndr_ies(struct bcm_cfg80211 *cfg);
extern s32 wl_cfg80211_clear_per_bss_ies(struct bcm_cfg80211 *cfg, s32 bssidx);

extern s32 wl_cfg80211_up(void *para);
extern s32 wl_cfg80211_down(void *para);
extern s32 wl_cfg80211_notify_ifadd(int ifidx, char *name, uint8 *mac, uint8 bssidx);
extern s32 wl_cfg80211_notify_ifdel(int ifidx, char *name, uint8 *mac, uint8 bssidx);
extern s32 wl_cfg80211_notify_ifchange(int ifidx, char *name, uint8 *mac, uint8 bssidx);
extern struct net_device* wl_cfg80211_allocate_if(struct bcm_cfg80211 *cfg, int ifidx, char *name,
	uint8 *mac, uint8 bssidx, char *dngl_name);
extern int wl_cfg80211_register_if(struct bcm_cfg80211 *cfg, int ifidx, struct net_device* ndev);
extern int wl_cfg80211_remove_if(struct bcm_cfg80211 *cfg, int ifidx, struct net_device* ndev);
extern int wl_cfg80211_scan_stop(bcm_struct_cfgdev *cfgdev);
extern bool wl_cfg80211_is_concurrent_mode(void);
extern void* wl_cfg80211_get_dhdp(void);
extern bool wl_cfg80211_is_p2p_active(void);
extern bool wl_cfg80211_is_roam_offload(void);
extern bool wl_cfg80211_is_event_from_connected_bssid(const wl_event_msg_t *e, int ifidx);
extern void wl_cfg80211_dbg_level(u32 level);
extern s32 wl_cfg80211_get_p2p_dev_addr(struct net_device *net, struct ether_addr *p2pdev_addr);
extern s32 wl_cfg80211_set_p2p_noa(struct net_device *net, char* buf, int len);
extern s32 wl_cfg80211_get_p2p_noa(struct net_device *net, char* buf, int len);
extern s32 wl_cfg80211_set_wps_p2p_ie(struct net_device *net, char *buf, int len,
	enum wl_management_type type);
extern s32 wl_cfg80211_set_p2p_ps(struct net_device *net, char* buf, int len);
extern s32 wl_cfg80211_set_p2p_ecsa(struct net_device *net, char* buf, int len);
#ifdef WL11ULB
extern s32 wl_cfg80211_set_ulb_mode(struct net_device *dev, int mode);
extern s32 wl_cfg80211_set_ulb_bw(struct net_device *dev,
	u32 ulb_bw,  char *ifname);
#endif /* WL11ULB */
#ifdef P2PLISTEN_AP_SAMECHN
extern s32 wl_cfg80211_set_p2p_resp_ap_chn(struct net_device *net, s32 enable);
#endif /* P2PLISTEN_AP_SAMECHN */

/* btcoex functions */
void* wl_cfg80211_btcoex_init(struct net_device *ndev);
void wl_cfg80211_btcoex_deinit(void);

#ifdef WL_SUPPORT_AUTO_CHANNEL
#define CHANSPEC_BUF_SIZE	1024
#define CHAN_SEL_IOCTL_DELAY	300
#define CHAN_SEL_RETRY_COUNT	15
#define CHANNEL_IS_RADAR(channel)	(((channel & WL_CHAN_RADAR) || \
	(channel & WL_CHAN_PASSIVE)) ? true : false)
#define CHANNEL_IS_2G(channel)	(((channel >= 1) && (channel <= 14)) ? \
	true : false)
#define CHANNEL_IS_5G(channel)	(((channel >= 36) && (channel <= 165)) ? \
	true : false)
extern s32 wl_cfg80211_get_best_channels(struct net_device *dev, char* command,
	int total_len);
#endif /* WL_SUPPORT_AUTO_CHANNEL */
extern int wl_cfg80211_ether_atoe(const char *a, struct ether_addr *n);
extern int wl_cfg80211_hang(struct net_device *dev, u16 reason);
extern s32 wl_mode_to_nl80211_iftype(s32 mode);
int wl_cfg80211_do_driver_init(struct net_device *net);
void wl_cfg80211_enable_trace(u32 level);
extern s32 wl_update_wiphybands(struct bcm_cfg80211 *cfg, bool notify);
extern s32 wl_cfg80211_if_is_group_owner(void);
extern chanspec_t wl_chspec_host_to_driver(chanspec_t chanspec);
extern chanspec_t wl_ch_host_to_driver(s32 bssidx, u16 channel);
extern s32 wl_set_tx_power(struct net_device *dev,
	enum nl80211_tx_power_setting type, s32 dbm);
extern s32 wl_get_tx_power(struct net_device *dev, s32 *dbm);
extern s32 wl_add_remove_eventmsg(struct net_device *ndev, u16 event, bool add);
extern void wl_stop_wait_next_action_frame(struct bcm_cfg80211 *cfg, struct net_device *ndev);
extern void wl_cfg80211_add_to_eventbuffer(wl_eventmsg_buf_t *ev, u16 event, bool set);
extern s32 wl_cfg80211_apply_eventbuffer(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, wl_eventmsg_buf_t *ev);
extern void get_primary_mac(struct bcm_cfg80211 *cfg, struct ether_addr *mac);
extern void wl_cfg80211_update_power_mode(struct net_device *dev);
extern void wl_cfg80211_set_passive_scan(struct net_device *dev, char *command);
extern void wl_terminate_event_handler(void);
#if defined(DHD_ENABLE_BIGDATA_LOGGING)
extern s32 wl_cfg80211_get_bss_info(struct net_device *dev, char* cmd, int total_len);
extern s32 wl_cfg80211_get_connect_failed_status(struct net_device *dev, char* cmd, int total_len);
#endif /* DHD_ENABLE_BIGDATA_LOGGING */

#define SCAN_BUF_CNT	2
#define SCAN_BUF_NEXT	1
#define WL_SCANTYPE_LEGACY	0x1
#define WL_SCANTYPE_P2P		0x2
#define wl_escan_set_sync_id(a, b) ((a) = htod16(0x1234))
#define wl_escan_set_type(a, b)
#define wl_escan_get_buf(a, b) ((wl_scan_results_t *) (a)->escan_info.escan_buf)
#define wl_escan_check_sync_id(a, b, c) 0
#define wl_escan_print_sync_id(a, b, c)
#define wl_escan_increment_sync_id(a, b)
#define wl_escan_init_sync_id(a)
extern void wl_cfg80211_ibss_vsie_set_buffer(vndr_ie_setbuf_t *ibss_vsie, int ibss_vsie_len);
extern s32 wl_cfg80211_ibss_vsie_delete(struct net_device *dev);
extern void wl_cfg80211_set_rmc_pid(int pid);
extern int wl_cfg80211_set_mgmt_vndr_ies(struct bcm_cfg80211 *cfg,
	bcm_struct_cfgdev *cfgdev, s32 bssidx, s32 pktflag,
	const u8 *vndr_ie, u32 vndr_ie_len);


/* Action frame specific functions */
extern u8 wl_get_action_category(void *frame, u32 frame_len);
extern int wl_get_public_action(void *frame, u32 frame_len, u8 *ret_action);

#ifdef WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST
struct net_device *wl_cfg80211_get_remain_on_channel_ndev(struct bcm_cfg80211 *cfg);
#endif /* WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST */

#ifdef WL_SUPPORT_ACS
#define ACS_MSRMNT_DELAY 1000 /* dump_obss delay in ms */
#define IOCTL_RETRY_COUNT 5
#define CHAN_NOISE_DUMMY -80
#define OBSS_TOKEN_IDX 15
#define IBSS_TOKEN_IDX 15
#define TX_TOKEN_IDX 14
#define CTG_TOKEN_IDX 13
#define PKT_TOKEN_IDX 15
#define IDLE_TOKEN_IDX 12
#endif /* WL_SUPPORT_ACS */

extern int wl_cfg80211_get_ioctl_version(void);
extern int wl_cfg80211_enable_roam_offload(struct net_device *dev, int enable);
extern s32 wl_cfg80211_dfs_ap_move(struct net_device *ndev, char *data,
		char *command, int total_len);
extern s32 wl_cfg80211_wbtext_config(struct net_device *ndev, char *data,
		char *command, int total_len);
extern int wl_cfg80211_wbtext_weight_config(struct net_device *ndev, char *data,
		char *command, int total_len);
extern int wl_cfg80211_wbtext_table_config(struct net_device *ndev, char *data,
		char *command, int total_len);
extern s32 wl_cfg80211_wbtext_delta_config(struct net_device *ndev, char *data,
		char *command, int total_len);
extern s32 wl_cfg80211_get_chanspecs_2g(struct net_device *ndev,
		void *buf, s32 buflen);
extern s32 wl_cfg80211_get_chanspecs_5g(struct net_device *ndev,
		void *buf, s32 buflen);
#if defined(WL_VIRTUAL_APSTA)
extern int wl_cfg80211_interface_create(struct net_device *dev, char *name);
extern int wl_cfg80211_interface_delete(struct net_device *dev, char *name);
#endif /* defined (WL_VIRTUAL_APSTA) */

#ifdef WL_NAN
extern int wl_cfg80211_nan_cmd_handler(struct net_device *ndev, char *cmd,
	int cmd_len);
#endif /* WL_NAN */

#ifdef WL_CFG80211_P2P_DEV_IF
extern void wl_cfg80211_del_p2p_wdev(void);
#endif /* WL_CFG80211_P2P_DEV_IF */

#if defined(WL_SUPPORT_AUTO_CHANNEL)
extern int wl_cfg80211_set_spect(struct net_device *dev, int spect);
extern int wl_cfg80211_get_sta_channel(void);
#endif /* WL_SUPPORT_AUTO_CHANNEL */

#ifdef P2P_LISTEN_OFFLOADING
extern s32 wl_cfg80211_p2plo_listen_start(struct net_device *dev, u8 *buf, int len);
extern s32 wl_cfg80211_p2plo_listen_stop(struct net_device *dev);
#endif /* P2P_LISTEN_OFFLOADING */

#define RETURN_EIO_IF_NOT_UP(wlpriv)                        \
do {                                    \
	struct net_device *checkSysUpNDev = bcmcfg_to_prmry_ndev(wlpriv);           \
	if (unlikely(!wl_get_drv_status(wlpriv, READY, checkSysUpNDev))) {  \
		WL_INFORM(("device is not ready\n"));           \
		return -EIO;                        \
	}                               \
} while (0)

#ifdef QOS_MAP_SET
extern uint8 *wl_get_up_table(void);
#endif /* QOS_MAP_SET */

#define P2PO_COOKIE     65535
u64 wl_cfg80211_get_new_roc_id(struct bcm_cfg80211 *cfg);
#if defined(SUPPORT_RANDOM_MAC_SCAN)
int wl_cfg80211_set_random_mac(struct net_device *dev, bool enable);
int wl_cfg80211_random_mac_enable(struct net_device *dev);
int wl_cfg80211_random_mac_disable(struct net_device *dev);
#endif /* SUPPORT_RANDOM_MAC_SCAN */
int wl_cfg80211_iface_count(void);
int wl_check_dongle_idle(struct wiphy *wiphy);
#endif /* _wl_cfg80211_h_ */
