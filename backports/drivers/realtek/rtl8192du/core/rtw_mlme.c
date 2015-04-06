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
#define _RTW_MLME_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <recv_osdep.h>
#include <xmit_osdep.h>
#include <hal_intf.h>
#include <mlme_osdep.h>
#include <sta_info.h>
#include <wifi.h>
#include <wlan_bssdef.h>
#include <rtw_ioctl_set.h>
#include <mlme_osdep.h>
#include <usb_osintf.h>
#include <rtw_mlme.h>
#include <linux/vmalloc.h>

extern unsigned char MCS_rate_2R[16];
extern unsigned char MCS_rate_1R[16];

int _rtw_init_mlme_priv(struct rtw_adapter *padapter)
{
	int i;
	u8 *pbuf;
	struct wlan_network *pnetwork;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	int res = _SUCCESS;

	/*  We don't need to memset padapter->XXX to zero,
	 * because adapter is allocated by vzalloc(). */

	pmlmepriv->nic_hdl = (u8 *)padapter;

	pmlmepriv->pscanned = NULL;
	pmlmepriv->fw_state = 0;
	pmlmepriv->cur_network.network.InfrastructureMode = NDIS802_11AUTOUNK;
	pmlmepriv->scan_mode = SCAN_ACTIVE;	/*  1: active, 0: pasive. Maybe someday we should rename this varable to "active_mode" (Jeff) */

	_rtw_spinlock_init(&(pmlmepriv->lock));
	_rtw_init_queue(&(pmlmepriv->free_bss_pool));
	_rtw_init_queue(&(pmlmepriv->scanned_queue));

	set_scanned_network_val(pmlmepriv, 0);

	memset(&pmlmepriv->assoc_ssid, 0, sizeof(struct ndis_802_11_ssid));

	pbuf = vzalloc(MAX_BSS_CNT * (sizeof(struct wlan_network)));

	if (pbuf == NULL) {
		res = _FAIL;
		goto exit;
	}
	pmlmepriv->free_bss_buf = pbuf;

	pnetwork = (struct wlan_network *)pbuf;

	for (i = 0; i < MAX_BSS_CNT; i++) {
		INIT_LIST_HEAD(&(pnetwork->list));

		rtw_list_insert_tail(&(pnetwork->list),
				     &(pmlmepriv->free_bss_pool.queue));

		pnetwork++;
	}

	/* allocate DMA-able/Non-Page memory for cmd_buf and rsp_buf */

	rtw_clear_scan_deny(padapter);

	rtw_init_mlme_timer(padapter);

exit:

	return res;
}

static void rtw_mfree_mlme_priv_lock(struct mlme_priv *pmlmepriv)
{
	_rtw_spinlock_free(&pmlmepriv->lock);
	_rtw_spinlock_free(&(pmlmepriv->free_bss_pool.lock));
	_rtw_spinlock_free(&(pmlmepriv->scanned_queue.lock));
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
#if defined (CONFIG_92D_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)
	rtw_buf_free(&pmlmepriv->assoc_req, &pmlmepriv->assoc_req_len);
	rtw_buf_free(&pmlmepriv->assoc_rsp, &pmlmepriv->assoc_rsp_len);
	rtw_free_mlme_ie_data(&pmlmepriv->wps_beacon_ie,
			      &pmlmepriv->wps_beacon_ie_len);
	rtw_free_mlme_ie_data(&pmlmepriv->wps_probe_req_ie,
			      &pmlmepriv->wps_probe_req_ie_len);
	rtw_free_mlme_ie_data(&pmlmepriv->wps_probe_resp_ie,
			      &pmlmepriv->wps_probe_resp_ie_len);
	rtw_free_mlme_ie_data(&pmlmepriv->wps_assoc_resp_ie,
			      &pmlmepriv->wps_assoc_resp_ie_len);

	rtw_free_mlme_ie_data(&pmlmepriv->p2p_beacon_ie,
			      &pmlmepriv->p2p_beacon_ie_len);
	rtw_free_mlme_ie_data(&pmlmepriv->p2p_probe_req_ie,
			      &pmlmepriv->p2p_probe_req_ie_len);
	rtw_free_mlme_ie_data(&pmlmepriv->p2p_probe_resp_ie,
			      &pmlmepriv->p2p_probe_resp_ie_len);
	rtw_free_mlme_ie_data(&pmlmepriv->p2p_go_probe_resp_ie,
			      &pmlmepriv->p2p_go_probe_resp_ie_len);
	rtw_free_mlme_ie_data(&pmlmepriv->p2p_assoc_req_ie,
			      &pmlmepriv->p2p_assoc_req_ie_len);
#endif
}

void _rtw_free_mlme_priv(struct mlme_priv *pmlmepriv)
{

	rtw_free_mlme_priv_ie_data(pmlmepriv);

	if (pmlmepriv) {
		rtw_mfree_mlme_priv_lock(pmlmepriv);

		if (pmlmepriv->free_bss_buf)
			vfree(pmlmepriv->free_bss_buf);
	}

}

int _rtw_enqueue_network(struct __queue *queue, struct wlan_network *pnetwork)
{

	if (pnetwork == NULL)
		goto exit;

	spin_lock_bh(&queue->lock);

	rtw_list_insert_tail(&pnetwork->list, &queue->queue);

	spin_unlock_bh(&queue->lock);

exit:

	return _SUCCESS;
}

struct wlan_network *_rtw_dequeue_network(struct __queue *queue)
{
	struct wlan_network *pnetwork;

	spin_lock_bh(&queue->lock);

	if (_rtw_queue_empty(queue) == true) {
		pnetwork = NULL;
	} else {
		pnetwork =
		    container_of((&queue->queue)->next, struct wlan_network,
				   list);

		list_del_init(&(pnetwork->list));
	}

	spin_unlock_bh(&queue->lock);

	return pnetwork;
}

struct wlan_network *_rtw_alloc_network(struct mlme_priv *pmlmepriv)
{				/* struct __queue *free_queue) */
	struct wlan_network *pnetwork;
	struct __queue *free_queue = &pmlmepriv->free_bss_pool;
	struct list_head *plist = NULL;

	spin_lock_bh(&free_queue->lock);

	if (_rtw_queue_empty(free_queue) == true) {
		pnetwork = NULL;
		goto exit;
	}
	plist = (&free_queue->queue)->next;

	pnetwork = container_of(plist, struct wlan_network, list);

	list_del_init(&pnetwork->list);

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
		 ("_rtw_alloc_network: ptr=%p\n", plist));
	pnetwork->network_type = 0;
	pnetwork->fixed = false;
	pnetwork->last_scanned = rtw_get_current_time();
	pnetwork->aid = 0;
	pnetwork->join_res = 0;

	pmlmepriv->num_of_scanned++;

exit:
	spin_unlock_bh(&free_queue->lock);

	return pnetwork;
}

void _rtw_free_network(struct mlme_priv *pmlmepriv,
		       struct wlan_network *pnetwork, u8 isfreeall)
{
	u32 curr_time, delta_time;
	u32 lifetime = SCANQUEUE_LIFETIME;
	struct __queue *free_queue = &(pmlmepriv->free_bss_pool);

	if (pnetwork == NULL)
		return;

	if (pnetwork->fixed == true)
		return;

	curr_time = rtw_get_current_time();

	if ((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == true) ||
	    (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == true))
		lifetime = 1;

	if (!isfreeall) {
		delta_time = (curr_time - pnetwork->last_scanned) / HZ;

		if (delta_time < lifetime)	/*  unit:sec */
			return;
	}

	spin_lock_bh(&free_queue->lock);

	list_del_init(&(pnetwork->list));

	rtw_list_insert_tail(&(pnetwork->list), &(free_queue->queue));

	pmlmepriv->num_of_scanned--;

	spin_unlock_bh(&free_queue->lock);
}

void _rtw_free_network_nolock(struct mlme_priv *pmlmepriv,
			      struct wlan_network *pnetwork)
{
	struct __queue *free_queue = &(pmlmepriv->free_bss_pool);

	if (pnetwork == NULL)
		return;

	if (pnetwork->fixed == true)
		return;

	list_del_init(&(pnetwork->list));

	rtw_list_insert_tail(&(pnetwork->list), get_list_head(free_queue));

	pmlmepriv->num_of_scanned--;
}

/*
	return the wlan_network with the matching addr

	Shall be called under atomic context... to avoid possible racing condition...
*/
struct wlan_network *_rtw_find_network(struct __queue *scanned_queue, u8 *addr)
{
	struct list_head *phead, *plist;
	struct wlan_network *pnetwork = NULL;
	u8 zero_addr[ETH_ALEN] = { 0, 0, 0, 0, 0, 0 };

	if (_rtw_memcmp(zero_addr, addr, ETH_ALEN)) {
		pnetwork = NULL;
		goto exit;
	}

	/* spin_lock_bh(&scanned_queue->lock); */

	phead = get_list_head(scanned_queue);
	plist = phead->next;

	while (plist != phead) {
		pnetwork = container_of(plist, struct wlan_network, list);

		if (_rtw_memcmp(addr, pnetwork->network.MacAddress, ETH_ALEN) ==
		    true)
			break;

		plist = plist->next;
	}

	if (plist == phead)
		pnetwork = NULL;

	/* spin_unlock_bh(&scanned_queue->lock); */

exit:

	return pnetwork;
}

void _rtw_free_network_queue(struct rtw_adapter *padapter, u8 isfreeall)
{
	struct list_head *phead, *plist;
	struct wlan_network *pnetwork;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct __queue *scanned_queue = &pmlmepriv->scanned_queue;

	spin_lock_bh(&scanned_queue->lock);

	phead = get_list_head(scanned_queue);
	plist = phead->next;

	while (rtw_end_of_queue_search(phead, plist) == false) {
		pnetwork = container_of(plist, struct wlan_network, list);

		plist = plist->next;

		_rtw_free_network(pmlmepriv, pnetwork, isfreeall);
	}

	spin_unlock_bh(&scanned_queue->lock);

}

int rtw_if_up(struct rtw_adapter *padapter)
{
	int res;

	if (padapter->bDriverStopped || padapter->bSurpriseRemoved ||
	    (check_fwstate(&padapter->mlmepriv, _FW_LINKED) == false)) {
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
			 ("rtw_if_up:bDriverStopped(%d) OR bSurpriseRemoved(%d)",
			  padapter->bDriverStopped,
			  padapter->bSurpriseRemoved));
		res = false;
	} else {
		res = true;
	}

	return res;
}

void rtw_generate_random_ibss(u8 *pibss)
{
	u32 curtime = rtw_get_current_time();

	pibss[0] = 0x02;	/* in ad-hoc mode bit1 must set to 1 */
	pibss[1] = 0x11;
	pibss[2] = 0x87;
	pibss[3] = (u8) (curtime & 0xff);	/* p[0]; */
	pibss[4] = (u8) ((curtime >> 8) & 0xff);	/* p[1]; */
	pibss[5] = (u8) ((curtime >> 16) & 0xff);	/* p[2]; */

	return;
}

u8 *rtw_get_capability_from_ie(u8 *ie)
{
	return ie + 10;
}

u16 rtw_get_capability(struct wlan_bssid_ex *bss)
{
	__le16 val;

	memcpy((u8 *)&val, rtw_get_capability_from_ie(bss->IEs), 2);

	return le16_to_cpu(val);
}

u8 *rtw_get_timestampe_from_ie(u8 *ie)
{
	return ie + 0;
}

u8 *rtw_get_beacon_interval_from_ie(u8 *ie)
{
	return ie + 8;
}

int rtw_init_mlme_priv(struct rtw_adapter *padapter)
{				/* struct mlme_priv *pmlmepriv) */
	int res;

	res = _rtw_init_mlme_priv(padapter);	/*  (pmlmepriv); */

	return res;
}

void rtw_free_mlme_priv(struct mlme_priv *pmlmepriv)
{

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("rtw_free_mlme_priv\n"));
	_rtw_free_mlme_priv(pmlmepriv);

}

static int rtw_enqueue_network(struct __queue *queue, struct wlan_network *pnetwork)
{
	int res;

	res = _rtw_enqueue_network(queue, pnetwork);

	return res;
}

static struct wlan_network *rtw_dequeue_network(struct __queue *queue)
{
	struct wlan_network *pnetwork;

	pnetwork = _rtw_dequeue_network(queue);

	return pnetwork;
}

static struct wlan_network *rtw_alloc_network(struct mlme_priv *pmlmepriv)
{
	struct wlan_network *pnetwork;

	pnetwork = _rtw_alloc_network(pmlmepriv);

	return pnetwork;
}

static void rtw_free_network(struct mlme_priv *pmlmepriv,
			     struct wlan_network *pnetwork, u8 is_freeall)
{

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
		 ("rtw_free_network==> ssid = %s\n\n",
		  pnetwork->network.Ssid.Ssid));
	_rtw_free_network(pmlmepriv, pnetwork, is_freeall);

}

