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

#include "disp_scaler.h"
#include "disp_display.h"
#include "disp_event.h"
#include "disp_layer.h"
#include "disp_clk.h"
#include "disp_lcd.h"
#include "disp_de.h"

/*
 * 0:scaler input pixel format
 * 1:scaler input yuv mode
 * 2:scaler input pixel sequence
 * 3:scaler output format
 */
__s32 Scaler_sw_para_to_reg(__u8 type, __u8 value)
{
	if (type == 0) { /* scaler input pixel format */
		if (value == DISP_FORMAT_YUV444)
			return DE_SCAL_INYUV444;
		else if (value == DISP_FORMAT_YUV420)
			return DE_SCAL_INYUV420;
		else if (value == DISP_FORMAT_YUV422)
			return DE_SCAL_INYUV422;
		else if (value == DISP_FORMAT_YUV411)
			return DE_SCAL_INYUV411;
		else if (value == DISP_FORMAT_CSIRGB)
			return DE_SCAL_INCSIRGB;
		else if (value == DISP_FORMAT_ARGB8888)
			return DE_SCAL_INRGB888;
		else if (value == DISP_FORMAT_RGB888)
			return DE_SCAL_INRGB888;
		else
			DE_WRN("not supported scaler input pixel format:%d in "
			       "Scaler_sw_para_to_reg\n", value);

	} else if (type == 1) { /* scaler input mode */
		if (value == DISP_MOD_INTERLEAVED)
			return DE_SCAL_INTER_LEAVED;
		else if (value == DISP_MOD_MB_PLANAR)
			return DE_SCAL_PLANNARMB;
		else if (value == DISP_MOD_NON_MB_PLANAR)
			return DE_SCAL_PLANNAR;
		else if (value == DISP_MOD_NON_MB_UV_COMBINED)
			return DE_SCAL_UVCOMBINED;
		else if (value == DISP_MOD_MB_UV_COMBINED)
			return DE_SCAL_UVCOMBINEDMB;
		else
			DE_WRN("not supported scaler input mode:%d in "
			       "Scaler_sw_para_to_reg\n", value);

	} else if (type == 2) { /* scaler input pixel sequence */
		if (value == DISP_SEQ_UYVY)
			return DE_SCAL_UYVY;
		else if (value == DISP_SEQ_YUYV)
			return DE_SCAL_YUYV;
		else if (value == DISP_SEQ_VYUY)
			return DE_SCAL_VYUY;
		else if (value == DISP_SEQ_YVYU)
			return DE_SCAL_YVYU;
		else if (value == DISP_SEQ_AYUV)
			return DE_SCAL_AYUV;
		else if (value == DISP_SEQ_UVUV)
			return DE_SCAL_UVUV;
		else if (value == DISP_SEQ_VUVU)
			return DE_SCAL_VUVU;
		else if (value == DISP_SEQ_ARGB)
			return DE_SCAL_ARGB;
		else if (value == DISP_SEQ_BGRA)
			return DE_SCAL_BGRA;
		else if (value == DISP_SEQ_P3210)
			return 0;
		else
			DE_WRN("not supported scaler input pixel sequence:%d "
			       "in Scaler_sw_para_to_reg\n", value);

	} else if (type == 3) { /* scaler output value */
		if (value == DISP_FORMAT_YUV444)
			return DE_SCAL_OUTPYUV444;
		else if (value == DISP_FORMAT_YUV422)
			return DE_SCAL_OUTPYUV422;
		else if (value == DISP_FORMAT_YUV420)
			return DE_SCAL_OUTPYUV420;
		else if (value == DISP_FORMAT_YUV411)
			return DE_SCAL_OUTPYUV411;
		else if (value == DISP_FORMAT_ARGB8888)
			return DE_SCAL_OUTI0RGB888;
		else if (value == DISP_FORMAT_RGB888)
			return DE_SCAL_OUTPRGB888;
		else
			DE_WRN("not supported scaler output value:%d in "
			       "Scaler_sw_para_to_reg\n", value);

	}
	DE_WRN("not supported type:%d in Scaler_sw_para_to_reg\n", type);
	return DIS_FAIL;
}

/*
 * 0: 3d in mode
 * 1: 3d out mode
 */
__s32 Scaler_3d_sw_para_to_reg(__u32 type, __u32 mode, __bool b_out_interlace)
{
	if (type == 0) {
		switch (mode) {
		case DISP_3D_SRC_MODE_TB:
			return DE_SCAL_3DIN_TB;

		case DISP_3D_SRC_MODE_FP:
			return DE_SCAL_3DIN_FP;

		case DISP_3D_SRC_MODE_SSF:
			return DE_SCAL_3DIN_SSF;

		case DISP_3D_SRC_MODE_SSH:
			return DE_SCAL_3DIN_SSH;

		case DISP_3D_SRC_MODE_LI:
			return DE_SCAL_3DIN_LI;

		default:
			DE_WRN("not supported 3d in mode:%d in "
			       "Scaler_3d_sw_para_to_reg\n", mode);
			return DIS_FAIL;
		}
	} else if (type == 1) {
		switch (mode) {
		case DISP_3D_OUT_MODE_CI_1:
			return DE_SCAL_3DOUT_CI_1;

		case DISP_3D_OUT_MODE_CI_2:
			return DE_SCAL_3DOUT_CI_2;

		case DISP_3D_OUT_MODE_CI_3:
			return DE_SCAL_3DOUT_CI_3;

		case DISP_3D_OUT_MODE_CI_4:
			return DE_SCAL_3DOUT_CI_4;

		case DISP_3D_OUT_MODE_LIRGB:
			return DE_SCAL_3DOUT_LIRGB;

		case DISP_3D_OUT_MODE_TB:
			return DE_SCAL_3DOUT_HDMI_TB;

		case DISP_3D_OUT_MODE_FP:
			if (b_out_interlace == TRUE)
				return DE_SCAL_3DOUT_HDMI_FPI;
			else
				return DE_SCAL_3DOUT_HDMI_FPP;

		case DISP_3D_OUT_MODE_SSF:
			return DE_SCAL_3DOUT_HDMI_SSF;

		case DISP_3D_OUT_MODE_SSH:
			return DE_SCAL_3DOUT_HDMI_SSH;

		case DISP_3D_OUT_MODE_LI:
			return DE_SCAL_3DOUT_HDMI_LI;

		case DISP_3D_OUT_MODE_FA:
			return DE_SCAL_3DOUT_HDMI_FA;

		default:
			DE_WRN("not supported 3d output mode:%d in "
			       "Scaler_3d_sw_para_to_reg\n", mode);
			return DIS_FAIL;
		}
	}

	return DIS_FAIL;
}

