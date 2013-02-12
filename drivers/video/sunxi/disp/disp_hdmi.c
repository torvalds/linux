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

#include "disp_display_i.h"
#include "disp_hdmi.h"
#include "disp_display.h"
#include "disp_event.h"
#include "disp_de.h"
#include "disp_tv.h"
#include "disp_lcd.h"
#include "disp_clk.h"

__s32 Display_Hdmi_Init(void)
{
	hdmi_clk_init();

	gdisp.screen[0].hdmi_mode = DISP_TV_MOD_720P_50HZ;
	gdisp.screen[1].hdmi_mode = DISP_TV_MOD_720P_50HZ;

	return DIS_SUCCESS;
}

__s32 Display_Hdmi_Exit(void)
{
	hdmi_clk_exit();

	return DIS_SUCCESS;
}

__s32 BSP_disp_hdmi_open(__u32 sel, __u32 wait_edid)
{
	if (!(gdisp.screen[sel].status & HDMI_ON)) {
		__disp_tv_mode_t tv_mod = gdisp.screen[sel].hdmi_mode;

		if (!gdisp.init_para.hdmi_wait_edid) {
			pr_err("hdmi funcs NULL, hdmi module not loaded?\n");
			return -1;
		}

		hdmi_clk_on();

		if (wait_edid && gdisp.init_para.hdmi_wait_edid() == 0) {
			tv_mod = DISP_TV_MODE_EDID;
			gdisp.init_para.hdmi_set_mode(tv_mod);
			gdisp.screen[sel].hdmi_mode = tv_mod;
		}

		lcdc_clk_on(sel);
		image_clk_on(sel);

		/*
		 * set image normal channel start bit, because every
		 * de_clk_off( ) will reset this bit
		 */
		Image_open(sel);
		disp_clk_cfg(sel, DISP_OUTPUT_TYPE_HDMI, tv_mod);

#ifdef CONFIG_ARCH_SUN4I
		BSP_disp_set_output_csc(sel, DISP_OUTPUT_TYPE_HDMI);
#else
		BSP_disp_set_output_csc(sel, DISP_OUTPUT_TYPE_HDMI,
					gdisp.screen[sel].
					iep_status & DRC_USED);
#endif
		DE_BE_set_display_size(sel, tv_mode_to_width(tv_mod),
				       tv_mode_to_height(tv_mod));
		DE_BE_Output_Select(sel, sel);

#ifdef CONFIG_ARCH_SUN5I
		DE_BE_Set_Outitl_enable(sel, Disp_get_screen_scan_mode(tv_mod));
		{
			int scaler_index;

			for (scaler_index = 0; scaler_index < 2; scaler_index++)
				if ((gdisp.scaler[scaler_index].status &
				     SCALER_USED) &&
				    (gdisp.scaler[scaler_index].screen_index ==
				     sel)) {
					/* interlace output */
					if (Disp_get_screen_scan_mode(tv_mod) ==
					    1)
						Scaler_Set_Outitl(scaler_index,
								  TRUE);
					else
						Scaler_Set_Outitl(scaler_index,
								  FALSE);
				}
		}
#endif /* CONFIG_ARCH_SUN5I */

		TCON1_set_hdmi_mode(sel, tv_mod);
		TCON1_open(sel);
		if (gdisp.init_para.Hdmi_open)
			gdisp.init_para.Hdmi_open();
		else {
			DE_WRN("Hdmi_open is NULL\n");
			return -1;
		}

		Disp_Switch_Dram_Mode(DISP_OUTPUT_TYPE_HDMI, tv_mod);

		gdisp.screen[sel].b_out_interlace =
			Disp_get_screen_scan_mode(tv_mod);
		gdisp.screen[sel].status |= HDMI_ON;
		gdisp.screen[sel].lcdc_status |= LCDC_TCON1_USED;
		gdisp.screen[sel].output_type = DISP_OUTPUT_TYPE_HDMI;

		Display_set_fb_timing(sel);
	}

	return DIS_SUCCESS;
}

