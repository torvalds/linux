/*
 *  linux/arch/arm/mach-exynos/include/mach/gpufreq.h
 *
  *
  * Copyright (c) 2010 Samsung Electronics Co., Ltd.
  *	http://www.samsung.com
  *
  *
  * This program is free software; you can redistribute it and/or modify
  * it under the terms of the GNU General Public License version 2 as
  * published by the Free Software Foundation.
 */

#ifdef CONFIG_CPU_EXYNOS4210
extern int mali_dvfs_bottom_lock_push(void);
extern int mali_dvfs_bottom_lock_pop(void);

static inline int exynos_gpufreq_lock(int dummy_val)
{
	return mali_dvfs_bottom_lock_push();
}

static inline int exynos_gpufreq_unlock(void)
{
	return mali_dvfs_bottom_lock_pop();
}

#elif defined(CONFIG_CPU_EXYNOS4412)
extern int mali_dvfs_bottom_lock_push(int lock_step);
extern int mali_dvfs_bottom_lock_pop(void);

static inline int exynos_gpufreq_lock(int lock_step)
{
	return mali_dvfs_bottom_lock_push(lock_step);
}
static inline int exynos_gpufreq_unlock(void)
{
	return mali_dvfs_bottom_lock_pop();
}
#else
static inline int exynos_gpufreq_lock(void)
{
	return 0;
}
static inline int exynos_gpufreq_unlock(void)
{
	return 0;
}
#endif

