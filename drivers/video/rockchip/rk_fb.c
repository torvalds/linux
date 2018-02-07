/*
 * drivers/video/rockchip/rk_fb.c
 *
 * Copyright (C) ROCKCHIP, Inc.
 * Author:yxj<yxj@rock-chips.com>
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
#include <linux/vmalloc.h>
#include <asm/div64.h>
#include <linux/uaccess.h>
#include <linux/rk_fb.h>
#include <linux/linux_logo.h>
#include <linux/dma-mapping.h>
#include <linux/regulator/consumer.h>
#include <linux/of_address.h>
#include <linux/memblock.h>

#include "bmp_helper.h"

#if defined(CONFIG_RK_HDMI)
#include "hdmi/rockchip-hdmi.h"
#endif

#if defined(CONFIG_ROCKCHIP_RGA) || defined(CONFIG_ROCKCHIP_RGA2)
#include "rga/rga.h"
#endif

#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <video/of_display_timing.h>
#include <video/display_timing.h>
#endif

#if defined(CONFIG_ION_ROCKCHIP)
#include <linux/rockchip_ion.h>
#include <linux/rockchip-iovmm.h>
#include <linux/dma-buf.h>
#include <linux/highmem.h>
#endif

#define H_USE_FENCE 1
/* #define FB_ROATE_BY_KERNEL 1 */

static int hdmi_switch_state;
static struct platform_device *fb_pdev;

#if defined(CONFIG_FB_MIRRORING)
int (*video_data_to_mirroring)(struct fb_info *info, u32 yuv_phy[2]);
EXPORT_SYMBOL(video_data_to_mirroring);
#endif

extern phys_addr_t uboot_logo_base;
extern phys_addr_t uboot_logo_size;
extern phys_addr_t uboot_logo_offset;
static struct rk_fb_trsm_ops *trsm_lvds_ops;
static struct rk_fb_trsm_ops *trsm_edp_ops;
static struct rk_fb_trsm_ops *trsm_mipi_ops;
static int uboot_logo_on;

static int rk_fb_debug_lvl;
static int rk_fb_iommu_debug;
module_param(rk_fb_debug_lvl, int, S_IRUGO | S_IWUSR);
module_param(rk_fb_iommu_debug, int, S_IRUGO | S_IWUSR);

#define rk_fb_dbg(level, x...) do {		\
	if (unlikely(rk_fb_debug_lvl >= level))	\
		pr_info(x);			\
	} while (0)
static int rk_fb_config_debug(struct rk_lcdc_driver *dev_drv,
			      struct rk_fb_win_cfg_data *win_data,
			      struct rk_fb_reg_data *regs, u32 cmd);
static int car_reversing;

static int is_car_camcap(void) {
	return car_reversing && strcmp("camcap", current->comm);
}

int support_uboot_display(void)
{
	return uboot_logo_on;
}

int rk_fb_get_display_policy(void)
{
	struct rk_fb *rk_fb;

	if (fb_pdev) {
		rk_fb = platform_get_drvdata(fb_pdev);
		return rk_fb->disp_policy;
	} else {
		return DISPLAY_POLICY_SDK;
	}
}

int rk_fb_trsm_ops_register(struct rk_fb_trsm_ops *ops, int type)
{
	switch (type) {
	case SCREEN_RGB:
	case SCREEN_LVDS:
	case SCREEN_DUAL_LVDS:
	case SCREEN_LVDS_10BIT:
	case SCREEN_DUAL_LVDS_10BIT:
		trsm_lvds_ops = ops;
		break;
	case SCREEN_EDP:
		trsm_edp_ops = ops;
		break;
	case SCREEN_MIPI:
	case SCREEN_DUAL_MIPI:
		trsm_mipi_ops = ops;
		break;
	default:
		pr_warn("%s: unsupported transmitter: %d!\n",
			__func__, type);
		break;
	}
	return 0;
}

struct rk_fb_trsm_ops *rk_fb_trsm_ops_get(int type)
{
	struct rk_fb_trsm_ops *ops;

	switch (type) {
	case SCREEN_RGB:
	case SCREEN_LVDS:
	case SCREEN_DUAL_LVDS:
	case SCREEN_LVDS_10BIT:
	case SCREEN_DUAL_LVDS_10BIT:
		ops = trsm_lvds_ops;
		break;
	case SCREEN_EDP:
		ops = trsm_edp_ops;
		break;
	case SCREEN_MIPI:
	case SCREEN_DUAL_MIPI:
		ops = trsm_mipi_ops;
		break;
	default:
		ops = NULL;
		pr_warn("%s: unsupported transmitter: %d!\n",
			__func__, type);
		break;
	}
	return ops;
}

int rk_fb_pixel_width(int data_format)
{
	int pixel_width;

	switch (data_format) {
	case XBGR888:
	case XRGB888:
	case ABGR888:
	case ARGB888:
	case FBDC_ARGB_888:
	case FBDC_ABGR_888:
	case FBDC_RGBX_888:
		pixel_width = 4 * 8;
		break;
	case RGB888:
	case BGR888:
		pixel_width = 3 * 8;
		break;
	case RGB565:
	case BGR565:
	case FBDC_RGB_565:
		pixel_width = 2 * 8;
		break;
	case YUV422:
	case YUV420:
	case YUV420_NV21:
	case YUV444:
		pixel_width = 1 * 8;
		break;
	case YUV422_A:
	case YUV420_A:
	case YUV444_A:
		pixel_width = 8;
		break;
	case YUYV422:
	case UYVY422:
	case YUYV420:
	case UYVY420:
		pixel_width = 16;
		break;
	default:
		pr_warn("%s: unsupported format: 0x%x\n",
			__func__, data_format);
		return -EINVAL;
	}
	return pixel_width;
}

static int rk_fb_data_fmt(int data_format, int bits_per_pixel)
{
	int fb_data_fmt = 0;

	if (data_format) {
		switch (data_format) {
		case HAL_PIXEL_FORMAT_RGBX_8888:
			fb_data_fmt = XBGR888;
			break;
		case HAL_PIXEL_FORMAT_BGRX_8888:
			fb_data_fmt = XRGB888;
			break;
		case HAL_PIXEL_FORMAT_RGBA_8888:
			fb_data_fmt = ABGR888;
			break;
		case HAL_PIXEL_FORMAT_BGRA_8888:
			fb_data_fmt = ARGB888;
			break;
		case HAL_PIXEL_FORMAT_RGB_888:
			fb_data_fmt = RGB888;
			break;
		case HAL_PIXEL_FORMAT_BGR_888:
			fb_data_fmt = BGR888;
			break;
		case HAL_PIXEL_FORMAT_RGB_565:
			fb_data_fmt = RGB565;
			break;
		case HAL_PIXEL_FORMAT_BGR_565:
			fb_data_fmt = BGR565;
			break;
		case HAL_PIXEL_FORMAT_YCbCr_422_SP:	/* yuv422 */
			fb_data_fmt = YUV422;
			break;
		case HAL_PIXEL_FORMAT_YCrCb_420_SP:	/* YUV420---vuvuvu */
			fb_data_fmt = YUV420_NV21;
			break;
		case HAL_PIXEL_FORMAT_YCrCb_NV12:	/* YUV420---uvuvuv */
			fb_data_fmt = YUV420;
			break;
		case HAL_PIXEL_FORMAT_YCrCb_444:	/* yuv444 */
			fb_data_fmt = YUV444;
			break;
		case HAL_PIXEL_FORMAT_YCrCb_NV12_10:	/* yuv444 */
			fb_data_fmt = YUV420_A;
			break;
		case HAL_PIXEL_FORMAT_YCbCr_422_SP_10:	/* yuv444 */
			fb_data_fmt = YUV422_A;
			break;
		case HAL_PIXEL_FORMAT_YCrCb_444_SP_10:	/* yuv444 */
			fb_data_fmt = YUV444_A;
			break;
		case HAL_PIXEL_FORMAT_FBDC_RGB565:	/* fbdc rgb565*/
			fb_data_fmt = FBDC_RGB_565;
			break;
		case HAL_PIXEL_FORMAT_FBDC_U8U8U8U8:	/* fbdc argb888 */
			fb_data_fmt = FBDC_ARGB_888;
			break;
		case HAL_PIXEL_FORMAT_FBDC_RGBA888:	/* fbdc abgr888 */
			fb_data_fmt = FBDC_ABGR_888;
			break;
		case HAL_PIXEL_FORMAT_FBDC_U8U8U8:	/* fbdc rgb888 */
			fb_data_fmt = FBDC_RGBX_888;
			break;
		case HAL_PIXEL_FORMAT_YUYV422:		/* yuyv422 */
			fb_data_fmt = YUYV422;
			break;
		case HAL_PIXEL_FORMAT_YUYV420:		/* yuyv420 */
			fb_data_fmt = YUYV420;
			break;
		case HAL_PIXEL_FORMAT_UYVY422:		/* uyvy422 */
			fb_data_fmt = UYVY422;
			break;
		case HAL_PIXEL_FORMAT_UYVY420:		/* uyvy420 */
			fb_data_fmt = UYVY420;
			break;
		default:
			pr_warn("%s: unsupported format: 0x%x\n",
				__func__, data_format);
			return -EINVAL;
		}
	} else {
		switch (bits_per_pixel) {
		case 32:
			fb_data_fmt = ARGB888;
			break;
		case 24:
			fb_data_fmt = RGB888;
			break;
		case 16:
			fb_data_fmt = RGB565;
			break;
		default:
			pr_warn("%s: unsupported bits_per_pixel: %d\n",
				__func__, bits_per_pixel);
			break;
		}
	}
	return fb_data_fmt;
}

/*
 * rk display power control parse from dts
 */
int rk_disp_pwr_ctr_parse_dt(struct rk_lcdc_driver *dev_drv)
{
	struct device_node *root = of_get_child_by_name(dev_drv->dev->of_node,
							"power_ctr");
	struct device_node *child;
	struct rk_disp_pwr_ctr_list *pwr_ctr;
	struct list_head *pos;
	enum of_gpio_flags flags;
	u32 val = 0;
	u32 debug = 0;
	int ret;

	INIT_LIST_HEAD(&dev_drv->pwrlist_head);
	if (!root) {
		dev_err(dev_drv->dev, "can't find power_ctr node for lcdc%d\n",
			dev_drv->id);
		return -ENODEV;
	}

	for_each_child_of_node(root, child) {
		pwr_ctr = kmalloc(sizeof(struct rk_disp_pwr_ctr_list),
				  GFP_KERNEL);
		if (!pwr_ctr)
			return -ENOMEM;
		strcpy(pwr_ctr->pwr_ctr.name, child->name);
		if (!of_property_read_u32(child, "rockchip,power_type", &val)) {
			if (val == GPIO) {
				pwr_ctr->pwr_ctr.type = GPIO;
				pwr_ctr->pwr_ctr.gpio = of_get_gpio_flags(child, 0, &flags);
				if (!gpio_is_valid(pwr_ctr->pwr_ctr.gpio)) {
					dev_err(dev_drv->dev, "%s ivalid gpio\n",
						child->name);
					return -EINVAL;
				}
				pwr_ctr->pwr_ctr.atv_val = !(flags & OF_GPIO_ACTIVE_LOW);
				ret = gpio_request(pwr_ctr->pwr_ctr.gpio,
						   child->name);
				if (ret) {
					dev_err(dev_drv->dev,
						"request %s gpio fail:%d\n",
						child->name, ret);
				}

			} else {
				pwr_ctr->pwr_ctr.type = REGULATOR;
				pwr_ctr->pwr_ctr.rgl_name = NULL;
				ret = of_property_read_string(child, "rockchip,regulator_name",
							      &(pwr_ctr->pwr_ctr.rgl_name));
				if (ret || IS_ERR_OR_NULL(pwr_ctr->pwr_ctr.rgl_name))
					dev_err(dev_drv->dev, "get regulator name failed!\n");
				if (!of_property_read_u32(child, "rockchip,regulator_voltage", &val))
					pwr_ctr->pwr_ctr.volt = val;
				else
					pwr_ctr->pwr_ctr.volt = 0;
			}
		};

		if (!of_property_read_u32(child, "rockchip,delay", &val))
			pwr_ctr->pwr_ctr.delay = val;
		else
			pwr_ctr->pwr_ctr.delay = 0;
		list_add_tail(&pwr_ctr->list, &dev_drv->pwrlist_head);
	}

	of_property_read_u32(root, "rockchip,debug", &debug);

	if (debug) {
		list_for_each(pos, &dev_drv->pwrlist_head) {
			pwr_ctr = list_entry(pos, struct rk_disp_pwr_ctr_list,
					     list);
			pr_info("pwr_ctr_name:%s\n"
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
	struct regulator *regulator_lcd = NULL;
	int count = 10;

	if (list_empty(&dev_drv->pwrlist_head))
		return 0;
	list_for_each(pos, &dev_drv->pwrlist_head) {
		pwr_ctr_list = list_entry(pos, struct rk_disp_pwr_ctr_list,
					  list);
		pwr_ctr = &pwr_ctr_list->pwr_ctr;
		if (pwr_ctr->type == GPIO) {
			gpio_direction_output(pwr_ctr->gpio, pwr_ctr->atv_val);
			mdelay(pwr_ctr->delay);
		} else if (pwr_ctr->type == REGULATOR) {
			if (pwr_ctr->rgl_name)
				regulator_lcd =
					regulator_get(NULL, pwr_ctr->rgl_name);
			if (regulator_lcd == NULL) {
				dev_err(dev_drv->dev,
					"%s: regulator get failed,regulator name:%s\n",
					__func__, pwr_ctr->rgl_name);
				continue;
			}
			regulator_set_voltage(regulator_lcd, pwr_ctr->volt, pwr_ctr->volt);
			while (!regulator_is_enabled(regulator_lcd)) {
				if (regulator_enable(regulator_lcd) == 0 || count == 0)
					break;
				else
					dev_err(dev_drv->dev,
						"regulator_enable failed,count=%d\n",
						count);
				count--;
			}
			regulator_put(regulator_lcd);
			msleep(pwr_ctr->delay);
		}
	}

	return 0;
}

int rk_disp_pwr_disable(struct rk_lcdc_driver *dev_drv)
{
	struct list_head *pos;
	struct rk_disp_pwr_ctr_list *pwr_ctr_list;
	struct pwr_ctr *pwr_ctr;
	struct regulator *regulator_lcd = NULL;
	int count = 10;

	if (list_empty(&dev_drv->pwrlist_head))
		return 0;
	list_for_each(pos, &dev_drv->pwrlist_head) {
		pwr_ctr_list = list_entry(pos, struct rk_disp_pwr_ctr_list,
					  list);
		pwr_ctr = &pwr_ctr_list->pwr_ctr;
		if (pwr_ctr->type == GPIO) {
			gpio_set_value(pwr_ctr->gpio, !pwr_ctr->atv_val);
		} else if (pwr_ctr->type == REGULATOR) {
			if (pwr_ctr->rgl_name)
				regulator_lcd = regulator_get(NULL, pwr_ctr->rgl_name);
			if (regulator_lcd == NULL) {
				dev_err(dev_drv->dev,
					"%s: regulator get failed,regulator name:%s\n",
					__func__, pwr_ctr->rgl_name);
				continue;
			}
			while (regulator_is_enabled(regulator_lcd) > 0) {
				if (regulator_disable(regulator_lcd) == 0 ||
				    count == 0)
					break;
				else
					dev_err(dev_drv->dev,
						"regulator_disable failed,count=%d\n",
						count);
				count--;
			}
			regulator_put(regulator_lcd);
		}
	}
	return 0;
}

int rk_fb_video_mode_from_timing(const struct display_timing *dt,
				 struct rk_screen *screen)
{
	screen->mode.pixclock = dt->pixelclock.typ;
	screen->mode.left_margin = dt->hback_porch.typ;
	screen->mode.right_margin = dt->hfront_porch.typ;
	screen->mode.xres = dt->hactive.typ;
	screen->mode.hsync_len = dt->hsync_len.typ;
	screen->mode.upper_margin = dt->vback_porch.typ;
	screen->mode.lower_margin = dt->vfront_porch.typ;
	screen->mode.yres = dt->vactive.typ;
	screen->mode.vsync_len = dt->vsync_len.typ;
	screen->type = dt->screen_type;
	screen->refresh_mode = dt->refresh_mode;
	screen->lvds_format = dt->lvds_format;
	screen->face = dt->face;
	screen->color_mode = dt->color_mode;
	screen->width = dt->screen_widt;
	screen->height = dt->screen_hight;
	screen->dsp_lut = dt->dsp_lut;
	screen->cabc_lut = dt->cabc_lut;
	screen->cabc_gamma_base = dt->cabc_gamma_base;

	if (dt->flags & DISPLAY_FLAGS_INTERLACED)
		screen->mode.vmode |= FB_VMODE_INTERLACED;
	if (dt->flags & DISPLAY_FLAGS_PIXDATA_POSEDGE)
		screen->pin_dclk = 1;
	else
		screen->pin_dclk = 0;
	if (dt->flags & DISPLAY_FLAGS_HSYNC_HIGH)
		screen->pin_hsync = 1;
	else
		screen->pin_hsync = 0;
	if (dt->flags & DISPLAY_FLAGS_VSYNC_HIGH)
		screen->pin_vsync = 1;
	else
		screen->pin_vsync = 0;
	if (dt->flags & DISPLAY_FLAGS_DE_HIGH)
		screen->pin_den = 1;
	else
		screen->pin_den = 0;

	return 0;
}

int rk_fb_prase_timing_dt(struct device_node *np, struct rk_screen *screen)
{
	struct display_timings *disp_timing;
	struct display_timing *dt;

	disp_timing = of_get_display_timings(np);
	if (!disp_timing) {
		pr_err("parse display timing err\n");
		return -EINVAL;
	}
	dt = display_timings_get(disp_timing, disp_timing->native_mode);
	rk_fb_video_mode_from_timing(dt, screen);

	return 0;
}

int rk_fb_calc_fps(struct rk_screen *screen, u32 pixclock)
{
	int x, y;
	unsigned long long hz;

	if (!screen) {
		pr_err("%s:null screen!\n", __func__);
		return 0;
	}
	x = screen->mode.xres + screen->mode.left_margin +
	    screen->mode.right_margin + screen->mode.hsync_len;
	y = screen->mode.yres + screen->mode.upper_margin +
	    screen->mode.lower_margin + screen->mode.vsync_len;

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
	case BGR888:
		strcpy(fmt, "BGR888");
		break;
	case RGB565:
		strcpy(fmt, "RGB565");
		break;
	case BGR565:
		strcpy(fmt, "BGR565");
		break;
	case YUV420:
	case YUV420_NV21:
		strcpy(fmt, "YUV420");
		break;
	case YUV422:
		strcpy(fmt, "YUV422");
		break;
	case YUV444:
		strcpy(fmt, "YUV444");
		break;
	case YUV420_A:
		strcpy(fmt, "YUV420_A");
		break;
	case YUV422_A:
		strcpy(fmt, "YUV422_A");
		break;
	case YUV444_A:
		strcpy(fmt, "YUV444_A");
		break;
	case XRGB888:
		strcpy(fmt, "XRGB888");
		break;
	case XBGR888:
		strcpy(fmt, "XBGR888");
		break;
	case ABGR888:
		strcpy(fmt, "ABGR888");
		break;
	case FBDC_RGB_565:
		strcpy(fmt, "FBDC_RGB_565");
		break;
	case FBDC_ARGB_888:
	case FBDC_ABGR_888:
		strcpy(fmt, "FBDC_ARGB_888");
		break;
	case FBDC_RGBX_888:
		strcpy(fmt, "FBDC_RGBX_888");
		break;
	case YUYV422:
		strcpy(fmt, "YUYV422");
		break;
	case YUYV420:
		strcpy(fmt, "YUYV420");
		break;
	case UYVY422:
		strcpy(fmt, "UYVY422");
		break;
	case UYVY420:
		strcpy(fmt, "UYVY420");
		break;
	default:
		strcpy(fmt, "invalid");
		break;
	}

	return fmt;
}

int rk_fb_set_vop_pwm(void)
{
	int i = 0;
	struct rk_fb *inf = NULL;
	struct rk_lcdc_driver *dev_drv = NULL;

	if (likely(fb_pdev))
		inf = platform_get_drvdata(fb_pdev);
	else
		return -1;

	for (i = 0; i < inf->num_lcdc; i++) {
		if (inf->lcdc_dev_drv[i]->cabc_mode == 1) {
			dev_drv = inf->lcdc_dev_drv[i];
			break;
		}
	}

	if (!dev_drv)
		return -1;

	mutex_lock(&dev_drv->win_config);
	if (dev_drv->ops->extern_func)
		dev_drv->ops->extern_func(dev_drv, UPDATE_CABC_PWM);
	mutex_unlock(&dev_drv->win_config);

	return 0;
}

/*
 * this is for hdmi
 * name: lcdc device name ,lcdc0 , lcdc1
 */
struct rk_lcdc_driver *rk_get_lcdc_drv(char *name)
{
	struct rk_fb *inf = NULL;
	struct rk_lcdc_driver *dev_drv = NULL;
	int i = 0;

	if (likely(fb_pdev))
		inf = platform_get_drvdata(fb_pdev);
	else
		return NULL;

	for (i = 0; i < inf->num_lcdc; i++) {
		if (!strcmp(inf->lcdc_dev_drv[i]->name, name)) {
			dev_drv = inf->lcdc_dev_drv[i];
			break;
		}
	}

	return dev_drv;
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
		if (inf->lcdc_dev_drv[i]->prop == PRMRY) {
			dev_drv = inf->lcdc_dev_drv[i];
			break;
		}
	}

	return dev_drv;
}

static __maybe_unused struct rk_lcdc_driver *rk_get_extend_lcdc_drv(void)
{
	struct rk_fb *inf = NULL;
	struct rk_lcdc_driver *dev_drv = NULL;
	int i = 0;

	if (likely(fb_pdev))
		inf = platform_get_drvdata(fb_pdev);
	else
		return NULL;

	for (i = 0; i < inf->num_lcdc; i++) {
		if (inf->lcdc_dev_drv[i]->prop == EXTEND) {
			dev_drv = inf->lcdc_dev_drv[i];
			break;
		}
	}

	return dev_drv;
}

/*
 * get one frame time of the prmry screen, unit: us
 */
u32 rk_fb_get_prmry_screen_ft(void)
{
	struct rk_lcdc_driver *dev_drv = rk_get_prmry_lcdc_drv();
	u32 htotal, vtotal, pixclock_ps;
	u64 pix_total, ft_us;

	if (unlikely(!dev_drv))
		return 0;

	pixclock_ps = dev_drv->pixclock;

	vtotal = dev_drv->cur_screen->mode.upper_margin +
		 dev_drv->cur_screen->mode.lower_margin +
		 dev_drv->cur_screen->mode.yres +
		 dev_drv->cur_screen->mode.vsync_len;
	htotal = dev_drv->cur_screen->mode.left_margin +
		 dev_drv->cur_screen->mode.right_margin +
		 dev_drv->cur_screen->mode.xres +
		 dev_drv->cur_screen->mode.hsync_len;
	pix_total = htotal * vtotal;
	ft_us = pix_total * pixclock_ps;
	do_div(ft_us, 1000000);
	if (dev_drv->frame_time.ft == 0)
		dev_drv->frame_time.ft = ft_us;

	ft_us = dev_drv->frame_time.framedone_t - dev_drv->frame_time.last_framedone_t;
	do_div(ft_us, 1000);
	ft_us = min(dev_drv->frame_time.ft, (u32)ft_us);
	if (ft_us != 0)
		dev_drv->frame_time.ft = ft_us;

	return dev_drv->frame_time.ft;
}

/*
 * get the vblanking time of the prmry screen, unit: us
 */
u32 rk_fb_get_prmry_screen_vbt(void)
{
	struct rk_lcdc_driver *dev_drv = rk_get_prmry_lcdc_drv();
	u32 htotal, vblank, pixclock_ps;
	u64 pix_blank, vbt_us;

	if (unlikely(!dev_drv))
		return 0;

	pixclock_ps = dev_drv->pixclock;

	htotal = (dev_drv->cur_screen->mode.left_margin +
		  dev_drv->cur_screen->mode.right_margin +
		  dev_drv->cur_screen->mode.xres +
		  dev_drv->cur_screen->mode.hsync_len);
	vblank = (dev_drv->cur_screen->mode.upper_margin +
		  dev_drv->cur_screen->mode.lower_margin +
		  dev_drv->cur_screen->mode.vsync_len);
	pix_blank = htotal * vblank;
	vbt_us = pix_blank * pixclock_ps;
	do_div(vbt_us, 1000000);
	return (u32)vbt_us;
}

/*
 * get the frame done time of the prmry screen, unit: us
 */
u64 rk_fb_get_prmry_screen_framedone_t(void)
{
	struct rk_lcdc_driver *dev_drv = rk_get_prmry_lcdc_drv();

	if (unlikely(!dev_drv))
		return 0;
	else
		return dev_drv->frame_time.framedone_t;
}

/*
 * set prmry screen status
 */
int rk_fb_set_prmry_screen_status(int status)
{
	struct rk_lcdc_driver *dev_drv = rk_get_prmry_lcdc_drv();
	struct rk_screen *screen;

	if (unlikely(!dev_drv))
		return 0;

	screen = dev_drv->cur_screen;
	switch (status) {
	case SCREEN_PREPARE_DDR_CHANGE:
		if (screen->type == SCREEN_MIPI ||
		    screen->type == SCREEN_DUAL_MIPI) {
			if (dev_drv->trsm_ops->dsp_pwr_off)
				dev_drv->trsm_ops->dsp_pwr_off();
		}
		break;
	case SCREEN_UNPREPARE_DDR_CHANGE:
		if (screen->type == SCREEN_MIPI ||
		    screen->type == SCREEN_DUAL_MIPI) {
			if (dev_drv->trsm_ops->dsp_pwr_on)
				dev_drv->trsm_ops->dsp_pwr_on();
		}
		break;
	default:
		break;
	}

	return 0;
}

u32 rk_fb_get_prmry_screen_pixclock(void)
{
	struct rk_lcdc_driver *dev_drv = rk_get_prmry_lcdc_drv();

	if (unlikely(!dev_drv))
		return 0;
	else
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
	} else {
		return RK_LF_STATUS_NC;
	}
}

