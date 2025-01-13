// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * This file is released under the GPL.
 */

#include "dm-array.h"
#include "dm-space-map.h"
#include "dm-transaction-manager.h"

#include <linux/export.h>
#include <linux/device-mapper.h>

#define DM_MSG_PREFIX "array"

/*----------------------------------------------------------------*/

/*
 * The array is implemented as a fully populated btree, which points to
 * blocks that contain the packed values.  This is more space efficient
 * than just using a btree since we don't store 1 key per value.
 */
struct array_block {
	__le32 csum;
	__le32 max_entries;
	__le32 nr_entries;
	__le32 value_size;
	__le64 blocknr; /* Block this node is supposed to live in. */
} __packed;

/*----------------------------------------------------------------*/

/*
 * Validator methods.  As usual we calculate a checksum, and also write the
 * block location into the header (paranoia about ssds remapping areas by
 * mistake).
 */
#define CSUM_XOR 595846735

static void array_block_prepare_for_write(const struct dm_block_validator *v,
					  struct dm_block *b,
					  size_t size_of_block)
{
	struct array_block *bh_le = dm_block_data(b);

	bh_le->blocknr = cpu_to_le64(dm_block_location(b));
	bh_le->csum = cpu_to_le32(dm_bm_checksum(&bh_le->max_entries,
						 size_of_block - sizeof(__le32),
						 CSUM_XOR));
}

static int array_block_check(const struct dm_block_validator *v,
			     struct dm_block *b,
			     size_t size_of_block)
{
	struct array_block *bh_le = dm_block_data(b);
	__le32 csum_disk;

	if (dm_block_location(b) != le64_to_cpu(bh_le->blocknr)) {
		DMERR_LIMIT("%s failed: blocknr %llu != wanted %llu", __func__,
			    (unsigned long long) le64_to_cpu(bh_le->blocknr),
			    (unsigned long long) dm_block_location(b));
		return -ENOTBLK;
	}

	csum_disk = cpu_to_le32(dm_bm_checksum(&bh_le->max_entries,
					       size_of_block - sizeof(__le32),
					       CSUM_XOR));
	if (csum_disk != bh_le->csum) {
		DMERR_LIMIT("%s failed: csum %u != wanted %u", __func__,
			    (unsigned int) le32_to_cpu(csum_disk),
			    (unsigned int) le32_to_cpu(bh_le->csum));
		return -EILSEQ;
	}

	return 0;
}

static const struct dm_block_validator array_validator = {
	.name = "array",
	.prepare_for_write = array_block_prepare_for_write,
	.check = array_block_check
};

/*----------------------------------------------------------------*/

/*
 * Functions for manipulating the array blocks.
 */

/*
 * Returns a pointer to a value within an array block.
 *
 * index - The index into _this_ specific block.
 */
static void *element_at(struct dm_array_info *info, struct array_block *ab,
			unsigned int index)
{
	unsigned char *entry = (unsigned char *) (ab + 1);

	entry += index * info->value_type.size;

	return entry;
}

/*
 * Utility function that calls one of the value_type methods on every value
 * in an array block.
 */
static void on_entries(struct dm_array_info *info, struct array_block *ab,
		       void (*fn)(void *, const void *, unsigned int))
{
	unsigned int nr_entries = le32_to_cpu(ab->nr_entries);

	fn(info->value_type.context, element_at(info, ab, 0), nr_entries);
}

/*
 * Increment every value in an array block.
 */
static void inc_ablock_entries(struct dm_array_info *info, struct array_block *ab)
{
	struct dm_btree_value_type *vt = &info->value_type;

	if (vt->inc)
		on_entries(info, ab, vt->inc);
}

/*
 * Decrement every value in an array block.
 */
static void dec_ablock_entries(struct dm_array_info *info, struct array_block *ab)
{
	struct dm_btree_value_type *vt = &info->value_type;

	if (vt->dec)
		on_entries(info, ab, vt->dec);
}

/*
 * Each array block can hold this many values.
 */
static uint32_t calc_max_entries(size_t value_size, size_t size_of_block)
{
	return (size_of_block - sizeof(struct array_block)) / value_size;
}

