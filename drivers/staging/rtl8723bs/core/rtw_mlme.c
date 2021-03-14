// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#define _RTW_MLME_C_

#include <linux/etherdevice.h>
#include <drv_types.h>
#include <rtw_debug.h>
#include <hal_btcoex.h>
#include <linux/jiffies.h>

extern u8 rtw_do_join(struct adapter *padapter);

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
	/*  wdev->iftype = NL80211_IFTYPE_STATION */
	pmlmepriv->cur_network.network.InfrastructureMode = Ndis802_11AutoUnknown;
	pmlmepriv->scan_mode = SCAN_ACTIVE;/*  1: active, 0: pasive. Maybe someday we should rename this varable to "active_mode" (Jeff) */

	spin_lock_init(&pmlmepriv->lock);
	_rtw_init_queue(&pmlmepriv->free_bss_pool);
	_rtw_init_queue(&pmlmepriv->scanned_queue);

	set_scanned_network_val(pmlmepriv, 0);

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
		#ifdef CONFIG_LAYER2_ROAMING_ACTIVE /* FIXME */
		| RTW_ROAM_ACTIVE
		#endif
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

/*
struct	wlan_network *_rtw_dequeue_network(struct __queue *queue)
{
	_irqL irqL;

	struct wlan_network *pnetwork;

	spin_lock_bh(&queue->lock);

	if (list_empty(&queue->queue))

		pnetwork = NULL;

	else
	{
		pnetwork = container_of(get_next(&queue->queue), struct wlan_network, list);

		list_del_init(&(pnetwork->list));
	}

	spin_unlock_bh(&queue->lock);

	return pnetwork;
}
*/

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

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
		 ("rtw_alloc_network: ptr =%p\n", plist));
	pnetwork->network_type = 0;
	pnetwork->fixed = false;
	pnetwork->last_scanned = jiffies;
	pnetwork->aid = 0;
	pnetwork->join_res = 0;

	pmlmepriv->num_of_scanned++;

exit:
	spin_unlock_bh(&free_queue->lock);

	return pnetwork;
}

void _rtw_free_network(struct	mlme_priv *pmlmepriv, struct wlan_network *pnetwork, u8 isfreeall)
{
	unsigned int delta_time;
	u32 lifetime = SCANQUEUE_LIFETIME;
/* 	_irqL irqL; */
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

	pmlmepriv->num_of_scanned--;

	/* DBG_871X("_rtw_free_network:SSID =%s\n", pnetwork->network.Ssid.Ssid); */

	spin_unlock_bh(&free_queue->lock);
}

void _rtw_free_network_nolock(struct	mlme_priv *pmlmepriv, struct wlan_network *pnetwork)
{

	struct __queue *free_queue = &(pmlmepriv->free_bss_pool);

	if (!pnetwork)
		return;

	if (pnetwork->fixed)
		return;

	/* spin_lock_irqsave(&free_queue->lock, irqL); */

	list_del_init(&(pnetwork->list));

	list_add_tail(&(pnetwork->list), get_list_head(free_queue));

	pmlmepriv->num_of_scanned--;

	/* spin_unlock_irqrestore(&free_queue->lock, irqL); */
}

/*
	return the wlan_network with the matching addr

	Shall be called under atomic context... to avoid possible racing condition...
*/
struct wlan_network *_rtw_find_network(struct __queue *scanned_queue, u8 *addr)
{
	struct list_head	*phead, *plist;
	struct	wlan_network *pnetwork = NULL;
	u8 zero_addr[ETH_ALEN] = {0, 0, 0, 0, 0, 0};

	if (!memcmp(zero_addr, addr, ETH_ALEN)) {
		pnetwork = NULL;
		goto exit;
	}

	/* spin_lock_bh(&scanned_queue->lock); */

	phead = get_list_head(scanned_queue);
	plist = get_next(phead);

	while (plist != phead) {
		pnetwork = container_of(plist, struct wlan_network, list);

		if (!memcmp(addr, pnetwork->network.MacAddress, ETH_ALEN))
			break;

		plist = get_next(plist);
	}

	if (plist == phead)
		pnetwork = NULL;

	/* spin_unlock_bh(&scanned_queue->lock); */

exit:
	return pnetwork;
}

void rtw_free_network_queue(struct adapter *padapter, u8 isfreeall)
{
	struct list_head *phead, *plist;
	struct wlan_network *pnetwork;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct __queue *scanned_queue = &pmlmepriv->scanned_queue;

	spin_lock_bh(&scanned_queue->lock);

	phead = get_list_head(scanned_queue);
	plist = get_next(phead);

	while (phead != plist) {

		pnetwork = container_of(plist, struct wlan_network, list);

		plist = get_next(plist);

		_rtw_free_network(pmlmepriv, pnetwork, isfreeall);

	}

	spin_unlock_bh(&scanned_queue->lock);
}

signed int rtw_if_up(struct adapter *padapter)
{
	signed int res;

	if (padapter->bDriverStopped || padapter->bSurpriseRemoved ||
		(check_fwstate(&padapter->mlmepriv, _FW_LINKED) == false)) {
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("rtw_if_up:bDriverStopped(%d) OR bSurpriseRemoved(%d)", padapter->bDriverStopped, padapter->bSurpriseRemoved));
		res = false;
	} else
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

	memcpy((u8 *)&val, rtw_get_capability_from_ie(bss->IEs), 2);

	return le16_to_cpu(val);
}

u8 *rtw_get_beacon_interval_from_ie(u8 *ie)
{
	return ie + 8;
}

void rtw_free_mlme_priv(struct mlme_priv *pmlmepriv)
{
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("rtw_free_mlme_priv\n"));
	_rtw_free_mlme_priv(pmlmepriv);
}

/*
static struct	wlan_network *rtw_dequeue_network(struct __queue *queue)
{
	struct wlan_network *pnetwork;

	pnetwork = _rtw_dequeue_network(queue);
	return pnetwork;
}
*/

void rtw_free_network_nolock(struct adapter *padapter, struct wlan_network *pnetwork);
void rtw_free_network_nolock(struct adapter *padapter, struct wlan_network *pnetwork)
{
	/* RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("rtw_free_network ==> ssid = %s\n\n" , pnetwork->network.Ssid.Ssid)); */
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
		    (pnetwork->network.Privacy == 0))
		ret = false;
	else if ((psecuritypriv->dot11PrivacyAlgrthm == _NO_PRIVACY_) &&
		 (pnetwork->network.Privacy == 1))
		ret = false;
	else
		ret = true;

	return ret;

}

inline int is_same_ess(struct wlan_bssid_ex *a, struct wlan_bssid_ex *b)
{
	/* RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("(%s,%d)(%s,%d)\n", */
	/* 		a->Ssid.Ssid, a->Ssid.SsidLength, b->Ssid.Ssid, b->Ssid.SsidLength)); */
	return (a->Ssid.SsidLength == b->Ssid.SsidLength)
		&&  !memcmp(a->Ssid.Ssid, b->Ssid.Ssid, a->Ssid.SsidLength);
}

