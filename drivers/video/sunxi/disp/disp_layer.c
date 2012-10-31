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

#include "disp_layer.h"
#include "disp_de.h"
#include "disp_display.h"
#include "disp_scaler.h"
#include "disp_event.h"
#include "disp_clk.h"

static __s32 Layer_Get_Idle_Hid(__u32 sel)
{
	__s32 i;

	for (i = 0; i < gdisp.screen[sel].max_layers; i++)
		if (!(gdisp.screen[sel].layer_manage[i].status & LAYER_USED))
			return i;

	return (__s32) DIS_NO_RES;
}

static __s32 Layer_Get_Idle_Prio(__u32 sel)
{
	__s32 i, j;

	for (i = 0; i < gdisp.screen[sel].max_layers; i++)
		for (j = 0; j < gdisp.screen[sel].max_layers; j++) {
			if (gdisp.screen[sel].layer_manage[j].para.prio == i)
				break;
			else if (j == gdisp.screen[sel].max_layers - 1)
				return i;
		}

	return DIS_PRIO_ERROR;
}

__u32 Layer_Get_Prio(__u32 sel, __u32 hid)
{
	if (gdisp.screen[sel].layer_manage[hid].status & LAYER_USED)
		return gdisp.screen[sel].layer_manage[hid].para.prio;

	return (__u32) DIS_PARA_FAILED;
}

__disp_pixel_type_t get_fb_type(__disp_pixel_fmt_t format)
{
	if (format == DISP_FORMAT_YUV444 || format == DISP_FORMAT_YUV422 ||
	    format == DISP_FORMAT_YUV420 || format == DISP_FORMAT_YUV411)
		return DISP_FB_TYPE_YUV;
	else
		return DISP_FB_TYPE_RGB;
}

/*
 * 0: yuv channel format
 * 1: yuv channel pixel sequence
 * 3: image0 pixel sequence
 */
__s32 img_sw_para_to_reg(__u8 type, __u8 mode, __u8 value)
{
	if (type == 0) { /* yuv channel format */
		if (mode == DISP_MOD_NON_MB_PLANAR &&
		    value == DISP_FORMAT_YUV411) {
			return 0;
		} else if (mode == DISP_MOD_NON_MB_PLANAR &&
			   value == DISP_FORMAT_YUV422) {
			return 1;
		} else if (mode == DISP_MOD_NON_MB_PLANAR &&
			   value == DISP_FORMAT_YUV444) {
			return 2;
		} else if (mode == DISP_MOD_INTERLEAVED &&
			   value == DISP_FORMAT_YUV422) {
			return 3;
		} else if (mode == DISP_MOD_INTERLEAVED &&
			   value == DISP_FORMAT_YUV444) {
			return 4;
		} else {
			DE_WRN("not supported yuv channel format:%d in "
			       "img_sw_para_to_reg\n", value);
			return 0;
		}
	} else if (type == 1) { /* yuv channel pixel sequence */
		if (mode == DISP_MOD_NON_MB_PLANAR && value == DISP_SEQ_P3210) {
			return 0;
		} else if (mode == DISP_MOD_NON_MB_PLANAR &&
			   value == DISP_SEQ_P0123) {
			return 1;
		} else if (mode == DISP_MOD_INTERLEAVED &&
			   value == DISP_SEQ_UYVY) {
			return 0;
		} else if (mode == DISP_MOD_INTERLEAVED &&
			   value == DISP_SEQ_YUYV) {
			return 1;
		} else if (mode == DISP_MOD_INTERLEAVED &&
			   value == DISP_SEQ_VYUY) {
			return 2;
		} else if (mode == DISP_MOD_INTERLEAVED &&
			   value == DISP_SEQ_YVYU) {
			return 3;
		} else if (mode == DISP_MOD_INTERLEAVED &&
			   value == DISP_SEQ_AYUV) {
			return 0;
		} else if (mode == DISP_MOD_INTERLEAVED &&
			   value == DISP_SEQ_VUYA) {
			return 1;
		} else {
			DE_WRN("not supported yuv channel pixel sequence:%d "
			       "in img_sw_para_to_reg\n", value);
			return 0;
		}
	} else if (type == 3) {	/* image0 pixel sequence */
		if (value == DISP_SEQ_ARGB) {
			return 0;
		} else if (value == DISP_SEQ_BGRA) {
			return 2;
		} else if (value == DISP_SEQ_P10) {
			return 0;
		} else if (value == DISP_SEQ_P01) {
			return 1;
		} else if (value == DISP_SEQ_P3210) {
			return 0;
		} else if (value == DISP_SEQ_P0123) {
			return 1;
		} else if (value == DISP_SEQ_P76543210) {
			return 0;
		} else if (value == DISP_SEQ_P67452301) {
			return 1;
		} else if (value == DISP_SEQ_P10325476) {
			return 2;
		} else if (value == DISP_SEQ_P01234567) {
			return 3;
		} else if (value == DISP_SEQ_2BPP_BIG_BIG) {
			return 0;
		} else if (value == DISP_SEQ_2BPP_BIG_LITTER) {
			return 1;
		} else if (value == DISP_SEQ_2BPP_LITTER_BIG) {
			return 2;
		} else if (value == DISP_SEQ_2BPP_LITTER_LITTER) {
			return 3;
		} else if (value == DISP_SEQ_1BPP_BIG_BIG) {
			return 0;
		} else if (value == DISP_SEQ_1BPP_BIG_LITTER) {
			return 1;
		} else if (value == DISP_SEQ_1BPP_LITTER_BIG) {
			return 2;
		} else if (value == DISP_SEQ_1BPP_LITTER_LITTER) {
			return 3;
		} else {
			DE_WRN("not supported image0 pixel sequence:%d in "
			       "img_sw_para_to_reg\n", value);
			return 0;
		}
	}

	DE_WRN("not supported type:%d in img_sw_para_to_reg\n", type);
	return 0;
}

