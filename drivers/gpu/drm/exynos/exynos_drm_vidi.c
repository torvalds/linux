// SPDX-License-Identifier: GPL-2.0-or-later
/* exynos_drm_vidi.c
 *
 * Copyright (C) 2012 Samsung Electronics Co.Ltd
 * Authors:
 *	Inki Dae <inki.dae@samsung.com>
 */

#include <linux/component.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/timer.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_vblank.h>
#include <drm/exynos_drm.h>

#include "exynos_drm_crtc.h"
#include "exynos_drm_drv.h"
#include "exynos_drm_fb.h"
#include "exynos_drm_plane.h"
#include "exynos_drm_vidi.h"

/* VIDI uses fixed refresh rate of 50Hz */
#define VIDI_REFRESH_TIME (1000 / 50)

/* vidi has totally three virtual windows. */
#define WINDOWS_NR		3

#define ctx_from_connector(c)	container_of(c, struct vidi_context, \
					connector)

struct vidi_context {
	struct drm_encoder		encoder;
	struct drm_device		*drm_dev;
	struct device			*dev;
	struct exynos_drm_crtc		*crtc;
	struct drm_connector		connector;
	struct exynos_drm_plane		planes[WINDOWS_NR];
	struct edid			*raw_edid;
	unsigned int			clkdiv;
	unsigned int			connected;
	bool				suspended;
	struct timer_list		timer;
	struct mutex			lock;
};

static inline struct vidi_context *encoder_to_vidi(struct drm_encoder *e)
{
	return container_of(e, struct vidi_context, encoder);
}

static const char fake_edid_info[] = {
	0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x4c, 0x2d, 0x05, 0x05,
	0x00, 0x00, 0x00, 0x00, 0x30, 0x12, 0x01, 0x03, 0x80, 0x10, 0x09, 0x78,
	0x0a, 0xee, 0x91, 0xa3, 0x54, 0x4c, 0x99, 0x26, 0x0f, 0x50, 0x54, 0xbd,
	0xee, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x66, 0x21, 0x50, 0xb0, 0x51, 0x00,
	0x1b, 0x30, 0x40, 0x70, 0x36, 0x00, 0xa0, 0x5a, 0x00, 0x00, 0x00, 0x1e,
	0x01, 0x1d, 0x00, 0x72, 0x51, 0xd0, 0x1e, 0x20, 0x6e, 0x28, 0x55, 0x00,
	0xa0, 0x5a, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x00, 0xfd, 0x00, 0x18,
	0x4b, 0x1a, 0x44, 0x17, 0x00, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
	0x00, 0x00, 0x00, 0xfc, 0x00, 0x53, 0x41, 0x4d, 0x53, 0x55, 0x4e, 0x47,
	0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x01, 0xbc, 0x02, 0x03, 0x1e, 0xf1,
	0x46, 0x84, 0x05, 0x03, 0x10, 0x20, 0x22, 0x23, 0x09, 0x07, 0x07, 0x83,
	0x01, 0x00, 0x00, 0xe2, 0x00, 0x0f, 0x67, 0x03, 0x0c, 0x00, 0x10, 0x00,
	0xb8, 0x2d, 0x01, 0x1d, 0x80, 0x18, 0x71, 0x1c, 0x16, 0x20, 0x58, 0x2c,
	0x25, 0x00, 0xa0, 0x5a, 0x00, 0x00, 0x00, 0x9e, 0x8c, 0x0a, 0xd0, 0x8a,
	0x20, 0xe0, 0x2d, 0x10, 0x10, 0x3e, 0x96, 0x00, 0xa0, 0x5a, 0x00, 0x00,
	0x00, 0x18, 0x02, 0x3a, 0x80, 0x18, 0x71, 0x38, 0x2d, 0x40, 0x58, 0x2c,
	0x45, 0x00, 0xa0, 0x5a, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x06
};

static const uint32_t formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_NV12,
};

