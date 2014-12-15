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
 *		    jianfeng_wang : add ge2d support 09/05/21
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/fb.h>
#include <linux/spinlock.h>
#include <asm/cacheflush.h>
#include <mach/am_regs.h>
#include <linux/fs.h>
#include <linux/sysfs.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/of_fdt.h>
#include <linux/console.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include "osd_log.h"
#include "osd_main.h"
#include "osd_dev.h"
#include <linux/amlogic/amlog.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
static struct early_suspend early_suspend;
static int early_suspend_flag = 0;
#endif
#ifdef CONFIG_SCREEN_ON_EARLY
static int early_resume_flag = 0;
#endif

MODULE_AMLOG(AMLOG_DEFAULT_LEVEL, 0x0, LOG_LEVEL_DESC, LOG_MASK_DESC);

static myfb_dev_t  *gp_fbdev_list[OSD_COUNT] = {NULL, NULL};

static DEFINE_MUTEX(dbg_mutex);
static char request2XScaleValue[32];
static char videohole[32];


static const color_bit_define_t*
_find_color_format(struct fb_var_screeninfo * var)
{
	u32	upper_margin, lower_margin, i, level;
	const color_bit_define_t *ret = NULL;

	level = (var->bits_per_pixel - 1) / 8;
	switch (level) {
	case 0:
		upper_margin = COLOR_INDEX_08_PAL256;
		lower_margin = COLOR_INDEX_02_PAL4;
		break;
	case 1:
		upper_margin = COLOR_INDEX_16_565;
		lower_margin = COLOR_INDEX_16_655;
		break;
	case 2:
		upper_margin = COLOR_INDEX_24_RGB;
		lower_margin = COLOR_INDEX_24_6666_A;
		break;
	case 3:
		upper_margin = COLOR_INDEX_32_ARGB;
		lower_margin = COLOR_INDEX_32_BGRA;
		break;
	case 4:
		upper_margin = COLOR_INDEX_YUV_422;
		lower_margin = COLOR_INDEX_YUV_422;
		break;
	default :
		return NULL;
	}
	//if not provide color component length then we find the first depth match.
	if ((var->red.length == 0) || (var->green.length == 0) || (var->blue.length == 0) ||
	    var->bits_per_pixel != (var->red.length + var->green.length + var->blue.length + var->transp.length)) {
		amlog_mask_level(LOG_MASK_PARA, LOG_LEVEL_LOW, "not provide color component length,use default color \n");
		ret = &default_color_format_array[upper_margin];
	} else {
		for (i = upper_margin; i >= lower_margin; i--) {
			if ((default_color_format_array[i].red_length == var->red.length) &&
			    (default_color_format_array[i].green_length == var->green.length) &&
			    (default_color_format_array[i].blue_length == var->blue.length) &&
			    (default_color_format_array[i].transp_length == var->transp.length) &&
			    (default_color_format_array[i].transp_offset == var->transp.offset) &&
			    (default_color_format_array[i].green_offset == var->green.offset) &&
			    (default_color_format_array[i].blue_offset == var->blue.offset) &&
			    (default_color_format_array[i].red_offset == var->red.offset)) {
				ret = &default_color_format_array[i];
				break;
			}
		}
	}
	return ret;
}
static void __init
_fbdev_set_default(struct myfb_dev *fbdev, int index)
{
	/* setup default value */
	fbdev->fb_info->var = mydef_var[index];
	fbdev->fb_info->fix = mydef_fix;
	fbdev->color = _find_color_format(&fbdev->fb_info->var);
}

static int
osd_ext_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct fb_fix_screeninfo *fix;
	struct myfb_dev *fbdev = (struct myfb_dev*)info->par;
	const color_bit_define_t   *color_format_pt;

	fix = &info->fix;

	color_format_pt = _find_color_format(var);
	if (color_format_pt == NULL || color_format_pt->color_index == 0) {
		return -EFAULT ;
	}
	amlog_mask_level(LOG_MASK_PARA, LOG_LEVEL_LOW, "select color format :index%d,bpp %d\r\n", color_format_pt->color_index, \
	                 color_format_pt->bpp) ;
	fbdev->color = color_format_pt ;
	var->red.offset = color_format_pt->red_offset;
	var->red.length = color_format_pt->red_length;
	var->red.msb_right = color_format_pt->red_msb_right ;
	var->green.offset  = color_format_pt->green_offset;
	var->green.length  = color_format_pt->green_length;
	var->green.msb_right = color_format_pt->green_msb_right;
	var->blue.offset   = color_format_pt->blue_offset;
	var->blue.length   = color_format_pt->blue_length;
	var->blue.msb_right = color_format_pt->blue_msb_right;
	var->transp.offset = color_format_pt->transp_offset ;
	var->transp.length = color_format_pt->transp_length ;
	var->transp.msb_right = color_format_pt->transp_msb_right ;
	var->bits_per_pixel = color_format_pt->bpp ;
	amlog_mask_level(LOG_MASK_PARA, LOG_LEVEL_LOW,
	                 "rgba(L/O):%d/%d-%d/%d-%d/%d-%d/%d\n",
	                 var->red.length, var->red.offset,
	                 var->green.length, var->green.offset,
	                 var->blue.length, var->blue.offset,
	                 var->transp.length, var->transp.offset);
	fix->visual = color_format_pt->color_type ;
	//adjust memory length.
	fix->line_length = var->xres_virtual * var->bits_per_pixel / 8;
	if (var->xres_virtual * var->yres_virtual * var->bits_per_pixel / 8 > fbdev->fb_len) {
		amlog_mask_level(LOG_MASK_PARA, LOG_LEVEL_HIGH, "no enough memory for %d*%d*%d\r\n", var->xres, var->yres, var->bits_per_pixel);
		return  -ENOMEM;
	}
	if (var->xres_virtual < var->xres) {
		var->xres_virtual = var->xres;
	}

	if (var->yres_virtual < var->yres) {
		var->yres_virtual = var->yres;
	}

	var->left_margin = var->right_margin = var->upper_margin = var->lower_margin = 0;


	if (var->xres + var->xoffset > var->xres_virtual) {
		var->xoffset = var->xres_virtual - var->xres;
	}
	if (var->yres + var->yoffset > var->yres_virtual) {
		var->yoffset = var->yres_virtual - var->yres;
	}

	return 0;
}

static int
osd_ext_set_par(struct fb_info *info)
{
	const vinfo_t *vinfo;
	struct myfb_dev *fbdev = (struct myfb_dev *)info->par;
	osd_ctl_t *osd_ext_ctrl = &fbdev->osd_ext_ctl;
	u32  virt_end_x, virt_end_y;

	vinfo = get_current_vinfo2();
	virt_end_x = osd_ext_ctrl->disp_start_x + info->var.xres;
	virt_end_y = osd_ext_ctrl->disp_start_y + info->var.yres;

	if (virt_end_x > vinfo->width) {
		osd_ext_ctrl->disp_end_x = vinfo->width - 1 ;
	} else {
		osd_ext_ctrl->disp_end_x = virt_end_x - 1;
	}
	if (virt_end_y  > vinfo->height) {
		osd_ext_ctrl->disp_end_y = vinfo->height - 1;
	} else {
		osd_ext_ctrl->disp_end_y = virt_end_y - 1;
	}
	osddev_ext_set((struct myfb_dev *)info->par);
	return  0;
}

static int
osd_ext_setcolreg(unsigned regno, unsigned red, unsigned green, unsigned blue,
                  unsigned transp, struct fb_info *info)
{
	return osddev_ext_setcolreg(regno, red, green, blue,
	                            transp, (struct myfb_dev *)info->par);
}

