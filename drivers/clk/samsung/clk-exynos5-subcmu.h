/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __CLK_EXYNOS5_SUBCMU_H
#define __CLK_EXYNOS5_SUBCMU_H

struct exynos5_subcmu_reg_dump {
	u32 offset;
	u32 value;
	u32 mask;
	u32 save;
};

struct exynos5_subcmu_info {
	const struct samsung_div_clock *div_clks;
	unsigned int nr_div_clks;
	const struct samsung_gate_clock *gate_clks;
	unsigned int nr_gate_clks;
	struct exynos5_subcmu_reg_dump *suspend_regs;
	unsigned int nr_suspend_regs;
	const char *pd_name;
};

void exynos5_subcmus_init(struct samsung_clk_provider *ctx, int nr_cmus,
			  const struct exynos5_subcmu_info *cmu);

#endif
