// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * Copyright 2026 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#include <linux/slab.h>

#include "efa_ah_cache.h"

static const struct rhashtable_params ah_cache_params = {
	.key_len = sizeof(struct efa_ah_cache_key),
	.key_offset = offsetof(struct efa_ah_cache_entry, key),
	.head_offset = offsetof(struct efa_ah_cache_entry, linkage),
};

int efa_ah_cache_init(struct efa_ah_cache *ah_cache)
{
	int err;

	mutex_init(&ah_cache->lock);
	err = rhashtable_init(&ah_cache->hashtable, &ah_cache_params);
	if (err)
		mutex_destroy(&ah_cache->lock);

	return err;
}

static void efa_ah_cache_entry_free(void *ptr, void *arg)
{
	struct efa_ah_cache_entry *entry = ptr;

	WARN_ON(entry->usecnt);
	mutex_destroy(&entry->lock);
	kfree(entry);
}

void efa_ah_cache_destroy(struct efa_ah_cache *ah_cache)
{
	rhashtable_free_and_destroy(&ah_cache->hashtable, efa_ah_cache_entry_free, NULL);
	mutex_destroy(&ah_cache->lock);
}

static struct efa_ah_cache_entry *efa_ah_cache_lookup_locked(struct efa_ah_cache *ah_cache, u16 pd,
							     u8 *gid)
	__must_hold(&ah_cache->lock)
{
	struct efa_ah_cache_key key = {};

	memcpy(key.gid, gid, sizeof(key.gid));
	key.pd = pd;

	return rhashtable_lookup_fast(&ah_cache->hashtable, &key, ah_cache_params);
}

struct efa_ah_cache_entry *efa_ah_cache_lookup(struct efa_ah_cache *ah_cache, u16 pd, u8 *gid)
{
	struct efa_ah_cache_entry *entry;

	mutex_lock(&ah_cache->lock);
	entry = efa_ah_cache_lookup_locked(ah_cache, pd, gid);
	mutex_unlock(&ah_cache->lock);

	return entry;
}

/**
 * efa_ah_cache_get - Get or create an AH cache entry
 * @ah_cache: AH cache
 * @pd: Protection domain number
 * @gid: GID address
 *
 * Look up an AH cache entry by PD and GID. If found, take a reference and
 * return it. If not found, allocate a new entry and insert it. The caller must lock
 * the entry mutex and check usecnt to determine whether a device create
 * command is needed.
 *
 * Return: Pointer to the entry on success, ERR_PTR on failure.
 */
struct efa_ah_cache_entry *efa_ah_cache_get(struct efa_ah_cache *ah_cache, u16 pd, u8 *gid)
{
	struct efa_ah_cache_entry *entry;
	int err;

	mutex_lock(&ah_cache->lock);

	entry = efa_ah_cache_lookup_locked(ah_cache, pd, gid);
	if (entry) {
		refcount_inc(&entry->refcount);
		mutex_unlock(&ah_cache->lock);
		return entry;
	}

	entry = kzalloc_obj(*entry);
	if (!entry) {
		mutex_unlock(&ah_cache->lock);
		return ERR_PTR(-ENOMEM);
	}

	memcpy(entry->key.gid, gid, sizeof(entry->key.gid));
	entry->key.pd = pd;
	refcount_set(&entry->refcount, 1);
	mutex_init(&entry->lock);

	err = rhashtable_insert_fast(&ah_cache->hashtable, &entry->linkage, ah_cache_params);
	if (err) {
		mutex_destroy(&entry->lock);
		kfree(entry);
		mutex_unlock(&ah_cache->lock);
		return ERR_PTR(err);
	}

	mutex_unlock(&ah_cache->lock);
	return entry;
}

/**
 * efa_ah_cache_put - Put a refcount of an AH cache entry
 * @ah_cache: AH cache
 * @entry: AH cache entry
 *
 * Drop the refcount. If it reaches zero, remove the entry from the hashtable
 * and free it.
 */
void efa_ah_cache_put(struct efa_ah_cache *ah_cache, struct efa_ah_cache_entry *entry)
{
	if (!refcount_dec_and_mutex_lock(&entry->refcount, &ah_cache->lock))
		return;

	/* AH cache lock is held here */
	rhashtable_remove_fast(&ah_cache->hashtable, &entry->linkage, ah_cache_params);
	mutex_unlock(&ah_cache->lock);

	mutex_destroy(&entry->lock);
	kfree(entry);
}