static void rtw_free_network_nolock(struct mlme_priv *pmlmepriv,
				    struct wlan_network *pnetwork)
{

	/* RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("rtw_free_network==> ssid = %s\n\n" , pnetwork->network.Ssid.Ssid)); */
	_rtw_free_network_nolock(pmlmepriv, pnetwork);

}

void rtw_free_network_queue(struct rtw_adapter *dev, u8 isfreeall)
{

	_rtw_free_network_queue(dev, isfreeall);

}

/*
	return the wlan_network with the matching addr

	Shall be called under atomic context... to avoid possible racing condition...
*/
struct wlan_network *rtw_find_network(struct __queue *scanned_queue, u8 *addr)
{
	struct wlan_network *pnetwork = _rtw_find_network(scanned_queue, addr);

	return pnetwork;
}

int rtw_is_same_ibss(struct rtw_adapter *adapter, struct wlan_network *pnetwork)
{
	int ret = true;
	struct security_priv *psecuritypriv = &adapter->securitypriv;

	if ((psecuritypriv->dot11PrivacyAlgrthm != _NO_PRIVACY_) &&
	    (pnetwork->network.Privacy == 0)) {
		ret = false;
	} else if ((psecuritypriv->dot11PrivacyAlgrthm == _NO_PRIVACY_) &&
		   (pnetwork->network.Privacy == 1)) {
		ret = false;
	} else {
		ret = true;
	}

	return ret;
}

static inline int is_same_ess(struct wlan_bssid_ex *a, struct wlan_bssid_ex *b)
{
	return (a->Ssid.SsidLength == b->Ssid.SsidLength) &&
		_rtw_memcmp(a->Ssid.Ssid, b->Ssid.Ssid,
			    a->Ssid.SsidLength) == true;
}

int is_same_network(struct wlan_bssid_ex *src, struct wlan_bssid_ex *dst)
{
	u16 s_cap, d_cap;
	__le16 le_scap, le_dcap;

	memcpy((u8 *)&le_scap, rtw_get_capability_from_ie(src->IEs), 2);
	memcpy((u8 *)&le_dcap, rtw_get_capability_from_ie(dst->IEs), 2);

	s_cap = le16_to_cpu(le_scap);
	d_cap = le16_to_cpu(le_dcap);

	return ((src->Ssid.SsidLength == dst->Ssid.SsidLength) &&
		((_rtw_memcmp(src->MacAddress, dst->MacAddress, ETH_ALEN)) == true) &&
		((_rtw_memcmp
		  (src->Ssid.Ssid, dst->Ssid.Ssid,
		   src->Ssid.SsidLength)) == true) &&
		   ((s_cap & WLAN_CAPABILITY_IBSS) ==
		    (d_cap & WLAN_CAPABILITY_IBSS)) &&
		   ((s_cap & WLAN_CAPABILITY_BSS) ==
		    (d_cap & WLAN_CAPABILITY_BSS)));
}

struct wlan_network *rtw_get_oldest_wlan_network(struct __queue *scanned_queue)
{
	struct list_head *plist, *phead;
	struct wlan_network *pwlan = NULL;
	struct wlan_network *oldest = NULL;

	phead = get_list_head(scanned_queue);

	plist = phead->next;

	while (1) {
		if (rtw_end_of_queue_search(phead, plist) == true)
			break;

		pwlan = container_of(plist, struct wlan_network, list);

		if (pwlan->fixed != true) {
			if (oldest == NULL ||
			    time_after(oldest->last_scanned,
				       pwlan->last_scanned))
				oldest = pwlan;
		}

		plist = plist->next;
	}

	return oldest;
}

static void update_network(struct wlan_bssid_ex *dst, struct wlan_bssid_ex *src,
			   struct rtw_adapter *padapter, bool update_ie)
{
	u8 ss_ori = dst->PhyInfo.SignalStrength;
	u8 sq_ori = dst->PhyInfo.SignalQuality;
	long rssi_ori = dst->Rssi;

	u8 ss_smp = src->PhyInfo.SignalStrength;
	u8 sq_smp = src->PhyInfo.SignalQuality;
	long rssi_smp = src->Rssi;

	u8 ss_final;
	u8 sq_final;
	long rssi_final;

#ifdef CONFIG_ANTENNA_DIVERSITY
	rtw_hal_antdiv_rssi_compared(padapter, dst, src);	/* this will update src.Rssi, need consider again */
#endif

#if defined(DBG_RX_SIGNAL_DISPLAY_PROCESSING) && 1
	if (strcmp(dst->Ssid.Ssid, DBG_RX_SIGNAL_DISPLAY_SSID_MONITORED) == 0) {
		DBG_8192D
		    ("%s %s(%pM, ch%u) ss_ori:%3u, sq_ori:%3u, rssi_ori:%3ld, ss_smp:%3u, sq_smp:%3u, rssi_smp:%3ld\n",
		     __func__, src->Ssid.Ssid, src->MacAddress,
		     src->Configuration.DSConfig, ss_ori, sq_ori, rssi_ori,
		     ss_smp, sq_smp, rssi_smp);
	}
#endif

	/* The rule below is 1/5 for sample value, 4/5 for history value */
	if (check_fwstate(&padapter->mlmepriv, _FW_LINKED) &&
	    is_same_network(&(padapter->mlmepriv.cur_network.network), src)) {
		/* Take the recvpriv's value for the connected AP */
		ss_final = padapter->recvpriv.signal_strength;
		sq_final = padapter->recvpriv.signal_qual;
		/* the rssi value here is undecorated, and will be used for antenna diversity */
		if (sq_smp != 101)	/* from the right channel */
			rssi_final = (src->Rssi + dst->Rssi * 4) / 5;
		else
			rssi_final = rssi_ori;
	} else {
		if (sq_smp != 101) {	/* from the right channel */
			ss_final =
			    ((u32) (src->PhyInfo.SignalStrength) +
			     (u32) (dst->PhyInfo.SignalStrength) * 4) / 5;
			sq_final =
			    ((u32) (src->PhyInfo.SignalQuality) +
			     (u32) (dst->PhyInfo.SignalQuality) * 4) / 5;
			rssi_final = (src->Rssi + dst->Rssi * 4) / 5;
		} else {
			/* bss info not receving from the right channel, use the original RX signal infos */
			ss_final = dst->PhyInfo.SignalStrength;
			sq_final = dst->PhyInfo.SignalQuality;
			rssi_final = dst->Rssi;
		}
	}

	if (update_ie)
		memcpy((u8 *)dst, (u8 *)src, get_wlan_bssid_ex_sz(src));

	dst->PhyInfo.SignalStrength = ss_final;
	dst->PhyInfo.SignalQuality = sq_final;
	dst->Rssi = rssi_final;

#if defined(DBG_RX_SIGNAL_DISPLAY_PROCESSING) && 1
	if (strcmp(dst->Ssid.Ssid, DBG_RX_SIGNAL_DISPLAY_SSID_MONITORED) == 0) {
		DBG_8192D
		    ("%s %s(%pM), SignalStrength:%u, SignalQuality:%u, RawRSSI:%ld\n",
		     __func__, dst->Ssid.Ssid, dst->MacAddress,
		     dst->PhyInfo.SignalStrength, dst->PhyInfo.SignalQuality,
		     dst->Rssi);
	}
#endif

}

static void update_current_network(struct rtw_adapter *adapter,
				   struct wlan_bssid_ex *pnetwork)
{
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);

	if ((check_fwstate(pmlmepriv, _FW_LINKED) == true) &&
	    (is_same_network(&(pmlmepriv->cur_network.network), pnetwork))) {
		/* RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,"Same Network\n"); */

		/* if (pmlmepriv->cur_network.network.IELength<= pnetwork->IELength) */
		{
			update_network(&(pmlmepriv->cur_network.network),
				       pnetwork, adapter, true);
			rtw_update_protection(adapter,
					      (pmlmepriv->cur_network.network.
					       IEs) +
					      sizeof(struct
						     ndis_802_11_fixed_ies),
					      pmlmepriv->cur_network.network.
					      IELength);
		}
	}

}

/*

Caller must hold pmlmepriv->lock first.

*/
void rtw_update_scanned_network(struct rtw_adapter *adapter,
				struct wlan_bssid_ex *target)
{
	struct list_head *plist, *phead;
	u32 bssid_ex_sz;
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);
	struct __queue *queue = &(pmlmepriv->scanned_queue);
	struct wlan_network *pnetwork = NULL;
	struct wlan_network *oldest = NULL;

	spin_lock_bh(&queue->lock);
	phead = get_list_head(queue);
	plist = phead->next;

	while (1) {
		if (rtw_end_of_queue_search(phead, plist) == true)
			break;

		pnetwork = container_of(plist, struct wlan_network, list);

		if ((unsigned long)(pnetwork) < 0x7ffffff) {
		}

		if (is_same_network(&(pnetwork->network), target))
			break;

		if ((oldest == ((struct wlan_network *)0)) ||
		    time_after(oldest->last_scanned, pnetwork->last_scanned))
			oldest = pnetwork;
		plist = plist->next;
	}

	/* If we didn't find a match, then get a new network slot to initialize
	 * with this beacon's information */
	if (rtw_end_of_queue_search(phead, plist) == true) {
		if (_rtw_queue_empty(&(pmlmepriv->free_bss_pool)) == true) {
			/* If there are no more slots, expire the oldest */
			/* list_del_init(&oldest->list); */
			pnetwork = oldest;

#ifdef CONFIG_ANTENNA_DIVERSITY
			rtw_hal_get_def_var(adapter, HAL_DEF_CURRENT_ANTENNA,
					    &(target->PhyInfo.Optimum_antenna));
#endif
			memcpy(&(pnetwork->network), target,
			       get_wlan_bssid_ex_sz(target));
			/*  variable initialize */
			pnetwork->fixed = false;
			pnetwork->last_scanned = rtw_get_current_time();

			pnetwork->network_type = 0;
			pnetwork->aid = 0;
			pnetwork->join_res = 0;

			/* bss info not receving from the right channel */
			if (pnetwork->network.PhyInfo.SignalQuality == 101)
				pnetwork->network.PhyInfo.SignalQuality = 0;
		} else {
			/* Otherwise just pull from the free list */

			pnetwork = rtw_alloc_network(pmlmepriv);	/*  will update scan_time */

			if (pnetwork == NULL) {
				RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
					 ("\n\n\nsomething wrong here\n\n\n"));
				goto exit;
			}

			bssid_ex_sz = get_wlan_bssid_ex_sz(target);
			target->Length = bssid_ex_sz;
#ifdef CONFIG_ANTENNA_DIVERSITY
			/* target->PhyInfo.Optimum_antenna = pHalData->CurAntenna; */
			rtw_hal_get_def_var(adapter, HAL_DEF_CURRENT_ANTENNA,
					    &(target->PhyInfo.Optimum_antenna));
#endif
			memcpy(&(pnetwork->network), target, bssid_ex_sz);

			pnetwork->last_scanned = rtw_get_current_time();

			/* bss info not receving from the right channel */
			if (pnetwork->network.PhyInfo.SignalQuality == 101)
				pnetwork->network.PhyInfo.SignalQuality = 0;

			rtw_list_insert_tail(&(pnetwork->list),
					     &(queue->queue));
		}
	} else {
		/* we have an entry and we are going to update it. But this entry may
		 * be already expired. In this case we do the same as we found a new
		 * net and call the new_net handler
		 */
		bool update_ie = true;

		pnetwork->last_scanned = rtw_get_current_time();

		/* target.Reserved[0]==1, means that scaned network is a bcn frame. */
		if ((pnetwork->network.IELength > target->IELength) &&
		    (target->Reserved[0] == 1))
			update_ie = false;

		update_network(&(pnetwork->network), target, adapter,
			       update_ie);
	}

exit:
	spin_unlock_bh(&queue->lock);

}

static void rtw_add_network(struct rtw_adapter *adapter,
			    struct wlan_bssid_ex *pnetwork)
{
	struct mlme_priv *pmlmepriv =
	    &(((struct rtw_adapter *)adapter)->mlmepriv);

	update_current_network(adapter, pnetwork);

	rtw_update_scanned_network(adapter, pnetwork);
}