static irqreturn_t Scaler_event_proc(int irq, void *parg)
{
	__u8 fe_intflags, be_intflags;
	__u32 sel = (__u32) parg;

	fe_intflags = DE_SCAL_QueryINT(sel);
	be_intflags = DE_BE_QueryINT(sel);
	DE_SCAL_ClearINT(sel, fe_intflags);
	DE_BE_ClearINT(sel, be_intflags);

	DE_INF("scaler %d interrupt, scal_int_status:0x%x!\n", sel,
	       fe_intflags);

	if (be_intflags & DE_IMG_REG_LOAD_FINISH)
		LCD_line_event_proc(sel);

	if (fe_intflags & DE_WB_END_IE) {
		DE_SCAL_DisableINT(sel, DE_FE_INTEN_ALL);

		if (gdisp.scaler[sel].b_scaler_finished == 1 &&
		    (&gdisp.scaler[sel].scaler_queue != NULL)) {
			gdisp.scaler[sel].b_scaler_finished = 2;
			wake_up_interruptible(&gdisp.scaler[sel].scaler_queue);
		} else
			__wrn("not scaler %d begin in DRV_scaler_finish\n",
			      sel);
	}

	return OSAL_IRQ_RETURN;
}

__s32 Scaler_Init(__u32 sel)
{
	irqreturn_t ret;

	scaler_clk_init(sel);
	DE_SCAL_EnableINT(sel, DE_WB_END_IE);

	if (sel == 0)
		ret = request_irq(INTC_IRQNO_SCALER0, Scaler_event_proc,
				  IRQF_DISABLED, "sunxi scaler0", (void *)sel);
	else if (sel == 1)
		ret = request_irq(INTC_IRQNO_SCALER1, Scaler_event_proc,
				  IRQF_DISABLED, "sunxi scaler1", (void *)sel);

	return DIS_SUCCESS;
}

__s32 Scaler_Exit(__u32 sel)
{
	if (sel == 0) {
		disable_irq(INTC_IRQNO_SCALER0);
		free_irq(INTC_IRQNO_SCALER0, (void *)sel);
	} else if (sel == 1) {
		disable_irq(INTC_IRQNO_SCALER1);
		free_irq(INTC_IRQNO_SCALER1, (void *)sel);
	}

	DE_SCAL_DisableINT(sel, DE_WB_END_IE);
	DE_SCAL_Reset(sel);
	DE_SCAL_Disable(sel);
	scaler_clk_off(sel);
	return DIS_SUCCESS;
}

__s32 Scaler_open(__u32 sel)
{
	DE_INF("scaler %d open\n", sel);

	scaler_clk_on(sel);
	DE_SCAL_Reset(sel);
	DE_SCAL_Enable(sel);

	return DIS_SUCCESS;
}

__s32 Scaler_close(__u32 sel)
{
	DE_INF("scaler %d close\n", sel);

	DE_SCAL_Reset(sel);
	DE_SCAL_Disable(sel);
	scaler_clk_off(sel);

	memset(&gdisp.scaler[sel], 0, sizeof(__disp_scaler_t));
	gdisp.scaler[sel].bright = 32;
	gdisp.scaler[sel].contrast = 32;
	gdisp.scaler[sel].saturation = 32;
	gdisp.scaler[sel].hue = 32;
	gdisp.scaler[sel].status &= ~SCALER_USED;

	return DIS_SUCCESS;
}

__s32 Scaler_Request(__u32 sel)
{
	__s32 ret = DIS_NO_RES;

	DE_INF("Scaler_Request,%d\n", sel);

#ifdef CONFIG_ARCH_SUN5I
	sel = 0; /* only one scaler */
#endif

	if (sel == 0) { /* request scaler0 */
		if (!(gdisp.scaler[0].status & SCALER_USED))
			ret = 0;
	} else if (sel == 1) { /* request scaler1 */
		if (!(gdisp.scaler[1].status & SCALER_USED))
			ret = 1;
	} else { /* request any scaler */
		if (!(gdisp.scaler[0].status & SCALER_USED))
			ret = 0;
		else if (!(gdisp.scaler[1].status & SCALER_USED))
			ret = 1;
	}

	if (ret == 0 || ret == 1) {
		Scaler_open(ret);
		gdisp.scaler[ret].b_close = FALSE;
		gdisp.scaler[ret].status |= SCALER_USED;
	} else
		DE_WRN("request scaler fail\n");

	return ret;
}

__s32 Scaler_Release(__u32 sel, __bool b_display)
{
	DE_INF("Scaler_Release:%d\n", sel);

	DE_SCAL_Set_Di_Ctrl(sel, 0, 0, 0, 0);
	if (b_display == FALSE ||
	    BSP_disp_get_output_type(sel) == DISP_OUTPUT_TYPE_NONE) {
		Scaler_close(sel);
	} else {
		gdisp.scaler[sel].b_close = TRUE;
	}

	return DIS_SUCCESS;
}

/*
 *  keep the source window
 */
