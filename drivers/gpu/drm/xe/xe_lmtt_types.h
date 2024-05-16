/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_LMTT_TYPES_H_
#define _XE_LMTT_TYPES_H_

#include <linux/types.h>

struct xe_bo;
struct xe_lmtt;
struct xe_lmtt_pt;
struct xe_lmtt_ops;

#define LMTT_PTE_INVALID	ULL(0)

/**
 * struct xe_lmtt - Local Memory Translation Table Manager
 */
struct xe_lmtt {
	/** @pd: root LMTT Directory */
	struct xe_lmtt_pt *pd;

	/** @ops: LMTT functions */
	const struct xe_lmtt_ops *ops;
};

/**
 * struct xe_lmtt_pt - Local Memory Translation Table Page Table
 *
 * Represents single level of the LMTT.
 */
struct xe_lmtt_pt {
	/** @level: page table level, 0 is leaf */
	unsigned int level;

	/** @bo: buffer object with actual LMTT PTE values */
	struct xe_bo *bo;

	/** @entries: leaf page tables, exist only for root/non-leaf */
	struct xe_lmtt_pt *entries[];
};

/**
 * struct xe_lmtt_ops - Local Memory Translation Table Operations
 *
 * Provides abstraction of the LMTT variants.
 */
struct xe_lmtt_ops {
	/* private: */
	unsigned int (*lmtt_root_pd_level)(void);
	unsigned int (*lmtt_pte_num)(unsigned int level);
	unsigned int (*lmtt_pte_size)(unsigned int level);
	unsigned int (*lmtt_pte_shift)(unsigned int level);
	unsigned int (*lmtt_pte_index)(u64 addr, unsigned int level);
	u64 (*lmtt_pte_encode)(unsigned long offset, unsigned int level);
};

extern const struct xe_lmtt_ops lmtt_2l_ops;
extern const struct xe_lmtt_ops lmtt_ml_ops;

#endif
