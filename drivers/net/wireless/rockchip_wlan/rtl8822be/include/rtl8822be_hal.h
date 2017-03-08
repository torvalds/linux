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
#ifndef _RTL8822BE_HAL_H_
#define _RTL8822BE_HAL_H_

#include <drv_types.h>		/* PADAPTER */

#define RT_BCN_INT_MASKS	(BIT20 | BIT25 | BIT26 | BIT16)

/* rtl8822be_ops.c */
void UpdateInterruptMask8822BE(PADAPTER, u32 AddMSR, u32 AddMSR1, u32 RemoveMSR, u32 RemoveMSR1);

#endif /* _RTL8822BE_HAL_H_ */
