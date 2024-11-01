// SPDX-License-Identifier: GPL-2.0-only
/*
 * Toshiba Visconti clock controller
 *
 * Copyright (c) 2021 TOSHIBA CORPORATION
 * Copyright (c) 2021 Toshiba Electronic Devices & Storage Corporation
 *
 * Nobuhiro Iwamatsu <nobuhiro1.iwamatsu@toshiba.co.jp>
 */

#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include <dt-bindings/clock/toshiba,tmpv770x.h>
#include <dt-bindings/reset/toshiba,tmpv770x.h>

#include "clkc.h"
#include "reset.h"

static DEFINE_SPINLOCK(tmpv770x_clk_lock);
static DEFINE_SPINLOCK(tmpv770x_rst_lock);

static const struct clk_parent_data clks_parent_data[] = {
	{ .fw_name = "pipll1", .name = "pipll1", },
};

static const struct clk_parent_data pietherplls_parent_data[] = {
	{ .fw_name = "pietherpll", .name = "pietherpll", },
};

static const struct visconti_fixed_clk fixed_clk_tables[] = {
	/* PLL1 */
	/* PICMPT0/1, PITSC, PIUWDT, PISWDT, PISBUS, PIPMU, PIGPMU, PITMU */
	/* PIEMM, PIMISC, PIGCOMM, PIDCOMM, PIMBUS, PIGPIO, PIPGM */
	{ TMPV770X_CLK_PIPLL1_DIV4, "pipll1_div4", "pipll1", 0, 1, 4, },
	/* PISBUS */
	{ TMPV770X_CLK_PIPLL1_DIV2, "pipll1_div2", "pipll1", 0, 1, 2, },
	/* PICOBUS_CLK */
	{ TMPV770X_CLK_PIPLL1_DIV1, "pipll1_div1", "pipll1", 0, 1, 1, },
	/* PIDNNPLL */
	/* CONN_CLK, PIMBUS, PICRC0/1 */
	{ TMPV770X_CLK_PIDNNPLL_DIV1, "pidnnpll_div1", "pidnnpll", 0, 1, 1, },
	{ TMPV770X_CLK_PIREFCLK, "pirefclk", "osc2-clk", 0, 1, 1, },
	{ TMPV770X_CLK_WDTCLK, "wdtclk", "osc2-clk", 0, 1, 1, },
};

static const struct visconti_clk_gate_table pietherpll_clk_gate_tables[] = {
	/* pietherpll */
	{ TMPV770X_CLK_PIETHER_2P5M, "piether_2p5m",
		pietherplls_parent_data, ARRAY_SIZE(pietherplls_parent_data),
		CLK_SET_RATE_PARENT, 0x34, 0x134, 4, 200,
		TMPV770X_RESET_PIETHER_2P5M, },
	{ TMPV770X_CLK_PIETHER_25M, "piether_25m",
		pietherplls_parent_data, ARRAY_SIZE(pietherplls_parent_data),
		CLK_SET_RATE_PARENT, 0x34, 0x134, 5, 20,
		TMPV770X_RESET_PIETHER_25M, },
	{ TMPV770X_CLK_PIETHER_50M, "piether_50m",
		pietherplls_parent_data, ARRAY_SIZE(pietherplls_parent_data),
		CLK_SET_RATE_PARENT, 0x34, 0x134, 6, 10,
		TMPV770X_RESET_PIETHER_50M, },
	{ TMPV770X_CLK_PIETHER_125M, "piether_125m",
		pietherplls_parent_data, ARRAY_SIZE(pietherplls_parent_data),
		CLK_SET_RATE_PARENT, 0x34, 0x134, 7, 4,
		TMPV770X_RESET_PIETHER_125M, },
};

