/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2014-2015, 2018-2021 ARM Limited. All rights reserved.
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
 * enum kbase_mmu_cache_flush_type - enum for MMU operations
 * @KBASE_MMU_OP_NONE:        To help catch uninitialized struct
 * @KBASE_MMU_OP_FIRST:       The lower boundary of enum
 * @KBASE_MMU_OP_LOCK:        Lock memory region
 * @KBASE_MMU_OP_UNLOCK:      Unlock memory region
 * @KBASE_MMU_OP_FLUSH_PT:    Flush page table (CLN+INV L2 only)
 * @KBASE_MMU_OP_FLUSH_MEM:   Flush memory (CLN+INV L2+LSC)
 * @KBASE_MMU_OP_COUNT:       The upper boundary of enum
 */
enum kbase_mmu_op_type {
	KBASE_MMU_OP_NONE = 0, /* Must be zero */
	KBASE_MMU_OP_FIRST, /* Must be the first non-zero op */
	KBASE_MMU_OP_LOCK = KBASE_MMU_OP_FIRST,
	KBASE_MMU_OP_UNLOCK,
	KBASE_MMU_OP_FLUSH_PT,
	KBASE_MMU_OP_FLUSH_MEM,
	KBASE_MMU_OP_COUNT /* Must be the last in enum */
};

/**
 * struct kbase_mmu_hw_op_param  - parameters for kbase_mmu_hw_do_operation()
 * @vpfn:          MMU Virtual Page Frame Number to start the operation on.
 * @nr:            Number of pages to work on.
 * @op:            Operation type (written to ASn_COMMAND).
 * @kctx_id:       Kernel context ID for MMU command tracepoint
 * @mmu_sync_info: Indicates whether this call is synchronous wrt MMU ops.
 */
struct kbase_mmu_hw_op_param {
	u64 vpfn;
	u32 nr;
	enum kbase_mmu_op_type op;
	u32 kctx_id;
	enum kbase_caller_mmu_sync_info mmu_sync_info;
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
 * kbase_mmu_hw_do_operation - Issue an operation to the MMU.
 * @kbdev:         kbase device to issue the MMU operation on.
 * @as:            address space to issue the MMU operation on.
 * @op_param:      parameters for the operation.
 *
 * Issue an operation (MMU invalidate, MMU flush, etc) on the address space that
 * is associated with the provided kbase_context over the specified range
 *
 * Return: Zero if the operation was successful, non-zero otherwise.
 */
int kbase_mmu_hw_do_operation(struct kbase_device *kbdev, struct kbase_as *as,
			      struct kbase_mmu_hw_op_param *op_param);

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
