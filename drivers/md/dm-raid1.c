/*
 * Copyright (C) 2003 Sistina Software Limited.
 *
 * This file is released under the GPL.
 */

#include "dm.h"
#include "dm-bio-list.h"
#include "dm-io.h"
#include "dm-log.h"
#include "kcopyd.h"

#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/mempool.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>

#define DM_MSG_PREFIX "raid1"
#define DM_IO_PAGES 64

#define DM_RAID1_HANDLE_ERRORS 0x01

static DECLARE_WAIT_QUEUE_HEAD(_kmirrord_recovery_stopped);

/*-----------------------------------------------------------------
 * Region hash
 *
 * The mirror splits itself up into discrete regions.  Each
 * region can be in one of three states: clean, dirty,
 * nosync.  There is no need to put clean regions in the hash.
 *
 * In addition to being present in the hash table a region _may_
 * be present on one of three lists.
 *
 *   clean_regions: Regions on this list have no io pending to
 *   them, they are in sync, we are no longer interested in them,
 *   they are dull.  rh_update_states() will remove them from the
 *   hash table.
 *
 *   quiesced_regions: These regions have been spun down, ready
 *   for recovery.  rh_recovery_start() will remove regions from
 *   this list and hand them to kmirrord, which will schedule the
 *   recovery io with kcopyd.
 *
 *   recovered_regions: Regions that kcopyd has successfully
 *   recovered.  rh_update_states() will now schedule any delayed
 *   io, up the recovery_count, and remove the region from the
 *   hash.
 *
 * There are 2 locks:
 *   A rw spin lock 'hash_lock' protects just the hash table,
 *   this is never held in write mode from interrupt context,
 *   which I believe means that we only have to disable irqs when
 *   doing a write lock.
 *
 *   An ordinary spin lock 'region_lock' that protects the three
 *   lists in the region_hash, with the 'state', 'list' and
 *   'bhs_delayed' fields of the regions.  This is used from irq
 *   context, so all other uses will have to suspend local irqs.
 *---------------------------------------------------------------*/
struct mirror_set;
struct region_hash {
	struct mirror_set *ms;
	uint32_t region_size;
	unsigned region_shift;

	/* holds persistent region state */
	struct dirty_log *log;

	/* hash table */
	rwlock_t hash_lock;
	mempool_t *region_pool;
	unsigned int mask;
	unsigned int nr_buckets;
	struct list_head *buckets;

	spinlock_t region_lock;
	atomic_t recovery_in_flight;
	struct semaphore recovery_count;
	struct list_head clean_regions;
	struct list_head quiesced_regions;
	struct list_head recovered_regions;
};

enum {
	RH_CLEAN,
	RH_DIRTY,
	RH_NOSYNC,
	RH_RECOVERING
};

struct region {
	struct region_hash *rh;	/* FIXME: can we get rid of this ? */
	region_t key;
	int state;

	struct list_head hash_list;
	struct list_head list;

	atomic_t pending;
	struct bio_list delayed_bios;
};


/*-----------------------------------------------------------------
 * Mirror set structures.
 *---------------------------------------------------------------*/
struct mirror {
	atomic_t error_count;
	struct dm_dev *dev;
	sector_t offset;
};

struct mirror_set {
	struct dm_target *ti;
	struct list_head list;
	struct region_hash rh;
	struct kcopyd_client *kcopyd_client;
	uint64_t features;

	spinlock_t lock;	/* protects the next two lists */
	struct bio_list reads;
	struct bio_list writes;

	struct dm_io_client *io_client;

	/* recovery */
	region_t nr_regions;
	int in_sync;

	struct mirror *default_mirror;	/* Default mirror */

	struct workqueue_struct *kmirrord_wq;
	struct work_struct kmirrord_work;

	unsigned int nr_mirrors;
	struct mirror mirror[0];
};

/*
 * Conversion fns
 */
static inline region_t bio_to_region(struct region_hash *rh, struct bio *bio)
{
	return (bio->bi_sector - rh->ms->ti->begin) >> rh->region_shift;
}

static inline sector_t region_to_sector(struct region_hash *rh, region_t region)
{
	return region << rh->region_shift;
}

static void wake(struct mirror_set *ms)
{
	queue_work(ms->kmirrord_wq, &ms->kmirrord_work);
}

/* FIXME move this */
static void queue_bio(struct mirror_set *ms, struct bio *bio, int rw);

