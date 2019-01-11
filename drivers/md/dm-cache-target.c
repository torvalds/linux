/*
 * Copyright (C) 2012 Red Hat. All rights reserved.
 *
 * This file is released under the GPL.
 */

#include "dm.h"
#include "dm-bio-prison-v2.h"
#include "dm-bio-record.h"
#include "dm-cache-metadata.h"

#include <linux/dm-io.h>
#include <linux/dm-kcopyd.h>
#include <linux/jiffies.h>
#include <linux/init.h>
#include <linux/mempool.h>
#include <linux/module.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#define DM_MSG_PREFIX "cache"

DECLARE_DM_KCOPYD_THROTTLE_WITH_MODULE_PARM(cache_copy_throttle,
	"A percentage of time allocated for copying to and/or from cache");

/*----------------------------------------------------------------*/

/*
 * Glossary:
 *
 * oblock: index of an origin block
 * cblock: index of a cache block
 * promotion: movement of a block from origin to cache
 * demotion: movement of a block from cache to origin
 * migration: movement of a block between the origin and cache device,
 *	      either direction
 */

/*----------------------------------------------------------------*/

struct io_tracker {
	spinlock_t lock;

	/*
	 * Sectors of in-flight IO.
	 */
	sector_t in_flight;

	/*
	 * The time, in jiffies, when this device became idle (if it is
	 * indeed idle).
	 */
	unsigned long idle_time;
	unsigned long last_update_time;
};

static void iot_init(struct io_tracker *iot)
{
	spin_lock_init(&iot->lock);
	iot->in_flight = 0ul;
	iot->idle_time = 0ul;
	iot->last_update_time = jiffies;
}

static bool __iot_idle_for(struct io_tracker *iot, unsigned long jifs)
{
	if (iot->in_flight)
		return false;

	return time_after(jiffies, iot->idle_time + jifs);
}

static bool iot_idle_for(struct io_tracker *iot, unsigned long jifs)
{
	bool r;
	unsigned long flags;

	spin_lock_irqsave(&iot->lock, flags);
	r = __iot_idle_for(iot, jifs);
	spin_unlock_irqrestore(&iot->lock, flags);

	return r;
}

static void iot_io_begin(struct io_tracker *iot, sector_t len)
{
	unsigned long flags;

	spin_lock_irqsave(&iot->lock, flags);
	iot->in_flight += len;
	spin_unlock_irqrestore(&iot->lock, flags);
}

static void __iot_io_end(struct io_tracker *iot, sector_t len)
{
	if (!len)
		return;

	iot->in_flight -= len;
	if (!iot->in_flight)
		iot->idle_time = jiffies;
}

static void iot_io_end(struct io_tracker *iot, sector_t len)
{
	unsigned long flags;

	spin_lock_irqsave(&iot->lock, flags);
	__iot_io_end(iot, len);
	spin_unlock_irqrestore(&iot->lock, flags);
}

/*----------------------------------------------------------------*/

/*
 * Represents a chunk of future work.  'input' allows continuations to pass
 * values between themselves, typically error values.
 */
struct continuation {
	struct work_struct ws;
	blk_status_t input;
};

static inline void init_continuation(struct continuation *k,
				     void (*fn)(struct work_struct *))
{
	INIT_WORK(&k->ws, fn);
	k->input = 0;
}

static inline void queue_continuation(struct workqueue_struct *wq,
				      struct continuation *k)
{
	queue_work(wq, &k->ws);
}

/*----------------------------------------------------------------*/

/*
 * The batcher collects together pieces of work that need a particular
 * operation to occur before they can proceed (typically a commit).
 */
struct batcher {
	/*
	 * The operation that everyone is waiting for.
	 */
	blk_status_t (*commit_op)(void *context);
	void *commit_context;

	/*
	 * This is how bios should be issued once the commit op is complete
	 * (accounted_request).
	 */
	void (*issue_op)(struct bio *bio, void *context);
	void *issue_context;

	/*
	 * Queued work gets put on here after commit.
	 */
	struct workqueue_struct *wq;

	spinlock_t lock;
	struct list_head work_items;
	struct bio_list bios;
	struct work_struct commit_work;

	bool commit_scheduled;
};

static void __commit(struct work_struct *_ws)
{
	struct batcher *b = container_of(_ws, struct batcher, commit_work);
	blk_status_t r;
	unsigned long flags;
	struct list_head work_items;
	struct work_struct *ws, *tmp;
	struct continuation *k;
	struct bio *bio;
	struct bio_list bios;

	INIT_LIST_HEAD(&work_items);
	bio_list_init(&bios);

	/*
	 * We have to grab these before the commit_op to avoid a race
	 * condition.
	 */
	spin_lock_irqsave(&b->lock, flags);
	list_splice_init(&b->work_items, &work_items);
	bio_list_merge(&bios, &b->bios);
	bio_list_init(&b->bios);
	b->commit_scheduled = false;
	spin_unlock_irqrestore(&b->lock, flags);

	r = b->commit_op(b->commit_context);

	list_for_each_entry_safe(ws, tmp, &work_items, entry) {
		k = container_of(ws, struct continuation, ws);
		k->input = r;
		INIT_LIST_HEAD(&ws->entry); /* to avoid a WARN_ON */
		queue_work(b->wq, ws);
	}

	while ((bio = bio_list_pop(&bios))) {
		if (r) {
			bio->bi_status = r;
			bio_endio(bio);
		} else
			b->issue_op(bio, b->issue_context);
	}
}

static void batcher_init(struct batcher *b,
			 blk_status_t (*commit_op)(void *),
			 void *commit_context,
			 void (*issue_op)(struct bio *bio, void *),
			 void *issue_context,
			 struct workqueue_struct *wq)
{
	b->commit_op = commit_op;
	b->commit_context = commit_context;
	b->issue_op = issue_op;
	b->issue_context = issue_context;
	b->wq = wq;

	spin_lock_init(&b->lock);
	INIT_LIST_HEAD(&b->work_items);
	bio_list_init(&b->bios);
	INIT_WORK(&b->commit_work, __commit);
	b->commit_scheduled = false;
}

static void async_commit(struct batcher *b)
{
	queue_work(b->wq, &b->commit_work);
}

static void continue_after_commit(struct batcher *b, struct continuation *k)
{
	unsigned long flags;
	bool commit_scheduled;

	spin_lock_irqsave(&b->lock, flags);
	commit_scheduled = b->commit_scheduled;
	list_add_tail(&k->ws.entry, &b->work_items);
	spin_unlock_irqrestore(&b->lock, flags);

	if (commit_scheduled)
		async_commit(b);
}

/*
 * Bios are errored if commit failed.
 */
static void issue_after_commit(struct batcher *b, struct bio *bio)
{
       unsigned long flags;
       bool commit_scheduled;

       spin_lock_irqsave(&b->lock, flags);
       commit_scheduled = b->commit_scheduled;
       bio_list_add(&b->bios, bio);
       spin_unlock_irqrestore(&b->lock, flags);

       if (commit_scheduled)
	       async_commit(b);
}

/*
 * Call this if some urgent work is waiting for the commit to complete.
 */
static void schedule_commit(struct batcher *b)
{
	bool immediate;
	unsigned long flags;

	spin_lock_irqsave(&b->lock, flags);
	immediate = !list_empty(&b->work_items) || !bio_list_empty(&b->bios);
	b->commit_scheduled = true;
	spin_unlock_irqrestore(&b->lock, flags);

	if (immediate)
		async_commit(b);
}

/*
 * There are a couple of places where we let a bio run, but want to do some
 * work before calling its endio function.  We do this by temporarily
 * changing the endio fn.
 */
struct dm_hook_info {
	bio_end_io_t *bi_end_io;
};

static void dm_hook_bio(struct dm_hook_info *h, struct bio *bio,
			bio_end_io_t *bi_end_io, void *bi_private)
{
	h->bi_end_io = bio->bi_end_io;

	bio->bi_end_io = bi_end_io;
	bio->bi_private = bi_private;
}

static void dm_unhook_bio(struct dm_hook_info *h, struct bio *bio)
{
	bio->bi_end_io = h->bi_end_io;
}

/*----------------------------------------------------------------*/

#define MIGRATION_POOL_SIZE 128
#define COMMIT_PERIOD HZ
#define MIGRATION_COUNT_WINDOW 10

/*
 * The block size of the device holding cache data must be
 * between 32KB and 1GB.
 */
#define DATA_DEV_BLOCK_SIZE_MIN_SECTORS (32 * 1024 >> SECTOR_SHIFT)
#define DATA_DEV_BLOCK_SIZE_MAX_SECTORS (1024 * 1024 * 1024 >> SECTOR_SHIFT)

enum cache_metadata_mode {
	CM_WRITE,		/* metadata may be changed */
	CM_READ_ONLY,		/* metadata may not be changed */
	CM_FAIL
};

enum cache_io_mode {
	/*
	 * Data is written to cached blocks only.  These blocks are marked
	 * dirty.  If you lose the cache device you will lose data.
	 * Potential performance increase for both reads and writes.
	 */
	CM_IO_WRITEBACK,

	/*
	 * Data is written to both cache and origin.  Blocks are never
	 * dirty.  Potential performance benfit for reads only.
	 */
	CM_IO_WRITETHROUGH,

	/*
	 * A degraded mode useful for various cache coherency situations
	 * (eg, rolling back snapshots).  Reads and writes always go to the
	 * origin.  If a write goes to a cached oblock, then the cache
	 * block is invalidated.
	 */
	CM_IO_PASSTHROUGH
};

struct cache_features {
	enum cache_metadata_mode mode;
	enum cache_io_mode io_mode;
	unsigned metadata_version;
};

struct cache_stats {
	atomic_t read_hit;
	atomic_t read_miss;
	atomic_t write_hit;
	atomic_t write_miss;
	atomic_t demotion;
	atomic_t promotion;
	atomic_t writeback;
	atomic_t copies_avoided;
	atomic_t cache_cell_clash;
	atomic_t commit_count;
	atomic_t discard_count;
};

struct cache {
	struct dm_target *ti;
	spinlock_t lock;

	/*
	 * Fields for converting from sectors to blocks.
	 */
	int sectors_per_block_shift;
	sector_t sectors_per_block;

	struct dm_cache_metadata *cmd;

	/*
	 * Metadata is written to this device.
	 */
	struct dm_dev *metadata_dev;

	/*
	 * The slower of the two data devices.  Typically a spindle.
	 */
	struct dm_dev *origin_dev;

	/*
	 * The faster of the two data devices.  Typically an SSD.
	 */
	struct dm_dev *cache_dev;

	/*
	 * Size of the origin device in _complete_ blocks and native sectors.
	 */
	dm_oblock_t origin_blocks;
	sector_t origin_sectors;

	/*
	 * Size of the cache device in blocks.
	 */
	dm_cblock_t cache_size;

	/*
	 * Invalidation fields.
	 */
	spinlock_t invalidation_lock;
	struct list_head invalidation_requests;

	sector_t migration_threshold;
	wait_queue_head_t migration_wait;
	atomic_t nr_allocated_migrations;

	/*
	 * The number of in flight migrations that are performing
	 * background io. eg, promotion, writeback.
	 */
	atomic_t nr_io_migrations;

	struct bio_list deferred_bios;

	struct rw_semaphore quiesce_lock;

	struct dm_target_callbacks callbacks;

	/*
	 * origin_blocks entries, discarded if set.
	 */
	dm_dblock_t discard_nr_blocks;
	unsigned long *discard_bitset;
	uint32_t discard_block_size; /* a power of 2 times sectors per block */

	/*
	 * Rather than reconstructing the table line for the status we just
	 * save it and regurgitate.
	 */
	unsigned nr_ctr_args;
	const char **ctr_args;

	struct dm_kcopyd_client *copier;
	struct work_struct deferred_bio_worker;
	struct work_struct migration_worker;
	struct workqueue_struct *wq;
	struct delayed_work waker;
	struct dm_bio_prison_v2 *prison;

	/*
	 * cache_size entries, dirty if set
	 */
	unsigned long *dirty_bitset;
	atomic_t nr_dirty;

	unsigned policy_nr_args;
	struct dm_cache_policy *policy;

	/*
	 * Cache features such as write-through.
	 */
	struct cache_features features;

	struct cache_stats stats;

	bool need_tick_bio:1;
	bool sized:1;
	bool invalidate:1;
	bool commit_requested:1;
	bool loaded_mappings:1;
	bool loaded_discards:1;

	struct rw_semaphore background_work_lock;

