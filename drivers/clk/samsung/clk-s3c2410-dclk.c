/*
 * Copyright (c) 2013 Heiko Stuebner <heiko@sntech.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Common Clock Framework support for s3c24xx external clock output.
 */

#include <linux/clkdev.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include "clk.h"

/* legacy access to misccr, until dt conversion is finished */
#include <mach/hardware.h>
#include <mach/regs-gpio.h>

#define MUX_DCLK0	0
#define MUX_DCLK1	1
#define DIV_DCLK0	2
#define DIV_DCLK1	3
#define GATE_DCLK0	4
#define GATE_DCLK1	5
#define MUX_CLKOUT0	6
#define MUX_CLKOUT1	7
#define DCLK_MAX_CLKS	(MUX_CLKOUT1 + 1)

enum supported_socs {
	S3C2410,
	S3C2412,
	S3C2440,
	S3C2443,
};

struct s3c24xx_dclk_drv_data {
	const char **clkout0_parent_names;
	int clkout0_num_parents;
	const char **clkout1_parent_names;
	int clkout1_num_parents;
	const char **mux_parent_names;
	int mux_num_parents;
};

/*
 * Clock for output-parent selection in misccr
 */

struct s3c24xx_clkout {
	struct clk_hw		hw;
	u32			mask;
	u8			shift;
};

#define to_s3c24xx_clkout(_hw) container_of(_hw, struct s3c24xx_clkout, hw)

static u8 s3c24xx_clkout_get_parent(struct clk_hw *hw)
{
	struct s3c24xx_clkout *clkout = to_s3c24xx_clkout(hw);
	int num_parents = clk_hw_get_num_parents(hw);
	u32 val;

	val = readl_relaxed(S3C24XX_MISCCR) >> clkout->shift;
	val >>= clkout->shift;
	val &= clkout->mask;

	if (val >= num_parents)
		return -EINVAL;

	return val;
}

static int s3c24xx_clkout_set_parent(struct clk_hw *hw, u8 index)
{
	struct s3c24xx_clkout *clkout = to_s3c24xx_clkout(hw);

	s3c2410_modify_misccr((clkout->mask << clkout->shift),
			      (index << clkout->shift));

	return 0;
}

static const struct clk_ops s3c24xx_clkout_ops = {
	.get_parent = s3c24xx_clkout_get_parent,
	.set_parent = s3c24xx_clkout_set_parent,
	.determine_rate = __clk_mux_determine_rate,
};

static struct clk_hw *s3c24xx_register_clkout(struct device *dev,
		const char *name, const char **parent_names, u8 num_parents,
		u8 shift, u32 mask)
{
	struct s3c24xx_clkout *clkout;
	struct clk_init_data init;
	int ret;

	/* allocate the clkout */
	clkout = kzalloc(sizeof(*clkout), GFP_KERNEL);
	if (!clkout)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &s3c24xx_clkout_ops;
	init.flags = 0;
	init.parent_names = parent_names;
	init.num_parents = num_parents;

	clkout->shift = shift;
	clkout->mask = mask;
	clkout->hw.init = &init;

	ret = clk_hw_register(dev, &clkout->hw);
	if (ret)
		return ERR_PTR(ret);

	return &clkout->hw;
}

/*
 * dclk and clkout init
 */

struct s3c24xx_dclk {
	struct device *dev;
	void __iomem *base;
	struct notifier_block dclk0_div_change_nb;
	struct notifier_block dclk1_div_change_nb;
	spinlock_t dclk_lock;
	unsigned long reg_save;
	/* clk_data must be the last entry in the structure */
	struct clk_hw_onecell_data clk_data;
};

#define to_s3c24xx_dclk0(x) \
		container_of(x, struct s3c24xx_dclk, dclk0_div_change_nb)

#define to_s3c24xx_dclk1(x) \
		container_of(x, struct s3c24xx_dclk, dclk1_div_change_nb)

static const char *dclk_s3c2410_p[] = { "pclk", "uclk" };
static const char *clkout0_s3c2410_p[] = { "mpll", "upll", "fclk", "hclk", "pclk",
			     "gate_dclk0" };
static const char *clkout1_s3c2410_p[] = { "mpll", "upll", "fclk", "hclk", "pclk",
			     "gate_dclk1" };

static const char *clkout0_s3c2412_p[] = { "mpll", "upll", "rtc_clkout",
			     "hclk", "pclk", "gate_dclk0" };
