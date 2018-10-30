/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 */
/*
 * Authors:
 *    Christian König <christian.koenig@amd.com>
 */

#include <drm/drmP.h>
#include "amdgpu.h"
#include "amdgpu_trace.h"
#include "amdgpu_amdkfd.h"

struct amdgpu_sync_entry {
	struct hlist_node	node;
	struct dma_fence	*fence;
	bool	explicit;
};

static struct kmem_cache *amdgpu_sync_slab;

/**
 * amdgpu_sync_create - zero init sync object
 *
 * @sync: sync object to initialize
 *
 * Just clear the sync object for now.
 */
void amdgpu_sync_create(struct amdgpu_sync *sync)
{
	hash_init(sync->fences);
	sync->last_vm_update = NULL;
}

/**
 * amdgpu_sync_same_dev - test if fence belong to us
 *
 * @adev: amdgpu device to use for the test
 * @f: fence to test
 *
 * Test if the fence was issued by us.
 */
static bool amdgpu_sync_same_dev(struct amdgpu_device *adev,
				 struct dma_fence *f)
{
	struct drm_sched_fence *s_fence = to_drm_sched_fence(f);

	if (s_fence) {
		struct amdgpu_ring *ring;

		ring = container_of(s_fence->sched, struct amdgpu_ring, sched);
		return ring->adev == adev;
	}

	return false;
}

/**
 * amdgpu_sync_get_owner - extract the owner of a fence
 *
 * @fence: fence get the owner from
 *
 * Extract who originally created the fence.
 */
static void *amdgpu_sync_get_owner(struct dma_fence *f)
{
	struct drm_sched_fence *s_fence;
	struct amdgpu_amdkfd_fence *kfd_fence;

	if (!f)
		return AMDGPU_FENCE_OWNER_UNDEFINED;

	s_fence = to_drm_sched_fence(f);
	if (s_fence)
		return s_fence->owner;

	kfd_fence = to_amdgpu_amdkfd_fence(f);
	if (kfd_fence)
		return AMDGPU_FENCE_OWNER_KFD;

	return AMDGPU_FENCE_OWNER_UNDEFINED;
}

/**
 * amdgpu_sync_keep_later - Keep the later fence
 *
 * @keep: existing fence to test
 * @fence: new fence
 *
 * Either keep the existing fence or the new one, depending which one is later.
 */
static void amdgpu_sync_keep_later(struct dma_fence **keep,
				   struct dma_fence *fence)
{
	if (*keep && dma_fence_is_later(*keep, fence))
		return;

	dma_fence_put(*keep);
	*keep = dma_fence_get(fence);
}

/**
 * amdgpu_sync_add_later - add the fence to the hash
 *
 * @sync: sync object to add the fence to
 * @f: fence to add
 *
 * Tries to add the fence to an existing hash entry. Returns true when an entry
 * was found, false otherwise.
 */
static bool amdgpu_sync_add_later(struct amdgpu_sync *sync, struct dma_fence *f, bool explicit)
{
	struct amdgpu_sync_entry *e;

	hash_for_each_possible(sync->fences, e, node, f->context) {
		if (unlikely(e->fence->context != f->context))
			continue;

		amdgpu_sync_keep_later(&e->fence, f);

		/* Preserve eplicit flag to not loose pipe line sync */
		e->explicit |= explicit;

		return true;
	}
	return false;
}

/**
 * amdgpu_sync_fence - remember to sync to this fence
 *
 * @sync: sync object to add fence to
 * @fence: fence to sync to
 *
 */
int amdgpu_sync_fence(struct amdgpu_device *adev, struct amdgpu_sync *sync,
		      struct dma_fence *f, bool explicit)
{
	struct amdgpu_sync_entry *e;

	if (!f)
		return 0;
	if (amdgpu_sync_same_dev(adev, f) &&
	    amdgpu_sync_get_owner(f) == AMDGPU_FENCE_OWNER_VM)
		amdgpu_sync_keep_later(&sync->last_vm_update, f);

	if (amdgpu_sync_add_later(sync, f, explicit))
		return 0;

	e = kmem_cache_alloc(amdgpu_sync_slab, GFP_KERNEL);
	if (!e)
		return -ENOMEM;

	e->explicit = explicit;

	hash_add(sync->fences, &e->node, f->context);
	e->fence = dma_fence_get(f);
	return 0;
}

/**
 * amdgpu_sync_resv - sync to a reservation object
 *
 * @sync: sync object to add fences from reservation object to
 * @resv: reservation object with embedded fence
 * @explicit_sync: true if we should only sync to the exclusive fence
 *
 * Sync to the fence
 */
int amdgpu_sync_resv(struct amdgpu_device *adev,
		     struct amdgpu_sync *sync,
		     struct reservation_object *resv,
		     void *owner, bool explicit_sync)
{
	struct reservation_object_list *flist;
	struct dma_fence *f;
	void *fence_owner;
	unsigned i;
	int r = 0;

	if (resv == NULL)
		return -EINVAL;

	/* always sync to the exclusive fence */
	f = reservation_object_get_excl(resv);
	r = amdgpu_sync_fence(adev, sync, f, false);

	flist = reservation_object_get_list(resv);
	if (!flist || r)
		return r;

	for (i = 0; i < flist->shared_count; ++i) {
		f = rcu_dereference_protected(flist->shared[i],
					      reservation_object_held(resv));
		/* We only want to trigger KFD eviction fences on
		 * evict or move jobs. Skip KFD fences otherwise.
		 */
		fence_owner = amdgpu_sync_get_owner(f);
		if (fence_owner == AMDGPU_FENCE_OWNER_KFD &&
		    owner != AMDGPU_FENCE_OWNER_UNDEFINED)
			continue;

		if (amdgpu_sync_same_dev(adev, f)) {
			/* VM updates are only interesting
			 * for other VM updates and moves.
			 */
			if ((owner != AMDGPU_FENCE_OWNER_UNDEFINED) &&
			    (fence_owner != AMDGPU_FENCE_OWNER_UNDEFINED) &&
			    ((owner == AMDGPU_FENCE_OWNER_VM) !=
			     (fence_owner == AMDGPU_FENCE_OWNER_VM)))
				continue;

			/* Ignore fence from the same owner and explicit one as
			 * long as it isn't undefined.
			 */
			if (owner != AMDGPU_FENCE_OWNER_UNDEFINED &&
			    (fence_owner == owner || explicit_sync))
				continue;
		}

		r = amdgpu_sync_fence(adev, sync, f, false);
		if (r)
			break;
	}
	return r;
}

