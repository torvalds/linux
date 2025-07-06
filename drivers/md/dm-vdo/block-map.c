// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "block-map.h"

#include <linux/bio.h>
#include <linux/ratelimit.h>

#include "errors.h"
#include "logger.h"
#include "memory-alloc.h"
#include "permassert.h"

#include "action-manager.h"
#include "admin-state.h"
#include "completion.h"
#include "constants.h"
#include "data-vio.h"
#include "encodings.h"
#include "io-submitter.h"
#include "physical-zone.h"
#include "recovery-journal.h"
#include "slab-depot.h"
#include "status-codes.h"
#include "types.h"
#include "vdo.h"
#include "vio.h"
#include "wait-queue.h"

/**
 * DOC: Block map eras
 *
 * The block map era, or maximum age, is used as follows:
 *
 * Each block map page, when dirty, records the earliest recovery journal block sequence number of
 * the changes reflected in that dirty block. Sequence numbers are classified into eras: every
 * @maximum_age sequence numbers, we switch to a new era. Block map pages are assigned to eras
 * according to the sequence number they record.
 *
 * In the current (newest) era, block map pages are not written unless there is cache pressure. In
 * the next oldest era, each time a new journal block is written 1/@maximum_age of the pages in
 * this era are issued for write. In all older eras, pages are issued for write immediately.
 */

struct page_descriptor {
	root_count_t root_index;
	height_t height;
	page_number_t page_index;
	slot_number_t slot;
} __packed;

union page_key {
	struct page_descriptor descriptor;
	u64 key;
};

struct write_if_not_dirtied_context {
	struct block_map_zone *zone;
	u8 generation;
};

struct block_map_tree_segment {
	struct tree_page *levels[VDO_BLOCK_MAP_TREE_HEIGHT];
};

struct block_map_tree {
	struct block_map_tree_segment *segments;
};

struct forest {
	struct block_map *map;
	size_t segments;
	struct boundary *boundaries;
	struct tree_page **pages;
	struct block_map_tree trees[];
};

struct cursor_level {
	page_number_t page_index;
	slot_number_t slot;
};

struct cursors;

struct cursor {
	struct vdo_waiter waiter;
	struct block_map_tree *tree;
	height_t height;
	struct cursors *parent;
	struct boundary boundary;
	struct cursor_level levels[VDO_BLOCK_MAP_TREE_HEIGHT];
	struct pooled_vio *vio;
};

struct cursors {
	struct block_map_zone *zone;
	struct vio_pool *pool;
	vdo_entry_callback_fn entry_callback;
	struct vdo_completion *completion;
	root_count_t active_roots;
	struct cursor cursors[];
};

static const physical_block_number_t NO_PAGE = 0xFFFFFFFFFFFFFFFF;

/* Used to indicate that the page holding the location of a tree root has been "loaded". */
static const physical_block_number_t VDO_INVALID_PBN = 0xFFFFFFFFFFFFFFFF;

const struct block_map_entry UNMAPPED_BLOCK_MAP_ENTRY = {
	.mapping_state = VDO_MAPPING_STATE_UNMAPPED & 0x0F,
	.pbn_high_nibble = 0,
	.pbn_low_word = __cpu_to_le32(VDO_ZERO_BLOCK & UINT_MAX),
};

#define LOG_INTERVAL 4000
#define DISPLAY_INTERVAL 100000

/*
 * For adjusting VDO page cache statistic fields which are only mutated on the logical zone thread.
 * Prevents any compiler shenanigans from affecting other threads reading those stats.
 */
#define ADD_ONCE(value, delta) WRITE_ONCE(value, (value) + (delta))

static inline bool is_dirty(const struct page_info *info)
{
	return info->state == PS_DIRTY;
}

static inline bool is_present(const struct page_info *info)
{
	return (info->state == PS_RESIDENT) || (info->state == PS_DIRTY);
}

static inline bool is_in_flight(const struct page_info *info)
{
	return (info->state == PS_INCOMING) || (info->state == PS_OUTGOING);
}

static inline bool is_incoming(const struct page_info *info)
{
	return info->state == PS_INCOMING;
}

static inline bool is_outgoing(const struct page_info *info)
{
	return info->state == PS_OUTGOING;
}

static inline bool is_valid(const struct page_info *info)
{
	return is_present(info) || is_outgoing(info);
}

static char *get_page_buffer(struct page_info *info)
{
	struct vdo_page_cache *cache = info->cache;

	return &cache->pages[(info - cache->infos) * VDO_BLOCK_SIZE];
}

static inline struct vdo_page_completion *page_completion_from_waiter(struct vdo_waiter *waiter)
{
	struct vdo_page_completion *completion;

	if (waiter == NULL)
		return NULL;

	completion = container_of(waiter, struct vdo_page_completion, waiter);
	vdo_assert_completion_type(&completion->completion, VDO_PAGE_COMPLETION);
	return completion;
}

/**
 * initialize_info() - Initialize all page info structures and put them on the free list.
 *
 * Return: VDO_SUCCESS or an error.
 */
static int initialize_info(struct vdo_page_cache *cache)
{
	struct page_info *info;

	INIT_LIST_HEAD(&cache->free_list);
	for (info = cache->infos; info < cache->infos + cache->page_count; info++) {
		int result;

		info->cache = cache;
		info->state = PS_FREE;
		info->pbn = NO_PAGE;

		result = create_metadata_vio(cache->vdo, VIO_TYPE_BLOCK_MAP,
					     VIO_PRIORITY_METADATA, info,
					     get_page_buffer(info), &info->vio);
		if (result != VDO_SUCCESS)
			return result;

		/* The thread ID should never change. */
		info->vio->completion.callback_thread_id = cache->zone->thread_id;

		INIT_LIST_HEAD(&info->state_entry);
		list_add_tail(&info->state_entry, &cache->free_list);
		INIT_LIST_HEAD(&info->lru_entry);
	}

	return VDO_SUCCESS;
}

/**
 * allocate_cache_components() - Allocate components of the cache which require their own
 *                               allocation.
 *
 * The caller is responsible for all clean up on errors.
 *
 * Return: VDO_SUCCESS or an error code.
 */
static int __must_check allocate_cache_components(struct vdo_page_cache *cache)
{
	u64 size = cache->page_count * (u64) VDO_BLOCK_SIZE;
	int result;

	result = vdo_allocate(cache->page_count, struct page_info, "page infos",
			      &cache->infos);
	if (result != VDO_SUCCESS)
		return result;

	result = vdo_allocate_memory(size, VDO_BLOCK_SIZE, "cache pages", &cache->pages);
	if (result != VDO_SUCCESS)
		return result;

	result = vdo_int_map_create(cache->page_count, &cache->page_map);
	if (result != VDO_SUCCESS)
		return result;

	return initialize_info(cache);
}

/**
 * assert_on_cache_thread() - Assert that a function has been called on the VDO page cache's
 *                            thread.
 */
static inline void assert_on_cache_thread(struct vdo_page_cache *cache,
					  const char *function_name)
{
	thread_id_t thread_id = vdo_get_callback_thread_id();

	VDO_ASSERT_LOG_ONLY((thread_id == cache->zone->thread_id),
			    "%s() must only be called on cache thread %d, not thread %d",
			    function_name, cache->zone->thread_id, thread_id);
}

/** assert_io_allowed() - Assert that a page cache may issue I/O. */
static inline void assert_io_allowed(struct vdo_page_cache *cache)
{
	VDO_ASSERT_LOG_ONLY(!vdo_is_state_quiescent(&cache->zone->state),
			    "VDO page cache may issue I/O");
}

/** report_cache_pressure() - Log and, if enabled, report cache pressure. */
static void report_cache_pressure(struct vdo_page_cache *cache)
{
	ADD_ONCE(cache->stats.cache_pressure, 1);
	if (cache->waiter_count > cache->page_count) {
		if ((cache->pressure_report % LOG_INTERVAL) == 0)
			vdo_log_info("page cache pressure %u", cache->stats.cache_pressure);

		if (++cache->pressure_report >= DISPLAY_INTERVAL)
			cache->pressure_report = 0;
	}
}

/**
 * get_page_state_name() - Return the name of a page state.
 *
 * If the page state is invalid a static string is returned and the invalid state is logged.
 *
 * Return: A pointer to a static page state name.
 */
static const char * __must_check get_page_state_name(enum vdo_page_buffer_state state)
{
	int result;
	static const char * const state_names[] = {
		"FREE", "INCOMING", "FAILED", "RESIDENT", "DIRTY", "OUTGOING"
	};

	BUILD_BUG_ON(ARRAY_SIZE(state_names) != PAGE_STATE_COUNT);

	result = VDO_ASSERT(state < ARRAY_SIZE(state_names),
			    "Unknown page_state value %d", state);
	if (result != VDO_SUCCESS)
		return "[UNKNOWN PAGE STATE]";

	return state_names[state];
}

/**
 * update_counter() - Update the counter associated with a given state.
 * @info: The page info to count.
 * @delta: The delta to apply to the counter.
 */
static void update_counter(struct page_info *info, s32 delta)
{
	struct block_map_statistics *stats = &info->cache->stats;

	switch (info->state) {
	case PS_FREE:
		ADD_ONCE(stats->free_pages, delta);
		return;

	case PS_INCOMING:
		ADD_ONCE(stats->incoming_pages, delta);
		return;

	case PS_OUTGOING:
		ADD_ONCE(stats->outgoing_pages, delta);
		return;

	case PS_FAILED:
		ADD_ONCE(stats->failed_pages, delta);
		return;

	case PS_RESIDENT:
		ADD_ONCE(stats->clean_pages, delta);
		return;

	case PS_DIRTY:
		ADD_ONCE(stats->dirty_pages, delta);
		return;

	default:
		return;
	}
}

/** update_lru() - Update the lru information for an active page. */
static void update_lru(struct page_info *info)
{
	if (info->cache->lru_list.prev != &info->lru_entry)
		list_move_tail(&info->lru_entry, &info->cache->lru_list);
}

/**
 * set_info_state() - Set the state of a page_info and put it on the right list, adjusting
 *                    counters.
 */
static void set_info_state(struct page_info *info, enum vdo_page_buffer_state new_state)
{
	if (new_state == info->state)
		return;

	update_counter(info, -1);
	info->state = new_state;
	update_counter(info, 1);

	switch (info->state) {
	case PS_FREE:
	case PS_FAILED:
		list_move_tail(&info->state_entry, &info->cache->free_list);
		return;

	case PS_OUTGOING:
		list_move_tail(&info->state_entry, &info->cache->outgoing_list);
		return;

	case PS_DIRTY:
		return;

	default:
		list_del_init(&info->state_entry);
	}
}

/** set_info_pbn() - Set the pbn for an info, updating the map as needed. */
static int __must_check set_info_pbn(struct page_info *info, physical_block_number_t pbn)
{
	struct vdo_page_cache *cache = info->cache;

	/* Either the new or the old page number must be NO_PAGE. */
	int result = VDO_ASSERT((pbn == NO_PAGE) || (info->pbn == NO_PAGE),
				"Must free a page before reusing it.");
	if (result != VDO_SUCCESS)
		return result;

	if (info->pbn != NO_PAGE)
		vdo_int_map_remove(cache->page_map, info->pbn);

	info->pbn = pbn;

	if (pbn != NO_PAGE) {
		result = vdo_int_map_put(cache->page_map, pbn, info, true, NULL);
		if (result != VDO_SUCCESS)
			return result;
	}
	return VDO_SUCCESS;
}

/** reset_page_info() - Reset page info to represent an unallocated page. */
static int reset_page_info(struct page_info *info)
{
	int result;

	result = VDO_ASSERT(info->busy == 0, "VDO Page must not be busy");
	if (result != VDO_SUCCESS)
		return result;

	result = VDO_ASSERT(!vdo_waitq_has_waiters(&info->waiting),
			    "VDO Page must not have waiters");
	if (result != VDO_SUCCESS)
		return result;

	result = set_info_pbn(info, NO_PAGE);
	set_info_state(info, PS_FREE);
	list_del_init(&info->lru_entry);
	return result;
}

/**
 * find_free_page() - Find a free page.
 *
 * Return: A pointer to the page info structure (if found), NULL otherwise.
 */
static struct page_info * __must_check find_free_page(struct vdo_page_cache *cache)
{
	struct page_info *info;

	info = list_first_entry_or_null(&cache->free_list, struct page_info,
					state_entry);
	if (info != NULL)
		list_del_init(&info->state_entry);

	return info;
}

/**
 * find_page() - Find the page info (if any) associated with a given pbn.
 * @pbn: The absolute physical block number of the page.
 *
 * Return: The page info for the page if available, or NULL if not.
 */
static struct page_info * __must_check find_page(struct vdo_page_cache *cache,
						 physical_block_number_t pbn)
{
	if ((cache->last_found != NULL) && (cache->last_found->pbn == pbn))
		return cache->last_found;

	cache->last_found = vdo_int_map_get(cache->page_map, pbn);
	return cache->last_found;
}

/**
 * select_lru_page() - Determine which page is least recently used.
 *
 * Picks the least recently used from among the non-busy entries at the front of each of the lru
 * list. Since whenever we mark a page busy we also put it to the end of the list it is unlikely
 * that the entries at the front are busy unless the queue is very short, but not impossible.
 *
 * Return: A pointer to the info structure for a relevant page, or NULL if no such page can be
 *         found. The page can be dirty or resident.
 */
static struct page_info * __must_check select_lru_page(struct vdo_page_cache *cache)
{
	struct page_info *info;

	list_for_each_entry(info, &cache->lru_list, lru_entry)
		if ((info->busy == 0) && !is_in_flight(info))
			return info;

	return NULL;
}

/* ASYNCHRONOUS INTERFACE BEYOND THIS POINT */

