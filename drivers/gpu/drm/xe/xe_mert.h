/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2025, Intel Corporation. All rights reserved.
 */

#ifndef __XE_MERT_H__
#define __XE_MERT_H__

#include <linux/completion.h>
#include <linux/spinlock.h>
#include <linux/types.h>

struct xe_device;
struct xe_tile;

struct xe_mert {
	/** @lock: protects the TLB invalidation status */
	spinlock_t lock;
	/** @tlb_inv_triggered: indicates if TLB invalidation was triggered */
	bool tlb_inv_triggered;
	/** @mert.tlb_inv_done: completion of TLB invalidation */
	struct completion tlb_inv_done;
};

#ifdef CONFIG_PCI_IOV
int xe_mert_invalidate_lmtt(struct xe_tile *tile);
void xe_mert_irq_handler(struct xe_device *xe, u32 master_ctl);
#else
static inline void xe_mert_irq_handler(struct xe_device *xe, u32 master_ctl) { }
#endif

#endif /* __XE_MERT_H__ */
