// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <linux/vmalloc.h>
#include <linux/sched/mm.h>

#include "msm_drv.h"
#include "msm_gem.h"
#include "msm_gpu.h"
#include "msm_gpu_trace.h"

/* Default disabled for now until it has some more testing on the different
 * iommu combinations that can be paired with the driver:
 */
static bool enable_eviction = true;
MODULE_PARM_DESC(enable_eviction, "Enable swappable GEM buffers");
module_param(enable_eviction, bool, 0600);

static bool can_swap(void)
{
	return enable_eviction && get_nr_swap_pages() > 0;
}

static bool can_block(struct shrink_control *sc)
{
	if (!(sc->gfp_mask & __GFP_DIRECT_RECLAIM))
		return false;
	return current_is_kswapd() || (sc->gfp_mask & __GFP_RECLAIM);
}

static unsigned long
msm_gem_shrinker_count(struct shrinker *shrinker, struct shrink_control *sc)
{
	struct msm_drm_private *priv = shrinker->private_data;
	unsigned count = priv->lru.dontneed.count;

	if (can_swap())
		count += priv->lru.willneed.count;

	return count;
}

static bool
with_vm_locks(struct ww_acquire_ctx *ticket,
	      void (*fn)(struct drm_gem_object *obj),
	      struct drm_gem_object *obj)
{
	/*
	 * Track last locked entry for for unwinding locks in error and
	 * success paths
	 */
	struct drm_gpuvm_bo *vm_bo, *last_locked = NULL;
	int ret = 0;

	drm_gem_for_each_gpuvm_bo (vm_bo, obj) {
		struct dma_resv *resv = drm_gpuvm_resv(vm_bo->vm);

		if (resv == obj->resv)
			continue;

		ret = dma_resv_lock(resv, ticket);

		/*
		 * Since we already skip the case when the VM and obj
		 * share a resv (ie. _NO_SHARE objs), we don't expect
		 * to hit a double-locking scenario... which the lock
		 * unwinding cannot really cope with.
		 */
		WARN_ON(ret == -EALREADY);

		/*
		 * Don't bother with slow-lock / backoff / retry sequence,
		 * if we can't get the lock just give up and move on to
		 * the next object.
		 */
		if (ret)
			goto out_unlock;

		/*
		 * Hold a ref to prevent the vm_bo from being freed
		 * and removed from the obj's gpuva list, as that would
		 * would result in missing the unlock below
		 */
		drm_gpuvm_bo_get(vm_bo);

		last_locked = vm_bo;
	}

	fn(obj);

out_unlock:
	if (last_locked) {
		drm_gem_for_each_gpuvm_bo (vm_bo, obj) {
			struct dma_resv *resv = drm_gpuvm_resv(vm_bo->vm);

			if (resv == obj->resv)
				continue;

			dma_resv_unlock(resv);

			/* Drop the ref taken while locking: */
			drm_gpuvm_bo_put(vm_bo);

			if (last_locked == vm_bo)
				break;
		}
	}

	return ret == 0;
}

static bool
purge(struct drm_gem_object *obj, struct ww_acquire_ctx *ticket)
{
	if (!is_purgeable(to_msm_bo(obj)))
		return false;

	if (msm_gem_active(obj))
		return false;

	return with_vm_locks(ticket, msm_gem_purge, obj);
}

static bool
evict(struct drm_gem_object *obj, struct ww_acquire_ctx *ticket)
{
	if (is_unevictable(to_msm_bo(obj)))
		return false;

	if (msm_gem_active(obj))
		return false;

	return with_vm_locks(ticket, msm_gem_evict, obj);
}

static bool
wait_for_idle(struct drm_gem_object *obj)
{
	enum dma_resv_usage usage = DMA_RESV_USAGE_BOOKKEEP;
	return dma_resv_wait_timeout(obj->resv, usage, false, 10) > 0;
}

static bool
active_purge(struct drm_gem_object *obj, struct ww_acquire_ctx *ticket)
{
	if (!wait_for_idle(obj))
		return false;

	return purge(obj, ticket);
}

static bool
active_evict(struct drm_gem_object *obj, struct ww_acquire_ctx *ticket)
{
	if (!wait_for_idle(obj))
		return false;

	return evict(obj, ticket);
}