__s32 Scaler_Set_Framebuffer(__u32 sel, __disp_fb_t *pfb)
{
	__scal_buf_addr_t scal_addr;
	__scal_src_size_t in_size;
	__scal_out_size_t out_size;
	__scal_src_type_t in_type;
	__scal_out_type_t out_type;
	__scal_scan_mod_t in_scan;
	__scal_scan_mod_t out_scan;
	__disp_scaler_t *scaler;
	__u32 screen_index;

	scaler = &(gdisp.scaler[sel]);
	screen_index = gdisp.scaler[sel].screen_index;

	memcpy(&scaler->in_fb, pfb, sizeof(__disp_fb_t));

	in_type.fmt = Scaler_sw_para_to_reg(0, scaler->in_fb.format);
	in_type.mod = Scaler_sw_para_to_reg(1, scaler->in_fb.mode);
	in_type.ps = Scaler_sw_para_to_reg(2, (__u8) scaler->in_fb.seq);
	in_type.byte_seq = 0;
	in_type.sample_method = 0;

	scal_addr.ch0_addr = scaler->in_fb.addr[0];
	scal_addr.ch1_addr = scaler->in_fb.addr[1];
	scal_addr.ch2_addr = scaler->in_fb.addr[2];

	in_size.src_width = scaler->in_fb.size.width;
	in_size.src_height = scaler->in_fb.size.height;
	in_size.x_off = scaler->src_win.x;
	in_size.y_off = scaler->src_win.y;
	in_size.scal_width = scaler->src_win.width;
	in_size.scal_height = scaler->src_win.height;

	out_type.byte_seq = scaler->out_fb.seq;
	out_type.fmt = scaler->out_fb.format;

	out_size.width = scaler->out_size.width;
	out_size.height = scaler->out_size.height;

	in_scan.field = FALSE;
	in_scan.bottom = FALSE;

#ifdef CONFIG_ARCH_SUN4I
	out_scan.field = (gdisp.screen[screen_index].de_flicker_status &
			  DE_FLICKER_USED) ?
		FALSE : gdisp.screen[screen_index].b_out_interlace;
#else
	out_scan.field = (gdisp.screen[screen_index].iep_status &
			  DE_FLICKER_USED) ?
		FALSE : gdisp.screen[screen_index].b_out_interlace;
#endif

	if (scaler->in_fb.cs_mode > DISP_VXYCC)
		scaler->in_fb.cs_mode = DISP_BT601;

	if (scaler->in_fb.b_trd_src) {
		__scal_3d_inmode_t inmode;
		__scal_3d_outmode_t outmode = 0;
		__scal_buf_addr_t scal_addr_right;

		inmode = Scaler_3d_sw_para_to_reg(0, scaler->in_fb.trd_mode, 0);
		outmode =
		    Scaler_3d_sw_para_to_reg(1, scaler->out_trd_mode,
					     gdisp.screen[screen_index].
					     b_out_interlace);

		DE_SCAL_Get_3D_In_Single_Size(inmode, &in_size, &in_size);
		if (scaler->b_trd_out) {
			DE_SCAL_Get_3D_Out_Single_Size(outmode, &out_size,
						       &out_size);
		}

		scal_addr_right.ch0_addr = scaler->in_fb.trd_right_addr[0];
		scal_addr_right.ch1_addr = scaler->in_fb.trd_right_addr[1];
		scal_addr_right.ch2_addr = scaler->in_fb.trd_right_addr[2];

		DE_SCAL_Set_3D_Ctrl(sel, scaler->b_trd_out, inmode, outmode);
		DE_SCAL_Config_3D_Src(sel, &scal_addr, &in_size, &in_type,
				      inmode, &scal_addr_right);
	} else
		DE_SCAL_Config_Src(sel, &scal_addr, &in_size, &in_type, FALSE,
				   FALSE);

	DE_SCAL_Set_Scaling_Factor(sel, &in_scan, &in_size, &in_type, &out_scan,
				   &out_size, &out_type);
	if (scaler->enhance_en == TRUE)
		Scaler_Set_Enhance(sel, scaler->bright, scaler->contrast,
				   scaler->saturation, scaler->hue);
	else
		DE_SCAL_Set_CSC_Coef(sel, scaler->in_fb.cs_mode, DISP_BT601,
				     get_fb_type(scaler->in_fb.format),
				     DISP_FB_TYPE_RGB, scaler->in_fb.br_swap,
				     0);

#ifdef CONFIG_ARCH_SUN4I
	DE_SCAL_Set_Scaling_Coef(sel, &in_scan, &in_size, &in_type, &out_scan,
				 &out_size, &out_type, scaler->smooth_mode);
#else
	gdisp.scaler[sel].coef_change = 1;
#endif

	return DIS_SUCCESS;
}

__s32 Scaler_Get_Framebuffer(__u32 sel, __disp_fb_t *pfb)
{
	__disp_scaler_t *scaler;

	if (pfb == NULL)
		return DIS_PARA_FAILED;

	scaler = &(gdisp.scaler[sel]);
	if (scaler->status & SCALER_USED)
		memcpy(pfb, &scaler->in_fb, sizeof(__disp_fb_t));
	else
		return DIS_PARA_FAILED;

	return DIS_SUCCESS;
}

__s32 Scaler_Set_Output_Size(__u32 sel, __disp_rectsz_t *size)
{
	__disp_scaler_t *scaler;
	__scal_src_size_t in_size;
	__scal_out_size_t out_size;
	__scal_src_type_t in_type;
	__scal_out_type_t out_type;
	__scal_scan_mod_t in_scan;
	__scal_scan_mod_t out_scan;
	__u32 screen_index;

	scaler = &(gdisp.scaler[sel]);
	screen_index = gdisp.scaler[sel].screen_index;

	scaler->out_size.height = size->height;
	scaler->out_size.width = size->width;

	in_type.mod = Scaler_sw_para_to_reg(1, scaler->in_fb.mode);
	in_type.fmt = Scaler_sw_para_to_reg(0, scaler->in_fb.format);
	in_type.ps = Scaler_sw_para_to_reg(2, (__u8) scaler->in_fb.seq);
	in_type.byte_seq = 0;
	in_type.sample_method = 0;

	in_size.src_width = scaler->src_win.width;
	in_size.src_height = scaler->in_fb.size.height;
	in_size.x_off = scaler->src_win.x;
	in_size.y_off = scaler->src_win.y;
	in_size.scal_height = scaler->src_win.height;
	in_size.scal_width = scaler->src_win.width;

	out_type.byte_seq = scaler->out_fb.seq;
	out_type.fmt = scaler->out_fb.format;

	out_size.width = scaler->out_size.width;
	out_size.height = scaler->out_size.height;

	in_scan.field = FALSE;
	in_scan.bottom = FALSE;

#ifdef CONFIG_ARCH_SUN4I
	out_scan.field = (gdisp.screen[screen_index].de_flicker_status &
			  DE_FLICKER_USED) ?
		FALSE : gdisp.screen[screen_index].b_out_interlace;
#else
	out_scan.field = (gdisp.screen[screen_index].iep_status ==
			  DE_FLICKER_USED) ?
		FALSE : gdisp.screen[screen_index].b_out_interlace;
#endif

	DE_SCAL_Set_Scaling_Factor(sel, &in_scan, &in_size, &in_type, &out_scan,
				   &out_size, &out_type);
	if (scaler->enhance_en == TRUE)
		Scaler_Set_Enhance(sel, scaler->bright, scaler->contrast,
				   scaler->saturation, scaler->hue);
	else
		DE_SCAL_Set_CSC_Coef(sel, scaler->in_fb.cs_mode, DISP_BT601,
				     get_fb_type(scaler->in_fb.format),
				     DISP_FB_TYPE_RGB, scaler->in_fb.br_swap,
				     0);

#ifdef CONFIG_ARCH_SUN4I
	DE_SCAL_Set_Scaling_Coef(sel, &in_scan, &in_size, &in_type, &out_scan,
				 &out_size, &out_type, scaler->smooth_mode);
#else
	gdisp.scaler[sel].coef_change = 1;
#endif

	DE_SCAL_Set_Out_Size(sel, &out_scan, &out_type, &out_size);

	return DIS_SUCCESS;
}

