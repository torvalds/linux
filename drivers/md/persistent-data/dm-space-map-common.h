/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * This file is released under the GPL.
 */

#ifndef DM_SPACE_MAP_COMMON_H
#define DM_SPACE_MAP_COMMON_H

#include "dm-btree.h"

/*----------------------------------------------------------------*/

/*
 * Low level disk format
 *
 * Bitmap btree
 * ------------
 *
 * Each value stored in the btree is an index_entry.  This points to a
 * block that is used as a bitmap.  Within the bitmap hold 2 bits per
 * entry, which represent UNUSED = 0, REF_COUNT = 1, REF_COUNT = 2 and
 * REF_COUNT = many.
 *
 * Refcount btree
 * --------------
 *
 * Any entry that has a ref count higher than 2 gets entered in the ref
 * count tree.  The leaf values for this tree is the 32-bit ref count.
 */

struct disk_index_entry {
	__le64 blocknr;
	__le32 nr_free;
	__le32 none_free_before;
} __attribute__ ((packed, aligned(8)));


#define MAX_METADATA_BITMAPS 255
struct disk_metadata_index {
	__le32 csum;
	__le32 padding;
	__le64 blocknr;

	struct disk_index_entry index[MAX_METADATA_BITMAPS];
} __attribute__ ((packed, aligned(8)));

struct ll_disk;

typedef int (*load_ie_fn)(struct ll_disk *ll, dm_block_t index, struct disk_index_entry *result);
typedef int (*save_ie_fn)(struct ll_disk *ll, dm_block_t index, struct disk_index_entry *ie);
typedef int (*init_index_fn)(struct ll_disk *ll);
typedef int (*open_index_fn)(struct ll_disk *ll);
typedef dm_block_t (*max_index_entries_fn)(struct ll_disk *ll);
typedef int (*commit_fn)(struct ll_disk *ll);

struct ll_disk {
	struct dm_transaction_manager *tm;
	struct dm_btree_info bitmap_info;
	struct dm_btree_info ref_count_info;

	uint32_t block_size;
	uint32_t entries_per_block;
	dm_block_t nr_blocks;
	dm_block_t nr_allocated;

	/*
	 * bitmap_root may be a btree root or a simple index.
	 */
	dm_block_t bitmap_root;

	dm_block_t ref_count_root;

	struct disk_metadata_index mi_le;
	load_ie_fn load_ie;
	save_ie_fn save_ie;
	init_index_fn init_index;
	open_index_fn open_index;
	max_index_entries_fn max_entries;
	commit_fn commit;
	bool bitmap_index_changed:1;
};

struct disk_sm_root {
	__le64 nr_blocks;
	__le64 nr_allocated;
	__le64 bitmap_root;
	__le64 ref_count_root;
} __attribute__ ((packed, aligned(8)));

#define ENTRIES_PER_BYTE 4

struct disk_bitmap_header {
	__le32 csum;
	__le32 not_used;
	__le64 blocknr;
} __attribute__ ((packed, aligned(8)));

enum allocation_event {
	SM_NONE,
	SM_ALLOC,
	SM_FREE,
};

/*----------------------------------------------------------------*/

int sm_ll_extend(struct ll_disk *ll, dm_block_t extra_blocks);
int sm_ll_lookup_bitmap(struct ll_disk *ll, dm_block_t b, uint32_t *result);
int sm_ll_lookup(struct ll_disk *ll, dm_block_t b, uint32_t *result);
int sm_ll_find_free_block(struct ll_disk *ll, dm_block_t begin,
			  dm_block_t end, dm_block_t *result);
int sm_ll_find_common_free_block(struct ll_disk *old_ll, struct ll_disk *new_ll,
	                         dm_block_t begin, dm_block_t end, dm_block_t *result);
int sm_ll_insert(struct ll_disk *ll, dm_block_t b, uint32_t ref_count, enum allocation_event *ev);
int sm_ll_inc(struct ll_disk *ll, dm_block_t b, enum allocation_event *ev);
int sm_ll_dec(struct ll_disk *ll, dm_block_t b, enum allocation_event *ev);
int sm_ll_commit(struct ll_disk *ll);

int sm_ll_new_metadata(struct ll_disk *ll, struct dm_transaction_manager *tm);
int sm_ll_open_metadata(struct ll_disk *ll, struct dm_transaction_manager *tm,
			void *root_le, size_t len);

int sm_ll_new_disk(struct ll_disk *ll, struct dm_transaction_manager *tm);
int sm_ll_open_disk(struct ll_disk *ll, struct dm_transaction_manager *tm,
		    void *root_le, size_t len);

/*----------------------------------------------------------------*/

#endif	/* DM_SPACE_MAP_COMMON_H */
