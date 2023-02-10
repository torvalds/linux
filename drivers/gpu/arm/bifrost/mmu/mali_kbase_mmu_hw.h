/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2014-2015, 2018-2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
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
 */

/**
 * DOC: Interface file for accessing MMU hardware functionality
 *
 * This module provides an abstraction for accessing the functionality provided
 * by the midgard MMU and thus allows all MMU HW access to be contained within
 * one common place and allows for different backends (implementations) to
 * be provided.
 */

#ifndef _KBASE_MMU_HW_H_
#define _KBASE_MMU_HW_H_

#include "mali_kbase_mmu.h"

/* Forward declarations */
struct kbase_device;
struct kbase_as;
struct kbase_context;

/**
 * enum kbase_mmu_fault_type - MMU fault type descriptor.
 * @KBASE_MMU_FAULT_TYPE_UNKNOWN:         unknown fault
 * @KBASE_MMU_FAULT_TYPE_PAGE:            page fault
 * @KBASE_MMU_FAULT_TYPE_BUS:             nus fault
 * @KBASE_MMU_FAULT_TYPE_PAGE_UNEXPECTED: page_unexpected fault
 * @KBASE_MMU_FAULT_TYPE_BUS_UNEXPECTED:  bus_unexpected fault
 */
enum kbase_mmu_fault_type {
	KBASE_MMU_FAULT_TYPE_UNKNOWN = 0,
	KBASE_MMU_FAULT_TYPE_PAGE,
	KBASE_MMU_FAULT_TYPE_BUS,
	KBASE_MMU_FAULT_TYPE_PAGE_UNEXPECTED,
	KBASE_MMU_FAULT_TYPE_BUS_UNEXPECTED
};

/**
 * struct kbase_mmu_hw_op_param  - parameters for kbase_mmu_hw_do_* functions
 * @vpfn:           MMU Virtual Page Frame Number to start the operation on.
 * @nr:             Number of pages to work on.
 * @op:             Operation type (written to ASn_COMMAND).
 * @kctx_id:        Kernel context ID for MMU command tracepoint.
 * @mmu_sync_info:  Indicates whether this call is synchronous wrt MMU ops.
 * @flush_skip_levels: Page table levels to skip flushing. (Only
 *                     applicable if GPU supports feature)
 */
struct kbase_mmu_hw_op_param {
	u64 vpfn;
	u32 nr;
	enum kbase_mmu_op_type op;
	u32 kctx_id;
	enum kbase_caller_mmu_sync_info mmu_sync_info;
	u64 flush_skip_levels;
};

/**
 * kbase_mmu_hw_configure - Configure an address space for use.
 * @kbdev:          kbase device to configure.
 * @as:             address space to configure.
 *
 * Configure the MMU using the address space details setup in the
 * kbase_context structure.
 */
void kbase_mmu_hw_configure(struct kbase_device *kbdev,
		struct kbase_as *as);

/**
 * kbase_mmu_hw_do_lock - Issue LOCK command to the MMU and program
 *                        the LOCKADDR register.
 *
 * @kbdev:     Kbase device to issue the MMU operation on.
 * @as:        Address space to issue the MMU operation on.
 * @op_param:  Pointer to struct containing information about the MMU
 *             operation to perform.
 *
 * hwaccess_lock needs to be held when calling this function.
 *
 * Return: 0 if issuing the command was successful, otherwise an error code.
 */
int kbase_mmu_hw_do_lock(struct kbase_device *kbdev, struct kbase_as *as,
			 const struct kbase_mmu_hw_op_param *op_param);

/**
 * kbase_mmu_hw_do_unlock_no_addr - Issue UNLOCK command to the MMU without
 *                                  programming the LOCKADDR register and wait
 *                                  for it to complete before returning.
 *
 * @kbdev:     Kbase device to issue the MMU operation on.
 * @as:        Address space to issue the MMU operation on.
 * @op_param:  Pointer to struct containing information about the MMU
 *             operation to perform.
 *
 * This function should be called for GPU where GPU command is used to flush
 * the cache(s) instead of MMU command.
 *
 * Return: 0 if issuing the command was successful, otherwise an error code.
 */
