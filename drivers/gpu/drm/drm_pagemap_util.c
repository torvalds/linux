// SPDX-License-Identifier: GPL-2.0-only OR MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <linux/slab.h>

#include <drm/drm_drv.h>
#include <drm/drm_managed.h>
#include <drm/drm_pagemap.h>
#include <drm/drm_pagemap_util.h>
#include <drm/drm_print.h>

/**
 * struct drm_pagemap_cache - Lookup structure for pagemaps
 *
 * Structure to keep track of active (refcount > 1) and inactive
 * (refcount == 0) pagemaps. Inactive pagemaps can be made active
 * again by waiting for the @queued completion (indicating that the
 * pagemap has been put on the @shrinker's list of shrinkable
 * pagemaps, and then successfully removing it from @shrinker's
 * list. The latter may fail if the shrinker is already in the
 * process of freeing the pagemap. A struct drm_pagemap_cache can
 * hold a single struct drm_pagemap.
 */
struct drm_pagemap_cache {
	/** @lookup_mutex: Mutex making the lookup process atomic */
	struct mutex lookup_mutex;
	/** @lock: Lock protecting the @dpagemap pointer */
	spinlock_t lock;
	/** @shrinker: Pointer to the shrinker used for this cache. Immutable. */
	struct drm_pagemap_shrinker *shrinker;
	/** @dpagemap: Non-refcounted pointer to the drm_pagemap */
	struct drm_pagemap *dpagemap;
	/**
	 * @queued: Signals when an inactive drm_pagemap has been put on
	 * @shrinker's list.
	 */
	struct completion queued;
};

/**
 * struct drm_pagemap_shrinker - Shrinker to remove unused pagemaps
 */
struct drm_pagemap_shrinker {
	/** @drm: Pointer to the drm device. */
	struct drm_device *drm;
	/** @lock: Spinlock to protect the @dpagemaps list. */
	spinlock_t lock;
	/** @dpagemaps: List of unused dpagemaps. */
	struct list_head dpagemaps;
	/** @num_dpagemaps: Number of unused dpagemaps in @dpagemaps. */
	atomic_t num_dpagemaps;
	/** @shrink: Pointer to the struct shrinker. */
	struct shrinker *shrink;
};

static bool drm_pagemap_shrinker_cancel(struct drm_pagemap *dpagemap);

static void drm_pagemap_cache_fini(void *arg)
{
	struct drm_pagemap_cache *cache = arg;
	struct drm_pagemap *dpagemap;

	drm_dbg(cache->shrinker->drm, "Destroying dpagemap cache.\n");
	spin_lock(&cache->lock);
	dpagemap = cache->dpagemap;
	if (!dpagemap) {
		spin_unlock(&cache->lock);
		goto out;
	}

	if (drm_pagemap_shrinker_cancel(dpagemap)) {
		cache->dpagemap = NULL;
		spin_unlock(&cache->lock);
		drm_pagemap_destroy(dpagemap, false);
	}

out:
	mutex_destroy(&cache->lookup_mutex);
	kfree(cache);
}

/**
 * drm_pagemap_cache_create_devm() - Create a drm_pagemap_cache
 * @shrinker: Pointer to a struct drm_pagemap_shrinker.
 *
 * Create a device-managed drm_pagemap cache. The cache is automatically
 * destroyed on struct device removal, at which point any *inactive*
 * drm_pagemap's are destroyed.
 *
 * Return: Pointer to a struct drm_pagemap_cache on success. Error pointer
 * on failure.
 */
struct drm_pagemap_cache *drm_pagemap_cache_create_devm(struct drm_pagemap_shrinker *shrinker)
{
	struct drm_pagemap_cache *cache = kzalloc(sizeof(*cache), GFP_KERNEL);
	int err;

	if (!cache)
		return ERR_PTR(-ENOMEM);

	mutex_init(&cache->lookup_mutex);
	spin_lock_init(&cache->lock);
	cache->shrinker = shrinker;
	init_completion(&cache->queued);
	err = devm_add_action_or_reset(shrinker->drm->dev, drm_pagemap_cache_fini, cache);
	if (err)
		return ERR_PTR(err);

	return cache;
}
EXPORT_SYMBOL(drm_pagemap_cache_create_devm);

