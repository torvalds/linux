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

#ifndef __DISP_VIDEO_H_
#define __DISP_VIDEO_H_

#include "disp_display_i.h"

#define CASE_P_SOURCE 0
#define CASE_I_SAME_FRAME_RATE 1
#define CASE_I_DIFF_FRAME_RATE 2

typedef enum {
	DIT_MODE_WEAVE = 0,
	DIT_MODE_BOB = 1,
	DIT_MODE_MAF = 2,
	DIT_MODE_MAF_BOB = 3,
} dit_mode_t;

typedef struct frame_para {
	__bool enable;

	__disp_video_fb_t video_cur;
	__disp_video_fb_t video_new;
	__u32 pre_frame_addr0;

	__bool have_got_frame;
	__bool fetch_field; /* for scaler */
	__bool fetch_bot; /* for dit if dit enable,else for scaler */
	__u32 display_cnt;
	__bool out_field;
	__bool out_bot;
	__bool dit_enable;
	dit_mode_t dit_mode;
	__bool tempdiff_en;
	__bool diagintp_en;

} frame_para_t;

typedef struct tv_mode_info {
	__u8 id;
	__s32 width;
	__s32 height;
	__bool interlace;
	__s32 frame_rate;
	__s32 vb_line;
} tv_mode_info_t;

__s32 Video_Operation_In_Vblanking(__u32 sel, __u32 tcon_index);
extern frame_para_t g_video[2][4];

#endif
