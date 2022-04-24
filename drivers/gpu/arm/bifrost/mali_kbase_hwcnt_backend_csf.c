// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2021-2022 ARM Limited. All rights reserved.
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

#include "mali_kbase_hwcnt_backend_csf.h"
#include "mali_kbase_hwcnt_gpu.h"
#include "mali_kbase_hwcnt_types.h"

#include <linux/log2.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/completion.h>

#ifndef BASE_MAX_NR_CLOCKS_REGULATORS
#define BASE_MAX_NR_CLOCKS_REGULATORS 4
#endif

#if IS_ENABLED(CONFIG_MALI_IS_FPGA) && !IS_ENABLED(CONFIG_MALI_BIFROST_NO_MALI)
/* Backend watch dog timer interval in milliseconds: 18 seconds. */
#define HWCNT_BACKEND_WATCHDOG_TIMER_INTERVAL_MS ((u32)18000)
#else
/* Backend watch dog timer interval in milliseconds: 1 second. */
#define HWCNT_BACKEND_WATCHDOG_TIMER_INTERVAL_MS ((u32)1000)
#endif /* IS_FPGA && !NO_MALI */

/**
 * enum kbase_hwcnt_backend_csf_dump_state - HWC CSF backend dumping states.
 *
 * @KBASE_HWCNT_BACKEND_CSF_DUMP_IDLE: Initial state, or the state if there is
 * an error.
 *
 * @KBASE_HWCNT_BACKEND_CSF_DUMP_REQUESTED: A user dump has been requested and
 * we are waiting for an ACK, this ACK could come from either PRFCNT_ACK,
 * PROTMODE_ENTER_ACK, or if an error occurs.
 *
 * @KBASE_HWCNT_BACKEND_CSF_DUMP_WATCHDOG_REQUESTED: A watchdog dump has been
 * requested and we're waiting for an ACK - this ACK could come from either
 * PRFCNT_ACK, or if an error occurs, PROTMODE_ENTER_ACK is not applied here
 * since watchdog request can't be triggered in protected mode.
 *
 * @KBASE_HWCNT_BACKEND_CSF_DUMP_QUERYING_INSERT: Checking the insert
 * immediately after receiving the ACK, so we know which index corresponds to
 * the buffer we requested.
 *
 * @KBASE_HWCNT_BACKEND_CSF_DUMP_WORKER_LAUNCHED: The insert has been saved and
 * now we have kicked off the worker.
 *
 * @KBASE_HWCNT_BACKEND_CSF_DUMP_ACCUMULATING: The insert has been saved and now
 * we have kicked off the worker to accumulate up to that insert and then copy
 * the delta to the user buffer to prepare for dump_get().
 *
 * @KBASE_HWCNT_BACKEND_CSF_DUMP_COMPLETED: The dump completed successfully.
 *
 * Valid state transitions:
 * IDLE -> REQUESTED (on user dump request)
 * IDLE -> WATCHDOG_REQUESTED (on watchdog request)
 * IDLE -> QUERYING_INSERT (on user dump request in protected mode)
 * REQUESTED -> QUERYING_INSERT (on dump acknowledged from firmware)
 * WATCHDOG_REQUESTED -> REQUESTED (on user dump request)
 * WATCHDOG_REQUESTED -> COMPLETED (on dump acknowledged from firmware for watchdog request)
 * QUERYING_INSERT -> WORKER_LAUNCHED (on worker submission)
 * WORKER_LAUNCHED -> ACCUMULATING (while the worker is accumulating)
 * ACCUMULATING -> COMPLETED (on accumulation completion)
 * COMPLETED -> QUERYING_INSERT (on user dump request in protected mode)
 * COMPLETED -> REQUESTED (on user dump request)
 * COMPLETED -> WATCHDOG_REQUESTED (on watchdog request)
 * COMPLETED -> IDLE (on disable)
 * ANY -> IDLE (on error)
 */
enum kbase_hwcnt_backend_csf_dump_state {
	KBASE_HWCNT_BACKEND_CSF_DUMP_IDLE,
	KBASE_HWCNT_BACKEND_CSF_DUMP_REQUESTED,
	KBASE_HWCNT_BACKEND_CSF_DUMP_WATCHDOG_REQUESTED,
	KBASE_HWCNT_BACKEND_CSF_DUMP_QUERYING_INSERT,
	KBASE_HWCNT_BACKEND_CSF_DUMP_WORKER_LAUNCHED,
	KBASE_HWCNT_BACKEND_CSF_DUMP_ACCUMULATING,
	KBASE_HWCNT_BACKEND_CSF_DUMP_COMPLETED,
};

/**
 * enum kbase_hwcnt_backend_csf_enable_state - HWC CSF backend enable states.
 *
 * @KBASE_HWCNT_BACKEND_CSF_DISABLED: Initial state, and the state when backend
 * is disabled.
 *
 * @KBASE_HWCNT_BACKEND_CSF_TRANSITIONING_TO_ENABLED: Enable request is in
 * progress, waiting for firmware acknowledgment.
 *
 * @KBASE_HWCNT_BACKEND_CSF_ENABLED: Enable request has been acknowledged,
 * enable is done.
 *
 * @KBASE_HWCNT_BACKEND_CSF_TRANSITIONING_TO_DISABLED: Disable request is in
 * progress, waiting for firmware acknowledgment.
 *
 * @KBASE_HWCNT_BACKEND_CSF_DISABLED_WAIT_FOR_WORKER: Disable request has been
 * acknowledged, waiting for dump workers to be finished.
 *
 * @KBASE_HWCNT_BACKEND_CSF_UNRECOVERABLE_ERROR_WAIT_FOR_WORKER: An
 * unrecoverable error happened, waiting for dump workers to be finished.
 *
 * @KBASE_HWCNT_BACKEND_CSF_UNRECOVERABLE_ERROR:  An unrecoverable error
 * happened, and dump workers have finished, waiting for reset.
 *
 * Valid state transitions:
 * DISABLED -> TRANSITIONING_TO_ENABLED (on enable)
 * TRANSITIONING_TO_ENABLED -> ENABLED (on enable ack)
 * ENABLED -> TRANSITIONING_TO_DISABLED (on disable)
 * TRANSITIONING_TO_DISABLED -> DISABLED_WAIT_FOR_WORKER (on disable ack)
 * DISABLED_WAIT_FOR_WORKER -> DISABLED (after workers are flushed)
 * DISABLED -> UNRECOVERABLE_ERROR (on unrecoverable error)
 * ANY but DISABLED -> UNRECOVERABLE_ERROR_WAIT_FOR_WORKER (on unrecoverable
 *                                                          error)
 * UNRECOVERABLE_ERROR -> DISABLED (on before reset)
 */
enum kbase_hwcnt_backend_csf_enable_state {
	KBASE_HWCNT_BACKEND_CSF_DISABLED,
	KBASE_HWCNT_BACKEND_CSF_TRANSITIONING_TO_ENABLED,
	KBASE_HWCNT_BACKEND_CSF_ENABLED,
	KBASE_HWCNT_BACKEND_CSF_TRANSITIONING_TO_DISABLED,
	KBASE_HWCNT_BACKEND_CSF_DISABLED_WAIT_FOR_WORKER,
	KBASE_HWCNT_BACKEND_CSF_UNRECOVERABLE_ERROR_WAIT_FOR_WORKER,
	KBASE_HWCNT_BACKEND_CSF_UNRECOVERABLE_ERROR,
};

/**
 * struct kbase_hwcnt_backend_csf_info - Information used to create an instance
 *                                       of a CSF hardware counter backend.
 * @backend:                      Pointer to access CSF backend.
 * @fw_in_protected_mode:         True if FW is running in protected mode, else
 *                                false.
 * @unrecoverable_error_happened: True if an recoverable error happened, else
 *                                false.
 * @csf_if:                       CSF interface object pointer.
 * @ring_buf_cnt:                 Dump buffer count in the ring buffer.
 * @counter_set:                  The performance counter set to use.
 * @metadata:                     Hardware counter metadata.
 * @prfcnt_info:                  Performance counter information.
 * @watchdog_if:                  Watchdog interface object pointer.
 */
struct kbase_hwcnt_backend_csf_info {
	struct kbase_hwcnt_backend_csf *backend;
	bool fw_in_protected_mode;
	bool unrecoverable_error_happened;
	struct kbase_hwcnt_backend_csf_if *csf_if;
	u32 ring_buf_cnt;
	enum kbase_hwcnt_set counter_set;
	const struct kbase_hwcnt_metadata *metadata;
	struct kbase_hwcnt_backend_csf_if_prfcnt_info prfcnt_info;
	struct kbase_hwcnt_watchdog_interface *watchdog_if;
};

/**
 * struct kbase_hwcnt_csf_physical_layout - HWC sample memory physical layout
 *                                          information.
 * @fe_cnt:             Front end block count.
 * @tiler_cnt:          Tiler block count.
 * @mmu_l2_cnt:         Memory system(MMU and L2 cache) block count.
 * @shader_cnt:         Shader Core block count.
 * @block_cnt:          Total block count (sum of all other block counts).
 * @shader_avail_mask:  Bitmap of all shader cores in the system.
 * @enable_mask_offset: Offset in array elements of enable mask in each block
 *                      starting from the beginning of block.
 * @headers_per_block:  Header size per block.
 * @counters_per_block: Counters size per block.
 * @values_per_block:   Total size per block.
 */
struct kbase_hwcnt_csf_physical_layout {
	u8 fe_cnt;
	u8 tiler_cnt;
	u8 mmu_l2_cnt;
	u8 shader_cnt;
	u8 block_cnt;
	u64 shader_avail_mask;
	size_t enable_mask_offset;
	size_t headers_per_block;
	size_t counters_per_block;
	size_t values_per_block;
};

