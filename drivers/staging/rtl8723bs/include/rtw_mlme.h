/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __RTW_MLME_H_
#define __RTW_MLME_H_


#define	MAX_BSS_CNT	128
/* define   MAX_JOIN_TIMEOUT	2000 */
/* define   MAX_JOIN_TIMEOUT	2500 */
#define   MAX_JOIN_TIMEOUT	6500

/* 	Commented by Albert 20101105 */
/* 	Increase the scanning timeout because of increasing the SURVEY_TO value. */

#define		SCANNING_TIMEOUT	8000

#ifdef PALTFORM_OS_WINCE
#define	SCANQUEUE_LIFETIME 12000000 /*  unit:us */
#else
#define	SCANQUEUE_LIFETIME 20000 /*  20sec, unit:msec */
#endif

#define WIFI_NULL_STATE		0x00000000
#define WIFI_ASOC_STATE		0x00000001		/*  Under Linked state... */
#define WIFI_SLEEP_STATE	0x00000004
#define WIFI_STATION_STATE	0x00000008
#define	WIFI_AP_STATE			0x00000010
#define	WIFI_ADHOC_STATE		0x00000020
#define WIFI_ADHOC_MASTER_STATE	0x00000040
#define WIFI_UNDER_LINKING	0x00000080

#define WIFI_UNDER_WPS			0x00000100
#define	WIFI_STA_ALIVE_CHK_STATE	0x00000400
#define	WIFI_SITE_MONITOR			0x00000800		/* to indicate the station is under site surveying */

/* ifdef UNDER_MPTEST */
#define	WIFI_MP_STATE							0x00010000
/* endif */

/* define _FW_UNDER_CMD		WIFI_UNDER_CMD */
#define _FW_UNDER_LINKING	WIFI_UNDER_LINKING
#define _FW_LINKED			WIFI_ASOC_STATE
#define _FW_UNDER_SURVEY	WIFI_SITE_MONITOR


enum {
 dot11AuthAlgrthm_Open = 0,
 dot11AuthAlgrthm_Shared,
 dot11AuthAlgrthm_8021X,
 dot11AuthAlgrthm_Auto,
 dot11AuthAlgrthm_WAPI,
 dot11AuthAlgrthm_MaxNum
};

/*  Scan type including active and passive scan. */
enum rt_scan_type {
	SCAN_PASSIVE,
	SCAN_ACTIVE,
	SCAN_MIX,
};

enum {
	GHZ24_50 = 0,
	GHZ_50,
	GHZ_24,
	GHZ_MAX,
};

/*

there are several "locks" in mlme_priv,
since mlme_priv is a shared resource between many threads,
like ISR/Call-Back functions, the OID handlers, and even timer functions.

Each struct __queue has its own locks, already.
Other items in mlme_priv are protected by mlme_priv.lock, while items in
xmit_priv are protected by xmit_priv.lock.

To avoid possible dead lock, any thread trying to modifiying mlme_priv
SHALL not lock up more than one locks at a time!

The only exception is that queue functions which take the __queue.lock
may be called with the xmit_priv.lock held. In this case the order
MUST always be first lock xmit_priv.lock and then call any queue functions
which take __queue.lock.
*/

struct sitesurvey_ctrl {
	u64	last_tx_pkts;
	uint	last_rx_pkts;
	signed int	traffic_busy;
	struct timer_list	sitesurvey_ctrl_timer;
};

struct rt_link_detect_t {
	u32 			NumTxOkInPeriod;
	u32 			NumRxOkInPeriod;
	u32 			NumRxUnicastOkInPeriod;
	bool			bBusyTraffic;
	bool			bTxBusyTraffic;
	bool			bRxBusyTraffic;
	bool			bHigherBusyTraffic; /*  For interrupt migration purpose. */
	bool			bHigherBusyRxTraffic; /*  We may disable Tx interrupt according as Rx traffic. */
	bool			bHigherBusyTxTraffic; /*  We may disable Tx interrupt according as Tx traffic. */
	/* u8 TrafficBusyState; */
	u8 TrafficTransitionCount;
	u32 LowPowerTransitionCount;
};

/* used for mlme_priv.roam_flags */
enum {
	RTW_ROAM_ON_EXPIRED = BIT0,
	RTW_ROAM_ON_RESUME = BIT1,
	RTW_ROAM_ACTIVE = BIT2,
};

