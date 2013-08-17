/* drivers/gpu/t6xx/kbase/src/platform/mali_kbase_platform.h
 *
 * Copyright 2011 by S.LSI. Samsung Electronics Inc.
 * San#24, Nongseo-Dong, Giheung-Gu, Yongin, Korea
 *
 * Samsung SoC Mali-T604 platform-dependent codes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software FoundatIon.
 */

/**
 * @file mali_kbase_platform.h
 * Platform-dependent init
 */

#ifndef _KBASE_PLATFORM_H_
#define _KBASE_PLATFORM_H_

struct exynos_context
{
	/** Indicator if system clock to mail-t604 is active */
	int cmu_pmu_status;
	/** cmd & pmu lock */
	spinlock_t cmu_pmu_lock;
	struct clk *sclk_g3d;
#ifdef CONFIG_MALI_T6XX_DVFS
	/*To calculate utilization for x sec*/
	int time_tick;
	int utilisation;
	u32 time_busy;
	u32 time_idle;
#endif
};

/* All things that are needed for the Linux port. */
int kbase_platform_cmu_pmu_control(struct kbase_device *kbdev, int control);
int kbase_platform_create_sysfs_file(struct device *dev);
void kbase_platform_remove_sysfs_file(struct device *dev);
int kbase_platform_is_power_on(void);
mali_error kbase_platform_init(struct kbase_device *kbdev);
void kbase_platform_term(kbase_device *kbdev);

int kbase_platform_clock_on(struct kbase_device *kbdev);
int kbase_platform_clock_off(struct kbase_device *kbdev);

#endif /* _KBASE_PLATFORM_H_ */
