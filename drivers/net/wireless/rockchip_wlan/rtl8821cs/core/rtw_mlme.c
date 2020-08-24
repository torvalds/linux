/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2019 Realtek Corporation.
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
 *****************************************************************************/
#define _RTW_MLME_C_

#include <hal_data.h>

extern void indicate_wx_scan_complete_event(_adapter *padapter);
extern u8 rtw_do_join(_adapter *padapter);


void rtw_init_mlme_timer(_adapter *padapter)
{
	struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;

	rtw_init_timer(&(pmlmepriv->assoc_timer), padapter, rtw_join_timeout_handler, padapter);
	rtw_init_timer(&(pmlmepriv->scan_to_timer), padapter, rtw_scan_timeout_handler, padapter);

#ifdef CONFIG_SET_SCAN_DENY_TIMER
	rtw_init_timer(&(pmlmepriv->set_scan_deny_timer), padapter, rtw_set_scan_deny_timer_hdl, padapter);
#endif

#ifdef RTK_DMP_PLATFORM
	_init_workitem(&(pmlmepriv->Linkup_workitem), Linkup_workitem_callback, padapter);
	_init_workitem(&(pmlmepriv->Linkdown_workitem), Linkdown_workitem_callback, padapter);
#endif
}

sint	_rtw_init_mlme_priv(_adapter *padapter)
{
	sint	i;
	u8	*pbuf;
	struct wlan_network	*pnetwork;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct rf_ctl_t *rfctl = adapter_to_rfctl(padapter);
	sint	res = _SUCCESS;


	/* We don't need to memset padapter->XXX to zero, because adapter is allocated by rtw_zvmalloc(). */
	/* _rtw_memset((u8 *)pmlmepriv, 0, sizeof(struct mlme_priv)); */


	/*qos_priv*/
	/*pmlmepriv->qospriv.qos_option = pregistrypriv->wmm_enable;*/

	/*ht_priv*/
#ifdef CONFIG_80211N_HT
	pmlmepriv->htpriv.ampdu_enable = _FALSE;/*set to disabled*/
#endif

	pmlmepriv->nic_hdl = (u8 *)padapter;

	pmlmepriv->pscanned = NULL;
	init_fwstate(pmlmepriv, WIFI_STATION_STATE);
	pmlmepriv->cur_network.network.InfrastructureMode = Ndis802_11AutoUnknown;
	pmlmepriv->scan_mode = SCAN_ACTIVE; /* 1: active, 0: pasive. Maybe someday we should rename this varable to "active_mode" (Jeff) */

	_rtw_spinlock_init(&(pmlmepriv->lock));
	_rtw_init_queue(&(pmlmepriv->free_bss_pool));
	_rtw_init_queue(&(pmlmepriv->scanned_queue));

	set_scanned_network_val(pmlmepriv, 0);

	_rtw_memset(&pmlmepriv->assoc_ssid, 0, sizeof(NDIS_802_11_SSID));

	if (padapter->registrypriv.max_bss_cnt != 0)
		pmlmepriv->max_bss_cnt = padapter->registrypriv.max_bss_cnt;
	else if (rfctl->max_chan_nums <= MAX_CHANNEL_NUM_2G)
		pmlmepriv->max_bss_cnt = MAX_BSS_CNT;
	else
		pmlmepriv->max_bss_cnt = MAX_BSS_CNT + MAX_BSS_CNT;


	pbuf = rtw_zvmalloc(pmlmepriv->max_bss_cnt * (sizeof(struct wlan_network)));

	if (pbuf == NULL) {
		res = _FAIL;
		goto exit;
	}
	pmlmepriv->free_bss_buf = pbuf;

	pnetwork = (struct wlan_network *)pbuf;

	for (i = 0; i < pmlmepriv->max_bss_cnt; i++) {
		_rtw_init_listhead(&(pnetwork->list));

		rtw_list_insert_tail(&(pnetwork->list), &(pmlmepriv->free_bss_pool.queue));

		pnetwork++;
	}

	/* allocate DMA-able/Non-Page memory for cmd_buf and rsp_buf */

	rtw_clear_scan_deny(padapter);
#ifdef CONFIG_ARP_KEEP_ALIVE
	pmlmepriv->bGetGateway = 0;
	pmlmepriv->GetGatewayTryCnt = 0;
#endif

#ifdef CONFIG_LAYER2_ROAMING
#define RTW_ROAM_SCAN_RESULT_EXP_MS (5*1000)
#define RTW_ROAM_RSSI_DIFF_TH 10
#define RTW_ROAM_SCAN_INTERVAL (5)    /* 5*(2 second)*/
#define RTW_ROAM_RSSI_THRESHOLD 70

	pmlmepriv->roam_flags = 0
				| RTW_ROAM_ON_EXPIRED
#ifdef CONFIG_LAYER2_ROAMING_RESUME
				| RTW_ROAM_ON_RESUME
#endif
#ifdef CONFIG_LAYER2_ROAMING_ACTIVE
				| RTW_ROAM_ACTIVE
#endif
				;

	pmlmepriv->roam_scanr_exp_ms = RTW_ROAM_SCAN_RESULT_EXP_MS;
	pmlmepriv->roam_rssi_diff_th = RTW_ROAM_RSSI_DIFF_TH;
	pmlmepriv->roam_scan_int 	 = RTW_ROAM_SCAN_INTERVAL;
	pmlmepriv->roam_rssi_threshold = RTW_ROAM_RSSI_THRESHOLD;
	pmlmepriv->need_to_roam = _FALSE;
	pmlmepriv->last_roaming = rtw_get_current_time();
#endif /* CONFIG_LAYER2_ROAMING */

#ifdef CONFIG_RTW_80211R
	rtw_ft_info_init(&pmlmepriv->ft_roam);
#endif
#ifdef CONFIG_LAYER2_ROAMING
#if defined(CONFIG_RTW_WNM) || defined(CONFIG_RTW_80211K)
	rtw_roam_nb_info_init(padapter);
	pmlmepriv->ch_cnt = 0;
#endif	
#endif

	pmlmepriv->defs_lmt_sta = 2;
	pmlmepriv->defs_lmt_time = 5;

	rtw_init_mlme_timer(padapter);

exit:


	return res;
}

void rtw_mfree_mlme_priv_lock(struct mlme_priv *pmlmepriv);
void rtw_mfree_mlme_priv_lock(struct mlme_priv *pmlmepriv)
{
	_rtw_spinlock_free(&pmlmepriv->lock);
	_rtw_spinlock_free(&(pmlmepriv->free_bss_pool.lock));
	_rtw_spinlock_free(&(pmlmepriv->scanned_queue.lock));
}

static void rtw_free_mlme_ie_data(u8 **ppie, u32 *plen)
{
	if (*ppie) {
		rtw_mfree(*ppie, *plen);
		*plen = 0;
		*ppie = NULL;
	}
}

void rtw_free_mlme_priv_ie_data(struct mlme_priv *pmlmepriv)
{
#if defined(CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)
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
	rtw_free_mlme_ie_data(&pmlmepriv->p2p_assoc_resp_ie, &pmlmepriv->p2p_assoc_resp_ie_len);
#endif

#if defined(CONFIG_WFD) && defined(CONFIG_IOCTL_CFG80211)
	rtw_free_mlme_ie_data(&pmlmepriv->wfd_beacon_ie, &pmlmepriv->wfd_beacon_ie_len);
	rtw_free_mlme_ie_data(&pmlmepriv->wfd_probe_req_ie, &pmlmepriv->wfd_probe_req_ie_len);
	rtw_free_mlme_ie_data(&pmlmepriv->wfd_probe_resp_ie, &pmlmepriv->wfd_probe_resp_ie_len);
	rtw_free_mlme_ie_data(&pmlmepriv->wfd_go_probe_resp_ie, &pmlmepriv->wfd_go_probe_resp_ie_len);
	rtw_free_mlme_ie_data(&pmlmepriv->wfd_assoc_req_ie, &pmlmepriv->wfd_assoc_req_ie_len);
	rtw_free_mlme_ie_data(&pmlmepriv->wfd_assoc_resp_ie, &pmlmepriv->wfd_assoc_resp_ie_len);
#endif

#ifdef CONFIG_RTW_80211R
	rtw_free_mlme_ie_data(&pmlmepriv->auth_rsp, &pmlmepriv->auth_rsp_len);
#endif
}

#if defined(CONFIG_WFD) && defined(CONFIG_IOCTL_CFG80211)
int rtw_mlme_update_wfd_ie_data(struct mlme_priv *mlme, u8 type, u8 *ie, u32 ie_len)
{
	_adapter *adapter = mlme_to_adapter(mlme);
	struct wifi_display_info *wfd_info = &adapter->wfd_info;
	u8 clear = 0;
	u8 **t_ie = NULL;
	u32 *t_ie_len = NULL;
	int ret = _FAIL;

	if (!hal_chk_wl_func(adapter, WL_FUNC_MIRACAST))
		goto success;

	if (wfd_info->wfd_enable == _TRUE)
		goto success; /* WFD IE is build by self */

	if (!ie && !ie_len)
		clear = 1;
	else if (!ie || !ie_len) {
		RTW_PRINT(FUNC_ADPT_FMT" type:%u, ie:%p, ie_len:%u"
			  , FUNC_ADPT_ARG(adapter), type, ie, ie_len);
		rtw_warn_on(1);
		goto exit;
	}

	switch (type) {
	case MLME_BEACON_IE:
		t_ie = &mlme->wfd_beacon_ie;
		t_ie_len = &mlme->wfd_beacon_ie_len;
		break;
	case MLME_PROBE_REQ_IE:
		t_ie = &mlme->wfd_probe_req_ie;
		t_ie_len = &mlme->wfd_probe_req_ie_len;
		break;
	case MLME_PROBE_RESP_IE:
		t_ie = &mlme->wfd_probe_resp_ie;
		t_ie_len = &mlme->wfd_probe_resp_ie_len;
		break;
	case MLME_GO_PROBE_RESP_IE:
		t_ie = &mlme->wfd_go_probe_resp_ie;
		t_ie_len = &mlme->wfd_go_probe_resp_ie_len;
		break;
	case MLME_ASSOC_REQ_IE:
		t_ie = &mlme->wfd_assoc_req_ie;
		t_ie_len = &mlme->wfd_assoc_req_ie_len;
		break;
	case MLME_ASSOC_RESP_IE:
		t_ie = &mlme->wfd_assoc_resp_ie;
		t_ie_len = &mlme->wfd_assoc_resp_ie_len;
		break;
	default:
		RTW_PRINT(FUNC_ADPT_FMT" unsupported type:%u"
			  , FUNC_ADPT_ARG(adapter), type);
		rtw_warn_on(1);
		goto exit;
	}

	if (*t_ie) {
		u32 free_len = *t_ie_len;
		*t_ie_len = 0;
		rtw_mfree(*t_ie, free_len);
		*t_ie = NULL;
	}

	if (!clear) {
		*t_ie = rtw_malloc(ie_len);
		if (*t_ie == NULL) {
			RTW_ERR(FUNC_ADPT_FMT" type:%u, rtw_malloc() fail\n"
				, FUNC_ADPT_ARG(adapter), type);
			goto exit;
		}
		_rtw_memcpy(*t_ie, ie, ie_len);
		*t_ie_len = ie_len;
	}

	if (*t_ie && *t_ie_len) {
		u8 *attr_content;
		u32 attr_contentlen = 0;

		attr_content = rtw_get_wfd_attr_content(*t_ie, *t_ie_len, WFD_ATTR_DEVICE_INFO, NULL, &attr_contentlen);
		if (attr_content && attr_contentlen) {
			if (RTW_GET_BE16(attr_content + 2) != wfd_info->rtsp_ctrlport) {
				wfd_info->rtsp_ctrlport = RTW_GET_BE16(attr_content + 2);
				RTW_INFO(FUNC_ADPT_FMT" type:%u, RTSP CTRL port = %u\n"
					, FUNC_ADPT_ARG(adapter), type, wfd_info->rtsp_ctrlport);
			}
		}
	}

success:
	ret = _SUCCESS;

exit:
	return ret;
}
#endif /* defined(CONFIG_WFD) && defined(CONFIG_IOCTL_CFG80211) */

void _rtw_free_mlme_priv(struct mlme_priv *pmlmepriv)
{
	_adapter *adapter = mlme_to_adapter(pmlmepriv);
	if (NULL == pmlmepriv) {
		rtw_warn_on(1);
		goto exit;
	}
	rtw_free_mlme_priv_ie_data(pmlmepriv);

	if (pmlmepriv) {
		rtw_mfree_mlme_priv_lock(pmlmepriv);

		if (pmlmepriv->free_bss_buf)
			rtw_vmfree(pmlmepriv->free_bss_buf, pmlmepriv->max_bss_cnt * sizeof(struct wlan_network));
	}
exit:
	return;
}

sint	_rtw_enqueue_network(_queue *queue, struct wlan_network *pnetwork)
{
	_irqL irqL;


	if (pnetwork == NULL)
		goto exit;

	_enter_critical_bh(&queue->lock, &irqL);

	rtw_list_insert_tail(&pnetwork->list, &queue->queue);

	_exit_critical_bh(&queue->lock, &irqL);

exit:


	return _SUCCESS;
}

/*
struct	wlan_network *_rtw_dequeue_network(_queue *queue)
{
	_irqL irqL;

	struct wlan_network *pnetwork;


	_enter_critical_bh(&queue->lock, &irqL);

	if (_rtw_queue_empty(queue) == _TRUE)

		pnetwork = NULL;

	else
	{
		pnetwork = LIST_CONTAINOR(get_next(&queue->queue), struct wlan_network, list);

		rtw_list_delete(&(pnetwork->list));
	}

	_exit_critical_bh(&queue->lock, &irqL);


	return pnetwork;
}
*/

struct	wlan_network *_rtw_alloc_network(struct	mlme_priv *pmlmepriv) /* (_queue *free_queue) */
{
	_irqL	irqL;
	struct	wlan_network	*pnetwork;
	_queue *free_queue = &pmlmepriv->free_bss_pool;
	_list *plist = NULL;


	_enter_critical_bh(&free_queue->lock, &irqL);

	if (_rtw_queue_empty(free_queue) == _TRUE) {
		pnetwork = NULL;
		goto exit;
	}
	plist = get_next(&(free_queue->queue));

	pnetwork = LIST_CONTAINOR(plist , struct wlan_network, list);

	rtw_list_delete(&pnetwork->list);

	pnetwork->network_type = 0;
	pnetwork->fixed = _FALSE;
	pnetwork->last_scanned = rtw_get_current_time();
	pnetwork->last_non_hidden_ssid_ap = pnetwork->last_scanned;
#if defined(CONFIG_RTW_MESH) && CONFIG_RTW_MESH_ACNODE_PREVENT
	pnetwork->acnode_stime = 0;
	pnetwork->acnode_notify_etime = 0;
#endif

	pnetwork->aid = 0;
	pnetwork->join_res = 0;

	pmlmepriv->num_of_scanned++;

exit:
	_exit_critical_bh(&free_queue->lock, &irqL);


	return pnetwork;
}

void _rtw_free_network(struct	mlme_priv *pmlmepriv , struct wlan_network *pnetwork, u8 isfreeall)
{
	u32 delta_time;
	u32 lifetime = SCANQUEUE_LIFETIME;
	_irqL irqL;
	_queue *free_queue = &(pmlmepriv->free_bss_pool);


	if (pnetwork == NULL)
		goto exit;

	if (pnetwork->fixed == _TRUE)
		goto exit;

	if ((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == _TRUE) ||
	    (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE))
		lifetime = 1;

	if (!isfreeall) {
		delta_time = (u32) rtw_get_passing_time_ms(pnetwork->last_scanned);
		if (delta_time < lifetime) /* unit:msec */
			goto exit;
	}

	_enter_critical_bh(&free_queue->lock, &irqL);

	rtw_list_delete(&(pnetwork->list));

	rtw_list_insert_tail(&(pnetwork->list), &(free_queue->queue));

	pmlmepriv->num_of_scanned--;


	/* RTW_INFO("_rtw_free_network:SSID=%s\n", pnetwork->network.Ssid.Ssid); */

	_exit_critical_bh(&free_queue->lock, &irqL);

exit:
	return;
}

void _rtw_free_network_nolock(struct	mlme_priv *pmlmepriv, struct wlan_network *pnetwork)
{

	_queue *free_queue = &(pmlmepriv->free_bss_pool);


	if (pnetwork == NULL)
		goto exit;

	if (pnetwork->fixed == _TRUE)
		goto exit;

	/* _enter_critical(&free_queue->lock, &irqL); */

	rtw_list_delete(&(pnetwork->list));

	rtw_list_insert_tail(&(pnetwork->list), get_list_head(free_queue));

	pmlmepriv->num_of_scanned--;

	/* _exit_critical(&free_queue->lock, &irqL); */

exit:
	return;
}

void _rtw_free_network_queue(_adapter *padapter, u8 isfreeall)
{
	_irqL irqL;
	_list *phead, *plist;
	struct wlan_network *pnetwork;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	_queue *scanned_queue = &pmlmepriv->scanned_queue;



	_enter_critical_bh(&scanned_queue->lock, &irqL);

	phead = get_list_head(scanned_queue);
	plist = get_next(phead);

	while (rtw_end_of_queue_search(phead, plist) == _FALSE) {

		pnetwork = LIST_CONTAINOR(plist, struct wlan_network, list);

		plist = get_next(plist);

		_rtw_free_network(pmlmepriv, pnetwork, isfreeall);

	}

	_exit_critical_bh(&scanned_queue->lock, &irqL);


}




sint rtw_if_up(_adapter *padapter)
{

	sint res;

	if (RTW_CANNOT_RUN(padapter) ||
	    (check_fwstate(&padapter->mlmepriv, WIFI_ASOC_STATE) == _FALSE)) {
		res = _FALSE;
	} else
		res =  _TRUE;

	return res;
}


void rtw_generate_random_ibss(u8 *pibss)
{
	*((u32 *)(&pibss[2])) = rtw_random32();
	pibss[0] = 0x02; /* in ad-hoc mode local bit must set to 1 */
	pibss[1] = 0x11;
	pibss[2] = 0x87;
}

u8 *rtw_get_capability_from_ie(u8 *ie)
{
	return ie + 8 + 2;
}


