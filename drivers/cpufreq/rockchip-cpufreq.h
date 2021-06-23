/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 */
#ifndef __ROCKCHIP_CPUFREQ_H
#define __ROCKCHIP_CPUFREQ_H

#if IS_ENABLED(CONFIG_ARM_ROCKCHIP_CPUFREQ)
int rockchip_cpufreq_adjust_power_scale(struct device *dev);
#else
static inline int rockchip_cpufreq_adjust_power_scale(struct device *dev)
{
	return -ENOTSUPP;
}
#endif /* CONFIG_ARM_ROCKCHIP_CPUFREQ */

#endif
