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

#include <rtw_sreset.h>
#include <usb_ops_linux.h>

void rtw_sreset_init(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	struct sreset_priv *psrtpriv = &pHalData->srestpriv;

	mutex_init(&psrtpriv->silentreset_mutex);
	psrtpriv->silent_reset_inprogress = false;
	psrtpriv->Wifi_Error_Status = WIFI_STATUS_SUCCESS;
	psrtpriv->last_tx_time = 0;
	psrtpriv->last_tx_complete_time = 0;
}

void rtw_sreset_reset_value(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	struct sreset_priv *psrtpriv = &pHalData->srestpriv;

	psrtpriv->silent_reset_inprogress = false;
	psrtpriv->Wifi_Error_Status = WIFI_STATUS_SUCCESS;
	psrtpriv->last_tx_time = 0;
	psrtpriv->last_tx_complete_time = 0;
}

u8 rtw_sreset_get_wifi_status(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	struct sreset_priv *psrtpriv = &pHalData->srestpriv;
	u8 status = WIFI_STATUS_SUCCESS;
	u32 val32 = 0;

	if (psrtpriv->silent_reset_inprogress)
		return status;
	val32 = rtl8723au_read32(padapter, REG_TXDMA_STATUS);
	if (val32 == 0xeaeaeaea) {
		psrtpriv->Wifi_Error_Status = WIFI_IF_NOT_EXIST;
	} else if (val32 != 0) {
		DBG_8723A("txdmastatu(%x)\n", val32);
		psrtpriv->Wifi_Error_Status = WIFI_MAC_TXDMA_ERROR;
	}

	if (WIFI_STATUS_SUCCESS != psrtpriv->Wifi_Error_Status) {
		DBG_8723A("==>%s error_status(0x%x)\n", __func__, psrtpriv->Wifi_Error_Status);
		status = (psrtpriv->Wifi_Error_Status &~(USB_READ_PORT_FAIL|USB_WRITE_PORT_FAIL));
	}
	DBG_8723A("==> %s wifi_status(0x%x)\n", __func__, status);

	/* status restore */
	psrtpriv->Wifi_Error_Status = WIFI_STATUS_SUCCESS;

	return status;
}

void sreset_set_wifi_error_status23a(struct rtw_adapter *padapter, u32 status)
{
	struct hal_data_8723a	*pHalData = GET_HAL_DATA(padapter);

	pHalData->srestpriv.Wifi_Error_Status = status;
}

void sreset_set_trigger_point(struct rtw_adapter *padapter, s32 tgp)
{
	struct hal_data_8723a	*pHalData = GET_HAL_DATA(padapter);

	pHalData->srestpriv.dbg_trigger_point = tgp;
}

bool rtw_sreset_inprogress(struct rtw_adapter *padapter)
{
	struct rtw_adapter *primary_adapter = GET_PRIMARY_ADAPTER(padapter);
	struct hal_data_8723a *pHalData = GET_HAL_DATA(primary_adapter);

	return pHalData->srestpriv.silent_reset_inprogress;
}

static void sreset_restore_security_station(struct rtw_adapter *padapter)
{
	struct mlme_priv *mlmepriv = &padapter->mlmepriv;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *psta;
	struct mlme_ext_info *pmlmeinfo = &padapter->mlmeextpriv.mlmext_info;
	u8 val8;

	if (pmlmeinfo->auth_algo == dot11AuthAlgrthm_8021X)
		val8 = 0xcc;
	else
		val8 = 0xcf;

	rtl8723a_set_sec_cfg(padapter, val8);

	if (padapter->securitypriv.dot11PrivacyAlgrthm ==
	    WLAN_CIPHER_SUITE_TKIP ||
	    padapter->securitypriv.dot11PrivacyAlgrthm ==
	    WLAN_CIPHER_SUITE_CCMP) {
		psta = rtw_get_stainfo23a(pstapriv, get_bssid(mlmepriv));
		if (psta == NULL) {
			/* DEBUG_ERR(("Set wpa_set_encryption: Obtain Sta_info fail\n")); */
		} else {
			/* pairwise key */
			rtw_setstakey_cmd23a(padapter, (unsigned char *)psta, true);
			/* group key */
			rtw_set_key23a(padapter,&padapter->securitypriv, padapter->securitypriv.dot118021XGrpKeyid, 0);
		}
	}
}

