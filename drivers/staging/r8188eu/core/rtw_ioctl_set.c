// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2012 Realtek Corporation. */

#define _RTW_IOCTL_SET_C_

#include "../include/osdep_service.h"
#include "../include/drv_types.h"
#include "../include/rtw_ioctl_set.h"
#include "../include/hal_intf.h"

#include "../include/usb_osintf.h"
#include "../include/usb_ops.h"

extern void indicate_wx_scan_complete_event(struct adapter *padapter);

u8 rtw_do_join(struct adapter *padapter)
{
	struct list_head *plist, *phead;
	u8 *pibss = NULL;
	struct	mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct __queue *queue	= &pmlmepriv->scanned_queue;
	u8 ret = _SUCCESS;

	spin_lock_bh(&pmlmepriv->scanned_queue.lock);
	phead = get_list_head(queue);
	plist = phead->next;

	pmlmepriv->cur_network.join_res = -2;

	set_fwstate(pmlmepriv, _FW_UNDER_LINKING);

	pmlmepriv->pscanned = plist;

	pmlmepriv->to_join = true;

	if (list_empty(&queue->queue)) {
		spin_unlock_bh(&pmlmepriv->scanned_queue.lock);
		_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);

		/* when set_ssid/set_bssid for rtw_do_join(), but scanning queue is empty */
		/* we try to issue sitesurvey firstly */

		if (!pmlmepriv->LinkDetectInfo.bBusyTraffic ||
		    pmlmepriv->to_roaming > 0) {
			/*  submit site_survey_cmd */
			ret = rtw_sitesurvey_cmd(padapter, &pmlmepriv->assoc_ssid, 1, NULL, 0);
			if (ret != _SUCCESS)
				pmlmepriv->to_join = false;
		} else {
			pmlmepriv->to_join = false;
			ret = _FAIL;
		}

		return ret;
	} else {
		int select_ret;

		spin_unlock_bh(&pmlmepriv->scanned_queue.lock);
		select_ret = rtw_select_and_join_from_scanned_queue(pmlmepriv);
		if (select_ret == _SUCCESS) {
			pmlmepriv->to_join = false;
			_set_timer(&pmlmepriv->assoc_timer, MAX_JOIN_TIMEOUT);
		} else {
			if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)) {
				/*  submit createbss_cmd to change to a ADHOC_MASTER */

				/* pmlmepriv->lock has been acquired by caller... */
				struct wlan_bssid_ex    *pdev_network = &padapter->registrypriv.dev_network;

				pmlmepriv->fw_state = WIFI_ADHOC_MASTER_STATE;

				pibss = padapter->registrypriv.dev_network.MacAddress;

				memcpy(&pdev_network->Ssid, &pmlmepriv->assoc_ssid, sizeof(struct ndis_802_11_ssid));

				rtw_update_registrypriv_dev_network(padapter);

				rtw_generate_random_ibss(pibss);

				if (rtw_createbss_cmd(padapter) != _SUCCESS)
					return false;

				pmlmepriv->to_join = false;
			} else {
				/*  can't associate ; reset under-linking */
				_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);

				/* when set_ssid/set_bssid for rtw_do_join(), but there are no desired bss in scanning queue */
				/* we try to issue sitesurvey firstly */
				if (!pmlmepriv->LinkDetectInfo.bBusyTraffic ||
				    pmlmepriv->to_roaming > 0) {
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

	return ret;
}

u8 rtw_set_802_11_bssid(struct adapter *padapter, u8 *bssid)
{
	u8 status = _SUCCESS;
	u32 cur_time = 0;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	if ((bssid[0] == 0x00 && bssid[1] == 0x00 && bssid[2] == 0x00 &&
	     bssid[3] == 0x00 && bssid[4] == 0x00 && bssid[5] == 0x00) ||
	    (bssid[0] == 0xFF && bssid[1] == 0xFF && bssid[2] == 0xFF &&
	     bssid[3] == 0xFF && bssid[4] == 0xFF && bssid[5] == 0xFF)) {
		status = _FAIL;
		goto exit;
	}

	spin_lock_bh(&pmlmepriv->lock);

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY))
		goto handle_tkip_countermeasure;
	else if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING))
		goto release_mlme_lock;

	if (check_fwstate(pmlmepriv, _FW_LINKED | WIFI_ADHOC_MASTER_STATE)) {
		if (!memcmp(&pmlmepriv->cur_network.network.MacAddress, bssid, ETH_ALEN)) {
			if (!check_fwstate(pmlmepriv, WIFI_STATION_STATE))
				goto release_mlme_lock;/* it means driver is in WIFI_ADHOC_MASTER_STATE, we needn't create bss again. */
		} else {
			rtw_disassoc_cmd(padapter, 0, true);

			if (check_fwstate(pmlmepriv, _FW_LINKED))
				rtw_indicate_disconnect(padapter);

			rtw_free_assoc_resources(padapter, 1);

			if (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)) {
				_clr_fwstate_(pmlmepriv, WIFI_ADHOC_MASTER_STATE);
				set_fwstate(pmlmepriv, WIFI_ADHOC_STATE);
			}
		}
	}

