/*
 * drivers/video/rockchip/rk_fb.c
 *
 * Copyright (C) ROCKCHIP, Inc.
 *Author:yxj<yxj@rock-chips.com>
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kthread.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <asm/div64.h>
#include <linux/uaccess.h>
#include <linux/rk_fb.h>
#include <linux/linux_logo.h>
#include <linux/dma-mapping.h>


#if defined(CONFIG_RK_HDMI)
#include "hdmi/rk_hdmi.h"
#endif

#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <video/of_display_timing.h>
#include <video/display_timing.h>
#include <dt-bindings/rkfb/rk_fb.h>
#endif

#if defined(CONFIG_ION_ROCKCHIP)
#include "../../staging/android/ion/ion.h"
#endif


static int hdmi_switch_complete;
static struct platform_device *fb_pdev;


#if defined(CONFIG_FB_MIRRORING)
int (*video_data_to_mirroring)(struct fb_info *info, u32 yuv_phy[2]);
EXPORT_SYMBOL(video_data_to_mirroring);
#endif




/* rk display power control parse from dts
 *
*/
int rk_disp_pwr_ctr_parse_dt(struct rk_lcdc_driver *dev_drv)
{
	struct device_node *root  = of_parse_phandle(dev_drv->dev->of_node,
				"power_ctr", 0);
	struct device_node *child;
	struct rk_disp_pwr_ctr_list *pwr_ctr;
	struct list_head *pos;
	enum of_gpio_flags flags;
	u32 val = 0;
	u32 debug = 0;
	int ret;

	INIT_LIST_HEAD(&dev_drv->pwrlist_head);
	if (!root) {
		dev_err(dev_drv->dev, "can't find power_ctr node for lcdc%d\n",dev_drv->id);
		return -ENODEV;
	}

	for_each_child_of_node(root, child) {
		pwr_ctr = kmalloc(sizeof(struct rk_disp_pwr_ctr_list), GFP_KERNEL);
		strcpy(pwr_ctr->pwr_ctr.name, child->name);
		if (!of_property_read_u32(child, "rockchip,power_type", &val)) {
			if (val == GPIO) {
				pwr_ctr->pwr_ctr.type = GPIO;
				pwr_ctr->pwr_ctr.gpio = of_get_gpio_flags(child, 0, &flags);
				if (!gpio_is_valid(pwr_ctr->pwr_ctr.gpio)) {
					dev_err(dev_drv->dev, "%s ivalid gpio\n", child->name);
					return -EINVAL;
				}
				pwr_ctr->pwr_ctr.atv_val = flags & OF_GPIO_ACTIVE_LOW;
				ret = gpio_request(pwr_ctr->pwr_ctr.gpio,child->name);
				if (ret) {
					dev_err(dev_drv->dev, "request %s gpio fail:%d\n",
						child->name,ret);
				}

			} else {
				pwr_ctr->pwr_ctr.type = REGULATOR;

			}
		};
		of_property_read_u32(child, "rockchip,delay", &val);
		pwr_ctr->pwr_ctr.delay = val;
		list_add_tail(&pwr_ctr->list, &dev_drv->pwrlist_head);
	}

	of_property_read_u32(root, "rockchip,debug", &debug);

	if (debug) {
		list_for_each(pos, &dev_drv->pwrlist_head) {
			pwr_ctr = list_entry(pos, struct rk_disp_pwr_ctr_list, list);
			printk(KERN_INFO "pwr_ctr_name:%s\n"
					 "pwr_type:%s\n"
					 "gpio:%d\n"
					 "atv_val:%d\n"
					 "delay:%d\n\n",
					 pwr_ctr->pwr_ctr.name,
					 (pwr_ctr->pwr_ctr.type == GPIO) ? "gpio" : "regulator",
					 pwr_ctr->pwr_ctr.gpio,
					 pwr_ctr->pwr_ctr.atv_val,
					 pwr_ctr->pwr_ctr.delay);
		}
	}

	return 0;

}

int rk_disp_pwr_enable(struct rk_lcdc_driver *dev_drv)
{
	struct list_head *pos;
	struct rk_disp_pwr_ctr_list *pwr_ctr_list;
	struct pwr_ctr *pwr_ctr;
	if (list_empty(&dev_drv->pwrlist_head))
		return 0;
	list_for_each(pos, &dev_drv->pwrlist_head) {
		pwr_ctr_list = list_entry(pos, struct rk_disp_pwr_ctr_list, list);
		pwr_ctr = &pwr_ctr_list->pwr_ctr;
		if (pwr_ctr->type == GPIO) {
			gpio_direction_output(pwr_ctr->gpio,pwr_ctr->atv_val);
			mdelay(pwr_ctr->delay);
		}
	}

	return 0;
}

int rk_disp_pwr_disable(struct rk_lcdc_driver *dev_drv)
{
	struct list_head *pos;
	struct rk_disp_pwr_ctr_list *pwr_ctr_list;
	struct pwr_ctr *pwr_ctr;
	if (list_empty(&dev_drv->pwrlist_head))
		return 0;
	list_for_each(pos, &dev_drv->pwrlist_head) {
		pwr_ctr_list = list_entry(pos, struct rk_disp_pwr_ctr_list, list);
		pwr_ctr = &pwr_ctr_list->pwr_ctr;
		if (pwr_ctr->type == GPIO) {
			gpio_set_value(pwr_ctr->gpio,pwr_ctr->atv_val);
		}
	}

	return 0;
}


int rk_disp_prase_timing_dt(struct rk_lcdc_driver *dev_drv)
{
	struct display_timings *disp_timing;
	struct display_timing *dt;
	struct rk_screen *screen = dev_drv->cur_screen;
	disp_timing = of_get_display_timings(dev_drv->dev->of_node);
	if (!disp_timing) {
		dev_err(dev_drv->dev, "parse display timing err\n");
		return -EINVAL;
	}
	dt = display_timings_get(disp_timing, 0);

	screen->mode.pixclock = dt->pixelclock.typ;
	screen->mode.left_margin = dt->hback_porch.typ;
	screen->mode.right_margin = dt->hfront_porch.typ;
	screen->mode.xres = dt->hactive.typ;
	screen->mode.hsync_len = dt->hsync_len.typ;
	screen->mode.upper_margin = dt->vback_porch.typ;
	screen->mode.lower_margin = dt->vfront_porch.typ;
	screen->mode.yres = dt->vactive.typ;
	screen->mode.vsync_len = dt->vsync_len.typ;
	printk(KERN_DEBUG "dclk:%d\n"
			 "hactive:%d\n"
			 "hback_porch:%d\n"
			 "hfront_porch:%d\n"
			 "hsync_len:%d\n"
			 "vactive:%d\n"
			 "vback_porch:%d\n"
			 "vfront_porch:%d\n"
			 "vsync_len:%d\n",
			dt->pixelclock.typ,
			dt->hactive.typ,
			dt->hback_porch.typ,
			dt->hfront_porch.typ,
			dt->hsync_len.typ,
			dt->vactive.typ,
			dt->vback_porch.typ,
			dt->vfront_porch.typ,
			dt->vsync_len.typ);
	return 0;

}

int  rk_fb_calc_fps(struct rk_screen * screen, u32 pixclock)
{
	int x, y;
	unsigned long long hz;
	if (!screen) {
		printk(KERN_ERR "%s:null screen!\n", __func__);
		return 0;
	}
	x = screen->mode.xres + screen->mode.left_margin + screen->mode.right_margin +
	    screen->mode.hsync_len;
	y = screen->mode.yres + screen->mode.upper_margin + screen->mode.lower_margin +
	    screen->mode.vsync_len;

	hz = 1000000000000ULL;	/* 1e12 picoseconds per second */

	hz += (x * y) / 2;
	do_div(hz, x * y);	/* divide by x * y with rounding */

	hz += pixclock / 2;
	do_div(hz, pixclock);	/* divide by pixclock with rounding */

	return hz;
}

char *get_format_string(enum data_format format, char *fmt)
{
	if (!fmt)
		return NULL;
	switch (format) {
	case ARGB888:
		strcpy(fmt, "ARGB888");
		break;
	case RGB888:
		strcpy(fmt, "RGB888");
		break;
	case RGB565:
		strcpy(fmt, "RGB565");
		break;
	case YUV420:
		strcpy(fmt, "YUV420");
		break;
	case YUV422:
		strcpy(fmt, "YUV422");
		break;
	case YUV444:
		strcpy(fmt, "YUV444");
		break;
	case XRGB888:
		strcpy(fmt, "XRGB888");
		break;
	case XBGR888:
		strcpy(fmt, "XBGR888");
		break;
	case ABGR888:
		strcpy(fmt, "XBGR888");
		break;
	default:
		strcpy(fmt, "invalid");
		break;
	}

	return fmt;

}



/**********************************************************************
this is for hdmi
name: lcdc device name ,lcdc0 , lcdc1
***********************************************************************/
struct rk_lcdc_driver *rk_get_lcdc_drv(char *name)
{
	struct rk_fb *inf =  platform_get_drvdata(fb_pdev);
	int i = 0;
	for (i = 0; i < inf->num_lcdc; i++) {
		if (!strcmp(inf->lcdc_dev_drv[i]->name, name))
			break;
	}
	return inf->lcdc_dev_drv[i];

}

