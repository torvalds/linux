// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "slab-depot.h"

#include <linux/atomic.h>
#include <linux/bio.h>
#include <linux/err.h>
#include <linux/log2.h>
#include <linux/min_heap.h>
#include <linux/minmax.h>

#include "logger.h"
#include "memory-alloc.h"
#include "numeric.h"
#include "permassert.h"
#include "string-utils.h"

#include "action-manager.h"
#include "admin-state.h"
#include "completion.h"
#include "constants.h"
#include "data-vio.h"
#include "encodings.h"
#include "io-submitter.h"
#include "physical-zone.h"
#include "priority-table.h"
#include "recovery-journal.h"
#include "repair.h"
#include "status-codes.h"
#include "types.h"
#include "vdo.h"
#include "vio.h"
#include "wait-queue.h"

static const u64 BYTES_PER_WORD = sizeof(u64);
static const bool NORMAL_OPERATION = true;

/**
 * get_lock() - Get the lock object for a slab journal block by sequence number.
 * @journal: vdo_slab journal to retrieve from.
 * @sequence_number: Sequence number of the block.
 *
 * Return: The lock object for the given sequence number.
 */
static inline struct journal_lock * __must_check get_lock(struct slab_journal *journal,
							  sequence_number_t sequence_number)
{
	return &journal->locks[sequence_number % journal->size];
}

static bool is_slab_open(struct vdo_slab *slab)
{
	return (!vdo_is_state_quiescing(&slab->state) &&
		!vdo_is_state_quiescent(&slab->state));
}

/**
 * must_make_entries_to_flush() - Check whether there are entry waiters which should delay a flush.
 * @journal: The journal to check.
 *
 * Return: true if there are no entry waiters, or if the slab is unrecovered.
 */
static inline bool __must_check must_make_entries_to_flush(struct slab_journal *journal)
{
	return ((journal->slab->status != VDO_SLAB_REBUILDING) &&
		vdo_waitq_has_waiters(&journal->entry_waiters));
}

/**
 * is_reaping() - Check whether a reap is currently in progress.
 * @journal: The journal which may be reaping.
 *
 * Return: true if the journal is reaping.
 */
static inline bool __must_check is_reaping(struct slab_journal *journal)
{
	return (journal->head != journal->unreapable);
}

/**
 * initialize_tail_block() - Initialize tail block as a new block.
 * @journal: The journal whose tail block is being initialized.
 */
static void initialize_tail_block(struct slab_journal *journal)
{
	struct slab_journal_block_header *header = &journal->tail_header;

	header->sequence_number = journal->tail;
	header->entry_count = 0;
	header->has_block_map_increments = false;
}

/**
 * initialize_journal_state() - Set all journal fields appropriately to start journaling.
 * @journal: The journal to be reset, based on its tail sequence number.
 */
static void initialize_journal_state(struct slab_journal *journal)
{
	journal->unreapable = journal->head;
	journal->reap_lock = get_lock(journal, journal->unreapable);
	journal->next_commit = journal->tail;
	journal->summarized = journal->last_summarized = journal->tail;
	initialize_tail_block(journal);
}

/**
 * block_is_full() - Check whether a journal block is full.
 * @journal: The slab journal for the block.
 *
 * Return: true if the tail block is full.
 */
static bool __must_check block_is_full(struct slab_journal *journal)
{
	journal_entry_count_t count = journal->tail_header.entry_count;

	return (journal->tail_header.has_block_map_increments ?
		(journal->full_entries_per_block == count) :
		(journal->entries_per_block == count));
}

static void add_entries(struct slab_journal *journal);
static void update_tail_block_location(struct slab_journal *journal);
static void release_journal_locks(struct vdo_waiter *waiter, void *context);

/**
 * is_slab_journal_blank() - Check whether a slab's journal is blank.
 *
 * A slab journal is blank if it has never had any entries recorded in it.
 *
 * Return: true if the slab's journal has never been modified.
 */
static bool is_slab_journal_blank(const struct vdo_slab *slab)
{
	return ((slab->journal.tail == 1) &&
		(slab->journal.tail_header.entry_count == 0));
}

/**
 * mark_slab_journal_dirty() - Put a slab journal on the dirty ring of its allocator in the correct
 *                             order.
 * @journal: The journal to be marked dirty.
 * @lock: The recovery journal lock held by the slab journal.
 */
static void mark_slab_journal_dirty(struct slab_journal *journal, sequence_number_t lock)
{
	struct slab_journal *dirty_journal;
	struct list_head *dirty_list = &journal->slab->allocator->dirty_slab_journals;

	VDO_ASSERT_LOG_ONLY(journal->recovery_lock == 0, "slab journal was clean");

	journal->recovery_lock = lock;
	list_for_each_entry_reverse(dirty_journal, dirty_list, dirty_entry) {
		if (dirty_journal->recovery_lock <= journal->recovery_lock)
			break;
	}

	list_move_tail(&journal->dirty_entry, dirty_journal->dirty_entry.next);
}

static void mark_slab_journal_clean(struct slab_journal *journal)
{
	journal->recovery_lock = 0;
	list_del_init(&journal->dirty_entry);
}

static void check_if_slab_drained(struct vdo_slab *slab)
{
	bool read_only;
	struct slab_journal *journal = &slab->journal;
	const struct admin_state_code *code;

	if (!vdo_is_state_draining(&slab->state) ||
	    must_make_entries_to_flush(journal) ||
	    is_reaping(journal) ||
	    journal->waiting_to_commit ||
	    !list_empty(&journal->uncommitted_blocks) ||
	    journal->updating_slab_summary ||
	    (slab->active_count > 0))
		return;

	/* When not suspending or recovering, the slab must be clean. */
	code = vdo_get_admin_state_code(&slab->state);
	read_only = vdo_is_read_only(slab->allocator->depot->vdo);
	if (!read_only &&
	    vdo_waitq_has_waiters(&slab->dirty_blocks) &&
	    (code != VDO_ADMIN_STATE_SUSPENDING) &&
	    (code != VDO_ADMIN_STATE_RECOVERING))
		return;

	vdo_finish_draining_with_result(&slab->state,
					(read_only ? VDO_READ_ONLY : VDO_SUCCESS));
}

/* FULLNESS HINT COMPUTATION */

/**
 * compute_fullness_hint() - Translate a slab's free block count into a 'fullness hint' that can be
 *                           stored in a slab_summary_entry's 7 bits that are dedicated to its free
 *                           count.
 * @depot: The depot whose summary being updated.
 * @free_blocks: The number of free blocks.
 *
 * Note: the number of free blocks must be strictly less than 2^23 blocks, even though
 * theoretically slabs could contain precisely 2^23 blocks; there is an assumption that at least
 * one block is used by metadata. This assumption is necessary; otherwise, the fullness hint might
 * overflow. The fullness hint formula is roughly (fullness >> 16) & 0x7f, but (2^23 >> 16) & 0x7f
 * is 0, which would make it impossible to distinguish completely full from completely empty.
 *
 * Return: A fullness hint, which can be stored in 7 bits.
 */
static u8 __must_check compute_fullness_hint(struct slab_depot *depot,
					     block_count_t free_blocks)
{
	block_count_t hint;

	VDO_ASSERT_LOG_ONLY((free_blocks < (1 << 23)), "free blocks must be less than 2^23");

	if (free_blocks == 0)
		return 0;

	hint = free_blocks >> depot->hint_shift;
	return ((hint == 0) ? 1 : hint);
}

/**
 * check_summary_drain_complete() - Check whether an allocators summary has finished draining.
 */
static void check_summary_drain_complete(struct block_allocator *allocator)
{
	if (!vdo_is_state_draining(&allocator->summary_state) ||
	    (allocator->summary_write_count > 0))
		return;

	vdo_finish_operation(&allocator->summary_state,
			     (vdo_is_read_only(allocator->depot->vdo) ?
			      VDO_READ_ONLY : VDO_SUCCESS));
}

/**
 * notify_summary_waiters() - Wake all the waiters in a given queue.
 * @allocator: The block allocator summary which owns the queue.
 * @queue: The queue to notify.
 */
static void notify_summary_waiters(struct block_allocator *allocator,
				   struct vdo_wait_queue *queue)
{
	int result = (vdo_is_read_only(allocator->depot->vdo) ?
		      VDO_READ_ONLY : VDO_SUCCESS);

	vdo_waitq_notify_all_waiters(queue, NULL, &result);
}

static void launch_write(struct slab_summary_block *summary_block);

/**
 * finish_updating_slab_summary_block() - Finish processing a block which attempted to write,
 *                                        whether or not the attempt succeeded.
 * @block: The block.
 */
static void finish_updating_slab_summary_block(struct slab_summary_block *block)
{
	notify_summary_waiters(block->allocator, &block->current_update_waiters);
	block->writing = false;
	block->allocator->summary_write_count--;
	if (vdo_waitq_has_waiters(&block->next_update_waiters))
		launch_write(block);
	else
		check_summary_drain_complete(block->allocator);
}

/**
 * finish_update() - This is the callback for a successful summary block write.
 * @completion: The write vio.
 */
static void finish_update(struct vdo_completion *completion)
{
	struct slab_summary_block *block =
		container_of(as_vio(completion), struct slab_summary_block, vio);

	atomic64_inc(&block->allocator->depot->summary_statistics.blocks_written);
	finish_updating_slab_summary_block(block);
}

/**
 * handle_write_error() - Handle an error writing a slab summary block.
 * @completion: The write VIO.
 */
static void handle_write_error(struct vdo_completion *completion)
{
	struct slab_summary_block *block =
		container_of(as_vio(completion), struct slab_summary_block, vio);

	vio_record_metadata_io_error(as_vio(completion));
	vdo_enter_read_only_mode(completion->vdo, completion->result);
	finish_updating_slab_summary_block(block);
}

static void write_slab_summary_endio(struct bio *bio)
{
	struct vio *vio = bio->bi_private;
	struct slab_summary_block *block =
		container_of(vio, struct slab_summary_block, vio);

	continue_vio_after_io(vio, finish_update, block->allocator->thread_id);
}

/**
 * launch_write() - Write a slab summary block unless it is currently out for writing.
 * @block: The block that needs to be committed.
 */
static void launch_write(struct slab_summary_block *block)
{
	struct block_allocator *allocator = block->allocator;
	struct slab_depot *depot = allocator->depot;
	physical_block_number_t pbn;

	if (block->writing)
		return;

	allocator->summary_write_count++;
	vdo_waitq_transfer_all_waiters(&block->next_update_waiters,
				       &block->current_update_waiters);
	block->writing = true;

	if (vdo_is_read_only(depot->vdo)) {
		finish_updating_slab_summary_block(block);
		return;
	}

	memcpy(block->outgoing_entries, block->entries, VDO_BLOCK_SIZE);

	/*
	 * Flush before writing to ensure that the slab journal tail blocks and reference updates
	 * covered by this summary update are stable. Otherwise, a subsequent recovery could
	 * encounter a slab summary update that refers to a slab journal tail block that has not
	 * actually been written. In such cases, the slab journal referenced will be treated as
	 * empty, causing any data within the slab which predates the existing recovery journal
	 * entries to be lost.
	 */
	pbn = (depot->summary_origin +
	       (VDO_SLAB_SUMMARY_BLOCKS_PER_ZONE * allocator->zone_number) +
	       block->index);
	vdo_submit_metadata_vio(&block->vio, pbn, write_slab_summary_endio,
				handle_write_error, REQ_OP_WRITE | REQ_PREFLUSH);
}

/**
 * update_slab_summary_entry() - Update the entry for a slab.
 * @slab: The slab whose entry is to be updated
 * @waiter: The waiter that is updating the summary.
 * @tail_block_offset: The offset of the slab journal's tail block.
 * @load_ref_counts: Whether the reference counts must be loaded from disk on the vdo load.
 * @is_clean: Whether the slab is clean.
 * @free_blocks: The number of free blocks.
 */
static void update_slab_summary_entry(struct vdo_slab *slab, struct vdo_waiter *waiter,
				      tail_block_offset_t tail_block_offset,
				      bool load_ref_counts, bool is_clean,
				      block_count_t free_blocks)
{
	u8 index = slab->slab_number / VDO_SLAB_SUMMARY_ENTRIES_PER_BLOCK;
	struct block_allocator *allocator = slab->allocator;
	struct slab_summary_block *block = &allocator->summary_blocks[index];
	int result;
	struct slab_summary_entry *entry;

	if (vdo_is_read_only(block->vio.completion.vdo)) {
		result = VDO_READ_ONLY;
		waiter->callback(waiter, &result);
		return;
	}

	if (vdo_is_state_draining(&allocator->summary_state) ||
	    vdo_is_state_quiescent(&allocator->summary_state)) {
		result = VDO_INVALID_ADMIN_STATE;
		waiter->callback(waiter, &result);
		return;
	}

	entry = &allocator->summary_entries[slab->slab_number];
	*entry = (struct slab_summary_entry) {
		.tail_block_offset = tail_block_offset,
		.load_ref_counts = (entry->load_ref_counts || load_ref_counts),
		.is_dirty = !is_clean,
		.fullness_hint = compute_fullness_hint(allocator->depot, free_blocks),
	};
	vdo_waitq_enqueue_waiter(&block->next_update_waiters, waiter);
	launch_write(block);
}

/**
 * finish_reaping() - Actually advance the head of the journal now that any necessary flushes are
 *                    complete.
 * @journal: The journal to be reaped.
 */
static void finish_reaping(struct slab_journal *journal)
{
	journal->head = journal->unreapable;
	add_entries(journal);
	check_if_slab_drained(journal->slab);
}

static void reap_slab_journal(struct slab_journal *journal);

/**
 * complete_reaping() - Finish reaping now that we have flushed the lower layer and then try
 *                      reaping again in case we deferred reaping due to an outstanding vio.
 * @completion: The flush vio.
 */
static void complete_reaping(struct vdo_completion *completion)
{
	struct slab_journal *journal = completion->parent;

	return_vio_to_pool(journal->slab->allocator->vio_pool,
			   vio_as_pooled_vio(as_vio(vdo_forget(completion))));
	finish_reaping(journal);
	reap_slab_journal(journal);
}

/**
 * handle_flush_error() - Handle an error flushing the lower layer.
 * @completion: The flush vio.
 */
static void handle_flush_error(struct vdo_completion *completion)
{
	vio_record_metadata_io_error(as_vio(completion));
	vdo_enter_read_only_mode(completion->vdo, completion->result);
	complete_reaping(completion);
}

static void flush_endio(struct bio *bio)
{
	struct vio *vio = bio->bi_private;
	struct slab_journal *journal = vio->completion.parent;

	continue_vio_after_io(vio, complete_reaping,
			      journal->slab->allocator->thread_id);
}

/**
 * flush_for_reaping() - A waiter callback for getting a vio with which to flush the lower layer
 *                       prior to reaping.
 * @waiter: The journal as a flush waiter.
 * @context: The newly acquired flush vio.
 */
static void flush_for_reaping(struct vdo_waiter *waiter, void *context)
{
	struct slab_journal *journal =
		container_of(waiter, struct slab_journal, flush_waiter);
	struct pooled_vio *pooled = context;
	struct vio *vio = &pooled->vio;

	vio->completion.parent = journal;
	vdo_submit_flush_vio(vio, flush_endio, handle_flush_error);
}

/**
 * reap_slab_journal() - Conduct a reap on a slab journal to reclaim unreferenced blocks.
 * @journal: The slab journal.
 */
static void reap_slab_journal(struct slab_journal *journal)
{
	bool reaped = false;

	if (is_reaping(journal)) {
		/* We already have a reap in progress so wait for it to finish. */
		return;
	}

	if ((journal->slab->status != VDO_SLAB_REBUILT) ||
	    !vdo_is_state_normal(&journal->slab->state) ||
	    vdo_is_read_only(journal->slab->allocator->depot->vdo)) {
		/*
		 * We must not reap in the first two cases, and there's no point in read-only mode.
		 */
		return;
	}

	/*
	 * Start reclaiming blocks only when the journal head has no references. Then stop when a
	 * block is referenced or reap reaches the most recently written block, referenced by the
	 * slab summary, which has the sequence number just before the tail.
	 */
	while ((journal->unreapable < journal->tail) && (journal->reap_lock->count == 0)) {
		reaped = true;
		journal->unreapable++;
		journal->reap_lock++;
		if (journal->reap_lock == &journal->locks[journal->size])
			journal->reap_lock = &journal->locks[0];
	}

	if (!reaped)
		return;

	/*
	 * It is never safe to reap a slab journal block without first issuing a flush, regardless
	 * of whether a user flush has been received or not. In the absence of the flush, the
	 * reference block write which released the locks allowing the slab journal to reap may not
	 * be persisted. Although slab summary writes will eventually issue flushes, multiple slab
	 * journal block writes can be issued while previous slab summary updates have not yet been
	 * made. Even though those slab journal block writes will be ignored if the slab summary
	 * update is not persisted, they may still overwrite the to-be-reaped slab journal block
	 * resulting in a loss of reference count updates.
	 */
	journal->flush_waiter.callback = flush_for_reaping;
	acquire_vio_from_pool(journal->slab->allocator->vio_pool,
			      &journal->flush_waiter);
}

/**
 * adjust_slab_journal_block_reference() - Adjust the reference count for a slab journal block.
 * @journal: The slab journal.
 * @sequence_number: The journal sequence number of the referenced block.
 * @adjustment: Amount to adjust the reference counter.
 *
 * Note that when the adjustment is negative, the slab journal will be reaped.
 */
static void adjust_slab_journal_block_reference(struct slab_journal *journal,
						sequence_number_t sequence_number,
						int adjustment)
{
	struct journal_lock *lock;

	if (sequence_number == 0)
		return;

	if (journal->slab->status == VDO_SLAB_REPLAYING) {
		/* Locks should not be used during offline replay. */
		return;
	}

	VDO_ASSERT_LOG_ONLY((adjustment != 0), "adjustment must be non-zero");
	lock = get_lock(journal, sequence_number);
	if (adjustment < 0) {
		VDO_ASSERT_LOG_ONLY((-adjustment <= lock->count),
				    "adjustment %d of lock count %u for slab journal block %llu must not underflow",
				    adjustment, lock->count,
				    (unsigned long long) sequence_number);
	}

	lock->count += adjustment;
	if (lock->count == 0)
		reap_slab_journal(journal);
}

/**
 * release_journal_locks() - Callback invoked after a slab summary update completes.
 * @waiter: The slab summary waiter that has just been notified.
 * @context: The result code of the update.
 *
 * Registered in the constructor on behalf of update_tail_block_location().
 *
 * Implements waiter_callback_fn.
 */
static void release_journal_locks(struct vdo_waiter *waiter, void *context)
{
	sequence_number_t first, i;
	struct slab_journal *journal =
		container_of(waiter, struct slab_journal, slab_summary_waiter);
	int result = *((int *) context);

	if (result != VDO_SUCCESS) {
		if (result != VDO_READ_ONLY) {
			/*
			 * Don't bother logging what might be lots of errors if we are already in
			 * read-only mode.
			 */
			vdo_log_error_strerror(result, "failed slab summary update %llu",
					       (unsigned long long) journal->summarized);
		}

		journal->updating_slab_summary = false;
		vdo_enter_read_only_mode(journal->slab->allocator->depot->vdo, result);
		check_if_slab_drained(journal->slab);
		return;
	}

	if (journal->partial_write_in_progress && (journal->summarized == journal->tail)) {
		journal->partial_write_in_progress = false;
		add_entries(journal);
	}

	first = journal->last_summarized;
	journal->last_summarized = journal->summarized;
	for (i = journal->summarized - 1; i >= first; i--) {
		/*
		 * Release the lock the summarized block held on the recovery journal. (During
		 * replay, recovery_start will always be 0.)
		 */
		if (journal->recovery_journal != NULL) {
			zone_count_t zone_number = journal->slab->allocator->zone_number;
			struct journal_lock *lock = get_lock(journal, i);

			vdo_release_recovery_journal_block_reference(journal->recovery_journal,
								     lock->recovery_start,
								     VDO_ZONE_TYPE_PHYSICAL,
								     zone_number);
		}

		/*
		 * Release our own lock against reaping for blocks that are committed. (This
		 * function will not change locks during replay.)
		 */
		adjust_slab_journal_block_reference(journal, i, -1);
	}

	journal->updating_slab_summary = false;

	reap_slab_journal(journal);

	/* Check if the slab summary needs to be updated again. */
	update_tail_block_location(journal);
}

/**
 * update_tail_block_location() - Update the tail block location in the slab summary, if necessary.
 * @journal: The slab journal that is updating its tail block location.
 */
static void update_tail_block_location(struct slab_journal *journal)
{
	block_count_t free_block_count;
	struct vdo_slab *slab = journal->slab;

	if (journal->updating_slab_summary ||
	    vdo_is_read_only(journal->slab->allocator->depot->vdo) ||
	    (journal->last_summarized >= journal->next_commit)) {
		check_if_slab_drained(slab);
		return;
	}

	if (slab->status != VDO_SLAB_REBUILT) {
		u8 hint = slab->allocator->summary_entries[slab->slab_number].fullness_hint;

		free_block_count = ((block_count_t) hint) << slab->allocator->depot->hint_shift;
	} else {
		free_block_count = slab->free_blocks;
	}

	journal->summarized = journal->next_commit;
	journal->updating_slab_summary = true;

	/*
	 * Update slab summary as dirty.
	 * vdo_slab journal can only reap past sequence number 1 when all the ref counts for this
	 * slab have been written to the layer. Therefore, indicate that the ref counts must be
	 * loaded when the journal head has reaped past sequence number 1.
	 */
	update_slab_summary_entry(slab, &journal->slab_summary_waiter,
				  journal->summarized % journal->size,
				  (journal->head > 1), false, free_block_count);
}

