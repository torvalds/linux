/* SPDX-License-Identifier: MIT */
/* Copyright © 2025 Intel Corporation */

#ifndef __INTEL_PANIC_H__
#define __INTEL_PANIC_H__

struct drm_scanout_buffer;
struct intel_framebuffer;

struct intel_framebuffer *intel_bo_alloc_framebuffer(void);
int intel_panic_setup(struct drm_scanout_buffer *sb);
void intel_panic_finish(struct intel_framebuffer *fb);

#endif /* __INTEL_PANIC_H__ */