static struct rk_lcdc_driver *rk_get_prmry_lcdc_drv(void)
{
	struct rk_fb *inf = NULL;
	struct rk_lcdc_driver *dev_drv = NULL;
	int i = 0;

	if (likely(fb_pdev))
		inf = platform_get_drvdata(fb_pdev);
	else
		return NULL;

	for (i = 0; i < inf->num_lcdc; i++) {
		if (inf->lcdc_dev_drv[i]->screen_ctr_info->prop ==  PRMRY) {
			dev_drv = inf->lcdc_dev_drv[i];
			break;
		}
	}

	return dev_drv;
}


int rk_fb_get_prmry_screen_ft(void)
{
	struct rk_lcdc_driver *dev_drv = rk_get_prmry_lcdc_drv();
	uint32_t htotal, vtotal, pix_total, ft_us, dclk_mhz;

	if (unlikely(!dev_drv))
		return 0;

	dclk_mhz = dev_drv->pixclock/(1000*1000);

	htotal = (dev_drv->cur_screen->mode.upper_margin + dev_drv->cur_screen->mode.lower_margin +
		dev_drv->cur_screen->mode.yres + dev_drv->cur_screen->mode.vsync_len);
	vtotal = (dev_drv->cur_screen->mode.left_margin + dev_drv->cur_screen->mode.right_margin +
		dev_drv->cur_screen->mode.xres + dev_drv->cur_screen->mode.hsync_len);
	pix_total = htotal*vtotal;
	ft_us = pix_total / dclk_mhz;

	return ft_us;
}

static struct rk_lcdc_driver  *rk_get_extend_lcdc_drv(void)
{
	struct rk_fb *inf = NULL;
	struct rk_lcdc_driver *dev_drv = NULL;
	int i = 0;

	if (likely(fb_pdev))
		inf = platform_get_drvdata(fb_pdev);
	else
		return NULL;

	for (i = 0; i < inf->num_lcdc; i++) {
		if (inf->lcdc_dev_drv[i]->screen_ctr_info->prop == EXTEND) {
			dev_drv = inf->lcdc_dev_drv[i];
			break;
		}
	}

	return dev_drv;
}

struct rk_screen *rk_fb_get_prmry_screen(void)
{
	struct rk_lcdc_driver *dev_drv = rk_get_prmry_lcdc_drv();
	return dev_drv->screen0;

}

u32 rk_fb_get_prmry_screen_pixclock(void)
{
	struct rk_lcdc_driver *dev_drv = rk_get_prmry_lcdc_drv();
	return dev_drv->pixclock;
}

int rk_fb_poll_prmry_screen_vblank(void)
{
	struct rk_lcdc_driver *dev_drv = rk_get_prmry_lcdc_drv();
	if (likely(dev_drv)) {
		if (dev_drv->ops->poll_vblank)
			return dev_drv->ops->poll_vblank(dev_drv);
		else
			return RK_LF_STATUS_NC;
	} else
		return RK_LF_STATUS_NC;
}

bool rk_fb_poll_wait_frame_complete(void)
{
	uint32_t timeout = RK_LF_MAX_TIMEOUT;
	if (rk_fb_poll_prmry_screen_vblank() == RK_LF_STATUS_NC)
		return false;

	while (!(rk_fb_poll_prmry_screen_vblank() == RK_LF_STATUS_FR)  &&  --timeout);
	while (!(rk_fb_poll_prmry_screen_vblank() == RK_LF_STATUS_FC)  &&  --timeout);

	return true;
}
static int rk_fb_open(struct fb_info *info, int user)
{
	struct rk_lcdc_driver *dev_drv = (struct rk_lcdc_driver *)info->par;
	int win_id;

	win_id = dev_drv->ops->fb_get_win_id(dev_drv, info->fix.id);
	if (dev_drv->win[win_id]->state)
		return 0;    /* if this win aready opened ,no need to reopen*/
	else
		dev_drv->ops->open(dev_drv, win_id, 1);
	return 0;
}

static int  get_extend_fb_id(struct fb_info *info)
{
	int fb_id = 0;
	char *id = info->fix.id;
	struct rk_lcdc_driver *dev_drv = (struct rk_lcdc_driver *)info->par;
	if (!strcmp(id, "fb0"))
		fb_id = 0;
	else if (!strcmp(id, "fb1"))
		fb_id = 1;
	else if (!strcmp(id, "fb2") && (dev_drv->num_win > 2))
		fb_id = 2;
	return fb_id;
}

static int rk_fb_close(struct fb_info *info, int user)
{
	struct rk_lcdc_driver *dev_drv = (struct rk_lcdc_driver *)info->par;
	struct rk_lcdc_win *win = NULL;
	int win_id = dev_drv->ops->fb_get_win_id(dev_drv, info->fix.id);
	if (win_id >= 0) {
		win = dev_drv->win[win_id];
		info->fix.smem_start = win->reserved;

		info->var.xres = dev_drv->screen0->mode.xres;
		info->var.yres = dev_drv->screen0->mode.yres;
		info->var.grayscale |= (info->var.xres<<8) + (info->var.yres<<20);
#if defined(CONFIG_LOGO_LINUX_BMP)
		info->var.bits_per_pixel = 32;
#else
		info->var.bits_per_pixel = 16;
#endif
		info->fix.line_length  = (info->var.xres)*(info->var.bits_per_pixel>>3);
		info->var.xres_virtual = info->var.xres;
		info->var.yres_virtual = info->var.yres;
		info->var.width =  dev_drv->screen0->width;
		info->var.height = dev_drv->screen0->height;
		info->var.pixclock = dev_drv->pixclock;
		info->var.left_margin = dev_drv->screen0->mode.left_margin;
		info->var.right_margin = dev_drv->screen0->mode.right_margin;
		info->var.upper_margin = dev_drv->screen0->mode.upper_margin;
		info->var.lower_margin = dev_drv->screen0->mode.lower_margin;
		info->var.vsync_len = dev_drv->screen0->mode.vsync_len;
		info->var.hsync_len = dev_drv->screen0->mode.hsync_len;
	}

	return 0;
}


#if defined(CONFIG_RK29_IPP)
static int get_ipp_format(int fmt)
{
	int ipp_fmt = IPP_XRGB_8888;
	switch (fmt) {
	case HAL_PIXEL_FORMAT_RGBX_8888:
	case HAL_PIXEL_FORMAT_RGBA_8888:
	case HAL_PIXEL_FORMAT_BGRA_8888:
	case HAL_PIXEL_FORMAT_RGB_888:
		ipp_fmt = IPP_XRGB_8888;
		break;
	case HAL_PIXEL_FORMAT_RGB_565:
		ipp_fmt = IPP_RGB_565;
		break;
	case HAL_PIXEL_FORMAT_YCbCr_422_SP:
		ipp_fmt = IPP_Y_CBCR_H2V1;
		break;
	case HAL_PIXEL_FORMAT_YCrCb_NV12:
		ipp_fmt = IPP_Y_CBCR_H2V2;
		break;
	case HAL_PIXEL_FORMAT_YCrCb_444:
		ipp_fmt = IPP_Y_CBCR_H1V1;
		break;
	default:
		ipp_fmt = IPP_IMGTYPE_LIMIT;
		break;
	}

	return ipp_fmt;
}

static void ipp_win_check(int *dst_w, int *dst_h, int *dst_vir_w,
				int rotation, int fmt)
{
	int align16 = 2;
	int align64 = 8;


	if (fmt == IPP_XRGB_8888) {
		align16 = 1;
		align64 = 2;
	} else if (fmt == IPP_RGB_565) {
		align16 = 1;
		align64 = 4;
	} else {
		align16 = 2;
		align64 = 8;
	}
	align16 -= 1;  /*for YUV, 1*/
	align64 -= 1;  /*for YUV, 7*/

	if (rotation == IPP_ROT_0) {
		if (fmt > IPP_RGB_565) {
			if ((*dst_w & 1) != 0)
				*dst_w = *dst_w+1;
			if ((*dst_h & 1) != 0)
				*dst_h = *dst_h+1;
			if (*dst_vir_w < *dst_w)
				*dst_vir_w = *dst_w;
		}
	} else {

		if ((*dst_w & align64) != 0)
			*dst_w = (*dst_w+align64)&(~align64);
		if ((fmt > IPP_RGB_565) && ((*dst_h & 1) == 1))
			*dst_h = *dst_h+1;
		if (*dst_vir_w < *dst_w)
			*dst_vir_w = *dst_w;
	}

}

static void fb_copy_by_ipp(struct fb_info *dst_info,
		struct fb_info *src_info, int offset)
{
	struct rk29_ipp_req ipp_req;
	uint32_t  rotation = 0;
	int dst_w, dst_h, dst_vir_w;
	int ipp_fmt;
	u8 data_format = (dst_info->var.nonstd)&0xff;

	memset(&ipp_req, 0, sizeof(struct rk29_ipp_req));
#if defined(CONFIG_FB_ROTATE)
	int orientation = 270 - CONFIG_ROTATE_ORIENTATION;
	switch (orientation) {
	case 0:
		rotation = IPP_ROT_0;
		break;
	case 90:
		rotation = IPP_ROT_90;
		break;
	case 180:
		rotation = IPP_ROT_180;
		break;
	case 270:
		rotation = IPP_ROT_270;
		break;
	default:
		rotation = IPP_ROT_270;
		break;

	}
#endif

