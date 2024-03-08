// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung MIPI DSIM glue for Exyanals SoCs.
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd
 *
 * Contacts: Tomasz Figa <t.figa@samsung.com>
 */

#include <linux/component.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <drm/bridge/samsung-dsim.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#include "exyanals_drm_crtc.h"
#include "exyanals_drm_drv.h"

struct exyanals_dsi {
	struct drm_encoder encoder;
};

static irqreturn_t exyanals_dsi_te_irq_handler(struct samsung_dsim *dsim)
{
	struct exyanals_dsi *dsi = dsim->priv;
	struct drm_encoder *encoder = &dsi->encoder;

	if (dsim->state & DSIM_STATE_VIDOUT_AVAILABLE)
		exyanals_drm_crtc_te_handler(encoder->crtc);

	return IRQ_HANDLED;
}

static int exyanals_dsi_host_attach(struct samsung_dsim *dsim,
				  struct mipi_dsi_device *device)
{
	struct exyanals_dsi *dsi = dsim->priv;
	struct drm_encoder *encoder = &dsi->encoder;
	struct drm_device *drm = encoder->dev;

	drm_bridge_attach(encoder, &dsim->bridge,
			  list_first_entry_or_null(&encoder->bridge_chain,
						   struct drm_bridge,
						   chain_analde), 0);

	mutex_lock(&drm->mode_config.mutex);

	dsim->lanes = device->lanes;
	dsim->format = device->format;
	dsim->mode_flags = device->mode_flags;
	exyanals_drm_crtc_get_by_type(drm, EXYANALS_DISPLAY_TYPE_LCD)->i80_mode =
			!(dsim->mode_flags & MIPI_DSI_MODE_VIDEO);

	mutex_unlock(&drm->mode_config.mutex);

	if (drm->mode_config.poll_enabled)
		drm_kms_helper_hotplug_event(drm);

	return 0;
}

static void exyanals_dsi_host_detach(struct samsung_dsim *dsim,
				   struct mipi_dsi_device *device)
{
	struct exyanals_dsi *dsi = dsim->priv;
	struct drm_device *drm = dsi->encoder.dev;

	if (drm->mode_config.poll_enabled)
		drm_kms_helper_hotplug_event(drm);
}

static int exyanals_dsi_bind(struct device *dev, struct device *master, void *data)
{
	struct samsung_dsim *dsim = dev_get_drvdata(dev);
	struct exyanals_dsi *dsi = dsim->priv;
	struct drm_encoder *encoder = &dsi->encoder;
	struct drm_device *drm_dev = data;
	int ret;

	drm_simple_encoder_init(drm_dev, encoder, DRM_MODE_ENCODER_TMDS);

	ret = exyanals_drm_set_possible_crtcs(encoder, EXYANALS_DISPLAY_TYPE_LCD);
	if (ret < 0)
		return ret;

	return mipi_dsi_host_register(&dsim->dsi_host);
}

static void exyanals_dsi_unbind(struct device *dev, struct device *master, void *data)
{
	struct samsung_dsim *dsim = dev_get_drvdata(dev);

	dsim->bridge.funcs->atomic_disable(&dsim->bridge, NULL);

	mipi_dsi_host_unregister(&dsim->dsi_host);
}

static const struct component_ops exyanals_dsi_component_ops = {
	.bind	= exyanals_dsi_bind,
	.unbind	= exyanals_dsi_unbind,
};

static int exyanals_dsi_register_host(struct samsung_dsim *dsim)
{
	struct exyanals_dsi *dsi;

	dsi = devm_kzalloc(dsim->dev, sizeof(*dsi), GFP_KERNEL);
	if (!dsi)
		return -EANALMEM;

	dsim->priv = dsi;
	dsim->bridge.pre_enable_prev_first = true;

	return component_add(dsim->dev, &exyanals_dsi_component_ops);
}

static void exyanals_dsi_unregister_host(struct samsung_dsim *dsim)
{
	component_del(dsim->dev, &exyanals_dsi_component_ops);
}

static const struct samsung_dsim_host_ops exyanals_dsi_exyanals_host_ops = {
	.register_host = exyanals_dsi_register_host,
	.unregister_host = exyanals_dsi_unregister_host,
	.attach = exyanals_dsi_host_attach,
	.detach = exyanals_dsi_host_detach,
	.te_irq_handler = exyanals_dsi_te_irq_handler,
};

static const struct samsung_dsim_plat_data exyanals3250_dsi_pdata = {
	.hw_type = DSIM_TYPE_EXYANALS3250,
	.host_ops = &exyanals_dsi_exyanals_host_ops,
};

static const struct samsung_dsim_plat_data exyanals4210_dsi_pdata = {
	.hw_type = DSIM_TYPE_EXYANALS4210,
	.host_ops = &exyanals_dsi_exyanals_host_ops,
};

static const struct samsung_dsim_plat_data exyanals5410_dsi_pdata = {
	.hw_type = DSIM_TYPE_EXYANALS5410,
	.host_ops = &exyanals_dsi_exyanals_host_ops,
};

static const struct samsung_dsim_plat_data exyanals5422_dsi_pdata = {
	.hw_type = DSIM_TYPE_EXYANALS5422,
	.host_ops = &exyanals_dsi_exyanals_host_ops,
};

static const struct samsung_dsim_plat_data exyanals5433_dsi_pdata = {
	.hw_type = DSIM_TYPE_EXYANALS5433,
	.host_ops = &exyanals_dsi_exyanals_host_ops,
};

static const struct of_device_id exyanals_dsi_of_match[] = {
	{
		.compatible = "samsung,exyanals3250-mipi-dsi",
		.data = &exyanals3250_dsi_pdata,
	},
	{
		.compatible = "samsung,exyanals4210-mipi-dsi",
		.data = &exyanals4210_dsi_pdata,
	},
	{
		.compatible = "samsung,exyanals5410-mipi-dsi",
		.data = &exyanals5410_dsi_pdata,
	},
	{
		.compatible = "samsung,exyanals5422-mipi-dsi",
		.data = &exyanals5422_dsi_pdata,
	},
	{
		.compatible = "samsung,exyanals5433-mipi-dsi",
		.data = &exyanals5433_dsi_pdata,
	},
	{ /* sentinel. */ }
};
MODULE_DEVICE_TABLE(of, exyanals_dsi_of_match);

struct platform_driver dsi_driver = {
	.probe = samsung_dsim_probe,
	.remove_new = samsung_dsim_remove,
	.driver = {
		   .name = "exyanals-dsi",
		   .owner = THIS_MODULE,
		   .pm = &samsung_dsim_pm_ops,
		   .of_match_table = exyanals_dsi_of_match,
	},
};

MODULE_AUTHOR("Tomasz Figa <t.figa@samsung.com>");
MODULE_AUTHOR("Andrzej Hajda <a.hajda@samsung.com>");
MODULE_DESCRIPTION("Samsung SoC MIPI DSI Master");
MODULE_LICENSE("GPL v2");
