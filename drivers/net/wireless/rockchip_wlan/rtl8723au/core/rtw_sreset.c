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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/

#include<rtw_sreset.h>

void sreset_init_value(_adapter *padapter)
{
#if defined(DBG_CONFIG_ERROR_DETECT)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct sreset_priv *psrtpriv = &pHalData->srestpriv;

	_rtw_mutex_init(&psrtpriv->silentreset_mutex);
	psrtpriv->silent_reset_inprogress = _FALSE;
	psrtpriv->Wifi_Error_Status = WIFI_STATUS_SUCCESS;
	psrtpriv->last_tx_time =0;
	psrtpriv->last_tx_complete_time =0;
#endif
}
void sreset_reset_value(_adapter *padapter)
{
#if defined(DBG_CONFIG_ERROR_DETECT)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct sreset_priv *psrtpriv = &pHalData->srestpriv;

	//psrtpriv->silent_reset_inprogress = _FALSE;
	psrtpriv->Wifi_Error_Status = WIFI_STATUS_SUCCESS;
	psrtpriv->last_tx_time =0;
	psrtpriv->last_tx_complete_time =0;
#endif
}

u8 sreset_get_wifi_status(_adapter *padapter)
{
#if defined(DBG_CONFIG_ERROR_DETECT)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct sreset_priv *psrtpriv = &pHalData->srestpriv;

	u8 status = WIFI_STATUS_SUCCESS;
	u32 val32 = 0;
	_irqL irqL;
	if(psrtpriv->silent_reset_inprogress == _TRUE)
        {
		return status;
	}
	val32 =rtw_read32(padapter,REG_TXDMA_STATUS);
	if(val32==0xeaeaeaea){
		psrtpriv->Wifi_Error_Status = WIFI_IF_NOT_EXIST;
	}
	else if(val32!=0){
		DBG_8192C("txdmastatu(%x)\n",val32);
		psrtpriv->Wifi_Error_Status = WIFI_MAC_TXDMA_ERROR;
	}

	if(WIFI_STATUS_SUCCESS !=psrtpriv->Wifi_Error_Status)
	{
		DBG_8192C("==>%s error_status(0x%x) \n",__FUNCTION__,psrtpriv->Wifi_Error_Status);
		status = (psrtpriv->Wifi_Error_Status &( ~(USB_READ_PORT_FAIL|USB_WRITE_PORT_FAIL)));
	}
	DBG_8192C("==> %s wifi_status(0x%x)\n",__FUNCTION__,status);

	//status restore
	psrtpriv->Wifi_Error_Status = WIFI_STATUS_SUCCESS;

	return status;
#else
	return WIFI_STATUS_SUCCESS;
#endif
}

void sreset_set_wifi_error_status(_adapter *padapter, u32 status)
{
#if defined(DBG_CONFIG_ERROR_DETECT)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	pHalData->srestpriv.Wifi_Error_Status = status;
#endif
}

void sreset_set_trigger_point(_adapter *padapter, s32 tgp)
{
#if defined(DBG_CONFIG_ERROR_DETECT)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	pHalData->srestpriv.dbg_trigger_point = tgp;
#endif
}

bool sreset_inprogress(_adapter *padapter)
{
#if defined(DBG_CONFIG_ERROR_RESET)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	return pHalData->srestpriv.silent_reset_inprogress;
#else
	return _FALSE;
#endif
}

