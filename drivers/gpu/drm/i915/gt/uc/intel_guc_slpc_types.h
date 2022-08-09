/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef _INTEL_GUC_SLPC_TYPES_H_
#define _INTEL_GUC_SLPC_TYPES_H_

#include <linux/atomic.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/types.h>

#define SLPC_RESET_TIMEOUT_MS 5

struct intel_guc_slpc {
	struct i915_vma *vma;
	struct slpc_shared_data *vaddr;
	bool supported;
	bool selected;

	/* platform frequency limits */
	u32 min_freq;
	u32 rp0_freq;
	u32 rp1_freq;
	u32 boost_freq;

	/* frequency softlimits */
	u32 min_freq_softlimit;
	u32 max_freq_softlimit;

	/* Protects set/reset of boost freq
	 * and value of num_waiters
	 */
	struct mutex lock;

	struct work_struct boost_work;
	atomic_t num_waiters;
	u32 num_boosts;
};

#endif