	struct batcher committer;
	struct work_struct commit_ws;

	struct io_tracker tracker;

	mempool_t migration_pool;

	struct bio_set bs;
};

struct per_bio_data {
	bool tick:1;
	unsigned req_nr:2;
	struct dm_bio_prison_cell_v2 *cell;
	struct dm_hook_info hook_info;
	sector_t len;
};

struct dm_cache_migration {
	struct continuation k;
	struct cache *cache;

	struct policy_work *op;
	struct bio *overwrite_bio;
	struct dm_bio_prison_cell_v2 *cell;

	dm_cblock_t invalidate_cblock;
	dm_oblock_t invalidate_oblock;
};

/*----------------------------------------------------------------*/

static bool writethrough_mode(struct cache *cache)
{
	return cache->features.io_mode == CM_IO_WRITETHROUGH;
}

static bool writeback_mode(struct cache *cache)
{
	return cache->features.io_mode == CM_IO_WRITEBACK;
}

static inline bool passthrough_mode(struct cache *cache)
{
	return unlikely(cache->features.io_mode == CM_IO_PASSTHROUGH);
}

/*----------------------------------------------------------------*/

static void wake_deferred_bio_worker(struct cache *cache)
{
	queue_work(cache->wq, &cache->deferred_bio_worker);
}

static void wake_migration_worker(struct cache *cache)
{
	if (passthrough_mode(cache))
		return;

	queue_work(cache->wq, &cache->migration_worker);
}

/*----------------------------------------------------------------*/

static struct dm_bio_prison_cell_v2 *alloc_prison_cell(struct cache *cache)
{
	return dm_bio_prison_alloc_cell_v2(cache->prison, GFP_NOWAIT);
}

static void free_prison_cell(struct cache *cache, struct dm_bio_prison_cell_v2 *cell)
{
	dm_bio_prison_free_cell_v2(cache->prison, cell);
}

static struct dm_cache_migration *alloc_migration(struct cache *cache)
{
	struct dm_cache_migration *mg;

	mg = mempool_alloc(&cache->migration_pool, GFP_NOWAIT);
	if (!mg)
		return NULL;

	memset(mg, 0, sizeof(*mg));

	mg->cache = cache;
	atomic_inc(&cache->nr_allocated_migrations);

	return mg;
}

static void free_migration(struct dm_cache_migration *mg)
{
	struct cache *cache = mg->cache;

	if (atomic_dec_and_test(&cache->nr_allocated_migrations))
		wake_up(&cache->migration_wait);

	mempool_free(mg, &cache->migration_pool);
}

/*----------------------------------------------------------------*/

static inline dm_oblock_t oblock_succ(dm_oblock_t b)
{
	return to_oblock(from_oblock(b) + 1ull);
}

static void build_key(dm_oblock_t begin, dm_oblock_t end, struct dm_cell_key_v2 *key)
{
	key->virtual = 0;
	key->dev = 0;
	key->block_begin = from_oblock(begin);
	key->block_end = from_oblock(end);
}

/*
 * We have two lock levels.  Level 0, which is used to prevent WRITEs, and
 * level 1 which prevents *both* READs and WRITEs.
 */
#define WRITE_LOCK_LEVEL 0
#define READ_WRITE_LOCK_LEVEL 1

static unsigned lock_level(struct bio *bio)
{
	return bio_data_dir(bio) == WRITE ?
		WRITE_LOCK_LEVEL :
		READ_WRITE_LOCK_LEVEL;
}

/*----------------------------------------------------------------
 * Per bio data
 *--------------------------------------------------------------*/

static struct per_bio_data *get_per_bio_data(struct bio *bio)
{
	struct per_bio_data *pb = dm_per_bio_data(bio, sizeof(struct per_bio_data));
	BUG_ON(!pb);
	return pb;
}

static struct per_bio_data *init_per_bio_data(struct bio *bio)
{
	struct per_bio_data *pb = get_per_bio_data(bio);

	pb->tick = false;
	pb->req_nr = dm_bio_get_target_bio_nr(bio);
	pb->cell = NULL;
	pb->len = 0;

	return pb;
}

/*----------------------------------------------------------------*/

static void defer_bio(struct cache *cache, struct bio *bio)
{
	unsigned long flags;

	spin_lock_irqsave(&cache->lock, flags);
	bio_list_add(&cache->deferred_bios, bio);
	spin_unlock_irqrestore(&cache->lock, flags);

	wake_deferred_bio_worker(cache);
}

static void defer_bios(struct cache *cache, struct bio_list *bios)
{
	unsigned long flags;

	spin_lock_irqsave(&cache->lock, flags);
	bio_list_merge(&cache->deferred_bios, bios);
	bio_list_init(bios);
	spin_unlock_irqrestore(&cache->lock, flags);

	wake_deferred_bio_worker(cache);
}

/*----------------------------------------------------------------*/

static bool bio_detain_shared(struct cache *cache, dm_oblock_t oblock, struct bio *bio)
{
	bool r;
	struct per_bio_data *pb;
	struct dm_cell_key_v2 key;
	dm_oblock_t end = to_oblock(from_oblock(oblock) + 1ULL);
	struct dm_bio_prison_cell_v2 *cell_prealloc, *cell;

	cell_prealloc = alloc_prison_cell(cache); /* FIXME: allow wait if calling from worker */
	if (!cell_prealloc) {
		defer_bio(cache, bio);
		return false;
	}

	build_key(oblock, end, &key);
	r = dm_cell_get_v2(cache->prison, &key, lock_level(bio), bio, cell_prealloc, &cell);
	if (!r) {
		/*
		 * Failed to get the lock.
		 */
		free_prison_cell(cache, cell_prealloc);
		return r;
	}

	if (cell != cell_prealloc)
		free_prison_cell(cache, cell_prealloc);

	pb = get_per_bio_data(bio);
	pb->cell = cell;

	return r;
}

/*----------------------------------------------------------------*/

static bool is_dirty(struct cache *cache, dm_cblock_t b)
{
	return test_bit(from_cblock(b), cache->dirty_bitset);
}

static void set_dirty(struct cache *cache, dm_cblock_t cblock)
{
	if (!test_and_set_bit(from_cblock(cblock), cache->dirty_bitset)) {
		atomic_inc(&cache->nr_dirty);
		policy_set_dirty(cache->policy, cblock);
	}
}

/*
 * These two are called when setting after migrations to force the policy
 * and dirty bitset to be in sync.
 */
static void force_set_dirty(struct cache *cache, dm_cblock_t cblock)
{
	if (!test_and_set_bit(from_cblock(cblock), cache->dirty_bitset))
		atomic_inc(&cache->nr_dirty);
	policy_set_dirty(cache->policy, cblock);
}

static void force_clear_dirty(struct cache *cache, dm_cblock_t cblock)
{
	if (test_and_clear_bit(from_cblock(cblock), cache->dirty_bitset)) {
		if (atomic_dec_return(&cache->nr_dirty) == 0)
			dm_table_event(cache->ti->table);
	}

	policy_clear_dirty(cache->policy, cblock);
}

/*----------------------------------------------------------------*/

static bool block_size_is_power_of_two(struct cache *cache)
{
	return cache->sectors_per_block_shift >= 0;
}

/* gcc on ARM generates spurious references to __udivdi3 and __umoddi3 */
#if defined(CONFIG_ARM) && __GNUC__ == 4 && __GNUC_MINOR__ <= 6
__always_inline
#endif
static dm_block_t block_div(dm_block_t b, uint32_t n)
{
	do_div(b, n);

	return b;
}

static dm_block_t oblocks_per_dblock(struct cache *cache)
{
	dm_block_t oblocks = cache->discard_block_size;

	if (block_size_is_power_of_two(cache))
		oblocks >>= cache->sectors_per_block_shift;
	else
		oblocks = block_div(oblocks, cache->sectors_per_block);

	return oblocks;
}

static dm_dblock_t oblock_to_dblock(struct cache *cache, dm_oblock_t oblock)
{
	return to_dblock(block_div(from_oblock(oblock),
				   oblocks_per_dblock(cache)));
}

static void set_discard(struct cache *cache, dm_dblock_t b)
{
	unsigned long flags;

	BUG_ON(from_dblock(b) >= from_dblock(cache->discard_nr_blocks));
	atomic_inc(&cache->stats.discard_count);

	spin_lock_irqsave(&cache->lock, flags);
	set_bit(from_dblock(b), cache->discard_bitset);
	spin_unlock_irqrestore(&cache->lock, flags);
}

static void clear_discard(struct cache *cache, dm_dblock_t b)
{
	unsigned long flags;

	spin_lock_irqsave(&cache->lock, flags);
	clear_bit(from_dblock(b), cache->discard_bitset);
	spin_unlock_irqrestore(&cache->lock, flags);
}

static bool is_discarded(struct cache *cache, dm_dblock_t b)
{
	int r;
	unsigned long flags;

	spin_lock_irqsave(&cache->lock, flags);
	r = test_bit(from_dblock(b), cache->discard_bitset);
	spin_unlock_irqrestore(&cache->lock, flags);

	return r;
}

static bool is_discarded_oblock(struct cache *cache, dm_oblock_t b)
{
	int r;
	unsigned long flags;

	spin_lock_irqsave(&cache->lock, flags);
	r = test_bit(from_dblock(oblock_to_dblock(cache, b)),
		     cache->discard_bitset);
	spin_unlock_irqrestore(&cache->lock, flags);

	return r;
}

/*----------------------------------------------------------------
 * Remapping
 *--------------------------------------------------------------*/
static void remap_to_origin(struct cache *cache, struct bio *bio)
{
	bio_set_dev(bio, cache->origin_dev->bdev);
}

static void remap_to_cache(struct cache *cache, struct bio *bio,
			   dm_cblock_t cblock)
{
	sector_t bi_sector = bio->bi_iter.bi_sector;
	sector_t block = from_cblock(cblock);

	bio_set_dev(bio, cache->cache_dev->bdev);
	if (!block_size_is_power_of_two(cache))
		bio->bi_iter.bi_sector =
			(block * cache->sectors_per_block) +
			sector_div(bi_sector, cache->sectors_per_block);
	else
		bio->bi_iter.bi_sector =
			(block << cache->sectors_per_block_shift) |
			(bi_sector & (cache->sectors_per_block - 1));
}

static void check_if_tick_bio_needed(struct cache *cache, struct bio *bio)
{
	unsigned long flags;
	struct per_bio_data *pb;

	spin_lock_irqsave(&cache->lock, flags);
	if (cache->need_tick_bio && !op_is_flush(bio->bi_opf) &&
	    bio_op(bio) != REQ_OP_DISCARD) {
		pb = get_per_bio_data(bio);
		pb->tick = true;
		cache->need_tick_bio = false;
	}
	spin_unlock_irqrestore(&cache->lock, flags);
}

static void __remap_to_origin_clear_discard(struct cache *cache, struct bio *bio,
					    dm_oblock_t oblock, bool bio_has_pbd)
{
	if (bio_has_pbd)
		check_if_tick_bio_needed(cache, bio);
	remap_to_origin(cache, bio);
	if (bio_data_dir(bio) == WRITE)
		clear_discard(cache, oblock_to_dblock(cache, oblock));
}

static void remap_to_origin_clear_discard(struct cache *cache, struct bio *bio,
					  dm_oblock_t oblock)
{
	// FIXME: check_if_tick_bio_needed() is called way too much through this interface
	__remap_to_origin_clear_discard(cache, bio, oblock, true);
}

static void remap_to_cache_dirty(struct cache *cache, struct bio *bio,
				 dm_oblock_t oblock, dm_cblock_t cblock)
{
	check_if_tick_bio_needed(cache, bio);
	remap_to_cache(cache, bio, cblock);
	if (bio_data_dir(bio) == WRITE) {
		set_dirty(cache, cblock);
		clear_discard(cache, oblock_to_dblock(cache, oblock));
	}
}

static dm_oblock_t get_bio_block(struct cache *cache, struct bio *bio)
{
	sector_t block_nr = bio->bi_iter.bi_sector;

	if (!block_size_is_power_of_two(cache))
		(void) sector_div(block_nr, cache->sectors_per_block);
	else
		block_nr >>= cache->sectors_per_block_shift;

	return to_oblock(block_nr);
}

static bool accountable_bio(struct cache *cache, struct bio *bio)
{
	return bio_op(bio) != REQ_OP_DISCARD;
}

