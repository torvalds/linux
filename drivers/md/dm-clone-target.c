// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019 Arrikto, Inc. All Rights Reserved.
 */

#include <linux/mm.h>
#include <linux/bio.h>
#include <linux/err.h>
#include <linux/hash.h>
#include <linux/list.h>
#include <linux/log2.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/dm-io.h>
#include <linux/mutex.h>
#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/blkdev.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/mempool.h>
#include <linux/spinlock.h>
#include <linux/blk_types.h>
#include <linux/dm-kcopyd.h>
#include <linux/workqueue.h>
#include <linux/backing-dev.h>
#include <linux/device-mapper.h>

#include "dm.h"
#include "dm-clone-metadata.h"

#define DM_MSG_PREFIX "clone"

/*
 * Minimum and maximum allowed region sizes
 */
#define MIN_REGION_SIZE (1 << 3)  /* 4KB */
#define MAX_REGION_SIZE (1 << 21) /* 1GB */

#define MIN_HYDRATIONS 256 /* Size of hydration mempool */
#define DEFAULT_HYDRATION_THRESHOLD 1 /* 1 region */
#define DEFAULT_HYDRATION_BATCH_SIZE 1 /* Hydrate in batches of 1 region */

#define COMMIT_PERIOD HZ /* 1 sec */

/*
 * Hydration hash table size: 1 << HASH_TABLE_BITS
 */
#define HASH_TABLE_BITS 15

DECLARE_DM_KCOPYD_THROTTLE_WITH_MODULE_PARM(clone_hydration_throttle,
	"A percentage of time allocated for hydrating regions");

/* Slab cache for struct dm_clone_region_hydration */
static struct kmem_cache *_hydration_cache;

/* dm-clone metadata modes */
enum clone_metadata_mode {
	CM_WRITE,		/* metadata may be changed */
	CM_READ_ONLY,		/* metadata may not be changed */
	CM_FAIL,		/* all metadata I/O fails */
};

struct hash_table_bucket;

struct clone {
	struct dm_target *ti;

	struct dm_dev *metadata_dev;
	struct dm_dev *dest_dev;
	struct dm_dev *source_dev;

	unsigned long nr_regions;
	sector_t region_size;
	unsigned int region_shift;

	/*
	 * A metadata commit and the actions taken in case it fails should run
	 * as a single atomic step.
	 */
	struct mutex commit_lock;

	struct dm_clone_metadata *cmd;

	/* Region hydration hash table */
	struct hash_table_bucket *ht;

	atomic_t ios_in_flight;

	wait_queue_head_t hydration_stopped;

	mempool_t hydration_pool;

	unsigned long last_commit_jiffies;

	/*
	 * We defer incoming WRITE bios for regions that are not hydrated,
	 * until after these regions have been hydrated.
	 *
	 * Also, we defer REQ_FUA and REQ_PREFLUSH bios, until after the
	 * metadata have been committed.
	 */
	spinlock_t lock;
	struct bio_list deferred_bios;
	struct bio_list deferred_discard_bios;
	struct bio_list deferred_flush_bios;
	struct bio_list deferred_flush_completions;

	/* Maximum number of regions being copied during background hydration. */
	unsigned int hydration_threshold;

	/* Number of regions to batch together during background hydration. */
	unsigned int hydration_batch_size;

	/* Which region to hydrate next */
	unsigned long hydration_offset;

	atomic_t hydrations_in_flight;

	/*
	 * Save a copy of the table line rather than reconstructing it for the
	 * status.
	 */
	unsigned int nr_ctr_args;
	const char **ctr_args;

	struct workqueue_struct *wq;
	struct work_struct worker;
	struct delayed_work waker;

	struct dm_kcopyd_client *kcopyd_client;

	enum clone_metadata_mode mode;
	unsigned long flags;
};

/*
 * dm-clone flags
 */
#define DM_CLONE_DISCARD_PASSDOWN 0
#define DM_CLONE_HYDRATION_ENABLED 1
#define DM_CLONE_HYDRATION_SUSPENDED 2

/*---------------------------------------------------------------------------*/

/*
 * Metadata failure handling.
 */
static enum clone_metadata_mode get_clone_mode(struct clone *clone)
{
	return READ_ONCE(clone->mode);
}

static const char *clone_device_name(struct clone *clone)
{
	return dm_table_device_name(clone->ti->table);
}

static void __set_clone_mode(struct clone *clone, enum clone_metadata_mode new_mode)
{
	const char *descs[] = {
		"read-write",
		"read-only",
		"fail"
	};

	enum clone_metadata_mode old_mode = get_clone_mode(clone);

	/* Never move out of fail mode */
	if (old_mode == CM_FAIL)
		new_mode = CM_FAIL;

	switch (new_mode) {
	case CM_FAIL:
	case CM_READ_ONLY:
		dm_clone_metadata_set_read_only(clone->cmd);
		break;

	case CM_WRITE:
		dm_clone_metadata_set_read_write(clone->cmd);
		break;
	}

	WRITE_ONCE(clone->mode, new_mode);

	if (new_mode != old_mode) {
		dm_table_event(clone->ti->table);
		DMINFO("%s: Switching to %s mode", clone_device_name(clone),
		       descs[(int)new_mode]);
	}
}

static void __abort_transaction(struct clone *clone)
{
	const char *dev_name = clone_device_name(clone);

	if (get_clone_mode(clone) >= CM_READ_ONLY)
		return;

	DMERR("%s: Aborting current metadata transaction", dev_name);
	if (dm_clone_metadata_abort(clone->cmd)) {
		DMERR("%s: Failed to abort metadata transaction", dev_name);
		__set_clone_mode(clone, CM_FAIL);
	}
}

static void __reload_in_core_bitset(struct clone *clone)
{
	const char *dev_name = clone_device_name(clone);

	if (get_clone_mode(clone) == CM_FAIL)
		return;

	/* Reload the on-disk bitset */
	DMINFO("%s: Reloading on-disk bitmap", dev_name);
	if (dm_clone_reload_in_core_bitset(clone->cmd)) {
		DMERR("%s: Failed to reload on-disk bitmap", dev_name);
		__set_clone_mode(clone, CM_FAIL);
	}
}

static void __metadata_operation_failed(struct clone *clone, const char *op, int r)
{
	DMERR("%s: Metadata operation `%s' failed: error = %d",
	      clone_device_name(clone), op, r);

	__abort_transaction(clone);
	__set_clone_mode(clone, CM_READ_ONLY);

	/*
	 * dm_clone_reload_in_core_bitset() may run concurrently with either
	 * dm_clone_set_region_hydrated() or dm_clone_cond_set_range(), but
	 * it's safe as we have already set the metadata to read-only mode.
	 */
	__reload_in_core_bitset(clone);
}

/*---------------------------------------------------------------------------*/

/* Wake up anyone waiting for region hydrations to stop */
static inline void wakeup_hydration_waiters(struct clone *clone)
{
	wake_up_all(&clone->hydration_stopped);
}

static inline void wake_worker(struct clone *clone)
{
	queue_work(clone->wq, &clone->worker);
}

/*---------------------------------------------------------------------------*/

/*
 * bio helper functions.
 */
static inline void remap_to_source(struct clone *clone, struct bio *bio)
{
	bio_set_dev(bio, clone->source_dev->bdev);
}

static inline void remap_to_dest(struct clone *clone, struct bio *bio)
{
	bio_set_dev(bio, clone->dest_dev->bdev);
}

static bool bio_triggers_commit(struct clone *clone, struct bio *bio)
{
	return op_is_flush(bio->bi_opf) &&
		dm_clone_changed_this_transaction(clone->cmd);
}

/* Get the address of the region in sectors */
static inline sector_t region_to_sector(struct clone *clone, unsigned long region_nr)
{
	return ((sector_t)region_nr << clone->region_shift);
}

/* Get the region number of the bio */
static inline unsigned long bio_to_region(struct clone *clone, struct bio *bio)
{
	return (bio->bi_iter.bi_sector >> clone->region_shift);
}

/* Get the region range covered by the bio */
static void bio_region_range(struct clone *clone, struct bio *bio,
			     unsigned long *rs, unsigned long *nr_regions)
{
	unsigned long end;

	*rs = dm_sector_div_up(bio->bi_iter.bi_sector, clone->region_size);
	end = bio_end_sector(bio) >> clone->region_shift;

	if (*rs >= end)
		*nr_regions = 0;
	else
		*nr_regions = end - *rs;
}