__s32 Scaler_Set_SclRegn(__u32 sel, __disp_rect_t *scl_rect)
{
	__disp_scaler_t *scaler;
	__scal_buf_addr_t scal_addr;
	__scal_src_size_t in_size;
	__scal_out_size_t out_size;
	__scal_src_type_t in_type;
	__scal_out_type_t out_type;
	__scal_scan_mod_t in_scan;
	__scal_scan_mod_t out_scan;
	__u32 screen_index;

	scaler = &(gdisp.scaler[sel]);
	screen_index = gdisp.scaler[sel].screen_index;

	scaler->src_win.x = scl_rect->x;
	scaler->src_win.y = scl_rect->y;
	scaler->src_win.height = scl_rect->height;
	scaler->src_win.width = scl_rect->width;

	in_type.mod = Scaler_sw_para_to_reg(1, scaler->in_fb.mode);
	in_type.fmt = Scaler_sw_para_to_reg(0, scaler->in_fb.format);
	in_type.ps = Scaler_sw_para_to_reg(2, (__u8) scaler->in_fb.seq);
	in_type.byte_seq = 0;
	in_type.sample_method = 0;

	scal_addr.ch0_addr = scaler->in_fb.addr[0];
	scal_addr.ch1_addr = scaler->in_fb.addr[1];
	scal_addr.ch2_addr = scaler->in_fb.addr[2];

	in_size.src_width = scaler->in_fb.size.width;
	in_size.src_height = scaler->in_fb.size.height;
	in_size.x_off = scaler->src_win.x;
	in_size.y_off = scaler->src_win.y;
	in_size.scal_width = scaler->src_win.width;
	in_size.scal_height = scaler->src_win.height;

	out_type.byte_seq = scaler->out_fb.seq;
	out_type.fmt = scaler->out_fb.format;

	out_size.width = scaler->out_size.width;
	out_size.height = scaler->out_size.height;

	in_scan.field = FALSE;
	in_scan.bottom = FALSE;

#ifdef CONFIG_ARCH_SUN4I
	out_scan.field = (gdisp.screen[screen_index].de_flicker_status &
			  DE_FLICKER_USED) ?
		FALSE : gdisp.screen[screen_index].b_out_interlace;
#else
	out_scan.field = (gdisp.screen[screen_index].iep_status ==
			  DE_FLICKER_USED) ?
		FALSE : gdisp.screen[screen_index].b_out_interlace;
#endif

	if (scaler->in_fb.cs_mode > DISP_VXYCC)
		scaler->in_fb.cs_mode = DISP_BT601;

	if (scaler->in_fb.b_trd_src) {
		__scal_3d_inmode_t inmode;
		__scal_3d_outmode_t outmode = 0;
		__scal_buf_addr_t scal_addr_right;

		inmode = Scaler_3d_sw_para_to_reg(0, scaler->in_fb.trd_mode, 0);
		outmode = Scaler_3d_sw_para_to_reg(1, scaler->out_trd_mode,
						   gdisp.screen[screen_index].
						   b_out_interlace);

		DE_SCAL_Get_3D_In_Single_Size(inmode, &in_size, &in_size);
		if (scaler->b_trd_out) {
			DE_SCAL_Get_3D_Out_Single_Size(outmode, &out_size,
						       &out_size);
		}

		scal_addr_right.ch0_addr = scaler->in_fb.trd_right_addr[0];
		scal_addr_right.ch1_addr = scaler->in_fb.trd_right_addr[1];
		scal_addr_right.ch2_addr = scaler->in_fb.trd_right_addr[2];

		DE_SCAL_Set_3D_Ctrl(sel, scaler->b_trd_out, inmode, outmode);
		DE_SCAL_Config_3D_Src(sel, &scal_addr, &in_size, &in_type,
				      inmode, &scal_addr_right);
	} else {
		DE_SCAL_Config_Src(sel, &scal_addr, &in_size, &in_type, FALSE,
				   FALSE);
	}
	DE_SCAL_Set_Scaling_Factor(sel, &in_scan, &in_size, &in_type, &out_scan,
				   &out_size, &out_type);

#ifdef CONFIG_ARCH_SUN4I
	DE_SCAL_Set_Scaling_Coef(sel, &in_scan, &in_size, &in_type, &out_scan,
				 &out_size, &out_type, scaler->smooth_mode);
#else
	gdisp.scaler[sel].coef_change = 1;
#endif

	return DIS_SUCCESS;
}

__s32 Scaler_Get_SclRegn(__u32 sel, __disp_rect_t *scl_rect)
{
	__disp_scaler_t *scaler;

	if (scl_rect == NULL)
		return DIS_PARA_FAILED;

	scaler = &(gdisp.scaler[sel]);
	if (scaler->status & SCALER_USED) {
		scl_rect->x = scaler->src_win.x;
		scl_rect->y = scaler->src_win.y;
		scl_rect->width = scaler->src_win.width;
		scl_rect->height = scaler->src_win.height;
	} else
		return DIS_PARA_FAILED;

	return DIS_SUCCESS;
}

