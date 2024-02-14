// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "vio.h"

#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/kernel.h>
#include <linux/ratelimit.h>

#include "logger.h"
#include "memory-alloc.h"
#include "permassert.h"

#include "constants.h"
#include "io-submitter.h"
#include "vdo.h"

/* A vio_pool is a collection of preallocated vios. */
struct vio_pool {
	/* The number of objects managed by the pool */
	size_t size;
	/* The list of objects which are available */
	struct list_head available;
	/* The queue of requestors waiting for objects from the pool */
	struct vdo_wait_queue waiting;
	/* The number of objects currently in use */
	size_t busy_count;
	/* The list of objects which are in use */
	struct list_head busy;
	/* The ID of the thread on which this pool may be used */
	thread_id_t thread_id;
	/* The buffer backing the pool's vios */
	char *buffer;
	/* The pool entries */
	struct pooled_vio vios[];
};

physical_block_number_t pbn_from_vio_bio(struct bio *bio)
{
	struct vio *vio = bio->bi_private;
	struct vdo *vdo = vio->completion.vdo;
	physical_block_number_t pbn = bio->bi_iter.bi_sector / VDO_SECTORS_PER_BLOCK;

	return ((pbn == VDO_GEOMETRY_BLOCK_LOCATION) ? pbn : pbn + vdo->geometry.bio_offset);
}

static int create_multi_block_bio(block_count_t size, struct bio **bio_ptr)
{
	struct bio *bio = NULL;
	int result;

	result = vdo_allocate_extended(struct bio, size + 1, struct bio_vec,
				       "bio", &bio);
	if (result != VDO_SUCCESS)
		return result;

	*bio_ptr = bio;
	return VDO_SUCCESS;
}

int vdo_create_bio(struct bio **bio_ptr)
{
	return create_multi_block_bio(1, bio_ptr);
}

void vdo_free_bio(struct bio *bio)
{
	if (bio == NULL)
		return;

	bio_uninit(bio);
	vdo_free(vdo_forget(bio));
}

int allocate_vio_components(struct vdo *vdo, enum vio_type vio_type,
			    enum vio_priority priority, void *parent,
			    unsigned int block_count, char *data, struct vio *vio)
{
	struct bio *bio;
	int result;

	result = VDO_ASSERT(block_count <= MAX_BLOCKS_PER_VIO,
			    "block count %u does not exceed maximum %u", block_count,
			    MAX_BLOCKS_PER_VIO);
	if (result != VDO_SUCCESS)
		return result;

	result = VDO_ASSERT(((vio_type != VIO_TYPE_UNINITIALIZED) && (vio_type != VIO_TYPE_DATA)),
			    "%d is a metadata type", vio_type);
	if (result != VDO_SUCCESS)
		return result;

	result = create_multi_block_bio(block_count, &bio);
	if (result != VDO_SUCCESS)
		return result;

	initialize_vio(vio, bio, block_count, vio_type, priority, vdo);
	vio->completion.parent = parent;
	vio->data = data;
	return VDO_SUCCESS;
}

/**
 * create_multi_block_metadata_vio() - Create a vio.
 * @vdo: The vdo on which the vio will operate.
 * @vio_type: The type of vio to create.
 * @priority: The relative priority to assign to the vio.
 * @parent: The parent of the vio.
 * @block_count: The size of the vio in blocks.
 * @data: The buffer.
 * @vio_ptr: A pointer to hold the new vio.
 *
 * Return: VDO_SUCCESS or an error.
 */
int create_multi_block_metadata_vio(struct vdo *vdo, enum vio_type vio_type,
				    enum vio_priority priority, void *parent,
				    unsigned int block_count, char *data,
				    struct vio **vio_ptr)
{
	struct vio *vio;
	int result;

	BUILD_BUG_ON(sizeof(struct vio) > 256);

	/*
	 * Metadata vios should use direct allocation and not use the buffer pool, which is
	 * reserved for submissions from the linux block layer.
	 */
	result = vdo_allocate(1, struct vio, __func__, &vio);
	if (result != VDO_SUCCESS) {
		vdo_log_error("metadata vio allocation failure %d", result);
		return result;
	}

