// SPDX-License-Identifier: GPL-2.0
/*
 * RZ/G3L LVDS Encoder Driver
 *
 * Copyright (C) 2026 Renesas Electronics Corporation
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>

#include "rzg3l_lvds_regs.h"

enum rzg3l_lvds_mode {
	RZG3L_LVDS_MODE_JEIDA = 0,
	RZG3L_LVDS_MODE_JEIDA_MIRROR = 1,
	RZG3L_LVDS_MODE_MODE2 = 2,
	RZG3L_LVDS_MODE_MODE2_MIRROR = 3,
	RZG3L_LVDS_MODE_VESA = 4,
	RZG3L_LVDS_MODE_VESA_MIRROR = 5,
	RZG3L_LVDS_MODE_MODE6 = 6,
	RZG3L_LVDS_MODE_MODE6_MIRROR = 7,
};

struct rzg3l_lvds {
	struct device *dev;
	struct reset_control_bulk_data resets[2];
	struct reset_control_bulk_data dsi_resets[2];
	struct regmap *regmap;
	struct drm_bridge bridge;
};

#define bridge_to_rzg3l_lvds(b) \
	container_of(b, struct rzg3l_lvds, bridge)

static const struct regmap_config rzg3l_lvds_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = LVDS_0_CTL_OFFSET,
};

/* -----------------------------------------------------------------------------
 * Bridge
 */

static void rzg3l_lvds_atomic_enable(struct drm_bridge *bridge,
				     struct drm_atomic_commit *state)
{
	struct rzg3l_lvds *lvds = bridge_to_rzg3l_lvds(bridge);
	const struct drm_bridge_state *bridge_state;
	u32 fmt;

	if (WARN_ON(pm_runtime_get_sync(lvds->dev) < 0))
		return;

	/* Get the LVDS format from the bridge state. */
	bridge_state = drm_atomic_get_new_bridge_state(state, bridge);
	if (WARN_ON(!bridge_state))
		return;

	switch (bridge_state->output_bus_cfg.format) {
	case MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA:
		fmt = RZG3L_LVDS_MODE_JEIDA;
		break;
	case MEDIA_BUS_FMT_RGB888_1X7X4_SPWG:
		fmt = RZG3L_LVDS_MODE_VESA;
		break;
	default:
		fmt = RZG3L_LVDS_MODE_VESA;
		dev_warn(lvds->dev, "Unsupported bus fmt 0x%04x\n",
			 bridge_state->output_bus_cfg.format);
		break;
	}

	regmap_update_bits(lvds->regmap, LVDS_0_PHY_OFFSET,
			   LVDS_0_PHY_CH_EN_BGR, LVDS_0_PHY_CH_EN_BGR);
	fsleep(20);

	regmap_update_bits(lvds->regmap, LVDS_0_PHY_OFFSET,
			   LVDS_0_PHY_CH_EN_LDO, LVDS_0_PHY_CH_EN_LDO);
	fsleep(10);

	regmap_write(lvds->regmap, LVDS_CMN, LVDS_CMN_RST_PHY0_SEL);
	regmap_update_bits(lvds->regmap, LVDS_0_CTL_OFFSET,
			   LVDS_0_CTL_FMT_SEL0_MSK,
			   FIELD_PREP(LVDS_0_CTL_FMT_SEL0_MSK, fmt));
	regmap_update_bits(lvds->regmap, LVDS_0_PHY_OFFSET,
			   LVDS_0_PHY_CH_IO_EN0_MSK, LVDS_0_PHY_CH_IO_EN0);
	regmap_write(lvds->regmap, LVDS_CMN,
		     LVDS_CMN_RST_PHY0_SEL | LVDS_CMN_PHY_RESET);
	fsleep(100);
}

static void rzg3l_lvds_atomic_disable(struct drm_bridge *bridge,
				      struct drm_atomic_commit *state)
{
	struct rzg3l_lvds *lvds = bridge_to_rzg3l_lvds(bridge);

	{
		PM_RUNTIME_ACQUIRE_IF_ENABLED(lvds->dev, pm);
		if (!PM_RUNTIME_ACQUIRE_ERR(&pm)) {
			regmap_update_bits(lvds->regmap, LVDS_CMN,
					   LVDS_CMN_PHY_RESET, 0);
			regmap_update_bits(lvds->regmap, LVDS_0_PHY_OFFSET,
					   LVDS_0_PHY_CH_IO_EN0_MSK, 0);
			regmap_update_bits(lvds->regmap, LVDS_0_PHY_OFFSET,
					   LVDS_0_PHY_CH_EN_LDO, 0);
			regmap_update_bits(lvds->regmap, LVDS_0_PHY_OFFSET,
					   LVDS_0_PHY_CH_EN_BGR, 0);
		}
	}

	pm_runtime_put_sync(lvds->dev);
}

static int rzg3l_lvds_attach(struct drm_bridge *bridge,
			     struct drm_encoder *encoder,
			     enum drm_bridge_attach_flags flags)
{
	struct rzg3l_lvds *lvds = bridge_to_rzg3l_lvds(bridge);