/**
 * reopen_slab_journal() - Reopen a slab's journal by emptying it and then adding pending entries.
 */
static void reopen_slab_journal(struct vdo_slab *slab)
{
	struct slab_journal *journal = &slab->journal;
	sequence_number_t block;

	VDO_ASSERT_LOG_ONLY(journal->tail_header.entry_count == 0,
			    "vdo_slab journal's active block empty before reopening");
	journal->head = journal->tail;
	initialize_journal_state(journal);

	/* Ensure no locks are spuriously held on an empty journal. */
	for (block = 1; block <= journal->size; block++) {
		VDO_ASSERT_LOG_ONLY((get_lock(journal, block)->count == 0),
				    "Scrubbed journal's block %llu is not locked",
				    (unsigned long long) block);
	}

	add_entries(journal);
}

static sequence_number_t get_committing_sequence_number(const struct pooled_vio *vio)
{
	const struct packed_slab_journal_block *block =
		(const struct packed_slab_journal_block *) vio->vio.data;

	return __le64_to_cpu(block->header.sequence_number);
}

/**
 * complete_write() - Handle post-commit processing.
 * @completion: The write vio as a completion.
 *
 * This is the callback registered by write_slab_journal_block().
 */
static void complete_write(struct vdo_completion *completion)
{
	int result = completion->result;
	struct pooled_vio *pooled = vio_as_pooled_vio(as_vio(completion));
	struct slab_journal *journal = completion->parent;
	sequence_number_t committed = get_committing_sequence_number(pooled);

	list_del_init(&pooled->list_entry);
	return_vio_to_pool(journal->slab->allocator->vio_pool, vdo_forget(pooled));

	if (result != VDO_SUCCESS) {
		vio_record_metadata_io_error(as_vio(completion));
		vdo_log_error_strerror(result, "cannot write slab journal block %llu",
				       (unsigned long long) committed);
		vdo_enter_read_only_mode(journal->slab->allocator->depot->vdo, result);
		check_if_slab_drained(journal->slab);
		return;
	}

	WRITE_ONCE(journal->events->blocks_written, journal->events->blocks_written + 1);

	if (list_empty(&journal->uncommitted_blocks)) {
		/* If no blocks are outstanding, then the commit point is at the tail. */
		journal->next_commit = journal->tail;
	} else {
		/* The commit point is always the beginning of the oldest incomplete block. */
		pooled = container_of(journal->uncommitted_blocks.next,
				      struct pooled_vio, list_entry);
		journal->next_commit = get_committing_sequence_number(pooled);
	}

	update_tail_block_location(journal);
}

static void write_slab_journal_endio(struct bio *bio)
{
	struct vio *vio = bio->bi_private;
	struct slab_journal *journal = vio->completion.parent;

	continue_vio_after_io(vio, complete_write, journal->slab->allocator->thread_id);
}

/**
 * write_slab_journal_block() - Write a slab journal block.
 * @waiter: The vio pool waiter which was just notified.
 * @context: The vio pool entry for the write.
 *
 * Callback from acquire_vio_from_pool() registered in commit_tail().
 */
static void write_slab_journal_block(struct vdo_waiter *waiter, void *context)
{
	struct pooled_vio *pooled = context;
	struct vio *vio = &pooled->vio;
	struct slab_journal *journal =
		container_of(waiter, struct slab_journal, resource_waiter);
	struct slab_journal_block_header *header = &journal->tail_header;
	int unused_entries = journal->entries_per_block - header->entry_count;
	physical_block_number_t block_number;
	const struct admin_state_code *operation;

	header->head = journal->head;
	list_add_tail(&pooled->list_entry, &journal->uncommitted_blocks);
	vdo_pack_slab_journal_block_header(header, &journal->block->header);

	/* Copy the tail block into the vio. */
	memcpy(pooled->vio.data, journal->block, VDO_BLOCK_SIZE);

	VDO_ASSERT_LOG_ONLY(unused_entries >= 0, "vdo_slab journal block is not overfull");
	if (unused_entries > 0) {
		/*
		 * Release the per-entry locks for any unused entries in the block we are about to
		 * write.
		 */
		adjust_slab_journal_block_reference(journal, header->sequence_number,
						    -unused_entries);
		journal->partial_write_in_progress = !block_is_full(journal);
	}

	block_number = journal->slab->journal_origin +
		(header->sequence_number % journal->size);
	vio->completion.parent = journal;

	/*
	 * This block won't be read in recovery until the slab summary is updated to refer to it.
	 * The slab summary update does a flush which is sufficient to protect us from corruption
	 * due to out of order slab journal, reference block, or block map writes.
	 */
	vdo_submit_metadata_vio(vdo_forget(vio), block_number, write_slab_journal_endio,
				complete_write, REQ_OP_WRITE);

	/* Since the write is submitted, the tail block structure can be reused. */
	journal->tail++;
	initialize_tail_block(journal);
	journal->waiting_to_commit = false;

	operation = vdo_get_admin_state_code(&journal->slab->state);
	if (operation == VDO_ADMIN_STATE_WAITING_FOR_RECOVERY) {
		vdo_finish_operation(&journal->slab->state,
				     (vdo_is_read_only(journal->slab->allocator->depot->vdo) ?
				      VDO_READ_ONLY : VDO_SUCCESS));
		return;
	}

	add_entries(journal);
}

/**
 * commit_tail() - Commit the tail block of the slab journal.
 * @journal: The journal whose tail block should be committed.
 */
static void commit_tail(struct slab_journal *journal)
{
	if ((journal->tail_header.entry_count == 0) && must_make_entries_to_flush(journal)) {
		/*
		 * There are no entries at the moment, but there are some waiters, so defer
		 * initiating the flush until those entries are ready to write.
		 */
		return;
	}

	if (vdo_is_read_only(journal->slab->allocator->depot->vdo) ||
	    journal->waiting_to_commit ||
	    (journal->tail_header.entry_count == 0)) {
		/*
		 * There is nothing to do since the tail block is empty, or writing, or the journal
		 * is in read-only mode.
		 */
		return;
	}

	/*
	 * Since we are about to commit the tail block, this journal no longer needs to be on the
	 * ring of journals which the recovery journal might ask to commit.
	 */
	mark_slab_journal_clean(journal);

	journal->waiting_to_commit = true;

	journal->resource_waiter.callback = write_slab_journal_block;
	acquire_vio_from_pool(journal->slab->allocator->vio_pool,
			      &journal->resource_waiter);
}

/**
 * encode_slab_journal_entry() - Encode a slab journal entry.
 * @tail_header: The unpacked header for the block.
 * @payload: The journal block payload to hold the entry.
 * @sbn: The slab block number of the entry to encode.
 * @operation: The type of the entry.
 * @increment: True if this is an increment.
 *
 * Exposed for unit tests.
 */
static void encode_slab_journal_entry(struct slab_journal_block_header *tail_header,
				      slab_journal_payload *payload,
				      slab_block_number sbn,
				      enum journal_operation operation,
				      bool increment)
{
	journal_entry_count_t entry_number = tail_header->entry_count++;

	if (operation == VDO_JOURNAL_BLOCK_MAP_REMAPPING) {
		if (!tail_header->has_block_map_increments) {
			memset(payload->full_entries.entry_types, 0,
			       VDO_SLAB_JOURNAL_ENTRY_TYPES_SIZE);
			tail_header->has_block_map_increments = true;
		}

		payload->full_entries.entry_types[entry_number / 8] |=
			((u8)1 << (entry_number % 8));
	}

	vdo_pack_slab_journal_entry(&payload->entries[entry_number], sbn, increment);
}

/**
 * expand_journal_point() - Convert a recovery journal journal_point which refers to both an
 *                          increment and a decrement to a single point which refers to one or the
 *                          other.
 * @recovery_point: The journal point to convert.
 * @increment: Whether the current entry is an increment.
 *
 * Return: The expanded journal point
 *
 * Because each data_vio has but a single recovery journal point, but may need to make both
 * increment and decrement entries in the same slab journal. In order to distinguish the two
 * entries, the entry count of the expanded journal point is twice the actual recovery journal
 * entry count for increments, and one more than that for decrements.
 */
static struct journal_point expand_journal_point(struct journal_point recovery_point,
						 bool increment)
{
	recovery_point.entry_count *= 2;
	if (!increment)
		recovery_point.entry_count++;

	return recovery_point;
}

/**
 * add_entry() - Actually add an entry to the slab journal, potentially firing off a write if a
 *               block becomes full.
 * @journal: The slab journal to append to.
 * @pbn: The pbn being adjusted.
 * @operation: The type of entry to make.
 * @increment: True if this is an increment.
 * @recovery_point: The expanded recovery point.
 *
 * This function is synchronous.
 */
static void add_entry(struct slab_journal *journal, physical_block_number_t pbn,
		      enum journal_operation operation, bool increment,
		      struct journal_point recovery_point)
{
	struct packed_slab_journal_block *block = journal->block;
	int result;

	result = VDO_ASSERT(vdo_before_journal_point(&journal->tail_header.recovery_point,
						     &recovery_point),
			    "recovery journal point is monotonically increasing, recovery point: %llu.%u, block recovery point: %llu.%u",
			    (unsigned long long) recovery_point.sequence_number,
			    recovery_point.entry_count,
			    (unsigned long long) journal->tail_header.recovery_point.sequence_number,
			    journal->tail_header.recovery_point.entry_count);
	if (result != VDO_SUCCESS) {
		vdo_enter_read_only_mode(journal->slab->allocator->depot->vdo, result);
		return;
	}

	if (operation == VDO_JOURNAL_BLOCK_MAP_REMAPPING) {
		result = VDO_ASSERT((journal->tail_header.entry_count <
				     journal->full_entries_per_block),
				    "block has room for full entries");
		if (result != VDO_SUCCESS) {
			vdo_enter_read_only_mode(journal->slab->allocator->depot->vdo,
						 result);
			return;
		}
	}

	encode_slab_journal_entry(&journal->tail_header, &block->payload,
				  pbn - journal->slab->start, operation, increment);
	journal->tail_header.recovery_point = recovery_point;
	if (block_is_full(journal))
		commit_tail(journal);
}

static inline block_count_t journal_length(const struct slab_journal *journal)
{
	return journal->tail - journal->head;
}

/**
 * vdo_attempt_replay_into_slab() - Replay a recovery journal entry into a slab's journal.
 * @slab: The slab to play into.
 * @pbn: The PBN for the entry.
 * @operation: The type of entry to add.
 * @increment: True if this entry is an increment.
 * @recovery_point: The recovery journal point corresponding to this entry.
 * @parent: The completion to notify when there is space to add the entry if the entry could not be
 *          added immediately.
 *
 * Return: true if the entry was added immediately.
 */
bool vdo_attempt_replay_into_slab(struct vdo_slab *slab, physical_block_number_t pbn,
				  enum journal_operation operation, bool increment,
				  struct journal_point *recovery_point,
				  struct vdo_completion *parent)
{
	struct slab_journal *journal = &slab->journal;
	struct slab_journal_block_header *header = &journal->tail_header;
	struct journal_point expanded = expand_journal_point(*recovery_point, increment);

	/* Only accept entries after the current recovery point. */
	if (!vdo_before_journal_point(&journal->tail_header.recovery_point, &expanded))
		return true;

	if ((header->entry_count >= journal->full_entries_per_block) &&
	    (header->has_block_map_increments || (operation == VDO_JOURNAL_BLOCK_MAP_REMAPPING))) {
		/*
		 * The tail block does not have room for the entry we are attempting to add so
		 * commit the tail block now.
		 */
		commit_tail(journal);
	}

	if (journal->waiting_to_commit) {
		vdo_start_operation_with_waiter(&journal->slab->state,
						VDO_ADMIN_STATE_WAITING_FOR_RECOVERY,
						parent, NULL);
		return false;
	}

	if (journal_length(journal) >= journal->size) {
		/*
		 * We must have reaped the current head before the crash, since the blocked
		 * threshold keeps us from having more entries than fit in a slab journal; hence we
		 * can just advance the head (and unreapable block), as needed.
		 */
		journal->head++;
		journal->unreapable++;
	}

	if (journal->slab->status == VDO_SLAB_REBUILT)
		journal->slab->status = VDO_SLAB_REPLAYING;

	add_entry(journal, pbn, operation, increment, expanded);
	return true;
}

/**
 * requires_reaping() - Check whether the journal must be reaped before adding new entries.
 * @journal: The journal to check.
 *
 * Return: true if the journal must be reaped.
 */
static bool requires_reaping(const struct slab_journal *journal)
{
	return (journal_length(journal) >= journal->blocking_threshold);
}

/** finish_summary_update() - A waiter callback that resets the writing state of a slab. */
static void finish_summary_update(struct vdo_waiter *waiter, void *context)
{
	struct vdo_slab *slab = container_of(waiter, struct vdo_slab, summary_waiter);
	int result = *((int *) context);

	slab->active_count--;

	if ((result != VDO_SUCCESS) && (result != VDO_READ_ONLY)) {
		vdo_log_error_strerror(result, "failed to update slab summary");
		vdo_enter_read_only_mode(slab->allocator->depot->vdo, result);
	}

	check_if_slab_drained(slab);
}

static void write_reference_block(struct vdo_waiter *waiter, void *context);

/**
 * launch_reference_block_write() - Launch the write of a dirty reference block by first acquiring
 *                                  a VIO for it from the pool.
 * @waiter: The waiter of the block which is starting to write.
 * @context: The parent slab of the block.
 *
 * This can be asynchronous since the writer will have to wait if all VIOs in the pool are
 * currently in use.
 */
static void launch_reference_block_write(struct vdo_waiter *waiter, void *context)
{
	struct vdo_slab *slab = context;

	if (vdo_is_read_only(slab->allocator->depot->vdo))
		return;

	slab->active_count++;
	container_of(waiter, struct reference_block, waiter)->is_writing = true;
	waiter->callback = write_reference_block;
	acquire_vio_from_pool(slab->allocator->vio_pool, waiter);
}

static void save_dirty_reference_blocks(struct vdo_slab *slab)
{
	vdo_waitq_notify_all_waiters(&slab->dirty_blocks,
				     launch_reference_block_write, slab);
	check_if_slab_drained(slab);
}

/**
 * finish_reference_block_write() - After a reference block has written, clean it, release its
 *                                  locks, and return its VIO to the pool.
 * @completion: The VIO that just finished writing.
 */
static void finish_reference_block_write(struct vdo_completion *completion)
{
	struct vio *vio = as_vio(completion);
	struct pooled_vio *pooled = vio_as_pooled_vio(vio);
	struct reference_block *block = completion->parent;
	struct vdo_slab *slab = block->slab;
	tail_block_offset_t offset;

	slab->active_count--;

	/* Release the slab journal lock. */
	adjust_slab_journal_block_reference(&slab->journal,
					    block->slab_journal_lock_to_release, -1);
	return_vio_to_pool(slab->allocator->vio_pool, pooled);

	/*
	 * We can't clear the is_writing flag earlier as releasing the slab journal lock may cause
	 * us to be dirtied again, but we don't want to double enqueue.
	 */
	block->is_writing = false;

	if (vdo_is_read_only(completion->vdo)) {
		check_if_slab_drained(slab);
		return;
	}

	/* Re-queue the block if it was re-dirtied while it was writing. */
	if (block->is_dirty) {
		vdo_waitq_enqueue_waiter(&block->slab->dirty_blocks, &block->waiter);
		if (vdo_is_state_draining(&slab->state)) {
			/* We must be saving, and this block will otherwise not be relaunched. */
			save_dirty_reference_blocks(slab);
		}

		return;
	}

	/*
	 * Mark the slab as clean in the slab summary if there are no dirty or writing blocks
	 * and no summary update in progress.
	 */
	if ((slab->active_count > 0) || vdo_waitq_has_waiters(&slab->dirty_blocks)) {
		check_if_slab_drained(slab);
		return;
	}

	offset = slab->allocator->summary_entries[slab->slab_number].tail_block_offset;
	slab->active_count++;
	slab->summary_waiter.callback = finish_summary_update;
	update_slab_summary_entry(slab, &slab->summary_waiter, offset,
				  true, true, slab->free_blocks);
}

/**
 * get_reference_counters_for_block() - Find the reference counters for a given block.
 * @block: The reference_block in question.
 *
 * Return: A pointer to the reference counters for this block.
 */
static vdo_refcount_t * __must_check get_reference_counters_for_block(struct reference_block *block)
{
	size_t block_index = block - block->slab->reference_blocks;

	return &block->slab->counters[block_index * COUNTS_PER_BLOCK];
}

/**
 * pack_reference_block() - Copy data from a reference block to a buffer ready to be written out.
 * @block: The block to copy.
 * @buffer: The char buffer to fill with the packed block.
 */
static void pack_reference_block(struct reference_block *block, void *buffer)
{
	struct packed_reference_block *packed = buffer;
	vdo_refcount_t *counters = get_reference_counters_for_block(block);
	sector_count_t i;
	struct packed_journal_point commit_point;

	vdo_pack_journal_point(&block->slab->slab_journal_point, &commit_point);

	for (i = 0; i < VDO_SECTORS_PER_BLOCK; i++) {
		packed->sectors[i].commit_point = commit_point;
		memcpy(packed->sectors[i].counts, counters + (i * COUNTS_PER_SECTOR),
		       (sizeof(vdo_refcount_t) * COUNTS_PER_SECTOR));
	}
}

static void write_reference_block_endio(struct bio *bio)
{
	struct vio *vio = bio->bi_private;
	struct reference_block *block = vio->completion.parent;
	thread_id_t thread_id = block->slab->allocator->thread_id;

	continue_vio_after_io(vio, finish_reference_block_write, thread_id);
}

/**
 * handle_io_error() - Handle an I/O error reading or writing a reference count block.
 * @completion: The VIO doing the I/O as a completion.
 */
static void handle_io_error(struct vdo_completion *completion)
{
	int result = completion->result;
	struct vio *vio = as_vio(completion);
	struct vdo_slab *slab = ((struct reference_block *) completion->parent)->slab;

	vio_record_metadata_io_error(vio);
	return_vio_to_pool(slab->allocator->vio_pool, vio_as_pooled_vio(vio));
	slab->active_count--;
	vdo_enter_read_only_mode(slab->allocator->depot->vdo, result);
	check_if_slab_drained(slab);
}

/**
 * write_reference_block() - After a dirty block waiter has gotten a VIO from the VIO pool, copy
 *                           its counters and associated data into the VIO, and launch the write.
 * @waiter: The waiter of the dirty block.
 * @context: The VIO returned by the pool.
 */
static void write_reference_block(struct vdo_waiter *waiter, void *context)
{
	size_t block_offset;
	physical_block_number_t pbn;
	struct pooled_vio *pooled = context;
	struct vdo_completion *completion = &pooled->vio.completion;
	struct reference_block *block = container_of(waiter, struct reference_block,
						     waiter);

	pack_reference_block(block, pooled->vio.data);
	block_offset = (block - block->slab->reference_blocks);
	pbn = (block->slab->ref_counts_origin + block_offset);
	block->slab_journal_lock_to_release = block->slab_journal_lock;
	completion->parent = block;

	/*
	 * Mark the block as clean, since we won't be committing any updates that happen after this
	 * moment. As long as VIO order is preserved, two VIOs updating this block at once will not
	 * cause complications.
	 */
	block->is_dirty = false;

	/*
	 * Flush before writing to ensure that the recovery journal and slab journal entries which
	 * cover this reference update are stable. This prevents data corruption that can be caused
	 * by out of order writes.
	 */
	WRITE_ONCE(block->slab->allocator->ref_counts_statistics.blocks_written,
		   block->slab->allocator->ref_counts_statistics.blocks_written + 1);

	completion->callback_thread_id = ((struct block_allocator *) pooled->context)->thread_id;
	vdo_submit_metadata_vio(&pooled->vio, pbn, write_reference_block_endio,
				handle_io_error, REQ_OP_WRITE | REQ_PREFLUSH);
}

static void reclaim_journal_space(struct slab_journal *journal)
{
	block_count_t length = journal_length(journal);
	struct vdo_slab *slab = journal->slab;
	block_count_t write_count = vdo_waitq_num_waiters(&slab->dirty_blocks);
	block_count_t written;

	if ((length < journal->flushing_threshold) || (write_count == 0))
		return;

	/* The slab journal is over the first threshold, schedule some reference block writes. */
	WRITE_ONCE(journal->events->flush_count, journal->events->flush_count + 1);
	if (length < journal->flushing_deadline) {
		/* Schedule more writes the closer to the deadline we get. */
		write_count /= journal->flushing_deadline - length + 1;
		write_count = max_t(block_count_t, write_count, 1);
	}

	for (written = 0; written < write_count; written++) {
		vdo_waitq_notify_next_waiter(&slab->dirty_blocks,
					     launch_reference_block_write, slab);
	}
}

/**
 * reference_count_to_status() - Convert a reference count to a reference status.
 * @count: The count to convert.
 *
 * Return: The appropriate reference status.
 */
static enum reference_status __must_check reference_count_to_status(vdo_refcount_t count)
{
	if (count == EMPTY_REFERENCE_COUNT)
		return RS_FREE;
	else if (count == 1)
		return RS_SINGLE;
	else if (count == PROVISIONAL_REFERENCE_COUNT)
		return RS_PROVISIONAL;
	else
		return RS_SHARED;
}

/**
 * dirty_block() - Mark a reference count block as dirty, potentially adding it to the dirty queue
 *                 if it wasn't already dirty.
 * @block: The reference block to mark as dirty.
 */
static void dirty_block(struct reference_block *block)
{
	if (block->is_dirty)
		return;

	block->is_dirty = true;
	if (!block->is_writing)
		vdo_waitq_enqueue_waiter(&block->slab->dirty_blocks, &block->waiter);
}