int is_same_network(struct wlan_bssid_ex *src, struct wlan_bssid_ex *dst, u8 feature)
{
	u16 s_cap, d_cap;
	__le16 tmps, tmpd;

	if (rtw_bug_check(dst, src, &s_cap, &d_cap) == false)
			return false;

	memcpy((u8 *)&tmps, rtw_get_capability_from_ie(src->IEs), 2);
	memcpy((u8 *)&tmpd, rtw_get_capability_from_ie(dst->IEs), 2);

	s_cap = le16_to_cpu(tmps);
	d_cap = le16_to_cpu(tmpd);

	return (src->Ssid.SsidLength == dst->Ssid.SsidLength) &&
		/* 	(src->Configuration.DSConfig == dst->Configuration.DSConfig) && */
			((!memcmp(src->MacAddress, dst->MacAddress, ETH_ALEN))) &&
			((!memcmp(src->Ssid.Ssid, dst->Ssid.Ssid, src->Ssid.SsidLength))) &&
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
	plist = get_next(phead);

	while (plist != phead) {
		found = container_of(plist, struct wlan_network, list);

		if (is_same_network(&network->network, &found->network, 0))
			break;

		plist = get_next(plist);
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

	plist = get_next(phead);

	while (1) {

		if (phead == plist)
			break;

		pwlan = container_of(plist, struct wlan_network, list);

		if (!pwlan->fixed) {
			if (oldest == NULL || time_after(oldest->last_scanned, pwlan->last_scanned))
				oldest = pwlan;
		}

		plist = get_next(plist);
	}
	return oldest;

}

void update_network(struct wlan_bssid_ex *dst, struct wlan_bssid_ex *src,
	struct adapter *padapter, bool update_ie)
{
	long rssi_ori = dst->Rssi;

	u8 sq_smp = src->PhyInfo.SignalQuality;

	u8 ss_final;
	u8 sq_final;
	long rssi_final;

	#if defined(DBG_RX_SIGNAL_DISPLAY_SSID_MONITORED) && 1
	if (strcmp(dst->Ssid.Ssid, DBG_RX_SIGNAL_DISPLAY_SSID_MONITORED) == 0) {
		DBG_871X(FUNC_ADPT_FMT" %s(%pM, ch%u) ss_ori:%3u, sq_ori:%3u, rssi_ori:%3ld, ss_smp:%3u, sq_smp:%3u, rssi_smp:%3ld\n"
			, FUNC_ADPT_ARG(padapter)
			, src->Ssid.Ssid, MAC_ARG(src->MacAddress), src->Configuration.DSConfig
			, ss_ori, sq_ori, rssi_ori
			, ss_smp, sq_smp, rssi_smp
		);
	}
	#endif

	/* The rule below is 1/5 for sample value, 4/5 for history value */
	if (check_fwstate(&padapter->mlmepriv, _FW_LINKED) && is_same_network(&(padapter->mlmepriv.cur_network.network), src, 0)) {
		/* Take the recvpriv's value for the connected AP*/
		ss_final = padapter->recvpriv.signal_strength;
		sq_final = padapter->recvpriv.signal_qual;
		/* the rssi value here is undecorated, and will be used for antenna diversity */
		if (sq_smp != 101) /* from the right channel */
			rssi_final = (src->Rssi+dst->Rssi*4)/5;
		else
			rssi_final = rssi_ori;
	} else {
		if (sq_smp != 101) { /* from the right channel */
			ss_final = ((u32)(src->PhyInfo.SignalStrength)+(u32)(dst->PhyInfo.SignalStrength)*4)/5;
			sq_final = ((u32)(src->PhyInfo.SignalQuality)+(u32)(dst->PhyInfo.SignalQuality)*4)/5;
			rssi_final = (src->Rssi+dst->Rssi*4)/5;
		} else {
			/* bss info not receiving from the right channel, use the original RX signal infos */
			ss_final = dst->PhyInfo.SignalStrength;
			sq_final = dst->PhyInfo.SignalQuality;
			rssi_final = dst->Rssi;
		}

	}

	if (update_ie) {
		dst->Reserved[0] = src->Reserved[0];
		dst->Reserved[1] = src->Reserved[1];
		memcpy((u8 *)dst, (u8 *)src, get_wlan_bssid_ex_sz(src));
	}

	dst->PhyInfo.SignalStrength = ss_final;
	dst->PhyInfo.SignalQuality = sq_final;
	dst->Rssi = rssi_final;

	#if defined(DBG_RX_SIGNAL_DISPLAY_SSID_MONITORED) && 1
	if (strcmp(dst->Ssid.Ssid, DBG_RX_SIGNAL_DISPLAY_SSID_MONITORED) == 0) {
		DBG_871X(FUNC_ADPT_FMT" %s(%pM), SignalStrength:%u, SignalQuality:%u, RawRSSI:%ld\n"
			, FUNC_ADPT_ARG(padapter)
			, dst->Ssid.Ssid, MAC_ARG(dst->MacAddress), dst->PhyInfo.SignalStrength, dst->PhyInfo.SignalQuality, dst->Rssi);
	}
	#endif
}

static void update_current_network(struct adapter *adapter, struct wlan_bssid_ex *pnetwork)
{
	struct	mlme_priv *pmlmepriv = &(adapter->mlmepriv);

	rtw_bug_check(&(pmlmepriv->cur_network.network),
		&(pmlmepriv->cur_network.network),
		&(pmlmepriv->cur_network.network),
		&(pmlmepriv->cur_network.network));

	if ((check_fwstate(pmlmepriv, _FW_LINKED) == true) && (is_same_network(&(pmlmepriv->cur_network.network), pnetwork, 0))) {
		/* RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,"Same Network\n"); */

		/* if (pmlmepriv->cur_network.network.IELength<= pnetwork->IELength) */
		{
			update_network(&(pmlmepriv->cur_network.network), pnetwork, adapter, true);
			rtw_update_protection(adapter, (pmlmepriv->cur_network.network.IEs) + sizeof(struct ndis_802_11_fix_ie),
									pmlmepriv->cur_network.network.IELength);
		}
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
	plist = get_next(phead);

	while (1) {
		if (phead == plist)
			break;

		pnetwork = container_of(plist, struct wlan_network, list);

		rtw_bug_check(pnetwork, pnetwork, pnetwork, pnetwork);

		if (is_same_network(&(pnetwork->network), target, feature)) {
			target_find = 1;
			break;
		}

		if (rtw_roam_flags(adapter)) {
			/* TODO: don't  select netowrk in the same ess as oldest if it's new enough*/
		}

		if (oldest == NULL || time_after(oldest->last_scanned, pnetwork->last_scanned))
			oldest = pnetwork;

		plist = get_next(plist);

	}

	/* If we didn't find a match, then get a new network slot to initialize
	 * with this beacon's information */
	/* if (phead == plist) { */
	if (!target_find) {
		if (list_empty(&pmlmepriv->free_bss_pool.queue)) {
			/* If there are no more slots, expire the oldest */
			/* list_del_init(&oldest->list); */
			pnetwork = oldest;
			if (!pnetwork) {
				RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("\n\n\nsomething wrong here\n\n\n"));
				goto exit;
			}
			memcpy(&(pnetwork->network), target,  get_wlan_bssid_ex_sz(target));
			/*  variable initialize */
			pnetwork->fixed = false;
			pnetwork->last_scanned = jiffies;

			pnetwork->network_type = 0;
			pnetwork->aid = 0;
			pnetwork->join_res = 0;

			/* bss info not receiving from the right channel */
			if (pnetwork->network.PhyInfo.SignalQuality == 101)
				pnetwork->network.PhyInfo.SignalQuality = 0;
		} else {
			/* Otherwise just pull from the free list */

			pnetwork = rtw_alloc_network(pmlmepriv); /*  will update scan_time */

			if (!pnetwork) {
				RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("\n\n\nsomething wrong here\n\n\n"));
				goto exit;
			}

			bssid_ex_sz = get_wlan_bssid_ex_sz(target);
			target->Length = bssid_ex_sz;
			memcpy(&(pnetwork->network), target, bssid_ex_sz);

			pnetwork->last_scanned = jiffies;

			/* bss info not receiving from the right channel */
			if (pnetwork->network.PhyInfo.SignalQuality == 101)
				pnetwork->network.PhyInfo.SignalQuality = 0;

			list_add_tail(&(pnetwork->list), &(queue->queue));

		}
	} else {
		/* we have an entry and we are going to update it. But this entry may
		 * be already expired. In this case we do the same as we found a new
		 * net and call the new_net handler
		 */
		bool update_ie = true;

		pnetwork->last_scanned = jiffies;

		/* target.Reserved[0]== 1, means that scanned network is a bcn frame. */
		if ((pnetwork->network.IELength > target->IELength) && (target->Reserved[0] == 1))
			update_ie = false;

		/*  probe resp(3) > beacon(1) > probe req(2) */
		if ((target->Reserved[0] != 2) &&
			(target->Reserved[0] >= pnetwork->network.Reserved[0])
			) {
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
	/* struct __queue	*queue	= &(pmlmepriv->scanned_queue); */

	/* spin_lock_bh(&queue->lock); */

	update_current_network(adapter, pnetwork);

	rtw_update_scanned_network(adapter, pnetwork);

	/* spin_unlock_bh(&queue->lock); */
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

	/* u8 wps_ie[512]; */
	uint wps_ielen;

	int bselected = true;

	desired_encmode = psecuritypriv->ndisencryptstatus;
	privacy = pnetwork->network.Privacy;

	if (check_fwstate(pmlmepriv, WIFI_UNDER_WPS)) {
		if (rtw_get_wps_ie(pnetwork->network.IEs+_FIXED_IE_LENGTH_, pnetwork->network.IELength-_FIXED_IE_LENGTH_, NULL, &wps_ielen))
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
			p = rtw_get_ie(pnetwork->network.IEs + _BEACON_IE_OFFSET_, WLAN_EID_RSN, &ie_len, (pnetwork->network.IELength - _BEACON_IE_OFFSET_));
			if (p && ie_len > 0)
				bselected = true;
			else
				bselected = false;
		}
	}

	if ((desired_encmode != Ndis802_11EncryptionDisabled) && (privacy == 0)) {
		DBG_871X("desired_encmode: %d, privacy: %d\n", desired_encmode, privacy);
		bselected = false;
	}

	if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == true) {
		if (pnetwork->network.InfrastructureMode != pmlmepriv->cur_network.network.InfrastructureMode)
			bselected = false;
	}

	return bselected;
}

