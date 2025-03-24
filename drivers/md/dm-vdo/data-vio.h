/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef DATA_VIO_H
#define DATA_VIO_H

#include <linux/atomic.h>
#include <linux/bio.h>
#include <linux/list.h>

#include "permassert.h"

#include "indexer.h"

#include "block-map.h"
#include "completion.h"
#include "constants.h"
#include "dedupe.h"
#include "encodings.h"
#include "logical-zone.h"
#include "physical-zone.h"
#include "types.h"
#include "vdo.h"
#include "vio.h"
#include "wait-queue.h"

/* Codes for describing the last asynchronous operation performed on a vio. */
enum async_operation_number {
	MIN_VIO_ASYNC_OPERATION_NUMBER,
	VIO_ASYNC_OP_LAUNCH = MIN_VIO_ASYNC_OPERATION_NUMBER,
	VIO_ASYNC_OP_ACKNOWLEDGE_WRITE,
	VIO_ASYNC_OP_ACQUIRE_VDO_HASH_LOCK,
	VIO_ASYNC_OP_ATTEMPT_LOGICAL_BLOCK_LOCK,
	VIO_ASYNC_OP_LOCK_DUPLICATE_PBN,
	VIO_ASYNC_OP_CHECK_FOR_DUPLICATION,
	VIO_ASYNC_OP_CLEANUP,
	VIO_ASYNC_OP_COMPRESS_DATA_VIO,
	VIO_ASYNC_OP_FIND_BLOCK_MAP_SLOT,
	VIO_ASYNC_OP_GET_MAPPED_BLOCK_FOR_READ,
	VIO_ASYNC_OP_GET_MAPPED_BLOCK_FOR_WRITE,
	VIO_ASYNC_OP_HASH_DATA_VIO,
	VIO_ASYNC_OP_JOURNAL_REMAPPING,
	VIO_ASYNC_OP_ATTEMPT_PACKING,
	VIO_ASYNC_OP_PUT_MAPPED_BLOCK,
	VIO_ASYNC_OP_READ_DATA_VIO,
	VIO_ASYNC_OP_UPDATE_DEDUPE_INDEX,
	VIO_ASYNC_OP_UPDATE_REFERENCE_COUNTS,
	VIO_ASYNC_OP_VERIFY_DUPLICATION,
	VIO_ASYNC_OP_WRITE_DATA_VIO,
	MAX_VIO_ASYNC_OPERATION_NUMBER,
} __packed;

struct lbn_lock {
	logical_block_number_t lbn;
	bool locked;
	struct vdo_wait_queue waiters;
	struct logical_zone *zone;
};

/* A position in the arboreal block map at a specific level. */
struct block_map_tree_slot {
	page_number_t page_index;
	struct block_map_slot block_map_slot;
};

/* Fields for using the arboreal block map. */
struct tree_lock {
	/* The current height at which this data_vio is operating */
	height_t height;
	/* The block map tree for this LBN */
	root_count_t root_index;
	/* Whether we hold a page lock */
	bool locked;
	/* The key for the lock map */
	u64 key;
	/* The queue of waiters for the page this vio is allocating or loading */
	struct vdo_wait_queue waiters;
	/* The block map tree slots for this LBN */
	struct block_map_tree_slot tree_slots[VDO_BLOCK_MAP_TREE_HEIGHT + 1];
};

struct zoned_pbn {
	physical_block_number_t pbn;
	enum block_mapping_state state;
	struct physical_zone *zone;
};

/*
 * Where a data_vio is on the compression path; advance_compression_stage() depends on the order of
 * this enum.
 */
enum data_vio_compression_stage {
	/* A data_vio which has not yet entered the compression path */
	DATA_VIO_PRE_COMPRESSOR,
	/* A data_vio which is in the compressor */
	DATA_VIO_COMPRESSING,
	/* A data_vio which is blocked in the packer */
	DATA_VIO_PACKING,
	/* A data_vio which is no longer on the compression path (and never will be) */
	DATA_VIO_POST_PACKER,
};

