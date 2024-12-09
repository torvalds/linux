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

int	rtw_init_mlme_priv(struct adapter *padapter)
{
	int	i;
	u8 *pbuf;
	struct wlan_network	*pnetwork;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	int	res = _SUCCESS;

	pmlmepriv->nic_hdl = (u8 *)padapter;

	pmlmepriv->pscanned = NULL;
	pmlmepriv->fw_state = WIFI_STATION_STATE; /*  Must sync with rtw_wdev_alloc() */
	pmlmepriv->cur_network.network.infrastructure_mode = Ndis802_11AutoUnknown;
	pmlmepriv->scan_mode = SCAN_ACTIVE;/*  1: active, 0: passive. Maybe someday we should rename this varable to "active_mode" (Jeff) */

	spin_lock_init(&pmlmepriv->lock);
	INIT_LIST_HEAD(&pmlmepriv->free_bss_pool.queue);
	spin_lock_init(&pmlmepriv->free_bss_pool.lock);
	INIT_LIST_HEAD(&pmlmepriv->scanned_queue.queue);
	spin_lock_init(&pmlmepriv->scanned_queue.lock);

	memset(&pmlmepriv->assoc_ssid, 0, sizeof(struct ndis_802_11_ssid));

	pbuf = vzalloc(array_size(MAX_BSS_CNT, sizeof(struct wlan_network)));

	if (!pbuf) {
		res = _FAIL;
		goto exit;
	}
	pmlmepriv->free_bss_buf = pbuf;

	pnetwork = (struct wlan_network *)pbuf;

	for (i = 0; i < MAX_BSS_CNT; i++) {
		INIT_LIST_HEAD(&pnetwork->list);

		list_add_tail(&pnetwork->list, &pmlmepriv->free_bss_pool.queue);

		pnetwork++;
	}

	/* allocate DMA-able/Non-Page memory for cmd_buf and rsp_buf */

	rtw_clear_scan_deny(padapter);

	#define RTW_ROAM_SCAN_RESULT_EXP_MS 5000
	#define RTW_ROAM_RSSI_DIFF_TH 10
	#define RTW_ROAM_SCAN_INTERVAL_MS 10000

	pmlmepriv->roam_flags = 0
		| RTW_ROAM_ON_EXPIRED
		| RTW_ROAM_ON_RESUME
		;

	pmlmepriv->roam_scanr_exp_ms = RTW_ROAM_SCAN_RESULT_EXP_MS;
	pmlmepriv->roam_rssi_diff_th = RTW_ROAM_RSSI_DIFF_TH;
	pmlmepriv->roam_scan_int_ms = RTW_ROAM_SCAN_INTERVAL_MS;

	rtw_init_mlme_timer(padapter);

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

void rtw_free_mlme_priv_ie_data(struct mlme_priv *pmlmepriv)
{
	rtw_buf_free(&pmlmepriv->assoc_req, &pmlmepriv->assoc_req_len);
	rtw_buf_free(&pmlmepriv->assoc_rsp, &pmlmepriv->assoc_rsp_len);
	rtw_free_mlme_ie_data(&pmlmepriv->wps_beacon_ie, &pmlmepriv->wps_beacon_ie_len);
	rtw_free_mlme_ie_data(&pmlmepriv->wps_probe_req_ie, &pmlmepriv->wps_probe_req_ie_len);
	rtw_free_mlme_ie_data(&pmlmepriv->wps_probe_resp_ie, &pmlmepriv->wps_probe_resp_ie_len);
	rtw_free_mlme_ie_data(&pmlmepriv->wps_assoc_resp_ie, &pmlmepriv->wps_assoc_resp_ie_len);

	rtw_free_mlme_ie_data(&pmlmepriv->p2p_beacon_ie, &pmlmepriv->p2p_beacon_ie_len);
	rtw_free_mlme_ie_data(&pmlmepriv->p2p_probe_req_ie, &pmlmepriv->p2p_probe_req_ie_len);
	rtw_free_mlme_ie_data(&pmlmepriv->p2p_probe_resp_ie, &pmlmepriv->p2p_probe_resp_ie_len);
	rtw_free_mlme_ie_data(&pmlmepriv->p2p_go_probe_resp_ie, &pmlmepriv->p2p_go_probe_resp_ie_len);
	rtw_free_mlme_ie_data(&pmlmepriv->p2p_assoc_req_ie, &pmlmepriv->p2p_assoc_req_ie_len);
}

void _rtw_free_mlme_priv(struct mlme_priv *pmlmepriv)
{
	if (pmlmepriv) {
		rtw_free_mlme_priv_ie_data(pmlmepriv);
		vfree(pmlmepriv->free_bss_buf);
	}
}

struct	wlan_network *rtw_alloc_network(struct	mlme_priv *pmlmepriv)
{
	struct	wlan_network	*pnetwork;
	struct __queue *free_queue = &pmlmepriv->free_bss_pool;
	struct list_head *plist = NULL;

	spin_lock_bh(&free_queue->lock);

	if (list_empty(&free_queue->queue)) {
		pnetwork = NULL;
		goto exit;
	}
	plist = get_next(&(free_queue->queue));

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

void _rtw_free_network(struct	mlme_priv *pmlmepriv, struct wlan_network *pnetwork, u8 isfreeall)
{
	unsigned int delta_time;
	u32 lifetime = SCANQUEUE_LIFETIME;
	struct __queue *free_queue = &(pmlmepriv->free_bss_pool);

	if (!pnetwork)
		return;

	if (pnetwork->fixed)
		return;

	if ((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == true) ||
		(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == true))
		lifetime = 1;

	if (!isfreeall) {
		delta_time = jiffies_to_msecs(jiffies - pnetwork->last_scanned);
		if (delta_time < lifetime)/*  unit:msec */
			return;
	}

	spin_lock_bh(&free_queue->lock);

	list_del_init(&(pnetwork->list));

	list_add_tail(&(pnetwork->list), &(free_queue->queue));

	spin_unlock_bh(&free_queue->lock);
}

void _rtw_free_network_nolock(struct	mlme_priv *pmlmepriv, struct wlan_network *pnetwork)
{

	struct __queue *free_queue = &(pmlmepriv->free_bss_pool);

	if (!pnetwork)
		return;

	if (pnetwork->fixed)
		return;

	list_del_init(&(pnetwork->list));

	list_add_tail(&(pnetwork->list), get_list_head(free_queue));
}

/*
	return the wlan_network with the matching addr

	Shall be called under atomic context... to avoid possible racing condition...
*/
struct wlan_network *_rtw_find_network(struct __queue *scanned_queue, u8 *addr)
{
	struct list_head	*phead, *plist;
	struct	wlan_network *pnetwork = NULL;

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

void rtw_free_network_queue(struct adapter *padapter, u8 isfreeall)
{
	struct list_head *phead, *plist, *tmp;
	struct wlan_network *pnetwork;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct __queue *scanned_queue = &pmlmepriv->scanned_queue;

	spin_lock_bh(&scanned_queue->lock);

	phead = get_list_head(scanned_queue);
	list_for_each_safe(plist, tmp, phead) {

		pnetwork = list_entry(plist, struct wlan_network, list);

		_rtw_free_network(pmlmepriv, pnetwork, isfreeall);

	}

	spin_unlock_bh(&scanned_queue->lock);
}

signed int rtw_if_up(struct adapter *padapter)
{
	signed int res;

	if (padapter->bDriverStopped || padapter->bSurpriseRemoved ||
		(check_fwstate(&padapter->mlmepriv, _FW_LINKED) == false))
		res = false;
	else
		res =  true;

	return res;
}

void rtw_generate_random_ibss(u8 *pibss)
{
	unsigned long curtime = jiffies;

	pibss[0] = 0x02;  /* in ad-hoc mode bit1 must set to 1 */
	pibss[1] = 0x11;
	pibss[2] = 0x87;
	pibss[3] = (u8)(curtime & 0xff) ;/* p[0]; */
	pibss[4] = (u8)((curtime>>8) & 0xff) ;/* p[1]; */
	pibss[5] = (u8)((curtime>>16) & 0xff) ;/* p[2]; */
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

void rtw_free_mlme_priv(struct mlme_priv *pmlmepriv)
{
	_rtw_free_mlme_priv(pmlmepriv);
}

void rtw_free_network_nolock(struct adapter *padapter, struct wlan_network *pnetwork);
void rtw_free_network_nolock(struct adapter *padapter, struct wlan_network *pnetwork)
{
	_rtw_free_network_nolock(&(padapter->mlmepriv), pnetwork);
	rtw_cfg80211_unlink_bss(padapter, pnetwork);
}

/*
	return the wlan_network with the matching addr

	Shall be called under atomic context... to avoid possible racing condition...
*/
struct	wlan_network *rtw_find_network(struct __queue *scanned_queue, u8 *addr)
{
	struct	wlan_network *pnetwork = _rtw_find_network(scanned_queue, addr);

	return pnetwork;
}

int rtw_is_same_ibss(struct adapter *adapter, struct wlan_network *pnetwork)
{
	int ret = true;
	struct security_priv *psecuritypriv = &adapter->securitypriv;

	if ((psecuritypriv->dot11PrivacyAlgrthm != _NO_PRIVACY_) &&
		    (pnetwork->network.privacy == 0))
		ret = false;
	else if ((psecuritypriv->dot11PrivacyAlgrthm == _NO_PRIVACY_) &&
		 (pnetwork->network.privacy == 1))
		ret = false;
	else
		ret = true;

	return ret;

}

inline int is_same_ess(struct wlan_bssid_ex *a, struct wlan_bssid_ex *b)
{
	return (a->ssid.ssid_length == b->ssid.ssid_length)
		&&  !memcmp(a->ssid.ssid, b->ssid.ssid, a->ssid.ssid_length);
}

int is_same_network(struct wlan_bssid_ex *src, struct wlan_bssid_ex *dst, u8 feature)
{
	u16 s_cap, d_cap;
	__le16 tmps, tmpd;

	if (rtw_bug_check(dst, src, &s_cap, &d_cap) == false)
		return false;

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

struct	wlan_network	*rtw_get_oldest_wlan_network(struct __queue *scanned_queue)
{
	struct list_head	*plist, *phead;

	struct	wlan_network	*pwlan = NULL;
	struct	wlan_network	*oldest = NULL;

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
	struct adapter *padapter, bool update_ie)
{
	long rssi_ori = dst->rssi;

	u8 sq_smp = src->phy_info.signal_quality;

	u8 ss_final;
	u8 sq_final;
	long rssi_final;

	/* The rule below is 1/5 for sample value, 4/5 for history value */
	if (check_fwstate(&padapter->mlmepriv, _FW_LINKED) && is_same_network(&(padapter->mlmepriv.cur_network.network), src, 0)) {
		/* Take the recvpriv's value for the connected AP*/
		ss_final = padapter->recvpriv.signal_strength;
		sq_final = padapter->recvpriv.signal_qual;
		/* the rssi value here is undecorated, and will be used for antenna diversity */
		if (sq_smp != 101) /* from the right channel */
			rssi_final = (src->rssi+dst->rssi*4)/5;
		else
			rssi_final = rssi_ori;
	} else {
		if (sq_smp != 101) { /* from the right channel */
			ss_final = ((u32)(src->phy_info.signal_strength)+(u32)(dst->phy_info.signal_strength)*4)/5;
			sq_final = ((u32)(src->phy_info.signal_quality)+(u32)(dst->phy_info.signal_quality)*4)/5;
			rssi_final = (src->rssi+dst->rssi*4)/5;
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
	struct	mlme_priv *pmlmepriv = &(adapter->mlmepriv);

	rtw_bug_check(&(pmlmepriv->cur_network.network),
		&(pmlmepriv->cur_network.network),
		&(pmlmepriv->cur_network.network),
		&(pmlmepriv->cur_network.network));

	if ((check_fwstate(pmlmepriv, _FW_LINKED) == true) && (is_same_network(&(pmlmepriv->cur_network.network), pnetwork, 0))) {
		update_network(&(pmlmepriv->cur_network.network), pnetwork, adapter, true);
		rtw_update_protection(adapter, (pmlmepriv->cur_network.network.ies) + sizeof(struct ndis_802_11_fix_ie),
								pmlmepriv->cur_network.network.ie_length);
	}
}

/*
Caller must hold pmlmepriv->lock first.
*/
void rtw_update_scanned_network(struct adapter *adapter, struct wlan_bssid_ex *target)
{
	struct list_head	*plist, *phead;
	u32 bssid_ex_sz;
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);
	struct __queue	*queue	= &(pmlmepriv->scanned_queue);
	struct wlan_network	*pnetwork = NULL;
	struct wlan_network	*oldest = NULL;
	int target_find = 0;
	u8 feature = 0;

	spin_lock_bh(&queue->lock);
	phead = get_list_head(queue);
	list_for_each(plist, phead) {
		pnetwork = list_entry(plist, struct wlan_network, list);

		rtw_bug_check(pnetwork, pnetwork, pnetwork, pnetwork);

		if (is_same_network(&(pnetwork->network), target, feature)) {
			target_find = 1;
			break;
		}

		if (rtw_roam_flags(adapter)) {
			/* TODO: don't select network in the same ess as oldest if it's new enough*/
		}

		if (!oldest || time_after(oldest->last_scanned, pnetwork->last_scanned))
			oldest = pnetwork;

	}

	/* If we didn't find a match, then get a new network slot to initialize
	 * with this beacon's information */
	if (!target_find) {
		if (list_empty(&pmlmepriv->free_bss_pool.queue)) {
			/* If there are no more slots, expire the oldest */
			/* list_del_init(&oldest->list); */
			pnetwork = oldest;
			if (!pnetwork)
				goto exit;

			memcpy(&(pnetwork->network), target,  get_wlan_bssid_ex_sz(target));
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

			pnetwork = rtw_alloc_network(pmlmepriv); /*  will update scan_time */

			if (!pnetwork)
				goto exit;

			bssid_ex_sz = get_wlan_bssid_ex_sz(target);
			target->length = bssid_ex_sz;
			memcpy(&(pnetwork->network), target, bssid_ex_sz);

			pnetwork->last_scanned = jiffies;

			/* bss info not receiving from the right channel */
			if (pnetwork->network.phy_info.signal_quality == 101)
				pnetwork->network.phy_info.signal_quality = 0;

			list_add_tail(&(pnetwork->list), &(queue->queue));

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

		update_network(&(pnetwork->network), target, adapter, update_ie);
	}

exit:
	spin_unlock_bh(&queue->lock);
}

void rtw_add_network(struct adapter *adapter, struct wlan_bssid_ex *pnetwork);
void rtw_add_network(struct adapter *adapter, struct wlan_bssid_ex *pnetwork)
{
	update_current_network(adapter, pnetwork);
	rtw_update_scanned_network(adapter, pnetwork);
}

/* select the desired network based on the capability of the (i)bss. */
/*  check items: (1) security */
/* 			   (2) network_type */
/* 			   (3) WMM */
/* 			   (4) HT */
/*                      (5) others */
int rtw_is_desired_network(struct adapter *adapter, struct wlan_network *pnetwork);
int rtw_is_desired_network(struct adapter *adapter, struct wlan_network *pnetwork)
{
	struct security_priv *psecuritypriv = &adapter->securitypriv;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	u32 desired_encmode;
	u32 privacy;
	uint wps_ielen;
	int bselected = true;

	desired_encmode = psecuritypriv->ndisencryptstatus;
	privacy = pnetwork->network.privacy;

	if (check_fwstate(pmlmepriv, WIFI_UNDER_WPS)) {
		if (rtw_get_wps_ie(pnetwork->network.ies+_FIXED_IE_LENGTH_, pnetwork->network.ie_length-_FIXED_IE_LENGTH_, NULL, &wps_ielen))
			return true;
		else
			return false;

	}
	if (adapter->registrypriv.wifi_spec == 1) { /* for  correct flow of 8021X  to do.... */
		u8 *p = NULL;
		uint ie_len = 0;

		if ((desired_encmode == Ndis802_11EncryptionDisabled) && (privacy != 0))
			bselected = false;

		if (psecuritypriv->ndisauthtype == Ndis802_11AuthModeWPA2PSK) {
			p = rtw_get_ie(pnetwork->network.ies + _BEACON_IE_OFFSET_, WLAN_EID_RSN, &ie_len, (pnetwork->network.ie_length - _BEACON_IE_OFFSET_));
			if (p && ie_len > 0)
				bselected = true;
			else
				bselected = false;
		}
	}

	if ((desired_encmode != Ndis802_11EncryptionDisabled) && (privacy == 0))
		bselected = false;

	if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == true) {
		if (pnetwork->network.infrastructure_mode != pmlmepriv->cur_network.network.infrastructure_mode)
			bselected = false;
	}

	return bselected;
}

/* TODO: Perry : For Power Management */
void rtw_atimdone_event_callback(struct adapter	*adapter, u8 *pbuf)
{
}

void rtw_survey_event_callback(struct adapter	*adapter, u8 *pbuf)
{
	u32 len;
	struct wlan_bssid_ex *pnetwork;
	struct	mlme_priv *pmlmepriv = &(adapter->mlmepriv);

	pnetwork = (struct wlan_bssid_ex *)pbuf;

	len = get_wlan_bssid_ex_sz(pnetwork);
	if (len > (sizeof(struct wlan_bssid_ex)))
		return;

	spin_lock_bh(&pmlmepriv->lock);

	/*  update IBSS_network 's timestamp */
	if ((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)) == true) {
		if (!memcmp(&(pmlmepriv->cur_network.network.mac_address), pnetwork->mac_address, ETH_ALEN)) {
			struct wlan_network *ibss_wlan = NULL;

			memcpy(pmlmepriv->cur_network.network.ies, pnetwork->ies, 8);
			spin_lock_bh(&(pmlmepriv->scanned_queue.lock));
			ibss_wlan = rtw_find_network(&pmlmepriv->scanned_queue,  pnetwork->mac_address);
			if (ibss_wlan) {
				memcpy(ibss_wlan->network.ies, pnetwork->ies, 8);
				spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));
				goto exit;
			}
			spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));
		}
	}

	/*  lock pmlmepriv->lock when you accessing network_q */
	if ((check_fwstate(pmlmepriv, _FW_UNDER_LINKING)) == false) {
		if (pnetwork->ssid.ssid[0] == 0)
			pnetwork->ssid.ssid_length = 0;
		rtw_add_network(adapter, pnetwork);
	}

exit:

	spin_unlock_bh(&pmlmepriv->lock);
}

void rtw_surveydone_event_callback(struct adapter	*adapter, u8 *pbuf)
{
	struct	mlme_priv *pmlmepriv = &(adapter->mlmepriv);

	spin_lock_bh(&pmlmepriv->lock);
	if (pmlmepriv->wps_probe_req_ie) {
		pmlmepriv->wps_probe_req_ie_len = 0;
		kfree(pmlmepriv->wps_probe_req_ie);
		pmlmepriv->wps_probe_req_ie = NULL;
	}

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY)) {
		spin_unlock_bh(&pmlmepriv->lock);
		del_timer_sync(&pmlmepriv->scan_to_timer);
		spin_lock_bh(&pmlmepriv->lock);
		_clr_fwstate_(pmlmepriv, _FW_UNDER_SURVEY);
	}

	rtw_set_signal_stat_timer(&adapter->recvpriv);

	if (pmlmepriv->to_join) {
		if ((check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == true)) {
			if (check_fwstate(pmlmepriv, _FW_LINKED) == false) {
				set_fwstate(pmlmepriv, _FW_UNDER_LINKING);

				if (rtw_select_and_join_from_scanned_queue(pmlmepriv) == _SUCCESS) {
					_set_timer(&pmlmepriv->assoc_timer, MAX_JOIN_TIMEOUT);
				} else {
					u8 ret = _SUCCESS;
					struct wlan_bssid_ex    *pdev_network = &(adapter->registrypriv.dev_network);
					u8 *pibss = adapter->registrypriv.dev_network.mac_address;

					/* pmlmepriv->fw_state ^= _FW_UNDER_SURVEY;because don't set assoc_timer */
					_clr_fwstate_(pmlmepriv, _FW_UNDER_SURVEY);

					memcpy(&pdev_network->ssid, &pmlmepriv->assoc_ssid, sizeof(struct ndis_802_11_ssid));

					rtw_update_registrypriv_dev_network(adapter);
					rtw_generate_random_ibss(pibss);

					pmlmepriv->fw_state = WIFI_ADHOC_MASTER_STATE;

					pmlmepriv->to_join = false;

					ret = rtw_createbss_cmd(adapter);
					if (ret != _SUCCESS)
						goto unlock;
				}
			}
		} else {
			int s_ret;

			set_fwstate(pmlmepriv, _FW_UNDER_LINKING);
			pmlmepriv->to_join = false;
			s_ret = rtw_select_and_join_from_scanned_queue(pmlmepriv);
			if (s_ret == _SUCCESS) {
				_set_timer(&pmlmepriv->assoc_timer, MAX_JOIN_TIMEOUT);
			} else if (s_ret == 2) {/* there is no need to wait for join */
				_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);
				rtw_indicate_connect(adapter);
			} else {
				if (rtw_to_roam(adapter) != 0) {
					if (rtw_dec_to_roam(adapter) == 0
						|| _SUCCESS != rtw_sitesurvey_cmd(adapter, &pmlmepriv->assoc_ssid, 1, NULL, 0)
					) {
						rtw_set_to_roam(adapter, 0);
						rtw_free_assoc_resources(adapter, 1);
						rtw_indicate_disconnect(adapter);
					} else {
						pmlmepriv->to_join = true;
					}
				} else
					rtw_indicate_disconnect(adapter);

				_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);
			}
		}
	} else {
		if (rtw_chk_roam_flags(adapter, RTW_ROAM_ACTIVE)) {
			if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)
				&& check_fwstate(pmlmepriv, _FW_LINKED)) {
				if (rtw_select_roaming_candidate(pmlmepriv) == _SUCCESS) {
					receive_disconnect(adapter, pmlmepriv->cur_network.network.mac_address
						, WLAN_REASON_ACTIVE_ROAM);
				}
			}
		}
	}

