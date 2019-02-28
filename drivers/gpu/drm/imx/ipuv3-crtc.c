// SPDX-License-Identifier: GPL-2.0+
/*
 * i.MX IPUv3 Graphics driver
 *
 * Copyright (C) 2011 Sascha Hauer, Pengutronix
 */
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_probe_helper.h>

#include <video/imx-ipu-v3.h>
#include "imx-drm.h"
#include "ipuv3-plane.h"

#define DRIVER_DESC		"i.MX IPUv3 Graphics"

struct ipu_crtc {
	struct device		*dev;
	struct drm_crtc		base;

	/* plane[0] is the full plane, plane[1] is the partial plane */
	struct ipu_plane	*plane[2];

	struct ipu_dc		*dc;
	struct ipu_di		*di;
	int			irq;
	struct drm_pending_vblank_event *event;
};

static inline struct ipu_crtc *to_ipu_crtc(struct drm_crtc *crtc)
{
	return container_of(crtc, struct ipu_crtc, base);
}

static void ipu_crtc_atomic_enable(struct drm_crtc *crtc,
				   struct drm_crtc_state *old_state)
{
	struct ipu_crtc *ipu_crtc = to_ipu_crtc(crtc);
	struct ipu_soc *ipu = dev_get_drvdata(ipu_crtc->dev->parent);

	ipu_prg_enable(ipu);
	ipu_dc_enable(ipu);
	ipu_dc_enable_channel(ipu_crtc->dc);
	ipu_di_enable(ipu_crtc->di);
}

static void ipu_crtc_disable_planes(struct ipu_crtc *ipu_crtc,
				    struct drm_crtc_state *old_crtc_state)
{
	bool disable_partial = false;
	bool disable_full = false;
	struct drm_plane *plane;

	drm_atomic_crtc_state_for_each_plane(plane, old_crtc_state) {
		if (plane == &ipu_crtc->plane[0]->base)
			disable_full = true;
		if (&ipu_crtc->plane[1] && plane == &ipu_crtc->plane[1]->base)
			disable_partial = true;
	}

	if (disable_partial)
		ipu_plane_disable(ipu_crtc->plane[1], true);
	if (disable_full)
		ipu_plane_disable(ipu_crtc->plane[0], false);
}

static void ipu_crtc_atomic_disable(struct drm_crtc *crtc,
				    struct drm_crtc_state *old_crtc_state)
{
	struct ipu_crtc *ipu_crtc = to_ipu_crtc(crtc);
	struct ipu_soc *ipu = dev_get_drvdata(ipu_crtc->dev->parent);

	ipu_dc_disable_channel(ipu_crtc->dc);
	ipu_di_disable(ipu_crtc->di);
	/*
	 * Planes must be disabled before DC clock is removed, as otherwise the
	 * attached IDMACs will be left in undefined state, possibly hanging
	 * the IPU or even system.
	 */
	ipu_crtc_disable_planes(ipu_crtc, old_crtc_state);
	ipu_dc_disable(ipu);
	ipu_prg_disable(ipu);

	spin_lock_irq(&crtc->dev->event_lock);
	if (crtc->state->event) {
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
	}
	spin_unlock_irq(&crtc->dev->event_lock);

	drm_crtc_vblank_off(crtc);
}

static void imx_drm_crtc_reset(struct drm_crtc *crtc)
{
	struct imx_crtc_state *state;

	if (crtc->state) {
		if (crtc->state->mode_blob)
			drm_property_blob_put(crtc->state->mode_blob);

		state = to_imx_crtc_state(crtc->state);
		memset(state, 0, sizeof(*state));
	} else {
		state = kzalloc(sizeof(*state), GFP_KERNEL);
		if (!state)
			return;
		crtc->state = &state->base;
	}

	state->base.crtc = crtc;
}

static struct drm_crtc_state *imx_drm_crtc_duplicate_state(struct drm_crtc *crtc)
{
	struct imx_crtc_state *state;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	__drm_atomic_helper_crtc_duplicate_state(crtc, &state->base);

