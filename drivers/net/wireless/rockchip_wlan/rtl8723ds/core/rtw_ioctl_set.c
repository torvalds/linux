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
#define _RTW_IOCTL_SET_C_

#include <drv_types.h>
#include <hal_data.h>


extern void indicate_wx_scan_complete_event(_adapter *padapter);

#define IS_MAC_ADDRESS_BROADCAST(addr) \
	(\
	 ((addr[0] == 0xff) && (addr[1] == 0xff) && \
	  (addr[2] == 0xff) && (addr[3] == 0xff) && \
	  (addr[4] == 0xff) && (addr[5] == 0xff)) ? _TRUE : _FALSE \
	)

u8 rtw_validate_bssid(u8 *bssid)
{
	u8 ret = _TRUE;

	if (is_zero_mac_addr(bssid)
	    || is_broadcast_mac_addr(bssid)
	    || is_multicast_mac_addr(bssid)
	   )
		ret = _FALSE;

	return ret;
}

u8 rtw_validate_ssid(NDIS_802_11_SSID *ssid)
{
#ifdef CONFIG_VALIDATE_SSID
	u8	 i;
#endif
	u8	ret = _TRUE;


	if (ssid->SsidLength > 32) {
		ret = _FALSE;
		goto exit;
	}

#ifdef CONFIG_VALIDATE_SSID
	for (i = 0; i < ssid->SsidLength; i++) {
		/* wifi, printable ascii code must be supported */
		if (!((ssid->Ssid[i] >= 0x20) && (ssid->Ssid[i] <= 0x7e))) {
			ret = _FALSE;
			break;
		}
	}
#endif /* CONFIG_VALIDATE_SSID */

exit:


	return ret;
}

