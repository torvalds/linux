/*
 * Copyright (C) 2003 Sistina Software Limited.
 *
 * This file is released under the GPL.
 */

#include "dm.h"
#include "dm-bio-list.h"
#include "dm-bio-record.h"

#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/mempool.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/log2.h>
#include <linux/hardirq.h>
#include <linux/dm-io.h>
#include <linux/dm-dirty-log.h>
#include <linux/dm-kcopyd.h>

#define DM_MSG_PREFIX "raid1"
#define DM_IO_PAGES 64

#define DM_RAID1_HANDLE_ERRORS 0x01
#define errors_handled(p)	((p)->features & DM_RAID1_HANDLE_ERRORS)

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
	struct dm_dirty_log *log;

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
	struct list_head failed_recovered_regions;
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
enum dm_raid1_error {
	DM_RAID1_WRITE_ERROR,
	DM_RAID1_SYNC_ERROR,
	DM_RAID1_READ_ERROR
};

struct mirror {
	struct mirror_set *ms;
	atomic_t error_count;
	unsigned long error_type;
	struct dm_dev *dev;
	sector_t offset;
};

struct mirror_set {
	struct dm_target *ti;
	struct list_head list;
	struct region_hash rh;
	struct dm_kcopyd_client *kcopyd_client;
	uint64_t features;

	spinlock_t lock;	/* protects the lists */
	struct bio_list reads;
	struct bio_list writes;
	struct bio_list failures;

	struct dm_io_client *io_client;
	mempool_t *read_record_pool;

	/* recovery */
	region_t nr_regions;
	int in_sync;
	int log_failure;
	atomic_t suspend;

	atomic_t default_mirror;	/* Default mirror */

	struct workqueue_struct *kmirrord_wq;
	struct work_struct kmirrord_work;
	struct timer_list timer;
	unsigned long timer_pending;

