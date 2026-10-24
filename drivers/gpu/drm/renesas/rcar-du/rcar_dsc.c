// SPDX-License-Identifier: GPL-2.0
/*
 * R-Car DSC Encoder
 *
 * Copyright (C) 2025 Marek Vasut <marek.vasut+renesas@mailbox.org>
 * Copyright (C) 2025 Renesas Electronics Corporation
 */

#include <linux/container_of.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>

struct rcar_dsc {
	struct drm_bridge bridge;

	struct device *dev;
};

static inline struct rcar_dsc *bridge_to_rcar_dsc(struct drm_bridge *bridge)
{
	return container_of(bridge, struct rcar_dsc, bridge);
}

/* -----------------------------------------------------------------------------
 * Bridge
 */

static int rcar_dsc_attach(struct drm_bridge *bridge,
			   struct drm_encoder *encoder,
			   enum drm_bridge_attach_flags flags)
{
	struct rcar_dsc *dsc = bridge_to_rcar_dsc(bridge);

	if (!(flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR))
		return -EINVAL;

	return drm_bridge_attach(encoder, dsc->bridge.next_bridge, bridge,
				 DRM_BRIDGE_ATTACH_NO_CONNECTOR);
}

static void rcar_dsc_atomic_enable(struct drm_bridge *bridge,
				   struct drm_atomic_commit *commit)
{
	struct rcar_dsc *dsc = bridge_to_rcar_dsc(bridge);

	WARN_ON(pm_runtime_get_sync(dsc->dev) < 0);
}

static void rcar_dsc_atomic_disable(struct drm_bridge *bridge,
				    struct drm_atomic_commit *commit)
{
	struct rcar_dsc *dsc = bridge_to_rcar_dsc(bridge);

	pm_runtime_put(dsc->dev);
}

static enum drm_mode_status
rcar_dsc_bridge_mode_valid(struct drm_bridge *bridge,
			   const struct drm_display_info *info,
			   const struct drm_display_mode *mode)
{
	if (mode->hdisplay < 320 || mode->hdisplay > 8190)
		return MODE_BAD_HVALUE;

	if (mode->vdisplay < 160 || mode->vdisplay > 8190)
		return MODE_BAD_VVALUE;

	if (mode->clock > 400000) /* Really 400 Mpixel/s */
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static const struct drm_bridge_funcs rcar_dsc_bridge_ops = {
	.attach = rcar_dsc_attach,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_create_state = drm_atomic_helper_bridge_create_state,
	.atomic_enable = rcar_dsc_atomic_enable,
	.atomic_disable = rcar_dsc_atomic_disable,
	.mode_valid = rcar_dsc_bridge_mode_valid,
};

/* -----------------------------------------------------------------------------
 * Probe & Remove
 */

static int rcar_dsc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rcar_dsc *dsc;
	int ret;

	dsc = devm_drm_bridge_alloc(dev, struct rcar_dsc, bridge,
				    &rcar_dsc_bridge_ops);
	if (IS_ERR(dsc))
		return PTR_ERR(dsc);

	platform_set_drvdata(pdev, dsc);

	dsc->dev = &pdev->dev;

	dsc->bridge.next_bridge = of_drm_get_bridge_by_endpoint(dev->of_node,
								1, 0);
	if (IS_ERR(dsc->bridge.next_bridge))
		return PTR_ERR(dsc->bridge.next_bridge);

	dsc->bridge.of_node = dev->of_node;

	pm_runtime_enable(&pdev->dev);

	ret = devm_drm_bridge_add(dev, &dsc->bridge);
	if (ret)
		goto err_runtime_disable;

	return 0;

err_runtime_disable:
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static void rcar_dsc_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
}

static const struct of_device_id rcar_dsc_of_table[] = {
	{ .compatible = "renesas,r8a779g0-dsc" },
	{}
};

MODULE_DEVICE_TABLE(of, rcar_dsc_of_table);

static struct platform_driver rcar_dsc_platform_driver = {
	.probe          = rcar_dsc_probe,
	.remove		= rcar_dsc_remove,
	.driver         = {
		.name   = "rcar-dsc",
		.of_match_table = rcar_dsc_of_table,
	},
};

module_platform_driver(rcar_dsc_platform_driver);

MODULE_DESCRIPTION("Renesas R-Car DSC Encoder Driver");
MODULE_LICENSE("GPL");