u8 rtw_do_join(_adapter *padapter);
u8 rtw_do_join(_adapter *padapter)
{
	_irqL	irqL;
	_list	*plist, *phead;
	u8 *pibss = NULL;
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct sitesurvey_parm parm;
	_queue	*queue	= &(pmlmepriv->scanned_queue);
	u8 ret = _SUCCESS;


	_enter_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
	phead = get_list_head(queue);
	plist = get_next(phead);


	pmlmepriv->cur_network.join_res = -2;

	set_fwstate(pmlmepriv, WIFI_UNDER_LINKING);

	pmlmepriv->pscanned = plist;

	pmlmepriv->to_join = _TRUE;

	rtw_init_sitesurvey_parm(padapter, &parm);
	_rtw_memcpy(&parm.ssid[0], &pmlmepriv->assoc_ssid, sizeof(NDIS_802_11_SSID));
	parm.ssid_num = 1;

	if (pmlmepriv->assoc_ch) {
		parm.ch_num = 1;
		parm.ch[0].hw_value = pmlmepriv->assoc_ch;
		parm.ch[0].flags = 0;
	}

	if (_rtw_queue_empty(queue) == _TRUE) {
		_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
		_clr_fwstate_(pmlmepriv, WIFI_UNDER_LINKING);

		/* when set_ssid/set_bssid for rtw_do_join(), but scanning queue is empty */
		/* we try to issue sitesurvey firstly	 */

		if (pmlmepriv->LinkDetectInfo.bBusyTraffic == _FALSE
		    || rtw_to_roam(padapter) > 0
		   ) {
			u8 ssc_chk = rtw_sitesurvey_condition_check(padapter, _FALSE);

			if ((ssc_chk == SS_ALLOW) || (ssc_chk == SS_DENY_BUSY_TRAFFIC) ){
				/* submit site_survey_cmd */
				ret = rtw_sitesurvey_cmd(padapter, &parm);
				if (_SUCCESS != ret)
					pmlmepriv->to_join = _FALSE;
			} else {
				/*if (ssc_chk == SS_DENY_BUDDY_UNDER_SURVEY)*/
				pmlmepriv->to_join = _FALSE;
				ret = _FAIL;
			}
		} else {
			pmlmepriv->to_join = _FALSE;
			ret = _FAIL;
		}

		goto exit;
	} else {
		int select_ret;
		_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
		select_ret = rtw_select_and_join_from_scanned_queue(pmlmepriv);
		if (select_ret == _SUCCESS) {
			pmlmepriv->to_join = _FALSE;
			_set_timer(&pmlmepriv->assoc_timer, MAX_JOIN_TIMEOUT);
		} else {
			if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE) {
				/* submit createbss_cmd to change to a ADHOC_MASTER */

				/* pmlmepriv->lock has been acquired by caller... */
				WLAN_BSSID_EX    *pdev_network = &(padapter->registrypriv.dev_network);

				/*pmlmepriv->fw_state = WIFI_ADHOC_MASTER_STATE;*/
				init_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE);

				pibss = padapter->registrypriv.dev_network.MacAddress;

				_rtw_memset(&pdev_network->Ssid, 0, sizeof(NDIS_802_11_SSID));
				_rtw_memcpy(&pdev_network->Ssid, &pmlmepriv->assoc_ssid, sizeof(NDIS_802_11_SSID));

				rtw_update_registrypriv_dev_network(padapter);

				rtw_generate_random_ibss(pibss);

				if (rtw_create_ibss_cmd(padapter, 0) != _SUCCESS) {
					ret =  _FALSE;
					goto exit;
				}

				pmlmepriv->to_join = _FALSE;


			} else {
				/* can't associate ; reset under-linking			 */
				_clr_fwstate_(pmlmepriv, WIFI_UNDER_LINKING);

				/* when set_ssid/set_bssid for rtw_do_join(), but there are no desired bss in scanning queue */
				/* we try to issue sitesurvey firstly			 */
				if (pmlmepriv->LinkDetectInfo.bBusyTraffic == _FALSE
				    || rtw_to_roam(padapter) > 0
				   ) {
					u8 ssc_chk = rtw_sitesurvey_condition_check(padapter, _FALSE);

					if ((ssc_chk == SS_ALLOW) || (ssc_chk == SS_DENY_BUSY_TRAFFIC)){
						/* RTW_INFO(("rtw_do_join() when   no desired bss in scanning queue\n"); */
						ret = rtw_sitesurvey_cmd(padapter, &parm);
						if (_SUCCESS != ret)
							pmlmepriv->to_join = _FALSE;
					} else {
						/*if (ssc_chk == SS_DENY_BUDDY_UNDER_SURVEY) {
						} else {*/
						ret = _FAIL;
						pmlmepriv->to_join = _FALSE;
					}
				} else {
					ret = _FAIL;
					pmlmepriv->to_join = _FALSE;
				}
			}

		}

	}

exit:

	return ret;
}

u8 rtw_set_802_11_bssid(_adapter *padapter, u8 *bssid)
{
	_irqL irqL;
	u8 status = _SUCCESS;

	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;


	RTW_PRINT("set bssid:%pM\n", bssid);

	if ((bssid[0] == 0x00 && bssid[1] == 0x00 && bssid[2] == 0x00 && bssid[3] == 0x00 && bssid[4] == 0x00 && bssid[5] == 0x00) ||
	    (bssid[0] == 0xFF && bssid[1] == 0xFF && bssid[2] == 0xFF && bssid[3] == 0xFF && bssid[4] == 0xFF && bssid[5] == 0xFF)) {
		status = _FAIL;
		goto exit;
	}

	_enter_critical_bh(&pmlmepriv->lock, &irqL);


	RTW_INFO("Set BSSID under fw_state=0x%08x\n", get_fwstate(pmlmepriv));
	if (check_fwstate(pmlmepriv, WIFI_UNDER_SURVEY) == _TRUE)
		goto handle_tkip_countermeasure;
	else if (check_fwstate(pmlmepriv, WIFI_UNDER_LINKING) == _TRUE)
		goto release_mlme_lock;

	if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE | WIFI_ADHOC_MASTER_STATE) == _TRUE) {

		if (_rtw_memcmp(&pmlmepriv->cur_network.network.MacAddress, bssid, ETH_ALEN) == _TRUE) {
			if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _FALSE)
				goto release_mlme_lock;/* it means driver is in WIFI_ADHOC_MASTER_STATE, we needn't create bss again. */
		} else {

			rtw_disassoc_cmd(padapter, 0, 0);

			if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == _TRUE)
				rtw_indicate_disconnect(padapter, 0, _FALSE);

			rtw_free_assoc_resources_cmd(padapter, _TRUE, 0);

			if ((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == _TRUE)) {
				_clr_fwstate_(pmlmepriv, WIFI_ADHOC_MASTER_STATE);
				set_fwstate(pmlmepriv, WIFI_ADHOC_STATE);
			}
		}
	}