static const struct visconti_clk_gate_table clk_gate_tables[] = {
	{ TMPV770X_CLK_HOX, "hox",
		clks_parent_data, ARRAY_SIZE(clks_parent_data),
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x4c, 0x14c, 0, 1,
		TMPV770X_RESET_HOX, },
	{ TMPV770X_CLK_PCIE_MSTR, "pcie_mstr",
		clks_parent_data, ARRAY_SIZE(clks_parent_data),
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x38, 0x138, 0, 1,
		TMPV770X_RESET_PCIE_MSTR, },
	{ TMPV770X_CLK_PCIE_AUX, "pcie_aux",
		clks_parent_data, ARRAY_SIZE(clks_parent_data),
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x38, 0x138, 1, 24,
		TMPV770X_RESET_PCIE_AUX, },
	{ TMPV770X_CLK_PIINTC, "piintc",
		clks_parent_data, ARRAY_SIZE(clks_parent_data),
		CLK_IGNORE_UNUSED, 0x8, 0x108, 0, 2, //FIX!!
		TMPV770X_RESET_PIINTC,},
	{ TMPV770X_CLK_PIETHER_BUS, "piether_bus",
		clks_parent_data, ARRAY_SIZE(clks_parent_data),
		0, 0x34, 0x134, 0, 2,
		TMPV770X_RESET_PIETHER_BUS, }, /* BUS_CLK */
	{ TMPV770X_CLK_PISPI0, "pispi0",
		clks_parent_data, ARRAY_SIZE(clks_parent_data),
		0, 0x28, 0x128, 0, 2,
		TMPV770X_RESET_PISPI0, },
	{ TMPV770X_CLK_PISPI1, "pispi1",
		clks_parent_data, ARRAY_SIZE(clks_parent_data),
		0, 0x28, 0x128, 1, 2,
		TMPV770X_RESET_PISPI1, },
	{ TMPV770X_CLK_PISPI2, "pispi2",
		clks_parent_data, ARRAY_SIZE(clks_parent_data),
		0, 0x28, 0x128, 2, 2,
		TMPV770X_RESET_PISPI2, },
	{ TMPV770X_CLK_PISPI3, "pispi3",
		clks_parent_data, ARRAY_SIZE(clks_parent_data),
		0, 0x28, 0x128, 3, 2,
		TMPV770X_RESET_PISPI3,},
	{ TMPV770X_CLK_PISPI4, "pispi4",
		clks_parent_data, ARRAY_SIZE(clks_parent_data),
		0, 0x28, 0x128, 4, 2,
		TMPV770X_RESET_PISPI4, },
	{ TMPV770X_CLK_PISPI5, "pispi5",
		clks_parent_data, ARRAY_SIZE(clks_parent_data),
		0, 0x28, 0x128, 5, 2,
		TMPV770X_RESET_PISPI5},
	{ TMPV770X_CLK_PISPI6, "pispi6",
		clks_parent_data, ARRAY_SIZE(clks_parent_data),
		0, 0x28, 0x128, 6, 2,
		TMPV770X_RESET_PISPI6,},
	{ TMPV770X_CLK_PIUART0, "piuart0",
		clks_parent_data, ARRAY_SIZE(clks_parent_data),
		//CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2c, 0x12c, 0, 4,
		0, 0x2c, 0x12c, 0, 4,
		TMPV770X_RESET_PIUART0,},
	{ TMPV770X_CLK_PIUART1, "piuart1",
		clks_parent_data, ARRAY_SIZE(clks_parent_data),
		//CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0x2c, 0x12c, 1, 4,
		0, 0x2c, 0x12c, 1, 4,
		TMPV770X_RESET_PIUART1, },
	{ TMPV770X_CLK_PIUART2, "piuart2",
		clks_parent_data, ARRAY_SIZE(clks_parent_data),
		0, 0x2c, 0x12c, 2, 4,
		TMPV770X_RESET_PIUART2, },
	{ TMPV770X_CLK_PIUART3, "piuart3",
		clks_parent_data, ARRAY_SIZE(clks_parent_data),
		0, 0x2c, 0x12c, 3, 4,
		TMPV770X_RESET_PIUART3, },
	{ TMPV770X_CLK_PII2C0, "pii2c0",
		clks_parent_data, ARRAY_SIZE(clks_parent_data),
		0, 0x30, 0x130, 0, 4,
		TMPV770X_RESET_PII2C0, },
	{ TMPV770X_CLK_PII2C1, "pii2c1",
		clks_parent_data, ARRAY_SIZE(clks_parent_data),
		0, 0x30, 0x130, 1, 4,
		TMPV770X_RESET_PII2C1, },
	{ TMPV770X_CLK_PII2C2, "pii2c2",
		clks_parent_data, ARRAY_SIZE(clks_parent_data),
		0, 0x30, 0x130, 2, 4,
		TMPV770X_RESET_PII2C2, },
	{ TMPV770X_CLK_PII2C3, "pii2c3",
		clks_parent_data, ARRAY_SIZE(clks_parent_data),
		0, 0x30, 0x130, 3, 4,
		TMPV770X_RESET_PII2C3,},
	{ TMPV770X_CLK_PII2C4, "pii2c4",
		clks_parent_data, ARRAY_SIZE(clks_parent_data),
		0, 0x30, 0x130, 4, 4,
		TMPV770X_RESET_PII2C4, },
	{ TMPV770X_CLK_PII2C5, "pii2c5",
		clks_parent_data, ARRAY_SIZE(clks_parent_data),
		0, 0x30, 0x130, 5, 4,
		TMPV770X_RESET_PII2C5, },
	{ TMPV770X_CLK_PII2C6, "pii2c6",
		clks_parent_data, ARRAY_SIZE(clks_parent_data),
		0, 0x30, 0x130, 6, 4,
		TMPV770X_RESET_PII2C6, },
	{ TMPV770X_CLK_PII2C7, "pii2c7",
		clks_parent_data, ARRAY_SIZE(clks_parent_data),
		0, 0x30, 0x130, 7, 4,
		TMPV770X_RESET_PII2C7, },
	{ TMPV770X_CLK_PII2C8, "pii2c8",
		clks_parent_data, ARRAY_SIZE(clks_parent_data),
		0, 0x30, 0x130, 8, 4,
		TMPV770X_RESET_PII2C8, },
	/* PIPCMIF */
	{ TMPV770X_CLK_PIPCMIF, "pipcmif",
		clks_parent_data, ARRAY_SIZE(clks_parent_data),
		0, 0x64, 0x164, 0, 4,
		TMPV770X_RESET_PIPCMIF, },
	/* PISYSTEM */
	{ TMPV770X_CLK_WRCK, "wrck",
		clks_parent_data, ARRAY_SIZE(clks_parent_data),
		0, 0x68, 0x168, 9, 32,
		NO_RESET, },
	{ TMPV770X_CLK_PICKMON, "pickmon",
		clks_parent_data, ARRAY_SIZE(clks_parent_data),
		0, 0x10, 0x110, 8, 4,
		TMPV770X_RESET_PICKMON, },
	{ TMPV770X_CLK_SBUSCLK, "sbusclk",
		clks_parent_data, ARRAY_SIZE(clks_parent_data),
		0, 0x14, 0x114, 0, 4,
		TMPV770X_RESET_SBUSCLK, },
};

