// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "packer.h"

#include <linux/atomic.h>
#include <linux/blkdev.h>

#include "logger.h"
#include "memory-alloc.h"
#include "permassert.h"
#include "string-utils.h"

#include "admin-state.h"
#include "completion.h"
#include "constants.h"
#include "data-vio.h"
#include "dedupe.h"
#include "encodings.h"
#include "io-submitter.h"
#include "physical-zone.h"
#include "status-codes.h"
#include "vdo.h"
#include "vio.h"

static const struct version_number COMPRESSED_BLOCK_1_0 = {
	.major_version = 1,
	.minor_version = 0,
};

#define COMPRESSED_BLOCK_1_0_SIZE (4 + 4 + (2 * VDO_MAX_COMPRESSION_SLOTS))

/**
 * vdo_get_compressed_block_fragment() - Get a reference to a compressed fragment from a compressed
 *                                       block.
 * @mapping_state [in] The mapping state for the look up.
 * @compressed_block [in] The compressed block that was read from disk.
 * @fragment_offset [out] The offset of the fragment within a compressed block.
 * @fragment_size [out] The size of the fragment.
 *
 * Return: If a valid compressed fragment is found, VDO_SUCCESS; otherwise, VDO_INVALID_FRAGMENT if
 *         the fragment is invalid.
 */
int vdo_get_compressed_block_fragment(enum block_mapping_state mapping_state,
				      struct compressed_block *block,
				      u16 *fragment_offset, u16 *fragment_size)
{
	u16 compressed_size;
	u16 offset = 0;
	unsigned int i;
	u8 slot;
	struct version_number version;

	if (!vdo_is_state_compressed(mapping_state))
		return VDO_INVALID_FRAGMENT;

	version = vdo_unpack_version_number(block->header.version);
	if (!vdo_are_same_version(version, COMPRESSED_BLOCK_1_0))
		return VDO_INVALID_FRAGMENT;

	slot = mapping_state - VDO_MAPPING_STATE_COMPRESSED_BASE;
	if (slot >= VDO_MAX_COMPRESSION_SLOTS)
		return VDO_INVALID_FRAGMENT;

	compressed_size = __le16_to_cpu(block->header.sizes[slot]);
	for (i = 0; i < slot; i++) {
		offset += __le16_to_cpu(block->header.sizes[i]);
		if (offset >= VDO_COMPRESSED_BLOCK_DATA_SIZE)
			return VDO_INVALID_FRAGMENT;
	}

	if ((offset + compressed_size) > VDO_COMPRESSED_BLOCK_DATA_SIZE)
		return VDO_INVALID_FRAGMENT;

	*fragment_offset = offset;
	*fragment_size = compressed_size;
	return VDO_SUCCESS;
}

/**
 * assert_on_packer_thread() - Check that we are on the packer thread.
 * @packer: The packer.
 * @caller: The function which is asserting.
 */
static inline void assert_on_packer_thread(struct packer *packer, const char *caller)
{
	VDO_ASSERT_LOG_ONLY((vdo_get_callback_thread_id() == packer->thread_id),
			    "%s() called from packer thread", caller);
}

/**
 * insert_in_sorted_list() - Insert a bin to the list.
 * @packer: The packer.
 * @bin: The bin to move to its sorted position.
 *
 * The list is in ascending order of free space. Since all bins are already in the list, this
 * actually moves the bin to the correct position in the list.
 */
static void insert_in_sorted_list(struct packer *packer, struct packer_bin *bin)
{
	struct packer_bin *active_bin;

	list_for_each_entry(active_bin, &packer->bins, list)
		if (active_bin->free_space > bin->free_space) {
			list_move_tail(&bin->list, &active_bin->list);
			return;
		}

	list_move_tail(&bin->list, &packer->bins);
}

/**
 * make_bin() - Allocate a bin and put it into the packer's list.
 * @packer: The packer.
 */
static int __must_check make_bin(struct packer *packer)
{
	struct packer_bin *bin;
	int result;

	result = vdo_allocate_extended(struct packer_bin, VDO_MAX_COMPRESSION_SLOTS,
				       struct vio *, __func__, &bin);
	if (result != VDO_SUCCESS)
		return result;

	bin->free_space = VDO_COMPRESSED_BLOCK_DATA_SIZE;
	INIT_LIST_HEAD(&bin->list);
	list_add_tail(&bin->list, &packer->bins);
	return VDO_SUCCESS;
}