handle_tkip_countermeasure:
	if (rtw_handle_tkip_countermeasure(padapter, __func__) == _FAIL) {
		status = _FAIL;
		goto release_mlme_lock;
	}

	_rtw_memset(&pmlmepriv->assoc_ssid, 0, sizeof(NDIS_802_11_SSID));
	_rtw_memcpy(&pmlmepriv->assoc_bssid, bssid, ETH_ALEN);
	pmlmepriv->assoc_ch = 0;
	pmlmepriv->assoc_by_bssid = _TRUE;

	if (check_fwstate(pmlmepriv, WIFI_UNDER_SURVEY) == _TRUE)
		pmlmepriv->to_join = _TRUE;
	else
		status = rtw_do_join(padapter);

release_mlme_lock:
	_exit_critical_bh(&pmlmepriv->lock, &irqL);

exit:


	return status;
}

u8 rtw_set_802_11_ssid(_adapter *padapter, NDIS_802_11_SSID *ssid)
{
	_irqL irqL;
	u8 status = _SUCCESS;

	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_network *pnetwork = &pmlmepriv->cur_network;


	RTW_PRINT("set ssid [%s] fw_state=0x%08x\n",
		  ssid->Ssid, get_fwstate(pmlmepriv));

	if (!rtw_is_hw_init_completed(padapter)) {
		status = _FAIL;
		goto exit;
	}

	_enter_critical_bh(&pmlmepriv->lock, &irqL);

	RTW_INFO("Set SSID under fw_state=0x%08x\n", get_fwstate(pmlmepriv));
	if (check_fwstate(pmlmepriv, WIFI_UNDER_SURVEY) == _TRUE)
		goto handle_tkip_countermeasure;
	else if (check_fwstate(pmlmepriv, WIFI_UNDER_LINKING) == _TRUE)
		goto release_mlme_lock;

	if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE | WIFI_ADHOC_MASTER_STATE) == _TRUE) {

		if ((pmlmepriv->assoc_ssid.SsidLength == ssid->SsidLength) &&
		    (_rtw_memcmp(&pmlmepriv->assoc_ssid.Ssid, ssid->Ssid, ssid->SsidLength) == _TRUE)) {
			if ((check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _FALSE)) {

				if (rtw_is_same_ibss(padapter, pnetwork) == _FALSE) {
					/* if in WIFI_ADHOC_MASTER_STATE | WIFI_ADHOC_STATE, create bss or rejoin again */
					rtw_disassoc_cmd(padapter, 0, 0);

					if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == _TRUE)
						rtw_indicate_disconnect(padapter, 0, _FALSE);

					rtw_free_assoc_resources_cmd(padapter, _TRUE, 0);

					if (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == _TRUE) {
						_clr_fwstate_(pmlmepriv, WIFI_ADHOC_MASTER_STATE);
						set_fwstate(pmlmepriv, WIFI_ADHOC_STATE);
					}
				} else {
					goto release_mlme_lock;/* it means driver is in WIFI_ADHOC_MASTER_STATE, we needn't create bss again. */
				}
			}
#ifdef CONFIG_LPS
			else
				rtw_lps_ctrl_wk_cmd(padapter, LPS_CTRL_JOINBSS, 0);
#endif
		} else {

			rtw_disassoc_cmd(padapter, 0, 0);

			if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == _TRUE)
				rtw_indicate_disconnect(padapter, 0, _FALSE);

			rtw_free_assoc_resources_cmd(padapter, _TRUE, 0);

			if (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == _TRUE) {
				_clr_fwstate_(pmlmepriv, WIFI_ADHOC_MASTER_STATE);
				set_fwstate(pmlmepriv, WIFI_ADHOC_STATE);
			}
		}
	}

