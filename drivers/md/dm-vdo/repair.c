// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "repair.h"

#include <linux/min_heap.h>
#include <linux/minmax.h>

#include "logger.h"
#include "memory-alloc.h"
#include "permassert.h"

#include "block-map.h"
#include "completion.h"
#include "constants.h"
#include "encodings.h"
#include "int-map.h"
#include "io-submitter.h"
#include "recovery-journal.h"
#include "slab-depot.h"
#include "types.h"
#include "vdo.h"
#include "wait-queue.h"

/*
 * An explicitly numbered block mapping. Numbering the mappings allows them to be sorted by logical
 * block number during repair while still preserving the relative order of journal entries with
 * the same logical block number.
 */
struct numbered_block_mapping {
	struct block_map_slot block_map_slot;
	struct block_map_entry block_map_entry;
	/* A serial number to use during replay */
	u32 number;
} __packed;

/*
 * The absolute position of an entry in the recovery journal, including the sector number and the
 * entry number within the sector.
 */
struct recovery_point {
	/* Block sequence number */
	sequence_number_t sequence_number;
	/* Sector number */
	u8 sector_count;
	/* Entry number */
	journal_entry_count_t entry_count;
	/* Whether or not the increment portion of the current entry has been applied */
	bool increment_applied;
};

DEFINE_MIN_HEAP(struct numbered_block_mapping, replay_heap);

struct repair_completion {
	/* The completion header */
	struct vdo_completion completion;

	/* A buffer to hold the data read off disk */
	char *journal_data;

	/* For loading the journal */
	data_vio_count_t vio_count;
	data_vio_count_t vios_complete;
	struct vio *vios;

	/* The number of entries to be applied to the block map */
	size_t block_map_entry_count;
	/* The sequence number of the first valid block for block map recovery */
	sequence_number_t block_map_head;
	/* The sequence number of the first valid block for slab journal replay */
	sequence_number_t slab_journal_head;
	/* The sequence number of the last valid block of the journal (if known) */
	sequence_number_t tail;
	/*
	 * The highest sequence number of the journal. During recovery (vs read-only rebuild), not
	 * the same as the tail, since the tail ignores blocks after the first hole.
	 */
	sequence_number_t highest_tail;

	/* The number of logical blocks currently known to be in use */
	block_count_t logical_blocks_used;
	/* The number of block map data blocks known to be allocated */
	block_count_t block_map_data_blocks;

	/* These fields are for playing the journal into the block map */
	/* The entry data for the block map recovery */
	struct numbered_block_mapping *entries;
	/* The number of entries in the entry array */
	size_t entry_count;
	/* number of pending (non-ready) requests*/
	page_count_t outstanding;
	/* number of page completions */
	page_count_t page_count;
	bool launching;
	/*
	 * a heap wrapping journal_entries. It re-orders and sorts journal entries in ascending LBN
	 * order, then original journal order. This permits efficient iteration over the journal
	 * entries in order.
	 */
	struct replay_heap replay_heap;
	/* Fields tracking progress through the journal entries. */
	struct numbered_block_mapping *current_entry;
	struct numbered_block_mapping *current_unfetched_entry;
	/* Current requested page's PBN */
	physical_block_number_t pbn;

	/* These fields are only used during recovery. */
	/* A location just beyond the last valid entry of the journal */
	struct recovery_point tail_recovery_point;
	/* The location of the next recovery journal entry to apply */
	struct recovery_point next_recovery_point;
	/* The journal point to give to the next synthesized decref */
	struct journal_point next_journal_point;
	/* The number of entries played into slab journals */
	size_t entries_added_to_slab_journals;

	/* These fields are only used during read-only rebuild */
	page_count_t page_to_fetch;
	/* the number of leaf pages in the block map */
	page_count_t leaf_pages;
	/* the last slot of the block map */
	struct block_map_slot last_slot;

	/*
	 * The page completions used for playing the journal into the block map, and, during
	 * read-only rebuild, for rebuilding the reference counts from the block map.
	 */
	struct vdo_page_completion page_completions[];
};

/*
 * This is a min_heap callback function that orders numbered_block_mappings using the
 * 'block_map_slot' field as the primary key and the mapping 'number' field as the secondary key.
 * Using the mapping number preserves the journal order of entries for the same slot, allowing us
 * to sort by slot while still ensuring we replay all entries with the same slot in the exact order
 * as they appeared in the journal.
 */
static bool mapping_is_less_than(const void *item1, const void *item2, void __always_unused *args)
{
	const struct numbered_block_mapping *mapping1 =
		(const struct numbered_block_mapping *) item1;
	const struct numbered_block_mapping *mapping2 =
		(const struct numbered_block_mapping *) item2;

	if (mapping1->block_map_slot.pbn != mapping2->block_map_slot.pbn)
		return mapping1->block_map_slot.pbn < mapping2->block_map_slot.pbn;

	if (mapping1->block_map_slot.slot != mapping2->block_map_slot.slot)
		return mapping1->block_map_slot.slot < mapping2->block_map_slot.slot;

	if (mapping1->number != mapping2->number)
		return mapping1->number < mapping2->number;

	return 0;
}

static void swap_mappings(void *item1, void *item2, void __always_unused *args)
{
	struct numbered_block_mapping *mapping1 = item1;
	struct numbered_block_mapping *mapping2 = item2;

	swap(*mapping1, *mapping2);
}

static const struct min_heap_callbacks repair_min_heap = {
	.less = mapping_is_less_than,
	.swp = swap_mappings,
};

static struct numbered_block_mapping *sort_next_heap_element(struct repair_completion *repair)
{
	struct replay_heap *heap = &repair->replay_heap;
	struct numbered_block_mapping *last;

	if (heap->nr == 0)
		return NULL;

	/*
	 * Swap the next heap element with the last one on the heap, popping it off the heap,
	 * restore the heap invariant, and return a pointer to the popped element.
	 */
	last = &repair->entries[--heap->nr];
	swap_mappings(heap->data, last, NULL);
	min_heap_sift_down(heap, 0, &repair_min_heap, NULL);
	return last;
}

/**
 * as_repair_completion() - Convert a generic completion to a repair_completion.
 * @completion: The completion to convert.
 *
 * Return: The repair_completion.
 */
static inline struct repair_completion * __must_check
as_repair_completion(struct vdo_completion *completion)
{
	vdo_assert_completion_type(completion, VDO_REPAIR_COMPLETION);
	return container_of(completion, struct repair_completion, completion);
}

static void prepare_repair_completion(struct repair_completion *repair,
				      vdo_action_fn callback, enum vdo_zone_type zone_type)
{
	struct vdo_completion *completion = &repair->completion;
	const struct thread_config *thread_config = &completion->vdo->thread_config;
	thread_id_t thread_id;

	/* All blockmap access is done on single thread, so use logical zone 0. */
	thread_id = ((zone_type == VDO_ZONE_TYPE_LOGICAL) ?
		     thread_config->logical_threads[0] :
		     thread_config->admin_thread);
	vdo_reset_completion(completion);
	vdo_set_completion_callback(completion, callback, thread_id);
}

static void launch_repair_completion(struct repair_completion *repair,
				     vdo_action_fn callback, enum vdo_zone_type zone_type)
{
	prepare_repair_completion(repair, callback, zone_type);
	vdo_launch_completion(&repair->completion);
}

static void uninitialize_vios(struct repair_completion *repair)
{
	while (repair->vio_count > 0)
		free_vio_components(&repair->vios[--repair->vio_count]);

	vdo_free(vdo_forget(repair->vios));
}