bool rk_fb_poll_wait_frame_complete(void)
{
	uint32_t timeout = RK_LF_MAX_TIMEOUT;
	struct rk_lcdc_driver *dev_drv = rk_get_prmry_lcdc_drv();

	if (likely(dev_drv)) {
		if (dev_drv->ops->set_irq_to_cpu)
			dev_drv->ops->set_irq_to_cpu(dev_drv, 0);
	}

	if (rk_fb_poll_prmry_screen_vblank() == RK_LF_STATUS_NC) {
		if (likely(dev_drv)) {
			if (dev_drv->ops->set_irq_to_cpu)
				dev_drv->ops->set_irq_to_cpu(dev_drv, 1);
		}
		return false;
	}
	while (!(rk_fb_poll_prmry_screen_vblank() == RK_LF_STATUS_FR) && --timeout)
		;
	while (!(rk_fb_poll_prmry_screen_vblank() == RK_LF_STATUS_FC) && --timeout)
		;
	if (likely(dev_drv)) {
		if (dev_drv->ops->set_irq_to_cpu)
			dev_drv->ops->set_irq_to_cpu(dev_drv, 1);
	}

	return true;
}

/* rk_fb_get_sysmmu_device_by_compatible()
 * @compt: dts device compatible name
 * return value: success: pointer to the device inside of platform device
 *               fail: NULL
 */
struct device *rk_fb_get_sysmmu_device_by_compatible(const char *compt)
{
	struct device_node *dn = NULL;
	struct platform_device *pd = NULL;
	struct device *ret = NULL ;

	dn = of_find_compatible_node(NULL, NULL, compt);
	if (!dn) {
		pr_info("can't find device node %s \r\n", compt);
		return NULL;
	}

	pd = of_find_device_by_node(dn);
	if (!pd) {
		pr_info("can't find platform device node %s \r\n", compt);
		return  NULL;
	}
	ret = &pd->dev;

	return ret;
}

#ifdef CONFIG_IOMMU_API
void rk_fb_platform_set_sysmmu(struct device *sysmmu, struct device *dev)
{
	dev->archdata.iommu = sysmmu;
}
#else
void rk_fb_platform_set_sysmmu(struct device *sysmmu, struct device *dev)
{
}
#endif

static int rk_fb_open(struct fb_info *info, int user)
{
	struct rk_fb_par *fb_par = (struct rk_fb_par *)info->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;
	int win_id;

	win_id = dev_drv->ops->fb_get_win_id(dev_drv, info->fix.id);
	fb_par->state++;
	/* if this win aready opened ,no need to reopen */
	if (dev_drv->win[win_id]->state)
		return 0;
	else
		dev_drv->ops->open(dev_drv, win_id, 1);
	return 0;
}

static int rk_fb_close(struct fb_info *info, int user)
{
	struct rk_fb_par *fb_par = (struct rk_fb_par *)info->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;
	struct rk_lcdc_win *win = NULL;
	int win_id = dev_drv->ops->fb_get_win_id(dev_drv, info->fix.id);

	if (win_id >= 0) {
		win = dev_drv->win[win_id];
		if (fb_par->state)
			fb_par->state--;
		if (!fb_par->state) {
			if (fb_par->fb_phy_base > 0)
				info->fix.smem_start = fb_par->fb_phy_base;
			info->var.xres = dev_drv->screen0->mode.xres;
			info->var.yres = dev_drv->screen0->mode.yres;
			/*
			 *info->var.grayscale |=
			 *   (info->var.xres << 8) + (info->var.yres << 20);
			 */
			info->var.xres_virtual = info->var.xres;
			info->var.yres_virtual = info->var.yres;
#if defined(CONFIG_LOGO_LINUX_BMP)
			info->var.bits_per_pixel = 32;
#else
			info->var.bits_per_pixel = 16;
#endif
			info->fix.line_length =
			    (info->var.xres_virtual) *
			    (info->var.bits_per_pixel >> 3);
			info->var.width = dev_drv->screen0->width;
			info->var.height = dev_drv->screen0->height;
			info->var.pixclock = dev_drv->pixclock;
			info->var.left_margin =
				dev_drv->screen0->mode.left_margin;
			info->var.right_margin =
				dev_drv->screen0->mode.right_margin;
			info->var.upper_margin =
				dev_drv->screen0->mode.upper_margin;
			info->var.lower_margin =
				dev_drv->screen0->mode.lower_margin;
			info->var.vsync_len = dev_drv->screen0->mode.vsync_len;
			info->var.hsync_len = dev_drv->screen0->mode.hsync_len;
		}
	}

	return 0;
}

#if defined(FB_ROATE_BY_KERNEL)

