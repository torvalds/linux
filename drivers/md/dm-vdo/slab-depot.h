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
	struct waiter resource_waiter;
	/* A waiter object for updating the slab summary */
	struct waiter slab_summary_waiter;
	/* A waiter object for getting a vio with which to flush */
	struct waiter flush_waiter;
	/* The queue of VIOs waiting to make an entry */
	struct wait_queue entry_waiters;
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
	struct waiter waiter;
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
	vdo_refcount_t *counters; /* use uds_allocate() to align data ptr */

	/* The saved block pointer and array indexes for the free block search */
	struct search_cursor search_cursor;

	/* A list of the dirty blocks waiting to be written out */
	struct wait_queue dirty_blocks;
	/* The number of blocks which are currently writing */
	size_t active_count;

	/* A waiter object for updating the slab summary */
	struct waiter summary_waiter;

	/* The latest slab journal for which there has been a reference count update */
	struct journal_point slab_journal_point;

	/* The number of reference count blocks */
	u32 reference_block_count;
	/* reference count block array */
	struct reference_block *reference_blocks;
};

bool __must_check vdo_attempt_replay_into_slab(struct vdo_slab *slab,
					       physical_block_number_t pbn,
					       enum journal_operation operation,
					       bool increment,
					       struct journal_point *recovery_point,
					       struct vdo_completion *parent);

void vdo_notify_slab_journals_are_recovered(struct vdo_completion *completion);

#endif /* VDO_SLAB_DEPOT_H */
