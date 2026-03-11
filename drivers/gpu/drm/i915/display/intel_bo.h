/* SPDX-License-Identifier: MIT */
/* Copyright © 2024 Intel Corporation */

#ifndef __INTEL_BO__
#define __INTEL_BO__

#include <linux/types.h>

struct drm_file;
struct drm_gem_object;
struct drm_mode_fb_cmd2;
struct drm_scanout_buffer;
struct intel_display;
struct intel_framebuffer;
struct seq_file;
struct vm_area_struct;

bool intel_bo_is_tiled(struct drm_gem_object *obj);
bool intel_bo_is_userptr(struct drm_gem_object *obj);
bool intel_bo_is_shmem(struct drm_gem_object *obj);
bool intel_bo_is_protected(struct drm_gem_object *obj);
int intel_bo_key_check(struct drm_gem_object *obj);
int intel_bo_fb_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma);
int intel_bo_read_from_page(struct drm_gem_object *obj, u64 offset, void *dst, int size);

void intel_bo_describe(struct seq_file *m, struct drm_gem_object *obj);

void intel_bo_framebuffer_fini(struct drm_gem_object *obj);
int intel_bo_framebuffer_init(struct drm_gem_object *obj, struct drm_mode_fb_cmd2 *mode_cmd);
struct drm_gem_object *intel_bo_framebuffer_lookup(struct intel_display *display,
						   struct drm_file *filp,
						   const struct drm_mode_fb_cmd2 *user_mode_cmd);

#endif /* __INTEL_BO__ */