#if defined(CONFIG_RK29_IPP)
static int get_ipp_format(int fmt)
{
	int ipp_fmt = IPP_XRGB_8888;

	switch (fmt) {
	case HAL_PIXEL_FORMAT_RGBX_8888:
	case HAL_PIXEL_FORMAT_BGRX_8888:
	case HAL_PIXEL_FORMAT_RGBA_8888:
	case HAL_PIXEL_FORMAT_BGRA_8888:
	case HAL_PIXEL_FORMAT_RGB_888:
	case HAL_PIXEL_FORMAT_BGR_888:
		ipp_fmt = IPP_XRGB_8888;
		break;
	case HAL_PIXEL_FORMAT_RGB_565:
	case HAL_PIXEL_FORMAT_BGR_565:
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
	align16 -= 1;		/*for YUV, 1 */
	align64 -= 1;		/*for YUV, 7 */

	if (rotation == IPP_ROT_0) {
		if (fmt > IPP_RGB_565) {
			if ((*dst_w & 1) != 0)
				*dst_w = *dst_w + 1;
			if ((*dst_h & 1) != 0)
				*dst_h = *dst_h + 1;
			if (*dst_vir_w < *dst_w)
				*dst_vir_w = *dst_w;
		}
	} else {
		if ((*dst_w & align64) != 0)
			*dst_w = (*dst_w + align64) & (~align64);
		if ((fmt > IPP_RGB_565) && ((*dst_h & 1) == 1))
			*dst_h = *dst_h + 1;
		if (*dst_vir_w < *dst_w)
			*dst_vir_w = *dst_w;
	}
}

static void fb_copy_by_ipp(struct fb_info *dst_info,
			   struct fb_info *src_info)
{
	struct rk29_ipp_req ipp_req;
	uint32_t rotation = 0;
	int dst_w, dst_h, dst_vir_w;
	int ipp_fmt;
	u8 data_format = (dst_info->var.nonstd) & 0xff;
	struct rk_fb_par *fb_par = (struct rk_fb_par *)dst_info->par;
	struct rk_lcdc_driver *ext_dev_drv = fb_par->lcdc_drv;
	u16 orientation = ext_dev_drv->rotate_mode;

	memset(&ipp_req, 0, sizeof(struct rk29_ipp_req));

	switch (orientation) {
	case 0:
		rotation = IPP_ROT_0;
		break;
	case ROTATE_90:
		rotation = IPP_ROT_90;
		break;
	case ROTATE_180:
		rotation = IPP_ROT_180;
		break;
	case ROTATE_270:
		rotation = IPP_ROT_270;
		break;
	default:
		rotation = IPP_ROT_270;
		break;
	}

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

#if defined(CONFIG_ROCKCHIP_RGA) || defined(CONFIG_ROCKCHIP_RGA2)
static int get_rga_format(int fmt)
{
	int rga_fmt = 0;

	switch (fmt) {
	case XBGR888:
		rga_fmt = RK_FORMAT_RGBX_8888;
		break;
	case ABGR888:
		rga_fmt = RK_FORMAT_RGBA_8888;
		break;
	case ARGB888:
		rga_fmt = RK_FORMAT_BGRA_8888;
		break;
	case RGB888:
		rga_fmt = RK_FORMAT_RGB_888;
		break;
	case RGB565:
		rga_fmt = RK_FORMAT_RGB_565;
		break;
	case YUV422:
		rga_fmt = RK_FORMAT_YCbCr_422_SP;
		break;
	case YUV420:
		rga_fmt = RK_FORMAT_YCbCr_420_SP;
		break;
	default:
		rga_fmt = RK_FORMAT_RGBA_8888;
		break;
	}

	return rga_fmt;
}

static void rga_win_check(struct rk_lcdc_win *dst_win,
			  struct rk_lcdc_win *src_win)
{
	int format = 0;

	format = get_rga_format(src_win->area[0].format);
	/* width and height must be even number */
	if (format >= RK_FORMAT_YCbCr_422_SP &&
	    format <= RK_FORMAT_YCrCb_420_P) {
		if ((src_win->area[0].xact % 2) != 0)
			src_win->area[0].xact += 1;
		if ((src_win->area[0].yact % 2) != 0)
			src_win->area[0].yact += 1;
	}
	if (src_win->area[0].xvir < src_win->area[0].xact)
		src_win->area[0].xvir = src_win->area[0].xact;
	if (src_win->area[0].yvir < src_win->area[0].yact)
		src_win->area[0].yvir = src_win->area[0].yact;

	format = get_rga_format(dst_win->area[0].format);
	if (format >= RK_FORMAT_YCbCr_422_SP &&
	    format <= RK_FORMAT_YCrCb_420_P) {
		if ((dst_win->area[0].xact % 2) != 0)
			dst_win->area[0].xact += 1;
		if ((dst_win->area[0].yact % 2) != 0)
			dst_win->area[0].yact += 1;
	}
	if (dst_win->area[0].xvir < dst_win->area[0].xact)
		dst_win->area[0].xvir = dst_win->area[0].xact;
	if (dst_win->area[0].yvir < dst_win->area[0].yact)
		dst_win->area[0].yvir = dst_win->area[0].yact;
}

static void win_copy_by_rga(struct rk_lcdc_win *dst_win,
			    struct rk_lcdc_win *src_win,
			    u16 orientation, int iommu_en)
{
	struct rga_req rga_request;
	long ret = 0;
	/* int fd = 0; */

	memset(&rga_request, 0, sizeof(rga_request));
	rga_win_check(dst_win, src_win);

	switch (orientation) {
	case ROTATE_90:
		rga_request.rotate_mode = 1;
		rga_request.sina = 65536;
		rga_request.cosa = 0;
		rga_request.dst.act_w = dst_win->area[0].yact;
		rga_request.dst.act_h = dst_win->area[0].xact;
		rga_request.dst.x_offset = dst_win->area[0].xact - 1;
		rga_request.dst.y_offset = 0;
		break;
	case ROTATE_180:
		rga_request.rotate_mode = 1;
		rga_request.sina = 0;
		rga_request.cosa = -65536;
		rga_request.dst.act_w = dst_win->area[0].xact;
		rga_request.dst.act_h = dst_win->area[0].yact;
		rga_request.dst.x_offset = dst_win->area[0].xact - 1;
		rga_request.dst.y_offset = dst_win->area[0].yact - 1;
		break;
	case ROTATE_270:
		rga_request.rotate_mode = 1;
		rga_request.sina = -65536;
		rga_request.cosa = 0;
		rga_request.dst.act_w = dst_win->area[0].yact;
		rga_request.dst.act_h = dst_win->area[0].xact;
		rga_request.dst.x_offset = 0;
		rga_request.dst.y_offset = dst_win->area[0].yact - 1;
		break;
	default:
		rga_request.rotate_mode = 0;
		rga_request.dst.act_w = dst_win->area[0].xact;
		rga_request.dst.act_h = dst_win->area[0].yact;
		rga_request.dst.x_offset = dst_win->area[0].xact - 1;
		rga_request.dst.y_offset = dst_win->area[0].yact - 1;
		break;
	}

	/*
	 * fd =
	 *    ion_share_dma_buf_fd(rk_fb->ion_client, src_win->area[0].ion_hdl);
	 * rga_request.src.yrgb_addr = fd;
	 * fd =
	 *    ion_share_dma_buf_fd(rk_fb->ion_client, dst_win->area[0].ion_hdl);
	 * rga_request.dst.yrgb_addr = fd;
	 */
	rga_request.src.yrgb_addr = 0;
	rga_request.src.uv_addr =
	    src_win->area[0].smem_start + src_win->area[0].y_offset;
	rga_request.src.v_addr = 0;

	rga_request.dst.yrgb_addr = 0;
	rga_request.dst.uv_addr =
	    dst_win->area[0].smem_start + dst_win->area[0].y_offset;
	rga_request.dst.v_addr = 0;

	rga_request.src.vir_w = src_win->area[0].xvir;
	rga_request.src.vir_h = src_win->area[0].yvir;
	rga_request.src.format = get_rga_format(src_win->area[0].format);
	rga_request.src.act_w = src_win->area[0].xact;
	rga_request.src.act_h = src_win->area[0].yact;
	rga_request.src.x_offset = 0;
	rga_request.src.y_offset = 0;

	rga_request.dst.vir_w = dst_win->area[0].xvir;
	rga_request.dst.vir_h = dst_win->area[0].yvir;
	rga_request.dst.format = get_rga_format(dst_win->area[0].format);

	rga_request.clip.xmin = 0;
	rga_request.clip.xmax = dst_win->area[0].xact - 1;
	rga_request.clip.ymin = 0;
	rga_request.clip.ymax = dst_win->area[0].yact - 1;
	rga_request.scale_mode = 0;

	if (iommu_en) {
		rga_request.mmu_info.mmu_en = 1;
		rga_request.mmu_info.mmu_flag = 1;
	} else {
		rga_request.mmu_info.mmu_en = 0;
		rga_request.mmu_info.mmu_flag = 0;
	}

	ret = rga_ioctl_kernel(&rga_request);
}

/*
 * This function is used for copying fb by RGA Module
 * RGA only support copy RGB to RGB
 * RGA2 support copy RGB to RGB and YUV to YUV
 */
static void fb_copy_by_rga(struct fb_info *dst_info,
			   struct fb_info *src_info)
{
	struct rk_fb_par *src_fb_par = (struct rk_fb_par *)src_info->par;
	struct rk_fb_par *dst_fb_par = (struct rk_fb_par *)dst_info->par;
	struct rk_lcdc_driver *dev_drv = src_fb_par->lcdc_drv;
	struct rk_lcdc_driver *ext_dev_drv = dst_fb_par->lcdc_drv;
	int win_id = 0, ext_win_id;
	struct rk_lcdc_win *src_win, *dst_win;

	win_id = dev_drv->ops->fb_get_win_id(dev_drv, src_info->fix.id);
	src_win = dev_drv->win[win_id];

	ext_win_id =
	    ext_dev_drv->ops->fb_get_win_id(ext_dev_drv, dst_info->fix.id);
	dst_win = ext_dev_drv->win[ext_win_id];

	win_copy_by_rga(dst_win, src_win, ext_dev_drv->rotate_mode,
			ext_dev_drv->iommu_enabled);
}
#endif

static int rk_fb_rotate(struct fb_info *dst_info,
			struct fb_info *src_info)
{
#if defined(CONFIG_RK29_IPP)
	fb_copy_by_ipp(dst_info, src_info);
#elif defined(CONFIG_ROCKCHIP_RGA) || defined(CONFIG_ROCKCHIP_RGA2)
	fb_copy_by_rga(dst_info, src_info);
#else
	return -1;
#endif
	return 0;
}

static int __maybe_unused rk_fb_win_rotate(struct rk_lcdc_win *dst_win,
					   struct rk_lcdc_win *src_win,
					   u16 rotate, int iommu_en)
{
#if defined(CONFIG_ROCKCHIP_RGA) || defined(CONFIG_ROCKCHIP_RGA2)
	win_copy_by_rga(dst_win, src_win, rotate, iommu_en);
#else
	return -1;
#endif
	return 0;
}

#endif

static int rk_fb_pan_display(struct fb_var_screeninfo *var,
			     struct fb_info *info)
{
	struct rk_fb_par *fb_par = (struct rk_fb_par *)info->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;
	struct fb_fix_screeninfo *fix = &info->fix;
	int win_id = 0;
	struct rk_lcdc_win *win = NULL;
	struct rk_screen *screen = dev_drv->cur_screen;
	u32 xoffset = var->xoffset;
	u32 yoffset = var->yoffset;
	u32 xvir = var->xres_virtual;
	u8 pixel_width;
	u32 vir_width_bit;
	u32 stride, uv_stride;
	u32 stride_32bit_1;
	u32 stride_32bit_2;
	u16 uv_x_off, uv_y_off, uv_y_act;
	u8 is_pic_yuv = 0;

	if (dev_drv->suspend_flag || is_car_camcap())
		return 0;
	win_id = dev_drv->ops->fb_get_win_id(dev_drv, info->fix.id);
	if (win_id < 0)
		return -ENODEV;
	else
		win = dev_drv->win[win_id];

	pixel_width = rk_fb_pixel_width(win->area[0].format);
	vir_width_bit = pixel_width * xvir;
	stride_32bit_1 = ALIGN_N_TIMES(vir_width_bit, 32) / 8;
	stride_32bit_2 = ALIGN_N_TIMES(vir_width_bit * 2, 32) / 8;

	switch (win->area[0].format) {
	case YUV422:
	case YUV422_A:
		is_pic_yuv = 1;
		stride = stride_32bit_1;
		uv_stride = stride_32bit_1;
		uv_x_off = xoffset;
		uv_y_off = yoffset;
		fix->line_length = stride;
		uv_y_act = win->area[0].yact >> 1;
		break;
	case YUV420:		/* nv12 */
	case YUV420_NV21:	/* nv21 */
	case YUV420_A:
		is_pic_yuv = 1;
		stride = stride_32bit_1;
		uv_stride = stride_32bit_1;
		uv_x_off = xoffset;
		uv_y_off = yoffset >> 1;
		fix->line_length = stride;
		uv_y_act = win->area[0].yact >> 1;
		break;
	case YUV444:
	case YUV444_A:
		is_pic_yuv = 1;
		stride = stride_32bit_1;
		uv_stride = stride_32bit_2;
		uv_x_off = xoffset * 2;
		uv_y_off = yoffset;
		fix->line_length = stride << 2;
		uv_y_act = win->area[0].yact;
		break;
	default:
		stride = stride_32bit_1;	/* default rgb */
		fix->line_length = stride;
		break;
	}

	/* x y mirror ,jump line */
	if ((screen->y_mirror == 1) ||
	    (win->xmirror && win->ymirror)) {
		if (screen->interlace == 1) {
			win->area[0].y_offset = yoffset * stride * 2 +
			    ((win->area[0].yact - 1) * 2 + 1) * stride +
			    xoffset * pixel_width / 8;
		} else {
			win->area[0].y_offset = yoffset * stride +
			    (win->area[0].yact - 1) * stride +
			    xoffset * pixel_width / 8;
		}
	} else {
		if (screen->interlace == 1) {
			win->area[0].y_offset =
			    yoffset * stride * 2 + xoffset * pixel_width / 8;
		} else {
			win->area[0].y_offset =
			    yoffset * stride + xoffset * pixel_width / 8;
		}
	}
	if (is_pic_yuv == 1) {
		if ((screen->y_mirror == 1) ||
		    (win->xmirror && win->ymirror)) {
			if (screen->interlace == 1) {
				win->area[0].c_offset =
				    uv_y_off * uv_stride * 2 +
				    ((uv_y_act - 1) * 2 + 1) * uv_stride +
				    uv_x_off * pixel_width / 8;
			} else {
				win->area[0].c_offset = uv_y_off * uv_stride +
				    (uv_y_act - 1) * uv_stride +
				    uv_x_off * pixel_width / 8;
			}
		} else {
			if (screen->interlace == 1) {
				win->area[0].c_offset =
				    uv_y_off * uv_stride * 2 +
				    uv_x_off * pixel_width / 8;
			} else {
				win->area[0].c_offset =
				    uv_y_off * uv_stride +
				    uv_x_off * pixel_width / 8;
			}
		}
	}

	win->area[0].smem_start = fix->smem_start;
	win->area[0].cbr_start = fix->mmio_start;
	win->area[0].state = 1;
	win->area_num = 1;

	dev_drv->ops->pan_display(dev_drv, win_id);

#ifdef	CONFIG_FB_MIRRORING
	if (video_data_to_mirroring)
		video_data_to_mirroring(info, NULL);
#endif
	/* if not want the config effect,set reserved[3] bit[0] 1 */
	if (likely((var->reserved[3] & 0x1) == 0))
		dev_drv->ops->cfg_done(dev_drv);
	if (dev_drv->hdmi_switch)
		mdelay(100);
	return 0;
}

static int rk_fb_get_list_stat(struct rk_lcdc_driver *dev_drv)
{
	int i, j;

	i = list_empty(&dev_drv->update_regs_list);
	j = list_empty(&dev_drv->saved_list);
	return i == j ? 0 : 1;
}

void rk_fd_fence_wait(struct rk_lcdc_driver *dev_drv, struct sync_fence *fence)
{
	int err = sync_fence_wait(fence, 1000);

	if (err >= 0)
		return;

	if (err == -ETIME)
		err = sync_fence_wait(fence, 10 * MSEC_PER_SEC);

	if (err < 0)
		pr_info("error waiting on fence\n");
}
#if 0
static int rk_fb_copy_from_loader(struct fb_info *info)
{
	struct rk_fb_par *fb_par = (struct rk_fb_par *)info->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;
	void *dst = info->screen_base;
	u32 dsp_addr[4];
	u32 src;
	u32 i, size;
	int win_id;
	struct rk_lcdc_win *win;

	win_id = dev_drv->ops->fb_get_win_id(dev_drv, info->fix.id);
	win = dev_drv->win[win_id];
	size = (win->area[0].xact) * (win->area[0].yact) << 2;
	dev_drv->ops->get_dsp_addr(dev_drv, dsp_addr);
	src = dsp_addr[win_id];
	dev_info(info->dev, "copy fb data %d x %d  from  dst_addr:%08x\n",
		 win->area[0].xact, win->area[0].yact, src);
	for (i = 0; i < size; i += PAGE_SIZE) {
		void *page = phys_to_page(i + src);
		void *from_virt = kmap(page);
		void *to_virt = dst + i;

		memcpy(to_virt, from_virt, PAGE_SIZE);
	}
	dev_drv->ops->direct_set_addr(dev_drv, win_id,
				      info->fix.smem_start);
	return 0;
}
#endif
static int g_last_addr[5][4];
static int g_now_config_addr[5][4];
static int g_last_state[5][4];
static int g_now_config_state[5][4];
int g_last_timeout;
u32 freed_addr[10];
u32 freed_index;

#define DUMP_CHUNK 256
char buf[PAGE_SIZE];

int rk_fb_sysmmu_fault_handler(struct device *dev,
			       enum rk_iommu_inttype itype,
			       unsigned long pgtable_base,
			       unsigned long fault_addr, unsigned int status)
{
	struct rk_lcdc_driver *dev_drv = rk_get_prmry_lcdc_drv();
	int i = 0, j = 0;
	static int page_fault_cnt;

	if ((page_fault_cnt++) >= 10)
		return 0;
	pr_err
	    ("PAGE FAULT occurred at 0x%lx (Page table base: 0x%lx),status=%d\n",
	     fault_addr, pgtable_base, status);
	pr_info("last config addr:\n");
	for (i = 0; i < 4; i++) {
		for (j = 0; j < 4; j++)
			pr_info("win[%d],area[%d] = 0x%08x\n",
				i, j, g_last_addr[i][j]);
	}
	pr_info("last freed buffer:\n");
	for (i = 0; (freed_addr[i] != 0xfefefefe) && freed_addr[i]; i++)
		pr_info("%d:0x%08x\n", i, freed_addr[i]);
	pr_info("last timeout:%d\n", g_last_timeout);
	dev_drv->ops->get_disp_info(dev_drv, buf, 0);
	for (i = 0; i < PAGE_SIZE; i += DUMP_CHUNK) {
		if ((PAGE_SIZE - i) > DUMP_CHUNK) {
			char c = buf[i + DUMP_CHUNK];

			buf[i + DUMP_CHUNK] = 0;
			pr_cont("%s", buf + i);
			buf[i + DUMP_CHUNK] = c;
		} else {
			buf[PAGE_SIZE - 1] = 0;
			pr_cont("%s", buf + i);
		}
	}

	return 0;
}

void rk_fb_free_wb_buf(struct rk_lcdc_driver *dev_drv,
		       struct rk_fb_reg_wb_data *wb_data)
{
	struct rk_fb *rk_fb = platform_get_drvdata(fb_pdev);

	if (dev_drv->iommu_enabled && wb_data->ion_handle)
		ion_unmap_iommu(dev_drv->dev, rk_fb->ion_client,
				wb_data->ion_handle);
	if (wb_data->ion_handle)
		ion_free(rk_fb->ion_client, wb_data->ion_handle);
}

void rk_fb_free_dma_buf(struct rk_lcdc_driver *dev_drv,
			struct rk_fb_reg_win_data *reg_win_data)
{
	int i, index_buf;
	struct rk_fb_reg_area_data *area_data;
	struct rk_fb *rk_fb = platform_get_drvdata(fb_pdev);

	for (i = 0; i < reg_win_data->area_num; i++) {
		area_data = &reg_win_data->reg_area_data[i];
		index_buf = area_data->index_buf;
		if (dev_drv->iommu_enabled) {
			if (area_data->ion_handle != NULL &&
			    !IS_YUV_FMT(area_data->data_format))
				ion_unmap_iommu(dev_drv->dev, rk_fb->ion_client,
						area_data->ion_handle);
			freed_addr[freed_index++] = area_data->smem_start;
		}
		if (area_data->ion_handle != NULL)
			ion_free(rk_fb->ion_client, area_data->ion_handle);

		if (area_data->acq_fence)
			sync_fence_put(area_data->acq_fence);
	}
}

static void rk_fb_update_win(struct rk_lcdc_driver *dev_drv,
			     struct rk_lcdc_win *win,
			     struct rk_fb_reg_win_data *reg_win_data)
{
	int i = 0;
	struct rk_fb *inf = platform_get_drvdata(fb_pdev);
	struct rk_screen *cur_screen;
	struct rk_screen primary_screen;

	if (unlikely(!inf) || unlikely(!dev_drv) ||
	    unlikely(!win) || unlikely(!reg_win_data))
		return;

	cur_screen = dev_drv->cur_screen;
	rk_fb_get_prmry_screen(&primary_screen);

	win->area_num = reg_win_data->area_num;
	win->id = reg_win_data->win_id;
	win->z_order = reg_win_data->z_order;

	if (reg_win_data->reg_area_data[0].smem_start > 0) {
		win->state = 1;
		win->area_num = reg_win_data->area_num;
		win->id = reg_win_data->win_id;
		win->z_order = reg_win_data->z_order;
		win->area[0].uv_vir_stride =
		    reg_win_data->reg_area_data[0].uv_vir_stride;
		win->area[0].cbr_start =
		    reg_win_data->reg_area_data[0].cbr_start;
		win->area[0].c_offset = reg_win_data->reg_area_data[0].c_offset;
		win->alpha_en = reg_win_data->alpha_en;
		win->alpha_mode = reg_win_data->alpha_mode;
		win->g_alpha_val = reg_win_data->g_alpha_val;
		/*
		 * reg_win_data mirror_en means that xmirror ymirror all
		 * enabled.
		 */
		win->xmirror = reg_win_data->mirror_en ? 1 : 0;
		win->ymirror = reg_win_data->mirror_en ? 1 : 0;
		win->colorspace = reg_win_data->colorspace;
		win->area[0].fbdc_en =
			reg_win_data->reg_area_data[0].fbdc_en;
		win->area[0].fbdc_cor_en =
			reg_win_data->reg_area_data[0].fbdc_cor_en;
		win->area[0].fbdc_data_format =
			reg_win_data->reg_area_data[0].fbdc_data_format;
		for (i = 0; i < RK_WIN_MAX_AREA; i++) {
			if (reg_win_data->reg_area_data[i].smem_start > 0) {
				win->area[i].format =
					reg_win_data->reg_area_data[i].data_format;
				win->area[i].data_space =
					reg_win_data->reg_area_data[i].data_space;
				win->area[i].ion_hdl =
					reg_win_data->reg_area_data[i].ion_handle;
				win->area[i].smem_start =
					reg_win_data->reg_area_data[i].smem_start;
				if (inf->disp_mode == DUAL ||
				    inf->disp_mode == DUAL_LCD ||
				    inf->disp_mode == NO_DUAL) {
					win->area[i].xpos =
						reg_win_data->reg_area_data[i].xpos;
					win->area[i].ypos =
						reg_win_data->reg_area_data[i].ypos;
					win->area[i].xsize =
						reg_win_data->reg_area_data[i].xsize;
					win->area[i].ysize =
						reg_win_data->reg_area_data[i].ysize;
				} else {
					win->area[i].xpos =
						reg_win_data->reg_area_data[i].xpos *
						cur_screen->mode.xres /
						primary_screen.mode.xres;
					win->area[i].ypos =
						reg_win_data->reg_area_data[i].ypos *
						cur_screen->mode.yres /
						primary_screen.mode.yres;
					win->area[i].xsize =
						reg_win_data->reg_area_data[i].xsize *
						cur_screen->mode.xres /
						primary_screen.mode.xres;
					win->area[i].ysize =
						reg_win_data->reg_area_data[i].ysize *
						cur_screen->mode.yres /
						primary_screen.mode.yres;

					/* recalc display size if set hdmi scaler when at ONE_DUAL mode */
					if (inf->disp_mode == ONE_DUAL && hdmi_switch_state) {
						if (cur_screen->xsize > 0 &&
						    cur_screen->xsize <= cur_screen->mode.xres) {
							win->area[i].xpos =
								((cur_screen->mode.xres - cur_screen->xsize) >> 1) +
								cur_screen->xsize * win->area[i].xpos / cur_screen->mode.xres;
							win->area[i].xsize =
								win->area[i].xsize * cur_screen->xsize / cur_screen->mode.xres;
						}
						if (cur_screen->ysize > 0 && cur_screen->ysize <= cur_screen->mode.yres) {
							win->area[i].ypos =
								((cur_screen->mode.yres - cur_screen->ysize) >> 1) +
								cur_screen->ysize * win->area[i].ypos / cur_screen->mode.yres;
							win->area[i].ysize =
								win->area[i].ysize * cur_screen->ysize / cur_screen->mode.yres;
						}
					}
				}
				win->area[i].xact =
				    reg_win_data->reg_area_data[i].xact;
				win->area[i].yact =
				    reg_win_data->reg_area_data[i].yact;
				win->area[i].xvir =
				    reg_win_data->reg_area_data[i].xvir;
				win->area[i].yvir =
				    reg_win_data->reg_area_data[i].yvir;
				win->area[i].xoff =
				    reg_win_data->reg_area_data[i].xoff;
				win->area[i].yoff =
				    reg_win_data->reg_area_data[i].yoff;
				win->area[i].y_offset =
				    reg_win_data->reg_area_data[i].y_offset;
				win->area[i].y_vir_stride =
				    reg_win_data->reg_area_data[i].y_vir_stride;
				win->area[i].state = 1;
				if (dev_drv->iommu_enabled) {
					g_now_config_addr[win->id][i] =
						win->area[i].smem_start +
						win->area[i].y_offset;
					g_now_config_state[win->id][i] = 1;
				}
			} else {
				win->area[i].state = 0;
				win->area[i].fbdc_en = 0;
				if (dev_drv->iommu_enabled) {
					g_now_config_addr[win->id][i] = 0;
					g_now_config_state[win->id][i] = 0;
				}
			}
		}
	}
}

static struct rk_fb_reg_win_data *rk_fb_get_win_data(struct rk_fb_reg_data
						     *regs, int win_id)
{
	int i;
	struct rk_fb_reg_win_data *win_data = NULL;

	for (i = 0; i < regs->win_num; i++) {
		if (regs->reg_win_data[i].win_id == win_id) {
			win_data = &(regs->reg_win_data[i]);
			break;
		}
	}

	return win_data;
}

static int rk_fb_reg_effect(struct rk_lcdc_driver *dev_drv,
			    struct rk_fb_reg_data *regs,
			    int count)
{
	int i, j, wait_for_vsync = false;
	unsigned int dsp_addr[5][4];
	int win_status = 0;

	if (dev_drv->ops->get_dsp_addr)
		dev_drv->ops->get_dsp_addr(dev_drv, dsp_addr);

	for (i = 0; i < dev_drv->lcdc_win_num; i++) {
		for (j = 0; j < RK_WIN_MAX_AREA; j++) {
			if ((j > 0) && (dev_drv->area_support[i] == 1))
				continue;
			if (dev_drv->win[i]->area[j].state == 1) {
				u32 new_start =
					dev_drv->win[i]->area[j].smem_start +
					dev_drv->win[i]->area[j].y_offset;
				u32 reg_start = dsp_addr[i][j];

				if (unlikely(new_start != reg_start)) {
					wait_for_vsync = true;
					dev_info(dev_drv->dev,
						 "win%d:new_addr:0x%08x cur_addr:0x%08x--%d\n",
						 i, new_start, reg_start,
						 101 - count);
					break;
				}
			} else if (dev_drv->win[i]->area[j].state == 0) {
				if (dev_drv->ops->get_win_state) {
					win_status =
					dev_drv->ops->get_win_state(dev_drv, i, j);
					if (win_status) {
						wait_for_vsync = true;
						dev_info(dev_drv->dev,
							 "win[%d]area[%d]: "
							 "state: %d, "
							 "cur state: %d,"
							 "count: %d\n",
							 i, j,
							 dev_drv->win[i]->area[j].state,
							 win_status,
							 101 - count);
					}
				}
			} else {
				pr_err("!!!win[%d]state:%d,error!!!\n",
				       i, dev_drv->win[i]->state);
			}
		}
	}

	return wait_for_vsync;
}

static int rk_fb_iommu_page_fault_dump(struct rk_lcdc_driver *dev_drv)
{
	int i, j, state, page_fault = 0;
	unsigned int dsp_addr[5][4];

	if (dev_drv->ops->extern_func) {
		dev_drv->ops->extern_func(dev_drv, UNMASK_PAGE_FAULT);
		page_fault = dev_drv->ops->extern_func(dev_drv, GET_PAGE_FAULT);
	}
	if (page_fault) {
		pr_info("last config:\n");
		for (i = 0; i < dev_drv->lcdc_win_num; i++) {
			for (j = 0; j < RK_WIN_MAX_AREA; j++) {
				if ((j > 0) && (dev_drv->area_support[i] == 1))
					continue;
				pr_info("win[%d]area[%d],state=%d,addr=0x%08x\n",
					i, j, g_last_state[i][j], g_last_addr[i][j]);
			}
		}

		pr_info("last freed buffer:\n");
		for (i = 0; (freed_addr[i] != 0xfefefefe) && freed_addr[i]; i++)
			pr_info("%d:0x%08x\n", i, freed_addr[i]);

		dev_drv->ops->get_dsp_addr(dev_drv, dsp_addr);
		pr_info("vop now state:\n");
		for (i = 0; i < dev_drv->lcdc_win_num; i++) {
			for (j = 0; j < RK_WIN_MAX_AREA; j++) {
				if ((j > 0) && (dev_drv->area_support[i] == 1))
					continue;
				state = dev_drv->ops->get_win_state(dev_drv, i, j);
				pr_info("win[%d]area[%d],state=%d,addr=0x%08x\n",
					i, j, state, dsp_addr[i][j]);
			}
		}
		pr_info("now config:\n");
		for (i = 0; i < dev_drv->lcdc_win_num; i++) {
			for (j = 0; j < RK_WIN_MAX_AREA; j++) {
				if ((j > 0) && (dev_drv->area_support[i] == 1))
					continue;
				pr_info("win[%d]area[%d],state=%d,addr=0x%08x\n",
					i, j, g_now_config_state[i][j],
					g_now_config_addr[i][j]);
			}
		}
		for (i = 0; i < DUMP_FRAME_NUM; i++)
			rk_fb_config_debug(dev_drv, &dev_drv->tmp_win_cfg[i],
					   &dev_drv->tmp_regs[i], 0);
	}

	for (i = 0; i < dev_drv->lcdc_win_num; i++) {
		for (j = 0; j < RK_WIN_MAX_AREA; j++) {
			if ((j > 0) && (dev_drv->area_support[i] == 1))
				continue;
			g_last_addr[i][j] = g_now_config_addr[i][j];
			g_last_state[i][j] = g_now_config_state[i][j];
		}
	}

	return page_fault;
}
static void rk_fb_update_reg(struct rk_lcdc_driver *dev_drv,
			     struct rk_fb_reg_data *regs)
{
	int i, j;
	struct rk_lcdc_win *win;
	ktime_t timestamp = dev_drv->vsync_info.timestamp;
	struct rk_fb_reg_win_data *win_data;
	bool wait_for_vsync;
	int count = 100;
	long timeout;
	int pagefault = 0;

	if (dev_drv->suspend_flag == 1) {
#ifdef H_USE_FENCE
		sw_sync_timeline_inc(dev_drv->timeline, 1);
#endif
		for (i = 0; i < regs->win_num; i++) {
			win_data = &regs->reg_win_data[i];
			rk_fb_free_dma_buf(dev_drv, win_data);
		}
		if (dev_drv->property.feature & SUPPORT_WRITE_BACK)
			rk_fb_free_wb_buf(dev_drv, &regs->reg_wb_data);
		kfree(regs);
		return;
	}
	/* acq_fence wait */
	for (i = 0; i < regs->win_num; i++) {
		win_data = &regs->reg_win_data[i];
		for (j = 0; j < RK_WIN_MAX_AREA; j++) {
			if (win_data->reg_area_data[j].acq_fence)
				rk_fd_fence_wait(dev_drv, win_data->reg_area_data[j].acq_fence);
		}
	}

	mutex_lock(&dev_drv->win_config);
	for (i = 0; i < dev_drv->lcdc_win_num; i++) {
		win = dev_drv->win[i];
		win_data = rk_fb_get_win_data(regs, i);
		if (win_data) {
			rk_fb_update_win(dev_drv, win, win_data);
			win->state = 1;
			dev_drv->ops->set_par(dev_drv, i);
			dev_drv->ops->pan_display(dev_drv, i);
		} else {
			win->z_order = -1;
			win->state = 0;
			for (j = 0; j < 4; j++) {
				win->area[j].state = 0;
				win->area[j].fbdc_en = 0;
			}
			if (dev_drv->iommu_enabled) {
				for (j = 0; j < 4; j++) {
					g_now_config_addr[i][j] = 0;
					g_now_config_state[i][j] = 0;
				}
			}
		}
	}
	dev_drv->ops->ovl_mgr(dev_drv, 0, 1);

	if (dev_drv->property.feature & SUPPORT_WRITE_BACK) {
		memcpy(&dev_drv->wb_data, &regs->reg_wb_data,
		       sizeof(struct rk_fb_reg_wb_data));
		if (dev_drv->ops->set_wb)
			dev_drv->ops->set_wb(dev_drv);
	}

	if (rk_fb_iommu_debug > 0)
		pagefault = rk_fb_iommu_page_fault_dump(dev_drv);

	if (pagefault == 0)
		dev_drv->ops->cfg_done(dev_drv);
	else
		sw_sync_timeline_inc(dev_drv->timeline, 1);
	mutex_unlock(&dev_drv->win_config);

	do {
		timestamp = dev_drv->vsync_info.timestamp;
		timeout = wait_event_interruptible_timeout(dev_drv->vsync_info.wait,
				ktime_compare(dev_drv->vsync_info.timestamp, timestamp) > 0,
				msecs_to_jiffies(50));
		if (timeout <= 0)
			dev_info(dev_drv->dev, "timeout: %ld\n", timeout);
		wait_for_vsync = rk_fb_reg_effect(dev_drv, regs, count);
	} while (wait_for_vsync && count--);
#ifdef H_USE_FENCE
	sw_sync_timeline_inc(dev_drv->timeline, 1);
#endif

	if (dev_drv->front_regs) {
		if (dev_drv->iommu_enabled) {
			if (dev_drv->ops->mmu_en)
				dev_drv->ops->mmu_en(dev_drv);
			freed_index = 0;
			g_last_timeout = timeout;
		}

		mutex_lock(&dev_drv->front_lock);

		for (i = 0; i < dev_drv->front_regs->win_num; i++) {
			win_data = &dev_drv->front_regs->reg_win_data[i];
			rk_fb_free_dma_buf(dev_drv, win_data);
		}
		if (dev_drv->property.feature & SUPPORT_WRITE_BACK)
			rk_fb_free_wb_buf(dev_drv,
					  &dev_drv->front_regs->reg_wb_data);
		kfree(dev_drv->front_regs);

		mutex_unlock(&dev_drv->front_lock);

		if (dev_drv->iommu_enabled)
			freed_addr[freed_index] = 0xfefefefe;
	}

	mutex_lock(&dev_drv->front_lock);

	dev_drv->front_regs = regs;

	mutex_unlock(&dev_drv->front_lock);

	trace_buffer_dump(&fb_pdev->dev, dev_drv);
}

static void rk_fb_update_regs_handler(struct kthread_work *work)
{
	struct rk_lcdc_driver *dev_drv =
	    container_of(work, struct rk_lcdc_driver, update_regs_work);
	struct rk_fb_reg_data *data, *next;

	mutex_lock(&dev_drv->update_regs_list_lock);
	dev_drv->saved_list = dev_drv->update_regs_list;
	list_replace_init(&dev_drv->update_regs_list, &dev_drv->saved_list);
	mutex_unlock(&dev_drv->update_regs_list_lock);

	list_for_each_entry_safe(data, next, &dev_drv->saved_list, list) {
		list_del(&data->list);
		rk_fb_update_reg(dev_drv, data);
	}

	if (dev_drv->wait_fs && list_empty(&dev_drv->update_regs_list))
		wake_up(&dev_drv->update_regs_wait);
}

static int rk_fb_check_config_var(struct rk_fb_area_par *area_par,
				  struct rk_screen *screen)
{
	if (area_par->phy_addr > 0)
		pr_err("%s[%d], phy_addr = 0x%x\n",
		       __func__, __LINE__, area_par->phy_addr);
	if ((area_par->x_offset + area_par->xact > area_par->xvir) ||
	    (area_par->xact <= 0) || (area_par->yact <= 0) ||
	    (area_par->xvir <= 0) || (area_par->yvir <= 0)) {
		pr_err("check config var fail 0:\n"
		       "x_offset=%d,xact=%d,xvir=%d\n",
		       area_par->x_offset, area_par->xact, area_par->xvir);
		return -EINVAL;
	}

	if ((area_par->xpos >= screen->mode.xres) ||
	    (area_par->ypos >= screen->mode.yres) ||
	    ((area_par->xsize <= 0) || (area_par->ysize <= 0))) {
		pr_warn("check config var fail 1:\n"
			"xpos=%d,xsize=%d,xres=%d\n"
			"ypos=%d,ysize=%d,yres=%d\n",
			area_par->xpos, area_par->xsize, screen->mode.xres,
			area_par->ypos, area_par->ysize, screen->mode.yres);
		return -EINVAL;
	}
	return 0;
}

static int rk_fb_config_debug(struct rk_lcdc_driver *dev_drv,
			      struct rk_fb_win_cfg_data *win_data,
			      struct rk_fb_reg_data *regs, u32 cmd)
{
	int i, j;
	struct rk_fb_win_par *win_par;
	struct rk_fb_area_par *area_par;
	struct rk_fb_reg_win_data *reg_win_data;
	struct rk_fb_reg_area_data *area_data;

	rk_fb_dbg(cmd, "-------------frame start-------------\n");
	rk_fb_dbg(cmd, "user config:\n");
	for (i = 0; i < dev_drv->lcdc_win_num; i++) {
		win_par = &(win_data->win_par[i]);
		if ((win_par->area_par[0].ion_fd <= 0) &&
		    (win_par->area_par[0].phy_addr <= 0))
			continue;
		rk_fb_dbg(cmd, "win[%d]:z_order=%d,galhpa_v=%d\n",
			  win_par->win_id, win_par->z_order,
			  win_par->g_alpha_val);
		for (j = 0; j < RK_WIN_MAX_AREA; j++) {
			area_par = &(win_par->area_par[j]);
			if (((j > 0) && (dev_drv->area_support[i] == 1)) ||
			    ((win_par->area_par[j].ion_fd <= 0) &&
			     (win_par->area_par[j].phy_addr <= 0)))
				continue;
			rk_fb_dbg(cmd, " area[%d]:fmt=%d,ion_fd=%d,phy_add=0x%x,xoff=%d,yoff=%d\n",
				  j, area_par->data_format, area_par->ion_fd,
				  area_par->phy_addr, area_par->x_offset,
				  area_par->y_offset);
			rk_fb_dbg(cmd, "	   xpos=%d,ypos=%d,xsize=%d,ysize=%d\n",
				  area_par->xpos, area_par->ypos,
				  area_par->xsize, area_par->ysize);
			rk_fb_dbg(cmd, "	   xact=%d,yact=%d,xvir=%d,yvir=%d\n",
				  area_par->xact, area_par->yact,
				  area_par->xvir, area_par->yvir);
			rk_fb_dbg(cmd, "	   data_space%d\n",
				  area_par->data_space);
		}
	}

	rk_fb_dbg(cmd, "regs data:\n");
	rk_fb_dbg(cmd, "win_num=%d,buf_num=%d\n",
		  regs->win_num, regs->buf_num);
	for (i = 0; i < dev_drv->lcdc_win_num; i++) {
		reg_win_data = &(regs->reg_win_data[i]);
		if (reg_win_data->reg_area_data[0].smem_start <= 0)
			continue;
		rk_fb_dbg(cmd, "win[%d]:z_order=%d,area_num=%d,area_buf_num=%d\n",
			  reg_win_data->win_id, reg_win_data->z_order,
			  reg_win_data->area_num, reg_win_data->area_buf_num);
		for (j = 0; j < RK_WIN_MAX_AREA; j++) {
			area_data = &(reg_win_data->reg_area_data[j]);
			if (((j > 0) && (dev_drv->area_support[i] == 1)) ||
			    (area_data->smem_start <= 0))
				continue;
			rk_fb_dbg(cmd, " area[%d]:fmt=%d,ion=%p,smem_star=0x%lx,cbr_star=0x%lx\n",
				  j, area_data->data_format, area_data->ion_handle,
				  area_data->smem_start, area_data->cbr_start);
			rk_fb_dbg(cmd, "	   yoff=0x%x,coff=0x%x,area_data->buff_len=%x\n",
				  area_data->y_offset, area_data->c_offset, area_data->buff_len);
			rk_fb_dbg(cmd, "	   xpos=%d,ypos=%d,xsize=%d,ysize=%d\n",
				  area_data->xpos, area_data->ypos,
				  area_data->xsize, area_data->ysize);
			rk_fb_dbg(cmd, "	   xact=%d,yact=%d,xvir=%d,yvir=%d\n",
				  area_data->xact, area_data->yact,
				  area_data->xvir, area_data->yvir);
		}
	}
	rk_fb_dbg(cmd, "-------------frame end---------------\n");

	return 0;
}
static int rk_fb_config_backup(struct rk_lcdc_driver *dev_drv,
			       struct rk_fb_win_cfg_data *win_cfg,
			       struct rk_fb_reg_data *regs)
{
	int i;

	/*2->1->0: 0 is newest*/
	for (i = 0; i < DUMP_FRAME_NUM - 1; i++) {
		memcpy(&dev_drv->tmp_win_cfg[DUMP_FRAME_NUM - 1 - i],
		       &dev_drv->tmp_win_cfg[DUMP_FRAME_NUM - 2 - i],
		       sizeof(struct rk_fb_win_cfg_data));
		memcpy(&dev_drv->tmp_regs[DUMP_FRAME_NUM - 1 - i],
		       &dev_drv->tmp_regs[DUMP_FRAME_NUM - 2 - i],
		       sizeof(struct rk_fb_reg_data));
	}

	memcpy(&dev_drv->tmp_win_cfg[0], win_cfg,
	       sizeof(struct rk_fb_win_cfg_data));
	memcpy(&dev_drv->tmp_regs[0], regs,
	       sizeof(struct rk_fb_reg_data));

	return 0;
}

static int rk_fb_set_wb_buffer(struct fb_info *info,
			       struct rk_fb_wb_cfg *wb_cfg,
			       struct rk_fb_reg_wb_data *wb_data)
{
	int ret = 0;
	ion_phys_addr_t phy_addr;
	size_t len;
	u8 fb_data_fmt;
	struct rk_fb_par *fb_par = (struct rk_fb_par *)info->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;
	struct rk_fb *rk_fb = dev_get_drvdata(info->device);

	if ((wb_cfg->phy_addr == 0) && (wb_cfg->ion_fd == 0)) {
		wb_data->state = 0;
		return 0;
	}
	if (wb_cfg->phy_addr == 0) {
		wb_data->ion_handle =
		    ion_import_dma_buf(rk_fb->ion_client,
				       wb_cfg->ion_fd);
		if (IS_ERR(wb_data->ion_handle)) {
			pr_info("Could not import handle: %ld\n",
				(long)wb_data->ion_handle);
			return -EINVAL;
		}
		if (dev_drv->iommu_enabled)
			ret = ion_map_iommu(dev_drv->dev,
					    rk_fb->ion_client,
					    wb_data->ion_handle,
					    (unsigned long *)&phy_addr,
					    (unsigned long *)&len);
		else
			ret = ion_phys(rk_fb->ion_client, wb_data->ion_handle,
				       &phy_addr, &len);
		if (ret < 0) {
			pr_err("ion map to get phy addr failed\n");
			ion_free(rk_fb->ion_client, wb_data->ion_handle);
			return -ENOMEM;
		}
		wb_data->smem_start = phy_addr;
	} else {
		wb_data->smem_start = wb_cfg->phy_addr;
	}

	fb_data_fmt = rk_fb_data_fmt(wb_cfg->data_format, 0);
	if (IS_YUV_FMT(fb_data_fmt))
		wb_data->cbr_start = wb_data->smem_start +
					wb_cfg->xsize * wb_cfg->ysize;
	wb_data->xsize = wb_cfg->xsize;
	wb_data->ysize = wb_cfg->ysize;
	wb_data->data_format = fb_data_fmt;
	wb_data->state = 1;

	return 0;
}

static int rk_fb_set_win_buffer(struct fb_info *info,
				struct rk_fb_win_par *win_par,
				struct rk_fb_reg_win_data *reg_win_data)
{
	struct rk_fb *rk_fb = dev_get_drvdata(info->device);
	struct fb_fix_screeninfo *fix = &info->fix;
	struct rk_fb_par *fb_par = (struct rk_fb_par *)info->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;
	/*if hdmi size move to hwc,screen should point to cur_screen
	 *otherwise point to screen0[main screen]*/
	struct rk_screen *screen = dev_drv->cur_screen;/*screen0;*/
	struct fb_info *fbi;
	int i, ion_fd, acq_fence_fd;
	u32 xvir = 0, yvir = 0;
	u32 xoffset = 0, yoffset = 0;

	struct ion_handle *hdl;
	size_t len;
	int index_buf = 0;
	u8 fb_data_fmt = 0;
	u8 pixel_width = 0;
	u32 vir_width_bit = 0;
	u32 stride = 0, uv_stride = 0;
	u32 stride_32bit_1 = 0;
	u32 stride_32bit_2 = 0;
	u16 uv_x_off = 0, uv_y_off = 0, uv_y_act = 0;
	u8 is_pic_yuv = 0;
	u8 ppixel_a = 0, global_a = 0;
	ion_phys_addr_t phy_addr;
	int ret = 0;
	int buff_len = 0;

	reg_win_data->reg_area_data[0].smem_start = -1;
	reg_win_data->area_num = 0;
	fbi = rk_fb->fb[win_par->win_id + dev_drv->fb_index_base];
	if (win_par->area_par[0].phy_addr == 0) {
		for (i = 0; i < RK_WIN_MAX_AREA; i++) {
			ion_fd = win_par->area_par[i].ion_fd;
			if (ion_fd > 0) {
				hdl =
				    ion_import_dma_buf(rk_fb->ion_client,
						       ion_fd);
				if (IS_ERR(hdl)) {
					pr_info("%s: win[%d]area[%d] can't import handle\n",
						__func__, win_par->win_id, i);
					pr_info("fd: %d, hdl: 0x%p, ion_client: 0x%p\n",
						ion_fd, hdl, rk_fb->ion_client);
					return -EINVAL;
					break;
				}
				reg_win_data->reg_area_data[i].ion_handle = hdl;
				if (dev_drv->iommu_enabled)
					ret = ion_map_iommu(dev_drv->dev,
							    rk_fb->ion_client,
							    hdl,
							    (unsigned long *)&phy_addr,
							    (unsigned long *)&len);
				else
					ret = ion_phys(rk_fb->ion_client, hdl,
						       &phy_addr, &len);
				if (ret < 0) {
					dev_err(fbi->dev, "ion map to get phy addr failed\n");
					ion_free(rk_fb->ion_client, hdl);
					return -ENOMEM;
				}
				reg_win_data->reg_area_data[i].smem_start = phy_addr;
				reg_win_data->area_num++;
				reg_win_data->area_buf_num++;
				reg_win_data->reg_area_data[i].index_buf = 1;
				reg_win_data->reg_area_data[i].buff_len = len;
			}
		}
	} else {
		reg_win_data->reg_area_data[0].smem_start =
		    win_par->area_par[0].phy_addr;
		reg_win_data->area_num = 1;
		reg_win_data->area_buf_num++;
		fbi->screen_base = phys_to_virt(win_par->area_par[0].phy_addr);
	}

	if (reg_win_data->area_num == 0) {
		for (i = 0; i < RK_WIN_MAX_AREA; i++)
			reg_win_data->reg_area_data[i].smem_start = 0;
		reg_win_data->z_order = -1;
		reg_win_data->win_id = -1;
		return 0;
	}

	for (i = 0; i < reg_win_data->area_num; i++) {
		acq_fence_fd = win_par->area_par[i].acq_fence_fd;
		index_buf = reg_win_data->reg_area_data[i].index_buf;
		if ((acq_fence_fd > 0) && (index_buf == 1)) {
			reg_win_data->reg_area_data[i].acq_fence =
			    sync_fence_fdget(win_par->area_par[i].acq_fence_fd);
		}
	}
	if (reg_win_data->reg_area_data[0].smem_start > 0) {
		reg_win_data->z_order = win_par->z_order;
		reg_win_data->win_id = win_par->win_id;
	} else {
		reg_win_data->z_order = -1;
		reg_win_data->win_id = -1;
	}

	reg_win_data->mirror_en = win_par->mirror_en;
	for (i = 0; i < reg_win_data->area_num; i++) {
		u8 data_format = win_par->area_par[i].data_format;
		/*rk_fb_check_config_var(&win_par->area_par[i], screen);*/
		reg_win_data->colorspace = CSC_FORMAT(data_format);
		data_format &= ~CSC_MASK;
		fb_data_fmt = rk_fb_data_fmt(data_format, 0);
		reg_win_data->reg_area_data[i].data_format = fb_data_fmt;
		reg_win_data->reg_area_data[i].data_space =
					win_par->area_par[i].data_space;
		if (IS_FBDC_FMT(fb_data_fmt)) {
			reg_win_data->reg_area_data[i].fbdc_en = 1;
			reg_win_data->reg_area_data[i].fbdc_cor_en = 1;
		} else {
			reg_win_data->reg_area_data[i].fbdc_en = 0;
			reg_win_data->reg_area_data[i].fbdc_cor_en = 0;
		}
		pixel_width = rk_fb_pixel_width(fb_data_fmt);

		ppixel_a |= ((fb_data_fmt == ARGB888) ||
			     (fb_data_fmt == FBDC_ARGB_888) ||
			     (fb_data_fmt == FBDC_ABGR_888) ||
			     (fb_data_fmt == ABGR888)) ? 1 : 0;
		/*act_height should be 2 pix align for interlace output*/
		if (win_par->area_par[i].yact % 2 == 1) {
			win_par->area_par[i].yact  -= 1;
			win_par->area_par[i].ysize -= 1;
		}

		/* buf offset should be 2 pix align*/
		if ((win_par->area_par[i].x_offset % 2 == 1) &&
		    IS_YUV_FMT(fb_data_fmt)) {
			win_par->area_par[i].x_offset += 1;
			win_par->area_par[i].xact -= 1;
		}

		/* visiable pos in panel */
		reg_win_data->reg_area_data[i].xpos = win_par->area_par[i].xpos;
		reg_win_data->reg_area_data[i].ypos = win_par->area_par[i].ypos;

		/* realy size in panel */
		reg_win_data->reg_area_data[i].xsize = win_par->area_par[i].xsize;
		reg_win_data->reg_area_data[i].ysize = win_par->area_par[i].ysize;

		/* realy size in panel */
		reg_win_data->reg_area_data[i].xact = win_par->area_par[i].xact;
		reg_win_data->reg_area_data[i].yact = win_par->area_par[i].yact;

		xoffset = win_par->area_par[i].x_offset;	/* buf offset */
		yoffset = win_par->area_par[i].y_offset;
		reg_win_data->reg_area_data[i].xoff = xoffset;
		reg_win_data->reg_area_data[i].yoff = yoffset;

		xvir = win_par->area_par[i].xvir;
		reg_win_data->reg_area_data[i].xvir = xvir;
		yvir = win_par->area_par[i].yvir;
		reg_win_data->reg_area_data[i].yvir = yvir;

		vir_width_bit = pixel_width * xvir;
		/* pixel_width = byte_num*8 */
		stride_32bit_1 = ((vir_width_bit + 31) & (~31)) / 8;
		stride_32bit_2 = ((vir_width_bit * 2 + 31) & (~31)) / 8;

		stride = stride_32bit_1;	/* default rgb */
		fix->line_length = stride;
		reg_win_data->reg_area_data[i].y_vir_stride = stride >> 2;

		/* x y mirror ,jump line
		 * reg_win_data->reg_area_data[i].y_offset =
		 *		yoffset*stride+xoffset*pixel_width/8;
		 */
		if (screen->y_mirror || reg_win_data->mirror_en) {
			if (screen->interlace == 1) {
				reg_win_data->reg_area_data[i].y_offset =
				    yoffset * stride * 2 +
				    ((reg_win_data->reg_area_data[i].yact - 1) * 2 + 1) * stride +
				    xoffset * pixel_width / 8;
			} else {
				reg_win_data->reg_area_data[i].y_offset =
				    yoffset * stride +
				    (reg_win_data->reg_area_data[i].yact - 1) * stride +
				    xoffset * pixel_width / 8;
			}
		} else {
			if (screen->interlace == 1) {
				reg_win_data->reg_area_data[i].y_offset =
				    yoffset * stride * 2 +
				    xoffset * pixel_width / 8;
			} else {
				reg_win_data->reg_area_data[i].y_offset =
				    yoffset * stride +
				    xoffset * pixel_width / 8;
			}
		}
		if (IS_RGB_FMT(fb_data_fmt) && dev_drv->iommu_enabled) {
			buff_len = yoffset * stride +
				xoffset * pixel_width / 8 +
				reg_win_data->reg_area_data[i].xvir *
				reg_win_data->reg_area_data[i].yact *
				pixel_width / 8 -
				reg_win_data->reg_area_data[i].xoff*
				pixel_width / 8;
			if (buff_len > reg_win_data->reg_area_data[i].buff_len)
				pr_err("\n!!!!!!error: fmt=%d,xvir[%d]*"
				       "yact[%d]*bpp[%d]"
				       "=buff_len[0x%x]>>mmu len=0x%x\n",
				       fb_data_fmt,
				       reg_win_data->reg_area_data[i].xvir,
				       reg_win_data->reg_area_data[i].yact,
				       pixel_width, buff_len,
				       reg_win_data->reg_area_data[i].buff_len);
		}
	}

	global_a = (win_par->g_alpha_val == 0) ? 0 : 1;
	reg_win_data->alpha_en = ppixel_a | global_a;
	reg_win_data->g_alpha_val = win_par->g_alpha_val;
	reg_win_data->alpha_mode = win_par->alpha_mode;

	switch (fb_data_fmt) {
	case YUV422:
	case YUV422_A:
		is_pic_yuv = 1;
		stride = stride_32bit_1;
		uv_stride = stride_32bit_1;
		uv_x_off = xoffset;
		uv_y_off = yoffset;
		fix->line_length = stride;
		uv_y_act = win_par->area_par[0].yact >> 1;
		break;
	case YUV420:		/* nv12 */
	case YUV420_NV21:	/* nv21 */
	case YUV420_A:
		is_pic_yuv = 1;
		stride = stride_32bit_1;
		uv_stride = stride_32bit_1;
		uv_x_off = xoffset;
		uv_y_off = yoffset >> 1;
		fix->line_length = stride;
		uv_y_act = win_par->area_par[0].yact >> 1;
		break;
	case YUV444:
	case YUV444_A:
		is_pic_yuv = 1;
		stride = stride_32bit_1;
		uv_stride = stride_32bit_2;
		uv_x_off = xoffset * 2;
		uv_y_off = yoffset;
		fix->line_length = stride << 2;
		uv_y_act = win_par->area_par[0].yact;
		break;
	default:
		break;
	}
	if (is_pic_yuv == 1) {
		reg_win_data->reg_area_data[0].cbr_start =
		    reg_win_data->reg_area_data[0].smem_start + xvir * yvir;
		reg_win_data->reg_area_data[0].uv_vir_stride = uv_stride >> 2;
		if ((screen->y_mirror == 1) || (reg_win_data->mirror_en)) {
			if (screen->interlace == 1) {
				reg_win_data->reg_area_data[0].c_offset =
				    uv_y_off * uv_stride * 2 +
				    ((uv_y_act - 1) * 2 + 1) * uv_stride +
				    uv_x_off * pixel_width / 8;
			} else {
				reg_win_data->reg_area_data[0].c_offset =
				    uv_y_off * uv_stride +
				    (uv_y_act - 1) * uv_stride +
				    uv_x_off * pixel_width / 8;
			}
		} else {
			if (screen->interlace == 1) {
				reg_win_data->reg_area_data[0].c_offset =
				    uv_y_off * uv_stride * 2 +
				    uv_x_off * pixel_width / 8;
			} else {
				reg_win_data->reg_area_data[0].c_offset =
				    uv_y_off * uv_stride +
				    uv_x_off * pixel_width / 8;
			}
		}
		buff_len = reg_win_data->reg_area_data[0].cbr_start +
			uv_y_off * uv_stride + uv_x_off * pixel_width / 8 +
			reg_win_data->reg_area_data[0].xvir *
			reg_win_data->reg_area_data[0].yact *
			pixel_width / 16 -
			reg_win_data->reg_area_data[0].smem_start -
			reg_win_data->reg_area_data[0].xoff*
			pixel_width / 16;
		if ((buff_len > reg_win_data->reg_area_data[0].buff_len) &&
		     dev_drv->iommu_enabled)
			pr_err("\n!!!!!!error: fmt=%d,xvir[%d]*"
			       "yact[%d]*bpp[%d]"
			       "=buff_len[0x%x]>>mmu len=0x%x\n",
			       fb_data_fmt,
			       reg_win_data->reg_area_data[0].xvir,
			       reg_win_data->reg_area_data[0].yact,
			       pixel_width, buff_len,
			       reg_win_data->reg_area_data[0].buff_len);
	}

	/* record buffer information for rk_fb_disp_scale to prevent fence
	 * timeout because rk_fb_disp_scale will call function
	 * info->fbops->fb_set_par(info);
	 * delete by hjc for new hdmi overscan framework.
	 */
	/* info->var.yoffset = yoffset;
	 * info->var.xoffset = xoffset;
	 */
	return 0;
}

static int rk_fb_set_win_config(struct fb_info *info,
				struct rk_fb_win_cfg_data *win_data)
{
	struct rk_fb_par *fb_par = (struct rk_fb_par *)info->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;
	struct rk_fb_reg_data *regs;
#ifdef H_USE_FENCE
	struct sync_fence *release_fence[RK_MAX_BUF_NUM];
	struct sync_fence *retire_fence;
	struct sync_pt *release_sync_pt[RK_MAX_BUF_NUM];
	struct sync_pt *retire_sync_pt;
	char fence_name[20];
#endif
	int ret = 0, i, j = 0;
	int list_is_empty = 0;
	struct rk_screen *screen = dev_drv->cur_screen;

	mutex_lock(&dev_drv->output_lock);

	for (i = 0; i < 4; i++) {
		for (j = 0; j < 4; j++) {
			if ((win_data->win_par[i].area_par[j].ion_fd > 0) ||
			    (win_data->win_par[i].area_par[j].phy_addr > 0))
				ret += rk_fb_check_config_var(
					&win_data->win_par[i].area_par[j],
					screen);
		}
	}
	if ((dev_drv->suspend_flag) || (dev_drv->hdmi_switch) || (ret < 0)) {
		dev_drv->timeline_max++;
		sw_sync_timeline_inc(dev_drv->timeline, 1);
		if (dev_drv->suspend_flag)
			pr_err("suspend_flag=%d\n", dev_drv->suspend_flag);
		else if (dev_drv->hdmi_switch)
			pr_err("hdmi switch = %d\n", dev_drv->hdmi_switch);
		else
			pr_err("error config ,ignore\n");
		for (j = 0; j < RK_MAX_BUF_NUM; j++)
			win_data->rel_fence_fd[j] = -1;
		win_data->ret_fence_fd = -1;
		goto err;
	}

	regs = kzalloc(sizeof(struct rk_fb_reg_data), GFP_KERNEL);
	if (!regs) {
		pr_info("could not allocate rk_fb_reg_data\n");
		ret = -ENOMEM;
		goto err;
	}

	for (i = 0, j = 0; i < dev_drv->lcdc_win_num; i++) {
		if (win_data->win_par[i].win_id < dev_drv->lcdc_win_num) {
			if (rk_fb_set_win_buffer(info, &win_data->win_par[i],
						 &regs->reg_win_data[j])) {
				ret = -ENOMEM;
				pr_info("error:%s[%d]\n", __func__, __LINE__);
				goto err2;
			}
			if (regs->reg_win_data[j].area_num > 0) {
				regs->win_num++;
				regs->buf_num +=
				    regs->reg_win_data[j].area_buf_num;
			}
			j++;
		} else {
			pr_info("error:win_id bigger than lcdc_win_num\n");
			pr_info("i=%d,win_id=%d\n", i,
				win_data->win_par[i].win_id);
		}
	}
	if (dev_drv->property.feature & SUPPORT_WRITE_BACK)
		rk_fb_set_wb_buffer(info, &win_data->wb_cfg,
				    &regs->reg_wb_data);
	if (regs->win_num <= 0)
		goto err_null_frame;

	dev_drv->timeline_max++;
#ifdef H_USE_FENCE
	win_data->ret_fence_fd = get_unused_fd_flags(0);
	if (win_data->ret_fence_fd < 0) {
		pr_err("ret_fence_fd=%d\n", win_data->ret_fence_fd);
		win_data->ret_fence_fd = -1;
		ret = -EFAULT;
		goto err2;
	}
	for (i = 0; i < RK_MAX_BUF_NUM; i++) {
		if (i < regs->buf_num) {
			sprintf(fence_name, "fence%d", i);
			win_data->rel_fence_fd[i] = get_unused_fd_flags(0);
			if (win_data->rel_fence_fd[i] < 0) {
				pr_info("rel_fence_fd=%d\n",
					win_data->rel_fence_fd[i]);
				ret = -EFAULT;
				goto err2;
			}
			release_sync_pt[i] =
			    sw_sync_pt_create(dev_drv->timeline,
					      dev_drv->timeline_max);
			release_fence[i] =
			    sync_fence_create(fence_name, release_sync_pt[i]);
			sync_fence_install(release_fence[i],
					   win_data->rel_fence_fd[i]);
		} else {
			win_data->rel_fence_fd[i] = -1;
		}
	}

	retire_sync_pt =
	    sw_sync_pt_create(dev_drv->timeline, dev_drv->timeline_max);
	retire_fence = sync_fence_create("ret_fence", retire_sync_pt);
	sync_fence_install(retire_fence, win_data->ret_fence_fd);
#else
	for (i = 0; i < RK_MAX_BUF_NUM; i++)
		win_data->rel_fence_fd[i] = -1;

	win_data->ret_fence_fd = -1;
#endif
	if (dev_drv->wait_fs == 0) {
		mutex_lock(&dev_drv->update_regs_list_lock);
		list_add_tail(&regs->list, &dev_drv->update_regs_list);
		mutex_unlock(&dev_drv->update_regs_list_lock);
		queue_kthread_work(&dev_drv->update_regs_worker,
				   &dev_drv->update_regs_work);
	} else {
		mutex_lock(&dev_drv->update_regs_list_lock);
		list_is_empty = list_empty(&dev_drv->update_regs_list) &&
					list_empty(&dev_drv->saved_list);
		mutex_unlock(&dev_drv->update_regs_list_lock);
		if (!list_is_empty) {
			ret = wait_event_timeout(dev_drv->update_regs_wait,
				list_empty(&dev_drv->update_regs_list) && list_empty(&dev_drv->saved_list),
				msecs_to_jiffies(60));
			if (ret > 0)
				rk_fb_update_reg(dev_drv, regs);
			else
				pr_info("%s: wait update_regs_wait timeout\n", __func__);
		} else if (ret == 0) {
			rk_fb_update_reg(dev_drv, regs);
		}
	}
	if (rk_fb_debug_lvl > 0)
		rk_fb_config_debug(dev_drv, win_data, regs, rk_fb_debug_lvl);
	if (rk_fb_iommu_debug > 0)
		rk_fb_config_backup(dev_drv, win_data, regs);
err:
	mutex_unlock(&dev_drv->output_lock);
	return ret;
err_null_frame:
	for (j = 0; j < RK_MAX_BUF_NUM; j++)
		win_data->rel_fence_fd[j] = -1;
	win_data->ret_fence_fd = -1;
	pr_info("win num = %d,null frame\n", regs->win_num);
err2:
	rk_fb_config_debug(dev_drv, win_data, regs, 0);
	kfree(regs);
	mutex_unlock(&dev_drv->output_lock);

	return ret;
}

#if 1
static int cfgdone_distlist[10] = { 0 };

static int cfgdone_index;
static int cfgdone_lasttime;

int rk_get_real_fps(int before)
{
	struct timespec now;
	int dist_curr;
	int dist_total = 0;
	int dist_count = 0;
	int dist_first = 0;

	int index = cfgdone_index;
	int i = 0, fps = 0;
	int total;

	if (before > 100)
		before = 100;
	if (before < 0)
		before = 0;

	getnstimeofday(&now);
	dist_curr = (now.tv_sec * 1000000 + now.tv_nsec / 1000) -
			cfgdone_lasttime;
	total = dist_curr;
	for (i = 0; i < 10; i++) {
		if (--index < 0)
			index = 9;
		total += cfgdone_distlist[index];
		if (i == 0)
			dist_first = cfgdone_distlist[index];
		if (total < (before * 1000)) {
			dist_total += cfgdone_distlist[index];
			dist_count++;
		} else {
			break;
		}
	}

	dist_curr = (dist_curr > dist_first) ? dist_curr : dist_first;
	dist_total += dist_curr;
	dist_count++;

	if (dist_total > 0)
		fps = (1000000 * dist_count) / dist_total;
	else
		fps = 60;

	return fps;
}
EXPORT_SYMBOL(rk_get_real_fps);

#endif
#define ION_MAX 10
static struct ion_handle *ion_hanle[ION_MAX];
static struct ion_handle *ion_hwc[1];
static int rk_fb_ioctl(struct fb_info *info, unsigned int cmd,
		       unsigned long arg)
{
	struct rk_fb *rk_fb = dev_get_drvdata(info->device);
	struct rk_fb_par *fb_par = (struct rk_fb_par *)info->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;
	struct fb_fix_screeninfo *fix = &info->fix;
	struct rk_lcdc_win *win;
	int enable;	/* enable fb:1 enable;0 disable */
	int ovl;	/* overlay:0 win1 on the top of win0;1,win0 on the top of win1 */
	int num_buf;	/* buffer_number */
	int ret = 0;
	struct rk_fb_win_cfg_data win_data;
	unsigned int dsp_addr[4][4];
	int list_stat;

	int win_id = dev_drv->ops->fb_get_win_id(dev_drv, info->fix.id);
	void __user *argp = (void __user *)arg;

	win = dev_drv->win[win_id];
	switch (cmd) {
	case RK_FBIOSET_HWC_ADDR:
	{
		u32 hwc_phy[1];

		if (copy_from_user(hwc_phy, argp, 4))
			return -EFAULT;
		if (!dev_drv->iommu_enabled) {
			fix->smem_start = hwc_phy[0];
		} else {
			int usr_fd;
			struct ion_handle *hdl;
			ion_phys_addr_t phy_addr;
			size_t len;

			usr_fd = hwc_phy[0];
			if (!usr_fd) {
				fix->smem_start = 0;
				fix->mmio_start = 0;
				dev_drv->ops->open(dev_drv, win_id, 0);
				break;
			}

			if (ion_hwc[0] != 0) {
				ion_free(rk_fb->ion_client, ion_hwc[0]);
				ion_hwc[0] = 0;
			}

			hdl = ion_import_dma_buf(rk_fb->ion_client, usr_fd);
			if (IS_ERR(hdl)) {
				dev_err(info->dev, "failed to get hwc ion handle:%ld\n",
					PTR_ERR(hdl));
				return -EFAULT;
			}

			ret = ion_map_iommu(dev_drv->dev, rk_fb->ion_client, hdl,
					    (unsigned long *)&phy_addr,
					    (unsigned long *)&len);
			if (ret < 0) {
				dev_err(info->dev, "ion map to get hwc phy addr failed");
				ion_free(rk_fb->ion_client, hdl);
				return -ENOMEM;
			}
			fix->smem_start = phy_addr;
			ion_hwc[0] = hdl;
		}
		break;
	}
	case RK_FBIOSET_YUV_ADDR:
		{
			u32 yuv_phy[2];

			if (copy_from_user(yuv_phy, argp, 8))
				return -EFAULT;
			if (!dev_drv->iommu_enabled || !strcmp(info->fix.id, "fb0")) {
				fix->smem_start = yuv_phy[0];
				fix->mmio_start = yuv_phy[1];
			} else {
				int usr_fd, offset, tmp;
				struct ion_handle *hdl;
				ion_phys_addr_t phy_addr;
				size_t len;

				usr_fd = yuv_phy[0];
				offset = yuv_phy[1] - yuv_phy[0];
				if (!usr_fd) {
					fix->smem_start = 0;
					fix->mmio_start = 0;
					break;
				}

				if (ion_hanle[ION_MAX - 1] != 0) {
					/*ion_unmap_kernel(rk_fb->ion_client,
					 *	ion_hanle[ION_MAX - 1]);
					 *ion_unmap_iommu(dev_drv->dev,
					 *	rk_fb->ion_client,
					 *	ion_hanle[ION_MAX - 1]);
					 */
					ion_free(rk_fb->ion_client, ion_hanle[ION_MAX - 1]);
					ion_hanle[ION_MAX - 1] = 0;
				}

				hdl = ion_import_dma_buf(rk_fb->ion_client, usr_fd);
				if (IS_ERR(hdl)) {
					dev_err(info->dev, "failed to get ion handle:%ld\n",
						PTR_ERR(hdl));
					return -EFAULT;
				}

				ret = ion_map_iommu(dev_drv->dev, rk_fb->ion_client, hdl,
						    (unsigned long *)&phy_addr,
						    (unsigned long *)&len);
				if (ret < 0) {
					dev_err(info->dev, "ion map to get phy addr failed");
					ion_free(rk_fb->ion_client, hdl);
					return -ENOMEM;
				}
				fix->smem_start = phy_addr;
				fix->mmio_start = phy_addr + offset;
				fix->smem_len = len;
				/*info->screen_base =
				 *	ion_map_kernel(rk_fb->ion_client, hdl);
				 */
				ion_hanle[0] = hdl;
				for (tmp = ION_MAX - 1; tmp > 0; tmp--)
					ion_hanle[tmp] = ion_hanle[tmp - 1];
				ion_hanle[0] = 0;
			}
			break;
		}
	case RK_FBIOSET_ENABLE:
		if (copy_from_user(&enable, argp, sizeof(enable)))
			return -EFAULT;
				if (enable && fb_par->state)
					fb_par->state++;
				else
					fb_par->state--;
		dev_drv->ops->open(dev_drv, win_id, enable);
		break;
	case RK_FBIOGET_ENABLE:
		enable = dev_drv->ops->get_win_state(dev_drv, win_id, 0);
		if (copy_to_user(argp, &enable, sizeof(enable)))
			return -EFAULT;
		break;
	case RK_FBIOSET_OVERLAY_STA:
		if (copy_from_user(&ovl, argp, sizeof(ovl)))
			return -EFAULT;
		dev_drv->ops->ovl_mgr(dev_drv, ovl, 1);
		break;
	case RK_FBIOGET_OVERLAY_STA:
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
		if (enable)
			dev_drv->vsync_info.active++;
		else
			dev_drv->vsync_info.active--;
		break;
	case RK_FBIOGET_DSP_ADDR:
		dev_drv->ops->get_dsp_addr(dev_drv, dsp_addr);
		if (copy_to_user(argp, &dsp_addr, sizeof(dsp_addr)))
			return -EFAULT;
		break;
	case RK_FBIOGET_LIST_STA:
		list_stat = rk_fb_get_list_stat(dev_drv);
		if (copy_to_user(argp, &list_stat, sizeof(list_stat)))
			return -EFAULT;

		break;
	case RK_FBIOGET_IOMMU_STA:
		if (copy_to_user(argp, &dev_drv->iommu_enabled,
				 sizeof(dev_drv->iommu_enabled)))
			return -EFAULT;
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
			int fd = -1;

			if (IS_ERR_OR_NULL(fb_par->ion_hdl)) {
				dev_err(info->dev,
					"get dma_buf fd failed,ion handle is err\n");
				return PTR_ERR(fb_par->ion_hdl);
			}
			fd = ion_share_dma_buf_fd(rk_fb->ion_client,
						  fb_par->ion_hdl);
			if (fd < 0) {
				dev_err(info->dev,
					"ion_share_dma_buf_fd failed\n");
				return fd;
			}
			if (copy_to_user(argp, &fd, sizeof(fd)))
				return -EFAULT;
			break;
		}
#endif
	case RK_FBIOSET_CLEAR_FB:
		memset(fb_par->fb_virt_base, 0, fb_par->fb_size);
		break;
	case RK_FBIOSET_CONFIG_DONE:
		{
			int curr = 0;
			struct timespec now;

			getnstimeofday(&now);
			curr = now.tv_sec * 1000000 + now.tv_nsec / 1000;
			cfgdone_distlist[cfgdone_index++] =
				curr - cfgdone_lasttime;
			cfgdone_lasttime = curr;
			if (cfgdone_index >= 10)
				cfgdone_index = 0;
		}
		if (is_car_camcap()) {
			int i = 0;

			for (i = 0; i < RK_MAX_BUF_NUM; i++)
				win_data.rel_fence_fd[i] = -1;

			win_data.ret_fence_fd = -1;
			goto cam_exit;
		}
		if (copy_from_user(&win_data,
				   (struct rk_fb_win_cfg_data __user *)argp,
				   sizeof(win_data))) {
			ret = -EFAULT;
			break;
		};

		dev_drv->wait_fs = win_data.wait_fs;
		ret = rk_fb_set_win_config(info, &win_data);

cam_exit:
		if (copy_to_user((struct rk_fb_win_cfg_data __user *)arg,
				 &win_data, sizeof(win_data))) {
			ret = -EFAULT;
			break;
		}
		memset(&win_data, 0, sizeof(struct rk_fb_win_cfg_data));

		if (dev_drv->uboot_logo)
			dev_drv->uboot_logo = 0;

		break;
	default:
		dev_drv->ops->ioctl(dev_drv, cmd, arg, win_id);
		break;
	}

	return ret;
}

static int rk_fb_blank(int blank_mode, struct fb_info *info)
{
	struct rk_fb_par *fb_par = (struct rk_fb_par *)info->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;
	struct fb_fix_screeninfo *fix = &info->fix;
	int win_id;
#if defined(CONFIG_RK_HDMI)
	struct rk_fb *rk_fb = dev_get_drvdata(info->device);
#endif

	if (is_car_camcap())
		return 0;
	win_id = dev_drv->ops->fb_get_win_id(dev_drv, fix->id);
	if (win_id < 0)
		return -ENODEV;
	mutex_lock(&dev_drv->switch_screen);
#if defined(CONFIG_RK_HDMI)
	if ((rk_fb->disp_mode == ONE_DUAL) &&
	    (hdmi_get_hotplug() == HDMI_HPD_ACTIVATED)) {
		pr_info("hdmi is connect , not blank lcdc\n");
	} else
#endif
	{
		dev_drv->ops->blank(dev_drv, win_id, blank_mode);
	}
	mutex_unlock(&dev_drv->switch_screen);
	return 0;
}

static int rk_fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	if ((0 == var->xres_virtual) || (0 == var->yres_virtual) ||
	    (0 == var->xres) || (0 == var->yres) || (var->xres < 16) ||
	    ((16 != var->bits_per_pixel) &&
	    (32 != var->bits_per_pixel) &&
	    (24 != var->bits_per_pixel))) {
		dev_err(info->dev, "%s check var fail 1:\n"
			"xres_vir:%d>>yres_vir:%d\n"
			"xres:%d>>yres:%d\n"
			"bits_per_pixel:%d\n",
			info->fix.id,
			var->xres_virtual,
			var->yres_virtual,
			var->xres, var->yres, var->bits_per_pixel);
		return -EINVAL;
	}

	if (((var->xoffset + var->xres) > var->xres_virtual) ||
	    ((var->yoffset + var->yres) > (var->yres_virtual))) {
		dev_err(info->dev, "%s check_var fail 2:\n"
			"xoffset:%d>>xres:%d>>xres_vir:%d\n"
			"yoffset:%d>>yres:%d>>yres_vir:%d\n",
			info->fix.id,
			var->xoffset,
			var->xres,
			var->xres_virtual,
			var->yoffset, var->yres, var->yres_virtual);
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
	struct rk_fb_par *fb_par = (struct rk_fb_par *)info->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;
	struct rk_lcdc_win *win = NULL;
	int win_id = 0;

	win_id = dev_drv->ops->fb_get_win_id(dev_drv, info->fix.id);
	if (win_id < 0)
		return -ENODEV;
	else
		win = dev_drv->win[win_id];

	/* only read the current frame buffer */
	if (win->area[0].format == RGB565) {
		total_size = win->area[0].y_vir_stride * win->area[0].yact << 1;
	} else if ((win->area[0].format == YUV420) ||
		   (win->area[0].format == YUV420_NV21)) {
		total_size =
		    (win->area[0].y_vir_stride * win->area[0].yact * 6);
	} else {
		total_size = win->area[0].y_vir_stride * win->area[0].yact << 2;
	}
	if (p >= total_size)
		return 0;

	if (count >= total_size)
		count = total_size;

	if (count + p > total_size)
		count = total_size - p;

	buffer = kmalloc((count > PAGE_SIZE) ? PAGE_SIZE : count, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	src = (u8 __iomem *)(info->screen_base + p + win->area[0].y_offset);

	while (count) {
		c = (count > PAGE_SIZE) ? PAGE_SIZE : count;
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
	struct rk_fb_par *fb_par = (struct rk_fb_par *)info->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;
	struct rk_lcdc_win *win = NULL;
	int win_id = 0;

	win_id = dev_drv->ops->fb_get_win_id(dev_drv, info->fix.id);
	if (win_id < 0)
		return -ENODEV;
	else
		win = dev_drv->win[win_id];

	/* write the current frame buffer */
	if (win->area[0].format == RGB565)
		total_size = win->area[0].xact * win->area[0].yact << 1;
	else
		total_size = win->area[0].xact * win->area[0].yact << 2;

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

	buffer = kmalloc((count > PAGE_SIZE) ? PAGE_SIZE : count, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	dst = (u8 __iomem *)(info->screen_base + p + win->area[0].y_offset);

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
	struct rk_fb_par *fb_par = (struct rk_fb_par *)info->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;
	struct rk_lcdc_win *win = NULL;
	struct rk_screen *screen = dev_drv->cur_screen;
	int win_id = 0;
	u16 xsize = 0, ysize = 0;	/* winx display window height/width --->LCDC_WINx_DSP_INFO */
	u32 xoffset = var->xoffset;	/* offset from virtual to visible */
	u32 yoffset = var->yoffset;
	u16 xpos = (var->nonstd >> 8) & 0xfff;	/*visiable pos in panel */
	u16 ypos = (var->nonstd >> 20) & 0xfff;
	u32 xvir = var->xres_virtual;
	u8 data_format = var->nonstd & 0xff;
	u8 fb_data_fmt;
	u8 pixel_width = 0;
	u32 vir_width_bit;
	u32 stride, uv_stride = 0;
	u32 stride_32bit_1;
	u32 stride_32bit_2;
	u16 uv_x_off, uv_y_off, uv_y_act;
	u8 is_pic_yuv = 0;
	/*var->pixclock = dev_drv->pixclock;*/
	if (dev_drv->suspend_flag || is_car_camcap())
		return 0;
	win_id = dev_drv->ops->fb_get_win_id(dev_drv, info->fix.id);
	if (win_id < 0)
		return -ENODEV;
	else
		win = dev_drv->win[win_id];

	/* if the application has specific the hor and ver display size */
	if (var->grayscale >> 8) {
		xsize = (var->grayscale >> 8) & 0xfff;
		ysize = (var->grayscale >> 20) & 0xfff;
		xsize |= (var->reserved[0] << 12);
		var->reserved[0] = 0;
		if (xsize > screen->mode.xres)
			xsize = screen->mode.xres;
		if (ysize > screen->mode.yres)
			ysize = screen->mode.yres;
	} else {		/*ohterwise  full  screen display */
		xsize = screen->mode.xres;
		ysize = screen->mode.yres;
	}

	win->colorspace = CSC_FORMAT(data_format);
	data_format &= ~CSC_MASK;
	fb_data_fmt = rk_fb_data_fmt(data_format, var->bits_per_pixel);
	if (IS_FBDC_FMT(fb_data_fmt)) {
		win->area[0].fbdc_en = 1;
		win->area[0].fbdc_cor_en = 1;
	} else {
		win->area[0].fbdc_en = 0;
		win->area[0].fbdc_cor_en = 0;
	}
	pixel_width = rk_fb_pixel_width(fb_data_fmt);
	vir_width_bit = pixel_width * xvir;
	/* pixel_width = byte_num * 8 */
	stride_32bit_1 = ALIGN_N_TIMES(vir_width_bit, 32) / 8;
	stride_32bit_2 = ALIGN_N_TIMES(vir_width_bit * 2, 32) / 8;

	switch (fb_data_fmt) {
	case YUV422:
	case YUV422_A:
		is_pic_yuv = 1;
		stride = stride_32bit_1;
		uv_stride = stride_32bit_1;
		uv_x_off = xoffset;
		uv_y_off = yoffset;
		fix->line_length = stride;
		uv_y_act = win->area[0].yact >> 1;
		break;
	case YUV420:		/* nv12 */
	case YUV420_NV21:	/* nv21 */
	case YUV420_A:
		is_pic_yuv = 1;
		stride = stride_32bit_1;
		uv_stride = stride_32bit_1;
		uv_x_off = xoffset;
		uv_y_off = yoffset >> 1;
		fix->line_length = stride;
		uv_y_act = win->area[0].yact >> 1;
		break;
	case YUV444:
	case YUV444_A:
		is_pic_yuv = 1;
		stride = stride_32bit_1;
		uv_stride = stride_32bit_2;
		uv_x_off = xoffset * 2;
		uv_y_off = yoffset;
		fix->line_length = stride << 2;
		uv_y_act = win->area[0].yact;
		break;
	default:
		stride = stride_32bit_1;	/* default rgb */
		fix->line_length = stride;
		break;
	}

	win->area[0].format = fb_data_fmt;
	win->area[0].y_vir_stride = stride >> 2;
	win->area[0].uv_vir_stride = uv_stride >> 2;
	win->area[0].xpos = xpos;
	win->area[0].ypos = ypos;
	win->area[0].xsize = xsize;
	win->area[0].ysize = ysize;
	win->area[0].xact = var->xres;	/* winx active window height,is a wint of vir */
	win->area[0].yact = var->yres;
	win->area[0].xvir = var->xres_virtual;	/* virtual resolution  stride --->LCDC_WINx_VIR */
	win->area[0].yvir = var->yres_virtual;
	win->area[0].xoff = xoffset;
	win->area[0].yoff = yoffset;
	win->ymirror = 0;
	win->state = 1;
	win->last_state = 1;

	win->area_num = 1;
	win->alpha_mode = 4;	/* AB_SRC_OVER; */
	win->alpha_en = ((win->area[0].format == ARGB888) ||
			 (win->area[0].format == FBDC_ARGB_888) ||
			 (win->area[0].format == FBDC_ABGR_888) ||
			 (win->area[0].format == ABGR888)) ? 1 : 0;
	win->g_alpha_val = 0;

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

			val = chan_to_field(red, &info->var.red);
			val |= chan_to_field(green, &info->var.green);
			val |= chan_to_field(blue, &info->var.blue);
			pal[regno] = val;
		}
		break;
	default:
		return -1;	/* unknown type */
	}

	return 0;
}

static int rk_fb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct rk_fb *rk_fb = platform_get_drvdata(fb_pdev);
	struct rk_fb_par *fb_par = (struct rk_fb_par *)info->par;
	struct ion_handle *handle = fb_par->ion_hdl;
	struct dma_buf *dma_buf = NULL;

	if (IS_ERR_OR_NULL(handle)) {
		dev_err(info->dev, "failed to get ion handle:%ld\n",
			PTR_ERR(handle));
		return -ENOMEM;
	}
	dma_buf = ion_share_dma_buf(rk_fb->ion_client, handle);
	if (IS_ERR_OR_NULL(dma_buf)) {
		pr_info("get ion share dma buf failed\n");
		return -ENOMEM;
	}

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	return dma_buf_mmap(dma_buf, vma, 0);
}

static struct fb_ops fb_ops = {
	.owner = THIS_MODULE,
	.fb_open = rk_fb_open,
	.fb_release = rk_fb_close,
	.fb_check_var = rk_fb_check_var,
	.fb_set_par = rk_fb_set_par,
	.fb_blank = rk_fb_blank,
	.fb_ioctl = rk_fb_ioctl,
	.fb_compat_ioctl = rk_fb_ioctl,
	.fb_pan_display = rk_fb_pan_display,
	.fb_read = rk_fb_read,
	.fb_write = rk_fb_write,
	.fb_setcolreg = fb_setcolreg,
	.fb_fillrect = cfb_fillrect,
	.fb_copyarea = cfb_copyarea,
	.fb_imageblit = cfb_imageblit,
};

static struct fb_var_screeninfo def_var = {
#if defined(CONFIG_LOGO_LINUX_BMP)
	.red = {16, 8, 0},
	.green = {8, 8, 0},
	.blue = {0, 8, 0},
	.transp = {0, 0, 0},
	.nonstd = HAL_PIXEL_FORMAT_BGRA_8888,
#else
	.red = {11, 5, 0},
	.green = {5, 6, 0},
	.blue = {0, 5, 0},
	.transp = {0, 0, 0},
	.nonstd = HAL_PIXEL_FORMAT_RGB_565,	/* (ypos<<20+xpos<<8+format) format */
#endif
	.grayscale = 0,		/* (ysize<<20+xsize<<8) */
	.activate = FB_ACTIVATE_NOW,
	.accel_flags = 0,
	.vmode = FB_VMODE_NONINTERLACED,
};

static struct fb_fix_screeninfo def_fix = {
	.type = FB_TYPE_PACKED_PIXELS,
	.type_aux = 0,
	.xpanstep = 1,
	.ypanstep = 1,
	.ywrapstep = 0,
	.accel = FB_ACCEL_NONE,
	.visual = FB_VISUAL_TRUECOLOR,

};

static int rk_fb_wait_for_vsync_thread(void *data)
{
	struct rk_lcdc_driver *dev_drv = data;
	struct rk_fb *rk_fb = platform_get_drvdata(fb_pdev);
	struct fb_info *fbi = rk_fb->fb[dev_drv->fb_index_base];

	while (!kthread_should_stop()) {
		ktime_t timestamp = dev_drv->vsync_info.timestamp;
		int ret = wait_event_interruptible(dev_drv->vsync_info.wait,
				!ktime_equal(timestamp, dev_drv->vsync_info.timestamp) &&
				(dev_drv->vsync_info.active > 0 || dev_drv->vsync_info.irq_stop));

		if (!ret)
			sysfs_notify(&fbi->dev->kobj, NULL, "vsync");
	}

	return 0;
}

static ssize_t rk_fb_vsync_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct rk_fb_par *fb_par = (struct rk_fb_par *)fbi->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;

	return scnprintf(buf, PAGE_SIZE, "%llu\n",
			 ktime_to_ns(dev_drv->vsync_info.timestamp));
}

