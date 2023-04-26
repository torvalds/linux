/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * This file is released under the GPL.
 */
#ifndef _LINUX_DM_ARRAY_H
#define _LINUX_DM_ARRAY_H

#include "dm-btree.h"

/*----------------------------------------------------------------*/

/*
 * The dm-array is a persistent version of an array.  It packs the data
 * more efficiently than a btree which will result in less disk space use,
 * and a performance boost.  The element get and set operations are still
 * O(ln(n)), but with a much smaller constant.
 *
 * The value type structure is reused from the btree type to support proper
 * reference counting of values.
 *
 * The arrays implicitly know their length, and bounds are checked for
 * lookups and updated.  It doesn't store this in an accessible place
 * because it would waste a whole metadata block.  Make sure you store the
 * size along with the array root in your encompassing data.
 *
 * Array entries are indexed via an unsigned integer starting from zero.
 * Arrays are not sparse; if you resize an array to have 'n' entries then
 * 'n - 1' will be the last valid index.
 *
 * Typical use:
 *
 * a) initialise a dm_array_info structure.  This describes the array
 *    values and ties it into a specific transaction manager.  It holds no
 *    instance data; the same info can be used for many similar arrays if
 *    you wish.
 *
 * b) Get yourself a root.  The root is the index of a block of data on the
 *    disk that holds a particular instance of an array.  You may have a
 *    pre existing root in your metadata that you wish to use, or you may
 *    want to create a brand new, empty array with dm_array_empty().
 *
 * Like the other data structures in this library, dm_array objects are
 * immutable between transactions.  Update functions will return you the
 * root for a _new_ array.  If you've incremented the old root, via
 * dm_tm_inc(), before calling the update function you may continue to use
 * it in parallel with the new root.
 *
 * c) resize an array with dm_array_resize().
 *
 * d) Get a value from the array with dm_array_get_value().
 *
 * e) Set a value in the array with dm_array_set_value().
 *
 * f) Walk an array of values in index order with dm_array_walk().  More
 *    efficient than making many calls to dm_array_get_value().
 *
 * g) Destroy the array with dm_array_del().  This tells the transaction
 *    manager that you're no longer using this data structure so it can
 *    recycle it's blocks.  (dm_array_dec() would be a better name for it,
 *    but del is in keeping with dm_btree_del()).
 */

/*
 * Describes an array.  Don't initialise this structure yourself, use the
 * init function below.
 */
struct dm_array_info {
	struct dm_transaction_manager *tm;
	struct dm_btree_value_type value_type;
	struct dm_btree_info btree_info;
};

/*
 * Sets up a dm_array_info structure.  You don't need to do anything with
 * this structure when you finish using it.
 *
 * info - the structure being filled in.
 * tm   - the transaction manager that should supervise this structure.
 * vt   - describes the leaf values.
 */
void dm_array_info_init(struct dm_array_info *info,
			struct dm_transaction_manager *tm,
			struct dm_btree_value_type *vt);

/*
 * Create an empty, zero length array.
 *
 * info - describes the array
 * root - on success this will be filled out with the root block
 */
int dm_array_empty(struct dm_array_info *info, dm_block_t *root);

/*
 * Resizes the array.
 *
 * info - describes the array
 * root - the root block of the array on disk
 * old_size - the caller is responsible for remembering the size of
 *            the array
 * new_size - can be bigger or smaller than old_size
 * value - if we're growing the array the new entries will have this value
 * new_root - on success, points to the new root block
 *
 * If growing the inc function for 'value' will be called the appropriate
 * number of times.  So if the caller is holding a reference they may want
 * to drop it.
 */
int dm_array_resize(struct dm_array_info *info, dm_block_t root,
		    uint32_t old_size, uint32_t new_size,
		    const void *value, dm_block_t *new_root)
	__dm_written_to_disk(value);

/*
 * Creates a new array populated with values provided by a callback
 * function.  This is more efficient than creating an empty array,
 * resizing, and then setting values since that process incurs a lot of
 * copying.
 *
 * Assumes 32bit values for now since it's only used by the cache hint
 * array.
 *
 * info - describes the array
 * root - the root block of the array on disk
 * size - the number of entries in the array
 * fn - the callback
 * context - passed to the callback
 */
typedef int (*value_fn)(uint32_t index, void *value_le, void *context);
int dm_array_new(struct dm_array_info *info, dm_block_t *root,
		 uint32_t size, value_fn fn, void *context);

/*
 * Frees a whole array.  The value_type's decrement operation will be called
 * for all values in the array
 */
int dm_array_del(struct dm_array_info *info, dm_block_t root);

/*
 * Lookup a value in the array
 *
 * info - describes the array
 * root - root block of the array
 * index - array index
 * value - the value to be read.  Will be in on-disk format of course.
 *
 * -ENODATA will be returned if the index is out of bounds.
 */
int dm_array_get_value(struct dm_array_info *info, dm_block_t root,
		       uint32_t index, void *value);

/*
 * Set an entry in the array.
 *
 * info - describes the array
 * root - root block of the array
 * index - array index
 * value - value to be written to disk.  Make sure you confirm the value is
 *         in on-disk format with__dm_bless_for_disk() before calling.
 * new_root - the new root block
 *
 * The old value being overwritten will be decremented, the new value
 * incremented.
 *
 * -ENODATA will be returned if the index is out of bounds.
 */
int dm_array_set_value(struct dm_array_info *info, dm_block_t root,
		       uint32_t index, const void *value, dm_block_t *new_root)
	__dm_written_to_disk(value);

/*
 * Walk through all the entries in an array.
 *
 * info - describes the array
 * root - root block of the array
 * fn - called back for every element
 * context - passed to the callback
 */
int dm_array_walk(struct dm_array_info *info, dm_block_t root,
		  int (*fn)(void *context, uint64_t key, void *leaf),
		  void *context);

/*----------------------------------------------------------------*/

/*
 * Cursor api.
 *
 * This lets you iterate through all the entries in an array efficiently
 * (it will preload metadata).
 *
 * I'm using a cursor, rather than a walk function with a callback because
 * the cache target needs to iterate both the mapping and hint arrays in
 * unison.
 */
struct dm_array_cursor {
	struct dm_array_info *info;
	struct dm_btree_cursor cursor;

	struct dm_block *block;
	struct array_block *ab;
	unsigned int index;
};

int dm_array_cursor_begin(struct dm_array_info *info,
			  dm_block_t root, struct dm_array_cursor *c);
void dm_array_cursor_end(struct dm_array_cursor *c);

uint32_t dm_array_cursor_index(struct dm_array_cursor *c);
int dm_array_cursor_next(struct dm_array_cursor *c);
int dm_array_cursor_skip(struct dm_array_cursor *c, uint32_t count);

/*
 * value_le is only valid while the cursor points at the current value.
 */
void dm_array_cursor_get_value(struct dm_array_cursor *c, void **value_le);

/*----------------------------------------------------------------*/

#endif	/* _LINUX_DM_ARRAY_H */
