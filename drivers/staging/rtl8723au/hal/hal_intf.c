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

#define _HAL_INTF_C_
#include <osdep_service.h>
#include <drv_types.h>

#include <hal_intf.h>

#include <usb_hal.h>
#include <rtl8723a_hal.h>

void rtw_hal_chip_configure23a(struct rtw_adapter *padapter)
{
	if (padapter->HalFunc.intf_chip_configure)
		padapter->HalFunc.intf_chip_configure(padapter);
}

void rtw_hal_read_chip_version23a(struct rtw_adapter *padapter)
{
	if (padapter->HalFunc.read_chip_version)
		padapter->HalFunc.read_chip_version(padapter);
}

void	rtw_hal_free_data23a(struct rtw_adapter *padapter)
{
	if (padapter->HalFunc.free_hal_data)
		padapter->HalFunc.free_hal_data(padapter);
}
void	rtw_hal_sw_led_init23a(struct rtw_adapter *padapter)
{
	if (padapter->HalFunc.InitSwLeds)
		padapter->HalFunc.InitSwLeds(padapter);
}

void rtw_hal_sw_led_deinit23a(struct rtw_adapter *padapter)
{
	if (padapter->HalFunc.DeInitSwLeds)
		padapter->HalFunc.DeInitSwLeds(padapter);
}

u32 rtw_hal_power_on23a(struct rtw_adapter *padapter)
{
	if (padapter->HalFunc.hal_power_on)
		return padapter->HalFunc.hal_power_on(padapter);
	return _FAIL;
}

uint	 rtw_hal_init23a(struct rtw_adapter *padapter)
{
	uint	status = _SUCCESS;

	padapter->hw_init_completed = false;

	status = padapter->HalFunc.hal_init(padapter);

	if (status == _SUCCESS) {
		padapter->hw_init_completed = true;

		if (padapter->registrypriv.notch_filter == 1)
			rtl8723a_notch_filter(padapter, 1);
	} else {
		padapter->hw_init_completed = false;
		DBG_8723A("rtw_hal_init23a: hal__init fail\n");
	}

	RT_TRACE(_module_hal_init_c_, _drv_err_, ("-rtl871x_hal_init:status = 0x%x\n", status));

	return status;
}

uint rtw_hal_deinit23a(struct rtw_adapter *padapter)
{
	uint	status = _SUCCESS;

	status = padapter->HalFunc.hal_deinit(padapter);

	if (status == _SUCCESS)
		padapter->hw_init_completed = false;
	else
		DBG_8723A("\n rtw_hal_deinit23a: hal_init fail\n");
	return status;
}

u8 rtw_hal_set_def_var23a(struct rtw_adapter *padapter, enum hal_def_variable eVariable, void *pValue)
{
	if (padapter->HalFunc.SetHalDefVarHandler)
		return padapter->HalFunc.SetHalDefVarHandler(padapter, eVariable, pValue);
	return _FAIL;
}
u8 rtw_hal_get_def_var23a(struct rtw_adapter *padapter, enum hal_def_variable eVariable, void *pValue)
{
	if (padapter->HalFunc.GetHalDefVarHandler)
		return padapter->HalFunc.GetHalDefVarHandler(padapter, eVariable, pValue);
	return _FAIL;
}

void rtw_hal_set_odm_var23a(struct rtw_adapter *padapter, enum hal_odm_variable eVariable, void *pValue1, bool bSet)
{
	if (padapter->HalFunc.SetHalODMVarHandler)
		padapter->HalFunc.SetHalODMVarHandler(padapter, eVariable, pValue1, bSet);
}
void	rtw_hal_get_odm_var23a(struct rtw_adapter *padapter, enum hal_odm_variable eVariable, void *pValue1, bool bSet)
{
	if (padapter->HalFunc.GetHalODMVarHandler)
		padapter->HalFunc.GetHalODMVarHandler(padapter, eVariable, pValue1, bSet);
}

void rtw_hal_enable_interrupt23a(struct rtw_adapter *padapter)
{
	if (padapter->HalFunc.enable_interrupt)
		padapter->HalFunc.enable_interrupt(padapter);
	else
		DBG_8723A("%s: HalFunc.enable_interrupt is NULL!\n", __FUNCTION__);

}
void rtw_hal_disable_interrupt23a(struct rtw_adapter *padapter)
{
	if (padapter->HalFunc.disable_interrupt)
		padapter->HalFunc.disable_interrupt(padapter);
	else
		DBG_8723A("%s: HalFunc.disable_interrupt is NULL!\n", __FUNCTION__);

}

