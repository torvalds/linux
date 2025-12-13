/* SPDX-License-Identifier: MIT */
/* Copyright © 2025 Intel Corporation */

#ifndef __INTEL_PANIC_H__
#define __INTEL_PANIC_H__

struct drm_scanout_buffer;
struct intel_panic;

struct intel_panic *intel_panic_alloc(void);
int intel_panic_setup(struct intel_panic *panic, struct drm_scanout_buffer *sb);
void intel_panic_finish(struct intel_panic *panic);

#endif /* __INTEL_PANIC_H__ */
