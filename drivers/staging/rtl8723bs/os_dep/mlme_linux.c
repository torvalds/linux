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


#define _MLME_OSDEP_C_

#include <drv_types.h>
#include <rtw_debug.h>

static void _dynamic_check_timer_handlder (void *FunctionContext)
{
	struct adapter *adapter = (struct adapter *)FunctionContext;

	rtw_dynamic_check_timer_handlder(adapter);

	_set_timer(&adapter->mlmepriv.dynamic_chk_timer, 2000);
}

static void _rtw_set_scan_deny_timer_hdl(void *FunctionContext)
{
	struct adapter *adapter = (struct adapter *)FunctionContext;
	rtw_set_scan_deny_timer_hdl(adapter);
}

void rtw_init_mlme_timer(struct adapter *padapter)
{
	struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;

	_init_timer(&(pmlmepriv->assoc_timer), padapter->pnetdev, _rtw_join_timeout_handler, padapter);
	/* _init_timer(&(pmlmepriv->sitesurveyctrl.sitesurvey_ctrl_timer), padapter->pnetdev, sitesurvey_ctrl_handler, padapter); */
	_init_timer(&(pmlmepriv->scan_to_timer), padapter->pnetdev, rtw_scan_timeout_handler, padapter);

	_init_timer(&(pmlmepriv->dynamic_chk_timer), padapter->pnetdev, _dynamic_check_timer_handlder, padapter);

	_init_timer(&(pmlmepriv->set_scan_deny_timer), padapter->pnetdev, _rtw_set_scan_deny_timer_hdl, padapter);
}

void rtw_os_indicate_connect(struct adapter *adapter)
{
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);

	if ((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) ==true) ||
		(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) ==true))
	{
		rtw_cfg80211_ibss_indicate_connect(adapter);
	}
	else
		rtw_cfg80211_indicate_connect(adapter);

	rtw_indicate_wx_assoc_event(adapter);
	netif_carrier_on(adapter->pnetdev);

	if (adapter->pid[2] != 0)
		rtw_signal_process(adapter->pid[2], SIGALRM);
}

void rtw_os_indicate_scan_done(struct adapter *padapter, bool aborted)
{
	rtw_cfg80211_indicate_scan_done(padapter, aborted);
	indicate_wx_scan_complete_event(padapter);
}

static RT_PMKID_LIST   backupPMKIDList[ NUM_PMKID_CACHE ];
void rtw_reset_securitypriv(struct adapter *adapter)
{
	u8 backupPMKIDIndex = 0;
	u8 backupTKIPCountermeasure = 0x00;
	u32 backupTKIPcountermeasure_time = 0;
	/*  add for CONFIG_IEEE80211W, none 11w also can use */
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;

	spin_lock_bh(&adapter->security_key_mutex);

	if (adapter->securitypriv.dot11AuthAlgrthm == dot11AuthAlgrthm_8021X)/* 802.1x */
	{
		/*  Added by Albert 2009/02/18 */
		/*  We have to backup the PMK information for WiFi PMK Caching test item. */
		/*  */
		/*  Backup the btkip_countermeasure information. */
		/*  When the countermeasure is trigger, the driver have to disconnect with AP for 60 seconds. */

		memset(&backupPMKIDList[ 0 ], 0x00, sizeof(RT_PMKID_LIST) * NUM_PMKID_CACHE);

		memcpy(&backupPMKIDList[ 0 ], &adapter->securitypriv.PMKIDList[ 0 ], sizeof(RT_PMKID_LIST) * NUM_PMKID_CACHE);
		backupPMKIDIndex = adapter->securitypriv.PMKIDIndex;
		backupTKIPCountermeasure = adapter->securitypriv.btkip_countermeasure;
		backupTKIPcountermeasure_time = adapter->securitypriv.btkip_countermeasure_time;

		/* reset RX BIP packet number */
		pmlmeext->mgnt_80211w_IPN_rx = 0;

		memset((unsigned char *)&adapter->securitypriv, 0, sizeof (struct security_priv));

		/*  Added by Albert 2009/02/18 */
		/*  Restore the PMK information to securitypriv structure for the following connection. */
		memcpy(&adapter->securitypriv.PMKIDList[ 0 ], &backupPMKIDList[ 0 ], sizeof(RT_PMKID_LIST) * NUM_PMKID_CACHE);
		adapter->securitypriv.PMKIDIndex = backupPMKIDIndex;
		adapter->securitypriv.btkip_countermeasure = backupTKIPCountermeasure;
		adapter->securitypriv.btkip_countermeasure_time = backupTKIPcountermeasure_time;

		adapter->securitypriv.ndisauthtype = Ndis802_11AuthModeOpen;
		adapter->securitypriv.ndisencryptstatus = Ndis802_11WEPDisabled;

	}
	else /* reset values in securitypriv */
	{
		/* if (adapter->mlmepriv.fw_state & WIFI_STATION_STATE) */
		/*  */
		struct security_priv *psec_priv =&adapter->securitypriv;

		psec_priv->dot11AuthAlgrthm =dot11AuthAlgrthm_Open;  /* open system */
		psec_priv->dot11PrivacyAlgrthm = _NO_PRIVACY_;
		psec_priv->dot11PrivacyKeyIndex = 0;

		psec_priv->dot118021XGrpPrivacy = _NO_PRIVACY_;
		psec_priv->dot118021XGrpKeyid = 1;

		psec_priv->ndisauthtype = Ndis802_11AuthModeOpen;
		psec_priv->ndisencryptstatus = Ndis802_11WEPDisabled;
		/*  */
	}
	/*  add for CONFIG_IEEE80211W, none 11w also can use */
	spin_unlock_bh(&adapter->security_key_mutex);
}

