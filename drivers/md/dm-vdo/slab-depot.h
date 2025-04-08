/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef VDO_SLAB_DEPOT_H
#define VDO_SLAB_DEPOT_H

#include <linux/atomic.h>
#include <linux/dm-kcopyd.h>
#include <linux/list.h>

#include "numeric.h"

#include "admin-state.h"
#include "completion.h"
#include "data-vio.h"
#include "encodings.h"
#include "physical-zone.h"
#include "priority-table.h"
#include "recovery-journal.h"
#include "statistics.h"
#include "types.h"
#include "vio.h"
#include "wait-queue.h"

/*
 * A slab_depot is responsible for managing all of the slabs and block allocators of a VDO. It has
 * a single array of slabs in order to eliminate the need for additional math in order to compute
 * which physical zone a PBN is in. It also has a block_allocator per zone.
 *
 * Each physical zone has a single dedicated queue and thread for performing all updates to the
 * slabs assigned to that zone. The concurrency guarantees of this single-threaded model allow the
 * code to omit more fine-grained locking for the various slab structures. Each physical zone
 * maintains a separate copy of the slab summary to remove the need for explicit locking on that
 * structure as well.
 *
 * Load operations must be performed on the admin thread. Normal operations, such as allocations
 * and reference count updates, must be performed on the appropriate physical zone thread. Requests
 * from the recovery journal to commit slab journal tail blocks must be scheduled from the recovery
 * journal thread to run on the appropriate physical zone thread. Save operations must be launched
 * from the same admin thread as the original load operation.
 */

enum {
	/* The number of vios in the vio pool is proportional to the throughput of the VDO. */
	BLOCK_ALLOCATOR_VIO_POOL_SIZE = 128,

	/*
	 * The number of vios in the vio pool used for loading reference count data. A slab's
	 * refcounts is capped at ~8MB, and we process one at a time in a zone, so 9 should be
	 * plenty.
	 */
	BLOCK_ALLOCATOR_REFCOUNT_VIO_POOL_SIZE = 9,
};

/*
 * Represents the possible status of a block.
 */
enum reference_status {
	RS_FREE, /* this block is free */
	RS_SINGLE, /* this block is singly-referenced */
	RS_SHARED, /* this block is shared */
	RS_PROVISIONAL /* this block is provisionally allocated */
};

struct vdo_slab;

struct journal_lock {
	u16 count;
	sequence_number_t recovery_start;
};

struct slab_journal {
	/* A waiter object for getting a VIO pool entry */
	struct vdo_waiter resource_waiter;
	/* A waiter object for updating the slab summary */
	struct vdo_waiter slab_summary_waiter;
	/* A waiter object for getting a vio with which to flush */
	struct vdo_waiter flush_waiter;
	/* The queue of VIOs waiting to make an entry */
	struct vdo_wait_queue entry_waiters;
	/* The parent slab reference of this journal */
	struct vdo_slab *slab;

	/* Whether a tail block commit is pending */
	bool waiting_to_commit;
	/* Whether the journal is updating the slab summary */
	bool updating_slab_summary;
	/* Whether the journal is adding entries from the entry_waiters queue */
	bool adding_entries;
	/* Whether a partial write is in progress */
	bool partial_write_in_progress;

	/* The oldest block in the journal on disk */
	sequence_number_t head;
	/* The oldest block in the journal which may not be reaped */
	sequence_number_t unreapable;
	/* The end of the half-open interval of the active journal */
	sequence_number_t tail;
	/* The next journal block to be committed */
	sequence_number_t next_commit;
	/* The tail sequence number that is written in the slab summary */
	sequence_number_t summarized;
	/* The tail sequence number that was last summarized in slab summary */
	sequence_number_t last_summarized;

	/* The sequence number of the recovery journal lock */
	sequence_number_t recovery_lock;

	/*
	 * The number of entries which fit in a single block. Can't use the constant because unit
	 * tests change this number.
	 */
	journal_entry_count_t entries_per_block;
	/*
	 * The number of full entries which fit in a single block. Can't use the constant because
	 * unit tests change this number.
	 */
	journal_entry_count_t full_entries_per_block;

	/* The recovery journal of the VDO (slab journal holds locks on it) */
	struct recovery_journal *recovery_journal;

	/* The statistics shared by all slab journals in our physical zone */
	struct slab_journal_statistics *events;
	/* A list of the VIO pool entries for outstanding journal block writes */
	struct list_head uncommitted_blocks;

