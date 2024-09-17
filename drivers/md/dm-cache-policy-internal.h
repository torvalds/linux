/*
 * Copyright (C) 2012 Red Hat. All rights reserved.
 *
 * This file is released under the GPL.
 */

#ifndef DM_CACHE_POLICY_INTERNAL_H
#define DM_CACHE_POLICY_INTERNAL_H

#include <linux/vmalloc.h>
#include "dm-cache-policy.h"

/*----------------------------------------------------------------*/

static inline int policy_lookup(struct dm_cache_policy *p, dm_oblock_t oblock, dm_cblock_t *cblock,
				int data_dir, bool fast_copy, bool *background_queued)
{
	return p->lookup(p, oblock, cblock, data_dir, fast_copy, background_queued);
}

static inline int policy_lookup_with_work(struct dm_cache_policy *p,
					  dm_oblock_t oblock, dm_cblock_t *cblock,
					  int data_dir, bool fast_copy,
					  struct policy_work **work)
{
	if (!p->lookup_with_work) {
		*work = NULL;
		return p->lookup(p, oblock, cblock, data_dir, fast_copy, NULL);
	}

	return p->lookup_with_work(p, oblock, cblock, data_dir, fast_copy, work);
}

static inline int policy_get_background_work(struct dm_cache_policy *p,
					     bool idle, struct policy_work **result)
{
	return p->get_background_work(p, idle, result);
}

static inline void policy_complete_background_work(struct dm_cache_policy *p,
						   struct policy_work *work,
						   bool success)
{
	return p->complete_background_work(p, work, success);
}

static inline void policy_set_dirty(struct dm_cache_policy *p, dm_cblock_t cblock)
{
	p->set_dirty(p, cblock);
}

static inline void policy_clear_dirty(struct dm_cache_policy *p, dm_cblock_t cblock)
{
	p->clear_dirty(p, cblock);
}

static inline int policy_load_mapping(struct dm_cache_policy *p,
				      dm_oblock_t oblock, dm_cblock_t cblock,
				      bool dirty, uint32_t hint, bool hint_valid)
{
	return p->load_mapping(p, oblock, cblock, dirty, hint, hint_valid);
}

static inline int policy_invalidate_mapping(struct dm_cache_policy *p,
					    dm_cblock_t cblock)
{
	return p->invalidate_mapping(p, cblock);
}

static inline uint32_t policy_get_hint(struct dm_cache_policy *p,
				       dm_cblock_t cblock)
{
	return p->get_hint ? p->get_hint(p, cblock) : 0;
}

static inline dm_cblock_t policy_residency(struct dm_cache_policy *p)
{
	return p->residency(p);
}

static inline void policy_tick(struct dm_cache_policy *p, bool can_block)
{
	if (p->tick)
		return p->tick(p, can_block);
}

static inline int policy_emit_config_values(struct dm_cache_policy *p, char *result,
					    unsigned int maxlen, ssize_t *sz_ptr)
{
	ssize_t sz = *sz_ptr;
	if (p->emit_config_values)
		return p->emit_config_values(p, result, maxlen, sz_ptr);

	DMEMIT("0 ");
	*sz_ptr = sz;
	return 0;
}

static inline int policy_set_config_value(struct dm_cache_policy *p,
					  const char *key, const char *value)
{
	return p->set_config_value ? p->set_config_value(p, key, value) : -EINVAL;
}

static inline void policy_allow_migrations(struct dm_cache_policy *p, bool allow)
{
	return p->allow_migrations(p, allow);
}

/*----------------------------------------------------------------*/

/*
 * Some utility functions commonly used by policies and the core target.
 */
static inline size_t bitset_size_in_bytes(unsigned int nr_entries)
{
	return sizeof(unsigned long) * dm_div_up(nr_entries, BITS_PER_LONG);
}

static inline unsigned long *alloc_bitset(unsigned int nr_entries)
{
	size_t s = bitset_size_in_bytes(nr_entries);
	return vzalloc(s);
}

static inline void clear_bitset(void *bitset, unsigned int nr_entries)
{
	size_t s = bitset_size_in_bytes(nr_entries);
	memset(bitset, 0, s);
}

static inline void free_bitset(unsigned long *bits)
{
	vfree(bits);
}

/*----------------------------------------------------------------*/

/*
 * Creates a new cache policy given a policy name, a cache size, an origin size and the block size.
 */
struct dm_cache_policy *dm_cache_policy_create(const char *name, dm_cblock_t cache_size,
					       sector_t origin_size, sector_t block_size);

/*
 * Destroys the policy.  This drops references to the policy module as well
 * as calling it's destroy method.  So always use this rather than calling
 * the policy->destroy method directly.
 */
void dm_cache_policy_destroy(struct dm_cache_policy *p);

/*
 * In case we've forgotten.
 */
const char *dm_cache_policy_get_name(struct dm_cache_policy *p);

const unsigned int *dm_cache_policy_get_version(struct dm_cache_policy *p);

size_t dm_cache_policy_get_hint_size(struct dm_cache_policy *p);

/*----------------------------------------------------------------*/

#endif /* DM_CACHE_POLICY_INTERNAL_H */
