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

#define _HAL_INTF_C_

#include <drv_types.h>
#include <hal_data.h>

void rtw_hal_chip_configure(_adapter *padapter)
{
	padapter->HalFunc.intf_chip_configure(padapter);
}

void rtw_hal_read_chip_info(_adapter *padapter)
{
	padapter->HalFunc.read_adapter_info(padapter);
}

void rtw_hal_read_chip_version(_adapter *padapter)
{
	padapter->HalFunc.read_chip_version(padapter);
}

void rtw_hal_def_value_init(_adapter *padapter)
{
	if (is_primary_adapter(padapter)) {
		padapter->HalFunc.init_default_value(padapter);

		rtw_init_hal_com_default_value(padapter);

		{
			struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
			HAL_DATA_TYPE *hal_data = GET_HAL_DATA(padapter);

			/* hal_data..macid_num is ready here */
			dvobj->macid_ctl.num = rtw_min(hal_data->macid_num, MACID_NUM_SW_LIMIT);
		}
	}
}

u8 rtw_hal_data_init(_adapter *padapter)
{
	if (is_primary_adapter(padapter)) {
		padapter->hal_data_sz = sizeof(HAL_DATA_TYPE);
		padapter->HalData = rtw_zvmalloc(padapter->hal_data_sz);
		if(padapter->HalData == NULL){
			DBG_8192C("cant not alloc memory for HAL DATA \n");
			return _FAIL;
		}
	}
	return _SUCCESS;
}

void rtw_hal_data_deinit(_adapter *padapter)
{	
	if (is_primary_adapter(padapter)) {
		if (padapter->HalData) 
		{
			#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
			phy_free_filebuf(padapter);				
			#endif
			rtw_vmfree(padapter->HalData, padapter->hal_data_sz);
			padapter->HalData = NULL;
			padapter->hal_data_sz = 0;
		}	
	}
}

void	rtw_hal_free_data(_adapter *padapter)
{
	//free HAL Data 	
	rtw_hal_data_deinit(padapter);	
}
void rtw_hal_dm_init(_adapter *padapter)
{
	if (is_primary_adapter(padapter)) {
		PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
		
		padapter->HalFunc.dm_init(padapter);

		_rtw_spinlock_init(&pHalData->IQKSpinLock);
	}
}
void rtw_hal_dm_deinit(_adapter *padapter)
{
	if (is_primary_adapter(padapter)) {
		PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);		

		padapter->HalFunc.dm_deinit(padapter);

		_rtw_spinlock_free(&pHalData->IQKSpinLock);
	}
}
void	rtw_hal_sw_led_init(_adapter *padapter)
{
	if(padapter->HalFunc.InitSwLeds)
		padapter->HalFunc.InitSwLeds(padapter);
}

void rtw_hal_sw_led_deinit(_adapter *padapter)
{
	if(padapter->HalFunc.DeInitSwLeds)
		padapter->HalFunc.DeInitSwLeds(padapter);
}

u32 rtw_hal_power_on(_adapter *padapter)
{	
	return padapter->HalFunc.hal_power_on(padapter);
}
void rtw_hal_power_off(_adapter *padapter)
{	
	padapter->HalFunc.hal_power_off(padapter);	
}


void rtw_hal_init_opmode(_adapter *padapter) 
{
	NDIS_802_11_NETWORK_INFRASTRUCTURE networkType = Ndis802_11InfrastructureMax;
	struct  mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	sint fw_state;

	fw_state = get_fwstate(pmlmepriv);

	if (fw_state & WIFI_ADHOC_STATE) 
		networkType = Ndis802_11IBSS;
	else if (fw_state & WIFI_STATION_STATE)
		networkType = Ndis802_11Infrastructure;
	else if (fw_state & WIFI_AP_STATE)
		networkType = Ndis802_11APMode;
	else
		return;

	rtw_setopmode_cmd(padapter, networkType, _FALSE); 
}

uint	 rtw_hal_init(_adapter *padapter) 
{
	uint	status = _SUCCESS;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	int i;

	status = padapter->HalFunc.hal_init(padapter);

	if(status == _SUCCESS){

		for (i = 0; i<dvobj->iface_nums; i++)
			dvobj->padapters[i]->hw_init_completed = _TRUE;
			
		if (padapter->registrypriv.notch_filter == 1)
			rtw_hal_notch_filter(padapter, 1);

		rtw_hal_reset_security_engine(padapter);

		for (i = 0; i<dvobj->iface_nums; i++)
			rtw_sec_restore_wep_key(dvobj->padapters[i]);

		rtw_led_control(padapter, LED_CTL_POWER_ON);

		init_hw_mlme_ext(padapter);

                rtw_hal_init_opmode(padapter);
		
#ifdef CONFIG_RF_GAIN_OFFSET
		rtw_bb_rf_gain_offset(padapter);
#endif //CONFIG_RF_GAIN_OFFSET

	}
	else{
		for (i = 0; i<dvobj->iface_nums; i++)
			dvobj->padapters[i]->hw_init_completed = _FALSE;
		DBG_871X("rtw_hal_init: hal__init fail\n");
	}

	RT_TRACE(_module_hal_init_c_,_drv_err_,("-rtl871x_hal_init:status=0x%x\n",status));

	return status;

}	