u16 rtw_get_capability(WLAN_BSSID_EX *bss)
{
	u16	val;

	_rtw_memcpy((u8 *)&val, rtw_get_capability_from_ie(bss->IEs), 2);

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


int	rtw_init_mlme_priv(_adapter *padapter) /* (struct	mlme_priv *pmlmepriv) */
{
	int	res;
	res = _rtw_init_mlme_priv(padapter);/* (pmlmepriv); */
	return res;
}

void rtw_free_mlme_priv(struct mlme_priv *pmlmepriv)
{
	_rtw_free_mlme_priv(pmlmepriv);
}

int	rtw_enqueue_network(_queue *queue, struct wlan_network *pnetwork);
int	rtw_enqueue_network(_queue *queue, struct wlan_network *pnetwork)
{
	int	res;
	res = _rtw_enqueue_network(queue, pnetwork);
	return res;
}

/*
static struct	wlan_network *rtw_dequeue_network(_queue *queue)
{
	struct wlan_network *pnetwork;
	pnetwork = _rtw_dequeue_network(queue);
	return pnetwork;
}
*/

struct	wlan_network *rtw_alloc_network(struct	mlme_priv *pmlmepriv);
struct	wlan_network *rtw_alloc_network(struct	mlme_priv *pmlmepriv) /* (_queue	*free_queue) */
{
	struct	wlan_network	*pnetwork;
	pnetwork = _rtw_alloc_network(pmlmepriv);
	return pnetwork;
}

void rtw_free_network(struct mlme_priv *pmlmepriv, struct	wlan_network *pnetwork, u8 is_freeall);
void rtw_free_network(struct mlme_priv *pmlmepriv, struct	wlan_network *pnetwork, u8 is_freeall)/* (struct	wlan_network *pnetwork, _queue	*free_queue) */
{
	_rtw_free_network(pmlmepriv, pnetwork, is_freeall);
}

void rtw_free_network_nolock(_adapter *padapter, struct wlan_network *pnetwork);
void rtw_free_network_nolock(_adapter *padapter, struct wlan_network *pnetwork)
{
	_rtw_free_network_nolock(&(padapter->mlmepriv), pnetwork);
#ifdef CONFIG_IOCTL_CFG80211
	rtw_cfg80211_unlink_bss(padapter, pnetwork);
#endif /* CONFIG_IOCTL_CFG80211 */
}


void rtw_free_network_queue(_adapter *dev, u8 isfreeall)
{
	_rtw_free_network_queue(dev, isfreeall);
}

struct wlan_network *_rtw_find_network(_queue *scanned_queue, const u8 *addr)
{
	_list	*phead, *plist;
	struct	wlan_network *pnetwork = NULL;
	u8 zero_addr[ETH_ALEN] = {0, 0, 0, 0, 0, 0};

	if (_rtw_memcmp(zero_addr, addr, ETH_ALEN)) {
		pnetwork = NULL;
		goto exit;
	}

	phead = get_list_head(scanned_queue);
	plist = get_next(phead);

	while (plist != phead) {
		pnetwork = LIST_CONTAINOR(plist, struct wlan_network , list);

		if (_rtw_memcmp(addr, pnetwork->network.MacAddress, ETH_ALEN) == _TRUE)
			break;

		plist = get_next(plist);
	}

	if (plist == phead)
		pnetwork = NULL;

exit:
	return pnetwork;
}

struct wlan_network *rtw_find_network(_queue *scanned_queue, const u8 *addr)
{
	struct	wlan_network *pnetwork;
	_irqL irqL;

	 _enter_critical_bh(&scanned_queue->lock, &irqL);
	pnetwork = _rtw_find_network(scanned_queue, addr);
	_exit_critical_bh(&scanned_queue->lock, &irqL);

	return pnetwork;
}

int rtw_is_same_ibss(_adapter *adapter, struct wlan_network *pnetwork)
{
	int ret = _TRUE;
	struct security_priv *psecuritypriv = &adapter->securitypriv;

	if ((psecuritypriv->dot11PrivacyAlgrthm != _NO_PRIVACY_) &&
	    (pnetwork->network.Privacy == 0))
		ret = _FALSE;
	else if ((psecuritypriv->dot11PrivacyAlgrthm == _NO_PRIVACY_) &&
		 (pnetwork->network.Privacy == 1))
		ret = _FALSE;
	else
		ret = _TRUE;

	return ret;

}

inline int is_same_ess(WLAN_BSSID_EX *a, WLAN_BSSID_EX *b)
{
	return (a->Ssid.SsidLength == b->Ssid.SsidLength)
	       &&  _rtw_memcmp(a->Ssid.Ssid, b->Ssid.Ssid, a->Ssid.SsidLength) == _TRUE;
}

int is_same_network(WLAN_BSSID_EX *src, WLAN_BSSID_EX *dst, u8 feature)
{
	u16 s_cap, d_cap;


	if (rtw_bug_check(dst, src, &s_cap, &d_cap) == _FALSE)
		return _FALSE;

	_rtw_memcpy((u8 *)&s_cap, rtw_get_capability_from_ie(src->IEs), 2);
	_rtw_memcpy((u8 *)&d_cap, rtw_get_capability_from_ie(dst->IEs), 2);


	s_cap = le16_to_cpu(s_cap);
	d_cap = le16_to_cpu(d_cap);


#ifdef CONFIG_P2P
	if ((feature == 1) && /* 1: P2P supported */
	    (_rtw_memcmp(src->MacAddress, dst->MacAddress, ETH_ALEN) == _TRUE)
	   )
		return _TRUE;
#endif

	/* Wi-Fi driver doesn't consider the situation of BCN and ProbRsp sent from the same hidden AP, 
	  * it considers these two packets are sent from different AP. 
	  * Therefore, the scan queue may store two scan results of the same hidden AP, likes below.
	  *
	  *  index            bssid              ch    RSSI   SdBm  Noise   age          flag             ssid
	  *    1    00:e0:4c:55:50:01    153   -73     -73        0     7044   [WPS][ESS]     RTK5G
	  *    3    00:e0:4c:55:50:01    153   -73     -73        0     7044   [WPS][ESS]
	  *
	  * Original rules will compare Ssid, SsidLength, MacAddress, s_cap, d_cap at the same time.
	  * Wi-Fi driver will assume that the BCN and ProbRsp sent from the same hidden AP are the same network
	  * after we add an additional rule to compare SsidLength and Ssid.
	  * It means the scan queue will not store two scan results of the same hidden AP, it only store ProbRsp.
	  * For customer request.
	  */
	  
	if (((_rtw_memcmp(src->MacAddress, dst->MacAddress, ETH_ALEN)) == _TRUE) &&
		((s_cap & WLAN_CAPABILITY_IBSS) == (d_cap & WLAN_CAPABILITY_IBSS)) &&
		((s_cap & WLAN_CAPABILITY_BSS) == (d_cap & WLAN_CAPABILITY_BSS))) {
		if ((src->Ssid.SsidLength == dst->Ssid.SsidLength) && 
			(((_rtw_memcmp(src->Ssid.Ssid, dst->Ssid.Ssid, src->Ssid.SsidLength)) == _TRUE) || //Case of normal AP
			(is_all_null(src->Ssid.Ssid, src->Ssid.SsidLength) == _TRUE || is_all_null(dst->Ssid.Ssid, dst->Ssid.SsidLength) == _TRUE))) //Case of hidden AP
			return _TRUE;
		else if ((src->Ssid.SsidLength == 0 || dst->Ssid.SsidLength == 0)) //Case of hidden AP
			return _TRUE;
		else
			return _FALSE;
	} else {
		return _FALSE;
	}
}

struct wlan_network *_rtw_find_same_network(_queue *scanned_queue, struct wlan_network *network)
{
	_list *phead, *plist;
	struct wlan_network *found = NULL;

	phead = get_list_head(scanned_queue);
	plist = get_next(phead);

	while (plist != phead) {
		found = LIST_CONTAINOR(plist, struct wlan_network , list);

		if (is_same_network(&network->network, &found->network, 0))
			break;

		plist = get_next(plist);
	}

	if (plist == phead)
		found = NULL;

	return found;
}

struct wlan_network *rtw_find_same_network(_queue *scanned_queue, struct wlan_network *network)
{
	_irqL irqL;
	struct wlan_network *found = NULL;

	if (scanned_queue == NULL || network == NULL)
		goto exit;

	_enter_critical_bh(&scanned_queue->lock, &irqL);
	found = _rtw_find_same_network(scanned_queue, network);
	_exit_critical_bh(&scanned_queue->lock, &irqL);

exit:
	return found;
}

struct	wlan_network	*rtw_get_oldest_wlan_network(_queue *scanned_queue)
{
	_list	*plist, *phead;


	struct	wlan_network	*pwlan = NULL;
	struct	wlan_network	*oldest = NULL;
	phead = get_list_head(scanned_queue);

	plist = get_next(phead);

	while (1) {

		if (rtw_end_of_queue_search(phead, plist) == _TRUE)
			break;

		pwlan = LIST_CONTAINOR(plist, struct wlan_network, list);

		if (pwlan->fixed != _TRUE) {
			if (oldest == NULL || rtw_time_after(oldest->last_scanned, pwlan->last_scanned))
				oldest = pwlan;
		}

		plist = get_next(plist);
	}
	return oldest;

}

void update_network(WLAN_BSSID_EX *dst, WLAN_BSSID_EX *src,
		    _adapter *padapter, bool update_ie)
{
#if defined(DBG_RX_SIGNAL_DISPLAY_SSID_MONITORED) && 1
	u8 ss_ori = dst->PhyInfo.SignalStrength;
	u8 sq_ori = dst->PhyInfo.SignalQuality;
	u8 ss_smp = src->PhyInfo.SignalStrength;
	long rssi_smp = src->Rssi;
#endif
	long rssi_ori = dst->Rssi;

	u8 sq_smp = src->PhyInfo.SignalQuality;
	u8 ss_final;
	u8 sq_final;
	long rssi_final;


#ifdef CONFIG_ANTENNA_DIVERSITY
	rtw_hal_antdiv_rssi_compared(padapter, dst, src); /* this will update src.Rssi, need consider again */
#endif

#if defined(DBG_RX_SIGNAL_DISPLAY_SSID_MONITORED) && 1
	if (strcmp(dst->Ssid.Ssid, DBG_RX_SIGNAL_DISPLAY_SSID_MONITORED) == 0) {
		RTW_INFO(FUNC_ADPT_FMT" %s("MAC_FMT", ch%u) ss_ori:%3u, sq_ori:%3u, rssi_ori:%3ld, ss_smp:%3u, sq_smp:%3u, rssi_smp:%3ld\n"
			 , FUNC_ADPT_ARG(padapter)
			, src->Ssid.Ssid, MAC_ARG(src->MacAddress), src->Configuration.DSConfig
			 , ss_ori, sq_ori, rssi_ori
			 , ss_smp, sq_smp, rssi_smp
			);
	}
#endif

	/* The rule below is 1/5 for sample value, 4/5 for history value */
	if (check_fwstate(&padapter->mlmepriv, WIFI_ASOC_STATE) && is_same_network(&(padapter->mlmepriv.cur_network.network), src, 0)) {
		/* Take the recvpriv's value for the connected AP*/
		ss_final = padapter->recvpriv.signal_strength;
		sq_final = padapter->recvpriv.signal_qual;
		/* the rssi value here is undecorated, and will be used for antenna diversity */
		if (sq_smp != 101) /* from the right channel */
			rssi_final = (src->Rssi + dst->Rssi * 4) / 5;
		else
			rssi_final = rssi_ori;
	} else {
		if (sq_smp != 101) { /* from the right channel */
			ss_final = ((u32)(src->PhyInfo.SignalStrength) + (u32)(dst->PhyInfo.SignalStrength) * 4) / 5;
			sq_final = ((u32)(src->PhyInfo.SignalQuality) + (u32)(dst->PhyInfo.SignalQuality) * 4) / 5;
			rssi_final = (src->Rssi + dst->Rssi * 4) / 5;
		} else {
			/* bss info not receving from the right channel, use the original RX signal infos */
			ss_final = dst->PhyInfo.SignalStrength;
			sq_final = dst->PhyInfo.SignalQuality;
			rssi_final = dst->Rssi;
		}

	}

	if (update_ie) {
		dst->Reserved[0] = src->Reserved[0];
		dst->Reserved[1] = src->Reserved[1];
		_rtw_memcpy((u8 *)dst, (u8 *)src, get_WLAN_BSSID_EX_sz(src));
	}

	dst->PhyInfo.SignalStrength = ss_final;
	dst->PhyInfo.SignalQuality = sq_final;
	dst->Rssi = rssi_final;

#if defined(DBG_RX_SIGNAL_DISPLAY_SSID_MONITORED) && 1
	if (strcmp(dst->Ssid.Ssid, DBG_RX_SIGNAL_DISPLAY_SSID_MONITORED) == 0) {
		RTW_INFO(FUNC_ADPT_FMT" %s("MAC_FMT"), SignalStrength:%u, SignalQuality:%u, RawRSSI:%ld\n"
			 , FUNC_ADPT_ARG(padapter)
			, dst->Ssid.Ssid, MAC_ARG(dst->MacAddress), dst->PhyInfo.SignalStrength, dst->PhyInfo.SignalQuality, dst->Rssi);
	}
#endif

#if 0 /* old codes, may be useful one day...
 * 	RTW_INFO("update_network: rssi=0x%lx dst->Rssi=%d ,dst->Rssi=0x%lx , src->Rssi=0x%lx",(dst->Rssi+src->Rssi)/2,dst->Rssi,dst->Rssi,src->Rssi); */
	if (check_fwstate(&padapter->mlmepriv, WIFI_ASOC_STATE) && is_same_network(&(padapter->mlmepriv.cur_network.network), src)) {

		/* RTW_INFO("b:ssid=%s update_network: src->rssi=0x%d padapter->recvpriv.ui_rssi=%d\n",src->Ssid.Ssid,src->Rssi,padapter->recvpriv.signal); */
		if (padapter->recvpriv.signal_qual_data.total_num++ >= PHY_LINKQUALITY_SLID_WIN_MAX) {
			padapter->recvpriv.signal_qual_data.total_num = PHY_LINKQUALITY_SLID_WIN_MAX;
			last_evm = padapter->recvpriv.signal_qual_data.elements[padapter->recvpriv.signal_qual_data.index];
			padapter->recvpriv.signal_qual_data.total_val -= last_evm;
		}
		padapter->recvpriv.signal_qual_data.total_val += query_rx_pwr_percentage(src->Rssi);

		padapter->recvpriv.signal_qual_data.elements[padapter->recvpriv.signal_qual_data.index++] = query_rx_pwr_percentage(src->Rssi);
		if (padapter->recvpriv.signal_qual_data.index >= PHY_LINKQUALITY_SLID_WIN_MAX)
			padapter->recvpriv.signal_qual_data.index = 0;

		/* RTW_INFO("Total SQ=%d  pattrib->signal_qual= %d\n", padapter->recvpriv.signal_qual_data.total_val, src->Rssi); */

		/* <1> Showed on UI for user,in percentage. */
		tmpVal = padapter->recvpriv.signal_qual_data.total_val / padapter->recvpriv.signal_qual_data.total_num;
		padapter->recvpriv.signal = (u8)tmpVal; /* Link quality */

		src->Rssi = translate_percentage_to_dbm(padapter->recvpriv.signal) ;
	} else {
		/*	RTW_INFO("ELSE:ssid=%s update_network: src->rssi=0x%d dst->rssi=%d\n",src->Ssid.Ssid,src->Rssi,dst->Rssi); */
		src->Rssi = (src->Rssi + dst->Rssi) / 2; /* dBM */
	}

	/*	RTW_INFO("a:update_network: src->rssi=0x%d padapter->recvpriv.ui_rssi=%d\n",src->Rssi,padapter->recvpriv.signal); */

#endif

}

static void update_current_network(_adapter *adapter, WLAN_BSSID_EX *pnetwork)
{
	struct	mlme_priv	*pmlmepriv = &(adapter->mlmepriv);


	rtw_bug_check(&(pmlmepriv->cur_network.network),
		      &(pmlmepriv->cur_network.network),
		      &(pmlmepriv->cur_network.network),
		      &(pmlmepriv->cur_network.network));

	if ((check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == _TRUE) && (is_same_network(&(pmlmepriv->cur_network.network), pnetwork, 0))) {

		/* if(pmlmepriv->cur_network.network.IELength<= pnetwork->IELength) */
		{
			update_network(&(pmlmepriv->cur_network.network), pnetwork, adapter, _TRUE);
			rtw_update_protection(adapter, (pmlmepriv->cur_network.network.IEs) + sizeof(NDIS_802_11_FIXED_IEs),
				      pmlmepriv->cur_network.network.IELength);
		}
	}


}


/*

Caller must hold pmlmepriv->lock first.


*/
bool rtw_update_scanned_network(_adapter *adapter, WLAN_BSSID_EX *target)
{
	_irqL irqL;
	_list	*plist, *phead;
	u32	bssid_ex_sz;
	struct mlme_priv	*pmlmepriv = &(adapter->mlmepriv);
#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo = &(adapter->wdinfo);
#endif /* CONFIG_P2P */
	_queue	*queue	= &(pmlmepriv->scanned_queue);
	struct wlan_network	*pnetwork = NULL;
	struct wlan_network	*choice = NULL;
	int target_find = 0;
	u8 feature = 0;
	bool update_ie = _FALSE;

	_enter_critical_bh(&queue->lock, &irqL);
	phead = get_list_head(queue);
	plist = get_next(phead);

#if 0
	RTW_INFO("%s => ssid:%s , rssi:%ld , ss:%d\n",
		__func__, target->Ssid.Ssid, target->Rssi, target->PhyInfo.SignalStrength);
#endif

#ifdef CONFIG_P2P
	if (!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
		feature = 1; /* p2p enable */
#endif

	while (1) {
		if (rtw_end_of_queue_search(phead, plist) == _TRUE)
			break;

		pnetwork = LIST_CONTAINOR(plist, struct wlan_network, list);

		rtw_bug_check(pnetwork, pnetwork, pnetwork, pnetwork);

#ifdef CONFIG_P2P
		if (!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE) &&
		    (_rtw_memcmp(pnetwork->network.MacAddress, target->MacAddress, ETH_ALEN) == _TRUE)) {
			target_find = 1;
			break;
		}
#endif

		if (is_same_network(&(pnetwork->network), target, feature)) {
			target_find = 1;
			break;
		}

		if (rtw_roam_flags(adapter)) {
			/* TODO: don't  select netowrk in the same ess as choice if it's new enough*/
		}
		if (pnetwork->fixed) {
			plist = get_next(plist);
			continue;
		}
			
#ifdef CONFIG_RSSI_PRIORITY
		if ((choice == NULL) || (pnetwork->network.PhyInfo.SignalStrength < choice->network.PhyInfo.SignalStrength))
			#ifdef CONFIG_RTW_MESH
			if (!MLME_IS_MESH(adapter) || !MLME_IS_ASOC(adapter)
				|| !rtw_bss_is_same_mbss(&pmlmepriv->cur_network.network, &pnetwork->network))
			#endif
				choice = pnetwork;
#else
		if (choice == NULL || rtw_time_after(choice->last_scanned, pnetwork->last_scanned))
			#ifdef CONFIG_RTW_MESH
			if (!MLME_IS_MESH(adapter) || !MLME_IS_ASOC(adapter)
				|| !rtw_bss_is_same_mbss(&pmlmepriv->cur_network.network, &pnetwork->network))
			#endif
				choice = pnetwork;
#endif
		plist = get_next(plist);

	}


	/* If we didn't find a match, then get a new network slot to initialize
	 * with this beacon's information */
	/* if (rtw_end_of_queue_search(phead,plist)== _TRUE) { */
	if (!target_find) {
		if (_rtw_queue_empty(&(pmlmepriv->free_bss_pool)) == _TRUE) {
			/* If there are no more slots, expire the choice */
			/* list_del_init(&choice->list); */
			pnetwork = choice;
			if (pnetwork == NULL)
				goto unlock_scan_queue;

#ifdef CONFIG_RSSI_PRIORITY
		RTW_DBG("%s => ssid:%s ,bssid:"MAC_FMT"  will be deleted from scanned_queue (rssi:%ld , ss:%d)\n",
			__func__, pnetwork->network.Ssid.Ssid, MAC_ARG(pnetwork->network.MacAddress), pnetwork->network.Rssi, pnetwork->network.PhyInfo.SignalStrength);
#else
		RTW_DBG("%s => ssid:%s ,bssid:"MAC_FMT" will be deleted from scanned_queue\n",
			__func__, pnetwork->network.Ssid.Ssid, MAC_ARG(pnetwork->network.MacAddress));
#endif

#ifdef CONFIG_ANTENNA_DIVERSITY
			rtw_hal_get_odm_var(adapter, HAL_ODM_ANTDIV_SELECT, &(target->PhyInfo.Optimum_antenna), NULL);
#endif
			_rtw_memcpy(&(pnetwork->network), target,  get_WLAN_BSSID_EX_sz(target));
			pnetwork->bcn_keys_valid = 0;
			if (target->Reserved[0] == BSS_TYPE_BCN || target->Reserved[0] == BSS_TYPE_PROB_RSP)
				rtw_update_bcn_keys_of_network(pnetwork);
			/* variable initialize */
			pnetwork->fixed = _FALSE;
			pnetwork->last_scanned = rtw_get_current_time();
			pnetwork->last_non_hidden_ssid_ap = pnetwork->last_scanned;
			#if defined(CONFIG_RTW_MESH) && CONFIG_RTW_MESH_ACNODE_PREVENT
			pnetwork->acnode_stime = 0;
			pnetwork->acnode_notify_etime = 0;
			#endif

			pnetwork->network_type = 0;
			pnetwork->aid = 0;
			pnetwork->join_res = 0;

			/* bss info not receving from the right channel */
			if (pnetwork->network.PhyInfo.SignalQuality == 101)
				pnetwork->network.PhyInfo.SignalQuality = 0;
		} else {
			/* Otherwise just pull from the free list */

			pnetwork = rtw_alloc_network(pmlmepriv); /* will update scan_time */
			if (pnetwork == NULL)
				goto unlock_scan_queue;

			bssid_ex_sz = get_WLAN_BSSID_EX_sz(target);
			target->Length = bssid_ex_sz;
#ifdef CONFIG_ANTENNA_DIVERSITY
			rtw_hal_get_odm_var(adapter, HAL_ODM_ANTDIV_SELECT, &(target->PhyInfo.Optimum_antenna), NULL);
#endif
			_rtw_memcpy(&(pnetwork->network), target, bssid_ex_sz);
			pnetwork->bcn_keys_valid = 0;
			if (target->Reserved[0] == BSS_TYPE_BCN || target->Reserved[0] == BSS_TYPE_PROB_RSP)
				rtw_update_bcn_keys_of_network(pnetwork);

			/* bss info not receving from the right channel */
			if (pnetwork->network.PhyInfo.SignalQuality == 101)
				pnetwork->network.PhyInfo.SignalQuality = 0;

			rtw_list_insert_tail(&(pnetwork->list), &(queue->queue));

		}
	} else {
		/* we have an entry and we are going to update it. But this entry may
		 * be already expired. In this case we do the same as we found a new
		 * net and call the new_net handler
		 */
		#if defined(CONFIG_RTW_MESH) && CONFIG_RTW_MESH_ACNODE_PREVENT
		systime last_scanned = pnetwork->last_scanned;
		#endif
		struct beacon_keys bcn_keys;
		bool bcn_keys_valid = 0;
		bool is_hidden_ssid_ap = 0;

		pnetwork->last_scanned = rtw_get_current_time();

		if (target->Reserved[0] == BSS_TYPE_BCN || target->Reserved[0] == BSS_TYPE_PROB_RSP) {
			if (target->InfrastructureMode == Ndis802_11Infrastructure) {
				is_hidden_ssid_ap = hidden_ssid_ap(target);
				if (!is_hidden_ssid_ap) /* update last time it's non hidden ssid AP */
					pnetwork->last_non_hidden_ssid_ap = rtw_get_current_time();
			}
			bcn_keys_valid = rtw_get_bcn_keys_from_bss(target, &bcn_keys);
		}

		if (target->InfrastructureMode == Ndis802_11_mesh
			|| target->Reserved[0] >= pnetwork->network.Reserved[0])
			update_ie = _TRUE;
		else if (target->InfrastructureMode == Ndis802_11Infrastructure && !pnetwork->fixed
			&& rtw_get_passing_time_ms(pnetwork->last_non_hidden_ssid_ap) > SCANQUEUE_LIFETIME)
			update_ie = _TRUE;
		else if (bcn_keys_valid) {
			if (is_hidden_ssid(bcn_keys.ssid, bcn_keys.ssid_len)) {
				/* hidden ssid, replace with current beacon ssid directly */
				_rtw_memcpy(bcn_keys.ssid, pnetwork->bcn_keys.ssid, pnetwork->bcn_keys.ssid_len);
				bcn_keys.ssid_len = pnetwork->bcn_keys.ssid_len;
			}
			if (rtw_bcn_key_compare(&pnetwork->bcn_keys, &bcn_keys) == _FALSE)
				update_ie = _TRUE;
		}

		#if defined(CONFIG_RTW_MESH) && CONFIG_RTW_MESH_ACNODE_PREVENT
		if (!MLME_IS_MESH(adapter) || !MLME_IS_ASOC(adapter)
			|| pnetwork->network.Configuration.DSConfig != target->Configuration.DSConfig
			|| rtw_get_passing_time_ms(last_scanned) > adapter->mesh_cfg.peer_sel_policy.scanr_exp_ms
			|| !rtw_bss_is_same_mbss(&pnetwork->network, target)
		) {
			pnetwork->acnode_stime = 0;
			pnetwork->acnode_notify_etime = 0;
		}
		#endif

		if (bcn_keys_valid) {
			_rtw_memcpy(&pnetwork->bcn_keys, &bcn_keys, sizeof(bcn_keys));
			pnetwork->bcn_keys_valid = 1;
		} else if (update_ie)
			pnetwork->bcn_keys_valid = 0;

		update_network(&(pnetwork->network), target, adapter, update_ie);
	}

	#if defined(CONFIG_RTW_MESH) && CONFIG_RTW_MESH_ACNODE_PREVENT
	if (MLME_IS_MESH(adapter) && MLME_IS_ASOC(adapter))
		rtw_mesh_update_scanned_acnode_status(adapter, pnetwork);
	#endif

unlock_scan_queue:
	_exit_critical_bh(&queue->lock, &irqL);

#ifdef CONFIG_RTW_MESH
	if (pnetwork && MLME_IS_MESH(adapter)
		&& check_fwstate(pmlmepriv, WIFI_ASOC_STATE)
		&& !check_fwstate(pmlmepriv, WIFI_UNDER_SURVEY)
	)
		rtw_chk_candidate_peer_notify(adapter, pnetwork);
#endif

	return update_ie;
}

void rtw_add_network(_adapter *adapter, WLAN_BSSID_EX *pnetwork);
void rtw_add_network(_adapter *adapter, WLAN_BSSID_EX *pnetwork)
{
	bool update_ie;
	/* _queue	*queue	= &(pmlmepriv->scanned_queue); */

	/* _enter_critical_bh(&queue->lock, &irqL); */

#if defined(CONFIG_P2P) && defined(CONFIG_P2P_REMOVE_GROUP_INFO)
	if (adapter->registrypriv.wifi_spec == 0)
		rtw_bss_ex_del_p2p_attr(pnetwork, P2P_ATTR_GROUP_INFO);
#endif

	if (!hal_chk_wl_func(adapter, WL_FUNC_MIRACAST))
		rtw_bss_ex_del_wfd_ie(pnetwork);

	/* Wi-Fi driver will update the current network if the scan result of the connected AP be updated by scan. */
	update_ie = rtw_update_scanned_network(adapter, pnetwork);

	if (update_ie)
		update_current_network(adapter, pnetwork);

	/* _exit_critical_bh(&queue->lock, &irqL); */

}

/* select the desired network based on the capability of the (i)bss.
 * check items: (1) security
 *			   (2) network_type
 *			   (3) WMM
 *			   (4) HT
 * (5) others */
int rtw_is_desired_network(_adapter *adapter, struct wlan_network *pnetwork);
int rtw_is_desired_network(_adapter *adapter, struct wlan_network *pnetwork)
{
	struct security_priv *psecuritypriv = &adapter->securitypriv;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	u32 desired_encmode;
	u32 privacy;

	/* u8 wps_ie[512]; */
	uint wps_ielen;

	int bselected = _TRUE;

	desired_encmode = psecuritypriv->ndisencryptstatus;
	privacy = pnetwork->network.Privacy;

	if (check_fwstate(pmlmepriv, WIFI_UNDER_WPS)) {
		if (rtw_get_wps_ie(pnetwork->network.IEs + _FIXED_IE_LENGTH_, pnetwork->network.IELength - _FIXED_IE_LENGTH_, NULL, &wps_ielen) != NULL)
			return _TRUE;
		else
			return _FALSE;
	}
	if (adapter->registrypriv.wifi_spec == 1) { /* for  correct flow of 8021X  to do.... */
		u8 *p = NULL;
		uint ie_len = 0;

		if ((desired_encmode == Ndis802_11EncryptionDisabled) && (privacy != 0))
			bselected = _FALSE;

		if (psecuritypriv->ndisauthtype == Ndis802_11AuthModeWPA2PSK) {
			p = rtw_get_ie(pnetwork->network.IEs + _BEACON_IE_OFFSET_, _RSN_IE_2_, &ie_len, (pnetwork->network.IELength - _BEACON_IE_OFFSET_));
			if (p && ie_len > 0)
				bselected = _TRUE;
			else
				bselected = _FALSE;
		}
	}


	if ((desired_encmode != Ndis802_11EncryptionDisabled) && (privacy == 0)) {
		RTW_INFO("desired_encmode: %d, privacy: %d\n", desired_encmode, privacy);
		bselected = _FALSE;
	}

	if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE) {
		if (pnetwork->network.InfrastructureMode != pmlmepriv->cur_network.network.InfrastructureMode)
			bselected = _FALSE;
	}


	return bselected;
}

void rtw_survey_event_callback(_adapter	*adapter, u8 *pbuf)
{
	_irqL  irqL;
	u32 len;
	WLAN_BSSID_EX *pnetwork;
	struct	mlme_priv	*pmlmepriv = &(adapter->mlmepriv);


	pnetwork = (WLAN_BSSID_EX *)pbuf;

	len = get_WLAN_BSSID_EX_sz(pnetwork);
	if (len > (sizeof(WLAN_BSSID_EX))) {
		return;
	}


	_enter_critical_bh(&pmlmepriv->lock, &irqL);

	/* update IBSS_network 's timestamp */
	if ((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)) == _TRUE) {
		if (_rtw_memcmp(&(pmlmepriv->cur_network.network.MacAddress), pnetwork->MacAddress, ETH_ALEN)) {
			struct wlan_network *ibss_wlan = NULL;
			_irqL	irqL;

			_rtw_memcpy(pmlmepriv->cur_network.network.IEs, pnetwork->IEs, 8);
			_enter_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
			ibss_wlan = _rtw_find_network(&pmlmepriv->scanned_queue,  pnetwork->MacAddress);
			if (ibss_wlan) {
				_rtw_memcpy(ibss_wlan->network.IEs , pnetwork->IEs, 8);
				_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
				goto exit;
			}
			_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
		}
	}

	/* lock pmlmepriv->lock when you accessing network_q */
	if ((check_fwstate(pmlmepriv, WIFI_UNDER_LINKING)) == _FALSE) {
		if (pnetwork->Ssid.Ssid[0] == 0)
			pnetwork->Ssid.SsidLength = 0;
		rtw_add_network(adapter, pnetwork);
	}

exit:

	_exit_critical_bh(&pmlmepriv->lock, &irqL);


	return;
}

void rtw_surveydone_event_callback(_adapter	*adapter, u8 *pbuf)
{
	_irqL  irqL;
	struct surveydone_event *parm = (struct surveydone_event *)pbuf;
	struct	mlme_priv	*pmlmepriv = &(adapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &adapter->mlmeextpriv;

#ifdef CONFIG_MLME_EXT
	mlmeext_surveydone_event_callback(adapter);
#endif


	_enter_critical_bh(&pmlmepriv->lock, &irqL);
	if (pmlmepriv->wps_probe_req_ie) {
		u32 free_len = pmlmepriv->wps_probe_req_ie_len;
		pmlmepriv->wps_probe_req_ie_len = 0;
		rtw_mfree(pmlmepriv->wps_probe_req_ie, free_len);
		pmlmepriv->wps_probe_req_ie = NULL;
	}


	if (check_fwstate(pmlmepriv, WIFI_UNDER_SURVEY) == _FALSE) {
		RTW_INFO(FUNC_ADPT_FMT" fw_state:0x%x\n", FUNC_ADPT_ARG(adapter), get_fwstate(pmlmepriv));
		/* rtw_warn_on(1); */
	}

	if (pmlmeext->scan_abort == _TRUE)
		pmlmeext->scan_abort = _FALSE;

	_clr_fwstate_(pmlmepriv, WIFI_UNDER_SURVEY);
	_exit_critical_bh(&pmlmepriv->lock, &irqL);

	_cancel_timer_ex(&pmlmepriv->scan_to_timer);

	_enter_critical_bh(&pmlmepriv->lock, &irqL);

#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
	rtw_set_signal_stat_timer(&adapter->recvpriv);
#endif

	if (pmlmepriv->to_join == _TRUE) {
		if ((check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE)) {
			if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == _FALSE) {
				set_fwstate(pmlmepriv, WIFI_UNDER_LINKING);

				if (rtw_select_and_join_from_scanned_queue(pmlmepriv) == _SUCCESS)
					_set_timer(&pmlmepriv->assoc_timer, MAX_JOIN_TIMEOUT);
				else {
					WLAN_BSSID_EX    *pdev_network = &(adapter->registrypriv.dev_network);
					u8 *pibss = adapter->registrypriv.dev_network.MacAddress;

					/* pmlmepriv->fw_state ^= WIFI_UNDER_SURVEY; */ /* because don't set assoc_timer */
					_clr_fwstate_(pmlmepriv, WIFI_UNDER_SURVEY);


					_rtw_memset(&pdev_network->Ssid, 0, sizeof(NDIS_802_11_SSID));
					_rtw_memcpy(&pdev_network->Ssid, &pmlmepriv->assoc_ssid, sizeof(NDIS_802_11_SSID));

					rtw_update_registrypriv_dev_network(adapter);
					rtw_generate_random_ibss(pibss);

					/*pmlmepriv->fw_state = WIFI_ADHOC_MASTER_STATE;*/
					init_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE);

					if (rtw_create_ibss_cmd(adapter, 0) != _SUCCESS)
						RTW_ERR("rtw_create_ibss_cmd FAIL\n");

					pmlmepriv->to_join = _FALSE;
				}
			}
		} else {
			int s_ret;
			set_fwstate(pmlmepriv, WIFI_UNDER_LINKING);
			pmlmepriv->to_join = _FALSE;
			s_ret = rtw_select_and_join_from_scanned_queue(pmlmepriv);
			if (_SUCCESS == s_ret)
				_set_timer(&pmlmepriv->assoc_timer, MAX_JOIN_TIMEOUT);
			else if (s_ret == 2) { /* there is no need to wait for join */
				_clr_fwstate_(pmlmepriv, WIFI_UNDER_LINKING);
				rtw_indicate_connect(adapter);
			} else {
				RTW_INFO("try_to_join, but select scanning queue fail, to_roam:%d\n", rtw_to_roam(adapter));

				if (rtw_to_roam(adapter) != 0) {
					struct sitesurvey_parm scan_parm;
					u8 ssc_chk = rtw_sitesurvey_condition_check(adapter, _FALSE);

					rtw_init_sitesurvey_parm(adapter, &scan_parm);
					_rtw_memcpy(&scan_parm.ssid[0], &pmlmepriv->assoc_ssid, sizeof(NDIS_802_11_SSID));
					scan_parm.ssid_num = 1;

					if (rtw_dec_to_roam(adapter) == 0
						|| (ssc_chk != SS_ALLOW && ssc_chk != SS_DENY_BUSY_TRAFFIC)
						|| _SUCCESS != rtw_sitesurvey_cmd(adapter, &scan_parm)
					   ) {
						rtw_set_to_roam(adapter, 0);
						rtw_free_assoc_resources(adapter, _TRUE);
						rtw_indicate_disconnect(adapter, 0, _FALSE);
					} else
						pmlmepriv->to_join = _TRUE;
				} else
					rtw_indicate_disconnect(adapter, 0, _FALSE);
				_clr_fwstate_(pmlmepriv, WIFI_UNDER_LINKING);
			}
		}
	} else {
		if (rtw_chk_roam_flags(adapter, RTW_ROAM_ACTIVE)) {
			if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)
			    && check_fwstate(pmlmepriv, WIFI_ASOC_STATE)) {
				if (rtw_select_roaming_candidate(pmlmepriv) == _SUCCESS) {
#ifdef CONFIG_RTW_80211R
					rtw_ft_start_roam(adapter,
						(u8 *)pmlmepriv->roam_network->network.MacAddress);
#else
					receive_disconnect(adapter, pmlmepriv->cur_network.network.MacAddress
						, WLAN_REASON_ACTIVE_ROAM, _FALSE);
#endif
				}
			}
		}
	}

	/* RTW_INFO("scan complete in %dms\n",rtw_get_passing_time_ms(pmlmepriv->scan_start_time)); */

	_exit_critical_bh(&pmlmepriv->lock, &irqL);

