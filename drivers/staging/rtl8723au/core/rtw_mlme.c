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
 ******************************************************************************/
#define _RTW_MLME_C_

#include <osdep_service.h>
#include <drv_types.h>
#include <recv_osdep.h>
#include <xmit_osdep.h>
#include <hal_intf.h>
#include <mlme_osdep.h>
#include <sta_info.h>
#include <linux/ieee80211.h>
#include <wifi.h>
#include <wlan_bssdef.h>
#include <rtw_sreset.h>

static struct wlan_network *
rtw_select_candidate_from_queue(struct mlme_priv *pmlmepriv);
static int rtw_do_join(struct rtw_adapter *padapter);

static void rtw_init_mlme_timer(struct rtw_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	setup_timer(&pmlmepriv->assoc_timer, rtw23a_join_to_handler,
		    (unsigned long)padapter);

	setup_timer(&pmlmepriv->scan_to_timer, rtw_scan_timeout_handler23a,
		    (unsigned long)padapter);

	setup_timer(&pmlmepriv->dynamic_chk_timer,
		    rtw_dynamic_check_timer_handler, (unsigned long)padapter);

	setup_timer(&pmlmepriv->set_scan_deny_timer,
		    rtw_set_scan_deny_timer_hdl, (unsigned long)padapter);
}

int rtw_init_mlme_priv23a(struct rtw_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	int res = _SUCCESS;

	pmlmepriv->nic_hdl = padapter;

	pmlmepriv->fw_state = 0;
	pmlmepriv->cur_network.network.ifmode = NL80211_IFTYPE_UNSPECIFIED;
	/*  1: active, 0: pasive. Maybe someday we should rename this
	    varable to "active_mode" (Jeff) */
	pmlmepriv->scan_mode = SCAN_ACTIVE;

	spin_lock_init(&pmlmepriv->lock);
	_rtw_init_queue23a(&pmlmepriv->scanned_queue);

	memset(&pmlmepriv->assoc_ssid, 0, sizeof(struct cfg80211_ssid));

	rtw_clear_scan_deny(padapter);

	rtw_init_mlme_timer(padapter);
	return res;
}

#ifdef CONFIG_8723AU_AP_MODE
static void rtw_free_mlme_ie_data(u8 **ppie, u32 *plen)
{
	if (*ppie) {
		kfree(*ppie);
		*plen = 0;
		*ppie = NULL;
	}
}
#endif

void rtw23a_free_mlme_priv_ie_data(struct mlme_priv *pmlmepriv)
{
#ifdef CONFIG_8723AU_AP_MODE
	kfree(pmlmepriv->assoc_req);
	kfree(pmlmepriv->assoc_rsp);
	rtw_free_mlme_ie_data(&pmlmepriv->wps_probe_req_ie,
			      &pmlmepriv->wps_probe_req_ie_len);
#endif
}

void rtw_free_mlme_priv23a(struct mlme_priv *pmlmepriv)
{
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
		 ("rtw_free_mlme_priv23a\n"));

	rtw23a_free_mlme_priv_ie_data(pmlmepriv);
}

struct wlan_network *rtw_alloc_network(struct mlme_priv *pmlmepriv, int gfp)
{
	struct wlan_network *pnetwork;

	pnetwork = kzalloc(sizeof(struct wlan_network), gfp);
	if (pnetwork) {
		INIT_LIST_HEAD(&pnetwork->list);
		pnetwork->network_type = 0;
		pnetwork->fixed = false;
		pnetwork->last_scanned = jiffies;
		pnetwork->aid = 0;
		pnetwork->join_res = 0;
	}

	return pnetwork;
}

static void _rtw_free_network23a(struct mlme_priv *pmlmepriv,
				 struct wlan_network *pnetwork)
{
	if (!pnetwork)
		return;

	if (pnetwork->fixed == true)
		return;

	list_del_init(&pnetwork->list);

	kfree(pnetwork);
}

/*
 return the wlan_network with the matching addr

 Shall be calle under atomic context... to avoid possible racing condition...
*/
struct wlan_network *
rtw_find_network23a(struct rtw_queue *scanned_queue, u8 *addr)
{
	struct list_head *phead, *plist;
	struct wlan_network *pnetwork = NULL;

	if (is_zero_ether_addr(addr)) {
		pnetwork = NULL;
		goto exit;
	}

	/* spin_lock_bh(&scanned_queue->lock); */

	phead = get_list_head(scanned_queue);
	plist = phead->next;

	while (plist != phead) {
		pnetwork = container_of(plist, struct wlan_network, list);

		if (ether_addr_equal(addr, pnetwork->network.MacAddress))
			break;

		plist = plist->next;
        }

	if (plist == phead)
		pnetwork = NULL;

	/* spin_unlock_bh(&scanned_queue->lock); */

exit:

	return pnetwork;
}

void rtw_free_network_queue23a(struct rtw_adapter *padapter)
{
	struct list_head *phead, *plist, *ptmp;
	struct wlan_network *pnetwork;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct rtw_queue *scanned_queue = &pmlmepriv->scanned_queue;

	spin_lock_bh(&scanned_queue->lock);

	phead = get_list_head(scanned_queue);

	list_for_each_safe(plist, ptmp, phead) {
		pnetwork = container_of(plist, struct wlan_network, list);

		_rtw_free_network23a(pmlmepriv, pnetwork);
	}

	spin_unlock_bh(&scanned_queue->lock);
}

int rtw_if_up23a(struct rtw_adapter *padapter)
{
	int res;

	if (padapter->bDriverStopped || padapter->bSurpriseRemoved ||
	    !check_fwstate(&padapter->mlmepriv, _FW_LINKED)) {
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
			 ("rtw_if_up23a:bDriverStopped(%d) OR "
			  "bSurpriseRemoved(%d)", padapter->bDriverStopped,
			  padapter->bSurpriseRemoved));
		res = false;
	} else
		res =  true;

	return res;
}

void rtw_generate_random_ibss23a(u8* pibss)
{
	unsigned long curtime = jiffies;

	pibss[0] = 0x02;  /* in ad-hoc mode bit1 must set to 1 */
	pibss[1] = 0x11;
	pibss[2] = 0x87;
	pibss[3] = curtime & 0xff;/* p[0]; */
	pibss[4] = (curtime >> 8) & 0xff;/* p[1]; */
	pibss[5] = (curtime >> 16) & 0xff;/* p[2]; */

	return;
}

void rtw_set_roaming(struct rtw_adapter *adapter, u8 to_roaming)
{
	if (to_roaming == 0)
		adapter->mlmepriv.to_join = false;
	adapter->mlmepriv.to_roaming = to_roaming;
}

static void _rtw_roaming(struct rtw_adapter *padapter,
			 struct wlan_network *tgt_network)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_network *pnetwork;
	int do_join_r;

	if (tgt_network)
		pnetwork = tgt_network;
	else
		pnetwork = &pmlmepriv->cur_network;

	if (padapter->mlmepriv.to_roaming > 0) {
		DBG_8723A("roaming from %s("MAC_FMT"), length:%d\n",
			  pnetwork->network.Ssid.ssid,
			  MAC_ARG(pnetwork->network.MacAddress),
			  pnetwork->network.Ssid.ssid_len);
		memcpy(&pmlmepriv->assoc_ssid, &pnetwork->network.Ssid,
		       sizeof(struct cfg80211_ssid));

		pmlmepriv->assoc_by_bssid = false;

		while (1) {
			do_join_r = rtw_do_join(padapter);
			if (do_join_r == _SUCCESS)
				break;
			else {
				DBG_8723A("roaming do_join return %d\n",
					  do_join_r);
				pmlmepriv->to_roaming--;

				if (padapter->mlmepriv.to_roaming > 0)
					continue;
				else {
					DBG_8723A("%s(%d) -to roaming fail, "
						  "indicate_disconnect\n",
						  __func__, __LINE__);
					rtw_indicate_disconnect23a(padapter);
					break;
				}
			}
		}
	}
}

void rtw23a_roaming(struct rtw_adapter *padapter,
		    struct wlan_network *tgt_network)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	spin_lock_bh(&pmlmepriv->lock);
	_rtw_roaming(padapter, tgt_network);
	spin_unlock_bh(&pmlmepriv->lock);
}

static void rtw_free_network_nolock(struct mlme_priv *pmlmepriv,
				    struct wlan_network *pnetwork)
{
	_rtw_free_network23a(pmlmepriv, pnetwork);
}

bool rtw_is_same_ibss23a(struct rtw_adapter *adapter,
			 struct wlan_network *pnetwork)
{
	int ret;
	struct security_priv *psecuritypriv = &adapter->securitypriv;

	if (psecuritypriv->dot11PrivacyAlgrthm != 0 &&
	    pnetwork->network.Privacy == 0)
		ret = false;
	else if (psecuritypriv->dot11PrivacyAlgrthm == 0 &&
		 pnetwork->network.Privacy == 1)
		ret = false;
	else
		ret = true;

	return ret;
}

inline int is_same_ess(struct wlan_bssid_ex *a, struct wlan_bssid_ex *b);
inline int is_same_ess(struct wlan_bssid_ex *a, struct wlan_bssid_ex *b)
{
	return (a->Ssid.ssid_len == b->Ssid.ssid_len) &&
		!memcmp(a->Ssid.ssid, b->Ssid.ssid, a->Ssid.ssid_len);
}

int is_same_network23a(struct wlan_bssid_ex *src, struct wlan_bssid_ex *dst)
{
	u16 s_cap, d_cap;

	s_cap = src->capability;
	d_cap = dst->capability;

	return ((src->Ssid.ssid_len == dst->Ssid.ssid_len) &&
		/*	(src->DSConfig == dst->DSConfig) && */
		ether_addr_equal(src->MacAddress, dst->MacAddress) &&
		!memcmp(src->Ssid.ssid, dst->Ssid.ssid, src->Ssid.ssid_len) &&
		(s_cap & WLAN_CAPABILITY_IBSS) ==
		(d_cap & WLAN_CAPABILITY_IBSS) &&
		(s_cap & WLAN_CAPABILITY_ESS) == (d_cap & WLAN_CAPABILITY_ESS));
}

