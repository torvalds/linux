/*
 * Copyright (C) 2012 Red Hat. All rights reserved.
 *
 * This file is released under the GPL.
 */

#ifndef DM_CACHE_POLICY_H
#define DM_CACHE_POLICY_H

#include "dm-cache-block-types.h"

#include <linux/device-mapper.h>

/*----------------------------------------------------------------*/

/*
 * The cache policy makes the important decisions about which blocks get to
 * live on the faster cache device.
 */
enum policy_operation {
	POLICY_PROMOTE,
	POLICY_DEMOTE,
	POLICY_WRITEBACK
};

/*
 * This is the instruction passed back to the core target.
 */
struct policy_work {
	enum policy_operation op;
	dm_oblock_t oblock;
	dm_cblock_t cblock;
};

/*
 * The cache policy object.  It is envisaged that this structure will be
 * embedded in a bigger, policy specific structure (ie. use container_of()).
 */
struct dm_cache_policy {
	/*
	 * Destroys this object.
	 */
	void (*destroy)(struct dm_cache_policy *p);

	/*
	 * Find the location of a block.
	 *
	 * Must not block.
	 *
	 * Returns 0 if in cache (cblock will be set), -ENOENT if not, < 0 for
	 * other errors (-EWOULDBLOCK would be typical).  data_dir should be
	 * READ or WRITE. fast_copy should be set if migrating this block would
	 * be 'cheap' somehow (eg, discarded data). background_queued will be set
	 * if a migration has just been queued.
	 */
	int (*lookup)(struct dm_cache_policy *p, dm_oblock_t oblock, dm_cblock_t *cblock,
		      int data_dir, bool fast_copy, bool *background_queued);

	/*
	 * Sometimes the core target can optimise a migration, eg, the
	 * block may be discarded, or the bio may cover an entire block.
	 * In order to optimise it needs the migration immediately though
	 * so it knows to do something different with the bio.
	 *
	 * This method is optional (policy-internal will fallback to using
	 * lookup).
	 */
	int (*lookup_with_work)(struct dm_cache_policy *p,
				dm_oblock_t oblock, dm_cblock_t *cblock,
				int data_dir, bool fast_copy,
				struct policy_work **work);

	/*
	 * Retrieves background work.  Returns -ENODATA when there's no
	 * background work.
	 */
	int (*get_background_work)(struct dm_cache_policy *p, bool idle,
			           struct policy_work **result);

	/*
	 * You must pass in the same work pointer that you were given, not
	 * a copy.
	 */
	void (*complete_background_work)(struct dm_cache_policy *p,
					 struct policy_work *work,
					 bool success);

	void (*set_dirty)(struct dm_cache_policy *p, dm_cblock_t cblock);
	void (*clear_dirty)(struct dm_cache_policy *p, dm_cblock_t cblock);

	/*
	 * Called when a cache target is first created.  Used to load a
	 * mapping from the metadata device into the policy.
	 */
	int (*load_mapping)(struct dm_cache_policy *p, dm_oblock_t oblock,
			    dm_cblock_t cblock, bool dirty,
			    uint32_t hint, bool hint_valid);

	/*
	 * Drops the mapping, irrespective of whether it's clean or dirty.
	 * Returns -ENODATA if cblock is not mapped.
	 */
	int (*invalidate_mapping)(struct dm_cache_policy *p, dm_cblock_t cblock);

	/*
	 * Gets the hint for a given cblock.  Called in a single threaded
	 * context.  So no locking required.
	 */
	uint32_t (*get_hint)(struct dm_cache_policy *p, dm_cblock_t cblock);

	/*
	 * How full is the cache?
	 */
	dm_cblock_t (*residency)(struct dm_cache_policy *p);

	/*
	 * Because of where we sit in the block layer, we can be asked to
	 * map a lot of little bios that are all in the same block (no
	 * queue merging has occurred).  To stop the policy being fooled by
	 * these, the core target sends regular tick() calls to the policy.
	 * The policy should only count an entry as hit once per tick.
	 *
	 * This method is optional.
	 */
	void (*tick)(struct dm_cache_policy *p, bool can_block);

	/*
	 * Configuration.
	 */
	int (*emit_config_values)(struct dm_cache_policy *p, char *result,
				  unsigned maxlen, ssize_t *sz_ptr);
	int (*set_config_value)(struct dm_cache_policy *p,
				const char *key, const char *value);

	void (*allow_migrations)(struct dm_cache_policy *p, bool allow);

	/*
	 * Book keeping ptr for the policy register, not for general use.
	 */
	void *private;
};

/*----------------------------------------------------------------*/

/*
 * We maintain a little register of the different policy types.
 */
#define CACHE_POLICY_NAME_SIZE 16
#define CACHE_POLICY_VERSION_SIZE 3

struct dm_cache_policy_type {
	/* For use by the register code only. */
	struct list_head list;

	/*
	 * Policy writers should fill in these fields.  The name field is
	 * what gets passed on the target line to select your policy.
	 */
	char name[CACHE_POLICY_NAME_SIZE];
	unsigned version[CACHE_POLICY_VERSION_SIZE];

	/*
	 * For use by an alias dm_cache_policy_type to point to the
	 * real dm_cache_policy_type.
	 */
	struct dm_cache_policy_type *real;

	/*
	 * Policies may store a hint for each cache block.
	 * Currently the size of this hint must be 0 or 4 bytes but we
	 * expect to relax this in future.
	 */
	size_t hint_size;

	struct module *owner;
	struct dm_cache_policy *(*create)(dm_cblock_t cache_size,
					  sector_t origin_size,
					  sector_t block_size);
};

int dm_cache_policy_register(struct dm_cache_policy_type *type);
void dm_cache_policy_unregister(struct dm_cache_policy_type *type);

/*----------------------------------------------------------------*/

#endif	/* DM_CACHE_POLICY_H */