static const enum drm_plane_type vidi_win_types[WINDOWS_NR] = {
	DRM_PLANE_TYPE_PRIMARY,
	DRM_PLANE_TYPE_OVERLAY,
	DRM_PLANE_TYPE_CURSOR,
};

static int vidi_enable_vblank(struct exynos_drm_crtc *crtc)
{
	struct vidi_context *ctx = crtc->ctx;

	if (ctx->suspended)
		return -EPERM;

	mod_timer(&ctx->timer,
		jiffies + msecs_to_jiffies(VIDI_REFRESH_TIME) - 1);

	return 0;
}

static void vidi_disable_vblank(struct exynos_drm_crtc *crtc)
{
}

static void vidi_update_plane(struct exynos_drm_crtc *crtc,
			      struct exynos_drm_plane *plane)
{
	struct drm_plane_state *state = plane->base.state;
	struct vidi_context *ctx = crtc->ctx;
	dma_addr_t addr;

	if (ctx->suspended)
		return;

	addr = exynos_drm_fb_dma_addr(state->fb, 0);
	DRM_DEV_DEBUG_KMS(ctx->dev, "dma_addr = %pad\n", &addr);
}

static void vidi_atomic_enable(struct exynos_drm_crtc *crtc)
{
	struct vidi_context *ctx = crtc->ctx;

	mutex_lock(&ctx->lock);

	ctx->suspended = false;

	mutex_unlock(&ctx->lock);

	drm_crtc_vblank_on(&crtc->base);
}

static void vidi_atomic_disable(struct exynos_drm_crtc *crtc)
{
	struct vidi_context *ctx = crtc->ctx;

	drm_crtc_vblank_off(&crtc->base);

	mutex_lock(&ctx->lock);

	ctx->suspended = true;

	mutex_unlock(&ctx->lock);
}

static const struct exynos_drm_crtc_ops vidi_crtc_ops = {
	.atomic_enable = vidi_atomic_enable,
	.atomic_disable = vidi_atomic_disable,
	.enable_vblank = vidi_enable_vblank,
	.disable_vblank = vidi_disable_vblank,
	.update_plane = vidi_update_plane,
	.atomic_flush = exynos_crtc_handle_event,
};

static void vidi_fake_vblank_timer(struct timer_list *t)
{
	struct vidi_context *ctx = from_timer(ctx, t, timer);

	if (drm_crtc_handle_vblank(&ctx->crtc->base))
		mod_timer(&ctx->timer,
			jiffies + msecs_to_jiffies(VIDI_REFRESH_TIME) - 1);
}

static ssize_t vidi_show_connection(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct vidi_context *ctx = dev_get_drvdata(dev);
	int rc;

	mutex_lock(&ctx->lock);

	rc = sprintf(buf, "%d\n", ctx->connected);

	mutex_unlock(&ctx->lock);

	return rc;
}

static ssize_t vidi_store_connection(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t len)
{
	struct vidi_context *ctx = dev_get_drvdata(dev);
	int ret;

	ret = kstrtoint(buf, 0, &ctx->connected);
	if (ret)
		return ret;

	if (ctx->connected > 1)
		return -EINVAL;

	/* use fake edid data for test. */
	if (!ctx->raw_edid)
		ctx->raw_edid = (struct edid *)fake_edid_info;

	/* if raw_edid isn't same as fake data then it can't be tested. */
	if (ctx->raw_edid != (struct edid *)fake_edid_info) {
		DRM_DEV_DEBUG_KMS(dev, "edid data is not fake data.\n");
		return -EINVAL;
	}

	DRM_DEV_DEBUG_KMS(dev, "requested connection.\n");

	drm_helper_hpd_irq_event(ctx->drm_dev);

	return len;
}

static DEVICE_ATTR(connection, 0644, vidi_show_connection,
			vidi_store_connection);

static struct attribute *vidi_attrs[] = {
	&dev_attr_connection.attr,
	NULL,
};
ATTRIBUTE_GROUPS(vidi);

