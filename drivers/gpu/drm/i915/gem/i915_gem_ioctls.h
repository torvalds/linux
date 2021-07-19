/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#ifndef I915_GEM_IOCTLS_H
#define I915_GEM_IOCTLS_H

struct drm_device;
struct drm_file;

int i915_gem_busy_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file);
int i915_gem_create_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file);
int i915_gem_execbuffer2_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file);
int i915_gem_get_aperture_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file);
int i915_gem_get_caching_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file);
int i915_gem_get_tiling_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file);
int i915_gem_madvise_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file);
int i915_gem_mmap_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file);
int i915_gem_mmap_offset_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file);
int i915_gem_pread_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file);
int i915_gem_pwrite_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file);
int i915_gem_set_caching_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file);
int i915_gem_set_domain_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file);
int i915_gem_set_tiling_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file);
int i915_gem_sw_finish_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file);
int i915_gem_throttle_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file);
int i915_gem_userptr_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file);
int i915_gem_wait_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file);

#endif
