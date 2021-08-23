/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef __INTEL_DPT_H__
#define __INTEL_DPT_H__

struct i915_address_space;
struct i915_vma;
struct intel_framebuffer;

void intel_dpt_destroy(struct i915_address_space *vm);
struct i915_vma *intel_dpt_pin(struct i915_address_space *vm);
void intel_dpt_unpin(struct i915_address_space *vm);
struct i915_address_space *
intel_dpt_create(struct intel_framebuffer *fb);

#endif /* __INTEL_DPT_H__ */
