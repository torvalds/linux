/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.
 */

#ifndef _DPU_HW_BLK_H
#define _DPU_HW_BLK_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/atomic.h>

struct dpu_hw_blk;

/**
 * struct dpu_hw_blk_ops - common hardware block operations
 * @start: start operation on first get
 * @stop: stop operation on last put
 */
struct dpu_hw_blk_ops {
	int (*start)(struct dpu_hw_blk *);
	void (*stop)(struct dpu_hw_blk *);
};

/**
 * struct dpu_hw_blk - definition of hardware block object
 * @list: list of hardware blocks
 * @type: hardware block type
 * @id: instance id
 * @refcount: reference/usage count
 */
struct dpu_hw_blk {
	struct list_head list;
	u32 type;
	int id;
	atomic_t refcount;
	struct dpu_hw_blk_ops ops;
};

void dpu_hw_blk_init(struct dpu_hw_blk *hw_blk, u32 type, int id,
		struct dpu_hw_blk_ops *ops);
void dpu_hw_blk_destroy(struct dpu_hw_blk *hw_blk);

struct dpu_hw_blk *dpu_hw_blk_get(struct dpu_hw_blk *hw_blk, u32 type, int id);
void dpu_hw_blk_put(struct dpu_hw_blk *hw_blk);
#endif /*_DPU_HW_BLK_H */
