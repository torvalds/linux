/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * This file is released under the GPL.
 */

#include "dm-space-map-common.h"
#include "dm-transaction-manager.h"

#include <linux/bitops.h>
#include <linux/device-mapper.h>

#define DM_MSG_PREFIX "space map common"

/*----------------------------------------------------------------*/

/*
 * Index validator.
 */
#define INDEX_CSUM_XOR 160478

static void index_prepare_for_write(struct dm_block_validator *v,
				    struct dm_block *b,
				    size_t block_size)
{
	struct disk_metadata_index *mi_le = dm_block_data(b);

	mi_le->blocknr = cpu_to_le64(dm_block_location(b));
	mi_le->csum = cpu_to_le32(dm_bm_checksum(&mi_le->padding,
						 block_size - sizeof(__le32),
						 INDEX_CSUM_XOR));
}

static int index_check(struct dm_block_validator *v,
		       struct dm_block *b,
		       size_t block_size)
{
	struct disk_metadata_index *mi_le = dm_block_data(b);
	__le32 csum_disk;

	if (dm_block_location(b) != le64_to_cpu(mi_le->blocknr)) {
		DMERR_LIMIT("index_check failed: blocknr %llu != wanted %llu",
			    le64_to_cpu(mi_le->blocknr), dm_block_location(b));
		return -ENOTBLK;
	}

	csum_disk = cpu_to_le32(dm_bm_checksum(&mi_le->padding,
					       block_size - sizeof(__le32),
					       INDEX_CSUM_XOR));
	if (csum_disk != mi_le->csum) {
		DMERR_LIMIT("index_check failed: csum %u != wanted %u",
			    le32_to_cpu(csum_disk), le32_to_cpu(mi_le->csum));
		return -EILSEQ;
	}

	return 0;
}

static struct dm_block_validator index_validator = {
	.name = "index",
	.prepare_for_write = index_prepare_for_write,
	.check = index_check
};

/*----------------------------------------------------------------*/

/*
 * Bitmap validator
 */
#define BITMAP_CSUM_XOR 240779

static void bitmap_prepare_for_write(struct dm_block_validator *v,
				     struct dm_block *b,
				     size_t block_size)
{
	struct disk_bitmap_header *disk_header = dm_block_data(b);

	disk_header->blocknr = cpu_to_le64(dm_block_location(b));
	disk_header->csum = cpu_to_le32(dm_bm_checksum(&disk_header->not_used,
						       block_size - sizeof(__le32),
						       BITMAP_CSUM_XOR));
}

static int bitmap_check(struct dm_block_validator *v,
			struct dm_block *b,
			size_t block_size)
{
	struct disk_bitmap_header *disk_header = dm_block_data(b);
	__le32 csum_disk;

	if (dm_block_location(b) != le64_to_cpu(disk_header->blocknr)) {
		DMERR_LIMIT("bitmap check failed: blocknr %llu != wanted %llu",
			    le64_to_cpu(disk_header->blocknr), dm_block_location(b));
		return -ENOTBLK;
	}

	csum_disk = cpu_to_le32(dm_bm_checksum(&disk_header->not_used,
					       block_size - sizeof(__le32),
					       BITMAP_CSUM_XOR));
	if (csum_disk != disk_header->csum) {
		DMERR_LIMIT("bitmap check failed: csum %u != wanted %u",
			    le32_to_cpu(csum_disk), le32_to_cpu(disk_header->csum));
		return -EILSEQ;
	}

	return 0;
}

static struct dm_block_validator dm_sm_bitmap_validator = {
	.name = "sm_bitmap",
	.prepare_for_write = bitmap_prepare_for_write,
	.check = bitmap_check
};

/*----------------------------------------------------------------*/

#define ENTRIES_PER_WORD 32
#define ENTRIES_SHIFT	5

static void *dm_bitmap_data(struct dm_block *b)
{
	return dm_block_data(b) + sizeof(struct disk_bitmap_header);
}

#define WORD_MASK_HIGH 0xAAAAAAAAAAAAAAAAULL

static unsigned bitmap_word_used(void *addr, unsigned b)
{
	__le64 *words_le = addr;
	__le64 *w_le = words_le + (b >> ENTRIES_SHIFT);

	uint64_t bits = le64_to_cpu(*w_le);
	uint64_t mask = (bits + WORD_MASK_HIGH + 1) & WORD_MASK_HIGH;

	return !(~bits & mask);
}