u32	rtw_hal_inirp_init23a(struct rtw_adapter *padapter)
{
	u32 rst = _FAIL;
	if (padapter->HalFunc.inirp_init)
		rst = padapter->HalFunc.inirp_init(padapter);
	else
		DBG_8723A(" %s HalFunc.inirp_init is NULL!!!\n", __FUNCTION__);
	return rst;
}

u32	rtw_hal_inirp_deinit23a(struct rtw_adapter *padapter)
{

	if (padapter->HalFunc.inirp_deinit)
		return padapter->HalFunc.inirp_deinit(padapter);

	return _FAIL;

}

s32	rtw_hal_xmit23aframe_enqueue(struct rtw_adapter *padapter, struct xmit_frame *pxmitframe)
{
	if (padapter->HalFunc.hal_xmitframe_enqueue)
		return padapter->HalFunc.hal_xmitframe_enqueue(padapter, pxmitframe);

	return false;
}

s32	rtw_hal_xmit23a(struct rtw_adapter *padapter, struct xmit_frame *pxmitframe)
{
	if (padapter->HalFunc.hal_xmit)
		return padapter->HalFunc.hal_xmit(padapter, pxmitframe);

	return false;
}

s32	rtw_hal_mgnt_xmit23a(struct rtw_adapter *padapter, struct xmit_frame *pmgntframe)
{
	s32 ret = _FAIL;
	if (padapter->HalFunc.mgnt_xmit)
		ret = padapter->HalFunc.mgnt_xmit(padapter, pmgntframe);
	return ret;
}

s32	rtw_hal_init23a_xmit_priv(struct rtw_adapter *padapter)
{
	if (padapter->HalFunc.init_xmit_priv != NULL)
		return padapter->HalFunc.init_xmit_priv(padapter);
	return _FAIL;
}

s32	rtw_hal_init23a_recv_priv(struct rtw_adapter *padapter)
{
	if (padapter->HalFunc.init_recv_priv)
		return padapter->HalFunc.init_recv_priv(padapter);

	return _FAIL;
}
void	rtw_hal_free_recv_priv23a(struct rtw_adapter *padapter)
{
	if (padapter->HalFunc.free_recv_priv)
		padapter->HalFunc.free_recv_priv(padapter);
}

void rtw_hal_update_ra_mask23a(struct sta_info *psta, u8 rssi_level)
{
	struct rtw_adapter *padapter;
	struct mlme_priv *pmlmepriv;

	if (!psta)
		return;

	padapter = psta->padapter;

	pmlmepriv = &padapter->mlmepriv;

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
#ifdef CONFIG_8723AU_AP_MODE
		add_RATid23a(padapter, psta, rssi_level);
#endif
	} else {
		if (padapter->HalFunc.UpdateRAMaskHandler)
			padapter->HalFunc.UpdateRAMaskHandler(padapter, psta->mac_id, rssi_level);
	}
}

void	rtw_hal_add_ra_tid23a(struct rtw_adapter *padapter, u32 bitmap, u8 arg, u8 rssi_level)
{
	if (padapter->HalFunc.Add_RateATid)
		padapter->HalFunc.Add_RateATid(padapter, bitmap, arg, rssi_level);
}

u32	rtw_hal_read_rfreg23a(struct rtw_adapter *padapter, u32 eRFPath, u32 RegAddr, u32 BitMask)
{
	u32 data = 0;
	if (padapter->HalFunc.read_rfreg)
		data = padapter->HalFunc.read_rfreg(padapter, eRFPath, RegAddr, BitMask);
	return data;
}
void	rtw_hal_write_rfreg23a(struct rtw_adapter *padapter, u32 eRFPath, u32 RegAddr, u32 BitMask, u32 Data)
{
	if (padapter->HalFunc.write_rfreg)
		padapter->HalFunc.write_rfreg(padapter, eRFPath, RegAddr, BitMask, Data);
}

s32	rtw_hal_interrupt_handler23a(struct rtw_adapter *padapter)
{
	if (padapter->HalFunc.interrupt_handler)
		return padapter->HalFunc.interrupt_handler(padapter);
	return _FAIL;
}

void	rtw_hal_set_bwmode23a(struct rtw_adapter *padapter,
			   enum ht_channel_width Bandwidth, u8 offset)
{
	if (padapter->HalFunc.set_bwmode_handler)
		padapter->HalFunc.set_bwmode_handler(padapter, Bandwidth,
						     offset);
}

void	rtw_hal_set_chan23a(struct rtw_adapter *padapter, u8 channel)
{
	if (padapter->HalFunc.set_channel_handler)
		padapter->HalFunc.set_channel_handler(padapter, channel);
}

void rtw_hal_bcn_related_reg_setting23a(struct rtw_adapter *padapter)
{
	if (padapter->HalFunc.SetBeaconRelatedRegistersHandler)
		padapter->HalFunc.SetBeaconRelatedRegistersHandler(padapter);
}