__s32 de_format_to_bpp(__disp_pixel_fmt_t fmt)
{
	switch (fmt) {
	case DISP_FORMAT_1BPP:
		return 1;

	case DISP_FORMAT_2BPP:
		return 2;

	case DISP_FORMAT_4BPP:
		return 4;

	case DISP_FORMAT_8BPP:
		return 8;

	case DISP_FORMAT_RGB655:
	case DISP_FORMAT_RGB565:
	case DISP_FORMAT_RGB556:
	case DISP_FORMAT_ARGB1555:
	case DISP_FORMAT_RGBA5551:
	case DISP_FORMAT_ARGB4444:
		return 16;

	case DISP_FORMAT_RGB888:
		return 24;

	case DISP_FORMAT_ARGB8888:
		return 32;

	case DISP_FORMAT_YUV444:
		return 24;

	case DISP_FORMAT_YUV422:
		return 16;

	case DISP_FORMAT_YUV420:
	case DISP_FORMAT_YUV411:
		return 12;

	case DISP_FORMAT_CSIRGB:
		return 32; /* ? */

	default:
		return 0;
	}
}

static __s32 Yuv_Channel_Request(__u32 sel, __u8 hid)
{
	if (!(gdisp.screen[sel].status & YUV_CH_USED)) {
		DE_BE_YUV_CH_Enable(sel, TRUE);
		DE_BE_Layer_Yuv_Ch_Enable(sel, hid, TRUE);

		gdisp.screen[sel].layer_manage[hid].byuv_ch = TRUE;
		gdisp.screen[sel].status |= YUV_CH_USED;
		return DIS_SUCCESS;
	}
	return DIS_NO_RES;
}

static __s32 Yuv_Channel_Release(__u32 sel, __u8 hid)
{
	de_yuv_ch_src_t yuv_src;

	memset(&yuv_src, 0, sizeof(de_yuv_ch_src_t));
	DE_BE_YUV_CH_Set_Src(sel, &yuv_src);
	DE_BE_YUV_CH_Enable(sel, FALSE);
	DE_BE_Layer_Yuv_Ch_Enable(sel, hid, FALSE);

	gdisp.screen[sel].layer_manage[hid].byuv_ch = FALSE;
	gdisp.screen[sel].status &= ~YUV_CH_USED;

	return DIS_SUCCESS;
}

__s32 Yuv_Channel_Set_framebuffer(__u32 sel, __disp_fb_t *pfb, __u32 xoffset,
				  __u32 yoffset)
{
	de_yuv_ch_src_t yuv_src;

	yuv_src.format = img_sw_para_to_reg(0, pfb->mode, pfb->format);
	yuv_src.mode = (__u8) pfb->mode;
	yuv_src.pixseq = img_sw_para_to_reg(1, pfb->mode, pfb->seq);
	yuv_src.ch0_base = pfb->addr[0];
	yuv_src.ch1_base = pfb->addr[1];
	yuv_src.ch2_base = pfb->addr[2];
	yuv_src.line_width = pfb->size.width;
	yuv_src.offset_x = xoffset;
	yuv_src.offset_y = yoffset;
	yuv_src.cs_mode = pfb->cs_mode;
	DE_BE_YUV_CH_Set_Src(sel, &yuv_src);

	return DIS_SUCCESS;
}

__s32 Yuv_Channel_adjusting(__u32 sel, __u32 mode, __u32 format, __s32 *src_x,
			    __u32 *scn_width)
{
	__u32 w_shift;
	__u32 reg_format;

	reg_format = img_sw_para_to_reg(0, mode, format);

	if (reg_format == 0x0) /* planar yuv411 */
		w_shift = 4;
	else if (reg_format == 0x1) /* planar yuv422 */
		w_shift = 3;
	else if (reg_format == 0x2) /* planar yuv444 */
		w_shift = 2;
	else
		w_shift = 0;

	*src_x = (*src_x >> w_shift) << w_shift;
	*scn_width = (*scn_width >> w_shift) << w_shift;

	return DIS_SUCCESS;
}

__s32 BSP_disp_layer_request(__u32 sel, __disp_layer_work_mode_t mode)
{
	__s32 hid;
	__s32 prio = 0;
	__layer_man_t *layer_man;

	hid = Layer_Get_Idle_Hid(sel);
	if (hid == DIS_NO_RES) {
		DE_WRN("all layer resource used!\n");
		return DIS_NULL;
	}
	prio = Layer_Get_Idle_Prio(sel);
	if (prio < 0) {
		DE_WRN("all layer prio used!\n");
		return DIS_NULL;
	}

	BSP_disp_cfg_start(sel);

	DE_BE_Layer_Enable(sel, hid, FALSE);
	DE_BE_Layer_Set_Prio(sel, hid, prio);
	DE_BE_Layer_Set_Work_Mode(sel, hid, DISP_LAYER_WORK_MODE_NORMAL);
	DE_BE_Layer_Video_Enable(sel, hid, FALSE);

	BSP_disp_cfg_finish(sel);

	layer_man = &gdisp.screen[sel].layer_manage[hid];
	memset(&layer_man->para, 0, sizeof(__disp_layer_info_t));
	layer_man->para.mode = DISP_LAYER_WORK_MODE_NORMAL;
	layer_man->para.prio = prio;
	layer_man->byuv_ch = 0;
	layer_man->status = LAYER_USED;

	return IDTOHAND(hid);
}

__s32 BSP_disp_layer_release(__u32 sel, __u32 hid)
{
	__layer_man_t *layer_man;

	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	BSP_disp_cfg_start(sel);

	layer_man = &gdisp.screen[sel].layer_manage[hid];

	if (layer_man->status & LAYER_USED) {
		if (layer_man->para.mode == DISP_LAYER_WORK_MODE_SCALER) {
			if (layer_man->para.b_from_screen) {
				Image_close(1 - sel);
				image_clk_off(1 - sel);
				gdisp.screen[1 - sel].image_output_type = 0;
			}
			/* release a scaler object */
			Scaler_Release(layer_man->scaler_index, FALSE);
		} else {
			if (layer_man->byuv_ch) {
				Yuv_Channel_Release(sel, hid);
			} else {
				layer_src_t layer_src;

				memset(&layer_src, 0, sizeof(layer_src_t));
				DE_BE_Layer_Set_Framebuffer(sel, hid,
							    &layer_src);
			}
		}
	}
	memset(layer_man, 0, sizeof(__layer_man_t));
	DE_BE_Layer_Enable(sel, hid, FALSE);
	DE_BE_Layer_Video_Enable(sel, hid, FALSE);
	DE_BE_Layer_Video_Ch_Sel(sel, hid, 0);
	DE_BE_Layer_Yuv_Ch_Enable(sel, hid, FALSE);
	DE_BE_Layer_Set_Screen_Win(sel, hid, &(layer_man->para.scn_win));
	DE_BE_Layer_Set_Prio(sel, hid, 0);
	DE_BE_Layer_Set_Pipe(sel, hid, 0);
	DE_BE_Layer_Alpha_Enable(sel, hid, FALSE);
	DE_BE_Layer_Set_Alpha_Value(sel, hid, 0);
	DE_BE_Layer_ColorKey_Enable(sel, hid, FALSE);

	BSP_disp_cfg_finish(sel);

	layer_man->para.prio = IDLE_PRIO;
	layer_man->status &= ~(LAYER_USED | LAYER_OPENED);

	return DIS_SUCCESS;
}