struct mlme_priv {

	spinlock_t	lock;
	signed int	fw_state;	/* shall we protect this variable? maybe not necessarily... */
	u8 bScanInProcess;
	u8 to_join; /* flag */

	u8 to_roam; /* roaming trying times */
	struct wlan_network *roam_network; /* the target of active roam */
	u8 roam_flags;
	u8 roam_rssi_diff_th; /* rssi difference threshold for active scan candidate selection */
	u32 roam_scan_int_ms; /* scan interval for active roam */
	u32 roam_scanr_exp_ms; /* scan result expire time in ms  for roam */
	u8 roam_tgt_addr[ETH_ALEN]; /* request to roam to speicific target without other consideration */

	u8 *nic_hdl;

	u8 not_indic_disco;
	struct list_head		*pscanned;
	struct __queue	free_bss_pool;
	struct __queue	scanned_queue;
	u8 *free_bss_buf;

	struct ndis_802_11_ssid	assoc_ssid;
	u8 assoc_bssid[6];

	struct wlan_network	cur_network;
	struct wlan_network *cur_network_scanned;

	/* uint wireless_mode; no used, remove it */

	u32 auto_scan_int_ms;

	struct timer_list assoc_timer;

	uint assoc_by_bssid;
	uint assoc_by_rssi;

	struct timer_list scan_to_timer; /*  driver itself handles scan_timeout status. */
	unsigned long scan_start_time; /*  used to evaluate the time spent in scanning */

	struct timer_list set_scan_deny_timer;
	atomic_t set_scan_deny; /* 0: allowed, 1: deny */

	struct qos_priv qospriv;

	/* Number of non-HT AP/stations */
	int num_sta_no_ht;

	/* Number of HT AP/stations 20 MHz */
	/* int num_sta_ht_20mhz; */


	int num_FortyMHzIntolerant;

	struct ht_priv htpriv;

	struct rt_link_detect_t	LinkDetectInfo;
	struct timer_list	dynamic_chk_timer; /* dynamic/periodic check timer */

	u8 acm_mask; /*  for wmm acm mask */
	u8 ChannelPlan;
	enum rt_scan_type	scan_mode; /*  active: 1, passive: 0 */

	u8 *wps_probe_req_ie;
	u32 wps_probe_req_ie_len;

	/* Number of associated Non-ERP stations (i.e., stations using 802.11b
	 * in 802.11g BSS) */
	int num_sta_non_erp;

	/* Number of associated stations that do not support Short Slot Time */
	int num_sta_no_short_slot_time;

	/* Number of associated stations that do not support Short Preamble */
	int num_sta_no_short_preamble;

	int olbc; /* Overlapping Legacy BSS Condition */

	/* Number of HT associated stations that do not support greenfield */
	int num_sta_ht_no_gf;

	/* Number of associated non-HT stations */
	/* int num_sta_no_ht; */

	/* Number of HT associated stations 20 MHz */
	int num_sta_ht_20mhz;

	/* Overlapping BSS information */
	int olbc_ht;

	u16 ht_op_mode;

	u8 *assoc_req;
	u32 assoc_req_len;
	u8 *assoc_rsp;
	u32 assoc_rsp_len;

	u8 *wps_beacon_ie;
	/* u8 *wps_probe_req_ie; */
	u8 *wps_probe_resp_ie;
	u8 *wps_assoc_resp_ie; /*  for CONFIG_IOCTL_CFG80211, this IE could include p2p ie / wfd ie */

	u32 wps_beacon_ie_len;
	/* u32 wps_probe_req_ie_len; */
	u32 wps_probe_resp_ie_len;
	u32 wps_assoc_resp_ie_len; /*  for CONFIG_IOCTL_CFG80211, this IE len could include p2p ie / wfd ie */

	u8 *p2p_beacon_ie;
	u8 *p2p_probe_req_ie;
	u8 *p2p_probe_resp_ie;
	u8 *p2p_go_probe_resp_ie; /* for GO */
	u8 *p2p_assoc_req_ie;

