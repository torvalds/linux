/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_KERNEL_CORE_H__
#define __MALI_KERNEL_CORE_H__

#include "mali_osk.h"

#if USING_MALI_PMM
#include "mali_ukk.h"
#include "mali_pmm.h"
#include "mali_pmm_system.h"
#endif

_mali_osk_errcode_t mali_kernel_constructor( void );
void mali_kernel_destructor( void );

/**
 * @brief Tranlate CPU physical to Mali physical addresses.
 *
 * This function is used to convert CPU physical addresses to Mali Physical
 * addresses, such that _mali_ukk_map_external_mem may be used to map them
 * into Mali. This will be used by _mali_ukk_va_to_mali_pa.
 *
 * This function only supports physically contiguous regions.
 *
 * A default implementation is provided, which uses a registered MEM_VALIDATION
 * resource to do a static translation. Only an address range which will lie
 * in the range specified by MEM_VALIDATION will be successfully translated.
 *
 * If a more complex, or non-static translation is required, then the
 * implementor has the following options:
 * - Rewrite this function to provide such a translation
 * - Integrate the provider of the memory with UMP.
 *
 * @param[in,out] phys_base pointer to the page-aligned base address of the
 * physical range to be translated
 *
 * @param[in] size size of the address range to be translated, which must be a
 * multiple of the physical page size.
 *
 * @return on success, _MALI_OSK_ERR_OK and *phys_base is translated. If the
 * cpu physical address range is not in the valid range, then a suitable
 * _mali_osk_errcode_t error.
 *
 */
_mali_osk_errcode_t mali_kernel_core_translate_cpu_to_mali_phys_range( u32 *phys_base, u32 size );


/**
 * @brief Validate a Mali physical address range.
 *
 * This function is used to ensure that an address range passed to
 * _mali_ukk_map_external_mem is allowed to be mapped into Mali.
 *
 * This function only supports physically contiguous regions.
 *
 * A default implementation is provided, which uses a registered MEM_VALIDATION
 * resource to do a static translation. Only an address range which will lie
 * in the range specified by MEM_VALIDATION will be successfully validated.
 *
 * If a more complex, or non-static validation is required, then the
 * implementor has the following options:
 * - Rewrite this function to provide such a validation
 * - Integrate the provider of the memory with UMP.
 *
 * @param phys_base page-aligned base address of the Mali physical range to be
 * validated.
 *
 * @param size size of the address range to be validated, which must be a
 * multiple of the physical page size.
 *
 * @return _MALI_OSK_ERR_OK if the Mali physical range is valid. Otherwise, a
 * suitable _mali_osk_errcode_t error.
 *
 */
_mali_osk_errcode_t mali_kernel_core_validate_mali_phys_range( u32 phys_base, u32 size );

#if USING_MALI_PMM
/**
 * @brief Signal a power up on a Mali core.
 *
 * This function flags a core as powered up.
 * For PP and GP cores it calls functions that move the core from a power off
 * queue into the idle queue ready to run jobs. It also tries to schedule any
 * pending jobs to run on it.
 *
 * This function will fail if the core is not powered off - either running or
 * already idle.
 *
 * @param core The PMM core id to power up.
 * @param queue_only When MALI_TRUE only re-queue the core - do not reset.
 *
 * @return _MALI_OSK_ERR_OK if the core has been powered up. Otherwise a
 * suitable _mali_osk_errcode_t error.
 */
_mali_osk_errcode_t mali_core_signal_power_up( mali_pmm_core_id core, mali_bool queue_only );

/**
 * @brief Signal a power down on a Mali core.
 *
 * This function flags a core as powered down.
 * For PP and GP cores it calls functions that move the core from an idle
 * queue into the power off queue.
 *
 * This function will fail if the core is not idle - either running or
 * already powered down.
 *
 * @param core The PMM core id to power up.
 * @param immediate_only Do not set the core to pending power down if it can't
 * power down immediately
 *
 * @return _MALI_OSK_ERR_OK if the core has been powered up. Otherwise a
 * suitable _mali_osk_errcode_t error.
 */
_mali_osk_errcode_t mali_core_signal_power_down( mali_pmm_core_id core, mali_bool immediate_only );

#endif

/**
 * Flag to indicate whether or not mali_benchmark is turned on.
 */
extern int mali_benchmark;


#endif /* __MALI_KERNEL_CORE_H__ */