struct wlan_network *
rtw_get_oldest_wlan_network23a(struct rtw_queue *scanned_queue)
{
	struct list_head *plist, *phead;
	struct wlan_network *pwlan;
	struct wlan_network *oldest = NULL;

	phead = get_list_head(scanned_queue);

	list_for_each(plist, phead) {
		pwlan = container_of(plist, struct wlan_network, list);

		if (pwlan->fixed != true) {
			if (!oldest || time_after(oldest->last_scanned,
						  pwlan->last_scanned))
				oldest = pwlan;
		}
	}

	return oldest;
}

void update_network23a(struct wlan_bssid_ex *dst, struct wlan_bssid_ex *src,
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

	DBG_8723A("%s %s(%pM, ch%u) ss_ori:%3u, sq_ori:%3u, rssi_ori:%3ld, "
		  "ss_smp:%3u, sq_smp:%3u, rssi_smp:%3ld\n",
		  __func__, src->Ssid.ssid, src->MacAddress,
		  src->DSConfig, ss_ori, sq_ori, rssi_ori,
		  ss_smp, sq_smp, rssi_smp
	);

	/* The rule below is 1/5 for sample value, 4/5 for history value */
	if (check_fwstate(&padapter->mlmepriv, _FW_LINKED) &&
	    is_same_network23a(&padapter->mlmepriv.cur_network.network, src)) {
		/* Take the recvpriv's value for the connected AP*/
		ss_final = padapter->recvpriv.signal_strength;
		sq_final = padapter->recvpriv.signal_qual;
		/* the rssi value here is undecorated, and will be
		   used for antenna diversity */
		if (sq_smp != 101) /* from the right channel */
			rssi_final = (src->Rssi+dst->Rssi*4)/5;
		else
			rssi_final = rssi_ori;
	} else {
		if (sq_smp != 101) { /* from the right channel */
			ss_final = ((u32)src->PhyInfo.SignalStrength +
				    (u32)dst->PhyInfo.SignalStrength * 4) / 5;
			sq_final = ((u32)src->PhyInfo.SignalQuality +
				    (u32)dst->PhyInfo.SignalQuality * 4) / 5;
			rssi_final = src->Rssi+dst->Rssi * 4 / 5;
		} else {
			/* bss info not receving from the right channel, use
			   the original RX signal infos */
			ss_final = dst->PhyInfo.SignalStrength;
			sq_final = dst->PhyInfo.SignalQuality;
			rssi_final = dst->Rssi;
		}

	}

	if (update_ie)
		memcpy(dst, src, get_wlan_bssid_ex_sz(src));

	dst->PhyInfo.SignalStrength = ss_final;
	dst->PhyInfo.SignalQuality = sq_final;
	dst->Rssi = rssi_final;

	DBG_8723A("%s %s(%pM), SignalStrength:%u, SignalQuality:%u, "
		  "RawRSSI:%ld\n",  __func__, dst->Ssid.ssid, dst->MacAddress,
		  dst->PhyInfo.SignalStrength,
		  dst->PhyInfo.SignalQuality, dst->Rssi);
}

static void update_current_network(struct rtw_adapter *adapter,
				   struct wlan_bssid_ex *pnetwork)
{
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;

	if (check_fwstate(pmlmepriv, _FW_LINKED) &&
	    is_same_network23a(&pmlmepriv->cur_network.network, pnetwork)) {
		int bcn_size;
		update_network23a(&pmlmepriv->cur_network.network,
				  pnetwork,adapter, true);

		bcn_size = offsetof(struct ieee80211_mgmt, u.beacon.variable) -
			offsetof(struct ieee80211_mgmt, u.beacon);

		rtw_update_protection23a(adapter,
					 pmlmepriv->cur_network.network.IEs +
					 bcn_size,
					 pmlmepriv->cur_network.network.IELength);
	}
}

/*

Caller must hold pmlmepriv->lock first.

*/
static void rtw_update_scanned_network(struct rtw_adapter *adapter,
				       struct wlan_bssid_ex *target)
{
	struct list_head *plist, *phead;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct wlan_network *pnetwork = NULL;
	struct wlan_network *oldest = NULL;
	struct rtw_queue *queue = &pmlmepriv->scanned_queue;
	u32 bssid_ex_sz;
	int found = 0;

	spin_lock_bh(&queue->lock);
	phead = get_list_head(queue);

	list_for_each(plist, phead) {
		pnetwork = container_of(plist, struct wlan_network, list);

		if (is_same_network23a(&pnetwork->network, target)) {
			found = 1;
			break;
		}
		if (!oldest || time_after(oldest->last_scanned,
					  pnetwork->last_scanned))
			oldest = pnetwork;
	}

	/* If we didn't find a match, then get a new network slot to initialize
	 * with this beacon's information */
	if (!found) {
		pnetwork = rtw_alloc_network(pmlmepriv, GFP_ATOMIC);
		if (!pnetwork) {
			if (!oldest) {
				RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
					 ("\n\n\nsomething wrong here\n\n\n"));
				goto exit;
			}
			pnetwork = oldest;
		} else
			list_add_tail(&pnetwork->list, &queue->queue);

		bssid_ex_sz = get_wlan_bssid_ex_sz(target);
		target->Length = bssid_ex_sz;
		memcpy(&pnetwork->network, target, bssid_ex_sz);

		/*  variable initialize */
		pnetwork->fixed = false;
		pnetwork->last_scanned = jiffies;

		pnetwork->network_type = 0;
		pnetwork->aid = 0;
		pnetwork->join_res = 0;

		/* bss info not receving from the right channel */
		if (pnetwork->network.PhyInfo.SignalQuality == 101)
			pnetwork->network.PhyInfo.SignalQuality = 0;
	} else {
		/*
		 * we have an entry and we are going to update it. But
		 * this entry may be already expired. In this case we
		 * do the same as we found a new net and call the
		 * new_net handler
		 */
		bool update_ie = true;

		pnetwork->last_scanned = jiffies;

		/* target.reserved == 1, means that scanned network is
		 * a bcn frame. */
		if (pnetwork->network.IELength > target->IELength &&
		    target->reserved == 1)
			update_ie = false;

		update_network23a(&pnetwork->network, target,adapter,
				  update_ie);
	}

exit:
	spin_unlock_bh(&queue->lock);
}

static void rtw_add_network(struct rtw_adapter *adapter,
			    struct wlan_bssid_ex *pnetwork)
{
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
	int bselected = true;

	desired_encmode = psecuritypriv->ndisencryptstatus;
	privacy = pnetwork->network.Privacy;

	if (check_fwstate(pmlmepriv, WIFI_UNDER_WPS)) {
		if (cfg80211_find_vendor_ie(WLAN_OUI_MICROSOFT,
					    WLAN_OUI_TYPE_MICROSOFT_WPA,
					    pnetwork->network.IEs +
					    _FIXED_IE_LENGTH_,
					    pnetwork->network.IELength -
					    _FIXED_IE_LENGTH_))
			return true;
		else
			return false;
	}
	if (adapter->registrypriv.wifi_spec == 1) {
		/* for  correct flow of 8021X  to do.... */
		if (desired_encmode == Ndis802_11EncryptionDisabled &&
		    privacy != 0)
	            bselected = false;
	}

	if (desired_encmode != Ndis802_11EncryptionDisabled && privacy == 0) {
		DBG_8723A("desired_encmode: %d, privacy: %d\n",
			  desired_encmode, privacy);
		bselected = false;
	}

	if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)) {
		if (pnetwork->network.ifmode !=
		    pmlmepriv->cur_network.network.ifmode)
			bselected = false;
	}

	return bselected;
}

/* TODO: Perry : For Power Management */
void rtw_atimdone_event_callback23a(struct rtw_adapter *adapter, const u8 *pbuf)
{
	RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("receive atimdone_evet\n"));

	return;
}

void rtw_survey_event_cb23a(struct rtw_adapter *adapter, const u8 *pbuf)
{
	u32 len;
	struct wlan_bssid_ex *pnetwork;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct survey_event *survey = (struct survey_event *)pbuf;

	pnetwork = survey->bss;

	RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,
		 ("rtw_survey_event_cb23a, ssid=%s\n", pnetwork->Ssid.ssid));

	len = get_wlan_bssid_ex_sz(pnetwork);
	if (len > (sizeof(struct wlan_bssid_ex))) {
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,
			 ("\n ****rtw_survey_event_cb23a: return a wrong "
			  "bss ***\n"));
		return;
	}

	spin_lock_bh(&pmlmepriv->lock);

	/*  update IBSS_network 's timestamp */
	if (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)) {
		/* RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
		   "rtw_survey_event_cb23a : WIFI_ADHOC_MASTER_STATE\n\n"); */
		if (ether_addr_equal(pmlmepriv->cur_network.network.MacAddress,
				     pnetwork->MacAddress)) {
			struct wlan_network* ibss_wlan;

			memcpy(pmlmepriv->cur_network.network.IEs,
			       pnetwork->IEs, 8);
			pmlmepriv->cur_network.network.beacon_interval =
				pnetwork->beacon_interval;
			pmlmepriv->cur_network.network.capability =
				pnetwork->capability;
			pmlmepriv->cur_network.network.tsf = pnetwork->tsf;
			spin_lock_bh(&pmlmepriv->scanned_queue.lock);
			ibss_wlan = rtw_find_network23a(
				&pmlmepriv->scanned_queue,
				pnetwork->MacAddress);
			if (ibss_wlan) {
				memcpy(ibss_wlan->network.IEs,
				       pnetwork->IEs, 8);
				pmlmepriv->cur_network.network.beacon_interval =
					ibss_wlan->network.beacon_interval;
				pmlmepriv->cur_network.network.capability =
					ibss_wlan->network.capability;
				pmlmepriv->cur_network.network.tsf =
					ibss_wlan->network.tsf;
				spin_unlock_bh(&pmlmepriv->scanned_queue.lock);
				goto exit;
			}
			spin_unlock_bh(&pmlmepriv->scanned_queue.lock);
		}
	}

	/*  lock pmlmepriv->lock when you accessing network_q */
	if (!check_fwstate(pmlmepriv, _FW_UNDER_LINKING)) {
	        if (pnetwork->Ssid.ssid[0] == 0)
			pnetwork->Ssid.ssid_len = 0;

		rtw_add_network(adapter, pnetwork);
	}