/**
 * struct kbase_hwcnt_backend_csf - Instance of a CSF hardware counter backend.
 * @info:                       CSF Info used to create the backend.
 * @dump_state:                 The dumping state of the backend.
 * @enable_state:               The CSF backend internal enabled state.
 * @insert_index_to_accumulate: The insert index in the ring buffer which need
 *                              to accumulate up to.
 * @enable_state_waitq:         Wait queue object used to notify the enable
 *                              changing flag is done.
 * @to_user_buf:                HWC sample buffer for client user, size
 *                              metadata.dump_buf_bytes.
 * @accum_buf:                  HWC sample buffer used as an internal
 *                              accumulator, size metadata.dump_buf_bytes.
 * @old_sample_buf:             HWC sample buffer to save the previous values
 *                              for delta calculation, size
 *                              prfcnt_info.dump_bytes.
 * @watchdog_last_seen_insert_idx: The insert index which watchdog has last
 *                                 seen, to check any new firmware automatic
 *                                 samples generated during the watchdog
 *                                 period.
 * @ring_buf:                   Opaque pointer for ring buffer object.
 * @ring_buf_cpu_base:          CPU base address of the allocated ring buffer.
 * @clk_enable_map:             The enable map specifying enabled clock domains.
 * @cycle_count_elapsed:        Cycle count elapsed for a given sample period.
 * @prev_cycle_count:           Previous cycle count to calculate the cycle
 *                              count for sample period.
 * @phys_layout:                Physical memory layout information of HWC
 *                              sample buffer.
 * @dump_completed:             Completion signaled by the dump worker when
 *                              it is completed accumulating up to the
 *                              insert_index_to_accumulate.
 *                              Should be initialized to the "complete" state.
 * @user_requested:             Flag to indicate a dump_request called from
 *                              user.
 * @hwc_dump_workq:             Single threaded work queue for HWC workers
 *                              execution.
 * @hwc_dump_work:              Worker to accumulate samples.
 * @hwc_threshold_work:         Worker for consuming available samples when
 *                              threshold interrupt raised.
 */
struct kbase_hwcnt_backend_csf {
	struct kbase_hwcnt_backend_csf_info *info;
	enum kbase_hwcnt_backend_csf_dump_state dump_state;
	enum kbase_hwcnt_backend_csf_enable_state enable_state;
	u32 insert_index_to_accumulate;
	wait_queue_head_t enable_state_waitq;
	u64 *to_user_buf;
	u64 *accum_buf;
	u32 *old_sample_buf;
	u32 watchdog_last_seen_insert_idx;
	struct kbase_hwcnt_backend_csf_if_ring_buf *ring_buf;
	void *ring_buf_cpu_base;
	u64 clk_enable_map;
	u64 cycle_count_elapsed[BASE_MAX_NR_CLOCKS_REGULATORS];
	u64 prev_cycle_count[BASE_MAX_NR_CLOCKS_REGULATORS];
	struct kbase_hwcnt_csf_physical_layout phys_layout;
	struct completion dump_completed;
	bool user_requested;
	struct workqueue_struct *hwc_dump_workq;
	struct work_struct hwc_dump_work;
	struct work_struct hwc_threshold_work;
};

static bool kbasep_hwcnt_backend_csf_backend_exists(
	struct kbase_hwcnt_backend_csf_info *csf_info)
{
	WARN_ON(!csf_info);
	csf_info->csf_if->assert_lock_held(csf_info->csf_if->ctx);
	return (csf_info->backend != NULL);
}

/**
 * kbasep_hwcnt_backend_csf_cc_initial_sample() - Initialize cycle count
 *                                                tracking.
 *
 * @backend_csf: Non-NULL pointer to backend.
 * @enable_map:  Non-NULL pointer to enable map specifying enabled counters.
 */
static void kbasep_hwcnt_backend_csf_cc_initial_sample(
	struct kbase_hwcnt_backend_csf *backend_csf,
	const struct kbase_hwcnt_enable_map *enable_map)
{
	u64 clk_enable_map = enable_map->clk_enable_map;
	u64 cycle_counts[BASE_MAX_NR_CLOCKS_REGULATORS];
	size_t clk;

	/* Read cycle count from CSF interface for both clock domains. */
	backend_csf->info->csf_if->get_gpu_cycle_count(
		backend_csf->info->csf_if->ctx, cycle_counts, clk_enable_map);

	kbase_hwcnt_metadata_for_each_clock(enable_map->metadata, clk) {
		if (kbase_hwcnt_clk_enable_map_enabled(clk_enable_map, clk))
			backend_csf->prev_cycle_count[clk] = cycle_counts[clk];
	}

	/* Keep clk_enable_map for dump_request. */
	backend_csf->clk_enable_map = clk_enable_map;
}

static void
kbasep_hwcnt_backend_csf_cc_update(struct kbase_hwcnt_backend_csf *backend_csf)
{
	u64 cycle_counts[BASE_MAX_NR_CLOCKS_REGULATORS];
	size_t clk;

	backend_csf->info->csf_if->assert_lock_held(
		backend_csf->info->csf_if->ctx);

	backend_csf->info->csf_if->get_gpu_cycle_count(
		backend_csf->info->csf_if->ctx, cycle_counts,
		backend_csf->clk_enable_map);

	kbase_hwcnt_metadata_for_each_clock(backend_csf->info->metadata, clk) {
		if (kbase_hwcnt_clk_enable_map_enabled(
			    backend_csf->clk_enable_map, clk)) {
			backend_csf->cycle_count_elapsed[clk] =
				cycle_counts[clk] -
				backend_csf->prev_cycle_count[clk];
			backend_csf->prev_cycle_count[clk] = cycle_counts[clk];
		}
	}
}

/* CSF backend implementation of kbase_hwcnt_backend_timestamp_ns_fn */
static u64
kbasep_hwcnt_backend_csf_timestamp_ns(struct kbase_hwcnt_backend *backend)
{
	struct kbase_hwcnt_backend_csf *backend_csf =
		(struct kbase_hwcnt_backend_csf *)backend;

	if (!backend_csf || !backend_csf->info || !backend_csf->info->csf_if)
		return 0;

	return backend_csf->info->csf_if->timestamp_ns(
		backend_csf->info->csf_if->ctx);
}

/** kbasep_hwcnt_backend_csf_process_enable_map() - Process the enable_map to
 *                                                  guarantee headers are
 *                                                  enabled if any counter is
 *                                                  required.
 *@phys_enable_map: HWC physical enable map to be processed.
 */
static void kbasep_hwcnt_backend_csf_process_enable_map(
	struct kbase_hwcnt_physical_enable_map *phys_enable_map)
{
	WARN_ON(!phys_enable_map);

	/* Enable header if any counter is required from user, the header is
	 * controlled by bit 0 of the enable mask.
	 */
	if (phys_enable_map->fe_bm)
		phys_enable_map->fe_bm |= 1;

	if (phys_enable_map->tiler_bm)
		phys_enable_map->tiler_bm |= 1;

	if (phys_enable_map->mmu_l2_bm)
		phys_enable_map->mmu_l2_bm |= 1;

	if (phys_enable_map->shader_bm)
		phys_enable_map->shader_bm |= 1;
}

static void kbasep_hwcnt_backend_csf_init_layout(
	const struct kbase_hwcnt_backend_csf_if_prfcnt_info *prfcnt_info,
	struct kbase_hwcnt_csf_physical_layout *phys_layout)
{
	u8 shader_core_cnt;
	size_t values_per_block;

	WARN_ON(!prfcnt_info);
	WARN_ON(!phys_layout);

	shader_core_cnt = fls64(prfcnt_info->core_mask);
	values_per_block =
		prfcnt_info->prfcnt_block_size / KBASE_HWCNT_VALUE_HW_BYTES;

	*phys_layout = (struct kbase_hwcnt_csf_physical_layout){
		.fe_cnt = KBASE_HWCNT_V5_FE_BLOCK_COUNT,
		.tiler_cnt = KBASE_HWCNT_V5_TILER_BLOCK_COUNT,
		.mmu_l2_cnt = prfcnt_info->l2_count,
		.shader_cnt = shader_core_cnt,
		.block_cnt = KBASE_HWCNT_V5_FE_BLOCK_COUNT +
			     KBASE_HWCNT_V5_TILER_BLOCK_COUNT +
			     prfcnt_info->l2_count + shader_core_cnt,
		.shader_avail_mask = prfcnt_info->core_mask,
		.headers_per_block = KBASE_HWCNT_V5_HEADERS_PER_BLOCK,
		.values_per_block = values_per_block,
		.counters_per_block =
			values_per_block - KBASE_HWCNT_V5_HEADERS_PER_BLOCK,
		.enable_mask_offset = KBASE_HWCNT_V5_PRFCNT_EN_HEADER,
	};
}

static void kbasep_hwcnt_backend_csf_reset_internal_buffers(
	struct kbase_hwcnt_backend_csf *backend_csf)
{
	size_t user_buf_bytes = backend_csf->info->metadata->dump_buf_bytes;

	memset(backend_csf->to_user_buf, 0, user_buf_bytes);
	memset(backend_csf->accum_buf, 0, user_buf_bytes);
	memset(backend_csf->old_sample_buf, 0,
	       backend_csf->info->prfcnt_info.dump_bytes);
}

static void kbasep_hwcnt_backend_csf_zero_sample_prfcnt_en_header(
	struct kbase_hwcnt_backend_csf *backend_csf, u32 *sample)
{
	u32 block_idx;
	const struct kbase_hwcnt_csf_physical_layout *phys_layout;
	u32 *block_buf;

	phys_layout = &backend_csf->phys_layout;

	for (block_idx = 0; block_idx < phys_layout->block_cnt; block_idx++) {
		block_buf = sample + block_idx * phys_layout->values_per_block;
		block_buf[phys_layout->enable_mask_offset] = 0;
	}
}

static void kbasep_hwcnt_backend_csf_zero_all_prfcnt_en_header(
	struct kbase_hwcnt_backend_csf *backend_csf)
{
	u32 idx;
	u32 *sample;
	char *cpu_dump_base;
	size_t dump_bytes = backend_csf->info->prfcnt_info.dump_bytes;

	cpu_dump_base = (char *)backend_csf->ring_buf_cpu_base;

	for (idx = 0; idx < backend_csf->info->ring_buf_cnt; idx++) {
		sample = (u32 *)&cpu_dump_base[idx * dump_bytes];
		kbasep_hwcnt_backend_csf_zero_sample_prfcnt_en_header(
			backend_csf, sample);
	}
}

static void kbasep_hwcnt_backend_csf_update_user_sample(
	struct kbase_hwcnt_backend_csf *backend_csf)
{
	size_t user_buf_bytes = backend_csf->info->metadata->dump_buf_bytes;

	/* Copy the data into the sample and wait for the user to get it. */
	memcpy(backend_csf->to_user_buf, backend_csf->accum_buf,
	       user_buf_bytes);

	/* After copied data into user sample, clear the accumulator values to
	 * prepare for the next accumulator, such as the next request or
	 * threshold.
	 */
	memset(backend_csf->accum_buf, 0, user_buf_bytes);
}