/**
 * get_reference_block() - Get the reference block that covers the given block index.
 */
static struct reference_block * __must_check get_reference_block(struct vdo_slab *slab,
								 slab_block_number index)
{
	return &slab->reference_blocks[index / COUNTS_PER_BLOCK];
}

/**
 * slab_block_number_from_pbn() - Determine the index within the slab of a particular physical
 *                                block number.
 * @slab: The slab.
 * @physical_block_number: The physical block number.
 * @slab_block_number_ptr: A pointer to the slab block number.
 *
 * Return: VDO_SUCCESS or an error code.
 */
static int __must_check slab_block_number_from_pbn(struct vdo_slab *slab,
						   physical_block_number_t pbn,
						   slab_block_number *slab_block_number_ptr)
{
	u64 slab_block_number;

	if (pbn < slab->start)
		return VDO_OUT_OF_RANGE;

	slab_block_number = pbn - slab->start;
	if (slab_block_number >= slab->allocator->depot->slab_config.data_blocks)
		return VDO_OUT_OF_RANGE;

	*slab_block_number_ptr = slab_block_number;
	return VDO_SUCCESS;
}

/**
 * get_reference_counter() - Get the reference counter that covers the given physical block number.
 * @slab: The slab to query.
 * @pbn: The physical block number.
 * @counter_ptr: A pointer to the reference counter.
 */
static int __must_check get_reference_counter(struct vdo_slab *slab,
					      physical_block_number_t pbn,
					      vdo_refcount_t **counter_ptr)
{
	slab_block_number index;
	int result = slab_block_number_from_pbn(slab, pbn, &index);

	if (result != VDO_SUCCESS)
		return result;

	*counter_ptr = &slab->counters[index];

	return VDO_SUCCESS;
}

static unsigned int calculate_slab_priority(struct vdo_slab *slab)
{
	block_count_t free_blocks = slab->free_blocks;
	unsigned int unopened_slab_priority = slab->allocator->unopened_slab_priority;
	unsigned int priority;

	/*
	 * Wholly full slabs must be the only ones with lowest priority, 0.
	 *
	 * Slabs that have never been opened (empty, newly initialized, and never been written to)
	 * have lower priority than previously opened slabs that have a significant number of free
	 * blocks. This ranking causes VDO to avoid writing physical blocks for the first time
	 * unless there are very few free blocks that have been previously written to.
	 *
	 * Since VDO doesn't discard blocks currently, reusing previously written blocks makes VDO
	 * a better client of any underlying storage that is thinly-provisioned (though discarding
	 * would be better).
	 *
	 * For all other slabs, the priority is derived from the logarithm of the number of free
	 * blocks. Slabs with the same order of magnitude of free blocks have the same priority.
	 * With 2^23 blocks, the priority will range from 1 to 25. The reserved
	 * unopened_slab_priority divides the range and is skipped by the logarithmic mapping.
	 */

	if (free_blocks == 0)
		return 0;

	if (is_slab_journal_blank(slab))
		return unopened_slab_priority;

	priority = (1 + ilog2(free_blocks));
	return ((priority < unopened_slab_priority) ? priority : priority + 1);
}

/*
 * Slabs are essentially prioritized by an approximation of the number of free blocks in the slab
 * so slabs with lots of free blocks will be opened for allocation before slabs that have few free
 * blocks.
 */
static void prioritize_slab(struct vdo_slab *slab)
{
	VDO_ASSERT_LOG_ONLY(list_empty(&slab->allocq_entry),
			    "a slab must not already be on a ring when prioritizing");
	slab->priority = calculate_slab_priority(slab);
	vdo_priority_table_enqueue(slab->allocator->prioritized_slabs,
				   slab->priority, &slab->allocq_entry);
}

/**
 * adjust_free_block_count() - Adjust the free block count and (if needed) reprioritize the slab.
 * @incremented: true if the free block count went up.
 */
static void adjust_free_block_count(struct vdo_slab *slab, bool incremented)
{
	struct block_allocator *allocator = slab->allocator;

	WRITE_ONCE(allocator->allocated_blocks,
		   allocator->allocated_blocks + (incremented ? -1 : 1));

	/* The open slab doesn't need to be reprioritized until it is closed. */
	if (slab == allocator->open_slab)
		return;

	/* Don't bother adjusting the priority table if unneeded. */
	if (slab->priority == calculate_slab_priority(slab))
		return;

	/*
	 * Reprioritize the slab to reflect the new free block count by removing it from the table
	 * and re-enqueuing it with the new priority.
	 */
	vdo_priority_table_remove(allocator->prioritized_slabs, &slab->allocq_entry);
	prioritize_slab(slab);
}

/**
 * increment_for_data() - Increment the reference count for a data block.
 * @slab: The slab which owns the block.
 * @block: The reference block which contains the block being updated.
 * @block_number: The block to update.
 * @old_status: The reference status of the data block before this increment.
 * @lock: The pbn_lock associated with this increment (may be NULL).
 * @counter_ptr: A pointer to the count for the data block (in, out).
 * @adjust_block_count: Whether to update the allocator's free block count.
 *
 * Return: VDO_SUCCESS or an error.
 */
static int increment_for_data(struct vdo_slab *slab, struct reference_block *block,
			      slab_block_number block_number,
			      enum reference_status old_status,
			      struct pbn_lock *lock, vdo_refcount_t *counter_ptr,
			      bool adjust_block_count)
{
	switch (old_status) {
	case RS_FREE:
		*counter_ptr = 1;
		block->allocated_count++;
		slab->free_blocks--;
		if (adjust_block_count)
			adjust_free_block_count(slab, false);

		break;

	case RS_PROVISIONAL:
		*counter_ptr = 1;
		break;

	default:
		/* Single or shared */
		if (*counter_ptr >= MAXIMUM_REFERENCE_COUNT) {
			return vdo_log_error_strerror(VDO_REF_COUNT_INVALID,
						      "Incrementing a block already having 254 references (slab %u, offset %u)",
						      slab->slab_number, block_number);
		}
		(*counter_ptr)++;
	}

	if (lock != NULL)
		vdo_unassign_pbn_lock_provisional_reference(lock);
	return VDO_SUCCESS;
}

/**
 * decrement_for_data() - Decrement the reference count for a data block.
 * @slab: The slab which owns the block.
 * @block: The reference block which contains the block being updated.
 * @block_number: The block to update.
 * @old_status: The reference status of the data block before this decrement.
 * @updater: The reference updater doing this operation in case we need to look up the pbn lock.
 * @lock: The pbn_lock associated with the block being decremented (may be NULL).
 * @counter_ptr: A pointer to the count for the data block (in, out).
 * @adjust_block_count: Whether to update the allocator's free block count.
 *
 * Return: VDO_SUCCESS or an error.
 */
static int decrement_for_data(struct vdo_slab *slab, struct reference_block *block,
			      slab_block_number block_number,
			      enum reference_status old_status,
			      struct reference_updater *updater,
			      vdo_refcount_t *counter_ptr, bool adjust_block_count)
{
	switch (old_status) {
	case RS_FREE:
		return vdo_log_error_strerror(VDO_REF_COUNT_INVALID,
					      "Decrementing free block at offset %u in slab %u",
					      block_number, slab->slab_number);

	case RS_PROVISIONAL:
	case RS_SINGLE:
		if (updater->zpbn.zone != NULL) {
			struct pbn_lock *lock = vdo_get_physical_zone_pbn_lock(updater->zpbn.zone,
									       updater->zpbn.pbn);

			if (lock != NULL) {
				/*
				 * There is a read lock on this block, so the block must not become
				 * unreferenced.
				 */
				*counter_ptr = PROVISIONAL_REFERENCE_COUNT;
				vdo_assign_pbn_lock_provisional_reference(lock);
				break;
			}
		}

		*counter_ptr = EMPTY_REFERENCE_COUNT;
		block->allocated_count--;
		slab->free_blocks++;
		if (adjust_block_count)
			adjust_free_block_count(slab, true);

		break;

	default:
		/* Shared */
		(*counter_ptr)--;
	}

	return VDO_SUCCESS;
}

/**
 * increment_for_block_map() - Increment the reference count for a block map page.
 * @slab: The slab which owns the block.
 * @block: The reference block which contains the block being updated.
 * @block_number: The block to update.
 * @old_status: The reference status of the block before this increment.
 * @lock: The pbn_lock associated with this increment (may be NULL).
 * @normal_operation: Whether we are in normal operation vs. recovery or rebuild.
 * @counter_ptr: A pointer to the count for the block (in, out).
 * @adjust_block_count: Whether to update the allocator's free block count.
 *
 * All block map increments should be from provisional to MAXIMUM_REFERENCE_COUNT. Since block map
 * blocks never dedupe they should never be adjusted from any other state. The adjustment always
 * results in MAXIMUM_REFERENCE_COUNT as this value is used to prevent dedupe against block map
 * blocks.
 *
 * Return: VDO_SUCCESS or an error.
 */
static int increment_for_block_map(struct vdo_slab *slab, struct reference_block *block,
				   slab_block_number block_number,
				   enum reference_status old_status,
				   struct pbn_lock *lock, bool normal_operation,
				   vdo_refcount_t *counter_ptr, bool adjust_block_count)
{
	switch (old_status) {
	case RS_FREE:
		if (normal_operation) {
			return vdo_log_error_strerror(VDO_REF_COUNT_INVALID,
						      "Incrementing unallocated block map block (slab %u, offset %u)",
						      slab->slab_number, block_number);
		}

		*counter_ptr = MAXIMUM_REFERENCE_COUNT;
		block->allocated_count++;
		slab->free_blocks--;
		if (adjust_block_count)
			adjust_free_block_count(slab, false);

		return VDO_SUCCESS;

	case RS_PROVISIONAL:
		if (!normal_operation)
			return vdo_log_error_strerror(VDO_REF_COUNT_INVALID,
						      "Block map block had provisional reference during replay (slab %u, offset %u)",
						      slab->slab_number, block_number);

		*counter_ptr = MAXIMUM_REFERENCE_COUNT;
		if (lock != NULL)
			vdo_unassign_pbn_lock_provisional_reference(lock);
		return VDO_SUCCESS;

	default:
		return vdo_log_error_strerror(VDO_REF_COUNT_INVALID,
					      "Incrementing a block map block which is already referenced %u times (slab %u, offset %u)",
					      *counter_ptr, slab->slab_number,
					      block_number);
	}
}

static bool __must_check is_valid_journal_point(const struct journal_point *point)
{
	return ((point != NULL) && (point->sequence_number > 0));
}

/**
 * update_reference_count() - Update the reference count of a block.
 * @slab: The slab which owns the block.
 * @block: The reference block which contains the block being updated.
 * @block_number: The block to update.
 * @slab_journal_point: The slab journal point at which this update is journaled.
 * @updater: The reference updater.
 * @normal_operation: Whether we are in normal operation vs. recovery or rebuild.
 * @adjust_block_count: Whether to update the slab's free block count.
 * @provisional_decrement_ptr: A pointer which will be set to true if this update was a decrement
 *                             of a provisional reference.
 *
 * Return: VDO_SUCCESS or an error.
 */
static int update_reference_count(struct vdo_slab *slab, struct reference_block *block,
				  slab_block_number block_number,
				  const struct journal_point *slab_journal_point,
				  struct reference_updater *updater,
				  bool normal_operation, bool adjust_block_count,
				  bool *provisional_decrement_ptr)
{
	vdo_refcount_t *counter_ptr = &slab->counters[block_number];
	enum reference_status old_status = reference_count_to_status(*counter_ptr);
	int result;

	if (!updater->increment) {
		result = decrement_for_data(slab, block, block_number, old_status,
					    updater, counter_ptr, adjust_block_count);
		if ((result == VDO_SUCCESS) && (old_status == RS_PROVISIONAL)) {
			if (provisional_decrement_ptr != NULL)
				*provisional_decrement_ptr = true;
			return VDO_SUCCESS;
		}
	} else if (updater->operation == VDO_JOURNAL_DATA_REMAPPING) {
		result = increment_for_data(slab, block, block_number, old_status,
					    updater->lock, counter_ptr, adjust_block_count);
	} else {
		result = increment_for_block_map(slab, block, block_number, old_status,
						 updater->lock, normal_operation,
						 counter_ptr, adjust_block_count);
	}

	if (result != VDO_SUCCESS)
		return result;

	if (is_valid_journal_point(slab_journal_point))
		slab->slab_journal_point = *slab_journal_point;

	return VDO_SUCCESS;
}

static int __must_check adjust_reference_count(struct vdo_slab *slab,
					       struct reference_updater *updater,
					       const struct journal_point *slab_journal_point)
{
	slab_block_number block_number;
	int result;
	struct reference_block *block;
	bool provisional_decrement = false;

	if (!is_slab_open(slab))
		return VDO_INVALID_ADMIN_STATE;

	result = slab_block_number_from_pbn(slab, updater->zpbn.pbn, &block_number);
	if (result != VDO_SUCCESS)
		return result;

	block = get_reference_block(slab, block_number);
	result = update_reference_count(slab, block, block_number, slab_journal_point,
					updater, NORMAL_OPERATION, true,
					&provisional_decrement);
	if ((result != VDO_SUCCESS) || provisional_decrement)
		return result;

	if (block->is_dirty && (block->slab_journal_lock > 0)) {
		sequence_number_t entry_lock = slab_journal_point->sequence_number;
		/*
		 * This block is already dirty and a slab journal entry has been made for it since
		 * the last time it was clean. We must release the per-entry slab journal lock for
		 * the entry associated with the update we are now doing.
		 */
		result = VDO_ASSERT(is_valid_journal_point(slab_journal_point),
				    "Reference count adjustments need slab journal points.");
		if (result != VDO_SUCCESS)
			return result;

		adjust_slab_journal_block_reference(&slab->journal, entry_lock, -1);
		return VDO_SUCCESS;
	}

	/*
	 * This may be the first time we are applying an update for which there is a slab journal
	 * entry to this block since the block was cleaned. Therefore, we convert the per-entry
	 * slab journal lock to an uncommitted reference block lock, if there is a per-entry lock.
	 */
	if (is_valid_journal_point(slab_journal_point))
		block->slab_journal_lock = slab_journal_point->sequence_number;
	else
		block->slab_journal_lock = 0;

	dirty_block(block);
	return VDO_SUCCESS;
}

/**
 * add_entry_from_waiter() - Add an entry to the slab journal.
 * @waiter: The vio which should make an entry now.
 * @context: The slab journal to make an entry in.
 *
 * This callback is invoked by add_entries() once it has determined that we are ready to make
 * another entry in the slab journal. Implements waiter_callback_fn.
 */
static void add_entry_from_waiter(struct vdo_waiter *waiter, void *context)
{
	int result;
	struct reference_updater *updater =
		container_of(waiter, struct reference_updater, waiter);
	struct data_vio *data_vio = data_vio_from_reference_updater(updater);
	struct slab_journal *journal = context;
	struct slab_journal_block_header *header = &journal->tail_header;
	struct journal_point slab_journal_point = {
		.sequence_number = header->sequence_number,
		.entry_count = header->entry_count,
	};
	sequence_number_t recovery_block = data_vio->recovery_journal_point.sequence_number;

	if (header->entry_count == 0) {
		/*
		 * This is the first entry in the current tail block, so get a lock on the recovery
		 * journal which we will hold until this tail block is committed.
		 */
		get_lock(journal, header->sequence_number)->recovery_start = recovery_block;
		if (journal->recovery_journal != NULL) {
			zone_count_t zone_number = journal->slab->allocator->zone_number;

			vdo_acquire_recovery_journal_block_reference(journal->recovery_journal,
								     recovery_block,
								     VDO_ZONE_TYPE_PHYSICAL,
								     zone_number);
		}

		mark_slab_journal_dirty(journal, recovery_block);
		reclaim_journal_space(journal);
	}

	add_entry(journal, updater->zpbn.pbn, updater->operation, updater->increment,
		  expand_journal_point(data_vio->recovery_journal_point,
				       updater->increment));

	if (journal->slab->status != VDO_SLAB_REBUILT) {
		/*
		 * If the slab is unrecovered, scrubbing will take care of the count since the
		 * update is now recorded in the journal.
		 */
		adjust_slab_journal_block_reference(journal,
						    slab_journal_point.sequence_number, -1);
		result = VDO_SUCCESS;
	} else {
		/* Now that an entry has been made in the slab journal, update the counter. */
		result = adjust_reference_count(journal->slab, updater,
						&slab_journal_point);
	}

	if (updater->increment)
		continue_data_vio_with_error(data_vio, result);
	else
		vdo_continue_completion(&data_vio->decrement_completion, result);
}

/**
 * is_next_entry_a_block_map_increment() - Check whether the next entry to be made is a block map
 *                                         increment.
 * @journal: The journal.
 *
 * Return: true if the first entry waiter's operation is a block map increment.
 */
static inline bool is_next_entry_a_block_map_increment(struct slab_journal *journal)
{
	struct vdo_waiter *waiter = vdo_waitq_get_first_waiter(&journal->entry_waiters);
	struct reference_updater *updater =
		container_of(waiter, struct reference_updater, waiter);

	return (updater->operation == VDO_JOURNAL_BLOCK_MAP_REMAPPING);
}

/**
 * add_entries() - Add as many entries as possible from the queue of vios waiting to make entries.
 * @journal: The journal to which entries may be added.
 *
 * By processing the queue in order, we ensure that slab journal entries are made in the same order
 * as recovery journal entries for the same increment or decrement.
 */
static void add_entries(struct slab_journal *journal)
{
	if (journal->adding_entries) {
		/* Protect against re-entrancy. */
		return;
	}

	journal->adding_entries = true;
	while (vdo_waitq_has_waiters(&journal->entry_waiters)) {
		struct slab_journal_block_header *header = &journal->tail_header;

		if (journal->partial_write_in_progress ||
		    (journal->slab->status == VDO_SLAB_REBUILDING)) {
			/*
			 * Don't add entries while rebuilding or while a partial write is
			 * outstanding, as it could result in reference count corruption.
			 */
			break;
		}

		if (journal->waiting_to_commit) {
			/*
			 * If we are waiting for resources to write the tail block, and the tail
			 * block is full, we can't make another entry.
			 */
			WRITE_ONCE(journal->events->tail_busy_count,
				   journal->events->tail_busy_count + 1);
			break;
		} else if (is_next_entry_a_block_map_increment(journal) &&
			   (header->entry_count >= journal->full_entries_per_block)) {
			/*
			 * The tail block does not have room for a block map increment, so commit
			 * it now.
			 */
			commit_tail(journal);
			if (journal->waiting_to_commit) {
				WRITE_ONCE(journal->events->tail_busy_count,
					   journal->events->tail_busy_count + 1);
				break;
			}
		}

		/* If the slab is over the blocking threshold, make the vio wait. */
		if (requires_reaping(journal)) {
			WRITE_ONCE(journal->events->blocked_count,
				   journal->events->blocked_count + 1);
			save_dirty_reference_blocks(journal->slab);
			break;
		}

		if (header->entry_count == 0) {
			struct journal_lock *lock =
				get_lock(journal, header->sequence_number);

			/*
			 * Check if the on disk slab journal is full. Because of the blocking and
			 * scrubbing thresholds, this should never happen.
			 */
			if (lock->count > 0) {
				VDO_ASSERT_LOG_ONLY((journal->head + journal->size) == journal->tail,
						    "New block has locks, but journal is not full");

				/*
				 * The blocking threshold must let the journal fill up if the new
				 * block has locks; if the blocking threshold is smaller than the
				 * journal size, the new block cannot possibly have locks already.
				 */
				VDO_ASSERT_LOG_ONLY((journal->blocking_threshold >= journal->size),
						    "New block can have locks already iff blocking threshold is at the end of the journal");

				WRITE_ONCE(journal->events->disk_full_count,
					   journal->events->disk_full_count + 1);
				save_dirty_reference_blocks(journal->slab);
				break;
			}

			/*
			 * Don't allow the new block to be reaped until all of the reference count
			 * blocks are written and the journal block has been fully committed as
			 * well.
			 */
			lock->count = journal->entries_per_block + 1;

			if (header->sequence_number == 1) {
				struct vdo_slab *slab = journal->slab;
				block_count_t i;

				/*
				 * This is the first entry in this slab journal, ever. Dirty all of
				 * the reference count blocks. Each will acquire a lock on the tail
				 * block so that the journal won't be reaped until the reference
				 * counts are initialized. The lock acquisition must be done by the
				 * ref_counts since here we don't know how many reference blocks
				 * the ref_counts has.
				 */
				for (i = 0; i < slab->reference_block_count; i++) {
					slab->reference_blocks[i].slab_journal_lock = 1;
					dirty_block(&slab->reference_blocks[i]);
				}

				adjust_slab_journal_block_reference(journal, 1,
								    slab->reference_block_count);
			}
		}

		vdo_waitq_notify_next_waiter(&journal->entry_waiters,
					     add_entry_from_waiter, journal);
	}

	journal->adding_entries = false;

	/* If there are no waiters, and we are flushing or saving, commit the tail block. */
	if (vdo_is_state_draining(&journal->slab->state) &&
	    !vdo_is_state_suspending(&journal->slab->state) &&
	    !vdo_waitq_has_waiters(&journal->entry_waiters))
		commit_tail(journal);
}

/**
 * reset_search_cursor() - Reset the free block search back to the first reference counter in the
 *                         first reference block of a slab.
 */
static void reset_search_cursor(struct vdo_slab *slab)
{
	struct search_cursor *cursor = &slab->search_cursor;

	cursor->block = cursor->first_block;
	cursor->index = 0;
	/* Unit tests have slabs with only one reference block (and it's a runt). */
	cursor->end_index = min_t(u32, COUNTS_PER_BLOCK, slab->block_count);
}