#ifdef CONFIG_P2P_PS
	if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == _TRUE)
		p2p_ps_wk_cmd(adapter, P2P_PS_SCAN_DONE, 0);
#endif /* CONFIG_P2P_PS */

	rtw_mi_os_xmit_schedule(adapter);

#ifdef CONFIG_DRVEXT_MODULE_WSC
	drvext_surveydone_callback(&adapter->drvextpriv);
#endif

#ifdef DBG_CONFIG_ERROR_DETECT
	{
		struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
		if (pmlmeext->sitesurvey_res.bss_cnt == 0) {
			/* rtw_hal_sreset_reset(adapter); */
		}
	}
#endif

#ifdef CONFIG_IOCTL_CFG80211
	rtw_cfg80211_surveydone_event_callback(adapter);
#endif /* CONFIG_IOCTL_CFG80211 */

	rtw_indicate_scan_done(adapter, _FALSE);

#if defined(CONFIG_CONCURRENT_MODE) && defined(CONFIG_IOCTL_CFG80211)
	rtw_cfg80211_indicate_scan_done_for_buddy(adapter, _FALSE);
#endif

#ifdef CONFIG_RTW_MESH
	#if CONFIG_RTW_MESH_OFFCH_CAND
	if (rtw_mesh_offch_candidate_accepted(adapter)) {
		u8 ch;

		ch = rtw_mesh_select_operating_ch(adapter);
		if (ch && pmlmepriv->cur_network.network.Configuration.DSConfig != ch) {
			u8 ifbmp = rtw_mi_get_ap_mesh_ifbmp(adapter);

			if (ifbmp) {
				/* switch to selected channel */
				rtw_change_bss_chbw_cmd(adapter, RTW_CMDF_DIRECTLY, ifbmp, 0, ch, REQ_BW_ORI, REQ_OFFSET_NONE);
				issue_probereq_ex(adapter, &pmlmepriv->cur_network.network.mesh_id, NULL, 0, 0, 0, 0);
			} else
				rtw_warn_on(1);
		}
	}
	#endif
#endif /* CONFIG_RTW_MESH */

#ifdef CONFIG_RTW_ACS
	if (parm->acs) {
		u8 ifbmp = rtw_mi_get_ap_mesh_ifbmp(adapter);

		if (ifbmp)
			rtw_change_bss_chbw_cmd(adapter, RTW_CMDF_DIRECTLY, ifbmp, 0, REQ_CH_INT_INFO, REQ_BW_ORI, REQ_OFFSET_NONE);
	}
#endif
}

u8 _rtw_sitesurvey_condition_check(const char *caller, _adapter *adapter, bool check_sc_interval)
{
	u8 ss_condition = SS_ALLOW;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct registry_priv *registry_par = &adapter->registrypriv;


#ifdef CONFIG_MP_INCLUDED
	if (rtw_mp_mode_check(adapter)) {
		RTW_INFO("%s ("ADPT_FMT") MP mode block Scan request\n", caller, ADPT_ARG(adapter));
		ss_condition = SS_DENY_MP_MODE;
		goto _exit;
	}
#endif

#ifdef DBG_LA_MODE
	if(registry_par->la_mode_en == 1 && MLME_IS_ASOC(adapter)) {
		RTW_INFO("%s ("ADPT_FMT") LA debug mode block Scan request\n", caller, ADPT_ARG(adapter));
		ss_condition = SS_DENY_LA_MODE;
		goto _exit;
	}
#endif

#ifdef CONFIG_RTW_REPEATER_SON
	if (adapter->rtw_rson_scanstage == RSON_SCAN_PROCESS) {
		RTW_INFO("%s ("ADPT_FMT") blocking scan for under rson scanning process\n", caller, ADPT_ARG(adapter));
		ss_condition = SS_DENY_RSON_SCANING;
		goto _exit;
	}
#endif
#ifdef CONFIG_IOCTL_CFG80211
	if (adapter_wdev_data(adapter)->block_scan == _TRUE) {
		RTW_INFO("%s ("ADPT_FMT") wdev_priv.block_scan is set\n", caller, ADPT_ARG(adapter));
		ss_condition = SS_DENY_BLOCK_SCAN;
		goto _exit;
	}
#endif

	if (adapter_to_dvobj(adapter)->scan_deny == _TRUE) {
		RTW_INFO("%s ("ADPT_FMT") tpt mode, scan deny!\n", caller, ADPT_ARG(adapter));
		ss_condition = SS_DENY_BLOCK_SCAN;
		goto _exit;
	}

	if (rtw_is_scan_deny(adapter)) {
		RTW_INFO("%s ("ADPT_FMT") : scan deny\n", caller, ADPT_ARG(adapter));
		ss_condition = SS_DENY_BY_DRV;
		goto _exit;
	}

#ifdef CONFIG_ADAPTIVITY_DENY_SCAN
	if (registry_par->adaptivity_en
	    && rtw_phydm_get_edcca_flag(adapter)
	    && rtw_is_2g_ch(GET_HAL_DATA(adapter)->current_channel)) {
		RTW_WARN(FUNC_ADPT_FMT": Adaptivity block scan! (ch=%u)\n",
			 FUNC_ADPT_ARG(adapter),
			 GET_HAL_DATA(adapter)->current_channel);
		ss_condition = SS_DENY_ADAPTIVITY;
		goto _exit;
	}
#endif /* CONFIG_ADAPTIVITY_DENY_SCAN */

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE)){
		if(check_fwstate(pmlmepriv, WIFI_UNDER_WPS)) {
			RTW_INFO("%s ("ADPT_FMT") : scan abort!! AP mode process WPS\n", caller, ADPT_ARG(adapter));
			ss_condition = SS_DENY_SELF_AP_UNDER_WPS;
			goto _exit;
		} else if (check_fwstate(pmlmepriv, WIFI_UNDER_LINKING) == _TRUE) {
			RTW_INFO("%s ("ADPT_FMT") : scan abort!!AP mode under linking (fwstate=0x%x)\n",
				caller, ADPT_ARG(adapter), pmlmepriv->fw_state);
			ss_condition = SS_DENY_SELF_AP_UNDER_LINKING;
			goto _exit;
		} else if (check_fwstate(pmlmepriv, WIFI_UNDER_SURVEY) == _TRUE) {
			RTW_INFO("%s ("ADPT_FMT") : scan abort!!AP mode under survey (fwstate=0x%x)\n",
				caller, ADPT_ARG(adapter), pmlmepriv->fw_state);
			ss_condition = SS_DENY_SELF_AP_UNDER_SURVEY;
			goto _exit;
		}
	} else {
		if (check_fwstate(pmlmepriv, WIFI_UNDER_LINKING) == _TRUE) {
			RTW_INFO("%s ("ADPT_FMT") : scan abort!!STA mode under linking (fwstate=0x%x)\n",
				caller, ADPT_ARG(adapter), pmlmepriv->fw_state);
			ss_condition = SS_DENY_SELF_STA_UNDER_LINKING;
			goto _exit;
		} else if (check_fwstate(pmlmepriv, WIFI_UNDER_SURVEY) == _TRUE) {
			RTW_INFO("%s ("ADPT_FMT") : scan abort!!STA mode under survey (fwstate=0x%x)\n",
				caller, ADPT_ARG(adapter), pmlmepriv->fw_state);
			ss_condition = SS_DENY_SELF_STA_UNDER_SURVEY;
			goto _exit;
		}
	}

#ifdef CONFIG_CONCURRENT_MODE
	if (rtw_mi_buddy_check_fwstate(adapter, WIFI_UNDER_LINKING | WIFI_UNDER_WPS)) {
		RTW_INFO("%s ("ADPT_FMT") : scan abort!! buddy_intf under linking or wps\n", caller, ADPT_ARG(adapter));
		ss_condition = SS_DENY_BUDDY_UNDER_LINK_WPS;
		goto _exit;

	} else if (rtw_mi_buddy_check_fwstate(adapter, WIFI_UNDER_SURVEY)) {
		RTW_INFO("%s ("ADPT_FMT") : scan abort!! buddy_intf under survey\n", caller, ADPT_ARG(adapter));
		ss_condition = SS_DENY_BUDDY_UNDER_SURVEY;
		goto _exit;
	}
#endif /* CONFIG_CONCURRENT_MODE */

#ifdef RTW_BUSY_DENY_SCAN
	/*
	 * busy traffic check
	 * Rules:
	 * 1. If (scan interval <= BUSY_TRAFFIC_SCAN_DENY_PERIOD) always allow
	 *    scan, otherwise goto rule 2.
	 * 2. Deny scan if any interface is busy, otherwise allow scan.
	 */
	if (pmlmepriv->lastscantime
	    && (rtw_get_passing_time_ms(pmlmepriv->lastscantime) >
		registry_par->scan_interval_thr)
	    && rtw_mi_busy_traffic_check(adapter)) {
		RTW_WARN("%s ("ADPT_FMT") : scan abort!! BusyTraffic\n",
			 caller, ADPT_ARG(adapter));
		ss_condition = SS_DENY_BUSY_TRAFFIC;
		goto _exit;
	}
#endif /* RTW_BUSY_DENY_SCAN */

_exit:
	return ss_condition;
}

static void free_scanqueue(struct	mlme_priv *pmlmepriv)
{
	_irqL irqL, irqL0;
	_queue *free_queue = &pmlmepriv->free_bss_pool;
	_queue *scan_queue = &pmlmepriv->scanned_queue;
	_list	*plist, *phead, *ptemp;


	_enter_critical_bh(&scan_queue->lock, &irqL0);
	_enter_critical_bh(&free_queue->lock, &irqL);

	phead = get_list_head(scan_queue);
	plist = get_next(phead);

	while (plist != phead) {
		ptemp = get_next(plist);
		rtw_list_delete(plist);
		rtw_list_insert_tail(plist, &free_queue->queue);
		plist = ptemp;
		pmlmepriv->num_of_scanned--;
	}

	_exit_critical_bh(&free_queue->lock, &irqL);
	_exit_critical_bh(&scan_queue->lock, &irqL0);

}

void rtw_reset_rx_info(_adapter *adapter)
{
	struct recv_priv  *precvpriv = &adapter->recvpriv;

	precvpriv->dbg_rx_ampdu_drop_count = 0;
	precvpriv->dbg_rx_ampdu_forced_indicate_count = 0;
	precvpriv->dbg_rx_ampdu_loss_count = 0;
	precvpriv->dbg_rx_dup_mgt_frame_drop_count = 0;
	precvpriv->dbg_rx_ampdu_window_shift_cnt = 0;
	precvpriv->dbg_rx_drop_count = 0;
	precvpriv->dbg_rx_conflic_mac_addr_cnt = 0;
}

/*
*rtw_free_assoc_resources: the caller has to lock pmlmepriv->lock
*/
void rtw_free_assoc_resources(_adapter *adapter, u8 lock_scanned_queue)
{
	_irqL irqL;
	struct wlan_network *pwlan = NULL;
	struct	mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct wlan_network *tgt_network = &pmlmepriv->cur_network;


#ifdef CONFIG_TDLS
	struct tdls_info *ptdlsinfo = &adapter->tdlsinfo;
#endif /* CONFIG_TDLS */


	RTW_INFO("%s-"ADPT_FMT" tgt_network MacAddress=" MAC_FMT" ssid=%s\n",
		__func__, ADPT_ARG(adapter), MAC_ARG(tgt_network->network.MacAddress), tgt_network->network.Ssid.Ssid);

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
		struct sta_info *psta;

		psta = rtw_get_stainfo(&adapter->stapriv, tgt_network->network.MacAddress);

#ifdef CONFIG_TDLS
		rtw_free_all_tdls_sta(adapter, _TRUE);
		rtw_reset_tdls_info(adapter);

		if (ptdlsinfo->link_established == _TRUE)
			rtw_tdls_cmd(adapter, NULL, TDLS_RS_RCR);
#endif /* CONFIG_TDLS */

		/* _enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL); */
		rtw_free_stainfo(adapter, psta);
		/* _exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL); */

	}

	if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE | WIFI_ADHOC_MASTER_STATE)) {
		struct sta_info *psta;

		rtw_free_all_stainfo(adapter);

		psta = rtw_get_bcmc_stainfo(adapter);
		/* _enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);		 */
		rtw_free_stainfo(adapter, psta);
		/* _exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);		 */

		rtw_init_bcmc_stainfo(adapter);
	}

	if (lock_scanned_queue)
		_enter_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);

	if (check_fwstate(pmlmepriv, WIFI_UNDER_WPS) || (pmlmepriv->wpa_phase == _TRUE)){
		RTW_INFO("Dont free disconnecting network of scanned_queue due to uner %s %s phase\n\n",
			check_fwstate(pmlmepriv, WIFI_UNDER_WPS) ? "WPS" : "",
			(pmlmepriv->wpa_phase == _TRUE) ? "WPA" : "");
	} else {
		pwlan = _rtw_find_same_network(&pmlmepriv->scanned_queue, tgt_network);
		if (pwlan) {
			pwlan->fixed = _FALSE;

			RTW_INFO("Free disconnecting network of scanned_queue\n");
			rtw_free_network_nolock(adapter, pwlan);
#ifdef CONFIG_P2P
			if (!rtw_p2p_chk_state(&adapter->wdinfo, P2P_STATE_NONE)) {
				rtw_set_scan_deny(adapter, 2000);
				/* rtw_clear_scan_deny(adapter); */
			}
#endif /* CONFIG_P2P */
		} else
			RTW_ERR("Free disconnecting network of scanned_queue failed due to pwlan == NULL\n\n");
	}

	if ((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) && (adapter->stapriv.asoc_sta_count == 1))
	    /*||check_fwstate(pmlmepriv, WIFI_STATION_STATE)*/) {
		if (pwlan)
			rtw_free_network_nolock(adapter, pwlan);
	}

	if (lock_scanned_queue)
		_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);

	adapter->securitypriv.key_mask = 0;

	rtw_reset_rx_info(adapter);


}

/*
*rtw_indicate_connect: the caller has to lock pmlmepriv->lock
*/
void rtw_indicate_connect(_adapter *padapter)
{
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;

	pmlmepriv->to_join = _FALSE;

	if (!check_fwstate(&padapter->mlmepriv, WIFI_ASOC_STATE)) {

		set_fwstate(pmlmepriv, WIFI_ASOC_STATE);

		rtw_led_control(padapter, LED_CTL_LINK);

		rtw_os_indicate_connect(padapter);
	}

	rtw_set_to_roam(padapter, 0);
	if (!MLME_IS_AP(padapter) && !MLME_IS_MESH(padapter))
		rtw_mi_set_scan_deny(padapter, 3000);


}


/*
*rtw_indicate_disconnect: the caller has to lock pmlmepriv->lock
*/
void rtw_indicate_disconnect(_adapter *padapter, u16 reason, u8 locally_generated)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX	*cur_network = &(pmlmeinfo->network);
#ifdef CONFIG_WAPI_SUPPORT
	struct sta_info *psta;
	struct sta_priv *pstapriv = &padapter->stapriv;
#endif
	u8 *wps_ie = NULL;
	uint wpsie_len = 0;

	if (check_fwstate(pmlmepriv, WIFI_UNDER_WPS))
		pmlmepriv->wpa_phase = _TRUE;

	_clr_fwstate_(pmlmepriv, WIFI_UNDER_LINKING | WIFI_UNDER_WPS | WIFI_OP_CH_SWITCHING | WIFI_UNDER_KEY_HANDSHAKE);

	/* force to clear cur_network_scanned's SELECTED REGISTRAR */
	if (pmlmepriv->cur_network_scanned) {
		WLAN_BSSID_EX	*current_joined_bss = &(pmlmepriv->cur_network_scanned->network);
		if (current_joined_bss) {
			wps_ie = rtw_get_wps_ie(current_joined_bss->IEs + _FIXED_IE_LENGTH_,
				current_joined_bss->IELength - _FIXED_IE_LENGTH_, NULL, &wpsie_len);
			if (wps_ie && wpsie_len > 0) {
				u8 *attr = NULL;
				u32 attr_len;
				attr = rtw_get_wps_attr(wps_ie, wpsie_len, WPS_ATTR_SELECTED_REGISTRAR,
							NULL, &attr_len);
				if (attr)
					*(attr + 4) = 0;
			}
		}
	}
	/* RTW_INFO("clear wps when %s\n", __func__); */

	if (rtw_to_roam(padapter) > 0)
		_clr_fwstate_(pmlmepriv, WIFI_ASOC_STATE);

#ifdef CONFIG_WAPI_SUPPORT
	psta = rtw_get_stainfo(pstapriv, cur_network->MacAddress);
	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE))
		rtw_wapi_return_one_sta_info(padapter, psta->cmn.mac_addr);
	else if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) ||
		 check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE))
		rtw_wapi_return_all_sta_info(padapter);
#endif

	if (check_fwstate(&padapter->mlmepriv, WIFI_ASOC_STATE)
	    || (rtw_to_roam(padapter) <= 0)
	   ) {

#ifdef CONFIG_RTW_TOKEN_BASED_XMIT
		if (ATOMIC_READ(&padapter->tbtx_tx_pause) == _TRUE) {
			ATOMIC_SET(&padapter->tbtx_tx_pause, _FALSE);
			rtw_tx_control_cmd(padapter);
		}
#endif

		rtw_os_indicate_disconnect(padapter, reason, locally_generated);

		/* set ips_deny_time to avoid enter IPS before LPS leave */
		rtw_set_ips_deny(padapter, 3000);

		_clr_fwstate_(pmlmepriv, WIFI_ASOC_STATE);

		rtw_led_control(padapter, LED_CTL_NO_LINK);

		rtw_clear_scan_deny(padapter);
	}

#ifdef CONFIG_P2P_PS
	p2p_ps_wk_cmd(padapter, P2P_PS_DISABLE, 1);
#endif /* CONFIG_P2P_PS */

#ifdef CONFIG_LPS
	rtw_lps_ctrl_wk_cmd(padapter, LPS_CTRL_DISCONNECT, 0);
#endif

#ifdef CONFIG_BEAMFORMING
	beamforming_wk_cmd(padapter, BEAMFORMING_CTRL_LEAVE, cur_network->MacAddress, ETH_ALEN, 1);
#endif /*CONFIG_BEAMFORMING*/

}

inline void rtw_indicate_scan_done(_adapter *padapter, bool aborted)
{
	RTW_INFO(FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(padapter));

	rtw_os_indicate_scan_done(padapter, aborted);

#ifdef CONFIG_IPS
	if (is_primary_adapter(padapter)
	    && (_FALSE == adapter_to_pwrctl(padapter)->bInSuspend)
	    && (check_fwstate(&padapter->mlmepriv, WIFI_ASOC_STATE | WIFI_UNDER_LINKING) == _FALSE)) {
		struct pwrctrl_priv *pwrpriv;

		pwrpriv = adapter_to_pwrctl(padapter);
		rtw_set_ips_deny(padapter, 0);
#ifdef CONFIG_IPS_CHECK_IN_WD
		_set_timer(&adapter_to_dvobj(padapter)->dynamic_chk_timer, 1);
#else /* !CONFIG_IPS_CHECK_IN_WD */
		_rtw_set_pwr_state_check_timer(pwrpriv, 1);
#endif /* !CONFIG_IPS_CHECK_IN_WD */
	}
#endif /* CONFIG_IPS */
}

static u32 _rtw_wait_scan_done(_adapter *adapter, u8 abort, u32 timeout_ms)
{
	systime start;
	u32 pass_ms;
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &(adapter->mlmeextpriv);

	start = rtw_get_current_time();

	pmlmeext->scan_abort = abort;

	while (check_fwstate(pmlmepriv, WIFI_UNDER_SURVEY)
	       && rtw_get_passing_time_ms(start) <= timeout_ms) {

		if (RTW_CANNOT_RUN(adapter))
			break;

		RTW_INFO(FUNC_NDEV_FMT"fw_state=WIFI_UNDER_SURVEY!\n", FUNC_NDEV_ARG(adapter->pnetdev));
		rtw_msleep_os(20);
	}

	if (_TRUE == abort) {
		if (check_fwstate(pmlmepriv, WIFI_UNDER_SURVEY)) {
			if (!RTW_CANNOT_RUN(adapter))
				RTW_INFO(FUNC_NDEV_FMT"waiting for scan_abort time out!\n", FUNC_NDEV_ARG(adapter->pnetdev));
#ifdef CONFIG_PLATFORM_MSTAR
			/*_clr_fwstate_(pmlmepriv, WIFI_UNDER_SURVEY);*/
			set_survey_timer(pmlmeext, 0);
			mlme_set_scan_to_timer(pmlmepriv, 50);
#endif
			rtw_indicate_scan_done(adapter, _TRUE);
		}
	}

	pmlmeext->scan_abort = _FALSE;
	pass_ms = rtw_get_passing_time_ms(start);

	return pass_ms;

}

void rtw_scan_wait_completed(_adapter *adapter)
{
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
	struct ss_res *ss = &pmlmeext->sitesurvey_res;

	_rtw_wait_scan_done(adapter, _FALSE, ss->scan_timeout_ms);
}

u32 rtw_scan_abort_timeout(_adapter *adapter, u32 timeout_ms)
{
	return _rtw_wait_scan_done(adapter, _TRUE, timeout_ms);
}

void rtw_scan_abort_no_wait(_adapter *adapter)
{
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &(adapter->mlmeextpriv);

	if (check_fwstate(pmlmepriv, WIFI_UNDER_SURVEY))
		pmlmeext->scan_abort = _TRUE;
}

void rtw_scan_abort(_adapter *adapter)
{
	rtw_scan_abort_timeout(adapter, 200);
}

static u32 _rtw_wait_join_done(_adapter *adapter, u8 abort, u32 timeout_ms)
{
	systime start;
	u32 pass_ms;
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &(adapter->mlmeextpriv);

	start = rtw_get_current_time();

	pmlmeext->join_abort = abort;
	if (abort)
		set_link_timer(pmlmeext, 1);

	while (rtw_get_passing_time_ms(start) <= timeout_ms
		&& (check_fwstate(pmlmepriv, WIFI_UNDER_LINKING)
			#ifdef CONFIG_IOCTL_CFG80211
			|| rtw_cfg80211_is_connect_requested(adapter)
			#endif
			)
	) {
		if (RTW_CANNOT_RUN(adapter))
			break;

		RTW_INFO(FUNC_ADPT_FMT" linking...\n", FUNC_ADPT_ARG(adapter));
		rtw_msleep_os(20);
	}

	if (abort) {
		if (check_fwstate(pmlmepriv, WIFI_UNDER_LINKING)
			#ifdef CONFIG_IOCTL_CFG80211
			|| rtw_cfg80211_is_connect_requested(adapter)
			#endif
		) {
			if (!RTW_CANNOT_RUN(adapter))
				RTW_INFO(FUNC_ADPT_FMT" waiting for join_abort time out!\n", FUNC_ADPT_ARG(adapter));
		}
	}

	pmlmeext->join_abort = 0;
	pass_ms = rtw_get_passing_time_ms(start);

	return pass_ms;
}

u32 rtw_join_abort_timeout(_adapter *adapter, u32 timeout_ms)
{
	return _rtw_wait_join_done(adapter, _TRUE, timeout_ms);
}

static struct sta_info *rtw_joinbss_update_stainfo(_adapter *padapter, struct wlan_network *pnetwork)
{
	int i;
	struct sta_info *psta = NULL;
	struct recv_reorder_ctrl *preorder_ctrl;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
#ifdef CONFIG_RTS_FULL_BW
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct wlan_network  *cur_network = &(pmlmepriv->cur_network);
#endif/*CONFIG_RTS_FULL_BW*/

	psta = rtw_get_stainfo(pstapriv, pnetwork->network.MacAddress);
	if (psta == NULL)
		psta = rtw_alloc_stainfo(pstapriv, pnetwork->network.MacAddress);