#define MIN_REGIONS 64
#define MAX_RECOVERY 1
static int rh_init(struct region_hash *rh, struct mirror_set *ms,
		   struct dirty_log *log, uint32_t region_size,
		   region_t nr_regions)
{
	unsigned int nr_buckets, max_buckets;
	size_t i;

	/*
	 * Calculate a suitable number of buckets for our hash
	 * table.
	 */
	max_buckets = nr_regions >> 6;
	for (nr_buckets = 128u; nr_buckets < max_buckets; nr_buckets <<= 1)
		;
	nr_buckets >>= 1;

	rh->ms = ms;
	rh->log = log;
	rh->region_size = region_size;
	rh->region_shift = ffs(region_size) - 1;
	rwlock_init(&rh->hash_lock);
	rh->mask = nr_buckets - 1;
	rh->nr_buckets = nr_buckets;

	rh->buckets = vmalloc(nr_buckets * sizeof(*rh->buckets));
	if (!rh->buckets) {
		DMERR("unable to allocate region hash memory");
		return -ENOMEM;
	}

	for (i = 0; i < nr_buckets; i++)
		INIT_LIST_HEAD(rh->buckets + i);

	spin_lock_init(&rh->region_lock);
	sema_init(&rh->recovery_count, 0);
	atomic_set(&rh->recovery_in_flight, 0);
	INIT_LIST_HEAD(&rh->clean_regions);
	INIT_LIST_HEAD(&rh->quiesced_regions);
	INIT_LIST_HEAD(&rh->recovered_regions);

	rh->region_pool = mempool_create_kmalloc_pool(MIN_REGIONS,
						      sizeof(struct region));
	if (!rh->region_pool) {
		vfree(rh->buckets);
		rh->buckets = NULL;
		return -ENOMEM;
	}

	return 0;
}

static void rh_exit(struct region_hash *rh)
{
	unsigned int h;
	struct region *reg, *nreg;

	BUG_ON(!list_empty(&rh->quiesced_regions));
	for (h = 0; h < rh->nr_buckets; h++) {
		list_for_each_entry_safe(reg, nreg, rh->buckets + h, hash_list) {
			BUG_ON(atomic_read(&reg->pending));
			mempool_free(reg, rh->region_pool);
		}
	}

	if (rh->log)
		dm_destroy_dirty_log(rh->log);
	if (rh->region_pool)
		mempool_destroy(rh->region_pool);
	vfree(rh->buckets);
}

#define RH_HASH_MULT 2654435387U

static inline unsigned int rh_hash(struct region_hash *rh, region_t region)
{
	return (unsigned int) ((region * RH_HASH_MULT) >> 12) & rh->mask;
}

static struct region *__rh_lookup(struct region_hash *rh, region_t region)
{
	struct region *reg;

	list_for_each_entry (reg, rh->buckets + rh_hash(rh, region), hash_list)
		if (reg->key == region)
			return reg;

	return NULL;
}

static void __rh_insert(struct region_hash *rh, struct region *reg)
{
	unsigned int h = rh_hash(rh, reg->key);
	list_add(&reg->hash_list, rh->buckets + h);
}

static struct region *__rh_alloc(struct region_hash *rh, region_t region)
{
	struct region *reg, *nreg;

	read_unlock(&rh->hash_lock);
	nreg = mempool_alloc(rh->region_pool, GFP_ATOMIC);
	if (unlikely(!nreg))
		nreg = kmalloc(sizeof(struct region), GFP_NOIO);
	nreg->state = rh->log->type->in_sync(rh->log, region, 1) ?
		RH_CLEAN : RH_NOSYNC;
	nreg->rh = rh;
	nreg->key = region;

	INIT_LIST_HEAD(&nreg->list);

	atomic_set(&nreg->pending, 0);
	bio_list_init(&nreg->delayed_bios);
	write_lock_irq(&rh->hash_lock);

	reg = __rh_lookup(rh, region);
	if (reg)
		/* we lost the race */
		mempool_free(nreg, rh->region_pool);

	else {
		__rh_insert(rh, nreg);
		if (nreg->state == RH_CLEAN) {
			spin_lock(&rh->region_lock);
			list_add(&nreg->list, &rh->clean_regions);
			spin_unlock(&rh->region_lock);
		}
		reg = nreg;
	}
	write_unlock_irq(&rh->hash_lock);
	read_lock(&rh->hash_lock);

	return reg;
}

static inline struct region *__rh_find(struct region_hash *rh, region_t region)
{
	struct region *reg;

	reg = __rh_lookup(rh, region);
	if (!reg)
		reg = __rh_alloc(rh, region);

	return reg;
}

static int rh_state(struct region_hash *rh, region_t region, int may_block)
{
	int r;
	struct region *reg;

	read_lock(&rh->hash_lock);
	reg = __rh_lookup(rh, region);
	read_unlock(&rh->hash_lock);

	if (reg)
		return reg->state;

	/*
	 * The region wasn't in the hash, so we fall back to the
	 * dirty log.
	 */
	r = rh->log->type->in_sync(rh->log, region, may_block);

	/*
	 * Any error from the dirty log (eg. -EWOULDBLOCK) gets
	 * taken as a RH_NOSYNC
	 */
	return r == 1 ? RH_CLEAN : RH_NOSYNC;
}

