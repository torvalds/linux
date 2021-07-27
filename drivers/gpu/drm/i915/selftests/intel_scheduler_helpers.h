/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef _INTEL_SELFTEST_SCHEDULER_HELPERS_H_
#define _INTEL_SELFTEST_SCHEDULER_HELPERS_H_

#include <linux/types.h>

struct i915_request;
struct intel_engine_cs;

struct intel_selftest_saved_policy {
	u32 flags;
	u32 reset;
	u64 timeslice;
	u64 preempt_timeout;
};

int intel_selftest_modify_policy(struct intel_engine_cs *engine,
				 struct intel_selftest_saved_policy *saved);
int intel_selftest_restore_policy(struct intel_engine_cs *engine,
				  struct intel_selftest_saved_policy *saved);
int intel_selftest_wait_for_rq(struct i915_request *rq);

#endif