unlock:
	spin_unlock_bh(&pmlmepriv->lock);

	rtw_os_xmit_schedule(adapter);

	rtw_cfg80211_surveydone_event_callback(adapter);

	rtw_indicate_scan_done(adapter, false);
}

void rtw_dummy_event_callback(struct adapter *adapter, u8 *pbuf)
{
}

void rtw_fwdbg_event_callback(struct adapter *adapter, u8 *pbuf)
{
}

static void free_scanqueue(struct	mlme_priv *pmlmepriv)
{
	struct __queue *free_queue = &pmlmepriv->free_bss_pool;
	struct __queue *scan_queue = &pmlmepriv->scanned_queue;
	struct list_head	*plist, *phead, *ptemp;

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

static void rtw_reset_rx_info(struct debug_priv *pdbgpriv)
{
	pdbgpriv->dbg_rx_ampdu_drop_count = 0;
	pdbgpriv->dbg_rx_ampdu_forced_indicate_count = 0;
	pdbgpriv->dbg_rx_ampdu_loss_count = 0;
	pdbgpriv->dbg_rx_dup_mgt_frame_drop_count = 0;
	pdbgpriv->dbg_rx_ampdu_window_shift_cnt = 0;
}

static void find_network(struct adapter *adapter)
{
	struct wlan_network *pwlan = NULL;
	struct	mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct wlan_network *tgt_network = &pmlmepriv->cur_network;

	pwlan = rtw_find_network(&pmlmepriv->scanned_queue, tgt_network->network.mac_address);
	if (pwlan)
		pwlan->fixed = false;

	if (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) &&
	    (adapter->stapriv.asoc_sta_count == 1))
		rtw_free_network_nolock(adapter, pwlan);
}

