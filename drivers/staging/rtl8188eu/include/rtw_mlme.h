/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __RTW_MLME_H_
#define __RTW_MLME_H_

#include <osdep_service.h>
#include <mlme_osdep.h>
#include <drv_types.h>
#include <wlan_bssdef.h>

#define	MAX_BSS_CNT	128
#define   MAX_JOIN_TIMEOUT	6500

/* Increase the scanning timeout because of increasing the SURVEY_TO value. */

#define		SCANNING_TIMEOUT	8000

#define	SCAN_INTERVAL	(30) /*  unit:2sec, 30*2=60sec */

#define	SCANQUEUE_LIFETIME 20 /*  unit:sec */

#define	WIFI_NULL_STATE			0x00000000

#define	WIFI_ASOC_STATE			0x00000001	/* Under Linked state */
#define	WIFI_REASOC_STATE		0x00000002
#define	WIFI_SLEEP_STATE		0x00000004
#define	WIFI_STATION_STATE		0x00000008

#define	WIFI_AP_STATE			0x00000010
#define	WIFI_ADHOC_STATE		0x00000020
#define WIFI_ADHOC_MASTER_STATE		0x00000040
#define WIFI_UNDER_LINKING		0x00000080

#define	WIFI_UNDER_WPS			0x00000100
#define	WIFI_STA_ALIVE_CHK_STATE	0x00000400
#define	WIFI_SITE_MONITOR		0x00000800	/* to indicate the station is under site surveying */

#define _FW_UNDER_LINKING	WIFI_UNDER_LINKING
#define _FW_LINKED			WIFI_ASOC_STATE
#define _FW_UNDER_SURVEY	WIFI_SITE_MONITOR

enum dot11AuthAlgrthmNum {
	dot11AuthAlgrthm_Open = 0, /* open system */
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

enum SCAN_RESULT_TYPE {
	SCAN_RESULT_P2P_ONLY = 0,	/* Will return all the P2P devices. */
	SCAN_RESULT_ALL = 1,		/* Will return all the scanned device,
					 * include AP.
					 */
	SCAN_RESULT_WFD_TYPE = 2	/* Will just return the correct WFD
					 * device.
					 */
					/* If this device is Miracast sink
					 * device, it will just return all the
					 * Miracast source devices.
					 */
};

/*
 * there are several "locks" in mlme_priv,
 * since mlme_priv is a shared resource between many threads,
 * like ISR/Call-Back functions, the OID handlers, and even timer functions.
 *
 * Each _queue has its own locks, already.
 * Other items are protected by mlme_priv.lock.
 *
 * To avoid possible dead lock, any thread trying to modifiying mlme_priv
 * SHALL not lock up more than one lock at a time!
 */

#define traffic_threshold	10
#define	traffic_scan_period	500

struct rt_link_detect {
	u32	NumTxOkInPeriod;
	u32	NumRxOkInPeriod;
	u32	NumRxUnicastOkInPeriod;
	bool	bBusyTraffic;
	bool	bTxBusyTraffic;
	bool	bRxBusyTraffic;
	bool	bHigherBusyTraffic; /*  For interrupt migration purpose. */
	bool	bHigherBusyRxTraffic; /* We may disable Tx interrupt according
				       * to Rx traffic.
				       */
	bool	bHigherBusyTxTraffic; /* We may disable Tx interrupt according
				       * to Tx traffic.
				       */
};

struct mlme_priv {
	spinlock_t lock;
	int fw_state;	/* shall we protect this variable? maybe not necessarily... */
	u8 bScanInProcess;
	u8 to_join; /* flag */
	u8 to_roaming; /*  roaming trying times */

	u8 *nic_hdl;

	struct list_head *pscanned;
	struct __queue free_bss_pool;
	struct __queue scanned_queue;
	u8 *free_bss_buf;

	struct ndis_802_11_ssid	assoc_ssid;
	u8	assoc_bssid[6];

	struct wlan_network	cur_network;

	u32	scan_interval;

	struct timer_list assoc_timer;

	uint assoc_by_bssid;

	struct timer_list scan_to_timer; /*  driver itself handles scan_timeout status. */

	struct qos_priv qospriv;

	/* Number of non-HT AP/stations */
	int num_sta_no_ht;

	/* Number of HT AP/stations 20 MHz */
	/* int num_sta_ht_20mhz; */

	int num_FortyMHzIntolerant;
	struct ht_priv	htpriv;
	struct rt_link_detect LinkDetectInfo;
	struct timer_list dynamic_chk_timer; /* dynamic/periodic check timer */

