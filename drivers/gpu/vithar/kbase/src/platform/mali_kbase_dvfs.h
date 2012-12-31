/* drivers/gpu/vithar/kbase/src/platform/mali_kbase_dvfs.h
 *
 * Copyright 2011 by S.LSI. Samsung Electronics Inc.
 * San#24, Nongseo-Dong, Giheung-Gu, Yongin, Korea
 *
 * Samsung SoC Mali-T604 DVFS driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software FoundatIon.
 */

/**
 * @file mali_kbase_dvfs.h
 * DVFS
 */

#ifndef _KBASE_DVFS_H_
#define _KBASE_DVFS_H_

/* Frequency that DVFS clock frequency decisions should be made */
#define KBASE_PM_DVFS_FREQUENCY                 100

#define MALI_DVFS_DEBUG 0
#define MALI_DVFS_START_MAX_STEP 1

#define	MALI_DVFS_STEP 4

#define MALI_DVFS_KEEP_STAY_CNT 10

struct regulator *kbase_platform_get_regulator(void);
int kbase_platform_regulator_init(struct device *dev);
int kbase_platform_regulator_disable(struct device *dev);
int kbase_platform_regulator_enable(struct device *dev);
int kbase_platform_get_default_voltage(struct device *dev, int *vol);
int kbase_platform_get_voltage(struct device *dev, int *vol);
int kbase_platform_set_voltage(struct device *dev, int vol);

#ifdef CONFIG_VITHAR_DVFS
int kbase_platform_dvfs_init(struct device *dev, int step);
void kbase_platform_dvfs_term(void);
int kbase_platform_dvfs_event(kbase_device *kbdev, u32 utilisation);
int kbase_platform_dvfs_get_control_status(void);
int kbase_pm_get_dvfs_utilisation(kbase_device *kbdev);
#endif

#endif /* _KBASE_DVFS_H_ */