/* select the desired network based on the capability of the (i)bss. */
/*  check items: (1) security */
/*			   (2) network_type */
/*			   (3) WMM */
/*			   (4) HT */
/*                      (5) others */
static int rtw_is_desired_network(struct rtw_adapter *adapter,
				  struct wlan_network *pnetwork)
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
		if (rtw_get_wps_ie
		    (pnetwork->network.IEs + _FIXED_IE_LENGTH_,
		     pnetwork->network.IELength - _FIXED_IE_LENGTH_, NULL,
		     &wps_ielen) != NULL) {
			return true;
		} else {
			return false;
		}
	}
	if (adapter->registrypriv.wifi_spec == 1) {	/* for  correct flow of 8021X  to do.... */
		if ((desired_encmode == NDIS802_11ENCRYPTION_DISABLED) && (privacy != 0))
			bselected = false;
	}

	if ((desired_encmode != NDIS802_11ENCRYPTION_DISABLED) && (privacy == 0)) {
		DBG_8192D("desired_encmode: %d, privacy: %d\n", desired_encmode,
			  privacy);
		bselected = false;
	}

	if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == true) {
		if (pnetwork->network.InfrastructureMode !=
		    pmlmepriv->cur_network.network.InfrastructureMode)
			bselected = false;
	}

	return bselected;
}

/* TODO: Perry : For Power Management */
void rtw_atimdone_event_callback(struct rtw_adapter *adapter, u8 *pbuf)
{

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
		 ("receive atimdone_evet\n"));

	return;
}

void rtw_survey_event_callback(struct rtw_adapter *adapter, u8 *pbuf)
{
	u32 len;
	struct wlan_bssid_ex *pnetwork;
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);

	pnetwork = (struct wlan_bssid_ex *)pbuf;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
		 ("rtw_survey_event_callback, ssid=%s\n", pnetwork->Ssid.Ssid));

	len = get_wlan_bssid_ex_sz(pnetwork);
	if (len > (sizeof(struct wlan_bssid_ex))) {
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
			 ("\n ****rtw_survey_event_callback: return a wrong bss ***\n"));
		return;
	}

	spin_lock_bh(&pmlmepriv->lock);

	/*  update IBSS_network 's timestamp */
	if ((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)) == true) {
		if (_rtw_memcmp
		    (&(pmlmepriv->cur_network.network.MacAddress),
		     pnetwork->MacAddress, ETH_ALEN)) {
			struct wlan_network *ibss_wlan = NULL;

			memcpy(pmlmepriv->cur_network.network.IEs,
			       pnetwork->IEs, 8);
			spin_lock_bh(&(pmlmepriv->scanned_queue.lock));
			ibss_wlan =
			    rtw_find_network(&pmlmepriv->scanned_queue,
					     pnetwork->MacAddress);
			if (ibss_wlan) {
				memcpy(ibss_wlan->network.IEs, pnetwork->IEs,
				       8);
				spin_unlock_bh(&
					       (pmlmepriv->scanned_queue.lock));
				goto exit;
			}
			spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));
		}
	}

	/*  lock pmlmepriv->lock when you accessing network_q */
	if ((check_fwstate(pmlmepriv, _FW_UNDER_LINKING)) == false) {
		if (pnetwork->Ssid.Ssid[0] == 0) {
			pnetwork->Ssid.SsidLength = 0;
		}
		rtw_add_network(adapter, pnetwork);
	}

exit:

	spin_unlock_bh(&pmlmepriv->lock);

	return;
}

void rtw_surveydone_event_callback(struct rtw_adapter *adapter, u8 *pbuf)
{
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);
#ifdef CONFIG_MLME_EXT

	mlmeext_surveydone_event_callback(adapter);

#endif

	spin_lock_bh(&pmlmepriv->lock);

	if (pmlmepriv->wps_probe_req_ie) {
		u32 free_len = pmlmepriv->wps_probe_req_ie_len;
		pmlmepriv->wps_probe_req_ie_len = 0;
		kfree(pmlmepriv->wps_probe_req_ie);
		pmlmepriv->wps_probe_req_ie = NULL;
	}

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
		 ("rtw_surveydone_event_callback: fw_state:%x\n\n",
		  get_fwstate(pmlmepriv)));

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY)) {
		u8 timer_cancelled;

		_cancel_timer(&pmlmepriv->scan_to_timer, &timer_cancelled);

		_clr_fwstate_(pmlmepriv, _FW_UNDER_SURVEY);
	} else {
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
			 ("nic status =%x, survey done event comes too late!\n",
			  get_fwstate(pmlmepriv)));
	}
	rtw_set_signal_stat_timer(&adapter->recvpriv);

	if (pmlmepriv->to_join == true) {
		if ((check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == true)) {
			if (check_fwstate(pmlmepriv, _FW_LINKED) == false) {
				set_fwstate(pmlmepriv, _FW_UNDER_LINKING);

				if (rtw_select_and_join_from_scanned_queue
				    (pmlmepriv) == _SUCCESS) {
					_set_timer(&pmlmepriv->assoc_timer,
						   MAX_JOIN_TIMEOUT);
				} else {
					struct wlan_bssid_ex *pdev_network =
					    &(adapter->registrypriv.
					      dev_network);
					u8 *pibss =
					    adapter->registrypriv.dev_network.
					    MacAddress;

					_clr_fwstate_(pmlmepriv,
						      _FW_UNDER_SURVEY);

					RT_TRACE(_module_rtl871x_mlme_c_,
						 _drv_err_,
						 ("switching to adhoc master\n"));

					memset(&pdev_network->Ssid, 0,
					       sizeof(struct ndis_802_11_ssid));
					memcpy(&pdev_network->Ssid,
					       &pmlmepriv->assoc_ssid,
					       sizeof(struct ndis_802_11_ssid));

					rtw_update_registrypriv_dev_network
					    (adapter);
					rtw_generate_random_ibss(pibss);

					pmlmepriv->fw_state =
					    WIFI_ADHOC_MASTER_STATE;

					if (rtw_createbss_cmd(adapter) !=
					    _SUCCESS) {
						RT_TRACE
						    (_module_rtl871x_mlme_c_,
						     _drv_err_,
						     ("Error=>rtw_createbss_cmd status FAIL\n"));
					}

					pmlmepriv->to_join = false;
				}
			}
		} else {
			int s_ret;
			set_fwstate(pmlmepriv, _FW_UNDER_LINKING);
			pmlmepriv->to_join = false;
			s_ret = rtw_select_and_join_from_scanned_queue(pmlmepriv);
			if (_SUCCESS == s_ret) {
				_set_timer(&pmlmepriv->assoc_timer,
					   MAX_JOIN_TIMEOUT);
			} else if (s_ret == 2) {	/* there is no need to wait for join */
				_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);
				rtw_indicate_connect(adapter);
			} else {
				DBG_8192D
				    ("try_to_join, but select scanning queue fail, to_roaming:%d\n",
				     rtw_to_roaming(adapter));
#ifdef CONFIG_LAYER2_ROAMING
				if (rtw_to_roaming(adapter) != 0) {
					if (--pmlmepriv->to_roaming == 0 || _SUCCESS !=
					    rtw_sitesurvey_cmd(adapter,
							       &pmlmepriv->
							       assoc_ssid, 1,
							       NULL, 0)
					    ) {
						rtw_set_roaming(adapter, 0);
						rtw_free_assoc_resources
						    (adapter, 1);
						rtw_indicate_disconnect
						    (adapter);
					} else {
						pmlmepriv->to_join = true;
					}
				}
#endif
				_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);
			}
		}
	}

	indicate_wx_scan_complete_event(adapter);
	/* DBG_8192D("scan complete in %dms\n",rtw_get_passing_time_ms(pmlmepriv->scan_start_time)); */

	spin_unlock_bh(&pmlmepriv->lock);

	rtw_os_xmit_schedule(adapter);
#ifdef CONFIG_CONCURRENT_MODE
	rtw_os_xmit_schedule(adapter->pbuddy_adapter);
#endif
#ifdef CONFIG_DUALMAC_CONCURRENT
	dc_resume_xmit(adapter);
#endif

#ifdef CONFIG_DRVEXT_MODULE_WSC
	drvext_surveydone_callback(&adapter->drvextpriv);
#endif

#ifdef DBG_CONFIG_ERROR_DETECT
	{
		struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
		if (pmlmeext->sitesurvey_res.bss_cnt == 0) {
			rtw_hal_sreset_reset(adapter);
		}
	}
#endif

	rtw_cfg80211_surveydone_event_callback(adapter);
}

static void free_scanqueue(struct mlme_priv *pmlmepriv)
{
	struct __queue *free_queue = &pmlmepriv->free_bss_pool;
	struct __queue *scan_queue = &pmlmepriv->scanned_queue;
	struct list_head *plist, *phead, *ptemp;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_, ("+free_scanqueue\n"));
	spin_lock_bh(&scan_queue->lock);
	spin_lock_bh(&free_queue->lock);

	phead = get_list_head(scan_queue);
	plist = phead->next;

	while (plist != phead) {
		ptemp = plist->next;
		list_del_init(plist);
		rtw_list_insert_tail(plist, &free_queue->queue);
		plist = ptemp;
		pmlmepriv->num_of_scanned--;
	}

	spin_unlock_bh(&free_queue->lock);
	spin_unlock_bh(&scan_queue->lock);

}

/*
*rtw_free_assoc_resources: the caller has to lock pmlmepriv->lock
*/
void rtw_free_assoc_resources(struct rtw_adapter *adapter,
			      int lock_scanned_queue)
{
	struct wlan_network *pwlan = NULL;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct sta_priv *pstapriv = &adapter->stapriv;
	struct wlan_network *tgt_network = &pmlmepriv->cur_network;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_,
		 ("+rtw_free_assoc_resources\n"));
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
		 ("tgt_network->network.MacAddress=%pM ssid=%s\n",
		  tgt_network->network.MacAddress,
		  tgt_network->network.Ssid.Ssid));

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE | WIFI_AP_STATE)) {
		struct sta_info *psta;

		psta =
		    rtw_get_stainfo(&adapter->stapriv,
				    tgt_network->network.MacAddress);

		spin_lock_bh(&(pstapriv->sta_hash_lock));
		rtw_free_stainfo(adapter, psta);
		spin_unlock_bh(&(pstapriv->sta_hash_lock));
	}

	if (check_fwstate
	    (pmlmepriv,
	     WIFI_ADHOC_STATE | WIFI_ADHOC_MASTER_STATE | WIFI_AP_STATE)) {
		struct sta_info *psta;

		rtw_free_all_stainfo(adapter);

		psta = rtw_get_bcmc_stainfo(adapter);
		spin_lock_bh(&(pstapriv->sta_hash_lock));
		rtw_free_stainfo(adapter, psta);
		spin_unlock_bh(&(pstapriv->sta_hash_lock));

		rtw_init_bcmc_stainfo(adapter);
	}

	if (lock_scanned_queue)
		spin_lock_bh(&(pmlmepriv->scanned_queue.lock));

	pwlan =
	    rtw_find_network(&pmlmepriv->scanned_queue,
			     tgt_network->network.MacAddress);
	if (pwlan) {
		pwlan->fixed = false;
	} else {
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
			 ("rtw_free_assoc_resources : pwlan== NULL\n\n"));
	}

	if ((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) &&
	    (adapter->stapriv.asoc_sta_count == 1)))
		rtw_free_network_nolock(pmlmepriv, pwlan);

	/* Sparse warning ifor context imbalance is OK here */
	if (lock_scanned_queue)
		spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));

	pmlmepriv->key_mask = 0;
}

/*
*rtw_indicate_connect: the caller has to lock pmlmepriv->lock
*/
void rtw_indicate_connect(struct rtw_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
		 ("+rtw_indicate_connect\n"));

	pmlmepriv->to_join = false;

	if (!check_fwstate(&padapter->mlmepriv, _FW_LINKED)) {
#ifdef CONFIG_SW_ANTENNA_DIVERSITY
		rtw_hal_set_hwreg(padapter, HW_VAR_ANTENNA_DIVERSITY_LINK, 0);
#endif
		set_fwstate(pmlmepriv, _FW_LINKED);

		rtw_led_control(padapter, LED_CTL_LINK);

#ifdef CONFIG_DRVEXT_MODULE
		if (padapter->drvextpriv.enable_wpa) {
			indicate_l2_connect(padapter);
		} else
#endif
		{
			rtw_os_indicate_connect(padapter);
		}
	}

	rtw_set_roaming(padapter, 0);

	rtw_set_scan_deny(padapter, 3000);

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
		 ("-rtw_indicate_connect: fw_state=0x%08x\n",
		  get_fwstate(pmlmepriv)));

}

/*
*rtw_indicate_disconnect: the caller has to lock pmlmepriv->lock
*/
void rtw_indicate_disconnect(struct rtw_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
		 ("+rtw_indicate_disconnect\n"));

	_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING | WIFI_UNDER_WPS);

	if (rtw_to_roaming(padapter) > 0)
		_clr_fwstate_(pmlmepriv, _FW_LINKED);

	if (check_fwstate(&padapter->mlmepriv, _FW_LINKED) ||
	    (rtw_to_roaming(padapter) <= 0)) {
		rtw_os_indicate_disconnect(padapter);
		_clr_fwstate_(pmlmepriv, _FW_LINKED);
		rtw_led_control(padapter, LED_CTL_NO_LINK);
		rtw_clear_scan_deny(padapter);
	}