handle_tkip_countermeasure:
	if (rtw_handle_tkip_countermeasure(padapter, __func__) == _FAIL) {
		status = _FAIL;
		goto release_mlme_lock;
	}

	if (rtw_validate_ssid(ssid) == _FALSE) {
		status = _FAIL;
		goto release_mlme_lock;
	}

	_rtw_memcpy(&pmlmepriv->assoc_ssid, ssid, sizeof(NDIS_802_11_SSID));
	pmlmepriv->assoc_ch = 0;
	pmlmepriv->assoc_by_bssid = _FALSE;

	if (check_fwstate(pmlmepriv, WIFI_UNDER_SURVEY) == _TRUE)
		pmlmepriv->to_join = _TRUE;
	else
		status = rtw_do_join(padapter);

release_mlme_lock:
	_exit_critical_bh(&pmlmepriv->lock, &irqL);

exit:


	return status;

}

u8 rtw_set_802_11_connect(_adapter *padapter,
			  u8 *bssid, NDIS_802_11_SSID *ssid, u16 ch)
{
	_irqL irqL;
	u8 status = _SUCCESS;
	bool bssid_valid = _TRUE;
	bool ssid_valid = _TRUE;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;


	if (!ssid || rtw_validate_ssid(ssid) == _FALSE)
		ssid_valid = _FALSE;

	if (!bssid || rtw_validate_bssid(bssid) == _FALSE)
		bssid_valid = _FALSE;

	if (ssid_valid == _FALSE && bssid_valid == _FALSE) {
		RTW_INFO(FUNC_ADPT_FMT" ssid:%p, ssid_valid:%d, bssid:%p, bssid_valid:%d\n",
			FUNC_ADPT_ARG(padapter), ssid, ssid_valid, bssid, bssid_valid);
		status = _FAIL;
		goto exit;
	}

	if (!rtw_is_hw_init_completed(padapter)) {
		status = _FAIL;
		goto exit;
	}

	_enter_critical_bh(&pmlmepriv->lock, &irqL);

	RTW_PRINT(FUNC_ADPT_FMT"  fw_state=0x%08x\n",
		  FUNC_ADPT_ARG(padapter), get_fwstate(pmlmepriv));

	if (check_fwstate(pmlmepriv, WIFI_UNDER_SURVEY) == _TRUE)
		goto handle_tkip_countermeasure;
	else if (check_fwstate(pmlmepriv, WIFI_UNDER_LINKING) == _TRUE)
		goto release_mlme_lock;

handle_tkip_countermeasure:
	if (rtw_handle_tkip_countermeasure(padapter, __func__) == _FAIL) {
		status = _FAIL;
		goto release_mlme_lock;
	}

	if (ssid && ssid_valid)
		_rtw_memcpy(&pmlmepriv->assoc_ssid, ssid, sizeof(NDIS_802_11_SSID));
	else
		_rtw_memset(&pmlmepriv->assoc_ssid, 0, sizeof(NDIS_802_11_SSID));

	if (bssid && bssid_valid) {
		_rtw_memcpy(&pmlmepriv->assoc_bssid, bssid, ETH_ALEN);
		pmlmepriv->assoc_by_bssid = _TRUE;
	} else
		pmlmepriv->assoc_by_bssid = _FALSE;

	pmlmepriv->assoc_ch = ch;

	if (check_fwstate(pmlmepriv, WIFI_UNDER_SURVEY) == _TRUE)
		pmlmepriv->to_join = _TRUE;
	else
		status = rtw_do_join(padapter);

release_mlme_lock:
	_exit_critical_bh(&pmlmepriv->lock, &irqL);

exit:
	return status;
}