static DEVICE_ATTR(vsync, S_IRUGO, rk_fb_vsync_show, NULL);

/*
 * this two function is for other module that in the kernel which
 * need show image directly through fb
 * fb_id:we have 4 fb here,default we use fb0 for ui display
 */
struct fb_info *rk_get_fb(int fb_id)
{
	struct rk_fb *inf = platform_get_drvdata(fb_pdev);
	struct fb_info *fb = inf->fb[fb_id];
	return fb;
}
EXPORT_SYMBOL(rk_get_fb);

void rk_direct_fb_show(struct fb_info *fbi)
{
	rk_fb_set_par(fbi);
	rk_fb_pan_display(&fbi->var, fbi);
}
EXPORT_SYMBOL(rk_direct_fb_show);

int rk_fb_dpi_open(bool open)
{
	struct rk_lcdc_driver *dev_drv = NULL;

	dev_drv = rk_get_prmry_lcdc_drv();
	if (dev_drv->ops->dpi_open)
		dev_drv->ops->dpi_open(dev_drv, open);
	return 0;
}

int rk_fb_dpi_win_sel(int win_id)
{
	struct rk_lcdc_driver *dev_drv = NULL;

	dev_drv = rk_get_prmry_lcdc_drv();
	if (dev_drv->ops->dpi_win_sel)
		dev_drv->ops->dpi_win_sel(dev_drv, win_id);
	return 0;
}