/*
 * Allocate a new array block.  The caller will need to unlock block.
 */
static int alloc_ablock(struct dm_array_info *info, size_t size_of_block,
			uint32_t max_entries,
			struct dm_block **block, struct array_block **ab)
{
	int r;

	r = dm_tm_new_block(info->btree_info.tm, &array_validator, block);
	if (r)
		return r;

	(*ab) = dm_block_data(*block);
	(*ab)->max_entries = cpu_to_le32(max_entries);
	(*ab)->nr_entries = cpu_to_le32(0);
	(*ab)->value_size = cpu_to_le32(info->value_type.size);

	return 0;
}

/*
 * Pad an array block out with a particular value.  Every instance will
 * cause an increment of the value_type.  new_nr must always be more than
 * the current number of entries.
 */
static void fill_ablock(struct dm_array_info *info, struct array_block *ab,
			const void *value, unsigned int new_nr)
{
	uint32_t nr_entries, delta, i;
	struct dm_btree_value_type *vt = &info->value_type;

	BUG_ON(new_nr > le32_to_cpu(ab->max_entries));
	BUG_ON(new_nr < le32_to_cpu(ab->nr_entries));

	nr_entries = le32_to_cpu(ab->nr_entries);
	delta = new_nr - nr_entries;
	if (vt->inc)
		vt->inc(vt->context, value, delta);
	for (i = nr_entries; i < new_nr; i++)
		memcpy(element_at(info, ab, i), value, vt->size);
	ab->nr_entries = cpu_to_le32(new_nr);
}

/*
 * Remove some entries from the back of an array block.  Every value
 * removed will be decremented.  new_nr must be <= the current number of
 * entries.
 */
static void trim_ablock(struct dm_array_info *info, struct array_block *ab,
			unsigned int new_nr)
{
	uint32_t nr_entries, delta;
	struct dm_btree_value_type *vt = &info->value_type;

	BUG_ON(new_nr > le32_to_cpu(ab->max_entries));
	BUG_ON(new_nr > le32_to_cpu(ab->nr_entries));

	nr_entries = le32_to_cpu(ab->nr_entries);
	delta = nr_entries - new_nr;
	if (vt->dec)
		vt->dec(vt->context, element_at(info, ab, new_nr - 1), delta);
	ab->nr_entries = cpu_to_le32(new_nr);
}

/*
 * Read locks a block, and coerces it to an array block.  The caller must
 * unlock 'block' when finished.
 */
static int get_ablock(struct dm_array_info *info, dm_block_t b,
		      struct dm_block **block, struct array_block **ab)
{
	int r;

	r = dm_tm_read_lock(info->btree_info.tm, b, &array_validator, block);
	if (r)
		return r;

	*ab = dm_block_data(*block);
	return 0;
}

/*
 * Unlocks an array block.
 */
static void unlock_ablock(struct dm_array_info *info, struct dm_block *block)
{
	dm_tm_unlock(info->btree_info.tm, block);
}

/*----------------------------------------------------------------*/

/*
 * Btree manipulation.
 */

/*
 * Looks up an array block in the btree, and then read locks it.
 *
 * index is the index of the index of the array_block, (ie. the array index
 * / max_entries).
 */
static int lookup_ablock(struct dm_array_info *info, dm_block_t root,
			 unsigned int index, struct dm_block **block,
			 struct array_block **ab)
{
	int r;
	uint64_t key = index;
	__le64 block_le;

	r = dm_btree_lookup(&info->btree_info, root, &key, &block_le);
	if (r)
		return r;

	return get_ablock(info, le64_to_cpu(block_le), block, ab);
}

/*
 * Insert an array block into the btree.  The block is _not_ unlocked.
 */
static int insert_ablock(struct dm_array_info *info, uint64_t index,
			 struct dm_block *block, dm_block_t *root)
{
	__le64 block_le = cpu_to_le64(dm_block_location(block));

	__dm_bless_for_disk(block_le);
	return dm_btree_insert(&info->btree_info, *root, &index, &block_le, root);
}

