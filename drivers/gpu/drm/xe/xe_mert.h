/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2025, Intel Corporation. All rights reserved.
 */

#ifndef _XE_MERT_H_
#define _XE_MERT_H_

#include <linux/completion.h>
#include <linux/spinlock.h>
#include <linux/types.h>

struct xe_device;

/**
 * struct xe_mert - MERT related data
 */
struct xe_mert {
	/** @lock: protects the TLB invalidation status */
	spinlock_t lock;
	/** @tlb_inv_triggered: indicates if TLB invalidation was triggered */
	bool tlb_inv_triggered;
	/** @tlb_inv_done: completion of TLB invalidation */
	struct completion tlb_inv_done;
};

#ifdef CONFIG_PCI_IOV
void xe_mert_init_early(struct xe_device *xe);
int xe_mert_invalidate_lmtt(struct xe_device *xe);
void xe_mert_irq_handler(struct xe_device *xe, u32 master_ctl);
#else
static inline void xe_mert_irq_handler(struct xe_device *xe, u32 master_ctl) { }
#endif

#endif