__s32 Scaler_Set_Para(__u32 sel, __disp_scaler_t *scl)
{
	__disp_scaler_t *scaler;
	__scal_buf_addr_t scal_addr;
	__scal_src_size_t in_size;
	__scal_out_size_t out_size;
	__scal_src_type_t in_type;
	__scal_out_type_t out_type;
	__scal_scan_mod_t in_scan;
	__scal_scan_mod_t out_scan;
	__u32 screen_index;

	scaler = &(gdisp.scaler[sel]);
	screen_index = gdisp.scaler[sel].screen_index;

	memcpy(&(scaler->in_fb), &(scl->in_fb), sizeof(__disp_fb_t));
	memcpy(&(scaler->src_win), &(scl->src_win), sizeof(__disp_rect_t));
	memcpy(&(scaler->out_size), &(scl->out_size), sizeof(__disp_rectsz_t));

	in_type.mod = Scaler_sw_para_to_reg(1, scaler->in_fb.mode);
	in_type.fmt = Scaler_sw_para_to_reg(0, scaler->in_fb.format);
	in_type.ps = Scaler_sw_para_to_reg(2, (__u8) scaler->in_fb.seq);
	in_type.byte_seq = 0;
	in_type.sample_method = 0;

	scal_addr.ch0_addr = scaler->in_fb.addr[0];
	scal_addr.ch1_addr = scaler->in_fb.addr[1];
	scal_addr.ch2_addr = scaler->in_fb.addr[2];

	in_size.src_width = scaler->in_fb.size.width;
	in_size.src_height = scaler->in_fb.size.height;
	in_size.x_off = scaler->src_win.x;
	in_size.y_off = scaler->src_win.y;
	in_size.scal_height = scaler->src_win.height;
	in_size.scal_width = scaler->src_win.width;

	out_type.byte_seq = scaler->out_fb.seq;
	out_type.fmt = scaler->out_fb.format;

	out_size.width = scaler->out_size.width;
	out_size.height = scaler->out_size.height;

	in_scan.field = FALSE;
	in_scan.bottom = FALSE;

#ifdef CONFIG_ARCH_SUN4I
	out_scan.field = (gdisp.screen[screen_index].de_flicker_status &
			  DE_FLICKER_USED) ?
		FALSE : gdisp.screen[screen_index].b_out_interlace;
#else
	out_scan.field = (gdisp.screen[screen_index].iep_status &
			  DE_FLICKER_USED) ?
		FALSE : gdisp.screen[screen_index].b_out_interlace;
#endif

	if (scaler->in_fb.cs_mode > DISP_VXYCC)
		scaler->in_fb.cs_mode = DISP_BT601;

	if (scaler->in_fb.b_trd_src) {
		__scal_3d_inmode_t inmode;
		__scal_3d_outmode_t outmode = 0;
		__scal_buf_addr_t scal_addr_right;

		inmode = Scaler_3d_sw_para_to_reg(0, scaler->in_fb.trd_mode, 0);
		outmode = Scaler_3d_sw_para_to_reg(1, scaler->out_trd_mode,
						   gdisp.screen[screen_index].
						   b_out_interlace);

		DE_SCAL_Get_3D_In_Single_Size(inmode, &in_size, &in_size);
		if (scaler->b_trd_out) {
			DE_SCAL_Get_3D_Out_Single_Size(outmode, &out_size,
						       &out_size);
		}

		scal_addr_right.ch0_addr = scaler->in_fb.trd_right_addr[0];
		scal_addr_right.ch1_addr = scaler->in_fb.trd_right_addr[1];
		scal_addr_right.ch2_addr = scaler->in_fb.trd_right_addr[2];

		DE_SCAL_Set_3D_Ctrl(sel, scaler->b_trd_out, inmode, outmode);
		DE_SCAL_Config_3D_Src(sel, &scal_addr, &in_size, &in_type,
				      inmode, &scal_addr_right);
	} else {
		DE_SCAL_Config_Src(sel, &scal_addr, &in_size, &in_type, FALSE,
				   FALSE);
	}
	DE_SCAL_Set_Scaling_Factor(sel, &in_scan, &in_size, &in_type, &out_scan,
				   &out_size, &out_type);
	DE_SCAL_Set_Init_Phase(sel, &in_scan, &in_size, &in_type, &out_scan,
			       &out_size, &out_type, FALSE);
	if (scaler->enhance_en == TRUE) {
		Scaler_Set_Enhance(sel, scaler->bright, scaler->contrast,
				   scaler->saturation, scaler->hue);
	} else {
		DE_SCAL_Set_CSC_Coef(sel, scaler->in_fb.cs_mode, DISP_BT601,
				     get_fb_type(scaler->in_fb.format),
				     DISP_FB_TYPE_RGB, scaler->in_fb.br_swap,
				     0);
	}

#ifdef CONFIG_ARCH_SUN4I
	DE_SCAL_Set_Scaling_Coef(sel, &in_scan, &in_size, &in_type, &out_scan,
				 &out_size, &out_type, scaler->smooth_mode);
#else
	gdisp.scaler[sel].coef_change = 1;
#endif

	DE_SCAL_Set_Out_Format(sel, &out_type);
	DE_SCAL_Set_Out_Size(sel, &out_scan, &out_type, &out_size);

	return DIS_NULL;
}

__s32 Scaler_Set_Outitl(__u32 sel, __bool enable)
{
	__scal_src_size_t in_size;
	__scal_out_size_t out_size;
	__scal_src_type_t in_type;
	__scal_out_type_t out_type;
	__scal_scan_mod_t in_scan;
	__scal_scan_mod_t out_scan;
	__disp_scaler_t *scaler;

	scaler = &(gdisp.scaler[sel]);

	in_type.fmt = Scaler_sw_para_to_reg(0, scaler->in_fb.format);
	in_type.mod = Scaler_sw_para_to_reg(1, scaler->in_fb.mode);
	in_type.ps = Scaler_sw_para_to_reg(2, scaler->in_fb.seq);
	in_type.byte_seq = 0;
	in_type.sample_method = 0;

	in_size.src_width = scaler->in_fb.size.width;
	in_size.src_height = scaler->in_fb.size.height;
	in_size.x_off = scaler->src_win.x;
	in_size.y_off = scaler->src_win.y;
	in_size.scal_height = scaler->src_win.height;
	in_size.scal_width = scaler->src_win.width;

	out_type.byte_seq = scaler->out_fb.seq;
	out_type.fmt = scaler->out_fb.format;

	out_size.width = scaler->out_size.width;
	out_size.height = scaler->out_size.height;

	in_scan.field = FALSE;
	in_scan.bottom = FALSE;

	out_scan.field = enable;

	DE_SCAL_Set_Init_Phase(sel, &in_scan, &in_size, &in_type, &out_scan,
			       &out_size, &out_type, FALSE);
	DE_SCAL_Set_Scaling_Factor(sel, &in_scan, &in_size, &in_type, &out_scan,
				   &out_size, &out_type);

#ifdef CONFIG_ARCH_SUN4I
	DE_SCAL_Set_Scaling_Coef(sel, &in_scan, &in_size, &in_type, &out_scan,
				 &out_size, &out_type, scaler->smooth_mode);
#else
	gdisp.scaler[sel].coef_change = 1;
#endif

	DE_SCAL_Set_Out_Size(sel, &out_scan, &out_type, &out_size);

	return DIS_SUCCESS;
}