struct data_vio_compression_status {
	enum data_vio_compression_stage stage;
	bool may_not_compress;
};

struct compression_state {
	/*
	 * The current compression status of this data_vio. This field contains a value which
	 * consists of a data_vio_compression_stage and a flag indicating whether a request has
	 * been made to cancel (or prevent) compression for this data_vio.
	 *
	 * This field should be accessed through the get_data_vio_compression_status() and
	 * set_data_vio_compression_status() methods. It should not be accessed directly.
	 */
	atomic_t status;

	/* The compressed size of this block */
	u16 size;

	/* The packer input or output bin slot which holds the enclosing data_vio */
	slot_number_t slot;

	/* The packer bin to which the enclosing data_vio has been assigned */
	struct packer_bin *bin;

	/* A link in the chain of data_vios which have been packed together */
	struct data_vio *next_in_batch;

	/* A vio which is blocked in the packer while holding a lock this vio needs. */
	struct data_vio *lock_holder;

	/*
	 * The compressed block used to hold the compressed form of this block and that of any
	 * other blocks for which this data_vio is the compressed write agent.
	 */
	struct compressed_block *block;
};

/* Fields supporting allocation of data blocks. */
struct allocation {
	/* The physical zone in which to allocate a physical block */
	struct physical_zone *zone;

	/* The block allocated to this vio */
	physical_block_number_t pbn;

	/*
	 * If non-NULL, the pooled PBN lock held on the allocated block. Must be a write lock until
	 * the block has been written, after which it will become a read lock.
	 */
	struct pbn_lock *lock;

	/* The type of write lock to obtain on the allocated block */
	enum pbn_lock_type write_lock_type;

	/* The zone which was the start of the current allocation cycle */
	zone_count_t first_allocation_zone;

	/* Whether this vio should wait for a clean slab */
	bool wait_for_clean_slab;
};

struct reference_updater {
	enum journal_operation operation;
	bool increment;
	struct zoned_pbn zpbn;
	struct pbn_lock *lock;
	struct vdo_waiter waiter;
};

/* A vio for processing user data requests. */
struct data_vio {
	/* The vdo_wait_queue entry structure */
	struct vdo_waiter waiter;

	/* The logical block of this request */
	struct lbn_lock logical;

	/* The state for traversing the block map tree */
	struct tree_lock tree_lock;

	/* The current partition address of this block */
	struct zoned_pbn mapped;

	/* The hash of this vio (if not zero) */
	struct uds_record_name record_name;

	/* Used for logging and debugging */
	enum async_operation_number last_async_operation;

	/* The operations to record in the recovery and slab journals */
	struct reference_updater increment_updater;
	struct reference_updater decrement_updater;

	u16 read : 1;
	u16 write : 1;
	u16 fua : 1;
	u16 is_zero : 1;
	u16 is_discard : 1;
	u16 is_partial : 1;
	u16 is_duplicate : 1;
	u16 first_reference_operation_complete : 1;
	u16 downgrade_allocation_lock : 1;

	struct allocation allocation;

	/*
	 * Whether this vio has received an allocation. This field is examined from threads not in
	 * the allocation zone.
	 */
	bool allocation_succeeded;

	/* The new partition address of this block after the vio write completes */
	struct zoned_pbn new_mapped;

	/* The hash zone responsible for the name (NULL if is_zero_block) */
	struct hash_zone *hash_zone;

	/* The lock this vio holds or shares with other vios with the same data */
	struct hash_lock *hash_lock;

	/* All data_vios sharing a hash lock are kept in a list linking these list entries */
	struct list_head hash_lock_entry;

	/* The block number in the partition of the UDS deduplication advice */
	struct zoned_pbn duplicate;

	/*
	 * The sequence number of the recovery journal block containing the increment entry for
	 * this vio.
	 */
	sequence_number_t recovery_sequence_number;

	/* The point in the recovery journal where this write last made an entry */
	struct journal_point recovery_journal_point;

	/* The list of vios in user initiated write requests */
	struct list_head write_entry;

	/* The generation number of the VDO that this vio belongs to */
	sequence_number_t flush_generation;

