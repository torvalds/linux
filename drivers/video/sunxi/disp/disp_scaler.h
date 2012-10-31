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

#ifndef _DISP_SCALER_H_
#define _DISP_SCALER_H_

#include "disp_display_i.h"

#define SCALER_HANDTOID(handle)  ((handle) - 100)
#define SCALER_IDTOHAND(ID)  ((ID) + 100)

#define SCALER_WB_FINISHED 0x00000002

typedef enum {
	SCALER_IN_SRC_DRAM = 0,
	SCALER_IN_SRC_IMAGE0_TO_LCD = 1,
	SCALER_IN_SRC_IMAGE1_TO_LCD = 2,
	SCALER_IN_SRC_IMAGE0 = 2,
	SCALER_IN_SRC_IMAGE1 = 3,
} __disp_scaler_in_src;

typedef struct {
	__u32 status;
	__bool b_reg_change;
	__bool b_close;

	__u32 screen_index;
	__disp_scaler_in_src input_sel;

	__u32 layer_id;
	__disp_fb_t in_fb;
	__disp_fb_t out_fb;
	__disp_rect_t src_win;
	__disp_rectsz_t out_size;
	__u32 smooth_mode;

	__bool enhance_en;
	__u32 bright;
	__u32 contrast;
	__u32 saturation;
	__u32 hue;

	__bool vpp_en;
	__u32 luma_sharpe_level;
	__u32 chroma_sharpe_level;
	__u32 while_exten_level;
	__u32 black_exten_level;

	__bool b_trd_out;
	__disp_3d_out_mode_t out_trd_mode;
	wait_queue_head_t scaler_queue;
	__bool b_scaler_finished;
#ifdef CONFIG_ARCH_SUN5I
	__bool coef_change;
#endif
} __disp_scaler_t;

extern __disp_scaler_t gscl;

__s32 Scaler_Init(__u32 sel);
__s32 Scaler_Exit(__u32 sel);
__s32 Scaler_open(__u32 sel);
__s32 Scaler_close(__u32 sel);
__s32 Scaler_Request(__u32 sel);
__s32 Scaler_Release(__u32 sel, __bool b_display);
__s32 Scaler_Set_Framebuffer(__u32 sel, __disp_fb_t *vfb_info);
__s32 Scaler_Get_Framebuffer(__u32 sel, __disp_fb_t *vfb_info);
__s32 Scaler_Set_SclRegn(__u32 sel, __disp_rect_t *scl_rect);
__s32 Scaler_Get_SclRegn(__u32 sel, __disp_rect_t *scl_rect);
__s32 Scaler_Set_Output_Size(__u32 sel, __disp_rectsz_t *out_size);
__s32 Scaler_Set_Para(__u32 sel, __disp_scaler_t *scl);
__s32 Scaler_Set_Outitl(__u32 sel, __bool enable);
__s32 Scaler_sw_para_to_reg(__u8 type, __u8 value);
__s32 Scaler_Set_Enhance(__u32 sel, __u32 bright, __u32 contrast,
			 __u32 saturation, __u32 hue);
__s32 Scaler_3d_sw_para_to_reg(__u32 type, __u32 mode, __bool b_out_interlace);

#endif
