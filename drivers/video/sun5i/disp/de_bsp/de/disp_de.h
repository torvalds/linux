/*
 * drivers/video/sun5i/disp/de_bsp/de/disp_de.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Danling <danliang@allwinnertech.com>
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


#ifndef __DISP_DE_H_
#define __DISP_DE_H_

#include "disp_display_i.h"

extern __hdle   h_tvahbclk;
extern __hdle   h_tv1clk;
extern __hdle   h_tv2clk;

#ifdef __LINUX_OSAL__
__s32 Scaler_event_proc(int irq, void *parg);
#else
__s32 Scaler_event_proc(void *parg);
#endif

__s32 Image_init(__u32 sel);
__s32 Image_exit(__u32 sel);
__s32 Image_open(__u32 sel);
__s32 Image_close(__u32 sel);
__s32 Disp_de_flicker_enable(__u32 sel, __u32 enable );

#endif
