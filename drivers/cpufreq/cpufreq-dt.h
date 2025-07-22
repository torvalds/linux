/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2016 Linaro
 * Viresh Kumar <viresh.kumar@linaro.org>
 */

#ifndef __CPUFREQ_DT_H__
#define __CPUFREQ_DT_H__

#include <linux/types.h>

struct cpufreq_policy;

struct cpufreq_dt_platform_data {
	bool have_governor_per_policy;

	unsigned int	(*get_intermediate)(struct cpufreq_policy *policy,
					    unsigned int index);
	int		(*target_intermediate)(struct cpufreq_policy *policy,
					       unsigned int index);
	int (*suspend)(struct cpufreq_policy *policy);
	int (*resume)(struct cpufreq_policy *policy);
};

struct platform_device *cpufreq_dt_pdev_register(struct device *dev);

#endif /* __CPUFREQ_DT_H__ */
