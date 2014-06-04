/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
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
#define _RTW_IOCTL_SET_C_

#include <osdep_service.h>
#include <drv_types.h>
#include <rtw_ioctl_set.h>
#include <hal_intf.h>

#include <usb_osintf.h>
#include <usb_ops.h>
#include <linux/ieee80211.h>

u8 rtw_do_join23a(struct rtw_adapter *padapter)
{
	struct list_head *plist, *phead;
	u8* pibss = NULL;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct rtw_queue *queue = &pmlmepriv->scanned_queue;
	u8 ret = _SUCCESS;

	spin_lock_bh(&pmlmepriv->scanned_queue.lock);
	phead = get_list_head(queue);
	plist = phead->next;

	RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_info_,
		 ("\n rtw_do_join23a: phead = %p; plist = %p\n\n\n",
		  phead, plist));

	pmlmepriv->cur_network.join_res = -2;

	set_fwstate(pmlmepriv, _FW_UNDER_LINKING);

	pmlmepriv->to_join = true;

	if (_rtw_queue_empty23a(queue) == true) {
		spin_unlock_bh(&pmlmepriv->scanned_queue.lock);
		_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);

		/* when set_ssid/set_bssid for rtw_do_join23a(), but
		   scanning queue is empty */
		/* we try to issue sitesurvey firstly */

		if (pmlmepriv->LinkDetectInfo.bBusyTraffic == false ||
		    rtw_to_roaming(padapter) > 0) {
			RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_info_,
				 ("rtw_do_join23a(): site survey if scanned_queue "
				  "is empty\n."));
			/*  submit site_survey23a_cmd */
			ret = rtw_sitesurvey_cmd23a(padapter,
						 &pmlmepriv->assoc_ssid, 1,
						 NULL, 0);
			if (ret != _SUCCESS) {
				pmlmepriv->to_join = false;
				RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_err_,
					 ("rtw_do_join23a(): site survey return "
					  "error\n."));
			}
		} else {
			pmlmepriv->to_join = false;
			ret = _FAIL;
		}

		goto exit;
	} else {
		int select_ret;
		spin_unlock_bh(&pmlmepriv->scanned_queue.lock);
		select_ret = rtw_select_and_join_from_scanned_queue23a(pmlmepriv);
		if (select_ret == _SUCCESS) {
			pmlmepriv->to_join = false;
			mod_timer(&pmlmepriv->assoc_timer,
				  jiffies + msecs_to_jiffies(MAX_JOIN_TIMEOUT));
		} else {
			if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == true) {
				struct wlan_bssid_ex *pdev_network;
				/*  submit createbss_cmd to change to a
				    ADHOC_MASTER */

				/* pmlmepriv->lock has been acquired by
				   caller... */
				pdev_network =
					&padapter->registrypriv.dev_network;

				pmlmepriv->fw_state = WIFI_ADHOC_MASTER_STATE;

				pibss = padapter->registrypriv.dev_network.MacAddress;

				memcpy(&pdev_network->Ssid,
				       &pmlmepriv->assoc_ssid,
				       sizeof(struct cfg80211_ssid));

				rtw_update_registrypriv_dev_network23a(padapter);

				rtw_generate_random_ibss23a(pibss);

				if (rtw_createbss_cmd23a(padapter) != _SUCCESS) {
					RT_TRACE(_module_rtl871x_ioctl_set_c_,
						 _drv_err_,
						 ("***Error =>do_goin: rtw_creat"
						  "ebss_cmd status FAIL***\n"));
					ret =  false;
					goto exit;
				}

				pmlmepriv->to_join = false;

				RT_TRACE(_module_rtl871x_ioctl_set_c_,
					 _drv_info_,
					 ("***Error => rtw_select_and_join_from"
					  "_scanned_queue FAIL under STA_Mode"
					  "***\n "));
			} else {
				/*  can't associate ; reset under-linking */
				_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);

				/* when set_ssid/set_bssid for rtw_do_join23a(),
				   but there are no desired bss in scanning
				   queue */
				/* we try to issue sitesurvey firstly */
				if (pmlmepriv->LinkDetectInfo.bBusyTraffic ==
				    false || rtw_to_roaming(padapter) > 0) {
					/* DBG_8723A("rtw_do_join23a() when   no "
					   "desired bss in scanning queue\n");
					*/
					ret = rtw_sitesurvey_cmd23a(padapter, &pmlmepriv->assoc_ssid, 1, NULL, 0);
					if (ret != _SUCCESS) {
						pmlmepriv->to_join = false;
						RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_err_, ("do_join(): site survey return error\n."));
					}
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

u8 rtw_set_802_11_ssid23a(struct rtw_adapter* padapter, struct cfg80211_ssid *ssid)
{
	u8 status = _SUCCESS;
	u32 cur_time = 0;

	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_network *pnetwork = &pmlmepriv->cur_network;



	DBG_8723A_LEVEL(_drv_always_, "set ssid [%s] fw_state = 0x%08x\n",
			ssid->ssid, get_fwstate(pmlmepriv));

	if (padapter->hw_init_completed == false) {
		RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_err_,
			 ("set_ssid: hw_init_completed == false =>exit!!!\n"));
		status = _FAIL;
		goto exit;
	}

	spin_lock_bh(&pmlmepriv->lock);

	DBG_8723A("Set SSID under fw_state = 0x%08x\n", get_fwstate(pmlmepriv));
	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY) == true) {
		goto handle_tkip_countermeasure;
	} else if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING) == true) {
		goto release_mlme_lock;
	}

	if (check_fwstate(pmlmepriv, _FW_LINKED|WIFI_ADHOC_MASTER_STATE) == true)
	{
		RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_info_,
			 ("set_ssid: _FW_LINKED||WIFI_ADHOC_MASTER_STATE\n"));

		if ((pmlmepriv->assoc_ssid.ssid_len == ssid->ssid_len) &&
		    !memcmp(&pmlmepriv->assoc_ssid.ssid, ssid->ssid,
			    ssid->ssid_len)) {
			if ((check_fwstate(pmlmepriv, WIFI_STATION_STATE) == false))
			{
				RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_err_,
					 ("Set SSID is the same ssid, fw_state = 0x%08x\n",
					  get_fwstate(pmlmepriv)));

				if (rtw_is_same_ibss23a(padapter, pnetwork) == false)
				{
					/* if in WIFI_ADHOC_MASTER_STATE | WIFI_ADHOC_STATE, create bss or rejoin again */
					rtw_disassoc_cmd23a(padapter, 0, true);

					if (check_fwstate(pmlmepriv, _FW_LINKED) == true)
						rtw_indicate_disconnect23a(padapter);

					rtw_free_assoc_resources23a(padapter, 1);

					if (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == true) {
						_clr_fwstate_(pmlmepriv, WIFI_ADHOC_MASTER_STATE);
						set_fwstate(pmlmepriv, WIFI_ADHOC_STATE);
					}
				} else {
					goto release_mlme_lock;/* it means driver is in WIFI_ADHOC_MASTER_STATE, we needn't create bss again. */
				}
			} else {
				rtw_lps_ctrl_wk_cmd23a(padapter, LPS_CTRL_JOINBSS, 1);
			}
		} else {
			RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_info_,
				 ("Set SSID not the same ssid\n"));
			RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_info_,
				 ("set_ssid =[%s] len = 0x%x\n", ssid->ssid,
				  (unsigned int)ssid->ssid_len));
			RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_info_,
				 ("assoc_ssid =[%s] len = 0x%x\n",
				  pmlmepriv->assoc_ssid.ssid,
				  (unsigned int)pmlmepriv->assoc_ssid.ssid_len));

			rtw_disassoc_cmd23a(padapter, 0, true);

			if (check_fwstate(pmlmepriv, _FW_LINKED) == true)
				rtw_indicate_disconnect23a(padapter);

			rtw_free_assoc_resources23a(padapter, 1);

			if (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == true) {
				_clr_fwstate_(pmlmepriv, WIFI_ADHOC_MASTER_STATE);
				set_fwstate(pmlmepriv, WIFI_ADHOC_STATE);
			}
		}
	}

