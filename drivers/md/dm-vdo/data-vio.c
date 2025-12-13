// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "data-vio.h"

#include <linux/atomic.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/device-mapper.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/lz4.h>
#include <linux/minmax.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/wait.h>

#include "logger.h"
#include "memory-alloc.h"
#include "murmurhash3.h"
#include "permassert.h"

#include "block-map.h"
#include "dump.h"
#include "encodings.h"
#include "int-map.h"
#include "io-submitter.h"
#include "logical-zone.h"
#include "packer.h"
#include "recovery-journal.h"
#include "slab-depot.h"
#include "status-codes.h"
#include "types.h"
#include "vdo.h"
#include "vio.h"
#include "wait-queue.h"

/**
 * DOC: Bio flags.
 *
 * For certain flags set on user bios, if the user bio has not yet been acknowledged, setting those
 * flags on our own bio(s) for that request may help underlying layers better fulfill the user
 * bio's needs. This constant contains the aggregate of those flags; VDO strips all the other
 * flags, as they convey incorrect information.
 *
 * These flags are always irrelevant if we have already finished the user bio as they are only
 * hints on IO importance. If VDO has finished the user bio, any remaining IO done doesn't care how
 * important finishing the finished bio was.
 *
 * Note that bio.c contains the complete list of flags we believe may be set; the following list
 * explains the action taken with each of those flags VDO could receive:
 *
 * * REQ_SYNC: Passed down if the user bio is not yet completed, since it indicates the user bio
 *   completion is required for further work to be done by the issuer.
 * * REQ_META: Passed down if the user bio is not yet completed, since it may mean the lower layer
 *   treats it as more urgent, similar to REQ_SYNC.
 * * REQ_PRIO: Passed down if the user bio is not yet completed, since it indicates the user bio is
 *   important.
 * * REQ_NOMERGE: Set only if the incoming bio was split; irrelevant to VDO IO.
 * * REQ_IDLE: Set if the incoming bio had more IO quickly following; VDO's IO pattern doesn't
 *   match incoming IO, so this flag is incorrect for it.
 * * REQ_FUA: Handled separately, and irrelevant to VDO IO otherwise.
 * * REQ_RAHEAD: Passed down, as, for reads, it indicates trivial importance.
 * * REQ_BACKGROUND: Not passed down, as VIOs are a limited resource and VDO needs them recycled
 *   ASAP to service heavy load, which is the only place where REQ_BACKGROUND might aid in load
 *   prioritization.
 */
static blk_opf_t PASSTHROUGH_FLAGS = (REQ_PRIO | REQ_META | REQ_SYNC | REQ_RAHEAD);

/**
 * DOC:
 *
 * The data_vio_pool maintains the pool of data_vios which a vdo uses to service incoming bios. For
 * correctness, and in order to avoid potentially expensive or blocking memory allocations during
 * normal operation, the number of concurrently active data_vios is capped. Furthermore, in order
 * to avoid starvation of reads and writes, at most 75% of the data_vios may be used for
 * discards. The data_vio_pool is responsible for enforcing these limits. Threads submitting bios
 * for which a data_vio or discard permit are not available will block until the necessary
 * resources are available. The pool is also responsible for distributing resources to blocked
 * threads and waking them. Finally, the pool attempts to batch the work of recycling data_vios by
 * performing the work of actually assigning resources to blocked threads or placing data_vios back
 * into the pool on a single cpu at a time.
 *
 * The pool contains two "limiters", one for tracking data_vios and one for tracking discard
 * permits. The limiters also provide safe cross-thread access to pool statistics without the need
 * to take the pool's lock. When a thread submits a bio to a vdo device, it will first attempt to
 * get a discard permit if it is a discard, and then to get a data_vio. If the necessary resources
 * are available, the incoming bio will be assigned to the acquired data_vio, and it will be
 * launched. However, if either of these are unavailable, the arrival time of the bio is recorded
 * in the bio's bi_private field, the bio and its submitter are both queued on the appropriate
 * limiter and the submitting thread will then put itself to sleep. (note that this mechanism will
 * break if jiffies are only 32 bits.)
 *
 * Whenever a data_vio has completed processing for the bio it was servicing, release_data_vio()
 * will be called on it. This function will add the data_vio to a funnel queue, and then check the
 * state of the pool. If the pool is not currently processing released data_vios, the pool's
 * completion will be enqueued on a cpu queue. This obviates the need for the releasing threads to
 * hold the pool's lock, and also batches release work while avoiding starvation of the cpu
 * threads.
 *
 * Whenever the pool's completion is run on a cpu thread, it calls process_release_callback() which
 * processes a batch of returned data_vios (currently at most 32) from the pool's funnel queue. For
 * each data_vio, it first checks whether that data_vio was processing a discard. If so, and there
 * is a blocked bio waiting for a discard permit, that permit is notionally transferred to the
 * eldest discard waiter, and that waiter is moved to the end of the list of discard bios waiting
 * for a data_vio. If there are no discard waiters, the discard permit is returned to the pool.
 * Next, the data_vio is assigned to the oldest blocked bio which either has a discard permit, or
 * doesn't need one and relaunched. If neither of these exist, the data_vio is returned to the
 * pool. Finally, if any waiting bios were launched, the threads which blocked trying to submit
 * them are awakened.
 */

#define DATA_VIO_RELEASE_BATCH_SIZE 128

static const unsigned int VDO_SECTORS_PER_BLOCK_MASK = VDO_SECTORS_PER_BLOCK - 1;
static const u32 COMPRESSION_STATUS_MASK = 0xff;
static const u32 MAY_NOT_COMPRESS_MASK = 0x80000000;

struct limiter;
typedef void (*assigner_fn)(struct limiter *limiter);

/* Bookkeeping structure for a single type of resource. */
struct limiter {
	/* The data_vio_pool to which this limiter belongs */
	struct data_vio_pool *pool;
	/* The maximum number of data_vios available */
	data_vio_count_t limit;
	/* The number of resources in use */
	data_vio_count_t busy;
	/* The maximum number of resources ever simultaneously in use */
	data_vio_count_t max_busy;
	/* The number of resources to release */
	data_vio_count_t release_count;
	/* The number of waiters to wake */
	data_vio_count_t wake_count;
	/* The list of waiting bios which are known to process_release_callback() */
	struct bio_list waiters;
	/* The list of waiting bios which are not yet known to process_release_callback() */
	struct bio_list new_waiters;
	/* The list of waiters which have their permits */
	struct bio_list *permitted_waiters;
	/* The function for assigning a resource to a waiter */
	assigner_fn assigner;
	/* The queue of blocked threads */
	wait_queue_head_t blocked_threads;
	/* The arrival time of the eldest waiter */
	u64 arrival;
};

/*
 * A data_vio_pool is a collection of preallocated data_vios which may be acquired from any thread,
 * and are released in batches.
 */
struct data_vio_pool {
	/* Completion for scheduling releases */
	struct vdo_completion completion;
	/* The administrative state of the pool */
	struct admin_state state;
	/* Lock protecting the pool */
	spinlock_t lock;
	/* The main limiter controlling the total data_vios in the pool. */
	struct limiter limiter;
	/* The limiter controlling data_vios for discard */
	struct limiter discard_limiter;
	/* The list of bios which have discard permits but still need a data_vio */
	struct bio_list permitted_discards;
	/* The list of available data_vios */
	struct list_head available;
	/* The queue of data_vios waiting to be returned to the pool */
	struct funnel_queue *queue;
	/* Whether the pool is processing, or scheduled to process releases */
	atomic_t processing;
	/* The data vios in the pool */
	struct data_vio data_vios[];
};

static const char * const ASYNC_OPERATION_NAMES[] = {
	"launch",
	"acknowledge_write",
	"acquire_hash_lock",
	"attempt_logical_block_lock",
	"lock_duplicate_pbn",
	"check_for_duplication",
	"cleanup",
	"compress_data_vio",
	"find_block_map_slot",
	"get_mapped_block_for_read",
	"get_mapped_block_for_write",
	"hash_data_vio",
	"journal_remapping",
	"vdo_attempt_packing",
	"put_mapped_block",
	"read_data_vio",
	"update_dedupe_index",
	"update_reference_counts",
	"verify_duplication",
	"write_data_vio",
};

/* The steps taken cleaning up a VIO, in the order they are performed. */
enum data_vio_cleanup_stage {
	VIO_CLEANUP_START,
	VIO_RELEASE_HASH_LOCK = VIO_CLEANUP_START,
	VIO_RELEASE_ALLOCATED,
	VIO_RELEASE_RECOVERY_LOCKS,
	VIO_RELEASE_LOGICAL,
	VIO_CLEANUP_DONE
};

static inline struct data_vio_pool * __must_check
as_data_vio_pool(struct vdo_completion *completion)
{
	vdo_assert_completion_type(completion, VDO_DATA_VIO_POOL_COMPLETION);
	return container_of(completion, struct data_vio_pool, completion);
}

static inline u64 get_arrival_time(struct bio *bio)
{
	return (u64) bio->bi_private;
}

/**
 * check_for_drain_complete_locked() - Check whether a data_vio_pool has no outstanding data_vios
 *				       or waiters while holding the pool's lock.
 */