__s32 BSP_disp_layer_open(__u32 sel, __u32 hid)
{
	__layer_man_t *layer_man;

	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	layer_man = &gdisp.screen[sel].layer_manage[hid];
	if (layer_man->status & LAYER_USED) {
		if (!(layer_man->status & LAYER_OPENED)) {
			BSP_disp_cfg_start(sel);
			DE_BE_Layer_Enable(sel, hid, TRUE);
			BSP_disp_cfg_finish(sel);
			layer_man->status |= LAYER_OPENED;
		}
		return DIS_SUCCESS;
	} else {
		DE_WRN("layer %d in screen %d not inited!\n", hid, sel);
		return DIS_OBJ_NOT_INITED;
	}
}

__s32 BSP_disp_layer_close(__u32 sel, __u32 hid)
{
	__layer_man_t *layer_man;

	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	layer_man = &gdisp.screen[sel].layer_manage[hid];
	if (layer_man->status & LAYER_USED) {
		if (layer_man->status & LAYER_OPENED) {
			BSP_disp_cfg_start(sel);
			DE_BE_Layer_Enable(sel, hid, FALSE);
			BSP_disp_cfg_finish(sel);
			layer_man->status &= ~LAYER_OPENED;
		}
		return DIS_SUCCESS;
	} else {
		DE_WRN("layer %d in screen %d not inited!\n", hid, sel);
		return DIS_OBJ_NOT_INITED;
	}
}

/*
 * keep the src window offset x/y
 */
__s32 BSP_disp_layer_set_framebuffer(__u32 sel, __u32 hid, __disp_fb_t *pfb)
{
	__s32 ret;
	layer_src_t layer_fb;
	__layer_man_t *layer_man;
	__u32 size;

	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	if (pfb == NULL)
		return DIS_PARA_FAILED;

	layer_man = &gdisp.screen[sel].layer_manage[hid];
	if (layer_man->status & LAYER_USED) {
		BSP_disp_cfg_start(sel);
		if (layer_man->para.mode == DISP_LAYER_WORK_MODE_SCALER) {
			ret = Scaler_Set_Framebuffer(layer_man->scaler_index,
						     pfb);
			BSP_disp_cfg_finish(sel);
			return ret;
		} else {
			if (get_fb_type(pfb->format) == DISP_FB_TYPE_YUV) {
				if (layer_man->byuv_ch == FALSE) {
					ret = Yuv_Channel_Request(sel, hid);
					if (ret != DIS_SUCCESS) {
						DE_WRN("request yuv channel "
						       "fail\n");
						BSP_disp_cfg_finish(sel);
						return ret;
					}
				}
				Yuv_Channel_adjusting(sel, pfb->mode,
						      pfb->format,
						      &layer_man->para.src_win.
						      x,
						      &layer_man->para.scn_win.
						      width);
				Yuv_Channel_Set_framebuffer(sel, pfb,
							    layer_man->para.
							    src_win.x,
							    layer_man->para.
							    src_win.y);
			} else {
				layer_fb.fb_addr = pfb->addr[0];
				layer_fb.pixseq = img_sw_para_to_reg(3, 0,
								     pfb->seq);
				layer_fb.br_swap = pfb->br_swap;
				layer_fb.fb_width = pfb->size.width;
				layer_fb.offset_x = layer_man->para.src_win.x;
				layer_fb.offset_y = layer_man->para.src_win.y;
				layer_fb.format = pfb->format;
				DE_BE_Layer_Set_Framebuffer(sel, hid,
							    &layer_fb);
			}

			memcpy(&layer_man->para.fb, pfb, sizeof(__disp_fb_t));

			size = (pfb->size.width *
				layer_man->para.src_win.height *
				de_format_to_bpp(pfb->format) + 7) / 8;

			if (layer_man->para.mode ==
			    DISP_LAYER_WORK_MODE_SCALER) {
				gdisp.scaler[layer_man->scaler_index].
				    b_reg_change = TRUE;
			}
			BSP_disp_cfg_finish(sel);

			return DIS_SUCCESS;
		}
	} else {
		DE_WRN("layer %d in screen %d not inited!\n", hid, sel);
		return DIS_OBJ_NOT_INITED;
	}
}

__s32 BSP_disp_layer_get_framebuffer(__u32 sel, __u32 hid, __disp_fb_t *pfb)
{
	__layer_man_t *layer_man;

	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	if (pfb == NULL)
		return DIS_PARA_FAILED;

	layer_man = &gdisp.screen[sel].layer_manage[hid];
	if (layer_man->status & LAYER_USED) {
		if (layer_man->para.mode == DISP_LAYER_WORK_MODE_SCALER) {
			return Scaler_Get_Framebuffer(layer_man->scaler_index,
						      pfb);
		} else {
			memcpy(pfb, &layer_man->para.fb, sizeof(__disp_fb_t));
			return DIS_SUCCESS;
		}
	} else {
		DE_WRN("layer %d in screen %d not inited!\n", hid, sel);
		return DIS_OBJ_NOT_INITED;
	}
}

/*
 * if not scaler mode, ignore the src window width&height.
 */