static inline int rh_in_sync(struct region_hash *rh,
			     region_t region, int may_block)
{
	int state = rh_state(rh, region, may_block);
	return state == RH_CLEAN || state == RH_DIRTY;
}

static void dispatch_bios(struct mirror_set *ms, struct bio_list *bio_list)
{
	struct bio *bio;

	while ((bio = bio_list_pop(bio_list))) {
		queue_bio(ms, bio, WRITE);
	}
}

static void complete_resync_work(struct region *reg, int success)
{
	struct region_hash *rh = reg->rh;

	rh->log->type->set_region_sync(rh->log, reg->key, success);
	dispatch_bios(rh->ms, &reg->delayed_bios);
	if (atomic_dec_and_test(&rh->recovery_in_flight))
		wake_up_all(&_kmirrord_recovery_stopped);
	up(&rh->recovery_count);
}

static void rh_update_states(struct region_hash *rh)
{
	struct region *reg, *next;

	LIST_HEAD(clean);
	LIST_HEAD(recovered);

	/*
	 * Quickly grab the lists.
	 */
	write_lock_irq(&rh->hash_lock);
	spin_lock(&rh->region_lock);
	if (!list_empty(&rh->clean_regions)) {
		list_splice(&rh->clean_regions, &clean);
		INIT_LIST_HEAD(&rh->clean_regions);

		list_for_each_entry (reg, &clean, list) {
			rh->log->type->clear_region(rh->log, reg->key);
			list_del(&reg->hash_list);
		}
	}

	if (!list_empty(&rh->recovered_regions)) {
		list_splice(&rh->recovered_regions, &recovered);
		INIT_LIST_HEAD(&rh->recovered_regions);

		list_for_each_entry (reg, &recovered, list)
			list_del(&reg->hash_list);
	}
	spin_unlock(&rh->region_lock);
	write_unlock_irq(&rh->hash_lock);

	/*
	 * All the regions on the recovered and clean lists have
	 * now been pulled out of the system, so no need to do
	 * any more locking.
	 */
	list_for_each_entry_safe (reg, next, &recovered, list) {
		rh->log->type->clear_region(rh->log, reg->key);
		complete_resync_work(reg, 1);
		mempool_free(reg, rh->region_pool);
	}

	rh->log->type->flush(rh->log);

	list_for_each_entry_safe (reg, next, &clean, list)
		mempool_free(reg, rh->region_pool);
}

static void rh_inc(struct region_hash *rh, region_t region)
{
	struct region *reg;

	read_lock(&rh->hash_lock);
	reg = __rh_find(rh, region);

	spin_lock_irq(&rh->region_lock);
	atomic_inc(&reg->pending);

	if (reg->state == RH_CLEAN) {
		reg->state = RH_DIRTY;
		list_del_init(&reg->list);	/* take off the clean list */
		spin_unlock_irq(&rh->region_lock);

		rh->log->type->mark_region(rh->log, reg->key);
	} else
		spin_unlock_irq(&rh->region_lock);


	read_unlock(&rh->hash_lock);
}

static void rh_inc_pending(struct region_hash *rh, struct bio_list *bios)
{
	struct bio *bio;

	for (bio = bios->head; bio; bio = bio->bi_next)
		rh_inc(rh, bio_to_region(rh, bio));
}

static void rh_dec(struct region_hash *rh, region_t region)
{
	unsigned long flags;
	struct region *reg;
	int should_wake = 0;

	read_lock(&rh->hash_lock);
	reg = __rh_lookup(rh, region);
	read_unlock(&rh->hash_lock);

	spin_lock_irqsave(&rh->region_lock, flags);
	if (atomic_dec_and_test(&reg->pending)) {
		/*
		 * There is no pending I/O for this region.
		 * We can move the region to corresponding list for next action.
		 * At this point, the region is not yet connected to any list.
		 *
		 * If the state is RH_NOSYNC, the region should be kept off
		 * from clean list.
		 * The hash entry for RH_NOSYNC will remain in memory
		 * until the region is recovered or the map is reloaded.
		 */

		/* do nothing for RH_NOSYNC */
		if (reg->state == RH_RECOVERING) {
			list_add_tail(&reg->list, &rh->quiesced_regions);
		} else if (reg->state == RH_DIRTY) {
			reg->state = RH_CLEAN;
			list_add(&reg->list, &rh->clean_regions);
		}
		should_wake = 1;
	}
	spin_unlock_irqrestore(&rh->region_lock, flags);

	if (should_wake)
		wake(rh->ms);
}

/*
 * Starts quiescing a region in preparation for recovery.
 */