/**
 * DOC: Cache lookup
 *
 * Cache lookup should be done under a locked mutex, so that a
 * failed drm_pagemap_get_from_cache() and a following
 * drm_pagemap_cache_setpagemap() are carried out as an atomic
 * operation WRT other lookups. Otherwise, racing lookups may
 * unnecessarily concurrently create pagemaps to fulfill a
 * failed lookup. The API provides two functions to perform this lock,
 * drm_pagemap_lock_lookup() and drm_pagemap_unlock_lookup() and they
 * should be used in the following way:
 *
 * .. code-block:: c
 *
 *		drm_pagemap_lock_lookup(cache);
 *		dpagemap = drm_pagemap_get_from_cache(cache);
 *		if (dpagemap)
 *			goto out_unlock;
 *
 *		dpagemap = driver_create_new_dpagemap();
 *		if (!IS_ERR(dpagemap))
 *			drm_pagemap_cache_set_pagemap(cache, dpagemap);
 *
 *     out_unlock:
 *		drm_pagemap_unlock_lookup(cache);
 */

/**
 * drm_pagemap_cache_lock_lookup() - Lock a drm_pagemap_cache for lookup.
 * @cache: The drm_pagemap_cache to lock.
 *
 * Return: %-EINTR if interrupted while blocking. %0 otherwise.
 */
int drm_pagemap_cache_lock_lookup(struct drm_pagemap_cache *cache)
{
	return mutex_lock_interruptible(&cache->lookup_mutex);
}
EXPORT_SYMBOL(drm_pagemap_cache_lock_lookup);

/**
 * drm_pagemap_cache_unlock_lookup() - Unlock a drm_pagemap_cache after lookup.
 * @cache: The drm_pagemap_cache to unlock.
 */
void drm_pagemap_cache_unlock_lookup(struct drm_pagemap_cache *cache)
{
	mutex_unlock(&cache->lookup_mutex);
}
EXPORT_SYMBOL(drm_pagemap_cache_unlock_lookup);

/**
 * drm_pagemap_get_from_cache() - Lookup of drm_pagemaps.
 * @cache: The cache used for lookup.
 *
 * If an active pagemap is present in the cache, it is immediately returned.
 * If an inactive pagemap is present, it's removed from the shrinker list and
 * an attempt is made to make it active.
 * If no pagemap present or the attempt to make it active failed, %NULL is returned
 * to indicate to the caller to create a new drm_pagemap and insert it into
 * the cache.
 *
 * Return: A reference-counted pointer to a drm_pagemap if successful. An error
 * pointer if an error occurred, or %NULL if no drm_pagemap was found and
 * the caller should insert a new one.
 */
struct drm_pagemap *drm_pagemap_get_from_cache(struct drm_pagemap_cache *cache)
{
	struct drm_pagemap *dpagemap;
	int err;

	lockdep_assert_held(&cache->lookup_mutex);
retry:
	spin_lock(&cache->lock);
	dpagemap = cache->dpagemap;
	if (drm_pagemap_get_unless_zero(dpagemap)) {
		spin_unlock(&cache->lock);
		return dpagemap;
	}

	if (!dpagemap) {
		spin_unlock(&cache->lock);
		return NULL;
	}

	if (!try_wait_for_completion(&cache->queued)) {
		spin_unlock(&cache->lock);
		err = wait_for_completion_interruptible(&cache->queued);
		if (err)
			return ERR_PTR(err);
		goto retry;
	}

	if (drm_pagemap_shrinker_cancel(dpagemap)) {
		cache->dpagemap = NULL;
		spin_unlock(&cache->lock);
		err = drm_pagemap_reinit(dpagemap);
		if (err) {
			drm_pagemap_destroy(dpagemap, false);
			return ERR_PTR(err);
		}
		drm_pagemap_cache_set_pagemap(cache, dpagemap);
	} else {
		cache->dpagemap = NULL;
		spin_unlock(&cache->lock);
		dpagemap = NULL;
	}

	return dpagemap;
}
EXPORT_SYMBOL(drm_pagemap_get_from_cache);

