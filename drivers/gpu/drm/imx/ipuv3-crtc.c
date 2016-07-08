/*
 * i.MX IPUv3 Graphics driver
 *
 * Copyright (C) 2011 Sascha Hauer, Pengutronix
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/component.h>
#include <linux/module.h>
#include <linux/export.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <linux/fb.h>
#include <linux/clk.h>
#include <linux/errno.h>
#include <linux/reservation.h>
#include <linux/dma-buf.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_fb_cma_helper.h>

#include <video/imx-ipu-v3.h>
#include "imx-drm.h"
#include "ipuv3-plane.h"

#define DRIVER_DESC		"i.MX IPUv3 Graphics"

enum ipu_flip_status {
	IPU_FLIP_NONE,
	IPU_FLIP_PENDING,
	IPU_FLIP_SUBMITTED,
};

struct ipu_flip_work {
	struct work_struct		unref_work;
	struct drm_gem_object		*bo;
	struct drm_pending_vblank_event *page_flip_event;
	struct work_struct		fence_work;
	struct ipu_crtc			*crtc;
	struct fence			*excl;
	unsigned			shared_count;
	struct fence			**shared;
};

struct ipu_crtc {
	struct device		*dev;
	struct drm_crtc		base;
	struct imx_drm_crtc	*imx_crtc;

	/* plane[0] is the full plane, plane[1] is the partial plane */
	struct ipu_plane	*plane[2];

	struct ipu_dc		*dc;
	struct ipu_di		*di;
	int			enabled;
	enum ipu_flip_status	flip_state;
	struct workqueue_struct *flip_queue;
	struct ipu_flip_work	*flip_work;
	int			irq;
	u32			bus_format;
	u32			bus_flags;
	int			di_hsync_pin;
	int			di_vsync_pin;
};

#define to_ipu_crtc(x) container_of(x, struct ipu_crtc, base)

static void ipu_fb_enable(struct ipu_crtc *ipu_crtc)
{
	struct ipu_soc *ipu = dev_get_drvdata(ipu_crtc->dev->parent);

	if (ipu_crtc->enabled)
		return;

	ipu_dc_enable(ipu);
	ipu_plane_enable(ipu_crtc->plane[0]);
	/* Start DC channel and DI after IDMAC */
	ipu_dc_enable_channel(ipu_crtc->dc);
	ipu_di_enable(ipu_crtc->di);
	drm_crtc_vblank_on(&ipu_crtc->base);

	ipu_crtc->enabled = 1;
}

static void ipu_fb_disable(struct ipu_crtc *ipu_crtc)
{
	struct ipu_soc *ipu = dev_get_drvdata(ipu_crtc->dev->parent);

	if (!ipu_crtc->enabled)
		return;

	/* Stop DC channel and DI before IDMAC */
	ipu_dc_disable_channel(ipu_crtc->dc);
	ipu_di_disable(ipu_crtc->di);
	ipu_plane_disable(ipu_crtc->plane[0]);
	ipu_dc_disable(ipu);
	drm_crtc_vblank_off(&ipu_crtc->base);

	ipu_crtc->enabled = 0;
}

static void ipu_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct ipu_crtc *ipu_crtc = to_ipu_crtc(crtc);

	dev_dbg(ipu_crtc->dev, "%s mode: %d\n", __func__, mode);

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		ipu_fb_enable(ipu_crtc);
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		ipu_fb_disable(ipu_crtc);
		break;
	}
}

static void ipu_flip_unref_work_func(struct work_struct *__work)
{
	struct ipu_flip_work *work =
			container_of(__work, struct ipu_flip_work, unref_work);

	drm_gem_object_unreference_unlocked(work->bo);
	kfree(work);
}

static void ipu_flip_fence_work_func(struct work_struct *__work)
{
	struct ipu_flip_work *work =
			container_of(__work, struct ipu_flip_work, fence_work);
	int i;

	/* wait for all fences attached to the FB obj to signal */
	if (work->excl) {
		fence_wait(work->excl, false);
		fence_put(work->excl);
	}
	for (i = 0; i < work->shared_count; i++) {
		fence_wait(work->shared[i], false);
		fence_put(work->shared[i]);
	}

	work->crtc->flip_state = IPU_FLIP_SUBMITTED;
}

