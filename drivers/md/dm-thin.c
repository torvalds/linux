// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2011-2012 Red Hat UK.
 *
 * This file is released under the GPL.
 */

#include "dm-thin-metadata.h"
#include "dm-bio-prison-v1.h"
#include "dm.h"

#include <linux/device-mapper.h>
#include <linux/dm-io.h>
#include <linux/dm-kcopyd.h>
#include <linux/jiffies.h>
#include <linux/log2.h>
#include <linux/list.h>
#include <linux/rculist.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/sort.h>
#include <linux/rbtree.h>

#define	DM_MSG_PREFIX	"thin"

/*
 * Tunable constants
 */
#define ENDIO_HOOK_POOL_SIZE 1024
#define MAPPING_POOL_SIZE 1024
#define COMMIT_PERIOD HZ
#define NO_SPACE_TIMEOUT_SECS 60

static unsigned int no_space_timeout_secs = NO_SPACE_TIMEOUT_SECS;

DECLARE_DM_KCOPYD_THROTTLE_WITH_MODULE_PARM(snapshot_copy_throttle,
		"A percentage of time allocated for copy on write");

/*
 * The block size of the device holding pool data must be
 * between 64KB and 1GB.
 */
#define DATA_DEV_BLOCK_SIZE_MIN_SECTORS (64 * 1024 >> SECTOR_SHIFT)
#define DATA_DEV_BLOCK_SIZE_MAX_SECTORS (1024 * 1024 * 1024 >> SECTOR_SHIFT)

/*
 * Device id is restricted to 24 bits.
 */
#define MAX_DEV_ID ((1 << 24) - 1)

/*
 * How do we handle breaking sharing of data blocks?
 * =================================================
 *
 * We use a standard copy-on-write btree to store the mappings for the
 * devices (note I'm talking about copy-on-write of the metadata here, not
 * the data).  When you take an internal snapshot you clone the root node
 * of the origin btree.  After this there is no concept of an origin or a
 * snapshot.  They are just two device trees that happen to point to the
 * same data blocks.
 *
 * When we get a write in we decide if it's to a shared data block using
 * some timestamp magic.  If it is, we have to break sharing.
 *
 * Let's say we write to a shared block in what was the origin.  The
 * steps are:
 *
 * i) plug io further to this physical block. (see bio_prison code).
 *
 * ii) quiesce any read io to that shared data block.  Obviously
 * including all devices that share this block.  (see dm_deferred_set code)
 *
 * iii) copy the data block to a newly allocate block.  This step can be
 * missed out if the io covers the block. (schedule_copy).
 *
 * iv) insert the new mapping into the origin's btree
 * (process_prepared_mapping).  This act of inserting breaks some
 * sharing of btree nodes between the two devices.  Breaking sharing only
 * effects the btree of that specific device.  Btrees for the other
 * devices that share the block never change.  The btree for the origin
 * device as it was after the last commit is untouched, ie. we're using
 * persistent data structures in the functional programming sense.
 *
 * v) unplug io to this physical block, including the io that triggered
 * the breaking of sharing.
 *
 * Steps (ii) and (iii) occur in parallel.
 *
 * The metadata _doesn't_ need to be committed before the io continues.  We
 * get away with this because the io is always written to a _new_ block.
 * If there's a crash, then:
 *
 * - The origin mapping will point to the old origin block (the shared
 * one).  This will contain the data as it was before the io that triggered
 * the breaking of sharing came in.
 *
 * - The snap mapping still points to the old block.  As it would after
 * the commit.
 *
 * The downside of this scheme is the timestamp magic isn't perfect, and
 * will continue to think that data block in the snapshot device is shared
 * even after the write to the origin has broken sharing.  I suspect data
 * blocks will typically be shared by many different devices, so we're
 * breaking sharing n + 1 times, rather than n, where n is the number of
 * devices that reference this data block.  At the moment I think the
 * benefits far, far outweigh the disadvantages.
 */

/*----------------------------------------------------------------*/

/*
 * Key building.
 */
enum lock_space {
	VIRTUAL,
	PHYSICAL
};

static bool build_key(struct dm_thin_device *td, enum lock_space ls,
		      dm_block_t b, dm_block_t e, struct dm_cell_key *key)
{
	key->virtual = (ls == VIRTUAL);
	key->dev = dm_thin_dev_id(td);
	key->block_begin = b;
	key->block_end = e;

	return dm_cell_key_has_valid_range(key);
}

static void build_data_key(struct dm_thin_device *td, dm_block_t b,
			   struct dm_cell_key *key)
{
	(void) build_key(td, PHYSICAL, b, b + 1llu, key);
}

static void build_virtual_key(struct dm_thin_device *td, dm_block_t b,
			      struct dm_cell_key *key)
{
	(void) build_key(td, VIRTUAL, b, b + 1llu, key);
}

/*----------------------------------------------------------------*/

#define THROTTLE_THRESHOLD (1 * HZ)

struct throttle {
	struct rw_semaphore lock;
	unsigned long threshold;
	bool throttle_applied;
};

static void throttle_init(struct throttle *t)
{
	init_rwsem(&t->lock);
	t->throttle_applied = false;
}

static void throttle_work_start(struct throttle *t)
{
	t->threshold = jiffies + THROTTLE_THRESHOLD;
}

static void throttle_work_update(struct throttle *t)
{
	if (!t->throttle_applied && time_is_before_jiffies(t->threshold)) {
		down_write(&t->lock);
		t->throttle_applied = true;
	}
}

static void throttle_work_complete(struct throttle *t)
{
	if (t->throttle_applied) {
		t->throttle_applied = false;
		up_write(&t->lock);
	}
}

static void throttle_lock(struct throttle *t)
{
	down_read(&t->lock);
}

static void throttle_unlock(struct throttle *t)
{
	up_read(&t->lock);
}

/*----------------------------------------------------------------*/

/*
 * A pool device ties together a metadata device and a data device.  It
 * also provides the interface for creating and destroying internal
 * devices.
 */
struct dm_thin_new_mapping;

/*
 * The pool runs in various modes.  Ordered in degraded order for comparisons.
 */
enum pool_mode {
	PM_WRITE,		/* metadata may be changed */
	PM_OUT_OF_DATA_SPACE,	/* metadata may be changed, though data may not be allocated */

	/*
	 * Like READ_ONLY, except may switch back to WRITE on metadata resize. Reported as READ_ONLY.
	 */
	PM_OUT_OF_METADATA_SPACE,
	PM_READ_ONLY,		/* metadata may not be changed */

	PM_FAIL,		/* all I/O fails */
};

struct pool_features {
	enum pool_mode mode;

	bool zero_new_blocks:1;
	bool discard_enabled:1;
	bool discard_passdown:1;
	bool error_if_no_space:1;
};

struct thin_c;
typedef void (*process_bio_fn)(struct thin_c *tc, struct bio *bio);
typedef void (*process_cell_fn)(struct thin_c *tc, struct dm_bio_prison_cell *cell);
typedef void (*process_mapping_fn)(struct dm_thin_new_mapping *m);

#define CELL_SORT_ARRAY_SIZE 8192

struct pool {
	struct list_head list;
	struct dm_target *ti;	/* Only set if a pool target is bound */

	struct mapped_device *pool_md;
	struct block_device *data_dev;
	struct block_device *md_dev;
	struct dm_pool_metadata *pmd;

	dm_block_t low_water_blocks;
	uint32_t sectors_per_block;
	int sectors_per_block_shift;

	struct pool_features pf;
	bool low_water_triggered:1;	/* A dm event has been sent */
	bool suspended:1;
	bool out_of_data_space:1;

	struct dm_bio_prison *prison;
	struct dm_kcopyd_client *copier;

	struct work_struct worker;
	struct workqueue_struct *wq;
	struct throttle throttle;
	struct delayed_work waker;
	struct delayed_work no_space_timeout;

	unsigned long last_commit_jiffies;
	unsigned int ref_count;

	spinlock_t lock;
	struct bio_list deferred_flush_bios;
	struct bio_list deferred_flush_completions;
	struct list_head prepared_mappings;
	struct list_head prepared_discards;
	struct list_head prepared_discards_pt2;
	struct list_head active_thins;

	struct dm_deferred_set *shared_read_ds;
	struct dm_deferred_set *all_io_ds;

	struct dm_thin_new_mapping *next_mapping;

	process_bio_fn process_bio;
	process_bio_fn process_discard;

	process_cell_fn process_cell;
	process_cell_fn process_discard_cell;

	process_mapping_fn process_prepared_mapping;
	process_mapping_fn process_prepared_discard;
	process_mapping_fn process_prepared_discard_pt2;

	struct dm_bio_prison_cell **cell_sort_array;

	mempool_t mapping_pool;
};

static void metadata_operation_failed(struct pool *pool, const char *op, int r);

static enum pool_mode get_pool_mode(struct pool *pool)
{
	return pool->pf.mode;
}

static void notify_of_pool_mode_change(struct pool *pool)
{
	static const char *descs[] = {
		"write",
		"out-of-data-space",
		"read-only",
		"read-only",
		"fail"
	};
	const char *extra_desc = NULL;
	enum pool_mode mode = get_pool_mode(pool);

	if (mode == PM_OUT_OF_DATA_SPACE) {
		if (!pool->pf.error_if_no_space)
			extra_desc = " (queue IO)";
		else
			extra_desc = " (error IO)";
	}

	dm_table_event(pool->ti->table);
	DMINFO("%s: switching pool to %s%s mode",
	       dm_device_name(pool->pool_md),
	       descs[(int)mode], extra_desc ? : "");
}

/*
 * Target context for a pool.
 */
struct pool_c {
	struct dm_target *ti;
	struct pool *pool;
	struct dm_dev *data_dev;
	struct dm_dev *metadata_dev;

	dm_block_t low_water_blocks;
	struct pool_features requested_pf; /* Features requested during table load */
	struct pool_features adjusted_pf;  /* Features used after adjusting for constituent devices */
};

/*
 * Target context for a thin.
 */
struct thin_c {
	struct list_head list;
	struct dm_dev *pool_dev;
	struct dm_dev *origin_dev;
	sector_t origin_size;
	dm_thin_id dev_id;

	struct pool *pool;
	struct dm_thin_device *td;
	struct mapped_device *thin_md;

	bool requeue_mode:1;
	spinlock_t lock;
	struct list_head deferred_cells;
	struct bio_list deferred_bio_list;
	struct bio_list retry_on_resume_list;
	struct rb_root sort_bio_list; /* sorted list of deferred bios */

	/*
	 * Ensures the thin is not destroyed until the worker has finished
	 * iterating the active_thins list.
	 */
	refcount_t refcount;
	struct completion can_destroy;
};

/*----------------------------------------------------------------*/

static bool block_size_is_power_of_two(struct pool *pool)
{
	return pool->sectors_per_block_shift >= 0;
}

static sector_t block_to_sectors(struct pool *pool, dm_block_t b)
{
	return block_size_is_power_of_two(pool) ?
		(b << pool->sectors_per_block_shift) :
		(b * pool->sectors_per_block);
}

/*----------------------------------------------------------------*/

struct discard_op {
	struct thin_c *tc;
	struct blk_plug plug;
	struct bio *parent_bio;
	struct bio *bio;
};

static void begin_discard(struct discard_op *op, struct thin_c *tc, struct bio *parent)
{
	BUG_ON(!parent);

	op->tc = tc;
	blk_start_plug(&op->plug);
	op->parent_bio = parent;
	op->bio = NULL;
}

static int issue_discard(struct discard_op *op, dm_block_t data_b, dm_block_t data_e)
{
	struct thin_c *tc = op->tc;
	sector_t s = block_to_sectors(tc->pool, data_b);
	sector_t len = block_to_sectors(tc->pool, data_e - data_b);

	return __blkdev_issue_discard(tc->pool_dev->bdev, s, len, GFP_NOIO, &op->bio);
}

static void end_discard(struct discard_op *op, int r)
{
	if (op->bio) {
		/*
		 * Even if one of the calls to issue_discard failed, we
		 * need to wait for the chain to complete.
		 */
		bio_chain(op->bio, op->parent_bio);
		op->bio->bi_opf = REQ_OP_DISCARD;
		submit_bio(op->bio);
	}

	blk_finish_plug(&op->plug);

	/*
	 * Even if r is set, there could be sub discards in flight that we
	 * need to wait for.
	 */
	if (r && !op->parent_bio->bi_status)
		op->parent_bio->bi_status = errno_to_blk_status(r);
	bio_endio(op->parent_bio);
}

/*----------------------------------------------------------------*/

/*
 * wake_worker() is used when new work is queued and when pool_resume is
 * ready to continue deferred IO processing.
 */
static void wake_worker(struct pool *pool)
{
	queue_work(pool->wq, &pool->worker);
}

/*----------------------------------------------------------------*/

static int bio_detain(struct pool *pool, struct dm_cell_key *key, struct bio *bio,
		      struct dm_bio_prison_cell **cell_result)
{
	int r;
	struct dm_bio_prison_cell *cell_prealloc;

	/*
	 * Allocate a cell from the prison's mempool.
	 * This might block but it can't fail.
	 */
	cell_prealloc = dm_bio_prison_alloc_cell(pool->prison, GFP_NOIO);

	r = dm_bio_detain(pool->prison, key, bio, cell_prealloc, cell_result);
	if (r) {
		/*
		 * We reused an old cell; we can get rid of
		 * the new one.
		 */
		dm_bio_prison_free_cell(pool->prison, cell_prealloc);
	}

	return r;
}

static void cell_release(struct pool *pool,
			 struct dm_bio_prison_cell *cell,
			 struct bio_list *bios)
{
	dm_cell_release(pool->prison, cell, bios);
	dm_bio_prison_free_cell(pool->prison, cell);
}

static void cell_visit_release(struct pool *pool,
			       void (*fn)(void *, struct dm_bio_prison_cell *),
			       void *context,
			       struct dm_bio_prison_cell *cell)
{
	dm_cell_visit_release(pool->prison, fn, context, cell);
	dm_bio_prison_free_cell(pool->prison, cell);
}

static void cell_release_no_holder(struct pool *pool,
				   struct dm_bio_prison_cell *cell,
				   struct bio_list *bios)
{
	dm_cell_release_no_holder(pool->prison, cell, bios);
	dm_bio_prison_free_cell(pool->prison, cell);
}

static void cell_error_with_code(struct pool *pool,
		struct dm_bio_prison_cell *cell, blk_status_t error_code)
{
	dm_cell_error(pool->prison, cell, error_code);
	dm_bio_prison_free_cell(pool->prison, cell);
}

static blk_status_t get_pool_io_error_code(struct pool *pool)
{
	return pool->out_of_data_space ? BLK_STS_NOSPC : BLK_STS_IOERR;
}

static void cell_error(struct pool *pool, struct dm_bio_prison_cell *cell)
{
	cell_error_with_code(pool, cell, get_pool_io_error_code(pool));
}

static void cell_success(struct pool *pool, struct dm_bio_prison_cell *cell)
{
	cell_error_with_code(pool, cell, 0);
}

static void cell_requeue(struct pool *pool, struct dm_bio_prison_cell *cell)
{
	cell_error_with_code(pool, cell, BLK_STS_DM_REQUEUE);
}

/*----------------------------------------------------------------*/

/*
 * A global list of pools that uses a struct mapped_device as a key.
 */
static struct dm_thin_pool_table {
	struct mutex mutex;
	struct list_head pools;
} dm_thin_pool_table;

static void pool_table_init(void)
{
	mutex_init(&dm_thin_pool_table.mutex);
	INIT_LIST_HEAD(&dm_thin_pool_table.pools);
}

static void pool_table_exit(void)
{
	mutex_destroy(&dm_thin_pool_table.mutex);
}

static void __pool_table_insert(struct pool *pool)
{
	BUG_ON(!mutex_is_locked(&dm_thin_pool_table.mutex));
	list_add(&pool->list, &dm_thin_pool_table.pools);
}

static void __pool_table_remove(struct pool *pool)
{
	BUG_ON(!mutex_is_locked(&dm_thin_pool_table.mutex));
	list_del(&pool->list);
}

static struct pool *__pool_table_lookup(struct mapped_device *md)
{
	struct pool *pool = NULL, *tmp;

	BUG_ON(!mutex_is_locked(&dm_thin_pool_table.mutex));

	list_for_each_entry(tmp, &dm_thin_pool_table.pools, list) {
		if (tmp->pool_md == md) {
			pool = tmp;
			break;
		}
	}

	return pool;
}

static struct pool *__pool_table_lookup_metadata_dev(struct block_device *md_dev)
{
	struct pool *pool = NULL, *tmp;

	BUG_ON(!mutex_is_locked(&dm_thin_pool_table.mutex));

	list_for_each_entry(tmp, &dm_thin_pool_table.pools, list) {
		if (tmp->md_dev == md_dev) {
			pool = tmp;
			break;
		}
	}

	return pool;
}

/*----------------------------------------------------------------*/

struct dm_thin_endio_hook {
	struct thin_c *tc;
	struct dm_deferred_entry *shared_read_entry;
	struct dm_deferred_entry *all_io_entry;
	struct dm_thin_new_mapping *overwrite_mapping;
	struct rb_node rb_node;
	struct dm_bio_prison_cell *cell;
};

static void __merge_bio_list(struct bio_list *bios, struct bio_list *master)
{
	bio_list_merge(bios, master);
	bio_list_init(master);
}