/**
 * advance_search_cursor() - Advance the search cursor to the start of the next reference block in
 *                           a slab,
 *
 * Wraps around to the first reference block if the current block is the last reference block.
 *
 * Return: true unless the cursor was at the last reference block.
 */
static bool advance_search_cursor(struct vdo_slab *slab)
{
	struct search_cursor *cursor = &slab->search_cursor;

	/*
	 * If we just finished searching the last reference block, then wrap back around to the
	 * start of the array.
	 */
	if (cursor->block == cursor->last_block) {
		reset_search_cursor(slab);
		return false;
	}

	/* We're not already at the end, so advance to cursor to the next block. */
	cursor->block++;
	cursor->index = cursor->end_index;

	if (cursor->block == cursor->last_block) {
		/* The last reference block will usually be a runt. */
		cursor->end_index = slab->block_count;
	} else {
		cursor->end_index += COUNTS_PER_BLOCK;
	}

	return true;
}

/**
 * vdo_adjust_reference_count_for_rebuild() - Adjust the reference count of a block during rebuild.
 *
 * Return: VDO_SUCCESS or an error.
 */
int vdo_adjust_reference_count_for_rebuild(struct slab_depot *depot,
					   physical_block_number_t pbn,
					   enum journal_operation operation)
{
	int result;
	slab_block_number block_number;
	struct reference_block *block;
	struct vdo_slab *slab = vdo_get_slab(depot, pbn);
	struct reference_updater updater = {
		.operation = operation,
		.increment = true,
	};

	result = slab_block_number_from_pbn(slab, pbn, &block_number);
	if (result != VDO_SUCCESS)
		return result;

	block = get_reference_block(slab, block_number);
	result = update_reference_count(slab, block, block_number, NULL,
					&updater, !NORMAL_OPERATION, false, NULL);
	if (result != VDO_SUCCESS)
		return result;

	dirty_block(block);
	return VDO_SUCCESS;
}

/**
 * replay_reference_count_change() - Replay the reference count adjustment from a slab journal
 *                                   entry into the reference count for a block.
 * @slab: The slab.
 * @entry_point: The slab journal point for the entry.
 * @entry: The slab journal entry being replayed.
 *
 * The adjustment will be ignored if it was already recorded in the reference count.
 *
 * Return: VDO_SUCCESS or an error code.
 */
static int replay_reference_count_change(struct vdo_slab *slab,
					 const struct journal_point *entry_point,
					 struct slab_journal_entry entry)
{
	int result;
	struct reference_block *block = get_reference_block(slab, entry.sbn);
	sector_count_t sector = (entry.sbn % COUNTS_PER_BLOCK) / COUNTS_PER_SECTOR;
	struct reference_updater updater = {
		.operation = entry.operation,
		.increment = entry.increment,
	};

	if (!vdo_before_journal_point(&block->commit_points[sector], entry_point)) {
		/* This entry is already reflected in the existing counts, so do nothing. */
		return VDO_SUCCESS;
	}

	/* This entry is not yet counted in the reference counts. */
	result = update_reference_count(slab, block, entry.sbn, entry_point,
					&updater, !NORMAL_OPERATION, false, NULL);
	if (result != VDO_SUCCESS)
		return result;

	dirty_block(block);
	return VDO_SUCCESS;
}

/**
 * find_zero_byte_in_word() - Find the array index of the first zero byte in word-sized range of
 *                            reference counters.
 * @word_ptr: A pointer to the eight counter bytes to check.
 * @start_index: The array index corresponding to word_ptr[0].
 * @fail_index: The array index to return if no zero byte is found.
 *
 * The search does no bounds checking; the function relies on the array being sufficiently padded.
 *
 * Return: The array index of the first zero byte in the word, or the value passed as fail_index if
 *         no zero byte was found.
 */
static inline slab_block_number find_zero_byte_in_word(const u8 *word_ptr,
						       slab_block_number start_index,
						       slab_block_number fail_index)
{
	u64 word = get_unaligned_le64(word_ptr);

	/* This looks like a loop, but GCC will unroll the eight iterations for us. */
	unsigned int offset;

	for (offset = 0; offset < BYTES_PER_WORD; offset++) {
		/* Assumes little-endian byte order, which we have on X86. */
		if ((word & 0xFF) == 0)
			return (start_index + offset);
		word >>= 8;
	}

	return fail_index;
}

/**
 * find_free_block() - Find the first block with a reference count of zero in the specified
 *                     range of reference counter indexes.
 * @slab: The slab counters to scan.
 * @index_ptr: A pointer to hold the array index of the free block.
 *
 * Exposed for unit testing.
 *
 * Return: true if a free block was found in the specified range.
 */
static bool find_free_block(const struct vdo_slab *slab, slab_block_number *index_ptr)
{
	slab_block_number zero_index;
	slab_block_number next_index = slab->search_cursor.index;
	slab_block_number end_index = slab->search_cursor.end_index;
	u8 *next_counter = &slab->counters[next_index];
	u8 *end_counter = &slab->counters[end_index];

	/*
	 * Search every byte of the first unaligned word. (Array is padded so reading past end is
	 * safe.)
	 */
	zero_index = find_zero_byte_in_word(next_counter, next_index, end_index);
	if (zero_index < end_index) {
		*index_ptr = zero_index;
		return true;
	}

	/*
	 * On architectures where unaligned word access is expensive, this would be a good place to
	 * advance to an alignment boundary.
	 */
	next_index += BYTES_PER_WORD;
	next_counter += BYTES_PER_WORD;

	/*
	 * Now we're word-aligned; check an word at a time until we find a word containing a zero.
	 * (Array is padded so reading past end is safe.)
	 */
	while (next_counter < end_counter) {
		/*
		 * The following code is currently an exact copy of the code preceding the loop,
		 * but if you try to merge them by using a do loop, it runs slower because a jump
		 * instruction gets added at the start of the iteration.
		 */
		zero_index = find_zero_byte_in_word(next_counter, next_index, end_index);
		if (zero_index < end_index) {
			*index_ptr = zero_index;
			return true;
		}

		next_index += BYTES_PER_WORD;
		next_counter += BYTES_PER_WORD;
	}

	return false;
}

/**
 * search_current_reference_block() - Search the reference block currently saved in the search
 *                                    cursor for a reference count of zero, starting at the saved
 *                                    counter index.
 * @slab: The slab to search.
 * @free_index_ptr: A pointer to receive the array index of the zero reference count.
 *
 * Return: true if an unreferenced counter was found.
 */
static bool search_current_reference_block(const struct vdo_slab *slab,
					   slab_block_number *free_index_ptr)
{
	/* Don't bother searching if the current block is known to be full. */
	return ((slab->search_cursor.block->allocated_count < COUNTS_PER_BLOCK) &&
		find_free_block(slab, free_index_ptr));
}

/**
 * search_reference_blocks() - Search each reference block for a reference count of zero.
 * @slab: The slab to search.
 * @free_index_ptr: A pointer to receive the array index of the zero reference count.
 *
 * Searches each reference block for a reference count of zero, starting at the reference block and
 * counter index saved in the search cursor and searching up to the end of the last reference
 * block. The search does not wrap.
 *
 * Return: true if an unreferenced counter was found.
 */
static bool search_reference_blocks(struct vdo_slab *slab,
				    slab_block_number *free_index_ptr)
{
	/* Start searching at the saved search position in the current block. */
	if (search_current_reference_block(slab, free_index_ptr))
		return true;

	/* Search each reference block up to the end of the slab. */
	while (advance_search_cursor(slab)) {
		if (search_current_reference_block(slab, free_index_ptr))
			return true;
	}

	return false;
}

/**
 * make_provisional_reference() - Do the bookkeeping for making a provisional reference.
 */
static void make_provisional_reference(struct vdo_slab *slab,
				       slab_block_number block_number)
{
	struct reference_block *block = get_reference_block(slab, block_number);

	/*
	 * Make the initial transition from an unreferenced block to a
	 * provisionally allocated block.
	 */
	slab->counters[block_number] = PROVISIONAL_REFERENCE_COUNT;

	/* Account for the allocation. */
	block->allocated_count++;
	slab->free_blocks--;
}

/**
 * dirty_all_reference_blocks() - Mark all reference count blocks in a slab as dirty.
 */
static void dirty_all_reference_blocks(struct vdo_slab *slab)
{
	block_count_t i;

	for (i = 0; i < slab->reference_block_count; i++)
		dirty_block(&slab->reference_blocks[i]);
}

/**
 * clear_provisional_references() - Clear the provisional reference counts from a reference block.
 * @block: The block to clear.
 */
static void clear_provisional_references(struct reference_block *block)
{
	vdo_refcount_t *counters = get_reference_counters_for_block(block);
	block_count_t j;

	for (j = 0; j < COUNTS_PER_BLOCK; j++) {
		if (counters[j] == PROVISIONAL_REFERENCE_COUNT) {
			counters[j] = EMPTY_REFERENCE_COUNT;
			block->allocated_count--;
		}
	}
}

static inline bool journal_points_equal(struct journal_point first,
					struct journal_point second)
{
	return ((first.sequence_number == second.sequence_number) &&
		(first.entry_count == second.entry_count));
}

/**
 * unpack_reference_block() - Unpack reference counts blocks into the internal memory structure.
 * @packed: The written reference block to be unpacked.
 * @block: The internal reference block to be loaded.
 */
static void unpack_reference_block(struct packed_reference_block *packed,
				   struct reference_block *block)
{
	block_count_t index;
	sector_count_t i;
	struct vdo_slab *slab = block->slab;
	vdo_refcount_t *counters = get_reference_counters_for_block(block);

	for (i = 0; i < VDO_SECTORS_PER_BLOCK; i++) {
		struct packed_reference_sector *sector = &packed->sectors[i];

		vdo_unpack_journal_point(&sector->commit_point, &block->commit_points[i]);
		memcpy(counters + (i * COUNTS_PER_SECTOR), sector->counts,
		       (sizeof(vdo_refcount_t) * COUNTS_PER_SECTOR));
		/* The slab_journal_point must be the latest point found in any sector. */
		if (vdo_before_journal_point(&slab->slab_journal_point,
					     &block->commit_points[i]))
			slab->slab_journal_point = block->commit_points[i];

		if ((i > 0) &&
		    !journal_points_equal(block->commit_points[0],
					  block->commit_points[i])) {
			size_t block_index = block - block->slab->reference_blocks;

			vdo_log_warning("Torn write detected in sector %u of reference block %zu of slab %u",
					i, block_index, block->slab->slab_number);
		}
	}

	block->allocated_count = 0;
	for (index = 0; index < COUNTS_PER_BLOCK; index++) {
		if (counters[index] != EMPTY_REFERENCE_COUNT)
			block->allocated_count++;
	}
}

/**
 * finish_reference_block_load() - After a reference block has been read, unpack it.
 * @completion: The VIO that just finished reading.
 */
static void finish_reference_block_load(struct vdo_completion *completion)
{
	struct vio *vio = as_vio(completion);
	struct pooled_vio *pooled = vio_as_pooled_vio(vio);
	struct reference_block *block = completion->parent;
	struct vdo_slab *slab = block->slab;

	unpack_reference_block((struct packed_reference_block *) vio->data, block);
	return_vio_to_pool(slab->allocator->vio_pool, pooled);
	slab->active_count--;
	clear_provisional_references(block);

	slab->free_blocks -= block->allocated_count;
	check_if_slab_drained(slab);
}

static void load_reference_block_endio(struct bio *bio)
{
	struct vio *vio = bio->bi_private;
	struct reference_block *block = vio->completion.parent;

	continue_vio_after_io(vio, finish_reference_block_load,
			      block->slab->allocator->thread_id);
}

/**
 * load_reference_block() - After a block waiter has gotten a VIO from the VIO pool, load the
 *                          block.
 * @waiter: The waiter of the block to load.
 * @context: The VIO returned by the pool.
 */
static void load_reference_block(struct vdo_waiter *waiter, void *context)
{
	struct pooled_vio *pooled = context;
	struct vio *vio = &pooled->vio;
	struct reference_block *block =
		container_of(waiter, struct reference_block, waiter);
	size_t block_offset = (block - block->slab->reference_blocks);

	vio->completion.parent = block;
	vdo_submit_metadata_vio(vio, block->slab->ref_counts_origin + block_offset,
				load_reference_block_endio, handle_io_error,
				REQ_OP_READ);
}

/**
 * load_reference_blocks() - Load a slab's reference blocks from the underlying storage into a
 *                           pre-allocated reference counter.
 */
static void load_reference_blocks(struct vdo_slab *slab)
{
	block_count_t i;

	slab->free_blocks = slab->block_count;
	slab->active_count = slab->reference_block_count;
	for (i = 0; i < slab->reference_block_count; i++) {
		struct vdo_waiter *waiter = &slab->reference_blocks[i].waiter;

		waiter->callback = load_reference_block;
		acquire_vio_from_pool(slab->allocator->vio_pool, waiter);
	}
}

/**
 * drain_slab() - Drain all reference count I/O.
 *
 * Depending upon the type of drain being performed (as recorded in the ref_count's vdo_slab), the
 * reference blocks may be loaded from disk or dirty reference blocks may be written out.
 */
static void drain_slab(struct vdo_slab *slab)
{
	bool save;
	bool load;
	const struct admin_state_code *state = vdo_get_admin_state_code(&slab->state);

	if (state == VDO_ADMIN_STATE_SUSPENDING)
		return;

	if ((state != VDO_ADMIN_STATE_REBUILDING) &&
	    (state != VDO_ADMIN_STATE_SAVE_FOR_SCRUBBING))
		commit_tail(&slab->journal);

	if ((state == VDO_ADMIN_STATE_RECOVERING) || (slab->counters == NULL))
		return;

	save = false;
	load = slab->allocator->summary_entries[slab->slab_number].load_ref_counts;
	if (state == VDO_ADMIN_STATE_SCRUBBING) {
		if (load) {
			load_reference_blocks(slab);
			return;
		}
	} else if (state == VDO_ADMIN_STATE_SAVE_FOR_SCRUBBING) {
		if (!load) {
			/* These reference counts were never written, so mark them all dirty. */
			dirty_all_reference_blocks(slab);
		}
		save = true;
	} else if (state == VDO_ADMIN_STATE_REBUILDING) {
		/*
		 * Write out the counters if the slab has written them before, or it has any
		 * non-zero reference counts, or there are any slab journal blocks.
		 */
		block_count_t data_blocks = slab->allocator->depot->slab_config.data_blocks;

		if (load || (slab->free_blocks != data_blocks) ||
		    !is_slab_journal_blank(slab)) {
			dirty_all_reference_blocks(slab);
			save = true;
		}
	} else if (state == VDO_ADMIN_STATE_SAVING) {
		save = (slab->status == VDO_SLAB_REBUILT);
	} else {
		vdo_finish_draining_with_result(&slab->state, VDO_SUCCESS);
		return;
	}

	if (save)
		save_dirty_reference_blocks(slab);
}

static int allocate_slab_counters(struct vdo_slab *slab)
{
	int result;
	size_t index, bytes;

	result = VDO_ASSERT(slab->reference_blocks == NULL,
			    "vdo_slab %u doesn't allocate refcounts twice",
			    slab->slab_number);
	if (result != VDO_SUCCESS)
		return result;

	result = vdo_allocate(slab->reference_block_count, struct reference_block,
			      __func__, &slab->reference_blocks);
	if (result != VDO_SUCCESS)
		return result;

	/*
	 * Allocate such that the runt slab has a full-length memory array, plus a little padding
	 * so we can word-search even at the very end.
	 */
	bytes = (slab->reference_block_count * COUNTS_PER_BLOCK) + (2 * BYTES_PER_WORD);
	result = vdo_allocate(bytes, vdo_refcount_t, "ref counts array",
			      &slab->counters);
	if (result != VDO_SUCCESS) {
		vdo_free(vdo_forget(slab->reference_blocks));
		return result;
	}

	slab->search_cursor.first_block = slab->reference_blocks;
	slab->search_cursor.last_block = &slab->reference_blocks[slab->reference_block_count - 1];
	reset_search_cursor(slab);

	for (index = 0; index < slab->reference_block_count; index++) {
		slab->reference_blocks[index] = (struct reference_block) {
			.slab = slab,
		};
	}

	return VDO_SUCCESS;
}

static int allocate_counters_if_clean(struct vdo_slab *slab)
{
	if (vdo_is_state_clean_load(&slab->state))
		return allocate_slab_counters(slab);

	return VDO_SUCCESS;
}

static void finish_loading_journal(struct vdo_completion *completion)
{
	struct vio *vio = as_vio(completion);
	struct slab_journal *journal = completion->parent;
	struct vdo_slab *slab = journal->slab;
	struct packed_slab_journal_block *block = (struct packed_slab_journal_block *) vio->data;
	struct slab_journal_block_header header;

	vdo_unpack_slab_journal_block_header(&block->header, &header);

	/* FIXME: should it be an error if the following conditional fails? */
	if ((header.metadata_type == VDO_METADATA_SLAB_JOURNAL) &&
	    (header.nonce == slab->allocator->nonce)) {
		journal->tail = header.sequence_number + 1;

		/*
		 * If the slab is clean, this implies the slab journal is empty, so advance the
		 * head appropriately.
		 */
		journal->head = (slab->allocator->summary_entries[slab->slab_number].is_dirty ?
				 header.head : journal->tail);
		journal->tail_header = header;
		initialize_journal_state(journal);
	}

	return_vio_to_pool(slab->allocator->vio_pool, vio_as_pooled_vio(vio));
	vdo_finish_loading_with_result(&slab->state, allocate_counters_if_clean(slab));
}

static void read_slab_journal_tail_endio(struct bio *bio)
{
	struct vio *vio = bio->bi_private;
	struct slab_journal *journal = vio->completion.parent;

	continue_vio_after_io(vio, finish_loading_journal,
			      journal->slab->allocator->thread_id);
}

static void handle_load_error(struct vdo_completion *completion)
{
	int result = completion->result;
	struct slab_journal *journal = completion->parent;
	struct vio *vio = as_vio(completion);

	vio_record_metadata_io_error(vio);
	return_vio_to_pool(journal->slab->allocator->vio_pool, vio_as_pooled_vio(vio));
	vdo_finish_loading_with_result(&journal->slab->state, result);
}

/**
 * read_slab_journal_tail() - Read the slab journal tail block by using a vio acquired from the vio
 *                            pool.
 * @waiter: The vio pool waiter which has just been notified.
 * @context: The vio pool entry given to the waiter.
 *
 * This is the success callback from acquire_vio_from_pool() when loading a slab journal.
 */
static void read_slab_journal_tail(struct vdo_waiter *waiter, void *context)
{
	struct slab_journal *journal =
		container_of(waiter, struct slab_journal, resource_waiter);
	struct vdo_slab *slab = journal->slab;
	struct pooled_vio *pooled = context;
	struct vio *vio = &pooled->vio;
	tail_block_offset_t last_commit_point =
		slab->allocator->summary_entries[slab->slab_number].tail_block_offset;

	/*
	 * Slab summary keeps the commit point offset, so the tail block is the block before that.
	 * Calculation supports small journals in unit tests.
	 */
	tail_block_offset_t tail_block = ((last_commit_point == 0) ?
					  (tail_block_offset_t)(journal->size - 1) :
					  (last_commit_point - 1));

	vio->completion.parent = journal;
	vio->completion.callback_thread_id = slab->allocator->thread_id;
	vdo_submit_metadata_vio(vio, slab->journal_origin + tail_block,
				read_slab_journal_tail_endio, handle_load_error,
				REQ_OP_READ);
}

/**
 * load_slab_journal() - Load a slab's journal by reading the journal's tail.
 */
static void load_slab_journal(struct vdo_slab *slab)
{
	struct slab_journal *journal = &slab->journal;
	tail_block_offset_t last_commit_point;

	last_commit_point = slab->allocator->summary_entries[slab->slab_number].tail_block_offset;
	if ((last_commit_point == 0) &&
	    !slab->allocator->summary_entries[slab->slab_number].load_ref_counts) {
		/*
		 * This slab claims that it has a tail block at (journal->size - 1), but a head of
		 * 1. This is impossible, due to the scrubbing threshold, on a real system, so
		 * don't bother reading the (bogus) data off disk.
		 */
		VDO_ASSERT_LOG_ONLY(((journal->size < 16) ||
				     (journal->scrubbing_threshold < (journal->size - 1))),
				    "Scrubbing threshold protects against reads of unwritten slab journal blocks");
		vdo_finish_loading_with_result(&slab->state,
					       allocate_counters_if_clean(slab));
		return;
	}

	journal->resource_waiter.callback = read_slab_journal_tail;
	acquire_vio_from_pool(slab->allocator->vio_pool, &journal->resource_waiter);
}

static void register_slab_for_scrubbing(struct vdo_slab *slab, bool high_priority)
{
	struct slab_scrubber *scrubber = &slab->allocator->scrubber;

	VDO_ASSERT_LOG_ONLY((slab->status != VDO_SLAB_REBUILT),
			    "slab to be scrubbed is unrecovered");

	if (slab->status != VDO_SLAB_REQUIRES_SCRUBBING)
		return;

	list_del_init(&slab->allocq_entry);
	if (!slab->was_queued_for_scrubbing) {
		WRITE_ONCE(scrubber->slab_count, scrubber->slab_count + 1);
		slab->was_queued_for_scrubbing = true;
	}

	if (high_priority) {
		slab->status = VDO_SLAB_REQUIRES_HIGH_PRIORITY_SCRUBBING;
		list_add_tail(&slab->allocq_entry, &scrubber->high_priority_slabs);
		return;
	}

	list_add_tail(&slab->allocq_entry, &scrubber->slabs);
}