/*----------------------------------------------------------------*/

static int __shadow_ablock(struct dm_array_info *info, dm_block_t b,
			   struct dm_block **block, struct array_block **ab)
{
	int inc;
	int r = dm_tm_shadow_block(info->btree_info.tm, b,
				   &array_validator, block, &inc);
	if (r)
		return r;

	*ab = dm_block_data(*block);
	if (inc)
		inc_ablock_entries(info, *ab);

	return 0;
}

/*
 * The shadow op will often be a noop.  Only insert if it really
 * copied data.
 */
static int __reinsert_ablock(struct dm_array_info *info, unsigned int index,
			     struct dm_block *block, dm_block_t b,
			     dm_block_t *root)
{
	int r = 0;

	if (dm_block_location(block) != b) {
		/*
		 * dm_tm_shadow_block will have already decremented the old
		 * block, but it is still referenced by the btree.  We
		 * increment to stop the insert decrementing it below zero
		 * when overwriting the old value.
		 */
		dm_tm_inc(info->btree_info.tm, b);
		r = insert_ablock(info, index, block, root);
	}

	return r;
}

/*
 * Looks up an array block in the btree.  Then shadows it, and updates the
 * btree to point to this new shadow.  'root' is an input/output parameter
 * for both the current root block, and the new one.
 */
static int shadow_ablock(struct dm_array_info *info, dm_block_t *root,
			 unsigned int index, struct dm_block **block,
			 struct array_block **ab)
{
	int r;
	uint64_t key = index;
	dm_block_t b;
	__le64 block_le;

	r = dm_btree_lookup(&info->btree_info, *root, &key, &block_le);
	if (r)
		return r;
	b = le64_to_cpu(block_le);

	r = __shadow_ablock(info, b, block, ab);
	if (r)
		return r;

	return __reinsert_ablock(info, index, *block, b, root);
}

/*
 * Allocate an new array block, and fill it with some values.
 */
static int insert_new_ablock(struct dm_array_info *info, size_t size_of_block,
			     uint32_t max_entries,
			     unsigned int block_index, uint32_t nr,
			     const void *value, dm_block_t *root)
{
	int r;
	struct dm_block *block;
	struct array_block *ab;

	r = alloc_ablock(info, size_of_block, max_entries, &block, &ab);
	if (r)
		return r;

	fill_ablock(info, ab, value, nr);
	r = insert_ablock(info, block_index, block, root);
	unlock_ablock(info, block);

	return r;
}

static int insert_full_ablocks(struct dm_array_info *info, size_t size_of_block,
			       unsigned int begin_block, unsigned int end_block,
			       unsigned int max_entries, const void *value,
			       dm_block_t *root)
{
	int r = 0;

	for (; !r && begin_block != end_block; begin_block++)
		r = insert_new_ablock(info, size_of_block, max_entries, begin_block, max_entries, value, root);

	return r;
}

/*
 * There are a bunch of functions involved with resizing an array.  This
 * structure holds information that commonly needed by them.  Purely here
 * to reduce parameter count.
 */
struct resize {
	/*
	 * Describes the array.
	 */
	struct dm_array_info *info;

	/*
	 * The current root of the array.  This gets updated.
	 */
	dm_block_t root;

	/*
	 * Metadata block size.  Used to calculate the nr entries in an
	 * array block.
	 */
	size_t size_of_block;

	/*
	 * Maximum nr entries in an array block.
	 */
	unsigned int max_entries;

	/*
	 * nr of completely full blocks in the array.
	 *
	 * 'old' refers to before the resize, 'new' after.
	 */
	unsigned int old_nr_full_blocks, new_nr_full_blocks;

	/*
	 * Number of entries in the final block.  0 iff only full blocks in
	 * the array.
	 */
	unsigned int old_nr_entries_in_last_block, new_nr_entries_in_last_block;

	/*
	 * The default value used when growing the array.
	 */
	const void *value;
};

