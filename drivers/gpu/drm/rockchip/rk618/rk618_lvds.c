/*
 * Copyright (c) 2017 Rockchip Electronics Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "rk618_output.h"

#define RK618_LVDS_CON			0x0084
#define LVDS_CON_START_PHASE(x)		HIWORD_UPDATE(x, 14, 14)
#define LVDS_DCLK_INV			HIWORD_UPDATE(1, 13, 13)
#define LVDS_CON_CHADS_10PF		HIWORD_UPDATE(3, 12, 11)
#define LVDS_CON_CHADS_5PF		HIWORD_UPDATE(2, 12, 11)
#define LVDS_CON_CHADS_7PF		HIWORD_UPDATE(1, 12, 11)
#define LVDS_CON_CHADS_3PF		HIWORD_UPDATE(0, 12, 11)
#define LVDS_CON_CHA1TTL_ENABLE		HIWORD_UPDATE(1, 10, 10)
#define LVDS_CON_CHA1TTL_DISABLE	HIWORD_UPDATE(0, 10, 10)
#define LVDS_CON_CHA0TTL_ENABLE		HIWORD_UPDATE(1, 9, 9)
#define LVDS_CON_CHA0TTL_DISABLE	HIWORD_UPDATE(0, 9, 9)
#define LVDS_CON_CHA1_POWER_UP		HIWORD_UPDATE(1, 8, 8)
#define LVDS_CON_CHA1_POWER_DOWN	HIWORD_UPDATE(0, 8, 8)
#define LVDS_CON_CHA0_POWER_UP		HIWORD_UPDATE(1, 7, 7)
#define LVDS_CON_CHA0_POWER_DOWN	HIWORD_UPDATE(0, 7, 7)
#define LVDS_CON_CBG_POWER_UP		HIWORD_UPDATE(1, 6, 6)
#define LVDS_CON_CBG_POWER_DOWN		HIWORD_UPDATE(0, 6, 6)
#define LVDS_CON_PLL_POWER_DOWN		HIWORD_UPDATE(1, 5, 5)
#define LVDS_CON_PLL_POWER_UP		HIWORD_UPDATE(0, 5, 5)
#define LVDS_CON_START_SEL_EVEN_PIXEL	HIWORD_UPDATE(1, 4, 4)
#define LVDS_CON_START_SEL_ODD_PIXEL	HIWORD_UPDATE(0, 4, 4)
#define LVDS_CON_CHASEL_DOUBLE_CHANNEL	HIWORD_UPDATE(1, 3, 3)
#define LVDS_CON_CHASEL_SINGLE_CHANNEL	HIWORD_UPDATE(0, 3, 3)
#define LVDS_CON_MSBSEL_D7		HIWORD_UPDATE(1, 2, 2)
#define LVDS_CON_MSBSEL_D0		HIWORD_UPDATE(0, 2, 2)
#define LVDS_CON_SELECT(x)		HIWORD_UPDATE(x, 1, 0)
#define LVDS_CON_SELECT_6BIT_MODE	HIWORD_UPDATE(3, 1, 0)
#define LVDS_CON_SELECT_8BIT_MODE_3	HIWORD_UPDATE(2, 1, 0)
#define LVDS_CON_SELECT_8BIT_MODE_2	HIWORD_UPDATE(1, 1, 0)
#define LVDS_CON_SELECT_8BIT_MODE_1	HIWORD_UPDATE(0, 1, 0)

#define IS_DOUBLE_CHANNEL(lvds)	((lvds)->channels == 2)

enum {
	LVDS_8BIT_MODE_FORMAT_1,
	LVDS_8BIT_MODE_FORMAT_2,
	LVDS_8BIT_MODE_FORMAT_3,
	LVDS_6BIT_MODE,
};

struct rk618_lvds {
	struct rk618_output base;
	struct device *dev;
	struct regmap *regmap;
	struct clk *clock;
	u32 channels;
	u32 format;
};

static inline struct rk618_lvds *to_lvds(struct rk618_output *output)
{
	return container_of(output, struct rk618_lvds, base);
}

static void rk618_lvds_enable(struct rk618_output *output)
{
	struct rk618_lvds *lvds = to_lvds(output);
	u32 value;

	clk_prepare_enable(lvds->clock);

	value = LVDS_CON_CHA0TTL_DISABLE | LVDS_CON_CHA1TTL_DISABLE |
		LVDS_CON_CHA0_POWER_UP | LVDS_CON_CBG_POWER_UP |
		LVDS_CON_PLL_POWER_UP | LVDS_CON_SELECT(lvds->format);

	if (IS_DOUBLE_CHANNEL(lvds))
		value |= LVDS_CON_CHA1_POWER_UP | LVDS_DCLK_INV |
			 LVDS_CON_CHASEL_DOUBLE_CHANNEL;
	else
		value |= LVDS_CON_CHA1_POWER_DOWN |
			 LVDS_CON_CHASEL_SINGLE_CHANNEL;

	regmap_write(lvds->regmap, RK618_LVDS_CON, value);
}

static void rk618_lvds_disable(struct rk618_output *output)
{
	struct rk618_lvds *lvds = to_lvds(output);

	regmap_write(lvds->regmap, RK618_LVDS_CON,
		     LVDS_CON_CHA0_POWER_DOWN | LVDS_CON_CHA1_POWER_DOWN |
		     LVDS_CON_CBG_POWER_DOWN | LVDS_CON_PLL_POWER_DOWN);

	clk_disable_unprepare(lvds->clock);
}

static void rk618_lvds_mode_set(struct rk618_output *output,
				const struct drm_display_mode *mode)
{
	struct rk618_lvds *lvds = to_lvds(output);

	if (mode->hdisplay > 1366 || mode->vdisplay > 1366)
		lvds->channels = 2;
	else
		lvds->channels = 1;

	switch (output->bus_format) {
	case MEDIA_BUS_FMT_RGB666_1X7X3_SPWG:	/* jeida-18 */
		lvds->format = LVDS_6BIT_MODE;
		break;
	case MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA:	/* jeida-24 */
		lvds->format = LVDS_8BIT_MODE_FORMAT_2;
		break;
	case MEDIA_BUS_FMT_RGB888_1X7X4_SPWG:	/* vesa-24 */
		lvds->format = LVDS_8BIT_MODE_FORMAT_1;
		break;
	default:
		lvds->format = LVDS_8BIT_MODE_FORMAT_3;
		break;
	}
}

