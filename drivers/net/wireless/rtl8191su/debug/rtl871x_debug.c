/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
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

#define _RTL871X_DEBUG_C_


#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
		
#ifdef CONFIG_DEBUG_RTL871X

	u32 GlobalDebugLevel = _drv_info_;

	u64 GlobalDebugComponents	= \
			_module_rtl871x_xmit_c_ |
			_module_xmit_osdep_c_ 	|
			_module_rtl871x_recv_c_ |
			_module_recv_osdep_c_ 	|
			_module_rtl871x_mlme_c_ |
			_module_mlme_osdep_c_ 	|
			_module_rtl871x_sta_mgt_c_ |
			_module_rtl871x_cmd_c_ 	|
			_module_cmd_osdep_c_ 	|
			_module_rtl871x_io_c_   | 
			_module_io_osdep_c_ 	|
			_module_os_intfs_c_|
			_module_rtl871x_security_c_|
			_module_rtl871x_eeprom_c_|
			_module_hal_init_c_|
			_module_hci_hal_init_c_|
			_module_rtl871x_ioctl_c_|
			_module_rtl871x_ioctl_set_c_|
			_module_rtl871x_ioctl_query_c_|
			_module_rtl871x_pwrctrl_c_|
			_module_hci_intfs_c_|
			_module_hci_ops_c_|
			_module_rtl871x_mp_ioctl_c_|
			_module_hci_ops_os_c_|
			_module_rtl871x_ioctl_os_c|
			_module_rtl871x_mp_c_ |		
			_module_rtl8712_cmd_c_|		
			_module_rtl8712_xmit_c_|
			_module_rtl8712_efuse_c_|
			_module_rtl8712_recv_c_;

									
#endif

