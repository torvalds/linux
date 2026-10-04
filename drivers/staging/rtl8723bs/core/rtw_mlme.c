// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#include <linux/etherdevice.h>
#include <drv_types.h>
#include <hal_btcoex.h>
#include <linux/jiffies.h>

static void _dynamic_check_timer_handler(struct timer_list *t)
{
	struct adapter *adapter =
		timer_container_of(adapter, t, mlmepriv.dynamic_chk_timer);

	rtw_dynamic_check_timer_handler(adapter);

	_set_timer(&adapter->mlmepriv.dynamic_chk_timer, 2000);
}

static void _rtw_set_scan_deny_timer_hdl(struct timer_list *t)
{
	struct adapter *adapter =
		timer_container_of(adapter, t, mlmepriv.set_scan_deny_timer);

	rtw_clear_scan_deny(adapter);
}

static void rtw_init_mlme_timer(struct adapter *adapter)
{
	struct	mlme_priv *mlme_priv = &adapter->mlmepriv;

	timer_setup(&mlme_priv->assoc_timer, _rtw_join_timeout_handler, 0);
	timer_setup(&mlme_priv->scan_to_timer, rtw_scan_timeout_handler, 0);
	timer_setup(&mlme_priv->dynamic_chk_timer,
		    _dynamic_check_timer_handler, 0);
	timer_setup(&mlme_priv->set_scan_deny_timer,
		    _rtw_set_scan_deny_timer_hdl, 0);
}

int rtw_init_mlme_priv(struct adapter *adapter)
{
	int i;
	u8 *buf;
	struct wlan_network *pnetwork;
	struct mlme_priv *mlme_priv = &adapter->mlmepriv;
	int res = _SUCCESS;

	mlme_priv->nic_hdl = (u8 *)adapter;

	mlme_priv->pscanned = NULL;
	mlme_priv->fw_state = WIFI_STATION_STATE; /*  Must sync with rtw_wdev_alloc() */
	mlme_priv->cur_network.network.infrastructure_mode = NL80211_IFTYPE_UNSPECIFIED;
	mlme_priv->scan_mode = SCAN_ACTIVE;/*  1: active, 0: passive. Maybe someday we should rename this variable to "active_mode" (Jeff) */

	spin_lock_init(&mlme_priv->lock);
	INIT_LIST_HEAD(&mlme_priv->free_bss_pool.queue);
	spin_lock_init(&mlme_priv->free_bss_pool.lock);
	INIT_LIST_HEAD(&mlme_priv->scanned_queue.queue);
	spin_lock_init(&mlme_priv->scanned_queue.lock);

	memset(&mlme_priv->assoc_ssid, 0, sizeof(struct ndis_802_11_ssid));

	buf = vzalloc(array_size(MAX_BSS_CNT, sizeof(struct wlan_network)));

	if (!buf) {
		res = _FAIL;
		goto exit;
	}
	mlme_priv->free_bss_buf = buf;

	pnetwork = (struct wlan_network *)buf;

	for (i = 0; i < MAX_BSS_CNT; i++) {
		INIT_LIST_HEAD(&pnetwork->list);

		list_add_tail(&pnetwork->list, &mlme_priv->free_bss_pool.queue);

		pnetwork++;
	}

	/* allocate DMA-able/Non-Page memory for cmd_buf and rsp_buf */

	rtw_clear_scan_deny(adapter);

	#define RTW_ROAM_SCAN_RESULT_EXP_MS 5000
	#define RTW_ROAM_RSSI_DIFF_TH 10
	#define RTW_ROAM_SCAN_INTERVAL_MS 10000

	mlme_priv->roam_flags = 0
		| RTW_ROAM_ON_EXPIRED
		| RTW_ROAM_ON_RESUME
		;

	mlme_priv->roam_scanr_exp_ms = RTW_ROAM_SCAN_RESULT_EXP_MS;
	mlme_priv->roam_rssi_diff_th = RTW_ROAM_RSSI_DIFF_TH;
	mlme_priv->roam_scan_int_ms = RTW_ROAM_SCAN_INTERVAL_MS;

	rtw_init_mlme_timer(adapter);

exit:

	return res;
}

static void rtw_free_mlme_ie_data(u8 **ppie, u32 *plen)
{
	if (*ppie) {
		kfree(*ppie);
		*plen = 0;
		*ppie = NULL;
	}
}

void rtw_free_mlme_priv_ie_data(struct mlme_priv *mlme_priv)
{
	rtw_buf_free(&mlme_priv->assoc_req, &mlme_priv->assoc_req_len);
	rtw_buf_free(&mlme_priv->assoc_rsp, &mlme_priv->assoc_rsp_len);
	rtw_free_mlme_ie_data(&mlme_priv->wps_beacon_ie, &mlme_priv->wps_beacon_ie_len);
	rtw_free_mlme_ie_data(&mlme_priv->wps_probe_req_ie, &mlme_priv->wps_probe_req_ie_len);
	rtw_free_mlme_ie_data(&mlme_priv->wps_probe_resp_ie, &mlme_priv->wps_probe_resp_ie_len);
	rtw_free_mlme_ie_data(&mlme_priv->wps_assoc_resp_ie, &mlme_priv->wps_assoc_resp_ie_len);

	rtw_free_mlme_ie_data(&mlme_priv->p2p_beacon_ie, &mlme_priv->p2p_beacon_ie_len);
	rtw_free_mlme_ie_data(&mlme_priv->p2p_probe_req_ie, &mlme_priv->p2p_probe_req_ie_len);
	rtw_free_mlme_ie_data(&mlme_priv->p2p_probe_resp_ie, &mlme_priv->p2p_probe_resp_ie_len);
	rtw_free_mlme_ie_data(&mlme_priv->p2p_go_probe_resp_ie, &mlme_priv->p2p_go_probe_resp_ie_len);
	rtw_free_mlme_ie_data(&mlme_priv->p2p_assoc_req_ie, &mlme_priv->p2p_assoc_req_ie_len);
}

void _rtw_free_mlme_priv(struct mlme_priv *mlme_priv)
{
	if (mlme_priv) {
		rtw_free_mlme_priv_ie_data(mlme_priv);
		vfree(mlme_priv->free_bss_buf);
	}
}

struct wlan_network *rtw_alloc_network(struct mlme_priv *mlme_priv)
{
	struct wlan_network *pnetwork;
	struct __queue *free_queue = &mlme_priv->free_bss_pool;
	struct list_head *plist = NULL;

	spin_lock_bh(&free_queue->lock);

	if (list_empty(&free_queue->queue)) {
		pnetwork = NULL;
		goto exit;
	}
	plist = get_next(&free_queue->queue);

	pnetwork = container_of(plist, struct wlan_network, list);

	list_del_init(&pnetwork->list);

	pnetwork->network_type = 0;
	pnetwork->fixed = false;
	pnetwork->last_scanned = jiffies;
	pnetwork->aid = 0;
	pnetwork->join_res = 0;

exit:
	spin_unlock_bh(&free_queue->lock);

	return pnetwork;
}

void _rtw_free_network(struct mlme_priv *mlme_priv, struct wlan_network *pnetwork, u8 isfreeall)
{
	unsigned int delta_time;
	u32 lifetime = SCANQUEUE_LIFETIME;
	struct __queue *free_queue = &mlme_priv->free_bss_pool;

	if (!pnetwork)
		return;

	if (pnetwork->fixed)
		return;

	if (check_fwstate(mlme_priv, WIFI_ADHOC_MASTER_STATE) ||
	    check_fwstate(mlme_priv, WIFI_ADHOC_STATE))
		lifetime = 1;

	if (!isfreeall) {
		delta_time = jiffies_to_msecs(jiffies - pnetwork->last_scanned);
		if (delta_time < lifetime)/*  unit:msec */
			return;
	}

	spin_lock_bh(&free_queue->lock);

	list_del_init(&pnetwork->list);

	list_add_tail(&pnetwork->list, &free_queue->queue);

	spin_unlock_bh(&free_queue->lock);
}

void _rtw_free_network_nolock(struct mlme_priv *mlme_priv, struct wlan_network *pnetwork)
{
	struct __queue *free_queue = &mlme_priv->free_bss_pool;

	if (!pnetwork)
		return;

	if (pnetwork->fixed)
		return;

	list_del_init(&pnetwork->list);

	list_add_tail(&pnetwork->list, get_list_head(free_queue));
}

/*
 * return the wlan_network with the matching addr
 *
 * Shall be called under atomic context... to avoid possible racing condition...
 */
struct wlan_network *_rtw_find_network(struct __queue *scanned_queue, u8 *addr)
{
	struct list_head *phead, *plist;
	struct wlan_network *pnetwork = NULL;

	if (is_zero_ether_addr(addr)) {
		pnetwork = NULL;
		goto exit;
	}

	phead = get_list_head(scanned_queue);
	list_for_each(plist, phead) {
		pnetwork = list_entry(plist, struct wlan_network, list);

		if (!memcmp(addr, pnetwork->network.mac_address, ETH_ALEN))
			break;
	}

	if (plist == phead)
		pnetwork = NULL;

exit:
	return pnetwork;
}

void rtw_free_network_queue(struct adapter *adapter, u8 isfreeall)
{
	struct list_head *phead, *plist, *tmp;
	struct wlan_network *pnetwork;
	struct mlme_priv *mlme_priv = &adapter->mlmepriv;
	struct __queue *scanned_queue = &mlme_priv->scanned_queue;

	spin_lock_bh(&scanned_queue->lock);

	phead = get_list_head(scanned_queue);
	list_for_each_safe(plist, tmp, phead) {
		pnetwork = list_entry(plist, struct wlan_network, list);

		_rtw_free_network(mlme_priv, pnetwork, isfreeall);
	}

	spin_unlock_bh(&scanned_queue->lock);
}

bool rtw_if_up(struct adapter *adapter)
{
	if (adapter->driver_stopped || adapter->bSurpriseRemoved ||
	    !check_fwstate(&adapter->mlmepriv, _FW_LINKED))
		return false;

	return true;
}

void rtw_generate_random_ibss(u8 *ibss)
{
	unsigned long curtime = jiffies;

	ibss[0] = 0x02;  /* in ad-hoc mode bit1 must set to 1 */
	ibss[1] = 0x11;
	ibss[2] = 0x87;
	ibss[3] = (u8)(curtime & 0xff) ;/* p[0]; */
	ibss[4] = (u8)((curtime >> 8) & 0xff) ;/* p[1]; */
	ibss[5] = (u8)((curtime >> 16) & 0xff) ;/* p[2]; */
}

u8 *rtw_get_capability_from_ie(u8 *ie)
{
	return ie + 8 + 2;
}

u16 rtw_get_capability(struct wlan_bssid_ex *bss)
{
	__le16	val;

	memcpy((u8 *)&val, rtw_get_capability_from_ie(bss->ies), 2);

	return le16_to_cpu(val);
}

u8 *rtw_get_beacon_interval_from_ie(u8 *ie)
{
	return ie + 8;
}

void rtw_free_mlme_priv(struct mlme_priv *mlme_priv)
{
	_rtw_free_mlme_priv(mlme_priv);
}

void rtw_free_network_nolock(struct adapter *adapter, struct wlan_network *pnetwork);
void rtw_free_network_nolock(struct adapter *adapter, struct wlan_network *pnetwork)
{
	_rtw_free_network_nolock(&adapter->mlmepriv, pnetwork);
	rtw_cfg80211_unlink_bss(adapter, pnetwork);
}

/*
 * return the wlan_network with the matching addr
 *
 * Shall be called under atomic context... to avoid possible racing condition...
 */
struct wlan_network *rtw_find_network(struct __queue *scanned_queue, u8 *addr)
{
	struct wlan_network *pnetwork = _rtw_find_network(scanned_queue, addr);

	return pnetwork;
}

bool rtw_is_same_ibss(struct adapter *adapter, struct wlan_network *pnetwork)
{
	struct security_priv *psecuritypriv = &adapter->securitypriv;

	if ((psecuritypriv->dot11_privacy_algrthm != _NO_PRIVACY_) &&
	    (pnetwork->network.privacy == 0))
		return false;
	else if ((psecuritypriv->dot11_privacy_algrthm == _NO_PRIVACY_) &&
		 (pnetwork->network.privacy == 1))
		return false;

	return true;
}

inline int is_same_ess(struct wlan_bssid_ex *a, struct wlan_bssid_ex *b)
{
	return (a->ssid.ssid_length == b->ssid.ssid_length) &&
		!memcmp(a->ssid.ssid, b->ssid.ssid, a->ssid.ssid_length);
}

