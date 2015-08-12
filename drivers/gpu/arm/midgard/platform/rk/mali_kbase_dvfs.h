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
 * 声明 平台相关的 mali_dvfs_facility 对外提供的接口, 比如初始化, 设置 gpu_clk_freq, ...
 * 但这里 并没有 实现良好封装. 
 *
 * .DP : mali_dvfs_facility : platform_dependent_part 中对 mali(gpu) DVFS 功能的具体实现.
 */

#ifndef _KBASE_DVFS_H_
#define _KBASE_DVFS_H_

/* Frequency that DVFS clock frequency decisions should be made */
#define KBASE_PM_DVFS_FREQUENCY                 100

#define MALI_DVFS_KEEP_STAY_CNT 10

/**
 * 一个门限, 当 counter_of_requests_to_jump_up_in_dvfs_level_table 到达该 value 的时候,
 * 才执行具体的将 current_dvfs_level 上跳的操作. 
 */
#define MALI_DVFS_UP_TIME_INTERVAL 1

/**
 * 一个门限, 当 counter_of_requests_to_jump_down_in_dvfs_level_table 到达该 value 的时候,
 * 才执行具体的将 current_dvfs_level 下跳的操作. 
 */
#define MALI_DVFS_DOWN_TIME_INTERVAL 2

/**
 * @see kbase_platform_dvfs_enable.
 */
#define MALI_DVFS_CURRENT_FREQ 0

#if 0
#define MALI_DVFS_BL_CONFIG_FREQ 500
#define MALI_DVFS_START_FREQ 400
#endif

/**
 * mali_dvfs_level_t, 某 mali_dvfs_level (功耗层级) 的具体配置信息. 
 */
typedef struct _mali_dvfs_info {
	/** 使用的电压.         .Q : 目前实际不起作用? */
	unsigned int voltage;
	/** 
	 * gpu_clock_freq. 当前 level 使用的 GPU 时钟频率. 以 KHz 为单位. 
	 */
	unsigned int clock;
	/** 
	 * 若 current_calculated_utilisation 低于本成员, 将可能下跳到 mali_dvfs_level_table 中, 临近的低功耗 mali_dvfs_level. 
	 */
	int min_threshold;
	/** 
	 * 若 current_calculated_utilisation 高于本成员, 将可能上跳到 mali_dvfs_level_table 中, 临近的高功耗 mali_dvfs_level. 
	 */
	int max_threshold;
	/** 
	 * total_time_in_this_level : gpu 停留在当前 level 上的 累计时间. 以 jiffy 为单位.
	 */
	unsigned long long time;
} mali_dvfs_info;

#define MALI_KHZ 1000
extern mali_dvfs_info *p_mali_dvfs_infotbl;
extern unsigned int MALI_DVFS_STEP;
#ifdef CONFIG_MALI_MIDGARD_DVFS
#define CONFIG_MALI_MIDGARD_FREQ_LOCK
#endif

/**
 * 将 gpu_clk 设置为 'freq', 'freq' 以 KHz 为单位. 
 */
void kbase_platform_dvfs_set_clock(struct kbase_device *kbdev, int freq);

/**
 * 命令 dvfs_module 为 gpu 配置 'level' 指定的 dvfs_level, 并具体生效.
 * @param level
 *      待使用的 mali_dvfs_level 在 mali_dvfs_level_table 中的 index.
 */
void kbase_platform_dvfs_set_level(struct kbase_device *kbdev, int level);

/**
 * 检索 mali_dvfs_level_table, 返回其中 gpu_clock_freq 精确是 'freq' 的 level_item 的 index.
 * 若没有找到, 返回 -1.
 * 'freq' 以 KHz 为单位.
 */
int kbase_platform_dvfs_get_level(int freq);

#ifdef CONFIG_MALI_MIDGARD_DVFS
/**
 * 初始化 mali_dvfs_facility.
 */
int kbase_platform_dvfs_init(struct kbase_device *dev);

/**
 * 中止化 mali_dvfs_facility.
 */
void kbase_platform_dvfs_term(void);
/*int kbase_platform_dvfs_event(struct kbase_device *kbdev, u32 utilisation);*/
/*int kbase_platform_dvfs_event(struct kbase_device *kbdev, u32 utilisation,u32 util_gl_share, u32 util_cl_share[2]);*/

/**
 * 返回当前 mali_dvfs 是否是开启, 即 common_parts 是否会回调通知 dvfs_event.
 */
int kbase_platform_dvfs_get_enable_status(void);

/**
 * 使能或者禁用 dvfs, 并将 gpu_clk 设置为 'freq'(最接近的 允许的 clk).
 * 若 'freq' 是 MALI_DVFS_CURRENT_FREQ, 则 "不" 改变当前的 gpu_clk_freq.
 */
int kbase_platform_dvfs_enable(bool enable, int freq);

/**
 * 返回 mali(gpu) 当前(最近的) utilisation.
 */
int kbase_platform_dvfs_get_utilisation(void);
#endif

/**
 * 返回 current_dvfs_level 在 mali_dvfs_level_table 中的 index.
 */
int mali_get_dvfs_current_level(void);

/**
 * 返回当前 dvfs_level_upper_limit 的 gpu_clk_freq, 以 KHz 为单位.
 * 若没有设置, 返回 -1.
 */
int mali_get_dvfs_upper_locked_freq(void);
/**
 * 返回当前 dvfs_level_lower_limit 的 gpu_clk_freq, 以 KHz 为单位.
 * 若没有设置, 返回 -1.
 */
int mali_get_dvfs_under_locked_freq(void);

/**
 * 将 'level' 设置为当前的 dvfs_level_upper_limit..
 * 这里用 "freq_lock" 不贴切.
 * @return 
 *      若成功, 返回 0.
 *      否则, 返回其他 value.
 */
int mali_dvfs_freq_lock(int level);
/**
 * 清除当前的 dvfs_level_upper_limit 设置. 
 */
void mali_dvfs_freq_unlock(void);
/**
 * 将 'level' 设置为当前的 dvfs_level_lower_limit.
 * @return 
 *      若成功, 返回 0.
 *      否则, 返回其他 value.
 */
int mali_dvfs_freq_under_lock(int level);
/**
 * 清除当前的 dvfs_level_lower_limit 设置. 
 */
void mali_dvfs_freq_under_unlock(void);

// @see 'time_in_state' in mali_kbase_platform.c.
ssize_t show_time_in_state(struct device *dev, struct device_attribute *attr, char *buf);
ssize_t set_time_in_state(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);

#endif				/* _KBASE_DVFS_H_ */
