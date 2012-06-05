/*
 * drivers/video/sun3i/disp/de_bsp/de/disp_lcd.h
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


#ifndef __DISP_LCD_H__
#define __DISP_LCD_H__

#include "disp_display_i.h"

__s32 Disp_lcdc_init(__u32 sel);
__s32 Disp_lcdc_exit(__u32 sel);
#ifndef __LINUX_OSAL__
__s32 Disp_lcdc_event_proc(void *parg);
#else
__s32 Disp_lcdc_event_proc(__s32 irq, void *parg);
#endif
__s32 Disp_lcdc_pin_cfg(__u32 sel, __disp_output_type_t out_type, __u32 bon);


#endif