/* Check whether a bio overwrites a region */
static inline bool is_overwrite_bio(struct clone *clone, struct bio *bio)
{
	return (bio_data_dir(bio) == WRITE && bio_sectors(bio) == clone->region_size);
}

static void fail_bios(struct bio_list *bios, blk_status_t status)
{
	struct bio *bio;

	while ((bio = bio_list_pop(bios))) {
		bio->bi_status = status;
		bio_endio(bio);
	}
}

static void submit_bios(struct bio_list *bios)
{
	struct bio *bio;
	struct blk_plug plug;

	blk_start_plug(&plug);

	while ((bio = bio_list_pop(bios)))
		submit_bio_noacct(bio);

	blk_finish_plug(&plug);
}

/*
 * Submit bio to the underlying device.
 *
 * If the bio triggers a commit, delay it, until after the metadata have been
 * committed.
 *
 * NOTE: The bio remapping must be performed by the caller.
 */
static void issue_bio(struct clone *clone, struct bio *bio)
{
	if (!bio_triggers_commit(clone, bio)) {
		submit_bio_noacct(bio);
		return;
	}

	/*
	 * If the metadata mode is RO or FAIL we won't be able to commit the
	 * metadata, so we complete the bio with an error.
	 */
	if (unlikely(get_clone_mode(clone) >= CM_READ_ONLY)) {
		bio_io_error(bio);
		return;
	}

	/*
	 * Batch together any bios that trigger commits and then issue a single
	 * commit for them in process_deferred_flush_bios().
	 */
	spin_lock_irq(&clone->lock);
	bio_list_add(&clone->deferred_flush_bios, bio);
	spin_unlock_irq(&clone->lock);

	wake_worker(clone);
}

/*
 * Remap bio to the destination device and submit it.
 *
 * If the bio triggers a commit, delay it, until after the metadata have been
 * committed.
 */
static void remap_and_issue(struct clone *clone, struct bio *bio)
{
	remap_to_dest(clone, bio);
	issue_bio(clone, bio);
}

/*
 * Issue bios that have been deferred until after their region has finished
 * hydrating.
 *
 * We delegate the bio submission to the worker thread, so this is safe to call
 * from interrupt context.
 */
static void issue_deferred_bios(struct clone *clone, struct bio_list *bios)
{
	struct bio *bio;
	unsigned long flags;
	struct bio_list flush_bios = BIO_EMPTY_LIST;
	struct bio_list normal_bios = BIO_EMPTY_LIST;

	if (bio_list_empty(bios))
		return;

	while ((bio = bio_list_pop(bios))) {
		if (bio_triggers_commit(clone, bio))
			bio_list_add(&flush_bios, bio);
		else
			bio_list_add(&normal_bios, bio);
	}

	spin_lock_irqsave(&clone->lock, flags);
	bio_list_merge(&clone->deferred_bios, &normal_bios);
	bio_list_merge(&clone->deferred_flush_bios, &flush_bios);
	spin_unlock_irqrestore(&clone->lock, flags);

	wake_worker(clone);
}

static void complete_overwrite_bio(struct clone *clone, struct bio *bio)
{
	unsigned long flags;

	/*
	 * If the bio has the REQ_FUA flag set we must commit the metadata
	 * before signaling its completion.
	 *
	 * complete_overwrite_bio() is only called by hydration_complete(),
	 * after having successfully updated the metadata. This means we don't
	 * need to call dm_clone_changed_this_transaction() to check if the
	 * metadata has changed and thus we can avoid taking the metadata spin
	 * lock.
	 */
	if (!(bio->bi_opf & REQ_FUA)) {
		bio_endio(bio);
		return;
	}

	/*
	 * If the metadata mode is RO or FAIL we won't be able to commit the
	 * metadata, so we complete the bio with an error.
	 */
	if (unlikely(get_clone_mode(clone) >= CM_READ_ONLY)) {
		bio_io_error(bio);
		return;
	}

	/*
	 * Batch together any bios that trigger commits and then issue a single
	 * commit for them in process_deferred_flush_bios().
	 */
	spin_lock_irqsave(&clone->lock, flags);
	bio_list_add(&clone->deferred_flush_completions, bio);
	spin_unlock_irqrestore(&clone->lock, flags);

	wake_worker(clone);
}

static void trim_bio(struct bio *bio, sector_t sector, unsigned int len)
{
	bio->bi_iter.bi_sector = sector;
	bio->bi_iter.bi_size = to_bytes(len);
}

static void complete_discard_bio(struct clone *clone, struct bio *bio, bool success)
{
	unsigned long rs, nr_regions;

	/*
	 * If the destination device supports discards, remap and trim the
	 * discard bio and pass it down. Otherwise complete the bio
	 * immediately.
	 */
	if (test_bit(DM_CLONE_DISCARD_PASSDOWN, &clone->flags) && success) {
		remap_to_dest(clone, bio);
		bio_region_range(clone, bio, &rs, &nr_regions);
		trim_bio(bio, region_to_sector(clone, rs),
			 nr_regions << clone->region_shift);
		submit_bio_noacct(bio);
	} else
		bio_endio(bio);
}

static void process_discard_bio(struct clone *clone, struct bio *bio)
{
	unsigned long rs, nr_regions;

	bio_region_range(clone, bio, &rs, &nr_regions);
	if (!nr_regions) {
		bio_endio(bio);
		return;
	}

	if (WARN_ON(rs >= clone->nr_regions || (rs + nr_regions) < rs ||
		    (rs + nr_regions) > clone->nr_regions)) {
		DMERR("%s: Invalid range (%lu + %lu, total regions %lu) for discard (%llu + %u)",
		      clone_device_name(clone), rs, nr_regions,
		      clone->nr_regions,
		      (unsigned long long)bio->bi_iter.bi_sector,
		      bio_sectors(bio));
		bio_endio(bio);
		return;
	}

	/*
	 * The covered regions are already hydrated so we just need to pass
	 * down the discard.
	 */
	if (dm_clone_is_range_hydrated(clone->cmd, rs, nr_regions)) {
		complete_discard_bio(clone, bio, true);
		return;
	}

	/*
	 * If the metadata mode is RO or FAIL we won't be able to update the
	 * metadata for the regions covered by the discard so we just ignore
	 * it.
	 */
	if (unlikely(get_clone_mode(clone) >= CM_READ_ONLY)) {
		bio_endio(bio);
		return;
	}

	/*
	 * Defer discard processing.
	 */
	spin_lock_irq(&clone->lock);
	bio_list_add(&clone->deferred_discard_bios, bio);
	spin_unlock_irq(&clone->lock);

	wake_worker(clone);
}

/*---------------------------------------------------------------------------*/

/*
 * dm-clone region hydrations.
 */
struct dm_clone_region_hydration {
	struct clone *clone;
	unsigned long region_nr;

	struct bio *overwrite_bio;
	bio_end_io_t *overwrite_bio_end_io;

	struct bio_list deferred_bios;

	blk_status_t status;

	/* Used by hydration batching */
	struct list_head list;

	/* Used by hydration hash table */
	struct hlist_node h;
};

/*
 * Hydration hash table implementation.
 *
 * Ideally we would like to use list_bl, which uses bit spin locks and employs
 * the least significant bit of the list head to lock the corresponding bucket,
 * reducing the memory overhead for the locks. But, currently, list_bl and bit
 * spin locks don't support IRQ safe versions. Since we have to take the lock
 * in both process and interrupt context, we must fall back to using regular
 * spin locks; one per hash table bucket.
 */
struct hash_table_bucket {
	struct hlist_head head;

	/* Spinlock protecting the bucket */
	spinlock_t lock;
};

#define bucket_lock_irqsave(bucket, flags) \
	spin_lock_irqsave(&(bucket)->lock, flags)

#define bucket_unlock_irqrestore(bucket, flags) \
	spin_unlock_irqrestore(&(bucket)->lock, flags)

#define bucket_lock_irq(bucket) \
	spin_lock_irq(&(bucket)->lock)

#define bucket_unlock_irq(bucket) \
	spin_unlock_irq(&(bucket)->lock)

