/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Header for Exynos PMU Driver support
 */

#ifndef __EXYNOS_PMU_H
#define __EXYNOS_PMU_H

#include <linux/io.h>

#define PMU_TABLE_END	(-1U)

struct regmap_access_table;

struct exynos_pmu_conf {
	unsigned int offset;
	u8 val[NUM_SYS_POWERDOWN];
};

/**
 * struct exynos_pmu_data - of_device_id (match) data
 *
 * @pmu_config: Optional table detailing register writes for target system
 *              states: SYS_AFTR, SYS_LPA, SYS_SLEEP.
 * @pmu_config_extra: Optional secondary table detailing additional register
 *                    writes for target system states: SYS_AFTR, SYS_LPA,
 *                    SYS_SLEEP.
 * @pmu_secure: Whether or not PMU register writes need to be done via SMC call.
 * @pmu_cpuhp: Whether or not extra handling is required for CPU hotplug and
 *             CPUidle outside of standard PSCI calls, due to non-compliant
 *             firmware.
 * @pmu_init: Optional init function.
 * @powerdown_conf: Optional callback before entering target system states:
 *                  SYS_AFTR, SYS_LPA, SYS_SLEEP. This will be invoked before
 *                  the registers from @pmu_config are written.
 * @powerdown_conf_extra: Optional secondary callback before entering
 *                        target system states: SYS_AFTR, SYS_LPA, SYS_SLEEP.
 *                        This will be invoked after @pmu_config registers have
 *                        been written.
 * @rd_table: A table of readable register ranges in case a custom regmap is
 *            used (i.e. when @pmu_secure is @true).
 * @wr_table: A table of writable register ranges in case a custom regmap is
 *            used (i.e. when @pmu_secure is @true).
 */
struct exynos_pmu_data {
	const struct exynos_pmu_conf *pmu_config;
	const struct exynos_pmu_conf *pmu_config_extra;
	bool pmu_secure;
	bool pmu_cpuhp;

	void (*pmu_init)(void);
	void (*powerdown_conf)(enum sys_powerdown);
	void (*powerdown_conf_extra)(enum sys_powerdown);

	const struct regmap_access_table *rd_table;
	const struct regmap_access_table *wr_table;
};

extern void __iomem *pmu_base_addr;

#ifdef CONFIG_EXYNOS_PMU_ARM_DRIVERS
/* list of all exported SoC specific data */
extern const struct exynos_pmu_data exynos3250_pmu_data;
extern const struct exynos_pmu_data exynos4210_pmu_data;
extern const struct exynos_pmu_data exynos4212_pmu_data;
extern const struct exynos_pmu_data exynos4412_pmu_data;
extern const struct exynos_pmu_data exynos5250_pmu_data;
extern const struct exynos_pmu_data exynos5420_pmu_data;
#endif
extern const struct exynos_pmu_data gs101_pmu_data;

extern void pmu_raw_writel(u32 val, u32 offset);
extern u32 pmu_raw_readl(u32 offset);

int tensor_sec_reg_write(void *context, unsigned int reg, unsigned int val);
int tensor_sec_reg_read(void *context, unsigned int reg, unsigned int *val);
int tensor_sec_update_bits(void *context, unsigned int reg, unsigned int mask,
			   unsigned int val);

#endif /* __EXYNOS_PMU_H */
