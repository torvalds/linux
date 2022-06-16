/* SPDX-License-Identifier: GPL-2.0-only */
/**************************************************************************
 * Copyright (c) 2014 Patrik Jakobsson
 * All Rights Reserved.
 *
 **************************************************************************/

#ifndef _GEM_H
#define _GEM_H

#include <linux/kernel.h>

#include <drm/drm_gem.h>

struct drm_device;

/*
 * PSB GEM object
 */

struct psb_gem_object {
	struct drm_gem_object base;

	struct resource resource;	/* GTT resource for our allocation */
	u32 offset;			/* GTT offset of our object */
	int in_gart;			/* Currently in the GART (ref ct) */
	bool stolen;			/* Backed from stolen RAM */
	bool mmapping;			/* Is mmappable */
	struct page **pages;		/* Backing pages if present */
};

static inline struct psb_gem_object *to_psb_gem_object(struct drm_gem_object *obj)
{
	return container_of(obj, struct psb_gem_object, base);
}

struct psb_gem_object *
psb_gem_create(struct drm_device *dev, u64 size, const char *name, bool stolen, u32 align);

int psb_gem_pin(struct psb_gem_object *pobj);
void psb_gem_unpin(struct psb_gem_object *pobj);

/*
 * Memory management
 */

int psb_gem_mm_init(struct drm_device *dev);
void psb_gem_mm_fini(struct drm_device *dev);
int psb_gem_mm_resume(struct drm_device *dev);

#endif