static unsigned long
msm_gem_shrinker_scan(struct shrinker *shrinker, struct shrink_control *sc)
{
	struct msm_drm_private *priv = shrinker->private_data;
	struct ww_acquire_ctx ticket;
	struct {
		struct drm_gem_lru *lru;
		bool (*shrink)(struct drm_gem_object *obj, struct ww_acquire_ctx *ticket);
		bool cond;
		unsigned long freed;
		unsigned long remaining;
	} stages[] = {
		/* Stages of progressively more aggressive/expensive reclaim: */
		{ &priv->lru.dontneed, purge,        true },
		{ &priv->lru.willneed, evict,        can_swap() },
		{ &priv->lru.dontneed, active_purge, can_block(sc) },
		{ &priv->lru.willneed, active_evict, can_swap() && can_block(sc) },
	};
	long nr = sc->nr_to_scan;
	unsigned long freed = 0;
	unsigned long remaining = 0;

	for (unsigned i = 0; (nr > 0) && (i < ARRAY_SIZE(stages)); i++) {
		if (!stages[i].cond)
			continue;
		stages[i].freed =
			drm_gem_lru_scan(stages[i].lru, nr,
					 &stages[i].remaining,
					 stages[i].shrink,
					 &ticket);
		nr -= stages[i].freed;
		freed += stages[i].freed;
		remaining += stages[i].remaining;
	}

	if (freed) {
		trace_msm_gem_shrink(sc->nr_to_scan, stages[0].freed,
				     stages[1].freed, stages[2].freed,
				     stages[3].freed);
	}

	return (freed > 0 && remaining > 0) ? freed : SHRINK_STOP;
}

#ifdef CONFIG_DEBUG_FS
unsigned long
msm_gem_shrinker_shrink(struct drm_device *dev, unsigned long nr_to_scan)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct shrink_control sc = {
		.nr_to_scan = nr_to_scan,
	};
	unsigned long ret = SHRINK_STOP;

	fs_reclaim_acquire(GFP_KERNEL);
	if (priv->shrinker)
		ret = msm_gem_shrinker_scan(priv->shrinker, &sc);
	fs_reclaim_release(GFP_KERNEL);

	return ret;
}
#endif

/* since we don't know any better, lets bail after a few
 * and if necessary the shrinker will be invoked again.
 * Seems better than unmapping *everything*
 */
static const int vmap_shrink_limit = 15;

static bool
vmap_shrink(struct drm_gem_object *obj, struct ww_acquire_ctx *ticket)
{
	if (!is_vunmapable(to_msm_bo(obj)))
		return false;

	msm_gem_vunmap(obj);

	return true;
}

static int
msm_gem_shrinker_vmap(struct notifier_block *nb, unsigned long event, void *ptr)
{
	struct msm_drm_private *priv =
		container_of(nb, struct msm_drm_private, vmap_notifier);
	struct drm_gem_lru *lrus[] = {
		&priv->lru.dontneed,
		&priv->lru.willneed,
		&priv->lru.pinned,
		NULL,
	};
	unsigned idx, unmapped = 0;
	unsigned long remaining = 0;

	for (idx = 0; lrus[idx] && unmapped < vmap_shrink_limit; idx++) {
		unmapped += drm_gem_lru_scan(lrus[idx],
					     vmap_shrink_limit - unmapped,
					     &remaining,
					     vmap_shrink,
					     NULL);
	}

	*(unsigned long *)ptr += unmapped;

	if (unmapped > 0)
		trace_msm_gem_purge_vmaps(unmapped);

	return NOTIFY_DONE;
}

/**
 * msm_gem_shrinker_init - Initialize msm shrinker
 * @dev: drm device
 *
 * This function registers and sets up the msm shrinker.
 */
int msm_gem_shrinker_init(struct drm_device *dev)
{
	struct msm_drm_private *priv = dev->dev_private;

	priv->shrinker = shrinker_alloc(0, "drm-msm_gem");
	if (!priv->shrinker)
		return -ENOMEM;

	priv->shrinker->count_objects = msm_gem_shrinker_count;
	priv->shrinker->scan_objects = msm_gem_shrinker_scan;
	priv->shrinker->private_data = priv;

	shrinker_register(priv->shrinker);

	priv->vmap_notifier.notifier_call = msm_gem_shrinker_vmap;
	WARN_ON(register_vmap_purge_notifier(&priv->vmap_notifier));

	return 0;
}

/**
 * msm_gem_shrinker_cleanup - Clean up msm shrinker
 * @dev: drm device
 *
 * This function unregisters the msm shrinker.
 */
void msm_gem_shrinker_cleanup(struct drm_device *dev)
{
	struct msm_drm_private *priv = dev->dev_private;

	if (priv->shrinker) {
		WARN_ON(unregister_vmap_purge_notifier(&priv->vmap_notifier));
		shrinker_free(priv->shrinker);
	}
}