static int
osd_ext_setcmap(struct fb_cmap *cmap, struct fb_info *info)
{
	int count, index, r;
	u16 *red, *green, *blue, *transp;
	u16 trans = 0xffff;

	red     = cmap->red;
	green   = cmap->green;
	blue    = cmap->blue;
	transp  = cmap->transp;
	index   = cmap->start;

	for (count = 0; count < cmap->len; count++) {
		if (transp) {
			trans = *transp++;
		}
		r = osddev_ext_setcolreg(index++, *red++, *green++, *blue++, trans,
		                         (struct myfb_dev *)info->par);
		if (r != 0) {
			return r;
		}
	}

	return 0;
}

static int
osd_ext_ioctl(struct fb_info *info, unsigned int cmd,
              unsigned long arg)
{
	struct myfb_dev *fbdev = (struct myfb_dev *)info->par;
	void __user *argp = (void __user *)arg;
	u32 src_colorkey;//16 bit or 24 bit
	u32 srckey_enable;
	u32 gbl_alpha;
	u32 osd_ext_order;
	s32 osd_ext_axis[4] = {0};
	s32 osd_ext_dst_axis[4] = {0};
	u32 block_windows[8] = {0};
	u32 block_mode;
	unsigned long ret;

	switch (cmd) {
	case  FBIOPUT_OSD_SRCKEY_ENABLE:
		ret = copy_from_user(&srckey_enable, argp, sizeof(u32));
		break;
	case  FBIOPUT_OSD_SRCCOLORKEY:
		ret = copy_from_user(&src_colorkey, argp, sizeof(u32));
		break ;
	case FBIOPUT_OSD_SET_GBL_ALPHA:
		ret = copy_from_user(&gbl_alpha, argp, sizeof(u32));
		break;
	case FBIOPUT_OSD_SCALE_AXIS:
		ret = copy_from_user(&osd_ext_axis, argp, 4 * sizeof(s32));
		break;
	case FBIOGET_OSD_SCALE_AXIS:
	case FBIOPUT_OSD_ORDER:
	case FBIOGET_OSD_ORDER:
	case FBIOGET_OSD_GET_GBL_ALPHA:
	case FBIOPUT_OSD_2X_SCALE:
	case FBIOPUT_OSD_ENABLE_3D_MODE:
	case FBIOPUT_OSD_FREE_SCALE_ENABLE:
	case FBIOPUT_OSD_FREE_SCALE_MODE:
	case FBIOPUT_OSD_FREE_SCALE_WIDTH:
	case FBIOPUT_OSD_FREE_SCALE_HEIGHT:
	case FBIOGET_OSD_BLOCK_WINDOWS:
	case FBIOGET_OSD_BLOCK_MODE:
	case FBIOGET_OSD_FREE_SCALE_AXIS:
	case FBIOGET_OSD_WINDOW_AXIS:
	case FBIOPUT_OSD_ROTATE_ON:
	case FBIOPUT_OSD_ROTATE_ANGLE:
		break;
	case FBIOPUT_OSD_BLOCK_MODE:
		block_mode = (u32)argp;
		break;
	case FBIOPUT_OSD_BLOCK_WINDOWS:
		ret = copy_from_user(&block_windows, argp, 8 * sizeof(u32));
		break;
	case FBIOPUT_OSD_FREE_SCALE_AXIS:
		ret = copy_from_user(&osd_ext_axis, argp, 4 * sizeof(s32));
		break;
	case FBIOPUT_OSD_WINDOW_AXIS:
		ret=copy_from_user(&osd_ext_dst_axis, argp, 4 * sizeof(s32));
		break;
	default :
		amlog_mask_level(LOG_MASK_IOCTL, LOG_LEVEL_HIGH, "command not supported\r\n ");
		return -1;
	}
	mutex_lock(&fbdev->lock);

	switch (cmd) {
	case FBIOPUT_OSD_ORDER:
		osddev_ext_change_osd_ext_order(info->node, arg);
		break;
	case FBIOGET_OSD_ORDER:
		osd_ext_order = osddev_ext_get_osd_ext_order(info->node);
		ret = copy_to_user(argp, &osd_ext_order, sizeof(u32));
		break;
	case FBIOPUT_OSD_FREE_SCALE_WIDTH:
		osddev_ext_free_scale_width(info->node, arg);
		break;
	case FBIOPUT_OSD_FREE_SCALE_HEIGHT:
		osddev_ext_free_scale_height(info->node, arg);
		break;
	case FBIOPUT_OSD_FREE_SCALE_ENABLE:
		osddev_ext_free_scale_enable(info->node, arg);
		break;
	case FBIOPUT_OSD_FREE_SCALE_MODE:
		osddev_ext_free_scale_mode(info->node, arg);
		break;
	case FBIOPUT_OSD_ENABLE_3D_MODE:
		osddev_ext_enable_3d_mode(info->node, arg);
		break;
	case FBIOPUT_OSD_2X_SCALE: //arg :higher 16 bit h_scale_enable, lower 16 bit v_scale_enable
		osddev_ext_set_2x_scale(info->node, arg & 0xffff0000 ? 1 : 0, arg & 0xffff ? 1 : 0);
		break;
	case FBIOPUT_OSD_ROTATE_ON:
		osddev_ext_set_osd_ext_rotate_on(info->node, arg);
		break;
	case FBIOPUT_OSD_ROTATE_ANGLE:
		osddev_ext_set_osd_ext_rotate_angle(info->node, arg);
		break;
	case FBIOPUT_OSD_SRCCOLORKEY:
		switch (fbdev->color->color_index) {
		case COLOR_INDEX_16_655:
		case COLOR_INDEX_16_844:
		case COLOR_INDEX_16_565:
		case COLOR_INDEX_24_888_B:
		case COLOR_INDEX_24_RGB:
		case COLOR_INDEX_YUV_422:
			amlog_mask_level(LOG_MASK_IOCTL, LOG_LEVEL_LOW, "set osd color key 0x%x\r\n", src_colorkey);
			fbdev->color_key = src_colorkey;
			osddev_ext_set_colorkey(info->node, fbdev->color->color_index, src_colorkey);
			break;
		default:
			break;
		}
		break ;
	case FBIOPUT_OSD_SRCKEY_ENABLE:
		switch (fbdev->color->color_index) {
		case COLOR_INDEX_16_655:
		case COLOR_INDEX_16_844:
		case COLOR_INDEX_16_565:
		case COLOR_INDEX_24_888_B:
		case COLOR_INDEX_24_RGB:
		case COLOR_INDEX_YUV_422:
			amlog_mask_level(LOG_MASK_IOCTL, LOG_LEVEL_LOW, "set osd color key %s\r\n", srckey_enable ? "enable" : "disable");
			if (srckey_enable != 0) {
				fbdev->enable_key_flag |= KEYCOLOR_FLAG_TARGET;
				if (!(fbdev->enable_key_flag & KEYCOLOR_FLAG_ONHOLD)) {
					osddev_ext_srckey_enable(info->node, 1);
					fbdev->enable_key_flag |= KEYCOLOR_FLAG_CURRENT;
				}
			} else {
				osddev_ext_srckey_enable(info->node, 0);
				fbdev->enable_key_flag &= ~(KEYCOLOR_FLAG_TARGET | KEYCOLOR_FLAG_CURRENT);
			}
			break;
		default:
			break;
		}
		break;
	case FBIOPUT_OSD_SET_GBL_ALPHA:
		osddev_ext_set_gbl_alpha(info->node, gbl_alpha);
		break;
	case  FBIOGET_OSD_GET_GBL_ALPHA:
		gbl_alpha = osddev_ext_get_gbl_alpha(info->node);
		ret = copy_to_user(argp, &gbl_alpha, sizeof(u32));
		break;

	case FBIOGET_OSD_SCALE_AXIS:
		osddev_ext_get_scale_axis(info->node, &osd_ext_axis[0], &osd_ext_axis[1], &osd_ext_axis[2], &osd_ext_axis[3]);
		ret = copy_to_user(argp, &osd_ext_axis, 4 * sizeof(s32));
		break;
	case FBIOPUT_OSD_SCALE_AXIS:
		osddev_ext_set_scale_axis(info->node, osd_ext_axis[0], osd_ext_axis[1], osd_ext_axis[2], osd_ext_axis[3]);
		break;
	case FBIOGET_OSD_BLOCK_WINDOWS:
		osddev_ext_get_block_windows(info->node, block_windows);
		ret = copy_to_user(argp, &block_windows, 8 * sizeof(u32));
		break;
	case FBIOPUT_OSD_BLOCK_WINDOWS:
		osddev_ext_set_block_windows(info->node, block_windows);
		break;
	case FBIOPUT_OSD_BLOCK_MODE:
		osddev_ext_set_block_mode(info->node, block_mode);
		break;
	case FBIOGET_OSD_BLOCK_MODE:
		osddev_ext_get_block_mode(info->node, &block_mode);
		ret = copy_to_user(argp, &block_mode, sizeof(u32));
		break;
	case FBIOGET_OSD_FREE_SCALE_AXIS:
		osddev_ext_get_free_scale_axis(info->node, &osd_ext_axis[0], &osd_ext_axis[1], &osd_ext_axis[2], &osd_ext_axis[3]);
		ret = copy_to_user(argp, &osd_ext_axis, 4 * sizeof(s32));
		break;
	case FBIOGET_OSD_WINDOW_AXIS:
		osddev_ext_get_window_axis(info->node, &osd_ext_dst_axis[0], &osd_ext_dst_axis[1], &osd_ext_dst_axis[2], &osd_ext_dst_axis[3]);
		ret=copy_to_user(argp, &osd_ext_dst_axis, 4 * sizeof(s32));
		break;
	case FBIOPUT_OSD_FREE_SCALE_AXIS:
		osddev_ext_set_free_scale_axis(info->node, osd_ext_axis[0], osd_ext_axis[1], osd_ext_axis[2], osd_ext_axis[3]);
		break;
	case FBIOPUT_OSD_WINDOW_AXIS:
		osddev_ext_set_window_axis(info->node, osd_ext_dst_axis[0], osd_ext_dst_axis[1], osd_ext_dst_axis[2], osd_ext_dst_axis[3]);
		break;
	default:
		break;
	}

	mutex_unlock(&fbdev->lock);

	return  0;
}

