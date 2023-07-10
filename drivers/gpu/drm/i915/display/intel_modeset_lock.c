// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/drm_modeset_lock.h>

#include "intel_display_types.h"
#include "intel_modeset_lock.h"

void _intel_modeset_lock_begin(struct drm_modeset_acquire_ctx *ctx,
			       struct intel_atomic_state *state,
			       unsigned int flags, int *ret)
{
	drm_modeset_acquire_init(ctx, flags);

	if (state)
		state->base.acquire_ctx = ctx;

	*ret = -EDEADLK;
}

bool _intel_modeset_lock_loop(int *ret)
{
	if (*ret == -EDEADLK) {
		*ret = 0;
		return true;
	}

	return false;
}

void _intel_modeset_lock_end(struct drm_modeset_acquire_ctx *ctx,
			     struct intel_atomic_state *state,
			     int *ret)
{
	if (*ret == -EDEADLK) {
		if (state)
			drm_atomic_state_clear(&state->base);

		*ret = drm_modeset_backoff(ctx);
		if (*ret == 0) {
			*ret = -EDEADLK;
			return;
		}
	}

	drm_modeset_drop_locks(ctx);
	drm_modeset_acquire_fini(ctx);
}