__s32 BSP_disp_scaler_set_smooth(__u32 sel, __disp_video_smooth_t mode)
{
	__disp_scaler_t *scaler;
	__scal_src_size_t in_size;
	__scal_out_size_t out_size;
	__scal_src_type_t in_type;
	__scal_out_type_t out_type;
	__scal_scan_mod_t in_scan;
	__scal_scan_mod_t out_scan;
	__u32 screen_index;

	scaler = &(gdisp.scaler[sel]);
	screen_index = gdisp.scaler[sel].screen_index;
	scaler->smooth_mode = mode;

	in_type.mod = Scaler_sw_para_to_reg(1, scaler->in_fb.mode);
	in_type.fmt = Scaler_sw_para_to_reg(0, scaler->in_fb.format);
	in_type.ps = Scaler_sw_para_to_reg(2, (__u8) scaler->in_fb.seq);
	in_type.byte_seq = 0;
	in_type.sample_method = 0;

	in_size.src_width = scaler->in_fb.size.width;
	in_size.src_height = scaler->in_fb.size.height;
	in_size.x_off = scaler->src_win.x;
	in_size.y_off = scaler->src_win.y;
	in_size.scal_height = scaler->src_win.height;
	in_size.scal_width = scaler->src_win.width;

	out_type.byte_seq = scaler->out_fb.seq;
	out_type.fmt = scaler->out_fb.format;

	out_size.width = scaler->out_size.width;
	out_size.height = scaler->out_size.height;

	in_scan.field = FALSE;
	in_scan.bottom = FALSE;

#ifdef CONFIG_ARCH_SUN4I
	out_scan.field = (gdisp.screen[screen_index].de_flicker_status &
			  DE_FLICKER_USED) ?
		FALSE : gdisp.screen[screen_index].b_out_interlace;
#else
	out_scan.field = (gdisp.screen[screen_index].iep_status ==
			  DE_FLICKER_USED) ?
		FALSE : gdisp.screen[screen_index].b_out_interlace;
#endif

#ifdef CONFIG_ARCH_SUN4I
	DE_SCAL_Set_Scaling_Coef(sel, &in_scan, &in_size, &in_type, &out_scan,
				 &out_size, &out_type, scaler->smooth_mode);
#else
	gdisp.scaler[sel].coef_change = 1;
#endif

	scaler->b_reg_change = TRUE;

	return DIS_SUCCESS;
}

__s32 BSP_disp_scaler_get_smooth(__u32 sel)
{
	return gdisp.scaler[sel].smooth_mode;
}

__s32 BSP_disp_scaler_request(void)
{
	__s32 sel = 0;
	sel = Scaler_Request(0xff);
	if (sel < 0)
		return sel;
	else
		gdisp.scaler[sel].screen_index = 0xff;
	return SCALER_IDTOHAND(sel);
}

__s32 BSP_disp_scaler_release(__u32 handle)
{
	__u32 sel = 0;

	sel = SCALER_HANDTOID(handle);
	return Scaler_Release(sel, FALSE);
}