/* Queue a slab for allocation or scrubbing. */
static void queue_slab(struct vdo_slab *slab)
{
	struct block_allocator *allocator = slab->allocator;
	block_count_t free_blocks;
	int result;

	VDO_ASSERT_LOG_ONLY(list_empty(&slab->allocq_entry),
			"a requeued slab must not already be on a ring");

	if (vdo_is_read_only(allocator->depot->vdo))
		return;

	free_blocks = slab->free_blocks;
	result = VDO_ASSERT((free_blocks <= allocator->depot->slab_config.data_blocks),
			    "rebuilt slab %u must have a valid free block count (has %llu, expected maximum %llu)",
			    slab->slab_number, (unsigned long long) free_blocks,
			    (unsigned long long) allocator->depot->slab_config.data_blocks);
	if (result != VDO_SUCCESS) {
		vdo_enter_read_only_mode(allocator->depot->vdo, result);
		return;
	}

	if (slab->status != VDO_SLAB_REBUILT) {
		register_slab_for_scrubbing(slab, false);
		return;
	}

	if (!vdo_is_state_resuming(&slab->state)) {
		/*
		 * If the slab is resuming, we've already accounted for it here, so don't do it
		 * again.
		 * FIXME: under what situation would the slab be resuming here?
		 */
		WRITE_ONCE(allocator->allocated_blocks,
			   allocator->allocated_blocks - free_blocks);
		if (!is_slab_journal_blank(slab)) {
			WRITE_ONCE(allocator->statistics.slabs_opened,
				   allocator->statistics.slabs_opened + 1);
		}
	}

	if (allocator->depot->vdo->suspend_type == VDO_ADMIN_STATE_SAVING)
		reopen_slab_journal(slab);

	prioritize_slab(slab);
}

/**
 * initiate_slab_action() - Initiate a slab action.
 *
 * Implements vdo_admin_initiator_fn.
 */
static void initiate_slab_action(struct admin_state *state)
{
	struct vdo_slab *slab = container_of(state, struct vdo_slab, state);

	if (vdo_is_state_draining(state)) {
		const struct admin_state_code *operation = vdo_get_admin_state_code(state);

		if (operation == VDO_ADMIN_STATE_SCRUBBING)
			slab->status = VDO_SLAB_REBUILDING;

		drain_slab(slab);
		check_if_slab_drained(slab);
		return;
	}

	if (vdo_is_state_loading(state)) {
		load_slab_journal(slab);
		return;
	}

	if (vdo_is_state_resuming(state)) {
		queue_slab(slab);
		vdo_finish_resuming(state);
		return;
	}

	vdo_finish_operation(state, VDO_INVALID_ADMIN_STATE);
}

/**
 * get_next_slab() - Get the next slab to scrub.
 * @scrubber: The slab scrubber.
 *
 * Return: The next slab to scrub or NULL if there are none.
 */
static struct vdo_slab *get_next_slab(struct slab_scrubber *scrubber)
{
	struct vdo_slab *slab;

	slab = list_first_entry_or_null(&scrubber->high_priority_slabs,
					struct vdo_slab, allocq_entry);
	if (slab != NULL)
		return slab;

	return list_first_entry_or_null(&scrubber->slabs, struct vdo_slab,
					allocq_entry);
}

/**
 * has_slabs_to_scrub() - Check whether a scrubber has slabs to scrub.
 * @scrubber: The scrubber to check.
 *
 * Return: true if the scrubber has slabs to scrub.
 */
static inline bool __must_check has_slabs_to_scrub(struct slab_scrubber *scrubber)
{
	return (get_next_slab(scrubber) != NULL);
}

/**
 * uninitialize_scrubber_vio() - Clean up the slab_scrubber's vio.
 * @scrubber: The scrubber.
 */
static void uninitialize_scrubber_vio(struct slab_scrubber *scrubber)
{
	vdo_free(vdo_forget(scrubber->vio.data));
	free_vio_components(&scrubber->vio);
}

/**
 * finish_scrubbing() - Stop scrubbing, either because there are no more slabs to scrub or because
 *                      there's been an error.
 * @scrubber: The scrubber.
 */
static void finish_scrubbing(struct slab_scrubber *scrubber, int result)
{
	bool notify = vdo_waitq_has_waiters(&scrubber->waiters);
	bool done = !has_slabs_to_scrub(scrubber);
	struct block_allocator *allocator =
		container_of(scrubber, struct block_allocator, scrubber);

	if (done)
		uninitialize_scrubber_vio(scrubber);

	if (scrubber->high_priority_only) {
		scrubber->high_priority_only = false;
		vdo_fail_completion(vdo_forget(scrubber->vio.completion.parent), result);
	} else if (done && (atomic_add_return(-1, &allocator->depot->zones_to_scrub) == 0)) {
		/* All of our slabs were scrubbed, and we're the last allocator to finish. */
		enum vdo_state prior_state =
			atomic_cmpxchg(&allocator->depot->vdo->state, VDO_RECOVERING,
				       VDO_DIRTY);

		/*
		 * To be safe, even if the CAS failed, ensure anything that follows is ordered with
		 * respect to whatever state change did happen.
		 */
		smp_mb__after_atomic();

		/*
		 * We must check the VDO state here and not the depot's read_only_notifier since
		 * the compare-swap-above could have failed due to a read-only entry which our own
		 * thread does not yet know about.
		 */
		if (prior_state == VDO_DIRTY)
			vdo_log_info("VDO commencing normal operation");
		else if (prior_state == VDO_RECOVERING)
			vdo_log_info("Exiting recovery mode");
	}

	/*
	 * Note that the scrubber has stopped, and inform anyone who might be waiting for that to
	 * happen.
	 */
	if (!vdo_finish_draining(&scrubber->admin_state))
		WRITE_ONCE(scrubber->admin_state.current_state,
			   VDO_ADMIN_STATE_SUSPENDED);

	/*
	 * We can't notify waiters until after we've finished draining or they'll just requeue.
	 * Fortunately if there were waiters, we can't have been freed yet.
	 */
	if (notify)
		vdo_waitq_notify_all_waiters(&scrubber->waiters, NULL, NULL);
}

static void scrub_next_slab(struct slab_scrubber *scrubber);

/**
 * slab_scrubbed() - Notify the scrubber that a slab has been scrubbed.
 * @completion: The slab rebuild completion.
 *
 * This callback is registered in apply_journal_entries().
 */
static void slab_scrubbed(struct vdo_completion *completion)
{
	struct slab_scrubber *scrubber =
		container_of(as_vio(completion), struct slab_scrubber, vio);
	struct vdo_slab *slab = scrubber->slab;

	slab->status = VDO_SLAB_REBUILT;
	queue_slab(slab);
	reopen_slab_journal(slab);
	WRITE_ONCE(scrubber->slab_count, scrubber->slab_count - 1);
	scrub_next_slab(scrubber);
}

/**
 * abort_scrubbing() - Abort scrubbing due to an error.
 * @scrubber: The slab scrubber.
 * @result: The error.
 */
static void abort_scrubbing(struct slab_scrubber *scrubber, int result)
{
	vdo_enter_read_only_mode(scrubber->vio.completion.vdo, result);
	finish_scrubbing(scrubber, result);
}

/**
 * handle_scrubber_error() - Handle errors while rebuilding a slab.
 * @completion: The slab rebuild completion.
 */
static void handle_scrubber_error(struct vdo_completion *completion)
{
	struct vio *vio = as_vio(completion);

	vio_record_metadata_io_error(vio);
	abort_scrubbing(container_of(vio, struct slab_scrubber, vio),
			completion->result);
}

/**
 * apply_block_entries() - Apply all the entries in a block to the reference counts.
 * @block: A block with entries to apply.
 * @entry_count: The number of entries to apply.
 * @block_number: The sequence number of the block.
 * @slab: The slab to apply the entries to.
 *
 * Return: VDO_SUCCESS or an error code.
 */
static int apply_block_entries(struct packed_slab_journal_block *block,
			       journal_entry_count_t entry_count,
			       sequence_number_t block_number, struct vdo_slab *slab)
{
	struct journal_point entry_point = {
		.sequence_number = block_number,
		.entry_count = 0,
	};
	int result;
	slab_block_number max_sbn = slab->end - slab->start;

	while (entry_point.entry_count < entry_count) {
		struct slab_journal_entry entry =
			vdo_decode_slab_journal_entry(block, entry_point.entry_count);

		if (entry.sbn > max_sbn) {
			/* This entry is out of bounds. */
			return vdo_log_error_strerror(VDO_CORRUPT_JOURNAL,
						      "vdo_slab journal entry (%llu, %u) had invalid offset %u in slab (size %u blocks)",
						      (unsigned long long) block_number,
						      entry_point.entry_count,
						      entry.sbn, max_sbn);
		}

		result = replay_reference_count_change(slab, &entry_point, entry);
		if (result != VDO_SUCCESS) {
			vdo_log_error_strerror(result,
					       "vdo_slab journal entry (%llu, %u) (%s of offset %u) could not be applied in slab %u",
					       (unsigned long long) block_number,
					       entry_point.entry_count,
					       vdo_get_journal_operation_name(entry.operation),
					       entry.sbn, slab->slab_number);
			return result;
		}
		entry_point.entry_count++;
	}

	return VDO_SUCCESS;
}

/**
 * apply_journal_entries() - Find the relevant vio of the slab journal and apply all valid entries.
 * @completion: The metadata read vio completion.
 *
 * This is a callback registered in start_scrubbing().
 */
static void apply_journal_entries(struct vdo_completion *completion)
{
	int result;
	struct slab_scrubber *scrubber =
		container_of(as_vio(completion), struct slab_scrubber, vio);
	struct vdo_slab *slab = scrubber->slab;
	struct slab_journal *journal = &slab->journal;

	/* Find the boundaries of the useful part of the journal. */
	sequence_number_t tail = journal->tail;
	tail_block_offset_t end_index = (tail - 1) % journal->size;
	char *end_data = scrubber->vio.data + (end_index * VDO_BLOCK_SIZE);
	struct packed_slab_journal_block *end_block =
		(struct packed_slab_journal_block *) end_data;

	sequence_number_t head = __le64_to_cpu(end_block->header.head);
	tail_block_offset_t head_index = head % journal->size;
	block_count_t index = head_index;

	struct journal_point ref_counts_point = slab->slab_journal_point;
	struct journal_point last_entry_applied = ref_counts_point;
	sequence_number_t sequence;

	for (sequence = head; sequence < tail; sequence++) {
		char *block_data = scrubber->vio.data + (index * VDO_BLOCK_SIZE);
		struct packed_slab_journal_block *block =
			(struct packed_slab_journal_block *) block_data;
		struct slab_journal_block_header header;

		vdo_unpack_slab_journal_block_header(&block->header, &header);

		if ((header.nonce != slab->allocator->nonce) ||
		    (header.metadata_type != VDO_METADATA_SLAB_JOURNAL) ||
		    (header.sequence_number != sequence) ||
		    (header.entry_count > journal->entries_per_block) ||
		    (header.has_block_map_increments &&
		     (header.entry_count > journal->full_entries_per_block))) {
			/* The block is not what we expect it to be. */
			vdo_log_error("vdo_slab journal block for slab %u was invalid",
				      slab->slab_number);
			abort_scrubbing(scrubber, VDO_CORRUPT_JOURNAL);
			return;
		}

		result = apply_block_entries(block, header.entry_count, sequence, slab);
		if (result != VDO_SUCCESS) {
			abort_scrubbing(scrubber, result);
			return;
		}

		last_entry_applied.sequence_number = sequence;
		last_entry_applied.entry_count = header.entry_count - 1;
		index++;
		if (index == journal->size)
			index = 0;
	}

	/*
	 * At the end of rebuild, the reference counters should be accurate to the end of the
	 * journal we just applied.
	 */
	result = VDO_ASSERT(!vdo_before_journal_point(&last_entry_applied,
						      &ref_counts_point),
			    "Refcounts are not more accurate than the slab journal");
	if (result != VDO_SUCCESS) {
		abort_scrubbing(scrubber, result);
		return;
	}

	/* Save out the rebuilt reference blocks. */
	vdo_prepare_completion(completion, slab_scrubbed, handle_scrubber_error,
			       slab->allocator->thread_id, completion->parent);
	vdo_start_operation_with_waiter(&slab->state,
					VDO_ADMIN_STATE_SAVE_FOR_SCRUBBING,
					completion, initiate_slab_action);
}

static void read_slab_journal_endio(struct bio *bio)
{
	struct vio *vio = bio->bi_private;
	struct slab_scrubber *scrubber = container_of(vio, struct slab_scrubber, vio);

	continue_vio_after_io(bio->bi_private, apply_journal_entries,
			      scrubber->slab->allocator->thread_id);
}

/**
 * start_scrubbing() - Read the current slab's journal from disk now that it has been flushed.
 * @completion: The scrubber's vio completion.
 *
 * This callback is registered in scrub_next_slab().
 */
static void start_scrubbing(struct vdo_completion *completion)
{
	struct slab_scrubber *scrubber =
		container_of(as_vio(completion), struct slab_scrubber, vio);
	struct vdo_slab *slab = scrubber->slab;

	if (!slab->allocator->summary_entries[slab->slab_number].is_dirty) {
		slab_scrubbed(completion);
		return;
	}

	vdo_submit_metadata_vio(&scrubber->vio, slab->journal_origin,
				read_slab_journal_endio, handle_scrubber_error,
				REQ_OP_READ);
}

/**
 * scrub_next_slab() - Scrub the next slab if there is one.
 * @scrubber: The scrubber.
 */
static void scrub_next_slab(struct slab_scrubber *scrubber)
{
	struct vdo_completion *completion = &scrubber->vio.completion;
	struct vdo_slab *slab;

	/*
	 * Note: this notify call is always safe only because scrubbing can only be started when
	 * the VDO is quiescent.
	 */
	vdo_waitq_notify_all_waiters(&scrubber->waiters, NULL, NULL);

	if (vdo_is_read_only(completion->vdo)) {
		finish_scrubbing(scrubber, VDO_READ_ONLY);
		return;
	}

	slab = get_next_slab(scrubber);
	if ((slab == NULL) ||
	    (scrubber->high_priority_only && list_empty(&scrubber->high_priority_slabs))) {
		finish_scrubbing(scrubber, VDO_SUCCESS);
		return;
	}

	if (vdo_finish_draining(&scrubber->admin_state))
		return;

	list_del_init(&slab->allocq_entry);
	scrubber->slab = slab;
	vdo_prepare_completion(completion, start_scrubbing, handle_scrubber_error,
			       slab->allocator->thread_id, completion->parent);
	vdo_start_operation_with_waiter(&slab->state, VDO_ADMIN_STATE_SCRUBBING,
					completion, initiate_slab_action);
}

/**
 * scrub_slabs() - Scrub all of an allocator's slabs that are eligible for scrubbing.
 * @allocator: The block_allocator to scrub.
 * @parent: The completion to notify when scrubbing is done, implies high_priority, may be NULL.
 */
static void scrub_slabs(struct block_allocator *allocator, struct vdo_completion *parent)
{
	struct slab_scrubber *scrubber = &allocator->scrubber;

	scrubber->vio.completion.parent = parent;
	scrubber->high_priority_only = (parent != NULL);
	if (!has_slabs_to_scrub(scrubber)) {
		finish_scrubbing(scrubber, VDO_SUCCESS);
		return;
	}

	if (scrubber->high_priority_only &&
	    vdo_is_priority_table_empty(allocator->prioritized_slabs) &&
	    list_empty(&scrubber->high_priority_slabs))
		register_slab_for_scrubbing(get_next_slab(scrubber), true);

	vdo_resume_if_quiescent(&scrubber->admin_state);
	scrub_next_slab(scrubber);
}

static inline void assert_on_allocator_thread(thread_id_t thread_id,
					      const char *function_name)
{
	VDO_ASSERT_LOG_ONLY((vdo_get_callback_thread_id() == thread_id),
			    "%s called on correct thread", function_name);
}

static void register_slab_with_allocator(struct block_allocator *allocator,
					 struct vdo_slab *slab)
{
	allocator->slab_count++;
	allocator->last_slab = slab->slab_number;
}

/**
 * get_depot_slab_iterator() - Return a slab_iterator over the slabs in a slab_depot.
 * @depot: The depot over which to iterate.
 * @start: The number of the slab to start iterating from.
 * @end: The number of the last slab which may be returned.
 * @stride: The difference in slab number between successive slabs.
 *
 * Iteration always occurs from higher to lower numbered slabs.
 *
 * Return: An initialized iterator structure.
 */
static struct slab_iterator get_depot_slab_iterator(struct slab_depot *depot,
						    slab_count_t start, slab_count_t end,
						    slab_count_t stride)
{
	struct vdo_slab **slabs = depot->slabs;

	return (struct slab_iterator) {
		.slabs = slabs,
		.next = (((slabs == NULL) || (start < end)) ? NULL : slabs[start]),
		.end = end,
		.stride = stride,
	};
}

static struct slab_iterator get_slab_iterator(const struct block_allocator *allocator)
{
	return get_depot_slab_iterator(allocator->depot, allocator->last_slab,
				       allocator->zone_number,
				       allocator->depot->zone_count);
}

/**
 * next_slab() - Get the next slab from a slab_iterator and advance the iterator
 * @iterator: The slab_iterator.
 *
 * Return: The next slab or NULL if the iterator is exhausted.
 */
static struct vdo_slab *next_slab(struct slab_iterator *iterator)
{
	struct vdo_slab *slab = iterator->next;

	if ((slab == NULL) || (slab->slab_number < iterator->end + iterator->stride))
		iterator->next = NULL;
	else
		iterator->next = iterator->slabs[slab->slab_number - iterator->stride];

	return slab;
}

/**
 * abort_waiter() - Abort vios waiting to make journal entries when read-only.
 *
 * This callback is invoked on all vios waiting to make slab journal entries after the VDO has gone
 * into read-only mode. Implements waiter_callback_fn.
 */
static void abort_waiter(struct vdo_waiter *waiter, void *context __always_unused)
{
	struct reference_updater *updater =
		container_of(waiter, struct reference_updater, waiter);
	struct data_vio *data_vio = data_vio_from_reference_updater(updater);

	if (updater->increment) {
		continue_data_vio_with_error(data_vio, VDO_READ_ONLY);
		return;
	}

	vdo_continue_completion(&data_vio->decrement_completion, VDO_READ_ONLY);
}

/* Implements vdo_read_only_notification_fn. */
static void notify_block_allocator_of_read_only_mode(void *listener,
						     struct vdo_completion *parent)
{
	struct block_allocator *allocator = listener;
	struct slab_iterator iterator;

	assert_on_allocator_thread(allocator->thread_id, __func__);
	iterator = get_slab_iterator(allocator);
	while (iterator.next != NULL) {
		struct vdo_slab *slab = next_slab(&iterator);

		vdo_waitq_notify_all_waiters(&slab->journal.entry_waiters,
					     abort_waiter, &slab->journal);
		check_if_slab_drained(slab);
	}

	vdo_finish_completion(parent);
}

/**
 * vdo_acquire_provisional_reference() - Acquire a provisional reference on behalf of a PBN lock if
 *                                       the block it locks is unreferenced.
 * @slab: The slab which contains the block.
 * @pbn: The physical block to reference.
 * @lock: The lock.
 *
 * Return: VDO_SUCCESS or an error.
 */
int vdo_acquire_provisional_reference(struct vdo_slab *slab, physical_block_number_t pbn,
				      struct pbn_lock *lock)
{
	slab_block_number block_number;
	int result;

	if (vdo_pbn_lock_has_provisional_reference(lock))
		return VDO_SUCCESS;

	if (!is_slab_open(slab))
		return VDO_INVALID_ADMIN_STATE;

	result = slab_block_number_from_pbn(slab, pbn, &block_number);
	if (result != VDO_SUCCESS)
		return result;

	if (slab->counters[block_number] == EMPTY_REFERENCE_COUNT) {
		make_provisional_reference(slab, block_number);
		if (lock != NULL)
			vdo_assign_pbn_lock_provisional_reference(lock);
	}

	if (vdo_pbn_lock_has_provisional_reference(lock))
		adjust_free_block_count(slab, false);

	return VDO_SUCCESS;
}

static int __must_check allocate_slab_block(struct vdo_slab *slab,
					    physical_block_number_t *block_number_ptr)
{
	slab_block_number free_index;

	if (!is_slab_open(slab))
		return VDO_INVALID_ADMIN_STATE;

	if (!search_reference_blocks(slab, &free_index))
		return VDO_NO_SPACE;

	VDO_ASSERT_LOG_ONLY((slab->counters[free_index] == EMPTY_REFERENCE_COUNT),
			    "free block must have ref count of zero");
	make_provisional_reference(slab, free_index);
	adjust_free_block_count(slab, false);

	/*
	 * Update the search hint so the next search will start at the array index just past the
	 * free block we just found.
	 */
	slab->search_cursor.index = (free_index + 1);

	*block_number_ptr = slab->start + free_index;
	return VDO_SUCCESS;
}

/**
 * open_slab() - Prepare a slab to be allocated from.
 * @slab: The slab.
 */
static void open_slab(struct vdo_slab *slab)
{
	reset_search_cursor(slab);
	if (is_slab_journal_blank(slab)) {
		WRITE_ONCE(slab->allocator->statistics.slabs_opened,
			   slab->allocator->statistics.slabs_opened + 1);
		dirty_all_reference_blocks(slab);
	} else {
		WRITE_ONCE(slab->allocator->statistics.slabs_reopened,
			   slab->allocator->statistics.slabs_reopened + 1);
	}

	slab->allocator->open_slab = slab;
}


/*
 * The block allocated will have a provisional reference and the reference must be either confirmed
 * with a subsequent increment or vacated with a subsequent decrement via
 * vdo_release_block_reference().
 */