static int __rh_recovery_prepare(struct region_hash *rh)
{
	int r;
	struct region *reg;
	region_t region;

	/*
	 * Ask the dirty log what's next.
	 */
	r = rh->log->type->get_resync_work(rh->log, &region);
	if (r <= 0)
		return r;

	/*
	 * Get this region, and start it quiescing by setting the
	 * recovering flag.
	 */
	read_lock(&rh->hash_lock);
	reg = __rh_find(rh, region);
	read_unlock(&rh->hash_lock);

	spin_lock_irq(&rh->region_lock);
	reg->state = RH_RECOVERING;

	/* Already quiesced ? */
	if (atomic_read(&reg->pending))
		list_del_init(&reg->list);
	else
		list_move(&reg->list, &rh->quiesced_regions);

	spin_unlock_irq(&rh->region_lock);

	return 1;
}

static void rh_recovery_prepare(struct region_hash *rh)
{
	/* Extra reference to avoid race with rh_stop_recovery */
	atomic_inc(&rh->recovery_in_flight);

	while (!down_trylock(&rh->recovery_count)) {
		atomic_inc(&rh->recovery_in_flight);
		if (__rh_recovery_prepare(rh) <= 0) {
			atomic_dec(&rh->recovery_in_flight);
			up(&rh->recovery_count);
			break;
		}
	}

	/* Drop the extra reference */
	if (atomic_dec_and_test(&rh->recovery_in_flight))
		wake_up_all(&_kmirrord_recovery_stopped);
}

/*
 * Returns any quiesced regions.
 */
static struct region *rh_recovery_start(struct region_hash *rh)
{
	struct region *reg = NULL;

	spin_lock_irq(&rh->region_lock);
	if (!list_empty(&rh->quiesced_regions)) {
		reg = list_entry(rh->quiesced_regions.next,
				 struct region, list);
		list_del_init(&reg->list);	/* remove from the quiesced list */
	}
	spin_unlock_irq(&rh->region_lock);

	return reg;
}

/* FIXME: success ignored for now */
static void rh_recovery_end(struct region *reg, int success)
{
	struct region_hash *rh = reg->rh;

	spin_lock_irq(&rh->region_lock);
	list_add(&reg->list, &reg->rh->recovered_regions);
	spin_unlock_irq(&rh->region_lock);

	wake(rh->ms);
}

static void rh_flush(struct region_hash *rh)
{
	rh->log->type->flush(rh->log);
}

static void rh_delay(struct region_hash *rh, struct bio *bio)
{
	struct region *reg;

	read_lock(&rh->hash_lock);
	reg = __rh_find(rh, bio_to_region(rh, bio));
	bio_list_add(&reg->delayed_bios, bio);
	read_unlock(&rh->hash_lock);
}

static void rh_stop_recovery(struct region_hash *rh)
{
	int i;

	/* wait for any recovering regions */
	for (i = 0; i < MAX_RECOVERY; i++)
		down(&rh->recovery_count);
}

static void rh_start_recovery(struct region_hash *rh)
{
	int i;

	for (i = 0; i < MAX_RECOVERY; i++)
		up(&rh->recovery_count);

	wake(rh->ms);
}

/*
 * Every mirror should look like this one.
 */
#define DEFAULT_MIRROR 0

/*
 * This is yucky.  We squirrel the mirror_set struct away inside
 * bi_next for write buffers.  This is safe since the bh
 * doesn't get submitted to the lower levels of block layer.
 */
static struct mirror_set *bio_get_ms(struct bio *bio)
{
	return (struct mirror_set *) bio->bi_next;
}

static void bio_set_ms(struct bio *bio, struct mirror_set *ms)
{
	bio->bi_next = (struct bio *) ms;
}

/*-----------------------------------------------------------------
 * Recovery.
 *
 * When a mirror is first activated we may find that some regions
 * are in the no-sync state.  We have to recover these by
 * recopying from the default mirror to all the others.
 *---------------------------------------------------------------*/
static void recovery_complete(int read_err, unsigned int write_err,
			      void *context)
{
	struct region *reg = (struct region *) context;

	/* FIXME: better error handling */
	rh_recovery_end(reg, !(read_err || write_err));
}

static int recover(struct mirror_set *ms, struct region *reg)
{
	int r;
	unsigned int i;
	struct io_region from, to[KCOPYD_MAX_REGIONS], *dest;
	struct mirror *m;
	unsigned long flags = 0;

	/* fill in the source */
	m = ms->default_mirror;
	from.bdev = m->dev->bdev;
	from.sector = m->offset + region_to_sector(reg->rh, reg->key);
	if (reg->key == (ms->nr_regions - 1)) {
		/*
		 * The final region may be smaller than
		 * region_size.
		 */
		from.count = ms->ti->len & (reg->rh->region_size - 1);
		if (!from.count)
			from.count = reg->rh->region_size;
	} else
		from.count = reg->rh->region_size;

	/* fill in the destinations */
	for (i = 0, dest = to; i < ms->nr_mirrors; i++) {
		if (&ms->mirror[i] == ms->default_mirror)
			continue;

		m = ms->mirror + i;
		dest->bdev = m->dev->bdev;
		dest->sector = m->offset + region_to_sector(reg->rh, reg->key);
		dest->count = from.count;
		dest++;
	}

	/* hand to kcopyd */
	set_bit(KCOPYD_IGNORE_ERROR, &flags);
	r = kcopyd_copy(ms->kcopyd_client, &from, ms->nr_mirrors - 1, to, flags,
			recovery_complete, reg);

	return r;
}