static unsigned sm_lookup_bitmap(void *addr, unsigned b)
{
	__le64 *words_le = addr;
	__le64 *w_le = words_le + (b >> ENTRIES_SHIFT);
	unsigned hi, lo;

	b = (b & (ENTRIES_PER_WORD - 1)) << 1;
	hi = !!test_bit_le(b, (void *) w_le);
	lo = !!test_bit_le(b + 1, (void *) w_le);
	return (hi << 1) | lo;
}

static void sm_set_bitmap(void *addr, unsigned b, unsigned val)
{
	__le64 *words_le = addr;
	__le64 *w_le = words_le + (b >> ENTRIES_SHIFT);

	b = (b & (ENTRIES_PER_WORD - 1)) << 1;

	if (val & 2)
		__set_bit_le(b, (void *) w_le);
	else
		__clear_bit_le(b, (void *) w_le);

	if (val & 1)
		__set_bit_le(b + 1, (void *) w_le);
	else
		__clear_bit_le(b + 1, (void *) w_le);
}

static int sm_find_free(void *addr, unsigned begin, unsigned end,
			unsigned *result)
{
	while (begin < end) {
		if (!(begin & (ENTRIES_PER_WORD - 1)) &&
		    bitmap_word_used(addr, begin)) {
			begin += ENTRIES_PER_WORD;
			continue;
		}

		if (!sm_lookup_bitmap(addr, begin)) {
			*result = begin;
			return 0;
		}

		begin++;
	}

	return -ENOSPC;
}

/*----------------------------------------------------------------*/

static int sm_ll_init(struct ll_disk *ll, struct dm_transaction_manager *tm)
{
	ll->tm = tm;

	ll->bitmap_info.tm = tm;
	ll->bitmap_info.levels = 1;

	/*
	 * Because the new bitmap blocks are created via a shadow
	 * operation, the old entry has already had its reference count
	 * decremented and we don't need the btree to do any bookkeeping.
	 */
	ll->bitmap_info.value_type.size = sizeof(struct disk_index_entry);
	ll->bitmap_info.value_type.inc = NULL;
	ll->bitmap_info.value_type.dec = NULL;
	ll->bitmap_info.value_type.equal = NULL;

	ll->ref_count_info.tm = tm;
	ll->ref_count_info.levels = 1;
	ll->ref_count_info.value_type.size = sizeof(uint32_t);
	ll->ref_count_info.value_type.inc = NULL;
	ll->ref_count_info.value_type.dec = NULL;
	ll->ref_count_info.value_type.equal = NULL;

	ll->block_size = dm_bm_block_size(dm_tm_get_bm(tm));

	if (ll->block_size > (1 << 30)) {
		DMERR("block size too big to hold bitmaps");
		return -EINVAL;
	}

	ll->entries_per_block = (ll->block_size - sizeof(struct disk_bitmap_header)) *
		ENTRIES_PER_BYTE;
	ll->nr_blocks = 0;
	ll->bitmap_root = 0;
	ll->ref_count_root = 0;
	ll->bitmap_index_changed = false;

	return 0;
}

int sm_ll_extend(struct ll_disk *ll, dm_block_t extra_blocks)
{
	int r;
	dm_block_t i, nr_blocks, nr_indexes;
	unsigned old_blocks, blocks;

	nr_blocks = ll->nr_blocks + extra_blocks;
	old_blocks = dm_sector_div_up(ll->nr_blocks, ll->entries_per_block);
	blocks = dm_sector_div_up(nr_blocks, ll->entries_per_block);

	nr_indexes = dm_sector_div_up(nr_blocks, ll->entries_per_block);
	if (nr_indexes > ll->max_entries(ll)) {
		DMERR("space map too large");
		return -EINVAL;
	}

	/*
	 * We need to set this before the dm_tm_new_block() call below.
	 */
	ll->nr_blocks = nr_blocks;
	for (i = old_blocks; i < blocks; i++) {
		struct dm_block *b;
		struct disk_index_entry idx;

		r = dm_tm_new_block(ll->tm, &dm_sm_bitmap_validator, &b);
		if (r < 0)
			return r;

		idx.blocknr = cpu_to_le64(dm_block_location(b));

		dm_tm_unlock(ll->tm, b);

		idx.nr_free = cpu_to_le32(ll->entries_per_block);
		idx.none_free_before = 0;

		r = ll->save_ie(ll, i, &idx);
		if (r < 0)
			return r;
	}

	return 0;
}