static void error_bio_list(struct bio_list *bios, blk_status_t error)
{
	struct bio *bio;

	while ((bio = bio_list_pop(bios))) {
		bio->bi_status = error;
		bio_endio(bio);
	}
}

static void error_thin_bio_list(struct thin_c *tc, struct bio_list *master,
		blk_status_t error)
{
	struct bio_list bios;

	bio_list_init(&bios);

	spin_lock_irq(&tc->lock);
	__merge_bio_list(&bios, master);
	spin_unlock_irq(&tc->lock);

	error_bio_list(&bios, error);
}

static void requeue_deferred_cells(struct thin_c *tc)
{
	struct pool *pool = tc->pool;
	struct list_head cells;
	struct dm_bio_prison_cell *cell, *tmp;

	INIT_LIST_HEAD(&cells);

	spin_lock_irq(&tc->lock);
	list_splice_init(&tc->deferred_cells, &cells);
	spin_unlock_irq(&tc->lock);

	list_for_each_entry_safe(cell, tmp, &cells, user_list)
		cell_requeue(pool, cell);
}

static void requeue_io(struct thin_c *tc)
{
	struct bio_list bios;

	bio_list_init(&bios);

	spin_lock_irq(&tc->lock);
	__merge_bio_list(&bios, &tc->deferred_bio_list);
	__merge_bio_list(&bios, &tc->retry_on_resume_list);
	spin_unlock_irq(&tc->lock);

	error_bio_list(&bios, BLK_STS_DM_REQUEUE);
	requeue_deferred_cells(tc);
}

static void error_retry_list_with_code(struct pool *pool, blk_status_t error)
{
	struct thin_c *tc;

	rcu_read_lock();
	list_for_each_entry_rcu(tc, &pool->active_thins, list)
		error_thin_bio_list(tc, &tc->retry_on_resume_list, error);
	rcu_read_unlock();
}

static void error_retry_list(struct pool *pool)
{
	error_retry_list_with_code(pool, get_pool_io_error_code(pool));
}

/*
 * This section of code contains the logic for processing a thin device's IO.
 * Much of the code depends on pool object resources (lists, workqueues, etc)
 * but most is exclusively called from the thin target rather than the thin-pool
 * target.
 */

static dm_block_t get_bio_block(struct thin_c *tc, struct bio *bio)
{
	struct pool *pool = tc->pool;
	sector_t block_nr = bio->bi_iter.bi_sector;

	if (block_size_is_power_of_two(pool))
		block_nr >>= pool->sectors_per_block_shift;
	else
		(void) sector_div(block_nr, pool->sectors_per_block);

	return block_nr;
}

/*
 * Returns the _complete_ blocks that this bio covers.
 */
static void get_bio_block_range(struct thin_c *tc, struct bio *bio,
				dm_block_t *begin, dm_block_t *end)
{
	struct pool *pool = tc->pool;
	sector_t b = bio->bi_iter.bi_sector;
	sector_t e = b + (bio->bi_iter.bi_size >> SECTOR_SHIFT);

	b += pool->sectors_per_block - 1ull; /* so we round up */

	if (block_size_is_power_of_two(pool)) {
		b >>= pool->sectors_per_block_shift;
		e >>= pool->sectors_per_block_shift;
	} else {
		(void) sector_div(b, pool->sectors_per_block);
		(void) sector_div(e, pool->sectors_per_block);
	}

	if (e < b) {
		/* Can happen if the bio is within a single block. */
		e = b;
	}

	*begin = b;
	*end = e;
}

static void remap(struct thin_c *tc, struct bio *bio, dm_block_t block)
{
	struct pool *pool = tc->pool;
	sector_t bi_sector = bio->bi_iter.bi_sector;

	bio_set_dev(bio, tc->pool_dev->bdev);
	if (block_size_is_power_of_two(pool)) {
		bio->bi_iter.bi_sector =
			(block << pool->sectors_per_block_shift) |
			(bi_sector & (pool->sectors_per_block - 1));
	} else {
		bio->bi_iter.bi_sector = (block * pool->sectors_per_block) +
				 sector_div(bi_sector, pool->sectors_per_block);
	}
}

static void remap_to_origin(struct thin_c *tc, struct bio *bio)
{
	bio_set_dev(bio, tc->origin_dev->bdev);
}

static int bio_triggers_commit(struct thin_c *tc, struct bio *bio)
{
	return op_is_flush(bio->bi_opf) &&
		dm_thin_changed_this_transaction(tc->td);
}

static void inc_all_io_entry(struct pool *pool, struct bio *bio)
{
	struct dm_thin_endio_hook *h;

	if (bio_op(bio) == REQ_OP_DISCARD)
		return;

	h = dm_per_bio_data(bio, sizeof(struct dm_thin_endio_hook));
	h->all_io_entry = dm_deferred_entry_inc(pool->all_io_ds);
}

static void issue(struct thin_c *tc, struct bio *bio)
{
	struct pool *pool = tc->pool;

	if (!bio_triggers_commit(tc, bio)) {
		dm_submit_bio_remap(bio, NULL);
		return;
	}

	/*
	 * Complete bio with an error if earlier I/O caused changes to
	 * the metadata that can't be committed e.g, due to I/O errors
	 * on the metadata device.
	 */
	if (dm_thin_aborted_changes(tc->td)) {
		bio_io_error(bio);
		return;
	}

	/*
	 * Batch together any bios that trigger commits and then issue a
	 * single commit for them in process_deferred_bios().
	 */
	spin_lock_irq(&pool->lock);
	bio_list_add(&pool->deferred_flush_bios, bio);
	spin_unlock_irq(&pool->lock);
}

static void remap_to_origin_and_issue(struct thin_c *tc, struct bio *bio)
{
	remap_to_origin(tc, bio);
	issue(tc, bio);
}

static void remap_and_issue(struct thin_c *tc, struct bio *bio,
			    dm_block_t block)
{
	remap(tc, bio, block);
	issue(tc, bio);
}

/*----------------------------------------------------------------*/

/*
 * Bio endio functions.
 */
struct dm_thin_new_mapping {
	struct list_head list;

	bool pass_discard:1;
	bool maybe_shared:1;

	/*
	 * Track quiescing, copying and zeroing preparation actions.  When this
	 * counter hits zero the block is prepared and can be inserted into the
	 * btree.
	 */
	atomic_t prepare_actions;

	blk_status_t status;
	struct thin_c *tc;
	dm_block_t virt_begin, virt_end;
	dm_block_t data_block;
	struct dm_bio_prison_cell *cell;

	/*
	 * If the bio covers the whole area of a block then we can avoid
	 * zeroing or copying.  Instead this bio is hooked.  The bio will
	 * still be in the cell, so care has to be taken to avoid issuing
	 * the bio twice.
	 */
	struct bio *bio;
	bio_end_io_t *saved_bi_end_io;
};

static void __complete_mapping_preparation(struct dm_thin_new_mapping *m)
{
	struct pool *pool = m->tc->pool;

	if (atomic_dec_and_test(&m->prepare_actions)) {
		list_add_tail(&m->list, &pool->prepared_mappings);
		wake_worker(pool);
	}
}

static void complete_mapping_preparation(struct dm_thin_new_mapping *m)
{
	unsigned long flags;
	struct pool *pool = m->tc->pool;

	spin_lock_irqsave(&pool->lock, flags);
	__complete_mapping_preparation(m);
	spin_unlock_irqrestore(&pool->lock, flags);
}

static void copy_complete(int read_err, unsigned long write_err, void *context)
{
	struct dm_thin_new_mapping *m = context;

	m->status = read_err || write_err ? BLK_STS_IOERR : 0;
	complete_mapping_preparation(m);
}

static void overwrite_endio(struct bio *bio)
{
	struct dm_thin_endio_hook *h = dm_per_bio_data(bio, sizeof(struct dm_thin_endio_hook));
	struct dm_thin_new_mapping *m = h->overwrite_mapping;

	bio->bi_end_io = m->saved_bi_end_io;

	m->status = bio->bi_status;
	complete_mapping_preparation(m);
}

/*----------------------------------------------------------------*/

/*
 * Workqueue.
 */

/*
 * Prepared mapping jobs.
 */

/*
 * This sends the bios in the cell, except the original holder, back
 * to the deferred_bios list.
 */
static void cell_defer_no_holder(struct thin_c *tc, struct dm_bio_prison_cell *cell)
{
	struct pool *pool = tc->pool;
	unsigned long flags;
	struct bio_list bios;

	bio_list_init(&bios);
	cell_release_no_holder(pool, cell, &bios);

	if (!bio_list_empty(&bios)) {
		spin_lock_irqsave(&tc->lock, flags);
		bio_list_merge(&tc->deferred_bio_list, &bios);
		spin_unlock_irqrestore(&tc->lock, flags);
		wake_worker(pool);
	}
}

static void thin_defer_bio(struct thin_c *tc, struct bio *bio);

struct remap_info {
	struct thin_c *tc;
	struct bio_list defer_bios;
	struct bio_list issue_bios;
};

static void __inc_remap_and_issue_cell(void *context,
				       struct dm_bio_prison_cell *cell)
{
	struct remap_info *info = context;
	struct bio *bio;

	while ((bio = bio_list_pop(&cell->bios))) {
		if (op_is_flush(bio->bi_opf) || bio_op(bio) == REQ_OP_DISCARD)
			bio_list_add(&info->defer_bios, bio);
		else {
			inc_all_io_entry(info->tc->pool, bio);

			/*
			 * We can't issue the bios with the bio prison lock
			 * held, so we add them to a list to issue on
			 * return from this function.
			 */
			bio_list_add(&info->issue_bios, bio);
		}
	}
}

static void inc_remap_and_issue_cell(struct thin_c *tc,
				     struct dm_bio_prison_cell *cell,
				     dm_block_t block)
{
	struct bio *bio;
	struct remap_info info;

	info.tc = tc;
	bio_list_init(&info.defer_bios);
	bio_list_init(&info.issue_bios);

	/*
	 * We have to be careful to inc any bios we're about to issue
	 * before the cell is released, and avoid a race with new bios
	 * being added to the cell.
	 */
	cell_visit_release(tc->pool, __inc_remap_and_issue_cell,
			   &info, cell);

	while ((bio = bio_list_pop(&info.defer_bios)))
		thin_defer_bio(tc, bio);

	while ((bio = bio_list_pop(&info.issue_bios)))
		remap_and_issue(info.tc, bio, block);
}

static void process_prepared_mapping_fail(struct dm_thin_new_mapping *m)
{
	cell_error(m->tc->pool, m->cell);
	list_del(&m->list);
	mempool_free(m, &m->tc->pool->mapping_pool);
}

static void complete_overwrite_bio(struct thin_c *tc, struct bio *bio)
{
	struct pool *pool = tc->pool;

	/*
	 * If the bio has the REQ_FUA flag set we must commit the metadata
	 * before signaling its completion.
	 */
	if (!bio_triggers_commit(tc, bio)) {
		bio_endio(bio);
		return;
	}

	/*
	 * Complete bio with an error if earlier I/O caused changes to the
	 * metadata that can't be committed, e.g, due to I/O errors on the
	 * metadata device.
	 */
	if (dm_thin_aborted_changes(tc->td)) {
		bio_io_error(bio);
		return;
	}

	/*
	 * Batch together any bios that trigger commits and then issue a
	 * single commit for them in process_deferred_bios().
	 */
	spin_lock_irq(&pool->lock);
	bio_list_add(&pool->deferred_flush_completions, bio);
	spin_unlock_irq(&pool->lock);
}

static void process_prepared_mapping(struct dm_thin_new_mapping *m)
{
	struct thin_c *tc = m->tc;
	struct pool *pool = tc->pool;
	struct bio *bio = m->bio;
	int r;

	if (m->status) {
		cell_error(pool, m->cell);
		goto out;
	}

	/*
	 * Commit the prepared block into the mapping btree.
	 * Any I/O for this block arriving after this point will get
	 * remapped to it directly.
	 */
	r = dm_thin_insert_block(tc->td, m->virt_begin, m->data_block);
	if (r) {
		metadata_operation_failed(pool, "dm_thin_insert_block", r);
		cell_error(pool, m->cell);
		goto out;
	}

	/*
	 * Release any bios held while the block was being provisioned.
	 * If we are processing a write bio that completely covers the block,
	 * we already processed it so can ignore it now when processing
	 * the bios in the cell.
	 */
	if (bio) {
		inc_remap_and_issue_cell(tc, m->cell, m->data_block);
		complete_overwrite_bio(tc, bio);
	} else {
		inc_all_io_entry(tc->pool, m->cell->holder);
		remap_and_issue(tc, m->cell->holder, m->data_block);
		inc_remap_and_issue_cell(tc, m->cell, m->data_block);
	}

out:
	list_del(&m->list);
	mempool_free(m, &pool->mapping_pool);
}

/*----------------------------------------------------------------*/

static void free_discard_mapping(struct dm_thin_new_mapping *m)
{
	struct thin_c *tc = m->tc;

	if (m->cell)
		cell_defer_no_holder(tc, m->cell);
	mempool_free(m, &tc->pool->mapping_pool);
}

static void process_prepared_discard_fail(struct dm_thin_new_mapping *m)
{
	bio_io_error(m->bio);
	free_discard_mapping(m);
}

static void process_prepared_discard_success(struct dm_thin_new_mapping *m)
{
	bio_endio(m->bio);
	free_discard_mapping(m);
}

static void process_prepared_discard_no_passdown(struct dm_thin_new_mapping *m)
{
	int r;
	struct thin_c *tc = m->tc;

	r = dm_thin_remove_range(tc->td, m->cell->key.block_begin, m->cell->key.block_end);
	if (r) {
		metadata_operation_failed(tc->pool, "dm_thin_remove_range", r);
		bio_io_error(m->bio);
	} else
		bio_endio(m->bio);

	cell_defer_no_holder(tc, m->cell);
	mempool_free(m, &tc->pool->mapping_pool);
}

/*----------------------------------------------------------------*/

static void passdown_double_checking_shared_status(struct dm_thin_new_mapping *m,
						   struct bio *discard_parent)
{
	/*
	 * We've already unmapped this range of blocks, but before we
	 * passdown we have to check that these blocks are now unused.
	 */
	int r = 0;
	bool shared = true;
	struct thin_c *tc = m->tc;
	struct pool *pool = tc->pool;
	dm_block_t b = m->data_block, e, end = m->data_block + m->virt_end - m->virt_begin;
	struct discard_op op;

	begin_discard(&op, tc, discard_parent);
	while (b != end) {
		/* find start of unmapped run */
		for (; b < end; b++) {
			r = dm_pool_block_is_shared(pool->pmd, b, &shared);
			if (r)
				goto out;

			if (!shared)
				break;
		}

		if (b == end)
			break;

		/* find end of run */
		for (e = b + 1; e != end; e++) {
			r = dm_pool_block_is_shared(pool->pmd, e, &shared);
			if (r)
				goto out;

			if (shared)
				break;
		}

		r = issue_discard(&op, b, e);
		if (r)
			goto out;

		b = e;
	}
out:
	end_discard(&op, r);
}

static void queue_passdown_pt2(struct dm_thin_new_mapping *m)
{
	unsigned long flags;
	struct pool *pool = m->tc->pool;

	spin_lock_irqsave(&pool->lock, flags);
	list_add_tail(&m->list, &pool->prepared_discards_pt2);
	spin_unlock_irqrestore(&pool->lock, flags);
	wake_worker(pool);
}

static void passdown_endio(struct bio *bio)
{
	/*
	 * It doesn't matter if the passdown discard failed, we still want
	 * to unmap (we ignore err).
	 */
	queue_passdown_pt2(bio->bi_private);
	bio_put(bio);
}

static void process_prepared_discard_passdown_pt1(struct dm_thin_new_mapping *m)
{
	int r;
	struct thin_c *tc = m->tc;
	struct pool *pool = tc->pool;
	struct bio *discard_parent;
	dm_block_t data_end = m->data_block + (m->virt_end - m->virt_begin);

	/*
	 * Only this thread allocates blocks, so we can be sure that the
	 * newly unmapped blocks will not be allocated before the end of
	 * the function.
	 */
	r = dm_thin_remove_range(tc->td, m->virt_begin, m->virt_end);
	if (r) {
		metadata_operation_failed(pool, "dm_thin_remove_range", r);
		bio_io_error(m->bio);
		cell_defer_no_holder(tc, m->cell);
		mempool_free(m, &pool->mapping_pool);
		return;
	}

	/*
	 * Increment the unmapped blocks.  This prevents a race between the
	 * passdown io and reallocation of freed blocks.
	 */
	r = dm_pool_inc_data_range(pool->pmd, m->data_block, data_end);
	if (r) {
		metadata_operation_failed(pool, "dm_pool_inc_data_range", r);
		bio_io_error(m->bio);
		cell_defer_no_holder(tc, m->cell);
		mempool_free(m, &pool->mapping_pool);
		return;
	}

	discard_parent = bio_alloc(NULL, 1, 0, GFP_NOIO);
	discard_parent->bi_end_io = passdown_endio;
	discard_parent->bi_private = m;
	if (m->maybe_shared)
		passdown_double_checking_shared_status(m, discard_parent);
	else {
		struct discard_op op;

		begin_discard(&op, tc, discard_parent);
		r = issue_discard(&op, m->data_block, data_end);
		end_discard(&op, r);
	}
}