static int hash_table_init(struct clone *clone)
{
	unsigned int i, sz;
	struct hash_table_bucket *bucket;

	sz = 1 << HASH_TABLE_BITS;

	clone->ht = kvmalloc(sz * sizeof(struct hash_table_bucket), GFP_KERNEL);
	if (!clone->ht)
		return -ENOMEM;

	for (i = 0; i < sz; i++) {
		bucket = clone->ht + i;

		INIT_HLIST_HEAD(&bucket->head);
		spin_lock_init(&bucket->lock);
	}

	return 0;
}

static void hash_table_exit(struct clone *clone)
{
	kvfree(clone->ht);
}

static struct hash_table_bucket *get_hash_table_bucket(struct clone *clone,
						       unsigned long region_nr)
{
	return &clone->ht[hash_long(region_nr, HASH_TABLE_BITS)];
}

/*
 * Search hash table for a hydration with hd->region_nr == region_nr
 *
 * NOTE: Must be called with the bucket lock held
 */
static struct dm_clone_region_hydration *__hash_find(struct hash_table_bucket *bucket,
						     unsigned long region_nr)
{
	struct dm_clone_region_hydration *hd;

	hlist_for_each_entry(hd, &bucket->head, h) {
		if (hd->region_nr == region_nr)
			return hd;
	}

	return NULL;
}

/*
 * Insert a hydration into the hash table.
 *
 * NOTE: Must be called with the bucket lock held.
 */
static inline void __insert_region_hydration(struct hash_table_bucket *bucket,
					     struct dm_clone_region_hydration *hd)
{
	hlist_add_head(&hd->h, &bucket->head);
}

/*
 * This function inserts a hydration into the hash table, unless someone else
 * managed to insert a hydration for the same region first. In the latter case
 * it returns the existing hydration descriptor for this region.
 *
 * NOTE: Must be called with the hydration hash table lock held.
 */
static struct dm_clone_region_hydration *
__find_or_insert_region_hydration(struct hash_table_bucket *bucket,
				  struct dm_clone_region_hydration *hd)
{
	struct dm_clone_region_hydration *hd2;

	hd2 = __hash_find(bucket, hd->region_nr);
	if (hd2)
		return hd2;

	__insert_region_hydration(bucket, hd);

	return hd;
}

/*---------------------------------------------------------------------------*/

/* Allocate a hydration */
static struct dm_clone_region_hydration *alloc_hydration(struct clone *clone)
{
	struct dm_clone_region_hydration *hd;

	/*
	 * Allocate a hydration from the hydration mempool.
	 * This might block but it can't fail.
	 */
	hd = mempool_alloc(&clone->hydration_pool, GFP_NOIO);
	hd->clone = clone;

	return hd;
}

static inline void free_hydration(struct dm_clone_region_hydration *hd)
{
	mempool_free(hd, &hd->clone->hydration_pool);
}

/* Initialize a hydration */
static void hydration_init(struct dm_clone_region_hydration *hd, unsigned long region_nr)
{
	hd->region_nr = region_nr;
	hd->overwrite_bio = NULL;
	bio_list_init(&hd->deferred_bios);
	hd->status = 0;

	INIT_LIST_HEAD(&hd->list);
	INIT_HLIST_NODE(&hd->h);
}

/*---------------------------------------------------------------------------*/

/*
 * Update dm-clone's metadata after a region has finished hydrating and remove
 * hydration from the hash table.
 */
static int hydration_update_metadata(struct dm_clone_region_hydration *hd)
{
	int r = 0;
	unsigned long flags;
	struct hash_table_bucket *bucket;
	struct clone *clone = hd->clone;

	if (unlikely(get_clone_mode(clone) >= CM_READ_ONLY))
		r = -EPERM;

	/* Update the metadata */
	if (likely(!r) && hd->status == BLK_STS_OK)
		r = dm_clone_set_region_hydrated(clone->cmd, hd->region_nr);

	bucket = get_hash_table_bucket(clone, hd->region_nr);

	/* Remove hydration from hash table */
	bucket_lock_irqsave(bucket, flags);
	hlist_del(&hd->h);
	bucket_unlock_irqrestore(bucket, flags);

	return r;
}

/*
 * Complete a region's hydration:
 *
 *	1. Update dm-clone's metadata.
 *	2. Remove hydration from hash table.
 *	3. Complete overwrite bio.
 *	4. Issue deferred bios.
 *	5. If this was the last hydration, wake up anyone waiting for
 *	   hydrations to finish.
 */
static void hydration_complete(struct dm_clone_region_hydration *hd)
{
	int r;
	blk_status_t status;
	struct clone *clone = hd->clone;

	r = hydration_update_metadata(hd);

	if (hd->status == BLK_STS_OK && likely(!r)) {
		if (hd->overwrite_bio)
			complete_overwrite_bio(clone, hd->overwrite_bio);

		issue_deferred_bios(clone, &hd->deferred_bios);
	} else {
		status = r ? BLK_STS_IOERR : hd->status;

		if (hd->overwrite_bio)
			bio_list_add(&hd->deferred_bios, hd->overwrite_bio);

		fail_bios(&hd->deferred_bios, status);
	}

	free_hydration(hd);

	if (atomic_dec_and_test(&clone->hydrations_in_flight))
		wakeup_hydration_waiters(clone);
}

static void hydration_kcopyd_callback(int read_err, unsigned long write_err, void *context)
{
	blk_status_t status;

	struct dm_clone_region_hydration *tmp, *hd = context;
	struct clone *clone = hd->clone;

	LIST_HEAD(batched_hydrations);

	if (read_err || write_err) {
		DMERR_LIMIT("%s: hydration failed", clone_device_name(clone));
		status = BLK_STS_IOERR;
	} else {
		status = BLK_STS_OK;
	}
	list_splice_tail(&hd->list, &batched_hydrations);

	hd->status = status;
	hydration_complete(hd);

	/* Complete batched hydrations */
	list_for_each_entry_safe(hd, tmp, &batched_hydrations, list) {
		hd->status = status;
		hydration_complete(hd);
	}

	/* Continue background hydration, if there is no I/O in-flight */
	if (test_bit(DM_CLONE_HYDRATION_ENABLED, &clone->flags) &&
	    !atomic_read(&clone->ios_in_flight))
		wake_worker(clone);
}

static void hydration_copy(struct dm_clone_region_hydration *hd, unsigned int nr_regions)
{
	unsigned long region_start, region_end;
	sector_t tail_size, region_size, total_size;
	struct dm_io_region from, to;
	struct clone *clone = hd->clone;

	if (WARN_ON(!nr_regions))
		return;

	region_size = clone->region_size;
	region_start = hd->region_nr;
	region_end = region_start + nr_regions - 1;

	total_size = region_to_sector(clone, nr_regions - 1);

	if (region_end == clone->nr_regions - 1) {
		/*
		 * The last region of the target might be smaller than
		 * region_size.
		 */
		tail_size = clone->ti->len & (region_size - 1);
		if (!tail_size)
			tail_size = region_size;
	} else {
		tail_size = region_size;
	}

	total_size += tail_size;

	from.bdev = clone->source_dev->bdev;
	from.sector = region_to_sector(clone, region_start);
	from.count = total_size;

	to.bdev = clone->dest_dev->bdev;
	to.sector = from.sector;
	to.count = from.count;

	/* Issue copy */
	atomic_add(nr_regions, &clone->hydrations_in_flight);
	dm_kcopyd_copy(clone->kcopyd_client, &from, 1, &to, 0,
		       hydration_kcopyd_callback, hd);
}

static void overwrite_endio(struct bio *bio)
{
	struct dm_clone_region_hydration *hd = bio->bi_private;

	bio->bi_end_io = hd->overwrite_bio_end_io;
	hd->status = bio->bi_status;

	hydration_complete(hd);
}

static void hydration_overwrite(struct dm_clone_region_hydration *hd, struct bio *bio)
{
	/*
	 * We don't need to save and restore bio->bi_private because device
	 * mapper core generates a new bio for us to use, with clean
	 * bi_private.
	 */
	hd->overwrite_bio = bio;
	hd->overwrite_bio_end_io = bio->bi_end_io;

	bio->bi_end_io = overwrite_endio;
	bio->bi_private = hd;

	atomic_inc(&hd->clone->hydrations_in_flight);
	submit_bio_noacct(bio);
}

