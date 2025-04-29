/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef __INTEL_DPT_H__
#define __INTEL_DPT_H__

#include <linux/types.h>

struct i915_address_space;
struct i915_vma;
struct intel_display;
struct intel_framebuffer;

void intel_dpt_destroy(struct i915_address_space *vm);
struct i915_vma *intel_dpt_pin_to_ggtt(struct i915_address_space *vm,
				       unsigned int alignment);
void intel_dpt_unpin_from_ggtt(struct i915_address_space *vm);
void intel_dpt_suspend(struct intel_display *display);
void intel_dpt_resume(struct intel_display *display);
struct i915_address_space *
intel_dpt_create(struct intel_framebuffer *fb);
u64 intel_dpt_offset(struct i915_vma *dpt_vma);

#endif /* __INTEL_DPT_H__ */
