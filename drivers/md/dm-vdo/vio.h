/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef VIO_H
#define VIO_H

#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/list.h>

#include "completion.h"
#include "constants.h"
#include "types.h"
#include "vdo.h"

enum {
	MAX_BLOCKS_PER_VIO = (BIO_MAX_VECS << PAGE_SHIFT) / VDO_BLOCK_SIZE,
};

struct pooled_vio {
	/* The underlying vio */
	struct vio vio;
	/* The list entry for chaining pooled vios together */
	struct list_head list_entry;
	/* The context set by the pool */
	void *context;
	/* The list entry used by the pool */
	struct list_head pool_entry;
};

/**
 * as_vio() - Convert a generic vdo_completion to a vio.
 * @completion: The completion to convert.
 *
 * Return: The completion as a vio.
 */
static inline struct vio *as_vio(struct vdo_completion *completion)
{
	vdo_assert_completion_type(completion, VIO_COMPLETION);
	return container_of(completion, struct vio, completion);
}

/**
 * get_vio_bio_zone_thread_id() - Get the thread id of the bio zone in which a vio should submit
 *                                its I/O.
 * @vio: The vio.
 *
 * Return: The id of the bio zone thread the vio should use.
 */
static inline thread_id_t __must_check get_vio_bio_zone_thread_id(struct vio *vio)
{
	return vio->completion.vdo->thread_config.bio_threads[vio->bio_zone];
}

physical_block_number_t __must_check pbn_from_vio_bio(struct bio *bio);

/**
 * assert_vio_in_bio_zone() - Check that a vio is running on the correct thread for its bio zone.
 * @vio: The vio to check.
 */
static inline void assert_vio_in_bio_zone(struct vio *vio)
{
	thread_id_t expected = get_vio_bio_zone_thread_id(vio);
	thread_id_t thread_id = vdo_get_callback_thread_id();

	VDO_ASSERT_LOG_ONLY((expected == thread_id),
			    "vio I/O for physical block %llu on thread %u, should be on bio zone thread %u",
			    (unsigned long long) pbn_from_vio_bio(vio->bio), thread_id,
			    expected);
}

int vdo_create_bio(struct bio **bio_ptr);
void vdo_free_bio(struct bio *bio);
int allocate_vio_components(struct vdo *vdo, enum vio_type vio_type,
			    enum vio_priority priority, void *parent,
			    unsigned int block_count, char *data, struct vio *vio);
int __must_check create_multi_block_metadata_vio(struct vdo *vdo, enum vio_type vio_type,
						 enum vio_priority priority,
						 void *parent, unsigned int block_count,
						 char *data, struct vio **vio_ptr);

static inline int __must_check create_metadata_vio(struct vdo *vdo, enum vio_type vio_type,
						   enum vio_priority priority,
						   void *parent, char *data,
						   struct vio **vio_ptr)
{
	return create_multi_block_metadata_vio(vdo, vio_type, priority, parent, 1, data,
					       vio_ptr);
}

void free_vio_components(struct vio *vio);
void free_vio(struct vio *vio);

/**
 * initialize_vio() - Initialize a vio.
 * @vio: The vio to initialize.
 * @bio: The bio this vio should use for its I/O.
 * @block_count: The size of this vio in vdo blocks.
 * @vio_type: The vio type.
 * @priority: The relative priority of the vio.
 * @vdo: The vdo for this vio.
 */
static inline void initialize_vio(struct vio *vio, struct bio *bio,
				  unsigned int block_count, enum vio_type vio_type,
				  enum vio_priority priority, struct vdo *vdo)
{
	/* data_vio's may not span multiple blocks */
	BUG_ON((vio_type == VIO_TYPE_DATA) && (block_count != 1));

	vio->bio = bio;
	vio->block_count = block_count;
	vio->type = vio_type;
	vio->priority = priority;
	vdo_initialize_completion(&vio->completion, vdo, VIO_COMPLETION);
}

void vdo_set_bio_properties(struct bio *bio, struct vio *vio, bio_end_io_t callback,
			    blk_opf_t bi_opf, physical_block_number_t pbn);

int vio_reset_bio(struct vio *vio, char *data, bio_end_io_t callback,
		  blk_opf_t bi_opf, physical_block_number_t pbn);

void update_vio_error_stats(struct vio *vio, const char *format, ...)
	__printf(2, 3);

/**
 * is_data_vio() - Check whether a vio is servicing an external data request.
 * @vio: The vio to check.
 */
static inline bool is_data_vio(struct vio *vio)
{
	return (vio->type == VIO_TYPE_DATA);
}

/**
 * get_metadata_priority() - Convert a vio's priority to a work item priority.
 * @vio: The vio.
 *
 * Return: The priority with which to submit the vio's bio.
 */
static inline enum vdo_completion_priority get_metadata_priority(struct vio *vio)
{
	return ((vio->priority == VIO_PRIORITY_HIGH) ?
		BIO_Q_HIGH_PRIORITY :
		BIO_Q_METADATA_PRIORITY);
}

/**
 * continue_vio() - Enqueue a vio to run its next callback.
 * @vio: The vio to continue.
 *
 * Return: The result of the current operation.
 */
static inline void continue_vio(struct vio *vio, int result)
{
	if (unlikely(result != VDO_SUCCESS))
		vdo_set_completion_result(&vio->completion, result);

	vdo_enqueue_completion(&vio->completion, VDO_WORK_Q_DEFAULT_PRIORITY);
}

void vdo_count_bios(struct atomic_bio_stats *bio_stats, struct bio *bio);
void vdo_count_completed_bios(struct bio *bio);

/**
 * continue_vio_after_io() - Continue a vio now that its I/O has returned.
 */
static inline void continue_vio_after_io(struct vio *vio, vdo_action_fn callback,
					 thread_id_t thread)
{
	vdo_count_completed_bios(vio->bio);
	vdo_set_completion_callback(&vio->completion, callback, thread);
	continue_vio(vio, blk_status_to_errno(vio->bio->bi_status));
}

void vio_record_metadata_io_error(struct vio *vio);

/* A vio_pool is a collection of preallocated vios used to write arbitrary metadata blocks. */

static inline struct pooled_vio *vio_as_pooled_vio(struct vio *vio)
{
	return container_of(vio, struct pooled_vio, vio);
}

struct vio_pool;

int __must_check make_vio_pool(struct vdo *vdo, size_t pool_size, thread_id_t thread_id,
			       enum vio_type vio_type, enum vio_priority priority,
			       void *context, struct vio_pool **pool_ptr);
void free_vio_pool(struct vio_pool *pool);
bool __must_check is_vio_pool_busy(struct vio_pool *pool);
void acquire_vio_from_pool(struct vio_pool *pool, struct vdo_waiter *waiter);
void return_vio_to_pool(struct vio_pool *pool, struct pooled_vio *vio);

#endif /* VIO_H */
