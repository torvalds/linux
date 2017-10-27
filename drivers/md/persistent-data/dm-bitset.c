/*
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * This file is released under the GPL.
 */

#include "dm-bitset.h"
#include "dm-transaction-manager.h"

#include <linux/export.h>
#include <linux/device-mapper.h>

#define DM_MSG_PREFIX "bitset"
#define BITS_PER_ARRAY_ENTRY 64

/*----------------------------------------------------------------*/

static struct dm_btree_value_type bitset_bvt = {
	.context = NULL,
	.size = sizeof(__le64),
	.inc = NULL,
	.dec = NULL,
	.equal = NULL,
};

/*----------------------------------------------------------------*/

void dm_disk_bitset_init(struct dm_transaction_manager *tm,
			 struct dm_disk_bitset *info)
{
	dm_array_info_init(&info->array_info, tm, &bitset_bvt);
	info->current_index_set = false;
}
EXPORT_SYMBOL_GPL(dm_disk_bitset_init);

int dm_bitset_empty(struct dm_disk_bitset *info, dm_block_t *root)
{
	return dm_array_empty(&info->array_info, root);
}
EXPORT_SYMBOL_GPL(dm_bitset_empty);

struct packer_context {
	bit_value_fn fn;
	unsigned nr_bits;
	void *context;
};

static int pack_bits(uint32_t index, void *value, void *context)
{
	int r;
	struct packer_context *p = context;
	unsigned bit, nr = min(64u, p->nr_bits - (index * 64));
	uint64_t word = 0;
	bool bv;

	for (bit = 0; bit < nr; bit++) {
		r = p->fn(index * 64 + bit, &bv, p->context);
		if (r)
			return r;

		if (bv)
			set_bit(bit, (unsigned long *) &word);
		else
			clear_bit(bit, (unsigned long *) &word);
	}

	*((__le64 *) value) = cpu_to_le64(word);

	return 0;
}

int dm_bitset_new(struct dm_disk_bitset *info, dm_block_t *root,
		  uint32_t size, bit_value_fn fn, void *context)
{
	struct packer_context p;
	p.fn = fn;
	p.nr_bits = size;
	p.context = context;

	return dm_array_new(&info->array_info, root, dm_div_up(size, 64), pack_bits, &p);
}
EXPORT_SYMBOL_GPL(dm_bitset_new);

int dm_bitset_resize(struct dm_disk_bitset *info, dm_block_t root,
		     uint32_t old_nr_entries, uint32_t new_nr_entries,
		     bool default_value, dm_block_t *new_root)
{
	uint32_t old_blocks = dm_div_up(old_nr_entries, BITS_PER_ARRAY_ENTRY);
	uint32_t new_blocks = dm_div_up(new_nr_entries, BITS_PER_ARRAY_ENTRY);
	__le64 value = default_value ? cpu_to_le64(~0) : cpu_to_le64(0);

	__dm_bless_for_disk(&value);
	return dm_array_resize(&info->array_info, root, old_blocks, new_blocks,
			       &value, new_root);
}
EXPORT_SYMBOL_GPL(dm_bitset_resize);

int dm_bitset_del(struct dm_disk_bitset *info, dm_block_t root)
{
	return dm_array_del(&info->array_info, root);
}
EXPORT_SYMBOL_GPL(dm_bitset_del);

int dm_bitset_flush(struct dm_disk_bitset *info, dm_block_t root,
		    dm_block_t *new_root)
{
	int r;
	__le64 value;

	if (!info->current_index_set || !info->dirty)
		return 0;

	value = cpu_to_le64(info->current_bits);

	__dm_bless_for_disk(&value);
	r = dm_array_set_value(&info->array_info, root, info->current_index,
			       &value, new_root);
	if (r)
		return r;

	info->current_index_set = false;
	info->dirty = false;

	return 0;
}
EXPORT_SYMBOL_GPL(dm_bitset_flush);

static int read_bits(struct dm_disk_bitset *info, dm_block_t root,
		     uint32_t array_index)
{
	int r;
	__le64 value;

	r = dm_array_get_value(&info->array_info, root, array_index, &value);
	if (r)
		return r;

	info->current_bits = le64_to_cpu(value);
	info->current_index_set = true;
	info->current_index = array_index;
	info->dirty = false;

	return 0;
}

static int get_array_entry(struct dm_disk_bitset *info, dm_block_t root,
			   uint32_t index, dm_block_t *new_root)
{
	int r;
	unsigned array_index = index / BITS_PER_ARRAY_ENTRY;

	if (info->current_index_set) {
		if (info->current_index == array_index)
			return 0;

		r = dm_bitset_flush(info, root, new_root);
		if (r)
			return r;
	}

	return read_bits(info, root, array_index);
}