static void accounted_begin(struct cache *cache, struct bio *bio)
{
	struct per_bio_data *pb;

	if (accountable_bio(cache, bio)) {
		pb = get_per_bio_data(bio);
		pb->len = bio_sectors(bio);
		iot_io_begin(&cache->tracker, pb->len);
	}
}

static void accounted_complete(struct cache *cache, struct bio *bio)
{
	struct per_bio_data *pb = get_per_bio_data(bio);

	iot_io_end(&cache->tracker, pb->len);
}

static void accounted_request(struct cache *cache, struct bio *bio)
{
	accounted_begin(cache, bio);
	generic_make_request(bio);
}

static void issue_op(struct bio *bio, void *context)
{
	struct cache *cache = context;
	accounted_request(cache, bio);
}

/*
 * When running in writethrough mode we need to send writes to clean blocks
 * to both the cache and origin devices.  Clone the bio and send them in parallel.
 */
static void remap_to_origin_and_cache(struct cache *cache, struct bio *bio,
				      dm_oblock_t oblock, dm_cblock_t cblock)
{
	struct bio *origin_bio = bio_clone_fast(bio, GFP_NOIO, &cache->bs);

	BUG_ON(!origin_bio);

	bio_chain(origin_bio, bio);
	/*
	 * Passing false to __remap_to_origin_clear_discard() skips
	 * all code that might use per_bio_data (since clone doesn't have it)
	 */
	__remap_to_origin_clear_discard(cache, origin_bio, oblock, false);
	submit_bio(origin_bio);

	remap_to_cache(cache, bio, cblock);
}

/*----------------------------------------------------------------
 * Failure modes
 *--------------------------------------------------------------*/
static enum cache_metadata_mode get_cache_mode(struct cache *cache)
{
	return cache->features.mode;
}

static const char *cache_device_name(struct cache *cache)
{
	return dm_device_name(dm_table_get_md(cache->ti->table));
}

static void notify_mode_switch(struct cache *cache, enum cache_metadata_mode mode)
{
	const char *descs[] = {
		"write",
		"read-only",
		"fail"
	};

	dm_table_event(cache->ti->table);
	DMINFO("%s: switching cache to %s mode",
	       cache_device_name(cache), descs[(int)mode]);
}

static void set_cache_mode(struct cache *cache, enum cache_metadata_mode new_mode)
{
	bool needs_check;
	enum cache_metadata_mode old_mode = get_cache_mode(cache);

	if (dm_cache_metadata_needs_check(cache->cmd, &needs_check)) {
		DMERR("%s: unable to read needs_check flag, setting failure mode.",
		      cache_device_name(cache));
		new_mode = CM_FAIL;
	}

	if (new_mode == CM_WRITE && needs_check) {
		DMERR("%s: unable to switch cache to write mode until repaired.",
		      cache_device_name(cache));
		if (old_mode != new_mode)
			new_mode = old_mode;
		else
			new_mode = CM_READ_ONLY;
	}

	/* Never move out of fail mode */
	if (old_mode == CM_FAIL)
		new_mode = CM_FAIL;

	switch (new_mode) {
	case CM_FAIL:
	case CM_READ_ONLY:
		dm_cache_metadata_set_read_only(cache->cmd);
		break;

	case CM_WRITE:
		dm_cache_metadata_set_read_write(cache->cmd);
		break;
	}

	cache->features.mode = new_mode;

	if (new_mode != old_mode)
		notify_mode_switch(cache, new_mode);
}

static void abort_transaction(struct cache *cache)
{
	const char *dev_name = cache_device_name(cache);

	if (get_cache_mode(cache) >= CM_READ_ONLY)
		return;

	if (dm_cache_metadata_set_needs_check(cache->cmd)) {
		DMERR("%s: failed to set 'needs_check' flag in metadata", dev_name);
		set_cache_mode(cache, CM_FAIL);
	}

	DMERR_LIMIT("%s: aborting current metadata transaction", dev_name);
	if (dm_cache_metadata_abort(cache->cmd)) {
		DMERR("%s: failed to abort metadata transaction", dev_name);
		set_cache_mode(cache, CM_FAIL);
	}
}

static void metadata_operation_failed(struct cache *cache, const char *op, int r)
{
	DMERR_LIMIT("%s: metadata operation '%s' failed: error = %d",
		    cache_device_name(cache), op, r);
	abort_transaction(cache);
	set_cache_mode(cache, CM_READ_ONLY);
}

/*----------------------------------------------------------------*/

static void load_stats(struct cache *cache)
{
	struct dm_cache_statistics stats;

	dm_cache_metadata_get_stats(cache->cmd, &stats);
	atomic_set(&cache->stats.read_hit, stats.read_hits);
	atomic_set(&cache->stats.read_miss, stats.read_misses);
	atomic_set(&cache->stats.write_hit, stats.write_hits);
	atomic_set(&cache->stats.write_miss, stats.write_misses);
}

static void save_stats(struct cache *cache)
{
	struct dm_cache_statistics stats;

	if (get_cache_mode(cache) >= CM_READ_ONLY)
		return;

	stats.read_hits = atomic_read(&cache->stats.read_hit);
	stats.read_misses = atomic_read(&cache->stats.read_miss);
	stats.write_hits = atomic_read(&cache->stats.write_hit);
	stats.write_misses = atomic_read(&cache->stats.write_miss);

	dm_cache_metadata_set_stats(cache->cmd, &stats);
}

static void update_stats(struct cache_stats *stats, enum policy_operation op)
{
	switch (op) {
	case POLICY_PROMOTE:
		atomic_inc(&stats->promotion);
		break;

	case POLICY_DEMOTE:
		atomic_inc(&stats->demotion);
		break;

	case POLICY_WRITEBACK:
		atomic_inc(&stats->writeback);
		break;
	}
}

/*----------------------------------------------------------------
 * Migration processing
 *
 * Migration covers moving data from the origin device to the cache, or
 * vice versa.
 *--------------------------------------------------------------*/

static void inc_io_migrations(struct cache *cache)
{
	atomic_inc(&cache->nr_io_migrations);
}

static void dec_io_migrations(struct cache *cache)
{
	atomic_dec(&cache->nr_io_migrations);
}

static bool discard_or_flush(struct bio *bio)
{
	return bio_op(bio) == REQ_OP_DISCARD || op_is_flush(bio->bi_opf);
}

static void calc_discard_block_range(struct cache *cache, struct bio *bio,
				     dm_dblock_t *b, dm_dblock_t *e)
{
	sector_t sb = bio->bi_iter.bi_sector;
	sector_t se = bio_end_sector(bio);

	*b = to_dblock(dm_sector_div_up(sb, cache->discard_block_size));

	if (se - sb < cache->discard_block_size)
		*e = *b;
	else
		*e = to_dblock(block_div(se, cache->discard_block_size));
}

/*----------------------------------------------------------------*/

static void prevent_background_work(struct cache *cache)
{
	lockdep_off();
	down_write(&cache->background_work_lock);
	lockdep_on();
}

static void allow_background_work(struct cache *cache)
{
	lockdep_off();
	up_write(&cache->background_work_lock);
	lockdep_on();
}

static bool background_work_begin(struct cache *cache)
{
	bool r;

	lockdep_off();
	r = down_read_trylock(&cache->background_work_lock);
	lockdep_on();

	return r;
}

static void background_work_end(struct cache *cache)
{
	lockdep_off();
	up_read(&cache->background_work_lock);
	lockdep_on();
}

/*----------------------------------------------------------------*/

static bool bio_writes_complete_block(struct cache *cache, struct bio *bio)
{
	return (bio_data_dir(bio) == WRITE) &&
		(bio->bi_iter.bi_size == (cache->sectors_per_block << SECTOR_SHIFT));
}

static bool optimisable_bio(struct cache *cache, struct bio *bio, dm_oblock_t block)
{
	return writeback_mode(cache) &&
		(is_discarded_oblock(cache, block) || bio_writes_complete_block(cache, bio));
}

static void quiesce(struct dm_cache_migration *mg,
		    void (*continuation)(struct work_struct *))
{
	init_continuation(&mg->k, continuation);
	dm_cell_quiesce_v2(mg->cache->prison, mg->cell, &mg->k.ws);
}

static struct dm_cache_migration *ws_to_mg(struct work_struct *ws)
{
	struct continuation *k = container_of(ws, struct continuation, ws);
	return container_of(k, struct dm_cache_migration, k);
}

static void copy_complete(int read_err, unsigned long write_err, void *context)
{
	struct dm_cache_migration *mg = container_of(context, struct dm_cache_migration, k);

	if (read_err || write_err)
		mg->k.input = BLK_STS_IOERR;

	queue_continuation(mg->cache->wq, &mg->k);
}

static int copy(struct dm_cache_migration *mg, bool promote)
{
	int r;
	struct dm_io_region o_region, c_region;
	struct cache *cache = mg->cache;

	o_region.bdev = cache->origin_dev->bdev;
	o_region.sector = from_oblock(mg->op->oblock) * cache->sectors_per_block;
	o_region.count = cache->sectors_per_block;

	c_region.bdev = cache->cache_dev->bdev;
	c_region.sector = from_cblock(mg->op->cblock) * cache->sectors_per_block;
	c_region.count = cache->sectors_per_block;

	if (promote)
		r = dm_kcopyd_copy(cache->copier, &o_region, 1, &c_region, 0, copy_complete, &mg->k);
	else
		r = dm_kcopyd_copy(cache->copier, &c_region, 1, &o_region, 0, copy_complete, &mg->k);

	return r;
}

static void bio_drop_shared_lock(struct cache *cache, struct bio *bio)
{
	struct per_bio_data *pb = get_per_bio_data(bio);

	if (pb->cell && dm_cell_put_v2(cache->prison, pb->cell))
		free_prison_cell(cache, pb->cell);
	pb->cell = NULL;
}

static void overwrite_endio(struct bio *bio)
{
	struct dm_cache_migration *mg = bio->bi_private;
	struct cache *cache = mg->cache;
	struct per_bio_data *pb = get_per_bio_data(bio);

	dm_unhook_bio(&pb->hook_info, bio);

	if (bio->bi_status)
		mg->k.input = bio->bi_status;

	queue_continuation(cache->wq, &mg->k);
}

static void overwrite(struct dm_cache_migration *mg,
		      void (*continuation)(struct work_struct *))
{
	struct bio *bio = mg->overwrite_bio;
	struct per_bio_data *pb = get_per_bio_data(bio);

	dm_hook_bio(&pb->hook_info, bio, overwrite_endio, mg);

	/*
	 * The overwrite bio is part of the copy operation, as such it does
	 * not set/clear discard or dirty flags.
	 */
	if (mg->op->op == POLICY_PROMOTE)
		remap_to_cache(mg->cache, bio, mg->op->cblock);
	else
		remap_to_origin(mg->cache, bio);

	init_continuation(&mg->k, continuation);
	accounted_request(mg->cache, bio);
}

/*
 * Migration steps:
 *
 * 1) exclusive lock preventing WRITEs
 * 2) quiesce
 * 3) copy or issue overwrite bio
 * 4) upgrade to exclusive lock preventing READs and WRITEs
 * 5) quiesce
 * 6) update metadata and commit
 * 7) unlock
 */
static void mg_complete(struct dm_cache_migration *mg, bool success)
{
	struct bio_list bios;
	struct cache *cache = mg->cache;
	struct policy_work *op = mg->op;
	dm_cblock_t cblock = op->cblock;

	if (success)
		update_stats(&cache->stats, op->op);

	switch (op->op) {
	case POLICY_PROMOTE:
		clear_discard(cache, oblock_to_dblock(cache, op->oblock));
		policy_complete_background_work(cache->policy, op, success);

		if (mg->overwrite_bio) {
			if (success)
				force_set_dirty(cache, cblock);
			else if (mg->k.input)
				mg->overwrite_bio->bi_status = mg->k.input;
			else
				mg->overwrite_bio->bi_status = BLK_STS_IOERR;
			bio_endio(mg->overwrite_bio);
		} else {
			if (success)
				force_clear_dirty(cache, cblock);
			dec_io_migrations(cache);
		}
		break;

	case POLICY_DEMOTE:
		/*
		 * We clear dirty here to update the nr_dirty counter.
		 */
		if (success)
			force_clear_dirty(cache, cblock);
		policy_complete_background_work(cache->policy, op, success);
		dec_io_migrations(cache);
		break;

	case POLICY_WRITEBACK:
		if (success)
			force_clear_dirty(cache, cblock);
		policy_complete_background_work(cache->policy, op, success);
		dec_io_migrations(cache);
		break;
	}

	bio_list_init(&bios);
	if (mg->cell) {
		if (dm_cell_unlock_v2(cache->prison, mg->cell, &bios))
			free_prison_cell(cache, mg->cell);
	}

	free_migration(mg);
	defer_bios(cache, &bios);
	wake_migration_worker(cache);

	background_work_end(cache);
}