static void free_repair_completion(struct repair_completion *repair)
{
	if (repair == NULL)
		return;

	/*
	 * We do this here because this function is the only common bottleneck for all clean up
	 * paths.
	 */
	repair->completion.vdo->block_map->zones[0].page_cache.rebuilding = false;

	uninitialize_vios(repair);
	vdo_free(vdo_forget(repair->journal_data));
	vdo_free(vdo_forget(repair->entries));
	vdo_free(repair);
}

static void finish_repair(struct vdo_completion *completion)
{
	struct vdo_completion *parent = completion->parent;
	struct vdo *vdo = completion->vdo;
	struct repair_completion *repair = as_repair_completion(completion);

	vdo_assert_on_admin_thread(vdo, __func__);

	if (vdo->load_state != VDO_REBUILD_FOR_UPGRADE)
		vdo->states.vdo.complete_recoveries++;

	vdo_initialize_recovery_journal_post_repair(vdo->recovery_journal,
						    vdo->states.vdo.complete_recoveries,
						    repair->highest_tail,
						    repair->logical_blocks_used,
						    repair->block_map_data_blocks);
	free_repair_completion(vdo_forget(repair));

	if (vdo_state_requires_read_only_rebuild(vdo->load_state)) {
		vdo_log_info("Read-only rebuild complete");
		vdo_launch_completion(parent);
		return;
	}

	/* FIXME: shouldn't this say either "recovery" or "repair"? */
	vdo_log_info("Rebuild complete");

	/*
	 * Now that we've freed the repair completion and its vast array of journal entries, we
	 * can allocate refcounts.
	 */
	vdo_continue_completion(parent, vdo_allocate_reference_counters(vdo->depot));
}

/**
 * abort_repair() - Handle a repair error.
 * @completion: The repair completion.
 */
static void abort_repair(struct vdo_completion *completion)
{
	struct vdo_completion *parent = completion->parent;
	int result = completion->result;
	struct repair_completion *repair = as_repair_completion(completion);

	if (vdo_state_requires_read_only_rebuild(completion->vdo->load_state))
		vdo_log_info("Read-only rebuild aborted");
	else
		vdo_log_warning("Recovery aborted");

	free_repair_completion(vdo_forget(repair));
	vdo_continue_completion(parent, result);
}

/**
 * abort_on_error() - Abort a repair if there is an error.
 * @result: The result to check.
 * @repair: The repair completion.
 *
 * Return: true if the result was an error.
 */
static bool __must_check abort_on_error(int result, struct repair_completion *repair)
{
	if (result == VDO_SUCCESS)
		return false;

	vdo_fail_completion(&repair->completion, result);
	return true;
}

/**
 * drain_slab_depot() - Flush out all dirty refcounts blocks now that they have been rebuilt or
 *                      recovered.
 * @completion: The repair completion.
 */
static void drain_slab_depot(struct vdo_completion *completion)
{
	struct vdo *vdo = completion->vdo;
	struct repair_completion *repair = as_repair_completion(completion);
	const struct admin_state_code *operation;

	vdo_assert_on_admin_thread(vdo, __func__);

	prepare_repair_completion(repair, finish_repair, VDO_ZONE_TYPE_ADMIN);
	if (vdo_state_requires_read_only_rebuild(vdo->load_state)) {
		vdo_log_info("Saving rebuilt state");
		operation = VDO_ADMIN_STATE_REBUILDING;
	} else {
		vdo_log_info("Replayed %zu journal entries into slab journals",
			     repair->entries_added_to_slab_journals);
		operation = VDO_ADMIN_STATE_RECOVERING;
	}

	vdo_drain_slab_depot(vdo->depot, operation, completion);
}

/**
 * flush_block_map_updates() - Flush the block map now that all the reference counts are rebuilt.
 * @completion: The repair completion.
 *
 * This callback is registered in finish_if_done().
 */
static void flush_block_map_updates(struct vdo_completion *completion)
{
	vdo_assert_on_admin_thread(completion->vdo, __func__);

	vdo_log_info("Flushing block map changes");
	prepare_repair_completion(as_repair_completion(completion), drain_slab_depot,
				  VDO_ZONE_TYPE_ADMIN);
	vdo_drain_block_map(completion->vdo->block_map, VDO_ADMIN_STATE_RECOVERING,
			    completion);
}

static bool fetch_page(struct repair_completion *repair,
		       struct vdo_completion *completion);

/**
 * handle_page_load_error() - Handle an error loading a page.
 * @completion: The vdo_page_completion.
 */
static void handle_page_load_error(struct vdo_completion *completion)
{
	struct repair_completion *repair = completion->parent;

	repair->outstanding--;
	vdo_set_completion_result(&repair->completion, completion->result);
	vdo_release_page_completion(completion);
	fetch_page(repair, completion);
}

/**
 * unmap_entry() - Unmap an invalid entry and indicate that its page must be written out.
 * @page: The page containing the entries
 * @completion: The page_completion for writing the page
 * @slot: The slot to unmap
 */
static void unmap_entry(struct block_map_page *page, struct vdo_completion *completion,
			slot_number_t slot)
{
	page->entries[slot] = UNMAPPED_BLOCK_MAP_ENTRY;
	vdo_request_page_write(completion);
}

/**
 * remove_out_of_bounds_entries() - Unmap entries which outside the logical space.
 * @page: The page containing the entries
 * @completion: The page_completion for writing the page
 * @start: The first slot to check
 */
static void remove_out_of_bounds_entries(struct block_map_page *page,
					 struct vdo_completion *completion,
					 slot_number_t start)
{
	slot_number_t slot;

	for (slot = start; slot < VDO_BLOCK_MAP_ENTRIES_PER_PAGE; slot++) {
		struct data_location mapping = vdo_unpack_block_map_entry(&page->entries[slot]);

		if (vdo_is_mapped_location(&mapping))
			unmap_entry(page, completion, slot);
	}
}

/**
 * process_slot() - Update the reference counts for a single entry.
 * @page: The page containing the entries
 * @completion: The page_completion for writing the page
 * @slot: The slot to check
 *
 * Return: true if the entry was a valid mapping
 */
static bool process_slot(struct block_map_page *page, struct vdo_completion *completion,
			 slot_number_t slot)
{
	struct slab_depot *depot = completion->vdo->depot;
	int result;
	struct data_location mapping = vdo_unpack_block_map_entry(&page->entries[slot]);

	if (!vdo_is_valid_location(&mapping)) {
		/* This entry is invalid, so remove it from the page. */
		unmap_entry(page, completion, slot);
		return false;
	}

	if (!vdo_is_mapped_location(&mapping))
		return false;


	if (mapping.pbn == VDO_ZERO_BLOCK)
		return true;

	if (!vdo_is_physical_data_block(depot, mapping.pbn)) {
		/*
		 * This is a nonsense mapping. Remove it from the map so we're at least consistent
		 * and mark the page dirty.
		 */
		unmap_entry(page, completion, slot);
		return false;
	}

	result = vdo_adjust_reference_count_for_rebuild(depot, mapping.pbn,
							VDO_JOURNAL_DATA_REMAPPING);
	if (result == VDO_SUCCESS)
		return true;

	vdo_log_error_strerror(result,
			       "Could not adjust reference count for PBN %llu, slot %u mapped to PBN %llu",
			       (unsigned long long) vdo_get_block_map_page_pbn(page),
			       slot, (unsigned long long) mapping.pbn);
	unmap_entry(page, completion, slot);
	return false;
}

/**
 * rebuild_reference_counts_from_page() - Rebuild reference counts from a block map page.
 * @repair: The repair completion.
 * @completion: The page completion holding the page.
 */
static void rebuild_reference_counts_from_page(struct repair_completion *repair,
					       struct vdo_completion *completion)
{
	slot_number_t slot, last_slot;
	struct block_map_page *page;
	int result;