static void process_prepared_discard_passdown_pt2(struct dm_thin_new_mapping *m)
{
	int r;
	struct thin_c *tc = m->tc;
	struct pool *pool = tc->pool;

	/*
	 * The passdown has completed, so now we can decrement all those
	 * unmapped blocks.
	 */
	r = dm_pool_dec_data_range(pool->pmd, m->data_block,
				   m->data_block + (m->virt_end - m->virt_begin));
	if (r) {
		metadata_operation_failed(pool, "dm_pool_dec_data_range", r);
		bio_io_error(m->bio);
	} else
		bio_endio(m->bio);

	cell_defer_no_holder(tc, m->cell);
	mempool_free(m, &pool->mapping_pool);
}

static void process_prepared(struct pool *pool, struct list_head *head,
			     process_mapping_fn *fn)
{
	struct list_head maps;
	struct dm_thin_new_mapping *m, *tmp;

	INIT_LIST_HEAD(&maps);
	spin_lock_irq(&pool->lock);
	list_splice_init(head, &maps);
	spin_unlock_irq(&pool->lock);

	list_for_each_entry_safe(m, tmp, &maps, list)
		(*fn)(m);
}

/*
 * Deferred bio jobs.
 */
static int io_overlaps_block(struct pool *pool, struct bio *bio)
{
	return bio->bi_iter.bi_size ==
		(pool->sectors_per_block << SECTOR_SHIFT);
}

static int io_overwrites_block(struct pool *pool, struct bio *bio)
{
	return (bio_data_dir(bio) == WRITE) &&
		io_overlaps_block(pool, bio);
}

static void save_and_set_endio(struct bio *bio, bio_end_io_t **save,
			       bio_end_io_t *fn)
{
	*save = bio->bi_end_io;
	bio->bi_end_io = fn;
}

static int ensure_next_mapping(struct pool *pool)
{
	if (pool->next_mapping)
		return 0;

	pool->next_mapping = mempool_alloc(&pool->mapping_pool, GFP_ATOMIC);

	return pool->next_mapping ? 0 : -ENOMEM;
}

static struct dm_thin_new_mapping *get_next_mapping(struct pool *pool)
{
	struct dm_thin_new_mapping *m = pool->next_mapping;

	BUG_ON(!pool->next_mapping);

	memset(m, 0, sizeof(struct dm_thin_new_mapping));
	INIT_LIST_HEAD(&m->list);
	m->bio = NULL;

	pool->next_mapping = NULL;

	return m;
}

static void ll_zero(struct thin_c *tc, struct dm_thin_new_mapping *m,
		    sector_t begin, sector_t end)
{
	struct dm_io_region to;

	to.bdev = tc->pool_dev->bdev;
	to.sector = begin;
	to.count = end - begin;

	dm_kcopyd_zero(tc->pool->copier, 1, &to, 0, copy_complete, m);
}

static void remap_and_issue_overwrite(struct thin_c *tc, struct bio *bio,
				      dm_block_t data_begin,
				      struct dm_thin_new_mapping *m)
{
	struct pool *pool = tc->pool;
	struct dm_thin_endio_hook *h = dm_per_bio_data(bio, sizeof(struct dm_thin_endio_hook));

	h->overwrite_mapping = m;
	m->bio = bio;
	save_and_set_endio(bio, &m->saved_bi_end_io, overwrite_endio);
	inc_all_io_entry(pool, bio);
	remap_and_issue(tc, bio, data_begin);
}

/*
 * A partial copy also needs to zero the uncopied region.
 */
static void schedule_copy(struct thin_c *tc, dm_block_t virt_block,
			  struct dm_dev *origin, dm_block_t data_origin,
			  dm_block_t data_dest,
			  struct dm_bio_prison_cell *cell, struct bio *bio,
			  sector_t len)
{
	struct pool *pool = tc->pool;
	struct dm_thin_new_mapping *m = get_next_mapping(pool);

	m->tc = tc;
	m->virt_begin = virt_block;
	m->virt_end = virt_block + 1u;
	m->data_block = data_dest;
	m->cell = cell;

	/*
	 * quiesce action + copy action + an extra reference held for the
	 * duration of this function (we may need to inc later for a
	 * partial zero).
	 */
	atomic_set(&m->prepare_actions, 3);

	if (!dm_deferred_set_add_work(pool->shared_read_ds, &m->list))
		complete_mapping_preparation(m); /* already quiesced */

	/*
	 * IO to pool_dev remaps to the pool target's data_dev.
	 *
	 * If the whole block of data is being overwritten, we can issue the
	 * bio immediately. Otherwise we use kcopyd to clone the data first.
	 */
	if (io_overwrites_block(pool, bio))
		remap_and_issue_overwrite(tc, bio, data_dest, m);
	else {
		struct dm_io_region from, to;

		from.bdev = origin->bdev;
		from.sector = data_origin * pool->sectors_per_block;
		from.count = len;

		to.bdev = tc->pool_dev->bdev;
		to.sector = data_dest * pool->sectors_per_block;
		to.count = len;

		dm_kcopyd_copy(pool->copier, &from, 1, &to,
			       0, copy_complete, m);

		/*
		 * Do we need to zero a tail region?
		 */
		if (len < pool->sectors_per_block && pool->pf.zero_new_blocks) {
			atomic_inc(&m->prepare_actions);
			ll_zero(tc, m,
				data_dest * pool->sectors_per_block + len,
				(data_dest + 1) * pool->sectors_per_block);
		}
	}

	complete_mapping_preparation(m); /* drop our ref */
}

static void schedule_internal_copy(struct thin_c *tc, dm_block_t virt_block,
				   dm_block_t data_origin, dm_block_t data_dest,
				   struct dm_bio_prison_cell *cell, struct bio *bio)
{
	schedule_copy(tc, virt_block, tc->pool_dev,
		      data_origin, data_dest, cell, bio,
		      tc->pool->sectors_per_block);
}

static void schedule_zero(struct thin_c *tc, dm_block_t virt_block,
			  dm_block_t data_block, struct dm_bio_prison_cell *cell,
			  struct bio *bio)
{
	struct pool *pool = tc->pool;
	struct dm_thin_new_mapping *m = get_next_mapping(pool);

	atomic_set(&m->prepare_actions, 1); /* no need to quiesce */
	m->tc = tc;
	m->virt_begin = virt_block;
	m->virt_end = virt_block + 1u;
	m->data_block = data_block;
	m->cell = cell;

	/*
	 * If the whole block of data is being overwritten or we are not
	 * zeroing pre-existing data, we can issue the bio immediately.
	 * Otherwise we use kcopyd to zero the data first.
	 */
	if (pool->pf.zero_new_blocks) {
		if (io_overwrites_block(pool, bio))
			remap_and_issue_overwrite(tc, bio, data_block, m);
		else {
			ll_zero(tc, m, data_block * pool->sectors_per_block,
				(data_block + 1) * pool->sectors_per_block);
		}
	} else
		process_prepared_mapping(m);
}

static void schedule_external_copy(struct thin_c *tc, dm_block_t virt_block,
				   dm_block_t data_dest,
				   struct dm_bio_prison_cell *cell, struct bio *bio)
{
	struct pool *pool = tc->pool;
	sector_t virt_block_begin = virt_block * pool->sectors_per_block;
	sector_t virt_block_end = (virt_block + 1) * pool->sectors_per_block;

	if (virt_block_end <= tc->origin_size) {
		schedule_copy(tc, virt_block, tc->origin_dev,
			      virt_block, data_dest, cell, bio,
			      pool->sectors_per_block);

	} else if (virt_block_begin < tc->origin_size) {
		schedule_copy(tc, virt_block, tc->origin_dev,
			      virt_block, data_dest, cell, bio,
			      tc->origin_size - virt_block_begin);

	} else
		schedule_zero(tc, virt_block, data_dest, cell, bio);
}

static void set_pool_mode(struct pool *pool, enum pool_mode new_mode);

static void requeue_bios(struct pool *pool);

static bool is_read_only_pool_mode(enum pool_mode mode)
{
	return (mode == PM_OUT_OF_METADATA_SPACE || mode == PM_READ_ONLY);
}

static bool is_read_only(struct pool *pool)
{
	return is_read_only_pool_mode(get_pool_mode(pool));
}

static void check_for_metadata_space(struct pool *pool)
{
	int r;
	const char *ooms_reason = NULL;
	dm_block_t nr_free;

	r = dm_pool_get_free_metadata_block_count(pool->pmd, &nr_free);
	if (r)
		ooms_reason = "Could not get free metadata blocks";
	else if (!nr_free)
		ooms_reason = "No free metadata blocks";

	if (ooms_reason && !is_read_only(pool)) {
		DMERR("%s", ooms_reason);
		set_pool_mode(pool, PM_OUT_OF_METADATA_SPACE);
	}
}

static void check_for_data_space(struct pool *pool)
{
	int r;
	dm_block_t nr_free;

	if (get_pool_mode(pool) != PM_OUT_OF_DATA_SPACE)
		return;

	r = dm_pool_get_free_block_count(pool->pmd, &nr_free);
	if (r)
		return;

	if (nr_free) {
		set_pool_mode(pool, PM_WRITE);
		requeue_bios(pool);
	}
}

/*
 * A non-zero return indicates read_only or fail_io mode.
 * Many callers don't care about the return value.
 */
static int commit(struct pool *pool)
{
	int r;

	if (get_pool_mode(pool) >= PM_OUT_OF_METADATA_SPACE)
		return -EINVAL;

	r = dm_pool_commit_metadata(pool->pmd);
	if (r)
		metadata_operation_failed(pool, "dm_pool_commit_metadata", r);
	else {
		check_for_metadata_space(pool);
		check_for_data_space(pool);
	}

	return r;
}

static void check_low_water_mark(struct pool *pool, dm_block_t free_blocks)
{
	if (free_blocks <= pool->low_water_blocks && !pool->low_water_triggered) {
		DMWARN("%s: reached low water mark for data device: sending event.",
		       dm_device_name(pool->pool_md));
		spin_lock_irq(&pool->lock);
		pool->low_water_triggered = true;
		spin_unlock_irq(&pool->lock);
		dm_table_event(pool->ti->table);
	}
}

static int alloc_data_block(struct thin_c *tc, dm_block_t *result)
{
	int r;
	dm_block_t free_blocks;
	struct pool *pool = tc->pool;

	if (WARN_ON(get_pool_mode(pool) != PM_WRITE))
		return -EINVAL;

	r = dm_pool_get_free_block_count(pool->pmd, &free_blocks);
	if (r) {
		metadata_operation_failed(pool, "dm_pool_get_free_block_count", r);
		return r;
	}

	check_low_water_mark(pool, free_blocks);

	if (!free_blocks) {
		/*
		 * Try to commit to see if that will free up some
		 * more space.
		 */
		r = commit(pool);
		if (r)
			return r;

		r = dm_pool_get_free_block_count(pool->pmd, &free_blocks);
		if (r) {
			metadata_operation_failed(pool, "dm_pool_get_free_block_count", r);
			return r;
		}

		if (!free_blocks) {
			set_pool_mode(pool, PM_OUT_OF_DATA_SPACE);
			return -ENOSPC;
		}
	}

	r = dm_pool_alloc_data_block(pool->pmd, result);
	if (r) {
		if (r == -ENOSPC)
			set_pool_mode(pool, PM_OUT_OF_DATA_SPACE);
		else
			metadata_operation_failed(pool, "dm_pool_alloc_data_block", r);
		return r;
	}

	r = dm_pool_get_free_metadata_block_count(pool->pmd, &free_blocks);
	if (r) {
		metadata_operation_failed(pool, "dm_pool_get_free_metadata_block_count", r);
		return r;
	}

	if (!free_blocks) {
		/* Let's commit before we use up the metadata reserve. */
		r = commit(pool);
		if (r)
			return r;
	}

	return 0;
}

/*
 * If we have run out of space, queue bios until the device is
 * resumed, presumably after having been reloaded with more space.
 */
static void retry_on_resume(struct bio *bio)
{
	struct dm_thin_endio_hook *h = dm_per_bio_data(bio, sizeof(struct dm_thin_endio_hook));
	struct thin_c *tc = h->tc;

	spin_lock_irq(&tc->lock);
	bio_list_add(&tc->retry_on_resume_list, bio);
	spin_unlock_irq(&tc->lock);
}

static blk_status_t should_error_unserviceable_bio(struct pool *pool)
{
	enum pool_mode m = get_pool_mode(pool);

	switch (m) {
	case PM_WRITE:
		/* Shouldn't get here */
		DMERR_LIMIT("bio unserviceable, yet pool is in PM_WRITE mode");
		return BLK_STS_IOERR;

	case PM_OUT_OF_DATA_SPACE:
		return pool->pf.error_if_no_space ? BLK_STS_NOSPC : 0;

	case PM_OUT_OF_METADATA_SPACE:
	case PM_READ_ONLY:
	case PM_FAIL:
		return BLK_STS_IOERR;
	default:
		/* Shouldn't get here */
		DMERR_LIMIT("bio unserviceable, yet pool has an unknown mode");
		return BLK_STS_IOERR;
	}
}

static void handle_unserviceable_bio(struct pool *pool, struct bio *bio)
{
	blk_status_t error = should_error_unserviceable_bio(pool);

	if (error) {
		bio->bi_status = error;
		bio_endio(bio);
	} else
		retry_on_resume(bio);
}

static void retry_bios_on_resume(struct pool *pool, struct dm_bio_prison_cell *cell)
{
	struct bio *bio;
	struct bio_list bios;
	blk_status_t error;

	error = should_error_unserviceable_bio(pool);
	if (error) {
		cell_error_with_code(pool, cell, error);
		return;
	}

	bio_list_init(&bios);
	cell_release(pool, cell, &bios);

	while ((bio = bio_list_pop(&bios)))
		retry_on_resume(bio);
}

static void process_discard_cell_no_passdown(struct thin_c *tc,
					     struct dm_bio_prison_cell *virt_cell)
{
	struct pool *pool = tc->pool;
	struct dm_thin_new_mapping *m = get_next_mapping(pool);

	/*
	 * We don't need to lock the data blocks, since there's no
	 * passdown.  We only lock data blocks for allocation and breaking sharing.
	 */
	m->tc = tc;
	m->virt_begin = virt_cell->key.block_begin;
	m->virt_end = virt_cell->key.block_end;
	m->cell = virt_cell;
	m->bio = virt_cell->holder;

	if (!dm_deferred_set_add_work(pool->all_io_ds, &m->list))
		pool->process_prepared_discard(m);
}

static void break_up_discard_bio(struct thin_c *tc, dm_block_t begin, dm_block_t end,
				 struct bio *bio)
{
	struct pool *pool = tc->pool;

	int r;
	bool maybe_shared;
	struct dm_cell_key data_key;
	struct dm_bio_prison_cell *data_cell;
	struct dm_thin_new_mapping *m;
	dm_block_t virt_begin, virt_end, data_begin, data_end;
	dm_block_t len, next_boundary;

	while (begin != end) {
		r = dm_thin_find_mapped_range(tc->td, begin, end, &virt_begin, &virt_end,
					      &data_begin, &maybe_shared);
		if (r) {
			/*
			 * Silently fail, letting any mappings we've
			 * created complete.
			 */
			break;
		}

		data_end = data_begin + (virt_end - virt_begin);

		/*
		 * Make sure the data region obeys the bio prison restrictions.
		 */
		while (data_begin < data_end) {
			r = ensure_next_mapping(pool);
			if (r)
				return; /* we did our best */

			next_boundary = ((data_begin >> BIO_PRISON_MAX_RANGE_SHIFT) + 1)
				<< BIO_PRISON_MAX_RANGE_SHIFT;
			len = min_t(sector_t, data_end - data_begin, next_boundary - data_begin);

			/* This key is certainly within range given the above splitting */
			(void) build_key(tc->td, PHYSICAL, data_begin, data_begin + len, &data_key);
			if (bio_detain(tc->pool, &data_key, NULL, &data_cell)) {
				/* contention, we'll give up with this range */
				data_begin += len;
				continue;
			}

			/*
			 * IO may still be going to the destination block.  We must
			 * quiesce before we can do the removal.
			 */
			m = get_next_mapping(pool);
			m->tc = tc;
			m->maybe_shared = maybe_shared;
			m->virt_begin = virt_begin;
			m->virt_end = virt_begin + len;
			m->data_block = data_begin;
			m->cell = data_cell;
			m->bio = bio;

			/*
			 * The parent bio must not complete before sub discard bios are
			 * chained to it (see end_discard's bio_chain)!
			 *
			 * This per-mapping bi_remaining increment is paired with
			 * the implicit decrement that occurs via bio_endio() in
			 * end_discard().
			 */
			bio_inc_remaining(bio);
			if (!dm_deferred_set_add_work(pool->all_io_ds, &m->list))
				pool->process_prepared_discard(m);

			virt_begin += len;
			data_begin += len;
		}

		begin = virt_end;
	}
}