/**
 * amdgpu_sync_peek_fence - get the next fence not signaled yet
 *
 * @sync: the sync object
 * @ring: optional ring to use for test
 *
 * Returns the next fence not signaled yet without removing it from the sync
 * object.
 */
struct dma_fence *amdgpu_sync_peek_fence(struct amdgpu_sync *sync,
					 struct amdgpu_ring *ring)
{
	struct amdgpu_sync_entry *e;
	struct hlist_node *tmp;
	int i;

	hash_for_each_safe(sync->fences, i, tmp, e, node) {
		struct dma_fence *f = e->fence;
		struct drm_sched_fence *s_fence = to_drm_sched_fence(f);

		if (dma_fence_is_signaled(f)) {
			hash_del(&e->node);
			dma_fence_put(f);
			kmem_cache_free(amdgpu_sync_slab, e);
			continue;
		}
		if (ring && s_fence) {
			/* For fences from the same ring it is sufficient
			 * when they are scheduled.
			 */
			if (s_fence->sched == &ring->sched) {
				if (dma_fence_is_signaled(&s_fence->scheduled))
					continue;

				return &s_fence->scheduled;
			}
		}

		return f;
	}

	return NULL;
}

/**
 * amdgpu_sync_get_fence - get the next fence from the sync object
 *
 * @sync: sync object to use
 * @explicit: true if the next fence is explicit
 *
 * Get and removes the next fence from the sync object not signaled yet.
 */
struct dma_fence *amdgpu_sync_get_fence(struct amdgpu_sync *sync, bool *explicit)
{
	struct amdgpu_sync_entry *e;
	struct hlist_node *tmp;
	struct dma_fence *f;
	int i;
	hash_for_each_safe(sync->fences, i, tmp, e, node) {

		f = e->fence;
		if (explicit)
			*explicit = e->explicit;

		hash_del(&e->node);
		kmem_cache_free(amdgpu_sync_slab, e);

		if (!dma_fence_is_signaled(f))
			return f;

		dma_fence_put(f);
	}
	return NULL;
}

/**
 * amdgpu_sync_clone - clone a sync object
 *
 * @source: sync object to clone
 * @clone: pointer to destination sync object
 *
 * Adds references to all unsignaled fences in @source to @clone. Also
 * removes signaled fences from @source while at it.
 */
int amdgpu_sync_clone(struct amdgpu_sync *source, struct amdgpu_sync *clone)
{
	struct amdgpu_sync_entry *e;
	struct hlist_node *tmp;
	struct dma_fence *f;
	int i, r;

	hash_for_each_safe(source->fences, i, tmp, e, node) {
		f = e->fence;
		if (!dma_fence_is_signaled(f)) {
			r = amdgpu_sync_fence(NULL, clone, f, e->explicit);
			if (r)
				return r;
		} else {
			hash_del(&e->node);
			dma_fence_put(f);
			kmem_cache_free(amdgpu_sync_slab, e);
		}
	}

	dma_fence_put(clone->last_vm_update);
	clone->last_vm_update = dma_fence_get(source->last_vm_update);

	return 0;
}

int amdgpu_sync_wait(struct amdgpu_sync *sync, bool intr)
{
	struct amdgpu_sync_entry *e;
	struct hlist_node *tmp;
	int i, r;

	hash_for_each_safe(sync->fences, i, tmp, e, node) {
		r = dma_fence_wait(e->fence, intr);
		if (r)
			return r;

		hash_del(&e->node);
		dma_fence_put(e->fence);
		kmem_cache_free(amdgpu_sync_slab, e);
	}

	return 0;
}

/**
 * amdgpu_sync_free - free the sync object
 *
 * @sync: sync object to use
 *
 * Free the sync object.
 */
void amdgpu_sync_free(struct amdgpu_sync *sync)
{
	struct amdgpu_sync_entry *e;
	struct hlist_node *tmp;
	unsigned i;

	hash_for_each_safe(sync->fences, i, tmp, e, node) {
		hash_del(&e->node);
		dma_fence_put(e->fence);
		kmem_cache_free(amdgpu_sync_slab, e);
	}

	dma_fence_put(sync->last_vm_update);
}

/**
 * amdgpu_sync_init - init sync object subsystem
 *
 * Allocate the slab allocator.
 */
int amdgpu_sync_init(void)
{
	amdgpu_sync_slab = kmem_cache_create(
		"amdgpu_sync", sizeof(struct amdgpu_sync_entry), 0,
		SLAB_HWCACHE_ALIGN, NULL);
	if (!amdgpu_sync_slab)
		return -ENOMEM;

	return 0;
}

/**
 * amdgpu_sync_fini - fini sync object subsystem
 *
 * Free the slab allocator.
 */
void amdgpu_sync_fini(void)
{
	kmem_cache_destroy(amdgpu_sync_slab);
}