	result = vdo_get_cached_page(completion, &page);
	if (result != VDO_SUCCESS) {
		vdo_set_completion_result(&repair->completion, result);
		return;
	}

	if (!page->header.initialized)
		return;

	/* Remove any bogus entries which exist beyond the end of the logical space. */
	if (vdo_get_block_map_page_pbn(page) == repair->last_slot.pbn) {
		last_slot = repair->last_slot.slot;
		remove_out_of_bounds_entries(page, completion, last_slot);
	} else {
		last_slot = VDO_BLOCK_MAP_ENTRIES_PER_PAGE;
	}

	/* Inform the slab depot of all entries on this page. */
	for (slot = 0; slot < last_slot; slot++) {
		if (process_slot(page, completion, slot))
			repair->logical_blocks_used++;
	}
}

/**
 * page_loaded() - Process a page which has just been loaded.
 * @completion: The vdo_page_completion for the fetched page.
 *
 * This callback is registered by fetch_page().
 */
static void page_loaded(struct vdo_completion *completion)
{
	struct repair_completion *repair = completion->parent;

	repair->outstanding--;
	rebuild_reference_counts_from_page(repair, completion);
	vdo_release_page_completion(completion);

	/* Advance progress to the next page, and fetch the next page we haven't yet requested. */
	fetch_page(repair, completion);
}

static physical_block_number_t get_pbn_to_fetch(struct repair_completion *repair,
						struct block_map *block_map)
{
	physical_block_number_t pbn = VDO_ZERO_BLOCK;

	if (repair->completion.result != VDO_SUCCESS)
		return VDO_ZERO_BLOCK;

	while ((pbn == VDO_ZERO_BLOCK) && (repair->page_to_fetch < repair->leaf_pages))
		pbn = vdo_find_block_map_page_pbn(block_map, repair->page_to_fetch++);

	if (vdo_is_physical_data_block(repair->completion.vdo->depot, pbn))
		return pbn;

	vdo_set_completion_result(&repair->completion, VDO_BAD_MAPPING);
	return VDO_ZERO_BLOCK;
}

/**
 * fetch_page() - Fetch a page from the block map.
 * @repair: The repair_completion.
 * @completion: The page completion to use.
 *
 * Return true if the rebuild is complete
 */
static bool fetch_page(struct repair_completion *repair,
		       struct vdo_completion *completion)
{
	struct vdo_page_completion *page_completion = (struct vdo_page_completion *) completion;
	struct block_map *block_map = repair->completion.vdo->block_map;
	physical_block_number_t pbn = get_pbn_to_fetch(repair, block_map);

	if (pbn != VDO_ZERO_BLOCK) {
		repair->outstanding++;
		/*
		 * We must set the requeue flag here to ensure that we don't blow the stack if all
		 * the requested pages are already in the cache or get load errors.
		 */
		vdo_get_page(page_completion, &block_map->zones[0], pbn, true, repair,
			     page_loaded, handle_page_load_error, true);
	}

	if (repair->outstanding > 0)
		return false;

	launch_repair_completion(repair, flush_block_map_updates, VDO_ZONE_TYPE_ADMIN);
	return true;
}

/**
 * rebuild_from_leaves() - Rebuild reference counts from the leaf block map pages.
 * @completion: The repair completion.
 *
 * Rebuilds reference counts from the leaf block map pages now that reference counts have been
 * rebuilt from the interior tree pages (which have been loaded in the process). This callback is
 * registered in rebuild_reference_counts().
 */
static void rebuild_from_leaves(struct vdo_completion *completion)
{
	page_count_t i;
	struct repair_completion *repair = as_repair_completion(completion);
	struct block_map *map = completion->vdo->block_map;

	repair->logical_blocks_used = 0;

	/*
	 * The PBN calculation doesn't work until the tree pages have been loaded, so we can't set
	 * this value at the start of repair.
	 */
	repair->leaf_pages = vdo_compute_block_map_page_count(map->entry_count);
	repair->last_slot = (struct block_map_slot) {
		.slot = map->entry_count % VDO_BLOCK_MAP_ENTRIES_PER_PAGE,
		.pbn = vdo_find_block_map_page_pbn(map, repair->leaf_pages - 1),
	};
	if (repair->last_slot.slot == 0)
		repair->last_slot.slot = VDO_BLOCK_MAP_ENTRIES_PER_PAGE;

	for (i = 0; i < repair->page_count; i++) {
		if (fetch_page(repair, &repair->page_completions[i].completion)) {
			/*
			 * The rebuild has already moved on, so it isn't safe nor is there a need
			 * to launch any more fetches.
			 */
			return;
		}
	}
}

/**
 * process_entry() - Process a single entry from the block map tree.
 * @pbn: A pbn which holds a block map tree page.
 * @completion: The parent completion of the traversal.
 *
 * Implements vdo_entry_callback_fn.
 *
 * Return: VDO_SUCCESS or an error.
 */
static int process_entry(physical_block_number_t pbn, struct vdo_completion *completion)
{
	struct repair_completion *repair = as_repair_completion(completion);
	struct slab_depot *depot = completion->vdo->depot;
	int result;

	if ((pbn == VDO_ZERO_BLOCK) || !vdo_is_physical_data_block(depot, pbn)) {
		return vdo_log_error_strerror(VDO_BAD_CONFIGURATION,
					      "PBN %llu out of range",
					      (unsigned long long) pbn);
	}

	result = vdo_adjust_reference_count_for_rebuild(depot, pbn,
							VDO_JOURNAL_BLOCK_MAP_REMAPPING);
	if (result != VDO_SUCCESS) {
		return vdo_log_error_strerror(result,
					      "Could not adjust reference count for block map tree PBN %llu",
					      (unsigned long long) pbn);
	}

	repair->block_map_data_blocks++;
	return VDO_SUCCESS;
}

static void rebuild_reference_counts(struct vdo_completion *completion)
{
	struct repair_completion *repair = as_repair_completion(completion);
	struct vdo *vdo = completion->vdo;
	struct vdo_page_cache *cache = &vdo->block_map->zones[0].page_cache;

	/* We must allocate ref_counts before we can rebuild them. */
	if (abort_on_error(vdo_allocate_reference_counters(vdo->depot), repair))
		return;

	/*
	 * Completion chaining from page cache hits can lead to stack overflow during the rebuild,
	 * so clear out the cache before this rebuild phase.
	 */
	if (abort_on_error(vdo_invalidate_page_cache(cache), repair))
		return;

	prepare_repair_completion(repair, rebuild_from_leaves, VDO_ZONE_TYPE_LOGICAL);
	vdo_traverse_forest(vdo->block_map, process_entry, completion);
}

static void increment_recovery_point(struct recovery_point *point)
{
	if (++point->entry_count < RECOVERY_JOURNAL_ENTRIES_PER_SECTOR)
		return;

	point->entry_count = 0;
	if (point->sector_count < (VDO_SECTORS_PER_BLOCK - 1)) {
		point->sector_count++;
		return;
	}

	point->sequence_number++;
	point->sector_count = 1;
}

/**
 * advance_points() - Advance the current recovery and journal points.
 * @repair: The repair_completion whose points are to be advanced.
 * @entries_per_block: The number of entries in a recovery journal block.
 */
static void advance_points(struct repair_completion *repair,
			   journal_entry_count_t entries_per_block)
{
	if (!repair->next_recovery_point.increment_applied) {
		repair->next_recovery_point.increment_applied	= true;
		return;
	}

	increment_recovery_point(&repair->next_recovery_point);
	vdo_advance_journal_point(&repair->next_journal_point, entries_per_block);
	repair->next_recovery_point.increment_applied	= false;
}