	WARN_ON(state->base.crtc != crtc);
	state->base.crtc = crtc;

	return &state->base;
}

static void imx_drm_crtc_destroy_state(struct drm_crtc *crtc,
				       struct drm_crtc_state *state)
{
	__drm_atomic_helper_crtc_destroy_state(state);
	kfree(to_imx_crtc_state(state));
}

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

static const struct drm_crtc_funcs ipu_crtc_funcs = {
	.set_config = drm_atomic_helper_set_config,
	.destroy = drm_crtc_cleanup,
	.page_flip = drm_atomic_helper_page_flip,
	.reset = imx_drm_crtc_reset,
	.atomic_duplicate_state = imx_drm_crtc_duplicate_state,
	.atomic_destroy_state = imx_drm_crtc_destroy_state,
	.enable_vblank = ipu_enable_vblank,
	.disable_vblank = ipu_disable_vblank,
};

static irqreturn_t ipu_irq_handler(int irq, void *dev_id)
{
	struct ipu_crtc *ipu_crtc = dev_id;
	struct drm_crtc *crtc = &ipu_crtc->base;
	unsigned long flags;
	int i;

	drm_crtc_handle_vblank(crtc);

	if (ipu_crtc->event) {
		for (i = 0; i < ARRAY_SIZE(ipu_crtc->plane); i++) {
			struct ipu_plane *plane = ipu_crtc->plane[i];

			if (!plane)
				continue;

			if (ipu_plane_atomic_update_pending(&plane->base))
				break;
		}

		if (i == ARRAY_SIZE(ipu_crtc->plane)) {
			spin_lock_irqsave(&crtc->dev->event_lock, flags);
			drm_crtc_send_vblank_event(crtc, ipu_crtc->event);
			ipu_crtc->event = NULL;
			drm_crtc_vblank_put(crtc);
			spin_unlock_irqrestore(&crtc->dev->event_lock, flags);
		}
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

	if ((vm.vsync_len == 0) || (vm.hsync_len == 0))
		return false;

	drm_display_mode_from_videomode(&vm, adjusted_mode);

	return true;
}

static int ipu_crtc_atomic_check(struct drm_crtc *crtc,
				 struct drm_crtc_state *state)
{
	u32 primary_plane_mask = drm_plane_mask(crtc->primary);

	if (state->active && (primary_plane_mask & state->plane_mask) == 0)
		return -EINVAL;

	return 0;
}

static void ipu_crtc_atomic_begin(struct drm_crtc *crtc,
				  struct drm_crtc_state *old_crtc_state)
{
	drm_crtc_vblank_on(crtc);
}

static void ipu_crtc_atomic_flush(struct drm_crtc *crtc,
				  struct drm_crtc_state *old_crtc_state)
{
	spin_lock_irq(&crtc->dev->event_lock);
	if (crtc->state->event) {
		struct ipu_crtc *ipu_crtc = to_ipu_crtc(crtc);

		WARN_ON(drm_crtc_vblank_get(crtc));
		ipu_crtc->event = crtc->state->event;
		crtc->state->event = NULL;
	}
	spin_unlock_irq(&crtc->dev->event_lock);
}

static void ipu_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_encoder *encoder;
	struct ipu_crtc *ipu_crtc = to_ipu_crtc(crtc);
	struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	struct imx_crtc_state *imx_crtc_state = to_imx_crtc_state(crtc->state);
	struct ipu_di_signal_cfg sig_cfg = {};
	unsigned long encoder_types = 0;

	dev_dbg(ipu_crtc->dev, "%s: mode->hdisplay: %d\n", __func__,
			mode->hdisplay);
	dev_dbg(ipu_crtc->dev, "%s: mode->vdisplay: %d\n", __func__,
			mode->vdisplay);

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		if (encoder->crtc == crtc)
			encoder_types |= BIT(encoder->encoder_type);
	}

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

	sig_cfg.enable_pol = !(imx_crtc_state->bus_flags & DRM_BUS_FLAG_DE_LOW);
	/* Default to driving pixel data on negative clock edges */
	sig_cfg.clk_pol = !!(imx_crtc_state->bus_flags &
			     DRM_BUS_FLAG_PIXDATA_POSEDGE);
	sig_cfg.bus_format = imx_crtc_state->bus_format;
	sig_cfg.v_to_h_sync = 0;
	sig_cfg.hsync_pin = imx_crtc_state->di_hsync_pin;
	sig_cfg.vsync_pin = imx_crtc_state->di_vsync_pin;

	drm_display_mode_to_videomode(mode, &sig_cfg.mode);

	ipu_dc_init_sync(ipu_crtc->dc, ipu_crtc->di,
			 mode->flags & DRM_MODE_FLAG_INTERLACE,
			 imx_crtc_state->bus_format, mode->hdisplay);
	ipu_di_init_sync_panel(ipu_crtc->di, &sig_cfg);
}

