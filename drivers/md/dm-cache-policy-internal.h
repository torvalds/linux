/*
 * Copyright (C) 2012 Red Hat. All rights reserved.
 *
 * This file is released under the GPL.
 */

#ifndef DM_CACHE_POLICY_INTERNAL_H
#define DM_CACHE_POLICY_INTERNAL_H

#include "dm-cache-policy.h"

/*----------------------------------------------------------------*/

/*
 * Little inline functions that simplify calling the policy methods.
 */
static inline int policy_map(struct dm_cache_policy *p, dm_oblock_t oblock,
			     bool can_block, bool can_migrate, bool discarded_oblock,
			     struct bio *bio, struct policy_locker *locker,
			     struct policy_result *result)
{
	return p->map(p, oblock, can_block, can_migrate, discarded_oblock, bio, locker, result);
}

static inline int policy_lookup(struct dm_cache_policy *p, dm_oblock_t oblock, dm_cblock_t *cblock)
{
	BUG_ON(!p->lookup);
	return p->lookup(p, oblock, cblock);
}

static inline void policy_set_dirty(struct dm_cache_policy *p, dm_oblock_t oblock)
{
	if (p->set_dirty)
		p->set_dirty(p, oblock);
}

static inline void policy_clear_dirty(struct dm_cache_policy *p, dm_oblock_t oblock)
{
	if (p->clear_dirty)
		p->clear_dirty(p, oblock);
}

static inline int policy_load_mapping(struct dm_cache_policy *p,
				      dm_oblock_t oblock, dm_cblock_t cblock,
				      uint32_t hint, bool hint_valid)
{
	return p->load_mapping(p, oblock, cblock, hint, hint_valid);
}

static inline int policy_walk_mappings(struct dm_cache_policy *p,
				      policy_walk_fn fn, void *context)
{
	return p->walk_mappings ? p->walk_mappings(p, fn, context) : 0;
}

static inline int policy_writeback_work(struct dm_cache_policy *p,
					dm_oblock_t *oblock,
					dm_cblock_t *cblock)
{
	return p->writeback_work ? p->writeback_work(p, oblock, cblock) : -ENOENT;
}

static inline void policy_remove_mapping(struct dm_cache_policy *p, dm_oblock_t oblock)
{
	p->remove_mapping(p, oblock);
}

static inline int policy_remove_cblock(struct dm_cache_policy *p, dm_cblock_t cblock)
{
	return p->remove_cblock(p, cblock);
}

static inline void policy_force_mapping(struct dm_cache_policy *p,
					dm_oblock_t current_oblock, dm_oblock_t new_oblock)
{
	return p->force_mapping(p, current_oblock, new_oblock);
}

static inline dm_cblock_t policy_residency(struct dm_cache_policy *p)
{
	return p->residency(p);
}

static inline void policy_tick(struct dm_cache_policy *p)
{
	if (p->tick)
		return p->tick(p);
}

static inline int policy_emit_config_values(struct dm_cache_policy *p, char *result, unsigned maxlen)
{
	ssize_t sz = 0;
	if (p->emit_config_values)
		return p->emit_config_values(p, result, maxlen);

	DMEMIT("0");
	return 0;
}

static inline int policy_set_config_value(struct dm_cache_policy *p,
					  const char *key, const char *value)
{
	return p->set_config_value ? p->set_config_value(p, key, value) : -EINVAL;
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

const unsigned *dm_cache_policy_get_version(struct dm_cache_policy *p);

size_t dm_cache_policy_get_hint_size(struct dm_cache_policy *p);

/*----------------------------------------------------------------*/

#endif /* DM_CACHE_POLICY_INTERNAL_H */