	if (psta) { /* update ptarget_sta */
		RTW_INFO("%s\n", __FUNCTION__);

		psta->cmn.aid  = pnetwork->join_res;

		update_sta_info(padapter, psta);

		/* update station supportRate */
		psta->bssratelen = rtw_get_rateset_len(pnetwork->network.SupportedRates);
		_rtw_memcpy(psta->bssrateset, pnetwork->network.SupportedRates, psta->bssratelen);
		rtw_hal_update_sta_ra_info(padapter, psta);

		psta->wireless_mode = pmlmeext->cur_wireless_mode;
		rtw_hal_update_sta_wset(padapter, psta);

		/* sta mode */
		rtw_hal_set_odm_var(padapter, HAL_ODM_STA_INFO, psta, _TRUE);

		/* security related */
#ifdef CONFIG_RTW_80211R
		if ((padapter->securitypriv.dot11AuthAlgrthm == dot11AuthAlgrthm_8021X)
			&& (psta->ft_pairwise_key_installed == _FALSE)) {
#else
		if (padapter->securitypriv.dot11AuthAlgrthm == dot11AuthAlgrthm_8021X) {
#endif
			u8 *ie;
			sint ie_len;
			u8 mfp_opt = MFP_NO;

			padapter->securitypriv.binstallGrpkey = _FALSE;
			padapter->securitypriv.busetkipkey = _FALSE;
			padapter->securitypriv.bgrpkey_handshake = _FALSE;

			ie = rtw_get_ie(pnetwork->network.IEs + _BEACON_IE_OFFSET_, WLAN_EID_RSN
				, &ie_len, (pnetwork->network.IELength - _BEACON_IE_OFFSET_));
			if (ie && ie_len > 0
				&& rtw_parse_wpa2_ie(ie, ie_len + 2, NULL, NULL, NULL, NULL, &mfp_opt) == _SUCCESS
			) {
				if (padapter->securitypriv.mfp_opt >= MFP_OPTIONAL && mfp_opt >= MFP_OPTIONAL)
					psta->flags |= WLAN_STA_MFP;
			}

			psta->ieee8021x_blocked = _TRUE;
			psta->dot118021XPrivacy = padapter->securitypriv.dot11PrivacyAlgrthm;

			_rtw_memset((u8 *)&psta->dot118021x_UncstKey, 0, sizeof(union Keytype));
			_rtw_memset((u8 *)&psta->dot11tkiprxmickey, 0, sizeof(union Keytype));
			_rtw_memset((u8 *)&psta->dot11tkiptxmickey, 0, sizeof(union Keytype));
		}

		/*	Commented by Albert 2012/07/21 */
		/*	When doing the WPS, the wps_ie_len won't equal to 0 */
		/*	And the Wi-Fi driver shouldn't allow the data packet to be tramsmitted. */
		if (padapter->securitypriv.wps_ie_len != 0) {
			psta->ieee8021x_blocked = _TRUE;
			padapter->securitypriv.wps_ie_len = 0;
		}


		/* for A-MPDU Rx reordering buffer control for sta_info */
		/* if A-MPDU Rx is enabled, reseting  rx_ordering_ctrl wstart_b(indicate_seq) to default value=0xffff */
		/* todo: check if AP can send A-MPDU packets */
		for (i = 0; i < 16 ; i++) {
			/* preorder_ctrl = &precvpriv->recvreorder_ctrl[i]; */
			preorder_ctrl = &psta->recvreorder_ctrl[i];
			preorder_ctrl->enable = _FALSE;
			preorder_ctrl->indicate_seq = 0xffff;
			rtw_clear_bit(RTW_RECV_ACK_OR_TIMEOUT, &preorder_ctrl->rec_abba_rsp_ack);
			#ifdef DBG_RX_SEQ
			RTW_INFO("DBG_RX_SEQ "FUNC_ADPT_FMT" tid:%u SN_CLEAR indicate_seq:%u preorder_ctrl->rec_abba_rsp_ack:%lu\n"
				, FUNC_ADPT_ARG(padapter)
				, i
				, preorder_ctrl->indicate_seq
				,preorder_ctrl->rec_abba_rsp_ack
				);
			#endif
			preorder_ctrl->wend_b = 0xffff;
			preorder_ctrl->wsize_b = 64;/* max_ampdu_sz; */ /* ex. 32(kbytes) -> wsize_b=32 */
			preorder_ctrl->ampdu_size = RX_AMPDU_SIZE_INVALID;
		}
	}

#ifdef	CONFIG_RTW_80211K
	_rtw_memcpy(&psta->rm_en_cap, pnetwork->network.PhyInfo.rm_en_cap, 5);
#endif
#ifdef CONFIG_RTS_FULL_BW
	rtw_parse_sta_vendor_ie_8812(padapter, psta, BSS_EX_TLV_IES(&cur_network->network), BSS_EX_TLV_IES_LEN(&cur_network->network));
#endif
	return psta;

}

/* pnetwork : returns from rtw_joinbss_event_callback
 * ptarget_wlan: found from scanned_queue */
static void rtw_joinbss_update_network(_adapter *padapter, struct wlan_network *ptarget_wlan, struct wlan_network  *pnetwork)
{
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct wlan_network  *cur_network = &(pmlmepriv->cur_network);
	sint tmp_fw_state = 0x0;

	RTW_INFO("%s\n", __FUNCTION__);

	/* why not use ptarget_wlan?? */
	_rtw_memcpy(&cur_network->network, &pnetwork->network, pnetwork->network.Length);
	/* some IEs in pnetwork is wrong, so we should use ptarget_wlan IEs */
	cur_network->network.IELength = ptarget_wlan->network.IELength;
	_rtw_memcpy(&cur_network->network.IEs[0], &ptarget_wlan->network.IEs[0], MAX_IE_SZ);

	cur_network->aid = pnetwork->join_res;


#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
	rtw_set_signal_stat_timer(&padapter->recvpriv);
#endif
	padapter->recvpriv.signal_strength = ptarget_wlan->network.PhyInfo.SignalStrength;
	padapter->recvpriv.signal_qual = ptarget_wlan->network.PhyInfo.SignalQuality;
	/* the ptarget_wlan->network.Rssi is raw data, we use ptarget_wlan->network.PhyInfo.SignalStrength instead (has scaled) */
	padapter->recvpriv.rssi = translate_percentage_to_dbm(ptarget_wlan->network.PhyInfo.SignalStrength);
#if defined(DBG_RX_SIGNAL_DISPLAY_PROCESSING) && 1
	RTW_INFO(FUNC_ADPT_FMT" signal_strength:%3u, rssi:%3d, signal_qual:%3u"
		 "\n"
		 , FUNC_ADPT_ARG(padapter)
		 , padapter->recvpriv.signal_strength
		 , padapter->recvpriv.rssi
		 , padapter->recvpriv.signal_qual
		);
#endif
#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
	rtw_set_signal_stat_timer(&padapter->recvpriv);
#endif

	/* update fw_state */ /* will clr WIFI_UNDER_LINKING here indirectly */

	switch (pnetwork->network.InfrastructureMode) {
	case Ndis802_11Infrastructure:
		/* Check encryption */
		if (psecuritypriv->dot11AuthAlgrthm == dot11AuthAlgrthm_8021X)
			tmp_fw_state = tmp_fw_state | WIFI_UNDER_KEY_HANDSHAKE;

		if (check_fwstate(pmlmepriv, WIFI_UNDER_WPS))
			tmp_fw_state = tmp_fw_state | WIFI_UNDER_WPS;

		init_fwstate(pmlmepriv, WIFI_STATION_STATE | tmp_fw_state);

		break;
	case Ndis802_11IBSS:
		/*pmlmepriv->fw_state = WIFI_ADHOC_STATE;*/
		init_fwstate(pmlmepriv, WIFI_ADHOC_STATE);
		break;
	default:
		/*pmlmepriv->fw_state = WIFI_NULL_STATE;*/
		init_fwstate(pmlmepriv, WIFI_NULL_STATE);
		break;
	}

	rtw_update_protection(padapter, (cur_network->network.IEs) + sizeof(NDIS_802_11_FIXED_IEs),
			      (cur_network->network.IELength));

#ifdef CONFIG_80211N_HT
	rtw_update_ht_cap(padapter, cur_network->network.IEs, cur_network->network.IELength, (u8) cur_network->network.Configuration.DSConfig);
#endif
}

/* Notes: the fucntion could be > passive_level (the same context as Rx tasklet)
 * pnetwork : returns from rtw_joinbss_event_callback
 * ptarget_wlan: found from scanned_queue
 * if join_res > 0, for (fw_state==WIFI_STATION_STATE), we check if  "ptarget_sta" & "ptarget_wlan" exist.
 * if join_res > 0, for (fw_state==WIFI_ADHOC_STATE), we only check if "ptarget_wlan" exist.
 * if join_res > 0, update "cur_network->network" from "pnetwork->network" if (ptarget_wlan !=NULL).
 */
/* #define REJOIN */
void rtw_joinbss_event_prehandle(_adapter *adapter, u8 *pbuf, u16 status)
{
	_irqL irqL;
	static u8 retry = 0;
	struct sta_info *ptarget_sta = NULL, *pcur_sta = NULL;
	struct	sta_priv *pstapriv = &adapter->stapriv;
	struct	mlme_priv	*pmlmepriv = &(adapter->mlmepriv);
	struct wlan_network	*pnetwork	= (struct wlan_network *)pbuf;
	struct wlan_network	*cur_network = &(pmlmepriv->cur_network);
	struct wlan_network	*pcur_wlan = NULL, *ptarget_wlan = NULL;
	unsigned int		the_same_macaddr = _FALSE;

	rtw_get_encrypt_decrypt_from_registrypriv(adapter);

	the_same_macaddr = _rtw_memcmp(pnetwork->network.MacAddress, cur_network->network.MacAddress, ETH_ALEN);

	pnetwork->network.Length = get_WLAN_BSSID_EX_sz(&pnetwork->network);
	if (pnetwork->network.Length > sizeof(WLAN_BSSID_EX))
		goto exit;

	_enter_critical_bh(&pmlmepriv->lock, &irqL);

	pmlmepriv->LinkDetectInfo.TrafficTransitionCount = 0;
	pmlmepriv->LinkDetectInfo.LowPowerTransitionCount = 0;


	if (pnetwork->join_res > 0) {
		_enter_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
		retry = 0;
		if (check_fwstate(pmlmepriv, WIFI_UNDER_LINKING)) {
			/* s1. find ptarget_wlan */
			if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE)) {
				if (the_same_macaddr == _TRUE)
					ptarget_wlan = _rtw_find_network(&pmlmepriv->scanned_queue, cur_network->network.MacAddress);
				else {
					pcur_wlan = _rtw_find_network(&pmlmepriv->scanned_queue, cur_network->network.MacAddress);
					if (pcur_wlan)
						pcur_wlan->fixed = _FALSE;

					pcur_sta = rtw_get_stainfo(pstapriv, cur_network->network.MacAddress);
					if (pcur_sta) {
						/* _enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL2); */
						rtw_free_stainfo(adapter,  pcur_sta);
						/* _exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL2); */
					}

					ptarget_wlan = _rtw_find_network(&pmlmepriv->scanned_queue, pnetwork->network.MacAddress);
					if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE) {
						if (ptarget_wlan)
							ptarget_wlan->fixed = _TRUE;
					}
				}

			} else {
				ptarget_wlan = _rtw_find_same_network(&pmlmepriv->scanned_queue, pnetwork);
				if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE) {
					if (ptarget_wlan)
						ptarget_wlan->fixed = _TRUE;
				}
			}

			/* s2. update cur_network */
			if (ptarget_wlan)
				rtw_joinbss_update_network(adapter, ptarget_wlan, pnetwork);
			else {
				RTW_PRINT("Can't find ptarget_wlan when joinbss_event callback\n");
				_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
				goto ignore_joinbss_callback;
			}


			/* s3. find ptarget_sta & update ptarget_sta after update cur_network only for station mode */
			if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE) {
				ptarget_sta = rtw_joinbss_update_stainfo(adapter, pnetwork);
				if (ptarget_sta == NULL) {
					RTW_ERR("Can't update stainfo when joinbss_event callback\n");
					_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
					goto ignore_joinbss_callback;
				}

				/* Queue TX packets before FW/HW ready */
				/* clear in mlmeext_joinbss_event_callback() */
				rtw_xmit_queue_set(ptarget_sta);
			}

			/* s4. indicate connect			 */
			if (MLME_IS_STA(adapter) || MLME_IS_ADHOC(adapter)) {
				pmlmepriv->cur_network_scanned = ptarget_wlan;
				rtw_indicate_connect(adapter);
			}

			/* s5. Cancle assoc_timer					 */
			_cancel_timer_ex(&pmlmepriv->assoc_timer);


		} else {
			_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
			goto ignore_joinbss_callback;
		}

		_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);

	} else if (pnetwork->join_res == -4) {
		rtw_reset_securitypriv(adapter);
		pmlmepriv->join_status = status;
		_set_timer(&pmlmepriv->assoc_timer, 1);

		/* rtw_free_assoc_resources(adapter, _TRUE); */

		if ((check_fwstate(pmlmepriv, WIFI_UNDER_LINKING)) == _TRUE) {
			_clr_fwstate_(pmlmepriv, WIFI_UNDER_LINKING);
		}

	} else { /* if join_res < 0 (join fails), then try again */

#ifdef REJOIN
		res = _FAIL;
		if (retry < 2) {
			res = rtw_select_and_join_from_scanned_queue(pmlmepriv);
		}

		if (res == _SUCCESS) {
			/* extend time of assoc_timer */
			_set_timer(&pmlmepriv->assoc_timer, MAX_JOIN_TIMEOUT);
			retry++;
		} else if (res == 2) { /* there is no need to wait for join */
			_clr_fwstate_(pmlmepriv, WIFI_UNDER_LINKING);
			rtw_indicate_connect(adapter);
		} else {
#endif
			pmlmepriv->join_status = status;
			_set_timer(&pmlmepriv->assoc_timer, 1);
			/* rtw_free_assoc_resources(adapter, _TRUE); */
			_clr_fwstate_(pmlmepriv, WIFI_UNDER_LINKING);

#ifdef REJOIN
			retry = 0;
		}
#endif
	}

ignore_joinbss_callback:
	_exit_critical_bh(&pmlmepriv->lock, &irqL);

exit:
	return;
}

void rtw_joinbss_event_callback(_adapter *adapter, u8 *pbuf)
{
	struct wlan_network	*pnetwork	= (struct wlan_network *)pbuf;


	mlmeext_joinbss_event_callback(adapter, pnetwork->join_res);

	rtw_mi_os_xmit_schedule(adapter);

}

void rtw_sta_media_status_rpt(_adapter *adapter, struct sta_info *sta, bool connected)
{
	struct macid_ctl_t *macid_ctl = &adapter->dvobj->macid_ctl;
	bool miracast_enabled = 0;
	bool miracast_sink = 0;
	u8 role = H2C_MSR_ROLE_RSVD;

	if (sta == NULL) {
		RTW_PRINT(FUNC_ADPT_FMT" sta is NULL\n"
			  , FUNC_ADPT_ARG(adapter));
		rtw_warn_on(1);
		return;
	}

	if (sta->cmn.mac_id >= macid_ctl->num) {
		RTW_PRINT(FUNC_ADPT_FMT" invalid macid:%u\n"
			  , FUNC_ADPT_ARG(adapter), sta->cmn.mac_id);
		rtw_warn_on(1);
		return;
	}

	if (!rtw_macid_is_used(macid_ctl, sta->cmn.mac_id)) {
		RTW_PRINT(FUNC_ADPT_FMT" macid:%u not is used, set connected to 0\n"
			  , FUNC_ADPT_ARG(adapter), sta->cmn.mac_id);
		connected = 0;
		rtw_warn_on(1);
	}

	if (connected && !rtw_macid_is_bmc(macid_ctl, sta->cmn.mac_id)) {
		miracast_enabled = STA_OP_WFD_MODE(sta) != 0 && is_miracast_enabled(adapter);
		miracast_sink = miracast_enabled && (STA_OP_WFD_MODE(sta) & MIRACAST_SINK);

#ifdef CONFIG_TDLS
		if (sta->tdls_sta_state & TDLS_LINKED_STATE)
			role = H2C_MSR_ROLE_TDLS;
		else
#endif
		if (MLME_IS_STA(adapter)) {
			if (MLME_IS_GC(adapter))
				role = H2C_MSR_ROLE_GO;
			else
				role = H2C_MSR_ROLE_AP;
		} else if (MLME_IS_AP(adapter)) {
			if (MLME_IS_GO(adapter))
				role = H2C_MSR_ROLE_GC;
			else
				role = H2C_MSR_ROLE_STA;
		} else if (MLME_IS_ADHOC(adapter) || MLME_IS_ADHOC_MASTER(adapter))
			role = H2C_MSR_ROLE_ADHOC;
		else if (MLME_IS_MESH(adapter))
			role = H2C_MSR_ROLE_MESH;

#ifdef CONFIG_WFD
		if (role == H2C_MSR_ROLE_GC
			|| role == H2C_MSR_ROLE_GO
			|| role == H2C_MSR_ROLE_TDLS
		) {
			if (adapter->wfd_info.rtsp_ctrlport
				|| adapter->wfd_info.tdls_rtsp_ctrlport
				|| adapter->wfd_info.peer_rtsp_ctrlport)
				rtw_wfd_st_switch(sta, 1);
		}
#endif
	}

	rtw_hal_set_FwMediaStatusRpt_single_cmd(adapter
		, connected
		, miracast_enabled
		, miracast_sink
		, role
		, sta->cmn.mac_id
	);
}

u8 rtw_sta_media_status_rpt_cmd(_adapter *adapter, struct sta_info *sta, bool connected)
{
	struct cmd_priv	*cmdpriv = &adapter->cmdpriv;
	struct cmd_obj *cmdobj;
	struct drvextra_cmd_parm *cmd_parm;
	struct sta_media_status_rpt_cmd_parm *rpt_parm;
	u8	res = _SUCCESS;

	cmdobj = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (cmdobj == NULL) {
		res = _FAIL;
		goto exit;
	}

	cmd_parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
	if (cmd_parm == NULL) {
		rtw_mfree((u8 *)cmdobj, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}

	rpt_parm = (struct sta_media_status_rpt_cmd_parm *)rtw_zmalloc(sizeof(struct sta_media_status_rpt_cmd_parm));
	if (rpt_parm == NULL) {
		rtw_mfree((u8 *)cmdobj, sizeof(struct cmd_obj));
		rtw_mfree((u8 *)cmd_parm, sizeof(struct drvextra_cmd_parm));
		res = _FAIL;
		goto exit;
	}

	rpt_parm->sta = sta;
	rpt_parm->connected = connected;

	cmd_parm->ec_id = STA_MSTATUS_RPT_WK_CID;
	cmd_parm->type = 0;
	cmd_parm->size = sizeof(struct sta_media_status_rpt_cmd_parm);
	cmd_parm->pbuf = (u8 *)rpt_parm;
	init_h2fwcmd_w_parm_no_rsp(cmdobj, cmd_parm, CMD_SET_DRV_EXTRA);

	res = rtw_enqueue_cmd(cmdpriv, cmdobj);

exit:
	return res;
}

inline void rtw_sta_media_status_rpt_cmd_hdl(_adapter *adapter, struct sta_media_status_rpt_cmd_parm *parm)
{
	rtw_sta_media_status_rpt(adapter, parm->sta, parm->connected);
}

void rtw_stassoc_event_callback(_adapter *adapter, u8 *pbuf)
{
	_irqL irqL;
	struct sta_info *psta;
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);
	struct stassoc_event	*pstassoc	= (struct stassoc_event *)pbuf;
	struct wlan_network	*cur_network = &(pmlmepriv->cur_network);
	struct wlan_network	*ptarget_wlan = NULL;


#if CONFIG_RTW_MACADDR_ACL
	if (rtw_access_ctrl(adapter, pstassoc->macaddr) == _FALSE)
		return;
#endif

#if defined(CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)
	if (MLME_IS_AP(adapter) || MLME_IS_MESH(adapter)) {
		psta = rtw_get_stainfo(&adapter->stapriv, pstassoc->macaddr);
		if (psta) {
			u8 *passoc_req = NULL;
			u32 assoc_req_len = 0;

			rtw_sta_media_status_rpt(adapter, psta, 1);

#ifdef CONFIG_MCC_MODE
			rtw_hal_mcc_update_macid_bitmap(adapter, psta->cmn.mac_id, _TRUE);
#endif /* CONFIG_MCC_MODE */

#ifndef CONFIG_AUTO_AP_MODE
			ap_sta_info_defer_update(adapter, psta);

			if (!MLME_IS_MESH(adapter)) {
				/* report to upper layer */
				RTW_INFO("indicate_sta_assoc_event to upper layer - hostapd\n");
				#ifdef CONFIG_IOCTL_CFG80211
				_enter_critical_bh(&psta->lock, &irqL);
				if (psta->passoc_req && psta->assoc_req_len > 0) {
					passoc_req = rtw_zmalloc(psta->assoc_req_len);
					if (passoc_req) {
						assoc_req_len = psta->assoc_req_len;
						_rtw_memcpy(passoc_req, psta->passoc_req, assoc_req_len);

						rtw_mfree(psta->passoc_req , psta->assoc_req_len);
						psta->passoc_req = NULL;
						psta->assoc_req_len = 0;
					}
				}
				_exit_critical_bh(&psta->lock, &irqL);

				if (passoc_req && assoc_req_len > 0) {
					rtw_cfg80211_indicate_sta_assoc(adapter, passoc_req, assoc_req_len);
					rtw_mfree(passoc_req, assoc_req_len);
				}
				#else /* !CONFIG_IOCTL_CFG80211	 */
				rtw_indicate_sta_assoc_event(adapter, psta);
				#endif /* !CONFIG_IOCTL_CFG80211 */
			}
#endif /* !CONFIG_AUTO_AP_MODE */

#ifdef CONFIG_BEAMFORMING
			beamforming_wk_cmd(adapter, BEAMFORMING_CTRL_ENTER, (u8 *)psta, sizeof(struct sta_info), 0);
#endif/*CONFIG_BEAMFORMING*/
			if (is_wep_enc(adapter->securitypriv.dot11PrivacyAlgrthm))
				rtw_ap_wep_pk_setting(adapter, psta);
		}
		goto exit;
	}
#endif /* defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME) */

	/* for AD-HOC mode */
	psta = rtw_get_stainfo(&adapter->stapriv, pstassoc->macaddr);
	if (psta == NULL) {
		RTW_ERR(FUNC_ADPT_FMT" get no sta_info with "MAC_FMT"\n"
			, FUNC_ADPT_ARG(adapter), MAC_ARG(pstassoc->macaddr));
		rtw_warn_on(1);
		goto exit;
	}

	rtw_sta_media_status_rpt(adapter, psta, 1);

	if (adapter->securitypriv.dot11AuthAlgrthm == dot11AuthAlgrthm_8021X)
		psta->dot118021XPrivacy = adapter->securitypriv.dot11PrivacyAlgrthm;


	psta->ieee8021x_blocked = _FALSE;

	_enter_critical_bh(&pmlmepriv->lock, &irqL);

	if ((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == _TRUE) ||
	    (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE)) {
		if (adapter->stapriv.asoc_sta_count == 2) {
			_enter_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
			ptarget_wlan = _rtw_find_network(&pmlmepriv->scanned_queue, cur_network->network.MacAddress);
			pmlmepriv->cur_network_scanned = ptarget_wlan;
			if (ptarget_wlan)
				ptarget_wlan->fixed = _TRUE;
			_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
			/* a sta + bc/mc_stainfo (not Ibss_stainfo) */
			rtw_indicate_connect(adapter);
		}
	}

	_exit_critical_bh(&pmlmepriv->lock, &irqL);


	mlmeext_sta_add_event_callback(adapter, psta);

#ifdef CONFIG_RTL8711
	/* submit SetStaKey_cmd to tell fw, fw will allocate an CAM entry for this sta	 */
	rtw_setstakey_cmd(adapter, psta, GROUP_KEY, _TRUE);
#endif

exit:
#ifdef CONFIG_RTS_FULL_BW
	rtw_set_rts_bw(adapter);
#endif/*CONFIG_RTS_FULL_BW*/
	return;
}

#ifdef CONFIG_IEEE80211W
void rtw_sta_timeout_event_callback(_adapter *adapter, u8 *pbuf)
{
	_irqL irqL;
	struct sta_info *psta;
	struct stadel_event *pstadel = (struct stadel_event *)pbuf;
	struct sta_priv *pstapriv = &adapter->stapriv;


	psta = rtw_get_stainfo(&adapter->stapriv, pstadel->macaddr);

	if (psta) {
		u8 updated = _FALSE;

		_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);
		if (rtw_is_list_empty(&psta->asoc_list) == _FALSE) {
			rtw_list_delete(&psta->asoc_list);
			pstapriv->asoc_list_cnt--;
			#ifdef CONFIG_RTW_TOKEN_BASED_XMIT
			if (psta->tbtx_enable)
				pstapriv->tbtx_asoc_list_cnt--;
			#endif
			updated = ap_free_sta(adapter, psta, _TRUE, WLAN_REASON_PREV_AUTH_NOT_VALID, _TRUE);
		}
		_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);

		associated_clients_update(adapter, updated, STA_INFO_UPDATE_ALL);
	}



}
#endif /* CONFIG_IEEE80211W */

#ifdef CONFIG_RTW_80211R
void rtw_ft_info_init(struct ft_roam_info *pft)
{
	_rtw_memset(pft, 0, sizeof(struct ft_roam_info));
	pft->ft_flags = 0
		| RTW_FT_EN
		| RTW_FT_OTD_EN
#ifdef CONFIG_RTW_BTM_ROAM
		| RTW_FT_BTM_ROAM
#endif
		;
	pft->ft_updated_bcn = _FALSE;
}

u8 rtw_ft_chk_roaming_candidate(
	_adapter *padapter, struct wlan_network *competitor)
{
	u8 *pmdie;
	u32 mdie_len = 0;
	struct ft_roam_info *pft_roam = &(padapter->mlmepriv.ft_roam);

	if (!(pmdie = rtw_get_ie(&competitor->network.IEs[12],
			_MDIE_, &mdie_len, competitor->network.IELength-12)))
		return _FALSE;

	if (!_rtw_memcmp(&pft_roam->mdid, (pmdie+2), 2))
		return _FALSE;

	/*The candidate don't support over-the-DS*/
	if (rtw_ft_valid_otd_candidate(padapter, pmdie)) {
		RTW_INFO("FT: ignore the candidate("
			MAC_FMT ") for over-the-DS\n", 
			MAC_ARG(competitor->network.MacAddress));
			rtw_ft_clr_flags(padapter, RTW_FT_PEER_OTD_EN);
		return _FALSE;	
	}

	return _TRUE;
}

void rtw_ft_update_stainfo(_adapter *padapter, WLAN_BSSID_EX *pnetwork)
{
	struct sta_priv		*pstapriv = &padapter->stapriv;
	struct sta_info		*psta = NULL;

	psta = rtw_get_stainfo(pstapriv, pnetwork->MacAddress);
	if (psta == NULL)
		psta = rtw_alloc_stainfo(pstapriv, pnetwork->MacAddress);

	if (padapter->securitypriv.dot11AuthAlgrthm == dot11AuthAlgrthm_8021X) {

		padapter->securitypriv.binstallGrpkey = _FALSE;
		padapter->securitypriv.busetkipkey = _FALSE;
		padapter->securitypriv.bgrpkey_handshake = _FALSE;

		psta->ieee8021x_blocked = _TRUE;
		psta->dot118021XPrivacy = padapter->securitypriv.dot11PrivacyAlgrthm;

		_rtw_memset((u8 *)&psta->dot118021x_UncstKey, 0, sizeof(union Keytype));
		_rtw_memset((u8 *)&psta->dot11tkiprxmickey, 0, sizeof(union Keytype));
		_rtw_memset((u8 *)&psta->dot11tkiptxmickey, 0, sizeof(union Keytype));
	}

}

void rtw_ft_reassoc_event_callback(_adapter *padapter, u8 *pbuf)
{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct stassoc_event *pstassoc = (struct stassoc_event *)pbuf;
	struct ft_roam_info *pft_roam = &(pmlmepriv->ft_roam);
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX *pnetwork = (WLAN_BSSID_EX *)&(pmlmeinfo->network);
	struct cfg80211_ft_event_params ft_evt_parms;
	_irqL irqL;

	_rtw_memset(&ft_evt_parms, 0, sizeof(ft_evt_parms));
	rtw_ft_update_stainfo(padapter, pnetwork);
	ft_evt_parms.ies_len = pft_roam->ft_event.ies_len;
	ft_evt_parms.ies =  rtw_zmalloc(ft_evt_parms.ies_len);
	if (ft_evt_parms.ies)
		_rtw_memcpy((void *)ft_evt_parms.ies, pft_roam->ft_event.ies, ft_evt_parms.ies_len);
	 else
		goto err_2;

	ft_evt_parms.target_ap = rtw_zmalloc(ETH_ALEN);
	if (ft_evt_parms.target_ap)
		_rtw_memcpy((void *)ft_evt_parms.target_ap, pstassoc->macaddr, ETH_ALEN);
	else
		goto err_1;

	ft_evt_parms.ric_ies = pft_roam->ft_event.ric_ies;
	ft_evt_parms.ric_ies_len = pft_roam->ft_event.ric_ies_len;

	rtw_ft_lock_set_status(padapter, RTW_FT_AUTHENTICATED_STA, &irqL);
	rtw_cfg80211_ft_event(padapter, &ft_evt_parms);
	RTW_INFO("%s: to "MAC_FMT"\n", __func__, MAC_ARG(ft_evt_parms.target_ap));

	rtw_mfree((u8 *)pft_roam->ft_event.target_ap, ETH_ALEN);
err_1:
	rtw_mfree((u8 *)ft_evt_parms.ies, ft_evt_parms.ies_len);
err_2:
	return;
}
#endif