static void process_discard_cell_passdown(struct thin_c *tc, struct dm_bio_prison_cell *virt_cell)
{
	struct bio *bio = virt_cell->holder;
	struct dm_thin_endio_hook *h = dm_per_bio_data(bio, sizeof(struct dm_thin_endio_hook));

	/*
	 * The virt_cell will only get freed once the origin bio completes.
	 * This means it will remain locked while all the individual
	 * passdown bios are in flight.
	 */
	h->cell = virt_cell;
	break_up_discard_bio(tc, virt_cell->key.block_begin, virt_cell->key.block_end, bio);

	/*
	 * We complete the bio now, knowing that the bi_remaining field
	 * will prevent completion until the sub range discards have
	 * completed.
	 */
	bio_endio(bio);
}

static void process_discard_bio(struct thin_c *tc, struct bio *bio)
{
	dm_block_t begin, end;
	struct dm_cell_key virt_key;
	struct dm_bio_prison_cell *virt_cell;

	get_bio_block_range(tc, bio, &begin, &end);
	if (begin == end) {
		/*
		 * The discard covers less than a block.
		 */
		bio_endio(bio);
		return;
	}

	if (unlikely(!build_key(tc->td, VIRTUAL, begin, end, &virt_key))) {
		DMERR_LIMIT("Discard doesn't respect bio prison limits");
		bio_endio(bio);
		return;
	}

	if (bio_detain(tc->pool, &virt_key, bio, &virt_cell)) {
		/*
		 * Potential starvation issue: We're relying on the
		 * fs/application being well behaved, and not trying to
		 * send IO to a region at the same time as discarding it.
		 * If they do this persistently then it's possible this
		 * cell will never be granted.
		 */
		return;
	}

	tc->pool->process_discard_cell(tc, virt_cell);
}

static void break_sharing(struct thin_c *tc, struct bio *bio, dm_block_t block,
			  struct dm_cell_key *key,
			  struct dm_thin_lookup_result *lookup_result,
			  struct dm_bio_prison_cell *cell)
{
	int r;
	dm_block_t data_block;
	struct pool *pool = tc->pool;

	r = alloc_data_block(tc, &data_block);
	switch (r) {
	case 0:
		schedule_internal_copy(tc, block, lookup_result->block,
				       data_block, cell, bio);
		break;

	case -ENOSPC:
		retry_bios_on_resume(pool, cell);
		break;

	default:
		DMERR_LIMIT("%s: alloc_data_block() failed: error = %d",
			    __func__, r);
		cell_error(pool, cell);
		break;
	}
}

static void __remap_and_issue_shared_cell(void *context,
					  struct dm_bio_prison_cell *cell)
{
	struct remap_info *info = context;
	struct bio *bio;

	while ((bio = bio_list_pop(&cell->bios))) {
		if (bio_data_dir(bio) == WRITE || op_is_flush(bio->bi_opf) ||
		    bio_op(bio) == REQ_OP_DISCARD)
			bio_list_add(&info->defer_bios, bio);
		else {
			struct dm_thin_endio_hook *h = dm_per_bio_data(bio, sizeof(struct dm_thin_endio_hook));

			h->shared_read_entry = dm_deferred_entry_inc(info->tc->pool->shared_read_ds);
			inc_all_io_entry(info->tc->pool, bio);
			bio_list_add(&info->issue_bios, bio);
		}
	}
}

static void remap_and_issue_shared_cell(struct thin_c *tc,
					struct dm_bio_prison_cell *cell,
					dm_block_t block)
{
	struct bio *bio;
	struct remap_info info;

	info.tc = tc;
	bio_list_init(&info.defer_bios);
	bio_list_init(&info.issue_bios);

	cell_visit_release(tc->pool, __remap_and_issue_shared_cell,
			   &info, cell);

	while ((bio = bio_list_pop(&info.defer_bios)))
		thin_defer_bio(tc, bio);

	while ((bio = bio_list_pop(&info.issue_bios)))
		remap_and_issue(tc, bio, block);
}

static void process_shared_bio(struct thin_c *tc, struct bio *bio,
			       dm_block_t block,
			       struct dm_thin_lookup_result *lookup_result,
			       struct dm_bio_prison_cell *virt_cell)
{
	struct dm_bio_prison_cell *data_cell;
	struct pool *pool = tc->pool;
	struct dm_cell_key key;

	/*
	 * If cell is already occupied, then sharing is already in the process
	 * of being broken so we have nothing further to do here.
	 */
	build_data_key(tc->td, lookup_result->block, &key);
	if (bio_detain(pool, &key, bio, &data_cell)) {
		cell_defer_no_holder(tc, virt_cell);
		return;
	}

	if (bio_data_dir(bio) == WRITE && bio->bi_iter.bi_size) {
		break_sharing(tc, bio, block, &key, lookup_result, data_cell);
		cell_defer_no_holder(tc, virt_cell);
	} else {
		struct dm_thin_endio_hook *h = dm_per_bio_data(bio, sizeof(struct dm_thin_endio_hook));

		h->shared_read_entry = dm_deferred_entry_inc(pool->shared_read_ds);
		inc_all_io_entry(pool, bio);
		remap_and_issue(tc, bio, lookup_result->block);

		remap_and_issue_shared_cell(tc, data_cell, lookup_result->block);
		remap_and_issue_shared_cell(tc, virt_cell, lookup_result->block);
	}
}

static void provision_block(struct thin_c *tc, struct bio *bio, dm_block_t block,
			    struct dm_bio_prison_cell *cell)
{
	int r;
	dm_block_t data_block;
	struct pool *pool = tc->pool;

	/*
	 * Remap empty bios (flushes) immediately, without provisioning.
	 */
	if (!bio->bi_iter.bi_size) {
		inc_all_io_entry(pool, bio);
		cell_defer_no_holder(tc, cell);

		remap_and_issue(tc, bio, 0);
		return;
	}

	/*
	 * Fill read bios with zeroes and complete them immediately.
	 */
	if (bio_data_dir(bio) == READ) {
		zero_fill_bio(bio);
		cell_defer_no_holder(tc, cell);
		bio_endio(bio);
		return;
	}

	r = alloc_data_block(tc, &data_block);
	switch (r) {
	case 0:
		if (tc->origin_dev)
			schedule_external_copy(tc, block, data_block, cell, bio);
		else
			schedule_zero(tc, block, data_block, cell, bio);
		break;

	case -ENOSPC:
		retry_bios_on_resume(pool, cell);
		break;

	default:
		DMERR_LIMIT("%s: alloc_data_block() failed: error = %d",
			    __func__, r);
		cell_error(pool, cell);
		break;
	}
}

static void process_cell(struct thin_c *tc, struct dm_bio_prison_cell *cell)
{
	int r;
	struct pool *pool = tc->pool;
	struct bio *bio = cell->holder;
	dm_block_t block = get_bio_block(tc, bio);
	struct dm_thin_lookup_result lookup_result;

	if (tc->requeue_mode) {
		cell_requeue(pool, cell);
		return;
	}

	r = dm_thin_find_block(tc->td, block, 1, &lookup_result);
	switch (r) {
	case 0:
		if (lookup_result.shared)
			process_shared_bio(tc, bio, block, &lookup_result, cell);
		else {
			inc_all_io_entry(pool, bio);
			remap_and_issue(tc, bio, lookup_result.block);
			inc_remap_and_issue_cell(tc, cell, lookup_result.block);
		}
		break;

	case -ENODATA:
		if (bio_data_dir(bio) == READ && tc->origin_dev) {
			inc_all_io_entry(pool, bio);
			cell_defer_no_holder(tc, cell);

			if (bio_end_sector(bio) <= tc->origin_size)
				remap_to_origin_and_issue(tc, bio);

			else if (bio->bi_iter.bi_sector < tc->origin_size) {
				zero_fill_bio(bio);
				bio->bi_iter.bi_size = (tc->origin_size - bio->bi_iter.bi_sector) << SECTOR_SHIFT;
				remap_to_origin_and_issue(tc, bio);

			} else {
				zero_fill_bio(bio);
				bio_endio(bio);
			}
		} else
			provision_block(tc, bio, block, cell);
		break;

	default:
		DMERR_LIMIT("%s: dm_thin_find_block() failed: error = %d",
			    __func__, r);
		cell_defer_no_holder(tc, cell);
		bio_io_error(bio);
		break;
	}
}

static void process_bio(struct thin_c *tc, struct bio *bio)
{
	struct pool *pool = tc->pool;
	dm_block_t block = get_bio_block(tc, bio);
	struct dm_bio_prison_cell *cell;
	struct dm_cell_key key;

	/*
	 * If cell is already occupied, then the block is already
	 * being provisioned so we have nothing further to do here.
	 */
	build_virtual_key(tc->td, block, &key);
	if (bio_detain(pool, &key, bio, &cell))
		return;

	process_cell(tc, cell);
}

static void __process_bio_read_only(struct thin_c *tc, struct bio *bio,
				    struct dm_bio_prison_cell *cell)
{
	int r;
	int rw = bio_data_dir(bio);
	dm_block_t block = get_bio_block(tc, bio);
	struct dm_thin_lookup_result lookup_result;

	r = dm_thin_find_block(tc->td, block, 1, &lookup_result);
	switch (r) {
	case 0:
		if (lookup_result.shared && (rw == WRITE) && bio->bi_iter.bi_size) {
			handle_unserviceable_bio(tc->pool, bio);
			if (cell)
				cell_defer_no_holder(tc, cell);
		} else {
			inc_all_io_entry(tc->pool, bio);
			remap_and_issue(tc, bio, lookup_result.block);
			if (cell)
				inc_remap_and_issue_cell(tc, cell, lookup_result.block);
		}
		break;

	case -ENODATA:
		if (cell)
			cell_defer_no_holder(tc, cell);
		if (rw != READ) {
			handle_unserviceable_bio(tc->pool, bio);
			break;
		}

		if (tc->origin_dev) {
			inc_all_io_entry(tc->pool, bio);
			remap_to_origin_and_issue(tc, bio);
			break;
		}

		zero_fill_bio(bio);
		bio_endio(bio);
		break;

	default:
		DMERR_LIMIT("%s: dm_thin_find_block() failed: error = %d",
			    __func__, r);
		if (cell)
			cell_defer_no_holder(tc, cell);
		bio_io_error(bio);
		break;
	}
}

static void process_bio_read_only(struct thin_c *tc, struct bio *bio)
{
	__process_bio_read_only(tc, bio, NULL);
}

static void process_cell_read_only(struct thin_c *tc, struct dm_bio_prison_cell *cell)
{
	__process_bio_read_only(tc, cell->holder, cell);
}

static void process_bio_success(struct thin_c *tc, struct bio *bio)
{
	bio_endio(bio);
}

static void process_bio_fail(struct thin_c *tc, struct bio *bio)
{
	bio_io_error(bio);
}

static void process_cell_success(struct thin_c *tc, struct dm_bio_prison_cell *cell)
{
	cell_success(tc->pool, cell);
}

static void process_cell_fail(struct thin_c *tc, struct dm_bio_prison_cell *cell)
{
	cell_error(tc->pool, cell);
}

/*
 * FIXME: should we also commit due to size of transaction, measured in
 * metadata blocks?
 */
static int need_commit_due_to_time(struct pool *pool)
{
	return !time_in_range(jiffies, pool->last_commit_jiffies,
			      pool->last_commit_jiffies + COMMIT_PERIOD);
}

#define thin_pbd(node) rb_entry((node), struct dm_thin_endio_hook, rb_node)
#define thin_bio(pbd) dm_bio_from_per_bio_data((pbd), sizeof(struct dm_thin_endio_hook))

static void __thin_bio_rb_add(struct thin_c *tc, struct bio *bio)
{
	struct rb_node **rbp, *parent;
	struct dm_thin_endio_hook *pbd;
	sector_t bi_sector = bio->bi_iter.bi_sector;

	rbp = &tc->sort_bio_list.rb_node;
	parent = NULL;
	while (*rbp) {
		parent = *rbp;
		pbd = thin_pbd(parent);

		if (bi_sector < thin_bio(pbd)->bi_iter.bi_sector)
			rbp = &(*rbp)->rb_left;
		else
			rbp = &(*rbp)->rb_right;
	}

	pbd = dm_per_bio_data(bio, sizeof(struct dm_thin_endio_hook));
	rb_link_node(&pbd->rb_node, parent, rbp);
	rb_insert_color(&pbd->rb_node, &tc->sort_bio_list);
}

static void __extract_sorted_bios(struct thin_c *tc)
{
	struct rb_node *node;
	struct dm_thin_endio_hook *pbd;
	struct bio *bio;

	for (node = rb_first(&tc->sort_bio_list); node; node = rb_next(node)) {
		pbd = thin_pbd(node);
		bio = thin_bio(pbd);

		bio_list_add(&tc->deferred_bio_list, bio);
		rb_erase(&pbd->rb_node, &tc->sort_bio_list);
	}

	WARN_ON(!RB_EMPTY_ROOT(&tc->sort_bio_list));
}

static void __sort_thin_deferred_bios(struct thin_c *tc)
{
	struct bio *bio;
	struct bio_list bios;

	bio_list_init(&bios);
	bio_list_merge(&bios, &tc->deferred_bio_list);
	bio_list_init(&tc->deferred_bio_list);

	/* Sort deferred_bio_list using rb-tree */
	while ((bio = bio_list_pop(&bios)))
		__thin_bio_rb_add(tc, bio);

	/*
	 * Transfer the sorted bios in sort_bio_list back to
	 * deferred_bio_list to allow lockless submission of
	 * all bios.
	 */
	__extract_sorted_bios(tc);
}

static void process_thin_deferred_bios(struct thin_c *tc)
{
	struct pool *pool = tc->pool;
	struct bio *bio;
	struct bio_list bios;
	struct blk_plug plug;
	unsigned int count = 0;

	if (tc->requeue_mode) {
		error_thin_bio_list(tc, &tc->deferred_bio_list,
				BLK_STS_DM_REQUEUE);
		return;
	}

	bio_list_init(&bios);

	spin_lock_irq(&tc->lock);

	if (bio_list_empty(&tc->deferred_bio_list)) {
		spin_unlock_irq(&tc->lock);
		return;
	}

	__sort_thin_deferred_bios(tc);

	bio_list_merge(&bios, &tc->deferred_bio_list);
	bio_list_init(&tc->deferred_bio_list);

	spin_unlock_irq(&tc->lock);

	blk_start_plug(&plug);
	while ((bio = bio_list_pop(&bios))) {
		/*
		 * If we've got no free new_mapping structs, and processing
		 * this bio might require one, we pause until there are some
		 * prepared mappings to process.
		 */
		if (ensure_next_mapping(pool)) {
			spin_lock_irq(&tc->lock);
			bio_list_add(&tc->deferred_bio_list, bio);
			bio_list_merge(&tc->deferred_bio_list, &bios);
			spin_unlock_irq(&tc->lock);
			break;
		}

		if (bio_op(bio) == REQ_OP_DISCARD)
			pool->process_discard(tc, bio);
		else
			pool->process_bio(tc, bio);

		if ((count++ & 127) == 0) {
			throttle_work_update(&pool->throttle);
			dm_pool_issue_prefetches(pool->pmd);
		}
		cond_resched();
	}
	blk_finish_plug(&plug);
}

static int cmp_cells(const void *lhs, const void *rhs)
{
	struct dm_bio_prison_cell *lhs_cell = *((struct dm_bio_prison_cell **) lhs);
	struct dm_bio_prison_cell *rhs_cell = *((struct dm_bio_prison_cell **) rhs);

	BUG_ON(!lhs_cell->holder);
	BUG_ON(!rhs_cell->holder);

	if (lhs_cell->holder->bi_iter.bi_sector < rhs_cell->holder->bi_iter.bi_sector)
		return -1;

	if (lhs_cell->holder->bi_iter.bi_sector > rhs_cell->holder->bi_iter.bi_sector)
		return 1;

	return 0;
}

static unsigned int sort_cells(struct pool *pool, struct list_head *cells)
{
	unsigned int count = 0;
	struct dm_bio_prison_cell *cell, *tmp;

	list_for_each_entry_safe(cell, tmp, cells, user_list) {
		if (count >= CELL_SORT_ARRAY_SIZE)
			break;

		pool->cell_sort_array[count++] = cell;
		list_del(&cell->user_list);
	}

	sort(pool->cell_sort_array, count, sizeof(cell), cmp_cells, NULL);

	return count;
}

static void process_thin_deferred_cells(struct thin_c *tc)
{
	struct pool *pool = tc->pool;
	struct list_head cells;
	struct dm_bio_prison_cell *cell;
	unsigned int i, j, count;

	INIT_LIST_HEAD(&cells);

	spin_lock_irq(&tc->lock);
	list_splice_init(&tc->deferred_cells, &cells);
	spin_unlock_irq(&tc->lock);

	if (list_empty(&cells))
		return;

	do {
		count = sort_cells(tc->pool, &cells);

		for (i = 0; i < count; i++) {
			cell = pool->cell_sort_array[i];
			BUG_ON(!cell->holder);

			/*
			 * If we've got no free new_mapping structs, and processing
			 * this bio might require one, we pause until there are some
			 * prepared mappings to process.
			 */
			if (ensure_next_mapping(pool)) {
				for (j = i; j < count; j++)
					list_add(&pool->cell_sort_array[j]->user_list, &cells);

				spin_lock_irq(&tc->lock);
				list_splice(&cells, &tc->deferred_cells);
				spin_unlock_irq(&tc->lock);
				return;
			}

			if (bio_op(cell->holder) == REQ_OP_DISCARD)
				pool->process_discard_cell(tc, cell);
			else
				pool->process_cell(tc, cell);
		}
		cond_resched();
	} while (!list_empty(&cells));
}