int rk_fb_dpi_status(void)
{
	int ret = 0;
	struct rk_lcdc_driver *dev_drv = NULL;

	dev_drv = rk_get_prmry_lcdc_drv();
	if (dev_drv->ops->dpi_status)
		ret = dev_drv->ops->dpi_status(dev_drv);

	return ret;
}

/*
 * function: this function will be called by display device, enable/disable lcdc
 * @screen: screen timing to be set to lcdc
 * @enable: 0 disable lcdc; 1 enable change lcdc timing; 2 just enable dclk
 * @lcdc_id: the lcdc id the display device attached ,0 or 1
 */
int rk_fb_switch_screen(struct rk_screen *screen, int enable, int lcdc_id)
{
	struct rk_fb *rk_fb =  platform_get_drvdata(fb_pdev);
	struct fb_info *info = NULL;
	struct rk_fb_par *fb_par = NULL;
	struct rk_lcdc_driver *dev_drv = NULL;
	struct rk_lcdc_win *win;
	char name[6] = {0};
	int i, win_id;
	static bool load_screen;
	char *envp[4];
	char envplcdc[32];
	char envpfbdev[32];
	int ret, list_is_empty = 0;

	if (unlikely(!rk_fb) || unlikely(!screen))
		return -ENODEV;

	/* get lcdc driver */
	sprintf(name, "lcdc%d", lcdc_id);
	dev_drv = rk_get_lcdc_drv(name);

	if (dev_drv == NULL) {
		pr_err("%s driver not found!", name);
		return -ENODEV;
	}
	if (screen->type == SCREEN_HDMI)
		pr_info("hdmi %s lcdc%d\n",
			enable ? "connect to" : "remove from",
			dev_drv->id);
	else if (screen->type == SCREEN_TVOUT ||
		 screen->type == SCREEN_TVOUT_TEST)
		pr_info("cvbs %s lcdc%d\n",
			enable ? "connect to" : "remove from",
			dev_drv->id);
	if (enable == 2 /*&& dev_drv->enable*/)
		return 0;
	pr_info("switch:en=%d,lcdc_id=%d,screen type=%d,cur type=%d",
		enable, lcdc_id, screen->type, dev_drv->cur_screen->type);
	pr_info("data space: %d, color mode: %d\n",
		screen->data_space, screen->color_mode);

	mutex_lock(&dev_drv->switch_screen);
	dev_drv->hot_plug_state = enable;
	hdmi_switch_state = 0;
	dev_drv->hdmi_switch = 1;
	if (!dev_drv->uboot_logo) {
		mdelay(200);
		list_is_empty = list_empty(&dev_drv->update_regs_list) &&
					   list_empty(&dev_drv->saved_list);
		if (!list_is_empty) {
			ret = wait_event_timeout(dev_drv->update_regs_wait,
						 list_empty(&dev_drv->update_regs_list) &&
						 list_empty(&dev_drv->saved_list),
						 msecs_to_jiffies(60));
			if (ret <= 0)
				pr_info("%s: wait update_regs_wait timeout\n",
					__func__);
		}
	}

	envp[0] = "switch vop screen";
	memset(envplcdc, 0, sizeof(envplcdc));
	memset(envpfbdev, 0, sizeof(envpfbdev));
	sprintf(envplcdc, "SCREEN=%d,ENABLE=%d,VOPID=%d", screen->type, enable, dev_drv->id);
	sprintf(envpfbdev, "FBDEV=%d", dev_drv->fb_index_base);
	envp[1] = envplcdc;
	envp[2] = envpfbdev;
	envp[3] = NULL;

	if ((rk_fb->disp_mode == ONE_DUAL) ||
	    (rk_fb->disp_mode == NO_DUAL)) {
		if ((dev_drv->ops->backlight_close) &&
		    (rk_fb->disp_policy != DISPLAY_POLICY_BOX))
			dev_drv->ops->backlight_close(dev_drv, 1);
		if (!dev_drv->uboot_logo || load_screen ||
		    (rk_fb->disp_policy != DISPLAY_POLICY_BOX)) {
			if (dev_drv->ops->dsp_black)
				dev_drv->ops->dsp_black(dev_drv, 0);
		}
		if ((dev_drv->ops->set_screen_scaler) &&
		    (rk_fb->disp_mode == ONE_DUAL))
			dev_drv->ops->set_screen_scaler(dev_drv,
							dev_drv->screen0, 0);
	}
	if (!enable) {
		/* if screen type is different, we do not disable lcdc. */
		if (dev_drv->cur_screen->type != screen->type) {
			dev_drv->hdmi_switch = 0;
			mutex_unlock(&dev_drv->switch_screen);
			return 0;
		}

		/* if used one lcdc to dual disp, no need to close win */
		if ((rk_fb->disp_mode == ONE_DUAL) ||
		    ((rk_fb->disp_mode == NO_DUAL) &&
		    (rk_fb->disp_policy != DISPLAY_POLICY_BOX))) {
			dev_drv->cur_screen = dev_drv->screen0;
			dev_drv->ops->load_screen(dev_drv, 1);
			/* force modify dsp size */
			info = rk_fb->fb[dev_drv->fb_index_base];
			info->var.grayscale &= 0xff;
			info->var.grayscale |=
				((dev_drv->cur_screen->mode.xres & 0xfff) << 8) +
				(dev_drv->cur_screen->mode.yres << 20);
			info->var.reserved[0] |= (dev_drv->cur_screen->mode.xres >> 12);
			mutex_lock(&dev_drv->win_config);
			info->var.xoffset = 0;
			info->var.yoffset = 0;
			info->fbops->fb_set_par(info);
			info->fbops->fb_pan_display(&info->var, info);
			mutex_unlock(&dev_drv->win_config);

			/*
			 * if currently is loader display, black until new
			 * display job.
			 */
			if (dev_drv->uboot_logo) {
				for (i = 0; i < dev_drv->lcdc_win_num; i++) {
					if (dev_drv->win[i] && dev_drv->win[i]->state &&
					    dev_drv->ops->win_direct_en)
						dev_drv->ops->win_direct_en(dev_drv, i, 0);
				}
			}

			/*if (dev_drv->ops->dsp_black)
			 *	dev_drv->ops->dsp_black(dev_drv, 0);
			 */
			if ((dev_drv->ops->backlight_close) &&
			    (rk_fb->disp_policy != DISPLAY_POLICY_BOX))
				dev_drv->ops->backlight_close(dev_drv, 0);
		} else if (rk_fb->num_lcdc > 1) {
			/* If there is more than one lcdc device, we disable
			 *  the layer which attached to this device
			 */
			flush_kthread_worker(&dev_drv->update_regs_worker);
			for (i = 0; i < dev_drv->lcdc_win_num; i++) {
				if (dev_drv->win[i] && dev_drv->win[i]->state)
					dev_drv->ops->open(dev_drv, i, 0);
			}
		}
		kobject_uevent_env(&dev_drv->dev->kobj, KOBJ_CHANGE, envp);

		hdmi_switch_state = 0;
		dev_drv->hdmi_switch = 0;
		mutex_unlock(&dev_drv->switch_screen);
		return 0;
	} else {
		if (load_screen || (rk_fb->disp_policy != DISPLAY_POLICY_BOX)) {
			for (i = 0; i < dev_drv->lcdc_win_num; i++) {
				if (dev_drv->win[i] && dev_drv->win[i]->state &&
					dev_drv->ops->win_direct_en)
					dev_drv->ops->win_direct_en(dev_drv, i, 0);
			}
		}
		if (dev_drv->screen1)
			dev_drv->cur_screen = dev_drv->screen1;

		memcpy(dev_drv->cur_screen, screen, sizeof(struct rk_screen));
		dev_drv->cur_screen->xsize = dev_drv->cur_screen->mode.xres;
		dev_drv->cur_screen->ysize = dev_drv->cur_screen->mode.yres;
		dev_drv->cur_screen->x_mirror =
					!!(dev_drv->rotate_mode & X_MIRROR);
		dev_drv->cur_screen->y_mirror =
					!!(dev_drv->rotate_mode & Y_MIRROR);
	}

	if (!dev_drv->uboot_logo || load_screen ||
	    (rk_fb->disp_policy != DISPLAY_POLICY_BOX)) {
		info = rk_fb->fb[dev_drv->fb_index_base];
		fb_par = (struct rk_fb_par *)info->par;
		win_id = 0;
		win = dev_drv->win[win_id];
		if (win && fb_par->state) {
			dev_drv->ops->load_screen(dev_drv, 1);
			info->var.activate |= FB_ACTIVATE_FORCE;
			if (rk_fb->disp_mode == ONE_DUAL) {
				info->var.grayscale &= 0xff;
				info->var.grayscale |=
					((dev_drv->cur_screen->mode.xres & 0xfff) << 8) +
					(dev_drv->cur_screen->ysize << 20);
				info->var.reserved[0] |= (dev_drv->cur_screen->mode.xres >> 12);
			}
			if (dev_drv->uboot_logo && win->state) {
				if (win->area[0].xpos ||
				    win->area[0].ypos) {
					win->area[0].xpos =
						(screen->mode.xres -
						 win->area[0].xsize) / 2;
					win->area[0].ypos =
						(screen->mode.yres -
						 win->area[0].ysize) / 2;
				} else {
					win->area[0].xsize = screen->mode.xres;
					win->area[0].ysize = screen->mode.yres;
				}
				dev_drv->ops->set_par(dev_drv, i);
				dev_drv->ops->cfg_done(dev_drv);
			} else if (!dev_drv->win[win_id]->state) {
				dev_drv->ops->open(dev_drv, win_id, 1);
				info->fbops->fb_pan_display(&info->var, info);
			}
		}
	} else {
		dev_drv->ops->load_screen(dev_drv, 0);
	}
	kobject_uevent_env(&dev_drv->dev->kobj, KOBJ_CHANGE, envp);

	if (dev_drv->cur_screen->width && dev_drv->cur_screen->height) {
		/* for vr auto dp support */
		info = rk_fb->fb[dev_drv->fb_index_base];
		info->var.width = dev_drv->cur_screen->width;
		info->var.height = dev_drv->cur_screen->height;
		pr_info("%s:info->var.width=%d, info->var.height=%d\n",
			__func__, info->var.width, info->var.height);
	}

	hdmi_switch_state = 1;
	load_screen = true;
	dev_drv->hdmi_switch = 0;
	if ((rk_fb->disp_mode == ONE_DUAL) || (rk_fb->disp_mode == NO_DUAL)) {
		if ((dev_drv->ops->set_screen_scaler) &&
		    (rk_fb->disp_mode == ONE_DUAL))
			dev_drv->ops->set_screen_scaler(dev_drv,
							dev_drv->screen0, 1);
		/*if (dev_drv->ops->dsp_black)
		 *	dev_drv->ops->dsp_black(dev_drv, 0);*/
		if ((dev_drv->ops->backlight_close) &&
		    (rk_fb->disp_policy != DISPLAY_POLICY_BOX) &&
		    (rk_fb->disp_mode == ONE_DUAL))
			dev_drv->ops->backlight_close(dev_drv, 0);
	}
	mutex_unlock(&dev_drv->switch_screen);
	return 0;
}