__s32 BSP_disp_layer_set_src_window(__u32 sel, __u32 hid, __disp_rect_t *regn)
{
	__layer_man_t *layer_man;

	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	if (regn == NULL)
		return DIS_PARA_FAILED;

	if (regn->width <= 0 || regn->height <= 0)
		return DIS_PARA_FAILED;

	layer_man = &gdisp.screen[sel].layer_manage[hid];
	if (layer_man->status & LAYER_USED) {
		BSP_disp_cfg_start(sel);
		if (layer_man->para.mode == DISP_LAYER_WORK_MODE_SCALER) {
			__s32 ret = 0;

			ret = Scaler_Set_SclRegn(layer_man->scaler_index, regn);
			gdisp.scaler[layer_man->scaler_index].b_reg_change =
			    TRUE;
			BSP_disp_cfg_finish(sel);
			return ret;
		} else {
			if (get_fb_type(layer_man->para.fb.format) ==
			    DISP_FB_TYPE_YUV) {
				Yuv_Channel_adjusting(sel,
						      layer_man->para.fb.mode,
						      layer_man->para.fb.format,
						      &layer_man->para.src_win.
						      x,
						      &layer_man->para.scn_win.
						      width);
				Yuv_Channel_Set_framebuffer(sel,
							    &(layer_man->para.
							      fb), regn->x,
							    regn->y);
			} else {
				layer_src_t layer_fb;

				layer_fb.fb_addr = layer_man->para.fb.addr[0];
				layer_fb.format = layer_man->para.fb.format;
				layer_fb.pixseq =
				    img_sw_para_to_reg(3, 0,
						       layer_man->para.fb.seq);
				layer_fb.br_swap = layer_man->para.fb.br_swap;
				layer_fb.fb_width =
				    layer_man->para.fb.size.width;
				layer_fb.offset_x = regn->x;
				layer_fb.offset_y = regn->y;
				layer_fb.format = layer_man->para.fb.format;

				DE_BE_Layer_Set_Framebuffer(sel, hid,
							    &layer_fb);
			}

			layer_man->para.src_win.x = regn->x;
			layer_man->para.src_win.y = regn->y;
			layer_man->para.src_win.width = regn->width;
			layer_man->para.src_win.height = regn->height;

			BSP_disp_cfg_finish(sel);

			return DIS_SUCCESS;
		}
	} else {
		DE_WRN("layer %d in screen %d not inited!\n", hid, sel);
		return DIS_OBJ_NOT_INITED;
	}
}

__s32 BSP_disp_layer_get_src_window(__u32 sel, __u32 hid, __disp_rect_t *regn)
{
	__layer_man_t *layer_man;

	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	if (regn == NULL) {
		DE_WRN("input parameter can't be null!\n");
		return DIS_PARA_FAILED;
	}

	layer_man = &gdisp.screen[sel].layer_manage[hid];
	if (layer_man->status & LAYER_USED) {
		if (layer_man->para.mode == DISP_LAYER_WORK_MODE_SCALER) {
			return Scaler_Get_SclRegn(layer_man->scaler_index,
						  regn);
		} else {
			regn->x = layer_man->para.src_win.x;
			regn->y = layer_man->para.src_win.y;
			regn->width = layer_man->para.scn_win.width;
			regn->height = layer_man->para.scn_win.height;
			return 0;
		}
	} else {
		DE_WRN("layer %d in screen %d not inited!\n", hid, sel);
		return DIS_OBJ_NOT_INITED;
	}
}

__s32 BSP_disp_layer_set_screen_window(__u32 sel, __u32 hid,
				       __disp_rect_t *regn)
{
	__disp_rectsz_t outsize;
	__layer_man_t *layer_man;

	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	if (regn == NULL) {
		DE_WRN("para is null in BSP_disp_layer_set_screen_window\n");
		return DIS_PARA_FAILED;
	}
	if (regn->width <= 0 || regn->height <= 0) {
		DE_WRN
		    ("width:%x,height:%x in BSP_disp_layer_set_screen_window\n",
		     regn->width, regn->height);
		return DIS_PARA_FAILED;
	}

	layer_man = &gdisp.screen[sel].layer_manage[hid];

	if (layer_man->status & LAYER_USED) {
		BSP_disp_cfg_start(sel);
		if (layer_man->para.mode == DISP_LAYER_WORK_MODE_SCALER) {
			__s32 ret;

			/*
			 * when scaler display on a interlace screen
			 * (480i, ntsc etc), scaler window must be even
			 * vertical offset
			 */
			regn->y &= ((gdisp.screen[sel].b_out_interlace == 1) ?
				    0xfffffffe : 0xffffffff);

			outsize.height = regn->height;
			outsize.width = regn->width;

			ret = Scaler_Set_Output_Size(layer_man->scaler_index,
						     &outsize);
			if (ret != DIS_SUCCESS) {
				DE_WRN("Scaler_Set_Output_Size fail!\n");
				BSP_disp_cfg_finish(sel);
				return ret;
			}
		}
		if (get_fb_type(layer_man->para.fb.format) == DISP_FB_TYPE_YUV
		    && layer_man->para.mode != DISP_LAYER_WORK_MODE_SCALER) {
			Yuv_Channel_adjusting(sel, layer_man->para.fb.mode,
					      layer_man->para.fb.format,
					      &layer_man->para.src_win.x,
					      &regn->width);
		}
		DE_BE_Layer_Set_Screen_Win(sel, hid, regn);
		layer_man->para.scn_win.x = regn->x;
		layer_man->para.scn_win.y = regn->y;
		layer_man->para.scn_win.width = regn->width;
		layer_man->para.scn_win.height = regn->height;

		if (layer_man->para.mode == DISP_LAYER_WORK_MODE_SCALER) {
			gdisp.scaler[layer_man->scaler_index].b_reg_change =
			    TRUE;
		}
		BSP_disp_cfg_finish(sel);

		return DIS_SUCCESS;
	} else {
		DE_WRN("layer %d in screen %d not inited!\n", hid, sel);
		return DIS_OBJ_NOT_INITED;
	}

}

