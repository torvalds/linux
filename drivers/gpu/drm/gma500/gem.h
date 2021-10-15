/* SPDX-License-Identifier: GPL-2.0-only */
/**************************************************************************
 * Copyright (c) 2014 Patrik Jakobsson
 * All Rights Reserved.
 *
 **************************************************************************/

#ifndef _GEM_H
#define _GEM_H

#include <drm/drm_gem.h>

struct drm_device;

extern const struct drm_gem_object_funcs psb_gem_object_funcs;

extern int psb_gem_create(struct drm_file *file, struct drm_device *dev,
			  u64 size, u32 *handlep, int stolen, u32 align);

struct gtt_range *psb_gtt_alloc_range(struct drm_device *dev, int len, const char *name,
				      int backed, u32 align);
void psb_gtt_free_range(struct drm_device *dev, struct gtt_range *gt);
int psb_gtt_pin(struct gtt_range *gt);
void psb_gtt_unpin(struct gtt_range *gt);

#endif
