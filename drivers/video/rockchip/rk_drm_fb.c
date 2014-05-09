/*
 * drivers/video/rockchip/rk_fb.c
 *
 * Copyright (C) ROCKCHIP, Inc.
 * Author:yzq<yxj@rock-chips.com>
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
#include <drm/drm_os_linux.h>
#include <linux/of_gpio.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <video/of_display_timing.h>
#include <video/display_timing.h>
#include <dt-bindings/rkfb/rk_fb.h>
#endif

#include <linux/display-sys.h>
#include "rk_drm_fb.h"
__weak int support_uboot_display(void)
{
	return 0;
}
static struct platform_device *drm_fb_pdev;
static struct rk_fb_trsm_ops *trsm_lvds_ops;
static struct rk_fb_trsm_ops *trsm_edp_ops;
static struct rk_fb_trsm_ops *trsm_mipi_ops;
static struct rk_display_device *disp_hdmi_devices;
void rk_drm_display_register(struct rk_display_ops *extend_ops, void *displaydata,int type)
{
	switch(type) {
		case SCREEN_HDMI:
			disp_hdmi_devices = kzalloc(sizeof(struct rk_display_device), GFP_KERNEL);
			disp_hdmi_devices->priv_data = displaydata;
			disp_hdmi_devices->ops = extend_ops;
			break;
		default:
			printk(KERN_WARNING "%s:un supported extend display:%d!\n",
			__func__, type);
		break;
	}
}
int rk_fb_trsm_ops_register(struct rk_fb_trsm_ops *ops, int type)
{
	switch (type) {
	case SCREEN_RGB:
	case SCREEN_LVDS:
	case SCREEN_DUAL_LVDS:
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
		printk(KERN_WARNING "%s:un supported transmitter:%d!\n",
			__func__, type);
		break;
	}
	return 0;
}

struct rk_display_device *rk_drm_extend_display_get(int type)
{
	struct rk_display_device *extend_display = NULL;
	switch (type) {
		case SCREEN_HDMI:
			if(disp_hdmi_devices)
				extend_display = disp_hdmi_devices;
			else
				printk(KERN_WARNING "%s:screen hdmi ops is NULL!\n",__func__);
			break;
		default:
			printk(KERN_WARNING "%s:un supported extend display:%d!\n",
			__func__, type);
			break;
	}
	return extend_display;
}
#if 0
struct void *get_extend_drv(void)
{
	struct rk_display_device *extend_display = rk_drm_extend_display_get(SCREEN_HDMI);
	return extend_display->priv_data;
}
#endif
struct rk_fb_trsm_ops *rk_fb_trsm_ops_get(int type)
{
	struct rk_fb_trsm_ops *ops;
	switch (type) {
	case SCREEN_RGB:
	case SCREEN_LVDS:
	case SCREEN_DUAL_LVDS:
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
		printk(KERN_WARNING "%s:un supported transmitter:%d!\n",
			__func__, type);
		break;
	}
	return ops;
}
/* rk display power control parse from dts
 *
*/
int rk_disp_pwr_ctr_parse_dt(struct rk_lcdc_driver *dev_drv)
{
	struct device_node *root  = of_get_child_by_name(dev_drv->dev->of_node,
				"power_ctr");
	struct device_node *child;
	struct rk_disp_pwr_ctr_list *pwr_ctr;
	struct list_head *pos;
	enum of_gpio_flags flags;
	u32 val = 0;
	u32 debug = 0;
	u32 mirror = 0;
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
				pwr_ctr->pwr_ctr.atv_val = !(flags & OF_GPIO_ACTIVE_LOW);
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

	of_property_read_u32(root, "rockchip,mirror", &mirror);

	if (mirror == NO_MIRROR) {
		dev_drv->screen0->x_mirror = 0;
		dev_drv->screen0->y_mirror = 0;
	} else if (mirror == X_MIRROR) {
		dev_drv->screen0->x_mirror = 1;
		dev_drv->screen0->y_mirror = 0;
	} else if (mirror == Y_MIRROR) {
		dev_drv->screen0->x_mirror = 0;
		dev_drv->screen0->y_mirror = 1;
	} else if(mirror == X_Y_MIRROR) {
		dev_drv->screen0->x_mirror = 1;
		dev_drv->screen0->y_mirror = 1;
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
	screen->lvds_format = dt->lvds_format;
	screen->face = dt->face;

	if (dt->flags & DISPLAY_FLAGS_PIXDATA_POSEDGE)
		screen->pin_dclk = 1;
	else
		screen->pin_dclk = 0;
	if(dt->flags & DISPLAY_FLAGS_HSYNC_HIGH)
		screen->pin_hsync = 1;
	else
		screen->pin_hsync = 0;
	if(dt->flags & DISPLAY_FLAGS_VSYNC_HIGH)
		screen->pin_vsync = 1;
	else
		screen->pin_vsync = 0;
	if(dt->flags & DISPLAY_FLAGS_DE_HIGH)
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
	dt = display_timings_get(disp_timing, 0);
	rk_fb_video_mode_from_timing(dt, screen);
	printk(KERN_ERR "dclk:%d\n"
			 "hactive:%d\n"
			 "hback_porch:%d\n"
			 "hfront_porch:%d\n"
			 "hsync_len:%d\n"
			 "vactive:%d\n"
			 "vback_porch:%d\n"
			 "vfront_porch:%d\n"
			 "vsync_len:%d\n"
			 "screen_type:%d\n"
			 "lvds_format:%d\n"
			 "face:%d\n",
			dt->pixelclock.typ,
			dt->hactive.typ,
			dt->hback_porch.typ,
			dt->hfront_porch.typ,
			dt->hsync_len.typ,
			dt->vactive.typ,
			dt->vback_porch.typ,
			dt->vfront_porch.typ,
			dt->vsync_len.typ,
			dt->screen_type,
			dt->lvds_format,
			dt->face);
	return 0;

}
static int init_lcdc_win(struct rk_lcdc_driver *dev_drv, struct rk_lcdc_win *def_win)
{
	int i;
	int lcdc_win_num = dev_drv->lcdc_win_num;
	for (i = 0; i < lcdc_win_num; i++) {
		struct rk_lcdc_win *win = NULL;
		win =  kzalloc(sizeof(struct rk_lcdc_win), GFP_KERNEL);
		if (!win) {
			dev_err(dev_drv->dev, "kzmalloc for win fail!");
			return   -ENOMEM;
		}

		strcpy(win->name, def_win[i].name);
		win->id = def_win[i].id;
		win->support_3d = def_win[i].support_3d;
		dev_drv->win[i] = win;
	}

	return 0;
}

static int init_lcdc_device_driver(struct rk_drm_screen_private *screen_priv,
					struct rk_lcdc_win *def_win, int index)
{
	struct rk_drm_private *rk_drm_priv = platform_get_drvdata(drm_fb_pdev);
	struct rk_lcdc_driver *dev_drv = screen_priv->lcdc_dev_drv;
	struct rk_screen *screen1 = NULL;
	struct rk_screen *screen = devm_kzalloc(dev_drv->dev,
				sizeof(struct rk_screen), GFP_KERNEL);
	if (!screen) {
		dev_err(dev_drv->dev, "malloc screen for lcdc%d fail!",
					dev_drv->id);
		goto fail_screen;
	}
	
	screen->screen_id = 0;
	screen->lcdc_id = dev_drv->id;
	screen->overscan.left = 100;
	screen->overscan.top = 100;
	screen->overscan.right = 100;
	screen->overscan.bottom = 100;
	dev_drv->screen0 = screen;
	dev_drv->cur_screen = screen;
	/* devie use one lcdc + rk61x scaler for dual display*/
	if (rk_drm_priv->disp_mode == ONE_DUAL) {
		screen1 = devm_kzalloc(dev_drv->dev,
						sizeof(struct rk_screen), GFP_KERNEL);
		if (screen1) {
			dev_err(dev_drv->dev, "malloc screen1 for lcdc%d fail!",
						dev_drv->id);
			goto fail_screen1;
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
	if (dev_drv->prop == PRMRY) {
		rk_fb_set_prmry_screen(screen);
		rk_fb_get_prmry_screen(screen);
		dev_drv->trsm_ops = rk_fb_trsm_ops_get(screen->type);
	}

	return 0;

fail_screen1:
	devm_kfree(dev_drv->dev,screen);
fail_screen:
	
	return -ENOMEM;
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
/********************************
*check if the primary lcdc has registerd,
the primary lcdc mas register first
*********************************/
bool is_prmry_rk_lcdc_registered(void)
{
	struct rk_drm_private *rk_drm_priv = platform_get_drvdata(drm_fb_pdev);
	if (rk_drm_priv->screen_priv[0].lcdc_dev_drv)
		return  true;
	else
		return false;


}
static void rk_fb_update_regs_handler(struct kthread_work *work)
{
	struct rk_lcdc_driver * dev_drv =
			container_of(work, struct rk_lcdc_driver, update_regs_work);
	struct rk_fb_reg_data *data, *next;
#if 0
	//struct list_head saved_list;
	mutex_lock(&dev_drv->update_regs_list_lock);
	saved_list = dev_drv->update_regs_list;
	list_replace_init(&dev_drv->update_regs_list, &saved_list);
	mutex_unlock(&dev_drv->update_regs_list_lock);
	
	list_for_each_entry_safe(data, next, &saved_list, list) {
		//rk_fb_update_reg(dev_drv,data);
		list_del(&data->list);
		kfree(data);
	}
#endif
}
static int rk_fb_wait_for_vsync_thread(void *data)
{
	struct rk_lcdc_driver  *dev_drv = data;
	struct rk_drm_private *rk_drm_priv = platform_get_drvdata(drm_fb_pdev);
	struct rk_drm_screen_private *drm_screen_priv = NULL;
	struct rk_drm_display *drm_display = NULL;

	if(dev_drv->prop == PRMRY)
		drm_screen_priv = &rk_drm_priv->screen_priv[0];
	else if(dev_drv->prop == EXTEND)
		drm_screen_priv = &rk_drm_priv->screen_priv[1];
	if(drm_screen_priv == NULL)
		return -1;
	drm_display = &drm_screen_priv->drm_disp;

	while (!kthread_should_stop()) {
		ktime_t timestamp = dev_drv->vsync_info.timestamp;
		int ret = wait_event_interruptible(dev_drv->vsync_info.wait,
			!ktime_equal(timestamp, dev_drv->vsync_info.timestamp) &&
			(dev_drv->vsync_info.active || dev_drv->vsync_info.irq_stop));
#if 1
		if(atomic_read(&drm_screen_priv->wait_vsync_done)){
			atomic_set(&drm_screen_priv->wait_vsync_done,0);
			DRM_WAKEUP(&drm_screen_priv->wait_vsync_queue);
		}
		if(!ret && drm_display->event_call_back)
			drm_display->event_call_back(drm_display,0,RK_DRM_CALLBACK_VSYNC);
#endif
	}

	return 0;
}

static void rk_drm_irq_handle(struct rk_lcdc_driver *dev_drv)
{
	struct rk_drm_private *rk_drm_priv = platform_get_drvdata(drm_fb_pdev);
	struct rk_drm_screen_private *drm_screen_priv = NULL;
	struct rk_drm_display *drm_display = NULL;
	if(dev_drv->prop == PRMRY)
		drm_screen_priv = &rk_drm_priv->screen_priv[0];
	else if(dev_drv->prop == EXTEND)
		drm_screen_priv = &rk_drm_priv->screen_priv[1];
	if(drm_screen_priv == NULL)
		return -1;
	drm_display = &drm_screen_priv->drm_disp;
	if(atomic_read(&drm_screen_priv->wait_vsync_done)){
			atomic_set(&drm_screen_priv->wait_vsync_done,0);
			DRM_WAKEUP(&drm_screen_priv->wait_vsync_queue);
	}

	if(drm_display->event_call_back)
			drm_display->event_call_back(drm_display,0,RK_DRM_CALLBACK_VSYNC);
}
int rk_fb_register(struct rk_lcdc_driver *dev_drv,
		struct rk_lcdc_win *win, int id)
{
	struct rk_drm_private *rk_drm_priv = platform_get_drvdata(drm_fb_pdev);
	struct rk_drm_display *drm_display = NULL;
	struct rk_drm_screen_private *drm_screen_priv = NULL;
	int i=0;

	if (rk_drm_priv->num_screen == RK30_MAX_LCDC_SUPPORT)
		return -ENXIO;
	for (i = 0; i < RK_DRM_MAX_SCREEN_NUM; i++) {
		if (!rk_drm_priv->screen_priv[i].lcdc_dev_drv) 
			break;
	}
	rk_drm_priv->num_screen++;
	drm_screen_priv = &rk_drm_priv->screen_priv[i];	
	drm_screen_priv->lcdc_dev_drv = dev_drv;
	drm_screen_priv->lcdc_dev_drv->id = id;

	init_lcdc_device_driver(drm_screen_priv,win,i);
	dev_drv->irq_call_back = rk_drm_irq_handle;
	
	drm_display = &drm_screen_priv->drm_disp;
	drm_display->num_win = dev_drv->lcdc_win_num;
	atomic_set(&drm_screen_priv->wait_vsync_done, 1);
	DRM_INIT_WAITQUEUE(&drm_screen_priv->wait_vsync_queue);
	if(dev_drv->prop == PRMRY){
		struct fb_modelist *modelist_new;
		struct fb_modelist *modelist;
		struct fb_videomode *mode;
		drm_display->modelist = kmalloc(sizeof(struct list_head),GFP_KERNEL);
		INIT_LIST_HEAD(drm_display->modelist);
		modelist_new = kmalloc(sizeof(struct fb_modelist),
				  GFP_KERNEL);
		drm_display->screen_type = RK_DRM_PRIMARY_SCREEN;
		drm_display->num_videomode = 1;
		drm_display->best_mode = 0;
		drm_display->is_connected = 1;
		memcpy(&modelist_new->mode,&dev_drv->cur_screen->mode,sizeof(struct fb_videomode));

	//	printk("---->yzq mode xres=%d yres=%d \n",modelist_new->mode.xres,modelist_new->mode.yres);
		list_add_tail(&modelist_new->list,drm_display->modelist);

		modelist = list_first_entry(drm_display->modelist, struct fb_modelist, list);
		mode=&modelist->mode;
	//	printk("---->yzq 1mode xres=%d yres=%d \n",mode->xres,mode->yres);

	}else if(dev_drv->prop == EXTEND){
		struct list_head *modelist;
		drm_screen_priv->ex_display = rk_drm_extend_display_get(SCREEN_HDMI);
	//	printk("------>yzq ex_display=%x\n",drm_screen_priv->ex_display);
		drm_display->screen_type = RK_DRM_EXTEND_SCREEN;
		drm_display->is_connected = 0;
#if 0
		drm_screen_priv->ex_display->ops->getmodelist(drm_screen_priv->ex_display,&modelist);

		memcpy(&drm_display->modelist,modelist,sizeof(struct list_head));
		drm_display->is_connected = drm_screen_priv->ex_display->ops->getstatus(drm_screen_priv->ex_display);
#endif
	}
	if (1){//dev_drv->prop == PRMRY) {
		init_waitqueue_head(&dev_drv->vsync_info.wait);
		dev_drv->vsync_info.thread = kthread_run(rk_fb_wait_for_vsync_thread,
				dev_drv, "fb-vsync");
		if (dev_drv->vsync_info.thread == ERR_PTR(-ENOMEM)) {
			printk( "failed to run vsync thread\n");
			dev_drv->vsync_info.thread = NULL;
		}
		dev_drv->vsync_info.active = 1;

		mutex_init(&dev_drv->output_lock);

		INIT_LIST_HEAD(&dev_drv->update_regs_list);
		mutex_init(&dev_drv->update_regs_list_lock);
		init_kthread_worker(&dev_drv->update_regs_worker);

		dev_drv->update_regs_thread = kthread_run(kthread_worker_fn,
				&dev_drv->update_regs_worker, "rk-fb");
		if (IS_ERR(dev_drv->update_regs_thread)) {
			int err = PTR_ERR(dev_drv->update_regs_thread);
			dev_drv->update_regs_thread = NULL;

			printk("failed to run update_regs thread\n");
			return err;
		}
		init_kthread_work(&dev_drv->update_regs_work, rk_fb_update_regs_handler);

		dev_drv->timeline = sw_sync_timeline_create("fb-timeline");			dev_drv->timeline_max = 1;
	}
	return 0;
}
int rk_fb_unregister(struct rk_lcdc_driver *dev_drv)

{
	struct rk_drm_private *rk_drm_priv = platform_get_drvdata(drm_fb_pdev);
	int i = 0;

	return 0;
}

struct rk_drm_display *rk_drm_get_info(int screen_type)
{
	struct rk_drm_private *rk_drm_priv = platform_get_drvdata(drm_fb_pdev);
	int i=0;
	for( i=0; i<rk_drm_priv->num_screen; i++){
		if(rk_drm_priv->screen_priv[i].drm_disp.screen_type == screen_type)
			break;
	}
	if(i==rk_drm_priv->num_screen){
		printk("--->%s can not find match DISPLAY_TYPE %d\n",__func__,screen_type);
		return NULL;
	}
	
	return &rk_drm_priv->screen_priv[i].drm_disp;
}

static int rk_drm_screen_blank(struct rk_drm_display *drm_disp)
{
//	struct rk_drm_private *rk_drm_priv = platform_get_drvdata(drm_fb_pdev);
	struct rk_drm_screen_private *drm_screen_priv =
	     	container_of(drm_disp, struct rk_drm_screen_private, drm_disp);
	struct rk_lcdc_driver *lcdc_dev = drm_screen_priv->lcdc_dev_drv;

	lcdc_dev->ops->blank(lcdc_dev, 0,drm_disp->enable?FB_BLANK_UNBLANK:FB_BLANK_NORMAL);

	return 0;
}

int rk_fb_disp_scale(u8 scale_x, u8 scale_y, u8 lcdc_id)
{
	return 0;
}
/**********************************************************************
this is for hdmi
name: lcdc device name ,lcdc0 , lcdc1
***********************************************************************/
struct rk_lcdc_driver *rk_get_lcdc_drv(char *name)
{
	struct rk_drm_private *rk_drm_priv = platform_get_drvdata(drm_fb_pdev);
	int i = 0;
	for (i = 0; i < rk_drm_priv->num_screen; i++) {
		if (!strcmp(rk_drm_priv->screen_priv[i].lcdc_dev_drv->name, name))
			break;
	}
	return rk_drm_priv->screen_priv[i].lcdc_dev_drv;

}
int rk_fb_switch_screen(struct rk_screen *screen , int enable, int lcdc_id)
{
	struct rk_drm_private *rk_drm_priv = platform_get_drvdata(drm_fb_pdev);
	struct rk_lcdc_driver *dev_drv = NULL;
	struct rk_drm_display *drm_disp = NULL;
	char name[6];
	int i;
	
	sprintf(name, "lcdc%d", lcdc_id);

	if (rk_drm_priv->disp_mode != DUAL) {
		dev_drv = rk_drm_priv->screen_priv[0].lcdc_dev_drv;
	} else {

		for (i = 0; i < rk_drm_priv->num_screen; i++) {
			if (rk_drm_priv->screen_priv[i].lcdc_dev_drv->prop == EXTEND) {
				drm_disp = &rk_drm_priv->screen_priv[i].drm_disp;
				dev_drv = rk_drm_priv->screen_priv[i].lcdc_dev_drv;
				break;
			}
		}

		if (i == rk_drm_priv->num_screen) {
			printk(KERN_ERR "%s driver not found!", name);
			return -ENODEV;
		}
	}
	printk("hdmi %s lcdc%d\n", enable ? "connect to" : "remove from", dev_drv->id);


	if(enable && !drm_disp->is_connected ){
		struct list_head *modelist;
		struct fb_modelist *modelist1;
		struct fb_videomode *mode;
		struct rk_display_device *ex_display = rk_drm_priv->screen_priv[i].ex_display;
		memcpy(dev_drv->cur_screen, screen, sizeof(struct rk_screen));
		if(ex_display == NULL)
			ex_display = rk_drm_extend_display_get(SCREEN_HDMI);
		rk_drm_priv->screen_priv[i].ex_display = ex_display;
		ex_display->ops->getmodelist(ex_display,&modelist);
		
		drm_disp->modelist = modelist;

		drm_disp->is_connected = true;
		drm_disp->event_call_back(drm_disp,0,RK_DRM_CALLBACK_HOTPLUG);
	}else{
//	printk("----->yzq %s %d \n",__func__,__LINE__);
		drm_disp->is_connected = false;
		drm_disp->event_call_back(drm_disp,0,RK_DRM_CALLBACK_HOTPLUG);
//	printk("----->yzq %s %d \n",__func__,__LINE__);
	}


}
static int rk_drm_screen_videomode_set(struct rk_drm_display *drm_disp)
{
//	struct rk_drm_private *rk_drm_priv = platform_get_drvdata(drm_fb_pdev);
	struct rk_drm_screen_private *drm_screen_priv =
	     	container_of(drm_disp, struct rk_drm_screen_private, drm_disp);
	struct rk_lcdc_driver *lcdc_dev = drm_screen_priv->lcdc_dev_drv;
	struct fb_videomode *mode = drm_disp->mode;
	if(!mode){
		printk(KERN_ERR"-->%s fb_video mode is NULL",__func__);
		return -1;
	}

//	printk("----->yzq %s %d xres=%d yres=%d refresh=%d \n",__func__,__LINE__,mode->xres,mode->yres,mode->refresh);
	if(lcdc_dev->prop == PRMRY){
		if(mode != &lcdc_dev->cur_screen->mode)
			memcpy(&lcdc_dev->cur_screen->mode,mode,sizeof(struct fb_videomode));

	}else{
		struct rk_display_device *ex_display = drm_screen_priv->ex_display;
		if(ex_display == NULL)
			ex_display = rk_drm_extend_display_get(SCREEN_HDMI);

	//	printk("------>yzq ex_display=%x\n",ex_display);
		if(ex_display == NULL){
			printk(KERN_ERR"-->%s can not find extend display ops\n",__func__);
			return -1;
		}
		ex_display->ops->setmode(ex_display, mode);
	}
	if(!lcdc_dev->atv_layer_cnt)
		lcdc_dev->ops->open(lcdc_dev, 0,true);

	lcdc_dev->ops->ovl_mgr(lcdc_dev, 3210, 1);
	lcdc_dev->ops->load_screen(lcdc_dev,1);

	return 0;
}

static int rk_drm_win_commit(struct rk_drm_display *drm_disp,unsigned int win_id)
{
//	struct rk_drm_private *rk_drm_priv = platform_get_drvdata(drm_fb_pdev);
	struct rk_drm_screen_private *drm_screen_priv =
	     	container_of(drm_disp, struct rk_drm_screen_private, drm_disp);
	struct rk_lcdc_driver *lcdc_dev = drm_screen_priv->lcdc_dev_drv;

	unsigned int i=0,j=0;
	for( i=1; i < RK_DRM_WIN_MASK; i=i<<1){
		if(i&win_id ){
			struct rk_lcdc_win *lcdc_win = lcdc_dev->win[j];
			struct rk_win_data *drm_win= &drm_disp->win[j];
			if(!lcdc_win && !drm_win){
				printk(KERN_ERR"---->%s can not find display win%d\n",__func__,j);
				return -1;
			}
			lcdc_win->format = drm_win->format;
			lcdc_win->area[0].xpos = drm_win->xpos;
			lcdc_win->area[0].ypos = drm_win->ypos;
			lcdc_win->area[0].xsize = drm_win->xsize;
			lcdc_win->area[0].ysize = drm_win->ysize;
			lcdc_win->area[0].xact = drm_win->xact;
			lcdc_win->area[0].yact = drm_win->yact;
			lcdc_win->area[0].xvir = drm_win->xvir;
			lcdc_win->area[0].y_vir_stride = drm_win->xvir;
			lcdc_win->area[0].smem_start = drm_win->yrgb_addr;
			lcdc_win->area[0].cbr_start = drm_win->uv_addr;
			lcdc_win->alpha_en = 1;
			if(lcdc_win->state != drm_win->enabled){
				lcdc_dev->ops->open(lcdc_dev, j,drm_win->enabled?true:false);
			}
			lcdc_dev->ops->set_par(lcdc_dev,j);
			lcdc_dev->ops->pan_display(lcdc_dev,j);
		}
		j++;
	}
	return 0;
}

static int rk_drm_display_commit(struct rk_drm_display *drm_disp)
{
//	struct rk_drm_private *rk_drm_priv = platform_get_drvdata(drm_fb_pdev);
	struct rk_drm_screen_private *drm_screen_priv =
	     	container_of(drm_disp, struct rk_drm_screen_private, drm_disp);
	struct rk_lcdc_driver *lcdc_dev = drm_screen_priv->lcdc_dev_drv;
	lcdc_dev->ops->lcdc_reg_update(lcdc_dev);
	return 0;
}

int rk_drm_disp_handle(struct rk_drm_display *drm_disp,unsigned int win_id,unsigned int cmd_id)
{
//	struct rk_drm_private *rk_drm_priv = platform_get_drvdata(drm_fb_pdev);
	struct rk_drm_screen_private *drm_screen_priv =
	     	container_of(drm_disp, struct rk_drm_screen_private, drm_disp);
	int i=0;
	for( i=1; i<RK_DRM_CMD_MASK; i=i<<1){
		switch(i&cmd_id){
			case RK_DRM_SCREEN_SET:
				rk_drm_screen_videomode_set(drm_disp);
				break;
			case RK_DRM_SCREEN_BLANK:
				rk_drm_screen_blank(drm_disp);
				break;
			case RK_DRM_WIN_COMMIT:
				rk_drm_win_commit(drm_disp, win_id);
				break;
			case RK_DRM_DISPLAY_COMMIT:
				if(win_id & i){
					if (!wait_event_timeout(drm_screen_priv->wait_vsync_queue,
								!atomic_read(&drm_screen_priv->wait_vsync_done),
								DRM_HZ/20))
						printk("wait frame timed out.\n");
				}

				rk_drm_display_commit(drm_disp);
				if(win_id & i){
					atomic_set(&drm_screen_priv->wait_vsync_done,1);
				}
		}
			
	}
	return 0;
}

struct rk_drm_display *rk_drm_get_diplay(int screen_type)
{
	struct rk_drm_private *rk_drm_priv = platform_get_drvdata(drm_fb_pdev);
	int i=0;
	for( i=0; i < rk_drm_priv->num_screen; i++){
		if(rk_drm_priv->screen_priv[i].drm_disp.screen_type == screen_type)
			return &rk_drm_priv->screen_priv[i].drm_disp;
	}

	return NULL;
}


static int rk_drm_fb_probe(struct platform_device *pdev)
{
	struct rk_drm_private  *rk_drm_priv= NULL;
	struct device_node *np = pdev->dev.of_node;
	u32 mode;

	if (!np) {
		dev_err(&pdev->dev, "Missing device tree node.\n");
		return -EINVAL;
	}

	rk_drm_priv= devm_kzalloc(&pdev->dev, sizeof(struct rk_drm_private), GFP_KERNEL);
	if (!rk_drm_priv) {
		dev_err(&pdev->dev, "kmalloc for rk_drm_priv fail!");
		return  -ENOMEM;
	}
	platform_set_drvdata(pdev, rk_drm_priv);

	if (!of_property_read_u32(np, "rockchip,disp-mode", &mode)) {
		rk_drm_priv->disp_mode = mode;
	} else {
		dev_err(&pdev->dev, "no disp-mode node found!");
		return -ENODEV;
	}
	dev_set_name(&pdev->dev, "rockchip-drmfb");

	drm_fb_pdev = pdev;
	dev_info(&pdev->dev, "rockchip drm framebuffer driver probe\n");
	return 0;
}

static int rk_drm_fb_remove(struct platform_device *pdev)
{
	struct rk_drm_private  *rk_drm_priv = platform_get_drvdata(pdev);
	kfree(rk_drm_priv);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static void rk_drm_fb_shutdown(struct platform_device *pdev)
{
	return;
}


static const struct of_device_id rk_drm_fb_dt_ids[] = {
	{ .compatible = "rockchip,rk-fb", },
	{}
};

static struct platform_driver rk_drm_fb_driver = {
	.probe		= rk_drm_fb_probe,
	.remove		= rk_drm_fb_remove,
	.driver		= {
		.name	= "rk-fb",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(rk_drm_fb_dt_ids),
	},
	.shutdown   = rk_drm_fb_shutdown,
};

static int __init rk_drm_fb_init(void)
{
	return platform_driver_register(&rk_drm_fb_driver);
}

static void __exit rk_drm_fb_exit(void)
{
	platform_driver_unregister(&rk_drm_fb_driver);
}

fs_initcall(rk_drm_fb_init);
module_exit(rk_drm_fb_exit);