static const struct visconti_reset_data clk_reset_data[] = {
	[TMPV770X_RESET_PIETHER_2P5M]	= { 0x434, 0x534, 4, },
	[TMPV770X_RESET_PIETHER_25M]	= { 0x434, 0x534, 5, },
	[TMPV770X_RESET_PIETHER_50M]	= { 0x434, 0x534, 6, },
	[TMPV770X_RESET_PIETHER_125M]	= { 0x434, 0x534, 7, },
	[TMPV770X_RESET_HOX]		= { 0x44c, 0x54c, 0, },
	[TMPV770X_RESET_PCIE_MSTR]	= { 0x438, 0x538, 0, },
	[TMPV770X_RESET_PCIE_AUX]	= { 0x438, 0x538, 1, },
	[TMPV770X_RESET_PIINTC]		= { 0x408, 0x508, 0, },
	[TMPV770X_RESET_PIETHER_BUS]	= { 0x434, 0x534, 0, },
	[TMPV770X_RESET_PISPI0]		= { 0x428, 0x528, 0, },
	[TMPV770X_RESET_PISPI1]		= { 0x428, 0x528, 1, },
	[TMPV770X_RESET_PISPI2]		= { 0x428, 0x528, 2, },
	[TMPV770X_RESET_PISPI3]		= { 0x428, 0x528, 3, },
	[TMPV770X_RESET_PISPI4]		= { 0x428, 0x528, 4, },
	[TMPV770X_RESET_PISPI5]		= { 0x428, 0x528, 5, },
	[TMPV770X_RESET_PISPI6]		= { 0x428, 0x528, 6, },
	[TMPV770X_RESET_PIUART0]	= { 0x42c, 0x52c, 0, },
	[TMPV770X_RESET_PIUART1]	= { 0x42c, 0x52c, 1, },
	[TMPV770X_RESET_PIUART2]	= { 0x42c, 0x52c, 2, },
	[TMPV770X_RESET_PIUART3]	= { 0x42c, 0x52c, 3, },
	[TMPV770X_RESET_PII2C0]		= { 0x430, 0x530, 0, },
	[TMPV770X_RESET_PII2C1]		= { 0x430, 0x530, 1, },
	[TMPV770X_RESET_PII2C2]		= { 0x430, 0x530, 2, },
	[TMPV770X_RESET_PII2C3]		= { 0x430, 0x530, 3, },
	[TMPV770X_RESET_PII2C4]		= { 0x430, 0x530, 4, },
	[TMPV770X_RESET_PII2C5]		= { 0x430, 0x530, 5, },
	[TMPV770X_RESET_PII2C6]		= { 0x430, 0x530, 6, },
	[TMPV770X_RESET_PII2C7]		= { 0x430, 0x530, 7, },
	[TMPV770X_RESET_PII2C8]		= { 0x430, 0x530, 8, },
	[TMPV770X_RESET_PIPCMIF]	= { 0x464, 0x564, 0, },
	[TMPV770X_RESET_PICKMON]	= { 0x410, 0x510, 8, },
	[TMPV770X_RESET_SBUSCLK]	= { 0x414, 0x514, 0, },
};