	result = allocate_vio_components(vdo, vio_type, priority, parent, block_count,
					 data, vio);
	if (result != VDO_SUCCESS) {
		vdo_free(vio);
		return result;
	}

	*vio_ptr  = vio;
	return VDO_SUCCESS;
}

/**
 * free_vio_components() - Free the components of a vio embedded in a larger structure.
 * @vio: The vio to destroy
 */
void free_vio_components(struct vio *vio)
{
	if (vio == NULL)
		return;

	BUG_ON(is_data_vio(vio));
	vdo_free_bio(vdo_forget(vio->bio));
}

/**
 * free_vio() - Destroy a vio.
 * @vio: The vio to destroy.
 */
void free_vio(struct vio *vio)
{
	free_vio_components(vio);
	vdo_free(vio);
}

/* Set bio properties for a VDO read or write. */
void vdo_set_bio_properties(struct bio *bio, struct vio *vio, bio_end_io_t callback,
			    blk_opf_t bi_opf, physical_block_number_t pbn)
{
	struct vdo *vdo = vio->completion.vdo;
	struct device_config *config = vdo->device_config;

	pbn -= vdo->geometry.bio_offset;
	vio->bio_zone = ((pbn / config->thread_counts.bio_rotation_interval) %
			 config->thread_counts.bio_threads);

	bio->bi_private = vio;
	bio->bi_end_io = callback;
	bio->bi_opf = bi_opf;
	bio->bi_iter.bi_sector = pbn * VDO_SECTORS_PER_BLOCK;
}

/*
 * Prepares the bio to perform IO with the specified buffer. May only be used on a VDO-allocated
 * bio, as it assumes the bio wraps a 4k buffer that is 4k aligned, but there does not have to be a
 * vio associated with the bio.
 */
int vio_reset_bio(struct vio *vio, char *data, bio_end_io_t callback,
		  blk_opf_t bi_opf, physical_block_number_t pbn)
{
	int bvec_count, offset, len, i;
	struct bio *bio = vio->bio;

	bio_reset(bio, bio->bi_bdev, bi_opf);
	vdo_set_bio_properties(bio, vio, callback, bi_opf, pbn);
	if (data == NULL)
		return VDO_SUCCESS;

	bio->bi_io_vec = bio->bi_inline_vecs;
	bio->bi_max_vecs = vio->block_count + 1;
	len = VDO_BLOCK_SIZE * vio->block_count;
	offset = offset_in_page(data);
	bvec_count = DIV_ROUND_UP(offset + len, PAGE_SIZE);

	/*
	 * If we knew that data was always on one page, or contiguous pages, we wouldn't need the
	 * loop. But if we're using vmalloc, it's not impossible that the data is in different
	 * pages that can't be merged in bio_add_page...
	 */
	for (i = 0; (i < bvec_count) && (len > 0); i++) {
		struct page *page;
		int bytes_added;
		int bytes = PAGE_SIZE - offset;

		if (bytes > len)
			bytes = len;

		page = is_vmalloc_addr(data) ? vmalloc_to_page(data) : virt_to_page(data);
		bytes_added = bio_add_page(bio, page, bytes, offset);

		if (bytes_added != bytes) {
			return vdo_log_error_strerror(VDO_BIO_CREATION_FAILED,
						      "Could only add %i bytes to bio",
						      bytes_added);
		}

		data += bytes;
		len -= bytes;
		offset = 0;
	}

	return VDO_SUCCESS;
}

/**
 * update_vio_error_stats() - Update per-vio error stats and log the error.
 * @vio: The vio which got an error.
 * @format: The format of the message to log (a printf style format).
 */
void update_vio_error_stats(struct vio *vio, const char *format, ...)
{
	static DEFINE_RATELIMIT_STATE(error_limiter, DEFAULT_RATELIMIT_INTERVAL,
				      DEFAULT_RATELIMIT_BURST);
	va_list args;
	int priority;
	struct vdo *vdo = vio->completion.vdo;

	switch (vio->completion.result) {
	case VDO_READ_ONLY:
		atomic64_inc(&vdo->stats.read_only_error_count);
		return;

	case VDO_NO_SPACE:
		atomic64_inc(&vdo->stats.no_space_error_count);
		priority = VDO_LOG_DEBUG;
		break;

	default:
		priority = VDO_LOG_ERR;
	}

	if (!__ratelimit(&error_limiter))
		return;

	va_start(args, format);
	vdo_vlog_strerror(priority, vio->completion.result, VDO_LOGGING_MODULE_NAME,
			  format, args);
	va_end(args);
}

