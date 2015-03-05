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
 * @file mali_platform.h
 * Platform specific Mali driver functions
 */

#ifndef __MALI_PMU_H__
#define __MALI_PMU_H__

#include "mali_osk.h"
#include "mali_kernel_common.h"
#include "mali_hw_core.h"

/** @brief MALI inbuilt PMU hardware info and PMU hardware has knowledge of cores power mask
 */
struct mali_pmu_core {
	struct mali_hw_core hw_core;
	u32 registered_cores_mask;
	u32 switch_delay;
};

/** @brief Register layout for hardware PMU
 */
typedef enum {
	PMU_REG_ADDR_MGMT_POWER_UP                  = 0x00,     /*< Power up register */
	PMU_REG_ADDR_MGMT_POWER_DOWN                = 0x04,     /*< Power down register */
	PMU_REG_ADDR_MGMT_STATUS                    = 0x08,     /*< Core sleep status register */
	PMU_REG_ADDR_MGMT_INT_MASK                  = 0x0C,     /*< Interrupt mask register */
	PMU_REG_ADDR_MGMT_INT_RAWSTAT               = 0x10,     /*< Interrupt raw status register */
	PMU_REG_ADDR_MGMT_INT_CLEAR                 = 0x18,     /*< Interrupt clear register */
	PMU_REG_ADDR_MGMT_SW_DELAY                  = 0x1C,     /*< Switch delay register */
	PMU_REGISTER_ADDRESS_SPACE_SIZE             = 0x28,     /*< Size of register space */
} pmu_reg_addr_mgmt_addr;

#define PMU_REG_VAL_IRQ 1

extern struct mali_pmu_core *mali_global_pmu_core;

/** @brief Initialisation of MALI PMU
 *
 * This is called from entry point of the driver in order to create and intialize the PMU resource
 *
 * @param resource it will be a pointer to a PMU resource
 * @param number_of_pp_cores Number of found PP resources in configuration
 * @param number_of_l2_caches Number of found L2 cache resources in configuration
 * @return The created PMU object, or NULL in case of failure.
 */
struct mali_pmu_core *mali_pmu_create(_mali_osk_resource_t *resource);

/** @brief It deallocates the PMU resource
 *
 * This is called on the exit of the driver to terminate the PMU resource
 *
 * @param pmu Pointer to PMU core object to delete
 */
void mali_pmu_delete(struct mali_pmu_core *pmu);

/** @brief Set registered cores mask
 *
 * @param pmu Pointer to PMU core object
 * @param mask All available/valid domain bits
 */
void mali_pmu_set_registered_cores_mask(struct mali_pmu_core *pmu, u32 mask);

/** @brief Retrieves the Mali PMU core object (if any)
 *
 * @return The Mali PMU object, or NULL if no PMU exists.
 */
MALI_STATIC_INLINE struct mali_pmu_core *mali_pmu_get_global_pmu_core(void)
{
	return mali_global_pmu_core;
}

/** @brief Reset PMU core
 *
 * @param pmu Pointer to PMU core object to reset
 */
void mali_pmu_reset(struct mali_pmu_core *pmu);

void mali_pmu_power_up_all(struct mali_pmu_core *pmu);

void mali_pmu_power_down_all(struct mali_pmu_core *pmu);

/** @brief Returns a mask of the currently powered up domains
 *
 * @param pmu Pointer to PMU core object
 */
MALI_STATIC_INLINE u32 mali_pmu_get_mask(struct mali_pmu_core *pmu)
{
	u32 stat = mali_hw_core_register_read(&pmu->hw_core, PMU_REG_ADDR_MGMT_STATUS);
	return ((~stat) & pmu->registered_cores_mask);
}

/** @brief MALI GPU power down using MALI in-built PMU
 *
 * Called to power down the specified cores.
 *
 * @param pmu Pointer to PMU core object to power down
 * @param mask Mask specifying which power domains to power down
 * @return _MALI_OSK_ERR_OK on success otherwise, a suitable _mali_osk_errcode_t error.
 */
_mali_osk_errcode_t mali_pmu_power_down(struct mali_pmu_core *pmu, u32 mask);

/** @brief MALI GPU power up using MALI in-built PMU
 *
 * Called to power up the specified cores.
 *
 * @param pmu Pointer to PMU core object to power up
 * @param mask Mask specifying which power domains to power up
 * @return _MALI_OSK_ERR_OK on success otherwise, a suitable _mali_osk_errcode_t error.
 */
_mali_osk_errcode_t mali_pmu_power_up(struct mali_pmu_core *pmu, u32 mask);

#endif /* __MALI_PMU_H__ */
