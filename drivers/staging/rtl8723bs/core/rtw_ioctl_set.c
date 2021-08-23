// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/

#include <drv_types.h>
#include <rtw_debug.h>

u8 rtw_validate_bssid(u8 *bssid)
{
	u8 ret = true;

	if (is_zero_mac_addr(bssid)
		|| is_broadcast_mac_addr(bssid)
		|| is_multicast_mac_addr(bssid)
	) {
		ret = false;
	}

	return ret;
}

u8 rtw_validate_ssid(struct ndis_802_11_ssid *ssid)
{
	u8 ret = true;

	if (ssid->ssid_length > 32) {
		ret = false;
		goto exit;
	}

exit:
	return ret;
}

u8 rtw_do_join(struct adapter *padapter)
{
	struct list_head	*plist, *phead;
	u8 *pibss = NULL;
	struct	mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct __queue	*queue	= &(pmlmepriv->scanned_queue);
	u8 ret = _SUCCESS;

	spin_lock_bh(&(pmlmepriv->scanned_queue.lock));
	phead = get_list_head(queue);
	plist = get_next(phead);

	pmlmepriv->cur_network.join_res = -2;

	set_fwstate(pmlmepriv, _FW_UNDER_LINKING);

	pmlmepriv->pscanned = plist;

	pmlmepriv->to_join = true;

	if (list_empty(&queue->queue)) {
		spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));
		_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);

		/* when set_ssid/set_bssid for rtw_do_join(), but scanning queue is empty */
		/* we try to issue sitesurvey firstly */

		if (pmlmepriv->LinkDetectInfo.bBusyTraffic == false
			|| rtw_to_roam(padapter) > 0
		) {
			/*  submit site_survey_cmd */
			ret = rtw_sitesurvey_cmd(padapter, &pmlmepriv->assoc_ssid, 1, NULL, 0);
			if (ret != _SUCCESS)
				pmlmepriv->to_join = false;

		} else {
			pmlmepriv->to_join = false;
			ret = _FAIL;
		}

		goto exit;
	} else {
		int select_ret;
		spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));
		select_ret = rtw_select_and_join_from_scanned_queue(pmlmepriv);
		if (select_ret == _SUCCESS) {
			pmlmepriv->to_join = false;
			_set_timer(&pmlmepriv->assoc_timer, MAX_JOIN_TIMEOUT);
		} else {
			if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == true) {
				/*  submit createbss_cmd to change to a ADHOC_MASTER */

				/* pmlmepriv->lock has been acquired by caller... */
				struct wlan_bssid_ex    *pdev_network = &(padapter->registrypriv.dev_network);

				pmlmepriv->fw_state = WIFI_ADHOC_MASTER_STATE;

				pibss = padapter->registrypriv.dev_network.mac_address;

				memcpy(&pdev_network->ssid, &pmlmepriv->assoc_ssid, sizeof(struct ndis_802_11_ssid));

				rtw_update_registrypriv_dev_network(padapter);

				rtw_generate_random_ibss(pibss);

				if (rtw_createbss_cmd(padapter) != _SUCCESS) {
					ret =  false;
					goto exit;
				}

				pmlmepriv->to_join = false;

			} else {
				/*  can't associate ; reset under-linking */
				_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);

				/* when set_ssid/set_bssid for rtw_do_join(), but there are no desired bss in scanning queue */
				/* we try to issue sitesurvey firstly */
				if (pmlmepriv->LinkDetectInfo.bBusyTraffic == false
					|| rtw_to_roam(padapter) > 0
				) {
					ret = rtw_sitesurvey_cmd(padapter, &pmlmepriv->assoc_ssid, 1, NULL, 0);
					if (ret != _SUCCESS)
						pmlmepriv->to_join = false;

				} else {
					ret = _FAIL;
					pmlmepriv->to_join = false;
				}
			}

		}

	}

exit:
	return ret;
}