static int visconti_clk_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct visconti_clk_provider *ctx;
	struct device *dev = &pdev->dev;
	struct regmap *regmap;
	int ret, i;

	regmap = syscon_node_to_regmap(np);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	ctx = visconti_init_clk(dev, regmap, TMPV770X_NR_CLK);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ret = visconti_register_reset_controller(dev, regmap, clk_reset_data,
						 TMPV770X_NR_RESET,
						 &visconti_reset_ops,
						 &tmpv770x_rst_lock);
	if (ret) {
		dev_err(dev, "Failed to register reset controller: %d\n", ret);
		return ret;
	}

	for (i = 0; i < (ARRAY_SIZE(fixed_clk_tables)); i++)
		ctx->clk_data.hws[fixed_clk_tables[i].id] =
			clk_hw_register_fixed_factor(NULL,
						     fixed_clk_tables[i].name,
						     fixed_clk_tables[i].parent,
						     fixed_clk_tables[i].flag,
						     fixed_clk_tables[i].mult,
						     fixed_clk_tables[i].div);

	ret = visconti_clk_register_gates(ctx, clk_gate_tables,
				    ARRAY_SIZE(clk_gate_tables), clk_reset_data,
				    &tmpv770x_clk_lock);
	if (ret) {
		dev_err(dev, "Failed to register main clock gate: %d\n", ret);
		return ret;
	}

	ret = visconti_clk_register_gates(ctx, pietherpll_clk_gate_tables,
				    ARRAY_SIZE(pietherpll_clk_gate_tables),
				    clk_reset_data, &tmpv770x_clk_lock);
	if (ret) {
		dev_err(dev, "Failed to register pietherpll clock gate: %d\n", ret);
		return ret;
	}

	return of_clk_add_hw_provider(np, of_clk_hw_onecell_get, &ctx->clk_data);
}

static const struct of_device_id visconti_clk_ids[] = {
	{ .compatible = "toshiba,tmpv7708-pismu", },
	{ }
};

static struct platform_driver visconti_clk_driver = {
	.probe  = visconti_clk_probe,
	.driver = {
		.name   = "visconti-clk",
		.of_match_table = visconti_clk_ids,
	},
};

builtin_platform_driver(visconti_clk_driver);