	/*
	 * The current tail block header state. This will be packed into the block just before it
	 * is written.
	 */
	struct slab_journal_block_header tail_header;
	/* A pointer to a block-sized buffer holding the packed block data */
	struct packed_slab_journal_block *block;

	/* The number of blocks in the on-disk journal */
	block_count_t size;
	/* The number of blocks at which to start pushing reference blocks */
	block_count_t flushing_threshold;
	/* The number of blocks at which all reference blocks should be writing */
	block_count_t flushing_deadline;
	/* The number of blocks at which to wait for reference blocks to write */
	block_count_t blocking_threshold;
	/* The number of blocks at which to scrub the slab before coming online */
	block_count_t scrubbing_threshold;

	/* This list entry is for block_allocator to keep a queue of dirty journals */
	struct list_head dirty_entry;

	/* The lock for the oldest unreaped block of the journal */
	struct journal_lock *reap_lock;
	/* The locks for each on disk block */
	struct journal_lock *locks;
};

/*
 * Reference_block structure
 *
 * Blocks are used as a proxy, permitting saves of partial refcounts.
 */
struct reference_block {
	/* This block waits on the ref_counts to tell it to write */
	struct vdo_waiter waiter;
	/* The slab to which this reference_block belongs */
	struct vdo_slab *slab;
	/* The number of references in this block that represent allocations */
	block_size_t allocated_count;
	/* The slab journal block on which this block must hold a lock */
	sequence_number_t slab_journal_lock;
	/* The slab journal block which should be released when this block is committed */
	sequence_number_t slab_journal_lock_to_release;
	/* The point up to which each sector is accurate on disk */
	struct journal_point commit_points[VDO_SECTORS_PER_BLOCK];
	/* Whether this block has been modified since it was written to disk */
	bool is_dirty;
	/* Whether this block is currently writing */
	bool is_writing;
};

/* The search_cursor represents the saved position of a free block search. */
struct search_cursor {
	/* The reference block containing the current search index */
	struct reference_block *block;
	/* The position at which to start searching for the next free counter */
	slab_block_number index;
	/* The position just past the last valid counter in the current block */
	slab_block_number end_index;

	/* A pointer to the first reference block in the slab */
	struct reference_block *first_block;
	/* A pointer to the last reference block in the slab */
	struct reference_block *last_block;
};

enum slab_rebuild_status {
	VDO_SLAB_REBUILT,
	VDO_SLAB_REPLAYING,
	VDO_SLAB_REQUIRES_SCRUBBING,
	VDO_SLAB_REQUIRES_HIGH_PRIORITY_SCRUBBING,
	VDO_SLAB_REBUILDING,
};

/*
 * This is the type declaration for the vdo_slab type. A vdo_slab currently consists of a run of
 * 2^23 data blocks, but that will soon change to dedicate a small number of those blocks for
 * metadata storage for the reference counts and slab journal for the slab.
 *
 * A reference count is maintained for each physical block number. The vast majority of blocks have
 * a very small reference count (usually 0 or 1). For references less than or equal to MAXIMUM_REFS
 * (254) the reference count is stored in counters[pbn].
 */
struct vdo_slab {
	/* A list entry to queue this slab in a block_allocator list */
	struct list_head allocq_entry;

	/* The struct block_allocator that owns this slab */
	struct block_allocator *allocator;

	/* The journal for this slab */
	struct slab_journal journal;

	/* The slab number of this slab */
	slab_count_t slab_number;
	/* The offset in the allocator partition of the first block in this slab */
	physical_block_number_t start;
	/* The offset of the first block past the end of this slab */
	physical_block_number_t end;
	/* The starting translated PBN of the slab journal */
	physical_block_number_t journal_origin;
	/* The starting translated PBN of the reference counts */
	physical_block_number_t ref_counts_origin;

	/* The administrative state of the slab */
	struct admin_state state;
	/* The status of the slab */
	enum slab_rebuild_status status;
	/* Whether the slab was ever queued for scrubbing */
	bool was_queued_for_scrubbing;

	/* The priority at which this slab has been queued for allocation */
	u8 priority;

	/* Fields beyond this point are the reference counts for the data blocks in this slab. */
	/* The size of the counters array */
	u32 block_count;
	/* The number of free blocks */
	u32 free_blocks;
	/* The array of reference counts */
	vdo_refcount_t *counters; /* use vdo_allocate() to align data ptr */

