// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__

#include <linux/mutex.h>
#include <linux/errno.h>
#include <linux/slab.h>

#include "dpu_hw_mdss.h"
#include "dpu_hw_blk.h"

/**
 * dpu_hw_blk_init - initialize hw block object
 * @hw_blk: pointer to hw block object
 * @type: hw block type - enum dpu_hw_blk_type
 * @id: instance id of the hw block
 */
void dpu_hw_blk_init(struct dpu_hw_blk *hw_blk, u32 type, int id)
{
	hw_blk->type = type;
	hw_blk->id = id;
}