/*
 * Hydrate bio's region.
 *
 * This function starts the hydration of the bio's region and puts the bio in
 * the list of deferred bios for this region. In case, by the time this
 * function is called, the region has finished hydrating it's submitted to the
 * destination device.
 *
 * NOTE: The bio remapping must be performed by the caller.
 */
static void hydrate_bio_region(struct clone *clone, struct bio *bio)
{
	unsigned long region_nr;
	struct hash_table_bucket *bucket;
	struct dm_clone_region_hydration *hd, *hd2;

	region_nr = bio_to_region(clone, bio);
	bucket = get_hash_table_bucket(clone, region_nr);

	bucket_lock_irq(bucket);

	hd = __hash_find(bucket, region_nr);
	if (hd) {
		/* Someone else is hydrating the region */
		bio_list_add(&hd->deferred_bios, bio);
		bucket_unlock_irq(bucket);
		return;
	}

	if (dm_clone_is_region_hydrated(clone->cmd, region_nr)) {
		/* The region has been hydrated */
		bucket_unlock_irq(bucket);
		issue_bio(clone, bio);
		return;
	}

	/*
	 * We must allocate a hydration descriptor and start the hydration of
	 * the corresponding region.
	 */
	bucket_unlock_irq(bucket);

	hd = alloc_hydration(clone);
	hydration_init(hd, region_nr);

	bucket_lock_irq(bucket);

	/* Check if the region has been hydrated in the meantime. */
	if (dm_clone_is_region_hydrated(clone->cmd, region_nr)) {
		bucket_unlock_irq(bucket);
		free_hydration(hd);
		issue_bio(clone, bio);
		return;
	}

	hd2 = __find_or_insert_region_hydration(bucket, hd);
	if (hd2 != hd) {
		/* Someone else started the region's hydration. */
		bio_list_add(&hd2->deferred_bios, bio);
		bucket_unlock_irq(bucket);
		free_hydration(hd);
		return;
	}

	/*
	 * If the metadata mode is RO or FAIL then there is no point starting a
	 * hydration, since we will not be able to update the metadata when the
	 * hydration finishes.
	 */
	if (unlikely(get_clone_mode(clone) >= CM_READ_ONLY)) {
		hlist_del(&hd->h);
		bucket_unlock_irq(bucket);
		free_hydration(hd);
		bio_io_error(bio);
		return;
	}

	/*
	 * Start region hydration.
	 *
	 * If a bio overwrites a region, i.e., its size is equal to the
	 * region's size, then we don't need to copy the region from the source
	 * to the destination device.
	 */
	if (is_overwrite_bio(clone, bio)) {
		bucket_unlock_irq(bucket);
		hydration_overwrite(hd, bio);
	} else {
		bio_list_add(&hd->deferred_bios, bio);
		bucket_unlock_irq(bucket);
		hydration_copy(hd, 1);
	}
}

/*---------------------------------------------------------------------------*/

/*
 * Background hydrations.
 */

/*
 * Batch region hydrations.
 *
 * To better utilize device bandwidth we batch together the hydration of
 * adjacent regions. This allows us to use small region sizes, e.g., 4KB, which
 * is good for small, random write performance (because of the overwriting of
 * un-hydrated regions) and at the same time issue big copy requests to kcopyd
 * to achieve high hydration bandwidth.
 */
struct batch_info {
	struct dm_clone_region_hydration *head;
	unsigned int nr_batched_regions;
};

static void __batch_hydration(struct batch_info *batch,
			      struct dm_clone_region_hydration *hd)
{
	struct clone *clone = hd->clone;
	unsigned int max_batch_size = READ_ONCE(clone->hydration_batch_size);

	if (batch->head) {
		/* Try to extend the current batch */
		if (batch->nr_batched_regions < max_batch_size &&
		    (batch->head->region_nr + batch->nr_batched_regions) == hd->region_nr) {
			list_add_tail(&hd->list, &batch->head->list);
			batch->nr_batched_regions++;
			hd = NULL;
		}

		/* Check if we should issue the current batch */
		if (batch->nr_batched_regions >= max_batch_size || hd) {
			hydration_copy(batch->head, batch->nr_batched_regions);
			batch->head = NULL;
			batch->nr_batched_regions = 0;
		}
	}

	if (!hd)
		return;

	/* We treat max batch sizes of zero and one equivalently */
	if (max_batch_size <= 1) {
		hydration_copy(hd, 1);
		return;
	}

	/* Start a new batch */
	BUG_ON(!list_empty(&hd->list));
	batch->head = hd;
	batch->nr_batched_regions = 1;
}

static unsigned long __start_next_hydration(struct clone *clone,
					    unsigned long offset,
					    struct batch_info *batch)
{
	struct hash_table_bucket *bucket;
	struct dm_clone_region_hydration *hd;
	unsigned long nr_regions = clone->nr_regions;

	hd = alloc_hydration(clone);

	/* Try to find a region to hydrate. */
	do {
		offset = dm_clone_find_next_unhydrated_region(clone->cmd, offset);
		if (offset == nr_regions)
			break;

		bucket = get_hash_table_bucket(clone, offset);
		bucket_lock_irq(bucket);

		if (!dm_clone_is_region_hydrated(clone->cmd, offset) &&
		    !__hash_find(bucket, offset)) {
			hydration_init(hd, offset);
			__insert_region_hydration(bucket, hd);
			bucket_unlock_irq(bucket);

			/* Batch hydration */
			__batch_hydration(batch, hd);

			return (offset + 1);
		}

		bucket_unlock_irq(bucket);

	} while (++offset < nr_regions);

	if (hd)
		free_hydration(hd);

	return offset;
}

/*
 * This function searches for regions that still reside in the source device
 * and starts their hydration.
 */
static void do_hydration(struct clone *clone)
{
	unsigned int current_volume;
	unsigned long offset, nr_regions = clone->nr_regions;

	struct batch_info batch = {
		.head = NULL,
		.nr_batched_regions = 0,
	};

	if (unlikely(get_clone_mode(clone) >= CM_READ_ONLY))
		return;

	if (dm_clone_is_hydration_done(clone->cmd))
		return;

	/*
	 * Avoid race with device suspension.
	 */
	atomic_inc(&clone->hydrations_in_flight);

	/*
	 * Make sure atomic_inc() is ordered before test_bit(), otherwise we
	 * might race with clone_postsuspend() and start a region hydration
	 * after the target has been suspended.
	 *
	 * This is paired with the smp_mb__after_atomic() in
	 * clone_postsuspend().
	 */
	smp_mb__after_atomic();

	offset = clone->hydration_offset;
	while (likely(!test_bit(DM_CLONE_HYDRATION_SUSPENDED, &clone->flags)) &&
	       !atomic_read(&clone->ios_in_flight) &&
	       test_bit(DM_CLONE_HYDRATION_ENABLED, &clone->flags) &&
	       offset < nr_regions) {
		current_volume = atomic_read(&clone->hydrations_in_flight);
		current_volume += batch.nr_batched_regions;

		if (current_volume > READ_ONCE(clone->hydration_threshold))
			break;

		offset = __start_next_hydration(clone, offset, &batch);
	}

	if (batch.head)
		hydration_copy(batch.head, batch.nr_batched_regions);

	if (offset >= nr_regions)
		offset = 0;

	clone->hydration_offset = offset;

	if (atomic_dec_and_test(&clone->hydrations_in_flight))
		wakeup_hydration_waiters(clone);
}

/*---------------------------------------------------------------------------*/

static bool need_commit_due_to_time(struct clone *clone)
{
	return !time_in_range(jiffies, clone->last_commit_jiffies,
			      clone->last_commit_jiffies + COMMIT_PERIOD);
}

/*
 * A non-zero return indicates read-only or fail mode.
 */