__s32 BSP_disp_layer_get_screen_window(__u32 sel, __u32 hid,
				       __disp_rect_t *regn)
{
	__layer_man_t *layer_man;

	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	if (regn == NULL)
		return DIS_PARA_FAILED;

	layer_man = &gdisp.screen[sel].layer_manage[hid];
	if (layer_man->status & LAYER_USED) {
		regn->x = layer_man->para.scn_win.x;
		regn->y = layer_man->para.scn_win.y;
		regn->width = layer_man->para.scn_win.width;
		regn->height = layer_man->para.scn_win.height;

		return DIS_SUCCESS;
	} else {
		DE_WRN("layer %d in screen %d not inited!\n", hid, sel);
		return DIS_OBJ_NOT_INITED;
	}
}

__s32 BSP_disp_layer_set_para(__u32 sel, __u32 hid,
			      __disp_layer_info_t *player)
{
	__s32 ret;
	__layer_man_t *layer_man;
	__u32 prio_tmp = 0;
	__u32 size;

	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	layer_man = &gdisp.screen[sel].layer_manage[hid];
	if (player->b_from_screen)
		player->mode = DISP_LAYER_WORK_MODE_SCALER;

	if (layer_man->status & LAYER_USED) {
		BSP_disp_cfg_start(sel);
		if (player->mode != DISP_LAYER_WORK_MODE_NORMAL ||
		    get_fb_type(player->fb.format) != DISP_FB_TYPE_YUV) {
			if (layer_man->byuv_ch)
				Yuv_Channel_Release(sel, hid);
		}
		if (player->mode != DISP_LAYER_WORK_MODE_SCALER) {
			if (layer_man->para.mode ==
			    DISP_LAYER_WORK_MODE_SCALER) {
				Scaler_Release(layer_man->scaler_index, TRUE);
				DE_BE_Layer_Video_Enable(sel, hid, FALSE);
				DE_BE_Layer_Video_Ch_Sel(sel, hid, 0);
				layer_man->para.mode =
					DISP_LAYER_WORK_MODE_NORMAL;
			}
		}

		if (player->mode == DISP_LAYER_WORK_MODE_SCALER) {
			__disp_scaler_t *scaler;

			if (layer_man->para.mode !=
			    DISP_LAYER_WORK_MODE_SCALER) {
				__u32 format = DISP_FORMAT_ARGB8888;

				ret = Scaler_Request(0xff);
				if (ret < 0) {
					DE_WRN("request scaler layer fail!\n");
					BSP_disp_cfg_finish(sel);
					return DIS_NO_RES;
				}
				DE_SCAL_Start(ret);

				format = DISP_FORMAT_ARGB8888;
				DE_BE_Layer_Set_Format(sel, hid, format, FALSE,
						       DISP_SEQ_ARGB);
				DE_BE_Layer_Video_Enable(sel, hid, TRUE);
				DE_BE_Layer_Video_Ch_Sel(sel, hid, ret);
				layer_man->scaler_index = ret;
				layer_man->para.mode =
					DISP_LAYER_WORK_MODE_SCALER;
				gdisp.scaler[ret].screen_index = sel;
			}
			scaler = &(gdisp.scaler[layer_man->scaler_index]);

			player->scn_win.y &=
				(gdisp.screen[sel].b_out_interlace == 1) ?
				0xfffffffe : 0xffffffff;
			scaler->out_fb.seq = DISP_SEQ_ARGB;
			scaler->out_fb.format = DISP_FORMAT_RGB888;
			scaler->out_size.height = player->scn_win.height;
			scaler->out_size.width = player->scn_win.width;
			if (player->b_from_screen) {
				scaler->src_win.x = 0;
				scaler->src_win.y = 0;
				scaler->src_win.width =
					BSP_disp_get_screen_width(1 - sel);
				scaler->src_win.height =
					BSP_disp_get_screen_height(1 - sel);
				scaler->in_fb.addr[0] = 0;
				scaler->in_fb.size.width =
					BSP_disp_get_screen_width(1 - sel);
				scaler->in_fb.size.height =
					BSP_disp_get_screen_height(1 - sel);
				scaler->in_fb.format = DISP_FORMAT_ARGB8888;
				scaler->in_fb.seq = DISP_SEQ_ARGB;
				scaler->in_fb.mode = DISP_MOD_INTERLEAVED;
				scaler->in_fb.br_swap = FALSE;
				scaler->in_fb.cs_mode = DISP_BT601;
				image_clk_on(sel);
				Image_open(1 - sel);
				DE_BE_Output_Select(1 - sel, 6 +
						    layer_man->scaler_index);
				DE_SCAL_Input_Select(layer_man->scaler_index,
						     6 + (1 - sel));
				gdisp.screen[1 - sel].image_output_type =
					IMAGE_OUTPUT_SCALER;
			} else {
				scaler->src_win.x = player->src_win.x;
				scaler->src_win.y = player->src_win.y;
				scaler->src_win.width = player->src_win.width;
				scaler->src_win.height = player->src_win.height;
				memcpy(&scaler->in_fb, &player->fb,
				       sizeof(__disp_fb_t));
				DE_SCAL_Input_Select(layer_man->scaler_index,
						     0);
			}
			scaler->b_trd_out = player->b_trd_out;
			scaler->out_trd_mode = player->out_trd_mode;
			DE_SCAL_Output_Select(layer_man->scaler_index, sel);
			Scaler_Set_Para(layer_man->scaler_index, scaler);
		} else {
			/* yuv channel */
			if (get_fb_type(player->fb.format) ==
			    DISP_FB_TYPE_YUV) {
				if (layer_man->byuv_ch == FALSE) {
					__s32 err = 0;

					err = Yuv_Channel_Request(sel, hid);
					if (err != DIS_SUCCESS) {
						DE_WRN("request yuv channel "
						       "fail\n");
						BSP_disp_cfg_finish(sel);
						return err;
					}
				}
				Yuv_Channel_adjusting(sel, player->fb.mode,
						      player->fb.format,
						      &player->src_win.x,
						      &player->scn_win.width);
				Yuv_Channel_Set_framebuffer(sel, &(player->fb),
							    player->src_win.x,
							    player->src_win.y);
			} else { /* normal rgb */
				layer_src_t layer_fb;
				__u32 bpp, size;

				layer_fb.fb_addr = player->fb.addr[0];
				layer_fb.format = player->fb.format;
				layer_fb.pixseq =
				    img_sw_para_to_reg(3, 0, player->fb.seq);
				layer_fb.br_swap = player->fb.br_swap;
				layer_fb.fb_width = player->fb.size.width;
				layer_fb.offset_x = player->src_win.x;
				layer_fb.offset_y = player->src_win.y;

				bpp = DE_BE_Format_To_Bpp(sel, layer_fb.format);
				size = (player->fb.size.width *
					player->scn_win.height * bpp + 7) / 8;

				DE_BE_Layer_Set_Framebuffer(sel, hid,
							    &layer_fb);
			}
		}

		DE_BE_Layer_Set_Work_Mode(sel, hid, player->mode);
		DE_BE_Layer_Set_Pipe(sel, hid, player->pipe);
		DE_BE_Layer_Alpha_Enable(sel, hid, player->alpha_en);
		DE_BE_Layer_Set_Alpha_Value(sel, hid, player->alpha_val);
		DE_BE_Layer_ColorKey_Enable(sel, hid, player->ck_enable);
		DE_BE_Layer_Set_Screen_Win(sel, hid, &player->scn_win);

		prio_tmp = layer_man->para.prio;
		memcpy(&(layer_man->para), player, sizeof(__disp_layer_info_t));
		layer_man->para.prio = prio_tmp; /* ignore the prio setting */
		if (layer_man->para.mode == DISP_LAYER_WORK_MODE_SCALER) {
			layer_man->para.src_win.width = player->src_win.width;
			layer_man->para.src_win.height = player->src_win.height;
			layer_man->para.b_from_screen = player->b_from_screen;
		}

		size = (player->fb.size.width * player->src_win.height *
			de_format_to_bpp(player->fb.format) + 7) / 8;

		if (layer_man->para.mode == DISP_LAYER_WORK_MODE_SCALER) {
			gdisp.scaler[layer_man->scaler_index].b_reg_change =
			    TRUE;
		}
		BSP_disp_cfg_finish(sel);

		return DIS_SUCCESS;
	} else {
		DE_WRN("layer %d in screen %d not inited!\n", hid, sel);
		return DIS_OBJ_NOT_INITED;
	}
}

