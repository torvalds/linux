/*
 * Copyright (C) 2007-2012 Allwinner Technology Co., Ltd.
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

extern __hdle h_tvahbclk;
extern __hdle h_tv1clk;
extern __hdle h_tv2clk;

__s32 Image_init(__u32 sel);
__s32 Image_exit(__u32 sel);
__s32 Image_open(__u32 sel);
__s32 Image_close(__u32 sel);
#ifdef CONFIG_ARCH_SUN4I
__s32 Disp_set_out_interlace(__u32 sel);
#endif

#endif
