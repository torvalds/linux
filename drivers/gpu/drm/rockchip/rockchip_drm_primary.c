/*
 * rockchip_drm_primary.c
 *
 * Copyright (C) ROCKCHIP, Inc.
 * Author:yzq<yzq@rock-chips.com>
 *
 * based on exynos_drm_fimd.c
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <drm/drmP.h>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>

#include <video/of_display_timing.h>
#include <drm/rockchip_drm.h>
#include <linux/rockchip/cpu.h>
#include <linux/rockchip/iomap.h>
#include <linux/rk_fb.h>
#include <video/display_timing.h>
#include <linux/rockchip/cpu.h>
#include <linux/rockchip/iomap.h>
#include <linux/rockchip/grf.h>
#include "rockchip_drm_drv.h"
#include "rockchip_drm_fbdev.h"
#include "rockchip_drm_crtc.h"
#include "rockchip_drm_iommu.h"
#include "rockchip_drm_primary.h"
static struct device *g_dev = NULL;

static bool primary_display_is_connected(struct device *dev)
{

	/* TODO. */

	return false;
}

static void *primary_get_panel(struct device *dev)
{
	struct primary_context *ctx = get_primary_context(dev);

	DRM_DEBUG_KMS("%s\n", __FILE__);

	return ctx->panel;
}

static int primary_check_timing(struct device *dev, void *timing)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	/* TODO. */

	return 0;
}

static int primary_display_power_on(struct device *dev, int mode)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	/* TODO */

	return 0;
}

static struct rockchip_drm_display_ops primary_display_ops = {
	.type = ROCKCHIP_DISPLAY_TYPE_LCD,
	.is_connected = primary_display_is_connected,
	.get_panel = primary_get_panel,
	.check_timing = primary_check_timing,
	.power_on = primary_display_power_on,
};

static void primary_dpms(struct device *subdrv_dev, int mode)
{
	struct primary_context *ctx = get_primary_context(subdrv_dev);

	DRM_DEBUG_KMS("%s, %d\n", __FILE__, mode);

	mutex_lock(&ctx->lock);

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		/*
		 * enable primary hardware only if suspended status.
		 *
		 * P.S. primary_dpms function would be called at booting time so
		 * clk_enable could be called double time.
		 */

		if (ctx->suspended)
			pm_runtime_get_sync(subdrv_dev);
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		if (!ctx->suspended)
			pm_runtime_put_sync(subdrv_dev);
		break;
	default:
		DRM_DEBUG_KMS("unspecified mode %d\n", mode);
		break;
	}

	mutex_unlock(&ctx->lock);
}

static void primary_apply(struct device *subdrv_dev)
{
	struct primary_context *ctx = get_primary_context(subdrv_dev);
	struct rockchip_drm_manager *mgr = ctx->subdrv.manager;
	struct rockchip_drm_manager_ops *mgr_ops = mgr->ops;
	struct rockchip_drm_overlay_ops *ovl_ops = mgr->overlay_ops;
	struct primary_win_data *win_data;
	int i;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	for (i = 0; i < WINDOWS_NR; i++) {
		win_data = &ctx->win_data[i];
		if (win_data->enabled && (ovl_ops && ovl_ops->commit))
			ovl_ops->commit(subdrv_dev, i);
	}

	if (mgr_ops && mgr_ops->commit)
		mgr_ops->commit(subdrv_dev);
}

static void primary_commit(struct device *dev)
{
	struct primary_context *ctx = get_primary_context(dev);
	struct rk_drm_display *drm_disp = ctx->drm_disp;
	struct rockchip_drm_panel_info *panel = (struct rockchip_drm_panel_info *)primary_get_panel(dev);
	struct fb_videomode *mode;

	printk(KERN_ERR"%s %d\n", __func__,__LINE__);
	if (ctx->suspended)
		return;
	drm_disp->mode = &panel->timing;
	drm_disp->enable = true;
	rk_drm_disp_handle(drm_disp,0,RK_DRM_SCREEN_SET);
}

