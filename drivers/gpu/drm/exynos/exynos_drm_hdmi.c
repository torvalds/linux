/*
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 * Authors:
 *	Inki Dae <inki.dae@samsung.com>
 *	Seung-Woo Kim <sw0312.kim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <drm/drmP.h>

#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <drm/exynos_drm.h>

#include "exynos_drm_drv.h"
#include "exynos_drm_hdmi.h"

#define to_context(dev)		platform_get_drvdata(to_platform_device(dev))
#define to_subdrv(dev)		to_context(dev)
#define get_ctx_from_subdrv(subdrv)	container_of(subdrv,\
					struct drm_hdmi_context, subdrv);

/* platform device pointer for common drm hdmi device. */
static struct platform_device *exynos_drm_hdmi_pdev;

/* Common hdmi subdrv needs to access the hdmi and mixer though context.
* These should be initialied by the repective drivers */
static struct exynos_drm_hdmi_context *hdmi_ctx;
static struct exynos_drm_hdmi_context *mixer_ctx;

/* these callback points shoud be set by specific drivers. */
static struct exynos_hdmi_ops *hdmi_ops;
static struct exynos_mixer_ops *mixer_ops;

struct drm_hdmi_context {
	struct exynos_drm_subdrv	subdrv;
	struct exynos_drm_hdmi_context	*hdmi_ctx;
	struct exynos_drm_hdmi_context	*mixer_ctx;

	bool	enabled[MIXER_WIN_NR];
};

int exynos_platform_device_hdmi_register(void)
{
	struct platform_device *pdev;

	if (exynos_drm_hdmi_pdev)
		return -EEXIST;

	pdev = platform_device_register_simple(
			"exynos-drm-hdmi", -1, NULL, 0);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	exynos_drm_hdmi_pdev = pdev;

	return 0;
}

void exynos_platform_device_hdmi_unregister(void)
{
	if (exynos_drm_hdmi_pdev) {
		platform_device_unregister(exynos_drm_hdmi_pdev);
		exynos_drm_hdmi_pdev = NULL;
	}
}

void exynos_hdmi_drv_attach(struct exynos_drm_hdmi_context *ctx)
{
	if (ctx)
		hdmi_ctx = ctx;
}

void exynos_mixer_drv_attach(struct exynos_drm_hdmi_context *ctx)
{
	if (ctx)
		mixer_ctx = ctx;
}

void exynos_hdmi_ops_register(struct exynos_hdmi_ops *ops)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (ops)
		hdmi_ops = ops;
}

void exynos_mixer_ops_register(struct exynos_mixer_ops *ops)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (ops)
		mixer_ops = ops;
}

static bool drm_hdmi_is_connected(struct device *dev)
{
	struct drm_hdmi_context *ctx = to_context(dev);

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (hdmi_ops && hdmi_ops->is_connected)
		return hdmi_ops->is_connected(ctx->hdmi_ctx->ctx);

	return false;
}

static struct edid *drm_hdmi_get_edid(struct device *dev,
			struct drm_connector *connector)
{
	struct drm_hdmi_context *ctx = to_context(dev);

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (hdmi_ops && hdmi_ops->get_edid)
		return hdmi_ops->get_edid(ctx->hdmi_ctx->ctx, connector);

	return NULL;
}

static int drm_hdmi_check_timing(struct device *dev, void *timing)
{
	struct drm_hdmi_context *ctx = to_context(dev);
	int ret = 0;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	/*
	* Both, mixer and hdmi should be able to handle the requested mode.
	* If any of the two fails, return mode as BAD.
	*/

	if (mixer_ops && mixer_ops->check_timing)
		ret = mixer_ops->check_timing(ctx->mixer_ctx->ctx, timing);

	if (ret)
		return ret;

	if (hdmi_ops && hdmi_ops->check_timing)
		return hdmi_ops->check_timing(ctx->hdmi_ctx->ctx, timing);

	return 0;
}

static int drm_hdmi_power_on(struct device *dev, int mode)
{
	struct drm_hdmi_context *ctx = to_context(dev);

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (hdmi_ops && hdmi_ops->power_on)
		return hdmi_ops->power_on(ctx->hdmi_ctx->ctx, mode);

	return 0;
}

static struct exynos_drm_display_ops drm_hdmi_display_ops = {
	.type = EXYNOS_DISPLAY_TYPE_HDMI,
	.is_connected = drm_hdmi_is_connected,
	.get_edid = drm_hdmi_get_edid,
	.check_timing = drm_hdmi_check_timing,
	.power_on = drm_hdmi_power_on,
};