/* TODO: Perry : For Power Management */
void rtw_atimdone_event_callback(struct adapter	*adapter, u8 *pbuf)
{
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("receive atimdone_event\n"));
}

void rtw_survey_event_callback(struct adapter	*adapter, u8 *pbuf)
{
	u32 len;
	struct wlan_bssid_ex *pnetwork;
	struct	mlme_priv *pmlmepriv = &(adapter->mlmepriv);

	pnetwork = (struct wlan_bssid_ex *)pbuf;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("rtw_survey_event_callback, ssid =%s\n",  pnetwork->Ssid.Ssid));

	len = get_wlan_bssid_ex_sz(pnetwork);
	if (len > (sizeof(struct wlan_bssid_ex))) {
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("\n ****rtw_survey_event_callback: return a wrong bss ***\n"));
		return;
	}

	spin_lock_bh(&pmlmepriv->lock);

	/*  update IBSS_network 's timestamp */
	if ((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)) == true) {
		/* RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,"rtw_survey_event_callback : WIFI_ADHOC_MASTER_STATE\n\n"); */
		if (!memcmp(&(pmlmepriv->cur_network.network.MacAddress), pnetwork->MacAddress, ETH_ALEN)) {
			struct wlan_network *ibss_wlan = NULL;

			memcpy(pmlmepriv->cur_network.network.IEs, pnetwork->IEs, 8);
			spin_lock_bh(&(pmlmepriv->scanned_queue.lock));
			ibss_wlan = rtw_find_network(&pmlmepriv->scanned_queue,  pnetwork->MacAddress);
			if (ibss_wlan) {
				memcpy(ibss_wlan->network.IEs, pnetwork->IEs, 8);
				spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));
				goto exit;
			}
			spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));
		}
	}

	/*  lock pmlmepriv->lock when you accessing network_q */
	if ((check_fwstate(pmlmepriv, _FW_UNDER_LINKING)) == false) {
		if (pnetwork->Ssid.Ssid[0] == 0)
			pnetwork->Ssid.SsidLength = 0;
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

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("rtw_surveydone_event_callback: fw_state:%x\n\n", get_fwstate(pmlmepriv)));

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY)) {
		del_timer_sync(&pmlmepriv->scan_to_timer);
		_clr_fwstate_(pmlmepriv, _FW_UNDER_SURVEY);
	} else {

		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("nic status =%x, survey done event comes too late!\n", get_fwstate(pmlmepriv)));
	}

	rtw_set_signal_stat_timer(&adapter->recvpriv);

	if (pmlmepriv->to_join) {
		if ((check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == true)) {
			if (check_fwstate(pmlmepriv, _FW_LINKED) == false) {
				set_fwstate(pmlmepriv, _FW_UNDER_LINKING);

				if (rtw_select_and_join_from_scanned_queue(pmlmepriv) == _SUCCESS) {
					_set_timer(&pmlmepriv->assoc_timer, MAX_JOIN_TIMEOUT);
				} else {
					struct wlan_bssid_ex    *pdev_network = &(adapter->registrypriv.dev_network);
					u8 *pibss = adapter->registrypriv.dev_network.MacAddress;

					/* pmlmepriv->fw_state ^= _FW_UNDER_SURVEY;because don't set assoc_timer */
					_clr_fwstate_(pmlmepriv, _FW_UNDER_SURVEY);

					RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("switching to adhoc master\n"));

					memcpy(&pdev_network->Ssid, &pmlmepriv->assoc_ssid, sizeof(struct ndis_802_11_ssid));

					rtw_update_registrypriv_dev_network(adapter);
					rtw_generate_random_ibss(pibss);

					pmlmepriv->fw_state = WIFI_ADHOC_MASTER_STATE;

					if (rtw_createbss_cmd(adapter) != _SUCCESS)
						RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("Error =>rtw_createbss_cmd status FAIL\n"));

					pmlmepriv->to_join = false;
				}
			}
		} else {
			int s_ret;

			set_fwstate(pmlmepriv, _FW_UNDER_LINKING);
			pmlmepriv->to_join = false;
			s_ret = rtw_select_and_join_from_scanned_queue(pmlmepriv);
			if (_SUCCESS == s_ret) {
			     _set_timer(&pmlmepriv->assoc_timer, MAX_JOIN_TIMEOUT);
			} else if (s_ret == 2) {/* there is no need to wait for join */
				_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);
				rtw_indicate_connect(adapter);
			} else {
				DBG_871X("try_to_join, but select scanning queue fail, to_roam:%d\n", rtw_to_roam(adapter));

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
					receive_disconnect(adapter, pmlmepriv->cur_network.network.MacAddress
						, WLAN_REASON_ACTIVE_ROAM);
				}
			}
		}
	}

	/* DBG_871X("scan complete in %dms\n", jiffies_to_msecs(jiffies - pmlmepriv->scan_start_time)); */

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

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_, ("+free_scanqueue\n"));
	spin_lock_bh(&scan_queue->lock);
	spin_lock_bh(&free_queue->lock);

	phead = get_list_head(scan_queue);
	plist = get_next(phead);

	while (plist != phead) {
		ptemp = get_next(plist);
		list_del_init(plist);
		list_add_tail(plist, &free_queue->queue);
		plist = ptemp;
		pmlmepriv->num_of_scanned--;
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

	pwlan = rtw_find_network(&pmlmepriv->scanned_queue, tgt_network->network.MacAddress);
	if (pwlan)
		pwlan->fixed = false;
	else
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("rtw_free_assoc_resources : pwlan == NULL\n\n"));

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
	struct	sta_priv *pstapriv = &adapter->stapriv;
	struct dvobj_priv *psdpriv = adapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_, ("+rtw_free_assoc_resources\n"));
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("tgt_network->network.MacAddress =%pM ssid =%s\n",
		MAC_ARG(tgt_network->network.MacAddress), tgt_network->network.Ssid.Ssid));

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE|WIFI_AP_STATE)) {
		struct sta_info *psta;

		psta = rtw_get_stainfo(&adapter->stapriv, tgt_network->network.MacAddress);
		spin_lock_bh(&(pstapriv->sta_hash_lock));
		rtw_free_stainfo(adapter,  psta);

		spin_unlock_bh(&(pstapriv->sta_hash_lock));

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

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("+rtw_indicate_connect\n"));

	pmlmepriv->to_join = false;

	if (!check_fwstate(&padapter->mlmepriv, _FW_LINKED)) {

		set_fwstate(pmlmepriv, _FW_LINKED);

		rtw_os_indicate_connect(padapter);
	}

	rtw_set_to_roam(padapter, 0);
	rtw_set_scan_deny(padapter, 3000);

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("-rtw_indicate_connect: fw_state = 0x%08x\n", get_fwstate(pmlmepriv)));
}