#ifdef CONFIG_LPS
#ifdef CONFIG_WOWLAN
	if (padapter->pwrctrlpriv.wowlan_mode == false)
#endif /* CONFIG_WOWLAN */
		rtw_lps_ctrl_wk_cmd(padapter, LPS_CTRL_DISCONNECT, 1);

#endif

}

inline void rtw_indicate_scan_done(struct rtw_adapter *padapter, bool aborted)
{
	rtw_os_indicate_scan_done(padapter, aborted);
}

void rtw_scan_abort(struct rtw_adapter *adapter)
{
	u32 cnt = 0;
	u32 start;
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &(adapter->mlmeextpriv);

	start = rtw_get_current_time();
	pmlmeext->scan_abort = true;
	while (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY) &&
	       rtw_get_passing_time_ms(start) <= 200) {
		if (adapter->bDriverStopped || adapter->bSurpriseRemoved)
			break;

		DBG_8192D(FUNC_NDEV_FMT "fw_state=_FW_UNDER_SURVEY!\n",
			  FUNC_NDEV_ARG(adapter->pnetdev));
		rtw_msleep_os(20);
	}

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY)) {
		if (!adapter->bDriverStopped && !adapter->bSurpriseRemoved)
			DBG_8192D(FUNC_NDEV_FMT
				  "waiting for scan_abort time out!\n",
				  FUNC_NDEV_ARG(adapter->pnetdev));
		rtw_indicate_scan_done(adapter, true);
	}
	pmlmeext->scan_abort = false;
}

static struct sta_info *rtw_joinbss_update_stainfo(struct rtw_adapter *padapter,
						   struct wlan_network
						   *pnetwork)
{
	int i;
	struct sta_info *bmc_sta, *psta = NULL;
	struct recv_reorder_ctrl *preorder_ctrl;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	psta = rtw_get_stainfo(pstapriv, pnetwork->network.MacAddress);
	if (psta == NULL) {
		psta =
		    rtw_alloc_stainfo(pstapriv, pnetwork->network.MacAddress);
	}

	if (psta) {		/* update ptarget_sta */
		DBG_8192D("%s\n", __func__);

		psta->aid = pnetwork->join_res;
#ifdef CONFIG_CONCURRENT_MODE

		if (PRIMARY_ADAPTER == padapter->adapter_type)
			psta->mac_id = 0;
		else
			psta->mac_id = 2;
#else
		psta->mac_id = 0;
#endif

		psta->raid = networktype_to_raid(pmlmeext->cur_wireless_mode);

		/* security related */
		if (padapter->securitypriv.dot11AuthAlgrthm ==
		    dot11AuthAlgrthm_8021X) {
			padapter->securitypriv.binstallGrpkey = false;
			padapter->securitypriv.busetkipkey = false;
			padapter->securitypriv.bgrpkey_handshake = false;

			psta->ieee8021x_blocked = true;
			psta->dot118021XPrivacy =
			    padapter->securitypriv.dot11PrivacyAlgrthm;

			memset((u8 *)&psta->dot118021x_UncstKey, 0,
			       sizeof(union Keytype));

			memset((u8 *)&psta->dot11tkiprxmickey, 0,
			       sizeof(union Keytype));
			memset((u8 *)&psta->dot11tkiptxmickey, 0,
			       sizeof(union Keytype));

			memset((u8 *)&psta->dot11txpn, 0, sizeof(union pn48));
			memset((u8 *)&psta->dot11rxpn, 0, sizeof(union pn48));
		}

		/*      Commented by Albert 2012/07/21 */
		/*      When doing the WPS, the wps_ie_len won't equal to 0 */
		/*      And the Wi-Fi driver shouldn't allow the data packet to be tramsmitted. */
		if (padapter->securitypriv.wps_ie_len != 0) {
			psta->ieee8021x_blocked = true;
			padapter->securitypriv.wps_ie_len = 0;
		}

		/* for A-MPDU Rx reordering buffer control for bmc_sta & sta_info */
		/* if A-MPDU Rx is enabled, reseting  rx_ordering_ctrl wstart_b(indicate_seq) to default value=0xffff */
		/* todo: check if AP can send A-MPDU packets */
		for (i = 0; i < 16; i++) {
			/* preorder_ctrl = &precvpriv->recvreorder_ctrl[i]; */
			preorder_ctrl = &psta->recvreorder_ctrl[i];
			preorder_ctrl->enable = false;
			preorder_ctrl->indicate_seq = 0xffff;
#ifdef DBG_RX_SEQ
			DBG_8192D("DBG_RX_SEQ %s:%d indicate_seq:%u\n",
				  __func__, __LINE__,
				  preorder_ctrl->indicate_seq);
#endif
			preorder_ctrl->wend_b = 0xffff;
			preorder_ctrl->wsize_b = 64;	/* max_ampdu_sz; ex. 32(kbytes) -> wsize_b=32 */
		}

		bmc_sta = rtw_get_bcmc_stainfo(padapter);
		if (bmc_sta) {
			for (i = 0; i < 16; i++) {
				/* preorder_ctrl = &precvpriv->recvreorder_ctrl[i]; */
				preorder_ctrl = &bmc_sta->recvreorder_ctrl[i];
				preorder_ctrl->enable = false;
				preorder_ctrl->indicate_seq = 0xffff;
#ifdef DBG_RX_SEQ
				DBG_8192D("DBG_RX_SEQ %s:%d indicate_seq:%u\n",
					  __func__, __LINE__,
					  preorder_ctrl->indicate_seq);
#endif
				preorder_ctrl->wend_b = 0xffff;
				preorder_ctrl->wsize_b = 64;	/* max_ampdu_sz; ex. 32(kbytes) -> wsize_b=32 */
			}
		}
		/* misc. */
		update_sta_info(padapter, psta);
	}
	return psta;
}

/* pnetwork : returns from rtw_joinbss_event_callback */
/* ptarget_wlan: found from scanned_queue */
static void rtw_joinbss_update_network(struct rtw_adapter *padapter,
				       struct wlan_network *ptarget_wlan,
				       struct wlan_network *pnetwork)
{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct wlan_network *cur_network = &(pmlmepriv->cur_network);

	DBG_8192D("%s\n", __func__);

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
		 ("\nfw_state:%x, BSSID:%pM\n", get_fwstate(pmlmepriv),
		  pnetwork->network.MacAddress));

	/*  why not use ptarget_wlan?? */
	memcpy(&cur_network->network, &pnetwork->network,
	       pnetwork->network.Length);

	cur_network->aid = pnetwork->join_res;

	rtw_set_signal_stat_timer(&padapter->recvpriv);
	padapter->recvpriv.signal_strength =
	    ptarget_wlan->network.PhyInfo.SignalStrength;
	padapter->recvpriv.signal_qual =
	    ptarget_wlan->network.PhyInfo.SignalQuality;
	/* the ptarget_wlan->network.Rssi is raw data,
	 * we use scaled ptarget_wlan->network.PhyInfo.SignalStrength instead
	 */
	padapter->recvpriv.rssi =
	    translate_percentage_to_dbm(ptarget_wlan->network.PhyInfo.
					SignalStrength);
#if defined(DBG_RX_SIGNAL_DISPLAY_PROCESSING) && 1
	DBG_8192D("%s signal_strength:%3u, rssi:%3d, signal_qual:%3u"
		  "\n", __func__, adapter->recvpriv.signal_strength,
		  adapter->recvpriv.rssi, adapter->recvpriv.signal_qual);
#endif
	rtw_set_signal_stat_timer(&padapter->recvpriv);

	/* update fw_state will clr _FW_UNDER_LINKING here indirectly */
	switch (pnetwork->network.InfrastructureMode) {
	case NDIS802_11INFRA:

		if (pmlmepriv->fw_state & WIFI_UNDER_WPS)
			pmlmepriv->fw_state =
			    WIFI_STATION_STATE | WIFI_UNDER_WPS;
		else
			pmlmepriv->fw_state = WIFI_STATION_STATE;

		break;
	case NDIS802_11IBSS:
		pmlmepriv->fw_state = WIFI_ADHOC_STATE;
		break;
	default:
		pmlmepriv->fw_state = WIFI_NULL_STATE;
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
			 ("Invalid network_mode\n"));
		break;
	}

	rtw_update_protection(padapter,
			      (cur_network->network.IEs) +
			      sizeof(struct ndis_802_11_fixed_ies),
			      (cur_network->network.IELength));

#ifdef CONFIG_80211N_HT
	rtw_update_ht_cap(padapter, cur_network->network.IEs,
			  cur_network->network.IELength,
			  (u8) cur_network->network.Configuration.DSConfig);
#endif
}

/* Notes: the fucntion could be > passive_level (the same context as Rx tasklet) */
/* pnetwork : returns from rtw_joinbss_event_callback */
/* ptarget_wlan: found from scanned_queue */
/* if join_res > 0, for (fw_state==WIFI_STATION_STATE), we check if  "ptarget_sta" & "ptarget_wlan" exist. */
/* if join_res > 0, for (fw_state==WIFI_ADHOC_STATE), we only check if "ptarget_wlan" exist. */
/* if join_res > 0, update "cur_network->network" from "pnetwork->network" if (ptarget_wlan !=NULL). */
/*  */
/* define REJOIN */
void rtw_joinbss_event_prehandle(struct rtw_adapter *adapter, u8 *pbuf)
{
#ifdef REJOIN
	static u8 retry;
#endif
	u8 timer_cancelled;
	struct sta_info *ptarget_sta = NULL, *pcur_sta = NULL;
	struct sta_priv *pstapriv = &adapter->stapriv;
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);
	struct wlan_network *pnetwork = (struct wlan_network *)pbuf;
	struct wlan_network *cur_network = &(pmlmepriv->cur_network);
	struct wlan_network *pcur_wlan = NULL, *ptarget_wlan = NULL;
	unsigned int the_same_macaddr = false;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
		 ("joinbss event call back received with res=%d\n",
		  pnetwork->join_res));

	if (pmlmepriv->assoc_ssid.SsidLength == 0) {
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
			 ("@@@@@   joinbss event call back  for Any SSid\n"));
	} else {
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
			 ("@@@@@   rtw_joinbss_event_callback for SSid:%s\n",
			  pmlmepriv->assoc_ssid.Ssid));
	}

	the_same_macaddr =
	    _rtw_memcmp(pnetwork->network.MacAddress,
			cur_network->network.MacAddress, ETH_ALEN);

	pnetwork->network.Length = get_wlan_bssid_ex_sz(&pnetwork->network);
	if (pnetwork->network.Length > sizeof(struct wlan_bssid_ex)) {
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
			 ("\n\n ***joinbss_evt_callback return a wrong bss ***\n\n"));
		return;
	}

	spin_lock_bh(&pmlmepriv->lock);

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
		 ("\n rtw_joinbss_event_callback !! _enter_critical\n"));

	if (pnetwork->join_res > 0) {
		spin_lock_bh(&(pmlmepriv->scanned_queue.lock));
#ifdef REJOIN
		retry = 0;
#endif
		if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING)) {
			/* s1. find ptarget_wlan */
			if (check_fwstate(pmlmepriv, _FW_LINKED)) {
				if (the_same_macaddr == true) {
					ptarget_wlan =
					    rtw_find_network(&pmlmepriv->
							     scanned_queue,
							     cur_network->
							     network.
							     MacAddress);
				} else {
					pcur_wlan =
					    rtw_find_network(&pmlmepriv->
							     scanned_queue,
							     cur_network->
							     network.
							     MacAddress);
					if (pcur_wlan)
						pcur_wlan->fixed = false;

					pcur_sta =
					    rtw_get_stainfo(pstapriv,
							    cur_network->
							    network.MacAddress);
					if (pcur_sta) {
						spin_lock_bh(&
							     (pstapriv->
							      sta_hash_lock));
						rtw_free_stainfo(adapter,
								 pcur_sta);
						spin_unlock_bh(&
							       (pstapriv->
								sta_hash_lock));
					}

					ptarget_wlan =
					    rtw_find_network(&pmlmepriv->
							     scanned_queue,
							     pnetwork->network.
							     MacAddress);
					if (check_fwstate
					    (pmlmepriv,
					     WIFI_STATION_STATE) == true) {
						if (ptarget_wlan)
							ptarget_wlan->fixed =
							    true;
					}
				}

			} else {
				ptarget_wlan =
				    rtw_find_network(&pmlmepriv->scanned_queue,
						     pnetwork->network.
						     MacAddress);
				if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)
				    == true) {
					if (ptarget_wlan)
						ptarget_wlan->fixed = true;
				}
			}

			/* s2. update cur_network */
			if (ptarget_wlan) {
				rtw_joinbss_update_network(adapter,
							   ptarget_wlan,
							   pnetwork);
			} else {
				RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
					 ("Can't find ptarget_wlan when joinbss_event callback\n"));
				spin_unlock_bh(&
					       (pmlmepriv->scanned_queue.lock));
				goto ignore_joinbss_callback;
			}

			/* s3. find ptarget_sta & update ptarget_sta after update cur_network only for station mode */
			if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) ==
			    true) {
				ptarget_sta =
				    rtw_joinbss_update_stainfo(adapter,
							       pnetwork);
				if (ptarget_sta == NULL) {
					RT_TRACE(_module_rtl871x_mlme_c_,
						 _drv_err_,
						 ("Can't update stainfo when joinbss_event callback\n"));
					spin_unlock_bh(&
						       (pmlmepriv->
							scanned_queue.lock));
					goto ignore_joinbss_callback;
				}
			}

			/* s4. indicate connect */
			if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) ==
			    true) {
				rtw_indicate_connect(adapter);
			} else {
				/* adhoc mode will rtw_indicate_connect when rtw_stassoc_event_callback */
				RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
					 ("adhoc mode, fw_state:%x",
					  get_fwstate(pmlmepriv)));
			}

			/* s5. Cancle assoc_timer */
			_cancel_timer(&pmlmepriv->assoc_timer,
				      &timer_cancelled);

			RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
				 ("Cancle assoc_timer\n"));

		} else {
			RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
				 ("rtw_joinbss_event_callback err: fw_state:%x",
				  get_fwstate(pmlmepriv)));
			spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));
			goto ignore_joinbss_callback;
		}

		spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));

	} else if (pnetwork->join_res == -4) {
		rtw_reset_securitypriv(adapter);
		_set_timer(&pmlmepriv->assoc_timer, 1);

		/* rtw_free_assoc_resources(adapter, 1); */

		if ((check_fwstate(pmlmepriv, _FW_UNDER_LINKING)) == true) {
			RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
				 ("fail! clear _FW_UNDER_LINKING ^^^fw_state=%x\n",
				  get_fwstate(pmlmepriv)));
			_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);
		}

	} else {		/* if join_res < 0 (join fails), then try again */

#ifdef REJOIN
		res = _FAIL;
		if (retry < 2) {
			res = rtw_select_and_join_from_scanned_queue(pmlmepriv);
			RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
				 ("rtw_select_and_join_from_scanned_queue again! res:%d\n",
				  res));
		}

		if (res == _SUCCESS) {
			/* extend time of assoc_timer */
			_set_timer(&pmlmepriv->assoc_timer, MAX_JOIN_TIMEOUT);
			retry++;
		} else if (res == 2) {	/* there is no need to wait for join */
			_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);
			rtw_indicate_connect(adapter);
		} else {
			RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
				 ("Set Assoc_Timer = 1; can't find match ssid in scanned_q\n"));
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

