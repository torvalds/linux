/* SPDX-License-Identifier: GPL-2.0-only */
/**************************************************************************
 * Copyright (c) 2007-2008, Intel Corporation.
 * All Rights Reserved.
 *
 **************************************************************************/

#ifndef _PSB_GTT_H_
#define _PSB_GTT_H_

#include <drm/drm_gem.h>

/* This wants cleaning up with respect to the psb_dev and un-needed stuff */
struct psb_gtt {
	uint32_t gatt_start;
	uint32_t mmu_gatt_start;
	uint32_t gtt_start;
	uint32_t gtt_phys_start;
	unsigned gtt_pages;
	unsigned gatt_pages;
	unsigned long stolen_size;
	unsigned long vram_stolen_size;
	struct rw_semaphore sem;
};

/* Exported functions */
extern int psb_gtt_init(struct drm_device *dev, int resume);
extern void psb_gtt_takedown(struct drm_device *dev);

/* Each gtt_range describes an allocation in the GTT area */
struct gtt_range {
	struct resource resource;	/* Resource for our allocation */
	u32 offset;			/* GTT offset of our object */
	struct drm_gem_object gem;	/* GEM high level stuff */
	int in_gart;			/* Currently in the GART (ref ct) */
	bool stolen;			/* Backed from stolen RAM */
	bool mmapping;			/* Is mmappable */
	struct page **pages;		/* Backing pages if present */
	int npage;			/* Number of backing pages */
	int roll;			/* Roll applied to the GTT entries */
};

#define to_gtt_range(x) container_of(x, struct gtt_range, gem)

extern struct gtt_range *psb_gtt_alloc_range(struct drm_device *dev, int len,
					     const char *name, int backed,
					     u32 align);
extern void psb_gtt_kref_put(struct gtt_range *gt);
extern void psb_gtt_free_range(struct drm_device *dev, struct gtt_range *gt);
extern int psb_gtt_pin(struct gtt_range *gt);
extern void psb_gtt_unpin(struct gtt_range *gt);
extern void psb_gtt_roll(struct drm_device *dev,
					struct gtt_range *gt, int roll);
extern int psb_gtt_restore(struct drm_device *dev);
#endif