uint rtw_hal_deinit(_adapter *padapter)
{
	uint	status = _SUCCESS;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	int i;
_func_enter_;

	status = padapter->HalFunc.hal_deinit(padapter);

	if(status == _SUCCESS){
		rtw_led_control(padapter, LED_CTL_POWER_OFF);
		for (i = 0; i<dvobj->iface_nums; i++) {
			padapter = dvobj->padapters[i];
			padapter->hw_init_completed = _FALSE;
		}
	}
	else
	{
		DBG_871X("\n rtw_hal_deinit: hal_init fail\n");
	}

_func_exit_;
	
	return status;
}

void rtw_hal_set_hwreg(_adapter *padapter, u8 variable, u8 *val)
{
	padapter->HalFunc.SetHwRegHandler(padapter, variable, val);
}

void rtw_hal_get_hwreg(_adapter *padapter, u8 variable, u8 *val)
{
	padapter->HalFunc.GetHwRegHandler(padapter, variable, val);
}

#ifdef CONFIG_C2H_PACKET_EN
void rtw_hal_set_hwreg_with_buf(_adapter *padapter, u8 variable, u8 *pbuf, int len)
{
	if (padapter->HalFunc.SetHwRegHandlerWithBuf)
		padapter->HalFunc.SetHwRegHandlerWithBuf(padapter, variable, pbuf, len);
}
#endif

u8 rtw_hal_set_def_var(_adapter *padapter, HAL_DEF_VARIABLE eVariable, PVOID pValue)
{	
	return padapter->HalFunc.SetHalDefVarHandler(padapter,eVariable,pValue);
}
u8 rtw_hal_get_def_var(_adapter *padapter, HAL_DEF_VARIABLE eVariable, PVOID pValue)
{	
	return padapter->HalFunc.GetHalDefVarHandler(padapter,eVariable,pValue);		
}	

void rtw_hal_set_odm_var(_adapter *padapter, HAL_ODM_VARIABLE eVariable, PVOID pValue1,BOOLEAN bSet)
{
	padapter->HalFunc.SetHalODMVarHandler(padapter,eVariable,pValue1,bSet);
}
void	rtw_hal_get_odm_var(_adapter *padapter, HAL_ODM_VARIABLE eVariable, PVOID pValue1,PVOID pValue2)
{
	padapter->HalFunc.GetHalODMVarHandler(padapter,eVariable,pValue1,pValue2);
}

/* FOR SDIO & PCIE */
void rtw_hal_enable_interrupt(_adapter *padapter)
{
#if defined(CONFIG_PCI_HCI) || defined (CONFIG_SDIO_HCI) || defined (CONFIG_GSPI_HCI)
	padapter->HalFunc.enable_interrupt(padapter);	
#endif //#if defined(CONFIG_PCI_HCI) || defined (CONFIG_SDIO_HCI) || defined (CONFIG_GSPI_HCI)
}

/* FOR SDIO & PCIE */
void rtw_hal_disable_interrupt(_adapter *padapter)
{
#if defined(CONFIG_PCI_HCI) || defined (CONFIG_SDIO_HCI) || defined (CONFIG_GSPI_HCI)
	padapter->HalFunc.disable_interrupt(padapter);
#endif //#if defined(CONFIG_PCI_HCI) || defined (CONFIG_SDIO_HCI) || defined (CONFIG_GSPI_HCI)
}


u8 rtw_hal_check_ips_status(_adapter *padapter)
{
	u8 val = _FALSE;
	if (padapter->HalFunc.check_ips_status)
		val = padapter->HalFunc.check_ips_status(padapter);
	else 
		DBG_871X("%s: HalFunc.check_ips_status is NULL!\n", __FUNCTION__);
	
	return val;
}

#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
void rtw_hal_clear_interrupt(_adapter *padapter)
{  
#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
	padapter->HalFunc.clear_interrupt(padapter);
#endif
}
void rtw_hal_set_wowlan_fw(_adapter *padapter, u8 sleep)
{
	padapter->HalFunc.hal_set_wowlan_fw(padapter, sleep);
}

#endif

