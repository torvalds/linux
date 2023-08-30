// SPDX-License-Identifier: GPL-2.0-only
/*
 * drivers/clk/clk-axm5516.c
 *
 * Provides clock implementations for three different types of clock devices on
 * the Axxia device: PLL clock, a clock divider and a clock mux.
 *
 * Copyright (C) 2014 LSI Corporation
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/clk-provider.h>
#include <linux/regmap.h>
#include <dt-bindings/clock/lsi,axm5516-clks.h>


/**
 * struct axxia_clk - Common struct to all Axxia clocks.
 * @hw: clk_hw for the common clk framework
 * @regmap: Regmap for the clock control registers
 */
struct axxia_clk {
	struct clk_hw hw;
	struct regmap *regmap;
};
#define to_axxia_clk(_hw) container_of(_hw, struct axxia_clk, hw)

/**
 * struct axxia_pllclk - Axxia PLL generated clock.
 * @aclk: Common struct
 * @reg: Offset into regmap for PLL control register
 */
struct axxia_pllclk {
	struct axxia_clk aclk;
	u32 reg;
};
#define to_axxia_pllclk(_aclk) container_of(_aclk, struct axxia_pllclk, aclk)

/**
 * axxia_pllclk_recalc - Calculate the PLL generated clock rate given the
 * parent clock rate.
 */
static unsigned long
axxia_pllclk_recalc(struct clk_hw *hw, unsigned long parent_rate)
{
	struct axxia_clk *aclk = to_axxia_clk(hw);
	struct axxia_pllclk *pll = to_axxia_pllclk(aclk);
	unsigned long rate, fbdiv, refdiv, postdiv;
	u32 control;

	regmap_read(aclk->regmap, pll->reg, &control);
	postdiv = ((control >> 0) & 0xf) + 1;
	fbdiv   = ((control >> 4) & 0xfff) + 3;
	refdiv  = ((control >> 16) & 0x1f) + 1;
	rate = (parent_rate / (refdiv * postdiv)) * fbdiv;

	return rate;
}

static const struct clk_ops axxia_pllclk_ops = {
	.recalc_rate = axxia_pllclk_recalc,
};

/**
 * struct axxia_divclk - Axxia clock divider
 * @aclk: Common struct
 * @reg: Offset into regmap for PLL control register
 * @shift: Bit position for divider value
 * @width: Number of bits in divider value
 */
struct axxia_divclk {
	struct axxia_clk aclk;
	u32 reg;
	u32 shift;
	u32 width;
};
#define to_axxia_divclk(_aclk) container_of(_aclk, struct axxia_divclk, aclk)

/**
 * axxia_divclk_recalc_rate - Calculate clock divider output rage
 */
static unsigned long
axxia_divclk_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct axxia_clk *aclk = to_axxia_clk(hw);
	struct axxia_divclk *divclk = to_axxia_divclk(aclk);
	u32 ctrl, div;

	regmap_read(aclk->regmap, divclk->reg, &ctrl);
	div = 1 + ((ctrl >> divclk->shift) & ((1 << divclk->width)-1));

	return parent_rate / div;
}

static const struct clk_ops axxia_divclk_ops = {
	.recalc_rate = axxia_divclk_recalc_rate,
};

/**
 * struct axxia_clkmux - Axxia clock mux
 * @aclk: Common struct
 * @reg: Offset into regmap for PLL control register
 * @shift: Bit position for selection value
 * @width: Number of bits in selection value
 */
struct axxia_clkmux {
	struct axxia_clk aclk;
	u32 reg;
	u32 shift;
	u32 width;
};
#define to_axxia_clkmux(_aclk) container_of(_aclk, struct axxia_clkmux, aclk)

/**
 * axxia_clkmux_get_parent - Return the index of selected parent clock
 */
static u8 axxia_clkmux_get_parent(struct clk_hw *hw)
{
	struct axxia_clk *aclk = to_axxia_clk(hw);
	struct axxia_clkmux *mux = to_axxia_clkmux(aclk);
	u32 ctrl, parent;

	regmap_read(aclk->regmap, mux->reg, &ctrl);
	parent = (ctrl >> mux->shift) & ((1 << mux->width) - 1);

	return (u8) parent;
}

static const struct clk_ops axxia_clkmux_ops = {
	.get_parent = axxia_clkmux_get_parent,
};


/*
 * PLLs
 */

static struct axxia_pllclk clk_fab_pll = {
	.aclk.hw.init = &(struct clk_init_data){
		.name = "clk_fab_pll",
		.parent_names = (const char *[]){
			"clk_ref0"
		},
		.num_parents = 1,
		.ops = &axxia_pllclk_ops,
	},
	.reg   = 0x01800,
};