void sreset_restore_security_station(_adapter *padapter)
{
	u8 EntryId = 0;
	struct mlme_priv *mlmepriv = &padapter->mlmepriv;
	struct sta_priv * pstapriv = &padapter->stapriv;
	struct sta_info *psta;
	struct security_priv* psecuritypriv=&(padapter->securitypriv);
	struct mlme_ext_info	*pmlmeinfo = &padapter->mlmeextpriv.mlmext_info;

	{
		u8 val8;

		if (pmlmeinfo->auth_algo == dot11AuthAlgrthm_8021X) {
			val8 = 0xcc;
		#ifdef CONFIG_WAPI_SUPPORT
		} else if (padapter->wapiInfo.bWapiEnable && pmlmeinfo->auth_algo == dot11AuthAlgrthm_WAPI) {
			//Disable TxUseDefaultKey, RxUseDefaultKey, RxBroadcastUseDefaultKey.
			val8 = 0x4c;
		#endif
		} else {
			val8 = 0xcf;
		}
		rtw_hal_set_hwreg(padapter, HW_VAR_SEC_CFG, (u8 *)(&val8));
	}

	#if 0
	if (	( padapter->securitypriv.dot11PrivacyAlgrthm == _WEP40_ ) ||
		( padapter->securitypriv.dot11PrivacyAlgrthm == _WEP104_ ))
	{

		for(EntryId=0; EntryId<4; EntryId++)
		{
			if(EntryId == psecuritypriv->dot11PrivacyKeyIndex)
				rtw_set_key(padapter,&padapter->securitypriv, EntryId, 1,_FALSE);
			else
				rtw_set_key(padapter,&padapter->securitypriv, EntryId, 0,_FALSE);
		}

	}
	else
	#endif
	if((padapter->securitypriv.dot11PrivacyAlgrthm == _TKIP_) ||
		(padapter->securitypriv.dot11PrivacyAlgrthm == _AES_))
	{
		psta = rtw_get_stainfo(pstapriv, get_bssid(mlmepriv));
		if (psta == NULL) {
			//DEBUG_ERR( ("Set wpa_set_encryption: Obtain Sta_info fail \n"));
		}
		else
		{
			//pairwise key
			rtw_setstakey_cmd(padapter, psta, _TRUE,_FALSE);
			//group key
			rtw_set_key(padapter,&padapter->securitypriv,padapter->securitypriv.dot118021XGrpKeyid, 0,_FALSE);
		}
	}
}

void sreset_restore_network_station(_adapter *padapter)
{
	struct mlme_priv *mlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	#if 0
	{
	//=======================================================
	// reset related register of Beacon control

	//set MSR to nolink
	Set_MSR(padapter, _HW_STATE_NOLINK_);		
	// reject all data frame
	rtw_write16(padapter, REG_RXFLTMAP2,0x00);
	//reset TSF
	rtw_write8(padapter, REG_DUAL_TSF_RST, (BIT(0)|BIT(1)));

	// disable update TSF
	SetBcnCtrlReg(padapter, BIT(4), 0);

	//=======================================================
	}
	#endif
	
	rtw_setopmode_cmd(padapter, Ndis802_11Infrastructure,_FALSE);

	{
		u8 threshold;
		#ifdef CONFIG_USB_HCI
		// TH=1 => means that invalidate usb rx aggregation
		// TH=0 => means that validate usb rx aggregation, use init value.
		if(mlmepriv->htpriv.ht_option) {
			if(padapter->registrypriv.wifi_spec==1)		
				threshold = 1;
			else
				threshold = 0;
			rtw_hal_set_hwreg(padapter, HW_VAR_RXDMA_AGG_PG_TH, (u8 *)(&threshold));
		} else {
			threshold = 1;
			rtw_hal_set_hwreg(padapter, HW_VAR_RXDMA_AGG_PG_TH, (u8 *)(&threshold));
		}
		#endif
	}

	set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);

	//disable dynamic functions, such as high power, DIG
	//Switch_DM_Func(padapter, DYNAMIC_FUNC_DISABLE, _FALSE);
	
	rtw_hal_set_hwreg(padapter, HW_VAR_BSSID, pmlmeinfo->network.MacAddress);

	{
		u8	join_type = 0;
		rtw_hal_set_hwreg(padapter, HW_VAR_MLME_JOIN, (u8 *)(&join_type));
	}

	Set_MSR(padapter, (pmlmeinfo->state & 0x3));

	mlmeext_joinbss_event_callback(padapter, 1);
	//restore Sequence No.
	rtw_write8(padapter,0x4dc,padapter->xmitpriv.nqos_ssn);

	sreset_restore_security_station(padapter);
}