handle_tkip_countermeasure:

	if (padapter->securitypriv.btkip_countermeasure == true) {
		cur_time = jiffies;

		if ((cur_time - padapter->securitypriv.btkip_countermeasure_time) > 60 * HZ)
		{
			padapter->securitypriv.btkip_countermeasure = false;
			padapter->securitypriv.btkip_countermeasure_time = 0;
		}
		else
		{
			status = _FAIL;
			goto release_mlme_lock;
		}
	}

	memcpy(&pmlmepriv->assoc_ssid, ssid, sizeof(struct cfg80211_ssid));
	pmlmepriv->assoc_by_bssid = false;

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY) == true) {
		pmlmepriv->to_join = true;
	}
	else {
		status = rtw_do_join23a(padapter);
	}

release_mlme_lock:
	spin_unlock_bh(&pmlmepriv->lock);

exit:
	RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_err_,
		("-rtw_set_802_11_ssid23a: status =%d\n", status));



	return status;
}

u8 rtw_set_802_11_infrastructure_mode23a(struct rtw_adapter* padapter,
	enum ndis_802_11_net_infra networktype)
{
	struct	mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct	wlan_network	*cur_network = &pmlmepriv->cur_network;
	enum ndis_802_11_net_infra* pold_state = &cur_network->network.InfrastructureMode;



	RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_notice_,
		 ("+rtw_set_802_11_infrastructure_mode23a: old =%d new =%d fw_state = 0x%08x\n",
		  *pold_state, networktype, get_fwstate(pmlmepriv)));

	if (*pold_state != networktype)
	{
		spin_lock_bh(&pmlmepriv->lock);

		RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_info_, (" change mode!"));
		/* DBG_8723A("change mode, old_mode =%d, new_mode =%d, fw_state = 0x%x\n", *pold_state, networktype, get_fwstate(pmlmepriv)); */

		if (*pold_state == Ndis802_11APMode)
		{
			/* change to other mode from Ndis802_11APMode */
			cur_network->join_res = -1;

#ifdef CONFIG_8723AU_AP_MODE
			stop_ap_mode23a(padapter);
#endif
		}

		if ((check_fwstate(pmlmepriv, _FW_LINKED) == true) ||(*pold_state == Ndis802_11IBSS))
			rtw_disassoc_cmd23a(padapter, 0, true);

		if ((check_fwstate(pmlmepriv, _FW_LINKED) == true) ||
			(check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == true))
			rtw_free_assoc_resources23a(padapter, 1);

		if ((*pold_state == Ndis802_11Infrastructure) ||(*pold_state == Ndis802_11IBSS))
	       {
			if (check_fwstate(pmlmepriv, _FW_LINKED) == true)
			{
				rtw_indicate_disconnect23a(padapter); /* will clr Linked_state; before this function, we must have chked whether  issue dis-assoc_cmd or not */
			}
	       }

		*pold_state = networktype;

		_clr_fwstate_(pmlmepriv, ~WIFI_NULL_STATE);

		switch (networktype)
		{
			case Ndis802_11IBSS:
				set_fwstate(pmlmepriv, WIFI_ADHOC_STATE);
				break;

			case Ndis802_11Infrastructure:
				set_fwstate(pmlmepriv, WIFI_STATION_STATE);
				break;

			case Ndis802_11APMode:
				set_fwstate(pmlmepriv, WIFI_AP_STATE);
#ifdef CONFIG_8723AU_AP_MODE
				start_ap_mode23a(padapter);
				/* rtw_indicate_connect23a(padapter); */
#endif

				break;

			case Ndis802_11AutoUnknown:
			case Ndis802_11InfrastructureMax:
				break;
		}

		/* SecClearAllKeys(adapter); */

		/* RT_TRACE(COMP_OID_SET, DBG_LOUD, ("set_infrastructure: fw_state:%x after changing mode\n", */
		/*									get_fwstate(pmlmepriv))); */

		spin_unlock_bh(&pmlmepriv->lock);
	}



	return true;
}

