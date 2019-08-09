// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include "msm_drv.h"
#include "msm_gem.h"

static bool msm_gem_shrinker_lock(struct drm_device *dev, bool *unlock)
{
	/* NOTE: we are *closer* to being able to get rid of
	 * mutex_trylock_recursive().. the msm_gem code itself does
	 * not need struct_mutex, although codepaths that can trigger
	 * shrinker are still called in code-paths that hold the
	 * struct_mutex.
	 *
	 * Also, msm_obj->madv is protected by struct_mutex.
	 *
	 * The next step is probably split out a seperate lock for
	 * protecting inactive_list, so that shrinker does not need
	 * struct_mutex.
	 */
	switch (mutex_trylock_recursive(&dev->struct_mutex)) {
	case MUTEX_TRYLOCK_FAILED:
		return false;

	case MUTEX_TRYLOCK_SUCCESS:
		*unlock = true;
		return true;

	case MUTEX_TRYLOCK_RECURSIVE:
		*unlock = false;
		return true;
	}

	BUG();
}

static unsigned long
msm_gem_shrinker_count(struct shrinker *shrinker, struct shrink_control *sc)
{
	struct msm_drm_private *priv =
		container_of(shrinker, struct msm_drm_private, shrinker);
	struct drm_device *dev = priv->dev;
	struct msm_gem_object *msm_obj;
	unsigned long count = 0;
	bool unlock;

	if (!msm_gem_shrinker_lock(dev, &unlock))
		return 0;

	list_for_each_entry(msm_obj, &priv->inactive_list, mm_list) {
		if (is_purgeable(msm_obj))
			count += msm_obj->base.size >> PAGE_SHIFT;
	}

	if (unlock)
		mutex_unlock(&dev->struct_mutex);

	return count;
}

static unsigned long
msm_gem_shrinker_scan(struct shrinker *shrinker, struct shrink_control *sc)
{
	struct msm_drm_private *priv =
		container_of(shrinker, struct msm_drm_private, shrinker);
	struct drm_device *dev = priv->dev;
	struct msm_gem_object *msm_obj;
	unsigned long freed = 0;
	bool unlock;

	if (!msm_gem_shrinker_lock(dev, &unlock))
		return SHRINK_STOP;

	list_for_each_entry(msm_obj, &priv->inactive_list, mm_list) {
		if (freed >= sc->nr_to_scan)
			break;
		if (is_purgeable(msm_obj)) {
			msm_gem_purge(&msm_obj->base, OBJ_LOCK_SHRINKER);
			freed += msm_obj->base.size >> PAGE_SHIFT;
		}
	}

	if (unlock)
		mutex_unlock(&dev->struct_mutex);

	if (freed > 0)
		pr_info_ratelimited("Purging %lu bytes\n", freed << PAGE_SHIFT);

	return freed;
}

static int
msm_gem_shrinker_vmap(struct notifier_block *nb, unsigned long event, void *ptr)
{
	struct msm_drm_private *priv =
		container_of(nb, struct msm_drm_private, vmap_notifier);
	struct drm_device *dev = priv->dev;
	struct msm_gem_object *msm_obj;
	unsigned unmapped = 0;
	bool unlock;

	if (!msm_gem_shrinker_lock(dev, &unlock))
		return NOTIFY_DONE;

	list_for_each_entry(msm_obj, &priv->inactive_list, mm_list) {
		if (is_vunmapable(msm_obj)) {
			msm_gem_vunmap(&msm_obj->base, OBJ_LOCK_SHRINKER);
			/* since we don't know any better, lets bail after a few
			 * and if necessary the shrinker will be invoked again.
			 * Seems better than unmapping *everything*
			 */
			if (++unmapped >= 15)
				break;
		}
	}

	if (unlock)
		mutex_unlock(&dev->struct_mutex);

	*(unsigned long *)ptr += unmapped;

	if (unmapped > 0)
		pr_info_ratelimited("Purging %u vmaps\n", unmapped);

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