static int ipu_page_flip(struct drm_crtc *crtc,
		struct drm_framebuffer *fb,
		struct drm_pending_vblank_event *event,
		uint32_t page_flip_flags)
{
	struct drm_gem_cma_object *cma_obj = drm_fb_cma_get_gem_obj(fb, 0);
	struct ipu_crtc *ipu_crtc = to_ipu_crtc(crtc);
	struct ipu_flip_work *flip_work;
	int ret;

	if (ipu_crtc->flip_state != IPU_FLIP_NONE)
		return -EBUSY;

	ret = imx_drm_crtc_vblank_get(ipu_crtc->imx_crtc);
	if (ret) {
		dev_dbg(ipu_crtc->dev, "failed to acquire vblank counter\n");
		list_del(&event->base.link);

		return ret;
	}

	flip_work = kzalloc(sizeof *flip_work, GFP_KERNEL);
	if (!flip_work) {
		ret = -ENOMEM;
		goto put_vblank;
	}
	INIT_WORK(&flip_work->unref_work, ipu_flip_unref_work_func);
	flip_work->page_flip_event = event;

	/* get BO backing the old framebuffer and take a reference */
	flip_work->bo = &drm_fb_cma_get_gem_obj(crtc->primary->fb, 0)->base;
	drm_gem_object_reference(flip_work->bo);

	ipu_crtc->flip_work = flip_work;
	/*
	 * If the object has a DMABUF attached, we need to wait on its fences
	 * if there are any.
	 */
	if (cma_obj->base.dma_buf) {
		INIT_WORK(&flip_work->fence_work, ipu_flip_fence_work_func);
		flip_work->crtc = ipu_crtc;

		ret = reservation_object_get_fences_rcu(
				cma_obj->base.dma_buf->resv, &flip_work->excl,
				&flip_work->shared_count, &flip_work->shared);

		if (unlikely(ret)) {
			DRM_ERROR("failed to get fences for buffer\n");
			goto free_flip_work;
		}

		/* No need to queue the worker if the are no fences */
		if (!flip_work->excl && !flip_work->shared_count) {
			ipu_crtc->flip_state = IPU_FLIP_SUBMITTED;
		} else {
			ipu_crtc->flip_state = IPU_FLIP_PENDING;
			queue_work(ipu_crtc->flip_queue,
				   &flip_work->fence_work);
		}
	} else {
		ipu_crtc->flip_state = IPU_FLIP_SUBMITTED;
	}

	return 0;

free_flip_work:
	drm_gem_object_unreference_unlocked(flip_work->bo);
	kfree(flip_work);
	ipu_crtc->flip_work = NULL;
put_vblank:
	imx_drm_crtc_vblank_put(ipu_crtc->imx_crtc);

	return ret;
}

static const struct drm_crtc_funcs ipu_crtc_funcs = {
	.set_config = drm_crtc_helper_set_config,
	.destroy = drm_crtc_cleanup,
	.page_flip = ipu_page_flip,
};

static int ipu_crtc_mode_set(struct drm_crtc *crtc,
			       struct drm_display_mode *orig_mode,
			       struct drm_display_mode *mode,
			       int x, int y,
			       struct drm_framebuffer *old_fb)
{
	struct drm_device *dev = crtc->dev;
	struct drm_encoder *encoder;
	struct ipu_crtc *ipu_crtc = to_ipu_crtc(crtc);
	struct ipu_di_signal_cfg sig_cfg = {};
	unsigned long encoder_types = 0;
	int ret;