/**
 * complete_with_page() - Helper to complete the VDO Page Completion request successfully.
 * @info: The page info representing the result page.
 * @vdo_page_comp: The VDO page completion to complete.
 */
static void complete_with_page(struct page_info *info,
			       struct vdo_page_completion *vdo_page_comp)
{
	bool available = vdo_page_comp->writable ? is_present(info) : is_valid(info);

	if (!available) {
		vdo_log_error_strerror(VDO_BAD_PAGE,
				       "Requested cache page %llu in state %s is not %s",
				       (unsigned long long) info->pbn,
				       get_page_state_name(info->state),
				       vdo_page_comp->writable ? "present" : "valid");
		vdo_fail_completion(&vdo_page_comp->completion, VDO_BAD_PAGE);
		return;
	}

	vdo_page_comp->info = info;
	vdo_page_comp->ready = true;
	vdo_finish_completion(&vdo_page_comp->completion);
}

/**
 * complete_waiter_with_error() - Complete a page completion with an error code.
 * @waiter: The page completion, as a waiter.
 * @result_ptr: A pointer to the error code.
 *
 * Implements waiter_callback_fn.
 */
static void complete_waiter_with_error(struct vdo_waiter *waiter, void *result_ptr)
{
	int *result = result_ptr;

	vdo_fail_completion(&page_completion_from_waiter(waiter)->completion, *result);
}

/**
 * complete_waiter_with_page() - Complete a page completion with a page.
 * @waiter: The page completion, as a waiter.
 * @page_info: The page info to complete with.
 *
 * Implements waiter_callback_fn.
 */
static void complete_waiter_with_page(struct vdo_waiter *waiter, void *page_info)
{
	complete_with_page(page_info, page_completion_from_waiter(waiter));
}

/**
 * distribute_page_over_waitq() - Complete a waitq of VDO page completions with a page result.
 *
 * Upon completion the waitq will be empty.
 *
 * Return: The number of pages distributed.
 */
static unsigned int distribute_page_over_waitq(struct page_info *info,
					       struct vdo_wait_queue *waitq)
{
	size_t num_pages;

	update_lru(info);
	num_pages = vdo_waitq_num_waiters(waitq);

	/*
	 * Increment the busy count once for each pending completion so that this page does not
	 * stop being busy until all completions have been processed.
	 */
	info->busy += num_pages;

	vdo_waitq_notify_all_waiters(waitq, complete_waiter_with_page, info);
	return num_pages;
}

/**
 * set_persistent_error() - Set a persistent error which all requests will receive in the future.
 * @context: A string describing what triggered the error.
 *
 * Once triggered, all enqueued completions will get this error. Any future requests will result in
 * this error as well.
 */
static void set_persistent_error(struct vdo_page_cache *cache, const char *context,
				 int result)
{
	struct page_info *info;
	/* If we're already read-only, there's no need to log. */
	struct vdo *vdo = cache->vdo;

	if ((result != VDO_READ_ONLY) && !vdo_is_read_only(vdo)) {
		vdo_log_error_strerror(result, "VDO Page Cache persistent error: %s",
				       context);
		vdo_enter_read_only_mode(vdo, result);
	}

	assert_on_cache_thread(cache, __func__);

	vdo_waitq_notify_all_waiters(&cache->free_waiters,
				     complete_waiter_with_error, &result);
	cache->waiter_count = 0;

	for (info = cache->infos; info < cache->infos + cache->page_count; info++) {
		vdo_waitq_notify_all_waiters(&info->waiting,
					     complete_waiter_with_error, &result);
	}
}

/**
 * validate_completed_page() - Check that a page completion which is being freed to the cache
 *                             referred to a valid page and is in a valid state.
 * @writable: Whether a writable page is required.
 *
 * Return: VDO_SUCCESS if the page was valid, otherwise as error
 */
static int __must_check validate_completed_page(struct vdo_page_completion *completion,
						bool writable)
{
	int result;

	result = VDO_ASSERT(completion->ready, "VDO Page completion not ready");
	if (result != VDO_SUCCESS)
		return result;

	result = VDO_ASSERT(completion->info != NULL,
			    "VDO Page Completion must be complete");
	if (result != VDO_SUCCESS)
		return result;

	result = VDO_ASSERT(completion->info->pbn == completion->pbn,
			    "VDO Page Completion pbn must be consistent");
	if (result != VDO_SUCCESS)
		return result;

	result = VDO_ASSERT(is_valid(completion->info),
			    "VDO Page Completion page must be valid");
	if (result != VDO_SUCCESS)
		return result;

	if (writable) {
		result = VDO_ASSERT(completion->writable,
				    "VDO Page Completion must be writable");
		if (result != VDO_SUCCESS)
			return result;
	}

	return VDO_SUCCESS;
}

static void check_for_drain_complete(struct block_map_zone *zone)
{
	if (vdo_is_state_draining(&zone->state) &&
	    (zone->active_lookups == 0) &&
	    !vdo_waitq_has_waiters(&zone->flush_waiters) &&
	    !is_vio_pool_busy(zone->vio_pool) &&
	    (zone->page_cache.outstanding_reads == 0) &&
	    (zone->page_cache.outstanding_writes == 0)) {
		vdo_finish_draining_with_result(&zone->state,
						(vdo_is_read_only(zone->block_map->vdo) ?
						 VDO_READ_ONLY : VDO_SUCCESS));
	}
}

static void enter_zone_read_only_mode(struct block_map_zone *zone, int result)
{
	vdo_enter_read_only_mode(zone->block_map->vdo, result);

	/*
	 * We are in read-only mode, so we won't ever write any page out.
	 * Just take all waiters off the waitq so the zone can drain.
	 */
	vdo_waitq_init(&zone->flush_waiters);
	check_for_drain_complete(zone);
}

static bool __must_check
validate_completed_page_or_enter_read_only_mode(struct vdo_page_completion *completion,
						bool writable)
{
	int result = validate_completed_page(completion, writable);

	if (result == VDO_SUCCESS)
		return true;

	enter_zone_read_only_mode(completion->info->cache->zone, result);
	return false;
}

/**
 * handle_load_error() - Handle page load errors.
 * @completion: The page read vio.
 */
static void handle_load_error(struct vdo_completion *completion)
{
	int result = completion->result;
	struct page_info *info = completion->parent;
	struct vdo_page_cache *cache = info->cache;

	assert_on_cache_thread(cache, __func__);
	vio_record_metadata_io_error(as_vio(completion));
	vdo_enter_read_only_mode(cache->zone->block_map->vdo, result);
	ADD_ONCE(cache->stats.failed_reads, 1);
	set_info_state(info, PS_FAILED);
	vdo_waitq_notify_all_waiters(&info->waiting, complete_waiter_with_error, &result);
	reset_page_info(info);

	/*
	 * Don't decrement until right before calling check_for_drain_complete() to
	 * ensure that the above work can't cause the page cache to be freed out from under us.
	 */
	cache->outstanding_reads--;
	check_for_drain_complete(cache->zone);
}

/**
 * page_is_loaded() - Callback used when a page has been loaded.
 * @completion: The vio which has loaded the page. Its parent is the page_info.
 */
static void page_is_loaded(struct vdo_completion *completion)
{
	struct page_info *info = completion->parent;
	struct vdo_page_cache *cache = info->cache;
	nonce_t nonce = info->cache->zone->block_map->nonce;
	struct block_map_page *page;
	enum block_map_page_validity validity;

	assert_on_cache_thread(cache, __func__);

	page = (struct block_map_page *) get_page_buffer(info);
	validity = vdo_validate_block_map_page(page, nonce, info->pbn);
	if (validity == VDO_BLOCK_MAP_PAGE_BAD) {
		physical_block_number_t pbn = vdo_get_block_map_page_pbn(page);
		int result = vdo_log_error_strerror(VDO_BAD_PAGE,
						    "Expected page %llu but got page %llu instead",
						    (unsigned long long) info->pbn,
						    (unsigned long long) pbn);

		vdo_continue_completion(completion, result);
		return;
	}

	if (validity == VDO_BLOCK_MAP_PAGE_INVALID)
		vdo_format_block_map_page(page, nonce, info->pbn, false);

	info->recovery_lock = 0;
	set_info_state(info, PS_RESIDENT);
	distribute_page_over_waitq(info, &info->waiting);

	/*
	 * Don't decrement until right before calling check_for_drain_complete() to
	 * ensure that the above work can't cause the page cache to be freed out from under us.
	 */
	cache->outstanding_reads--;
	check_for_drain_complete(cache->zone);
}

/**
 * handle_rebuild_read_error() - Handle a read error during a read-only rebuild.
 * @completion: The page load completion.
 */
static void handle_rebuild_read_error(struct vdo_completion *completion)
{
	struct page_info *info = completion->parent;
	struct vdo_page_cache *cache = info->cache;

	assert_on_cache_thread(cache, __func__);

	/*
	 * We are doing a read-only rebuild, so treat this as a successful read
	 * of an uninitialized page.
	 */
	vio_record_metadata_io_error(as_vio(completion));
	ADD_ONCE(cache->stats.failed_reads, 1);
	memset(get_page_buffer(info), 0, VDO_BLOCK_SIZE);
	vdo_reset_completion(completion);
	page_is_loaded(completion);
}

static void load_cache_page_endio(struct bio *bio)
{
	struct vio *vio = bio->bi_private;
	struct page_info *info = vio->completion.parent;

	continue_vio_after_io(vio, page_is_loaded, info->cache->zone->thread_id);
}

/**
 * launch_page_load() - Begin the process of loading a page.
 *
 * Return: VDO_SUCCESS or an error code.
 */
static int __must_check launch_page_load(struct page_info *info,
					 physical_block_number_t pbn)
{
	int result;
	vdo_action_fn callback;
	struct vdo_page_cache *cache = info->cache;

	assert_io_allowed(cache);

	result = set_info_pbn(info, pbn);
	if (result != VDO_SUCCESS)
		return result;

	result = VDO_ASSERT((info->busy == 0), "Page is not busy before loading.");
	if (result != VDO_SUCCESS)
		return result;

	set_info_state(info, PS_INCOMING);
	cache->outstanding_reads++;
	ADD_ONCE(cache->stats.pages_loaded, 1);
	callback = (cache->rebuilding ? handle_rebuild_read_error : handle_load_error);
	vdo_submit_metadata_vio(info->vio, pbn, load_cache_page_endio,
				callback, REQ_OP_READ | REQ_PRIO);
	return VDO_SUCCESS;
}

static void write_pages(struct vdo_completion *completion);

/** handle_flush_error() - Handle errors flushing the layer. */
static void handle_flush_error(struct vdo_completion *completion)
{
	struct page_info *info = completion->parent;

	vio_record_metadata_io_error(as_vio(completion));
	set_persistent_error(info->cache, "flush failed", completion->result);
	write_pages(completion);
}

static void flush_endio(struct bio *bio)
{
	struct vio *vio = bio->bi_private;
	struct page_info *info = vio->completion.parent;

	continue_vio_after_io(vio, write_pages, info->cache->zone->thread_id);
}

/** save_pages() - Attempt to save the outgoing pages by first flushing the layer. */
static void save_pages(struct vdo_page_cache *cache)
{
	struct page_info *info;
	struct vio *vio;

	if ((cache->pages_in_flush > 0) || (cache->pages_to_flush == 0))
		return;

	assert_io_allowed(cache);

	info = list_first_entry(&cache->outgoing_list, struct page_info, state_entry);

	cache->pages_in_flush = cache->pages_to_flush;
	cache->pages_to_flush = 0;
	ADD_ONCE(cache->stats.flush_count, 1);

	vio = info->vio;

	/*
	 * We must make sure that the recovery journal entries that changed these pages were
	 * successfully persisted, and thus must issue a flush before each batch of pages is
	 * written to ensure this.
	 */
	vdo_submit_flush_vio(vio, flush_endio, handle_flush_error);
}

/**
 * schedule_page_save() - Add a page to the outgoing list of pages waiting to be saved.
 *
 * Once in the list, a page may not be used until it has been written out.
 */
static void schedule_page_save(struct page_info *info)
{
	if (info->busy > 0) {
		info->write_status = WRITE_STATUS_DEFERRED;
		return;
	}

	info->cache->pages_to_flush++;
	info->cache->outstanding_writes++;
	set_info_state(info, PS_OUTGOING);
}

/**
 * launch_page_save() - Add a page to outgoing pages waiting to be saved, and then start saving
 * pages if another save is not in progress.
 */
static void launch_page_save(struct page_info *info)
{
	schedule_page_save(info);
	save_pages(info->cache);
}

/**
 * completion_needs_page() - Determine whether a given vdo_page_completion (as a waiter) is
 *                           requesting a given page number.
 * @context: A pointer to the pbn of the desired page.
 *
 * Implements waiter_match_fn.
 *
 * Return: true if the page completion is for the desired page number.
 */
static bool completion_needs_page(struct vdo_waiter *waiter, void *context)
{
	physical_block_number_t *pbn = context;

	return (page_completion_from_waiter(waiter)->pbn == *pbn);
}

/**
 * allocate_free_page() - Allocate a free page to the first completion in the waiting queue, and
 *                        any other completions that match it in page number.
 */