static void kbasep_hwcnt_backend_csf_accumulate_sample(
	const struct kbase_hwcnt_csf_physical_layout *phys_layout,
	size_t dump_bytes, u64 *accum_buf, const u32 *old_sample_buf,
	const u32 *new_sample_buf, bool clearing_samples)
{
	size_t block_idx;
	const u32 *old_block = old_sample_buf;
	const u32 *new_block = new_sample_buf;
	u64 *acc_block = accum_buf;
	const size_t values_per_block = phys_layout->values_per_block;

	for (block_idx = 0; block_idx < phys_layout->block_cnt; block_idx++) {
		const u32 old_enable_mask =
			old_block[phys_layout->enable_mask_offset];
		const u32 new_enable_mask =
			new_block[phys_layout->enable_mask_offset];

		if (new_enable_mask == 0) {
			/* Hardware block was unavailable or we didn't turn on
			 * any counters. Do nothing.
			 */
		} else {
			/* Hardware block was available and it had some counters
			 * enabled. We need to update the accumulation buffer.
			 */
			size_t ctr_idx;

			/* Unconditionally copy the headers. */
			for (ctr_idx = 0;
			     ctr_idx < phys_layout->headers_per_block;
			     ctr_idx++) {
				acc_block[ctr_idx] = new_block[ctr_idx];
			}

			/* Accumulate counter samples
			 *
			 * When accumulating samples we need to take into
			 * account whether the counter sampling method involves
			 * clearing counters back to zero after each sample is
			 * taken.
			 *
			 * The intention for CSF was that all HW should use
			 * counters which wrap to zero when their maximum value
			 * is reached. This, combined with non-clearing
			 * sampling, enables multiple concurrent users to
			 * request samples without interfering with each other.
			 *
			 * However some early HW may not support wrapping
			 * counters, for these GPUs counters must be cleared on
			 * sample to avoid loss of data due to counters
			 * saturating at their maximum value.
			 */
			if (!clearing_samples) {
				if (old_enable_mask == 0) {
					/* Hardware block was previously
					 * unavailable. Accumulate the new
					 * counters only, as we know previous
					 * values are zeroes.
					 */
					for (ctr_idx =
						     phys_layout
							     ->headers_per_block;
					     ctr_idx < values_per_block;
					     ctr_idx++) {
						acc_block[ctr_idx] +=
							new_block[ctr_idx];
					}
				} else {
					/* Hardware block was previously
					 * available. Accumulate the delta
					 * between old and new counter values.
					 */
					for (ctr_idx =
						     phys_layout
							     ->headers_per_block;
					     ctr_idx < values_per_block;
					     ctr_idx++) {
						acc_block[ctr_idx] +=
							new_block[ctr_idx] -
							old_block[ctr_idx];
					}
				}
			} else {
				for (ctr_idx = phys_layout->headers_per_block;
				     ctr_idx < values_per_block; ctr_idx++) {
					acc_block[ctr_idx] +=
						new_block[ctr_idx];
				}
			}
		}
		old_block += values_per_block;
		new_block += values_per_block;
		acc_block += values_per_block;
	}

	WARN_ON(old_block !=
		old_sample_buf + (dump_bytes / KBASE_HWCNT_VALUE_HW_BYTES));
	WARN_ON(new_block !=
		new_sample_buf + (dump_bytes / KBASE_HWCNT_VALUE_HW_BYTES));
	WARN_ON(acc_block !=
		accum_buf + (dump_bytes / KBASE_HWCNT_VALUE_HW_BYTES));
	(void)dump_bytes;
}

static void kbasep_hwcnt_backend_csf_accumulate_samples(
	struct kbase_hwcnt_backend_csf *backend_csf, u32 extract_index_to_start,
	u32 insert_index_to_stop)
{
	u32 raw_idx;
	unsigned long flags;
	u8 *cpu_dump_base = (u8 *)backend_csf->ring_buf_cpu_base;
	const size_t ring_buf_cnt = backend_csf->info->ring_buf_cnt;
	const size_t buf_dump_bytes = backend_csf->info->prfcnt_info.dump_bytes;
	bool clearing_samples = backend_csf->info->prfcnt_info.clearing_samples;
	u32 *old_sample_buf = backend_csf->old_sample_buf;
	u32 *new_sample_buf = old_sample_buf;

	if (extract_index_to_start == insert_index_to_stop)
		/* No samples to accumulate. Early out. */
		return;

	/* Sync all the buffers to CPU side before read the data. */
	backend_csf->info->csf_if->ring_buf_sync(backend_csf->info->csf_if->ctx,
						 backend_csf->ring_buf,
						 extract_index_to_start,
						 insert_index_to_stop, true);

	/* Consider u32 wrap case, '!=' is used here instead of '<' operator */
	for (raw_idx = extract_index_to_start; raw_idx != insert_index_to_stop;
	     raw_idx++) {
		/* The logical "&" acts as a modulo operation since buf_count
		 * must be a power of two.
		 */
		const u32 buf_idx = raw_idx & (ring_buf_cnt - 1);

		new_sample_buf =
			(u32 *)&cpu_dump_base[buf_idx * buf_dump_bytes];

		kbasep_hwcnt_backend_csf_accumulate_sample(
			&backend_csf->phys_layout, buf_dump_bytes,
			backend_csf->accum_buf, old_sample_buf, new_sample_buf,
			clearing_samples);

		old_sample_buf = new_sample_buf;
	}

	/* Save the newest buffer as the old buffer for next time. */
	memcpy(backend_csf->old_sample_buf, new_sample_buf, buf_dump_bytes);

	/* Reset the prfcnt_en header on each sample before releasing them. */
	for (raw_idx = extract_index_to_start; raw_idx != insert_index_to_stop;
	     raw_idx++) {
		const u32 buf_idx = raw_idx & (ring_buf_cnt - 1);
		u32 *sample = (u32 *)&cpu_dump_base[buf_idx * buf_dump_bytes];

		kbasep_hwcnt_backend_csf_zero_sample_prfcnt_en_header(
			backend_csf, sample);
	}

	/* Sync zeroed buffers to avoid coherency issues on future use. */
	backend_csf->info->csf_if->ring_buf_sync(backend_csf->info->csf_if->ctx,
						 backend_csf->ring_buf,
						 extract_index_to_start,
						 insert_index_to_stop, false);

	/* After consuming all samples between extract_idx and insert_idx,
	 * set the raw extract index to insert_idx so that the sample buffers
	 * can be released back to the ring buffer pool.
	 */
	backend_csf->info->csf_if->lock(backend_csf->info->csf_if->ctx, &flags);
	backend_csf->info->csf_if->set_extract_index(
		backend_csf->info->csf_if->ctx, insert_index_to_stop);
	/* Update the watchdog last seen index to check any new FW auto samples
	 * in next watchdog callback.
	 */
	backend_csf->watchdog_last_seen_insert_idx = insert_index_to_stop;
	backend_csf->info->csf_if->unlock(backend_csf->info->csf_if->ctx,
					  flags);
}

static void kbasep_hwcnt_backend_csf_change_es_and_wake_waiters(
	struct kbase_hwcnt_backend_csf *backend_csf,
	enum kbase_hwcnt_backend_csf_enable_state new_state)
{
	backend_csf->info->csf_if->assert_lock_held(
		backend_csf->info->csf_if->ctx);

	if (backend_csf->enable_state != new_state) {
		backend_csf->enable_state = new_state;

		wake_up(&backend_csf->enable_state_waitq);
	}
}

static void kbasep_hwcnt_backend_watchdog_timer_cb(void *info)
{
	struct kbase_hwcnt_backend_csf_info *csf_info = info;
	struct kbase_hwcnt_backend_csf *backend_csf;
	unsigned long flags;

	csf_info->csf_if->lock(csf_info->csf_if->ctx, &flags);

	if (WARN_ON(!kbasep_hwcnt_backend_csf_backend_exists(csf_info))) {
		csf_info->csf_if->unlock(csf_info->csf_if->ctx, flags);
		return;
	}

	backend_csf = csf_info->backend;

	/* Only do watchdog request when all conditions are met: */
	if (/* 1. Backend is enabled. */
	    (backend_csf->enable_state == KBASE_HWCNT_BACKEND_CSF_ENABLED) &&
	    /* 2. FW is not in protected mode. */
	    (!csf_info->fw_in_protected_mode) &&
	    /* 3. dump state indicates no other dumping is in progress. */
	    ((backend_csf->dump_state == KBASE_HWCNT_BACKEND_CSF_DUMP_IDLE) ||
	     (backend_csf->dump_state ==
	      KBASE_HWCNT_BACKEND_CSF_DUMP_COMPLETED))) {
		u32 extract_index;
		u32 insert_index;

		/* Read the raw extract and insert indexes from the CSF interface. */
		csf_info->csf_if->get_indexes(csf_info->csf_if->ctx,
					      &extract_index, &insert_index);

		/* Do watchdog request if no new FW auto samples. */
		if (insert_index ==
		    backend_csf->watchdog_last_seen_insert_idx) {
			/* Trigger the watchdog request. */
			csf_info->csf_if->dump_request(csf_info->csf_if->ctx);

			/* A watchdog dump is required, change the state to
			 * start the request process.
			 */
			backend_csf->dump_state =
				KBASE_HWCNT_BACKEND_CSF_DUMP_WATCHDOG_REQUESTED;
		}
	}

	/* Must schedule another callback when in the transitional state because
	 * this function can be called for the first time before the performance
	 * counter enabled interrupt.
	 */
	if ((backend_csf->enable_state == KBASE_HWCNT_BACKEND_CSF_ENABLED) ||
	    (backend_csf->enable_state ==
	     KBASE_HWCNT_BACKEND_CSF_TRANSITIONING_TO_ENABLED)) {
		/* Reschedule the timer for next watchdog callback. */
		csf_info->watchdog_if->modify(
			csf_info->watchdog_if->timer,
			HWCNT_BACKEND_WATCHDOG_TIMER_INTERVAL_MS);
	}

	csf_info->csf_if->unlock(csf_info->csf_if->ctx, flags);
}

/**
 * kbasep_hwcnt_backend_csf_dump_worker() - HWC dump worker.
 * @work: Work structure.
 *
 * To accumulate all available samples in the ring buffer when a request has
 * been done.
 *
 */