static bool check_for_drain_complete_locked(struct data_vio_pool *pool)
{
	if (pool->limiter.busy > 0)
		return false;

	VDO_ASSERT_LOG_ONLY((pool->discard_limiter.busy == 0),
			    "no outstanding discard permits");

	return (bio_list_empty(&pool->limiter.new_waiters) &&
		bio_list_empty(&pool->discard_limiter.new_waiters));
}

static void initialize_lbn_lock(struct data_vio *data_vio, logical_block_number_t lbn)
{
	struct vdo *vdo = vdo_from_data_vio(data_vio);
	zone_count_t zone_number;
	struct lbn_lock *lock = &data_vio->logical;

	lock->lbn = lbn;
	lock->locked = false;
	vdo_waitq_init(&lock->waiters);
	zone_number = vdo_compute_logical_zone(data_vio);
	lock->zone = &vdo->logical_zones->zones[zone_number];
}

static void launch_locked_request(struct data_vio *data_vio)
{
	data_vio->logical.locked = true;
	if (data_vio->write) {
		struct vdo *vdo = vdo_from_data_vio(data_vio);

		if (vdo_is_read_only(vdo)) {
			continue_data_vio_with_error(data_vio, VDO_READ_ONLY);
			return;
		}
	}

	data_vio->last_async_operation = VIO_ASYNC_OP_FIND_BLOCK_MAP_SLOT;
	vdo_find_block_map_slot(data_vio);
}

static void acknowledge_data_vio(struct data_vio *data_vio)
{
	struct vdo *vdo = vdo_from_data_vio(data_vio);
	struct bio *bio = data_vio->user_bio;
	int error = vdo_status_to_errno(data_vio->vio.completion.result);

	if (bio == NULL)
		return;

	VDO_ASSERT_LOG_ONLY((data_vio->remaining_discard <=
			     (u32) (VDO_BLOCK_SIZE - data_vio->offset)),
			    "data_vio to acknowledge is not an incomplete discard");

	data_vio->user_bio = NULL;
	vdo_count_bios(&vdo->stats.bios_acknowledged, bio);
	if (data_vio->is_partial)
		vdo_count_bios(&vdo->stats.bios_acknowledged_partial, bio);

	bio->bi_status = errno_to_blk_status(error);
	bio_endio(bio);
}

static void copy_to_bio(struct bio *bio, char *data_ptr)
{
	struct bio_vec biovec;
	struct bvec_iter iter;

	bio_for_each_segment(biovec, bio, iter) {
		memcpy_to_bvec(&biovec, data_ptr);
		data_ptr += biovec.bv_len;
	}
}

struct data_vio_compression_status get_data_vio_compression_status(struct data_vio *data_vio)
{
	u32 packed = atomic_read(&data_vio->compression.status);

	/* pairs with cmpxchg in set_data_vio_compression_status */
	smp_rmb();
	return (struct data_vio_compression_status) {
		.stage = packed & COMPRESSION_STATUS_MASK,
		.may_not_compress = ((packed & MAY_NOT_COMPRESS_MASK) != 0),
	};
}

/**
 * pack_status() - Convert a data_vio_compression_status into a u32 which may be stored
 *                 atomically.
 * @status: The state to convert.
 *
 * Return: The compression state packed into a u32.
 */
static u32 __must_check pack_status(struct data_vio_compression_status status)
{
	return status.stage | (status.may_not_compress ? MAY_NOT_COMPRESS_MASK : 0);
}

/**
 * set_data_vio_compression_status() - Set the compression status of a data_vio.
 * @data_vio: The data_vio to change.
 * @status: The expected current status of the data_vio.
 * @new_status: The status to set.
 *
 * Return: true if the new status was set, false if the data_vio's compression status did not
 *         match the expected state, and so was left unchanged.
 */
static bool __must_check
set_data_vio_compression_status(struct data_vio *data_vio,
				struct data_vio_compression_status status,
				struct data_vio_compression_status new_status)
{
	u32 actual;
	u32 expected = pack_status(status);
	u32 replacement = pack_status(new_status);

	/*
	 * Extra barriers because this was original developed using a CAS operation that implicitly
	 * had them.
	 */
	smp_mb__before_atomic();
	actual = atomic_cmpxchg(&data_vio->compression.status, expected, replacement);
	/* same as before_atomic */
	smp_mb__after_atomic();
	return (expected == actual);
}

struct data_vio_compression_status advance_data_vio_compression_stage(struct data_vio *data_vio)
{
	for (;;) {
		struct data_vio_compression_status status =
			get_data_vio_compression_status(data_vio);
		struct data_vio_compression_status new_status = status;

		if (status.stage == DATA_VIO_POST_PACKER) {
			/* We're already in the last stage. */
			return status;
		}

		if (status.may_not_compress) {
			/*
			 * Compression has been dis-allowed for this VIO, so skip the rest of the
			 * path and go to the end.
			 */
			new_status.stage = DATA_VIO_POST_PACKER;
		} else {
			/* Go to the next state. */
			new_status.stage++;
		}

		if (set_data_vio_compression_status(data_vio, status, new_status))
			return new_status;

		/* Another thread changed the status out from under us so try again. */
	}
}

/**
 * cancel_data_vio_compression() - Prevent this data_vio from being compressed or packed.
 *
 * Return: true if the data_vio is in the packer and the caller was the first caller to cancel it.
 */
bool cancel_data_vio_compression(struct data_vio *data_vio)
{
	struct data_vio_compression_status status, new_status;

	for (;;) {
		status = get_data_vio_compression_status(data_vio);
		if (status.may_not_compress || (status.stage == DATA_VIO_POST_PACKER)) {
			/* This data_vio is already set up to not block in the packer. */
			break;
		}

		new_status.stage = status.stage;
		new_status.may_not_compress = true;

		if (set_data_vio_compression_status(data_vio, status, new_status))
			break;
	}

	return ((status.stage == DATA_VIO_PACKING) && !status.may_not_compress);
}

/**
 * attempt_logical_block_lock() - Attempt to acquire the lock on a logical block.
 * @completion: The data_vio for an external data request as a completion.
 *
 * This is the start of the path for all external requests. It is registered in launch_data_vio().
 */
static void attempt_logical_block_lock(struct vdo_completion *completion)
{
	struct data_vio *data_vio = as_data_vio(completion);
	struct lbn_lock *lock = &data_vio->logical;
	struct vdo *vdo = vdo_from_data_vio(data_vio);
	struct data_vio *lock_holder;
	int result;

	assert_data_vio_in_logical_zone(data_vio);

	if (data_vio->logical.lbn >= vdo->states.vdo.config.logical_blocks) {
		continue_data_vio_with_error(data_vio, VDO_OUT_OF_RANGE);
		return;
	}

	result = vdo_int_map_put(lock->zone->lbn_operations, lock->lbn,
				 data_vio, false, (void **) &lock_holder);
	if (result != VDO_SUCCESS) {
		continue_data_vio_with_error(data_vio, result);
		return;
	}

	if (lock_holder == NULL) {
		/* We got the lock */
		launch_locked_request(data_vio);
		return;
	}

	result = VDO_ASSERT(lock_holder->logical.locked, "logical block lock held");
	if (result != VDO_SUCCESS) {
		continue_data_vio_with_error(data_vio, result);
		return;
	}

	/*
	 * If the new request is a pure read request (not read-modify-write) and the lock_holder is
	 * writing and has received an allocation, service the read request immediately by copying
	 * data from the lock_holder to avoid having to flush the write out of the packer just to
	 * prevent the read from waiting indefinitely. If the lock_holder does not yet have an
	 * allocation, prevent it from blocking in the packer and wait on it. This is necessary in
	 * order to prevent returning data that may not have actually been written.
	 */
	if (!data_vio->write && READ_ONCE(lock_holder->allocation_succeeded)) {
		copy_to_bio(data_vio->user_bio, lock_holder->vio.data + data_vio->offset);
		acknowledge_data_vio(data_vio);
		complete_data_vio(completion);
		return;
	}

	data_vio->last_async_operation = VIO_ASYNC_OP_ATTEMPT_LOGICAL_BLOCK_LOCK;
	vdo_waitq_enqueue_waiter(&lock_holder->logical.waiters, &data_vio->waiter);

	/*
	 * Prevent writes and read-modify-writes from blocking indefinitely on lock holders in the
	 * packer.
	 */
	if (lock_holder->write && cancel_data_vio_compression(lock_holder)) {
		data_vio->compression.lock_holder = lock_holder;
		launch_data_vio_packer_callback(data_vio,
						vdo_remove_lock_holder_from_packer);
	}
}

/**
 * launch_data_vio() - (Re)initialize a data_vio to have a new logical block number, keeping the
 *		       same parent and other state and send it on its way.
 */
static void launch_data_vio(struct data_vio *data_vio, logical_block_number_t lbn)
{
	struct vdo_completion *completion = &data_vio->vio.completion;

	/*
	 * Clearing the tree lock must happen before initializing the LBN lock, which also adds
	 * information to the tree lock.
	 */
	memset(&data_vio->tree_lock, 0, sizeof(data_vio->tree_lock));
	initialize_lbn_lock(data_vio, lbn);
	INIT_LIST_HEAD(&data_vio->hash_lock_entry);
	INIT_LIST_HEAD(&data_vio->write_entry);

	memset(&data_vio->allocation, 0, sizeof(data_vio->allocation));

	data_vio->is_duplicate = false;

	memset(&data_vio->record_name, 0, sizeof(data_vio->record_name));
	memset(&data_vio->duplicate, 0, sizeof(data_vio->duplicate));
	vdo_reset_completion(&data_vio->decrement_completion);
	vdo_reset_completion(completion);
	completion->error_handler = handle_data_vio_error;
	set_data_vio_logical_callback(data_vio, attempt_logical_block_lock);
	vdo_enqueue_completion(completion, VDO_DEFAULT_Q_MAP_BIO_PRIORITY);
}