/*
 * todo
 */
__s32
BSP_disp_layer_get_para(__u32 sel, __u32 hid, __disp_layer_info_t *player)
{
	__layer_man_t *layer_man;

	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	layer_man = &gdisp.screen[sel].layer_manage[hid];
	if (layer_man->status & LAYER_USED) {
		memcpy(player, &layer_man->para, sizeof(__disp_layer_info_t));

		return DIS_SUCCESS;
	} else {
		DE_WRN("layer %d in screen %d not inited!\n", hid, sel);
		return DIS_OBJ_NOT_INITED;
	}
}

__s32 BSP_disp_layer_set_smooth(__u32 sel, __u32 hid,
				__disp_video_smooth_t mode)
{
	__layer_man_t *layer_man;

	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	layer_man = &gdisp.screen[sel].layer_manage[hid];
	if (layer_man->status & LAYER_USED) {
		if (layer_man->para.mode == DISP_LAYER_WORK_MODE_SCALER) {
			BSP_disp_scaler_set_smooth(layer_man->scaler_index,
						   mode);
			return DIS_SUCCESS;
		} else {
			DE_WRN("layer not scaler mode!\n");
			return DIS_NOT_SUPPORT;
		}
	} else {
		DE_WRN("layer %d in screen %d not inited!\n", hid, sel);
		return DIS_OBJ_NOT_INITED;
	}
}

__s32 BSP_disp_layer_get_smooth(__u32 sel, __u32 hid)
{
	__layer_man_t *layer_man;

	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	layer_man = &gdisp.screen[sel].layer_manage[hid];
	if (layer_man->status & LAYER_USED) {
		if (layer_man->para.mode == DISP_LAYER_WORK_MODE_SCALER) {
			__s32 mode;
			mode = (__s32)
				BSP_disp_scaler_get_smooth(layer_man->
							   scaler_index);
			return mode;
		} else {
			DE_WRN("layer not scaler mode!\n");
			return DIS_NOT_SUPPORT;
		}
	} else {
		DE_WRN("layer %d in screen %d not inited!\n", hid, sel);
		return DIS_OBJ_NOT_INITED;
	}
}

__s32 BSP_disp_layer_set_bright(__u32 sel, __u32 hid, __u32 bright)
{
	__layer_man_t *layer_man;

	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	layer_man = &gdisp.screen[sel].layer_manage[hid];
	if ((layer_man->status & LAYER_USED) &&
	    layer_man->para.mode == DISP_LAYER_WORK_MODE_SCALER) {
		gdisp.scaler[layer_man->scaler_index].bright = bright;
		if (gdisp.scaler[layer_man->scaler_index].enhance_en == TRUE) {
			Scaler_Set_Enhance(layer_man->scaler_index,
					   gdisp.scaler[layer_man->
							scaler_index].bright,
					   gdisp.scaler[layer_man->
							scaler_index].contrast,
					   gdisp.scaler[layer_man->
							scaler_index].
					   saturation,
					   gdisp.scaler[layer_man->
							scaler_index].hue);
		}

		return DIS_SUCCESS;
	}
	return DIS_NOT_SUPPORT;
}

__s32 BSP_disp_layer_get_bright(__u32 sel, __u32 hid)
{
	__layer_man_t *layer_man;

	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	layer_man = &gdisp.screen[sel].layer_manage[hid];
	if ((layer_man->status & LAYER_USED) &&
	    layer_man->para.mode == DISP_LAYER_WORK_MODE_SCALER) {
		return gdisp.scaler[layer_man->scaler_index].bright;
	}
	return DIS_NOT_SUPPORT;
}