static int osd_ext_open(struct fb_info *info, int arg)
{
	return 0;
}

static int
osd_ext_blank(int blank_mode, struct fb_info *info)
{
	osddev_ext_enable((blank_mode != 0) ? 0 : 1, info->node);

	return 0;
}

static int osd_ext_pan_display(struct fb_var_screeninfo *var,
                               struct fb_info *fbi)
{

	osddev_ext_pan_display(var, fbi);
	amlog_mask_level(LOG_MASK_PARA, LOG_LEVEL_LOW, "osd_ext_pan_display:=>osd%d\r\n", fbi->node);
	return 0;
}

#if defined(CONFIG_FB_OSD2_CURSOR)
static int osd_ext_cursor(struct fb_info *fbi, struct fb_cursor *var)
{
	s16 startx = 0, starty = 0;
	myfb_dev_t *fb_dev = gp_fbdev_list[1];
	if (fb_dev) {
		startx = fb_dev->osd_ext_ctl.disp_start_x;
		starty = fb_dev->osd_ext_ctl.disp_start_y;
	}
	osddev_ext_cursor((struct myfb_dev *)fbi->par,
	                  (s16)var->hot.x, (s16)var->hot.y,
	                  startx, starty,
	                  fbi->var.xres, fbi->var.yres);
	return 0;
}
#endif

static int osd_ext_sync(struct fb_info *info)
{
	return 0;
}

/* fb_ops structures */
static struct fb_ops osd_ext_ops = {
	.owner          = THIS_MODULE,
	.fb_check_var   = osd_ext_check_var,
	.fb_set_par     = osd_ext_set_par,
	.fb_setcolreg   = osd_ext_setcolreg,
	.fb_setcmap     = osd_ext_setcmap,
	.fb_fillrect    = cfb_fillrect,
	.fb_copyarea    = cfb_copyarea,
	.fb_imageblit   = cfb_imageblit,
#ifdef CONFIG_FB_SOFT_CURSOR
	.fb_cursor      = soft_cursor,
#elif defined(CONFIG_FB_OSD2_CURSOR)
	.fb_cursor      = osd_ext_cursor,
#endif
	.fb_ioctl       = osd_ext_ioctl,
	.fb_open        = osd_ext_open,
	.fb_blank       = osd_ext_blank,
	.fb_pan_display = osd_ext_pan_display,
	.fb_sync        = osd_ext_sync,
};

static void set_default_display_axis(struct fb_var_screeninfo *var, osd_ctl_t *osd_ext_ctrl, const vinfo_t *vinfo)
{
	u32 virt_end_x = osd_ext_ctrl->disp_start_x + var->xres;
	u32 virt_end_y = osd_ext_ctrl->disp_start_y + var->yres;

	if (virt_end_x > vinfo->width) {
		osd_ext_ctrl->disp_end_x = vinfo->width - 1 ; //screen axis
	} else {
		osd_ext_ctrl->disp_end_x = virt_end_x - 1 ; //screen axis
	}
	if (virt_end_y > vinfo->height) {
		osd_ext_ctrl->disp_end_y = vinfo->height - 1 ;
	} else {
		osd_ext_ctrl->disp_end_y = virt_end_y - 1 ; //screen axis
	}
	return ;
}

int osd_ext_notify_callback(struct notifier_block *block, unsigned long cmd , void *para)
{
	const vinfo_t *vinfo;
	myfb_dev_t *fb_dev;
	int  i, blank;
	disp_rect_t *disp_rect;

	vinfo = get_current_vinfo2();
	amlog_mask_level(LOG_MASK_PARA, LOG_LEVEL_LOW, "tv_server:vmode=%s\r\n", vinfo->name);

	if(vinfo->mode == VMODE_INIT_NULL){
		return 1;
	}

	switch (cmd) {
	case  VOUT_EVENT_MODE_CHANGE:
		amlog_mask_level(LOG_MASK_PARA, LOG_LEVEL_LOW, "recevie change mode  message \r\n");
		for (i = 0; i < OSD_COUNT; i++) {
			if (NULL == (fb_dev = gp_fbdev_list[i])) {
				continue;
			}
			set_default_display_axis(&fb_dev->fb_info->var, &fb_dev->osd_ext_ctl, vinfo);
			console_lock();
			osddev_ext_update_disp_axis(fb_dev, 1);
			console_unlock();
		}
		break;

	case VOUT_EVENT_OSD_BLANK:
		blank = *(int*)para ;
		for (i = 0; i < OSD_COUNT; i++) {
			if (NULL == (fb_dev = gp_fbdev_list[i])) {
				continue;
			}
			console_lock();
			osd_ext_blank(blank, fb_dev->fb_info);
			console_unlock();
		}
		break;
	case   VOUT_EVENT_OSD_DISP_AXIS:
		disp_rect = (disp_rect_t*)para;

		for (i = 0; i < OSD_COUNT; i++) {
			if (!disp_rect) {
				break;
			}
			fb_dev = gp_fbdev_list[i];
			if (fb_dev->preblend_enable) {
				break;    //if osd layer preblend ,it's position is controled by vpp.
			}
			fb_dev->osd_ext_ctl.disp_start_x = disp_rect->x  ;
			fb_dev->osd_ext_ctl.disp_start_y = disp_rect->y  ;
			amlog_mask_level(LOG_MASK_PARA, LOG_LEVEL_LOW, "set disp axis: x:%d y:%d w:%d h:%d\r\n"  , \
			                 disp_rect->x, disp_rect->y, \
			                 disp_rect->w, disp_rect->h);
			if (disp_rect->x + disp_rect->w > vinfo->width) {
				fb_dev->osd_ext_ctl.disp_end_x = vinfo->width - 1;
			} else {
				fb_dev->osd_ext_ctl.disp_end_x = fb_dev->osd_ext_ctl.disp_start_x + disp_rect->w - 1 ;
			}
			if (disp_rect->y + disp_rect->h > vinfo->height) {
				fb_dev->osd_ext_ctl.disp_end_y = vinfo->height - 1;
			} else {
				fb_dev->osd_ext_ctl.disp_end_y = fb_dev->osd_ext_ctl.disp_start_y + disp_rect->h - 1 ;
			}
			disp_rect ++;
			amlog_mask_level(LOG_MASK_PARA, LOG_LEVEL_LOW, "new disp axis: startx:%d starty:%d endx:%d endy:%d\r\n"  , \
			                 fb_dev->osd_ext_ctl.disp_start_x, fb_dev->osd_ext_ctl.disp_start_y, \
			                 fb_dev->osd_ext_ctl.disp_end_x, fb_dev->osd_ext_ctl.disp_end_y);
			console_lock();
			osddev_ext_update_disp_axis(fb_dev, 0);
			console_unlock();
		}

		break;
	}
	return 0;
}

