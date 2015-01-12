/* exynos_drm_vidi.c
 *
 * Copyright (C) 2012 Samsung Electronics Co.Ltd
 * Authors:
 *	Inki Dae <inki.dae@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#include <drm/drmP.h>

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/component.h>

#include <drm/exynos_drm.h>

#include <drm/drm_edid.h>
#include <drm/drm_crtc_helper.h>

#include "exynos_drm_drv.h"
#include "exynos_drm_crtc.h"
#include "exynos_drm_encoder.h"
#include "exynos_drm_vidi.h"

/* vidi has totally three virtual windows. */
#define WINDOWS_NR		3

#define ctx_from_connector(c)	container_of(c, struct vidi_context, \
					connector)

struct vidi_win_data {
	unsigned int		offset_x;
	unsigned int		offset_y;
	unsigned int		ovl_width;
	unsigned int		ovl_height;
	unsigned int		fb_width;
	unsigned int		fb_height;
	unsigned int		bpp;
	dma_addr_t		dma_addr;
	unsigned int		buf_offsize;
	unsigned int		line_size;	/* bytes */
	bool			enabled;
};

struct vidi_context {
	struct exynos_drm_manager	manager;
	struct exynos_drm_display	display;
	struct platform_device		*pdev;
	struct drm_device		*drm_dev;
	struct drm_crtc			*crtc;
	struct drm_encoder		*encoder;
	struct drm_connector		connector;
	struct vidi_win_data		win_data[WINDOWS_NR];
	struct edid			*raw_edid;
	unsigned int			clkdiv;
	unsigned int			default_win;
	unsigned long			irq_flags;
	unsigned int			connected;
	bool				vblank_on;
	bool				suspended;
	bool				direct_vblank;
	struct work_struct		work;
	struct mutex			lock;
	int				pipe;
};

static inline struct vidi_context *manager_to_vidi(struct exynos_drm_manager *m)
{
	return container_of(m, struct vidi_context, manager);
}

