/*
 * Copyright (C) 2012 Red Hat. All rights reserved.
 *
 * This file is released under the GPL.
 */

#include "dm.h"
#include "dm-bio-prison.h"
#include "dm-bio-record.h"
#include "dm-cache-metadata.h"

#include <linux/dm-io.h>
#include <linux/dm-kcopyd.h>
#include <linux/jiffies.h>
#include <linux/init.h>
#include <linux/mempool.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#define DM_MSG_PREFIX "cache"

DECLARE_DM_KCOPYD_THROTTLE_WITH_MODULE_PARM(cache_copy_throttle,
	"A percentage of time allocated for copying to and/or from cache");

/*----------------------------------------------------------------*/

#define IOT_RESOLUTION 4

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
};

struct cache_stats {
	atomic_t read_hit;
	atomic_t read_miss;
	atomic_t write_hit;
	atomic_t write_miss;
	atomic_t demotion;
	atomic_t promotion;
	atomic_t copies_avoided;
	atomic_t cache_cell_clash;
	atomic_t commit_count;
	atomic_t discard_count;
};

/*
 * Defines a range of cblocks, begin to (end - 1) are in the range.  end is
 * the one-past-the-end value.
 */
struct cblock_range {
	dm_cblock_t begin;
	dm_cblock_t end;
};

struct invalidation_request {
	struct list_head list;
	struct cblock_range *cblocks;

	atomic_t complete;
	int err;

	wait_queue_head_t result_wait;
};

struct cache {
	struct dm_target *ti;
	struct dm_target_callbacks callbacks;

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
	 * Fields for converting from sectors to blocks.
	 */
	sector_t sectors_per_block;
	int sectors_per_block_shift;

	spinlock_t lock;
	struct list_head deferred_cells;
	struct bio_list deferred_bios;
	struct bio_list deferred_flush_bios;
	struct bio_list deferred_writethrough_bios;
	struct list_head quiesced_migrations;
	struct list_head completed_migrations;
	struct list_head need_commit_migrations;
	sector_t migration_threshold;
	wait_queue_head_t migration_wait;
	atomic_t nr_allocated_migrations;

	/*
	 * The number of in flight migrations that are performing
	 * background io. eg, promotion, writeback.
	 */
	atomic_t nr_io_migrations;

	wait_queue_head_t quiescing_wait;
	atomic_t quiescing;
	atomic_t quiescing_ack;

	/*
	 * cache_size entries, dirty if set
	 */
	atomic_t nr_dirty;
	unsigned long *dirty_bitset;

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
	struct workqueue_struct *wq;
	struct work_struct worker;

	struct delayed_work waker;
	unsigned long last_commit_jiffies;

	struct dm_bio_prison *prison;
	struct dm_deferred_set *all_io_ds;

	mempool_t *migration_pool;

	struct dm_cache_policy *policy;
	unsigned policy_nr_args;

	bool need_tick_bio:1;
	bool sized:1;
	bool invalidate:1;
	bool commit_requested:1;
	bool loaded_mappings:1;
	bool loaded_discards:1;

	/*
	 * Cache features such as write-through.
	 */
	struct cache_features features;

	struct cache_stats stats;

	/*
	 * Invalidation fields.
	 */
	spinlock_t invalidation_lock;
	struct list_head invalidation_requests;

	struct io_tracker origin_tracker;
};

struct per_bio_data {
	bool tick:1;
	unsigned req_nr:2;
	struct dm_deferred_entry *all_io_entry;
	struct dm_hook_info hook_info;
	sector_t len;

	/*
	 * writethrough fields.  These MUST remain at the end of this
	 * structure and the 'cache' member must be the first as it
	 * is used to determine the offset of the writethrough fields.
	 */
	struct cache *cache;
	dm_cblock_t cblock;
	struct dm_bio_details bio_details;
};

struct dm_cache_migration {
	struct list_head list;
	struct cache *cache;

	unsigned long start_jiffies;
	dm_oblock_t old_oblock;
	dm_oblock_t new_oblock;
	dm_cblock_t cblock;

	bool err:1;
	bool discard:1;
	bool writeback:1;
	bool demote:1;
	bool promote:1;
	bool requeue_holder:1;
	bool invalidate:1;

	struct dm_bio_prison_cell *old_ocell;
	struct dm_bio_prison_cell *new_ocell;
};

/*
 * Processing a bio in the worker thread may require these memory
 * allocations.  We prealloc to avoid deadlocks (the same worker thread
 * frees them back to the mempool).
 */
struct prealloc {
	struct dm_cache_migration *mg;
	struct dm_bio_prison_cell *cell1;
	struct dm_bio_prison_cell *cell2;
};

static enum cache_metadata_mode get_cache_mode(struct cache *cache);

static void wake_worker(struct cache *cache)
{
	queue_work(cache->wq, &cache->worker);
}

/*----------------------------------------------------------------*/

static struct dm_bio_prison_cell *alloc_prison_cell(struct cache *cache)
{
	/* FIXME: change to use a local slab. */
	return dm_bio_prison_alloc_cell(cache->prison, GFP_NOWAIT);
}

static void free_prison_cell(struct cache *cache, struct dm_bio_prison_cell *cell)
{
	dm_bio_prison_free_cell(cache->prison, cell);
}

static struct dm_cache_migration *alloc_migration(struct cache *cache)
{
	struct dm_cache_migration *mg;

	mg = mempool_alloc(cache->migration_pool, GFP_NOWAIT);
	if (mg) {
		mg->cache = cache;
		atomic_inc(&mg->cache->nr_allocated_migrations);
	}

	return mg;
}

static void free_migration(struct dm_cache_migration *mg)
{
	struct cache *cache = mg->cache;

	if (atomic_dec_and_test(&cache->nr_allocated_migrations))
		wake_up(&cache->migration_wait);

	mempool_free(mg, cache->migration_pool);
}

static int prealloc_data_structs(struct cache *cache, struct prealloc *p)
{
	if (!p->mg) {
		p->mg = alloc_migration(cache);
		if (!p->mg)
			return -ENOMEM;
	}

	if (!p->cell1) {
		p->cell1 = alloc_prison_cell(cache);
		if (!p->cell1)
			return -ENOMEM;
	}

	if (!p->cell2) {
		p->cell2 = alloc_prison_cell(cache);
		if (!p->cell2)
			return -ENOMEM;
	}

	return 0;
}

static void prealloc_free_structs(struct cache *cache, struct prealloc *p)
{
	if (p->cell2)
		free_prison_cell(cache, p->cell2);

	if (p->cell1)
		free_prison_cell(cache, p->cell1);

	if (p->mg)
		free_migration(p->mg);
}

static struct dm_cache_migration *prealloc_get_migration(struct prealloc *p)
{
	struct dm_cache_migration *mg = p->mg;

	BUG_ON(!mg);
	p->mg = NULL;

	return mg;
}

/*
 * You must have a cell within the prealloc struct to return.  If not this
 * function will BUG() rather than returning NULL.
 */
static struct dm_bio_prison_cell *prealloc_get_cell(struct prealloc *p)
{
	struct dm_bio_prison_cell *r = NULL;

	if (p->cell1) {
		r = p->cell1;
		p->cell1 = NULL;

	} else if (p->cell2) {
		r = p->cell2;
		p->cell2 = NULL;
	} else
		BUG();

	return r;
}

/*
 * You can't have more than two cells in a prealloc struct.  BUG() will be
 * called if you try and overfill.
 */
static void prealloc_put_cell(struct prealloc *p, struct dm_bio_prison_cell *cell)
{
	if (!p->cell2)
		p->cell2 = cell;

	else if (!p->cell1)
		p->cell1 = cell;

	else
		BUG();
}

/*----------------------------------------------------------------*/

static void build_key(dm_oblock_t begin, dm_oblock_t end, struct dm_cell_key *key)
{
	key->virtual = 0;
	key->dev = 0;
	key->block_begin = from_oblock(begin);
	key->block_end = from_oblock(end);
}

/*
 * The caller hands in a preallocated cell, and a free function for it.
 * The cell will be freed if there's an error, or if it wasn't used because
 * a cell with that key already exists.
 */
typedef void (*cell_free_fn)(void *context, struct dm_bio_prison_cell *cell);

