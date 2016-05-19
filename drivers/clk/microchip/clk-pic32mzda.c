/*
 * Purna Chandra Mandal,<purna.mandal@microchip.com>
 * Copyright (C) 2015 Microchip Technology Inc.  All rights reserved.
 *
 * This program is free software; you can distribute it and/or modify it
 * under the terms of the GNU General Public License (Version 2) as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */
#include <dt-bindings/clock/microchip,pic32-clock.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <asm/traps.h>

#include "clk-core.h"

/* FRC Postscaler */
#define OSC_FRCDIV_MASK		0x07
#define OSC_FRCDIV_SHIFT	24

/* SPLL fields */
#define PLL_ICLK_MASK		0x01
#define PLL_ICLK_SHIFT		7

#define DECLARE_PERIPHERAL_CLOCK(__clk_name, __reg, __flags)	\
	{							\
		.ctrl_reg = (__reg),				\
		.init_data = {					\
			.name = (__clk_name),			\
			.parent_names = (const char *[]) {	\
				"sys_clk"			\
			},					\
			.num_parents = 1,			\
			.ops = &pic32_pbclk_ops,		\
			.flags = (__flags),			\
		},						\
	}

#define DECLARE_REFO_CLOCK(__clkid, __reg)				\
	{								\
		.ctrl_reg = (__reg),					\
		.init_data = {						\
			.name = "refo" #__clkid "_clk",			\
			.parent_names = (const char *[]) {		\
				"sys_clk", "pb1_clk", "posc_clk",	\
				"frc_clk", "lprc_clk", "sosc_clk",	\
				"sys_pll", "refi" #__clkid "_clk",	\
				"bfrc_clk",				\
			},						\
			.num_parents = 9,				\
			.flags = CLK_SET_RATE_GATE | CLK_SET_PARENT_GATE,\
			.ops = &pic32_roclk_ops,			\
		},							\
		.parent_map = (const u32[]) {				\
			0, 1, 2, 3, 4, 5, 7, 8, 9			\
		},							\
	}

static const struct pic32_ref_osc_data ref_clks[] = {
	DECLARE_REFO_CLOCK(1, 0x80),
	DECLARE_REFO_CLOCK(2, 0xa0),
	DECLARE_REFO_CLOCK(3, 0xc0),
	DECLARE_REFO_CLOCK(4, 0xe0),
	DECLARE_REFO_CLOCK(5, 0x100),
};

static const struct pic32_periph_clk_data periph_clocks[] = {
	DECLARE_PERIPHERAL_CLOCK("pb1_clk", 0x140, 0),
	DECLARE_PERIPHERAL_CLOCK("pb2_clk", 0x150, CLK_IGNORE_UNUSED),
	DECLARE_PERIPHERAL_CLOCK("pb3_clk", 0x160, 0),
	DECLARE_PERIPHERAL_CLOCK("pb4_clk", 0x170, 0),
	DECLARE_PERIPHERAL_CLOCK("pb5_clk", 0x180, 0),
	DECLARE_PERIPHERAL_CLOCK("pb6_clk", 0x190, 0),
	DECLARE_PERIPHERAL_CLOCK("cpu_clk", 0x1a0, CLK_IGNORE_UNUSED),
};

static const struct pic32_sys_clk_data sys_mux_clk = {
	.slew_reg = 0x1c0,
	.slew_div = 2, /* step of div_4 -> div_2 -> no_div */
	.init_data = {
		.name = "sys_clk",
		.parent_names = (const char *[]) {
			"frcdiv_clk", "sys_pll", "posc_clk",
			"sosc_clk", "lprc_clk", "frcdiv_clk",
		},
		.num_parents = 6,
		.ops = &pic32_sclk_ops,
	},
	.parent_map = (const u32[]) {
		0, 1, 2, 4, 5, 7,
	},
};

static const struct pic32_sys_pll_data sys_pll = {
	.ctrl_reg = 0x020,
	.status_reg = 0x1d0,
	.lock_mask = BIT(7),
	.init_data = {
		.name = "sys_pll",
		.parent_names = (const char *[]) {
			"spll_mux_clk"
		},
		.num_parents = 1,
		.ops = &pic32_spll_ops,
	},
};

static const struct pic32_sec_osc_data sosc_clk = {
	.status_reg = 0x1d0,
	.enable_mask = BIT(1),
	.status_mask = BIT(4),
	.init_data = {
		.name = "sosc_clk",
		.parent_names = NULL,
		.ops = &pic32_sosc_ops,
	},
};

static int pic32mzda_critical_clks[] = {
	PB2CLK, PB7CLK
};

/* PIC32MZDA clock data */
struct pic32mzda_clk_data {
	struct clk *clks[MAXCLKS];
	struct pic32_clk_common core;
	struct clk_onecell_data onecell_data;
	struct notifier_block failsafe_notifier;
};

static int pic32_fscm_nmi(struct notifier_block *nb,
			  unsigned long action, void *data)
{
	struct pic32mzda_clk_data *cd;

	cd  = container_of(nb, struct pic32mzda_clk_data, failsafe_notifier);

	/* SYSCLK is now running from BFRCCLK. Report clock failure. */
	if (readl(cd->core.iobase) & BIT(2))
		pr_alert("pic32-clk: FSCM detected clk failure.\n");

	/* TODO: detect reason of failure and recover accordingly */

	return NOTIFY_OK;
}