	struct work_struct trigger_event;

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

static void delayed_wake_fn(unsigned long data)
{
	struct mirror_set *ms = (struct mirror_set *) data;

	clear_bit(0, &ms->timer_pending);
	wake(ms);
}

static void delayed_wake(struct mirror_set *ms)
{
	if (test_and_set_bit(0, &ms->timer_pending))
		return;

	ms->timer.expires = jiffies + HZ / 5;
	ms->timer.data = (unsigned long) ms;
	ms->timer.function = delayed_wake_fn;
	add_timer(&ms->timer);
}

/* FIXME move this */
static void queue_bio(struct mirror_set *ms, struct bio *bio, int rw);

#define MIN_REGIONS 64
#define MAX_RECOVERY 1
static int rh_init(struct region_hash *rh, struct mirror_set *ms,
		   struct dm_dirty_log *log, uint32_t region_size,
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
	INIT_LIST_HEAD(&rh->failed_recovered_regions);

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
		dm_dirty_log_destroy(rh->log);
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

	/*
	 * Dispatch the bios before we call 'wake_up_all'.
	 * This is important because if we are suspending,
	 * we want to know that recovery is complete and
	 * the work queue is flushed.  If we wake_up_all
	 * before we dispatch_bios (queue bios and call wake()),
	 * then we risk suspending before the work queue
	 * has been properly flushed.
	 */
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
	LIST_HEAD(failed_recovered);

	/*
	 * Quickly grab the lists.
	 */
	write_lock_irq(&rh->hash_lock);
	spin_lock(&rh->region_lock);
	if (!list_empty(&rh->clean_regions)) {
		list_splice_init(&rh->clean_regions, &clean);

		list_for_each_entry(reg, &clean, list)
			list_del(&reg->hash_list);
	}

	if (!list_empty(&rh->recovered_regions)) {
		list_splice_init(&rh->recovered_regions, &recovered);

		list_for_each_entry (reg, &recovered, list)
			list_del(&reg->hash_list);
	}

	if (!list_empty(&rh->failed_recovered_regions)) {
		list_splice_init(&rh->failed_recovered_regions,
				 &failed_recovered);

		list_for_each_entry(reg, &failed_recovered, list)
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

	list_for_each_entry_safe(reg, next, &failed_recovered, list) {
		complete_resync_work(reg, errors_handled(rh->ms) ? 0 : 1);
		mempool_free(reg, rh->region_pool);
	}

	list_for_each_entry_safe(reg, next, &clean, list) {
		rh->log->type->clear_region(rh->log, reg->key);
		mempool_free(reg, rh->region_pool);
	}

	rh->log->type->flush(rh->log);
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

static void rh_recovery_end(struct region *reg, int success)
{
	struct region_hash *rh = reg->rh;

	spin_lock_irq(&rh->region_lock);
	if (success)
		list_add(&reg->list, &reg->rh->recovered_regions);
	else {
		reg->state = RH_NOSYNC;
		list_add(&reg->list, &reg->rh->failed_recovered_regions);
	}
	spin_unlock_irq(&rh->region_lock);

	wake(rh->ms);
}

static int rh_flush(struct region_hash *rh)
{
	return rh->log->type->flush(rh->log);
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

#define MIN_READ_RECORDS 20
struct dm_raid1_read_record {
	struct mirror *m;
	struct dm_bio_details details;
};

/*
 * Every mirror should look like this one.
 */
#define DEFAULT_MIRROR 0

/*
 * This is yucky.  We squirrel the mirror struct away inside
 * bi_next for read/write buffers.  This is safe since the bh
 * doesn't get submitted to the lower levels of block layer.
 */
static struct mirror *bio_get_m(struct bio *bio)
{
	return (struct mirror *) bio->bi_next;
}

static void bio_set_m(struct bio *bio, struct mirror *m)
{
	bio->bi_next = (struct bio *) m;
}

static struct mirror *get_default_mirror(struct mirror_set *ms)
{
	return &ms->mirror[atomic_read(&ms->default_mirror)];
}

static void set_default_mirror(struct mirror *m)
{
	struct mirror_set *ms = m->ms;
	struct mirror *m0 = &(ms->mirror[0]);

	atomic_set(&ms->default_mirror, m - m0);
}

/* fail_mirror
 * @m: mirror device to fail
 * @error_type: one of the enum's, DM_RAID1_*_ERROR
 *
 * If errors are being handled, record the type of
 * error encountered for this device.  If this type
 * of error has already been recorded, we can return;
 * otherwise, we must signal userspace by triggering
 * an event.  Additionally, if the device is the
 * primary device, we must choose a new primary, but
 * only if the mirror is in-sync.
 *
 * This function must not block.
 */
static void fail_mirror(struct mirror *m, enum dm_raid1_error error_type)
{
	struct mirror_set *ms = m->ms;
	struct mirror *new;

	if (!errors_handled(ms))
		return;

	/*
	 * error_count is used for nothing more than a
	 * simple way to tell if a device has encountered
	 * errors.
	 */
	atomic_inc(&m->error_count);

	if (test_and_set_bit(error_type, &m->error_type))
		return;

	if (m != get_default_mirror(ms))
		goto out;

	if (!ms->in_sync) {
		/*
		 * Better to issue requests to same failing device
		 * than to risk returning corrupt data.
		 */
		DMERR("Primary mirror (%s) failed while out-of-sync: "
		      "Reads may fail.", m->dev->name);
		goto out;
	}

	for (new = ms->mirror; new < ms->mirror + ms->nr_mirrors; new++)
		if (!atomic_read(&new->error_count)) {
			set_default_mirror(new);
			break;
		}

	if (unlikely(new == ms->mirror + ms->nr_mirrors))
		DMWARN("All sides of mirror have failed.");

out:
	schedule_work(&ms->trigger_event);
}

/*-----------------------------------------------------------------
 * Recovery.
 *
 * When a mirror is first activated we may find that some regions
 * are in the no-sync state.  We have to recover these by
 * recopying from the default mirror to all the others.
 *---------------------------------------------------------------*/
static void recovery_complete(int read_err, unsigned long write_err,
			      void *context)
{
	struct region *reg = (struct region *)context;
	struct mirror_set *ms = reg->rh->ms;
	int m, bit = 0;

	if (read_err) {
		/* Read error means the failure of default mirror. */
		DMERR_LIMIT("Unable to read primary mirror during recovery");
		fail_mirror(get_default_mirror(ms), DM_RAID1_SYNC_ERROR);
	}

	if (write_err) {
		DMERR_LIMIT("Write error during recovery (error = 0x%lx)",
			    write_err);
		/*
		 * Bits correspond to devices (excluding default mirror).
		 * The default mirror cannot change during recovery.
		 */
		for (m = 0; m < ms->nr_mirrors; m++) {
			if (&ms->mirror[m] == get_default_mirror(ms))
				continue;
			if (test_bit(bit, &write_err))
				fail_mirror(ms->mirror + m,
					    DM_RAID1_SYNC_ERROR);
			bit++;
		}
	}

	rh_recovery_end(reg, !(read_err || write_err));
}

static int recover(struct mirror_set *ms, struct region *reg)
{
	int r;
	unsigned int i;
	struct dm_io_region from, to[DM_KCOPYD_MAX_REGIONS], *dest;
	struct mirror *m;
	unsigned long flags = 0;

	/* fill in the source */
	m = get_default_mirror(ms);
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
		if (&ms->mirror[i] == get_default_mirror(ms))
			continue;

		m = ms->mirror + i;
		dest->bdev = m->dev->bdev;
		dest->sector = m->offset + region_to_sector(reg->rh, reg->key);
		dest->count = from.count;
		dest++;
	}

	/* hand to kcopyd */
	set_bit(DM_KCOPYD_IGNORE_ERROR, &flags);
	r = dm_kcopyd_copy(ms->kcopyd_client, &from, ms->nr_mirrors - 1, to,
			   flags, recovery_complete, reg);

	return r;
}

static void do_recovery(struct mirror_set *ms)
{
	int r;
	struct region *reg;
	struct dm_dirty_log *log = ms->rh.log;

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
	struct mirror *m = get_default_mirror(ms);

	do {
		if (likely(!atomic_read(&m->error_count)))
			return m;

		if (m-- == ms->mirror)
			m += ms->nr_mirrors;
	} while (m != get_default_mirror(ms));

	return NULL;
}

static int default_ok(struct mirror *m)
{
	struct mirror *default_mirror = get_default_mirror(m->ms);

	return !atomic_read(&default_mirror->error_count);
}

static int mirror_available(struct mirror_set *ms, struct bio *bio)
{
	region_t region = bio_to_region(&ms->rh, bio);

	if (ms->rh.log->type->in_sync(ms->rh.log, region, 0))
		return choose_mirror(ms,  bio->bi_sector) ? 1 : 0;

	return 0;
}

/*
 * remap a buffer to a particular mirror.
 */
static sector_t map_sector(struct mirror *m, struct bio *bio)
{
	return m->offset + (bio->bi_sector - m->ms->ti->begin);
}

static void map_bio(struct mirror *m, struct bio *bio)
{
	bio->bi_bdev = m->dev->bdev;
	bio->bi_sector = map_sector(m, bio);
}

static void map_region(struct dm_io_region *io, struct mirror *m,
		       struct bio *bio)
{
	io->bdev = m->dev->bdev;
	io->sector = map_sector(m, bio);
	io->count = bio->bi_size >> 9;
}

/*-----------------------------------------------------------------
 * Reads
 *---------------------------------------------------------------*/
static void read_callback(unsigned long error, void *context)
{
	struct bio *bio = context;
	struct mirror *m;

	m = bio_get_m(bio);
	bio_set_m(bio, NULL);

	if (likely(!error)) {
		bio_endio(bio, 0);
		return;
	}

	fail_mirror(m, DM_RAID1_READ_ERROR);

	if (likely(default_ok(m)) || mirror_available(m->ms, bio)) {
		DMWARN_LIMIT("Read failure on mirror device %s.  "
			     "Trying alternative device.",
			     m->dev->name);
		queue_bio(m->ms, bio, bio_rw(bio));
		return;
	}

	DMERR_LIMIT("Read failure on mirror device %s.  Failing I/O.",
		    m->dev->name);
	bio_endio(bio, -EIO);
}

/* Asynchronous read. */
static void read_async_bio(struct mirror *m, struct bio *bio)
{
	struct dm_io_region io;
	struct dm_io_request io_req = {
		.bi_rw = READ,
		.mem.type = DM_IO_BVEC,
		.mem.ptr.bvec = bio->bi_io_vec + bio->bi_idx,
		.notify.fn = read_callback,
		.notify.context = bio,
		.client = m->ms->io_client,
	};

	map_region(&io, m, bio);
	bio_set_m(bio, m);
	(void) dm_io(&io_req, 1, &io, NULL);
}

static void do_reads(struct mirror_set *ms, struct bio_list *reads)
{
	region_t region;
	struct bio *bio;
	struct mirror *m;

	while ((bio = bio_list_pop(reads))) {
		region = bio_to_region(&ms->rh, bio);
		m = get_default_mirror(ms);

		/*
		 * We can only read balance if the region is in sync.
		 */
		if (likely(rh_in_sync(&ms->rh, region, 1)))
			m = choose_mirror(ms, bio->bi_sector);
		else if (m && atomic_read(&m->error_count))
			m = NULL;

		if (likely(m))
			read_async_bio(m, bio);
		else
			bio_endio(bio, -EIO);
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

/* __bio_mark_nosync
 * @ms
 * @bio
 * @done
 * @error
 *
 * The bio was written on some mirror(s) but failed on other mirror(s).
 * We can successfully endio the bio but should avoid the region being
 * marked clean by setting the state RH_NOSYNC.
 *
 * This function is _not_ safe in interrupt context!
 */
static void __bio_mark_nosync(struct mirror_set *ms,
			      struct bio *bio, unsigned done, int error)
{
	unsigned long flags;
	struct region_hash *rh = &ms->rh;
	struct dm_dirty_log *log = ms->rh.log;
	struct region *reg;
	region_t region = bio_to_region(rh, bio);
	int recovering = 0;

	/* We must inform the log that the sync count has changed. */
	log->type->set_region_sync(log, region, 0);
	ms->in_sync = 0;

	read_lock(&rh->hash_lock);
	reg = __rh_find(rh, region);
	read_unlock(&rh->hash_lock);

	/* region hash entry should exist because write was in-flight */
	BUG_ON(!reg);
	BUG_ON(!list_empty(&reg->list));

	spin_lock_irqsave(&rh->region_lock, flags);
	/*
	 * Possible cases:
	 *   1) RH_DIRTY
	 *   2) RH_NOSYNC: was dirty, other preceeding writes failed
	 *   3) RH_RECOVERING: flushing pending writes
	 * Either case, the region should have not been connected to list.
	 */
	recovering = (reg->state == RH_RECOVERING);
	reg->state = RH_NOSYNC;
	BUG_ON(!list_empty(&reg->list));
	spin_unlock_irqrestore(&rh->region_lock, flags);

	bio_endio(bio, error);
	if (recovering)
		complete_resync_work(reg, 0);
}

static void write_callback(unsigned long error, void *context)
{
	unsigned i, ret = 0;
	struct bio *bio = (struct bio *) context;
	struct mirror_set *ms;
	int uptodate = 0;
	int should_wake = 0;
	unsigned long flags;

	ms = bio_get_m(bio)->ms;
	bio_set_m(bio, NULL);

	/*
	 * NOTE: We don't decrement the pending count here,
	 * instead it is done by the targets endio function.
	 * This way we handle both writes to SYNC and NOSYNC
	 * regions with the same code.
	 */
	if (likely(!error))
		goto out;

	for (i = 0; i < ms->nr_mirrors; i++)
		if (test_bit(i, &error))
			fail_mirror(ms->mirror + i, DM_RAID1_WRITE_ERROR);
		else
			uptodate = 1;

	if (unlikely(!uptodate)) {
		DMERR("All replicated volumes dead, failing I/O");
		/* None of the writes succeeded, fail the I/O. */
		ret = -EIO;
	} else if (errors_handled(ms)) {
		/*
		 * Need to raise event.  Since raising
		 * events can block, we need to do it in
		 * the main thread.
		 */
		spin_lock_irqsave(&ms->lock, flags);
		if (!ms->failures.head)
			should_wake = 1;
		bio_list_add(&ms->failures, bio);
		spin_unlock_irqrestore(&ms->lock, flags);
		if (should_wake)
			wake(ms);
		return;
	}
out:
	bio_endio(bio, ret);
}

static void do_write(struct mirror_set *ms, struct bio *bio)
{
	unsigned int i;
	struct dm_io_region io[ms->nr_mirrors], *dest = io;
	struct mirror *m;
	struct dm_io_request io_req = {
		.bi_rw = WRITE,
		.mem.type = DM_IO_BVEC,
		.mem.ptr.bvec = bio->bi_io_vec + bio->bi_idx,
		.notify.fn = write_callback,
		.notify.context = bio,
		.client = ms->io_client,
	};

	for (i = 0, m = ms->mirror; i < ms->nr_mirrors; i++, m++)
		map_region(dest++, m, bio);

	/*
	 * Use default mirror because we only need it to retrieve the reference
	 * to the mirror set in write_callback().
	 */
	bio_set_m(bio, get_default_mirror(ms));

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
	ms->log_failure = rh_flush(&ms->rh) ? 1 : 0;

	/*
	 * Dispatch io.
	 */
	if (unlikely(ms->log_failure)) {
		spin_lock_irq(&ms->lock);
		bio_list_merge(&ms->failures, &sync);
		spin_unlock_irq(&ms->lock);
		wake(ms);
	} else
		while ((bio = bio_list_pop(&sync)))
			do_write(ms, bio);

	while ((bio = bio_list_pop(&recover)))
		rh_delay(&ms->rh, bio);

	while ((bio = bio_list_pop(&nosync))) {
		map_bio(get_default_mirror(ms), bio);
		generic_make_request(bio);
	}
}

static void do_failures(struct mirror_set *ms, struct bio_list *failures)
{
	struct bio *bio;

	if (!failures->head)
		return;

	if (!ms->log_failure) {
		while ((bio = bio_list_pop(failures)))
			__bio_mark_nosync(ms, bio, bio->bi_size, 0);
		return;
	}

	/*
	 * If the log has failed, unattempted writes are being
	 * put on the failures list.  We can't issue those writes
	 * until a log has been marked, so we must store them.
	 *
	 * If a 'noflush' suspend is in progress, we can requeue
	 * the I/O's to the core.  This give userspace a chance
	 * to reconfigure the mirror, at which point the core
	 * will reissue the writes.  If the 'noflush' flag is
	 * not set, we have no choice but to return errors.
	 *
	 * Some writes on the failures list may have been
	 * submitted before the log failure and represent a
	 * failure to write to one of the devices.  It is ok
	 * for us to treat them the same and requeue them
	 * as well.
	 */
	if (dm_noflush_suspending(ms->ti)) {
		while ((bio = bio_list_pop(failures)))
			bio_endio(bio, DM_ENDIO_REQUEUE);
		return;
	}

	if (atomic_read(&ms->suspend)) {
		while ((bio = bio_list_pop(failures)))
			bio_endio(bio, -EIO);
		return;
	}

	spin_lock_irq(&ms->lock);
	bio_list_merge(&ms->failures, failures);
	spin_unlock_irq(&ms->lock);

	delayed_wake(ms);
}

static void trigger_event(struct work_struct *work)
{
	struct mirror_set *ms =
		container_of(work, struct mirror_set, trigger_event);

	dm_table_event(ms->ti->table);
}

/*-----------------------------------------------------------------
 * kmirrord
 *---------------------------------------------------------------*/
static void do_mirror(struct work_struct *work)
{
	struct mirror_set *ms =container_of(work, struct mirror_set,
					    kmirrord_work);
	struct bio_list reads, writes, failures;
	unsigned long flags;

	spin_lock_irqsave(&ms->lock, flags);
	reads = ms->reads;
	writes = ms->writes;
	failures = ms->failures;
	bio_list_init(&ms->reads);
	bio_list_init(&ms->writes);
	bio_list_init(&ms->failures);
	spin_unlock_irqrestore(&ms->lock, flags);

	rh_update_states(&ms->rh);
	do_recovery(ms);
	do_reads(ms, &reads);
	do_writes(ms, &writes);
	do_failures(ms, &failures);

	dm_table_unplug_all(ms->ti->table);
}


/*-----------------------------------------------------------------
 * Target functions
 *---------------------------------------------------------------*/
static struct mirror_set *alloc_context(unsigned int nr_mirrors,
					uint32_t region_size,
					struct dm_target *ti,
					struct dm_dirty_log *dl)
{
	size_t len;
	struct mirror_set *ms = NULL;

	if (array_too_big(sizeof(*ms), sizeof(ms->mirror[0]), nr_mirrors))
		return NULL;

	len = sizeof(*ms) + (sizeof(ms->mirror[0]) * nr_mirrors);

	ms = kzalloc(len, GFP_KERNEL);
	if (!ms) {
		ti->error = "Cannot allocate mirror context";
		return NULL;
	}

	spin_lock_init(&ms->lock);

	ms->ti = ti;
	ms->nr_mirrors = nr_mirrors;
	ms->nr_regions = dm_sector_div_up(ti->len, region_size);
	ms->in_sync = 0;
	ms->log_failure = 0;
	atomic_set(&ms->suspend, 0);
	atomic_set(&ms->default_mirror, DEFAULT_MIRROR);

	len = sizeof(struct dm_raid1_read_record);
	ms->read_record_pool = mempool_create_kmalloc_pool(MIN_READ_RECORDS,
							   len);
	if (!ms->read_record_pool) {
		ti->error = "Error creating mirror read_record_pool";
		kfree(ms);
		return NULL;
	}

	ms->io_client = dm_io_client_create(DM_IO_PAGES);
	if (IS_ERR(ms->io_client)) {
		ti->error = "Error creating dm_io client";
		mempool_destroy(ms->read_record_pool);
		kfree(ms);
 		return NULL;
	}

	if (rh_init(&ms->rh, ms, dl, region_size, ms->nr_regions)) {
		ti->error = "Error creating dirty region hash";
		dm_io_client_destroy(ms->io_client);
		mempool_destroy(ms->read_record_pool);
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
	mempool_destroy(ms->read_record_pool);
	kfree(ms);
}

static inline int _check_region_size(struct dm_target *ti, uint32_t size)
{
	return !(size % (PAGE_SIZE >> 9) || !is_power_of_2(size) ||
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

	ms->mirror[mirror].ms = ms;
	atomic_set(&(ms->mirror[mirror].error_count), 0);
	ms->mirror[mirror].error_type = 0;
	ms->mirror[mirror].offset = offset;

	return 0;
}

/*
 * Create dirty log: log_type #log_params <log_params>
 */
static struct dm_dirty_log *create_dirty_log(struct dm_target *ti,
					  unsigned int argc, char **argv,
					  unsigned int *args_used)
{
	unsigned int param_count;
	struct dm_dirty_log *dl;

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

	dl = dm_dirty_log_create(argv[0], ti, param_count, argv + 2);
	if (!dl) {
		ti->error = "Error creating mirror dirty log";
		return NULL;
	}

	if (!_check_region_size(ti, dl->type->get_region_size(dl))) {
		ti->error = "Invalid region size";
		dm_dirty_log_destroy(dl);
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
	struct dm_dirty_log *dl;

	dl = create_dirty_log(ti, argc, argv, &args_used);
	if (!dl)
		return -EINVAL;

	argv += args_used;
	argc -= args_used;

	if (!argc || sscanf(argv[0], "%u", &nr_mirrors) != 1 ||
	    nr_mirrors < 2 || nr_mirrors > DM_KCOPYD_MAX_REGIONS + 1) {
		ti->error = "Invalid number of mirrors";
		dm_dirty_log_destroy(dl);
		return -EINVAL;
	}

	argv++, argc--;

	if (argc < nr_mirrors * 2) {
		ti->error = "Too few mirror arguments";
		dm_dirty_log_destroy(dl);
		return -EINVAL;
	}

	ms = alloc_context(nr_mirrors, dl->type->get_region_size(dl), ti, dl);
	if (!ms) {
		dm_dirty_log_destroy(dl);
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
		r = -ENOMEM;
		goto err_free_context;
	}
	INIT_WORK(&ms->kmirrord_work, do_mirror);
	init_timer(&ms->timer);
	ms->timer_pending = 0;
	INIT_WORK(&ms->trigger_event, trigger_event);

	r = parse_features(ms, argc, argv, &args_used);
	if (r)
		goto err_destroy_wq;

	argv += args_used;
	argc -= args_used;

	/*
	 * Any read-balancing addition depends on the
	 * DM_RAID1_HANDLE_ERRORS flag being present.
	 * This is because the decision to balance depends
	 * on the sync state of a region.  If the above
	 * flag is not present, we ignore errors; and
	 * the sync state may be inaccurate.
	 */

	if (argc) {
		ti->error = "Too many mirror arguments";
		r = -EINVAL;
		goto err_destroy_wq;
	}

	r = dm_kcopyd_client_create(DM_IO_PAGES, &ms->kcopyd_client);
	if (r)
		goto err_destroy_wq;

	wake(ms);
	return 0;

err_destroy_wq:
	destroy_workqueue(ms->kmirrord_wq);
err_free_context:
	free_context(ms, ti, ms->nr_mirrors);
	return r;
}

static void mirror_dtr(struct dm_target *ti)
{
	struct mirror_set *ms = (struct mirror_set *) ti->private;

	del_timer_sync(&ms->timer);
	flush_workqueue(ms->kmirrord_wq);
	dm_kcopyd_client_destroy(ms->kcopyd_client);
	destroy_workqueue(ms->kmirrord_wq);
	free_context(ms, ti, ms->nr_mirrors);
}

static void queue_bio(struct mirror_set *ms, struct bio *bio, int rw)
{
	unsigned long flags;
	int should_wake = 0;
	struct bio_list *bl;

	bl = (rw == WRITE) ? &ms->writes : &ms->reads;
	spin_lock_irqsave(&ms->lock, flags);
	should_wake = !(bl->head);
	bio_list_add(bl, bio);
	spin_unlock_irqrestore(&ms->lock, flags);

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
	struct dm_raid1_read_record *read_record = NULL;

	if (rw == WRITE) {
		/* Save region for mirror_end_io() handler */
		map_context->ll = bio_to_region(&ms->rh, bio);
		queue_bio(ms, bio, rw);
		return DM_MAPIO_SUBMITTED;
	}

	r = ms->rh.log->type->in_sync(ms->rh.log,
				      bio_to_region(&ms->rh, bio), 0);
	if (r < 0 && r != -EWOULDBLOCK)
		return r;

	/*
	 * If region is not in-sync queue the bio.
	 */
	if (!r || (r == -EWOULDBLOCK)) {
		if (rw == READA)
			return -EWOULDBLOCK;

		queue_bio(ms, bio, rw);
		return DM_MAPIO_SUBMITTED;
	}

	/*
	 * The region is in-sync and we can perform reads directly.
	 * Store enough information so we can retry if it fails.
	 */
	m = choose_mirror(ms, bio->bi_sector);
	if (unlikely(!m))
		return -EIO;

	read_record = mempool_alloc(ms->read_record_pool, GFP_NOIO);
	if (likely(read_record)) {
		dm_bio_record(&read_record->details, bio);
		map_context->ptr = read_record;
		read_record->m = m;
	}

	map_bio(m, bio);

	return DM_MAPIO_REMAPPED;
}

static int mirror_end_io(struct dm_target *ti, struct bio *bio,
			 int error, union map_info *map_context)
{
	int rw = bio_rw(bio);
	struct mirror_set *ms = (struct mirror_set *) ti->private;
	struct mirror *m = NULL;
	struct dm_bio_details *bd = NULL;
	struct dm_raid1_read_record *read_record = map_context->ptr;

	/*
	 * We need to dec pending if this was a write.
	 */
	if (rw == WRITE) {
		rh_dec(&ms->rh, map_context->ll);
		return error;
	}

	if (error == -EOPNOTSUPP)
		goto out;

	if ((error == -EWOULDBLOCK) && bio_rw_ahead(bio))
		goto out;

	if (unlikely(error)) {
		if (!read_record) {
			/*
			 * There wasn't enough memory to record necessary
			 * information for a retry or there was no other
			 * mirror in-sync.
			 */
			DMERR_LIMIT("Mirror read failed.");
			return -EIO;
		}

		m = read_record->m;

		DMERR("Mirror read failed from %s. Trying alternative device.",
		      m->dev->name);

		fail_mirror(m, DM_RAID1_READ_ERROR);

		/*
		 * A failed read is requeued for another attempt using an intact
		 * mirror.
		 */
		if (default_ok(m) || mirror_available(ms, bio)) {
			bd = &read_record->details;

			dm_bio_restore(bd, bio);
			mempool_free(read_record, ms->read_record_pool);
			map_context->ptr = NULL;
			queue_bio(ms, bio, rw);
			return 1;
		}
		DMERR("All replicated volumes dead, failing I/O");
	}

out:
	if (read_record) {
		mempool_free(read_record, ms->read_record_pool);
		map_context->ptr = NULL;
	}

	return error;
}

static void mirror_presuspend(struct dm_target *ti)
{
	struct mirror_set *ms = (struct mirror_set *) ti->private;
	struct dm_dirty_log *log = ms->rh.log;

	atomic_set(&ms->suspend, 1);

	/*
	 * We must finish up all the work that we've
	 * generated (i.e. recovery work).
	 */
	rh_stop_recovery(&ms->rh);

	wait_event(_kmirrord_recovery_stopped,
		   !atomic_read(&ms->rh.recovery_in_flight));

	if (log->type->presuspend && log->type->presuspend(log))
		/* FIXME: need better error handling */
		DMWARN("log presuspend failed");

	/*
	 * Now that recovery is complete/stopped and the
	 * delayed bios are queued, we need to wait for
	 * the worker thread to complete.  This way,
	 * we know that all of our I/O has been pushed.
	 */
	flush_workqueue(ms->kmirrord_wq);
}

static void mirror_postsuspend(struct dm_target *ti)
{
	struct mirror_set *ms = ti->private;
	struct dm_dirty_log *log = ms->rh.log;

	if (log->type->postsuspend && log->type->postsuspend(log))
		/* FIXME: need better error handling */
		DMWARN("log postsuspend failed");
}

static void mirror_resume(struct dm_target *ti)
{
	struct mirror_set *ms = ti->private;
	struct dm_dirty_log *log = ms->rh.log;

	atomic_set(&ms->suspend, 0);
	if (log->type->resume && log->type->resume(log))
		/* FIXME: need better error handling */
		DMWARN("log resume failed");
	rh_start_recovery(&ms->rh);
}

/*
 * device_status_char
 * @m: mirror device/leg we want the status of
 *
 * We return one character representing the most severe error
 * we have encountered.
 *    A => Alive - No failures
 *    D => Dead - A write failure occurred leaving mirror out-of-sync
 *    S => Sync - A sychronization failure occurred, mirror out-of-sync
 *    R => Read - A read failure occurred, mirror data unaffected
 *
 * Returns: <char>
 */
static char device_status_char(struct mirror *m)
{
	if (!atomic_read(&(m->error_count)))
		return 'A';

	return (test_bit(DM_RAID1_WRITE_ERROR, &(m->error_type))) ? 'D' :
		(test_bit(DM_RAID1_SYNC_ERROR, &(m->error_type))) ? 'S' :
		(test_bit(DM_RAID1_READ_ERROR, &(m->error_type))) ? 'R' : 'U';
}


static int mirror_status(struct dm_target *ti, status_type_t type,
			 char *result, unsigned int maxlen)
{
	unsigned int m, sz = 0;
	struct mirror_set *ms = (struct mirror_set *) ti->private;
	struct dm_dirty_log *log = ms->rh.log;
	char buffer[ms->nr_mirrors + 1];

	switch (type) {
	case STATUSTYPE_INFO:
		DMEMIT("%d ", ms->nr_mirrors);
		for (m = 0; m < ms->nr_mirrors; m++) {
			DMEMIT("%s ", ms->mirror[m].dev->name);
			buffer[m] = device_status_char(&(ms->mirror[m]));
		}
		buffer[m] = '\0';

		DMEMIT("%llu/%llu 1 %s ",
		      (unsigned long long)log->type->get_sync_count(ms->rh.log),
		      (unsigned long long)ms->nr_regions, buffer);

		sz += log->type->status(ms->rh.log, type, result+sz, maxlen-sz);

		break;

	case STATUSTYPE_TABLE:
		sz = log->type->status(ms->rh.log, type, result, maxlen);

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
	.version = {1, 0, 20},
	.module	 = THIS_MODULE,
	.ctr	 = mirror_ctr,
	.dtr	 = mirror_dtr,
	.map	 = mirror_map,
	.end_io	 = mirror_end_io,
	.presuspend = mirror_presuspend,
	.postsuspend = mirror_postsuspend,
	.resume	 = mirror_resume,
	.status	 = mirror_status,
};

static int __init dm_mirror_init(void)
{
	int r;

	r = dm_register_target(&mirror_target);
	if (r < 0)
		DMERR("Failed to register mirror target");

	return r;
}

static void __exit dm_mirror_exit(void)
{
	int r;

	r = dm_unregister_target(&mirror_target);
	if (r < 0)
		DMERR("unregister failed %d", r);
}

/* Module hooks */
module_init(dm_mirror_init);
module_exit(dm_mirror_exit);

MODULE_DESCRIPTION(DM_NAME " mirror target");
MODULE_AUTHOR("Joe Thornber");
MODULE_LICENSE("GPL");
