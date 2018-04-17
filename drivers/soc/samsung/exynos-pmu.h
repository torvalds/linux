/*
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Header for EXYNOS PMU Driver support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __EXYNOS_PMU_H
#define __EXYNOS_PMU_H

#include <linux/io.h>

#define PMU_TABLE_END	(-1U)

struct exynos_pmu_conf {
	unsigned int offset;
	u8 val[NUM_SYS_POWERDOWN];
};

struct exynos_pmu_data {
	const struct exynos_pmu_conf *pmu_config;

	void (*pmu_init)(void);
	void (*powerdown_conf)(enum sys_powerdown);
	void (*powerdown_conf_extra)(enum sys_powerdown);
};

extern void __iomem *pmu_base_addr;

#ifdef CONFIG_EXYNOS_PMU_ARM_DRIVERS
/* list of all exported SoC specific data */
extern const struct exynos_pmu_data exynos3250_pmu_data;
extern const struct exynos_pmu_data exynos4210_pmu_data;
extern const struct exynos_pmu_data exynos4412_pmu_data;
extern const struct exynos_pmu_data exynos5250_pmu_data;
extern const struct exynos_pmu_data exynos5420_pmu_data;
#endif

extern void pmu_raw_writel(u32 val, u32 offset);
extern u32 pmu_raw_readl(u32 offset);
#endif /* __EXYNOS_PMU_H */