static struct axxia_pllclk clk_cpu_pll = {
	.aclk.hw.init = &(struct clk_init_data){
		.name = "clk_cpu_pll",
		.parent_names = (const char *[]){
			"clk_ref0"
		},
		.num_parents = 1,
		.ops = &axxia_pllclk_ops,
	},
	.reg   = 0x02000,
};

static struct axxia_pllclk clk_sys_pll = {
	.aclk.hw.init = &(struct clk_init_data){
		.name = "clk_sys_pll",
		.parent_names = (const char *[]){
			"clk_ref0"
		},
		.num_parents = 1,
		.ops = &axxia_pllclk_ops,
	},
	.reg   = 0x02800,
};

static struct axxia_pllclk clk_sm0_pll = {
	.aclk.hw.init = &(struct clk_init_data){
		.name = "clk_sm0_pll",
		.parent_names = (const char *[]){
			"clk_ref2"
		},
		.num_parents = 1,
		.ops = &axxia_pllclk_ops,
	},
	.reg   = 0x03000,
};

static struct axxia_pllclk clk_sm1_pll = {
	.aclk.hw.init = &(struct clk_init_data){
		.name = "clk_sm1_pll",
		.parent_names = (const char *[]){
			"clk_ref1"
		},
		.num_parents = 1,
		.ops = &axxia_pllclk_ops,
	},
	.reg   = 0x03800,
};

/*
 * Clock dividers
 */

static struct axxia_divclk clk_cpu0_div = {
	.aclk.hw.init = &(struct clk_init_data){
		.name = "clk_cpu0_div",
		.parent_names = (const char *[]){
			"clk_cpu_pll"
		},
		.num_parents = 1,
		.ops = &axxia_divclk_ops,
	},
	.reg   = 0x10008,
	.shift = 0,
	.width = 4,
};

static struct axxia_divclk clk_cpu1_div = {
	.aclk.hw.init = &(struct clk_init_data){
		.name = "clk_cpu1_div",
		.parent_names = (const char *[]){
			"clk_cpu_pll"
		},
		.num_parents = 1,
		.ops = &axxia_divclk_ops,
	},
	.reg   = 0x10008,
	.shift = 4,
	.width = 4,
};

static struct axxia_divclk clk_cpu2_div = {
	.aclk.hw.init = &(struct clk_init_data){
		.name = "clk_cpu2_div",
		.parent_names = (const char *[]){
			"clk_cpu_pll"
		},
		.num_parents = 1,
		.ops = &axxia_divclk_ops,
	},
	.reg   = 0x10008,
	.shift = 8,
	.width = 4,
};

static struct axxia_divclk clk_cpu3_div = {
	.aclk.hw.init = &(struct clk_init_data){
		.name = "clk_cpu3_div",
		.parent_names = (const char *[]){
			"clk_cpu_pll"
		},
		.num_parents = 1,
		.ops = &axxia_divclk_ops,
	},
	.reg   = 0x10008,
	.shift = 12,
	.width = 4,
};

static struct axxia_divclk clk_nrcp_div = {
	.aclk.hw.init = &(struct clk_init_data){
		.name = "clk_nrcp_div",
		.parent_names = (const char *[]){
			"clk_sys_pll"
		},
		.num_parents = 1,
		.ops = &axxia_divclk_ops,
	},
	.reg   = 0x1000c,
	.shift = 0,
	.width = 4,
};

static struct axxia_divclk clk_sys_div = {
	.aclk.hw.init = &(struct clk_init_data){
		.name = "clk_sys_div",
		.parent_names = (const char *[]){
			"clk_sys_pll"
		},
		.num_parents = 1,
		.ops = &axxia_divclk_ops,
	},
	.reg   = 0x1000c,
	.shift = 4,
	.width = 4,
};

static struct axxia_divclk clk_fab_div = {
	.aclk.hw.init = &(struct clk_init_data){
		.name = "clk_fab_div",
		.parent_names = (const char *[]){
			"clk_fab_pll"
		},
		.num_parents = 1,
		.ops = &axxia_divclk_ops,
	},
	.reg   = 0x1000c,
	.shift = 8,
	.width = 4,
};

static struct axxia_divclk clk_per_div = {
	.aclk.hw.init = &(struct clk_init_data){
		.name = "clk_per_div",
		.parent_names = (const char *[]){
			"clk_sm1_pll"
		},
		.num_parents = 1,
		.ops = &axxia_divclk_ops,
	},
	.reg   = 0x1000c,
	.shift = 12,
	.width = 4,
};

