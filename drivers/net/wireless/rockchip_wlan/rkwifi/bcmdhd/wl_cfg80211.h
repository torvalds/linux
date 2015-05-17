/*
 * Linux cfg80211 driver
 *
 * $Copyright Open Broadcom Corporation$
 *
 * $Id: wl_cfg80211.h 505096 2014-09-26 12:49:04Z $
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

#include <wl_cfgp2p.h>

struct wl_conf;
struct wl_iface;
struct bcm_cfg80211;
struct wl_security;
struct wl_ibss;


#define htod32(i) (i)
#define htod16(i) (i)
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

/* 0 invalidates all debug messages.  default is 1 */
#define WL_DBG_LEVEL 0xFF

#define CFG80211_ERROR_TEXT		"CFG80211-ERROR) "

#define MAX_WAIT_TIME 1500
#define DNGL_FUNC(func, parameters) func parameters;

#define PM_BLOCK 1
#define PM_ENABLE 0

#if defined(DHD_DEBUG)
#define	WL_ERR(args)									\
do {										\
	if (wl_dbg_level & WL_DBG_ERR) {				\
			printk(KERN_INFO CFG80211_ERROR_TEXT "%s : ", __func__);	\
			printk args;						\
		}								\
} while (0)
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
#define	WL_TRACE_HW4			WL_TRACE
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
#define WL_AP_MAX		256
#define WL_FILE_NAME_MAX	256
#define WL_DWELL_TIME 		200
#define WL_MED_DWELL_TIME       400
#define WL_MIN_DWELL_TIME	100
#define WL_LONG_DWELL_TIME 	1000
#define IFACE_MAX_CNT 		2
#define WL_SCAN_CONNECT_DWELL_TIME_MS 		200
#define WL_SCAN_JOIN_PROBE_INTERVAL_MS 		20
#define WL_SCAN_JOIN_ACTIVE_DWELL_TIME_MS 	320
#define WL_SCAN_JOIN_PASSIVE_DWELL_TIME_MS 	400
#define WL_AF_TX_MAX_RETRY 	5

#define WL_AF_SEARCH_TIME_MAX           450
#define WL_AF_TX_EXTRA_TIME_MAX         200

#define WL_SCAN_TIMER_INTERVAL_MS	10000 /* Scan timeout */
#define WL_CHANNEL_SYNC_RETRY 	5
#define WL_INVALID 		-1

/* Bring down SCB Timeout to 20secs from 60secs default */
#ifndef WL_SCB_TIMEOUT
#define WL_SCB_TIMEOUT 20
#endif

/* SCAN_SUPPRESS timer values in ms */
#define WL_SCAN_SUPPRESS_TIMEOUT 31000 /* default Framwork DHCP timeout is 30 sec */
#define WL_SCAN_SUPPRESS_RETRY 3000

#define WL_PM_ENABLE_TIMEOUT 10000

#ifdef WLAIBSS
/* Custom AIBSS beacon parameters */
#define AIBSS_INITIAL_MIN_BCN_DUR	500
#define AIBSS_MIN_BCN_DUR		5000
#define AIBSS_BCN_FLOOD_DUR		5000
#endif /* WLAIBSS */

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

