/*
 *  Port on Texas Instruments TMS320C6x architecture
 *
 *  Copyright (C) 2011 Texas Instruments Incorporated
 *  Author: Mark Salter <msalter@redhat.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/clkdev.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <asm/clock.h>
#include <asm/setup.h>
#include <asm/irq.h>

/*
 * Common SoC clock support.
 */

/* Default input for PLL1 */
struct clk clkin1 = {
	.name = "clkin1",
	.node = LIST_HEAD_INIT(clkin1.node),
	.children = LIST_HEAD_INIT(clkin1.children),
	.childnode = LIST_HEAD_INIT(clkin1.childnode),
};

struct pll_data c6x_soc_pll1 = {
	.num	   = 1,
	.sysclks   = {
		{
			.name = "pll1",
			.parent = &clkin1,
			.pll_data = &c6x_soc_pll1,
			.flags = CLK_PLL,
		},
		{
			.name = "pll1_sysclk1",
			.parent = &c6x_soc_pll1.sysclks[0],
			.flags = CLK_PLL,
		},
		{
			.name = "pll1_sysclk2",
			.parent = &c6x_soc_pll1.sysclks[0],
			.flags = CLK_PLL,
		},
		{
			.name = "pll1_sysclk3",
			.parent = &c6x_soc_pll1.sysclks[0],
			.flags = CLK_PLL,
		},
		{
			.name = "pll1_sysclk4",
			.parent = &c6x_soc_pll1.sysclks[0],
			.flags = CLK_PLL,
		},
		{
			.name = "pll1_sysclk5",
			.parent = &c6x_soc_pll1.sysclks[0],
			.flags = CLK_PLL,
		},
		{
			.name = "pll1_sysclk6",
			.parent = &c6x_soc_pll1.sysclks[0],
			.flags = CLK_PLL,
		},
		{
			.name = "pll1_sysclk7",
			.parent = &c6x_soc_pll1.sysclks[0],
			.flags = CLK_PLL,
		},
		{
			.name = "pll1_sysclk8",
			.parent = &c6x_soc_pll1.sysclks[0],
			.flags = CLK_PLL,
		},
		{
			.name = "pll1_sysclk9",
			.parent = &c6x_soc_pll1.sysclks[0],
			.flags = CLK_PLL,
		},
		{
			.name = "pll1_sysclk10",
			.parent = &c6x_soc_pll1.sysclks[0],
			.flags = CLK_PLL,
		},
		{
			.name = "pll1_sysclk11",
			.parent = &c6x_soc_pll1.sysclks[0],
			.flags = CLK_PLL,
		},
		{
			.name = "pll1_sysclk12",
			.parent = &c6x_soc_pll1.sysclks[0],
			.flags = CLK_PLL,
		},
		{
			.name = "pll1_sysclk13",
			.parent = &c6x_soc_pll1.sysclks[0],
			.flags = CLK_PLL,
		},
		{
			.name = "pll1_sysclk14",
			.parent = &c6x_soc_pll1.sysclks[0],
			.flags = CLK_PLL,
		},
		{
			.name = "pll1_sysclk15",
			.parent = &c6x_soc_pll1.sysclks[0],
			.flags = CLK_PLL,
		},
		{
			.name = "pll1_sysclk16",
			.parent = &c6x_soc_pll1.sysclks[0],
			.flags = CLK_PLL,
		},
	},
};

/* CPU core clock */
struct clk c6x_core_clk = {
	.name = "core",
};

/* miscellaneous IO clocks */
struct clk c6x_i2c_clk = {
	.name = "i2c",
};

struct clk c6x_watchdog_clk = {
	.name = "watchdog",
};

struct clk c6x_mcbsp1_clk = {
	.name = "mcbsp1",
};

struct clk c6x_mcbsp2_clk = {
	.name = "mcbsp2",
};

struct clk c6x_mdio_clk = {
	.name = "mdio",
};


#ifdef CONFIG_SOC_TMS320C6455
static struct clk_lookup c6455_clks[] = {
	CLK(NULL, "pll1", &c6x_soc_pll1.sysclks[0]),
	CLK(NULL, "pll1_sysclk2", &c6x_soc_pll1.sysclks[2]),
	CLK(NULL, "pll1_sysclk3", &c6x_soc_pll1.sysclks[3]),
	CLK(NULL, "pll1_sysclk4", &c6x_soc_pll1.sysclks[4]),
	CLK(NULL, "pll1_sysclk5", &c6x_soc_pll1.sysclks[5]),
	CLK(NULL, "core", &c6x_core_clk),
	CLK("i2c_davinci.1", NULL, &c6x_i2c_clk),
	CLK("watchdog", NULL, &c6x_watchdog_clk),
	CLK("2c81800.mdio", NULL, &c6x_mdio_clk),
	CLK("", NULL, NULL)
};