/**
 * drm_pagemap_cache_set_pagemap() - Assign a drm_pagemap to a drm_pagemap_cache
 * @cache: The cache to assign the drm_pagemap to.
 * @dpagemap: The drm_pagemap to assign.
 *
 * The function must be called to populate a drm_pagemap_cache only
 * after a call to drm_pagemap_get_from_cache() returns NULL.
 */
void drm_pagemap_cache_set_pagemap(struct drm_pagemap_cache *cache, struct drm_pagemap *dpagemap)
{
	struct drm_device *drm = dpagemap->drm;

	lockdep_assert_held(&cache->lookup_mutex);
	spin_lock(&cache->lock);
	dpagemap->cache = cache;
	swap(cache->dpagemap, dpagemap);
	reinit_completion(&cache->queued);
	spin_unlock(&cache->lock);
	drm_WARN_ON(drm, !!dpagemap);
}
EXPORT_SYMBOL(drm_pagemap_cache_set_pagemap);

/**
 * drm_pagemap_get_from_cache_if_active() - Quick lookup of active drm_pagemaps
 * @cache: The cache to lookup from.
 *
 * Function that should be used to lookup a drm_pagemap that is already active.
 * (refcount > 0).
 *
 * Return: A pointer to the cache's drm_pagemap if it's active; %NULL otherwise.
 */
struct drm_pagemap *drm_pagemap_get_from_cache_if_active(struct drm_pagemap_cache *cache)
{
	struct drm_pagemap *dpagemap;

	spin_lock(&cache->lock);
	dpagemap = drm_pagemap_get_unless_zero(cache->dpagemap);
	spin_unlock(&cache->lock);

	return dpagemap;
}
EXPORT_SYMBOL(drm_pagemap_get_from_cache_if_active);

static bool drm_pagemap_shrinker_cancel(struct drm_pagemap *dpagemap)
{
	struct drm_pagemap_cache *cache = dpagemap->cache;
	struct drm_pagemap_shrinker *shrinker = cache->shrinker;

	spin_lock(&shrinker->lock);
	if (list_empty(&dpagemap->shrink_link)) {
		spin_unlock(&shrinker->lock);
		return false;
	}

	list_del_init(&dpagemap->shrink_link);
	atomic_dec(&shrinker->num_dpagemaps);
	spin_unlock(&shrinker->lock);
	return true;
}

#ifdef CONFIG_PROVE_LOCKING
/**
 * drm_pagemap_shrinker_might_lock() - lockdep test for drm_pagemap_shrinker_add()
 * @dpagemap: The drm pagemap.
 *
 * The drm_pagemap_shrinker_add() function performs some locking.
 * This function can be called in code-paths that might
 * call drm_pagemap_shrinker_add() to detect any lockdep problems early.
 */
void drm_pagemap_shrinker_might_lock(struct drm_pagemap *dpagemap)
{
	int idx;

	if (drm_dev_enter(dpagemap->drm, &idx)) {
		struct drm_pagemap_cache *cache = dpagemap->cache;

		if (cache)
			might_lock(&cache->shrinker->lock);

		drm_dev_exit(idx);
	}
}
#endif

/**
 * drm_pagemap_shrinker_add() - Add a drm_pagemap to the shrinker list or destroy
 * @dpagemap: The drm_pagemap.
 *
 * If @dpagemap is associated with a &struct drm_pagemap_cache AND the
 * struct device backing the drm device is still alive, add @dpagemap to
 * the &struct drm_pagemap_shrinker list of shrinkable drm_pagemaps.
 *
 * Otherwise destroy the pagemap directly using drm_pagemap_destroy().
 *
 * This is an internal function which is not intended to be exposed to drivers.
 */
void drm_pagemap_shrinker_add(struct drm_pagemap *dpagemap)
{
	struct drm_pagemap_cache *cache;
	struct drm_pagemap_shrinker *shrinker;
	int idx;

	/*
	 * The pagemap cache and shrinker are disabled at
	 * pci device remove time. After that, dpagemaps
	 * are freed directly.
	 */
	if (!drm_dev_enter(dpagemap->drm, &idx))
		goto out_no_cache;

	cache = dpagemap->cache;
	if (!cache) {
		drm_dev_exit(idx);
		goto out_no_cache;
	}

	shrinker = cache->shrinker;
	spin_lock(&shrinker->lock);
	list_add_tail(&dpagemap->shrink_link, &shrinker->dpagemaps);
	atomic_inc(&shrinker->num_dpagemaps);
	spin_unlock(&shrinker->lock);
	complete_all(&cache->queued);
	drm_dev_exit(idx);
	return;

out_no_cache:
	drm_pagemap_destroy(dpagemap, true);
}

