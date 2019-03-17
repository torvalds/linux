/*
 * Copyright (C) 2010-2017 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_osk_mali.h
 * Defines the OS abstraction layer which is specific for the Mali kernel device driver (OSK)
 */

#ifndef __MALI_OSK_MALI_H__
#define __MALI_OSK_MALI_H__

#include <linux/mali/mali_utgard.h>
#include <mali_osk.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_MALI_DEVFREQ
struct mali_device {
	struct device *dev;
#ifdef CONFIG_HAVE_CLK
	struct clk *clock;
#endif
#ifdef CONFIG_REGULATOR
	struct regulator *regulator;
#endif
#ifdef CONFIG_PM_DEVFREQ
	struct devfreq_dev_profile devfreq_profile;
	struct devfreq *devfreq;
	unsigned long current_freq;
	unsigned long current_voltage;
	struct monitor_dev_info *mdev_info;
#ifdef CONFIG_DEVFREQ_THERMAL
	struct thermal_cooling_device *devfreq_cooling;
#endif
#endif
	struct mali_pm_metrics_data mali_metrics;
};
#endif

/** @addtogroup _mali_osk_miscellaneous
 * @{ */

/** @brief Struct with device specific configuration data
 */
typedef struct mali_gpu_device_data _mali_osk_device_data;

#ifdef CONFIG_MALI_DT
/** @brief Initialize those device resources when we use device tree
 *
 * @return _MALI_OSK_ERR_OK on success, otherwise failure.
 */
_mali_osk_errcode_t _mali_osk_resource_initialize(void);
#endif

/** @brief Find Mali GPU HW resource
 *
 * @param addr Address of Mali GPU resource to find
 * @param res Storage for resource information if resource is found.
 * @return _MALI_OSK_ERR_OK on success, _MALI_OSK_ERR_ITEM_NOT_FOUND if resource is not found
 */
_mali_osk_errcode_t _mali_osk_resource_find(u32 addr, _mali_osk_resource_t *res);


/** @brief Find Mali GPU HW base address
 *
 * @return 0 if resources are found, otherwise the Mali GPU component with lowest address.
 */
uintptr_t _mali_osk_resource_base_address(void);

/** @brief Find the specific GPU resource.
 *
 * @return value
 * 0x400 if Mali 400 specific GPU resource identified
 * 0x450 if Mali 450 specific GPU resource identified
 * 0x470 if Mali 470 specific GPU resource identified
 *
 */
u32 _mali_osk_identify_gpu_resource(void);

/** @brief Retrieve the Mali GPU specific data
 *
 * @return _MALI_OSK_ERR_OK on success, otherwise failure.
 */
_mali_osk_errcode_t _mali_osk_device_data_get(_mali_osk_device_data *data);

/** @brief Find the pmu domain config from device data.
 *
 * @param domain_config_array used to store pmu domain config found in device data.
 * @param array_size is the size of array domain_config_array.
 */
void _mali_osk_device_data_pmu_config_get(u16 *domain_config_array, int array_size);

/** @brief Get Mali PMU switch delay
 *
 *@return pmu switch delay if it is configured
 */
u32 _mali_osk_get_pmu_switch_delay(void);

/** @brief Determines if Mali GPU has been configured with shared interrupts.
 *
 * @return MALI_TRUE if shared interrupts, MALI_FALSE if not.
 */
mali_bool _mali_osk_shared_interrupts(void);

/** @brief Initialize the gpu secure mode.
 * The gpu secure mode will initially be in a disabled state.
 * @return _MALI_OSK_ERR_OK on success, otherwise failure.
 */
_mali_osk_errcode_t _mali_osk_gpu_secure_mode_init(void);

/** @brief Deinitialize the gpu secure mode.
 * @return _MALI_OSK_ERR_OK on success, otherwise failure.
 */
_mali_osk_errcode_t _mali_osk_gpu_secure_mode_deinit(void);

/** @brief Reset GPU and enable the gpu secure mode.
 * @return _MALI_OSK_ERR_OK on success, otherwise failure.
 */
_mali_osk_errcode_t _mali_osk_gpu_reset_and_secure_mode_enable(void);

/** @brief Reset GPU and disable the gpu secure mode.
 * @return _MALI_OSK_ERR_OK on success, otherwise failure.
 */
_mali_osk_errcode_t _mali_osk_gpu_reset_and_secure_mode_disable(void);

/** @brief Check if the gpu secure mode has been enabled.
 * @return MALI_TRUE if enabled, otherwise MALI_FALSE.
 */
mali_bool _mali_osk_gpu_secure_mode_is_enabled(void);

/** @brief Check if the gpu secure mode is supported.
 * @return MALI_TRUE if supported, otherwise MALI_FALSE.
 */
mali_bool _mali_osk_gpu_secure_mode_is_supported(void);


/** @} */ /* end group _mali_osk_miscellaneous */

#ifdef __cplusplus
}
#endif

#endif /* __MALI_OSK_MALI_H__ */