static void thin_get(struct thin_c *tc);
static void thin_put(struct thin_c *tc);

/*
 * We can't hold rcu_read_lock() around code that can block.  So we
 * find a thin with the rcu lock held; bump a refcount; then drop
 * the lock.
 */
static struct thin_c *get_first_thin(struct pool *pool)
{
	struct thin_c *tc = NULL;

	rcu_read_lock();
	if (!list_empty(&pool->active_thins)) {
		tc = list_entry_rcu(pool->active_thins.next, struct thin_c, list);
		thin_get(tc);
	}
	rcu_read_unlock();

	return tc;
}

static struct thin_c *get_next_thin(struct pool *pool, struct thin_c *tc)
{
	struct thin_c *old_tc = tc;

	rcu_read_lock();
	list_for_each_entry_continue_rcu(tc, &pool->active_thins, list) {
		thin_get(tc);
		thin_put(old_tc);
		rcu_read_unlock();
		return tc;
	}
	thin_put(old_tc);
	rcu_read_unlock();

	return NULL;
}

static void process_deferred_bios(struct pool *pool)
{
	struct bio *bio;
	struct bio_list bios, bio_completions;
	struct thin_c *tc;

	tc = get_first_thin(pool);
	while (tc) {
		process_thin_deferred_cells(tc);
		process_thin_deferred_bios(tc);
		tc = get_next_thin(pool, tc);
	}

	/*
	 * If there are any deferred flush bios, we must commit the metadata
	 * before issuing them or signaling their completion.
	 */
	bio_list_init(&bios);
	bio_list_init(&bio_completions);

	spin_lock_irq(&pool->lock);
	bio_list_merge(&bios, &pool->deferred_flush_bios);
	bio_list_init(&pool->deferred_flush_bios);

	bio_list_merge(&bio_completions, &pool->deferred_flush_completions);
	bio_list_init(&pool->deferred_flush_completions);
	spin_unlock_irq(&pool->lock);

	if (bio_list_empty(&bios) && bio_list_empty(&bio_completions) &&
	    !(dm_pool_changed_this_transaction(pool->pmd) && need_commit_due_to_time(pool)))
		return;

	if (commit(pool)) {
		bio_list_merge(&bios, &bio_completions);

		while ((bio = bio_list_pop(&bios)))
			bio_io_error(bio);
		return;
	}
	pool->last_commit_jiffies = jiffies;

	while ((bio = bio_list_pop(&bio_completions)))
		bio_endio(bio);

	while ((bio = bio_list_pop(&bios))) {
		/*
		 * The data device was flushed as part of metadata commit,
		 * so complete redundant flushes immediately.
		 */
		if (bio->bi_opf & REQ_PREFLUSH)
			bio_endio(bio);
		else
			dm_submit_bio_remap(bio, NULL);
	}
}

static void do_worker(struct work_struct *ws)
{
	struct pool *pool = container_of(ws, struct pool, worker);

	throttle_work_start(&pool->throttle);
	dm_pool_issue_prefetches(pool->pmd);
	throttle_work_update(&pool->throttle);
	process_prepared(pool, &pool->prepared_mappings, &pool->process_prepared_mapping);
	throttle_work_update(&pool->throttle);
	process_prepared(pool, &pool->prepared_discards, &pool->process_prepared_discard);
	throttle_work_update(&pool->throttle);
	process_prepared(pool, &pool->prepared_discards_pt2, &pool->process_prepared_discard_pt2);
	throttle_work_update(&pool->throttle);
	process_deferred_bios(pool);
	throttle_work_complete(&pool->throttle);
}

/*
 * We want to commit periodically so that not too much
 * unwritten data builds up.
 */
static void do_waker(struct work_struct *ws)
{
	struct pool *pool = container_of(to_delayed_work(ws), struct pool, waker);

	wake_worker(pool);
	queue_delayed_work(pool->wq, &pool->waker, COMMIT_PERIOD);
}

/*
 * We're holding onto IO to allow userland time to react.  After the
 * timeout either the pool will have been resized (and thus back in
 * PM_WRITE mode), or we degrade to PM_OUT_OF_DATA_SPACE w/ error_if_no_space.
 */
static void do_no_space_timeout(struct work_struct *ws)
{
	struct pool *pool = container_of(to_delayed_work(ws), struct pool,
					 no_space_timeout);

	if (get_pool_mode(pool) == PM_OUT_OF_DATA_SPACE && !pool->pf.error_if_no_space) {
		pool->pf.error_if_no_space = true;
		notify_of_pool_mode_change(pool);
		error_retry_list_with_code(pool, BLK_STS_NOSPC);
	}
}

/*----------------------------------------------------------------*/

struct pool_work {
	struct work_struct worker;
	struct completion complete;
};

static struct pool_work *to_pool_work(struct work_struct *ws)
{
	return container_of(ws, struct pool_work, worker);
}

static void pool_work_complete(struct pool_work *pw)
{
	complete(&pw->complete);
}

static void pool_work_wait(struct pool_work *pw, struct pool *pool,
			   void (*fn)(struct work_struct *))
{
	INIT_WORK_ONSTACK(&pw->worker, fn);
	init_completion(&pw->complete);
	queue_work(pool->wq, &pw->worker);
	wait_for_completion(&pw->complete);
}

/*----------------------------------------------------------------*/

struct noflush_work {
	struct pool_work pw;
	struct thin_c *tc;
};

static struct noflush_work *to_noflush(struct work_struct *ws)
{
	return container_of(to_pool_work(ws), struct noflush_work, pw);
}

static void do_noflush_start(struct work_struct *ws)
{
	struct noflush_work *w = to_noflush(ws);

	w->tc->requeue_mode = true;
	requeue_io(w->tc);
	pool_work_complete(&w->pw);
}

static void do_noflush_stop(struct work_struct *ws)
{
	struct noflush_work *w = to_noflush(ws);

	w->tc->requeue_mode = false;
	pool_work_complete(&w->pw);
}

static void noflush_work(struct thin_c *tc, void (*fn)(struct work_struct *))
{
	struct noflush_work w;

	w.tc = tc;
	pool_work_wait(&w.pw, tc->pool, fn);
}

/*----------------------------------------------------------------*/

static void set_discard_callbacks(struct pool *pool)
{
	struct pool_c *pt = pool->ti->private;

	if (pt->adjusted_pf.discard_passdown) {
		pool->process_discard_cell = process_discard_cell_passdown;
		pool->process_prepared_discard = process_prepared_discard_passdown_pt1;
		pool->process_prepared_discard_pt2 = process_prepared_discard_passdown_pt2;
	} else {
		pool->process_discard_cell = process_discard_cell_no_passdown;
		pool->process_prepared_discard = process_prepared_discard_no_passdown;
	}
}

static void set_pool_mode(struct pool *pool, enum pool_mode new_mode)
{
	struct pool_c *pt = pool->ti->private;
	bool needs_check = dm_pool_metadata_needs_check(pool->pmd);
	enum pool_mode old_mode = get_pool_mode(pool);
	unsigned long no_space_timeout = READ_ONCE(no_space_timeout_secs) * HZ;

	/*
	 * Never allow the pool to transition to PM_WRITE mode if user
	 * intervention is required to verify metadata and data consistency.
	 */
	if (new_mode == PM_WRITE && needs_check) {
		DMERR("%s: unable to switch pool to write mode until repaired.",
		      dm_device_name(pool->pool_md));
		if (old_mode != new_mode)
			new_mode = old_mode;
		else
			new_mode = PM_READ_ONLY;
	}
	/*
	 * If we were in PM_FAIL mode, rollback of metadata failed.  We're
	 * not going to recover without a thin_repair.	So we never let the
	 * pool move out of the old mode.
	 */
	if (old_mode == PM_FAIL)
		new_mode = old_mode;

	switch (new_mode) {
	case PM_FAIL:
		dm_pool_metadata_read_only(pool->pmd);
		pool->process_bio = process_bio_fail;
		pool->process_discard = process_bio_fail;
		pool->process_cell = process_cell_fail;
		pool->process_discard_cell = process_cell_fail;
		pool->process_prepared_mapping = process_prepared_mapping_fail;
		pool->process_prepared_discard = process_prepared_discard_fail;

		error_retry_list(pool);
		break;

	case PM_OUT_OF_METADATA_SPACE:
	case PM_READ_ONLY:
		dm_pool_metadata_read_only(pool->pmd);
		pool->process_bio = process_bio_read_only;
		pool->process_discard = process_bio_success;
		pool->process_cell = process_cell_read_only;
		pool->process_discard_cell = process_cell_success;
		pool->process_prepared_mapping = process_prepared_mapping_fail;
		pool->process_prepared_discard = process_prepared_discard_success;

		error_retry_list(pool);
		break;

	case PM_OUT_OF_DATA_SPACE:
		/*
		 * Ideally we'd never hit this state; the low water mark
		 * would trigger userland to extend the pool before we
		 * completely run out of data space.  However, many small
		 * IOs to unprovisioned space can consume data space at an
		 * alarming rate.  Adjust your low water mark if you're
		 * frequently seeing this mode.
		 */
		pool->out_of_data_space = true;
		pool->process_bio = process_bio_read_only;
		pool->process_discard = process_discard_bio;
		pool->process_cell = process_cell_read_only;
		pool->process_prepared_mapping = process_prepared_mapping;
		set_discard_callbacks(pool);

		if (!pool->pf.error_if_no_space && no_space_timeout)
			queue_delayed_work(pool->wq, &pool->no_space_timeout, no_space_timeout);
		break;

	case PM_WRITE:
		if (old_mode == PM_OUT_OF_DATA_SPACE)
			cancel_delayed_work_sync(&pool->no_space_timeout);
		pool->out_of_data_space = false;
		pool->pf.error_if_no_space = pt->requested_pf.error_if_no_space;
		dm_pool_metadata_read_write(pool->pmd);
		pool->process_bio = process_bio;
		pool->process_discard = process_discard_bio;
		pool->process_cell = process_cell;
		pool->process_prepared_mapping = process_prepared_mapping;
		set_discard_callbacks(pool);
		break;
	}

	pool->pf.mode = new_mode;
	/*
	 * The pool mode may have changed, sync it so bind_control_target()
	 * doesn't cause an unexpected mode transition on resume.
	 */
	pt->adjusted_pf.mode = new_mode;

	if (old_mode != new_mode)
		notify_of_pool_mode_change(pool);
}

static void abort_transaction(struct pool *pool)
{
	const char *dev_name = dm_device_name(pool->pool_md);

	DMERR_LIMIT("%s: aborting current metadata transaction", dev_name);
	if (dm_pool_abort_metadata(pool->pmd)) {
		DMERR("%s: failed to abort metadata transaction", dev_name);
		set_pool_mode(pool, PM_FAIL);
	}

	if (dm_pool_metadata_set_needs_check(pool->pmd)) {
		DMERR("%s: failed to set 'needs_check' flag in metadata", dev_name);
		set_pool_mode(pool, PM_FAIL);
	}
}

static void metadata_operation_failed(struct pool *pool, const char *op, int r)
{
	DMERR_LIMIT("%s: metadata operation '%s' failed: error = %d",
		    dm_device_name(pool->pool_md), op, r);

	abort_transaction(pool);
	set_pool_mode(pool, PM_READ_ONLY);
}

/*----------------------------------------------------------------*/

/*
 * Mapping functions.
 */

/*
 * Called only while mapping a thin bio to hand it over to the workqueue.
 */
static void thin_defer_bio(struct thin_c *tc, struct bio *bio)
{
	struct pool *pool = tc->pool;

	spin_lock_irq(&tc->lock);
	bio_list_add(&tc->deferred_bio_list, bio);
	spin_unlock_irq(&tc->lock);

	wake_worker(pool);
}

static void thin_defer_bio_with_throttle(struct thin_c *tc, struct bio *bio)
{
	struct pool *pool = tc->pool;

	throttle_lock(&pool->throttle);
	thin_defer_bio(tc, bio);
	throttle_unlock(&pool->throttle);
}

static void thin_defer_cell(struct thin_c *tc, struct dm_bio_prison_cell *cell)
{
	struct pool *pool = tc->pool;

	throttle_lock(&pool->throttle);
	spin_lock_irq(&tc->lock);
	list_add_tail(&cell->user_list, &tc->deferred_cells);
	spin_unlock_irq(&tc->lock);
	throttle_unlock(&pool->throttle);

	wake_worker(pool);
}

static void thin_hook_bio(struct thin_c *tc, struct bio *bio)
{
	struct dm_thin_endio_hook *h = dm_per_bio_data(bio, sizeof(struct dm_thin_endio_hook));

	h->tc = tc;
	h->shared_read_entry = NULL;
	h->all_io_entry = NULL;
	h->overwrite_mapping = NULL;
	h->cell = NULL;
}

/*
 * Non-blocking function called from the thin target's map function.
 */
static int thin_bio_map(struct dm_target *ti, struct bio *bio)
{
	int r;
	struct thin_c *tc = ti->private;
	dm_block_t block = get_bio_block(tc, bio);
	struct dm_thin_device *td = tc->td;
	struct dm_thin_lookup_result result;
	struct dm_bio_prison_cell *virt_cell, *data_cell;
	struct dm_cell_key key;

	thin_hook_bio(tc, bio);

	if (tc->requeue_mode) {
		bio->bi_status = BLK_STS_DM_REQUEUE;
		bio_endio(bio);
		return DM_MAPIO_SUBMITTED;
	}

	if (get_pool_mode(tc->pool) == PM_FAIL) {
		bio_io_error(bio);
		return DM_MAPIO_SUBMITTED;
	}

	if (op_is_flush(bio->bi_opf) || bio_op(bio) == REQ_OP_DISCARD) {
		thin_defer_bio_with_throttle(tc, bio);
		return DM_MAPIO_SUBMITTED;
	}

	/*
	 * We must hold the virtual cell before doing the lookup, otherwise
	 * there's a race with discard.
	 */
	build_virtual_key(tc->td, block, &key);
	if (bio_detain(tc->pool, &key, bio, &virt_cell))
		return DM_MAPIO_SUBMITTED;

	r = dm_thin_find_block(td, block, 0, &result);

	/*
	 * Note that we defer readahead too.
	 */
	switch (r) {
	case 0:
		if (unlikely(result.shared)) {
			/*
			 * We have a race condition here between the
			 * result.shared value returned by the lookup and
			 * snapshot creation, which may cause new
			 * sharing.
			 *
			 * To avoid this always quiesce the origin before
			 * taking the snap.  You want to do this anyway to
			 * ensure a consistent application view
			 * (i.e. lockfs).
			 *
			 * More distant ancestors are irrelevant. The
			 * shared flag will be set in their case.
			 */
			thin_defer_cell(tc, virt_cell);
			return DM_MAPIO_SUBMITTED;
		}

		build_data_key(tc->td, result.block, &key);
		if (bio_detain(tc->pool, &key, bio, &data_cell)) {
			cell_defer_no_holder(tc, virt_cell);
			return DM_MAPIO_SUBMITTED;
		}

		inc_all_io_entry(tc->pool, bio);
		cell_defer_no_holder(tc, data_cell);
		cell_defer_no_holder(tc, virt_cell);

		remap(tc, bio, result.block);
		return DM_MAPIO_REMAPPED;

	case -ENODATA:
	case -EWOULDBLOCK:
		thin_defer_cell(tc, virt_cell);
		return DM_MAPIO_SUBMITTED;

	default:
		/*
		 * Must always call bio_io_error on failure.
		 * dm_thin_find_block can fail with -EINVAL if the
		 * pool is switched to fail-io mode.
		 */
		bio_io_error(bio);
		cell_defer_no_holder(tc, virt_cell);
		return DM_MAPIO_SUBMITTED;
	}
}

static void requeue_bios(struct pool *pool)
{
	struct thin_c *tc;

	rcu_read_lock();
	list_for_each_entry_rcu(tc, &pool->active_thins, list) {
		spin_lock_irq(&tc->lock);
		bio_list_merge(&tc->deferred_bio_list, &tc->retry_on_resume_list);
		bio_list_init(&tc->retry_on_resume_list);
		spin_unlock_irq(&tc->lock);
	}
	rcu_read_unlock();
}

/*
 *--------------------------------------------------------------
 * Binding of control targets to a pool object
 *--------------------------------------------------------------
 */
static bool is_factor(sector_t block_size, uint32_t n)
{
	return !sector_div(block_size, n);
}

/*
 * If discard_passdown was enabled verify that the data device
 * supports discards.  Disable discard_passdown if not.
 */
static void disable_discard_passdown_if_not_supported(struct pool_c *pt)
{
	struct pool *pool = pt->pool;
	struct block_device *data_bdev = pt->data_dev->bdev;
	struct queue_limits *data_limits = &bdev_get_queue(data_bdev)->limits;
	const char *reason = NULL;

	if (!pt->adjusted_pf.discard_passdown)
		return;

	if (!bdev_max_discard_sectors(pt->data_dev->bdev))
		reason = "discard unsupported";

	else if (data_limits->max_discard_sectors < pool->sectors_per_block)
		reason = "max discard sectors smaller than a block";

	if (reason) {
		DMWARN("Data device (%pg) %s: Disabling discard passdown.", data_bdev, reason);
		pt->adjusted_pf.discard_passdown = false;
	}
}