/*
*rtw_free_assoc_resources: the caller has to lock pmlmepriv->lock
*/
void rtw_free_assoc_resources(struct adapter *adapter, int lock_scanned_queue)
{
	struct	mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct wlan_network *tgt_network = &pmlmepriv->cur_network;
	struct dvobj_priv *psdpriv = adapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE|WIFI_AP_STATE)) {
		struct sta_info *psta;

		psta = rtw_get_stainfo(&adapter->stapriv, tgt_network->network.mac_address);
		rtw_free_stainfo(adapter,  psta);
	}

	if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE|WIFI_AP_STATE)) {
		struct sta_info *psta;

		rtw_free_all_stainfo(adapter);

		psta = rtw_get_bcmc_stainfo(adapter);
		rtw_free_stainfo(adapter, psta);

		rtw_init_bcmc_stainfo(adapter);
	}

	find_network(adapter);

	if (lock_scanned_queue)
		adapter->securitypriv.key_mask = 0;

	rtw_reset_rx_info(pdbgpriv);
}

/*
*rtw_indicate_connect: the caller has to lock pmlmepriv->lock
*/
void rtw_indicate_connect(struct adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	pmlmepriv->to_join = false;

	if (!check_fwstate(&padapter->mlmepriv, _FW_LINKED)) {

		set_fwstate(pmlmepriv, _FW_LINKED);

		rtw_os_indicate_connect(padapter);
	}

	rtw_set_to_roam(padapter, 0);
	rtw_set_scan_deny(padapter, 3000);

}

/*
*rtw_indicate_disconnect: the caller has to lock pmlmepriv->lock
*/
void rtw_indicate_disconnect(struct adapter *padapter)
{
	struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;

	_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING|WIFI_UNDER_WPS);

	if (rtw_to_roam(padapter) > 0)
		_clr_fwstate_(pmlmepriv, _FW_LINKED);

	if (check_fwstate(&padapter->mlmepriv, _FW_LINKED)
		|| (rtw_to_roam(padapter) <= 0)
	) {
		rtw_os_indicate_disconnect(padapter);

		/* set ips_deny_time to avoid enter IPS before LPS leave */
		rtw_set_ips_deny(padapter, 3000);

		_clr_fwstate_(pmlmepriv, _FW_LINKED);

		rtw_clear_scan_deny(padapter);
	}

	rtw_lps_ctrl_wk_cmd(padapter, LPS_CTRL_DISCONNECT, 1);
}

inline void rtw_indicate_scan_done(struct adapter *padapter, bool aborted)
{
	rtw_os_indicate_scan_done(padapter, aborted);

	if (is_primary_adapter(padapter) &&
	    (!adapter_to_pwrctl(padapter)->bInSuspend) &&
	    (!check_fwstate(&padapter->mlmepriv,
			    WIFI_ASOC_STATE|WIFI_UNDER_LINKING))) {
		rtw_set_ips_deny(padapter, 0);
		_set_timer(&padapter->mlmepriv.dynamic_chk_timer, 1);
	}
}

void rtw_scan_abort(struct adapter *adapter)
{
	unsigned long start;
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &(adapter->mlmeextpriv);

	start = jiffies;
	pmlmeext->scan_abort = true;
	while (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY)
		&& jiffies_to_msecs(start) <= 200) {

		if (adapter->bDriverStopped || adapter->bSurpriseRemoved)
			break;

		msleep(20);
	}

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY))
		rtw_indicate_scan_done(adapter, true);

	pmlmeext->scan_abort = false;
}

static struct sta_info *rtw_joinbss_update_stainfo(struct adapter *padapter, struct wlan_network *pnetwork)
{
	int i;
	struct sta_info *bmc_sta, *psta = NULL;
	struct recv_reorder_ctrl *preorder_ctrl;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	psta = rtw_get_stainfo(pstapriv, pnetwork->network.mac_address);
	if (!psta)
		psta = rtw_alloc_stainfo(pstapriv, pnetwork->network.mac_address);

