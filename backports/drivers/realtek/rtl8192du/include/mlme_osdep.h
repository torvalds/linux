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
 *
 ******************************************************************************/
#ifndef	__MLME_OSDEP_H_
#define __MLME_OSDEP_H_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>

void rtw_init_mlme_timer(struct rtw_adapter *padapter);
void rtw_os_indicate_disconnect(struct rtw_adapter *adapter);
void rtw_os_indicate_connect(struct rtw_adapter *adapter);
void rtw_os_indicate_scan_done(struct rtw_adapter *padapter, bool aborted);
void rtw_report_sec_ie(struct rtw_adapter *adapter,u8 authmode,u8 *sec_ie);

void rtw_reset_securitypriv(struct rtw_adapter *adapter);
void indicate_wx_scan_complete_event(struct rtw_adapter *padapter);
u8 rtw_do_join(struct rtw_adapter *padapter);


#endif	/* _MLME_OSDEP_H_ */