static struct notifier_block osd_ext_notifier_nb = {
	.notifier_call	= osd_ext_notify_callback,
};

static ssize_t store_preblend_enable(struct device *device, struct device_attribute *attr,
                                     const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	struct myfb_dev *fbdev = (struct myfb_dev *)fb_info->par;
	fbdev->preblend_enable = simple_strtoul(buf, NULL, 0);
	vout_notifier_call_chain(VOUT_EVENT_OSD_PREBLEND_ENABLE, &fbdev->preblend_enable) ;
	return count;
}

static ssize_t show_preblend_enable(struct device *device, struct device_attribute *attr,
                                    char *buf)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	struct myfb_dev *fbdev = (struct myfb_dev *)fb_info->par;
	return snprintf(buf, PAGE_SIZE, "preblend[%s]\n", fbdev->preblend_enable ? "enable" : "disable");
}

static ssize_t store_enable_3d(struct device *device, struct device_attribute *attr,
                               const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	struct myfb_dev *fbdev = (struct myfb_dev *)fb_info->par;
	int err;
	fbdev->enable_3d = simple_strtoul(buf, NULL, 0);
	if ((err = osd_ext_ioctl(fb_info, FBIOPUT_OSD_ENABLE_3D_MODE, fbdev->enable_3d))) {
		return err;
	}
	return count;
}

static ssize_t show_enable_3d(struct device *device, struct device_attribute *attr,
                              char *buf)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	struct myfb_dev *fbdev = (struct myfb_dev *)fb_info->par;
	return snprintf(buf, PAGE_SIZE, "3d_enable:[0x%x]\n", fbdev->enable_3d);
}

static ssize_t store_color_key(struct device *device, struct device_attribute *attr,
                               const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	struct myfb_dev *fbdev = (struct myfb_dev *)fb_info->par;
	int r = simple_strtoul(buf, NULL, 16);
	switch (fbdev->color->color_index) {
	case COLOR_INDEX_16_655:
	case COLOR_INDEX_16_844:
	case COLOR_INDEX_16_565:
	case COLOR_INDEX_24_888_B:
	case COLOR_INDEX_24_RGB:
	case COLOR_INDEX_YUV_422:
		fbdev->color_key = r;
		osddev_ext_set_colorkey(fb_info->node, fbdev->color->color_index, fbdev->color_key);
		break;
	default:
		break;
	}
	return count;
}

static ssize_t show_color_key(struct device *device, struct device_attribute *attr,
                              char *buf)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	struct myfb_dev *fbdev = (struct myfb_dev *)fb_info->par;
	return snprintf(buf, PAGE_SIZE, "0x%x\n", fbdev->color_key);
}

static ssize_t store_enable_key(struct device *device, struct device_attribute *attr,
                                const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	struct myfb_dev *fbdev = (struct myfb_dev *)fb_info->par;
	int r = simple_strtoul(buf, NULL, 0);

	mutex_lock(&fbdev->lock);

	if (r != 0) {
		fbdev->enable_key_flag |= KEYCOLOR_FLAG_TARGET;
		if (!(fbdev->enable_key_flag & KEYCOLOR_FLAG_ONHOLD)) {
			osddev_ext_srckey_enable(fb_info->node, 1);
			fbdev->enable_key_flag |= KEYCOLOR_FLAG_CURRENT;
		}
	} else {
		fbdev->enable_key_flag &= ~(KEYCOLOR_FLAG_TARGET | KEYCOLOR_FLAG_CURRENT);
		osddev_ext_srckey_enable(fb_info->node, 0);
	}

	mutex_unlock(&fbdev->lock);

	return count;
}

static ssize_t show_enable_key(struct device *device, struct device_attribute *attr,
                               char *buf)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	struct myfb_dev *fbdev = (struct myfb_dev *)fb_info->par;
	return snprintf(buf, PAGE_SIZE, (fbdev->enable_key_flag & KEYCOLOR_FLAG_TARGET) ? "1\n" : "0\n");
}

static ssize_t store_enable_key_onhold(struct device *device, struct device_attribute *attr,
                                       const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	struct myfb_dev *fbdev = (struct myfb_dev *)fb_info->par;
	int r = simple_strtoul(buf, NULL, 0);

	mutex_lock(&fbdev->lock);

	if (r != 0) {
		/* hold all the calls to enable color key */
		fbdev->enable_key_flag |= KEYCOLOR_FLAG_ONHOLD;
		fbdev->enable_key_flag &= ~KEYCOLOR_FLAG_CURRENT;
		osddev_ext_srckey_enable(fb_info->node, 0);

	} else {
		fbdev->enable_key_flag &= ~KEYCOLOR_FLAG_ONHOLD;

		/* if target and current mistach then recover the pending key settings */
		if (fbdev->enable_key_flag & KEYCOLOR_FLAG_TARGET) {
			if ((fbdev->enable_key_flag & KEYCOLOR_FLAG_CURRENT) == 0) {
				fbdev->enable_key_flag |= KEYCOLOR_FLAG_CURRENT;
				osddev_ext_srckey_enable(fb_info->node, 1);
			}
		} else {
			if (fbdev->enable_key_flag & KEYCOLOR_FLAG_CURRENT) {
				fbdev->enable_key_flag &= ~KEYCOLOR_FLAG_CURRENT;
				osddev_ext_srckey_enable(fb_info->node, 0);
			}
		}
	}

	mutex_unlock(&fbdev->lock);

	return count;
}

static ssize_t show_enable_key_onhold(struct device *device, struct device_attribute *attr,
                                      char *buf)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	struct myfb_dev *fbdev = (struct myfb_dev *)fb_info->par;
	return snprintf(buf, PAGE_SIZE, (fbdev->enable_key_flag & KEYCOLOR_FLAG_ONHOLD) ? "1\n" : "0\n");
}


static int parse_para(const char *para, int para_num, int *result)
{
	char *endp;
	const char *startp = para;
	int *out = result;
	int len = 0, count = 0;

	if (!startp) {
		return 0;
	}

	len = strlen(startp);

	do {
		while (startp && (isspace(*startp) || !isgraph(*startp)) && len) {
			startp++;
			len--;
		}

		if (len == 0) {
			break;
		}

		*out++ = simple_strtol(startp, &endp, 0);

		len -= endp - startp;
		startp = endp;
		count++;

	} while ((endp) && (count < para_num) && (len > 0));

	return count;
}

static ssize_t show_request_2xscale(struct device *device, struct device_attribute *attr,
                                    char *buf)
{
	memcpy(buf, request2XScaleValue, 32);
	return 32;
}

static ssize_t store__request_2xscale(struct device *device, struct device_attribute *attr,
                                      const char *buf, size_t count)
{
	memset(request2XScaleValue, 0, 32);
	memcpy(request2XScaleValue, buf, count);
	return count;
}