int is_same_network(struct wlan_bssid_ex *src, struct wlan_bssid_ex *dst, u8 feature)
{
	u16 s_cap, d_cap;
	__le16 tmps, tmpd;

	memcpy((u8 *)&tmps, rtw_get_capability_from_ie(src->ies), 2);
	memcpy((u8 *)&tmpd, rtw_get_capability_from_ie(dst->ies), 2);

	s_cap = le16_to_cpu(tmps);
	d_cap = le16_to_cpu(tmpd);

	return (src->ssid.ssid_length == dst->ssid.ssid_length) &&
			((!memcmp(src->mac_address, dst->mac_address, ETH_ALEN))) &&
			((!memcmp(src->ssid.ssid, dst->ssid.ssid, src->ssid.ssid_length))) &&
			((s_cap & WLAN_CAPABILITY_IBSS) ==
			(d_cap & WLAN_CAPABILITY_IBSS)) &&
			((s_cap & WLAN_CAPABILITY_ESS) ==
			(d_cap & WLAN_CAPABILITY_ESS));
}

struct wlan_network *_rtw_find_same_network(struct __queue *scanned_queue, struct wlan_network *network)
{
	struct list_head *phead, *plist;
	struct wlan_network *found = NULL;

	phead = get_list_head(scanned_queue);
	list_for_each(plist, phead) {
		found = list_entry(plist, struct wlan_network, list);

		if (is_same_network(&network->network, &found->network, 0))
			break;
	}

	if (plist == phead)
		found = NULL;

	return found;
}

struct wlan_network *rtw_get_oldest_wlan_network(struct __queue *scanned_queue)
{
	struct list_head *plist, *phead;

	struct	wlan_network *pwlan = NULL;
	struct	wlan_network *oldest = NULL;

	phead = get_list_head(scanned_queue);

	list_for_each(plist, phead) {
		pwlan = list_entry(plist, struct wlan_network, list);

		if (!pwlan->fixed) {
			if (!oldest || time_after(oldest->last_scanned, pwlan->last_scanned))
				oldest = pwlan;
		}
	}
	return oldest;
}

void update_network(struct wlan_bssid_ex *dst, struct wlan_bssid_ex *src,
		    struct adapter *adapter, bool update_ie)
{
	long rssi_ori = dst->rssi;

	u8 sq_smp = src->phy_info.signal_quality;

	u8 ss_final;
	u8 sq_final;
	long rssi_final;

	/* The rule below is 1/5 for sample value, 4/5 for history value */
	if (check_fwstate(&adapter->mlmepriv, _FW_LINKED) && is_same_network(&adapter->mlmepriv.cur_network.network, src, 0)) {
		/* Take the recvpriv's value for the connected AP*/
		ss_final = adapter->recvpriv.signal_strength;
		sq_final = adapter->recvpriv.signal_qual;
		/* the rssi value here is undecorated, and will be used for antenna diversity */
		if (sq_smp != 101) /* from the right channel */
			rssi_final = (src->rssi + dst->rssi * 4) / 5;
		else
			rssi_final = rssi_ori;
	} else {
		if (sq_smp != 101) { /* from the right channel */
			ss_final = ((u32)(src->phy_info.signal_strength) +
				    (u32)(dst->phy_info.signal_strength) * 4) / 5;
			sq_final = ((u32)(src->phy_info.signal_quality) +
				    (u32)(dst->phy_info.signal_quality) * 4) / 5;
			rssi_final = (src->rssi + dst->rssi * 4) / 5;
		} else {
			/* bss info not receiving from the right channel, use the original RX signal infos */
			ss_final = dst->phy_info.signal_strength;
			sq_final = dst->phy_info.signal_quality;
			rssi_final = dst->rssi;
		}
	}

	if (update_ie) {
		dst->reserved[0] = src->reserved[0];
		dst->reserved[1] = src->reserved[1];
		memcpy((u8 *)dst, (u8 *)src, get_wlan_bssid_ex_sz(src));
	}

	dst->phy_info.signal_strength = ss_final;
	dst->phy_info.signal_quality = sq_final;
	dst->rssi = rssi_final;
}

static void update_current_network(struct adapter *adapter, struct wlan_bssid_ex *pnetwork)
{
	struct	mlme_priv *mlme_priv = &adapter->mlmepriv;

	if (check_fwstate(mlme_priv, _FW_LINKED) && (is_same_network(&mlme_priv->cur_network.network, pnetwork, 0))) {
		update_network(&mlme_priv->cur_network.network, pnetwork, adapter, true);
		if (mlme_priv->cur_network.network.ie_length < sizeof(struct ndis_802_11_fix_ie))
			return;

		rtw_update_protection(adapter, (mlme_priv->cur_network.network.ies) + sizeof(struct ndis_802_11_fix_ie),
								mlme_priv->cur_network.network.ie_length - sizeof(struct ndis_802_11_fix_ie));
	}
}

/* Caller must hold mlme_priv->lock first. */
void rtw_update_scanned_network(struct adapter *adapter, struct wlan_bssid_ex *target)
{
	struct list_head *plist, *phead;
	u32 bssid_ex_sz;
	struct mlme_priv *mlme_priv = &adapter->mlmepriv;
	struct __queue *queue = &mlme_priv->scanned_queue;
	struct wlan_network *pnetwork = NULL;
	struct wlan_network *oldest = NULL;
	int target_find = 0;
	u8 feature = 0;

	spin_lock_bh(&queue->lock);
	phead = get_list_head(queue);
	list_for_each(plist, phead) {
		pnetwork = list_entry(plist, struct wlan_network, list);

		if (is_same_network(&pnetwork->network, target, feature)) {
			target_find = 1;
			break;
		}

		if (rtw_roam_flags(adapter)) {
			/* TODO: don't select network in the same ess as oldest if it's new enough*/
		}

		if (!oldest || time_after(oldest->last_scanned, pnetwork->last_scanned))
			oldest = pnetwork;
	}

	/*
	 * If we didn't find a match, then get a new network slot to initialize
	 * with this beacon's information
	 */
	if (!target_find) {
		if (list_empty(&mlme_priv->free_bss_pool.queue)) {
			/* If there are no more slots, expire the oldest */
			/* list_del_init(&oldest->list); */
			pnetwork = oldest;
			if (!pnetwork)
				goto exit;

			memcpy(&pnetwork->network, target,  get_wlan_bssid_ex_sz(target));
			/*  variable initialize */
			pnetwork->fixed = false;
			pnetwork->last_scanned = jiffies;

			pnetwork->network_type = 0;
			pnetwork->aid = 0;
			pnetwork->join_res = 0;

			/* bss info not receiving from the right channel */
			if (pnetwork->network.phy_info.signal_quality == 101)
				pnetwork->network.phy_info.signal_quality = 0;
		} else {
			/* Otherwise just pull from the free list */

			pnetwork = rtw_alloc_network(mlme_priv); /*  will update scan_time */

			if (!pnetwork)
				goto exit;

			bssid_ex_sz = get_wlan_bssid_ex_sz(target);
			target->length = bssid_ex_sz;
			memcpy(&pnetwork->network, target, bssid_ex_sz);

			pnetwork->last_scanned = jiffies;

			/* bss info not receiving from the right channel */
			if (pnetwork->network.phy_info.signal_quality == 101)
				pnetwork->network.phy_info.signal_quality = 0;

			list_add_tail(&pnetwork->list, &queue->queue);
		}
	} else {
		/* we have an entry and we are going to update it. But this entry may
		 * be already expired. In this case we do the same as we found a new
		 * net and call the new_net handler
		 */
		bool update_ie = true;

		pnetwork->last_scanned = jiffies;

		/* target.reserved[0]== 1, means that scanned network is a bcn frame. */
		if (pnetwork->network.ie_length > target->ie_length && target->reserved[0] == 1)
			update_ie = false;

		/*  probe resp(3) > beacon(1) > probe req(2) */
		if (target->reserved[0] != 2 &&
		    target->reserved[0] >= pnetwork->network.reserved[0]) {
			update_ie = true;
		} else {
			update_ie = false;
		}

		update_network(&pnetwork->network, target, adapter, update_ie);
	}

exit:
	spin_unlock_bh(&queue->lock);
}

static void rtw_add_network(struct adapter *adapter, struct wlan_bssid_ex *pnetwork)
{
	update_current_network(adapter, pnetwork);
	rtw_update_scanned_network(adapter, pnetwork);
}

/* select the desired network based on the capability of the (i)bss.
 * check items:
 * (1) security
 * (2) network_type
 * (3) WMM
 * (4) HT
 * (5) others
 */
static bool rtw_is_desired_network(struct adapter *adapter, struct wlan_network *pnetwork)
{
	struct security_priv *psecuritypriv = &adapter->securitypriv;
	struct mlme_priv *mlme_priv = &adapter->mlmepriv;
	u32 desired_encmode;
	u32 privacy;
	uint wps_ielen;
	bool bselected = true;

	desired_encmode = psecuritypriv->ndisencryptstatus;
	privacy = pnetwork->network.privacy;

	if (check_fwstate(mlme_priv, WIFI_UNDER_WPS)) {
		if (pnetwork->network.ie_length < _FIXED_IE_LENGTH_)
			return false;
		if (rtw_get_wps_ie(pnetwork->network.ies + _FIXED_IE_LENGTH_, pnetwork->network.ie_length - _FIXED_IE_LENGTH_, NULL, &wps_ielen))
			return true;
		else
			return false;
	}

	if (adapter->registrypriv.wifi_spec == 1) { /* for  correct flow of 8021X  to do.... */
		u8 *p = NULL;
		uint ie_len = 0;

		if ((desired_encmode == NDIS_802_11_ENCRYPTION_DISABLED) && (privacy != 0))
			bselected = false;

		if (psecuritypriv->ndisauthtype == Ndis802_11AuthModeWPA2PSK) {
			if (pnetwork->network.ie_length < _BEACON_IE_OFFSET_) {
				bselected = false;
			} else {
				p = rtw_get_ie(pnetwork->network.ies + _BEACON_IE_OFFSET_, WLAN_EID_RSN, &ie_len, (pnetwork->network.ie_length - _BEACON_IE_OFFSET_));
				if (p && ie_len > 0)
					bselected = true;
				else
					bselected = false;
			}
		}
	}

	if ((desired_encmode != NDIS_802_11_ENCRYPTION_DISABLED) && (privacy == 0))
		bselected = false;

	if (check_fwstate(mlme_priv, WIFI_ADHOC_STATE)) {
		if (pnetwork->network.infrastructure_mode != mlme_priv->cur_network.network.infrastructure_mode)
			bselected = false;
	}

	return bselected;
}

void rtw_survey_event_callback(struct adapter *adapter, u8 *buf)
{
	u32 len;
	struct wlan_bssid_ex *pnetwork;
	struct mlme_priv *mlme_priv = &adapter->mlmepriv;

	pnetwork = (struct wlan_bssid_ex *)buf;

	len = get_wlan_bssid_ex_sz(pnetwork);
	if (len > (sizeof(struct wlan_bssid_ex)))
		return;

	spin_lock_bh(&mlme_priv->lock);

	/*  update IBSS_network 's timestamp */
	if (check_fwstate(mlme_priv, WIFI_ADHOC_MASTER_STATE)) {
		if (!memcmp(&mlme_priv->cur_network.network.mac_address, pnetwork->mac_address, ETH_ALEN)) {
			struct wlan_network *ibss_wlan = NULL;

			memcpy(mlme_priv->cur_network.network.ies, pnetwork->ies, 8);
			spin_lock_bh(&mlme_priv->scanned_queue.lock);
			ibss_wlan = rtw_find_network(&mlme_priv->scanned_queue,  pnetwork->mac_address);
			if (ibss_wlan) {
				memcpy(ibss_wlan->network.ies, pnetwork->ies, 8);
				spin_unlock_bh(&mlme_priv->scanned_queue.lock);
				goto exit;
			}
			spin_unlock_bh(&mlme_priv->scanned_queue.lock);
		}
	}

	/*  lock mlme_priv->lock when you accessing network_q */
	if (!check_fwstate(mlme_priv, _FW_UNDER_LINKING)) {
		if (pnetwork->ssid.ssid[0] == 0)
			pnetwork->ssid.ssid_length = 0;
		rtw_add_network(adapter, pnetwork);
	}

exit:

	spin_unlock_bh(&mlme_priv->lock);
}

