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

#include <usb_ops.h>
#include <linux/ieee80211.h>

int rtw_set_802_11_bssid23a_list_scan(struct rtw_adapter *padapter,
				      struct cfg80211_ssid *pssid,
				      int ssid_max_num)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	int res = _SUCCESS;

	RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_err_,
		 ("+rtw_set_802_11_bssid23a_list_scan(), fw_state =%x\n",
		  get_fwstate(pmlmepriv)));

	if (!padapter) {
		res = _FAIL;
		goto exit;
	}
	if (padapter->hw_init_completed == false) {
		res = _FAIL;
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
			DBG_8723A("%s(%s): scan deny\n",
				  __func__, padapter->pnetdev->name);
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

/*
* rtw_get_cur_max_rate23a -
* @adapter: pointer to _adapter structure
*
* Return 0 or 100Kbps
*/
u16 rtw_get_cur_max_rate23a(struct rtw_adapter *adapter)
{
	int i = 0;
	const u8 *p;
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

	if (pmlmeext->cur_wireless_mode & (WIRELESS_11_24N|WIRELESS_11_5N)) {
		p = cfg80211_find_ie(WLAN_EID_HT_CAPABILITY,
				     &pcur_bss->IEs[12],
				     pcur_bss->IELength - 12);
		if (p && p[1] > 0) {
			pht_capie = (struct ieee80211_ht_cap *)(p + 2);

			memcpy(&mcs_rate, &pht_capie->mcs, 2);

			/* bw_40MHz = (pht_capie->cap_info&
			   IEEE80211_HT_CAP_SUP_WIDTH_20_40) ? 1:0; */
			/* cur_bwmod is updated by beacon, pmlmeinfo is
			   updated by association response */
			bw_40MHz = (pmlmeext->cur_bwmode &&
				    (pmlmeinfo->HT_info.ht_param &
				     IEEE80211_HT_PARAM_CHAN_WIDTH_ANY)) ? 1:0;

			/* short_GI = (pht_capie->cap_info & (IEEE80211_HT_CAP
			   _SGI_20|IEEE80211_HT_CAP_SGI_40)) ? 1 : 0; */
			short_GI_20 =
				(pmlmeinfo->ht_cap.cap_info &
				 cpu_to_le16(IEEE80211_HT_CAP_SGI_20)) ? 1:0;
			short_GI_40 =
				(pmlmeinfo->ht_cap.cap_info &
				 cpu_to_le16(IEEE80211_HT_CAP_SGI_40)) ? 1:0;

			rf_type = rtl8723a_get_rf_type(adapter);
			max_rate = rtw_mcs_rate23a(rf_type, bw_40MHz &
						pregistrypriv->cbw40_enable,
						short_GI_20, short_GI_40,
						&pmlmeinfo->ht_cap.mcs);
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