	dev_dbg(ipu_crtc->dev, "%s: mode->hdisplay: %d\n", __func__,
			mode->hdisplay);
	dev_dbg(ipu_crtc->dev, "%s: mode->vdisplay: %d\n", __func__,
			mode->vdisplay);

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head)
		if (encoder->crtc == crtc)
			encoder_types |= BIT(encoder->encoder_type);

	dev_dbg(ipu_crtc->dev, "%s: attached to encoder types 0x%lx\n",
		__func__, encoder_types);

	/*
	 * If we have DAC or LDB, then we need the IPU DI clock to be
	 * the same as the LDB DI clock. For TVDAC, derive the IPU DI
	 * clock from 27 MHz TVE_DI clock, but allow to divide it.
	 */
	if (encoder_types & (BIT(DRM_MODE_ENCODER_DAC) |
			     BIT(DRM_MODE_ENCODER_LVDS)))
		sig_cfg.clkflags = IPU_DI_CLKMODE_SYNC | IPU_DI_CLKMODE_EXT;
	else if (encoder_types & BIT(DRM_MODE_ENCODER_TVDAC))
		sig_cfg.clkflags = IPU_DI_CLKMODE_EXT;
	else
		sig_cfg.clkflags = 0;

	sig_cfg.enable_pol = !(ipu_crtc->bus_flags & DRM_BUS_FLAG_DE_LOW);
	/* Default to driving pixel data on negative clock edges */
	sig_cfg.clk_pol = !!(ipu_crtc->bus_flags &
			     DRM_BUS_FLAG_PIXDATA_POSEDGE);
	sig_cfg.bus_format = ipu_crtc->bus_format;
	sig_cfg.v_to_h_sync = 0;
	sig_cfg.hsync_pin = ipu_crtc->di_hsync_pin;
	sig_cfg.vsync_pin = ipu_crtc->di_vsync_pin;

	drm_display_mode_to_videomode(mode, &sig_cfg.mode);

	ret = ipu_dc_init_sync(ipu_crtc->dc, ipu_crtc->di,
			       mode->flags & DRM_MODE_FLAG_INTERLACE,
			       ipu_crtc->bus_format, mode->hdisplay);
	if (ret) {
		dev_err(ipu_crtc->dev,
				"initializing display controller failed with %d\n",
				ret);
		return ret;
	}

	ret = ipu_di_init_sync_panel(ipu_crtc->di, &sig_cfg);
	if (ret) {
		dev_err(ipu_crtc->dev,
				"initializing panel failed with %d\n", ret);
		return ret;
	}

	return ipu_plane_mode_set(ipu_crtc->plane[0], crtc, mode,
				  crtc->primary->fb,
				  0, 0, mode->hdisplay, mode->vdisplay,
				  x, y, mode->hdisplay, mode->vdisplay,
				  mode->flags & DRM_MODE_FLAG_INTERLACE);
}

static void ipu_crtc_handle_pageflip(struct ipu_crtc *ipu_crtc)
{
	unsigned long flags;
	struct drm_device *drm = ipu_crtc->base.dev;
	struct ipu_flip_work *work = ipu_crtc->flip_work;

	spin_lock_irqsave(&drm->event_lock, flags);
	if (work->page_flip_event)
		drm_crtc_send_vblank_event(&ipu_crtc->base,
					   work->page_flip_event);
	imx_drm_crtc_vblank_put(ipu_crtc->imx_crtc);
	spin_unlock_irqrestore(&drm->event_lock, flags);
}

static irqreturn_t ipu_irq_handler(int irq, void *dev_id)
{
	struct ipu_crtc *ipu_crtc = dev_id;

	imx_drm_handle_vblank(ipu_crtc->imx_crtc);

	if (ipu_crtc->flip_state == IPU_FLIP_SUBMITTED) {
		struct ipu_plane *plane = ipu_crtc->plane[0];

		ipu_plane_set_base(plane, ipu_crtc->base.primary->fb,
				   plane->x, plane->y);
		ipu_crtc_handle_pageflip(ipu_crtc);
		queue_work(ipu_crtc->flip_queue,
			   &ipu_crtc->flip_work->unref_work);
		ipu_crtc->flip_state = IPU_FLIP_NONE;
	}

	return IRQ_HANDLED;
}

static bool ipu_crtc_mode_fixup(struct drm_crtc *crtc,
				  const struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	struct ipu_crtc *ipu_crtc = to_ipu_crtc(crtc);
	struct videomode vm;
	int ret;

	drm_display_mode_to_videomode(adjusted_mode, &vm);

	ret = ipu_di_adjust_videomode(ipu_crtc->di, &vm);
	if (ret)
		return false;

	drm_display_mode_from_videomode(&vm, adjusted_mode);

	return true;
}