/*
*rtw_indicate_disconnect: the caller has to lock pmlmepriv->lock
*/
void rtw_indicate_disconnect(struct adapter *padapter)
{
	struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("+rtw_indicate_disconnect\n"));

	_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING|WIFI_UNDER_WPS);

	/* DBG_871X("clear wps when %s\n", __func__); */

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
	DBG_871X(FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(padapter));

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

		DBG_871X(FUNC_NDEV_FMT"fw_state = _FW_UNDER_SURVEY!\n", FUNC_NDEV_ARG(adapter->pnetdev));
		msleep(20);
	}

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY)) {
		if (!adapter->bDriverStopped && !adapter->bSurpriseRemoved)
			DBG_871X(FUNC_NDEV_FMT"waiting for scan_abort time out!\n", FUNC_NDEV_ARG(adapter->pnetdev));
		rtw_indicate_scan_done(adapter, true);
	}
	pmlmeext->scan_abort = false;
}

static struct sta_info *rtw_joinbss_update_stainfo(struct adapter *padapter, struct wlan_network *pnetwork)
{
	int i;
	struct sta_info *bmc_sta, *psta = NULL;
	struct recv_reorder_ctrl *preorder_ctrl;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	psta = rtw_get_stainfo(pstapriv, pnetwork->network.MacAddress);
	if (!psta)
		psta = rtw_alloc_stainfo(pstapriv, pnetwork->network.MacAddress);

	if (psta) { /* update ptarget_sta */

		DBG_871X("%s\n", __func__);

		psta->aid  = pnetwork->join_res;

		update_sta_info(padapter, psta);

		/* update station supportRate */
		psta->bssratelen = rtw_get_rateset_len(pnetwork->network.SupportedRates);
		memcpy(psta->bssrateset, pnetwork->network.SupportedRates, psta->bssratelen);
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

		/* 	Commented by Albert 2012/07/21 */
		/* 	When doing the WPS, the wps_ie_len won't equal to 0 */
		/* 	And the Wi-Fi driver shouldn't allow the data packet to be transmitted. */
		if (padapter->securitypriv.wps_ie_len != 0) {
			psta->ieee8021x_blocked = true;
			padapter->securitypriv.wps_ie_len = 0;
		}

		/* for A-MPDU Rx reordering buffer control for bmc_sta & sta_info */
		/* if A-MPDU Rx is enabled, resetting  rx_ordering_ctrl wstart_b(indicate_seq) to default value = 0xffff */
		/* todo: check if AP can send A-MPDU packets */
		for (i = 0; i < 16 ; i++) {
			/* preorder_ctrl = &precvpriv->recvreorder_ctrl[i]; */
			preorder_ctrl = &psta->recvreorder_ctrl[i];
			preorder_ctrl->enable = false;
			preorder_ctrl->indicate_seq = 0xffff;
			#ifdef DBG_RX_SEQ
			DBG_871X("DBG_RX_SEQ %s:%d indicate_seq:%u\n", __func__, __LINE__,
				preorder_ctrl->indicate_seq);
			#endif
			preorder_ctrl->wend_b = 0xffff;
			preorder_ctrl->wsize_b = 64;/* max_ampdu_sz;ex. 32(kbytes) -> wsize_b =32 */
		}

		bmc_sta = rtw_get_bcmc_stainfo(padapter);
		if (bmc_sta) {
			for (i = 0; i < 16 ; i++) {
				/* preorder_ctrl = &precvpriv->recvreorder_ctrl[i]; */
				preorder_ctrl = &bmc_sta->recvreorder_ctrl[i];
				preorder_ctrl->enable = false;
				preorder_ctrl->indicate_seq = 0xffff;
				#ifdef DBG_RX_SEQ
				DBG_871X("DBG_RX_SEQ %s:%d indicate_seq:%u\n", __func__, __LINE__,
					preorder_ctrl->indicate_seq);
				#endif
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

	DBG_871X("%s\n", __func__);

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("\nfw_state:%x, BSSID:%pM\n"
		, get_fwstate(pmlmepriv), MAC_ARG(pnetwork->network.MacAddress)));

	/*  why not use ptarget_wlan?? */
	memcpy(&cur_network->network, &pnetwork->network, pnetwork->network.Length);
	/*  some IEs in pnetwork is wrong, so we should use ptarget_wlan IEs */
	cur_network->network.IELength = ptarget_wlan->network.IELength;
	memcpy(&cur_network->network.IEs[0], &ptarget_wlan->network.IEs[0], MAX_IE_SZ);

	cur_network->aid = pnetwork->join_res;

	rtw_set_signal_stat_timer(&padapter->recvpriv);

	padapter->recvpriv.signal_strength = ptarget_wlan->network.PhyInfo.SignalStrength;
	padapter->recvpriv.signal_qual = ptarget_wlan->network.PhyInfo.SignalQuality;
	/* the ptarget_wlan->network.Rssi is raw data, we use ptarget_wlan->network.PhyInfo.SignalStrength instead (has scaled) */
	padapter->recvpriv.rssi = translate_percentage_to_dbm(ptarget_wlan->network.PhyInfo.SignalStrength);
	#if defined(DBG_RX_SIGNAL_DISPLAY_PROCESSING) && 1
		DBG_871X(FUNC_ADPT_FMT" signal_strength:%3u, rssi:%3d, signal_qual:%3u"
			"\n"
			, FUNC_ADPT_ARG(padapter)
			, padapter->recvpriv.signal_strength
			, padapter->recvpriv.rssi
			, padapter->recvpriv.signal_qual
	);
	#endif

	rtw_set_signal_stat_timer(&padapter->recvpriv);

	/* update fw_state will clr _FW_UNDER_LINKING here indirectly */
	switch (pnetwork->network.InfrastructureMode) {
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
			RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("Invalid network_mode\n"));
			break;
	}

	rtw_update_protection(padapter, (cur_network->network.IEs) + sizeof(struct ndis_802_11_fix_ie),
									(cur_network->network.IELength));

	rtw_update_ht_cap(padapter, cur_network->network.IEs, cur_network->network.IELength, (u8) cur_network->network.Configuration.DSConfig);
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
	static u8 retry;
	struct sta_info *ptarget_sta = NULL, *pcur_sta = NULL;
	struct	sta_priv *pstapriv = &adapter->stapriv;
	struct	mlme_priv *pmlmepriv = &(adapter->mlmepriv);
	struct wlan_network	*pnetwork	= (struct wlan_network *)pbuf;
	struct wlan_network	*cur_network = &(pmlmepriv->cur_network);
	struct wlan_network	*pcur_wlan = NULL, *ptarget_wlan = NULL;
	unsigned int		the_same_macaddr = false;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("joinbss event call back received with res =%d\n", pnetwork->join_res));

	rtw_get_encrypt_decrypt_from_registrypriv(adapter);