#if defined(CONFIG_USB_HCI) || defined (CONFIG_PCI_HCI)
u32	rtw_hal_inirp_init(_adapter *padapter)
{
	if (is_primary_adapter(padapter)) 		
		return padapter->HalFunc.inirp_init(padapter);	
	 return _SUCCESS;
}
u32	rtw_hal_inirp_deinit(_adapter *padapter)
{

	if (is_primary_adapter(padapter)) 	
		return padapter->HalFunc.inirp_deinit(padapter);

	return _SUCCESS;
}
#endif //#if defined(CONFIG_USB_HCI) || defined (CONFIG_PCI_HCI)

#if defined(CONFIG_PCI_HCI)
void	rtw_hal_irp_reset(_adapter *padapter)
{
	padapter->HalFunc.irp_reset(padapter);
}
#endif //#if defined(CONFIG_PCI_HCI)

/* for USB Auto-suspend */
u8	rtw_hal_intf_ps_func(_adapter *padapter,HAL_INTF_PS_FUNC efunc_id, u8* val)
{	
	if(padapter->HalFunc.interface_ps_func)	
		return padapter->HalFunc.interface_ps_func(padapter,efunc_id,val);
	return _FAIL;
}

s32	rtw_hal_xmitframe_enqueue(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	return padapter->HalFunc.hal_xmitframe_enqueue(padapter, pxmitframe);
}

s32	rtw_hal_xmit(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	return padapter->HalFunc.hal_xmit(padapter, pxmitframe);
}

/*
 * [IMPORTANT] This function would be run in interrupt context.
 */
s32	rtw_hal_mgnt_xmit(_adapter *padapter, struct xmit_frame *pmgntframe)
{
	s32 ret = _FAIL;
	unsigned char	*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	update_mgntframe_attrib_addr(padapter, pmgntframe);
	//pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	//pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;
	//_rtw_memcpy(pmgntframe->attrib.ra, pwlanhdr->addr1, ETH_ALEN);

#ifdef CONFIG_IEEE80211W
	if(padapter->securitypriv.binstallBIPkey == _TRUE)
	{
		if(IS_MCAST(pmgntframe->attrib.ra))
		{
			pmgntframe->attrib.encrypt = _BIP_;
			//pmgntframe->attrib.bswenc = _TRUE;
		}	
		else
		{
			pmgntframe->attrib.encrypt = _AES_;
			pmgntframe->attrib.bswenc = _TRUE;
		}
		rtw_mgmt_xmitframe_coalesce(padapter, pmgntframe->pkt, pmgntframe);
	}
#endif //CONFIG_IEEE80211W
	
	ret = padapter->HalFunc.mgnt_xmit(padapter, pmgntframe);
	return ret;
}

s32	rtw_hal_init_xmit_priv(_adapter *padapter)
{	
	return padapter->HalFunc.init_xmit_priv(padapter);	
}
void	rtw_hal_free_xmit_priv(_adapter *padapter)
{
	padapter->HalFunc.free_xmit_priv(padapter);
}

s32	rtw_hal_init_recv_priv(_adapter *padapter)
{	
	return padapter->HalFunc.init_recv_priv(padapter);
}
void	rtw_hal_free_recv_priv(_adapter *padapter)
{
	padapter->HalFunc.free_recv_priv(padapter);
}