static inline struct vidi_context *display_to_vidi(struct exynos_drm_display *d)
{
	return container_of(d, struct vidi_context, display);
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

static void vidi_apply(struct exynos_drm_manager *mgr)
{
	struct vidi_context *ctx = manager_to_vidi(mgr);
	struct exynos_drm_manager_ops *mgr_ops = mgr->ops;
	struct vidi_win_data *win_data;
	int i;

	for (i = 0; i < WINDOWS_NR; i++) {
		win_data = &ctx->win_data[i];
		if (win_data->enabled && (mgr_ops && mgr_ops->win_commit))
			mgr_ops->win_commit(mgr, i);
	}

	if (mgr_ops && mgr_ops->commit)
		mgr_ops->commit(mgr);
}

static void vidi_commit(struct exynos_drm_manager *mgr)
{
	struct vidi_context *ctx = manager_to_vidi(mgr);

	if (ctx->suspended)
		return;
}

static int vidi_enable_vblank(struct exynos_drm_manager *mgr)
{
	struct vidi_context *ctx = manager_to_vidi(mgr);

	if (ctx->suspended)
		return -EPERM;

	if (!test_and_set_bit(0, &ctx->irq_flags))
		ctx->vblank_on = true;

	ctx->direct_vblank = true;

	/*
	 * in case of page flip request, vidi_finish_pageflip function
	 * will not be called because direct_vblank is true and then
	 * that function will be called by manager_ops->win_commit callback
	 */
	schedule_work(&ctx->work);

	return 0;
}

static void vidi_disable_vblank(struct exynos_drm_manager *mgr)
{
	struct vidi_context *ctx = manager_to_vidi(mgr);

	if (ctx->suspended)
		return;

	if (test_and_clear_bit(0, &ctx->irq_flags))
		ctx->vblank_on = false;
}

static void vidi_win_mode_set(struct exynos_drm_manager *mgr,
			struct exynos_drm_overlay *overlay)
{
	struct vidi_context *ctx = manager_to_vidi(mgr);
	struct vidi_win_data *win_data;
	int win;
	unsigned long offset;

	if (!overlay) {
		DRM_ERROR("overlay is NULL\n");
		return;
	}

	win = overlay->zpos;
	if (win == DEFAULT_ZPOS)
		win = ctx->default_win;

	if (win < 0 || win >= WINDOWS_NR)
		return;

	offset = overlay->fb_x * (overlay->bpp >> 3);
	offset += overlay->fb_y * overlay->pitch;

	DRM_DEBUG_KMS("offset = 0x%lx, pitch = %x\n", offset, overlay->pitch);

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

	/*
	 * some parts of win_data should be transferred to user side
	 * through specific ioctl.
	 */

	DRM_DEBUG_KMS("offset_x = %d, offset_y = %d\n",
			win_data->offset_x, win_data->offset_y);
	DRM_DEBUG_KMS("ovl_width = %d, ovl_height = %d\n",
			win_data->ovl_width, win_data->ovl_height);
	DRM_DEBUG_KMS("paddr = 0x%lx\n", (unsigned long)win_data->dma_addr);
	DRM_DEBUG_KMS("fb_width = %d, crtc_width = %d\n",
			overlay->fb_width, overlay->crtc_width);
}

static void vidi_win_commit(struct exynos_drm_manager *mgr, int zpos)
{
	struct vidi_context *ctx = manager_to_vidi(mgr);
	struct vidi_win_data *win_data;
	int win = zpos;

	if (ctx->suspended)
		return;

	if (win == DEFAULT_ZPOS)
		win = ctx->default_win;

	if (win < 0 || win >= WINDOWS_NR)
		return;

	win_data = &ctx->win_data[win];

	win_data->enabled = true;

	DRM_DEBUG_KMS("dma_addr = %pad\n", &win_data->dma_addr);

	if (ctx->vblank_on)
		schedule_work(&ctx->work);
}

static void vidi_win_disable(struct exynos_drm_manager *mgr, int zpos)
{
	struct vidi_context *ctx = manager_to_vidi(mgr);
	struct vidi_win_data *win_data;
	int win = zpos;

	if (win == DEFAULT_ZPOS)
		win = ctx->default_win;

	if (win < 0 || win >= WINDOWS_NR)
		return;

	win_data = &ctx->win_data[win];
	win_data->enabled = false;

	/* TODO. */
}

static int vidi_power_on(struct exynos_drm_manager *mgr, bool enable)
{
	struct vidi_context *ctx = manager_to_vidi(mgr);

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (enable != false && enable != true)
		return -EINVAL;

	if (enable) {
		ctx->suspended = false;

		/* if vblank was enabled status, enable it again. */
		if (test_and_clear_bit(0, &ctx->irq_flags))
			vidi_enable_vblank(mgr);

		vidi_apply(mgr);
	} else {
		ctx->suspended = true;
	}

	return 0;
}

static void vidi_dpms(struct exynos_drm_manager *mgr, int mode)
{
	struct vidi_context *ctx = manager_to_vidi(mgr);

	DRM_DEBUG_KMS("%d\n", mode);

	mutex_lock(&ctx->lock);

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		vidi_power_on(mgr, true);
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		vidi_power_on(mgr, false);
		break;
	default:
		DRM_DEBUG_KMS("unspecified mode %d\n", mode);
		break;
	}

	mutex_unlock(&ctx->lock);
}

static int vidi_mgr_initialize(struct exynos_drm_manager *mgr,
			struct drm_device *drm_dev)
{
	struct vidi_context *ctx = manager_to_vidi(mgr);
	struct exynos_drm_private *priv = drm_dev->dev_private;

	mgr->drm_dev = ctx->drm_dev = drm_dev;
	mgr->pipe = ctx->pipe = priv->pipe++;

	return 0;
}

static struct exynos_drm_manager_ops vidi_manager_ops = {
	.dpms = vidi_dpms,
	.commit = vidi_commit,
	.enable_vblank = vidi_enable_vblank,
	.disable_vblank = vidi_disable_vblank,
	.win_mode_set = vidi_win_mode_set,
	.win_commit = vidi_win_commit,
	.win_disable = vidi_win_disable,
};

