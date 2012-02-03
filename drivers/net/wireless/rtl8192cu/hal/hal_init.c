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

#include <hal_init.h>

#ifdef CONFIG_SDIO_HCI
	#include <sdio_hal.h>
#elif defined(CONFIG_USB_HCI)
	#include <usb_hal.h>
#endif

void intf_chip_configure(_adapter *padapter)
{
	if(padapter->HalFunc.intf_chip_configure)
		padapter->HalFunc.intf_chip_configure(padapter);
}

void intf_read_chip_info(_adapter *padapter)
{
	if(padapter->HalFunc.read_adapter_info)
		padapter->HalFunc.read_adapter_info(padapter);
}

void intf_read_chip_version(_adapter *padapter)
{
	if(padapter->HalFunc.read_chip_version)
		padapter->HalFunc.read_chip_version(padapter);
}

void	rtw_dm_init(_adapter *padapter)
{
	if(padapter->HalFunc.dm_init)
		padapter->HalFunc.dm_init(padapter);
}

void	rtw_sw_led_init(_adapter *padapter)
{
	if(padapter->HalFunc.InitSwLeds)
		padapter->HalFunc.InitSwLeds(padapter);
}

void rtw_sw_led_deinit(_adapter *padapter)
{
	if(padapter->HalFunc.DeInitSwLeds)
		padapter->HalFunc.DeInitSwLeds(padapter);
}

uint	 rtw_hal_init(_adapter *padapter) 
{
	uint	status = _SUCCESS;
	
	padapter->hw_init_completed=_FALSE;

	status = padapter->HalFunc.hal_init(padapter);

	if(status == _SUCCESS){
		padapter->hw_init_completed = _TRUE;
	}
	else{
	 	padapter->hw_init_completed = _FALSE;
		RT_TRACE(_module_hal_init_c_,_drv_err_,("rtw_hal_init: hal__init fail\n"));
	}

	RT_TRACE(_module_hal_init_c_,_drv_err_,("-rtl871x_hal_init:status=0x%x\n",status));

	return status;

}	

uint	 rtw_hal_deinit(_adapter *padapter)
{
	uint	status = _SUCCESS;
	
_func_enter_;

	status = padapter->HalFunc.hal_deinit(padapter);

	if(status == _SUCCESS){
		padapter->hw_init_completed = _FALSE;
	}
	else
	{
		RT_TRACE(_module_hal_init_c_,_drv_err_,("\n rtw_hal_deinit: hal_init fail\n"));
	}
	
_func_exit_;
	
	return status;
	
}
#ifdef DBG_CONFIG_ERROR_DETECT
void	rtw_sreset_init(_adapter *padapter)
{
	if(padapter->HalFunc.sreset_init_value)
		padapter->HalFunc.sreset_init_value(padapter); 
}
#endif	
