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

#include <drm/drm_atomic_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_print.h>

#include "hibmc_drm_drv.h"
#include "hibmc_drm_regs.h"

static int hibmc_connector_get_modes(struct drm_connector *connector)
{
	int count;

	count = drm_add_modes_noedid(connector,
				     connector->dev->mode_config.max_width,
				     connector->dev->mode_config.max_height);
	drm_set_preferred_mode(connector, 1024, 768);

	return count;
}

static enum drm_mode_status hibmc_connector_mode_valid(struct drm_connector *connector,
				      struct drm_display_mode *mode)
{
	return MODE_OK;
}

static const struct drm_connector_helper_funcs
	hibmc_connector_helper_funcs = {
	.get_modes = hibmc_connector_get_modes,
	.mode_valid = hibmc_connector_mode_valid,
};

static const struct drm_connector_funcs hibmc_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
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
	struct hibmc_drm_private *priv = dev->dev_private;

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

static const struct drm_encoder_funcs hibmc_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

int hibmc_vdac_init(struct hibmc_drm_private *priv)
{
	struct drm_device *dev = priv->dev;
	struct drm_encoder *encoder = &priv->encoder;
	struct drm_connector *connector = &priv->connector;
	int ret;

	encoder->possible_crtcs = 0x1;
	ret = drm_encoder_init(dev, encoder, &hibmc_encoder_funcs,
			       DRM_MODE_ENCODER_DAC, NULL);
	if (ret) {
		drm_err(dev, "failed to init encoder: %d\n", ret);
		return ret;
	}

	drm_encoder_helper_add(encoder, &hibmc_encoder_helper_funcs);

	ret = drm_connector_init(dev, connector, &hibmc_connector_funcs,
				 DRM_MODE_CONNECTOR_VGA);
	if (ret) {
		drm_err(dev, "failed to init connector: %d\n", ret);
		return ret;
	}
	drm_connector_helper_add(connector, &hibmc_connector_helper_funcs);

	drm_connector_attach_encoder(connector, encoder);

	return 0;
}