int vdo_allocate_block(struct block_allocator *allocator,
		       physical_block_number_t *block_number_ptr)
{
	int result;

	if (allocator->open_slab != NULL) {
		/* Try to allocate the next block in the currently open slab. */
		result = allocate_slab_block(allocator->open_slab, block_number_ptr);
		if ((result == VDO_SUCCESS) || (result != VDO_NO_SPACE))
			return result;

		/* Put the exhausted open slab back into the priority table. */
		prioritize_slab(allocator->open_slab);
	}

	/* Remove the highest priority slab from the priority table and make it the open slab. */
	open_slab(list_entry(vdo_priority_table_dequeue(allocator->prioritized_slabs),
			     struct vdo_slab, allocq_entry));

	/*
	 * Try allocating again. If we're out of space immediately after opening a slab, then every
	 * slab must be fully allocated.
	 */
	return allocate_slab_block(allocator->open_slab, block_number_ptr);
}

/**
 * vdo_enqueue_clean_slab_waiter() - Wait for a clean slab.
 * @allocator: The block_allocator on which to wait.
 * @waiter: The waiter.
 *
 * Return: VDO_SUCCESS if the waiter was queued, VDO_NO_SPACE if there are no slabs to scrub, and
 *         some other error otherwise.
 */
int vdo_enqueue_clean_slab_waiter(struct block_allocator *allocator,
				  struct vdo_waiter *waiter)
{
	if (vdo_is_read_only(allocator->depot->vdo))
		return VDO_READ_ONLY;

	if (vdo_is_state_quiescent(&allocator->scrubber.admin_state))
		return VDO_NO_SPACE;

	vdo_waitq_enqueue_waiter(&allocator->scrubber.waiters, waiter);
	return VDO_SUCCESS;
}

/**
 * vdo_modify_reference_count() - Modify the reference count of a block by first making a slab
 *                                journal entry and then updating the reference counter.
 *
 * @data_vio: The data_vio for which to add the entry.
 * @updater: Which of the data_vio's reference updaters is being submitted.
 */
void vdo_modify_reference_count(struct vdo_completion *completion,
				struct reference_updater *updater)
{
	struct vdo_slab *slab = vdo_get_slab(completion->vdo->depot, updater->zpbn.pbn);

	if (!is_slab_open(slab)) {
		vdo_continue_completion(completion, VDO_INVALID_ADMIN_STATE);
		return;
	}

	if (vdo_is_read_only(completion->vdo)) {
		vdo_continue_completion(completion, VDO_READ_ONLY);
		return;
	}

	vdo_waitq_enqueue_waiter(&slab->journal.entry_waiters, &updater->waiter);
	if ((slab->status != VDO_SLAB_REBUILT) && requires_reaping(&slab->journal))
		register_slab_for_scrubbing(slab, true);

	add_entries(&slab->journal);
}

/* Release an unused provisional reference. */
int vdo_release_block_reference(struct block_allocator *allocator,
				physical_block_number_t pbn)
{
	struct reference_updater updater;

	if (pbn == VDO_ZERO_BLOCK)
		return VDO_SUCCESS;

	updater = (struct reference_updater) {
		.operation = VDO_JOURNAL_DATA_REMAPPING,
		.increment = false,
		.zpbn = {
			.pbn = pbn,
		},
	};

	return adjust_reference_count(vdo_get_slab(allocator->depot, pbn),
				      &updater, NULL);
}

/*
 * This is a min_heap callback function orders slab_status structures using the 'is_clean' field as
 * the primary key and the 'emptiness' field as the secondary key.
 *
 * Slabs need to be pushed onto the rings in the same order they are to be popped off. Popping
 * should always get the most empty first, so pushing should be from most empty to least empty.
 * Thus, the ordering is reversed from the usual sense since min_heap returns smaller elements
 * before larger ones.
 */
static bool slab_status_is_less_than(const void *item1, const void *item2)
{
	const struct slab_status *info1 = item1;
	const struct slab_status *info2 = item2;

	if (info1->is_clean != info2->is_clean)
		return info1->is_clean;
	if (info1->emptiness != info2->emptiness)
		return info1->emptiness > info2->emptiness;
	return info1->slab_number < info2->slab_number;
}

static void swap_slab_statuses(void *item1, void *item2)
{
	struct slab_status *info1 = item1;
	struct slab_status *info2 = item2;

	swap(*info1, *info2);
}

static const struct min_heap_callbacks slab_status_min_heap = {
	.less = slab_status_is_less_than,
	.swp = swap_slab_statuses,
};

/* Inform the slab actor that a action has finished on some slab; used by apply_to_slabs(). */
static void slab_action_callback(struct vdo_completion *completion)
{
	struct block_allocator *allocator = vdo_as_block_allocator(completion);
	struct slab_actor *actor = &allocator->slab_actor;

	if (--actor->slab_action_count == 0) {
		actor->callback(completion);
		return;
	}

	vdo_reset_completion(completion);
}

/* Preserve the error from part of an action and continue. */
static void handle_operation_error(struct vdo_completion *completion)
{
	struct block_allocator *allocator = vdo_as_block_allocator(completion);

	if (allocator->state.waiter != NULL)
		vdo_set_completion_result(allocator->state.waiter, completion->result);
	completion->callback(completion);
}

/* Perform an action on each of an allocator's slabs in parallel. */
static void apply_to_slabs(struct block_allocator *allocator, vdo_action_fn callback)
{
	struct slab_iterator iterator;

	vdo_prepare_completion(&allocator->completion, slab_action_callback,
			       handle_operation_error, allocator->thread_id, NULL);
	allocator->completion.requeue = false;

	/*
	 * Since we are going to dequeue all of the slabs, the open slab will become invalid, so
	 * clear it.
	 */
	allocator->open_slab = NULL;

	/* Ensure that we don't finish before we're done starting. */
	allocator->slab_actor = (struct slab_actor) {
		.slab_action_count = 1,
		.callback = callback,
	};

	iterator = get_slab_iterator(allocator);
	while (iterator.next != NULL) {
		const struct admin_state_code *operation =
			vdo_get_admin_state_code(&allocator->state);
		struct vdo_slab *slab = next_slab(&iterator);

		list_del_init(&slab->allocq_entry);
		allocator->slab_actor.slab_action_count++;
		vdo_start_operation_with_waiter(&slab->state, operation,
						&allocator->completion,
						initiate_slab_action);
	}

	slab_action_callback(&allocator->completion);
}

static void finish_loading_allocator(struct vdo_completion *completion)
{
	struct block_allocator *allocator = vdo_as_block_allocator(completion);
	const struct admin_state_code *operation =
		vdo_get_admin_state_code(&allocator->state);

	if (allocator->eraser != NULL)
		dm_kcopyd_client_destroy(vdo_forget(allocator->eraser));

	if (operation == VDO_ADMIN_STATE_LOADING_FOR_RECOVERY) {
		void *context =
			vdo_get_current_action_context(allocator->depot->action_manager);

		vdo_replay_into_slab_journals(allocator, context);
		return;
	}

	vdo_finish_loading(&allocator->state);
}

static void erase_next_slab_journal(struct block_allocator *allocator);

static void copy_callback(int read_err, unsigned long write_err, void *context)
{
	struct block_allocator *allocator = context;
	int result = (((read_err == 0) && (write_err == 0)) ? VDO_SUCCESS : -EIO);

	if (result != VDO_SUCCESS) {
		vdo_fail_completion(&allocator->completion, result);
		return;
	}

	erase_next_slab_journal(allocator);
}

/* erase_next_slab_journal() - Erase the next slab journal. */
static void erase_next_slab_journal(struct block_allocator *allocator)
{
	struct vdo_slab *slab;
	physical_block_number_t pbn;
	struct dm_io_region regions[1];
	struct slab_depot *depot = allocator->depot;
	block_count_t blocks = depot->slab_config.slab_journal_blocks;

	if (allocator->slabs_to_erase.next == NULL) {
		vdo_finish_completion(&allocator->completion);
		return;
	}

	slab = next_slab(&allocator->slabs_to_erase);
	pbn = slab->journal_origin - depot->vdo->geometry.bio_offset;
	regions[0] = (struct dm_io_region) {
		.bdev = vdo_get_backing_device(depot->vdo),
		.sector = pbn * VDO_SECTORS_PER_BLOCK,
		.count = blocks * VDO_SECTORS_PER_BLOCK,
	};
	dm_kcopyd_zero(allocator->eraser, 1, regions, 0, copy_callback, allocator);
}

/* Implements vdo_admin_initiator_fn. */
static void initiate_load(struct admin_state *state)
{
	struct block_allocator *allocator =
		container_of(state, struct block_allocator, state);
	const struct admin_state_code *operation = vdo_get_admin_state_code(state);

	if (operation == VDO_ADMIN_STATE_LOADING_FOR_REBUILD) {
		/*
		 * Must requeue because the kcopyd client cannot be freed in the same stack frame
		 * as the kcopyd callback, lest it deadlock.
		 */
		vdo_prepare_completion_for_requeue(&allocator->completion,
						   finish_loading_allocator,
						   handle_operation_error,
						   allocator->thread_id, NULL);
		allocator->eraser = dm_kcopyd_client_create(NULL);
		if (IS_ERR(allocator->eraser)) {
			vdo_fail_completion(&allocator->completion,
					    PTR_ERR(allocator->eraser));
			allocator->eraser = NULL;
			return;
		}
		allocator->slabs_to_erase = get_slab_iterator(allocator);

		erase_next_slab_journal(allocator);
		return;
	}

	apply_to_slabs(allocator, finish_loading_allocator);
}

/**
 * vdo_notify_slab_journals_are_recovered() - Inform a block allocator that its slab journals have
 *                                            been recovered from the recovery journal.
 * @completion The allocator completion
 */
void vdo_notify_slab_journals_are_recovered(struct vdo_completion *completion)
{
	struct block_allocator *allocator = vdo_as_block_allocator(completion);

	vdo_finish_loading_with_result(&allocator->state, completion->result);
}

static int get_slab_statuses(struct block_allocator *allocator,
			     struct slab_status **statuses_ptr)
{
	int result;
	struct slab_status *statuses;
	struct slab_iterator iterator = get_slab_iterator(allocator);

	result = vdo_allocate(allocator->slab_count, struct slab_status, __func__,
			      &statuses);
	if (result != VDO_SUCCESS)
		return result;

	*statuses_ptr = statuses;

	while (iterator.next != NULL)  {
		slab_count_t slab_number = next_slab(&iterator)->slab_number;

		*statuses++ = (struct slab_status) {
			.slab_number = slab_number,
			.is_clean = !allocator->summary_entries[slab_number].is_dirty,
			.emptiness = allocator->summary_entries[slab_number].fullness_hint,
		};
	}

	return VDO_SUCCESS;
}

/* Prepare slabs for allocation or scrubbing. */
static int __must_check vdo_prepare_slabs_for_allocation(struct block_allocator *allocator)
{
	struct slab_status current_slab_status;
	DEFINE_MIN_HEAP(struct slab_status, heap) heap;
	int result;
	struct slab_status *slab_statuses;
	struct slab_depot *depot = allocator->depot;

	WRITE_ONCE(allocator->allocated_blocks,
		   allocator->slab_count * depot->slab_config.data_blocks);
	result = get_slab_statuses(allocator, &slab_statuses);
	if (result != VDO_SUCCESS)
		return result;

	/* Sort the slabs by cleanliness, then by emptiness hint. */
	heap = (struct heap) {
		.data = slab_statuses,
		.nr = allocator->slab_count,
		.size = allocator->slab_count,
	};
	min_heapify_all(&heap, &slab_status_min_heap);

	while (heap.nr > 0) {
		bool high_priority;
		struct vdo_slab *slab;
		struct slab_journal *journal;

		current_slab_status = slab_statuses[0];
		min_heap_pop(&heap, &slab_status_min_heap);
		slab = depot->slabs[current_slab_status.slab_number];

		if ((depot->load_type == VDO_SLAB_DEPOT_REBUILD_LOAD) ||
		    (!allocator->summary_entries[slab->slab_number].load_ref_counts &&
		     current_slab_status.is_clean)) {
			queue_slab(slab);
			continue;
		}

		slab->status = VDO_SLAB_REQUIRES_SCRUBBING;
		journal = &slab->journal;
		high_priority = ((current_slab_status.is_clean &&
				 (depot->load_type == VDO_SLAB_DEPOT_NORMAL_LOAD)) ||
				 (journal_length(journal) >= journal->scrubbing_threshold));
		register_slab_for_scrubbing(slab, high_priority);
	}

	vdo_free(slab_statuses);
	return VDO_SUCCESS;
}

static const char *status_to_string(enum slab_rebuild_status status)
{
	switch (status) {
	case VDO_SLAB_REBUILT:
		return "REBUILT";
	case VDO_SLAB_REQUIRES_SCRUBBING:
		return "SCRUBBING";
	case VDO_SLAB_REQUIRES_HIGH_PRIORITY_SCRUBBING:
		return "PRIORITY_SCRUBBING";
	case VDO_SLAB_REBUILDING:
		return "REBUILDING";
	case VDO_SLAB_REPLAYING:
		return "REPLAYING";
	default:
		return "UNKNOWN";
	}
}

void vdo_dump_block_allocator(const struct block_allocator *allocator)
{
	unsigned int pause_counter = 0;
	struct slab_iterator iterator = get_slab_iterator(allocator);
	const struct slab_scrubber *scrubber = &allocator->scrubber;

	vdo_log_info("block_allocator zone %u", allocator->zone_number);
	while (iterator.next != NULL) {
		struct vdo_slab *slab = next_slab(&iterator);
		struct slab_journal *journal = &slab->journal;

		if (slab->reference_blocks != NULL) {
			/* Terse because there are a lot of slabs to dump and syslog is lossy. */
			vdo_log_info("slab %u: P%u, %llu free", slab->slab_number,
				     slab->priority,
				     (unsigned long long) slab->free_blocks);
		} else {
			vdo_log_info("slab %u: status %s", slab->slab_number,
				     status_to_string(slab->status));
		}

		vdo_log_info("  slab journal: entry_waiters=%zu waiting_to_commit=%s updating_slab_summary=%s head=%llu unreapable=%llu tail=%llu next_commit=%llu summarized=%llu last_summarized=%llu recovery_lock=%llu dirty=%s",
			     vdo_waitq_num_waiters(&journal->entry_waiters),
			     vdo_bool_to_string(journal->waiting_to_commit),
			     vdo_bool_to_string(journal->updating_slab_summary),
			     (unsigned long long) journal->head,
			     (unsigned long long) journal->unreapable,
			     (unsigned long long) journal->tail,
			     (unsigned long long) journal->next_commit,
			     (unsigned long long) journal->summarized,
			     (unsigned long long) journal->last_summarized,
			     (unsigned long long) journal->recovery_lock,
			     vdo_bool_to_string(journal->recovery_lock != 0));
		/*
		 * Given the frequency with which the locks are just a tiny bit off, it might be
		 * worth dumping all the locks, but that might be too much logging.
		 */

		if (slab->counters != NULL) {
			/* Terse because there are a lot of slabs to dump and syslog is lossy. */
			vdo_log_info("  slab: free=%u/%u blocks=%u dirty=%zu active=%zu journal@(%llu,%u)",
				     slab->free_blocks, slab->block_count,
				     slab->reference_block_count,
				     vdo_waitq_num_waiters(&slab->dirty_blocks),
				     slab->active_count,
				     (unsigned long long) slab->slab_journal_point.sequence_number,
				     slab->slab_journal_point.entry_count);
		} else {
			vdo_log_info("  no counters");
		}

		/*
		 * Wait for a while after each batch of 32 slabs dumped, an arbitrary number,
		 * allowing the kernel log a chance to be flushed instead of being overrun.
		 */
		if (pause_counter++ == 31) {
			pause_counter = 0;
			vdo_pause_for_logger();
		}
	}

	vdo_log_info("slab_scrubber slab_count %u waiters %zu %s%s",
		     READ_ONCE(scrubber->slab_count),
		     vdo_waitq_num_waiters(&scrubber->waiters),
		     vdo_get_admin_state_code(&scrubber->admin_state)->name,
		     scrubber->high_priority_only ? ", high_priority_only " : "");
}

static void free_slab(struct vdo_slab *slab)
{
	if (slab == NULL)
		return;

	list_del(&slab->allocq_entry);
	vdo_free(vdo_forget(slab->journal.block));
	vdo_free(vdo_forget(slab->journal.locks));
	vdo_free(vdo_forget(slab->counters));
	vdo_free(vdo_forget(slab->reference_blocks));
	vdo_free(slab);
}

static int initialize_slab_journal(struct vdo_slab *slab)
{
	struct slab_journal *journal = &slab->journal;
	const struct slab_config *slab_config = &slab->allocator->depot->slab_config;
	int result;

	result = vdo_allocate(slab_config->slab_journal_blocks, struct journal_lock,
			      __func__, &journal->locks);
	if (result != VDO_SUCCESS)
		return result;

	result = vdo_allocate(VDO_BLOCK_SIZE, char, "struct packed_slab_journal_block",
			      (char **) &journal->block);
	if (result != VDO_SUCCESS)
		return result;

	journal->slab = slab;
	journal->size = slab_config->slab_journal_blocks;
	journal->flushing_threshold = slab_config->slab_journal_flushing_threshold;
	journal->blocking_threshold = slab_config->slab_journal_blocking_threshold;
	journal->scrubbing_threshold = slab_config->slab_journal_scrubbing_threshold;
	journal->entries_per_block = VDO_SLAB_JOURNAL_ENTRIES_PER_BLOCK;
	journal->full_entries_per_block = VDO_SLAB_JOURNAL_FULL_ENTRIES_PER_BLOCK;
	journal->events = &slab->allocator->slab_journal_statistics;
	journal->recovery_journal = slab->allocator->depot->vdo->recovery_journal;
	journal->tail = 1;
	journal->head = 1;

	journal->flushing_deadline = journal->flushing_threshold;
	/*
	 * Set there to be some time between the deadline and the blocking threshold, so that
	 * hopefully all are done before blocking.
	 */
	if ((journal->blocking_threshold - journal->flushing_threshold) > 5)
		journal->flushing_deadline = journal->blocking_threshold - 5;

	journal->slab_summary_waiter.callback = release_journal_locks;

	INIT_LIST_HEAD(&journal->dirty_entry);
	INIT_LIST_HEAD(&journal->uncommitted_blocks);

	journal->tail_header.nonce = slab->allocator->nonce;
	journal->tail_header.metadata_type = VDO_METADATA_SLAB_JOURNAL;
	initialize_journal_state(journal);
	return VDO_SUCCESS;
}

/**
 * make_slab() - Construct a new, empty slab.
 * @slab_origin: The physical block number within the block allocator partition of the first block
 *               in the slab.
 * @allocator: The block allocator to which the slab belongs.
 * @slab_number: The slab number of the slab.
 * @is_new: true if this slab is being allocated as part of a resize.
 * @slab_ptr: A pointer to receive the new slab.
 *
 * Return: VDO_SUCCESS or an error code.
 */
static int __must_check make_slab(physical_block_number_t slab_origin,
				  struct block_allocator *allocator,
				  slab_count_t slab_number, bool is_new,
				  struct vdo_slab **slab_ptr)
{
	const struct slab_config *slab_config = &allocator->depot->slab_config;
	struct vdo_slab *slab;
	int result;

	result = vdo_allocate(1, struct vdo_slab, __func__, &slab);
	if (result != VDO_SUCCESS)
		return result;

	*slab = (struct vdo_slab) {
		.allocator = allocator,
		.start = slab_origin,
		.end = slab_origin + slab_config->slab_blocks,
		.slab_number = slab_number,
		.ref_counts_origin = slab_origin + slab_config->data_blocks,
		.journal_origin =
			vdo_get_slab_journal_start_block(slab_config, slab_origin),
		.block_count = slab_config->data_blocks,
		.free_blocks = slab_config->data_blocks,
		.reference_block_count =
			vdo_get_saved_reference_count_size(slab_config->data_blocks),
	};
	INIT_LIST_HEAD(&slab->allocq_entry);

	result = initialize_slab_journal(slab);
	if (result != VDO_SUCCESS) {
		free_slab(slab);
		return result;
	}

	if (is_new) {
		vdo_set_admin_state_code(&slab->state, VDO_ADMIN_STATE_NEW);
		result = allocate_slab_counters(slab);
		if (result != VDO_SUCCESS) {
			free_slab(slab);
			return result;
		}
	} else {
		vdo_set_admin_state_code(&slab->state, VDO_ADMIN_STATE_NORMAL_OPERATION);
	}

	*slab_ptr = slab;
	return VDO_SUCCESS;
}

/**
 * allocate_slabs() - Allocate a new slab pointer array.
 * @depot: The depot.
 * @slab_count: The number of slabs the depot should have in the new array.
 *
 * Any existing slab pointers will be copied into the new array, and slabs will be allocated as
 * needed. The newly allocated slabs will not be distributed for use by the block allocators.
 *
 * Return: VDO_SUCCESS or an error code.
 */
static int allocate_slabs(struct slab_depot *depot, slab_count_t slab_count)
{
	block_count_t slab_size;
	bool resizing = false;
	physical_block_number_t slab_origin;
	int result;

	result = vdo_allocate(slab_count, struct vdo_slab *,
			      "slab pointer array", &depot->new_slabs);
	if (result != VDO_SUCCESS)
		return result;

	if (depot->slabs != NULL) {
		memcpy(depot->new_slabs, depot->slabs,
		       depot->slab_count * sizeof(struct vdo_slab *));
		resizing = true;
	}

	slab_size = depot->slab_config.slab_blocks;
	slab_origin = depot->first_block + (depot->slab_count * slab_size);

	for (depot->new_slab_count = depot->slab_count;
	     depot->new_slab_count < slab_count;
	     depot->new_slab_count++, slab_origin += slab_size) {
		struct block_allocator *allocator =
			&depot->allocators[depot->new_slab_count % depot->zone_count];
		struct vdo_slab **slab_ptr = &depot->new_slabs[depot->new_slab_count];

		result = make_slab(slab_origin, allocator, depot->new_slab_count,
				   resizing, slab_ptr);
		if (result != VDO_SUCCESS)
			return result;
	}

	return VDO_SUCCESS;
}