static void ipu_crtc_prepare(struct drm_crtc *crtc)
{
	struct ipu_crtc *ipu_crtc = to_ipu_crtc(crtc);

	ipu_fb_disable(ipu_crtc);
}

static void ipu_crtc_commit(struct drm_crtc *crtc)
{
	struct ipu_crtc *ipu_crtc = to_ipu_crtc(crtc);

	ipu_fb_enable(ipu_crtc);
}

static const struct drm_crtc_helper_funcs ipu_helper_funcs = {
	.dpms = ipu_crtc_dpms,
	.mode_fixup = ipu_crtc_mode_fixup,
	.mode_set = ipu_crtc_mode_set,
	.prepare = ipu_crtc_prepare,
	.commit = ipu_crtc_commit,
};

static int ipu_enable_vblank(struct drm_crtc *crtc)
{
	struct ipu_crtc *ipu_crtc = to_ipu_crtc(crtc);

	enable_irq(ipu_crtc->irq);

	return 0;
}

static void ipu_disable_vblank(struct drm_crtc *crtc)
{
	struct ipu_crtc *ipu_crtc = to_ipu_crtc(crtc);

	disable_irq_nosync(ipu_crtc->irq);
}

static int ipu_set_interface_pix_fmt(struct drm_crtc *crtc,
		u32 bus_format, int hsync_pin, int vsync_pin, u32 bus_flags)
{
	struct ipu_crtc *ipu_crtc = to_ipu_crtc(crtc);

	ipu_crtc->bus_format = bus_format;
	ipu_crtc->bus_flags = bus_flags;
	ipu_crtc->di_hsync_pin = hsync_pin;
	ipu_crtc->di_vsync_pin = vsync_pin;

	return 0;
}

static const struct imx_drm_crtc_helper_funcs ipu_crtc_helper_funcs = {
	.enable_vblank = ipu_enable_vblank,
	.disable_vblank = ipu_disable_vblank,
	.set_interface_pix_fmt = ipu_set_interface_pix_fmt,
	.crtc_funcs = &ipu_crtc_funcs,
	.crtc_helper_funcs = &ipu_helper_funcs,
};

static void ipu_put_resources(struct ipu_crtc *ipu_crtc)
{
	if (!IS_ERR_OR_NULL(ipu_crtc->dc))
		ipu_dc_put(ipu_crtc->dc);
	if (!IS_ERR_OR_NULL(ipu_crtc->di))
		ipu_di_put(ipu_crtc->di);
}

static int ipu_get_resources(struct ipu_crtc *ipu_crtc,
		struct ipu_client_platformdata *pdata)
{
	struct ipu_soc *ipu = dev_get_drvdata(ipu_crtc->dev->parent);
	int ret;

	ipu_crtc->dc = ipu_dc_get(ipu, pdata->dc);
	if (IS_ERR(ipu_crtc->dc)) {
		ret = PTR_ERR(ipu_crtc->dc);
		goto err_out;
	}

	ipu_crtc->di = ipu_di_get(ipu, pdata->di);
	if (IS_ERR(ipu_crtc->di)) {
		ret = PTR_ERR(ipu_crtc->di);
		goto err_out;
	}

	return 0;
err_out:
	ipu_put_resources(ipu_crtc);

	return ret;
}

static int ipu_crtc_init(struct ipu_crtc *ipu_crtc,
	struct ipu_client_platformdata *pdata, struct drm_device *drm)
{
	struct ipu_soc *ipu = dev_get_drvdata(ipu_crtc->dev->parent);
	int dp = -EINVAL;
	int ret;

	ret = ipu_get_resources(ipu_crtc, pdata);
	if (ret) {
		dev_err(ipu_crtc->dev, "getting resources failed with %d.\n",
				ret);
		return ret;
	}

	if (pdata->dp >= 0)
		dp = IPU_DP_FLOW_SYNC_BG;
	ipu_crtc->plane[0] = ipu_plane_init(drm, ipu, pdata->dma[0], dp, 0,
					    DRM_PLANE_TYPE_PRIMARY);
	if (IS_ERR(ipu_crtc->plane[0])) {
		ret = PTR_ERR(ipu_crtc->plane[0]);
		goto err_put_resources;
	}