handle_tkip_countermeasure:
	/* should we add something here...? */

	if (padapter->securitypriv.btkip_countermeasure) {
		cur_time = jiffies;

		if ((cur_time - padapter->securitypriv.btkip_countermeasure_time) > 60 * HZ) {
			padapter->securitypriv.btkip_countermeasure = false;
			padapter->securitypriv.btkip_countermeasure_time = 0;
		} else {
			status = _FAIL;
			goto release_mlme_lock;
		}
	}

	memcpy(&pmlmepriv->assoc_bssid, bssid, ETH_ALEN);
	pmlmepriv->assoc_by_bssid = true;

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY))
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
	u32 cur_time = 0;

	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_network *pnetwork = &pmlmepriv->cur_network;

	if (!padapter->hw_init_completed) {
		status = _FAIL;
		goto exit;
	}

	spin_lock_bh(&pmlmepriv->lock);

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY)) {
		goto handle_tkip_countermeasure;
	} else if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING)) {
		goto release_mlme_lock;
	}

	if (check_fwstate(pmlmepriv, _FW_LINKED | WIFI_ADHOC_MASTER_STATE)) {
		if ((pmlmepriv->assoc_ssid.SsidLength == ssid->SsidLength) &&
		    (!memcmp(&pmlmepriv->assoc_ssid.Ssid, ssid->Ssid, ssid->SsidLength))) {
			if (!check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
				if (!rtw_is_same_ibss(padapter, pnetwork)) {
					/* if in WIFI_ADHOC_MASTER_STATE | WIFI_ADHOC_STATE, create bss or rejoin again */
					rtw_disassoc_cmd(padapter, 0, true);

					if (check_fwstate(pmlmepriv, _FW_LINKED))
						rtw_indicate_disconnect(padapter);

					rtw_free_assoc_resources(padapter, 1);

					if (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)) {
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

			if (check_fwstate(pmlmepriv, _FW_LINKED))
				rtw_indicate_disconnect(padapter);

			rtw_free_assoc_resources(padapter, 1);

			if (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)) {
				_clr_fwstate_(pmlmepriv, WIFI_ADHOC_MASTER_STATE);
				set_fwstate(pmlmepriv, WIFI_ADHOC_STATE);
			}
		}
	}

handle_tkip_countermeasure:

	if (padapter->securitypriv.btkip_countermeasure) {
		cur_time = jiffies;

		if ((cur_time - padapter->securitypriv.btkip_countermeasure_time) > 60 * HZ) {
			padapter->securitypriv.btkip_countermeasure = false;
			padapter->securitypriv.btkip_countermeasure_time = 0;
		} else {
			status = _FAIL;
			goto release_mlme_lock;
		}
	}

	memcpy(&pmlmepriv->assoc_ssid, ssid, sizeof(struct ndis_802_11_ssid));
	pmlmepriv->assoc_by_bssid = false;

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY)) {
		pmlmepriv->to_join = true;
	} else {
		status = rtw_do_join(padapter);
	}

release_mlme_lock:
	spin_unlock_bh(&pmlmepriv->lock);

exit:
	return status;
}

u8 rtw_set_802_11_infrastructure_mode(struct adapter *padapter,
	enum ndis_802_11_network_infra networktype)
{
	struct	mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct	wlan_network	*cur_network = &pmlmepriv->cur_network;
	enum ndis_802_11_network_infra *pold_state = &cur_network->network.InfrastructureMode;

	if (*pold_state != networktype) {
		spin_lock_bh(&pmlmepriv->lock);

		if (*pold_state == Ndis802_11APMode) {
			/* change to other mode from Ndis802_11APMode */
			cur_network->join_res = -1;

			stop_ap_mode(padapter);
		}

		if ((check_fwstate(pmlmepriv, _FW_LINKED)) ||
		    (*pold_state == Ndis802_11IBSS))
			rtw_disassoc_cmd(padapter, 0, true);

		if ((check_fwstate(pmlmepriv, _FW_LINKED)) ||
		    (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)))
			rtw_free_assoc_resources(padapter, 1);

		if ((*pold_state == Ndis802_11Infrastructure) || (*pold_state == Ndis802_11IBSS)) {
			if (check_fwstate(pmlmepriv, _FW_LINKED))
				rtw_indicate_disconnect(padapter); /* will clr Linked_state; before this function, we must have chked whether  issue dis-assoc_cmd or not */
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
			break;
		case Ndis802_11AutoUnknown:
		case Ndis802_11InfrastructureMax:
			break;
		}
		spin_unlock_bh(&pmlmepriv->lock);
	}

	return true;
}