int dm_bitset_set_bit(struct dm_disk_bitset *info, dm_block_t root,
		      uint32_t index, dm_block_t *new_root)
{
	int r;
	unsigned b = index % BITS_PER_ARRAY_ENTRY;

	r = get_array_entry(info, root, index, new_root);
	if (r)
		return r;

	set_bit(b, (unsigned long *) &info->current_bits);
	info->dirty = true;

	return 0;
}
EXPORT_SYMBOL_GPL(dm_bitset_set_bit);

int dm_bitset_clear_bit(struct dm_disk_bitset *info, dm_block_t root,
			uint32_t index, dm_block_t *new_root)
{
	int r;
	unsigned b = index % BITS_PER_ARRAY_ENTRY;

	r = get_array_entry(info, root, index, new_root);
	if (r)
		return r;

	clear_bit(b, (unsigned long *) &info->current_bits);
	info->dirty = true;

	return 0;
}
EXPORT_SYMBOL_GPL(dm_bitset_clear_bit);

int dm_bitset_test_bit(struct dm_disk_bitset *info, dm_block_t root,
		       uint32_t index, dm_block_t *new_root, bool *result)
{
	int r;
	unsigned b = index % BITS_PER_ARRAY_ENTRY;

	r = get_array_entry(info, root, index, new_root);
	if (r)
		return r;

	*result = test_bit(b, (unsigned long *) &info->current_bits);
	return 0;
}
EXPORT_SYMBOL_GPL(dm_bitset_test_bit);

static int cursor_next_array_entry(struct dm_bitset_cursor *c)
{
	int r;
	__le64 *value;

	r = dm_array_cursor_next(&c->cursor);
	if (r)
		return r;

	dm_array_cursor_get_value(&c->cursor, (void **) &value);
	c->array_index++;
	c->bit_index = 0;
	c->current_bits = le64_to_cpu(*value);
	return 0;
}

int dm_bitset_cursor_begin(struct dm_disk_bitset *info,
			   dm_block_t root, uint32_t nr_entries,
			   struct dm_bitset_cursor *c)
{
	int r;
	__le64 *value;

	if (!nr_entries)
		return -ENODATA;

	c->info = info;
	c->entries_remaining = nr_entries;

	r = dm_array_cursor_begin(&info->array_info, root, &c->cursor);
	if (r)
		return r;

	dm_array_cursor_get_value(&c->cursor, (void **) &value);
	c->array_index = 0;
	c->bit_index = 0;
	c->current_bits = le64_to_cpu(*value);

	return r;
}
EXPORT_SYMBOL_GPL(dm_bitset_cursor_begin);

void dm_bitset_cursor_end(struct dm_bitset_cursor *c)
{
	return dm_array_cursor_end(&c->cursor);
}
EXPORT_SYMBOL_GPL(dm_bitset_cursor_end);

int dm_bitset_cursor_next(struct dm_bitset_cursor *c)
{
	int r = 0;

	if (!c->entries_remaining)
		return -ENODATA;

	c->entries_remaining--;
	if (++c->bit_index > 63)
		r = cursor_next_array_entry(c);

	return r;
}
EXPORT_SYMBOL_GPL(dm_bitset_cursor_next);

int dm_bitset_cursor_skip(struct dm_bitset_cursor *c, uint32_t count)
{
	int r;
	__le64 *value;
	uint32_t nr_array_skip;
	uint32_t remaining_in_word = 64 - c->bit_index;

	if (c->entries_remaining < count)
		return -ENODATA;

	if (count < remaining_in_word) {
		c->bit_index += count;
		c->entries_remaining -= count;
		return 0;

	} else {
		c->entries_remaining -= remaining_in_word;
		count -= remaining_in_word;
	}

	nr_array_skip = (count / 64) + 1;
	r = dm_array_cursor_skip(&c->cursor, nr_array_skip);
	if (r)
		return r;

	dm_array_cursor_get_value(&c->cursor, (void **) &value);
	c->entries_remaining -= count;
	c->array_index += nr_array_skip;
	c->bit_index = count & 63;
	c->current_bits = le64_to_cpu(*value);

	return 0;
}
EXPORT_SYMBOL_GPL(dm_bitset_cursor_skip);

bool dm_bitset_cursor_get_value(struct dm_bitset_cursor *c)
{
	return test_bit(c->bit_index, (unsigned long *) &c->current_bits);
}
EXPORT_SYMBOL_GPL(dm_bitset_cursor_get_value);

/*----------------------------------------------------------------*/
