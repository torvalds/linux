/******************************************************************************
 *
 * Copyright(c) 2015 - 2016 Realtek Corporation. All rights reserved.
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
#ifndef _RTL8822BS_HAL_H_
#define _RTL8822BS_HAL_H_

#include <drv_types.h>		/* PADAPTER */

/* rtl8822bs_ops.c */
void rtl8822bs_set_hal_ops(PADAPTER);

/* rtl8822bs_xmit.c */
s32 rtl8822bs_dequeue_writeport(PADAPTER);
#define _dequeue_writeport(a)	rtl8822bs_dequeue_writeport(a)

#endif /* _RTL8822BS_HAL_H_ */