enum wl_handler_del_type {
	WL_HANDLER_NOTUSE,
	WL_HANDLER_DEL,
	WL_HANDLER_MAINTAIN,
	WL_HANDLER_PEND
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

struct ap_info {
/* Structure to hold WPS, WPA IEs for a AP */
	u8   probe_res_ie[VNDR_IES_MAX_BUF_LEN];
	u8   beacon_ie[VNDR_IES_MAX_BUF_LEN];
	u8   assoc_res_ie[VNDR_IES_MAX_BUF_LEN];
	u32 probe_res_ie_len;
	u32 beacon_ie_len;
	u32 assoc_res_ie_len;
	u8 *wpa_ie;
	u8 *rsn_ie;
	u8 *wps_ie;
	bool security_mode;
};

struct sta_info {
	/* Structure to hold WPS IE for a STA */
	u8  probe_req_ie[VNDR_IES_BUF_LEN];
	u8  assoc_req_ie[VNDR_IES_BUF_LEN];
	u32 probe_req_ie_len;
	u32 assoc_req_ie_len;
};

struct afx_hdl {
	wl_af_params_t *pending_tx_act_frm;
	struct ether_addr	tx_dst_addr;
	struct net_device *dev;
	struct work_struct work;
	u32 bssidx;
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

#ifdef WL_SDO
/* Service discovery */
typedef struct {
	uint8	transaction_id; /* Transaction ID */
	uint8   protocol;       /* Service protocol type */
	uint16  query_len;      /* Length of query */
	uint16  response_len;   /* Length of response */
	uint8   qrbuf[1];
} wl_sd_qr_t;

typedef struct {
	uint16	period;                 /* extended listen period */
	uint16	interval;               /* extended listen interval */
} wl_sd_listen_t;

#define WL_SD_STATE_IDLE 0x0000
#define WL_SD_SEARCH_SVC 0x0001
#define WL_SD_ADV_SVC    0x0002

enum wl_dd_state {
    WL_DD_STATE_IDLE,
    WL_DD_STATE_SEARCH,
    WL_DD_STATE_LISTEN
};

#define MAX_SDO_PROTO_STR_LEN 20
typedef struct wl_sdo_proto {
	char str[MAX_SDO_PROTO_STR_LEN];
	u32 val;
} wl_sdo_proto_t;

typedef struct sd_offload {
	u32 sd_state;
	enum wl_dd_state dd_state;
	wl_sd_listen_t sd_listen;
} sd_offload_t;

typedef struct sdo_event {
	u8 addr[ETH_ALEN];
	uint16	freq;        /* channel Freq */
	uint8	count;       /* Tlv count  */
	uint16	update_ind;
} sdo_event_t;
#endif /* WL_SDO */

#ifdef WL11U
/* Max length of Interworking element */
#define IW_IES_MAX_BUF_LEN 		9
#endif
#ifdef WLFBT
#define FBT_KEYLEN		32
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
	spinlock_t eq_lock;	/* for event queue synchronization */
	spinlock_t cfgdrv_lock;	/* to protect scan status (and others if needed) */
	struct completion act_frm_scan;
	struct completion iface_disable;
	struct completion wait_next_af;
	struct mutex usr_sync;	/* maily for up/down synchronization */
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
	void *pub;
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
	struct ap_info *ap_info;
	struct sta_info *sta_info;
	struct p2p_info *p2p;
	bool p2p_supported;
	void *btcoex_info;
	struct timer_list scan_timeout;   /* Timer for catch scan event timeout */
	s32(*state_notifier) (struct bcm_cfg80211 *cfg,
		struct net_info *_net_info, enum wl_status state, bool set);
	unsigned long interrested_state;
	wlc_ssid_t hostapd_ssid;
#ifdef WL_SDO
	sd_offload_t *sdo;
#endif
#ifdef WL11U
	bool wl11u;
	u8 iw_ie[IW_IES_MAX_BUF_LEN];
	u32 iw_ie_len;
#endif /* WL11U */
	bool sched_scan_running;	/* scheduled scan req status */
#ifdef WL_SCHED_SCAN
	struct cfg80211_sched_scan_request *sched_scan_req;	/* scheduled scan req */
#endif /* WL_SCHED_SCAN */
#ifdef WL_HOST_BAND_MGMT
	u8 curr_band;
#endif /* WL_HOST_BAND_MGMT */
	bool scan_suppressed;
	struct timer_list scan_supp_timer;
	struct work_struct wlan_work;
	struct mutex event_sync;	/* maily for up/down synchronization */
	bool disable_roam_event;
	bool pm_enable_work_on;
	struct delayed_work pm_enable_work;
	vndr_ie_setbuf_t *ibss_vsie;	/* keep the VSIE for IBSS */
	int ibss_vsie_len;
#ifdef WLAIBSS
	u32 aibss_txfail_pid;
	u32 aibss_txfail_seq;
#endif /* WLAIBSS */
	u32 rmc_event_pid;
	u32 rmc_event_seq;
#ifdef WLAIBSS_MCHAN
	struct ether_addr ibss_if_addr;
	bcm_struct_cfgdev *ibss_cfgdev; /* For AIBSS */
#endif /* WLAIBSS_MCHAN */
	bcm_struct_cfgdev *bss_cfgdev;  /* For DUAL STA/STA+AP */
	s32 cfgdev_bssidx;
	bool bss_pending_op;		/* indicate where there is a pending IF operation */
#ifdef WLFBT
	uint8 fbt_key[FBT_KEYLEN];
#endif
	int roam_offload;
	bool nan_running;
#ifdef P2PLISTEN_AP_SAMECHN
	bool p2p_resp_apchn_status;
#endif /* P2PLISTEN_AP_SAMECHN */
#ifdef WLTDLS
	u8 *tdls_mgmt_frame;
	u32 tdls_mgmt_frame_len;
	s32 tdls_mgmt_freq;
#endif /* WLTDLS */
};


static inline struct wl_bss_info *next_bss(struct wl_scan_results *list, struct wl_bss_info *bss)
{
	return bss = bss ?
		(struct wl_bss_info *)((uintptr) bss + dtoh32(bss->length)) : list->bss_info;
}
static inline s32
wl_alloc_netinfo(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	struct wireless_dev * wdev, s32 mode, bool pm_block)
{
	struct net_info *_net_info;
	s32 err = 0;
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
		cfg->iface_cnt++;
		list_add(&_net_info->list, &cfg->net_list);
	}
	return err;
}
static inline void
wl_dealloc_netinfo(struct bcm_cfg80211 *cfg, struct net_device *ndev)
{
	struct net_info *_net_info, *next;

	list_for_each_entry_safe(_net_info, next, &cfg->net_list, list) {
		if (ndev && (_net_info->ndev == ndev)) {
			list_del(&_net_info->list);
			cfg->iface_cnt--;
			kfree(_net_info);
		}
	}

}
static inline void
wl_delete_all_netinfo(struct bcm_cfg80211 *cfg)
{
	struct net_info *_net_info, *next;

	list_for_each_entry_safe(_net_info, next, &cfg->net_list, list) {
		list_del(&_net_info->list);
			if (_net_info->wdev)
				kfree(_net_info->wdev);
			kfree(_net_info);
	}
	cfg->iface_cnt = 0;
}
static inline u32
wl_get_status_all(struct bcm_cfg80211 *cfg, s32 status)