static void mg_success(struct work_struct *ws)
{
	struct dm_cache_migration *mg = ws_to_mg(ws);
	mg_complete(mg, mg->k.input == 0);
}

static void mg_update_metadata(struct work_struct *ws)
{
	int r;
	struct dm_cache_migration *mg = ws_to_mg(ws);
	struct cache *cache = mg->cache;
	struct policy_work *op = mg->op;

	switch (op->op) {
	case POLICY_PROMOTE:
		r = dm_cache_insert_mapping(cache->cmd, op->cblock, op->oblock);
		if (r) {
			DMERR_LIMIT("%s: migration failed; couldn't insert mapping",
				    cache_device_name(cache));
			metadata_operation_failed(cache, "dm_cache_insert_mapping", r);

			mg_complete(mg, false);
			return;
		}
		mg_complete(mg, true);
		break;

	case POLICY_DEMOTE:
		r = dm_cache_remove_mapping(cache->cmd, op->cblock);
		if (r) {
			DMERR_LIMIT("%s: migration failed; couldn't update on disk metadata",
				    cache_device_name(cache));
			metadata_operation_failed(cache, "dm_cache_remove_mapping", r);

			mg_complete(mg, false);
			return;
		}

		/*
		 * It would be nice if we only had to commit when a REQ_FLUSH
		 * comes through.  But there's one scenario that we have to
		 * look out for:
		 *
		 * - vblock x in a cache block
		 * - domotion occurs
		 * - cache block gets reallocated and over written
		 * - crash
		 *
		 * When we recover, because there was no commit the cache will
		 * rollback to having the data for vblock x in the cache block.
		 * But the cache block has since been overwritten, so it'll end
		 * up pointing to data that was never in 'x' during the history
		 * of the device.
		 *
		 * To avoid this issue we require a commit as part of the
		 * demotion operation.
		 */
		init_continuation(&mg->k, mg_success);
		continue_after_commit(&cache->committer, &mg->k);
		schedule_commit(&cache->committer);
		break;

	case POLICY_WRITEBACK:
		mg_complete(mg, true);
		break;
	}
}

static void mg_update_metadata_after_copy(struct work_struct *ws)
{
	struct dm_cache_migration *mg = ws_to_mg(ws);

	/*
	 * Did the copy succeed?
	 */
	if (mg->k.input)
		mg_complete(mg, false);
	else
		mg_update_metadata(ws);
}

static void mg_upgrade_lock(struct work_struct *ws)
{
	int r;
	struct dm_cache_migration *mg = ws_to_mg(ws);

	/*
	 * Did the copy succeed?
	 */
	if (mg->k.input)
		mg_complete(mg, false);

	else {
		/*
		 * Now we want the lock to prevent both reads and writes.
		 */
		r = dm_cell_lock_promote_v2(mg->cache->prison, mg->cell,
					    READ_WRITE_LOCK_LEVEL);
		if (r < 0)
			mg_complete(mg, false);

		else if (r)
			quiesce(mg, mg_update_metadata);

		else
			mg_update_metadata(ws);
	}
}

static void mg_full_copy(struct work_struct *ws)
{
	struct dm_cache_migration *mg = ws_to_mg(ws);
	struct cache *cache = mg->cache;
	struct policy_work *op = mg->op;
	bool is_policy_promote = (op->op == POLICY_PROMOTE);

	if ((!is_policy_promote && !is_dirty(cache, op->cblock)) ||
	    is_discarded_oblock(cache, op->oblock)) {
		mg_upgrade_lock(ws);
		return;
	}

	init_continuation(&mg->k, mg_upgrade_lock);

	if (copy(mg, is_policy_promote)) {
		DMERR_LIMIT("%s: migration copy failed", cache_device_name(cache));
		mg->k.input = BLK_STS_IOERR;
		mg_complete(mg, false);
	}
}

static void mg_copy(struct work_struct *ws)
{
	struct dm_cache_migration *mg = ws_to_mg(ws);

	if (mg->overwrite_bio) {
		/*
		 * No exclusive lock was held when we last checked if the bio
		 * was optimisable.  So we have to check again in case things
		 * have changed (eg, the block may no longer be discarded).
		 */
		if (!optimisable_bio(mg->cache, mg->overwrite_bio, mg->op->oblock)) {
			/*
			 * Fallback to a real full copy after doing some tidying up.
			 */
			bool rb = bio_detain_shared(mg->cache, mg->op->oblock, mg->overwrite_bio);
			BUG_ON(rb); /* An exclussive lock must _not_ be held for this block */
			mg->overwrite_bio = NULL;
			inc_io_migrations(mg->cache);
			mg_full_copy(ws);
			return;
		}

		/*
		 * It's safe to do this here, even though it's new data
		 * because all IO has been locked out of the block.
		 *
		 * mg_lock_writes() already took READ_WRITE_LOCK_LEVEL
		 * so _not_ using mg_upgrade_lock() as continutation.
		 */
		overwrite(mg, mg_update_metadata_after_copy);

	} else
		mg_full_copy(ws);
}

static int mg_lock_writes(struct dm_cache_migration *mg)
{
	int r;
	struct dm_cell_key_v2 key;
	struct cache *cache = mg->cache;
	struct dm_bio_prison_cell_v2 *prealloc;

	prealloc = alloc_prison_cell(cache);
	if (!prealloc) {
		DMERR_LIMIT("%s: alloc_prison_cell failed", cache_device_name(cache));
		mg_complete(mg, false);
		return -ENOMEM;
	}

	/*
	 * Prevent writes to the block, but allow reads to continue.
	 * Unless we're using an overwrite bio, in which case we lock
	 * everything.
	 */
	build_key(mg->op->oblock, oblock_succ(mg->op->oblock), &key);
	r = dm_cell_lock_v2(cache->prison, &key,
			    mg->overwrite_bio ?  READ_WRITE_LOCK_LEVEL : WRITE_LOCK_LEVEL,
			    prealloc, &mg->cell);
	if (r < 0) {
		free_prison_cell(cache, prealloc);
		mg_complete(mg, false);
		return r;
	}

	if (mg->cell != prealloc)
		free_prison_cell(cache, prealloc);

	if (r == 0)
		mg_copy(&mg->k.ws);
	else
		quiesce(mg, mg_copy);

	return 0;
}

static int mg_start(struct cache *cache, struct policy_work *op, struct bio *bio)
{
	struct dm_cache_migration *mg;

	if (!background_work_begin(cache)) {
		policy_complete_background_work(cache->policy, op, false);
		return -EPERM;
	}

	mg = alloc_migration(cache);
	if (!mg) {
		policy_complete_background_work(cache->policy, op, false);
		background_work_end(cache);
		return -ENOMEM;
	}

	mg->op = op;
	mg->overwrite_bio = bio;

	if (!bio)
		inc_io_migrations(cache);

	return mg_lock_writes(mg);
}

/*----------------------------------------------------------------
 * invalidation processing
 *--------------------------------------------------------------*/

static void invalidate_complete(struct dm_cache_migration *mg, bool success)
{
	struct bio_list bios;
	struct cache *cache = mg->cache;

	bio_list_init(&bios);
	if (dm_cell_unlock_v2(cache->prison, mg->cell, &bios))
		free_prison_cell(cache, mg->cell);

	if (!success && mg->overwrite_bio)
		bio_io_error(mg->overwrite_bio);

	free_migration(mg);
	defer_bios(cache, &bios);

	background_work_end(cache);
}

static void invalidate_completed(struct work_struct *ws)
{
	struct dm_cache_migration *mg = ws_to_mg(ws);
	invalidate_complete(mg, !mg->k.input);
}

static int invalidate_cblock(struct cache *cache, dm_cblock_t cblock)
{
	int r = policy_invalidate_mapping(cache->policy, cblock);
	if (!r) {
		r = dm_cache_remove_mapping(cache->cmd, cblock);
		if (r) {
			DMERR_LIMIT("%s: invalidation failed; couldn't update on disk metadata",
				    cache_device_name(cache));
			metadata_operation_failed(cache, "dm_cache_remove_mapping", r);
		}

	} else if (r == -ENODATA) {
		/*
		 * Harmless, already unmapped.
		 */
		r = 0;

	} else
		DMERR("%s: policy_invalidate_mapping failed", cache_device_name(cache));

	return r;
}

static void invalidate_remove(struct work_struct *ws)
{
	int r;
	struct dm_cache_migration *mg = ws_to_mg(ws);
	struct cache *cache = mg->cache;

	r = invalidate_cblock(cache, mg->invalidate_cblock);
	if (r) {
		invalidate_complete(mg, false);
		return;
	}

	init_continuation(&mg->k, invalidate_completed);
	continue_after_commit(&cache->committer, &mg->k);
	remap_to_origin_clear_discard(cache, mg->overwrite_bio, mg->invalidate_oblock);
	mg->overwrite_bio = NULL;
	schedule_commit(&cache->committer);
}

static int invalidate_lock(struct dm_cache_migration *mg)
{
	int r;
	struct dm_cell_key_v2 key;
	struct cache *cache = mg->cache;
	struct dm_bio_prison_cell_v2 *prealloc;

	prealloc = alloc_prison_cell(cache);
	if (!prealloc) {
		invalidate_complete(mg, false);
		return -ENOMEM;
	}

	build_key(mg->invalidate_oblock, oblock_succ(mg->invalidate_oblock), &key);
	r = dm_cell_lock_v2(cache->prison, &key,
			    READ_WRITE_LOCK_LEVEL, prealloc, &mg->cell);
	if (r < 0) {
		free_prison_cell(cache, prealloc);
		invalidate_complete(mg, false);
		return r;
	}

	if (mg->cell != prealloc)
		free_prison_cell(cache, prealloc);

	if (r)
		quiesce(mg, invalidate_remove);

	else {
		/*
		 * We can't call invalidate_remove() directly here because we
		 * might still be in request context.
		 */
		init_continuation(&mg->k, invalidate_remove);
		queue_work(cache->wq, &mg->k.ws);
	}

	return 0;
}

static int invalidate_start(struct cache *cache, dm_cblock_t cblock,
			    dm_oblock_t oblock, struct bio *bio)
{
	struct dm_cache_migration *mg;

	if (!background_work_begin(cache))
		return -EPERM;

	mg = alloc_migration(cache);
	if (!mg) {
		background_work_end(cache);
		return -ENOMEM;
	}

	mg->overwrite_bio = bio;
	mg->invalidate_cblock = cblock;
	mg->invalidate_oblock = oblock;

	return invalidate_lock(mg);
}

/*----------------------------------------------------------------
 * bio processing
 *--------------------------------------------------------------*/

enum busy {
	IDLE,
	BUSY
};

static enum busy spare_migration_bandwidth(struct cache *cache)
{
	bool idle = iot_idle_for(&cache->tracker, HZ);
	sector_t current_volume = (atomic_read(&cache->nr_io_migrations) + 1) *
		cache->sectors_per_block;

	if (idle && current_volume <= cache->migration_threshold)
		return IDLE;
	else
		return BUSY;
}

static void inc_hit_counter(struct cache *cache, struct bio *bio)
{
	atomic_inc(bio_data_dir(bio) == READ ?
		   &cache->stats.read_hit : &cache->stats.write_hit);
}

static void inc_miss_counter(struct cache *cache, struct bio *bio)
{
	atomic_inc(bio_data_dir(bio) == READ ?
		   &cache->stats.read_miss : &cache->stats.write_miss);
}

/*----------------------------------------------------------------*/