	if (psta) { /* update ptarget_sta */

		psta->aid  = pnetwork->join_res;

		update_sta_info(padapter, psta);

		/* update station supportRate */
		psta->bssratelen = rtw_get_rateset_len(pnetwork->network.supported_rates);
		memcpy(psta->bssrateset, pnetwork->network.supported_rates, psta->bssratelen);
		rtw_hal_update_sta_rate_mask(padapter, psta);

		psta->wireless_mode = pmlmeext->cur_wireless_mode;
		psta->raid = networktype_to_raid_ex(padapter, psta);

		/* sta mode */
		rtw_hal_set_odm_var(padapter, HAL_ODM_STA_INFO, psta, true);

		/* security related */
		if (padapter->securitypriv.dot11AuthAlgrthm == dot11AuthAlgrthm_8021X) {
			padapter->securitypriv.binstallGrpkey = false;
			padapter->securitypriv.busetkipkey = false;
			padapter->securitypriv.bgrpkey_handshake = false;

			psta->ieee8021x_blocked = true;
			psta->dot118021XPrivacy = padapter->securitypriv.dot11PrivacyAlgrthm;

			memset((u8 *)&psta->dot118021x_UncstKey, 0, sizeof(union Keytype));

			memset((u8 *)&psta->dot11tkiprxmickey, 0, sizeof(union Keytype));
			memset((u8 *)&psta->dot11tkiptxmickey, 0, sizeof(union Keytype));

			memset((u8 *)&psta->dot11txpn, 0, sizeof(union pn48));
			psta->dot11txpn.val = psta->dot11txpn.val + 1;
			memset((u8 *)&psta->dot11wtxpn, 0, sizeof(union pn48));
			memset((u8 *)&psta->dot11rxpn, 0, sizeof(union pn48));
		}

		/* When doing the WPS, the wps_ie_len won't equal to 0 */
		/* And the Wi-Fi driver shouldn't allow the data packet to be transmitted. */
		if (padapter->securitypriv.wps_ie_len != 0) {
			psta->ieee8021x_blocked = true;
			padapter->securitypriv.wps_ie_len = 0;
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

		bmc_sta = rtw_get_bcmc_stainfo(padapter);
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
static void rtw_joinbss_update_network(struct adapter *padapter, struct wlan_network *ptarget_wlan, struct wlan_network  *pnetwork)
{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct wlan_network  *cur_network = &(pmlmepriv->cur_network);

	/*  why not use ptarget_wlan?? */
	memcpy(&cur_network->network, &pnetwork->network, pnetwork->network.length);
	/*  some ies in pnetwork is wrong, so we should use ptarget_wlan ies */
	cur_network->network.ie_length = ptarget_wlan->network.ie_length;
	memcpy(&cur_network->network.ies[0], &ptarget_wlan->network.ies[0], MAX_IE_SZ);

	cur_network->aid = pnetwork->join_res;

	rtw_set_signal_stat_timer(&padapter->recvpriv);

	padapter->recvpriv.signal_strength = ptarget_wlan->network.phy_info.signal_strength;
	padapter->recvpriv.signal_qual = ptarget_wlan->network.phy_info.signal_quality;
	/* the ptarget_wlan->network.rssi is raw data, we use ptarget_wlan->network.phy_info.signal_strength instead (has scaled) */
	padapter->recvpriv.rssi = translate_percentage_to_dbm(ptarget_wlan->network.phy_info.signal_strength);

	rtw_set_signal_stat_timer(&padapter->recvpriv);

	/* update fw_state will clr _FW_UNDER_LINKING here indirectly */
	switch (pnetwork->network.infrastructure_mode) {
	case Ndis802_11Infrastructure:

			if (pmlmepriv->fw_state&WIFI_UNDER_WPS)
				pmlmepriv->fw_state = WIFI_STATION_STATE|WIFI_UNDER_WPS;
			else
				pmlmepriv->fw_state = WIFI_STATION_STATE;

			break;
	case Ndis802_11IBSS:
			pmlmepriv->fw_state = WIFI_ADHOC_STATE;
			break;
	default:
			pmlmepriv->fw_state = WIFI_NULL_STATE;
			break;
	}

	rtw_update_protection(padapter, (cur_network->network.ies) + sizeof(struct ndis_802_11_fix_ie),
									(cur_network->network.ie_length));

	rtw_update_ht_cap(padapter, cur_network->network.ies, cur_network->network.ie_length, (u8) cur_network->network.configuration.ds_config);
}

/* Notes: the function could be > passive_level (the same context as Rx tasklet) */
/* pnetwork : returns from rtw_joinbss_event_callback */
/* ptarget_wlan: found from scanned_queue */
/* if join_res > 0, for (fw_state ==WIFI_STATION_STATE), we check if  "ptarget_sta" & "ptarget_wlan" exist. */
/* if join_res > 0, for (fw_state ==WIFI_ADHOC_STATE), we only check if "ptarget_wlan" exist. */
/* if join_res > 0, update "cur_network->network" from "pnetwork->network" if (ptarget_wlan != NULL). */
/*  */
/* define REJOIN */
void rtw_joinbss_event_prehandle(struct adapter *adapter, u8 *pbuf)
{
	static u8 __maybe_unused retry;
	struct sta_info *ptarget_sta = NULL, *pcur_sta = NULL;
	struct	sta_priv *pstapriv = &adapter->stapriv;
	struct	mlme_priv *pmlmepriv = &(adapter->mlmepriv);
	struct wlan_network	*pnetwork	= (struct wlan_network *)pbuf;
	struct wlan_network	*cur_network = &(pmlmepriv->cur_network);
	struct wlan_network	*pcur_wlan = NULL, *ptarget_wlan = NULL;
	unsigned int		the_same_macaddr = false;

	rtw_get_encrypt_decrypt_from_registrypriv(adapter);

	the_same_macaddr = !memcmp(pnetwork->network.mac_address, cur_network->network.mac_address, ETH_ALEN);

	pnetwork->network.length = get_wlan_bssid_ex_sz(&pnetwork->network);
	if (pnetwork->network.length > sizeof(struct wlan_bssid_ex))
		return;

	spin_lock_bh(&pmlmepriv->lock);

	pmlmepriv->LinkDetectInfo.TrafficTransitionCount = 0;
	pmlmepriv->LinkDetectInfo.LowPowerTransitionCount = 0;

	if (pnetwork->join_res > 0) {
		spin_lock_bh(&(pmlmepriv->scanned_queue.lock));
		retry = 0;
		if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING)) {
			/* s1. find ptarget_wlan */
			if (check_fwstate(pmlmepriv, _FW_LINKED)) {
				if (the_same_macaddr) {
					ptarget_wlan = rtw_find_network(&pmlmepriv->scanned_queue, cur_network->network.mac_address);
				} else {
					pcur_wlan = rtw_find_network(&pmlmepriv->scanned_queue, cur_network->network.mac_address);
					if (pcur_wlan)
						pcur_wlan->fixed = false;

					pcur_sta = rtw_get_stainfo(pstapriv, cur_network->network.mac_address);
					if (pcur_sta)
						rtw_free_stainfo(adapter,  pcur_sta);

					ptarget_wlan = rtw_find_network(&pmlmepriv->scanned_queue, pnetwork->network.mac_address);
					if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == true) {
						if (ptarget_wlan)
							ptarget_wlan->fixed = true;
					}
				}

			} else {
				ptarget_wlan = _rtw_find_same_network(&pmlmepriv->scanned_queue, pnetwork);
				if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == true) {
					if (ptarget_wlan)
						ptarget_wlan->fixed = true;
				}
			}

			/* s2. update cur_network */
			if (ptarget_wlan) {
				rtw_joinbss_update_network(adapter, ptarget_wlan, pnetwork);
			} else {
				netdev_dbg(adapter->pnetdev,
					   "Can't find ptarget_wlan when joinbss_event callback\n");
				spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));
				goto ignore_joinbss_callback;
			}

			/* s3. find ptarget_sta & update ptarget_sta after update cur_network only for station mode */
			if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == true) {
				ptarget_sta = rtw_joinbss_update_stainfo(adapter, pnetwork);
				if (!ptarget_sta) {
					spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));
					goto ignore_joinbss_callback;
				}
			}

			/* s4. indicate connect */
			if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == true) {
				pmlmepriv->cur_network_scanned = ptarget_wlan;
				rtw_indicate_connect(adapter);
			}

			spin_unlock_bh(&pmlmepriv->scanned_queue.lock);

			spin_unlock_bh(&pmlmepriv->lock);
			/* s5. Cancel assoc_timer */
			del_timer_sync(&pmlmepriv->assoc_timer);
			spin_lock_bh(&pmlmepriv->lock);
		} else {
			spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));
		}
	} else if (pnetwork->join_res == -4) {
		rtw_reset_securitypriv(adapter);
		_set_timer(&pmlmepriv->assoc_timer, 1);

		if ((check_fwstate(pmlmepriv, _FW_UNDER_LINKING)) == true)
			_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);

	} else {/* if join_res < 0 (join fails), then try again */

		#ifdef REJOIN
		res = _FAIL;
		if (retry < 2)
			res = rtw_select_and_join_from_scanned_queue(pmlmepriv);

		if (res == _SUCCESS) {
			/* extend time of assoc_timer */
			_set_timer(&pmlmepriv->assoc_timer, MAX_JOIN_TIMEOUT);
			retry++;
		} else if (res == 2) {/* there is no need to wait for join */
			_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);
			rtw_indicate_connect(adapter);
		} else {
		#endif

			_set_timer(&pmlmepriv->assoc_timer, 1);
			_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);

		#ifdef REJOIN
			retry = 0;
		}
		#endif
	}

ignore_joinbss_callback:

	spin_unlock_bh(&pmlmepriv->lock);
}

void rtw_joinbss_event_callback(struct adapter *adapter, u8 *pbuf)
{
	struct wlan_network	*pnetwork	= (struct wlan_network *)pbuf;

	mlmeext_joinbss_event_callback(adapter, pnetwork->join_res);

	rtw_os_xmit_schedule(adapter);
}

/* FOR STA, AP , AD-HOC mode */
void rtw_sta_media_status_rpt(struct adapter *adapter, struct sta_info *psta, u32 mstatus)
{
	u16 media_status_rpt;

	if (!psta)
		return;

	media_status_rpt = (u16)((psta->mac_id<<8)|mstatus); /*   MACID|OPMODE:1 connect */
	rtw_hal_set_hwreg(adapter, HW_VAR_H2C_MEDIA_STATUS_RPT, (u8 *)&media_status_rpt);
}

