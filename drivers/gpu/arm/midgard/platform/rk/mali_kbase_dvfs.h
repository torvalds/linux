/* drivers/gpu/midgard/platform/rk/mali_kbase_dvfs.h
 *
 * Rockchip SoC Mali-T764 DVFS driver
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
#define MALI_DVFS_TIME_INTERVAL 2

#define MALI_DVFS_CURRENT_FREQ 0
#if 0
#define MALI_DVFS_BL_CONFIG_FREQ 500
#define MALI_DVFS_START_FREQ 400
#endif
typedef struct _mali_dvfs_info {
	unsigned int voltage;
	unsigned int clock;
	int min_threshold;
	int max_threshold;
	unsigned long long time;
} mali_dvfs_info;

#define MALI_KHZ 1000
extern mali_dvfs_info *p_mali_dvfs_infotbl;
extern unsigned int MALI_DVFS_STEP;
#ifdef CONFIG_MALI_MIDGARD_DVFS
#define CONFIG_MALI_MIDGARD_FREQ_LOCK
#endif

void kbase_platform_dvfs_set_clock(kbase_device *kbdev, int freq);
void kbase_platform_dvfs_set_level(struct kbase_device *kbdev, int level);
int kbase_platform_dvfs_get_level(int freq);

#ifdef CONFIG_MALI_MIDGARD_DVFS
int kbase_platform_dvfs_init(struct kbase_device *dev);
void kbase_platform_dvfs_term(void);
/*int kbase_platform_dvfs_event(struct kbase_device *kbdev, u32 utilisation);*/
/*int kbase_platform_dvfs_event(struct kbase_device *kbdev, u32 utilisation,u32 util_gl_share, u32 util_cl_share[2]);*/
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

#endif				/* _KBASE_DVFS_H_ */
