/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
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

#define	SCANNING_TIMEOUT	8000

#define	SCAN_INTERVAL	(30) /*  unit:2sec, 30*2 = 60sec */

#define	SCANQUEUE_LIFETIME 20 /*  unit:sec */

#define	WIFI_NULL_STATE		0x00000000

#define	WIFI_ASOC_STATE		0x00000001 /*  Under Linked state.*/
#define	WIFI_REASOC_STATE	0x00000002
#define	WIFI_SLEEP_STATE	0x00000004
#define	WIFI_STATION_STATE	0x00000008

#define	WIFI_AP_STATE		0x00000010
#define	WIFI_ADHOC_STATE	0x00000020
#define   WIFI_ADHOC_MASTER_STATE	0x00000040
#define   WIFI_UNDER_LINKING	0x00000080

#define	WIFI_UNDER_WPS		0x00000100
#define	WIFI_STA_ALIVE_CHK_STATE	0x00000400
/* to indicate the station is under site surveying */
#define	WIFI_SITE_MONITOR	0x00000800

#define	WIFI_MP_STATE		0x00010000
#define	WIFI_MP_CTX_BACKGROUND	0x00020000	/*  in continous tx background */
#define	WIFI_MP_CTX_ST		0x00040000	/*  in continous tx with single-tone */
#define	WIFI_MP_CTX_BACKGROUND_PENDING	0x00080000	/*  pending in continous tx background due to out of skb */
#define	WIFI_MP_CTX_CCK_HW	0x00100000	/*  in continous tx */
#define	WIFI_MP_CTX_CCK_CS	0x00200000	/*  in continous tx with carrier suppression */
#define   WIFI_MP_LPBK_STATE	0x00400000

#define _FW_UNDER_LINKING	WIFI_UNDER_LINKING
#define _FW_LINKED		WIFI_ASOC_STATE
#define _FW_UNDER_SURVEY	WIFI_SITE_MONITOR


enum dot11AuthAlgrthmNum {
	dot11AuthAlgrthm_Open = 0,
	dot11AuthAlgrthm_Shared,
	dot11AuthAlgrthm_8021X,
	dot11AuthAlgrthm_Auto,
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
};

/*

there are several "locks" in mlme_priv,
since mlme_priv is a shared resource between many threads,
like ISR/Call-Back functions, the OID handlers, and even timer functions.


Each _queue has its own locks, already.
Other items are protected by mlme_priv.lock.

To avoid possible dead lock, any thread trying to modifiying mlme_priv
SHALL not lock up more than one locks at a time!
*/

struct rt_link_detect {
	u32	NumTxOkInPeriod;
	u32	NumRxOkInPeriod;
	u32	NumRxUnicastOkInPeriod;
	bool	bBusyTraffic;
	bool	bTxBusyTraffic;
	bool	bRxBusyTraffic;
	bool	bHigherBusyTraffic; /*  For interrupt migration purpose. */
	bool	bHigherBusyRxTraffic; /*  We may disable Tx interrupt according as Rx traffic. */
	bool	bHigherBusyTxTraffic; /*  We may disable Tx interrupt according as Tx traffic. */
};

struct mlme_priv {
	spinlock_t	lock;
	int	fw_state;
	u8 bScanInProcess;
	u8	to_join; /* flag */
	u8 to_roaming; /*  roaming trying times */

	struct rtw_adapter *nic_hdl;

	u8	not_indic_disco;
	struct rtw_queue	scanned_queue;

	struct cfg80211_ssid assoc_ssid;
	u8	assoc_bssid[6];

	struct wlan_network	cur_network;

	/* uint wireless_mode; no used, remove it */

	u32	scan_interval;

	struct timer_list assoc_timer;

	uint assoc_by_bssid;
	uint assoc_by_rssi;

	struct timer_list scan_to_timer;

	struct timer_list set_scan_deny_timer;
	atomic_t set_scan_deny; /* 0: allowed, 1: deny */

	unsigned int qos_option;

