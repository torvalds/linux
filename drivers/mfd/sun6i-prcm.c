// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 Free Electrons
 *
 * Author: Boris BREZILLON <boris.brezillon@free-electrons.com>
 *
 * Allwinner PRCM (Power/Reset/Clock Management) driver
 */

#include <linux/mfd/core.h>
#include <linux/init.h>
#include <linux/of.h>

#define SUN8I_CODEC_ANALOG_BASE	0x1c0
#define SUN8I_CODEC_ANALOG_SIZE	0x4

struct prcm_data {
	int nsubdevs;
	const struct mfd_cell *subdevs;
};

static const struct resource sun6i_a31_ar100_clk_res[] = {
	DEFINE_RES_MEM(0x0, 4)
};

static const struct resource sun6i_a31_apb0_clk_res[] = {
	DEFINE_RES_MEM(0xc, 4)
};

static const struct resource sun6i_a31_apb0_gates_clk_res[] = {
	DEFINE_RES_MEM(0x28, 4)
};

static const struct resource sun6i_a31_ir_clk_res[] = {
	DEFINE_RES_MEM(0x54, 4)
};

static const struct resource sun6i_a31_apb0_rstc_res[] = {
	DEFINE_RES_MEM(0xb0, 4)
};

static const struct resource sun8i_codec_analog_res[] = {
	DEFINE_RES_MEM(SUN8I_CODEC_ANALOG_BASE, SUN8I_CODEC_ANALOG_SIZE),
};

static const struct mfd_cell sun6i_a31_prcm_subdevs[] = {
	{
		.name = "sun6i-a31-ar100-clk",
		.of_compatible = "allwinner,sun6i-a31-ar100-clk",
		.num_resources = ARRAY_SIZE(sun6i_a31_ar100_clk_res),
		.resources = sun6i_a31_ar100_clk_res,
	},
	{
		.name = "sun6i-a31-apb0-clk",
		.of_compatible = "allwinner,sun6i-a31-apb0-clk",
		.num_resources = ARRAY_SIZE(sun6i_a31_apb0_clk_res),
		.resources = sun6i_a31_apb0_clk_res,
	},
	{
		.name = "sun6i-a31-apb0-gates-clk",
		.of_compatible = "allwinner,sun6i-a31-apb0-gates-clk",
		.num_resources = ARRAY_SIZE(sun6i_a31_apb0_gates_clk_res),
		.resources = sun6i_a31_apb0_gates_clk_res,
	},
	{
		.name = "sun6i-a31-ir-clk",
		.of_compatible = "allwinner,sun4i-a10-mod0-clk",
		.num_resources = ARRAY_SIZE(sun6i_a31_ir_clk_res),
		.resources = sun6i_a31_ir_clk_res,
	},
	{
		.name = "sun6i-a31-apb0-clock-reset",
		.of_compatible = "allwinner,sun6i-a31-clock-reset",
		.num_resources = ARRAY_SIZE(sun6i_a31_apb0_rstc_res),
		.resources = sun6i_a31_apb0_rstc_res,
	},
};

static const struct mfd_cell sun8i_a23_prcm_subdevs[] = {
	{
		.name = "sun8i-a23-apb0-clk",
		.of_compatible = "allwinner,sun8i-a23-apb0-clk",
		.num_resources = ARRAY_SIZE(sun6i_a31_apb0_clk_res),
		.resources = sun6i_a31_apb0_clk_res,
	},
	{
		.name = "sun6i-a31-apb0-gates-clk",
		.of_compatible = "allwinner,sun8i-a23-apb0-gates-clk",
		.num_resources = ARRAY_SIZE(sun6i_a31_apb0_gates_clk_res),
		.resources = sun6i_a31_apb0_gates_clk_res,
	},
	{
		.name = "sun6i-a31-apb0-clock-reset",
		.of_compatible = "allwinner,sun6i-a31-clock-reset",
		.num_resources = ARRAY_SIZE(sun6i_a31_apb0_rstc_res),
		.resources = sun6i_a31_apb0_rstc_res,
	},
	{
		.name		= "sun8i-codec-analog",
		.of_compatible	= "allwinner,sun8i-a23-codec-analog",
		.num_resources	= ARRAY_SIZE(sun8i_codec_analog_res),
		.resources	= sun8i_codec_analog_res,
	},
};

static const struct prcm_data sun6i_a31_prcm_data = {
	.nsubdevs = ARRAY_SIZE(sun6i_a31_prcm_subdevs),
	.subdevs = sun6i_a31_prcm_subdevs,
};

static const struct prcm_data sun8i_a23_prcm_data = {
	.nsubdevs = ARRAY_SIZE(sun8i_a23_prcm_subdevs),
	.subdevs = sun8i_a23_prcm_subdevs,
};

static const struct of_device_id sun6i_prcm_dt_ids[] = {
	{
		.compatible = "allwinner,sun6i-a31-prcm",
		.data = &sun6i_a31_prcm_data,
	},
	{
		.compatible = "allwinner,sun8i-a23-prcm",
		.data = &sun8i_a23_prcm_data,
	},
	{ /* sentinel */ },
};

static int sun6i_prcm_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	const struct prcm_data *data;
	struct resource *res;
	int ret;

	match = of_match_node(sun6i_prcm_dt_ids, pdev->dev.of_node);
	if (!match)
		return -EINVAL;

	data = match->data;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "no prcm memory region provided\n");
		return -ENOENT;
	}

	ret = mfd_add_devices(&pdev->dev, 0, data->subdevs, data->nsubdevs,
			      res, -1, NULL);
	if (ret) {
		dev_err(&pdev->dev, "failed to add subdevices\n");
		return ret;
	}

	return 0;
}

static struct platform_driver sun6i_prcm_driver = {
	.driver = {
		.name = "sun6i-prcm",
		.of_match_table = sun6i_prcm_dt_ids,
	},
	.probe = sun6i_prcm_probe,
};
builtin_platform_driver(sun6i_prcm_driver);
