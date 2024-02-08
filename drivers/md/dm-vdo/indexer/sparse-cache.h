/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef UDS_SPARSE_CACHE_H
#define UDS_SPARSE_CACHE_H

#include "geometry.h"
#include "indexer.h"

/*
 * The sparse cache is a cache of entire chapter indexes from sparse chapters used for searching
 * for names after all other search paths have failed. It contains only complete chapter indexes;
 * record pages from sparse chapters and single index pages used for resolving hooks are kept in
 * the regular page cache in the volume.
 *
 * The most important property of this cache is the absence of synchronization for read operations.
 * Safe concurrent access to the cache by the zone threads is controlled by the triage queue and
 * the barrier requests it issues to the zone queues. The set of cached chapters does not and must
 * not change between the carefully coordinated calls to uds_update_sparse_cache() from the zone
 * threads. Outside of updates, every zone will get the same result when calling
 * uds_sparse_cache_contains() as every other zone.
 */

struct index_zone;
struct sparse_cache;

int __must_check uds_make_sparse_cache(const struct index_geometry *geometry,
				       unsigned int capacity, unsigned int zone_count,
				       struct sparse_cache **cache_ptr);

void uds_free_sparse_cache(struct sparse_cache *cache);

bool uds_sparse_cache_contains(struct sparse_cache *cache, u64 virtual_chapter,
			       unsigned int zone_number);

int __must_check uds_update_sparse_cache(struct index_zone *zone, u64 virtual_chapter);

void uds_invalidate_sparse_cache(struct sparse_cache *cache);

int __must_check uds_search_sparse_cache(struct index_zone *zone,
					 const struct uds_record_name *name,
					 u64 *virtual_chapter_ptr, u16 *record_page_ptr);

#endif /* UDS_SPARSE_CACHE_H */
