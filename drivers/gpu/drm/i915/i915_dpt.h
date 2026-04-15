/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Intel Corporation */

#ifndef __I915_DPT_H__
#define __I915_DPT_H__

#include <linux/types.h>

struct i915_address_space;
struct i915_vma;
struct intel_dpt;

struct i915_address_space *i915_dpt_to_vm(struct intel_dpt *dpt);
struct i915_vma *i915_dpt_pin_to_ggtt(struct intel_dpt *dpt, unsigned int alignment);
void i915_dpt_unpin_from_ggtt(struct intel_dpt *dpt);
u64 i915_dpt_offset(struct i915_vma *dpt_vma);

extern const struct intel_display_dpt_interface i915_display_dpt_interface;

#endif /* __I915_DPT_H__ */