{
	struct net_info *_net_info, *next;
	u32 cnt = 0;
	list_for_each_entry_safe(_net_info, next, &cfg->net_list, list) {
		if (_net_info->ndev &&
			test_bit(status, &_net_info->sme_state))
			cnt++;
	}
	return cnt;
}
static inline void
wl_set_status_all(struct bcm_cfg80211 *cfg, s32 status, u32 op)
{
	struct net_info *_net_info, *next;
	list_for_each_entry_safe(_net_info, next, &cfg->net_list, list) {
		switch (op) {
			case 1:
				return; /* set all status is not allowed */
			case 2:
				clear_bit(status, &_net_info->sme_state);
				if (cfg->state_notifier &&
					test_bit(status, &(cfg->interrested_state)))
					cfg->state_notifier(cfg, _net_info, status, false);
				break;
			case 4:
				return; /* change all status is not allowed */
			default:
				return; /* unknown operation */
		}
	}
}
static inline void
wl_set_status_by_netdev(struct bcm_cfg80211 *cfg, s32 status,
	struct net_device *ndev, u32 op)
{

	struct net_info *_net_info, *next;

	list_for_each_entry_safe(_net_info, next, &cfg->net_list, list) {
		if (ndev && (_net_info->ndev == ndev)) {
			switch (op) {
				case 1:
					set_bit(status, &_net_info->sme_state);
					if (cfg->state_notifier &&
						test_bit(status, &(cfg->interrested_state)))
						cfg->state_notifier(cfg, _net_info, status, true);
					break;
				case 2:
					clear_bit(status, &_net_info->sme_state);
					if (cfg->state_notifier &&
						test_bit(status, &(cfg->interrested_state)))
						cfg->state_notifier(cfg, _net_info, status, false);
					break;
				case 4:
					change_bit(status, &_net_info->sme_state);
					break;
			}
		}

	}

}

static inline u32
wl_get_status_by_netdev(struct bcm_cfg80211 *cfg, s32 status,
	struct net_device *ndev)
{
	struct net_info *_net_info, *next;