exit:

	spin_unlock_bh(&pmlmepriv->lock);

	kfree(survey->bss);
	survey->bss = NULL;

	return;
}

void
rtw_surveydone_event_callback23a(struct rtw_adapter *adapter, const u8 *pbuf)
{
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
	int ret;

	spin_lock_bh(&pmlmepriv->lock);

	if (pmlmepriv->wps_probe_req_ie) {
		pmlmepriv->wps_probe_req_ie_len = 0;
		kfree(pmlmepriv->wps_probe_req_ie);
		pmlmepriv->wps_probe_req_ie = NULL;
	}

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
		 ("rtw_surveydone_event_callback23a: fw_state:%x\n\n",
		  get_fwstate(pmlmepriv)));

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY)) {
		del_timer_sync(&pmlmepriv->scan_to_timer);

		_clr_fwstate_(pmlmepriv, _FW_UNDER_SURVEY);
	} else {
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
			 ("nic status =%x, survey done event comes too late!\n",
			  get_fwstate(pmlmepriv)));
	}

	rtw_set_signal_stat_timer(&adapter->recvpriv);

	if (pmlmepriv->to_join == true) {
		set_fwstate(pmlmepriv, _FW_UNDER_LINKING);
		if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)) {
			ret = rtw_select_and_join_from_scanned_queue23a(
				pmlmepriv);
			if (ret != _SUCCESS)
				rtw_do_join_adhoc(adapter);
		} else {
			pmlmepriv->to_join = false;
			ret = rtw_select_and_join_from_scanned_queue23a(
				pmlmepriv);
			if (ret != _SUCCESS) {
				DBG_8723A("try_to_join, but select scanning "
					  "queue fail, to_roaming:%d\n",
					  adapter->mlmepriv.to_roaming);
				if (adapter->mlmepriv.to_roaming) {
					if (--pmlmepriv->to_roaming == 0 ||
					    rtw_sitesurvey_cmd23a(
						    adapter,
						    &pmlmepriv->assoc_ssid, 1,
						    NULL, 0) != _SUCCESS) {
						rtw_set_roaming(adapter, 0);
						rtw_free_assoc_resources23a(
							adapter, 1);
						rtw_indicate_disconnect23a(
							adapter);
					} else
						pmlmepriv->to_join = true;
				}
				_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);
			}
		}
	}

	spin_unlock_bh(&pmlmepriv->lock);

	rtw_os_xmit_schedule23a(adapter);

	if (pmlmeext->sitesurvey_res.bss_cnt == 0)
		rtw_sreset_reset(adapter);

	rtw_cfg80211_surveydone_event_callback(adapter);
}

static void free_scanqueue(struct mlme_priv *pmlmepriv)
{
	struct wlan_network *pnetwork;
	struct rtw_queue *scan_queue = &pmlmepriv->scanned_queue;
	struct list_head *plist, *phead, *ptemp;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_, ("+free_scanqueue\n"));
	spin_lock_bh(&scan_queue->lock);

	phead = get_list_head(scan_queue);

	list_for_each_safe(plist, ptemp, phead) {
		pnetwork = container_of(plist, struct wlan_network, list);
		pnetwork->fixed = false;
		_rtw_free_network23a(pmlmepriv, pnetwork);
        }

	spin_unlock_bh(&scan_queue->lock);
}

/*
 *rtw_free_assoc_resources23a: the caller has to lock pmlmepriv->lock
 */
void rtw_free_assoc_resources23a(struct rtw_adapter *adapter,
				 int lock_scanned_queue)
{
	struct wlan_network* pwlan;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct sta_priv *pstapriv = &adapter->stapriv;
	struct wlan_network *tgt_network = &pmlmepriv->cur_network;
	struct sta_info* psta;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_,
		 ("+rtw_free_assoc_resources23a\n"));
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
		 ("tgt_network->network.MacAddress="MAC_FMT" ssid=%s\n",
		  MAC_ARG(tgt_network->network.MacAddress),
		  tgt_network->network.Ssid.ssid));

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE|WIFI_AP_STATE)) {
		psta = rtw_get_stainfo23a(&adapter->stapriv,
					  tgt_network->network.MacAddress);

		spin_lock_bh(&pstapriv->sta_hash_lock);
		rtw_free_stainfo23a(adapter,  psta);
		spin_unlock_bh(&pstapriv->sta_hash_lock);
	}

	if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE |
			  WIFI_ADHOC_MASTER_STATE|WIFI_AP_STATE)) {
		rtw_free_all_stainfo23a(adapter);

		psta = rtw_get_bcmc_stainfo23a(adapter);
		spin_lock_bh(&pstapriv->sta_hash_lock);
		rtw_free_stainfo23a(adapter, psta);
		spin_unlock_bh(&pstapriv->sta_hash_lock);

		rtw_init_bcmc_stainfo23a(adapter);
	}

	if (lock_scanned_queue)
		spin_lock_bh(&pmlmepriv->scanned_queue.lock);

	pwlan = rtw_find_network23a(&pmlmepriv->scanned_queue,
				    tgt_network->network.MacAddress);
	if (pwlan)
		pwlan->fixed = false;
	else
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,
			 ("rtw_free_assoc_resources23a : pwlan== NULL\n"));

	if (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) &&
	    adapter->stapriv.asoc_sta_count == 1)
		rtw_free_network_nolock(pmlmepriv, pwlan);

	if (lock_scanned_queue)
		spin_unlock_bh(&pmlmepriv->scanned_queue.lock);

	pmlmepriv->key_mask = 0;
}

/*
*rtw_indicate_connect23a: the caller has to lock pmlmepriv->lock
*/
void rtw_indicate_connect23a(struct rtw_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
		 ("+rtw_indicate_connect23a\n"));

	pmlmepriv->to_join = false;

	if (!check_fwstate(&padapter->mlmepriv, _FW_LINKED)) {
		set_fwstate(pmlmepriv, _FW_LINKED);

		rtw_led_control(padapter, LED_CTL_LINK);

		rtw_cfg80211_indicate_connect(padapter);

		netif_carrier_on(padapter->pnetdev);

		if (padapter->pid[2] != 0)
			kill_pid(find_vpid(padapter->pid[2]), SIGALRM, 1);
	}

	rtw_set_roaming(padapter, 0);

	rtw_set_scan_deny(padapter, 3000);

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
		 ("-rtw_indicate_connect23a: fw_state=0x%08x\n",
		  get_fwstate(pmlmepriv)));
}

/*
 *rtw_indicate_disconnect23a: the caller has to lock pmlmepriv->lock
 */
void rtw_indicate_disconnect23a(struct rtw_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
		 ("+rtw_indicate_disconnect23a\n"));

	_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING|WIFI_UNDER_WPS);

        /* DBG_8723A("clear wps when %s\n", __func__); */

	if (padapter->mlmepriv.to_roaming > 0)
		_clr_fwstate_(pmlmepriv, _FW_LINKED);

	if (check_fwstate(&padapter->mlmepriv, _FW_LINKED) ||
	    padapter->mlmepriv.to_roaming <= 0) {
		rtw_os_indicate_disconnect23a(padapter);

		/* set ips_deny_time to avoid enter IPS before LPS leave */
		padapter->pwrctrlpriv.ips_deny_time =
			jiffies + msecs_to_jiffies(3000);

		_clr_fwstate_(pmlmepriv, _FW_LINKED);

		rtw_led_control(padapter, LED_CTL_NO_LINK);

		rtw_clear_scan_deny(padapter);

	}

	rtw_lps_ctrl_wk_cmd23a(padapter, LPS_CTRL_DISCONNECT, 1);
}

void rtw_scan_abort23a(struct rtw_adapter *adapter)
{
	unsigned long start;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;

	start = jiffies;
	pmlmeext->scan_abort = true;
	while (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY) &&
	       jiffies_to_msecs(jiffies - start) <= 200) {
		if (adapter->bDriverStopped || adapter->bSurpriseRemoved)
			break;

		DBG_8723A("%s(%s): fw_state = _FW_UNDER_SURVEY!\n",
			  __func__, adapter->pnetdev->name);
		msleep(20);
	}

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY)) {
		if (!adapter->bDriverStopped && !adapter->bSurpriseRemoved)
			DBG_8723A("%s(%s): waiting for scan_abort time out!\n",
				  __func__, adapter->pnetdev->name);
		rtw_cfg80211_indicate_scan_done(wdev_to_priv(adapter->rtw_wdev),
						true);
	}
	pmlmeext->scan_abort = false;
}

static struct sta_info *
rtw_joinbss_update_stainfo(struct rtw_adapter *padapter,
			   struct wlan_network *pnetwork)
{
	int i;
	struct sta_info *bmc_sta, *psta;
	struct recv_reorder_ctrl *preorder_ctrl;
	struct sta_priv *pstapriv = &padapter->stapriv;

	psta = rtw_get_stainfo23a(pstapriv, pnetwork->network.MacAddress);
	if (!psta)
		psta = rtw_alloc_stainfo23a(pstapriv,
					    pnetwork->network.MacAddress,
					    GFP_ATOMIC);

