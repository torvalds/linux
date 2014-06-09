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
		 ("+%s: fw_state =%x\n", __func__, get_fwstate(pmlmepriv)));

	if (!padapter) {
		res = _FAIL;
		goto exit;
	}
	if (padapter->hw_init_completed == false) {
		res = _FAIL;
		RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_err_,
			 ("%s: hw_init_completed == false ===\n", __func__));
		goto exit;
	}

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY | _FW_UNDER_LINKING) ||
	    (pmlmepriv->LinkDetectInfo.bBusyTraffic == true)) {
		/*  Scan or linking is in progress, do nothing. */
		RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_err_,
			 ("%s fail since fw_state = %x\n", __func__,
			  get_fwstate(pmlmepriv)));

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
