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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 
******************************************************************************/
#include <rtl8192c_sreset.h>
#include <rtl8192c_hal.h>
#ifdef DBG_CONFIG_ERROR_DETECT
extern void rtw_cancel_all_timer(_adapter *padapter);

void rtl8192c_sreset_init_value(_adapter *padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);	
	struct sreset_priv *psrtpriv = &pHalData->srestpriv;
	
	_rtw_mutex_init(&psrtpriv->silentreset_mutex );
	psrtpriv->silent_reset_inprogress = _FALSE;
	psrtpriv->Wifi_Error_Status = WIFI_STATUS_SUCCESS;
	psrtpriv->last_tx_time =0;
	psrtpriv->last_tx_complete_time =0;	
}
void rtl8192c_sreset_reset_value(_adapter *padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);	
	struct sreset_priv *psrtpriv = &pHalData->srestpriv;	
	psrtpriv->silent_reset_inprogress = _FALSE;
	psrtpriv->Wifi_Error_Status = WIFI_STATUS_SUCCESS;
	psrtpriv->last_tx_time =0;
	psrtpriv->last_tx_complete_time =0;
}
	
static void _restore_security_setting(_adapter *padapter)
{
	u8 EntryId = 0;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;	
	struct sta_priv * pstapriv = &padapter->stapriv;
	struct sta_info *psta;
	struct security_priv* psecuritypriv=&(padapter->securitypriv);	
	struct mlme_ext_info	*pmlmeinfo = &padapter->mlmeextpriv.mlmext_info;

	(pmlmeinfo->auth_algo == dot11AuthAlgrthm_8021X)
		? rtw_write8(padapter, REG_SECCFG, 0xcc)
		: rtw_write8(padapter, REG_SECCFG, 0xcf);
	
	if (	( padapter->securitypriv.dot11PrivacyAlgrthm == _WEP40_ ) ||
		( padapter->securitypriv.dot11PrivacyAlgrthm == _WEP104_ ))		
	{

		for(EntryId=0; EntryId<4; EntryId++)
		{
			if(EntryId == psecuritypriv->dot11PrivacyKeyIndex)
				rtw_set_key(padapter,&padapter->securitypriv, EntryId, 1);
			else
				rtw_set_key(padapter,&padapter->securitypriv, EntryId, 0);
		}	

	}
	else if((padapter->securitypriv.dot11PrivacyAlgrthm == _TKIP_) ||
		(padapter->securitypriv.dot11PrivacyAlgrthm == _AES_))
	{
		psta = rtw_get_stainfo(pstapriv, get_bssid(pmlmepriv));				
		if (psta == NULL) {
			//DEBUG_ERR( ("Set wpa_set_encryption: Obtain Sta_info fail \n"));
		}
		else
		{			
			//pairwise key
			rtw_setstakey_cmd(padapter, (unsigned char *)psta, _TRUE);
			//group key			
			rtw_set_key(padapter,&padapter->securitypriv,padapter->securitypriv.dot118021XGrpKeyid, 0);
		}
	}
	
}

static void _restore_network_status(_adapter *padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX	*pnetwork = (WLAN_BSSID_EX*)(&(pmlmeinfo->network));
	unsigned short	caps;
	u8	join_type;
#if 1

	//=======================================================
	// reset related register of Beacon control 
	
	//set MSR to nolink		
	Set_NETYPE0_MSR(padapter, _HW_STATE_NOLINK_);		
	// reject all data frame
	rtw_write16(padapter, REG_RXFLTMAP2,0x00);		
	//reset TSF
	rtw_write8(padapter, REG_DUAL_TSF_RST, (BIT(0)|BIT(1)));

	//disable update TSF
	if(IS_NORMAL_CHIP(pHalData->VersionID))
		rtw_write8(padapter, REG_BCN_CTRL, rtw_read8(padapter, REG_BCN_CTRL)|BIT(4));		
	else	
		rtw_write8(padapter, REG_BCN_CTRL, rtw_read8(padapter, REG_BCN_CTRL)|BIT(4)|BIT(5));					

	//=======================================================
	rtw_joinbss_reset(padapter);
	set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);
	//pmlmeinfo->assoc_AP_vendor = maxAP;
	
	if (padapter->registrypriv.wifi_spec) {
		// for WiFi test, follow WMM test plan spec
		rtw_write32(padapter, REG_EDCA_VO_PARAM, 0x002F431C);
		rtw_write32(padapter, REG_EDCA_VI_PARAM, 0x005E541C);
		rtw_write32(padapter, REG_EDCA_BE_PARAM, 0x0000A525);
		rtw_write32(padapter, REG_EDCA_BK_PARAM, 0x0000A549);
	
                // for WiFi test, mixed mode with intel STA under bg mode throughput issue
	        if (padapter->mlmepriv.htpriv.ht_option == 0)
		     rtw_write32(padapter, REG_EDCA_BE_PARAM, 0x00004320);

	} else {
		rtw_write32(padapter, REG_EDCA_VO_PARAM, 0x002F3217);
		rtw_write32(padapter, REG_EDCA_VI_PARAM, 0x005E4317);
		rtw_write32(padapter, REG_EDCA_BE_PARAM, 0x00105320);
		rtw_write32(padapter, REG_EDCA_BK_PARAM, 0x0000A444);
	}
	
	//disable dynamic functions, such as high power, DIG
	//Switch_DM_Func(padapter, DYNAMIC_FUNC_DISABLE, _FALSE);	