void rtw_joinbss_event_callback(struct rtw_adapter *adapter, u8 *pbuf)
{
	struct wlan_network *pnetwork = (struct wlan_network *)pbuf;

	mlmeext_joinbss_event_callback(adapter, pnetwork->join_res);

	rtw_os_xmit_schedule(adapter);

#ifdef CONFIG_CONCURRENT_MODE
	rtw_os_xmit_schedule(adapter->pbuddy_adapter);
#endif

#ifdef CONFIG_DUALMAC_CONCURRENT
	dc_resume_xmit(adapter);
#endif

}

void rtw_stassoc_event_callback(struct rtw_adapter *adapter, u8 *pbuf)
{
	struct sta_info *psta;
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);
	struct stassoc_event *pstassoc = (struct stassoc_event *)pbuf;
	struct wlan_network *cur_network = &(pmlmepriv->cur_network);
	struct wlan_network *ptarget_wlan = NULL;

	if (rtw_access_ctrl(adapter, pstassoc->macaddr) == false)
		return;

#if defined (CONFIG_92D_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)
	if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		psta = rtw_get_stainfo(&adapter->stapriv, pstassoc->macaddr);
		if (psta) {
			u8 *passoc_req = NULL;
			u32 assoc_req_len;

			spin_lock_bh(&psta->lock);
			if (psta->passoc_req && psta->assoc_req_len > 0) {
				passoc_req = kzalloc(psta->assoc_req_len, GFP_ATOMIC);
				if (passoc_req) {
					assoc_req_len = psta->assoc_req_len;
					memcpy(passoc_req, psta->passoc_req,
					       assoc_req_len);

					kfree(psta->passoc_req);
					psta->passoc_req = NULL;
					psta->assoc_req_len = 0;
				}
			}
			spin_unlock_bh(&psta->lock);

			if (passoc_req && assoc_req_len > 0) {
				rtw_cfg80211_indicate_sta_assoc(adapter,
								passoc_req,
								assoc_req_len);

				kfree(passoc_req);
			}

			ap_sta_info_defer_update(adapter, psta);
		}
		return;
	}
#endif

	psta = rtw_get_stainfo(&adapter->stapriv, pstassoc->macaddr);
	if (psta != NULL) {
		/* the sta have been in sta_info_queue => do nothing */

		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
			 ("Error: rtw_stassoc_event_callback: sta has been in sta_hash_queue\n"));

		return;	/* between drv has received this event before and  fw have not yet to set key to CAM_ENTRY) */
	}

	psta = rtw_alloc_stainfo(&adapter->stapriv, pstassoc->macaddr);
	if (psta == NULL) {
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
			 ("Can't alloc sta_info when rtw_stassoc_event_callback\n"));
		return;
	}

	/* to do : init sta_info variable */
	psta->qos_option = 0;
	psta->mac_id = (uint) pstassoc->cam_id;
	/* psta->aid = (uint)pstassoc->cam_id; */

	if (adapter->securitypriv.dot11AuthAlgrthm == dot11AuthAlgrthm_8021X)
		psta->dot118021XPrivacy =
		    adapter->securitypriv.dot11PrivacyAlgrthm;

	psta->ieee8021x_blocked = false;

	spin_lock_bh(&pmlmepriv->lock);

	if ((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == true) ||
	    (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == true)) {
		if (adapter->stapriv.asoc_sta_count == 2) {
			spin_lock_bh(&(pmlmepriv->scanned_queue.lock));
			ptarget_wlan =
			    rtw_find_network(&pmlmepriv->scanned_queue,
					     cur_network->network.MacAddress);
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

void rtw_stadel_event_callback(struct rtw_adapter *adapter, u8 *pbuf)
{
	struct sta_info *psta;
	struct wlan_network *pwlan = NULL;
	struct wlan_bssid_ex *pdev_network = NULL;
	u8 *pibss = NULL;
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);
	struct stadel_event *pstadel = (struct stadel_event *)pbuf;
	struct sta_priv *pstapriv = &adapter->stapriv;
	struct wlan_network *tgt_network = &(pmlmepriv->cur_network);

#ifdef CONFIG_92D_AP_MODE
	if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		rtw_cfg80211_indicate_sta_disassoc(adapter, pstadel->macaddr,
						   *(u16 *)pstadel->rsvd);
		return;
	}
#endif
	mlmeext_sta_del_event_callback(adapter);

	spin_lock_bh(&pmlmepriv->lock);

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
#ifdef CONFIG_LAYER2_ROAMING
		if (rtw_to_roaming(adapter) > 0)
			pmlmepriv->to_roaming--;	/* this stadel_event is caused by roaming, decrease to_roaming */
		else if (rtw_to_roaming(adapter) == 0)
			rtw_set_roaming(adapter,
					adapter->registrypriv.
					max_roaming_times);
			if (*((unsigned short *)(pstadel->rsvd)) !=
			    WLAN_REASON_EXPIRATION_CHK)
				rtw_set_roaming(adapter, 0);	/* don't roam */
#endif

		rtw_free_uc_swdec_pending_queue(adapter);

		rtw_free_assoc_resources(adapter, 1);
		rtw_indicate_disconnect(adapter);
		spin_lock_bh(&(pmlmepriv->scanned_queue.lock));
		/*  remove the network entry in scanned_queue */
		pwlan =
		    rtw_find_network(&pmlmepriv->scanned_queue,
				     tgt_network->network.MacAddress);
		if (pwlan) {
			pwlan->fixed = false;
			rtw_free_network_nolock(pmlmepriv, pwlan);
		}
		spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));

		_rtw_roaming(adapter, tgt_network);

	}

	if (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) ||
	    check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)) {
		psta = rtw_get_stainfo(&adapter->stapriv, pstadel->macaddr);

		spin_lock_bh(&(pstapriv->sta_hash_lock));
		rtw_free_stainfo(adapter, psta);
		spin_unlock_bh(&(pstapriv->sta_hash_lock));

		if (adapter->stapriv.asoc_sta_count == 1) {	/* a sta + bc/mc_stainfo (not Ibss_stainfo) */
			spin_lock_bh(&(pmlmepriv->scanned_queue.lock));
			/* free old ibss network */
			pwlan =
			    rtw_find_network(&pmlmepriv->scanned_queue,
					     tgt_network->network.MacAddress);
			if (pwlan) {
				pwlan->fixed = false;
				rtw_free_network_nolock(pmlmepriv, pwlan);
			}
			spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));
			/* re-create ibss */
			pdev_network = &(adapter->registrypriv.dev_network);
			pibss = adapter->registrypriv.dev_network.MacAddress;

			memcpy(pdev_network, &tgt_network->network,
			       get_wlan_bssid_ex_sz(&tgt_network->network));

			memset(&pdev_network->Ssid, 0,
			       sizeof(struct ndis_802_11_ssid));
			memcpy(&pdev_network->Ssid, &pmlmepriv->assoc_ssid,
			       sizeof(struct ndis_802_11_ssid));

			rtw_update_registrypriv_dev_network(adapter);

			rtw_generate_random_ibss(pibss);

			if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)) {
				set_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE);
				_clr_fwstate_(pmlmepriv, WIFI_ADHOC_STATE);
			}

			if (rtw_createbss_cmd(adapter) != _SUCCESS) {
				RT_TRACE(_module_rtl871x_ioctl_set_c_,
					 _drv_err_,
					 ("***Error=>stadel_event_callback: rtw_createbss_cmd status FAIL***\n "));
			}
		}
	}

	spin_unlock_bh(&pmlmepriv->lock);

}

void rtw_cpwm_event_callback(struct rtw_adapter *padapter, u8 *pbuf)
{
#ifdef CONFIG_LPS_LCLK
	struct reportpwrstate_parm *preportpwrstate;
#endif

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
		 ("rtw_cpwm_event_callback !!!\n"));
#ifdef CONFIG_LPS_LCLK
	preportpwrstate = (struct reportpwrstate_parm *)pbuf;
	preportpwrstate->state |= (u8) (padapter->pwrctrlpriv.cpwm_tog + 0x80);
	cpwm_int_hdl(padapter, preportpwrstate);
#endif

}

/*
* _rtw_join_timeout_handler - Timeout/faliure handler for CMD JoinBss
* @adapter: pointer to _adapter structure
*/
void _rtw_join_timeout_handler(struct rtw_adapter *adapter)
{
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
#ifdef CONFIG_LAYER2_ROAMING
	int do_join_r;
#endif /* CONFIG_LAYER2_ROAMING */

	DBG_8192D("%s, fw_state=%x\n", __func__, get_fwstate(pmlmepriv));

	if (adapter->bDriverStopped || adapter->bSurpriseRemoved)
		return;

	spin_lock_bh(&pmlmepriv->lock);

#ifdef CONFIG_LAYER2_ROAMING
	if (rtw_to_roaming(adapter) > 0) {	/* join timeout caused by roaming */
		while (1) {
			pmlmepriv->to_roaming--;
			if (rtw_to_roaming(adapter) != 0) {	/* try another */
				DBG_8192D("%s try another roaming\n", __func__);
				do_join_r = rtw_do_join(adapter);
				if (_SUCCESS != do_join_r) {
					DBG_8192D
					    ("%s roaming do_join return %d\n",
					     __func__, do_join_r);
					continue;
				}
				break;
			} else {
				DBG_8192D("%s We've try roaming but fail\n",
					  __func__);
				rtw_indicate_disconnect(adapter);
				break;
			}
		}

	} else
#endif
	{
		rtw_indicate_disconnect(adapter);
		free_scanqueue(pmlmepriv);	/*  */
	}

	spin_unlock_bh(&pmlmepriv->lock);

#ifdef CONFIG_DRVEXT_MODULE_WSC
	drvext_assoc_fail_indicate(&adapter->drvextpriv);
#endif

}

