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
};

#define to_gtt_range(x) container_of(x, struct gtt_range, gem)

extern int psb_gtt_restore(struct drm_device *dev);

int psb_gtt_insert(struct drm_device *dev, struct gtt_range *r, int resume);
void psb_gtt_remove(struct drm_device *dev, struct gtt_range *r);

#endif
