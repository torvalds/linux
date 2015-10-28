/*
 * Rockchip SoC Mali-450 DVFS driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software FoundatIon.
 */
#include <linux/workqueue.h>
#include <linux/types.h>
#include <linux/device.h>

#ifndef _MALI_DVFS_H_
#define _MALI_DVFS_H_


/**
 * mali_dvfs_context_t, context of mali_dvfs_facility.
 */
struct mali_dvfs {
	/**
	 * work_to_handle_mali_utilization_event.
	 */
	struct work_struct work;
	/**
	 * current_mali_utilization.
	 */
	unsigned int utilisation;
	/**
	 * index_of_current_dvfs_level.
	 * current_dvfs_level 在 available_dvfs_level_list 中的 index.
	 */
	unsigned int current_level;

	/**
	 * mali_dvfs_facility 是否被使能.
	 */
	bool enabled;

	/**
	 * count_of_continuous_requests_to_jump_up_in_dvfs_level_table.
	 * 对 "连续" 的 requests_to_jump_up_in_dvfs_level_table 计数.
	 */
	unsigned int m_count_of_requests_to_jump_up;

	/**
	 * count_of_continuous_requests_to_jump_down_in_dvfs_level_table.
	 * 对 "连续" 的 requests_to_jump_down_in_dvfs_level_table 计数.
	 */
	unsigned int m_count_of_requests_to_jump_down;
};

int mali_dvfs_init(struct device *dev);

void mali_dvfs_term(struct device *dev);

bool mali_dvfs_is_enabled(struct device *dev);

void mali_dvfs_enable(struct device *dev);

void mali_dvfs_disable(struct device *dev);

unsigned int mali_dvfs_utilisation(struct device *dev);

int mali_dvfs_event(struct device *dev, u32 utilisation);

#endif		/*_MALI_DVFS_H_*/
