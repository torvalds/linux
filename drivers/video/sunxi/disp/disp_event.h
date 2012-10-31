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

#ifndef __DISP_EVENT_H__
#define __DISP_EVENT_H__

#include "disp_display_i.h"
#include "disp_layer.h"

void LCD_vbi_event_proc(__u32 sel, __u32 tcon_index);
void LCD_line_event_proc(__u32 sel);
__s32 BSP_disp_cfg_start(__u32 sel);
__s32 BSP_disp_cfg_finish(__u32 sel);

#endif