void rtw_stassoc_event_callback(struct adapter *adapter, u8 *pbuf)
{
	struct sta_info *psta;
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);
	struct stassoc_event	*pstassoc	= (struct stassoc_event *)pbuf;
	struct wlan_network	*cur_network = &(pmlmepriv->cur_network);
	struct wlan_network	*ptarget_wlan = NULL;

	if (rtw_access_ctrl(adapter, pstassoc->macaddr) == false)
		return;

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		psta = rtw_get_stainfo(&adapter->stapriv, pstassoc->macaddr);
		if (psta) {
			u8 *passoc_req = NULL;
			u32 assoc_req_len = 0;

			rtw_sta_media_status_rpt(adapter, psta, 1);

			ap_sta_info_defer_update(adapter, psta);

			/* report to upper layer */
			spin_lock_bh(&psta->lock);
			if (psta->passoc_req && psta->assoc_req_len > 0) {
				passoc_req = rtw_zmalloc(psta->assoc_req_len);
				if (passoc_req) {
					assoc_req_len = psta->assoc_req_len;
					memcpy(passoc_req, psta->passoc_req, assoc_req_len);

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

	if (adapter->securitypriv.dot11AuthAlgrthm == dot11AuthAlgrthm_8021X)
		psta->dot118021XPrivacy = adapter->securitypriv.dot11PrivacyAlgrthm;

	psta->ieee8021x_blocked = false;

	spin_lock_bh(&pmlmepriv->lock);

	if ((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == true) ||
		(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == true)) {
		if (adapter->stapriv.asoc_sta_count == 2) {
			spin_lock_bh(&(pmlmepriv->scanned_queue.lock));
			ptarget_wlan = rtw_find_network(&pmlmepriv->scanned_queue, cur_network->network.mac_address);
			pmlmepriv->cur_network_scanned = ptarget_wlan;
			if (ptarget_wlan)
				ptarget_wlan->fixed = true;
			spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));
			/*  a sta + bc/mc_stainfo (not Ibss_stainfo) */
			rtw_indicate_connect(adapter);
		}
	}

	spin_unlock_bh(&pmlmepriv->lock);

	mlmeext_sta_add_event_callback(adapter, psta);
}

void rtw_stadel_event_callback(struct adapter *adapter, u8 *pbuf)
{
	int mac_id = (-1);
	struct sta_info *psta;
	struct wlan_network *pwlan = NULL;
	struct wlan_bssid_ex    *pdev_network = NULL;
	u8 *pibss = NULL;
	struct	mlme_priv *pmlmepriv = &(adapter->mlmepriv);
	struct	stadel_event *pstadel	= (struct stadel_event *)pbuf;
	struct wlan_network *tgt_network = &(pmlmepriv->cur_network);
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);

	psta = rtw_get_stainfo(&adapter->stapriv, pstadel->macaddr);
	if (psta)
		mac_id = psta->mac_id;
	else
		mac_id = pstadel->mac_id;

	if (mac_id >= 0) {
		u16 media_status;

		media_status = (mac_id<<8)|0; /*   MACID|OPMODE:0 means disconnect */
		/* for STA, AP, ADHOC mode, report disconnect stauts to FW */
		rtw_hal_set_hwreg(adapter, HW_VAR_H2C_MEDIA_STATUS_RPT, (u8 *)&media_status);
	}

	/* if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) */
	if ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)
		return;

	mlmeext_sta_del_event_callback(adapter);

	spin_lock_bh(&pmlmepriv->lock);

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
		u16 reason = *((unsigned short *)(pstadel->rsvd));
		bool roam = false;
		struct wlan_network *roam_target = NULL;

		if (adapter->registrypriv.wifi_spec == 1) {
			roam = false;
		} else if (reason == WLAN_REASON_EXPIRATION_CHK && rtw_chk_roam_flags(adapter, RTW_ROAM_ON_EXPIRED)) {
			roam = true;
		} else if (reason == WLAN_REASON_ACTIVE_ROAM && rtw_chk_roam_flags(adapter, RTW_ROAM_ACTIVE)) {
			roam = true;
			roam_target = pmlmepriv->roam_network;
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

		spin_lock_bh(&(pmlmepriv->scanned_queue.lock));
		/*  remove the network entry in scanned_queue */
		pwlan = rtw_find_network(&pmlmepriv->scanned_queue, tgt_network->network.mac_address);
		if (pwlan) {
			pwlan->fixed = false;
			rtw_free_network_nolock(adapter, pwlan);
		}
		spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));

		_rtw_roaming(adapter, roam_target);
	}

	if (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) ||
	      check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)) {

		rtw_free_stainfo(adapter,  psta);

		if (adapter->stapriv.asoc_sta_count == 1) {/* a sta + bc/mc_stainfo (not Ibss_stainfo) */
			u8 ret = _SUCCESS;
			spin_lock_bh(&(pmlmepriv->scanned_queue.lock));
			/* free old ibss network */
			pwlan = rtw_find_network(&pmlmepriv->scanned_queue, tgt_network->network.mac_address);
			if (pwlan) {
				pwlan->fixed = false;
				rtw_free_network_nolock(adapter, pwlan);
			}
			spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));
			/* re-create ibss */
			pdev_network = &(adapter->registrypriv.dev_network);
			pibss = adapter->registrypriv.dev_network.mac_address;

			memcpy(pdev_network, &tgt_network->network, get_wlan_bssid_ex_sz(&tgt_network->network));

			memcpy(&pdev_network->ssid, &pmlmepriv->assoc_ssid, sizeof(struct ndis_802_11_ssid));

			rtw_update_registrypriv_dev_network(adapter);

			rtw_generate_random_ibss(pibss);

			if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)) {
				set_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE);
				_clr_fwstate_(pmlmepriv, WIFI_ADHOC_STATE);
			}

			ret = rtw_createbss_cmd(adapter);
			if (ret != _SUCCESS)
				goto unlock;
		}

	}

unlock:
	spin_unlock_bh(&pmlmepriv->lock);
}

void rtw_cpwm_event_callback(struct adapter *padapter, u8 *pbuf)
{
	struct reportpwrstate_parm *preportpwrstate;

	preportpwrstate = (struct reportpwrstate_parm *)pbuf;
	preportpwrstate->state |= (u8)(adapter_to_pwrctl(padapter)->cpwm_tog + 0x80);
	cpwm_int_hdl(padapter, preportpwrstate);
}

void rtw_wmm_event_callback(struct adapter *padapter, u8 *pbuf)
{
	WMMOnAssocRsp(padapter);
}

/*
* _rtw_join_timeout_handler - Timeout/failure handler for CMD JoinBss
* @adapter: pointer to struct adapter structure
*/
void _rtw_join_timeout_handler(struct timer_list *t)
{
	struct adapter *adapter = from_timer(adapter, t,
						  mlmepriv.assoc_timer);
	struct	mlme_priv *pmlmepriv = &adapter->mlmepriv;

	if (adapter->bDriverStopped || adapter->bSurpriseRemoved)
		return;

	spin_lock_bh(&pmlmepriv->lock);

	if (rtw_to_roam(adapter) > 0) { /* join timeout caused by roaming */
		while (1) {
			rtw_dec_to_roam(adapter);
			if (rtw_to_roam(adapter) != 0) { /* try another */
				int do_join_r;

				do_join_r = rtw_do_join(adapter);
				if (do_join_r != _SUCCESS)
					continue;

				break;
			} else {
				rtw_indicate_disconnect(adapter);
				break;
			}
		}

	} else {
		rtw_indicate_disconnect(adapter);
		free_scanqueue(pmlmepriv);/*  */

		/* indicate disconnect for the case that join_timeout and check_fwstate != FW_LINKED */
		rtw_cfg80211_indicate_disconnect(adapter);

	}

	spin_unlock_bh(&pmlmepriv->lock);
}

/*
* rtw_scan_timeout_handler - Timeout/Failure handler for CMD SiteSurvey
* @adapter: pointer to struct adapter structure
*/
void rtw_scan_timeout_handler(struct timer_list *t)
{
	struct adapter *adapter = from_timer(adapter, t,
						  mlmepriv.scan_to_timer);
	struct	mlme_priv *pmlmepriv = &adapter->mlmepriv;

	spin_lock_bh(&pmlmepriv->lock);

	_clr_fwstate_(pmlmepriv, _FW_UNDER_SURVEY);

	spin_unlock_bh(&pmlmepriv->lock);

	rtw_indicate_scan_done(adapter, true);
}

void rtw_mlme_reset_auto_scan_int(struct adapter *adapter)
{
	struct mlme_priv *mlme = &adapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);

	if (pmlmeinfo->VHT_enable) /* disable auto scan when connect to 11AC AP */
		mlme->auto_scan_int_ms = 0;
	else if (adapter->registrypriv.wifi_spec && is_client_associated_to_ap(adapter) == true)
		mlme->auto_scan_int_ms = 60*1000;
	else if (rtw_chk_roam_flags(adapter, RTW_ROAM_ACTIVE)) {
		if (check_fwstate(mlme, WIFI_STATION_STATE) && check_fwstate(mlme, _FW_LINKED))
			mlme->auto_scan_int_ms = mlme->roam_scan_int_ms;
	} else
		mlme->auto_scan_int_ms = 0; /* disabled */
}

