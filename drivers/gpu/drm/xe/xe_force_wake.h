/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_FORCE_WAKE_H_
#define _XE_FORCE_WAKE_H_

#include "xe_assert.h"
#include "xe_force_wake_types.h"

struct xe_gt;

void xe_force_wake_init_gt(struct xe_gt *gt,
			   struct xe_force_wake *fw);
void xe_force_wake_init_engines(struct xe_gt *gt,
				struct xe_force_wake *fw);
int xe_force_wake_get(struct xe_force_wake *fw,
		      enum xe_force_wake_domains domains);
int xe_force_wake_put(struct xe_force_wake *fw,
		      enum xe_force_wake_domains domains);

static inline int
xe_force_wake_ref(struct xe_force_wake *fw,
		  enum xe_force_wake_domains domain)
{
	xe_gt_assert(fw->gt, domain);
	return fw->domains[ffs(domain) - 1].ref;
}

static inline void
xe_force_wake_assert_held(struct xe_force_wake *fw,
			  enum xe_force_wake_domains domain)
{
	xe_gt_assert(fw->gt, fw->awake_domains & domain);
}

#endif