	dst_w = dst_info->var.xres;
	dst_h = dst_info->var.yres;
	dst_vir_w = dst_info->var.xres_virtual;
	ipp_fmt = get_ipp_format(data_format);
	ipp_win_check(&dst_w, &dst_h, &dst_vir_w, rotation, ipp_fmt);
	ipp_req.src0.YrgbMst = src_info->fix.smem_start + offset;
	ipp_req.src0.w = src_info->var.xres;
	ipp_req.src0.h = src_info->var.yres;
	ipp_req.src_vir_w = src_info->var.xres_virtual;
	ipp_req.src0.fmt = ipp_fmt;

	ipp_req.dst0.YrgbMst = dst_info->fix.smem_start + offset;
	ipp_req.dst0.w = dst_w;
	ipp_req.dst0.h = dst_h;
	ipp_req.dst_vir_w = dst_vir_w;
	ipp_req.dst0.fmt = ipp_fmt;


	ipp_req.timeout = 100;
	ipp_req.flag = rotation;
	ipp_blit_sync(&ipp_req);

}

#endif


static int rk_fb_rotate(struct fb_info *dst_info,
				struct fb_info *src_info, int offset)
{
	#if defined(CONFIG_RK29_IPP)
		fb_copy_by_ipp(dst_info, src_info, offset);
	#else
		return -1;
	#endif
		return 0;
}
static int rk_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct rk_fb *rk_fb = dev_get_drvdata(info->device);
	struct rk_lcdc_driver *dev_drv  = (struct rk_lcdc_driver *)info->par;
	struct fb_fix_screeninfo *fix = &info->fix;
	struct fb_info *extend_info = NULL;
	struct rk_lcdc_driver *extend_dev_drv = NULL;
	int win_id = 0, extend_win_id = 0;
	struct rk_lcdc_win *extend_win = NULL;
	struct rk_lcdc_win *win = NULL;
	int fb_id = 0;


	u32 xoffset = var->xoffset;
	u32 yoffset = var->yoffset;
	u32 xvir = var->xres_virtual;
	u8 data_format = var->nonstd&0xff;

	win_id = dev_drv->ops->fb_get_win_id(dev_drv, info->fix.id);
	if (win_id < 0)
		return  -ENODEV;
	else
		win = dev_drv->win[win_id];

	if (rk_fb->disp_mode == DUAL) {
		fb_id = get_extend_fb_id(info);
		extend_info = rk_fb->fb[(rk_fb->num_fb>>1) + fb_id];
		extend_dev_drv  = (struct rk_lcdc_driver *)extend_info->par;
		extend_win_id = dev_drv->ops->fb_get_win_id(extend_dev_drv,
						extend_info->fix.id);
		extend_win = extend_dev_drv->win[extend_win_id];
	}

	switch (win->format) {
	case XBGR888:
	case ARGB888:
	case ABGR888:
		win->y_offset = (yoffset*xvir + xoffset)*4;
		break;
	case  RGB888:
		win->y_offset = (yoffset*xvir + xoffset)*3;
		break;
	case RGB565:
		win->y_offset = (yoffset*xvir + xoffset)*2;
		break;
	case  YUV422:
		win->y_offset = yoffset*xvir + xoffset;
		win->c_offset = win->y_offset;
		break;
	case  YUV420:
		win->y_offset = yoffset*xvir + xoffset;
		win->c_offset = (yoffset>>1)*xvir + xoffset;
		break;
	case  YUV444:
		win->y_offset = yoffset*xvir + xoffset;
		win->c_offset = yoffset*2*xvir + (xoffset<<1);
		break;
	default:
		printk(KERN_ERR "%s un supported format:0x%x\n",
			__func__, data_format);
		return -EINVAL;
	}
	win->smem_start = fix->smem_start;
	win->cbr_start = fix->mmio_start;
	dev_drv->ops->pan_display(dev_drv, win_id);
	if (rk_fb->disp_mode == DUAL) {
		if (extend_win->state && (hdmi_switch_complete)) {
			extend_win->y_offset = win->y_offset;
		#if defined(CONFIG_FB_ROTATE) || !defined(CONFIG_THREE_FB_BUFFER)
			rk_fb_rotate(extend_info, info, win->y_offset);
		#endif
			extend_dev_drv->ops->pan_display(extend_dev_drv, extend_win_id);
		}
	}

	#ifdef	CONFIG_FB_MIRRORING
	if (video_data_to_mirroring)
		video_data_to_mirroring(info, NULL);
	#endif
	return 0;
}

static int rk_fb_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg)
{
	struct rk_fb *rk_fb = dev_get_drvdata(info->device);
	struct rk_lcdc_driver *dev_drv = (struct rk_lcdc_driver *)info->par;
	struct fb_fix_screeninfo *fix = &info->fix;
	int fb_id = 0, extend_win_id = 0;
	struct fb_info *extend_info = NULL;
	struct rk_lcdc_driver *extend_dev_drv = NULL;
	struct rk_lcdc_win *extend_win = NULL;
	struct rk_lcdc_win *win;
	int enable; /* enable fb:1 enable;0 disable*/
	int ovl;   /*overlay:0 win1 on the top of win0;1,win0 on the top of win1*/
	int num_buf; /*buffer_number*/
	int ret;

	int  win_id = dev_drv->ops->fb_get_win_id(dev_drv, info->fix.id);
	
	void __user *argp = (void __user *)arg;

	win = dev_drv->win[win_id];
	if (rk_fb->disp_mode == DUAL) {
		fb_id = get_extend_fb_id(info);
		extend_info = rk_fb->fb[(rk_fb->num_fb>>1) + fb_id];
		extend_dev_drv  = (struct rk_lcdc_driver *)extend_info->par;
		extend_win_id = dev_drv->ops->fb_get_win_id(extend_dev_drv,
						extend_info->fix.id);
		extend_win = extend_dev_drv->win[extend_win_id];
	}

	switch (cmd) {
	case RK_FBIOSET_YUV_ADDR:
	{
		u32 yuv_phy[2];
		if (copy_from_user(yuv_phy, argp, 8))
			return -EFAULT;
		fix->smem_start = yuv_phy[0];
		fix->mmio_start = yuv_phy[1];
		break;
	}
	case RK_FBIOSET_ENABLE:
		if (copy_from_user(&enable, argp, sizeof(enable)))
			return -EFAULT;
		dev_drv->ops->open(dev_drv, win_id, enable);
		break;
	case RK_FBIOGET_ENABLE:
		enable = dev_drv->ops->get_win_state(dev_drv, win_id);
		if (copy_to_user(argp, &enable, sizeof(enable)))
			return -EFAULT;
		break;
	case RK_FBIOSET_OVERLAY_STATE:
		if (copy_from_user(&ovl, argp, sizeof(ovl)))
			return -EFAULT;
		dev_drv->ops->ovl_mgr(dev_drv, ovl, 1);
		break;
	case RK_FBIOGET_OVERLAY_STATE:
		ovl = dev_drv->ops->ovl_mgr(dev_drv, 0, 0);
		if (copy_to_user(argp, &ovl, sizeof(ovl)))
			return -EFAULT;
		break;
	case RK_FBIOPUT_NUM_BUFFERS:
		if (copy_from_user(&num_buf, argp, sizeof(num_buf)))
			return -EFAULT;
		dev_drv->num_buf = num_buf;
		break;
	case RK_FBIOSET_VSYNC_ENABLE:
		if (copy_from_user(&enable, argp, sizeof(enable)))
			return -EFAULT;
		dev_drv->vsync_info.active = enable;
		break;
#if defined(CONFIG_ION_ROCKCHIP)
	case RK_FBIOSET_DMABUF_FD:
	{
		int usr_fd;
		struct ion_handle *hdl;
		ion_phys_addr_t phy_addr;
		size_t len;
		if (copy_from_user(&usr_fd, argp, sizeof(usr_fd)))
			return -EFAULT;
		hdl = ion_import_dma_buf(rk_fb->ion_client, usr_fd);
		ion_phys(rk_fb->ion_client, hdl, &phy_addr, &len);
		fix->smem_start = phy_addr;
		break;
	}
	case RK_FBIOGET_DMABUF_FD:
	{
		int fd = win->dma_buf_fd;
		if (copy_to_user(argp, &fd, sizeof(fd)));
			return -EFAULT;
		break;
	}
#endif
	case RK_FBIOSET_CONFIG_DONE:
		ret = copy_from_user(&(dev_drv->wait_fs), argp, sizeof(dev_drv->wait_fs));
		if (dev_drv->ops->lcdc_reg_update)
			dev_drv->ops->lcdc_reg_update(dev_drv);
		if (rk_fb->disp_mode == DUAL) {
			if (extend_win->state && (hdmi_switch_complete)) {
				if (rk_fb->num_fb >= 2) {
					if (extend_dev_drv->ops->lcdc_reg_update)
						extend_dev_drv->ops->lcdc_reg_update(extend_dev_drv);
				}
			}
		}
		break;
	default:
		dev_drv->ops->ioctl(dev_drv, cmd, arg, win_id);
		break;
	}
	return 0;
}