static void copy_from_bio(struct bio *bio, char *data_ptr)
{
	struct bio_vec biovec;
	struct bvec_iter iter;

	bio_for_each_segment(biovec, bio, iter) {
		memcpy_from_bvec(data_ptr, &biovec);
		data_ptr += biovec.bv_len;
	}
}

static void launch_bio(struct vdo *vdo, struct data_vio *data_vio, struct bio *bio)
{
	logical_block_number_t lbn;
	/*
	 * Zero out the fields which don't need to be preserved (i.e. which are not pointers to
	 * separately allocated objects).
	 */
	memset(data_vio, 0, offsetof(struct data_vio, vio));
	memset(&data_vio->compression, 0, offsetof(struct compression_state, block));

	data_vio->user_bio = bio;
	data_vio->offset = to_bytes(bio->bi_iter.bi_sector & VDO_SECTORS_PER_BLOCK_MASK);
	data_vio->is_partial = (bio->bi_iter.bi_size < VDO_BLOCK_SIZE) || (data_vio->offset != 0);

	/*
	 * Discards behave very differently than other requests when coming in from device-mapper.
	 * We have to be able to handle any size discards and various sector offsets within a
	 * block.
	 */
	if (bio_op(bio) == REQ_OP_DISCARD) {
		data_vio->remaining_discard = bio->bi_iter.bi_size;
		data_vio->write = true;
		data_vio->is_discard = true;
		if (data_vio->is_partial) {
			vdo_count_bios(&vdo->stats.bios_in_partial, bio);
			data_vio->read = true;
		}
	} else if (data_vio->is_partial) {
		vdo_count_bios(&vdo->stats.bios_in_partial, bio);
		data_vio->read = true;
		if (bio_data_dir(bio) == WRITE)
			data_vio->write = true;
	} else if (bio_data_dir(bio) == READ) {
		data_vio->read = true;
	} else {
		/*
		 * Copy the bio data to a char array so that we can continue to use the data after
		 * we acknowledge the bio.
		 */
		copy_from_bio(bio, data_vio->vio.data);
		data_vio->is_zero = mem_is_zero(data_vio->vio.data, VDO_BLOCK_SIZE);
		data_vio->write = true;
	}

	if (data_vio->user_bio->bi_opf & REQ_FUA)
		data_vio->fua = true;

	lbn = (bio->bi_iter.bi_sector - vdo->starting_sector_offset) / VDO_SECTORS_PER_BLOCK;
	launch_data_vio(data_vio, lbn);
}

static void assign_data_vio(struct limiter *limiter, struct data_vio *data_vio)
{
	struct bio *bio = bio_list_pop(limiter->permitted_waiters);

	launch_bio(limiter->pool->completion.vdo, data_vio, bio);
	limiter->wake_count++;

	bio = bio_list_peek(limiter->permitted_waiters);
	limiter->arrival = ((bio == NULL) ? U64_MAX : get_arrival_time(bio));
}

static void assign_discard_permit(struct limiter *limiter)
{
	struct bio *bio = bio_list_pop(&limiter->waiters);

	if (limiter->arrival == U64_MAX)
		limiter->arrival = get_arrival_time(bio);

	bio_list_add(limiter->permitted_waiters, bio);
}

static void get_waiters(struct limiter *limiter)
{
	bio_list_merge_init(&limiter->waiters, &limiter->new_waiters);
}

static inline struct data_vio *get_available_data_vio(struct data_vio_pool *pool)
{
	struct data_vio *data_vio =
		list_first_entry(&pool->available, struct data_vio, pool_entry);

	list_del_init(&data_vio->pool_entry);
	return data_vio;
}

static void assign_data_vio_to_waiter(struct limiter *limiter)
{
	assign_data_vio(limiter, get_available_data_vio(limiter->pool));
}

static void update_limiter(struct limiter *limiter)
{
	struct bio_list *waiters = &limiter->waiters;
	data_vio_count_t available = limiter->limit - limiter->busy;

	VDO_ASSERT_LOG_ONLY((limiter->release_count <= limiter->busy),
			    "Release count %u is not more than busy count %u",
			    limiter->release_count, limiter->busy);

	get_waiters(limiter);
	for (; (limiter->release_count > 0) && !bio_list_empty(waiters); limiter->release_count--)
		limiter->assigner(limiter);

	if (limiter->release_count > 0) {
		WRITE_ONCE(limiter->busy, limiter->busy - limiter->release_count);
		limiter->release_count = 0;
		return;
	}

	for (; (available > 0) && !bio_list_empty(waiters); available--)
		limiter->assigner(limiter);

	WRITE_ONCE(limiter->busy, limiter->limit - available);
	if (limiter->max_busy < limiter->busy)
		WRITE_ONCE(limiter->max_busy, limiter->busy);
}

/**
 * schedule_releases() - Ensure that release processing is scheduled.
 *
 * If this call switches the state to processing, enqueue. Otherwise, some other thread has already
 * done so.
 */
static void schedule_releases(struct data_vio_pool *pool)
{
	/* Pairs with the barrier in process_release_callback(). */
	smp_mb__before_atomic();
	if (atomic_cmpxchg(&pool->processing, false, true))
		return;

	pool->completion.requeue = true;
	vdo_launch_completion_with_priority(&pool->completion,
					    CPU_Q_COMPLETE_VIO_PRIORITY);
}

static void reuse_or_release_resources(struct data_vio_pool *pool,
				       struct data_vio *data_vio,
				       struct list_head *returned)
{
	if (data_vio->remaining_discard > 0) {
		if (bio_list_empty(&pool->discard_limiter.waiters)) {
			/* Return the data_vio's discard permit. */
			pool->discard_limiter.release_count++;
		} else {
			assign_discard_permit(&pool->discard_limiter);
		}
	}

	if (pool->limiter.arrival < pool->discard_limiter.arrival) {
		assign_data_vio(&pool->limiter, data_vio);
	} else if (pool->discard_limiter.arrival < U64_MAX) {
		assign_data_vio(&pool->discard_limiter, data_vio);
	} else {
		list_add(&data_vio->pool_entry, returned);
		pool->limiter.release_count++;
	}
}

/**
 * process_release_callback() - Process a batch of data_vio releases.
 * @completion: The pool with data_vios to release.
 */
static void process_release_callback(struct vdo_completion *completion)
{
	struct data_vio_pool *pool = as_data_vio_pool(completion);
	bool reschedule;
	bool drained;
	data_vio_count_t processed;
	data_vio_count_t to_wake;
	data_vio_count_t discards_to_wake;
	LIST_HEAD(returned);

	spin_lock(&pool->lock);
	get_waiters(&pool->discard_limiter);
	get_waiters(&pool->limiter);
	spin_unlock(&pool->lock);

	if (pool->limiter.arrival == U64_MAX) {
		struct bio *bio = bio_list_peek(&pool->limiter.waiters);

		if (bio != NULL)
			pool->limiter.arrival = get_arrival_time(bio);
	}

	for (processed = 0; processed < DATA_VIO_RELEASE_BATCH_SIZE; processed++) {
		struct data_vio *data_vio;
		struct funnel_queue_entry *entry = vdo_funnel_queue_poll(pool->queue);

		if (entry == NULL)
			break;

		data_vio = as_data_vio(container_of(entry, struct vdo_completion,
						    work_queue_entry_link));
		acknowledge_data_vio(data_vio);
		reuse_or_release_resources(pool, data_vio, &returned);
	}

	spin_lock(&pool->lock);
	/*
	 * There is a race where waiters could be added while we are in the unlocked section above.
	 * Those waiters could not see the resources we are now about to release, so we assign
	 * those resources now as we have no guarantee of being rescheduled. This is handled in
	 * update_limiter().
	 */
	update_limiter(&pool->discard_limiter);
	list_splice(&returned, &pool->available);
	update_limiter(&pool->limiter);
	to_wake = pool->limiter.wake_count;
	pool->limiter.wake_count = 0;
	discards_to_wake = pool->discard_limiter.wake_count;
	pool->discard_limiter.wake_count = 0;

	atomic_set(&pool->processing, false);
	/* Pairs with the barrier in schedule_releases(). */
	smp_mb();

	reschedule = !vdo_is_funnel_queue_empty(pool->queue);
	drained = (!reschedule &&
		   vdo_is_state_draining(&pool->state) &&
		   check_for_drain_complete_locked(pool));
	spin_unlock(&pool->lock);

	if (to_wake > 0)
		wake_up_nr(&pool->limiter.blocked_threads, to_wake);

	if (discards_to_wake > 0)
		wake_up_nr(&pool->discard_limiter.blocked_threads, discards_to_wake);

	if (reschedule)
		schedule_releases(pool);
	else if (drained)
		vdo_finish_draining(&pool->state);
}

static void initialize_limiter(struct limiter *limiter, struct data_vio_pool *pool,
			       assigner_fn assigner, data_vio_count_t limit)
{
	limiter->pool = pool;
	limiter->assigner = assigner;
	limiter->limit = limit;
	limiter->arrival = U64_MAX;
	init_waitqueue_head(&limiter->blocked_threads);
}