	/* Number of non-HT AP/stations */
	int num_sta_no_ht;

	int num_FortyMHzIntolerant;

	struct ht_priv	htpriv;

	struct rt_link_detect LinkDetectInfo;
	struct timer_list dynamic_chk_timer; /* dynamic/periodic check timer */

	u8	key_mask; /* use for ips to set wep key after ips_leave23a */
	u8	acm_mask; /*  for wmm acm mask */
	u8	ChannelPlan;
	enum rt_scan_type scan_mode; /*  active: 1, passive: 0 */

	u8 *wps_probe_req_ie;
	u32 wps_probe_req_ie_len;
	u8 *assoc_req;
	u32 assoc_req_len;
	u32 assoc_rsp_len;
	u8 *assoc_rsp;
	u32 wps_assoc_resp_ie_len;
	u8 *wps_assoc_resp_ie;
	u8 *wps_probe_resp_ie;
	u32 wps_probe_resp_ie_len;
	u8 *wps_beacon_ie;
	u32 wps_beacon_ie_len;
	u32 p2p_go_probe_resp_ie_len; /* for GO */
	u32 p2p_assoc_req_ie_len;
	u8 *p2p_beacon_ie;
	u8 *p2p_probe_req_ie;
	u8 *p2p_probe_resp_ie;
	u8 *p2p_go_probe_resp_ie; /* for GO */
	u8 *p2p_assoc_req_ie;
	u32 p2p_beacon_ie_len;
	u32 p2p_probe_req_ie_len;
	u32 p2p_probe_resp_ie_len;
	u8 *wfd_assoc_req_ie;
	u32 wfd_assoc_req_ie_len;

#ifdef CONFIG_8723AU_AP_MODE
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

	spinlock_t	bcn_update_lock;
	u8		update_bcn;

#endif /* ifdef CONFIG_8723AU_AP_MODE */

	u8 *wfd_beacon_ie;
	u8 *wfd_probe_req_ie;
	u8 *wfd_probe_resp_ie;
	u8 *wfd_go_probe_resp_ie; /* for GO */

	u32 wfd_beacon_ie_len;
	u32 wfd_probe_req_ie_len;
	u32 wfd_probe_resp_ie_len;
	u32 wfd_go_probe_resp_ie_len; /* for GO */
};

void rtw_joinbss_event_prehandle23a(struct rtw_adapter *adapter, u8 *pbuf);
void rtw_survey_event_cb23a(struct rtw_adapter *adapter, const u8 *pbuf);
void rtw_surveydone_event_callback23a(struct rtw_adapter *adapter, const u8 *pbuf);
void rtw23a_joinbss_event_cb(struct rtw_adapter *adapter, const u8 *pbuf);
void rtw_stassoc_event_callback23a(struct rtw_adapter *adapter, const u8 *pbuf);
void rtw_stadel_event_callback23a(struct rtw_adapter *adapter, const u8 *pbuf);

int event_thread(void *context);
void rtw23a_join_to_handler(unsigned long);

void rtw_free_network_queue23a(struct rtw_adapter *adapter);
int rtw_init_mlme_priv23a(struct rtw_adapter *adapter);

void rtw_free_mlme_priv23a(struct mlme_priv *pmlmepriv);

int rtw_do_join_adhoc(struct rtw_adapter *adapter);
int rtw_do_join_network(struct rtw_adapter *adapter,
			struct wlan_network *candidate);
int rtw_select_and_join_from_scanned_queue23a(struct mlme_priv *pmlmepriv);
int rtw_set_key23a(struct rtw_adapter *adapter,
		struct security_priv *psecuritypriv, int keyid, u8 set_tx);
int rtw_set_auth23a(struct rtw_adapter *adapter,
		 struct security_priv *psecuritypriv);

static inline u8 *get_bssid(struct mlme_priv *pmlmepriv)
{	/* if sta_mode:pmlmepriv->cur_network.network.MacAddress => bssid */
	/*  if adhoc_mode:pmlmepriv->cur_network.network.MacAddress => ibss mac address */
	return pmlmepriv->cur_network.network.MacAddress;
}