void rtw_surveydone_event_callback(struct adapter *adapter, u8 *buf)
{
	struct mlme_priv *mlme_priv = &adapter->mlmepriv;

	spin_lock_bh(&mlme_priv->lock);
	if (mlme_priv->wps_probe_req_ie) {
		mlme_priv->wps_probe_req_ie_len = 0;
		kfree(mlme_priv->wps_probe_req_ie);
		mlme_priv->wps_probe_req_ie = NULL;
	}

	if (check_fwstate(mlme_priv, _FW_UNDER_SURVEY)) {
		spin_unlock_bh(&mlme_priv->lock);
		timer_delete_sync(&mlme_priv->scan_to_timer);
		spin_lock_bh(&mlme_priv->lock);
		_clr_fwstate_(mlme_priv, _FW_UNDER_SURVEY);
	}

	rtw_set_signal_stat_timer(&adapter->recvpriv);

	if (mlme_priv->to_join) {
		if (check_fwstate(mlme_priv, WIFI_ADHOC_STATE)) {
			if (!check_fwstate(mlme_priv, _FW_LINKED)) {
				set_fwstate(mlme_priv, _FW_UNDER_LINKING);

				if (rtw_select_and_join_from_scanned_queue(mlme_priv) == _SUCCESS) {
					_set_timer(&mlme_priv->assoc_timer, MAX_JOIN_TIMEOUT);
				} else {
					u8 ret = _SUCCESS;
					struct wlan_bssid_ex *dev_network =
						&adapter->registrypriv.dev_network;

					u8 *ibss = adapter->registrypriv.dev_network.mac_address;

					/* mlme_priv->fw_state ^= _FW_UNDER_SURVEY;because don't set assoc_timer */
					_clr_fwstate_(mlme_priv, _FW_UNDER_SURVEY);

					memcpy(&dev_network->ssid, &mlme_priv->assoc_ssid,
					       sizeof(struct ndis_802_11_ssid));

					rtw_update_registrypriv_dev_network(adapter);
					rtw_generate_random_ibss(ibss);

					mlme_priv->fw_state = WIFI_ADHOC_MASTER_STATE;

					mlme_priv->to_join = false;

					ret = rtw_createbss_cmd(adapter);
					if (ret != _SUCCESS)
						goto unlock;
				}
			}
		} else {
			int s_ret;

			set_fwstate(mlme_priv, _FW_UNDER_LINKING);
			mlme_priv->to_join = false;
			s_ret = rtw_select_and_join_from_scanned_queue(mlme_priv);
			if (s_ret == _SUCCESS) {
				_set_timer(&mlme_priv->assoc_timer, MAX_JOIN_TIMEOUT);
			} else if (s_ret == 2) {/* there is no need to wait for join */
				_clr_fwstate_(mlme_priv, _FW_UNDER_LINKING);
				rtw_indicate_connect(adapter);
			} else {
				if (rtw_to_roam(adapter) != 0) {
					if (rtw_dec_to_roam(adapter) == 0 ||
					    rtw_sitesurvey_cmd(adapter, &mlme_priv->assoc_ssid,
							       1, NULL, 0) != _SUCCESS) {
						rtw_set_to_roam(adapter, 0);
						rtw_free_assoc_resources(adapter, 1);
						rtw_indicate_disconnect(adapter);
					} else {
						mlme_priv->to_join = true;
					}
				} else {
					rtw_indicate_disconnect(adapter);
				}
				_clr_fwstate_(mlme_priv, _FW_UNDER_LINKING);
			}
		}
	} else {
		if (rtw_chk_roam_flags(adapter, RTW_ROAM_ACTIVE)) {
			if (check_fwstate(mlme_priv, WIFI_STATION_STATE) &&
			    check_fwstate(mlme_priv, _FW_LINKED)) {
				if (rtw_select_roaming_candidate(mlme_priv) == _SUCCESS) {
					receive_disconnect(adapter, mlme_priv->cur_network.network.mac_address
						, WLAN_REASON_ACTIVE_ROAM);
				}
			}
		}
	}

unlock:
	spin_unlock_bh(&mlme_priv->lock);

	rtw_os_xmit_schedule(adapter);

	rtw_cfg80211_surveydone_event_callback(adapter);

	rtw_indicate_scan_done(adapter, false);
}

void rtw_dummy_event_callback(struct adapter *adapter, u8 *buf)
{
}

void rtw_fwdbg_event_callback(struct adapter *adapter, u8 *buf)
{
}

static void free_scanqueue(struct mlme_priv *mlme_priv)
{
	struct __queue *free_queue = &mlme_priv->free_bss_pool;
	struct __queue *scan_queue = &mlme_priv->scanned_queue;
	struct list_head *plist, *phead, *ptemp;

	spin_lock_bh(&scan_queue->lock);
	spin_lock_bh(&free_queue->lock);

	phead = get_list_head(scan_queue);
	plist = get_next(phead);

	while (plist != phead) {
		ptemp = get_next(plist);
		list_del_init(plist);
		list_add_tail(plist, &free_queue->queue);
		plist = ptemp;
	}

	spin_unlock_bh(&free_queue->lock);
	spin_unlock_bh(&scan_queue->lock);
}

static void find_network(struct adapter *adapter)
{
	struct wlan_network *pwlan = NULL;
	struct mlme_priv *mlme_priv = &adapter->mlmepriv;
	struct wlan_network *tgt_network = &mlme_priv->cur_network;

	pwlan = rtw_find_network(&mlme_priv->scanned_queue, tgt_network->network.mac_address);
	if (!pwlan)
		return;

	pwlan->fixed = false;

	if (check_fwstate(mlme_priv, WIFI_ADHOC_MASTER_STATE) &&
	    (adapter->stapriv.asoc_sta_count == 1))
		rtw_free_network_nolock(adapter, pwlan);
}

/* rtw_free_assoc_resources: the caller has to lock mlme_priv->lock */
void rtw_free_assoc_resources(struct adapter *adapter, int lock_scanned_queue)
{
	struct mlme_priv *mlme_priv = &adapter->mlmepriv;
	struct wlan_network *tgt_network = &mlme_priv->cur_network;

	if (check_fwstate(mlme_priv, WIFI_STATION_STATE | WIFI_AP_STATE)) {
		struct sta_info *psta;

		psta = rtw_get_stainfo(&adapter->stapriv, tgt_network->network.mac_address);
		rtw_free_stainfo(adapter, psta);
	}

	if (check_fwstate(mlme_priv, WIFI_ADHOC_STATE | WIFI_ADHOC_MASTER_STATE | WIFI_AP_STATE)) {
		struct sta_info *psta;

		rtw_free_all_stainfo(adapter);

		psta = rtw_get_bcmc_stainfo(adapter);
		rtw_free_stainfo(adapter, psta);

		rtw_init_bcmc_stainfo(adapter);
	}

	find_network(adapter);

	if (lock_scanned_queue)
		adapter->securitypriv.key_mask = 0;
}

/* rtw_indicate_connect: the caller has to lock mlme_priv->lock */
void rtw_indicate_connect(struct adapter *adapter)
{
	struct mlme_priv *mlme_priv = &adapter->mlmepriv;

	mlme_priv->to_join = false;

	if (!check_fwstate(&adapter->mlmepriv, _FW_LINKED)) {
		set_fwstate(mlme_priv, _FW_LINKED);

		if (check_fwstate(mlme_priv, WIFI_ADHOC_MASTER_STATE) ||
		    check_fwstate(mlme_priv, WIFI_ADHOC_STATE))
			rtw_cfg80211_ibss_indicate_connect(adapter);
		else
			rtw_cfg80211_indicate_connect(adapter);

		netif_carrier_on(adapter->pnetdev);

		if (adapter->pid[2] != 0)
			rtw_signal_process(adapter->pid[2], SIGALRM);
	}

	rtw_set_to_roam(adapter, 0);
	rtw_set_scan_deny(adapter, 3000);
}

/* rtw_indicate_disconnect: the caller has to lock mlme_priv->lock */
void rtw_indicate_disconnect(struct adapter *adapter)
{
	struct	mlme_priv *mlme_priv = &adapter->mlmepriv;

	_clr_fwstate_(mlme_priv, _FW_UNDER_LINKING | WIFI_UNDER_WPS);

	if (rtw_to_roam(adapter) > 0)
		_clr_fwstate_(mlme_priv, _FW_LINKED);

	if (check_fwstate(&adapter->mlmepriv, _FW_LINKED) || rtw_to_roam(adapter) <= 0) {
		/*  Do it first for tx broadcast pkt after disconnection issue! */
		netif_carrier_off(adapter->pnetdev);

		rtw_cfg80211_indicate_disconnect(adapter);

		rtw_reset_securitypriv_cmd(adapter);

		/* set ips_deny_time to avoid enter IPS before LPS leave */
		rtw_set_ips_deny(adapter, 3000);

		_clr_fwstate_(mlme_priv, _FW_LINKED);

		rtw_clear_scan_deny(adapter);
	}

	rtw_lps_ctrl_wk_cmd(adapter, LPS_CTRL_DISCONNECT, 1);
}

inline void rtw_indicate_scan_done(struct adapter *adapter, bool aborted)
{
	rtw_cfg80211_indicate_scan_done(adapter, aborted);

	if ((!adapter_to_pwrctl(adapter)->bInSuspend) &&
	    (!check_fwstate(&adapter->mlmepriv,
			    WIFI_ASOC_STATE | WIFI_UNDER_LINKING))) {
		rtw_set_ips_deny(adapter, 0);
		_set_timer(&adapter->mlmepriv.dynamic_chk_timer, 1);
	}
}

void rtw_scan_abort(struct adapter *adapter)
{
	unsigned long start;
	struct mlme_priv *mlme_priv = &adapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;

	start = jiffies;
	pmlmeext->scan_abort = true;
	while (check_fwstate(mlme_priv, _FW_UNDER_SURVEY)
	       && jiffies_to_msecs(start) <= 200) {
		if (adapter->driver_stopped || adapter->bSurpriseRemoved)
			break;

		msleep(20);
	}

	if (check_fwstate(mlme_priv, _FW_UNDER_SURVEY))
		rtw_indicate_scan_done(adapter, true);

	pmlmeext->scan_abort = false;
}

static struct sta_info *rtw_joinbss_update_stainfo(struct adapter *adapter, struct wlan_network *pnetwork)
{
	int i;
	struct sta_info *bmc_sta, *psta = NULL;
	struct recv_reorder_ctrl *preorder_ctrl;
	struct sta_priv *pstapriv = &adapter->stapriv;
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;

	psta = rtw_get_stainfo(pstapriv, pnetwork->network.mac_address);
	if (!psta)
		psta = rtw_alloc_stainfo(pstapriv, pnetwork->network.mac_address);

	if (psta) { /* update ptarget_sta */

		psta->aid = pnetwork->join_res;

		update_sta_info(adapter, psta);

		/* update station supportRate */
		psta->bssratelen = rtw_get_rateset_len(pnetwork->network.supported_rates);
		memcpy(psta->bssrateset, pnetwork->network.supported_rates, psta->bssratelen);
		rtw_hal_update_sta_rate_mask(adapter, psta);

		psta->wireless_mode = pmlmeext->cur_wireless_mode;
		psta->raid = networktype_to_raid_ex(adapter, psta);

		/* sta mode */
		rtw_hal_set_odm_var(adapter, HAL_ODM_STA_INFO, psta, true);

		/* security related */
		if (adapter->securitypriv.dot11_auth_algrthm == dot11_auth_algrthm_8021x) {
			adapter->securitypriv.binstallGrpkey = false;
			adapter->securitypriv.busetkipkey = false;
			adapter->securitypriv.bgrpkey_handshake = false;

			psta->ieee8021x_blocked = true;
			psta->dot118021_x_privacy = adapter->securitypriv.dot11_privacy_algrthm;

			memset((u8 *)&psta->dot118021x_uncst_key, 0, sizeof(union Keytype));

			memset((u8 *)&psta->dot11tkiprxmickey, 0, sizeof(union Keytype));
			memset((u8 *)&psta->dot11tkiptxmickey, 0, sizeof(union Keytype));

			memset((u8 *)&psta->dot11txpn, 0, sizeof(union pn48));
			psta->dot11txpn.val = psta->dot11txpn.val + 1;
			memset((u8 *)&psta->dot11wtxpn, 0, sizeof(union pn48));
			memset((u8 *)&psta->dot11rxpn, 0, sizeof(union pn48));
		}

		/* When doing the WPS, the wps_ie_len won't equal to 0 */
		/* And the Wi-Fi driver shouldn't allow the data packet to be transmitted. */
		if (adapter->securitypriv.wps_ie_len != 0) {
			psta->ieee8021x_blocked = true;
			adapter->securitypriv.wps_ie_len = 0;
		}

		/* for A-MPDU Rx reordering buffer control for bmc_sta & sta_info */
		/* if A-MPDU Rx is enabled, resetting  rx_ordering_ctrl wstart_b(indicate_seq) to default value = 0xffff */
		/* todo: check if AP can send A-MPDU packets */
		for (i = 0; i < 16 ; i++) {
			preorder_ctrl = &psta->recvreorder_ctrl[i];
			preorder_ctrl->enable = false;
			preorder_ctrl->indicate_seq = 0xffff;
			preorder_ctrl->wend_b = 0xffff;
			preorder_ctrl->wsize_b = 64;/* max_ampdu_sz;ex. 32(kbytes) -> wsize_b =32 */
		}

		bmc_sta = rtw_get_bcmc_stainfo(adapter);
		if (bmc_sta) {
			for (i = 0; i < 16 ; i++) {
				preorder_ctrl = &bmc_sta->recvreorder_ctrl[i];
				preorder_ctrl->enable = false;
				preorder_ctrl->indicate_seq = 0xffff;
				preorder_ctrl->wend_b = 0xffff;
				preorder_ctrl->wsize_b = 64;/* max_ampdu_sz;ex. 32(kbytes) -> wsize_b =32 */
			}
		}
	}