void rtw_os_indicate_disconnect(struct adapter *adapter)
{
	/* RT_PMKID_LIST   backupPMKIDList[ NUM_PMKID_CACHE ]; */

	netif_carrier_off(adapter->pnetdev); /*  Do it first for tx broadcast pkt after disconnection issue! */

	rtw_cfg80211_indicate_disconnect(adapter);

	rtw_indicate_wx_disassoc_event(adapter);

	 /* modify for CONFIG_IEEE80211W, none 11w also can use the same command */
	 rtw_reset_securitypriv_cmd(adapter);
}

void rtw_report_sec_ie(struct adapter *adapter, u8 authmode, u8 *sec_ie)
{
	uint	len;
	u8 *buff,*p, i;
	union iwreq_data wrqu;

	RT_TRACE(_module_mlme_osdep_c_, _drv_info_, ("+rtw_report_sec_ie, authmode =%d\n", authmode));

	buff = NULL;
	if (authmode == _WPA_IE_ID_)
	{
		RT_TRACE(_module_mlme_osdep_c_, _drv_info_, ("rtw_report_sec_ie, authmode =%d\n", authmode));

		buff = rtw_zmalloc(IW_CUSTOM_MAX);
		if (NULL == buff) {
			DBG_871X(FUNC_ADPT_FMT ": alloc memory FAIL!!\n",
				FUNC_ADPT_ARG(adapter));
			return;
		}
		p = buff;

		p+=sprintf(p,"ASSOCINFO(ReqIEs =");

		len = sec_ie[1]+2;
		len = (len < IW_CUSTOM_MAX) ? len:IW_CUSTOM_MAX;

		for (i = 0;i<len;i++) {
			p+=sprintf(p,"%02x", sec_ie[i]);
		}

		p+=sprintf(p,")");

		memset(&wrqu, 0, sizeof(wrqu));

		wrqu.data.length =p-buff;

		wrqu.data.length = (wrqu.data.length<IW_CUSTOM_MAX) ? wrqu.data.length:IW_CUSTOM_MAX;

		kfree(buff);
	}
}

void init_addba_retry_timer(struct adapter *padapter, struct sta_info *psta)
{
	_init_timer(&psta->addba_retry_timer, padapter->pnetdev, addba_timer_hdl, psta);
}

void init_mlme_ext_timer(struct adapter *padapter)
{
	struct	mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	_init_timer(&pmlmeext->survey_timer, padapter->pnetdev, survey_timer_hdl, padapter);
	_init_timer(&pmlmeext->link_timer, padapter->pnetdev, link_timer_hdl, padapter);
	_init_timer(&pmlmeext->sa_query_timer, padapter->pnetdev, sa_query_timer_hdl, padapter);
}
