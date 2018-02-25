/*
 *
 * (C) COPYRIGHT 2014-2015 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 */

/**
 * @file
 * Interface file for accessing MMU hardware functionality
 */

/**
 * @page mali_kbase_mmu_hw_page MMU hardware interface
 *
 * @section mali_kbase_mmu_hw_intro_sec Introduction
 * This module provides an abstraction for accessing the functionality provided
 * by the midgard MMU and thus allows all MMU HW access to be contained within
 * one common place and allows for different backends (implementations) to
 * be provided.
 */

#ifndef _MALI_KBASE_MMU_HW_H_
#define _MALI_KBASE_MMU_HW_H_

/* Forward declarations */
struct kbase_device;
struct kbase_as;
struct kbase_context;

/**
 * @addtogroup base_kbase_api
 * @{
 */

/**
 * @addtogroup mali_kbase_mmu_hw  MMU access APIs
 * @{
 */

/** @brief MMU fault type descriptor.
 */
enum kbase_mmu_fault_type {
	KBASE_MMU_FAULT_TYPE_UNKNOWN = 0,
	KBASE_MMU_FAULT_TYPE_PAGE,
	KBASE_MMU_FAULT_TYPE_BUS,
	KBASE_MMU_FAULT_TYPE_PAGE_UNEXPECTED,
	KBASE_MMU_FAULT_TYPE_BUS_UNEXPECTED
};

/** @brief Configure an address space for use.
 *
 * Configure the MMU using the address space details setup in the
 * @ref kbase_context structure.
 *
 * @param[in]  kbdev          kbase device to configure.
 * @param[in]  as             address space to configure.
 * @param[in]  kctx           kbase context to configure.
 */
void kbase_mmu_hw_configure(struct kbase_device *kbdev,
		struct kbase_as *as, struct kbase_context *kctx);

/** @brief Issue an operation to the MMU.
 *
 * Issue an operation (MMU invalidate, MMU flush, etc) on the address space that
 * is associated with the provided @ref kbase_context over the specified range
 *
 * @param[in]  kbdev         kbase device to issue the MMU operation on.
 * @param[in]  as            address space to issue the MMU operation on.
 * @param[in]  kctx          kbase context to issue the MMU operation on.
 * @param[in]  vpfn          MMU Virtual Page Frame Number to start the
 *                           operation on.
 * @param[in]  nr            Number of pages to work on.
 * @param[in]  type          Operation type (written to ASn_COMMAND).
 * @param[in]  handling_irq  Is this operation being called during the handling
 *                           of an interrupt?
 *
 * @return Zero if the operation was successful, non-zero otherwise.
 */
int kbase_mmu_hw_do_operation(struct kbase_device *kbdev, struct kbase_as *as,
		struct kbase_context *kctx, u64 vpfn, u32 nr, u32 type,
		unsigned int handling_irq);

/** @brief Clear a fault that has been previously reported by the MMU.
 *
 * Clear a bus error or page fault that has been reported by the MMU.
 *
 * @param[in]  kbdev         kbase device to  clear the fault from.
 * @param[in]  as            address space to  clear the fault from.
 * @param[in]  kctx          kbase context to clear the fault from or NULL.
 * @param[in]  type          The type of fault that needs to be cleared.
 */
void kbase_mmu_hw_clear_fault(struct kbase_device *kbdev, struct kbase_as *as,
		struct kbase_context *kctx, enum kbase_mmu_fault_type type);

/** @brief Enable fault that has been previously reported by the MMU.
 *
 * After a page fault or bus error has been reported by the MMU these
 * will be disabled. After these are handled this function needs to be
 * called to enable the page fault or bus error fault again.
 *
 * @param[in]  kbdev         kbase device to again enable the fault from.
 * @param[in]  as            address space to again enable the fault from.
 * @param[in]  kctx          kbase context to again enable the fault from.
 * @param[in]  type          The type of fault that needs to be enabled again.
 */
void kbase_mmu_hw_enable_fault(struct kbase_device *kbdev, struct kbase_as *as,
		struct kbase_context *kctx, enum kbase_mmu_fault_type type);

/** @} *//* end group mali_kbase_mmu_hw */
/** @} *//* end group base_kbase_api */

#endif	/* _MALI_KBASE_MMU_HW_H_ */