__s32 BSP_disp_scaler_start(__u32 handle, __disp_scaler_para_t *para)
{
	__scal_buf_addr_t in_addr;
	__scal_buf_addr_t out_addr;
	__scal_src_size_t in_size;
	__scal_out_size_t out_size;
	__scal_src_type_t in_type;
	__scal_out_type_t out_type;
	__scal_scan_mod_t in_scan;
	__scal_scan_mod_t out_scan;
	__u32 size = 0;
	__u32 sel = 0;
	__s32 ret = 0;
#ifdef CONFIG_ARCH_SUN5I
	__u32 i = 0;
	__u32 ch_num = 0;
#endif

	if (para == NULL) {
		DE_WRN("input parameter can't be null!\n");
		return DIS_FAIL;
	}

	sel = SCALER_HANDTOID(handle);

	in_type.mod = Scaler_sw_para_to_reg(1, para->input_fb.mode);
	in_type.fmt = Scaler_sw_para_to_reg(0, para->input_fb.format);
	in_type.ps = Scaler_sw_para_to_reg(2, (__u8) para->input_fb.seq);
	in_type.byte_seq = 0;
	in_type.sample_method = 0;

	if (get_fb_type(para->output_fb.format) == DISP_FB_TYPE_YUV) {
		if (para->output_fb.mode == DISP_MOD_NON_MB_PLANAR) {
			out_type.fmt =
			    Scaler_sw_para_to_reg(3, para->output_fb.format);
		} else {
			DE_WRN("output mode:%d invalid in "
			       "Display_Scaler_Start\n", para->output_fb.mode);
			return DIS_FAIL;
		}
	} else {
		if (para->output_fb.mode == DISP_MOD_NON_MB_PLANAR &&
		    (para->output_fb.format == DISP_FORMAT_RGB888 ||
		     para->output_fb.format == DISP_FORMAT_ARGB8888)) {
			out_type.fmt = DE_SCAL_OUTPRGB888;
		} else if (para->output_fb.mode == DISP_MOD_INTERLEAVED &&
			   para->output_fb.format == DISP_FORMAT_ARGB8888) {
			out_type.fmt = DE_SCAL_OUTI0RGB888;
		} else {
			DE_WRN("output para invalid in Display_Scaler_Start,"
			       "mode:%d,format:%d\n", para->output_fb.mode,
			       para->output_fb.format);
			return DIS_FAIL;
		}
	}
	out_type.byte_seq = Scaler_sw_para_to_reg(2, para->output_fb.seq);

	out_size.width = para->output_fb.size.width;
	out_size.height = para->output_fb.size.height;

	in_addr.ch0_addr = para->input_fb.addr[0];
	in_addr.ch1_addr = para->input_fb.addr[1];
	in_addr.ch2_addr = para->input_fb.addr[2];

	in_size.src_width = para->input_fb.size.width;
	in_size.src_height = para->input_fb.size.height;
	in_size.x_off = para->source_regn.x;
	in_size.y_off = para->source_regn.y;
	in_size.scal_width = para->source_regn.width;
	in_size.scal_height = para->source_regn.height;

	in_scan.field = FALSE;
	in_scan.bottom = FALSE;

	/*
	 * when use scaler as writeback, won't be outinterlaced for any
	 * display device
	 */
	out_scan.field = FALSE;
	out_scan.bottom = FALSE;

	out_addr.ch0_addr = para->output_fb.addr[0];
	out_addr.ch1_addr = para->output_fb.addr[1];
	out_addr.ch2_addr = para->output_fb.addr[2];

	size = (para->input_fb.size.width * para->input_fb.size.height *
		de_format_to_bpp(para->input_fb.format) + 7) / 8;

	size = (para->output_fb.size.width * para->output_fb.size.height *
		de_format_to_bpp(para->output_fb.format) + 7) / 8;

	if (para->input_fb.b_trd_src) {
		__scal_3d_inmode_t inmode;
		__scal_3d_outmode_t outmode = 0;
		__scal_buf_addr_t scal_addr_right;

		inmode = Scaler_3d_sw_para_to_reg(0, para->input_fb.trd_mode,
						  FALSE);
		outmode = Scaler_3d_sw_para_to_reg(1, para->output_fb.trd_mode,
						   FALSE);

		DE_SCAL_Get_3D_In_Single_Size(inmode, &in_size, &in_size);
		if (para->output_fb.b_trd_src) {
			DE_SCAL_Get_3D_Out_Single_Size(outmode, &out_size,
						       &out_size);
		}

		scal_addr_right.ch0_addr = para->input_fb.trd_right_addr[0];
		scal_addr_right.ch1_addr = para->input_fb.trd_right_addr[1];
		scal_addr_right.ch2_addr = para->input_fb.trd_right_addr[2];

		DE_SCAL_Set_3D_Ctrl(sel, para->output_fb.b_trd_src, inmode,
				    outmode);
		DE_SCAL_Config_3D_Src(sel, &in_addr, &in_size, &in_type, inmode,
				      &scal_addr_right);
	} else {
		DE_SCAL_Config_Src(sel, &in_addr, &in_size, &in_type, FALSE,
				   FALSE);
	}
	DE_SCAL_Set_Scaling_Factor(sel, &in_scan, &in_size, &in_type, &out_scan,
				   &out_size, &out_type);
	DE_SCAL_Set_Init_Phase(sel, &in_scan, &in_size, &in_type, &out_scan,
			       &out_size, &out_type, FALSE);
	DE_SCAL_Set_CSC_Coef(sel, para->input_fb.cs_mode,
			     para->output_fb.cs_mode,
			     get_fb_type(para->input_fb.format),
			     get_fb_type(para->output_fb.format),
			     para->input_fb.br_swap, para->output_fb.br_swap);
	DE_SCAL_Set_Scaling_Coef(sel, &in_scan, &in_size, &in_type, &out_scan,
				 &out_size, &out_type, DISP_VIDEO_NATUAL);
	DE_SCAL_Set_Out_Format(sel, &out_type);
	DE_SCAL_Set_Out_Size(sel, &out_scan, &out_type, &out_size);

#ifdef CONFIG_ARCH_SUN4I
	DE_SCAL_Set_Writeback_Addr(sel, &out_addr);

	DE_SCAL_Output_Select(sel, 3);
	DE_SCAL_EnableINT(sel, DE_WB_END_IE);
	DE_SCAL_Start(sel);
	DE_SCAL_Set_Reg_Rdy(sel);

	{
		long timeout = (100 * HZ) / 1000; /* 100ms */

		init_waitqueue_head(&(gdisp.scaler[sel].scaler_queue));
		gdisp.scaler[sel].b_scaler_finished = 1;
		DE_SCAL_Writeback_Enable(sel);

		timeout =
		    wait_event_interruptible_timeout(gdisp.scaler[sel].
						     scaler_queue,
						     gdisp.scaler[sel].
						     b_scaler_finished == 2,
						     timeout);
		gdisp.scaler[sel].b_scaler_finished = 0;
		if (timeout == 0) {
			__wrn("wait scaler %d finished timeout\n", sel);
			return -1;
		}
	}

	DE_SCAL_Reset(sel);
	DE_SCAL_Writeback_Disable(sel);
#else
	if (para->output_fb.mode == DISP_MOD_INTERLEAVED)
		ch_num = 1;
	else if (para->output_fb.mode == DISP_MOD_MB_UV_COMBINED ||
		 para->output_fb.mode == DISP_MOD_NON_MB_UV_COMBINED)
		ch_num = 2;
	else if (para->output_fb.mode == DISP_MOD_MB_PLANAR ||
		 para->output_fb.mode == DISP_MOD_NON_MB_PLANAR)
		ch_num = 3;

	for (i = 0; i < ch_num; i++) {
		__scal_buf_addr_t addr;
		ret = 0;

		addr.ch0_addr = out_addr.ch0_addr;
		if (i == 1)
			addr.ch0_addr = out_addr.ch1_addr;
		else if (i == 2)
			addr.ch0_addr = out_addr.ch2_addr;
		DE_SCAL_Enable(sel);

		DE_SCAL_Set_Writeback_Addr(sel, &addr);
		DE_SCAL_Set_Writeback_Chnl(sel, i);

		DE_SCAL_Output_Select(sel, 3);
		DE_SCAL_EnableINT(sel, DE_WB_END_IE);
		DE_SCAL_Start(sel);
		DE_SCAL_Set_Reg_Rdy(sel);

		{
			long timeout = (100 * HZ) / 1000; /* 100ms */

			init_waitqueue_head(&(gdisp.scaler[sel].scaler_queue));
			gdisp.scaler[sel].b_scaler_finished = 1;
			DE_SCAL_Writeback_Enable(sel);

			timeout =
			    wait_event_interruptible_timeout(gdisp.scaler[sel].
							     scaler_queue,
							     gdisp.scaler[sel].
							     b_scaler_finished
							     == 2, timeout);
			gdisp.scaler[sel].b_scaler_finished = 0;

			if (timeout == 0) {
				__wrn("wait scaler %d finished timeout\n", sel);
				DE_SCAL_Writeback_Disable(sel);
				DE_SCAL_Reset(sel);
				DE_SCAL_Disable(sel);
				return -1;
			}
		}

		DE_SCAL_Writeback_Disable(sel);
		DE_SCAL_Reset(sel);
		DE_SCAL_Disable(sel);
	}
#endif /* CONFIG_ARCH_SUN4I */

	return ret;
}

