// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "i915_active.h"
#include "intel_context.h"
#include "intel_context_param.h"
#include "intel_ring.h"

int intel_context_set_ring_size(struct intel_context *ce, long sz)
{
	int err;

	if (intel_context_lock_pinned(ce))
		return -EINTR;

	err = i915_active_wait(&ce->active);
	if (err < 0)
		goto unlock;

	if (intel_context_is_pinned(ce)) {
		err = -EBUSY; /* In active use, come back later! */
		goto unlock;
	}

	if (test_bit(CONTEXT_ALLOC_BIT, &ce->flags)) {
		struct intel_ring *ring;

		/* Replace the existing ringbuffer */
		ring = intel_engine_create_ring(ce->engine, sz);
		if (IS_ERR(ring)) {
			err = PTR_ERR(ring);
			goto unlock;
		}

		intel_ring_put(ce->ring);
		ce->ring = ring;

		/* Context image will be updated on next pin */
	} else {
		ce->ring = __intel_context_ring_size(sz);
	}

unlock:
	intel_context_unlock_pinned(ce);
	return err;
}

long intel_context_get_ring_size(struct intel_context *ce)
{
	long sz = (unsigned long)READ_ONCE(ce->ring);

	if (test_bit(CONTEXT_ALLOC_BIT, &ce->flags)) {
		if (intel_context_lock_pinned(ce))
			return -EINTR;

		sz = ce->ring->size;
		intel_context_unlock_pinned(ce);
	}

	return sz;
}