static int bio_detain_range(struct cache *cache, dm_oblock_t oblock_begin, dm_oblock_t oblock_end,
			    struct bio *bio, struct dm_bio_prison_cell *cell_prealloc,
			    cell_free_fn free_fn, void *free_context,
			    struct dm_bio_prison_cell **cell_result)
{
	int r;
	struct dm_cell_key key;

	build_key(oblock_begin, oblock_end, &key);
	r = dm_bio_detain(cache->prison, &key, bio, cell_prealloc, cell_result);
	if (r)
		free_fn(free_context, cell_prealloc);

	return r;
}

static int bio_detain(struct cache *cache, dm_oblock_t oblock,
		      struct bio *bio, struct dm_bio_prison_cell *cell_prealloc,
		      cell_free_fn free_fn, void *free_context,
		      struct dm_bio_prison_cell **cell_result)
{
	dm_oblock_t end = to_oblock(from_oblock(oblock) + 1ULL);
	return bio_detain_range(cache, oblock, end, bio,
				cell_prealloc, free_fn, free_context, cell_result);
}

static int get_cell(struct cache *cache,
		    dm_oblock_t oblock,
		    struct prealloc *structs,
		    struct dm_bio_prison_cell **cell_result)
{
	int r;
	struct dm_cell_key key;
	struct dm_bio_prison_cell *cell_prealloc;

	cell_prealloc = prealloc_get_cell(structs);

	build_key(oblock, to_oblock(from_oblock(oblock) + 1ULL), &key);
	r = dm_get_cell(cache->prison, &key, cell_prealloc, cell_result);
	if (r)
		prealloc_put_cell(structs, cell_prealloc);

	return r;
}

/*----------------------------------------------------------------*/

static bool is_dirty(struct cache *cache, dm_cblock_t b)
{
	return test_bit(from_cblock(b), cache->dirty_bitset);
}

static void set_dirty(struct cache *cache, dm_oblock_t oblock, dm_cblock_t cblock)
{
	if (!test_and_set_bit(from_cblock(cblock), cache->dirty_bitset)) {
		atomic_inc(&cache->nr_dirty);
		policy_set_dirty(cache->policy, oblock);
	}
}