static void do_recovery(struct mirror_set *ms)
{
	int r;
	struct region *reg;
	struct dirty_log *log = ms->rh.log;

	/*
	 * Start quiescing some regions.
	 */
	rh_recovery_prepare(&ms->rh);

	/*
	 * Copy any already quiesced regions.
	 */
	while ((reg = rh_recovery_start(&ms->rh))) {
		r = recover(ms, reg);
		if (r)
			rh_recovery_end(reg, 0);
	}

	/*
	 * Update the in sync flag.
	 */
	if (!ms->in_sync &&
	    (log->type->get_sync_count(log) == ms->nr_regions)) {
		/* the sync is complete */
		dm_table_event(ms->ti->table);
		ms->in_sync = 1;
	}
}

/*-----------------------------------------------------------------
 * Reads
 *---------------------------------------------------------------*/
static struct mirror *choose_mirror(struct mirror_set *ms, sector_t sector)
{
	/* FIXME: add read balancing */
	return ms->default_mirror;
}

/*
 * remap a buffer to a particular mirror.
 */
static void map_bio(struct mirror_set *ms, struct mirror *m, struct bio *bio)
{
	bio->bi_bdev = m->dev->bdev;
	bio->bi_sector = m->offset + (bio->bi_sector - ms->ti->begin);
}

static void do_reads(struct mirror_set *ms, struct bio_list *reads)
{
	region_t region;
	struct bio *bio;
	struct mirror *m;

	while ((bio = bio_list_pop(reads))) {
		region = bio_to_region(&ms->rh, bio);

		/*
		 * We can only read balance if the region is in sync.
		 */
		if (rh_in_sync(&ms->rh, region, 1))
			m = choose_mirror(ms, bio->bi_sector);
		else
			m = ms->default_mirror;

		map_bio(ms, m, bio);
		generic_make_request(bio);
	}
}

/*-----------------------------------------------------------------
 * Writes.
 *
 * We do different things with the write io depending on the
 * state of the region that it's in:
 *
 * SYNC: 	increment pending, use kcopyd to write to *all* mirrors
 * RECOVERING:	delay the io until recovery completes
 * NOSYNC:	increment pending, just write to the default mirror
 *---------------------------------------------------------------*/
static void write_callback(unsigned long error, void *context)
{
	unsigned int i;
	int uptodate = 1;
	struct bio *bio = (struct bio *) context;
	struct mirror_set *ms;

	ms = bio_get_ms(bio);
	bio_set_ms(bio, NULL);

	/*
	 * NOTE: We don't decrement the pending count here,
	 * instead it is done by the targets endio function.
	 * This way we handle both writes to SYNC and NOSYNC
	 * regions with the same code.
	 */

	if (error) {
		/*
		 * only error the io if all mirrors failed.
		 * FIXME: bogus
		 */
		uptodate = 0;
		for (i = 0; i < ms->nr_mirrors; i++)
			if (!test_bit(i, &error)) {
				uptodate = 1;
				break;
			}
	}
	bio_endio(bio, bio->bi_size, 0);
}

static void do_write(struct mirror_set *ms, struct bio *bio)
{
	unsigned int i;
	struct io_region io[KCOPYD_MAX_REGIONS+1];
	struct mirror *m;
	struct dm_io_request io_req = {
		.bi_rw = WRITE,
		.mem.type = DM_IO_BVEC,
		.mem.ptr.bvec = bio->bi_io_vec + bio->bi_idx,
		.notify.fn = write_callback,
		.notify.context = bio,
		.client = ms->io_client,
	};

	for (i = 0; i < ms->nr_mirrors; i++) {
		m = ms->mirror + i;

		io[i].bdev = m->dev->bdev;
		io[i].sector = m->offset + (bio->bi_sector - ms->ti->begin);
		io[i].count = bio->bi_size >> 9;
	}

	bio_set_ms(bio, ms);

	(void) dm_io(&io_req, ms->nr_mirrors, io, NULL);
}