__s32 BSP_disp_capture_screen(__u32 sel, __disp_capture_screen_para_t *para)
{
	__scal_buf_addr_t in_addr;
	__scal_buf_addr_t out_addr;
	__scal_src_size_t in_size;
	__scal_out_size_t out_size;
	__scal_src_type_t in_type;
	__scal_out_type_t out_type;
	__scal_scan_mod_t in_scan;
	__scal_scan_mod_t out_scan;
	__u32 size = 0;
	__s32 scaler_idx = 0;
	__s32 ret = 0;

	if (para == NULL) {
		DE_WRN("input parameter can't be null!\n");
		return DIS_FAIL;
	}

	scaler_idx = Scaler_Request(0xff);
	if (scaler_idx < 0) {
		DE_WRN("request scaler fail in BSP_disp_capture_screen\n");
		return DIS_FAIL;
	} else {
		gdisp.scaler[sel].screen_index = 0xff;
	}

	in_type.mod = Scaler_sw_para_to_reg(1, DISP_MOD_INTERLEAVED);
	in_type.fmt = Scaler_sw_para_to_reg(0, DISP_FORMAT_ARGB8888);
	in_type.ps = Scaler_sw_para_to_reg(2, DISP_SEQ_ARGB);
	in_type.byte_seq = 0;
	in_type.sample_method = 0;

	if (get_fb_type(para->output_fb.format) == DISP_FB_TYPE_YUV) {
		if (para->output_fb.mode == DISP_MOD_NON_MB_PLANAR) {
			out_type.fmt =
			    Scaler_sw_para_to_reg(3, para->output_fb.format);
		} else {
			DE_WRN("output mode:%d invalid in "
			       "Display_Scaler_Start\n", para->output_fb.mode);
			return DIS_FAIL;
		}
	} else {
		if (para->output_fb.mode == DISP_MOD_NON_MB_PLANAR &&
		    (para->output_fb.format == DISP_FORMAT_RGB888 ||
		     para->output_fb.format == DISP_FORMAT_ARGB8888)) {
			out_type.fmt = DE_SCAL_OUTPRGB888;
		} else if (para->output_fb.mode == DISP_MOD_INTERLEAVED &&
			   para->output_fb.format == DISP_FORMAT_ARGB8888) {
			out_type.fmt = DE_SCAL_OUTI0RGB888;
		} else {
			DE_WRN("output para invalid in Display_Scaler_Start, "
			       "mode:%d,format:%d\n", para->output_fb.mode,
			       para->output_fb.format);
			return DIS_FAIL;
		}
		para->output_fb.br_swap = FALSE;
	}
	out_type.byte_seq = Scaler_sw_para_to_reg(2, para->output_fb.seq);

	out_size.width = para->output_fb.size.width;
	out_size.height = para->output_fb.size.height;

	if (BSP_disp_get_output_type(sel) != DISP_OUTPUT_TYPE_NONE) {
		in_size.src_width = BSP_disp_get_screen_width(sel);
		in_size.src_height = BSP_disp_get_screen_height(sel);
		in_size.x_off = 0;
		in_size.y_off = 0;
		in_size.scal_width = BSP_disp_get_screen_width(sel);
		in_size.scal_height = BSP_disp_get_screen_height(sel);
	} else {
		in_size.src_width = para->screen_size.width;
		in_size.src_height = para->screen_size.height;
		in_size.x_off = 0;
		in_size.y_off = 0;
		in_size.scal_width = para->screen_size.width;
		in_size.scal_height = para->screen_size.height;
	}

	in_scan.field = FALSE;
	in_scan.bottom = FALSE;

	/*
	 * when use scaler as writeback, won't be outinterlaced for any
	 * display device
	 */
	out_scan.field = FALSE;
	out_scan.bottom = FALSE;

	in_addr.ch0_addr = 0;
	in_addr.ch1_addr = 0;
	in_addr.ch2_addr = 0;

	out_addr.ch0_addr = para->output_fb.addr[0];
	out_addr.ch1_addr = para->output_fb.addr[1];
	out_addr.ch2_addr = para->output_fb.addr[2];

	size = (para->output_fb.size.width * para->output_fb.size.height *
		de_format_to_bpp(para->output_fb.format) + 7) / 8;

	if (BSP_disp_get_output_type(sel) == DISP_OUTPUT_TYPE_NONE) {
		DE_SCAL_Input_Select(scaler_idx, 6 + sel);
		DE_BE_set_display_size(sel, para->screen_size.width,
				       para->screen_size.height);
		DE_BE_Output_Select(sel, 6 + scaler_idx);
		image_clk_on(sel);
		Image_open(sel);
		DE_BE_Cfg_Ready(sel);
	} else {
		DE_SCAL_Input_Select(scaler_idx, 4 + sel);
		DE_BE_Output_Select(sel, 2 + (scaler_idx * 2) + sel);
	}
	DE_SCAL_Config_Src(scaler_idx, &in_addr, &in_size, &in_type, FALSE,
			   FALSE);
	DE_SCAL_Set_Scaling_Factor(scaler_idx, &in_scan, &in_size, &in_type,
				   &out_scan, &out_size, &out_type);
	DE_SCAL_Set_Init_Phase(scaler_idx, &in_scan, &in_size, &in_type,
			       &out_scan, &out_size, &out_type, FALSE);
	DE_SCAL_Set_CSC_Coef(scaler_idx, DISP_BT601, para->output_fb.cs_mode,
			     DISP_FB_TYPE_RGB,
			     get_fb_type(para->output_fb.format), 0, 0);
	DE_SCAL_Set_Scaling_Coef(scaler_idx, &in_scan, &in_size, &in_type,
				 &out_scan, &out_size, &out_type,
				 DISP_VIDEO_NATUAL);
	DE_SCAL_Set_Out_Format(scaler_idx, &out_type);
	DE_SCAL_Set_Out_Size(scaler_idx, &out_scan, &out_type, &out_size);
	DE_SCAL_Set_Writeback_Addr(scaler_idx, &out_addr);
	DE_SCAL_Output_Select(scaler_idx, 3);
	DE_SCAL_ClearINT(scaler_idx, DE_WB_END_IE);
	DE_SCAL_EnableINT(scaler_idx, DE_WB_END_IE);
	DE_SCAL_Set_Reg_Rdy(scaler_idx);
	DE_SCAL_Writeback_Enable(scaler_idx);
	DE_SCAL_Start(scaler_idx);

	DE_INF("capture begin\n");
	{
		long timeout = (100 * HZ) / 1000; /* 100ms */

		init_waitqueue_head(&(gdisp.scaler[scaler_idx].scaler_queue));
		gdisp.scaler[scaler_idx].b_scaler_finished = 1;
		DE_SCAL_Writeback_Enable(scaler_idx);

		timeout =
		    wait_event_interruptible_timeout(gdisp.scaler[scaler_idx].
						     scaler_queue,
						     gdisp.scaler[scaler_idx].
						     b_scaler_finished == 2,
						     timeout);
		gdisp.scaler[scaler_idx].b_scaler_finished = 0;
		if (timeout == 0) {
			__wrn("wait scaler %d finished timeout\n", scaler_idx);
			return -1;
		}
	}

	DE_SCAL_Reset(scaler_idx);
	Scaler_Release(scaler_idx, FALSE);
	if (BSP_disp_get_output_type(sel) == DISP_OUTPUT_TYPE_NONE) {
		Image_close(sel);
		image_clk_off(sel);
	}
	DE_BE_Output_Select(sel, sel);

	return ret;
}

__s32 Scaler_Set_Enhance(__u32 sel, __u32 bright, __u32 contrast,
			 __u32 saturation, __u32 hue)
{
	__u32 b_yuv_in, b_yuv_out;
	__disp_scaler_t *scaler;

	scaler = &(gdisp.scaler[sel]);

	b_yuv_in = (get_fb_type(scaler->in_fb.format) == DISP_FB_TYPE_YUV) ?
		1 : 0;
	b_yuv_out = (get_fb_type(scaler->out_fb.format) == DISP_FB_TYPE_YUV) ?
		1 : 0;
	DE_SCAL_Set_CSC_Coef_Enhance(sel, scaler->in_fb.cs_mode,
				     scaler->out_fb.cs_mode, b_yuv_in,
				     b_yuv_out, bright, contrast, saturation,
				     hue, scaler->in_fb.br_swap, 0);
	scaler->b_reg_change = TRUE;

	return DIS_SUCCESS;
}
