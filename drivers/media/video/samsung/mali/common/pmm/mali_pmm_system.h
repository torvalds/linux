/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_PMM_SYSTEM_H__
#define __MALI_PMM_SYSTEM_H__

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @addtogroup pmmapi Power Management Module APIs
 *
 * @{
 *
 * @defgroup pmmapi_system Power Management Module System Functions
 *
 * @{
 */

extern struct mali_kernel_subsystem mali_subsystem_pmm;

/** @brief Register a core with the PMM, which will power up
 * the core
 *
 * @param core the core to register with the PMM
 * @return error if the core cannot be powered up
 */
_mali_osk_errcode_t malipmm_core_register( mali_pmm_core_id core );

/** @brief Unregister a core with the PMM
 *
 * @param core the core to unregister with the PMM
 */
void malipmm_core_unregister( mali_pmm_core_id core );

/** @brief Acknowledge that a power down is okay to happen
 *
 * A core should not be running a job, or be in the idle queue when this
 * is called.
 *
 * @param core the core that can now be powered down
 */
void malipmm_core_power_down_okay( mali_pmm_core_id core );

/** @} */ /* End group pmmapi_system */
/** @} */ /* End group pmmapi */

#ifdef __cplusplus
}
#endif

#endif /* __MALI_PMM_H__ */