static int drm_hdmi_enable_vblank(struct device *subdrv_dev)
{
	struct drm_hdmi_context *ctx = to_context(subdrv_dev);
	struct exynos_drm_subdrv *subdrv = &ctx->subdrv;
	struct exynos_drm_manager *manager = subdrv->manager;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (mixer_ops && mixer_ops->enable_vblank)
		return mixer_ops->enable_vblank(ctx->mixer_ctx->ctx,
						manager->pipe);

	return 0;
}

static void drm_hdmi_disable_vblank(struct device *subdrv_dev)
{
	struct drm_hdmi_context *ctx = to_context(subdrv_dev);

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (mixer_ops && mixer_ops->disable_vblank)
		return mixer_ops->disable_vblank(ctx->mixer_ctx->ctx);
}

static void drm_hdmi_wait_for_vblank(struct device *subdrv_dev)
{
	struct drm_hdmi_context *ctx = to_context(subdrv_dev);

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (mixer_ops && mixer_ops->wait_for_vblank)
		mixer_ops->wait_for_vblank(ctx->mixer_ctx->ctx);
}

static void drm_hdmi_mode_fixup(struct device *subdrv_dev,
				struct drm_connector *connector,
				const struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	struct drm_display_mode *m;
	int mode_ok;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	drm_mode_set_crtcinfo(adjusted_mode, 0);

	mode_ok = drm_hdmi_check_timing(subdrv_dev, adjusted_mode);

	/* just return if user desired mode exists. */
	if (mode_ok == 0)
		return;

	/*
	 * otherwise, find the most suitable mode among modes and change it
	 * to adjusted_mode.
	 */
	list_for_each_entry(m, &connector->modes, head) {
		mode_ok = drm_hdmi_check_timing(subdrv_dev, m);

		if (mode_ok == 0) {
			struct drm_mode_object base;
			struct list_head head;

			DRM_INFO("desired mode doesn't exist so\n");
			DRM_INFO("use the most suitable mode among modes.\n");

			DRM_DEBUG_KMS("Adjusted Mode: [%d]x[%d] [%d]Hz\n",
				m->hdisplay, m->vdisplay, m->vrefresh);

			/* preserve display mode header while copying. */
			head = adjusted_mode->head;
			base = adjusted_mode->base;
			memcpy(adjusted_mode, m, sizeof(*m));
			adjusted_mode->head = head;
			adjusted_mode->base = base;
			break;
		}
	}
}

static void drm_hdmi_mode_set(struct device *subdrv_dev, void *mode)
{
	struct drm_hdmi_context *ctx = to_context(subdrv_dev);

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (hdmi_ops && hdmi_ops->mode_set)
		hdmi_ops->mode_set(ctx->hdmi_ctx->ctx, mode);
}

static void drm_hdmi_get_max_resol(struct device *subdrv_dev,
				unsigned int *width, unsigned int *height)
{
	struct drm_hdmi_context *ctx = to_context(subdrv_dev);

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (hdmi_ops && hdmi_ops->get_max_resol)
		hdmi_ops->get_max_resol(ctx->hdmi_ctx->ctx, width, height);
}

static void drm_hdmi_commit(struct device *subdrv_dev)
{
	struct drm_hdmi_context *ctx = to_context(subdrv_dev);

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (hdmi_ops && hdmi_ops->commit)
		hdmi_ops->commit(ctx->hdmi_ctx->ctx);
}

static void drm_hdmi_dpms(struct device *subdrv_dev, int mode)
{
	struct drm_hdmi_context *ctx = to_context(subdrv_dev);

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (mixer_ops && mixer_ops->dpms)
		mixer_ops->dpms(ctx->mixer_ctx->ctx, mode);

	if (hdmi_ops && hdmi_ops->dpms)
		hdmi_ops->dpms(ctx->hdmi_ctx->ctx, mode);
}

static void drm_hdmi_apply(struct device *subdrv_dev)
{
	struct drm_hdmi_context *ctx = to_context(subdrv_dev);
	int i;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	for (i = 0; i < MIXER_WIN_NR; i++) {
		if (!ctx->enabled[i])
			continue;
		if (mixer_ops && mixer_ops->win_commit)
			mixer_ops->win_commit(ctx->mixer_ctx->ctx, i);
	}

	if (hdmi_ops && hdmi_ops->commit)
		hdmi_ops->commit(ctx->hdmi_ctx->ctx);
}

static struct exynos_drm_manager_ops drm_hdmi_manager_ops = {
	.dpms = drm_hdmi_dpms,
	.apply = drm_hdmi_apply,
	.enable_vblank = drm_hdmi_enable_vblank,
	.disable_vblank = drm_hdmi_disable_vblank,
	.wait_for_vblank = drm_hdmi_wait_for_vblank,
	.mode_fixup = drm_hdmi_mode_fixup,
	.mode_set = drm_hdmi_mode_set,
	.get_max_resol = drm_hdmi_get_max_resol,
	.commit = drm_hdmi_commit,
};

