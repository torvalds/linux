// SPDX-License-Identifier: GPL-2.0-only
#include <linux/bits.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/units.h>

#include <dt-bindings/clock/marvell,pxa1908.h>

#include "clk.h"

#define MPMU_UART_PLL		0x14

#define MPMU_NR_CLKS		39

struct pxa1908_clk_unit {
	struct mmp_clk_unit unit;
	void __iomem *base;
};

static struct mmp_param_fixed_rate_clk fixed_rate_clks[] = {
	{PXA1908_CLK_CLK32, "clk32", NULL, 0, 32768},
	{PXA1908_CLK_VCTCXO, "vctcxo", NULL, 0, 26 * HZ_PER_MHZ},
	{PXA1908_CLK_PLL1_624, "pll1_624", NULL, 0, 624 * HZ_PER_MHZ},
	{PXA1908_CLK_PLL1_416, "pll1_416", NULL, 0, 416 * HZ_PER_MHZ},
	{PXA1908_CLK_PLL1_499, "pll1_499", NULL, 0, 499 * HZ_PER_MHZ},
	{PXA1908_CLK_PLL1_832, "pll1_832", NULL, 0, 832 * HZ_PER_MHZ},
	{PXA1908_CLK_PLL1_1248, "pll1_1248", NULL, 0, 1248 * HZ_PER_MHZ},
};

static struct mmp_param_fixed_factor_clk fixed_factor_clks[] = {
	{PXA1908_CLK_PLL1_D2, "pll1_d2", "pll1_624", 1, 2, 0},
	{PXA1908_CLK_PLL1_D4, "pll1_d4", "pll1_d2", 1, 2, 0},
	{PXA1908_CLK_PLL1_D6, "pll1_d6", "pll1_d2", 1, 3, 0},
	{PXA1908_CLK_PLL1_D8, "pll1_d8", "pll1_d4", 1, 2, 0},
	{PXA1908_CLK_PLL1_D12, "pll1_d12", "pll1_d6", 1, 2, 0},
	{PXA1908_CLK_PLL1_D13, "pll1_d13", "pll1_624", 1, 13, 0},
	{PXA1908_CLK_PLL1_D16, "pll1_d16", "pll1_d8", 1, 2, 0},
	{PXA1908_CLK_PLL1_D24, "pll1_d24", "pll1_d12", 1, 2, 0},
	{PXA1908_CLK_PLL1_D48, "pll1_d48", "pll1_d24", 1, 2, 0},
	{PXA1908_CLK_PLL1_D96, "pll1_d96", "pll1_d48", 1, 2, 0},
	{PXA1908_CLK_PLL1_32, "pll1_32", "pll1_d13", 2, 3, 0},
	{PXA1908_CLK_PLL1_208, "pll1_208", "pll1_d2", 2, 3, 0},
	{PXA1908_CLK_PLL1_117, "pll1_117", "pll1_624", 3, 16, 0},
};

static struct u32_fract uart_factor_tbl[] = {
	{.numerator = 8125, .denominator = 1536},	/* 14.745MHz */
};

static struct mmp_clk_factor_masks uart_factor_masks = {
	.factor = 2,
	.num_mask = GENMASK(12, 0),
	.den_mask = GENMASK(12, 0),
	.num_shift = 16,
	.den_shift = 0,
};

static void pxa1908_pll_init(struct pxa1908_clk_unit *pxa_unit)
{
	struct mmp_clk_unit *unit = &pxa_unit->unit;

	mmp_register_fixed_rate_clks(unit, fixed_rate_clks,
					ARRAY_SIZE(fixed_rate_clks));

	mmp_register_fixed_factor_clks(unit, fixed_factor_clks,
					ARRAY_SIZE(fixed_factor_clks));

	mmp_clk_register_factor("uart_pll", "pll1_d4",
			CLK_SET_RATE_PARENT,
			pxa_unit->base + MPMU_UART_PLL,
			&uart_factor_masks, uart_factor_tbl,
			ARRAY_SIZE(uart_factor_tbl), NULL);
}

static int pxa1908_mpmu_probe(struct platform_device *pdev)
{
	struct pxa1908_clk_unit *pxa_unit;

	pxa_unit = devm_kzalloc(&pdev->dev, sizeof(*pxa_unit), GFP_KERNEL);
	if (!pxa_unit)
		return -ENOMEM;

	pxa_unit->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pxa_unit->base))
		return PTR_ERR(pxa_unit->base);

	mmp_clk_init(pdev->dev.of_node, &pxa_unit->unit, MPMU_NR_CLKS);

	pxa1908_pll_init(pxa_unit);

	return 0;
}

static const struct of_device_id pxa1908_mpmu_match_table[] = {
	{ .compatible = "marvell,pxa1908-mpmu" },
	{ }
};
MODULE_DEVICE_TABLE(of, pxa1908_mpmu_match_table);

static struct platform_driver pxa1908_mpmu_driver = {
	.probe = pxa1908_mpmu_probe,
	.driver = {
		.name = "pxa1908-mpmu",
		.of_match_table = pxa1908_mpmu_match_table
	}
};
module_platform_driver(pxa1908_mpmu_driver);

MODULE_AUTHOR("Duje Mihanović <duje.mihanovic@skole.hr>");
MODULE_DESCRIPTION("Marvell PXA1908 MPMU Clock Driver");
MODULE_LICENSE("GPL");