static int commit_metadata(struct clone *clone, bool *dest_dev_flushed)
{
	int r = 0;

	if (dest_dev_flushed)
		*dest_dev_flushed = false;

	mutex_lock(&clone->commit_lock);

	if (!dm_clone_changed_this_transaction(clone->cmd))
		goto out;

	if (unlikely(get_clone_mode(clone) >= CM_READ_ONLY)) {
		r = -EPERM;
		goto out;
	}

	r = dm_clone_metadata_pre_commit(clone->cmd);
	if (unlikely(r)) {
		__metadata_operation_failed(clone, "dm_clone_metadata_pre_commit", r);
		goto out;
	}

	r = blkdev_issue_flush(clone->dest_dev->bdev);
	if (unlikely(r)) {
		__metadata_operation_failed(clone, "flush destination device", r);
		goto out;
	}

	if (dest_dev_flushed)
		*dest_dev_flushed = true;

	r = dm_clone_metadata_commit(clone->cmd);
	if (unlikely(r)) {
		__metadata_operation_failed(clone, "dm_clone_metadata_commit", r);
		goto out;
	}

	if (dm_clone_is_hydration_done(clone->cmd))
		dm_table_event(clone->ti->table);
out:
	mutex_unlock(&clone->commit_lock);

	return r;
}

static void process_deferred_discards(struct clone *clone)
{
	int r = -EPERM;
	struct bio *bio;
	struct blk_plug plug;
	unsigned long rs, nr_regions;
	struct bio_list discards = BIO_EMPTY_LIST;

	spin_lock_irq(&clone->lock);
	bio_list_merge(&discards, &clone->deferred_discard_bios);
	bio_list_init(&clone->deferred_discard_bios);
	spin_unlock_irq(&clone->lock);

	if (bio_list_empty(&discards))
		return;

	if (unlikely(get_clone_mode(clone) >= CM_READ_ONLY))
		goto out;

	/* Update the metadata */
	bio_list_for_each(bio, &discards) {
		bio_region_range(clone, bio, &rs, &nr_regions);
		/*
		 * A discard request might cover regions that have been already
		 * hydrated. There is no need to update the metadata for these
		 * regions.
		 */
		r = dm_clone_cond_set_range(clone->cmd, rs, nr_regions);
		if (unlikely(r))
			break;
	}
out:
	blk_start_plug(&plug);
	while ((bio = bio_list_pop(&discards)))
		complete_discard_bio(clone, bio, r == 0);
	blk_finish_plug(&plug);
}

static void process_deferred_bios(struct clone *clone)
{
	struct bio_list bios = BIO_EMPTY_LIST;

	spin_lock_irq(&clone->lock);
	bio_list_merge(&bios, &clone->deferred_bios);
	bio_list_init(&clone->deferred_bios);
	spin_unlock_irq(&clone->lock);

	if (bio_list_empty(&bios))
		return;

	submit_bios(&bios);
}

static void process_deferred_flush_bios(struct clone *clone)
{
	struct bio *bio;
	bool dest_dev_flushed;
	struct bio_list bios = BIO_EMPTY_LIST;
	struct bio_list bio_completions = BIO_EMPTY_LIST;

	/*
	 * If there are any deferred flush bios, we must commit the metadata
	 * before issuing them or signaling their completion.
	 */
	spin_lock_irq(&clone->lock);
	bio_list_merge(&bios, &clone->deferred_flush_bios);
	bio_list_init(&clone->deferred_flush_bios);

	bio_list_merge(&bio_completions, &clone->deferred_flush_completions);
	bio_list_init(&clone->deferred_flush_completions);
	spin_unlock_irq(&clone->lock);

	if (bio_list_empty(&bios) && bio_list_empty(&bio_completions) &&
	    !(dm_clone_changed_this_transaction(clone->cmd) && need_commit_due_to_time(clone)))
		return;

	if (commit_metadata(clone, &dest_dev_flushed)) {
		bio_list_merge(&bios, &bio_completions);

		while ((bio = bio_list_pop(&bios)))
			bio_io_error(bio);

		return;
	}

	clone->last_commit_jiffies = jiffies;

	while ((bio = bio_list_pop(&bio_completions)))
		bio_endio(bio);

	while ((bio = bio_list_pop(&bios))) {
		if ((bio->bi_opf & REQ_PREFLUSH) && dest_dev_flushed) {
			/* We just flushed the destination device as part of
			 * the metadata commit, so there is no reason to send
			 * another flush.
			 */
			bio_endio(bio);
		} else {
			submit_bio_noacct(bio);
		}
	}
}

static void do_worker(struct work_struct *work)
{
	struct clone *clone = container_of(work, typeof(*clone), worker);

	process_deferred_bios(clone);
	process_deferred_discards(clone);

	/*
	 * process_deferred_flush_bios():
	 *
	 *   - Commit metadata
	 *
	 *   - Process deferred REQ_FUA completions
	 *
	 *   - Process deferred REQ_PREFLUSH bios
	 */
	process_deferred_flush_bios(clone);

	/* Background hydration */
	do_hydration(clone);
}

/*
 * Commit periodically so that not too much unwritten data builds up.
 *
 * Also, restart background hydration, if it has been stopped by in-flight I/O.
 */
static void do_waker(struct work_struct *work)
{
	struct clone *clone = container_of(to_delayed_work(work), struct clone, waker);

	wake_worker(clone);
	queue_delayed_work(clone->wq, &clone->waker, COMMIT_PERIOD);
}

/*---------------------------------------------------------------------------*/

/*
 * Target methods
 */
static int clone_map(struct dm_target *ti, struct bio *bio)
{
	struct clone *clone = ti->private;
	unsigned long region_nr;

	atomic_inc(&clone->ios_in_flight);

	if (unlikely(get_clone_mode(clone) == CM_FAIL))
		return DM_MAPIO_KILL;

	/*
	 * REQ_PREFLUSH bios carry no data:
	 *
	 * - Commit metadata, if changed
	 *
	 * - Pass down to destination device
	 */
	if (bio->bi_opf & REQ_PREFLUSH) {
		remap_and_issue(clone, bio);
		return DM_MAPIO_SUBMITTED;
	}

	bio->bi_iter.bi_sector = dm_target_offset(ti, bio->bi_iter.bi_sector);

	/*
	 * dm-clone interprets discards and performs a fast hydration of the
	 * discarded regions, i.e., we skip the copy from the source device and
	 * just mark the regions as hydrated.
	 */
	if (bio_op(bio) == REQ_OP_DISCARD) {
		process_discard_bio(clone, bio);
		return DM_MAPIO_SUBMITTED;
	}

	/*
	 * If the bio's region is hydrated, redirect it to the destination
	 * device.
	 *
	 * If the region is not hydrated and the bio is a READ, redirect it to
	 * the source device.
	 *
	 * Else, defer WRITE bio until after its region has been hydrated and
	 * start the region's hydration immediately.
	 */
	region_nr = bio_to_region(clone, bio);
	if (dm_clone_is_region_hydrated(clone->cmd, region_nr)) {
		remap_and_issue(clone, bio);
		return DM_MAPIO_SUBMITTED;
	} else if (bio_data_dir(bio) == READ) {
		remap_to_source(clone, bio);
		return DM_MAPIO_REMAPPED;
	}

	remap_to_dest(clone, bio);
	hydrate_bio_region(clone, bio);

	return DM_MAPIO_SUBMITTED;
}

static int clone_endio(struct dm_target *ti, struct bio *bio, blk_status_t *error)
{
	struct clone *clone = ti->private;

	atomic_dec(&clone->ios_in_flight);

	return DM_ENDIO_DONE;
}

static void emit_flags(struct clone *clone, char *result, unsigned int maxlen,
		       ssize_t *sz_ptr)
{
	ssize_t sz = *sz_ptr;
	unsigned int count;

	count = !test_bit(DM_CLONE_HYDRATION_ENABLED, &clone->flags);
	count += !test_bit(DM_CLONE_DISCARD_PASSDOWN, &clone->flags);

	DMEMIT("%u ", count);

	if (!test_bit(DM_CLONE_HYDRATION_ENABLED, &clone->flags))
		DMEMIT("no_hydration ");

	if (!test_bit(DM_CLONE_DISCARD_PASSDOWN, &clone->flags))
		DMEMIT("no_discard_passdown ");

	*sz_ptr = sz;
}

static void emit_core_args(struct clone *clone, char *result,
			   unsigned int maxlen, ssize_t *sz_ptr)
{
	ssize_t sz = *sz_ptr;
	unsigned int count = 4;

	DMEMIT("%u hydration_threshold %u hydration_batch_size %u ", count,
	       READ_ONCE(clone->hydration_threshold),
	       READ_ONCE(clone->hydration_batch_size));

	*sz_ptr = sz;
}