static void sreset_restore_network_station(struct rtw_adapter *padapter)
{
	struct mlme_priv *mlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	u8 threshold;

	rtw_setopmode_cmd23a(padapter, NL80211_IFTYPE_STATION);

	/*  TH = 1 => means that invalidate usb rx aggregation */
	/*  TH = 0 => means that validate usb rx aggregation, use init value. */
	if (mlmepriv->htpriv.ht_option) {
		if (padapter->registrypriv.wifi_spec == 1)
			threshold = 1;
		else
			threshold = 0;
	} else
		threshold = 1;

	rtl8723a_set_rxdma_agg_pg_th(padapter, threshold);

	set_channel_bwmode23a(padapter, pmlmeext->cur_channel,
			      pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);

	hw_var_set_bssid(padapter, pmlmeinfo->network.MacAddress);
	hw_var_set_mlme_join(padapter, 0);

	Set_MSR23a(padapter, (pmlmeinfo->state & 0x3));

	mlmeext_joinbss_event_callback23a(padapter, 1);
	/* restore Sequence No. */
	rtl8723au_write8(padapter, 0x4dc, padapter->xmitpriv.nqos_ssn);

	sreset_restore_security_station(padapter);
}

static void sreset_restore_network_status(struct rtw_adapter *padapter)
{
	struct mlme_priv *mlmepriv = &padapter->mlmepriv;

	if (check_fwstate(mlmepriv, WIFI_STATION_STATE)) {
		DBG_8723A("%s(%s): fwstate:0x%08x - WIFI_STATION_STATE\n",
			  __func__, padapter->pnetdev->name,
			  get_fwstate(mlmepriv));
		sreset_restore_network_station(padapter);
#ifdef CONFIG_8723AU_AP_MODE
	} else if (check_fwstate(mlmepriv, WIFI_AP_STATE)) {
		DBG_8723A("%s(%s): fwstate:0x%08x - WIFI_AP_STATE\n",
			  __func__, padapter->pnetdev->name,
			  get_fwstate(mlmepriv));
		rtw_ap_restore_network(padapter);
#endif
	} else if (check_fwstate(mlmepriv, WIFI_ADHOC_STATE)) {
		DBG_8723A("%s(%s): fwstate:0x%08x - WIFI_ADHOC_STATE\n",
			  __func__, padapter->pnetdev->name,
			  get_fwstate(mlmepriv));
	} else {
		DBG_8723A("%s(%s): fwstate:0x%08x - ???\n", __func__,
			  padapter->pnetdev->name, get_fwstate(mlmepriv));
	}
}

static void sreset_stop_adapter(struct rtw_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;

	if (padapter == NULL)
		return;

	DBG_8723A("%s(%s)\n", __func__, padapter->pnetdev->name);

	if (!rtw_netif_queue_stopped(padapter->pnetdev))
		netif_tx_stop_all_queues(padapter->pnetdev);

	rtw_cancel_all_timer23a(padapter);

	/* TODO: OS and HCI independent */
	tasklet_kill(&pxmitpriv->xmit_tasklet);

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY))
		rtw_scan_abort23a(padapter);

	if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING))
		rtw23a_join_to_handler((unsigned long)padapter);
}

static void sreset_start_adapter(struct rtw_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;

	if (padapter == NULL)
		return;

	DBG_8723A("%s(%s)\n", __func__, padapter->pnetdev->name);

	if (check_fwstate(pmlmepriv, _FW_LINKED)) {
		sreset_restore_network_status(padapter);
	}

	/* TODO: OS and HCI independent */
	tasklet_hi_schedule(&pxmitpriv->xmit_tasklet);

	mod_timer(&padapter->mlmepriv.dynamic_chk_timer,
		  jiffies + msecs_to_jiffies(2000));

	if (rtw_netif_queue_stopped(padapter->pnetdev))
		netif_tx_wake_all_queues(padapter->pnetdev);
}

void rtw_sreset_reset(struct rtw_adapter *active_adapter)
{
	struct rtw_adapter *padapter = GET_PRIMARY_ADAPTER(active_adapter);
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	struct sreset_priv *psrtpriv = &pHalData->srestpriv;
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	unsigned long start = jiffies;

	DBG_8723A("%s\n", __func__);

	psrtpriv->Wifi_Error_Status = WIFI_STATUS_SUCCESS;

	mutex_lock(&psrtpriv->silentreset_mutex);
	psrtpriv->silent_reset_inprogress = true;
	pwrpriv->change_rfpwrstate = rf_off;

	sreset_stop_adapter(padapter);

	ips_enter23a(padapter);
	ips_leave23a(padapter);

	sreset_start_adapter(padapter);
	psrtpriv->silent_reset_inprogress = false;
	mutex_unlock(&psrtpriv->silentreset_mutex);

	DBG_8723A("%s done in %d ms\n", __func__,
		  jiffies_to_msecs(jiffies - start));
}
