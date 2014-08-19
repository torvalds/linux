/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2009-2014 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

/**
 * @file mali_platform.h
 * Platform specific Mali driver functions
 */

#ifndef __MALI_PMU_H__
#define __MALI_PMU_H__

#include "mali_osk.h"

#define MALI_GP_DOMAIN_INDEX    0
#define MALI_PP0_DOMAIN_INDEX   1
#define MALI_PP1_DOMAIN_INDEX   2
#define MALI_PP2_DOMAIN_INDEX   3
#define MALI_PP3_DOMAIN_INDEX   4
#define MALI_PP4_DOMAIN_INDEX   5
#define MALI_PP5_DOMAIN_INDEX   6
#define MALI_PP6_DOMAIN_INDEX   7
#define MALI_PP7_DOMAIN_INDEX   8
#define MALI_L20_DOMAIN_INDEX   9
#define MALI_L21_DOMAIN_INDEX   10
#define MALI_L22_DOMAIN_INDEX   11

#define MALI_MAX_NUMBER_OF_DOMAINS      12

/* Record the domain config from the customer or default config */
extern u16 mali_pmu_global_domain_config[];

static inline u16 mali_pmu_get_domain_mask(u32 index)
{
	MALI_DEBUG_ASSERT(MALI_MAX_NUMBER_OF_DOMAINS > index);

	return mali_pmu_global_domain_config[index];
}

static inline void mali_pmu_set_domain_mask(u32 index, u16 value)
{
	MALI_DEBUG_ASSERT(MALI_MAX_NUMBER_OF_DOMAINS > index);

	mali_pmu_global_domain_config[index] = value;
}

static inline void mali_pmu_copy_domain_mask(void *src, u32 len)
{
	_mali_osk_memcpy(mali_pmu_global_domain_config, src, len);
}

struct mali_pmu_core;

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

/** @brief Reset PMU core
 *
 * @param pmu Pointer to PMU core object to reset
 * @return _MALI_OSK_ERR_OK on success, otherwise failure.
 */
_mali_osk_errcode_t mali_pmu_reset(struct mali_pmu_core *pmu);

/** @brief MALI GPU power down using MALI in-built PMU
 *
 * Called to power down the specified cores. The mask will be saved so that \a
 * mali_pmu_power_up_all will bring the PMU back to the previous state set with
 * this function or \a mali_pmu_power_up.
 *
 * @param pmu Pointer to PMU core object to power down
 * @param mask Mask specifying which power domains to power down
 * @return _MALI_OSK_ERR_OK on success otherwise, a suitable _mali_osk_errcode_t error.
 */
_mali_osk_errcode_t mali_pmu_power_down(struct mali_pmu_core *pmu, u32 mask);

/** @brief MALI GPU power up using MALI in-built PMU
 *
 * Called to power up the specified cores. The mask will be saved so that \a
 * mali_pmu_power_up_all will bring the PMU back to the previous state set with
 * this function or \a mali_pmu_power_down.
 *
 * @param pmu Pointer to PMU core object to power up
 * @param mask Mask specifying which power domains to power up
 * @return _MALI_OSK_ERR_OK on success otherwise, a suitable _mali_osk_errcode_t error.
 */
_mali_osk_errcode_t mali_pmu_power_up(struct mali_pmu_core *pmu, u32 mask);

/** @brief MALI GPU power down using MALI in-built PMU
 *
 * called to power down all cores
 *
 * @param pmu Pointer to PMU core object to power down
 * @return _MALI_OSK_ERR_OK on success otherwise, a suitable _mali_osk_errcode_t error.
 */
_mali_osk_errcode_t mali_pmu_power_down_all(struct mali_pmu_core *pmu);

/** @brief MALI GPU power up using MALI in-built PMU
 *
 * called to power up all cores
 *
 * @param pmu Pointer to PMU core object to power up
 * @return _MALI_OSK_ERR_OK on success otherwise, a suitable _mali_osk_errcode_t error.
 */
_mali_osk_errcode_t mali_pmu_power_up_all(struct mali_pmu_core *pmu);

/** @brief Retrieves the Mali PMU core object (if any)
 *
 * @return The Mali PMU object, or NULL if no PMU exists.
 */
struct mali_pmu_core *mali_pmu_get_global_pmu_core(void);

#endif /* __MALI_PMU_H__ */
