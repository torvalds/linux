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

#ifndef __DISP_TV_H__
#define __DISP_TV_H__

#include "disp_display_i.h"

__s32 Disp_TVEC_Init(__u32 sel);
__s32 Disp_TVEC_Exit(__u32 sel);
__s32 Disp_TVEC_Open(__u32 sel);
__s32 Disp_TVEC_Close(__u32 sel);
__s32 Disp_Switch_Dram_Mode(__u32 type, __u8 tv_mod);
__s32 Disp_TVEC_Event_Proc(void *parg);

#endif
