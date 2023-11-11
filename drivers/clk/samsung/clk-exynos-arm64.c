// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Linaro Ltd.
 * Copyright (C) 2021 Dávid Virág <virag.david003@gmail.com>
 * Author: Sam Protsenko <semen.protsenko@linaro.org>
 * Author: Dávid Virág <virag.david003@gmail.com>
 *
 * This file contains shared functions used by some arm64 Exynos SoCs,
 * such as Exynos7885 or Exynos850 to register and init CMUs.
 */
#include <linux/clk.h>
#include <linux/of_address.h>

#include "clk-exynos-arm64.h"

/* Gate register bits */
#define GATE_MANUAL		BIT(20)
#define GATE_ENABLE_HWACG	BIT(28)

/* Gate register offsets range */
#define GATE_OFF_START		0x2000
#define GATE_OFF_END		0x2fff

/**
 * exynos_arm64_init_clocks - Set clocks initial configuration
 * @np:			CMU device tree node with "reg" property (CMU addr)
 * @reg_offs:		Register offsets array for clocks to init
 * @reg_offs_len:	Number of register offsets in reg_offs array
 *
 * Set manual control mode for all gate clocks.
 */
static void __init exynos_arm64_init_clocks(struct device_node *np,
		const unsigned long *reg_offs, size_t reg_offs_len)
{
	void __iomem *reg_base;
	size_t i;

	reg_base = of_iomap(np, 0);
	if (!reg_base)
		panic("%s: failed to map registers\n", __func__);

	for (i = 0; i < reg_offs_len; ++i) {
		void __iomem *reg = reg_base + reg_offs[i];
		u32 val;

		/* Modify only gate clock registers */
		if (reg_offs[i] < GATE_OFF_START || reg_offs[i] > GATE_OFF_END)
			continue;

		val = readl(reg);
		val |= GATE_MANUAL;
		val &= ~GATE_ENABLE_HWACG;
		writel(val, reg);
	}

	iounmap(reg_base);
}

/**
 * exynos_arm64_register_cmu - Register specified Exynos CMU domain
 * @dev:	Device object; may be NULL if this function is not being
 *		called from platform driver probe function
 * @np:		CMU device tree node
 * @cmu:	CMU data
 *
 * Register specified CMU domain, which includes next steps:
 *
 * 1. Enable parent clock of @cmu CMU
 * 2. Set initial registers configuration for @cmu CMU clocks
 * 3. Register @cmu CMU clocks using Samsung clock framework API
 */
void __init exynos_arm64_register_cmu(struct device *dev,
		struct device_node *np, const struct samsung_cmu_info *cmu)
{
	/* Keep CMU parent clock running (needed for CMU registers access) */
	if (cmu->clk_name) {
		struct clk *parent_clk;

		if (dev)
			parent_clk = clk_get(dev, cmu->clk_name);
		else
			parent_clk = of_clk_get_by_name(np, cmu->clk_name);

		if (IS_ERR(parent_clk)) {
			pr_err("%s: could not find bus clock %s; err = %ld\n",
			       __func__, cmu->clk_name, PTR_ERR(parent_clk));
		} else {
			clk_prepare_enable(parent_clk);
		}
	}

	exynos_arm64_init_clocks(np, cmu->clk_regs, cmu->nr_clk_regs);
	samsung_cmu_register_one(np, cmu);
}
