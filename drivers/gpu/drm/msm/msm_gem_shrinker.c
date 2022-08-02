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
static bool enable_eviction = false;
MODULE_PARM_DESC(enable_eviction, "Enable swappable GEM buffers");
module_param(enable_eviction, bool, 0600);

static bool can_swap(void)
{
	return enable_eviction && get_nr_swap_pages() > 0;
}

static unsigned long
msm_gem_shrinker_count(struct shrinker *shrinker, struct shrink_control *sc)
{
	struct msm_drm_private *priv =
		container_of(shrinker, struct msm_drm_private, shrinker);
	unsigned count = priv->lru.dontneed.count;

	if (can_swap())
		count += priv->lru.willneed.count;

	return count;
}

static bool
purge(struct drm_gem_object *obj)
{
	if (!is_purgeable(to_msm_bo(obj)))
		return false;

	if (msm_gem_active(obj))
		return false;

	msm_gem_purge(obj);

	return true;
}

static bool
evict(struct drm_gem_object *obj)
{
	if (is_unevictable(to_msm_bo(obj)))
		return false;

	if (msm_gem_active(obj))
		return false;

	msm_gem_evict(obj);

	return true;
}

static unsigned long
msm_gem_shrinker_scan(struct shrinker *shrinker, struct shrink_control *sc)
{
	struct msm_drm_private *priv =
		container_of(shrinker, struct msm_drm_private, shrinker);
	long nr = sc->nr_to_scan;
	unsigned long freed, purged, evicted = 0;

	purged = drm_gem_lru_scan(&priv->lru.dontneed, nr, purge);
	nr -= purged;

	if (can_swap() && nr > 0) {
		evicted = drm_gem_lru_scan(&priv->lru.willneed, nr, evict);
		nr -= evicted;
	}

	freed = purged + evicted;

	if (freed)
		trace_msm_gem_shrink(sc->nr_to_scan, purged, evicted);

	return (freed > 0) ? freed : SHRINK_STOP;
}

#ifdef CONFIG_DEBUG_FS
unsigned long
msm_gem_shrinker_shrink(struct drm_device *dev, unsigned long nr_to_scan)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct shrink_control sc = {
		.nr_to_scan = nr_to_scan,
	};
	int ret;

	fs_reclaim_acquire(GFP_KERNEL);
	ret = msm_gem_shrinker_scan(&priv->shrinker, &sc);
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
vmap_shrink(struct drm_gem_object *obj)
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

	for (idx = 0; lrus[idx] && unmapped < vmap_shrink_limit; idx++) {
		unmapped += drm_gem_lru_scan(lrus[idx],
					     vmap_shrink_limit - unmapped,
					     vmap_shrink);
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
void msm_gem_shrinker_init(struct drm_device *dev)
{
	struct msm_drm_private *priv = dev->dev_private;
	priv->shrinker.count_objects = msm_gem_shrinker_count;
	priv->shrinker.scan_objects = msm_gem_shrinker_scan;
	priv->shrinker.seeks = DEFAULT_SEEKS;
	WARN_ON(register_shrinker(&priv->shrinker, "drm-msm_gem"));

	priv->vmap_notifier.notifier_call = msm_gem_shrinker_vmap;
	WARN_ON(register_vmap_purge_notifier(&priv->vmap_notifier));
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

	if (priv->shrinker.nr_deferred) {
		WARN_ON(unregister_vmap_purge_notifier(&priv->vmap_notifier));
		unregister_shrinker(&priv->shrinker);
	}
}