	if (pmlmepriv->assoc_ssid.SsidLength == 0)
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("@@@@@   joinbss event call back  for Any SSid\n"));
	else
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("@@@@@   rtw_joinbss_event_callback for SSid:%s\n", pmlmepriv->assoc_ssid.Ssid));

	the_same_macaddr = !memcmp(pnetwork->network.MacAddress, cur_network->network.MacAddress, ETH_ALEN);

	pnetwork->network.Length = get_wlan_bssid_ex_sz(&pnetwork->network);
	if (pnetwork->network.Length > sizeof(struct wlan_bssid_ex)) {
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("\n\n ***joinbss_evt_callback return a wrong bss ***\n\n"));
		return;
	}

	spin_lock_bh(&pmlmepriv->lock);

	pmlmepriv->LinkDetectInfo.TrafficTransitionCount = 0;
	pmlmepriv->LinkDetectInfo.LowPowerTransitionCount = 0;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("\n rtw_joinbss_event_callback !! spin_lock_irqsave\n"));

	if (pnetwork->join_res > 0) {
		spin_lock_bh(&(pmlmepriv->scanned_queue.lock));
		retry = 0;
		if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING)) {
			/* s1. find ptarget_wlan */
			if (check_fwstate(pmlmepriv, _FW_LINKED)) {
				if (the_same_macaddr) {
					ptarget_wlan = rtw_find_network(&pmlmepriv->scanned_queue, cur_network->network.MacAddress);
				} else {
					pcur_wlan = rtw_find_network(&pmlmepriv->scanned_queue, cur_network->network.MacAddress);
					if (pcur_wlan)
						pcur_wlan->fixed = false;

					pcur_sta = rtw_get_stainfo(pstapriv, cur_network->network.MacAddress);
					if (pcur_sta)
						rtw_free_stainfo(adapter,  pcur_sta);

					ptarget_wlan = rtw_find_network(&pmlmepriv->scanned_queue, pnetwork->network.MacAddress);
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
				DBG_871X_LEVEL(_drv_always_, "Can't find ptarget_wlan when joinbss_event callback\n");
				spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));
				goto ignore_joinbss_callback;
			}

			/* s3. find ptarget_sta & update ptarget_sta after update cur_network only for station mode */
			if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == true) {
				ptarget_sta = rtw_joinbss_update_stainfo(adapter, pnetwork);
				if (!ptarget_sta) {
					RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("Can't update stainfo when joinbss_event callback\n"));
					spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));
					goto ignore_joinbss_callback;
				}
			}

			/* s4. indicate connect */
			if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == true) {
				pmlmepriv->cur_network_scanned = ptarget_wlan;
				rtw_indicate_connect(adapter);
			} else {
				/* adhoc mode will rtw_indicate_connect when rtw_stassoc_event_callback */
				RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("adhoc mode, fw_state:%x", get_fwstate(pmlmepriv)));
			}

			/* s5. Cancel assoc_timer */
			del_timer_sync(&pmlmepriv->assoc_timer);

			RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("Cancel assoc_timer\n"));

		} else {
			RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("rtw_joinbss_event_callback err: fw_state:%x", get_fwstate(pmlmepriv)));
			spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));
			goto ignore_joinbss_callback;
		}

		spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));

	} else if (pnetwork->join_res == -4) {
		rtw_reset_securitypriv(adapter);
		_set_timer(&pmlmepriv->assoc_timer, 1);

		/* rtw_free_assoc_resources(adapter, 1); */

		if ((check_fwstate(pmlmepriv, _FW_UNDER_LINKING)) == true) {
			RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("fail! clear _FW_UNDER_LINKING ^^^fw_state =%x\n", get_fwstate(pmlmepriv)));
			_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);
		}

	} else {/* if join_res < 0 (join fails), then try again */

		#ifdef REJOIN
		res = _FAIL;
		if (retry < 2) {
			res = rtw_select_and_join_from_scanned_queue(pmlmepriv);
			RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("rtw_select_and_join_from_scanned_queue again! res:%d\n", res));
		}

		if (res == _SUCCESS) {
			/* extend time of assoc_timer */
			_set_timer(&pmlmepriv->assoc_timer, MAX_JOIN_TIMEOUT);
			retry++;
		} else if (res == 2) {/* there is no need to wait for join */
			_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);
			rtw_indicate_connect(adapter);
		} else {
			RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("Set Assoc_Timer = 1; can't find match ssid in scanned_q\n"));
		#endif

			_set_timer(&pmlmepriv->assoc_timer, 1);
			/* rtw_free_assoc_resources(adapter, 1); */
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

#ifndef CONFIG_AUTO_AP_MODE

			ap_sta_info_defer_update(adapter, psta);

			/* report to upper layer */
			DBG_871X("indicate_sta_assoc_event to upper layer - hostapd\n");
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
#endif /* CONFIG_AUTO_AP_MODE */
		}
		return;
	}

	/* for AD-HOC mode */
	psta = rtw_get_stainfo(&adapter->stapriv, pstassoc->macaddr);
	if (psta) {
		/* the sta have been in sta_info_queue => do nothing */

		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("Error: rtw_stassoc_event_callback: sta has been in sta_hash_queue\n"));

		return; /* between drv has received this event before and  fw have not yet to set key to CAM_ENTRY) */
	}

	psta = rtw_alloc_stainfo(&adapter->stapriv, pstassoc->macaddr);
	if (!psta) {
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("Can't alloc sta_info when rtw_stassoc_event_callback\n"));
		return;
	}

	/* to do : init sta_info variable */
	psta->qos_option = 0;
	psta->mac_id = (uint)pstassoc->cam_id;
	/* psta->aid = (uint)pstassoc->cam_id; */
	DBG_871X("%s\n", __func__);
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
			ptarget_wlan = rtw_find_network(&pmlmepriv->scanned_queue, cur_network->network.MacAddress);
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

	DBG_871X("%s(mac_id =%d) =%pM\n", __func__, mac_id, MAC_ARG(pstadel->macaddr));

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
		pwlan = rtw_find_network(&pmlmepriv->scanned_queue, tgt_network->network.MacAddress);
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
			/* rtw_indicate_disconnect(adapter);removed@20091105 */
			spin_lock_bh(&(pmlmepriv->scanned_queue.lock));
			/* free old ibss network */
			/* pwlan = rtw_find_network(&pmlmepriv->scanned_queue, pstadel->macaddr); */
			pwlan = rtw_find_network(&pmlmepriv->scanned_queue, tgt_network->network.MacAddress);
			if (pwlan) {
				pwlan->fixed = false;
				rtw_free_network_nolock(adapter, pwlan);
			}
			spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));
			/* re-create ibss */
			pdev_network = &(adapter->registrypriv.dev_network);
			pibss = adapter->registrypriv.dev_network.MacAddress;

			memcpy(pdev_network, &tgt_network->network, get_wlan_bssid_ex_sz(&tgt_network->network));

			memcpy(&pdev_network->Ssid, &pmlmepriv->assoc_ssid, sizeof(struct ndis_802_11_ssid));

			rtw_update_registrypriv_dev_network(adapter);

			rtw_generate_random_ibss(pibss);

			if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)) {
				set_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE);
				_clr_fwstate_(pmlmepriv, WIFI_ADHOC_STATE);
			}

			if (rtw_createbss_cmd(adapter) != _SUCCESS)
				RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_err_, ("***Error =>stadel_event_callback: rtw_createbss_cmd status FAIL***\n "));
		}

	}

	spin_unlock_bh(&pmlmepriv->lock);
}

void rtw_cpwm_event_callback(struct adapter *padapter, u8 *pbuf)
{
	struct reportpwrstate_parm *preportpwrstate;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("+rtw_cpwm_event_callback !!!\n"));
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

	DBG_871X("%s, fw_state =%x\n", __func__, get_fwstate(pmlmepriv));

	if (adapter->bDriverStopped || adapter->bSurpriseRemoved)
		return;

	spin_lock_bh(&pmlmepriv->lock);

	if (rtw_to_roam(adapter) > 0) { /* join timeout caused by roaming */
		while (1) {
			rtw_dec_to_roam(adapter);
			if (rtw_to_roam(adapter) != 0) { /* try another */
				int do_join_r;

				DBG_871X("%s try another roaming\n", __func__);
				do_join_r = rtw_do_join(adapter);
				if (_SUCCESS != do_join_r) {
					DBG_871X("%s roaming do_join return %d\n", __func__, do_join_r);
					continue;
				}
				break;
			} else {
				DBG_871X("%s We've try roaming but fail\n", __func__);
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

	DBG_871X(FUNC_ADPT_FMT" fw_state =%x\n", FUNC_ADPT_ARG(adapter), get_fwstate(pmlmepriv));

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
			if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY|_FW_UNDER_LINKING) == true) {
				DBG_871X(FUNC_ADPT_FMT" _FW_UNDER_SURVEY|_FW_UNDER_LINKING\n", FUNC_ADPT_ARG(padapter));
				goto exit;
			}

			if (pmlmepriv->LinkDetectInfo.bBusyTraffic) {
				DBG_871X(FUNC_ADPT_FMT" exit BusyTraffic\n", FUNC_ADPT_ARG(padapter));
				goto exit;
			}
		}

		DBG_871X(FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(padapter));

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

	if (is_primary_adapter(adapter))
		DBG_871X("IsBtDisabled =%d, IsBtControlLps =%d\n", hal_btcoex_IsBtDisabled(adapter), hal_btcoex_IsBtControlLps(adapter));

	if ((adapter_to_pwrctl(adapter)->bFwCurrentInPSMode)
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

	DBG_871X(FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(adapter));
}

