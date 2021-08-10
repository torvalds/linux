/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2021 ARM Limited. All rights reserved.
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

/*
 * Virtual interface for CSF hardware counter backend.
 */

#ifndef _KBASE_HWCNT_BACKEND_CSF_IF_H_
#define _KBASE_HWCNT_BACKEND_CSF_IF_H_

#include <linux/types.h>

/**
 * struct kbase_hwcnt_backend_csf_if_ctx - Opaque pointer to a CSF interface
 *                                         context.
 */
struct kbase_hwcnt_backend_csf_if_ctx;

/**
 * struct kbase_hwcnt_backend_csf_if_ring_buf - Opaque pointer to a CSF
 *                                              interface ring buffer.
 */
struct kbase_hwcnt_backend_csf_if_ring_buf;

/**
 * struct kbase_hwcnt_backend_csf_if_enable - enable hardware counter collection
 *                                            structure.
 * @fe_bm:          Front End counters selection bitmask.
 * @shader_bm:      Shader counters selection bitmask.
 * @tiler_bm:       Tiler counters selection bitmask.
 * @mmu_l2_bm:      MMU_L2 counters selection bitmask.
 * @counter_set:    The performance counter set to enable.
 * @clk_enable_map: An array of u64 bitfields, each bit of which enables cycle
 *                  counter for a given clock domain.
 */
struct kbase_hwcnt_backend_csf_if_enable {
	u32 fe_bm;
	u32 shader_bm;
	u32 tiler_bm;
	u32 mmu_l2_bm;
	u8 counter_set;
	u64 clk_enable_map;
};

/**
 * struct kbase_hwcnt_backend_csf_if_prfcnt_info - Performance counter
 *                                                 information.
 * @dump_bytes:       Bytes of GPU memory required to perform a performance
 *                    counter dump.
 * @prfcnt_block_size Bytes of each performance counter block.
 * @l2_count:         The MMU L2 cache count.
 * @core_mask:        Shader core mask.
 * @clk_cnt:          Clock domain count in the system.
 * @clearing_samples: Indicates whether counters are cleared after each sample
 *                    is taken.
 */
struct kbase_hwcnt_backend_csf_if_prfcnt_info {
	size_t dump_bytes;
	size_t prfcnt_block_size;
	size_t l2_count;
	u64 core_mask;
	u8 clk_cnt;
	bool clearing_samples;
};

/**
 * typedef kbase_hwcnt_backend_csf_if_assert_lock_held_fn - Assert that the
 *                                                          backend spinlock is
 *                                                          held.
 * @ctx: Non-NULL pointer to a CSF context.
 */
typedef void kbase_hwcnt_backend_csf_if_assert_lock_held_fn(
	struct kbase_hwcnt_backend_csf_if_ctx *ctx);

/**
 * typedef kbase_hwcnt_backend_csf_if_lock_fn - Acquire backend spinlock.
 *
 * @ctx:   Non-NULL pointer to a CSF context.
 * @flags: Pointer to the memory location that would store the previous
 *         interrupt state.
 */
typedef void
kbase_hwcnt_backend_csf_if_lock_fn(struct kbase_hwcnt_backend_csf_if_ctx *ctx,
				   unsigned long *flags);

/**
 * typedef kbase_hwcnt_backend_csf_if_unlock_fn - Release backend spinlock.
 *
 * @ctx:   Non-NULL pointer to a CSF context.
 * @flags: Previously stored interrupt state when Scheduler interrupt
 *         spinlock was acquired.
 */
typedef void
kbase_hwcnt_backend_csf_if_unlock_fn(struct kbase_hwcnt_backend_csf_if_ctx *ctx,
				     unsigned long flags);

/**
 * typedef kbase_hwcnt_backend_csf_if_get_prfcnt_info_fn - Get performance
 *                                                         counter information.
 * @ctx:          Non-NULL pointer to a CSF context.
 * @prfcnt_info:  Non-NULL pointer to struct where performance counter
 *                information should be stored.
 */
typedef void kbase_hwcnt_backend_csf_if_get_prfcnt_info_fn(
	struct kbase_hwcnt_backend_csf_if_ctx *ctx,
	struct kbase_hwcnt_backend_csf_if_prfcnt_info *prfcnt_info);

/**
 * typedef kbase_hwcnt_backend_csf_if_ring_buf_alloc_fn - Allocate a ring buffer
 *                                                        for CSF interface.
 * @ctx:           Non-NULL pointer to a CSF context.
 * @buf_count:     The buffer count in the ring buffer to be allocated,
 *                 MUST be power of 2.
 * @cpu_dump_base: Non-NULL pointer to where ring buffer CPU base address is
 *                 stored when success.
 * @ring_buf:      Non-NULL pointer to where ring buffer is stored when success.
 *
 * A ring buffer is needed by the CSF interface to do manual HWC sample and
 * automatic HWC samples, the buffer count in the ring buffer MUST be power
 * of 2 to meet the hardware requirement.
 *
 * Return: 0 on success, else error code.
 */