	return psta;
}

/* pnetwork : returns from rtw_joinbss_event_callback */
/* ptarget_wlan: found from scanned_queue */
static void rtw_joinbss_update_network(struct adapter *adapter, struct wlan_network *ptarget_wlan,
				       struct wlan_network *pnetwork)
{
	struct mlme_priv *mlme_priv = &adapter->mlmepriv;
	struct wlan_network *cur_network = &mlme_priv->cur_network;

	/*  why not use ptarget_wlan?? */
	memcpy(&cur_network->network, &pnetwork->network, pnetwork->network.length);
	/*  some ies in pnetwork is wrong, so we should use ptarget_wlan ies */
	cur_network->network.ie_length = ptarget_wlan->network.ie_length;
	memcpy(&cur_network->network.ies[0], &ptarget_wlan->network.ies[0], MAX_IE_SZ);

	cur_network->aid = pnetwork->join_res;

	rtw_set_signal_stat_timer(&adapter->recvpriv);

	adapter->recvpriv.signal_strength = ptarget_wlan->network.phy_info.signal_strength;
	adapter->recvpriv.signal_qual = ptarget_wlan->network.phy_info.signal_quality;
	/* the ptarget_wlan->network.rssi is raw data, we use ptarget_wlan->network.phy_info.signal_strength instead (has scaled) */
	adapter->recvpriv.rssi = translate_percentage_to_dbm(ptarget_wlan->network.phy_info.signal_strength);

	rtw_set_signal_stat_timer(&adapter->recvpriv);

	/* update fw_state will clr _FW_UNDER_LINKING here indirectly */
	switch (pnetwork->network.infrastructure_mode) {
	case NL80211_IFTYPE_STATION:

			if (mlme_priv->fw_state & WIFI_UNDER_WPS)
				mlme_priv->fw_state = WIFI_STATION_STATE | WIFI_UNDER_WPS;
			else
				mlme_priv->fw_state = WIFI_STATION_STATE;

			break;
	case NL80211_IFTYPE_ADHOC:
			mlme_priv->fw_state = WIFI_ADHOC_STATE;
			break;
	default:
			mlme_priv->fw_state = WIFI_NULL_STATE;
			break;
	}

	if (cur_network->network.ie_length < sizeof(struct ndis_802_11_fix_ie))
		return;

	rtw_update_protection(adapter, (cur_network->network.ies) + sizeof(struct ndis_802_11_fix_ie),
									(cur_network->network.ie_length - sizeof(struct ndis_802_11_fix_ie)));

	rtw_update_ht_cap(adapter, cur_network->network.ies, cur_network->network.ie_length, (u8) cur_network->network.configuration.ds_config);
}

static struct rt_pmkid_list backup_pmkid_list[NUM_PMKID_CACHE];
void rtw_reset_securitypriv(struct adapter *adapter)
{
	u8 backup_pmkid_index = 0;
	u8 backup_tkip_countermeasure = 0x00;
	u32 backup_tkip_countermeasure_time = 0;
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;

	spin_lock_bh(&adapter->security_key_mutex);

	if (adapter->securitypriv.dot11_auth_algrthm == dot11_auth_algrthm_8021x) {
		/* 802.1x */
		/*  Added by Albert 2009/02/18 */
		/*  We have to backup the PMK information for WiFi PMK Caching test item. */
		/*  Backup the btkip_countermeasure information. */
		/*  When the countermeasure is trigger, the driver have to disconnect with AP for 60 seconds. */

		memcpy(&backup_pmkid_list[0], &adapter->securitypriv.PMKIDList[0], sizeof(struct rt_pmkid_list) * NUM_PMKID_CACHE);
		backup_pmkid_index = adapter->securitypriv.PMKIDIndex;
		backup_tkip_countermeasure = adapter->securitypriv.btkip_countermeasure;
		backup_tkip_countermeasure_time = adapter->securitypriv.btkip_countermeasure_time;

		/* reset RX BIP packet number */
		pmlmeext->mgnt_80211w_IPN_rx = 0;

		memset((unsigned char *)&adapter->securitypriv, 0, sizeof(struct security_priv));

		/*  Added by Albert 2009/02/18 */
		/*  Restore the PMK information to securitypriv structure for the following connection. */
		memcpy(&adapter->securitypriv.PMKIDList[0], &backup_pmkid_list[0], sizeof(struct rt_pmkid_list) * NUM_PMKID_CACHE);
		adapter->securitypriv.PMKIDIndex = backup_pmkid_index;
		adapter->securitypriv.btkip_countermeasure = backup_tkip_countermeasure;
		adapter->securitypriv.btkip_countermeasure_time = backup_tkip_countermeasure_time;

		adapter->securitypriv.ndisauthtype = Ndis802_11AuthModeOpen;
		adapter->securitypriv.ndisencryptstatus = NDIS_802_11_ENCRYPTION_DISABLED;

	} else {
		/* reset values in securitypriv */
		/* if (adapter->mlmepriv.fw_state & WIFI_STATION_STATE) */
		struct security_priv *psec_priv = &adapter->securitypriv;

		psec_priv->dot11_auth_algrthm = dot11_auth_algrthm_open;  /* open system */
		psec_priv->dot11_privacy_algrthm = _NO_PRIVACY_;
		psec_priv->dot11PrivacyKeyIndex = 0;

		psec_priv->dot118021XGrpPrivacy = _NO_PRIVACY_;
		psec_priv->dot118021XGrpKeyid = 1;

		psec_priv->ndisauthtype = Ndis802_11AuthModeOpen;
		psec_priv->ndisencryptstatus = NDIS_802_11_ENCRYPTION_DISABLED;
	}
	spin_unlock_bh(&adapter->security_key_mutex);
}

/* Notes: the function could be > passive_level (the same context as Rx tasklet) */
/* pnetwork : returns from rtw_joinbss_event_callback */
/* ptarget_wlan: found from scanned_queue */
/* if join_res > 0, for (fw_state ==WIFI_STATION_STATE), we check if  "ptarget_sta" & "ptarget_wlan" exist. */
/* if join_res > 0, for (fw_state ==WIFI_ADHOC_STATE), we only check if "ptarget_wlan" exist. */
/* if join_res > 0, update "cur_network->network" from "pnetwork->network" if (ptarget_wlan != NULL). */
void rtw_joinbss_event_prehandle(struct adapter *adapter, u8 *buf)
{
	struct sta_info *ptarget_sta = NULL, *pcur_sta = NULL;
	struct sta_priv *pstapriv = &adapter->stapriv;
	struct mlme_priv *mlme_priv = &adapter->mlmepriv;
	struct wlan_network *pnetwork = (struct wlan_network *)buf;
	struct wlan_network *cur_network = &mlme_priv->cur_network;
	struct wlan_network *pcur_wlan = NULL, *ptarget_wlan = NULL;
	unsigned int the_same_macaddr = false;

	the_same_macaddr = !memcmp(pnetwork->network.mac_address, cur_network->network.mac_address, ETH_ALEN);

	pnetwork->network.length = get_wlan_bssid_ex_sz(&pnetwork->network);
	if (pnetwork->network.length > sizeof(struct wlan_bssid_ex))
		return;

	spin_lock_bh(&mlme_priv->lock);

	mlme_priv->link_detect_info.traffic_transition_count = 0;
	mlme_priv->link_detect_info.low_power_transition_count = 0;

	if (pnetwork->join_res == -4) {
		rtw_reset_securitypriv(adapter);
		_set_timer(&mlme_priv->assoc_timer, 1);

		if (check_fwstate(mlme_priv, _FW_UNDER_LINKING))
			_clr_fwstate_(mlme_priv, _FW_UNDER_LINKING);

		spin_unlock_bh(&mlme_priv->lock);
		return;
	}

	if (pnetwork->join_res <= 0) { /* if join_res < 0 (join fails), then try again */
		_set_timer(&mlme_priv->assoc_timer, 1);
		_clr_fwstate_(mlme_priv, _FW_UNDER_LINKING);
		spin_unlock_bh(&mlme_priv->lock);
		return;
	}

	spin_lock_bh(&mlme_priv->scanned_queue.lock);

	if (!check_fwstate(mlme_priv, _FW_UNDER_LINKING)) {
		spin_unlock_bh(&mlme_priv->scanned_queue.lock);
		spin_unlock_bh(&mlme_priv->lock);
		return;
	}

	/* s1. find ptarget_wlan */
	if (check_fwstate(mlme_priv, _FW_LINKED)) {
		if (the_same_macaddr) {
			ptarget_wlan = rtw_find_network(&mlme_priv->scanned_queue, cur_network->network.mac_address);
		} else {
			pcur_wlan = rtw_find_network(&mlme_priv->scanned_queue, cur_network->network.mac_address);
			if (pcur_wlan)
				pcur_wlan->fixed = false;

			pcur_sta = rtw_get_stainfo(pstapriv, cur_network->network.mac_address);
			if (pcur_sta)
				rtw_free_stainfo(adapter, pcur_sta);

			ptarget_wlan = rtw_find_network(&mlme_priv->scanned_queue, pnetwork->network.mac_address);
			if (check_fwstate(mlme_priv, WIFI_STATION_STATE)) {
				if (ptarget_wlan)
					ptarget_wlan->fixed = true;
			}
		}
	} else {
		ptarget_wlan = _rtw_find_same_network(&mlme_priv->scanned_queue, pnetwork);
		if (check_fwstate(mlme_priv, WIFI_STATION_STATE)) {
			if (ptarget_wlan)
				ptarget_wlan->fixed = true;
		}
	}

	/* s2. update cur_network */
	if (!ptarget_wlan) {
		netdev_dbg(adapter->pnetdev,
			   "Can't find ptarget_wlan when joinbss_event callback\n");
		spin_unlock_bh(&mlme_priv->scanned_queue.lock);
		spin_unlock_bh(&mlme_priv->lock);
		return;
	}

	rtw_joinbss_update_network(adapter, ptarget_wlan, pnetwork);

	/* s3. find ptarget_sta & update ptarget_sta after update cur_network only for station mode */
	if (check_fwstate(mlme_priv, WIFI_STATION_STATE)) {
		ptarget_sta = rtw_joinbss_update_stainfo(adapter, pnetwork);
		if (!ptarget_sta) {
			spin_unlock_bh(&mlme_priv->scanned_queue.lock);
			spin_unlock_bh(&mlme_priv->lock);
			return;
		}
	}

	/* s4. indicate connect */
	if (check_fwstate(mlme_priv, WIFI_STATION_STATE)) {
		mlme_priv->cur_network_scanned = ptarget_wlan;
		rtw_indicate_connect(adapter);
	}

	spin_unlock_bh(&mlme_priv->scanned_queue.lock);

	spin_unlock_bh(&mlme_priv->lock);
	/* s5. Cancel assoc_timer */
	timer_delete_sync(&mlme_priv->assoc_timer);
}

void rtw_joinbss_event_callback(struct adapter *adapter, u8 *buf)
{
	struct wlan_network *pnetwork = (struct wlan_network *)buf;

	mlmeext_joinbss_event_callback(adapter, pnetwork->join_res);

	rtw_os_xmit_schedule(adapter);
}

