/* radeon_prime.h -- Private header for radeon driver -*- linux-c -*-
 *
 * Copyright 2012 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef __RADEON_PRIME_H__
#define __RADEON_PRIME_H__

struct dma_buf *radeon_gem_prime_export(struct drm_gem_object *gobj,
					int flags);
struct sg_table *radeon_gem_prime_get_sg_table(struct drm_gem_object *obj);
int radeon_gem_prime_pin(struct drm_gem_object *obj);
void radeon_gem_prime_unpin(struct drm_gem_object *obj);
void *radeon_gem_prime_vmap(struct drm_gem_object *obj);
void radeon_gem_prime_vunmap(struct drm_gem_object *obj, void *vaddr);
struct drm_gem_object *radeon_gem_prime_import_sg_table(struct drm_device *dev,
							struct dma_buf_attachment *,
							struct sg_table *sg);

#endif				/* __RADEON_PRIME_H__ */