void rtw_set_scan_deny(struct adapter *adapter, u32 ms)
{
	struct mlme_priv *mlmepriv = &adapter->mlmepriv;

	DBG_871X(FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(adapter));
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

	DBG_871X("roam candidate:%s %s(%pM, ch%3u) rssi:%d, age:%5d\n",
		(competitor == mlme->cur_network_scanned)?"*":" ",
		competitor->network.Ssid.Ssid,
		MAC_ARG(competitor->network.MacAddress),
		competitor->network.Configuration.DSConfig,
		(int)competitor->network.Rssi,
		jiffies_to_msecs(jiffies - competitor->last_scanned)
	);

	/* got specific addr to roam */
	if (!is_zero_mac_addr(mlme->roam_tgt_addr)) {
		if (!memcmp(mlme->roam_tgt_addr, competitor->network.MacAddress, ETH_ALEN))
			goto update;
		else
			goto exit;
	}
	if (jiffies_to_msecs(jiffies - competitor->last_scanned) >= mlme->roam_scanr_exp_ms)
		goto exit;

	if (competitor->network.Rssi - mlme->cur_network_scanned->network.Rssi < mlme->roam_rssi_diff_th)
		goto exit;

	if (*candidate && (*candidate)->network.Rssi >= competitor->network.Rssi)
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

	mlme->pscanned = get_next(phead);

	while (phead != mlme->pscanned) {

		pnetwork = container_of(mlme->pscanned, struct wlan_network, list);
		if (!pnetwork) {
			RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("%s return _FAIL:(pnetwork == NULL)\n", __func__));
			ret = _FAIL;
			goto exit;
		}

		mlme->pscanned = get_next(mlme->pscanned);

		DBG_871X("%s(%pM, ch%u) rssi:%d\n"
			, pnetwork->network.Ssid.Ssid
			, MAC_ARG(pnetwork->network.MacAddress)
			, pnetwork->network.Configuration.DSConfig
			, (int)pnetwork->network.Rssi);

		rtw_check_roaming_candidate(mlme, &candidate, pnetwork);

	}

	if (!candidate) {
		DBG_871X("%s: return _FAIL(candidate == NULL)\n", __func__);
		ret = _FAIL;
		goto exit;
	} else {
		DBG_871X("%s: candidate: %s(%pM, ch:%u)\n", __func__,
			candidate->network.Ssid.Ssid, MAC_ARG(candidate->network.MacAddress),
			candidate->network.Configuration.DSConfig);

		mlme->roam_network = candidate;

		if (!memcmp(candidate->network.MacAddress, mlme->roam_tgt_addr, ETH_ALEN))
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
		if (memcmp(competitor->network.MacAddress, mlme->assoc_bssid, ETH_ALEN))
			goto exit;
	}

	/* check ssid, if needed */
	if (mlme->assoc_ssid.Ssid[0] && mlme->assoc_ssid.SsidLength) {
		if (competitor->network.Ssid.SsidLength != mlme->assoc_ssid.SsidLength
			|| memcmp(competitor->network.Ssid.Ssid, mlme->assoc_ssid.Ssid, mlme->assoc_ssid.SsidLength)
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

	if (*candidate == NULL || (*candidate)->network.Rssi < competitor->network.Rssi) {
		*candidate = competitor;
		updated = true;
	}

	if (updated) {
		DBG_871X("[by_bssid:%u][assoc_ssid:%s]"
			"[to_roam:%u] "
			"new candidate: %s(%pM, ch%u) rssi:%d\n",
			mlme->assoc_by_bssid,
			mlme->assoc_ssid.Ssid,
			rtw_to_roam(adapter),
			(*candidate)->network.Ssid.Ssid,
			MAC_ARG((*candidate)->network.MacAddress),
			(*candidate)->network.Configuration.DSConfig,
			(int)(*candidate)->network.Rssi
		);
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
	pmlmepriv->pscanned = get_next(phead);

	while (phead != pmlmepriv->pscanned) {

		pnetwork = container_of(pmlmepriv->pscanned, struct wlan_network, list);
		if (!pnetwork) {
			RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("%s return _FAIL:(pnetwork == NULL)\n", __func__));
			ret = _FAIL;
			goto exit;
		}

		pmlmepriv->pscanned = get_next(pmlmepriv->pscanned);

		DBG_871X("%s(%pM, ch%u) rssi:%d\n"
			, pnetwork->network.Ssid.Ssid
			, MAC_ARG(pnetwork->network.MacAddress)
			, pnetwork->network.Configuration.DSConfig
			, (int)pnetwork->network.Rssi);

		rtw_check_join_candidate(pmlmepriv, &candidate, pnetwork);

	}

	if (!candidate) {
		DBG_871X("%s: return _FAIL(candidate == NULL)\n", __func__);
		ret = _FAIL;
		goto exit;
	} else {
		DBG_871X("%s: candidate: %s(%pM, ch:%u)\n", __func__,
			candidate->network.Ssid.Ssid, MAC_ARG(candidate->network.MacAddress),
			candidate->network.Configuration.DSConfig);
		goto candidate_exist;
	}

candidate_exist:

	/*  check for situation of  _FW_LINKED */
	if (check_fwstate(pmlmepriv, _FW_LINKED) == true) {
		DBG_871X("%s: _FW_LINKED while ask_for_joinbss!!!\n", __func__);

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

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("after enqueue set_auth_cmd, auth_mode =%x\n", psecuritypriv->dot11AuthAlgrthm));

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

	if (psecuritypriv->dot11AuthAlgrthm == dot11AuthAlgrthm_8021X) {
		psetkeyparm->algorithm = (unsigned char)psecuritypriv->dot118021XGrpPrivacy;
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("\n rtw_set_key: psetkeyparm->algorithm =(unsigned char)psecuritypriv->dot118021XGrpPrivacy =%d\n", psetkeyparm->algorithm));
	} else {
		psetkeyparm->algorithm = (u8)psecuritypriv->dot11PrivacyAlgrthm;
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("\n rtw_set_key: psetkeyparm->algorithm =(u8)psecuritypriv->dot11PrivacyAlgrthm =%d\n", psetkeyparm->algorithm));

	}
	psetkeyparm->keyid = (u8)keyid;/* 0~3 */
	psetkeyparm->set_tx = set_tx;
	if (is_wep_enc(psetkeyparm->algorithm))
		adapter->securitypriv.key_mask |= BIT(psetkeyparm->keyid);

	DBG_871X("==> rtw_set_key algorithm(%x), keyid(%x), key_mask(%x)\n", psetkeyparm->algorithm, psetkeyparm->keyid, adapter->securitypriv.key_mask);
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("\n rtw_set_key: psetkeyparm->algorithm =%d psetkeyparm->keyid =(u8)keyid =%d\n", psetkeyparm->algorithm, keyid));

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
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("\n rtw_set_key:psecuritypriv->dot11PrivacyAlgrthm = %x (must be 1 or 2 or 4 or 5)\n", psecuritypriv->dot11PrivacyAlgrthm));
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