static int bind_control_target(struct pool *pool, struct dm_target *ti)
{
	struct pool_c *pt = ti->private;

	/*
	 * We want to make sure that a pool in PM_FAIL mode is never upgraded.
	 */
	enum pool_mode old_mode = get_pool_mode(pool);
	enum pool_mode new_mode = pt->adjusted_pf.mode;

	/*
	 * Don't change the pool's mode until set_pool_mode() below.
	 * Otherwise the pool's process_* function pointers may
	 * not match the desired pool mode.
	 */
	pt->adjusted_pf.mode = old_mode;

	pool->ti = ti;
	pool->pf = pt->adjusted_pf;
	pool->low_water_blocks = pt->low_water_blocks;

	set_pool_mode(pool, new_mode);

	return 0;
}

static void unbind_control_target(struct pool *pool, struct dm_target *ti)
{
	if (pool->ti == ti)
		pool->ti = NULL;
}

/*
 *--------------------------------------------------------------
 * Pool creation
 *--------------------------------------------------------------
 */
/* Initialize pool features. */
static void pool_features_init(struct pool_features *pf)
{
	pf->mode = PM_WRITE;
	pf->zero_new_blocks = true;
	pf->discard_enabled = true;
	pf->discard_passdown = true;
	pf->error_if_no_space = false;
}

static void __pool_destroy(struct pool *pool)
{
	__pool_table_remove(pool);

	vfree(pool->cell_sort_array);
	if (dm_pool_metadata_close(pool->pmd) < 0)
		DMWARN("%s: dm_pool_metadata_close() failed.", __func__);

	dm_bio_prison_destroy(pool->prison);
	dm_kcopyd_client_destroy(pool->copier);

	cancel_delayed_work_sync(&pool->waker);
	cancel_delayed_work_sync(&pool->no_space_timeout);
	if (pool->wq)
		destroy_workqueue(pool->wq);

	if (pool->next_mapping)
		mempool_free(pool->next_mapping, &pool->mapping_pool);
	mempool_exit(&pool->mapping_pool);
	dm_deferred_set_destroy(pool->shared_read_ds);
	dm_deferred_set_destroy(pool->all_io_ds);
	kfree(pool);
}

static struct kmem_cache *_new_mapping_cache;

static struct pool *pool_create(struct mapped_device *pool_md,
				struct block_device *metadata_dev,
				struct block_device *data_dev,
				unsigned long block_size,
				int read_only, char **error)
{
	int r;
	void *err_p;
	struct pool *pool;
	struct dm_pool_metadata *pmd;
	bool format_device = read_only ? false : true;

	pmd = dm_pool_metadata_open(metadata_dev, block_size, format_device);
	if (IS_ERR(pmd)) {
		*error = "Error creating metadata object";
		return (struct pool *)pmd;
	}

	pool = kzalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool) {
		*error = "Error allocating memory for pool";
		err_p = ERR_PTR(-ENOMEM);
		goto bad_pool;
	}

	pool->pmd = pmd;
	pool->sectors_per_block = block_size;
	if (block_size & (block_size - 1))
		pool->sectors_per_block_shift = -1;
	else
		pool->sectors_per_block_shift = __ffs(block_size);
	pool->low_water_blocks = 0;
	pool_features_init(&pool->pf);
	pool->prison = dm_bio_prison_create();
	if (!pool->prison) {
		*error = "Error creating pool's bio prison";
		err_p = ERR_PTR(-ENOMEM);
		goto bad_prison;
	}

	pool->copier = dm_kcopyd_client_create(&dm_kcopyd_throttle);
	if (IS_ERR(pool->copier)) {
		r = PTR_ERR(pool->copier);
		*error = "Error creating pool's kcopyd client";
		err_p = ERR_PTR(r);
		goto bad_kcopyd_client;
	}

	/*
	 * Create singlethreaded workqueue that will service all devices
	 * that use this metadata.
	 */
	pool->wq = alloc_ordered_workqueue("dm-" DM_MSG_PREFIX, WQ_MEM_RECLAIM);
	if (!pool->wq) {
		*error = "Error creating pool's workqueue";
		err_p = ERR_PTR(-ENOMEM);
		goto bad_wq;
	}

	throttle_init(&pool->throttle);
	INIT_WORK(&pool->worker, do_worker);
	INIT_DELAYED_WORK(&pool->waker, do_waker);
	INIT_DELAYED_WORK(&pool->no_space_timeout, do_no_space_timeout);
	spin_lock_init(&pool->lock);
	bio_list_init(&pool->deferred_flush_bios);
	bio_list_init(&pool->deferred_flush_completions);
	INIT_LIST_HEAD(&pool->prepared_mappings);
	INIT_LIST_HEAD(&pool->prepared_discards);
	INIT_LIST_HEAD(&pool->prepared_discards_pt2);
	INIT_LIST_HEAD(&pool->active_thins);
	pool->low_water_triggered = false;
	pool->suspended = true;
	pool->out_of_data_space = false;

	pool->shared_read_ds = dm_deferred_set_create();
	if (!pool->shared_read_ds) {
		*error = "Error creating pool's shared read deferred set";
		err_p = ERR_PTR(-ENOMEM);
		goto bad_shared_read_ds;
	}

	pool->all_io_ds = dm_deferred_set_create();
	if (!pool->all_io_ds) {
		*error = "Error creating pool's all io deferred set";
		err_p = ERR_PTR(-ENOMEM);
		goto bad_all_io_ds;
	}

	pool->next_mapping = NULL;
	r = mempool_init_slab_pool(&pool->mapping_pool, MAPPING_POOL_SIZE,
				   _new_mapping_cache);
	if (r) {
		*error = "Error creating pool's mapping mempool";
		err_p = ERR_PTR(r);
		goto bad_mapping_pool;
	}

	pool->cell_sort_array =
		vmalloc(array_size(CELL_SORT_ARRAY_SIZE,
				   sizeof(*pool->cell_sort_array)));
	if (!pool->cell_sort_array) {
		*error = "Error allocating cell sort array";
		err_p = ERR_PTR(-ENOMEM);
		goto bad_sort_array;
	}

	pool->ref_count = 1;
	pool->last_commit_jiffies = jiffies;
	pool->pool_md = pool_md;
	pool->md_dev = metadata_dev;
	pool->data_dev = data_dev;
	__pool_table_insert(pool);

	return pool;

bad_sort_array:
	mempool_exit(&pool->mapping_pool);
bad_mapping_pool:
	dm_deferred_set_destroy(pool->all_io_ds);
bad_all_io_ds:
	dm_deferred_set_destroy(pool->shared_read_ds);
bad_shared_read_ds:
	destroy_workqueue(pool->wq);
bad_wq:
	dm_kcopyd_client_destroy(pool->copier);
bad_kcopyd_client:
	dm_bio_prison_destroy(pool->prison);
bad_prison:
	kfree(pool);
bad_pool:
	if (dm_pool_metadata_close(pmd))
		DMWARN("%s: dm_pool_metadata_close() failed.", __func__);

	return err_p;
}

static void __pool_inc(struct pool *pool)
{
	BUG_ON(!mutex_is_locked(&dm_thin_pool_table.mutex));
	pool->ref_count++;
}

static void __pool_dec(struct pool *pool)
{
	BUG_ON(!mutex_is_locked(&dm_thin_pool_table.mutex));
	BUG_ON(!pool->ref_count);
	if (!--pool->ref_count)
		__pool_destroy(pool);
}

static struct pool *__pool_find(struct mapped_device *pool_md,
				struct block_device *metadata_dev,
				struct block_device *data_dev,
				unsigned long block_size, int read_only,
				char **error, int *created)
{
	struct pool *pool = __pool_table_lookup_metadata_dev(metadata_dev);

	if (pool) {
		if (pool->pool_md != pool_md) {
			*error = "metadata device already in use by a pool";
			return ERR_PTR(-EBUSY);
		}
		if (pool->data_dev != data_dev) {
			*error = "data device already in use by a pool";
			return ERR_PTR(-EBUSY);
		}
		__pool_inc(pool);

	} else {
		pool = __pool_table_lookup(pool_md);
		if (pool) {
			if (pool->md_dev != metadata_dev || pool->data_dev != data_dev) {
				*error = "different pool cannot replace a pool";
				return ERR_PTR(-EINVAL);
			}
			__pool_inc(pool);

		} else {
			pool = pool_create(pool_md, metadata_dev, data_dev, block_size, read_only, error);
			*created = 1;
		}
	}

	return pool;
}

/*
 *--------------------------------------------------------------
 * Pool target methods
 *--------------------------------------------------------------
 */
static void pool_dtr(struct dm_target *ti)
{
	struct pool_c *pt = ti->private;

	mutex_lock(&dm_thin_pool_table.mutex);

	unbind_control_target(pt->pool, ti);
	__pool_dec(pt->pool);
	dm_put_device(ti, pt->metadata_dev);
	dm_put_device(ti, pt->data_dev);
	kfree(pt);

	mutex_unlock(&dm_thin_pool_table.mutex);
}

static int parse_pool_features(struct dm_arg_set *as, struct pool_features *pf,
			       struct dm_target *ti)
{
	int r;
	unsigned int argc;
	const char *arg_name;

	static const struct dm_arg _args[] = {
		{0, 4, "Invalid number of pool feature arguments"},
	};

	/*
	 * No feature arguments supplied.
	 */
	if (!as->argc)
		return 0;

	r = dm_read_arg_group(_args, as, &argc, &ti->error);
	if (r)
		return -EINVAL;

	while (argc && !r) {
		arg_name = dm_shift_arg(as);
		argc--;

		if (!strcasecmp(arg_name, "skip_block_zeroing"))
			pf->zero_new_blocks = false;

		else if (!strcasecmp(arg_name, "ignore_discard"))
			pf->discard_enabled = false;

		else if (!strcasecmp(arg_name, "no_discard_passdown"))
			pf->discard_passdown = false;

		else if (!strcasecmp(arg_name, "read_only"))
			pf->mode = PM_READ_ONLY;

		else if (!strcasecmp(arg_name, "error_if_no_space"))
			pf->error_if_no_space = true;

		else {
			ti->error = "Unrecognised pool feature requested";
			r = -EINVAL;
			break;
		}
	}

	return r;
}

static void metadata_low_callback(void *context)
{
	struct pool *pool = context;

	DMWARN("%s: reached low water mark for metadata device: sending event.",
	       dm_device_name(pool->pool_md));

	dm_table_event(pool->ti->table);
}

/*
 * We need to flush the data device **before** committing the metadata.
 *
 * This ensures that the data blocks of any newly inserted mappings are
 * properly written to non-volatile storage and won't be lost in case of a
 * crash.
 *
 * Failure to do so can result in data corruption in the case of internal or
 * external snapshots and in the case of newly provisioned blocks, when block
 * zeroing is enabled.
 */
static int metadata_pre_commit_callback(void *context)
{
	struct pool *pool = context;

	return blkdev_issue_flush(pool->data_dev);
}

static sector_t get_dev_size(struct block_device *bdev)
{
	return bdev_nr_sectors(bdev);
}

static void warn_if_metadata_device_too_big(struct block_device *bdev)
{
	sector_t metadata_dev_size = get_dev_size(bdev);

	if (metadata_dev_size > THIN_METADATA_MAX_SECTORS_WARNING)
		DMWARN("Metadata device %pg is larger than %u sectors: excess space will not be used.",
		       bdev, THIN_METADATA_MAX_SECTORS);
}

static sector_t get_metadata_dev_size(struct block_device *bdev)
{
	sector_t metadata_dev_size = get_dev_size(bdev);

	if (metadata_dev_size > THIN_METADATA_MAX_SECTORS)
		metadata_dev_size = THIN_METADATA_MAX_SECTORS;

	return metadata_dev_size;
}

static dm_block_t get_metadata_dev_size_in_blocks(struct block_device *bdev)
{
	sector_t metadata_dev_size = get_metadata_dev_size(bdev);

	sector_div(metadata_dev_size, THIN_METADATA_BLOCK_SIZE);

	return metadata_dev_size;
}

/*
 * When a metadata threshold is crossed a dm event is triggered, and
 * userland should respond by growing the metadata device.  We could let
 * userland set the threshold, like we do with the data threshold, but I'm
 * not sure they know enough to do this well.
 */
static dm_block_t calc_metadata_threshold(struct pool_c *pt)
{
	/*
	 * 4M is ample for all ops with the possible exception of thin
	 * device deletion which is harmless if it fails (just retry the
	 * delete after you've grown the device).
	 */
	dm_block_t quarter = get_metadata_dev_size_in_blocks(pt->metadata_dev->bdev) / 4;

	return min((dm_block_t)1024ULL /* 4M */, quarter);
}

/*
 * thin-pool <metadata dev> <data dev>
 *	     <data block size (sectors)>
 *	     <low water mark (blocks)>
 *	     [<#feature args> [<arg>]*]
 *
 * Optional feature arguments are:
 *	     skip_block_zeroing: skips the zeroing of newly-provisioned blocks.
 *	     ignore_discard: disable discard
 *	     no_discard_passdown: don't pass discards down to the data device
 *	     read_only: Don't allow any changes to be made to the pool metadata.
 *	     error_if_no_space: error IOs, instead of queueing, if no space.
 */
static int pool_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	int r, pool_created = 0;
	struct pool_c *pt;
	struct pool *pool;
	struct pool_features pf;
	struct dm_arg_set as;
	struct dm_dev *data_dev;
	unsigned long block_size;
	dm_block_t low_water_blocks;
	struct dm_dev *metadata_dev;
	blk_mode_t metadata_mode;

	/*
	 * FIXME Remove validation from scope of lock.
	 */
	mutex_lock(&dm_thin_pool_table.mutex);

	if (argc < 4) {
		ti->error = "Invalid argument count";
		r = -EINVAL;
		goto out_unlock;
	}

	as.argc = argc;
	as.argv = argv;

	/* make sure metadata and data are different devices */
	if (!strcmp(argv[0], argv[1])) {
		ti->error = "Error setting metadata or data device";
		r = -EINVAL;
		goto out_unlock;
	}

	/*
	 * Set default pool features.
	 */
	pool_features_init(&pf);

	dm_consume_args(&as, 4);
	r = parse_pool_features(&as, &pf, ti);
	if (r)
		goto out_unlock;

	metadata_mode = BLK_OPEN_READ |
		((pf.mode == PM_READ_ONLY) ? 0 : BLK_OPEN_WRITE);
	r = dm_get_device(ti, argv[0], metadata_mode, &metadata_dev);
	if (r) {
		ti->error = "Error opening metadata block device";
		goto out_unlock;
	}
	warn_if_metadata_device_too_big(metadata_dev->bdev);

	r = dm_get_device(ti, argv[1], BLK_OPEN_READ | BLK_OPEN_WRITE, &data_dev);
	if (r) {
		ti->error = "Error getting data device";
		goto out_metadata;
	}

	if (kstrtoul(argv[2], 10, &block_size) || !block_size ||
	    block_size < DATA_DEV_BLOCK_SIZE_MIN_SECTORS ||
	    block_size > DATA_DEV_BLOCK_SIZE_MAX_SECTORS ||
	    block_size & (DATA_DEV_BLOCK_SIZE_MIN_SECTORS - 1)) {
		ti->error = "Invalid block size";
		r = -EINVAL;
		goto out;
	}

	if (kstrtoull(argv[3], 10, (unsigned long long *)&low_water_blocks)) {
		ti->error = "Invalid low water mark";
		r = -EINVAL;
		goto out;
	}

	pt = kzalloc(sizeof(*pt), GFP_KERNEL);
	if (!pt) {
		r = -ENOMEM;
		goto out;
	}

	pool = __pool_find(dm_table_get_md(ti->table), metadata_dev->bdev, data_dev->bdev,
			   block_size, pf.mode == PM_READ_ONLY, &ti->error, &pool_created);
	if (IS_ERR(pool)) {
		r = PTR_ERR(pool);
		goto out_free_pt;
	}

	/*
	 * 'pool_created' reflects whether this is the first table load.
	 * Top level discard support is not allowed to be changed after
	 * initial load.  This would require a pool reload to trigger thin
	 * device changes.
	 */
	if (!pool_created && pf.discard_enabled != pool->pf.discard_enabled) {
		ti->error = "Discard support cannot be disabled once enabled";
		r = -EINVAL;
		goto out_flags_changed;
	}

	pt->pool = pool;
	pt->ti = ti;
	pt->metadata_dev = metadata_dev;
	pt->data_dev = data_dev;
	pt->low_water_blocks = low_water_blocks;
	pt->adjusted_pf = pt->requested_pf = pf;
	ti->num_flush_bios = 1;
	ti->limit_swap_bios = true;

	/*
	 * Only need to enable discards if the pool should pass
	 * them down to the data device.  The thin device's discard
	 * processing will cause mappings to be removed from the btree.
	 */
	if (pf.discard_enabled && pf.discard_passdown) {
		ti->num_discard_bios = 1;
		/*
		 * Setting 'discards_supported' circumvents the normal
		 * stacking of discard limits (this keeps the pool and
		 * thin devices' discard limits consistent).
		 */
		ti->discards_supported = true;
		ti->max_discard_granularity = true;
	}
	ti->private = pt;

	r = dm_pool_register_metadata_threshold(pt->pool->pmd,
						calc_metadata_threshold(pt),
						metadata_low_callback,
						pool);
	if (r) {
		ti->error = "Error registering metadata threshold";
		goto out_flags_changed;
	}

	dm_pool_register_pre_commit_callback(pool->pmd,
					     metadata_pre_commit_callback, pool);

	mutex_unlock(&dm_thin_pool_table.mutex);

	return 0;

