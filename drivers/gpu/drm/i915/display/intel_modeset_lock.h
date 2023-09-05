/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_MODESET_LOCK_H__
#define __INTEL_MODESET_LOCK_H__

#include <linux/types.h>

struct drm_modeset_acquire_ctx;
struct intel_atomic_state;

void _intel_modeset_lock_begin(struct drm_modeset_acquire_ctx *ctx,
			       struct intel_atomic_state *state,
			       unsigned int flags,
			       int *ret);
bool _intel_modeset_lock_loop(int *ret);
void _intel_modeset_lock_end(struct drm_modeset_acquire_ctx *ctx,
			     struct intel_atomic_state *state,
			     int *ret);

/*
 * Note that one must always use "continue" rather than
 * "break" or "return" to handle errors within the
 * intel_modeset_lock_ctx_retry() block.
 */
#define intel_modeset_lock_ctx_retry(ctx, state, flags, ret) \
	for (_intel_modeset_lock_begin((ctx), (state), (flags), &(ret)); \
	     _intel_modeset_lock_loop(&(ret)); \
	     _intel_modeset_lock_end((ctx), (state), &(ret)))

#endif /* __INTEL_MODESET_LOCK_H__ */