/**
 * initialize_data_vio() - Allocate the components of a data_vio.
 *
 * The caller is responsible for cleaning up the data_vio on error.
 *
 * Return: VDO_SUCCESS or an error.
 */
static int initialize_data_vio(struct data_vio *data_vio, struct vdo *vdo)
{
	struct bio *bio;
	int result;

	BUILD_BUG_ON(VDO_BLOCK_SIZE > PAGE_SIZE);
	result = vdo_allocate_memory(VDO_BLOCK_SIZE, 0, "data_vio data",
				     &data_vio->vio.data);
	if (result != VDO_SUCCESS)
		return vdo_log_error_strerror(result,
					      "data_vio data allocation failure");

	result = vdo_allocate_memory(VDO_BLOCK_SIZE, 0, "compressed block",
				     &data_vio->compression.block);
	if (result != VDO_SUCCESS) {
		return vdo_log_error_strerror(result,
					      "data_vio compressed block allocation failure");
	}

	result = vdo_allocate_memory(VDO_BLOCK_SIZE, 0, "vio scratch",
				     &data_vio->scratch_block);
	if (result != VDO_SUCCESS)
		return vdo_log_error_strerror(result,
					      "data_vio scratch allocation failure");

	result = vdo_create_bio(&bio);
	if (result != VDO_SUCCESS)
		return vdo_log_error_strerror(result,
					      "data_vio data bio allocation failure");

	vdo_initialize_completion(&data_vio->decrement_completion, vdo,
				  VDO_DECREMENT_COMPLETION);
	initialize_vio(&data_vio->vio, bio, 1, VIO_TYPE_DATA, VIO_PRIORITY_DATA, vdo);

	return VDO_SUCCESS;
}

static void destroy_data_vio(struct data_vio *data_vio)
{
	if (data_vio == NULL)
		return;

	vdo_free_bio(vdo_forget(data_vio->vio.bio));
	vdo_free(vdo_forget(data_vio->vio.data));
	vdo_free(vdo_forget(data_vio->compression.block));
	vdo_free(vdo_forget(data_vio->scratch_block));
}

/**
 * make_data_vio_pool() - Initialize a data_vio pool.
 * @vdo: The vdo to which the pool will belong.
 * @pool_size: The number of data_vios in the pool.
 * @discard_limit: The maximum number of data_vios which may be used for discards.
 * @pool_ptr: A pointer to hold the newly allocated pool.
 */
int make_data_vio_pool(struct vdo *vdo, data_vio_count_t pool_size,
		       data_vio_count_t discard_limit, struct data_vio_pool **pool_ptr)
{
	int result;
	struct data_vio_pool *pool;
	data_vio_count_t i;

	result = vdo_allocate_extended(struct data_vio_pool, pool_size, struct data_vio,
				       __func__, &pool);
	if (result != VDO_SUCCESS)
		return result;

	VDO_ASSERT_LOG_ONLY((discard_limit <= pool_size),
			    "discard limit does not exceed pool size");
	initialize_limiter(&pool->discard_limiter, pool, assign_discard_permit,
			   discard_limit);
	pool->discard_limiter.permitted_waiters = &pool->permitted_discards;
	initialize_limiter(&pool->limiter, pool, assign_data_vio_to_waiter, pool_size);
	pool->limiter.permitted_waiters = &pool->limiter.waiters;
	INIT_LIST_HEAD(&pool->available);
	spin_lock_init(&pool->lock);
	vdo_set_admin_state_code(&pool->state, VDO_ADMIN_STATE_NORMAL_OPERATION);
	vdo_initialize_completion(&pool->completion, vdo, VDO_DATA_VIO_POOL_COMPLETION);
	vdo_prepare_completion(&pool->completion, process_release_callback,
			       process_release_callback, vdo->thread_config.cpu_thread,
			       NULL);

	result = vdo_make_funnel_queue(&pool->queue);
	if (result != VDO_SUCCESS) {
		free_data_vio_pool(vdo_forget(pool));
		return result;
	}

	for (i = 0; i < pool_size; i++) {
		struct data_vio *data_vio = &pool->data_vios[i];

		result = initialize_data_vio(data_vio, vdo);
		if (result != VDO_SUCCESS) {
			destroy_data_vio(data_vio);
			free_data_vio_pool(pool);
			return result;
		}

		list_add(&data_vio->pool_entry, &pool->available);
	}

	*pool_ptr = pool;
	return VDO_SUCCESS;
}

/**
 * free_data_vio_pool() - Free a data_vio_pool and the data_vios in it.
 *
 * All data_vios must be returned to the pool before calling this function.
 */
void free_data_vio_pool(struct data_vio_pool *pool)
{
	struct data_vio *data_vio, *tmp;

	if (pool == NULL)
		return;

	/*
	 * Pairs with the barrier in process_release_callback(). Possibly not needed since it
	 * caters to an enqueue vs. free race.
	 */
	smp_mb();
	BUG_ON(atomic_read(&pool->processing));

	spin_lock(&pool->lock);
	VDO_ASSERT_LOG_ONLY((pool->limiter.busy == 0),
			    "data_vio pool must not have %u busy entries when being freed",
			    pool->limiter.busy);
	VDO_ASSERT_LOG_ONLY((bio_list_empty(&pool->limiter.waiters) &&
			     bio_list_empty(&pool->limiter.new_waiters)),
			    "data_vio pool must not have threads waiting to read or write when being freed");
	VDO_ASSERT_LOG_ONLY((bio_list_empty(&pool->discard_limiter.waiters) &&
			     bio_list_empty(&pool->discard_limiter.new_waiters)),
			    "data_vio pool must not have threads waiting to discard when being freed");
	spin_unlock(&pool->lock);

	list_for_each_entry_safe(data_vio, tmp, &pool->available, pool_entry) {
		list_del_init(&data_vio->pool_entry);
		destroy_data_vio(data_vio);
	}

	vdo_free_funnel_queue(vdo_forget(pool->queue));
	vdo_free(pool);
}

static bool acquire_permit(struct limiter *limiter)
{
	if (limiter->busy >= limiter->limit)
		return false;

	WRITE_ONCE(limiter->busy, limiter->busy + 1);
	if (limiter->max_busy < limiter->busy)
		WRITE_ONCE(limiter->max_busy, limiter->busy);
	return true;
}

static void wait_permit(struct limiter *limiter, struct bio *bio)
	__releases(&limiter->pool->lock)
{
	DEFINE_WAIT(wait);

	bio_list_add(&limiter->new_waiters, bio);
	prepare_to_wait_exclusive(&limiter->blocked_threads, &wait,
				  TASK_UNINTERRUPTIBLE);
	spin_unlock(&limiter->pool->lock);
	io_schedule();
	finish_wait(&limiter->blocked_threads, &wait);
}

/**
 * vdo_launch_bio() - Acquire a data_vio from the pool, assign the bio to it, and launch it.
 *
 * This will block if data_vios or discard permits are not available.
 */
void vdo_launch_bio(struct data_vio_pool *pool, struct bio *bio)
{
	struct data_vio *data_vio;

	VDO_ASSERT_LOG_ONLY(!vdo_is_state_quiescent(&pool->state),
			    "data_vio_pool not quiescent on acquire");

	bio->bi_private = (void *) jiffies;
	spin_lock(&pool->lock);
	if ((bio_op(bio) == REQ_OP_DISCARD) &&
	    !acquire_permit(&pool->discard_limiter)) {
		wait_permit(&pool->discard_limiter, bio);
		return;
	}

	if (!acquire_permit(&pool->limiter)) {
		wait_permit(&pool->limiter, bio);
		return;
	}

	data_vio = get_available_data_vio(pool);
	spin_unlock(&pool->lock);
	launch_bio(pool->completion.vdo, data_vio, bio);
}

/* Implements vdo_admin_initiator_fn. */
static void initiate_drain(struct admin_state *state)
{
	bool drained;
	struct data_vio_pool *pool = container_of(state, struct data_vio_pool, state);

	spin_lock(&pool->lock);
	drained = check_for_drain_complete_locked(pool);
	spin_unlock(&pool->lock);

	if (drained)
		vdo_finish_draining(state);
}

static void assert_on_vdo_cpu_thread(const struct vdo *vdo, const char *name)
{
	VDO_ASSERT_LOG_ONLY((vdo_get_callback_thread_id() == vdo->thread_config.cpu_thread),
			    "%s called on cpu thread", name);
}

/**
 * drain_data_vio_pool() - Wait asynchronously for all data_vios to be returned to the pool.
 * @completion: The completion to notify when the pool has drained.
 */
void drain_data_vio_pool(struct data_vio_pool *pool, struct vdo_completion *completion)
{
	assert_on_vdo_cpu_thread(completion->vdo, __func__);
	vdo_start_draining(&pool->state, VDO_ADMIN_STATE_SUSPENDING, completion,
			   initiate_drain);
}

/**
 * resume_data_vio_pool() - Resume a data_vio pool.
 * @completion: The completion to notify when the pool has resumed.
 */
void resume_data_vio_pool(struct data_vio_pool *pool, struct vdo_completion *completion)
{
	assert_on_vdo_cpu_thread(completion->vdo, __func__);
	vdo_continue_completion(completion, vdo_resume_if_quiescent(&pool->state));
}