#if defined(CONFIG_RTW_WNM) || defined(CONFIG_RTW_80211K)
void rtw_roam_nb_info_init(_adapter *padapter)
{
	struct roam_nb_info *pnb = &(padapter->mlmepriv.nb_info);
	
	_rtw_memset(&pnb->nb_rpt, 0, sizeof(pnb->nb_rpt));
	_rtw_memset(&pnb->nb_rpt_ch_list, 0, sizeof(pnb->nb_rpt_ch_list));
	_rtw_memset(&pnb->roam_target_addr, 0, ETH_ALEN);
	pnb->nb_rpt_valid = _FALSE;
	pnb->nb_rpt_ch_list_num = 0;
	pnb->preference_en = _FALSE;
	pnb->nb_rpt_is_same = _TRUE;
	pnb->last_nb_rpt_entries = 0;
#ifdef CONFIG_RTW_WNM
	rtw_init_timer(&pnb->roam_scan_timer, 
		padapter, rtw_wnm_roam_scan_hdl, 
		padapter);
#endif
}

u8 rtw_roam_nb_scan_list_set(
	_adapter *padapter, struct sitesurvey_parm *pparm)
{
	u8 ret = _FALSE;
	u32 i;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct roam_nb_info *pnb = &(pmlmepriv->nb_info);

	if (!rtw_chk_roam_flags(padapter, RTW_ROAM_ACTIVE))
		return ret;

	if (!pmlmepriv->need_to_roam)
		return ret;

	if ((!pmlmepriv->nb_info.nb_rpt_valid) || (!pnb->nb_rpt_ch_list_num))
		return ret;

	if (!pparm)
		return ret;

	rtw_init_sitesurvey_parm(padapter, pparm);
	if (rtw_roam_busy_scan(padapter, pnb)) {
		pparm->ch_num = 1;
		pparm->ch[pmlmepriv->ch_cnt].hw_value = 
			pnb->nb_rpt_ch_list[pmlmepriv->ch_cnt].hw_value;
		pmlmepriv->ch_cnt++;
		ret = _TRUE;
		if (pmlmepriv->ch_cnt == pnb->nb_rpt_ch_list_num) {
			pmlmepriv->nb_info.nb_rpt_valid = _FALSE;
			pmlmepriv->ch_cnt = 0;
		}
		goto set_bssid_list;
	}

	pparm->ch_num = (pnb->nb_rpt_ch_list_num > RTW_CHANNEL_SCAN_AMOUNT)?
		(RTW_CHANNEL_SCAN_AMOUNT):(pnb->nb_rpt_ch_list_num);
	for (i=0; i<pparm->ch_num; i++) {
		pparm->ch[i].hw_value = pnb->nb_rpt_ch_list[i].hw_value;
		pparm->ch[i].flags = RTW_IEEE80211_CHAN_PASSIVE_SCAN;
	}

	pmlmepriv->nb_info.nb_rpt_valid = _FALSE;
	pmlmepriv->ch_cnt = 0;		
	ret = _TRUE;

set_bssid_list:
	rtw_set_802_11_bssid_list_scan(padapter, pparm);
	return ret;
}
#endif

void rtw_sta_mstatus_disc_rpt(_adapter *adapter, u8 mac_id)
{
	struct macid_ctl_t *macid_ctl = &adapter->dvobj->macid_ctl;

	if (mac_id < macid_ctl->num) {
		u8 id_is_shared = mac_id == RTW_DEFAULT_MGMT_MACID; /* TODO: real shared macid judgment */

		RTW_INFO(FUNC_ADPT_FMT" - mac_id=%d%s\n", FUNC_ADPT_ARG(adapter)
			, mac_id, id_is_shared ? " shared" : "");

		if (!id_is_shared) {
			rtw_hal_macid_drop(adapter, mac_id);
			rtw_hal_set_FwMediaStatusRpt_single_cmd(adapter, 0, 0, 0, 0, mac_id);
			/*
			 * For safety, prevent from keeping macid sleep.
			 * If we can sure all power mode enter/leave are paired,
			 * this check can be removed.
			 * Lucas@20131113
			 */
			/* wakeup macid after disconnect. */
			/*if (MLME_IS_STA(adapter))*/
			rtw_hal_macid_wakeup(adapter, mac_id);
		}
	} else {
		RTW_PRINT(FUNC_ADPT_FMT" invalid macid:%u\n"
			  , FUNC_ADPT_ARG(adapter), mac_id);
		rtw_warn_on(1);
	}
}
void rtw_sta_mstatus_report(_adapter *adapter)
{
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct wlan_network *tgt_network = &pmlmepriv->cur_network;
	struct sta_info *psta = NULL;

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) && check_fwstate(pmlmepriv, WIFI_ASOC_STATE)) {
		psta = rtw_get_stainfo(&adapter->stapriv, tgt_network->network.MacAddress);
		if (psta)
			rtw_sta_mstatus_disc_rpt(adapter, psta->cmn.mac_id);
		else {
			RTW_INFO("%s "ADPT_FMT" - mac_addr: "MAC_FMT" psta == NULL\n", __func__, ADPT_ARG(adapter), MAC_ARG(tgt_network->network.MacAddress));
			rtw_warn_on(1);
		}
	}
}

void rtw_stadel_event_callback(_adapter *adapter, u8 *pbuf)
{
	_irqL irqL, irqL2;

	struct sta_info *psta;
	struct wlan_network *pwlan = NULL;
	WLAN_BSSID_EX    *pdev_network = NULL;
	u8 *pibss = NULL;
	struct	mlme_priv	*pmlmepriv = &(adapter->mlmepriv);
	struct	stadel_event *pstadel	= (struct stadel_event *)pbuf;
	struct wlan_network *tgt_network = &(pmlmepriv->cur_network);

	RTW_INFO("%s(mac_id=%d)=" MAC_FMT "\n", __func__, pstadel->mac_id, MAC_ARG(pstadel->macaddr));
	rtw_sta_mstatus_disc_rpt(adapter, pstadel->mac_id);

#ifdef CONFIG_MCC_MODE
	rtw_hal_mcc_update_macid_bitmap(adapter, pstadel->mac_id, _FALSE);
#endif /* CONFIG_MCC_MODE */

	psta = rtw_get_stainfo(&adapter->stapriv, pstadel->macaddr);

	if (psta == NULL) {
		RTW_INFO("%s(mac_id=%d)=" MAC_FMT " psta == NULL\n", __func__, pstadel->mac_id, MAC_ARG(pstadel->macaddr));
		/*rtw_warn_on(1);*/
	}

	if (psta)
		rtw_wfd_st_switch(psta, 0);

	if (MLME_IS_MESH(adapter)) {
		rtw_free_stainfo(adapter, psta);
		goto exit;
	}

	if (MLME_IS_AP(adapter)) {
#ifdef CONFIG_IOCTL_CFG80211
#ifdef COMPAT_KERNEL_RELEASE

#elif (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37)) || defined(CONFIG_CFG80211_FORCE_COMPATIBLE_2_6_37_UNDER)
		rtw_cfg80211_indicate_sta_disassoc(adapter, pstadel->macaddr, *(u16 *)pstadel->rsvd);
#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37)) || defined(CONFIG_CFG80211_FORCE_COMPATIBLE_2_6_37_UNDER) */
#endif /* CONFIG_IOCTL_CFG80211 */

		rtw_free_stainfo(adapter, psta);

		goto exit;
	}

	mlmeext_sta_del_event_callback(adapter);

	_enter_critical_bh(&pmlmepriv->lock, &irqL2);

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
		u16 reason = *((unsigned short *)(pstadel->rsvd));
		bool roam = _FALSE;
		struct wlan_network *roam_target = NULL;

#ifdef CONFIG_LAYER2_ROAMING
#ifdef CONFIG_RTW_80211R
		if (rtw_ft_roam_expired(adapter, reason))
			pmlmepriv->ft_roam.ft_roam_on_expired = _TRUE;
		else
			pmlmepriv->ft_roam.ft_roam_on_expired = _FALSE;
#endif
		if (adapter->registrypriv.wifi_spec == 1)
			roam = _FALSE;
		else if (reason == WLAN_REASON_EXPIRATION_CHK && rtw_chk_roam_flags(adapter, RTW_ROAM_ON_EXPIRED))
			roam = _TRUE;
		else if (reason == WLAN_REASON_ACTIVE_ROAM && rtw_chk_roam_flags(adapter, RTW_ROAM_ACTIVE)) {
			roam = _TRUE;
			roam_target = pmlmepriv->roam_network;
		}
		if (roam == _TRUE) {
			if (rtw_to_roam(adapter) > 0)
				rtw_dec_to_roam(adapter); /* this stadel_event is caused by roaming, decrease to_roam */
			else if (rtw_to_roam(adapter) == 0)
				rtw_set_to_roam(adapter, adapter->registrypriv.max_roaming_times);
		} else
			rtw_set_to_roam(adapter, 0);
#endif /* CONFIG_LAYER2_ROAMING */

		rtw_free_uc_swdec_pending_queue(adapter);

		rtw_free_assoc_resources(adapter, _TRUE);
		rtw_free_mlme_priv_ie_data(pmlmepriv);

		rtw_indicate_disconnect(adapter, *(u16 *)pstadel->rsvd, pstadel->locally_generated);

		_rtw_roaming(adapter, roam_target);
	}

	if (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) ||
	    check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)) {

		/* _enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL); */
		rtw_free_stainfo(adapter,  psta);
		/* _exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL); */

		if (adapter->stapriv.asoc_sta_count == 1) { /* a sta + bc/mc_stainfo (not Ibss_stainfo) */
			/* rtw_indicate_disconnect(adapter); */ /* removed@20091105 */
			_enter_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
			/* free old ibss network */
			/* pwlan = _rtw_find_network(&pmlmepriv->scanned_queue, pstadel->macaddr); */
			pwlan = _rtw_find_network(&pmlmepriv->scanned_queue, tgt_network->network.MacAddress);
			if (pwlan) {
				pwlan->fixed = _FALSE;
				rtw_free_network_nolock(adapter, pwlan);
			}
			_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
			/* re-create ibss */
			pdev_network = &(adapter->registrypriv.dev_network);
			pibss = adapter->registrypriv.dev_network.MacAddress;

			_rtw_memcpy(pdev_network, &tgt_network->network, get_WLAN_BSSID_EX_sz(&tgt_network->network));

			_rtw_memset(&pdev_network->Ssid, 0, sizeof(NDIS_802_11_SSID));
			_rtw_memcpy(&pdev_network->Ssid, &pmlmepriv->assoc_ssid, sizeof(NDIS_802_11_SSID));

			rtw_update_registrypriv_dev_network(adapter);

			rtw_generate_random_ibss(pibss);

			if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)) {
				set_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE);
				_clr_fwstate_(pmlmepriv, WIFI_ADHOC_STATE);
			}

			if (rtw_create_ibss_cmd(adapter, 0) != _SUCCESS)
				RTW_ERR("rtw_create_ibss_cmd FAIL\n");

		}

	}

	_exit_critical_bh(&pmlmepriv->lock, &irqL2);
exit:
	#ifdef CONFIG_RTS_FULL_BW
	rtw_set_rts_bw(adapter);
	#endif/*CONFIG_RTS_FULL_BW*/
	return;
}

void rtw_wmm_event_callback(PADAPTER padapter, u8 *pbuf)
{

	WMMOnAssocRsp(padapter);


}

/*
* rtw_join_timeout_handler - Timeout/failure handler for CMD JoinBss
*/
void rtw_join_timeout_handler(void *ctx)
{
	_adapter *adapter = (_adapter *)ctx;
	_irqL irqL;
	struct	mlme_priv *pmlmepriv = &adapter->mlmepriv;

#if 0
	if (rtw_is_drv_stopped(adapter)) {
		_rtw_up_sema(&pmlmepriv->assoc_terminate);
		return;
	}
#endif



	RTW_INFO("%s, fw_state=%x\n", __FUNCTION__, get_fwstate(pmlmepriv));

	if (RTW_CANNOT_RUN(adapter))
		return;


	_enter_critical_bh(&pmlmepriv->lock, &irqL);

#ifdef CONFIG_LAYER2_ROAMING
	if (rtw_to_roam(adapter) > 0) { /* join timeout caused by roaming */
		while (1) {
			rtw_dec_to_roam(adapter);
			if (rtw_to_roam(adapter) != 0) { /* try another */
				int do_join_r;
				RTW_INFO("%s try another roaming\n", __FUNCTION__);
				do_join_r = rtw_do_join(adapter);
				if (_SUCCESS != do_join_r) {
					RTW_INFO("%s roaming do_join return %d\n", __FUNCTION__ , do_join_r);
					continue;
				}
				break;
			} else {
				RTW_INFO("%s We've try roaming but fail\n", __FUNCTION__);
#ifdef CONFIG_RTW_80211R
				rtw_ft_clr_flags(adapter, RTW_FT_PEER_EN|RTW_FT_PEER_OTD_EN);
				rtw_ft_reset_status(adapter);
#endif
				rtw_indicate_disconnect(adapter, pmlmepriv->join_status, _FALSE);
				break;
			}
		}

	} else
#endif
	{
		rtw_indicate_disconnect(adapter, pmlmepriv->join_status, _FALSE);
		free_scanqueue(pmlmepriv);/* ??? */

#ifdef CONFIG_IOCTL_CFG80211
		/* indicate disconnect for the case that join_timeout and check_fwstate != FW_LINKED */
		rtw_cfg80211_indicate_disconnect(adapter, pmlmepriv->join_status, _FALSE);
#endif /* CONFIG_IOCTL_CFG80211 */

	}

	pmlmepriv->join_status = 0; /* reset */

	_exit_critical_bh(&pmlmepriv->lock, &irqL);


#ifdef CONFIG_DRVEXT_MODULE_WSC
	drvext_assoc_fail_indicate(&adapter->drvextpriv);
#endif



}

/*
* rtw_scan_timeout_handler - Timeout/Faliure handler for CMD SiteSurvey
* @adapter: pointer to _adapter structure
*/
void rtw_scan_timeout_handler(void *ctx)
{
	_adapter *adapter = (_adapter *)ctx;
	_irqL irqL;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	RTW_INFO(FUNC_ADPT_FMT" fw_state=%x\n", FUNC_ADPT_ARG(adapter), get_fwstate(pmlmepriv));

	_enter_critical_bh(&pmlmepriv->lock, &irqL);

	_clr_fwstate_(pmlmepriv, WIFI_UNDER_SURVEY);

	_exit_critical_bh(&pmlmepriv->lock, &irqL);

#ifdef CONFIG_IOCTL_CFG80211
	rtw_cfg80211_surveydone_event_callback(adapter);
#endif /* CONFIG_IOCTL_CFG80211 */

	rtw_indicate_scan_done(adapter, _TRUE);

#if defined(CONFIG_CONCURRENT_MODE) && defined(CONFIG_IOCTL_CFG80211)
	rtw_cfg80211_indicate_scan_done_for_buddy(adapter, _TRUE);
#endif
}

void rtw_mlme_reset_auto_scan_int(_adapter *adapter, u8 *reason)
{
#if defined(CONFIG_RTW_MESH) && defined(CONFIG_DFS_MASTER)
#if CONFIG_RTW_MESH_OFFCH_CAND 
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
#endif
#endif
	u8 u_ch;
	u32 interval_ms = 0xffffffff; /* 0xffffffff: special value to make min() works well, also means no auto scan */

	*reason = RTW_AUTO_SCAN_REASON_UNSPECIFIED;
	rtw_mi_get_ch_setting_union(adapter, &u_ch, NULL, NULL);

	if (hal_chk_bw_cap(adapter, BW_CAP_40M)
		&& is_client_associated_to_ap(adapter) == _TRUE
		&& u_ch >= 1 && u_ch <= 14
		&& adapter->registrypriv.wifi_spec
		/* TODO: AP Connected is 40MHz capability? */
	) {
		interval_ms = rtw_min(interval_ms, 60 * 1000);
		*reason |= RTW_AUTO_SCAN_REASON_2040_BSS;
	}

#ifdef CONFIG_RTW_MESH
	#if CONFIG_RTW_MESH_OFFCH_CAND
	if (adapter->mesh_cfg.peer_sel_policy.offch_find_int_ms
		&& rtw_mesh_offch_candidate_accepted(adapter)
		#ifdef CONFIG_DFS_MASTER
		&& (!rfctl->radar_detect_ch || (IS_CH_WAITING(rfctl) && !IS_UNDER_CAC(rfctl)))
		#endif
	) {
		interval_ms = rtw_min(interval_ms, adapter->mesh_cfg.peer_sel_policy.offch_find_int_ms);
		*reason |= RTW_AUTO_SCAN_REASON_MESH_OFFCH_CAND;
	}
	#endif
#endif /* CONFIG_RTW_MESH */

	if (interval_ms == 0xffffffff)
		interval_ms = 0;

	rtw_mlme_set_auto_scan_int(adapter, interval_ms);
	return;
}

void rtw_drv_scan_by_self(_adapter *padapter, u8 reason)
{
	struct sitesurvey_parm parm;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	int i;
#if 1
	u8 ssc_chk;

	ssc_chk = rtw_sitesurvey_condition_check(padapter, _FALSE);
	if( ssc_chk == SS_DENY_BUSY_TRAFFIC) {
		#ifdef CONFIG_LAYER2_ROAMING
		if (rtw_chk_roam_flags(padapter, RTW_ROAM_ACTIVE) && pmlmepriv->need_to_roam == _TRUE)
			RTW_INFO(FUNC_ADPT_FMT" need to roam, don't care BusyTraffic\n", FUNC_ADPT_ARG(padapter));
		else
		#endif
			RTW_INFO(FUNC_ADPT_FMT" exit BusyTraffic\n", FUNC_ADPT_ARG(padapter));
			goto exit;
	}
	else if (ssc_chk != SS_ALLOW)
		goto exit;

	if (!rtw_is_adapter_up(padapter))
		goto exit;
#else
	if (rtw_is_scan_deny(padapter))
		goto exit;

	if (!rtw_is_adapter_up(padapter))
		goto exit;

	if (rtw_mi_busy_traffic_check(padapter)) {
#ifdef CONFIG_LAYER2_ROAMING
		if (rtw_chk_roam_flags(padapter, RTW_ROAM_ACTIVE) && pmlmepriv->need_to_roam == _TRUE) {
			RTW_INFO("need to roam, don't care BusyTraffic\n");
		} else
#endif
		{
			RTW_INFO(FUNC_ADPT_FMT" exit BusyTraffic\n", FUNC_ADPT_ARG(padapter));
			goto exit;
		}
	}
	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) && check_fwstate(pmlmepriv, WIFI_UNDER_WPS)) {
		RTW_INFO(FUNC_ADPT_FMT" WIFI_AP_STATE && WIFI_UNDER_WPS\n", FUNC_ADPT_ARG(padapter));
		goto exit;
	}
	if (check_fwstate(pmlmepriv, (WIFI_UNDER_SURVEY | WIFI_UNDER_LINKING)) == _TRUE) {
		RTW_INFO(FUNC_ADPT_FMT" WIFI_UNDER_SURVEY|WIFI_UNDER_LINKING\n", FUNC_ADPT_ARG(padapter));
		goto exit;
	}

#ifdef CONFIG_CONCURRENT_MODE
	if (rtw_mi_buddy_check_fwstate(padapter, (WIFI_UNDER_SURVEY | WIFI_UNDER_LINKING | WIFI_UNDER_WPS))) {
		RTW_INFO(FUNC_ADPT_FMT", but buddy_intf is under scanning or linking or wps_phase\n", FUNC_ADPT_ARG(padapter));
		goto exit;
	}
#endif
#endif

	RTW_INFO(FUNC_ADPT_FMT" reason:0x%02x\n", FUNC_ADPT_ARG(padapter), reason);

	/* only for 20/40 BSS */
	if (reason == RTW_AUTO_SCAN_REASON_2040_BSS) {
		rtw_init_sitesurvey_parm(padapter, &parm);
		for (i=0;i<14;i++) {
			parm.ch[i].hw_value = i + 1;
			parm.ch[i].flags = RTW_IEEE80211_CHAN_PASSIVE_SCAN;
		}
		parm.ch_num = 14;
		rtw_set_802_11_bssid_list_scan(padapter, &parm);
		goto exit;
	}

#if defined(CONFIG_RTW_WNM) || defined(CONFIG_RTW_80211K)
	if ((reason == RTW_AUTO_SCAN_REASON_ROAM) 
		&& (rtw_roam_nb_scan_list_set(padapter, &parm)))
		goto exit;
#endif

	rtw_set_802_11_bssid_list_scan(padapter, NULL);
exit:
	return;
}

static void rtw_auto_scan_handler(_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	u8 reason = RTW_AUTO_SCAN_REASON_UNSPECIFIED;

	rtw_mlme_reset_auto_scan_int(padapter, &reason);

#ifdef CONFIG_P2P
	if (!rtw_p2p_chk_state(&padapter->wdinfo, P2P_STATE_NONE))
		goto exit;
#endif

#ifdef CONFIG_TDLS
	if (padapter->tdlsinfo.link_established == _TRUE)
		goto exit;
#endif

	if (pmlmepriv->auto_scan_int_ms == 0
	    || rtw_get_passing_time_ms(pmlmepriv->scan_start_time) < pmlmepriv->auto_scan_int_ms)
		goto exit;

	rtw_drv_scan_by_self(padapter, reason);

exit:
	return;
}
static u8 is_drv_in_lps(_adapter *adapter)
{
	u8 is_in_lps = _FALSE;

	#ifdef CONFIG_LPS_LCLK_WD_TIMER /* to avoid leaving lps 32k frequently*/
	if ((adapter_to_pwrctl(adapter)->bFwCurrentInPSMode == _TRUE)
	#ifdef CONFIG_BT_COEXIST
		&& (rtw_btcoex_IsBtControlLps(adapter) == _FALSE)
	#endif
		)
		is_in_lps = _TRUE;
	#endif /* CONFIG_LPS_LCLK_WD_TIMER*/
	return is_in_lps;
}
void rtw_iface_dynamic_check_timer_handlder(_adapter *adapter)
{
#ifdef CONFIG_AP_MODE
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
#endif /* CONFIG_AP_MODE */

	if (adapter->net_closed == _TRUE)
		return;
	#ifdef CONFIG_LPS_LCLK_WD_TIMER /* to avoid leaving lps 32k frequently*/
	if (is_drv_in_lps(adapter)) {
		u8 bEnterPS;

		linked_status_chk(adapter, 1);

		bEnterPS = traffic_status_watchdog(adapter, 1);
		if (bEnterPS) {
			/* rtw_lps_ctrl_wk_cmd(adapter, LPS_CTRL_ENTER, 0); */
			rtw_hal_dm_watchdog_in_lps(adapter);
		} else {
			/* call rtw_lps_ctrl_wk_cmd(padapter, LPS_CTRL_LEAVE, 0) in traffic_status_watchdog() */
		}
	}
	#endif /* CONFIG_LPS_LCLK_WD_TIMER	*/

	/* auto site survey */
	rtw_auto_scan_handler(adapter);

#ifdef CONFIG_AP_MODE
	if (MLME_IS_AP(adapter)|| MLME_IS_MESH(adapter)) {
		#ifndef CONFIG_ACTIVE_KEEP_ALIVE_CHECK
		expire_timeout_chk(adapter);
		#endif /* !CONFIG_ACTIVE_KEEP_ALIVE_CHECK */

		#ifdef CONFIG_BMC_TX_RATE_SELECT
		rtw_update_bmc_sta_tx_rate(adapter);
		#endif /*CONFIG_BMC_TX_RATE_SELECT*/
	}
#endif /*CONFIG_AP_MODE*/


#ifdef CONFIG_BR_EXT

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 35))
	rcu_read_lock();
#endif /* (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 35)) */

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35))
	if (adapter->pnetdev->br_port
#else	/* (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35)) */
	if (rcu_dereference(adapter->pnetdev->rx_handler_data)
#endif /* (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35)) */
		&& (check_fwstate(pmlmepriv, WIFI_STATION_STATE | WIFI_ADHOC_STATE) == _TRUE)) {
		/* expire NAT2.5 entry */
		void nat25_db_expire(_adapter *priv);
		nat25_db_expire(adapter);

		if (adapter->pppoe_connection_in_progress > 0)
			adapter->pppoe_connection_in_progress--;
		/* due to rtw_dynamic_check_timer_handlder() is called every 2 seconds */
		if (adapter->pppoe_connection_in_progress > 0)
			adapter->pppoe_connection_in_progress--;
	}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 35))
	rcu_read_unlock();
#endif /* (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 35)) */

#endif /* CONFIG_BR_EXT */

}

/*TP_avg(t) = (1/10) * TP_avg(t-1) + (9/10) * TP(t) MBps*/
static void collect_sta_traffic_statistics(_adapter *adapter)
{
	struct macid_ctl_t *macid_ctl = &adapter->dvobj->macid_ctl;
	struct sta_info *sta;
	u64 curr_tx_bytes = 0, curr_rx_bytes = 0;
	u32 curr_tx_mbytes = 0, curr_rx_mbytes = 0;
	int i;

	for (i = 0; i < MACID_NUM_SW_LIMIT; i++) {
		sta = macid_ctl->sta[i];
		if (sta && !is_broadcast_mac_addr(sta->cmn.mac_addr)) {
			if (sta->sta_stats.last_tx_bytes > sta->sta_stats.tx_bytes)
				sta->sta_stats.last_tx_bytes =  sta->sta_stats.tx_bytes;
			if (sta->sta_stats.last_rx_bytes > sta->sta_stats.rx_bytes)
				sta->sta_stats.last_rx_bytes = sta->sta_stats.rx_bytes;
			if (sta->sta_stats.last_rx_bc_bytes > sta->sta_stats.rx_bc_bytes)
				sta->sta_stats.last_rx_bc_bytes = sta->sta_stats.rx_bc_bytes;
			if (sta->sta_stats.last_rx_mc_bytes > sta->sta_stats.rx_mc_bytes)
				sta->sta_stats.last_rx_mc_bytes = sta->sta_stats.rx_mc_bytes;

			curr_tx_bytes = sta->sta_stats.tx_bytes - sta->sta_stats.last_tx_bytes;
			curr_rx_bytes = sta->sta_stats.rx_bytes - sta->sta_stats.last_rx_bytes;
			sta->sta_stats.tx_tp_kbits = (curr_tx_bytes * 8 / 2) >> 10;/*Kbps*/
			sta->sta_stats.rx_tp_kbits = (curr_rx_bytes * 8 / 2) >> 10;/*Kbps*/

			sta->sta_stats.smooth_tx_tp_kbits = (sta->sta_stats.smooth_tx_tp_kbits * 6 / 10) + (sta->sta_stats.tx_tp_kbits * 4 / 10);/*Kbps*/
			sta->sta_stats.smooth_rx_tp_kbits = (sta->sta_stats.smooth_rx_tp_kbits * 6 / 10) + (sta->sta_stats.rx_tp_kbits * 4 / 10);/*Kbps*/

			curr_tx_mbytes = (curr_tx_bytes / 2) >> 20;/*MBps*/
			curr_rx_mbytes = (curr_rx_bytes / 2) >> 20;/*MBps*/

			sta->cmn.tx_moving_average_tp =
				(sta->cmn.tx_moving_average_tp / 10) + (curr_tx_mbytes * 9 / 10); /*MBps*/

			sta->cmn.rx_moving_average_tp =
				(sta->cmn.rx_moving_average_tp / 10) + (curr_rx_mbytes * 9 /10); /*MBps*/

			rtw_collect_bcn_info(sta->padapter);

			if (adapter->bsta_tp_dump)
				dump_sta_traffic(RTW_DBGDUMP, adapter, sta);

			sta->sta_stats.last_tx_bytes = sta->sta_stats.tx_bytes;
			sta->sta_stats.last_rx_bytes = sta->sta_stats.rx_bytes;
			sta->sta_stats.last_rx_bc_bytes = sta->sta_stats.rx_bc_bytes;
			sta->sta_stats.last_rx_mc_bytes = sta->sta_stats.rx_mc_bytes;
		}
	}
}