/**
 * before_recovery_point() - Check whether the first point precedes the second point.
 * @first: The first recovery point.
 * @second: The second recovery point.
 *
 * Return: true if the first point precedes the second point.
 */
static bool __must_check before_recovery_point(const struct recovery_point *first,
					       const struct recovery_point *second)
{
	if (first->sequence_number < second->sequence_number)
		return true;

	if (first->sequence_number > second->sequence_number)
		return false;

	if (first->sector_count < second->sector_count)
		return true;

	return ((first->sector_count == second->sector_count) &&
		(first->entry_count < second->entry_count));
}

static struct packed_journal_sector * __must_check get_sector(struct recovery_journal *journal,
							      char *journal_data,
							      sequence_number_t sequence,
							      u8 sector_number)
{
	off_t offset;

	offset = ((vdo_get_recovery_journal_block_number(journal, sequence) * VDO_BLOCK_SIZE) +
		  (VDO_SECTOR_SIZE * sector_number));
	return (struct packed_journal_sector *) (journal_data + offset);
}

/**
 * get_entry() - Unpack the recovery journal entry associated with the given recovery point.
 * @repair: The repair completion.
 * @point: The recovery point.
 *
 * Return: The unpacked contents of the matching recovery journal entry.
 */
static struct recovery_journal_entry get_entry(const struct repair_completion *repair,
					       const struct recovery_point *point)
{
	struct packed_journal_sector *sector;

	sector = get_sector(repair->completion.vdo->recovery_journal,
			    repair->journal_data, point->sequence_number,
			    point->sector_count);
	return vdo_unpack_recovery_journal_entry(&sector->entries[point->entry_count]);
}

/**
 * validate_recovery_journal_entry() - Validate a recovery journal entry.
 * @vdo: The vdo.
 * @entry: The entry to validate.
 *
 * Return: VDO_SUCCESS or an error.
 */
static int validate_recovery_journal_entry(const struct vdo *vdo,
					   const struct recovery_journal_entry *entry)
{
	if ((entry->slot.pbn >= vdo->states.vdo.config.physical_blocks) ||
	    (entry->slot.slot >= VDO_BLOCK_MAP_ENTRIES_PER_PAGE) ||
	    !vdo_is_valid_location(&entry->mapping) ||
	    !vdo_is_valid_location(&entry->unmapping) ||
	    !vdo_is_physical_data_block(vdo->depot, entry->mapping.pbn) ||
	    !vdo_is_physical_data_block(vdo->depot, entry->unmapping.pbn)) {
		return vdo_log_error_strerror(VDO_CORRUPT_JOURNAL,
					      "Invalid entry: %s (%llu, %u) from %llu to %llu is not within bounds",
					      vdo_get_journal_operation_name(entry->operation),
					      (unsigned long long) entry->slot.pbn,
					      entry->slot.slot,
					      (unsigned long long) entry->unmapping.pbn,
					      (unsigned long long) entry->mapping.pbn);
	}

	if ((entry->operation == VDO_JOURNAL_BLOCK_MAP_REMAPPING) &&
	    (vdo_is_state_compressed(entry->mapping.state) ||
	     (entry->mapping.pbn == VDO_ZERO_BLOCK) ||
	     (entry->unmapping.state != VDO_MAPPING_STATE_UNMAPPED) ||
	     (entry->unmapping.pbn != VDO_ZERO_BLOCK))) {
		return vdo_log_error_strerror(VDO_CORRUPT_JOURNAL,
					      "Invalid entry: %s (%llu, %u) from %llu to %llu is not a valid tree mapping",
					      vdo_get_journal_operation_name(entry->operation),
					      (unsigned long long) entry->slot.pbn,
					      entry->slot.slot,
					      (unsigned long long) entry->unmapping.pbn,
					      (unsigned long long) entry->mapping.pbn);
	}

	return VDO_SUCCESS;
}

/**
 * add_slab_journal_entries() - Replay recovery journal entries into the slab journals of the
 *                              allocator currently being recovered.
 * @completion: The allocator completion.
 *
 * Waits for slab journal tailblock space when necessary. This method is its own callback.
 */
static void add_slab_journal_entries(struct vdo_completion *completion)
{
	struct recovery_point *recovery_point;
	struct repair_completion *repair = completion->parent;
	struct vdo *vdo = completion->vdo;
	struct recovery_journal *journal = vdo->recovery_journal;
	struct block_allocator *allocator = vdo_as_block_allocator(completion);

	/* Get ready in case we need to enqueue again. */
	vdo_prepare_completion(completion, add_slab_journal_entries,
			       vdo_notify_slab_journals_are_recovered,
			       completion->callback_thread_id, repair);
	for (recovery_point = &repair->next_recovery_point;
	     before_recovery_point(recovery_point, &repair->tail_recovery_point);
	     advance_points(repair, journal->entries_per_block)) {
		int result;
		physical_block_number_t pbn;
		struct vdo_slab *slab;
		struct recovery_journal_entry entry = get_entry(repair, recovery_point);
		bool increment = !repair->next_recovery_point.increment_applied;

		if (increment) {
			result = validate_recovery_journal_entry(vdo, &entry);
			if (result != VDO_SUCCESS) {
				vdo_enter_read_only_mode(vdo, result);
				vdo_fail_completion(completion, result);
				return;
			}

			pbn = entry.mapping.pbn;
		} else {
			pbn = entry.unmapping.pbn;
		}

		if (pbn == VDO_ZERO_BLOCK)
			continue;

		slab = vdo_get_slab(vdo->depot, pbn);
		if (slab->allocator != allocator)
			continue;

		if (!vdo_attempt_replay_into_slab(slab, pbn, entry.operation, increment,
						  &repair->next_journal_point,
						  completion))
			return;

		repair->entries_added_to_slab_journals++;
	}

	vdo_notify_slab_journals_are_recovered(completion);
}

/**
 * vdo_replay_into_slab_journals() - Replay recovery journal entries in the slab journals of slabs
 *                                   owned by a given block_allocator.
 * @allocator: The allocator whose slab journals are to be recovered.
 * @context: The slab depot load context supplied by a recovery when it loads the depot.
 */
void vdo_replay_into_slab_journals(struct block_allocator *allocator, void *context)
{
	struct vdo_completion *completion = &allocator->completion;
	struct repair_completion *repair = context;
	struct vdo *vdo = completion->vdo;

	vdo_assert_on_physical_zone_thread(vdo, allocator->zone_number, __func__);
	if (repair->entry_count == 0) {
		/* there's nothing to replay */
		repair->logical_blocks_used = vdo->recovery_journal->logical_blocks_used;
		repair->block_map_data_blocks = vdo->recovery_journal->block_map_data_blocks;
		vdo_notify_slab_journals_are_recovered(completion);
		return;
	}

	repair->next_recovery_point = (struct recovery_point) {
		.sequence_number = repair->slab_journal_head,
		.sector_count = 1,
		.entry_count = 0,
	};

	repair->next_journal_point = (struct journal_point) {
		.sequence_number = repair->slab_journal_head,
		.entry_count = 0,
	};

	vdo_log_info("Replaying entries into slab journals for zone %u",
		     allocator->zone_number);
	completion->parent = repair;
	add_slab_journal_entries(completion);
}

static void load_slab_depot(struct vdo_completion *completion)
{
	struct repair_completion *repair = as_repair_completion(completion);
	const struct admin_state_code *operation;

	vdo_assert_on_admin_thread(completion->vdo, __func__);

	if (vdo_state_requires_read_only_rebuild(completion->vdo->load_state)) {
		prepare_repair_completion(repair, rebuild_reference_counts,
					  VDO_ZONE_TYPE_LOGICAL);
		operation = VDO_ADMIN_STATE_LOADING_FOR_REBUILD;
	} else {
		prepare_repair_completion(repair, drain_slab_depot, VDO_ZONE_TYPE_ADMIN);
		operation = VDO_ADMIN_STATE_LOADING_FOR_RECOVERY;
	}

	vdo_load_slab_depot(completion->vdo->depot, operation, completion, repair);
}