static void __init c6455_setup_clocks(struct device_node *node)
{
	struct pll_data *pll = &c6x_soc_pll1;
	struct clk *sysclks = pll->sysclks;

	pll->flags = PLL_HAS_PRE | PLL_HAS_MUL;

	sysclks[2].flags |= FIXED_DIV_PLL;
	sysclks[2].div = 3;
	sysclks[3].flags |= FIXED_DIV_PLL;
	sysclks[3].div = 6;
	sysclks[4].div = PLLDIV4;
	sysclks[5].div = PLLDIV5;

	c6x_core_clk.parent = &sysclks[0];
	c6x_i2c_clk.parent = &sysclks[3];
	c6x_watchdog_clk.parent = &sysclks[3];
	c6x_mdio_clk.parent = &sysclks[3];

	c6x_clks_init(c6455_clks);
}
#endif /* CONFIG_SOC_TMS320C6455 */

#ifdef CONFIG_SOC_TMS320C6457
static struct clk_lookup c6457_clks[] = {
	CLK(NULL, "pll1", &c6x_soc_pll1.sysclks[0]),
	CLK(NULL, "pll1_sysclk1", &c6x_soc_pll1.sysclks[1]),
	CLK(NULL, "pll1_sysclk2", &c6x_soc_pll1.sysclks[2]),
	CLK(NULL, "pll1_sysclk3", &c6x_soc_pll1.sysclks[3]),
	CLK(NULL, "pll1_sysclk4", &c6x_soc_pll1.sysclks[4]),
	CLK(NULL, "pll1_sysclk5", &c6x_soc_pll1.sysclks[5]),
	CLK(NULL, "core", &c6x_core_clk),
	CLK("i2c_davinci.1", NULL, &c6x_i2c_clk),
	CLK("watchdog", NULL, &c6x_watchdog_clk),
	CLK("2c81800.mdio", NULL, &c6x_mdio_clk),
	CLK("", NULL, NULL)
};

static void __init c6457_setup_clocks(struct device_node *node)
{
	struct pll_data *pll = &c6x_soc_pll1;
	struct clk *sysclks = pll->sysclks;

	pll->flags = PLL_HAS_MUL | PLL_HAS_POST;

	sysclks[1].flags |= FIXED_DIV_PLL;
	sysclks[1].div = 1;
	sysclks[2].flags |= FIXED_DIV_PLL;
	sysclks[2].div = 3;
	sysclks[3].flags |= FIXED_DIV_PLL;
	sysclks[3].div = 6;
	sysclks[4].div = PLLDIV4;
	sysclks[5].div = PLLDIV5;

	c6x_core_clk.parent = &sysclks[1];
	c6x_i2c_clk.parent = &sysclks[3];
	c6x_watchdog_clk.parent = &sysclks[5];
	c6x_mdio_clk.parent = &sysclks[5];

	c6x_clks_init(c6457_clks);
}
#endif /* CONFIG_SOC_TMS320C6455 */

#ifdef CONFIG_SOC_TMS320C6472
static struct clk_lookup c6472_clks[] = {
	CLK(NULL, "pll1", &c6x_soc_pll1.sysclks[0]),
	CLK(NULL, "pll1_sysclk1", &c6x_soc_pll1.sysclks[1]),
	CLK(NULL, "pll1_sysclk2", &c6x_soc_pll1.sysclks[2]),
	CLK(NULL, "pll1_sysclk3", &c6x_soc_pll1.sysclks[3]),
	CLK(NULL, "pll1_sysclk4", &c6x_soc_pll1.sysclks[4]),
	CLK(NULL, "pll1_sysclk5", &c6x_soc_pll1.sysclks[5]),
	CLK(NULL, "pll1_sysclk6", &c6x_soc_pll1.sysclks[6]),
	CLK(NULL, "pll1_sysclk7", &c6x_soc_pll1.sysclks[7]),
	CLK(NULL, "pll1_sysclk8", &c6x_soc_pll1.sysclks[8]),
	CLK(NULL, "pll1_sysclk9", &c6x_soc_pll1.sysclks[9]),
	CLK(NULL, "pll1_sysclk10", &c6x_soc_pll1.sysclks[10]),
	CLK(NULL, "core", &c6x_core_clk),
	CLK("i2c_davinci.1", NULL, &c6x_i2c_clk),
	CLK("watchdog", NULL, &c6x_watchdog_clk),
	CLK("2c81800.mdio", NULL, &c6x_mdio_clk),
	CLK("", NULL, NULL)
};

/* assumptions used for delay loop calculations */
#define MIN_CLKIN1_KHz 15625
#define MAX_CORE_KHz   700000
#define MIN_PLLOUT_KHz MIN_CLKIN1_KHz

static void __init c6472_setup_clocks(struct device_node *node)
{
	struct pll_data *pll = &c6x_soc_pll1;
	struct clk *sysclks = pll->sysclks;
	int i;

	pll->flags = PLL_HAS_MUL;

	for (i = 1; i <= 6; i++) {
		sysclks[i].flags |= FIXED_DIV_PLL;
		sysclks[i].div = 1;
	}

	sysclks[7].flags |= FIXED_DIV_PLL;
	sysclks[7].div = 3;
	sysclks[8].flags |= FIXED_DIV_PLL;
	sysclks[8].div = 6;
	sysclks[9].flags |= FIXED_DIV_PLL;
	sysclks[9].div = 2;
	sysclks[10].div = PLLDIV10;

	c6x_core_clk.parent = &sysclks[get_coreid() + 1];
	c6x_i2c_clk.parent = &sysclks[8];
	c6x_watchdog_clk.parent = &sysclks[8];
	c6x_mdio_clk.parent = &sysclks[5];

	c6x_clks_init(c6472_clks);
}
#endif /* CONFIG_SOC_TMS320C6472 */