static int map_bio(struct cache *cache, struct bio *bio, dm_oblock_t block,
		   bool *commit_needed)
{
	int r, data_dir;
	bool rb, background_queued;
	dm_cblock_t cblock;

	*commit_needed = false;

	rb = bio_detain_shared(cache, block, bio);
	if (!rb) {
		/*
		 * An exclusive lock is held for this block, so we have to
		 * wait.  We set the commit_needed flag so the current
		 * transaction will be committed asap, allowing this lock
		 * to be dropped.
		 */
		*commit_needed = true;
		return DM_MAPIO_SUBMITTED;
	}

	data_dir = bio_data_dir(bio);

	if (optimisable_bio(cache, bio, block)) {
		struct policy_work *op = NULL;

		r = policy_lookup_with_work(cache->policy, block, &cblock, data_dir, true, &op);
		if (unlikely(r && r != -ENOENT)) {
			DMERR_LIMIT("%s: policy_lookup_with_work() failed with r = %d",
				    cache_device_name(cache), r);
			bio_io_error(bio);
			return DM_MAPIO_SUBMITTED;
		}

		if (r == -ENOENT && op) {
			bio_drop_shared_lock(cache, bio);
			BUG_ON(op->op != POLICY_PROMOTE);
			mg_start(cache, op, bio);
			return DM_MAPIO_SUBMITTED;
		}
	} else {
		r = policy_lookup(cache->policy, block, &cblock, data_dir, false, &background_queued);
		if (unlikely(r && r != -ENOENT)) {
			DMERR_LIMIT("%s: policy_lookup() failed with r = %d",
				    cache_device_name(cache), r);
			bio_io_error(bio);
			return DM_MAPIO_SUBMITTED;
		}

		if (background_queued)
			wake_migration_worker(cache);
	}

	if (r == -ENOENT) {
		struct per_bio_data *pb = get_per_bio_data(bio);

		/*
		 * Miss.
		 */
		inc_miss_counter(cache, bio);
		if (pb->req_nr == 0) {
			accounted_begin(cache, bio);
			remap_to_origin_clear_discard(cache, bio, block);
		} else {
			/*
			 * This is a duplicate writethrough io that is no
			 * longer needed because the block has been demoted.
			 */
			bio_endio(bio);
			return DM_MAPIO_SUBMITTED;
		}
	} else {
		/*
		 * Hit.
		 */
		inc_hit_counter(cache, bio);

		/*
		 * Passthrough always maps to the origin, invalidating any
		 * cache blocks that are written to.
		 */
		if (passthrough_mode(cache)) {
			if (bio_data_dir(bio) == WRITE) {
				bio_drop_shared_lock(cache, bio);
				atomic_inc(&cache->stats.demotion);
				invalidate_start(cache, cblock, block, bio);
			} else
				remap_to_origin_clear_discard(cache, bio, block);
		} else {
			if (bio_data_dir(bio) == WRITE && writethrough_mode(cache) &&
			    !is_dirty(cache, cblock)) {
				remap_to_origin_and_cache(cache, bio, block, cblock);
				accounted_begin(cache, bio);
			} else
				remap_to_cache_dirty(cache, bio, block, cblock);
		}
	}

	/*
	 * dm core turns FUA requests into a separate payload and FLUSH req.
	 */
	if (bio->bi_opf & REQ_FUA) {
		/*
		 * issue_after_commit will call accounted_begin a second time.  So
		 * we call accounted_complete() to avoid double accounting.
		 */
		accounted_complete(cache, bio);
		issue_after_commit(&cache->committer, bio);
		*commit_needed = true;
		return DM_MAPIO_SUBMITTED;
	}

	return DM_MAPIO_REMAPPED;
}

static bool process_bio(struct cache *cache, struct bio *bio)
{
	bool commit_needed;

	if (map_bio(cache, bio, get_bio_block(cache, bio), &commit_needed) == DM_MAPIO_REMAPPED)
		generic_make_request(bio);

	return commit_needed;
}

/*
 * A non-zero return indicates read_only or fail_io mode.
 */
static int commit(struct cache *cache, bool clean_shutdown)
{
	int r;

	if (get_cache_mode(cache) >= CM_READ_ONLY)
		return -EINVAL;

	atomic_inc(&cache->stats.commit_count);
	r = dm_cache_commit(cache->cmd, clean_shutdown);
	if (r)
		metadata_operation_failed(cache, "dm_cache_commit", r);

	return r;
}

/*
 * Used by the batcher.
 */
static blk_status_t commit_op(void *context)
{
	struct cache *cache = context;

	if (dm_cache_changed_this_transaction(cache->cmd))
		return errno_to_blk_status(commit(cache, false));

	return 0;
}

/*----------------------------------------------------------------*/

static bool process_flush_bio(struct cache *cache, struct bio *bio)
{
	struct per_bio_data *pb = get_per_bio_data(bio);

	if (!pb->req_nr)
		remap_to_origin(cache, bio);
	else
		remap_to_cache(cache, bio, 0);

	issue_after_commit(&cache->committer, bio);
	return true;
}

static bool process_discard_bio(struct cache *cache, struct bio *bio)
{
	dm_dblock_t b, e;

	// FIXME: do we need to lock the region?  Or can we just assume the
	// user wont be so foolish as to issue discard concurrently with
	// other IO?
	calc_discard_block_range(cache, bio, &b, &e);
	while (b != e) {
		set_discard(cache, b);
		b = to_dblock(from_dblock(b) + 1);
	}

	bio_endio(bio);

	return false;
}

static void process_deferred_bios(struct work_struct *ws)
{
	struct cache *cache = container_of(ws, struct cache, deferred_bio_worker);

	unsigned long flags;
	bool commit_needed = false;
	struct bio_list bios;
	struct bio *bio;

	bio_list_init(&bios);

	spin_lock_irqsave(&cache->lock, flags);
	bio_list_merge(&bios, &cache->deferred_bios);
	bio_list_init(&cache->deferred_bios);
	spin_unlock_irqrestore(&cache->lock, flags);

	while ((bio = bio_list_pop(&bios))) {
		if (bio->bi_opf & REQ_PREFLUSH)
			commit_needed = process_flush_bio(cache, bio) || commit_needed;

		else if (bio_op(bio) == REQ_OP_DISCARD)
			commit_needed = process_discard_bio(cache, bio) || commit_needed;

		else
			commit_needed = process_bio(cache, bio) || commit_needed;
	}

	if (commit_needed)
		schedule_commit(&cache->committer);
}

/*----------------------------------------------------------------
 * Main worker loop
 *--------------------------------------------------------------*/

static void requeue_deferred_bios(struct cache *cache)
{
	struct bio *bio;
	struct bio_list bios;

	bio_list_init(&bios);
	bio_list_merge(&bios, &cache->deferred_bios);
	bio_list_init(&cache->deferred_bios);

	while ((bio = bio_list_pop(&bios))) {
		bio->bi_status = BLK_STS_DM_REQUEUE;
		bio_endio(bio);
	}
}

/*
 * We want to commit periodically so that not too much
 * unwritten metadata builds up.
 */
static void do_waker(struct work_struct *ws)
{
	struct cache *cache = container_of(to_delayed_work(ws), struct cache, waker);

	policy_tick(cache->policy, true);
	wake_migration_worker(cache);
	schedule_commit(&cache->committer);
	queue_delayed_work(cache->wq, &cache->waker, COMMIT_PERIOD);
}

static void check_migrations(struct work_struct *ws)
{
	int r;
	struct policy_work *op;
	struct cache *cache = container_of(ws, struct cache, migration_worker);
	enum busy b;

	for (;;) {
		b = spare_migration_bandwidth(cache);

		r = policy_get_background_work(cache->policy, b == IDLE, &op);
		if (r == -ENODATA)
			break;

		if (r) {
			DMERR_LIMIT("%s: policy_background_work failed",
				    cache_device_name(cache));
			break;
		}

		r = mg_start(cache, op, NULL);
		if (r)
			break;
	}
}

/*----------------------------------------------------------------
 * Target methods
 *--------------------------------------------------------------*/

/*
 * This function gets called on the error paths of the constructor, so we
 * have to cope with a partially initialised struct.
 */
static void destroy(struct cache *cache)
{
	unsigned i;

	mempool_exit(&cache->migration_pool);

	if (cache->prison)
		dm_bio_prison_destroy_v2(cache->prison);

	if (cache->wq)
		destroy_workqueue(cache->wq);

	if (cache->dirty_bitset)
		free_bitset(cache->dirty_bitset);

	if (cache->discard_bitset)
		free_bitset(cache->discard_bitset);

	if (cache->copier)
		dm_kcopyd_client_destroy(cache->copier);

	if (cache->cmd)
		dm_cache_metadata_close(cache->cmd);

	if (cache->metadata_dev)
		dm_put_device(cache->ti, cache->metadata_dev);

	if (cache->origin_dev)
		dm_put_device(cache->ti, cache->origin_dev);

	if (cache->cache_dev)
		dm_put_device(cache->ti, cache->cache_dev);

	if (cache->policy)
		dm_cache_policy_destroy(cache->policy);

	for (i = 0; i < cache->nr_ctr_args ; i++)
		kfree(cache->ctr_args[i]);
	kfree(cache->ctr_args);

	bioset_exit(&cache->bs);

	kfree(cache);
}

static void cache_dtr(struct dm_target *ti)
{
	struct cache *cache = ti->private;

	destroy(cache);
}

static sector_t get_dev_size(struct dm_dev *dev)
{
	return i_size_read(dev->bdev->bd_inode) >> SECTOR_SHIFT;
}

/*----------------------------------------------------------------*/

/*
 * Construct a cache device mapping.
 *
 * cache <metadata dev> <cache dev> <origin dev> <block size>
 *       <#feature args> [<feature arg>]*
 *       <policy> <#policy args> [<policy arg>]*
 *
 * metadata dev    : fast device holding the persistent metadata
 * cache dev	   : fast device holding cached data blocks
 * origin dev	   : slow device holding original data blocks
 * block size	   : cache unit size in sectors
 *
 * #feature args   : number of feature arguments passed
 * feature args    : writethrough.  (The default is writeback.)
 *
 * policy	   : the replacement policy to use
 * #policy args    : an even number of policy arguments corresponding
 *		     to key/value pairs passed to the policy
 * policy args	   : key/value pairs passed to the policy
 *		     E.g. 'sequential_threshold 1024'
 *		     See cache-policies.txt for details.
 *
 * Optional feature arguments are:
 *   writethrough  : write through caching that prohibits cache block
 *		     content from being different from origin block content.
 *		     Without this argument, the default behaviour is to write
 *		     back cache block contents later for performance reasons,
 *		     so they may differ from the corresponding origin blocks.
 */
struct cache_args {
	struct dm_target *ti;

	struct dm_dev *metadata_dev;

	struct dm_dev *cache_dev;
	sector_t cache_sectors;

	struct dm_dev *origin_dev;
	sector_t origin_sectors;

	uint32_t block_size;

	const char *policy_name;
	int policy_argc;
	const char **policy_argv;

	struct cache_features features;
};

static void destroy_cache_args(struct cache_args *ca)
{
	if (ca->metadata_dev)
		dm_put_device(ca->ti, ca->metadata_dev);

	if (ca->cache_dev)
		dm_put_device(ca->ti, ca->cache_dev);

	if (ca->origin_dev)
		dm_put_device(ca->ti, ca->origin_dev);

	kfree(ca);
}

static bool at_least_one_arg(struct dm_arg_set *as, char **error)
{
	if (!as->argc) {
		*error = "Insufficient args";
		return false;
	}

	return true;
}

static int parse_metadata_dev(struct cache_args *ca, struct dm_arg_set *as,
			      char **error)
{
	int r;
	sector_t metadata_dev_size;
	char b[BDEVNAME_SIZE];

	if (!at_least_one_arg(as, error))
		return -EINVAL;

	r = dm_get_device(ca->ti, dm_shift_arg(as), FMODE_READ | FMODE_WRITE,
			  &ca->metadata_dev);
	if (r) {
		*error = "Error opening metadata device";
		return r;
	}

	metadata_dev_size = get_dev_size(ca->metadata_dev);
	if (metadata_dev_size > DM_CACHE_METADATA_MAX_SECTORS_WARNING)
		DMWARN("Metadata device %s is larger than %u sectors: excess space will not be used.",
		       bdevname(ca->metadata_dev->bdev, b), THIN_METADATA_MAX_SECTORS);

	return 0;
}

static int parse_cache_dev(struct cache_args *ca, struct dm_arg_set *as,
			   char **error)
{
	int r;

	if (!at_least_one_arg(as, error))
		return -EINVAL;

	r = dm_get_device(ca->ti, dm_shift_arg(as), FMODE_READ | FMODE_WRITE,
			  &ca->cache_dev);
	if (r) {
		*error = "Error opening cache device";
		return r;
	}
	ca->cache_sectors = get_dev_size(ca->cache_dev);

	return 0;
}