/**
 * vdo_make_packer() - Make a new block packer.
 *
 * @vdo: The vdo to which this packer belongs.
 * @bin_count: The number of partial bins to keep in memory.
 * @packer_ptr: A pointer to hold the new packer.
 *
 * Return: VDO_SUCCESS or an error
 */
int vdo_make_packer(struct vdo *vdo, block_count_t bin_count, struct packer **packer_ptr)
{
	struct packer *packer;
	block_count_t i;
	int result;

	result = vdo_allocate(1, struct packer, __func__, &packer);
	if (result != VDO_SUCCESS)
		return result;

	packer->thread_id = vdo->thread_config.packer_thread;
	packer->size = bin_count;
	INIT_LIST_HEAD(&packer->bins);
	vdo_set_admin_state_code(&packer->state, VDO_ADMIN_STATE_NORMAL_OPERATION);

	for (i = 0; i < bin_count; i++) {
		result = make_bin(packer);
		if (result != VDO_SUCCESS) {
			vdo_free_packer(packer);
			return result;
		}
	}

	/*
	 * The canceled bin can hold up to half the number of user vios. Every canceled vio in the
	 * bin must have a canceler for which it is waiting, and any canceler will only have
	 * canceled one lock holder at a time.
	 */
	result = vdo_allocate_extended(struct packer_bin, MAXIMUM_VDO_USER_VIOS / 2,
				       struct vio *, __func__, &packer->canceled_bin);
	if (result != VDO_SUCCESS) {
		vdo_free_packer(packer);
		return result;
	}

	result = vdo_make_default_thread(vdo, packer->thread_id);
	if (result != VDO_SUCCESS) {
		vdo_free_packer(packer);
		return result;
	}

	*packer_ptr = packer;
	return VDO_SUCCESS;
}

/**
 * vdo_free_packer() - Free a block packer.
 * @packer: The packer to free.
 */
void vdo_free_packer(struct packer *packer)
{
	struct packer_bin *bin, *tmp;

	if (packer == NULL)
		return;

	list_for_each_entry_safe(bin, tmp, &packer->bins, list) {
		list_del_init(&bin->list);
		vdo_free(bin);
	}

	vdo_free(vdo_forget(packer->canceled_bin));
	vdo_free(packer);
}

/**
 * get_packer_from_data_vio() - Get the packer from a data_vio.
 * @data_vio: The data_vio.
 *
 * Return: The packer from the VDO to which the data_vio belongs.
 */
static inline struct packer *get_packer_from_data_vio(struct data_vio *data_vio)
{
	return vdo_from_data_vio(data_vio)->packer;
}

/**
 * vdo_get_packer_statistics() - Get the current statistics from the packer.
 * @packer: The packer to query.
 *
 * Return: a copy of the current statistics for the packer.
 */
struct packer_statistics vdo_get_packer_statistics(const struct packer *packer)
{
	const struct packer_statistics *stats = &packer->statistics;

	return (struct packer_statistics) {
		.compressed_fragments_written = READ_ONCE(stats->compressed_fragments_written),
		.compressed_blocks_written = READ_ONCE(stats->compressed_blocks_written),
		.compressed_fragments_in_packer = READ_ONCE(stats->compressed_fragments_in_packer),
	};
}

/**
 * abort_packing() - Abort packing a data_vio.
 * @data_vio: The data_vio to abort.
 */
static void abort_packing(struct data_vio *data_vio)
{
	struct packer *packer = get_packer_from_data_vio(data_vio);

	WRITE_ONCE(packer->statistics.compressed_fragments_in_packer,
		   packer->statistics.compressed_fragments_in_packer - 1);

	write_data_vio(data_vio);
}

/**
 * release_compressed_write_waiter() - Update a data_vio for which a successful compressed write
 *                                     has completed and send it on its way.
 * @data_vio: The data_vio to release.
 * @allocation: The allocation to which the compressed block was written.
 */
static void release_compressed_write_waiter(struct data_vio *data_vio,
					    struct allocation *allocation)
{
	data_vio->new_mapped = (struct zoned_pbn) {
		.pbn = allocation->pbn,
		.zone = allocation->zone,
		.state = data_vio->compression.slot + VDO_MAPPING_STATE_COMPRESSED_BASE,
	};

	vdo_share_compressed_write_lock(data_vio, allocation->lock);
	update_metadata_for_data_vio_write(data_vio, allocation->lock);
}

/**
 * finish_compressed_write() - Finish a compressed block write.
 * @completion: The compressed write completion.
 *
 * This callback is registered in continue_after_allocation().
 */
