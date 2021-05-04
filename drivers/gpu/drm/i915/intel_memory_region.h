/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_MEMORY_REGION_H__
#define __INTEL_MEMORY_REGION_H__

#include <linux/kref.h>
#include <linux/ioport.h>
#include <linux/mutex.h>
#include <linux/io-mapping.h>
#include <drm/drm_mm.h>

#include "i915_buddy.h"

struct drm_i915_private;
struct drm_i915_gem_object;
struct intel_memory_region;
struct sg_table;

/**
 *  Base memory type
 */
enum intel_memory_type {
	INTEL_MEMORY_SYSTEM = 0,
	INTEL_MEMORY_LOCAL,
	INTEL_MEMORY_STOLEN,
};

enum intel_region_id {
	INTEL_REGION_SMEM = 0,
	INTEL_REGION_LMEM,
	INTEL_REGION_STOLEN,
	INTEL_REGION_UNKNOWN, /* Should be last */
};

#define REGION_SMEM     BIT(INTEL_REGION_SMEM)
#define REGION_LMEM     BIT(INTEL_REGION_LMEM)
#define REGION_STOLEN   BIT(INTEL_REGION_STOLEN)

#define INTEL_MEMORY_TYPE_SHIFT 16

#define MEMORY_TYPE_FROM_REGION(r) (ilog2((r) >> INTEL_MEMORY_TYPE_SHIFT))
#define MEMORY_INSTANCE_FROM_REGION(r) (ilog2((r) & 0xffff))

#define I915_ALLOC_MIN_PAGE_SIZE  BIT(0)
#define I915_ALLOC_CONTIGUOUS     BIT(1)

#define for_each_memory_region(mr, i915, id) \
	for (id = 0; id < ARRAY_SIZE((i915)->mm.regions); id++) \
		for_each_if((mr) = (i915)->mm.regions[id])

struct intel_memory_region_ops {
	unsigned int flags;

	int (*init)(struct intel_memory_region *mem);
	void (*release)(struct intel_memory_region *mem);

	int (*init_object)(struct intel_memory_region *mem,
			   struct drm_i915_gem_object *obj,
			   resource_size_t size,
			   unsigned int flags);
};

struct intel_memory_region {
	struct drm_i915_private *i915;

	const struct intel_memory_region_ops *ops;

	struct io_mapping iomap;
	struct resource region;

	/* For fake LMEM */
	struct drm_mm_node fake_mappable;

	struct i915_buddy_mm mm;
	struct mutex mm_lock;

	struct kref kref;

	resource_size_t io_start;
	resource_size_t min_page_size;
	resource_size_t total;
	resource_size_t avail;

	unsigned int type;
	unsigned int instance;
	unsigned int id;
	char name[8];

	dma_addr_t remap_addr;

	struct {
		struct mutex lock; /* Protects access to objects */
		struct list_head list;
		struct list_head purgeable;
	} objects;
};

int intel_memory_region_init_buddy(struct intel_memory_region *mem);
void intel_memory_region_release_buddy(struct intel_memory_region *mem);

int __intel_memory_region_get_pages_buddy(struct intel_memory_region *mem,
					  resource_size_t size,
					  unsigned int flags,
					  struct list_head *blocks);
struct i915_buddy_block *
__intel_memory_region_get_block_buddy(struct intel_memory_region *mem,
				      resource_size_t size,
				      unsigned int flags);
void __intel_memory_region_put_pages_buddy(struct intel_memory_region *mem,
					   struct list_head *blocks);
void __intel_memory_region_put_block_buddy(struct i915_buddy_block *block);

struct intel_memory_region *
intel_memory_region_create(struct drm_i915_private *i915,
			   resource_size_t start,
			   resource_size_t size,
			   resource_size_t min_page_size,
			   resource_size_t io_start,
			   const struct intel_memory_region_ops *ops);

struct intel_memory_region *
intel_memory_region_get(struct intel_memory_region *mem);
void intel_memory_region_put(struct intel_memory_region *mem);

int intel_memory_regions_hw_probe(struct drm_i915_private *i915);
void intel_memory_regions_driver_release(struct drm_i915_private *i915);
struct intel_memory_region *
intel_memory_region_by_type(struct drm_i915_private *i915,
			    enum intel_memory_type mem_type);

__printf(2, 3) void
intel_memory_region_set_name(struct intel_memory_region *mem,
			     const char *fmt, ...);

#endif
