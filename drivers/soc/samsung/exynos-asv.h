/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 Samsung Electronics Co., Ltd.
 *	      http://www.samsung.com/
 * Author: Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * Samsung Exynos SoC Adaptive Supply Voltage support
 */
#ifndef __LINUX_SOC_EXYNOS_ASV_H
#define __LINUX_SOC_EXYNOS_ASV_H

struct regmap;

/* HPM, IDS values to select target group */
struct asv_limit_entry {
	unsigned int hpm;
	unsigned int ids;
};

struct exynos_asv_table {
	unsigned int num_rows;
	unsigned int num_cols;
	u32 *buf;
};

struct exynos_asv_subsys {
	struct exynos_asv *asv;
	const char *cpu_dt_compat;
	int id;
	struct exynos_asv_table table;

	unsigned int base_volt;
	unsigned int offset_volt_h;
	unsigned int offset_volt_l;
};

struct exynos_asv {
	struct device *dev;
	struct regmap *chipid_regmap;
	struct exynos_asv_subsys subsys[2];

	int (*opp_get_voltage)(const struct exynos_asv_subsys *subs,
			       int level, unsigned int voltage);
	unsigned int group;
	unsigned int table;

	/* True if SG fields from PKG_ID register should be used */
	bool use_sg;
	/* ASV bin read from DT */
	int of_bin;
};

static inline u32 __asv_get_table_entry(const struct exynos_asv_table *table,
					unsigned int row, unsigned int col)
{
	return table->buf[row * (table->num_cols) + col];
}

static inline u32 exynos_asv_opp_get_voltage(const struct exynos_asv_subsys *subsys,
					unsigned int level, unsigned int group)
{
	return __asv_get_table_entry(&subsys->table, level, group + 1);
}

static inline u32 exynos_asv_opp_get_frequency(const struct exynos_asv_subsys *subsys,
					unsigned int level)
{
	return __asv_get_table_entry(&subsys->table, level, 0);
}

#endif /* __LINUX_SOC_EXYNOS_ASV_H */
