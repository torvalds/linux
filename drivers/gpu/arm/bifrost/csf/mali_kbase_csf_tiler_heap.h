/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2019-2021 ARM Limited. All rights reserved.
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

#ifndef _KBASE_CSF_TILER_HEAP_H_
#define _KBASE_CSF_TILER_HEAP_H_

#include <mali_kbase.h>

/**
 * kbase_csf_tiler_heap_context_init - Initialize the tiler heaps context for a
 *                                     GPU address space
 *
 * @kctx: Pointer to the kbase context being initialized.
 *
 * Return: 0 if successful or a negative error code on failure.
 */
int kbase_csf_tiler_heap_context_init(struct kbase_context *kctx);

/**
 * kbase_csf_tiler_heap_context_term - Terminate the tiler heaps context for a
 *                                     GPU address space
 *
 * @kctx: Pointer to the kbase context being terminated.
 *
 * This function deletes any chunked tiler heaps that weren't deleted before
 * context termination.
 */
void kbase_csf_tiler_heap_context_term(struct kbase_context *kctx);

/**
 * kbase_csf_tiler_heap_init - Initialize a chunked tiler memory heap.
 *
 * @kctx: Pointer to the kbase context in which to allocate resources for the
 *        tiler heap.
 * @chunk_size: Size of each chunk, in bytes. Must be page-aligned.
 * @initial_chunks: The initial number of chunks to allocate. Must not be
 *                  zero or greater than @max_chunks.
 * @max_chunks: The maximum number of chunks that the heap should be allowed
 *              to use. Must not be less than @initial_chunks.
 * @target_in_flight: Number of render-passes that the driver should attempt to
 *                    keep in flight for which allocation of new chunks is
 *                    allowed. Must not be zero.
 * @gpu_heap_va: Where to store the GPU virtual address of the context that was
 *               set up for the tiler heap.
 * @first_chunk_va: Where to store the GPU virtual address of the first chunk
 *                  allocated for the heap. This points to the header of the
 *                  heap chunk and not to the low address of free memory in it.
 *
 * Return: 0 if successful or a negative error code on failure.
 */
int kbase_csf_tiler_heap_init(struct kbase_context *kctx,
	u32 chunk_size, u32 initial_chunks, u32 max_chunks,
	u16 target_in_flight, u64 *gpu_heap_va,
	u64 *first_chunk_va);

/**
 * kbasep_cs_tiler_heap_term - Terminate a chunked tiler memory heap.
 *
 * @kctx: Pointer to the kbase context in which the tiler heap was initialized.
 * @gpu_heap_va: The GPU virtual address of the context that was set up for the
 *               tiler heap.
 *
 * This function will terminate a chunked tiler heap and cause all the chunks
 * (initial and those added during out-of-memory processing) to be freed.
 * It is the caller's responsibility to ensure no further operations on this
 * heap will happen before calling this function.
 *
 * Return: 0 if successful or a negative error code on failure.
 */
int kbase_csf_tiler_heap_term(struct kbase_context *kctx, u64 gpu_heap_va);

/**
 * kbase_csf_tiler_heap_alloc_new_chunk - Allocate a new chunk for tiler heap.
 *
 * @kctx:               Pointer to the kbase context in which the tiler heap was initialized.
 * @gpu_heap_va:        GPU virtual address of the heap context.
 * @nr_in_flight:       Number of render passes that are in-flight, must not be zero.
 * @pending_frag_count: Number of render passes in-flight with completed vertex/tiler stage.
 *                      The minimum value is zero but it must be less or equal to
 *                      the total number of render passes in flight
 * @new_chunk_ptr:      Where to store the GPU virtual address & size of the new
 *                      chunk allocated for the heap.
 *
 * This function will allocate a new chunk for the chunked tiler heap depending
 * on the settings provided by userspace when the heap was created and the
 * heap's statistics (like number of render passes in-flight).
 * It would return an appropriate error code if a new chunk couldn't be
 * allocated.
 *
 * Return: 0 if a new chunk was allocated otherwise an appropriate negative
 *         error code (like -EBUSY when a free chunk is expected to be
 *         available upon completion of a render pass and -EINVAL when
 *         invalid value was passed for one of the argument).
 */
int kbase_csf_tiler_heap_alloc_new_chunk(struct kbase_context *kctx,
	u64 gpu_heap_va, u32 nr_in_flight, u32 pending_frag_count, u64 *new_chunk_ptr);
#endif