static int rk_fb_blank(int blank_mode, struct fb_info *info)
{
	struct rk_lcdc_driver *dev_drv = (struct rk_lcdc_driver *)info->par;
	struct fb_fix_screeninfo *fix = &info->fix;
	int win_id;
	struct rk_fb *rk_fb = dev_get_drvdata(info->device);

	win_id = dev_drv->ops->fb_get_win_id(dev_drv, fix->id);
	if (win_id < 0)
		return  -ENODEV;
#if defined(CONFIG_RK_HDMI)
	if ((rk_fb->disp_mode == ONE_DUAL) && (hdmi_get_hotplug() == HDMI_HPD_ACTIVED)) {
		printk(KERN_INFO "hdmi is connect , not blank lcdc\n");
	} else
#endif
	{
		dev_drv->ops->blank(dev_drv, win_id, blank_mode);
	}
	return 0;
}

static int rk_fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{

	if ((0 == var->xres_virtual) || (0 == var->yres_virtual) ||
		(0 == var->xres) || (0 == var->yres) || (var->xres < 16) ||
		((16 != var->bits_per_pixel) && (32 != var->bits_per_pixel))) {
		dev_err(info->dev, "%s check var fail 1:\n"
				"xres_vir:%d>>yres_vir:%d\n"
				"xres:%d>>yres:%d\n"
				"bits_per_pixel:%d\n",
				info->fix.id,
				var->xres_virtual,
				var->yres_virtual,
				var->xres,
				var->yres,
				var->bits_per_pixel);
		return -EINVAL;
	}

	if (((var->xoffset+var->xres) > var->xres_virtual) ||
		((var->yoffset+var->yres) > (var->yres_virtual))) {
		dev_err(info->dev, "%s check_var fail 2:\n"
				"xoffset:%d>>xres:%d>>xres_vir:%d\n"
				"yoffset:%d>>yres:%d>>yres_vir:%d\n",
				info->fix.id,
				var->xoffset,
				var->xres,
				var->xres_virtual,
				var->yoffset,
				var->yres,
				var->yres_virtual);
		return -EINVAL;
	}

	return 0;
}

