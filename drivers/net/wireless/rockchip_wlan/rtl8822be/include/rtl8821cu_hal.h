/******************************************************************************
 *
 * Copyright(c) 2016 Realtek Corporation. All rights reserved.
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
#ifndef _RTL8821CU_HAL_H_
#define _RTL8821CU_HAL_H_

#include <drv_types.h>		/* PADAPTER */

/* rtl8821cu_ops.c */
u8 rtl8821cu_set_hal_ops(PADAPTER);
void rtl8821cu_set_hw_type(struct dvobj_priv *pdvobj);

#endif /* _RTL8821CU_HAL_H_ */