/*
 * Removes a consecutive set of array blocks from the btree.  The values
 * in block are decremented as a side effect of the btree remove.
 *
 * begin_index - the index of the first array block to remove.
 * end_index - the one-past-the-end value.  ie. this block is not removed.
 */
static int drop_blocks(struct resize *resize, unsigned int begin_index,
		       unsigned int end_index)
{
	int r;

	while (begin_index != end_index) {
		uint64_t key = begin_index++;

		r = dm_btree_remove(&resize->info->btree_info, resize->root,
				    &key, &resize->root);
		if (r)
			return r;
	}

	return 0;
}

/*
 * Calculates how many blocks are needed for the array.
 */
static unsigned int total_nr_blocks_needed(unsigned int nr_full_blocks,
				       unsigned int nr_entries_in_last_block)
{
	return nr_full_blocks + (nr_entries_in_last_block ? 1 : 0);
}

/*
 * Shrink an array.
 */
static int shrink(struct resize *resize)
{
	int r;
	unsigned int begin, end;
	struct dm_block *block;
	struct array_block *ab;

	/*
	 * Lose some blocks from the back?
	 */
	if (resize->new_nr_full_blocks < resize->old_nr_full_blocks) {
		begin = total_nr_blocks_needed(resize->new_nr_full_blocks,
					       resize->new_nr_entries_in_last_block);
		end = total_nr_blocks_needed(resize->old_nr_full_blocks,
					     resize->old_nr_entries_in_last_block);

		r = drop_blocks(resize, begin, end);
		if (r)
			return r;
	}

	/*
	 * Trim the new tail block
	 */
	if (resize->new_nr_entries_in_last_block) {
		r = shadow_ablock(resize->info, &resize->root,
				  resize->new_nr_full_blocks, &block, &ab);
		if (r)
			return r;

		trim_ablock(resize->info, ab, resize->new_nr_entries_in_last_block);
		unlock_ablock(resize->info, block);
	}

	return 0;
}

/*
 * Grow an array.
 */
static int grow_extend_tail_block(struct resize *resize, uint32_t new_nr_entries)
{
	int r;
	struct dm_block *block;
	struct array_block *ab;

	r = shadow_ablock(resize->info, &resize->root,
			  resize->old_nr_full_blocks, &block, &ab);
	if (r)
		return r;

	fill_ablock(resize->info, ab, resize->value, new_nr_entries);
	unlock_ablock(resize->info, block);

	return r;
}

static int grow_add_tail_block(struct resize *resize)
{
	return insert_new_ablock(resize->info, resize->size_of_block,
				 resize->max_entries,
				 resize->new_nr_full_blocks,
				 resize->new_nr_entries_in_last_block,
				 resize->value, &resize->root);
}

static int grow_needs_more_blocks(struct resize *resize)
{
	int r;
	unsigned int old_nr_blocks = resize->old_nr_full_blocks;

	if (resize->old_nr_entries_in_last_block > 0) {
		old_nr_blocks++;

		r = grow_extend_tail_block(resize, resize->max_entries);
		if (r)
			return r;
	}

	r = insert_full_ablocks(resize->info, resize->size_of_block,
				old_nr_blocks,
				resize->new_nr_full_blocks,
				resize->max_entries, resize->value,
				&resize->root);
	if (r)
		return r;

	if (resize->new_nr_entries_in_last_block)
		r = grow_add_tail_block(resize);

	return r;
}

static int grow(struct resize *resize)
{
	if (resize->new_nr_full_blocks > resize->old_nr_full_blocks)
		return grow_needs_more_blocks(resize);

	else if (resize->old_nr_entries_in_last_block)
		return grow_extend_tail_block(resize, resize->new_nr_entries_in_last_block);

	else
		return grow_add_tail_block(resize);
}

/*----------------------------------------------------------------*/

/*
 * These are the value_type functions for the btree elements, which point
 * to array blocks.
 */
static void block_inc(void *context, const void *value, unsigned int count)
{
	const __le64 *block_le = value;
	struct dm_array_info *info = context;
	unsigned int i;

	for (i = 0; i < count; i++, block_le++)
		dm_tm_inc(info->btree_info.tm, le64_to_cpu(*block_le));
}

