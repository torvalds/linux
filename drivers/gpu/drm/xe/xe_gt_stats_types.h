/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef _XE_GT_STATS_TYPES_H_
#define _XE_GT_STATS_TYPES_H_

#include <linux/types.h>

/**
 * enum xe_gt_stats_id - GT statistics identifiers
 * @XE_GT_STATS_ID_SVM_PAGEFAULT_COUNT: Total SVM page faults handled.
 * @XE_GT_STATS_ID_TLB_INVAL: Total GPU Translation Lookaside Buffer (TLB)
 *   invalidations issued.
 * @XE_GT_STATS_ID_SVM_TLB_INVAL_COUNT: TLB invalidations issued during SVM
 *   page-fault handling.
 * @XE_GT_STATS_ID_SVM_TLB_INVAL_US: Cumulative time (µs) waiting for TLB
 *   invalidations during SVM page-fault handling.
 *
 * @XE_GT_STATS_ID_VMA_PAGEFAULT_COUNT: Buffer-object (non-SVM) page faults
 *   handled.
 * @XE_GT_STATS_ID_VMA_PAGEFAULT_KB: Size (KiB) of VMAs involved in
 *   buffer-object page fault handling.
 * @XE_GT_STATS_ID_INVALID_PREFETCH_PAGEFAULT_COUNT: GPU prefetch faults for
 *   addresses with no valid backing.
 *
 * @XE_GT_STATS_ID_SVM_4K_PAGEFAULT_COUNT: SVM page faults resolved by
 *   mapping 4K pages.
 * @XE_GT_STATS_ID_SVM_64K_PAGEFAULT_COUNT: SVM page faults resolved by
 *   mapping 64K pages.
 * @XE_GT_STATS_ID_SVM_2M_PAGEFAULT_COUNT: SVM page faults resolved by
 *   mapping 2M pages.
 * @XE_GT_STATS_ID_SVM_4K_VALID_PAGEFAULT_COUNT: Valid SVM page faults
 *   at 4K page size, where the GPU mapping was already valid — resolved without
 *   creating new mappings.
 * @XE_GT_STATS_ID_SVM_64K_VALID_PAGEFAULT_COUNT: Valid SVM page faults at 64K
 *   page size.
 * @XE_GT_STATS_ID_SVM_2M_VALID_PAGEFAULT_COUNT: Valid SVM page faults at 2M
 *   page size.
 * @XE_GT_STATS_ID_SVM_4K_PAGEFAULT_US: Cumulative time (µs) handling 4K SVM
 *   page faults.
 * @XE_GT_STATS_ID_SVM_64K_PAGEFAULT_US: Cumulative time (µs) handling 64K
 *   SVM page faults.
 * @XE_GT_STATS_ID_SVM_2M_PAGEFAULT_US: Cumulative time (µs) handling 2M SVM
 *   page faults.
 *
 * @XE_GT_STATS_ID_SVM_4K_MIGRATE_COUNT: 4K pages moved from CPU to device
 *   memory.
 * @XE_GT_STATS_ID_SVM_64K_MIGRATE_COUNT: 64K pages moved from CPU to device
 *   memory.
 * @XE_GT_STATS_ID_SVM_2M_MIGRATE_COUNT: 2M pages moved from CPU to device
 *   memory.
 * @XE_GT_STATS_ID_SVM_4K_MIGRATE_US: Cumulative time (µs) moving 4K pages
 *   from CPU to device memory.
 * @XE_GT_STATS_ID_SVM_64K_MIGRATE_US: Cumulative time (µs) moving 64K pages
 *   from CPU to device memory.
 * @XE_GT_STATS_ID_SVM_2M_MIGRATE_US: Cumulative time (µs) moving 2M pages
 *   from CPU to device memory.
 *
 * @XE_GT_STATS_ID_SVM_DEVICE_COPY_US: Cumulative time (µs) for memory copies to
 *   device, across all page sizes.
 * @XE_GT_STATS_ID_SVM_4K_DEVICE_COPY_US: Cumulative time (µs) for memory copies
 *   of 4K pages to device.
 * @XE_GT_STATS_ID_SVM_64K_DEVICE_COPY_US: Cumulative time (µs) for memory
 *   copies of 64K pages to device.
 * @XE_GT_STATS_ID_SVM_2M_DEVICE_COPY_US: Cumulative time (µs) for memory copies
 *   of 2M pages to device.
 * @XE_GT_STATS_ID_SVM_CPU_COPY_US: Cumulative time (µs) for memory copies to
 *   CPU, across all page sizes.
 * @XE_GT_STATS_ID_SVM_4K_CPU_COPY_US: Cumulative time (µs) for memory copies of
 *   4K pages to CPU.
 * @XE_GT_STATS_ID_SVM_64K_CPU_COPY_US: Cumulative time (µs) for memory copies
 *   of 64K pages to CPU.
 * @XE_GT_STATS_ID_SVM_2M_CPU_COPY_US: Cumulative time (µs) for memory copies of
 *   2M pages to CPU.
 * @XE_GT_STATS_ID_SVM_DEVICE_COPY_KB: Data (KiB) copied to device across all
 *   page sizes.
 * @XE_GT_STATS_ID_SVM_4K_DEVICE_COPY_KB: Data (KiB) copied to device for 4K
 *   pages.
 * @XE_GT_STATS_ID_SVM_64K_DEVICE_COPY_KB: Data (KiB) copied to device for
 *   64K pages.
 * @XE_GT_STATS_ID_SVM_2M_DEVICE_COPY_KB: Data (KiB) copied to device for 2M
 *   pages.
 * @XE_GT_STATS_ID_SVM_CPU_COPY_KB: Data (KiB) copied to CPU across all page
 *   sizes.
 * @XE_GT_STATS_ID_SVM_4K_CPU_COPY_KB: Data (KiB) copied to CPU for 4K pages.
 * @XE_GT_STATS_ID_SVM_64K_CPU_COPY_KB: Data (KiB) copied to CPU for 64K pages.
 * @XE_GT_STATS_ID_SVM_2M_CPU_COPY_KB: Data (KiB) copied to CPU for 2M pages.
 *
 * @XE_GT_STATS_ID_SVM_4K_GET_PAGES_US: Cumulative time (µs) getting CPU
 *   memory pages for GPU access at 4K page size.
 * @XE_GT_STATS_ID_SVM_64K_GET_PAGES_US: Cumulative time (µs) getting CPU
 *   memory pages for GPU access at 64K page size.
 * @XE_GT_STATS_ID_SVM_2M_GET_PAGES_US: Cumulative time (µs) getting CPU
 *   memory pages for GPU access at 2M page size.
 * @XE_GT_STATS_ID_SVM_4K_BIND_US: Cumulative time (µs) binding 4K pages
 *   into the GPU page table.
 * @XE_GT_STATS_ID_SVM_64K_BIND_US: Cumulative time (µs) binding 64K pages
 *   into the GPU page table.
 * @XE_GT_STATS_ID_SVM_2M_BIND_US: Cumulative time (µs) binding 2M pages
 *   into the GPU page table.
 *
 * @XE_GT_STATS_ID_HW_ENGINE_GROUP_SUSPEND_LR_QUEUE_COUNT: Times the
 *   scheduler preempted a long-running (LR) GPU exec queue.
 * @XE_GT_STATS_ID_HW_ENGINE_GROUP_SKIP_LR_QUEUE_COUNT: Times the scheduler
 *   skipped suspend because the system was idle.
 * @XE_GT_STATS_ID_HW_ENGINE_GROUP_WAIT_DMA_QUEUE_COUNT: Times the driver
 *   stalled waiting for prior GPU work to complete before scheduling more.
 * @XE_GT_STATS_ID_HW_ENGINE_GROUP_SUSPEND_LR_QUEUE_US: Cumulative time
 *   (µs) spent preempting long-running (LR) GPU exec queues.
 * @XE_GT_STATS_ID_HW_ENGINE_GROUP_WAIT_DMA_QUEUE_US: Cumulative time (µs)
 *   stalled waiting for prior GPU work to complete.
 *
 * @XE_GT_STATS_ID_PRL_4K_ENTRY_COUNT: 4K-page entries from the page reclaim
 *   list that were processed.
 * @XE_GT_STATS_ID_PRL_64K_ENTRY_COUNT: 64K-page entries from the page reclaim
 *   list that were processed.
 * @XE_GT_STATS_ID_PRL_2M_ENTRY_COUNT: 2M-page entries from the page reclaim
 *   list that were processed.
 * @XE_GT_STATS_ID_PRL_ISSUED_COUNT: Times a page reclamation was issued.
 * @XE_GT_STATS_ID_PRL_ABORTED_COUNT: Times the page reclaim process was
 *   aborted.
 *
 * @__XE_GT_STATS_NUM_IDS: Number of valid IDs; not a real counter.
 *
 * See Documentation/gpu/xe/xe_gt_stats.rst.
 */
