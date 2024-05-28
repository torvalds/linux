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

/*
 * The block map is responsible for tracking all the logical to physical mappings of a VDO. It
 * consists of a collection of 60 radix trees gradually allocated as logical addresses are used.
 * Each tree is assigned to a logical zone such that it is easy to compute which zone must handle
 * each logical address. Each logical zone also has a dedicated portion of the leaf page cache.
 *
 * Each logical zone has a single dedicated queue and thread for performing all updates to the
 * radix trees assigned to that zone. The concurrency guarantees of this single-threaded model
 * allow the code to omit more fine-grained locking for the block map structures.
 *
 * Load operations must be performed on the admin thread. Normal operations, such as reading and
 * updating mappings, must be performed on the appropriate logical zone thread. Save operations
 * must be launched from the same admin thread as the original load operation.
 */

enum {
	BLOCK_MAP_VIO_POOL_SIZE = 64,
};

/*
 * Generation counter for page references.
 */
typedef u32 vdo_page_generation;

extern const struct block_map_entry UNMAPPED_BLOCK_MAP_ENTRY;

/* The VDO Page Cache abstraction. */
struct vdo_page_cache {
	/* the VDO which owns this cache */
	struct vdo *vdo;
	/* number of pages in cache */
	page_count_t page_count;
	/* number of pages to write in the current batch */
	page_count_t pages_in_batch;
	/* Whether the VDO is doing a read-only rebuild */
	bool rebuilding;

	/* array of page information entries */
	struct page_info *infos;
	/* raw memory for pages */
	char *pages;
	/* cache last found page info */
	struct page_info *last_found;
	/* map of page number to info */
	struct int_map *page_map;
	/* main LRU list (all infos) */
	struct list_head lru_list;
	/* free page list (oldest first) */
	struct list_head free_list;
	/* outgoing page list */
	struct list_head outgoing_list;
	/* number of read I/O operations pending */
	page_count_t outstanding_reads;
	/* number of write I/O operations pending */
	page_count_t outstanding_writes;
	/* number of pages covered by the current flush */
	page_count_t pages_in_flush;
	/* number of pages waiting to be included in the next flush */
	page_count_t pages_to_flush;
	/* number of discards in progress */
	unsigned int discard_count;
	/* how many VPCs waiting for free page */
	unsigned int waiter_count;
	/* queue of waiters who want a free page */
	struct vdo_wait_queue free_waiters;
	/*
	 * Statistics are only updated on the logical zone thread, but are accessed from other
	 * threads.
	 */
	struct block_map_statistics stats;
	/* counter for pressure reports */
	u32 pressure_report;
	/* the block map zone to which this cache belongs */
	struct block_map_zone *zone;
};

/*
 * The state of a page buffer. If the page buffer is free no particular page is bound to it,
 * otherwise the page buffer is bound to particular page whose absolute pbn is in the pbn field. If
 * the page is resident or dirty the page data is stable and may be accessed. Otherwise the page is
 * in flight (incoming or outgoing) and its data should not be accessed.
 *
 * @note Update the static data in get_page_state_name() if you change this enumeration.
 */
enum vdo_page_buffer_state {
	/* this page buffer is not being used */
	PS_FREE,
	/* this page is being read from store */
	PS_INCOMING,
	/* attempt to load this page failed */
	PS_FAILED,
	/* this page is valid and un-modified */
	PS_RESIDENT,
	/* this page is valid and modified */
	PS_DIRTY,
	/* this page is being written and should not be used */
	PS_OUTGOING,
	/* not a state */
	PAGE_STATE_COUNT,
} __packed;

/*
 * The write status of page
 */
enum vdo_page_write_status {
	WRITE_STATUS_NORMAL,
	WRITE_STATUS_DISCARD,
	WRITE_STATUS_DEFERRED,
} __packed;

/* Per-page-slot information. */
struct page_info {
	/* Preallocated page struct vio */
	struct vio *vio;
	/* back-link for references */
	struct vdo_page_cache *cache;
	/* the pbn of the page */
	physical_block_number_t pbn;
	/* page is busy (temporarily locked) */
	u16 busy;
	/* the write status the page */
	enum vdo_page_write_status write_status;
	/* page state */
	enum vdo_page_buffer_state state;
	/* queue of completions awaiting this item */
	struct vdo_wait_queue waiting;
	/* state linked list entry */
	struct list_head state_entry;
	/* LRU entry */
	struct list_head lru_entry;
	/*
	 * The earliest recovery journal block containing uncommitted updates to the block map page
	 * associated with this page_info. A reference (lock) is held on that block to prevent it
	 * from being reaped. When this value changes, the reference on the old value must be
	 * released and a reference on the new value must be acquired.
	 */
	sequence_number_t recovery_lock;
};

/*
 * A completion awaiting a specific page. Also a live reference into the page once completed, until
 * freed.
 */
struct vdo_page_completion {
	/* The generic completion */
	struct vdo_completion completion;
	/* The cache involved */
	struct vdo_page_cache *cache;
	/* The waiter for the pending list */
	struct vdo_waiter waiter;
	/* The absolute physical block number of the page on disk */
	physical_block_number_t pbn;
	/* Whether the page may be modified */
	bool writable;
	/* Whether the page is available */
	bool ready;
	/* The info structure for the page, only valid when ready */
	struct page_info *info;
};

struct forest;

struct tree_page {
	struct vdo_waiter waiter;

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
	/* The number of periods after which an element will be expired */
	block_count_t maximum_age;
	/* The oldest period which has unexpired elements */
	sequence_number_t oldest_period;
	/* One more than the current period */
	sequence_number_t next_period;
	/* The offset in the array of lists of the oldest period */
	block_count_t offset;
	/* Expired pages */
	dirty_era_t expired;
	/* The lists of dirty pages */
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
	struct vdo_wait_queue flush_waiters;
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

static inline struct vdo_page_completion *as_vdo_page_completion(struct vdo_completion *completion)
{
	vdo_assert_completion_type(completion, VDO_PAGE_COMPLETION);
	return container_of(completion, struct vdo_page_completion, completion);
}

void vdo_release_page_completion(struct vdo_completion *completion);

void vdo_get_page(struct vdo_page_completion *page_completion,
		  struct block_map_zone *zone, physical_block_number_t pbn,
		  bool writable, void *parent, vdo_action_fn callback,
		  vdo_action_fn error_handler, bool requeue);

void vdo_request_page_write(struct vdo_completion *completion);

int __must_check vdo_get_cached_page(struct vdo_completion *completion,
				     struct block_map_page **page_ptr);

int __must_check vdo_invalidate_page_cache(struct vdo_page_cache *cache);

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
			 struct vdo_completion *completion);

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
