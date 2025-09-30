// SPDX-License-Identifier: GPL-2.0-or-later
/* Hisilicon Hibmc SoC drm driver
 *
 * Based on the bochs drm driver.
 *
 * Copyright (c) 2016 Huawei Limited.
 *
 * Author:
 *	Rongrong Zou <zourongrong@huawei.com>
 *	Rongrong Zou <zourongrong@gmail.com>
 *	Jianhua Li <lijianhua@huawei.com>
 */

#include <linux/io.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_simple_kms_helper.h>

#include "hibmc_drm_drv.h"
#include "hibmc_drm_regs.h"

static int hibmc_connector_get_modes(struct drm_connector *connector)
{
	struct hibmc_vdac *vdac = to_hibmc_vdac(connector);
	const struct drm_edid *drm_edid;
	int count;

	drm_edid = drm_edid_read_ddc(connector, &vdac->adapter);

	drm_edid_connector_update(connector, drm_edid);

	if (drm_edid) {
		count = drm_edid_connector_add_modes(connector);
		if (count)
			goto out;
	}

	count = drm_add_modes_noedid(connector,
				     connector->dev->mode_config.max_width,
				     connector->dev->mode_config.max_height);
	drm_set_preferred_mode(connector, 1024, 768);

out:
	drm_edid_free(drm_edid);

	return count;
}

static void hibmc_connector_destroy(struct drm_connector *connector)
{
	struct hibmc_vdac *vdac = to_hibmc_vdac(connector);

	hibmc_ddc_del(vdac);
	drm_connector_cleanup(connector);
}

static const struct drm_connector_helper_funcs
	hibmc_connector_helper_funcs = {
	.get_modes = hibmc_connector_get_modes,
	.detect_ctx = drm_connector_helper_detect_from_ddc,
};

static const struct drm_connector_funcs hibmc_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = hibmc_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static void hibmc_encoder_mode_set(struct drm_encoder *encoder,
				   struct drm_display_mode *mode,
				   struct drm_display_mode *adj_mode)
{
	u32 reg;
	struct drm_device *dev = encoder->dev;
	struct hibmc_drm_private *priv = to_hibmc_drm_private(dev);

	reg = readl(priv->mmio + HIBMC_DISPLAY_CONTROL_HISILE);
	reg |= HIBMC_DISPLAY_CONTROL_FPVDDEN(1);
	reg |= HIBMC_DISPLAY_CONTROL_PANELDATE(1);
	reg |= HIBMC_DISPLAY_CONTROL_FPEN(1);
	reg |= HIBMC_DISPLAY_CONTROL_VBIASEN(1);
	writel(reg, priv->mmio + HIBMC_DISPLAY_CONTROL_HISILE);
}

static const struct drm_encoder_helper_funcs hibmc_encoder_helper_funcs = {
	.mode_set = hibmc_encoder_mode_set,
};

int hibmc_vdac_init(struct hibmc_drm_private *priv)
{
	struct drm_device *dev = &priv->dev;
	struct hibmc_vdac *vdac = &priv->vdac;
	struct drm_encoder *encoder = &vdac->encoder;
	struct drm_crtc *crtc = &priv->crtc;
	struct drm_connector *connector = &vdac->connector;
	int ret;

	ret = hibmc_ddc_create(dev, vdac);
	if (ret) {
		drm_err(dev, "failed to create ddc: %d\n", ret);
		return ret;
	}

	encoder->possible_crtcs = drm_crtc_mask(crtc);
	ret = drmm_encoder_init(dev, encoder, NULL, DRM_MODE_ENCODER_DAC, NULL);
	if (ret) {
		drm_err(dev, "failed to init encoder: %d\n", ret);
		goto err;
	}

	drm_encoder_helper_add(encoder, &hibmc_encoder_helper_funcs);

	ret = drm_connector_init_with_ddc(dev, connector,
					  &hibmc_connector_funcs,
					  DRM_MODE_CONNECTOR_VGA,
					  &vdac->adapter);
	if (ret) {
		drm_err(dev, "failed to init connector: %d\n", ret);
		goto err;
	}

	drm_connector_helper_add(connector, &hibmc_connector_helper_funcs);

	drm_connector_attach_encoder(connector, encoder);

	connector->polled = DRM_CONNECTOR_POLL_CONNECT | DRM_CONNECTOR_POLL_DISCONNECT;

	return 0;

err:
	hibmc_ddc_del(vdac);

	return ret;
}