	/* The saved block pointer and array indexes for the free block search */
	struct search_cursor search_cursor;

	/* A list of the dirty blocks waiting to be written out */
	struct vdo_wait_queue dirty_blocks;
	/* The number of blocks which are currently reading or writing */
	size_t active_count;

	/* A waiter object for updating the slab summary */
	struct vdo_waiter summary_waiter;

	/* The latest slab journal for which there has been a reference count update */
	struct journal_point slab_journal_point;

	/* The number of reference count blocks */
	u32 reference_block_count;
	/* reference count block array */
	struct reference_block *reference_blocks;
};

enum block_allocator_drain_step {
	VDO_DRAIN_ALLOCATOR_START,
	VDO_DRAIN_ALLOCATOR_STEP_SCRUBBER,
	VDO_DRAIN_ALLOCATOR_STEP_SLABS,
	VDO_DRAIN_ALLOCATOR_STEP_SUMMARY,
	VDO_DRAIN_ALLOCATOR_STEP_FINISHED,
};

struct slab_scrubber {
	/* The queue of slabs to scrub first */
	struct list_head high_priority_slabs;
	/* The queue of slabs to scrub once there are no high_priority_slabs */
	struct list_head slabs;
	/* The queue of VIOs waiting for a slab to be scrubbed */
	struct vdo_wait_queue waiters;

	/*
	 * The number of slabs that are unrecovered or being scrubbed. This field is modified by
	 * the physical zone thread, but is queried by other threads.
	 */
	slab_count_t slab_count;

	/* The administrative state of the scrubber */
	struct admin_state admin_state;
	/* Whether to only scrub high-priority slabs */
	bool high_priority_only;
	/* The slab currently being scrubbed */
	struct vdo_slab *slab;
	/* The vio for loading slab journal blocks */
	struct vio vio;
};

/* A sub-structure for applying actions in parallel to all an allocator's slabs. */
struct slab_actor {
	/* The number of slabs performing a slab action */
	slab_count_t slab_action_count;
	/* The method to call when a slab action has been completed by all slabs */
	vdo_action_fn callback;
};

/* A slab_iterator is a structure for iterating over a set of slabs. */
struct slab_iterator {
	struct vdo_slab **slabs;
	struct vdo_slab *next;
	slab_count_t end;
	slab_count_t stride;
};

/*
 * The slab_summary provides hints during load and recovery about the state of the slabs in order
 * to avoid the need to read the slab journals in their entirety before a VDO can come online.
 *
 * The information in the summary for each slab includes the rough number of free blocks (which is
 * used to prioritize scrubbing), the cleanliness of a slab (so that clean slabs containing free
 * space will be used on restart), and the location of the tail block of the slab's journal.
 *
 * The slab_summary has its own partition at the end of the volume which is sized to allow for a
 * complete copy of the summary for each of up to 16 physical zones.
 *
 * During resize, the slab_summary moves its backing partition and is saved once moved; the
 * slab_summary is not permitted to overwrite the previous recovery journal space.
 *
 * The slab_summary does not have its own version information, but relies on the VDO volume version
 * number.
 */

/*
 * A slab status is a very small structure for use in determining the ordering of slabs in the
 * scrubbing process.
 */
struct slab_status {
	slab_count_t slab_number;
	bool is_clean;
	u8 emptiness;
};

struct slab_summary_block {
	/* The block_allocator to which this block belongs */
	struct block_allocator *allocator;
	/* The index of this block in its zone's summary */
	block_count_t index;
	/* Whether this block has a write outstanding */
	bool writing;
	/* Ring of updates waiting on the outstanding write */
	struct vdo_wait_queue current_update_waiters;
	/* Ring of updates waiting on the next write */
	struct vdo_wait_queue next_update_waiters;
	/* The active slab_summary_entry array for this block */
	struct slab_summary_entry *entries;
	/* The vio used to write this block */
	struct vio vio;
	/* The packed entries, one block long, backing the vio */
	char *outgoing_entries;
};

/*
 * The statistics for all the slab summary zones owned by this slab summary. These fields are all
 * mutated only by their physical zone threads, but are read by other threads when gathering
 * statistics for the entire depot.
 */
struct atomic_slab_summary_statistics {
	/* Number of blocks written */
	atomic64_t blocks_written;
};