u8 rtw_set_802_11_bssid(struct adapter *padapter, u8 *bssid)
{
	u8 status = _SUCCESS;

	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	netdev_dbg(padapter->pnetdev, "set bssid:%pM\n", bssid);

	if ((bssid[0] == 0x00 && bssid[1] == 0x00 && bssid[2] == 0x00 && bssid[3] == 0x00 && bssid[4] == 0x00 && bssid[5] == 0x00) ||
	    (bssid[0] == 0xFF && bssid[1] == 0xFF && bssid[2] == 0xFF && bssid[3] == 0xFF && bssid[4] == 0xFF && bssid[5] == 0xFF)) {
		status = _FAIL;
		goto exit;
	}

	spin_lock_bh(&pmlmepriv->lock);


	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY) == true)
		goto handle_tkip_countermeasure;
	else if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING) == true)
		goto release_mlme_lock;

	if (check_fwstate(pmlmepriv, _FW_LINKED|WIFI_ADHOC_MASTER_STATE) == true) {
		if (!memcmp(&pmlmepriv->cur_network.network.mac_address, bssid, ETH_ALEN)) {
			if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == false)
				goto release_mlme_lock;/* it means driver is in WIFI_ADHOC_MASTER_STATE, we needn't create bss again. */
		} else {
			rtw_disassoc_cmd(padapter, 0, true);

			if (check_fwstate(pmlmepriv, _FW_LINKED) == true)
				rtw_indicate_disconnect(padapter);

			rtw_free_assoc_resources(padapter, 1);

			if ((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == true)) {
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

	memset(&pmlmepriv->assoc_ssid, 0, sizeof(struct ndis_802_11_ssid));
	memcpy(&pmlmepriv->assoc_bssid, bssid, ETH_ALEN);
	pmlmepriv->assoc_by_bssid = true;

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY) == true)
		pmlmepriv->to_join = true;
	else
		status = rtw_do_join(padapter);

release_mlme_lock:
	spin_unlock_bh(&pmlmepriv->lock);

exit:

	return status;
}

u8 rtw_set_802_11_ssid(struct adapter *padapter, struct ndis_802_11_ssid *ssid)
{
	u8 status = _SUCCESS;

	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_network *pnetwork = &pmlmepriv->cur_network;

	netdev_dbg(padapter->pnetdev, "set ssid [%s] fw_state = 0x%08x\n",
		   ssid->ssid, get_fwstate(pmlmepriv));

	if (padapter->hw_init_completed == false) {
		status = _FAIL;
		goto exit;
	}

	spin_lock_bh(&pmlmepriv->lock);

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY) == true)
		goto handle_tkip_countermeasure;
	else if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING) == true)
		goto release_mlme_lock;

	if (check_fwstate(pmlmepriv, _FW_LINKED|WIFI_ADHOC_MASTER_STATE) == true) {
		if ((pmlmepriv->assoc_ssid.ssid_length == ssid->ssid_length) &&
		    (!memcmp(&pmlmepriv->assoc_ssid.ssid, ssid->ssid, ssid->ssid_length))) {
			if ((check_fwstate(pmlmepriv, WIFI_STATION_STATE) == false)) {
				if (rtw_is_same_ibss(padapter, pnetwork) == false) {
					/* if in WIFI_ADHOC_MASTER_STATE | WIFI_ADHOC_STATE, create bss or rejoin again */
					rtw_disassoc_cmd(padapter, 0, true);

					if (check_fwstate(pmlmepriv, _FW_LINKED) == true)
						rtw_indicate_disconnect(padapter);

					rtw_free_assoc_resources(padapter, 1);

					if (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == true) {
						_clr_fwstate_(pmlmepriv, WIFI_ADHOC_MASTER_STATE);
						set_fwstate(pmlmepriv, WIFI_ADHOC_STATE);
					}
				} else {
					goto release_mlme_lock;/* it means driver is in WIFI_ADHOC_MASTER_STATE, we needn't create bss again. */
				}
			} else {
				rtw_lps_ctrl_wk_cmd(padapter, LPS_CTRL_JOINBSS, 1);
			}
		} else {
			rtw_disassoc_cmd(padapter, 0, true);

			if (check_fwstate(pmlmepriv, _FW_LINKED) == true)
				rtw_indicate_disconnect(padapter);

			rtw_free_assoc_resources(padapter, 1);

			if (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == true) {
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

	if (rtw_validate_ssid(ssid) == false) {
		status = _FAIL;
		goto release_mlme_lock;
	}

	memcpy(&pmlmepriv->assoc_ssid, ssid, sizeof(struct ndis_802_11_ssid));
	pmlmepriv->assoc_by_bssid = false;

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY) == true)
		pmlmepriv->to_join = true;
	else
		status = rtw_do_join(padapter);

release_mlme_lock:
	spin_unlock_bh(&pmlmepriv->lock);

exit:

	return status;
}