void rtw_hal_update_ra_mask(struct sta_info *psta, u8 rssi_level)
{
	_adapter *padapter;
	struct mlme_priv *pmlmepriv;

	if(!psta)
		return;

	padapter = psta->padapter;

	pmlmepriv = &(padapter->mlmepriv);
	
	if(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
	{
		add_RATid(padapter, psta, rssi_level);
	}
	else
	{
		padapter->HalFunc.UpdateRAMaskHandler(padapter, psta->mac_id, rssi_level);
	}
}

void	rtw_hal_add_ra_tid(_adapter *padapter, u32 bitmap, u8* arg, u8 rssi_level)
{
	padapter->HalFunc.Add_RateATid(padapter, bitmap, arg, rssi_level);
}

/*	Start specifical interface thread		*/
void	rtw_hal_start_thread(_adapter *padapter)
{
#if defined(CONFIG_SDIO_HCI) || defined (CONFIG_GSPI_HCI)
#ifndef CONFIG_SDIO_TX_TASKLET	
	padapter->HalFunc.run_thread(padapter);	
#endif
#endif
}
/*	Start specifical interface thread		*/
void	rtw_hal_stop_thread(_adapter *padapter)
{
#if defined(CONFIG_SDIO_HCI) || defined (CONFIG_GSPI_HCI)
#ifndef CONFIG_SDIO_TX_TASKLET
	
	padapter->HalFunc.cancel_thread(padapter);
	
#endif
#endif	
}

u32	rtw_hal_read_bbreg(_adapter *padapter, u32 RegAddr, u32 BitMask)
{
	u32 data = 0;
	if(padapter->HalFunc.read_bbreg)
		 data = padapter->HalFunc.read_bbreg(padapter, RegAddr, BitMask);
	return data;
}
void	rtw_hal_write_bbreg(_adapter *padapter, u32 RegAddr, u32 BitMask, u32 Data)
{
	if(padapter->HalFunc.write_bbreg)
		padapter->HalFunc.write_bbreg(padapter, RegAddr, BitMask, Data);
}

u32	rtw_hal_read_rfreg(_adapter *padapter, u32 eRFPath, u32 RegAddr, u32 BitMask)
{
	u32 data = 0;
	if( padapter->HalFunc.read_rfreg)
		data = padapter->HalFunc.read_rfreg(padapter, eRFPath, RegAddr, BitMask);
	return data;
}
void	rtw_hal_write_rfreg(_adapter *padapter, u32 eRFPath, u32 RegAddr, u32 BitMask, u32 Data)
{
	if(padapter->HalFunc.write_rfreg)
		padapter->HalFunc.write_rfreg(padapter, eRFPath, RegAddr, BitMask, Data);	
}
#if defined(CONFIG_PCI_HCI)
s32	rtw_hal_interrupt_handler(_adapter *padapter)
{
	s32 ret = _FAIL;
	ret = padapter->HalFunc.interrupt_handler(padapter);
	return ret;
}
#endif
void	rtw_hal_set_bwmode(_adapter *padapter, CHANNEL_WIDTH Bandwidth, u8 Offset)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PDM_ODM_T		pDM_Odm = &(pHalData->odmpriv);
	
	ODM_AcquireSpinLock( pDM_Odm, RT_IQK_SPINLOCK);
	if(pDM_Odm->RFCalibrateInfo.bIQKInProgress == _TRUE)
		DBG_871X_LEVEL(_drv_err_, "%s, %d, IQK may race condition\n", __func__,__LINE__);
	ODM_ReleaseSpinLock( pDM_Odm, RT_IQK_SPINLOCK);
	padapter->HalFunc.set_bwmode_handler(padapter, Bandwidth, Offset);
	
}

void	rtw_hal_set_chan(_adapter *padapter, u8 channel)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PDM_ODM_T		pDM_Odm = &(pHalData->odmpriv);
	
	ODM_AcquireSpinLock( pDM_Odm, RT_IQK_SPINLOCK);
	if(pDM_Odm->RFCalibrateInfo.bIQKInProgress == _TRUE)
		DBG_871X_LEVEL(_drv_err_, "%s, %d, IQK may race condition\n", __func__,__LINE__);
	ODM_ReleaseSpinLock( pDM_Odm, RT_IQK_SPINLOCK);
	padapter->HalFunc.set_channel_handler(padapter, channel);	
}

void	rtw_hal_set_chnl_bw(_adapter *padapter, u8 channel, CHANNEL_WIDTH Bandwidth, u8 Offset40, u8 Offset80)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PDM_ODM_T		pDM_Odm = &(pHalData->odmpriv);
	
	ODM_AcquireSpinLock( pDM_Odm, RT_IQK_SPINLOCK);
	if(pDM_Odm->RFCalibrateInfo.bIQKInProgress == _TRUE)
		DBG_871X_LEVEL(_drv_err_, "%s, %d, IQK may race condition\n", __func__,__LINE__);
	ODM_ReleaseSpinLock( pDM_Odm, RT_IQK_SPINLOCK);
	padapter->HalFunc.set_chnl_bw_handler(padapter, channel, Bandwidth, Offset40, Offset80);	
}

void	rtw_hal_set_tx_power_level(_adapter *padapter, u8 channel)
{
	if(padapter->HalFunc.set_tx_power_level_handler)
		padapter->HalFunc.set_tx_power_level_handler(padapter, channel);
}

void	rtw_hal_get_tx_power_level(_adapter *padapter, s32 *powerlevel)
{
	if(padapter->HalFunc.get_tx_power_level_handler)
		padapter->HalFunc.get_tx_power_level_handler(padapter, powerlevel);
}

void	rtw_hal_dm_watchdog(_adapter *padapter)
{
	if (!is_primary_adapter(padapter))
		return;

	padapter->HalFunc.hal_dm_watchdog(padapter);
	
}

#ifdef CONFIG_LPS_LCLK_WD_TIMER
void	rtw_hal_dm_watchdog_in_lps(_adapter *padapter)
{
#if defined(CONFIG_CONCURRENT_MODE)
	if (padapter->iface_type != IFACE_PORT0)
		return;
#endif	

	if (adapter_to_pwrctl(padapter)->bFwCurrentInPSMode ==_TRUE ) {
		padapter->HalFunc.hal_dm_watchdog_in_lps(padapter);//this fuction caller is in interrupt context				 	
	}
}
#endif

