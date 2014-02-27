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
#ifndef	__MLME_OSDEP_H_
#define __MLME_OSDEP_H_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>

#if defined(PLATFORM_WINDOWS) || defined(PLATFORM_MPIXEL)
extern int time_after(u32 now, u32 old);
#endif

extern void rtw_init_mlme_timer(_adapter *padapter);
extern void rtw_os_indicate_disconnect( _adapter *adapter );
extern void rtw_os_indicate_connect( _adapter *adapter );
void rtw_os_indicate_scan_done( _adapter *padapter, bool aborted);
extern void rtw_report_sec_ie(_adapter *adapter,u8 authmode,u8 *sec_ie);

void rtw_reset_securitypriv( _adapter *adapter );

#endif	//_MLME_OSDEP_H_