static void rtw_auto_scan_handler(struct adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	rtw_mlme_reset_auto_scan_int(padapter);

	if (pmlmepriv->auto_scan_int_ms != 0
		&& jiffies_to_msecs(jiffies - pmlmepriv->scan_start_time) > pmlmepriv->auto_scan_int_ms) {

		if (!padapter->registrypriv.wifi_spec) {
			if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY | _FW_UNDER_LINKING) == true)
				goto exit;

			if (pmlmepriv->LinkDetectInfo.bBusyTraffic)
				goto exit;
		}

		rtw_set_802_11_bssid_list_scan(padapter, NULL, 0);
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

	if (adapter->bDriverStopped || adapter->bSurpriseRemoved)
		return;

	if (adapter->net_closed)
		return;

	if ((adapter_to_pwrctl(adapter)->fw_current_in_ps_mode)
		&& !(hal_btcoex_IsBtControlLps(adapter))
		) {
		u8 bEnterPS;

		linked_status_chk(adapter);

		bEnterPS = traffic_status_watchdog(adapter, 1);
		if (bEnterPS) {
			/* rtw_lps_ctrl_wk_cmd(adapter, LPS_CTRL_ENTER, 1); */
			rtw_hal_dm_watchdog_in_lps(adapter);
		} else {
			/* call rtw_lps_ctrl_wk_cmd(padapter, LPS_CTRL_LEAVE, 1) in traffic_status_watchdog() */
		}

	} else {
		if (is_primary_adapter(adapter))
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

	if (is_same_ess(&competitor->network, &mlme->cur_network.network) == false)
		goto exit;

	if (rtw_is_desired_network(adapter, competitor) == false)
		goto exit;

	/* got specific addr to roam */
	if (!is_zero_mac_addr(mlme->roam_tgt_addr)) {
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
	struct list_head	*phead;
	struct __queue	*queue	= &(mlme->scanned_queue);
	struct	wlan_network	*pnetwork = NULL;
	struct	wlan_network	*candidate = NULL;

	if (!mlme->cur_network_scanned) {
		rtw_warn_on(1);
		return ret;
	}

	spin_lock_bh(&(mlme->scanned_queue.lock));
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
	spin_unlock_bh(&(mlme->scanned_queue.lock));

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
		if (competitor->network.ssid.ssid_length != mlme->assoc_ssid.ssid_length
			|| memcmp(competitor->network.ssid.ssid, mlme->assoc_ssid.ssid, mlme->assoc_ssid.ssid_length)
		)
			goto exit;
	}

	if (rtw_is_desired_network(adapter, competitor)  == false)
		goto exit;

	if (rtw_to_roam(adapter) > 0) {
		if (jiffies_to_msecs(jiffies - competitor->last_scanned) >= mlme->roam_scanr_exp_ms
			|| is_same_ess(&competitor->network, &mlme->cur_network.network) == false
		)
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
Calling context:
The caller of the sub-routine will be in critical section...
The caller must hold the following spinlock
pmlmepriv->lock
*/

int rtw_select_and_join_from_scanned_queue(struct mlme_priv *pmlmepriv)
{
	int ret;
	struct list_head	*phead;
	struct adapter *adapter;
	struct __queue	*queue	= &(pmlmepriv->scanned_queue);
	struct	wlan_network	*pnetwork = NULL;
	struct	wlan_network	*candidate = NULL;

	adapter = (struct adapter *)pmlmepriv->nic_hdl;

	spin_lock_bh(&(pmlmepriv->scanned_queue.lock));

	if (pmlmepriv->roam_network) {
		candidate = pmlmepriv->roam_network;
		pmlmepriv->roam_network = NULL;
		goto candidate_exist;
	}

	phead = get_list_head(queue);
	list_for_each(pmlmepriv->pscanned, phead) {

		pnetwork = list_entry(pmlmepriv->pscanned,
				      struct wlan_network, list);

		rtw_check_join_candidate(pmlmepriv, &candidate, pnetwork);

	}

	if (!candidate) {
		ret = _FAIL;
		goto exit;
	} else {
		goto candidate_exist;
	}

candidate_exist:

	/*  check for situation of  _FW_LINKED */
	if (check_fwstate(pmlmepriv, _FW_LINKED) == true) {
		rtw_disassoc_cmd(adapter, 0, true);
		rtw_indicate_disconnect(adapter);
		rtw_free_assoc_resources(adapter, 0);
	}

	set_fwstate(pmlmepriv, _FW_UNDER_LINKING);
	ret = rtw_joinbss_cmd(adapter, candidate);

exit:
	spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));
	return ret;
}

signed int rtw_set_auth(struct adapter *adapter, struct security_priv *psecuritypriv)
{
	struct	cmd_obj *pcmd;
	struct	setauth_parm *psetauthparm;
	struct	cmd_priv *pcmdpriv = &(adapter->cmdpriv);
	signed int		res = _SUCCESS;

	pcmd = rtw_zmalloc(sizeof(struct cmd_obj));
	if (!pcmd) {
		res = _FAIL;  /* try again */
		goto exit;
	}

	psetauthparm = rtw_zmalloc(sizeof(struct setauth_parm));
	if (!psetauthparm) {
		kfree(pcmd);
		res = _FAIL;
		goto exit;
	}

	psetauthparm->mode = (unsigned char)psecuritypriv->dot11AuthAlgrthm;

	pcmd->cmdcode = _SetAuth_CMD_;
	pcmd->parmbuf = (unsigned char *)psetauthparm;
	pcmd->cmdsz =  (sizeof(struct setauth_parm));
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
	struct cmd_obj		*pcmd;
	struct setkey_parm	*psetkeyparm;
	struct cmd_priv 	*pcmdpriv = &(adapter->cmdpriv);
	signed int	res = _SUCCESS;

	psetkeyparm = rtw_zmalloc(sizeof(struct setkey_parm));
	if (!psetkeyparm) {
		res = _FAIL;
		goto exit;
	}

	if (psecuritypriv->dot11AuthAlgrthm == dot11AuthAlgrthm_8021X)
		psetkeyparm->algorithm = (unsigned char)psecuritypriv->dot118021XGrpPrivacy;
	else
		psetkeyparm->algorithm = (u8)psecuritypriv->dot11PrivacyAlgrthm;

	psetkeyparm->keyid = (u8)keyid;/* 0~3 */
	psetkeyparm->set_tx = set_tx;
	if (is_wep_enc(psetkeyparm->algorithm))
		adapter->securitypriv.key_mask |= BIT(psetkeyparm->keyid);

	switch (psetkeyparm->algorithm) {

	case _WEP40_:
		keylen = 5;
		memcpy(&(psetkeyparm->key[0]), &(psecuritypriv->dot11DefKey[keyid].skey[0]), keylen);
		break;
	case _WEP104_:
		keylen = 13;
		memcpy(&(psetkeyparm->key[0]), &(psecuritypriv->dot11DefKey[keyid].skey[0]), keylen);
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
		kfree(psetkeyparm);
		goto exit;
	}

	if (enqueue) {
		pcmd = rtw_zmalloc(sizeof(struct cmd_obj));
		if (!pcmd) {
			kfree(psetkeyparm);
			res = _FAIL;  /* try again */
			goto exit;
		}

		pcmd->cmdcode = _SetKey_CMD_;
		pcmd->parmbuf = (u8 *)psetkeyparm;
		pcmd->cmdsz =  (sizeof(struct setkey_parm));
		pcmd->rsp = NULL;
		pcmd->rspsz = 0;

		INIT_LIST_HEAD(&pcmd->list);

		res = rtw_enqueue_cmd(pcmdpriv, pcmd);
	} else {
		setkey_hdl(adapter, (u8 *)psetkeyparm);
		kfree(psetkeyparm);
	}
exit:
	return res;
}

/* adjust ies for rtw_joinbss_cmd in WMM */
int rtw_restruct_wmm_ie(struct adapter *adapter, u8 *in_ie, u8 *out_ie, uint in_len, uint initial_out_len)
{
	unsigned	int ielength = 0;
	unsigned int i, j;

	i = 12; /* after the fixed IE */
	while (i < in_len) {
		ielength = initial_out_len;

		if (in_ie[i] == 0xDD && in_ie[i+2] == 0x00 && in_ie[i+3] == 0x50  && in_ie[i+4] == 0xF2 && in_ie[i+5] == 0x02 && i+5 < in_len) { /* WMM element ID and OUI */
			for (j = i; j < i + 9; j++) {
				out_ie[ielength] = in_ie[j];
				ielength++;
			}
			out_ie[initial_out_len + 1] = 0x07;
			out_ie[initial_out_len + 6] = 0x00;
			out_ie[initial_out_len + 8] = 0x00;

			break;
		}

		i += (in_ie[i+1]+2); /*  to the next IE element */
	}

	return ielength;

}

/*  */
/*  Ported from 8185: IsInPreAuthKeyList(). (Renamed from SecIsInPreAuthKeyList(), 2006-10-13.) */
/*  Added by Annie, 2006-05-07. */
/*  */
/*  Search by BSSID, */
/*  Return Value: */
/* 		-1		:if there is no pre-auth key in the  table */
/* 		>= 0		:if there is pre-auth key, and   return the entry id */
/*  */
/*  */

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

/*  */
/*  Check the RSN IE length */
/*  If the RSN IE length <= 20, the RSN IE didn't include the PMKID information */
/*  0-11th element in the array are the fixed IE */
/*  12th element in the array is the IE */
/*  13th element in the array is the IE length */
/*  */

static int rtw_append_pmkid(struct adapter *Adapter, int iEntry, u8 *ie, uint ie_len)
{
	struct security_priv *psecuritypriv = &Adapter->securitypriv;

	if (ie[13] <= 20) {
		/*  The RSN IE didn't include the PMK ID, append the PMK information */
		ie[ie_len] = 1;
		ie_len++;
		ie[ie_len] = 0;	/* PMKID count = 0x0100 */
		ie_len++;
		memcpy(&ie[ie_len], &psecuritypriv->PMKIDList[iEntry].PMKID, 16);
		ie_len += 16;
		ie[13] += 18;/* PMKID length = 2+16 */
	}
	return ie_len;
}