/* FOR STA, AP , AD-HOC mode */
void rtw_sta_media_status_rpt(struct adapter *adapter, struct sta_info *psta, u32 mstatus)
{
	u16 media_status_rpt;

	if (!psta)
		return;

	media_status_rpt = (u16)((psta->mac_id << 8) | mstatus); /*   MACID|OPMODE:1 connect */
	rtw_hal_set_hwreg(adapter, HW_VAR_H2C_MEDIA_STATUS_RPT, (u8 *)&media_status_rpt);
}

void rtw_stassoc_event_callback(struct adapter *adapter, u8 *buf)
{
	struct sta_info *psta;
	struct mlme_priv *mlme_priv = &adapter->mlmepriv;
	struct stassoc_event *pstassoc = (struct stassoc_event *)buf;
	struct wlan_network *cur_network = &mlme_priv->cur_network;
	struct wlan_network *ptarget_wlan = NULL;

	if (!rtw_access_ctrl(adapter, pstassoc->macaddr))
		return;

	if (check_fwstate(mlme_priv, WIFI_AP_STATE)) {
		psta = rtw_get_stainfo(&adapter->stapriv, pstassoc->macaddr);
		if (psta) {
			u8 *passoc_req = NULL;
			u32 assoc_req_len = 0;

			rtw_sta_media_status_rpt(adapter, psta, 1);

			ap_sta_info_defer_update(adapter, psta);

			/* report to upper layer */
			spin_lock_bh(&psta->lock);
			if (psta->passoc_req && psta->assoc_req_len > 0) {
				passoc_req = kmemdup(psta->passoc_req, psta->assoc_req_len, GFP_ATOMIC);
				if (passoc_req) {
					assoc_req_len = psta->assoc_req_len;

					kfree(psta->passoc_req);
					psta->passoc_req = NULL;
					psta->assoc_req_len = 0;
				}
			}
			spin_unlock_bh(&psta->lock);

			if (passoc_req && assoc_req_len > 0) {
				rtw_cfg80211_indicate_sta_assoc(adapter, passoc_req, assoc_req_len);

				kfree(passoc_req);
			}
		}
		return;
	}

	/* for AD-HOC mode */
	psta = rtw_get_stainfo(&adapter->stapriv, pstassoc->macaddr);
	if (psta) {
		/* the sta have been in sta_info_queue => do nothing */

		return; /* between drv has received this event before and  fw have not yet to set key to CAM_ENTRY) */
	}

	psta = rtw_alloc_stainfo(&adapter->stapriv, pstassoc->macaddr);
	if (!psta)
		return;

	/* to do : init sta_info variable */
	psta->qos_option = 0;
	psta->mac_id = (uint)pstassoc->cam_id;

	/* for ad-hoc mode */
	rtw_hal_set_odm_var(adapter, HAL_ODM_STA_INFO, psta, true);

	rtw_sta_media_status_rpt(adapter, psta, 1);

	if (adapter->securitypriv.dot11_auth_algrthm == dot11_auth_algrthm_8021x)
		psta->dot118021_x_privacy = adapter->securitypriv.dot11_privacy_algrthm;

	psta->ieee8021x_blocked = false;

	spin_lock_bh(&mlme_priv->lock);

	if (check_fwstate(mlme_priv, WIFI_ADHOC_MASTER_STATE) ||
	    check_fwstate(mlme_priv, WIFI_ADHOC_STATE)) {
		if (adapter->stapriv.asoc_sta_count == 2) {
			spin_lock_bh(&mlme_priv->scanned_queue.lock);
			ptarget_wlan = rtw_find_network(&mlme_priv->scanned_queue, cur_network->network.mac_address);
			mlme_priv->cur_network_scanned = ptarget_wlan;
			if (ptarget_wlan)
				ptarget_wlan->fixed = true;
			spin_unlock_bh(&mlme_priv->scanned_queue.lock);
			/*  a sta + bc/mc_stainfo (not Ibss_stainfo) */
			rtw_indicate_connect(adapter);
		}
	}

	spin_unlock_bh(&mlme_priv->lock);

	mlmeext_sta_add_event_callback(adapter, psta);
}