static ssize_t  show_video_hole(struct device *device, struct device_attribute *attr,
                                char *buf)
{
	memcpy(buf, videohole, 32);
	return 32;
}

static ssize_t store__video_hole(struct device *device, struct device_attribute *attr,
                                 const char *buf, size_t count)
{
	memset(videohole, 0, 32);
	memcpy(videohole, buf, count);
	return count;
}

static ssize_t show_free_scale_axis(struct device *device, struct device_attribute *attr,
                                    char *buf)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	int x, y, w, h;

	osddev_ext_get_free_scale_axis(fb_info->node, &x, &y, &w, &h);

	return snprintf(buf, PAGE_SIZE, "%d %d %d %d\n", x, y, w, h);
}

static ssize_t store_free_scale_axis(struct device *device, struct device_attribute *attr,
                                     const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	int parsed[4];

	if (likely(parse_para(buf, 4, parsed) == 4)) {
		osddev_ext_set_free_scale_axis(fb_info->node, parsed[0], parsed[1], parsed[2], parsed[3]);
	} else {
		amlog_level(LOG_LEVEL_HIGH, "set free scale axis error\n");
	}

	return count;
}

static ssize_t show_scale_axis(struct device *device, struct device_attribute *attr,
                               char *buf)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	int x0, y0, x1, y1;

	osddev_ext_get_scale_axis(fb_info->node, &x0, &y0, &x1, &y1);

	return snprintf(buf, PAGE_SIZE, "%d %d %d %d\n", x0, y0, x1, y1);
}

static ssize_t store_scale_axis(struct device *device, struct device_attribute *attr,
                                const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	int parsed[4];

	if (likely(parse_para(buf, 4, parsed) == 4)) {
		osddev_ext_set_scale_axis(fb_info->node, parsed[0], parsed[1], parsed[2], parsed[3]);
	} else {
		amlog_level(LOG_LEVEL_HIGH, "set scale axis error\n");
	}

	return count;
}

static ssize_t store_scale_width(struct device *device, struct device_attribute *attr,
                                 const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	unsigned int free_scale_width = 0;
	int err;
	free_scale_width = simple_strtoul(buf, NULL, 0);
	if ((err = osd_ext_ioctl(fb_info, FBIOPUT_OSD_FREE_SCALE_WIDTH, free_scale_width))) {
		return err;
	}
	return count;
}

static ssize_t show_scale_width(struct device *device, struct device_attribute *attr,
                                 char *buf)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	unsigned int free_scale_width = 0;

	osddev_ext_get_free_scale_width(fb_info->node, &free_scale_width);
	return snprintf(buf, PAGE_SIZE, "free_scale_width:[0x%x]\n", free_scale_width);
}

static ssize_t store_scale_height(struct device *device, struct device_attribute *attr,
                                  const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	unsigned int free_scale_height = 0;
	int err;
	free_scale_height = simple_strtoul(buf, NULL, 0);
	if ((err = osd_ext_ioctl(fb_info, FBIOPUT_OSD_FREE_SCALE_HEIGHT, free_scale_height))) {
		return err;
	}
	return count;
}

static ssize_t show_scale_height(struct device *device, struct device_attribute *attr,
                                  char *buf)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	unsigned int free_scale_height = 0;

	osddev_ext_get_free_scale_height(fb_info->node, &free_scale_height);
	return snprintf(buf, PAGE_SIZE, "free_scale_height:[0x%x]\n", free_scale_height);
}

static ssize_t store_free_scale(struct device *device, struct device_attribute *attr,
                                const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	unsigned int free_scale_enable = 0;
	int err;
	free_scale_enable = simple_strtoul(buf, NULL, 0);
	if ((err = osd_ext_ioctl(fb_info, FBIOPUT_OSD_FREE_SCALE_ENABLE, free_scale_enable))) {
		return err;
	}
	return count;
}

static ssize_t show_free_scale(struct device *device, struct device_attribute *attr,
                               char *buf)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	unsigned int free_scale_enable = 0;

	osddev_ext_get_free_scale_enable(fb_info->node, &free_scale_enable);
	return snprintf(buf, PAGE_SIZE, "free_scale_enable:[0x%x]\n",free_scale_enable);
}

static ssize_t store_scale(struct device *device, struct device_attribute *attr,
                           const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	struct myfb_dev *fbdev = (struct myfb_dev *)fb_info->par;
	int err;
	fbdev->scale = simple_strtoul(buf, NULL, 0);
	if ((err = osd_ext_ioctl(fb_info, FBIOPUT_OSD_2X_SCALE, fbdev->scale))) {
		return err;
	}
	return count;
}

static ssize_t show_scale(struct device *device, struct device_attribute *attr,
                          char *buf)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	struct myfb_dev *fbdev = (struct myfb_dev *)fb_info->par;
	return snprintf(buf, PAGE_SIZE, "scale:[0x%x]\n", fbdev->scale);
}

static ssize_t store_osd_info(struct device *device, struct device_attribute *attr,
                                  const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	int osd_ext_val[3][4] = {};
	unsigned int osd_ext_reg[2][5] = {};
	int osd_ext_info_flag = 0;

	if (NULL != buf) {
		if (strncmp(buf, "val", 3) == 0) {
			osd_ext_info_flag = 0;
		} else if (strncmp(buf, "reg", 3) == 0) {
			osd_ext_info_flag = 1;
		} else {
			osd_ext_info_flag = 2;
		}
		osddev_ext_get_osd_ext_info(fb_info->node, osd_ext_val, osd_ext_reg, osd_ext_info_flag);
	} else {
		amlog_level(LOG_LEVEL_HIGH, "set store_osd_info *buf* is NULL error\n");
	}

	if (osd_ext_info_flag == 0) {
		amlog_level(LOG_LEVEL_HIGH, "pandata[%d].x_start :  %d\n"
		            "pandata[%d].x_end   :  %d\n"
		            "pandata[%d].y_start :  %d\n"
		            "pandata[%d].y_end   :  %d\n",
		            fb_info->node, osd_ext_val[0][0], fb_info->node,
		            osd_ext_val[0][1], fb_info->node, osd_ext_val[0][2],
		            fb_info->node, osd_ext_val[0][3]);
		amlog_level(LOG_LEVEL_HIGH, "dispdata[%d].x_start :  %d\n"
		            "dispdata[%d].x_end   :  %d\n"
		            "dispdata[%d].y_start :  %d\n"
		            "dispdata[%d].y_end   :  %d\n",
		            fb_info->node, osd_ext_val[1][0], fb_info->node,
		            osd_ext_val[1][1], fb_info->node, osd_ext_val[1][2],
		            fb_info->node, osd_ext_val[1][3]);
		amlog_level(LOG_LEVEL_HIGH, "scaledata[%d].x_start :  %d\n"
		            "scaledata[%d].x_end   :  %d\n"
		            "scaledata[%d].y_start :  %d\n"
		            "scaledata[%d].y_end   :  %d\n",
		            fb_info->node, osd_ext_val[2][0], fb_info->node,
		            osd_ext_val[2][1], fb_info->node, osd_ext_val[2][2],
		            fb_info->node, osd_ext_val[2][3]);
	} else if (osd_ext_info_flag == 1) {
		amlog_level(LOG_LEVEL_HIGH,  " [0x1a1b] : *0x%x*\n"
		            " [0x1a1c] : *0x%x*\n"
		            " [0x1a1d] : *0x%x*\n"
		            " [0x1a1e] : *0x%x*\n"
		            " [0x1a13] : *0x%x*\n",
		            osd_ext_reg[0][0], osd_ext_reg[0][1], osd_ext_reg[0][2], osd_ext_reg[0][3], osd_ext_reg[0][4]);
		amlog_level(LOG_LEVEL_HIGH, "  [0x1a3b] : *0x%x*\n"
		            "  [0x1a3c] : *0x%x*\n"
		            "  [0x1a3d] : *0x%x*\n"
		            "  [0x1a3e] : *0x%x*\n"
		            "  [0x1a64] : *0x%x*\n",
		            osd_ext_reg[1][0], osd_ext_reg[1][1], osd_ext_reg[1][2], osd_ext_reg[1][3], osd_ext_reg[1][4]);
	} else if (osd_ext_info_flag == 2) {
		amlog_level(LOG_LEVEL_HIGH, "set store_osd_info osd_ext_info_flag is error\n");
	}

	return count;
}