	if (psta) { /* update ptarget_sta */
		DBG_8723A("%s\n", __func__);

		psta->aid  = pnetwork->join_res;
		psta->mac_id = 0;

		/* sta mode */
		rtl8723a_SetHalODMVar(padapter, HAL_ODM_STA_INFO, psta, true);

		/* security related */
		if (padapter->securitypriv.dot11AuthAlgrthm ==
		    dot11AuthAlgrthm_8021X) {
			padapter->securitypriv.binstallGrpkey = 0;
			padapter->securitypriv.busetkipkey = 0;

			psta->ieee8021x_blocked = true;
			psta->dot118021XPrivacy =
				padapter->securitypriv.dot11PrivacyAlgrthm;

			memset(&psta->dot118021x_UncstKey, 0,
			       sizeof (union Keytype));

			memset(&psta->dot11tkiprxmickey, 0,
			       sizeof (union Keytype));
			memset(&psta->dot11tkiptxmickey, 0,
			       sizeof (union Keytype));

			memset(&psta->dot11txpn, 0, sizeof (union pn48));
			memset(&psta->dot11rxpn, 0, sizeof (union pn48));
		}

		/*	Commented by Albert 2012/07/21 */
		/*	When doing the WPS, the wps_ie_len won't equal to 0 */
		/*	And the Wi-Fi driver shouldn't allow the data packet
			to be tramsmitted. */
		if (padapter->securitypriv.wps_ie_len != 0) {
			psta->ieee8021x_blocked = true;
			padapter->securitypriv.wps_ie_len = 0;
		}

		/* for A-MPDU Rx reordering buffer control for bmc_sta &
		 * sta_info */
		/* if A-MPDU Rx is enabled, reseting
		   rx_ordering_ctrl wstart_b(indicate_seq) to default
		   value = 0xffff */
		/* todo: check if AP can send A-MPDU packets */
		for (i = 0; i < 16 ; i++) {
			/* preorder_ctrl = &precvpriv->recvreorder_ctrl[i]; */
			preorder_ctrl = &psta->recvreorder_ctrl[i];
			preorder_ctrl->enable = false;
			preorder_ctrl->indicate_seq = 0xffff;
			preorder_ctrl->wend_b = 0xffff;
			/* max_ampdu_sz; ex. 32(kbytes) -> wsize_b = 32 */
			preorder_ctrl->wsize_b = 64;
		}

		bmc_sta = rtw_get_bcmc_stainfo23a(padapter);
		if (bmc_sta) {
			for (i = 0; i < 16 ; i++) {
				preorder_ctrl = &bmc_sta->recvreorder_ctrl[i];
				preorder_ctrl->enable = false;
				preorder_ctrl->indicate_seq = 0xffff;
				preorder_ctrl->wend_b = 0xffff;
				/* max_ampdu_sz; ex. 32(kbytes) ->
				   wsize_b = 32 */
				preorder_ctrl->wsize_b = 64;
			}
		}

		/* misc. */
		update_sta_info23a(padapter, psta);

	}

	return psta;
}

/* pnetwork : returns from rtw23a_joinbss_event_cb */
/* ptarget_wlan: found from scanned_queue */
static void
rtw_joinbss_update_network23a(struct rtw_adapter *padapter,
			      struct wlan_network *ptarget_wlan,
			      struct wlan_network  *pnetwork)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_network *cur_network = &pmlmepriv->cur_network;
	int bcn_size;

	DBG_8723A("%s\n", __func__);

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
		 ("\nfw_state:%x, BSSID:"MAC_FMT"\n", get_fwstate(pmlmepriv),
		  MAC_ARG(pnetwork->network.MacAddress)));

	/*  why not use ptarget_wlan?? */
	memcpy(&cur_network->network, &pnetwork->network,
	       pnetwork->network.Length);
	/*  some IEs in pnetwork is wrong, so we should use ptarget_wlan IEs */
	cur_network->network.IELength = ptarget_wlan->network.IELength;
	memcpy(&cur_network->network.IEs[0], &ptarget_wlan->network.IEs[0],
	       MAX_IE_SZ);

	cur_network->aid = pnetwork->join_res;

	rtw_set_signal_stat_timer(&padapter->recvpriv);
	padapter->recvpriv.signal_strength =
		ptarget_wlan->network.PhyInfo.SignalStrength;
	padapter->recvpriv.signal_qual =
		ptarget_wlan->network.PhyInfo.SignalQuality;
	/*
	 * the ptarget_wlan->network.Rssi is raw data, we use
	 * ptarget_wlan->network.PhyInfo.SignalStrength instead (has scaled)
	 */
	padapter->recvpriv.rssi = translate_percentage_to_dbm(
		ptarget_wlan->network.PhyInfo.SignalStrength);
	DBG_8723A("%s signal_strength:%3u, rssi:%3d, signal_qual:%3u\n",
		  __func__, padapter->recvpriv.signal_strength,
		  padapter->recvpriv.rssi, padapter->recvpriv.signal_qual);
	rtw_set_signal_stat_timer(&padapter->recvpriv);

	/* update fw_state will clr _FW_UNDER_LINKING here indirectly */
	switch (pnetwork->network.ifmode) {
	case NL80211_IFTYPE_P2P_CLIENT:
	case NL80211_IFTYPE_STATION:
		if (pmlmepriv->fw_state & WIFI_UNDER_WPS)
			pmlmepriv->fw_state = WIFI_STATION_STATE|WIFI_UNDER_WPS;
		else
			pmlmepriv->fw_state = WIFI_STATION_STATE;
		break;
	case NL80211_IFTYPE_ADHOC:
		pmlmepriv->fw_state = WIFI_ADHOC_STATE;
		break;
	default:
		pmlmepriv->fw_state = WIFI_NULL_STATE;
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
			 ("Invalid network_mode\n"));
		break;
	}

	bcn_size = offsetof(struct ieee80211_mgmt, u.beacon.variable) -
		offsetof(struct ieee80211_mgmt, u.beacon);

	rtw_update_protection23a(padapter, cur_network->network.IEs +
				 bcn_size, cur_network->network.IELength);

	rtw_update_ht_cap23a(padapter, cur_network->network.IEs,
			     cur_network->network.IELength);
}

/*
 * Notes:
 * the fucntion could be > passive_level (the same context as Rx tasklet)
 * pnetwork : returns from rtw23a_joinbss_event_cb
 * ptarget_wlan: found from scanned_queue
 * if join_res > 0, for (fw_state==WIFI_STATION_STATE),
 * we check if  "ptarget_sta" & "ptarget_wlan" exist.
 * if join_res > 0, for (fw_state==WIFI_ADHOC_STATE),
 * we only check if "ptarget_wlan" exist.
 * if join_res > 0, update "cur_network->network" from "pnetwork->network"
 * if (ptarget_wlan !=NULL).
 */

void rtw_joinbss_event_prehandle23a(struct rtw_adapter *adapter, u8 *pbuf)
{
	struct sta_info *ptarget_sta, *pcur_sta;
	struct sta_priv *pstapriv = &adapter->stapriv;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct wlan_network *pnetwork = (struct wlan_network *)pbuf;
	struct wlan_network *cur_network = &pmlmepriv->cur_network;
	struct wlan_network *pcur_wlan, *ptarget_wlan = NULL;
	bool the_same_macaddr;

	RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,
		 ("joinbss event call back received with res=%d\n",
		  pnetwork->join_res));

	if (pmlmepriv->assoc_ssid.ssid_len == 0) {
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,
			 ("@@@@@   joinbss event call back  for Any SSid\n"));
	} else {
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,
			 ("@@@@@   rtw23a_joinbss_event_cb for SSid:%s\n",
			  pmlmepriv->assoc_ssid.ssid));
	}

	if (ether_addr_equal(pnetwork->network.MacAddress,
			     cur_network->network.MacAddress))
		the_same_macaddr = true;
	else
		the_same_macaddr = false;

	pnetwork->network.Length = get_wlan_bssid_ex_sz(&pnetwork->network);
	if (pnetwork->network.Length > sizeof(struct wlan_bssid_ex)) {
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,
			 ("\n\n ***joinbss_evt_callback return a wrong bss "
			  "***\n\n"));
		return;
	}

	spin_lock_bh(&pmlmepriv->lock);

	RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,
		 ("\n rtw23a_joinbss_event_cb !! _enter_critical\n"));

	if (pnetwork->join_res > 0) {
		spin_lock_bh(&pmlmepriv->scanned_queue.lock);
		if (check_fwstate(pmlmepriv,_FW_UNDER_LINKING)) {
			/* s1. find ptarget_wlan */
			if (check_fwstate(pmlmepriv, _FW_LINKED)) {
				if (the_same_macaddr == true) {
					ptarget_wlan = rtw_find_network23a(&pmlmepriv->scanned_queue, cur_network->network.MacAddress);
				} else {
					pcur_wlan = rtw_find_network23a(&pmlmepriv->scanned_queue, cur_network->network.MacAddress);
					if (pcur_wlan)
						pcur_wlan->fixed = false;

					pcur_sta = rtw_get_stainfo23a(pstapriv, cur_network->network.MacAddress);
					if (pcur_sta) {
						spin_lock_bh(&pstapriv->sta_hash_lock);
						rtw_free_stainfo23a(adapter,
								    pcur_sta);
						spin_unlock_bh(&pstapriv->sta_hash_lock);
					}

					ptarget_wlan = rtw_find_network23a(&pmlmepriv->scanned_queue, pnetwork->network.MacAddress);
					if (check_fwstate(pmlmepriv,
							  WIFI_STATION_STATE)) {
						if (ptarget_wlan)
							ptarget_wlan->fixed =
								true;
					}
				}

			} else {
				ptarget_wlan = rtw_find_network23a(
					&pmlmepriv->scanned_queue,
					pnetwork->network.MacAddress);
				if (check_fwstate(pmlmepriv,
						  WIFI_STATION_STATE)) {
					if (ptarget_wlan)
						ptarget_wlan->fixed = true;
				}
			}

			/* s2. update cur_network */
			if (ptarget_wlan)
				rtw_joinbss_update_network23a(adapter,
							      ptarget_wlan,
							      pnetwork);
			else {
				RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,
					 ("Can't find ptarget_wlan when "
					  "joinbss_event callback\n"));
				spin_unlock_bh(&pmlmepriv->scanned_queue.lock);
				goto ignore_joinbss_callback;
			}

			/* s3. find ptarget_sta & update ptarget_sta after
			   update cur_network only for station mode */
			if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
				ptarget_sta = rtw_joinbss_update_stainfo(
					adapter, pnetwork);
				if (!ptarget_sta) {
					RT_TRACE(_module_rtl871x_mlme_c_,
						 _drv_err_,
						 ("Can't update stainfo when "
						  "joinbss_event callback\n"));
					spin_unlock_bh(&pmlmepriv->scanned_queue.lock);
					goto ignore_joinbss_callback;
				}
			}

			/* s4. indicate connect */
			if (check_fwstate(pmlmepriv, WIFI_STATION_STATE))
				rtw_indicate_connect23a(adapter);
			else {
				/* adhoc mode will rtw_indicate_connect23a
				   when rtw_stassoc_event_callback23a */
				RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,
					 ("adhoc mode, fw_state:%x",
					  get_fwstate(pmlmepriv)));
			}

			/* s5. Cancle assoc_timer */
			del_timer_sync(&pmlmepriv->assoc_timer);

			RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,
				 ("Cancle assoc_timer\n"));
		} else {
			RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
				 ("rtw23a_joinbss_event_cb err: fw_state:%x",
				 get_fwstate(pmlmepriv)));
			spin_unlock_bh(&pmlmepriv->scanned_queue.lock);
			goto ignore_joinbss_callback;
		}
		spin_unlock_bh(&pmlmepriv->scanned_queue.lock);
	} else if (pnetwork->join_res == -4) {
		rtw_reset_securitypriv23a(adapter);
		mod_timer(&pmlmepriv->assoc_timer,
			  jiffies + msecs_to_jiffies(1));

		/* rtw_free_assoc_resources23a(adapter, 1); */

		if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING)) {
			RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
				 ("fail! clear _FW_UNDER_LINKING ^^^fw_state="
				  "%x\n", get_fwstate(pmlmepriv)));
			_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);
		}
	} else {
		/* if join_res < 0 (join fails), then try again */
		mod_timer(&pmlmepriv->assoc_timer,
			  jiffies + msecs_to_jiffies(1));
		_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);
	}

