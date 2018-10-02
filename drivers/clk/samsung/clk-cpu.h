/*
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Common Clock Framework support for all PLL's in Samsung platforms
*/

#ifndef __SAMSUNG_CLK_CPU_H
#define __SAMSUNG_CLK_CPU_H

#include "clk.h"

/**
 * struct exynos_cpuclk_data: config data to setup cpu clocks.
 * @prate: frequency of the primary parent clock (in KHz).
 * @div0: value to be programmed in the div_cpu0 register.
 * @div1: value to be programmed in the div_cpu1 register.
 *
 * This structure holds the divider configuration data for dividers in the CPU
 * clock domain. The parent frequency at which these divider values are valid is
 * specified in @prate. The @prate is the frequency of the primary parent clock.
 * For CPU clock domains that do not have a DIV1 register, the @div1 member
 * value is not used.
 */
struct exynos_cpuclk_cfg_data {
	unsigned long	prate;
	unsigned long	div0;
	unsigned long	div1;
};

/**
 * struct exynos_cpuclk: information about clock supplied to a CPU core.
 * @hw:	handle between CCF and CPU clock.
 * @alt_parent: alternate parent clock to use when switching the speed
 *	of the primary parent clock.
 * @ctrl_base:	base address of the clock controller.
 * @lock: cpu clock domain register access lock.
 * @cfg: cpu clock rate configuration data.
 * @num_cfgs: number of array elements in @cfg array.
 * @clk_nb: clock notifier registered for changes in clock speed of the
 *	primary parent clock.
 * @flags: configuration flags for the CPU clock.
 *
 * This structure holds information required for programming the CPU clock for
 * various clock speeds.
 */
struct exynos_cpuclk {
	struct clk_hw				hw;
	struct clk_hw				*alt_parent;
	void __iomem				*ctrl_base;
	spinlock_t				*lock;
	const struct exynos_cpuclk_cfg_data	*cfg;
	const unsigned long			num_cfgs;
	struct notifier_block			clk_nb;
	unsigned long				flags;

/* The CPU clock registers have DIV1 configuration register */
#define CLK_CPU_HAS_DIV1		(1 << 0)
/* When ALT parent is active, debug clocks need safe divider values */
#define CLK_CPU_NEEDS_DEBUG_ALT_DIV	(1 << 1)
/* The CPU clock registers have Exynos5433-compatible layout */
#define CLK_CPU_HAS_E5433_REGS_LAYOUT	(1 << 2)
};

extern int __init exynos_register_cpu_clock(struct samsung_clk_provider *ctx,
			unsigned int lookup_id, const char *name,
			const char *parent, const char *alt_parent,
			unsigned long offset,
			const struct exynos_cpuclk_cfg_data *cfg,
			unsigned long num_cfgs, unsigned long flags);

#endif /* __SAMSUNG_CLK_CPU_H */
