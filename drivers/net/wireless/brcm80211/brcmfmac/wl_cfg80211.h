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

#define WL_DBG_NONE		0
#define WL_DBG_CONN		(1 << 5)
#define WL_DBG_SCAN		(1 << 4)
#define WL_DBG_TRACE		(1 << 3)
#define WL_DBG_INFO		(1 << 1)
#define WL_DBG_ERR		(1 << 0)
#define WL_DBG_MASK		((WL_DBG_INFO | WL_DBG_ERR | WL_DBG_TRACE) | \
				(WL_DBG_SCAN) | (WL_DBG_CONN))

#define	WL_ERR(fmt, ...)					\
do {								\
	if (brcmf_dbg_level & WL_DBG_ERR) {			\
		if (net_ratelimit()) {				\
			pr_err("ERROR @%s : " fmt,		\
			       __func__, ##__VA_ARGS__);	\
		}						\
	}							\
} while (0)

#if (defined DEBUG)
#define	WL_INFO(fmt, ...)					\
do {								\
	if (brcmf_dbg_level & WL_DBG_INFO) {			\
		if (net_ratelimit()) {				\
			pr_err("INFO @%s : " fmt,		\
			       __func__, ##__VA_ARGS__);	\
		}						\
	}							\
} while (0)

#define	WL_TRACE(fmt, ...)					\
do {								\
	if (brcmf_dbg_level & WL_DBG_TRACE) {			\
		if (net_ratelimit()) {				\
			pr_err("TRACE @%s : " fmt,		\
			       __func__, ##__VA_ARGS__);	\
		}						\
	}							\
} while (0)

#define	WL_SCAN(fmt, ...)					\
do {								\
	if (brcmf_dbg_level & WL_DBG_SCAN) {			\
		if (net_ratelimit()) {				\
			pr_err("SCAN @%s : " fmt,		\
			       __func__, ##__VA_ARGS__);	\
		}						\
	}							\
} while (0)

#define	WL_CONN(fmt, ...)					\
do {								\
	if (brcmf_dbg_level & WL_DBG_CONN) {			\
		if (net_ratelimit()) {				\
			pr_err("CONN @%s : " fmt,		\
			       __func__, ##__VA_ARGS__);	\
		}						\
	}							\
} while (0)

#else /* (defined DEBUG) */
#define	WL_INFO(fmt, args...)
#define	WL_TRACE(fmt, args...)
#define	WL_SCAN(fmt, args...)
#define	WL_CONN(fmt, args...)
#endif /* (defined DEBUG) */

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
#define WL_DCMD_LEN_MAX	1024
#define WL_EXTRA_BUF_MAX	2048
#define WL_ISCAN_BUF_MAX	2048	/*
				 * the buf length can be BRCMF_DCMD_MAXLEN
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

#define WL_ESCAN_BUF_SIZE		(1024 * 64)
#define WL_ESCAN_TIMER_INTERVAL_MS	8000 /* E-Scan timeout */

#define WL_ESCAN_ACTION_START		1
#define WL_ESCAN_ACTION_CONTINUE	2
#define WL_ESCAN_ACTION_ABORT		3

#define WL_AUTH_SHARED_KEY		1	/* d11 shared authentication */
#define IE_MAX_LEN			512

/* dongle status */
enum wl_status {
	WL_STATUS_READY,
	WL_STATUS_SCANNING,
	WL_STATUS_SCAN_ABORTING,
	WL_STATUS_CONNECTING,
	WL_STATUS_CONNECTED,
	WL_STATUS_AP_CREATING,
	WL_STATUS_AP_CREATED
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

/* forward declaration */
struct brcmf_cfg80211_info;

/* cfg80211 main event loop */
struct brcmf_cfg80211_event_loop {
	s32(*handler[BRCMF_E_LAST]) (struct brcmf_cfg80211_info *cfg,
				     struct net_device *ndev,
				     const struct brcmf_event_msg *e,
				     void *data);
};

/* basic structure of scan request */
struct brcmf_cfg80211_scan_req {
	struct brcmf_ssid_le ssid_le;
};

/* basic structure of information element */
struct brcmf_cfg80211_ie {
	u16 offset;
	u8 buf[WL_TLV_INFO_MAX];
};

/* event queue for cfg80211 main event */
struct brcmf_cfg80211_event_q {
	struct list_head evt_q_list;
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
		(struct brcmf_cfg80211_info *cfg);
};

/* dongle iscan controller */
struct brcmf_cfg80211_iscan_ctrl {
	struct net_device *ndev;
	struct timer_list timer;
	u32 timer_ms;
	u32 timer_on;
	s32 state;
	struct work_struct work;
	struct brcmf_cfg80211_iscan_eloop el;
	void *data;
	s8 dcmd_buf[BRCMF_DCMD_SMLEN];
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
struct brcmf_cfg80211_assoc_ielen_le {
	__le32 req_len;
	__le32 resp_len;
};

/* wpa2 pmk list */
struct brcmf_cfg80211_pmk_list {
	struct pmkid_list pmkids;
	struct pmkid foo[MAXPMKID - 1];
};

/* dongle escan state */
enum wl_escan_state {
	WL_ESCAN_STATE_IDLE,
	WL_ESCAN_STATE_SCANNING
};

struct escan_info {
	u32 escan_state;
	u8 escan_buf[WL_ESCAN_BUF_SIZE];
	struct wiphy *wiphy;
	struct net_device *ndev;
};

/* Structure to hold WPS, WPA IEs for a AP */
struct ap_info {
	u8 probe_res_ie[IE_MAX_LEN];
	u8 beacon_ie[IE_MAX_LEN];
	u32 probe_res_ie_len;
	u32 beacon_ie_len;
	u8 *wpa_ie;
	u8 *rsn_ie;
	bool security_mode;
};

/**
 * struct brcmf_pno_param_le - PNO scan configuration parameters
 *
 * @version: PNO parameters version.
 * @scan_freq: scan frequency.
 * @lost_network_timeout: #sec. to declare discovered network as lost.
 * @flags: Bit field to control features of PFN such as sort criteria auto
 *	enable switch and background scan.
 * @rssi_margin: Margin to avoid jitter for choosing a PFN based on RSSI sort
 *	criteria.
 * @bestn: number of best networks in each scan.
 * @mscan: number of scans recorded.
 * @repeat: minimum number of scan intervals before scan frequency changes
 *	in adaptive scan.
 * @exp: exponent of 2 for maximum scan interval.
 * @slow_freq: slow scan period.
 */
struct brcmf_pno_param_le {
	__le32 version;
	__le32 scan_freq;
	__le32 lost_network_timeout;
	__le16 flags;
	__le16 rssi_margin;
	u8 bestn;
	u8 mscan;
	u8 repeat;
	u8 exp;
	__le32 slow_freq;
};

/**
 * struct brcmf_pno_net_param_le - scan parameters per preferred network.
 *
 * @ssid: ssid name and its length.
 * @flags: bit2: hidden.
 * @infra: BSS vs IBSS.
 * @auth: Open vs Closed.
 * @wpa_auth: WPA type.
 * @wsec: wsec value.
 */
struct brcmf_pno_net_param_le {
	struct brcmf_ssid_le ssid;
	__le32 flags;
	__le32 infra;
	__le32 auth;
	__le32 wpa_auth;
	__le32 wsec;
};

/**
 * struct brcmf_pno_net_info_le - information per found network.
 *
 * @bssid: BSS network identifier.
 * @channel: channel number only.
 * @SSID_len: length of ssid.
 * @SSID: ssid characters.
 * @RSSI: receive signal strength (in dBm).
 * @timestamp: age in seconds.
 */
struct brcmf_pno_net_info_le {
	u8 bssid[ETH_ALEN];
	u8 channel;
	u8 SSID_len;
	u8 SSID[32];
	__le16	RSSI;
	__le16	timestamp;
};

/**
 * struct brcmf_pno_scanresults_le - result returned in PNO NET FOUND event.
 *
 * @version: PNO version identifier.
 * @status: indicates completion status of PNO scan.
 * @count: amount of brcmf_pno_net_info_le entries appended.
 */
struct brcmf_pno_scanresults_le {
	__le32 version;
	__le32 status;
	__le32 count;
};

/**
 * struct brcmf_cfg80211_info - dongle private data of cfg80211 interface
 *
 * @wdev: representing wl cfg80211 device.
 * @conf: dongle configuration.
 * @scan_request: cfg80211 scan request object.
 * @el: main event loop.
 * @evt_q_list: used for event queue.
 * @evt_q_lock: for event queue synchronization.
 * @usr_sync: mainly for dongle up/down synchronization.
 * @bss_list: bss_list holding scanned ap information.
 * @scan_results: results of the last scan.
 * @scan_req_int: internal scan request object.
 * @bss_info: bss information for cfg80211 layer.
 * @ie: information element object for internal purpose.
 * @profile: holding dongle profile.
 * @iscan: iscan controller information.
 * @conn_info: association info.
 * @pmk_list: wpa2 pmk list.
 * @event_work: event handler work struct.
 * @status: current dongle status.
 * @pub: common driver information.
 * @channel: current channel.
 * @iscan_on: iscan on/off switch.
 * @iscan_kickstart: indicate iscan already started.
 * @active_scan: current scan mode.
 * @sched_escan: e-scan for scheduled scan support running.
 * @ibss_starter: indicates this sta is ibss starter.
 * @link_up: link/connection up flag.
 * @pwr_save: indicate whether dongle to support power save mode.
 * @dongle_up: indicate whether dongle up or not.
 * @roam_on: on/off switch for dongle self-roaming.
 * @scan_tried: indicates if first scan attempted.
 * @dcmd_buf: dcmd buffer.
 * @extra_buf: mainly to grab assoc information.
 * @debugfsdir: debugfs folder for this device.
 * @escan_on: escan on/off switch.
 * @escan_info: escan information.
 * @escan_timeout: Timer for catch scan timeout.
 * @escan_timeout_work: scan timeout worker.
 * @escan_ioctl_buf: dongle command buffer for escan commands.
 * @ap_info: host ap information.
 * @ci: used to link this structure to netdev private data.
 */
struct brcmf_cfg80211_info {
	struct wireless_dev *wdev;
	struct brcmf_cfg80211_conf *conf;
	struct cfg80211_scan_request *scan_request;
	struct brcmf_cfg80211_event_loop el;
	struct list_head evt_q_list;
	spinlock_t	 evt_q_lock;
	struct mutex usr_sync;
	struct brcmf_scan_results *bss_list;
	struct brcmf_scan_results *scan_results;
	struct brcmf_cfg80211_scan_req *scan_req_int;
	struct wl_cfg80211_bss_info *bss_info;
	struct brcmf_cfg80211_ie ie;
	struct brcmf_cfg80211_profile *profile;
	struct brcmf_cfg80211_iscan_ctrl *iscan;
	struct brcmf_cfg80211_connect_info conn_info;
	struct brcmf_cfg80211_pmk_list *pmk_list;
	struct work_struct event_work;
	unsigned long status;
	struct brcmf_pub *pub;
	u32 channel;
	bool iscan_on;
	bool iscan_kickstart;
	bool active_scan;
	bool sched_escan;
	bool ibss_starter;
	bool link_up;
	bool pwr_save;
	bool dongle_up;
	bool roam_on;
	bool scan_tried;
	u8 *dcmd_buf;
	u8 *extra_buf;
	struct dentry *debugfsdir;
	bool escan_on;
	struct escan_info escan_info;
	struct timer_list escan_timeout;
	struct work_struct escan_timeout_work;
	u8 *escan_ioctl_buf;
	struct ap_info *ap_info;
};

static inline struct wiphy *cfg_to_wiphy(struct brcmf_cfg80211_info *w)
{
	return w->wdev->wiphy;
}

static inline struct brcmf_cfg80211_info *wiphy_to_cfg(struct wiphy *w)
{
	return (struct brcmf_cfg80211_info *)(wiphy_priv(w));
}

static inline struct brcmf_cfg80211_info *wdev_to_cfg(struct wireless_dev *wd)
{
	return (struct brcmf_cfg80211_info *)(wdev_priv(wd));
}

static inline struct net_device *cfg_to_ndev(struct brcmf_cfg80211_info *cfg)
{
	return cfg->wdev->netdev;
}

static inline struct brcmf_cfg80211_info *ndev_to_cfg(struct net_device *ndev)
{
	return wdev_to_cfg(ndev->ieee80211_ptr);
}

#define iscan_to_cfg(i) ((struct brcmf_cfg80211_info *)(i->data))
#define cfg_to_iscan(w) (w->iscan)

static inline struct
brcmf_cfg80211_connect_info *cfg_to_conn(struct brcmf_cfg80211_info *cfg)
{
	return &cfg->conn_info;
}

struct brcmf_cfg80211_info *brcmf_cfg80211_attach(struct net_device *ndev,
						  struct device *busdev,
						  struct brcmf_pub *drvr);
void brcmf_cfg80211_detach(struct brcmf_cfg80211_info *cfg);

/* event handler from dongle */
void brcmf_cfg80211_event(struct net_device *ndev,
			  const struct brcmf_event_msg *e, void *data);
s32 brcmf_cfg80211_up(struct brcmf_cfg80211_info *cfg);
s32 brcmf_cfg80211_down(struct brcmf_cfg80211_info *cfg);

#endif				/* _wl_cfg80211_h_ */