/*
 * function:this function current only called by hdmi for
 *	scale the display
 * scale_x: scale rate of x resolution
 * scale_y: scale rate of y resolution
 * lcdc_id: the lcdc id the hdmi attached ,0 or 1
 */
int rk_fb_disp_scale(u8 scale_x, u8 scale_y, u8 lcdc_id)
{
	struct rk_fb *inf = platform_get_drvdata(fb_pdev);
	struct fb_info *info = NULL;
	struct fb_info *pmy_info = NULL;
	struct fb_var_screeninfo *var = NULL;
	struct rk_lcdc_driver *dev_drv = NULL;
	u16 screen_x, screen_y;
	u16 xpos, ypos;
	char name[6];
	struct rk_screen primary_screen;

	rk_fb_get_prmry_screen(&primary_screen);
	if (primary_screen.type == SCREEN_HDMI)
		return 0;

	pr_err("should not be here--%s\n", __func__);

	return 0;
	sprintf(name, "lcdc%d", lcdc_id);

	if (inf->disp_mode == DUAL) {
		dev_drv = rk_get_lcdc_drv(name);
		if (!dev_drv) {
			pr_err("%s driver not found!", name);
			return -ENODEV;
		}
	} else {
		dev_drv = inf->lcdc_dev_drv[0];
	}

	if (inf->num_lcdc == 1) {
		info = inf->fb[0];
	} else if (inf->num_lcdc == 2) {
		info = inf->fb[dev_drv->lcdc_win_num];
		pmy_info = inf->fb[0];
	}

	var = &info->var;
	screen_x = dev_drv->cur_screen->mode.xres;
	screen_y = dev_drv->cur_screen->mode.yres;

	if (inf->disp_mode != DUAL && dev_drv->screen1) {
		dev_drv->cur_screen->xpos =
		    (screen_x - screen_x * scale_x / 100) >> 1;
		dev_drv->cur_screen->ypos =
		    (screen_y - screen_y * scale_y / 100) >> 1;
		dev_drv->cur_screen->xsize = screen_x * scale_x / 100;
		dev_drv->cur_screen->ysize = screen_y * scale_y / 100;
	} else {
		xpos = (screen_x - screen_x * scale_x / 100) >> 1;
		ypos = (screen_y - screen_y * scale_y / 100) >> 1;
		dev_drv->cur_screen->xsize = screen_x * scale_x / 100;
		dev_drv->cur_screen->ysize = screen_y * scale_y / 100;
		if (inf->disp_mode == ONE_DUAL) {
			var->nonstd &= 0xff;
			var->nonstd |= (xpos << 8) + (ypos << 20);
			var->grayscale &= 0xff;
			var->grayscale |=
				(dev_drv->cur_screen->xsize << 8) +
				(dev_drv->cur_screen->ysize << 20);
		}
	}

	mutex_lock(&dev_drv->win_config);
	info->fbops->fb_set_par(info);
	dev_drv->ops->cfg_done(dev_drv);
	mutex_unlock(&dev_drv->win_config);

	return 0;
}