static int primary_enable_vblank(struct device *dev)
{
	struct primary_context *ctx = get_primary_context(dev);

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (ctx->suspended)
		return -EPERM;

	return 0;
}

static void primary_disable_vblank(struct device *dev)
{
	struct primary_context *ctx = get_primary_context(dev);

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (ctx->suspended)
		return;
}

static void primary_wait_for_vblank(struct device *dev)
{
	struct primary_context *ctx = get_primary_context(dev);

	if (ctx->suspended)
		return;

	atomic_set(&ctx->wait_vsync_event, 1);

	if (!wait_event_timeout(ctx->wait_vsync_queue,
				!atomic_read(&ctx->wait_vsync_event),
				DRM_HZ/20))
		DRM_DEBUG_KMS("vblank wait timed out.\n");
}

static void primary_event_call_back_handle(struct rk_drm_display *drm_disp,int win_id,int event)
{
	struct primary_context *ctx = get_primary_context(g_dev);
	struct rockchip_drm_subdrv *subdrv = &ctx->subdrv;
	struct rockchip_drm_manager *manager = subdrv->manager;
	struct drm_device *drm_dev = subdrv->drm_dev;
	switch(event){
		case RK_DRM_CALLBACK_VSYNC:
			/* check the crtc is detached already from encoder */
			if (manager->pipe < 0)
				return;

			drm_handle_vblank(drm_dev, manager->pipe);
			rockchip_drm_crtc_finish_pageflip(drm_dev, manager->pipe);
			/* set wait vsync event to zero and wake up queue. */
			if (atomic_read(&ctx->wait_vsync_event)) {
				atomic_set(&ctx->wait_vsync_event, 0);
				DRM_WAKEUP(&ctx->wait_vsync_queue);
			}
			break;
		default:
			printk(KERN_ERR"-->%s unhandle event %d\n",__func__,event);
			break;
	}
}
static struct rockchip_drm_manager_ops primary_manager_ops = {
	.dpms = primary_dpms,
	.apply = primary_apply,
	.commit = primary_commit,
	.enable_vblank = primary_enable_vblank,
	.disable_vblank = primary_disable_vblank,
	.wait_for_vblank = primary_wait_for_vblank,
};

static void primary_win_mode_set(struct device *dev,
			      struct rockchip_drm_overlay *overlay)
{
	struct primary_context *ctx = get_primary_context(dev);
	struct primary_win_data *win_data;
	int win;
	unsigned long offset;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (!overlay) {
		dev_err(dev, "overlay is NULL\n");
		return;
	}

	win = overlay->zpos;
	if (win == DEFAULT_ZPOS)
		win = ctx->default_win;

	if (win < 0 || win > WINDOWS_NR)
		return;

	offset = overlay->fb_x * (overlay->bpp >> 3);
	offset += overlay->fb_y * overlay->pitch;

//	printk("offset = 0x%lx, pitch = %x\n", offset, overlay->pitch);
//	printk("crtc_x=%d crtc_y=%d crtc_width=%d crtc_height=%d\n",overlay->crtc_x,overlay->crtc_y,overlay->crtc_width,overlay->crtc_height);
//	printk("fb_width=%d fb_height=%d dma_addr=%x offset=%x\n",overlay->fb_width,overlay->fb_height,overlay->dma_addr[0],offset);

	win_data = &ctx->win_data[win];

	win_data->offset_x = overlay->crtc_x;
	win_data->offset_y = overlay->crtc_y;
	win_data->ovl_width = overlay->crtc_width;
	win_data->ovl_height = overlay->crtc_height;
	win_data->fb_width = overlay->fb_width;
	win_data->fb_height = overlay->fb_height;
	win_data->dma_addr = overlay->dma_addr[0] + offset;
	win_data->bpp = overlay->bpp;
	win_data->buf_offsize = (overlay->fb_width - overlay->crtc_width) *
				(overlay->bpp >> 3);
	win_data->line_size = overlay->crtc_width * (overlay->bpp >> 3);

}