__s32 BSP_disp_hdmi_close(__u32 sel)
{
	if (gdisp.screen[sel].status & HDMI_ON) {
		if (gdisp.init_para.Hdmi_close) {
			gdisp.init_para.Hdmi_close();
		} else {
			DE_WRN("Hdmi_close is NULL\n");
			return -1;
		}
		Image_close(sel);
		TCON1_close(sel);

		image_clk_off(sel);
		lcdc_clk_off(sel);
		hdmi_clk_off();

#ifdef CONFIG_ARCH_SUN5I
		DE_BE_Set_Outitl_enable(sel, FALSE);
		{
			int scaler_index;

			for (scaler_index = 0; scaler_index < 2; scaler_index++)
				if ((gdisp.scaler[scaler_index].status &
				     SCALER_USED) &&
				    (gdisp.scaler[scaler_index].screen_index ==
				     sel))
					Scaler_Set_Outitl(scaler_index, FALSE);
		}
#endif /* CONFIG_ARCH_SUN5I */

		gdisp.screen[sel].b_out_interlace = 0;
		gdisp.screen[sel].lcdc_status &= ~LCDC_TCON1_USED;
		gdisp.screen[sel].status &= ~HDMI_ON;
		gdisp.screen[sel].output_type = DISP_OUTPUT_TYPE_NONE;
		gdisp.screen[sel].pll_use_status &=
			(gdisp.screen[sel].pll_use_status == VIDEO_PLL0_USED) ?
			~VIDEO_PLL0_USED : ~VIDEO_PLL1_USED;
	}

	return DIS_SUCCESS;
}

__s32 BSP_disp_hdmi_set_mode(__u32 sel, __disp_tv_mode_t mode)
{
	if (mode >= DISP_TV_MODE_NUM) {
		DE_WRN("unsupported hdmi mode:%d in BSP_disp_hdmi_set_mode\n",
		       mode);
		return DIS_FAIL;
	}

	if (gdisp.init_para.hdmi_set_mode) {
		gdisp.init_para.hdmi_set_mode(mode);
	} else {
		DE_WRN("hdmi_set_mode is NULL\n");
		return -1;
	}

	gdisp.screen[sel].hdmi_mode = mode;
	gdisp.screen[sel].output_type = DISP_OUTPUT_TYPE_HDMI;

	return DIS_SUCCESS;
}

__u32 fb_videomode_pixclock_to_hdmi_pclk(__u32 pixclock)
{
	/*
	 * The pixelclock -> picoseconds -> pixelclock conversions we do
	 * lose precision, which *is* a problem.
	 * We can only do pixelclocks which are a divider of 1 - 15 of
	 * a multiple of 3 MHz. So all clocks must have been a multiple of
	 * 100 or 250 KHz before the conversion -> round to the nearest
	 * multiple of 50 KHz to undo the precision loss.
	 */
	__u32 pclk = (PICOS2HZ(pixclock) + 25000) / 50000;
	return pclk * 50000;
}

void videomode_to_video_timing(struct __disp_video_timing *video_timing,
		const struct fb_videomode *mode)
{
	memset(video_timing, 0, sizeof(struct __disp_video_timing));
	video_timing->VIC = 511;
	video_timing->PCLK =
		fb_videomode_pixclock_to_hdmi_pclk(mode->pixclock);
	video_timing->AVI_PR = 0;
	video_timing->INPUTX = mode->xres;
	video_timing->INPUTY = mode->yres;
	video_timing->HT = mode->xres + mode->left_margin +
			mode->right_margin + mode->hsync_len;
	video_timing->HBP = mode->left_margin + mode->hsync_len;
	video_timing->HFP = mode->right_margin;
	video_timing->HPSW =  mode->hsync_len;
	video_timing->VT = mode->yres + mode->upper_margin +
			mode->lower_margin + mode->vsync_len;
	video_timing->VBP = mode->upper_margin + mode->vsync_len;
	video_timing->VFP = mode->lower_margin;
	video_timing->VPSW = mode->vsync_len;
	if (mode->vmode & FB_VMODE_INTERLACED)
		video_timing->I = true;

	if (mode->sync & FB_SYNC_HOR_HIGH_ACT)
		video_timing->HSYNC = true;

	if (mode->sync & FB_SYNC_VERT_HIGH_ACT)
		video_timing->VSYNC = true;

}