	return drm_bridge_attach(encoder, lvds->bridge.next_bridge, bridge, flags);
}

static enum drm_mode_status
rzg3l_lvds_bridge_mode_valid(struct drm_bridge *bridge,
			     const struct drm_display_info *info,
			     const struct drm_display_mode *mode)
{
	if (mode->clock > 87000)
		return MODE_CLOCK_HIGH;

	if (mode->clock < 25000)
		return MODE_CLOCK_LOW;

	return MODE_OK;
}

static const struct drm_bridge_funcs rzg3l_lvds_bridge_ops = {
	.attach = rzg3l_lvds_attach,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_create_state = drm_atomic_helper_bridge_create_state,
	.atomic_enable = rzg3l_lvds_atomic_enable,
	.atomic_disable = rzg3l_lvds_atomic_disable,
	.mode_valid = rzg3l_lvds_bridge_mode_valid,
};

/* -----------------------------------------------------------------------------
 * Power Management
 */

static int rzg3l_lvds_pm_runtime_suspend(struct device *dev)
{
	struct rzg3l_lvds *lvds = dev_get_drvdata(dev);

	return reset_control_bulk_assert(ARRAY_SIZE(lvds->resets), lvds->resets);
}

static int rzg3l_lvds_pm_runtime_resume(struct device *dev)
{
	struct rzg3l_lvds *lvds = dev_get_drvdata(dev);

	return reset_control_bulk_deassert(ARRAY_SIZE(lvds->resets), lvds->resets);
}

static DEFINE_RUNTIME_DEV_PM_OPS(rzg3l_lvds_pm_ops,
				 rzg3l_lvds_pm_runtime_suspend,
				 rzg3l_lvds_pm_runtime_resume, NULL);

/* -----------------------------------------------------------------------------
 * Probe & Remove
 */

static int rzg3l_lvds_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rzg3l_lvds *lvds;
	void __iomem *base;
	int ret;

	lvds = devm_drm_bridge_alloc(dev, struct rzg3l_lvds, bridge,
				     &rzg3l_lvds_bridge_ops);
	if (IS_ERR(lvds))
		return PTR_ERR(lvds);

	lvds->dev = dev;
	lvds->bridge.of_node = pdev->dev.of_node;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	lvds->regmap = devm_regmap_init_mmio(dev, base, &rzg3l_lvds_regmap_config);
	if (IS_ERR(lvds->regmap))
		return dev_err_probe(dev, PTR_ERR(lvds->regmap),
				     "Failed to init regmap\n");

	lvds->resets[0].id = "prst";
	lvds->resets[1].id = "lvdrst";
	ret = devm_reset_control_bulk_get_exclusive(dev, ARRAY_SIZE(lvds->resets),
						    lvds->resets);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get prst/core resets\n");

	platform_set_drvdata(pdev, lvds);
	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable Runtime PM\n");

	lvds->bridge.next_bridge = devm_drm_of_get_bridge(dev, dev->of_node, 1, 0);
	if (IS_ERR(lvds->bridge.next_bridge))
		return dev_err_probe(dev, PTR_ERR(lvds->bridge.next_bridge),
				     "Failed to get next bridge\n");

	/*
	 * This module cannot be used at the same time as MIPI-DSI, so assert
	 * the MIPI_DSI_CMN_RSTB and MIPI_DSI_ARESET_N resets before using this
	 * module.
	 */
	lvds->dsi_resets[0].id = "rst";
	lvds->dsi_resets[1].id = "arst";
	ret = devm_reset_control_bulk_get_exclusive(dev, ARRAY_SIZE(lvds->dsi_resets),
						    lvds->dsi_resets);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get rst/arst resets\n");

	ret = reset_control_bulk_assert(ARRAY_SIZE(lvds->dsi_resets), lvds->dsi_resets);
	if (ret < 0)
		return ret;

	ret = devm_drm_bridge_add(dev, &lvds->bridge);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to register drm bridge\n");

	return ret;
}

static const struct of_device_id rzg3l_lvds_of_table[] = {
	{ .compatible = "renesas,r9a08g046-lvds" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, rzg3l_lvds_of_table);

static struct platform_driver rzg3l_lvds_platform_driver = {
	.probe		= rzg3l_lvds_probe,
	.driver		= {
		.name	= "rzg3l-lvds",
		.pm	= pm_ptr(&rzg3l_lvds_pm_ops),
		.of_match_table = rzg3l_lvds_of_table,
	},
};

module_platform_driver(rzg3l_lvds_platform_driver);

MODULE_AUTHOR("Biju Das <biju.das.jz@bp.renesas.com>");
MODULE_AUTHOR("Tommaso Merciai <tommaso.merciai.xr@bp.renesas.com>");
MODULE_DESCRIPTION("Renesas RZ/G3L LVDS Encoder Driver");
MODULE_LICENSE("GPL");