static void allocate_free_page(struct page_info *info)
{
	int result;
	struct vdo_waiter *oldest_waiter;
	physical_block_number_t pbn;
	struct vdo_page_cache *cache = info->cache;

	assert_on_cache_thread(cache, __func__);

	if (!vdo_waitq_has_waiters(&cache->free_waiters)) {
		if (cache->stats.cache_pressure > 0) {
			vdo_log_info("page cache pressure relieved");
			WRITE_ONCE(cache->stats.cache_pressure, 0);
		}

		return;
	}

	result = reset_page_info(info);
	if (result != VDO_SUCCESS) {
		set_persistent_error(cache, "cannot reset page info", result);
		return;
	}

	oldest_waiter = vdo_waitq_get_first_waiter(&cache->free_waiters);
	pbn = page_completion_from_waiter(oldest_waiter)->pbn;

	/*
	 * Remove all entries which match the page number in question and push them onto the page
	 * info's waitq.
	 */
	vdo_waitq_dequeue_matching_waiters(&cache->free_waiters, completion_needs_page,
					   &pbn, &info->waiting);
	cache->waiter_count -= vdo_waitq_num_waiters(&info->waiting);

	result = launch_page_load(info, pbn);
	if (result != VDO_SUCCESS) {
		vdo_waitq_notify_all_waiters(&info->waiting,
					     complete_waiter_with_error, &result);
	}
}

/**
 * discard_a_page() - Begin the process of discarding a page.
 *
 * If no page is discardable, increments a count of deferred frees so that the next release of a
 * page which is no longer busy will kick off another discard cycle. This is an indication that the
 * cache is not big enough.
 *
 * If the selected page is not dirty, immediately allocates the page to the oldest completion
 * waiting for a free page.
 */
static void discard_a_page(struct vdo_page_cache *cache)
{
	struct page_info *info = select_lru_page(cache);

	if (info == NULL) {
		report_cache_pressure(cache);
		return;
	}

	if (!is_dirty(info)) {
		allocate_free_page(info);
		return;
	}

	VDO_ASSERT_LOG_ONLY(!is_in_flight(info),
			    "page selected for discard is not in flight");

	cache->discard_count++;
	info->write_status = WRITE_STATUS_DISCARD;
	launch_page_save(info);
}

/**
 * discard_page_for_completion() - Helper used to trigger a discard so that the completion can get
 *                                 a different page.
 */
static void discard_page_for_completion(struct vdo_page_completion *vdo_page_comp)
{
	struct vdo_page_cache *cache = vdo_page_comp->cache;

	cache->waiter_count++;
	vdo_waitq_enqueue_waiter(&cache->free_waiters, &vdo_page_comp->waiter);
	discard_a_page(cache);
}

/**
 * discard_page_if_needed() - Helper used to trigger a discard if the cache needs another free
 *                            page.
 * @cache: The page cache.
 */
static void discard_page_if_needed(struct vdo_page_cache *cache)
{
	if (cache->waiter_count > cache->discard_count)
		discard_a_page(cache);
}

/**
 * write_has_finished() - Inform the cache that a write has finished (possibly with an error).
 * @info: The info structure for the page whose write just completed.
 *
 * Return: true if the page write was a discard.
 */
static bool write_has_finished(struct page_info *info)
{
	bool was_discard = (info->write_status == WRITE_STATUS_DISCARD);

	assert_on_cache_thread(info->cache, __func__);
	info->cache->outstanding_writes--;

	info->write_status = WRITE_STATUS_NORMAL;
	return was_discard;
}

/**
 * handle_page_write_error() - Handler for page write errors.
 * @completion: The page write vio.
 */
static void handle_page_write_error(struct vdo_completion *completion)
{
	int result = completion->result;
	struct page_info *info = completion->parent;
	struct vdo_page_cache *cache = info->cache;

	vio_record_metadata_io_error(as_vio(completion));

	/* If we're already read-only, write failures are to be expected. */
	if (result != VDO_READ_ONLY) {
		vdo_log_ratelimit(vdo_log_error,
				  "failed to write block map page %llu",
				  (unsigned long long) info->pbn);
	}

	set_info_state(info, PS_DIRTY);
	ADD_ONCE(cache->stats.failed_writes, 1);
	set_persistent_error(cache, "cannot write page", result);

	if (!write_has_finished(info))
		discard_page_if_needed(cache);

	check_for_drain_complete(cache->zone);
}

static void page_is_written_out(struct vdo_completion *completion);

static void write_cache_page_endio(struct bio *bio)
{
	struct vio *vio = bio->bi_private;
	struct page_info *info = vio->completion.parent;

	continue_vio_after_io(vio, page_is_written_out, info->cache->zone->thread_id);
}

/**
 * page_is_written_out() - Callback used when a page has been written out.
 * @completion: The vio which wrote the page. Its parent is a page_info.
 */
static void page_is_written_out(struct vdo_completion *completion)
{
	bool was_discard, reclaimed;
	u32 reclamations;
	struct page_info *info = completion->parent;
	struct vdo_page_cache *cache = info->cache;
	struct block_map_page *page = (struct block_map_page *) get_page_buffer(info);

	if (!page->header.initialized) {
		page->header.initialized = true;
		vdo_submit_metadata_vio(info->vio, info->pbn,
					write_cache_page_endio,
					handle_page_write_error,
					REQ_OP_WRITE | REQ_PRIO | REQ_PREFLUSH);
		return;
	}

	/* Handle journal updates and torn write protection. */
	vdo_release_recovery_journal_block_reference(cache->zone->block_map->journal,
						     info->recovery_lock,
						     VDO_ZONE_TYPE_LOGICAL,
						     cache->zone->zone_number);
	info->recovery_lock = 0;
	was_discard = write_has_finished(info);
	reclaimed = (!was_discard || (info->busy > 0) || vdo_waitq_has_waiters(&info->waiting));

	set_info_state(info, PS_RESIDENT);

	reclamations = distribute_page_over_waitq(info, &info->waiting);
	ADD_ONCE(cache->stats.reclaimed, reclamations);

	if (was_discard)
		cache->discard_count--;

	if (reclaimed)
		discard_page_if_needed(cache);
	else
		allocate_free_page(info);

	check_for_drain_complete(cache->zone);
}

/**
 * write_pages() - Write the batch of pages which were covered by the layer flush which just
 *                 completed.
 * @flush_completion: The flush vio.
 *
 * This callback is registered in save_pages().
 */
static void write_pages(struct vdo_completion *flush_completion)
{
	struct vdo_page_cache *cache = ((struct page_info *) flush_completion->parent)->cache;

	/*
	 * We need to cache these two values on the stack since it is possible for the last
	 * page info to cause the page cache to get freed. Hence once we launch the last page,
	 * it may be unsafe to dereference the cache.
	 */
	bool has_unflushed_pages = (cache->pages_to_flush > 0);
	page_count_t pages_in_flush = cache->pages_in_flush;

	cache->pages_in_flush = 0;
	while (pages_in_flush-- > 0) {
		struct page_info *info =
			list_first_entry(&cache->outgoing_list, struct page_info,
					 state_entry);

		list_del_init(&info->state_entry);
		if (vdo_is_read_only(info->cache->vdo)) {
			struct vdo_completion *completion = &info->vio->completion;

			vdo_reset_completion(completion);
			completion->callback = page_is_written_out;
			completion->error_handler = handle_page_write_error;
			vdo_fail_completion(completion, VDO_READ_ONLY);
			continue;
		}
		ADD_ONCE(info->cache->stats.pages_saved, 1);
		vdo_submit_metadata_vio(info->vio, info->pbn, write_cache_page_endio,
					handle_page_write_error, REQ_OP_WRITE | REQ_PRIO);
	}

	if (has_unflushed_pages) {
		/*
		 * If there are unflushed pages, the cache can't have been freed, so this call is
		 * safe.
		 */
		save_pages(cache);
	}
}

/**
 * vdo_release_page_completion() - Release a VDO Page Completion.
 *
 * The page referenced by this completion (if any) will no longer be held busy by this completion.
 * If a page becomes discardable and there are completions awaiting free pages then a new round of
 * page discarding is started.
 */
void vdo_release_page_completion(struct vdo_completion *completion)
{
	struct page_info *discard_info = NULL;
	struct vdo_page_completion *page_completion = as_vdo_page_completion(completion);
	struct vdo_page_cache *cache;

	if (completion->result == VDO_SUCCESS) {
		if (!validate_completed_page_or_enter_read_only_mode(page_completion, false))
			return;

		if (--page_completion->info->busy == 0)
			discard_info = page_completion->info;
	}

	VDO_ASSERT_LOG_ONLY((page_completion->waiter.next_waiter == NULL),
			    "Page being released after leaving all queues");

	page_completion->info = NULL;
	cache = page_completion->cache;
	assert_on_cache_thread(cache, __func__);

	if (discard_info != NULL) {
		if (discard_info->write_status == WRITE_STATUS_DEFERRED) {
			discard_info->write_status = WRITE_STATUS_NORMAL;
			launch_page_save(discard_info);
		}

		/*
		 * if there are excess requests for pages (that have not already started discards)
		 * we need to discard some page (which may be this one)
		 */
		discard_page_if_needed(cache);
	}
}

/**
 * load_page_for_completion() - Helper function to load a page as described by a VDO Page
 *                              Completion.
 */
static void load_page_for_completion(struct page_info *info,
				     struct vdo_page_completion *vdo_page_comp)
{
	int result;

	vdo_waitq_enqueue_waiter(&info->waiting, &vdo_page_comp->waiter);
	result = launch_page_load(info, vdo_page_comp->pbn);
	if (result != VDO_SUCCESS) {
		vdo_waitq_notify_all_waiters(&info->waiting,
					     complete_waiter_with_error, &result);
	}
}

/**
 * vdo_get_page() - Initialize a page completion and get a block map page.
 * @page_completion: The vdo_page_completion to initialize.
 * @zone: The block map zone of the desired page.
 * @pbn: The absolute physical block of the desired page.
 * @writable: Whether the page can be modified.
 * @parent: The object to notify when the fetch is complete.
 * @callback: The notification callback.
 * @error_handler: The handler for fetch errors.
 * @requeue: Whether we must requeue when notifying the parent.
 *
 * May cause another page to be discarded (potentially writing a dirty page) and the one nominated
 * by the completion to be loaded from disk. When the callback is invoked, the page will be
 * resident in the cache and marked busy. All callers must call vdo_release_page_completion()
 * when they are done with the page to clear the busy mark.
 */
void vdo_get_page(struct vdo_page_completion *page_completion,
		  struct block_map_zone *zone, physical_block_number_t pbn,
		  bool writable, void *parent, vdo_action_fn callback,
		  vdo_action_fn error_handler, bool requeue)
{
	struct vdo_page_cache *cache = &zone->page_cache;
	struct vdo_completion *completion = &page_completion->completion;
	struct page_info *info;

	assert_on_cache_thread(cache, __func__);
	VDO_ASSERT_LOG_ONLY((page_completion->waiter.next_waiter == NULL),
			    "New page completion was not already on a wait queue");

	*page_completion = (struct vdo_page_completion) {
		.pbn = pbn,
		.writable = writable,
		.cache = cache,
	};

	vdo_initialize_completion(completion, cache->vdo, VDO_PAGE_COMPLETION);
	vdo_prepare_completion(completion, callback, error_handler,
			       cache->zone->thread_id, parent);
	completion->requeue = requeue;

	if (page_completion->writable && vdo_is_read_only(cache->vdo)) {
		vdo_fail_completion(completion, VDO_READ_ONLY);
		return;
	}

	if (page_completion->writable)
		ADD_ONCE(cache->stats.write_count, 1);
	else
		ADD_ONCE(cache->stats.read_count, 1);

	info = find_page(cache, page_completion->pbn);
	if (info != NULL) {
		/* The page is in the cache already. */
		if ((info->write_status == WRITE_STATUS_DEFERRED) ||
		    is_incoming(info) ||
		    (is_outgoing(info) && page_completion->writable)) {
			/* The page is unusable until it has finished I/O. */
			ADD_ONCE(cache->stats.wait_for_page, 1);
			vdo_waitq_enqueue_waiter(&info->waiting, &page_completion->waiter);
			return;
		}

		if (is_valid(info)) {
			/* The page is usable. */
			ADD_ONCE(cache->stats.found_in_cache, 1);
			if (!is_present(info))
				ADD_ONCE(cache->stats.read_outgoing, 1);
			update_lru(info);
			info->busy++;
			complete_with_page(info, page_completion);
			return;
		}

		/* Something horrible has gone wrong. */
		VDO_ASSERT_LOG_ONLY(false, "Info found in a usable state.");
	}

	/* The page must be fetched. */
	info = find_free_page(cache);
	if (info != NULL) {
		ADD_ONCE(cache->stats.fetch_required, 1);
		load_page_for_completion(info, page_completion);
		return;
	}

	/* The page must wait for a page to be discarded. */
	ADD_ONCE(cache->stats.discard_required, 1);
	discard_page_for_completion(page_completion);
}

/**
 * vdo_request_page_write() - Request that a VDO page be written out as soon as it is not busy.
 * @completion: The vdo_page_completion containing the page.
 */
void vdo_request_page_write(struct vdo_completion *completion)
{
	struct page_info *info;
	struct vdo_page_completion *vdo_page_comp = as_vdo_page_completion(completion);

	if (!validate_completed_page_or_enter_read_only_mode(vdo_page_comp, true))
		return;

	info = vdo_page_comp->info;
	set_info_state(info, PS_DIRTY);
	launch_page_save(info);
}

/**
 * vdo_get_cached_page() - Get the block map page from a page completion.
 * @completion: A vdo page completion whose callback has been called.
 * @page_ptr: A pointer to hold the page
 *
 * Return: VDO_SUCCESS or an error
 */
int vdo_get_cached_page(struct vdo_completion *completion,
			struct block_map_page **page_ptr)
{
	int result;
	struct vdo_page_completion *vpc;

	vpc = as_vdo_page_completion(completion);
	result = validate_completed_page(vpc, true);
	if (result == VDO_SUCCESS)
		*page_ptr = (struct block_map_page *) get_page_buffer(vpc->info);

	return result;
}

/**
 * vdo_invalidate_page_cache() - Invalidate all entries in the VDO page cache.
 *
 * There must not be any dirty pages in the cache.
 *
 * Return: A success or error code.
 */