static unsigned long
drm_pagemap_shrinker_count(struct shrinker *shrink, struct shrink_control *sc)
{
	struct drm_pagemap_shrinker *shrinker = shrink->private_data;
	unsigned long count = atomic_read(&shrinker->num_dpagemaps);

	return count ? : SHRINK_EMPTY;
}

static unsigned long
drm_pagemap_shrinker_scan(struct shrinker *shrink, struct shrink_control *sc)
{
	struct drm_pagemap_shrinker *shrinker = shrink->private_data;
	struct drm_pagemap *dpagemap;
	struct drm_pagemap_cache *cache;
	unsigned long nr_freed = 0;

	sc->nr_scanned = 0;
	spin_lock(&shrinker->lock);
	do {
		dpagemap = list_first_entry_or_null(&shrinker->dpagemaps, typeof(*dpagemap),
						    shrink_link);
		if (!dpagemap)
			break;

		atomic_dec(&shrinker->num_dpagemaps);
		list_del_init(&dpagemap->shrink_link);
		spin_unlock(&shrinker->lock);

		sc->nr_scanned++;
		nr_freed++;

		cache = dpagemap->cache;
		spin_lock(&cache->lock);
		cache->dpagemap = NULL;
		spin_unlock(&cache->lock);

		drm_dbg(dpagemap->drm, "Shrinking dpagemap %p.\n", dpagemap);
		drm_pagemap_destroy(dpagemap, true);
		spin_lock(&shrinker->lock);
	} while (sc->nr_scanned < sc->nr_to_scan);
	spin_unlock(&shrinker->lock);

	return sc->nr_scanned ? nr_freed : SHRINK_STOP;
}

static void drm_pagemap_shrinker_fini(void *arg)
{
	struct drm_pagemap_shrinker *shrinker = arg;

	drm_dbg(shrinker->drm, "Destroying dpagemap shrinker.\n");
	drm_WARN_ON(shrinker->drm, !!atomic_read(&shrinker->num_dpagemaps));
	shrinker_free(shrinker->shrink);
	kfree(shrinker);
}

/**
 * drm_pagemap_shrinker_create_devm() - Create and register a pagemap shrinker
 * @drm: The drm device
 *
 * Create and register a pagemap shrinker that shrinks unused pagemaps
 * and thereby reduces memory footprint.
 * The shrinker is drm_device managed and unregisters itself when
 * the drm device is removed.
 *
 * Return: %0 on success, negative error code on failure.
 */
struct drm_pagemap_shrinker *drm_pagemap_shrinker_create_devm(struct drm_device *drm)
{
	struct drm_pagemap_shrinker *shrinker;
	struct shrinker *shrink;
	int err;

	shrinker = kzalloc(sizeof(*shrinker), GFP_KERNEL);
	if (!shrinker)
		return ERR_PTR(-ENOMEM);

	shrink = shrinker_alloc(0, "drm-drm_pagemap:%s", drm->unique);
	if (!shrink) {
		kfree(shrinker);
		return ERR_PTR(-ENOMEM);
	}

	spin_lock_init(&shrinker->lock);
	INIT_LIST_HEAD(&shrinker->dpagemaps);
	shrinker->drm = drm;
	shrinker->shrink = shrink;
	shrink->count_objects = drm_pagemap_shrinker_count;
	shrink->scan_objects = drm_pagemap_shrinker_scan;
	shrink->private_data = shrinker;
	shrinker_register(shrink);

	err = devm_add_action_or_reset(drm->dev, drm_pagemap_shrinker_fini, shrinker);
	if (err)
		return ERR_PTR(err);

	return shrinker;
}
EXPORT_SYMBOL(drm_pagemap_shrinker_create_devm);

/**
 * struct drm_pagemap_owner - Device interconnect group
 * @kref: Reference count.
 *
 * A struct drm_pagemap_owner identifies a device interconnect group.
 */