out_flags_changed:
	__pool_dec(pool);
out_free_pt:
	kfree(pt);
out:
	dm_put_device(ti, data_dev);
out_metadata:
	dm_put_device(ti, metadata_dev);
out_unlock:
	mutex_unlock(&dm_thin_pool_table.mutex);

	return r;
}

static int pool_map(struct dm_target *ti, struct bio *bio)
{
	struct pool_c *pt = ti->private;
	struct pool *pool = pt->pool;

	/*
	 * As this is a singleton target, ti->begin is always zero.
	 */
	spin_lock_irq(&pool->lock);
	bio_set_dev(bio, pt->data_dev->bdev);
	spin_unlock_irq(&pool->lock);

	return DM_MAPIO_REMAPPED;
}

static int maybe_resize_data_dev(struct dm_target *ti, bool *need_commit)
{
	int r;
	struct pool_c *pt = ti->private;
	struct pool *pool = pt->pool;
	sector_t data_size = ti->len;
	dm_block_t sb_data_size;

	*need_commit = false;

	(void) sector_div(data_size, pool->sectors_per_block);

	r = dm_pool_get_data_dev_size(pool->pmd, &sb_data_size);
	if (r) {
		DMERR("%s: failed to retrieve data device size",
		      dm_device_name(pool->pool_md));
		return r;
	}

	if (data_size < sb_data_size) {
		DMERR("%s: pool target (%llu blocks) too small: expected %llu",
		      dm_device_name(pool->pool_md),
		      (unsigned long long)data_size, sb_data_size);
		return -EINVAL;

	} else if (data_size > sb_data_size) {
		if (dm_pool_metadata_needs_check(pool->pmd)) {
			DMERR("%s: unable to grow the data device until repaired.",
			      dm_device_name(pool->pool_md));
			return 0;
		}

		if (sb_data_size)
			DMINFO("%s: growing the data device from %llu to %llu blocks",
			       dm_device_name(pool->pool_md),
			       sb_data_size, (unsigned long long)data_size);
		r = dm_pool_resize_data_dev(pool->pmd, data_size);
		if (r) {
			metadata_operation_failed(pool, "dm_pool_resize_data_dev", r);
			return r;
		}

		*need_commit = true;
	}

	return 0;
}

static int maybe_resize_metadata_dev(struct dm_target *ti, bool *need_commit)
{
	int r;
	struct pool_c *pt = ti->private;
	struct pool *pool = pt->pool;
	dm_block_t metadata_dev_size, sb_metadata_dev_size;

	*need_commit = false;

	metadata_dev_size = get_metadata_dev_size_in_blocks(pool->md_dev);

	r = dm_pool_get_metadata_dev_size(pool->pmd, &sb_metadata_dev_size);
	if (r) {
		DMERR("%s: failed to retrieve metadata device size",
		      dm_device_name(pool->pool_md));
		return r;
	}

	if (metadata_dev_size < sb_metadata_dev_size) {
		DMERR("%s: metadata device (%llu blocks) too small: expected %llu",
		      dm_device_name(pool->pool_md),
		      metadata_dev_size, sb_metadata_dev_size);
		return -EINVAL;

	} else if (metadata_dev_size > sb_metadata_dev_size) {
		if (dm_pool_metadata_needs_check(pool->pmd)) {
			DMERR("%s: unable to grow the metadata device until repaired.",
			      dm_device_name(pool->pool_md));
			return 0;
		}

		warn_if_metadata_device_too_big(pool->md_dev);
		DMINFO("%s: growing the metadata device from %llu to %llu blocks",
		       dm_device_name(pool->pool_md),
		       sb_metadata_dev_size, metadata_dev_size);

		if (get_pool_mode(pool) == PM_OUT_OF_METADATA_SPACE)
			set_pool_mode(pool, PM_WRITE);

		r = dm_pool_resize_metadata_dev(pool->pmd, metadata_dev_size);
		if (r) {
			metadata_operation_failed(pool, "dm_pool_resize_metadata_dev", r);
			return r;
		}

		*need_commit = true;
	}

	return 0;
}

/*
 * Retrieves the number of blocks of the data device from
 * the superblock and compares it to the actual device size,
 * thus resizing the data device in case it has grown.
 *
 * This both copes with opening preallocated data devices in the ctr
 * being followed by a resume
 * -and-
 * calling the resume method individually after userspace has
 * grown the data device in reaction to a table event.
 */
static int pool_preresume(struct dm_target *ti)
{
	int r;
	bool need_commit1, need_commit2;
	struct pool_c *pt = ti->private;
	struct pool *pool = pt->pool;

	/*
	 * Take control of the pool object.
	 */
	r = bind_control_target(pool, ti);
	if (r)
		goto out;

	r = maybe_resize_data_dev(ti, &need_commit1);
	if (r)
		goto out;

	r = maybe_resize_metadata_dev(ti, &need_commit2);
	if (r)
		goto out;

	if (need_commit1 || need_commit2)
		(void) commit(pool);
out:
	/*
	 * When a thin-pool is PM_FAIL, it cannot be rebuilt if
	 * bio is in deferred list. Therefore need to return 0
	 * to allow pool_resume() to flush IO.
	 */
	if (r && get_pool_mode(pool) == PM_FAIL)
		r = 0;

	return r;
}

static void pool_suspend_active_thins(struct pool *pool)
{
	struct thin_c *tc;

	/* Suspend all active thin devices */
	tc = get_first_thin(pool);
	while (tc) {
		dm_internal_suspend_noflush(tc->thin_md);
		tc = get_next_thin(pool, tc);
	}
}

static void pool_resume_active_thins(struct pool *pool)
{
	struct thin_c *tc;

	/* Resume all active thin devices */
	tc = get_first_thin(pool);
	while (tc) {
		dm_internal_resume(tc->thin_md);
		tc = get_next_thin(pool, tc);
	}
}

static void pool_resume(struct dm_target *ti)
{
	struct pool_c *pt = ti->private;
	struct pool *pool = pt->pool;

	/*
	 * Must requeue active_thins' bios and then resume
	 * active_thins _before_ clearing 'suspend' flag.
	 */
	requeue_bios(pool);
	pool_resume_active_thins(pool);

	spin_lock_irq(&pool->lock);
	pool->low_water_triggered = false;
	pool->suspended = false;
	spin_unlock_irq(&pool->lock);

	do_waker(&pool->waker.work);
}

static void pool_presuspend(struct dm_target *ti)
{
	struct pool_c *pt = ti->private;
	struct pool *pool = pt->pool;

	spin_lock_irq(&pool->lock);
	pool->suspended = true;
	spin_unlock_irq(&pool->lock);

	pool_suspend_active_thins(pool);
}

static void pool_presuspend_undo(struct dm_target *ti)
{
	struct pool_c *pt = ti->private;
	struct pool *pool = pt->pool;

	pool_resume_active_thins(pool);

	spin_lock_irq(&pool->lock);
	pool->suspended = false;
	spin_unlock_irq(&pool->lock);
}

static void pool_postsuspend(struct dm_target *ti)
{
	struct pool_c *pt = ti->private;
	struct pool *pool = pt->pool;

	cancel_delayed_work_sync(&pool->waker);
	cancel_delayed_work_sync(&pool->no_space_timeout);
	flush_workqueue(pool->wq);
	(void) commit(pool);
}

static int check_arg_count(unsigned int argc, unsigned int args_required)
{
	if (argc != args_required) {
		DMWARN("Message received with %u arguments instead of %u.",
		       argc, args_required);
		return -EINVAL;
	}

	return 0;
}

static int read_dev_id(char *arg, dm_thin_id *dev_id, int warning)
{
	if (!kstrtoull(arg, 10, (unsigned long long *)dev_id) &&
	    *dev_id <= MAX_DEV_ID)
		return 0;

	if (warning)
		DMWARN("Message received with invalid device id: %s", arg);

	return -EINVAL;
}

static int process_create_thin_mesg(unsigned int argc, char **argv, struct pool *pool)
{
	dm_thin_id dev_id;
	int r;

	r = check_arg_count(argc, 2);
	if (r)
		return r;

	r = read_dev_id(argv[1], &dev_id, 1);
	if (r)
		return r;

	r = dm_pool_create_thin(pool->pmd, dev_id);
	if (r) {
		DMWARN("Creation of new thinly-provisioned device with id %s failed.",
		       argv[1]);
		return r;
	}

	return 0;
}

static int process_create_snap_mesg(unsigned int argc, char **argv, struct pool *pool)
{
	dm_thin_id dev_id;
	dm_thin_id origin_dev_id;
	int r;

	r = check_arg_count(argc, 3);
	if (r)
		return r;

	r = read_dev_id(argv[1], &dev_id, 1);
	if (r)
		return r;

	r = read_dev_id(argv[2], &origin_dev_id, 1);
	if (r)
		return r;

	r = dm_pool_create_snap(pool->pmd, dev_id, origin_dev_id);
	if (r) {
		DMWARN("Creation of new snapshot %s of device %s failed.",
		       argv[1], argv[2]);
		return r;
	}

	return 0;
}

static int process_delete_mesg(unsigned int argc, char **argv, struct pool *pool)
{
	dm_thin_id dev_id;
	int r;

	r = check_arg_count(argc, 2);
	if (r)
		return r;

	r = read_dev_id(argv[1], &dev_id, 1);
	if (r)
		return r;

	r = dm_pool_delete_thin_device(pool->pmd, dev_id);
	if (r)
		DMWARN("Deletion of thin device %s failed.", argv[1]);

	return r;
}

static int process_set_transaction_id_mesg(unsigned int argc, char **argv, struct pool *pool)
{
	dm_thin_id old_id, new_id;
	int r;

	r = check_arg_count(argc, 3);
	if (r)
		return r;

	if (kstrtoull(argv[1], 10, (unsigned long long *)&old_id)) {
		DMWARN("set_transaction_id message: Unrecognised id %s.", argv[1]);
		return -EINVAL;
	}

	if (kstrtoull(argv[2], 10, (unsigned long long *)&new_id)) {
		DMWARN("set_transaction_id message: Unrecognised new id %s.", argv[2]);
		return -EINVAL;
	}

	r = dm_pool_set_metadata_transaction_id(pool->pmd, old_id, new_id);
	if (r) {
		DMWARN("Failed to change transaction id from %s to %s.",
		       argv[1], argv[2]);
		return r;
	}

	return 0;
}

static int process_reserve_metadata_snap_mesg(unsigned int argc, char **argv, struct pool *pool)
{
	int r;

	r = check_arg_count(argc, 1);
	if (r)
		return r;

	(void) commit(pool);

	r = dm_pool_reserve_metadata_snap(pool->pmd);
	if (r)
		DMWARN("reserve_metadata_snap message failed.");

	return r;
}

static int process_release_metadata_snap_mesg(unsigned int argc, char **argv, struct pool *pool)
{
	int r;

	r = check_arg_count(argc, 1);
	if (r)
		return r;

	r = dm_pool_release_metadata_snap(pool->pmd);
	if (r)
		DMWARN("release_metadata_snap message failed.");

	return r;
}

/*
 * Messages supported:
 *   create_thin	<dev_id>
 *   create_snap	<dev_id> <origin_id>
 *   delete		<dev_id>
 *   set_transaction_id <current_trans_id> <new_trans_id>
 *   reserve_metadata_snap
 *   release_metadata_snap
 */
static int pool_message(struct dm_target *ti, unsigned int argc, char **argv,
			char *result, unsigned int maxlen)
{
	int r = -EINVAL;
	struct pool_c *pt = ti->private;
	struct pool *pool = pt->pool;

	if (get_pool_mode(pool) >= PM_OUT_OF_METADATA_SPACE) {
		DMERR("%s: unable to service pool target messages in READ_ONLY or FAIL mode",
		      dm_device_name(pool->pool_md));
		return -EOPNOTSUPP;
	}

	if (!strcasecmp(argv[0], "create_thin"))
		r = process_create_thin_mesg(argc, argv, pool);

	else if (!strcasecmp(argv[0], "create_snap"))
		r = process_create_snap_mesg(argc, argv, pool);

	else if (!strcasecmp(argv[0], "delete"))
		r = process_delete_mesg(argc, argv, pool);

	else if (!strcasecmp(argv[0], "set_transaction_id"))
		r = process_set_transaction_id_mesg(argc, argv, pool);

	else if (!strcasecmp(argv[0], "reserve_metadata_snap"))
		r = process_reserve_metadata_snap_mesg(argc, argv, pool);

	else if (!strcasecmp(argv[0], "release_metadata_snap"))
		r = process_release_metadata_snap_mesg(argc, argv, pool);

	else
		DMWARN("Unrecognised thin pool target message received: %s", argv[0]);

	if (!r)
		(void) commit(pool);

	return r;
}

static void emit_flags(struct pool_features *pf, char *result,
		       unsigned int sz, unsigned int maxlen)
{
	unsigned int count = !pf->zero_new_blocks + !pf->discard_enabled +
		!pf->discard_passdown + (pf->mode == PM_READ_ONLY) +
		pf->error_if_no_space;
	DMEMIT("%u ", count);

	if (!pf->zero_new_blocks)
		DMEMIT("skip_block_zeroing ");

	if (!pf->discard_enabled)
		DMEMIT("ignore_discard ");

	if (!pf->discard_passdown)
		DMEMIT("no_discard_passdown ");

	if (pf->mode == PM_READ_ONLY)
		DMEMIT("read_only ");

	if (pf->error_if_no_space)
		DMEMIT("error_if_no_space ");
}

/*
 * Status line is:
 *    <transaction id> <used metadata sectors>/<total metadata sectors>
 *    <used data sectors>/<total data sectors> <held metadata root>
 *    <pool mode> <discard config> <no space config> <needs_check>
 */
static void pool_status(struct dm_target *ti, status_type_t type,
			unsigned int status_flags, char *result, unsigned int maxlen)
{
	int r;
	unsigned int sz = 0;
	uint64_t transaction_id;
	dm_block_t nr_free_blocks_data;
	dm_block_t nr_free_blocks_metadata;
	dm_block_t nr_blocks_data;
	dm_block_t nr_blocks_metadata;
	dm_block_t held_root;
	enum pool_mode mode;
	char buf[BDEVNAME_SIZE];
	char buf2[BDEVNAME_SIZE];
	struct pool_c *pt = ti->private;
	struct pool *pool = pt->pool;

	switch (type) {
	case STATUSTYPE_INFO:
		if (get_pool_mode(pool) == PM_FAIL) {
			DMEMIT("Fail");
			break;
		}

		/* Commit to ensure statistics aren't out-of-date */
		if (!(status_flags & DM_STATUS_NOFLUSH_FLAG) && !dm_suspended(ti))
			(void) commit(pool);

		r = dm_pool_get_metadata_transaction_id(pool->pmd, &transaction_id);
		if (r) {
			DMERR("%s: dm_pool_get_metadata_transaction_id returned %d",
			      dm_device_name(pool->pool_md), r);
			goto err;
		}

		r = dm_pool_get_free_metadata_block_count(pool->pmd, &nr_free_blocks_metadata);
		if (r) {
			DMERR("%s: dm_pool_get_free_metadata_block_count returned %d",
			      dm_device_name(pool->pool_md), r);
			goto err;
		}

		r = dm_pool_get_metadata_dev_size(pool->pmd, &nr_blocks_metadata);
		if (r) {
			DMERR("%s: dm_pool_get_metadata_dev_size returned %d",
			      dm_device_name(pool->pool_md), r);
			goto err;
		}

		r = dm_pool_get_free_block_count(pool->pmd, &nr_free_blocks_data);
		if (r) {
			DMERR("%s: dm_pool_get_free_block_count returned %d",
			      dm_device_name(pool->pool_md), r);
			goto err;
		}

		r = dm_pool_get_data_dev_size(pool->pmd, &nr_blocks_data);
		if (r) {
			DMERR("%s: dm_pool_get_data_dev_size returned %d",
			      dm_device_name(pool->pool_md), r);
			goto err;
		}

		r = dm_pool_get_metadata_snap(pool->pmd, &held_root);
		if (r) {
			DMERR("%s: dm_pool_get_metadata_snap returned %d",
			      dm_device_name(pool->pool_md), r);
			goto err;
		}

		DMEMIT("%llu %llu/%llu %llu/%llu ",
		       (unsigned long long)transaction_id,
		       (unsigned long long)(nr_blocks_metadata - nr_free_blocks_metadata),
		       (unsigned long long)nr_blocks_metadata,
		       (unsigned long long)(nr_blocks_data - nr_free_blocks_data),
		       (unsigned long long)nr_blocks_data);

		if (held_root)
			DMEMIT("%llu ", held_root);
		else
			DMEMIT("- ");

		mode = get_pool_mode(pool);
		if (mode == PM_OUT_OF_DATA_SPACE)
			DMEMIT("out_of_data_space ");
		else if (is_read_only_pool_mode(mode))
			DMEMIT("ro ");
		else
			DMEMIT("rw ");

		if (!pool->pf.discard_enabled)
			DMEMIT("ignore_discard ");
		else if (pool->pf.discard_passdown)
			DMEMIT("discard_passdown ");
		else
			DMEMIT("no_discard_passdown ");

		if (pool->pf.error_if_no_space)
			DMEMIT("error_if_no_space ");
		else
			DMEMIT("queue_if_no_space ");

		if (dm_pool_metadata_needs_check(pool->pmd))
			DMEMIT("needs_check ");
		else
			DMEMIT("- ");

		DMEMIT("%llu ", (unsigned long long)calc_metadata_threshold(pt));

		break;

	case STATUSTYPE_TABLE:
		DMEMIT("%s %s %lu %llu ",
		       format_dev_t(buf, pt->metadata_dev->bdev->bd_dev),
		       format_dev_t(buf2, pt->data_dev->bdev->bd_dev),
		       (unsigned long)pool->sectors_per_block,
		       (unsigned long long)pt->low_water_blocks);
		emit_flags(&pt->requested_pf, result, sz, maxlen);
		break;

	case STATUSTYPE_IMA:
		*result = '\0';
		break;
	}
	return;

err:
	DMEMIT("Error");
}

