/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
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
#ifndef USING_MALI_PMM
/* @brief System power up/down cores that can be passed into mali_platform_powerdown/up() */
#define MALI_PLATFORM_SYSTEM  0
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** @brief description of power change reasons
 */
typedef enum mali_power_mode_tag
{
	MALI_POWER_MODE_ON,
	MALI_POWER_MODE_LIGHT_SLEEP,
	MALI_POWER_MODE_DEEP_SLEEP,
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
 * Call as part of platform init if there is no PMM support, else the
 * PMM will call it.
 * There are three power modes defined:
 *  1) MALI_POWER_MODE_ON
 *  2) MALI_POWER_MODE_LIGHT_SLEEP
 *  3) MALI_POWER_MODE_DEEP_SLEEP
 * MALI power management module transitions to MALI_POWER_MODE_LIGHT_SLEEP mode when MALI is idle
 * for idle timer (software timer defined in mali_pmm_policy_jobcontrol.h) duration, MALI transitions
 * to MALI_POWER_MODE_LIGHT_SLEEP mode during timeout if there are no more jobs queued.
 * MALI power management module transitions to MALI_POWER_MODE_DEEP_SLEEP mode when OS does system power
 * off.
 * Customer has to add power down code when MALI transitions to MALI_POWER_MODE_LIGHT_SLEEP or MALI_POWER_MODE_DEEP_SLEEP
 * mode.
 * MALI_POWER_MODE_ON mode is entered when the MALI is to powered up. Some customers want to control voltage regulators during
 * the whole system powers on/off. Customer can track in this function whether the MALI is powered up from
 * MALI_POWER_MODE_LIGHT_SLEEP or MALI_POWER_MODE_DEEP_SLEEP mode and manage the voltage regulators as well.
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
void mali_utilization_suspend(void);

#ifdef CONFIG_REGULATOR
unsigned int mali_regulator_get_usecount(void);
void mali_regulator_disable(void);
void mali_regulator_enable(void);
void mali_regulator_set_voltage(int min_uV, int max_uV);
#endif

mali_bool mali_clk_set_rate(unsigned int clk, unsigned int mhz);
unsigned long mali_clk_get_rate(void);
void mali_clk_put(mali_bool binc_mali_clk);

#ifdef MALI_PMM_RUNTIME_JOB_CONTROL_ON
_mali_osk_errcode_t mali_platform_powerdown(u32 cores);
_mali_osk_errcode_t mali_platform_powerup(u32 cores);
#endif

#ifdef CONFIG_MALI_DVFS
#define MALI_DVFS_STEPS 5
mali_bool init_mali_dvfs_status(int step);
void deinit_mali_dvfs_status(void);
mali_bool mali_dvfs_handler(u32 utilization);
int mali_dvfs_is_running(void);
void mali_dvfs_late_resume(void);
int get_mali_dvfs_control_status(void);
mali_bool set_mali_dvfs_current_step(unsigned int step);
void mali_default_step_set(int step, mali_bool boostup);
int change_dvfs_tableset(int change_clk, int change_step);
#endif

void mali_set_runtime_resume_params(int clk, int volt);

#ifdef __cplusplus
}
#endif
#endif