/*
* rtw_scan_timeout_handler - Timeout/Faliure handler for CMD SiteSurvey
* @adapter: pointer to _adapter structure
*/
void rtw_scan_timeout_handler(struct rtw_adapter *adapter)
{
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;

	DBG_8192D(FUNC_ADPT_FMT " fw_state=%x\n", FUNC_ADPT_ARG(adapter),
		  get_fwstate(pmlmepriv));

	spin_lock_bh(&pmlmepriv->lock);

	_clr_fwstate_(pmlmepriv, _FW_UNDER_SURVEY);

	spin_unlock_bh(&pmlmepriv->lock);

	rtw_indicate_scan_done(adapter, true);
}

static void rtw_auto_scan_handler(struct rtw_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct pwrctrl_priv *pwrctrlpriv = &padapter->pwrctrlpriv;

	/* auto site survey per 60sec */
	if (pmlmepriv->scan_interval > 0) {
		pmlmepriv->scan_interval--;
		if (pmlmepriv->scan_interval == 0) {
#ifdef CONFIG_CONCURRENT_MODE
			if (rtw_buddy_adapter_up(padapter)) {
				if ((check_buddy_fwstate
				     (padapter,
				      _FW_UNDER_SURVEY | _FW_UNDER_LINKING) ==
				     true) ||
				     (padapter->pbuddy_adapter->mlmepriv.
					LinkDetectInfo.bBusyTraffic == true)) {
					DBG_8192D
					    ("%s, but buddy_intf is under scanning or linking or BusyTraffic\n",
					     __func__);
					return;
				}
			}
#endif

			DBG_8192D("%s\n", __func__);

			rtw_set_802_11_bssid_list_scan(padapter, NULL, 0);

			pmlmepriv->scan_interval = SCAN_INTERVAL;	/*  30*2 sec = 60sec */
		}
	}
}

void rtw_dynamic_check_timer_handlder(struct rtw_adapter *adapter)
{
#ifdef CONFIG_92D_AP_MODE
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
#endif /* CONFIG_92D_AP_MODE */
	struct registry_priv *pregistrypriv = &adapter->registrypriv;
#ifdef CONFIG_CONCURRENT_MODE
	struct rtw_adapter *pbuddy_adapter = adapter->pbuddy_adapter;
#endif

	if (!adapter)
		return;

	if (adapter->hw_init_completed == false)
		return;

	if ((adapter->bDriverStopped == true) ||
	    (adapter->bSurpriseRemoved == true))
		return;

#ifdef CONFIG_CONCURRENT_MODE
	if (pbuddy_adapter) {
		if (adapter->net_closed == true &&
		    pbuddy_adapter->net_closed == true) {
			return;
		}
	} else
#endif /* CONFIG_CONCURRENT_MODE */
	if (adapter->net_closed == true) {
		return;
	}

	rtw_dynamic_chk_wk_cmd(adapter);

	if (pregistrypriv->wifi_spec == 1) {
		/* auto site survey */
		rtw_auto_scan_handler(adapter);
	}
}

#ifdef CONFIG_SET_SCAN_DENY_TIMER
inline bool rtw_is_scan_deny(struct rtw_adapter *adapter)
{
	struct mlme_priv *mlmepriv = &adapter->mlmepriv;
	return (ATOMIC_READ(&mlmepriv->set_scan_deny) != 0) ? true : false;
}

inline void rtw_clear_scan_deny(struct rtw_adapter *adapter)
{
	struct mlme_priv *mlmepriv = &adapter->mlmepriv;
	ATOMIC_SET(&mlmepriv->set_scan_deny, 0);
}

void rtw_set_scan_deny_timer_hdl(struct rtw_adapter *adapter)
{
	rtw_clear_scan_deny(adapter);
}

void rtw_set_scan_deny(struct rtw_adapter *adapter, u32 ms)
{
	struct mlme_priv *mlmepriv = &adapter->mlmepriv;
#ifdef CONFIG_CONCURRENT_MODE
	struct mlme_priv *b_mlmepriv;
#endif
	ATOMIC_SET(&mlmepriv->set_scan_deny, 1);
	_set_timer(&mlmepriv->set_scan_deny_timer, ms);

#ifdef CONFIG_CONCURRENT_MODE
	if (!adapter->pbuddy_adapter)
		return;

	b_mlmepriv = &adapter->pbuddy_adapter->mlmepriv;
	ATOMIC_SET(&b_mlmepriv->set_scan_deny, 1);
	_set_timer(&b_mlmepriv->set_scan_deny_timer, ms);
#endif
}
#endif

#if defined(IEEE80211_SCAN_RESULT_EXPIRE)
#define RTW_SCAN_RESULT_EXPIRE (IEEE80211_SCAN_RESULT_EXPIRE/HZ*1000 - 1000)	/* 3000 -1000 */
#else
#define RTW_SCAN_RESULT_EXPIRE 2000
#endif

/*
* Select a new join candidate from the original @param candidate and @param competitor
* @return true: candidate is updated
* @return false: candidate is not updated
*/
static int rtw_check_join_candidate(struct mlme_priv *pmlmepriv,
				    struct wlan_network **candidate,
				    struct wlan_network *competitor)
{
	int updated = false;
	struct rtw_adapter *adapter =
	    container_of(pmlmepriv, struct rtw_adapter, mlmepriv);

	/* check bssid, if needed */
	if (pmlmepriv->assoc_by_bssid == true) {
		if (_rtw_memcmp
		    (competitor->network.MacAddress, pmlmepriv->assoc_bssid,
		     ETH_ALEN) == false)
			goto exit;
	}

	/* check ssid, if needed */
	if (pmlmepriv->assoc_ssid.Ssid && pmlmepriv->assoc_ssid.SsidLength) {
		if (competitor->network.Ssid.SsidLength !=
		    pmlmepriv->assoc_ssid.SsidLength ||
		    _rtw_memcmp(competitor->network.Ssid.Ssid,
				   pmlmepriv->assoc_ssid.Ssid,
				   pmlmepriv->assoc_ssid.SsidLength) == false)
			goto exit;
	}

	if (rtw_is_desired_network(adapter, competitor) == false)
		goto exit;

#ifdef CONFIG_LAYER2_ROAMING
	if (rtw_to_roaming(adapter) > 0) {
		if (rtw_get_passing_time_ms((u32) competitor->last_scanned) >=
		    RTW_SCAN_RESULT_EXPIRE ||
		    is_same_ess(&competitor->network,
				   &pmlmepriv->cur_network.network) == false)
			goto exit;
	}
#endif

	if (*candidate == NULL ||
	    (*candidate)->network.Rssi < competitor->network.Rssi) {
		*candidate = competitor;
		updated = true;
	}

	if (updated) {
		DBG_8192D("[by_bssid:%u][assoc_ssid:%s]"
			  "new candidate: %s(%pM) rssi:%d\n",
			  pmlmepriv->assoc_by_bssid, pmlmepriv->assoc_ssid.Ssid,
			  (*candidate)->network.Ssid.Ssid,
			  (*candidate)->network.MacAddress,
			  (int)(*candidate)->network.Rssi);
#ifdef CONFIG_LAYER2_ROAMING
		DBG_8192D("[to_roaming:%u]\n", rtw_to_roaming(adapter));
#endif
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
	struct list_head *phead;
	struct rtw_adapter *adapter;
	struct __queue *queue = &(pmlmepriv->scanned_queue);
	struct wlan_network *pnetwork = NULL;
	struct wlan_network *candidate = NULL;
	u8 bSupportAntDiv = false;

	spin_lock_bh(&(pmlmepriv->scanned_queue.lock));
	phead = get_list_head(queue);
	adapter = (struct rtw_adapter *)pmlmepriv->nic_hdl;

	pmlmepriv->pscanned = phead->next;

	while (!rtw_end_of_queue_search(phead, pmlmepriv->pscanned)) {
		pnetwork =
		    container_of(pmlmepriv->pscanned, struct wlan_network,
				   list);
		if (pnetwork == NULL) {
			RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
				 ("%s return _FAIL:(pnetwork==NULL)\n",
				  __func__));
			ret = _FAIL;
			goto exit;
		}

		pmlmepriv->pscanned = pmlmepriv->pscanned->next;

		rtw_check_join_candidate(pmlmepriv, &candidate, pnetwork);
	}

	if (candidate == NULL) {
		DBG_8192D("%s: return _FAIL(candidate == NULL)\n", __func__);
		ret = _FAIL;
		goto exit;
	} else {
		DBG_8192D("%s: candidate: %s(%pM, ch:%u)\n", __func__,
			  candidate->network.Ssid.Ssid,
			  candidate->network.MacAddress,
			  candidate->network.Configuration.DSConfig);
	}

	/*  check for situation of  _FW_LINKED */
	if (check_fwstate(pmlmepriv, _FW_LINKED) == true) {
		DBG_8192D("%s: _FW_LINKED while ask_for_joinbss!!!\n",
			  __func__);

		rtw_disassoc_cmd(adapter, 0, true);
		rtw_indicate_disconnect(adapter);
		rtw_free_assoc_resources(adapter, 0);
	}
#ifdef CONFIG_ANTENNA_DIVERSITY
	rtw_hal_get_def_var(adapter, HAL_DEF_IS_SUPPORT_ANT_DIV,
			    &(bSupportAntDiv));
	if (true == bSupportAntDiv) {
		u8 CurrentAntenna;
		rtw_hal_get_def_var(adapter, HAL_DEF_CURRENT_ANTENNA,
				    &(CurrentAntenna));
		DBG_8192D("#### Opt_Ant_(%s) , cur_Ant(%s)\n",
			  (2 ==
			   candidate->network.PhyInfo.
			   Optimum_antenna) ? "A" : "B",
			  (2 == CurrentAntenna) ? "A" : "B");
	}
#endif
	set_fwstate(pmlmepriv, _FW_UNDER_LINKING);
	ret = rtw_joinbss_cmd(adapter, candidate);

exit:
	spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));

	return ret;
}

int rtw_set_auth(struct rtw_adapter *adapter,
		 struct security_priv *psecuritypriv)
{
	struct cmd_obj *pcmd;
	struct setauth_parm *psetauthparm;
	struct cmd_priv *pcmdpriv = &(adapter->cmdpriv);
	int res = _SUCCESS;

	pcmd = (struct cmd_obj *)kzalloc(sizeof(struct cmd_obj), GFP_KERNEL);
	if (pcmd == NULL) {
		res = _FAIL;	/* try again */
		goto exit;
	}

	psetauthparm =
	    (struct setauth_parm *)kzalloc(sizeof(struct setauth_parm), GFP_KERNEL);
	if (psetauthparm == NULL) {
		kfree(pcmd);
		res = _FAIL;
		goto exit;
	}

	memset(psetauthparm, 0, sizeof(struct setauth_parm));
	psetauthparm->mode = (unsigned char)psecuritypriv->dot11AuthAlgrthm;

	pcmd->cmdcode = _SETAUTH_CMD_;
	pcmd->parmbuf = (unsigned char *)psetauthparm;
	pcmd->cmdsz = (sizeof(struct setauth_parm));
	pcmd->rsp = NULL;
	pcmd->rspsz = 0;

	INIT_LIST_HEAD(&pcmd->list);

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
		 ("after enqueue set_auth_cmd, auth_mode=%x\n",
		  psecuritypriv->dot11AuthAlgrthm));

	res = rtw_enqueue_cmd(pcmdpriv, pcmd);

exit:

	return res;
}

int rtw_set_key(struct rtw_adapter *adapter,
		struct security_priv *psecuritypriv, int keyid, u8 set_tx)
{
	u8 keylen;
	struct cmd_obj *pcmd;
	struct setkey_parm *psetkeyparm;
	struct cmd_priv *pcmdpriv = &(adapter->cmdpriv);
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);
	int res = _SUCCESS;

	pcmd = (struct cmd_obj *)kzalloc(sizeof(struct cmd_obj), GFP_KERNEL);
	if (pcmd == NULL) {
		res = _FAIL;	/* try again */
		goto exit;
	}
	psetkeyparm =
	    (struct setkey_parm *)kzalloc(sizeof(struct setkey_parm), GFP_KERNEL);
	if (psetkeyparm == NULL) {
		kfree(pcmd);
		res = _FAIL;
		goto exit;
	}

	memset(psetkeyparm, 0, sizeof(struct setkey_parm));

	if (psecuritypriv->dot11AuthAlgrthm == dot11AuthAlgrthm_8021X) {
		psetkeyparm->algorithm =
		    (unsigned char)psecuritypriv->dot118021XGrpPrivacy;
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
			 ("\n rtw_set_key: psetkeyparm->algorithm=(unsigned char)psecuritypriv->dot118021XGrpPrivacy=%d\n",
			  psetkeyparm->algorithm));
	} else {
		psetkeyparm->algorithm =
		    (u8) psecuritypriv->dot11PrivacyAlgrthm;
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
			 ("\n rtw_set_key: psetkeyparm->algorithm=(u8)psecuritypriv->dot11PrivacyAlgrthm=%d\n",
			  psetkeyparm->algorithm));
	}
	psetkeyparm->keyid = (u8) keyid;	/* 0~3 */
	psetkeyparm->set_tx = set_tx;
	pmlmepriv->key_mask |= BIT(psetkeyparm->keyid);
