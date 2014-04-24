/*
 * Copyright (C) ROCKCHIP, Inc.
 * Author:yzq<yzq@rock-chips.com>
 *
 * based on exynos_drm_core.c
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
#include "rockchip_drm_drv.h"
#include "rockchip_drm_encoder.h"
#include "rockchip_drm_connector.h"
#include "rockchip_drm_fbdev.h"

static LIST_HEAD(rockchip_drm_subdrv_list);

static int rockchip_drm_create_enc_conn(struct drm_device *dev,
					struct rockchip_drm_subdrv *subdrv)
{
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	int ret;

	DRM_DEBUG_DRIVER("%s\n", __FILE__);

	subdrv->manager->dev = subdrv->dev;

	/* create and initialize a encoder for this sub driver. */
	encoder = rockchip_drm_encoder_create(dev, subdrv->manager,
			(1 << MAX_CRTC) - 1);
	if (!encoder) {
		DRM_ERROR("failed to create encoder\n");
		return -EFAULT;
	}

	/*
	 * create and initialize a connector for this sub driver and
	 * attach the encoder created above to the connector.
	 */
	connector = rockchip_drm_connector_create(dev, encoder);
	if (!connector) {
		DRM_ERROR("failed to create connector\n");
		ret = -EFAULT;
		goto err_destroy_encoder;
	}

	subdrv->encoder = encoder;
	subdrv->connector = connector;

	return 0;

err_destroy_encoder:
	encoder->funcs->destroy(encoder);
	return ret;
}

static void rockchip_drm_destroy_enc_conn(struct rockchip_drm_subdrv *subdrv)
{
	if (subdrv->encoder) {
		struct drm_encoder *encoder = subdrv->encoder;
		encoder->funcs->destroy(encoder);
		subdrv->encoder = NULL;
	}

	if (subdrv->connector) {
		struct drm_connector *connector = subdrv->connector;
		connector->funcs->destroy(connector);
		subdrv->connector = NULL;
	}
}

static int rockchip_drm_subdrv_probe(struct drm_device *dev,
					struct rockchip_drm_subdrv *subdrv)
{
	if (subdrv->probe) {
		int ret;

		subdrv->drm_dev = dev;

		/*
		 * this probe callback would be called by sub driver
		 * after setting of all resources to this sub driver,
		 * such as clock, irq and register map are done or by load()
		 * of rockchip drm driver.
		 *
		 * P.S. note that this driver is considered for modularization.
		 */
		ret = subdrv->probe(dev, subdrv->dev);
		if (ret)
			return ret;
	}

	return 0;
}

static void rockchip_drm_subdrv_remove(struct drm_device *dev,
				      struct rockchip_drm_subdrv *subdrv)
{
	DRM_DEBUG_DRIVER("%s\n", __FILE__);

	if (subdrv->remove)
		subdrv->remove(dev, subdrv->dev);
}

int rockchip_drm_device_register(struct drm_device *dev)
{
	struct rockchip_drm_subdrv *subdrv, *n;
	unsigned int fine_cnt = 0;
	int err;

	DRM_DEBUG_DRIVER("%s\n", __FILE__);

	if (!dev)
		return -EINVAL;

	list_for_each_entry_safe(subdrv, n, &rockchip_drm_subdrv_list, list) {
		err = rockchip_drm_subdrv_probe(dev, subdrv);
		if (err) {
			DRM_DEBUG("rockchip drm subdrv probe failed.\n");
			list_del(&subdrv->list);
			continue;
		}

		/*
		 * if manager is null then it means that this sub driver
		 * doesn't need encoder and connector.
		 */
		if (!subdrv->manager) {
			fine_cnt++;
			continue;
		}

		err = rockchip_drm_create_enc_conn(dev, subdrv);
		if (err) {
			DRM_DEBUG("failed to create encoder and connector.\n");
			rockchip_drm_subdrv_remove(dev, subdrv);
			list_del(&subdrv->list);
			continue;
		}

		fine_cnt++;
	}

	if (!fine_cnt)
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL_GPL(rockchip_drm_device_register);

int rockchip_drm_device_unregister(struct drm_device *dev)
{
	struct rockchip_drm_subdrv *subdrv;

	DRM_DEBUG_DRIVER("%s\n", __FILE__);

	if (!dev) {
		WARN(1, "Unexpected drm device unregister!\n");
		return -EINVAL;
	}

	list_for_each_entry(subdrv, &rockchip_drm_subdrv_list, list) {
		rockchip_drm_subdrv_remove(dev, subdrv);
		rockchip_drm_destroy_enc_conn(subdrv);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rockchip_drm_device_unregister);

int rockchip_drm_subdrv_register(struct rockchip_drm_subdrv *subdrv)
{
	DRM_DEBUG_DRIVER("%s\n", __FILE__);

	if (!subdrv)
		return -EINVAL;

	list_add_tail(&subdrv->list, &rockchip_drm_subdrv_list);

	return 0;
}
EXPORT_SYMBOL_GPL(rockchip_drm_subdrv_register);

int rockchip_drm_subdrv_unregister(struct rockchip_drm_subdrv *subdrv)
{
	DRM_DEBUG_DRIVER("%s\n", __FILE__);

	if (!subdrv)
		return -EINVAL;

	list_del(&subdrv->list);

	return 0;
}
EXPORT_SYMBOL_GPL(rockchip_drm_subdrv_unregister);

int rockchip_drm_subdrv_open(struct drm_device *dev, struct drm_file *file)
{
	struct rockchip_drm_subdrv *subdrv;
	int ret;

	list_for_each_entry(subdrv, &rockchip_drm_subdrv_list, list) {
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
EXPORT_SYMBOL_GPL(rockchip_drm_subdrv_open);

void rockchip_drm_subdrv_close(struct drm_device *dev, struct drm_file *file)
{
	struct rockchip_drm_subdrv *subdrv;

	list_for_each_entry(subdrv, &rockchip_drm_subdrv_list, list) {
		if (subdrv->close)
			subdrv->close(dev, subdrv->dev, file);
	}
}
EXPORT_SYMBOL_GPL(rockchip_drm_subdrv_close);
