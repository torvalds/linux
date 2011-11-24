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
#ifndef __SDIO_OSINTF_H
#define __SDIO_OSINTF_H


#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>


extern unsigned int sd_dvobj_init(_adapter * adapter);
extern void sd_dvobj_deinit(_adapter * adapter);

void rtl871x_intf_stop(_adapter *padapter);

u8 sd_hal_bus_init(_adapter * padapter);
u8 sd_hal_bus_deinit(_adapter * padapter);
void update_xmit_hw_res(_adapter * padapter);
void sd_c2h_hdl( PADAPTER	padapter);

#ifdef PLATFORM_OS_CE
extern NDIS_STATUS ce_sd_get_dev_hdl(_adapter *padapter );
SD_API_STATUS  
ce_sd_int_callback(SD_DEVICE_HANDLE hDevice, _adapter* padapter);
extern void sd_setup_irs(_adapter *padapter);
#endif

#endif

