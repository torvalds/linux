/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#ifndef _RTL8723B_SRESET_H_
#define _RTL8723B_SRESET_H_

#include <rtw_sreset.h>

#ifdef DBG_CONFIG_ERROR_DETECT
	extern void rtl8723b_sreset_xmit_status_check(_adapter *padapter);
	extern void rtl8723b_sreset_linked_status_check(_adapter *padapter);
#endif
#endif