static int pool_iterate_devices(struct dm_target *ti,
				iterate_devices_callout_fn fn, void *data)
{
	struct pool_c *pt = ti->private;

	return fn(ti, pt->data_dev, 0, ti->len, data);
}

static void pool_io_hints(struct dm_target *ti, struct queue_limits *limits)
{
	struct pool_c *pt = ti->private;
	struct pool *pool = pt->pool;
	sector_t io_opt_sectors = limits->io_opt >> SECTOR_SHIFT;

	/*
	 * If max_sectors is smaller than pool->sectors_per_block adjust it
	 * to the highest possible power-of-2 factor of pool->sectors_per_block.
	 * This is especially beneficial when the pool's data device is a RAID
	 * device that has a full stripe width that matches pool->sectors_per_block
	 * -- because even though partial RAID stripe-sized IOs will be issued to a
	 *    single RAID stripe; when aggregated they will end on a full RAID stripe
	 *    boundary.. which avoids additional partial RAID stripe writes cascading
	 */
	if (limits->max_sectors < pool->sectors_per_block) {
		while (!is_factor(pool->sectors_per_block, limits->max_sectors)) {
			if ((limits->max_sectors & (limits->max_sectors - 1)) == 0)
				limits->max_sectors--;
			limits->max_sectors = rounddown_pow_of_two(limits->max_sectors);
		}
	}

	/*
	 * If the system-determined stacked limits are compatible with the
	 * pool's blocksize (io_opt is a factor) do not override them.
	 */
	if (io_opt_sectors < pool->sectors_per_block ||
	    !is_factor(io_opt_sectors, pool->sectors_per_block)) {
		if (is_factor(pool->sectors_per_block, limits->max_sectors))
			blk_limits_io_min(limits, limits->max_sectors << SECTOR_SHIFT);
		else
			blk_limits_io_min(limits, pool->sectors_per_block << SECTOR_SHIFT);
		blk_limits_io_opt(limits, pool->sectors_per_block << SECTOR_SHIFT);
	}

	/*
	 * pt->adjusted_pf is a staging area for the actual features to use.
	 * They get transferred to the live pool in bind_control_target()
	 * called from pool_preresume().
	 */

	if (pt->adjusted_pf.discard_enabled) {
		disable_discard_passdown_if_not_supported(pt);
		if (!pt->adjusted_pf.discard_passdown)
			limits->max_discard_sectors = 0;
		/*
		 * The pool uses the same discard limits as the underlying data
		 * device.  DM core has already set this up.
		 */
	} else {
		/*
		 * Must explicitly disallow stacking discard limits otherwise the
		 * block layer will stack them if pool's data device has support.
		 */
		limits->discard_granularity = 0;
	}
}

static struct target_type pool_target = {
	.name = "thin-pool",
	.features = DM_TARGET_SINGLETON | DM_TARGET_ALWAYS_WRITEABLE |
		    DM_TARGET_IMMUTABLE,
	.version = {1, 23, 0},
	.module = THIS_MODULE,
	.ctr = pool_ctr,
	.dtr = pool_dtr,
	.map = pool_map,
	.presuspend = pool_presuspend,
	.presuspend_undo = pool_presuspend_undo,
	.postsuspend = pool_postsuspend,
	.preresume = pool_preresume,
	.resume = pool_resume,
	.message = pool_message,
	.status = pool_status,
	.iterate_devices = pool_iterate_devices,
	.io_hints = pool_io_hints,
};

/*
 *--------------------------------------------------------------
 * Thin target methods
 *--------------------------------------------------------------
 */
static void thin_get(struct thin_c *tc)
{
	refcount_inc(&tc->refcount);
}

static void thin_put(struct thin_c *tc)
{
	if (refcount_dec_and_test(&tc->refcount))
		complete(&tc->can_destroy);
}

static void thin_dtr(struct dm_target *ti)
{
	struct thin_c *tc = ti->private;

	spin_lock_irq(&tc->pool->lock);
	list_del_rcu(&tc->list);
	spin_unlock_irq(&tc->pool->lock);
	synchronize_rcu();

	thin_put(tc);
	wait_for_completion(&tc->can_destroy);

	mutex_lock(&dm_thin_pool_table.mutex);

	__pool_dec(tc->pool);
	dm_pool_close_thin_device(tc->td);
	dm_put_device(ti, tc->pool_dev);
	if (tc->origin_dev)
		dm_put_device(ti, tc->origin_dev);
	kfree(tc);

	mutex_unlock(&dm_thin_pool_table.mutex);
}

/*
 * Thin target parameters:
 *
 * <pool_dev> <dev_id> [origin_dev]
 *
 * pool_dev: the path to the pool (eg, /dev/mapper/my_pool)
 * dev_id: the internal device identifier
 * origin_dev: a device external to the pool that should act as the origin
 *
 * If the pool device has discards disabled, they get disabled for the thin
 * device as well.
 */
static int thin_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	int r;
	struct thin_c *tc;
	struct dm_dev *pool_dev, *origin_dev;
	struct mapped_device *pool_md;

	mutex_lock(&dm_thin_pool_table.mutex);

	if (argc != 2 && argc != 3) {
		ti->error = "Invalid argument count";
		r = -EINVAL;
		goto out_unlock;
	}

	tc = ti->private = kzalloc(sizeof(*tc), GFP_KERNEL);
	if (!tc) {
		ti->error = "Out of memory";
		r = -ENOMEM;
		goto out_unlock;
	}
	tc->thin_md = dm_table_get_md(ti->table);
	spin_lock_init(&tc->lock);
	INIT_LIST_HEAD(&tc->deferred_cells);
	bio_list_init(&tc->deferred_bio_list);
	bio_list_init(&tc->retry_on_resume_list);
	tc->sort_bio_list = RB_ROOT;

	if (argc == 3) {
		if (!strcmp(argv[0], argv[2])) {
			ti->error = "Error setting origin device";
			r = -EINVAL;
			goto bad_origin_dev;
		}

		r = dm_get_device(ti, argv[2], BLK_OPEN_READ, &origin_dev);
		if (r) {
			ti->error = "Error opening origin device";
			goto bad_origin_dev;
		}
		tc->origin_dev = origin_dev;
	}

	r = dm_get_device(ti, argv[0], dm_table_get_mode(ti->table), &pool_dev);
	if (r) {
		ti->error = "Error opening pool device";
		goto bad_pool_dev;
	}
	tc->pool_dev = pool_dev;

	if (read_dev_id(argv[1], (unsigned long long *)&tc->dev_id, 0)) {
		ti->error = "Invalid device id";
		r = -EINVAL;
		goto bad_common;
	}

	pool_md = dm_get_md(tc->pool_dev->bdev->bd_dev);
	if (!pool_md) {
		ti->error = "Couldn't get pool mapped device";
		r = -EINVAL;
		goto bad_common;
	}

	tc->pool = __pool_table_lookup(pool_md);
	if (!tc->pool) {
		ti->error = "Couldn't find pool object";
		r = -EINVAL;
		goto bad_pool_lookup;
	}
	__pool_inc(tc->pool);

	if (get_pool_mode(tc->pool) == PM_FAIL) {
		ti->error = "Couldn't open thin device, Pool is in fail mode";
		r = -EINVAL;
		goto bad_pool;
	}

	r = dm_pool_open_thin_device(tc->pool->pmd, tc->dev_id, &tc->td);
	if (r) {
		ti->error = "Couldn't open thin internal device";
		goto bad_pool;
	}

	r = dm_set_target_max_io_len(ti, tc->pool->sectors_per_block);
	if (r)
		goto bad;

	ti->num_flush_bios = 1;
	ti->limit_swap_bios = true;
	ti->flush_supported = true;
	ti->accounts_remapped_io = true;
	ti->per_io_data_size = sizeof(struct dm_thin_endio_hook);

	/* In case the pool supports discards, pass them on. */
	if (tc->pool->pf.discard_enabled) {
		ti->discards_supported = true;
		ti->num_discard_bios = 1;
		ti->max_discard_granularity = true;
	}

	mutex_unlock(&dm_thin_pool_table.mutex);

	spin_lock_irq(&tc->pool->lock);
	if (tc->pool->suspended) {
		spin_unlock_irq(&tc->pool->lock);
		mutex_lock(&dm_thin_pool_table.mutex); /* reacquire for __pool_dec */
		ti->error = "Unable to activate thin device while pool is suspended";
		r = -EINVAL;
		goto bad;
	}
	refcount_set(&tc->refcount, 1);
	init_completion(&tc->can_destroy);
	list_add_tail_rcu(&tc->list, &tc->pool->active_thins);
	spin_unlock_irq(&tc->pool->lock);
	/*
	 * This synchronize_rcu() call is needed here otherwise we risk a
	 * wake_worker() call finding no bios to process (because the newly
	 * added tc isn't yet visible).  So this reduces latency since we
	 * aren't then dependent on the periodic commit to wake_worker().
	 */
	synchronize_rcu();

	dm_put(pool_md);

	return 0;

bad:
	dm_pool_close_thin_device(tc->td);
bad_pool:
	__pool_dec(tc->pool);
bad_pool_lookup:
	dm_put(pool_md);
bad_common:
	dm_put_device(ti, tc->pool_dev);
bad_pool_dev:
	if (tc->origin_dev)
		dm_put_device(ti, tc->origin_dev);
bad_origin_dev:
	kfree(tc);
out_unlock:
	mutex_unlock(&dm_thin_pool_table.mutex);

	return r;
}

static int thin_map(struct dm_target *ti, struct bio *bio)
{
	bio->bi_iter.bi_sector = dm_target_offset(ti, bio->bi_iter.bi_sector);

	return thin_bio_map(ti, bio);
}

static int thin_endio(struct dm_target *ti, struct bio *bio,
		blk_status_t *err)
{
	unsigned long flags;
	struct dm_thin_endio_hook *h = dm_per_bio_data(bio, sizeof(struct dm_thin_endio_hook));
	struct list_head work;
	struct dm_thin_new_mapping *m, *tmp;
	struct pool *pool = h->tc->pool;

	if (h->shared_read_entry) {
		INIT_LIST_HEAD(&work);
		dm_deferred_entry_dec(h->shared_read_entry, &work);

		spin_lock_irqsave(&pool->lock, flags);
		list_for_each_entry_safe(m, tmp, &work, list) {
			list_del(&m->list);
			__complete_mapping_preparation(m);
		}
		spin_unlock_irqrestore(&pool->lock, flags);
	}

	if (h->all_io_entry) {
		INIT_LIST_HEAD(&work);
		dm_deferred_entry_dec(h->all_io_entry, &work);
		if (!list_empty(&work)) {
			spin_lock_irqsave(&pool->lock, flags);
			list_for_each_entry_safe(m, tmp, &work, list)
				list_add_tail(&m->list, &pool->prepared_discards);
			spin_unlock_irqrestore(&pool->lock, flags);
			wake_worker(pool);
		}
	}

	if (h->cell)
		cell_defer_no_holder(h->tc, h->cell);

	return DM_ENDIO_DONE;
}

static void thin_presuspend(struct dm_target *ti)
{
	struct thin_c *tc = ti->private;

	if (dm_noflush_suspending(ti))
		noflush_work(tc, do_noflush_start);
}

static void thin_postsuspend(struct dm_target *ti)
{
	struct thin_c *tc = ti->private;

	/*
	 * The dm_noflush_suspending flag has been cleared by now, so
	 * unfortunately we must always run this.
	 */
	noflush_work(tc, do_noflush_stop);
}

static int thin_preresume(struct dm_target *ti)
{
	struct thin_c *tc = ti->private;

	if (tc->origin_dev)
		tc->origin_size = get_dev_size(tc->origin_dev->bdev);

	return 0;
}

/*
 * <nr mapped sectors> <highest mapped sector>
 */
static void thin_status(struct dm_target *ti, status_type_t type,
			unsigned int status_flags, char *result, unsigned int maxlen)
{
	int r;
	ssize_t sz = 0;
	dm_block_t mapped, highest;
	char buf[BDEVNAME_SIZE];
	struct thin_c *tc = ti->private;

	if (get_pool_mode(tc->pool) == PM_FAIL) {
		DMEMIT("Fail");
		return;
	}

	if (!tc->td)
		DMEMIT("-");
	else {
		switch (type) {
		case STATUSTYPE_INFO:
			r = dm_thin_get_mapped_count(tc->td, &mapped);
			if (r) {
				DMERR("dm_thin_get_mapped_count returned %d", r);
				goto err;
			}

			r = dm_thin_get_highest_mapped_block(tc->td, &highest);
			if (r < 0) {
				DMERR("dm_thin_get_highest_mapped_block returned %d", r);
				goto err;
			}

			DMEMIT("%llu ", mapped * tc->pool->sectors_per_block);
			if (r)
				DMEMIT("%llu", ((highest + 1) *
						tc->pool->sectors_per_block) - 1);
			else
				DMEMIT("-");
			break;

		case STATUSTYPE_TABLE:
			DMEMIT("%s %lu",
			       format_dev_t(buf, tc->pool_dev->bdev->bd_dev),
			       (unsigned long) tc->dev_id);
			if (tc->origin_dev)
				DMEMIT(" %s", format_dev_t(buf, tc->origin_dev->bdev->bd_dev));
			break;

		case STATUSTYPE_IMA:
			*result = '\0';
			break;
		}
	}

	return;

err:
	DMEMIT("Error");
}

static int thin_iterate_devices(struct dm_target *ti,
				iterate_devices_callout_fn fn, void *data)
{
	sector_t blocks;
	struct thin_c *tc = ti->private;
	struct pool *pool = tc->pool;

	/*
	 * We can't call dm_pool_get_data_dev_size() since that blocks.  So
	 * we follow a more convoluted path through to the pool's target.
	 */
	if (!pool->ti)
		return 0;	/* nothing is bound */

	blocks = pool->ti->len;
	(void) sector_div(blocks, pool->sectors_per_block);
	if (blocks)
		return fn(ti, tc->pool_dev, 0, pool->sectors_per_block * blocks, data);

	return 0;
}

static void thin_io_hints(struct dm_target *ti, struct queue_limits *limits)
{
	struct thin_c *tc = ti->private;
	struct pool *pool = tc->pool;

	if (pool->pf.discard_enabled) {
		limits->discard_granularity = pool->sectors_per_block << SECTOR_SHIFT;
		limits->max_discard_sectors = pool->sectors_per_block * BIO_PRISON_MAX_RANGE;
	}
}

static struct target_type thin_target = {
	.name = "thin",
	.version = {1, 23, 0},
	.module	= THIS_MODULE,
	.ctr = thin_ctr,
	.dtr = thin_dtr,
	.map = thin_map,
	.end_io = thin_endio,
	.preresume = thin_preresume,
	.presuspend = thin_presuspend,
	.postsuspend = thin_postsuspend,
	.status = thin_status,
	.iterate_devices = thin_iterate_devices,
	.io_hints = thin_io_hints,
};

/*----------------------------------------------------------------*/

static int __init dm_thin_init(void)
{
	int r = -ENOMEM;

	pool_table_init();

	_new_mapping_cache = KMEM_CACHE(dm_thin_new_mapping, 0);
	if (!_new_mapping_cache)
		return r;

	r = dm_register_target(&thin_target);
	if (r)
		goto bad_new_mapping_cache;

	r = dm_register_target(&pool_target);
	if (r)
		goto bad_thin_target;

	return 0;

bad_thin_target:
	dm_unregister_target(&thin_target);
bad_new_mapping_cache:
	kmem_cache_destroy(_new_mapping_cache);

	return r;
}

static void dm_thin_exit(void)
{
	dm_unregister_target(&thin_target);
	dm_unregister_target(&pool_target);

	kmem_cache_destroy(_new_mapping_cache);

	pool_table_exit();
}

module_init(dm_thin_init);
module_exit(dm_thin_exit);

module_param_named(no_space_timeout, no_space_timeout_secs, uint, 0644);
MODULE_PARM_DESC(no_space_timeout, "Out of data space queue IO timeout in seconds");

MODULE_DESCRIPTION(DM_NAME " thin provisioning target");
MODULE_AUTHOR("Joe Thornber <dm-devel@lists.linux.dev>");
MODULE_LICENSE("GPL");