int sm_ll_lookup_bitmap(struct ll_disk *ll, dm_block_t b, uint32_t *result)
{
	int r;
	dm_block_t index = b;
	struct disk_index_entry ie_disk;
	struct dm_block *blk;

	b = do_div(index, ll->entries_per_block);
	r = ll->load_ie(ll, index, &ie_disk);
	if (r < 0)
		return r;

	r = dm_tm_read_lock(ll->tm, le64_to_cpu(ie_disk.blocknr),
			    &dm_sm_bitmap_validator, &blk);
	if (r < 0)
		return r;

	*result = sm_lookup_bitmap(dm_bitmap_data(blk), b);

	dm_tm_unlock(ll->tm, blk);

	return 0;
}

static int sm_ll_lookup_big_ref_count(struct ll_disk *ll, dm_block_t b,
				      uint32_t *result)
{
	__le32 le_rc;
	int r;

	r = dm_btree_lookup(&ll->ref_count_info, ll->ref_count_root, &b, &le_rc);
	if (r < 0)
		return r;

	*result = le32_to_cpu(le_rc);

	return r;
}

int sm_ll_lookup(struct ll_disk *ll, dm_block_t b, uint32_t *result)
{
	int r = sm_ll_lookup_bitmap(ll, b, result);

	if (r)
		return r;

	if (*result != 3)
		return r;

	return sm_ll_lookup_big_ref_count(ll, b, result);
}

int sm_ll_find_free_block(struct ll_disk *ll, dm_block_t begin,
			  dm_block_t end, dm_block_t *result)
{
	int r;
	struct disk_index_entry ie_disk;
	dm_block_t i, index_begin = begin;
	dm_block_t index_end = dm_sector_div_up(end, ll->entries_per_block);

	/*
	 * FIXME: Use shifts
	 */
	begin = do_div(index_begin, ll->entries_per_block);
	end = do_div(end, ll->entries_per_block);

	for (i = index_begin; i < index_end; i++, begin = 0) {
		struct dm_block *blk;
		unsigned position;
		uint32_t bit_end;

		r = ll->load_ie(ll, i, &ie_disk);
		if (r < 0)
			return r;

		if (le32_to_cpu(ie_disk.nr_free) == 0)
			continue;

		r = dm_tm_read_lock(ll->tm, le64_to_cpu(ie_disk.blocknr),
				    &dm_sm_bitmap_validator, &blk);
		if (r < 0)
			return r;

		bit_end = (i == index_end - 1) ?  end : ll->entries_per_block;

		r = sm_find_free(dm_bitmap_data(blk),
				 max_t(unsigned, begin, le32_to_cpu(ie_disk.none_free_before)),
				 bit_end, &position);
		if (r == -ENOSPC) {
			/*
			 * This might happen because we started searching
			 * part way through the bitmap.
			 */
			dm_tm_unlock(ll->tm, blk);
			continue;

		} else if (r < 0) {
			dm_tm_unlock(ll->tm, blk);
			return r;
		}

		dm_tm_unlock(ll->tm, blk);

		*result = i * ll->entries_per_block + (dm_block_t) position;
		return 0;
	}

	return -ENOSPC;
}

static int sm_ll_mutate(struct ll_disk *ll, dm_block_t b,
			int (*mutator)(void *context, uint32_t old, uint32_t *new),
			void *context, enum allocation_event *ev)
{
	int r;
	uint32_t bit, old, ref_count;
	struct dm_block *nb;
	dm_block_t index = b;
	struct disk_index_entry ie_disk;
	void *bm_le;
	int inc;

	bit = do_div(index, ll->entries_per_block);
	r = ll->load_ie(ll, index, &ie_disk);
	if (r < 0)
		return r;

	r = dm_tm_shadow_block(ll->tm, le64_to_cpu(ie_disk.blocknr),
			       &dm_sm_bitmap_validator, &nb, &inc);
	if (r < 0) {
		DMERR("dm_tm_shadow_block() failed");
		return r;
	}
	ie_disk.blocknr = cpu_to_le64(dm_block_location(nb));

	bm_le = dm_bitmap_data(nb);
	old = sm_lookup_bitmap(bm_le, bit);

	if (old > 2) {
		r = sm_ll_lookup_big_ref_count(ll, b, &old);
		if (r < 0) {
			dm_tm_unlock(ll->tm, nb);
			return r;
		}
	}

	r = mutator(context, old, &ref_count);
	if (r) {
		dm_tm_unlock(ll->tm, nb);
		return r;
	}

	if (ref_count <= 2) {
		sm_set_bitmap(bm_le, bit, ref_count);

		dm_tm_unlock(ll->tm, nb);

		if (old > 2) {
			r = dm_btree_remove(&ll->ref_count_info,
					    ll->ref_count_root,
					    &b, &ll->ref_count_root);
			if (r)
				return r;
		}

	} else {
		__le32 le_rc = cpu_to_le32(ref_count);

		sm_set_bitmap(bm_le, bit, 3);
		dm_tm_unlock(ll->tm, nb);

		__dm_bless_for_disk(&le_rc);
		r = dm_btree_insert(&ll->ref_count_info, ll->ref_count_root,
				    &b, &le_rc, &ll->ref_count_root);
		if (r < 0) {
			DMERR("ref count insert failed");
			return r;
		}
	}

	if (ref_count && !old) {
		*ev = SM_ALLOC;
		ll->nr_allocated++;
		le32_add_cpu(&ie_disk.nr_free, -1);
		if (le32_to_cpu(ie_disk.none_free_before) == bit)
			ie_disk.none_free_before = cpu_to_le32(bit + 1);

	} else if (old && !ref_count) {
		*ev = SM_FREE;
		ll->nr_allocated--;
		le32_add_cpu(&ie_disk.nr_free, 1);
		ie_disk.none_free_before = cpu_to_le32(min(le32_to_cpu(ie_disk.none_free_before), bit));
	}

	return ll->save_ie(ll, index, &ie_disk);
}