void rtw_sta_traffic_info(void *sel, _adapter *adapter)
{
	struct macid_ctl_t *macid_ctl = &adapter->dvobj->macid_ctl;
	struct sta_info *sta;
	int i;

	for (i = 0; i < MACID_NUM_SW_LIMIT; i++) {
		sta = macid_ctl->sta[i];
		if (sta && !is_broadcast_mac_addr(sta->cmn.mac_addr))
			dump_sta_traffic(sel, adapter, sta);
	}
}

/*#define DBG_TRAFFIC_STATISTIC*/
static void collect_traffic_statistics(_adapter *padapter)
{
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);

	/*_rtw_memset(&pdvobjpriv->traffic_stat, 0, sizeof(struct rtw_traffic_statistics));*/

	/* Tx bytes reset*/
	pdvobjpriv->traffic_stat.tx_bytes = 0;
	pdvobjpriv->traffic_stat.tx_pkts = 0;
	pdvobjpriv->traffic_stat.tx_drop = 0;

	/* Rx bytes reset*/
	pdvobjpriv->traffic_stat.rx_bytes = 0;
	pdvobjpriv->traffic_stat.rx_pkts = 0;
	pdvobjpriv->traffic_stat.rx_drop = 0;

	rtw_mi_traffic_statistics(padapter);

	/* Calculate throughput in last interval */
	pdvobjpriv->traffic_stat.cur_tx_bytes = pdvobjpriv->traffic_stat.tx_bytes - pdvobjpriv->traffic_stat.last_tx_bytes;
	pdvobjpriv->traffic_stat.cur_rx_bytes = pdvobjpriv->traffic_stat.rx_bytes - pdvobjpriv->traffic_stat.last_rx_bytes;
	pdvobjpriv->traffic_stat.last_tx_bytes = pdvobjpriv->traffic_stat.tx_bytes;
	pdvobjpriv->traffic_stat.last_rx_bytes = pdvobjpriv->traffic_stat.rx_bytes;

	pdvobjpriv->traffic_stat.cur_tx_tp = (u32)(pdvobjpriv->traffic_stat.cur_tx_bytes * 8 / 2 / 1024 / 1024);/*Mbps*/
	pdvobjpriv->traffic_stat.cur_rx_tp = (u32)(pdvobjpriv->traffic_stat.cur_rx_bytes * 8 / 2 / 1024 / 1024);/*Mbps*/

	#ifdef DBG_TRAFFIC_STATISTIC
	RTW_INFO("\n========================\n");
	RTW_INFO("cur_tx_bytes:%lld\n", pdvobjpriv->traffic_stat.cur_tx_bytes);
	RTW_INFO("cur_rx_bytes:%lld\n", pdvobjpriv->traffic_stat.cur_rx_bytes);

	RTW_INFO("last_tx_bytes:%lld\n", pdvobjpriv->traffic_stat.last_tx_bytes);
	RTW_INFO("last_rx_bytes:%lld\n", pdvobjpriv->traffic_stat.last_rx_bytes);

	RTW_INFO("cur_tx_tp:%d (Mbps)\n", pdvobjpriv->traffic_stat.cur_tx_tp);
	RTW_INFO("cur_rx_tp:%d (Mbps)\n", pdvobjpriv->traffic_stat.cur_rx_tp);
	#endif

#ifdef CONFIG_RTW_NAPI
#ifdef CONFIG_RTW_NAPI_DYNAMIC
	dynamic_napi_th_chk (padapter);
#endif /* CONFIG_RTW_NAPI_DYNAMIC */
#endif
	
}

void rtw_dynamic_check_timer_handlder(void *ctx)
{
	struct dvobj_priv *pdvobj = (struct dvobj_priv *)ctx;
	_adapter *adapter = dvobj_get_primary_adapter(pdvobj);

	if (!adapter)
		goto exit;

#if (MP_DRIVER == 1)
	if (adapter->registrypriv.mp_mode == 1 && adapter->mppriv.mp_dm == 0) { /* for MP ODM dynamic Tx power tracking */
		/* RTW_INFO("%s mp_dm =0 return\n", __func__); */
		goto exit;
	}
#endif

	if (!rtw_is_hw_init_completed(adapter))
		goto exit;

	if (RTW_CANNOT_RUN(adapter))
		goto exit;

	collect_traffic_statistics(adapter);
	collect_sta_traffic_statistics(adapter);
	rtw_mi_dynamic_check_timer_handlder(adapter);

	if (!is_drv_in_lps(adapter))
		rtw_dynamic_chk_wk_cmd(adapter);

exit:
	_set_timer(&pdvobj->dynamic_chk_timer, 2000);
}


#ifdef CONFIG_SET_SCAN_DENY_TIMER
inline bool rtw_is_scan_deny(_adapter *adapter)
{
	struct mlme_priv *mlmepriv = &adapter->mlmepriv;
	return (ATOMIC_READ(&mlmepriv->set_scan_deny) != 0) ? _TRUE : _FALSE;
}

inline void rtw_clear_scan_deny(_adapter *adapter)
{
	struct mlme_priv *mlmepriv = &adapter->mlmepriv;
	ATOMIC_SET(&mlmepriv->set_scan_deny, 0);
	if (0)
		RTW_INFO(FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(adapter));
}

void rtw_set_scan_deny_timer_hdl(void *ctx)
{
	_adapter *adapter = (_adapter *)ctx;

	rtw_clear_scan_deny(adapter);
}
void rtw_set_scan_deny(_adapter *adapter, u32 ms)
{
	struct mlme_priv *mlmepriv = &adapter->mlmepriv;
	if (0)
		RTW_INFO(FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(adapter));
	ATOMIC_SET(&mlmepriv->set_scan_deny, 1);
	_set_timer(&mlmepriv->set_scan_deny_timer, ms);
}
#endif

#ifdef CONFIG_LAYER2_ROAMING
/*
* Select a new roaming candidate from the original @param candidate and @param competitor
* @return _TRUE: candidate is updated
* @return _FALSE: candidate is not updated
*/
static int rtw_check_roaming_candidate(struct mlme_priv *mlme
	, struct wlan_network **candidate, struct wlan_network *competitor)
{
	int updated = _FALSE;
	_adapter *adapter = container_of(mlme, _adapter, mlmepriv);
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	RT_CHANNEL_INFO *chset = rfctl->channel_set;
	u8 ch = competitor->network.Configuration.DSConfig;

	if (rtw_chset_search_ch(chset, ch) < 0)
		goto exit;
	if (IS_DFS_SLAVE_WITH_RD(rfctl)
		&& !rtw_odm_dfs_domain_unknown(rfctl_to_dvobj(rfctl))
		&& rtw_chset_is_ch_non_ocp(chset, ch))
		goto exit;

#if defined(CONFIG_RTW_REPEATER_SON) &&  (!defined(CONFIG_RTW_REPEATER_SON_ROOT))
	if (rtw_rson_isupdate_roamcan(mlme, candidate, competitor))
		goto  update;
	goto exit;
#endif

	if (is_same_ess(&competitor->network, &mlme->cur_network.network) == _FALSE)
		goto exit;

	if (rtw_is_desired_network(adapter, competitor) == _FALSE)
		goto exit;

#ifdef CONFIG_LAYER2_ROAMING
	if (mlme->need_to_roam == _FALSE)
		goto exit;
#endif

#ifdef CONFIG_RTW_80211R
	if (rtw_ft_chk_flags(adapter, RTW_FT_PEER_EN)) {
		if (rtw_ft_chk_roaming_candidate(adapter, competitor) == _FALSE)
		goto exit;
	}
#endif

	RTW_INFO("roam candidate:%s %s("MAC_FMT", ch%3u) rssi:%d, age:%5d\n",
		 (competitor == mlme->cur_network_scanned) ? "*" : " " ,
		 competitor->network.Ssid.Ssid,
		 MAC_ARG(competitor->network.MacAddress),
		 competitor->network.Configuration.DSConfig,
		 (int)competitor->network.Rssi,
		 rtw_get_passing_time_ms(competitor->last_scanned)
		);

	/* got specific addr to roam */
	if (!is_zero_mac_addr(mlme->roam_tgt_addr)) {
		if (_rtw_memcmp(mlme->roam_tgt_addr, competitor->network.MacAddress, ETH_ALEN) == _TRUE)
			goto update;
		else
			goto exit;
	}
#if 1
	if (rtw_get_passing_time_ms(competitor->last_scanned) >= mlme->roam_scanr_exp_ms)
		goto exit;

#if defined(CONFIG_RTW_80211R) && defined(CONFIG_RTW_WNM)
	if (rtw_wnm_btm_diff_bss(adapter) && 
		rtw_wnm_btm_roam_candidate(adapter, competitor)) {
		goto update;
	}	
#endif

	if (competitor->network.Rssi - mlme->cur_network_scanned->network.Rssi < mlme->roam_rssi_diff_th)
		goto exit;

	if (*candidate != NULL && (*candidate)->network.Rssi >= competitor->network.Rssi)
		goto exit;
#else
	goto exit;
#endif

update:
	*candidate = competitor;
	updated = _TRUE;

exit:
	return updated;
}

int rtw_select_roaming_candidate(struct mlme_priv *mlme)
{
	_irqL	irqL;
	int ret = _FAIL;
	_list	*phead;
	_adapter *adapter;
	_queue	*queue	= &(mlme->scanned_queue);
	struct	wlan_network	*pnetwork = NULL;
	struct	wlan_network	*candidate = NULL;

	if (mlme->cur_network_scanned == NULL) {
		rtw_warn_on(1);
		goto exit;
	}

	_enter_critical_bh(&(mlme->scanned_queue.lock), &irqL);
	phead = get_list_head(queue);
	adapter = (_adapter *)mlme->nic_hdl;

	mlme->pscanned = get_next(phead);

	while (!rtw_end_of_queue_search(phead, mlme->pscanned)) {

		pnetwork = LIST_CONTAINOR(mlme->pscanned, struct wlan_network, list);
		if (pnetwork == NULL) {
			ret = _FAIL;
			goto exit;
		}

		mlme->pscanned = get_next(mlme->pscanned);

		if (0)
			RTW_INFO("%s("MAC_FMT", ch%u) rssi:%d\n"
				 , pnetwork->network.Ssid.Ssid
				 , MAC_ARG(pnetwork->network.MacAddress)
				 , pnetwork->network.Configuration.DSConfig
				 , (int)pnetwork->network.Rssi);

		rtw_check_roaming_candidate(mlme, &candidate, pnetwork);

	}

	if (candidate == NULL) {
	/*	if parent note lost the path to root and there is no other cadidate, report disconnection	*/
#if defined(CONFIG_RTW_REPEATER_SON) &&  (!defined(CONFIG_RTW_REPEATER_SON_ROOT))
		struct rtw_rson_struct  rson_curr;
		u8 rson_score;

		rtw_get_rson_struct(&(mlme->cur_network_scanned->network), &rson_curr);
		rson_score = rtw_cal_rson_score(&rson_curr, mlme->cur_network_scanned->network.Rssi);
		if (check_fwstate(mlme, WIFI_ASOC_STATE)
			&& ((rson_score == RTW_RSON_SCORE_NOTCNNT)
			|| (rson_score == RTW_RSON_SCORE_NOTSUP)))
			receive_disconnect(adapter, mlme->cur_network_scanned->network.MacAddress
								, WLAN_REASON_EXPIRATION_CHK, _FALSE);
#endif
		RTW_INFO("%s: return _FAIL(candidate == NULL)\n", __FUNCTION__);
		ret = _FAIL;
		goto exit;
	} else {
#if defined(CONFIG_RTW_REPEATER_SON) &&  (!defined(CONFIG_RTW_REPEATER_SON_ROOT))
		struct rtw_rson_struct  rson_curr;
		u8 rson_score;

		rtw_get_rson_struct(&(candidate->network), &rson_curr);
		rson_score = rtw_cal_rson_score(&rson_curr, candidate->network.Rssi);
		RTW_INFO("%s: candidate: %s("MAC_FMT", ch:%u) rson_score:%d\n", __FUNCTION__,
			candidate->network.Ssid.Ssid, MAC_ARG(candidate->network.MacAddress),
			 candidate->network.Configuration.DSConfig, rson_score);
#else
		RTW_INFO("%s: candidate: %s("MAC_FMT", ch:%u)\n", __FUNCTION__,
			candidate->network.Ssid.Ssid, MAC_ARG(candidate->network.MacAddress),
			 candidate->network.Configuration.DSConfig);
#endif
		mlme->roam_network = candidate;

		if (_rtw_memcmp(candidate->network.MacAddress, mlme->roam_tgt_addr, ETH_ALEN) == _TRUE)
			_rtw_memset(mlme->roam_tgt_addr, 0, ETH_ALEN);
	}

	ret = _SUCCESS;
exit:
	_exit_critical_bh(&(mlme->scanned_queue.lock), &irqL);

	return ret;
}
#endif /* CONFIG_LAYER2_ROAMING */

/*
* Select a new join candidate from the original @param candidate and @param competitor
* @return _TRUE: candidate is updated
* @return _FALSE: candidate is not updated
*/
static int rtw_check_join_candidate(struct mlme_priv *mlme
	    , struct wlan_network **candidate, struct wlan_network *competitor)
{
	int updated = _FALSE;
	_adapter *adapter = container_of(mlme, _adapter, mlmepriv);
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	RT_CHANNEL_INFO *chset = rfctl->channel_set;
	u8 ch = competitor->network.Configuration.DSConfig;

	if (rtw_chset_search_ch(chset, ch) < 0)
		goto exit;
	if (IS_DFS_SLAVE_WITH_RD(rfctl)
		&& !rtw_odm_dfs_domain_unknown(rfctl_to_dvobj(rfctl))
		&& rtw_chset_is_ch_non_ocp(chset, ch))
		goto exit;

#if defined(CONFIG_RTW_REPEATER_SON) &&  (!defined(CONFIG_RTW_REPEATER_SON_ROOT))
	s16 rson_score;
	struct rtw_rson_struct  rson_data;

	if (rtw_rson_choose(candidate, competitor)) {
		*candidate = competitor;
		rtw_get_rson_struct(&((*candidate)->network), &rson_data);
		rson_score = rtw_cal_rson_score(&rson_data, (*candidate)->network.Rssi);
		RTW_INFO("[assoc_ssid:%s] new candidate: %s("MAC_FMT", ch%u) rson_score:%d\n",
			 mlme->assoc_ssid.Ssid,
			 (*candidate)->network.Ssid.Ssid,
			 MAC_ARG((*candidate)->network.MacAddress),
			 (*candidate)->network.Configuration.DSConfig,
			 rson_score);
		return _TRUE;
	}
	return _FALSE;
#endif

	/* check bssid, if needed */
	if (mlme->assoc_by_bssid == _TRUE) {
		if (_rtw_memcmp(competitor->network.MacAddress, mlme->assoc_bssid, ETH_ALEN) == _FALSE)
			goto exit;
	}

	/* check ssid, if needed */
	if (mlme->assoc_ssid.Ssid[0] && mlme->assoc_ssid.SsidLength) {
		if (competitor->network.Ssid.SsidLength != mlme->assoc_ssid.SsidLength
		    || _rtw_memcmp(competitor->network.Ssid.Ssid, mlme->assoc_ssid.Ssid, mlme->assoc_ssid.SsidLength) == _FALSE
		   )
			goto exit;
	}

	if (rtw_is_desired_network(adapter, competitor)  == _FALSE)
		goto exit;

#ifdef CONFIG_LAYER2_ROAMING
	if (rtw_to_roam(adapter) > 0) {
		if (rtw_get_passing_time_ms(competitor->last_scanned) >= mlme->roam_scanr_exp_ms
		    || is_same_ess(&competitor->network, &mlme->cur_network.network) == _FALSE
		   )
			goto exit;
	}
#endif

	if (*candidate == NULL || (*candidate)->network.Rssi < competitor->network.Rssi) {
		*candidate = competitor;
		updated = _TRUE;
	}

	if (updated) {
		RTW_INFO("[by_bssid:%u][assoc_ssid:%s][to_roam:%u] "
			 "new candidate: %s("MAC_FMT", ch%u) rssi:%d\n",
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
	_irqL	irqL;
	int ret;
	_list	*phead;
	_adapter *adapter;
	_queue	*queue	= &(pmlmepriv->scanned_queue);
	struct	wlan_network	*pnetwork = NULL;
	struct	wlan_network	*candidate = NULL;
#ifdef CONFIG_ANTENNA_DIVERSITY
	u8		bSupportAntDiv = _FALSE;
#endif

	adapter = (_adapter *)pmlmepriv->nic_hdl;

	_enter_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);

#ifdef CONFIG_LAYER2_ROAMING
	if (pmlmepriv->roam_network) {
		candidate = pmlmepriv->roam_network;
		pmlmepriv->roam_network = NULL;
		goto candidate_exist;
	}
#endif

	phead = get_list_head(queue);
	pmlmepriv->pscanned = get_next(phead);

	while (!rtw_end_of_queue_search(phead, pmlmepriv->pscanned)) {

		pnetwork = LIST_CONTAINOR(pmlmepriv->pscanned, struct wlan_network, list);
		if (pnetwork == NULL) {
			ret = _FAIL;
			goto exit;
		}

		pmlmepriv->pscanned = get_next(pmlmepriv->pscanned);

		if (0)
			RTW_INFO("%s("MAC_FMT", ch%u) rssi:%d\n"
				 , pnetwork->network.Ssid.Ssid
				 , MAC_ARG(pnetwork->network.MacAddress)
				 , pnetwork->network.Configuration.DSConfig
				 , (int)pnetwork->network.Rssi);

		rtw_check_join_candidate(pmlmepriv, &candidate, pnetwork);

	}

	if (candidate == NULL) {
		RTW_INFO("%s: return _FAIL(candidate == NULL)\n", __FUNCTION__);
#ifdef CONFIG_WOWLAN
		_clr_fwstate_(pmlmepriv, WIFI_ASOC_STATE | WIFI_UNDER_LINKING);
#endif
		ret = _FAIL;
		goto exit;
	} else {
		RTW_INFO("%s: candidate: %s("MAC_FMT", ch:%u)\n", __FUNCTION__,
			candidate->network.Ssid.Ssid, MAC_ARG(candidate->network.MacAddress),
			 candidate->network.Configuration.DSConfig);
		goto candidate_exist;
	}

candidate_exist:

	/* check for situation of  WIFI_ASOC_STATE */
	if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == _TRUE) {
		RTW_INFO("%s: WIFI_ASOC_STATE while ask_for_joinbss!!!\n", __FUNCTION__);

#if 0 /* for WPA/WPA2 authentication, wpa_supplicant will expect authentication from AP, it is needed to reconnect AP... */
		if (is_same_network(&pmlmepriv->cur_network.network, &candidate->network)) {
			RTW_INFO("%s: WIFI_ASOC_STATE and is same network, it needn't join again\n", __FUNCTION__);

			rtw_indicate_connect(adapter);/* rtw_indicate_connect again */

			ret = 2;
			goto exit;
		} else
#endif
		{
			rtw_disassoc_cmd(adapter, 0, 0);
			rtw_indicate_disconnect(adapter, 0, _FALSE);
			rtw_free_assoc_resources_cmd(adapter, _TRUE, 0);
		}
	}

#ifdef CONFIG_ANTENNA_DIVERSITY
	rtw_hal_get_def_var(adapter, HAL_DEF_IS_SUPPORT_ANT_DIV, &(bSupportAntDiv));
	if (_TRUE == bSupportAntDiv) {
		u8 CurrentAntenna;
		rtw_hal_get_odm_var(adapter, HAL_ODM_ANTDIV_SELECT, &(CurrentAntenna), NULL);
		RTW_INFO("#### Opt_Ant_(%s) , cur_Ant(%s)\n",
			(MAIN_ANT == candidate->network.PhyInfo.Optimum_antenna) ? "MAIN_ANT" : "AUX_ANT",
			 (MAIN_ANT == CurrentAntenna) ? "MAIN_ANT" : "AUX_ANT"
			);
	}
#endif
	set_fwstate(pmlmepriv, WIFI_UNDER_LINKING);
	ret = rtw_joinbss_cmd(adapter, candidate);

exit:
	_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);


	return ret;
}

sint rtw_set_auth(_adapter *adapter, struct security_priv *psecuritypriv)
{
	struct	cmd_obj *pcmd;
	struct	setauth_parm *psetauthparm;
	struct	cmd_priv	*pcmdpriv = &(adapter->cmdpriv);
	sint		res = _SUCCESS;


	pcmd = (struct	cmd_obj *)rtw_zmalloc(sizeof(struct	cmd_obj));
	if (pcmd == NULL) {
		res = _FAIL; /* try again */
		goto exit;
	}

	psetauthparm = (struct setauth_parm *)rtw_zmalloc(sizeof(struct setauth_parm));
	if (psetauthparm == NULL) {
		rtw_mfree((unsigned char *)pcmd, sizeof(struct	cmd_obj));
		res = _FAIL;
		goto exit;
	}

	_rtw_memset(psetauthparm, 0, sizeof(struct setauth_parm));
	psetauthparm->mode = (unsigned char)psecuritypriv->dot11AuthAlgrthm;

	pcmd->cmdcode = CMD_SET_AUTH;
	pcmd->parmbuf = (unsigned char *)psetauthparm;
	pcmd->cmdsz = (sizeof(struct setauth_parm));
	pcmd->rsp = NULL;
	pcmd->rspsz = 0;


	_rtw_init_listhead(&pcmd->list);


	res = rtw_enqueue_cmd(pcmdpriv, pcmd);

exit:


	return res;

}


sint rtw_set_key(_adapter *adapter, struct security_priv *psecuritypriv, sint keyid, u8 set_tx, bool enqueue)
{
	u8	keylen;
	struct cmd_obj		*pcmd;
	struct setkey_parm	*psetkeyparm;
	struct cmd_priv		*pcmdpriv = &(adapter->cmdpriv);
	sint	res = _SUCCESS;


	psetkeyparm = (struct setkey_parm *)rtw_zmalloc(sizeof(struct setkey_parm));
	if (psetkeyparm == NULL) {
		res = _FAIL;
		goto exit;
	}
	_rtw_memset(psetkeyparm, 0, sizeof(struct setkey_parm));

	if (psecuritypriv->dot11AuthAlgrthm == dot11AuthAlgrthm_8021X) {
		psetkeyparm->algorithm = (unsigned char)psecuritypriv->dot118021XGrpPrivacy;
	} else {
		psetkeyparm->algorithm = (u8)psecuritypriv->dot11PrivacyAlgrthm;

	}
	psetkeyparm->keyid = (u8)keyid;/* 0~3 */
	psetkeyparm->set_tx = set_tx;
	if (is_wep_enc(psetkeyparm->algorithm))
		adapter->securitypriv.key_mask |= BIT(psetkeyparm->keyid);

	RTW_INFO("==> rtw_set_key algorithm(%x),keyid(%x),key_mask(%x)\n", psetkeyparm->algorithm, psetkeyparm->keyid, adapter->securitypriv.key_mask);

	switch (psetkeyparm->algorithm) {

	case _WEP40_:
		keylen = 5;
		_rtw_memcpy(&(psetkeyparm->key[0]), &(psecuritypriv->dot11DefKey[keyid].skey[0]), keylen);
		break;
	case _WEP104_:
		keylen = 13;
		_rtw_memcpy(&(psetkeyparm->key[0]), &(psecuritypriv->dot11DefKey[keyid].skey[0]), keylen);
		break;
	case _TKIP_:
		keylen = 16;
		_rtw_memcpy(&psetkeyparm->key, &psecuritypriv->dot118021XGrpKey[keyid], keylen);
		break;
	case _AES_:
	case _GCMP_:
		keylen = 16;
		_rtw_memcpy(&psetkeyparm->key, &psecuritypriv->dot118021XGrpKey[keyid], keylen);
		break;
	case _GCMP_256_:
	case _CCMP_256_:
		keylen = 32;
		_rtw_memcpy(&psetkeyparm->key, &psecuritypriv->dot118021XGrpKey[keyid], keylen);
		break;
	default:
		res = _FAIL;
		rtw_mfree((unsigned char *)psetkeyparm, sizeof(struct setkey_parm));
		goto exit;
	}


	if (enqueue) {
		pcmd = (struct	cmd_obj *)rtw_zmalloc(sizeof(struct	cmd_obj));
		if (pcmd == NULL) {
			rtw_mfree((unsigned char *)psetkeyparm, sizeof(struct setkey_parm));
			res = _FAIL; /* try again */
			goto exit;
		}

		pcmd->cmdcode =CMD_SET_KEY;
		pcmd->parmbuf = (u8 *)psetkeyparm;
		pcmd->cmdsz = (sizeof(struct setkey_parm));
		pcmd->rsp = NULL;
		pcmd->rspsz = 0;

		_rtw_init_listhead(&pcmd->list);

		/* _rtw_init_sema(&(pcmd->cmd_sem), 0); */

		res = rtw_enqueue_cmd(pcmdpriv, pcmd);
	} else {
		setkey_hdl(adapter, (u8 *)psetkeyparm);
		rtw_mfree((u8 *) psetkeyparm, sizeof(struct setkey_parm));
	}
exit:
	return res;

}

#ifdef CONFIG_WMMPS_STA
/*
 * rtw_uapsd_use_default_setting
 * This function is used for setting default uapsd max sp length to uapsd_max_sp_len
 * in qos_priv data structure from registry. In additional, it will also map default uapsd 
 * ac to each uapsd TID, delivery-enabled and trigger-enabled of corresponding TID. 
 * 
 * Arguments:
 * @padapter: _adapter pointer.
 *
 * Auther: Arvin Liu
 * Date: 2017/05/03
 */
