/* drivers/gpu/t6xx/kbase/src/platform/mali_kbase_dvfs.h
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

#define MALI_DVFS_KEEP_STAY_CNT 10
#define MALI_DVFS_TIME_INTERVAL 5

#define MALI_DVFS_CURRENT_FREQ 0
#define MALI_DVFS_BL_CONFIG_FREQ 533
#define MALI_DVFS_START_FREQ 450

#ifdef CONFIG_MALI_T6XX_DVFS
#define CONFIG_MALI_T6XX_FREQ_LOCK
#ifdef CONFIG_CPU_FREQ
#define MALI_DVFS_ASV_ENABLE
#endif
#endif

struct regulator *kbase_platform_get_regulator(void);
int kbase_platform_regulator_init(void);
int kbase_platform_regulator_disable(void);
int kbase_platform_regulator_enable(void);
int kbase_platform_get_voltage(struct device *dev, int *vol);
int kbase_platform_set_voltage(struct device *dev, int vol);
void kbase_platform_dvfs_set_clock(kbase_device *kbdev, int freq);
int kbase_platform_dvfs_sprint_avs_table(char *buf);
int kbase_platform_dvfs_set(int enable);
void kbase_platform_dvfs_set_level(struct kbase_device *kbdev, int level);
int kbase_platform_dvfs_get_level(int freq);

#ifdef CONFIG_MALI_T6XX_DVFS
int kbase_platform_dvfs_init(struct kbase_device *dev);
void kbase_platform_dvfs_term(void);
int kbase_platform_dvfs_event(struct kbase_device *kbdev, u32 utilisation);
int kbase_platform_dvfs_get_enable_status(void);
int kbase_platform_dvfs_enable(bool enable, int freq);
int kbase_platform_dvfs_get_utilisation(void);
#endif

int mali_get_dvfs_current_level(void);
int mali_get_dvfs_upper_locked_freq(void);
int mali_get_dvfs_under_locked_freq(void);
int mali_dvfs_freq_lock(int level);
void mali_dvfs_freq_unlock(void);
int mali_dvfs_freq_under_lock(int level);
void mali_dvfs_freq_under_unlock(void);

ssize_t show_time_in_state(struct device *dev, struct device_attribute *attr, char *buf);
ssize_t set_time_in_state(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);

#endif /* _KBASE_DVFS_H_ */