static int parse_origin_dev(struct cache_args *ca, struct dm_arg_set *as,
			    char **error)
{
	int r;

	if (!at_least_one_arg(as, error))
		return -EINVAL;

	r = dm_get_device(ca->ti, dm_shift_arg(as), FMODE_READ | FMODE_WRITE,
			  &ca->origin_dev);
	if (r) {
		*error = "Error opening origin device";
		return r;
	}

	ca->origin_sectors = get_dev_size(ca->origin_dev);
	if (ca->ti->len > ca->origin_sectors) {
		*error = "Device size larger than cached device";
		return -EINVAL;
	}

	return 0;
}

static int parse_block_size(struct cache_args *ca, struct dm_arg_set *as,
			    char **error)
{
	unsigned long block_size;

	if (!at_least_one_arg(as, error))
		return -EINVAL;

	if (kstrtoul(dm_shift_arg(as), 10, &block_size) || !block_size ||
	    block_size < DATA_DEV_BLOCK_SIZE_MIN_SECTORS ||
	    block_size > DATA_DEV_BLOCK_SIZE_MAX_SECTORS ||
	    block_size & (DATA_DEV_BLOCK_SIZE_MIN_SECTORS - 1)) {
		*error = "Invalid data block size";
		return -EINVAL;
	}

	if (block_size > ca->cache_sectors) {
		*error = "Data block size is larger than the cache device";
		return -EINVAL;
	}

	ca->block_size = block_size;

	return 0;
}

static void init_features(struct cache_features *cf)
{
	cf->mode = CM_WRITE;
	cf->io_mode = CM_IO_WRITEBACK;
	cf->metadata_version = 1;
}

static int parse_features(struct cache_args *ca, struct dm_arg_set *as,
			  char **error)
{
	static const struct dm_arg _args[] = {
		{0, 2, "Invalid number of cache feature arguments"},
	};

	int r;
	unsigned argc;
	const char *arg;
	struct cache_features *cf = &ca->features;

	init_features(cf);

	r = dm_read_arg_group(_args, as, &argc, error);
	if (r)
		return -EINVAL;

	while (argc--) {
		arg = dm_shift_arg(as);

		if (!strcasecmp(arg, "writeback"))
			cf->io_mode = CM_IO_WRITEBACK;

		else if (!strcasecmp(arg, "writethrough"))
			cf->io_mode = CM_IO_WRITETHROUGH;

		else if (!strcasecmp(arg, "passthrough"))
			cf->io_mode = CM_IO_PASSTHROUGH;

		else if (!strcasecmp(arg, "metadata2"))
			cf->metadata_version = 2;

		else {
			*error = "Unrecognised cache feature requested";
			return -EINVAL;
		}
	}

	return 0;
}

static int parse_policy(struct cache_args *ca, struct dm_arg_set *as,
			char **error)
{
	static const struct dm_arg _args[] = {
		{0, 1024, "Invalid number of policy arguments"},
	};

	int r;

	if (!at_least_one_arg(as, error))
		return -EINVAL;

	ca->policy_name = dm_shift_arg(as);

	r = dm_read_arg_group(_args, as, &ca->policy_argc, error);
	if (r)
		return -EINVAL;

	ca->policy_argv = (const char **)as->argv;
	dm_consume_args(as, ca->policy_argc);

	return 0;
}

static int parse_cache_args(struct cache_args *ca, int argc, char **argv,
			    char **error)
{
	int r;
	struct dm_arg_set as;

	as.argc = argc;
	as.argv = argv;

	r = parse_metadata_dev(ca, &as, error);
	if (r)
		return r;

	r = parse_cache_dev(ca, &as, error);
	if (r)
		return r;

	r = parse_origin_dev(ca, &as, error);
	if (r)
		return r;

	r = parse_block_size(ca, &as, error);
	if (r)
		return r;

	r = parse_features(ca, &as, error);
	if (r)
		return r;

	r = parse_policy(ca, &as, error);
	if (r)
		return r;

	return 0;
}

/*----------------------------------------------------------------*/

static struct kmem_cache *migration_cache;

#define NOT_CORE_OPTION 1

static int process_config_option(struct cache *cache, const char *key, const char *value)
{
	unsigned long tmp;

	if (!strcasecmp(key, "migration_threshold")) {
		if (kstrtoul(value, 10, &tmp))
			return -EINVAL;

		cache->migration_threshold = tmp;
		return 0;
	}

	return NOT_CORE_OPTION;
}

static int set_config_value(struct cache *cache, const char *key, const char *value)
{
	int r = process_config_option(cache, key, value);

	if (r == NOT_CORE_OPTION)
		r = policy_set_config_value(cache->policy, key, value);

	if (r)
		DMWARN("bad config value for %s: %s", key, value);

	return r;
}

static int set_config_values(struct cache *cache, int argc, const char **argv)
{
	int r = 0;

	if (argc & 1) {
		DMWARN("Odd number of policy arguments given but they should be <key> <value> pairs.");
		return -EINVAL;
	}

	while (argc) {
		r = set_config_value(cache, argv[0], argv[1]);
		if (r)
			break;

		argc -= 2;
		argv += 2;
	}

	return r;
}

static int create_cache_policy(struct cache *cache, struct cache_args *ca,
			       char **error)
{
	struct dm_cache_policy *p = dm_cache_policy_create(ca->policy_name,
							   cache->cache_size,
							   cache->origin_sectors,
							   cache->sectors_per_block);
	if (IS_ERR(p)) {
		*error = "Error creating cache's policy";
		return PTR_ERR(p);
	}
	cache->policy = p;
	BUG_ON(!cache->policy);

	return 0;
}

/*
 * We want the discard block size to be at least the size of the cache
 * block size and have no more than 2^14 discard blocks across the origin.
 */
#define MAX_DISCARD_BLOCKS (1 << 14)

static bool too_many_discard_blocks(sector_t discard_block_size,
				    sector_t origin_size)
{
	(void) sector_div(origin_size, discard_block_size);

	return origin_size > MAX_DISCARD_BLOCKS;
}

static sector_t calculate_discard_block_size(sector_t cache_block_size,
					     sector_t origin_size)
{
	sector_t discard_block_size = cache_block_size;

	if (origin_size)
		while (too_many_discard_blocks(discard_block_size, origin_size))
			discard_block_size *= 2;

	return discard_block_size;
}

static void set_cache_size(struct cache *cache, dm_cblock_t size)
{
	dm_block_t nr_blocks = from_cblock(size);

	if (nr_blocks > (1 << 20) && cache->cache_size != size)
		DMWARN_LIMIT("You have created a cache device with a lot of individual cache blocks (%llu)\n"
			     "All these mappings can consume a lot of kernel memory, and take some time to read/write.\n"
			     "Please consider increasing the cache block size to reduce the overall cache block count.",
			     (unsigned long long) nr_blocks);

	cache->cache_size = size;
}

static int is_congested(struct dm_dev *dev, int bdi_bits)
{
	struct request_queue *q = bdev_get_queue(dev->bdev);
	return bdi_congested(q->backing_dev_info, bdi_bits);
}

static int cache_is_congested(struct dm_target_callbacks *cb, int bdi_bits)
{
	struct cache *cache = container_of(cb, struct cache, callbacks);

	return is_congested(cache->origin_dev, bdi_bits) ||
		is_congested(cache->cache_dev, bdi_bits);
}

#define DEFAULT_MIGRATION_THRESHOLD 2048

static int cache_create(struct cache_args *ca, struct cache **result)
{
	int r = 0;
	char **error = &ca->ti->error;
	struct cache *cache;
	struct dm_target *ti = ca->ti;
	dm_block_t origin_blocks;
	struct dm_cache_metadata *cmd;
	bool may_format = ca->features.mode == CM_WRITE;

	cache = kzalloc(sizeof(*cache), GFP_KERNEL);
	if (!cache)
		return -ENOMEM;

	cache->ti = ca->ti;
	ti->private = cache;
	ti->num_flush_bios = 2;
	ti->flush_supported = true;

	ti->num_discard_bios = 1;
	ti->discards_supported = true;
	ti->split_discard_bios = false;

	ti->per_io_data_size = sizeof(struct per_bio_data);

	cache->features = ca->features;
	if (writethrough_mode(cache)) {
		/* Create bioset for writethrough bios issued to origin */
		r = bioset_init(&cache->bs, BIO_POOL_SIZE, 0, 0);
		if (r)
			goto bad;
	}

	cache->callbacks.congested_fn = cache_is_congested;
	dm_table_add_target_callbacks(ti->table, &cache->callbacks);

	cache->metadata_dev = ca->metadata_dev;
	cache->origin_dev = ca->origin_dev;
	cache->cache_dev = ca->cache_dev;

	ca->metadata_dev = ca->origin_dev = ca->cache_dev = NULL;

	origin_blocks = cache->origin_sectors = ca->origin_sectors;
	origin_blocks = block_div(origin_blocks, ca->block_size);
	cache->origin_blocks = to_oblock(origin_blocks);

	cache->sectors_per_block = ca->block_size;
	if (dm_set_target_max_io_len(ti, cache->sectors_per_block)) {
		r = -EINVAL;
		goto bad;
	}

	if (ca->block_size & (ca->block_size - 1)) {
		dm_block_t cache_size = ca->cache_sectors;

		cache->sectors_per_block_shift = -1;
		cache_size = block_div(cache_size, ca->block_size);
		set_cache_size(cache, to_cblock(cache_size));
	} else {
		cache->sectors_per_block_shift = __ffs(ca->block_size);
		set_cache_size(cache, to_cblock(ca->cache_sectors >> cache->sectors_per_block_shift));
	}

	r = create_cache_policy(cache, ca, error);
	if (r)
		goto bad;

	cache->policy_nr_args = ca->policy_argc;
	cache->migration_threshold = DEFAULT_MIGRATION_THRESHOLD;

	r = set_config_values(cache, ca->policy_argc, ca->policy_argv);
	if (r) {
		*error = "Error setting cache policy's config values";
		goto bad;
	}

	cmd = dm_cache_metadata_open(cache->metadata_dev->bdev,
				     ca->block_size, may_format,
				     dm_cache_policy_get_hint_size(cache->policy),
				     ca->features.metadata_version);
	if (IS_ERR(cmd)) {
		*error = "Error creating metadata object";
		r = PTR_ERR(cmd);
		goto bad;
	}
	cache->cmd = cmd;
	set_cache_mode(cache, CM_WRITE);
	if (get_cache_mode(cache) != CM_WRITE) {
		*error = "Unable to get write access to metadata, please check/repair metadata.";
		r = -EINVAL;
		goto bad;
	}

	if (passthrough_mode(cache)) {
		bool all_clean;

		r = dm_cache_metadata_all_clean(cache->cmd, &all_clean);
		if (r) {
			*error = "dm_cache_metadata_all_clean() failed";
			goto bad;
		}

		if (!all_clean) {
			*error = "Cannot enter passthrough mode unless all blocks are clean";
			r = -EINVAL;
			goto bad;
		}

		policy_allow_migrations(cache->policy, false);
	}

	spin_lock_init(&cache->lock);
	bio_list_init(&cache->deferred_bios);
	atomic_set(&cache->nr_allocated_migrations, 0);
	atomic_set(&cache->nr_io_migrations, 0);
	init_waitqueue_head(&cache->migration_wait);

	r = -ENOMEM;
	atomic_set(&cache->nr_dirty, 0);
	cache->dirty_bitset = alloc_bitset(from_cblock(cache->cache_size));
	if (!cache->dirty_bitset) {
		*error = "could not allocate dirty bitset";
		goto bad;
	}
	clear_bitset(cache->dirty_bitset, from_cblock(cache->cache_size));

	cache->discard_block_size =
		calculate_discard_block_size(cache->sectors_per_block,
					     cache->origin_sectors);
	cache->discard_nr_blocks = to_dblock(dm_sector_div_up(cache->origin_sectors,
							      cache->discard_block_size));
	cache->discard_bitset = alloc_bitset(from_dblock(cache->discard_nr_blocks));
	if (!cache->discard_bitset) {
		*error = "could not allocate discard bitset";
		goto bad;
	}
	clear_bitset(cache->discard_bitset, from_dblock(cache->discard_nr_blocks));

	cache->copier = dm_kcopyd_client_create(&dm_kcopyd_throttle);
	if (IS_ERR(cache->copier)) {
		*error = "could not create kcopyd client";
		r = PTR_ERR(cache->copier);
		goto bad;
	}

	cache->wq = alloc_workqueue("dm-" DM_MSG_PREFIX, WQ_MEM_RECLAIM, 0);
	if (!cache->wq) {
		*error = "could not create workqueue for metadata object";
		goto bad;
	}
	INIT_WORK(&cache->deferred_bio_worker, process_deferred_bios);
	INIT_WORK(&cache->migration_worker, check_migrations);
	INIT_DELAYED_WORK(&cache->waker, do_waker);

	cache->prison = dm_bio_prison_create_v2(cache->wq);
	if (!cache->prison) {
		*error = "could not create bio prison";
		goto bad;
	}

	r = mempool_init_slab_pool(&cache->migration_pool, MIGRATION_POOL_SIZE,
				   migration_cache);
	if (r) {
		*error = "Error creating cache's migration mempool";
		goto bad;
	}

	cache->need_tick_bio = true;
	cache->sized = false;
	cache->invalidate = false;
	cache->commit_requested = false;
	cache->loaded_mappings = false;
	cache->loaded_discards = false;

	load_stats(cache);

	atomic_set(&cache->stats.demotion, 0);
	atomic_set(&cache->stats.promotion, 0);
	atomic_set(&cache->stats.copies_avoided, 0);
	atomic_set(&cache->stats.cache_cell_clash, 0);
	atomic_set(&cache->stats.commit_count, 0);
	atomic_set(&cache->stats.discard_count, 0);

	spin_lock_init(&cache->invalidation_lock);
	INIT_LIST_HEAD(&cache->invalidation_requests);

	batcher_init(&cache->committer, commit_op, cache,
		     issue_op, cache, cache->wq);
	iot_init(&cache->tracker);

	init_rwsem(&cache->background_work_lock);
	prevent_background_work(cache);

	*result = cache;
	return 0;
bad:
	destroy(cache);
	return r;
}