	list_for_each_entry_safe(_net_info, next, &cfg->net_list, list) {
				if (ndev && (_net_info->ndev == ndev))
					return test_bit(status, &_net_info->sme_state);
	}
	return 0;
}

static inline s32
wl_get_mode_by_netdev(struct bcm_cfg80211 *cfg, struct net_device *ndev)
{
	struct net_info *_net_info, *next;

	list_for_each_entry_safe(_net_info, next, &cfg->net_list, list) {
				if (ndev && (_net_info->ndev == ndev))
					return _net_info->mode;
	}
	return -1;
}


static inline void
wl_set_mode_by_netdev(struct bcm_cfg80211 *cfg, struct net_device *ndev,
	s32 mode)
{
	struct net_info *_net_info, *next;

	list_for_each_entry_safe(_net_info, next, &cfg->net_list, list) {
				if (ndev && (_net_info->ndev == ndev))
					_net_info->mode = mode;
	}
}
static inline struct wl_profile *
wl_get_profile_by_netdev(struct bcm_cfg80211 *cfg, struct net_device *ndev)
{
	struct net_info *_net_info, *next;

	list_for_each_entry_safe(_net_info, next, &cfg->net_list, list) {
				if (ndev && (_net_info->ndev == ndev))
					return &_net_info->profile;
	}
	return NULL;
}
static inline struct net_info *
wl_get_netinfo_by_netdev(struct bcm_cfg80211 *cfg, struct net_device *ndev)
{
	struct net_info *_net_info, *next;

	list_for_each_entry_safe(_net_info, next, &cfg->net_list, list) {
				if (ndev && (_net_info->ndev == ndev))
					return _net_info;
	}
	return NULL;
}
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

#if defined(WL_CFG80211_P2P_DEV_IF)
#define wdev_to_wlc_ndev(wdev, cfg)	\
	((wdev->iftype == NL80211_IFTYPE_P2P_DEVICE) ? \
	bcmcfg_to_prmry_ndev(cfg) : wdev_to_ndev(wdev))
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
#define ndev_to_cfgdev(ndev)	ndev_to_wdev(ndev)
#define cfgdev_to_ndev(cfgdev)  cfgdev ? (cfgdev->netdev) : NULL
#define discover_cfgdev(cfgdev, cfg) (cfgdev->iftype == NL80211_IFTYPE_P2P_DEVICE)
#else
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
extern s32 wl_cfg80211_attach(struct net_device *ndev, void *context);
extern s32 wl_cfg80211_attach_post(struct net_device *ndev);
extern void wl_cfg80211_detach(void *para);

extern void wl_cfg80211_event(struct net_device *ndev, const wl_event_msg_t *e,
            void *data);
void wl_cfg80211_set_parent_dev(void *dev);
struct device *wl_cfg80211_get_parent_dev(void);

extern s32 wl_cfg80211_up(void *para);
extern s32 wl_cfg80211_down(void *para);
extern s32 wl_cfg80211_notify_ifadd(int ifidx, char *name, uint8 *mac, uint8 bssidx);
extern s32 wl_cfg80211_notify_ifdel(int ifidx, char *name, uint8 *mac, uint8 bssidx);
extern s32 wl_cfg80211_notify_ifchange(int ifidx, char *name, uint8 *mac, uint8 bssidx);
extern struct net_device* wl_cfg80211_allocate_if(struct bcm_cfg80211 *cfg, int ifidx, char *name,
	uint8 *mac, uint8 bssidx);
extern int wl_cfg80211_register_if(struct bcm_cfg80211 *cfg, int ifidx, struct net_device* ndev);
extern int wl_cfg80211_remove_if(struct bcm_cfg80211 *cfg, int ifidx, struct net_device* ndev);
extern int wl_cfg80211_scan_stop(bcm_struct_cfgdev *cfgdev);
extern bool wl_cfg80211_is_vsdb_mode(void);
extern void* wl_cfg80211_get_dhdp(void);
extern bool wl_cfg80211_is_p2p_active(void);
extern void wl_cfg80211_dbg_level(u32 level);
extern s32 wl_cfg80211_get_p2p_dev_addr(struct net_device *net, struct ether_addr *p2pdev_addr);
extern s32 wl_cfg80211_set_p2p_noa(struct net_device *net, char* buf, int len);
extern s32 wl_cfg80211_get_p2p_noa(struct net_device *net, char* buf, int len);
extern s32 wl_cfg80211_set_wps_p2p_ie(struct net_device *net, char *buf, int len,
	enum wl_management_type type);