static void kbasep_hwcnt_backend_csf_dump_worker(struct work_struct *work)
{
	unsigned long flags;
	struct kbase_hwcnt_backend_csf *backend_csf;
	u32 insert_index_to_acc;
	u32 extract_index;
	u32 insert_index;

	WARN_ON(!work);
	backend_csf = container_of(work, struct kbase_hwcnt_backend_csf,
				   hwc_dump_work);
	backend_csf->info->csf_if->lock(backend_csf->info->csf_if->ctx, &flags);
	/* Assert the backend is not destroyed. */
	WARN_ON(backend_csf != backend_csf->info->backend);

	/* The backend was disabled or had an error while the worker was being
	 * launched.
	 */
	if (backend_csf->enable_state != KBASE_HWCNT_BACKEND_CSF_ENABLED) {
		WARN_ON(backend_csf->dump_state !=
			KBASE_HWCNT_BACKEND_CSF_DUMP_IDLE);
		WARN_ON(!completion_done(&backend_csf->dump_completed));
		backend_csf->info->csf_if->unlock(
			backend_csf->info->csf_if->ctx, flags);
		return;
	}

	WARN_ON(backend_csf->dump_state !=
		KBASE_HWCNT_BACKEND_CSF_DUMP_WORKER_LAUNCHED);

	backend_csf->dump_state = KBASE_HWCNT_BACKEND_CSF_DUMP_ACCUMULATING;
	insert_index_to_acc = backend_csf->insert_index_to_accumulate;

	/* Read the raw extract and insert indexes from the CSF interface. */
	backend_csf->info->csf_if->get_indexes(backend_csf->info->csf_if->ctx,
					       &extract_index, &insert_index);

	backend_csf->info->csf_if->unlock(backend_csf->info->csf_if->ctx,
					  flags);

	/* Accumulate up to the insert we grabbed at the prfcnt request
	 * interrupt.
	 */
	kbasep_hwcnt_backend_csf_accumulate_samples(backend_csf, extract_index,
						    insert_index_to_acc);

	/* Copy to the user buffer so if a threshold interrupt fires
	 * between now and get(), the accumulations are untouched.
	 */
	kbasep_hwcnt_backend_csf_update_user_sample(backend_csf);

	/* Dump done, set state back to COMPLETED for next request. */
	backend_csf->info->csf_if->lock(backend_csf->info->csf_if->ctx, &flags);
	/* Assert the backend is not destroyed. */
	WARN_ON(backend_csf != backend_csf->info->backend);

	/* The backend was disabled or had an error while we were accumulating.
	 */
	if (backend_csf->enable_state != KBASE_HWCNT_BACKEND_CSF_ENABLED) {
		WARN_ON(backend_csf->dump_state !=
			KBASE_HWCNT_BACKEND_CSF_DUMP_IDLE);
		WARN_ON(!completion_done(&backend_csf->dump_completed));
		backend_csf->info->csf_if->unlock(
			backend_csf->info->csf_if->ctx, flags);
		return;
	}

	WARN_ON(backend_csf->dump_state !=
		KBASE_HWCNT_BACKEND_CSF_DUMP_ACCUMULATING);

	/* Our work here is done - set the wait object and unblock waiters. */
	backend_csf->dump_state = KBASE_HWCNT_BACKEND_CSF_DUMP_COMPLETED;
	complete_all(&backend_csf->dump_completed);
	backend_csf->info->csf_if->unlock(backend_csf->info->csf_if->ctx,
					  flags);
}

/**
 * kbasep_hwcnt_backend_csf_threshold_worker() - Threshold worker.
 *
 * @work: Work structure.
 *
 * Called when a HWC threshold interrupt raised to consume all available samples
 * in the ring buffer.
 */
static void kbasep_hwcnt_backend_csf_threshold_worker(struct work_struct *work)
{
	unsigned long flags;
	struct kbase_hwcnt_backend_csf *backend_csf;
	u32 extract_index;
	u32 insert_index;

	WARN_ON(!work);

	backend_csf = container_of(work, struct kbase_hwcnt_backend_csf,
				   hwc_threshold_work);
	backend_csf->info->csf_if->lock(backend_csf->info->csf_if->ctx, &flags);

	/* Assert the backend is not destroyed. */
	WARN_ON(backend_csf != backend_csf->info->backend);

	/* Read the raw extract and insert indexes from the CSF interface. */
	backend_csf->info->csf_if->get_indexes(backend_csf->info->csf_if->ctx,
					       &extract_index, &insert_index);

	/* The backend was disabled or had an error while the worker was being
	 * launched.
	 */
	if (backend_csf->enable_state != KBASE_HWCNT_BACKEND_CSF_ENABLED) {
		backend_csf->info->csf_if->unlock(
			backend_csf->info->csf_if->ctx, flags);
		return;
	}

	/* Early out if we are not in the IDLE state or COMPLETED state, as this
	 * means a concurrent dump is in progress and we don't want to
	 * interfere.
	 */
	if ((backend_csf->dump_state != KBASE_HWCNT_BACKEND_CSF_DUMP_IDLE) &&
	    (backend_csf->dump_state !=
	     KBASE_HWCNT_BACKEND_CSF_DUMP_COMPLETED)) {
		backend_csf->info->csf_if->unlock(
			backend_csf->info->csf_if->ctx, flags);
		return;
	}
	backend_csf->info->csf_if->unlock(backend_csf->info->csf_if->ctx,
					  flags);

	/* Accumulate everything we possibly can. We grabbed the insert index
	 * immediately after we acquired the lock but before we checked whether
	 * a concurrent dump was triggered. This ensures that if a concurrent
	 * dump was triggered between releasing the lock and now, we know for a
	 * fact that our insert will not exceed the concurrent dump's
	 * insert_to_accumulate, so we don't risk accumulating too much data.
	 */
	kbasep_hwcnt_backend_csf_accumulate_samples(backend_csf, extract_index,
						    insert_index);

	/* No need to wake up anything since it is not a user dump request. */
}

static void kbase_hwcnt_backend_csf_submit_dump_worker(
	struct kbase_hwcnt_backend_csf_info *csf_info)
{
	u32 extract_index;

	WARN_ON(!csf_info);
	csf_info->csf_if->assert_lock_held(csf_info->csf_if->ctx);

	WARN_ON(!kbasep_hwcnt_backend_csf_backend_exists(csf_info));
	WARN_ON(csf_info->backend->enable_state !=
		KBASE_HWCNT_BACKEND_CSF_ENABLED);
	WARN_ON(csf_info->backend->dump_state !=
		KBASE_HWCNT_BACKEND_CSF_DUMP_QUERYING_INSERT);

	/* Save insert index now so that the dump worker only accumulates the
	 * HWC data associated with this request. Extract index is not stored
	 * as that needs to be checked when accumulating to prevent re-reading
	 * buffers that have already been read and returned to the GPU.
	 */
	csf_info->csf_if->get_indexes(
		csf_info->csf_if->ctx, &extract_index,
		&csf_info->backend->insert_index_to_accumulate);
	csf_info->backend->dump_state =
		KBASE_HWCNT_BACKEND_CSF_DUMP_WORKER_LAUNCHED;

	/* Submit the accumulator task into the work queue. */
	queue_work(csf_info->backend->hwc_dump_workq,
		   &csf_info->backend->hwc_dump_work);
}

static void kbasep_hwcnt_backend_csf_get_physical_enable(
	struct kbase_hwcnt_backend_csf *backend_csf,
	const struct kbase_hwcnt_enable_map *enable_map,
	struct kbase_hwcnt_backend_csf_if_enable *enable)
{
	enum kbase_hwcnt_physical_set phys_counter_set;
	struct kbase_hwcnt_physical_enable_map phys_enable_map;

	kbase_hwcnt_gpu_enable_map_to_physical(&phys_enable_map, enable_map);

	/* process the enable_map to guarantee the block header is enabled which
	 * is needed for delta calculation.
	 */
	kbasep_hwcnt_backend_csf_process_enable_map(&phys_enable_map);

	kbase_hwcnt_gpu_set_to_physical(&phys_counter_set,
					backend_csf->info->counter_set);

	/* Use processed enable_map to enable HWC in HW level. */
	enable->fe_bm = phys_enable_map.fe_bm;
	enable->shader_bm = phys_enable_map.shader_bm;
	enable->tiler_bm = phys_enable_map.tiler_bm;
	enable->mmu_l2_bm = phys_enable_map.mmu_l2_bm;
	enable->counter_set = phys_counter_set;
	enable->clk_enable_map = enable_map->clk_enable_map;
}

/* CSF backend implementation of kbase_hwcnt_backend_dump_enable_nolock_fn */
static int kbasep_hwcnt_backend_csf_dump_enable_nolock(
	struct kbase_hwcnt_backend *backend,
	const struct kbase_hwcnt_enable_map *enable_map)
{
	struct kbase_hwcnt_backend_csf *backend_csf =
		(struct kbase_hwcnt_backend_csf *)backend;
	struct kbase_hwcnt_backend_csf_if_enable enable;
	int err;

	if (!backend_csf || !enable_map ||
	    (enable_map->metadata != backend_csf->info->metadata))
		return -EINVAL;

	backend_csf->info->csf_if->assert_lock_held(
		backend_csf->info->csf_if->ctx);

	kbasep_hwcnt_backend_csf_get_physical_enable(backend_csf, enable_map,
						     &enable);

	/* enable_state should be DISABLED before we transfer it to enabled */
	if (backend_csf->enable_state != KBASE_HWCNT_BACKEND_CSF_DISABLED)
		return -EIO;

	err = backend_csf->info->watchdog_if->enable(
		backend_csf->info->watchdog_if->timer,
		HWCNT_BACKEND_WATCHDOG_TIMER_INTERVAL_MS,
		kbasep_hwcnt_backend_watchdog_timer_cb, backend_csf->info);
	if (err)
		return err;

	backend_csf->dump_state = KBASE_HWCNT_BACKEND_CSF_DUMP_IDLE;
	WARN_ON(!completion_done(&backend_csf->dump_completed));
	kbasep_hwcnt_backend_csf_change_es_and_wake_waiters(
		backend_csf, KBASE_HWCNT_BACKEND_CSF_TRANSITIONING_TO_ENABLED);

	backend_csf->info->csf_if->dump_enable(backend_csf->info->csf_if->ctx,
					       backend_csf->ring_buf, &enable);

	kbasep_hwcnt_backend_csf_cc_initial_sample(backend_csf, enable_map);

	return 0;
}

/* CSF backend implementation of kbase_hwcnt_backend_dump_enable_fn */
static int kbasep_hwcnt_backend_csf_dump_enable(
	struct kbase_hwcnt_backend *backend,
	const struct kbase_hwcnt_enable_map *enable_map)
{
	int errcode;
	unsigned long flags;
	struct kbase_hwcnt_backend_csf *backend_csf =
		(struct kbase_hwcnt_backend_csf *)backend;

	if (!backend_csf)
		return -EINVAL;

	backend_csf->info->csf_if->lock(backend_csf->info->csf_if->ctx, &flags);
	errcode = kbasep_hwcnt_backend_csf_dump_enable_nolock(backend,
							      enable_map);
	backend_csf->info->csf_if->unlock(backend_csf->info->csf_if->ctx,
					  flags);
	return errcode;
}

static void kbasep_hwcnt_backend_csf_wait_enable_transition_complete(
	struct kbase_hwcnt_backend_csf *backend_csf, unsigned long *lock_flags)
{
	backend_csf->info->csf_if->assert_lock_held(
		backend_csf->info->csf_if->ctx);

