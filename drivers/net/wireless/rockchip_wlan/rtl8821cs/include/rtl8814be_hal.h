/******************************************************************************
 *
 * Copyright(c) 2015 - 2017 Realtek Corporation.
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
#ifndef _RTL8814BE_HAL_H_
#define _RTL8814BE_HAL_H_

#include <drv_types.h>		/* PADAPTER */

#define RT_BCN_INT_MASKS	(BIT_BCNDMAINT0_MSK_8814B |	\
				 BIT_TXBCN0OK_MSK_8814B |	\
				 BIT_TXBCN0ERR_MSK_8814B |	\
				 BIT_BCNDERR0_MSK_8814B)

/* rtl8814be_ops.c */
void UpdateInterruptMask8814BE(PADAPTER, u32 AddMSR, u32 AddMSR1, u32 RemoveMSR, u32 RemoveMSR1);
u16 get_txbd_rw_reg(u16 q_idx);


#endif /* _RTL8814BE_HAL_H_ */
