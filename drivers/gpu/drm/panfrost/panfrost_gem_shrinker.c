/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2019 Arm Ltd.
 *
 * Based on msm_gem_freedreno.c:
 * Copyright (C) 2016 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <linux/list.h>

#include <drm/drm_device.h>
#include <drm/drm_gem_shmem_helper.h>

#include "panfrost_device.h"
#include "panfrost_gem.h"
#include "panfrost_mmu.h"

static unsigned long
panfrost_gem_shrinker_count(struct shrinker *shrinker, struct shrink_control *sc)
{
	struct panfrost_device *pfdev =
		container_of(shrinker, struct panfrost_device, shrinker);
	struct drm_gem_shmem_object *shmem;
	unsigned long count = 0;

	if (!mutex_trylock(&pfdev->shrinker_lock))
		return 0;

	list_for_each_entry(shmem, &pfdev->shrinker_list, madv_list) {
		if (drm_gem_shmem_is_purgeable(shmem))
			count += shmem->base.size >> PAGE_SHIFT;
	}

	mutex_unlock(&pfdev->shrinker_lock);

	return count;
}

static bool panfrost_gem_purge(struct drm_gem_object *obj)
{
	struct drm_gem_shmem_object *shmem = to_drm_gem_shmem_obj(obj);
	struct panfrost_gem_object *bo = to_panfrost_bo(obj);

	if (!mutex_trylock(&shmem->pages_lock))
		return false;

	panfrost_gem_teardown_mappings(bo);
	drm_gem_shmem_purge_locked(obj);

	mutex_unlock(&shmem->pages_lock);
	return true;
}

static unsigned long
panfrost_gem_shrinker_scan(struct shrinker *shrinker, struct shrink_control *sc)
{
	struct panfrost_device *pfdev =
		container_of(shrinker, struct panfrost_device, shrinker);
	struct drm_gem_shmem_object *shmem, *tmp;
	unsigned long freed = 0;

	if (!mutex_trylock(&pfdev->shrinker_lock))
		return SHRINK_STOP;

	list_for_each_entry_safe(shmem, tmp, &pfdev->shrinker_list, madv_list) {
		if (freed >= sc->nr_to_scan)
			break;
		if (drm_gem_shmem_is_purgeable(shmem) &&
		    panfrost_gem_purge(&shmem->base)) {
			freed += shmem->base.size >> PAGE_SHIFT;
			list_del_init(&shmem->madv_list);
		}
	}

	mutex_unlock(&pfdev->shrinker_lock);

	if (freed > 0)
		pr_info_ratelimited("Purging %lu bytes\n", freed << PAGE_SHIFT);

	return freed;
}

/**
 * panfrost_gem_shrinker_init - Initialize panfrost shrinker
 * @dev: DRM device
 *
 * This function registers and sets up the panfrost shrinker.
 */
void panfrost_gem_shrinker_init(struct drm_device *dev)
{
	struct panfrost_device *pfdev = dev->dev_private;
	pfdev->shrinker.count_objects = panfrost_gem_shrinker_count;
	pfdev->shrinker.scan_objects = panfrost_gem_shrinker_scan;
	pfdev->shrinker.seeks = DEFAULT_SEEKS;
	WARN_ON(register_shrinker(&pfdev->shrinker));
}

/**
 * panfrost_gem_shrinker_cleanup - Clean up panfrost shrinker
 * @dev: DRM device
 *
 * This function unregisters the panfrost shrinker.
 */
void panfrost_gem_shrinker_cleanup(struct drm_device *dev)
{
	struct panfrost_device *pfdev = dev->dev_private;

	if (pfdev->shrinker.nr_deferred) {
		unregister_shrinker(&pfdev->shrinker);
	}
}
