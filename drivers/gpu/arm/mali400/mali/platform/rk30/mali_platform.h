/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2009-2012 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

/**
 * @file mali_platform.h
 * Platform specific Mali driver functions
 */

#ifndef __MALI_PLATFORM_H__
#define __MALI_PLATFORM_H__

#include "mali_dvfs.h"
#include "mali_osk.h"
#include <linux/mali/mali_utgard.h>
#include <linux/rockchip/dvfs.h>
#include <linux/cpufreq.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief
 * description of power change reasons
 */
enum mali_power_mode {
	MALI_POWER_MODE_ON,           /**< Power Mali on */
	MALI_POWER_MODE_LIGHT_SLEEP,  /**< Mali has been idle for a short time, or runtime PM suspend */
	MALI_POWER_MODE_DEEP_SLEEP,   /**< Mali has been idle for a long time, or OS suspend */
};

/**
 * dvfs_level_t, 标识一个 dvfs_level (的具体配置).
 * .DP : dvfs_level.
 */
struct mali_fv_info {
	/**
	 * 当前 dvfs_level 使用的 gpu_clk_freq.
	 */
	unsigned long freq;
	/**
	 * .DP : min_mali_utilization_in_percentage_in_this_level.
	 * 若当前的 mali_utilization_in_percentage
	 *	小于 min_mali_utilization_in_percentage_in_this_level,
	 * 则触发一次 requests_to_jump_down_in_dvfs_level_table,
	 * 当 对 requests_to_jump_down_in_dvfs_level_table 的连续计数
	 * (.DP : continuous_count_of_requests_to_jump_down) 达到一定数值,
	 * 则 dvfs_facility 会下跳一个 level.
	 */
	unsigned int min;
	/**
	 * .DP : max_mali_utilization_in_percentage_in_this_level.
	 * 若当前的 mali_utilization_in_percentage
	 *	大于 max_mali_utilization_in_percentage_in_this_level,
	 * 则触发一次 requests_to_jump_up_in_dvfs_level_table,
	 * 当 对 requests_to_jump_up_in_dvfs_level_table 的连续计数
	 * (.DP : continuous_count_of_requests_to_jump_up) 达到一定数值,
	 *	则 dvfs_facility 会上跳一个 level.
	 */
	unsigned int max;
};

/**
 * mali_driver_private_data_t.
 * 和 平台相关的 mali_driver 的私有数据,
 *		包含 clk, power_domain handles, mali_dvfs_facility 等.
 *
 * 该类型在 platform_dependent_part 中定义,
 * 显然也只会在 platform_dependent_part 中使用.
 */
struct mali_platform_drv_data {
	/**
	 * gpu_dvfs_node
	 */
	struct dvfs_node *clk;

	/**
	 * gpu_power_domain.
	 */
	struct clk *pd;

	/**
	 * available_dvfs_level_list.
	 * 将用于保存 系统配置支持的所有 dvfs_level.
	 * .R : 实际上, 放在 mali_dvfs_context 中为宜.
	 */
	struct mali_fv_info *fv_info;
	/**
	 * len_of_available_dvfs_level_list, 也即 根据系统配置得到的 available_dvfs_level 的个数.
	 */
	unsigned int fv_info_length;

	/**
	 * mali_dvfs_context.
	 */
	struct mali_dvfs dvfs;

	/**
	 * device_of_mali_gpu.
	 */
	struct device *dev;

	/**
	 * gpu 是否 "被上电, 且被送入 clk".
	 */
	bool power_state;

	_mali_osk_mutex_t *clock_set_lock;
};

/** @brief Platform specific setup and initialisation of MALI
 *
 * This is called from the entrypoint of the driver to initialize the platform
 *
 * @return _MALI_OSK_ERR_OK on success otherwise, a suitable _mali_osk_errcode_t error.
 */
_mali_osk_errcode_t mali_platform_init(struct platform_device *pdev);

/** @brief Platform specific deinitialisation of MALI
 *
 * This is called on the exit of the driver to terminate the platform
 *
 * @return _MALI_OSK_ERR_OK on success otherwise, a suitable _mali_osk_errcode_t error.
 */
_mali_osk_errcode_t mali_platform_deinit(struct platform_device *pdev);

/** @brief Platform specific powerdown sequence of MALI
 *
 * Notification from the Mali device driver stating the new desired power mode.
 * MALI_POWER_MODE_ON must be obeyed, while the other modes are optional.
 * @param power_mode defines the power modes
 * @return _MALI_OSK_ERR_OK on success otherwise, a suitable _mali_osk_errcode_t error.
 */
_mali_osk_errcode_t mali_platform_power_mode_change(
			enum mali_power_mode power_mode);


/**
 * @brief
 * Platform specific handling
 *      of GPU utilization data
 *
 * When GPU utilization data is enabled,
 * this function
 * will be periodically called.
 *
 * @param utilization
 *      The workload utilization
 *              of the Mali GPU.
 *      0 = no utilization,
 *      256 = full utilization.
 */
void mali_gpu_utilization_handler(struct mali_gpu_utilization_data *data);

int mali_set_level(struct device *dev, int level);

#ifdef __cplusplus
}
#endif
#endif