typedef int kbase_hwcnt_backend_csf_if_ring_buf_alloc_fn(
	struct kbase_hwcnt_backend_csf_if_ctx *ctx, u32 buf_count,
	void **cpu_dump_base,
	struct kbase_hwcnt_backend_csf_if_ring_buf **ring_buf);

/**
 * typedef kbase_hwcnt_backend_csf_if_ring_buf_sync_fn - Sync HWC dump buffers
 *                                                       memory.
 * @ctx:             Non-NULL pointer to a CSF context.
 * @ring_buf:        Non-NULL pointer to the ring buffer.
 * @buf_index_first: The first buffer index in the ring buffer to be synced,
 *                   inclusive.
 * @buf_index_last:  The last buffer index in the ring buffer to be synced,
 *                   exclusive.
 * @for_cpu:         The direction of sync to be applied, set to true when CPU
 *                   cache needs invalidating before reading the buffer, and set
 *                   to false after CPU writes to flush these before this memory
 *                   is overwritten by the GPU.
 *
 * Flush cached HWC dump buffer data to ensure that all writes from GPU and CPU
 * are correctly observed.
 */
typedef void kbase_hwcnt_backend_csf_if_ring_buf_sync_fn(
	struct kbase_hwcnt_backend_csf_if_ctx *ctx,
	struct kbase_hwcnt_backend_csf_if_ring_buf *ring_buf,
	u32 buf_index_first, u32 buf_index_last, bool for_cpu);

/**
 * typedef kbase_hwcnt_backend_csf_if_ring_buf_free_fn - Free a ring buffer for
 *                                                       the CSF interface.
 *
 * @ctx:      Non-NULL pointer to a CSF interface context.
 * @ring_buf: Non-NULL pointer to the ring buffer which to be freed.
 */
typedef void kbase_hwcnt_backend_csf_if_ring_buf_free_fn(
	struct kbase_hwcnt_backend_csf_if_ctx *ctx,
	struct kbase_hwcnt_backend_csf_if_ring_buf *ring_buf);

/**
 * typedef kbase_hwcnt_backend_csf_if_timestamp_ns_fn - Get the current
 *                                                      timestamp of the CSF
 *                                                      interface.
 * @ctx: Non-NULL pointer to a CSF interface context.
 *
 * Return: CSF interface timestamp in nanoseconds.
 */
typedef u64 kbase_hwcnt_backend_csf_if_timestamp_ns_fn(
	struct kbase_hwcnt_backend_csf_if_ctx *ctx);

/**
 * typedef kbase_hwcnt_backend_csf_if_dump_enable_fn - Setup and enable hardware
 *                                                     counter in CSF interface.
 * @ctx:      Non-NULL pointer to a CSF interface context.
 * @ring_buf: Non-NULL pointer to the ring buffer which used to setup the HWC.
 * @enable:   Non-NULL pointer to the enable map of HWC.
 *
 * Requires lock to be taken before calling.
 */
typedef void kbase_hwcnt_backend_csf_if_dump_enable_fn(
	struct kbase_hwcnt_backend_csf_if_ctx *ctx,
	struct kbase_hwcnt_backend_csf_if_ring_buf *ring_buf,
	struct kbase_hwcnt_backend_csf_if_enable *enable);

/**
 * typedef kbase_hwcnt_backend_csf_if_dump_disable_fn - Disable hardware counter
 *                                                      in CSF interface.
 * @ctx: Non-NULL pointer to a CSF interface context.
 *
 * Requires lock to be taken before calling.
 */
typedef void kbase_hwcnt_backend_csf_if_dump_disable_fn(
	struct kbase_hwcnt_backend_csf_if_ctx *ctx);

/**
 * typedef kbase_hwcnt_backend_csf_if_dump_request_fn - Request a HWC dump.
 *
 * @ctx: Non-NULL pointer to the interface context.
 *
 * Requires lock to be taken before calling.
 */
typedef void kbase_hwcnt_backend_csf_if_dump_request_fn(
	struct kbase_hwcnt_backend_csf_if_ctx *ctx);

