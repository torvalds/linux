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

#include "disp_event.h"
#include "disp_display.h"
#include "disp_de.h"
#include "disp_video.h"
#include "disp_scaler.h"

__s32 BSP_disp_cmd_cache(__u32 sel)
{
	gdisp.screen[sel].cache_flag = TRUE;
	return DIS_SUCCESS;
}

__s32 BSP_disp_cmd_submit(__u32 sel)
{
	gdisp.screen[sel].cache_flag = FALSE;

	return DIS_SUCCESS;
}

__s32 BSP_disp_cfg_start(__u32 sel)
{
	gdisp.screen[sel].cfg_cnt++;

	return DIS_SUCCESS;
}

__s32 BSP_disp_cfg_finish(__u32 sel)
{
	gdisp.screen[sel].cfg_cnt--;

	return DIS_SUCCESS;
}

void LCD_vbi_event_proc(__u32 sel, __u32 tcon_index)
{
	__u32 cur_line = 0, start_delay = 0;
	__u32 i = 0;

	Video_Operation_In_Vblanking(sel, tcon_index);

	cur_line = LCDC_get_cur_line(sel, tcon_index);
	start_delay = LCDC_get_start_delay(sel, tcon_index);
	if (cur_line > start_delay - 3) {
#if 0
		DE_INF("cur_line(%d) >= start_delay(%d)-3 in "
		       "LCD_vbi_event_proc\n", cur_line, start_delay);
#endif
		return;
	}
#ifdef CONFIG_ARCH_SUN5I
	IEP_Operation_In_Vblanking(sel, tcon_index);
#endif

	if (gdisp.screen[sel].LCD_CPUIF_ISR)
		(*gdisp.screen[sel].LCD_CPUIF_ISR) ();

	if (gdisp.screen[sel].cache_flag == FALSE &&
	    gdisp.screen[sel].cfg_cnt == 0) {
		for (i = 0; i < 2; i++) {
			if ((gdisp.scaler[i].status & SCALER_USED) &&
			    (gdisp.scaler[i].screen_index == sel)) {
				DE_SCAL_Set_Reg_Rdy(i);
#if 0
				DE_SCAL_Reset(i);
				DE_SCAL_Start(i);
#endif
				gdisp.scaler[i].b_reg_change = FALSE;
			}
			if (gdisp.scaler[i].b_close == TRUE) {
				Scaler_close(i);
				gdisp.scaler[i].b_close = FALSE;
			}
#ifdef CONFIG_ARCH_SUN5I
			if (gdisp.scaler[i].coef_change == TRUE) {
				__scal_src_size_t in_size;
				__scal_out_size_t out_size;
				__scal_src_type_t in_type;
				__scal_out_type_t out_type;
				__scal_scan_mod_t in_scan;
				__scal_scan_mod_t out_scan;
				__disp_scaler_t *scaler;

				scaler = &(gdisp.scaler[sel]);

				in_scan.field = FALSE;
				in_scan.bottom = FALSE;

				in_type.fmt =
				    Scaler_sw_para_to_reg(0,
							  scaler->in_fb.format);
				in_type.mod =
				    Scaler_sw_para_to_reg(1,
							  scaler->in_fb.mode);
				in_type.ps =
				    Scaler_sw_para_to_reg(2,
							  (__u8) scaler->in_fb.
							  seq);
				in_type.byte_seq = 0;
				in_type.sample_method = 0;

				in_size.src_width = scaler->in_fb.size.width;
				in_size.src_height = scaler->in_fb.size.height;
				in_size.x_off = scaler->src_win.x;
				in_size.y_off = scaler->src_win.y;
				in_size.scal_width = scaler->src_win.width;
				in_size.scal_height = scaler->src_win.height;

				out_scan.field = (gdisp.screen[sel].iep_status &
						  DE_FLICKER_USED) ?
					FALSE :
					gdisp.screen[sel].b_out_interlace;

				out_type.byte_seq = scaler->out_fb.seq;
				out_type.fmt = scaler->out_fb.format;

				out_size.width = scaler->out_size.width;
				out_size.height = scaler->out_size.height;

				DE_SCAL_Set_Scaling_Coef(sel, &in_scan,
							 &in_size, &in_type,
							 &out_scan, &out_size,
							 &out_type,
							 scaler->smooth_mode);

				gdisp.scaler[i].coef_change = FALSE;
			}
#endif

		}
		DE_BE_Cfg_Ready(sel);
		gdisp.screen[sel].have_cfg_reg = TRUE;
	}
#if 0
	cur_line = LCDC_get_cur_line(sel, tcon_index);

	if (cur_line > 5)
		DE_INF("%d\n", cur_line);
#endif

	return;
}

void LCD_line_event_proc(__u32 sel)
{
	if (gdisp.screen[sel].have_cfg_reg) {
		gdisp.init_para.disp_int_process(sel);
		gdisp.screen[sel].have_cfg_reg = FALSE;
	}
}