	u32 p2p_beacon_ie_len;
	u32 p2p_probe_req_ie_len;
	u32 p2p_probe_resp_ie_len;
	u32 p2p_go_probe_resp_ie_len; /* for GO */
	u32 p2p_assoc_req_ie_len;

	spinlock_t	bcn_update_lock;
	u8 update_bcn;

	u8 NumOfBcnInfoChkFail;
	unsigned long	timeBcnInfoChkStart;
};

void rtw_mlme_reset_auto_scan_int(struct adapter *adapter);

struct hostapd_priv {
	struct adapter *padapter;
};

extern int hostapd_mode_init(struct adapter *padapter);
extern void hostapd_mode_unload(struct adapter *padapter);

extern void rtw_joinbss_event_prehandle(struct adapter *adapter, u8 *pbuf);
extern void rtw_survey_event_callback(struct adapter *adapter, u8 *pbuf);
extern void rtw_surveydone_event_callback(struct adapter *adapter, u8 *pbuf);
extern void rtw_joinbss_event_callback(struct adapter *adapter, u8 *pbuf);
extern void rtw_stassoc_event_callback(struct adapter *adapter, u8 *pbuf);
extern void rtw_stadel_event_callback(struct adapter *adapter, u8 *pbuf);
extern void rtw_atimdone_event_callback(struct adapter *adapter, u8 *pbuf);
extern void rtw_cpwm_event_callback(struct adapter *adapter, u8 *pbuf);
extern void rtw_wmm_event_callback(struct adapter *padapter, u8 *pbuf);

extern void rtw_join_timeout_handler(struct timer_list *t);
extern void _rtw_scan_timeout_handler(struct timer_list *t);

int event_thread(void *context);

extern void rtw_free_network_queue(struct adapter *adapter, u8 isfreeall);
extern int rtw_init_mlme_priv(struct adapter *adapter);/*  (struct mlme_priv *pmlmepriv); */

extern void rtw_free_mlme_priv(struct mlme_priv *pmlmepriv);


extern signed int rtw_select_and_join_from_scanned_queue(struct mlme_priv *pmlmepriv);
extern signed int rtw_set_key(struct adapter *adapter, struct security_priv *psecuritypriv, signed int keyid, u8 set_tx, bool enqueue);
extern signed int rtw_set_auth(struct adapter *adapter, struct security_priv *psecuritypriv);

static inline u8 *get_bssid(struct mlme_priv *pmlmepriv)
{	/* if sta_mode:pmlmepriv->cur_network.network.mac_address => bssid */
	/*  if adhoc_mode:pmlmepriv->cur_network.network.mac_address => ibss mac address */
	return pmlmepriv->cur_network.network.mac_address;
}

static inline signed int check_fwstate(struct mlme_priv *pmlmepriv, signed int state)
{
	if (pmlmepriv->fw_state & state)
		return true;

	return false;
}

static inline signed int get_fwstate(struct mlme_priv *pmlmepriv)
{
	return pmlmepriv->fw_state;
}

/*
 * No Limit on the calling context,
 * therefore set it to be the critical section...
 *
 * ### NOTE:#### (!!!!)
 * MUST TAKE CARE THAT BEFORE CALLING THIS FUNC, YOU SHOULD HAVE LOCKED pmlmepriv->lock
 */
static inline void set_fwstate(struct mlme_priv *pmlmepriv, signed int state)
{
	pmlmepriv->fw_state |= state;
	/* FOR HW integration */
	if (state == _FW_UNDER_SURVEY)
		pmlmepriv->bScanInProcess = true;
}

static inline void _clr_fwstate_(struct mlme_priv *pmlmepriv, signed int state)
{
	pmlmepriv->fw_state &= ~state;
	/* FOR HW integration */
	if (state == _FW_UNDER_SURVEY)
		pmlmepriv->bScanInProcess = false;
}

extern u16 rtw_get_capability(struct wlan_bssid_ex *bss);
extern void rtw_update_scanned_network(struct adapter *adapter, struct wlan_bssid_ex *target);
extern void rtw_disconnect_hdl_under_linked(struct adapter *adapter, struct sta_info *psta, u8 free_assoc);
extern void rtw_generate_random_ibss(u8 *pibss);
extern struct wlan_network *rtw_find_network(struct __queue *scanned_queue, u8 *addr);
extern struct wlan_network *rtw_get_oldest_wlan_network(struct __queue *scanned_queue);
struct wlan_network *_rtw_find_same_network(struct __queue *scanned_queue, struct wlan_network *network);

