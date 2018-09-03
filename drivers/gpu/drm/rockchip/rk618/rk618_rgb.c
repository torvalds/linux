// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 */

#include "rk618_output.h"

struct rk618_rgb {
	struct rk618_output base;
	struct device *dev;
	struct regmap *regmap;
	struct clk *clock;
};

static inline struct rk618_rgb *to_rgb(struct rk618_output *output)
{
	return container_of(output, struct rk618_rgb, base);
}

static void rk618_rgb_enable(struct rk618_output *output)
{
	struct rk618_rgb *rgb = to_rgb(output);
	u32 value;
	struct device_node *endpoint;
	int lcdc1_output_rgb = 0;

	endpoint = of_graph_get_endpoint_by_regs(output->dev->of_node, 1, 0);
	if (endpoint && of_device_is_available(endpoint))
		lcdc1_output_rgb = 1;

	clk_prepare_enable(rgb->clock);

	if (lcdc1_output_rgb) {
		value = LVDS_CON_CHA1TTL_DISABLE | LVDS_CON_CHA0TTL_DISABLE |
			LVDS_CON_CHA1_POWER_DOWN | LVDS_CON_CHA0_POWER_DOWN |
			LVDS_CON_CBG_POWER_DOWN | LVDS_CON_PLL_POWER_DOWN;
		regmap_write(rgb->regmap, RK618_LVDS_CON, value);

		regmap_write(rgb->regmap, RK618_IO_CON0,
			     PORT1_OUTPUT_TTL_ENABLE);
	} else {
		value = LVDS_CON_CBG_POWER_DOWN | LVDS_CON_CHA1_POWER_DOWN |
			LVDS_CON_CHA0_POWER_DOWN;
		value |= LVDS_CON_CHA0TTL_ENABLE | LVDS_CON_CHA1TTL_ENABLE |
			LVDS_CON_PLL_POWER_DOWN;
		regmap_write(rgb->regmap, RK618_LVDS_CON, value);

		regmap_write(rgb->regmap, RK618_IO_CON0, PORT2_OUTPUT_TTL);
	}
}

static void rk618_rgb_disable(struct rk618_output *output)
{
	struct rk618_rgb *rgb = to_rgb(output);

	regmap_write(rgb->regmap, RK618_LVDS_CON,
		     LVDS_CON_CHA0_POWER_DOWN | LVDS_CON_CHA1_POWER_DOWN |
		     LVDS_CON_CBG_POWER_DOWN | LVDS_CON_PLL_POWER_DOWN);

	clk_disable_unprepare(rgb->clock);
}

static const struct rk618_output_funcs rk618_rgb_funcs = {
	.enable = rk618_rgb_enable,
	.disable = rk618_rgb_disable,
};

static int rk618_rgb_bind(struct device *dev, struct device *master,
			  void *data)
{
	struct drm_device *drm = data;
	struct rk618_rgb *rgb = dev_get_drvdata(dev);

	return rk618_output_bind(&rgb->base, drm, DRM_MODE_ENCODER_LVDS,
				 DRM_MODE_CONNECTOR_LVDS);
}

static void rk618_rgb_unbind(struct device *dev, struct device *master,
			     void *data)
{
	struct rk618_rgb *rgb = dev_get_drvdata(dev);

	rk618_output_unbind(&rgb->base);
}

static const struct component_ops rk618_rgb_component_ops = {
	.bind = rk618_rgb_bind,
	.unbind = rk618_rgb_unbind,
};

static const struct of_device_id rk618_rgb_of_match[] = {
	{ .compatible = "rockchip,rk618-rgb", },
	{},
};
MODULE_DEVICE_TABLE(of, rk618_rgb_of_match);

static int rk618_rgb_probe(struct platform_device *pdev)
{
	struct rk618 *rk618 = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct rk618_rgb *rgb;
	int ret;

	if (!of_device_is_available(dev->of_node))
		return -ENODEV;

	rgb = devm_kzalloc(dev, sizeof(*rgb), GFP_KERNEL);
	if (!rgb)
		return -ENOMEM;

	rgb->dev = dev;
	rgb->regmap = rk618->regmap;
	platform_set_drvdata(pdev, rgb);

	rgb->clock = devm_clk_get(dev, "rgb");
	if (IS_ERR(rgb->clock)) {
		ret = PTR_ERR(rgb->clock);
		dev_err(dev, "failed to get rgb clock: %d\n", ret);
		return ret;
	}

	rgb->base.parent = rk618;
	rgb->base.dev = dev;
	rgb->base.funcs = &rk618_rgb_funcs;
	ret = rk618_output_register(&rgb->base);
	if (ret)
		return ret;

	return component_add(dev, &rk618_rgb_component_ops);
}

static int rk618_rgb_remove(struct platform_device *pdev)
{
	struct rk618_rgb *rgb = platform_get_drvdata(pdev);

	component_del(rgb->dev, &rk618_rgb_component_ops);

	return 0;
}

static struct platform_driver rk618_rgb_driver = {
	.driver = {
		.name = "rk618-rgb",
		.of_match_table = of_match_ptr(rk618_rgb_of_match),
	},
	.probe = rk618_rgb_probe,
	.remove = rk618_rgb_remove,
};
module_platform_driver(rk618_rgb_driver);

MODULE_AUTHOR("Chen Shunqing <csq@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip RK618 RGB driver");
MODULE_LICENSE("GPL v2");