static void primary_win_set_pixfmt(struct device *dev, unsigned int win)
{
	struct primary_context *ctx = get_primary_context(dev);
	struct primary_win_data *win_data = &ctx->win_data[win];
}

static void primary_win_set_colkey(struct device *dev, unsigned int win)
{
//	struct primary_context *ctx = get_primary_context(dev);

	DRM_DEBUG_KMS("%s\n", __FILE__);

}
#if 0
static ktime_t win_start;
static ktime_t win_end;
static ktime_t win_start1;
static ktime_t win_end1;
#endif
static void primary_win_commit(struct device *dev, int zpos)
{
	struct primary_context *ctx = get_primary_context(dev);
	struct rk_drm_display *drm_disp = ctx->drm_disp;
	struct rk_win_data *rk_win = NULL; 
	struct primary_win_data *win_data;
	int win = zpos;
	unsigned long val,  size;
	u32 xpos, ypos;

//	printk(KERN_ERR"%s %d\n", __func__,__LINE__);

	if (ctx->suspended)
		return;

	if (win == DEFAULT_ZPOS)
		win = ctx->default_win;

	if (win < 0 || win > WINDOWS_NR)
		return;
#if 0
	if(win == 0){
		win_start = ktime_get();
		win_start = ktime_sub(win_start, win_end);
		printk("user flip buffer time %dus\n", (int)ktime_to_us(win_start));
	//	win_start = ktime_get();
	}
#endif
	rk_win = &drm_disp->win[win];
	win_data = &ctx->win_data[win];
	switch(win_data->bpp){
		case 32:
			rk_win->format = ARGB888;
			break;
		case 24:
			rk_win->format = RGB888;
			break;
		case 16:
			rk_win->format = RGB565;
			break;
		default:
			printk("not support format %d\n",win_data->bpp);
			break;
	}

	rk_win->xpos = win_data->offset_x;
	rk_win->ypos = win_data->offset_y;
	rk_win->xact = win_data->ovl_width;
	rk_win->yact = win_data->ovl_height;
	rk_win->xsize = win_data->ovl_width;
	rk_win->ysize = win_data->ovl_height;
	rk_win->xvir = win_data->fb_width;
	rk_win->yrgb_addr = win_data->dma_addr;
	rk_win->enabled = true;

	rk_drm_disp_handle(drm_disp,1<<win,RK_DRM_WIN_COMMIT | RK_DRM_DISPLAY_COMMIT);
		
	win_data->enabled = true;
#if 0
	if(win ==0){
	//	win_end = ktime_get();
	//	win_end = ktime_sub(win_end, win_start);
	//	printk("flip buffer time %dus\n", (int)ktime_to_us(win_end));
		win_end = ktime_get();
	}
#endif

}

static void primary_win_disable(struct device *dev, int zpos)
{
	struct primary_context *ctx = get_primary_context(dev);
	struct rk_drm_display *drm_disp = ctx->drm_disp;
	struct primary_win_data *win_data;
	int win = zpos;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (win == DEFAULT_ZPOS)
		win = ctx->default_win;

	if (win < 0 || win > WINDOWS_NR)
		return;

	win_data = &ctx->win_data[win];

	if (ctx->suspended) {
		/* do not resume this window*/
		win_data->resume = false;
		return;
	}
	drm_disp->win[win].enabled = false;
	rk_drm_disp_handle(drm_disp,1<<win,RK_DRM_WIN_COMMIT | RK_DRM_DISPLAY_COMMIT);

	win_data->enabled = false;
}

static struct rockchip_drm_overlay_ops primary_overlay_ops = {
	.mode_set = primary_win_mode_set,
	.commit = primary_win_commit,
	.disable = primary_win_disable,
};

