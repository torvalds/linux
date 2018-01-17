/*
 *
 * (C) COPYRIGHT 2014-2015 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 */

/**
 * @file mali_kbase_config_platform.h
 * 声明 platform_config_of_rk (platform_rk 的 platform_config).
 */

/**
 * Maximum frequency GPU will be clocked at.
 * Given in kHz.
 * This must be specified as there is no default value.
 *
 * Attached value: number in kHz
 * Default value: NA
 */
#define GPU_FREQ_KHZ_MAX (5000)

/**
 * Minimum frequency GPU will be clocked at.
 * Given in kHz.
 * This must be specified as there is no default value.
 *
 * Attached value: number in kHz
 * Default value: NA
 */
#define GPU_FREQ_KHZ_MIN (5000)

/**
 * CPU_SPEED_FUNC
 * - A pointer to a function that calculates the CPU clock
 *
 * CPU clock speed of the platform is in MHz
 * - see kbase_cpu_clk_speed_func for the function prototype.
 *
 * Attached value: A kbase_cpu_clk_speed_func.
 * Default Value:  NA
 */
#define CPU_SPEED_FUNC (NULL)

/**
 * GPU_SPEED_FUNC
 * - A pointer to a function that calculates the GPU clock
 *
 * GPU clock speed of the platform in MHz
 * - see kbase_gpu_clk_speed_func for the function prototype.
 *
 * Attached value: A kbase_gpu_clk_speed_func.
 * Default Value:  NA
 */
#define GPU_SPEED_FUNC (NULL)

/**
 * Power management configuration
 *
 * Attached value:
 *	pointer to @ref kbase_pm_callback_conf
 * Default value:
 *	See @ref kbase_pm_callback_conf
 */
#define POWER_MANAGEMENT_CALLBACKS (&pm_callbacks)
extern struct kbase_pm_callback_conf pm_callbacks;

/**
 * Platform specific configuration functions
 *
 * Attached value:
 *	pointer to @ref kbase_platform_funcs_conf
 * Default value:
 *	See @ref kbase_platform_funcs_conf
 */
#define PLATFORM_FUNCS (&platform_funcs)
extern struct kbase_platform_funcs_conf platform_funcs;

/**
 * Secure mode switch
 *
 * Attached value: pointer to @ref kbase_secure_ops
 */
#define SECURE_CALLBACKS (NULL)