void vio_record_metadata_io_error(struct vio *vio)
{
	const char *description;
	physical_block_number_t pbn = pbn_from_vio_bio(vio->bio);

	if (bio_op(vio->bio) == REQ_OP_READ) {
		description = "read";
	} else if ((vio->bio->bi_opf & REQ_PREFLUSH) == REQ_PREFLUSH) {
		description = (((vio->bio->bi_opf & REQ_FUA) == REQ_FUA) ?
			       "write+preflush+fua" :
			       "write+preflush");
	} else if ((vio->bio->bi_opf & REQ_FUA) == REQ_FUA) {
		description = "write+fua";
	} else {
		description = "write";
	}

	update_vio_error_stats(vio,
			       "Completing %s vio of type %u for physical block %llu with error",
			       description, vio->type, (unsigned long long) pbn);
}

/**
 * make_vio_pool() - Create a new vio pool.
 * @vdo: The vdo.
 * @pool_size: The number of vios in the pool.
 * @thread_id: The ID of the thread using this pool.
 * @vio_type: The type of vios in the pool.
 * @priority: The priority with which vios from the pool should be enqueued.
 * @context: The context that each entry will have.
 * @pool_ptr: The resulting pool.
 *
 * Return: A success or error code.
 */
int make_vio_pool(struct vdo *vdo, size_t pool_size, thread_id_t thread_id,
		  enum vio_type vio_type, enum vio_priority priority, void *context,
		  struct vio_pool **pool_ptr)
{
	struct vio_pool *pool;
	char *ptr;
	int result;

	result = vdo_allocate_extended(struct vio_pool, pool_size, struct pooled_vio,
				       __func__, &pool);
	if (result != VDO_SUCCESS)
		return result;

	pool->thread_id = thread_id;
	INIT_LIST_HEAD(&pool->available);
	INIT_LIST_HEAD(&pool->busy);

	result = vdo_allocate(pool_size * VDO_BLOCK_SIZE, char,
			      "VIO pool buffer", &pool->buffer);
	if (result != VDO_SUCCESS) {
		free_vio_pool(pool);
		return result;
	}

	ptr = pool->buffer;
	for (pool->size = 0; pool->size < pool_size; pool->size++, ptr += VDO_BLOCK_SIZE) {
		struct pooled_vio *pooled = &pool->vios[pool->size];

		result = allocate_vio_components(vdo, vio_type, priority, NULL, 1, ptr,
						 &pooled->vio);
		if (result != VDO_SUCCESS) {
			free_vio_pool(pool);
			return result;
		}

		pooled->context = context;
		list_add_tail(&pooled->pool_entry, &pool->available);
	}

	*pool_ptr = pool;
	return VDO_SUCCESS;
}

/**
 * free_vio_pool() - Destroy a vio pool.
 * @pool: The pool to free.
 */
void free_vio_pool(struct vio_pool *pool)
{
	struct pooled_vio *pooled, *tmp;

	if (pool == NULL)
		return;

	/* Remove all available vios from the object pool. */
	VDO_ASSERT_LOG_ONLY(!vdo_waitq_has_waiters(&pool->waiting),
			    "VIO pool must not have any waiters when being freed");
	VDO_ASSERT_LOG_ONLY((pool->busy_count == 0),
			    "VIO pool must not have %zu busy entries when being freed",
			    pool->busy_count);
	VDO_ASSERT_LOG_ONLY(list_empty(&pool->busy),
			    "VIO pool must not have busy entries when being freed");

	list_for_each_entry_safe(pooled, tmp, &pool->available, pool_entry) {
		list_del(&pooled->pool_entry);
		free_vio_components(&pooled->vio);
		pool->size--;
	}

	VDO_ASSERT_LOG_ONLY(pool->size == 0,
			    "VIO pool must not have missing entries when being freed");

	vdo_free(vdo_forget(pool->buffer));
	vdo_free(pool);
}