void rtw_stadel_event_callback(struct adapter *adapter, u8 *buf)
{
	int mac_id = (-1);
	struct sta_info *psta;
	struct wlan_network *pwlan = NULL;
	struct wlan_bssid_ex    *dev_network = NULL;
	u8 *ibss = NULL;
	struct mlme_priv *mlme_priv = &adapter->mlmepriv;
	struct stadel_event *pstadel = (struct stadel_event *)buf;
	struct wlan_network *tgt_network = &mlme_priv->cur_network;
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	psta = rtw_get_stainfo(&adapter->stapriv, pstadel->macaddr);
	if (psta)
		mac_id = psta->mac_id;
	else
		mac_id = pstadel->mac_id;

	if (mac_id >= 0) {
		u16 media_status;

		media_status = (mac_id << 8) | 0; /*   MACID|OPMODE:0 means disconnect */
		/* for STA, AP, ADHOC mode, report disconnect status to FW */
		rtw_hal_set_hwreg(adapter, HW_VAR_H2C_MEDIA_STATUS_RPT, (u8 *)&media_status);
	}

	/* if (check_fwstate(mlme_priv, WIFI_AP_STATE)) */
	if ((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE)
		return;

	mlmeext_sta_del_event_callback(adapter);

	spin_lock_bh(&mlme_priv->lock);

	if (check_fwstate(mlme_priv, WIFI_STATION_STATE)) {
		u16 reason = *((unsigned short *)(pstadel->rsvd));
		bool roam = false;
		struct wlan_network *roam_target = NULL;

		if (adapter->registrypriv.wifi_spec == 1) {
			roam = false;
		} else if (reason == WLAN_REASON_EXPIRATION_CHK && rtw_chk_roam_flags(adapter, RTW_ROAM_ON_EXPIRED)) {
			roam = true;
		} else if (reason == WLAN_REASON_ACTIVE_ROAM && rtw_chk_roam_flags(adapter, RTW_ROAM_ACTIVE)) {
			roam = true;
			roam_target = mlme_priv->roam_network;
		}

		if (roam) {
			if (rtw_to_roam(adapter) > 0)
				rtw_dec_to_roam(adapter); /* this stadel_event is caused by roaming, decrease to_roam */
			else if (rtw_to_roam(adapter) == 0)
				rtw_set_to_roam(adapter, adapter->registrypriv.max_roaming_times);
		} else {
			rtw_set_to_roam(adapter, 0);
		}

		rtw_free_uc_swdec_pending_queue(adapter);

		rtw_free_assoc_resources(adapter, 1);
		rtw_indicate_disconnect(adapter);

		spin_lock_bh(&mlme_priv->scanned_queue.lock);
		/*  remove the network entry in scanned_queue */
		pwlan = rtw_find_network(&mlme_priv->scanned_queue, tgt_network->network.mac_address);
		if (pwlan) {
			pwlan->fixed = false;
			rtw_free_network_nolock(adapter, pwlan);
		}
		spin_unlock_bh(&mlme_priv->scanned_queue.lock);

		_rtw_roaming(adapter, roam_target);
	}

	if (check_fwstate(mlme_priv, WIFI_ADHOC_MASTER_STATE) ||
	    check_fwstate(mlme_priv, WIFI_ADHOC_STATE)) {
		rtw_free_stainfo(adapter, psta);

		if (adapter->stapriv.asoc_sta_count == 1) {/* a sta + bc/mc_stainfo (not Ibss_stainfo) */
			u8 ret = _SUCCESS;

			spin_lock_bh(&mlme_priv->scanned_queue.lock);
			/* free old ibss network */
			pwlan = rtw_find_network(&mlme_priv->scanned_queue, tgt_network->network.mac_address);
			if (pwlan) {
				pwlan->fixed = false;
				rtw_free_network_nolock(adapter, pwlan);
			}
			spin_unlock_bh(&mlme_priv->scanned_queue.lock);
			/* re-create ibss */
			dev_network = &adapter->registrypriv.dev_network;
			ibss = adapter->registrypriv.dev_network.mac_address;

			memcpy(dev_network, &tgt_network->network, get_wlan_bssid_ex_sz(&tgt_network->network));

			memcpy(&dev_network->ssid, &mlme_priv->assoc_ssid, sizeof(struct ndis_802_11_ssid));

			rtw_update_registrypriv_dev_network(adapter);

			rtw_generate_random_ibss(ibss);

			if (check_fwstate(mlme_priv, WIFI_ADHOC_STATE)) {
				set_fwstate(mlme_priv, WIFI_ADHOC_MASTER_STATE);
				_clr_fwstate_(mlme_priv, WIFI_ADHOC_STATE);
			}

			ret = rtw_createbss_cmd(adapter);
			if (ret != _SUCCESS)
				goto unlock;
		}
	}

unlock:
	spin_unlock_bh(&mlme_priv->lock);
}

void rtw_cpwm_event_callback(struct adapter *adapter, u8 *buf)
{
	struct reportpwrstate_parm *preportpwrstate;

	preportpwrstate = (struct reportpwrstate_parm *)buf;
	preportpwrstate->state |= (u8)(adapter_to_pwrctl(adapter)->cpwm_tog + 0x80);
	cpwm_int_hdl(adapter, preportpwrstate);
}

void rtw_wmm_event_callback(struct adapter *adapter, u8 *buf)
{
	WMMOnAssocRsp(adapter);
}

/*
 * _rtw_join_timeout_handler - Timeout/failure handler for CMD JoinBss
 * @adapter: pointer to struct adapter structure
 */
void _rtw_join_timeout_handler(struct timer_list *t)
{
	struct adapter *adapter = timer_container_of(adapter, t,
						     mlmepriv.assoc_timer);
	struct mlme_priv *mlme_priv = &adapter->mlmepriv;

	if (adapter->driver_stopped || adapter->bSurpriseRemoved)
		return;

	spin_lock_bh(&mlme_priv->lock);

	if (rtw_to_roam(adapter) > 0) { /* join timeout caused by roaming */
		while (1) {
			rtw_dec_to_roam(adapter);
			if (rtw_to_roam(adapter) != 0) { /* try another */
				int do_join_r;

				do_join_r = rtw_do_join(adapter);
				if (do_join_r != _SUCCESS)
					continue;

				break;
			}

			rtw_indicate_disconnect(adapter);
			break;
		}

	} else {
		rtw_indicate_disconnect(adapter);
		free_scanqueue(mlme_priv);

		/* indicate disconnect for the case that join_timeout and check_fwstate != FW_LINKED */
		rtw_cfg80211_indicate_disconnect(adapter);
	}

	spin_unlock_bh(&mlme_priv->lock);
}

/*
 * rtw_scan_timeout_handler - Timeout/Failure handler for CMD SiteSurvey
 * @adapter: pointer to struct adapter structure
 */
void rtw_scan_timeout_handler(struct timer_list *t)
{
	struct adapter *adapter = timer_container_of(adapter, t,
						     mlmepriv.scan_to_timer);
	struct mlme_priv *mlme_priv = &adapter->mlmepriv;

	spin_lock_bh(&mlme_priv->lock);

	_clr_fwstate_(mlme_priv, _FW_UNDER_SURVEY);

	spin_unlock_bh(&mlme_priv->lock);

	rtw_indicate_scan_done(adapter, true);
}

void rtw_mlme_reset_auto_scan_int(struct adapter *adapter)
{
	struct mlme_priv *mlme = &adapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	if (pmlmeinfo->VHT_enable) {
		/* disable auto scan when connect to 11AC AP */
		mlme->auto_scan_int_ms = 0;
	} else if (adapter->registrypriv.wifi_spec && is_client_associated_to_ap(adapter)) {
		mlme->auto_scan_int_ms = 60 * 1000;
	} else if (rtw_chk_roam_flags(adapter, RTW_ROAM_ACTIVE)) {
		if (check_fwstate(mlme, WIFI_STATION_STATE) && check_fwstate(mlme, _FW_LINKED))
			mlme->auto_scan_int_ms = mlme->roam_scan_int_ms;
	} else {
		mlme->auto_scan_int_ms = 0; /* disabled */
	}
}

static void rtw_auto_scan_handler(struct adapter *adapter)
{
	struct mlme_priv *mlme_priv = &adapter->mlmepriv;

	rtw_mlme_reset_auto_scan_int(adapter);

	if (mlme_priv->auto_scan_int_ms != 0 &&
	    jiffies_to_msecs(jiffies - mlme_priv->scan_start_time) > mlme_priv->auto_scan_int_ms) {
		if (!adapter->registrypriv.wifi_spec) {
			if (check_fwstate(mlme_priv, _FW_UNDER_SURVEY | _FW_UNDER_LINKING))
				goto exit;

			if (mlme_priv->link_detect_info.busy_traffic)
				goto exit;
		}

		rtw_set_802_11_bssid_list_scan(adapter, NULL, 0);
	}

exit:
	return;
}

void rtw_dynamic_check_timer_handler(struct adapter *adapter)
{
	if (!adapter)
		return;

	if (!adapter->hw_init_completed)
		return;

	if (adapter->driver_stopped || adapter->bSurpriseRemoved)
		return;

	if (adapter->net_closed)
		return;

	if ((adapter_to_pwrctl(adapter)->fw_current_in_ps_mode) &&
	    !(hal_btcoex_IsBtControlLps(adapter))
		) {
		bool should_enter_ps;

		linked_status_chk(adapter);

		should_enter_ps = traffic_status_watchdog(adapter, true);
		if (should_enter_ps) {
			/* rtw_lps_ctrl_wk_cmd(adapter, LPS_CTRL_ENTER, 1); */
			rtw_hal_dm_watchdog_in_lps(adapter);
		} else {
			/* call rtw_lps_ctrl_wk_cmd(adapter, LPS_CTRL_LEAVE, 1) in traffic_status_watchdog() */
		}

	} else {
		rtw_dynamic_chk_wk_cmd(adapter);
	}

	/* auto site survey */
	rtw_auto_scan_handler(adapter);
}

inline bool rtw_is_scan_deny(struct adapter *adapter)
{
	struct mlme_priv *mlmepriv = &adapter->mlmepriv;

	return (atomic_read(&mlmepriv->set_scan_deny) != 0) ? true : false;
}

inline void rtw_clear_scan_deny(struct adapter *adapter)
{
	struct mlme_priv *mlmepriv = &adapter->mlmepriv;

	atomic_set(&mlmepriv->set_scan_deny, 0);
}

void rtw_set_scan_deny(struct adapter *adapter, u32 ms)
{
	struct mlme_priv *mlmepriv = &adapter->mlmepriv;

	atomic_set(&mlmepriv->set_scan_deny, 1);
	_set_timer(&mlmepriv->set_scan_deny_timer, ms);
}

/*
 * Select a new roaming candidate from the original @param candidate and @param competitor
 * @return true: candidate is updated
 * @return false: candidate is not updated
 */
static int rtw_check_roaming_candidate(struct mlme_priv *mlme
	, struct wlan_network **candidate, struct wlan_network *competitor)
{
	int updated = false;
	struct adapter *adapter = container_of(mlme, struct adapter, mlmepriv);

	if (!is_same_ess(&competitor->network, &mlme->cur_network.network))
		goto exit;

	if (!rtw_is_desired_network(adapter, competitor))
		goto exit;

	/* got specific addr to roam */
	if (!is_zero_ether_addr(mlme->roam_tgt_addr)) {
		if (!memcmp(mlme->roam_tgt_addr, competitor->network.mac_address, ETH_ALEN))
			goto update;
		else
			goto exit;
	}
	if (jiffies_to_msecs(jiffies - competitor->last_scanned) >= mlme->roam_scanr_exp_ms)
		goto exit;

	if (competitor->network.rssi - mlme->cur_network_scanned->network.rssi < mlme->roam_rssi_diff_th)
		goto exit;

	if (*candidate && (*candidate)->network.rssi >= competitor->network.rssi)
		goto exit;

update:
	*candidate = competitor;
	updated = true;

exit:
	return updated;
}

int rtw_select_roaming_candidate(struct mlme_priv *mlme)
{
	int ret = _FAIL;
	struct list_head *phead;
	struct __queue *queue = &mlme->scanned_queue;
	struct wlan_network *pnetwork = NULL;
	struct wlan_network *candidate = NULL;

	if (!mlme->cur_network_scanned) {
		WARN_ON(1);
		return ret;
	}

	spin_lock_bh(&mlme->scanned_queue.lock);
	phead = get_list_head(queue);

	list_for_each(mlme->pscanned, phead) {
		pnetwork = list_entry(mlme->pscanned, struct wlan_network,
				      list);

		rtw_check_roaming_candidate(mlme, &candidate, pnetwork);
	}

	if (!candidate) {
		ret = _FAIL;
		goto exit;
	} else {
		mlme->roam_network = candidate;

		if (!memcmp(candidate->network.mac_address, mlme->roam_tgt_addr, ETH_ALEN))
			eth_zero_addr(mlme->roam_tgt_addr);
	}

	ret = _SUCCESS;
exit:
	spin_unlock_bh(&mlme->scanned_queue.lock);

	return ret;
}

/*
 * Select a new join candidate from the original @param candidate and @param competitor
 * @return true: candidate is updated
 * @return false: candidate is not updated
 */
static int rtw_check_join_candidate(struct mlme_priv *mlme
	, struct wlan_network **candidate, struct wlan_network *competitor)
{
	int updated = false;
	struct adapter *adapter = container_of(mlme, struct adapter, mlmepriv);

	/* check bssid, if needed */
	if (mlme->assoc_by_bssid) {
		if (memcmp(competitor->network.mac_address, mlme->assoc_bssid, ETH_ALEN))
			goto exit;
	}

	/* check ssid, if needed */
	if (mlme->assoc_ssid.ssid[0] && mlme->assoc_ssid.ssid_length) {
		if (competitor->network.ssid.ssid_length != mlme->assoc_ssid.ssid_length ||
		    memcmp(competitor->network.ssid.ssid, mlme->assoc_ssid.ssid, mlme->assoc_ssid.ssid_length)
		)
			goto exit;
	}

	if (!rtw_is_desired_network(adapter, competitor))
		goto exit;

	if (rtw_to_roam(adapter) > 0) {
		if (jiffies_to_msecs(jiffies - competitor->last_scanned) >=
		    mlme->roam_scanr_exp_ms ||
		    !is_same_ess(&competitor->network, &mlme->cur_network.network))
			goto exit;
	}

	if (!*candidate || (*candidate)->network.rssi < competitor->network.rssi) {
		*candidate = competitor;
		updated = true;
	}

exit:
	return updated;
}

/*
 * Calling context:
 * The caller of the sub-routine will be in critical section...
 * The caller must hold the following spinlock
 * mlme_priv->lock
 */

int rtw_select_and_join_from_scanned_queue(struct mlme_priv *mlme_priv)
{
	int ret;
	struct list_head *phead;
	struct adapter *adapter;
	struct __queue *queue = &mlme_priv->scanned_queue;
	struct wlan_network *pnetwork = NULL;
	struct wlan_network *candidate = NULL;

	adapter = (struct adapter *)mlme_priv->nic_hdl;

	spin_lock_bh(&mlme_priv->scanned_queue.lock);

	if (mlme_priv->roam_network) {
		candidate = mlme_priv->roam_network;
		mlme_priv->roam_network = NULL;
		goto candidate_exist;
	}

	phead = get_list_head(queue);
	list_for_each(mlme_priv->pscanned, phead) {
		pnetwork = list_entry(mlme_priv->pscanned,
				      struct wlan_network, list);

		rtw_check_join_candidate(mlme_priv, &candidate, pnetwork);
	}

	if (!candidate) {
		ret = _FAIL;
		goto exit;
	} else {
		goto candidate_exist;
	}

candidate_exist:

	/*  check for situation of  _FW_LINKED */
	if (check_fwstate(mlme_priv, _FW_LINKED)) {
		rtw_disassoc_cmd(adapter, 0, true);
		rtw_indicate_disconnect(adapter);
		rtw_free_assoc_resources(adapter, 0);
	}

	set_fwstate(mlme_priv, _FW_UNDER_LINKING);
	ret = rtw_joinbss_cmd(adapter, candidate);

exit:
	spin_unlock_bh(&mlme_priv->scanned_queue.lock);
	return ret;
}

signed int rtw_set_auth(struct adapter *adapter, struct security_priv *psecuritypriv)
{
	struct cmd_obj *pcmd;
	struct setauth_parm *psetauthparm;
	struct cmd_priv *pcmdpriv = &adapter->cmdpriv;
	signed int res = _SUCCESS;

	pcmd = kzalloc_obj(*pcmd);
	if (!pcmd) {
		res = _FAIL;  /* try again */
		goto exit;
	}

	psetauthparm = kzalloc_obj(*psetauthparm);
	if (!psetauthparm) {
		kfree(pcmd);
		res = _FAIL;
		goto exit;
	}

	psetauthparm->mode = (unsigned char)psecuritypriv->dot11_auth_algrthm;

	pcmd->cmdcode = SET_AUTH_CMD;
	pcmd->parmbuf = (unsigned char *)psetauthparm;
	pcmd->cmdsz = (sizeof(struct setauth_parm));
	pcmd->rsp = NULL;
	pcmd->rspsz = 0;

	INIT_LIST_HEAD(&pcmd->list);

	res = rtw_enqueue_cmd(pcmdpriv, pcmd);

exit:
	return res;
}

signed int rtw_set_key(struct adapter *adapter, struct security_priv *psecuritypriv, signed int keyid, u8 set_tx, bool enqueue)
{
	u8 keylen;
	struct cmd_obj *pcmd;
	struct setkey_parm *psetkeyparm;
	struct cmd_priv *pcmdpriv = &adapter->cmdpriv;
	signed int res = _SUCCESS;

	psetkeyparm = kzalloc_obj(*psetkeyparm);
	if (!psetkeyparm) {
		res = _FAIL;
		goto exit;
	}

	if (psecuritypriv->dot11_auth_algrthm == dot11_auth_algrthm_8021x)
		psetkeyparm->algorithm = (unsigned char)psecuritypriv->dot118021XGrpPrivacy;
	else
		psetkeyparm->algorithm = (u8)psecuritypriv->dot11_privacy_algrthm;

	psetkeyparm->keyid = (u8)keyid;/* 0~3 */
	psetkeyparm->set_tx = set_tx;
	if (is_wep_enc(psetkeyparm->algorithm))
		adapter->securitypriv.key_mask |= BIT(psetkeyparm->keyid);

	switch (psetkeyparm->algorithm) {
	case _WEP40_:
		keylen = 5;
		memcpy(&psetkeyparm->key[0], &psecuritypriv->dot11DefKey[keyid].skey[0], keylen);
		break;
	case _WEP104_:
		keylen = 13;
		memcpy(&psetkeyparm->key[0], &psecuritypriv->dot11DefKey[keyid].skey[0], keylen);
		break;
	case _TKIP_:
		keylen = 16;
		memcpy(&psetkeyparm->key, &psecuritypriv->dot118021XGrpKey[keyid], keylen);
		psetkeyparm->grpkey = 1;
		break;
	case _AES_:
		keylen = 16;
		memcpy(&psetkeyparm->key, &psecuritypriv->dot118021XGrpKey[keyid], keylen);
		psetkeyparm->grpkey = 1;
		break;
	default:
		res = _FAIL;
		kfree_sensitive(psetkeyparm);
		goto exit;
	}

	if (enqueue) {
		pcmd = kzalloc_obj(*pcmd);
		if (!pcmd) {
			kfree_sensitive(psetkeyparm);
			res = _FAIL;  /* try again */
			goto exit;
		}

		pcmd->cmdcode = SET_KEY_CMD;
		pcmd->parmbuf = (u8 *)psetkeyparm;
		pcmd->cmdsz = (sizeof(struct setkey_parm));
		pcmd->rsp = NULL;
		pcmd->rspsz = 0;

		INIT_LIST_HEAD(&pcmd->list);

		res = rtw_enqueue_cmd(pcmdpriv, pcmd);
	} else {
		setkey_hdl(adapter, (u8 *)psetkeyparm);
		kfree_sensitive(psetkeyparm);
	}
exit:
	return res;
}

/* adjust ies for rtw_joinbss_cmd in WMM */
int rtw_restruct_wmm_ie(struct adapter *adapter, u8 *in_ie, u8 *out_ie, uint in_len, uint initial_out_len)
{
	unsigned int ielength = 0;
	unsigned int i, j;

	i = 12; /* after the fixed IE */
	while (i < in_len) {
		ielength = initial_out_len;

		if (i + 5 < in_len &&
		    in_ie[i] == 0xDD && in_ie[i + 2] == 0x00 &&
		    in_ie[i + 3] == 0x50 && in_ie[i + 4] == 0xF2 &&
		    in_ie[i + 5] == 0x02) {
			for (j = i; j < i + 9; j++) {
				out_ie[ielength] = in_ie[j];
				ielength++;
			}
			out_ie[initial_out_len + 1] = 0x07;
			out_ie[initial_out_len + 6] = 0x00;
			out_ie[initial_out_len + 8] = 0x00;

			break;
		}

		if (i + 1 >= in_len)
			break;

		i += (in_ie[i + 1] + 2); /*  to the next IE element */
	}

	return ielength;
}

/* Ported from 8185: IsInPreAuthKeyList().
 * (Renamed from SecIsInPreAuthKeyList(), 2006-10-13.)
 * Added by Annie, 2006-05-07.
 *
 * Search by BSSID,
 *
 * Return Value:
 * -1: if there is no pre-auth key in the  table
 * >=0: if there is pre-auth key, and return the entry id
 */
static int SecIsInPMKIDList(struct adapter *Adapter, u8 *bssid)
{
	struct security_priv *p = &Adapter->securitypriv;
	int i;

	for (i = 0; i < NUM_PMKID_CACHE; i++)
		if ((p->PMKIDList[i].bUsed) &&
		    (!memcmp(p->PMKIDList[i].Bssid, bssid, ETH_ALEN)))
			return i;
	return -1;
}

/*  Check the RSN IE length */
/*  If the RSN IE length <= 20, the RSN IE didn't include the PMKID information */
/*  0-11th element in the array are the fixed IE */
/*  12th element in the array is the IE */
/*  13th element in the array is the IE length */

static int rtw_append_pmkid(struct adapter *Adapter, int pmkid_idx, u8 *ie, uint ie_len)
{
	struct security_priv *psecuritypriv = &Adapter->securitypriv;

	if (ie[13] <= 20) {
		/*  The RSN IE didn't include the PMK ID, append the PMK information */
		ie[ie_len] = 1;
		ie_len++;
		ie[ie_len] = 0;	/* PMKID count = 0x0100 */
		ie_len++;
		memcpy(&ie[ie_len], &psecuritypriv->PMKIDList[pmkid_idx].PMKID, 16);
		ie_len += 16;
		ie[13] += 18;/* PMKID length = 2+16 */
	}
	return ie_len;
}

static void rtw_report_sec_ie(struct adapter *adapter, u8 authmode, u8 *sec_ie)
{
	uint len;
	u8 *buff, *p;
	union iwreq_data wrqu;

	buff = NULL;
	if (authmode == WLAN_EID_VENDOR_SPECIFIC) {
		buff = kzalloc(IW_CUSTOM_MAX, GFP_ATOMIC);
		if (!buff)
			return;

		p = buff;

		p += scnprintf(p, IW_CUSTOM_MAX - (p - buff), "ASSOCINFO(ReqIEs =");

		len = sec_ie[1] + 2;
		len = (len < IW_CUSTOM_MAX) ? len : IW_CUSTOM_MAX;

		p += scnprintf(p, IW_CUSTOM_MAX - (p - buff), " %*ph", len, sec_ie);

		p += scnprintf(p, IW_CUSTOM_MAX - (p - buff), ")");

		memset(&wrqu, 0, sizeof(wrqu));

		wrqu.data.length = p - buff;

		wrqu.data.length = (wrqu.data.length < IW_CUSTOM_MAX) ? wrqu.data.length : IW_CUSTOM_MAX;

		kfree(buff);
	}
}

signed int rtw_restruct_sec_ie(struct adapter *adapter, u8 *in_ie, u8 *out_ie, uint in_len)
{
	u8 authmode = 0x0;
	uint ielength;
	int pmkid_idx;

	struct mlme_priv *mlme_priv = &adapter->mlmepriv;
	struct security_priv *psecuritypriv = &adapter->securitypriv;
	uint ndisauthmode = psecuritypriv->ndisauthtype;

	/* copy fixed ie only */
	memcpy(out_ie, in_ie, 12);
	ielength = 12;
	if ((ndisauthmode == Ndis802_11AuthModeWPA) || (ndisauthmode == Ndis802_11AuthModeWPAPSK))
		authmode = WLAN_EID_VENDOR_SPECIFIC;
	if ((ndisauthmode == Ndis802_11AuthModeWPA2) || (ndisauthmode == Ndis802_11AuthModeWPA2PSK))
		authmode = WLAN_EID_RSN;

	if (check_fwstate(mlme_priv, WIFI_UNDER_WPS)) {
		memcpy(out_ie + ielength, psecuritypriv->wps_ie, psecuritypriv->wps_ie_len);

		ielength += psecuritypriv->wps_ie_len;
	} else if ((authmode == WLAN_EID_VENDOR_SPECIFIC) || (authmode == WLAN_EID_RSN)) {
		/* copy RSN or SSN */
		memcpy(&out_ie[ielength], &psecuritypriv->supplicant_ie[0], psecuritypriv->supplicant_ie[1] + 2);
		ielength += psecuritypriv->supplicant_ie[1] + 2;
		rtw_report_sec_ie(adapter, authmode, psecuritypriv->supplicant_ie);
	}

	pmkid_idx = SecIsInPMKIDList(adapter, mlme_priv->assoc_bssid);
	if (pmkid_idx < 0)
		return ielength;

	if (authmode == WLAN_EID_RSN)
		ielength = rtw_append_pmkid(adapter, pmkid_idx, out_ie, ielength);

	return ielength;
}

void rtw_init_registrypriv_dev_network(struct adapter *adapter)
{
	struct registry_priv *registry_priv = &adapter->registrypriv;
	struct eeprom_priv *peepriv = &adapter->eeprompriv;
	struct wlan_bssid_ex *dev_network = &registry_priv->dev_network;
	u8 *myhwaddr = myid(peepriv);

	memcpy(dev_network->mac_address, myhwaddr, ETH_ALEN);

	memcpy(&dev_network->ssid, &registry_priv->ssid, sizeof(struct ndis_802_11_ssid));

	dev_network->configuration.length = sizeof(struct ndis_802_11_conf);
	dev_network->configuration.beacon_period = 100;
}

void rtw_update_registrypriv_dev_network(struct adapter *adapter)
{
	int sz = 0;
	struct registry_priv *registry_priv = &adapter->registrypriv;
	struct wlan_bssid_ex *dev_network = &registry_priv->dev_network;
	struct security_priv *psecuritypriv = &adapter->securitypriv;
	struct wlan_network *cur_network = &adapter->mlmepriv.cur_network;

	dev_network->privacy = (psecuritypriv->dot11_privacy_algrthm > 0 ? 1 : 0) ; /*  adhoc no 802.1x */

	dev_network->rssi = 0;

	dev_network->configuration.ds_config = (registry_priv->channel);

	if (cur_network->network.infrastructure_mode == NL80211_IFTYPE_ADHOC)
		dev_network->configuration.atim_window = (0);

	dev_network->infrastructure_mode = (cur_network->network.infrastructure_mode);

	/*  1. Supported rates */
	/*  2. IE */

	/* rtw_set_supported_rate(dev_network->supported_rates, registry_priv->wireless_mode) ;  will be called in rtw_generate_ie */
	sz = rtw_generate_ie(registry_priv);

	dev_network->ie_length = sz;

	dev_network->length = get_wlan_bssid_ex_sz((struct wlan_bssid_ex *)dev_network);

	/* notes: translate ie_length & length after assign the length to cmdsz in createbss_cmd(); */
	/* dev_network->ie_length = cpu_to_le32(sz); */
}

/* the function is at passive_level */
void rtw_joinbss_reset(struct adapter *adapter)
{
	u8 threshold;
	struct mlme_priv *mlme_priv = &adapter->mlmepriv;

	struct ht_priv *ht_priv = &mlme_priv->htpriv;

	/* todo: if you want to do something io/reg/hw setting before join_bss, please add code here */

	mlme_priv->num_FortyMHzIntolerant = 0;

	mlme_priv->num_sta_no_ht = 0;

	ht_priv->ampdu_enable = false;/* reset to disabled */

	/*  TH = 1 => means that invalidate usb rx aggregation */
	/*  TH = 0 => means that validate usb rx aggregation, use init value. */
	if (ht_priv->ht_option) {
		if (adapter->registrypriv.wifi_spec == 1)
			threshold = 1;
		else
			threshold = 0;
		rtw_hal_set_hwreg(adapter, HW_VAR_RXDMA_AGG_PG_TH, (u8 *)(&threshold));
	} else {
		threshold = 1;
		rtw_hal_set_hwreg(adapter, HW_VAR_RXDMA_AGG_PG_TH, (u8 *)(&threshold));
	}
}

void rtw_ht_use_default_setting(struct adapter *adapter)
{
	struct mlme_priv *mlme_priv = &adapter->mlmepriv;
	struct ht_priv *ht_priv = &mlme_priv->htpriv;
	struct registry_priv *registry_priv = &adapter->registrypriv;
	bool hw_ldpc_support = false, hw_stbc_support = false;
	bool hw_support_beamformer = false, hw_support_beamformee = false;

	if (registry_priv->wifi_spec)
		ht_priv->bss_coexist = 1;
	else
		ht_priv->bss_coexist = 0;

	ht_priv->sgi_40m = TEST_FLAG(registry_priv->short_gi, BIT(1)) ? true : false;
	ht_priv->sgi_20m = TEST_FLAG(registry_priv->short_gi, BIT(0)) ? true : false;

	/*  LDPC support */
	rtw_hal_get_def_var(adapter, HAL_DEF_RX_LDPC, (u8 *)&hw_ldpc_support);
	CLEAR_FLAGS(ht_priv->ldpc_cap);
	if (hw_ldpc_support) {
		if (TEST_FLAG(registry_priv->ldpc_cap, BIT(4)))
			SET_FLAG(ht_priv->ldpc_cap, LDPC_HT_ENABLE_RX);
	}
	rtw_hal_get_def_var(adapter, HAL_DEF_TX_LDPC, (u8 *)&hw_ldpc_support);
	if (hw_ldpc_support) {
		if (TEST_FLAG(registry_priv->ldpc_cap, BIT(5)))
			SET_FLAG(ht_priv->ldpc_cap, LDPC_HT_ENABLE_TX);
	}

	/*  STBC */
	rtw_hal_get_def_var(adapter, HAL_DEF_TX_STBC, (u8 *)&hw_stbc_support);
	CLEAR_FLAGS(ht_priv->stbc_cap);
	if (hw_stbc_support) {
		if (TEST_FLAG(registry_priv->stbc_cap, BIT(5)))
			SET_FLAG(ht_priv->stbc_cap, STBC_HT_ENABLE_TX);
	}
	rtw_hal_get_def_var(adapter, HAL_DEF_RX_STBC, (u8 *)&hw_stbc_support);
	if (hw_stbc_support) {
		if (TEST_FLAG(registry_priv->stbc_cap, BIT(4)))
			SET_FLAG(ht_priv->stbc_cap, STBC_HT_ENABLE_RX);
	}

	/*  Beamforming setting */
	rtw_hal_get_def_var(adapter, HAL_DEF_EXPLICIT_BEAMFORMER, (u8 *)&hw_support_beamformer);
	rtw_hal_get_def_var(adapter, HAL_DEF_EXPLICIT_BEAMFORMEE, (u8 *)&hw_support_beamformee);
	CLEAR_FLAGS(ht_priv->beamform_cap);
	if (TEST_FLAG(registry_priv->beamform_cap, BIT(4)) && hw_support_beamformer)
		SET_FLAG(ht_priv->beamform_cap, BEAMFORMING_HT_BEAMFORMER_ENABLE);

	if (TEST_FLAG(registry_priv->beamform_cap, BIT(5)) && hw_support_beamformee)
		SET_FLAG(ht_priv->beamform_cap, BEAMFORMING_HT_BEAMFORMEE_ENABLE);
}

void rtw_build_wmm_ie_ht(struct adapter *adapter, u8 *out_ie, uint *pout_len)
{
	unsigned char WMM_IE[] = {0x00, 0x50, 0xf2, 0x02, 0x00, 0x01, 0x00};
	int out_len;

	if (adapter->mlmepriv.qospriv.qos_option == 0) {
		out_len = *pout_len;
		rtw_set_ie(out_ie + out_len, WLAN_EID_VENDOR_SPECIFIC,
			   _WMM_IE_Length_, WMM_IE, pout_len);

		adapter->mlmepriv.qospriv.qos_option = 1;
	}
}

/* the function is >= passive_level */
unsigned int rtw_restructure_ht_ie(struct adapter *adapter, u8 *in_ie, u8 *out_ie, uint in_len, uint *pout_len, u8 channel)
{
	u32 ielen, out_len;
	enum ieee80211_max_ampdu_length_exp max_rx_ampdu_factor;
	unsigned char *p;
	struct ieee80211_ht_cap ht_capie;
	u8 cbw40_enable = 0, stbc_rx_enable = 0, operation_bw = 0;
	struct registry_priv *registry_priv = &adapter->registrypriv;
	struct mlme_priv *mlme_priv = &adapter->mlmepriv;
	struct ht_priv *ht_priv = &mlme_priv->htpriv;
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;

	ht_priv->ht_option = false;

	out_len = *pout_len;

	memset(&ht_capie, 0, sizeof(struct ieee80211_ht_cap));

	ht_capie.cap_info = cpu_to_le16(IEEE80211_HT_CAP_DSSSCCK40);

	if (ht_priv->sgi_20m)
		ht_capie.cap_info |= cpu_to_le16(IEEE80211_HT_CAP_SGI_20);

	/* Get HT BW */
	if (!in_ie) {
		/* TDLS: TODO 20/40 issue */
		if (check_fwstate(mlme_priv, WIFI_STATION_STATE)) {
			operation_bw = adapter->mlmeextpriv.cur_bwmode;
			if (operation_bw > CHANNEL_WIDTH_40)
				operation_bw = CHANNEL_WIDTH_40;
		} else {
			/* TDLS: TODO 40? */
			operation_bw = CHANNEL_WIDTH_40;
		}
	} else {
		p = rtw_get_ie(in_ie, WLAN_EID_HT_OPERATION, &ielen, in_len);
		if (p && (ielen == sizeof(struct ieee80211_ht_addt_info))) {
			struct HT_info_element *pht_info = (struct HT_info_element *)(p + 2);

			if (pht_info->infos[0] & BIT(2)) {
				switch (pht_info->infos[0] & 0x3) {
				case 1:
				case 3:
					operation_bw = CHANNEL_WIDTH_40;
					break;
				default:
					operation_bw = CHANNEL_WIDTH_20;
					break;
				}
			} else {
				operation_bw = CHANNEL_WIDTH_20;
			}
		}
	}

	/* to disable 40M Hz support while gd_bw_40MHz_en = 0 */
	if ((registry_priv->bw_mode & 0x0f) > 0)
		cbw40_enable = 1;

	if ((cbw40_enable == 1) && (operation_bw == CHANNEL_WIDTH_40)) {
		ht_capie.cap_info |= cpu_to_le16(IEEE80211_HT_CAP_SUP_WIDTH);
		if (ht_priv->sgi_40m)
			ht_capie.cap_info |= cpu_to_le16(IEEE80211_HT_CAP_SGI_40);
	}

	if (TEST_FLAG(ht_priv->stbc_cap, STBC_HT_ENABLE_TX))
		ht_capie.cap_info |= cpu_to_le16(IEEE80211_HT_CAP_TX_STBC);

	/* todo: disable SM power save mode */
	ht_capie.cap_info |= cpu_to_le16(IEEE80211_HT_CAP_SM_PS);

	if (TEST_FLAG(ht_priv->stbc_cap, STBC_HT_ENABLE_RX)) {
		if ((channel <= 14 && registry_priv->rx_stbc == 0x1) ||	/* enable for 2.4GHz */
			(registry_priv->wifi_spec == 1))
			stbc_rx_enable = 1;
	}

	/* fill default supported_mcs_set */
	memcpy(&ht_capie.mcs, pmlmeext->default_supported_mcs_set, 16);

	/* update default supported_mcs_set */
	if (stbc_rx_enable)
		ht_capie.cap_info |= cpu_to_le16(IEEE80211_HT_CAP_RX_STBC_1R);/* RX STBC One spatial stream */

	set_mcs_rate_by_mask(ht_capie.mcs.rx_mask, MCS_RATE_1R);

	{
		u32 rx_packet_offset, max_recvbuf_sz;

		rtw_hal_get_def_var(adapter, HAL_DEF_RX_PACKET_OFFSET, &rx_packet_offset);
		rtw_hal_get_def_var(adapter, HAL_DEF_MAX_RECVBUF_SZ, &max_recvbuf_sz);
	}

	if (adapter->driver_rx_ampdu_factor != 0xFF)
		max_rx_ampdu_factor =
		  (enum ieee80211_max_ampdu_length_exp)adapter->driver_rx_ampdu_factor;
	else
		rtw_hal_get_def_var(adapter, HW_VAR_MAX_RX_AMPDU_FACTOR,
				    &max_rx_ampdu_factor);

	ht_capie.ampdu_params_info = (max_rx_ampdu_factor & 0x03);

	if (adapter->securitypriv.dot11_privacy_algrthm == _AES_)
		ht_capie.ampdu_params_info |= (IEEE80211_HT_CAP_AMPDU_DENSITY & (0x07 << 2));
	else
		ht_capie.ampdu_params_info |= (IEEE80211_HT_CAP_AMPDU_DENSITY & 0x00);

	rtw_set_ie(out_ie + out_len, WLAN_EID_HT_CAPABILITY,
		   sizeof(struct ieee80211_ht_cap), (unsigned char *)&ht_capie, pout_len);

	ht_priv->ht_option = true;

	if (in_ie) {
		p = rtw_get_ie(in_ie, WLAN_EID_HT_OPERATION, &ielen, in_len);
		if (p && (ielen == sizeof(struct ieee80211_ht_addt_info))) {
			out_len = *pout_len;
			rtw_set_ie(out_ie + out_len, WLAN_EID_HT_OPERATION, ielen, p + 2, pout_len);
		}
	}

	return ht_priv->ht_option;
}

/* the function is > passive_level (in critical_section) */
void rtw_update_ht_cap(struct adapter *adapter, u8 *pie, uint ie_len, u8 channel)
{
	u8 *p, max_ampdu_sz;
	int len;
	struct ieee80211_ht_cap *pht_capie;
	struct mlme_priv *mlme_priv = &adapter->mlmepriv;
	struct ht_priv *ht_priv = &mlme_priv->htpriv;
	struct registry_priv *registry_priv = &adapter->registrypriv;
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	u8 cbw40_enable = 0;

	if (!ht_priv->ht_option)
		return;

	if ((!pmlmeinfo->HT_info_enable) || (!pmlmeinfo->HT_caps_enable))
		return;

	/* maybe needs check if ap supports rx ampdu. */
	if (!(ht_priv->ampdu_enable) && registry_priv->ampdu_enable == 1)
		ht_priv->ampdu_enable = true;

	/* check Max Rx A-MPDU Size */
	len = 0;
	p = rtw_get_ie(pie + sizeof(struct ndis_802_11_fix_ie), WLAN_EID_HT_CAPABILITY, &len, ie_len - sizeof(struct ndis_802_11_fix_ie));
	if (p && len > 0) {
		pht_capie = (struct ieee80211_ht_cap *)(p + 2);
		max_ampdu_sz = (pht_capie->ampdu_params_info & IEEE80211_HT_CAP_AMPDU_FACTOR);
		max_ampdu_sz = 1 << (max_ampdu_sz + 3); /*  max_ampdu_sz (kbytes); */

		ht_priv->rx_ampdu_maxlen = max_ampdu_sz;
	}

	len = 0;
	p = rtw_get_ie(pie + sizeof(struct ndis_802_11_fix_ie), WLAN_EID_HT_OPERATION, &len, ie_len - sizeof(struct ndis_802_11_fix_ie));
	if (p && len > 0) {
		/* todo: */
	}

	if ((registry_priv->bw_mode & 0x0f) > 0)
		cbw40_enable = 1;

	/* update cur_bwmode & cur_ch_offset */
	if ((cbw40_enable) &&
	    (le16_to_cpu(pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info) &
	      BIT(1)) && (pmlmeinfo->HT_info.infos[0] & BIT(2))) {
		int i;

		/* update the MCS set */
		for (i = 0; i < 16; i++)
			pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate[i] &= pmlmeext->default_supported_mcs_set[i];

		/* update the MCS rates */
		set_mcs_rate_by_mask(pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate, MCS_RATE_1R);

		/* switch to the 40M Hz mode according to the AP */
		/* pmlmeext->cur_bwmode = CHANNEL_WIDTH_40; */
		switch ((pmlmeinfo->HT_info.infos[0] & 0x3)) {
		case EXTCHNL_OFFSET_UPPER:
			pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_LOWER;
			break;

		case EXTCHNL_OFFSET_LOWER:
			pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_UPPER;
			break;

		default:
			pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
			break;
		}
	}

	/*  Config SM Power Save setting */
	pmlmeinfo->SM_PS =
		(le16_to_cpu(pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info) &
		 0x0C) >> 2;

	/*  Config current HT Protection mode. */
	pmlmeinfo->HT_protection = pmlmeinfo->HT_info.infos[1] & 0x3;
}

void rtw_issue_addbareq_cmd(struct adapter *adapter, struct xmit_frame *pxmitframe)
{
	u8 issued;
	int priority;
	struct sta_info *psta;
	struct ht_priv *ht_priv;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	s32 bmcst = is_multicast_ether_addr(pattrib->ra);

	if (bmcst || (adapter->mlmepriv.link_detect_info.num_tx_ok_in_period < 100))
		return;

	priority = pattrib->priority;

	psta = rtw_get_stainfo(&adapter->stapriv, pattrib->ra);
	if (pattrib->psta != psta)
		return;

	if (!psta)
		return;

	if (!(psta->state & _FW_LINKED))
		return;

	ht_priv = &psta->htpriv;

	if (ht_priv->ht_option && ht_priv->ampdu_enable) {
		issued = (ht_priv->agg_enable_bitmap >> priority) & 0x1;
		issued |= (ht_priv->candidate_tid_bitmap >> priority) & 0x1;

		if (issued == 0) {
			psta->htpriv.candidate_tid_bitmap |= BIT((u8)priority);
			rtw_addbareq_cmd(adapter, (u8) priority, pattrib->ra);
		}
	}
}

void rtw_append_exented_cap(struct adapter *adapter, u8 *out_ie, uint *pout_len)
{
	struct mlme_priv *mlme_priv = &adapter->mlmepriv;
	struct ht_priv *ht_priv = &mlme_priv->htpriv;
	u8 cap_content[8] = {0};

	if (ht_priv->bss_coexist)
		SET_EXT_CAPABILITY_ELE_BSS_COEXIST(cap_content, 1);

	rtw_set_ie(out_ie + *pout_len, WLAN_EID_EXT_CAPABILITY, 8, cap_content, pout_len);
}

inline void rtw_set_to_roam(struct adapter *adapter, u8 to_roam)
{
	if (to_roam == 0)
		adapter->mlmepriv.to_join = false;
	adapter->mlmepriv.to_roam = to_roam;
}

inline u8 rtw_dec_to_roam(struct adapter *adapter)
{
	adapter->mlmepriv.to_roam--;
	return adapter->mlmepriv.to_roam;
}

inline u8 rtw_to_roam(struct adapter *adapter)
{
	return adapter->mlmepriv.to_roam;
}

void rtw_roaming(struct adapter *adapter, struct wlan_network *tgt_network)
{
	struct mlme_priv *mlme_priv = &adapter->mlmepriv;

	spin_lock_bh(&mlme_priv->lock);
	_rtw_roaming(adapter, tgt_network);
	spin_unlock_bh(&mlme_priv->lock);
}

void _rtw_roaming(struct adapter *adapter, struct wlan_network *tgt_network)
{
	struct mlme_priv *mlme_priv = &adapter->mlmepriv;
	struct wlan_network *cur_network = &mlme_priv->cur_network;

	if (rtw_to_roam(adapter) > 0) {
		memcpy(&mlme_priv->assoc_ssid, &cur_network->network.ssid, sizeof(struct ndis_802_11_ssid));

		mlme_priv->assoc_by_bssid = false;

		while (rtw_do_join(adapter) != _SUCCESS) {
			rtw_dec_to_roam(adapter);
			if (rtw_to_roam(adapter) <= 0) {
				rtw_indicate_disconnect(adapter);
				break;
			}
		}
	}
}

bool rtw_linked_check(struct adapter *adapter)
{
	if (check_fwstate(&adapter->mlmepriv, WIFI_AP_STATE) ||
	    check_fwstate(&adapter->mlmepriv, WIFI_ADHOC_STATE | WIFI_ADHOC_MASTER_STATE)) {
		if (adapter->stapriv.asoc_sta_count > 2)
			return true;
	} else {	/* Station mode */
		if (check_fwstate(&adapter->mlmepriv, _FW_LINKED))
			return true;
	}
	return false;
}
