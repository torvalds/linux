/******************************************************************************
 *
 * Copyright(c) 2016 - 2017 Realtek Corporation.
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
#ifndef _RTL8821CS_H_
#define _RTL8821CS_H_

#include <drv_types.h>		/* PADAPTER */

/* rtl8821cs_halinit.c */
u32 rtl8821cs_hal_init(PADAPTER);
u32 rtl8821cs_hal_deinit(PADAPTER adapter);

void rtl8821cs_init_default_value(PADAPTER);

/* rtl8821cs_ops.c */
u32 rtl8821cs_get_interrupt(PADAPTER);
void rtl8821cs_clear_interrupt(PADAPTER, u32 hisr);
u32 rtl8821cs_get_himr(PADAPTER adapter);
void rtl8821cs_update_himr(PADAPTER adapter, u32 himr);
void rtl8821cs_update_interrupt_mask(PADAPTER padapter, u32 AddMSR, u32 RemoveMSR);

/* rtl8821cs_xmit.c */


#endif /* _RTL8821CS_HAL_H_ */
