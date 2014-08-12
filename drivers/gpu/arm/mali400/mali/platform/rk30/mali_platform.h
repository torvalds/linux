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

#include "mali_osk.h"
#include <linux/mali/mali_utgard.h>
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
void mali_gpu_utilization_handler(struct mali_gpu_utilization_data *data);

/** @brief Setting the power domain of MALI
 *
 * This function sets the power domain of MALI if Linux run time power management is enabled
 *
 * @param dev Reference to struct platform_device (defined in linux) used by MALI GPU
 */
void set_mali_parent_power_domain(void* dev);

#ifdef __cplusplus
}
#endif
#endif