/**
 * typedef kbase_hwcnt_backend_csf_if_get_indexes_fn - Get current extract and
 *                                                     insert indexes of the
 *                                                     ring buffer.
 *
 * @ctx:           Non-NULL pointer to a CSF interface context.
 * @extract_index: Non-NULL pointer where current extract index to be saved.
 * @insert_index:  Non-NULL pointer where current insert index to be saved.
 *
 * Requires lock to be taken before calling.
 */
typedef void kbase_hwcnt_backend_csf_if_get_indexes_fn(
	struct kbase_hwcnt_backend_csf_if_ctx *ctx, u32 *extract_index,
	u32 *insert_index);

/**
 * typedef kbase_hwcnt_backend_csf_if_set_extract_index_fn - Update the extract
 *                                                           index of the ring
 *                                                           buffer.
 *
 * @ctx:            Non-NULL pointer to a CSF interface context.
 * @extract_index:  New extract index to be set.
 *
 * Requires lock to be taken before calling.
 */
typedef void kbase_hwcnt_backend_csf_if_set_extract_index_fn(
	struct kbase_hwcnt_backend_csf_if_ctx *ctx, u32 extract_index);

/**
 * typedef kbase_hwcnt_backend_csf_if_get_gpu_cycle_count_fn - Get the current
 *                                                             GPU cycle count.
 * @ctx:            Non-NULL pointer to a CSF interface context.
 * @cycle_counts:   Non-NULL pointer to an array where cycle counts to be saved,
 *                  the array size should be at least as big as the number of
 *                  clock domains returned by get_prfcnt_info interface.
 * @clk_enable_map: An array of bitfields, each bit specifies an enabled clock
 *                  domain.
 *
 * Requires lock to be taken before calling.
 */
typedef void kbase_hwcnt_backend_csf_if_get_gpu_cycle_count_fn(
	struct kbase_hwcnt_backend_csf_if_ctx *ctx, u64 *cycle_counts,
	u64 clk_enable_map);

/**
 * struct kbase_hwcnt_backend_csf_if - Hardware counter backend CSF virtual
 *                                     interface.
 * @ctx:                 CSF interface context.
 * @assert_lock_held:    Function ptr to assert backend spinlock is held.
 * @lock:                Function ptr to acquire backend spinlock.
 * @unlock:              Function ptr to release backend spinlock.
 * @get_prfcnt_info:     Function ptr to get performance counter related
 *                       information.
 * @ring_buf_alloc:      Function ptr to allocate ring buffer for CSF HWC.
 * @ring_buf_sync:       Function ptr to sync ring buffer to CPU.
 * @ring_buf_free:       Function ptr to free ring buffer for CSF HWC.
 * @timestamp_ns:        Function ptr to get the current CSF interface
 *                       timestamp.
 * @dump_enable:         Function ptr to enable dumping.
 * @dump_enable_nolock:  Function ptr to enable dumping while the
 *                       backend-specific spinlock is already held.
 * @dump_disable:        Function ptr to disable dumping.
 * @dump_request:        Function ptr to request a dump.
 * @get_indexes:         Function ptr to get extract and insert indexes of the
 *                       ring buffer.
 * @set_extract_index:   Function ptr to set extract index of ring buffer.
 * @get_gpu_cycle_count: Function ptr to get the GPU cycle count.
 */
struct kbase_hwcnt_backend_csf_if {
	struct kbase_hwcnt_backend_csf_if_ctx *ctx;
	kbase_hwcnt_backend_csf_if_assert_lock_held_fn *assert_lock_held;
	kbase_hwcnt_backend_csf_if_lock_fn *lock;
	kbase_hwcnt_backend_csf_if_unlock_fn *unlock;
	kbase_hwcnt_backend_csf_if_get_prfcnt_info_fn *get_prfcnt_info;
	kbase_hwcnt_backend_csf_if_ring_buf_alloc_fn *ring_buf_alloc;
	kbase_hwcnt_backend_csf_if_ring_buf_sync_fn *ring_buf_sync;
	kbase_hwcnt_backend_csf_if_ring_buf_free_fn *ring_buf_free;
	kbase_hwcnt_backend_csf_if_timestamp_ns_fn *timestamp_ns;
	kbase_hwcnt_backend_csf_if_dump_enable_fn *dump_enable;
	kbase_hwcnt_backend_csf_if_dump_disable_fn *dump_disable;
	kbase_hwcnt_backend_csf_if_dump_request_fn *dump_request;
	kbase_hwcnt_backend_csf_if_get_indexes_fn *get_indexes;
	kbase_hwcnt_backend_csf_if_set_extract_index_fn *set_extract_index;
	kbase_hwcnt_backend_csf_if_get_gpu_cycle_count_fn *get_gpu_cycle_count;
};

#endif /* #define _KBASE_HWCNT_BACKEND_CSF_IF_H_ */