static void do_writes(struct mirror_set *ms, struct bio_list *writes)
{
	int state;
	struct bio *bio;
	struct bio_list sync, nosync, recover, *this_list = NULL;

	if (!writes->head)
		return;

	/*
	 * Classify each write.
	 */
	bio_list_init(&sync);
	bio_list_init(&nosync);
	bio_list_init(&recover);

	while ((bio = bio_list_pop(writes))) {
		state = rh_state(&ms->rh, bio_to_region(&ms->rh, bio), 1);
		switch (state) {
		case RH_CLEAN:
		case RH_DIRTY:
			this_list = &sync;
			break;

		case RH_NOSYNC:
			this_list = &nosync;
			break;

		case RH_RECOVERING:
			this_list = &recover;
			break;
		}

		bio_list_add(this_list, bio);
	}

	/*
	 * Increment the pending counts for any regions that will
	 * be written to (writes to recover regions are going to
	 * be delayed).
	 */
	rh_inc_pending(&ms->rh, &sync);
	rh_inc_pending(&ms->rh, &nosync);
	rh_flush(&ms->rh);

	/*
	 * Dispatch io.
	 */
	while ((bio = bio_list_pop(&sync)))
		do_write(ms, bio);

	while ((bio = bio_list_pop(&recover)))
		rh_delay(&ms->rh, bio);

	while ((bio = bio_list_pop(&nosync))) {
		map_bio(ms, ms->default_mirror, bio);
		generic_make_request(bio);
	}
}

/*-----------------------------------------------------------------
 * kmirrord
 *---------------------------------------------------------------*/
static void do_mirror(struct work_struct *work)
{
	struct mirror_set *ms =container_of(work, struct mirror_set,
					    kmirrord_work);
	struct bio_list reads, writes;

	spin_lock(&ms->lock);
	reads = ms->reads;
	writes = ms->writes;
	bio_list_init(&ms->reads);
	bio_list_init(&ms->writes);
	spin_unlock(&ms->lock);

	rh_update_states(&ms->rh);
	do_recovery(ms);
	do_reads(ms, &reads);
	do_writes(ms, &writes);
}

/*-----------------------------------------------------------------
 * Target functions
 *---------------------------------------------------------------*/
static struct mirror_set *alloc_context(unsigned int nr_mirrors,
					uint32_t region_size,
					struct dm_target *ti,
					struct dirty_log *dl)
{
	size_t len;
	struct mirror_set *ms = NULL;

	if (array_too_big(sizeof(*ms), sizeof(ms->mirror[0]), nr_mirrors))
		return NULL;

	len = sizeof(*ms) + (sizeof(ms->mirror[0]) * nr_mirrors);

	ms = kmalloc(len, GFP_KERNEL);
	if (!ms) {
		ti->error = "Cannot allocate mirror context";
		return NULL;
	}

	memset(ms, 0, len);
	spin_lock_init(&ms->lock);

	ms->ti = ti;
	ms->nr_mirrors = nr_mirrors;
	ms->nr_regions = dm_sector_div_up(ti->len, region_size);
	ms->in_sync = 0;
	ms->default_mirror = &ms->mirror[DEFAULT_MIRROR];

	ms->io_client = dm_io_client_create(DM_IO_PAGES);
	if (IS_ERR(ms->io_client)) {
		ti->error = "Error creating dm_io client";
		kfree(ms);
 		return NULL;
	}

	if (rh_init(&ms->rh, ms, dl, region_size, ms->nr_regions)) {
		ti->error = "Error creating dirty region hash";
		kfree(ms);
		return NULL;
	}

	return ms;
}

static void free_context(struct mirror_set *ms, struct dm_target *ti,
			 unsigned int m)
{
	while (m--)
		dm_put_device(ti, ms->mirror[m].dev);

	dm_io_client_destroy(ms->io_client);
	rh_exit(&ms->rh);
	kfree(ms);
}

static inline int _check_region_size(struct dm_target *ti, uint32_t size)
{
	return !(size % (PAGE_SIZE >> 9) || (size & (size - 1)) ||
		 size > ti->len);
}

static int get_mirror(struct mirror_set *ms, struct dm_target *ti,
		      unsigned int mirror, char **argv)
{
	unsigned long long offset;

	if (sscanf(argv[1], "%llu", &offset) != 1) {
		ti->error = "Invalid offset";
		return -EINVAL;
	}

	if (dm_get_device(ti, argv[0], offset, ti->len,
			  dm_table_get_mode(ti->table),
			  &ms->mirror[mirror].dev)) {
		ti->error = "Device lookup failure";
		return -ENXIO;
	}

	ms->mirror[mirror].offset = offset;

	return 0;
}

/*
 * Create dirty log: log_type #log_params <log_params>
 */
static struct dirty_log *create_dirty_log(struct dm_target *ti,
					  unsigned int argc, char **argv,
					  unsigned int *args_used)
{
	unsigned int param_count;
	struct dirty_log *dl;

	if (argc < 2) {
		ti->error = "Insufficient mirror log arguments";
		return NULL;
	}

	if (sscanf(argv[1], "%u", &param_count) != 1) {
		ti->error = "Invalid mirror log argument count";
		return NULL;
	}

	*args_used = 2 + param_count;

	if (argc < *args_used) {
		ti->error = "Insufficient mirror log arguments";
		return NULL;
	}

	dl = dm_create_dirty_log(argv[0], ti, param_count, argv + 2);
	if (!dl) {
		ti->error = "Error creating mirror dirty log";
		return NULL;
	}

	if (!_check_region_size(ti, dl->type->get_region_size(dl))) {
		ti->error = "Invalid region size";
		dm_destroy_dirty_log(dl);
		return NULL;
	}

	return dl;
}

