/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 * Copyright(c) 2014, Jes Sorensen <Jes.Sorensen@redhat.com>
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
#ifndef __RTL8723A_BT_INTF_H__
#define __RTL8723A_BT_INTF_H__

#include <drv_types.h>

#ifdef CONFIG_8723AU_BT_COEXIST
bool rtl8723a_BT_using_antenna_1(struct rtw_adapter *padapter);
#else
static inline bool rtl8723a_BT_using_antenna_1(struct rtw_adapter *padapter)
{
	return false;
}
#endif

#endif
