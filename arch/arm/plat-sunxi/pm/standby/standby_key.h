/*
 * arch/arm/plat-sunxi/pm/standby/standby_key.h
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

#ifndef __STANDBY_KEY_H__
#define __STANDBY_KEY_H__

#include "standby_cfg.h"
//define key controller registers
typedef struct __STANDBY_KEY_REG
{
    // offset:0x00
    volatile __u32   Lradc_Ctrl;
    volatile __u32   Lradc_Intc;
    volatile __u32   Lradc_Ints;
    volatile __u32   Lradc_Data0;
    volatile __u32   Lradc_Data1;
} __standby_key_reg_t;

extern __s32 standby_key_init(void);
extern __s32 standby_key_exit(void);
extern __s32 standby_query_key(void);


#endif  /* __STANDBY_KEY_H__ */

