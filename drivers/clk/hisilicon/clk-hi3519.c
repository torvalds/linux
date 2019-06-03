// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hi3519 Clock Driver
 *
 * Copyright (c) 2015-2016 HiSilicon Technologies Co., Ltd.
 */

#include <dt-bindings/clock/hi3519-clock.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include "clk.h"
#include "reset.h"

#define HI3519_INNER_CLK_OFFSET	64
#define HI3519_FIXED_24M	65
#define HI3519_FIXED_50M	66
#define HI3519_FIXED_75M	67
#define HI3519_FIXED_125M	68
#define HI3519_FIXED_150M	69
#define HI3519_FIXED_200M	70
#define HI3519_FIXED_250M	71
#define HI3519_FIXED_300M	72
#define HI3519_FIXED_400M	73
#define HI3519_FMC_MUX		74

#define HI3519_NR_CLKS		128

struct hi3519_crg_data {
	struct hisi_clock_data *clk_data;
	struct hisi_reset_controller *rstc;
};

static const struct hisi_fixed_rate_clock hi3519_fixed_rate_clks[] = {
	{ HI3519_FIXED_24M, "24m", NULL, 0, 24000000, },
	{ HI3519_FIXED_50M, "50m", NULL, 0, 50000000, },
	{ HI3519_FIXED_75M, "75m", NULL, 0, 75000000, },
	{ HI3519_FIXED_125M, "125m", NULL, 0, 125000000, },
	{ HI3519_FIXED_150M, "150m", NULL, 0, 150000000, },
	{ HI3519_FIXED_200M, "200m", NULL, 0, 200000000, },
	{ HI3519_FIXED_250M, "250m", NULL, 0, 250000000, },
	{ HI3519_FIXED_300M, "300m", NULL, 0, 300000000, },
	{ HI3519_FIXED_400M, "400m", NULL, 0, 400000000, },
};

static const char *const fmc_mux_p[] = {
		"24m", "75m", "125m", "150m", "200m", "250m", "300m", "400m", };
static u32 fmc_mux_table[] = {0, 1, 2, 3, 4, 5, 6, 7};

static const struct hisi_mux_clock hi3519_mux_clks[] = {
	{ HI3519_FMC_MUX, "fmc_mux", fmc_mux_p, ARRAY_SIZE(fmc_mux_p),
		CLK_SET_RATE_PARENT, 0xc0, 2, 3, 0, fmc_mux_table, },
};

static const struct hisi_gate_clock hi3519_gate_clks[] = {
	{ HI3519_FMC_CLK, "clk_fmc", "fmc_mux",
		CLK_SET_RATE_PARENT, 0xc0, 1, 0, },
	{ HI3519_UART0_CLK, "clk_uart0", "24m",
		CLK_SET_RATE_PARENT, 0xe4, 20, 0, },
	{ HI3519_UART1_CLK, "clk_uart1", "24m",
		CLK_SET_RATE_PARENT, 0xe4, 21, 0, },
	{ HI3519_UART2_CLK, "clk_uart2", "24m",
		CLK_SET_RATE_PARENT, 0xe4, 22, 0, },
	{ HI3519_UART3_CLK, "clk_uart3", "24m",
		CLK_SET_RATE_PARENT, 0xe4, 23, 0, },
	{ HI3519_UART4_CLK, "clk_uart4", "24m",
		CLK_SET_RATE_PARENT, 0xe4, 24, 0, },
	{ HI3519_SPI0_CLK, "clk_spi0", "50m",
		CLK_SET_RATE_PARENT, 0xe4, 16, 0, },
	{ HI3519_SPI1_CLK, "clk_spi1", "50m",
		CLK_SET_RATE_PARENT, 0xe4, 17, 0, },
	{ HI3519_SPI2_CLK, "clk_spi2", "50m",
		CLK_SET_RATE_PARENT, 0xe4, 18, 0, },
};

static struct hisi_clock_data *hi3519_clk_register(struct platform_device *pdev)
{
	struct hisi_clock_data *clk_data;
	int ret;

