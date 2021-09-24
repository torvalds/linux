/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2020, Intel Corporation. All rights reserved.
 */

#ifndef __INTEL_PXP_TYPES_H__
#define __INTEL_PXP_TYPES_H__

#include <linux/completion.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/workqueue.h>

struct intel_context;
struct i915_pxp_component;

struct intel_pxp {
	struct i915_pxp_component *pxp_component;
	bool pxp_component_added;

	struct intel_context *ce;

	/*
	 * After a teardown, the arb session can still be in play on the HW
	 * even if the keys are gone, so we can't rely on the HW state of the
	 * session to know if it's valid and need to track the status in SW.
	 */
	bool arb_is_valid;

	/*
	 * Keep track of which key instance we're on, so we can use it to
	 * determine if an object was created using the current key or a
	 * previous one.
	 */
	u32 key_instance;

	struct mutex tee_mutex; /* protects the tee channel binding */

	/*
	 * If the HW perceives an attack on the integrity of the encryption it
	 * will invalidate the keys and expect SW to re-initialize the session.
	 * We keep track of this state to make sure we only re-start the arb
	 * session when required.
	 */
	bool hw_state_invalidated;

	bool irq_enabled;
	struct completion termination;

	struct work_struct session_work;
	u32 session_events; /* protected with gt->irq_lock */
#define PXP_TERMINATION_REQUEST  BIT(0)
#define PXP_TERMINATION_COMPLETE BIT(1)
};

#endif /* __INTEL_PXP_TYPES_H__ */