static void __block_dec(void *context, const void *value)
{
	int r;
	uint64_t b;
	__le64 block_le;
	uint32_t ref_count;
	struct dm_block *block;
	struct array_block *ab;
	struct dm_array_info *info = context;

	memcpy(&block_le, value, sizeof(block_le));
	b = le64_to_cpu(block_le);

	r = dm_tm_ref(info->btree_info.tm, b, &ref_count);
	if (r) {
		DMERR_LIMIT("couldn't get reference count for block %llu",
			    (unsigned long long) b);
		return;
	}

	if (ref_count == 1) {
		/*
		 * We're about to drop the last reference to this ablock.
		 * So we need to decrement the ref count of the contents.
		 */
		r = get_ablock(info, b, &block, &ab);
		if (r) {
			DMERR_LIMIT("couldn't get array block %llu",
				    (unsigned long long) b);
			return;
		}

		dec_ablock_entries(info, ab);
		unlock_ablock(info, block);
	}

	dm_tm_dec(info->btree_info.tm, b);
}

static void block_dec(void *context, const void *value, unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count; i++, value += sizeof(__le64))
		__block_dec(context, value);
}

static int block_equal(void *context, const void *value1, const void *value2)
{
	return !memcmp(value1, value2, sizeof(__le64));
}

/*----------------------------------------------------------------*/

void dm_array_info_init(struct dm_array_info *info,
			struct dm_transaction_manager *tm,
			struct dm_btree_value_type *vt)
{
	struct dm_btree_value_type *bvt = &info->btree_info.value_type;

	memcpy(&info->value_type, vt, sizeof(info->value_type));
	info->btree_info.tm = tm;
	info->btree_info.levels = 1;

	bvt->context = info;
	bvt->size = sizeof(__le64);
	bvt->inc = block_inc;
	bvt->dec = block_dec;
	bvt->equal = block_equal;
}
EXPORT_SYMBOL_GPL(dm_array_info_init);

int dm_array_empty(struct dm_array_info *info, dm_block_t *root)
{
	return dm_btree_empty(&info->btree_info, root);
}
EXPORT_SYMBOL_GPL(dm_array_empty);

static int array_resize(struct dm_array_info *info, dm_block_t root,
			uint32_t old_size, uint32_t new_size,
			const void *value, dm_block_t *new_root)
{
	int r;
	struct resize resize;

	if (old_size == new_size) {
		*new_root = root;
		return 0;
	}

	resize.info = info;
	resize.root = root;
	resize.size_of_block = dm_bm_block_size(dm_tm_get_bm(info->btree_info.tm));
	resize.max_entries = calc_max_entries(info->value_type.size,
					      resize.size_of_block);

	resize.old_nr_full_blocks = old_size / resize.max_entries;
	resize.old_nr_entries_in_last_block = old_size % resize.max_entries;
	resize.new_nr_full_blocks = new_size / resize.max_entries;
	resize.new_nr_entries_in_last_block = new_size % resize.max_entries;
	resize.value = value;

	r = ((new_size > old_size) ? grow : shrink)(&resize);
	if (r)
		return r;

	*new_root = resize.root;
	return 0;
}

int dm_array_resize(struct dm_array_info *info, dm_block_t root,
		    uint32_t old_size, uint32_t new_size,
		    const void *value, dm_block_t *new_root)
	__dm_written_to_disk(value)
{
	int r = array_resize(info, root, old_size, new_size, value, new_root);

	__dm_unbless_for_disk(value);
	return r;
}
EXPORT_SYMBOL_GPL(dm_array_resize);

static int populate_ablock_with_values(struct dm_array_info *info, struct array_block *ab,
				       value_fn fn, void *context,
				       unsigned int base, unsigned int new_nr)
{
	int r;
	unsigned int i;
	struct dm_btree_value_type *vt = &info->value_type;

	BUG_ON(le32_to_cpu(ab->nr_entries));
	BUG_ON(new_nr > le32_to_cpu(ab->max_entries));

	for (i = 0; i < new_nr; i++) {
		r = fn(base + i, element_at(info, ab, i), context);
		if (r)
			return r;

		if (vt->inc)
			vt->inc(vt->context, element_at(info, ab, i), 1);
	}

	ab->nr_entries = cpu_to_le32(new_nr);
	return 0;
}