__s32 BSP_disp_layer_set_contrast(__u32 sel, __u32 hid, __u32 contrast)
{
	__layer_man_t *layer_man;

	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	layer_man = &gdisp.screen[sel].layer_manage[hid];
	if ((layer_man->status & LAYER_USED) &&
	    layer_man->para.mode == DISP_LAYER_WORK_MODE_SCALER) {
		gdisp.scaler[layer_man->scaler_index].contrast = contrast;
		if (gdisp.scaler[layer_man->scaler_index].enhance_en == TRUE) {
			Scaler_Set_Enhance(layer_man->scaler_index,
					   gdisp.scaler[layer_man->
							scaler_index].bright,
					   gdisp.scaler[layer_man->
							scaler_index].contrast,
					   gdisp.scaler[layer_man->
							scaler_index].
					   saturation,
					   gdisp.scaler[layer_man->
							scaler_index].hue);
		}

		return DIS_SUCCESS;
	}
	return DIS_NOT_SUPPORT;
}

__s32 BSP_disp_layer_get_contrast(__u32 sel, __u32 hid)
{
	__layer_man_t *layer_man;

	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	layer_man = &gdisp.screen[sel].layer_manage[hid];
	if ((layer_man->status & LAYER_USED) &&
	    layer_man->para.mode == DISP_LAYER_WORK_MODE_SCALER) {
		return gdisp.scaler[layer_man->scaler_index].contrast;
	}
	return DIS_NOT_SUPPORT;
}

__s32 BSP_disp_layer_set_saturation(__u32 sel, __u32 hid, __u32 saturation)
{
	__layer_man_t *layer_man;

	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	layer_man = &gdisp.screen[sel].layer_manage[hid];
	if ((layer_man->status & LAYER_USED) &&
	    layer_man->para.mode == DISP_LAYER_WORK_MODE_SCALER) {
		gdisp.scaler[layer_man->scaler_index].saturation = saturation;
		if (gdisp.scaler[layer_man->scaler_index].enhance_en == TRUE) {
			Scaler_Set_Enhance(layer_man->scaler_index,
					   gdisp.scaler[layer_man->
							scaler_index].bright,
					   gdisp.scaler[layer_man->
							scaler_index].contrast,
					   gdisp.scaler[layer_man->
							scaler_index].
					   saturation,
					   gdisp.scaler[layer_man->
							scaler_index].hue);
		}

		return DIS_SUCCESS;
	}
	return DIS_NOT_SUPPORT;
}

__s32 BSP_disp_layer_get_saturation(__u32 sel, __u32 hid)
{
	__layer_man_t *layer_man;

	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	layer_man = &gdisp.screen[sel].layer_manage[hid];
	if ((layer_man->status & LAYER_USED) &&
	    layer_man->para.mode == DISP_LAYER_WORK_MODE_SCALER) {
		return gdisp.scaler[layer_man->scaler_index].saturation;
	}
	return DIS_NOT_SUPPORT;
}

__s32 BSP_disp_layer_set_hue(__u32 sel, __u32 hid, __u32 hue)
{
	__layer_man_t *layer_man;

	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	layer_man = &gdisp.screen[sel].layer_manage[hid];
	if ((layer_man->status & LAYER_USED) &&
	    layer_man->para.mode == DISP_LAYER_WORK_MODE_SCALER) {
		gdisp.scaler[layer_man->scaler_index].hue = hue;
		if (gdisp.scaler[layer_man->scaler_index].enhance_en == TRUE) {
			Scaler_Set_Enhance(layer_man->scaler_index,
					   gdisp.scaler[layer_man->
							scaler_index].bright,
					   gdisp.scaler[layer_man->
							scaler_index].contrast,
					   gdisp.scaler[layer_man->
							scaler_index].
					   saturation,
					   gdisp.scaler[layer_man->
							scaler_index].hue);
		}

		return DIS_SUCCESS;
	}
	return DIS_NOT_SUPPORT;
}

__s32 BSP_disp_layer_get_hue(__u32 sel, __u32 hid)
{
	__layer_man_t *layer_man;

	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	layer_man = &gdisp.screen[sel].layer_manage[hid];
	if ((layer_man->status & LAYER_USED) &&
	    layer_man->para.mode == DISP_LAYER_WORK_MODE_SCALER) {
		return gdisp.scaler[layer_man->scaler_index].hue;
	}
	return DIS_NOT_SUPPORT;
}

__s32 BSP_disp_layer_enhance_enable(__u32 sel, __u32 hid, __bool enable)
{
	__layer_man_t *layer_man;

	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	layer_man = &gdisp.screen[sel].layer_manage[hid];
	if ((layer_man->status & LAYER_USED) &&
	    layer_man->para.mode == DISP_LAYER_WORK_MODE_SCALER) {
		if (enable == FALSE) {
			Scaler_Set_Enhance(layer_man->scaler_index, 32, 32, 32,
					   32);
		} else {
			Scaler_Set_Enhance(layer_man->scaler_index,
					   gdisp.scaler[layer_man->
							scaler_index].bright,
					   gdisp.scaler[layer_man->
							scaler_index].contrast,
					   gdisp.scaler[layer_man->
							scaler_index].
					   saturation,
					   gdisp.scaler[layer_man->
							scaler_index].hue);
		}
		gdisp.scaler[layer_man->scaler_index].enhance_en = enable;
		return DIS_SUCCESS;
	}
	return DIS_NOT_SUPPORT;
}

__s32 BSP_disp_layer_get_enhance_enable(__u32 sel, __u32 hid)
{
	__layer_man_t *layer_man;

	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	layer_man = &gdisp.screen[sel].layer_manage[hid];
	if ((layer_man->status & LAYER_USED) &&
	    layer_man->para.mode == DISP_LAYER_WORK_MODE_SCALER) {
		return gdisp.scaler[layer_man->scaler_index].enhance_en;
	}
	return DIS_NOT_SUPPORT;
}

__s32 BSP_disp_layer_vpp_enable(__u32 sel, __u32 hid, __bool enable)
{
	__layer_man_t *layer_man;

	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	layer_man = &gdisp.screen[sel].layer_manage[hid];
	if ((layer_man->status & LAYER_USED) &&
	    (layer_man->para.mode == DISP_LAYER_WORK_MODE_SCALER) &&
	    (get_fb_type(layer_man->para.fb.format) == DISP_FB_TYPE_YUV)) {
		DE_SCAL_Vpp_Enable(layer_man->scaler_index, enable);
		gdisp.scaler[layer_man->scaler_index].vpp_en = enable;
		return DIS_SUCCESS;
	}
	return DIS_NOT_SUPPORT;
}