int vidi_connection_ioctl(struct drm_device *drm_dev, void *data,
				struct drm_file *file_priv)
{
	struct vidi_context *ctx = dev_get_drvdata(drm_dev->dev);
	struct drm_exynos_vidi_connection *vidi = data;

	if (!vidi) {
		DRM_DEV_DEBUG_KMS(ctx->dev,
				  "user data for vidi is null.\n");
		return -EINVAL;
	}

	if (vidi->connection > 1) {
		DRM_DEV_DEBUG_KMS(ctx->dev,
				  "connection should be 0 or 1.\n");
		return -EINVAL;
	}

	if (ctx->connected == vidi->connection) {
		DRM_DEV_DEBUG_KMS(ctx->dev,
				  "same connection request.\n");
		return -EINVAL;
	}

	if (vidi->connection) {
		struct edid *raw_edid;

		raw_edid = (struct edid *)(unsigned long)vidi->edid;
		if (!drm_edid_is_valid(raw_edid)) {
			DRM_DEV_DEBUG_KMS(ctx->dev,
					  "edid data is invalid.\n");
			return -EINVAL;
		}
		ctx->raw_edid = drm_edid_duplicate(raw_edid);
		if (!ctx->raw_edid) {
			DRM_DEV_DEBUG_KMS(ctx->dev,
					  "failed to allocate raw_edid.\n");
			return -ENOMEM;
		}
	} else {
		/*
		 * with connection = 0, free raw_edid
		 * only if raw edid data isn't same as fake data.
		 */
		if (ctx->raw_edid && ctx->raw_edid !=
				(struct edid *)fake_edid_info) {
			kfree(ctx->raw_edid);
			ctx->raw_edid = NULL;
		}
	}

	ctx->connected = vidi->connection;
	drm_helper_hpd_irq_event(ctx->drm_dev);

	return 0;
}

static enum drm_connector_status vidi_detect(struct drm_connector *connector,
			bool force)
{
	struct vidi_context *ctx = ctx_from_connector(connector);

	/*
	 * connection request would come from user side
	 * to do hotplug through specific ioctl.
	 */
	return ctx->connected ? connector_status_connected :
			connector_status_disconnected;
}

static void vidi_connector_destroy(struct drm_connector *connector)
{
}

static const struct drm_connector_funcs vidi_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = vidi_detect,
	.destroy = vidi_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int vidi_get_modes(struct drm_connector *connector)
{
	struct vidi_context *ctx = ctx_from_connector(connector);
	struct edid *edid;
	int edid_len;

	/*
	 * the edid data comes from user side and it would be set
	 * to ctx->raw_edid through specific ioctl.
	 */
	if (!ctx->raw_edid) {
		DRM_DEV_DEBUG_KMS(ctx->dev, "raw_edid is null.\n");
		return -EFAULT;
	}

	edid_len = (1 + ctx->raw_edid->extensions) * EDID_LENGTH;
	edid = kmemdup(ctx->raw_edid, edid_len, GFP_KERNEL);
	if (!edid) {
		DRM_DEV_DEBUG_KMS(ctx->dev, "failed to allocate edid\n");
		return -ENOMEM;
	}

	drm_connector_update_edid_property(connector, edid);

	return drm_add_edid_modes(connector, edid);
}

static const struct drm_connector_helper_funcs vidi_connector_helper_funcs = {
	.get_modes = vidi_get_modes,
};

static int vidi_create_connector(struct drm_encoder *encoder)
{
	struct vidi_context *ctx = encoder_to_vidi(encoder);
	struct drm_connector *connector = &ctx->connector;
	int ret;

	connector->polled = DRM_CONNECTOR_POLL_HPD;

	ret = drm_connector_init(ctx->drm_dev, connector,
			&vidi_connector_funcs, DRM_MODE_CONNECTOR_VIRTUAL);
	if (ret) {
		DRM_DEV_ERROR(ctx->dev,
			      "Failed to initialize connector with drm\n");
		return ret;
	}

	drm_connector_helper_add(connector, &vidi_connector_helper_funcs);
	drm_connector_attach_encoder(connector, encoder);

	return 0;
}

