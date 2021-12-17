// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "i915_vma_snapshot.h"
#include "i915_vma_types.h"
#include "i915_vma.h"

/**
 * i915_vma_snapshot_init - Initialize a struct i915_vma_snapshot from
 * a struct i915_vma.
 * @vsnap: The i915_vma_snapshot to init.
 * @vma: A struct i915_vma used to initialize @vsnap.
 * @name: Name associated with the snapshot. The character pointer needs to
 * stay alive over the lifitime of the shapsot
 */
void i915_vma_snapshot_init(struct i915_vma_snapshot *vsnap,
			    struct i915_vma *vma,
			    const char *name)
{
	if (!i915_vma_is_pinned(vma))
		assert_object_held(vma->obj);

	vsnap->name = name;
	vsnap->size = vma->size;
	vsnap->obj_size = vma->obj->base.size;
	vsnap->gtt_offset = vma->node.start;
	vsnap->gtt_size = vma->node.size;
	vsnap->page_sizes = vma->page_sizes.gtt;
	vsnap->pages = vma->pages;
	vsnap->pages_rsgt = NULL;
	vsnap->mr = NULL;
	if (vma->obj->mm.rsgt)
		vsnap->pages_rsgt = i915_refct_sgt_get(vma->obj->mm.rsgt);
	vsnap->mr = vma->obj->mm.region;
	kref_init(&vsnap->kref);
	vsnap->vma_resource = &vma->active;
	vsnap->onstack = false;
	vsnap->present = true;
}

/**
 * i915_vma_snapshot_init_onstack - Initialize a struct i915_vma_snapshot from
 * a struct i915_vma, but avoid kfreeing it on last put.
 * @vsnap: The i915_vma_snapshot to init.
 * @vma: A struct i915_vma used to initialize @vsnap.
 * @name: Name associated with the snapshot. The character pointer needs to
 * stay alive over the lifitime of the shapsot
 */
void i915_vma_snapshot_init_onstack(struct i915_vma_snapshot *vsnap,
				    struct i915_vma *vma,
				    const char *name)
{
	i915_vma_snapshot_init(vsnap, vma, name);
	vsnap->onstack = true;
}

static void vma_snapshot_release(struct kref *ref)
{
	struct i915_vma_snapshot *vsnap =
		container_of(ref, typeof(*vsnap), kref);

	vsnap->present = false;
	if (vsnap->pages_rsgt)
		i915_refct_sgt_put(vsnap->pages_rsgt);
	if (!vsnap->onstack)
		kfree(vsnap);
}

/**
 * i915_vma_snapshot_put - Put an i915_vma_snapshot pointer reference
 * @vsnap: The pointer reference
 */
void i915_vma_snapshot_put(struct i915_vma_snapshot *vsnap)
{
	kref_put(&vsnap->kref, vma_snapshot_release);
}

/**
 * i915_vma_snapshot_put_onstack - Put an onstcak i915_vma_snapshot pointer
 * reference and varify that the structure is released
 * @vsnap: The pointer reference
 *
 * This function is intended to be paired with a i915_vma_init_onstack()
 * and should be called before exiting the scope that declared or
 * freeing the structure that embedded @vsnap to verify that all references
 * have been released.
 */
void i915_vma_snapshot_put_onstack(struct i915_vma_snapshot *vsnap)
{
	if (!kref_put(&vsnap->kref, vma_snapshot_release))
		GEM_BUG_ON(1);
}

/**
 * i915_vma_snapshot_resource_pin - Temporarily block the memory the
 * vma snapshot is pointing to from being released.
 * @vsnap: The vma snapshot.
 * @lockdep_cookie: Pointer to bool needed for lockdep support. This needs
 * to be passed to the paired i915_vma_snapshot_resource_unpin.
 *
 * This function will temporarily try to hold up a fence or similar structure
 * and will therefore enter a fence signaling critical section.
 *
 * Return: true if we succeeded in blocking the memory from being released,
 * false otherwise.
 */
bool i915_vma_snapshot_resource_pin(struct i915_vma_snapshot *vsnap,
				    bool *lockdep_cookie)
{
	bool pinned = i915_active_acquire_if_busy(vsnap->vma_resource);

	if (pinned)
		*lockdep_cookie = dma_fence_begin_signalling();

	return pinned;
}

/**
 * i915_vma_snapshot_resource_unpin - Unblock vma snapshot memory from
 * being released.
 * @vsnap: The vma snapshot.
 * @lockdep_cookie: Cookie returned from matching i915_vma_resource_pin().
 *
 * Might leave a fence signalling critical section and signal a fence.
 */
void i915_vma_snapshot_resource_unpin(struct i915_vma_snapshot *vsnap,
				      bool lockdep_cookie)
{
	dma_fence_end_signalling(lockdep_cookie);

	return i915_active_release(vsnap->vma_resource);
}