struct drm_pagemap_owner {
	struct kref kref;
};

static void drm_pagemap_owner_release(struct kref *kref)
{
	kfree(container_of(kref, struct drm_pagemap_owner, kref));
}

/**
 * drm_pagemap_release_owner() - Stop participating in an interconnect group
 * @peer: Pointer to the struct drm_pagemap_peer used when joining the group
 *
 * Stop participating in an interconnect group. This function is typically
 * called when a pagemap is removed to indicate that it doesn't need to
 * be taken into account.
 */
void drm_pagemap_release_owner(struct drm_pagemap_peer *peer)
{
	struct drm_pagemap_owner_list *owner_list = peer->list;

	if (!owner_list)
		return;

	mutex_lock(&owner_list->lock);
	list_del(&peer->link);
	kref_put(&peer->owner->kref, drm_pagemap_owner_release);
	peer->owner = NULL;
	mutex_unlock(&owner_list->lock);
}
EXPORT_SYMBOL(drm_pagemap_release_owner);

/**
 * typedef interconnect_fn - Callback function to identify fast interconnects
 * @peer1: First endpoint.
 * @peer2: Second endpont.
 *
 * The function returns %true iff @peer1 and @peer2 have a fast interconnect.
 * Note that this is symmetrical. The function has no notion of client and provider,
 * which may not be sufficient in some cases. However, since the callback is intended
 * to guide in providing common pagemap owners, the notion of a common owner to
 * indicate fast interconnects would then have to change as well.
 *
 * Return: %true iff @peer1 and @peer2 have a fast interconnect. Otherwise @false.
 */
typedef bool (*interconnect_fn)(struct drm_pagemap_peer *peer1, struct drm_pagemap_peer *peer2);

/**
 * drm_pagemap_acquire_owner() - Join an interconnect group
 * @peer: A struct drm_pagemap_peer keeping track of the device interconnect
 * @owner_list: Pointer to the owner_list, keeping track of all interconnects
 * @has_interconnect: Callback function to determine whether two peers have a
 * fast local interconnect.
 *
 * Repeatedly calls @has_interconnect for @peer and other peers on @owner_list to
 * determine a set of peers for which @peer has a fast interconnect. That set will
 * have common &struct drm_pagemap_owner, and upon successful return, @peer::owner
 * will point to that struct, holding a reference, and @peer will be registered in
 * @owner_list. If @peer doesn't have any fast interconnects to other @peers, a
 * new unique &struct drm_pagemap_owner will be allocated for it, and that
 * may be shared with other peers that, at a later point, are determined to have
 * a fast interconnect with @peer.
 *
 * When @peer no longer participates in an interconnect group,
 * drm_pagemap_release_owner() should be called to drop the reference on the
 * struct drm_pagemap_owner.
 *
 * Return: %0 on success, negative error code on failure.
 */
int drm_pagemap_acquire_owner(struct drm_pagemap_peer *peer,
			      struct drm_pagemap_owner_list *owner_list,
			      interconnect_fn has_interconnect)
{
	struct drm_pagemap_peer *cur_peer;
	struct drm_pagemap_owner *owner = NULL;
	bool interconnect = false;

	mutex_lock(&owner_list->lock);
	might_alloc(GFP_KERNEL);
	list_for_each_entry(cur_peer, &owner_list->peers, link) {
		if (cur_peer->owner != owner) {
			if (owner && interconnect)
				break;
			owner = cur_peer->owner;
			interconnect = true;
		}
		if (interconnect && !has_interconnect(peer, cur_peer))
			interconnect = false;
	}

	if (!interconnect) {
		owner = kmalloc(sizeof(*owner), GFP_KERNEL);
		if (!owner) {
			mutex_unlock(&owner_list->lock);
			return -ENOMEM;
		}
		kref_init(&owner->kref);
		list_add_tail(&peer->link, &owner_list->peers);
	} else {
		kref_get(&owner->kref);
		list_add_tail(&peer->link, &cur_peer->link);
	}
	peer->owner = owner;
	peer->list = owner_list;
	mutex_unlock(&owner_list->lock);

	return 0;
}
EXPORT_SYMBOL(drm_pagemap_acquire_owner);