u8 rtw_set_802_11_infrastructure_mode(_adapter *padapter,
			      NDIS_802_11_NETWORK_INFRASTRUCTURE networktype, u8 flags)
{
	_irqL irqL;
	struct	mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct	wlan_network	*cur_network = &pmlmepriv->cur_network;
	NDIS_802_11_NETWORK_INFRASTRUCTURE *pold_state = &(cur_network->network.InfrastructureMode);
	u8 ap2sta_mode = _FALSE;
	u8 ret = _TRUE;
	u8 is_linked = _FALSE, is_adhoc_master = _FALSE;

	if (*pold_state != networktype) {
		/* RTW_INFO("change mode, old_mode=%d, new_mode=%d, fw_state=0x%x\n", *pold_state, networktype, get_fwstate(pmlmepriv)); */

		if (*pold_state == Ndis802_11APMode
			|| *pold_state == Ndis802_11_mesh
		) {
			/* change to other mode from Ndis802_11APMode/Ndis802_11_mesh */
			cur_network->join_res = -1;
			ap2sta_mode = _TRUE;
#ifdef CONFIG_NATIVEAP_MLME
			stop_ap_mode(padapter);
#endif
		}

		_enter_critical_bh(&pmlmepriv->lock, &irqL);
		is_linked = check_fwstate(pmlmepriv, WIFI_ASOC_STATE);
		is_adhoc_master = check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE);

		/* flags = 0, means enqueue cmd and no wait */
		if (flags != 0)
			_exit_critical_bh(&pmlmepriv->lock, &irqL);

		if ((is_linked == _TRUE) || (*pold_state == Ndis802_11IBSS))
			rtw_disassoc_cmd(padapter, 0, flags);

		if ((is_linked == _TRUE) ||
		    (is_adhoc_master == _TRUE))
			rtw_free_assoc_resources_cmd(padapter, _TRUE, flags);

		if ((*pold_state == Ndis802_11Infrastructure) || (*pold_state == Ndis802_11IBSS)) {
			if (is_linked == _TRUE) {
				rtw_indicate_disconnect(padapter, 0, _FALSE); /*will clr Linked_state; before this function, we must have checked whether issue dis-assoc_cmd or not*/
			}
		}

		/* flags = 0, means enqueue cmd and no wait */
		if (flags != 0)
			_enter_critical_bh(&pmlmepriv->lock, &irqL);

		*pold_state = networktype;

		_clr_fwstate_(pmlmepriv, ~WIFI_NULL_STATE);

		switch (networktype) {
		case Ndis802_11IBSS:
			set_fwstate(pmlmepriv, WIFI_ADHOC_STATE);
			break;

		case Ndis802_11Infrastructure:
			set_fwstate(pmlmepriv, WIFI_STATION_STATE);

			if (ap2sta_mode)
				rtw_init_bcmc_stainfo(padapter);
			break;

		case Ndis802_11APMode:
			set_fwstate(pmlmepriv, WIFI_AP_STATE);
#ifdef CONFIG_NATIVEAP_MLME
			start_ap_mode(padapter);
			/* rtw_indicate_connect(padapter); */
#endif

			break;

#ifdef CONFIG_RTW_MESH
		case Ndis802_11_mesh:
			set_fwstate(pmlmepriv, WIFI_MESH_STATE);
			start_ap_mode(padapter);
			break;
#endif

		case Ndis802_11AutoUnknown:
		case Ndis802_11InfrastructureMax:
			break;
#ifdef CONFIG_WIFI_MONITOR
		case Ndis802_11Monitor:
			set_fwstate(pmlmepriv, WIFI_MONITOR_STATE);
			break;
#endif /* CONFIG_WIFI_MONITOR */
		default:
			ret = _FALSE;
			rtw_warn_on(1);
		}

		/* SecClearAllKeys(adapter); */


		_exit_critical_bh(&pmlmepriv->lock, &irqL);
	}

	return ret;
}