struct block_allocator {
	struct vdo_completion completion;
	/* The slab depot for this allocator */
	struct slab_depot *depot;
	/* The nonce of the VDO */
	nonce_t nonce;
	/* The physical zone number of this allocator */
	zone_count_t zone_number;
	/* The thread ID for this allocator's physical zone */
	thread_id_t thread_id;
	/* The number of slabs in this allocator */
	slab_count_t slab_count;
	/* The number of the last slab owned by this allocator */
	slab_count_t last_slab;
	/* The reduced priority level used to preserve unopened slabs */
	unsigned int unopened_slab_priority;
	/* The state of this allocator */
	struct admin_state state;
	/* The actor for applying an action to all slabs */
	struct slab_actor slab_actor;

	/* The slab from which blocks are currently being allocated */
	struct vdo_slab *open_slab;
	/* A priority queue containing all slabs available for allocation */
	struct priority_table *prioritized_slabs;
	/* The slab scrubber */
	struct slab_scrubber scrubber;
	/* What phase of the close operation the allocator is to perform */
	enum block_allocator_drain_step drain_step;

	/*
	 * These statistics are all mutated only by the physical zone thread, but are read by other
	 * threads when gathering statistics for the entire depot.
	 */
	/*
	 * The count of allocated blocks in this zone. Not in block_allocator_statistics for
	 * historical reasons.
	 */
	u64 allocated_blocks;
	/* Statistics for this block allocator */
	struct block_allocator_statistics statistics;
	/* Cumulative statistics for the slab journals in this zone */
	struct slab_journal_statistics slab_journal_statistics;
	/* Cumulative statistics for the reference counters in this zone */
	struct ref_counts_statistics ref_counts_statistics;

	/*
	 * This is the head of a queue of slab journals which have entries in their tail blocks
	 * which have not yet started to commit. When the recovery journal is under space pressure,
	 * slab journals which have uncommitted entries holding a lock on the recovery journal head
	 * are forced to commit their blocks early. This list is kept in order, with the tail
	 * containing the slab journal holding the most recent recovery journal lock.
	 */
	struct list_head dirty_slab_journals;

	/* The vio pool for reading and writing block allocator metadata */
	struct vio_pool *vio_pool;
	/* The vio pool for large initial reads of ref count areas */
	struct vio_pool *refcount_big_vio_pool;
	/* How many ref count blocks are read per vio at initial load */
	u32 refcount_blocks_per_big_vio;
	/* The dm_kcopyd client for erasing slab journals */
	struct dm_kcopyd_client *eraser;
	/* Iterator over the slabs to be erased */
	struct slab_iterator slabs_to_erase;

	/* The portion of the slab summary managed by this allocator */
	/* The state of the slab summary */
	struct admin_state summary_state;
	/* The number of outstanding summary writes */
	block_count_t summary_write_count;
	/* The array (owned by the blocks) of all entries */
	struct slab_summary_entry *summary_entries;
	/* The array of slab_summary_blocks */
	struct slab_summary_block *summary_blocks;
};

enum slab_depot_load_type {
	VDO_SLAB_DEPOT_NORMAL_LOAD,
	VDO_SLAB_DEPOT_RECOVERY_LOAD,
	VDO_SLAB_DEPOT_REBUILD_LOAD
};

struct slab_depot {
	zone_count_t zone_count;
	zone_count_t old_zone_count;
	struct vdo *vdo;
	struct slab_config slab_config;
	struct action_manager *action_manager;

	physical_block_number_t first_block;
	physical_block_number_t last_block;
	physical_block_number_t origin;

	/* slab_size == (1 << slab_size_shift) */
	unsigned int slab_size_shift;

	/* Determines how slabs should be queued during load */
	enum slab_depot_load_type load_type;

	/* The state for notifying slab journals to release recovery journal */
	sequence_number_t active_release_request;
	sequence_number_t new_release_request;

	/* State variables for scrubbing complete handling */
	atomic_t zones_to_scrub;

	/* Array of pointers to individually allocated slabs */
	struct vdo_slab **slabs;
	/* The number of slabs currently allocated and stored in 'slabs' */
	slab_count_t slab_count;

	/* Array of pointers to a larger set of slabs (used during resize) */
	struct vdo_slab **new_slabs;
	/* The number of slabs currently allocated and stored in 'new_slabs' */
	slab_count_t new_slab_count;
	/* The size that 'new_slabs' was allocated for */
	block_count_t new_size;

	/* The last block before resize, for rollback */
	physical_block_number_t old_last_block;
	/* The last block after resize, for resize */
	physical_block_number_t new_last_block;