u8 rtw_set_802_11_bssid23a_list_scan(struct rtw_adapter *padapter,
				  struct cfg80211_ssid *pssid, int ssid_max_num)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	u8 res = true;

	RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_err_,
		 ("+rtw_set_802_11_bssid23a_list_scan(), fw_state =%x\n",
		  get_fwstate(pmlmepriv)));

	if (!padapter) {
		res = false;
		goto exit;
	}
	if (padapter->hw_init_completed == false) {
		res = false;
		RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_err_,
			 ("\n === rtw_set_802_11_bssid23a_list_scan:"
			  "hw_init_completed == false ===\n"));
		goto exit;
	}

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY | _FW_UNDER_LINKING) ||
	    (pmlmepriv->LinkDetectInfo.bBusyTraffic == true)) {
		/*  Scan or linking is in progress, do nothing. */
		RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_err_,
			 ("rtw_set_802_11_bssid23a_list_scan fail since fw_state "
			  "= %x\n", get_fwstate(pmlmepriv)));
		res = true;

		if (check_fwstate(pmlmepriv,
				  (_FW_UNDER_SURVEY|_FW_UNDER_LINKING))) {
			RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_err_,
				 ("\n###_FW_UNDER_SURVEY|_FW_UNDER_LINKING\n"));
		} else {
			RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_err_,
				 ("\n###pmlmepriv->sitesurveyctrl.traffic_"
				  "busy == true\n"));
		}
	} else {
		if (rtw_is_scan_deny(padapter)) {
			DBG_8723A(FUNC_ADPT_FMT": scan deny\n",
				  FUNC_ADPT_ARG(padapter));
			return _SUCCESS;
		}

		spin_lock_bh(&pmlmepriv->lock);

		res = rtw_sitesurvey_cmd23a(padapter, pssid, ssid_max_num,
					 NULL, 0);

		spin_unlock_bh(&pmlmepriv->lock);
	}
exit:
	return res;
}

u8 rtw_set_802_11_authentication_mode23a(struct rtw_adapter* padapter,
				      enum ndis_802_11_auth_mode authmode)
{
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	int res;
	u8 ret;

	RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_info_,
		 ("set_802_11_auth.mode(): mode =%x\n", authmode));

	psecuritypriv->ndisauthtype = authmode;

	RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_info_,
		 ("rtw_set_802_11_authentication_mode23a:"
		  "psecuritypriv->ndisauthtype =%d",
		  psecuritypriv->ndisauthtype));

	if (psecuritypriv->ndisauthtype > 3)
		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;

	res = rtw_set_auth23a(padapter, psecuritypriv);

	if (res == _SUCCESS)
		ret = true;
	else
		ret = false;

	return ret;
}

u8 rtw_set_802_11_add_wep23a(struct rtw_adapter* padapter,
			  struct ndis_802_11_wep *wep)
{
	u8 bdefaultkey;
	u8 btransmitkey;
	int keyid, res;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	u8 ret = _SUCCESS;

	bdefaultkey = (wep->KeyIndex & 0x40000000) > 0 ? false : true;
	btransmitkey = (wep->KeyIndex & 0x80000000) > 0 ? true  : false;
	keyid = wep->KeyIndex & 0x3fffffff;

	if (keyid >= 4) {
		RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_err_,
			 ("MgntActrtw_set_802_11_add_wep23a:keyid>4 =>fail\n"));
		ret = false;
		goto exit;
	}

	switch (wep->KeyLength)
	{
	case 5:
		psecuritypriv->dot11PrivacyAlgrthm = _WEP40_;
		RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_info_,
			 ("MgntActrtw_set_802_11_add_wep23a:wep->KeyLength = 5\n"));
		break;
	case 13:
		psecuritypriv->dot11PrivacyAlgrthm = _WEP104_;
		RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_info_,
			 ("MgntActrtw_set_802_11_add_wep23a:wep->KeyLength = 13\n"));
		break;
	default:
		psecuritypriv->dot11PrivacyAlgrthm = _NO_PRIVACY_;
		RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_info_,
			 ("MgntActrtw_set_802_11_add_wep23a:wep->KeyLength!= 5 "
			  "or 13\n"));
			break;
	}

	RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_info_,
		 ("rtw_set_802_11_add_wep23a:befor memcpy, wep->KeyLength = 0x%x "
		  "wep->KeyIndex = 0x%x  keyid =%x\n",
		  wep->KeyLength, wep->KeyIndex, keyid));

	memcpy(&psecuritypriv->dot11DefKey[keyid].skey[0],
	       &wep->KeyMaterial, wep->KeyLength);

	psecuritypriv->dot11DefKeylen[keyid] = wep->KeyLength;

	psecuritypriv->dot11PrivacyKeyIndex = keyid;

	RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_info_,
		 ("rtw_set_802_11_add_wep23a:security key material : %x %x %x %x "
		  "%x %x %x %x %x %x %x %x %x\n",
		  psecuritypriv->dot11DefKey[keyid].skey[0],
		  psecuritypriv->dot11DefKey[keyid].skey[1],
		  psecuritypriv->dot11DefKey[keyid].skey[2],
		  psecuritypriv->dot11DefKey[keyid].skey[3],
		  psecuritypriv->dot11DefKey[keyid].skey[4],
		  psecuritypriv->dot11DefKey[keyid].skey[5],
		  psecuritypriv->dot11DefKey[keyid].skey[6],
		  psecuritypriv->dot11DefKey[keyid].skey[7],
		  psecuritypriv->dot11DefKey[keyid].skey[8],
		  psecuritypriv->dot11DefKey[keyid].skey[9],
		  psecuritypriv->dot11DefKey[keyid].skey[10],
		  psecuritypriv->dot11DefKey[keyid].skey[11],
		  psecuritypriv->dot11DefKey[keyid].skey[12]));

	res = rtw_set_key23a(padapter, psecuritypriv, keyid, 1);

	if (res == _FAIL)
		ret = false;