int dm_array_new(struct dm_array_info *info, dm_block_t *root,
		 uint32_t size, value_fn fn, void *context)
{
	int r;
	struct dm_block *block;
	struct array_block *ab;
	unsigned int block_index, end_block, size_of_block, max_entries;

	r = dm_array_empty(info, root);
	if (r)
		return r;

	size_of_block = dm_bm_block_size(dm_tm_get_bm(info->btree_info.tm));
	max_entries = calc_max_entries(info->value_type.size, size_of_block);
	end_block = dm_div_up(size, max_entries);

	for (block_index = 0; block_index != end_block; block_index++) {
		r = alloc_ablock(info, size_of_block, max_entries, &block, &ab);
		if (r)
			break;

		r = populate_ablock_with_values(info, ab, fn, context,
						block_index * max_entries,
						min(max_entries, size));
		if (r) {
			unlock_ablock(info, block);
			break;
		}

		r = insert_ablock(info, block_index, block, root);
		unlock_ablock(info, block);
		if (r)
			break;

		size -= max_entries;
	}

	return r;
}
EXPORT_SYMBOL_GPL(dm_array_new);

int dm_array_del(struct dm_array_info *info, dm_block_t root)
{
	return dm_btree_del(&info->btree_info, root);
}
EXPORT_SYMBOL_GPL(dm_array_del);

int dm_array_get_value(struct dm_array_info *info, dm_block_t root,
		       uint32_t index, void *value_le)
{
	int r;
	struct dm_block *block;
	struct array_block *ab;
	size_t size_of_block;
	unsigned int entry, max_entries;

	size_of_block = dm_bm_block_size(dm_tm_get_bm(info->btree_info.tm));
	max_entries = calc_max_entries(info->value_type.size, size_of_block);

	r = lookup_ablock(info, root, index / max_entries, &block, &ab);
	if (r)
		return r;

	entry = index % max_entries;
	if (entry >= le32_to_cpu(ab->nr_entries))
		r = -ENODATA;
	else
		memcpy(value_le, element_at(info, ab, entry),
		       info->value_type.size);

	unlock_ablock(info, block);
	return r;
}
EXPORT_SYMBOL_GPL(dm_array_get_value);

static int array_set_value(struct dm_array_info *info, dm_block_t root,
			   uint32_t index, const void *value, dm_block_t *new_root)
{
	int r;
	struct dm_block *block;
	struct array_block *ab;
	size_t size_of_block;
	unsigned int max_entries;
	unsigned int entry;
	void *old_value;
	struct dm_btree_value_type *vt = &info->value_type;

	size_of_block = dm_bm_block_size(dm_tm_get_bm(info->btree_info.tm));
	max_entries = calc_max_entries(info->value_type.size, size_of_block);

	r = shadow_ablock(info, &root, index / max_entries, &block, &ab);
	if (r)
		return r;
	*new_root = root;

	entry = index % max_entries;
	if (entry >= le32_to_cpu(ab->nr_entries)) {
		r = -ENODATA;
		goto out;
	}

	old_value = element_at(info, ab, entry);
	if (vt->dec &&
	    (!vt->equal || !vt->equal(vt->context, old_value, value))) {
		vt->dec(vt->context, old_value, 1);
		if (vt->inc)
			vt->inc(vt->context, value, 1);
	}

	memcpy(old_value, value, info->value_type.size);

out:
	unlock_ablock(info, block);
	return r;
}

int dm_array_set_value(struct dm_array_info *info, dm_block_t root,
		 uint32_t index, const void *value, dm_block_t *new_root)
	__dm_written_to_disk(value)
{
	int r;

	r = array_set_value(info, root, index, value, new_root);
	__dm_unbless_for_disk(value);
	return r;
}
EXPORT_SYMBOL_GPL(dm_array_set_value);

