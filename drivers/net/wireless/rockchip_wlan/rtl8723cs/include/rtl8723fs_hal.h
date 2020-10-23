/******************************************************************************
 *
 * Copyright(c) 2019 Realtek Corporation.
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
#ifndef _RTL8723FS_HAL_H_
#define _RTL8723FS_HAL_H_

#include <drv_types.h>		/* PADAPTER */

/* rtl8723fs_ops.c */
void rtl8723fs_set_hal_ops(PADAPTER);

#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
void rtl8723fs_disable_interrupt_but_cpwm2(PADAPTER adapter);
#endif

/* rtl8723fs_xmit.c */
s32 rtl8723fs_dequeue_writeport(PADAPTER);
#define _dequeue_writeport(a)	rtl8723fs_dequeue_writeport(a)

#endif /* _RTL8723FS_HAL_H_ */