static void dump_limiter(const char *name, struct limiter *limiter)
{
	vdo_log_info("%s: %u of %u busy (max %u), %s", name, limiter->busy,
		     limiter->limit, limiter->max_busy,
		     ((bio_list_empty(&limiter->waiters) &&
		       bio_list_empty(&limiter->new_waiters)) ?
		      "no waiters" : "has waiters"));
}

/**
 * dump_data_vio_pool() - Dump a data_vio pool to the log.
 * @dump_vios: Whether to dump the details of each busy data_vio as well.
 */
void dump_data_vio_pool(struct data_vio_pool *pool, bool dump_vios)
{
	/*
	 * In order that syslog can empty its buffer, sleep after 35 elements for 4ms (till the
	 * second clock tick).  These numbers were picked based on experiments with lab machines.
	 */
	static const int ELEMENTS_PER_BATCH = 35;
	static const int SLEEP_FOR_SYSLOG = 4000;

	if (pool == NULL)
		return;

	spin_lock(&pool->lock);
	dump_limiter("data_vios", &pool->limiter);
	dump_limiter("discard permits", &pool->discard_limiter);
	if (dump_vios) {
		int i;
		int dumped = 0;

		for (i = 0; i < pool->limiter.limit; i++) {
			struct data_vio *data_vio = &pool->data_vios[i];

			if (!list_empty(&data_vio->pool_entry))
				continue;

			dump_data_vio(data_vio);
			if (++dumped >= ELEMENTS_PER_BATCH) {
				spin_unlock(&pool->lock);
				dumped = 0;
				fsleep(SLEEP_FOR_SYSLOG);
				spin_lock(&pool->lock);
			}
		}
	}

	spin_unlock(&pool->lock);
}

data_vio_count_t get_data_vio_pool_active_requests(struct data_vio_pool *pool)
{
	return READ_ONCE(pool->limiter.busy);
}

data_vio_count_t get_data_vio_pool_request_limit(struct data_vio_pool *pool)
{
	return READ_ONCE(pool->limiter.limit);
}

data_vio_count_t get_data_vio_pool_maximum_requests(struct data_vio_pool *pool)
{
	return READ_ONCE(pool->limiter.max_busy);
}

static void update_data_vio_error_stats(struct data_vio *data_vio)
{
	u8 index = 0;
	static const char * const operations[] = {
		[0] = "empty",
		[1] = "read",
		[2] = "write",
		[3] = "read-modify-write",
		[5] = "read+fua",
		[6] = "write+fua",
		[7] = "read-modify-write+fua",
	};

	if (data_vio->read)
		index = 1;

	if (data_vio->write)
		index += 2;

	if (data_vio->fua)
		index += 4;

	update_vio_error_stats(&data_vio->vio,
			       "Completing %s vio for LBN %llu with error after %s",
			       operations[index],
			       (unsigned long long) data_vio->logical.lbn,
			       get_data_vio_operation_name(data_vio));
}

static void perform_cleanup_stage(struct data_vio *data_vio,
				  enum data_vio_cleanup_stage stage);

/**
 * release_allocated_lock() - Release the PBN lock and/or the reference on the allocated block at
 *			      the end of processing a data_vio.
 */
static void release_allocated_lock(struct vdo_completion *completion)
{
	struct data_vio *data_vio = as_data_vio(completion);

	assert_data_vio_in_allocated_zone(data_vio);
	release_data_vio_allocation_lock(data_vio, false);
	perform_cleanup_stage(data_vio, VIO_RELEASE_RECOVERY_LOCKS);
}

/** release_lock() - Release an uncontended LBN lock. */
static void release_lock(struct data_vio *data_vio, struct lbn_lock *lock)
{
	struct int_map *lock_map = lock->zone->lbn_operations;
	struct data_vio *lock_holder;

	if (!lock->locked) {
		/*  The lock is not locked, so it had better not be registered in the lock map. */
		struct data_vio *lock_holder = vdo_int_map_get(lock_map, lock->lbn);

		VDO_ASSERT_LOG_ONLY((data_vio != lock_holder),
				    "no logical block lock held for block %llu",
				    (unsigned long long) lock->lbn);
		return;
	}

	/* Release the lock by removing the lock from the map. */
	lock_holder = vdo_int_map_remove(lock_map, lock->lbn);
	VDO_ASSERT_LOG_ONLY((data_vio == lock_holder),
			    "logical block lock mismatch for block %llu",
			    (unsigned long long) lock->lbn);
	lock->locked = false;
}

/** transfer_lock() - Transfer a contended LBN lock to the eldest waiter. */
static void transfer_lock(struct data_vio *data_vio, struct lbn_lock *lock)
{
	struct data_vio *lock_holder, *next_lock_holder;
	int result;

	VDO_ASSERT_LOG_ONLY(lock->locked, "lbn_lock with waiters is not locked");

	/* Another data_vio is waiting for the lock, transfer it in a single lock map operation. */
	next_lock_holder =
		vdo_waiter_as_data_vio(vdo_waitq_dequeue_waiter(&lock->waiters));

	/* Transfer the remaining lock waiters to the next lock holder. */
	vdo_waitq_transfer_all_waiters(&lock->waiters,
				       &next_lock_holder->logical.waiters);

	result = vdo_int_map_put(lock->zone->lbn_operations, lock->lbn,
				 next_lock_holder, true, (void **) &lock_holder);
	if (result != VDO_SUCCESS) {
		continue_data_vio_with_error(next_lock_holder, result);
		return;
	}

	VDO_ASSERT_LOG_ONLY((lock_holder == data_vio),
			    "logical block lock mismatch for block %llu",
			    (unsigned long long) lock->lbn);
	lock->locked = false;

	/*
	 * If there are still waiters, other data_vios must be trying to get the lock we just
	 * transferred. We must ensure that the new lock holder doesn't block in the packer.
	 */
	if (vdo_waitq_has_waiters(&next_lock_holder->logical.waiters))
		cancel_data_vio_compression(next_lock_holder);

	/*
	 * Avoid stack overflow on lock transfer.
	 * FIXME: this is only an issue in the 1 thread config.
	 */
	next_lock_holder->vio.completion.requeue = true;
	launch_locked_request(next_lock_holder);
}

/**
 * release_logical_lock() - Release the logical block lock and flush generation lock at the end of
 *			    processing a data_vio.
 */
static void release_logical_lock(struct vdo_completion *completion)
{
	struct data_vio *data_vio = as_data_vio(completion);
	struct lbn_lock *lock = &data_vio->logical;

	assert_data_vio_in_logical_zone(data_vio);

	if (vdo_waitq_has_waiters(&lock->waiters))
		transfer_lock(data_vio, lock);
	else
		release_lock(data_vio, lock);

	vdo_release_flush_generation_lock(data_vio);
	perform_cleanup_stage(data_vio, VIO_CLEANUP_DONE);
}

/** clean_hash_lock() - Release the hash lock at the end of processing a data_vio. */
static void clean_hash_lock(struct vdo_completion *completion)
{
	struct data_vio *data_vio = as_data_vio(completion);

	assert_data_vio_in_hash_zone(data_vio);
	if (completion->result != VDO_SUCCESS) {
		vdo_clean_failed_hash_lock(data_vio);
		return;
	}

	vdo_release_hash_lock(data_vio);
	perform_cleanup_stage(data_vio, VIO_RELEASE_LOGICAL);
}

/**
 * finish_cleanup() - Make some assertions about a data_vio which has finished cleaning up.
 *
 * If it is part of a multi-block discard, starts on the next block, otherwise, returns it to the
 * pool.
 */
static void finish_cleanup(struct data_vio *data_vio)
{
	struct vdo_completion *completion = &data_vio->vio.completion;
	u32 discard_size = min_t(u32, data_vio->remaining_discard,
				 VDO_BLOCK_SIZE - data_vio->offset);

	VDO_ASSERT_LOG_ONLY(data_vio->allocation.lock == NULL,
			    "complete data_vio has no allocation lock");
	VDO_ASSERT_LOG_ONLY(data_vio->hash_lock == NULL,
			    "complete data_vio has no hash lock");
	if ((data_vio->remaining_discard <= discard_size) ||
	    (completion->result != VDO_SUCCESS)) {
		struct data_vio_pool *pool = completion->vdo->data_vio_pool;

		vdo_funnel_queue_put(pool->queue, &completion->work_queue_entry_link);
		schedule_releases(pool);
		return;
	}

	data_vio->remaining_discard -= discard_size;
	data_vio->is_partial = (data_vio->remaining_discard < VDO_BLOCK_SIZE);
	data_vio->read = data_vio->is_partial;
	data_vio->offset = 0;
	completion->requeue = true;
	data_vio->first_reference_operation_complete = false;
	launch_data_vio(data_vio, data_vio->logical.lbn + 1);
}

/** perform_cleanup_stage() - Perform the next step in the process of cleaning up a data_vio. */
static void perform_cleanup_stage(struct data_vio *data_vio,
				  enum data_vio_cleanup_stage stage)
{
	struct vdo *vdo = vdo_from_data_vio(data_vio);