ignore_joinbss_callback:

	spin_unlock_bh(&pmlmepriv->lock);
}

void rtw23a_joinbss_event_cb(struct rtw_adapter *adapter, const u8 *pbuf)
{
	struct wlan_network *pnetwork = (struct wlan_network *)pbuf;

	mlmeext_joinbss_event_callback23a(adapter, pnetwork->join_res);

	rtw_os_xmit_schedule23a(adapter);
}

void rtw_stassoc_event_callback23a(struct rtw_adapter *adapter, const u8 *pbuf)
{
	struct sta_info *psta;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct stassoc_event *pstassoc = (struct stassoc_event*)pbuf;
	struct wlan_network *cur_network = &pmlmepriv->cur_network;
	struct wlan_network *ptarget_wlan;

	if (rtw_access_ctrl23a(adapter, pstassoc->macaddr) == false)
		return;

#ifdef CONFIG_8723AU_AP_MODE
	if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		psta = rtw_get_stainfo23a(&adapter->stapriv, pstassoc->macaddr);
		if (psta) {
			/* bss_cap_update_on_sta_join23a(adapter, psta); */
			/* sta_info_update23a(adapter, psta); */
			ap_sta_info_defer_update23a(adapter, psta);
		}
		return;
	}
#endif
	/* for AD-HOC mode */
	psta = rtw_get_stainfo23a(&adapter->stapriv, pstassoc->macaddr);
	if (psta != NULL) {
		/* the sta have been in sta_info_queue => do nothing */
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,
			 ("Error: rtw_stassoc_event_callback23a: sta has "
			  "been in sta_hash_queue\n"));
		/* between drv has received this event before and
		   fw have not yet to set key to CAM_ENTRY) */
		return;
	}

	psta = rtw_alloc_stainfo23a(&adapter->stapriv, pstassoc->macaddr,
		GFP_KERNEL);
	if (!psta) {
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,
			 ("Can't alloc sta_info when "
			  "rtw_stassoc_event_callback23a\n"));
		return;
	}

	/* to do : init sta_info variable */
	psta->qos_option = 0;
	psta->mac_id = (uint)pstassoc->cam_id;
	/* psta->aid = (uint)pstassoc->cam_id; */
	DBG_8723A("%s\n",__func__);
	/* for ad-hoc mode */
	rtl8723a_SetHalODMVar(adapter, HAL_ODM_STA_INFO, psta, true);

	if (adapter->securitypriv.dot11AuthAlgrthm == dot11AuthAlgrthm_8021X)
		psta->dot118021XPrivacy =
			adapter->securitypriv.dot11PrivacyAlgrthm;

	psta->ieee8021x_blocked = false;

	spin_lock_bh(&pmlmepriv->lock);

	if (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) ||
	    check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)) {
		if (adapter->stapriv.asoc_sta_count == 2) {
			spin_lock_bh(&pmlmepriv->scanned_queue.lock);
			ptarget_wlan =
				rtw_find_network23a(&pmlmepriv->scanned_queue,
						    cur_network->network.MacAddress);
			if (ptarget_wlan)
				ptarget_wlan->fixed = true;
			spin_unlock_bh(&pmlmepriv->scanned_queue.lock);
			/*  a sta + bc/mc_stainfo (not Ibss_stainfo) */
			rtw_indicate_connect23a(adapter);
		}
	}

	spin_unlock_bh(&pmlmepriv->lock);

	mlmeext_sta_add_event_callback23a(adapter, psta);
}

void rtw_stadel_event_callback23a(struct rtw_adapter *adapter, const u8 *pbuf)
{
	int mac_id;
	struct sta_info *psta;
	struct wlan_network* pwlan;
	struct wlan_bssid_ex *pdev_network;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct stadel_event *pstadel = (struct stadel_event *)pbuf;
	struct sta_priv *pstapriv = &adapter->stapriv;
	struct wlan_network *tgt_network = &pmlmepriv->cur_network;

	psta = rtw_get_stainfo23a(&adapter->stapriv, pstadel->macaddr);
	if (psta)
		mac_id = psta->mac_id;
	else
		mac_id = pstadel->mac_id;

	DBG_8723A("%s(mac_id=%d)=" MAC_FMT "\n", __func__, mac_id,
		  MAC_ARG(pstadel->macaddr));

        if (check_fwstate(pmlmepriv, WIFI_AP_STATE))
		return;

	mlmeext_sta_del_event_callback23a(adapter);

	spin_lock_bh(&pmlmepriv->lock);

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
		if (adapter->mlmepriv.to_roaming > 0) {
			/* this stadel_event is caused by roaming,
			   decrease to_roaming */
			pmlmepriv->to_roaming--;
		} else if (adapter->mlmepriv.to_roaming == 0)
			rtw_set_roaming(adapter, adapter->registrypriv.max_roaming_times);
		if (*((u16 *)pstadel->rsvd) != WLAN_REASON_EXPIRATION_CHK)
			rtw_set_roaming(adapter, 0); /* don't roam */

		rtw_free_uc_swdec_pending_queue23a(adapter);

		rtw_free_assoc_resources23a(adapter, 1);
		rtw_indicate_disconnect23a(adapter);
		spin_lock_bh(&pmlmepriv->scanned_queue.lock);
		/*  remove the network entry in scanned_queue */
		pwlan = rtw_find_network23a(&pmlmepriv->scanned_queue,
					    tgt_network->network.MacAddress);
		if (pwlan) {
			pwlan->fixed = false;
			rtw_free_network_nolock(pmlmepriv, pwlan);
		}
		spin_unlock_bh(&pmlmepriv->scanned_queue.lock);

		_rtw_roaming(adapter, tgt_network);
	}

	if (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) ||
	    check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)) {

		spin_lock_bh(&pstapriv->sta_hash_lock);
		rtw_free_stainfo23a(adapter,  psta);
		spin_unlock_bh(&pstapriv->sta_hash_lock);

		/* a sta + bc/mc_stainfo (not Ibss_stainfo) */
		if (adapter->stapriv.asoc_sta_count == 1) {
			spin_lock_bh(&pmlmepriv->scanned_queue.lock);
			/* free old ibss network */
			/* pwlan = rtw_find_network23a(
			   &pmlmepriv->scanned_queue, pstadel->macaddr); */
			pwlan = rtw_find_network23a(&pmlmepriv->scanned_queue,
						    tgt_network->network.MacAddress);
			if (pwlan) {
				pwlan->fixed = false;
				rtw_free_network_nolock(pmlmepriv, pwlan);
			}
			spin_unlock_bh(&pmlmepriv->scanned_queue.lock);
			/* re-create ibss */
			pdev_network = &adapter->registrypriv.dev_network;

			memcpy(pdev_network, &tgt_network->network,
			       get_wlan_bssid_ex_sz(&tgt_network->network));

			rtw_do_join_adhoc(adapter);
		}
	}

	spin_unlock_bh(&pmlmepriv->lock);
}

/*
* rtw23a_join_to_handler - Timeout/faliure handler for CMD JoinBss
* @adapter: pointer to _adapter structure
*/
void rtw23a_join_to_handler (unsigned long data)
{
	struct rtw_adapter *adapter = (struct rtw_adapter *)data;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	int do_join_r;

	DBG_8723A("%s, fw_state=%x\n", __func__, get_fwstate(pmlmepriv));

	if (adapter->bDriverStopped || adapter->bSurpriseRemoved)
		return;

	spin_lock_bh(&pmlmepriv->lock);

	if (adapter->mlmepriv.to_roaming > 0) {
		/* join timeout caused by roaming */
		while (1) {
			pmlmepriv->to_roaming--;
			if (adapter->mlmepriv.to_roaming != 0) {
				/* try another */
				DBG_8723A("%s try another roaming\n", __func__);
				do_join_r = rtw_do_join(adapter);
				if (do_join_r != _SUCCESS) {
					DBG_8723A("%s roaming do_join return "
						  "%d\n", __func__ , do_join_r);
					continue;
				}
				break;
			} else {
				DBG_8723A("%s We've try roaming but fail\n",
					  __func__);
				rtw_indicate_disconnect23a(adapter);
				break;
			}
		}
	} else {
		rtw_indicate_disconnect23a(adapter);
		free_scanqueue(pmlmepriv);/*  */

		/* indicate disconnect for the case that join_timeout and
		   check_fwstate != FW_LINKED */
		rtw_cfg80211_indicate_disconnect(adapter);
	}

	spin_unlock_bh(&pmlmepriv->lock);

}

/*
* rtw_scan_timeout_handler23a - Timeout/Faliure handler for CMD SiteSurvey
* @data: pointer to _adapter structure
*/
void rtw_scan_timeout_handler23a(unsigned long data)
{
	struct rtw_adapter *adapter = (struct rtw_adapter *)data;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;

	DBG_8723A("%s(%s): fw_state =%x\n", __func__, adapter->pnetdev->name,
		  get_fwstate(pmlmepriv));

	spin_lock_bh(&pmlmepriv->lock);

	_clr_fwstate_(pmlmepriv, _FW_UNDER_SURVEY);

	spin_unlock_bh(&pmlmepriv->lock);

	rtw_cfg80211_indicate_scan_done(wdev_to_priv(adapter->rtw_wdev), true);
}

