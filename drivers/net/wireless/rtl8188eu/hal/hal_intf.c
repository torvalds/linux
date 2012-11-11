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

#define _HAL_INIT_C_
#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <rtw_byteorder.h>

#include <hal_intf.h>

#ifdef CONFIG_SDIO_HCI
	#include <sdio_hal.h>
#elif defined(CONFIG_USB_HCI)
	#include <usb_hal.h>
#endif

void rtw_hal_chip_configure(_adapter *padapter)
{
	if(padapter->HalFunc.intf_chip_configure)
		padapter->HalFunc.intf_chip_configure(padapter);
}

void rtw_hal_read_chip_info(_adapter *padapter)
{
	if(padapter->HalFunc.read_adapter_info)
		padapter->HalFunc.read_adapter_info(padapter);
}

void rtw_hal_read_chip_version(_adapter *padapter)
{
	if(padapter->HalFunc.read_chip_version)
		padapter->HalFunc.read_chip_version(padapter);
}

void rtw_hal_def_value_init(_adapter *padapter)
{
	if(padapter->HalFunc.init_default_value)
		padapter->HalFunc.init_default_value(padapter);
}
void	rtw_hal_free_data(_adapter *padapter)
{
	if(padapter->HalFunc.free_hal_data)
		padapter->HalFunc.free_hal_data(padapter);
}
void	rtw_hal_dm_init(_adapter *padapter)
{
	if(padapter->HalFunc.dm_init)
		padapter->HalFunc.dm_init(padapter);
}
void rtw_hal_dm_deinit(_adapter *padapter)
{
	// cancel dm  timer
	if(padapter->HalFunc.dm_deinit)
		padapter->HalFunc.dm_deinit(padapter);	
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

uint	 rtw_hal_init(_adapter *padapter) 
{
	uint	status = _SUCCESS;

#ifdef CONFIG_DUALMAC_CONCURRENT
	if(padapter->hw_init_completed == _TRUE)
	{
		DBG_871X("rtw_hal_init: hw_init_completed == _TRUE\n");
		return status;
	}

	// before init mac0, driver must init mac1 first to avoid usb rx error.
	if((padapter->pbuddy_adapter != NULL) && (padapter->DualMacConcurrent == _TRUE)
		&& (padapter->adapter_type == PRIMARY_ADAPTER))
	{
		if(padapter->pbuddy_adapter->hw_init_completed == _TRUE)
		{
			DBG_871X("rtw_hal_init: pbuddy_adapter hw_init_completed == _TRUE\n");
		}
		else
		{
			status = 	padapter->HalFunc.hal_init(padapter->pbuddy_adapter);
			if(status == _SUCCESS){
				padapter->pbuddy_adapter->hw_init_completed = _TRUE;
			}
			else{
			 	padapter->pbuddy_adapter->hw_init_completed = _FALSE;
				RT_TRACE(_module_hal_init_c_,_drv_err_,("rtw_hal_init: hal__init fail(pbuddy_adapter)\n"));
				return status;
			}
		}
	}
#endif

	padapter->hw_init_completed=_FALSE;

	status = padapter->HalFunc.hal_init(padapter);

	if(status == _SUCCESS){
		padapter->hw_init_completed = _TRUE;
	}
	else{
	 	padapter->hw_init_completed = _FALSE;
		DBG_871X("rtw_hal_init: hal__init fail\n");
	}

	RT_TRACE(_module_hal_init_c_,_drv_err_,("-rtl871x_hal_init:status=0x%x\n",status));

	return status;

}	

uint rtw_hal_deinit(_adapter *padapter)
{
	uint	status = _SUCCESS;
	
_func_enter_;

	status = padapter->HalFunc.hal_deinit(padapter);

	if(status == _SUCCESS){
		padapter->hw_init_completed = _FALSE;
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
	if (padapter->HalFunc.SetHwRegHandler)
		padapter->HalFunc.SetHwRegHandler(padapter, variable, val);
}

void rtw_hal_get_hwreg(_adapter *padapter, u8 variable, u8 *val)
{
	if (padapter->HalFunc.GetHwRegHandler)
		padapter->HalFunc.GetHwRegHandler(padapter, variable, val);
}

u8 rtw_hal_set_def_var(_adapter *padapter, HAL_DEF_VARIABLE eVariable, PVOID pValue)
{	
	if(padapter->HalFunc.SetHalDefVarHandler)
		return padapter->HalFunc.SetHalDefVarHandler(padapter,eVariable,pValue);
	return _FAIL;
}
u8 rtw_hal_get_def_var(_adapter *padapter, HAL_DEF_VARIABLE eVariable, PVOID pValue)
{	
	if(padapter->HalFunc.GetHalDefVarHandler)
		return padapter->HalFunc.GetHalDefVarHandler(padapter,eVariable,pValue);
	return _FAIL;	
}	

void rtw_hal_set_odm_var(_adapter *padapter, HAL_ODM_VARIABLE eVariable, PVOID pValue1,BOOLEAN bSet)
{
	if(padapter->HalFunc.SetHalODMVarHandler)
		padapter->HalFunc.SetHalODMVarHandler(padapter,eVariable,pValue1,bSet);
}
void	rtw_hal_get_odm_var(_adapter *padapter, HAL_ODM_VARIABLE eVariable, PVOID pValue1,BOOLEAN bSet)
{
	if(padapter->HalFunc.GetHalODMVarHandler)
		padapter->HalFunc.GetHalODMVarHandler(padapter,eVariable,pValue1,bSet);
}

void rtw_hal_enable_interrupt(_adapter *padapter)
{
	if (padapter->HalFunc.enable_interrupt)
		padapter->HalFunc.enable_interrupt(padapter);
	else 
		DBG_871X("%s: HalFunc.enable_interrupt is NULL!\n", __FUNCTION__);
	
}
void rtw_hal_disable_interrupt(_adapter *padapter)
{
	if (padapter->HalFunc.disable_interrupt)
		padapter->HalFunc.disable_interrupt(padapter);
	else 
		DBG_871X("%s: HalFunc.disable_interrupt is NULL!\n", __FUNCTION__);
	
}


u32	rtw_hal_inirp_init(_adapter *padapter)
{
	u32 rst = _FAIL;
	if(padapter->HalFunc.inirp_init)	
		rst = padapter->HalFunc.inirp_init(padapter);	
	else		
		DBG_871X(" %s Initialize dvobjpriv.inirp_init error!!!\n",__FUNCTION__);		
	return rst;
}
	
u32	rtw_hal_inirp_deinit(_adapter *padapter)
{
	
	if(padapter->HalFunc.inirp_deinit)
		return padapter->HalFunc.inirp_deinit(padapter);

	return _FAIL;
		
}

u8	rtw_hal_intf_ps_func(_adapter *padapter,HAL_INTF_PS_FUNC efunc_id, u8* val)
{	
	if(padapter->HalFunc.interface_ps_func)	
		return padapter->HalFunc.interface_ps_func(padapter,efunc_id,val);
	return _FAIL;
}

s32	rtw_hal_xmit(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	if(padapter->HalFunc.hal_xmit)
		return padapter->HalFunc.hal_xmit(padapter, pxmitframe);

	return _FALSE;	
}

void	rtw_hal_mgnt_xmit(_adapter *padapter, struct xmit_frame *pmgntframe)
{
	if(padapter->HalFunc.mgnt_xmit)
		padapter->HalFunc.mgnt_xmit(padapter, pmgntframe);
}
s32	rtw_hal_init_xmit_priv(_adapter *padapter)
{	
	if(padapter->HalFunc.init_xmit_priv != NULL)
		return padapter->HalFunc.init_xmit_priv(padapter);
	return _FAIL;
}
void	rtw_hal_free_xmit_priv(_adapter *padapter)
{
	if(padapter->HalFunc.free_xmit_priv != NULL)
		padapter->HalFunc.free_xmit_priv(padapter);
}

s32	rtw_hal_init_recv_priv(_adapter *padapter)
{	
	if(padapter->HalFunc.init_recv_priv)
		return padapter->HalFunc.init_recv_priv(padapter);

	return _FAIL;
}
void	rtw_hal_free_recv_priv(_adapter *padapter)
{	
	if(padapter->HalFunc.free_recv_priv)
		padapter->HalFunc.free_recv_priv(padapter);
}

void rtw_hal_update_ra_mask(_adapter *padapter, u32 mac_id, u8 rssi_level)
{
	if(padapter->HalFunc.UpdateRAMaskHandler)
		padapter->HalFunc.UpdateRAMaskHandler(padapter,mac_id,rssi_level);
}

void	rtw_hal_add_ra_tid(_adapter *padapter, u32 bitmap, u8 arg)
{
	if(padapter->HalFunc.Add_RateATid)
		padapter->HalFunc.Add_RateATid(padapter, bitmap, arg);
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

s32	rtw_hal_interrupt_handler(_adapter *padapter)
{
	if(padapter->HalFunc.interrupt_handler)
		return padapter->HalFunc.interrupt_handler(padapter);
	return _FAIL;
}

void	rtw_hal_set_bwmode(_adapter *padapter, HT_CHANNEL_WIDTH Bandwidth, u8 Offset)
{
	if(padapter->HalFunc.set_bwmode_handler)
		padapter->HalFunc.set_bwmode_handler(padapter, Bandwidth, Offset);
}

void	rtw_hal_set_chan(_adapter *padapter, u8 channel)
{
	if(padapter->HalFunc.set_channel_handler)
		padapter->HalFunc.set_channel_handler(padapter, channel);
}

void	rtw_hal_dm_watchdog(_adapter *padapter)
{
	if(padapter->HalFunc.hal_dm_watchdog)
		padapter->HalFunc.hal_dm_watchdog(padapter);
}

void rtw_hal_bcn_related_reg_setting(_adapter *padapter)
{
	if(padapter->HalFunc.SetBeaconRelatedRegistersHandler)
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
	if(padapter->HalFunc.sreset_init_value)
		padapter->HalFunc.sreset_init_value(padapter); 
}
void rtw_hal_sreset_reset(_adapter *padapter)
{
	if(padapter->HalFunc.silentreset)
		padapter->HalFunc.silentreset(padapter);	
}

void rtw_hal_silent_reset(_adapter *padapter)
{
	if(padapter->HalFunc.sreset_reset_value)
		padapter->HalFunc.sreset_reset_value(padapter);
}

void rtw_hal_sreset_xmit_status_check(_adapter *padapter)
{
	if(padapter->HalFunc.sreset_xmit_status_check)
		padapter->HalFunc.sreset_xmit_status_check(padapter);		
}
void rtw_hal_sreset_linked_status_check(_adapter *padapter)
{
	if(padapter->HalFunc.sreset_linked_status_check)
		padapter->HalFunc.sreset_linked_status_check(padapter);	
}
u8   rtw_hal_sreset_get_wifi_status(_adapter *padapter)
{
	u8 status = 0;
	if(padapter->HalFunc.sreset_get_wifi_status)				
		status = padapter->HalFunc.sreset_get_wifi_status(padapter);       
	return status;
}

#endif	//DBG_CONFIG_ERROR_DETECT

#ifdef CONFIG_IOL
int rtw_hal_iol_cmd(ADAPTER *adapter, struct xmit_frame *xmit_frame, u32 max_wating_ms)
{
	if(adapter->HalFunc.IOL_exec_cmds_sync)
		return adapter->HalFunc.IOL_exec_cmds_sync(adapter, xmit_frame, max_wating_ms);
	return _FAIL;
}
#endif

#ifdef CONFIG_XMIT_THREAD_MODE
s32 rtw_hal_xmit_thread_handler(_adapter *padapter)
{
	if(padapter->HalFunc.xmit_thread_handler)
		return padapter->HalFunc.xmit_thread_handler(padapter);
	return _FAIL;
}
#endif