#ifdef CONFIG_AUTOSUSPEND
	if (true == adapter->pwrctrlpriv.bInternalAutoSuspend) {
		adapter->pwrctrlpriv.wepkeymask = pmlmepriv->key_mask;
		DBG_8192D("....AutoSuspend pwrctrlpriv.wepkeymask(%x)\n",
			  adapter->pwrctrlpriv.wepkeymask);
	}
#endif
	DBG_8192D("==> rtw_set_key algorithm(%x),keyid(%x),key_mask(%x)\n",
		  psetkeyparm->algorithm, psetkeyparm->keyid,
		  pmlmepriv->key_mask);
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
		 ("\n rtw_set_key: psetkeyparm->algorithm=%d psetkeyparm->keyid=(u8)keyid=%d\n",
		  psetkeyparm->algorithm, keyid));

	switch (psetkeyparm->algorithm) {
	case _WEP40_:
		keylen = 5;
		memcpy(&(psetkeyparm->key[0]),
		       &(psecuritypriv->dot11DefKey[keyid].skey[0]), keylen);
		break;
	case _WEP104_:
		keylen = 13;
		memcpy(&(psetkeyparm->key[0]),
		       &(psecuritypriv->dot11DefKey[keyid].skey[0]), keylen);
		break;
	case _TKIP_:
		keylen = 16;
		memcpy(&psetkeyparm->key,
		       &psecuritypriv->dot118021XGrpKey[keyid], keylen);
		psetkeyparm->grpkey = 1;
		break;
	case _AES_:
		keylen = 16;
		memcpy(&psetkeyparm->key,
		       &psecuritypriv->dot118021XGrpKey[keyid], keylen);
		psetkeyparm->grpkey = 1;
		break;
	default:
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
			 ("\n rtw_set_key:psecuritypriv->dot11PrivacyAlgrthm = %x (must be 1 or 2 or 4 or 5)\n",
			  psecuritypriv->dot11PrivacyAlgrthm));
		res = _FAIL;
		goto exit;
	}

	pcmd->cmdcode = _SETKEY_CMD_;
	pcmd->parmbuf = (u8 *)psetkeyparm;
	pcmd->cmdsz = (sizeof(struct setkey_parm));
	pcmd->rsp = NULL;
	pcmd->rspsz = 0;

	INIT_LIST_HEAD(&pcmd->list);

	/* _rtw_init_sema(&(pcmd->cmd_sem), 0); */

	res = rtw_enqueue_cmd(pcmdpriv, pcmd);

exit:

	return res;
}

/* adjust IEs for rtw_joinbss_cmd in WMM */
int rtw_restruct_wmm_ie(struct rtw_adapter *adapter, u8 *in_ie, u8 *out_ie,
			uint in_len, uint initial_out_len)
{
	unsigned int ielength = 0;
	unsigned int i, j;

	i = 12;			/* after the fixed IE */
	while (i < in_len) {
		ielength = initial_out_len;

		if (in_ie[i] == 0xDD && in_ie[i + 2] == 0x00 && in_ie[i + 3] == 0x50 && in_ie[i + 4] == 0xF2 && in_ie[i + 5] == 0x02 && i + 5 < in_len) {	/* WMM element ID and OUI */
			/* Append WMM IE to the last index of out_ie */
			for (j = i; j < i + 9; j++) {
				out_ie[ielength] = in_ie[j];
				ielength++;
			}
			out_ie[initial_out_len + 1] = 0x07;
			out_ie[initial_out_len + 6] = 0x00;
			out_ie[initial_out_len + 8] = 0x00;

			break;
		}

		i += (in_ie[i + 1] + 2);	/*  to the next IE element */
	}

	return ielength;
}

/*  */
/*  Ported from 8185: IsInPreAuthKeyList(). (Renamed from SecIsInPreAuthKeyList(), 2006-10-13.) */
/*  Added by Annie, 2006-05-07. */
/*  */
/*  Search by BSSID, */
/*  Return Value: */
/*		-1		:if there is no pre-auth key in the  table */
/*		>=0		:if there is pre-auth key, and   return the entry id */
/*  */
/*  */

static int SecIsInPMKIDList(struct rtw_adapter *adapter, u8 *bssid)
{
	struct security_priv *psecuritypriv = &adapter->securitypriv;
	int i = 0;

	do {
		if ((psecuritypriv->PMKIDList[i].bUsed) &&
		    (_rtw_memcmp
		     (psecuritypriv->PMKIDList[i].Bssid, bssid,
		      ETH_ALEN) == true)) {
			break;
		} else {
			i++;
			/* continue; */
		}

	} while (i < NUM_PMKID_CACHE);

	if (i == NUM_PMKID_CACHE) {
		i = -1;		/*  Could not find. */
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

static int rtw_append_pmkid(struct rtw_adapter *adapter, int iEntry, u8 *ie,
			    uint ie_len)
{
	struct security_priv *psecuritypriv = &adapter->securitypriv;

	if (ie[13] <= 20) {
		/*  The RSN IE didn't include the PMK ID, append the PMK information */
		ie[ie_len] = 1;
		ie_len++;
		ie[ie_len] = 0;	/* PMKID count = 0x0100 */
		ie_len++;
		memcpy(&ie[ie_len], &psecuritypriv->PMKIDList[iEntry].PMKID,
		       16);

		ie_len += 16;
		ie[13] += 18;	/* PMKID length = 2+16 */
	}
	return ie_len;
}

int rtw_restruct_sec_ie(struct rtw_adapter *adapter, u8 *in_ie, u8 *out_ie,
			uint in_len)
{
	u8 authmode, securitytype, match;
	u8 sec_ie[255], uncst_oui[4], bkup_ie[255];
	u8 wpa_oui[4] = { 0x0, 0x50, 0xf2, 0x01 };
	uint ielength, cnt, remove_cnt;
	int iEntry;

	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct security_priv *psecuritypriv = &adapter->securitypriv;
	uint ndisauthmode = psecuritypriv->ndisauthtype;
	uint ndissecuritytype = psecuritypriv->ndisencryptstatus;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_,
		 ("+rtw_restruct_sec_ie: ndisauthmode=%d ndissecuritytype=%d\n",
		  ndisauthmode, ndissecuritytype));

	/* copy fixed ie only */
	memcpy(out_ie, in_ie, 12);
	ielength = 12;
	if ((ndisauthmode == NDIS802_11AUTHMODEWPA) ||
	    (ndisauthmode == NDIS802_11AUTHMODEWPAPSK))
		authmode = _WPA_IE_ID_;
	if ((ndisauthmode == NDIS802_11AUTHMODEWPA2) ||
	    (ndisauthmode == NDIS802_11AUTHMODEWPA2PSK))
		authmode = _WPA2_IE_ID_;

	if (check_fwstate(pmlmepriv, WIFI_UNDER_WPS)) {
		memcpy(out_ie + ielength, psecuritypriv->wps_ie,
		       psecuritypriv->wps_ie_len);

		ielength += psecuritypriv->wps_ie_len;
	} else if ((authmode == _WPA_IE_ID_) || (authmode == _WPA2_IE_ID_)) {
		/* copy RSN or SSN */
		memcpy(&out_ie[ielength], &psecuritypriv->supplicant_ie[0],
		       psecuritypriv->supplicant_ie[1] + 2);
		ielength += psecuritypriv->supplicant_ie[1] + 2;
		rtw_report_sec_ie(adapter, authmode,
				  psecuritypriv->supplicant_ie);

#ifdef CONFIG_DRVEXT_MODULE
		drvext_report_sec_ie(&adapter->drvextpriv, authmode, sec_ie);
#endif
	}

	iEntry = SecIsInPMKIDList(adapter, pmlmepriv->assoc_bssid);
	if (iEntry < 0) {
		return ielength;
	} else {
		if (authmode == _WPA2_IE_ID_) {
			ielength =
			    rtw_append_pmkid(adapter, iEntry, out_ie, ielength);
		}
	}

	return ielength;
}

void rtw_init_registrypriv_dev_network(struct rtw_adapter *adapter)
{
	struct registry_priv *pregistrypriv = &adapter->registrypriv;
	struct eeprom_priv *peepriv = &adapter->eeprompriv;
	struct wlan_bssid_ex *pdev_network = &pregistrypriv->dev_network;
	u8 *myhwaddr = myid(peepriv);

	memcpy(pdev_network->MacAddress, myhwaddr, ETH_ALEN);

	memcpy(&pdev_network->Ssid, &pregistrypriv->ssid,
	       sizeof(struct ndis_802_11_ssid));

	pdev_network->Configuration.Length = sizeof(struct ndis_802_11_config);
	pdev_network->Configuration.BeaconPeriod = 100;
	pdev_network->Configuration.FHConfig.Length = 0;
	pdev_network->Configuration.FHConfig.HopPattern = 0;
	pdev_network->Configuration.FHConfig.HopSet = 0;
	pdev_network->Configuration.FHConfig.DwellTime = 0;

}

void rtw_update_registrypriv_dev_network(struct rtw_adapter *adapter)
{
	int sz = 0;
	struct registry_priv *pregistrypriv = &adapter->registrypriv;
	struct wlan_bssid_ex *pdev_network = &pregistrypriv->dev_network;
	struct security_priv *psecuritypriv = &adapter->securitypriv;
	struct wlan_network *cur_network = &adapter->mlmepriv.cur_network;
	/* struct       xmit_priv       *pxmitpriv = &adapter->xmitpriv; */

	pdev_network->Privacy = (psecuritypriv->dot11PrivacyAlgrthm > 0 ? 1 : 0);	/*  adhoc no 802.1x */

	pdev_network->Rssi = 0;

	switch (pregistrypriv->wireless_mode) {
	case WIRELESS_11B:
		pdev_network->NetworkTypeInUse = (NDIS802_11DS);
		break;
	case WIRELESS_11G:
	case WIRELESS_11BG:
	case WIRELESS_11_24N:
	case WIRELESS_11G_24N:
	case WIRELESS_11BG_24N:
		pdev_network->NetworkTypeInUse = (NDIS802_11OFDM24);
		break;
	case WIRELESS_11A:
	case WIRELESS_11A_5N:
		pdev_network->NetworkTypeInUse = (NDIS802_11OFDM5);
		break;
	case WIRELESS_11ABGN:
		if (pregistrypriv->channel > 14)
			pdev_network->NetworkTypeInUse = (NDIS802_11OFDM5);
		else
			pdev_network->NetworkTypeInUse = (NDIS802_11OFDM24);
		break;
	default:
		/*  TODO */
		break;
	}

	pdev_network->Configuration.DSConfig = (pregistrypriv->channel);
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
		 ("pregistrypriv->channel=%d, pdev_network->Configuration.DSConfig=0x%x\n",
		  pregistrypriv->channel,
		  pdev_network->Configuration.DSConfig));

	if (cur_network->network.InfrastructureMode == NDIS802_11IBSS)
		pdev_network->Configuration.ATIMWindow = (0);

	pdev_network->InfrastructureMode =
	    (cur_network->network.InfrastructureMode);

	/*  1. Supported rates */
	/*  2. IE */

	sz = rtw_generate_ie(pregistrypriv);

	pdev_network->IELength = sz;

	pdev_network->Length =
	    get_wlan_bssid_ex_sz((struct wlan_bssid_ex *)pdev_network);

}

/* the fucntion is at passive_level */
void rtw_joinbss_reset(struct rtw_adapter *padapter)
{
	u8 threshold;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

#ifdef CONFIG_80211N_HT
	struct ht_priv *phtpriv = &pmlmepriv->htpriv;
#endif

	/* todo: if you want to do something io/reg/hw setting before join_bss, please add code here */

#ifdef CONFIG_80211N_HT

	pmlmepriv->num_FortyMHzIntolerant = 0;

	pmlmepriv->num_sta_no_ht = 0;

	phtpriv->ampdu_enable = false;	/* reset to disabled */

	/*  TH=1 => means that invalidate usb rx aggregation */
	/*  TH=0 => means that validate usb rx aggregation, use init value. */
	if (phtpriv->ht_option) {
		if (padapter->registrypriv.wifi_spec == 1)
			threshold = 1;
		else
			threshold = 0;
		rtw_hal_set_hwreg(padapter, HW_VAR_RXDMA_AGG_PG_TH,
				  (u8 *)(&threshold));
	} else {
		threshold = 1;
		rtw_hal_set_hwreg(padapter, HW_VAR_RXDMA_AGG_PG_TH,
				  (u8 *)(&threshold));
	}
#endif
}