int vdo_invalidate_page_cache(struct vdo_page_cache *cache)
{
	struct page_info *info;

	assert_on_cache_thread(cache, __func__);

	/* Make sure we don't throw away any dirty pages. */
	for (info = cache->infos; info < cache->infos + cache->page_count; info++) {
		int result = VDO_ASSERT(!is_dirty(info), "cache must have no dirty pages");

		if (result != VDO_SUCCESS)
			return result;
	}

	/* Reset the page map by re-allocating it. */
	vdo_int_map_free(vdo_forget(cache->page_map));
	return vdo_int_map_create(cache->page_count, &cache->page_map);
}

/**
 * get_tree_page_by_index() - Get the tree page for a given height and page index.
 *
 * Return: The requested page.
 */
static struct tree_page * __must_check get_tree_page_by_index(struct forest *forest,
							      root_count_t root_index,
							      height_t height,
							      page_number_t page_index)
{
	page_number_t offset = 0;
	size_t segment;

	for (segment = 0; segment < forest->segments; segment++) {
		page_number_t border = forest->boundaries[segment].levels[height - 1];

		if (page_index < border) {
			struct block_map_tree *tree = &forest->trees[root_index];

			return &(tree->segments[segment].levels[height - 1][page_index - offset]);
		}

		offset = border;
	}

	return NULL;
}

/* Get the page referred to by the lock's tree slot at its current height. */
static inline struct tree_page *get_tree_page(const struct block_map_zone *zone,
					      const struct tree_lock *lock)
{
	return get_tree_page_by_index(zone->block_map->forest, lock->root_index,
				      lock->height,
				      lock->tree_slots[lock->height].page_index);
}

/** vdo_copy_valid_page() - Validate and copy a buffer to a page. */
bool vdo_copy_valid_page(char *buffer, nonce_t nonce,
			 physical_block_number_t pbn,
			 struct block_map_page *page)
{
	struct block_map_page *loaded = (struct block_map_page *) buffer;
	enum block_map_page_validity validity =
		vdo_validate_block_map_page(loaded, nonce, pbn);

	if (validity == VDO_BLOCK_MAP_PAGE_VALID) {
		memcpy(page, loaded, VDO_BLOCK_SIZE);
		return true;
	}

	if (validity == VDO_BLOCK_MAP_PAGE_BAD) {
		vdo_log_error_strerror(VDO_BAD_PAGE,
				       "Expected page %llu but got page %llu instead",
				       (unsigned long long) pbn,
				       (unsigned long long) vdo_get_block_map_page_pbn(loaded));
	}

	return false;
}

/**
 * in_cyclic_range() - Check whether the given value is between the lower and upper bounds, within
 *                     a cyclic range of values from 0 to (modulus - 1).
 * @lower: The lowest value to accept.
 * @value: The value to check.
 * @upper: The highest value to accept.
 * @modulus: The size of the cyclic space, no more than 2^15.
 *
 * The value and both bounds must be smaller than the modulus.
 *
 * Return: true if the value is in range.
 */
static bool in_cyclic_range(u16 lower, u16 value, u16 upper, u16 modulus)
{
	if (value < lower)
		value += modulus;
	if (upper < lower)
		upper += modulus;
	return (value <= upper);
}

/**
 * is_not_older() - Check whether a generation is strictly older than some other generation in the
 *                  context of a zone's current generation range.
 * @zone: The zone in which to do the comparison.
 * @a: The generation in question.
 * @b: The generation to compare to.
 *
 * Return: true if generation @a is not strictly older than generation @b in the context of @zone
 */
static bool __must_check is_not_older(struct block_map_zone *zone, u8 a, u8 b)
{
	int result;

	result = VDO_ASSERT((in_cyclic_range(zone->oldest_generation, a, zone->generation, 1 << 8) &&
			     in_cyclic_range(zone->oldest_generation, b, zone->generation, 1 << 8)),
			    "generation(s) %u, %u are out of range [%u, %u]",
			    a, b, zone->oldest_generation, zone->generation);
	if (result != VDO_SUCCESS) {
		enter_zone_read_only_mode(zone, result);
		return true;
	}

	return in_cyclic_range(b, a, zone->generation, 1 << 8);
}

static void release_generation(struct block_map_zone *zone, u8 generation)
{
	int result;

	result = VDO_ASSERT((zone->dirty_page_counts[generation] > 0),
			    "dirty page count underflow for generation %u", generation);
	if (result != VDO_SUCCESS) {
		enter_zone_read_only_mode(zone, result);
		return;
	}

	zone->dirty_page_counts[generation]--;
	while ((zone->dirty_page_counts[zone->oldest_generation] == 0) &&
	       (zone->oldest_generation != zone->generation))
		zone->oldest_generation++;
}

static void set_generation(struct block_map_zone *zone, struct tree_page *page,
			   u8 new_generation)
{
	u32 new_count;
	int result;
	bool decrement_old = vdo_waiter_is_waiting(&page->waiter);
	u8 old_generation = page->generation;

	if (decrement_old && (old_generation == new_generation))
		return;

	page->generation = new_generation;
	new_count = ++zone->dirty_page_counts[new_generation];
	result = VDO_ASSERT((new_count != 0), "dirty page count overflow for generation %u",
			    new_generation);
	if (result != VDO_SUCCESS) {
		enter_zone_read_only_mode(zone, result);
		return;
	}

	if (decrement_old)
		release_generation(zone, old_generation);
}

static void write_page(struct tree_page *tree_page, struct pooled_vio *vio);

/* Implements waiter_callback_fn */
static void write_page_callback(struct vdo_waiter *waiter, void *context)
{
	write_page(container_of(waiter, struct tree_page, waiter), context);
}

static void acquire_vio(struct vdo_waiter *waiter, struct block_map_zone *zone)
{
	waiter->callback = write_page_callback;
	acquire_vio_from_pool(zone->vio_pool, waiter);
}

/* Return: true if all possible generations were not already active */
static bool attempt_increment(struct block_map_zone *zone)
{
	u8 generation = zone->generation + 1;

	if (zone->oldest_generation == generation)
		return false;

	zone->generation = generation;
	return true;
}

/* Launches a flush if one is not already in progress. */
static void enqueue_page(struct tree_page *page, struct block_map_zone *zone)
{
	if ((zone->flusher == NULL) && attempt_increment(zone)) {
		zone->flusher = page;
		acquire_vio(&page->waiter, zone);
		return;
	}

	vdo_waitq_enqueue_waiter(&zone->flush_waiters, &page->waiter);
}

static void write_page_if_not_dirtied(struct vdo_waiter *waiter, void *context)
{
	struct tree_page *page = container_of(waiter, struct tree_page, waiter);
	struct write_if_not_dirtied_context *write_context = context;

	if (page->generation == write_context->generation) {
		acquire_vio(waiter, write_context->zone);
		return;
	}

	enqueue_page(page, write_context->zone);
}

static void return_to_pool(struct block_map_zone *zone, struct pooled_vio *vio)
{
	return_vio_to_pool(vio);
	check_for_drain_complete(zone);
}

/* This callback is registered in write_initialized_page(). */
static void finish_page_write(struct vdo_completion *completion)
{
	bool dirty;
	struct vio *vio = as_vio(completion);
	struct pooled_vio *pooled = container_of(vio, struct pooled_vio, vio);
	struct tree_page *page = completion->parent;
	struct block_map_zone *zone = pooled->context;

	vdo_release_recovery_journal_block_reference(zone->block_map->journal,
						     page->writing_recovery_lock,
						     VDO_ZONE_TYPE_LOGICAL,
						     zone->zone_number);

	dirty = (page->writing_generation != page->generation);
	release_generation(zone, page->writing_generation);
	page->writing = false;

	if (zone->flusher == page) {
		struct write_if_not_dirtied_context context = {
			.zone = zone,
			.generation = page->writing_generation,
		};

		vdo_waitq_notify_all_waiters(&zone->flush_waiters,
					     write_page_if_not_dirtied, &context);
		if (dirty && attempt_increment(zone)) {
			write_page(page, pooled);
			return;
		}

		zone->flusher = NULL;
	}

	if (dirty) {
		enqueue_page(page, zone);
	} else if ((zone->flusher == NULL) && vdo_waitq_has_waiters(&zone->flush_waiters) &&
		   attempt_increment(zone)) {
		zone->flusher = container_of(vdo_waitq_dequeue_waiter(&zone->flush_waiters),
					     struct tree_page, waiter);
		write_page(zone->flusher, pooled);
		return;
	}

	return_to_pool(zone, pooled);
}

static void handle_write_error(struct vdo_completion *completion)
{
	int result = completion->result;
	struct vio *vio = as_vio(completion);
	struct pooled_vio *pooled = container_of(vio, struct pooled_vio, vio);
	struct block_map_zone *zone = pooled->context;

	vio_record_metadata_io_error(vio);
	enter_zone_read_only_mode(zone, result);
	return_to_pool(zone, pooled);
}

static void write_page_endio(struct bio *bio);

static void write_initialized_page(struct vdo_completion *completion)
{
	struct vio *vio = as_vio(completion);
	struct pooled_vio *pooled = container_of(vio, struct pooled_vio, vio);
	struct block_map_zone *zone = pooled->context;
	struct tree_page *tree_page = completion->parent;
	struct block_map_page *page = (struct block_map_page *) vio->data;
	blk_opf_t operation = REQ_OP_WRITE | REQ_PRIO;

	/*
	 * Now that we know the page has been written at least once, mark the copy we are writing
	 * as initialized.
	 */
	page->header.initialized = true;

	if (zone->flusher == tree_page)
		operation |= REQ_PREFLUSH;

	vdo_submit_metadata_vio(vio, vdo_get_block_map_page_pbn(page),
				write_page_endio, handle_write_error,
				operation);
}

static void write_page_endio(struct bio *bio)
{
	struct pooled_vio *vio = bio->bi_private;
	struct block_map_zone *zone = vio->context;
	struct block_map_page *page = (struct block_map_page *) vio->vio.data;

	continue_vio_after_io(&vio->vio,
			      (page->header.initialized ?
			       finish_page_write : write_initialized_page),
			      zone->thread_id);
}

static void write_page(struct tree_page *tree_page, struct pooled_vio *vio)
{
	struct vdo_completion *completion = &vio->vio.completion;
	struct block_map_zone *zone = vio->context;
	struct block_map_page *page = vdo_as_block_map_page(tree_page);

	if ((zone->flusher != tree_page) &&
	    is_not_older(zone, tree_page->generation, zone->generation)) {
		/*
		 * This page was re-dirtied after the last flush was issued, hence we need to do
		 * another flush.
		 */
		enqueue_page(tree_page, zone);
		return_to_pool(zone, vio);
		return;
	}

	completion->parent = tree_page;
	memcpy(vio->vio.data, tree_page->page_buffer, VDO_BLOCK_SIZE);
	completion->callback_thread_id = zone->thread_id;

	tree_page->writing = true;
	tree_page->writing_generation = tree_page->generation;
	tree_page->writing_recovery_lock = tree_page->recovery_lock;

	/* Clear this now so that we know this page is not on any dirty list. */
	tree_page->recovery_lock = 0;

	/*
	 * We've already copied the page into the vio which will write it, so if it was not yet
	 * initialized, the first write will indicate that (for torn write protection). It is now
	 * safe to mark it as initialized in memory since if the write fails, the in memory state
	 * will become irrelevant.
	 */
	if (page->header.initialized) {
		write_initialized_page(completion);
		return;
	}

	page->header.initialized = true;
	vdo_submit_metadata_vio(&vio->vio, vdo_get_block_map_page_pbn(page),
				write_page_endio, handle_write_error,
				REQ_OP_WRITE | REQ_PRIO);
}

/* Release a lock on a page which was being loaded or allocated. */
static void release_page_lock(struct data_vio *data_vio, char *what)
{
	struct block_map_zone *zone;
	struct tree_lock *lock_holder;
	struct tree_lock *lock = &data_vio->tree_lock;

	VDO_ASSERT_LOG_ONLY(lock->locked,
			    "release of unlocked block map page %s for key %llu in tree %u",
			    what, (unsigned long long) lock->key, lock->root_index);

	zone = data_vio->logical.zone->block_map_zone;
	lock_holder = vdo_int_map_remove(zone->loading_pages, lock->key);
	VDO_ASSERT_LOG_ONLY((lock_holder == lock),
			    "block map page %s mismatch for key %llu in tree %u",
			    what, (unsigned long long) lock->key, lock->root_index);
	lock->locked = false;
}

static void finish_lookup(struct data_vio *data_vio, int result)
{
	data_vio->tree_lock.height = 0;

	--data_vio->logical.zone->block_map_zone->active_lookups;

	set_data_vio_logical_callback(data_vio, continue_data_vio_with_block_map_slot);
	data_vio->vio.completion.error_handler = handle_data_vio_error;
	continue_data_vio_with_error(data_vio, result);
}

static void abort_lookup_for_waiter(struct vdo_waiter *waiter, void *context)
{
	struct data_vio *data_vio = vdo_waiter_as_data_vio(waiter);
	int result = *((int *) context);

	if (!data_vio->write) {
		if (result == VDO_NO_SPACE)
			result = VDO_SUCCESS;
	} else if (result != VDO_NO_SPACE) {
		result = VDO_READ_ONLY;
	}

	finish_lookup(data_vio, result);
}

static void abort_lookup(struct data_vio *data_vio, int result, char *what)
{
	if (result != VDO_NO_SPACE)
		enter_zone_read_only_mode(data_vio->logical.zone->block_map_zone, result);

	if (data_vio->tree_lock.locked) {
		release_page_lock(data_vio, what);
		vdo_waitq_notify_all_waiters(&data_vio->tree_lock.waiters,
					     abort_lookup_for_waiter,
					     &result);
	}

	finish_lookup(data_vio, result);
}