static int set_ref_count(void *context, uint32_t old, uint32_t *new)
{
	*new = *((uint32_t *) context);
	return 0;
}

int sm_ll_insert(struct ll_disk *ll, dm_block_t b,
		 uint32_t ref_count, enum allocation_event *ev)
{
	return sm_ll_mutate(ll, b, set_ref_count, &ref_count, ev);
}

static int inc_ref_count(void *context, uint32_t old, uint32_t *new)
{
	*new = old + 1;
	return 0;
}

int sm_ll_inc(struct ll_disk *ll, dm_block_t b, enum allocation_event *ev)
{
	return sm_ll_mutate(ll, b, inc_ref_count, NULL, ev);
}

static int dec_ref_count(void *context, uint32_t old, uint32_t *new)
{
	if (!old) {
		DMERR_LIMIT("unable to decrement a reference count below 0");
		return -EINVAL;
	}

	*new = old - 1;
	return 0;
}

int sm_ll_dec(struct ll_disk *ll, dm_block_t b, enum allocation_event *ev)
{
	return sm_ll_mutate(ll, b, dec_ref_count, NULL, ev);
}

int sm_ll_commit(struct ll_disk *ll)
{
	int r = 0;

	if (ll->bitmap_index_changed) {
		r = ll->commit(ll);
		if (!r)
			ll->bitmap_index_changed = false;
	}

	return r;
}

/*----------------------------------------------------------------*/

static int metadata_ll_load_ie(struct ll_disk *ll, dm_block_t index,
			       struct disk_index_entry *ie)
{
	memcpy(ie, ll->mi_le.index + index, sizeof(*ie));
	return 0;
}

static int metadata_ll_save_ie(struct ll_disk *ll, dm_block_t index,
			       struct disk_index_entry *ie)
{
	ll->bitmap_index_changed = true;
	memcpy(ll->mi_le.index + index, ie, sizeof(*ie));
	return 0;
}

static int metadata_ll_init_index(struct ll_disk *ll)
{
	int r;
	struct dm_block *b;

	r = dm_tm_new_block(ll->tm, &index_validator, &b);
	if (r < 0)
		return r;

	memcpy(dm_block_data(b), &ll->mi_le, sizeof(ll->mi_le));
	ll->bitmap_root = dm_block_location(b);

	dm_tm_unlock(ll->tm, b);

	return 0;
}

static int metadata_ll_open(struct ll_disk *ll)
{
	int r;
	struct dm_block *block;

	r = dm_tm_read_lock(ll->tm, ll->bitmap_root,
			    &index_validator, &block);
	if (r)
		return r;

	memcpy(&ll->mi_le, dm_block_data(block), sizeof(ll->mi_le));
	dm_tm_unlock(ll->tm, block);

	return 0;
}

static dm_block_t metadata_ll_max_entries(struct ll_disk *ll)
{
	return MAX_METADATA_BITMAPS;
}

static int metadata_ll_commit(struct ll_disk *ll)
{
	int r, inc;
	struct dm_block *b;

	r = dm_tm_shadow_block(ll->tm, ll->bitmap_root, &index_validator, &b, &inc);
	if (r)
		return r;

	memcpy(dm_block_data(b), &ll->mi_le, sizeof(ll->mi_le));
	ll->bitmap_root = dm_block_location(b);

	dm_tm_unlock(ll->tm, b);

	return 0;
}