/**
 * vdo_abandon_new_slabs() - Abandon any new slabs in this depot, freeing them as needed.
 * @depot: The depot.
 */
void vdo_abandon_new_slabs(struct slab_depot *depot)
{
	slab_count_t i;

	if (depot->new_slabs == NULL)
		return;

	for (i = depot->slab_count; i < depot->new_slab_count; i++)
		free_slab(vdo_forget(depot->new_slabs[i]));
	depot->new_slab_count = 0;
	depot->new_size = 0;
	vdo_free(vdo_forget(depot->new_slabs));
}

/**
 * get_allocator_thread_id() - Get the ID of the thread on which a given allocator operates.
 *
 * Implements vdo_zone_thread_getter_fn.
 */
static thread_id_t get_allocator_thread_id(void *context, zone_count_t zone_number)
{
	return ((struct slab_depot *) context)->allocators[zone_number].thread_id;
}

/**
 * release_recovery_journal_lock() - Request the slab journal to release the recovery journal lock
 *                                   it may hold on a specified recovery journal block.
 * @journal: The slab journal.
 * @recovery_lock: The sequence number of the recovery journal block whose locks should be
 *                 released.
 *
 * Return: true if the journal does hold a lock on the specified block (which it will release).
 */
static bool __must_check release_recovery_journal_lock(struct slab_journal *journal,
						       sequence_number_t recovery_lock)
{
	if (recovery_lock > journal->recovery_lock) {
		VDO_ASSERT_LOG_ONLY((recovery_lock < journal->recovery_lock),
				    "slab journal recovery lock is not older than the recovery journal head");
		return false;
	}

	if ((recovery_lock < journal->recovery_lock) ||
	    vdo_is_read_only(journal->slab->allocator->depot->vdo))
		return false;

	/* All locks are held by the block which is in progress; write it. */
	commit_tail(journal);
	return true;
}

/*
 * Request a commit of all dirty tail blocks which are locking the recovery journal block the depot
 * is seeking to release.
 *
 * Implements vdo_zone_action_fn.
 */
static void release_tail_block_locks(void *context, zone_count_t zone_number,
				     struct vdo_completion *parent)
{
	struct slab_journal *journal, *tmp;
	struct slab_depot *depot = context;
	struct list_head *list = &depot->allocators[zone_number].dirty_slab_journals;

	list_for_each_entry_safe(journal, tmp, list, dirty_entry) {
		if (!release_recovery_journal_lock(journal,
						   depot->active_release_request))
			break;
	}

	vdo_finish_completion(parent);
}

/**
 * prepare_for_tail_block_commit() - Prepare to commit oldest tail blocks.
 *
 * Implements vdo_action_preamble_fn.
 */
static void prepare_for_tail_block_commit(void *context, struct vdo_completion *parent)
{
	struct slab_depot *depot = context;

	depot->active_release_request = depot->new_release_request;
	vdo_finish_completion(parent);
}

/**
 * schedule_tail_block_commit() - Schedule a tail block commit if necessary.
 *
 * This method should not be called directly. Rather, call vdo_schedule_default_action() on the
 * depot's action manager.
 *
 * Implements vdo_action_scheduler_fn.
 */
static bool schedule_tail_block_commit(void *context)
{
	struct slab_depot *depot = context;

	if (depot->new_release_request == depot->active_release_request)
		return false;

	return vdo_schedule_action(depot->action_manager,
				   prepare_for_tail_block_commit,
				   release_tail_block_locks,
				   NULL, NULL);
}

/**
 * initialize_slab_scrubber() - Initialize an allocator's slab scrubber.
 * @allocator: The allocator being initialized
 *
 * Return: VDO_SUCCESS or an error.
 */
static int initialize_slab_scrubber(struct block_allocator *allocator)
{
	struct slab_scrubber *scrubber = &allocator->scrubber;
	block_count_t slab_journal_size =
		allocator->depot->slab_config.slab_journal_blocks;
	char *journal_data;
	int result;

	result = vdo_allocate(VDO_BLOCK_SIZE * slab_journal_size,
			      char, __func__, &journal_data);
	if (result != VDO_SUCCESS)
		return result;

	result = allocate_vio_components(allocator->completion.vdo,
					 VIO_TYPE_SLAB_JOURNAL,
					 VIO_PRIORITY_METADATA,
					 allocator, slab_journal_size,
					 journal_data, &scrubber->vio);
	if (result != VDO_SUCCESS) {
		vdo_free(journal_data);
		return result;
	}

	INIT_LIST_HEAD(&scrubber->high_priority_slabs);
	INIT_LIST_HEAD(&scrubber->slabs);
	vdo_set_admin_state_code(&scrubber->admin_state, VDO_ADMIN_STATE_SUSPENDED);
	return VDO_SUCCESS;
}

/**
 * initialize_slab_summary_block() - Initialize a slab_summary_block.
 * @allocator: The allocator which owns the block.
 * @index: The index of this block in its zone's summary.
 *
 * Return: VDO_SUCCESS or an error.
 */
static int __must_check initialize_slab_summary_block(struct block_allocator *allocator,
						      block_count_t index)
{
	struct slab_summary_block *block = &allocator->summary_blocks[index];
	int result;

	result = vdo_allocate(VDO_BLOCK_SIZE, char, __func__, &block->outgoing_entries);
	if (result != VDO_SUCCESS)
		return result;

	result = allocate_vio_components(allocator->depot->vdo, VIO_TYPE_SLAB_SUMMARY,
					 VIO_PRIORITY_METADATA, NULL, 1,
					 block->outgoing_entries, &block->vio);
	if (result != VDO_SUCCESS)
		return result;

	block->allocator = allocator;
	block->entries = &allocator->summary_entries[VDO_SLAB_SUMMARY_ENTRIES_PER_BLOCK * index];
	block->index = index;
	return VDO_SUCCESS;
}

static int __must_check initialize_block_allocator(struct slab_depot *depot,
						   zone_count_t zone)
{
	int result;
	block_count_t i;
	struct block_allocator *allocator = &depot->allocators[zone];
	struct vdo *vdo = depot->vdo;
	block_count_t max_free_blocks = depot->slab_config.data_blocks;
	unsigned int max_priority = (2 + ilog2(max_free_blocks));

	*allocator = (struct block_allocator) {
		.depot = depot,
		.zone_number = zone,
		.thread_id = vdo->thread_config.physical_threads[zone],
		.nonce = vdo->states.vdo.nonce,
	};

	INIT_LIST_HEAD(&allocator->dirty_slab_journals);
	vdo_set_admin_state_code(&allocator->state, VDO_ADMIN_STATE_NORMAL_OPERATION);
	result = vdo_register_read_only_listener(vdo, allocator,
						 notify_block_allocator_of_read_only_mode,
						 allocator->thread_id);
	if (result != VDO_SUCCESS)
		return result;

	vdo_initialize_completion(&allocator->completion, vdo, VDO_BLOCK_ALLOCATOR_COMPLETION);
	result = make_vio_pool(vdo, BLOCK_ALLOCATOR_VIO_POOL_SIZE, allocator->thread_id,
			       VIO_TYPE_SLAB_JOURNAL, VIO_PRIORITY_METADATA,
			       allocator, &allocator->vio_pool);
	if (result != VDO_SUCCESS)
		return result;

	result = initialize_slab_scrubber(allocator);
	if (result != VDO_SUCCESS)
		return result;

	result = vdo_make_priority_table(max_priority, &allocator->prioritized_slabs);
	if (result != VDO_SUCCESS)
		return result;

	result = vdo_allocate(VDO_SLAB_SUMMARY_BLOCKS_PER_ZONE,
			      struct slab_summary_block, __func__,
			      &allocator->summary_blocks);
	if (result != VDO_SUCCESS)
		return result;

	vdo_set_admin_state_code(&allocator->summary_state,
				 VDO_ADMIN_STATE_NORMAL_OPERATION);
	allocator->summary_entries = depot->summary_entries + (MAX_VDO_SLABS * zone);

	/* Initialize each summary block. */
	for (i = 0; i < VDO_SLAB_SUMMARY_BLOCKS_PER_ZONE; i++) {
		result = initialize_slab_summary_block(allocator, i);
		if (result != VDO_SUCCESS)
			return result;
	}

	/*
	 * Performing well atop thin provisioned storage requires either that VDO discards freed
	 * blocks, or that the block allocator try to use slabs that already have allocated blocks
	 * in preference to slabs that have never been opened. For reasons we have not been able to
	 * fully understand, some SSD machines have been have been very sensitive (50% reduction in
	 * test throughput) to very slight differences in the timing and locality of block
	 * allocation. Assigning a low priority to unopened slabs (max_priority/2, say) would be
	 * ideal for the story, but anything less than a very high threshold (max_priority - 1)
	 * hurts on these machines.
	 *
	 * This sets the free block threshold for preferring to open an unopened slab to the binary
	 * floor of 3/4ths the total number of data blocks in a slab, which will generally evaluate
	 * to about half the slab size.
	 */
	allocator->unopened_slab_priority = (1 + ilog2((max_free_blocks * 3) / 4));

	return VDO_SUCCESS;
}

static int allocate_components(struct slab_depot *depot,
			       struct partition *summary_partition)
{
	int result;
	zone_count_t zone;
	slab_count_t slab_count;
	u8 hint;
	u32 i;
	const struct thread_config *thread_config = &depot->vdo->thread_config;

	result = vdo_make_action_manager(depot->zone_count, get_allocator_thread_id,
					 thread_config->journal_thread, depot,
					 schedule_tail_block_commit,
					 depot->vdo, &depot->action_manager);
	if (result != VDO_SUCCESS)
		return result;

	depot->origin = depot->first_block;

	/* block size must be a multiple of entry size */
	BUILD_BUG_ON((VDO_BLOCK_SIZE % sizeof(struct slab_summary_entry)) != 0);

	depot->summary_origin = summary_partition->offset;
	depot->hint_shift = vdo_get_slab_summary_hint_shift(depot->slab_size_shift);
	result = vdo_allocate(MAXIMUM_VDO_SLAB_SUMMARY_ENTRIES,
			      struct slab_summary_entry, __func__,
			      &depot->summary_entries);
	if (result != VDO_SUCCESS)
		return result;


	/* Initialize all the entries. */
	hint = compute_fullness_hint(depot, depot->slab_config.data_blocks);
	for (i = 0; i < MAXIMUM_VDO_SLAB_SUMMARY_ENTRIES; i++) {
		/*
		 * This default tail block offset must be reflected in
		 * slabJournal.c::read_slab_journal_tail().
		 */
		depot->summary_entries[i] = (struct slab_summary_entry) {
			.tail_block_offset = 0,
			.fullness_hint = hint,
			.load_ref_counts = false,
			.is_dirty = false,
		};
	}

	slab_count = vdo_compute_slab_count(depot->first_block, depot->last_block,
					    depot->slab_size_shift);
	if (thread_config->physical_zone_count > slab_count) {
		return vdo_log_error_strerror(VDO_BAD_CONFIGURATION,
					      "%u physical zones exceeds slab count %u",
					      thread_config->physical_zone_count,
					      slab_count);
	}

	/* Initialize the block allocators. */
	for (zone = 0; zone < depot->zone_count; zone++) {
		result = initialize_block_allocator(depot, zone);
		if (result != VDO_SUCCESS)
			return result;
	}

	/* Allocate slabs. */
	result = allocate_slabs(depot, slab_count);
	if (result != VDO_SUCCESS)
		return result;

	/* Use the new slabs. */
	for (i = depot->slab_count; i < depot->new_slab_count; i++) {
		struct vdo_slab *slab = depot->new_slabs[i];

		register_slab_with_allocator(slab->allocator, slab);
		WRITE_ONCE(depot->slab_count, depot->slab_count + 1);
	}

	depot->slabs = depot->new_slabs;
	depot->new_slabs = NULL;
	depot->new_slab_count = 0;

	return VDO_SUCCESS;
}

/**
 * vdo_decode_slab_depot() - Make a slab depot and configure it with the state read from the super
 *                           block.
 * @state: The slab depot state from the super block.
 * @vdo: The VDO which will own the depot.
 * @summary_partition: The partition which holds the slab summary.
 * @depot_ptr: A pointer to hold the depot.
 *
 * Return: A success or error code.
 */
int vdo_decode_slab_depot(struct slab_depot_state_2_0 state, struct vdo *vdo,
			  struct partition *summary_partition,
			  struct slab_depot **depot_ptr)
{
	unsigned int slab_size_shift;
	struct slab_depot *depot;
	int result;

	/*
	 * Calculate the bit shift for efficiently mapping block numbers to slabs. Using a shift
	 * requires that the slab size be a power of two.
	 */
	block_count_t slab_size = state.slab_config.slab_blocks;

	if (!is_power_of_2(slab_size)) {
		return vdo_log_error_strerror(UDS_INVALID_ARGUMENT,
					      "slab size must be a power of two");
	}
	slab_size_shift = ilog2(slab_size);

	result = vdo_allocate_extended(struct slab_depot,
				       vdo->thread_config.physical_zone_count,
				       struct block_allocator, __func__, &depot);
	if (result != VDO_SUCCESS)
		return result;

	depot->vdo = vdo;
	depot->old_zone_count = state.zone_count;
	depot->zone_count = vdo->thread_config.physical_zone_count;
	depot->slab_config = state.slab_config;
	depot->first_block = state.first_block;
	depot->last_block = state.last_block;
	depot->slab_size_shift = slab_size_shift;

	result = allocate_components(depot, summary_partition);
	if (result != VDO_SUCCESS) {
		vdo_free_slab_depot(depot);
		return result;
	}

	*depot_ptr = depot;
	return VDO_SUCCESS;
}

static void uninitialize_allocator_summary(struct block_allocator *allocator)
{
	block_count_t i;

	if (allocator->summary_blocks == NULL)
		return;

	for (i = 0; i < VDO_SLAB_SUMMARY_BLOCKS_PER_ZONE; i++) {
		free_vio_components(&allocator->summary_blocks[i].vio);
		vdo_free(vdo_forget(allocator->summary_blocks[i].outgoing_entries));
	}

	vdo_free(vdo_forget(allocator->summary_blocks));
}

/**
 * vdo_free_slab_depot() - Destroy a slab depot.
 * @depot: The depot to destroy.
 */
void vdo_free_slab_depot(struct slab_depot *depot)
{
	zone_count_t zone = 0;

	if (depot == NULL)
		return;

	vdo_abandon_new_slabs(depot);

	for (zone = 0; zone < depot->zone_count; zone++) {
		struct block_allocator *allocator = &depot->allocators[zone];

		if (allocator->eraser != NULL)
			dm_kcopyd_client_destroy(vdo_forget(allocator->eraser));

		uninitialize_allocator_summary(allocator);
		uninitialize_scrubber_vio(&allocator->scrubber);
		free_vio_pool(vdo_forget(allocator->vio_pool));
		vdo_free_priority_table(vdo_forget(allocator->prioritized_slabs));
	}

	if (depot->slabs != NULL) {
		slab_count_t i;

		for (i = 0; i < depot->slab_count; i++)
			free_slab(vdo_forget(depot->slabs[i]));
	}

	vdo_free(vdo_forget(depot->slabs));
	vdo_free(vdo_forget(depot->action_manager));
	vdo_free(vdo_forget(depot->summary_entries));
	vdo_free(depot);
}

/**
 * vdo_record_slab_depot() - Record the state of a slab depot for encoding into the super block.
 * @depot: The depot to encode.
 *
 * Return: The depot state.
 */
struct slab_depot_state_2_0 vdo_record_slab_depot(const struct slab_depot *depot)
{
	/*
	 * If this depot is currently using 0 zones, it must have been synchronously loaded by a
	 * tool and is now being saved. We did not load and combine the slab summary, so we still
	 * need to do that next time we load with the old zone count rather than 0.
	 */
	struct slab_depot_state_2_0 state;
	zone_count_t zones_to_record = depot->zone_count;

	if (depot->zone_count == 0)
		zones_to_record = depot->old_zone_count;

	state = (struct slab_depot_state_2_0) {
		.slab_config = depot->slab_config,
		.first_block = depot->first_block,
		.last_block = depot->last_block,
		.zone_count = zones_to_record,
	};

	return state;
}

/**
 * vdo_allocate_reference_counters() - Allocate the reference counters for all slabs in the depot.
 *
 * Context: This method may be called only before entering normal operation from the load thread.
 *
 * Return: VDO_SUCCESS or an error.
 */
int vdo_allocate_reference_counters(struct slab_depot *depot)
{
	struct slab_iterator iterator =
		get_depot_slab_iterator(depot, depot->slab_count - 1, 0, 1);

	while (iterator.next != NULL) {
		int result = allocate_slab_counters(next_slab(&iterator));

		if (result != VDO_SUCCESS)
			return result;
	}

	return VDO_SUCCESS;
}

/**
 * get_slab_number() - Get the number of the slab that contains a specified block.
 * @depot: The slab depot.
 * @pbn: The physical block number.
 * @slab_number_ptr: A pointer to hold the slab number.
 *
 * Return: VDO_SUCCESS or an error.
 */
static int __must_check get_slab_number(const struct slab_depot *depot,
					physical_block_number_t pbn,
					slab_count_t *slab_number_ptr)
{
	slab_count_t slab_number;

	if (pbn < depot->first_block)
		return VDO_OUT_OF_RANGE;

	slab_number = (pbn - depot->first_block) >> depot->slab_size_shift;
	if (slab_number >= depot->slab_count)
		return VDO_OUT_OF_RANGE;

	*slab_number_ptr = slab_number;
	return VDO_SUCCESS;
}

/**
 * vdo_get_slab() - Get the slab object for the slab that contains a specified block.
 * @depot: The slab depot.
 * @pbn: The physical block number.
 *
 * Will put the VDO in read-only mode if the PBN is not a valid data block nor the zero block.
 *
 * Return: The slab containing the block, or NULL if the block number is the zero block or
 * otherwise out of range.
 */
struct vdo_slab *vdo_get_slab(const struct slab_depot *depot,
			      physical_block_number_t pbn)
{
	slab_count_t slab_number;
	int result;

	if (pbn == VDO_ZERO_BLOCK)
		return NULL;

	result = get_slab_number(depot, pbn, &slab_number);
	if (result != VDO_SUCCESS) {
		vdo_enter_read_only_mode(depot->vdo, result);
		return NULL;
	}

	return depot->slabs[slab_number];
}

/**
 * vdo_get_increment_limit() - Determine how many new references a block can acquire.
 * @depot: The slab depot.
 * @pbn: The physical block number that is being queried.
 *
 * Context: This method must be called from the physical zone thread of the PBN.
 *
 * Return: The number of available references.
 */
u8 vdo_get_increment_limit(struct slab_depot *depot, physical_block_number_t pbn)
{
	struct vdo_slab *slab = vdo_get_slab(depot, pbn);
	vdo_refcount_t *counter_ptr = NULL;
	int result;

	if ((slab == NULL) || (slab->status != VDO_SLAB_REBUILT))
		return 0;

	result = get_reference_counter(slab, pbn, &counter_ptr);
	if (result != VDO_SUCCESS)
		return 0;

	if (*counter_ptr == PROVISIONAL_REFERENCE_COUNT)
		return (MAXIMUM_REFERENCE_COUNT - 1);

	return (MAXIMUM_REFERENCE_COUNT - *counter_ptr);
}

/**
 * vdo_is_physical_data_block() - Determine whether the given PBN refers to a data block.
 * @depot: The depot.
 * @pbn: The physical block number to ask about.
 *
 * Return: True if the PBN corresponds to a data block.
 */
bool vdo_is_physical_data_block(const struct slab_depot *depot,
				physical_block_number_t pbn)
{
	slab_count_t slab_number;
	slab_block_number sbn;

	return ((pbn == VDO_ZERO_BLOCK) ||
		((get_slab_number(depot, pbn, &slab_number) == VDO_SUCCESS) &&
		 (slab_block_number_from_pbn(depot->slabs[slab_number], pbn, &sbn) ==
		  VDO_SUCCESS)));
}

/**
 * vdo_get_slab_depot_allocated_blocks() - Get the total number of data blocks allocated across all
 * the slabs in the depot.
 * @depot: The slab depot.
 *
 * This is the total number of blocks with a non-zero reference count.
 *
 * Context: This may be called from any thread.
 *
 * Return: The total number of blocks with a non-zero reference count.
 */
block_count_t vdo_get_slab_depot_allocated_blocks(const struct slab_depot *depot)
{
	block_count_t total = 0;
	zone_count_t zone;

	for (zone = 0; zone < depot->zone_count; zone++) {
		/* The allocators are responsible for thread safety. */
		total += READ_ONCE(depot->allocators[zone].allocated_blocks);
	}

	return total;
}

/**
 * vdo_get_slab_depot_data_blocks() - Get the total number of data blocks in all the slabs in the
 *                                    depot.
 * @depot: The slab depot.
 *
 * Context: This may be called from any thread.
 *
 * Return: The total number of data blocks in all slabs.
 */
block_count_t vdo_get_slab_depot_data_blocks(const struct slab_depot *depot)
{
	return (READ_ONCE(depot->slab_count) * depot->slab_config.data_blocks);
}

/**
 * finish_combining_zones() - Clean up after saving out the combined slab summary.
 * @completion: The vio which was used to write the summary data.
 */
static void finish_combining_zones(struct vdo_completion *completion)
{
	int result = completion->result;
	struct vdo_completion *parent = completion->parent;

	free_vio(as_vio(vdo_forget(completion)));
	vdo_fail_completion(parent, result);
}