u8 rtw_set_802_11_disassociate(_adapter *padapter)
{
	_irqL irqL;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;


	_enter_critical_bh(&pmlmepriv->lock, &irqL);

	if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == _TRUE) {

		rtw_disassoc_cmd(padapter, 0, 0);
		rtw_indicate_disconnect(padapter, 0, _FALSE);
		/* modify for CONFIG_IEEE80211W, none 11w can use it */
		rtw_free_assoc_resources_cmd(padapter, _TRUE, 0);
		if (_FAIL == rtw_pwr_wakeup(padapter))
			RTW_INFO("%s(): rtw_pwr_wakeup fail !!!\n", __FUNCTION__);
	}

	_exit_critical_bh(&pmlmepriv->lock, &irqL);


	return _TRUE;
}

#if 1
u8 rtw_set_802_11_bssid_list_scan(_adapter *padapter, struct sitesurvey_parm *pparm)
{
	_irqL	irqL;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	u8	res = _TRUE;

	_enter_critical_bh(&pmlmepriv->lock, &irqL);
	res = rtw_sitesurvey_cmd(padapter, pparm);
	_exit_critical_bh(&pmlmepriv->lock, &irqL);

	return res;
}

#else
u8 rtw_set_802_11_bssid_list_scan(_adapter *padapter, struct sitesurvey_parm *pparm)
{
	_irqL	irqL;
	struct	mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	u8	res = _TRUE;



	if (padapter == NULL) {
		res = _FALSE;
		goto exit;
	}
	if (!rtw_is_hw_init_completed(padapter)) {
		res = _FALSE;
		goto exit;
	}

	if ((check_fwstate(pmlmepriv, WIFI_UNDER_SURVEY | WIFI_UNDER_LINKING) == _TRUE) ||
	    (pmlmepriv->LinkDetectInfo.bBusyTraffic == _TRUE)) {
		/* Scan or linking is in progress, do nothing. */
		res = _TRUE;


	} else {
		if (rtw_is_scan_deny(padapter)) {
			RTW_INFO(FUNC_ADPT_FMT": scan deny\n", FUNC_ADPT_ARG(padapter));
			indicate_wx_scan_complete_event(padapter);
			return _SUCCESS;
		}

		_enter_critical_bh(&pmlmepriv->lock, &irqL);

		res = rtw_sitesurvey_cmd(padapter, pparm);

		_exit_critical_bh(&pmlmepriv->lock, &irqL);
	}
exit:


	return res;
}
#endif

#ifdef CONFIG_RTW_ACS
u8 rtw_set_acs_sitesurvey(_adapter *adapter)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	struct sitesurvey_parm parm;
	u8 uch;
	u8 ch_num = 0;
	int i;
	BAND_TYPE band;
	u8 (*center_chs_num)(u8) = NULL;
	u8 (*center_chs)(u8, u8) = NULL;
	u8 ret = _FAIL;

	if (!rtw_mi_get_ch_setting_union(adapter, &uch, NULL, NULL))
		goto exit;

	_rtw_memset(&parm, 0, sizeof(struct sitesurvey_parm));
	parm.scan_mode = SCAN_PASSIVE;
	parm.bw = CHANNEL_WIDTH_20;
	parm.acs = 1;

	for (band = BAND_ON_2_4G; band < BAND_MAX; band++) {
		if (band == BAND_ON_2_4G) {
			center_chs_num = center_chs_2g_num;
			center_chs = center_chs_2g;
		} else
		#ifdef CONFIG_IEEE80211_BAND_5GHZ
		if (band == BAND_ON_5G) {
			center_chs_num = center_chs_5g_num;
			center_chs = center_chs_5g;
		} else
		#endif
		{
			center_chs_num = NULL;
			center_chs = NULL;
		}

		if (!center_chs_num || !center_chs)
			continue;

		if (rfctl->ch_sel_within_same_band) {
			if (rtw_is_2g_ch(uch) && band != BAND_ON_2_4G)
				continue;
			#ifdef CONFIG_IEEE80211_BAND_5GHZ
			if (rtw_is_5g_ch(uch) && band != BAND_ON_5G)
				continue;
			#endif
		}

		ch_num = center_chs_num(CHANNEL_WIDTH_20);	
		for (i = 0; i < ch_num && parm.ch_num < RTW_CHANNEL_SCAN_AMOUNT; i++) {
			parm.ch[parm.ch_num].hw_value = center_chs(CHANNEL_WIDTH_20, i);
			parm.ch[parm.ch_num].flags = RTW_IEEE80211_CHAN_PASSIVE_SCAN;
			parm.ch_num++;
		}
	}

	ret = rtw_set_802_11_bssid_list_scan(adapter, &parm);