__s32 BSP_disp_set_videomode(__u32 sel, const struct fb_videomode *mode)
{
	struct __disp_video_timing *old_video_timing =
			kzalloc(sizeof(struct __disp_video_timing), GFP_KERNEL);
	struct __disp_video_timing *new_video_timing =
			kzalloc(sizeof(struct __disp_video_timing), GFP_KERNEL);
	__disp_tv_mode_t hdmi_mode = gdisp.screen[sel].hdmi_mode;

	if (!old_video_timing && !new_video_timing)
		return DIS_FAIL;

	if (!gdisp.init_para.hdmi_set_videomode)
		return DIS_FAIL;

	if (gdisp.init_para.hdmi_get_video_timing(hdmi_mode,
			old_video_timing) != 0)
		return DIS_FAIL;

	videomode_to_video_timing(new_video_timing, mode);

	gdisp.init_para.hdmi_set_mode(DISP_TV_MODE_EDID);
	if (gdisp.init_para.hdmi_set_videomode(new_video_timing) != 0)
		goto failure;

	if (disp_clk_cfg(sel, DISP_OUTPUT_TYPE_HDMI, DISP_TV_MODE_EDID) != 0)
		goto failure;

	if (DE_BE_set_display_size(sel, new_video_timing->INPUTX,
			new_video_timing->INPUTY) != 0)
		goto failure;

	if (TCON1_set_hdmi_mode(sel, DISP_TV_MODE_EDID) != 0)
		goto failure;

	gdisp.screen[sel].hdmi_mode = DISP_TV_MODE_EDID;
	gdisp.screen[sel].b_out_interlace = new_video_timing->I;

	kfree(old_video_timing);
	kfree(new_video_timing);
	return DIS_SUCCESS;

failure:
	gdisp.init_para.hdmi_set_mode(hdmi_mode);
	gdisp.init_para.hdmi_set_videomode(old_video_timing);
	disp_clk_cfg(sel, DISP_OUTPUT_TYPE_HDMI, hdmi_mode);
	DE_BE_set_display_size(sel, old_video_timing->INPUTX,
			old_video_timing->INPUTY);
	TCON1_set_hdmi_mode(sel, hdmi_mode);
	kfree(old_video_timing);
	kfree(new_video_timing);
	return DIS_FAIL;
}

__s32 BSP_disp_hdmi_get_mode(__u32 sel)
{
	return gdisp.screen[sel].hdmi_mode;
}

__s32 BSP_disp_hdmi_check_support_mode(__u32 sel, __u8 mode)
{
	__s32 ret = 0;

	if (gdisp.init_para.hdmi_mode_support) {
		ret = gdisp.init_para.hdmi_mode_support(mode);
	} else {
		DE_WRN("hdmi_mode_support is NULL\n");
		return -1;
	}

	return ret;
}

__s32 BSP_disp_hdmi_get_hpd_status(__u32 sel)
{
	__s32 ret = 0;

	if (gdisp.init_para.hdmi_get_HPD_status) {
		ret = gdisp.init_para.hdmi_get_HPD_status();
	} else {
		DE_WRN("hdmi_get_HPD_status is NULL\n");
		return -1;
	}

	return ret;
}

__s32 BSP_disp_hdmi_set_src(__u32 sel, __disp_lcdc_src_t src)
{
	switch (src) {
	case DISP_LCDC_SRC_DE_CH1:
		TCON1_select_src(sel, LCDC_SRC_DE1);
		break;

	case DISP_LCDC_SRC_DE_CH2:
		TCON1_select_src(sel, LCDC_SRC_DE2);
		break;

	case DISP_LCDC_SRC_BLUT:
		TCON1_select_src(sel, LCDC_SRC_BLUE);
		break;

	default:
		DE_WRN("not supported lcdc src:%d in BSP_disp_tv_set_src\n",
		       src);
		return DIS_NOT_SUPPORT;
	}
	return DIS_SUCCESS;
}

__s32 BSP_disp_set_hdmi_func(__disp_hdmi_func *func)
{
	gdisp.init_para.hdmi_wait_edid = func->hdmi_wait_edid;
	gdisp.init_para.Hdmi_open = func->Hdmi_open;
	gdisp.init_para.Hdmi_close = func->Hdmi_close;
	gdisp.init_para.hdmi_set_mode = func->hdmi_set_mode;
	gdisp.init_para.hdmi_set_videomode = func->hdmi_set_videomode;
	gdisp.init_para.hdmi_mode_support = func->hdmi_mode_support;
	gdisp.init_para.hdmi_get_video_timing = func->hdmi_get_video_timing;
	gdisp.init_para.hdmi_get_HPD_status = func->hdmi_get_HPD_status;
	gdisp.init_para.hdmi_set_pll = func->hdmi_set_pll;

	return DIS_SUCCESS;
}