/* adjust IEs for rtw_joinbss_cmd in WMM */
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
	struct security_priv *psecuritypriv = &Adapter->securitypriv;
	int i = 0;

	do {
		if ((psecuritypriv->PMKIDList[i].bUsed) &&
				(!memcmp(psecuritypriv->PMKIDList[i].Bssid, bssid, ETH_ALEN))) {
			break;
		} else {
			i++;
			/* continue; */
		}

	} while (i < NUM_PMKID_CACHE);

	if (i == NUM_PMKID_CACHE) {
		i = -1;/*  Could not find. */
	} else {
		/*  There is one Pre-Authentication Key for the specific BSSID. */
	}

	return i;

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

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_,
		 ("+rtw_restruct_sec_ie: ndisauthmode =%d\n", ndisauthmode));

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
		/* debug for CONFIG_IEEE80211W
		{
			int jj;
			printk("supplicant_ie_length =%d &&&&&&&&&&&&&&&&&&&\n", psecuritypriv->supplicant_ie[1]+2);
			for (jj = 0; jj < psecuritypriv->supplicant_ie[1]+2; jj++)
				printk(" %02x ", psecuritypriv->supplicant_ie[jj]);
			printk("\n");
		}*/
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

	memcpy(pdev_network->MacAddress, myhwaddr, ETH_ALEN);

	memcpy(&pdev_network->Ssid, &pregistrypriv->ssid, sizeof(struct ndis_802_11_ssid));

	pdev_network->Configuration.Length = sizeof(struct ndis_802_11_conf);
	pdev_network->Configuration.BeaconPeriod = 100;
	pdev_network->Configuration.FHConfig.Length = 0;
	pdev_network->Configuration.FHConfig.HopPattern = 0;
	pdev_network->Configuration.FHConfig.HopSet = 0;
	pdev_network->Configuration.FHConfig.DwellTime = 0;
}

void rtw_update_registrypriv_dev_network(struct adapter *adapter)
{
	int sz = 0;
	struct registry_priv *pregistrypriv = &adapter->registrypriv;
	struct wlan_bssid_ex    *pdev_network = &pregistrypriv->dev_network;
	struct	security_priv *psecuritypriv = &adapter->securitypriv;
	struct	wlan_network	*cur_network = &adapter->mlmepriv.cur_network;
	/* struct	xmit_priv *pxmitpriv = &adapter->xmitpriv; */

	pdev_network->Privacy = (psecuritypriv->dot11PrivacyAlgrthm > 0 ? 1 : 0) ; /*  adhoc no 802.1x */

	pdev_network->Rssi = 0;

	switch (pregistrypriv->wireless_mode) {
	case WIRELESS_11B:
		pdev_network->NetworkTypeInUse = (Ndis802_11DS);
		break;
	case WIRELESS_11G:
	case WIRELESS_11BG:
	case WIRELESS_11_24N:
	case WIRELESS_11G_24N:
	case WIRELESS_11BG_24N:
		pdev_network->NetworkTypeInUse = (Ndis802_11OFDM24);
		break;
	case WIRELESS_11A:
	case WIRELESS_11A_5N:
		pdev_network->NetworkTypeInUse = (Ndis802_11OFDM5);
		break;
	case WIRELESS_11ABGN:
		if (pregistrypriv->channel > 14)
			pdev_network->NetworkTypeInUse = (Ndis802_11OFDM5);
		else
			pdev_network->NetworkTypeInUse = (Ndis802_11OFDM24);
		break;
	default:
		/*  TODO */
		break;
	}

	pdev_network->Configuration.DSConfig = (pregistrypriv->channel);
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("pregistrypriv->channel =%d, pdev_network->Configuration.DSConfig = 0x%x\n", pregistrypriv->channel, pdev_network->Configuration.DSConfig));

	if (cur_network->network.InfrastructureMode == Ndis802_11IBSS)
		pdev_network->Configuration.ATIMWindow = (0);

	pdev_network->InfrastructureMode = (cur_network->network.InfrastructureMode);

	/*  1. Supported rates */
	/*  2. IE */

	/* rtw_set_supported_rate(pdev_network->SupportedRates, pregistrypriv->wireless_mode) ;  will be called in rtw_generate_ie */
	sz = rtw_generate_ie(pregistrypriv);

	pdev_network->IELength = sz;

	pdev_network->Length = get_wlan_bssid_ex_sz((struct wlan_bssid_ex  *)pdev_network);

	/* notes: translate IELength & Length after assign the Length to cmdsz in createbss_cmd(); */
	/* pdev_network->IELength = cpu_to_le32(sz); */
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
	if (phtpriv->ldpc_cap)
		DBG_871X("[HT] Support LDPC = 0x%02X\n", phtpriv->ldpc_cap);

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
	if (phtpriv->stbc_cap)
		DBG_871X("[HT] Support STBC = 0x%02X\n", phtpriv->stbc_cap);

	/*  Beamforming setting */
	rtw_hal_get_def_var(padapter, HAL_DEF_EXPLICIT_BEAMFORMER, (u8 *)&bHwSupportBeamformer);
	rtw_hal_get_def_var(padapter, HAL_DEF_EXPLICIT_BEAMFORMEE, (u8 *)&bHwSupportBeamformee);
	CLEAR_FLAGS(phtpriv->beamform_cap);
	if (TEST_FLAG(pregistrypriv->beamform_cap, BIT4) && bHwSupportBeamformer) {
		SET_FLAG(phtpriv->beamform_cap, BEAMFORMING_HT_BEAMFORMER_ENABLE);
		DBG_871X("[HT] Support Beamformer\n");
	}
	if (TEST_FLAG(pregistrypriv->beamform_cap, BIT5) && bHwSupportBeamformee) {
		SET_FLAG(phtpriv->beamform_cap, BEAMFORMING_HT_BEAMFORMEE_ENABLE);
		DBG_871X("[HT] Support Beamformee\n");
	}
}

void rtw_build_wmm_ie_ht(struct adapter *padapter, u8 *out_ie, uint *pout_len)
{
	unsigned char WMM_IE[] = {0x00, 0x50, 0xf2, 0x02, 0x00, 0x01, 0x00};
	int out_len;
	u8 *pframe;

	if (padapter->mlmepriv.qospriv.qos_option == 0) {
		out_len = *pout_len;
		pframe = rtw_set_ie(out_ie+out_len, WLAN_EID_VENDOR_SPECIFIC,
							_WMM_IE_Length_, WMM_IE, pout_len);

		padapter->mlmepriv.qospriv.qos_option = 1;
	}
}

/* the function is >= passive_level */
unsigned int rtw_restructure_ht_ie(struct adapter *padapter, u8 *in_ie, u8 *out_ie, uint in_len, uint *pout_len, u8 channel)
{
	u32 ielen, out_len;
	enum ieee80211_max_ampdu_length_exp max_rx_ampdu_factor;
	unsigned char *p, *pframe;
	struct ieee80211_ht_cap ht_capie;
	u8 cbw40_enable = 0, stbc_rx_enable = 0, rf_type = 0, operation_bw = 0;
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
			(pregistrypriv->wifi_spec == 1)) {
			stbc_rx_enable = 1;
			DBG_871X("declare supporting RX STBC\n");
		}
	}

	/* fill default supported_mcs_set */
	memcpy(ht_capie.mcs.rx_mask, pmlmeext->default_supported_mcs_set, 16);

	/* update default supported_mcs_set */
	rtw_hal_get_hwreg(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));

	switch (rf_type) {
	case RF_1T1R:
		if (stbc_rx_enable)
			ht_capie.cap_info |= cpu_to_le16(IEEE80211_HT_CAP_RX_STBC_1R);/* RX STBC One spatial stream */

		set_mcs_rate_by_mask(ht_capie.mcs.rx_mask, MCS_RATE_1R);
		break;

	case RF_2T2R:
	case RF_1T2R:
	default:
		if (stbc_rx_enable)
			ht_capie.cap_info |= cpu_to_le16(IEEE80211_HT_CAP_RX_STBC_2R);/* RX STBC two spatial stream */

		#ifdef CONFIG_DISABLE_MCS13TO15
		if (((cbw40_enable == 1) && (operation_bw == CHANNEL_WIDTH_40)) && (pregistrypriv->wifi_spec != 1))
				set_mcs_rate_by_mask(ht_capie.mcs.rx_mask, MCS_RATE_2R_13TO15_OFF);
		else
				set_mcs_rate_by_mask(ht_capie.mcs.rx_mask, MCS_RATE_2R);
		#else /* CONFIG_DISABLE_MCS13TO15 */
			set_mcs_rate_by_mask(ht_capie.mcs.rx_mask, MCS_RATE_2R);
		#endif /* CONFIG_DISABLE_MCS13TO15 */
		break;
	}

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

	/* rtw_hal_get_def_var(padapter, HW_VAR_MAX_RX_AMPDU_FACTOR, &max_rx_ampdu_factor); */
	ht_capie.ampdu_params_info = (max_rx_ampdu_factor&0x03);

	if (padapter->securitypriv.dot11PrivacyAlgrthm == _AES_)
		ht_capie.ampdu_params_info |= (IEEE80211_HT_CAP_AMPDU_DENSITY&(0x07<<2));
	else
		ht_capie.ampdu_params_info |= (IEEE80211_HT_CAP_AMPDU_DENSITY&0x00);

	pframe = rtw_set_ie(out_ie+out_len, WLAN_EID_HT_CAPABILITY,
						sizeof(struct ieee80211_ht_cap), (unsigned char *)&ht_capie, pout_len);

	phtpriv->ht_option = true;

	if (in_ie) {
		p = rtw_get_ie(in_ie, WLAN_EID_HT_OPERATION, &ielen, in_len);
		if (p && (ielen == sizeof(struct ieee80211_ht_addt_info))) {
			out_len = *pout_len;
			pframe = rtw_set_ie(out_ie+out_len, WLAN_EID_HT_OPERATION, ielen, p+2, pout_len);
		}
	}

	return phtpriv->ht_option;

}