	while ((backend_csf->enable_state ==
		KBASE_HWCNT_BACKEND_CSF_TRANSITIONING_TO_ENABLED) ||
	       (backend_csf->enable_state ==
		KBASE_HWCNT_BACKEND_CSF_TRANSITIONING_TO_DISABLED)) {
		backend_csf->info->csf_if->unlock(
			backend_csf->info->csf_if->ctx, *lock_flags);

		wait_event(
			backend_csf->enable_state_waitq,
			(backend_csf->enable_state !=
			 KBASE_HWCNT_BACKEND_CSF_TRANSITIONING_TO_ENABLED) &&
				(backend_csf->enable_state !=
				 KBASE_HWCNT_BACKEND_CSF_TRANSITIONING_TO_DISABLED));

		backend_csf->info->csf_if->lock(backend_csf->info->csf_if->ctx,
						lock_flags);
	}
}

/* CSF backend implementation of kbase_hwcnt_backend_dump_disable_fn */
static void
kbasep_hwcnt_backend_csf_dump_disable(struct kbase_hwcnt_backend *backend)
{
	unsigned long flags;
	struct kbase_hwcnt_backend_csf *backend_csf =
		(struct kbase_hwcnt_backend_csf *)backend;
	bool do_disable = false;

	WARN_ON(!backend_csf);

	backend_csf->info->csf_if->lock(backend_csf->info->csf_if->ctx, &flags);

	/* Make sure we wait until any previous enable or disable have completed
	 * before doing anything.
	 */
	kbasep_hwcnt_backend_csf_wait_enable_transition_complete(backend_csf,
								 &flags);

	if (backend_csf->enable_state == KBASE_HWCNT_BACKEND_CSF_DISABLED ||
	    backend_csf->enable_state ==
		    KBASE_HWCNT_BACKEND_CSF_UNRECOVERABLE_ERROR) {
		/* If we are already disabled or in an unrecoverable error
		 * state, there is nothing for us to do.
		 */
		backend_csf->info->csf_if->unlock(
			backend_csf->info->csf_if->ctx, flags);
		return;
	}

	if (backend_csf->enable_state == KBASE_HWCNT_BACKEND_CSF_ENABLED) {
		kbasep_hwcnt_backend_csf_change_es_and_wake_waiters(
			backend_csf,
			KBASE_HWCNT_BACKEND_CSF_TRANSITIONING_TO_DISABLED);
		backend_csf->dump_state = KBASE_HWCNT_BACKEND_CSF_DUMP_IDLE;
		complete_all(&backend_csf->dump_completed);
		/* Only disable if we were previously enabled - in all other
		 * cases the call to disable will have already been made.
		 */
		do_disable = true;
	}

	WARN_ON(backend_csf->dump_state != KBASE_HWCNT_BACKEND_CSF_DUMP_IDLE);
	WARN_ON(!completion_done(&backend_csf->dump_completed));

	backend_csf->info->csf_if->unlock(backend_csf->info->csf_if->ctx,
					  flags);

	/* Deregister the timer and block until any timer callback has completed.
	 * We've transitioned out of the ENABLED state so we can guarantee it
	 * won't reschedule itself.
	 */
	backend_csf->info->watchdog_if->disable(
		backend_csf->info->watchdog_if->timer);

	/* Block until any async work has completed. We have transitioned out of
	 * the ENABLED state so we can guarantee no new work will concurrently
	 * be submitted.
	 */
	flush_workqueue(backend_csf->hwc_dump_workq);

	backend_csf->info->csf_if->lock(backend_csf->info->csf_if->ctx, &flags);

	if (do_disable)
		backend_csf->info->csf_if->dump_disable(
			backend_csf->info->csf_if->ctx);

	kbasep_hwcnt_backend_csf_wait_enable_transition_complete(backend_csf,
								 &flags);

	switch (backend_csf->enable_state) {
	case KBASE_HWCNT_BACKEND_CSF_DISABLED_WAIT_FOR_WORKER:
		kbasep_hwcnt_backend_csf_change_es_and_wake_waiters(
			backend_csf, KBASE_HWCNT_BACKEND_CSF_DISABLED);
		break;
	case KBASE_HWCNT_BACKEND_CSF_UNRECOVERABLE_ERROR_WAIT_FOR_WORKER:
		kbasep_hwcnt_backend_csf_change_es_and_wake_waiters(
			backend_csf,
			KBASE_HWCNT_BACKEND_CSF_UNRECOVERABLE_ERROR);
		break;
	default:
		WARN_ON(true);
		break;
	}

	backend_csf->user_requested = false;
	backend_csf->watchdog_last_seen_insert_idx = 0;

	backend_csf->info->csf_if->unlock(backend_csf->info->csf_if->ctx,
					  flags);

	/* After disable, zero the header of all buffers in the ring buffer back
	 * to 0 to prepare for the next enable.
	 */
	kbasep_hwcnt_backend_csf_zero_all_prfcnt_en_header(backend_csf);

	/* Sync zeroed buffers to avoid coherency issues on future use. */
	backend_csf->info->csf_if->ring_buf_sync(
		backend_csf->info->csf_if->ctx, backend_csf->ring_buf, 0,
		backend_csf->info->ring_buf_cnt, false);

	/* Reset accumulator, old_sample_buf and user_sample to all-0 to prepare
	 * for next enable.
	 */
	kbasep_hwcnt_backend_csf_reset_internal_buffers(backend_csf);
}

/* CSF backend implementation of kbase_hwcnt_backend_dump_request_fn */
static int
kbasep_hwcnt_backend_csf_dump_request(struct kbase_hwcnt_backend *backend,
				      u64 *dump_time_ns)
{
	unsigned long flags;
	struct kbase_hwcnt_backend_csf *backend_csf =
		(struct kbase_hwcnt_backend_csf *)backend;
	bool do_request = false;
	bool watchdog_dumping = false;

	if (!backend_csf)
		return -EINVAL;

	backend_csf->info->csf_if->lock(backend_csf->info->csf_if->ctx, &flags);

	/* If we're transitioning to enabled there's nothing to accumulate, and
	 * the user dump buffer is already zeroed. We can just short circuit to
	 * the DUMP_COMPLETED state.
	 */
	if (backend_csf->enable_state ==
	    KBASE_HWCNT_BACKEND_CSF_TRANSITIONING_TO_ENABLED) {
		backend_csf->dump_state =
			KBASE_HWCNT_BACKEND_CSF_DUMP_COMPLETED;
		*dump_time_ns = kbasep_hwcnt_backend_csf_timestamp_ns(backend);
		kbasep_hwcnt_backend_csf_cc_update(backend_csf);
		backend_csf->user_requested = true;
		backend_csf->info->csf_if->unlock(
			backend_csf->info->csf_if->ctx, flags);
		return 0;
	}

	/* Otherwise, make sure we're already enabled. */
	if (backend_csf->enable_state != KBASE_HWCNT_BACKEND_CSF_ENABLED) {
		backend_csf->info->csf_if->unlock(
			backend_csf->info->csf_if->ctx, flags);
		return -EIO;
	}

	/* Make sure that this is either the first request since enable or the
	 * previous user dump has completed or a watchdog dump is in progress,
	 * so we can avoid midway through a user dump.
	 * If user request comes while a watchdog dumping is in progress,
	 * the user request takes the ownership of the watchdog dumping sample by
	 * changing the dump_state so the interrupt for the watchdog
	 * request can be processed instead of ignored.
	 */
	if ((backend_csf->dump_state != KBASE_HWCNT_BACKEND_CSF_DUMP_IDLE) &&
	    (backend_csf->dump_state !=
	     KBASE_HWCNT_BACKEND_CSF_DUMP_COMPLETED) &&
	    (backend_csf->dump_state !=
	     KBASE_HWCNT_BACKEND_CSF_DUMP_WATCHDOG_REQUESTED)) {
		/* HWC is disabled or another user dump is ongoing,
		 * or we're on fault.
		 */
		backend_csf->info->csf_if->unlock(
			backend_csf->info->csf_if->ctx, flags);
		/* HWC is disabled or another dump is ongoing, or we are on
		 * fault.
		 */
		return -EIO;
	}

	/* Reset the completion so dump_wait() has something to wait on. */
	reinit_completion(&backend_csf->dump_completed);

	if (backend_csf->dump_state ==
	    KBASE_HWCNT_BACKEND_CSF_DUMP_WATCHDOG_REQUESTED)
		watchdog_dumping = true;

	if ((backend_csf->enable_state == KBASE_HWCNT_BACKEND_CSF_ENABLED) &&
	    !backend_csf->info->fw_in_protected_mode) {
		/* Only do the request if we are fully enabled and not in
		 * protected mode.
		 */
		backend_csf->dump_state =
			KBASE_HWCNT_BACKEND_CSF_DUMP_REQUESTED;
		do_request = true;
	} else {
		/* Skip the request and waiting for ack and go straight to
		 * checking the insert and kicking off the worker to do the dump
		 */
		backend_csf->dump_state =
			KBASE_HWCNT_BACKEND_CSF_DUMP_QUERYING_INSERT;
	}

	/* CSF firmware might enter protected mode now, but still call request.
	 * That is fine, as we changed state while holding the lock, so the
	 * protected mode enter function will query the insert and launch the
	 * dumping worker.
	 * At some point we will get the dump request ACK saying a dump is done,
	 * but we can ignore it if we are not in the REQUESTED state and process
	 * it in next round dumping worker.
	 */

	*dump_time_ns = kbasep_hwcnt_backend_csf_timestamp_ns(backend);
	kbasep_hwcnt_backend_csf_cc_update(backend_csf);
	backend_csf->user_requested = true;

	if (do_request) {
		/* If a watchdog dumping is in progress, don't need to do
		 * another request, just update the dump_state and take the
		 * ownership of the sample which watchdog requested.
		 */
		if (!watchdog_dumping)
			backend_csf->info->csf_if->dump_request(
				backend_csf->info->csf_if->ctx);
	} else
		kbase_hwcnt_backend_csf_submit_dump_worker(backend_csf->info);

	backend_csf->info->csf_if->unlock(backend_csf->info->csf_if->ctx,
					  flags);

	/* Modify watchdog timer to delay the regular check time since
	 * just requested.
	 */
	backend_csf->info->watchdog_if->modify(
		backend_csf->info->watchdog_if->timer,
		HWCNT_BACKEND_WATCHDOG_TIMER_INTERVAL_MS);

	return 0;
}