/**
 * is_vio_pool_busy() - Check whether an vio pool has outstanding entries.
 *
 * Return: true if the pool is busy.
 */
bool is_vio_pool_busy(struct vio_pool *pool)
{
	return (pool->busy_count != 0);
}

/**
 * acquire_vio_from_pool() - Acquire a vio and buffer from the pool (asynchronous).
 * @pool: The vio pool.
 * @waiter: Object that is requesting a vio.
 */
void acquire_vio_from_pool(struct vio_pool *pool, struct vdo_waiter *waiter)
{
	struct pooled_vio *pooled;

	VDO_ASSERT_LOG_ONLY((pool->thread_id == vdo_get_callback_thread_id()),
			    "acquire from active vio_pool called from correct thread");

	if (list_empty(&pool->available)) {
		vdo_waitq_enqueue_waiter(&pool->waiting, waiter);
		return;
	}

	pooled = list_first_entry(&pool->available, struct pooled_vio, pool_entry);
	pool->busy_count++;
	list_move_tail(&pooled->pool_entry, &pool->busy);
	(*waiter->callback)(waiter, pooled);
}

/**
 * return_vio_to_pool() - Return a vio to the pool
 * @pool: The vio pool.
 * @vio: The pooled vio to return.
 */
void return_vio_to_pool(struct vio_pool *pool, struct pooled_vio *vio)
{
	VDO_ASSERT_LOG_ONLY((pool->thread_id == vdo_get_callback_thread_id()),
			    "vio pool entry returned on same thread as it was acquired");

	vio->vio.completion.error_handler = NULL;
	vio->vio.completion.parent = NULL;
	if (vdo_waitq_has_waiters(&pool->waiting)) {
		vdo_waitq_notify_next_waiter(&pool->waiting, NULL, vio);
		return;
	}

	list_move_tail(&vio->pool_entry, &pool->available);
	--pool->busy_count;
}

/*
 * Various counting functions for statistics.
 * These are used for bios coming into VDO, as well as bios generated by VDO.
 */
void vdo_count_bios(struct atomic_bio_stats *bio_stats, struct bio *bio)
{
	if (((bio->bi_opf & REQ_PREFLUSH) != 0) && (bio->bi_iter.bi_size == 0)) {
		atomic64_inc(&bio_stats->empty_flush);
		atomic64_inc(&bio_stats->flush);
		return;
	}

	switch (bio_op(bio)) {
	case REQ_OP_WRITE:
		atomic64_inc(&bio_stats->write);
		break;
	case REQ_OP_READ:
		atomic64_inc(&bio_stats->read);
		break;
	case REQ_OP_DISCARD:
		atomic64_inc(&bio_stats->discard);
		break;
		/*
		 * All other operations are filtered out in dmvdo.c, or not created by VDO, so
		 * shouldn't exist.
		 */
	default:
		VDO_ASSERT_LOG_ONLY(0, "Bio operation %d not a write, read, discard, or empty flush",
				    bio_op(bio));
	}

	if ((bio->bi_opf & REQ_PREFLUSH) != 0)
		atomic64_inc(&bio_stats->flush);
	if (bio->bi_opf & REQ_FUA)
		atomic64_inc(&bio_stats->fua);
}

static void count_all_bios_completed(struct vio *vio, struct bio *bio)
{
	struct atomic_statistics *stats = &vio->completion.vdo->stats;

	if (is_data_vio(vio)) {
		vdo_count_bios(&stats->bios_out_completed, bio);
		return;
	}

	vdo_count_bios(&stats->bios_meta_completed, bio);
	if (vio->type == VIO_TYPE_RECOVERY_JOURNAL)
		vdo_count_bios(&stats->bios_journal_completed, bio);
	else if (vio->type == VIO_TYPE_BLOCK_MAP)
		vdo_count_bios(&stats->bios_page_cache_completed, bio);
}

void vdo_count_completed_bios(struct bio *bio)
{
	struct vio *vio = (struct vio *) bio->bi_private;

	atomic64_inc(&vio->completion.vdo->stats.bios_completed);
	count_all_bios_completed(vio, bio);
}