void rtw_dynamic_check_timer_handler(unsigned long data)
{
	struct rtw_adapter *adapter = (struct rtw_adapter *)data;

	if (adapter->hw_init_completed == false)
		goto out;

	if (adapter->bDriverStopped == true ||
	    adapter->bSurpriseRemoved == true)
		goto out;

	if (adapter->net_closed == true)
		goto out;

	rtw_dynamic_chk_wk_cmd23a(adapter);

out:
	mod_timer(&adapter->mlmepriv.dynamic_chk_timer,
		  jiffies + msecs_to_jiffies(2000));
}

inline bool rtw_is_scan_deny(struct rtw_adapter *adapter)
{
	struct mlme_priv *mlmepriv = &adapter->mlmepriv;
	return (atomic_read(&mlmepriv->set_scan_deny) != 0) ? true : false;
}

void rtw_clear_scan_deny(struct rtw_adapter *adapter)
{
	struct mlme_priv *mlmepriv = &adapter->mlmepriv;
	atomic_set(&mlmepriv->set_scan_deny, 0);
}

void rtw_set_scan_deny_timer_hdl(unsigned long data)
{
	struct rtw_adapter *adapter = (struct rtw_adapter *)data;
	rtw_clear_scan_deny(adapter);
}

void rtw_set_scan_deny(struct rtw_adapter *adapter, u32 ms)
{
	struct mlme_priv *mlmepriv = &adapter->mlmepriv;

	atomic_set(&mlmepriv->set_scan_deny, 1);
	mod_timer(&mlmepriv->set_scan_deny_timer,
		  jiffies + msecs_to_jiffies(ms));
}

#if defined(IEEE80211_SCAN_RESULT_EXPIRE)
#define RTW_SCAN_RESULT_EXPIRE IEEE80211_SCAN_RESULT_EXPIRE/HZ*1000 -1000 /* 3000 -1000 */
#else
#define RTW_SCAN_RESULT_EXPIRE 2000
#endif

/*
* Select a new join candidate from the original @param candidate and
*     @param competitor
* @return true: candidate is updated
* @return false: candidate is not updated
*/
static int rtw_check_join_candidate(struct mlme_priv *pmlmepriv,
				    struct wlan_network **candidate,
				    struct wlan_network *competitor)
{
	int updated = false;
	struct rtw_adapter *adapter;

	adapter = container_of(pmlmepriv, struct rtw_adapter, mlmepriv);

	/* check bssid, if needed */
	if (pmlmepriv->assoc_by_bssid == true) {
		if (!ether_addr_equal(competitor->network.MacAddress,
				      pmlmepriv->assoc_bssid))
			goto exit;
	}

	/* check ssid, if needed */
	if (pmlmepriv->assoc_ssid.ssid_len) {
		if (competitor->network.Ssid.ssid_len !=
		    pmlmepriv->assoc_ssid.ssid_len ||
		    memcmp(competitor->network.Ssid.ssid,
			   pmlmepriv->assoc_ssid.ssid,
			   pmlmepriv->assoc_ssid.ssid_len))
			goto exit;
	}

	if (rtw_is_desired_network(adapter, competitor) == false)
		goto exit;

	if (adapter->mlmepriv.to_roaming > 0) {
		unsigned int passed;

		passed = jiffies_to_msecs(jiffies - competitor->last_scanned);
		if (passed >= RTW_SCAN_RESULT_EXPIRE ||
		    is_same_ess(&competitor->network,
				&pmlmepriv->cur_network.network) == false)
			goto exit;
	}

	if (!*candidate ||
	    (*candidate)->network.Rssi<competitor->network.Rssi) {
		*candidate = competitor;
		updated = true;
	}

	if (updated) {
		DBG_8723A("[by_bssid:%u][assoc_ssid:%s][to_roaming:%u] "
			  "new candidate: %s("MAC_FMT") rssi:%d\n",
			  pmlmepriv->assoc_by_bssid,
			  pmlmepriv->assoc_ssid.ssid,
			  adapter->mlmepriv.to_roaming,
			  (*candidate)->network.Ssid.ssid,
			  MAC_ARG((*candidate)->network.MacAddress),
			  (int)(*candidate)->network.Rssi);
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

static int rtw_do_join(struct rtw_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	int ret;

	pmlmepriv->cur_network.join_res = -2;

	set_fwstate(pmlmepriv, _FW_UNDER_LINKING);

	pmlmepriv->to_join = true;

	ret = rtw_select_and_join_from_scanned_queue23a(pmlmepriv);
	if (ret == _SUCCESS) {
		pmlmepriv->to_join = false;
	} else {
		if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)) {
			/* switch to ADHOC_MASTER */
			ret = rtw_do_join_adhoc(padapter);
			if (ret != _SUCCESS)
				goto exit;
		} else {
			/*  can't associate ; reset under-linking */
			_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);

			ret = _FAIL;
			pmlmepriv->to_join = false;
		}
	}

exit:
	return ret;
}

static struct wlan_network *
rtw_select_candidate_from_queue(struct mlme_priv *pmlmepriv)
{
	struct wlan_network *pnetwork, *candidate = NULL;
	struct rtw_queue *queue = &pmlmepriv->scanned_queue;
	struct list_head *phead, *plist, *ptmp;

	spin_lock_bh(&pmlmepriv->scanned_queue.lock);
	phead = get_list_head(queue);

	list_for_each_safe(plist, ptmp, phead) {
		pnetwork = container_of(plist, struct wlan_network, list);
		if (!pnetwork) {
			RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
				 ("%s: return _FAIL:(pnetwork == NULL)\n",
				  __func__));
			goto exit;
		}

		rtw_check_join_candidate(pmlmepriv, &candidate, pnetwork);
	}

exit:
	spin_unlock_bh(&pmlmepriv->scanned_queue.lock);
	return candidate;
}


int rtw_do_join_adhoc(struct rtw_adapter *adapter)
{
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct wlan_bssid_ex *pdev_network;
	u8 *ibss;
	int ret;

	pdev_network = &adapter->registrypriv.dev_network;
	ibss = adapter->registrypriv.dev_network.MacAddress;

	_clr_fwstate_(pmlmepriv, _FW_UNDER_SURVEY);

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
		 ("switching to adhoc master\n"));

	memcpy(&pdev_network->Ssid, &pmlmepriv->assoc_ssid,
	       sizeof(struct cfg80211_ssid));

	rtw_update_registrypriv_dev_network23a(adapter);
	rtw_generate_random_ibss23a(ibss);

	pmlmepriv->fw_state = WIFI_ADHOC_MASTER_STATE;

	ret = rtw_createbss_cmd23a(adapter);
	if (ret != _SUCCESS) {
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
			 ("Error =>rtw_createbss_cmd23a status FAIL\n"));
	} else  {
		pmlmepriv->to_join = false;
	}

	return ret;
}

int rtw_do_join_network(struct rtw_adapter *adapter,
			struct wlan_network *candidate)
{
	int ret;

	/*  check for situation of  _FW_LINKED */
	if (check_fwstate(&adapter->mlmepriv, _FW_LINKED)) {
		DBG_8723A("%s: _FW_LINKED while ask_for_joinbss!\n", __func__);

		rtw_disassoc_cmd23a(adapter, 0, true);
		rtw_indicate_disconnect23a(adapter);
		rtw_free_assoc_resources23a(adapter, 0);
	}
	set_fwstate(&adapter->mlmepriv, _FW_UNDER_LINKING);

	ret = rtw_joinbss_cmd23a(adapter, candidate);

	if (ret == _SUCCESS)
		mod_timer(&adapter->mlmepriv.assoc_timer,
			  jiffies + msecs_to_jiffies(MAX_JOIN_TIMEOUT));

	return ret;
}

int rtw_select_and_join_from_scanned_queue23a(struct mlme_priv *pmlmepriv)
{
	struct rtw_adapter *adapter;
	struct wlan_network *candidate = NULL;
	int ret;

	adapter = pmlmepriv->nic_hdl;

	candidate = rtw_select_candidate_from_queue(pmlmepriv);
	if (!candidate) {
		DBG_8723A("%s: return _FAIL(candidate == NULL)\n", __func__);
		ret = _FAIL;
		goto exit;
	} else {
		DBG_8723A("%s: candidate: %s("MAC_FMT", ch:%u)\n", __func__,
			  candidate->network.Ssid.ssid,
			  MAC_ARG(candidate->network.MacAddress),
			  candidate->network.DSConfig);
	}

	ret = rtw_do_join_network(adapter, candidate);

exit:
	return ret;
}

int rtw_set_auth23a(struct rtw_adapter * adapter,
		    struct security_priv *psecuritypriv)
{
	struct cmd_obj* pcmd;
	struct setauth_parm *psetauthparm;
	struct cmd_priv *pcmdpriv = &adapter->cmdpriv;
	int res = _SUCCESS;

	pcmd = kzalloc(sizeof(struct cmd_obj), GFP_KERNEL);
	if (!pcmd) {
		res = _FAIL;  /* try again */
		goto exit;
	}

	psetauthparm = kzalloc(sizeof(struct setauth_parm), GFP_KERNEL);
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

	RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,
		 ("after enqueue set_auth_cmd, auth_mode=%x\n",
		  psecuritypriv->dot11AuthAlgrthm));

	res = rtw_enqueue_cmd23a(pcmdpriv, pcmd);

exit:

	return res;
}

int rtw_set_key23a(struct rtw_adapter *adapter,
		   struct security_priv *psecuritypriv, int keyid, u8 set_tx)
{
	u8 keylen;
	struct cmd_obj *pcmd;
	struct setkey_parm *psetkeyparm;
	struct cmd_priv *pcmdpriv = &adapter->cmdpriv;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	int res = _SUCCESS;

	if (keyid >= 4) {
		res = _FAIL;
		goto exit;
	}

	pcmd = kzalloc(sizeof(struct cmd_obj), GFP_KERNEL);
	if (!pcmd) {
		res = _FAIL;  /* try again */
		goto exit;
	}
	psetkeyparm = kzalloc(sizeof(struct setkey_parm), GFP_KERNEL);
	if (!psetkeyparm) {
		kfree(pcmd);
		res = _FAIL;
		goto exit;
	}