static struct axxia_divclk clk_mmc_div = {
	.aclk.hw.init = &(struct clk_init_data){
		.name = "clk_mmc_div",
		.parent_names = (const char *[]){
			"clk_sm1_pll"
		},
		.num_parents = 1,
		.ops = &axxia_divclk_ops,
	},
	.reg   = 0x1000c,
	.shift = 16,
	.width = 4,
};

/*
 * Clock MUXes
 */

static struct axxia_clkmux clk_cpu0_mux = {
	.aclk.hw.init = &(struct clk_init_data){
		.name = "clk_cpu0",
		.parent_names = (const char *[]){
			"clk_ref0",
			"clk_cpu_pll",
			"clk_cpu0_div",
			"clk_cpu0_div"
		},
		.num_parents = 4,
		.ops = &axxia_clkmux_ops,
	},
	.reg   = 0x10000,
	.shift = 0,
	.width = 2,
};

static struct axxia_clkmux clk_cpu1_mux = {
	.aclk.hw.init = &(struct clk_init_data){
		.name = "clk_cpu1",
		.parent_names = (const char *[]){
			"clk_ref0",
			"clk_cpu_pll",
			"clk_cpu1_div",
			"clk_cpu1_div"
		},
		.num_parents = 4,
		.ops = &axxia_clkmux_ops,
	},
	.reg   = 0x10000,
	.shift = 2,
	.width = 2,
};

static struct axxia_clkmux clk_cpu2_mux = {
	.aclk.hw.init = &(struct clk_init_data){
		.name = "clk_cpu2",
		.parent_names = (const char *[]){
			"clk_ref0",
			"clk_cpu_pll",
			"clk_cpu2_div",
			"clk_cpu2_div"
		},
		.num_parents = 4,
		.ops = &axxia_clkmux_ops,
	},
	.reg   = 0x10000,
	.shift = 4,
	.width = 2,
};

static struct axxia_clkmux clk_cpu3_mux = {
	.aclk.hw.init = &(struct clk_init_data){
		.name = "clk_cpu3",
		.parent_names = (const char *[]){
			"clk_ref0",
			"clk_cpu_pll",
			"clk_cpu3_div",
			"clk_cpu3_div"
		},
		.num_parents = 4,
		.ops = &axxia_clkmux_ops,
	},
	.reg   = 0x10000,
	.shift = 6,
	.width = 2,
};

static struct axxia_clkmux clk_nrcp_mux = {
	.aclk.hw.init = &(struct clk_init_data){
		.name = "clk_nrcp",
		.parent_names = (const char *[]){
			"clk_ref0",
			"clk_sys_pll",
			"clk_nrcp_div",
			"clk_nrcp_div"
		},
		.num_parents = 4,
		.ops = &axxia_clkmux_ops,
	},
	.reg   = 0x10004,
	.shift = 0,
	.width = 2,
};

static struct axxia_clkmux clk_sys_mux = {
	.aclk.hw.init = &(struct clk_init_data){
		.name = "clk_sys",
		.parent_names = (const char *[]){
			"clk_ref0",
			"clk_sys_pll",
			"clk_sys_div",
			"clk_sys_div"
		},
		.num_parents = 4,
		.ops = &axxia_clkmux_ops,
	},
	.reg   = 0x10004,
	.shift = 2,
	.width = 2,
};

static struct axxia_clkmux clk_fab_mux = {
	.aclk.hw.init = &(struct clk_init_data){
		.name = "clk_fab",
		.parent_names = (const char *[]){
			"clk_ref0",
			"clk_fab_pll",
			"clk_fab_div",
			"clk_fab_div"
		},
		.num_parents = 4,
		.ops = &axxia_clkmux_ops,
	},
	.reg   = 0x10004,
	.shift = 4,
	.width = 2,
};

static struct axxia_clkmux clk_per_mux = {
	.aclk.hw.init = &(struct clk_init_data){
		.name = "clk_per",
		.parent_names = (const char *[]){
			"clk_ref1",
			"clk_per_div"
		},
		.num_parents = 2,
		.ops = &axxia_clkmux_ops,
	},
	.reg   = 0x10004,
	.shift = 6,
	.width = 1,
};

static struct axxia_clkmux clk_mmc_mux = {
	.aclk.hw.init = &(struct clk_init_data){
		.name = "clk_mmc",
		.parent_names = (const char *[]){
			"clk_ref1",
			"clk_mmc_div"
		},
		.num_parents = 2,
		.ops = &axxia_clkmux_ops,
	},
	.reg   = 0x10004,
	.shift = 9,
	.width = 1,
};