void rtw_hal_bcn_related_reg_setting(_adapter *padapter)
{	
	padapter->HalFunc.SetBeaconRelatedRegistersHandler(padapter);	
}


#ifdef CONFIG_ANTENNA_DIVERSITY
u8	rtw_hal_antdiv_before_linked(_adapter *padapter)
{	
	if(padapter->HalFunc.AntDivBeforeLinkHandler)
		return padapter->HalFunc.AntDivBeforeLinkHandler(padapter);
	return _FALSE;		
}
void	rtw_hal_antdiv_rssi_compared(_adapter *padapter, WLAN_BSSID_EX *dst, WLAN_BSSID_EX *src)
{
	if(padapter->HalFunc.AntDivCompareHandler)
		padapter->HalFunc.AntDivCompareHandler(padapter, dst, src);
}
#endif

#ifdef CONFIG_HOSTAPD_MLME
s32	rtw_hal_hostap_mgnt_xmit_entry(_adapter *padapter, _pkt *pkt)
{
	if(padapter->HalFunc.hostap_mgnt_xmit_entry)
		return padapter->HalFunc.hostap_mgnt_xmit_entry(padapter, pkt);
	return _FAIL;
}
#endif //CONFIG_HOSTAPD_MLME

#ifdef DBG_CONFIG_ERROR_DETECT
void	rtw_hal_sreset_init(_adapter *padapter)
{
	padapter->HalFunc.sreset_init_value(padapter); 
}
void rtw_hal_sreset_reset(_adapter *padapter)
{
	padapter = GET_PRIMARY_ADAPTER(padapter);
	padapter->HalFunc.silentreset(padapter);
}

void rtw_hal_sreset_reset_value(_adapter *padapter)
{
	padapter->HalFunc.sreset_reset_value(padapter);
}

void rtw_hal_sreset_xmit_status_check(_adapter *padapter)
{
	if (!is_primary_adapter(padapter))
		return;

	padapter->HalFunc.sreset_xmit_status_check(padapter);		
}
void rtw_hal_sreset_linked_status_check(_adapter *padapter)
{
	if (!is_primary_adapter(padapter))
		return;
	padapter->HalFunc.sreset_linked_status_check(padapter);	
}
u8   rtw_hal_sreset_get_wifi_status(_adapter *padapter)
{	
	return padapter->HalFunc.sreset_get_wifi_status(padapter);
}

bool rtw_hal_sreset_inprogress(_adapter *padapter)
{
	padapter = GET_PRIMARY_ADAPTER(padapter);
	return padapter->HalFunc.sreset_inprogress(padapter);
}
#endif	//DBG_CONFIG_ERROR_DETECT

#ifdef CONFIG_IOL
int rtw_hal_iol_cmd(ADAPTER *adapter, struct xmit_frame *xmit_frame, u32 max_wating_ms, u32 bndy_cnt)
{
	if(adapter->HalFunc.IOL_exec_cmds_sync)
		return adapter->HalFunc.IOL_exec_cmds_sync(adapter, xmit_frame, max_wating_ms,bndy_cnt);
	return _FAIL;
}
#endif

#ifdef CONFIG_XMIT_THREAD_MODE
s32 rtw_hal_xmit_thread_handler(_adapter *padapter)
{	
	return padapter->HalFunc.xmit_thread_handler(padapter);
}
#endif

void rtw_hal_notch_filter(_adapter *adapter, bool enable)
{
	if(adapter->HalFunc.hal_notch_filter)
		adapter->HalFunc.hal_notch_filter(adapter,enable);		
}

void rtw_hal_reset_security_engine(_adapter * adapter)
{
	if(adapter->HalFunc.hal_reset_security_engine)
		adapter->HalFunc.hal_reset_security_engine(adapter);
}

bool rtw_hal_c2h_valid(_adapter *adapter, u8 *buf)
{
	HAL_DATA_TYPE *HalData = GET_HAL_DATA(adapter);
	HAL_VERSION *hal_ver = &HalData->VersionID;
	bool ret = _FAIL;

	if (IS_8188E(*hal_ver)) {
		ret = c2h_evt_valid((struct c2h_evt_hdr *)buf);
	} else if(IS_8192E(*hal_ver) || IS_8812_SERIES(*hal_ver) || IS_8821_SERIES(*hal_ver) || IS_8723B_SERIES(*hal_ver)) {
		ret = c2h_evt_valid((struct c2h_evt_hdr_88xx*)buf);
	} else {
		rtw_warn_on(1);
	}

	return ret;
}

