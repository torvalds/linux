/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * This file is released under the GPL.
 */

#ifndef _LINUX_DM_TRANSACTION_MANAGER_H
#define _LINUX_DM_TRANSACTION_MANAGER_H

#include "dm-block-manager.h"

struct dm_transaction_manager;
struct dm_space_map;

/*----------------------------------------------------------------*/

/*
 * This manages the scope of a transaction.  It also enforces immutability
 * of the on-disk data structures by limiting access to writeable blocks.
 *
 * Clients should not fiddle with the block manager directly.
 */

void dm_tm_destroy(struct dm_transaction_manager *tm);

/*
 * The non-blocking version of a transaction manager is intended for use in
 * fast path code that needs to do lookups e.g. a dm mapping function.
 * You create the non-blocking variant from a normal tm.  The interface is
 * the same, except that most functions will just return -EWOULDBLOCK.
 * Methods that return void yet may block should not be called on a clone
 * viz. dm_tm_inc, dm_tm_dec.  Call dm_tm_destroy() as you would with a normal
 * tm when you've finished with it.  You may not destroy the original prior
 * to clones.
 */
struct dm_transaction_manager *dm_tm_create_non_blocking_clone(struct dm_transaction_manager *real);

/*
 * We use a 2-phase commit here.
 *
 * i) Make all changes for the transaction *except* for the superblock.
 * Then call dm_tm_pre_commit() to flush them to disk.
 *
 * ii) Lock your superblock.  Update.  Then call dm_tm_commit() which will
 * unlock the superblock and flush it.  No other blocks should be updated
 * during this period.  Care should be taken to never unlock a partially
 * updated superblock; perform any operations that could fail *before* you
 * take the superblock lock.
 */
int dm_tm_pre_commit(struct dm_transaction_manager *tm);
int dm_tm_commit(struct dm_transaction_manager *tm, struct dm_block *superblock);

/*
 * These methods are the only way to get hold of a writeable block.
 */

/*
 * dm_tm_new_block() is pretty self-explanatory.  Make sure you do actually
 * write to the whole of @data before you unlock, otherwise you could get
 * a data leak.  (The other option is for tm_new_block() to zero new blocks
 * before handing them out, which will be redundant in most, if not all,
 * cases).
 * Zeroes the new block and returns with write lock held.
 */
int dm_tm_new_block(struct dm_transaction_manager *tm,
		    struct dm_block_validator *v,
		    struct dm_block **result);

/*
 * dm_tm_shadow_block() allocates a new block and copies the data from @orig
 * to it.  It then decrements the reference count on original block.  Use
 * this to update the contents of a block in a data structure, don't
 * confuse this with a clone - you shouldn't access the orig block after
 * this operation.  Because the tm knows the scope of the transaction it
 * can optimise requests for a shadow of a shadow to a no-op.  Don't forget
 * to unlock when you've finished with the shadow.
 *
 * The @inc_children flag is used to tell the caller whether it needs to
 * adjust reference counts for children.  (Data in the block may refer to
 * other blocks.)
 *
 * Shadowing implicitly drops a reference on @orig so you must not have
 * it locked when you call this.
 */
int dm_tm_shadow_block(struct dm_transaction_manager *tm, dm_block_t orig,
		       struct dm_block_validator *v,
		       struct dm_block **result, int *inc_children);

/*
 * Read access.  You can lock any block you want.  If there's a write lock
 * on it outstanding then it'll block.
 */
int dm_tm_read_lock(struct dm_transaction_manager *tm, dm_block_t b,
		    struct dm_block_validator *v,
		    struct dm_block **result);

void dm_tm_unlock(struct dm_transaction_manager *tm, struct dm_block *b);

/*
 * Functions for altering the reference count of a block directly.
 */
void dm_tm_inc(struct dm_transaction_manager *tm, dm_block_t b);
void dm_tm_inc_range(struct dm_transaction_manager *tm, dm_block_t b, dm_block_t e);
void dm_tm_dec(struct dm_transaction_manager *tm, dm_block_t b);
void dm_tm_dec_range(struct dm_transaction_manager *tm, dm_block_t b, dm_block_t e);

/*
 * Builds up runs of adjacent blocks, and then calls the given fn
 * (typically dm_tm_inc/dec).  Very useful when you have to perform
 * the same tm operation on all values in a btree leaf.
 */
typedef void (*dm_tm_run_fn)(struct dm_transaction_manager *, dm_block_t, dm_block_t);
void dm_tm_with_runs(struct dm_transaction_manager *tm,
		     const __le64 *value_le, unsigned count, dm_tm_run_fn fn);

int dm_tm_ref(struct dm_transaction_manager *tm, dm_block_t b, uint32_t *result);

/*
 * Finds out if a given block is shared (ie. has a reference count higher
 * than one).
 */
int dm_tm_block_is_shared(struct dm_transaction_manager *tm, dm_block_t b,
			  int *result);

struct dm_block_manager *dm_tm_get_bm(struct dm_transaction_manager *tm);

/*
 * If you're using a non-blocking clone the tm will build up a list of
 * requested blocks that weren't in core.  This call will request those
 * blocks to be prefetched.
 */
void dm_tm_issue_prefetches(struct dm_transaction_manager *tm);

/*
 * A little utility that ties the knot by producing a transaction manager
 * that has a space map managed by the transaction manager...
 *
 * Returns a tm that has an open transaction to write the new disk sm.
 * Caller should store the new sm root and commit.
 *
 * The superblock location is passed so the metadata space map knows it
 * shouldn't be used.
 */
int dm_tm_create_with_sm(struct dm_block_manager *bm, dm_block_t sb_location,
			 struct dm_transaction_manager **tm,
			 struct dm_space_map **sm);

int dm_tm_open_with_sm(struct dm_block_manager *bm, dm_block_t sb_location,
		       void *sm_root, size_t root_len,
		       struct dm_transaction_manager **tm,
		       struct dm_space_map **sm);

#endif	/* _LINUX_DM_TRANSACTION_MANAGER_H */