static void vidi_fake_vblank_handler(struct work_struct *work)
{
	struct vidi_context *ctx = container_of(work, struct vidi_context,
					work);

	if (ctx->pipe < 0)
		return;

	/* refresh rate is about 50Hz. */
	usleep_range(16000, 20000);

	mutex_lock(&ctx->lock);

	if (ctx->direct_vblank) {
		drm_handle_vblank(ctx->drm_dev, ctx->pipe);
		ctx->direct_vblank = false;
		mutex_unlock(&ctx->lock);
		return;
	}

	mutex_unlock(&ctx->lock);

	exynos_drm_crtc_finish_pageflip(ctx->drm_dev, ctx->pipe);
}

static int vidi_show_connection(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct vidi_context *ctx = dev_get_drvdata(dev);
	int rc;

	mutex_lock(&ctx->lock);

	rc = sprintf(buf, "%d\n", ctx->connected);

	mutex_unlock(&ctx->lock);

	return rc;
}

static int vidi_store_connection(struct device *dev,
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
		DRM_DEBUG_KMS("edid data is not fake data.\n");
		return -EINVAL;
	}

	DRM_DEBUG_KMS("requested connection.\n");

	drm_helper_hpd_irq_event(ctx->drm_dev);

	return len;
}

static DEVICE_ATTR(connection, 0644, vidi_show_connection,
			vidi_store_connection);

int vidi_connection_ioctl(struct drm_device *drm_dev, void *data,
				struct drm_file *file_priv)
{
	struct vidi_context *ctx = NULL;
	struct drm_encoder *encoder;
	struct exynos_drm_display *display;
	struct drm_exynos_vidi_connection *vidi = data;

	if (!vidi) {
		DRM_DEBUG_KMS("user data for vidi is null.\n");
		return -EINVAL;
	}

	if (vidi->connection > 1) {
		DRM_DEBUG_KMS("connection should be 0 or 1.\n");
		return -EINVAL;
	}

	list_for_each_entry(encoder, &drm_dev->mode_config.encoder_list,
								head) {
		display = exynos_drm_get_display(encoder);

		if (display->type == EXYNOS_DISPLAY_TYPE_VIDI) {
			ctx = display_to_vidi(display);
			break;
		}
	}

	if (!ctx) {
		DRM_DEBUG_KMS("not found virtual device type encoder.\n");
		return -EINVAL;
	}

	if (ctx->connected == vidi->connection) {
		DRM_DEBUG_KMS("same connection request.\n");
		return -EINVAL;
	}

	if (vidi->connection) {
		struct edid *raw_edid  = (struct edid *)(uint32_t)vidi->edid;
		if (!drm_edid_is_valid(raw_edid)) {
			DRM_DEBUG_KMS("edid data is invalid.\n");
			return -EINVAL;
		}
		ctx->raw_edid = drm_edid_duplicate(raw_edid);
		if (!ctx->raw_edid) {
			DRM_DEBUG_KMS("failed to allocate raw_edid.\n");
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

static struct drm_connector_funcs vidi_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = vidi_detect,
	.destroy = vidi_connector_destroy,
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
		DRM_DEBUG_KMS("raw_edid is null.\n");
		return -EFAULT;
	}

	edid_len = (1 + ctx->raw_edid->extensions) * EDID_LENGTH;
	edid = kmemdup(ctx->raw_edid, edid_len, GFP_KERNEL);
	if (!edid) {
		DRM_DEBUG_KMS("failed to allocate edid\n");
		return -ENOMEM;
	}

	drm_mode_connector_update_edid_property(connector, edid);

	return drm_add_edid_modes(connector, edid);
}

static struct drm_encoder *vidi_best_encoder(struct drm_connector *connector)
{
	struct vidi_context *ctx = ctx_from_connector(connector);

	return ctx->encoder;
}

static struct drm_connector_helper_funcs vidi_connector_helper_funcs = {
	.get_modes = vidi_get_modes,
	.best_encoder = vidi_best_encoder,
};

static int vidi_create_connector(struct exynos_drm_display *display,
				struct drm_encoder *encoder)
{
	struct vidi_context *ctx = display_to_vidi(display);
	struct drm_connector *connector = &ctx->connector;
	int ret;

	ctx->encoder = encoder;
	connector->polled = DRM_CONNECTOR_POLL_HPD;

	ret = drm_connector_init(ctx->drm_dev, connector,
			&vidi_connector_funcs, DRM_MODE_CONNECTOR_VIRTUAL);
	if (ret) {
		DRM_ERROR("Failed to initialize connector with drm\n");
		return ret;
	}

