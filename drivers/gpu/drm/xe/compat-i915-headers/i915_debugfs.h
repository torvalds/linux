/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __I915_DEBUGFS_H__
#define __I915_DEBUGFS_H__

struct drm_i915_gem_object;
struct seq_file;

static inline void i915_debugfs_describe_obj(struct seq_file *m, struct drm_i915_gem_object *obj) {}

#endif /* __I915_DEBUGFS_H__ */