u8 rtw_set_802_11_connect(struct adapter *padapter, u8 *bssid, struct ndis_802_11_ssid *ssid)
{
	u8 status = _SUCCESS;
	bool bssid_valid = true;
	bool ssid_valid = true;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	if (!ssid || rtw_validate_ssid(ssid) == false)
		ssid_valid = false;

	if (!bssid || rtw_validate_bssid(bssid) == false)
		bssid_valid = false;

	if (!ssid_valid && !bssid_valid) {
		status = _FAIL;
		goto exit;
	}

	if (padapter->hw_init_completed == false) {
		status = _FAIL;
		goto exit;
	}

	spin_lock_bh(&pmlmepriv->lock);

	netdev_dbg(padapter->pnetdev, FUNC_ADPT_FMT "  fw_state = 0x%08x\n",
		   FUNC_ADPT_ARG(padapter), get_fwstate(pmlmepriv));

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY) == true)
		goto handle_tkip_countermeasure;
	else if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING) == true)
		goto release_mlme_lock;

handle_tkip_countermeasure:
	if (rtw_handle_tkip_countermeasure(padapter, __func__) == _FAIL) {
		status = _FAIL;
		goto release_mlme_lock;
	}

	if (ssid && ssid_valid)
		memcpy(&pmlmepriv->assoc_ssid, ssid, sizeof(struct ndis_802_11_ssid));
	else
		memset(&pmlmepriv->assoc_ssid, 0, sizeof(struct ndis_802_11_ssid));

	if (bssid && bssid_valid) {
		memcpy(&pmlmepriv->assoc_bssid, bssid, ETH_ALEN);
		pmlmepriv->assoc_by_bssid = true;
	} else {
		pmlmepriv->assoc_by_bssid = false;
	}

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY) == true)
		pmlmepriv->to_join = true;
	else
		status = rtw_do_join(padapter);

release_mlme_lock:
	spin_unlock_bh(&pmlmepriv->lock);

exit:
	return status;
}

u8 rtw_set_802_11_infrastructure_mode(struct adapter *padapter,
	enum ndis_802_11_network_infrastructure networktype)
{
	struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct	wlan_network	*cur_network = &pmlmepriv->cur_network;
	enum ndis_802_11_network_infrastructure *pold_state = &(cur_network->network.infrastructure_mode);

	if (*pold_state != networktype) {
		if (*pold_state == Ndis802_11APMode) {
			/* change to other mode from Ndis802_11APMode */
			cur_network->join_res = -1;

			stop_ap_mode(padapter);
		}

		spin_lock_bh(&pmlmepriv->lock);

		if ((check_fwstate(pmlmepriv, _FW_LINKED) == true) || (*pold_state == Ndis802_11IBSS))
			rtw_disassoc_cmd(padapter, 0, true);

		if ((check_fwstate(pmlmepriv, _FW_LINKED) == true) ||
			(check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == true))
			rtw_free_assoc_resources(padapter, 1);

		if ((*pold_state == Ndis802_11Infrastructure) || (*pold_state == Ndis802_11IBSS)) {
			if (check_fwstate(pmlmepriv, _FW_LINKED) == true)
				rtw_indicate_disconnect(padapter); /* will clr Linked_state; before this function, we must have checked whether issue dis-assoc_cmd or not */
	       }

		*pold_state = networktype;

		_clr_fwstate_(pmlmepriv, ~WIFI_NULL_STATE);

		switch (networktype) {
		case Ndis802_11IBSS:
			set_fwstate(pmlmepriv, WIFI_ADHOC_STATE);
			break;

		case Ndis802_11Infrastructure:
			set_fwstate(pmlmepriv, WIFI_STATION_STATE);
			break;

		case Ndis802_11APMode:
			set_fwstate(pmlmepriv, WIFI_AP_STATE);
			start_ap_mode(padapter);
			/* rtw_indicate_connect(padapter); */

			break;

		case Ndis802_11AutoUnknown:
		case Ndis802_11InfrastructureMax:
			break;
		}

		/* SecClearAllKeys(adapter); */

		spin_unlock_bh(&pmlmepriv->lock);
	}
	return true;
}