#endif

	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_BSSID, pmlmeinfo->network.MacAddress);
	join_type = 0;
	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_MLME_JOIN, (u8 *)(&join_type));

	Set_NETYPE0_MSR(padapter, (pmlmeinfo->state & 0x3));

	mlmeext_joinbss_event_callback(padapter, 1);
	//restore Sequence No.
	rtw_write8(padapter,0x4dc,padapter->xmitpriv.nqos_ssn);	
}
void rtl8192c_silentreset_for_specific_platform(_adapter *padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);	
	struct sreset_priv *psrtpriv = &pHalData->srestpriv;
	
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;		
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;	
	_irqL irqL;

#ifdef DBG_CONFIG_ERROR_RESET

	DBG_871X("%s\n", __FUNCTION__);

	psrtpriv->Wifi_Error_Status = WIFI_STATUS_SUCCESS;

	if (!netif_queue_stopped(padapter->pnetdev))
		netif_stop_queue(padapter->pnetdev);
		
	rtw_cancel_all_timer(padapter);	
	tasklet_kill(&pxmitpriv->xmit_tasklet);	

	_enter_critical_mutex(&psrtpriv->silentreset_mutex, &irqL);
	psrtpriv->silent_reset_inprogress = _TRUE;
	pwrpriv->change_rfpwrstate = rf_off;	
#ifdef CONFIG_IPS
	ips_enter(padapter);								
	ips_leave(padapter);
#endif
	if(check_fwstate(pmlmepriv, _FW_LINKED)== _TRUE)
	{
		_restore_network_status(padapter);
		_restore_security_setting(padapter);	
	}
	
	_clr_fwstate_(pmlmepriv, _FW_UNDER_SURVEY | _FW_UNDER_LINKING);
	
	psrtpriv->silent_reset_inprogress = _FALSE;
	_exit_critical_mutex(&psrtpriv->silentreset_mutex, &irqL);
		
	tasklet_hi_schedule(&pxmitpriv->xmit_tasklet);
	_set_timer(&padapter->mlmepriv.dynamic_chk_timer, 2000);	
				
	if (netif_queue_stopped(padapter->pnetdev))
		netif_wake_queue(padapter->pnetdev);
#endif
}

void rtl8192c_sreset_xmit_status_check(_adapter *padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);	
	struct sreset_priv *psrtpriv = &pHalData->srestpriv;	
	
	unsigned long current_time;
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	unsigned int diff_time;
	u32 txdma_status;
	if( (txdma_status=rtw_read32(padapter, REG_TXDMA_STATUS)) !=0x00){
		DBG_871X("%s REG_TXDMA_STATUS:0x%08x\n", __FUNCTION__, txdma_status);
		rtl8192c_silentreset_for_specific_platform(padapter);						
	}
	
	//total xmit irp = 4
	//DBG_8192C("==>%s free_xmitbuf_cnt(%d),txirp_cnt(%d)\n",__FUNCTION__,pxmitpriv->free_xmitbuf_cnt,pxmitpriv->txirp_cnt);
	//if(pxmitpriv->txirp_cnt == NR_XMITBUFF+1)
	current_time = rtw_get_current_time();
	if(0==pxmitpriv->free_xmitbuf_cnt)
	{
		diff_time = jiffies_to_msecs(current_time - psrtpriv->last_tx_time);
			
		if(diff_time > 2000){
			if(psrtpriv->last_tx_complete_time==0){
				psrtpriv->last_tx_complete_time = current_time;
			}
			else{
				diff_time = jiffies_to_msecs(current_time - psrtpriv->last_tx_complete_time);
				if(diff_time > 4000){
					//padapter->Wifi_Error_Status = WIFI_TX_HANG;
					DBG_8192C("%s tx hang\n", __FUNCTION__);
					rtl8192c_silentreset_for_specific_platform(padapter);	
				}
			}
		}	
	}	
}
void rtl8192c_sreset_linked_status_check(_adapter *padapter)
{
	u32 regc50,regc58,reg824,reg800;
	regc50 = rtw_read32(padapter,0xc50);
	regc58 = rtw_read32(padapter,0xc58);
	reg824 = rtw_read32(padapter,0x824);
	reg800 = rtw_read32(padapter,0x800);	
	if(	((regc50&0xFFFFFF00)!= 0x69543400)||
		((regc58&0xFFFFFF00)!= 0x69543400)||
		(((reg824&0xFFFFFF00)!= 0x00390000)&&(((reg824&0xFFFFFF00)!= 0x80390000)))||
		( ((reg800&0xFFFFFF00)!= 0x03040000)&&((reg800&0xFFFFFF00)!= 0x83040000)))
	{
		DBG_8192C("%s regc50:0x%08x, regc58:0x%08x, reg824:0x%08x, reg800:0x%08x,\n", __FUNCTION__,
			regc50, regc58, reg824, reg800);
		rtl8192c_silentreset_for_specific_platform(padapter);	
	}
}

#ifdef DBG_CONFIG_ERROR_DETECT
u8 rtl8192c_sreset_get_wifi_status(_adapter *padapter)	
{
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
}
#endif

#endif