static void flush_block_map(struct vdo_completion *completion)
{
	struct repair_completion *repair = as_repair_completion(completion);
	const struct admin_state_code *operation;

	vdo_assert_on_admin_thread(completion->vdo, __func__);

	vdo_log_info("Flushing block map changes");
	prepare_repair_completion(repair, load_slab_depot, VDO_ZONE_TYPE_ADMIN);
	operation = (vdo_state_requires_read_only_rebuild(completion->vdo->load_state) ?
		     VDO_ADMIN_STATE_REBUILDING :
		     VDO_ADMIN_STATE_RECOVERING);
	vdo_drain_block_map(completion->vdo->block_map, operation, completion);
}

static bool finish_if_done(struct repair_completion *repair)
{
	/* Pages are still being launched or there is still work to do */
	if (repair->launching || (repair->outstanding > 0))
		return false;

	if (repair->completion.result != VDO_SUCCESS) {
		page_count_t i;

		for (i = 0; i < repair->page_count; i++) {
			struct vdo_page_completion *page_completion =
				&repair->page_completions[i];

			if (page_completion->ready)
				vdo_release_page_completion(&page_completion->completion);
		}

		vdo_launch_completion(&repair->completion);
		return true;
	}

	if (repair->current_entry >= repair->entries)
		return false;

	launch_repair_completion(repair, flush_block_map, VDO_ZONE_TYPE_ADMIN);
	return true;
}

static void abort_block_map_recovery(struct repair_completion *repair, int result)
{
	vdo_set_completion_result(&repair->completion, result);
	finish_if_done(repair);
}

/**
 * find_entry_starting_next_page() - Find the first journal entry after a given entry which is not
 *                                   on the same block map page.
 * @repair: The repair completion.
 * @current_entry: The entry to search from.
 * @needs_sort: Whether sorting is needed to proceed.
 *
 * Return: Pointer to the first later journal entry on a different block map page, or a pointer to
 *         just before the journal entries if no subsequent entry is on a different block map page.
 */
static struct numbered_block_mapping *
find_entry_starting_next_page(struct repair_completion *repair,
			      struct numbered_block_mapping *current_entry, bool needs_sort)
{
	size_t current_page;

	/* If current_entry is invalid, return immediately. */
	if (current_entry < repair->entries)
		return current_entry;

	current_page = current_entry->block_map_slot.pbn;

	/* Decrement current_entry until it's out of bounds or on a different page. */
	while ((current_entry >= repair->entries) &&
	       (current_entry->block_map_slot.pbn == current_page)) {
		if (needs_sort) {
			struct numbered_block_mapping *just_sorted_entry =
				sort_next_heap_element(repair);
			VDO_ASSERT_LOG_ONLY(just_sorted_entry < current_entry,
					    "heap is returning elements in an unexpected order");
		}

		current_entry--;
	}

	return current_entry;
}

/*
 * Apply a range of journal entries [starting_entry, ending_entry) journal
 * entries to a block map page.
 */
static void apply_journal_entries_to_page(struct block_map_page *page,
					  struct numbered_block_mapping *starting_entry,
					  struct numbered_block_mapping *ending_entry)
{
	struct numbered_block_mapping *current_entry = starting_entry;

	while (current_entry != ending_entry) {
		page->entries[current_entry->block_map_slot.slot] = current_entry->block_map_entry;
		current_entry--;
	}
}

static void recover_ready_pages(struct repair_completion *repair,
				struct vdo_completion *completion);

static void block_map_page_loaded(struct vdo_completion *completion)
{
	struct repair_completion *repair = as_repair_completion(completion->parent);

	repair->outstanding--;
	if (!repair->launching)
		recover_ready_pages(repair, completion);
}

static void handle_block_map_page_load_error(struct vdo_completion *completion)
{
	struct repair_completion *repair = as_repair_completion(completion->parent);

	repair->outstanding--;
	abort_block_map_recovery(repair, completion->result);
}

static void fetch_block_map_page(struct repair_completion *repair,
				 struct vdo_completion *completion)
{
	physical_block_number_t pbn;

	if (repair->current_unfetched_entry < repair->entries)
		/* Nothing left to fetch. */
		return;

	/* Fetch the next page we haven't yet requested. */
	pbn = repair->current_unfetched_entry->block_map_slot.pbn;
	repair->current_unfetched_entry =
		find_entry_starting_next_page(repair, repair->current_unfetched_entry,
					      true);
	repair->outstanding++;
	vdo_get_page(((struct vdo_page_completion *) completion),
		     &repair->completion.vdo->block_map->zones[0], pbn, true,
		     &repair->completion, block_map_page_loaded,
		     handle_block_map_page_load_error, false);
}

static struct vdo_page_completion *get_next_page_completion(struct repair_completion *repair,
							    struct vdo_page_completion *completion)
{
	completion++;
	if (completion == (&repair->page_completions[repair->page_count]))
		completion = &repair->page_completions[0];
	return completion;
}

static void recover_ready_pages(struct repair_completion *repair,
				struct vdo_completion *completion)
{
	struct vdo_page_completion *page_completion = (struct vdo_page_completion *) completion;

	if (finish_if_done(repair))
		return;

	if (repair->pbn != page_completion->pbn)
		return;

	while (page_completion->ready) {
		struct numbered_block_mapping *start_of_next_page;
		struct block_map_page *page;
		int result;

		result = vdo_get_cached_page(completion, &page);
		if (result != VDO_SUCCESS) {
			abort_block_map_recovery(repair, result);
			return;
		}

		start_of_next_page =
			find_entry_starting_next_page(repair, repair->current_entry,
						      false);
		apply_journal_entries_to_page(page, repair->current_entry,
					      start_of_next_page);
		repair->current_entry = start_of_next_page;
		vdo_request_page_write(completion);
		vdo_release_page_completion(completion);

		if (finish_if_done(repair))
			return;

		repair->pbn = repair->current_entry->block_map_slot.pbn;
		fetch_block_map_page(repair, completion);
		page_completion = get_next_page_completion(repair, page_completion);
		completion = &page_completion->completion;
	}
}

static void recover_block_map(struct vdo_completion *completion)
{
	struct repair_completion *repair = as_repair_completion(completion);
	struct vdo *vdo = completion->vdo;
	struct numbered_block_mapping *first_sorted_entry;
	page_count_t i;

	vdo_assert_on_logical_zone_thread(vdo, 0, __func__);

	/* Suppress block map errors. */
	vdo->block_map->zones[0].page_cache.rebuilding =
		vdo_state_requires_read_only_rebuild(vdo->load_state);

	if (repair->block_map_entry_count == 0) {
		vdo_log_info("Replaying 0 recovery entries into block map");
		vdo_free(vdo_forget(repair->journal_data));
		launch_repair_completion(repair, load_slab_depot, VDO_ZONE_TYPE_ADMIN);
		return;
	}

	/*
	 * Organize the journal entries into a binary heap so we can iterate over them in sorted
	 * order incrementally, avoiding an expensive sort call.
	 */
	repair->replay_heap = (struct replay_heap) {
		.data = repair->entries,
		.nr = repair->block_map_entry_count,
		.size = repair->block_map_entry_count,
	};
	min_heapify_all(&repair->replay_heap, &repair_min_heap, NULL);

	vdo_log_info("Replaying %zu recovery entries into block map",
		     repair->block_map_entry_count);

	repair->current_entry = &repair->entries[repair->block_map_entry_count - 1];
	first_sorted_entry = sort_next_heap_element(repair);
	VDO_ASSERT_LOG_ONLY(first_sorted_entry == repair->current_entry,
			    "heap is returning elements in an unexpected order");

	/* Prevent any page from being processed until all pages have been launched. */
	repair->launching = true;
	repair->pbn = repair->current_entry->block_map_slot.pbn;
	repair->current_unfetched_entry = repair->current_entry;
	for (i = 0; i < repair->page_count; i++) {
		if (repair->current_unfetched_entry < repair->entries)
			break;

		fetch_block_map_page(repair, &repair->page_completions[i].completion);
	}
	repair->launching = false;

	/* Process any ready pages. */
	recover_ready_pages(repair, &repair->page_completions[0].completion);
}