exit:

	return ret;
}

/*
* rtw_get_cur_max_rate23a -
* @adapter: pointer to _adapter structure
*
* Return 0 or 100Kbps
*/
u16 rtw_get_cur_max_rate23a(struct rtw_adapter *adapter)
{
	int i = 0;
	u8 *p;
	u16 rate = 0, max_rate = 0;
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct registry_priv *pregistrypriv = &adapter->registrypriv;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct wlan_bssid_ex  *pcur_bss = &pmlmepriv->cur_network.network;
	struct ieee80211_ht_cap *pht_capie;
	u8 rf_type = 0;
	u8 bw_40MHz = 0, short_GI_20 = 0, short_GI_40 = 0;
	u16 mcs_rate = 0;
	u32 ht_ielen = 0;

	if (!check_fwstate(pmlmepriv, _FW_LINKED) &&
	    !check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE))
		return 0;

	if (pmlmeext->cur_wireless_mode & (WIRELESS_11_24N|WIRELESS_11_5N)) {
		p = rtw_get_ie23a(&pcur_bss->IEs[12], _HT_CAPABILITY_IE_,
			       &ht_ielen, pcur_bss->IELength - 12);
		if (p && ht_ielen > 0) {
			pht_capie = (struct ieee80211_ht_cap *)(p + 2);

			memcpy(&mcs_rate, &pht_capie->mcs, 2);

			/* bw_40MHz = (pht_capie->cap_info&
			   IEEE80211_HT_CAP_SUP_WIDTH_20_40) ? 1:0; */
			/* cur_bwmod is updated by beacon, pmlmeinfo is
			   updated by association response */
			bw_40MHz = (pmlmeext->cur_bwmode &&
				    (HT_INFO_HT_PARAM_REC_TRANS_CHNL_WIDTH &
				     pmlmeinfo->HT_info.infos[0])) ? 1:0;

			/* short_GI = (pht_capie->cap_info & (IEEE80211_HT_CAP
			   _SGI_20|IEEE80211_HT_CAP_SGI_40)) ? 1 : 0; */
			short_GI_20 = (pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info&IEEE80211_HT_CAP_SGI_20) ? 1:0;
			short_GI_40 = (pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info&IEEE80211_HT_CAP_SGI_40) ? 1:0;

			rtw23a_hal_get_hwreg(adapter, HW_VAR_RF_TYPE,
					  (u8 *)(&rf_type));
			max_rate = rtw_mcs_rate23a(rf_type, bw_40MHz &
						pregistrypriv->cbw40_enable,
						short_GI_20, short_GI_40,
						pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate
			);
		}
	} else {
		while ((pcur_bss->SupportedRates[i] != 0) &&
		       (pcur_bss->SupportedRates[i] != 0xFF)) {
			rate = pcur_bss->SupportedRates[i] & 0x7F;
			if (rate>max_rate)
				max_rate = rate;
			i++;
		}

		max_rate = max_rate * 10 / 2;
	}

	return max_rate;
}