void sreset_restore_network_status(_adapter *padapter)
{
	struct mlme_priv *mlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if (check_fwstate(mlmepriv, WIFI_STATION_STATE)) {
		DBG_871X(FUNC_ADPT_FMT" fwstate:0x%08x - WIFI_STATION_STATE\n", FUNC_ADPT_ARG(padapter), get_fwstate(mlmepriv));
		sreset_restore_network_station(padapter);
	} else if (check_fwstate(mlmepriv, WIFI_AP_STATE)) {
		DBG_871X(FUNC_ADPT_FMT" fwstate:0x%08x - WIFI_AP_STATE\n", FUNC_ADPT_ARG(padapter), get_fwstate(mlmepriv));
		rtw_ap_restore_network(padapter);
	} else if (check_fwstate(mlmepriv, WIFI_ADHOC_STATE)) {
		DBG_871X(FUNC_ADPT_FMT" fwstate:0x%08x - WIFI_ADHOC_STATE\n", FUNC_ADPT_ARG(padapter), get_fwstate(mlmepriv));
	} else {
		DBG_871X(FUNC_ADPT_FMT" fwstate:0x%08x - ???\n", FUNC_ADPT_ARG(padapter), get_fwstate(mlmepriv));
	}
}

void sreset_stop_adapter(_adapter *padapter)
{
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;

	if (padapter == NULL)
		return;

	DBG_871X(FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(padapter));

	if (!rtw_netif_queue_stopped(padapter->pnetdev))
		rtw_netif_stop_queue(padapter->pnetdev);

	rtw_cancel_all_timer(padapter);

	/* TODO: OS and HCI independent */
	#if defined(PLATFORM_LINUX) && defined(CONFIG_USB_HCI)
	tasklet_kill(&pxmitpriv->xmit_tasklet);
	#endif

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY))
		rtw_scan_abort(padapter);

	if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING))
	{
		rtw_set_roaming(padapter, 0);
		_rtw_join_timeout_handler(padapter);
	}

}

void sreset_start_adapter(_adapter *padapter)
{
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;

	if (padapter == NULL)
		return;

	DBG_871X(FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(padapter));

	if (check_fwstate(pmlmepriv, _FW_LINKED)) {
		sreset_restore_network_status(padapter);
	}

	/* TODO: OS and HCI independent */
	#if defined(PLATFORM_LINUX) && defined(CONFIG_USB_HCI)
	tasklet_hi_schedule(&pxmitpriv->xmit_tasklet);
	#endif

	_set_timer(&padapter->mlmepriv.dynamic_chk_timer, 2000);

	if (rtw_netif_queue_stopped(padapter->pnetdev))
		rtw_netif_wake_queue(padapter->pnetdev);

}

void sreset_reset(_adapter *padapter)
{
#ifdef DBG_CONFIG_ERROR_RESET
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct sreset_priv *psrtpriv = &pHalData->srestpriv;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	_irqL irqL;
	u32 start = rtw_get_current_time();

	DBG_871X("%s\n", __FUNCTION__);

	psrtpriv->Wifi_Error_Status = WIFI_STATUS_SUCCESS;

	_enter_pwrlock(&pwrpriv->lock);

	psrtpriv->silent_reset_inprogress = _TRUE;
	pwrpriv->change_rfpwrstate = rf_off;

	sreset_stop_adapter(padapter);
	#ifdef CONFIG_CONCURRENT_MODE
	sreset_stop_adapter(padapter->pbuddy_adapter);
	#endif

	#ifdef CONFIG_IPS
	_ips_enter(padapter);
	_ips_leave(padapter);
	#endif

	sreset_start_adapter(padapter);
	#ifdef CONFIG_CONCURRENT_MODE
	sreset_start_adapter(padapter->pbuddy_adapter);
	#endif

	psrtpriv->silent_reset_inprogress = _FALSE;

	_exit_pwrlock(&pwrpriv->lock);

	DBG_871X("%s done in %d ms\n", __FUNCTION__, rtw_get_passing_time_ms(start));
#endif
}