	u8	key_mask; /* use for ips to set wep key after ips_leave */
	u8	acm_mask; /*  for wmm acm mask */
	u8	ChannelPlan;
	enum rt_scan_type scan_mode; /*  active: 1, passive: 0 */

	/* u8 probereq_wpsie[MAX_WPS_IE_LEN];added in probe req */
	/* int probereq_wpsie_len; */
	u8 *wps_probe_req_ie;
	u32 wps_probe_req_ie_len;

	u8 *assoc_req;
	u32 assoc_req_len;
	u8 *assoc_rsp;
	u32 assoc_rsp_len;

#if defined(CONFIG_88EU_AP_MODE)
	/* Number of associated Non-ERP stations (i.e., stations using 802.11b
	 * in 802.11g BSS)
	 */
	int num_sta_non_erp;

	/* Number of associated stations that do not support Short Slot Time */
	int num_sta_no_short_slot_time;

	/* Number of associated stations that do not support Short Preamble */
	int num_sta_no_short_preamble;

	int olbc; /* Overlapping Legacy BSS Condition */

	/* Number of HT assoc sta that do not support greenfield */
	int num_sta_ht_no_gf;

	/* Number of associated non-HT stations */
	/* int num_sta_no_ht; */

	/* Number of HT associated stations 20 MHz */
	int num_sta_ht_20mhz;

	/* Overlapping BSS information */
	int olbc_ht;

	u16 ht_op_mode;

	u8 *wps_beacon_ie;
	/* u8 *wps_probe_req_ie; */
	u8 *wps_probe_resp_ie;
	u8 *wps_assoc_resp_ie;

	u32 wps_beacon_ie_len;
	u32 wps_probe_resp_ie_len;
	u32 wps_assoc_resp_ie_len;