static const struct rk618_output_funcs rk618_lvds_funcs = {
	.enable = rk618_lvds_enable,
	.disable = rk618_lvds_disable,
	.mode_set = rk618_lvds_mode_set,
};

static int rk618_lvds_bind(struct device *dev, struct device *master,
			   void *data)
{
	struct drm_device *drm = data;
	struct rk618_lvds *lvds = dev_get_drvdata(dev);

	return rk618_output_bind(&lvds->base, drm, DRM_MODE_ENCODER_LVDS,
				 DRM_MODE_CONNECTOR_LVDS);
}

static void rk618_lvds_unbind(struct device *dev, struct device *master,
			      void *data)
{
	struct rk618_lvds *lvds = dev_get_drvdata(dev);

	rk618_output_unbind(&lvds->base);
}

static const struct component_ops rk618_lvds_component_ops = {
	.bind = rk618_lvds_bind,
	.unbind = rk618_lvds_unbind,
};

static int rk618_lvds_probe(struct platform_device *pdev)
{
	struct rk618 *rk618 = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct rk618_lvds *lvds;
	int ret;

	if (!of_device_is_available(dev->of_node))
		return -ENODEV;

	lvds = devm_kzalloc(dev, sizeof(*lvds), GFP_KERNEL);
	if (!lvds)
		return -ENOMEM;

	lvds->dev = dev;
	lvds->regmap = rk618->regmap;
	platform_set_drvdata(pdev, lvds);

	lvds->clock = devm_clk_get(dev, "lvds");
	if (IS_ERR(lvds->clock)) {
		ret = PTR_ERR(lvds->clock);
		dev_err(dev, "failed to get lvds clock: %d\n", ret);
		return ret;
	}

	lvds->base.parent = rk618;
	lvds->base.dev = dev;
	lvds->base.funcs = &rk618_lvds_funcs;
	ret = rk618_output_register(&lvds->base);
	if (ret)
		return ret;

	return component_add(dev, &rk618_lvds_component_ops);
}

static int rk618_lvds_remove(struct platform_device *pdev)
{
	struct rk618_lvds *lvds = platform_get_drvdata(pdev);

	component_del(lvds->dev, &rk618_lvds_component_ops);

	return 0;
}

static const struct of_device_id rk618_lvds_of_match[] = {
	{ .compatible = "rockchip,rk618-lvds", },
	{},
};
MODULE_DEVICE_TABLE(of, rk618_lvds_of_match);

static struct platform_driver rk618_lvds_driver = {
	.driver = {
		.name = "rk618-lvds",
		.of_match_table = of_match_ptr(rk618_lvds_of_match),
	},
	.probe = rk618_lvds_probe,
	.remove = rk618_lvds_remove,
};
module_platform_driver(rk618_lvds_driver);

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip RK618 LVDS driver");
MODULE_LICENSE("GPL v2");
