/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */

#ifndef __IGT_GEM_UTILS_H__
#define __IGT_GEM_UTILS_H__

struct i915_request;
struct i915_gem_context;
struct intel_engine_cs;

struct i915_request *
igt_request_alloc(struct i915_gem_context *ctx, struct intel_engine_cs *engine);

#endif /* __IGT_GEM_UTILS_H__ */