	/* The statistics for the slab summary */
	struct atomic_slab_summary_statistics summary_statistics;
	/* The start of the slab summary partition */
	physical_block_number_t summary_origin;
	/* The number of bits to shift to get a 7-bit fullness hint */
	unsigned int hint_shift;
	/* The slab summary entries for all of the zones the partition can hold */
	struct slab_summary_entry *summary_entries;

	/* The block allocators for this depot */
	struct block_allocator allocators[];
};

struct reference_updater;

bool __must_check vdo_attempt_replay_into_slab(struct vdo_slab *slab,
					       physical_block_number_t pbn,
					       enum journal_operation operation,
					       bool increment,
					       struct journal_point *recovery_point,
					       struct vdo_completion *parent);

int __must_check vdo_adjust_reference_count_for_rebuild(struct slab_depot *depot,
							physical_block_number_t pbn,
							enum journal_operation operation);

static inline struct block_allocator *vdo_as_block_allocator(struct vdo_completion *completion)
{
	vdo_assert_completion_type(completion, VDO_BLOCK_ALLOCATOR_COMPLETION);
	return container_of(completion, struct block_allocator, completion);
}

int __must_check vdo_acquire_provisional_reference(struct vdo_slab *slab,
						   physical_block_number_t pbn,
						   struct pbn_lock *lock);

int __must_check vdo_allocate_block(struct block_allocator *allocator,
				    physical_block_number_t *block_number_ptr);

int vdo_enqueue_clean_slab_waiter(struct block_allocator *allocator,
				  struct vdo_waiter *waiter);

void vdo_modify_reference_count(struct vdo_completion *completion,
				struct reference_updater *updater);

int __must_check vdo_release_block_reference(struct block_allocator *allocator,
					     physical_block_number_t pbn);

void vdo_notify_slab_journals_are_recovered(struct vdo_completion *completion);

void vdo_dump_block_allocator(const struct block_allocator *allocator);

int __must_check vdo_decode_slab_depot(struct slab_depot_state_2_0 state,
				       struct vdo *vdo,
				       struct partition *summary_partition,
				       struct slab_depot **depot_ptr);

void vdo_free_slab_depot(struct slab_depot *depot);

struct slab_depot_state_2_0 __must_check vdo_record_slab_depot(const struct slab_depot *depot);

int __must_check vdo_allocate_reference_counters(struct slab_depot *depot);

struct vdo_slab * __must_check vdo_get_slab(const struct slab_depot *depot,
					    physical_block_number_t pbn);

u8 __must_check vdo_get_increment_limit(struct slab_depot *depot,
					physical_block_number_t pbn);

bool __must_check vdo_is_physical_data_block(const struct slab_depot *depot,
					     physical_block_number_t pbn);

block_count_t __must_check vdo_get_slab_depot_allocated_blocks(const struct slab_depot *depot);

block_count_t __must_check vdo_get_slab_depot_data_blocks(const struct slab_depot *depot);

void vdo_get_slab_depot_statistics(const struct slab_depot *depot,
				   struct vdo_statistics *stats);

void vdo_load_slab_depot(struct slab_depot *depot,
			 const struct admin_state_code *operation,
			 struct vdo_completion *parent, void *context);

void vdo_prepare_slab_depot_to_allocate(struct slab_depot *depot,
					enum slab_depot_load_type load_type,
					struct vdo_completion *parent);

void vdo_update_slab_depot_size(struct slab_depot *depot);

int __must_check vdo_prepare_to_grow_slab_depot(struct slab_depot *depot,
						const struct partition *partition);

void vdo_use_new_slabs(struct slab_depot *depot, struct vdo_completion *parent);

void vdo_abandon_new_slabs(struct slab_depot *depot);

void vdo_drain_slab_depot(struct slab_depot *depot,
			  const struct admin_state_code *operation,
			  struct vdo_completion *parent);

void vdo_resume_slab_depot(struct slab_depot *depot, struct vdo_completion *parent);

void vdo_commit_oldest_slab_journal_tail_blocks(struct slab_depot *depot,
						sequence_number_t recovery_block_number);

void vdo_scrub_all_unrecovered_slabs(struct slab_depot *depot,
				     struct vdo_completion *parent);

void vdo_dump_slab_depot(const struct slab_depot *depot);

#endif /* VDO_SLAB_DEPOT_H */