int kbase_mmu_hw_do_unlock_no_addr(struct kbase_device *kbdev, struct kbase_as *as,
				   const struct kbase_mmu_hw_op_param *op_param);

/**
 * kbase_mmu_hw_do_unlock - Issue UNLOCK command to the MMU and wait for it
 *                          to complete before returning.
 *
 * @kbdev:     Kbase device to issue the MMU operation on.
 * @as:        Address space to issue the MMU operation on.
 * @op_param:  Pointer to struct containing information about the MMU
 *             operation to perform.
 *
 * Return: 0 if issuing the command was successful, otherwise an error code.
 */
int kbase_mmu_hw_do_unlock(struct kbase_device *kbdev, struct kbase_as *as,
			   const struct kbase_mmu_hw_op_param *op_param);
/**
 * kbase_mmu_hw_do_flush - Issue a flush operation to the MMU.
 *
 * @kbdev:      Kbase device to issue the MMU operation on.
 * @as:         Address space to issue the MMU operation on.
 * @op_param:   Pointer to struct containing information about the MMU
 *              operation to perform.
 *
 * Issue a flush operation on the address space as per the information
 * specified inside @op_param. This function should not be called for
 * GPUs where MMU command to flush the cache(s) is deprecated.
 * mmu_hw_mutex needs to be held when calling this function.
 *
 * Return: 0 if the operation was successful, non-zero otherwise.
 */
int kbase_mmu_hw_do_flush(struct kbase_device *kbdev, struct kbase_as *as,
			  const struct kbase_mmu_hw_op_param *op_param);

/**
 * kbase_mmu_hw_do_flush_locked - Issue a flush operation to the MMU.
 *
 * @kbdev:      Kbase device to issue the MMU operation on.
 * @as:         Address space to issue the MMU operation on.
 * @op_param:   Pointer to struct containing information about the MMU
 *              operation to perform.
 *
 * Issue a flush operation on the address space as per the information
 * specified inside @op_param. This function should not be called for
 * GPUs where MMU command to flush the cache(s) is deprecated.
 * Both mmu_hw_mutex and hwaccess_lock need to be held when calling this
 * function.
 *
 * Return: 0 if the operation was successful, non-zero otherwise.
 */
int kbase_mmu_hw_do_flush_locked(struct kbase_device *kbdev, struct kbase_as *as,
				 const struct kbase_mmu_hw_op_param *op_param);

/**
 * kbase_mmu_hw_do_flush_on_gpu_ctrl - Issue a flush operation to the MMU.
 *
 * @kbdev:      Kbase device to issue the MMU operation on.
 * @as:         Address space to issue the MMU operation on.
 * @op_param:   Pointer to struct containing information about the MMU
 *              operation to perform.
 *
 * Issue a flush operation on the address space as per the information
 * specified inside @op_param. GPU command is used to flush the cache(s)
 * instead of the MMU command.
 *
 * Return: 0 if the operation was successful, non-zero otherwise.
 */
int kbase_mmu_hw_do_flush_on_gpu_ctrl(struct kbase_device *kbdev, struct kbase_as *as,
				      const struct kbase_mmu_hw_op_param *op_param);

/**
 * kbase_mmu_hw_clear_fault - Clear a fault that has been previously reported by
 *                            the MMU.
 * @kbdev:         kbase device to  clear the fault from.
 * @as:            address space to  clear the fault from.
 * @type:          The type of fault that needs to be cleared.
 *
 * Clear a bus error or page fault that has been reported by the MMU.
 */
void kbase_mmu_hw_clear_fault(struct kbase_device *kbdev, struct kbase_as *as,
		enum kbase_mmu_fault_type type);

/**
 * kbase_mmu_hw_enable_fault - Enable fault that has been previously reported by
 *                             the MMU.
 * @kbdev:         kbase device to again enable the fault from.
 * @as:            address space to again enable the fault from.
 * @type:          The type of fault that needs to be enabled again.
 *
 * After a page fault or bus error has been reported by the MMU these
 * will be disabled. After these are handled this function needs to be
 * called to enable the page fault or bus error fault again.
 */
void kbase_mmu_hw_enable_fault(struct kbase_device *kbdev, struct kbase_as *as,
		enum kbase_mmu_fault_type type);

#endif	/* _KBASE_MMU_HW_H_ */