static int copy_ctr_args(struct cache *cache, int argc, const char **argv)
{
	unsigned i;
	const char **copy;

	copy = kcalloc(argc, sizeof(*copy), GFP_KERNEL);
	if (!copy)
		return -ENOMEM;
	for (i = 0; i < argc; i++) {
		copy[i] = kstrdup(argv[i], GFP_KERNEL);
		if (!copy[i]) {
			while (i--)
				kfree(copy[i]);
			kfree(copy);
			return -ENOMEM;
		}
	}

	cache->nr_ctr_args = argc;
	cache->ctr_args = copy;

	return 0;
}

static int cache_ctr(struct dm_target *ti, unsigned argc, char **argv)
{
	int r = -EINVAL;
	struct cache_args *ca;
	struct cache *cache = NULL;

	ca = kzalloc(sizeof(*ca), GFP_KERNEL);
	if (!ca) {
		ti->error = "Error allocating memory for cache";
		return -ENOMEM;
	}
	ca->ti = ti;

	r = parse_cache_args(ca, argc, argv, &ti->error);
	if (r)
		goto out;

	r = cache_create(ca, &cache);
	if (r)
		goto out;

	r = copy_ctr_args(cache, argc - 3, (const char **)argv + 3);
	if (r) {
		destroy(cache);
		goto out;
	}

	ti->private = cache;
out:
	destroy_cache_args(ca);
	return r;
}

/*----------------------------------------------------------------*/

static int cache_map(struct dm_target *ti, struct bio *bio)
{
	struct cache *cache = ti->private;

	int r;
	bool commit_needed;
	dm_oblock_t block = get_bio_block(cache, bio);

	init_per_bio_data(bio);
	if (unlikely(from_oblock(block) >= from_oblock(cache->origin_blocks))) {
		/*
		 * This can only occur if the io goes to a partial block at
		 * the end of the origin device.  We don't cache these.
		 * Just remap to the origin and carry on.
		 */
		remap_to_origin(cache, bio);
		accounted_begin(cache, bio);
		return DM_MAPIO_REMAPPED;
	}

	if (discard_or_flush(bio)) {
		defer_bio(cache, bio);
		return DM_MAPIO_SUBMITTED;
	}

	r = map_bio(cache, bio, block, &commit_needed);
	if (commit_needed)
		schedule_commit(&cache->committer);

	return r;
}

static int cache_end_io(struct dm_target *ti, struct bio *bio, blk_status_t *error)
{
	struct cache *cache = ti->private;
	unsigned long flags;
	struct per_bio_data *pb = get_per_bio_data(bio);

	if (pb->tick) {
		policy_tick(cache->policy, false);

		spin_lock_irqsave(&cache->lock, flags);
		cache->need_tick_bio = true;
		spin_unlock_irqrestore(&cache->lock, flags);
	}

	bio_drop_shared_lock(cache, bio);
	accounted_complete(cache, bio);

	return DM_ENDIO_DONE;
}

static int write_dirty_bitset(struct cache *cache)
{
	int r;

	if (get_cache_mode(cache) >= CM_READ_ONLY)
		return -EINVAL;

	r = dm_cache_set_dirty_bits(cache->cmd, from_cblock(cache->cache_size), cache->dirty_bitset);
	if (r)
		metadata_operation_failed(cache, "dm_cache_set_dirty_bits", r);

	return r;
}

static int write_discard_bitset(struct cache *cache)
{
	unsigned i, r;

	if (get_cache_mode(cache) >= CM_READ_ONLY)
		return -EINVAL;

	r = dm_cache_discard_bitset_resize(cache->cmd, cache->discard_block_size,
					   cache->discard_nr_blocks);
	if (r) {
		DMERR("%s: could not resize on-disk discard bitset", cache_device_name(cache));
		metadata_operation_failed(cache, "dm_cache_discard_bitset_resize", r);
		return r;
	}

	for (i = 0; i < from_dblock(cache->discard_nr_blocks); i++) {
		r = dm_cache_set_discard(cache->cmd, to_dblock(i),
					 is_discarded(cache, to_dblock(i)));
		if (r) {
			metadata_operation_failed(cache, "dm_cache_set_discard", r);
			return r;
		}
	}

	return 0;
}

static int write_hints(struct cache *cache)
{
	int r;

	if (get_cache_mode(cache) >= CM_READ_ONLY)
		return -EINVAL;

	r = dm_cache_write_hints(cache->cmd, cache->policy);
	if (r) {
		metadata_operation_failed(cache, "dm_cache_write_hints", r);
		return r;
	}

	return 0;
}

/*
 * returns true on success
 */
static bool sync_metadata(struct cache *cache)
{
	int r1, r2, r3, r4;

	r1 = write_dirty_bitset(cache);
	if (r1)
		DMERR("%s: could not write dirty bitset", cache_device_name(cache));

	r2 = write_discard_bitset(cache);
	if (r2)
		DMERR("%s: could not write discard bitset", cache_device_name(cache));

	save_stats(cache);

	r3 = write_hints(cache);
	if (r3)
		DMERR("%s: could not write hints", cache_device_name(cache));

	/*
	 * If writing the above metadata failed, we still commit, but don't
	 * set the clean shutdown flag.  This will effectively force every
	 * dirty bit to be set on reload.
	 */
	r4 = commit(cache, !r1 && !r2 && !r3);
	if (r4)
		DMERR("%s: could not write cache metadata", cache_device_name(cache));

	return !r1 && !r2 && !r3 && !r4;
}

static void cache_postsuspend(struct dm_target *ti)
{
	struct cache *cache = ti->private;

	prevent_background_work(cache);
	BUG_ON(atomic_read(&cache->nr_io_migrations));

	cancel_delayed_work(&cache->waker);
	flush_workqueue(cache->wq);
	WARN_ON(cache->tracker.in_flight);

	/*
	 * If it's a flush suspend there won't be any deferred bios, so this
	 * call is harmless.
	 */
	requeue_deferred_bios(cache);

	if (get_cache_mode(cache) == CM_WRITE)
		(void) sync_metadata(cache);
}

static int load_mapping(void *context, dm_oblock_t oblock, dm_cblock_t cblock,
			bool dirty, uint32_t hint, bool hint_valid)
{
	int r;
	struct cache *cache = context;

	if (dirty) {
		set_bit(from_cblock(cblock), cache->dirty_bitset);
		atomic_inc(&cache->nr_dirty);
	} else
		clear_bit(from_cblock(cblock), cache->dirty_bitset);

	r = policy_load_mapping(cache->policy, oblock, cblock, dirty, hint, hint_valid);
	if (r)
		return r;

	return 0;
}

/*
 * The discard block size in the on disk metadata is not
 * neccessarily the same as we're currently using.  So we have to
 * be careful to only set the discarded attribute if we know it
 * covers a complete block of the new size.
 */
struct discard_load_info {
	struct cache *cache;

	/*
	 * These blocks are sized using the on disk dblock size, rather
	 * than the current one.
	 */
	dm_block_t block_size;
	dm_block_t discard_begin, discard_end;
};

static void discard_load_info_init(struct cache *cache,
				   struct discard_load_info *li)
{
	li->cache = cache;
	li->discard_begin = li->discard_end = 0;
}

static void set_discard_range(struct discard_load_info *li)
{
	sector_t b, e;

	if (li->discard_begin == li->discard_end)
		return;

	/*
	 * Convert to sectors.
	 */
	b = li->discard_begin * li->block_size;
	e = li->discard_end * li->block_size;

	/*
	 * Then convert back to the current dblock size.
	 */
	b = dm_sector_div_up(b, li->cache->discard_block_size);
	sector_div(e, li->cache->discard_block_size);

	/*
	 * The origin may have shrunk, so we need to check we're still in
	 * bounds.
	 */
	if (e > from_dblock(li->cache->discard_nr_blocks))
		e = from_dblock(li->cache->discard_nr_blocks);

	for (; b < e; b++)
		set_discard(li->cache, to_dblock(b));
}

static int load_discard(void *context, sector_t discard_block_size,
			dm_dblock_t dblock, bool discard)
{
	struct discard_load_info *li = context;

	li->block_size = discard_block_size;

	if (discard) {
		if (from_dblock(dblock) == li->discard_end)
			/*
			 * We're already in a discard range, just extend it.
			 */
			li->discard_end = li->discard_end + 1ULL;

		else {
			/*
			 * Emit the old range and start a new one.
			 */
			set_discard_range(li);
			li->discard_begin = from_dblock(dblock);
			li->discard_end = li->discard_begin + 1ULL;
		}
	} else {
		set_discard_range(li);
		li->discard_begin = li->discard_end = 0;
	}

	return 0;
}

static dm_cblock_t get_cache_dev_size(struct cache *cache)
{
	sector_t size = get_dev_size(cache->cache_dev);
	(void) sector_div(size, cache->sectors_per_block);
	return to_cblock(size);
}

static bool can_resize(struct cache *cache, dm_cblock_t new_size)
{
	if (from_cblock(new_size) > from_cblock(cache->cache_size))
		return true;

	/*
	 * We can't drop a dirty block when shrinking the cache.
	 */
	while (from_cblock(new_size) < from_cblock(cache->cache_size)) {
		new_size = to_cblock(from_cblock(new_size) + 1);
		if (is_dirty(cache, new_size)) {
			DMERR("%s: unable to shrink cache; cache block %llu is dirty",
			      cache_device_name(cache),
			      (unsigned long long) from_cblock(new_size));
			return false;
		}
	}

	return true;
}

static int resize_cache_dev(struct cache *cache, dm_cblock_t new_size)
{
	int r;

	r = dm_cache_resize(cache->cmd, new_size);
	if (r) {
		DMERR("%s: could not resize cache metadata", cache_device_name(cache));
		metadata_operation_failed(cache, "dm_cache_resize", r);
		return r;
	}

	set_cache_size(cache, new_size);

	return 0;
}

static int cache_preresume(struct dm_target *ti)
{
	int r = 0;
	struct cache *cache = ti->private;
	dm_cblock_t csize = get_cache_dev_size(cache);

	/*
	 * Check to see if the cache has resized.
	 */
	if (!cache->sized) {
		r = resize_cache_dev(cache, csize);
		if (r)
			return r;

		cache->sized = true;

	} else if (csize != cache->cache_size) {
		if (!can_resize(cache, csize))
			return -EINVAL;

		r = resize_cache_dev(cache, csize);
		if (r)
			return r;
	}

	if (!cache->loaded_mappings) {
		r = dm_cache_load_mappings(cache->cmd, cache->policy,
					   load_mapping, cache);
		if (r) {
			DMERR("%s: could not load cache mappings", cache_device_name(cache));
			metadata_operation_failed(cache, "dm_cache_load_mappings", r);
			return r;
		}

		cache->loaded_mappings = true;
	}

	if (!cache->loaded_discards) {
		struct discard_load_info li;

		/*
		 * The discard bitset could have been resized, or the
		 * discard block size changed.  To be safe we start by
		 * setting every dblock to not discarded.
		 */
		clear_bitset(cache->discard_bitset, from_dblock(cache->discard_nr_blocks));

		discard_load_info_init(cache, &li);
		r = dm_cache_load_discards(cache->cmd, load_discard, &li);
		if (r) {
			DMERR("%s: could not load origin discards", cache_device_name(cache));
			metadata_operation_failed(cache, "dm_cache_load_discards", r);
			return r;
		}
		set_discard_range(&li);

		cache->loaded_discards = true;
	}

	return r;
}