/* Table of all supported clocks indexed by the clock identifiers from the
 * device tree binding
 */
static struct axxia_clk *axmclk_clocks[] = {
	[AXXIA_CLK_FAB_PLL]  = &clk_fab_pll.aclk,
	[AXXIA_CLK_CPU_PLL]  = &clk_cpu_pll.aclk,
	[AXXIA_CLK_SYS_PLL]  = &clk_sys_pll.aclk,
	[AXXIA_CLK_SM0_PLL]  = &clk_sm0_pll.aclk,
	[AXXIA_CLK_SM1_PLL]  = &clk_sm1_pll.aclk,
	[AXXIA_CLK_FAB_DIV]  = &clk_fab_div.aclk,
	[AXXIA_CLK_SYS_DIV]  = &clk_sys_div.aclk,
	[AXXIA_CLK_NRCP_DIV] = &clk_nrcp_div.aclk,
	[AXXIA_CLK_CPU0_DIV] = &clk_cpu0_div.aclk,
	[AXXIA_CLK_CPU1_DIV] = &clk_cpu1_div.aclk,
	[AXXIA_CLK_CPU2_DIV] = &clk_cpu2_div.aclk,
	[AXXIA_CLK_CPU3_DIV] = &clk_cpu3_div.aclk,
	[AXXIA_CLK_PER_DIV]  = &clk_per_div.aclk,
	[AXXIA_CLK_MMC_DIV]  = &clk_mmc_div.aclk,
	[AXXIA_CLK_FAB]      = &clk_fab_mux.aclk,
	[AXXIA_CLK_SYS]      = &clk_sys_mux.aclk,
	[AXXIA_CLK_NRCP]     = &clk_nrcp_mux.aclk,
	[AXXIA_CLK_CPU0]     = &clk_cpu0_mux.aclk,
	[AXXIA_CLK_CPU1]     = &clk_cpu1_mux.aclk,
	[AXXIA_CLK_CPU2]     = &clk_cpu2_mux.aclk,
	[AXXIA_CLK_CPU3]     = &clk_cpu3_mux.aclk,
	[AXXIA_CLK_PER]      = &clk_per_mux.aclk,
	[AXXIA_CLK_MMC]      = &clk_mmc_mux.aclk,
};

static struct clk_hw *
of_clk_axmclk_get(struct of_phandle_args *clkspec, void *unused)
{
	unsigned int idx = clkspec->args[0];

	if (idx >= ARRAY_SIZE(axmclk_clocks)) {
		pr_err("%s: invalid index %u\n", __func__, idx);
		return ERR_PTR(-EINVAL);
	}

	return &axmclk_clocks[idx]->hw;
}

static const struct regmap_config axmclk_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x1fffc,
	.fast_io	= true,
};

static const struct of_device_id axmclk_match_table[] = {
	{ .compatible = "lsi,axm5516-clks" },
	{ }
};
MODULE_DEVICE_TABLE(of, axmclk_match_table);

static int axmclk_probe(struct platform_device *pdev)
{
	void __iomem *base;
	int i, ret;
	struct device *dev = &pdev->dev;
	struct regmap *regmap;
	size_t num_clks;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	regmap = devm_regmap_init_mmio(dev, base, &axmclk_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	num_clks = ARRAY_SIZE(axmclk_clocks);
	pr_info("axmclk: supporting %zu clocks\n", num_clks);

	/* Update each entry with the allocated regmap and register the clock
	 * with the common clock framework
	 */
	for (i = 0; i < num_clks; i++) {
		axmclk_clocks[i]->regmap = regmap;
		ret = devm_clk_hw_register(dev, &axmclk_clocks[i]->hw);
		if (ret)
			return ret;
	}

	return devm_of_clk_add_hw_provider(dev, of_clk_axmclk_get, NULL);
}

static struct platform_driver axmclk_driver = {
	.probe		= axmclk_probe,
	.driver		= {
		.name	= "clk-axm5516",
		.of_match_table = axmclk_match_table,
	},
};

static int __init axmclk_init(void)
{
	return platform_driver_register(&axmclk_driver);
}
core_initcall(axmclk_init);

static void __exit axmclk_exit(void)
{
	platform_driver_unregister(&axmclk_driver);
}
module_exit(axmclk_exit);

MODULE_DESCRIPTION("AXM5516 clock driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:clk-axm5516");