/*
 * Status format:
 *
 * <metadata block size> <#used metadata blocks>/<#total metadata blocks>
 * <clone region size> <#hydrated regions>/<#total regions> <#hydrating regions>
 * <#features> <features>* <#core args> <core args>* <clone metadata mode>
 */
static void clone_status(struct dm_target *ti, status_type_t type,
			 unsigned int status_flags, char *result,
			 unsigned int maxlen)
{
	int r;
	unsigned int i;
	ssize_t sz = 0;
	dm_block_t nr_free_metadata_blocks = 0;
	dm_block_t nr_metadata_blocks = 0;
	char buf[BDEVNAME_SIZE];
	struct clone *clone = ti->private;

	switch (type) {
	case STATUSTYPE_INFO:
		if (get_clone_mode(clone) == CM_FAIL) {
			DMEMIT("Fail");
			break;
		}

		/* Commit to ensure statistics aren't out-of-date */
		if (!(status_flags & DM_STATUS_NOFLUSH_FLAG) && !dm_suspended(ti))
			(void) commit_metadata(clone, NULL);

		r = dm_clone_get_free_metadata_block_count(clone->cmd, &nr_free_metadata_blocks);

		if (r) {
			DMERR("%s: dm_clone_get_free_metadata_block_count returned %d",
			      clone_device_name(clone), r);
			goto error;
		}

		r = dm_clone_get_metadata_dev_size(clone->cmd, &nr_metadata_blocks);

		if (r) {
			DMERR("%s: dm_clone_get_metadata_dev_size returned %d",
			      clone_device_name(clone), r);
			goto error;
		}

		DMEMIT("%u %llu/%llu %llu %u/%lu %u ",
		       DM_CLONE_METADATA_BLOCK_SIZE,
		       (unsigned long long)(nr_metadata_blocks - nr_free_metadata_blocks),
		       (unsigned long long)nr_metadata_blocks,
		       (unsigned long long)clone->region_size,
		       dm_clone_nr_of_hydrated_regions(clone->cmd),
		       clone->nr_regions,
		       atomic_read(&clone->hydrations_in_flight));

		emit_flags(clone, result, maxlen, &sz);
		emit_core_args(clone, result, maxlen, &sz);

		switch (get_clone_mode(clone)) {
		case CM_WRITE:
			DMEMIT("rw");
			break;
		case CM_READ_ONLY:
			DMEMIT("ro");
			break;
		case CM_FAIL:
			DMEMIT("Fail");
		}

		break;

	case STATUSTYPE_TABLE:
		format_dev_t(buf, clone->metadata_dev->bdev->bd_dev);
		DMEMIT("%s ", buf);

		format_dev_t(buf, clone->dest_dev->bdev->bd_dev);
		DMEMIT("%s ", buf);

		format_dev_t(buf, clone->source_dev->bdev->bd_dev);
		DMEMIT("%s", buf);

		for (i = 0; i < clone->nr_ctr_args; i++)
			DMEMIT(" %s", clone->ctr_args[i]);
	}

	return;

error:
	DMEMIT("Error");
}

static sector_t get_dev_size(struct dm_dev *dev)
{
	return i_size_read(dev->bdev->bd_inode) >> SECTOR_SHIFT;
}

/*---------------------------------------------------------------------------*/

/*
 * Construct a clone device mapping:
 *
 * clone <metadata dev> <destination dev> <source dev> <region size>
 *	[<#feature args> [<feature arg>]* [<#core args> [key value]*]]
 *
 * metadata dev: Fast device holding the persistent metadata
 * destination dev: The destination device, which will become a clone of the
 *                  source device
 * source dev: The read-only source device that gets cloned
 * region size: dm-clone unit size in sectors
 *
 * #feature args: Number of feature arguments passed
 * feature args: E.g. no_hydration, no_discard_passdown
 *
 * #core arguments: An even number of core arguments
 * core arguments: Key/value pairs for tuning the core
 *		   E.g. 'hydration_threshold 256'
 */
static int parse_feature_args(struct dm_arg_set *as, struct clone *clone)
{
	int r;
	unsigned int argc;
	const char *arg_name;
	struct dm_target *ti = clone->ti;

	const struct dm_arg args = {
		.min = 0,
		.max = 2,
		.error = "Invalid number of feature arguments"
	};

	/* No feature arguments supplied */
	if (!as->argc)
		return 0;

	r = dm_read_arg_group(&args, as, &argc, &ti->error);
	if (r)
		return r;

	while (argc) {
		arg_name = dm_shift_arg(as);
		argc--;

		if (!strcasecmp(arg_name, "no_hydration")) {
			__clear_bit(DM_CLONE_HYDRATION_ENABLED, &clone->flags);
		} else if (!strcasecmp(arg_name, "no_discard_passdown")) {
			__clear_bit(DM_CLONE_DISCARD_PASSDOWN, &clone->flags);
		} else {
			ti->error = "Invalid feature argument";
			return -EINVAL;
		}
	}

	return 0;
}

static int parse_core_args(struct dm_arg_set *as, struct clone *clone)
{
	int r;
	unsigned int argc;
	unsigned int value;
	const char *arg_name;
	struct dm_target *ti = clone->ti;

	const struct dm_arg args = {
		.min = 0,
		.max = 4,
		.error = "Invalid number of core arguments"
	};

	/* Initialize core arguments */
	clone->hydration_batch_size = DEFAULT_HYDRATION_BATCH_SIZE;
	clone->hydration_threshold = DEFAULT_HYDRATION_THRESHOLD;

	/* No core arguments supplied */
	if (!as->argc)
		return 0;

	r = dm_read_arg_group(&args, as, &argc, &ti->error);
	if (r)
		return r;

	if (argc & 1) {
		ti->error = "Number of core arguments must be even";
		return -EINVAL;
	}

	while (argc) {
		arg_name = dm_shift_arg(as);
		argc -= 2;

		if (!strcasecmp(arg_name, "hydration_threshold")) {
			if (kstrtouint(dm_shift_arg(as), 10, &value)) {
				ti->error = "Invalid value for argument `hydration_threshold'";
				return -EINVAL;
			}
			clone->hydration_threshold = value;
		} else if (!strcasecmp(arg_name, "hydration_batch_size")) {
			if (kstrtouint(dm_shift_arg(as), 10, &value)) {
				ti->error = "Invalid value for argument `hydration_batch_size'";
				return -EINVAL;
			}
			clone->hydration_batch_size = value;
		} else {
			ti->error = "Invalid core argument";
			return -EINVAL;
		}
	}

	return 0;
}

static int parse_region_size(struct clone *clone, struct dm_arg_set *as, char **error)
{
	int r;
	unsigned int region_size;
	struct dm_arg arg;

	arg.min = MIN_REGION_SIZE;
	arg.max = MAX_REGION_SIZE;
	arg.error = "Invalid region size";

	r = dm_read_arg(&arg, as, &region_size, error);
	if (r)
		return r;

	/* Check region size is a power of 2 */
	if (!is_power_of_2(region_size)) {
		*error = "Region size is not a power of 2";
		return -EINVAL;
	}

	/* Validate the region size against the device logical block size */
	if (region_size % (bdev_logical_block_size(clone->source_dev->bdev) >> 9) ||
	    region_size % (bdev_logical_block_size(clone->dest_dev->bdev) >> 9)) {
		*error = "Region size is not a multiple of device logical block size";
		return -EINVAL;
	}

	clone->region_size = region_size;

	return 0;
}

static int validate_nr_regions(unsigned long n, char **error)
{
	/*
	 * dm_bitset restricts us to 2^32 regions. test_bit & co. restrict us
	 * further to 2^31 regions.
	 */
	if (n > (1UL << 31)) {
		*error = "Too many regions. Consider increasing the region size";
		return -EINVAL;
	}

	return 0;
}

static int parse_metadata_dev(struct clone *clone, struct dm_arg_set *as, char **error)
{
	int r;
	sector_t metadata_dev_size;
	char b[BDEVNAME_SIZE];

	r = dm_get_device(clone->ti, dm_shift_arg(as), FMODE_READ | FMODE_WRITE,
			  &clone->metadata_dev);
	if (r) {
		*error = "Error opening metadata device";
		return r;
	}

	metadata_dev_size = get_dev_size(clone->metadata_dev);
	if (metadata_dev_size > DM_CLONE_METADATA_MAX_SECTORS_WARNING)
		DMWARN("Metadata device %s is larger than %u sectors: excess space will not be used.",
		       bdevname(clone->metadata_dev->bdev, b), DM_CLONE_METADATA_MAX_SECTORS);

	return 0;
}