void	rtw_uapsd_use_default_setting(_adapter *padapter)
{
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct qos_priv		*pqospriv = &pmlmepriv->qospriv;
	struct registry_priv		*pregistrypriv = &padapter->registrypriv;

	if (pregistrypriv->uapsd_ac_enable != 0) {
		pqospriv->uapsd_max_sp_len = pregistrypriv->uapsd_max_sp_len;
		
		CLEAR_FLAGS(pqospriv->uapsd_tid);
		CLEAR_FLAGS(pqospriv->uapsd_tid_delivery_enabled);
		CLEAR_FLAGS(pqospriv->uapsd_tid_trigger_enabled);

		/* check the uapsd setting of AC_VO from registry then map these setting to each TID if necessary  */
		if(TEST_FLAG(pregistrypriv->uapsd_ac_enable, DRV_CFG_UAPSD_VO)) {
			SET_FLAG(pqospriv->uapsd_tid, WMM_TID7);
			SET_FLAG(pqospriv->uapsd_tid_delivery_enabled, WMM_TID7);
			SET_FLAG(pqospriv->uapsd_tid_trigger_enabled, WMM_TID7);
			SET_FLAG(pqospriv->uapsd_tid, WMM_TID6);
			SET_FLAG(pqospriv->uapsd_tid_delivery_enabled, WMM_TID6);
			SET_FLAG(pqospriv->uapsd_tid_trigger_enabled, WMM_TID6);
		}

		/* check the uapsd setting of AC_VI from registry then map these setting to each TID if necessary  */
		if(TEST_FLAG(pregistrypriv->uapsd_ac_enable, DRV_CFG_UAPSD_VI)) {	
			SET_FLAG(pqospriv->uapsd_tid, WMM_TID5);
			SET_FLAG(pqospriv->uapsd_tid_delivery_enabled, WMM_TID5);
			SET_FLAG(pqospriv->uapsd_tid_trigger_enabled, WMM_TID5);
			SET_FLAG(pqospriv->uapsd_tid, WMM_TID4);
			SET_FLAG(pqospriv->uapsd_tid_delivery_enabled, WMM_TID4);
			SET_FLAG(pqospriv->uapsd_tid_trigger_enabled, WMM_TID4);
		}

		/* check the uapsd setting of AC_BK from registry then map these setting to each TID if necessary  */
		if(TEST_FLAG(pregistrypriv->uapsd_ac_enable, DRV_CFG_UAPSD_BK)) {
			SET_FLAG(pqospriv->uapsd_tid, WMM_TID2);
			SET_FLAG(pqospriv->uapsd_tid_delivery_enabled, WMM_TID2);
			SET_FLAG(pqospriv->uapsd_tid_trigger_enabled, WMM_TID2);
			SET_FLAG(pqospriv->uapsd_tid, WMM_TID1);
			SET_FLAG(pqospriv->uapsd_tid_delivery_enabled, WMM_TID1);
			SET_FLAG(pqospriv->uapsd_tid_trigger_enabled, WMM_TID1);
		}

		/* check the uapsd setting of AC_BE from registry then map these setting to each TID if necessary  */
		if(TEST_FLAG(pregistrypriv->uapsd_ac_enable, DRV_CFG_UAPSD_BE)) {
			SET_FLAG(pqospriv->uapsd_tid, WMM_TID3);
			SET_FLAG(pqospriv->uapsd_tid_delivery_enabled, WMM_TID3);
			SET_FLAG(pqospriv->uapsd_tid_trigger_enabled, WMM_TID3);
			SET_FLAG(pqospriv->uapsd_tid, WMM_TID0);
			SET_FLAG(pqospriv->uapsd_tid_delivery_enabled, WMM_TID0);
			SET_FLAG(pqospriv->uapsd_tid_trigger_enabled, WMM_TID0);
		}

		RTW_INFO("[WMMPS] UAPSD MAX SP Len = 0x%02x, UAPSD TID enabled = 0x%02x\n", 
			pqospriv->uapsd_max_sp_len, (u8)pqospriv->uapsd_tid);
	}

}

/*
 * rtw_is_wmmps_mode
 * This function is used for checking whether Driver and an AP support uapsd function or not.
 * If both of them support uapsd function, it will return true. Otherwise returns false.
 * 
 * Arguments:
 * @padapter: _adapter pointer.
 *
 * Auther: Arvin Liu
 * Date: 2017/06/12
 */
bool rtw_is_wmmps_mode(_adapter *padapter) 
{
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct qos_priv	*pqospriv = &pmlmepriv->qospriv;
		
	if ((pqospriv->uapsd_ap_supported) && ((pqospriv->uapsd_tid & BIT_MASK_TID_TC)  != 0))
		return _TRUE;

	return _FALSE;
}
#endif /* CONFIG_WMMPS_STA */

/* adjust IEs for rtw_joinbss_cmd in WMM */
int rtw_restruct_wmm_ie(_adapter *adapter, u8 *in_ie, u8 *out_ie, uint in_len, uint initial_out_len)
{
#ifdef CONFIG_WMMPS_STA
	struct mlme_priv		*pmlmepriv = &adapter->mlmepriv;
	struct qos_priv		*pqospriv = &pmlmepriv->qospriv;
#endif /* CONFIG_WMMPS_STA */
	unsigned	int ielength = 0;
	unsigned int i, j;
	u8 qos_info = 0;

	i = 12; /* after the fixed IE */
	while (i < in_len) {
		ielength = initial_out_len;

		if (in_ie[i] == 0xDD && in_ie[i + 2] == 0x00 && in_ie[i + 3] == 0x50  && in_ie[i + 4] == 0xF2 && in_ie[i + 5] == 0x02 && i + 5 < in_len) { /* WMM element ID and OUI */

			/* Append WMM IE to the last index of out_ie */
#if 0
			for (j = i; j < i + (in_ie[i + 1] + 2); j++) {
				out_ie[ielength] = in_ie[j];
				ielength++;
			}
			out_ie[initial_out_len + 8] = 0x00; /* force the QoS Info Field to be zero */
#endif

			for (j = i; j < i + 9; j++) {
				out_ie[ielength] = in_ie[j];
				ielength++;
			}
			out_ie[initial_out_len + 1] = 0x07;
			out_ie[initial_out_len + 6] = 0x00;

#ifdef CONFIG_WMMPS_STA
			switch(pqospriv->uapsd_max_sp_len) {
				case NO_LIMIT: 
					/* do nothing */
					break;
				case TWO_MSDU: 
					SET_FLAG(qos_info, BIT5);
					break;
				case FOUR_MSDU: 
					SET_FLAG(qos_info, BIT6);
					break;	
				case SIX_MSDU: 
					SET_FLAG(qos_info, BIT5);
					SET_FLAG(qos_info, BIT6);
					break;
				default:
					/* do nothing */
					break;
			};

			/* check TID7 and TID6 for AC_VO to set corresponding Qos_info bit in WMM IE  */
			if((TEST_FLAG(pqospriv->uapsd_tid, WMM_TID7)) && (TEST_FLAG(pqospriv->uapsd_tid, WMM_TID6)))
				SET_FLAG(qos_info, WMM_IE_UAPSD_VO);
			/* check TID5 and TID4 for AC_VI to set corresponding Qos_info bit in WMM IE  */
			if((TEST_FLAG(pqospriv->uapsd_tid, WMM_TID5)) && (TEST_FLAG(pqospriv->uapsd_tid, WMM_TID4)))
				SET_FLAG(qos_info, WMM_IE_UAPSD_VI);
			/* check TID2 and TID1 for AC_BK to set corresponding Qos_info bit in WMM IE  */
			if((TEST_FLAG(pqospriv->uapsd_tid, WMM_TID2)) && (TEST_FLAG(pqospriv->uapsd_tid, WMM_TID1)))
				SET_FLAG(qos_info, WMM_IE_UAPSD_BK);
			/* check TID3 and TID0 for AC_BE to set corresponding Qos_info bit in WMM IE  */
			if((TEST_FLAG(pqospriv->uapsd_tid, WMM_TID3)) && (TEST_FLAG(pqospriv->uapsd_tid, WMM_TID0)))
				SET_FLAG(qos_info, WMM_IE_UAPSD_BE);
#endif /* CONFIG_WMMPS_STA */
			
			out_ie[initial_out_len + 8] = qos_info;

			break;
		}

		i += (in_ie[i + 1] + 2); /* to the next IE element */
	}

	return ielength;

}


/*
 * Ported from 8185: IsInPreAuthKeyList(). (Renamed from SecIsInPreAuthKeyList(), 2006-10-13.)
 * Added by Annie, 2006-05-07.
 *
 * Search by BSSID,
 * Return Value:
 *		-1		:if there is no pre-auth key in the  table
 *		>=0		:if there is pre-auth key, and   return the entry id
 *
 *   */

static int SecIsInPMKIDList(_adapter *Adapter, u8 *bssid)
{
	struct security_priv *psecuritypriv = &Adapter->securitypriv;
	int i = 0;

	do {
		if ((psecuritypriv->PMKIDList[i].bUsed) &&
		    (_rtw_memcmp(psecuritypriv->PMKIDList[i].Bssid, bssid, ETH_ALEN) == _TRUE))
			break;
		else {
			i++;
			/* continue; */
		}

	} while (i < NUM_PMKID_CACHE);

	if (i == NUM_PMKID_CACHE) {
		i = -1;/* Could not find. */
	} else {
		/* There is one Pre-Authentication Key for the specific BSSID. */
	}

	return i;

}

int rtw_cached_pmkid(_adapter *Adapter, u8 *bssid)
{
	return SecIsInPMKIDList(Adapter, bssid);
}

int rtw_rsn_sync_pmkid(_adapter *adapter, u8 *ie, uint ie_len, int i_ent)
{
	struct security_priv *sec = &adapter->securitypriv;
	struct rsne_info info;
	u8 gm_cs[4];
	int i;

	rtw_rsne_info_parse(ie, ie_len, &info);

	if (info.err) {
		RTW_WARN(FUNC_ADPT_FMT" rtw_rsne_info_parse error\n"
			, FUNC_ADPT_ARG(adapter));
		return 0;
	}

	if (i_ent < 0 && info.pmkid_cnt == 0)
		goto exit;

	if (i_ent >= 0 && info.pmkid_cnt == 1 && _rtw_memcmp(info.pmkid_list, sec->PMKIDList[i_ent].PMKID, 16)) {
		RTW_INFO(FUNC_ADPT_FMT" has carried the same PMKID:"KEY_FMT"\n"
			, FUNC_ADPT_ARG(adapter), KEY_ARG(&sec->PMKIDList[i_ent].PMKID));
		goto exit;
	}

	/* bakcup group mgmt cs */
	if (info.gmcs)
		_rtw_memcpy(gm_cs, info.gmcs, 4);

	if (info.pmkid_cnt) {
		RTW_INFO(FUNC_ADPT_FMT" remove original PMKID, count:%u\n"
			 , FUNC_ADPT_ARG(adapter), info.pmkid_cnt);
		for (i = 0; i < info.pmkid_cnt; i++)
			RTW_INFO("    "KEY_FMT"\n", KEY_ARG(info.pmkid_list + i * 16));
	}

	if (i_ent >= 0) {
		RTW_INFO(FUNC_ADPT_FMT" append PMKID:"KEY_FMT"\n"
			, FUNC_ADPT_ARG(adapter), KEY_ARG(sec->PMKIDList[i_ent].PMKID));

		info.pmkid_cnt = 1; /* update new pmkid_cnt */
		_rtw_memcpy(info.pmkid_list, sec->PMKIDList[i_ent].PMKID, 16);
	} else
		info.pmkid_cnt = 0; /* update new pmkid_cnt */

	RTW_PUT_LE16(info.pmkid_list - 2, info.pmkid_cnt);
	if (info.gmcs)
		_rtw_memcpy(info.pmkid_list + 16 * info.pmkid_cnt, gm_cs, 4);

	ie_len = 1 + 1 + 2 + 4
		+ 2 + 4 * info.pcs_cnt
		+ 2 + 4 * info.akm_cnt
		+ 2
		+ 2 + 16 * info.pmkid_cnt
		+ (info.gmcs ? 4 : 0)
		;
	
	ie[1] = (u8)(ie_len - 2);

exit:
	return ie_len;
}

sint rtw_restruct_sec_ie(_adapter *adapter, u8 *out_ie)
{
	u8 authmode = 0x0;
	uint	ielength = 0;
	int iEntry;

	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct security_priv *psecuritypriv = &adapter->securitypriv;
	uint	ndisauthmode = psecuritypriv->ndisauthtype;

	if ((ndisauthmode == Ndis802_11AuthModeWPA) || (ndisauthmode == Ndis802_11AuthModeWPAPSK))
		authmode = _WPA_IE_ID_;
	if ((ndisauthmode == Ndis802_11AuthModeWPA2) || (ndisauthmode == Ndis802_11AuthModeWPA2PSK))
		authmode = _WPA2_IE_ID_;

	if (check_fwstate(pmlmepriv, WIFI_UNDER_WPS)) {
		_rtw_memcpy(out_ie, psecuritypriv->wps_ie, psecuritypriv->wps_ie_len);
		ielength = psecuritypriv->wps_ie_len;

	} else if ((authmode == _WPA_IE_ID_) || (authmode == _WPA2_IE_ID_)) {
		/* copy RSN or SSN		 */
		_rtw_memcpy(out_ie, psecuritypriv->supplicant_ie, psecuritypriv->supplicant_ie[1] + 2);
		/* debug for CONFIG_IEEE80211W
		{
			int jj;
			printk("supplicant_ie_length=%d &&&&&&&&&&&&&&&&&&&\n", psecuritypriv->supplicant_ie[1]+2);
			for(jj=0; jj < psecuritypriv->supplicant_ie[1]+2; jj++)
				printk(" %02x ", psecuritypriv->supplicant_ie[jj]);
			printk("\n");
		}*/
		ielength = psecuritypriv->supplicant_ie[1] + 2;
		rtw_report_sec_ie(adapter, authmode, psecuritypriv->supplicant_ie);
	}

	if (authmode == WLAN_EID_RSN) {
		iEntry = SecIsInPMKIDList(adapter, pmlmepriv->assoc_bssid);
		ielength = rtw_rsn_sync_pmkid(adapter, out_ie, ielength, iEntry);
	}

	return ielength;
}

void rtw_init_registrypriv_dev_network(_adapter *adapter)
{
	struct registry_priv *pregistrypriv = &adapter->registrypriv;
	WLAN_BSSID_EX    *pdev_network = &pregistrypriv->dev_network;
	u8 *myhwaddr = adapter_mac_addr(adapter);


	_rtw_memcpy(pdev_network->MacAddress, myhwaddr, ETH_ALEN);

	_rtw_memcpy(&pdev_network->Ssid, &pregistrypriv->ssid, sizeof(NDIS_802_11_SSID));

	pdev_network->Configuration.Length = sizeof(NDIS_802_11_CONFIGURATION);
	pdev_network->Configuration.BeaconPeriod = 100;
}

void rtw_update_registrypriv_dev_network(_adapter *adapter)
{
	int sz = 0;
	struct registry_priv *pregistrypriv = &adapter->registrypriv;
	WLAN_BSSID_EX    *pdev_network = &pregistrypriv->dev_network;
	struct	security_priv	*psecuritypriv = &adapter->securitypriv;
	struct	wlan_network	*cur_network = &adapter->mlmepriv.cur_network;
	/* struct	xmit_priv	*pxmitpriv = &adapter->xmitpriv; */
	struct mlme_ext_priv	*pmlmeext = &adapter->mlmeextpriv;


#if 0
	pxmitpriv->vcs_setting = pregistrypriv->vrtl_carrier_sense;
	pxmitpriv->vcs = pregistrypriv->vcs_type;
	pxmitpriv->vcs_type = pregistrypriv->vcs_type;
	/* pxmitpriv->rts_thresh = pregistrypriv->rts_thresh; */
	pxmitpriv->frag_len = pregistrypriv->frag_thresh;

	adapter->qospriv.qos_option = pregistrypriv->wmm_enable;
#endif

	pdev_network->Privacy = (psecuritypriv->dot11PrivacyAlgrthm > 0 ? 1 : 0) ; /* adhoc no 802.1x */

	pdev_network->Rssi = 0;

	pdev_network->Configuration.DSConfig = (pregistrypriv->channel);

	if (cur_network->network.InfrastructureMode == Ndis802_11IBSS) {
		pdev_network->Configuration.ATIMWindow = (0);

		if (pmlmeext->cur_channel != 0)
			pdev_network->Configuration.DSConfig = pmlmeext->cur_channel;
		else
			pdev_network->Configuration.DSConfig = 1;
	}

	pdev_network->InfrastructureMode = (cur_network->network.InfrastructureMode);

	/* 1. Supported rates */
	/* 2. IE */

	/* rtw_set_supported_rate(pdev_network->SupportedRates, pregistrypriv->wireless_mode) ; */ /* will be called in rtw_generate_ie */
	sz = rtw_generate_ie(pregistrypriv);

	pdev_network->IELength = sz;

	pdev_network->Length = get_WLAN_BSSID_EX_sz((WLAN_BSSID_EX *)pdev_network);

	/* notes: translate IELength & Length after assign the Length to cmdsz in createbss_cmd(); */
	/* pdev_network->IELength = cpu_to_le32(sz); */


}

void rtw_get_encrypt_decrypt_from_registrypriv(_adapter *adapter)
{



}

/* the fucntion is at passive_level */
void rtw_joinbss_reset(_adapter *padapter)
{
	u8	threshold;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	/* todo: if you want to do something io/reg/hw setting before join_bss, please add code here */

#ifdef CONFIG_80211N_HT
	struct ht_priv		*phtpriv = &pmlmepriv->htpriv;

	pmlmepriv->num_FortyMHzIntolerant = 0;

	pmlmepriv->num_sta_no_ht = 0;

	phtpriv->ampdu_enable = _FALSE;/* reset to disabled */

#if defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI)
	/* TH=1 => means that invalidate usb rx aggregation */
	/* TH=0 => means that validate usb rx aggregation, use init value. */
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
#endif/* #if defined( CONFIG_USB_HCI) || defined (CONFIG_SDIO_HCI) */

#endif/* #ifdef CONFIG_80211N_HT */

}


#ifdef CONFIG_80211N_HT
void	rtw_ht_use_default_setting(_adapter *padapter)
{
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct ht_priv		*phtpriv = &pmlmepriv->htpriv;
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;
	BOOLEAN		bHwLDPCSupport = _FALSE, bHwSTBCSupport = _FALSE;
#ifdef CONFIG_BEAMFORMING
	BOOLEAN		bHwSupportBeamformer = _FALSE, bHwSupportBeamformee = _FALSE;
#endif /* CONFIG_BEAMFORMING */

	if (pregistrypriv->wifi_spec)
		phtpriv->bss_coexist = 1;
	else
		phtpriv->bss_coexist = 0;

	phtpriv->sgi_40m = TEST_FLAG(pregistrypriv->short_gi, BIT1) ? _TRUE : _FALSE;
	phtpriv->sgi_20m = TEST_FLAG(pregistrypriv->short_gi, BIT0) ? _TRUE : _FALSE;

	/* LDPC support */
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
		RTW_INFO("[HT] HAL Support LDPC = 0x%02X\n", phtpriv->ldpc_cap);

	/* STBC */
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
		RTW_INFO("[HT] HAL Support STBC = 0x%02X\n", phtpriv->stbc_cap);

	/* Beamforming setting */
	CLEAR_FLAGS(phtpriv->beamform_cap);
#ifdef CONFIG_BEAMFORMING
#ifdef RTW_BEAMFORMING_VERSION_2
	/* only enable beamforming in STA client mode */
	if (MLME_IS_STA(padapter) && !MLME_IS_GC(padapter)
				  && !MLME_IS_ADHOC(padapter)
				  && !MLME_IS_MESH(padapter))
#endif
	{
		rtw_hal_get_def_var(padapter, HAL_DEF_EXPLICIT_BEAMFORMER, (u8 *)&bHwSupportBeamformer);
		rtw_hal_get_def_var(padapter, HAL_DEF_EXPLICIT_BEAMFORMEE, (u8 *)&bHwSupportBeamformee);
		if (TEST_FLAG(pregistrypriv->beamform_cap, BIT4) && bHwSupportBeamformer) {
			SET_FLAG(phtpriv->beamform_cap, BEAMFORMING_HT_BEAMFORMER_ENABLE);
			RTW_INFO("[HT] HAL Support Beamformer\n");
		}
		if (TEST_FLAG(pregistrypriv->beamform_cap, BIT5) && bHwSupportBeamformee) {
			SET_FLAG(phtpriv->beamform_cap, BEAMFORMING_HT_BEAMFORMEE_ENABLE);
			RTW_INFO("[HT] HAL Support Beamformee\n");
		}
	}
#endif /* CONFIG_BEAMFORMING */
}
void rtw_build_wmm_ie_ht(_adapter *padapter, u8 *out_ie, uint *pout_len)
{
	unsigned char WMM_IE[] = {0x00, 0x50, 0xf2, 0x02, 0x00, 0x01, 0x00};
	int out_len;
	u8 *pframe;

	if (padapter->mlmepriv.qospriv.qos_option == 0) {
		out_len = *pout_len;
		pframe = rtw_set_ie(out_ie + out_len, _VENDOR_SPECIFIC_IE_,
				    _WMM_IE_Length_, WMM_IE, pout_len);

		padapter->mlmepriv.qospriv.qos_option = 1;
	}
}
#if defined(CONFIG_80211N_HT)
/* the fucntion is >= passive_level */
unsigned int rtw_restructure_ht_ie(_adapter *padapter, u8 *in_ie, u8 *out_ie, uint in_len, uint *pout_len, u8 channel)
{
	u32 ielen, out_len;
	u32 rx_packet_offset, max_recvbuf_sz;
	HT_CAP_AMPDU_FACTOR max_rx_ampdu_factor;
	HT_CAP_AMPDU_DENSITY best_ampdu_density;
	unsigned char *p, *pframe;
	struct rtw_ieee80211_ht_cap ht_capie;
	u8	cbw40_enable = 0, rf_num = 0, rx_stbc_nss = 0, rx_nss = 0;
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct ht_priv		*phtpriv = &pmlmepriv->htpriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
#ifdef CONFIG_80211AC_VHT
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct vht_priv	*pvhtpriv = &pmlmepriv->vhtpriv;
#endif /* CONFIG_80211AC_VHT */

	phtpriv->ht_option = _FALSE;

	out_len = *pout_len;

	_rtw_memset(&ht_capie, 0, sizeof(struct rtw_ieee80211_ht_cap));

	ht_capie.cap_info = IEEE80211_HT_CAP_DSSSCCK40;

	if (phtpriv->sgi_20m)
		ht_capie.cap_info |= IEEE80211_HT_CAP_SGI_20;

	/* check if 40MHz is allowed according to hal cap and registry */
	if (hal_chk_bw_cap(padapter, BW_CAP_40M)) {
		if (channel > 14) {
			if (REGSTY_IS_BW_5G_SUPPORT(pregistrypriv, CHANNEL_WIDTH_40))
				cbw40_enable = 1;
		} else {
			if (REGSTY_IS_BW_2G_SUPPORT(pregistrypriv, CHANNEL_WIDTH_40))
				cbw40_enable = 1;
		}
	}

	if (cbw40_enable) {
		struct rf_ctl_t *rfctl = adapter_to_rfctl(padapter);
		RT_CHANNEL_INFO *chset = rfctl->channel_set;
		u8 oper_bw = CHANNEL_WIDTH_20, oper_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;

		if (in_ie == NULL) {
			/* TDLS: TODO 20/40 issue */
			if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
				oper_bw = padapter->mlmeextpriv.cur_bwmode;
				if (oper_bw > CHANNEL_WIDTH_40)
					oper_bw = CHANNEL_WIDTH_40;
			} else
				/* TDLS: TODO 40? */
				oper_bw = CHANNEL_WIDTH_40;
		} else {
			p = rtw_get_ie(in_ie, WLAN_EID_HT_OPERATION, &ielen, in_len);
			if (p && ielen == HT_OP_IE_LEN) {
				if (GET_HT_OP_ELE_STA_CHL_WIDTH(p + 2)) {
					switch (GET_HT_OP_ELE_2ND_CHL_OFFSET(p + 2)) {
					case SCA:
						oper_bw = CHANNEL_WIDTH_40;
						oper_offset = HAL_PRIME_CHNL_OFFSET_LOWER;
						break;
					case SCB:
						oper_bw = CHANNEL_WIDTH_40;
						oper_offset = HAL_PRIME_CHNL_OFFSET_UPPER;
						break;
					}
				}
			}
			// IOT issue : AP TP-Link WDR6500
			if(oper_bw == CHANNEL_WIDTH_40){ 
				p = rtw_get_ie(in_ie, WLAN_EID_HT_CAP, &ielen, in_len);
				if (p && ielen == HT_CAP_IE_LEN) {
					oper_bw = GET_HT_CAP_ELE_CHL_WIDTH(p + 2)  ? CHANNEL_WIDTH_40 : CHANNEL_WIDTH_20;
					if(oper_bw == CHANNEL_WIDTH_20)
						oper_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
				}
			}
		}

		/* adjust bw to fit in channel plan setting */
		if (oper_bw == CHANNEL_WIDTH_40
			&& oper_offset != HAL_PRIME_CHNL_OFFSET_DONT_CARE /* check this because TDLS has no info to set offset */
			&& (!rtw_chset_is_chbw_valid(chset, channel, oper_bw, oper_offset)
				|| (IS_DFS_SLAVE_WITH_RD(rfctl)
					&& !rtw_odm_dfs_domain_unknown(rfctl_to_dvobj(rfctl))
					&& rtw_chset_is_chbw_non_ocp(chset, channel, oper_bw, oper_offset))
				)
		) {
			oper_bw = CHANNEL_WIDTH_20;
			oper_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
			rtw_warn_on(!rtw_chset_is_chbw_valid(chset, channel, oper_bw, oper_offset));
			if (IS_DFS_SLAVE_WITH_RD(rfctl) && !rtw_odm_dfs_domain_unknown(rfctl_to_dvobj(rfctl)))
				rtw_warn_on(rtw_chset_is_chbw_non_ocp(chset, channel, oper_bw, oper_offset));
		}

		if (oper_bw == CHANNEL_WIDTH_40) {
			ht_capie.cap_info |= IEEE80211_HT_CAP_SUP_WIDTH;
			if (phtpriv->sgi_40m)
				ht_capie.cap_info |= IEEE80211_HT_CAP_SGI_40;
		}

		cbw40_enable = oper_bw == CHANNEL_WIDTH_40 ? 1 : 0;
	}

	/* todo: disable SM power save mode */
	ht_capie.cap_info |= IEEE80211_HT_CAP_SM_PS;

	/* RX LDPC */
	if (TEST_FLAG(phtpriv->ldpc_cap, LDPC_HT_ENABLE_RX)) {
		ht_capie.cap_info |= IEEE80211_HT_CAP_LDPC_CODING;
		RTW_INFO("[HT] Declare supporting RX LDPC\n");
	}

	/* TX STBC */
	if (TEST_FLAG(phtpriv->stbc_cap, STBC_HT_ENABLE_TX)) {
		ht_capie.cap_info |= IEEE80211_HT_CAP_TX_STBC;
		RTW_INFO("[HT] Declare supporting TX STBC\n");
	}

	/* RX STBC */
	if (TEST_FLAG(phtpriv->stbc_cap, STBC_HT_ENABLE_RX)) {
		if ((pregistrypriv->rx_stbc == 0x3) ||							/* enable for 2.4/5 GHz */
		    ((channel <= 14) && (pregistrypriv->rx_stbc == 0x1)) ||		/* enable for 2.4GHz */
		    ((channel > 14) && (pregistrypriv->rx_stbc == 0x2)) ||		/* enable for 5GHz */
		    (pregistrypriv->wifi_spec == 1)) {
			/* HAL_DEF_RX_STBC means STBC RX spatial stream, todo: VHT 4 streams */
			rtw_hal_get_def_var(padapter, HAL_DEF_RX_STBC, (u8 *)(&rx_stbc_nss));
			SET_HT_CAP_ELE_RX_STBC(&ht_capie, rx_stbc_nss);
			RTW_INFO("[HT] Declare supporting RX STBC = %d\n", rx_stbc_nss);
		}
	}

	/* fill default supported_mcs_set */
	_rtw_memcpy(ht_capie.supp_mcs_set, pmlmeext->default_supported_mcs_set, 16);

	/* update default supported_mcs_set */
	rx_nss = GET_HAL_RX_NSS(padapter);

	switch (rx_nss) {
	case 1:
		set_mcs_rate_by_mask(ht_capie.supp_mcs_set, MCS_RATE_1R);
		break;
	case 2:
		#ifdef CONFIG_DISABLE_MCS13TO15
		if (cbw40_enable && pregistrypriv->wifi_spec != 1)
			set_mcs_rate_by_mask(ht_capie.supp_mcs_set, MCS_RATE_2R_13TO15_OFF);
		else
		#endif
			set_mcs_rate_by_mask(ht_capie.supp_mcs_set, MCS_RATE_2R);
		break;
	case 3:
		set_mcs_rate_by_mask(ht_capie.supp_mcs_set, MCS_RATE_3R);
		break;
	case 4:
		set_mcs_rate_by_mask(ht_capie.supp_mcs_set, MCS_RATE_4R);
		break;
	default:
		RTW_WARN("rf_type:%d or rx_nss:%u is not expected\n", GET_HAL_RFPATH(padapter), rx_nss);
	}

	{
		rtw_hal_get_def_var(padapter, HAL_DEF_RX_PACKET_OFFSET, &rx_packet_offset);
		rtw_hal_get_def_var(padapter, HAL_DEF_MAX_RECVBUF_SZ, &max_recvbuf_sz);
		if (max_recvbuf_sz - rx_packet_offset >= (8191 - 256)) {
			RTW_INFO("%s IEEE80211_HT_CAP_MAX_AMSDU is set\n", __FUNCTION__);
			ht_capie.cap_info = ht_capie.cap_info | IEEE80211_HT_CAP_MAX_AMSDU;
		}
	}
	/*
	AMPDU_para [1:0]:Max AMPDU Len => 0:8k , 1:16k, 2:32k, 3:64k
	AMPDU_para [4:2]:Min MPDU Start Spacing
	*/

	/*
	#if defined(CONFIG_RTL8188E) && defined(CONFIG_SDIO_HCI)
	ht_capie.ampdu_params_info = 2;
	#else
	ht_capie.ampdu_params_info = (IEEE80211_HT_CAP_AMPDU_FACTOR&0x03);
	#endif
	*/

	if (padapter->driver_rx_ampdu_factor != 0xFF)
		max_rx_ampdu_factor = (HT_CAP_AMPDU_FACTOR)padapter->driver_rx_ampdu_factor;
	else
		rtw_hal_get_def_var(padapter, HW_VAR_MAX_RX_AMPDU_FACTOR, &max_rx_ampdu_factor);

	/* rtw_hal_get_def_var(padapter, HW_VAR_MAX_RX_AMPDU_FACTOR, &max_rx_ampdu_factor); */
	ht_capie.ampdu_params_info = (max_rx_ampdu_factor & 0x03);

	if (padapter->driver_rx_ampdu_spacing != 0xFF)
		ht_capie.ampdu_params_info |= ((padapter->driver_rx_ampdu_spacing & 0x07) << 2);
	else {
		if (padapter->securitypriv.dot11PrivacyAlgrthm == _AES_) {
			/*
			*	Todo : Each chip must to ask DD , this chip best ampdu_density setting
			*	By yiwei.sun
			*/
			rtw_hal_get_def_var(padapter, HW_VAR_BEST_AMPDU_DENSITY, &best_ampdu_density);

			ht_capie.ampdu_params_info |= (IEEE80211_HT_CAP_AMPDU_DENSITY & (best_ampdu_density << 2));

		} else
			ht_capie.ampdu_params_info |= (IEEE80211_HT_CAP_AMPDU_DENSITY & 0x00);
	}