/* CSF backend implementation of kbase_hwcnt_backend_dump_wait_fn */
static int
kbasep_hwcnt_backend_csf_dump_wait(struct kbase_hwcnt_backend *backend)
{
	unsigned long flags;
	struct kbase_hwcnt_backend_csf *backend_csf =
		(struct kbase_hwcnt_backend_csf *)backend;
	int errcode;

	if (!backend_csf)
		return -EINVAL;

	wait_for_completion(&backend_csf->dump_completed);

	backend_csf->info->csf_if->lock(backend_csf->info->csf_if->ctx, &flags);
	/* Make sure the last dump actually succeeded when user requested is
	 * set.
	 */
	if (backend_csf->user_requested &&
	    ((backend_csf->dump_state ==
	      KBASE_HWCNT_BACKEND_CSF_DUMP_COMPLETED) ||
	     (backend_csf->dump_state ==
	      KBASE_HWCNT_BACKEND_CSF_DUMP_WATCHDOG_REQUESTED)))
		errcode = 0;
	else
		errcode = -EIO;

	backend_csf->info->csf_if->unlock(backend_csf->info->csf_if->ctx,
					  flags);

	return errcode;
}

/* CSF backend implementation of kbase_hwcnt_backend_dump_clear_fn */
static int
kbasep_hwcnt_backend_csf_dump_clear(struct kbase_hwcnt_backend *backend)
{
	struct kbase_hwcnt_backend_csf *backend_csf =
		(struct kbase_hwcnt_backend_csf *)backend;
	int errcode;
	u64 ts;

	if (!backend_csf)
		return -EINVAL;

	/* Request a dump so we can clear all current counters. */
	errcode = kbasep_hwcnt_backend_csf_dump_request(backend, &ts);
	if (!errcode)
		/* Wait for the manual dump or auto dump to be done and
		 * accumulator to be updated.
		 */
		errcode = kbasep_hwcnt_backend_csf_dump_wait(backend);

	return errcode;
}

/* CSF backend implementation of kbase_hwcnt_backend_dump_get_fn */
static int kbasep_hwcnt_backend_csf_dump_get(
	struct kbase_hwcnt_backend *backend,
	struct kbase_hwcnt_dump_buffer *dst,
	const struct kbase_hwcnt_enable_map *dst_enable_map, bool accumulate)
{
	struct kbase_hwcnt_backend_csf *backend_csf =
		(struct kbase_hwcnt_backend_csf *)backend;
	int ret;
	size_t clk;

	if (!backend_csf || !dst || !dst_enable_map ||
	    (backend_csf->info->metadata != dst->metadata) ||
	    (dst_enable_map->metadata != dst->metadata))
		return -EINVAL;

	/* Extract elapsed cycle count for each clock domain if enabled. */
	kbase_hwcnt_metadata_for_each_clock(dst_enable_map->metadata, clk) {
		if (!kbase_hwcnt_clk_enable_map_enabled(
			    dst_enable_map->clk_enable_map, clk))
			continue;

		/* Reset the counter to zero if accumulation is off. */
		if (!accumulate)
			dst->clk_cnt_buf[clk] = 0;
		dst->clk_cnt_buf[clk] += backend_csf->cycle_count_elapsed[clk];
	}

	/* We just return the user buffer without checking the current state,
	 * as it is undefined to call this function without a prior succeeding
	 * one to dump_wait().
	 */
	ret = kbase_hwcnt_csf_dump_get(dst, backend_csf->to_user_buf,
				       dst_enable_map, accumulate);

	return ret;
}

/**
 * kbasep_hwcnt_backend_csf_destroy() - Destroy CSF backend.
 * @backend_csf: Pointer to CSF backend to destroy.
 *
 * Can be safely called on a backend in any state of partial construction.
 *
 */
static void
kbasep_hwcnt_backend_csf_destroy(struct kbase_hwcnt_backend_csf *backend_csf)
{
	if (!backend_csf)
		return;

	destroy_workqueue(backend_csf->hwc_dump_workq);

	backend_csf->info->csf_if->ring_buf_free(backend_csf->info->csf_if->ctx,
						 backend_csf->ring_buf);

	kfree(backend_csf->accum_buf);
	backend_csf->accum_buf = NULL;

	kfree(backend_csf->old_sample_buf);
	backend_csf->old_sample_buf = NULL;

	kfree(backend_csf->to_user_buf);
	backend_csf->to_user_buf = NULL;

	kfree(backend_csf);
}

/**
 * kbasep_hwcnt_backend_csf_create() - Create a CSF backend instance.
 *
 * @csf_info:    Non-NULL pointer to backend info.
 * @out_backend: Non-NULL pointer to where backend is stored on success.
 *
 * Return: 0 on success, else error code.
 */
static int
kbasep_hwcnt_backend_csf_create(struct kbase_hwcnt_backend_csf_info *csf_info,
				struct kbase_hwcnt_backend_csf **out_backend)
{
	struct kbase_hwcnt_backend_csf *backend_csf = NULL;
	int errcode = -ENOMEM;

	WARN_ON(!csf_info);
	WARN_ON(!out_backend);

	backend_csf = kzalloc(sizeof(*backend_csf), GFP_KERNEL);
	if (!backend_csf)
		goto alloc_error;

	backend_csf->info = csf_info;
	kbasep_hwcnt_backend_csf_init_layout(&csf_info->prfcnt_info,
					     &backend_csf->phys_layout);

	backend_csf->accum_buf =
		kzalloc(csf_info->metadata->dump_buf_bytes, GFP_KERNEL);
	if (!backend_csf->accum_buf)
		goto err_alloc_acc_buf;

	backend_csf->old_sample_buf =
		kzalloc(csf_info->prfcnt_info.dump_bytes, GFP_KERNEL);
	if (!backend_csf->old_sample_buf)
		goto err_alloc_pre_sample_buf;

	backend_csf->to_user_buf =
		kzalloc(csf_info->metadata->dump_buf_bytes, GFP_KERNEL);
	if (!backend_csf->to_user_buf)
		goto err_alloc_user_sample_buf;

	errcode = csf_info->csf_if->ring_buf_alloc(
		csf_info->csf_if->ctx, csf_info->ring_buf_cnt,
		&backend_csf->ring_buf_cpu_base, &backend_csf->ring_buf);
	if (errcode)
		goto err_ring_buf_alloc;
	errcode = -ENOMEM;

	/* Zero all performance enable header to prepare for first enable. */
	kbasep_hwcnt_backend_csf_zero_all_prfcnt_en_header(backend_csf);

	/* Sync zeroed buffers to avoid coherency issues on use. */
	backend_csf->info->csf_if->ring_buf_sync(
		backend_csf->info->csf_if->ctx, backend_csf->ring_buf, 0,
		backend_csf->info->ring_buf_cnt, false);

	init_completion(&backend_csf->dump_completed);

	init_waitqueue_head(&backend_csf->enable_state_waitq);

	/* Allocate a single threaded work queue for dump worker and threshold
	 * worker.
	 */
	backend_csf->hwc_dump_workq =
		alloc_workqueue("mali_hwc_dump_wq", WQ_HIGHPRI | WQ_UNBOUND, 1);
	if (!backend_csf->hwc_dump_workq)
		goto err_alloc_workqueue;

	INIT_WORK(&backend_csf->hwc_dump_work,
		  kbasep_hwcnt_backend_csf_dump_worker);
	INIT_WORK(&backend_csf->hwc_threshold_work,
		  kbasep_hwcnt_backend_csf_threshold_worker);

	backend_csf->enable_state = KBASE_HWCNT_BACKEND_CSF_DISABLED;
	backend_csf->dump_state = KBASE_HWCNT_BACKEND_CSF_DUMP_IDLE;
	complete_all(&backend_csf->dump_completed);
	backend_csf->user_requested = false;
	backend_csf->watchdog_last_seen_insert_idx = 0;

	*out_backend = backend_csf;
	return 0;

err_alloc_workqueue:
	backend_csf->info->csf_if->ring_buf_free(backend_csf->info->csf_if->ctx,
						 backend_csf->ring_buf);
err_ring_buf_alloc:
	kfree(backend_csf->to_user_buf);
	backend_csf->to_user_buf = NULL;
err_alloc_user_sample_buf:
	kfree(backend_csf->old_sample_buf);
	backend_csf->old_sample_buf = NULL;
err_alloc_pre_sample_buf:
	kfree(backend_csf->accum_buf);
	backend_csf->accum_buf = NULL;
err_alloc_acc_buf:
	kfree(backend_csf);
alloc_error:
	return errcode;
}

/* CSF backend implementation of kbase_hwcnt_backend_init_fn */
static int
kbasep_hwcnt_backend_csf_init(const struct kbase_hwcnt_backend_info *info,
			      struct kbase_hwcnt_backend **out_backend)
{
	unsigned long flags;
	struct kbase_hwcnt_backend_csf *backend_csf = NULL;
	struct kbase_hwcnt_backend_csf_info *csf_info =
		(struct kbase_hwcnt_backend_csf_info *)info;
	int errcode;
	bool success = false;

	if (!info || !out_backend)
		return -EINVAL;

	/* Create the backend. */
	errcode = kbasep_hwcnt_backend_csf_create(csf_info, &backend_csf);
	if (errcode)
		return errcode;

	/* If it was not created before, attach it to csf_info.
	 * Use spin lock to avoid concurrent initialization.
	 */
	backend_csf->info->csf_if->lock(backend_csf->info->csf_if->ctx, &flags);
	if (csf_info->backend == NULL) {
		csf_info->backend = backend_csf;
		*out_backend = (struct kbase_hwcnt_backend *)backend_csf;
		success = true;
		if (csf_info->unrecoverable_error_happened)
			backend_csf->enable_state =
				KBASE_HWCNT_BACKEND_CSF_UNRECOVERABLE_ERROR;
	}
	backend_csf->info->csf_if->unlock(backend_csf->info->csf_if->ctx,
					  flags);

	/* Destroy the new created backend if the backend has already created
	 * before. In normal case, this won't happen if the client call init()
	 * function properly.
	 */
	if (!success) {
		kbasep_hwcnt_backend_csf_destroy(backend_csf);
		return -EBUSY;
	}

	return 0;
}

/* CSF backend implementation of kbase_hwcnt_backend_term_fn */
static void kbasep_hwcnt_backend_csf_term(struct kbase_hwcnt_backend *backend)
{
	unsigned long flags;
	struct kbase_hwcnt_backend_csf *backend_csf =
		(struct kbase_hwcnt_backend_csf *)backend;

	if (!backend)
		return;

	kbasep_hwcnt_backend_csf_dump_disable(backend);

	/* Set the backend in csf_info to NULL so we won't handle any external
	 * notification anymore since we are terminating.
	 */
	backend_csf->info->csf_if->lock(backend_csf->info->csf_if->ctx, &flags);
	backend_csf->info->backend = NULL;
	backend_csf->info->csf_if->unlock(backend_csf->info->csf_if->ctx,
					  flags);

	kbasep_hwcnt_backend_csf_destroy(backend_csf);
}

