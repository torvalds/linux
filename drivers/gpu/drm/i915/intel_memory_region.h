/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_MEMORY_REGION_H__
#define __INTEL_MEMORY_REGION_H__

#include <linux/ioport.h>
#include <linux/mutex.h>
#include <linux/io-mapping.h>
#include <drm/drm_mm.h>
#include <drm/i915_drm.h>

struct drm_i915_private;
struct drm_i915_gem_object;
struct drm_printer;
struct intel_memory_region;
struct sg_table;
struct ttm_resource;

enum intel_memory_type {
	INTEL_MEMORY_SYSTEM = I915_MEMORY_CLASS_SYSTEM,
	INTEL_MEMORY_LOCAL = I915_MEMORY_CLASS_DEVICE,
	INTEL_MEMORY_STOLEN_SYSTEM,
	INTEL_MEMORY_STOLEN_LOCAL,
	INTEL_MEMORY_MOCK,
};

enum intel_region_id {
	INTEL_REGION_SMEM = 0,
	INTEL_REGION_LMEM,
	INTEL_REGION_STOLEN_SMEM,
	INTEL_REGION_STOLEN_LMEM,
	INTEL_REGION_UNKNOWN, /* Should be last */
};

#define REGION_SMEM     BIT(INTEL_REGION_SMEM)
#define REGION_LMEM     BIT(INTEL_REGION_LMEM)
#define REGION_STOLEN_SMEM   BIT(INTEL_REGION_STOLEN_SMEM)
#define REGION_STOLEN_LMEM   BIT(INTEL_REGION_STOLEN_LMEM)

#define I915_ALLOC_CONTIGUOUS     BIT(0)

#define for_each_memory_region(mr, i915, id) \
	for (id = 0; id < ARRAY_SIZE((i915)->mm.regions); id++) \
		for_each_if((mr) = (i915)->mm.regions[id])

struct intel_memory_region_ops {
	unsigned int flags;

	int (*init)(struct intel_memory_region *mem);
	int (*release)(struct intel_memory_region *mem);

	int (*init_object)(struct intel_memory_region *mem,
			   struct drm_i915_gem_object *obj,
			   resource_size_t size,
			   resource_size_t page_size,
			   unsigned int flags);
};

struct intel_memory_region {
	struct drm_i915_private *i915;

	const struct intel_memory_region_ops *ops;

	struct io_mapping iomap;
	struct resource region;

	/* For fake LMEM */
	struct drm_mm_node fake_mappable;

	resource_size_t io_start;
	resource_size_t min_page_size;
	resource_size_t total;
	resource_size_t avail;

	u16 type;
	u16 instance;
	enum intel_region_id id;
	char name[16];
	bool private; /* not for userspace */

	dma_addr_t remap_addr;

	struct {
		struct mutex lock; /* Protects access to objects */
		struct list_head list;
	} objects;

	bool is_range_manager;

	void *region_private;
};

struct intel_memory_region *
intel_memory_region_lookup(struct drm_i915_private *i915,
			   u16 class, u16 instance);

struct intel_memory_region *
intel_memory_region_create(struct drm_i915_private *i915,
			   resource_size_t start,
			   resource_size_t size,
			   resource_size_t min_page_size,
			   resource_size_t io_start,
			   u16 type,
			   u16 instance,
			   const struct intel_memory_region_ops *ops);

void intel_memory_region_destroy(struct intel_memory_region *mem);

int intel_memory_regions_hw_probe(struct drm_i915_private *i915);
void intel_memory_regions_driver_release(struct drm_i915_private *i915);
struct intel_memory_region *
intel_memory_region_by_type(struct drm_i915_private *i915,
			    enum intel_memory_type mem_type);

__printf(2, 3) void
intel_memory_region_set_name(struct intel_memory_region *mem,
			     const char *fmt, ...);

int intel_memory_region_reserve(struct intel_memory_region *mem,
				resource_size_t offset,
				resource_size_t size);

void intel_memory_region_debug(struct intel_memory_region *mr,
			       struct drm_printer *printer);

struct intel_memory_region *
i915_gem_ttm_system_setup(struct drm_i915_private *i915,
			  u16 type, u16 instance);
struct intel_memory_region *
i915_gem_shmem_setup(struct drm_i915_private *i915,
		     u16 type, u16 instance);

#endif
