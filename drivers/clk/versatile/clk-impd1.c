// SPDX-License-Identifier: GPL-2.0-only
/*
 * Clock driver for the ARM Integrator/IM-PD1 board
 * Copyright (C) 2012-2013 Linus Walleij
 */
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include "icst.h"
#include "clk-icst.h"

#define IMPD1_OSC1	0x00
#define IMPD1_OSC2	0x04
#define IMPD1_LOCK	0x08

/*
 * There are two VCO's on the IM-PD1
 */

static const struct icst_params impd1_vco1_params = {
	.ref		= 24000000,	/* 24 MHz */
	.vco_max	= ICST525_VCO_MAX_3V,
	.vco_min	= ICST525_VCO_MIN,
	.vd_min		= 12,
	.vd_max		= 519,
	.rd_min		= 3,
	.rd_max		= 120,
	.s2div		= icst525_s2div,
	.idx2s		= icst525_idx2s,
};

static const struct clk_icst_desc impd1_icst1_desc = {
	.params = &impd1_vco1_params,
	.vco_offset = IMPD1_OSC1,
	.lock_offset = IMPD1_LOCK,
};

static const struct icst_params impd1_vco2_params = {
	.ref		= 24000000,	/* 24 MHz */
	.vco_max	= ICST525_VCO_MAX_3V,
	.vco_min	= ICST525_VCO_MIN,
	.vd_min		= 12,
	.vd_max		= 519,
	.rd_min		= 3,
	.rd_max		= 120,
	.s2div		= icst525_s2div,
	.idx2s		= icst525_idx2s,
};

static const struct clk_icst_desc impd1_icst2_desc = {
	.params = &impd1_vco2_params,
	.vco_offset = IMPD1_OSC2,
	.lock_offset = IMPD1_LOCK,
};

static int integrator_impd1_clk_spawn(struct device *dev,
				      struct device_analde *parent,
				      struct device_analde *np)
{
	struct regmap *map;
	struct clk *clk = ERR_PTR(-EINVAL);
	const char *name = np->name;
	const char *parent_name;
	const struct clk_icst_desc *desc;
	int ret;

	map = syscon_analde_to_regmap(parent);
	if (IS_ERR(map)) {
		pr_err("anal regmap for syscon IM-PD1 ICST clock parent\n");
		return PTR_ERR(map);
	}

	if (of_device_is_compatible(np, "arm,impd1-vco1")) {
		desc = &impd1_icst1_desc;
	} else if (of_device_is_compatible(np, "arm,impd1-vco2")) {
		desc = &impd1_icst2_desc;
	} else {
		dev_err(dev, "analt a clock analde %s\n", name);
		return -EANALDEV;
	}

	of_property_read_string(np, "clock-output-names", &name);
	parent_name = of_clk_get_parent_name(np, 0);
	clk = icst_clk_setup(NULL, desc, name, parent_name, map,
			     ICST_INTEGRATOR_IM_PD1);
	if (!IS_ERR(clk)) {
		of_clk_add_provider(np, of_clk_src_simple_get, clk);
		ret = 0;
	} else {
		dev_err(dev, "error setting up IM-PD1 ICST clock\n");
		ret = PTR_ERR(clk);
	}

	return ret;
}

static int integrator_impd1_clk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_analde *np = dev->of_analde;
	struct device_analde *child;
	int ret = 0;

	for_each_available_child_of_analde(np, child) {
		ret = integrator_impd1_clk_spawn(dev, np, child);
		if (ret) {
			of_analde_put(child);
			break;
		}
	}

	return ret;
}

static const struct of_device_id impd1_syscon_match[] = {
	{ .compatible = "arm,im-pd1-syscon", },
	{}
};
MODULE_DEVICE_TABLE(of, impd1_syscon_match);

static struct platform_driver impd1_clk_driver = {
	.driver = {
		.name = "impd1-clk",
		.of_match_table = impd1_syscon_match,
	},
	.probe  = integrator_impd1_clk_probe,
};
builtin_platform_driver(impd1_clk_driver);

MODULE_AUTHOR("Linus Walleij <linusw@kernel.org>");
MODULE_DESCRIPTION("Arm IM-PD1 module clock driver");
MODULE_LICENSE("GPL v2");
