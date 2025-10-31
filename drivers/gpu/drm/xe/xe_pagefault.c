// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <drm/drm_managed.h>

#include "xe_device.h"
#include "xe_gt_types.h"
#include "xe_pagefault.h"
#include "xe_pagefault_types.h"

/**
 * DOC: Xe page faults
 *
 * Xe page faults are handled in two layers. The producer layer interacts with
 * hardware or firmware to receive and parse faults into struct xe_pagefault,
 * then forwards them to the consumer. The consumer layer services the faults
 * (e.g., memory migration, page table updates) and acknowledges the result back
 * to the producer, which then forwards the results to the hardware or firmware.
 * The consumer uses a page fault queue sized to absorb all potential faults and
 * a multi-threaded worker to process them. Multiple producers are supported,
 * with a single shared consumer.
 *
 * xe_pagefault.c implements the consumer layer.
 */

static int xe_pagefault_entry_size(void)
{
	/*
	 * Power of two alignment is not a hardware requirement, rather a
	 * software restriction which makes the math for page fault queue
	 * management simplier.
	 */
	return roundup_pow_of_two(sizeof(struct xe_pagefault));
}

static void xe_pagefault_queue_work(struct work_struct *w)
{
	/* TODO: Implement */
}

static int xe_pagefault_queue_init(struct xe_device *xe,
				   struct xe_pagefault_queue *pf_queue)
{
	struct xe_gt *gt;
	int total_num_eus = 0;
	u8 id;

	for_each_gt(gt, xe, id) {
		xe_dss_mask_t all_dss;
		int num_dss, num_eus;

		bitmap_or(all_dss, gt->fuse_topo.g_dss_mask,
			  gt->fuse_topo.c_dss_mask, XE_MAX_DSS_FUSE_BITS);

		num_dss = bitmap_weight(all_dss, XE_MAX_DSS_FUSE_BITS);
		num_eus = bitmap_weight(gt->fuse_topo.eu_mask_per_dss,
					XE_MAX_EU_FUSE_BITS) * num_dss;

		total_num_eus += num_eus;
	}

	xe_assert(xe, total_num_eus);

	/*
	 * user can issue separate page faults per EU and per CS
	 *
	 * XXX: Multiplier required as compute UMD are getting PF queue errors
	 * without it. Follow on why this multiplier is required.
	 */
#define PF_MULTIPLIER	8
	pf_queue->size = (total_num_eus + XE_NUM_HW_ENGINES) *
		xe_pagefault_entry_size() * PF_MULTIPLIER;
	pf_queue->size = roundup_pow_of_two(pf_queue->size);
#undef PF_MULTIPLIER

	drm_dbg(&xe->drm, "xe_pagefault_entry_size=%d, total_num_eus=%d, pf_queue->size=%u",
		xe_pagefault_entry_size(), total_num_eus, pf_queue->size);

	spin_lock_init(&pf_queue->lock);
	INIT_WORK(&pf_queue->worker, xe_pagefault_queue_work);

	pf_queue->data = drmm_kzalloc(&xe->drm, pf_queue->size, GFP_KERNEL);
	if (!pf_queue->data)
		return -ENOMEM;

	return 0;
}

static void xe_pagefault_fini(void *arg)
{
	struct xe_device *xe = arg;

	destroy_workqueue(xe->usm.pf_wq);
}

/**
 * xe_pagefault_init() - Page fault init
 * @xe: xe device instance
 *
 * Initialize Xe page fault state. Must be done after reading fuses.
 *
 * Return: 0 on Success, errno on failure
 */
int xe_pagefault_init(struct xe_device *xe)
{
	int err, i;

	if (!xe->info.has_usm)
		return 0;

	xe->usm.pf_wq = alloc_workqueue("xe_page_fault_work_queue",
					WQ_UNBOUND | WQ_HIGHPRI,
					XE_PAGEFAULT_QUEUE_COUNT);
	if (!xe->usm.pf_wq)
		return -ENOMEM;

	for (i = 0; i < XE_PAGEFAULT_QUEUE_COUNT; ++i) {
		err = xe_pagefault_queue_init(xe, xe->usm.pf_queue + i);
		if (err)
			goto err_out;
	}

	return devm_add_action_or_reset(xe->drm.dev, xe_pagefault_fini, xe);

err_out:
	destroy_workqueue(xe->usm.pf_wq);
	return err;
}

/**
 * xe_pagefault_reset() - Page fault reset for a GT
 * @xe: xe device instance
 * @gt: GT being reset
 *
 * Reset the Xe page fault state for a GT; that is, squash any pending faults on
 * the GT.
 */
void xe_pagefault_reset(struct xe_device *xe, struct xe_gt *gt)
{
	/* TODO - implement */
}

/**
 * xe_pagefault_handler() - Page fault handler
 * @xe: xe device instance
 * @pf: Page fault
 *
 * Sink the page fault to a queue (i.e., a memory buffer) and queue a worker to
 * service it. Safe to be called from IRQ or process context. Reclaim safe.
 *
 * Return: 0 on success, errno on failure
 */
int xe_pagefault_handler(struct xe_device *xe, struct xe_pagefault *pf)
{
	/* TODO - implement */
	return 0;
}