/* the function is > passive_level (in critical_section) */
void rtw_update_ht_cap(struct adapter *padapter, u8 *pie, uint ie_len, u8 channel)
{
	u8 *p, max_ampdu_sz;
	int len;
	/* struct sta_info *bmc_sta, *psta; */
	struct ieee80211_ht_cap *pht_capie;
	struct ieee80211_ht_addt_info *pht_addtinfo;
	/* struct recv_reorder_ctrl *preorder_ctrl; */
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct ht_priv 	*phtpriv = &pmlmepriv->htpriv;
	/* struct recv_priv *precvpriv = &padapter->recvpriv; */
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	/* struct wlan_network *pcur_network = &(pmlmepriv->cur_network);; */
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 cbw40_enable = 0;

	if (!phtpriv->ht_option)
		return;

	if ((!pmlmeinfo->HT_info_enable) || (!pmlmeinfo->HT_caps_enable))
		return;

	DBG_871X("+rtw_update_ht_cap()\n");

	/* maybe needs check if ap supports rx ampdu. */
	if (!(phtpriv->ampdu_enable) && pregistrypriv->ampdu_enable == 1) {
		phtpriv->ampdu_enable = true;
	}

	/* check Max Rx A-MPDU Size */
	len = 0;
	p = rtw_get_ie(pie+sizeof(struct ndis_802_11_fix_ie), WLAN_EID_HT_CAPABILITY, &len, ie_len-sizeof(struct ndis_802_11_fix_ie));
	if (p && len > 0) {
		pht_capie = (struct ieee80211_ht_cap *)(p+2);
		max_ampdu_sz = (pht_capie->ampdu_params_info & IEEE80211_HT_CAP_AMPDU_FACTOR);
		max_ampdu_sz = 1 << (max_ampdu_sz+3); /*  max_ampdu_sz (kbytes); */

		/* DBG_871X("rtw_update_ht_cap(): max_ampdu_sz =%d\n", max_ampdu_sz); */
		phtpriv->rx_ampdu_maxlen = max_ampdu_sz;

	}

	len = 0;
	p = rtw_get_ie(pie+sizeof(struct ndis_802_11_fix_ie), WLAN_EID_HT_OPERATION, &len, ie_len-sizeof(struct ndis_802_11_fix_ie));
	if (p && len > 0) {
		pht_addtinfo = (struct ieee80211_ht_addt_info *)(p+2);
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
		u8 rf_type;

		rtw_hal_get_hwreg(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));

		/* update the MCS set */
		for (i = 0; i < 16; i++)
			pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate[i] &= pmlmeext->default_supported_mcs_set[i];

		/* update the MCS rates */
		switch (rf_type) {
		case RF_1T1R:
		case RF_1T2R:
			set_mcs_rate_by_mask(pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate, MCS_RATE_1R);
			break;
		case RF_2T2R:
		default:
#ifdef CONFIG_DISABLE_MCS13TO15
			if (pmlmeext->cur_bwmode == CHANNEL_WIDTH_40 && pregistrypriv->wifi_spec != 1)
				set_mcs_rate_by_mask(pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate, MCS_RATE_2R_13TO15_OFF);
			else
				set_mcs_rate_by_mask(pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate, MCS_RATE_2R);
#else /* CONFIG_DISABLE_MCS13TO15 */
			set_mcs_rate_by_mask(pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate, MCS_RATE_2R);
#endif /* CONFIG_DISABLE_MCS13TO15 */
		}

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
	if (pmlmeinfo->SM_PS == WLAN_HT_CAP_SM_PS_STATIC)
		DBG_871X("%s(): WLAN_HT_CAP_SM_PS_STATIC\n", __func__);

	/*  */
	/*  Config current HT Protection mode. */
	/*  */
	pmlmeinfo->HT_protection = pmlmeinfo->HT_info.infos[1] & 0x3;
}

void rtw_issue_addbareq_cmd(struct adapter *padapter, struct xmit_frame *pxmitframe)
{
	u8 issued;
	int priority;
	struct sta_info *psta = NULL;
	struct ht_priv *phtpriv;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	s32 bmcst = IS_MCAST(pattrib->ra);

	/* if (bmcst || (padapter->mlmepriv.LinkDetectInfo.bTxBusyTraffic == false)) */
	if (bmcst || (padapter->mlmepriv.LinkDetectInfo.NumTxOkInPeriod < 100))
		return;

	priority = pattrib->priority;

	psta = rtw_get_stainfo(&padapter->stapriv, pattrib->ra);
	if (pattrib->psta != psta) {
		DBG_871X("%s, pattrib->psta(%p) != psta(%p)\n", __func__, pattrib->psta, psta);
		return;
	}

	if (!psta) {
		DBG_871X("%s, psta ==NUL\n", __func__);
		return;
	}

	if (!(psta->state & _FW_LINKED)) {
		DBG_871X("%s, psta->state(0x%x) != _FW_LINKED\n", __func__, psta->state);
		return;
	}

	phtpriv = &psta->htpriv;

	if (phtpriv->ht_option && phtpriv->ampdu_enable) {
		issued = (phtpriv->agg_enable_bitmap>>priority)&0x1;
		issued |= (phtpriv->candidate_tid_bitmap>>priority)&0x1;

		if (0 == issued) {
			DBG_871X("rtw_issue_addbareq_cmd, p =%d\n", priority);
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
	int do_join_r;

	if (0 < rtw_to_roam(padapter)) {
		DBG_871X("roaming from %s(%pM), length:%d\n",
				cur_network->network.Ssid.Ssid, MAC_ARG(cur_network->network.MacAddress),
				cur_network->network.Ssid.SsidLength);
		memcpy(&pmlmepriv->assoc_ssid, &cur_network->network.Ssid, sizeof(struct ndis_802_11_ssid));

		pmlmepriv->assoc_by_bssid = false;

		while (1) {
			do_join_r = rtw_do_join(padapter);
			if (_SUCCESS == do_join_r) {
				break;
			} else {
				DBG_871X("roaming do_join return %d\n", do_join_r);
				rtw_dec_to_roam(padapter);

				if (rtw_to_roam(padapter) > 0) {
					continue;
				} else {
					DBG_871X("%s(%d) -to roaming fail, indicate_disconnect\n", __func__, __LINE__);
					rtw_indicate_disconnect(padapter);
					break;
				}
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