#ifdef CONFIG_80211N_HT

/* the fucntion is >= passive_level */
unsigned int rtw_restructure_ht_ie(struct rtw_adapter *padapter, u8 *in_ie,
				   u8 *out_ie, uint in_len, uint *pout_len,
				   u8 channel)
{
	u32 ielen, out_len;
	unsigned char *p, *pframe;
	struct rtw_ieee80211_ht_cap ht_capie;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct qos_priv *pqospriv = &pmlmepriv->qospriv;
	struct ht_priv *phtpriv = &pmlmepriv->htpriv;
	struct registry_priv *pregpriv = &padapter->registrypriv;
	u8 cbw40_enable = 0;
 	unsigned char WMM_IE[] = {0x00, 0x50, 0xf2, 0x02, 0x00, 0x01, 0x00};

	phtpriv->ht_option = false;

	p = rtw_get_ie(in_ie + 12, _HT_CAPABILITY_IE_, &ielen, in_len - 12);

	if (p && ielen > 0) {
		if (pqospriv->qos_option == 0) {
			out_len = *pout_len;
			pframe =
			    rtw_set_ie(out_ie + out_len, _VENDOR_SPECIFIC_IE_,
				       _WMM_IE_Length_, WMM_IE, pout_len);

			pqospriv->qos_option = 1;
		}

		out_len = *pout_len;

		memset(&ht_capie, 0, sizeof(struct rtw_ieee80211_ht_cap));

		ht_capie.cap_info =
		    IEEE80211_HT_CAP_SGI_20 | IEEE80211_HT_CAP_TX_STBC |
		    IEEE80211_HT_CAP_DSSSCCK40;
		/* if insert module set only support 20MHZ, don't add the 40MHZ and SGI_40 */
		if (channel > 14) {
			if (pregpriv->cbw40_enable & BIT(1))
				cbw40_enable = 1;
		} else if (pregpriv->cbw40_enable & BIT(0)) {
			cbw40_enable = 1;
		}

		if (cbw40_enable != 0)
			ht_capie.cap_info |=
			    IEEE80211_HT_CAP_SUP_WIDTH |
			    IEEE80211_HT_CAP_SGI_40;

		{
			u32 rx_packet_offset, max_recvbuf_sz;
			rtw_hal_get_def_var(padapter, HAL_DEF_RX_PACKET_OFFSET,
					    &rx_packet_offset);
			rtw_hal_get_def_var(padapter, HAL_DEF_MAX_RECVBUF_SZ,
					    &max_recvbuf_sz);
		}

		ht_capie.ampdu_params_info =
		    (IEEE80211_HT_CAP_AMPDU_FACTOR & 0x03);

		if (padapter->securitypriv.dot11PrivacyAlgrthm == _AES_)
			ht_capie.ampdu_params_info |=
			    (IEEE80211_HT_CAP_AMPDU_DENSITY & (0x07 << 2));
		else
			ht_capie.ampdu_params_info |=
			    (IEEE80211_HT_CAP_AMPDU_DENSITY & 0x00);

		pframe = rtw_set_ie(out_ie + out_len, _HT_CAPABILITY_IE_,
				    sizeof(struct rtw_ieee80211_ht_cap),
				    (unsigned char *)&ht_capie, pout_len);
		phtpriv->ht_option = true;

		p = rtw_get_ie(in_ie + 12, _HT_ADD_INFO_IE_, &ielen,
			       in_len - 12);
		if (p && (ielen == sizeof(struct ieee80211_ht_addt_info))) {
			out_len = *pout_len;
			pframe =
			    rtw_set_ie(out_ie + out_len, _HT_ADD_INFO_IE_,
				       ielen, p + 2, pout_len);
		}
	}
	return phtpriv->ht_option;
}

/* the function is > passive_level (in critical_section) */
void rtw_update_ht_cap(struct rtw_adapter *padapter, u8 *pie, uint ie_len,
		       u8 channel)
{
	u8 *p, max_ampdu_sz;
	int len;
	/* struct sta_info *bmc_sta, *psta; */
	struct rtw_ieee80211_ht_cap *pht_capie;
	/* struct recv_reorder_ctrl *preorder_ctrl; */
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct ht_priv *phtpriv = &pmlmepriv->htpriv;
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

	DBG_8192D("+rtw_update_ht_cap()\n");

	/* maybe needs check if ap supports rx ampdu. */
	if ((phtpriv->ampdu_enable == false) &&
	    (pregistrypriv->ampdu_enable == 1)) {
		if (pregistrypriv->wifi_spec == 1) {
			phtpriv->ampdu_enable = false;
		} else {
			phtpriv->ampdu_enable = true;
		}
	} else if (pregistrypriv->ampdu_enable == 2) {
		phtpriv->ampdu_enable = true;
	}

	/* check Max Rx A-MPDU Size */
	len = 0;
	p = rtw_get_ie(pie + sizeof(struct ndis_802_11_fixed_ies),
		       _HT_CAPABILITY_IE_, &len,
		       ie_len - sizeof(struct ndis_802_11_fixed_ies));
	if (p && len > 0) {
		pht_capie = (struct rtw_ieee80211_ht_cap *)(p + 2);
		max_ampdu_sz =
		    (pht_capie->
		     ampdu_params_info & IEEE80211_HT_CAP_AMPDU_FACTOR);
		max_ampdu_sz = 1 << (max_ampdu_sz + 3);	/*  max_ampdu_sz (kbytes); */

		/* DBG_8192D("rtw_update_ht_cap(): max_ampdu_sz=%d\n", max_ampdu_sz); */
		phtpriv->rx_ampdu_maxlen = max_ampdu_sz;
	}

	len = 0;
	p = rtw_get_ie(pie + sizeof(struct ndis_802_11_fixed_ies),
		       _HT_ADD_INFO_IE_, &len,
		       ie_len - sizeof(struct ndis_802_11_fixed_ies));

	if (channel > 14) {
		if (pregistrypriv->cbw40_enable & BIT(1))
			cbw40_enable = 1;
	} else if (pregistrypriv->cbw40_enable & BIT(0)) {
		cbw40_enable = 1;
	}

	/* update cur_bwmode & cur_ch_offset */
	if ((cbw40_enable) &&
	    (le16_to_cpu(pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info) & BIT(1)) &&
	    (pmlmeinfo->HT_info.infos[0] & BIT(2))) {
		int i;
		u8 rf_type;

		rtw_hal_get_hwreg(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));

		/* update the MCS rates */
		for (i = 0; i < 16; i++) {
			if ((rf_type == RF_1T1R) || (rf_type == RF_1T2R)) {
				pmlmeinfo->HT_caps.u.HT_cap_element.
				    MCS_rate[i] &= MCS_rate_1R[i];
			} else {
				pmlmeinfo->HT_caps.u.HT_cap_element.
				    MCS_rate[i] &= MCS_rate_2R[i];
			}
			if (pregistrypriv->special_rf_path)
				pmlmeinfo->HT_caps.u.HT_cap_element.
				    MCS_rate[i] &= MCS_rate_1R[i];
		}
		/* switch to the 40M Hz mode accoring to the AP */
		pmlmeext->cur_bwmode = HT_CHANNEL_WIDTH_40;
		switch ((pmlmeinfo->HT_info.infos[0] & 0x3)) {
		case HT_EXTCHNL_OFFSET_UPPER:
			pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_LOWER;
			break;

		case HT_EXTCHNL_OFFSET_LOWER:
			pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_UPPER;
			break;

		default:
			pmlmeext->cur_ch_offset =
			    HAL_PRIME_CHNL_OFFSET_DONT_CARE;
			break;
		}
	}

	/*  */
	/*  Config SM Power Save setting */
	/*  */
	pmlmeinfo->SM_PS =
	    (le16_to_cpu(pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info) & 0x0C) >> 2;
	if (pmlmeinfo->SM_PS == WLAN_HT_CAP_SM_PS_STATIC) {
		DBG_8192D("%s(): WLAN_HT_CAP_SM_PS_STATIC\n", __func__);
	}

	/*  */
	/*  Config current HT Protection mode. */
	/*  */
	pmlmeinfo->HT_protection = pmlmeinfo->HT_info.infos[1] & 0x3;
}

void rtw_issue_addbareq_cmd(struct rtw_adapter *padapter,
			    struct xmit_frame *pxmitframe)
{
	u8 issued;
	int priority;
	struct sta_info *psta = NULL;
	struct ht_priv *phtpriv;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	s32 bmcst = IS_MCAST(pattrib->ra);

	if (bmcst
	    || (padapter->mlmepriv.LinkDetectInfo.bTxBusyTraffic == false))
		return;

	priority = pattrib->priority;

	if (pattrib->psta)
		psta = pattrib->psta;
	else
		psta = rtw_get_stainfo(&padapter->stapriv, pattrib->ra);

	if (psta == NULL)
		return;

	phtpriv = &psta->htpriv;

	if ((phtpriv->ht_option == true) && (phtpriv->ampdu_enable == true)) {
		issued = (phtpriv->agg_enable_bitmap >> priority) & 0x1;
		issued |= (phtpriv->candidate_tid_bitmap >> priority) & 0x1;

		if (0 == issued) {
			DBG_8192D("rtw_issue_addbareq_cmd, p=%d\n", priority);
			psta->htpriv.candidate_tid_bitmap |= BIT((u8) priority);
			rtw_addbareq_cmd(padapter, (u8) priority, pattrib->ra);
		}
	}
}

#endif

#ifdef CONFIG_LAYER2_ROAMING
inline void rtw_set_roaming(struct rtw_adapter *adapter, u8 to_roaming)
{
	if (to_roaming == 0)
		adapter->mlmepriv.to_join = false;
	adapter->mlmepriv.to_roaming = to_roaming;
}

inline u8 rtw_to_roaming(struct rtw_adapter *adapter)
{
	return adapter->mlmepriv.to_roaming;
}

void rtw_roaming(struct rtw_adapter *padapter, struct wlan_network *tgt_network)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	spin_lock_bh(&pmlmepriv->lock);
	_rtw_roaming(padapter, tgt_network);
	spin_unlock_bh(&pmlmepriv->lock);
}

void _rtw_roaming(struct rtw_adapter *padapter,
		  struct wlan_network *tgt_network)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	int do_join_r;

	struct wlan_network *pnetwork;

	if (tgt_network != NULL)
		pnetwork = tgt_network;
	else
		pnetwork = &pmlmepriv->cur_network;

	if (0 < rtw_to_roaming(padapter)) {
		DBG_8192D("roaming from %s(%pM), length:%d\n",
			  pnetwork->network.Ssid.Ssid,
			  pnetwork->network.MacAddress,
			  pnetwork->network.Ssid.SsidLength);
		memcpy(&pmlmepriv->assoc_ssid, &pnetwork->network.Ssid,
		       sizeof(struct ndis_802_11_ssid));

		pmlmepriv->assoc_by_bssid = false;

		while (1) {
			do_join_r = rtw_do_join(padapter);
			if (_SUCCESS == do_join_r) {
				break;
			} else {
				DBG_8192D("roaming do_join return %d\n",
					  do_join_r);
				pmlmepriv->to_roaming--;

				if (0 < rtw_to_roaming(padapter)) {
					continue;
				} else {
					DBG_8192D
					    ("%s(%d) -to roaming fail, indicate_disconnect\n",
					     __func__, __LINE__);
					rtw_indicate_disconnect(padapter);
					break;
				}
			}
		}
	}
}
#endif

#ifdef CONFIG_CONCURRENT_MODE
int rtw_buddy_adapter_up(struct rtw_adapter *padapter)
{
	int res = false;

	if (padapter == NULL)
		return res;

	if (padapter->pbuddy_adapter == NULL) {
		res = false;
	} else if ((padapter->pbuddy_adapter->bDriverStopped) ||
		   (padapter->pbuddy_adapter->bSurpriseRemoved) ||
		   (padapter->pbuddy_adapter->bup == false) ||
		   (padapter->pbuddy_adapter->hw_init_completed == false)) {
		res = false;
	} else {
		res = true;
	}

	return res;
}

int check_buddy_fwstate(struct rtw_adapter *padapter, int state)
{
	if (padapter == NULL)
		return false;

	if (padapter->pbuddy_adapter == NULL)
		return false;

	if ((state == WIFI_FW_NULL_STATE) &&
	    (padapter->pbuddy_adapter->mlmepriv.fw_state == WIFI_FW_NULL_STATE))
		return true;

	if (padapter->pbuddy_adapter->mlmepriv.fw_state & state)
		return true;

	return false;
}
#endif /* CONFIG_CONCURRENT_MODE */