	switch (stage) {
	case VIO_RELEASE_HASH_LOCK:
		if (data_vio->hash_lock != NULL) {
			launch_data_vio_hash_zone_callback(data_vio, clean_hash_lock);
			return;
		}
		fallthrough;

	case VIO_RELEASE_ALLOCATED:
		if (data_vio_has_allocation(data_vio)) {
			launch_data_vio_allocated_zone_callback(data_vio,
								release_allocated_lock);
			return;
		}
		fallthrough;

	case VIO_RELEASE_RECOVERY_LOCKS:
		if ((data_vio->recovery_sequence_number > 0) &&
		    (READ_ONCE(vdo->read_only_notifier.read_only_error) == VDO_SUCCESS) &&
		    (data_vio->vio.completion.result != VDO_READ_ONLY))
			vdo_log_warning("VDO not read-only when cleaning data_vio with RJ lock");
		fallthrough;

	case VIO_RELEASE_LOGICAL:
		launch_data_vio_logical_callback(data_vio, release_logical_lock);
		return;

	default:
		finish_cleanup(data_vio);
	}
}

void complete_data_vio(struct vdo_completion *completion)
{
	struct data_vio *data_vio = as_data_vio(completion);

	completion->error_handler = NULL;
	data_vio->last_async_operation = VIO_ASYNC_OP_CLEANUP;
	perform_cleanup_stage(data_vio,
			      (data_vio->write ? VIO_CLEANUP_START : VIO_RELEASE_LOGICAL));
}

static void enter_read_only_mode(struct vdo_completion *completion)
{
	if (vdo_is_read_only(completion->vdo))
		return;

	if (completion->result != VDO_READ_ONLY) {
		struct data_vio *data_vio = as_data_vio(completion);

		vdo_log_error_strerror(completion->result,
				       "Preparing to enter read-only mode: data_vio for LBN %llu (becoming mapped to %llu, previously mapped to %llu, allocated %llu) is completing with a fatal error after operation %s",
				       (unsigned long long) data_vio->logical.lbn,
				       (unsigned long long) data_vio->new_mapped.pbn,
				       (unsigned long long) data_vio->mapped.pbn,
				       (unsigned long long) data_vio->allocation.pbn,
				       get_data_vio_operation_name(data_vio));
	}

	vdo_enter_read_only_mode(completion->vdo, completion->result);
}

void handle_data_vio_error(struct vdo_completion *completion)
{
	struct data_vio *data_vio = as_data_vio(completion);

	if ((completion->result == VDO_READ_ONLY) || (data_vio->user_bio == NULL))
		enter_read_only_mode(completion);

	update_data_vio_error_stats(data_vio);
	complete_data_vio(completion);
}

/**
 * get_data_vio_operation_name() - Get the name of the last asynchronous operation performed on a
 *				   data_vio.
 */
const char *get_data_vio_operation_name(struct data_vio *data_vio)
{
	BUILD_BUG_ON((MAX_VIO_ASYNC_OPERATION_NUMBER - MIN_VIO_ASYNC_OPERATION_NUMBER) !=
		     ARRAY_SIZE(ASYNC_OPERATION_NAMES));

	return ((data_vio->last_async_operation < MAX_VIO_ASYNC_OPERATION_NUMBER) ?
		ASYNC_OPERATION_NAMES[data_vio->last_async_operation] :
		"unknown async operation");
}

/**
 * data_vio_allocate_data_block() - Allocate a data block.
 *
 * @write_lock_type: The type of write lock to obtain on the block.
 * @callback: The callback which will attempt an allocation in the current zone and continue if it
 *	      succeeds.
 * @error_handler: The handler for errors while allocating.
 */
void data_vio_allocate_data_block(struct data_vio *data_vio,
				  enum pbn_lock_type write_lock_type,
				  vdo_action_fn callback, vdo_action_fn error_handler)
{
	struct allocation *allocation = &data_vio->allocation;

	VDO_ASSERT_LOG_ONLY((allocation->pbn == VDO_ZERO_BLOCK),
			    "data_vio does not have an allocation");
	allocation->write_lock_type = write_lock_type;
	allocation->zone = vdo_get_next_allocation_zone(data_vio->logical.zone);
	allocation->first_allocation_zone = allocation->zone->zone_number;

	data_vio->vio.completion.error_handler = error_handler;
	launch_data_vio_allocated_zone_callback(data_vio, callback);
}

/**
 * release_data_vio_allocation_lock() - Release the PBN lock on a data_vio's allocated block.
 * @reset: If true, the allocation will be reset (i.e. any allocated pbn will be forgotten).
 *
 * If the reference to the locked block is still provisional, it will be released as well.
 */
void release_data_vio_allocation_lock(struct data_vio *data_vio, bool reset)
{
	struct allocation *allocation = &data_vio->allocation;
	physical_block_number_t locked_pbn = allocation->pbn;

	assert_data_vio_in_allocated_zone(data_vio);

	if (reset || vdo_pbn_lock_has_provisional_reference(allocation->lock))
		allocation->pbn = VDO_ZERO_BLOCK;

	vdo_release_physical_zone_pbn_lock(allocation->zone, locked_pbn,
					   vdo_forget(allocation->lock));
}

/**
 * uncompress_data_vio() - Uncompress the data a data_vio has just read.
 * @mapping_state: The mapping state indicating which fragment to decompress.
 * @buffer: The buffer to receive the uncompressed data.
 */
int uncompress_data_vio(struct data_vio *data_vio,
			enum block_mapping_state mapping_state, char *buffer)
{
	int size;
	u16 fragment_offset, fragment_size;
	struct compressed_block *block = data_vio->compression.block;
	int result = vdo_get_compressed_block_fragment(mapping_state, block,
						       &fragment_offset, &fragment_size);

	if (result != VDO_SUCCESS) {
		vdo_log_debug("%s: compressed fragment error %d", __func__, result);
		return result;
	}

	size = LZ4_decompress_safe((block->data + fragment_offset), buffer,
				   fragment_size, VDO_BLOCK_SIZE);
	if (size != VDO_BLOCK_SIZE) {
		vdo_log_debug("%s: lz4 error", __func__);
		return VDO_INVALID_FRAGMENT;
	}

	return VDO_SUCCESS;
}

/**
 * modify_for_partial_write() - Do the modify-write part of a read-modify-write cycle.
 * @completion: The data_vio which has just finished its read.
 *
 * This callback is registered in read_block().
 */
static void modify_for_partial_write(struct vdo_completion *completion)
{
	struct data_vio *data_vio = as_data_vio(completion);
	char *data = data_vio->vio.data;
	struct bio *bio = data_vio->user_bio;

	assert_data_vio_on_cpu_thread(data_vio);

	if (bio_op(bio) == REQ_OP_DISCARD) {
		memset(data + data_vio->offset, '\0', min_t(u32,
							    data_vio->remaining_discard,
							    VDO_BLOCK_SIZE - data_vio->offset));
	} else {
		copy_from_bio(bio, data + data_vio->offset);
	}

	data_vio->is_zero = mem_is_zero(data, VDO_BLOCK_SIZE);
	data_vio->read = false;
	launch_data_vio_logical_callback(data_vio,
					 continue_data_vio_with_block_map_slot);
}

static void complete_read(struct vdo_completion *completion)
{
	struct data_vio *data_vio = as_data_vio(completion);
	char *data = data_vio->vio.data;
	bool compressed = vdo_is_state_compressed(data_vio->mapped.state);

	assert_data_vio_on_cpu_thread(data_vio);

	if (compressed) {
		int result = uncompress_data_vio(data_vio, data_vio->mapped.state, data);

		if (result != VDO_SUCCESS) {
			continue_data_vio_with_error(data_vio, result);
			return;
		}
	}

	if (data_vio->write) {
		modify_for_partial_write(completion);
		return;
	}

	if (compressed || data_vio->is_partial)
		copy_to_bio(data_vio->user_bio, data + data_vio->offset);

	acknowledge_data_vio(data_vio);
	complete_data_vio(completion);
}

static void read_endio(struct bio *bio)
{
	struct data_vio *data_vio = vio_as_data_vio(bio->bi_private);
	int result = blk_status_to_errno(bio->bi_status);

	vdo_count_completed_bios(bio);
	if (result != VDO_SUCCESS) {
		continue_data_vio_with_error(data_vio, result);
		return;
	}

	launch_data_vio_cpu_callback(data_vio, complete_read,
				     CPU_Q_COMPLETE_READ_PRIORITY);
}

static void complete_zero_read(struct vdo_completion *completion)
{
	struct data_vio *data_vio = as_data_vio(completion);

	assert_data_vio_on_cpu_thread(data_vio);

	if (data_vio->is_partial) {
		memset(data_vio->vio.data, 0, VDO_BLOCK_SIZE);
		if (data_vio->write) {
			modify_for_partial_write(completion);
			return;
		}
	} else {
		zero_fill_bio(data_vio->user_bio);
	}

	complete_read(completion);
}

/**
 * read_block() - Read a block asynchronously.
 *
 * This is the callback registered in read_block_mapping().
 */