extern s32 wl_cfg80211_set_p2p_ps(struct net_device *net, char* buf, int len);
#ifdef P2PLISTEN_AP_SAMECHN
extern s32 wl_cfg80211_set_p2p_resp_ap_chn(struct net_device *net, s32 enable);
#endif /* P2PLISTEN_AP_SAMECHN */

/* btcoex functions */
void* wl_cfg80211_btcoex_init(struct net_device *ndev);
void wl_cfg80211_btcoex_deinit(void);

#ifdef WL_SDO
extern s32 wl_cfg80211_sdo_init(struct bcm_cfg80211 *cfg);
extern s32 wl_cfg80211_sdo_deinit(struct bcm_cfg80211 *cfg);
extern s32 wl_cfg80211_sd_offload(struct net_device *net, char *cmd, char* buf, int len);
extern s32 wl_cfg80211_pause_sdo(struct net_device *dev, struct bcm_cfg80211 *cfg);
extern s32 wl_cfg80211_resume_sdo(struct net_device *dev, struct bcm_cfg80211 *cfg);

#endif

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
extern int wl_cfg80211_hex_str_to_bin(unsigned char *data, int dlen, char *str);
extern int wl_cfg80211_hang(struct net_device *dev, u16 reason);
extern s32 wl_mode_to_nl80211_iftype(s32 mode);
int wl_cfg80211_do_driver_init(struct net_device *net);
void wl_cfg80211_enable_trace(u32 level);
extern s32 wl_update_wiphybands(struct bcm_cfg80211 *cfg, bool notify);
extern s32 wl_cfg80211_if_is_group_owner(void);
extern  chanspec_t wl_chspec_host_to_driver(chanspec_t chanspec);
extern chanspec_t wl_ch_host_to_driver(u16 channel);
extern s32 wl_set_tx_power(struct net_device *dev,
	enum nl80211_tx_power_setting type, s32 dbm);
extern s32 wl_get_tx_power(struct net_device *dev, s32 *dbm);
extern s32 wl_add_remove_eventmsg(struct net_device *ndev, u16 event, bool add);
extern void wl_stop_wait_next_action_frame(struct bcm_cfg80211 *cfg, struct net_device *ndev);
#ifdef WL_HOST_BAND_MGMT
extern s32 wl_cfg80211_set_band(struct net_device *ndev, int band);
#endif /* WL_HOST_BAND_MGMT */
#if defined(DHCP_SCAN_SUPPRESS)
extern int wl_cfg80211_scan_suppress(struct net_device *dev, int suppress);
#endif /* OEM_ANDROID */
extern void wl_cfg80211_add_to_eventbuffer(wl_eventmsg_buf_t *ev, u16 event, bool set);
extern s32 wl_cfg80211_apply_eventbuffer(struct net_device *ndev,
	struct bcm_cfg80211 *cfg, wl_eventmsg_buf_t *ev);
extern void get_primary_mac(struct bcm_cfg80211 *cfg, struct ether_addr *mac);
extern void wl_cfg80211_update_power_mode(struct net_device *dev);
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
#ifdef WLAIBSS
extern void wl_cfg80211_set_txfail_pid(int pid);
#endif /* WLAIBSS */
extern void wl_cfg80211_set_rmc_pid(int pid);

#ifdef WLFBT
extern void wl_cfg80211_get_fbt_key(uint8 *key);
#endif

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

#ifdef WL_NAN
extern int wl_cfg80211_nan_cmd_handler(struct net_device *ndev, char *cmd,
	int cmd_len);
#endif /* WL_NAN */

#ifdef WL_CFG80211_P2P_DEV_IF
extern void wl_cfg80211_del_p2p_wdev(void);
#endif /* WL_CFG80211_P2P_DEV_IF */

#endif /* _wl_cfg80211_h_ */
