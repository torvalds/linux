/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef VDO_BLOCK_MAP_H
#define VDO_BLOCK_MAP_H

#include <linux/list.h>

#include "numeric.h"

#include "admin-state.h"
#include "completion.h"
#include "encodings.h"
#include "int-map.h"
#include "statistics.h"
#include "types.h"
#include "vio.h"
#include "wait-queue.h"

enum {
	BLOCK_MAP_VIO_POOL_SIZE = 64,
};

/*
 * Generation counter for page references.
 */
typedef u32 vdo_page_generation;

extern const struct block_map_entry UNMAPPED_BLOCK_MAP_ENTRY;

struct forest;

struct tree_page {
	struct waiter waiter;

	/* Dirty list entry */
	struct list_head entry;

	/* If dirty, the tree zone flush generation in which it was last dirtied. */
	u8 generation;

	/* Whether this page is an interior tree page being written out. */
	bool writing;

	/* If writing, the tree zone flush generation of the copy being written. */
	u8 writing_generation;

	/*
	 * Sequence number of the earliest recovery journal block containing uncommitted updates to
	 * this page
	 */
	sequence_number_t recovery_lock;

	/* The value of recovery_lock when the this page last started writing */
	sequence_number_t writing_recovery_lock;

	char page_buffer[VDO_BLOCK_SIZE];
};

enum block_map_page_type {
	VDO_TREE_PAGE,
	VDO_CACHE_PAGE,
};

typedef struct list_head dirty_era_t[2];

struct dirty_lists {
	/** The number of periods after which an element will be expired */
	block_count_t maximum_age;
	/** The oldest period which has unexpired elements */
	sequence_number_t oldest_period;
	/** One more than the current period */
	sequence_number_t next_period;
	/** The offset in the array of lists of the oldest period */
	block_count_t offset;
	/** Expired pages */
	dirty_era_t expired;
	/** The lists of dirty pages */
	dirty_era_t eras[];
};

struct block_map_zone {
	zone_count_t zone_number;
	thread_id_t thread_id;
	struct admin_state state;
	struct block_map *block_map;
	/* Dirty pages, by era*/
	struct dirty_lists *dirty_lists;
	struct vdo_page_cache page_cache;
	data_vio_count_t active_lookups;
	struct int_map *loading_pages;
	struct vio_pool *vio_pool;
	/* The tree page which has issued or will be issuing a flush */
	struct tree_page *flusher;
	struct wait_queue flush_waiters;
	/* The generation after the most recent flush */
	u8 generation;
	u8 oldest_generation;
	/* The counts of dirty pages in each generation */
	u32 dirty_page_counts[256];
};

struct block_map {
	struct vdo *vdo;
	struct action_manager *action_manager;
	/* The absolute PBN of the first root of the tree part of the block map */
	physical_block_number_t root_origin;
	block_count_t root_count;

	/* The era point we are currently distributing to the zones */
	sequence_number_t current_era_point;
	/* The next era point */
	sequence_number_t pending_era_point;

	/* The number of entries in block map */
	block_count_t entry_count;
	nonce_t nonce;
	struct recovery_journal *journal;

	/* The trees for finding block map pages */
	struct forest *forest;
	/* The expanded trees awaiting growth */
	struct forest *next_forest;
	/* The number of entries after growth */
	block_count_t next_entry_count;

	zone_count_t zone_count;
	struct block_map_zone zones[];
};

/**
 * typedef vdo_entry_callback_fn - A function to be called for each allocated PBN when traversing
 *                                 the forest.
 * @pbn: A PBN of a tree node.
 * @completion: The parent completion of the traversal.
 *
 * Return: VDO_SUCCESS or an error.
 */
typedef int (*vdo_entry_callback_fn)(physical_block_number_t pbn,
				     struct vdo_completion *completion);

static inline struct block_map_page * __must_check
vdo_as_block_map_page(struct tree_page *tree_page)
{
	return (struct block_map_page *) tree_page->page_buffer;
}

bool vdo_copy_valid_page(char *buffer, nonce_t nonce,
			 physical_block_number_t pbn,
			 struct block_map_page *page);

void vdo_find_block_map_slot(struct data_vio *data_vio);

physical_block_number_t vdo_find_block_map_page_pbn(struct block_map *map,
						    page_number_t page_number);

void vdo_write_tree_page(struct tree_page *page, struct block_map_zone *zone);

void vdo_traverse_forest(struct block_map *map, vdo_entry_callback_fn callback,
			 struct vdo_completion *parent);

int __must_check vdo_decode_block_map(struct block_map_state_2_0 state,
				      block_count_t logical_blocks, struct vdo *vdo,
				      struct recovery_journal *journal, nonce_t nonce,
				      page_count_t cache_size, block_count_t maximum_age,
				      struct block_map **map_ptr);

void vdo_drain_block_map(struct block_map *map, const struct admin_state_code *operation,
			 struct vdo_completion *parent);

void vdo_resume_block_map(struct block_map *map, struct vdo_completion *parent);

int __must_check vdo_prepare_to_grow_block_map(struct block_map *map,
					       block_count_t new_logical_blocks);

void vdo_grow_block_map(struct block_map *map, struct vdo_completion *parent);

void vdo_abandon_block_map_growth(struct block_map *map);

void vdo_free_block_map(struct block_map *map);

struct block_map_state_2_0 __must_check vdo_record_block_map(const struct block_map *map);

void vdo_initialize_block_map_from_journal(struct block_map *map,
					   struct recovery_journal *journal);

zone_count_t vdo_compute_logical_zone(struct data_vio *data_vio);

void vdo_advance_block_map_era(struct block_map *map,
			       sequence_number_t recovery_block_number);

void vdo_update_block_map_page(struct block_map_page *page, struct data_vio *data_vio,
			       physical_block_number_t pbn,
			       enum block_mapping_state mapping_state,
			       sequence_number_t *recovery_lock);

void vdo_get_mapped_block(struct data_vio *data_vio);

void vdo_put_mapped_block(struct data_vio *data_vio);

struct block_map_statistics __must_check vdo_get_block_map_statistics(struct block_map *map);

/**
 * vdo_convert_maximum_age() - Convert the maximum age to reflect the new recovery journal format
 * @age: The configured maximum age
 *
 * Return: The converted age
 *
 * In the old recovery journal format, each journal block held 311 entries, and every write bio
 * made two entries. The old maximum age was half the usable journal length. In the new format,
 * each block holds only 217 entries, but each bio only makes one entry. We convert the configured
 * age so that the number of writes in a block map era is the same in the old and new formats. This
 * keeps the bound on the amount of work required to recover the block map from the recovery
 * journal the same across the format change. It also keeps the amortization of block map page
 * writes to write bios the same.
 */
static inline block_count_t vdo_convert_maximum_age(block_count_t age)
{
	return DIV_ROUND_UP(age * RECOVERY_JOURNAL_1_ENTRIES_PER_BLOCK,
			    2 * RECOVERY_JOURNAL_ENTRIES_PER_BLOCK);
}

#endif /* VDO_BLOCK_MAP_H */