static void abort_load(struct data_vio *data_vio, int result)
{
	abort_lookup(data_vio, result, "load");
}

static bool __must_check is_invalid_tree_entry(const struct vdo *vdo,
					       const struct data_location *mapping,
					       height_t height)
{
	if (!vdo_is_valid_location(mapping) ||
	    vdo_is_state_compressed(mapping->state) ||
	    (vdo_is_mapped_location(mapping) && (mapping->pbn == VDO_ZERO_BLOCK)))
		return true;

	/* Roots aren't physical data blocks, so we can't check their PBNs. */
	if (height == VDO_BLOCK_MAP_TREE_HEIGHT)
		return false;

	return !vdo_is_physical_data_block(vdo->depot, mapping->pbn);
}

static void load_block_map_page(struct block_map_zone *zone, struct data_vio *data_vio);
static void allocate_block_map_page(struct block_map_zone *zone,
				    struct data_vio *data_vio);

static void continue_with_loaded_page(struct data_vio *data_vio,
				      struct block_map_page *page)
{
	struct tree_lock *lock = &data_vio->tree_lock;
	struct block_map_tree_slot slot = lock->tree_slots[lock->height];
	struct data_location mapping =
		vdo_unpack_block_map_entry(&page->entries[slot.block_map_slot.slot]);

	if (is_invalid_tree_entry(vdo_from_data_vio(data_vio), &mapping, lock->height)) {
		vdo_log_error_strerror(VDO_BAD_MAPPING,
				       "Invalid block map tree PBN: %llu with state %u for page index %u at height %u",
				       (unsigned long long) mapping.pbn, mapping.state,
				       lock->tree_slots[lock->height - 1].page_index,
				       lock->height - 1);
		abort_load(data_vio, VDO_BAD_MAPPING);
		return;
	}

	if (!vdo_is_mapped_location(&mapping)) {
		/* The page we need is unallocated */
		allocate_block_map_page(data_vio->logical.zone->block_map_zone,
					data_vio);
		return;
	}

	lock->tree_slots[lock->height - 1].block_map_slot.pbn = mapping.pbn;
	if (lock->height == 1) {
		finish_lookup(data_vio, VDO_SUCCESS);
		return;
	}

	/* We know what page we need to load next */
	load_block_map_page(data_vio->logical.zone->block_map_zone, data_vio);
}

static void continue_load_for_waiter(struct vdo_waiter *waiter, void *context)
{
	struct data_vio *data_vio = vdo_waiter_as_data_vio(waiter);

	data_vio->tree_lock.height--;
	continue_with_loaded_page(data_vio, context);
}

static void finish_block_map_page_load(struct vdo_completion *completion)
{
	physical_block_number_t pbn;
	struct tree_page *tree_page;
	struct block_map_page *page;
	nonce_t nonce;
	struct vio *vio = as_vio(completion);
	struct pooled_vio *pooled = vio_as_pooled_vio(vio);
	struct data_vio *data_vio = completion->parent;
	struct block_map_zone *zone = pooled->context;
	struct tree_lock *tree_lock = &data_vio->tree_lock;

	tree_lock->height--;
	pbn = tree_lock->tree_slots[tree_lock->height].block_map_slot.pbn;
	tree_page = get_tree_page(zone, tree_lock);
	page = (struct block_map_page *) tree_page->page_buffer;
	nonce = zone->block_map->nonce;

	if (!vdo_copy_valid_page(vio->data, nonce, pbn, page))
		vdo_format_block_map_page(page, nonce, pbn, false);
	return_vio_to_pool(pooled);

	/* Release our claim to the load and wake any waiters */
	release_page_lock(data_vio, "load");
	vdo_waitq_notify_all_waiters(&tree_lock->waiters, continue_load_for_waiter, page);
	continue_with_loaded_page(data_vio, page);
}

static void handle_io_error(struct vdo_completion *completion)
{
	int result = completion->result;
	struct vio *vio = as_vio(completion);
	struct pooled_vio *pooled = container_of(vio, struct pooled_vio, vio);
	struct data_vio *data_vio = completion->parent;

	vio_record_metadata_io_error(vio);
	return_vio_to_pool(pooled);
	abort_load(data_vio, result);
}

static void load_page_endio(struct bio *bio)
{
	struct vio *vio = bio->bi_private;
	struct data_vio *data_vio = vio->completion.parent;

	continue_vio_after_io(vio, finish_block_map_page_load,
			      data_vio->logical.zone->thread_id);
}

static void load_page(struct vdo_waiter *waiter, void *context)
{
	struct pooled_vio *pooled = context;
	struct data_vio *data_vio = vdo_waiter_as_data_vio(waiter);
	struct tree_lock *lock = &data_vio->tree_lock;
	physical_block_number_t pbn = lock->tree_slots[lock->height - 1].block_map_slot.pbn;

	pooled->vio.completion.parent = data_vio;
	vdo_submit_metadata_vio(&pooled->vio, pbn, load_page_endio,
				handle_io_error, REQ_OP_READ | REQ_PRIO);
}

/*
 * If the page is already locked, queue up to wait for the lock to be released. If the lock is
 * acquired, @data_vio->tree_lock.locked will be true.
 */
static int attempt_page_lock(struct block_map_zone *zone, struct data_vio *data_vio)
{
	int result;
	struct tree_lock *lock_holder;
	struct tree_lock *lock = &data_vio->tree_lock;
	height_t height = lock->height;
	struct block_map_tree_slot tree_slot = lock->tree_slots[height];
	union page_key key;

	key.descriptor = (struct page_descriptor) {
		.root_index = lock->root_index,
		.height = height,
		.page_index = tree_slot.page_index,
		.slot = tree_slot.block_map_slot.slot,
	};
	lock->key = key.key;

	result = vdo_int_map_put(zone->loading_pages, lock->key,
				 lock, false, (void **) &lock_holder);
	if (result != VDO_SUCCESS)
		return result;

	if (lock_holder == NULL) {
		/* We got the lock */
		data_vio->tree_lock.locked = true;
		return VDO_SUCCESS;
	}

	/* Someone else is loading or allocating the page we need */
	vdo_waitq_enqueue_waiter(&lock_holder->waiters, &data_vio->waiter);
	return VDO_SUCCESS;
}

/* Load a block map tree page from disk, for the next level in the data vio tree lock. */
static void load_block_map_page(struct block_map_zone *zone, struct data_vio *data_vio)
{
	int result;

	result = attempt_page_lock(zone, data_vio);
	if (result != VDO_SUCCESS) {
		abort_load(data_vio, result);
		return;
	}

	if (data_vio->tree_lock.locked) {
		data_vio->waiter.callback = load_page;
		acquire_vio_from_pool(zone->vio_pool, &data_vio->waiter);
	}
}

static void allocation_failure(struct vdo_completion *completion)
{
	struct data_vio *data_vio = as_data_vio(completion);

	if (vdo_requeue_completion_if_needed(completion,
					     data_vio->logical.zone->thread_id))
		return;

	abort_lookup(data_vio, completion->result, "allocation");
}

static void continue_allocation_for_waiter(struct vdo_waiter *waiter, void *context)
{
	struct data_vio *data_vio = vdo_waiter_as_data_vio(waiter);
	struct tree_lock *tree_lock = &data_vio->tree_lock;
	physical_block_number_t pbn = *((physical_block_number_t *) context);

	tree_lock->height--;
	data_vio->tree_lock.tree_slots[tree_lock->height].block_map_slot.pbn = pbn;

	if (tree_lock->height == 0) {
		finish_lookup(data_vio, VDO_SUCCESS);
		return;
	}

	allocate_block_map_page(data_vio->logical.zone->block_map_zone, data_vio);
}

/** expire_oldest_list() - Expire the oldest list. */
static void expire_oldest_list(struct dirty_lists *dirty_lists)
{
	block_count_t i = dirty_lists->offset++;

	dirty_lists->oldest_period++;
	if (!list_empty(&dirty_lists->eras[i][VDO_TREE_PAGE])) {
		list_splice_tail_init(&dirty_lists->eras[i][VDO_TREE_PAGE],
				      &dirty_lists->expired[VDO_TREE_PAGE]);
	}

	if (!list_empty(&dirty_lists->eras[i][VDO_CACHE_PAGE])) {
		list_splice_tail_init(&dirty_lists->eras[i][VDO_CACHE_PAGE],
				      &dirty_lists->expired[VDO_CACHE_PAGE]);
	}

	if (dirty_lists->offset == dirty_lists->maximum_age)
		dirty_lists->offset = 0;
}


/** update_period() - Update the dirty_lists period if necessary. */
static void update_period(struct dirty_lists *dirty, sequence_number_t period)
{
	while (dirty->next_period <= period) {
		if ((dirty->next_period - dirty->oldest_period) == dirty->maximum_age)
			expire_oldest_list(dirty);
		dirty->next_period++;
	}
}

/** write_expired_elements() - Write out the expired list. */
static void write_expired_elements(struct block_map_zone *zone)
{
	struct tree_page *page, *ttmp;
	struct page_info *info, *ptmp;
	struct list_head *expired;
	u8 generation = zone->generation;

	expired = &zone->dirty_lists->expired[VDO_TREE_PAGE];
	list_for_each_entry_safe(page, ttmp, expired, entry) {
		int result;

		list_del_init(&page->entry);

		result = VDO_ASSERT(!vdo_waiter_is_waiting(&page->waiter),
				    "Newly expired page not already waiting to write");
		if (result != VDO_SUCCESS) {
			enter_zone_read_only_mode(zone, result);
			continue;
		}

		set_generation(zone, page, generation);
		if (!page->writing)
			enqueue_page(page, zone);
	}

	expired = &zone->dirty_lists->expired[VDO_CACHE_PAGE];
	list_for_each_entry_safe(info, ptmp, expired, state_entry) {
		list_del_init(&info->state_entry);
		schedule_page_save(info);
	}

	save_pages(&zone->page_cache);
}

/**
 * add_to_dirty_lists() - Add an element to the dirty lists.
 * @zone: The zone in which we are operating.
 * @entry: The list entry of the element to add.
 * @type: The type of page.
 * @old_period: The period in which the element was previously dirtied, or 0 if it was not dirty.
 * @new_period: The period in which the element has now been dirtied, or 0 if it does not hold a
 *              lock.
 */
static void add_to_dirty_lists(struct block_map_zone *zone,
			       struct list_head *entry,
			       enum block_map_page_type type,
			       sequence_number_t old_period,
			       sequence_number_t new_period)
{
	struct dirty_lists *dirty_lists = zone->dirty_lists;

	if ((old_period == new_period) || ((old_period != 0) && (old_period < new_period)))
		return;

	if (new_period < dirty_lists->oldest_period) {
		list_move_tail(entry, &dirty_lists->expired[type]);
	} else {
		update_period(dirty_lists, new_period);
		list_move_tail(entry,
			       &dirty_lists->eras[new_period % dirty_lists->maximum_age][type]);
	}

	write_expired_elements(zone);
}

/*
 * Record the allocation in the tree and wake any waiters now that the write lock has been
 * released.
 */
static void finish_block_map_allocation(struct vdo_completion *completion)
{
	physical_block_number_t pbn;
	struct tree_page *tree_page;
	struct block_map_page *page;
	sequence_number_t old_lock;
	struct data_vio *data_vio = as_data_vio(completion);
	struct block_map_zone *zone = data_vio->logical.zone->block_map_zone;
	struct tree_lock *tree_lock = &data_vio->tree_lock;
	height_t height = tree_lock->height;

	assert_data_vio_in_logical_zone(data_vio);

	tree_page = get_tree_page(zone, tree_lock);
	pbn = tree_lock->tree_slots[height - 1].block_map_slot.pbn;

	/* Record the allocation. */
	page = (struct block_map_page *) tree_page->page_buffer;
	old_lock = tree_page->recovery_lock;
	vdo_update_block_map_page(page, data_vio, pbn,
				  VDO_MAPPING_STATE_UNCOMPRESSED,
				  &tree_page->recovery_lock);

	if (vdo_waiter_is_waiting(&tree_page->waiter)) {
		/* This page is waiting to be written out. */
		if (zone->flusher != tree_page) {
			/*
			 * The outstanding flush won't cover the update we just made,
			 * so mark the page as needing another flush.
			 */
			set_generation(zone, tree_page, zone->generation);
		}
	} else {
		/* Put the page on a dirty list */
		if (old_lock == 0)
			INIT_LIST_HEAD(&tree_page->entry);
		add_to_dirty_lists(zone, &tree_page->entry, VDO_TREE_PAGE,
				   old_lock, tree_page->recovery_lock);
	}

	tree_lock->height--;
	if (height > 1) {
		/* Format the interior node we just allocated (in memory). */
		tree_page = get_tree_page(zone, tree_lock);
		vdo_format_block_map_page(tree_page->page_buffer,
					  zone->block_map->nonce,
					  pbn, false);
	}

	/* Release our claim to the allocation and wake any waiters */
	release_page_lock(data_vio, "allocation");
	vdo_waitq_notify_all_waiters(&tree_lock->waiters,
				     continue_allocation_for_waiter, &pbn);
	if (tree_lock->height == 0) {
		finish_lookup(data_vio, VDO_SUCCESS);
		return;
	}

	allocate_block_map_page(zone, data_vio);
}

static void release_block_map_write_lock(struct vdo_completion *completion)
{
	struct data_vio *data_vio = as_data_vio(completion);

	assert_data_vio_in_allocated_zone(data_vio);

	release_data_vio_allocation_lock(data_vio, true);
	launch_data_vio_logical_callback(data_vio, finish_block_map_allocation);
}