enum xe_gt_stats_id {
	XE_GT_STATS_ID_SVM_PAGEFAULT_COUNT,
	XE_GT_STATS_ID_TLB_INVAL,
	XE_GT_STATS_ID_SVM_TLB_INVAL_COUNT,
	XE_GT_STATS_ID_SVM_TLB_INVAL_US,
	XE_GT_STATS_ID_VMA_PAGEFAULT_COUNT,
	XE_GT_STATS_ID_VMA_PAGEFAULT_KB,
	XE_GT_STATS_ID_INVALID_PREFETCH_PAGEFAULT_COUNT,
	XE_GT_STATS_ID_SVM_4K_PAGEFAULT_COUNT,
	XE_GT_STATS_ID_SVM_64K_PAGEFAULT_COUNT,
	XE_GT_STATS_ID_SVM_2M_PAGEFAULT_COUNT,
	XE_GT_STATS_ID_SVM_4K_VALID_PAGEFAULT_COUNT,
	XE_GT_STATS_ID_SVM_64K_VALID_PAGEFAULT_COUNT,
	XE_GT_STATS_ID_SVM_2M_VALID_PAGEFAULT_COUNT,
	XE_GT_STATS_ID_SVM_4K_PAGEFAULT_US,
	XE_GT_STATS_ID_SVM_64K_PAGEFAULT_US,
	XE_GT_STATS_ID_SVM_2M_PAGEFAULT_US,
	XE_GT_STATS_ID_SVM_4K_MIGRATE_COUNT,
	XE_GT_STATS_ID_SVM_64K_MIGRATE_COUNT,
	XE_GT_STATS_ID_SVM_2M_MIGRATE_COUNT,
	XE_GT_STATS_ID_SVM_4K_MIGRATE_US,
	XE_GT_STATS_ID_SVM_64K_MIGRATE_US,
	XE_GT_STATS_ID_SVM_2M_MIGRATE_US,
	XE_GT_STATS_ID_SVM_DEVICE_COPY_US,
	XE_GT_STATS_ID_SVM_4K_DEVICE_COPY_US,
	XE_GT_STATS_ID_SVM_64K_DEVICE_COPY_US,
	XE_GT_STATS_ID_SVM_2M_DEVICE_COPY_US,
	XE_GT_STATS_ID_SVM_CPU_COPY_US,
	XE_GT_STATS_ID_SVM_4K_CPU_COPY_US,
	XE_GT_STATS_ID_SVM_64K_CPU_COPY_US,
	XE_GT_STATS_ID_SVM_2M_CPU_COPY_US,
	XE_GT_STATS_ID_SVM_DEVICE_COPY_KB,
	XE_GT_STATS_ID_SVM_4K_DEVICE_COPY_KB,
	XE_GT_STATS_ID_SVM_64K_DEVICE_COPY_KB,
	XE_GT_STATS_ID_SVM_2M_DEVICE_COPY_KB,
	XE_GT_STATS_ID_SVM_CPU_COPY_KB,
	XE_GT_STATS_ID_SVM_4K_CPU_COPY_KB,
	XE_GT_STATS_ID_SVM_64K_CPU_COPY_KB,
	XE_GT_STATS_ID_SVM_2M_CPU_COPY_KB,
	XE_GT_STATS_ID_SVM_4K_GET_PAGES_US,
	XE_GT_STATS_ID_SVM_64K_GET_PAGES_US,
	XE_GT_STATS_ID_SVM_2M_GET_PAGES_US,
	XE_GT_STATS_ID_SVM_4K_BIND_US,
	XE_GT_STATS_ID_SVM_64K_BIND_US,
	XE_GT_STATS_ID_SVM_2M_BIND_US,
	XE_GT_STATS_ID_HW_ENGINE_GROUP_SUSPEND_LR_QUEUE_COUNT,
	XE_GT_STATS_ID_HW_ENGINE_GROUP_SKIP_LR_QUEUE_COUNT,
	XE_GT_STATS_ID_HW_ENGINE_GROUP_WAIT_DMA_QUEUE_COUNT,
	XE_GT_STATS_ID_HW_ENGINE_GROUP_SUSPEND_LR_QUEUE_US,
	XE_GT_STATS_ID_HW_ENGINE_GROUP_WAIT_DMA_QUEUE_US,
	XE_GT_STATS_ID_PRL_4K_ENTRY_COUNT,
	XE_GT_STATS_ID_PRL_64K_ENTRY_COUNT,
	XE_GT_STATS_ID_PRL_2M_ENTRY_COUNT,
	XE_GT_STATS_ID_PRL_ISSUED_COUNT,
	XE_GT_STATS_ID_PRL_ABORTED_COUNT,
	/* must be the last entry */
	__XE_GT_STATS_NUM_IDS,
};

/**
 * struct xe_gt_stats - Per-CPU GT statistics counters
 * @counters: Array of 64-bit counters indexed by &enum xe_gt_stats_id
 *
 * This structure is used for high-frequency, per-CPU statistics collection
 * in the Xe driver. By using a per-CPU allocation and ensuring the structure
 * is cache-line aligned, we avoid the performance-heavy atomics and cache
 * coherency traffic.
 *
 * Updates to these counters should be performed using the this_cpu_add()
 * macro to ensure they are atomic with respect to local interrupts and
 * preemption-safe without the overhead of explicit locking.
 */
struct xe_gt_stats {
	u64 counters[__XE_GT_STATS_NUM_IDS];
} ____cacheline_aligned;

#endif