s32 rtw_hal_c2h_evt_read(_adapter *adapter, u8 *buf)
{
	HAL_DATA_TYPE *HalData = GET_HAL_DATA(adapter);
	HAL_VERSION *hal_ver = &HalData->VersionID;
	s32 ret = _FAIL;

	if (IS_8188E(*hal_ver)) {
		ret = c2h_evt_read(adapter, buf);
	} else if(IS_8192E(*hal_ver) || IS_8812_SERIES(*hal_ver) || IS_8821_SERIES(*hal_ver) || IS_8723B_SERIES(*hal_ver)) {
		ret = c2h_evt_read_88xx(adapter, buf);
	} else {
		rtw_warn_on(1);
	}

	return ret;
}

s32 rtw_hal_c2h_handler(_adapter *adapter, u8 *c2h_evt)
{
	s32 ret = _FAIL;
	if (adapter->HalFunc.c2h_handler)
		ret = adapter->HalFunc.c2h_handler(adapter, c2h_evt);
	return ret;
}

c2h_id_filter rtw_hal_c2h_id_filter_ccx(_adapter *adapter)
{	
	return adapter->HalFunc.c2h_id_filter_ccx;
}

s32 rtw_hal_is_disable_sw_channel_plan(PADAPTER padapter)
{
	return GET_HAL_DATA(padapter)->bDisableSWChannelPlan;
}

s32 rtw_hal_macid_sleep(PADAPTER padapter, u8 macid)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct macid_ctl_t *macid_ctl = dvobj_to_macidctl(dvobj);
	u8 support;

	support = _FALSE;
	rtw_hal_get_def_var(padapter, HAL_DEF_MACID_SLEEP, &support);
	if (_FALSE == support)
		return _FAIL;

	if (macid >= macid_ctl->num) {
		DBG_871X_LEVEL(_drv_err_, FUNC_ADPT_FMT": Invalid macid(%u)\n",
			FUNC_ADPT_ARG(padapter), macid);
		return _FAIL;
	}

	rtw_hal_set_hwreg(padapter, HW_VAR_MACID_SLEEP, &macid);

	return _SUCCESS;
}

s32 rtw_hal_macid_wakeup(PADAPTER padapter, u8 macid)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct macid_ctl_t *macid_ctl = dvobj_to_macidctl(dvobj);
	u8 support;

	support = _FALSE;
	rtw_hal_get_def_var(padapter, HAL_DEF_MACID_SLEEP, &support);
	if (_FALSE == support)
		return _FAIL;

	if (macid >= macid_ctl->num) {
		DBG_871X_LEVEL(_drv_err_, FUNC_ADPT_FMT": Invalid macid(%u)\n",
			FUNC_ADPT_ARG(padapter), macid);
		return _FAIL;
	}

	rtw_hal_set_hwreg(padapter, HW_VAR_MACID_WAKEUP, &macid);

	return _SUCCESS;
}

s32 rtw_hal_fill_h2c_cmd(PADAPTER padapter, u8 ElementID, u32 CmdLen, u8 *pCmdBuffer)
{
	return padapter->HalFunc.fill_h2c_cmd(padapter, ElementID, CmdLen, pCmdBuffer);	
}

void rtw_hal_fill_fake_txdesc(_adapter* padapter, u8* pDesc, u32 BufferLen,
		u8 IsPsPoll, u8 IsBTQosNull, u8 bDataFrame)
{
	padapter->HalFunc.fill_fake_txdesc(padapter, pDesc, BufferLen,IsPsPoll, IsBTQosNull, bDataFrame);

}
u8 rtw_hal_get_txbuff_rsvd_page_num(_adapter *adapter, bool wowlan)
{
	return adapter->HalFunc.hal_get_tx_buff_rsvd_page_num(adapter, wowlan);
}

#ifdef CONFIG_GPIO_API
void rtw_hal_update_hisr_hsisr_ind(_adapter *padapter, u32 flag)
{
	if (padapter->HalFunc.update_hisr_hsisr_ind)
		padapter->HalFunc.update_hisr_hsisr_ind(padapter, flag);
}
#endif

#define rtw_hal_error_msg(ops_fun)		\
	DBG_871X_LEVEL(_drv_always_, "### %s - Error : Please hook HalFunc.%s ###\n",__FUNCTION__,ops_fun)