/*
 * Newly allocated block map pages are set to have to MAXIMUM_REFERENCES after they are journaled,
 * to prevent deduplication against the block after we release the write lock on it, but before we
 * write out the page.
 */
static void set_block_map_page_reference_count(struct vdo_completion *completion)
{
	struct data_vio *data_vio = as_data_vio(completion);

	assert_data_vio_in_allocated_zone(data_vio);

	completion->callback = release_block_map_write_lock;
	vdo_modify_reference_count(completion, &data_vio->increment_updater);
}

static void journal_block_map_allocation(struct vdo_completion *completion)
{
	struct data_vio *data_vio = as_data_vio(completion);

	assert_data_vio_in_journal_zone(data_vio);

	set_data_vio_allocated_zone_callback(data_vio,
					     set_block_map_page_reference_count);
	vdo_add_recovery_journal_entry(completion->vdo->recovery_journal, data_vio);
}

static void allocate_block(struct vdo_completion *completion)
{
	struct data_vio *data_vio = as_data_vio(completion);
	struct tree_lock *lock = &data_vio->tree_lock;
	physical_block_number_t pbn;

	assert_data_vio_in_allocated_zone(data_vio);

	if (!vdo_allocate_block_in_zone(data_vio))
		return;

	pbn = data_vio->allocation.pbn;
	lock->tree_slots[lock->height - 1].block_map_slot.pbn = pbn;
	data_vio->increment_updater = (struct reference_updater) {
		.operation = VDO_JOURNAL_BLOCK_MAP_REMAPPING,
		.increment = true,
		.zpbn = {
			.pbn = pbn,
			.state = VDO_MAPPING_STATE_UNCOMPRESSED,
		},
		.lock = data_vio->allocation.lock,
	};

	launch_data_vio_journal_callback(data_vio, journal_block_map_allocation);
}

static void allocate_block_map_page(struct block_map_zone *zone,
				    struct data_vio *data_vio)
{
	int result;

	if (!data_vio->write || data_vio->is_discard) {
		/* This is a pure read or a discard, so there's nothing left to do here. */
		finish_lookup(data_vio, VDO_SUCCESS);
		return;
	}

	result = attempt_page_lock(zone, data_vio);
	if (result != VDO_SUCCESS) {
		abort_lookup(data_vio, result, "allocation");
		return;
	}

	if (!data_vio->tree_lock.locked)
		return;

	data_vio_allocate_data_block(data_vio, VIO_BLOCK_MAP_WRITE_LOCK,
				     allocate_block, allocation_failure);
}

/**
 * vdo_find_block_map_slot() - Find the block map slot in which the block map entry for a data_vio
 *                             resides and cache that result in the data_vio.
 *
 * All ancestors in the tree will be allocated or loaded, as needed.
 */
void vdo_find_block_map_slot(struct data_vio *data_vio)
{
	page_number_t page_index;
	struct block_map_tree_slot tree_slot;
	struct data_location mapping;
	struct block_map_page *page = NULL;
	struct tree_lock *lock = &data_vio->tree_lock;
	struct block_map_zone *zone = data_vio->logical.zone->block_map_zone;

	zone->active_lookups++;
	if (vdo_is_state_draining(&zone->state)) {
		finish_lookup(data_vio, VDO_SHUTTING_DOWN);
		return;
	}

	lock->tree_slots[0].block_map_slot.slot =
		data_vio->logical.lbn % VDO_BLOCK_MAP_ENTRIES_PER_PAGE;
	page_index = (lock->tree_slots[0].page_index / zone->block_map->root_count);
	tree_slot = (struct block_map_tree_slot) {
		.page_index = page_index / VDO_BLOCK_MAP_ENTRIES_PER_PAGE,
		.block_map_slot = {
			.pbn = 0,
			.slot = page_index % VDO_BLOCK_MAP_ENTRIES_PER_PAGE,
		},
	};

	for (lock->height = 1; lock->height <= VDO_BLOCK_MAP_TREE_HEIGHT; lock->height++) {
		physical_block_number_t pbn;

		lock->tree_slots[lock->height] = tree_slot;
		page = (struct block_map_page *) (get_tree_page(zone, lock)->page_buffer);
		pbn = vdo_get_block_map_page_pbn(page);
		if (pbn != VDO_ZERO_BLOCK) {
			lock->tree_slots[lock->height].block_map_slot.pbn = pbn;
			break;
		}

		/* Calculate the index and slot for the next level. */
		tree_slot.block_map_slot.slot =
			tree_slot.page_index % VDO_BLOCK_MAP_ENTRIES_PER_PAGE;
		tree_slot.page_index = tree_slot.page_index / VDO_BLOCK_MAP_ENTRIES_PER_PAGE;
	}

	/* The page at this height has been allocated and loaded. */
	mapping = vdo_unpack_block_map_entry(&page->entries[tree_slot.block_map_slot.slot]);
	if (is_invalid_tree_entry(vdo_from_data_vio(data_vio), &mapping, lock->height)) {
		vdo_log_error_strerror(VDO_BAD_MAPPING,
				       "Invalid block map tree PBN: %llu with state %u for page index %u at height %u",
				       (unsigned long long) mapping.pbn, mapping.state,
				       lock->tree_slots[lock->height - 1].page_index,
				       lock->height - 1);
		abort_load(data_vio, VDO_BAD_MAPPING);
		return;
	}

	if (!vdo_is_mapped_location(&mapping)) {
		/* The page we want one level down has not been allocated, so allocate it. */
		allocate_block_map_page(zone, data_vio);
		return;
	}

	lock->tree_slots[lock->height - 1].block_map_slot.pbn = mapping.pbn;
	if (lock->height == 1) {
		/* This is the ultimate block map page, so we're done */
		finish_lookup(data_vio, VDO_SUCCESS);
		return;
	}

	/* We know what page we need to load. */
	load_block_map_page(zone, data_vio);
}

/*
 * Find the PBN of a leaf block map page. This method may only be used after all allocated tree
 * pages have been loaded, otherwise, it may give the wrong answer (0).
 */
physical_block_number_t vdo_find_block_map_page_pbn(struct block_map *map,
						    page_number_t page_number)
{
	struct data_location mapping;
	struct tree_page *tree_page;
	struct block_map_page *page;
	root_count_t root_index = page_number % map->root_count;
	page_number_t page_index = page_number / map->root_count;
	slot_number_t slot = page_index % VDO_BLOCK_MAP_ENTRIES_PER_PAGE;

	page_index /= VDO_BLOCK_MAP_ENTRIES_PER_PAGE;

	tree_page = get_tree_page_by_index(map->forest, root_index, 1, page_index);
	page = (struct block_map_page *) tree_page->page_buffer;
	if (!page->header.initialized)
		return VDO_ZERO_BLOCK;

	mapping = vdo_unpack_block_map_entry(&page->entries[slot]);
	if (!vdo_is_valid_location(&mapping) || vdo_is_state_compressed(mapping.state))
		return VDO_ZERO_BLOCK;
	return mapping.pbn;
}

/*
 * Write a tree page or indicate that it has been re-dirtied if it is already being written. This
 * method is used when correcting errors in the tree during read-only rebuild.
 */
void vdo_write_tree_page(struct tree_page *page, struct block_map_zone *zone)
{
	bool waiting = vdo_waiter_is_waiting(&page->waiter);

	if (waiting && (zone->flusher == page))
		return;

	set_generation(zone, page, zone->generation);
	if (waiting || page->writing)
		return;

	enqueue_page(page, zone);
}

static int make_segment(struct forest *old_forest, block_count_t new_pages,
			struct boundary *new_boundary, struct forest *forest)
{
	size_t index = (old_forest == NULL) ? 0 : old_forest->segments;
	struct tree_page *page_ptr;
	page_count_t segment_sizes[VDO_BLOCK_MAP_TREE_HEIGHT];
	height_t height;
	root_count_t root;
	int result;

	forest->segments = index + 1;

	result = vdo_allocate(forest->segments, struct boundary,
			      "forest boundary array", &forest->boundaries);
	if (result != VDO_SUCCESS)
		return result;

	result = vdo_allocate(forest->segments, struct tree_page *,
			      "forest page pointers", &forest->pages);
	if (result != VDO_SUCCESS)
		return result;

	result = vdo_allocate(new_pages, struct tree_page,
			      "new forest pages", &forest->pages[index]);
	if (result != VDO_SUCCESS)
		return result;

	if (index > 0) {
		memcpy(forest->boundaries, old_forest->boundaries,
		       index * sizeof(struct boundary));
		memcpy(forest->pages, old_forest->pages,
		       index * sizeof(struct tree_page *));
	}

	memcpy(&(forest->boundaries[index]), new_boundary, sizeof(struct boundary));

	for (height = 0; height < VDO_BLOCK_MAP_TREE_HEIGHT; height++) {
		segment_sizes[height] = new_boundary->levels[height];
		if (index > 0)
			segment_sizes[height] -= old_forest->boundaries[index - 1].levels[height];
	}

	page_ptr = forest->pages[index];
	for (root = 0; root < forest->map->root_count; root++) {
		struct block_map_tree_segment *segment;
		struct block_map_tree *tree = &(forest->trees[root]);
		height_t height;

		int result = vdo_allocate(forest->segments,
					  struct block_map_tree_segment,
					  "tree root segments", &tree->segments);
		if (result != VDO_SUCCESS)
			return result;

		if (index > 0) {
			memcpy(tree->segments, old_forest->trees[root].segments,
			       index * sizeof(struct block_map_tree_segment));
		}

		segment = &(tree->segments[index]);
		for (height = 0; height < VDO_BLOCK_MAP_TREE_HEIGHT; height++) {
			if (segment_sizes[height] == 0)
				continue;

			segment->levels[height] = page_ptr;
			if (height == (VDO_BLOCK_MAP_TREE_HEIGHT - 1)) {
				/* Record the root. */
				struct block_map_page *page =
					vdo_format_block_map_page(page_ptr->page_buffer,
								  forest->map->nonce,
								  VDO_INVALID_PBN, true);
				page->entries[0] =
					vdo_pack_block_map_entry(forest->map->root_origin + root,
								 VDO_MAPPING_STATE_UNCOMPRESSED);
			}
			page_ptr += segment_sizes[height];
		}
	}

	return VDO_SUCCESS;
}

static void deforest(struct forest *forest, size_t first_page_segment)
{
	root_count_t root;

	if (forest->pages != NULL) {
		size_t segment;

		for (segment = first_page_segment; segment < forest->segments; segment++)
			vdo_free(forest->pages[segment]);
		vdo_free(forest->pages);
	}

	for (root = 0; root < forest->map->root_count; root++)
		vdo_free(forest->trees[root].segments);

	vdo_free(forest->boundaries);
	vdo_free(forest);
}

/**
 * make_forest() - Make a collection of trees for a block_map, expanding the existing forest if
 *                 there is one.
 * @entries: The number of entries the block map will hold.
 *
 * Return: VDO_SUCCESS or an error.
 */
static int make_forest(struct block_map *map, block_count_t entries)
{
	struct forest *forest, *old_forest = map->forest;
	struct boundary new_boundary, *old_boundary = NULL;
	block_count_t new_pages;
	int result;

	if (old_forest != NULL)
		old_boundary = &(old_forest->boundaries[old_forest->segments - 1]);

	new_pages = vdo_compute_new_forest_pages(map->root_count, old_boundary,
						 entries, &new_boundary);
	if (new_pages == 0) {
		map->next_entry_count = entries;
		return VDO_SUCCESS;
	}

	result = vdo_allocate_extended(struct forest, map->root_count,
				       struct block_map_tree, __func__,
				       &forest);
	if (result != VDO_SUCCESS)
		return result;

	forest->map = map;
	result = make_segment(old_forest, new_pages, &new_boundary, forest);
	if (result != VDO_SUCCESS) {
		deforest(forest, forest->segments - 1);
		return result;
	}

	map->next_forest = forest;
	map->next_entry_count = entries;
	return VDO_SUCCESS;
}

/**
 * replace_forest() - Replace a block_map's forest with the already-prepared larger forest.
 */
static void replace_forest(struct block_map *map)
{
	if (map->next_forest != NULL) {
		if (map->forest != NULL)
			deforest(map->forest, map->forest->segments);
		map->forest = vdo_forget(map->next_forest);
	}

	map->entry_count = map->next_entry_count;
	map->next_entry_count = 0;
}

/**
 * finish_cursor() - Finish the traversal of a single tree. If it was the last cursor, finish the
 *                   traversal.
 */
static void finish_cursor(struct cursor *cursor)
{
	struct cursors *cursors = cursor->parent;
	struct vdo_completion *completion = cursors->completion;

	return_vio_to_pool(vdo_forget(cursor->vio));
	if (--cursors->active_roots > 0)
		return;

	vdo_free(cursors);

	vdo_finish_completion(completion);
}

static void traverse(struct cursor *cursor);

/**
 * continue_traversal() - Continue traversing a block map tree.
 * @completion: The VIO doing a read or write.
 */
static void continue_traversal(struct vdo_completion *completion)
{
	vio_record_metadata_io_error(as_vio(completion));
	traverse(completion->parent);
}

/**
 * finish_traversal_load() - Continue traversing a block map tree now that a page has been loaded.
 * @completion: The VIO doing the read.
 */
static void finish_traversal_load(struct vdo_completion *completion)
{
	struct cursor *cursor = completion->parent;
	height_t height = cursor->height;
	struct cursor_level *level = &cursor->levels[height];
	struct tree_page *tree_page =
		&(cursor->tree->segments[0].levels[height][level->page_index]);
	struct block_map_page *page = (struct block_map_page *) tree_page->page_buffer;

	vdo_copy_valid_page(cursor->vio->vio.data,
			    cursor->parent->zone->block_map->nonce,
			    pbn_from_vio_bio(cursor->vio->vio.bio), page);
	traverse(cursor);
}

