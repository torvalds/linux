// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "intel_gt.h"

void intel_gt_init_early(struct intel_gt *gt)
{
	INIT_LIST_HEAD(&gt->active_rings);
	INIT_LIST_HEAD(&gt->closed_vma);

	spin_lock_init(&gt->closed_lock);
}