u8 rtw_hal_ops_check(_adapter *padapter)
{	
	u8 ret = _SUCCESS;
#if 1
	/*** initialize section ***/
	if (NULL == padapter->HalFunc.read_chip_version) {
		rtw_hal_error_msg("read_chip_version");
		ret = _FAIL;
	}	
	if (NULL == padapter->HalFunc.init_default_value) {
		rtw_hal_error_msg("init_default_value");
		ret = _FAIL;
	}
	if (NULL == padapter->HalFunc.intf_chip_configure) {
		rtw_hal_error_msg("intf_chip_configure");
		ret = _FAIL;
	}
	if (NULL == padapter->HalFunc.read_adapter_info) {
		rtw_hal_error_msg("read_adapter_info");
		ret = _FAIL;
	}

	if (NULL == padapter->HalFunc.hal_power_on) {		
		rtw_hal_error_msg("hal_power_on");
		ret = _FAIL;
	}	
	if (NULL == padapter->HalFunc.hal_power_off) {
		rtw_hal_error_msg("hal_power_off");
		ret = _FAIL;
	}
	
	if (NULL == padapter->HalFunc.hal_init) {
		rtw_hal_error_msg("hal_init");
		ret = _FAIL;
	}
	if (NULL == padapter->HalFunc.hal_deinit) {
		rtw_hal_error_msg("hal_deinit");
		ret = _FAIL;
	}
	
	/*** xmit section ***/
	if (NULL == padapter->HalFunc.init_xmit_priv) {
		rtw_hal_error_msg("init_xmit_priv");
		ret = _FAIL;
	}
	if (NULL == padapter->HalFunc.free_xmit_priv) {
		rtw_hal_error_msg("free_xmit_priv");
		ret = _FAIL;
	}
	if (NULL == padapter->HalFunc.hal_xmit) {
		rtw_hal_error_msg("hal_xmit");
		ret = _FAIL;
	}
	if (NULL == padapter->HalFunc.mgnt_xmit) {
		rtw_hal_error_msg("mgnt_xmit");
		ret = _FAIL;
	}
	#ifdef CONFIG_XMIT_THREAD_MODE
	if (NULL == padapter->HalFunc.xmit_thread_handler) {
		rtw_hal_error_msg("xmit_thread_handler");
		ret = _FAIL;
	}
	#endif
	if (NULL == padapter->HalFunc.hal_xmitframe_enqueue) {
		rtw_hal_error_msg("hal_xmitframe_enqueue");
		ret = _FAIL;
	}
	#if defined(CONFIG_SDIO_HCI) || defined (CONFIG_GSPI_HCI)
	#ifndef CONFIG_SDIO_TX_TASKLET
	if (NULL == padapter->HalFunc.run_thread) {
		rtw_hal_error_msg("run_thread");
		ret = _FAIL;
	}
	if (NULL == padapter->HalFunc.cancel_thread) {
		rtw_hal_error_msg("cancel_thread");
		ret = _FAIL;
	}
	#endif
	#endif
	
	/*** recv section ***/
	if (NULL == padapter->HalFunc.init_recv_priv) {
		rtw_hal_error_msg("init_recv_priv");
		ret = _FAIL;
	}
	if (NULL == padapter->HalFunc.free_recv_priv) {
		rtw_hal_error_msg("free_recv_priv");
		ret = _FAIL;
	}
	#if defined(CONFIG_USB_HCI) || defined (CONFIG_PCI_HCI)
	if (NULL == padapter->HalFunc.inirp_init) {
		rtw_hal_error_msg("inirp_init");
		ret = _FAIL;
	}
	if (NULL == padapter->HalFunc.inirp_deinit) {
		rtw_hal_error_msg("inirp_deinit");
		ret = _FAIL;
	}
	#endif //#if defined(CONFIG_USB_HCI) || defined (CONFIG_PCI_HCI)
	
	
	/*** interrupt hdl section ***/
	#if defined(CONFIG_PCI_HCI)
	if (NULL == padapter->HalFunc.irp_reset) {
		rtw_hal_error_msg("irp_reset");
		ret = _FAIL;
	}	
	if (NULL == padapter->HalFunc.interrupt_handler) {
		rtw_hal_error_msg("interrupt_handler");
		ret = _FAIL;
	}
	#endif //#if defined(CONFIG_PCI_HCI)	

	#if defined(CONFIG_PCI_HCI) || defined (CONFIG_SDIO_HCI) || defined (CONFIG_GSPI_HCI)	
	if (NULL == padapter->HalFunc.enable_interrupt) {
		rtw_hal_error_msg("enable_interrupt");
		ret = _FAIL;
	}
	if (NULL == padapter->HalFunc.disable_interrupt) {
		rtw_hal_error_msg("disable_interrupt");
		ret = _FAIL;
	}
	#endif //defined(CONFIG_PCI_HCI) || defined (CONFIG_SDIO_HCI) || defined (CONFIG_GSPI_HCI)
		
	
	/*** DM section ***/
	if (NULL == padapter->HalFunc.dm_init) {
		rtw_hal_error_msg("dm_init");
		ret = _FAIL;
	}
	if (NULL == padapter->HalFunc.dm_deinit) {
		rtw_hal_error_msg("dm_deinit");
		ret = _FAIL;
	}	
	if (NULL == padapter->HalFunc.hal_dm_watchdog) {
		rtw_hal_error_msg("hal_dm_watchdog");
		ret = _FAIL;
	}
	#ifdef CONFIG_LPS_LCLK_WD_TIMER
	if (NULL == padapter->HalFunc.hal_dm_watchdog_in_lps) {
		rtw_hal_error_msg("hal_dm_watchdog_in_lps");
		ret = _FAIL;
	}
	#endif

	/*** xxx section ***/
	if (NULL == padapter->HalFunc.set_bwmode_handler) {
		rtw_hal_error_msg("set_bwmode_handler");
		ret = _FAIL;
	}

	if (NULL == padapter->HalFunc.set_channel_handler) {
		rtw_hal_error_msg("set_channel_handler");
		ret = _FAIL;
	}

	if (NULL == padapter->HalFunc.set_chnl_bw_handler) {
		rtw_hal_error_msg("set_chnl_bw_handler");
		ret = _FAIL;
	}	
	
	if (NULL == padapter->HalFunc.SetHwRegHandler) {
		rtw_hal_error_msg("SetHwRegHandler");
		ret = _FAIL;
	}
	if (NULL == padapter->HalFunc.GetHwRegHandler) {
		rtw_hal_error_msg("GetHwRegHandler");
		ret = _FAIL;
	}
	if (NULL == padapter->HalFunc.GetHalDefVarHandler) {
		rtw_hal_error_msg("GetHalDefVarHandler");
		ret = _FAIL;
	}
	if (NULL == padapter->HalFunc.SetHalDefVarHandler) {
		rtw_hal_error_msg("SetHalDefVarHandler");
		ret = _FAIL;
	}
	if (NULL == padapter->HalFunc.GetHalODMVarHandler) {
		rtw_hal_error_msg("GetHalODMVarHandler");
		ret = _FAIL;
	}
	if (NULL == padapter->HalFunc.SetHalODMVarHandler) {
		rtw_hal_error_msg("SetHalODMVarHandler");
		ret = _FAIL;
	}
	if (NULL == padapter->HalFunc.UpdateRAMaskHandler) {
		rtw_hal_error_msg("UpdateRAMaskHandler");
		ret = _FAIL;
	}	
	
	if (NULL == padapter->HalFunc.SetBeaconRelatedRegistersHandler) {
		rtw_hal_error_msg("SetBeaconRelatedRegistersHandler");
		ret = _FAIL;
	}

	if (NULL == padapter->HalFunc.Add_RateATid) {
		rtw_hal_error_msg("Add_RateATid");
		ret = _FAIL;
	}	

	if (NULL == padapter->HalFunc.fill_h2c_cmd) {
		rtw_hal_error_msg("fill_h2c_cmd");
		ret = _FAIL;
	}
	if (NULL == padapter->HalFunc.fill_fake_txdesc) {
		rtw_hal_error_msg("fill_fake_txdesc");
		ret = _FAIL;
	}
	if (NULL == padapter->HalFunc.hal_get_tx_buff_rsvd_page_num) {
		rtw_hal_error_msg("hal_get_tx_buff_rsvd_page_num");
		ret = _FAIL;
	}

	#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
	#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
	if (NULL == padapter->HalFunc.clear_interrupt) {
		rtw_hal_error_msg("clear_interrupt");
		ret = _FAIL;
	}
	#endif
	if (NULL == padapter->HalFunc.hal_set_wowlan_fw) {
		rtw_hal_error_msg("hal_set_wowlan_fw");
		ret = _FAIL;
	}
	#endif //CONFIG_WOWLAN
	
	
	/*** SReset section ***/
	#ifdef DBG_CONFIG_ERROR_DETECT		
	if (NULL == padapter->HalFunc.sreset_init_value) {
		rtw_hal_error_msg("sreset_init_value");
		ret = _FAIL;
	}
	if (NULL == padapter->HalFunc.sreset_reset_value) {
		rtw_hal_error_msg("sreset_reset_value");
		ret = _FAIL;
	}
	if (NULL == padapter->HalFunc.silentreset) {
		rtw_hal_error_msg("silentreset");
		ret = _FAIL;
	}
	if (NULL == padapter->HalFunc.sreset_xmit_status_check) {
		rtw_hal_error_msg("sreset_xmit_status_check");
		ret = _FAIL;
	}
	if (NULL == padapter->HalFunc.sreset_linked_status_check) {
		rtw_hal_error_msg("sreset_linked_status_check");
		ret = _FAIL;
	}
	if (NULL == padapter->HalFunc.sreset_get_wifi_status) {
		rtw_hal_error_msg("sreset_get_wifi_status");
		ret = _FAIL;
	}
	if (NULL == padapter->HalFunc.sreset_inprogress) {
		rtw_hal_error_msg("sreset_inprogress");
		ret = _FAIL;
	}
	#endif  //#ifdef DBG_CONFIG_ERROR_DETECT

#endif
	return  ret;
}