static ssize_t store_order(struct device *device, struct device_attribute *attr,
                           const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	struct myfb_dev *fbdev = (struct myfb_dev *)fb_info->par;
	int err;

	fbdev->order = simple_strtoul(buf, NULL, 0);
	if ((err = osd_ext_ioctl(fb_info, FBIOPUT_OSD_ORDER, fbdev->order))) {
		return err;
	}
	return count;
}

static ssize_t show_order(struct device *device, struct device_attribute *attr,
                          char *buf)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	struct myfb_dev *fbdev = (struct myfb_dev *)fb_info->par;
	int err;

	if ((err = osd_ext_ioctl(fb_info, FBIOGET_OSD_ORDER, (unsigned long)&fbdev->order))) {
		return err;
	}
	return snprintf(buf, PAGE_SIZE, "order:[0x%x]\n", fbdev->order);
}

static ssize_t show_block_windows(struct device *device, struct device_attribute *attr,
                                  char *buf)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	u32 wins[8];

	osddev_ext_get_block_windows(fb_info->node, wins);

	return snprintf(buf, PAGE_SIZE, "0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
	                wins[0], wins[1], wins[2], wins[3], wins[4], wins[5], wins[6], wins[7]);
}

static ssize_t store_block_windows(struct device *device, struct device_attribute *attr,
                                   const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	int parsed[8];

	if (likely(parse_para(buf, 8, parsed) == 8)) {
		osddev_ext_set_block_windows(fb_info->node, parsed);
	} else {
		amlog_level(LOG_LEVEL_HIGH, "set block windows error\n");
	}

	return count;
}

static ssize_t show_block_mode(struct device *device, struct device_attribute *attr,
                               char *buf)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	u32 mode;

	osddev_ext_get_block_mode(fb_info->node, &mode);

	return snprintf(buf, PAGE_SIZE, "0x%x\n", mode);
}

static ssize_t store_block_mode(struct device *device, struct device_attribute *attr,
                                const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	u32 mode;

	mode = simple_strtoul(buf, NULL, 0);
	osddev_ext_set_block_mode(fb_info->node, mode);

	return count;
}

static ssize_t store_freescale_mode(struct device *device, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	unsigned int free_scale_mode=0;
	int err;
	free_scale_mode= simple_strtoul(buf, NULL, 0);
	if ((err = osd_ext_ioctl(fb_info,FBIOPUT_OSD_FREE_SCALE_MODE,free_scale_mode)))
		return err;
	return count;
}

static ssize_t show_freescale_mode(struct device *device, struct device_attribute *attr,
			 char *buf)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	unsigned int free_scale_mode=0;

	osddev_ext_get_free_scale_mode(fb_info->node, &free_scale_mode);

	return snprintf(buf, PAGE_SIZE, "free_scale_mode:%s\n",free_scale_mode?"new":"default");
}

static ssize_t show_window_axis(struct device *device, struct device_attribute *attr,
			char *buf)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	int x0, y0, x1, y1;

	osddev_ext_get_window_axis(fb_info->node, &x0, &y0, &x1, &y1);

	return snprintf(buf, PAGE_SIZE, "window axis is [%d %d %d %d]\n", x0, y0, x1, y1);
}

static ssize_t store_window_axis(struct device *device, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	s32 parsed[4];

	if (likely(parse_para(buf, 4, parsed) == 4)) {
		osddev_ext_set_window_axis(fb_info->node, parsed[0], parsed[1], parsed[2], parsed[3]);
	} else {
		amlog_level(LOG_LEVEL_HIGH, "set window axis error\n");
	}

	return count;
}

static ssize_t show_clone(struct device *device, struct device_attribute *attr,
			 char *buf)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	u32 clone;

	osddev_ext_get_clone(fb_info->node, &clone);

	return snprintf(buf, PAGE_SIZE, "osd_ext->clone: [0x%x]\n", clone);
}

static ssize_t store_clone(struct device *device, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	u32 clone;

	clone = simple_strtoul(buf, NULL, 0);
	osddev_ext_set_clone(fb_info->node, clone);

	return count;
}

static ssize_t show_angle(struct device *device, struct device_attribute *attr,
			 char *buf)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	u32 angle;

	osddev_ext_get_angle(fb_info->node, &angle);

	return snprintf(buf, PAGE_SIZE, "osd_ext->angle: [0x%x]\n", angle);
}

static ssize_t store_angle(struct device *device, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	u32 angle;

	angle = simple_strtoul(buf, NULL, 0);
	osddev_ext_set_angle(fb_info->node, angle);

	return count;
}

static ssize_t show_rotate_on(struct device *device, struct device_attribute *attr,
                        char *buf)
{
        struct fb_info *fb_info = dev_get_drvdata(device);
        unsigned int osd_ext_rotate = 0;

        osddev_ext_get_osd_ext_rotate_on(fb_info->node, &osd_ext_rotate);
        return snprintf(buf, PAGE_SIZE, "osd_ext_rotate:[%s]\n", osd_ext_rotate?"ON":"OFF");
}

static ssize_t store_rotate_on(struct device *device, struct device_attribute *attr,
                         const char *buf, size_t count)
{
        struct fb_info *fb_info = dev_get_drvdata(device);
        unsigned int osd_ext_rotate = 0;
        int err;
        osd_ext_rotate = simple_strtoul(buf, NULL, 0);
        if ((err = osd_ext_ioctl(fb_info,FBIOPUT_OSD_ROTATE_ON,osd_ext_rotate)))
                return err;
        return count;
}

static ssize_t show_rotate_angle(struct device *device, struct device_attribute *attr,
                        char *buf)
{
        struct fb_info *fb_info = dev_get_drvdata(device);
        unsigned int osd_ext_rotate_angle = 0;

        osddev_ext_get_osd_ext_rotate_angle(fb_info->node, &osd_ext_rotate_angle);
        return snprintf(buf, PAGE_SIZE, "osd_ext_rotate:%d\n", osd_ext_rotate_angle);
}

static ssize_t store_rotate_angle(struct device *device, struct device_attribute *attr,
                         const char *buf, size_t count)
{
        struct fb_info *fb_info = dev_get_drvdata(device);
        unsigned int osd_ext_rotate_angle = 0;
        int err;
        osd_ext_rotate_angle = simple_strtoul(buf, NULL, 0);
        if ((err = osd_ext_ioctl(fb_info,FBIOPUT_OSD_ROTATE_ANGLE,osd_ext_rotate_angle)))
                return err;
        return count;
}

static ssize_t show_prot_canvas(struct device *device, struct device_attribute *attr,
			char *buf)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	int x_start, y_start, x_end, y_end;

	osddev_ext_get_prot_canvas(fb_info->node, &x_start, &y_start, &x_end, &y_end);

	return snprintf(buf, PAGE_SIZE, "%d %d %d %d\n", x_start, y_start, x_end, y_end);
}