static void exynos_vidi_mode_set(struct drm_encoder *encoder,
			       struct drm_display_mode *mode,
			       struct drm_display_mode *adjusted_mode)
{
}

static void exynos_vidi_enable(struct drm_encoder *encoder)
{
}

static void exynos_vidi_disable(struct drm_encoder *encoder)
{
}

static const struct drm_encoder_helper_funcs exynos_vidi_encoder_helper_funcs = {
	.mode_set = exynos_vidi_mode_set,
	.enable = exynos_vidi_enable,
	.disable = exynos_vidi_disable,
};

static int vidi_bind(struct device *dev, struct device *master, void *data)
{
	struct vidi_context *ctx = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	struct drm_encoder *encoder = &ctx->encoder;
	struct exynos_drm_plane *exynos_plane;
	struct exynos_drm_plane_config plane_config = { 0 };
	unsigned int i;
	int ret;

	ctx->drm_dev = drm_dev;

	plane_config.pixel_formats = formats;
	plane_config.num_pixel_formats = ARRAY_SIZE(formats);

	for (i = 0; i < WINDOWS_NR; i++) {
		plane_config.zpos = i;
		plane_config.type = vidi_win_types[i];

		ret = exynos_plane_init(drm_dev, &ctx->planes[i], i,
					&plane_config);
		if (ret)
			return ret;
	}

	exynos_plane = &ctx->planes[DEFAULT_WIN];
	ctx->crtc = exynos_drm_crtc_create(drm_dev, &exynos_plane->base,
			EXYNOS_DISPLAY_TYPE_VIDI, &vidi_crtc_ops, ctx);
	if (IS_ERR(ctx->crtc)) {
		DRM_DEV_ERROR(dev, "failed to create crtc.\n");
		return PTR_ERR(ctx->crtc);
	}

	drm_simple_encoder_init(drm_dev, encoder, DRM_MODE_ENCODER_TMDS);

	drm_encoder_helper_add(encoder, &exynos_vidi_encoder_helper_funcs);

	ret = exynos_drm_set_possible_crtcs(encoder, EXYNOS_DISPLAY_TYPE_VIDI);
	if (ret < 0)
		return ret;

	ret = vidi_create_connector(encoder);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to create connector ret = %d\n",
			      ret);
		drm_encoder_cleanup(encoder);
		return ret;
	}

	return 0;
}


static void vidi_unbind(struct device *dev, struct device *master, void *data)
{
	struct vidi_context *ctx = dev_get_drvdata(dev);

	del_timer_sync(&ctx->timer);
}

static const struct component_ops vidi_component_ops = {
	.bind	= vidi_bind,
	.unbind = vidi_unbind,
};

static int vidi_probe(struct platform_device *pdev)
{
	struct vidi_context *ctx;
	struct device *dev = &pdev->dev;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->dev = dev;

	timer_setup(&ctx->timer, vidi_fake_vblank_timer, 0);

	mutex_init(&ctx->lock);

	platform_set_drvdata(pdev, ctx);

	return component_add(dev, &vidi_component_ops);
}

static int vidi_remove(struct platform_device *pdev)
{
	struct vidi_context *ctx = platform_get_drvdata(pdev);

	if (ctx->raw_edid != (struct edid *)fake_edid_info) {
		kfree(ctx->raw_edid);
		ctx->raw_edid = NULL;

		return -EINVAL;
	}

	component_del(&pdev->dev, &vidi_component_ops);

	return 0;
}

struct platform_driver vidi_driver = {
	.probe		= vidi_probe,
	.remove		= vidi_remove,
	.driver		= {
		.name	= "exynos-drm-vidi",
		.owner	= THIS_MODULE,
		.dev_groups = vidi_groups,
	},
};
