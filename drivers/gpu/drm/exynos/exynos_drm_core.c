/* exynos_drm_core.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Author:
 *	Inki Dae <inki.dae@samsung.com>
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *	Seung-Woo Kim <sw0312.kim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <drm/drmP.h>
#include "exynos_drm_drv.h"
#include "exynos_drm_crtc.h"
#include "exynos_drm_encoder.h"
#include "exynos_drm_fbdev.h"

static LIST_HEAD(exynos_drm_subdrv_list);

int exynos_drm_create_enc_conn(struct drm_device *dev,
					struct exynos_drm_display *display)
{
	struct drm_encoder *encoder;
	int ret;
	unsigned long possible_crtcs = 0;

	ret = exynos_drm_crtc_get_pipe_from_type(dev, display->type);
	if (ret < 0)
		return ret;

	possible_crtcs |= 1 << ret;

	/* create and initialize a encoder for this sub driver. */
	encoder = exynos_drm_encoder_create(dev, display, possible_crtcs);
	if (!encoder) {
		DRM_ERROR("failed to create encoder\n");
		return -EFAULT;
	}

	display->encoder = encoder;

	ret = display->ops->create_connector(display, encoder);
	if (ret) {
		DRM_ERROR("failed to create connector ret = %d\n", ret);
		goto err_destroy_encoder;
	}

	return 0;

err_destroy_encoder:
	encoder->funcs->destroy(encoder);
	return ret;
}

int exynos_drm_subdrv_register(struct exynos_drm_subdrv *subdrv)
{
	if (!subdrv)
		return -EINVAL;

	list_add_tail(&subdrv->list, &exynos_drm_subdrv_list);

	return 0;
}
EXPORT_SYMBOL_GPL(exynos_drm_subdrv_register);

int exynos_drm_subdrv_unregister(struct exynos_drm_subdrv *subdrv)
{
	if (!subdrv)
		return -EINVAL;

	list_del(&subdrv->list);

	return 0;
}
EXPORT_SYMBOL_GPL(exynos_drm_subdrv_unregister);

int exynos_drm_device_subdrv_probe(struct drm_device *dev)
{
	struct exynos_drm_subdrv *subdrv, *n;
	int err;

	if (!dev)
		return -EINVAL;

	list_for_each_entry_safe(subdrv, n, &exynos_drm_subdrv_list, list) {
		if (subdrv->probe) {
			subdrv->drm_dev = dev;

			/*
			 * this probe callback would be called by sub driver
			 * after setting of all resources to this sub driver,
			 * such as clock, irq and register map are done.
			 */
			err = subdrv->probe(dev, subdrv->dev);
			if (err) {
				DRM_DEBUG("exynos drm subdrv probe failed.\n");
				list_del(&subdrv->list);
				continue;
			}
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(exynos_drm_device_subdrv_probe);

int exynos_drm_device_subdrv_remove(struct drm_device *dev)
{
	struct exynos_drm_subdrv *subdrv;

	if (!dev) {
		WARN(1, "Unexpected drm device unregister!\n");
		return -EINVAL;
	}

	list_for_each_entry(subdrv, &exynos_drm_subdrv_list, list) {
		if (subdrv->remove)
			subdrv->remove(dev, subdrv->dev);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(exynos_drm_device_subdrv_remove);

int exynos_drm_subdrv_open(struct drm_device *dev, struct drm_file *file)
{
	struct exynos_drm_subdrv *subdrv;
	int ret;

	list_for_each_entry(subdrv, &exynos_drm_subdrv_list, list) {
		if (subdrv->open) {
			ret = subdrv->open(dev, subdrv->dev, file);
			if (ret)
				goto err;
		}
	}

	return 0;

err:
	list_for_each_entry_reverse(subdrv, &subdrv->list, list) {
		if (subdrv->close)
			subdrv->close(dev, subdrv->dev, file);
	}
	return ret;
}
EXPORT_SYMBOL_GPL(exynos_drm_subdrv_open);

void exynos_drm_subdrv_close(struct drm_device *dev, struct drm_file *file)
{
	struct exynos_drm_subdrv *subdrv;

	list_for_each_entry(subdrv, &exynos_drm_subdrv_list, list) {
		if (subdrv->close)
			subdrv->close(dev, subdrv->dev, file);
	}
}
EXPORT_SYMBOL_GPL(exynos_drm_subdrv_close);