static void finish_compressed_write(struct vdo_completion *completion)
{
	struct data_vio *agent = as_data_vio(completion);
	struct data_vio *client, *next;

	assert_data_vio_in_allocated_zone(agent);

	/*
	 * Process all the non-agent waiters first to ensure that the pbn lock can not be released
	 * until all of them have had a chance to journal their increfs.
	 */
	for (client = agent->compression.next_in_batch; client != NULL; client = next) {
		next = client->compression.next_in_batch;
		release_compressed_write_waiter(client, &agent->allocation);
	}

	completion->error_handler = handle_data_vio_error;
	release_compressed_write_waiter(agent, &agent->allocation);
}

static void handle_compressed_write_error(struct vdo_completion *completion)
{
	struct data_vio *agent = as_data_vio(completion);
	struct allocation *allocation = &agent->allocation;
	struct data_vio *client, *next;

	if (vdo_requeue_completion_if_needed(completion, allocation->zone->thread_id))
		return;

	update_vio_error_stats(as_vio(completion),
			       "Completing compressed write vio for physical block %llu with error",
			       (unsigned long long) allocation->pbn);

	for (client = agent->compression.next_in_batch; client != NULL; client = next) {
		next = client->compression.next_in_batch;
		write_data_vio(client);
	}

	/* Now that we've released the batch from the packer, forget the error and continue on. */
	vdo_reset_completion(completion);
	completion->error_handler = handle_data_vio_error;
	write_data_vio(agent);
}

/**
 * add_to_bin() - Put a data_vio in a specific packer_bin in which it will definitely fit.
 * @bin: The bin in which to put the data_vio.
 * @data_vio: The data_vio to add.
 */
static void add_to_bin(struct packer_bin *bin, struct data_vio *data_vio)
{
	data_vio->compression.bin = bin;
	data_vio->compression.slot = bin->slots_used;
	bin->incoming[bin->slots_used++] = data_vio;
}

/**
 * remove_from_bin() - Get the next data_vio whose compression has not been canceled from a bin.
 * @packer: The packer.
 * @bin: The bin from which to get a data_vio.
 *
 * Any canceled data_vios will be moved to the canceled bin.
 * Return: An uncanceled data_vio from the bin or NULL if there are none.
 */
static struct data_vio *remove_from_bin(struct packer *packer, struct packer_bin *bin)
{
	while (bin->slots_used > 0) {
		struct data_vio *data_vio = bin->incoming[--bin->slots_used];

		if (!advance_data_vio_compression_stage(data_vio).may_not_compress) {
			data_vio->compression.bin = NULL;
			return data_vio;
		}

		add_to_bin(packer->canceled_bin, data_vio);
	}

	/* The bin is now empty. */
	bin->free_space = VDO_COMPRESSED_BLOCK_DATA_SIZE;
	return NULL;
}

/**
 * initialize_compressed_block() - Initialize a compressed block.
 * @block: The compressed block to initialize.
 * @size: The size of the agent's fragment.
 *
 * This method initializes the compressed block in the compressed write agent. Because the
 * compressor already put the agent's compressed fragment at the start of the compressed block's
 * data field, it needn't be copied. So all we need do is initialize the header and set the size of
 * the agent's fragment.
 */
static void initialize_compressed_block(struct compressed_block *block, u16 size)
{
	/*
	 * Make sure the block layout isn't accidentally changed by changing the length of the
	 * block header.
	 */
	BUILD_BUG_ON(sizeof(struct compressed_block_header) != COMPRESSED_BLOCK_1_0_SIZE);

	block->header.version = vdo_pack_version_number(COMPRESSED_BLOCK_1_0);
	block->header.sizes[0] = __cpu_to_le16(size);
}

/**
 * pack_fragment() - Pack a data_vio's fragment into the compressed block in which it is already
 *                   known to fit.
 * @compression: The agent's compression_state to pack in to.
 * @data_vio: The data_vio to pack.
 * @offset: The offset into the compressed block at which to pack the fragment.
 * @block: The compressed block which will be written out when batch is fully packed.
 *
 * Return: The new amount of space used.
 */
static block_size_t __must_check pack_fragment(struct compression_state *compression,
					       struct data_vio *data_vio,
					       block_size_t offset, slot_number_t slot,
					       struct compressed_block *block)
{
	struct compression_state *to_pack = &data_vio->compression;
	char *fragment = to_pack->block->data;