static void traversal_endio(struct bio *bio)
{
	struct vio *vio = bio->bi_private;
	struct cursor *cursor = vio->completion.parent;

	continue_vio_after_io(vio, finish_traversal_load,
			      cursor->parent->zone->thread_id);
}

/**
 * traverse() - Traverse a single block map tree.
 *
 * This is the recursive heart of the traversal process.
 */
static void traverse(struct cursor *cursor)
{
	for (; cursor->height < VDO_BLOCK_MAP_TREE_HEIGHT; cursor->height++) {
		height_t height = cursor->height;
		struct cursor_level *level = &cursor->levels[height];
		struct tree_page *tree_page =
			&(cursor->tree->segments[0].levels[height][level->page_index]);
		struct block_map_page *page = (struct block_map_page *) tree_page->page_buffer;

		if (!page->header.initialized)
			continue;

		for (; level->slot < VDO_BLOCK_MAP_ENTRIES_PER_PAGE; level->slot++) {
			struct cursor_level *next_level;
			page_number_t entry_index =
				(VDO_BLOCK_MAP_ENTRIES_PER_PAGE * level->page_index) + level->slot;
			struct data_location location =
				vdo_unpack_block_map_entry(&page->entries[level->slot]);

			if (!vdo_is_valid_location(&location)) {
				/* This entry is invalid, so remove it from the page. */
				page->entries[level->slot] = UNMAPPED_BLOCK_MAP_ENTRY;
				vdo_write_tree_page(tree_page, cursor->parent->zone);
				continue;
			}

			if (!vdo_is_mapped_location(&location))
				continue;

			/* Erase mapped entries past the end of the logical space. */
			if (entry_index >= cursor->boundary.levels[height]) {
				page->entries[level->slot] = UNMAPPED_BLOCK_MAP_ENTRY;
				vdo_write_tree_page(tree_page, cursor->parent->zone);
				continue;
			}

			if (cursor->height < VDO_BLOCK_MAP_TREE_HEIGHT - 1) {
				int result = cursor->parent->entry_callback(location.pbn,
									    cursor->parent->completion);
				if (result != VDO_SUCCESS) {
					page->entries[level->slot] = UNMAPPED_BLOCK_MAP_ENTRY;
					vdo_write_tree_page(tree_page, cursor->parent->zone);
					continue;
				}
			}

			if (cursor->height == 0)
				continue;

			cursor->height--;
			next_level = &cursor->levels[cursor->height];
			next_level->page_index = entry_index;
			next_level->slot = 0;
			level->slot++;
			vdo_submit_metadata_vio(&cursor->vio->vio, location.pbn,
						traversal_endio, continue_traversal,
						REQ_OP_READ | REQ_PRIO);
			return;
		}
	}

	finish_cursor(cursor);
}

/**
 * launch_cursor() - Start traversing a single block map tree now that the cursor has a VIO with
 *                   which to load pages.
 * @context: The pooled_vio just acquired.
 *
 * Implements waiter_callback_fn.
 */
static void launch_cursor(struct vdo_waiter *waiter, void *context)
{
	struct cursor *cursor = container_of(waiter, struct cursor, waiter);
	struct pooled_vio *pooled = context;

	cursor->vio = pooled;
	pooled->vio.completion.parent = cursor;
	pooled->vio.completion.callback_thread_id = cursor->parent->zone->thread_id;
	traverse(cursor);
}

/**
 * compute_boundary() - Compute the number of pages used at each level of the given root's tree.
 *
 * Return: The list of page counts as a boundary structure.
 */
static struct boundary compute_boundary(struct block_map *map, root_count_t root_index)
{
	struct boundary boundary;
	height_t height;
	page_count_t leaf_pages = vdo_compute_block_map_page_count(map->entry_count);
	/*
	 * Compute the leaf pages for this root. If the number of leaf pages does not distribute
	 * evenly, we must determine if this root gets an extra page. Extra pages are assigned to
	 * roots starting from tree 0.
	 */
	page_count_t last_tree_root = (leaf_pages - 1) % map->root_count;
	page_count_t level_pages = leaf_pages / map->root_count;

	if (root_index <= last_tree_root)
		level_pages++;

	for (height = 0; height < VDO_BLOCK_MAP_TREE_HEIGHT - 1; height++) {
		boundary.levels[height] = level_pages;
		level_pages = DIV_ROUND_UP(level_pages, VDO_BLOCK_MAP_ENTRIES_PER_PAGE);
	}

	/* The root node always exists, even if the root is otherwise unused. */
	boundary.levels[VDO_BLOCK_MAP_TREE_HEIGHT - 1] = 1;

	return boundary;
}

/**
 * vdo_traverse_forest() - Walk the entire forest of a block map.
 * @callback: A function to call with the pbn of each allocated node in the forest.
 * @completion: The completion to notify on each traversed PBN, and when traversal completes.
 */
void vdo_traverse_forest(struct block_map *map, vdo_entry_callback_fn callback,
			 struct vdo_completion *completion)
{
	root_count_t root;
	struct cursors *cursors;
	int result;

	result = vdo_allocate_extended(struct cursors, map->root_count,
				       struct cursor, __func__, &cursors);
	if (result != VDO_SUCCESS) {
		vdo_fail_completion(completion, result);
		return;
	}

	cursors->zone = &map->zones[0];
	cursors->pool = cursors->zone->vio_pool;
	cursors->entry_callback = callback;
	cursors->completion = completion;
	cursors->active_roots = map->root_count;
	for (root = 0; root < map->root_count; root++) {
		struct cursor *cursor = &cursors->cursors[root];

		*cursor = (struct cursor) {
			.tree = &map->forest->trees[root],
			.height = VDO_BLOCK_MAP_TREE_HEIGHT - 1,
			.parent = cursors,
			.boundary = compute_boundary(map, root),
		};

		cursor->waiter.callback = launch_cursor;
		acquire_vio_from_pool(cursors->pool, &cursor->waiter);
	}
}

/**
 * initialize_block_map_zone() - Initialize the per-zone portions of the block map.
 * @maximum_age: The number of journal blocks before a dirtied page is considered old and must be
 *               written out.
 */
static int __must_check initialize_block_map_zone(struct block_map *map,
						  zone_count_t zone_number,
						  page_count_t cache_size,
						  block_count_t maximum_age)
{
	int result;
	block_count_t i;
	struct vdo *vdo = map->vdo;
	struct block_map_zone *zone = &map->zones[zone_number];

	BUILD_BUG_ON(sizeof(struct page_descriptor) != sizeof(u64));

	zone->zone_number = zone_number;
	zone->thread_id = vdo->thread_config.logical_threads[zone_number];
	zone->block_map = map;

	result = vdo_allocate_extended(struct dirty_lists, maximum_age,
				       dirty_era_t, __func__,
				       &zone->dirty_lists);
	if (result != VDO_SUCCESS)
		return result;

	zone->dirty_lists->maximum_age = maximum_age;
	INIT_LIST_HEAD(&zone->dirty_lists->expired[VDO_TREE_PAGE]);
	INIT_LIST_HEAD(&zone->dirty_lists->expired[VDO_CACHE_PAGE]);

	for (i = 0; i < maximum_age; i++) {
		INIT_LIST_HEAD(&zone->dirty_lists->eras[i][VDO_TREE_PAGE]);
		INIT_LIST_HEAD(&zone->dirty_lists->eras[i][VDO_CACHE_PAGE]);
	}

	result = vdo_int_map_create(VDO_LOCK_MAP_CAPACITY, &zone->loading_pages);
	if (result != VDO_SUCCESS)
		return result;

	result = make_vio_pool(vdo, BLOCK_MAP_VIO_POOL_SIZE, 1,
			       zone->thread_id, VIO_TYPE_BLOCK_MAP_INTERIOR,
			       VIO_PRIORITY_METADATA, zone, &zone->vio_pool);
	if (result != VDO_SUCCESS)
		return result;

	vdo_set_admin_state_code(&zone->state, VDO_ADMIN_STATE_NORMAL_OPERATION);

	zone->page_cache.zone = zone;
	zone->page_cache.vdo = vdo;
	zone->page_cache.page_count = cache_size / map->zone_count;
	zone->page_cache.stats.free_pages = zone->page_cache.page_count;

	result = allocate_cache_components(&zone->page_cache);
	if (result != VDO_SUCCESS)
		return result;

	/* initialize empty circular queues */
	INIT_LIST_HEAD(&zone->page_cache.lru_list);
	INIT_LIST_HEAD(&zone->page_cache.outgoing_list);

	return VDO_SUCCESS;
}

/* Implements vdo_zone_thread_getter_fn */
static thread_id_t get_block_map_zone_thread_id(void *context, zone_count_t zone_number)
{
	struct block_map *map = context;

	return map->zones[zone_number].thread_id;
}

/* Implements vdo_action_preamble_fn */
static void prepare_for_era_advance(void *context, struct vdo_completion *parent)
{
	struct block_map *map = context;

	map->current_era_point = map->pending_era_point;
	vdo_finish_completion(parent);
}

/* Implements vdo_zone_action_fn */
static void advance_block_map_zone_era(void *context, zone_count_t zone_number,
				       struct vdo_completion *parent)
{
	struct block_map *map = context;
	struct block_map_zone *zone = &map->zones[zone_number];

	update_period(zone->dirty_lists, map->current_era_point);
	write_expired_elements(zone);
	vdo_finish_completion(parent);
}

/*
 * Schedule an era advance if necessary. This method should not be called directly. Rather, call
 * vdo_schedule_default_action() on the block map's action manager.
 *
 * Implements vdo_action_scheduler_fn.
 */
static bool schedule_era_advance(void *context)
{
	struct block_map *map = context;

	if (map->current_era_point == map->pending_era_point)
		return false;

	return vdo_schedule_action(map->action_manager, prepare_for_era_advance,
				   advance_block_map_zone_era, NULL, NULL);
}

static void uninitialize_block_map_zone(struct block_map_zone *zone)
{
	struct vdo_page_cache *cache = &zone->page_cache;

	vdo_free(vdo_forget(zone->dirty_lists));
	free_vio_pool(vdo_forget(zone->vio_pool));
	vdo_int_map_free(vdo_forget(zone->loading_pages));
	if (cache->infos != NULL) {
		struct page_info *info;

		for (info = cache->infos; info < cache->infos + cache->page_count; info++)
			free_vio(vdo_forget(info->vio));
	}

	vdo_int_map_free(vdo_forget(cache->page_map));
	vdo_free(vdo_forget(cache->infos));
	vdo_free(vdo_forget(cache->pages));
}

void vdo_free_block_map(struct block_map *map)
{
	zone_count_t zone;

	if (map == NULL)
		return;

	for (zone = 0; zone < map->zone_count; zone++)
		uninitialize_block_map_zone(&map->zones[zone]);

	vdo_abandon_block_map_growth(map);
	if (map->forest != NULL)
		deforest(vdo_forget(map->forest), 0);
	vdo_free(vdo_forget(map->action_manager));
	vdo_free(map);
}

/* @journal may be NULL. */
int vdo_decode_block_map(struct block_map_state_2_0 state, block_count_t logical_blocks,
			 struct vdo *vdo, struct recovery_journal *journal,
			 nonce_t nonce, page_count_t cache_size, block_count_t maximum_age,
			 struct block_map **map_ptr)
{
	struct block_map *map;
	int result;
	zone_count_t zone = 0;

	BUILD_BUG_ON(VDO_BLOCK_MAP_ENTRIES_PER_PAGE !=
		     ((VDO_BLOCK_SIZE - sizeof(struct block_map_page)) /
		      sizeof(struct block_map_entry)));
	result = VDO_ASSERT(cache_size > 0, "block map cache size is specified");
	if (result != VDO_SUCCESS)
		return result;

	result = vdo_allocate_extended(struct block_map,
				       vdo->thread_config.logical_zone_count,
				       struct block_map_zone, __func__, &map);
	if (result != VDO_SUCCESS)
		return result;

	map->vdo = vdo;
	map->root_origin = state.root_origin;
	map->root_count = state.root_count;
	map->entry_count = logical_blocks;
	map->journal = journal;
	map->nonce = nonce;

	result = make_forest(map, map->entry_count);
	if (result != VDO_SUCCESS) {
		vdo_free_block_map(map);
		return result;
	}

	replace_forest(map);

	map->zone_count = vdo->thread_config.logical_zone_count;
	for (zone = 0; zone < map->zone_count; zone++) {
		result = initialize_block_map_zone(map, zone, cache_size, maximum_age);
		if (result != VDO_SUCCESS) {
			vdo_free_block_map(map);
			return result;
		}
	}

	result = vdo_make_action_manager(map->zone_count, get_block_map_zone_thread_id,
					 vdo_get_recovery_journal_thread_id(journal),
					 map, schedule_era_advance, vdo,
					 &map->action_manager);
	if (result != VDO_SUCCESS) {
		vdo_free_block_map(map);
		return result;
	}

	*map_ptr = map;
	return VDO_SUCCESS;
}

struct block_map_state_2_0 vdo_record_block_map(const struct block_map *map)
{
	return (struct block_map_state_2_0) {
		.flat_page_origin = VDO_BLOCK_MAP_FLAT_PAGE_ORIGIN,
		/* This is the flat page count, which has turned out to always be 0. */
		.flat_page_count = 0,
		.root_origin = map->root_origin,
		.root_count = map->root_count,
	};
}

/* The block map needs to know the journals' sequence number to initialize the eras. */
void vdo_initialize_block_map_from_journal(struct block_map *map,
					   struct recovery_journal *journal)
{
	zone_count_t z = 0;

	map->current_era_point = vdo_get_recovery_journal_current_sequence_number(journal);
	map->pending_era_point = map->current_era_point;