	ret = imx_drm_add_crtc(drm, &ipu_crtc->base, &ipu_crtc->imx_crtc,
			&ipu_crtc->plane[0]->base, &ipu_crtc_helper_funcs,
			pdata->of_node);
	if (ret) {
		dev_err(ipu_crtc->dev, "adding crtc failed with %d.\n", ret);
		goto err_put_resources;
	}

	ret = ipu_plane_get_resources(ipu_crtc->plane[0]);
	if (ret) {
		dev_err(ipu_crtc->dev, "getting plane 0 resources failed with %d.\n",
			ret);
		goto err_remove_crtc;
	}

	/* If this crtc is using the DP, add an overlay plane */
	if (pdata->dp >= 0 && pdata->dma[1] > 0) {
		ipu_crtc->plane[1] = ipu_plane_init(drm, ipu, pdata->dma[1],
						IPU_DP_FLOW_SYNC_FG,
						drm_crtc_mask(&ipu_crtc->base),
						DRM_PLANE_TYPE_OVERLAY);
		if (IS_ERR(ipu_crtc->plane[1]))
			ipu_crtc->plane[1] = NULL;
	}

	ipu_crtc->irq = ipu_plane_irq(ipu_crtc->plane[0]);
	ret = devm_request_irq(ipu_crtc->dev, ipu_crtc->irq, ipu_irq_handler, 0,
			"imx_drm", ipu_crtc);
	if (ret < 0) {
		dev_err(ipu_crtc->dev, "irq request failed with %d.\n", ret);
		goto err_put_plane_res;
	}
	/* Only enable IRQ when we actually need it to trigger work. */
	disable_irq(ipu_crtc->irq);

	ipu_crtc->flip_queue = create_singlethread_workqueue("ipu-crtc-flip");

	return 0;

err_put_plane_res:
	ipu_plane_put_resources(ipu_crtc->plane[0]);
err_remove_crtc:
	imx_drm_remove_crtc(ipu_crtc->imx_crtc);
err_put_resources:
	ipu_put_resources(ipu_crtc);

	return ret;
}

static int ipu_drm_bind(struct device *dev, struct device *master, void *data)
{
	struct ipu_client_platformdata *pdata = dev->platform_data;
	struct drm_device *drm = data;
	struct ipu_crtc *ipu_crtc;
	int ret;

	ipu_crtc = devm_kzalloc(dev, sizeof(*ipu_crtc), GFP_KERNEL);
	if (!ipu_crtc)
		return -ENOMEM;

	ipu_crtc->dev = dev;

	ret = ipu_crtc_init(ipu_crtc, pdata, drm);
	if (ret)
		return ret;

	dev_set_drvdata(dev, ipu_crtc);

	return 0;
}

static void ipu_drm_unbind(struct device *dev, struct device *master,
	void *data)
{
	struct ipu_crtc *ipu_crtc = dev_get_drvdata(dev);

	imx_drm_remove_crtc(ipu_crtc->imx_crtc);

	destroy_workqueue(ipu_crtc->flip_queue);
	ipu_plane_put_resources(ipu_crtc->plane[0]);
	ipu_put_resources(ipu_crtc);
}

static const struct component_ops ipu_crtc_ops = {
	.bind = ipu_drm_bind,
	.unbind = ipu_drm_unbind,
};

static int ipu_drm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;

	if (!dev->platform_data)
		return -EINVAL;

	ret = dma_set_coherent_mask(dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	return component_add(dev, &ipu_crtc_ops);
}

static int ipu_drm_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &ipu_crtc_ops);
	return 0;
}

static struct platform_driver ipu_drm_driver = {
	.driver = {
		.name = "imx-ipuv3-crtc",
	},
	.probe = ipu_drm_probe,
	.remove = ipu_drm_remove,
};
module_platform_driver(ipu_drm_driver);

MODULE_AUTHOR("Sascha Hauer <s.hauer@pengutronix.de>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:imx-ipuv3-crtc");