	to_pack->next_in_batch = compression->next_in_batch;
	compression->next_in_batch = data_vio;
	to_pack->slot = slot;
	block->header.sizes[slot] = __cpu_to_le16(to_pack->size);
	memcpy(&block->data[offset], fragment, to_pack->size);
	return (offset + to_pack->size);
}

/**
 * compressed_write_end_io() - The bio_end_io for a compressed block write.
 * @bio: The bio for the compressed write.
 */
static void compressed_write_end_io(struct bio *bio)
{
	struct data_vio *data_vio = vio_as_data_vio(bio->bi_private);

	vdo_count_completed_bios(bio);
	set_data_vio_allocated_zone_callback(data_vio, finish_compressed_write);
	continue_data_vio_with_error(data_vio, blk_status_to_errno(bio->bi_status));
}

/**
 * write_bin() - Write out a bin.
 * @packer: The packer.
 * @bin: The bin to write.
 */
static void write_bin(struct packer *packer, struct packer_bin *bin)
{
	int result;
	block_size_t offset;
	slot_number_t slot = 1;
	struct compression_state *compression;
	struct compressed_block *block;
	struct data_vio *agent = remove_from_bin(packer, bin);
	struct data_vio *client;
	struct packer_statistics *stats;

	if (agent == NULL)
		return;

	compression = &agent->compression;
	compression->slot = 0;
	block = compression->block;
	initialize_compressed_block(block, compression->size);
	offset = compression->size;

	while ((client = remove_from_bin(packer, bin)) != NULL)
		offset = pack_fragment(compression, client, offset, slot++, block);

	/*
	 * If the batch contains only a single vio, then we save nothing by saving the compressed
	 * form. Continue processing the single vio in the batch.
	 */
	if (slot == 1) {
		abort_packing(agent);
		return;
	}

	if (slot < VDO_MAX_COMPRESSION_SLOTS) {
		/* Clear out the sizes of the unused slots */
		memset(&block->header.sizes[slot], 0,
		       (VDO_MAX_COMPRESSION_SLOTS - slot) * sizeof(__le16));
	}

	agent->vio.completion.error_handler = handle_compressed_write_error;
	if (vdo_is_read_only(vdo_from_data_vio(agent))) {
		continue_data_vio_with_error(agent, VDO_READ_ONLY);
		return;
	}

	result = vio_reset_bio(&agent->vio, (char *) block, compressed_write_end_io,
			       REQ_OP_WRITE, agent->allocation.pbn);
	if (result != VDO_SUCCESS) {
		continue_data_vio_with_error(agent, result);
		return;
	}

	/*
	 * Once the compressed write is submitted, the fragments are no longer in the packer, so
	 * update stats now.
	 */
	stats = &packer->statistics;
	WRITE_ONCE(stats->compressed_fragments_in_packer,
		   (stats->compressed_fragments_in_packer - slot));
	WRITE_ONCE(stats->compressed_fragments_written,
		   (stats->compressed_fragments_written + slot));
	WRITE_ONCE(stats->compressed_blocks_written,
		   stats->compressed_blocks_written + 1);

	vdo_submit_data_vio(agent);
}

/**
 * add_data_vio_to_packer_bin() - Add a data_vio to a bin's incoming queue
 * @packer: The packer.
 * @bin: The bin to which to add the data_vio.
 * @data_vio: The data_vio to add to the bin's queue.
 *
 * Adds a data_vio to a bin's incoming queue, handles logical space change, and calls physical
 * space processor.
 */
static void add_data_vio_to_packer_bin(struct packer *packer, struct packer_bin *bin,
				       struct data_vio *data_vio)
{
	/* If the selected bin doesn't have room, start a new batch to make room. */
	if (bin->free_space < data_vio->compression.size)
		write_bin(packer, bin);

	add_to_bin(bin, data_vio);
	bin->free_space -= data_vio->compression.size;

	/* If we happen to exactly fill the bin, start a new batch. */
	if ((bin->slots_used == VDO_MAX_COMPRESSION_SLOTS) ||
	    (bin->free_space == 0))
		write_bin(packer, bin);

	/* Now that we've finished changing the free space, restore the sort order. */
	insert_in_sorted_list(packer, bin);
}

/**
 * select_bin() - Select the bin that should be used to pack the compressed data in a data_vio with
 *                other data_vios.
 * @packer: The packer.
 * @data_vio: The data_vio.
 */
static struct packer_bin * __must_check select_bin(struct packer *packer,
						   struct data_vio *data_vio)
{
	/*
	 * First best fit: select the bin with the least free space that has enough room for the
	 * compressed data in the data_vio.
	 */
	struct packer_bin *bin, *fullest_bin;