static int parse_features(struct mirror_set *ms, unsigned argc, char **argv,
			  unsigned *args_used)
{
	unsigned num_features;
	struct dm_target *ti = ms->ti;

	*args_used = 0;

	if (!argc)
		return 0;

	if (sscanf(argv[0], "%u", &num_features) != 1) {
		ti->error = "Invalid number of features";
		return -EINVAL;
	}

	argc--;
	argv++;
	(*args_used)++;

	if (num_features > argc) {
		ti->error = "Not enough arguments to support feature count";
		return -EINVAL;
	}

	if (!strcmp("handle_errors", argv[0]))
		ms->features |= DM_RAID1_HANDLE_ERRORS;
	else {
		ti->error = "Unrecognised feature requested";
		return -EINVAL;
	}

	(*args_used)++;

	return 0;
}

/*
 * Construct a mirror mapping:
 *
 * log_type #log_params <log_params>
 * #mirrors [mirror_path offset]{2,}
 * [#features <features>]
 *
 * log_type is "core" or "disk"
 * #log_params is between 1 and 3
 *
 * If present, features must be "handle_errors".
 */
static int mirror_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	int r;
	unsigned int nr_mirrors, m, args_used;
	struct mirror_set *ms;
	struct dirty_log *dl;

	dl = create_dirty_log(ti, argc, argv, &args_used);
	if (!dl)
		return -EINVAL;

	argv += args_used;
	argc -= args_used;

	if (!argc || sscanf(argv[0], "%u", &nr_mirrors) != 1 ||
	    nr_mirrors < 2 || nr_mirrors > KCOPYD_MAX_REGIONS + 1) {
		ti->error = "Invalid number of mirrors";
		dm_destroy_dirty_log(dl);
		return -EINVAL;
	}

	argv++, argc--;

	if (argc < nr_mirrors * 2) {
		ti->error = "Too few mirror arguments";
		dm_destroy_dirty_log(dl);
		return -EINVAL;
	}

	ms = alloc_context(nr_mirrors, dl->type->get_region_size(dl), ti, dl);
	if (!ms) {
		dm_destroy_dirty_log(dl);
		return -ENOMEM;
	}

	/* Get the mirror parameter sets */
	for (m = 0; m < nr_mirrors; m++) {
		r = get_mirror(ms, ti, m, argv);
		if (r) {
			free_context(ms, ti, m);
			return r;
		}
		argv += 2;
		argc -= 2;
	}

	ti->private = ms;
 	ti->split_io = ms->rh.region_size;

	ms->kmirrord_wq = create_singlethread_workqueue("kmirrord");
	if (!ms->kmirrord_wq) {
		DMERR("couldn't start kmirrord");
		free_context(ms, ti, m);
		return -ENOMEM;
	}
	INIT_WORK(&ms->kmirrord_work, do_mirror);

	r = parse_features(ms, argc, argv, &args_used);
	if (r) {
		free_context(ms, ti, ms->nr_mirrors);
		return r;
	}

	argv += args_used;
	argc -= args_used;

	if (argc) {
		ti->error = "Too many mirror arguments";
		free_context(ms, ti, ms->nr_mirrors);
		return -EINVAL;
	}

	r = kcopyd_client_create(DM_IO_PAGES, &ms->kcopyd_client);
	if (r) {
		destroy_workqueue(ms->kmirrord_wq);
		free_context(ms, ti, ms->nr_mirrors);
		return r;
	}

	wake(ms);
	return 0;
}

static void mirror_dtr(struct dm_target *ti)
{
	struct mirror_set *ms = (struct mirror_set *) ti->private;

	flush_workqueue(ms->kmirrord_wq);
	kcopyd_client_destroy(ms->kcopyd_client);
	destroy_workqueue(ms->kmirrord_wq);
	free_context(ms, ti, ms->nr_mirrors);
}

static void queue_bio(struct mirror_set *ms, struct bio *bio, int rw)
{
	int should_wake = 0;
	struct bio_list *bl;

	bl = (rw == WRITE) ? &ms->writes : &ms->reads;
	spin_lock(&ms->lock);
	should_wake = !(bl->head);
	bio_list_add(bl, bio);
	spin_unlock(&ms->lock);

	if (should_wake)
		wake(ms);
}

/*
 * Mirror mapping function
 */
static int mirror_map(struct dm_target *ti, struct bio *bio,
		      union map_info *map_context)
{
	int r, rw = bio_rw(bio);
	struct mirror *m;
	struct mirror_set *ms = ti->private;

	map_context->ll = bio_to_region(&ms->rh, bio);