static ssize_t store_prot_canvas(struct device *device, struct device_attribute *attr,
                         const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	int parsed[4];

	if (likely(parse_para(buf, 4, parsed) == 4)) {
		osddev_ext_set_prot_canvas(fb_info->node, parsed[0], parsed[1], parsed[2], parsed[3]);
	} else {
		amlog_level(LOG_LEVEL_HIGH, "set prot canvas error\n");
	}

	return count;
}
static struct device_attribute osd_ext_attrs[] = {
	__ATTR(scale, S_IRUGO | S_IWUSR, show_scale, store_scale),
	__ATTR(order, S_IRUGO | S_IWUSR, show_order, store_order),
	__ATTR(enable_3d, S_IRUGO | S_IWUSR, show_enable_3d, store_enable_3d),
	__ATTR(preblend_enable, S_IRUGO | S_IWUSR, show_preblend_enable, store_preblend_enable),
	__ATTR(free_scale, S_IRUGO | S_IWUSR | S_IWGRP, show_free_scale, store_free_scale),
	__ATTR(scale_axis, S_IRUGO|S_IWUSR, show_scale_axis, store_scale_axis),
	__ATTR(scale_width, S_IRUGO | S_IWUSR, show_scale_width, store_scale_width),
	__ATTR(scale_height, S_IRUGO | S_IWUSR, show_scale_height, store_scale_height),
	__ATTR(color_key, S_IRUGO | S_IWUSR, show_color_key, store_color_key),
	__ATTR(enable_key, S_IRUGO | S_IWUSR, show_enable_key, store_enable_key),
	__ATTR(enable_key_onhold, S_IRUGO | S_IWUSR, show_enable_key_onhold, store_enable_key_onhold),
	__ATTR(block_windows, S_IRUGO | S_IWUSR, show_block_windows, store_block_windows),
	__ATTR(block_mode, S_IRUGO | S_IWUSR, show_block_mode, store_block_mode),
	__ATTR(free_scale_axis, S_IRUGO | S_IWUSR, show_free_scale_axis, store_free_scale_axis),
	__ATTR(request2XScale, S_IRUGO|S_IWUSR, show_request_2xscale, store__request_2xscale),
	__ATTR(osd_info_msg, S_IRUGO | S_IWUSR, NULL, store_osd_info),
	__ATTR(video_hole, S_IRUGO | S_IWUSR, show_video_hole, store__video_hole),
	__ATTR(window_axis, S_IRUGO|S_IWUSR, show_window_axis, store_window_axis),
	__ATTR(freescale_mode, S_IRUGO|S_IWUSR, show_freescale_mode, store_freescale_mode),
	__ATTR(clone, S_IRUGO|S_IWUSR|S_IWGRP, show_clone, store_clone),
	__ATTR(angle, S_IRUGO|S_IWUSR, show_angle, store_angle),
	__ATTR(prot_on, S_IRUGO|S_IWUSR|S_IWGRP, show_rotate_on, store_rotate_on),
	__ATTR(prot_angle, S_IRUGO|S_IWUSR, show_rotate_angle, store_rotate_angle),
	__ATTR(prot_canvas, S_IRUGO|S_IWUSR, show_prot_canvas, store_prot_canvas),
};

#ifdef CONFIG_PM
static int osd_ext_suspend(struct platform_device *pdev, pm_message_t state)
{
#ifdef CONFIG_HAS_EARLYSUSPEND
	if (early_suspend_flag)
		return 0;
#endif
	osddev_ext_suspend();
	return 0;
}

static int osd_ext_resume(struct platform_device * dev)
{
#ifdef CONFIG_SCREEN_ON_EARLY
	if (early_resume_flag) {
		early_resume_flag = 0;
		return 0;
	}
#endif
#ifdef CONFIG_HAS_EARLYSUSPEND
    if (early_suspend_flag)
        return 0;
#endif
	osddev_ext_resume();
	return 0;
}
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
static void osd_ext_early_suspend(struct early_suspend *h)
{
	if (early_suspend_flag) {
		return;
	}
	osddev_ext_suspend();
	early_suspend_flag = 1;
}

static void osd_ext_late_resume(struct early_suspend *h)
{
	if (!early_suspend_flag) {
		return;
	}
	early_suspend_flag = 0;
	osddev_ext_resume();
}
#endif

#ifdef CONFIG_SCREEN_ON_EARLY
void osd_ext_resume_early(void)
{
#ifdef CONFIG_HAS_EARLYSUSPEND
	osddev_ext_resume();
	early_suspend_flag = 0;
#endif
	early_resume_flag = 1;
	return ;
}
EXPORT_SYMBOL(osd_ext_resume_early);
#endif