struct walk_info {
	struct dm_array_info *info;
	int (*fn)(void *context, uint64_t key, void *leaf);
	void *context;
};

static int walk_ablock(void *context, uint64_t *keys, void *leaf)
{
	struct walk_info *wi = context;

	int r;
	unsigned int i;
	__le64 block_le;
	unsigned int nr_entries, max_entries;
	struct dm_block *block;
	struct array_block *ab;

	memcpy(&block_le, leaf, sizeof(block_le));
	r = get_ablock(wi->info, le64_to_cpu(block_le), &block, &ab);
	if (r)
		return r;

	max_entries = le32_to_cpu(ab->max_entries);
	nr_entries = le32_to_cpu(ab->nr_entries);
	for (i = 0; i < nr_entries; i++) {
		r = wi->fn(wi->context, keys[0] * max_entries + i,
			   element_at(wi->info, ab, i));

		if (r)
			break;
	}

	unlock_ablock(wi->info, block);
	return r;
}

int dm_array_walk(struct dm_array_info *info, dm_block_t root,
		  int (*fn)(void *, uint64_t key, void *leaf),
		  void *context)
{
	struct walk_info wi;

	wi.info = info;
	wi.fn = fn;
	wi.context = context;

	return dm_btree_walk(&info->btree_info, root, walk_ablock, &wi);
}
EXPORT_SYMBOL_GPL(dm_array_walk);

/*----------------------------------------------------------------*/

static int load_ablock(struct dm_array_cursor *c)
{
	int r;
	__le64 value_le;
	uint64_t key;

	if (c->block)
		unlock_ablock(c->info, c->block);

	c->index = 0;

	r = dm_btree_cursor_get_value(&c->cursor, &key, &value_le);
	if (r) {
		DMERR("dm_btree_cursor_get_value failed");
		goto out;

	} else {
		r = get_ablock(c->info, le64_to_cpu(value_le), &c->block, &c->ab);
		if (r) {
			DMERR("get_ablock failed");
			goto out;
		}
	}

	return 0;

out:
	dm_btree_cursor_end(&c->cursor);
	c->block = NULL;
	c->ab = NULL;
	return r;
}

int dm_array_cursor_begin(struct dm_array_info *info, dm_block_t root,
			  struct dm_array_cursor *c)
{
	int r;

	memset(c, 0, sizeof(*c));
	c->info = info;
	r = dm_btree_cursor_begin(&info->btree_info, root, true, &c->cursor);
	if (r) {
		DMERR("couldn't create btree cursor");
		return r;
	}

	return load_ablock(c);
}
EXPORT_SYMBOL_GPL(dm_array_cursor_begin);

void dm_array_cursor_end(struct dm_array_cursor *c)
{
	if (c->block)
		unlock_ablock(c->info, c->block);

	dm_btree_cursor_end(&c->cursor);
}
EXPORT_SYMBOL_GPL(dm_array_cursor_end);

int dm_array_cursor_next(struct dm_array_cursor *c)
{
	int r;

	if (!c->block)
		return -ENODATA;

	c->index++;

	if (c->index >= le32_to_cpu(c->ab->nr_entries)) {
		r = dm_btree_cursor_next(&c->cursor);
		if (r)
			return r;

		r = load_ablock(c);
		if (r)
			return r;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(dm_array_cursor_next);

int dm_array_cursor_skip(struct dm_array_cursor *c, uint32_t count)
{
	int r;

	do {
		uint32_t remaining = le32_to_cpu(c->ab->nr_entries) - c->index;

		if (count < remaining) {
			c->index += count;
			return 0;
		}

		count -= remaining;
		c->index += (remaining - 1);
		r = dm_array_cursor_next(c);

	} while (!r);

	return r;
}
EXPORT_SYMBOL_GPL(dm_array_cursor_skip);

void dm_array_cursor_get_value(struct dm_array_cursor *c, void **value_le)
{
	*value_le = element_at(c->info, c->ab, c->index);
}
EXPORT_SYMBOL_GPL(dm_array_cursor_get_value);

/*----------------------------------------------------------------*/