#ifdef CONFIG_BEAMFORMING
	ht_capie.tx_BF_cap_info = 0;

	/* HT Beamformer*/
	if (TEST_FLAG(phtpriv->beamform_cap, BEAMFORMING_HT_BEAMFORMER_ENABLE)) {
		/* Transmit NDP Capable */
		SET_HT_CAP_TXBF_TRANSMIT_NDP_CAP(&ht_capie, 1);
		/* Explicit Compressed Steering Capable */
		SET_HT_CAP_TXBF_EXPLICIT_COMP_STEERING_CAP(&ht_capie, 1);
		/* Compressed Steering Number Antennas */
		SET_HT_CAP_TXBF_COMP_STEERING_NUM_ANTENNAS(&ht_capie, 1);
		rtw_hal_get_def_var(padapter, HAL_DEF_BEAMFORMER_CAP, (u8 *)&rf_num);
		SET_HT_CAP_TXBF_CHNL_ESTIMATION_NUM_ANTENNAS(&ht_capie, rf_num);
	}

	/* HT Beamformee */
	if (TEST_FLAG(phtpriv->beamform_cap, BEAMFORMING_HT_BEAMFORMEE_ENABLE)) {
		/* Receive NDP Capable */
		SET_HT_CAP_TXBF_RECEIVE_NDP_CAP(&ht_capie, 1);
		/* Explicit Compressed Beamforming Feedback Capable */
		SET_HT_CAP_TXBF_EXPLICIT_COMP_FEEDBACK_CAP(&ht_capie, 2);

		rtw_hal_get_def_var(padapter, HAL_DEF_BEAMFORMEE_CAP, (u8 *)&rf_num);
#ifdef CONFIG_80211AC_VHT
		/* IOT action suggested by Yu Chen 2017/3/3 */
		if ((pmlmeinfo->assoc_AP_vendor == HT_IOT_PEER_BROADCOM) &&
			!pvhtpriv->ap_bf_cap.is_mu_bfer &&
			pvhtpriv->ap_bf_cap.su_sound_dim == 2)
			rf_num = (rf_num >= 2 ? 2 : rf_num);
#endif
		SET_HT_CAP_TXBF_COMP_STEERING_NUM_ANTENNAS(&ht_capie, rf_num);
	}
#endif/*CONFIG_BEAMFORMING*/

	pframe = rtw_set_ie(out_ie + out_len, _HT_CAPABILITY_IE_,
		sizeof(struct rtw_ieee80211_ht_cap), (unsigned char *)&ht_capie, pout_len);

	phtpriv->ht_option = _TRUE;

	if (in_ie != NULL) {
		p = rtw_get_ie(in_ie, _HT_ADD_INFO_IE_, &ielen, in_len);
		if (p && (ielen == sizeof(struct ieee80211_ht_addt_info))) {
			out_len = *pout_len;
			pframe = rtw_set_ie(out_ie + out_len, _HT_ADD_INFO_IE_, ielen, p + 2 , pout_len);
		}
	}

	return phtpriv->ht_option;

}

/* the fucntion is > passive_level (in critical_section) */
void rtw_update_ht_cap(_adapter *padapter, u8 *pie, uint ie_len, u8 channel)
{
	u8 *p, max_ampdu_sz;
	int len;
	/* struct sta_info *bmc_sta, *psta; */
	struct rtw_ieee80211_ht_cap *pht_capie;
	struct ieee80211_ht_addt_info *pht_addtinfo;
	/* struct recv_reorder_ctrl *preorder_ctrl; */
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct ht_priv		*phtpriv = &pmlmepriv->htpriv;
	/* struct recv_priv *precvpriv = &padapter->recvpriv; */
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	/* struct wlan_network *pcur_network = &(pmlmepriv->cur_network);; */
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 cbw40_enable = 0;


	if (!phtpriv->ht_option)
		return;

	if ((!pmlmeinfo->HT_info_enable) || (!pmlmeinfo->HT_caps_enable))
		return;

	RTW_INFO("+rtw_update_ht_cap()\n");

	/* maybe needs check if ap supports rx ampdu. */
	if ((phtpriv->ampdu_enable == _FALSE) && (pregistrypriv->ampdu_enable == 1)) {
		if (pregistrypriv->wifi_spec == 1) {
			/* remove this part because testbed AP should disable RX AMPDU */
			/* phtpriv->ampdu_enable = _FALSE; */
			phtpriv->ampdu_enable = _TRUE;
		} else
			phtpriv->ampdu_enable = _TRUE;
	} 


	/* check Max Rx A-MPDU Size */
	len = 0;
	p = rtw_get_ie(pie + sizeof(NDIS_802_11_FIXED_IEs), _HT_CAPABILITY_IE_, &len, ie_len - sizeof(NDIS_802_11_FIXED_IEs));
	if (p && len > 0) {
		pht_capie = (struct rtw_ieee80211_ht_cap *)(p + 2);
		max_ampdu_sz = (pht_capie->ampdu_params_info & IEEE80211_HT_CAP_AMPDU_FACTOR);
		max_ampdu_sz = 1 << (max_ampdu_sz + 3); /* max_ampdu_sz (kbytes); */

		/* RTW_INFO("rtw_update_ht_cap(): max_ampdu_sz=%d\n", max_ampdu_sz); */
		phtpriv->rx_ampdu_maxlen = max_ampdu_sz;

	}


	len = 0;
	p = rtw_get_ie(pie + sizeof(NDIS_802_11_FIXED_IEs), _HT_ADD_INFO_IE_, &len, ie_len - sizeof(NDIS_802_11_FIXED_IEs));
	if (p && len > 0) {
		pht_addtinfo = (struct ieee80211_ht_addt_info *)(p + 2);
		/* todo: */
	}

	if (hal_chk_bw_cap(padapter, BW_CAP_40M)) {
		if (channel > 14) {
			if (REGSTY_IS_BW_5G_SUPPORT(pregistrypriv, CHANNEL_WIDTH_40))
				cbw40_enable = 1;
		} else {
			if (REGSTY_IS_BW_2G_SUPPORT(pregistrypriv, CHANNEL_WIDTH_40))
				cbw40_enable = 1;
		}
	}

	/* update cur_bwmode & cur_ch_offset */
	if ((cbw40_enable) &&
	    (pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info & BIT(1)) &&
	    (pmlmeinfo->HT_info.infos[0] & BIT(2))) {
		int i;
		u8 tx_nss = 0;

		tx_nss = GET_HAL_TX_NSS(padapter);

		/* update the MCS set */
		for (i = 0; i < 16; i++)
			pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate[i] &= pmlmeext->default_supported_mcs_set[i];

		/* update the MCS rates */
		switch (tx_nss) {
		case 1:
			set_mcs_rate_by_mask(pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate, MCS_RATE_1R);
			break;
		case 2:
			#ifdef CONFIG_DISABLE_MCS13TO15
			if (pmlmeext->cur_bwmode == CHANNEL_WIDTH_40 && pregistrypriv->wifi_spec != 1)
				set_mcs_rate_by_mask(pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate, MCS_RATE_2R_13TO15_OFF);
			else
			#endif
				set_mcs_rate_by_mask(pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate, MCS_RATE_2R);
			break;
		case 3:
			set_mcs_rate_by_mask(pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate, MCS_RATE_3R);
			break;
		case 4:
			set_mcs_rate_by_mask(pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate, MCS_RATE_4R);
			break;
		default:
			RTW_WARN("tx_nss:%u is not expected\n", tx_nss);
		}

		/* switch to the 40M Hz mode accoring to the AP */
		/* pmlmeext->cur_bwmode = CHANNEL_WIDTH_40; */
		switch ((pmlmeinfo->HT_info.infos[0] & 0x3)) {
		case EXTCHNL_OFFSET_UPPER:
			pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_LOWER;
			break;

		case EXTCHNL_OFFSET_LOWER:
			pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_UPPER;
			break;

		default:
			pmlmeext->cur_bwmode = CHANNEL_WIDTH_20;
			pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
			RTW_INFO("%s : ch offset is not assigned for HT40 mod , update cur_bwmode=%u, cur_ch_offset=%u\n", 
					__func__, pmlmeext->cur_bwmode, pmlmeext->cur_ch_offset);
			break;
		}
	}

	/*  */
	/* Config SM Power Save setting */
	/*  */
	pmlmeinfo->SM_PS = (pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info & 0x0C) >> 2;
	if (pmlmeinfo->SM_PS == WLAN_HT_CAP_SM_PS_STATIC) {
#if 0
		u8 i;
		/* update the MCS rates */
		for (i = 0; i < 16; i++)
			pmlmeinfo->HT_caps.HT_cap_element.MCS_rate[i] &= MCS_rate_1R[i];
#endif
		RTW_INFO("%s(): WLAN_HT_CAP_SM_PS_STATIC\n", __FUNCTION__);
	}

	/*  */
	/* Config current HT Protection mode. */
	/*  */
	pmlmeinfo->HT_protection = pmlmeinfo->HT_info.infos[1] & 0x3;
}
#endif

#ifdef CONFIG_TDLS
void rtw_issue_addbareq_cmd_tdls(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	struct sta_info *ptdls_sta = NULL;
	u8 issued;
	int priority;
	struct ht_priv	*phtpriv;

	priority = pattrib->priority;

	if (pattrib->direct_link == _TRUE) {
		ptdls_sta = rtw_get_stainfo(&padapter->stapriv, pattrib->dst);
		if ((ptdls_sta != NULL) && (ptdls_sta->tdls_sta_state & TDLS_LINKED_STATE)) {
			phtpriv = &ptdls_sta->htpriv;

			if ((phtpriv->ht_option == _TRUE) && (phtpriv->ampdu_enable == _TRUE)) {
				issued = (phtpriv->agg_enable_bitmap >> priority) & 0x1;
				issued |= (phtpriv->candidate_tid_bitmap >> priority) & 0x1;

				if (0 == issued) {
					RTW_INFO("[%s], p=%d\n", __FUNCTION__, priority);
					ptdls_sta->htpriv.candidate_tid_bitmap |= BIT((u8)priority);
					rtw_addbareq_cmd(padapter, (u8)priority, pattrib->dst);
				}
			}
		}
	}
}
#endif /* CONFIG_TDLS */

#ifdef CONFIG_80211N_HT
static u8 rtw_issue_addbareq_check(_adapter *padapter, struct xmit_frame *pxmitframe, u8 issue_when_busy)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct registry_priv *pregistry = &padapter->registrypriv;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	s32 bmcst = IS_MCAST(pattrib->ra);

	if (bmcst)
		return _FALSE;

	if (pregistry->tx_quick_addba_req == 0) {
		if ((issue_when_busy == _TRUE) && (pmlmepriv->LinkDetectInfo.bBusyTraffic == _FALSE))
			return _FALSE;

		if (pmlmepriv->LinkDetectInfo.NumTxOkInPeriod < 100)
			return _FALSE;
	}

	return _TRUE;
}

void rtw_issue_addbareq_cmd(_adapter *padapter, struct xmit_frame *pxmitframe, u8 issue_when_busy)
{
	u8 issued;
	int priority;
	struct sta_info *psta = NULL;
	struct ht_priv	*phtpriv;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;

	if (rtw_issue_addbareq_check(padapter,pxmitframe, issue_when_busy) == _FALSE)
		return;

	priority = pattrib->priority;

#ifdef CONFIG_TDLS
	rtw_issue_addbareq_cmd_tdls(padapter, pxmitframe);
#endif /* CONFIG_TDLS */

	psta = rtw_get_stainfo(&padapter->stapriv, pattrib->ra);
	if (pattrib->psta != psta) {
		RTW_INFO("%s, pattrib->psta(%p) != psta(%p)\n", __func__, pattrib->psta, psta);
		return;
	}

	if (psta == NULL) {
		RTW_INFO("%s, psta==NUL\n", __func__);
		return;
	}

	if (!(psta->state & WIFI_ASOC_STATE)) {
		RTW_INFO("%s, psta->state(0x%x) != WIFI_ASOC_STATE\n", __func__, psta->state);
		return;
	}


	phtpriv = &psta->htpriv;

	if ((phtpriv->ht_option == _TRUE) && (phtpriv->ampdu_enable == _TRUE)) {
		issued = (phtpriv->agg_enable_bitmap >> priority) & 0x1;
		issued |= (phtpriv->candidate_tid_bitmap >> priority) & 0x1;

		if (0 == issued) {
			RTW_INFO("rtw_issue_addbareq_cmd, p=%d\n", priority);
			psta->htpriv.candidate_tid_bitmap |= BIT((u8)priority);
			rtw_addbareq_cmd(padapter, (u8) priority, pattrib->ra);
		}
	}

}
#endif /* CONFIG_80211N_HT */
void rtw_append_exented_cap(_adapter *padapter, u8 *out_ie, uint *pout_len)
{
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct ht_priv		*phtpriv = &pmlmepriv->htpriv;
#ifdef CONFIG_80211AC_VHT
	struct vht_priv	*pvhtpriv = &pmlmepriv->vhtpriv;
#endif /* CONFIG_80211AC_VHT */
	u8	cap_content[8] = { 0 };
	u8	*pframe;
	u8   null_content[8] = {0};

	if (phtpriv->bss_coexist)
		SET_EXT_CAPABILITY_ELE_BSS_COEXIST(cap_content, 1);

#ifdef CONFIG_80211AC_VHT
	if (pvhtpriv->vht_option)
		SET_EXT_CAPABILITY_ELE_OP_MODE_NOTIF(cap_content, 1);
#endif /* CONFIG_80211AC_VHT */
#ifdef CONFIG_RTW_WNM
	rtw_wnm_set_ext_cap_btm(cap_content, 1);
#endif
	/*
		From 802.11 specification,if a STA does not support any of capabilities defined
		in the Extended Capabilities element, then the STA is not required to
		transmit the Extended Capabilities element.
	*/
	if (_FALSE == _rtw_memcmp(cap_content, null_content, 8))
		pframe = rtw_set_ie(out_ie + *pout_len, EID_EXTCapability, 8, cap_content , pout_len);
}
#endif

#ifdef CONFIG_LAYER2_ROAMING
inline void rtw_set_to_roam(_adapter *adapter, u8 to_roam)
{
	if (to_roam == 0)
		adapter->mlmepriv.to_join = _FALSE;
	adapter->mlmepriv.to_roam = to_roam;
}

inline u8 rtw_dec_to_roam(_adapter *adapter)
{
	adapter->mlmepriv.to_roam--;
	return adapter->mlmepriv.to_roam;
}

inline u8 rtw_to_roam(_adapter *adapter)
{
	return adapter->mlmepriv.to_roam;
}

void rtw_roaming(_adapter *padapter, struct wlan_network *tgt_network)
{
	_irqL irqL;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;

	_enter_critical_bh(&pmlmepriv->lock, &irqL);
	_rtw_roaming(padapter, tgt_network);
	_exit_critical_bh(&pmlmepriv->lock, &irqL);
}
void _rtw_roaming(_adapter *padapter, struct wlan_network *tgt_network)
{
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct wlan_network *cur_network = &pmlmepriv->cur_network;
	int do_join_r;

	if (0 < rtw_to_roam(padapter)) {
		RTW_INFO("roaming from %s("MAC_FMT"), length:%d\n",
			cur_network->network.Ssid.Ssid, MAC_ARG(cur_network->network.MacAddress),
			 cur_network->network.Ssid.SsidLength);
		_rtw_memcpy(&pmlmepriv->assoc_ssid, &cur_network->network.Ssid, sizeof(NDIS_802_11_SSID));
		pmlmepriv->assoc_ch = 0;
		pmlmepriv->assoc_by_bssid = _FALSE;

#ifdef CONFIG_WAPI_SUPPORT
		rtw_wapi_return_all_sta_info(padapter);
#endif

		while (1) {
			do_join_r = rtw_do_join(padapter);
			if (_SUCCESS == do_join_r)
				break;
			else {
				RTW_INFO("roaming do_join return %d\n", do_join_r);
				rtw_dec_to_roam(padapter);

				if (rtw_to_roam(padapter) > 0)
					continue;
				else {
					RTW_INFO("%s(%d) -to roaming fail, indicate_disconnect\n", __FUNCTION__, __LINE__);
#ifdef CONFIG_RTW_80211R
					rtw_ft_clr_flags(padapter, RTW_FT_PEER_EN|RTW_FT_PEER_OTD_EN);
					rtw_ft_reset_status(padapter);
#endif
					rtw_indicate_disconnect(padapter, 0, _FALSE);
					break;
				}
			}
		}
	}

}
#endif /* CONFIG_LAYER2_ROAMING */

bool rtw_adjust_chbw(_adapter *adapter, u8 req_ch, u8 *req_bw, u8 *req_offset)
{
	struct registry_priv *regsty = adapter_to_regsty(adapter);
	u8 allowed_bw;

	if (req_ch < 14)
		allowed_bw = REGSTY_BW_2G(regsty);
	else if (req_ch == 14)
		allowed_bw = CHANNEL_WIDTH_20;
	else
		allowed_bw = REGSTY_BW_5G(regsty);

	allowed_bw = hal_largest_bw(adapter, allowed_bw);

	if (allowed_bw == CHANNEL_WIDTH_80 && *req_bw > CHANNEL_WIDTH_80)
		*req_bw = CHANNEL_WIDTH_80;
	else if (allowed_bw == CHANNEL_WIDTH_40 && *req_bw > CHANNEL_WIDTH_40)
		*req_bw = CHANNEL_WIDTH_40;
	else if (allowed_bw == CHANNEL_WIDTH_20 && *req_bw > CHANNEL_WIDTH_20) {
		*req_bw = CHANNEL_WIDTH_20;
		*req_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	} else
		return _FALSE;

	return _TRUE;
}

sint rtw_linked_check(_adapter *padapter)
{
	if (MLME_IS_AP(padapter) || MLME_IS_MESH(padapter)
		|| MLME_IS_ADHOC(padapter) || MLME_IS_ADHOC_MASTER(padapter)
	) {
		if (padapter->stapriv.asoc_sta_count > 2)
			return _TRUE;
	} else {
		/* Station mode */
		if (check_fwstate(&padapter->mlmepriv, WIFI_ASOC_STATE) == _TRUE)
			return _TRUE;
	}
	return _FALSE;
}
/*#define DBG_ADAPTER_STATE_CHK*/
u8 rtw_is_adapter_up(_adapter *padapter)
{
	if (padapter == NULL)
		return _FALSE;

	if (RTW_CANNOT_RUN(padapter)) {
		#ifdef DBG_ADAPTER_STATE_CHK
		RTW_INFO(FUNC_ADPT_FMT " FALSE -bDriverStopped(%s) bSurpriseRemoved(%s)\n"
			, FUNC_ADPT_ARG(padapter)
			, rtw_is_drv_stopped(padapter) ? "True" : "False"
			, rtw_is_surprise_removed(padapter) ? "True" : "False");
		#endif
		return _FALSE;
	}

	if (!rtw_is_hw_init_completed(padapter)) {
		#ifdef DBG_ADAPTER_STATE_CHK
		RTW_INFO(FUNC_ADPT_FMT " FALSE -(hw_init_completed == _FALSE)\n", FUNC_ADPT_ARG(padapter));
		#endif
		return _FALSE;
	}

	if (padapter->bup == _FALSE) {
		#ifdef DBG_ADAPTER_STATE_CHK
		RTW_INFO(FUNC_ADPT_FMT " FALSE -(bup == _FALSE)\n", FUNC_ADPT_ARG(padapter));
		#endif
		return _FALSE;
	}

	return _TRUE;
}

bool is_miracast_enabled(_adapter *adapter)
{
	bool enabled = 0;
#ifdef CONFIG_WFD
	struct wifi_display_info *wfdinfo = &adapter->wfd_info;

	enabled = (wfdinfo->stack_wfd_mode & (MIRACAST_SOURCE | MIRACAST_SINK))
		  || (wfdinfo->op_wfd_mode & (MIRACAST_SOURCE | MIRACAST_SINK));
#endif

	return enabled;
}

bool rtw_chk_miracast_mode(_adapter *adapter, u8 mode)
{
	bool ret = 0;
#ifdef CONFIG_WFD
	struct wifi_display_info *wfdinfo = &adapter->wfd_info;

	ret = (wfdinfo->stack_wfd_mode & mode) || (wfdinfo->op_wfd_mode & mode);
#endif

	return ret;
}

const char *get_miracast_mode_str(int mode)
{
	if (mode == MIRACAST_SOURCE)
		return "SOURCE";
	else if (mode == MIRACAST_SINK)
		return "SINK";
	else if (mode == (MIRACAST_SOURCE | MIRACAST_SINK))
		return "SOURCE&SINK";
	else if (mode == MIRACAST_DISABLED)
		return "DISABLED";
	else
		return "INVALID";
}

#ifdef CONFIG_WFD
static bool wfd_st_match_rule(_adapter *adapter, u8 *local_naddr, u8 *local_port, u8 *remote_naddr, u8 *remote_port)
{
	struct wifi_display_info *wfdinfo = &adapter->wfd_info;

	if (ntohs(*((u16 *)local_port)) == wfdinfo->rtsp_ctrlport
	    || ntohs(*((u16 *)local_port)) == wfdinfo->tdls_rtsp_ctrlport
	    || ntohs(*((u16 *)remote_port)) == wfdinfo->peer_rtsp_ctrlport)
		return _TRUE;
	return _FALSE;
}

static struct st_register wfd_st_reg = {
	.s_proto = 0x06,
	.rule = wfd_st_match_rule,
};
#endif /* CONFIG_WFD */

inline void rtw_wfd_st_switch(struct sta_info *sta, bool on)
{
#ifdef CONFIG_WFD
	if (on)
		rtw_st_ctl_register(&sta->st_ctl, SESSION_TRACKER_REG_ID_WFD, &wfd_st_reg);
	else
		rtw_st_ctl_unregister(&sta->st_ctl, SESSION_TRACKER_REG_ID_WFD);
#endif
}

void dump_arp_pkt(void *sel, u8 *da, u8 *sa, u8 *arp, bool tx)
{
	RTW_PRINT_SEL(sel, "%s ARP da="MAC_FMT", sa="MAC_FMT"\n"
		, tx ? "send" : "recv", MAC_ARG(da), MAC_ARG(sa));
	RTW_PRINT_SEL(sel, "htype=%u, ptype=0x%04x, hlen=%u, plen=%u, oper=%u\n"
		, GET_ARP_HTYPE(arp), GET_ARP_PTYPE(arp), GET_ARP_HLEN(arp)
		, GET_ARP_PLEN(arp), GET_ARP_OPER(arp));
	RTW_PRINT_SEL(sel, "sha="MAC_FMT", spa="IP_FMT"\n"
		, MAC_ARG(ARP_SENDER_MAC_ADDR(arp)), IP_ARG(ARP_SENDER_IP_ADDR(arp)));
	RTW_PRINT_SEL(sel, "tha="MAC_FMT", tpa="IP_FMT"\n"
		, MAC_ARG(ARP_TARGET_MAC_ADDR(arp)), IP_ARG(ARP_TARGET_IP_ADDR(arp)));
}