static inline bool check_fwstate(struct mlme_priv *pmlmepriv, int state)
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
	if (check_fwstate(pmlmepriv, state) == true)
		pmlmepriv->fw_state ^= state;
	spin_unlock_bh(&pmlmepriv->lock);
}

static inline void clr_fwstate_ex(struct mlme_priv *pmlmepriv, int state)
{
	spin_lock_bh(&pmlmepriv->lock);
	_clr_fwstate_(pmlmepriv, state);
	spin_unlock_bh(&pmlmepriv->lock);
}

u16 rtw_get_capability23a(struct wlan_bssid_ex *bss);
void rtw_disconnect_hdl23a_under_linked(struct rtw_adapter *adapter,
				     struct sta_info *psta, u8 free_assoc);
void rtw_generate_random_ibss23a(u8 *pibss);
struct wlan_network *rtw_find_network23a(struct rtw_queue *scanned_queue, u8 *addr);
struct wlan_network *rtw_get_oldest_wlan_network23a(struct rtw_queue *scanned_queue);

void rtw_free_assoc_resources23a(struct rtw_adapter *adapter,
			      int lock_scanned_queue);
void rtw_indicate_disconnect23a(struct rtw_adapter *adapter);
void rtw_indicate_connect23a(struct rtw_adapter *adapter);
void rtw_scan_abort23a(struct rtw_adapter *adapter);

int rtw_restruct_sec_ie23a(struct rtw_adapter *adapter, u8 *in_ie, u8 *out_ie,
			uint in_len);
int rtw_restruct_wmm_ie23a(struct rtw_adapter *adapter, u8 *in_ie, u8 *out_ie,
			uint in_len, uint initial_out_len);
void rtw_init_registrypriv_dev_network23a(struct rtw_adapter *adapter);

void rtw_update_registrypriv_dev_network23a(struct rtw_adapter *adapter);

void rtw_scan_timeout_handler23a(unsigned long data);

void rtw_dynamic_check_timer_handler(unsigned long data);
bool rtw_is_scan_deny(struct rtw_adapter *adapter);
void rtw_clear_scan_deny(struct rtw_adapter *adapter);
void rtw_set_scan_deny_timer_hdl(unsigned long data);
void rtw_set_scan_deny(struct rtw_adapter *adapter, u32 ms);

void rtw23a_free_mlme_priv_ie_data(struct mlme_priv *pmlmepriv);

void _rtw_free_mlme_priv23a(struct mlme_priv *pmlmepriv);

struct wlan_network *rtw_alloc_network(struct mlme_priv *pmlmepriv, int gfp);

int rtw_if_up23a(struct rtw_adapter *padapter);

int rtw_linked_check(struct rtw_adapter *padapter);

__le16 *rtw_get_capability23a_from_ie(u8 *ie);
__le16 *rtw_get_beacon_interval23a_from_ie(u8 *ie);


void rtw_joinbss_reset23a(struct rtw_adapter *padapter);

bool rtw_restructure_ht_ie23a(struct rtw_adapter *padapter, u8 *in_ie,
			      u8 *out_ie, uint in_len, uint *pout_len);
void rtw_update_ht_cap23a(struct rtw_adapter *padapter,
		       u8 *pie, uint ie_len);
void rtw_issue_addbareq_cmd23a(struct rtw_adapter *padapter,
			    struct xmit_frame *pxmitframe);

bool rtw_is_same_ibss23a(struct rtw_adapter *adapter,
			 struct wlan_network *pnetwork);
int is_same_network23a(struct wlan_bssid_ex *src, struct wlan_bssid_ex *dst);

void rtw23a_roaming(struct rtw_adapter *adapter,
		 struct wlan_network *tgt_network);
void rtw_set_roaming(struct rtw_adapter *adapter, u8 to_roaming);

#endif /* __RTL871X_MLME_H_ */