static const char *clkout1_s3c2412_p[] = { "xti", "upll", "fclk", "hclk", "pclk",
			     "gate_dclk1" };

static const char *clkout0_s3c2440_p[] = { "xti", "upll", "fclk", "hclk", "pclk",
			     "gate_dclk0" };
static const char *clkout1_s3c2440_p[] = { "mpll", "upll", "rtc_clkout",
			     "hclk", "pclk", "gate_dclk1" };

static const char *dclk_s3c2443_p[] = { "pclk", "epll" };
static const char *clkout0_s3c2443_p[] = { "xti", "epll", "armclk", "hclk", "pclk",
			     "gate_dclk0" };
static const char *clkout1_s3c2443_p[] = { "dummy", "epll", "rtc_clkout",
			     "hclk", "pclk", "gate_dclk1" };

#define DCLKCON_DCLK_DIV_MASK		0xf
#define DCLKCON_DCLK0_DIV_SHIFT		4
#define DCLKCON_DCLK0_CMP_SHIFT		8
#define DCLKCON_DCLK1_DIV_SHIFT		20
#define DCLKCON_DCLK1_CMP_SHIFT		24

static void s3c24xx_dclk_update_cmp(struct s3c24xx_dclk *s3c24xx_dclk,
				    int div_shift, int cmp_shift)
{
	unsigned long flags = 0;
	u32 dclk_con, div, cmp;

	spin_lock_irqsave(&s3c24xx_dclk->dclk_lock, flags);

	dclk_con = readl_relaxed(s3c24xx_dclk->base);

	div = ((dclk_con >> div_shift) & DCLKCON_DCLK_DIV_MASK) + 1;
	cmp = ((div + 1) / 2) - 1;

	dclk_con &= ~(DCLKCON_DCLK_DIV_MASK << cmp_shift);
	dclk_con |= (cmp << cmp_shift);

	writel_relaxed(dclk_con, s3c24xx_dclk->base);

	spin_unlock_irqrestore(&s3c24xx_dclk->dclk_lock, flags);
}

static int s3c24xx_dclk0_div_notify(struct notifier_block *nb,
			       unsigned long event, void *data)
{
	struct s3c24xx_dclk *s3c24xx_dclk = to_s3c24xx_dclk0(nb);

	if (event == POST_RATE_CHANGE) {
		s3c24xx_dclk_update_cmp(s3c24xx_dclk,
			DCLKCON_DCLK0_DIV_SHIFT, DCLKCON_DCLK0_CMP_SHIFT);
	}

	return NOTIFY_DONE;
}

static int s3c24xx_dclk1_div_notify(struct notifier_block *nb,
			       unsigned long event, void *data)
{
	struct s3c24xx_dclk *s3c24xx_dclk = to_s3c24xx_dclk1(nb);

	if (event == POST_RATE_CHANGE) {
		s3c24xx_dclk_update_cmp(s3c24xx_dclk,
			DCLKCON_DCLK1_DIV_SHIFT, DCLKCON_DCLK1_CMP_SHIFT);
	}

	return NOTIFY_DONE;
}

#ifdef CONFIG_PM_SLEEP
static int s3c24xx_dclk_suspend(struct device *dev)
{
	struct s3c24xx_dclk *s3c24xx_dclk = dev_get_drvdata(dev);

	s3c24xx_dclk->reg_save = readl_relaxed(s3c24xx_dclk->base);
	return 0;
}

