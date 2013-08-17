/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_platform.h
 * Platform specific Mali driver functions
 */

#ifndef __MALI_PLATFORM_H__
#define __MALI_PLATFORM_H__

#include "mali_osk.h"

#ifdef CONFIG_CPU_EXYNOS4210
#define MALI_DVFS_STEPS 2
#else
#define MALI_DVFS_STEPS 5
#endif

/* @Enable or Disable Mali GPU Bottom Lock feature */
#define MALI_GPU_BOTTOM_LOCK 1

#define MALI_VOLTAGE_LOCK 1

/* @Enable or Disable the CPU frequency lock when the GPU clock is 440 Mhz */
#define CPUFREQ_LOCK_DURING_440 0

#ifdef __cplusplus
extern "C" {
#endif

/** @brief description of power change reasons
 */
typedef enum mali_power_mode_tag
{
	MALI_POWER_MODE_ON,           /**< Power Mali on */
	MALI_POWER_MODE_LIGHT_SLEEP,  /**< Mali has been idle for a short time, or runtime PM suspend */
	MALI_POWER_MODE_DEEP_SLEEP,   /**< Mali has been idle for a long time, or OS suspend */
} mali_power_mode;

/** @brief Platform specific setup and initialisation of MALI
 *
 * This is called from the entrypoint of the driver to initialize the platform
 *
 * @return _MALI_OSK_ERR_OK on success otherwise, a suitable _mali_osk_errcode_t error.
 */
_mali_osk_errcode_t mali_platform_init(void);

/** @brief Platform specific deinitialisation of MALI
 *
 * This is called on the exit of the driver to terminate the platform
 *
 * @return _MALI_OSK_ERR_OK on success otherwise, a suitable _mali_osk_errcode_t error.
 */
_mali_osk_errcode_t mali_platform_deinit(void);

/** @brief Platform specific powerdown sequence of MALI
 *
 * Notification from the Mali device driver stating the new desired power mode.
 * MALI_POWER_MODE_ON must be obeyed, while the other modes are optional.
 * @param power_mode defines the power modes
 * @return _MALI_OSK_ERR_OK on success otherwise, a suitable _mali_osk_errcode_t error.
 */
_mali_osk_errcode_t mali_platform_power_mode_change(mali_power_mode power_mode);


/** @brief Platform specific handling of GPU utilization data
 *
 * When GPU utilization data is enabled, this function will be
 * periodically called.
 *
 * @param utilization The workload utilization of the Mali GPU. 0 = no utilization, 256 = full utilization.
 */
void mali_gpu_utilization_handler(u32 utilization);

/** @brief Setting the power domain of MALI
 *
 * This function sets the power domain of MALI if Linux run time power management is enabled
 *
 * @param dev Reference to struct platform_device (defined in linux) used by MALI GPU
 */
//void set_mali_parent_power_domain(void* dev);
void mali_utilization_suspend(void);

#ifdef CONFIG_REGULATOR
int mali_regulator_get_usecount(void);
void mali_regulator_disable(void);
void mali_regulator_enable(void);
void mali_regulator_set_voltage(int min_uV, int max_uV);
#endif
mali_bool mali_clk_set_rate(unsigned int clk, unsigned int mhz);
unsigned long mali_clk_get_rate(void);
void mali_clk_put(mali_bool binc_mali_clk);

#if MALI_PMM_RUNTIME_JOB_CONTROL_ON
_mali_osk_errcode_t mali_platform_powerdown(u32 cores);
_mali_osk_errcode_t mali_platform_powerup(u32 cores);
#endif


#if USING_MALI_PMM
#if MALI_POWER_MGMT_TEST_SUITE
/** @brief function to get status of individual cores
 *
 * This function is used by power management test suite to get the status of powered up/down the number
 * of cores
 * @param utilization The workload utilization of the Mali GPU. 0 = no utilization, 256 = full utilization.
 */
u32 pmu_get_power_up_down_info(void);
#endif
#endif

#if MALI_DVFS_ENABLED
mali_bool init_mali_dvfs_status(int step);
void deinit_mali_dvfs_status(void);
mali_bool mali_dvfs_handler(u32 utilization);
int mali_dvfs_is_running(void);
void mali_dvfs_late_resume(void);
int get_mali_dvfs_control_status(void);
mali_bool set_mali_dvfs_current_step(unsigned int step);
void mali_default_step_set(int step, mali_bool boostup);
int change_dvfs_tableset(int change_clk, int change_step);
#ifdef CONFIG_CPU_EXYNOS4210
#if MALI_GPU_BOTTOM_LOCK
int mali_dvfs_bottom_lock_push(void);
int mali_dvfs_bottom_lock_pop(void);
#endif
#else
int mali_dvfs_bottom_lock_push(int lock_step);
int mali_dvfs_bottom_lock_pop(void);
#endif
#endif

int mali_dvfs_get_vol(int step);

#if MALI_VOLTAGE_LOCK
int mali_voltage_lock_push(int lock_vol);
int mali_voltage_lock_pop(void);
int mali_voltage_lock_init(void);
int mali_vol_get_from_table(int vol);
#endif

#ifdef __cplusplus
}
#endif
#endif