u8 rtw_set_802_11_disassociate(struct adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	spin_lock_bh(&pmlmepriv->lock);

	if (check_fwstate(pmlmepriv, _FW_LINKED)) {
		rtw_disassoc_cmd(padapter, 0, true);
		rtw_indicate_disconnect(padapter);
		rtw_free_assoc_resources(padapter, 1);
		rtw_pwr_wakeup(padapter);
	}

	spin_unlock_bh(&pmlmepriv->lock);

	return true;
}

u8 rtw_set_802_11_bssid_list_scan(struct adapter *padapter, struct ndis_802_11_ssid *pssid, int ssid_max_num)
{
	struct	mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	u8	res = true;

	if (!padapter) {
		res = false;
		goto exit;
	}
	if (!padapter->hw_init_completed) {
		res = false;
		goto exit;
	}

	if ((check_fwstate(pmlmepriv, _FW_UNDER_SURVEY | _FW_UNDER_LINKING)) ||
	    (pmlmepriv->LinkDetectInfo.bBusyTraffic)) {
		/*  Scan or linking is in progress, do nothing. */
		res = true;
	} else {
		if (rtw_is_scan_deny(padapter)) {
			indicate_wx_scan_complete_event(padapter);
			return _SUCCESS;
		}

		spin_lock_bh(&pmlmepriv->lock);

		res = rtw_sitesurvey_cmd(padapter, pssid, ssid_max_num, NULL, 0);

		spin_unlock_bh(&pmlmepriv->lock);
	}
exit:

	return res;
}

u8 rtw_set_802_11_authentication_mode(struct adapter *padapter, enum ndis_802_11_auth_mode authmode)
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
	int		keyid, res;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	u8		ret = _SUCCESS;

	keyid = wep->KeyIndex & 0x3fffffff;

	if (keyid >= 4) {
		ret = false;
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

	memcpy(&psecuritypriv->dot11DefKey[keyid].skey[0], &wep->KeyMaterial, wep->KeyLength);

	psecuritypriv->dot11DefKeylen[keyid] = wep->KeyLength;

	psecuritypriv->dot11PrivacyKeyIndex = keyid;

	res = rtw_set_key(padapter, psecuritypriv, keyid, 1);

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
	u8	*p;
	u16	rate = 0, max_rate = 0;
	struct mlme_ext_priv	*pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &pmlmeext->mlmext_info;
	struct registry_priv *pregistrypriv = &adapter->registrypriv;
	struct mlme_priv	*pmlmepriv = &adapter->mlmepriv;
	struct wlan_bssid_ex  *pcur_bss = &pmlmepriv->cur_network.network;
	struct ieee80211_ht_cap *pht_capie;
	u8	bw_40MHz = 0, short_GI_20 = 0, short_GI_40 = 0;
	u16	mcs_rate = 0;
	u32	ht_ielen = 0;

	if ((!check_fwstate(pmlmepriv, _FW_LINKED)) &&
	    (!check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)))
		return 0;

	if (pmlmeext->cur_wireless_mode & (WIRELESS_11_24N)) {
		p = rtw_get_ie(&pcur_bss->IEs[12], _HT_CAPABILITY_IE_, &ht_ielen, pcur_bss->IELength - 12);
		if (p && ht_ielen > 0) {
			pht_capie = (struct ieee80211_ht_cap *)(p + 2);

			memcpy(&mcs_rate, pht_capie->mcs.rx_mask, 2);

			/* cur_bwmod is updated by beacon, pmlmeinfo is updated by association response */
			bw_40MHz = (pmlmeext->cur_bwmode && (HT_INFO_HT_PARAM_REC_TRANS_CHNL_WIDTH & pmlmeinfo->HT_info.infos[0])) ? 1 : 0;

			short_GI_20 = (le16_to_cpu(pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info) & IEEE80211_HT_CAP_SGI_20) ? 1 : 0;
			short_GI_40 = (le16_to_cpu(pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info) & IEEE80211_HT_CAP_SGI_40) ? 1 : 0;

			max_rate = rtw_mcs_rate(bw_40MHz & (pregistrypriv->cbw40_enable),
						short_GI_20,
						short_GI_40,
						pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate);
		}
	} else {
		while ((pcur_bss->SupportedRates[i] != 0) && (pcur_bss->SupportedRates[i] != 0xFF)) {
			rate = pcur_bss->SupportedRates[i] & 0x7F;
			if (rate > max_rate)
				max_rate = rate;
			i++;
		}

		max_rate *= 5;
	}

	return max_rate;
}