/**
 * get_recovery_journal_block_header() - Get the block header for a block at a position in the
 *                                       journal data and unpack it.
 * @journal: The recovery journal.
 * @data: The recovery journal data.
 * @sequence: The sequence number.
 *
 * Return: The unpacked header.
 */
static struct recovery_block_header __must_check
get_recovery_journal_block_header(struct recovery_journal *journal, char *data,
				  sequence_number_t sequence)
{
	physical_block_number_t pbn =
		vdo_get_recovery_journal_block_number(journal, sequence);
	char *header = &data[pbn * VDO_BLOCK_SIZE];

	return vdo_unpack_recovery_block_header((struct packed_journal_header *) header);
}

/**
 * is_valid_recovery_journal_block() - Determine whether the given header describes a valid block
 *                                     for the given journal.
 * @journal: The journal to use.
 * @header: The unpacked block header to check.
 * @old_ok: Whether an old format header is valid.
 *
 * A block is not valid if it is unformatted, or if it is older than the last successful recovery
 * or reformat.
 *
 * Return: True if the header is valid.
 */
static bool __must_check is_valid_recovery_journal_block(const struct recovery_journal *journal,
							 const struct recovery_block_header *header,
							 bool old_ok)
{
	if ((header->nonce != journal->nonce) ||
	    (header->recovery_count != journal->recovery_count))
		return false;

	if (header->metadata_type == VDO_METADATA_RECOVERY_JOURNAL_2)
		return (header->entry_count <= journal->entries_per_block);

	return (old_ok &&
		(header->metadata_type == VDO_METADATA_RECOVERY_JOURNAL) &&
		(header->entry_count <= RECOVERY_JOURNAL_1_ENTRIES_PER_BLOCK));
}

/**
 * is_exact_recovery_journal_block() - Determine whether the given header describes the exact block
 *                                     indicated.
 * @journal: The journal to use.
 * @header: The unpacked block header to check.
 * @sequence: The expected sequence number.
 *
 * Return: True if the block matches.
 */
static bool __must_check is_exact_recovery_journal_block(const struct recovery_journal *journal,
							 const struct recovery_block_header *header,
							 sequence_number_t sequence)
{
	return ((header->sequence_number == sequence) &&
		(is_valid_recovery_journal_block(journal, header, true)));
}

/**
 * find_recovery_journal_head_and_tail() - Find the tail and head of the journal.
 * @repair: The repair completion.
 *
 * Return: True if there were valid journal blocks.
 */
static bool find_recovery_journal_head_and_tail(struct repair_completion *repair)
{
	struct recovery_journal *journal = repair->completion.vdo->recovery_journal;
	bool found_entries = false;
	physical_block_number_t i;

	/*
	 * Ensure that we don't replay old entries since we know the tail recorded in the super
	 * block must be a lower bound. Not doing so can result in extra data loss by setting the
	 * tail too early.
	 */
	repair->highest_tail = journal->tail;
	for (i = 0; i < journal->size; i++) {
		struct recovery_block_header header =
			get_recovery_journal_block_header(journal, repair->journal_data, i);

		if (!is_valid_recovery_journal_block(journal, &header, true)) {
			/* This block is old or incorrectly formatted */
			continue;
		}

		if (vdo_get_recovery_journal_block_number(journal, header.sequence_number) != i) {
			/* This block is in the wrong location */
			continue;
		}

		if (header.sequence_number >= repair->highest_tail) {
			found_entries = true;
			repair->highest_tail = header.sequence_number;
		}

		if (!found_entries)
			continue;

		if (header.block_map_head > repair->block_map_head)
			repair->block_map_head = header.block_map_head;

		if (header.slab_journal_head > repair->slab_journal_head)
			repair->slab_journal_head = header.slab_journal_head;
	}

	return found_entries;
}

/**
 * unpack_entry() - Unpack a recovery journal entry in either format.
 * @vdo: The vdo.
 * @packed: The entry to unpack.
 * @format: The expected format of the entry.
 * @entry: The unpacked entry.
 *
 * Return: true if the entry should be applied.3
 */
static bool unpack_entry(struct vdo *vdo, char *packed, enum vdo_metadata_type format,
			 struct recovery_journal_entry *entry)
{
	if (format == VDO_METADATA_RECOVERY_JOURNAL_2) {
		struct packed_recovery_journal_entry *packed_entry =
			(struct packed_recovery_journal_entry *) packed;

		*entry = vdo_unpack_recovery_journal_entry(packed_entry);
	} else {
		physical_block_number_t low32, high4;

		struct packed_recovery_journal_entry_1 *packed_entry =
			(struct packed_recovery_journal_entry_1 *) packed;

		if (packed_entry->operation == VDO_JOURNAL_DATA_INCREMENT)
			entry->operation = VDO_JOURNAL_DATA_REMAPPING;
		else if (packed_entry->operation == VDO_JOURNAL_BLOCK_MAP_INCREMENT)
			entry->operation = VDO_JOURNAL_BLOCK_MAP_REMAPPING;
		else
			return false;

		low32 = __le32_to_cpu(packed_entry->pbn_low_word);
		high4 = packed_entry->pbn_high_nibble;
		entry->slot = (struct block_map_slot) {
			.pbn = ((high4 << 32) | low32),
			.slot = (packed_entry->slot_low | (packed_entry->slot_high << 6)),
		};
		entry->mapping = vdo_unpack_block_map_entry(&packed_entry->block_map_entry);
		entry->unmapping = (struct data_location) {
			.pbn = VDO_ZERO_BLOCK,
			.state = VDO_MAPPING_STATE_UNMAPPED,
		};
	}

	return (validate_recovery_journal_entry(vdo, entry) == VDO_SUCCESS);
}

/**
 * append_sector_entries() - Append an array of recovery journal entries from a journal block
 *                           sector to the array of numbered mappings in the repair completion,
 *                           numbering each entry in the order they are appended.
 * @repair: The repair completion.
 * @entries: The entries in the sector.
 * @format: The format of the sector.
 * @entry_count: The number of entries to append.
 */
static void append_sector_entries(struct repair_completion *repair, char *entries,
				  enum vdo_metadata_type format,
				  journal_entry_count_t entry_count)
{
	journal_entry_count_t i;
	struct vdo *vdo = repair->completion.vdo;
	off_t increment = ((format == VDO_METADATA_RECOVERY_JOURNAL_2)
			   ? sizeof(struct packed_recovery_journal_entry)
			   : sizeof(struct packed_recovery_journal_entry_1));

	for (i = 0; i < entry_count; i++, entries += increment) {
		struct recovery_journal_entry entry;

		if (!unpack_entry(vdo, entries, format, &entry))
			/* When recovering from read-only mode, ignore damaged entries. */
			continue;

		repair->entries[repair->block_map_entry_count] =
			(struct numbered_block_mapping) {
			.block_map_slot = entry.slot,
			.block_map_entry = vdo_pack_block_map_entry(entry.mapping.pbn,
								    entry.mapping.state),
			.number = repair->block_map_entry_count,
		};
		repair->block_map_entry_count++;
	}
}