	/* The completion to use for fetching block map pages for this vio */
	struct vdo_page_completion page_completion;

	/* The user bio that initiated this VIO */
	struct bio *user_bio;

	/* partial block support */
	block_size_t offset;

	/*
	 * The number of bytes to be discarded. For discards, this field will always be positive,
	 * whereas for non-discards it will always be 0. Hence it can be used to determine whether
	 * a data_vio is processing a discard, even after the user_bio has been acknowledged.
	 */
	u32 remaining_discard;

	struct dedupe_context *dedupe_context;

	/* Fields beyond this point will not be reset when a pooled data_vio is reused. */

	struct vio vio;

	/* The completion for making reference count decrements */
	struct vdo_completion decrement_completion;

	/* All of the fields necessary for the compression path */
	struct compression_state compression;

	/* A block used as output during compression or uncompression */
	char *scratch_block;

	struct list_head pool_entry;
};

static inline struct data_vio *vio_as_data_vio(struct vio *vio)
{
	VDO_ASSERT_LOG_ONLY((vio->type == VIO_TYPE_DATA), "vio is a data_vio");
	return container_of(vio, struct data_vio, vio);
}

static inline struct data_vio *as_data_vio(struct vdo_completion *completion)
{
	return vio_as_data_vio(as_vio(completion));
}

static inline struct data_vio *vdo_waiter_as_data_vio(struct vdo_waiter *waiter)
{
	if (waiter == NULL)
		return NULL;

	return container_of(waiter, struct data_vio, waiter);
}

static inline struct data_vio *data_vio_from_reference_updater(struct reference_updater *updater)
{
	if (updater->increment)
		return container_of(updater, struct data_vio, increment_updater);

	return container_of(updater, struct data_vio, decrement_updater);
}

static inline bool data_vio_has_flush_generation_lock(struct data_vio *data_vio)
{
	return !list_empty(&data_vio->write_entry);
}

static inline struct vdo *vdo_from_data_vio(struct data_vio *data_vio)
{
	return data_vio->vio.completion.vdo;
}

static inline bool data_vio_has_allocation(struct data_vio *data_vio)
{
	return (data_vio->allocation.pbn != VDO_ZERO_BLOCK);
}

struct data_vio_compression_status __must_check
advance_data_vio_compression_stage(struct data_vio *data_vio);
struct data_vio_compression_status __must_check
get_data_vio_compression_status(struct data_vio *data_vio);
bool cancel_data_vio_compression(struct data_vio *data_vio);

struct data_vio_pool;

int make_data_vio_pool(struct vdo *vdo, data_vio_count_t pool_size,
		       data_vio_count_t discard_limit, struct data_vio_pool **pool_ptr);
void free_data_vio_pool(struct data_vio_pool *pool);
void vdo_launch_bio(struct data_vio_pool *pool, struct bio *bio);
void drain_data_vio_pool(struct data_vio_pool *pool, struct vdo_completion *completion);
void resume_data_vio_pool(struct data_vio_pool *pool, struct vdo_completion *completion);

void dump_data_vio_pool(struct data_vio_pool *pool, bool dump_vios);
data_vio_count_t get_data_vio_pool_active_requests(struct data_vio_pool *pool);
data_vio_count_t get_data_vio_pool_request_limit(struct data_vio_pool *pool);
data_vio_count_t get_data_vio_pool_maximum_requests(struct data_vio_pool *pool);

void complete_data_vio(struct vdo_completion *completion);
void handle_data_vio_error(struct vdo_completion *completion);

static inline void continue_data_vio(struct data_vio *data_vio)
{
	vdo_launch_completion(&data_vio->vio.completion);
}

/**
 * continue_data_vio_with_error() - Set an error code and then continue processing a data_vio.
 *
 * This will not mask older errors. This function can be called with a success code, but it is more
 * efficient to call continue_data_vio() if the caller knows the result was a success.
 */
static inline void continue_data_vio_with_error(struct data_vio *data_vio, int result)
{
	vdo_continue_completion(&data_vio->vio.completion, result);
}

const char * __must_check get_data_vio_operation_name(struct data_vio *data_vio);

