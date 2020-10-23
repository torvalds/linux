// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include "msm_drv.h"
#include "msm_gem.h"
#include "msm_gpu_trace.h"

static unsigned long
msm_gem_shrinker_count(struct shrinker *shrinker, struct shrink_control *sc)
{
	struct msm_drm_private *priv =
		container_of(shrinker, struct msm_drm_private, shrinker);
	struct msm_gem_object *msm_obj;
	unsigned long count = 0;

	mutex_lock(&priv->mm_lock);

	list_for_each_entry(msm_obj, &priv->inactive_list, mm_list) {
		if (!msm_gem_trylock(&msm_obj->base))
			continue;
		if (is_purgeable(msm_obj))
			count += msm_obj->base.size >> PAGE_SHIFT;
		msm_gem_unlock(&msm_obj->base);
	}

	mutex_unlock(&priv->mm_lock);

	return count;
}

static unsigned long
msm_gem_shrinker_scan(struct shrinker *shrinker, struct shrink_control *sc)
{
	struct msm_drm_private *priv =
		container_of(shrinker, struct msm_drm_private, shrinker);
	struct msm_gem_object *msm_obj;
	unsigned long freed = 0;

	mutex_lock(&priv->mm_lock);

	list_for_each_entry(msm_obj, &priv->inactive_list, mm_list) {
		if (freed >= sc->nr_to_scan)
			break;
		if (!msm_gem_trylock(&msm_obj->base))
			continue;
		if (is_purgeable(msm_obj)) {
			msm_gem_purge(&msm_obj->base);
			freed += msm_obj->base.size >> PAGE_SHIFT;
		}
		msm_gem_unlock(&msm_obj->base);
	}

	mutex_unlock(&priv->mm_lock);

	if (freed > 0)
		trace_msm_gem_purge(freed << PAGE_SHIFT);

	return freed;
}

static int
msm_gem_shrinker_vmap(struct notifier_block *nb, unsigned long event, void *ptr)
{
	struct msm_drm_private *priv =
		container_of(nb, struct msm_drm_private, vmap_notifier);
	struct msm_gem_object *msm_obj;
	unsigned unmapped = 0;

	mutex_lock(&priv->mm_lock);

	list_for_each_entry(msm_obj, &priv->inactive_list, mm_list) {
		if (!msm_gem_trylock(&msm_obj->base))
			continue;
		if (is_vunmapable(msm_obj)) {
			msm_gem_vunmap(&msm_obj->base);
			unmapped++;
		}
		msm_gem_unlock(&msm_obj->base);

		/* since we don't know any better, lets bail after a few
		 * and if necessary the shrinker will be invoked again.
		 * Seems better than unmapping *everything*
		 */
		if (++unmapped >= 15)
			break;
	}

	mutex_unlock(&priv->mm_lock);

	*(unsigned long *)ptr += unmapped;

	if (unmapped > 0)
		trace_msm_gem_purge_vmaps(unmapped);

	return NOTIFY_DONE;
}

/**
 * msm_gem_shrinker_init - Initialize msm shrinker
 * @dev_priv: msm device
 *
 * This function registers and sets up the msm shrinker.
 */
void msm_gem_shrinker_init(struct drm_device *dev)
{
	struct msm_drm_private *priv = dev->dev_private;
	priv->shrinker.count_objects = msm_gem_shrinker_count;
	priv->shrinker.scan_objects = msm_gem_shrinker_scan;
	priv->shrinker.seeks = DEFAULT_SEEKS;
	WARN_ON(register_shrinker(&priv->shrinker));

	priv->vmap_notifier.notifier_call = msm_gem_shrinker_vmap;
	WARN_ON(register_vmap_purge_notifier(&priv->vmap_notifier));
}

/**
 * msm_gem_shrinker_cleanup - Clean up msm shrinker
 * @dev_priv: msm device
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