static journal_entry_count_t entries_per_sector(enum vdo_metadata_type format,
						u8 sector_number)
{
	if (format == VDO_METADATA_RECOVERY_JOURNAL_2)
		return RECOVERY_JOURNAL_ENTRIES_PER_SECTOR;

	return ((sector_number == (VDO_SECTORS_PER_BLOCK - 1))
		? RECOVERY_JOURNAL_1_ENTRIES_IN_LAST_SECTOR
		: RECOVERY_JOURNAL_1_ENTRIES_PER_SECTOR);
}

static void extract_entries_from_block(struct repair_completion *repair,
				       struct recovery_journal *journal,
				       sequence_number_t sequence,
				       enum vdo_metadata_type format,
				       journal_entry_count_t entries)
{
	sector_count_t i;
	struct recovery_block_header header =
		get_recovery_journal_block_header(journal, repair->journal_data,
						  sequence);

	if (!is_exact_recovery_journal_block(journal, &header, sequence) ||
	    (header.metadata_type != format)) {
		/* This block is invalid, so skip it. */
		return;
	}

	entries = min(entries, header.entry_count);
	for (i = 1; i < VDO_SECTORS_PER_BLOCK; i++) {
		struct packed_journal_sector *sector =
			get_sector(journal, repair->journal_data, sequence, i);
		journal_entry_count_t sector_entries =
			min(entries, entries_per_sector(format, i));

		if (vdo_is_valid_recovery_journal_sector(&header, sector, i)) {
			/* Only extract as many as the block header calls for. */
			append_sector_entries(repair, (char *) sector->entries, format,
					      min_t(journal_entry_count_t,
						    sector->entry_count,
						    sector_entries));
		}

		/*
		 * Even if the sector wasn't full, count it as full when counting up to the
		 * entry count the block header claims.
		 */
		entries -= sector_entries;
	}
}

static int parse_journal_for_rebuild(struct repair_completion *repair)
{
	int result;
	sequence_number_t i;
	block_count_t count;
	enum vdo_metadata_type format;
	struct vdo *vdo = repair->completion.vdo;
	struct recovery_journal *journal = vdo->recovery_journal;
	journal_entry_count_t entries_per_block = journal->entries_per_block;

	format = get_recovery_journal_block_header(journal, repair->journal_data,
						   repair->highest_tail).metadata_type;
	if (format == VDO_METADATA_RECOVERY_JOURNAL)
		entries_per_block = RECOVERY_JOURNAL_1_ENTRIES_PER_BLOCK;

	/*
	 * Allocate an array of numbered_block_mapping structures large enough to transcribe every
	 * packed_recovery_journal_entry from every valid journal block.
	 */
	count = ((repair->highest_tail - repair->block_map_head + 1) * entries_per_block);
	result = vdo_allocate(count, struct numbered_block_mapping, __func__,
			      &repair->entries);
	if (result != VDO_SUCCESS)
		return result;

	for (i = repair->block_map_head; i <= repair->highest_tail; i++)
		extract_entries_from_block(repair, journal, i, format, entries_per_block);

	return VDO_SUCCESS;
}

static int validate_heads(struct repair_completion *repair)
{
	/* Both reap heads must be behind the tail. */
	if ((repair->block_map_head <= repair->tail) &&
	    (repair->slab_journal_head <= repair->tail))
		return VDO_SUCCESS;


	return vdo_log_error_strerror(VDO_CORRUPT_JOURNAL,
				      "Journal tail too early. block map head: %llu, slab journal head: %llu, tail: %llu",
				      (unsigned long long) repair->block_map_head,
				      (unsigned long long) repair->slab_journal_head,
				      (unsigned long long) repair->tail);
}

/**
 * extract_new_mappings() - Find all valid new mappings to be applied to the block map.
 * @repair: The repair completion.
 *
 * The mappings are extracted from the journal and stored in a sortable array so that all of the
 * mappings to be applied to a given block map page can be done in a single page fetch.
 */
static int extract_new_mappings(struct repair_completion *repair)
{
	int result;
	struct vdo *vdo = repair->completion.vdo;
	struct recovery_point recovery_point = {
		.sequence_number = repair->block_map_head,
		.sector_count = 1,
		.entry_count = 0,
	};

	/*
	 * Allocate an array of numbered_block_mapping structs just large enough to transcribe
	 * every packed_recovery_journal_entry from every valid journal block.
	 */
	result = vdo_allocate(repair->entry_count, struct numbered_block_mapping,
			      __func__, &repair->entries);
	if (result != VDO_SUCCESS)
		return result;

	for (; before_recovery_point(&recovery_point, &repair->tail_recovery_point);
	     increment_recovery_point(&recovery_point)) {
		struct recovery_journal_entry entry = get_entry(repair, &recovery_point);

		result = validate_recovery_journal_entry(vdo, &entry);
		if (result != VDO_SUCCESS) {
			vdo_enter_read_only_mode(vdo, result);
			return result;
		}

		repair->entries[repair->block_map_entry_count] =
			(struct numbered_block_mapping) {
			.block_map_slot = entry.slot,
			.block_map_entry = vdo_pack_block_map_entry(entry.mapping.pbn,
								    entry.mapping.state),
			.number = repair->block_map_entry_count,
		};
		repair->block_map_entry_count++;
	}

	result = VDO_ASSERT((repair->block_map_entry_count <= repair->entry_count),
			    "approximate entry count is an upper bound");
	if (result != VDO_SUCCESS)
		vdo_enter_read_only_mode(vdo, result);

	return result;
}

/**
 * compute_usages() - Compute the lbns in use and block map data blocks counts from the tail of
 *                    the journal.
 * @repair: The repair completion.
 */
static noinline int compute_usages(struct repair_completion *repair)
{
	/*
	 * This function is declared noinline to avoid a spurious valgrind error regarding the
	 * following structure being uninitialized.
	 */
	struct recovery_point recovery_point = {
		.sequence_number = repair->tail,
		.sector_count = 1,
		.entry_count = 0,
	};

	struct vdo *vdo = repair->completion.vdo;
	struct recovery_journal *journal = vdo->recovery_journal;
	struct recovery_block_header header =
		get_recovery_journal_block_header(journal, repair->journal_data,
						  repair->tail);

	repair->logical_blocks_used = header.logical_blocks_used;
	repair->block_map_data_blocks = header.block_map_data_blocks;

	for (; before_recovery_point(&recovery_point, &repair->tail_recovery_point);
	     increment_recovery_point(&recovery_point)) {
		struct recovery_journal_entry entry = get_entry(repair, &recovery_point);
		int result;

		result = validate_recovery_journal_entry(vdo, &entry);
		if (result != VDO_SUCCESS) {
			vdo_enter_read_only_mode(vdo, result);
			return result;
		}

		if (entry.operation == VDO_JOURNAL_BLOCK_MAP_REMAPPING) {
			repair->block_map_data_blocks++;
			continue;
		}

		if (vdo_is_mapped_location(&entry.mapping))
			repair->logical_blocks_used++;

		if (vdo_is_mapped_location(&entry.unmapping))
			repair->logical_blocks_used--;
	}

	return VDO_SUCCESS;
}