static void read_block(struct vdo_completion *completion)
{
	struct data_vio *data_vio = as_data_vio(completion);
	struct vio *vio = as_vio(completion);
	int result = VDO_SUCCESS;

	if (data_vio->mapped.pbn == VDO_ZERO_BLOCK) {
		launch_data_vio_cpu_callback(data_vio, complete_zero_read,
					     CPU_Q_COMPLETE_VIO_PRIORITY);
		return;
	}

	data_vio->last_async_operation = VIO_ASYNC_OP_READ_DATA_VIO;
	if (vdo_is_state_compressed(data_vio->mapped.state)) {
		result = vio_reset_bio(vio, (char *) data_vio->compression.block,
				       read_endio, REQ_OP_READ, data_vio->mapped.pbn);
	} else {
		blk_opf_t opf = ((data_vio->user_bio->bi_opf & PASSTHROUGH_FLAGS) | REQ_OP_READ);

		if (data_vio->is_partial) {
			result = vio_reset_bio(vio, vio->data, read_endio, opf,
					       data_vio->mapped.pbn);
		} else {
			/* A full 4k read. Use the incoming bio to avoid having to copy the data */
			bio_reset(vio->bio, vio->bio->bi_bdev, opf);
			bio_init_clone(data_vio->user_bio->bi_bdev, vio->bio,
				       data_vio->user_bio, GFP_KERNEL);

			/* Copy over the original bio iovec and opflags. */
			vdo_set_bio_properties(vio->bio, vio, read_endio, opf,
					       data_vio->mapped.pbn);
		}
	}

	if (result != VDO_SUCCESS) {
		continue_data_vio_with_error(data_vio, result);
		return;
	}

	vdo_submit_data_vio(data_vio);
}

static inline struct data_vio *
reference_count_update_completion_as_data_vio(struct vdo_completion *completion)
{
	if (completion->type == VIO_COMPLETION)
		return as_data_vio(completion);

	return container_of(completion, struct data_vio, decrement_completion);
}

/**
 * update_block_map() - Rendezvous of the data_vio and decrement completions after each has
 *                      made its reference updates. Handle any error from either, or proceed
 *                      to updating the block map.
 * @completion: The completion of the write in progress.
 */
static void update_block_map(struct vdo_completion *completion)
{
	struct data_vio *data_vio = reference_count_update_completion_as_data_vio(completion);

	assert_data_vio_in_logical_zone(data_vio);

	if (!data_vio->first_reference_operation_complete) {
		/* Rendezvous, we're first */
		data_vio->first_reference_operation_complete = true;
		return;
	}

	completion = &data_vio->vio.completion;
	vdo_set_completion_result(completion, data_vio->decrement_completion.result);
	if (completion->result != VDO_SUCCESS) {
		handle_data_vio_error(completion);
		return;
	}

	completion->error_handler = handle_data_vio_error;
	if (data_vio->hash_lock != NULL)
		set_data_vio_hash_zone_callback(data_vio, vdo_continue_hash_lock);
	else
		completion->callback = complete_data_vio;

	data_vio->last_async_operation = VIO_ASYNC_OP_PUT_MAPPED_BLOCK;
	vdo_put_mapped_block(data_vio);
}

static void decrement_reference_count(struct vdo_completion *completion)
{
	struct data_vio *data_vio = container_of(completion, struct data_vio,
						 decrement_completion);

	assert_data_vio_in_mapped_zone(data_vio);

	vdo_set_completion_callback(completion, update_block_map,
				    data_vio->logical.zone->thread_id);
	completion->error_handler = update_block_map;
	vdo_modify_reference_count(completion, &data_vio->decrement_updater);
}

static void increment_reference_count(struct vdo_completion *completion)
{
	struct data_vio *data_vio = as_data_vio(completion);

	assert_data_vio_in_new_mapped_zone(data_vio);

	if (data_vio->downgrade_allocation_lock) {
		/*
		 * Now that the data has been written, it's safe to deduplicate against the
		 * block. Downgrade the allocation lock to a read lock so it can be used later by
		 * the hash lock. This is done here since it needs to happen sometime before we
		 * return to the hash zone, and we are currently on the correct thread. For
		 * compressed blocks, the downgrade will have already been done.
		 */
		vdo_downgrade_pbn_write_lock(data_vio->allocation.lock, false);
	}

	set_data_vio_logical_callback(data_vio, update_block_map);
	completion->error_handler = update_block_map;
	vdo_modify_reference_count(completion, &data_vio->increment_updater);
}

/** journal_remapping() - Add a recovery journal entry for a data remapping. */
static void journal_remapping(struct vdo_completion *completion)
{
	struct data_vio *data_vio = as_data_vio(completion);

	assert_data_vio_in_journal_zone(data_vio);

	data_vio->decrement_updater.operation = VDO_JOURNAL_DATA_REMAPPING;
	data_vio->decrement_updater.zpbn = data_vio->mapped;
	if (data_vio->new_mapped.pbn == VDO_ZERO_BLOCK) {
		data_vio->first_reference_operation_complete = true;
		if (data_vio->mapped.pbn == VDO_ZERO_BLOCK)
			set_data_vio_logical_callback(data_vio, update_block_map);
	} else {
		set_data_vio_new_mapped_zone_callback(data_vio,
						      increment_reference_count);
	}

	if (data_vio->mapped.pbn == VDO_ZERO_BLOCK) {
		data_vio->first_reference_operation_complete = true;
	} else {
		vdo_set_completion_callback(&data_vio->decrement_completion,
					    decrement_reference_count,
					    data_vio->mapped.zone->thread_id);
	}

	data_vio->last_async_operation = VIO_ASYNC_OP_JOURNAL_REMAPPING;
	vdo_add_recovery_journal_entry(completion->vdo->recovery_journal, data_vio);
}

/**
 * read_old_block_mapping() - Get the previous PBN/LBN mapping of an in-progress write.
 *
 * Gets the previous PBN mapped to this LBN from the block map, so as to make an appropriate
 * journal entry referencing the removal of this LBN->PBN mapping.
 */
static void read_old_block_mapping(struct vdo_completion *completion)
{
	struct data_vio *data_vio = as_data_vio(completion);

	assert_data_vio_in_logical_zone(data_vio);

	data_vio->last_async_operation = VIO_ASYNC_OP_GET_MAPPED_BLOCK_FOR_WRITE;
	set_data_vio_journal_callback(data_vio, journal_remapping);
	vdo_get_mapped_block(data_vio);
}

void update_metadata_for_data_vio_write(struct data_vio *data_vio, struct pbn_lock *lock)
{
	data_vio->increment_updater = (struct reference_updater) {
		.operation = VDO_JOURNAL_DATA_REMAPPING,
		.increment = true,
		.zpbn = data_vio->new_mapped,
		.lock = lock,
	};

	launch_data_vio_logical_callback(data_vio, read_old_block_mapping);
}

/**
 * pack_compressed_data() - Attempt to pack the compressed data_vio into a block.
 *
 * This is the callback registered in launch_compress_data_vio().
 */
static void pack_compressed_data(struct vdo_completion *completion)
{
	struct data_vio *data_vio = as_data_vio(completion);

	assert_data_vio_in_packer_zone(data_vio);

	if (!vdo_get_compressing(vdo_from_data_vio(data_vio)) ||
	    get_data_vio_compression_status(data_vio).may_not_compress) {
		write_data_vio(data_vio);
		return;
	}

	data_vio->last_async_operation = VIO_ASYNC_OP_ATTEMPT_PACKING;
	vdo_attempt_packing(data_vio);
}

/**
 * compress_data_vio() - Do the actual work of compressing the data on a CPU queue.
 *
 * This callback is registered in launch_compress_data_vio().
 */
static void compress_data_vio(struct vdo_completion *completion)
{
	struct data_vio *data_vio = as_data_vio(completion);
	int size;

	assert_data_vio_on_cpu_thread(data_vio);

	/*
	 * By putting the compressed data at the start of the compressed block data field, we won't
	 * need to copy it if this data_vio becomes a compressed write agent.
	 */
	size = LZ4_compress_default(data_vio->vio.data,
				    data_vio->compression.block->data, VDO_BLOCK_SIZE,
				    VDO_MAX_COMPRESSED_FRAGMENT_SIZE,
				    (char *) vdo_get_work_queue_private_data());
	if ((size > 0) && (size < VDO_COMPRESSED_BLOCK_DATA_SIZE)) {
		data_vio->compression.size = size;
		launch_data_vio_packer_callback(data_vio, pack_compressed_data);
		return;
	}

	write_data_vio(data_vio);
}

/**
 * launch_compress_data_vio() - Continue a write by attempting to compress the data.
 *
 * This is a re-entry point to vio_write used by hash locks.
 */
void launch_compress_data_vio(struct data_vio *data_vio)
{
	VDO_ASSERT_LOG_ONLY(!data_vio->is_duplicate, "compressing a non-duplicate block");
	VDO_ASSERT_LOG_ONLY(data_vio->hash_lock != NULL,
			    "data_vio to compress has a hash_lock");
	VDO_ASSERT_LOG_ONLY(data_vio_has_allocation(data_vio),
			    "data_vio to compress has an allocation");

	/*
	 * There are 4 reasons why a data_vio which has reached this point will not be eligible for
	 * compression:
	 *
	 * 1) Since data_vios can block indefinitely in the packer, it would be bad to do so if the
	 * write request also requests FUA.
	 *
	 * 2) A data_vio should not be compressed when compression is disabled for the vdo.
	 *
	 * 3) A data_vio could be doing a partial write on behalf of a larger discard which has not
	 * yet been acknowledged and hence blocking in the packer would be bad.
	 *
	 * 4) Some other data_vio may be waiting on this data_vio in which case blocking in the
	 * packer would also be bad.
	 */
	if (data_vio->fua ||
	    !vdo_get_compressing(vdo_from_data_vio(data_vio)) ||
	    ((data_vio->user_bio != NULL) && (bio_op(data_vio->user_bio) == REQ_OP_DISCARD)) ||
	    (advance_data_vio_compression_stage(data_vio).stage != DATA_VIO_COMPRESSING)) {
		write_data_vio(data_vio);
		return;
	}

	data_vio->last_async_operation = VIO_ASYNC_OP_COMPRESS_DATA_VIO;
	launch_data_vio_cpu_callback(data_vio, compress_data_vio,
				     CPU_Q_COMPRESS_BLOCK_PRIORITY);
}

