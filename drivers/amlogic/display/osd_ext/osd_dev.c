/*
 * Amlogic osd
 * frame buffer driver
 *
 * Copyright (C) 2009 Amlogic, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Author:  Tim Yao <timyao@amlogic.com>
 *
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/fb.h>
#include <mach/am_regs.h>
#include <linux/amlogic/osd/osd.h>

#include "osd_hw.h"
#include "osd_dev.h"
/* to-do: TV output mode should be configured by
 * sysfs attribute
 */

void osddev_ext_set(struct myfb_dev *fbdev)
{
	fbdev_lock(fbdev);

	//memset((char*) fbdev->fb_mem,0x0,fbdev->fb_len);
	osd_ext_setup(&fbdev->osd_ext_ctl,
	              fbdev->fb_info->var.xoffset,
	              fbdev->fb_info->var.yoffset,
	              fbdev->fb_info->var.xres,
	              fbdev->fb_info->var.yres,
	              fbdev->fb_info->var.xres_virtual,
	              fbdev->fb_info->var.yres_virtual,
	              fbdev->osd_ext_ctl.disp_start_x,
	              fbdev->osd_ext_ctl.disp_start_y,
	              fbdev->osd_ext_ctl.disp_end_x,
	              fbdev->osd_ext_ctl.disp_end_y,
	              fbdev->fb_mem_paddr,
	              fbdev->color,
	              fbdev->fb_info->node - 2);

	fbdev_unlock(fbdev);

	return;
}

void osddev_ext_update_disp_axis(struct myfb_dev *fbdev, int  mode_change)
{
	osddev_ext_update_disp_axis_hw(fbdev->osd_ext_ctl.disp_start_x,
	                            fbdev->osd_ext_ctl.disp_end_x,
	                            fbdev->osd_ext_ctl.disp_start_y,
	                            fbdev->osd_ext_ctl.disp_end_y,
	                            fbdev->fb_info->var.xoffset,
	                            fbdev->fb_info->var.yoffset,
	                            mode_change,
	                            fbdev->fb_info->node - 2);
}

int osddev_ext_setcolreg(unsigned regno, u16 red, u16 green, u16 blue,
                         u16 transp, struct myfb_dev *fbdev)
{
	struct fb_info *info = fbdev->fb_info;

	if ((fbdev->color->color_index == COLOR_INDEX_02_PAL4) ||
	    (fbdev->color->color_index == COLOR_INDEX_04_PAL16) ||
	    (fbdev->color->color_index == COLOR_INDEX_08_PAL256)) {

		fbdev_lock(fbdev);

		osd_ext_setpal_hw(regno, red, green, blue, transp, fbdev->fb_info->node - 2);

		fbdev_unlock(fbdev);
	}

	if (info->fix.visual == FB_VISUAL_TRUECOLOR) {
		u32 v, r, g, b, a;

		if (regno >= 16) {
			return 1;
		}

		r = red    >> (16 - info->var.red.length);
		g = green  >> (16 - info->var.green.length);
		b = blue   >> (16 - info->var.blue.length);
		a = transp >> (16 - info->var.transp.length);

		v = (r << info->var.red.offset)   |
		    (g << info->var.green.offset) |
		    (b << info->var.blue.offset)  |
		    (a << info->var.transp.offset);

		((u32*)(info->pseudo_palette))[regno] = v;
	}

	return 0;
}

void osddev_ext_init(void)
{
	osd_ext_init_hw(0);
}

u32	osddev_ext_get_osd_ext_order(u32 index)
{
	return osd_ext_get_osd_ext_order_hw(index - 2);
}

void osddev_ext_change_osd_ext_order(u32 index, u32 order)
{
	osd_ext_change_osd_ext_order_hw(index - 2, order);
}

void osddev_ext_free_scale_enable(u32 index , u32 enable)
{
	//at present we only support osd1 & osd2 have the same random scale mode.
	osd_ext_free_scale_enable_hw(index - 2, enable);
}

void osddev_ext_get_free_scale_enable(u32 index, u32 *free_scale_enable)
{
	osd_ext_get_free_scale_enable_hw(index - 2, free_scale_enable);
}

void osddev_ext_free_scale_width(u32 index , u32 width)
{
	//at present we only support osd1 & osd2 have the same random scale mode.
	osd_ext_free_scale_width_hw(index - 2, width);
}

void osddev_ext_get_free_scale_width(u32 index, u32 *free_scale_width)
{
	osd_ext_get_free_scale_width_hw(index - 2, free_scale_width);
}

void osddev_ext_free_scale_height(u32 index , u32 height)
{
	//at present we only support osd1 & osd2 have the same random scale mode.
	osd_ext_free_scale_height_hw(index - 2, height);
}

void osddev_ext_get_free_scale_height(u32 index, u32 *free_scale_height)
{
	osd_ext_get_free_scale_height_hw(index - 2, free_scale_height);
}

void osddev_ext_get_free_scale_axis(u32 index, s32 *x0, s32 *y0, s32 *x1, s32 *y1)
{
	//at present we only support osd1 & osd2 have the same random scale mode.
	osd_ext_get_free_scale_axis_hw(index - 2, x0, y0, x1, y1);
}

void osddev_ext_set_free_scale_axis(u32 index, s32 x0, s32 y0, s32 x1, s32 y1)
{
	//at present we only support osd1 & osd2 have the same random scale mode.
	osd_ext_set_free_scale_axis_hw(index - 2, x0, y0, x1, y1);
}

void osddev_ext_enable_3d_mode(u32 index , u32 enable)
{
	osd_ext_enable_3d_mode_hw(index - 2, enable);
}