/**
 * kbasep_hwcnt_backend_csf_info_destroy() - Destroy a CSF backend info.
 * @info: Pointer to info to destroy.
 *
 * Can be safely called on a backend info in any state of partial construction.
 *
 */
static void kbasep_hwcnt_backend_csf_info_destroy(
	const struct kbase_hwcnt_backend_csf_info *info)
{
	if (!info)
		return;

	/* The backend should be destroyed before the info object destroy. */
	WARN_ON(info->backend != NULL);

	/* The metadata should be destroyed before the info object destroy. */
	WARN_ON(info->metadata != NULL);

	kfree(info);
}

/**
 * kbasep_hwcnt_backend_csf_info_create() - Create a CSF backend info.
 *
 * @csf_if:        Non-NULL pointer to a hwcnt backend CSF interface structure
 *                 used to create backend interface.
 * @ring_buf_cnt: The buffer count of the CSF hwcnt backend ring buffer.
 *                MUST be power of 2.
 * @watchdog_if:  Non-NULL pointer to a hwcnt watchdog interface structure used to create
 *                backend interface.
 * @out_info:     Non-NULL pointer to where info is stored on success.
 *
 * Return: 0 on success, else error code.
 */
static int kbasep_hwcnt_backend_csf_info_create(
	struct kbase_hwcnt_backend_csf_if *csf_if, u32 ring_buf_cnt,
	struct kbase_hwcnt_watchdog_interface *watchdog_if,
	const struct kbase_hwcnt_backend_csf_info **out_info)
{
	struct kbase_hwcnt_backend_csf_info *info = NULL;

	if (WARN_ON(!csf_if) || WARN_ON(!watchdog_if) || WARN_ON(!out_info) ||
	    WARN_ON(!is_power_of_2(ring_buf_cnt)))
		return -EINVAL;

	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	*info = (struct kbase_hwcnt_backend_csf_info)
	{
#if defined(CONFIG_MALI_BIFROST_PRFCNT_SET_SECONDARY)
		.counter_set = KBASE_HWCNT_SET_SECONDARY,
#elif defined(CONFIG_MALI_PRFCNT_SET_TERTIARY)
		.counter_set = KBASE_HWCNT_SET_TERTIARY,
#else
		/* Default to primary */
		.counter_set = KBASE_HWCNT_SET_PRIMARY,
#endif
		.backend = NULL, .csf_if = csf_if, .ring_buf_cnt = ring_buf_cnt,
		.fw_in_protected_mode = false,
		.unrecoverable_error_happened = false,
		.watchdog_if = watchdog_if,
	};
	*out_info = info;

	return 0;
}

/* CSF backend implementation of kbase_hwcnt_backend_metadata_fn */
static const struct kbase_hwcnt_metadata *
kbasep_hwcnt_backend_csf_metadata(const struct kbase_hwcnt_backend_info *info)
{
	if (!info)
		return NULL;

	WARN_ON(!((const struct kbase_hwcnt_backend_csf_info *)info)->metadata);

	return ((const struct kbase_hwcnt_backend_csf_info *)info)->metadata;
}

static void kbasep_hwcnt_backend_csf_handle_unrecoverable_error(
	struct kbase_hwcnt_backend_csf *backend_csf)
{
	bool do_disable = false;

	backend_csf->info->csf_if->assert_lock_held(
		backend_csf->info->csf_if->ctx);

	/* We are already in or transitioning to the unrecoverable error state.
	 * Early out.
	 */
	if ((backend_csf->enable_state ==
	     KBASE_HWCNT_BACKEND_CSF_UNRECOVERABLE_ERROR) ||
	    (backend_csf->enable_state ==
	     KBASE_HWCNT_BACKEND_CSF_UNRECOVERABLE_ERROR_WAIT_FOR_WORKER))
		return;

	/* If we are disabled, we know we have no pending workers, so skip the
	 * waiting state.
	 */
	if (backend_csf->enable_state == KBASE_HWCNT_BACKEND_CSF_DISABLED) {
		kbasep_hwcnt_backend_csf_change_es_and_wake_waiters(
			backend_csf,
			KBASE_HWCNT_BACKEND_CSF_UNRECOVERABLE_ERROR);
		return;
	}

	/* Trigger a disable only if we are not already transitioning to
	 * disabled, we don't want to disable twice if an unrecoverable error
	 * happens while we are disabling.
	 */
	do_disable = (backend_csf->enable_state !=
		      KBASE_HWCNT_BACKEND_CSF_TRANSITIONING_TO_DISABLED);

	kbasep_hwcnt_backend_csf_change_es_and_wake_waiters(
		backend_csf,
		KBASE_HWCNT_BACKEND_CSF_UNRECOVERABLE_ERROR_WAIT_FOR_WORKER);

	/* Transition the dump to the IDLE state and unblock any waiters. The
	 * IDLE state signifies an error.
	 */
	backend_csf->dump_state = KBASE_HWCNT_BACKEND_CSF_DUMP_IDLE;
	complete_all(&backend_csf->dump_completed);

	/* Trigger a disable only if we are not already transitioning to
	 * disabled, - we don't want to disable twice if an unrecoverable error
	 * happens while we are disabling.
	 */
	if (do_disable)
		backend_csf->info->csf_if->dump_disable(
			backend_csf->info->csf_if->ctx);
}

static void kbasep_hwcnt_backend_csf_handle_recoverable_error(
	struct kbase_hwcnt_backend_csf *backend_csf)
{
	backend_csf->info->csf_if->assert_lock_held(
		backend_csf->info->csf_if->ctx);

	switch (backend_csf->enable_state) {
	case KBASE_HWCNT_BACKEND_CSF_DISABLED:
	case KBASE_HWCNT_BACKEND_CSF_DISABLED_WAIT_FOR_WORKER:
	case KBASE_HWCNT_BACKEND_CSF_TRANSITIONING_TO_DISABLED:
	case KBASE_HWCNT_BACKEND_CSF_UNRECOVERABLE_ERROR:
	case KBASE_HWCNT_BACKEND_CSF_UNRECOVERABLE_ERROR_WAIT_FOR_WORKER:
		/* Already disabled or disabling, or in an unrecoverable error.
		 * Nothing to be done to handle the error.
		 */
		return;
	case KBASE_HWCNT_BACKEND_CSF_TRANSITIONING_TO_ENABLED:
		/* A seemingly recoverable error that occurs while we are
		 * transitioning to enabled is probably unrecoverable.
		 */
		kbasep_hwcnt_backend_csf_handle_unrecoverable_error(
			backend_csf);
		return;
	case KBASE_HWCNT_BACKEND_CSF_ENABLED:
		/* Start transitioning to the disabled state. We can't wait for
		 * it as this recoverable error might be triggered from an
		 * interrupt. The wait will be done in the eventual call to
		 * disable().
		 */
		kbasep_hwcnt_backend_csf_change_es_and_wake_waiters(
			backend_csf,
			KBASE_HWCNT_BACKEND_CSF_TRANSITIONING_TO_DISABLED);
		/* Transition the dump to the IDLE state and unblock any
		 * waiters. The IDLE state signifies an error.
		 */
		backend_csf->dump_state = KBASE_HWCNT_BACKEND_CSF_DUMP_IDLE;
		complete_all(&backend_csf->dump_completed);

		backend_csf->info->csf_if->dump_disable(
			backend_csf->info->csf_if->ctx);
		return;
	}
}

void kbase_hwcnt_backend_csf_protm_entered(
	struct kbase_hwcnt_backend_interface *iface)
{
	struct kbase_hwcnt_backend_csf_info *csf_info =
		(struct kbase_hwcnt_backend_csf_info *)iface->info;

	csf_info->csf_if->assert_lock_held(csf_info->csf_if->ctx);
	csf_info->fw_in_protected_mode = true;

	/* Call on_prfcnt_sample() to trigger collection of the protected mode
	 * entry auto-sample if there is currently a pending dump request.
	 */
	kbase_hwcnt_backend_csf_on_prfcnt_sample(iface);
}

void kbase_hwcnt_backend_csf_protm_exited(
	struct kbase_hwcnt_backend_interface *iface)
{
	struct kbase_hwcnt_backend_csf_info *csf_info;

	csf_info = (struct kbase_hwcnt_backend_csf_info *)iface->info;

	csf_info->csf_if->assert_lock_held(csf_info->csf_if->ctx);
	csf_info->fw_in_protected_mode = false;
}

void kbase_hwcnt_backend_csf_on_unrecoverable_error(
	struct kbase_hwcnt_backend_interface *iface)
{
	unsigned long flags;
	struct kbase_hwcnt_backend_csf_info *csf_info;

	csf_info = (struct kbase_hwcnt_backend_csf_info *)iface->info;

	csf_info->csf_if->lock(csf_info->csf_if->ctx, &flags);
	csf_info->unrecoverable_error_happened = true;
	/* Early out if the backend does not exist. */
	if (!kbasep_hwcnt_backend_csf_backend_exists(csf_info)) {
		csf_info->csf_if->unlock(csf_info->csf_if->ctx, flags);
		return;
	}

	kbasep_hwcnt_backend_csf_handle_unrecoverable_error(csf_info->backend);

	csf_info->csf_if->unlock(csf_info->csf_if->ctx, flags);
}

void kbase_hwcnt_backend_csf_on_before_reset(
	struct kbase_hwcnt_backend_interface *iface)
{
	unsigned long flags;
	struct kbase_hwcnt_backend_csf_info *csf_info;
	struct kbase_hwcnt_backend_csf *backend_csf;

	csf_info = (struct kbase_hwcnt_backend_csf_info *)iface->info;

	csf_info->csf_if->lock(csf_info->csf_if->ctx, &flags);
	csf_info->unrecoverable_error_happened = false;
	/* Early out if the backend does not exist. */
	if (!kbasep_hwcnt_backend_csf_backend_exists(csf_info)) {
		csf_info->csf_if->unlock(csf_info->csf_if->ctx, flags);
		return;
	}
	backend_csf = csf_info->backend;

	if ((backend_csf->enable_state != KBASE_HWCNT_BACKEND_CSF_DISABLED) &&
	    (backend_csf->enable_state !=
	     KBASE_HWCNT_BACKEND_CSF_UNRECOVERABLE_ERROR)) {
		/* Before a reset occurs, we must either have been disabled
		 * (else we lose data) or we should have encountered an
		 * unrecoverable error. Either way, we will have disabled the
		 * interface and waited for any workers that might have still
		 * been in flight.
		 * If not in these states, fire off one more disable to make
		 * sure everything is turned off before the power is pulled.
		 * We can't wait for this disable to complete, but it doesn't
		 * really matter, the power is being pulled.
		 */
		kbasep_hwcnt_backend_csf_handle_unrecoverable_error(
			csf_info->backend);
	}

