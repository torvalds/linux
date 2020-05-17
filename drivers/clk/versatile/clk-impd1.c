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
#include <linux/platform_data/clk-integrator.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include "icst.h"
#include "clk-icst.h"

#define IMPD1_OSC1	0x00
#define IMPD1_OSC2	0x04
#define IMPD1_LOCK	0x08

struct impd1_clk {
	char *pclkname;
	struct clk *pclk;
	char *vco1name;
	struct clk *vco1clk;
	char *vco2name;
	struct clk *vco2clk;
	struct clk *mmciclk;
	char *uartname;
	struct clk *uartclk;
	char *spiname;
	struct clk *spiclk;
	char *scname;
	struct clk *scclk;
	struct clk_lookup *clks[15];
};

/* One entry for each connected IM-PD1 LM */
static struct impd1_clk impd1_clks[4];

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

/**
 * integrator_impd1_clk_init() - set up the integrator clock tree
 * @base: base address of the logic module (LM)
 * @id: the ID of this LM
 */
void integrator_impd1_clk_init(void __iomem *base, unsigned int id)
{
	struct impd1_clk *imc;
	struct clk *clk;
	struct clk *pclk;
	int i;

	if (id > 3) {
		pr_crit("no more than 4 LMs can be attached\n");
		return;
	}
	imc = &impd1_clks[id];

	/* Register the fixed rate PCLK */
	imc->pclkname = kasprintf(GFP_KERNEL, "lm%x-pclk", id);
	pclk = clk_register_fixed_rate(NULL, imc->pclkname, NULL, 0, 0);
	imc->pclk = pclk;

	imc->vco1name = kasprintf(GFP_KERNEL, "lm%x-vco1", id);
	clk = icst_clk_register(NULL, &impd1_icst1_desc, imc->vco1name, NULL,
				base);
	imc->vco1clk = clk;
	imc->clks[0] = clkdev_alloc(pclk, "apb_pclk", "lm%x:01000", id);
	imc->clks[1] = clkdev_alloc(clk, NULL, "lm%x:01000", id);

	/* VCO2 is also called "CLK2" */
	imc->vco2name = kasprintf(GFP_KERNEL, "lm%x-vco2", id);
	clk = icst_clk_register(NULL, &impd1_icst2_desc, imc->vco2name, NULL,
				base);
	imc->vco2clk = clk;

	/* MMCI uses CLK2 right off */
	imc->clks[2] = clkdev_alloc(pclk, "apb_pclk", "lm%x:00700", id);
	imc->clks[3] = clkdev_alloc(clk, NULL, "lm%x:00700", id);

	/* UART reference clock divides CLK2 by a fixed factor 4 */
	imc->uartname = kasprintf(GFP_KERNEL, "lm%x-uartclk", id);
	clk = clk_register_fixed_factor(NULL, imc->uartname, imc->vco2name,
				   CLK_IGNORE_UNUSED, 1, 4);
	imc->uartclk = clk;
	imc->clks[4] = clkdev_alloc(pclk, "apb_pclk", "lm%x:00100", id);
	imc->clks[5] = clkdev_alloc(clk, NULL, "lm%x:00100", id);
	imc->clks[6] = clkdev_alloc(pclk, "apb_pclk", "lm%x:00200", id);
	imc->clks[7] = clkdev_alloc(clk, NULL, "lm%x:00200", id);

	/* SPI PL022 clock divides CLK2 by a fixed factor 64 */
	imc->spiname = kasprintf(GFP_KERNEL, "lm%x-spiclk", id);
	clk = clk_register_fixed_factor(NULL, imc->spiname, imc->vco2name,
				   CLK_IGNORE_UNUSED, 1, 64);
	imc->clks[8] = clkdev_alloc(pclk, "apb_pclk", "lm%x:00300", id);
	imc->clks[9] = clkdev_alloc(clk, NULL, "lm%x:00300", id);

	/* The GPIO blocks and AACI have only PCLK */
	imc->clks[10] = clkdev_alloc(pclk, "apb_pclk", "lm%x:00400", id);
	imc->clks[11] = clkdev_alloc(pclk, "apb_pclk", "lm%x:00500", id);
	imc->clks[12] = clkdev_alloc(pclk, "apb_pclk", "lm%x:00800", id);

	/* Smart Card clock divides CLK2 by a fixed factor 4 */
	imc->scname = kasprintf(GFP_KERNEL, "lm%x-scclk", id);
	clk = clk_register_fixed_factor(NULL, imc->scname, imc->vco2name,
				   CLK_IGNORE_UNUSED, 1, 4);
	imc->scclk = clk;
	imc->clks[13] = clkdev_alloc(pclk, "apb_pclk", "lm%x:00600", id);
	imc->clks[14] = clkdev_alloc(clk, NULL, "lm%x:00600", id);

	for (i = 0; i < ARRAY_SIZE(imc->clks); i++)
		clkdev_add(imc->clks[i]);
}
EXPORT_SYMBOL_GPL(integrator_impd1_clk_init);

void integrator_impd1_clk_exit(unsigned int id)
{
	int i;
	struct impd1_clk *imc;

	if (id > 3)
		return;
	imc = &impd1_clks[id];

	for (i = 0; i < ARRAY_SIZE(imc->clks); i++)
		clkdev_drop(imc->clks[i]);
	clk_unregister(imc->spiclk);
	clk_unregister(imc->uartclk);
	clk_unregister(imc->vco2clk);
	clk_unregister(imc->vco1clk);
	clk_unregister(imc->pclk);
	kfree(imc->scname);
	kfree(imc->spiname);
	kfree(imc->uartname);
	kfree(imc->vco2name);
	kfree(imc->vco1name);
	kfree(imc->pclkname);
}
EXPORT_SYMBOL_GPL(integrator_impd1_clk_exit);

static int integrator_impd1_clk_spawn(struct device *dev,
				      struct device_node *parent,
				      struct device_node *np)
{
	struct regmap *map;
	struct clk *clk = ERR_PTR(-EINVAL);
	const char *name = np->name;
	const char *parent_name;
	const struct clk_icst_desc *desc;
	int ret;

	map = syscon_node_to_regmap(parent);
	if (IS_ERR(map)) {
		pr_err("no regmap for syscon IM-PD1 ICST clock parent\n");
		return PTR_ERR(map);
	}

	if (of_device_is_compatible(np, "arm,impd1-vco1")) {
		desc = &impd1_icst1_desc;
	} else if (of_device_is_compatible(np, "arm,impd1-vco2")) {
		desc = &impd1_icst2_desc;
	} else {
		dev_err(dev, "not a clock node %s\n", name);
		return -ENODEV;
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
	struct device_node *np = dev->of_node;
	struct device_node *child;
	int ret = 0;

	for_each_available_child_of_node(np, child) {
		ret = integrator_impd1_clk_spawn(dev, np, child);
		if (ret)
			break;
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
