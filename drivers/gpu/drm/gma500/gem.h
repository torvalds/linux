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

struct gtt_range *
psb_gem_create(struct drm_device *dev, u64 size, const char *name, bool stolen, u32 align);

int psb_gem_pin(struct gtt_range *gt);
void psb_gem_unpin(struct gtt_range *gt);

#endif