	clk_data = hisi_clk_alloc(pdev, HI3519_NR_CLKS);
	if (!clk_data)
		return ERR_PTR(-ENOMEM);

	ret = hisi_clk_register_fixed_rate(hi3519_fixed_rate_clks,
				     ARRAY_SIZE(hi3519_fixed_rate_clks),
				     clk_data);
	if (ret)
		return ERR_PTR(ret);

	ret = hisi_clk_register_mux(hi3519_mux_clks,
				ARRAY_SIZE(hi3519_mux_clks),
				clk_data);
	if (ret)
		goto unregister_fixed_rate;

	ret = hisi_clk_register_gate(hi3519_gate_clks,
				ARRAY_SIZE(hi3519_gate_clks),
				clk_data);
	if (ret)
		goto unregister_mux;

	ret = of_clk_add_provider(pdev->dev.of_node,
			of_clk_src_onecell_get, &clk_data->clk_data);
	if (ret)
		goto unregister_gate;

	return clk_data;

unregister_fixed_rate:
	hisi_clk_unregister_fixed_rate(hi3519_fixed_rate_clks,
				ARRAY_SIZE(hi3519_fixed_rate_clks),
				clk_data);

unregister_mux:
	hisi_clk_unregister_mux(hi3519_mux_clks,
				ARRAY_SIZE(hi3519_mux_clks),
				clk_data);
unregister_gate:
	hisi_clk_unregister_gate(hi3519_gate_clks,
				ARRAY_SIZE(hi3519_gate_clks),
				clk_data);
	return ERR_PTR(ret);
}

static void hi3519_clk_unregister(struct platform_device *pdev)
{
	struct hi3519_crg_data *crg = platform_get_drvdata(pdev);

	of_clk_del_provider(pdev->dev.of_node);

	hisi_clk_unregister_gate(hi3519_gate_clks,
				ARRAY_SIZE(hi3519_mux_clks),
				crg->clk_data);
	hisi_clk_unregister_mux(hi3519_mux_clks,
				ARRAY_SIZE(hi3519_mux_clks),
				crg->clk_data);
	hisi_clk_unregister_fixed_rate(hi3519_fixed_rate_clks,
				ARRAY_SIZE(hi3519_fixed_rate_clks),
				crg->clk_data);
}

static int hi3519_clk_probe(struct platform_device *pdev)
{
	struct hi3519_crg_data *crg;

	crg = devm_kmalloc(&pdev->dev, sizeof(*crg), GFP_KERNEL);
	if (!crg)
		return -ENOMEM;

	crg->rstc = hisi_reset_init(pdev);
	if (!crg->rstc)
		return -ENOMEM;

	crg->clk_data = hi3519_clk_register(pdev);
	if (IS_ERR(crg->clk_data)) {
		hisi_reset_exit(crg->rstc);
		return PTR_ERR(crg->clk_data);
	}

	platform_set_drvdata(pdev, crg);
	return 0;
}

static int hi3519_clk_remove(struct platform_device *pdev)
{
	struct hi3519_crg_data *crg = platform_get_drvdata(pdev);

	hisi_reset_exit(crg->rstc);
	hi3519_clk_unregister(pdev);
	return 0;
}


static const struct of_device_id hi3519_clk_match_table[] = {
	{ .compatible = "hisilicon,hi3519-crg" },
	{ }
};
MODULE_DEVICE_TABLE(of, hi3519_clk_match_table);

static struct platform_driver hi3519_clk_driver = {
	.probe          = hi3519_clk_probe,
	.remove		= hi3519_clk_remove,
	.driver         = {
		.name   = "hi3519-clk",
		.of_match_table = hi3519_clk_match_table,
	},
};

static int __init hi3519_clk_init(void)
{
	return platform_driver_register(&hi3519_clk_driver);
}
core_initcall(hi3519_clk_init);

static void __exit hi3519_clk_exit(void)
{
	platform_driver_unregister(&hi3519_clk_driver);
}
module_exit(hi3519_clk_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("HiSilicon Hi3519 Clock Driver");