static int s3c24xx_dclk_resume(struct device *dev)
{
	struct s3c24xx_dclk *s3c24xx_dclk = dev_get_drvdata(dev);

	writel_relaxed(s3c24xx_dclk->reg_save, s3c24xx_dclk->base);
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(s3c24xx_dclk_pm_ops,
			 s3c24xx_dclk_suspend, s3c24xx_dclk_resume);

static int s3c24xx_dclk_probe(struct platform_device *pdev)
{
	struct s3c24xx_dclk *s3c24xx_dclk;
	struct resource *mem;
	struct s3c24xx_dclk_drv_data *dclk_variant;
	struct clk_hw **clk_table;
	int ret, i;

	s3c24xx_dclk = devm_kzalloc(&pdev->dev,
				    struct_size(s3c24xx_dclk, clk_data.hws,
						DCLK_MAX_CLKS),
				    GFP_KERNEL);
	if (!s3c24xx_dclk)
		return -ENOMEM;

	clk_table = s3c24xx_dclk->clk_data.hws;

	s3c24xx_dclk->dev = &pdev->dev;
	s3c24xx_dclk->clk_data.num = DCLK_MAX_CLKS;
	platform_set_drvdata(pdev, s3c24xx_dclk);
	spin_lock_init(&s3c24xx_dclk->dclk_lock);

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	s3c24xx_dclk->base = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(s3c24xx_dclk->base))
		return PTR_ERR(s3c24xx_dclk->base);

	dclk_variant = (struct s3c24xx_dclk_drv_data *)
				platform_get_device_id(pdev)->driver_data;


	clk_table[MUX_DCLK0] = clk_hw_register_mux(&pdev->dev, "mux_dclk0",
				dclk_variant->mux_parent_names,
				dclk_variant->mux_num_parents, 0,
				s3c24xx_dclk->base, 1, 1, 0,
				&s3c24xx_dclk->dclk_lock);
	clk_table[MUX_DCLK1] = clk_hw_register_mux(&pdev->dev, "mux_dclk1",
				dclk_variant->mux_parent_names,
				dclk_variant->mux_num_parents, 0,
				s3c24xx_dclk->base, 17, 1, 0,
				&s3c24xx_dclk->dclk_lock);

	clk_table[DIV_DCLK0] = clk_hw_register_divider(&pdev->dev, "div_dclk0",
				"mux_dclk0", 0, s3c24xx_dclk->base,
				4, 4, 0, &s3c24xx_dclk->dclk_lock);
	clk_table[DIV_DCLK1] = clk_hw_register_divider(&pdev->dev, "div_dclk1",
				"mux_dclk1", 0, s3c24xx_dclk->base,
				20, 4, 0, &s3c24xx_dclk->dclk_lock);

	clk_table[GATE_DCLK0] = clk_hw_register_gate(&pdev->dev, "gate_dclk0",
				"div_dclk0", CLK_SET_RATE_PARENT,
				s3c24xx_dclk->base, 0, 0,
				&s3c24xx_dclk->dclk_lock);
	clk_table[GATE_DCLK1] = clk_hw_register_gate(&pdev->dev, "gate_dclk1",
				"div_dclk1", CLK_SET_RATE_PARENT,
				s3c24xx_dclk->base, 16, 0,
				&s3c24xx_dclk->dclk_lock);

	clk_table[MUX_CLKOUT0] = s3c24xx_register_clkout(&pdev->dev,
				"clkout0", dclk_variant->clkout0_parent_names,
				dclk_variant->clkout0_num_parents, 4, 7);
	clk_table[MUX_CLKOUT1] = s3c24xx_register_clkout(&pdev->dev,
				"clkout1", dclk_variant->clkout1_parent_names,
				dclk_variant->clkout1_num_parents, 8, 7);

	for (i = 0; i < DCLK_MAX_CLKS; i++)
		if (IS_ERR(clk_table[i])) {
			dev_err(&pdev->dev, "clock %d failed to register\n", i);
			ret = PTR_ERR(clk_table[i]);
			goto err_clk_register;
		}

	ret = clk_hw_register_clkdev(clk_table[MUX_DCLK0], "dclk0", NULL);
	if (!ret)
		ret = clk_hw_register_clkdev(clk_table[MUX_DCLK1], "dclk1",
					     NULL);
	if (!ret)
		ret = clk_hw_register_clkdev(clk_table[MUX_CLKOUT0],
					     "clkout0", NULL);
	if (!ret)
		ret = clk_hw_register_clkdev(clk_table[MUX_CLKOUT1],
					     "clkout1", NULL);
	if (ret) {
		dev_err(&pdev->dev, "failed to register aliases, %d\n", ret);
		goto err_clk_register;
	}

	s3c24xx_dclk->dclk0_div_change_nb.notifier_call =
						s3c24xx_dclk0_div_notify;

	s3c24xx_dclk->dclk1_div_change_nb.notifier_call =
						s3c24xx_dclk1_div_notify;

	ret = clk_notifier_register(clk_table[DIV_DCLK0]->clk,
				    &s3c24xx_dclk->dclk0_div_change_nb);
	if (ret)
		goto err_clk_register;

	ret = clk_notifier_register(clk_table[DIV_DCLK1]->clk,
				    &s3c24xx_dclk->dclk1_div_change_nb);
	if (ret)
		goto err_dclk_notify;

	return 0;

err_dclk_notify:
	clk_notifier_unregister(clk_table[DIV_DCLK0]->clk,
				&s3c24xx_dclk->dclk0_div_change_nb);
err_clk_register:
	for (i = 0; i < DCLK_MAX_CLKS; i++)
		if (clk_table[i] && !IS_ERR(clk_table[i]))
			clk_hw_unregister(clk_table[i]);

	return ret;
}