	drm_connector_helper_add(connector, &vidi_connector_helper_funcs);
	drm_connector_register(connector);
	drm_mode_connector_attach_encoder(connector, encoder);

	return 0;
}


static struct exynos_drm_display_ops vidi_display_ops = {
	.create_connector = vidi_create_connector,
};

static int vidi_bind(struct device *dev, struct device *master, void *data)
{
	struct vidi_context *ctx = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	struct drm_crtc *crtc = ctx->crtc;
	int ret;

	vidi_mgr_initialize(&ctx->manager, drm_dev);

	ret = exynos_drm_crtc_create(&ctx->manager);
	if (ret) {
		DRM_ERROR("failed to create crtc.\n");
		return ret;
	}

	ret = exynos_drm_create_enc_conn(drm_dev, &ctx->display);
	if (ret) {
		crtc->funcs->destroy(crtc);
		DRM_ERROR("failed to create encoder and connector.\n");
		return ret;
	}

	return 0;
}


static void vidi_unbind(struct device *dev, struct device *master, void *data)
{
}

static const struct component_ops vidi_component_ops = {
	.bind	= vidi_bind,
	.unbind = vidi_unbind,
};

static int vidi_probe(struct platform_device *pdev)
{
	struct vidi_context *ctx;
	int ret;

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->manager.type = EXYNOS_DISPLAY_TYPE_VIDI;
	ctx->manager.ops = &vidi_manager_ops;
	ctx->display.type = EXYNOS_DISPLAY_TYPE_VIDI;
	ctx->display.ops = &vidi_display_ops;
	ctx->default_win = 0;
	ctx->pdev = pdev;

	ret = exynos_drm_component_add(&pdev->dev, EXYNOS_DEVICE_TYPE_CRTC,
					ctx->manager.type);
	if (ret)
		return ret;

	ret = exynos_drm_component_add(&pdev->dev, EXYNOS_DEVICE_TYPE_CONNECTOR,
					ctx->display.type);
	if (ret)
		goto err_del_crtc_component;

	INIT_WORK(&ctx->work, vidi_fake_vblank_handler);

	mutex_init(&ctx->lock);

	platform_set_drvdata(pdev, ctx);

	ret = device_create_file(&pdev->dev, &dev_attr_connection);
	if (ret < 0) {
		DRM_ERROR("failed to create connection sysfs.\n");
		goto err_del_conn_component;
	}

	ret = component_add(&pdev->dev, &vidi_component_ops);
	if (ret)
		goto err_remove_file;

	return ret;

err_remove_file:
	device_remove_file(&pdev->dev, &dev_attr_connection);
err_del_conn_component:
	exynos_drm_component_del(&pdev->dev, EXYNOS_DEVICE_TYPE_CONNECTOR);
err_del_crtc_component:
	exynos_drm_component_del(&pdev->dev, EXYNOS_DEVICE_TYPE_CRTC);

	return ret;
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
	exynos_drm_component_del(&pdev->dev, EXYNOS_DEVICE_TYPE_CONNECTOR);
	exynos_drm_component_del(&pdev->dev, EXYNOS_DEVICE_TYPE_CRTC);

	return 0;
}

struct platform_driver vidi_driver = {
	.probe		= vidi_probe,
	.remove		= vidi_remove,
	.driver		= {
		.name	= "exynos-drm-vidi",
		.owner	= THIS_MODULE,
	},
};

int exynos_drm_probe_vidi(void)
{
	struct platform_device *pdev;
	int ret;

	pdev = platform_device_register_simple("exynos-drm-vidi", -1, NULL, 0);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	ret = platform_driver_register(&vidi_driver);
	if (ret) {
		platform_device_unregister(pdev);
		return ret;
	}

	return ret;
}

static int exynos_drm_remove_vidi_device(struct device *dev, void *data)
{
	platform_device_unregister(to_platform_device(dev));

	return 0;
}

void exynos_drm_remove_vidi(void)
{
	int ret = driver_for_each_device(&vidi_driver.driver, NULL, NULL,
					 exynos_drm_remove_vidi_device);
	/* silence compiler warning */
	(void)ret;

	platform_driver_unregister(&vidi_driver);
}
