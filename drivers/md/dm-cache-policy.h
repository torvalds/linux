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

/* FIXME: make it clear which methods are optional.  Get debug policy to
 * double check this at start.
 */

/*
 * The cache policy makes the important decisions about which blocks get to
 * live on the faster cache device.
 *
 * When the core target has to remap a bio it calls the 'map' method of the
 * policy.  This returns an instruction telling the core target what to do.
 *
 * POLICY_HIT:
 *   That block is in the cache.  Remap to the cache and carry on.
 *
 * POLICY_MISS:
 *   This block is on the origin device.  Remap and carry on.
 *
 * POLICY_NEW:
 *   This block is currently on the origin device, but the policy wants to
 *   move it.  The core should:
 *
 *   - hold any further io to this origin block
 *   - copy the origin to the given cache block
 *   - release all the held blocks
 *   - remap the original block to the cache
 *
 * POLICY_REPLACE:
 *   This block is currently on the origin device.  The policy wants to
 *   move it to the cache, with the added complication that the destination
 *   cache block needs a writeback first.  The core should:
 *
 *   - hold any further io to this origin block
 *   - hold any further io to the origin block that's being written back
 *   - writeback
 *   - copy new block to cache
 *   - release held blocks
 *   - remap bio to cache and reissue.
 *
 * Should the core run into trouble while processing a POLICY_NEW or
 * POLICY_REPLACE instruction it will roll back the policies mapping using
 * remove_mapping() or force_mapping().  These methods must not fail.  This
 * approach avoids having transactional semantics in the policy (ie, the
 * core informing the policy when a migration is complete), and hence makes
 * it easier to write new policies.
 *
 * In general policy methods should never block, except in the case of the
 * map function when can_migrate is set.  So be careful to implement using
 * bounded, preallocated memory.
 */
enum policy_operation {
	POLICY_HIT,
	POLICY_MISS,
	POLICY_NEW,
	POLICY_REPLACE
};

/*
 * When issuing a POLICY_REPLACE the policy needs to make a callback to
 * lock the block being demoted.  This doesn't need to occur during a
 * writeback operation since the block remains in the cache.
 */
struct policy_locker;
typedef int (*policy_lock_fn)(struct policy_locker *l, dm_oblock_t oblock);

struct policy_locker {
	policy_lock_fn fn;
};

/*
 * This is the instruction passed back to the core target.
 */
struct policy_result {
	enum policy_operation op;
	dm_oblock_t old_oblock;	/* POLICY_REPLACE */
	dm_cblock_t cblock;	/* POLICY_HIT, POLICY_NEW, POLICY_REPLACE */
};

typedef int (*policy_walk_fn)(void *context, dm_cblock_t cblock,
			      dm_oblock_t oblock, uint32_t hint);

/*
 * The cache policy object.  Just a bunch of methods.  It is envisaged that
 * this structure will be embedded in a bigger, policy specific structure
 * (ie. use container_of()).
 */
struct dm_cache_policy {

	/*
	 * FIXME: make it clear which methods are optional, and which may
	 * block.
	 */

	/*
	 * Destroys this object.
	 */
	void (*destroy)(struct dm_cache_policy *p);

	/*
	 * See large comment above.
	 *
	 * oblock      - the origin block we're interested in.
	 *
	 * can_block - indicates whether the current thread is allowed to
	 *             block.  -EWOULDBLOCK returned if it can't and would.
	 *
	 * can_migrate - gives permission for POLICY_NEW or POLICY_REPLACE
	 *               instructions.  If denied and the policy would have
	 *               returned one of these instructions it should
	 *               return -EWOULDBLOCK.
	 *
	 * discarded_oblock - indicates whether the whole origin block is
	 *               in a discarded state (FIXME: better to tell the
	 *               policy about this sooner, so it can recycle that
	 *               cache block if it wants.)
	 * bio         - the bio that triggered this call.
	 * result      - gets filled in with the instruction.
	 *
	 * May only return 0, or -EWOULDBLOCK (if !can_migrate)
	 */
	int (*map)(struct dm_cache_policy *p, dm_oblock_t oblock,
		   bool can_block, bool can_migrate, bool discarded_oblock,
		   struct bio *bio, struct policy_locker *locker,
		   struct policy_result *result);

	/*
	 * Sometimes we want to see if a block is in the cache, without
	 * triggering any update of stats.  (ie. it's not a real hit).
	 *
	 * Must not block.
	 *
	 * Returns 0 if in cache, -ENOENT if not, < 0 for other errors
	 * (-EWOULDBLOCK would be typical).
	 */
	int (*lookup)(struct dm_cache_policy *p, dm_oblock_t oblock, dm_cblock_t *cblock);

	void (*set_dirty)(struct dm_cache_policy *p, dm_oblock_t oblock);
	void (*clear_dirty)(struct dm_cache_policy *p, dm_oblock_t oblock);

	/*
	 * Called when a cache target is first created.  Used to load a
	 * mapping from the metadata device into the policy.
	 */
	int (*load_mapping)(struct dm_cache_policy *p, dm_oblock_t oblock,
			    dm_cblock_t cblock, uint32_t hint, bool hint_valid);

	int (*walk_mappings)(struct dm_cache_policy *p, policy_walk_fn fn,
			     void *context);

	/*
	 * Override functions used on the error paths of the core target.
	 * They must succeed.
	 */
	void (*remove_mapping)(struct dm_cache_policy *p, dm_oblock_t oblock);
	void (*force_mapping)(struct dm_cache_policy *p, dm_oblock_t current_oblock,
			      dm_oblock_t new_oblock);

	/*
	 * This is called via the invalidate_cblocks message.  It is
	 * possible the particular cblock has already been removed due to a
	 * write io in passthrough mode.  In which case this should return
	 * -ENODATA.
	 */
	int (*remove_cblock)(struct dm_cache_policy *p, dm_cblock_t cblock);

	/*
	 * Provide a dirty block to be written back by the core target.  If
	 * critical_only is set then the policy should only provide work if
	 * it urgently needs it.
	 *
	 * Returns:
	 *
	 * 0 and @cblock,@oblock: block to write back provided
	 *
	 * -ENODATA: no dirty blocks available
	 */
	int (*writeback_work)(struct dm_cache_policy *p, dm_oblock_t *oblock, dm_cblock_t *cblock,
			      bool critical_only);

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
	 */
	void (*tick)(struct dm_cache_policy *p, bool can_block);

	/*
	 * Configuration.
	 */
	int (*emit_config_values)(struct dm_cache_policy *p, char *result,
				  unsigned maxlen, ssize_t *sz_ptr);
	int (*set_config_value)(struct dm_cache_policy *p,
				const char *key, const char *value);

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
	 * Policies may store a hint for each each cache block.
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