exit:
	return ret;
}
#endif /* CONFIG_RTW_ACS */

u8 rtw_set_802_11_authentication_mode(_adapter *padapter, NDIS_802_11_AUTHENTICATION_MODE authmode)
{
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	int res;
	u8 ret;



	psecuritypriv->ndisauthtype = authmode;


	if (psecuritypriv->ndisauthtype > 3)
		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;

#ifdef CONFIG_WAPI_SUPPORT
	if (psecuritypriv->ndisauthtype == 6)
		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_WAPI;
#endif

	res = rtw_set_auth(padapter, psecuritypriv);

	if (res == _SUCCESS)
		ret = _TRUE;
	else
		ret = _FALSE;


	return ret;
}

u8 rtw_set_802_11_add_wep(_adapter *padapter, NDIS_802_11_WEP *wep)
{

	u8		bdefaultkey;
	u8		btransmitkey;
	sint		keyid, res;
	struct security_priv *psecuritypriv = &(padapter->securitypriv);
	u8		ret = _SUCCESS;


	bdefaultkey = (wep->KeyIndex & 0x40000000) > 0 ? _FALSE : _TRUE; /* for ??? */
	btransmitkey = (wep->KeyIndex & 0x80000000) > 0 ? _TRUE  : _FALSE;	/* for ??? */
	keyid = wep->KeyIndex & 0x3fffffff;

	if (keyid >= 4) {
		ret = _FALSE;
		goto exit;
	}

	switch (wep->KeyLength) {
	case 5:
		psecuritypriv->dot11PrivacyAlgrthm = _WEP40_;
		break;
	case 13:
		psecuritypriv->dot11PrivacyAlgrthm = _WEP104_;
		break;
	default:
		psecuritypriv->dot11PrivacyAlgrthm = _NO_PRIVACY_;
		break;
	}


	_rtw_memcpy(&(psecuritypriv->dot11DefKey[keyid].skey[0]), &(wep->KeyMaterial), wep->KeyLength);

	psecuritypriv->dot11DefKeylen[keyid] = wep->KeyLength;

	psecuritypriv->dot11PrivacyKeyIndex = keyid;


	res = rtw_set_key(padapter, psecuritypriv, keyid, 1, _TRUE);

	if (res == _FAIL)
		ret = _FALSE;
exit:


	return ret;

}

/*
* rtw_get_cur_max_rate -
* @adapter: pointer to _adapter structure
*
* Return 0 or 100Kbps
*/
u16 rtw_get_cur_max_rate(_adapter *adapter)
{
	int j;
	int	i = 0;
	u16	rate = 0, max_rate = 0;
	struct mlme_priv	*pmlmepriv = &adapter->mlmepriv;
	WLAN_BSSID_EX	*pcur_bss = &pmlmepriv->cur_network.network;
	int	sta_bssrate_len = 0;
	unsigned char	sta_bssrate[NumRates];
	struct sta_info *psta = NULL;
	u8	short_GI = 0;

#ifdef CONFIG_MP_INCLUDED
	if (adapter->registrypriv.mp_mode == 1) {
		if (check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE)
			return 0;
	}
#endif

	if ((check_fwstate(pmlmepriv, WIFI_ASOC_STATE) != _TRUE)
	    && (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) != _TRUE))
		return 0;

	psta = rtw_get_stainfo(&adapter->stapriv, get_bssid(pmlmepriv));
	if (psta == NULL)
		return 0;

	short_GI = query_ra_short_GI(psta, rtw_get_tx_bw_mode(adapter, psta));