static int parse_journal_for_recovery(struct repair_completion *repair)
{
	int result;
	sequence_number_t i, head;
	bool found_entries = false;
	struct recovery_journal *journal = repair->completion.vdo->recovery_journal;
	struct recovery_block_header header;
	enum vdo_metadata_type expected_format;

	head = min(repair->block_map_head, repair->slab_journal_head);
	header = get_recovery_journal_block_header(journal, repair->journal_data, head);
	expected_format = header.metadata_type;
	for (i = head; i <= repair->highest_tail; i++) {
		journal_entry_count_t block_entries;
		u8 j;

		repair->tail = i;
		repair->tail_recovery_point = (struct recovery_point) {
			.sequence_number = i,
			.sector_count = 0,
			.entry_count = 0,
		};

		header = get_recovery_journal_block_header(journal, repair->journal_data, i);
		if (!is_exact_recovery_journal_block(journal, &header, i)) {
			/* A bad block header was found so this must be the end of the journal. */
			break;
		} else if (header.metadata_type != expected_format) {
			/* There is a mix of old and new format blocks, so we need to rebuild. */
			vdo_log_error_strerror(VDO_CORRUPT_JOURNAL,
					       "Recovery journal is in an invalid format, a read-only rebuild is required.");
			vdo_enter_read_only_mode(repair->completion.vdo, VDO_CORRUPT_JOURNAL);
			return VDO_CORRUPT_JOURNAL;
		}

		block_entries = header.entry_count;

		/* Examine each sector in turn to determine the last valid sector. */
		for (j = 1; j < VDO_SECTORS_PER_BLOCK; j++) {
			struct packed_journal_sector *sector =
				get_sector(journal, repair->journal_data, i, j);
			journal_entry_count_t sector_entries =
				min_t(journal_entry_count_t, sector->entry_count,
				      block_entries);

			/* A bad sector means that this block was torn. */
			if (!vdo_is_valid_recovery_journal_sector(&header, sector, j))
				break;

			if (sector_entries > 0) {
				found_entries = true;
				repair->tail_recovery_point.sector_count++;
				repair->tail_recovery_point.entry_count = sector_entries;
				block_entries -= sector_entries;
				repair->entry_count += sector_entries;
			}

			/* If this sector is short, the later sectors can't matter. */
			if ((sector_entries < RECOVERY_JOURNAL_ENTRIES_PER_SECTOR) ||
			    (block_entries == 0))
				break;
		}

		/* If this block was not filled, or if it tore, no later block can matter. */
		if ((header.entry_count != journal->entries_per_block) || (block_entries > 0))
			break;
	}

	if (!found_entries) {
		return validate_heads(repair);
	} else if (expected_format == VDO_METADATA_RECOVERY_JOURNAL) {
		/* All journal blocks have the old format, so we need to upgrade. */
		vdo_log_error_strerror(VDO_UNSUPPORTED_VERSION,
				       "Recovery journal is in the old format. Downgrade and complete recovery, then upgrade with a clean volume");
		return VDO_UNSUPPORTED_VERSION;
	}

	/* Set the tail to the last valid tail block, if there is one. */
	if (repair->tail_recovery_point.sector_count == 0)
		repair->tail--;

	result = validate_heads(repair);
	if (result != VDO_SUCCESS)
		return result;

	vdo_log_info("Highest-numbered recovery journal block has sequence number %llu, and the highest-numbered usable block is %llu",
		     (unsigned long long) repair->highest_tail,
		     (unsigned long long) repair->tail);

	result = extract_new_mappings(repair);
	if (result != VDO_SUCCESS)
		return result;

	return compute_usages(repair);
}

static int parse_journal(struct repair_completion *repair)
{
	if (!find_recovery_journal_head_and_tail(repair))
		return VDO_SUCCESS;

	return (vdo_state_requires_read_only_rebuild(repair->completion.vdo->load_state) ?
		parse_journal_for_rebuild(repair) :
		parse_journal_for_recovery(repair));
}

static void finish_journal_load(struct vdo_completion *completion)
{
	struct repair_completion *repair = completion->parent;

	if (++repair->vios_complete != repair->vio_count)
		return;

	vdo_log_info("Finished reading recovery journal");
	uninitialize_vios(repair);
	prepare_repair_completion(repair, recover_block_map, VDO_ZONE_TYPE_LOGICAL);
	vdo_continue_completion(&repair->completion, parse_journal(repair));
}

static void handle_journal_load_error(struct vdo_completion *completion)
{
	struct repair_completion *repair = completion->parent;

	/* Preserve the error */
	vdo_set_completion_result(&repair->completion, completion->result);
	vio_record_metadata_io_error(as_vio(completion));
	completion->callback(completion);
}

static void read_journal_endio(struct bio *bio)
{
	struct vio *vio = bio->bi_private;
	struct vdo *vdo = vio->completion.vdo;

	continue_vio_after_io(vio, finish_journal_load, vdo->thread_config.admin_thread);
}

/**
 * vdo_repair() - Load the recovery journal and then recover or rebuild a vdo.
 * @parent: The completion to notify when the operation is complete
 */
void vdo_repair(struct vdo_completion *parent)
{
	int result;
	char *ptr;
	struct repair_completion *repair;
	struct vdo *vdo = parent->vdo;
	struct recovery_journal *journal = vdo->recovery_journal;
	physical_block_number_t pbn = journal->origin;
	block_count_t remaining = journal->size;
	block_count_t vio_count = DIV_ROUND_UP(remaining, MAX_BLOCKS_PER_VIO);
	page_count_t page_count = min_t(page_count_t,
					vdo->device_config->cache_size >> 1,
					MAXIMUM_SIMULTANEOUS_VDO_BLOCK_MAP_RESTORATION_READS);

	vdo_assert_on_admin_thread(vdo, __func__);

	if (vdo->load_state == VDO_FORCE_REBUILD) {
		vdo_log_warning("Rebuilding reference counts to clear read-only mode");
		vdo->states.vdo.read_only_recoveries++;
	} else if (vdo->load_state == VDO_REBUILD_FOR_UPGRADE) {
		vdo_log_warning("Rebuilding reference counts for upgrade");
	} else {
		vdo_log_warning("Device was dirty, rebuilding reference counts");
	}

	result = vdo_allocate_extended(struct repair_completion, page_count,
				       struct vdo_page_completion, __func__,
				       &repair);
	if (result != VDO_SUCCESS) {
		vdo_fail_completion(parent, result);
		return;
	}

	vdo_initialize_completion(&repair->completion, vdo, VDO_REPAIR_COMPLETION);
	repair->completion.error_handler = abort_repair;
	repair->completion.parent = parent;
	prepare_repair_completion(repair, finish_repair, VDO_ZONE_TYPE_ADMIN);
	repair->page_count = page_count;

	result = vdo_allocate(remaining * VDO_BLOCK_SIZE, char, __func__,
			      &repair->journal_data);
	if (abort_on_error(result, repair))
		return;

	result = vdo_allocate(vio_count, struct vio, __func__, &repair->vios);
	if (abort_on_error(result, repair))
		return;

	ptr = repair->journal_data;
	for (repair->vio_count = 0; repair->vio_count < vio_count; repair->vio_count++) {
		block_count_t blocks = min_t(block_count_t, remaining,
					     MAX_BLOCKS_PER_VIO);

		result = allocate_vio_components(vdo, VIO_TYPE_RECOVERY_JOURNAL,
						 VIO_PRIORITY_METADATA,
						 repair, blocks, ptr,
						 &repair->vios[repair->vio_count]);
		if (abort_on_error(result, repair))
			return;

		ptr += (blocks * VDO_BLOCK_SIZE);
		remaining -= blocks;
	}

	for (vio_count = 0; vio_count < repair->vio_count;
	     vio_count++, pbn += MAX_BLOCKS_PER_VIO) {
		vdo_submit_metadata_vio(&repair->vios[vio_count], pbn, read_journal_endio,
					handle_journal_load_error, REQ_OP_READ);
	}
}
