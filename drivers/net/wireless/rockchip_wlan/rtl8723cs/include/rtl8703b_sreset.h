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
#ifndef _RTL8703B_SRESET_H_
#define _RTL8703B_SRESET_H_

#include <rtw_sreset.h>

#ifdef DBG_CONFIG_ERROR_DETECT
	extern void rtl8703b_sreset_xmit_status_check(_adapter *padapter);
	extern void rtl8703b_sreset_linked_status_check(_adapter *padapter);
#endif
#endif