static int pic32mzda_clk_probe(struct platform_device *pdev)
{
	const char *const pll_mux_parents[] = {"posc_clk", "frc_clk"};
	struct device_node *np = pdev->dev.of_node;
	struct pic32mzda_clk_data *cd;
	struct pic32_clk_common *core;
	struct clk *pll_mux_clk, *clk;
	struct clk **clks;
	int nr_clks, i, ret;

	cd = devm_kzalloc(&pdev->dev, sizeof(*cd), GFP_KERNEL);
	if (!cd)
		return -ENOMEM;

	core = &cd->core;
	core->iobase = of_io_request_and_map(np, 0, of_node_full_name(np));
	if (IS_ERR(core->iobase)) {
		dev_err(&pdev->dev, "pic32-clk: failed to map registers\n");
		return PTR_ERR(core->iobase);
	}

	spin_lock_init(&core->reg_lock);
	core->dev = &pdev->dev;
	clks = &cd->clks[0];

	/* register fixed rate clocks */
	clks[POSCCLK] = clk_register_fixed_rate(&pdev->dev, "posc_clk", NULL,
						CLK_IS_ROOT, 24000000);
	clks[FRCCLK] =  clk_register_fixed_rate(&pdev->dev, "frc_clk", NULL,
						CLK_IS_ROOT, 8000000);
	clks[BFRCCLK] = clk_register_fixed_rate(&pdev->dev, "bfrc_clk", NULL,
						CLK_IS_ROOT, 8000000);
	clks[LPRCCLK] = clk_register_fixed_rate(&pdev->dev, "lprc_clk", NULL,
						CLK_IS_ROOT, 32000);
	clks[UPLLCLK] = clk_register_fixed_rate(&pdev->dev, "usbphy_clk", NULL,
						CLK_IS_ROOT, 24000000);
	/* fixed rate (optional) clock */
	if (of_find_property(np, "microchip,pic32mzda-sosc", NULL)) {
		pr_info("pic32-clk: dt requests SOSC.\n");
		clks[SOSCCLK] = pic32_sosc_clk_register(&sosc_clk, core);
	}
	/* divider clock */
	clks[FRCDIVCLK] = clk_register_divider(&pdev->dev, "frcdiv_clk",
					       "frc_clk", 0,
					       core->iobase,
					       OSC_FRCDIV_SHIFT,
					       OSC_FRCDIV_MASK,
					       CLK_DIVIDER_POWER_OF_TWO,
					       &core->reg_lock);
	/* PLL ICLK mux */
	pll_mux_clk = clk_register_mux(&pdev->dev, "spll_mux_clk",
				       pll_mux_parents, 2, 0,
				       core->iobase + 0x020,
				       PLL_ICLK_SHIFT, 1, 0, &core->reg_lock);
	if (IS_ERR(pll_mux_clk))
		pr_err("spll_mux_clk: clk register failed\n");

	/* PLL */
	clks[PLLCLK] = pic32_spll_clk_register(&sys_pll, core);
	/* SYSTEM clock */
	clks[SCLK] = pic32_sys_clk_register(&sys_mux_clk, core);
	/* Peripheral bus clocks */
	for (nr_clks = PB1CLK, i = 0; nr_clks <= PB7CLK; i++, nr_clks++)
		clks[nr_clks] = pic32_periph_clk_register(&periph_clocks[i],
							  core);
	/* Reference oscillator clock */
	for (nr_clks = REF1CLK, i = 0; nr_clks <= REF5CLK; i++, nr_clks++)
		clks[nr_clks] = pic32_refo_clk_register(&ref_clks[i], core);

	/* register clkdev */
	for (i = 0; i < MAXCLKS; i++) {
		if (IS_ERR(clks[i]))
			continue;
		clk_register_clkdev(clks[i], NULL, __clk_get_name(clks[i]));
	}

	/* register clock provider */
	cd->onecell_data.clks = clks;
	cd->onecell_data.clk_num = MAXCLKS;
	ret = of_clk_add_provider(np, of_clk_src_onecell_get,
				  &cd->onecell_data);
	if (ret)
		return ret;

	/* force enable critical clocks */
	for (i = 0; i < ARRAY_SIZE(pic32mzda_critical_clks); i++) {
		clk = clks[pic32mzda_critical_clks[i]];
		if (clk_prepare_enable(clk))
			dev_err(&pdev->dev, "clk_prepare_enable(%s) failed\n",
				__clk_get_name(clk));
	}

	/* register NMI for failsafe clock monitor */
	cd->failsafe_notifier.notifier_call = pic32_fscm_nmi;
	return register_nmi_notifier(&cd->failsafe_notifier);
}

static const struct of_device_id pic32mzda_clk_match_table[] = {
	{ .compatible = "microchip,pic32mzda-clk", },
	{ }
};
MODULE_DEVICE_TABLE(of, pic32mzda_clk_match_table);

static struct platform_driver pic32mzda_clk_driver = {
	.probe		= pic32mzda_clk_probe,
	.driver		= {
		.name	= "clk-pic32mzda",
		.of_match_table = pic32mzda_clk_match_table,
	},
};

static int __init microchip_pic32mzda_clk_init(void)
{
	return platform_driver_register(&pic32mzda_clk_driver);
}
core_initcall(microchip_pic32mzda_clk_init);

MODULE_DESCRIPTION("Microchip PIC32MZDA Clock Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:clk-pic32mzda");
