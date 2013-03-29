/*
 * arch/arm/plat-sunxi/pm/standby/standby_int.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Kevin Zhang <kevin@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef __STANDBY_INT_H__
#define __STANDBY_INT_H__

#include "standby_cfg.h"


/* define interrupt source */
enum interrupt_source_e{

    INT_SOURCE_EXTNMI   = 0,
    INT_SOURCE_IR0      = 5,
    INT_SOURCE_IR1      = 6,
    INT_SOURCE_KEYPAD   = 21,
    INT_SOURCE_TIMER0   = 22,
    INT_SOURCE_TIMER1   = 23,
    INT_SOURCE_ALARM    = 24,
    INT_SOURCE_TOUCHPNL = 29,
    INT_SOURCE_LRADC    = 31,
    INT_SOURCE_USB0     = 38,
    INT_SOURCE_USB1     = 39,
    INT_SOURCE_USB2     = 40,
    INT_SOURCE_USB3     = 64,
    INT_SOURCE_USB4     = 65,
};


/* define register for interrupt controller */
struct standby_int_reg_t{

    volatile __u32   Vector;
    volatile __u32   BaseAddr;
    volatile __u32   reserved0;
    volatile __u32   NmiCtrl;

    volatile __u32   IrqPend[3];
    volatile __u32   reserved1;

    volatile __u32   FiqPend[3];
    volatile __u32   reserved2;

    volatile __u32   TypeSel[3];
    volatile __u32   reserved3;

    volatile __u32   IrqEn[3];
    volatile __u32   reserved4;

    volatile __u32   IrqMask[3];
    volatile __u32   reserved5;

    volatile __u32   IrqResp[3];
    volatile __u32   reserved6;

    volatile __u32   IrqForce[3];
    volatile __u32   reserved7;

    volatile __u32   IrqPrio[5];
};


extern __s32 standby_int_init(void);
extern __s32 standby_int_exit(void);
extern __s32 standby_enable_int(enum interrupt_source_e src);
extern __s32 standby_query_int(enum interrupt_source_e src);


#endif  //__STANDBY_INT_H__

