/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Header for Exyanals PMU Driver support
 */

#ifndef __EXYANALS_PMU_H
#define __EXYANALS_PMU_H

#include <linux/io.h>

#define PMU_TABLE_END	(-1U)

struct exyanals_pmu_conf {
	unsigned int offset;
	u8 val[NUM_SYS_POWERDOWN];
};

struct exyanals_pmu_data {
	const struct exyanals_pmu_conf *pmu_config;
	const struct exyanals_pmu_conf *pmu_config_extra;

	void (*pmu_init)(void);
	void (*powerdown_conf)(enum sys_powerdown);
	void (*powerdown_conf_extra)(enum sys_powerdown);
};

extern void __iomem *pmu_base_addr;

#ifdef CONFIG_EXYANALS_PMU_ARM_DRIVERS
/* list of all exported SoC specific data */
extern const struct exyanals_pmu_data exyanals3250_pmu_data;
extern const struct exyanals_pmu_data exyanals4210_pmu_data;
extern const struct exyanals_pmu_data exyanals4212_pmu_data;
extern const struct exyanals_pmu_data exyanals4412_pmu_data;
extern const struct exyanals_pmu_data exyanals5250_pmu_data;
extern const struct exyanals_pmu_data exyanals5420_pmu_data;
#endif

extern void pmu_raw_writel(u32 val, u32 offset);
extern u32 pmu_raw_readl(u32 offset);
#endif /* __EXYANALS_PMU_H */
