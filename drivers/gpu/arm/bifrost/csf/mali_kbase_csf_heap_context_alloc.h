/*
 *
 * (C) COPYRIGHT 2019 ARM Limited. All rights reserved.
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

#include <mali_kbase.h>

#ifndef _KBASE_CSF_HEAP_CONTEXT_ALLOC_H_
#define _KBASE_CSF_HEAP_CONTEXT_ALLOC_H_

/**
 * kbase_csf_heap_context_allocator_init - Initialize an allocator for heap
 *                                         contexts
 * @ctx_alloc: Pointer to the heap context allocator to initialize.
 * @kctx:      Pointer to the kbase context.
 *
 * Return: 0 if successful or a negative error code on failure.
 */
int kbase_csf_heap_context_allocator_init(
	struct kbase_csf_heap_context_allocator *const ctx_alloc,
	struct kbase_context *const kctx);

/**
 * kbase_csf_heap_context_allocator_term - Terminate an allocator for heap
 *                                         contexts
 * @ctx_alloc: Pointer to the heap context allocator to terminate.
 */
void kbase_csf_heap_context_allocator_term(
	struct kbase_csf_heap_context_allocator *const ctx_alloc);

/**
 * kbase_csf_heap_context_allocator_alloc - Allocate a heap context structure
 *
 * If this function is successful then it returns the address of a
 * zero-initialized heap context structure for use by the firmware.
 *
 * @ctx_alloc: Pointer to the heap context allocator.
 *
 * Return: GPU virtual address of the allocated heap context or 0 on failure.
 */
u64 kbase_csf_heap_context_allocator_alloc(
	struct kbase_csf_heap_context_allocator *const ctx_alloc);

/**
 * kbase_csf_heap_context_allocator_free - Free a heap context structure
 *
 * This function returns a heap context structure to the free pool of unused
 * contexts for possible reuse by a future call to
 * @kbase_csf_heap_context_allocator_alloc.
 *
 * @ctx_alloc:   Pointer to the heap context allocator.
 * @heap_gpu_va: The GPU virtual address of a heap context structure that
 *               was allocated for the firmware.
 */
void kbase_csf_heap_context_allocator_free(
	struct kbase_csf_heap_context_allocator *const ctx_alloc,
	u64 const heap_gpu_va);

#endif /* _KBASE_CSF_HEAP_CONTEXT_ALLOC_H_ */