static inline void assert_data_vio_in_hash_zone(struct data_vio *data_vio)
{
	thread_id_t expected = data_vio->hash_zone->thread_id;
	thread_id_t thread_id = vdo_get_callback_thread_id();
	/*
	 * It's odd to use the LBN, but converting the record name to hex is a bit clunky for an
	 * inline, and the LBN better than nothing as an identifier.
	 */
	VDO_ASSERT_LOG_ONLY((expected == thread_id),
			    "data_vio for logical block %llu on thread %u, should be on hash zone thread %u",
			    (unsigned long long) data_vio->logical.lbn, thread_id, expected);
}

static inline void set_data_vio_hash_zone_callback(struct data_vio *data_vio,
						   vdo_action_fn callback)
{
	vdo_set_completion_callback(&data_vio->vio.completion, callback,
				    data_vio->hash_zone->thread_id);
}

/**
 * launch_data_vio_hash_zone_callback() - Set a callback as a hash zone operation and invoke it
 *					  immediately.
 */
static inline void launch_data_vio_hash_zone_callback(struct data_vio *data_vio,
						      vdo_action_fn callback)
{
	set_data_vio_hash_zone_callback(data_vio, callback);
	vdo_launch_completion(&data_vio->vio.completion);
}

static inline void assert_data_vio_in_logical_zone(struct data_vio *data_vio)
{
	thread_id_t expected = data_vio->logical.zone->thread_id;
	thread_id_t thread_id = vdo_get_callback_thread_id();

	VDO_ASSERT_LOG_ONLY((expected == thread_id),
			    "data_vio for logical block %llu on thread %u, should be on thread %u",
			    (unsigned long long) data_vio->logical.lbn, thread_id, expected);
}

static inline void set_data_vio_logical_callback(struct data_vio *data_vio,
						 vdo_action_fn callback)
{
	vdo_set_completion_callback(&data_vio->vio.completion, callback,
				    data_vio->logical.zone->thread_id);
}

/**
 * launch_data_vio_logical_callback() - Set a callback as a logical block operation and invoke it
 *					immediately.
 */
static inline void launch_data_vio_logical_callback(struct data_vio *data_vio,
						    vdo_action_fn callback)
{
	set_data_vio_logical_callback(data_vio, callback);
	vdo_launch_completion(&data_vio->vio.completion);
}

static inline void assert_data_vio_in_allocated_zone(struct data_vio *data_vio)
{
	thread_id_t expected = data_vio->allocation.zone->thread_id;
	thread_id_t thread_id = vdo_get_callback_thread_id();

	VDO_ASSERT_LOG_ONLY((expected == thread_id),
			    "struct data_vio for allocated physical block %llu on thread %u, should be on thread %u",
			    (unsigned long long) data_vio->allocation.pbn, thread_id,
			    expected);
}

static inline void set_data_vio_allocated_zone_callback(struct data_vio *data_vio,
							vdo_action_fn callback)
{
	vdo_set_completion_callback(&data_vio->vio.completion, callback,
				    data_vio->allocation.zone->thread_id);
}

/**
 * launch_data_vio_allocated_zone_callback() - Set a callback as a physical block operation in a
 *					       data_vio's allocated zone and queue the data_vio and
 *					       invoke it immediately.
 */
static inline void launch_data_vio_allocated_zone_callback(struct data_vio *data_vio,
							   vdo_action_fn callback)
{
	set_data_vio_allocated_zone_callback(data_vio, callback);
	vdo_launch_completion(&data_vio->vio.completion);
}

static inline void assert_data_vio_in_duplicate_zone(struct data_vio *data_vio)
{
	thread_id_t expected = data_vio->duplicate.zone->thread_id;
	thread_id_t thread_id = vdo_get_callback_thread_id();

	VDO_ASSERT_LOG_ONLY((expected == thread_id),
			    "data_vio for duplicate physical block %llu on thread %u, should be on thread %u",
			    (unsigned long long) data_vio->duplicate.pbn, thread_id,
			    expected);
}