#if defined(CONFIG_ION_ROCKCHIP)
static int rk_fb_alloc_buffer_by_ion(struct fb_info *fbi,
				     struct rk_lcdc_win *win,
				     unsigned long fb_mem_size)
{
	struct rk_fb *rk_fb = platform_get_drvdata(fb_pdev);
	struct rk_fb_par *fb_par = (struct rk_fb_par *)fbi->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;
	struct ion_handle *handle;
	ion_phys_addr_t phy_addr;
	size_t len;
	int ret = 0;

	if (dev_drv->iommu_enabled)
		handle = ion_alloc(rk_fb->ion_client, (size_t)fb_mem_size, 0,
				   ION_HEAP_SYSTEM_MASK, 0);
	else
		handle = ion_alloc(rk_fb->ion_client, (size_t)fb_mem_size, 0,
				   ION_HEAP_TYPE_DMA_MASK, 0);

	if (IS_ERR(handle)) {
		dev_err(fbi->device, "failed to ion_alloc:%ld\n",
			PTR_ERR(handle));
		return -ENOMEM;
	}

	fb_par->ion_hdl = handle;
	win->area[0].dma_buf = ion_share_dma_buf(rk_fb->ion_client, handle);
	if (IS_ERR_OR_NULL(win->area[0].dma_buf)) {
		pr_info("ion_share_dma_buf() failed\n");
		goto err_share_dma_buf;
	}
	win->area[0].ion_hdl = handle;
	if (dev_drv->prop == PRMRY)
		fbi->screen_base = ion_map_kernel(rk_fb->ion_client, handle);
	if (dev_drv->iommu_enabled && dev_drv->mmu_dev)
		ret = ion_map_iommu(dev_drv->dev, rk_fb->ion_client, handle,
				    (unsigned long *)&phy_addr,
				    (unsigned long *)&len);
	else
		ret = ion_phys(rk_fb->ion_client, handle, &phy_addr, &len);
	if (ret < 0) {
		dev_err(fbi->dev, "ion map to get phy addr failed\n");
		goto err_share_dma_buf;
	}
	fbi->fix.smem_start = phy_addr;
	fbi->fix.smem_len = len;
	pr_info("alloc_buffer:ion_phy_addr=0x%lx\n", phy_addr);
	return 0;

err_share_dma_buf:
	ion_free(rk_fb->ion_client, handle);
	return -ENOMEM;
}
#endif

static int rk_fb_alloc_buffer(struct fb_info *fbi)
{
	struct rk_fb *rk_fb = platform_get_drvdata(fb_pdev);
	struct rk_fb_par *fb_par = (struct rk_fb_par *)fbi->par;
	struct rk_lcdc_driver *dev_drv = fb_par->lcdc_drv;
	struct rk_lcdc_win *win = NULL;
	int win_id;
	int ret = 0;
	unsigned long fb_mem_size;
#if !defined(CONFIG_ION_ROCKCHIP)
	dma_addr_t fb_mem_phys;
	void *fb_mem_virt;
#endif
	ion_phys_addr_t phy_addr;
	size_t len;

	win_id = dev_drv->ops->fb_get_win_id(dev_drv, fbi->fix.id);
	if (win_id < 0)
		return -ENODEV;
	else
		win = dev_drv->win[win_id];

	if (!strcmp(fbi->fix.id, "fb0")) {
		fb_mem_size = get_fb_size(dev_drv->reserved_fb);
#if defined(CONFIG_ION_ROCKCHIP)
		if (rk_fb_alloc_buffer_by_ion(fbi, win, fb_mem_size) < 0)
			return -ENOMEM;
#else
		fb_mem_virt = dma_alloc_writecombine(fbi->dev, fb_mem_size,
						     &fb_mem_phys, GFP_KERNEL);
		if (!fb_mem_virt) {
			pr_err("%s: Failed to allocate framebuffer\n",
			       __func__);
			return -ENOMEM;
		}
		fbi->fix.smem_len = fb_mem_size;
		fbi->fix.smem_start = fb_mem_phys;
		fbi->screen_base = fb_mem_virt;
#endif
		memset(fbi->screen_base, 0, fbi->fix.smem_len);
	} else {
		if (dev_drv->prop == EXTEND && dev_drv->iommu_enabled) {
			struct rk_lcdc_driver *dev_drv_prmry;
			int win_id_prmry;

			fb_mem_size = get_fb_size(dev_drv->reserved_fb);
#if defined(CONFIG_ION_ROCKCHIP)
			dev_drv_prmry = rk_get_prmry_lcdc_drv();
			if (dev_drv_prmry == NULL)
				return -ENODEV;
			win_id_prmry =
				dev_drv_prmry->ops->fb_get_win_id(dev_drv_prmry,
								 fbi->fix.id);
			if (win_id_prmry < 0)
				return -ENODEV;
			else
				fb_par->ion_hdl =
				dev_drv_prmry->win[win_id_prmry]->area[0].ion_hdl;
			fbi->screen_base =
				ion_map_kernel(rk_fb->ion_client,
					       fb_par->ion_hdl);
			dev_drv->win[win_id]->area[0].ion_hdl =
				fb_par->ion_hdl;
			if (dev_drv->mmu_dev)
				ret = ion_map_iommu(dev_drv->dev,
						    rk_fb->ion_client,
						    fb_par->ion_hdl,
						    (unsigned long *)&phy_addr,
						    (unsigned long *)&len);
			else
				ret = ion_phys(rk_fb->ion_client,
					       fb_par->ion_hdl,
					       &phy_addr, &len);
			if (ret < 0) {
				dev_err(fbi->dev, "ion map to get phy addr failed\n");
				return -ENOMEM;
			}
			fbi->fix.smem_start = phy_addr;
			fbi->fix.smem_len = len;
#else
			fb_mem_virt = dma_alloc_writecombine(fbi->dev,
							     fb_mem_size,
							     &fb_mem_phys,
							     GFP_KERNEL);
			if (!fb_mem_virt) {
				pr_err("%s: Failed to allocate framebuffer\n",
				       __func__);
				return -ENOMEM;
			}
			fbi->fix.smem_len = fb_mem_size;
			fbi->fix.smem_start = fb_mem_phys;
			fbi->screen_base = fb_mem_virt;
#endif
		} else {
			fbi->fix.smem_start = rk_fb->fb[0]->fix.smem_start;
			fbi->fix.smem_len = rk_fb->fb[0]->fix.smem_len;
			fbi->screen_base = rk_fb->fb[0]->screen_base;
		}
	}

	fbi->screen_size = fbi->fix.smem_len;
	fb_par->fb_phy_base = fbi->fix.smem_start;
	fb_par->fb_virt_base = fbi->screen_base;
	fb_par->fb_size = fbi->fix.smem_len;

	pr_info("%s:phy:%lx>>vir:%p>>len:0x%x\n", fbi->fix.id,
		fbi->fix.smem_start, fbi->screen_base,
		fbi->fix.smem_len);
	return ret;
}

#if 0
static int rk_release_fb_buffer(struct fb_info *fbi)
{
	/* buffer for fb1 and fb3 are alloc by android */
	if (!strcmp(fbi->fix.id, "fb1") || !strcmp(fbi->fix.id, "fb3"))
		return 0;
	iounmap(fbi->screen_base);
	release_mem_region(fbi->fix.smem_start, fbi->fix.smem_len);
	return 0;
}
#endif

static int init_lcdc_win(struct rk_lcdc_driver *dev_drv,
			 struct rk_lcdc_win *def_win)
{
	int i;
	int lcdc_win_num = dev_drv->lcdc_win_num;

	for (i = 0; i < lcdc_win_num; i++) {
		struct rk_lcdc_win *win = NULL;

		win = kzalloc(sizeof(struct rk_lcdc_win), GFP_KERNEL);
		if (!win) {
			dev_err(dev_drv->dev, "kzmalloc for win fail!");
			return -ENOMEM;
		}

		strcpy(win->name, def_win[i].name);
		win->id = def_win[i].id;
		win->support_3d = def_win[i].support_3d;
		win->property.feature = def_win[i].property.feature;
		win->property.max_input_x = def_win[i].property.max_input_x;
		win->property.max_input_y = def_win[i].property.max_input_y;
		dev_drv->win[i] = win;
	}

	return 0;
}

static int init_lcdc_device_driver(struct rk_fb *rk_fb,
				   struct rk_lcdc_win *def_win, int index)
{
	struct rk_lcdc_driver *dev_drv = rk_fb->lcdc_dev_drv[index];
	struct rk_screen *screen = devm_kzalloc(dev_drv->dev,
						sizeof(struct rk_screen),
						GFP_KERNEL);
	int i = 0;

	if (!screen) {
		dev_err(dev_drv->dev, "malloc screen for lcdc%d fail!",
			dev_drv->id);
		return -ENOMEM;
	}