static void handle_combining_error(struct vdo_completion *completion)
{
	vio_record_metadata_io_error(as_vio(completion));
	finish_combining_zones(completion);
}

static void write_summary_endio(struct bio *bio)
{
	struct vio *vio = bio->bi_private;
	struct vdo *vdo = vio->completion.vdo;

	continue_vio_after_io(vio, finish_combining_zones,
			      vdo->thread_config.admin_thread);
}

/**
 * combine_summaries() - Treating the current entries buffer as the on-disk value of all zones,
 *                       update every zone to the correct values for every slab.
 * @depot: The depot whose summary entries should be combined.
 */
static void combine_summaries(struct slab_depot *depot)
{
	/*
	 * Combine all the old summary data into the portion of the buffer corresponding to the
	 * first zone.
	 */
	zone_count_t zone = 0;
	struct slab_summary_entry *entries = depot->summary_entries;

	if (depot->old_zone_count > 1) {
		slab_count_t entry_number;

		for (entry_number = 0; entry_number < MAX_VDO_SLABS; entry_number++) {
			if (zone != 0) {
				memcpy(entries + entry_number,
				       entries + (zone * MAX_VDO_SLABS) + entry_number,
				       sizeof(struct slab_summary_entry));
			}

			zone++;
			if (zone == depot->old_zone_count)
				zone = 0;
		}
	}

	/* Copy the combined data to each zones's region of the buffer. */
	for (zone = 1; zone < MAX_VDO_PHYSICAL_ZONES; zone++) {
		memcpy(entries + (zone * MAX_VDO_SLABS), entries,
		       MAX_VDO_SLABS * sizeof(struct slab_summary_entry));
	}
}

/**
 * finish_loading_summary() - Finish loading slab summary data.
 * @completion: The vio which was used to read the summary data.
 *
 * Combines the slab summary data from all the previously written zones and copies the combined
 * summary to each partition's data region. Then writes the combined summary back out to disk. This
 * callback is registered in load_summary_endio().
 */
static void finish_loading_summary(struct vdo_completion *completion)
{
	struct slab_depot *depot = completion->vdo->depot;

	/* Combine the summary from each zone so each zone is correct for all slabs. */
	combine_summaries(depot);

	/* Write the combined summary back out. */
	vdo_submit_metadata_vio(as_vio(completion), depot->summary_origin,
				write_summary_endio, handle_combining_error,
				REQ_OP_WRITE);
}

static void load_summary_endio(struct bio *bio)
{
	struct vio *vio = bio->bi_private;
	struct vdo *vdo = vio->completion.vdo;

	continue_vio_after_io(vio, finish_loading_summary,
			      vdo->thread_config.admin_thread);
}

/**
 * load_slab_summary() - The preamble of a load operation.
 *
 * Implements vdo_action_preamble_fn.
 */
static void load_slab_summary(void *context, struct vdo_completion *parent)
{
	int result;
	struct vio *vio;
	struct slab_depot *depot = context;
	const struct admin_state_code *operation =
		vdo_get_current_manager_operation(depot->action_manager);

	result = create_multi_block_metadata_vio(depot->vdo, VIO_TYPE_SLAB_SUMMARY,
						 VIO_PRIORITY_METADATA, parent,
						 VDO_SLAB_SUMMARY_BLOCKS,
						 (char *) depot->summary_entries, &vio);
	if (result != VDO_SUCCESS) {
		vdo_fail_completion(parent, result);
		return;
	}

	if ((operation == VDO_ADMIN_STATE_FORMATTING) ||
	    (operation == VDO_ADMIN_STATE_LOADING_FOR_REBUILD)) {
		finish_loading_summary(&vio->completion);
		return;
	}

	vdo_submit_metadata_vio(vio, depot->summary_origin, load_summary_endio,
				handle_combining_error, REQ_OP_READ);
}

/* Implements vdo_zone_action_fn. */
static void load_allocator(void *context, zone_count_t zone_number,
			   struct vdo_completion *parent)
{
	struct slab_depot *depot = context;

	vdo_start_loading(&depot->allocators[zone_number].state,
			  vdo_get_current_manager_operation(depot->action_manager),
			  parent, initiate_load);
}

/**
 * vdo_load_slab_depot() - Asynchronously load any slab depot state that isn't included in the
 *                         super_block component.
 * @depot: The depot to load.
 * @operation: The type of load to perform.
 * @parent: The completion to notify when the load is complete.
 * @context: Additional context for the load operation; may be NULL.
 *
 * This method may be called only before entering normal operation from the load thread.
 */
void vdo_load_slab_depot(struct slab_depot *depot,
			 const struct admin_state_code *operation,
			 struct vdo_completion *parent, void *context)
{
	if (!vdo_assert_load_operation(operation, parent))
		return;

	vdo_schedule_operation_with_context(depot->action_manager, operation,
					    load_slab_summary, load_allocator,
					    NULL, context, parent);
}

/* Implements vdo_zone_action_fn. */
static void prepare_to_allocate(void *context, zone_count_t zone_number,
				struct vdo_completion *parent)
{
	struct slab_depot *depot = context;
	struct block_allocator *allocator = &depot->allocators[zone_number];
	int result;

	result = vdo_prepare_slabs_for_allocation(allocator);
	if (result != VDO_SUCCESS) {
		vdo_fail_completion(parent, result);
		return;
	}

	scrub_slabs(allocator, parent);
}

/**
 * vdo_prepare_slab_depot_to_allocate() - Prepare the slab depot to come online and start
 *                                        allocating blocks.
 * @depot: The depot to prepare.
 * @load_type: The load type.
 * @parent: The completion to notify when the operation is complete.
 *
 * This method may be called only before entering normal operation from the load thread. It must be
 * called before allocation may proceed.
 */
void vdo_prepare_slab_depot_to_allocate(struct slab_depot *depot,
					enum slab_depot_load_type load_type,
					struct vdo_completion *parent)
{
	depot->load_type = load_type;
	atomic_set(&depot->zones_to_scrub, depot->zone_count);
	vdo_schedule_action(depot->action_manager, NULL,
			    prepare_to_allocate, NULL, parent);
}

/**
 * vdo_update_slab_depot_size() - Update the slab depot to reflect its new size in memory.
 * @depot: The depot to update.
 *
 * This size is saved to disk as part of the super block.
 */
void vdo_update_slab_depot_size(struct slab_depot *depot)
{
	depot->last_block = depot->new_last_block;
}

/**
 * vdo_prepare_to_grow_slab_depot() - Allocate new memory needed for a resize of a slab depot to
 *                                    the given size.
 * @depot: The depot to prepare to resize.
 * @partition: The new depot partition
 *
 * Return: VDO_SUCCESS or an error.
 */
int vdo_prepare_to_grow_slab_depot(struct slab_depot *depot,
				   const struct partition *partition)
{
	struct slab_depot_state_2_0 new_state;
	int result;
	slab_count_t new_slab_count;

	if ((partition->count >> depot->slab_size_shift) <= depot->slab_count)
		return VDO_INCREMENT_TOO_SMALL;

	/* Generate the depot configuration for the new block count. */
	VDO_ASSERT_LOG_ONLY(depot->first_block == partition->offset,
			    "New slab depot partition doesn't change origin");
	result = vdo_configure_slab_depot(partition, depot->slab_config,
					  depot->zone_count, &new_state);
	if (result != VDO_SUCCESS)
		return result;

	new_slab_count = vdo_compute_slab_count(depot->first_block,
						new_state.last_block,
						depot->slab_size_shift);
	if (new_slab_count <= depot->slab_count)
		return vdo_log_error_strerror(VDO_INCREMENT_TOO_SMALL,
					      "Depot can only grow");
	if (new_slab_count == depot->new_slab_count) {
		/* Check it out, we've already got all the new slabs allocated! */
		return VDO_SUCCESS;
	}

	vdo_abandon_new_slabs(depot);
	result = allocate_slabs(depot, new_slab_count);
	if (result != VDO_SUCCESS) {
		vdo_abandon_new_slabs(depot);
		return result;
	}

	depot->new_size = partition->count;
	depot->old_last_block = depot->last_block;
	depot->new_last_block = new_state.last_block;

	return VDO_SUCCESS;
}

/**
 * finish_registration() - Finish registering new slabs now that all of the allocators have
 *                         received their new slabs.
 *
 * Implements vdo_action_conclusion_fn.
 */
static int finish_registration(void *context)
{
	struct slab_depot *depot = context;

	WRITE_ONCE(depot->slab_count, depot->new_slab_count);
	vdo_free(depot->slabs);
	depot->slabs = depot->new_slabs;
	depot->new_slabs = NULL;
	depot->new_slab_count = 0;
	return VDO_SUCCESS;
}

/* Implements vdo_zone_action_fn. */
static void register_new_slabs(void *context, zone_count_t zone_number,
			       struct vdo_completion *parent)
{
	struct slab_depot *depot = context;
	struct block_allocator *allocator = &depot->allocators[zone_number];
	slab_count_t i;

	for (i = depot->slab_count; i < depot->new_slab_count; i++) {
		struct vdo_slab *slab = depot->new_slabs[i];

		if (slab->allocator == allocator)
			register_slab_with_allocator(allocator, slab);
	}

	vdo_finish_completion(parent);
}

/**
 * vdo_use_new_slabs() - Use the new slabs allocated for resize.
 * @depot: The depot.
 * @parent: The object to notify when complete.
 */
void vdo_use_new_slabs(struct slab_depot *depot, struct vdo_completion *parent)
{
	VDO_ASSERT_LOG_ONLY(depot->new_slabs != NULL, "Must have new slabs to use");
	vdo_schedule_operation(depot->action_manager,
			       VDO_ADMIN_STATE_SUSPENDED_OPERATION,
			       NULL, register_new_slabs,
			       finish_registration, parent);
}

/**
 * stop_scrubbing() - Tell the scrubber to stop scrubbing after it finishes the slab it is
 *                    currently working on.
 * @scrubber: The scrubber to stop.
 * @parent: The completion to notify when scrubbing has stopped.
 */
static void stop_scrubbing(struct block_allocator *allocator)
{
	struct slab_scrubber *scrubber = &allocator->scrubber;

	if (vdo_is_state_quiescent(&scrubber->admin_state)) {
		vdo_finish_completion(&allocator->completion);
	} else {
		vdo_start_draining(&scrubber->admin_state,
				   VDO_ADMIN_STATE_SUSPENDING,
				   &allocator->completion, NULL);
	}
}

/* Implements vdo_admin_initiator_fn. */
static void initiate_summary_drain(struct admin_state *state)
{
	check_summary_drain_complete(container_of(state, struct block_allocator,
						  summary_state));
}

static void do_drain_step(struct vdo_completion *completion)
{
	struct block_allocator *allocator = vdo_as_block_allocator(completion);

	vdo_prepare_completion_for_requeue(&allocator->completion, do_drain_step,
					   handle_operation_error, allocator->thread_id,
					   NULL);
	switch (++allocator->drain_step) {
	case VDO_DRAIN_ALLOCATOR_STEP_SCRUBBER:
		stop_scrubbing(allocator);
		return;

	case VDO_DRAIN_ALLOCATOR_STEP_SLABS:
		apply_to_slabs(allocator, do_drain_step);
		return;

	case VDO_DRAIN_ALLOCATOR_STEP_SUMMARY:
		vdo_start_draining(&allocator->summary_state,
				   vdo_get_admin_state_code(&allocator->state),
				   completion, initiate_summary_drain);
		return;

	case VDO_DRAIN_ALLOCATOR_STEP_FINISHED:
		VDO_ASSERT_LOG_ONLY(!is_vio_pool_busy(allocator->vio_pool),
				    "vio pool not busy");
		vdo_finish_draining_with_result(&allocator->state, completion->result);
		return;

	default:
		vdo_finish_draining_with_result(&allocator->state, UDS_BAD_STATE);
	}
}

/* Implements vdo_admin_initiator_fn. */
static void initiate_drain(struct admin_state *state)
{
	struct block_allocator *allocator =
		container_of(state, struct block_allocator, state);

	allocator->drain_step = VDO_DRAIN_ALLOCATOR_START;
	do_drain_step(&allocator->completion);
}

/*
 * Drain all allocator I/O. Depending upon the type of drain, some or all dirty metadata may be
 * written to disk. The type of drain will be determined from the state of the allocator's depot.
 *
 * Implements vdo_zone_action_fn.
 */
static void drain_allocator(void *context, zone_count_t zone_number,
			    struct vdo_completion *parent)
{
	struct slab_depot *depot = context;

	vdo_start_draining(&depot->allocators[zone_number].state,
			   vdo_get_current_manager_operation(depot->action_manager),
			   parent, initiate_drain);
}

/**
 * vdo_drain_slab_depot() - Drain all slab depot I/O.
 * @depot: The depot to drain.
 * @operation: The drain operation (flush, rebuild, suspend, or save).
 * @parent: The completion to finish when the drain is complete.
 *
 * If saving, or flushing, all dirty depot metadata will be written out. If saving or suspending,
 * the depot will be left in a suspended state.
 */
void vdo_drain_slab_depot(struct slab_depot *depot,
			  const struct admin_state_code *operation,
			  struct vdo_completion *parent)
{
	vdo_schedule_operation(depot->action_manager, operation,
			       NULL, drain_allocator, NULL, parent);
}

/**
 * resume_scrubbing() - Tell the scrubber to resume scrubbing if it has been stopped.
 * @allocator: The allocator being resumed.
 */
static void resume_scrubbing(struct block_allocator *allocator)
{
	int result;
	struct slab_scrubber *scrubber = &allocator->scrubber;

	if (!has_slabs_to_scrub(scrubber)) {
		vdo_finish_completion(&allocator->completion);
		return;
	}

	result = vdo_resume_if_quiescent(&scrubber->admin_state);
	if (result != VDO_SUCCESS) {
		vdo_fail_completion(&allocator->completion, result);
		return;
	}

	scrub_next_slab(scrubber);
	vdo_finish_completion(&allocator->completion);
}

static void do_resume_step(struct vdo_completion *completion)
{
	struct block_allocator *allocator = vdo_as_block_allocator(completion);

	vdo_prepare_completion_for_requeue(&allocator->completion, do_resume_step,
					   handle_operation_error,
					   allocator->thread_id, NULL);
	switch (--allocator->drain_step) {
	case VDO_DRAIN_ALLOCATOR_STEP_SUMMARY:
		vdo_fail_completion(completion,
				    vdo_resume_if_quiescent(&allocator->summary_state));
		return;

	case VDO_DRAIN_ALLOCATOR_STEP_SLABS:
		apply_to_slabs(allocator, do_resume_step);
		return;

	case VDO_DRAIN_ALLOCATOR_STEP_SCRUBBER:
		resume_scrubbing(allocator);
		return;

	case VDO_DRAIN_ALLOCATOR_START:
		vdo_finish_resuming_with_result(&allocator->state, completion->result);
		return;

	default:
		vdo_finish_resuming_with_result(&allocator->state, UDS_BAD_STATE);
	}
}

/* Implements vdo_admin_initiator_fn. */
static void initiate_resume(struct admin_state *state)
{
	struct block_allocator *allocator =
		container_of(state, struct block_allocator, state);

	allocator->drain_step = VDO_DRAIN_ALLOCATOR_STEP_FINISHED;
	do_resume_step(&allocator->completion);
}

/* Implements vdo_zone_action_fn. */
static void resume_allocator(void *context, zone_count_t zone_number,
			     struct vdo_completion *parent)
{
	struct slab_depot *depot = context;

	vdo_start_resuming(&depot->allocators[zone_number].state,
			   vdo_get_current_manager_operation(depot->action_manager),
			   parent, initiate_resume);
}

/**
 * vdo_resume_slab_depot() - Resume a suspended slab depot.
 * @depot: The depot to resume.
 * @parent: The completion to finish when the depot has resumed.
 */
void vdo_resume_slab_depot(struct slab_depot *depot, struct vdo_completion *parent)
{
	if (vdo_is_read_only(depot->vdo)) {
		vdo_continue_completion(parent, VDO_READ_ONLY);
		return;
	}

	vdo_schedule_operation(depot->action_manager, VDO_ADMIN_STATE_RESUMING,
			       NULL, resume_allocator, NULL, parent);
}

/**
 * vdo_commit_oldest_slab_journal_tail_blocks() - Commit all dirty tail blocks which are locking a
 *                                                given recovery journal block.
 * @depot: The depot.
 * @recovery_block_number: The sequence number of the recovery journal block whose locks should be
 *                         released.
 *
 * Context: This method must be called from the journal zone thread.
 */
void vdo_commit_oldest_slab_journal_tail_blocks(struct slab_depot *depot,
						sequence_number_t recovery_block_number)
{
	if (depot == NULL)
		return;

	depot->new_release_request = recovery_block_number;
	vdo_schedule_default_action(depot->action_manager);
}

/* Implements vdo_zone_action_fn. */
static void scrub_all_unrecovered_slabs(void *context, zone_count_t zone_number,
					struct vdo_completion *parent)
{
	struct slab_depot *depot = context;

	scrub_slabs(&depot->allocators[zone_number], NULL);
	vdo_launch_completion(parent);
}

/**
 * vdo_scrub_all_unrecovered_slabs() - Scrub all unrecovered slabs.
 * @depot: The depot to scrub.
 * @parent: The object to notify when scrubbing has been launched for all zones.
 */
void vdo_scrub_all_unrecovered_slabs(struct slab_depot *depot,
				     struct vdo_completion *parent)
{
	vdo_schedule_action(depot->action_manager, NULL,
			    scrub_all_unrecovered_slabs,
			    NULL, parent);
}

/**
 * get_block_allocator_statistics() - Get the total of the statistics from all the block allocators
 *                                    in the depot.
 * @depot: The slab depot.
 *
 * Return: The statistics from all block allocators in the depot.
 */
static struct block_allocator_statistics __must_check
get_block_allocator_statistics(const struct slab_depot *depot)
{
	struct block_allocator_statistics totals;
	zone_count_t zone;

	memset(&totals, 0, sizeof(totals));

	for (zone = 0; zone < depot->zone_count; zone++) {
		const struct block_allocator *allocator = &depot->allocators[zone];
		const struct block_allocator_statistics *stats = &allocator->statistics;

		totals.slab_count += allocator->slab_count;
		totals.slabs_opened += READ_ONCE(stats->slabs_opened);
		totals.slabs_reopened += READ_ONCE(stats->slabs_reopened);
	}

	return totals;
}

/**
 * get_ref_counts_statistics() - Get the cumulative ref_counts statistics for the depot.
 * @depot: The slab depot.
 *
 * Return: The cumulative statistics for all ref_counts in the depot.
 */
static struct ref_counts_statistics __must_check
get_ref_counts_statistics(const struct slab_depot *depot)
{
	struct ref_counts_statistics totals;
	zone_count_t zone;

	memset(&totals, 0, sizeof(totals));

	for (zone = 0; zone < depot->zone_count; zone++) {
		totals.blocks_written +=
			READ_ONCE(depot->allocators[zone].ref_counts_statistics.blocks_written);
	}

	return totals;
}

/**
 * get_slab_journal_statistics() - Get the aggregated slab journal statistics for the depot.
 * @depot: The slab depot.
 *
 * Return: The aggregated statistics for all slab journals in the depot.
 */
static struct slab_journal_statistics __must_check
get_slab_journal_statistics(const struct slab_depot *depot)
{
	struct slab_journal_statistics totals;
	zone_count_t zone;

	memset(&totals, 0, sizeof(totals));

	for (zone = 0; zone < depot->zone_count; zone++) {
		const struct slab_journal_statistics *stats =
			&depot->allocators[zone].slab_journal_statistics;

		totals.disk_full_count += READ_ONCE(stats->disk_full_count);
		totals.flush_count += READ_ONCE(stats->flush_count);
		totals.blocked_count += READ_ONCE(stats->blocked_count);
		totals.blocks_written += READ_ONCE(stats->blocks_written);
		totals.tail_busy_count += READ_ONCE(stats->tail_busy_count);
	}

	return totals;
}

/**
 * vdo_get_slab_depot_statistics() - Get all the vdo_statistics fields that are properties of the
 *                                   slab depot.
 * @depot: The slab depot.
 * @stats: The vdo statistics structure to partially fill.
 */
void vdo_get_slab_depot_statistics(const struct slab_depot *depot,
				   struct vdo_statistics *stats)
{
	slab_count_t slab_count = READ_ONCE(depot->slab_count);
	slab_count_t unrecovered = 0;
	zone_count_t zone;

	for (zone = 0; zone < depot->zone_count; zone++) {
		/* The allocators are responsible for thread safety. */
		unrecovered += READ_ONCE(depot->allocators[zone].scrubber.slab_count);
	}

	stats->recovery_percentage = (slab_count - unrecovered) * 100 / slab_count;
	stats->allocator = get_block_allocator_statistics(depot);
	stats->ref_counts = get_ref_counts_statistics(depot);
	stats->slab_journal = get_slab_journal_statistics(depot);
	stats->slab_summary = (struct slab_summary_statistics) {
		.blocks_written = atomic64_read(&depot->summary_statistics.blocks_written),
	};
}

/**
 * vdo_dump_slab_depot() - Dump the slab depot, in a thread-unsafe fashion.
 * @depot: The slab depot.
 */
void vdo_dump_slab_depot(const struct slab_depot *depot)
{
	vdo_log_info("vdo slab depot");
	vdo_log_info("  zone_count=%u old_zone_count=%u slabCount=%u active_release_request=%llu new_release_request=%llu",
		     (unsigned int) depot->zone_count,
		     (unsigned int) depot->old_zone_count, READ_ONCE(depot->slab_count),
		     (unsigned long long) depot->active_release_request,
		     (unsigned long long) depot->new_release_request);
}