	/* A reset is the only way to exit the unrecoverable error state */
	if (backend_csf->enable_state ==
	    KBASE_HWCNT_BACKEND_CSF_UNRECOVERABLE_ERROR) {
		kbasep_hwcnt_backend_csf_change_es_and_wake_waiters(
			backend_csf, KBASE_HWCNT_BACKEND_CSF_DISABLED);
	}

	csf_info->csf_if->unlock(csf_info->csf_if->ctx, flags);
}

void kbase_hwcnt_backend_csf_on_prfcnt_sample(
	struct kbase_hwcnt_backend_interface *iface)
{
	struct kbase_hwcnt_backend_csf_info *csf_info;
	struct kbase_hwcnt_backend_csf *backend_csf;

	csf_info = (struct kbase_hwcnt_backend_csf_info *)iface->info;
	csf_info->csf_if->assert_lock_held(csf_info->csf_if->ctx);

	/* Early out if the backend does not exist. */
	if (!kbasep_hwcnt_backend_csf_backend_exists(csf_info))
		return;
	backend_csf = csf_info->backend;

	/* Skip the dump_work if it's a watchdog request. */
	if (backend_csf->dump_state ==
	    KBASE_HWCNT_BACKEND_CSF_DUMP_WATCHDOG_REQUESTED) {
		backend_csf->dump_state =
			KBASE_HWCNT_BACKEND_CSF_DUMP_COMPLETED;
		return;
	}

	/* If the current state is not REQUESTED, this HWC sample will be
	 * skipped and processed in next dump_request.
	 */
	if (backend_csf->dump_state != KBASE_HWCNT_BACKEND_CSF_DUMP_REQUESTED)
		return;
	backend_csf->dump_state = KBASE_HWCNT_BACKEND_CSF_DUMP_QUERYING_INSERT;

	kbase_hwcnt_backend_csf_submit_dump_worker(csf_info);
}

void kbase_hwcnt_backend_csf_on_prfcnt_threshold(
	struct kbase_hwcnt_backend_interface *iface)
{
	struct kbase_hwcnt_backend_csf_info *csf_info;
	struct kbase_hwcnt_backend_csf *backend_csf;

	csf_info = (struct kbase_hwcnt_backend_csf_info *)iface->info;
	csf_info->csf_if->assert_lock_held(csf_info->csf_if->ctx);

	/* Early out if the backend does not exist. */
	if (!kbasep_hwcnt_backend_csf_backend_exists(csf_info))
		return;
	backend_csf = csf_info->backend;

	if (backend_csf->enable_state == KBASE_HWCNT_BACKEND_CSF_ENABLED)
		/* Submit the threshold work into the work queue to consume the
		 * available samples.
		 */
		queue_work(backend_csf->hwc_dump_workq,
			   &backend_csf->hwc_threshold_work);
}

void kbase_hwcnt_backend_csf_on_prfcnt_overflow(
	struct kbase_hwcnt_backend_interface *iface)
{
	struct kbase_hwcnt_backend_csf_info *csf_info;

	csf_info = (struct kbase_hwcnt_backend_csf_info *)iface->info;
	csf_info->csf_if->assert_lock_held(csf_info->csf_if->ctx);

	/* Early out if the backend does not exist. */
	if (!kbasep_hwcnt_backend_csf_backend_exists(csf_info))
		return;

	/* Called when an overflow occurs. We treat this as a recoverable error,
	 * so we start transitioning to the disabled state.
	 * We could try and handle it while enabled, but in a real system we
	 * never expect an overflow to occur so there is no point implementing
	 * complex recovery code when we can just turn ourselves off instead for
	 * a while.
	 */
	kbasep_hwcnt_backend_csf_handle_recoverable_error(csf_info->backend);
}

void kbase_hwcnt_backend_csf_on_prfcnt_enable(
	struct kbase_hwcnt_backend_interface *iface)
{
	struct kbase_hwcnt_backend_csf_info *csf_info;
	struct kbase_hwcnt_backend_csf *backend_csf;

	csf_info = (struct kbase_hwcnt_backend_csf_info *)iface->info;
	csf_info->csf_if->assert_lock_held(csf_info->csf_if->ctx);

	/* Early out if the backend does not exist. */
	if (!kbasep_hwcnt_backend_csf_backend_exists(csf_info))
		return;
	backend_csf = csf_info->backend;

	if (backend_csf->enable_state ==
	    KBASE_HWCNT_BACKEND_CSF_TRANSITIONING_TO_ENABLED) {
		kbasep_hwcnt_backend_csf_change_es_and_wake_waiters(
			backend_csf, KBASE_HWCNT_BACKEND_CSF_ENABLED);
	} else if (backend_csf->enable_state ==
		   KBASE_HWCNT_BACKEND_CSF_ENABLED) {
		/* Unexpected, but we are already in the right state so just
		 * ignore it.
		 */
	} else {
		/* Unexpected state change, assume everything is broken until
		 * we reset.
		 */
		kbasep_hwcnt_backend_csf_handle_unrecoverable_error(
			csf_info->backend);
	}
}

void kbase_hwcnt_backend_csf_on_prfcnt_disable(
	struct kbase_hwcnt_backend_interface *iface)
{
	struct kbase_hwcnt_backend_csf_info *csf_info;
	struct kbase_hwcnt_backend_csf *backend_csf;

	csf_info = (struct kbase_hwcnt_backend_csf_info *)iface->info;
	csf_info->csf_if->assert_lock_held(csf_info->csf_if->ctx);

	/* Early out if the backend does not exist. */
	if (!kbasep_hwcnt_backend_csf_backend_exists(csf_info))
		return;
	backend_csf = csf_info->backend;

	if (backend_csf->enable_state ==
	    KBASE_HWCNT_BACKEND_CSF_TRANSITIONING_TO_DISABLED) {
		kbasep_hwcnt_backend_csf_change_es_and_wake_waiters(
			backend_csf,
			KBASE_HWCNT_BACKEND_CSF_DISABLED_WAIT_FOR_WORKER);
	} else if (backend_csf->enable_state ==
		   KBASE_HWCNT_BACKEND_CSF_DISABLED) {
		/* Unexpected, but we are already in the right state so just
		 * ignore it.
		 */
	} else {
		/* Unexpected state change, assume everything is broken until
		 * we reset.
		 */
		kbasep_hwcnt_backend_csf_handle_unrecoverable_error(
			csf_info->backend);
	}
}

int kbase_hwcnt_backend_csf_metadata_init(
	struct kbase_hwcnt_backend_interface *iface)
{
	int errcode;
	struct kbase_hwcnt_backend_csf_info *csf_info;
	struct kbase_hwcnt_gpu_info gpu_info;

	if (!iface)
		return -EINVAL;

	csf_info = (struct kbase_hwcnt_backend_csf_info *)iface->info;

	WARN_ON(!csf_info->csf_if->get_prfcnt_info);

	csf_info->csf_if->get_prfcnt_info(csf_info->csf_if->ctx,
					  &csf_info->prfcnt_info);

	/* The clock domain counts should not exceed the number of maximum
	 * number of clock regulators.
	 */
	if (csf_info->prfcnt_info.clk_cnt > BASE_MAX_NR_CLOCKS_REGULATORS)
		return -EIO;

	gpu_info.l2_count = csf_info->prfcnt_info.l2_count;
	gpu_info.core_mask = csf_info->prfcnt_info.core_mask;
	gpu_info.clk_cnt = csf_info->prfcnt_info.clk_cnt;
	gpu_info.prfcnt_values_per_block =
		csf_info->prfcnt_info.prfcnt_block_size /
		KBASE_HWCNT_VALUE_HW_BYTES;
	errcode = kbase_hwcnt_csf_metadata_create(
		&gpu_info, csf_info->counter_set, &csf_info->metadata);
	if (errcode)
		return errcode;

	/*
	 * Dump abstraction size should be exactly twice the size and layout as
	 * the physical dump size since 64-bit per value used in metadata.
	 */
	WARN_ON(csf_info->prfcnt_info.dump_bytes * 2 !=
		csf_info->metadata->dump_buf_bytes);

	return 0;
}

void kbase_hwcnt_backend_csf_metadata_term(
	struct kbase_hwcnt_backend_interface *iface)
{
	struct kbase_hwcnt_backend_csf_info *csf_info;

	if (!iface)
		return;

	csf_info = (struct kbase_hwcnt_backend_csf_info *)iface->info;
	if (csf_info->metadata) {
		kbase_hwcnt_csf_metadata_destroy(csf_info->metadata);
		csf_info->metadata = NULL;
	}
}

int kbase_hwcnt_backend_csf_create(
	struct kbase_hwcnt_backend_csf_if *csf_if, u32 ring_buf_cnt,
	struct kbase_hwcnt_watchdog_interface *watchdog_if,
	struct kbase_hwcnt_backend_interface *iface)
{
	int errcode;
	const struct kbase_hwcnt_backend_csf_info *info = NULL;

	if (!iface || !csf_if || !watchdog_if)
		return -EINVAL;

	/* The buffer count must be power of 2 */
	if (!is_power_of_2(ring_buf_cnt))
		return -EINVAL;

	errcode = kbasep_hwcnt_backend_csf_info_create(csf_if, ring_buf_cnt,
						       watchdog_if, &info);
	if (errcode)
		return errcode;

	iface->info = (struct kbase_hwcnt_backend_info *)info;
	iface->metadata = kbasep_hwcnt_backend_csf_metadata;
	iface->init = kbasep_hwcnt_backend_csf_init;
	iface->term = kbasep_hwcnt_backend_csf_term;
	iface->timestamp_ns = kbasep_hwcnt_backend_csf_timestamp_ns;
	iface->dump_enable = kbasep_hwcnt_backend_csf_dump_enable;
	iface->dump_enable_nolock = kbasep_hwcnt_backend_csf_dump_enable_nolock;
	iface->dump_disable = kbasep_hwcnt_backend_csf_dump_disable;
	iface->dump_clear = kbasep_hwcnt_backend_csf_dump_clear;
	iface->dump_request = kbasep_hwcnt_backend_csf_dump_request;
	iface->dump_wait = kbasep_hwcnt_backend_csf_dump_wait;
	iface->dump_get = kbasep_hwcnt_backend_csf_dump_get;

	return 0;
}

void kbase_hwcnt_backend_csf_destroy(struct kbase_hwcnt_backend_interface *iface)
{
	if (!iface)
		return;

	kbasep_hwcnt_backend_csf_info_destroy(
		(const struct kbase_hwcnt_backend_csf_info *)iface->info);
	memset(iface, 0, sizeof(*iface));
}