	screen->screen_id = 0;
	screen->lcdc_id = dev_drv->id;
	screen->overscan.left = 100;
	screen->overscan.top = 100;
	screen->overscan.right = 100;
	screen->overscan.bottom = 100;

	screen->x_mirror = !!(dev_drv->rotate_mode & X_MIRROR);
	screen->y_mirror = !!(dev_drv->rotate_mode & Y_MIRROR);

	dev_drv->screen0 = screen;
	dev_drv->cur_screen = screen;
	/* devie use one lcdc + rk61x scaler for dual display */
	if ((rk_fb->disp_mode == ONE_DUAL) || (rk_fb->disp_mode == NO_DUAL)) {
		struct rk_screen *screen1 =
				devm_kzalloc(dev_drv->dev,
					     sizeof(struct rk_screen),
					     GFP_KERNEL);
		if (!screen1) {
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
	mutex_init(&dev_drv->win_config);
	mutex_init(&dev_drv->front_lock);
	mutex_init(&dev_drv->switch_screen);
	dev_drv->ops->fb_win_remap(dev_drv, dev_drv->fb_win_map);
	dev_drv->first_frame = 1;
	dev_drv->overscan.left = 100;
	dev_drv->overscan.top = 100;
	dev_drv->overscan.right = 100;
	dev_drv->overscan.bottom = 100;
	for (i = 0; i < RK30_MAX_LAYER_SUPPORT; i++)
		dev_drv->area_support[i] = 1;
	if (dev_drv->ops->area_support_num)
		dev_drv->ops->area_support_num(dev_drv, dev_drv->area_support);
	rk_disp_pwr_ctr_parse_dt(dev_drv);
	if (dev_drv->prop == PRMRY) {
		rk_fb_set_prmry_screen(screen);
		rk_fb_get_prmry_screen(screen);
	}
	dev_drv->trsm_ops = rk_fb_trsm_ops_get(screen->type);
	if (dev_drv->prop != PRMRY)
		rk_fb_get_extern_screen(screen);
	dev_drv->output_color = screen->color_mode;

	return 0;
}

#ifdef CONFIG_LOGO_LINUX_BMP
static struct linux_logo *bmp_logo;
static int fb_prewine_bmp_logo(struct fb_info *info, int rotate)
{
	bmp_logo = fb_find_logo(24);
	if (bmp_logo == NULL) {
		pr_info("%s error\n", __func__);
		return 0;
	}
	return 1;
}

static void fb_show_bmp_logo(struct fb_info *info, int rotate)
{
	unsigned char *src = bmp_logo->data;
	unsigned char *dst = info->screen_base;
	int i;
	unsigned int needwidth = (*(src - 24) << 8) | (*(src - 23));
	unsigned int needheight = (*(src - 22) << 8) | (*(src - 21));

	for (i = 0; i < needheight; i++)
		memcpy(dst + info->var.xres * i * 4,
		       src + bmp_logo->width * i * 4, needwidth * 4);
}
#endif

/*
 * check if the primary lcdc has registered,
 * the primary lcdc mas register first
 */
bool is_prmry_rk_lcdc_registered(void)
{
	struct rk_fb *rk_fb = platform_get_drvdata(fb_pdev);

	if (rk_fb->lcdc_dev_drv[0])
		return true;
	else
		return false;
}

phys_addr_t uboot_logo_base;
phys_addr_t uboot_logo_size;
phys_addr_t uboot_logo_offset;

static int __init rockchip_uboot_mem_late_init(void)
{
	int err;

	if (uboot_logo_size) {
		void *start = phys_to_virt(uboot_logo_base);
		void *end = phys_to_virt(uboot_logo_base + uboot_logo_size);

		err = memblock_free(uboot_logo_base, uboot_logo_size);
		if (err < 0)
			pr_err("%s: freeing memblock failed: %d\n",
			       __func__, err);
		free_reserved_area(start, end, -1, "logo");
	}
	return 0;
}

late_initcall(rockchip_uboot_mem_late_init);

int rk_fb_register(struct rk_lcdc_driver *dev_drv,
		   struct rk_lcdc_win *win, int id)
{
	struct rk_fb *rk_fb = platform_get_drvdata(fb_pdev);
	struct fb_info *fbi;
	struct rk_fb_par *fb_par = NULL;
	int i = 0, ret = 0, index = 0;
	unsigned long flags;
	char time_line_name[16];
	int mirror = 0;

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
	for (i = 0; i < dev_drv->lcdc_win_num; i++) {
		fbi = framebuffer_alloc(0, &fb_pdev->dev);
		if (!fbi) {
			dev_err(&fb_pdev->dev, "fb framebuffer_alloc fail!");
			return -ENOMEM;
		}
		fb_par = devm_kzalloc(&fb_pdev->dev, sizeof(struct rk_fb_par),
				      GFP_KERNEL);
		if (!fb_par) {
			dev_err(&fb_pdev->dev, "malloc fb_par for fb%d fail!",
				rk_fb->num_fb);
			return -ENOMEM;
		}
		fb_par->id = rk_fb->num_fb;
		fb_par->lcdc_drv = dev_drv;
		fbi->par = (void *)fb_par;
		fbi->var = def_var;
		fbi->fix = def_fix;
		sprintf(fbi->fix.id, "fb%d", rk_fb->num_fb);
		fb_videomode_to_var(&fbi->var, &dev_drv->cur_screen->mode);
		if (dev_drv->dsp_mode == ONE_VOP_DUAL_MIPI_VER_SCAN) {
			fbi->var.xres /= 2;
			fbi->var.yres *= 2;
			fbi->var.xres_virtual /= 2;
			fbi->var.yres_virtual *= 2;
		}
		fbi->var.width = dev_drv->cur_screen->width;
		fbi->var.height = dev_drv->cur_screen->height;
		fbi->var.grayscale |=
		    ((fbi->var.xres & 0xfff) << 8) + (fbi->var.yres << 20);
		fbi->var.reserved[0] |= (fbi->var.xres >> 12);
#if defined(CONFIG_LOGO_LINUX_BMP)
		fbi->var.bits_per_pixel = 32;
#else
		fbi->var.bits_per_pixel = 16;
#endif
		fbi->fix.line_length =
		    (fbi->var.xres_virtual) * (fbi->var.bits_per_pixel >> 3);
		if (dev_drv->iommu_enabled)
			fb_ops.fb_mmap = rk_fb_mmap;
		fbi->fbops = &fb_ops;
		fbi->flags = FBINFO_FLAG_DEFAULT;
		fbi->pseudo_palette = dev_drv->win[i]->pseudo_pal;
		ret = register_framebuffer(fbi);
		if (ret < 0) {
			dev_err(&fb_pdev->dev,
				"%s fb%d register_framebuffer fail!\n",
				__func__, rk_fb->num_fb);
			return ret;
		}
		rkfb_create_sysfs(fbi);
		rk_fb->fb[rk_fb->num_fb] = fbi;
		dev_info(fbi->dev, "rockchip framebuffer registerd:%s\n",
			 fbi->fix.id);
		rk_fb->num_fb++;

		if (i == 0) {
			init_waitqueue_head(&dev_drv->vsync_info.wait);
			init_waitqueue_head(&dev_drv->update_regs_wait);
			ret = device_create_file(fbi->dev, &dev_attr_vsync);
			if (ret)
				dev_err(fbi->dev,
					"failed to create vsync file\n");
			dev_drv->vsync_info.thread =
			    kthread_run(rk_fb_wait_for_vsync_thread, dev_drv,
					"fb-vsync");
			if (dev_drv->vsync_info.thread == ERR_PTR(-ENOMEM)) {
				dev_err(fbi->dev,
					"failed to run vsync thread\n");
				dev_drv->vsync_info.thread = NULL;
			}
			dev_drv->vsync_info.active = 1;

			mutex_init(&dev_drv->output_lock);

			INIT_LIST_HEAD(&dev_drv->update_regs_list);
			INIT_LIST_HEAD(&dev_drv->saved_list);
			mutex_init(&dev_drv->update_regs_list_lock);
			init_kthread_worker(&dev_drv->update_regs_worker);

			dev_drv->update_regs_thread =
			    kthread_run(kthread_worker_fn,
					&dev_drv->update_regs_worker, "rk-fb");
			if (IS_ERR(dev_drv->update_regs_thread)) {
				int err = PTR_ERR(dev_drv->update_regs_thread);

				dev_drv->update_regs_thread = NULL;
				pr_info("failed to run update_regs thread\n");
				return err;
			}
			init_kthread_work(&dev_drv->update_regs_work,
					  rk_fb_update_regs_handler);

			snprintf(time_line_name, sizeof(time_line_name),
				 "vop%d-timeline", id);
			dev_drv->timeline =
			    sw_sync_timeline_create(time_line_name);
			dev_drv->timeline_max = 1;
		}
	}

	/* show logo for primary display device */
#if !defined(CONFIG_FRAMEBUFFER_CONSOLE)
	if (dev_drv->prop == PRMRY) {
		u16 xact, yact;
		int format;
		u32 dsp_addr;
		struct fb_info *main_fbi = rk_fb->fb[0];

		main_fbi->fbops->fb_open(main_fbi, 1);
		main_fbi->var.pixclock = dev_drv->pixclock;
		if (dev_drv->iommu_enabled) {
			if (dev_drv->mmu_dev)
				rockchip_iovmm_set_fault_handler(dev_drv->dev,
						rk_fb_sysmmu_fault_handler);
		}

		rk_fb_alloc_buffer(main_fbi);	/* only alloc memory for main fb */
		dev_drv->uboot_logo = support_uboot_display();

		if (dev_drv->uboot_logo &&
		    uboot_logo_offset && uboot_logo_base) {
			int width, height, bits, xvir;
			phys_addr_t start = uboot_logo_base + uboot_logo_offset;
			unsigned int size = uboot_logo_size - uboot_logo_offset;
			unsigned int nr_pages;
			int ymirror = 0;
			struct page **pages;
			char *vaddr;
			int logo_len, i = 0;

			if (dev_drv->ops->get_dspbuf_info)
				dev_drv->ops->get_dspbuf_info(dev_drv, &xact,
					&yact, &format,	&dsp_addr, &ymirror);
			logo_len = rk_fb_pixel_width(format) * xact * yact >> 3;
			nr_pages = size >> PAGE_SHIFT;
			pages = kzalloc(sizeof(struct page) * nr_pages,
					GFP_KERNEL);
			if (!pages)
				return -ENOMEM;
			while (i < nr_pages) {
				pages[i] = phys_to_page(start);
				start += PAGE_SIZE;
				i++;
			}
			vaddr = vmap(pages, nr_pages, VM_MAP, PAGE_KERNEL);
			if (!vaddr) {
				pr_err("failed to vmap phy addr 0x%lx\n",
				       (long)(uboot_logo_base +
				       uboot_logo_offset));
				kfree(pages);
				return -1;
			}

			if (bmpdecoder(vaddr, main_fbi->screen_base, &width,
				       &height, &bits)) {
				kfree(pages);
				vunmap(vaddr);
				return 0;
			}
			kfree(pages);
			vunmap(vaddr);
			if (width != xact || height != yact) {
				pr_err("can't support uboot kernel logo use different size [%dx%d] != [%dx%d]\n",
				       xact, yact, width, height);
				return 0;
			}
			xvir = ALIGN(width * bits, 1 << 5) >> 5;
			ymirror = 0;
			local_irq_save(flags);
			if (dev_drv->ops->wait_frame_start)
				dev_drv->ops->wait_frame_start(dev_drv, 0);
			mirror = ymirror || dev_drv->cur_screen->y_mirror;
			if (dev_drv->ops->post_dspbuf) {
				dev_drv->ops->post_dspbuf(dev_drv,
					main_fbi->fix.smem_start +
					(mirror ? logo_len : 0),
					rk_fb_data_fmt(0, bits),
					width, height, xvir,
					ymirror);
			}
			if (dev_drv->iommu_enabled) {
				rk_fb_poll_wait_frame_complete();
				if (dev_drv->ops->mmu_en)
					dev_drv->ops->mmu_en(dev_drv);
				freed_index = 0;
			}
			local_irq_restore(flags);
			return 0;
		} else if (dev_drv->uboot_logo && uboot_logo_base) {
			u32 start = uboot_logo_base;
			int logo_len, i = 0;
			int y_mirror = 0;
			unsigned int nr_pages;
			struct page **pages;
			char *vaddr;
			int align = 0, xvir;

			dev_drv->ops->get_dspbuf_info(dev_drv, &xact,
						      &yact, &format,
						      &start,
						      &y_mirror);
			logo_len = rk_fb_pixel_width(format) * xact * yact >> 3;
			if (logo_len > uboot_logo_size ||
			    logo_len > main_fbi->fix.smem_len) {
				pr_err("logo size > uboot reserve buffer size\n");
				return -1;
			}
			if (y_mirror)
				start -= logo_len;

			align = start % PAGE_SIZE;
			start -= align;
			nr_pages = PAGE_ALIGN(logo_len + align) >> PAGE_SHIFT;
			pages = kzalloc(sizeof(struct page) * nr_pages,
					GFP_KERNEL);
			if (!pages)
				return -ENOMEM;
			while (i < nr_pages) {
				pages[i] = phys_to_page(start);
				start += PAGE_SIZE;
				i++;
			}
			vaddr = vmap(pages, nr_pages, VM_MAP, PAGE_KERNEL);
			if (!vaddr) {
				pr_err("failed to vmap phy addr 0x%x\n",
				       start);
				kfree(pages);
				return -1;
			}

			memcpy(main_fbi->screen_base, vaddr + align, logo_len);

			kfree(pages);
			vunmap(vaddr);
			xvir = ALIGN(xact * rk_fb_pixel_width(format),
				     1 << 5) >> 5;
			local_irq_save(flags);
			if (dev_drv->ops->wait_frame_start)
				dev_drv->ops->wait_frame_start(dev_drv, 0);
			mirror = y_mirror || dev_drv->cur_screen->y_mirror;
			dev_drv->ops->post_dspbuf(dev_drv,
					main_fbi->fix.smem_start +
					(mirror ? logo_len : 0),
					format,	xact, yact,
					xvir,
					y_mirror);
			if (dev_drv->iommu_enabled) {
				rk_fb_poll_wait_frame_complete();
				if (dev_drv->ops->mmu_en)
					dev_drv->ops->mmu_en(dev_drv);
				freed_index = 0;
			}
			local_irq_restore(flags);
			return 0;
		} else {
			if (dev_drv->iommu_enabled) {
				if (dev_drv->ops->mmu_en)
					dev_drv->ops->mmu_en(dev_drv);
				freed_index = 0;
			}
		}
#if defined(CONFIG_LOGO)
		main_fbi->fbops->fb_set_par(main_fbi);
#if  defined(CONFIG_LOGO_LINUX_BMP)
		if (fb_prewine_bmp_logo(main_fbi, FB_ROTATE_UR)) {
			fb_set_cmap(&main_fbi->cmap, main_fbi);
			fb_show_bmp_logo(main_fbi, FB_ROTATE_UR);
		}
#else
		if (fb_prepare_logo(main_fbi, FB_ROTATE_UR)) {
			fb_set_cmap(&main_fbi->cmap, main_fbi);
			fb_show_logo(main_fbi, FB_ROTATE_UR);
		}
#endif
		main_fbi->fbops->fb_pan_display(&main_fbi->var, main_fbi);
#endif
	} else {
		struct fb_info *extend_fbi = rk_fb->fb[dev_drv->fb_index_base];

		extend_fbi->var.pixclock = rk_fb->fb[0]->var.pixclock;
		if (rk_fb->disp_mode == DUAL_LCD) {
			extend_fbi->fbops->fb_open(extend_fbi, 1);
			if (dev_drv->iommu_enabled) {
				if (dev_drv->mmu_dev)
					rockchip_iovmm_set_fault_handler(dev_drv->dev,
									 rk_fb_sysmmu_fault_handler);
			}
			rk_fb_alloc_buffer(extend_fbi);
		}
	}
#endif
	return 0;
}

int rk_fb_unregister(struct rk_lcdc_driver *dev_drv)
{
	struct rk_fb *fb_inf = platform_get_drvdata(fb_pdev);
	struct fb_info *fbi;
	int fb_index_base = dev_drv->fb_index_base;
	int fb_num = dev_drv->lcdc_win_num;
	int i = 0;

	if (fb_inf->lcdc_dev_drv[i]->vsync_info.thread) {
		fb_inf->lcdc_dev_drv[i]->vsync_info.irq_stop = 1;
		kthread_stop(fb_inf->lcdc_dev_drv[i]->vsync_info.thread);
	}

	for (i = 0; i < fb_num; i++)
		kfree(dev_drv->win[i]);

	for (i = fb_index_base; i < (fb_index_base + fb_num); i++) {
		fbi = fb_inf->fb[i];
		unregister_framebuffer(fbi);
		/* rk_release_fb_buffer(fbi); */
		framebuffer_release(fbi);
	}
	fb_inf->lcdc_dev_drv[dev_drv->id] = NULL;
	fb_inf->num_lcdc--;

	return 0;
}

int rk_fb_set_car_reverse_status(struct rk_lcdc_driver *dev_drv,
				 int status)
{
	char *envp[3] = {"Request", "FORCE UPDATE", NULL};

	if (status) {
		car_reversing = 1;
		flush_kthread_worker(&dev_drv->update_regs_worker);
		dev_drv->timeline_max++;
#ifdef H_USE_FENCE
		sw_sync_timeline_inc(dev_drv->timeline, 1);
#endif
		pr_debug("%s: camcap reverse start...\n", __func__);
	} else {
		car_reversing = 0;
		kobject_uevent_env(&dev_drv->dev->kobj,
				   KOBJ_CHANGE, envp);
		pr_debug("%s: camcap reverse finish...\n", __func__);
	}

	return 0;
}

static int rk_fb_probe(struct platform_device *pdev)
{
	struct rk_fb *rk_fb = NULL;
	struct device_node *np = pdev->dev.of_node;
	u32 mode, ret;
	struct device_node *node;

	if (!np) {
		dev_err(&pdev->dev, "Missing device tree node.\n");
		return -EINVAL;
	}

	rk_fb = devm_kzalloc(&pdev->dev, sizeof(struct rk_fb), GFP_KERNEL);
	if (!rk_fb) {
		dev_err(&pdev->dev, "kmalloc for rk fb fail!");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, rk_fb);

	if (!of_property_read_u32(np, "rockchip,disp-mode", &mode)) {
		rk_fb->disp_mode = mode;

	} else {
		dev_err(&pdev->dev, "no disp-mode node found!");
		return -ENODEV;
	}

	if (!of_property_read_u32(np, "rockchip,disp-policy", &mode)) {
		rk_fb->disp_policy = mode;
		pr_info("fb disp policy is %s\n",
			rk_fb->disp_policy ? "box" : "sdk");
	}

	if (!of_property_read_u32(np, "rockchip,uboot-logo-on", &uboot_logo_on))
		pr_info("uboot-logo-on:%d\n", uboot_logo_on);

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

	node = of_parse_phandle(np, "memory-region", 0);
	if (node) {
		struct resource r;

		ret = of_address_to_resource(node, 0, &r);
		if (ret)
			return ret;

		if (uboot_logo_on) {
			uboot_logo_base = r.start;
			uboot_logo_size = resource_size(&r);

			if (uboot_logo_size > SZ_16M)
				uboot_logo_offset = SZ_16M;
			else
				uboot_logo_offset = 0;
		}
		pr_info("logo: base=0x%llx, size=0x%llx, offset=0x%llx\n",
			uboot_logo_base, uboot_logo_size, uboot_logo_offset);
	}

	fb_pdev = pdev;
	dev_info(&pdev->dev, "rockchip framebuffer driver probe\n");
	return 0;
}

static int rk_fb_remove(struct platform_device *pdev)
{
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
		sw_sync_timeline_inc(rk_fb->lcdc_dev_drv[i]->timeline, 1);
	}
}

static const struct of_device_id rkfb_dt_ids[] = {
	{.compatible = "rockchip,rk-fb",},
	{}
};

static struct platform_driver rk_fb_driver = {
	.probe = rk_fb_probe,
	.remove = rk_fb_remove,
	.driver = {
		   .name = "rk-fb",
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(rkfb_dt_ids),
		   },
	.shutdown = rk_fb_shutdown,
};

static int __init rk_fb_init(void)
{
	return platform_driver_register(&rk_fb_driver);
}

static void __exit rk_fb_exit(void)
{
	platform_driver_unregister(&rk_fb_driver);
}

fs_initcall(rk_fb_init);
module_exit(rk_fb_exit);
