/*
 *
 * (C) COPYRIGHT 2019-2020 ARM Limited. All rights reserved.
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

#ifndef _KBASE_MMU_INTERNAL_H_
#define _KBASE_MMU_INTERNAL_H_

void kbase_mmu_get_as_setup(struct kbase_mmu_table *mmut,
		struct kbase_mmu_setup * const setup);

void kbase_gpu_report_bus_fault_and_kill(struct kbase_context *kctx,
		struct kbase_as *as, struct kbase_fault *fault);

void kbase_mmu_report_fault_and_kill(struct kbase_context *kctx,
		struct kbase_as *as, const char *reason_str,
		struct kbase_fault *fault);

/**
 * kbase_mmu_interrupt_process - Process a bus or page fault.
 * @kbdev   The kbase_device the fault happened on
 * @kctx    The kbase_context for the faulting address space if one was found.
 * @as      The address space that has the fault
 * @fault   Data relating to the fault
 *
 * This function will process a fault on a specific address space
 */
void kbase_mmu_interrupt_process(struct kbase_device *kbdev,
		struct kbase_context *kctx, struct kbase_as *as,
		struct kbase_fault *fault);

/**
 * kbase_mmu_switch_to_ir() - Switch to incremental rendering if possible
 * @kctx    The kbase_context for the faulting address space.
 * @reg     Reference of a growable GPU memory region in the same context.
 *          Takes ownership of the reference if successful.
 *
 * Used to switch to incremental rendering if we have nearly run out of
 * virtual address space in a growable memory region.
 *
 * Return 0 if successful, otherwise a negative error code.
 */
int kbase_mmu_switch_to_ir(struct kbase_context *kctx,
	struct kbase_va_region *reg);

#endif /* _KBASE_MMU_INTERNAL_H_ */