	if (psecuritypriv->dot11AuthAlgrthm == dot11AuthAlgrthm_8021X) {
		psetkeyparm->algorithm = (unsigned char)
			psecuritypriv->dot118021XGrpPrivacy;
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
			 ("\n rtw_set_key23a: psetkeyparm->algorithm = "
			  "(unsigned char)psecuritypriv->dot118021XGrpPrivacy "
			  "=%d\n", psetkeyparm->algorithm));
	} else {
		psetkeyparm->algorithm = (u8)psecuritypriv->dot11PrivacyAlgrthm;
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
			 ("\n rtw_set_key23a: psetkeyparm->algorithm = (u8)"
			  "psecuritypriv->dot11PrivacyAlgrthm =%d\n",
			  psetkeyparm->algorithm));
	}
	psetkeyparm->keyid = keyid;/* 0~3 */
	psetkeyparm->set_tx = set_tx;
	if (is_wep_enc(psetkeyparm->algorithm))
		pmlmepriv->key_mask |= BIT(psetkeyparm->keyid);

	DBG_8723A("==> rtw_set_key23a algorithm(%x), keyid(%x), key_mask(%x)\n",
		  psetkeyparm->algorithm, psetkeyparm->keyid,
		  pmlmepriv->key_mask);
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
		 ("\n rtw_set_key23a: psetkeyparm->algorithm =%d psetkeyparm->"
		  "keyid = (u8)keyid =%d\n", psetkeyparm->algorithm, keyid));

	switch (psetkeyparm->algorithm) {
	case WLAN_CIPHER_SUITE_WEP40:
		keylen = 5;
		memcpy(&psetkeyparm->key[0],
		       &psecuritypriv->wep_key[keyid].key, keylen);
		break;
	case WLAN_CIPHER_SUITE_WEP104:
		keylen = 13;
		memcpy(&psetkeyparm->key[0],
		       &psecuritypriv->wep_key[keyid].key, keylen);
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		keylen = 16;
		memcpy(&psetkeyparm->key,
		       &psecuritypriv->dot118021XGrpKey[keyid], keylen);
		psetkeyparm->grpkey = 1;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		keylen = 16;
		memcpy(&psetkeyparm->key,
		       &psecuritypriv->dot118021XGrpKey[keyid], keylen);
		psetkeyparm->grpkey = 1;
		break;
	default:
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
			 ("\n rtw_set_key23a:psecuritypriv->dot11PrivacyAlgrthm"
			  " = %x (must be 1 or 2 or 4 or 5)\n",
			  psecuritypriv->dot11PrivacyAlgrthm));
		res = _FAIL;
		kfree(pcmd);
		kfree(psetkeyparm);
		goto exit;
	}

	pcmd->cmdcode = _SetKey_CMD_;
	pcmd->parmbuf = (u8 *)psetkeyparm;
	pcmd->cmdsz =  (sizeof(struct setkey_parm));
	pcmd->rsp = NULL;
	pcmd->rspsz = 0;

	/* sema_init(&pcmd->cmd_sem, 0); */

	res = rtw_enqueue_cmd23a(pcmdpriv, pcmd);

exit:

	return res;
}

/* adjust IEs for rtw_joinbss_cmd23a in WMM */
int rtw_restruct_wmm_ie23a(struct rtw_adapter *adapter, u8 *in_ie,
			u8 *out_ie, uint in_len, uint initial_out_len)
{
	unsigned int ielength = 0;
	unsigned int i, j;

	i = 12; /* after the fixed IE */
	while (i < in_len) {
		ielength = initial_out_len;

		/* WMM element ID and OUI */
		if (in_ie[i] == 0xDD && in_ie[i + 2] == 0x00 &&
		    in_ie[i + 3] == 0x50 && in_ie[i + 4] == 0xF2 &&
		    in_ie[i + 5] == 0x02 && i+5 < in_len) {

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

		i += (in_ie[i + 1] + 2); /*  to the next IE element */
	}

	return ielength;
}

/*  */
/*  Ported from 8185: IsInPreAuthKeyList().
    (Renamed from SecIsInPreAuthKeyList(), 2006-10-13.) */
/*  Added by Annie, 2006-05-07. */
/*  */
/*  Search by BSSID, */
/*  Return Value: */
/*		-1	:if there is no pre-auth key in the  table */
/*		>= 0	:if there is pre-auth key, and   return the entry id */
/*  */
/*  */

static int SecIsInPMKIDList(struct rtw_adapter *Adapter, u8 *bssid)
{
	struct security_priv *psecuritypriv = &Adapter->securitypriv;
	int i = 0;

	do {
		if (psecuritypriv->PMKIDList[i].bUsed &&
                    ether_addr_equal(psecuritypriv->PMKIDList[i].Bssid, bssid)) {
			break;
		} else {
			i++;
			/* continue; */
		}
	} while (i < NUM_PMKID_CACHE);

	if (i == NUM_PMKID_CACHE)
		i = -1;/*  Could not find. */
	else {
		/*  There is one Pre-Authentication Key for
		    the specific BSSID. */
	}

	return i;
}

/*  */
/*  Check the RSN IE length */
/*  If the RSN IE length <= 20, the RSN IE didn't include
    the PMKID information */
/*  0-11th element in the array are the fixed IE */
/*  12th element in the array is the IE */
/*  13th element in the array is the IE length */
/*  */

static int rtw_append_pmkid(struct rtw_adapter *Adapter, int iEntry,
			    u8 *ie, uint ie_len)
{
	struct security_priv *psecuritypriv = &Adapter->securitypriv;

	if (ie[13] <= 20) {
		/*  The RSN IE didn't include the PMK ID,
		    append the PMK information */
			ie[ie_len] = 1;
			ie_len++;
			ie[ie_len] = 0;	/* PMKID count = 0x0100 */
			ie_len++;
			memcpy(&ie[ie_len],
			       &psecuritypriv->PMKIDList[iEntry].PMKID, 16);

			ie_len += 16;
			ie[13] += 18;/* PMKID length = 2+16 */
	}
	return ie_len;
}

int rtw_restruct_sec_ie23a(struct rtw_adapter *adapter, u8 *in_ie, u8 *out_ie,
			   uint in_len)
{
	u8 authmode;
	uint ielength;
	int iEntry;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct security_priv *psecuritypriv = &adapter->securitypriv;
	uint ndisauthmode = psecuritypriv->ndisauthtype;
	uint ndissecuritytype = psecuritypriv->ndisencryptstatus;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_,
		 ("+rtw_restruct_sec_ie23a: ndisauthmode=%d "
		  "ndissecuritytype=%d\n", ndisauthmode, ndissecuritytype));

	/* copy fixed ie only */
	memcpy(out_ie, in_ie, 12);
	ielength = 12;
	if (ndisauthmode == Ndis802_11AuthModeWPA ||
	    ndisauthmode == Ndis802_11AuthModeWPAPSK)
		authmode = WLAN_EID_VENDOR_SPECIFIC;
	if (ndisauthmode == Ndis802_11AuthModeWPA2 ||
	    ndisauthmode == Ndis802_11AuthModeWPA2PSK)
		authmode = WLAN_EID_RSN;

	if (check_fwstate(pmlmepriv, WIFI_UNDER_WPS)) {
		memcpy(out_ie + ielength, psecuritypriv->wps_ie,
		       psecuritypriv->wps_ie_len);

		ielength += psecuritypriv->wps_ie_len;
	} else if (authmode == WLAN_EID_VENDOR_SPECIFIC ||
		   authmode == WLAN_EID_RSN) {
		/* copy RSN or SSN */
		memcpy(&out_ie[ielength], &psecuritypriv->supplicant_ie[0],
		       psecuritypriv->supplicant_ie[1] + 2);
		ielength += psecuritypriv->supplicant_ie[1] + 2;
	}

	iEntry = SecIsInPMKIDList(adapter, pmlmepriv->assoc_bssid);
	if (iEntry < 0)
		return ielength;
	else {
		if (authmode == WLAN_EID_RSN)
			ielength = rtw_append_pmkid(adapter, iEntry,
						    out_ie, ielength);
	}

	return ielength;
}

void rtw_init_registrypriv_dev_network23a(struct rtw_adapter* adapter)
{
	struct registry_priv* pregistrypriv = &adapter->registrypriv;
	struct eeprom_priv* peepriv = &adapter->eeprompriv;
	struct wlan_bssid_ex    *pdev_network = &pregistrypriv->dev_network;
	u8 *myhwaddr = myid(peepriv);

	ether_addr_copy(pdev_network->MacAddress, myhwaddr);

	memcpy(&pdev_network->Ssid, &pregistrypriv->ssid,
	       sizeof(struct cfg80211_ssid));

	pdev_network->beacon_interval = 100;
}

void rtw_update_registrypriv_dev_network23a(struct rtw_adapter* adapter)
{
	int sz = 0;
	struct registry_priv* pregistrypriv = &adapter->registrypriv;
	struct wlan_bssid_ex *pdev_network = &pregistrypriv->dev_network;
	struct security_priv *psecuritypriv = &adapter->securitypriv;
	struct wlan_network *cur_network = &adapter->mlmepriv.cur_network;
	/* struct	xmit_priv	*pxmitpriv = &adapter->xmitpriv; */

	pdev_network->Privacy =
		(psecuritypriv->dot11PrivacyAlgrthm > 0 ? 1 : 0);

	pdev_network->Rssi = 0;

	pdev_network->DSConfig = pregistrypriv->channel;
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
		 ("pregistrypriv->channel =%d, pdev_network->DSConfig = 0x%x\n",
		  pregistrypriv->channel, pdev_network->DSConfig));

	if (cur_network->network.ifmode == NL80211_IFTYPE_ADHOC)
		pdev_network->ATIMWindow = 0;

	pdev_network->ifmode = cur_network->network.ifmode;

	/*  1. Supported rates */
	/*  2. IE */

	sz = rtw_generate_ie23a(pregistrypriv);

	pdev_network->IELength = sz;

	pdev_network->Length =
		get_wlan_bssid_ex_sz(pdev_network);

	/* notes: translate IELength & Length after assign the
	   Length to cmdsz in createbss_cmd(); */
	/* pdev_network->IELength = cpu_to_le32(sz); */
}

