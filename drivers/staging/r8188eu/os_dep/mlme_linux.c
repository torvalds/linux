// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. i*/

#define _MLME_OSDEP_C_

#include "../include/osdep_service.h"
#include "../include/drv_types.h"
#include "../include/mlme_osdep.h"

void rtw_join_timeout_handler (struct timer_list *t)
{
	struct adapter *adapter = from_timer(adapter, t, mlmepriv.assoc_timer);

	_rtw_join_timeout_handler(adapter);
}

void _rtw_scan_timeout_handler (struct timer_list *t)
{
	struct adapter *adapter = from_timer(adapter, t, mlmepriv.scan_to_timer);

	rtw_scan_timeout_handler(adapter);
}

static void _dynamic_check_timer_handlder(struct timer_list *t)
{
	struct adapter *adapter = from_timer(adapter, t, mlmepriv.dynamic_chk_timer);

	rtw_dynamic_check_timer_handlder(adapter);
	_set_timer(&adapter->mlmepriv.dynamic_chk_timer, 2000);
}

void rtw_init_mlme_timer(struct adapter *padapter)
{
	struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;

	timer_setup(&pmlmepriv->assoc_timer, rtw_join_timeout_handler, 0);
	timer_setup(&pmlmepriv->scan_to_timer, _rtw_scan_timeout_handler, 0);
	timer_setup(&pmlmepriv->dynamic_chk_timer, _dynamic_check_timer_handlder, 0);
}

void rtw_os_indicate_connect(struct adapter *adapter)
{

	rtw_indicate_wx_assoc_event(adapter);
	netif_carrier_on(adapter->pnetdev);
	if (adapter->pid[2] != 0)
		rtw_signal_process(adapter->pid[2], SIGALRM);

}

void rtw_os_indicate_scan_done(struct adapter *padapter, bool aborted)
{
	indicate_wx_scan_complete_event(padapter);
}

static struct rt_pmkid_list backup_pmkid[NUM_PMKID_CACHE];

void rtw_reset_securitypriv(struct adapter *adapter)
{
	u8	backup_index = 0;
	u8	backup_counter = 0x00;
	u32	backup_time = 0;

	if (adapter->securitypriv.dot11AuthAlgrthm == dot11AuthAlgrthm_8021X) {
		/* 802.1x */
		/*  We have to backup the PMK information for WiFi PMK Caching test item. */
		/*  Backup the btkip_countermeasure information. */
		/*  When the countermeasure is trigger, the driver have to disconnect with AP for 60 seconds. */
		memset(&backup_pmkid[0], 0x00, sizeof(struct rt_pmkid_list) * NUM_PMKID_CACHE);
		memcpy(&backup_pmkid[0], &adapter->securitypriv.PMKIDList[0], sizeof(struct rt_pmkid_list) * NUM_PMKID_CACHE);
		backup_index = adapter->securitypriv.PMKIDIndex;
		backup_counter = adapter->securitypriv.btkip_countermeasure;
		backup_time = adapter->securitypriv.btkip_countermeasure_time;
		memset((unsigned char *)&adapter->securitypriv, 0, sizeof(struct security_priv));

		/*  Restore the PMK information to securitypriv structure for the following connection. */
		memcpy(&adapter->securitypriv.PMKIDList[0],
			    &backup_pmkid[0],
			    sizeof(struct rt_pmkid_list) * NUM_PMKID_CACHE);
		adapter->securitypriv.PMKIDIndex = backup_index;
		adapter->securitypriv.btkip_countermeasure = backup_counter;
		adapter->securitypriv.btkip_countermeasure_time = backup_time;
		adapter->securitypriv.ndisauthtype = Ndis802_11AuthModeOpen;
		adapter->securitypriv.ndisencryptstatus = Ndis802_11WEPDisabled;
	} else {
		/* reset values in securitypriv */
		struct security_priv *psec_priv = &adapter->securitypriv;

		psec_priv->dot11AuthAlgrthm = dot11AuthAlgrthm_Open;  /* open system */
		psec_priv->dot11PrivacyAlgrthm = _NO_PRIVACY_;
		psec_priv->dot11PrivacyKeyIndex = 0;
		psec_priv->dot118021XGrpPrivacy = _NO_PRIVACY_;
		psec_priv->dot118021XGrpKeyid = 1;
		psec_priv->ndisauthtype = Ndis802_11AuthModeOpen;
		psec_priv->ndisencryptstatus = Ndis802_11WEPDisabled;
	}
}