static inline void set_data_vio_duplicate_zone_callback(struct data_vio *data_vio,
							vdo_action_fn callback)
{
	vdo_set_completion_callback(&data_vio->vio.completion, callback,
				    data_vio->duplicate.zone->thread_id);
}

/**
 * launch_data_vio_duplicate_zone_callback() - Set a callback as a physical block operation in a
 *					       data_vio's duplicate zone and queue the data_vio and
 *					       invoke it immediately.
 */
static inline void launch_data_vio_duplicate_zone_callback(struct data_vio *data_vio,
							   vdo_action_fn callback)
{
	set_data_vio_duplicate_zone_callback(data_vio, callback);
	vdo_launch_completion(&data_vio->vio.completion);
}

static inline void assert_data_vio_in_mapped_zone(struct data_vio *data_vio)
{
	thread_id_t expected = data_vio->mapped.zone->thread_id;
	thread_id_t thread_id = vdo_get_callback_thread_id();

	VDO_ASSERT_LOG_ONLY((expected == thread_id),
			    "data_vio for mapped physical block %llu on thread %u, should be on thread %u",
			    (unsigned long long) data_vio->mapped.pbn, thread_id, expected);
}

static inline void set_data_vio_mapped_zone_callback(struct data_vio *data_vio,
						     vdo_action_fn callback)
{
	vdo_set_completion_callback(&data_vio->vio.completion, callback,
				    data_vio->mapped.zone->thread_id);
}

static inline void assert_data_vio_in_new_mapped_zone(struct data_vio *data_vio)
{
	thread_id_t expected = data_vio->new_mapped.zone->thread_id;
	thread_id_t thread_id = vdo_get_callback_thread_id();

	VDO_ASSERT_LOG_ONLY((expected == thread_id),
			    "data_vio for new_mapped physical block %llu on thread %u, should be on thread %u",
			    (unsigned long long) data_vio->new_mapped.pbn, thread_id,
			    expected);
}

static inline void set_data_vio_new_mapped_zone_callback(struct data_vio *data_vio,
							 vdo_action_fn callback)
{
	vdo_set_completion_callback(&data_vio->vio.completion, callback,
				    data_vio->new_mapped.zone->thread_id);
}

static inline void assert_data_vio_in_journal_zone(struct data_vio *data_vio)
{
	thread_id_t journal_thread = vdo_from_data_vio(data_vio)->thread_config.journal_thread;
	thread_id_t thread_id = vdo_get_callback_thread_id();

	VDO_ASSERT_LOG_ONLY((journal_thread == thread_id),
			    "data_vio for logical block %llu on thread %u, should be on journal thread %u",
			    (unsigned long long) data_vio->logical.lbn, thread_id,
			    journal_thread);
}

static inline void set_data_vio_journal_callback(struct data_vio *data_vio,
						 vdo_action_fn callback)
{
	thread_id_t journal_thread = vdo_from_data_vio(data_vio)->thread_config.journal_thread;

	vdo_set_completion_callback(&data_vio->vio.completion, callback, journal_thread);
}

/**
 * launch_data_vio_journal_callback() - Set a callback as a journal operation and invoke it
 *					immediately.
 */
static inline void launch_data_vio_journal_callback(struct data_vio *data_vio,
						    vdo_action_fn callback)
{
	set_data_vio_journal_callback(data_vio, callback);
	vdo_launch_completion(&data_vio->vio.completion);
}

static inline void assert_data_vio_in_packer_zone(struct data_vio *data_vio)
{
	thread_id_t packer_thread = vdo_from_data_vio(data_vio)->thread_config.packer_thread;
	thread_id_t thread_id = vdo_get_callback_thread_id();

	VDO_ASSERT_LOG_ONLY((packer_thread == thread_id),
			    "data_vio for logical block %llu on thread %u, should be on packer thread %u",
			    (unsigned long long) data_vio->logical.lbn, thread_id,
			    packer_thread);
}

static inline void set_data_vio_packer_callback(struct data_vio *data_vio,
						vdo_action_fn callback)
{
	thread_id_t packer_thread = vdo_from_data_vio(data_vio)->thread_config.packer_thread;

	vdo_set_completion_callback(&data_vio->vio.completion, callback, packer_thread);
}