u8 rtw_set_802_11_disassociate(struct adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	spin_lock_bh(&pmlmepriv->lock);

	if (check_fwstate(pmlmepriv, _FW_LINKED) == true) {
		rtw_disassoc_cmd(padapter, 0, true);
		rtw_indicate_disconnect(padapter);
		/* modify for CONFIG_IEEE80211W, none 11w can use it */
		rtw_free_assoc_resources_cmd(padapter);
		rtw_pwr_wakeup(padapter);
	}

	spin_unlock_bh(&pmlmepriv->lock);

	return true;
}

u8 rtw_set_802_11_bssid_list_scan(struct adapter *padapter, struct ndis_802_11_ssid *pssid, int ssid_max_num)
{
	struct	mlme_priv 	*pmlmepriv = &padapter->mlmepriv;
	u8 res = true;

	if (padapter == NULL) {
		res = false;
		goto exit;
	}
	if (padapter->hw_init_completed == false) {
		res = false;
		goto exit;
	}

	if ((check_fwstate(pmlmepriv, _FW_UNDER_SURVEY|_FW_UNDER_LINKING) == true) ||
		(pmlmepriv->LinkDetectInfo.bBusyTraffic == true)) {
		/*  Scan or linking is in progress, do nothing. */
		res = true;

	} else {
		if (rtw_is_scan_deny(padapter))
			return _SUCCESS;

		spin_lock_bh(&pmlmepriv->lock);

		res = rtw_sitesurvey_cmd(padapter, pssid, ssid_max_num, NULL, 0);

		spin_unlock_bh(&pmlmepriv->lock);
	}
exit:

	return res;
}

u8 rtw_set_802_11_authentication_mode(struct adapter *padapter, enum ndis_802_11_authentication_mode authmode)
{
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	int res;
	u8 ret;

	psecuritypriv->ndisauthtype = authmode;

	if (psecuritypriv->ndisauthtype > 3)
		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;

	res = rtw_set_auth(padapter, psecuritypriv);

	if (res == _SUCCESS)
		ret = true;
	else
		ret = false;

	return ret;
}

u8 rtw_set_802_11_add_wep(struct adapter *padapter, struct ndis_802_11_wep *wep)
{

	signed int		keyid, res;
	struct security_priv *psecuritypriv = &(padapter->securitypriv);
	u8 ret = _SUCCESS;

	keyid = wep->key_index & 0x3fffffff;

	if (keyid >= 4) {
		ret = false;
		goto exit;
	}

	switch (wep->key_length) {
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

	memcpy(&(psecuritypriv->dot11DefKey[keyid].skey[0]), &(wep->key_material), wep->key_length);

	psecuritypriv->dot11DefKeylen[keyid] = wep->key_length;

	psecuritypriv->dot11PrivacyKeyIndex = keyid;

	res = rtw_set_key(padapter, psecuritypriv, keyid, 1, true);

	if (res == _FAIL)
		ret = false;
exit:

	return ret;
}

/*
* rtw_get_cur_max_rate -
* @adapter: pointer to struct adapter structure
*
* Return 0 or 100Kbps
*/
u16 rtw_get_cur_max_rate(struct adapter *adapter)
{
	int	i = 0;
	u16 rate = 0, max_rate = 0;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct wlan_bssid_ex	*pcur_bss = &pmlmepriv->cur_network.network;
	struct sta_info *psta = NULL;
	u8 short_GI = 0;

	if ((check_fwstate(pmlmepriv, _FW_LINKED) != true)
		&& (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) != true))
		return 0;

	psta = rtw_get_stainfo(&adapter->stapriv, get_bssid(pmlmepriv));
	if (psta == NULL)
		return 0;

	short_GI = query_ra_short_GI(psta);

	if (is_supported_ht(psta->wireless_mode)) {
		max_rate = rtw_mcs_rate(psta->bw_mode == CHANNEL_WIDTH_40 ? 1 : 0,
					short_GI,
					psta->htpriv.ht_cap.mcs.rx_mask);
	} else {
		while ((pcur_bss->supported_rates[i] != 0) && (pcur_bss->supported_rates[i] != 0xFF)) {
			rate = pcur_bss->supported_rates[i]&0x7F;
			if (rate > max_rate)
				max_rate = rate;
			i++;
		}

		max_rate = max_rate*10/2;
	}

	return max_rate;
}