#ifdef CONFIG_80211N_HT
	if (is_supported_ht(psta->wireless_mode)) {
		max_rate = rtw_ht_mcs_rate((psta->cmn.bw_mode == CHANNEL_WIDTH_40) ? 1 : 0
			, short_GI
			, psta->htpriv.ht_cap.supp_mcs_set
		);
	}
#ifdef CONFIG_80211AC_VHT
	else if (is_supported_vht(psta->wireless_mode))
		max_rate = ((rtw_vht_mcs_to_data_rate(psta->cmn.bw_mode, short_GI, pmlmepriv->vhtpriv.vht_highest_rate) + 1) >> 1) * 10;
#endif /* CONFIG_80211AC_VHT */
	else
#endif /* CONFIG_80211N_HT */
	{
		/*station mode show :station && ap support rate; softap :show ap support rate*/	
		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
			get_rate_set(adapter, sta_bssrate, &sta_bssrate_len);/*get sta rate and length*/


		while ((pcur_bss->SupportedRates[i] != 0) && (pcur_bss->SupportedRates[i] != 0xFF)) {
			rate = pcur_bss->SupportedRates[i] & 0x7F;/*AP support rates*/
			/*RTW_INFO("%s rate=%02X \n", __func__, rate);*/

			/*check STA  support rate or not */
			if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE) {
				for (j = 0; j < sta_bssrate_len; j++) {
					/* Avoid the proprietary data rate (22Mbps) of Handlink WSG-4000 AP */
					if ((rate | IEEE80211_BASIC_RATE_MASK)
					    == (sta_bssrate[j] | IEEE80211_BASIC_RATE_MASK)) {
						if (rate > max_rate) {
							max_rate = rate;
						}
						break;
					}
				}
			} else {
			
				if (rate > max_rate)
					max_rate = rate;

			}
			i++;
		}

		max_rate = max_rate * 10 / 2;
	}
	return max_rate;
}

/*
* rtw_set_scan_mode -
* @adapter: pointer to _adapter structure
* @scan_mode:
*
* Return _SUCCESS or _FAIL
*/
int rtw_set_scan_mode(_adapter *adapter, RT_SCAN_TYPE scan_mode)
{
	if (scan_mode != SCAN_ACTIVE && scan_mode != SCAN_PASSIVE)
		return _FAIL;

	adapter->mlmepriv.scan_mode = scan_mode;

	return _SUCCESS;
}

/*
* rtw_set_channel_plan -
* @adapter: pointer to _adapter structure
* @channel_plan:
*
* Return _SUCCESS or _FAIL
*/
int rtw_set_channel_plan(_adapter *adapter, u8 channel_plan)
{
	/* handle by cmd_thread to sync with scan operation */
	return rtw_set_chplan_cmd(adapter, RTW_CMDF_WAIT_ACK, channel_plan, 1);
}

/*
* rtw_set_country -
* @adapter: pointer to _adapter structure
* @country_code: string of country code
*
* Return _SUCCESS or _FAIL
*/
int rtw_set_country(_adapter *adapter, const char *country_code)
{
#ifdef CONFIG_RTW_IOCTL_SET_COUNTRY
	return rtw_set_country_cmd(adapter, RTW_CMDF_WAIT_ACK, country_code, 1);
#else
	RTW_INFO("%s(): not applied\n", __func__);
	return _SUCCESS;
#endif
}

/*
* rtw_set_band -
* @adapter: pointer to _adapter structure
* @band: band to set
*
* Return _SUCCESS or _FAIL
*/
int rtw_set_band(_adapter *adapter, u8 band)
{
	if (rtw_band_valid(band)) {
		RTW_INFO(FUNC_ADPT_FMT" band:%d\n", FUNC_ADPT_ARG(adapter), band);
		adapter->setband = band;
		return _SUCCESS;
	}

	RTW_PRINT(FUNC_ADPT_FMT" band:%d fail\n", FUNC_ADPT_ARG(adapter), band);
	return _FAIL;
}