	spinlock_t bcn_update_lock;
	u8		update_bcn;
#endif /* if defined (CONFIG_88EU_AP_MODE) */
};

#ifdef CONFIG_88EU_AP_MODE

struct hostapd_priv {
	struct adapter *padapter;
};

int hostapd_mode_init(struct adapter *padapter);
void hostapd_mode_unload(struct adapter *padapter);
#endif

extern const u8 WPA_TKIP_CIPHER[4];
extern const u8 RSN_TKIP_CIPHER[4];
extern u8 REALTEK_96B_IE[];
extern const u8 MCS_rate_1R[16];

void rtw_joinbss_event_prehandle(struct adapter *adapter, u8 *pbuf);
void rtw_survey_event_callback(struct adapter *adapter, u8 *pbuf);
void rtw_surveydone_event_callback(struct adapter *adapter, u8 *pbuf);
void rtw_joinbss_event_callback(struct adapter *adapter, u8 *pbuf);
void rtw_stassoc_event_callback(struct adapter *adapter, u8 *pbuf);
void rtw_stadel_event_callback(struct adapter *adapter, u8 *pbuf);
void rtw_atimdone_event_callback(struct adapter *adapter, u8 *pbuf);
void rtw_cpwm_event_callback(struct adapter *adapter, u8 *pbuf);
void indicate_wx_scan_complete_event(struct adapter *padapter);
void rtw_indicate_wx_assoc_event(struct adapter *padapter);
void rtw_indicate_wx_disassoc_event(struct adapter *padapter);
int event_thread(void *context);
void rtw_free_network_queue(struct adapter *adapter, u8 isfreeall);
int rtw_init_mlme_priv(struct adapter *adapter);
void rtw_free_mlme_priv(struct mlme_priv *pmlmepriv);
int rtw_select_and_join_from_scanned_queue(struct mlme_priv *pmlmepriv);
int rtw_set_key(struct adapter *adapter, struct security_priv *psecuritypriv,
		int keyid, u8 set_tx);
int rtw_set_auth(struct adapter *adapter, struct security_priv *psecuritypriv);

static inline u8 *get_bssid(struct mlme_priv *pmlmepriv)
{	/* if sta_mode:pmlmepriv->cur_network.network.MacAddress=> bssid */
	/*  if adhoc_mode:pmlmepriv->cur_network.network.MacAddress=> ibss mac address */
	return pmlmepriv->cur_network.network.MacAddress;
}

static inline int check_fwstate(struct mlme_priv *pmlmepriv, int state)
{
	if (pmlmepriv->fw_state & state)
		return true;

	return false;
}

static inline int get_fwstate(struct mlme_priv *pmlmepriv)
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
static inline void set_fwstate(struct mlme_priv *pmlmepriv, int state)
{
	pmlmepriv->fw_state |= state;
	/* FOR HW integration */
	if (_FW_UNDER_SURVEY == state)
		pmlmepriv->bScanInProcess = true;
}

static inline void _clr_fwstate_(struct mlme_priv *pmlmepriv, int state)
{
	pmlmepriv->fw_state &= ~state;
	/* FOR HW integration */
	if (_FW_UNDER_SURVEY == state)
		pmlmepriv->bScanInProcess = false;
}

/*
 * No Limit on the calling context,
 * therefore set it to be the critical section...
 */
static inline void clr_fwstate(struct mlme_priv *pmlmepriv, int state)
{
	spin_lock_bh(&pmlmepriv->lock);
	if (check_fwstate(pmlmepriv, state))
		pmlmepriv->fw_state ^= state;
	spin_unlock_bh(&pmlmepriv->lock);
}

static inline void clr_fwstate_ex(struct mlme_priv *pmlmepriv, int state)
{
	spin_lock_bh(&pmlmepriv->lock);
	_clr_fwstate_(pmlmepriv, state);
	spin_unlock_bh(&pmlmepriv->lock);
}

u16 rtw_get_capability(struct wlan_bssid_ex *bss);
void rtw_update_scanned_network(struct adapter *adapter,
				struct wlan_bssid_ex *target);
void rtw_disconnect_hdl_under_linked(struct adapter *adapter,
				     struct sta_info *psta, u8 free_assoc);
void rtw_generate_random_ibss(u8 *pibss);
struct wlan_network *rtw_find_network(struct __queue *scanned_queue, u8 *addr);
struct wlan_network *rtw_get_oldest_wlan_network(struct __queue *scanned_queue);

void rtw_free_assoc_resources(struct adapter *adapter);
void rtw_free_assoc_resources_locked(struct adapter *adapter);
void rtw_indicate_disconnect(struct adapter *adapter);
void rtw_indicate_connect(struct adapter *adapter);
void rtw_indicate_scan_done(struct adapter *padapter, bool aborted);

int rtw_restruct_sec_ie(struct adapter *adapter, u8 *in_ie, u8 *out_ie,
			uint in_len);
int rtw_restruct_wmm_ie(struct adapter *adapter, u8 *in_ie, u8 *out_ie,
			uint in_len, uint initial_out_len);
void rtw_init_registrypriv_dev_network(struct adapter *adapter);

void rtw_update_registrypriv_dev_network(struct adapter *adapter);

void rtw_get_encrypt_decrypt_from_registrypriv(struct adapter *adapter);

void _rtw_join_timeout_handler(struct timer_list *t);
void rtw_scan_timeout_handler(struct timer_list *t);

void rtw_dynamic_check_timer_handlder(struct timer_list *t);
#define rtw_is_scan_deny(adapter) false
#define rtw_clear_scan_deny(adapter) do {} while (0)
#define rtw_set_scan_deny_timer_hdl(adapter) do {} while (0)
#define rtw_set_scan_deny(adapter, ms) do {} while (0)

void rtw_free_mlme_priv_ie_data(struct mlme_priv *pmlmepriv);

struct wlan_network *_rtw_alloc_network(struct mlme_priv *pmlmepriv);

int rtw_if_up(struct adapter *padapter);

u8 *rtw_get_capability_from_ie(u8 *ie);
u8 *rtw_get_beacon_interval_from_ie(u8 *ie);

void rtw_joinbss_reset(struct adapter *padapter);

unsigned int rtw_restructure_ht_ie(struct adapter *padapter, u8 *in_ie,
				   u8 *out_ie, uint in_len, uint *pout_len);
void rtw_update_ht_cap(struct adapter *padapter, u8 *pie, uint ie_len);
void rtw_issue_addbareq_cmd(struct adapter *padapter,
			    struct xmit_frame *pxmitframe);

int rtw_is_same_ibss(struct adapter *adapter, struct wlan_network *pnetwork);
int is_same_network(struct wlan_bssid_ex *src, struct wlan_bssid_ex *dst);

void rtw_roaming(struct adapter *padapter, struct wlan_network *tgt_network);
void _rtw_roaming(struct adapter *padapter, struct wlan_network *tgt_network);

void rtw_stassoc_hw_rpt(struct adapter *adapter, struct sta_info *psta);

#endif /* __RTL871X_MLME_H_ */
