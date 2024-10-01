/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef __I915_CMD_PARSER_H__
#define __I915_CMD_PARSER_H__

#include <linux/types.h>

struct drm_i915_private;
struct intel_engine_cs;
struct i915_vma;

int i915_cmd_parser_get_version(struct drm_i915_private *dev_priv);
int intel_engine_init_cmd_parser(struct intel_engine_cs *engine);
void intel_engine_cleanup_cmd_parser(struct intel_engine_cs *engine);
int intel_engine_cmd_parser(struct intel_engine_cs *engine,
			    struct i915_vma *batch,
			    unsigned long batch_offset,
			    unsigned long batch_length,
			    struct i915_vma *shadow,
			    bool trampoline);
#define I915_CMD_PARSER_TRAMPOLINE_SIZE 8

#endif /* __I915_CMD_PARSER_H__ */