signed int rtw_restruct_sec_ie(struct adapter *adapter, u8 *in_ie, u8 *out_ie, uint in_len)
{
	u8 authmode = 0x0;
	uint	ielength;
	int iEntry;

	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct security_priv *psecuritypriv = &adapter->securitypriv;
	uint	ndisauthmode = psecuritypriv->ndisauthtype;

	/* copy fixed ie only */
	memcpy(out_ie, in_ie, 12);
	ielength = 12;
	if ((ndisauthmode == Ndis802_11AuthModeWPA) || (ndisauthmode == Ndis802_11AuthModeWPAPSK))
		authmode = WLAN_EID_VENDOR_SPECIFIC;
	if ((ndisauthmode == Ndis802_11AuthModeWPA2) || (ndisauthmode == Ndis802_11AuthModeWPA2PSK))
		authmode = WLAN_EID_RSN;

	if (check_fwstate(pmlmepriv, WIFI_UNDER_WPS)) {
		memcpy(out_ie+ielength, psecuritypriv->wps_ie, psecuritypriv->wps_ie_len);

		ielength += psecuritypriv->wps_ie_len;
	} else if ((authmode == WLAN_EID_VENDOR_SPECIFIC) || (authmode == WLAN_EID_RSN)) {
		/* copy RSN or SSN */
		memcpy(&out_ie[ielength], &psecuritypriv->supplicant_ie[0], psecuritypriv->supplicant_ie[1]+2);
		ielength += psecuritypriv->supplicant_ie[1]+2;
		rtw_report_sec_ie(adapter, authmode, psecuritypriv->supplicant_ie);
	}

	iEntry = SecIsInPMKIDList(adapter, pmlmepriv->assoc_bssid);
	if (iEntry < 0) {
		return ielength;
	} else {
		if (authmode == WLAN_EID_RSN)
			ielength = rtw_append_pmkid(adapter, iEntry, out_ie, ielength);
	}
	return ielength;
}

void rtw_init_registrypriv_dev_network(struct adapter *adapter)
{
	struct registry_priv *pregistrypriv = &adapter->registrypriv;
	struct eeprom_priv *peepriv = &adapter->eeprompriv;
	struct wlan_bssid_ex    *pdev_network = &pregistrypriv->dev_network;
	u8 *myhwaddr = myid(peepriv);

	memcpy(pdev_network->mac_address, myhwaddr, ETH_ALEN);

	memcpy(&pdev_network->ssid, &pregistrypriv->ssid, sizeof(struct ndis_802_11_ssid));

	pdev_network->configuration.length = sizeof(struct ndis_802_11_conf);
	pdev_network->configuration.beacon_period = 100;
}

void rtw_update_registrypriv_dev_network(struct adapter *adapter)
{
	int sz = 0;
	struct registry_priv *pregistrypriv = &adapter->registrypriv;
	struct wlan_bssid_ex    *pdev_network = &pregistrypriv->dev_network;
	struct	security_priv *psecuritypriv = &adapter->securitypriv;
	struct	wlan_network	*cur_network = &adapter->mlmepriv.cur_network;

	pdev_network->privacy = (psecuritypriv->dot11PrivacyAlgrthm > 0 ? 1 : 0) ; /*  adhoc no 802.1x */

	pdev_network->rssi = 0;

	switch (pregistrypriv->wireless_mode) {
	case WIRELESS_11B:
		pdev_network->network_type_in_use = (Ndis802_11DS);
		break;
	case WIRELESS_11G:
	case WIRELESS_11BG:
	case WIRELESS_11_24N:
	case WIRELESS_11G_24N:
	case WIRELESS_11BG_24N:
		pdev_network->network_type_in_use = (Ndis802_11OFDM24);
		break;
	default:
		/*  TODO */
		break;
	}

	pdev_network->configuration.ds_config = (pregistrypriv->channel);

	if (cur_network->network.infrastructure_mode == Ndis802_11IBSS)
		pdev_network->configuration.atim_window = (0);

	pdev_network->infrastructure_mode = (cur_network->network.infrastructure_mode);

	/*  1. Supported rates */
	/*  2. IE */

	/* rtw_set_supported_rate(pdev_network->supported_rates, pregistrypriv->wireless_mode) ;  will be called in rtw_generate_ie */
	sz = rtw_generate_ie(pregistrypriv);

	pdev_network->ie_length = sz;

	pdev_network->length = get_wlan_bssid_ex_sz((struct wlan_bssid_ex  *)pdev_network);

	/* notes: translate ie_length & length after assign the length to cmdsz in createbss_cmd(); */
	/* pdev_network->ie_length = cpu_to_le32(sz); */
}

void rtw_get_encrypt_decrypt_from_registrypriv(struct adapter *adapter)
{
}

/* the function is at passive_level */
void rtw_joinbss_reset(struct adapter *padapter)
{
	u8 threshold;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	struct ht_priv 	*phtpriv = &pmlmepriv->htpriv;

	/* todo: if you want to do something io/reg/hw setting before join_bss, please add code here */

	pmlmepriv->num_FortyMHzIntolerant = 0;

	pmlmepriv->num_sta_no_ht = 0;

	phtpriv->ampdu_enable = false;/* reset to disabled */

	/*  TH = 1 => means that invalidate usb rx aggregation */
	/*  TH = 0 => means that validate usb rx aggregation, use init value. */
	if (phtpriv->ht_option) {
		if (padapter->registrypriv.wifi_spec == 1)
			threshold = 1;
		else
			threshold = 0;
		rtw_hal_set_hwreg(padapter, HW_VAR_RXDMA_AGG_PG_TH, (u8 *)(&threshold));
	} else {
		threshold = 1;
		rtw_hal_set_hwreg(padapter, HW_VAR_RXDMA_AGG_PG_TH, (u8 *)(&threshold));
	}
}

void rtw_ht_use_default_setting(struct adapter *padapter)
{
	struct mlme_priv 	*pmlmepriv = &padapter->mlmepriv;
	struct ht_priv 	*phtpriv = &pmlmepriv->htpriv;
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	bool		bHwLDPCSupport = false, bHwSTBCSupport = false;
	bool		bHwSupportBeamformer = false, bHwSupportBeamformee = false;

	if (pregistrypriv->wifi_spec)
		phtpriv->bss_coexist = 1;
	else
		phtpriv->bss_coexist = 0;

	phtpriv->sgi_40m = TEST_FLAG(pregistrypriv->short_gi, BIT1) ? true : false;
	phtpriv->sgi_20m = TEST_FLAG(pregistrypriv->short_gi, BIT0) ? true : false;

	/*  LDPC support */
	rtw_hal_get_def_var(padapter, HAL_DEF_RX_LDPC, (u8 *)&bHwLDPCSupport);
	CLEAR_FLAGS(phtpriv->ldpc_cap);
	if (bHwLDPCSupport) {
		if (TEST_FLAG(pregistrypriv->ldpc_cap, BIT4))
			SET_FLAG(phtpriv->ldpc_cap, LDPC_HT_ENABLE_RX);
	}
	rtw_hal_get_def_var(padapter, HAL_DEF_TX_LDPC, (u8 *)&bHwLDPCSupport);
	if (bHwLDPCSupport) {
		if (TEST_FLAG(pregistrypriv->ldpc_cap, BIT5))
			SET_FLAG(phtpriv->ldpc_cap, LDPC_HT_ENABLE_TX);
	}

	/*  STBC */
	rtw_hal_get_def_var(padapter, HAL_DEF_TX_STBC, (u8 *)&bHwSTBCSupport);
	CLEAR_FLAGS(phtpriv->stbc_cap);
	if (bHwSTBCSupport) {
		if (TEST_FLAG(pregistrypriv->stbc_cap, BIT5))
			SET_FLAG(phtpriv->stbc_cap, STBC_HT_ENABLE_TX);
	}
	rtw_hal_get_def_var(padapter, HAL_DEF_RX_STBC, (u8 *)&bHwSTBCSupport);
	if (bHwSTBCSupport) {
		if (TEST_FLAG(pregistrypriv->stbc_cap, BIT4))
			SET_FLAG(phtpriv->stbc_cap, STBC_HT_ENABLE_RX);
	}

	/*  Beamforming setting */
	rtw_hal_get_def_var(padapter, HAL_DEF_EXPLICIT_BEAMFORMER, (u8 *)&bHwSupportBeamformer);
	rtw_hal_get_def_var(padapter, HAL_DEF_EXPLICIT_BEAMFORMEE, (u8 *)&bHwSupportBeamformee);
	CLEAR_FLAGS(phtpriv->beamform_cap);
	if (TEST_FLAG(pregistrypriv->beamform_cap, BIT4) && bHwSupportBeamformer)
		SET_FLAG(phtpriv->beamform_cap, BEAMFORMING_HT_BEAMFORMER_ENABLE);

	if (TEST_FLAG(pregistrypriv->beamform_cap, BIT5) && bHwSupportBeamformee)
		SET_FLAG(phtpriv->beamform_cap, BEAMFORMING_HT_BEAMFORMEE_ENABLE);
}

void rtw_build_wmm_ie_ht(struct adapter *padapter, u8 *out_ie, uint *pout_len)
{
	unsigned char WMM_IE[] = {0x00, 0x50, 0xf2, 0x02, 0x00, 0x01, 0x00};
	int out_len;

	if (padapter->mlmepriv.qospriv.qos_option == 0) {
		out_len = *pout_len;
		rtw_set_ie(out_ie+out_len, WLAN_EID_VENDOR_SPECIFIC,
			   _WMM_IE_Length_, WMM_IE, pout_len);

		padapter->mlmepriv.qospriv.qos_option = 1;
	}
}