static const struct drm_crtc_helper_funcs ipu_helper_funcs = {
	.mode_fixup = ipu_crtc_mode_fixup,
	.mode_set_nofb = ipu_crtc_mode_set_nofb,
	.atomic_check = ipu_crtc_atomic_check,
	.atomic_begin = ipu_crtc_atomic_begin,
	.atomic_flush = ipu_crtc_atomic_flush,
	.atomic_disable = ipu_crtc_atomic_disable,
	.atomic_enable = ipu_crtc_atomic_enable,
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
	struct drm_crtc *crtc = &ipu_crtc->base;
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

	crtc->port = pdata->of_node;
	drm_crtc_helper_add(crtc, &ipu_helper_funcs);
	drm_crtc_init_with_planes(drm, crtc, &ipu_crtc->plane[0]->base, NULL,
				  &ipu_crtc_funcs, NULL);

	ret = ipu_plane_get_resources(ipu_crtc->plane[0]);
	if (ret) {
		dev_err(ipu_crtc->dev, "getting plane 0 resources failed with %d.\n",
			ret);
		goto err_put_resources;
	}

	/* If this crtc is using the DP, add an overlay plane */
	if (pdata->dp >= 0 && pdata->dma[1] > 0) {
		ipu_crtc->plane[1] = ipu_plane_init(drm, ipu, pdata->dma[1],
						IPU_DP_FLOW_SYNC_FG,
						drm_crtc_mask(&ipu_crtc->base),
						DRM_PLANE_TYPE_OVERLAY);
		if (IS_ERR(ipu_crtc->plane[1])) {
			ipu_crtc->plane[1] = NULL;
		} else {
			ret = ipu_plane_get_resources(ipu_crtc->plane[1]);
			if (ret) {
				dev_err(ipu_crtc->dev, "getting plane 1 "
					"resources failed with %d.\n", ret);
				goto err_put_plane0_res;
			}
		}
	}

	ipu_crtc->irq = ipu_plane_irq(ipu_crtc->plane[0]);
	ret = devm_request_irq(ipu_crtc->dev, ipu_crtc->irq, ipu_irq_handler, 0,
			"imx_drm", ipu_crtc);
	if (ret < 0) {
		dev_err(ipu_crtc->dev, "irq request failed with %d.\n", ret);
		goto err_put_plane1_res;
	}
	/* Only enable IRQ when we actually need it to trigger work. */
	disable_irq(ipu_crtc->irq);

	return 0;

err_put_plane1_res:
	if (ipu_crtc->plane[1])
		ipu_plane_put_resources(ipu_crtc->plane[1]);
err_put_plane0_res:
	ipu_plane_put_resources(ipu_crtc->plane[0]);
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

	ipu_put_resources(ipu_crtc);
	if (ipu_crtc->plane[1])
		ipu_plane_put_resources(ipu_crtc->plane[1]);
	ipu_plane_put_resources(ipu_crtc->plane[0]);
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

struct platform_driver ipu_drm_driver = {
	.driver = {
		.name = "imx-ipuv3-crtc",
	},
	.probe = ipu_drm_probe,
	.remove = ipu_drm_remove,
};