void rtw_os_indicate_disconnect(struct adapter *adapter)
{

	netif_carrier_off(adapter->pnetdev); /*  Do it first for tx broadcast pkt after disconnection issue! */
	rtw_indicate_wx_disassoc_event(adapter);
	rtw_reset_securitypriv(adapter);
}

void rtw_report_sec_ie(struct adapter *adapter, u8 authmode, u8 *sec_ie)
{
	uint	len;
	u8	*buff, *p, i;
	union iwreq_data wrqu;

	buff = NULL;
	if (authmode == _WPA_IE_ID_) {
		buff = kzalloc(IW_CUSTOM_MAX, GFP_KERNEL);
		if (!buff)
			return;
		p = buff;
		p += sprintf(p, "ASSOCINFO(ReqIEs =");
		len = sec_ie[1] + 2;
		len =  (len < IW_CUSTOM_MAX) ? len : IW_CUSTOM_MAX;
		for (i = 0; i < len; i++)
			p += sprintf(p, "%02x", sec_ie[i]);
		p += sprintf(p, ")");
		memset(&wrqu, 0, sizeof(wrqu));
		wrqu.data.length = p - buff;
		wrqu.data.length = (wrqu.data.length < IW_CUSTOM_MAX) ?
				   wrqu.data.length : IW_CUSTOM_MAX;
		wireless_send_event(adapter->pnetdev, IWEVCUSTOM, &wrqu, buff);
		kfree(buff);
	}
}

static void _survey_timer_hdl(struct timer_list *t)
{
	struct adapter *padapter = from_timer(padapter, t, mlmeextpriv.survey_timer);

	survey_timer_hdl(padapter);
}

static void _link_timer_hdl(struct timer_list *t)
{
	struct adapter *padapter = from_timer(padapter, t, mlmeextpriv.link_timer);
	link_timer_hdl(padapter);
}

static void _addba_timer_hdl(struct timer_list *t)
{
	struct sta_info *psta = from_timer(psta, t, addba_retry_timer);
	addba_timer_hdl(psta);
}

void init_addba_retry_timer(struct adapter *padapter, struct sta_info *psta)
{
	timer_setup(&psta->addba_retry_timer, _addba_timer_hdl, 0);
}

void init_mlme_ext_timer(struct adapter *padapter)
{
	struct	mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	timer_setup(&pmlmeext->survey_timer, _survey_timer_hdl, 0);
	timer_setup(&pmlmeext->link_timer, _link_timer_hdl, 0);
}

void rtw_indicate_sta_assoc_event(struct adapter *padapter, struct sta_info *psta)
{
	union iwreq_data wrqu;
	struct sta_priv *pstapriv = &padapter->stapriv;

	if (!psta)
		return;

	if (psta->aid > NUM_STA)
		return;

	if (pstapriv->sta_aid[psta->aid - 1] != psta)
		return;

	wrqu.addr.sa_family = ARPHRD_ETHER;

	memcpy(wrqu.addr.sa_data, psta->hwaddr, ETH_ALEN);

	DBG_88E("+rtw_indicate_sta_assoc_event\n");

	wireless_send_event(padapter->pnetdev, IWEVREGISTERED, &wrqu, NULL);
}

void rtw_indicate_sta_disassoc_event(struct adapter *padapter, struct sta_info *psta)
{
	union iwreq_data wrqu;
	struct sta_priv *pstapriv = &padapter->stapriv;

	if (!psta)
		return;

	if (psta->aid > NUM_STA)
		return;

	if (pstapriv->sta_aid[psta->aid - 1] != psta)
		return;

	wrqu.addr.sa_family = ARPHRD_ETHER;

	memcpy(wrqu.addr.sa_data, psta->hwaddr, ETH_ALEN);

	DBG_88E("+rtw_indicate_sta_disassoc_event\n");

	wireless_send_event(padapter->pnetdev, IWEVEXPIRED, &wrqu, NULL);
}