/**
 * launch_data_vio_packer_callback() - Set a callback as a packer operation and invoke it
 *				       immediately.
 */
static inline void launch_data_vio_packer_callback(struct data_vio *data_vio,
						   vdo_action_fn callback)
{
	set_data_vio_packer_callback(data_vio, callback);
	vdo_launch_completion(&data_vio->vio.completion);
}

static inline void assert_data_vio_on_cpu_thread(struct data_vio *data_vio)
{
	thread_id_t cpu_thread = vdo_from_data_vio(data_vio)->thread_config.cpu_thread;
	thread_id_t thread_id = vdo_get_callback_thread_id();

	VDO_ASSERT_LOG_ONLY((cpu_thread == thread_id),
			    "data_vio for logical block %llu on thread %u, should be on cpu thread %u",
			    (unsigned long long) data_vio->logical.lbn, thread_id,
			    cpu_thread);
}

static inline void set_data_vio_cpu_callback(struct data_vio *data_vio,
					     vdo_action_fn callback)
{
	thread_id_t cpu_thread = vdo_from_data_vio(data_vio)->thread_config.cpu_thread;

	vdo_set_completion_callback(&data_vio->vio.completion, callback, cpu_thread);
}

/**
 * launch_data_vio_cpu_callback() - Set a callback to run on the CPU queues and invoke it
 *				    immediately.
 */
static inline void launch_data_vio_cpu_callback(struct data_vio *data_vio,
						vdo_action_fn callback,
						enum vdo_completion_priority priority)
{
	set_data_vio_cpu_callback(data_vio, callback);
	vdo_launch_completion_with_priority(&data_vio->vio.completion, priority);
}

static inline void set_data_vio_bio_zone_callback(struct data_vio *data_vio,
						  vdo_action_fn callback)
{
	vdo_set_completion_callback(&data_vio->vio.completion, callback,
				    get_vio_bio_zone_thread_id(&data_vio->vio));
}

/**
 * launch_data_vio_bio_zone_callback() - Set a callback as a bio zone operation and invoke it
 *					 immediately.
 */
static inline void launch_data_vio_bio_zone_callback(struct data_vio *data_vio,
						     vdo_action_fn callback)
{
	set_data_vio_bio_zone_callback(data_vio, callback);
	vdo_launch_completion_with_priority(&data_vio->vio.completion,
					    BIO_Q_DATA_PRIORITY);
}

/**
 * launch_data_vio_on_bio_ack_queue() - If the vdo uses a bio_ack queue, set a callback to run on
 *					it and invoke it immediately, otherwise, just run the
 *					callback on the current thread.
 */
static inline void launch_data_vio_on_bio_ack_queue(struct data_vio *data_vio,
						    vdo_action_fn callback)
{
	struct vdo_completion *completion = &data_vio->vio.completion;
	struct vdo *vdo = completion->vdo;

	if (!vdo_uses_bio_ack_queue(vdo)) {
		callback(completion);
		return;
	}

	vdo_set_completion_callback(completion, callback,
				    vdo->thread_config.bio_ack_thread);
	vdo_launch_completion_with_priority(completion, BIO_ACK_Q_ACK_PRIORITY);
}

void data_vio_allocate_data_block(struct data_vio *data_vio,
				  enum pbn_lock_type write_lock_type,
				  vdo_action_fn callback, vdo_action_fn error_handler);

void release_data_vio_allocation_lock(struct data_vio *data_vio, bool reset);

int __must_check uncompress_data_vio(struct data_vio *data_vio,
				     enum block_mapping_state mapping_state,
				     char *buffer);

void update_metadata_for_data_vio_write(struct data_vio *data_vio,
					struct pbn_lock *lock);
void write_data_vio(struct data_vio *data_vio);
void launch_compress_data_vio(struct data_vio *data_vio);
void continue_data_vio_with_block_map_slot(struct vdo_completion *completion);

#endif /* DATA_VIO_H */