static struct resource memobj;
static int 
osd_ext_probe(struct platform_device *pdev)
{
	int r,ret;
	struct fb_info *fbi = NULL;
	const vinfo_t *vinfo;
	struct fb_var_screeninfo *var;
	struct fb_fix_screeninfo *fix;
	struct resource *mem;
	int  index, Bpp;
	logo_object_t  *init_logo_obj = NULL;
	int  logo_osd_ext_index = 0, i;
	myfb_dev_t 	*fbdev = NULL;
	int osd_ext_memory = 0;	//0:don't need osd_ext memory,1:need osd_ext memory

	vout2_register_client(&osd_ext_notifier_nb);

	if (NULL == init_logo_obj) {
		set_current_vmode2(VMODE_INIT_NULL);
		//set_current_vmode2(VMODE_LCD);
		osddev_ext_init();
	}
	vinfo = get_current_vinfo2();

	ret = of_property_read_u32_array(pdev->dev.of_node, "need-memory", &osd_ext_memory, 1);
	if(ret){
		printk("don't find need osd_ext memory from mesonfb_ext-dts\n");
	}

	for (index = 0; index < OSD_COUNT; index++) {
		//platform resource
#if 0
		if (!(mem = platform_get_resource(pdev, IORESOURCE_MEM, index))) {
			amlog_level(LOG_LEVEL_HIGH, "No frame buffer memory define.\n");
			r = -EFAULT;
			goto failed2;
		}

		//if we have no resource then no need to create this device.
		amlog_level(LOG_LEVEL_HIGH, "[osd%d] 0x%x-0x%x\n", index+2, mem->start, mem->end);
		if (!mem || mem->start == 0 || mem->end == 0 || mem->start == mem->end) {
			continue ;
		}
#else
		if(osd_ext_memory){
			mem = &memobj;
			ret = find_reserve_block(pdev->dev.of_node->name,index);
			if(ret < 0){
				amlog_level(LOG_LEVEL_HIGH,"can not find %s%d reserve block\n",pdev->dev.of_node->name,index);
				r = -EFAULT;
				goto failed2;
			}
			mem->start = (phys_addr_t)get_reserve_block_addr(ret);
			mem->end = mem->start+ (phys_addr_t)get_reserve_block_size(ret)-1;
		}
#endif
		fbi = framebuffer_alloc(sizeof(struct myfb_dev), &pdev->dev);
		if (!fbi) {
			r = -ENOMEM;
			goto failed1;
		}

		fbdev = (struct myfb_dev *)fbi->par;
		fbdev->fb_info = fbi;
		fbdev->dev = pdev;

		mutex_init(&fbdev->lock);

		var = &fbi->var;
		fix = &fbi->fix;

		gp_fbdev_list[index] = fbdev;

		if(osd_ext_memory){
			fbdev->fb_mem_paddr = mem->start;
			fbdev->fb_len = mem->end - mem->start + 1;
			fbdev->fb_mem_vaddr = ioremap_wc(fbdev->fb_mem_paddr, fbdev->fb_len);

			if (!fbdev->fb_mem_vaddr) {
				amlog_level(LOG_LEVEL_HIGH, "failed to ioremap framebuffer\n");
				r = -ENOMEM;
				goto failed1;
			}
		}else{
			fbdev->fb_mem_paddr = 0;		//osd_ext don't need memory
		}

		//clear framebuffer memory
		amlog_level(LOG_LEVEL_HIGH, "Frame buffer memory assigned at phy:0x%08x, vir:0x%p, size=%dK\n",
		            fbdev->fb_mem_paddr, fbdev->fb_mem_vaddr, fbdev->fb_len >> 10);

		if (init_logo_obj && index == logo_osd_ext_index) { //adjust default var info
			int  bpp = init_logo_obj->dev->output_dev.osd.color_depth; //bytes per pixel

			mydef_var[index].xres = init_logo_obj->dev->vinfo->width;
			mydef_var[index].yres = init_logo_obj->dev->vinfo->height;
			mydef_var[index].xres_virtual = init_logo_obj->dev->vinfo->width;
			mydef_var[index].yres_virtual = init_logo_obj->dev->vinfo->height << 1; //logo always use double buffer
			mydef_var[index].bits_per_pixel = bpp ;
			amlog_level(LOG_LEVEL_HIGH, "init fbdev bpp is :%d\r\n", mydef_var[index].bits_per_pixel);

			if (mydef_var[index].bits_per_pixel > 32) {
				mydef_var[index].bits_per_pixel = 32;
			}
		} else {
			if (index == OSD0){
				ret = of_property_read_u32_array(pdev->dev.of_node, "display_size_default", &var_screeninfo[0], 5);
				if(ret){
					printk("don't find to display_size_default from mesonfb_ext-dts\n");
				}
				else {
					int  bpp=var_screeninfo[4];//bytes per pixel
					mydef_var[index].xres=var_screeninfo[0];
					mydef_var[index].yres=var_screeninfo[1];
					mydef_var[index].xres_virtual=var_screeninfo[2];
					mydef_var[index].yres_virtual=var_screeninfo[3];//logo always use double buffer
					mydef_var[index].bits_per_pixel=bpp;

					amlog_level(LOG_LEVEL_HIGH,"init fbdev bpp is :%d\r\n",mydef_var[index].bits_per_pixel);
					if(mydef_var[index].bits_per_pixel>32)
					{
						mydef_var[index].bits_per_pixel=32;
					}
				}
			}
#if 0 //avoid clearing the mem of uboot osd1 logo
			amlog_level(LOG_LEVEL_HIGH, "---------------clear framebuffer%d memory  \r\n", index + 2);
			memset((char*)fbdev->fb_mem_vaddr, 0x80, fbdev->fb_len);
#endif
		}

		_fbdev_set_default(fbdev, index);
		if (NULL == fbdev->color) {
			amlog_level(LOG_LEVEL_HIGH, "fbdev->color NULL\n");
			r = -ENOENT;
			goto failed1;
		}

		if(osd_ext_memory){
			Bpp = (fbdev->color->color_index > 8 ? (fbdev->color->color_index > 16 ? (fbdev->color->color_index > 24 ? 4 : 3) : 2) : 1);
			fix->line_length = var->xres_virtual * Bpp;
			fix->smem_start = fbdev->fb_mem_paddr;
			fix->smem_len = fbdev->fb_len;
		}

		if (fb_alloc_cmap(&fbi->cmap, 16, 0) != 0) {
			amlog_level(LOG_LEVEL_HIGH, "unable to allocate color map memory\n");
			r = -ENOMEM;
			goto failed2;
		}

		if (!(fbi->pseudo_palette = kmalloc(sizeof(u32) * 16, GFP_KERNEL))) {
			amlog_level(LOG_LEVEL_HIGH, "unable to allocate pseudo palette memory\n");
			r = -ENOMEM;
			goto failed2;
		}
		memset(fbi->pseudo_palette, 0, sizeof(u32) * 16);

		fbi->fbops = &osd_ext_ops;

		if(osd_ext_memory){
			fbi->screen_base = (char __iomem *)fbdev->fb_mem_vaddr ;
			fbi->screen_size = fix->smem_len;
		}

		set_default_display_axis(&fbdev->fb_info->var, &fbdev->osd_ext_ctl, vinfo);
		osd_ext_check_var(var, fbi);
		register_framebuffer(fbi);
		if (NULL == init_logo_obj) { //if we have init a logo object ,then no need to setup hardware .
			osddev_ext_set(fbdev);
		}
		for (i = 0; i < ARRAY_SIZE(osd_ext_attrs); i++) {
			ret=device_create_file(fbi->dev, &osd_ext_attrs[i]);
		}
	}

	index = 0;

#ifdef CONFIG_HAS_EARLYSUSPEND
	early_suspend.level = EARLY_SUSPEND_LEVEL_STOP_DRAWING;
	early_suspend.suspend = osd_ext_early_suspend;
	early_suspend.resume = osd_ext_late_resume;
	register_early_suspend(&early_suspend);
#endif

	amlog_level(LOG_LEVEL_HIGH, "osd-ext probe ok  \r\n");
	return 0;

failed2:
	fb_dealloc_cmap(&fbi->cmap);
failed1:
	amlog_level(LOG_LEVEL_HIGH, "Driver module insert failed.\n");
	return r;
}

static int
osd_ext_remove(struct platform_device *pdev)
{
	struct fb_info *fbi;
	int i = 0;

	amlog_level(LOG_LEVEL_HIGH, "osd_ext_remove.\n");
	if (!pdev) {
		return -ENODEV;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&early_suspend);
#endif

	vout_unregister_client(&osd_ext_notifier_nb);

	for (i = 0; i < OSD_COUNT; i++) {
		int j;
		if (gp_fbdev_list[i]) {
			myfb_dev_t * fbdev = gp_fbdev_list[i];

			fbi = fbdev->fb_info;
			for (j = 0; j < ARRAY_SIZE(osd_ext_attrs); j++) {
				device_remove_file(fbi->dev, &osd_ext_attrs[j]);
			}
			iounmap(fbdev->fb_mem_vaddr);
			kfree(fbi->pseudo_palette);
			fb_dealloc_cmap(&fbi->cmap);
			unregister_framebuffer(fbi);
			framebuffer_release(fbi);
		}
	}
	return 0;
}

#ifndef MODULE
/* Process kernel command line parameters */
static int __init
osd_ext_setup_attribute(char *options)
{
	char *this_opt = NULL;
	int r = 0;

	if (!options || !*options) {
		goto exit;
	}

	while (!r && (this_opt = strsep(&options, ",")) != NULL) {
		if (!strncmp(this_opt, "xres:", 5)) {
			mydef_var[0].xres = mydef_var[0].xres_virtual = simple_strtoul(this_opt + 5, NULL, 0);
		} else if (!strncmp(this_opt, "yres:", 5)) {
			mydef_var[0].yres = mydef_var[0].yres_virtual = simple_strtoul(this_opt + 5, NULL, 0);
		} else {
			amlog_level(LOG_LEVEL_HIGH, "invalid option\n");
			r = -1;
		}
	}
exit:
	return r;
}
#endif

/****************************************/
static const struct of_device_id meson_fbext_dt_match[]={
	{	.compatible 	= "amlogic,mesonfb-ext",
	},
	{},
};

static struct platform_driver
osd_ext_driver = {
	.probe      = osd_ext_probe,
	.remove     = osd_ext_remove,
#ifdef CONFIG_PM
	.suspend    = osd_ext_suspend,
	.resume     = osd_ext_resume,
#endif
	.driver     = {
		.name   = "mesonfb-ext",
		.of_match_table=meson_fbext_dt_match,
	}
};

static int __init
osd_ext_init_module(void)
{
	amlog_level(LOG_LEVEL_HIGH, "osd_ext_init\n");
#ifndef MODULE
	{
		char *option;
		if (fb_get_options("mesonfb-ext", &option)) {
			return -ENODEV;
		}

		osd_ext_setup_attribute(option);
	}
#endif

	if (platform_driver_register(&osd_ext_driver)) {
		amlog_level(LOG_LEVEL_HIGH, "failed to register osd ext driver\n");
		return -ENODEV;
	}

	return 0;
}

static void __exit
osd_ext_remove_module(void)
{
	amlog_level(LOG_LEVEL_HIGH, "osd_ext_remove_module.\n");

	platform_driver_unregister(&osd_ext_driver);
}

/****************************************/
module_init(osd_ext_init_module);
module_exit(osd_ext_remove_module);

MODULE_DESCRIPTION("AMLOGIC framebuffer driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tim Yao <timyao@amlogic.com>");