	list_for_each_entry(bin, &packer->bins, list) {
		if (bin->free_space >= data_vio->compression.size)
			return bin;
	}

	/*
	 * None of the bins have enough space for the data_vio. We're not allowed to create new
	 * bins, so we have to overflow one of the existing bins. It's pretty intuitive to select
	 * the fullest bin, since that "wastes" the least amount of free space in the compressed
	 * block. But if the space currently used in the fullest bin is smaller than the compressed
	 * size of the incoming block, it seems wrong to force that bin to write when giving up on
	 * compressing the incoming data_vio would likewise "waste" the least amount of free space.
	 */
	fullest_bin = list_first_entry(&packer->bins, struct packer_bin, list);
	if (data_vio->compression.size >=
	    (VDO_COMPRESSED_BLOCK_DATA_SIZE - fullest_bin->free_space))
		return NULL;

	/*
	 * The fullest bin doesn't have room, but writing it out and starting a new batch with the
	 * incoming data_vio will increase the packer's free space.
	 */
	return fullest_bin;
}

/**
 * vdo_attempt_packing() - Attempt to rewrite the data in this data_vio as part of a compressed
 *                         block.
 * @data_vio: The data_vio to pack.
 */
void vdo_attempt_packing(struct data_vio *data_vio)
{
	int result;
	struct packer_bin *bin;
	struct data_vio_compression_status status = get_data_vio_compression_status(data_vio);
	struct packer *packer = get_packer_from_data_vio(data_vio);

	assert_on_packer_thread(packer, __func__);

	result = VDO_ASSERT((status.stage == DATA_VIO_COMPRESSING),
			    "attempt to pack data_vio not ready for packing, stage: %u",
			    status.stage);
	if (result != VDO_SUCCESS)
		return;

	/*
	 * Increment whether or not this data_vio will be packed or not since abort_packing()
	 * always decrements the counter.
	 */
	WRITE_ONCE(packer->statistics.compressed_fragments_in_packer,
		   packer->statistics.compressed_fragments_in_packer + 1);

	/*
	 * If packing of this data_vio is disallowed for administrative reasons, give up before
	 * making any state changes.
	 */
	if (!vdo_is_state_normal(&packer->state) ||
	    (data_vio->flush_generation < packer->flush_generation)) {
		abort_packing(data_vio);
		return;
	}

	/*
	 * The advance_data_vio_compression_stage() check here verifies that the data_vio is
	 * allowed to be compressed (if it has already been canceled, we'll fall out here). Once
	 * the data_vio is in the DATA_VIO_PACKING state, it must be guaranteed to be put in a bin
	 * before any more requests can be processed by the packer thread. Otherwise, a canceling
	 * data_vio could attempt to remove the canceled data_vio from the packer and fail to
	 * rendezvous with it. Thus, we must call select_bin() first to ensure that we will
	 * actually add the data_vio to a bin before advancing to the DATA_VIO_PACKING stage.
	 */
	bin = select_bin(packer, data_vio);
	if ((bin == NULL) ||
	    (advance_data_vio_compression_stage(data_vio).stage != DATA_VIO_PACKING)) {
		abort_packing(data_vio);
		return;
	}

	add_data_vio_to_packer_bin(packer, bin, data_vio);
}

/**
 * check_for_drain_complete() - Check whether the packer has drained.
 * @packer: The packer.
 */
static void check_for_drain_complete(struct packer *packer)
{
	if (vdo_is_state_draining(&packer->state) && (packer->canceled_bin->slots_used == 0))
		vdo_finish_draining(&packer->state);
}

/**
 * write_all_non_empty_bins() - Write out all non-empty bins on behalf of a flush or suspend.
 * @packer: The packer being flushed.
 */
static void write_all_non_empty_bins(struct packer *packer)
{
	struct packer_bin *bin;

	list_for_each_entry(bin, &packer->bins, list)
		write_bin(packer, bin);
		/*
		 * We don't need to re-sort the bin here since this loop will make every bin have
		 * the same amount of free space, so every ordering is sorted.
		 */

	check_for_drain_complete(packer);
}

/**
 * vdo_flush_packer() - Request that the packer flush asynchronously.
 * @packer: The packer to flush.
 *
 * All bins with at least two compressed data blocks will be written out, and any solitary pending
 * VIOs will be released from the packer. While flushing is in progress, any VIOs submitted to
 * vdo_attempt_packing() will be continued immediately without attempting to pack them.
 */