static void cache_resume(struct dm_target *ti)
{
	struct cache *cache = ti->private;

	cache->need_tick_bio = true;
	allow_background_work(cache);
	do_waker(&cache->waker.work);
}

/*
 * Status format:
 *
 * <metadata block size> <#used metadata blocks>/<#total metadata blocks>
 * <cache block size> <#used cache blocks>/<#total cache blocks>
 * <#read hits> <#read misses> <#write hits> <#write misses>
 * <#demotions> <#promotions> <#dirty>
 * <#features> <features>*
 * <#core args> <core args>
 * <policy name> <#policy args> <policy args>* <cache metadata mode> <needs_check>
 */
static void cache_status(struct dm_target *ti, status_type_t type,
			 unsigned status_flags, char *result, unsigned maxlen)
{
	int r = 0;
	unsigned i;
	ssize_t sz = 0;
	dm_block_t nr_free_blocks_metadata = 0;
	dm_block_t nr_blocks_metadata = 0;
	char buf[BDEVNAME_SIZE];
	struct cache *cache = ti->private;
	dm_cblock_t residency;
	bool needs_check;

	switch (type) {
	case STATUSTYPE_INFO:
		if (get_cache_mode(cache) == CM_FAIL) {
			DMEMIT("Fail");
			break;
		}

		/* Commit to ensure statistics aren't out-of-date */
		if (!(status_flags & DM_STATUS_NOFLUSH_FLAG) && !dm_suspended(ti))
			(void) commit(cache, false);

		r = dm_cache_get_free_metadata_block_count(cache->cmd, &nr_free_blocks_metadata);
		if (r) {
			DMERR("%s: dm_cache_get_free_metadata_block_count returned %d",
			      cache_device_name(cache), r);
			goto err;
		}

		r = dm_cache_get_metadata_dev_size(cache->cmd, &nr_blocks_metadata);
		if (r) {
			DMERR("%s: dm_cache_get_metadata_dev_size returned %d",
			      cache_device_name(cache), r);
			goto err;
		}

		residency = policy_residency(cache->policy);

		DMEMIT("%u %llu/%llu %llu %llu/%llu %u %u %u %u %u %u %lu ",
		       (unsigned)DM_CACHE_METADATA_BLOCK_SIZE,
		       (unsigned long long)(nr_blocks_metadata - nr_free_blocks_metadata),
		       (unsigned long long)nr_blocks_metadata,
		       (unsigned long long)cache->sectors_per_block,
		       (unsigned long long) from_cblock(residency),
		       (unsigned long long) from_cblock(cache->cache_size),
		       (unsigned) atomic_read(&cache->stats.read_hit),
		       (unsigned) atomic_read(&cache->stats.read_miss),
		       (unsigned) atomic_read(&cache->stats.write_hit),
		       (unsigned) atomic_read(&cache->stats.write_miss),
		       (unsigned) atomic_read(&cache->stats.demotion),
		       (unsigned) atomic_read(&cache->stats.promotion),
		       (unsigned long) atomic_read(&cache->nr_dirty));

		if (cache->features.metadata_version == 2)
			DMEMIT("2 metadata2 ");
		else
			DMEMIT("1 ");

		if (writethrough_mode(cache))
			DMEMIT("writethrough ");

		else if (passthrough_mode(cache))
			DMEMIT("passthrough ");

		else if (writeback_mode(cache))
			DMEMIT("writeback ");

		else {
			DMERR("%s: internal error: unknown io mode: %d",
			      cache_device_name(cache), (int) cache->features.io_mode);
			goto err;
		}

		DMEMIT("2 migration_threshold %llu ", (unsigned long long) cache->migration_threshold);

		DMEMIT("%s ", dm_cache_policy_get_name(cache->policy));
		if (sz < maxlen) {
			r = policy_emit_config_values(cache->policy, result, maxlen, &sz);
			if (r)
				DMERR("%s: policy_emit_config_values returned %d",
				      cache_device_name(cache), r);
		}

		if (get_cache_mode(cache) == CM_READ_ONLY)
			DMEMIT("ro ");
		else
			DMEMIT("rw ");

		r = dm_cache_metadata_needs_check(cache->cmd, &needs_check);

		if (r || needs_check)
			DMEMIT("needs_check ");
		else
			DMEMIT("- ");

		break;

	case STATUSTYPE_TABLE:
		format_dev_t(buf, cache->metadata_dev->bdev->bd_dev);
		DMEMIT("%s ", buf);
		format_dev_t(buf, cache->cache_dev->bdev->bd_dev);
		DMEMIT("%s ", buf);
		format_dev_t(buf, cache->origin_dev->bdev->bd_dev);
		DMEMIT("%s", buf);

		for (i = 0; i < cache->nr_ctr_args - 1; i++)
			DMEMIT(" %s", cache->ctr_args[i]);
		if (cache->nr_ctr_args)
			DMEMIT(" %s", cache->ctr_args[cache->nr_ctr_args - 1]);
	}

	return;

err:
	DMEMIT("Error");
}

/*
 * Defines a range of cblocks, begin to (end - 1) are in the range.  end is
 * the one-past-the-end value.
 */
struct cblock_range {
	dm_cblock_t begin;
	dm_cblock_t end;
};

/*
 * A cache block range can take two forms:
 *
 * i) A single cblock, eg. '3456'
 * ii) A begin and end cblock with a dash between, eg. 123-234
 */
static int parse_cblock_range(struct cache *cache, const char *str,
			      struct cblock_range *result)
{
	char dummy;
	uint64_t b, e;
	int r;

	/*
	 * Try and parse form (ii) first.
	 */
	r = sscanf(str, "%llu-%llu%c", &b, &e, &dummy);
	if (r < 0)
		return r;

	if (r == 2) {
		result->begin = to_cblock(b);
		result->end = to_cblock(e);
		return 0;
	}

	/*
	 * That didn't work, try form (i).
	 */
	r = sscanf(str, "%llu%c", &b, &dummy);
	if (r < 0)
		return r;

	if (r == 1) {
		result->begin = to_cblock(b);
		result->end = to_cblock(from_cblock(result->begin) + 1u);
		return 0;
	}

	DMERR("%s: invalid cblock range '%s'", cache_device_name(cache), str);
	return -EINVAL;
}

static int validate_cblock_range(struct cache *cache, struct cblock_range *range)
{
	uint64_t b = from_cblock(range->begin);
	uint64_t e = from_cblock(range->end);
	uint64_t n = from_cblock(cache->cache_size);

	if (b >= n) {
		DMERR("%s: begin cblock out of range: %llu >= %llu",
		      cache_device_name(cache), b, n);
		return -EINVAL;
	}

	if (e > n) {
		DMERR("%s: end cblock out of range: %llu > %llu",
		      cache_device_name(cache), e, n);
		return -EINVAL;
	}

	if (b >= e) {
		DMERR("%s: invalid cblock range: %llu >= %llu",
		      cache_device_name(cache), b, e);
		return -EINVAL;
	}

	return 0;
}

static inline dm_cblock_t cblock_succ(dm_cblock_t b)
{
	return to_cblock(from_cblock(b) + 1);
}

static int request_invalidation(struct cache *cache, struct cblock_range *range)
{
	int r = 0;

	/*
	 * We don't need to do any locking here because we know we're in
	 * passthrough mode.  There's is potential for a race between an
	 * invalidation triggered by an io and an invalidation message.  This
	 * is harmless, we must not worry if the policy call fails.
	 */
	while (range->begin != range->end) {
		r = invalidate_cblock(cache, range->begin);
		if (r)
			return r;

		range->begin = cblock_succ(range->begin);
	}

	cache->commit_requested = true;
	return r;
}

static int process_invalidate_cblocks_message(struct cache *cache, unsigned count,
					      const char **cblock_ranges)
{
	int r = 0;
	unsigned i;
	struct cblock_range range;

	if (!passthrough_mode(cache)) {
		DMERR("%s: cache has to be in passthrough mode for invalidation",
		      cache_device_name(cache));
		return -EPERM;
	}

	for (i = 0; i < count; i++) {
		r = parse_cblock_range(cache, cblock_ranges[i], &range);
		if (r)
			break;

		r = validate_cblock_range(cache, &range);
		if (r)
			break;

		/*
		 * Pass begin and end origin blocks to the worker and wake it.
		 */
		r = request_invalidation(cache, &range);
		if (r)
			break;
	}

	return r;
}

/*
 * Supports
 *	"<key> <value>"
 * and
 *     "invalidate_cblocks [(<begin>)|(<begin>-<end>)]*
 *
 * The key migration_threshold is supported by the cache target core.
 */
static int cache_message(struct dm_target *ti, unsigned argc, char **argv,
			 char *result, unsigned maxlen)
{
	struct cache *cache = ti->private;

	if (!argc)
		return -EINVAL;

	if (get_cache_mode(cache) >= CM_READ_ONLY) {
		DMERR("%s: unable to service cache target messages in READ_ONLY or FAIL mode",
		      cache_device_name(cache));
		return -EOPNOTSUPP;
	}

	if (!strcasecmp(argv[0], "invalidate_cblocks"))
		return process_invalidate_cblocks_message(cache, argc - 1, (const char **) argv + 1);

	if (argc != 2)
		return -EINVAL;

	return set_config_value(cache, argv[0], argv[1]);
}

static int cache_iterate_devices(struct dm_target *ti,
				 iterate_devices_callout_fn fn, void *data)
{
	int r = 0;
	struct cache *cache = ti->private;

	r = fn(ti, cache->cache_dev, 0, get_dev_size(cache->cache_dev), data);
	if (!r)
		r = fn(ti, cache->origin_dev, 0, ti->len, data);

	return r;
}

static void set_discard_limits(struct cache *cache, struct queue_limits *limits)
{
	/*
	 * FIXME: these limits may be incompatible with the cache device
	 */
	limits->max_discard_sectors = min_t(sector_t, cache->discard_block_size * 1024,
					    cache->origin_sectors);
	limits->discard_granularity = cache->discard_block_size << SECTOR_SHIFT;
}

static void cache_io_hints(struct dm_target *ti, struct queue_limits *limits)
{
	struct cache *cache = ti->private;
	uint64_t io_opt_sectors = limits->io_opt >> SECTOR_SHIFT;

	/*
	 * If the system-determined stacked limits are compatible with the
	 * cache's blocksize (io_opt is a factor) do not override them.
	 */
	if (io_opt_sectors < cache->sectors_per_block ||
	    do_div(io_opt_sectors, cache->sectors_per_block)) {
		blk_limits_io_min(limits, cache->sectors_per_block << SECTOR_SHIFT);
		blk_limits_io_opt(limits, cache->sectors_per_block << SECTOR_SHIFT);
	}
	set_discard_limits(cache, limits);
}

/*----------------------------------------------------------------*/

static struct target_type cache_target = {
	.name = "cache",
	.version = {2, 0, 0},
	.module = THIS_MODULE,
	.ctr = cache_ctr,
	.dtr = cache_dtr,
	.map = cache_map,
	.end_io = cache_end_io,
	.postsuspend = cache_postsuspend,
	.preresume = cache_preresume,
	.resume = cache_resume,
	.status = cache_status,
	.message = cache_message,
	.iterate_devices = cache_iterate_devices,
	.io_hints = cache_io_hints,
};

static int __init dm_cache_init(void)
{
	int r;

	migration_cache = KMEM_CACHE(dm_cache_migration, 0);
	if (!migration_cache) {
		dm_unregister_target(&cache_target);
		return -ENOMEM;
	}

	r = dm_register_target(&cache_target);
	if (r) {
		DMERR("cache target registration failed: %d", r);
		return r;
	}

	return 0;
}

static void __exit dm_cache_exit(void)
{
	dm_unregister_target(&cache_target);
	kmem_cache_destroy(migration_cache);
}

module_init(dm_cache_init);
module_exit(dm_cache_exit);

MODULE_DESCRIPTION(DM_NAME " cache target");
MODULE_AUTHOR("Joe Thornber <ejt@redhat.com>");
MODULE_LICENSE("GPL");