extern void rtw_free_assoc_resources(struct adapter *adapter, int lock_scanned_queue);
extern void rtw_indicate_disconnect(struct adapter *adapter);
extern void rtw_indicate_connect(struct adapter *adapter);
void rtw_indicate_scan_done(struct adapter *padapter, bool aborted);
void rtw_scan_abort(struct adapter *adapter);

extern int rtw_restruct_sec_ie(struct adapter *adapter, u8 *in_ie, u8 *out_ie, uint in_len);
extern int rtw_restruct_wmm_ie(struct adapter *adapter, u8 *in_ie, u8 *out_ie, uint in_len, uint initial_out_len);
extern void rtw_init_registrypriv_dev_network(struct adapter *adapter);

extern void rtw_update_registrypriv_dev_network(struct adapter *adapter);

extern void rtw_get_encrypt_decrypt_from_registrypriv(struct adapter *adapter);

extern void _rtw_join_timeout_handler(struct timer_list *t);
extern void rtw_scan_timeout_handler(struct timer_list *t);

extern void rtw_dynamic_check_timer_handler(struct adapter *adapter);
bool rtw_is_scan_deny(struct adapter *adapter);
void rtw_clear_scan_deny(struct adapter *adapter);
void rtw_set_scan_deny(struct adapter *adapter, u32 ms);

void rtw_free_mlme_priv_ie_data(struct mlme_priv *pmlmepriv);

extern void _rtw_free_mlme_priv(struct mlme_priv *pmlmepriv);

/* extern struct wlan_network* _rtw_dequeue_network(struct __queue *queue); */

extern struct wlan_network *rtw_alloc_network(struct mlme_priv *pmlmepriv);


extern void _rtw_free_network(struct mlme_priv *pmlmepriv, struct wlan_network *pnetwork, u8 isfreeall);
extern void _rtw_free_network_nolock(struct mlme_priv *pmlmepriv, struct wlan_network *pnetwork);


extern struct wlan_network *_rtw_find_network(struct __queue *scanned_queue, u8 *addr);

extern signed int rtw_if_up(struct adapter *padapter);

signed int rtw_linked_check(struct adapter *padapter);

u8 *rtw_get_capability_from_ie(u8 *ie);
u8 *rtw_get_beacon_interval_from_ie(u8 *ie);


void rtw_joinbss_reset(struct adapter *padapter);

void rtw_ht_use_default_setting(struct adapter *padapter);
void rtw_build_wmm_ie_ht(struct adapter *padapter, u8 *out_ie, uint *pout_len);
unsigned int rtw_restructure_ht_ie(struct adapter *padapter, u8 *in_ie, u8 *out_ie, uint in_len, uint *pout_len, u8 channel);
void rtw_update_ht_cap(struct adapter *padapter, u8 *pie, uint ie_len, u8 channel);
void rtw_issue_addbareq_cmd(struct adapter *padapter, struct xmit_frame *pxmitframe);
void rtw_append_exented_cap(struct adapter *padapter, u8 *out_ie, uint *pout_len);

int rtw_is_same_ibss(struct adapter *adapter, struct wlan_network *pnetwork);
int is_same_network(struct wlan_bssid_ex *src, struct wlan_bssid_ex *dst, u8 feature);

#define rtw_roam_flags(adapter) ((adapter)->mlmepriv.roam_flags)
#define rtw_chk_roam_flags(adapter, flags) ((adapter)->mlmepriv.roam_flags & flags)

void _rtw_roaming(struct adapter *adapter, struct wlan_network *tgt_network);
void rtw_roaming(struct adapter *adapter, struct wlan_network *tgt_network);
void rtw_set_to_roam(struct adapter *adapter, u8 to_roam);
u8 rtw_dec_to_roam(struct adapter *adapter);
u8 rtw_to_roam(struct adapter *adapter);
int rtw_select_roaming_candidate(struct mlme_priv *pmlmepriv);

void rtw_sta_media_status_rpt(struct adapter *adapter, struct sta_info *psta, u32 mstatus);

#endif /* __RTL871X_MLME_H_ */
