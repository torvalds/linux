/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */



/**
 * @file
 * Interface file for the direct implementation for MMU hardware access
 */

/**
 * @page mali_kbase_mmu_hw_direct_page Direct MMU hardware interface
 *
 * @section mali_kbase_mmu_hw_direct_intro_sec Introduction
 * This module provides the interface(s) that are required by the direct
 * register access implementation of the MMU hardware interface
 * @ref mali_kbase_mmu_hw_page .
 */

#ifndef _MALI_KBASE_MMU_HW_DIRECT_H_
#define _MALI_KBASE_MMU_HW_DIRECT_H_

#include <mali_kbase_defs.h>

/**
 * @addtogroup mali_kbase_mmu_hw
 * @{
 */

/**
 * @addtogroup mali_kbase_mmu_hw_direct Direct register access to MMU
 * @{
 */

/** @brief Process an MMU interrupt.
 *
 * Process the MMU interrupt that was reported by the @ref kbase_device.
 *
 * @param[in]  kbdev          kbase context to clear the fault from.
 * @param[in]  irq_stat       Value of the MMU_IRQ_STATUS register
 */
void kbase_mmu_interrupt(struct kbase_device *kbdev, u32 irq_stat);

/** @} *//* end group mali_kbase_mmu_hw_direct */
/** @} *//* end group mali_kbase_mmu_hw */

#endif	/* _MALI_KBASE_MMU_HW_DIRECT_H_ */
