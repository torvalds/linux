/*
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * This file is released under the GPL.
 */
#ifndef _LINUX_DM_BITSET_H
#define _LINUX_DM_BITSET_H

#include "dm-array.h"

/*----------------------------------------------------------------*/

/*
 * This bitset type is a thin wrapper round a dm_array of 64bit words.  It
 * uses a tiny, one word cache to reduce the number of array lookups and so
 * increase performance.
 *
 * Like the dm-array that it's based on, the caller needs to keep track of
 * the size of the bitset separately.  The underlying dm-array implicitly
 * knows how many words it's storing and will return -ENODATA if you try
 * and access an out of bounds word.  However, an out of bounds bit in the
 * final word will _not_ be detected, you have been warned.
 *
 * Bits are indexed from zero.

 * Typical use:
 *
 * a) Initialise a dm_disk_bitset structure with dm_disk_bitset_init().
 *    This describes the bitset and includes the cache.  It's not called it
 *    dm_bitset_info in line with other data structures because it does
 *    include instance data.
 *
 * b) Get yourself a root.  The root is the index of a block of data on the
 *    disk that holds a particular instance of an bitset.  You may have a
 *    pre existing root in your metadata that you wish to use, or you may
 *    want to create a brand new, empty bitset with dm_bitset_empty().
 *
 * Like the other data structures in this library, dm_bitset objects are
 * immutable between transactions.  Update functions will return you the
 * root for a _new_ array.  If you've incremented the old root, via
 * dm_tm_inc(), before calling the update function you may continue to use
 * it in parallel with the new root.
 *
 * Even read operations may trigger the cache to be flushed and as such
 * return a root for a new, updated bitset.
 *
 * c) resize a bitset with dm_bitset_resize().
 *
 * d) Set a bit with dm_bitset_set_bit().
 *
 * e) Clear a bit with dm_bitset_clear_bit().
 *
 * f) Test a bit with dm_bitset_test_bit().
 *
 * g) Flush all updates from the cache with dm_bitset_flush().
 *
 * h) Destroy the bitset with dm_bitset_del().  This tells the transaction
 *    manager that you're no longer using this data structure so it can
 *    recycle it's blocks.  (dm_bitset_dec() would be a better name for it,
 *    but del is in keeping with dm_btree_del()).
 */

/*
 * Opaque object.  Unlike dm_array_info, you should have one of these per
 * bitset.  Initialise with dm_disk_bitset_init().
 */
struct dm_disk_bitset {
	struct dm_array_info array_info;

	uint32_t current_index;
	uint64_t current_bits;

	bool current_index_set:1;
	bool dirty:1;
};

/*
 * Sets up a dm_disk_bitset structure.  You don't need to do anything with
 * this structure when you finish using it.
 *
 * tm - the transaction manager that should supervise this structure
 * info - the structure being initialised
 */
void dm_disk_bitset_init(struct dm_transaction_manager *tm,
			 struct dm_disk_bitset *info);

/*
 * Create an empty, zero length bitset.
 *
 * info - describes the bitset
 * new_root - on success, points to the new root block
 */
int dm_bitset_empty(struct dm_disk_bitset *info, dm_block_t *new_root);

/*
 * Creates a new bitset populated with values provided by a callback
 * function.  This is more efficient than creating an empty bitset,
 * resizing, and then setting values since that process incurs a lot of
 * copying.
 *
 * info - describes the array
 * root - the root block of the array on disk
 * size - the number of entries in the array
 * fn - the callback
 * context - passed to the callback
 */
typedef int (*bit_value_fn)(uint32_t index, bool *value, void *context);
int dm_bitset_new(struct dm_disk_bitset *info, dm_block_t *root,
		  uint32_t size, bit_value_fn fn, void *context);

/*
 * Resize the bitset.
 *
 * info - describes the bitset
 * old_root - the root block of the array on disk
 * old_nr_entries - the number of bits in the old bitset
 * new_nr_entries - the number of bits you want in the new bitset
 * default_value - the value for any new bits
 * new_root - on success, points to the new root block
 */
int dm_bitset_resize(struct dm_disk_bitset *info, dm_block_t old_root,
		     uint32_t old_nr_entries, uint32_t new_nr_entries,
		     bool default_value, dm_block_t *new_root);

/*
 * Frees the bitset.
 */
int dm_bitset_del(struct dm_disk_bitset *info, dm_block_t root);

/*
 * Set a bit.
 *
 * info - describes the bitset
 * root - the root block of the bitset
 * index - the bit index
 * new_root - on success, points to the new root block
 *
 * -ENODATA will be returned if the index is out of bounds.
 */
int dm_bitset_set_bit(struct dm_disk_bitset *info, dm_block_t root,
		      uint32_t index, dm_block_t *new_root);

/*
 * Clears a bit.
 *
 * info - describes the bitset
 * root - the root block of the bitset
 * index - the bit index
 * new_root - on success, points to the new root block
 *
 * -ENODATA will be returned if the index is out of bounds.
 */
int dm_bitset_clear_bit(struct dm_disk_bitset *info, dm_block_t root,
			uint32_t index, dm_block_t *new_root);

/*
 * Tests a bit.
 *
 * info - describes the bitset
 * root - the root block of the bitset
 * index - the bit index
 * new_root - on success, points to the new root block (cached values may have been written)
 * result - the bit value you're after
 *
 * -ENODATA will be returned if the index is out of bounds.
 */
int dm_bitset_test_bit(struct dm_disk_bitset *info, dm_block_t root,
		       uint32_t index, dm_block_t *new_root, bool *result);

/*
 * Flush any cached changes to disk.
 *
 * info - describes the bitset
 * root - the root block of the bitset
 * new_root - on success, points to the new root block
 */
int dm_bitset_flush(struct dm_disk_bitset *info, dm_block_t root,
		    dm_block_t *new_root);

struct dm_bitset_cursor {
	struct dm_disk_bitset *info;
	struct dm_array_cursor cursor;

	uint32_t entries_remaining;
	uint32_t array_index;
	uint32_t bit_index;
	uint64_t current_bits;
};

/*
 * Make sure you've flush any dm_disk_bitset and updated the root before
 * using this.
 */
int dm_bitset_cursor_begin(struct dm_disk_bitset *info,
			   dm_block_t root, uint32_t nr_entries,
			   struct dm_bitset_cursor *c);
void dm_bitset_cursor_end(struct dm_bitset_cursor *c);

int dm_bitset_cursor_next(struct dm_bitset_cursor *c);
bool dm_bitset_cursor_get_value(struct dm_bitset_cursor *c);

/*----------------------------------------------------------------*/

#endif /* _LINUX_DM_BITSET_H */