static int parse_dest_dev(struct clone *clone, struct dm_arg_set *as, char **error)
{
	int r;
	sector_t dest_dev_size;

	r = dm_get_device(clone->ti, dm_shift_arg(as), FMODE_READ | FMODE_WRITE,
			  &clone->dest_dev);
	if (r) {
		*error = "Error opening destination device";
		return r;
	}

	dest_dev_size = get_dev_size(clone->dest_dev);
	if (dest_dev_size < clone->ti->len) {
		dm_put_device(clone->ti, clone->dest_dev);
		*error = "Device size larger than destination device";
		return -EINVAL;
	}

	return 0;
}

static int parse_source_dev(struct clone *clone, struct dm_arg_set *as, char **error)
{
	int r;
	sector_t source_dev_size;

	r = dm_get_device(clone->ti, dm_shift_arg(as), FMODE_READ,
			  &clone->source_dev);
	if (r) {
		*error = "Error opening source device";
		return r;
	}

	source_dev_size = get_dev_size(clone->source_dev);
	if (source_dev_size < clone->ti->len) {
		dm_put_device(clone->ti, clone->source_dev);
		*error = "Device size larger than source device";
		return -EINVAL;
	}

	return 0;
}

static int copy_ctr_args(struct clone *clone, int argc, const char **argv, char **error)
{
	unsigned int i;
	const char **copy;

	copy = kcalloc(argc, sizeof(*copy), GFP_KERNEL);
	if (!copy)
		goto error;

	for (i = 0; i < argc; i++) {
		copy[i] = kstrdup(argv[i], GFP_KERNEL);

		if (!copy[i]) {
			while (i--)
				kfree(copy[i]);
			kfree(copy);
			goto error;
		}
	}

	clone->nr_ctr_args = argc;
	clone->ctr_args = copy;
	return 0;

error:
	*error = "Failed to allocate memory for table line";
	return -ENOMEM;
}

static int clone_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	int r;
	sector_t nr_regions;
	struct clone *clone;
	struct dm_arg_set as;

	if (argc < 4) {
		ti->error = "Invalid number of arguments";
		return -EINVAL;
	}

	as.argc = argc;
	as.argv = argv;

	clone = kzalloc(sizeof(*clone), GFP_KERNEL);
	if (!clone) {
		ti->error = "Failed to allocate clone structure";
		return -ENOMEM;
	}

	clone->ti = ti;

	/* Initialize dm-clone flags */
	__set_bit(DM_CLONE_HYDRATION_ENABLED, &clone->flags);
	__set_bit(DM_CLONE_HYDRATION_SUSPENDED, &clone->flags);
	__set_bit(DM_CLONE_DISCARD_PASSDOWN, &clone->flags);

	r = parse_metadata_dev(clone, &as, &ti->error);
	if (r)
		goto out_with_clone;

	r = parse_dest_dev(clone, &as, &ti->error);
	if (r)
		goto out_with_meta_dev;

	r = parse_source_dev(clone, &as, &ti->error);
	if (r)
		goto out_with_dest_dev;

	r = parse_region_size(clone, &as, &ti->error);
	if (r)
		goto out_with_source_dev;

	clone->region_shift = __ffs(clone->region_size);
	nr_regions = dm_sector_div_up(ti->len, clone->region_size);

	/* Check for overflow */
	if (nr_regions != (unsigned long)nr_regions) {
		ti->error = "Too many regions. Consider increasing the region size";
		r = -EOVERFLOW;
		goto out_with_source_dev;
	}

	clone->nr_regions = nr_regions;

	r = validate_nr_regions(clone->nr_regions, &ti->error);
	if (r)
		goto out_with_source_dev;

	r = dm_set_target_max_io_len(ti, clone->region_size);
	if (r) {
		ti->error = "Failed to set max io len";
		goto out_with_source_dev;
	}

	r = parse_feature_args(&as, clone);
	if (r)
		goto out_with_source_dev;

	r = parse_core_args(&as, clone);
	if (r)
		goto out_with_source_dev;

	/* Load metadata */
	clone->cmd = dm_clone_metadata_open(clone->metadata_dev->bdev, ti->len,
					    clone->region_size);
	if (IS_ERR(clone->cmd)) {
		ti->error = "Failed to load metadata";
		r = PTR_ERR(clone->cmd);
		goto out_with_source_dev;
	}

	__set_clone_mode(clone, CM_WRITE);

	if (get_clone_mode(clone) != CM_WRITE) {
		ti->error = "Unable to get write access to metadata, please check/repair metadata";
		r = -EPERM;
		goto out_with_metadata;
	}

	clone->last_commit_jiffies = jiffies;

	/* Allocate hydration hash table */
	r = hash_table_init(clone);
	if (r) {
		ti->error = "Failed to allocate hydration hash table";
		goto out_with_metadata;
	}

	atomic_set(&clone->ios_in_flight, 0);
	init_waitqueue_head(&clone->hydration_stopped);
	spin_lock_init(&clone->lock);
	bio_list_init(&clone->deferred_bios);
	bio_list_init(&clone->deferred_discard_bios);
	bio_list_init(&clone->deferred_flush_bios);
	bio_list_init(&clone->deferred_flush_completions);
	clone->hydration_offset = 0;
	atomic_set(&clone->hydrations_in_flight, 0);

	clone->wq = alloc_workqueue("dm-" DM_MSG_PREFIX, WQ_MEM_RECLAIM, 0);
	if (!clone->wq) {
		ti->error = "Failed to allocate workqueue";
		r = -ENOMEM;
		goto out_with_ht;
	}

	INIT_WORK(&clone->worker, do_worker);
	INIT_DELAYED_WORK(&clone->waker, do_waker);

	clone->kcopyd_client = dm_kcopyd_client_create(&dm_kcopyd_throttle);
	if (IS_ERR(clone->kcopyd_client)) {
		r = PTR_ERR(clone->kcopyd_client);
		goto out_with_wq;
	}

	r = mempool_init_slab_pool(&clone->hydration_pool, MIN_HYDRATIONS,
				   _hydration_cache);
	if (r) {
		ti->error = "Failed to create dm_clone_region_hydration memory pool";
		goto out_with_kcopyd;
	}

	/* Save a copy of the table line */
	r = copy_ctr_args(clone, argc - 3, (const char **)argv + 3, &ti->error);
	if (r)
		goto out_with_mempool;

	mutex_init(&clone->commit_lock);

	/* Enable flushes */
	ti->num_flush_bios = 1;
	ti->flush_supported = true;

	/* Enable discards */
	ti->discards_supported = true;
	ti->num_discard_bios = 1;

	ti->private = clone;

	return 0;

out_with_mempool:
	mempool_exit(&clone->hydration_pool);
out_with_kcopyd:
	dm_kcopyd_client_destroy(clone->kcopyd_client);
out_with_wq:
	destroy_workqueue(clone->wq);
out_with_ht:
	hash_table_exit(clone);
out_with_metadata:
	dm_clone_metadata_close(clone->cmd);
out_with_source_dev:
	dm_put_device(ti, clone->source_dev);
out_with_dest_dev:
	dm_put_device(ti, clone->dest_dev);
out_with_meta_dev:
	dm_put_device(ti, clone->metadata_dev);
out_with_clone:
	kfree(clone);

	return r;
}

static void clone_dtr(struct dm_target *ti)
{
	unsigned int i;
	struct clone *clone = ti->private;

	mutex_destroy(&clone->commit_lock);

	for (i = 0; i < clone->nr_ctr_args; i++)
		kfree(clone->ctr_args[i]);
	kfree(clone->ctr_args);

	mempool_exit(&clone->hydration_pool);
	dm_kcopyd_client_destroy(clone->kcopyd_client);
	destroy_workqueue(clone->wq);
	hash_table_exit(clone);
	dm_clone_metadata_close(clone->cmd);
	dm_put_device(ti, clone->source_dev);
	dm_put_device(ti, clone->dest_dev);
	dm_put_device(ti, clone->metadata_dev);

	kfree(clone);
}