__s32 BSP_disp_layer_get_vpp_enable(__u32 sel, __u32 hid)
{
	__layer_man_t *layer_man;

	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	layer_man = &gdisp.screen[sel].layer_manage[hid];
	if ((layer_man->status & LAYER_USED) &&
	    (layer_man->para.mode == DISP_LAYER_WORK_MODE_SCALER) &&
	    (get_fb_type(layer_man->para.fb.format) == DISP_FB_TYPE_YUV)) {
		return gdisp.scaler[layer_man->scaler_index].vpp_en;
	}
	return DIS_NOT_SUPPORT;
}

__s32 BSP_disp_layer_set_luma_sharp_level(__u32 sel, __u32 hid, __u32 level)
{
	__layer_man_t *layer_man;

	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	layer_man = &gdisp.screen[sel].layer_manage[hid];
	if ((layer_man->status & LAYER_USED) &&
	    (layer_man->para.mode == DISP_LAYER_WORK_MODE_SCALER) &&
	    (get_fb_type(layer_man->para.fb.format) == DISP_FB_TYPE_YUV)) {
		DE_SCAL_Vpp_Set_Luma_Sharpness_Level(layer_man->scaler_index,
						     level);
		gdisp.scaler[layer_man->scaler_index].luma_sharpe_level = level;
		return DIS_SUCCESS;
	}
	return DIS_NOT_SUPPORT;
}

__s32 BSP_disp_layer_get_luma_sharp_level(__u32 sel, __u32 hid)
{
	__layer_man_t *layer_man;

	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	layer_man = &gdisp.screen[sel].layer_manage[hid];
	if ((layer_man->status & LAYER_USED) &&
	    (layer_man->para.mode == DISP_LAYER_WORK_MODE_SCALER) &&
	    (get_fb_type(layer_man->para.fb.format) == DISP_FB_TYPE_YUV)) {
		return gdisp.scaler[layer_man->scaler_index].luma_sharpe_level;
	}
	return DIS_NOT_SUPPORT;
}

__s32 BSP_disp_layer_set_chroma_sharp_level(__u32 sel, __u32 hid, __u32 level)
{
	__layer_man_t *layer_man;

	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	layer_man = &gdisp.screen[sel].layer_manage[hid];
	if ((layer_man->status & LAYER_USED) &&
	    (layer_man->para.mode == DISP_LAYER_WORK_MODE_SCALER) &&
	    (get_fb_type(layer_man->para.fb.format) == DISP_FB_TYPE_YUV)) {
		DE_SCAL_Vpp_Set_Chroma_Sharpness_Level(layer_man->scaler_index,
						       level);
		gdisp.scaler[layer_man->scaler_index].chroma_sharpe_level =
			level;
		return DIS_SUCCESS;
	}
	return DIS_NOT_SUPPORT;
}

__s32 BSP_disp_layer_get_chroma_sharp_level(__u32 sel, __u32 hid)
{
	__layer_man_t *layer_man;

	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	layer_man = &gdisp.screen[sel].layer_manage[hid];
	if ((layer_man->status & LAYER_USED) &&
	    (layer_man->para.mode == DISP_LAYER_WORK_MODE_SCALER) &&
	    (get_fb_type(layer_man->para.fb.format) == DISP_FB_TYPE_YUV)) {
		return gdisp.scaler[layer_man->scaler_index].
			chroma_sharpe_level;
	}
	return DIS_NOT_SUPPORT;
}

__s32 BSP_disp_layer_set_white_exten_level(__u32 sel, __u32 hid, __u32 level)
{
	__layer_man_t *layer_man;

	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	layer_man = &gdisp.screen[sel].layer_manage[hid];
	if ((layer_man->status & LAYER_USED) &&
	    (layer_man->para.mode == DISP_LAYER_WORK_MODE_SCALER) &&
	    (get_fb_type(layer_man->para.fb.format) == DISP_FB_TYPE_YUV)) {
		DE_SCAL_Vpp_Set_White_Level_Extension(layer_man->scaler_index,
						      level);
		gdisp.scaler[layer_man->scaler_index].while_exten_level = level;
		return DIS_SUCCESS;
	}
	return DIS_NOT_SUPPORT;
}

__s32 BSP_disp_layer_get_white_exten_level(__u32 sel, __u32 hid)
{
	__layer_man_t *layer_man;

	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	layer_man = &gdisp.screen[sel].layer_manage[hid];
	if ((layer_man->status & LAYER_USED) &&
	    (layer_man->para.mode == DISP_LAYER_WORK_MODE_SCALER) &&
	    (get_fb_type(layer_man->para.fb.format) == DISP_FB_TYPE_YUV)) {
		return gdisp.scaler[layer_man->scaler_index].while_exten_level;
	}
	return DIS_NOT_SUPPORT;
}

__s32 BSP_disp_layer_set_black_exten_level(__u32 sel, __u32 hid, __u32 level)
{
	__layer_man_t *layer_man;

	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	layer_man = &gdisp.screen[sel].layer_manage[hid];
	if ((layer_man->status & LAYER_USED) &&
	    (layer_man->para.mode == DISP_LAYER_WORK_MODE_SCALER) &&
	    (get_fb_type(layer_man->para.fb.format) == DISP_FB_TYPE_YUV)) {
		DE_SCAL_Vpp_Set_Black_Level_Extension(layer_man->scaler_index,
						      level);
		gdisp.scaler[layer_man->scaler_index].black_exten_level = level;
		return DIS_SUCCESS;
	}
	return DIS_NOT_SUPPORT;
}

__s32 BSP_disp_layer_get_black_exten_level(__u32 sel, __u32 hid)
{
	__layer_man_t *layer_man;

	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	layer_man = &gdisp.screen[sel].layer_manage[hid];
	if ((layer_man->status & LAYER_USED) &&
	    (layer_man->para.mode == DISP_LAYER_WORK_MODE_SCALER) &&
	    (get_fb_type(layer_man->para.fb.format) == DISP_FB_TYPE_YUV)) {
		return gdisp.scaler[layer_man->scaler_index].black_exten_level;
	}
	return DIS_NOT_SUPPORT;
}