void osddev_ext_set_scale_axis(u32 index, s32 x0, s32 y0, s32 x1, s32 y1)
{
	osd_ext_set_scale_axis_hw(index - 2, x0, y0, x1, y1);
}

void osddev_ext_get_scale_axis(u32 index, s32 *x0, s32 *y0, s32 *x1, s32 *y1)
{
	osd_ext_get_scale_axis_hw(index - 2, x0, y0, x1, y1);
}

void osddev_ext_free_scale_mode(u32 index ,u32 freescale_mode)
{
	osd_ext_free_scale_mode_hw(index - 2, freescale_mode);
}

void osddev_ext_get_free_scale_mode(u32 index, u32 *freescale_mode)
{
	osd_ext_get_free_scale_mode_hw(index - 2, freescale_mode);
}

void osddev_ext_get_window_axis(u32 index, s32 *x0, s32 *y0, s32 *x1, s32 *y1)
{
	osd_ext_get_window_axis_hw(index - 2, x0, y0, x1, y1);
}

void osddev_ext_set_window_axis(u32 index, s32 x0, s32 y0, s32 x1, s32 y1)
{
	osd_ext_set_window_axis_hw(index - 2, x0, y0, x1, y1);
}

void osddev_ext_get_osd_ext_info(u32 index, s32(*posdval)[4], u32(*posdreg)[5], s32 info_flag)
{
	osd_ext_get_osd_ext_info_hw(index - 2, posdval, posdreg, info_flag);
}

void osddev_ext_set_2x_scale(u32 index, u16 h_scale_enable, u16 v_scale_enable)
{
	osd_ext_set_2x_scale_hw(index - 2, h_scale_enable, v_scale_enable);
}


void osddev_ext_get_osd_ext_rotate_on(u32 index, u32 *on_off)
{
        osd_ext_get_osd_ext_rotate_on_hw(index - 2, on_off);
}

void osddev_ext_set_osd_ext_rotate_on(u32 index, u32 on_off)
{
        osd_ext_set_osd_ext_rotate_on_hw(index - 2, on_off);
}

void osddev_ext_get_osd_ext_rotate_angle(u32 index, u32 *angle)
{
        osd_ext_get_osd_ext_rotate_angle_hw(index - 2, angle);
}

void osddev_ext_set_osd_ext_rotate_angle(u32 index, u32 angle)
{
        osd_ext_set_osd_ext_rotate_angle_hw(index - 2, angle);
}

void osddev_ext_get_prot_canvas(u32 index, s32 *x_start, s32 *y_start, s32 *x_end, s32 *y_end)
{
	osd_ext_get_prot_canvas_hw(index - 2, x_start, y_start, x_end, y_end);
}

void osddev_ext_set_prot_canvas(u32 index, s32 x_start, s32 y_start, s32 x_end, s32 y_end)
{
	osd_ext_set_prot_canvas_hw(index - 2, x_start, y_start, x_end, y_end);
}

void osddev_ext_set_block_windows(u32 index, u32 *block_windows)
{
	osd_ext_set_block_windows_hw(index - 2, block_windows);
}

void osddev_ext_get_block_windows(u32 index, u32 *block_windows)
{
	osd_ext_get_block_windows_hw(index - 2, block_windows);
}

void osddev_ext_set_block_mode(u32 index, u32 mode)
{
	osd_ext_set_block_mode_hw(index - 2, mode);
}

void osddev_ext_get_block_mode(u32 index, u32 *mode)
{
	osd_ext_get_block_mode_hw(index - 2, mode);
}

void osddev_ext_enable(int enable, int  index)
{
	osd_ext_enable_hw(enable, index - 2);
}

void osddev_ext_pan_display(struct fb_var_screeninfo *var, struct fb_info *fbi)
{
	osd_ext_pan_display_hw(var->xoffset, var->yoffset, fbi->node - 2);
}

#if defined(CONFIG_FB_OSD2_CURSOR)
void osddev_ext_cursor(struct myfb_dev *fbdev, s16 x, s16 y, s16 xstart, s16 ystart, u32 osd_ext_w, u32 osd_ext_h)
{
	fbdev_lock(fbdev);
	osd_ext_cursor_hw(x, y, xstart, ystart, osd_ext_w, osd_ext_h, fbdev->fb_info->node - 2);
	fbdev_unlock(fbdev);
}
#endif

void osddev_ext_set_colorkey(u32 index, u32 bpp, u32 colorkey)
{
	osd_ext_set_colorkey_hw(index - 2, bpp, colorkey);
}

void osddev_ext_srckey_enable(u32  index, u8 enable)
{
	osd_ext_srckey_enable_hw(index - 2, enable);
}

void osddev_ext_set_gbl_alpha(u32 index, u32 gbl_alpha)
{
	osd_ext_set_gbl_alpha_hw(index - 2, gbl_alpha);
}

u32 osddev_ext_get_gbl_alpha(u32  index)
{
	return osd_ext_get_gbl_alpha_hw(index - 2);
}

void osddev_ext_suspend(void)
{
	osd_ext_suspend_hw();
}

void osddev_ext_resume(void)
{
	osd_ext_resume_hw();
}

void osddev_ext_get_angle(u32 index, u32 *angle)
{
	osd_ext_get_angle_hw(index - 2, angle);
}

void osddev_ext_set_angle(u32 index, u32 angle)
{
	osd_ext_set_angle_hw(index - 2, angle);
}

void osddev_ext_get_clone(u32 index, u32 *clone)
{
	osd_ext_get_clone_hw(index - 2, clone);
}

void osddev_ext_set_clone(u32 index, u32 clone)
{
	osd_ext_set_clone_hw(index - 2, clone);
}