static int s3c24xx_dclk_remove(struct platform_device *pdev)
{
	struct s3c24xx_dclk *s3c24xx_dclk = platform_get_drvdata(pdev);
	struct clk_hw **clk_table = s3c24xx_dclk->clk_data.hws;
	int i;

	clk_notifier_unregister(clk_table[DIV_DCLK1]->clk,
				&s3c24xx_dclk->dclk1_div_change_nb);
	clk_notifier_unregister(clk_table[DIV_DCLK0]->clk,
				&s3c24xx_dclk->dclk0_div_change_nb);

	for (i = 0; i < DCLK_MAX_CLKS; i++)
		clk_hw_unregister(clk_table[i]);

	return 0;
}

static struct s3c24xx_dclk_drv_data dclk_variants[] = {
	[S3C2410] = {
		.clkout0_parent_names = clkout0_s3c2410_p,
		.clkout0_num_parents = ARRAY_SIZE(clkout0_s3c2410_p),
		.clkout1_parent_names = clkout1_s3c2410_p,
		.clkout1_num_parents = ARRAY_SIZE(clkout1_s3c2410_p),
		.mux_parent_names = dclk_s3c2410_p,
		.mux_num_parents = ARRAY_SIZE(dclk_s3c2410_p),
	},
	[S3C2412] = {
		.clkout0_parent_names = clkout0_s3c2412_p,
		.clkout0_num_parents = ARRAY_SIZE(clkout0_s3c2412_p),
		.clkout1_parent_names = clkout1_s3c2412_p,
		.clkout1_num_parents = ARRAY_SIZE(clkout1_s3c2412_p),
		.mux_parent_names = dclk_s3c2410_p,
		.mux_num_parents = ARRAY_SIZE(dclk_s3c2410_p),
	},
	[S3C2440] = {
		.clkout0_parent_names = clkout0_s3c2440_p,
		.clkout0_num_parents = ARRAY_SIZE(clkout0_s3c2440_p),
		.clkout1_parent_names = clkout1_s3c2440_p,
		.clkout1_num_parents = ARRAY_SIZE(clkout1_s3c2440_p),
		.mux_parent_names = dclk_s3c2410_p,
		.mux_num_parents = ARRAY_SIZE(dclk_s3c2410_p),
	},
	[S3C2443] = {
		.clkout0_parent_names = clkout0_s3c2443_p,
		.clkout0_num_parents = ARRAY_SIZE(clkout0_s3c2443_p),
		.clkout1_parent_names = clkout1_s3c2443_p,
		.clkout1_num_parents = ARRAY_SIZE(clkout1_s3c2443_p),
		.mux_parent_names = dclk_s3c2443_p,
		.mux_num_parents = ARRAY_SIZE(dclk_s3c2443_p),
	},
};

static const struct platform_device_id s3c24xx_dclk_driver_ids[] = {
	{
		.name		= "s3c2410-dclk",
		.driver_data	= (kernel_ulong_t)&dclk_variants[S3C2410],
	}, {
		.name		= "s3c2412-dclk",
		.driver_data	= (kernel_ulong_t)&dclk_variants[S3C2412],
	}, {
		.name		= "s3c2440-dclk",
		.driver_data	= (kernel_ulong_t)&dclk_variants[S3C2440],
	}, {
		.name		= "s3c2443-dclk",
		.driver_data	= (kernel_ulong_t)&dclk_variants[S3C2443],
	},
	{ }
};

MODULE_DEVICE_TABLE(platform, s3c24xx_dclk_driver_ids);

static struct platform_driver s3c24xx_dclk_driver = {
	.driver = {
		.name			= "s3c24xx-dclk",
		.pm			= &s3c24xx_dclk_pm_ops,
		.suppress_bind_attrs	= true,
	},
	.probe = s3c24xx_dclk_probe,
	.remove = s3c24xx_dclk_remove,
	.id_table = s3c24xx_dclk_driver_ids,
};
module_platform_driver(s3c24xx_dclk_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Heiko Stuebner <heiko@sntech.de>");
MODULE_DESCRIPTION("Driver for the S3C24XX external clock outputs");