static ssize_t rk_fb_read(struct fb_info *info, char __user *buf,
			   size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;
	u8 *buffer, *dst;
	u8 __iomem *src;
	int c, cnt = 0, err = 0;
	unsigned long total_size;
	struct rk_lcdc_driver *dev_drv = (struct rk_lcdc_driver *)info->par;
	struct rk_lcdc_win *win = NULL;
	int win_id = 0;

	win_id = dev_drv->ops->fb_get_win_id(dev_drv, info->fix.id);
	if (win_id < 0)
		return  -ENODEV;
	else
		win = dev_drv->win[win_id];

	if (win->format == RGB565)
		total_size = win->xact*win->yact<<1; /*only read the current frame buffer*/
	else
		total_size = win->xact*win->yact<<2;


	if (p >= total_size)
		return 0;

	if (count >= total_size)
		count = total_size;

	if (count + p > total_size)
		count = total_size - p;

	buffer = kmalloc((count > PAGE_SIZE) ? PAGE_SIZE : count,
			 GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	src = (u8 __iomem *) (info->screen_base + p + win->y_offset);

	while (count) {
		c  = (count > PAGE_SIZE) ? PAGE_SIZE : count;
		dst = buffer;
		fb_memcpy_fromfb(dst, src, c);
		dst += c;
		src += c;

		if (copy_to_user(buf, buffer, c)) {
			err = -EFAULT;
			break;
		}
		*ppos += c;
		buf += c;
		cnt += c;
		count -= c;
	}

	kfree(buffer);

	return (err) ? err : cnt;
}

static ssize_t rk_fb_write(struct fb_info *info, const char __user *buf,
			    size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;
	u8 *buffer, *src;
	u8 __iomem *dst;
	int c, cnt = 0, err = 0;
	unsigned long total_size;
	struct rk_lcdc_driver *dev_drv = (struct rk_lcdc_driver *)info->par;
	struct rk_lcdc_win *win = NULL;
	int win_id = 0;

	win_id = dev_drv->ops->fb_get_win_id(dev_drv, info->fix.id);
	if (win_id < 0)
		return  -ENODEV;
	else
		win = dev_drv->win[win_id];

	if (win->format == RGB565)
		total_size = win->xact*win->yact<<1; /*write the current frame buffer*/
	else
		total_size = win->xact*win->yact<<2;

	if (p > total_size)
		return -EFBIG;

	if (count > total_size) {
		err = -EFBIG;
		count = total_size;
	}

	if (count + p > total_size) {
		if (!err)
			err = -ENOSPC;

		count = total_size - p;
	}

	buffer = kmalloc((count > PAGE_SIZE) ? PAGE_SIZE : count,
			 GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	dst = (u8 __iomem *) (info->screen_base + p + win->y_offset);

	while (count) {
		c = (count > PAGE_SIZE) ? PAGE_SIZE : count;
		src = buffer;

		if (copy_from_user(src, buf, c)) {
			err = -EFAULT;
			break;
		}

		fb_memcpy_tofb(dst, src, c);
		dst += c;
		src += c;
		*ppos += c;
		buf += c;
		cnt += c;
		count -= c;
	}

	kfree(buffer);

	return (cnt) ? cnt : err;

}

static int rk_fb_set_par(struct fb_info *info)
{
	struct fb_var_screeninfo *var = &info->var;
	struct fb_fix_screeninfo *fix = &info->fix;
	struct rk_lcdc_driver *dev_drv = (struct rk_lcdc_driver *)info->par;
	struct rk_fb *rk_fb = dev_get_drvdata(info->device);
	int fb_id, extend_win_id = 0;
	struct fb_info *extend_info = NULL;
	struct rk_lcdc_driver *extend_dev_drv = NULL;
	struct rk_lcdc_win *extend_win = NULL;
	struct rk_lcdc_win *win = NULL;
	struct rk_screen *screen = dev_drv->cur_screen;
	int win_id = 0;
	u32 cblen = 0, crlen = 0;
	u16 xsize = 0, ysize = 0;                 /*winx display window height/width --->LCDC_WINx_DSP_INFO*/
	u32 xoffset = var->xoffset;		/* offset from virtual to visible*/
	u32 yoffset = var->yoffset;
	u16 xpos = (var->nonstd>>8) & 0xfff;   /*visiable pos in panel*/
	u16 ypos = (var->nonstd>>20) & 0xfff;
	u32 xvir = var->xres_virtual;
	u32 yvir = var->yres_virtual;
	u8 data_format = var->nonstd&0xff;



	var->pixclock = dev_drv->pixclock;
	win_id = dev_drv->ops->fb_get_win_id(dev_drv, info->fix.id);
	if (win_id < 0)
		return  -ENODEV;
	else
		win = dev_drv->win[win_id];

	if (rk_fb->disp_mode == DUAL) {
		fb_id = get_extend_fb_id(info);
		extend_info = rk_fb->fb[(rk_fb->num_fb>>1) + fb_id];
		extend_dev_drv  = (struct rk_lcdc_driver *)extend_info->par;
		extend_win_id = dev_drv->ops->fb_get_win_id(extend_dev_drv,
					extend_info->fix.id);
		extend_win = extend_dev_drv->win[extend_win_id];
	}
	if (var->grayscale>>8) { /*if the application has specific the horizontal and vertical display size*/
		xsize = (var->grayscale>>8) & 0xfff;
		ysize = (var->grayscale>>20) & 0xfff;
	} else  { /*ohterwise  full  screen display*/
		xsize = screen->mode.xres;
		ysize = screen->mode.yres;
	}

/*this is for device like rk2928 ,whic have one lcdc but two display outputs*/
/*save winameter set by android*/
if (rk_fb->disp_mode != DUAL) {
	if (screen->screen_id == 0) {

		dev_drv->screen0->xsize = xsize;
		dev_drv->screen0->ysize = ysize;
		dev_drv->screen0->xpos  = xpos;
		dev_drv->screen0->ypos = ypos;
	} else {
		xsize = dev_drv->screen1->xsize;
		ysize = dev_drv->screen1->ysize;
		xpos = dev_drv->screen1->xpos;
		ypos = dev_drv->screen1->ypos;
	}
}

#if 1
	switch (data_format) {
	case HAL_PIXEL_FORMAT_RGBX_8888:
		win->format = XBGR888;
		fix->line_length = 4 * xvir;
		win->y_offset = (yoffset*xvir + xoffset)*4;
		break;
	case HAL_PIXEL_FORMAT_RGBA_8888:
		win->format = ABGR888;
		fix->line_length = 4 * xvir;
		win->y_offset = (yoffset*xvir + xoffset)*4;
		break;
	case HAL_PIXEL_FORMAT_BGRA_8888:
		win->format = ARGB888;
		fix->line_length = 4 * xvir;
		win->y_offset = (yoffset*xvir + xoffset)*4;
		break;
	case HAL_PIXEL_FORMAT_RGB_888:
		win->format = RGB888;
		fix->line_length = 3 * xvir;
		win->y_offset = (yoffset*xvir + xoffset)*3;
		break;
	case HAL_PIXEL_FORMAT_RGB_565:
		win->format = RGB565;
		fix->line_length = 2 * xvir;
		win->y_offset = (yoffset*xvir + xoffset)*2;
		break;
	case HAL_PIXEL_FORMAT_YCbCr_422_SP:
		win->format = YUV422;
		fix->line_length = xvir;
		cblen = crlen = (xvir*yvir)>>1;
		win->y_offset = yoffset*xvir + xoffset;
		win->c_offset = win->y_offset;
		break;
	case HAL_PIXEL_FORMAT_YCrCb_NV12:
		win->format = YUV420;
		fix->line_length = xvir;
		cblen = crlen = (xvir*yvir)>>2;
		win->y_offset = yoffset*xvir + xoffset;
		win->c_offset = (yoffset>>1)*xvir + xoffset;
		break;
	case HAL_PIXEL_FORMAT_YCrCb_444:
		win->format = 5;
		fix->line_length = xvir<<2;
		win->y_offset = yoffset*xvir + xoffset;
		win->c_offset = yoffset*2*xvir + (xoffset<<1);
		cblen = crlen = (xvir*yvir);
		break;
	default:
		printk(KERN_ERR "%s:un supported format:0x%x\n", __func__, data_format);
		return -EINVAL;
	}
#else
	switch (var->bits_per_pixel) {
	case 32:
		win->format = ARGB888;
		fix->line_length = 4 * xvir;
		win->y_offset = (yoffset*xvir + xoffset)*4;
		break;
	case 16:
		win->format = RGB565;
		fix->line_length = 2 * xvir;
		win->y_offset = (yoffset*xvir + xoffset)*2;
		break;

	}
#endif

	win->xpos = xpos;
	win->ypos = ypos;
	win->xsize = xsize;
	win->ysize = ysize;

	win->smem_start = fix->smem_start;
	win->cbr_start = fix->mmio_start;
	win->xact = var->xres;              /*winx active window height,is a wint of vir*/
	win->yact = var->yres;
	win->xvir =  var->xres_virtual;	   /*virtual resolution	 stride --->LCDC_WINx_VIR*/
	win->yvir =  var->yres_virtual;
	if (rk_fb->disp_mode == DUAL) {
		if (extend_win->state && (hdmi_switch_complete)) {
			if (info != extend_info) {
				if (win->xact < win->yact) {
					extend_win->xact = win->yact;
					extend_win->yact = win->xact;
					extend_win->xvir = win->yact;
					extend_info->var.xres = var->yres;
					extend_info->var.yres = var->xres;
					extend_info->var.xres_virtual = var->yres;
				} else {
					extend_win->xact = win->xact;
					extend_win->yact = win->yact;
					extend_win->xvir = win->xvir;
					extend_info->var.xres = var->xres;
					extend_info->var.yres = var->yres;
					extend_info->var.xres_virtual = var->xres_virtual;
				}
				extend_win->format = win->format;
				extend_info->var.nonstd &= 0xffffff00;
				extend_info->var.nonstd |= data_format;
				extend_dev_drv->ops->set_par(extend_dev_drv, extend_win_id);
			}
		}
	}
	dev_drv->ops->set_par(dev_drv, win_id);

	return 0;
}

static inline unsigned int chan_to_field(unsigned int chan,
					 struct fb_bitfield *bf)
{
	chan &= 0xffff;
	chan >>= 16 - bf->length;
	return chan << bf->offset;
}

static int fb_setcolreg(unsigned regno,
			       unsigned red, unsigned green, unsigned blue,
			       unsigned transp, struct fb_info *info)
{
	unsigned int val;

	switch (info->fix.visual) {
	case FB_VISUAL_TRUECOLOR:
		/* true-colour, use pseudo-palette */
		if (regno < 16) {
			u32 *pal = info->pseudo_palette;
			val  = chan_to_field(red,   &info->var.red);
			val |= chan_to_field(green, &info->var.green);
			val |= chan_to_field(blue,  &info->var.blue);
			pal[regno] = val;
		}
		break;
	default:
		return -1;	/* unknown type */
	}

	return 0;
}

static struct fb_ops fb_ops = {
	.owner          = THIS_MODULE,
	.fb_open        = rk_fb_open,
	.fb_release     = rk_fb_close,
	.fb_check_var   = rk_fb_check_var,
	.fb_set_par     = rk_fb_set_par,
	.fb_blank       = rk_fb_blank,
	.fb_ioctl       = rk_fb_ioctl,
	.fb_pan_display = rk_pan_display,
	.fb_read        = rk_fb_read,
	.fb_write       = rk_fb_write,
	.fb_setcolreg   = fb_setcolreg,
	.fb_fillrect    = cfb_fillrect,
	.fb_copyarea    = cfb_copyarea,
	.fb_imageblit   = cfb_imageblit,
};



static struct fb_var_screeninfo def_var = {
#if defined(CONFIG_LOGO_LINUX_BMP)
	.red		= {16, 8, 0},
	.green		= {8, 8, 0},
	.blue		= {0, 8, 0},
	.transp		= {0, 0, 0},
	.nonstd		= HAL_PIXEL_FORMAT_BGRA_8888,
#else
	.red		= {11, 5, 0},
	.green		= {5, 6, 0},
	.blue		= {0, 5, 0},
	.transp		= {0, 0, 0},
	.nonstd		= HAL_PIXEL_FORMAT_RGB_565,   /*(ypos<<20+xpos<<8+format) format*/
#endif
	.grayscale	= 0,  /*(ysize<<20+xsize<<8)*/
	.activate	= FB_ACTIVATE_NOW,
	.accel_flags	= 0,
	.vmode		= FB_VMODE_NONINTERLACED,
};

static struct fb_fix_screeninfo def_fix = {
	.type		 = FB_TYPE_PACKED_PIXELS,
	.type_aux	 = 0,
	.xpanstep	 = 1,
	.ypanstep	 = 1,
	.ywrapstep	 = 0,
	.accel		 = FB_ACCEL_NONE,
	.visual		 = FB_VISUAL_TRUECOLOR,

};


static int rk_fb_wait_for_vsync_thread(void *data)
{
	struct rk_lcdc_driver  *dev_drv = data;
	struct rk_fb *rk_fb =  platform_get_drvdata(fb_pdev);
	struct fb_info *fbi = rk_fb->fb[0];

	while (!kthread_should_stop()) {
		ktime_t timestamp = dev_drv->vsync_info.timestamp;
		int ret = wait_event_interruptible(dev_drv->vsync_info.wait,
			!ktime_equal(timestamp, dev_drv->vsync_info.timestamp) &&
			(dev_drv->vsync_info.active || dev_drv->vsync_info.irq_stop));

		if (!ret)
			sysfs_notify(&fbi->dev->kobj, NULL, "vsync");
	}

	return 0;
}

static ssize_t rk_fb_vsync_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_lcdc_driver *dev_drv = (struct rk_lcdc_driver *)fbi->par;
	return scnprintf(buf, PAGE_SIZE, "%llu\n",
			ktime_to_ns(dev_drv->vsync_info.timestamp));
}

static DEVICE_ATTR(vsync, S_IRUGO, rk_fb_vsync_show, NULL);


/*****************************************************************
this two function is for other module that in the kernel which
need show image directly through fb
fb_id:we have 4 fb here,default we use fb0 for ui display
*******************************************************************/
struct fb_info *rk_get_fb(int fb_id)
{
	struct rk_fb *inf =  platform_get_drvdata(fb_pdev);
	struct fb_info *fb = inf->fb[fb_id];
	return fb;
}
EXPORT_SYMBOL(rk_get_fb);

void rk_direct_fb_show(struct fb_info *fbi)
{
	rk_fb_set_par(fbi);
	rk_pan_display(&fbi->var, fbi);
}
EXPORT_SYMBOL(rk_direct_fb_show);


static int set_xact_yact_for_hdmi(struct fb_var_screeninfo *pmy_var,
					struct fb_var_screeninfo *hdmi_var)
{
	if (pmy_var->xres < pmy_var->yres) {  /*vertical  lcd screen*/
		hdmi_var->xres = pmy_var->yres;
		hdmi_var->yres = pmy_var->xres;
		hdmi_var->xres_virtual = pmy_var->yres;
	} else {
		hdmi_var->xres = pmy_var->xres;
		hdmi_var->yres = pmy_var->yres;
		hdmi_var->xres_virtual = pmy_var->xres_virtual;
	}

	return 0;

}
int rk_fb_dpi_open(bool open)
{
	struct rk_lcdc_driver *dev_drv = NULL;
	dev_drv = rk_get_prmry_lcdc_drv();
	dev_drv->ops->dpi_open(dev_drv, open);

	return 0;
}
int rk_fb_dpi_win_sel(int win_id)
{
	struct rk_lcdc_driver *dev_drv = NULL;
	dev_drv = rk_get_prmry_lcdc_drv();
	dev_drv->ops->dpi_win_sel(dev_drv, win_id);

	return 0;
}
int rk_fb_dpi_status(void)
{
	int ret;
	struct rk_lcdc_driver *dev_drv = NULL;
	dev_drv = rk_get_prmry_lcdc_drv();
	ret = dev_drv->ops->dpi_status(dev_drv);

	return ret;
}

/******************************************
*function:this function will be called by hdmi,when
*             hdmi plug in/out
*screen: the screen attached to hdmi
*enable: 1,hdmi plug in,0,hdmi plug out
*lcdc_id: the lcdc id the hdmi attached ,0 or 1
******************************************/
int rk_fb_switch_screen(struct rk_screen *screen , int enable, int lcdc_id)
{
	struct rk_fb *rk_fb =  platform_get_drvdata(fb_pdev);
	struct fb_info *info = NULL;
	struct rk_lcdc_driver *dev_drv = NULL;
	struct fb_var_screeninfo *hdmi_var    = NULL;
	struct fb_var_screeninfo *pmy_var = NULL;      /*var for primary screen*/
	struct fb_info *pmy_info = NULL;
	struct fb_fix_screeninfo *pmy_fix = NULL;
	int i;
	struct fb_fix_screeninfo *hdmi_fix    = NULL;
	char name[6];
	int ret;
	int win_id;

	if (rk_fb->disp_mode != DUAL)
		rk29_backlight_set(0);

	sprintf(name, "lcdc%d", lcdc_id);

	if (rk_fb->disp_mode != DUAL) {
		dev_drv = rk_fb->lcdc_dev_drv[0];
	} else {

		for (i = 0; i < rk_fb->num_lcdc; i++) {
			if (rk_fb->lcdc_dev_drv[i]->prop == EXTEND) {
				dev_drv = rk_fb->lcdc_dev_drv[i];
				break;
			}
		}

		if (i == rk_fb->num_lcdc) {
			printk(KERN_ERR "%s driver not found!", name);
			return -ENODEV;
		}
	}
	printk("hdmi %s lcdc%d\n", enable ? "connect to" : "remove from", dev_drv->id);

	if (rk_fb->num_lcdc == 1)
		info = rk_fb->fb[0];
	else if (rk_fb->num_lcdc == 2)
		info = rk_fb->fb[dev_drv->num_win]; /*the main fb of lcdc1*/

	if (dev_drv->screen1) { /*device like rk2928 ,have only one lcdc but two outputs*/
		if (enable) {
			memcpy(dev_drv->screen1, screen, sizeof(struct rk_screen));
			dev_drv->screen1->lcdc_id = 0; /*connect screen1 to output interface 0*/
			dev_drv->screen1->screen_id = 1;
			dev_drv->screen0->lcdc_id = 1; /*connect screen0 to output interface 1*/
			dev_drv->cur_screen = dev_drv->screen1;
			dev_drv->screen0->ext_screen = dev_drv->screen1;
			if (dev_drv->screen0->sscreen_get) {
				dev_drv->screen0->sscreen_get(dev_drv->screen0,
					dev_drv->cur_screen->hdmi_resolution);
			}


		} else {
			dev_drv->screen1->lcdc_id = 1; /*connect screen1 to output interface 1*/
			dev_drv->screen0->lcdc_id = 0; /*connect screen0 to output interface 0*/
			dev_drv->cur_screen = dev_drv->screen0;
			dev_drv->screen_ctr_info->set_screen_info(dev_drv->cur_screen,
					dev_drv->screen_ctr_info->lcd_info);
		}
	} else{
		if (enable)
			memcpy(dev_drv->cur_screen, screen, sizeof(struct rk_screen));
	}


	win_id = dev_drv->ops->fb_get_win_id(dev_drv, info->fix.id);

	if (!enable && !dev_drv->screen1) { /*only double lcdc device need to close*/
		if (dev_drv->win[win_id]->state)
			dev_drv->ops->open(dev_drv, win_id, enable); /*disable the win which attached to this fb*/
		hdmi_switch_complete = 0;

		return 0;
	}

	hdmi_var = &info->var;
	hdmi_fix = &info->fix;
	if (rk_fb->disp_mode  == DUAL) {
		if (likely(rk_fb->num_lcdc == 2)) {
			pmy_var = &rk_fb->fb[0]->var;
			pmy_fix = &rk_fb->fb[0]->fix;
			set_xact_yact_for_hdmi(pmy_var, hdmi_var);
			hdmi_var->nonstd &= 0xffffff00;
			hdmi_var->nonstd |= (pmy_var->nonstd & 0xff); /*use the same format as primary screen*/
		} else {
			printk(KERN_WARNING "%s>>only one lcdc,dual display no supported!", __func__);
		}
	}
	hdmi_var->grayscale &= 0xff;
	hdmi_var->grayscale |= (dev_drv->cur_screen->mode.xres<<8) + (dev_drv->cur_screen->mode.yres<<20);
	if (dev_drv->screen1) { /*device like rk2928,whic have one lcdc but two outputs*/
	/*	info->var.nonstd &= 0xff;
		info->var.nonstd |= (dev_drv->cur_screen->mode.xpos<<8) + (dev_drv->cur_screen->mode.ypos<<20);
		info->var.grayscale &= 0xff;
		info->var.grayscale |= (dev_drv->cur_screen->mode.x_res<<8) + (dev_drv->cur_screen->mode.y_res<<20);*/
		dev_drv->screen1->xsize = dev_drv->cur_screen->mode.xres;
		dev_drv->screen1->ysize = dev_drv->cur_screen->mode.yres;
		dev_drv->screen1->xpos = 0;
		dev_drv->screen1->ypos = 0;
	}

	ret = info->fbops->fb_open(info, 1);
	dev_drv->ops->load_screen(dev_drv, 1);
	ret = info->fbops->fb_set_par(info);
	if (dev_drv->ops->lcdc_hdmi_process)
		dev_drv->ops->lcdc_hdmi_process(dev_drv, enable);

	if (rk_fb->disp_mode == DUAL) {
		if (likely(rk_fb->num_lcdc == 2)) {
			pmy_info = rk_fb->fb[0];
			pmy_info->fbops->fb_pan_display(pmy_var, pmy_info);
		} else {
			printk(KERN_WARNING "%s>>only one lcdc,dual display no supported!", __func__);
		}
	} else {
		info->fbops->fb_pan_display(hdmi_var, info);
	}
	info->fbops->fb_ioctl(info, RK_FBIOSET_CONFIG_DONE, 0);
	if (dev_drv->screen1) {
		if (dev_drv->screen0->sscreen_set) {
			dev_drv->ops->blank(dev_drv, 0, FB_BLANK_NORMAL);
			msleep(100);
			dev_drv->screen0->sscreen_set(dev_drv->screen0, enable);
			dev_drv->ops->blank(dev_drv, 0, FB_BLANK_UNBLANK);
		}
	}

	if (rk_fb->disp_mode != DUAL)
		rk29_backlight_set(1);
	hdmi_switch_complete = enable;
	return 0;

}




/******************************************
function:this function current only called by hdmi for
	scale the display
scale_x: scale rate of x resolution
scale_y: scale rate of y resolution
lcdc_id: the lcdc id the hdmi attached ,0 or 1
******************************************/

int rk_fb_disp_scale(u8 scale_x, u8 scale_y, u8 lcdc_id)
{
	struct rk_fb *inf =  platform_get_drvdata(fb_pdev);
	struct fb_info *info = NULL;
	struct fb_var_screeninfo *var = NULL;
	struct rk_lcdc_driver *dev_drv = NULL;
	u16 screen_x, screen_y;
	u16 xpos, ypos;
	u16 xsize, ysize;
	char name[6];
	int i = 0;
	sprintf(name, "lcdc%d", lcdc_id);

#if defined(CONFIG_ONE_LCDC_DUAL_OUTPUT_INF)
	dev_drv = inf->lcdc_dev_drv[0];
#else
	for (i = 0; i < inf->num_lcdc; i++) {
		if (inf->lcdc_dev_drv[i]->screen_ctr_info->prop == EXTEND) {
			dev_drv = inf->lcdc_dev_drv[i];
			break;
		}
	}

	if (i == inf->num_lcdc) {
		printk(KERN_ERR "%s driver not found!", name);
		return -ENODEV;

	}
#endif
	if (inf->num_lcdc == 1)
		info = inf->fb[0];
	else if (inf->num_lcdc == 2)
		info = inf->fb[dev_drv->num_win];

	var = &info->var;
	screen_x = dev_drv->cur_screen->mode.xres;
	screen_y = dev_drv->cur_screen->mode.yres;

#if defined(CONFIG_ONE_LCDC_DUAL_OUTPUT_INF) || defined(CONFIG_NO_DUAL_DISP)
	if (dev_drv->cur_screen->screen_id == 1) {
		dev_drv->cur_screen->xpos = (screen_x-screen_x*scale_x/100)>>1;
		dev_drv->cur_screen->ypos = (screen_y-screen_y*scale_y/100)>>1;
		dev_drv->cur_screen->xsize = screen_x*scale_x/100;
		dev_drv->cur_screen->ysize = screen_y*scale_y/100;
	} else
#endif
	{
		xpos = (screen_x-screen_x*scale_x/100)>>1;
		ypos = (screen_y-screen_y*scale_y/100)>>1;
		xsize = screen_x*scale_x/100;
		ysize = screen_y*scale_y/100;
		var->nonstd &= 0xff;
		var->nonstd |= (xpos<<8) + (ypos<<20);
		var->grayscale &= 0xff;
		var->grayscale |= (xsize<<8) + (ysize<<20);
	}

	info->fbops->fb_set_par(info);
	info->fbops->fb_ioctl(info, RK_FBIOSET_CONFIG_DONE, 0);
	return 0;


}

static int rk_fb_alloc_buffer(struct fb_info *fbi, int fb_id)
{
	struct rk_fb *rk_fb = platform_get_drvdata(fb_pdev);
	struct rk_lcdc_driver *dev_drv = (struct rk_lcdc_driver *)fbi->par;
	struct rk_lcdc_win *win = NULL;
	int win_id;
	int ret = 0;
	unsigned long fb_mem_size;
#if defined(CONFIG_ION_ROCKCHIP)
	struct ion_handle *handle;
	int dma_buf_fd;
	ion_phys_addr_t phy_addr;
	size_t len;
#else
	dma_addr_t fb_mem_phys;
	void *fb_mem_virt;
#endif
	win_id = dev_drv->ops->fb_get_win_id(dev_drv, fbi->fix.id);
	if (win_id < 0)
		return  -ENODEV;
	else
		win = dev_drv->win[win_id];

	if (!strcmp(fbi->fix.id, "fb0")) {
		fb_mem_size = 3 * (fbi->var.xres * fbi->var.yres) << 2;
		fb_mem_size = ALIGN(fb_mem_size, SZ_1M);
#if defined(CONFIG_ION_ROCKCHIP)
		handle = ion_alloc(rk_fb->ion_client, (size_t)fb_mem_size, 0, ION_HEAP(ION_VIDEO_HEAP_ID), 0);
		if (IS_ERR(handle)) {
			dev_err(fbi->device, "failed to ion_alloc:%ld\n",PTR_ERR(handle));
			return -ENOMEM;
		}

		ion_phys(rk_fb->ion_client, handle, &phy_addr, &len);
		fbi->fix.smem_start = phy_addr;
		fbi->fix.smem_len = len;
		fbi->screen_base = ion_map_kernel(rk_fb->ion_client, handle);
		dma_buf_fd = ion_share_dma_buf_fd(rk_fb->ion_client, handle);
		if (dma_buf_fd < 0) {
			dev_err(fbi->dev, "ion_share_dma_buf_fd failed\n");
			return dma_buf_fd;

		}
		win->ion_handle = handle;
		win->dma_buf_fd = dma_buf_fd;
#else

		fb_mem_virt = dma_alloc_writecombine(fbi->dev, fb_mem_size, &fb_mem_phys,
			GFP_KERNEL);
		if (!fb_mem_virt) {
			pr_err("%s: Failed to allocate framebuffer\n", __func__);
			return -ENOMEM;
		}
		fbi->fix.smem_len = fb_mem_size;
		fbi->fix.smem_start = fb_mem_phys;
		fbi->screen_base = fb_mem_virt;
#endif
		memset(fbi->screen_base, 0, fbi->fix.smem_len);
		printk(KERN_INFO "fb%d:phy:%lx>>vir:%p>>len:0x%x\n", fb_id,
		fbi->fix.smem_start, fbi->screen_base, fbi->fix.smem_len);
	} else {
#if defined(CONFIG_FB_ROTATE) || !defined(CONFIG_THREE_FB_BUFFER)
		res = platform_get_resource_byname(fb_pdev,
			IORESOURCE_MEM, "fb2 buf");
		if (res == NULL) {
			dev_err(&fb_pdev->dev, "failed to get win0 memory \n");
			ret = -ENOENT;
		}
		fbi->fix.smem_start = res->start;
		fbi->fix.smem_len = res->end - res->start + 1;
		mem = request_mem_region(res->start, resource_size(res),
			fb_pdev->name);
		fbi->screen_base = ioremap(res->start, fbi->fix.smem_len);
		memset(fbi->screen_base, 0, fbi->fix.smem_len);
#else    /*three buffer no need to copy*/
		fbi->fix.smem_start = rk_fb->fb[0]->fix.smem_start;
		fbi->fix.smem_len   = rk_fb->fb[0]->fix.smem_len;
		fbi->screen_base    = rk_fb->fb[0]->screen_base;
#endif
		printk(KERN_INFO "fb%d:phy:%lx>>vir:%p>>len:0x%x\n", fb_id,
			fbi->fix.smem_start, fbi->screen_base, fbi->fix.smem_len);
	}

	fbi->screen_size = fbi->fix.smem_len;
	win_id = dev_drv->ops->fb_get_win_id(dev_drv, fbi->fix.id);
	if (win_id >= 0) {
		win = dev_drv->win[win_id];
		win->reserved = fbi->fix.smem_start;
	}

	return ret;
}

static int rk_release_fb_buffer(struct fb_info *fbi)
{
	if (!strcmp(fbi->fix.id, "fb1") || !strcmp(fbi->fix.id, "fb3"))  /*buffer for fb1 and fb3 are alloc by android*/
		return 0;
	iounmap(fbi->screen_base);
	release_mem_region(fbi->fix.smem_start, fbi->fix.smem_len);
	return 0;

}
static int init_lcdc_win(struct rk_lcdc_driver *dev_drv, struct rk_lcdc_win *def_win)
{
	int i;
	int num_win = dev_drv->num_win;
	for (i = 0; i < num_win; i++) {
		struct rk_lcdc_win *win = NULL;
		win =  kzalloc(sizeof(struct rk_lcdc_win), GFP_KERNEL);
		if (!win) {
			dev_err(dev_drv->dev, "kzmalloc for win fail!");
			return   -ENOMEM;
		}
		win = &def_win[i];
		strcpy(win->name, def_win->name);
		win->id = def_win->id;
		win->support_3d = def_win->support_3d;
		dev_drv->win[i] = win;
	}

	return 0;
}


static int init_lcdc_device_driver(struct rk_fb *rk_fb,
					struct rk_lcdc_win *def_win, int index)
{
	struct rk_lcdc_driver *dev_drv = rk_fb->lcdc_dev_drv[index];
	struct rk_screen *screen = devm_kzalloc(dev_drv->dev,
				sizeof(struct rk_screen), GFP_KERNEL);
	if (!screen) {
		dev_err(dev_drv->dev, "malloc screen for lcdc%d fail!",
					dev_drv->id);
		return -ENOMEM;
	}
	
	screen->screen_id = 0;
	screen->lcdc_id = dev_drv->id;
	dev_drv->screen0 = screen;
	dev_drv->cur_screen = screen;
	/* devie use one lcdc + rk61x scaler for dual display*/
	if (rk_fb->disp_mode == ONE_DUAL) {
		struct rk_screen *screen1 = devm_kzalloc(dev_drv->dev,
						sizeof(struct rk_screen), GFP_KERNEL);
		if (screen1) {
			dev_err(dev_drv->dev, "malloc screen1 for lcdc%d fail!",
						dev_drv->id);
			return -ENOMEM;
		}
		screen1->screen_id = 1;
		screen1->lcdc_id = 1;
		dev_drv->screen1 = screen1;
	}
	sprintf(dev_drv->name, "lcdc%d", dev_drv->id);
	init_lcdc_win(dev_drv, def_win);
	init_completion(&dev_drv->frame_done);
	spin_lock_init(&dev_drv->cpl_lock);
	mutex_init(&dev_drv->fb_win_id_mutex);
	dev_drv->ops->fb_win_remap(dev_drv, FB_DEFAULT_ORDER);
	dev_drv->first_frame = 1;
	rk_disp_pwr_ctr_parse_dt(dev_drv);
	rk_disp_prase_timing_dt(dev_drv);

	return 0;
}

#ifdef CONFIG_LOGO_LINUX_BMP
static struct linux_logo *bmp_logo;
static int fb_prewine_bmp_logo(struct fb_info *info, int rotate)
{
	bmp_logo = fb_find_logo(24);
	if (bmp_logo == NULL) {
		printk(KERN_INFO "%s error\n", __func__);
		return 0;
	}
	return 1;
}

static void fb_show_bmp_logo(struct fb_info *info, int rotate)
{
	unsigned char *src = bmp_logo->data;
	unsigned char *dst = info->screen_base;
	int i;
	unsigned int Needwidth = (*(src-24)<<8) | (*(src-23));
	unsigned int Needheight = (*(src-22)<<8) | (*(src-21));

	for (i = 0; i < Needheight; i++)
		memcpy(dst+info->var.xres*i*4,
			src+bmp_logo->width*i*4, Needwidth*4);

}
#endif

/********************************
*check if the primary lcdc has registerd,
the primary lcdc mas register first
*********************************/
bool is_prmry_rk_lcdc_registered(void)
{
	struct rk_fb *rk_fb = platform_get_drvdata(fb_pdev);
	if (rk_fb->lcdc_dev_drv[0])
		return  true;
	else
		return false;


}
int rk_fb_register(struct rk_lcdc_driver *dev_drv,
				struct rk_lcdc_win *win, int id)
{
	struct rk_fb *rk_fb = platform_get_drvdata(fb_pdev);
	struct fb_info *fbi;
	int i = 0, ret = 0, index = 0;

	if (rk_fb->num_lcdc == RK30_MAX_LCDC_SUPPORT)
		return -ENXIO;

	for (i = 0; i < RK30_MAX_LCDC_SUPPORT; i++) {
		if (!rk_fb->lcdc_dev_drv[i]) {
			rk_fb->lcdc_dev_drv[i] = dev_drv;
			rk_fb->lcdc_dev_drv[i]->id = id;
			rk_fb->num_lcdc++;
			break;
		}
	}

	index = i;
	init_lcdc_device_driver(rk_fb, win, index);
	dev_drv->fb_index_base = rk_fb->num_fb;
	for (i = 0; i < dev_drv->num_win; i++) {
		fbi = framebuffer_alloc(0, &fb_pdev->dev);
		if (!fbi) {
			dev_err(&fb_pdev->dev, "fb framebuffer_alloc fail!");
			ret = -ENOMEM;
		}
		fbi->par = dev_drv;
		fbi->var = def_var;
		fbi->fix = def_fix;
		sprintf(fbi->fix.id, "fb%d", rk_fb->num_fb);
		fbi->var.xres = dev_drv->cur_screen->mode.xres;
		fbi->var.yres = dev_drv->cur_screen->mode.yres;
		fbi->var.grayscale |= (fbi->var.xres<<8) + (fbi->var.yres<<20);
#if defined(CONFIG_LOGO_LINUX_BMP)
		fbi->var.bits_per_pixel = 32;
#else
		fbi->var.bits_per_pixel = 16;
#endif
		fbi->fix.line_length  = (fbi->var.xres)*(fbi->var.bits_per_pixel>>3);
		fbi->var.xres_virtual = fbi->var.xres;
		fbi->var.yres_virtual = fbi->var.yres;
		fbi->var.width = dev_drv->cur_screen->width;
		fbi->var.height = dev_drv->cur_screen->height;
		fbi->var.pixclock = dev_drv->pixclock;
		fbi->var.left_margin = dev_drv->cur_screen->mode.left_margin;
		fbi->var.right_margin = dev_drv->cur_screen->mode.right_margin;
		fbi->var.upper_margin = dev_drv->cur_screen->mode.upper_margin;
		fbi->var.lower_margin = dev_drv->cur_screen->mode.lower_margin;
		fbi->var.vsync_len = dev_drv->cur_screen->mode.vsync_len;
		fbi->var.hsync_len = dev_drv->cur_screen->mode.hsync_len;
		fbi->fbops = &fb_ops;
		fbi->flags = FBINFO_FLAG_DEFAULT;
		fbi->pseudo_palette = dev_drv->win[i]->pseudo_pal;
		if (i == 0) /* only alloc memory for main fb*/
			rk_fb_alloc_buffer(fbi, rk_fb->num_fb);
		ret = register_framebuffer(fbi);
		if (ret < 0) {
			dev_err(&fb_pdev->dev, "%s fb%d register_framebuffer fail!\n",
					__func__, rk_fb->num_fb);
			return ret;
		}
		rkfb_create_sysfs(fbi);
		rk_fb->fb[rk_fb->num_fb] = fbi;
		dev_info(&fb_pdev->dev, "rockchip framebuffer registerd:%s\n",
					fbi->fix.id);
		rk_fb->num_fb++;

		if (i == 0) {
			init_waitqueue_head(&dev_drv->vsync_info.wait);
			ret = device_create_file(fbi->dev, &dev_attr_vsync);
			if (ret)
				dev_err(fbi->dev, "failed to create vsync file\n");
			dev_drv->vsync_info.thread = kthread_run(rk_fb_wait_for_vsync_thread,
								dev_drv, "fb-vsync");
			if (dev_drv->vsync_info.thread == ERR_PTR(-ENOMEM)) {
				dev_err(fbi->dev, "failed to run vsync thread\n");
				dev_drv->vsync_info.thread = NULL;
			}
			dev_drv->vsync_info.active = 1;
		}

	}

 /*show logo for primary display device*/
#if !defined(CONFIG_FRAMEBUFFER_CONSOLE) && defined(CONFIG_LOGO)
if (dev_drv->prop == PRMRY) {
	struct fb_info *main_fbi = rk_fb->fb[0];
	main_fbi->fbops->fb_open(main_fbi, 1);
	main_fbi->fbops->fb_set_par(main_fbi);
#if  defined(CONFIG_LOGO_LINUX_BMP)
	if (fb_prewine_bmp_logo(main_fbi, FB_ROTATE_UR)) {
		fb_set_cmap(&main_fbi->cmap, main_fbi);
		fb_show_bmp_logo(main_fbi, FB_ROTATE_UR);
		main_fbi->fbops->fb_pan_display(&main_fbi->var, main_fbi);
	}
#else
	if (fb_prepare_logo(main_fbi, FB_ROTATE_UR)) {
		fb_set_cmap(&main_fbi->cmap, main_fbi);
		fb_show_logo(main_fbi, FB_ROTATE_UR);
		main_fbi->fbops->fb_pan_display(&main_fbi->var, main_fbi);
	}
#endif
	main_fbi->fbops->fb_ioctl(main_fbi, RK_FBIOSET_CONFIG_DONE, 0);
}
#endif
	return 0;


}

int rk_fb_unregister(struct rk_lcdc_driver *dev_drv)
{

	struct rk_fb *fb_inf = platform_get_drvdata(fb_pdev);
	struct fb_info *fbi;
	int fb_index_base = dev_drv->fb_index_base;
	int fb_num = dev_drv->num_win;
	int i = 0;

	if (fb_inf->lcdc_dev_drv[i]->vsync_info.thread) {
		fb_inf->lcdc_dev_drv[i]->vsync_info.irq_stop = 1;
		kthread_stop(fb_inf->lcdc_dev_drv[i]->vsync_info.thread);
	}

	for (i = 0; i < fb_num; i++)
		kfree(dev_drv->win[i]);

	for (i = fb_index_base; i < (fb_index_base+fb_num); i++) {
		fbi = fb_inf->fb[i];
		unregister_framebuffer(fbi);
		/*rk_release_fb_buffer(fbi);*/
		framebuffer_release(fbi);
	}
	fb_inf->lcdc_dev_drv[dev_drv->id] = NULL;
	fb_inf->num_lcdc--;

	return 0;
}



#if defined(CONFIG_HAS_EARLYSUSPEND)
struct suspend_info {
	struct early_suspend early_suspend;
	struct rk_fb *inf;
};

static void rkfb_early_suspend(struct early_suspend *h)
{
	struct suspend_info *info = container_of(h, struct suspend_info,
						early_suspend);
	struct rk_fb *inf = info->inf;
	int i;
	for (i = 0; i < inf->num_lcdc; i++) {
		if (!inf->lcdc_dev_drv[i])
			continue;
		inf->lcdc_dev_drv[i]->suspend(inf->lcdc_dev_drv[i]);
	}
}
static void rkfb_early_resume(struct early_suspend *h)
{
	struct suspend_info *info = container_of(h, struct suspend_info,
						early_suspend);
	struct rk_fb *inf = info->inf;
	int i;
	for (i = 0; i < inf->num_lcdc; i++) {
		if (!inf->lcdc_dev_drv[i])
			continue;
		inf->lcdc_dev_drv[i]->resume(inf->lcdc_dev_drv[i]);
	}

}



static struct suspend_info suspend_info = {
	.early_suspend.suspend = rkfb_early_suspend,
	.early_suspend.resume = rkfb_early_resume,
	.early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB,
};
#endif

static int rk_fb_probe(struct platform_device *pdev)
{
	struct rk_fb *rk_fb = NULL;
	struct device_node *np = pdev->dev.of_node;
	u32 mode;

	if (!np) {
		dev_err(&pdev->dev, "Missing device tree node.\n");
		return -EINVAL;
	}

	rk_fb = devm_kzalloc(&pdev->dev, sizeof(struct rk_fb), GFP_KERNEL);
	if (!rk_fb) {
		dev_err(&pdev->dev, "kmalloc for rk fb fail!");
		return  -ENOMEM;
	}
	platform_set_drvdata(pdev, rk_fb);

	if (!of_property_read_u32(np, "rockchip,disp-mode", &mode)) {
		rk_fb->disp_mode = mode;

	} else {
		dev_err(&pdev->dev, "no disp-mode node found!");
		return -ENODEV;
	}
	dev_set_name(&pdev->dev, "rockchip-fb");
#if defined(CONFIG_ION_ROCKCHIP)
	rk_fb->ion_client = rockchip_ion_client_create("rk_fb");
	if (IS_ERR(rk_fb->ion_client)) {
		dev_err(&pdev->dev, "failed to create ion client for rk fb");
		return PTR_ERR(rk_fb->ion_client);
	} else {
		dev_info(&pdev->dev, "rk fb ion client create success!\n");
	}
#endif

#if defined(CONFIG_HAS_EARLYSUSPEND)
	suspend_info.inf = rk_fb;
	register_early_suspend(&suspend_info.early_suspend);
#endif
	fb_pdev = pdev;
	dev_info(&pdev->dev, "rockchip framebuffer driver probe\n");
	return 0;
}

static int rk_fb_remove(struct platform_device *pdev)
{
	struct rk_fb *rk_fb = platform_get_drvdata(pdev);
	kfree(rk_fb);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static void rk_fb_shutdown(struct platform_device *pdev)
{
	struct rk_fb *rk_fb = platform_get_drvdata(pdev);
	int i;
	for (i = 0; i < rk_fb->num_lcdc; i++) {
		if (!rk_fb->lcdc_dev_drv[i])
			continue;

	}

#if	defined(CONFIG_HAS_EARLYSUSPEND)
	unregister_early_suspend(&suspend_info.early_suspend);
#endif
}


static const struct of_device_id rkfb_dt_ids[] = {
	{ .compatible = "rockchip,rk-fb", },
	{}
};

static struct platform_driver rk_fb_driver = {
	.probe		= rk_fb_probe,
	.remove		= rk_fb_remove,
	.driver		= {
		.name	= "rk-fb",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(rkfb_dt_ids),
	},
	.shutdown   = rk_fb_shutdown,
};

static int __init rk_fb_init(void)
{
	return platform_driver_register(&rk_fb_driver);
}

static void __exit rk_fb_exit(void)
{
	platform_driver_unregister(&rk_fb_driver);
}

subsys_initcall_sync(rk_fb_init);
module_exit(rk_fb_exit);