	if (rw == WRITE) {
		queue_bio(ms, bio, rw);
		return DM_MAPIO_SUBMITTED;
	}

	r = ms->rh.log->type->in_sync(ms->rh.log,
				      bio_to_region(&ms->rh, bio), 0);
	if (r < 0 && r != -EWOULDBLOCK)
		return r;

	if (r == -EWOULDBLOCK)	/* FIXME: ugly */
		r = DM_MAPIO_SUBMITTED;

	/*
	 * We don't want to fast track a recovery just for a read
	 * ahead.  So we just let it silently fail.
	 * FIXME: get rid of this.
	 */
	if (!r && rw == READA)
		return -EIO;

	if (!r) {
		/* Pass this io over to the daemon */
		queue_bio(ms, bio, rw);
		return DM_MAPIO_SUBMITTED;
	}

	m = choose_mirror(ms, bio->bi_sector);
	if (!m)
		return -EIO;

	map_bio(ms, m, bio);
	return DM_MAPIO_REMAPPED;
}

static int mirror_end_io(struct dm_target *ti, struct bio *bio,
			 int error, union map_info *map_context)
{
	int rw = bio_rw(bio);
	struct mirror_set *ms = (struct mirror_set *) ti->private;
	region_t region = map_context->ll;

	/*
	 * We need to dec pending if this was a write.
	 */
	if (rw == WRITE)
		rh_dec(&ms->rh, region);

	return 0;
}

static void mirror_postsuspend(struct dm_target *ti)
{
	struct mirror_set *ms = (struct mirror_set *) ti->private;
	struct dirty_log *log = ms->rh.log;

	rh_stop_recovery(&ms->rh);

	/* Wait for all I/O we generated to complete */
	wait_event(_kmirrord_recovery_stopped,
		   !atomic_read(&ms->rh.recovery_in_flight));

	if (log->type->suspend && log->type->suspend(log))
		/* FIXME: need better error handling */
		DMWARN("log suspend failed");
}

static void mirror_resume(struct dm_target *ti)
{
	struct mirror_set *ms = (struct mirror_set *) ti->private;
	struct dirty_log *log = ms->rh.log;
	if (log->type->resume && log->type->resume(log))
		/* FIXME: need better error handling */
		DMWARN("log resume failed");
	rh_start_recovery(&ms->rh);
}

static int mirror_status(struct dm_target *ti, status_type_t type,
			 char *result, unsigned int maxlen)
{
	unsigned int m, sz = 0;
	struct mirror_set *ms = (struct mirror_set *) ti->private;

	switch (type) {
	case STATUSTYPE_INFO:
		DMEMIT("%d ", ms->nr_mirrors);
		for (m = 0; m < ms->nr_mirrors; m++)
			DMEMIT("%s ", ms->mirror[m].dev->name);

		DMEMIT("%llu/%llu",
			(unsigned long long)ms->rh.log->type->
				get_sync_count(ms->rh.log),
			(unsigned long long)ms->nr_regions);

		sz = ms->rh.log->type->status(ms->rh.log, type, result, maxlen);

		break;

	case STATUSTYPE_TABLE:
		sz = ms->rh.log->type->status(ms->rh.log, type, result, maxlen);

		DMEMIT("%d", ms->nr_mirrors);
		for (m = 0; m < ms->nr_mirrors; m++)
			DMEMIT(" %s %llu", ms->mirror[m].dev->name,
				(unsigned long long)ms->mirror[m].offset);

		if (ms->features & DM_RAID1_HANDLE_ERRORS)
			DMEMIT(" 1 handle_errors");
	}

	return 0;
}

static struct target_type mirror_target = {
	.name	 = "mirror",
	.version = {1, 0, 3},
	.module	 = THIS_MODULE,
	.ctr	 = mirror_ctr,
	.dtr	 = mirror_dtr,
	.map	 = mirror_map,
	.end_io	 = mirror_end_io,
	.postsuspend = mirror_postsuspend,
	.resume	 = mirror_resume,
	.status	 = mirror_status,
};

static int __init dm_mirror_init(void)
{
	int r;

	r = dm_dirty_log_init();
	if (r)
		return r;

	r = dm_register_target(&mirror_target);
	if (r < 0) {
		DMERR("%s: Failed to register mirror target",
		      mirror_target.name);
		dm_dirty_log_exit();
	}

	return r;
}

static void __exit dm_mirror_exit(void)
{
	int r;

	r = dm_unregister_target(&mirror_target);
	if (r < 0)
		DMERR("%s: unregister failed %d", mirror_target.name, r);

	dm_dirty_log_exit();
}

/* Module hooks */
module_init(dm_mirror_init);
module_exit(dm_mirror_exit);

MODULE_DESCRIPTION(DM_NAME " mirror target");
MODULE_AUTHOR("Joe Thornber");
MODULE_LICENSE("GPL");
