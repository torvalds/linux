/*
 * Copyright 1998-2008 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.

 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * either version 2, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTIES OR REPRESENTATIONS; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.See the GNU General Public License
 * for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include "global.h"
int viafb_platform_epia_dvi = STATE_OFF;
int viafb_device_lcd_dualedge = STATE_OFF;
int viafb_bus_width = 12;
int viafb_display_hardware_layout = HW_LAYOUT_LCD_DVI;
int viafb_memsize;
int viafb_DeviceStatus = CRT_Device;
int viafb_hotplug;
int viafb_refresh = 60;
int viafb_refresh1 = 60;
int viafb_lcd_dsp_method = LCD_EXPANDSION;
int viafb_lcd_mode = LCD_OPENLDI;
int viafb_bpp = 32;
int viafb_bpp1 = 32;
int viafb_CRT_ON = 1;
int viafb_DVI_ON;
int viafb_LCD_ON ;
int viafb_LCD2_ON;
int viafb_SAMM_ON;
int viafb_dual_fb;
int viafb_hotplug_Xres = 640;
int viafb_hotplug_Yres = 480;
int viafb_hotplug_bpp = 32;
int viafb_hotplug_refresh = 60;
unsigned int viafb_second_offset;
int viafb_second_size;
int viafb_primary_dev = None_Device;
unsigned int viafb_second_xres = 640;
unsigned int viafb_second_yres = 480;
unsigned int viafb_second_virtual_xres;
unsigned int viafb_second_virtual_yres;
int viafb_lcd_panel_id = LCD_PANEL_ID_MAXIMUM + 1;
struct fb_info *viafbinfo;
struct fb_info *viafbinfo1;
struct viafb_par *viaparinfo;
struct viafb_par *viaparinfo1;