int sm_ll_new_metadata(struct ll_disk *ll, struct dm_transaction_manager *tm)
{
	int r;

	r = sm_ll_init(ll, tm);
	if (r < 0)
		return r;

	ll->load_ie = metadata_ll_load_ie;
	ll->save_ie = metadata_ll_save_ie;
	ll->init_index = metadata_ll_init_index;
	ll->open_index = metadata_ll_open;
	ll->max_entries = metadata_ll_max_entries;
	ll->commit = metadata_ll_commit;

	ll->nr_blocks = 0;
	ll->nr_allocated = 0;

	r = ll->init_index(ll);
	if (r < 0)
		return r;

	r = dm_btree_empty(&ll->ref_count_info, &ll->ref_count_root);
	if (r < 0)
		return r;

	return 0;
}

int sm_ll_open_metadata(struct ll_disk *ll, struct dm_transaction_manager *tm,
			void *root_le, size_t len)
{
	int r;
	struct disk_sm_root *smr = root_le;

	if (len < sizeof(struct disk_sm_root)) {
		DMERR("sm_metadata root too small");
		return -ENOMEM;
	}

	r = sm_ll_init(ll, tm);
	if (r < 0)
		return r;

	ll->load_ie = metadata_ll_load_ie;
	ll->save_ie = metadata_ll_save_ie;
	ll->init_index = metadata_ll_init_index;
	ll->open_index = metadata_ll_open;
	ll->max_entries = metadata_ll_max_entries;
	ll->commit = metadata_ll_commit;

	ll->nr_blocks = le64_to_cpu(smr->nr_blocks);
	ll->nr_allocated = le64_to_cpu(smr->nr_allocated);
	ll->bitmap_root = le64_to_cpu(smr->bitmap_root);
	ll->ref_count_root = le64_to_cpu(smr->ref_count_root);

	return ll->open_index(ll);
}

/*----------------------------------------------------------------*/

static int disk_ll_load_ie(struct ll_disk *ll, dm_block_t index,
			   struct disk_index_entry *ie)
{
	return dm_btree_lookup(&ll->bitmap_info, ll->bitmap_root, &index, ie);
}

static int disk_ll_save_ie(struct ll_disk *ll, dm_block_t index,
			   struct disk_index_entry *ie)
{
	__dm_bless_for_disk(ie);
	return dm_btree_insert(&ll->bitmap_info, ll->bitmap_root,
			       &index, ie, &ll->bitmap_root);
}

static int disk_ll_init_index(struct ll_disk *ll)
{
	return dm_btree_empty(&ll->bitmap_info, &ll->bitmap_root);
}

static int disk_ll_open(struct ll_disk *ll)
{
	/* nothing to do */
	return 0;
}

static dm_block_t disk_ll_max_entries(struct ll_disk *ll)
{
	return -1ULL;
}

static int disk_ll_commit(struct ll_disk *ll)
{
	return 0;
}

int sm_ll_new_disk(struct ll_disk *ll, struct dm_transaction_manager *tm)
{
	int r;

	r = sm_ll_init(ll, tm);
	if (r < 0)
		return r;

	ll->load_ie = disk_ll_load_ie;
	ll->save_ie = disk_ll_save_ie;
	ll->init_index = disk_ll_init_index;
	ll->open_index = disk_ll_open;
	ll->max_entries = disk_ll_max_entries;
	ll->commit = disk_ll_commit;

	ll->nr_blocks = 0;
	ll->nr_allocated = 0;

	r = ll->init_index(ll);
	if (r < 0)
		return r;

	r = dm_btree_empty(&ll->ref_count_info, &ll->ref_count_root);
	if (r < 0)
		return r;

	return 0;
}

int sm_ll_open_disk(struct ll_disk *ll, struct dm_transaction_manager *tm,
		    void *root_le, size_t len)
{
	int r;
	struct disk_sm_root *smr = root_le;

	if (len < sizeof(struct disk_sm_root)) {
		DMERR("sm_metadata root too small");
		return -ENOMEM;
	}

	r = sm_ll_init(ll, tm);
	if (r < 0)
		return r;

	ll->load_ie = disk_ll_load_ie;
	ll->save_ie = disk_ll_save_ie;
	ll->init_index = disk_ll_init_index;
	ll->open_index = disk_ll_open;
	ll->max_entries = disk_ll_max_entries;
	ll->commit = disk_ll_commit;

	ll->nr_blocks = le64_to_cpu(smr->nr_blocks);
	ll->nr_allocated = le64_to_cpu(smr->nr_allocated);
	ll->bitmap_root = le64_to_cpu(smr->bitmap_root);
	ll->ref_count_root = le64_to_cpu(smr->ref_count_root);

	return ll->open_index(ll);
}

/*----------------------------------------------------------------*/