static struct rockchip_drm_manager primary_manager = {
	.pipe		= -1,
	.ops		= &primary_manager_ops,
	.overlay_ops	= &primary_overlay_ops,
	.display_ops	= &primary_display_ops,
};
#if 0
static irqreturn_t primary_irq_handler(int irq, void *dev_id)
{
	struct primary_context *ctx = (struct primary_context *)dev_id;
	struct rockchip_drm_subdrv *subdrv = &ctx->subdrv;
	struct drm_device *drm_dev = subdrv->drm_dev;
	struct rockchip_drm_manager *manager = subdrv->manager;
	u32 intr0_reg;



	/* check the crtc is detached already from encoder */
	if (manager->pipe < 0)
		goto out;

	drm_handle_vblank(drm_dev, manager->pipe);
	rockchip_drm_crtc_finish_pageflip(drm_dev, manager->pipe);

	/* set wait vsync event to zero and wake up queue. */
	if (atomic_read(&ctx->wait_vsync_event)) {
		atomic_set(&ctx->wait_vsync_event, 0);
		DRM_WAKEUP(&ctx->wait_vsync_queue);
	}
out:
	return IRQ_HANDLED;
}
#endif
static int primary_subdrv_probe(struct drm_device *drm_dev, struct device *dev)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	/*
	 * enable drm irq mode.
	 * - with irq_enabled = 1, we can use the vblank feature.
	 *
	 * P.S. note that we wouldn't use drm irq handler but
	 *	just specific driver own one instead because
	 *	drm framework supports only one irq handler.
	 */
	drm_dev->irq_enabled = 1;

	/*
	 * with vblank_disable_allowed = 1, vblank interrupt will be disabled
	 * by drm timer once a current process gives up ownership of
	 * vblank event.(after drm_vblank_put function is called)
	 */
	drm_dev->vblank_disable_allowed = 1;

	/* attach this sub driver to iommu mapping if supported. */
	if (is_drm_iommu_supported(drm_dev))
		drm_iommu_attach_device(drm_dev, dev);

	return 0;
}

static void primary_subdrv_remove(struct drm_device *drm_dev, struct device *dev)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	/* detach this sub driver from iommu mapping if supported. */
	if (is_drm_iommu_supported(drm_dev))
		drm_iommu_detach_device(drm_dev, dev);
}


static void primary_clear_win(struct primary_context *ctx, int win)
{
	u32 val;

	DRM_DEBUG_KMS("%s\n", __FILE__);

}


static void primary_window_suspend(struct device *dev)
{
	struct primary_win_data *win_data = NULL;
	struct primary_context *ctx = get_primary_context(dev);
	int i;

	for (i = 0; i < WINDOWS_NR; i++) {
		win_data = &ctx->win_data[i];
		win_data->resume = win_data->enabled;
		primary_win_disable(dev, i);
	}
	primary_wait_for_vblank(dev);
}

static void primary_window_resume(struct device *dev)
{
	struct primary_context *ctx = get_primary_context(dev);
	struct primary_win_data *win_data;
	int i;

	for (i = 0; i < WINDOWS_NR; i++) {
		win_data = &ctx->win_data[i];
		win_data->enabled = win_data->resume;
		win_data->resume = false;
	}
}

static int primary_activate(struct primary_context *ctx, bool enable)
{
	struct device *dev = ctx->subdrv.dev;
	struct rk_drm_display *drm_disp = ctx->drm_disp;
	if (enable) {
		int ret;

		ctx->suspended = false;

		drm_disp->enable = true;

		rk_drm_disp_handle(drm_disp,0,RK_DRM_SCREEN_BLANK);

		/* if vblank was enabled status, enable it again. */
		if (ctx->vblank_en)
			primary_enable_vblank(dev);

		primary_window_resume(dev);
	} else {
		primary_window_suspend(dev);

		drm_disp->enable = false;

		rk_drm_disp_handle(drm_disp,0,RK_DRM_SCREEN_BLANK);

		ctx->suspended = true;
	}

	return 0;
}