/* the fucntion is at passive_level */
void rtw_joinbss_reset23a(struct rtw_adapter *padapter)
{
	u8 threshold;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct ht_priv *phtpriv = &pmlmepriv->htpriv;

	/* todo: if you want to do something io/reg/hw setting
	   before join_bss, please add code here */

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
	} else
		threshold = 1;

	rtl8723a_set_rxdma_agg_pg_th(padapter, threshold);
}

/* the fucntion is >= passive_level */
bool rtw_restructure_ht_ie23a(struct rtw_adapter *padapter, u8 *in_ie,
			      u8 *out_ie, uint in_len, uint *pout_len)
{
	u32 out_len;
	int max_rx_ampdu_factor;
	unsigned char *pframe;
	const u8 *p;
	struct ieee80211_ht_cap ht_capie;
	u8 WMM_IE[7] = {0x00, 0x50, 0xf2, 0x02, 0x00, 0x01, 0x00};
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct ht_priv *phtpriv = &pmlmepriv->htpriv;

	phtpriv->ht_option = false;

	p = cfg80211_find_ie(WLAN_EID_HT_CAPABILITY, in_ie + 12, in_len -12);

	if (p && p[1] > 0) {
		u32 rx_packet_offset, max_recvbuf_sz;
		if (pmlmepriv->qos_option == 0) {
			out_len = *pout_len;
			pframe = rtw_set_ie23a(out_ie + out_len,
					       WLAN_EID_VENDOR_SPECIFIC,
					       sizeof(WMM_IE), WMM_IE,
					       pout_len);

			pmlmepriv->qos_option = 1;
		}

		out_len = *pout_len;

		memset(&ht_capie, 0, sizeof(struct ieee80211_ht_cap));

		ht_capie.cap_info = IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
			IEEE80211_HT_CAP_SGI_20 | IEEE80211_HT_CAP_SGI_40 |
			IEEE80211_HT_CAP_TX_STBC | IEEE80211_HT_CAP_DSSSCCK40;

		GetHalDefVar8192CUsb(padapter, HAL_DEF_RX_PACKET_OFFSET,
				     &rx_packet_offset);
		GetHalDefVar8192CUsb(padapter, HAL_DEF_MAX_RECVBUF_SZ,
				     &max_recvbuf_sz);

		GetHalDefVar8192CUsb(padapter, HW_VAR_MAX_RX_AMPDU_FACTOR,
				     &max_rx_ampdu_factor);
		ht_capie.ampdu_params_info = max_rx_ampdu_factor & 0x03;

		if (padapter->securitypriv.dot11PrivacyAlgrthm ==
		    WLAN_CIPHER_SUITE_CCMP)
			ht_capie.ampdu_params_info |=
				(IEEE80211_HT_AMPDU_PARM_DENSITY& (0x07 << 2));
		else
			ht_capie.ampdu_params_info |=
				(IEEE80211_HT_AMPDU_PARM_DENSITY & 0x00);

		pframe = rtw_set_ie23a(out_ie + out_len, WLAN_EID_HT_CAPABILITY,
				    sizeof(struct ieee80211_ht_cap),
				    (unsigned char*)&ht_capie, pout_len);

		phtpriv->ht_option = true;

		p = cfg80211_find_ie(WLAN_EID_HT_OPERATION, in_ie + 12,
				     in_len -12);
		if (p && (p[1] == sizeof(struct ieee80211_ht_operation))) {
			out_len = *pout_len;
			pframe = rtw_set_ie23a(out_ie + out_len,
					       WLAN_EID_HT_OPERATION,
					       p[1], p + 2 , pout_len);
		}
	}

	return phtpriv->ht_option;
}

/* the fucntion is > passive_level (in critical_section) */
void rtw_update_ht_cap23a(struct rtw_adapter *padapter, u8 *pie, uint ie_len)
{
	u8 max_ampdu_sz;
	const u8 *p;
	struct ieee80211_ht_cap *pht_capie;
	struct ieee80211_ht_operation *pht_addtinfo;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct ht_priv *phtpriv = &pmlmepriv->htpriv;
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	int bcn_fixed_size;

	if (!phtpriv->ht_option)
		return;

	if ((!pmlmeinfo->HT_info_enable) || (!pmlmeinfo->HT_caps_enable))
		return;

	DBG_8723A("+rtw_update_ht_cap23a()\n");

	bcn_fixed_size = offsetof(struct ieee80211_mgmt, u.beacon.variable) -
		offsetof(struct ieee80211_mgmt, u.beacon);

	/* Adjust pie + ie_len for our searches */
	pie += bcn_fixed_size;
	ie_len -= bcn_fixed_size;

	/* maybe needs check if ap supports rx ampdu. */
	if (!phtpriv->ampdu_enable && pregistrypriv->ampdu_enable == 1) {
		if (pregistrypriv->wifi_spec == 1)
			phtpriv->ampdu_enable = false;
		else
			phtpriv->ampdu_enable = true;
	} else if (pregistrypriv->ampdu_enable == 2)
		phtpriv->ampdu_enable = true;

	/* check Max Rx A-MPDU Size */
	p = cfg80211_find_ie(WLAN_EID_HT_CAPABILITY, pie, ie_len);

	if (p && p[1] > 0) {
		pht_capie = (struct ieee80211_ht_cap *)(p + 2);
		max_ampdu_sz = pht_capie->ampdu_params_info &
			IEEE80211_HT_AMPDU_PARM_FACTOR;
		/*  max_ampdu_sz (kbytes); */
		max_ampdu_sz = 1 << (max_ampdu_sz + 3);

		phtpriv->rx_ampdu_maxlen = max_ampdu_sz;
	}

	p = cfg80211_find_ie(WLAN_EID_HT_OPERATION, pie, ie_len);
	if (p && p[1] > 0) {
		pht_addtinfo = (struct ieee80211_ht_operation *)(p + 2);
		/* todo: */
	}

	/* update cur_bwmode & cur_ch_offset */
	if (pregistrypriv->cbw40_enable &&
	    pmlmeinfo->ht_cap.cap_info &
	    cpu_to_le16(IEEE80211_HT_CAP_SUP_WIDTH_20_40) &&
	    pmlmeinfo->HT_info.ht_param & IEEE80211_HT_PARAM_CHAN_WIDTH_ANY) {
		int i;
		u8 rf_type;

		rf_type = rtl8723a_get_rf_type(padapter);

		/* update the MCS rates */
		for (i = 0; i < IEEE80211_HT_MCS_MASK_LEN; i++) {
			if (rf_type == RF_1T1R || rf_type == RF_1T2R)
				pmlmeinfo->ht_cap.mcs.rx_mask[i] &=
					MCS_rate_1R23A[i];
			else
				pmlmeinfo->ht_cap.mcs.rx_mask[i] &=
					MCS_rate_2R23A[i];
		}
		/* switch to the 40M Hz mode accoring to the AP */
		pmlmeext->cur_bwmode = HT_CHANNEL_WIDTH_40;
		switch (pmlmeinfo->HT_info.ht_param &
			IEEE80211_HT_PARAM_CHA_SEC_OFFSET) {
		case IEEE80211_HT_PARAM_CHA_SEC_ABOVE:
			pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_LOWER;
			break;

		case IEEE80211_HT_PARAM_CHA_SEC_BELOW:
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
		(le16_to_cpu(pmlmeinfo->ht_cap.cap_info) &
		 IEEE80211_HT_CAP_SM_PS) >> IEEE80211_HT_CAP_SM_PS_SHIFT;
	if (pmlmeinfo->SM_PS == WLAN_HT_CAP_SM_PS_STATIC)
		DBG_8723A("%s(): WLAN_HT_CAP_SM_PS_STATIC\n", __func__);

	/*  */
	/*  Config current HT Protection mode. */
	/*  */
	pmlmeinfo->HT_protection =
		le16_to_cpu(pmlmeinfo->HT_info.operation_mode) &
		IEEE80211_HT_OP_MODE_PROTECTION;
}

void rtw_issue_addbareq_cmd23a(struct rtw_adapter *padapter,
			       struct xmit_frame *pxmitframe)
{
	u8 issued;
	int priority;
	struct sta_info *psta;
	struct ht_priv	*phtpriv;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	s32 bmcst = is_multicast_ether_addr(pattrib->ra);

	if (bmcst || padapter->mlmepriv.LinkDetectInfo.NumTxOkInPeriod < 100)
		return;

	priority = pattrib->priority;

	if (pattrib->psta)
		psta = pattrib->psta;
	else {
		DBG_8723A("%s, call rtw_get_stainfo23a()\n", __func__);
		psta = rtw_get_stainfo23a(&padapter->stapriv, pattrib->ra);
	}

	if (!psta) {
		DBG_8723A("%s, psta == NUL\n", __func__);
		return;
	}

	if (!(psta->state &_FW_LINKED)) {
		DBG_8723A("%s, psta->state(0x%x) != _FW_LINKED\n",
			  __func__, psta->state);
		return;
	}

	phtpriv = &psta->htpriv;

	if (phtpriv->ht_option && phtpriv->ampdu_enable) {
		issued = (phtpriv->agg_enable_bitmap>>priority)&0x1;
		issued |= (phtpriv->candidate_tid_bitmap>>priority)&0x1;

		if (issued == 0) {
			DBG_8723A("rtw_issue_addbareq_cmd23a, p =%d\n",
				  priority);
			psta->htpriv.candidate_tid_bitmap |= BIT(priority);
			rtw_addbareq_cmd23a(padapter, (u8) priority,
					    pattrib->ra);
		}
	}
}

int rtw_linked_check(struct rtw_adapter *padapter)
{
	if (check_fwstate(&padapter->mlmepriv, WIFI_AP_STATE) ||
	    check_fwstate(&padapter->mlmepriv,
			  WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE)) {
		if (padapter->stapriv.asoc_sta_count > 2)
			return true;
	} else {	/* Station mode */
		if (check_fwstate(&padapter->mlmepriv, _FW_LINKED))
			return true;
	}
	return false;
}
