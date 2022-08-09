/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */
#ifndef _I915_VMA_SNAPSHOT_H_
#define _I915_VMA_SNAPSHOT_H_

#include <linux/kref.h>
#include <linux/slab.h>
#include <linux/types.h>

struct i915_active;
struct i915_refct_sgt;
struct i915_vma;
struct intel_memory_region;
struct sg_table;

/**
 * DOC: Simple utilities for snapshotting GPU vma metadata, later used for
 * error capture. Vi use a separate header for this to avoid issues due to
 * recursive header includes.
 */

/**
 * struct i915_vma_snapshot - Snapshot of vma metadata.
 * @size: The vma size in bytes.
 * @obj_size: The size of the underlying object in bytes.
 * @gtt_offset: The gtt offset the vma is bound to.
 * @gtt_size: The size in bytes allocated for the vma in the GTT.
 * @pages: The struct sg_table pointing to the pages bound.
 * @pages_rsgt: The refcounted sg_table holding the reference for @pages if any.
 * @mr: The memory region pointed for the pages bound.
 * @kref: Reference for this structure.
 * @vma_resource: FIXME: A means to keep the unbind fence from signaling.
 * Temporarily while we have only sync unbinds, and still use the vma
 * active, we use that. With async unbinding we need a signaling refcount
 * for the unbind fence.
 * @page_sizes: The vma GTT page sizes information.
 * @onstack: Whether the structure shouldn't be freed on final put.
 * @present: Whether the structure is present and initialized.
 */
struct i915_vma_snapshot {
	const char *name;
	size_t size;
	size_t obj_size;
	size_t gtt_offset;
	size_t gtt_size;
	struct sg_table *pages;
	struct i915_refct_sgt *pages_rsgt;
	struct intel_memory_region *mr;
	struct kref kref;
	struct i915_active *vma_resource;
	u32 page_sizes;
	bool onstack:1;
	bool present:1;
};

void i915_vma_snapshot_init(struct i915_vma_snapshot *vsnap,
			    struct i915_vma *vma,
			    const char *name);

void i915_vma_snapshot_init_onstack(struct i915_vma_snapshot *vsnap,
				    struct i915_vma *vma,
				    const char *name);

void i915_vma_snapshot_put(struct i915_vma_snapshot *vsnap);

void i915_vma_snapshot_put_onstack(struct i915_vma_snapshot *vsnap);

bool i915_vma_snapshot_resource_pin(struct i915_vma_snapshot *vsnap,
				    bool *lockdep_cookie);

void i915_vma_snapshot_resource_unpin(struct i915_vma_snapshot *vsnap,
				      bool lockdep_cookie);

/**
 * i915_vma_snapshot_alloc - Allocate a struct i915_vma_snapshot
 * @gfp: Allocation mode.
 *
 * Return: A pointer to a struct i915_vma_snapshot if successful.
 * NULL otherwise.
 */
static inline struct i915_vma_snapshot *i915_vma_snapshot_alloc(gfp_t gfp)
{
	return kmalloc(sizeof(struct i915_vma_snapshot), gfp);
}

/**
 * i915_vma_snapshot_get - Take a reference on a struct i915_vma_snapshot
 *
 * Return: A pointer to a struct i915_vma_snapshot.
 */
static inline struct i915_vma_snapshot *
i915_vma_snapshot_get(struct i915_vma_snapshot *vsnap)
{
	kref_get(&vsnap->kref);
	return vsnap;
}

/**
 * i915_vma_snapshot_present - Whether a struct i915_vma_snapshot is
 * present and initialized.
 *
 * Return: true if present and initialized; false otherwise.
 */
static inline bool
i915_vma_snapshot_present(const struct i915_vma_snapshot *vsnap)
{
	return vsnap && vsnap->present;
}

#endif