/*---------------------------------------------------------------------------*/

static void clone_postsuspend(struct dm_target *ti)
{
	struct clone *clone = ti->private;

	/*
	 * To successfully suspend the device:
	 *
	 *	- We cancel the delayed work for periodic commits and wait for
	 *	  it to finish.
	 *
	 *	- We stop the background hydration, i.e. we prevent new region
	 *	  hydrations from starting.
	 *
	 *	- We wait for any in-flight hydrations to finish.
	 *
	 *	- We flush the workqueue.
	 *
	 *	- We commit the metadata.
	 */
	cancel_delayed_work_sync(&clone->waker);

	set_bit(DM_CLONE_HYDRATION_SUSPENDED, &clone->flags);

	/*
	 * Make sure set_bit() is ordered before atomic_read(), otherwise we
	 * might race with do_hydration() and miss some started region
	 * hydrations.
	 *
	 * This is paired with smp_mb__after_atomic() in do_hydration().
	 */
	smp_mb__after_atomic();

	wait_event(clone->hydration_stopped, !atomic_read(&clone->hydrations_in_flight));
	flush_workqueue(clone->wq);

	(void) commit_metadata(clone, NULL);
}

static void clone_resume(struct dm_target *ti)
{
	struct clone *clone = ti->private;

	clear_bit(DM_CLONE_HYDRATION_SUSPENDED, &clone->flags);
	do_waker(&clone->waker.work);
}

static bool bdev_supports_discards(struct block_device *bdev)
{
	struct request_queue *q = bdev_get_queue(bdev);

	return (q && blk_queue_discard(q));
}

/*
 * If discard_passdown was enabled verify that the destination device supports
 * discards. Disable discard_passdown if not.
 */
static void disable_passdown_if_not_supported(struct clone *clone)
{
	struct block_device *dest_dev = clone->dest_dev->bdev;
	struct queue_limits *dest_limits = &bdev_get_queue(dest_dev)->limits;
	const char *reason = NULL;
	char buf[BDEVNAME_SIZE];

	if (!test_bit(DM_CLONE_DISCARD_PASSDOWN, &clone->flags))
		return;

	if (!bdev_supports_discards(dest_dev))
		reason = "discard unsupported";
	else if (dest_limits->max_discard_sectors < clone->region_size)
		reason = "max discard sectors smaller than a region";

	if (reason) {
		DMWARN("Destination device (%s) %s: Disabling discard passdown.",
		       bdevname(dest_dev, buf), reason);
		clear_bit(DM_CLONE_DISCARD_PASSDOWN, &clone->flags);
	}
}

static void set_discard_limits(struct clone *clone, struct queue_limits *limits)
{
	struct block_device *dest_bdev = clone->dest_dev->bdev;
	struct queue_limits *dest_limits = &bdev_get_queue(dest_bdev)->limits;

	if (!test_bit(DM_CLONE_DISCARD_PASSDOWN, &clone->flags)) {
		/* No passdown is done so we set our own virtual limits */
		limits->discard_granularity = clone->region_size << SECTOR_SHIFT;
		limits->max_discard_sectors = round_down(UINT_MAX >> SECTOR_SHIFT, clone->region_size);
		return;
	}

	/*
	 * clone_iterate_devices() is stacking both the source and destination
	 * device limits but discards aren't passed to the source device, so
	 * inherit destination's limits.
	 */
	limits->max_discard_sectors = dest_limits->max_discard_sectors;
	limits->max_hw_discard_sectors = dest_limits->max_hw_discard_sectors;
	limits->discard_granularity = dest_limits->discard_granularity;
	limits->discard_alignment = dest_limits->discard_alignment;
	limits->discard_misaligned = dest_limits->discard_misaligned;
	limits->max_discard_segments = dest_limits->max_discard_segments;
}

static void clone_io_hints(struct dm_target *ti, struct queue_limits *limits)
{
	struct clone *clone = ti->private;
	u64 io_opt_sectors = limits->io_opt >> SECTOR_SHIFT;

	/*
	 * If the system-determined stacked limits are compatible with
	 * dm-clone's region size (io_opt is a factor) do not override them.
	 */
	if (io_opt_sectors < clone->region_size ||
	    do_div(io_opt_sectors, clone->region_size)) {
		blk_limits_io_min(limits, clone->region_size << SECTOR_SHIFT);
		blk_limits_io_opt(limits, clone->region_size << SECTOR_SHIFT);
	}

	disable_passdown_if_not_supported(clone);
	set_discard_limits(clone, limits);
}

static int clone_iterate_devices(struct dm_target *ti,
				 iterate_devices_callout_fn fn, void *data)
{
	int ret;
	struct clone *clone = ti->private;
	struct dm_dev *dest_dev = clone->dest_dev;
	struct dm_dev *source_dev = clone->source_dev;

	ret = fn(ti, source_dev, 0, ti->len, data);
	if (!ret)
		ret = fn(ti, dest_dev, 0, ti->len, data);
	return ret;
}

/*
 * dm-clone message functions.
 */
static void set_hydration_threshold(struct clone *clone, unsigned int nr_regions)
{
	WRITE_ONCE(clone->hydration_threshold, nr_regions);

	/*
	 * If user space sets hydration_threshold to zero then the hydration
	 * will stop. If at a later time the hydration_threshold is increased
	 * we must restart the hydration process by waking up the worker.
	 */
	wake_worker(clone);
}

static void set_hydration_batch_size(struct clone *clone, unsigned int nr_regions)
{
	WRITE_ONCE(clone->hydration_batch_size, nr_regions);
}

static void enable_hydration(struct clone *clone)
{
	if (!test_and_set_bit(DM_CLONE_HYDRATION_ENABLED, &clone->flags))
		wake_worker(clone);
}

static void disable_hydration(struct clone *clone)
{
	clear_bit(DM_CLONE_HYDRATION_ENABLED, &clone->flags);
}

static int clone_message(struct dm_target *ti, unsigned int argc, char **argv,
			 char *result, unsigned int maxlen)
{
	struct clone *clone = ti->private;
	unsigned int value;

	if (!argc)
		return -EINVAL;

	if (!strcasecmp(argv[0], "enable_hydration")) {
		enable_hydration(clone);
		return 0;
	}

	if (!strcasecmp(argv[0], "disable_hydration")) {
		disable_hydration(clone);
		return 0;
	}

	if (argc != 2)
		return -EINVAL;

	if (!strcasecmp(argv[0], "hydration_threshold")) {
		if (kstrtouint(argv[1], 10, &value))
			return -EINVAL;

		set_hydration_threshold(clone, value);

		return 0;
	}

	if (!strcasecmp(argv[0], "hydration_batch_size")) {
		if (kstrtouint(argv[1], 10, &value))
			return -EINVAL;

		set_hydration_batch_size(clone, value);

		return 0;
	}

	DMERR("%s: Unsupported message `%s'", clone_device_name(clone), argv[0]);
	return -EINVAL;
}

static struct target_type clone_target = {
	.name = "clone",
	.version = {1, 0, 0},
	.module = THIS_MODULE,
	.ctr = clone_ctr,
	.dtr =  clone_dtr,
	.map = clone_map,
	.end_io = clone_endio,
	.postsuspend = clone_postsuspend,
	.resume = clone_resume,
	.status = clone_status,
	.message = clone_message,
	.io_hints = clone_io_hints,
	.iterate_devices = clone_iterate_devices,
};

/*---------------------------------------------------------------------------*/

/* Module functions */
static int __init dm_clone_init(void)
{
	int r;

	_hydration_cache = KMEM_CACHE(dm_clone_region_hydration, 0);
	if (!_hydration_cache)
		return -ENOMEM;

	r = dm_register_target(&clone_target);
	if (r < 0) {
		DMERR("Failed to register clone target");
		return r;
	}

	return 0;
}

static void __exit dm_clone_exit(void)
{
	dm_unregister_target(&clone_target);

	kmem_cache_destroy(_hydration_cache);
	_hydration_cache = NULL;
}

/* Module hooks */
module_init(dm_clone_init);
module_exit(dm_clone_exit);

MODULE_DESCRIPTION(DM_NAME " clone target");
MODULE_AUTHOR("Nikos Tsironis <ntsironis@arrikto.com>");
MODULE_LICENSE("GPL");
