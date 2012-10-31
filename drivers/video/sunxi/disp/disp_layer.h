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

#ifndef _DISP_LAYER_H_
#define _DISP_LAYER_H_

#include "disp_display_i.h"

#define HLID_ASSERT(no, max) \
do { \
	if ((__s32)(no) < DIS_SUCCESS || (no) >= (max))	\
		return DIS_PARA_FAILED;			\
} while (0);

#define IDLE_HID    0xff
#define IDLE_PRIO   0xff

#define LAYER_OPENED        0x00000001
#define LAYER_USED          0x00000002

typedef struct layer_man_t {
	__u32 status;
	__bool byuv_ch;

	/*
	 * used if scaler mode: 0/1
	 */
	__u32 scaler_index;
#ifdef CONFIG_ARCH_SUN4I
	__bool video_enhancement_en;
#endif
	__disp_layer_info_t para;
} __layer_man_t;

typedef enum {
	DISP_FB_TYPE_RGB = 0x0,
	DISP_FB_TYPE_YUV = 0x1,
} __disp_pixel_type_t;

__u32 Layer_Get_Prio(__u32 sel, __u32 hid);
__disp_pixel_type_t get_fb_type(__disp_pixel_fmt_t format);
__s32 de_format_to_bpp(__disp_pixel_fmt_t fmt);
__s32 img_sw_para_to_reg(__u8 type, __u8 mode, __u8 value);
__s32 Yuv_Channel_Set_framebuffer(__u32 sel, __disp_fb_t *pfb, __u32 xoffset,
				  __u32 yoffset);
__s32 Yuv_Channel_adjusting(__u32 sel, __u32 mode, __u32 format, __s32 *src_x,
			    __u32 *scn_width);

#endif
