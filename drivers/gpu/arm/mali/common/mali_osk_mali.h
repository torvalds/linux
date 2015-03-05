/*
 * Copyright (C) 2010-2014 ARM Limited. All rights reserved.
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

/** @brief Find the number of L2 cache cores.
 *
 * @return return the number of l2 cache cores we find in device resources.
 */
u32 _mali_osk_l2_resource_count(void);

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

/** @} */ /* end group _mali_osk_miscellaneous */

#ifdef __cplusplus
}
#endif

#endif /* __MALI_OSK_MALI_H__ */