static void drm_mixer_mode_set(struct device *subdrv_dev,
		struct exynos_drm_overlay *overlay)
{
	struct drm_hdmi_context *ctx = to_context(subdrv_dev);

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (mixer_ops && mixer_ops->win_mode_set)
		mixer_ops->win_mode_set(ctx->mixer_ctx->ctx, overlay);
}

static void drm_mixer_commit(struct device *subdrv_dev, int zpos)
{
	struct drm_hdmi_context *ctx = to_context(subdrv_dev);
	int win = (zpos == DEFAULT_ZPOS) ? MIXER_DEFAULT_WIN : zpos;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (win < 0 || win >= MIXER_WIN_NR) {
		DRM_ERROR("mixer window[%d] is wrong\n", win);
		return;
	}

	if (mixer_ops && mixer_ops->win_commit)
		mixer_ops->win_commit(ctx->mixer_ctx->ctx, win);

	ctx->enabled[win] = true;
}

static void drm_mixer_disable(struct device *subdrv_dev, int zpos)
{
	struct drm_hdmi_context *ctx = to_context(subdrv_dev);
	int win = (zpos == DEFAULT_ZPOS) ? MIXER_DEFAULT_WIN : zpos;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (win < 0 || win >= MIXER_WIN_NR) {
		DRM_ERROR("mixer window[%d] is wrong\n", win);
		return;
	}

	if (mixer_ops && mixer_ops->win_disable)
		mixer_ops->win_disable(ctx->mixer_ctx->ctx, win);

	ctx->enabled[win] = false;
}

static struct exynos_drm_overlay_ops drm_hdmi_overlay_ops = {
	.mode_set = drm_mixer_mode_set,
	.commit = drm_mixer_commit,
	.disable = drm_mixer_disable,
};

static struct exynos_drm_manager hdmi_manager = {
	.pipe		= -1,
	.ops		= &drm_hdmi_manager_ops,
	.overlay_ops	= &drm_hdmi_overlay_ops,
	.display_ops	= &drm_hdmi_display_ops,
};

static int hdmi_subdrv_probe(struct drm_device *drm_dev,
		struct device *dev)
{
	struct exynos_drm_subdrv *subdrv = to_subdrv(dev);
	struct drm_hdmi_context *ctx;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (!hdmi_ctx) {
		DRM_ERROR("hdmi context not initialized.\n");
		return -EFAULT;
	}

	if (!mixer_ctx) {
		DRM_ERROR("mixer context not initialized.\n");
		return -EFAULT;
	}

	ctx = get_ctx_from_subdrv(subdrv);

	if (!ctx) {
		DRM_ERROR("no drm hdmi context.\n");
		return -EFAULT;
	}

	ctx->hdmi_ctx = hdmi_ctx;
	ctx->mixer_ctx = mixer_ctx;

	ctx->hdmi_ctx->drm_dev = drm_dev;
	ctx->mixer_ctx->drm_dev = drm_dev;

	if (mixer_ops->iommu_on)
		mixer_ops->iommu_on(ctx->mixer_ctx->ctx, true);

	return 0;
}

static void hdmi_subdrv_remove(struct drm_device *drm_dev, struct device *dev)
{
	struct drm_hdmi_context *ctx;
	struct exynos_drm_subdrv *subdrv = to_subdrv(dev);

	ctx = get_ctx_from_subdrv(subdrv);

	if (mixer_ops->iommu_on)
		mixer_ops->iommu_on(ctx->mixer_ctx->ctx, false);
}

static int exynos_drm_hdmi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct exynos_drm_subdrv *subdrv;
	struct drm_hdmi_context *ctx;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		DRM_LOG_KMS("failed to alloc common hdmi context.\n");
		return -ENOMEM;
	}

	subdrv = &ctx->subdrv;

	subdrv->dev = dev;
	subdrv->manager = &hdmi_manager;
	subdrv->probe = hdmi_subdrv_probe;
	subdrv->remove = hdmi_subdrv_remove;

	platform_set_drvdata(pdev, subdrv);

	exynos_drm_subdrv_register(subdrv);

	return 0;
}

static int exynos_drm_hdmi_remove(struct platform_device *pdev)
{
	struct drm_hdmi_context *ctx = platform_get_drvdata(pdev);

	DRM_DEBUG_KMS("%s\n", __FILE__);

	exynos_drm_subdrv_unregister(&ctx->subdrv);

	return 0;
}

struct platform_driver exynos_drm_common_hdmi_driver = {
	.probe		= exynos_drm_hdmi_probe,
	.remove		= exynos_drm_hdmi_remove,
	.driver		= {
		.name	= "exynos-drm-hdmi",
		.owner	= THIS_MODULE,
	},
};
