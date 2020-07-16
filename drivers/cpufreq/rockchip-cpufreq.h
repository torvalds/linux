/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 */
#ifndef __ROCKCHIP_CPUFREQ_H
#define __ROCKCHIP_CPUFREQ_H

#if IS_ENABLED(CONFIG_ARM_ROCKCHIP_CPUFREQ)
unsigned int rockchip_cpufreq_adjust_target(int cpu, unsigned int freq);
int rockchip_cpufreq_check_rate_volt(struct device *dev);
int rockchip_cpufreq_set_opp_info(struct device *dev);
void rockchip_cpufreq_put_opp_info(struct device *dev);
int rockchip_cpufreq_adjust_power_scale(struct device *dev);
int rockchip_cpufreq_suspend(struct cpufreq_policy *policy);
#else
static inline unsigned int rockchip_cpufreq_adjust_target(int cpu,
							  unsigned int freq)
{
	return freq;
}

static inline int rockchip_cpufreq_check_rate_volt(struct device *dev)
{
	return -ENOTSUPP;
}

static inline int rockchip_cpufreq_set_opp_info(struct device *dev)
{
	return -ENOTSUPP;
}

static inline void rockchip_cpufreq_put_opp_info(struct device *dev)
{
}

static inline int rockchip_cpufreq_adjust_power_scale(struct device *dev)
{
	return -ENOTSUPP;
}

static inline int rockchip_cpufreq_suspend(struct cpufreq_policy *policy)
{
	return -ENOTSUPP;
}

#endif /* CONFIG_ARM_ROCKCHIP_CPUFREQ */

#endif