void vdo_flush_packer(struct packer *packer)
{
	assert_on_packer_thread(packer, __func__);
	if (vdo_is_state_normal(&packer->state))
		write_all_non_empty_bins(packer);
}

/**
 * vdo_remove_lock_holder_from_packer() - Remove a lock holder from the packer.
 * @completion: The data_vio which needs a lock held by a data_vio in the packer. The data_vio's
 *              compression.lock_holder field will point to the data_vio to remove.
 */
void vdo_remove_lock_holder_from_packer(struct vdo_completion *completion)
{
	struct data_vio *data_vio = as_data_vio(completion);
	struct packer *packer = get_packer_from_data_vio(data_vio);
	struct data_vio *lock_holder;
	struct packer_bin *bin;
	slot_number_t slot;

	assert_data_vio_in_packer_zone(data_vio);

	lock_holder = vdo_forget(data_vio->compression.lock_holder);
	bin = lock_holder->compression.bin;
	VDO_ASSERT_LOG_ONLY((bin != NULL), "data_vio in packer has a bin");

	slot = lock_holder->compression.slot;
	bin->slots_used--;
	if (slot < bin->slots_used) {
		bin->incoming[slot] = bin->incoming[bin->slots_used];
		bin->incoming[slot]->compression.slot = slot;
	}

	lock_holder->compression.bin = NULL;
	lock_holder->compression.slot = 0;

	if (bin != packer->canceled_bin) {
		bin->free_space += lock_holder->compression.size;
		insert_in_sorted_list(packer, bin);
	}

	abort_packing(lock_holder);
	check_for_drain_complete(packer);
}

/**
 * vdo_increment_packer_flush_generation() - Increment the flush generation in the packer.
 * @packer: The packer.
 *
 * This will also cause the packer to flush so that any VIOs from previous generations will exit
 * the packer.
 */
void vdo_increment_packer_flush_generation(struct packer *packer)
{
	assert_on_packer_thread(packer, __func__);
	packer->flush_generation++;
	vdo_flush_packer(packer);
}

/**
 * initiate_drain() - Initiate a drain.
 *
 * Implements vdo_admin_initiator_fn.
 */
static void initiate_drain(struct admin_state *state)
{
	struct packer *packer = container_of(state, struct packer, state);

	write_all_non_empty_bins(packer);
}

/**
 * vdo_drain_packer() - Drain the packer by preventing any more VIOs from entering the packer and
 *                      then flushing.
 * @packer: The packer to drain.
 * @completion: The completion to finish when the packer has drained.
 */
void vdo_drain_packer(struct packer *packer, struct vdo_completion *completion)
{
	assert_on_packer_thread(packer, __func__);
	vdo_start_draining(&packer->state, VDO_ADMIN_STATE_SUSPENDING, completion,
			   initiate_drain);
}

/**
 * vdo_resume_packer() - Resume a packer which has been suspended.
 * @packer: The packer to resume.
 * @parent: The completion to finish when the packer has resumed.
 */
void vdo_resume_packer(struct packer *packer, struct vdo_completion *parent)
{
	assert_on_packer_thread(packer, __func__);
	vdo_continue_completion(parent, vdo_resume_if_quiescent(&packer->state));
}

static void dump_packer_bin(const struct packer_bin *bin, bool canceled)
{
	if (bin->slots_used == 0)
		/* Don't dump empty bins. */
		return;

	vdo_log_info("	  %sBin slots_used=%u free_space=%zu",
		     (canceled ? "Canceled" : ""), bin->slots_used, bin->free_space);

	/*
	 * FIXME: dump vios in bin->incoming? The vios should have been dumped from the vio pool.
	 * Maybe just dump their addresses so it's clear they're here?
	 */
}

/**
 * vdo_dump_packer() - Dump the packer.
 * @packer: The packer.
 *
 * Context: dumps in a thread-unsafe fashion.
 */
void vdo_dump_packer(const struct packer *packer)
{
	struct packer_bin *bin;

	vdo_log_info("packer");
	vdo_log_info("	flushGeneration=%llu state %s  packer_bin_count=%llu",
		     (unsigned long long) packer->flush_generation,
		     vdo_get_admin_state_code(&packer->state)->name,
		     (unsigned long long) packer->size);

	list_for_each_entry(bin, &packer->bins, list)
		dump_packer_bin(bin, false);

	dump_packer_bin(packer->canceled_bin, true);
}