/* the function is >= passive_level */
unsigned int rtw_restructure_ht_ie(struct adapter *padapter, u8 *in_ie, u8 *out_ie, uint in_len, uint *pout_len, u8 channel)
{
	u32 ielen, out_len;
	enum ieee80211_max_ampdu_length_exp max_rx_ampdu_factor;
	unsigned char *p;
	struct ieee80211_ht_cap ht_capie;
	u8 cbw40_enable = 0, stbc_rx_enable = 0, operation_bw = 0;
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct ht_priv 	*phtpriv = &pmlmepriv->htpriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	phtpriv->ht_option = false;

	out_len = *pout_len;

	memset(&ht_capie, 0, sizeof(struct ieee80211_ht_cap));

	ht_capie.cap_info = cpu_to_le16(IEEE80211_HT_CAP_DSSSCCK40);

	if (phtpriv->sgi_20m)
		ht_capie.cap_info |= cpu_to_le16(IEEE80211_HT_CAP_SGI_20);

	/* Get HT BW */
	if (!in_ie) {
		/* TDLS: TODO 20/40 issue */
		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
			operation_bw = padapter->mlmeextpriv.cur_bwmode;
			if (operation_bw > CHANNEL_WIDTH_40)
				operation_bw = CHANNEL_WIDTH_40;
		} else
			/* TDLS: TODO 40? */
			operation_bw = CHANNEL_WIDTH_40;
	} else {
		p = rtw_get_ie(in_ie, WLAN_EID_HT_OPERATION, &ielen, in_len);
		if (p && (ielen == sizeof(struct ieee80211_ht_addt_info))) {
			struct HT_info_element *pht_info = (struct HT_info_element *)(p+2);

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
	if (channel > 14) {
		if ((pregistrypriv->bw_mode & 0xf0) > 0)
			cbw40_enable = 1;
	} else {
		if ((pregistrypriv->bw_mode & 0x0f) > 0)
			cbw40_enable = 1;
	}

	if ((cbw40_enable == 1) && (operation_bw == CHANNEL_WIDTH_40)) {
		ht_capie.cap_info |= cpu_to_le16(IEEE80211_HT_CAP_SUP_WIDTH);
		if (phtpriv->sgi_40m)
			ht_capie.cap_info |= cpu_to_le16(IEEE80211_HT_CAP_SGI_40);
	}

	if (TEST_FLAG(phtpriv->stbc_cap, STBC_HT_ENABLE_TX))
		ht_capie.cap_info |= cpu_to_le16(IEEE80211_HT_CAP_TX_STBC);

	/* todo: disable SM power save mode */
	ht_capie.cap_info |= cpu_to_le16(IEEE80211_HT_CAP_SM_PS);

	if (TEST_FLAG(phtpriv->stbc_cap, STBC_HT_ENABLE_RX)) {
		if ((channel <= 14 && pregistrypriv->rx_stbc == 0x1) ||	/* enable for 2.4GHz */
			(pregistrypriv->wifi_spec == 1))
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

		rtw_hal_get_def_var(padapter, HAL_DEF_RX_PACKET_OFFSET, &rx_packet_offset);
		rtw_hal_get_def_var(padapter, HAL_DEF_MAX_RECVBUF_SZ, &max_recvbuf_sz);
	}

	if (padapter->driver_rx_ampdu_factor != 0xFF)
		max_rx_ampdu_factor =
		  (enum ieee80211_max_ampdu_length_exp)padapter->driver_rx_ampdu_factor;
	else
		rtw_hal_get_def_var(padapter, HW_VAR_MAX_RX_AMPDU_FACTOR,
				    &max_rx_ampdu_factor);

	ht_capie.ampdu_params_info = (max_rx_ampdu_factor&0x03);

	if (padapter->securitypriv.dot11PrivacyAlgrthm == _AES_)
		ht_capie.ampdu_params_info |= (IEEE80211_HT_CAP_AMPDU_DENSITY&(0x07<<2));
	else
		ht_capie.ampdu_params_info |= (IEEE80211_HT_CAP_AMPDU_DENSITY&0x00);

	rtw_set_ie(out_ie+out_len, WLAN_EID_HT_CAPABILITY,
		   sizeof(struct ieee80211_ht_cap), (unsigned char *)&ht_capie, pout_len);

	phtpriv->ht_option = true;

	if (in_ie) {
		p = rtw_get_ie(in_ie, WLAN_EID_HT_OPERATION, &ielen, in_len);
		if (p && (ielen == sizeof(struct ieee80211_ht_addt_info))) {
			out_len = *pout_len;
			rtw_set_ie(out_ie+out_len, WLAN_EID_HT_OPERATION, ielen, p+2, pout_len);
		}
	}

	return phtpriv->ht_option;

}

/* the function is > passive_level (in critical_section) */
void rtw_update_ht_cap(struct adapter *padapter, u8 *pie, uint ie_len, u8 channel)
{
	u8 *p, max_ampdu_sz;
	int len;
	struct ieee80211_ht_cap *pht_capie;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct ht_priv 	*phtpriv = &pmlmepriv->htpriv;
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 cbw40_enable = 0;

	if (!phtpriv->ht_option)
		return;

	if ((!pmlmeinfo->HT_info_enable) || (!pmlmeinfo->HT_caps_enable))
		return;

	/* maybe needs check if ap supports rx ampdu. */
	if (!(phtpriv->ampdu_enable) && pregistrypriv->ampdu_enable == 1)
		phtpriv->ampdu_enable = true;

	/* check Max Rx A-MPDU Size */
	len = 0;
	p = rtw_get_ie(pie+sizeof(struct ndis_802_11_fix_ie), WLAN_EID_HT_CAPABILITY, &len, ie_len-sizeof(struct ndis_802_11_fix_ie));
	if (p && len > 0) {
		pht_capie = (struct ieee80211_ht_cap *)(p+2);
		max_ampdu_sz = (pht_capie->ampdu_params_info & IEEE80211_HT_CAP_AMPDU_FACTOR);
		max_ampdu_sz = 1 << (max_ampdu_sz+3); /*  max_ampdu_sz (kbytes); */

		phtpriv->rx_ampdu_maxlen = max_ampdu_sz;

	}

	len = 0;
	p = rtw_get_ie(pie+sizeof(struct ndis_802_11_fix_ie), WLAN_EID_HT_OPERATION, &len, ie_len-sizeof(struct ndis_802_11_fix_ie));
	if (p && len > 0) {
		/* todo: */
	}

	if (channel > 14) {
		if ((pregistrypriv->bw_mode & 0xf0) > 0)
			cbw40_enable = 1;
	} else {
		if ((pregistrypriv->bw_mode & 0x0f) > 0)
			cbw40_enable = 1;
	}

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

	/*  */
	/*  Config SM Power Save setting */
	/*  */
	pmlmeinfo->SM_PS =
		(le16_to_cpu(pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info) &
		 0x0C) >> 2;

	/*  */
	/*  Config current HT Protection mode. */
	/*  */
	pmlmeinfo->HT_protection = pmlmeinfo->HT_info.infos[1] & 0x3;
}

void rtw_issue_addbareq_cmd(struct adapter *padapter, struct xmit_frame *pxmitframe)
{
	u8 issued;
	int priority;
	struct sta_info *psta;
	struct ht_priv *phtpriv;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	s32 bmcst = is_multicast_ether_addr(pattrib->ra);

	/* if (bmcst || (padapter->mlmepriv.LinkDetectInfo.bTxBusyTraffic == false)) */
	if (bmcst || (padapter->mlmepriv.LinkDetectInfo.NumTxOkInPeriod < 100))
		return;

	priority = pattrib->priority;

	psta = rtw_get_stainfo(&padapter->stapriv, pattrib->ra);
	if (pattrib->psta != psta)
		return;

	if (!psta)
		return;

	if (!(psta->state & _FW_LINKED))
		return;

	phtpriv = &psta->htpriv;

	if (phtpriv->ht_option && phtpriv->ampdu_enable) {
		issued = (phtpriv->agg_enable_bitmap>>priority)&0x1;
		issued |= (phtpriv->candidate_tid_bitmap>>priority)&0x1;

		if (issued == 0) {
			psta->htpriv.candidate_tid_bitmap |= BIT((u8)priority);
			rtw_addbareq_cmd(padapter, (u8) priority, pattrib->ra);
		}
	}

}

void rtw_append_exented_cap(struct adapter *padapter, u8 *out_ie, uint *pout_len)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct ht_priv 	*phtpriv = &pmlmepriv->htpriv;
	u8 cap_content[8] = {0};

	if (phtpriv->bss_coexist)
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

void rtw_roaming(struct adapter *padapter, struct wlan_network *tgt_network)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	spin_lock_bh(&pmlmepriv->lock);
	_rtw_roaming(padapter, tgt_network);
	spin_unlock_bh(&pmlmepriv->lock);
}
void _rtw_roaming(struct adapter *padapter, struct wlan_network *tgt_network)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_network *cur_network = &pmlmepriv->cur_network;

	if (rtw_to_roam(padapter) > 0) {
		memcpy(&pmlmepriv->assoc_ssid, &cur_network->network.ssid, sizeof(struct ndis_802_11_ssid));

		pmlmepriv->assoc_by_bssid = false;

		while (rtw_do_join(padapter) != _SUCCESS) {
			rtw_dec_to_roam(padapter);
			if (rtw_to_roam(padapter) <= 0) {
				rtw_indicate_disconnect(padapter);
				break;
			}
		}
	}
}

signed int rtw_linked_check(struct adapter *padapter)
{
	if ((check_fwstate(&padapter->mlmepriv, WIFI_AP_STATE) == true) ||
			(check_fwstate(&padapter->mlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE) == true)) {
		if (padapter->stapriv.asoc_sta_count > 2)
			return true;
	} else {	/* Station mode */
		if (check_fwstate(&padapter->mlmepriv, _FW_LINKED) == true)
			return true;
	}
	return false;
}