/**
 * hash_data_vio() - Hash the data in a data_vio and set the hash zone (which also flags the record
 *		     name as set).

 * This callback is registered in prepare_for_dedupe().
 */
static void hash_data_vio(struct vdo_completion *completion)
{
	struct data_vio *data_vio = as_data_vio(completion);

	assert_data_vio_on_cpu_thread(data_vio);
	VDO_ASSERT_LOG_ONLY(!data_vio->is_zero, "zero blocks should not be hashed");

	murmurhash3_128(data_vio->vio.data, VDO_BLOCK_SIZE, 0x62ea60be,
			&data_vio->record_name);

	data_vio->hash_zone = vdo_select_hash_zone(vdo_from_data_vio(data_vio)->hash_zones,
						   &data_vio->record_name);
	data_vio->last_async_operation = VIO_ASYNC_OP_ACQUIRE_VDO_HASH_LOCK;
	launch_data_vio_hash_zone_callback(data_vio, vdo_acquire_hash_lock);
}

/** prepare_for_dedupe() - Prepare for the dedupe path after attempting to get an allocation. */
static void prepare_for_dedupe(struct data_vio *data_vio)
{
	/* We don't care what thread we are on. */
	VDO_ASSERT_LOG_ONLY(!data_vio->is_zero, "must not prepare to dedupe zero blocks");

	/*
	 * Before we can dedupe, we need to know the record name, so the first
	 * step is to hash the block data.
	 */
	data_vio->last_async_operation = VIO_ASYNC_OP_HASH_DATA_VIO;
	launch_data_vio_cpu_callback(data_vio, hash_data_vio, CPU_Q_HASH_BLOCK_PRIORITY);
}

/**
 * write_bio_finished() - This is the bio_end_io function registered in write_block() to be called
 *			  when a data_vio's write to the underlying storage has completed.
 */
static void write_bio_finished(struct bio *bio)
{
	struct data_vio *data_vio = vio_as_data_vio((struct vio *) bio->bi_private);

	vdo_count_completed_bios(bio);
	vdo_set_completion_result(&data_vio->vio.completion,
				  blk_status_to_errno(bio->bi_status));
	data_vio->downgrade_allocation_lock = true;
	update_metadata_for_data_vio_write(data_vio, data_vio->allocation.lock);
}

/** write_data_vio() - Write a data block to storage without compression. */
void write_data_vio(struct data_vio *data_vio)
{
	struct data_vio_compression_status status, new_status;
	int result;

	if (!data_vio_has_allocation(data_vio)) {
		/*
		 * There was no space to write this block and we failed to deduplicate or compress
		 * it.
		 */
		continue_data_vio_with_error(data_vio, VDO_NO_SPACE);
		return;
	}

	new_status = (struct data_vio_compression_status) {
		.stage = DATA_VIO_POST_PACKER,
		.may_not_compress = true,
	};

	do {
		status = get_data_vio_compression_status(data_vio);
	} while ((status.stage != DATA_VIO_POST_PACKER) &&
		 !set_data_vio_compression_status(data_vio, status, new_status));

	/* Write the data from the data block buffer. */
	result = vio_reset_bio(&data_vio->vio, data_vio->vio.data,
			       write_bio_finished, REQ_OP_WRITE,
			       data_vio->allocation.pbn);
	if (result != VDO_SUCCESS) {
		continue_data_vio_with_error(data_vio, result);
		return;
	}

	data_vio->last_async_operation = VIO_ASYNC_OP_WRITE_DATA_VIO;
	vdo_submit_data_vio(data_vio);
}

/**
 * acknowledge_write_callback() - Acknowledge a write to the requestor.
 *
 * This callback is registered in allocate_block() and continue_write_with_block_map_slot().
 */
static void acknowledge_write_callback(struct vdo_completion *completion)
{
	struct data_vio *data_vio = as_data_vio(completion);
	struct vdo *vdo = completion->vdo;

	VDO_ASSERT_LOG_ONLY((!vdo_uses_bio_ack_queue(vdo) ||
			     (vdo_get_callback_thread_id() == vdo->thread_config.bio_ack_thread)),
			    "%s() called on bio ack queue", __func__);
	VDO_ASSERT_LOG_ONLY(data_vio_has_flush_generation_lock(data_vio),
			    "write VIO to be acknowledged has a flush generation lock");
	acknowledge_data_vio(data_vio);
	if (data_vio->new_mapped.pbn == VDO_ZERO_BLOCK) {
		/* This is a zero write or discard */
		update_metadata_for_data_vio_write(data_vio, NULL);
		return;
	}

	prepare_for_dedupe(data_vio);
}

/**
 * allocate_block() - Attempt to allocate a block in the current allocation zone.
 *
 * This callback is registered in continue_write_with_block_map_slot().
 */
static void allocate_block(struct vdo_completion *completion)
{
	struct data_vio *data_vio = as_data_vio(completion);

	assert_data_vio_in_allocated_zone(data_vio);

	if (!vdo_allocate_block_in_zone(data_vio))
		return;

	completion->error_handler = handle_data_vio_error;
	WRITE_ONCE(data_vio->allocation_succeeded, true);
	data_vio->new_mapped = (struct zoned_pbn) {
		.zone = data_vio->allocation.zone,
		.pbn = data_vio->allocation.pbn,
		.state = VDO_MAPPING_STATE_UNCOMPRESSED,
	};

	if (data_vio->fua ||
	    data_vio->remaining_discard > (u32) (VDO_BLOCK_SIZE - data_vio->offset)) {
		prepare_for_dedupe(data_vio);
		return;
	}

	data_vio->last_async_operation = VIO_ASYNC_OP_ACKNOWLEDGE_WRITE;
	launch_data_vio_on_bio_ack_queue(data_vio, acknowledge_write_callback);
}

/**
 * handle_allocation_error() - Handle an error attempting to allocate a block.
 *
 * This error handler is registered in continue_write_with_block_map_slot().
 */
static void handle_allocation_error(struct vdo_completion *completion)
{
	struct data_vio *data_vio = as_data_vio(completion);

	if (completion->result == VDO_NO_SPACE) {
		/* We failed to get an allocation, but we can try to dedupe. */
		vdo_reset_completion(completion);
		completion->error_handler = handle_data_vio_error;
		prepare_for_dedupe(data_vio);
		return;
	}

	/* We got a "real" error, not just a failure to allocate, so fail the request. */
	handle_data_vio_error(completion);
}

static int assert_is_discard(struct data_vio *data_vio)
{
	int result = VDO_ASSERT(data_vio->is_discard,
				"data_vio with no block map page is a discard");

	return ((result == VDO_SUCCESS) ? result : VDO_READ_ONLY);
}

/**
 * continue_data_vio_with_block_map_slot() - Read the data_vio's mapping from the block map.
 *
 * This callback is registered in launch_read_data_vio().
 */
void continue_data_vio_with_block_map_slot(struct vdo_completion *completion)
{
	struct data_vio *data_vio = as_data_vio(completion);

	assert_data_vio_in_logical_zone(data_vio);
	if (data_vio->read) {
		set_data_vio_logical_callback(data_vio, read_block);
		data_vio->last_async_operation = VIO_ASYNC_OP_GET_MAPPED_BLOCK_FOR_READ;
		vdo_get_mapped_block(data_vio);
		return;
	}

	vdo_acquire_flush_generation_lock(data_vio);

	if (data_vio->tree_lock.tree_slots[0].block_map_slot.pbn == VDO_ZERO_BLOCK) {
		/*
		 * This is a discard for a block on a block map page which has not been allocated, so
		 * there's nothing more we need to do.
		 */
		completion->callback = complete_data_vio;
		continue_data_vio_with_error(data_vio, assert_is_discard(data_vio));
		return;
	}

	/*
	 * We need an allocation if this is neither a full-block discard nor a
	 * full-block zero write.
	 */
	if (!data_vio->is_zero && (!data_vio->is_discard || data_vio->is_partial)) {
		data_vio_allocate_data_block(data_vio, VIO_WRITE_LOCK, allocate_block,
					     handle_allocation_error);
		return;
	}

	/*
	 * We don't need to write any data, so skip allocation and just update the block map and
	 * reference counts (via the journal).
	 */
	data_vio->new_mapped.pbn = VDO_ZERO_BLOCK;
	if (data_vio->is_zero)
		data_vio->new_mapped.state = VDO_MAPPING_STATE_UNCOMPRESSED;

	if (data_vio->remaining_discard > (u32) (VDO_BLOCK_SIZE - data_vio->offset)) {
		/* This is not the final block of a discard so we can't acknowledge it yet. */
		update_metadata_for_data_vio_write(data_vio, NULL);
		return;
	}

	data_vio->last_async_operation = VIO_ASYNC_OP_ACKNOWLEDGE_WRITE;
	launch_data_vio_on_bio_ack_queue(data_vio, acknowledge_write_callback);
}