static int primary_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct primary_context *ctx;
	struct rockchip_drm_subdrv *subdrv;
	struct rockchip_drm_panel_info *panel;
	struct rk_drm_display *drm_display = NULL;
	struct fb_modelist *modelist;
	struct fb_videomode *mode;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	g_dev = dev;
	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	panel = devm_kzalloc(dev, sizeof(struct rockchip_drm_panel_info), GFP_KERNEL);
	ctx->panel = panel;

	drm_display = rk_drm_get_diplay(RK_DRM_PRIMARY_SCREEN);
	ctx->drm_disp = drm_display;
	ctx->default_win = 0;
	modelist = list_first_entry(drm_display->modelist, struct fb_modelist, list);
	mode = &modelist->mode;
	memcpy(&panel->timing,mode,sizeof(struct fb_videomode));

	drm_display->event_call_back = primary_event_call_back_handle;

	DRM_INIT_WAITQUEUE(&ctx->wait_vsync_queue);
	atomic_set(&ctx->wait_vsync_event, 0);

	subdrv = &ctx->subdrv;

	subdrv->dev = dev;
	subdrv->manager = &primary_manager;
	subdrv->probe = primary_subdrv_probe;
	subdrv->remove = primary_subdrv_remove;

	mutex_init(&ctx->lock);

	platform_set_drvdata(pdev, ctx);

	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);
	
	//primary_commit(dev);
	primary_activate(ctx, true);

	rockchip_drm_subdrv_register(subdrv);

	return 0;
}

static int primary_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct primary_context *ctx = platform_get_drvdata(pdev);

	DRM_DEBUG_KMS("%s\n", __FILE__);

	rockchip_drm_subdrv_unregister(&ctx->subdrv);

	if (ctx->suspended)
		goto out;

	pm_runtime_set_suspended(dev);
	pm_runtime_put_sync(dev);

out:
	pm_runtime_disable(dev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int primary_suspend(struct device *dev)
{
	struct primary_context *ctx = get_primary_context(dev);

	/*
	 * do not use pm_runtime_suspend(). if pm_runtime_suspend() is
	 * called here, an error would be returned by that interface
	 * because the usage_count of pm runtime is more than 1.
	 */
	if (!pm_runtime_suspended(dev))
		return primary_activate(ctx, false);

	return 0;
}

static int primary_resume(struct device *dev)
{
	struct primary_context *ctx = get_primary_context(dev);

	/*
	 * if entered to sleep when lcd panel was on, the usage_count
	 * of pm runtime would still be 1 so in this case, fimd driver
	 * should be on directly not drawing on pm runtime interface.
	 */
	if (!pm_runtime_suspended(dev)) {
		int ret;

		ret = primary_activate(ctx, true);
		if (ret < 0)
			return ret;

		/*
		 * in case of dpms on(standby), primary_apply function will
		 * be called by encoder's dpms callback to update fimd's
		 * registers but in case of sleep wakeup, it's not.
		 * so primary_apply function should be called at here.
		 */
		primary_apply(dev);
	}

	return 0;
}
#endif

#ifdef CONFIG_PM_RUNTIME
static int primary_runtime_suspend(struct device *dev)
{
	struct primary_context *ctx = get_primary_context(dev);

	DRM_DEBUG_KMS("%s\n", __FILE__);

	return primary_activate(ctx, false);
}

static int primary_runtime_resume(struct device *dev)
{
	struct primary_context *ctx = get_primary_context(dev);

	DRM_DEBUG_KMS("%s\n", __FILE__);

	return primary_activate(ctx, true);
}
#endif

static const struct dev_pm_ops primary_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(primary_suspend, primary_resume)
	SET_RUNTIME_PM_OPS(primary_runtime_suspend, primary_runtime_resume, NULL)
};

struct platform_driver primary_platform_driver = {
	.probe		= primary_probe,
	.remove		= primary_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "primary-display",
		.pm	= &primary_pm_ops,
	},
};
