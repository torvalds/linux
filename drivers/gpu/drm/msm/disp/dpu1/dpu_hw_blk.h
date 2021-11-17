/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.
 */

#ifndef _DPU_HW_BLK_H
#define _DPU_HW_BLK_H

#include <linux/types.h>
#include <linux/list.h>

struct dpu_hw_blk;


/**
 * struct dpu_hw_blk - definition of hardware block object
 * @list: list of hardware blocks
 * @type: hardware block type
 * @id: instance id
 * @refcount: reference/usage count
 */
struct dpu_hw_blk {
	/* opaque */
};

#endif /*_DPU_HW_BLK_H */
