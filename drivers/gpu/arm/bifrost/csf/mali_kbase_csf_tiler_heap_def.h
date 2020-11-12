/*
 *
 * (C) COPYRIGHT 2020 ARM Limited. All rights reserved.
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

#ifndef _KBASE_CSF_TILER_HEAP_DEF_H_
#define _KBASE_CSF_TILER_HEAP_DEF_H_

#include <mali_kbase.h>

/* Size of a tiler heap chunk header, in bytes. */
#define CHUNK_HDR_SIZE ((size_t)64)

/* Bit-position of the next chunk's size when stored in a chunk header. */
#define CHUNK_HDR_NEXT_SIZE_POS (0)

/* Bit-position of the next chunk's address when stored in a chunk header. */
#define CHUNK_HDR_NEXT_ADDR_POS (12)

/* Bitmask of the next chunk's size when stored in a chunk header. */
#define CHUNK_HDR_NEXT_SIZE_MASK (((u64)1 << CHUNK_HDR_NEXT_ADDR_POS) - 1u)

/* Bitmask of the address of the next chunk when stored in a chunk header. */
#define CHUNK_HDR_NEXT_ADDR_MASK (~CHUNK_HDR_NEXT_SIZE_MASK)

/* Right-shift before storing the next chunk's size in a chunk header. */
#define CHUNK_HDR_NEXT_SIZE_ENCODE_SHIFT (12)

/* Right-shift before storing the next chunk's address in a chunk header. */
#define CHUNK_HDR_NEXT_ADDR_ENCODE_SHIFT (12)

/* Bitmask of valid chunk sizes. This is also the maximum chunk size, in bytes.
 */
#define CHUNK_SIZE_MASK \
	((CHUNK_HDR_NEXT_SIZE_MASK >> CHUNK_HDR_NEXT_SIZE_POS) << \
	 CHUNK_HDR_NEXT_SIZE_ENCODE_SHIFT)

/* Bitmask of valid chunk addresses. This is also the highest address. */
#define CHUNK_ADDR_MASK \
	((CHUNK_HDR_NEXT_ADDR_MASK >> CHUNK_HDR_NEXT_ADDR_POS) << \
	 CHUNK_HDR_NEXT_ADDR_ENCODE_SHIFT)

/**
 * struct kbase_csf_tiler_heap_chunk - A tiler heap chunk managed by the kernel
 *
 * Chunks are allocated upon initialization of a tiler heap or in response to
 * out-of-memory events from the firmware. Chunks are always fully backed by
 * physical memory to avoid the overhead of processing GPU page faults. The
 * allocated GPU memory regions are linked together independent of the list of
 * kernel objects of this type.
 *
 * @link:   Link to this chunk in a list of chunks belonging to a
 *          @kbase_csf_tiler_heap.
 * @region: Pointer to the GPU memory region allocated for the chunk.
 * @gpu_va: GPU virtual address of the start of the memory region.
 *          This points to the header of the chunk and not to the low address
 *          of free memory within it.
 */
struct kbase_csf_tiler_heap_chunk {
	struct list_head link;
	struct kbase_va_region *region;
	u64 gpu_va;
};

/**
 * struct kbase_csf_tiler_heap - A tiler heap managed by the kernel
 *
 * @kctx:            Pointer to the kbase context with which this heap is
 *                   associated.
 * @link:            Link to this heap in a list of tiler heaps belonging to
 *                   the @kbase_csf_tiler_heap_context.
 * @chunk_size:      Size of each chunk, in bytes. Must be page-aligned.
 * @chunk_count:     The number of chunks currently allocated. Must not be
 *                   zero or greater than @max_chunks.
 * @max_chunks:      The maximum number of chunks that the heap should be
 *                   allowed to use. Must not be less than @chunk_count.
 * @target_in_flight: Number of render-passes that the driver should attempt
 *                    to keep in flight for which allocation of new chunks is
 *                    allowed. Must not be zero.
 * @gpu_va:          The GPU virtual address of the heap context structure that
 *                   was allocated for the firmware. This is also used to
 *                   uniquely identify the heap.
 * @chunks_list:     Linked list of allocated chunks.
 */
struct kbase_csf_tiler_heap {
	struct kbase_context *kctx;
	struct list_head link;
	u32 chunk_size;
	u32 chunk_count;
	u32 max_chunks;
	u16 target_in_flight;
	u64 gpu_va;
	struct list_head chunks_list;
};
#endif /* !_KBASE_CSF_TILER_HEAP_DEF_H_ */