#ifdef CONFIG_SOC_TMS320C6474
static struct clk_lookup c6474_clks[] = {
	CLK(NULL, "pll1", &c6x_soc_pll1.sysclks[0]),
	CLK(NULL, "pll1_sysclk7", &c6x_soc_pll1.sysclks[7]),
	CLK(NULL, "pll1_sysclk9", &c6x_soc_pll1.sysclks[9]),
	CLK(NULL, "pll1_sysclk10", &c6x_soc_pll1.sysclks[10]),
	CLK(NULL, "pll1_sysclk11", &c6x_soc_pll1.sysclks[11]),
	CLK(NULL, "pll1_sysclk12", &c6x_soc_pll1.sysclks[12]),
	CLK(NULL, "pll1_sysclk13", &c6x_soc_pll1.sysclks[13]),
	CLK(NULL, "core", &c6x_core_clk),
	CLK("i2c_davinci.1", NULL, &c6x_i2c_clk),
	CLK("mcbsp.1", NULL, &c6x_mcbsp1_clk),
	CLK("mcbsp.2", NULL, &c6x_mcbsp2_clk),
	CLK("watchdog", NULL, &c6x_watchdog_clk),
	CLK("2c81800.mdio", NULL, &c6x_mdio_clk),
	CLK("", NULL, NULL)
};

static void __init c6474_setup_clocks(struct device_node *node)
{
	struct pll_data *pll = &c6x_soc_pll1;
	struct clk *sysclks = pll->sysclks;

	pll->flags = PLL_HAS_MUL;

	sysclks[7].flags |= FIXED_DIV_PLL;
	sysclks[7].div = 1;
	sysclks[9].flags |= FIXED_DIV_PLL;
	sysclks[9].div = 3;
	sysclks[10].flags |= FIXED_DIV_PLL;
	sysclks[10].div = 6;

	sysclks[11].div = PLLDIV11;

	sysclks[12].flags |= FIXED_DIV_PLL;
	sysclks[12].div = 2;

	sysclks[13].div = PLLDIV13;

	c6x_core_clk.parent = &sysclks[7];
	c6x_i2c_clk.parent = &sysclks[10];
	c6x_watchdog_clk.parent = &sysclks[10];
	c6x_mcbsp1_clk.parent = &sysclks[10];
	c6x_mcbsp2_clk.parent = &sysclks[10];

	c6x_clks_init(c6474_clks);
}
#endif /* CONFIG_SOC_TMS320C6474 */

static struct of_device_id c6x_clkc_match[] __initdata = {
#ifdef CONFIG_SOC_TMS320C6455
	{ .compatible = "ti,c6455-pll", .data = c6455_setup_clocks },
#endif
#ifdef CONFIG_SOC_TMS320C6457
	{ .compatible = "ti,c6457-pll", .data = c6457_setup_clocks },
#endif
#ifdef CONFIG_SOC_TMS320C6472
	{ .compatible = "ti,c6472-pll", .data = c6472_setup_clocks },
#endif
#ifdef CONFIG_SOC_TMS320C6474
	{ .compatible = "ti,c6474-pll", .data = c6474_setup_clocks },
#endif
	{ .compatible = "ti,c64x+pll" },
	{}
};

void __init c64x_setup_clocks(void)
{
	void (*__setup_clocks)(struct device_node *np);
	struct pll_data *pll = &c6x_soc_pll1;
	struct device_node *node;
	const struct of_device_id *id;
	int err;
	u32 val;

	node = of_find_matching_node(NULL, c6x_clkc_match);
	if (!node)
		return;

	pll->base = of_iomap(node, 0);
	if (!pll->base)
		goto out;

	err = of_property_read_u32(node, "clock-frequency", &val);
	if (err || val == 0) {
		pr_err("%s: no clock-frequency found! Using %dMHz\n",
		       node->full_name, (int)val / 1000000);
		val = 25000000;
	}
	clkin1.rate = val;

	err = of_property_read_u32(node, "ti,c64x+pll-bypass-delay", &val);
	if (err)
		val = 5000;
	pll->bypass_delay = val;

	err = of_property_read_u32(node, "ti,c64x+pll-reset-delay", &val);
	if (err)
		val = 30000;
	pll->reset_delay = val;

	err = of_property_read_u32(node, "ti,c64x+pll-lock-delay", &val);
	if (err)
		val = 30000;
	pll->lock_delay = val;

	/* id->data is a pointer to SoC-specific setup */
	id = of_match_node(c6x_clkc_match, node);
	if (id && id->data) {
		__setup_clocks = id->data;
		__setup_clocks(node);
	}

out:
	of_node_put(node);
}