static void clear_dirty(struct cache *cache, dm_oblock_t oblock, dm_cblock_t cblock)
{
	if (test_and_clear_bit(from_cblock(cblock), cache->dirty_bitset)) {
		policy_clear_dirty(cache->policy, oblock);
		if (atomic_dec_return(&cache->nr_dirty) == 0)
			dm_table_event(cache->ti->table);
	}
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

static dm_oblock_t dblock_to_oblock(struct cache *cache, dm_dblock_t dblock)
{
	return to_oblock(from_dblock(dblock) * oblocks_per_dblock(cache));
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

/*----------------------------------------------------------------
 * Per bio data
 *--------------------------------------------------------------*/

/*
 * If using writeback, leave out struct per_bio_data's writethrough fields.
 */
#define PB_DATA_SIZE_WB (offsetof(struct per_bio_data, cache))
#define PB_DATA_SIZE_WT (sizeof(struct per_bio_data))

static bool writethrough_mode(struct cache_features *f)
{
	return f->io_mode == CM_IO_WRITETHROUGH;
}

static bool writeback_mode(struct cache_features *f)
{
	return f->io_mode == CM_IO_WRITEBACK;
}

static bool passthrough_mode(struct cache_features *f)
{
	return f->io_mode == CM_IO_PASSTHROUGH;
}

static size_t get_per_bio_data_size(struct cache *cache)
{
	return writethrough_mode(&cache->features) ? PB_DATA_SIZE_WT : PB_DATA_SIZE_WB;
}

static struct per_bio_data *get_per_bio_data(struct bio *bio, size_t data_size)
{
	struct per_bio_data *pb = dm_per_bio_data(bio, data_size);
	BUG_ON(!pb);
	return pb;
}

static struct per_bio_data *init_per_bio_data(struct bio *bio, size_t data_size)
{
	struct per_bio_data *pb = get_per_bio_data(bio, data_size);

	pb->tick = false;
	pb->req_nr = dm_bio_get_target_bio_nr(bio);
	pb->all_io_entry = NULL;
	pb->len = 0;

	return pb;
}

/*----------------------------------------------------------------
 * Remapping
 *--------------------------------------------------------------*/
static void remap_to_origin(struct cache *cache, struct bio *bio)
{
	bio->bi_bdev = cache->origin_dev->bdev;
}

static void remap_to_cache(struct cache *cache, struct bio *bio,
			   dm_cblock_t cblock)
{
	sector_t bi_sector = bio->bi_iter.bi_sector;
	sector_t block = from_cblock(cblock);

	bio->bi_bdev = cache->cache_dev->bdev;
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
	size_t pb_data_size = get_per_bio_data_size(cache);
	struct per_bio_data *pb = get_per_bio_data(bio, pb_data_size);

	spin_lock_irqsave(&cache->lock, flags);
	if (cache->need_tick_bio &&
	    !(bio->bi_opf & (REQ_FUA | REQ_PREFLUSH)) &&
	    bio_op(bio) != REQ_OP_DISCARD) {
		pb->tick = true;
		cache->need_tick_bio = false;
	}
	spin_unlock_irqrestore(&cache->lock, flags);
}

static void remap_to_origin_clear_discard(struct cache *cache, struct bio *bio,
				  dm_oblock_t oblock)
{
	check_if_tick_bio_needed(cache, bio);
	remap_to_origin(cache, bio);
	if (bio_data_dir(bio) == WRITE)
		clear_discard(cache, oblock_to_dblock(cache, oblock));
}

static void remap_to_cache_dirty(struct cache *cache, struct bio *bio,
				 dm_oblock_t oblock, dm_cblock_t cblock)
{
	check_if_tick_bio_needed(cache, bio);
	remap_to_cache(cache, bio, cblock);
	if (bio_data_dir(bio) == WRITE) {
		set_dirty(cache, oblock, cblock);
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

static int bio_triggers_commit(struct cache *cache, struct bio *bio)
{
	return bio->bi_opf & (REQ_PREFLUSH | REQ_FUA);
}

/*
 * You must increment the deferred set whilst the prison cell is held.  To
 * encourage this, we ask for 'cell' to be passed in.
 */
static void inc_ds(struct cache *cache, struct bio *bio,
		   struct dm_bio_prison_cell *cell)
{
	size_t pb_data_size = get_per_bio_data_size(cache);
	struct per_bio_data *pb = get_per_bio_data(bio, pb_data_size);

	BUG_ON(!cell);
	BUG_ON(pb->all_io_entry);

	pb->all_io_entry = dm_deferred_entry_inc(cache->all_io_ds);
}

static bool accountable_bio(struct cache *cache, struct bio *bio)
{
	return ((bio->bi_bdev == cache->origin_dev->bdev) &&
		bio_op(bio) != REQ_OP_DISCARD);
}

static void accounted_begin(struct cache *cache, struct bio *bio)
{
	size_t pb_data_size = get_per_bio_data_size(cache);
	struct per_bio_data *pb = get_per_bio_data(bio, pb_data_size);

	if (accountable_bio(cache, bio)) {
		pb->len = bio_sectors(bio);
		iot_io_begin(&cache->origin_tracker, pb->len);
	}
}

static void accounted_complete(struct cache *cache, struct bio *bio)
{
	size_t pb_data_size = get_per_bio_data_size(cache);
	struct per_bio_data *pb = get_per_bio_data(bio, pb_data_size);

	iot_io_end(&cache->origin_tracker, pb->len);
}

static void accounted_request(struct cache *cache, struct bio *bio)
{
	accounted_begin(cache, bio);
	generic_make_request(bio);
}

static void issue(struct cache *cache, struct bio *bio)
{
	unsigned long flags;

	if (!bio_triggers_commit(cache, bio)) {
		accounted_request(cache, bio);
		return;
	}

	/*
	 * Batch together any bios that trigger commits and then issue a
	 * single commit for them in do_worker().
	 */
	spin_lock_irqsave(&cache->lock, flags);
	cache->commit_requested = true;
	bio_list_add(&cache->deferred_flush_bios, bio);
	spin_unlock_irqrestore(&cache->lock, flags);
}

static void inc_and_issue(struct cache *cache, struct bio *bio, struct dm_bio_prison_cell *cell)
{
	inc_ds(cache, bio, cell);
	issue(cache, bio);
}

static void defer_writethrough_bio(struct cache *cache, struct bio *bio)
{
	unsigned long flags;

	spin_lock_irqsave(&cache->lock, flags);
	bio_list_add(&cache->deferred_writethrough_bios, bio);
	spin_unlock_irqrestore(&cache->lock, flags);

	wake_worker(cache);
}

static void writethrough_endio(struct bio *bio)
{
	struct per_bio_data *pb = get_per_bio_data(bio, PB_DATA_SIZE_WT);

	dm_unhook_bio(&pb->hook_info, bio);

	if (bio->bi_error) {
		bio_endio(bio);
		return;
	}

	dm_bio_restore(&pb->bio_details, bio);
	remap_to_cache(pb->cache, bio, pb->cblock);

	/*
	 * We can't issue this bio directly, since we're in interrupt
	 * context.  So it gets put on a bio list for processing by the
	 * worker thread.
	 */
	defer_writethrough_bio(pb->cache, bio);
}

/*
 * When running in writethrough mode we need to send writes to clean blocks
 * to both the cache and origin devices.  In future we'd like to clone the
 * bio and send them in parallel, but for now we're doing them in
 * series as this is easier.
 */
static void remap_to_origin_then_cache(struct cache *cache, struct bio *bio,
				       dm_oblock_t oblock, dm_cblock_t cblock)
{
	struct per_bio_data *pb = get_per_bio_data(bio, PB_DATA_SIZE_WT);

	pb->cache = cache;
	pb->cblock = cblock;
	dm_hook_bio(&pb->hook_info, bio, writethrough_endio, NULL);
	dm_bio_record(&pb->bio_details, bio);

	remap_to_origin_clear_discard(pb->cache, bio, oblock);
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
		DMERR("unable to read needs_check flag, setting failure mode");
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
	return bio_op(bio) == REQ_OP_DISCARD ||
	       bio->bi_opf & (REQ_PREFLUSH | REQ_FUA);
}

static void __cell_defer(struct cache *cache, struct dm_bio_prison_cell *cell)
{
	if (discard_or_flush(cell->holder)) {
		/*
		 * We have to handle these bios individually.
		 */
		dm_cell_release(cache->prison, cell, &cache->deferred_bios);
		free_prison_cell(cache, cell);
	} else
		list_add_tail(&cell->user_list, &cache->deferred_cells);
}

static void cell_defer(struct cache *cache, struct dm_bio_prison_cell *cell, bool holder)
{
	unsigned long flags;

	if (!holder && dm_cell_promote_or_release(cache->prison, cell)) {
		/*
		 * There was no prisoner to promote to holder, the
		 * cell has been released.
		 */
		free_prison_cell(cache, cell);
		return;
	}

	spin_lock_irqsave(&cache->lock, flags);
	__cell_defer(cache, cell);
	spin_unlock_irqrestore(&cache->lock, flags);

	wake_worker(cache);
}

static void cell_error_with_code(struct cache *cache, struct dm_bio_prison_cell *cell, int err)
{
	dm_cell_error(cache->prison, cell, err);
	free_prison_cell(cache, cell);
}

static void cell_requeue(struct cache *cache, struct dm_bio_prison_cell *cell)
{
	cell_error_with_code(cache, cell, DM_ENDIO_REQUEUE);
}

static void free_io_migration(struct dm_cache_migration *mg)
{
	struct cache *cache = mg->cache;

	dec_io_migrations(cache);
	free_migration(mg);
	wake_worker(cache);
}

static void migration_failure(struct dm_cache_migration *mg)
{
	struct cache *cache = mg->cache;
	const char *dev_name = cache_device_name(cache);

	if (mg->writeback) {
		DMERR_LIMIT("%s: writeback failed; couldn't copy block", dev_name);
		set_dirty(cache, mg->old_oblock, mg->cblock);
		cell_defer(cache, mg->old_ocell, false);

	} else if (mg->demote) {
		DMERR_LIMIT("%s: demotion failed; couldn't copy block", dev_name);
		policy_force_mapping(cache->policy, mg->new_oblock, mg->old_oblock);

		cell_defer(cache, mg->old_ocell, mg->promote ? false : true);
		if (mg->promote)
			cell_defer(cache, mg->new_ocell, true);
	} else {
		DMERR_LIMIT("%s: promotion failed; couldn't copy block", dev_name);
		policy_remove_mapping(cache->policy, mg->new_oblock);
		cell_defer(cache, mg->new_ocell, true);
	}

	free_io_migration(mg);
}

static void migration_success_pre_commit(struct dm_cache_migration *mg)
{
	int r;
	unsigned long flags;
	struct cache *cache = mg->cache;

	if (mg->writeback) {
		clear_dirty(cache, mg->old_oblock, mg->cblock);
		cell_defer(cache, mg->old_ocell, false);
		free_io_migration(mg);
		return;

	} else if (mg->demote) {
		r = dm_cache_remove_mapping(cache->cmd, mg->cblock);
		if (r) {
			DMERR_LIMIT("%s: demotion failed; couldn't update on disk metadata",
				    cache_device_name(cache));
			metadata_operation_failed(cache, "dm_cache_remove_mapping", r);
			policy_force_mapping(cache->policy, mg->new_oblock,
					     mg->old_oblock);
			if (mg->promote)
				cell_defer(cache, mg->new_ocell, true);
			free_io_migration(mg);
			return;
		}
	} else {
		r = dm_cache_insert_mapping(cache->cmd, mg->cblock, mg->new_oblock);
		if (r) {
			DMERR_LIMIT("%s: promotion failed; couldn't update on disk metadata",
				    cache_device_name(cache));
			metadata_operation_failed(cache, "dm_cache_insert_mapping", r);
			policy_remove_mapping(cache->policy, mg->new_oblock);
			free_io_migration(mg);
			return;
		}
	}

	spin_lock_irqsave(&cache->lock, flags);
	list_add_tail(&mg->list, &cache->need_commit_migrations);
	cache->commit_requested = true;
	spin_unlock_irqrestore(&cache->lock, flags);
}

static void migration_success_post_commit(struct dm_cache_migration *mg)
{
	unsigned long flags;
	struct cache *cache = mg->cache;

	if (mg->writeback) {
		DMWARN_LIMIT("%s: writeback unexpectedly triggered commit",
			     cache_device_name(cache));
		return;

	} else if (mg->demote) {
		cell_defer(cache, mg->old_ocell, mg->promote ? false : true);

		if (mg->promote) {
			mg->demote = false;

			spin_lock_irqsave(&cache->lock, flags);
			list_add_tail(&mg->list, &cache->quiesced_migrations);
			spin_unlock_irqrestore(&cache->lock, flags);

		} else {
			if (mg->invalidate)
				policy_remove_mapping(cache->policy, mg->old_oblock);
			free_io_migration(mg);
		}

	} else {
		if (mg->requeue_holder) {
			clear_dirty(cache, mg->new_oblock, mg->cblock);
			cell_defer(cache, mg->new_ocell, true);
		} else {
			/*
			 * The block was promoted via an overwrite, so it's dirty.
			 */
			set_dirty(cache, mg->new_oblock, mg->cblock);
			bio_endio(mg->new_ocell->holder);
			cell_defer(cache, mg->new_ocell, false);
		}
		free_io_migration(mg);
	}
}

static void copy_complete(int read_err, unsigned long write_err, void *context)
{
	unsigned long flags;
	struct dm_cache_migration *mg = (struct dm_cache_migration *) context;
	struct cache *cache = mg->cache;

	if (read_err || write_err)
		mg->err = true;

	spin_lock_irqsave(&cache->lock, flags);
	list_add_tail(&mg->list, &cache->completed_migrations);
	spin_unlock_irqrestore(&cache->lock, flags);

	wake_worker(cache);
}

static void issue_copy(struct dm_cache_migration *mg)
{
	int r;
	struct dm_io_region o_region, c_region;
	struct cache *cache = mg->cache;
	sector_t cblock = from_cblock(mg->cblock);

	o_region.bdev = cache->origin_dev->bdev;
	o_region.count = cache->sectors_per_block;

	c_region.bdev = cache->cache_dev->bdev;
	c_region.sector = cblock * cache->sectors_per_block;
	c_region.count = cache->sectors_per_block;

	if (mg->writeback || mg->demote) {
		/* demote */
		o_region.sector = from_oblock(mg->old_oblock) * cache->sectors_per_block;
		r = dm_kcopyd_copy(cache->copier, &c_region, 1, &o_region, 0, copy_complete, mg);
	} else {
		/* promote */
		o_region.sector = from_oblock(mg->new_oblock) * cache->sectors_per_block;
		r = dm_kcopyd_copy(cache->copier, &o_region, 1, &c_region, 0, copy_complete, mg);
	}

	if (r < 0) {
		DMERR_LIMIT("%s: issuing migration failed", cache_device_name(cache));
		migration_failure(mg);
	}
}

static void overwrite_endio(struct bio *bio)
{
	struct dm_cache_migration *mg = bio->bi_private;
	struct cache *cache = mg->cache;
	size_t pb_data_size = get_per_bio_data_size(cache);
	struct per_bio_data *pb = get_per_bio_data(bio, pb_data_size);
	unsigned long flags;

	dm_unhook_bio(&pb->hook_info, bio);

	if (bio->bi_error)
		mg->err = true;

	mg->requeue_holder = false;

	spin_lock_irqsave(&cache->lock, flags);
	list_add_tail(&mg->list, &cache->completed_migrations);
	spin_unlock_irqrestore(&cache->lock, flags);

	wake_worker(cache);
}

static void issue_overwrite(struct dm_cache_migration *mg, struct bio *bio)
{
	size_t pb_data_size = get_per_bio_data_size(mg->cache);
	struct per_bio_data *pb = get_per_bio_data(bio, pb_data_size);

	dm_hook_bio(&pb->hook_info, bio, overwrite_endio, mg);
	remap_to_cache_dirty(mg->cache, bio, mg->new_oblock, mg->cblock);

	/*
	 * No need to inc_ds() here, since the cell will be held for the
	 * duration of the io.
	 */
	accounted_request(mg->cache, bio);
}

static bool bio_writes_complete_block(struct cache *cache, struct bio *bio)
{
	return (bio_data_dir(bio) == WRITE) &&
		(bio->bi_iter.bi_size == (cache->sectors_per_block << SECTOR_SHIFT));
}

static void avoid_copy(struct dm_cache_migration *mg)
{
	atomic_inc(&mg->cache->stats.copies_avoided);
	migration_success_pre_commit(mg);
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

static void issue_discard(struct dm_cache_migration *mg)
{
	dm_dblock_t b, e;
	struct bio *bio = mg->new_ocell->holder;
	struct cache *cache = mg->cache;

	calc_discard_block_range(cache, bio, &b, &e);
	while (b != e) {
		set_discard(cache, b);
		b = to_dblock(from_dblock(b) + 1);
	}

	bio_endio(bio);
	cell_defer(cache, mg->new_ocell, false);
	free_migration(mg);
	wake_worker(cache);
}

static void issue_copy_or_discard(struct dm_cache_migration *mg)
{
	bool avoid;
	struct cache *cache = mg->cache;

	if (mg->discard) {
		issue_discard(mg);
		return;
	}

	if (mg->writeback || mg->demote)
		avoid = !is_dirty(cache, mg->cblock) ||
			is_discarded_oblock(cache, mg->old_oblock);
	else {
		struct bio *bio = mg->new_ocell->holder;

		avoid = is_discarded_oblock(cache, mg->new_oblock);

		if (writeback_mode(&cache->features) &&
		    !avoid && bio_writes_complete_block(cache, bio)) {
			issue_overwrite(mg, bio);
			return;
		}
	}

	avoid ? avoid_copy(mg) : issue_copy(mg);
}

static void complete_migration(struct dm_cache_migration *mg)
{
	if (mg->err)
		migration_failure(mg);
	else
		migration_success_pre_commit(mg);
}

static void process_migrations(struct cache *cache, struct list_head *head,
			       void (*fn)(struct dm_cache_migration *))
{
	unsigned long flags;
	struct list_head list;
	struct dm_cache_migration *mg, *tmp;

	INIT_LIST_HEAD(&list);
	spin_lock_irqsave(&cache->lock, flags);
	list_splice_init(head, &list);
	spin_unlock_irqrestore(&cache->lock, flags);

	list_for_each_entry_safe(mg, tmp, &list, list)
		fn(mg);
}

static void __queue_quiesced_migration(struct dm_cache_migration *mg)
{
	list_add_tail(&mg->list, &mg->cache->quiesced_migrations);
}

static void queue_quiesced_migration(struct dm_cache_migration *mg)
{
	unsigned long flags;
	struct cache *cache = mg->cache;

	spin_lock_irqsave(&cache->lock, flags);
	__queue_quiesced_migration(mg);
	spin_unlock_irqrestore(&cache->lock, flags);

	wake_worker(cache);
}

static void queue_quiesced_migrations(struct cache *cache, struct list_head *work)
{
	unsigned long flags;
	struct dm_cache_migration *mg, *tmp;

	spin_lock_irqsave(&cache->lock, flags);
	list_for_each_entry_safe(mg, tmp, work, list)
		__queue_quiesced_migration(mg);
	spin_unlock_irqrestore(&cache->lock, flags);

	wake_worker(cache);
}

static void check_for_quiesced_migrations(struct cache *cache,
					  struct per_bio_data *pb)
{
	struct list_head work;

	if (!pb->all_io_entry)
		return;

	INIT_LIST_HEAD(&work);
	dm_deferred_entry_dec(pb->all_io_entry, &work);

	if (!list_empty(&work))
		queue_quiesced_migrations(cache, &work);
}

static void quiesce_migration(struct dm_cache_migration *mg)
{
	if (!dm_deferred_set_add_work(mg->cache->all_io_ds, &mg->list))
		queue_quiesced_migration(mg);
}

static void promote(struct cache *cache, struct prealloc *structs,
		    dm_oblock_t oblock, dm_cblock_t cblock,
		    struct dm_bio_prison_cell *cell)
{
	struct dm_cache_migration *mg = prealloc_get_migration(structs);

	mg->err = false;
	mg->discard = false;
	mg->writeback = false;
	mg->demote = false;
	mg->promote = true;
	mg->requeue_holder = true;
	mg->invalidate = false;
	mg->cache = cache;
	mg->new_oblock = oblock;
	mg->cblock = cblock;
	mg->old_ocell = NULL;
	mg->new_ocell = cell;
	mg->start_jiffies = jiffies;

	inc_io_migrations(cache);
	quiesce_migration(mg);
}

static void writeback(struct cache *cache, struct prealloc *structs,
		      dm_oblock_t oblock, dm_cblock_t cblock,
		      struct dm_bio_prison_cell *cell)
{
	struct dm_cache_migration *mg = prealloc_get_migration(structs);

	mg->err = false;
	mg->discard = false;
	mg->writeback = true;
	mg->demote = false;
	mg->promote = false;
	mg->requeue_holder = true;
	mg->invalidate = false;
	mg->cache = cache;
	mg->old_oblock = oblock;
	mg->cblock = cblock;
	mg->old_ocell = cell;
	mg->new_ocell = NULL;
	mg->start_jiffies = jiffies;

	inc_io_migrations(cache);
	quiesce_migration(mg);
}

static void demote_then_promote(struct cache *cache, struct prealloc *structs,
				dm_oblock_t old_oblock, dm_oblock_t new_oblock,
				dm_cblock_t cblock,
				struct dm_bio_prison_cell *old_ocell,
				struct dm_bio_prison_cell *new_ocell)
{
	struct dm_cache_migration *mg = prealloc_get_migration(structs);

	mg->err = false;
	mg->discard = false;
	mg->writeback = false;
	mg->demote = true;
	mg->promote = true;
	mg->requeue_holder = true;
	mg->invalidate = false;
	mg->cache = cache;
	mg->old_oblock = old_oblock;
	mg->new_oblock = new_oblock;
	mg->cblock = cblock;
	mg->old_ocell = old_ocell;
	mg->new_ocell = new_ocell;
	mg->start_jiffies = jiffies;

	inc_io_migrations(cache);
	quiesce_migration(mg);
}

/*
 * Invalidate a cache entry.  No writeback occurs; any changes in the cache
 * block are thrown away.
 */
static void invalidate(struct cache *cache, struct prealloc *structs,
		       dm_oblock_t oblock, dm_cblock_t cblock,
		       struct dm_bio_prison_cell *cell)
{
	struct dm_cache_migration *mg = prealloc_get_migration(structs);

	mg->err = false;
	mg->discard = false;
	mg->writeback = false;
	mg->demote = true;
	mg->promote = false;
	mg->requeue_holder = true;
	mg->invalidate = true;
	mg->cache = cache;
	mg->old_oblock = oblock;
	mg->cblock = cblock;
	mg->old_ocell = cell;
	mg->new_ocell = NULL;
	mg->start_jiffies = jiffies;

	inc_io_migrations(cache);
	quiesce_migration(mg);
}

static void discard(struct cache *cache, struct prealloc *structs,
		    struct dm_bio_prison_cell *cell)
{
	struct dm_cache_migration *mg = prealloc_get_migration(structs);

	mg->err = false;
	mg->discard = true;
	mg->writeback = false;
	mg->demote = false;
	mg->promote = false;
	mg->requeue_holder = false;
	mg->invalidate = false;
	mg->cache = cache;
	mg->old_ocell = NULL;
	mg->new_ocell = cell;
	mg->start_jiffies = jiffies;

	quiesce_migration(mg);
}

/*----------------------------------------------------------------
 * bio processing
 *--------------------------------------------------------------*/
static void defer_bio(struct cache *cache, struct bio *bio)
{
	unsigned long flags;

	spin_lock_irqsave(&cache->lock, flags);
	bio_list_add(&cache->deferred_bios, bio);
	spin_unlock_irqrestore(&cache->lock, flags);

	wake_worker(cache);
}

static void process_flush_bio(struct cache *cache, struct bio *bio)
{
	size_t pb_data_size = get_per_bio_data_size(cache);
	struct per_bio_data *pb = get_per_bio_data(bio, pb_data_size);

	BUG_ON(bio->bi_iter.bi_size);
	if (!pb->req_nr)
		remap_to_origin(cache, bio);
	else
		remap_to_cache(cache, bio, 0);

	/*
	 * REQ_PREFLUSH is not directed at any particular block so we don't
	 * need to inc_ds().  REQ_FUA's are split into a write + REQ_PREFLUSH
	 * by dm-core.
	 */
	issue(cache, bio);
}

static void process_discard_bio(struct cache *cache, struct prealloc *structs,
				struct bio *bio)
{
	int r;
	dm_dblock_t b, e;
	struct dm_bio_prison_cell *cell_prealloc, *new_ocell;

	calc_discard_block_range(cache, bio, &b, &e);
	if (b == e) {
		bio_endio(bio);
		return;
	}

	cell_prealloc = prealloc_get_cell(structs);
	r = bio_detain_range(cache, dblock_to_oblock(cache, b), dblock_to_oblock(cache, e), bio, cell_prealloc,
			     (cell_free_fn) prealloc_put_cell,
			     structs, &new_ocell);
	if (r > 0)
		return;

	discard(cache, structs, new_ocell);
}

static bool spare_migration_bandwidth(struct cache *cache)
{
	sector_t current_volume = (atomic_read(&cache->nr_io_migrations) + 1) *
		cache->sectors_per_block;
	return current_volume < cache->migration_threshold;
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

struct inc_detail {
	struct cache *cache;
	struct bio_list bios_for_issue;
	struct bio_list unhandled_bios;
	bool any_writes;
};

static void inc_fn(void *context, struct dm_bio_prison_cell *cell)
{
	struct bio *bio;
	struct inc_detail *detail = context;
	struct cache *cache = detail->cache;

	inc_ds(cache, cell->holder, cell);
	if (bio_data_dir(cell->holder) == WRITE)
		detail->any_writes = true;

	while ((bio = bio_list_pop(&cell->bios))) {
		if (discard_or_flush(bio)) {
			bio_list_add(&detail->unhandled_bios, bio);
			continue;
		}

		if (bio_data_dir(bio) == WRITE)
			detail->any_writes = true;

		bio_list_add(&detail->bios_for_issue, bio);
		inc_ds(cache, bio, cell);
	}
}

// FIXME: refactor these two
static void remap_cell_to_origin_clear_discard(struct cache *cache,
					       struct dm_bio_prison_cell *cell,
					       dm_oblock_t oblock, bool issue_holder)
{
	struct bio *bio;
	unsigned long flags;
	struct inc_detail detail;

	detail.cache = cache;
	bio_list_init(&detail.bios_for_issue);
	bio_list_init(&detail.unhandled_bios);
	detail.any_writes = false;

	spin_lock_irqsave(&cache->lock, flags);
	dm_cell_visit_release(cache->prison, inc_fn, &detail, cell);
	bio_list_merge(&cache->deferred_bios, &detail.unhandled_bios);
	spin_unlock_irqrestore(&cache->lock, flags);

	remap_to_origin(cache, cell->holder);
	if (issue_holder)
		issue(cache, cell->holder);
	else
		accounted_begin(cache, cell->holder);

	if (detail.any_writes)
		clear_discard(cache, oblock_to_dblock(cache, oblock));

	while ((bio = bio_list_pop(&detail.bios_for_issue))) {
		remap_to_origin(cache, bio);
		issue(cache, bio);
	}

	free_prison_cell(cache, cell);
}

static void remap_cell_to_cache_dirty(struct cache *cache, struct dm_bio_prison_cell *cell,
				      dm_oblock_t oblock, dm_cblock_t cblock, bool issue_holder)
{
	struct bio *bio;
	unsigned long flags;
	struct inc_detail detail;

	detail.cache = cache;
	bio_list_init(&detail.bios_for_issue);
	bio_list_init(&detail.unhandled_bios);
	detail.any_writes = false;

	spin_lock_irqsave(&cache->lock, flags);
	dm_cell_visit_release(cache->prison, inc_fn, &detail, cell);
	bio_list_merge(&cache->deferred_bios, &detail.unhandled_bios);
	spin_unlock_irqrestore(&cache->lock, flags);

	remap_to_cache(cache, cell->holder, cblock);
	if (issue_holder)
		issue(cache, cell->holder);
	else
		accounted_begin(cache, cell->holder);

	if (detail.any_writes) {
		set_dirty(cache, oblock, cblock);
		clear_discard(cache, oblock_to_dblock(cache, oblock));
	}

	while ((bio = bio_list_pop(&detail.bios_for_issue))) {
		remap_to_cache(cache, bio, cblock);
		issue(cache, bio);
	}

	free_prison_cell(cache, cell);
}

/*----------------------------------------------------------------*/

struct old_oblock_lock {
	struct policy_locker locker;
	struct cache *cache;
	struct prealloc *structs;
	struct dm_bio_prison_cell *cell;
};

static int null_locker(struct policy_locker *locker, dm_oblock_t b)
{
	/* This should never be called */
	BUG();
	return 0;
}

static int cell_locker(struct policy_locker *locker, dm_oblock_t b)
{
	struct old_oblock_lock *l = container_of(locker, struct old_oblock_lock, locker);
	struct dm_bio_prison_cell *cell_prealloc = prealloc_get_cell(l->structs);

	return bio_detain(l->cache, b, NULL, cell_prealloc,
			  (cell_free_fn) prealloc_put_cell,
			  l->structs, &l->cell);
}

static void process_cell(struct cache *cache, struct prealloc *structs,
			 struct dm_bio_prison_cell *new_ocell)
{
	int r;
	bool release_cell = true;
	struct bio *bio = new_ocell->holder;
	dm_oblock_t block = get_bio_block(cache, bio);
	struct policy_result lookup_result;
	bool passthrough = passthrough_mode(&cache->features);
	bool fast_promotion, can_migrate;
	struct old_oblock_lock ool;

	fast_promotion = is_discarded_oblock(cache, block) || bio_writes_complete_block(cache, bio);
	can_migrate = !passthrough && (fast_promotion || spare_migration_bandwidth(cache));

	ool.locker.fn = cell_locker;
	ool.cache = cache;
	ool.structs = structs;
	ool.cell = NULL;
	r = policy_map(cache->policy, block, true, can_migrate, fast_promotion,
		       bio, &ool.locker, &lookup_result);

	if (r == -EWOULDBLOCK)
		/* migration has been denied */
		lookup_result.op = POLICY_MISS;

	switch (lookup_result.op) {
	case POLICY_HIT:
		if (passthrough) {
			inc_miss_counter(cache, bio);

			/*
			 * Passthrough always maps to the origin,
			 * invalidating any cache blocks that are written
			 * to.
			 */

			if (bio_data_dir(bio) == WRITE) {
				atomic_inc(&cache->stats.demotion);
				invalidate(cache, structs, block, lookup_result.cblock, new_ocell);
				release_cell = false;

			} else {
				/* FIXME: factor out issue_origin() */
				remap_to_origin_clear_discard(cache, bio, block);
				inc_and_issue(cache, bio, new_ocell);
			}
		} else {
			inc_hit_counter(cache, bio);

			if (bio_data_dir(bio) == WRITE &&
			    writethrough_mode(&cache->features) &&
			    !is_dirty(cache, lookup_result.cblock)) {
				remap_to_origin_then_cache(cache, bio, block, lookup_result.cblock);
				inc_and_issue(cache, bio, new_ocell);

			} else {
				remap_cell_to_cache_dirty(cache, new_ocell, block, lookup_result.cblock, true);
				release_cell = false;
			}
		}

		break;

	case POLICY_MISS:
		inc_miss_counter(cache, bio);
		remap_cell_to_origin_clear_discard(cache, new_ocell, block, true);
		release_cell = false;
		break;

	case POLICY_NEW:
		atomic_inc(&cache->stats.promotion);
		promote(cache, structs, block, lookup_result.cblock, new_ocell);
		release_cell = false;
		break;

	case POLICY_REPLACE:
		atomic_inc(&cache->stats.demotion);
		atomic_inc(&cache->stats.promotion);
		demote_then_promote(cache, structs, lookup_result.old_oblock,
				    block, lookup_result.cblock,
				    ool.cell, new_ocell);
		release_cell = false;
		break;

	default:
		DMERR_LIMIT("%s: %s: erroring bio, unknown policy op: %u",
			    cache_device_name(cache), __func__,
			    (unsigned) lookup_result.op);
		bio_io_error(bio);
	}

	if (release_cell)
		cell_defer(cache, new_ocell, false);
}

static void process_bio(struct cache *cache, struct prealloc *structs,
			struct bio *bio)
{
	int r;
	dm_oblock_t block = get_bio_block(cache, bio);
	struct dm_bio_prison_cell *cell_prealloc, *new_ocell;

	/*
	 * Check to see if that block is currently migrating.
	 */
	cell_prealloc = prealloc_get_cell(structs);
	r = bio_detain(cache, block, bio, cell_prealloc,
		       (cell_free_fn) prealloc_put_cell,
		       structs, &new_ocell);
	if (r > 0)
		return;

	process_cell(cache, structs, new_ocell);
}

static int need_commit_due_to_time(struct cache *cache)
{
	return jiffies < cache->last_commit_jiffies ||
	       jiffies > cache->last_commit_jiffies + COMMIT_PERIOD;
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

static int commit_if_needed(struct cache *cache)
{
	int r = 0;

	if ((cache->commit_requested || need_commit_due_to_time(cache)) &&
	    dm_cache_changed_this_transaction(cache->cmd)) {
		r = commit(cache, false);
		cache->commit_requested = false;
		cache->last_commit_jiffies = jiffies;
	}

	return r;
}

static void process_deferred_bios(struct cache *cache)
{
	bool prealloc_used = false;
	unsigned long flags;
	struct bio_list bios;
	struct bio *bio;
	struct prealloc structs;

	memset(&structs, 0, sizeof(structs));
	bio_list_init(&bios);

	spin_lock_irqsave(&cache->lock, flags);
	bio_list_merge(&bios, &cache->deferred_bios);
	bio_list_init(&cache->deferred_bios);
	spin_unlock_irqrestore(&cache->lock, flags);

	while (!bio_list_empty(&bios)) {
		/*
		 * If we've got no free migration structs, and processing
		 * this bio might require one, we pause until there are some
		 * prepared mappings to process.
		 */
		prealloc_used = true;
		if (prealloc_data_structs(cache, &structs)) {
			spin_lock_irqsave(&cache->lock, flags);
			bio_list_merge(&cache->deferred_bios, &bios);
			spin_unlock_irqrestore(&cache->lock, flags);
			break;
		}

		bio = bio_list_pop(&bios);

		if (bio->bi_opf & REQ_PREFLUSH)
			process_flush_bio(cache, bio);
		else if (bio_op(bio) == REQ_OP_DISCARD)
			process_discard_bio(cache, &structs, bio);
		else
			process_bio(cache, &structs, bio);
	}

	if (prealloc_used)
		prealloc_free_structs(cache, &structs);
}

static void process_deferred_cells(struct cache *cache)
{
	bool prealloc_used = false;
	unsigned long flags;
	struct dm_bio_prison_cell *cell, *tmp;
	struct list_head cells;
	struct prealloc structs;

	memset(&structs, 0, sizeof(structs));

	INIT_LIST_HEAD(&cells);

	spin_lock_irqsave(&cache->lock, flags);
	list_splice_init(&cache->deferred_cells, &cells);
	spin_unlock_irqrestore(&cache->lock, flags);

	list_for_each_entry_safe(cell, tmp, &cells, user_list) {
		/*
		 * If we've got no free migration structs, and processing
		 * this bio might require one, we pause until there are some
		 * prepared mappings to process.
		 */
		prealloc_used = true;
		if (prealloc_data_structs(cache, &structs)) {
			spin_lock_irqsave(&cache->lock, flags);
			list_splice(&cells, &cache->deferred_cells);
			spin_unlock_irqrestore(&cache->lock, flags);
			break;
		}

		process_cell(cache, &structs, cell);
	}

	if (prealloc_used)
		prealloc_free_structs(cache, &structs);
}

static void process_deferred_flush_bios(struct cache *cache, bool submit_bios)
{
	unsigned long flags;
	struct bio_list bios;
	struct bio *bio;

	bio_list_init(&bios);

	spin_lock_irqsave(&cache->lock, flags);
	bio_list_merge(&bios, &cache->deferred_flush_bios);
	bio_list_init(&cache->deferred_flush_bios);
	spin_unlock_irqrestore(&cache->lock, flags);

	/*
	 * These bios have already been through inc_ds()
	 */
	while ((bio = bio_list_pop(&bios)))
		submit_bios ? accounted_request(cache, bio) : bio_io_error(bio);
}

static void process_deferred_writethrough_bios(struct cache *cache)
{
	unsigned long flags;
	struct bio_list bios;
	struct bio *bio;

	bio_list_init(&bios);

	spin_lock_irqsave(&cache->lock, flags);
	bio_list_merge(&bios, &cache->deferred_writethrough_bios);
	bio_list_init(&cache->deferred_writethrough_bios);
	spin_unlock_irqrestore(&cache->lock, flags);

	/*
	 * These bios have already been through inc_ds()
	 */
	while ((bio = bio_list_pop(&bios)))
		accounted_request(cache, bio);
}

static void writeback_some_dirty_blocks(struct cache *cache)
{
	bool prealloc_used = false;
	dm_oblock_t oblock;
	dm_cblock_t cblock;
	struct prealloc structs;
	struct dm_bio_prison_cell *old_ocell;
	bool busy = !iot_idle_for(&cache->origin_tracker, HZ);

	memset(&structs, 0, sizeof(structs));

	while (spare_migration_bandwidth(cache)) {
		if (policy_writeback_work(cache->policy, &oblock, &cblock, busy))
			break; /* no work to do */

		prealloc_used = true;
		if (prealloc_data_structs(cache, &structs) ||
		    get_cell(cache, oblock, &structs, &old_ocell)) {
			policy_set_dirty(cache->policy, oblock);
			break;
		}

		writeback(cache, &structs, oblock, cblock, old_ocell);
	}

	if (prealloc_used)
		prealloc_free_structs(cache, &structs);
}

/*----------------------------------------------------------------
 * Invalidations.
 * Dropping something from the cache *without* writing back.
 *--------------------------------------------------------------*/

static void process_invalidation_request(struct cache *cache, struct invalidation_request *req)
{
	int r = 0;
	uint64_t begin = from_cblock(req->cblocks->begin);
	uint64_t end = from_cblock(req->cblocks->end);

	while (begin != end) {
		r = policy_remove_cblock(cache->policy, to_cblock(begin));
		if (!r) {
			r = dm_cache_remove_mapping(cache->cmd, to_cblock(begin));
			if (r) {
				metadata_operation_failed(cache, "dm_cache_remove_mapping", r);
				break;
			}

		} else if (r == -ENODATA) {
			/* harmless, already unmapped */
			r = 0;

		} else {
			DMERR("%s: policy_remove_cblock failed", cache_device_name(cache));
			break;
		}

		begin++;
        }

	cache->commit_requested = true;

	req->err = r;
	atomic_set(&req->complete, 1);

	wake_up(&req->result_wait);
}

static void process_invalidation_requests(struct cache *cache)
{
	struct list_head list;
	struct invalidation_request *req, *tmp;

	INIT_LIST_HEAD(&list);
	spin_lock(&cache->invalidation_lock);
	list_splice_init(&cache->invalidation_requests, &list);
	spin_unlock(&cache->invalidation_lock);

	list_for_each_entry_safe (req, tmp, &list, list)
		process_invalidation_request(cache, req);
}

/*----------------------------------------------------------------
 * Main worker loop
 *--------------------------------------------------------------*/
static bool is_quiescing(struct cache *cache)
{
	return atomic_read(&cache->quiescing);
}

static void ack_quiescing(struct cache *cache)
{
	if (is_quiescing(cache)) {
		atomic_inc(&cache->quiescing_ack);
		wake_up(&cache->quiescing_wait);
	}
}

static void wait_for_quiescing_ack(struct cache *cache)
{
	wait_event(cache->quiescing_wait, atomic_read(&cache->quiescing_ack));
}

static void start_quiescing(struct cache *cache)
{
	atomic_inc(&cache->quiescing);
	wait_for_quiescing_ack(cache);
}

static void stop_quiescing(struct cache *cache)
{
	atomic_set(&cache->quiescing, 0);
	atomic_set(&cache->quiescing_ack, 0);
}

static void wait_for_migrations(struct cache *cache)
{
	wait_event(cache->migration_wait, !atomic_read(&cache->nr_allocated_migrations));
}

static void stop_worker(struct cache *cache)
{
	cancel_delayed_work(&cache->waker);
	flush_workqueue(cache->wq);
}

static void requeue_deferred_cells(struct cache *cache)
{
	unsigned long flags;
	struct list_head cells;
	struct dm_bio_prison_cell *cell, *tmp;

	INIT_LIST_HEAD(&cells);
	spin_lock_irqsave(&cache->lock, flags);
	list_splice_init(&cache->deferred_cells, &cells);
	spin_unlock_irqrestore(&cache->lock, flags);

	list_for_each_entry_safe(cell, tmp, &cells, user_list)
		cell_requeue(cache, cell);
}

static void requeue_deferred_bios(struct cache *cache)
{
	struct bio *bio;
	struct bio_list bios;

	bio_list_init(&bios);
	bio_list_merge(&bios, &cache->deferred_bios);
	bio_list_init(&cache->deferred_bios);

	while ((bio = bio_list_pop(&bios))) {
		bio->bi_error = DM_ENDIO_REQUEUE;
		bio_endio(bio);
	}
}

static int more_work(struct cache *cache)
{
	if (is_quiescing(cache))
		return !list_empty(&cache->quiesced_migrations) ||
			!list_empty(&cache->completed_migrations) ||
			!list_empty(&cache->need_commit_migrations);
	else
		return !bio_list_empty(&cache->deferred_bios) ||
			!list_empty(&cache->deferred_cells) ||
			!bio_list_empty(&cache->deferred_flush_bios) ||
			!bio_list_empty(&cache->deferred_writethrough_bios) ||
			!list_empty(&cache->quiesced_migrations) ||
			!list_empty(&cache->completed_migrations) ||
			!list_empty(&cache->need_commit_migrations) ||
			cache->invalidate;
}

static void do_worker(struct work_struct *ws)
{
	struct cache *cache = container_of(ws, struct cache, worker);

	do {
		if (!is_quiescing(cache)) {
			writeback_some_dirty_blocks(cache);
			process_deferred_writethrough_bios(cache);
			process_deferred_bios(cache);
			process_deferred_cells(cache);
			process_invalidation_requests(cache);
		}

		process_migrations(cache, &cache->quiesced_migrations, issue_copy_or_discard);
		process_migrations(cache, &cache->completed_migrations, complete_migration);

		if (commit_if_needed(cache)) {
			process_deferred_flush_bios(cache, false);
			process_migrations(cache, &cache->need_commit_migrations, migration_failure);
		} else {
			process_deferred_flush_bios(cache, true);
			process_migrations(cache, &cache->need_commit_migrations,
					   migration_success_post_commit);
		}

		ack_quiescing(cache);

	} while (more_work(cache));
}

/*
 * We want to commit periodically so that not too much
 * unwritten metadata builds up.
 */
static void do_waker(struct work_struct *ws)
{
	struct cache *cache = container_of(to_delayed_work(ws), struct cache, waker);
	policy_tick(cache->policy, true);
	wake_worker(cache);
	queue_delayed_work(cache->wq, &cache->waker, COMMIT_PERIOD);
}

/*----------------------------------------------------------------*/

static int is_congested(struct dm_dev *dev, int bdi_bits)
{
	struct request_queue *q = bdev_get_queue(dev->bdev);
	return bdi_congested(&q->backing_dev_info, bdi_bits);
}

static int cache_is_congested(struct dm_target_callbacks *cb, int bdi_bits)
{
	struct cache *cache = container_of(cb, struct cache, callbacks);

	return is_congested(cache->origin_dev, bdi_bits) ||
		is_congested(cache->cache_dev, bdi_bits);
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

	mempool_destroy(cache->migration_pool);

	if (cache->all_io_ds)
		dm_deferred_set_destroy(cache->all_io_ds);

	if (cache->prison)
		dm_bio_prison_destroy(cache->prison);

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
}

static int parse_features(struct cache_args *ca, struct dm_arg_set *as,
			  char **error)
{
	static struct dm_arg _args[] = {
		{0, 1, "Invalid number of cache feature arguments"},
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
	static struct dm_arg _args[] = {
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
	ti->discard_zeroes_data_unsupported = true;
	ti->split_discard_bios = false;

	cache->features = ca->features;
	ti->per_io_data_size = get_per_bio_data_size(cache);

	cache->callbacks.congested_fn = cache_is_congested;
	dm_table_add_target_callbacks(ti->table, &cache->callbacks);

	cache->metadata_dev = ca->metadata_dev;
	cache->origin_dev = ca->origin_dev;
	cache->cache_dev = ca->cache_dev;

	ca->metadata_dev = ca->origin_dev = ca->cache_dev = NULL;

	/* FIXME: factor out this whole section */
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
				     dm_cache_policy_get_hint_size(cache->policy));
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

	if (passthrough_mode(&cache->features)) {
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
	}

	spin_lock_init(&cache->lock);
	INIT_LIST_HEAD(&cache->deferred_cells);
	bio_list_init(&cache->deferred_bios);
	bio_list_init(&cache->deferred_flush_bios);
	bio_list_init(&cache->deferred_writethrough_bios);
	INIT_LIST_HEAD(&cache->quiesced_migrations);
	INIT_LIST_HEAD(&cache->completed_migrations);
	INIT_LIST_HEAD(&cache->need_commit_migrations);
	atomic_set(&cache->nr_allocated_migrations, 0);
	atomic_set(&cache->nr_io_migrations, 0);
	init_waitqueue_head(&cache->migration_wait);

	init_waitqueue_head(&cache->quiescing_wait);
	atomic_set(&cache->quiescing, 0);
	atomic_set(&cache->quiescing_ack, 0);

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

	cache->wq = alloc_ordered_workqueue("dm-" DM_MSG_PREFIX, WQ_MEM_RECLAIM);
	if (!cache->wq) {
		*error = "could not create workqueue for metadata object";
		goto bad;
	}
	INIT_WORK(&cache->worker, do_worker);
	INIT_DELAYED_WORK(&cache->waker, do_waker);
	cache->last_commit_jiffies = jiffies;

	cache->prison = dm_bio_prison_create();
	if (!cache->prison) {
		*error = "could not create bio prison";
		goto bad;
	}

	cache->all_io_ds = dm_deferred_set_create();
	if (!cache->all_io_ds) {
		*error = "could not create all_io deferred set";
		goto bad;
	}

	cache->migration_pool = mempool_create_slab_pool(MIGRATION_POOL_SIZE,
							 migration_cache);
	if (!cache->migration_pool) {
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

	iot_init(&cache->origin_tracker);

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
	struct dm_bio_prison_cell *cell = NULL;
	dm_oblock_t block = get_bio_block(cache, bio);
	size_t pb_data_size = get_per_bio_data_size(cache);
	bool can_migrate = false;
	bool fast_promotion;
	struct policy_result lookup_result;
	struct per_bio_data *pb = init_per_bio_data(bio, pb_data_size);
	struct old_oblock_lock ool;

	ool.locker.fn = null_locker;

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

	/*
	 * Check to see if that block is currently migrating.
	 */
	cell = alloc_prison_cell(cache);
	if (!cell) {
		defer_bio(cache, bio);
		return DM_MAPIO_SUBMITTED;
	}

	r = bio_detain(cache, block, bio, cell,
		       (cell_free_fn) free_prison_cell,
		       cache, &cell);
	if (r) {
		if (r < 0)
			defer_bio(cache, bio);

		return DM_MAPIO_SUBMITTED;
	}

	fast_promotion = is_discarded_oblock(cache, block) || bio_writes_complete_block(cache, bio);

	r = policy_map(cache->policy, block, false, can_migrate, fast_promotion,
		       bio, &ool.locker, &lookup_result);
	if (r == -EWOULDBLOCK) {
		cell_defer(cache, cell, true);
		return DM_MAPIO_SUBMITTED;

	} else if (r) {
		DMERR_LIMIT("%s: Unexpected return from cache replacement policy: %d",
			    cache_device_name(cache), r);
		cell_defer(cache, cell, false);
		bio_io_error(bio);
		return DM_MAPIO_SUBMITTED;
	}

	r = DM_MAPIO_REMAPPED;
	switch (lookup_result.op) {
	case POLICY_HIT:
		if (passthrough_mode(&cache->features)) {
			if (bio_data_dir(bio) == WRITE) {
				/*
				 * We need to invalidate this block, so
				 * defer for the worker thread.
				 */
				cell_defer(cache, cell, true);
				r = DM_MAPIO_SUBMITTED;

			} else {
				inc_miss_counter(cache, bio);
				remap_to_origin_clear_discard(cache, bio, block);
				accounted_begin(cache, bio);
				inc_ds(cache, bio, cell);
				// FIXME: we want to remap hits or misses straight
				// away rather than passing over to the worker.
				cell_defer(cache, cell, false);
			}

		} else {
			inc_hit_counter(cache, bio);
			if (bio_data_dir(bio) == WRITE && writethrough_mode(&cache->features) &&
			    !is_dirty(cache, lookup_result.cblock)) {
				remap_to_origin_then_cache(cache, bio, block, lookup_result.cblock);
				accounted_begin(cache, bio);
				inc_ds(cache, bio, cell);
				cell_defer(cache, cell, false);

			} else
				remap_cell_to_cache_dirty(cache, cell, block, lookup_result.cblock, false);
		}
		break;

	case POLICY_MISS:
		inc_miss_counter(cache, bio);
		if (pb->req_nr != 0) {
			/*
			 * This is a duplicate writethrough io that is no
			 * longer needed because the block has been demoted.
			 */
			bio_endio(bio);
			// FIXME: remap everything as a miss
			cell_defer(cache, cell, false);
			r = DM_MAPIO_SUBMITTED;

		} else
			remap_cell_to_origin_clear_discard(cache, cell, block, false);
		break;

	default:
		DMERR_LIMIT("%s: %s: erroring bio: unknown policy op: %u",
			    cache_device_name(cache), __func__,
			    (unsigned) lookup_result.op);
		cell_defer(cache, cell, false);
		bio_io_error(bio);
		r = DM_MAPIO_SUBMITTED;
	}

	return r;
}

static int cache_end_io(struct dm_target *ti, struct bio *bio, int error)
{
	struct cache *cache = ti->private;
	unsigned long flags;
	size_t pb_data_size = get_per_bio_data_size(cache);
	struct per_bio_data *pb = get_per_bio_data(bio, pb_data_size);

	if (pb->tick) {
		policy_tick(cache->policy, false);

		spin_lock_irqsave(&cache->lock, flags);
		cache->need_tick_bio = true;
		spin_unlock_irqrestore(&cache->lock, flags);
	}

	check_for_quiesced_migrations(cache, pb);
	accounted_complete(cache, bio);

	return 0;
}

static int write_dirty_bitset(struct cache *cache)
{
	unsigned i, r;

	if (get_cache_mode(cache) >= CM_READ_ONLY)
		return -EINVAL;

	for (i = 0; i < from_cblock(cache->cache_size); i++) {
		r = dm_cache_set_dirty(cache->cmd, to_cblock(i),
				       is_dirty(cache, to_cblock(i)));
		if (r) {
			metadata_operation_failed(cache, "dm_cache_set_dirty", r);
			return r;
		}
	}

	return 0;
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

	start_quiescing(cache);
	wait_for_migrations(cache);
	stop_worker(cache);
	requeue_deferred_bios(cache);
	requeue_deferred_cells(cache);
	stop_quiescing(cache);

	if (get_cache_mode(cache) == CM_WRITE)
		(void) sync_metadata(cache);
}

static int load_mapping(void *context, dm_oblock_t oblock, dm_cblock_t cblock,
			bool dirty, uint32_t hint, bool hint_valid)
{
	int r;
	struct cache *cache = context;

	r = policy_load_mapping(cache->policy, oblock, cblock, hint, hint_valid);
	if (r)
		return r;

	if (dirty)
		set_dirty(cache, oblock, cblock);
	else
		clear_dirty(cache, oblock, cblock);

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

		if (writethrough_mode(&cache->features))
			DMEMIT("1 writethrough ");

		else if (passthrough_mode(&cache->features))
			DMEMIT("1 passthrough ");

		else if (writeback_mode(&cache->features))
			DMEMIT("1 writeback ");

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
 * A cache block range can take two forms:
 *
 * i) A single cblock, eg. '3456'
 * ii) A begin and end cblock with dots between, eg. 123-234
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

static int request_invalidation(struct cache *cache, struct cblock_range *range)
{
	struct invalidation_request req;

	INIT_LIST_HEAD(&req.list);
	req.cblocks = range;
	atomic_set(&req.complete, 0);
	req.err = 0;
	init_waitqueue_head(&req.result_wait);

	spin_lock(&cache->invalidation_lock);
	list_add(&req.list, &cache->invalidation_requests);
	spin_unlock(&cache->invalidation_lock);
	wake_worker(cache);

	wait_event(req.result_wait, atomic_read(&req.complete));
	return req.err;
}

static int process_invalidate_cblocks_message(struct cache *cache, unsigned count,
					      const char **cblock_ranges)
{
	int r = 0;
	unsigned i;
	struct cblock_range range;

	if (!passthrough_mode(&cache->features)) {
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
static int cache_message(struct dm_target *ti, unsigned argc, char **argv)
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
	.version = {1, 9, 0},
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

	r = dm_register_target(&cache_target);
	if (r) {
		DMERR("cache target registration failed: %d", r);
		return r;
	}

	migration_cache = KMEM_CACHE(dm_cache_migration, 0);
	if (!migration_cache) {
		dm_unregister_target(&cache_target);
		return -ENOMEM;
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