	for (z = 0; z < map->zone_count; z++) {
		struct dirty_lists *dirty_lists = map->zones[z].dirty_lists;

		VDO_ASSERT_LOG_ONLY(dirty_lists->next_period == 0, "current period not set");
		dirty_lists->oldest_period = map->current_era_point;
		dirty_lists->next_period = map->current_era_point + 1;
		dirty_lists->offset = map->current_era_point % dirty_lists->maximum_age;
	}
}

/* Compute the logical zone for the LBN of a data vio. */
zone_count_t vdo_compute_logical_zone(struct data_vio *data_vio)
{
	struct block_map *map = vdo_from_data_vio(data_vio)->block_map;
	struct tree_lock *tree_lock = &data_vio->tree_lock;
	page_number_t page_number = data_vio->logical.lbn / VDO_BLOCK_MAP_ENTRIES_PER_PAGE;

	tree_lock->tree_slots[0].page_index = page_number;
	tree_lock->root_index = page_number % map->root_count;
	return (tree_lock->root_index % map->zone_count);
}

void vdo_advance_block_map_era(struct block_map *map,
			       sequence_number_t recovery_block_number)
{
	if (map == NULL)
		return;

	map->pending_era_point = recovery_block_number;
	vdo_schedule_default_action(map->action_manager);
}

/* Implements vdo_admin_initiator_fn */
static void initiate_drain(struct admin_state *state)
{
	struct block_map_zone *zone = container_of(state, struct block_map_zone, state);

	VDO_ASSERT_LOG_ONLY((zone->active_lookups == 0),
			    "%s() called with no active lookups", __func__);

	if (!vdo_is_state_suspending(state)) {
		while (zone->dirty_lists->oldest_period < zone->dirty_lists->next_period)
			expire_oldest_list(zone->dirty_lists);
		write_expired_elements(zone);
	}

	check_for_drain_complete(zone);
}

/* Implements vdo_zone_action_fn. */
static void drain_zone(void *context, zone_count_t zone_number,
		       struct vdo_completion *parent)
{
	struct block_map *map = context;
	struct block_map_zone *zone = &map->zones[zone_number];

	vdo_start_draining(&zone->state,
			   vdo_get_current_manager_operation(map->action_manager),
			   parent, initiate_drain);
}

void vdo_drain_block_map(struct block_map *map, const struct admin_state_code *operation,
			 struct vdo_completion *parent)
{
	vdo_schedule_operation(map->action_manager, operation, NULL, drain_zone, NULL,
			       parent);
}

/* Implements vdo_zone_action_fn. */
static void resume_block_map_zone(void *context, zone_count_t zone_number,
				  struct vdo_completion *parent)
{
	struct block_map *map = context;
	struct block_map_zone *zone = &map->zones[zone_number];

	vdo_fail_completion(parent, vdo_resume_if_quiescent(&zone->state));
}

void vdo_resume_block_map(struct block_map *map, struct vdo_completion *parent)
{
	vdo_schedule_operation(map->action_manager, VDO_ADMIN_STATE_RESUMING,
			       NULL, resume_block_map_zone, NULL, parent);
}

/* Allocate an expanded collection of trees, for a future growth. */
int vdo_prepare_to_grow_block_map(struct block_map *map,
				  block_count_t new_logical_blocks)
{
	if (map->next_entry_count == new_logical_blocks)
		return VDO_SUCCESS;

	if (map->next_entry_count > 0)
		vdo_abandon_block_map_growth(map);

	if (new_logical_blocks < map->entry_count) {
		map->next_entry_count = map->entry_count;
		return VDO_SUCCESS;
	}

	return make_forest(map, new_logical_blocks);
}

/* Implements vdo_action_preamble_fn */
static void grow_forest(void *context, struct vdo_completion *completion)
{
	replace_forest(context);
	vdo_finish_completion(completion);
}

/* Requires vdo_prepare_to_grow_block_map() to have been previously called. */
void vdo_grow_block_map(struct block_map *map, struct vdo_completion *parent)
{
	vdo_schedule_operation(map->action_manager,
			       VDO_ADMIN_STATE_SUSPENDED_OPERATION,
			       grow_forest, NULL, NULL, parent);
}

void vdo_abandon_block_map_growth(struct block_map *map)
{
	struct forest *forest = vdo_forget(map->next_forest);

	if (forest != NULL)
		deforest(forest, forest->segments - 1);

	map->next_entry_count = 0;
}

/* Release the page completion and then continue the requester. */
static inline void finish_processing_page(struct vdo_completion *completion, int result)
{
	struct vdo_completion *parent = completion->parent;

	vdo_release_page_completion(completion);
	vdo_continue_completion(parent, result);
}

static void handle_page_error(struct vdo_completion *completion)
{
	finish_processing_page(completion, completion->result);
}

/* Fetch the mapping page for a block map update, and call the provided handler when fetched. */
static void fetch_mapping_page(struct data_vio *data_vio, bool modifiable,
			       vdo_action_fn action)
{
	struct block_map_zone *zone = data_vio->logical.zone->block_map_zone;

	if (vdo_is_state_draining(&zone->state)) {
		continue_data_vio_with_error(data_vio, VDO_SHUTTING_DOWN);
		return;
	}

	vdo_get_page(&data_vio->page_completion, zone,
		     data_vio->tree_lock.tree_slots[0].block_map_slot.pbn,
		     modifiable, &data_vio->vio.completion,
		     action, handle_page_error, false);
}

/**
 * clear_mapped_location() - Clear a data_vio's mapped block location, setting it to be unmapped.
 *
 * This indicates the block map entry for the logical block is either unmapped or corrupted.
 */
static void clear_mapped_location(struct data_vio *data_vio)
{
	data_vio->mapped = (struct zoned_pbn) {
		.state = VDO_MAPPING_STATE_UNMAPPED,
	};
}

/**
 * set_mapped_location() - Decode and validate a block map entry, and set the mapped location of a
 *                         data_vio.
 *
 * Return: VDO_SUCCESS or VDO_BAD_MAPPING if the map entry is invalid or an error code for any
 *         other failure
 */
static int __must_check set_mapped_location(struct data_vio *data_vio,
					    const struct block_map_entry *entry)
{
	/* Unpack the PBN for logging purposes even if the entry is invalid. */
	struct data_location mapped = vdo_unpack_block_map_entry(entry);

	if (vdo_is_valid_location(&mapped)) {
		int result;

		result = vdo_get_physical_zone(vdo_from_data_vio(data_vio),
					       mapped.pbn, &data_vio->mapped.zone);
		if (result == VDO_SUCCESS) {
			data_vio->mapped.pbn = mapped.pbn;
			data_vio->mapped.state = mapped.state;
			return VDO_SUCCESS;
		}

		/*
		 * Return all errors not specifically known to be errors from validating the
		 * location.
		 */
		if ((result != VDO_OUT_OF_RANGE) && (result != VDO_BAD_MAPPING))
			return result;
	}

	/*
	 * Log the corruption even if we wind up ignoring it for write VIOs, converting all cases
	 * to VDO_BAD_MAPPING.
	 */
	vdo_log_error_strerror(VDO_BAD_MAPPING,
			       "PBN %llu with state %u read from the block map was invalid",
			       (unsigned long long) mapped.pbn, mapped.state);

	/*
	 * A read VIO has no option but to report the bad mapping--reading zeros would be hiding
	 * known data loss.
	 */
	if (!data_vio->write)
		return VDO_BAD_MAPPING;

	/*
	 * A write VIO only reads this mapping to decref the old block. Treat this as an unmapped
	 * entry rather than fail the write.
	 */
	clear_mapped_location(data_vio);
	return VDO_SUCCESS;
}

/* This callback is registered in vdo_get_mapped_block(). */
static void get_mapping_from_fetched_page(struct vdo_completion *completion)
{
	int result;
	struct vdo_page_completion *vpc = as_vdo_page_completion(completion);
	const struct block_map_page *page;
	const struct block_map_entry *entry;
	struct data_vio *data_vio = as_data_vio(completion->parent);
	struct block_map_tree_slot *tree_slot;

	if (completion->result != VDO_SUCCESS) {
		finish_processing_page(completion, completion->result);
		return;
	}

	result = validate_completed_page(vpc, false);
	if (result != VDO_SUCCESS) {
		finish_processing_page(completion, result);
		return;
	}

	page = (const struct block_map_page *) get_page_buffer(vpc->info);
	tree_slot = &data_vio->tree_lock.tree_slots[0];
	entry = &page->entries[tree_slot->block_map_slot.slot];

	result = set_mapped_location(data_vio, entry);
	finish_processing_page(completion, result);
}

void vdo_update_block_map_page(struct block_map_page *page, struct data_vio *data_vio,
			       physical_block_number_t pbn,
			       enum block_mapping_state mapping_state,
			       sequence_number_t *recovery_lock)
{
	struct block_map_zone *zone = data_vio->logical.zone->block_map_zone;
	struct block_map *block_map = zone->block_map;
	struct recovery_journal *journal = block_map->journal;
	sequence_number_t old_locked, new_locked;
	struct tree_lock *tree_lock = &data_vio->tree_lock;

	/* Encode the new mapping. */
	page->entries[tree_lock->tree_slots[tree_lock->height].block_map_slot.slot] =
		vdo_pack_block_map_entry(pbn, mapping_state);

	/* Adjust references on the recovery journal blocks. */
	old_locked = *recovery_lock;
	new_locked = data_vio->recovery_sequence_number;

	if ((old_locked == 0) || (old_locked > new_locked)) {
		vdo_acquire_recovery_journal_block_reference(journal, new_locked,
							     VDO_ZONE_TYPE_LOGICAL,
							     zone->zone_number);

		if (old_locked > 0) {
			vdo_release_recovery_journal_block_reference(journal, old_locked,
								     VDO_ZONE_TYPE_LOGICAL,
								     zone->zone_number);
		}

		*recovery_lock = new_locked;
	}

	/*
	 * FIXME: explain this more
	 * Release the transferred lock from the data_vio.
	 */
	vdo_release_journal_entry_lock(journal, new_locked);
	data_vio->recovery_sequence_number = 0;
}

static void put_mapping_in_fetched_page(struct vdo_completion *completion)
{
	struct data_vio *data_vio = as_data_vio(completion->parent);
	sequence_number_t old_lock;
	struct vdo_page_completion *vpc;
	struct page_info *info;
	int result;

	if (completion->result != VDO_SUCCESS) {
		finish_processing_page(completion, completion->result);
		return;
	}

	vpc = as_vdo_page_completion(completion);
	result = validate_completed_page(vpc, true);
	if (result != VDO_SUCCESS) {
		finish_processing_page(completion, result);
		return;
	}

	info = vpc->info;
	old_lock = info->recovery_lock;
	vdo_update_block_map_page((struct block_map_page *) get_page_buffer(info),
				  data_vio, data_vio->new_mapped.pbn,
				  data_vio->new_mapped.state, &info->recovery_lock);
	set_info_state(info, PS_DIRTY);
	add_to_dirty_lists(info->cache->zone, &info->state_entry,
			   VDO_CACHE_PAGE, old_lock, info->recovery_lock);
	finish_processing_page(completion, VDO_SUCCESS);
}

/* Read a stored block mapping into a data_vio. */
void vdo_get_mapped_block(struct data_vio *data_vio)
{
	if (data_vio->tree_lock.tree_slots[0].block_map_slot.pbn == VDO_ZERO_BLOCK) {
		/*
		 * We know that the block map page for this LBN has not been allocated, so the
		 * block must be unmapped.
		 */
		clear_mapped_location(data_vio);
		continue_data_vio(data_vio);
		return;
	}

	fetch_mapping_page(data_vio, false, get_mapping_from_fetched_page);
}

/* Update a stored block mapping to reflect a data_vio's new mapping. */
void vdo_put_mapped_block(struct data_vio *data_vio)
{
	fetch_mapping_page(data_vio, true, put_mapping_in_fetched_page);
}

struct block_map_statistics vdo_get_block_map_statistics(struct block_map *map)
{
	zone_count_t zone = 0;
	struct block_map_statistics totals;

	memset(&totals, 0, sizeof(struct block_map_statistics));
	for (zone = 0; zone < map->zone_count; zone++) {
		const struct block_map_statistics *stats =
			&(map->zones[zone].page_cache.stats);

		totals.dirty_pages += READ_ONCE(stats->dirty_pages);
		totals.clean_pages += READ_ONCE(stats->clean_pages);
		totals.free_pages += READ_ONCE(stats->free_pages);
		totals.failed_pages += READ_ONCE(stats->failed_pages);
		totals.incoming_pages += READ_ONCE(stats->incoming_pages);
		totals.outgoing_pages += READ_ONCE(stats->outgoing_pages);
		totals.cache_pressure += READ_ONCE(stats->cache_pressure);
		totals.read_count += READ_ONCE(stats->read_count);
		totals.write_count += READ_ONCE(stats->write_count);
		totals.failed_reads += READ_ONCE(stats->failed_reads);
		totals.failed_writes += READ_ONCE(stats->failed_writes);
		totals.reclaimed += READ_ONCE(stats->reclaimed);
		totals.read_outgoing += READ_ONCE(stats->read_outgoing);
		totals.found_in_cache += READ_ONCE(stats->found_in_cache);
		totals.discard_required += READ_ONCE(stats->discard_required);
		totals.wait_for_page += READ_ONCE(stats->wait_for_page);
		totals.fetch_required += READ_ONCE(stats->fetch_required);
		totals.pages_loaded += READ_ONCE(stats->pages_loaded);
		totals.pages_saved += READ_ONCE(stats->pages_saved);
		totals.flush_count += READ_ONCE(stats->flush_count);
	}

	return totals;
}
