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

#define WL_NUM_SCAN_MAX			10
#define WL_NUM_PMKIDS_MAX		MAXPMKID
#define WL_TLV_INFO_MAX			1024
#define WL_BSS_INFO_MAX			2048
#define WL_ASSOC_INFO_MAX		512	/* assoc related fil max buf */
#define WL_EXTRA_BUF_MAX		2048
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

/**
 * enum brcmf_scan_status - dongle scan status
 *
 * @BRCMF_SCAN_STATUS_BUSY: scanning in progress on dongle.
 * @BRCMF_SCAN_STATUS_ABORT: scan being aborted on dongle.
 */
enum brcmf_scan_status {
	BRCMF_SCAN_STATUS_BUSY,
	BRCMF_SCAN_STATUS_ABORT,
};

/* wi-fi mode */
enum wl_mode {
	WL_MODE_BSS,
	WL_MODE_IBSS,
	WL_MODE_AP
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

/* basic structure of scan request */
struct brcmf_cfg80211_scan_req {
	struct brcmf_ssid_le ssid_le;
};

/* basic structure of information element */
struct brcmf_cfg80211_ie {
	u16 offset;
	u8 buf[WL_TLV_INFO_MAX];
};

/* security information with currently associated ap */
struct brcmf_cfg80211_security {
	u32 wpa_versions;
	u32 auth_type;
	u32 cipher_pairwise;
	u32 cipher_group;
	u32 wpa_auth;
};

/**
 * struct brcmf_cfg80211_profile - profile information.
 *
 * @ssid: ssid of associated/associating ap.
 * @bssid: bssid of joined/joining ibss.
 * @sec: security information.
 */
struct brcmf_cfg80211_profile {
	struct brcmf_ssid ssid;
	u8 bssid[ETH_ALEN];
	struct brcmf_cfg80211_security sec;
};

/**
 * enum brcmf_vif_status - bit indices for vif status.
 *
 * @BRCMF_VIF_STATUS_READY: ready for operation.
 * @BRCMF_VIF_STATUS_CONNECTING: connect/join in progress.
 * @BRCMF_VIF_STATUS_CONNECTED: connected/joined succesfully.
 * @BRCMF_VIF_STATUS_AP_CREATING: interface configured for AP operation.
 * @BRCMF_VIF_STATUS_AP_CREATED: AP operation started.
 */
enum brcmf_vif_status {
	BRCMF_VIF_STATUS_READY,
	BRCMF_VIF_STATUS_CONNECTING,
	BRCMF_VIF_STATUS_CONNECTED,
	BRCMF_VIF_STATUS_AP_CREATING,
	BRCMF_VIF_STATUS_AP_CREATED
};

/**
 * struct vif_saved_ie - holds saved IEs for a virtual interface.
 *
 * @probe_res_ie: IE info for probe response.
 * @beacon_ie: IE info for beacon frame.
 * @probe_res_ie_len: IE info length for probe response.
 * @beacon_ie_len: IE info length for beacon frame.
 */
struct vif_saved_ie {
	u8  probe_res_ie[IE_MAX_LEN];
	u8  beacon_ie[IE_MAX_LEN];
	u32 probe_res_ie_len;
	u32 beacon_ie_len;
};

/**
 * struct brcmf_cfg80211_vif - virtual interface specific information.
 *
 * @ifp: lower layer interface pointer
 * @wdev: wireless device.
 * @profile: profile information.
 * @mode: operating mode.
 * @roam_off: roaming state.
 * @sme_state: SME state using enum brcmf_vif_status bits.
 * @pm_block: power-management blocked.
 * @list: linked list.
 */
struct brcmf_cfg80211_vif {
	struct brcmf_if *ifp;
	struct wireless_dev wdev;
	struct brcmf_cfg80211_profile profile;
	s32 mode;
	s32 roam_off;
	unsigned long sme_state;
	bool pm_block;
	struct vif_saved_ie saved_ie;
	struct list_head list;
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
 * @wiphy: wiphy object for cfg80211 interface.
 * @conf: dongle configuration.
 * @scan_request: cfg80211 scan request object.
 * @usr_sync: mainly for dongle up/down synchronization.
 * @bss_list: bss_list holding scanned ap information.
 * @scan_req_int: internal scan request object.
 * @bss_info: bss information for cfg80211 layer.
 * @ie: information element object for internal purpose.
 * @conn_info: association info.
 * @pmk_list: wpa2 pmk list.
 * @scan_status: scan activity on the dongle.
 * @pub: common driver information.
 * @channel: current channel.
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
 * @escan_info: escan information.
 * @escan_timeout: Timer for catch scan timeout.
 * @escan_timeout_work: scan timeout worker.
 * @escan_ioctl_buf: dongle command buffer for escan commands.
 * @vif_list: linked list of vif instances.
 * @vif_cnt: number of vif instances.
 */
struct brcmf_cfg80211_info {
	struct wiphy *wiphy;
	struct brcmf_cfg80211_conf *conf;
	struct cfg80211_scan_request *scan_request;
	struct mutex usr_sync;
	struct brcmf_scan_results *bss_list;
	struct brcmf_cfg80211_scan_req scan_req_int;
	struct wl_cfg80211_bss_info *bss_info;
	struct brcmf_cfg80211_ie ie;
	struct brcmf_cfg80211_connect_info conn_info;
	struct brcmf_cfg80211_pmk_list *pmk_list;
	unsigned long scan_status;
	struct brcmf_pub *pub;
	u32 channel;
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
	struct escan_info escan_info;
	struct timer_list escan_timeout;
	struct work_struct escan_timeout_work;
	u8 *escan_ioctl_buf;
	struct list_head vif_list;
	u8 vif_cnt;
};

static inline struct wiphy *cfg_to_wiphy(struct brcmf_cfg80211_info *cfg)
{
	return cfg->wiphy;
}

static inline struct brcmf_cfg80211_info *wiphy_to_cfg(struct wiphy *w)
{
	return (struct brcmf_cfg80211_info *)(wiphy_priv(w));
}

static inline struct brcmf_cfg80211_info *wdev_to_cfg(struct wireless_dev *wd)
{
	return (struct brcmf_cfg80211_info *)(wdev_priv(wd));
}

static inline
struct net_device *cfg_to_ndev(struct brcmf_cfg80211_info *cfg)
{
	struct brcmf_cfg80211_vif *vif;
	vif = list_first_entry(&cfg->vif_list, struct brcmf_cfg80211_vif, list);
	return vif->wdev.netdev;
}

static inline struct brcmf_cfg80211_info *ndev_to_cfg(struct net_device *ndev)
{
	return wdev_to_cfg(ndev->ieee80211_ptr);
}

static inline struct brcmf_cfg80211_profile *ndev_to_prof(struct net_device *nd)
{
	struct brcmf_if *ifp = netdev_priv(nd);
	return &ifp->vif->profile;
}

static inline struct brcmf_cfg80211_vif *ndev_to_vif(struct net_device *ndev)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	return ifp->vif;
}

static inline struct
brcmf_cfg80211_connect_info *cfg_to_conn(struct brcmf_cfg80211_info *cfg)
{
	return &cfg->conn_info;
}

struct brcmf_cfg80211_info *brcmf_cfg80211_attach(struct brcmf_pub *drvr);
void brcmf_cfg80211_detach(struct brcmf_cfg80211_info *cfg);
s32 brcmf_cfg80211_up(struct brcmf_cfg80211_info *cfg);
s32 brcmf_cfg80211_down(struct brcmf_cfg80211_info *cfg);

#endif				/* _wl_cfg80211_h_ */
