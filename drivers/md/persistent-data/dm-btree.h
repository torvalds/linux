/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * This file is released under the GPL.
 */
#ifndef _LINUX_DM_BTREE_H
#define _LINUX_DM_BTREE_H

#include "dm-block-manager.h"

struct dm_transaction_manager;

/*----------------------------------------------------------------*/

/*
 * Annotations used to check on-disk metadata is handled as little-endian.
 */
#ifdef __CHECKER__
#  define __dm_written_to_disk(x) __releases(x)
#  define __dm_reads_from_disk(x) __acquires(x)
#  define __dm_bless_for_disk(x) __acquire(x)
#  define __dm_unbless_for_disk(x) __release(x)
#else
#  define __dm_written_to_disk(x)
#  define __dm_reads_from_disk(x)
#  define __dm_bless_for_disk(x)
#  define __dm_unbless_for_disk(x)
#endif

/*----------------------------------------------------------------*/

/*
 * Manipulates hierarchical B+ trees with 64-bit keys and arbitrary-sized
 * values.
 */

/*
 * Information about the values stored within the btree.
 */
struct dm_btree_value_type {
	void *context;

	/*
	 * The size in bytes of each value.
	 */
	uint32_t size;

	/*
	 * Any of these methods can be safely set to NULL if you do not
	 * need the corresponding feature.
	 */

	/*
	 * The btree is making a duplicate of the value, for instance
	 * because previously-shared btree nodes have now diverged.
	 * @value argument is the new copy that the copy function may modify.
	 * (Probably it just wants to increment a reference count
	 * somewhere.) This method is _not_ called for insertion of a new
	 * value: It is assumed the ref count is already 1.
	 */
	void (*inc)(void *context, const void *value);

	/*
	 * This value is being deleted.  The btree takes care of freeing
	 * the memory pointed to by @value.  Often the del function just
	 * needs to decrement a reference count somewhere.
	 */
	void (*dec)(void *context, const void *value);

	/*
	 * A test for equality between two values.  When a value is
	 * overwritten with a new one, the old one has the dec method
	 * called _unless_ the new and old value are deemed equal.
	 */
	int (*equal)(void *context, const void *value1, const void *value2);
};

/*
 * The shape and contents of a btree.
 */
struct dm_btree_info {
	struct dm_transaction_manager *tm;

	/*
	 * Number of nested btrees. (Not the depth of a single tree.)
	 */
	unsigned levels;
	struct dm_btree_value_type value_type;
};

/*
 * Set up an empty tree.  O(1).
 */
int dm_btree_empty(struct dm_btree_info *info, dm_block_t *root);

/*
 * Delete a tree.  O(n) - this is the slow one!  It can also block, so
 * please don't call it on an IO path.
 */
int dm_btree_del(struct dm_btree_info *info, dm_block_t root);

/*
 * All the lookup functions return -ENODATA if the key cannot be found.
 */

/*
 * Tries to find a key that matches exactly.  O(ln(n))
 */
int dm_btree_lookup(struct dm_btree_info *info, dm_block_t root,
		    uint64_t *keys, void *value_le);

/*
 * Insertion (or overwrite an existing value).  O(ln(n))
 */
int dm_btree_insert(struct dm_btree_info *info, dm_block_t root,
		    uint64_t *keys, void *value, dm_block_t *new_root)
		    __dm_written_to_disk(value);

/*
 * A variant of insert that indicates whether it actually inserted or just
 * overwrote.  Useful if you're keeping track of the number of entries in a
 * tree.
 */
int dm_btree_insert_notify(struct dm_btree_info *info, dm_block_t root,
			   uint64_t *keys, void *value, dm_block_t *new_root,
			   int *inserted)
			   __dm_written_to_disk(value);

/*
 * Remove a key if present.  This doesn't remove empty sub trees.  Normally
 * subtrees represent a separate entity, like a snapshot map, so this is
 * correct behaviour.  O(ln(n)).
 */
int dm_btree_remove(struct dm_btree_info *info, dm_block_t root,
		    uint64_t *keys, dm_block_t *new_root);

/*
 * Returns < 0 on failure.  Otherwise the number of key entries that have
 * been filled out.  Remember trees can have zero entries, and as such have
 * no lowest key.
 */
int dm_btree_find_lowest_key(struct dm_btree_info *info, dm_block_t root,
			     uint64_t *result_keys);

/*
 * Returns < 0 on failure.  Otherwise the number of key entries that have
 * been filled out.  Remember trees can have zero entries, and as such have
 * no highest key.
 */
int dm_btree_find_highest_key(struct dm_btree_info *info, dm_block_t root,
			      uint64_t *result_keys);

/*
 * Iterate through the a btree, calling fn() on each entry.
 * It only works for single level trees and is internally recursive, so
 * monitor stack usage carefully.
 */
int dm_btree_walk(struct dm_btree_info *info, dm_block_t root,
		  int (*fn)(void *context, uint64_t *keys, void *leaf),
		  void *context);

#endif	/* _LINUX_DM_BTREE_H */
