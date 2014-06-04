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
 ******************************************************************************/
#ifndef _RTL8723A_SRESET_H_
#define _RTL8723A_SRESET_H_

#include <osdep_service.h>
#include <drv_types.h>
#include <rtw_sreset.h>

void rtl8723a_sreset_xmit_status_check(struct rtw_adapter *padapter);
void rtl8723a_sreset_linked_status_check(struct rtw_adapter *padapter);

#endif
